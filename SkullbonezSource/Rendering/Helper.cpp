/*
File: SkullbonezSource/Rendering/Helper.cpp
Purpose:
  Collects legacy helper routines that bridge engine subsystems.

Mental model:
  Runtime code connects authored scene data, input, simulation, render
  backends, and validation-oriented launch modes. Follow who owns state and
  when that state changes.

Glossary:
  Asset system: Runtime-owned registry borrowed to resolve helper shader source
  while helper-owned shader handles remain backend resources.
  Cbuffer (Constant Buffer): Shader constant block uploaded once before a draw.
  Material table: Fixed t4 texture that stores default per-kind material response
  values for object shaders.
  Instance payload: Per-object data appended after the model matrix in an
  instanced draw stream.
  Resource factory: Borrowed backend lifetime interface used to create or
  destroy helper-owned mesh, material-table, and dynamic-buffer handles.
  Validation gate: Repository script that proves a class of changes before
  commit or PR.

Invariants:
  - C++ constant-buffer structs must match reflected HLSL cbuffer size and
    field order, or the draw that depends on them must be skipped.
  - Static mesh/shader handles are backend resources and must be reset before
    backend teardown or recreation.
  - Helper functions may cache opaque backend handles, but every create, delete,
    state mutation, and draw submission must use the caller-borrowed render
    capability for the current backend/frame.

Related:
  - SkullbonezSource/Rendering/Helper.h
  - Agentic/Reference/runtime-reference.md
  - Agentic/Reference/comment-style-guide.md
*/
#include "Helper.h"
#include "../Assets/AssetSystem.h"
#include "../Physics/ConvexHullShape.h"
#include "../Core/Profiler.h"
#include "IRenderCommandContext.h"
#include "IRenderResourceFactory.h"
#include "PrimitiveMeshBuilder.h"

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstring>
#include <vector>


using namespace SkullbonezCore::Basics;
using namespace SkullbonezCore::Rendering;
using namespace SkullbonezCore::Math::CollisionDetection;
using namespace SkullbonezCore::Math::Transformation;
using namespace SkullbonezCore::Math::Vector;


std::unique_ptr<IShader> RenderHelper::sphereShader;
std::unique_ptr<IShader> RenderHelper::shadowDepthShader;
uint32_t RenderHelper::sphereInstMesh = 0;
int RenderHelper::sphereVertexCount = 0;
std::vector<float> RenderHelper::sphereInstanceData;
uint32_t RenderHelper::lowPolySphereInstMesh = 0;
int RenderHelper::lowPolySphereVertexCount = 0;
uint32_t RenderHelper::activeSphereInstMesh = 0;
int RenderHelper::activeSphereVertexCount = 0;
uint32_t RenderHelper::boxInstMesh = 0;
int RenderHelper::boxVertexCount = 0;
std::vector<float> RenderHelper::boxInstanceData;
uint32_t RenderHelper::pineInstMesh = 0;
int RenderHelper::pineVertexCount = 0;
std::vector<float> RenderHelper::pineInstanceData;

static constexpr int INSTANCE_MATRIX_FLOATS = 16;
static constexpr int INSTANCE_MATERIAL_FLOAT4_COUNT = 4;
static constexpr int INSTANCE_MATERIAL_FLOATS = INSTANCE_MATERIAL_FLOAT4_COUNT * 4;
static constexpr int INSTANCE_FLOATS = INSTANCE_MATRIX_FLOATS + INSTANCE_MATERIAL_FLOATS;
static constexpr int HULL_MAX_TRIANGLE_VERTICES =
    ConvexHullShape::MAX_FACES * ( ConvexHullShape::MAX_FACE_VERTICES - 2 ) * 3;
static constexpr int HULL_DYNAMIC_FLOATS_PER_VERTEX = 3 + 3 + 2 + INSTANCE_FLOATS;
static constexpr int PRIMITIVE_SHAPE_MESH = 0;
static constexpr int PRIMITIVE_SHAPE_SPHERE = 1;
static constexpr int MATERIAL_TABLE_WIDTH = 16;
static constexpr int MATERIAL_TABLE_TEXTURE_SLOT = 4;
static bool sSphereBatchTransparent = false;
static bool sBoxBatchTransparent = false;
static bool sPineBatchTransparent = false;
static bool sSphereBatchReady = false;
static bool sBoxBatchReady = false;
static bool sPineBatchReady = false;
static uint32_t sMaterialTableTexture = 0;
static uint32_t sConvexHullDynamicVB = 0;
static std::array<float, HULL_MAX_TRIANGLE_VERTICES * HULL_DYNAMIC_FLOATS_PER_VERTEX> sConvexHullVertexData = {};

// Layout contract: mirrors the Uniforms cbuffer in lit_textured_instanced.hlsl.
// SetConstantBufferBytes rejects this block if the reflected shader size drifts,
// which turns C++/HLSL packing mistakes into a skipped draw plus Debug log event
// instead of visually corrupting every object in the batch.
struct PrimitiveBatchShaderConstants
{
    Matrix4 view;
    Matrix4 projection;
    float clipPlane[4];
    float lightPosition[4];
    float lightAmbient[4];
    float lightDiffuse[4];
    float materialAmbient[4];
    float materialDiffuse[4];
    int objectStyle;
    int primitiveShape;
    float materialAlpha;
    float objectStylePad;
    Matrix4 shadowViewProj;
    float shadowParams[4];
    float shadowFlags[4];
};

// Layout contract: mirrors the Uniforms cbuffer in shadow_depth_instanced.hlsl.
// These matrices are the light-space view/projection for object shadow casters,
// not the viewer camera used by the visible object pass.
struct InstancedShadowDepthConstants
{
    Matrix4 view;
    Matrix4 projection;
    float clipPlane[4];
};

static void BeginPrimitiveBatchTransparency( IRenderCommandContext& renderCommands, bool isTransparent )
{
    if ( isTransparent )
    {
        renderCommands.SetBlend( true );
        renderCommands.SetBlendFunc( BlendFactor::SrcAlpha, BlendFactor::OneMinusSrcAlpha );
        renderCommands.SetDepthWrite( false );
    }
}

static void EndPrimitiveBatchTransparency( IRenderCommandContext& renderCommands, bool wasTransparent )
{
    if ( wasTransparent )
    {
        renderCommands.SetDepthWrite( true );
    }
    renderCommands.SetBlend( false );
}

static uint8_t MaterialByte( float value )
{
    // The material table is an 8-bit texture, so clamp and round normalized
    // material parameters at the CPU boundary before the shader samples them.
    return static_cast<uint8_t>( std::clamp( value, 0.0f, 1.0f ) * 255.0f + 0.5f );
}

static void EnsureMaterialTableTexture( IRenderResourceFactory& renderResources, IRenderCommandContext& renderCommands )
{
    // Concept: the current object material table is a tiny texture, not a
    // structured buffer or bindless descriptor table.
    //
    // Each texel row stores default roughness, metallic, specular, and
    // stylization for one RenderMaterialKind. The per-instance payload still
    // carries draw-local values; the t4 table gives shaders a stable fallback
    // and a validation-visible binding point without expanding the resource
    // model beyond the ordinary raster ABI.
    if ( sMaterialTableTexture != 0 )
    {
        renderCommands.BindTexture( sMaterialTableTexture, MATERIAL_TABLE_TEXTURE_SLOT );
        return;
    }

    uint8_t rows[MATERIAL_TABLE_WIDTH * 4] = {};
    for ( int i = 0; i < MATERIAL_TABLE_WIDTH; ++i )
    {
        RenderMaterial material;
        material.kind =
            static_cast<RenderMaterialKind>( std::clamp( i, 0, static_cast<int>( RenderMaterialKind::Pine ) ) );
        ApplyRenderMaterialDefaults( material );
        rows[i * 4 + 0] = MaterialByte( material.roughness );
        rows[i * 4 + 1] = MaterialByte( material.metallic );
        rows[i * 4 + 2] = MaterialByte( material.specular );
        rows[i * 4 + 3] = MaterialByte( material.stylization );
    }

    sMaterialTableTexture = renderResources.CreateTexture2D( rows, MATERIAL_TABLE_WIDTH, 1, 4, false, false );
    renderCommands.BindTexture( sMaterialTableTexture, MATERIAL_TABLE_TEXTURE_SLOT );
}

static void
AppendMaterialInstancePayload( std::vector<float>& out, const Matrix4& model, const RenderMaterial& material )
{
    // Contract: every primitive batch uses the same instance stream layout:
    // model matrix columns followed by material0/material1/material2/material3. The DX12
    // input layout and both instanced shaders must stay in lockstep with this
    // packing order.
    const float* md = model.Data();
    out.insert( out.end(), md, md + INSTANCE_MATRIX_FLOATS );
    const RenderMaterialInstancePayload payload = PackRenderMaterialInstancePayload( material );
    out.insert( out.end(), payload.material0, payload.material0 + 4 );
    out.insert( out.end(), payload.material1, payload.material1 + 4 );
    out.insert( out.end(), payload.material2, payload.material2 + 4 );
    out.insert( out.end(), payload.material3, payload.material3 + 4 );
}

static std::array<float, INSTANCE_FLOATS> BuildSingleMaterialInstancePayload( const Matrix4& model,
                                                                              const RenderMaterial& material )
{
    std::array<float, INSTANCE_FLOATS> out = {};
    const float* md = model.Data();
    std::copy( md, md + INSTANCE_MATRIX_FLOATS, out.begin() );
    const RenderMaterialInstancePayload payload = PackRenderMaterialInstancePayload( material );
    std::copy( payload.material0, payload.material0 + 4, out.begin() + INSTANCE_MATRIX_FLOATS );
    std::copy( payload.material1, payload.material1 + 4, out.begin() + INSTANCE_MATRIX_FLOATS + 4 );
    std::copy( payload.material2, payload.material2 + 4, out.begin() + INSTANCE_MATRIX_FLOATS + 8 );
    std::copy( payload.material3, payload.material3 + 4, out.begin() + INSTANCE_MATRIX_FLOATS + 12 );
    return out;
}

static std::array<float, INSTANCE_FLOATS> BuildSingleMatrixPayload( const Matrix4& model )
{
    std::array<float, INSTANCE_FLOATS> out = {};
    const float* md = model.Data();
    std::copy( md, md + INSTANCE_MATRIX_FLOATS, out.begin() );
    return out;
}

static void EnsureConvexHullDynamicVB( IRenderResourceFactory& renderResources )
{
    if ( sConvexHullDynamicVB != 0 )
    {
        return;
    }

    int attribs[] = { 3, 3, 2, 4, 4, 4, 4, 4, 4, 4, 4 };
    sConvexHullDynamicVB = renderResources.CreateDynamicVB( attribs, 11, HULL_MAX_TRIANGLE_VERTICES );
}

static int BuildConvexHullDynamicVertices( const ConvexHullShape& hull,
                                           const std::array<float, INSTANCE_FLOATS>& instancePayload )
{
    int vertexCount = 0;
    auto emitVertex = [&]( uint16_t index, const Vector3& normal, float u, float v )
    {
        if ( vertexCount >= HULL_MAX_TRIANGLE_VERTICES )
        {
            return;
        }

        const Vector3 p = hull.GetPosition() + hull.GetVertex( index );
        float* out = &sConvexHullVertexData[static_cast<size_t>( vertexCount ) * HULL_DYNAMIC_FLOATS_PER_VERTEX];
        out[0] = p.x;
        out[1] = p.y;
        out[2] = p.z;
        out[3] = normal.x;
        out[4] = normal.y;
        out[5] = normal.z;
        out[6] = u;
        out[7] = v;
        std::copy( instancePayload.begin(), instancePayload.end(), out + 8 );
        ++vertexCount;
    };

    for ( uint16_t f = 0; f < hull.GetFaceCount(); ++f )
    {
        const ConvexHullFace& face = hull.GetFace( f );
        if ( face.indexCount < 3 )
        {
            continue;
        }

        const uint16_t root = hull.GetFaceIndex( face.firstIndex );
        for ( uint8_t i = 1; i + 1 < face.indexCount; ++i )
        {
            const uint16_t b = hull.GetFaceIndex( face.firstIndex + i );
            const uint16_t c = hull.GetFaceIndex( face.firstIndex + i + 1 );
            emitVertex( root, face.normalLocal, 0.0f, 0.0f );
            emitVertex( b, face.normalLocal, 1.0f, 0.0f );
            emitVertex( c, face.normalLocal, 0.0f, 1.0f );
        }
    }

    return vertexCount;
}

static void ApplySceneLightConstants( PrimitiveBatchShaderConstants& constants,
                                      const OrdinaryRenderConfig& ordinary )
{
    constants.lightAmbient[0] = ordinary.skyAmbientR;
    constants.lightAmbient[1] = ordinary.skyAmbientG;
    constants.lightAmbient[2] = ordinary.skyAmbientB;
    constants.lightAmbient[3] = ordinary.ambientStrength;
    constants.lightDiffuse[0] = ordinary.sunColorR * ordinary.sunIntensity;
    constants.lightDiffuse[1] = ordinary.sunColorG * ordinary.sunIntensity;
    constants.lightDiffuse[2] = ordinary.sunColorB * ordinary.sunIntensity;
    constants.lightDiffuse[3] = ordinary.boxRoughnessScale;
    constants.materialAmbient[0] = ordinary.groundAmbientR;
    constants.materialAmbient[1] = ordinary.groundAmbientG;
    constants.materialAmbient[2] = ordinary.groundAmbientB;
    constants.materialAmbient[3] = ordinary.ballRoughnessScale;
    constants.materialDiffuse[0] = 1.0f;
    constants.materialDiffuse[1] = 1.0f;
    constants.materialDiffuse[2] = 1.0f;
    constants.materialDiffuse[3] = ordinary.ballSpecularScale;
    constants.objectStylePad = ordinary.boxSpecularScale;
}

static void ApplySceneLightUniforms( IShader& shader, const OrdinaryRenderConfig& ordinary )
{
    shader.SetVec4( "uLightAmbient",
                    ordinary.skyAmbientR,
                    ordinary.skyAmbientG,
                    ordinary.skyAmbientB,
                    ordinary.ambientStrength );
    shader.SetVec4( "uLightDiffuse",
                    ordinary.sunColorR * ordinary.sunIntensity,
                    ordinary.sunColorG * ordinary.sunIntensity,
                    ordinary.sunColorB * ordinary.sunIntensity,
                    ordinary.boxRoughnessScale );
}

static void ApplyBatchLightConstants( PrimitiveBatchShaderConstants& constants,
                                      const OrdinaryRenderConfig& ordinaryRender,
                                      const CinematicRenderConfig* cinematicOverride )
{
    if ( cinematicOverride )
    {
        const CinematicRenderConfig& cinematic = *cinematicOverride;
        constants.lightAmbient[0] = 0.28f;
        constants.lightAmbient[1] = 0.15f;
        constants.lightAmbient[2] = 0.06f;
        constants.lightAmbient[3] = 1.0f;
        constants.lightDiffuse[0] = cinematic.sunColorR * 2.35f;
        constants.lightDiffuse[1] = cinematic.sunColorG * 2.35f;
        constants.lightDiffuse[2] = cinematic.sunColorB * 2.35f;
        constants.lightDiffuse[3] = 1.0f;
        return;
    }

    ApplySceneLightConstants( constants, ordinaryRender );
}

static int ObjectStyleForShader( const CinematicRenderConfig* cinematicOverride )
{
    // Encode render mode separately from the light vector. Negative values mean
    // "cinematic style", while ordinary batches use style 0 and may still use a
    // directional lightPosition.w of 0 for the sun/shadow-map contract.
    return cinematicOverride ? -( cinematicOverride->objectStyle + 1 ) : 0;
}

static int ObjectStyleForMeshSelection( const CinematicRenderConfig* cinematicOverride )
{
    return cinematicOverride ? cinematicOverride->objectStyle : 0;
}

static void FillShadowReceiverConstants( IRenderCommandContext& renderCommands,
                                         PrimitiveBatchShaderConstants& constants,
                                         const ShadowFrameData* shadow,
                                         bool receive,
                                         bool objectReceiver )
{
    const bool enabled = shadow && shadow->valid && receive && shadow->depthTextureHandle != 0;
    Matrix4 identity;
    constants.shadowViewProj = enabled ? shadow->lightViewProjection : identity;
    const float depthBias =
        enabled ? ( objectReceiver ? (std::max)( shadow->depthBias, 0.0015f ) : shadow->depthBias ) : 0.0f;
    const float slopeBias =
        enabled ? ( objectReceiver ? (std::max)( shadow->slopeBias, 0.0035f ) : shadow->slopeBias ) : 0.0f;
    constants.shadowParams[0] = enabled ? shadow->strength : 0.0f;
    constants.shadowParams[1] = depthBias;
    constants.shadowParams[2] = slopeBias;
    constants.shadowParams[3] = enabled ? shadow->texelSize * shadow->softness : 0.0f;
    constants.shadowFlags[0] = enabled ? 1.0f : 0.0f;
    constants.shadowFlags[1] = receive ? 1.0f : 0.0f;
    constants.shadowFlags[2] = enabled ? static_cast<float>( shadow->pcfRadius ) : 0.0f;
    constants.shadowFlags[3] = enabled && shadow->zeroToOneDepth ? 1.0f : 0.0f;

    renderCommands.BindTexture( enabled ? shadow->depthTextureHandle : 0, SHADOW_TEXTURE_SLOT );
}

struct PrimitiveBatchShaderParams
{
    IRenderCommandContext& renderCommands;
    IRenderResourceFactory& renderResources;
    const Matrix4& view;
    const Matrix4& projection;
    const float* lightPosition;
    const float* clipPlane;
    const OrdinaryRenderConfig& ordinaryRender;
    const CinematicRenderConfig* cinematic;
    const ShadowFrameData* shadow;
    int primitiveShape;
    bool receiveShadows;
    float materialAlpha;
};

static bool BindPrimitiveBatchShader( IShader& shader, const PrimitiveBatchShaderParams& params )
{
    EnsureMaterialTableTexture( params.renderResources, params.renderCommands );

    float viewLightPos[4];
    for ( int i = 0; i < 3; ++i )
    {
        viewLightPos[i] = params.view.m[i] * params.lightPosition[0] + params.view.m[i + 4] * params.lightPosition[1] +
                          params.view.m[i + 8] * params.lightPosition[2] +
                          params.view.m[i + 12] * params.lightPosition[3];
    }
    viewLightPos[3] = params.lightPosition[3];

    shader.Use();
    PrimitiveBatchShaderConstants constants = {};
    constants.view = params.view;
    constants.projection = params.projection;
    constants.clipPlane[0] = params.clipPlane[0];
    constants.clipPlane[1] = params.clipPlane[1];
    constants.clipPlane[2] = params.clipPlane[2];
    constants.clipPlane[3] = params.clipPlane[3];
    constants.lightPosition[0] = viewLightPos[0];
    constants.lightPosition[1] = viewLightPos[1];
    constants.lightPosition[2] = viewLightPos[2];
    constants.lightPosition[3] = viewLightPos[3];
    constants.materialAmbient[0] = 0.2f;
    constants.materialAmbient[1] = 0.2f;
    constants.materialAmbient[2] = 0.2f;
    constants.materialAmbient[3] = 1.0f;
    constants.materialDiffuse[0] = 0.8f;
    constants.materialDiffuse[1] = 0.8f;
    constants.materialDiffuse[2] = 0.8f;
    constants.materialDiffuse[3] = 1.0f;
    constants.objectStyle = ObjectStyleForShader( params.cinematic );
    constants.primitiveShape = params.primitiveShape;
    constants.materialAlpha = std::clamp( params.materialAlpha, 0.0f, 1.0f );
    constants.objectStylePad = 0.0f;
    ApplyBatchLightConstants( constants, params.ordinaryRender, params.cinematic );
    FillShadowReceiverConstants( params.renderCommands, constants, params.shadow, params.receiveShadows, true );
    return shader.SetConstantBufferBytes( &constants, sizeof( constants ), "PrimitiveBatchShaderConstants" );
}

void RenderHelper::SetClipPlane( float x, float y, float z, float w )
{
    sClipPlane[0] = x;
    sClipPlane[1] = y;
    sClipPlane[2] = z;
    sClipPlane[3] = w;
}


const float* RenderHelper::GetClipPlane()
{
    return sClipPlane;
}


void RenderHelper::ResetRenderResources( IRenderResourceFactory* renderResources )
{
    sphereShader.reset();
    shadowDepthShader.reset();
    if ( sphereInstMesh != 0 )
    {
        if ( renderResources )
        {
            renderResources->DestroyInstancedMesh( sphereInstMesh );
        }
        sphereInstMesh = 0;
    }
    if ( lowPolySphereInstMesh != 0 )
    {
        if ( renderResources )
        {
            renderResources->DestroyInstancedMesh( lowPolySphereInstMesh );
        }
        lowPolySphereInstMesh = 0;
    }
    if ( boxInstMesh != 0 )
    {
        if ( renderResources )
        {
            renderResources->DestroyInstancedMesh( boxInstMesh );
        }
        boxInstMesh = 0;
    }
    if ( pineInstMesh != 0 )
    {
        if ( renderResources )
        {
            renderResources->DestroyInstancedMesh( pineInstMesh );
        }
        pineInstMesh = 0;
    }
    if ( sMaterialTableTexture != 0 )
    {
        if ( renderResources )
        {
            renderResources->DeleteTexture( sMaterialTableTexture );
        }
        sMaterialTableTexture = 0;
    }
    if ( sConvexHullDynamicVB != 0 )
    {
        if ( renderResources )
        {
            renderResources->DestroyDynamicVB( sConvexHullDynamicVB );
        }
        sConvexHullDynamicVB = 0;
    }
    activeSphereInstMesh = 0;
    activeSphereVertexCount = 0;
}


void RenderHelper::EnsureSphereShader( IRenderResourceFactory& renderResources,
                                       const SkullbonezCore::Assets::AssetSystem& assets,
                                       const OrdinaryRenderConfig& ordinaryRender )
{
    if ( !sphereShader )
    {
        // Lifetime: helper static state owns the shader handle, while source
        // lookup comes from the Run-owned asset registry borrowed by the pass.
        sphereShader = assets.CreateShader( renderResources, "shader.lit_textured_instanced" );
        sphereShader->Use();
        ApplySceneLightUniforms( *sphereShader, ordinaryRender );
        sphereShader->SetVec4( "uMaterialAmbient", 0.2f, 0.2f, 0.2f, 1.0f );
        sphereShader->SetVec4( "uMaterialDiffuse", 0.8f, 0.8f, 0.8f, 1.0f );
        sphereShader->SetFloat( "uMaterialAlpha", 1.0f );
    }
}


void RenderHelper::EnsureShadowDepthShader( IRenderResourceFactory& renderResources,
                                            const SkullbonezCore::Assets::AssetSystem& assets )
{
    if ( !shadowDepthShader )
    {
        // One shared instanced depth shader is enough for balls, boxes, and pine
        // visuals because all three meshes expose the same static attributes and
        // per-instance material layout. The fragment output is irrelevant; the
        // depth attachment is the shadow map product.
        shadowDepthShader = assets.CreateShader( renderResources, "shader.shadow_depth_instanced" );
    }
}


void RenderHelper::EnsureSphereMesh( IRenderResourceFactory& renderResources )
{
    if ( sphereInstMesh == 0 )
    {
        BuildSphereMesh( renderResources, 25, 25 );
    }
    // Mesh prewarm is used by DXR; draw entry points initialize the lit shader with frame config.
}


void RenderHelper::BuildSphereMesh( IRenderResourceFactory& renderResources, int slices, int stacks )
{
    std::vector<float> verts;
    verts.reserve( PrimitiveMeshes::SphereTriangleVertexCount( slices, stacks ) * 8 );

    PrimitiveMeshes::EmitUnitSphere(
        slices,
        stacks,
        [&]( const PrimitiveMeshes::VertexPNUV& vertex )
        {
            verts.insert( verts.end(),
                          { vertex.x, vertex.y, vertex.z, vertex.nx, vertex.ny, vertex.nz, vertex.u, vertex.v } );
        } );

    sphereVertexCount = PrimitiveMeshes::SphereTriangleVertexCount( slices, stacks );

    // Static layout: 3 attributes (pos3, normal3, uv2) at locations 0-2
    int staticAttribSizes[] = { 3, 3, 2 };
    // Instance layout: model matrix plus three float4 material rows, starting at location 3.
    int instanceAttribSizes[] = { 4, 4, 4, 4, 4, 4, 4, 4 };
    sphereInstMesh = renderResources.CreateInstancedMesh( verts.data(),
                                                          sphereVertexCount,
                                                          8,
                                                          MAX_GAME_MODELS,
                                                          INSTANCE_FLOATS,
                                                          3,
                                                          instanceAttribSizes,
                                                          8,
                                                          staticAttribSizes,
                                                          3 );

    sphereInstanceData.reserve( MAX_GAME_MODELS * INSTANCE_FLOATS );
}


void RenderHelper::BuildLowPolySphereMesh( IRenderResourceFactory& renderResources, int slices, int stacks )
{
    std::vector<float> verts;
    verts.reserve( PrimitiveMeshes::SphereTriangleVertexCount( slices, stacks ) * 8 );

    PrimitiveMeshes::EmitUnitSphereFlat(
        slices,
        stacks,
        [&]( const PrimitiveMeshes::VertexPNUV& vertex )
        {
            verts.insert( verts.end(),
                          { vertex.x, vertex.y, vertex.z, vertex.nx, vertex.ny, vertex.nz, vertex.u, vertex.v } );
        } );

    lowPolySphereVertexCount = PrimitiveMeshes::SphereTriangleVertexCount( slices, stacks );

    int staticAttribSizes[] = { 3, 3, 2 };
    int instanceAttribSizes[] = { 4, 4, 4, 4, 4, 4, 4, 4 };
    lowPolySphereInstMesh = renderResources.CreateInstancedMesh( verts.data(),
                                                                 lowPolySphereVertexCount,
                                                                 8,
                                                                 MAX_GAME_MODELS,
                                                                 INSTANCE_FLOATS,
                                                                 3,
                                                                 instanceAttribSizes,
                                                                 8,
                                                                 staticAttribSizes,
                                                                 3 );

    sphereInstanceData.reserve( MAX_GAME_MODELS * INSTANCE_FLOATS );
}


void RenderHelper::DrawSphereBatchBegin( IRenderCommandContext& renderCommands,
                                         IRenderResourceFactory& renderResources,
                                         const SkullbonezCore::Assets::AssetSystem& assets,
                                         const Matrix4& view,
                                         const Matrix4& proj,
                                         const float lightPos[4],
                                         const OrdinaryRenderConfig& ordinaryRender,
                                         bool isTransparent,
                                         const CinematicRenderConfig* cinematic,
                                         const ShadowFrameData* shadow,
                                         float materialAlpha )
{
    sSphereBatchTransparent = isTransparent;
    sSphereBatchReady = false;
    const int objectStyle = ObjectStyleForMeshSelection( cinematic );
    const bool useLowPolySphereMesh = objectStyle == 6;
    if ( useLowPolySphereMesh )
    {
        if ( lowPolySphereInstMesh == 0 )
        {
            BuildLowPolySphereMesh( renderResources, 12, 7 );
        }
        activeSphereInstMesh = lowPolySphereInstMesh;
        activeSphereVertexCount = lowPolySphereVertexCount;
    }
    else
    {
        if ( sphereInstMesh == 0 )
        {
            BuildSphereMesh( renderResources, 25, 25 );
        }
        activeSphereInstMesh = sphereInstMesh;
        activeSphereVertexCount = sphereVertexCount;
    }
    EnsureSphereShader( renderResources, assets, ordinaryRender );

    BeginPrimitiveBatchTransparency( renderCommands, isTransparent );

    // Low-poly spheres still cast real shadows onto terrain, but they do not
    // receive object shadows. The shadow map is single and terrain-sized, so
    // ball-on-ball receiver shadows alias badly across the large flat facets
    // used by the low-poly beachball style.
    const bool receiveSphereShadows = shadow && shadow->objectsReceive && !useLowPolySphereMesh;
    sSphereBatchReady = BindPrimitiveBatchShader( *sphereShader,
                                                  { renderCommands,
                                                    renderResources,
                                                    view,
                                                    proj,
                                                    lightPos,
                                                    sClipPlane,
                                                    ordinaryRender,
                                                    cinematic,
                                                    shadow,
                                                    PRIMITIVE_SHAPE_SPHERE,
                                                    receiveSphereShadows,
                                                    materialAlpha } );
    sphereInstanceData.clear();
}


void RenderHelper::DrawSphereBatchModel( const Matrix4& model, const RenderMaterial& material )
{
    AppendMaterialInstancePayload( sphereInstanceData, model, material );
}


void RenderHelper::DrawSphereBatchModel( const Matrix4& model,
                                         float tintR,
                                         float tintG,
                                         float tintB,
                                         float colorOverride )
{
    DrawSphereBatchModel( model, MakeRenderMaterialFromLegacyTint( tintR, tintG, tintB, colorOverride ) );
}


void RenderHelper::DrawSphereBatchEnd( IRenderCommandContext& renderCommands )
{
    int instanceCount = static_cast<int>( sphereInstanceData.size() ) / INSTANCE_FLOATS;
    if ( instanceCount > 0 && activeSphereInstMesh != 0 )
    {
        if ( sSphereBatchReady )
        {
            renderCommands.UploadInstanceData( activeSphereInstMesh,
                                               sphereInstanceData.data(),
                                               static_cast<int>( sphereInstanceData.size() ) );
            renderCommands.DrawInstancedMesh( activeSphereInstMesh, activeSphereVertexCount, instanceCount );
        }
    }
    EndPrimitiveBatchTransparency( renderCommands, sSphereBatchTransparent );
    sSphereBatchTransparent = false;
    sSphereBatchReady = false;
}


void RenderHelper::DrawShadowDepthSphereBatchBegin( IRenderCommandContext& renderCommands,
                                                    IRenderResourceFactory& renderResources,
                                                    const SkullbonezCore::Assets::AssetSystem& assets,
                                                    const Matrix4& view,
                                                    const Matrix4& proj,
                                                    const CinematicRenderConfig* cinematic )
{
    (void)renderCommands;
    sSphereBatchReady = false;
    // Match the visible sphere mesh selection. If a low-poly style is active,
    // the depth pass also uses the faceted mesh, which prevents a smooth sphere
    // shadow from appearing under a visibly low-poly ball.
    const bool useLowPolySphereMesh = ObjectStyleForMeshSelection( cinematic ) == 6;
    if ( useLowPolySphereMesh )
    {
        if ( lowPolySphereInstMesh == 0 )
        {
            BuildLowPolySphereMesh( renderResources, 12, 7 );
        }
        activeSphereInstMesh = lowPolySphereInstMesh;
        activeSphereVertexCount = lowPolySphereVertexCount;
    }
    else
    {
        if ( sphereInstMesh == 0 )
        {
            BuildSphereMesh( renderResources, 25, 25 );
        }
        activeSphereInstMesh = sphereInstMesh;
        activeSphereVertexCount = sphereVertexCount;
    }

    EnsureShadowDepthShader( renderResources, assets );
    shadowDepthShader->Use();
    InstancedShadowDepthConstants constants = {};
    constants.view = view;
    constants.projection = proj;
    constants.clipPlane[0] = sClipPlane[0];
    constants.clipPlane[1] = sClipPlane[1];
    constants.clipPlane[2] = sClipPlane[2];
    constants.clipPlane[3] = sClipPlane[3];
    sSphereBatchReady =
        shadowDepthShader->SetConstantBufferBytes( &constants, sizeof( constants ), "InstancedShadowDepthConstants" );
    sphereInstanceData.clear();
}


void RenderHelper::DrawShadowDepthSphereBatchModel( const Matrix4& model )
{
    DrawSphereBatchModel( model, 1.0f, 1.0f, 1.0f, 0.0f );
}


void RenderHelper::DrawShadowDepthSphereBatchEnd( IRenderCommandContext& renderCommands )
{
    int instanceCount = static_cast<int>( sphereInstanceData.size() ) / INSTANCE_FLOATS;
    if ( sSphereBatchReady && instanceCount > 0 && activeSphereInstMesh != 0 )
    {
        // Upload only the compact per-instance stream, then issue one instanced
        // draw for every sphere caster. This keeps the shadow pass draw-call
        // count predictable even in scenes with hundreds of balls.
        renderCommands.UploadInstanceData( activeSphereInstMesh,
                                           sphereInstanceData.data(),
                                           static_cast<int>( sphereInstanceData.size() ) );
        renderCommands.DrawInstancedMesh( activeSphereInstMesh, activeSphereVertexCount, instanceCount );
    }
    sSphereBatchReady = false;
}


// =============================================================================
// BOX INSTANCED RENDERING
// =============================================================================
//
// Unit cubes [-1,1]^3 scaled by half-extents via the model matrix.
// Uses the same lit_textured_instanced shader as spheres so lighting is
// consistent. The cube has outward-facing normals and simple planar UV.
//
// =============================================================================


void RenderHelper::BuildBoxMesh( IRenderResourceFactory& renderResources )
{
    std::vector<float> verts;
    verts.reserve( PrimitiveMeshes::BoxTriangleVertexCount() * 8 );

    PrimitiveMeshes::EmitUnitBox(
        [&]( const PrimitiveMeshes::VertexPNUV& vertex )
        {
            verts.insert( verts.end(),
                          { vertex.x, vertex.y, vertex.z, vertex.nx, vertex.ny, vertex.nz, vertex.u, vertex.v } );
        } );

    boxVertexCount = PrimitiveMeshes::BoxTriangleVertexCount();

    int staticAttribSizes[] = { 3, 3, 2 };
    int instanceAttribSizes[] = { 4, 4, 4, 4, 4, 4, 4, 4 };
    boxInstMesh = renderResources.CreateInstancedMesh( verts.data(),
                                                       boxVertexCount,
                                                       8,
                                                       MAX_GAME_MODELS,
                                                       INSTANCE_FLOATS,
                                                       3,
                                                       instanceAttribSizes,
                                                       8,
                                                       staticAttribSizes,
                                                       3 );

    boxInstanceData.reserve( MAX_GAME_MODELS * INSTANCE_FLOATS );
}


void RenderHelper::DrawBoxBatchBegin( IRenderCommandContext& renderCommands,
                                      IRenderResourceFactory& renderResources,
                                      const SkullbonezCore::Assets::AssetSystem& assets,
                                      const Matrix4& view,
                                      const Matrix4& proj,
                                      const float lightPos[4],
                                      const OrdinaryRenderConfig& ordinaryRender,
                                      bool isTransparent,
                                      const CinematicRenderConfig* cinematic,
                                      const ShadowFrameData* shadow,
                                      float materialAlpha )
{
    sBoxBatchTransparent = isTransparent;
    sBoxBatchReady = false;
    if ( boxInstMesh == 0 )
    {
        BuildBoxMesh( renderResources );
    }

    // Reuse sphere shader (same vertex layout, same lighting model).
    EnsureSphereShader( renderResources, assets, ordinaryRender );

    BeginPrimitiveBatchTransparency( renderCommands, isTransparent );

    sBoxBatchReady = BindPrimitiveBatchShader( *sphereShader,
                                               { renderCommands,
                                                 renderResources,
                                                 view,
                                                 proj,
                                                 lightPos,
                                                 sClipPlane,
                                                 ordinaryRender,
                                                 cinematic,
                                                 shadow,
                                                 PRIMITIVE_SHAPE_MESH,
                                                 shadow ? shadow->objectsReceive : false,
                                                 materialAlpha } );
    boxInstanceData.clear();
}


void RenderHelper::DrawBoxBatchModel( const Matrix4& model, const RenderMaterial& material )
{
    AppendMaterialInstancePayload( boxInstanceData, model, material );
}


void RenderHelper::DrawBoxBatchModel( const Matrix4& model, float tintR, float tintG, float tintB, float colorOverride )
{
    DrawBoxBatchModel( model, MakeRenderMaterialFromLegacyTint( tintR, tintG, tintB, colorOverride ) );
}


void RenderHelper::DrawBoxBatchEnd( IRenderCommandContext& renderCommands )
{
    int instanceCount = static_cast<int>( boxInstanceData.size() ) / INSTANCE_FLOATS;
    if ( sBoxBatchReady && instanceCount > 0 )
    {
        renderCommands.UploadInstanceData( boxInstMesh, boxInstanceData.data(), static_cast<int>( boxInstanceData.size() ) );
        renderCommands.DrawInstancedMesh( boxInstMesh, boxVertexCount, instanceCount );
    }
    EndPrimitiveBatchTransparency( renderCommands, sBoxBatchTransparent );
    sBoxBatchTransparent = false;
    sBoxBatchReady = false;
}


void RenderHelper::DrawShadowDepthBoxBatchBegin( IRenderCommandContext& renderCommands,
                                                 IRenderResourceFactory& renderResources,
                                                 const SkullbonezCore::Assets::AssetSystem& assets,
                                                 const Matrix4& view,
                                                 const Matrix4& proj )
{
    (void)renderCommands;
    sBoxBatchReady = false;
    if ( boxInstMesh == 0 )
    {
        BuildBoxMesh( renderResources );
    }
    EnsureShadowDepthShader( renderResources, assets );
    shadowDepthShader->Use();
    InstancedShadowDepthConstants constants = {};
    constants.view = view;
    constants.projection = proj;
    constants.clipPlane[0] = sClipPlane[0];
    constants.clipPlane[1] = sClipPlane[1];
    constants.clipPlane[2] = sClipPlane[2];
    constants.clipPlane[3] = sClipPlane[3];
    sBoxBatchReady =
        shadowDepthShader->SetConstantBufferBytes( &constants, sizeof( constants ), "InstancedShadowDepthConstants" );
    boxInstanceData.clear();
}


void RenderHelper::DrawShadowDepthBoxBatchModel( const Matrix4& model )
{
    DrawBoxBatchModel( model, 1.0f, 1.0f, 1.0f, 0.0f );
}


void RenderHelper::DrawShadowDepthBoxBatchEnd( IRenderCommandContext& renderCommands )
{
    int instanceCount = static_cast<int>( boxInstanceData.size() ) / INSTANCE_FLOATS;
    if ( sBoxBatchReady && instanceCount > 0 && boxInstMesh != 0 )
    {
        // This draw is the box-caster fix point: if a scene has boxes and shadow
        // maps are active, their depth is written here before terrain/objects
        // sample the map in the forward pass.
        renderCommands.UploadInstanceData( boxInstMesh, boxInstanceData.data(), static_cast<int>( boxInstanceData.size() ) );
        renderCommands.DrawInstancedMesh( boxInstMesh, boxVertexCount, instanceCount );
    }
    sBoxBatchReady = false;
}

void RenderHelper::DrawConvexHullModel( IRenderCommandContext& renderCommands,
                                        IRenderResourceFactory& renderResources,
                                        const SkullbonezCore::Assets::AssetSystem& assets,
                                        const ConvexHullShape& hull,
                                        const Matrix4& model,
                                        const RenderMaterial& material,
                                        const Matrix4& view,
                                        const Matrix4& proj,
                                        const float lightPos[4],
                                        const OrdinaryRenderConfig& ordinaryRender,
                                        bool isTransparent,
                                        const CinematicRenderConfig* cinematic,
                                        const ShadowFrameData* shadow,
                                        float materialAlpha )
{
    EnsureConvexHullDynamicVB( renderResources );
    const std::array<float, INSTANCE_FLOATS> instancePayload = BuildSingleMaterialInstancePayload( model, material );
    const int vertexCount = BuildConvexHullDynamicVertices( hull, instancePayload );
    if ( sConvexHullDynamicVB == 0 || vertexCount <= 0 )
    {
        return;
    }

    EnsureSphereShader( renderResources, assets, ordinaryRender );
    BeginPrimitiveBatchTransparency( renderCommands, isTransparent );
    const bool ready = BindPrimitiveBatchShader( *sphereShader,
                                                 { renderCommands,
                                                   renderResources,
                                                   view,
                                                   proj,
                                                   lightPos,
                                                   sClipPlane,
                                                   ordinaryRender,
                                                   cinematic,
                                                   shadow,
                                                   PRIMITIVE_SHAPE_MESH,
                                                   shadow ? shadow->objectsReceive : false,
                                                   materialAlpha } );
    if ( ready )
    {
        renderCommands.UploadAndDrawDynamicVB( sConvexHullDynamicVB, sConvexHullVertexData.data(), vertexCount );
    }
    EndPrimitiveBatchTransparency( renderCommands, isTransparent );
}

void RenderHelper::DrawShadowDepthConvexHullModel( IRenderCommandContext& renderCommands,
                                                   IRenderResourceFactory& renderResources,
                                                   const SkullbonezCore::Assets::AssetSystem& assets,
                                                   const ConvexHullShape& hull,
                                                   const Matrix4& model,
                                                   const Matrix4& view,
                                                   const Matrix4& proj )
{
    EnsureConvexHullDynamicVB( renderResources );
    const std::array<float, INSTANCE_FLOATS> instancePayload = BuildSingleMatrixPayload( model );
    const int vertexCount = BuildConvexHullDynamicVertices( hull, instancePayload );
    if ( sConvexHullDynamicVB == 0 || vertexCount <= 0 )
    {
        return;
    }

    EnsureShadowDepthShader( renderResources, assets );
    shadowDepthShader->Use();
    InstancedShadowDepthConstants constants = {};
    constants.view = view;
    constants.projection = proj;
    constants.clipPlane[0] = sClipPlane[0];
    constants.clipPlane[1] = sClipPlane[1];
    constants.clipPlane[2] = sClipPlane[2];
    constants.clipPlane[3] = sClipPlane[3];
    if ( shadowDepthShader->SetConstantBufferBytes( &constants, sizeof( constants ), "InstancedShadowDepthConstants" ) )
    {
        renderCommands.UploadAndDrawDynamicVB( sConvexHullDynamicVB, sConvexHullVertexData.data(), vertexCount );
    }
}


void RenderHelper::BuildPineMesh( IRenderResourceFactory& renderResources )
{
    std::vector<float> verts;
    verts.reserve( PrimitiveMeshes::PineTriangleVertexCount() * 8 );

    PrimitiveMeshes::EmitUnitPinePyramid(
        [&]( const PrimitiveMeshes::VertexPNUV& vertex )
        {
            verts.insert( verts.end(),
                          { vertex.x, vertex.y, vertex.z, vertex.nx, vertex.ny, vertex.nz, vertex.u, vertex.v } );
        } );

    pineVertexCount = PrimitiveMeshes::PineTriangleVertexCount();

    int staticAttribSizes[] = { 3, 3, 2 };
    int instanceAttribSizes[] = { 4, 4, 4, 4, 4, 4, 4, 4 };
    pineInstMesh = renderResources.CreateInstancedMesh( verts.data(),
                                                        pineVertexCount,
                                                        8,
                                                        MAX_GAME_MODELS,
                                                        INSTANCE_FLOATS,
                                                        3,
                                                        instanceAttribSizes,
                                                        8,
                                                        staticAttribSizes,
                                                        3 );

    pineInstanceData.reserve( MAX_GAME_MODELS * INSTANCE_FLOATS );
}


void RenderHelper::DrawPineBatchBegin( IRenderCommandContext& renderCommands,
                                       IRenderResourceFactory& renderResources,
                                       const SkullbonezCore::Assets::AssetSystem& assets,
                                       const Matrix4& view,
                                       const Matrix4& proj,
                                       const float lightPos[4],
                                       const OrdinaryRenderConfig& ordinaryRender,
                                       bool isTransparent,
                                       const CinematicRenderConfig* cinematic,
                                       const ShadowFrameData* shadow,
                                       float materialAlpha )
{
    sPineBatchTransparent = isTransparent;
    sPineBatchReady = false;
    if ( pineInstMesh == 0 )
    {
        BuildPineMesh( renderResources );
    }

    EnsureSphereShader( renderResources, assets, ordinaryRender );

    BeginPrimitiveBatchTransparency( renderCommands, isTransparent );

    sPineBatchReady = BindPrimitiveBatchShader( *sphereShader,
                                                { renderCommands,
                                                  renderResources,
                                                  view,
                                                  proj,
                                                  lightPos,
                                                  sClipPlane,
                                                  ordinaryRender,
                                                  cinematic,
                                                  shadow,
                                                  PRIMITIVE_SHAPE_MESH,
                                                  shadow ? shadow->objectsReceive : false,
                                                  materialAlpha } );
    pineInstanceData.clear();
}


void RenderHelper::DrawPineBatchModel( const Matrix4& model, const RenderMaterial& material )
{
    AppendMaterialInstancePayload( pineInstanceData, model, material );
}


void RenderHelper::DrawPineBatchModel( const Matrix4& model,
                                       float tintR,
                                       float tintG,
                                       float tintB,
                                       float colorOverride )
{
    DrawPineBatchModel( model, MakeRenderMaterialFromLegacyTint( tintR, tintG, tintB, colorOverride ) );
}


void RenderHelper::DrawPineBatchEnd( IRenderCommandContext& renderCommands )
{
    int instanceCount = static_cast<int>( pineInstanceData.size() ) / INSTANCE_FLOATS;
    if ( sPineBatchReady && instanceCount > 0 )
    {
        renderCommands.UploadInstanceData( pineInstMesh, pineInstanceData.data(), static_cast<int>( pineInstanceData.size() ) );
        renderCommands.DrawInstancedMesh( pineInstMesh, pineVertexCount, instanceCount );
    }
    EndPrimitiveBatchTransparency( renderCommands, sPineBatchTransparent );
    sPineBatchTransparent = false;
    sPineBatchReady = false;
}


void RenderHelper::DrawShadowDepthPineBatchBegin( IRenderCommandContext& renderCommands,
                                                  IRenderResourceFactory& renderResources,
                                                  const SkullbonezCore::Assets::AssetSystem& assets,
                                                  const Matrix4& view,
                                                  const Matrix4& proj )
{
    (void)renderCommands;
    sPineBatchReady = false;
    if ( pineInstMesh == 0 )
    {
        BuildPineMesh( renderResources );
    }
    EnsureShadowDepthShader( renderResources, assets );
    shadowDepthShader->Use();
    InstancedShadowDepthConstants constants = {};
    constants.view = view;
    constants.projection = proj;
    constants.clipPlane[0] = sClipPlane[0];
    constants.clipPlane[1] = sClipPlane[1];
    constants.clipPlane[2] = sClipPlane[2];
    constants.clipPlane[3] = sClipPlane[3];
    sPineBatchReady =
        shadowDepthShader->SetConstantBufferBytes( &constants, sizeof( constants ), "InstancedShadowDepthConstants" );
    pineInstanceData.clear();
}


void RenderHelper::DrawShadowDepthPineBatchModel( const Matrix4& model )
{
    DrawPineBatchModel( model, 1.0f, 1.0f, 1.0f, 0.0f );
}


void RenderHelper::DrawShadowDepthPineBatchEnd( IRenderCommandContext& renderCommands )
{
    int instanceCount = static_cast<int>( pineInstanceData.size() ) / INSTANCE_FLOATS;
    if ( sPineBatchReady && instanceCount > 0 && pineInstMesh != 0 )
    {
        renderCommands.UploadInstanceData( pineInstMesh,
                                           pineInstanceData.data(),
                                           static_cast<int>( pineInstanceData.size() ) );
        renderCommands.DrawInstancedMesh( pineInstMesh, pineVertexCount, instanceCount );
    }
    sPineBatchReady = false;
}


void RenderHelper::StateSetup()
{
    // Initial render state is owned by the DX12 backend. This hook remains as a
    // small extension point for any helper-level setup that must happen after
    // the renderer has initialized.
}
