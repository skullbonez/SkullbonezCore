/*
File: SkullbonezSource/SkullbonezHelper.cpp
Purpose:
  Collects legacy helper routines that bridge engine subsystems.

Mental model:
  Runtime code connects authored scene data, input, simulation, render
  backends, and validation-oriented launch modes. Follow who owns state and
  when that state changes.

Glossary:
  Cbuffer (Constant Buffer): Shader constant block uploaded once before a draw.
  Material table: Fixed t4 texture that stores default per-kind material response
  values for object shaders.
  Instance payload: Per-object data appended after the model matrix in an
  instanced draw stream.
  Validation gate: Repository script that proves a class of changes before
  commit or PR.

Related:
  - SkullbonezSource/SkullbonezHelper.h
  - Agentic/Reference/runtime-reference.md
  - Agentic/Reference/comment-style-guide.md
*/
#include "SkullbonezHelper.h"
#include "SkullbonezAssetSystem.h"
#include "SkullbonezConvexHullShape.h"
#include "SkullbonezProfiler.h"
#include "SkullbonezIRenderBackend.h"
#include "SkullbonezPrimitiveMeshBuilder.h"

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <vector>


using namespace SkullbonezCore::Basics;
using namespace SkullbonezCore::Rendering;
using namespace SkullbonezCore::Math::CollisionDetection;
using namespace SkullbonezCore::Math::Transformation;
using namespace SkullbonezCore::Math::Vector;


std::unique_ptr<IShader> SkullbonezHelper::sphereShader;
std::unique_ptr<IShader> SkullbonezHelper::shadowDepthShader;
uint32_t SkullbonezHelper::sphereInstMesh = 0;
int SkullbonezHelper::sphereVertexCount = 0;
std::vector<float> SkullbonezHelper::sphereInstanceData;
uint32_t SkullbonezHelper::lowPolySphereInstMesh = 0;
int SkullbonezHelper::lowPolySphereVertexCount = 0;
uint32_t SkullbonezHelper::activeSphereInstMesh = 0;
int SkullbonezHelper::activeSphereVertexCount = 0;
uint32_t SkullbonezHelper::boxInstMesh = 0;
int SkullbonezHelper::boxVertexCount = 0;
std::vector<float> SkullbonezHelper::boxInstanceData;
uint32_t SkullbonezHelper::pineInstMesh = 0;
int SkullbonezHelper::pineVertexCount = 0;
std::vector<float> SkullbonezHelper::pineInstanceData;

static constexpr int INSTANCE_MATRIX_FLOATS = 16;
static constexpr int INSTANCE_MATERIAL_FLOAT4_COUNT = 3;
static constexpr int INSTANCE_MATERIAL_FLOATS = INSTANCE_MATERIAL_FLOAT4_COUNT * 4;
static constexpr int INSTANCE_FLOATS = INSTANCE_MATRIX_FLOATS + INSTANCE_MATERIAL_FLOATS;
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

struct ConvexHullMeshResource
{
    uint64_t hash = 0;
    uint32_t mesh = 0;
    int vertexCount = 0;
};

static std::vector<ConvexHullMeshResource> sConvexHullMeshes;
static std::vector<float> sConvexHullInstanceData;

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

static void BeginPrimitiveBatchTransparency( bool isTransparent )
{
    if ( isTransparent )
    {
        Gfx().SetBlend( true );
        Gfx().SetBlendFunc( BlendFactor::SrcAlpha, BlendFactor::OneMinusSrcAlpha );
        Gfx().SetDepthWrite( false );
    }
}

static void EndPrimitiveBatchTransparency( bool wasTransparent )
{
    if ( wasTransparent )
    {
        Gfx().SetDepthWrite( true );
    }
    Gfx().SetBlend( false );
}

static uint8_t MaterialByte( float value )
{
    // The material table is an 8-bit texture, so clamp and round normalized
    // material parameters at the CPU boundary before the shader samples them.
    return static_cast<uint8_t>( std::clamp( value, 0.0f, 1.0f ) * 255.0f + 0.5f );
}

static void EnsureMaterialTableTexture()
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
        Gfx().BindTexture( sMaterialTableTexture, MATERIAL_TABLE_TEXTURE_SLOT );
        return;
    }

    uint8_t rows[MATERIAL_TABLE_WIDTH * 4] = {};
    for ( int i = 0; i < MATERIAL_TABLE_WIDTH; ++i )
    {
        RenderMaterial material;
        material.kind = static_cast<RenderMaterialKind>( std::clamp( i, 0, static_cast<int>( RenderMaterialKind::Pine ) ) );
        ApplyRenderMaterialDefaults( material );
        rows[i * 4 + 0] = MaterialByte( material.roughness );
        rows[i * 4 + 1] = MaterialByte( material.metallic );
        rows[i * 4 + 2] = MaterialByte( material.specular );
        rows[i * 4 + 3] = MaterialByte( material.stylization );
    }

    sMaterialTableTexture = Gfx().CreateTexture2D( rows, MATERIAL_TABLE_WIDTH, 1, 4, false, false );
    Gfx().BindTexture( sMaterialTableTexture, MATERIAL_TABLE_TEXTURE_SLOT );
}

static void AppendMaterialInstancePayload( std::vector<float>& out, const Matrix4& model, const RenderMaterial& material )
{
    // Contract: every primitive batch uses the same instance stream layout:
    // model matrix columns followed by material0/material1/material2. The DX12
    // input layout and both instanced shaders must stay in lockstep with this
    // packing order.
    const float* md = model.Data();
    out.insert( out.end(), md, md + INSTANCE_MATRIX_FLOATS );
    const RenderMaterialInstancePayload payload = PackRenderMaterialInstancePayload( material );
    out.insert( out.end(), payload.material0, payload.material0 + 4 );
    out.insert( out.end(), payload.material1, payload.material1 + 4 );
    out.insert( out.end(), payload.material2, payload.material2 + 4 );
}

static void HashUint32( uint64_t& hash, uint32_t value )
{
    constexpr uint64_t FNV_PRIME = 1099511628211ull;
    for ( int i = 0; i < 4; ++i )
    {
        hash ^= static_cast<uint8_t>( value >> ( i * 8 ) );
        hash *= FNV_PRIME;
    }
}

static void HashFloat( uint64_t& hash, float value )
{
    uint32_t bits = 0;
    std::memcpy( &bits, &value, sizeof( bits ) );
    HashUint32( hash, bits );
}

static uint64_t HashConvexHullGeometry( const ConvexHullShape& hull )
{
    uint64_t hash = 1469598103934665603ull;
    HashUint32( hash, hull.GetVertexCount() );
    HashUint32( hash, hull.GetFaceCount() );
    for ( uint16_t v = 0; v < hull.GetVertexCount(); ++v )
    {
        const Vector3& p = hull.GetVertex( v );
        HashFloat( hash, p.x );
        HashFloat( hash, p.y );
        HashFloat( hash, p.z );
    }
    for ( uint16_t f = 0; f < hull.GetFaceCount(); ++f )
    {
        const ConvexHullFace& face = hull.GetFace( f );
        HashUint32( hash, face.indexCount );
        for ( uint8_t i = 0; i < face.indexCount; ++i )
        {
            HashUint32( hash, hull.GetFaceIndex( face.firstIndex + i ) );
        }
    }
    return hash;
}

static uint32_t GetConvexHullInstancedMesh( const ConvexHullShape& hull, int& outVertexCount )
{
    const uint64_t hash = HashConvexHullGeometry( hull );
    for ( const ConvexHullMeshResource& resource : sConvexHullMeshes )
    {
        if ( resource.hash == hash )
        {
            outVertexCount = resource.vertexCount;
            return resource.mesh;
        }
    }

    std::vector<float> verts;
    verts.reserve( static_cast<size_t>( hull.GetFaceCount() ) * 6u * 8u );
    auto emitVertex = [&]( uint16_t index, const Vector3& normal, float u, float v )
    {
        const Vector3 p = hull.GetPosition() + hull.GetVertex( index );
        verts.insert( verts.end(), { p.x, p.y, p.z, normal.x, normal.y, normal.z, u, v } );
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

    outVertexCount = static_cast<int>( verts.size() / 8 );
    if ( outVertexCount <= 0 )
    {
        return 0;
    }

    int staticAttribSizes[] = { 3, 3, 2 };
    int instanceAttribSizes[] = { 4, 4, 4, 4, 4, 4, 4 };
    ConvexHullMeshResource resource;
    resource.hash = hash;
    resource.vertexCount = outVertexCount;
    resource.mesh = Gfx().CreateInstancedMesh( verts.data(), outVertexCount, 8, 1, INSTANCE_FLOATS, 3, instanceAttribSizes, 7, staticAttribSizes, 3 );
    sConvexHullMeshes.push_back( resource );
    sConvexHullInstanceData.reserve( INSTANCE_FLOATS );
    return resource.mesh;
}

static void ApplySceneLightConstants( PrimitiveBatchShaderConstants& constants )
{
    const OrdinaryRenderConfig& ordinary = Cfg().ordinaryRender;
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

static void ApplySceneLightUniforms( IShader& shader )
{
    const OrdinaryRenderConfig& ordinary = Cfg().ordinaryRender;
    shader.SetVec4( "uLightAmbient", ordinary.skyAmbientR, ordinary.skyAmbientG, ordinary.skyAmbientB, ordinary.ambientStrength );
    shader.SetVec4( "uLightDiffuse", ordinary.sunColorR * ordinary.sunIntensity, ordinary.sunColorG * ordinary.sunIntensity, ordinary.sunColorB * ordinary.sunIntensity, ordinary.boxRoughnessScale );
}

static void ApplyBatchLightConstants( PrimitiveBatchShaderConstants& constants, const CinematicRenderConfig* cinematicOverride )
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

    ApplySceneLightConstants( constants );
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

static void FillShadowReceiverConstants( PrimitiveBatchShaderConstants& constants, const ShadowFrameData* shadow, bool receive, bool objectReceiver )
{
    const bool enabled = shadow && shadow->valid && receive && shadow->depthTextureHandle != 0;
    Matrix4 identity;
    constants.shadowViewProj = enabled ? shadow->lightViewProjection : identity;
    const float depthBias = enabled
                                ? ( objectReceiver ? (std::max)( shadow->depthBias, 0.0015f ) : shadow->depthBias )
                                : 0.0f;
    const float slopeBias = enabled
                                ? ( objectReceiver ? (std::max)( shadow->slopeBias, 0.0035f ) : shadow->slopeBias )
                                : 0.0f;
    constants.shadowParams[0] = enabled ? shadow->strength : 0.0f;
    constants.shadowParams[1] = depthBias;
    constants.shadowParams[2] = slopeBias;
    constants.shadowParams[3] = enabled ? shadow->texelSize * shadow->softness : 0.0f;
    constants.shadowFlags[0] = enabled ? 1.0f : 0.0f;
    constants.shadowFlags[1] = receive ? 1.0f : 0.0f;
    constants.shadowFlags[2] = enabled ? static_cast<float>( shadow->pcfRadius ) : 0.0f;
    constants.shadowFlags[3] = enabled && shadow->zeroToOneDepth ? 1.0f : 0.0f;

    Gfx().BindTexture( enabled ? shadow->depthTextureHandle : 0, SHADOW_TEXTURE_SLOT );
}

struct PrimitiveBatchShaderParams
{
    const Matrix4& view;
    const Matrix4& projection;
    const float* lightPosition;
    const float* clipPlane;
    const CinematicRenderConfig* cinematic;
    const ShadowFrameData* shadow;
    int primitiveShape;
    bool receiveShadows;
    float materialAlpha;
};

static bool BindPrimitiveBatchShader( IShader& shader, const PrimitiveBatchShaderParams& params )
{
    EnsureMaterialTableTexture();

    float viewLightPos[4];
    for ( int i = 0; i < 3; ++i )
    {
        viewLightPos[i] = params.view.m[i] * params.lightPosition[0] +
                          params.view.m[i + 4] * params.lightPosition[1] +
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
    ApplyBatchLightConstants( constants, params.cinematic );
    FillShadowReceiverConstants( constants, params.shadow, params.receiveShadows, true );
    return shader.SetConstantBufferBytes( &constants, sizeof( constants ), "PrimitiveBatchShaderConstants" );
}

void SkullbonezHelper::SetClipPlane( float x, float y, float z, float w )
{
    sClipPlane[0] = x;
    sClipPlane[1] = y;
    sClipPlane[2] = z;
    sClipPlane[3] = w;
}


const float* SkullbonezHelper::GetClipPlane()
{
    return sClipPlane;
}


void SkullbonezHelper::ResetRenderResources()
{
    sphereShader.reset();
    shadowDepthShader.reset();
    if ( sphereInstMesh != 0 )
    {
        Gfx().DestroyInstancedMesh( sphereInstMesh );
        sphereInstMesh = 0;
    }
    if ( lowPolySphereInstMesh != 0 )
    {
        Gfx().DestroyInstancedMesh( lowPolySphereInstMesh );
        lowPolySphereInstMesh = 0;
    }
    if ( boxInstMesh != 0 )
    {
        Gfx().DestroyInstancedMesh( boxInstMesh );
        boxInstMesh = 0;
    }
    if ( pineInstMesh != 0 )
    {
        Gfx().DestroyInstancedMesh( pineInstMesh );
        pineInstMesh = 0;
    }
    if ( sMaterialTableTexture != 0 )
    {
        Gfx().DeleteTexture( sMaterialTableTexture );
        sMaterialTableTexture = 0;
    }
    activeSphereInstMesh = 0;
    activeSphereVertexCount = 0;
    for ( ConvexHullMeshResource& resource : sConvexHullMeshes )
    {
        if ( resource.mesh != 0 )
        {
            Gfx().DestroyInstancedMesh( resource.mesh );
            resource.mesh = 0;
        }
    }
    sConvexHullMeshes.clear();
}


void SkullbonezHelper::EnsureSphereShader()
{
    if ( !sphereShader )
    {
        sphereShader = SkullbonezCore::Assets::CreateShaderFromActiveAssets( "shader.lit_textured_instanced" );
        sphereShader->Use();
        ApplySceneLightUniforms( *sphereShader );
        sphereShader->SetVec4( "uMaterialAmbient", 0.2f, 0.2f, 0.2f, 1.0f );
        sphereShader->SetVec4( "uMaterialDiffuse", 0.8f, 0.8f, 0.8f, 1.0f );
        sphereShader->SetFloat( "uMaterialAlpha", 1.0f );
    }
}


void SkullbonezHelper::EnsureShadowDepthShader()
{
    if ( !shadowDepthShader )
    {
        // One shared instanced depth shader is enough for balls, boxes, and pine
        // visuals because all three meshes expose the same static attributes and
        // per-instance material layout. The fragment output is irrelevant; the
        // depth attachment is the shadow map product.
        shadowDepthShader = SkullbonezCore::Assets::CreateShaderFromActiveAssets( "shader.shadow_depth_instanced" );
    }
}


void SkullbonezHelper::EnsureSphereMesh()
{
    if ( sphereInstMesh == 0 )
    {
        BuildSphereMesh( 25, 25 );
    }
    EnsureSphereShader();
}


void SkullbonezHelper::BuildSphereMesh( int slices, int stacks )
{
    std::vector<float> verts;
    verts.reserve( PrimitiveMeshes::SphereTriangleVertexCount( slices, stacks ) * 8 );

    PrimitiveMeshes::EmitUnitSphere( slices, stacks, [&]( const PrimitiveMeshes::VertexPNUV& vertex )
                                     { verts.insert( verts.end(), { vertex.x, vertex.y, vertex.z, vertex.nx, vertex.ny, vertex.nz, vertex.u, vertex.v } ); } );

    sphereVertexCount = PrimitiveMeshes::SphereTriangleVertexCount( slices, stacks );

    // Static layout: 3 attributes (pos3, normal3, uv2) at locations 0-2
    int staticAttribSizes[] = { 3, 3, 2 };
    // Instance layout: model matrix plus three float4 material rows, starting at location 3.
    int instanceAttribSizes[] = { 4, 4, 4, 4, 4, 4, 4 };
    sphereInstMesh = Gfx().CreateInstancedMesh( verts.data(), sphereVertexCount, 8, MAX_GAME_MODELS, INSTANCE_FLOATS, 3, instanceAttribSizes, 7, staticAttribSizes, 3 );

    sphereInstanceData.reserve( MAX_GAME_MODELS * INSTANCE_FLOATS );
}


void SkullbonezHelper::BuildLowPolySphereMesh( int slices, int stacks )
{
    std::vector<float> verts;
    verts.reserve( PrimitiveMeshes::SphereTriangleVertexCount( slices, stacks ) * 8 );

    PrimitiveMeshes::EmitUnitSphereFlat( slices, stacks, [&]( const PrimitiveMeshes::VertexPNUV& vertex )
                                         { verts.insert( verts.end(), { vertex.x, vertex.y, vertex.z, vertex.nx, vertex.ny, vertex.nz, vertex.u, vertex.v } ); } );

    lowPolySphereVertexCount = PrimitiveMeshes::SphereTriangleVertexCount( slices, stacks );

    int staticAttribSizes[] = { 3, 3, 2 };
    int instanceAttribSizes[] = { 4, 4, 4, 4, 4, 4, 4 };
    lowPolySphereInstMesh = Gfx().CreateInstancedMesh( verts.data(), lowPolySphereVertexCount, 8, MAX_GAME_MODELS, INSTANCE_FLOATS, 3, instanceAttribSizes, 7, staticAttribSizes, 3 );

    sphereInstanceData.reserve( MAX_GAME_MODELS * INSTANCE_FLOATS );
}


void SkullbonezHelper::DrawSphereBatchBegin( const Matrix4& view, const Matrix4& proj, const float lightPos[4], bool isTransparent, const CinematicRenderConfig* cinematic, const ShadowFrameData* shadow, float materialAlpha )
{
    sSphereBatchTransparent = isTransparent;
    sSphereBatchReady = false;
    const int objectStyle = ObjectStyleForMeshSelection( cinematic );
    const bool useLowPolySphereMesh = objectStyle == 6;
    if ( useLowPolySphereMesh )
    {
        if ( lowPolySphereInstMesh == 0 )
        {
            BuildLowPolySphereMesh( 12, 7 );
        }
        activeSphereInstMesh = lowPolySphereInstMesh;
        activeSphereVertexCount = lowPolySphereVertexCount;
    }
    else
    {
        if ( sphereInstMesh == 0 )
        {
            BuildSphereMesh( 25, 25 );
        }
        activeSphereInstMesh = sphereInstMesh;
        activeSphereVertexCount = sphereVertexCount;
    }
    EnsureSphereShader();

    BeginPrimitiveBatchTransparency( isTransparent );

    // Low-poly spheres still cast real shadows onto terrain, but they do not
    // receive object shadows. The shadow map is single and terrain-sized, so
    // ball-on-ball receiver shadows alias badly across the large flat facets
    // used by the low-poly beachball style.
    const bool receiveSphereShadows = shadow && shadow->objectsReceive && !useLowPolySphereMesh;
    sSphereBatchReady = BindPrimitiveBatchShader( *sphereShader,
                                                  { view,
                                                    proj,
                                                    lightPos,
                                                    sClipPlane,
                                                    cinematic,
                                                    shadow,
                                                    PRIMITIVE_SHAPE_SPHERE,
                                                    receiveSphereShadows,
                                                    materialAlpha } );
    sphereInstanceData.clear();
}


void SkullbonezHelper::DrawSphereBatchModel( const Matrix4& model, const RenderMaterial& material )
{
    AppendMaterialInstancePayload( sphereInstanceData, model, material );
}


void SkullbonezHelper::DrawSphereBatchModel( const Matrix4& model, float tintR, float tintG, float tintB, float colorOverride )
{
    DrawSphereBatchModel( model, MakeRenderMaterialFromLegacyTint( tintR, tintG, tintB, colorOverride ) );
}


void SkullbonezHelper::DrawSphereBatchEnd()
{
    int instanceCount = static_cast<int>( sphereInstanceData.size() ) / INSTANCE_FLOATS;
    if ( instanceCount > 0 && activeSphereInstMesh != 0 )
    {
        if ( sSphereBatchReady )
        {
            Gfx().UploadInstanceData( activeSphereInstMesh, sphereInstanceData.data(), static_cast<int>( sphereInstanceData.size() ) );
            Gfx().DrawInstancedMesh( activeSphereInstMesh, activeSphereVertexCount, instanceCount );
        }
    }
    EndPrimitiveBatchTransparency( sSphereBatchTransparent );
    sSphereBatchTransparent = false;
    sSphereBatchReady = false;
}


void SkullbonezHelper::DrawShadowDepthSphereBatchBegin( const Matrix4& view, const Matrix4& proj, const CinematicRenderConfig* cinematic )
{
    sSphereBatchReady = false;
    // Match the visible sphere mesh selection. If a low-poly style is active,
    // the depth pass also uses the faceted mesh, which prevents a smooth sphere
    // shadow from appearing under a visibly low-poly ball.
    const bool useLowPolySphereMesh = ObjectStyleForMeshSelection( cinematic ) == 6;
    if ( useLowPolySphereMesh )
    {
        if ( lowPolySphereInstMesh == 0 )
        {
            BuildLowPolySphereMesh( 12, 7 );
        }
        activeSphereInstMesh = lowPolySphereInstMesh;
        activeSphereVertexCount = lowPolySphereVertexCount;
    }
    else
    {
        if ( sphereInstMesh == 0 )
        {
            BuildSphereMesh( 25, 25 );
        }
        activeSphereInstMesh = sphereInstMesh;
        activeSphereVertexCount = sphereVertexCount;
    }

    EnsureShadowDepthShader();
    shadowDepthShader->Use();
    InstancedShadowDepthConstants constants = {};
    constants.view = view;
    constants.projection = proj;
    constants.clipPlane[0] = sClipPlane[0];
    constants.clipPlane[1] = sClipPlane[1];
    constants.clipPlane[2] = sClipPlane[2];
    constants.clipPlane[3] = sClipPlane[3];
    sSphereBatchReady = shadowDepthShader->SetConstantBufferBytes( &constants, sizeof( constants ), "InstancedShadowDepthConstants" );
    sphereInstanceData.clear();
}


void SkullbonezHelper::DrawShadowDepthSphereBatchModel( const Matrix4& model )
{
    DrawSphereBatchModel( model, 1.0f, 1.0f, 1.0f, 0.0f );
}


void SkullbonezHelper::DrawShadowDepthSphereBatchEnd()
{
    int instanceCount = static_cast<int>( sphereInstanceData.size() ) / INSTANCE_FLOATS;
    if ( sSphereBatchReady && instanceCount > 0 && activeSphereInstMesh != 0 )
    {
        // Upload only the compact per-instance stream, then issue one instanced
        // draw for every sphere caster. This keeps the shadow pass draw-call
        // count predictable even in scenes with hundreds of balls.
        Gfx().UploadInstanceData( activeSphereInstMesh, sphereInstanceData.data(), static_cast<int>( sphereInstanceData.size() ) );
        Gfx().DrawInstancedMesh( activeSphereInstMesh, activeSphereVertexCount, instanceCount );
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


void SkullbonezHelper::BuildBoxMesh()
{
    std::vector<float> verts;
    verts.reserve( PrimitiveMeshes::BoxTriangleVertexCount() * 8 );

    PrimitiveMeshes::EmitUnitBox( [&]( const PrimitiveMeshes::VertexPNUV& vertex )
                                  { verts.insert( verts.end(), { vertex.x, vertex.y, vertex.z, vertex.nx, vertex.ny, vertex.nz, vertex.u, vertex.v } ); } );

    boxVertexCount = PrimitiveMeshes::BoxTriangleVertexCount();

    int staticAttribSizes[] = { 3, 3, 2 };
    int instanceAttribSizes[] = { 4, 4, 4, 4, 4, 4, 4 };
    boxInstMesh = Gfx().CreateInstancedMesh( verts.data(), boxVertexCount, 8, MAX_GAME_MODELS, INSTANCE_FLOATS, 3, instanceAttribSizes, 7, staticAttribSizes, 3 );

    boxInstanceData.reserve( MAX_GAME_MODELS * INSTANCE_FLOATS );
}


void SkullbonezHelper::DrawBoxBatchBegin( const Matrix4& view, const Matrix4& proj, const float lightPos[4], bool isTransparent, const CinematicRenderConfig* cinematic, const ShadowFrameData* shadow, float materialAlpha )
{
    sBoxBatchTransparent = isTransparent;
    sBoxBatchReady = false;
    if ( boxInstMesh == 0 )
    {
        BuildBoxMesh();
    }

    // Reuse sphere shader (same vertex layout, same lighting model).
    EnsureSphereShader();

    BeginPrimitiveBatchTransparency( isTransparent );

    sBoxBatchReady = BindPrimitiveBatchShader( *sphereShader,
                                               { view,
                                                 proj,
                                                 lightPos,
                                                 sClipPlane,
                                                 cinematic,
                                                 shadow,
                                                 PRIMITIVE_SHAPE_MESH,
                                                 shadow ? shadow->objectsReceive : false,
                                                 materialAlpha } );
    boxInstanceData.clear();
}


void SkullbonezHelper::DrawBoxBatchModel( const Matrix4& model, const RenderMaterial& material )
{
    AppendMaterialInstancePayload( boxInstanceData, model, material );
}


void SkullbonezHelper::DrawBoxBatchModel( const Matrix4& model, float tintR, float tintG, float tintB, float colorOverride )
{
    DrawBoxBatchModel( model, MakeRenderMaterialFromLegacyTint( tintR, tintG, tintB, colorOverride ) );
}


void SkullbonezHelper::DrawBoxBatchEnd()
{
    int instanceCount = static_cast<int>( boxInstanceData.size() ) / INSTANCE_FLOATS;
    if ( sBoxBatchReady && instanceCount > 0 )
    {
        Gfx().UploadInstanceData( boxInstMesh, boxInstanceData.data(), static_cast<int>( boxInstanceData.size() ) );
        Gfx().DrawInstancedMesh( boxInstMesh, boxVertexCount, instanceCount );
    }
    EndPrimitiveBatchTransparency( sBoxBatchTransparent );
    sBoxBatchTransparent = false;
    sBoxBatchReady = false;
}


void SkullbonezHelper::DrawShadowDepthBoxBatchBegin( const Matrix4& view, const Matrix4& proj )
{
    sBoxBatchReady = false;
    if ( boxInstMesh == 0 )
    {
        BuildBoxMesh();
    }
    EnsureShadowDepthShader();
    shadowDepthShader->Use();
    InstancedShadowDepthConstants constants = {};
    constants.view = view;
    constants.projection = proj;
    constants.clipPlane[0] = sClipPlane[0];
    constants.clipPlane[1] = sClipPlane[1];
    constants.clipPlane[2] = sClipPlane[2];
    constants.clipPlane[3] = sClipPlane[3];
    sBoxBatchReady = shadowDepthShader->SetConstantBufferBytes( &constants, sizeof( constants ), "InstancedShadowDepthConstants" );
    boxInstanceData.clear();
}


void SkullbonezHelper::DrawShadowDepthBoxBatchModel( const Matrix4& model )
{
    DrawBoxBatchModel( model, 1.0f, 1.0f, 1.0f, 0.0f );
}


void SkullbonezHelper::DrawShadowDepthBoxBatchEnd()
{
    int instanceCount = static_cast<int>( boxInstanceData.size() ) / INSTANCE_FLOATS;
    if ( sBoxBatchReady && instanceCount > 0 && boxInstMesh != 0 )
    {
        // This draw is the box-caster fix point: if a scene has boxes and shadow
        // maps are active, their depth is written here before terrain/objects
        // sample the map in the forward pass.
        Gfx().UploadInstanceData( boxInstMesh, boxInstanceData.data(), static_cast<int>( boxInstanceData.size() ) );
        Gfx().DrawInstancedMesh( boxInstMesh, boxVertexCount, instanceCount );
    }
    sBoxBatchReady = false;
}

void SkullbonezHelper::DrawConvexHullModel( const ConvexHullShape& hull,
                                            const Matrix4& model,
                                            const RenderMaterial& material,
                                            const Matrix4& view,
                                            const Matrix4& proj,
                                            const float lightPos[4],
                                            bool isTransparent,
                                            const CinematicRenderConfig* cinematic,
                                            const ShadowFrameData* shadow,
                                            float materialAlpha )
{
    int vertexCount = 0;
    const uint32_t mesh = GetConvexHullInstancedMesh( hull, vertexCount );
    if ( mesh == 0 || vertexCount <= 0 )
    {
        return;
    }

    EnsureSphereShader();
    BeginPrimitiveBatchTransparency( isTransparent );
    const bool ready = BindPrimitiveBatchShader( *sphereShader,
                                                 { view,
                                                   proj,
                                                   lightPos,
                                                   sClipPlane,
                                                   cinematic,
                                                   shadow,
                                                   PRIMITIVE_SHAPE_MESH,
                                                   shadow ? shadow->objectsReceive : false,
                                                   materialAlpha } );
    if ( ready )
    {
        sConvexHullInstanceData.clear();
        AppendMaterialInstancePayload( sConvexHullInstanceData, model, material );
        Gfx().UploadInstanceData( mesh, sConvexHullInstanceData.data(), static_cast<int>( sConvexHullInstanceData.size() ) );
        Gfx().DrawInstancedMesh( mesh, vertexCount, 1 );
    }
    EndPrimitiveBatchTransparency( isTransparent );
}

void SkullbonezHelper::DrawShadowDepthConvexHullModel( const ConvexHullShape& hull, const Matrix4& model, const Matrix4& view, const Matrix4& proj )
{
    int vertexCount = 0;
    const uint32_t mesh = GetConvexHullInstancedMesh( hull, vertexCount );
    if ( mesh == 0 || vertexCount <= 0 )
    {
        return;
    }

    EnsureShadowDepthShader();
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
        sConvexHullInstanceData.clear();
        AppendMaterialInstancePayload( sConvexHullInstanceData, model, MakeRenderMaterialFromLegacyTint( 1.0f, 1.0f, 1.0f, 0.0f ) );
        Gfx().UploadInstanceData( mesh, sConvexHullInstanceData.data(), static_cast<int>( sConvexHullInstanceData.size() ) );
        Gfx().DrawInstancedMesh( mesh, vertexCount, 1 );
    }
}


void SkullbonezHelper::BuildPineMesh()
{
    std::vector<float> verts;
    verts.reserve( PrimitiveMeshes::PineTriangleVertexCount() * 8 );

    PrimitiveMeshes::EmitUnitPinePyramid( [&]( const PrimitiveMeshes::VertexPNUV& vertex )
                                          { verts.insert( verts.end(), { vertex.x, vertex.y, vertex.z, vertex.nx, vertex.ny, vertex.nz, vertex.u, vertex.v } ); } );

    pineVertexCount = PrimitiveMeshes::PineTriangleVertexCount();

    int staticAttribSizes[] = { 3, 3, 2 };
    int instanceAttribSizes[] = { 4, 4, 4, 4, 4, 4, 4 };
    pineInstMesh = Gfx().CreateInstancedMesh( verts.data(), pineVertexCount, 8, MAX_GAME_MODELS, INSTANCE_FLOATS, 3, instanceAttribSizes, 7, staticAttribSizes, 3 );

    pineInstanceData.reserve( MAX_GAME_MODELS * INSTANCE_FLOATS );
}


void SkullbonezHelper::DrawPineBatchBegin( const Matrix4& view, const Matrix4& proj, const float lightPos[4], bool isTransparent, const CinematicRenderConfig* cinematic, const ShadowFrameData* shadow, float materialAlpha )
{
    sPineBatchTransparent = isTransparent;
    sPineBatchReady = false;
    if ( pineInstMesh == 0 )
    {
        BuildPineMesh();
    }

    EnsureSphereShader();

    BeginPrimitiveBatchTransparency( isTransparent );

    sPineBatchReady = BindPrimitiveBatchShader( *sphereShader,
                                                { view,
                                                  proj,
                                                  lightPos,
                                                  sClipPlane,
                                                  cinematic,
                                                  shadow,
                                                  PRIMITIVE_SHAPE_MESH,
                                                  shadow ? shadow->objectsReceive : false,
                                                  materialAlpha } );
    pineInstanceData.clear();
}


void SkullbonezHelper::DrawPineBatchModel( const Matrix4& model, const RenderMaterial& material )
{
    AppendMaterialInstancePayload( pineInstanceData, model, material );
}


void SkullbonezHelper::DrawPineBatchModel( const Matrix4& model, float tintR, float tintG, float tintB, float colorOverride )
{
    DrawPineBatchModel( model, MakeRenderMaterialFromLegacyTint( tintR, tintG, tintB, colorOverride ) );
}


void SkullbonezHelper::DrawPineBatchEnd()
{
    int instanceCount = static_cast<int>( pineInstanceData.size() ) / INSTANCE_FLOATS;
    if ( sPineBatchReady && instanceCount > 0 )
    {
        Gfx().UploadInstanceData( pineInstMesh, pineInstanceData.data(), static_cast<int>( pineInstanceData.size() ) );
        Gfx().DrawInstancedMesh( pineInstMesh, pineVertexCount, instanceCount );
    }
    EndPrimitiveBatchTransparency( sPineBatchTransparent );
    sPineBatchTransparent = false;
    sPineBatchReady = false;
}


void SkullbonezHelper::DrawShadowDepthPineBatchBegin( const Matrix4& view, const Matrix4& proj )
{
    sPineBatchReady = false;
    if ( pineInstMesh == 0 )
    {
        BuildPineMesh();
    }
    EnsureShadowDepthShader();
    shadowDepthShader->Use();
    InstancedShadowDepthConstants constants = {};
    constants.view = view;
    constants.projection = proj;
    constants.clipPlane[0] = sClipPlane[0];
    constants.clipPlane[1] = sClipPlane[1];
    constants.clipPlane[2] = sClipPlane[2];
    constants.clipPlane[3] = sClipPlane[3];
    sPineBatchReady = shadowDepthShader->SetConstantBufferBytes( &constants, sizeof( constants ), "InstancedShadowDepthConstants" );
    pineInstanceData.clear();
}


void SkullbonezHelper::DrawShadowDepthPineBatchModel( const Matrix4& model )
{
    DrawPineBatchModel( model, 1.0f, 1.0f, 1.0f, 0.0f );
}


void SkullbonezHelper::DrawShadowDepthPineBatchEnd()
{
    int instanceCount = static_cast<int>( pineInstanceData.size() ) / INSTANCE_FLOATS;
    if ( sPineBatchReady && instanceCount > 0 && pineInstMesh != 0 )
    {
        Gfx().UploadInstanceData( pineInstMesh, pineInstanceData.data(), static_cast<int>( pineInstanceData.size() ) );
        Gfx().DrawInstancedMesh( pineInstMesh, pineVertexCount, instanceCount );
    }
    sPineBatchReady = false;
}


void SkullbonezHelper::StateSetup()
{
    // Initial render state is owned by the DX12 backend. This hook remains as a
    // small extension point for any helper-level setup that must happen after
    // the renderer has initialized.
}
