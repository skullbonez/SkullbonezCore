/*
File: SkullbonezSource/Rendering/Helper.cpp
Purpose:
  Collects legacy helper routines that bridge engine subsystems.

Mental model:
  Helper.cpp collects legacy helper routines that bridge engine subsystems. As
  an implementation unit, keep edits anchored on render submission and
  resource lifetime and on the glossary/invariants below.

Glossary:
  Cbuffer (Constant Buffer): Shader constant block uploaded once before a draw.
  Material table: Fixed t4 texture that stores default per-kind material response
  values for object shaders.
  Instance payload: Per-object data appended after the model matrix in an
  instanced draw stream.

Invariants:
  - C++ constant-buffer structs must match reflected HLSL cbuffer size and
    field order, or the draw that depends on them must be skipped.
  - Helper-owned mesh/shader handles are backend resources and must be released
    by the helper destructor before backend teardown or recreation.

Related:
  - SkullbonezSource/Rendering/Helper.h
  - Agentic/Reference/runtime-reference.md
  - Agentic/Reference/comment-style-guide.md
*/
#include "Helper.h"
#include "../Core/Config.h"
#include "../GameObjects/SceneCapacity.h"
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


static constexpr int INSTANCE_MATRIX_FLOATS = RenderHelperState::INSTANCE_MATRIX_FLOATS;
static constexpr int INSTANCE_FLOATS = RenderHelperState::INSTANCE_FLOATS;
static constexpr int HULL_MAX_TRIANGLE_VERTICES = RenderHelperState::HULL_MAX_TRIANGLE_VERTICES;
static constexpr int HULL_DYNAMIC_FLOATS_PER_VERTEX = RenderHelperState::HULL_DYNAMIC_FLOATS_PER_VERTEX;
static constexpr int PRIMITIVE_SHAPE_MESH = 0;
static constexpr int PRIMITIVE_SHAPE_SPHERE = 1;
static constexpr int MATERIAL_TABLE_WIDTH = 16;
static constexpr int MATERIAL_TABLE_TEXTURE_SLOT = 4;

static IRenderResourceFactory& Resources( const RenderHelperContext& context )
{
    return context.renderResources;
}

static IRenderCommandContext& Commands( const RenderHelperContext& context )
{
    return context.renderCommands;
}

static const SkullbonezCore::Assets::AssetSystem& AssetRegistry( const RenderHelperContext& context )
{
    return context.assets;
}

static const EngineConfig& Config( const RenderHelperContext& context )
{
    return context.config;
}

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

static void BeginPrimitiveBatchTransparency( const RenderHelperContext& context, bool isTransparent )
{
    if ( isTransparent )
    {
        Commands( context ).SetBlend( true );
        Commands( context ).SetBlendFunc( BlendFactor::SrcAlpha, BlendFactor::OneMinusSrcAlpha );
        Commands( context ).SetDepthWrite( false );
    }
}

static void EndPrimitiveBatchTransparency( const RenderHelperContext& context, bool wasTransparent )
{
    if ( wasTransparent )
    {
        Commands( context ).SetDepthWrite( true );
    }
    Commands( context ).SetBlend( false );
}

static uint8_t MaterialByte( float value )
{
    // The material table is an 8-bit texture, so clamp and round normalized
    // material parameters at the CPU boundary before the shader samples them.
    return static_cast<uint8_t>( std::clamp( value, 0.0f, 1.0f ) * 255.0f + 0.5f );
}

static void EnsureMaterialTableTexture( const RenderHelperContext& context, RenderHelperState& state )
{
    // Concept: the current object material table is a tiny texture, not a
    // structured buffer or bindless descriptor table.
    //
    // Each texel row stores default roughness, metallic, specular, and
    // stylization for one RenderMaterialKind. The per-instance payload still
    // carries draw-local values; the t4 table gives shaders a stable fallback
    // and a validation-visible binding point without expanding the resource
    // model beyond the ordinary raster ABI.
    if ( state.materialTableTexture != 0 )
    {
        Commands( context ).BindTexture( state.materialTableTexture, MATERIAL_TABLE_TEXTURE_SLOT );
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

    state.materialTableTexture = Resources( context ).CreateTexture2D( rows, MATERIAL_TABLE_WIDTH, 1, 4, false, false );
    Commands( context ).BindTexture( state.materialTableTexture, MATERIAL_TABLE_TEXTURE_SLOT );
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

static void EnsureConvexHullDynamicVB( const RenderHelperContext& context, RenderHelperState& state )
{
    if ( state.convexHullDynamicVB != 0 )
    {
        return;
    }

    int attribs[] = { 3, 3, 2, 4, 4, 4, 4, 4, 4, 4, 4 };
    state.convexHullDynamicVB = Resources( context ).CreateDynamicVB( attribs, 11, HULL_MAX_TRIANGLE_VERTICES );
}

static int BuildConvexHullDynamicVertices( const ConvexHullShape& hull,
                                           const std::array<float, INSTANCE_FLOATS>& instancePayload,
                                           RenderHelperState& state )
{
    int vertexCount = 0;
    auto emitVertex = [&]( uint16_t index, const Vector3& normal, float u, float v )
    {
        if ( vertexCount >= HULL_MAX_TRIANGLE_VERTICES )
        {
            return;
        }

        const Vector3 p = hull.GetPosition() + hull.GetVertex( index );
        float* out = &state.convexHullVertexData[static_cast<size_t>( vertexCount ) * HULL_DYNAMIC_FLOATS_PER_VERTEX];
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

static void ApplySceneLightConstants( const RenderHelperContext& context, PrimitiveBatchShaderConstants& constants )
{
    const OrdinaryRenderConfig& ordinary = Config( context ).ordinaryRender;
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

static void ApplySceneLightUniforms( const RenderHelperContext& context, IShader& shader )
{
    const OrdinaryRenderConfig& ordinary = Config( context ).ordinaryRender;
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
                                      const RenderHelperContext& context,
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

    ApplySceneLightConstants( context, constants );
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

static void FillShadowReceiverConstants( PrimitiveBatchShaderConstants& constants,
                                         const RenderHelperContext& context,
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

    Commands( context ).BindTexture( enabled ? shadow->depthTextureHandle : 0, SHADOW_TEXTURE_SLOT );
}

struct PrimitiveBatchShaderParams
{
    const RenderHelperContext& context;
    RenderHelperState& helperState;
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
    EnsureMaterialTableTexture( params.context, params.helperState );

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
    ApplyBatchLightConstants( constants, params.context, params.cinematic );
    FillShadowReceiverConstants( constants, params.context, params.shadow, params.receiveShadows, true );
    return shader.SetConstantBufferBytes( &constants, sizeof( constants ), "PrimitiveBatchShaderConstants" );
}

void RenderHelper::SetClipPlane( float x, float y, float z, float w )
{
    m_state.clipPlane[0] = x;
    m_state.clipPlane[1] = y;
    m_state.clipPlane[2] = z;
    m_state.clipPlane[3] = w;
}


const float* RenderHelper::GetClipPlane() const
{
    return m_state.clipPlane;
}


RenderHelper::RenderHelper( IRenderResourceFactory* renderResources )
{
    m_state.renderResources = renderResources;
}


RenderHelper::~RenderHelper()
{
    ReleaseOwnedRenderResources();
}


RenderHelper::PrimitiveBatchScope::PrimitiveBatchScope( RenderHelper& helper,
                                                        const RenderHelperContext& context,
                                                        PrimitiveBatchKind kind )
    : m_helper( &helper ), m_context( &context ), m_kind( kind ), m_active( true )
{
}


RenderHelper::PrimitiveBatchScope::PrimitiveBatchScope( PrimitiveBatchScope&& other ) noexcept
    : m_helper( other.m_helper ), m_context( other.m_context ), m_kind( other.m_kind ), m_active( other.m_active )
{
    other.m_active = false;
}


RenderHelper::PrimitiveBatchScope& RenderHelper::PrimitiveBatchScope::operator=( PrimitiveBatchScope&& other ) noexcept
{
    if ( this != &other )
    {
        EndIfActive();
        m_helper = other.m_helper;
        m_context = other.m_context;
        m_kind = other.m_kind;
        m_active = other.m_active;
        other.m_active = false;
    }
    return *this;
}


RenderHelper::PrimitiveBatchScope::~PrimitiveBatchScope()
{
    EndIfActive();
}


void RenderHelper::PrimitiveBatchScope::DrawModel( const Matrix4& model, const RenderMaterial& material )
{
    assert( m_helper && m_active );
    switch ( m_kind )
    {
    case PrimitiveBatchKind::Sphere:
        m_helper->DrawSphereBatchModel( model, material );
        break;
    case PrimitiveBatchKind::Box:
        m_helper->DrawBoxBatchModel( model, material );
        break;
    case PrimitiveBatchKind::Pine:
        m_helper->DrawPineBatchModel( model, material );
        break;
    default:
        assert( false && "DrawModel requires a visible primitive batch scope" );
        break;
    }
}


void RenderHelper::PrimitiveBatchScope::DrawModel( const Matrix4& model,
                                                   float tintR,
                                                   float tintG,
                                                   float tintB,
                                                   float colorOverride )
{
    DrawModel( model, MakeRenderMaterialFromLegacyTint( tintR, tintG, tintB, colorOverride ) );
}


void RenderHelper::PrimitiveBatchScope::DrawShadowModel( const Matrix4& model )
{
    assert( m_helper && m_active );
    switch ( m_kind )
    {
    case PrimitiveBatchKind::ShadowSphere:
        m_helper->DrawShadowDepthSphereBatchModel( model );
        break;
    case PrimitiveBatchKind::ShadowBox:
        m_helper->DrawShadowDepthBoxBatchModel( model );
        break;
    case PrimitiveBatchKind::ShadowPine:
        m_helper->DrawShadowDepthPineBatchModel( model );
        break;
    default:
        assert( false && "DrawShadowModel requires a shadow primitive batch scope" );
        break;
    }
}


void RenderHelper::PrimitiveBatchScope::EndIfActive()
{
    if ( !m_active || !m_helper || !m_context )
    {
        return;
    }

    switch ( m_kind )
    {
    case PrimitiveBatchKind::Sphere:
        m_helper->DrawSphereBatchEnd( *m_context );
        break;
    case PrimitiveBatchKind::Box:
        m_helper->DrawBoxBatchEnd( *m_context );
        break;
    case PrimitiveBatchKind::Pine:
        m_helper->DrawPineBatchEnd( *m_context );
        break;
    case PrimitiveBatchKind::ShadowSphere:
        m_helper->DrawShadowDepthSphereBatchEnd( *m_context );
        break;
    case PrimitiveBatchKind::ShadowBox:
        m_helper->DrawShadowDepthBoxBatchEnd( *m_context );
        break;
    case PrimitiveBatchKind::ShadowPine:
        m_helper->DrawShadowDepthPineBatchEnd( *m_context );
        break;
    }

    m_active = false;
}


RenderHelper::PrimitiveBatchScope RenderHelper::BeginSphereBatch( const RenderHelperContext& context,
                                                                  const Matrix4& view,
                                                                  const Matrix4& proj,
                                                                  const float lightPos[4],
                                                                  bool isTransparent,
                                                                  const CinematicRenderConfig* cinematic,
                                                                  const ShadowFrameData* shadow,
                                                                  float materialAlpha )
{
    BindRenderResourceFactory( context.renderResources );
    DrawSphereBatchBegin( context, view, proj, lightPos, isTransparent, cinematic, shadow, materialAlpha );
    return PrimitiveBatchScope( *this, context, PrimitiveBatchKind::Sphere );
}


RenderHelper::PrimitiveBatchScope RenderHelper::BeginBoxBatch( const RenderHelperContext& context,
                                                               const Matrix4& view,
                                                               const Matrix4& proj,
                                                               const float lightPos[4],
                                                               bool isTransparent,
                                                               const CinematicRenderConfig* cinematic,
                                                               const ShadowFrameData* shadow,
                                                               float materialAlpha )
{
    BindRenderResourceFactory( context.renderResources );
    DrawBoxBatchBegin( context, view, proj, lightPos, isTransparent, cinematic, shadow, materialAlpha );
    return PrimitiveBatchScope( *this, context, PrimitiveBatchKind::Box );
}


RenderHelper::PrimitiveBatchScope RenderHelper::BeginPineBatch( const RenderHelperContext& context,
                                                                const Matrix4& view,
                                                                const Matrix4& proj,
                                                                const float lightPos[4],
                                                                bool isTransparent,
                                                                const CinematicRenderConfig* cinematic,
                                                                const ShadowFrameData* shadow,
                                                                float materialAlpha )
{
    BindRenderResourceFactory( context.renderResources );
    DrawPineBatchBegin( context, view, proj, lightPos, isTransparent, cinematic, shadow, materialAlpha );
    return PrimitiveBatchScope( *this, context, PrimitiveBatchKind::Pine );
}


RenderHelper::PrimitiveBatchScope RenderHelper::BeginShadowDepthSphereBatch( const RenderHelperContext& context,
                                                                             const Matrix4& view,
                                                                             const Matrix4& proj,
                                                                             const CinematicRenderConfig* cinematic )
{
    BindRenderResourceFactory( context.renderResources );
    DrawShadowDepthSphereBatchBegin( context, view, proj, cinematic );
    return PrimitiveBatchScope( *this, context, PrimitiveBatchKind::ShadowSphere );
}


RenderHelper::PrimitiveBatchScope
RenderHelper::BeginShadowDepthBoxBatch( const RenderHelperContext& context, const Matrix4& view, const Matrix4& proj )
{
    BindRenderResourceFactory( context.renderResources );
    DrawShadowDepthBoxBatchBegin( context, view, proj );
    return PrimitiveBatchScope( *this, context, PrimitiveBatchKind::ShadowBox );
}


RenderHelper::PrimitiveBatchScope
RenderHelper::BeginShadowDepthPineBatch( const RenderHelperContext& context, const Matrix4& view, const Matrix4& proj )
{
    BindRenderResourceFactory( context.renderResources );
    DrawShadowDepthPineBatchBegin( context, view, proj );
    return PrimitiveBatchScope( *this, context, PrimitiveBatchKind::ShadowPine );
}


void RenderHelper::BindRenderResourceFactory( IRenderResourceFactory& renderResources )
{
    assert( !m_state.renderResources || m_state.renderResources == &renderResources );
    m_state.renderResources = &renderResources;
}


void RenderHelper::ReleaseOwnedRenderResources()
{
    IRenderResourceFactory* renderResources = m_state.renderResources;
    m_state.sphereShader.reset();
    m_state.shadowDepthShader.reset();
    if ( m_state.sphereInstMesh != 0 )
    {
        if ( renderResources )
        {
            renderResources->DestroyInstancedMesh( m_state.sphereInstMesh );
        }
        m_state.sphereInstMesh = 0;
    }
    if ( m_state.lowPolySphereInstMesh != 0 )
    {
        if ( renderResources )
        {
            renderResources->DestroyInstancedMesh( m_state.lowPolySphereInstMesh );
        }
        m_state.lowPolySphereInstMesh = 0;
    }
    if ( m_state.boxInstMesh != 0 )
    {
        if ( renderResources )
        {
            renderResources->DestroyInstancedMesh( m_state.boxInstMesh );
        }
        m_state.boxInstMesh = 0;
    }
    if ( m_state.pineInstMesh != 0 )
    {
        if ( renderResources )
        {
            renderResources->DestroyInstancedMesh( m_state.pineInstMesh );
        }
        m_state.pineInstMesh = 0;
    }
    if ( m_state.materialTableTexture != 0 )
    {
        if ( renderResources )
        {
            renderResources->DeleteTexture( m_state.materialTableTexture );
        }
        m_state.materialTableTexture = 0;
    }
    if ( m_state.convexHullDynamicVB != 0 )
    {
        if ( renderResources )
        {
            renderResources->DestroyDynamicVB( m_state.convexHullDynamicVB );
        }
        m_state.convexHullDynamicVB = 0;
    }
    m_state.activeSphereInstMesh = 0;
    m_state.activeSphereVertexCount = 0;
    m_state.renderResources = nullptr;
}


void RenderHelper::EnsureSphereShader( const RenderHelperContext& context )
{
    if ( !m_state.sphereShader )
    {
        m_state.sphereShader =
            AssetRegistry( context ).CreateShader( Resources( context ), "shader.lit_textured_instanced" );
        if ( !m_state.sphereShader )
        {
            return;
        }
        m_state.sphereShader->Use();
        ApplySceneLightUniforms( context, *m_state.sphereShader );
        m_state.sphereShader->SetVec4( "uMaterialAmbient", 0.2f, 0.2f, 0.2f, 1.0f );
        m_state.sphereShader->SetVec4( "uMaterialDiffuse", 0.8f, 0.8f, 0.8f, 1.0f );
        m_state.sphereShader->SetFloat( "uMaterialAlpha", 1.0f );
    }
}


void RenderHelper::EnsureShadowDepthShader( const RenderHelperContext& context )
{
    if ( !m_state.shadowDepthShader )
    {
        // One shared instanced depth shader is enough for balls, boxes, and pine
        // visuals because all three meshes expose the same static attributes and
        // per-instance material layout. The fragment output is irrelevant; the
        // depth attachment is the shadow map product.
        m_state.shadowDepthShader =
            AssetRegistry( context ).CreateShader( Resources( context ), "shader.shadow_depth_instanced" );
    }
}


void RenderHelper::EnsureSphereMesh( const RenderHelperContext& context )
{
    BindRenderResourceFactory( context.renderResources );
    if ( m_state.sphereInstMesh == 0 )
    {
        BuildSphereMesh( context, 25, 25 );
    }
    EnsureSphereShader( context );
}


void RenderHelper::EnsureShadowDepthPrimitiveResources( const RenderHelperContext& context )
{
    BindRenderResourceFactory( context.renderResources );
    // Runtime allocation policy: the first shadowed frame must not compile the
    // shared depth shader or create primitive buffers. Build the primitive meshes
    // under backend init while command/resource services are explicitly borrowed.
    if ( m_state.sphereInstMesh == 0 )
    {
        BuildSphereMesh( context, 25, 25 );
    }
    if ( m_state.lowPolySphereInstMesh == 0 )
    {
        BuildLowPolySphereMesh( context, 12, 7 );
    }
    if ( m_state.boxInstMesh == 0 )
    {
        BuildBoxMesh( context );
    }
    if ( m_state.pineInstMesh == 0 )
    {
        BuildPineMesh( context );
    }
    EnsureShadowDepthShader( context );
}


void RenderHelper::BuildSphereMesh( const RenderHelperContext& context, int slices, int stacks )
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

    m_state.sphereVertexCount = PrimitiveMeshes::SphereTriangleVertexCount( slices, stacks );

    // Static layout: 3 attributes (pos3, normal3, uv2) at locations 0-2
    int staticAttribSizes[] = { 3, 3, 2 };
    // Instance layout: model matrix plus three float4 material rows, starting at location 3.
    int instanceAttribSizes[] = { 4, 4, 4, 4, 4, 4, 4, 4 };
    m_state.sphereInstMesh = Resources( context ).CreateInstancedMesh( verts.data(),
                                                                       m_state.sphereVertexCount,
                                                                       8,
                                                                       MAX_GAME_MODELS,
                                                                       INSTANCE_FLOATS,
                                                                       3,
                                                                       instanceAttribSizes,
                                                                       8,
                                                                       staticAttribSizes,
                                                                       3 );

    m_state.sphereInstanceData.reserve( MAX_GAME_MODELS * INSTANCE_FLOATS );
}


void RenderHelper::BuildLowPolySphereMesh( const RenderHelperContext& context, int slices, int stacks )
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

    m_state.lowPolySphereVertexCount = PrimitiveMeshes::SphereTriangleVertexCount( slices, stacks );

    int staticAttribSizes[] = { 3, 3, 2 };
    int instanceAttribSizes[] = { 4, 4, 4, 4, 4, 4, 4, 4 };
    m_state.lowPolySphereInstMesh = Resources( context ).CreateInstancedMesh( verts.data(),
                                                                              m_state.lowPolySphereVertexCount,
                                                                              8,
                                                                              MAX_GAME_MODELS,
                                                                              INSTANCE_FLOATS,
                                                                              3,
                                                                              instanceAttribSizes,
                                                                              8,
                                                                              staticAttribSizes,
                                                                              3 );

    m_state.sphereInstanceData.reserve( MAX_GAME_MODELS * INSTANCE_FLOATS );
}


void RenderHelper::DrawSphereBatchBegin( const RenderHelperContext& context,
                                         const Matrix4& view,
                                         const Matrix4& proj,
                                         const float lightPos[4],
                                         bool isTransparent,
                                         const CinematicRenderConfig* cinematic,
                                         const ShadowFrameData* shadow,
                                         float materialAlpha )
{
    m_state.sphereBatchTransparent = isTransparent;
    m_state.sphereBatchReady = false;
    const int objectStyle = ObjectStyleForMeshSelection( cinematic );
    const bool useLowPolySphereMesh = objectStyle == 6;
    if ( useLowPolySphereMesh )
    {
        if ( m_state.lowPolySphereInstMesh == 0 )
        {
            BuildLowPolySphereMesh( context, 12, 7 );
        }
        m_state.activeSphereInstMesh = m_state.lowPolySphereInstMesh;
        m_state.activeSphereVertexCount = m_state.lowPolySphereVertexCount;
    }
    else
    {
        if ( m_state.sphereInstMesh == 0 )
        {
            BuildSphereMesh( context, 25, 25 );
        }
        m_state.activeSphereInstMesh = m_state.sphereInstMesh;
        m_state.activeSphereVertexCount = m_state.sphereVertexCount;
    }
    EnsureSphereShader( context );
    if ( !m_state.sphereShader || m_state.activeSphereInstMesh == 0 )
    {
        m_state.sphereBatchTransparent = false;
        return;
    }

    BeginPrimitiveBatchTransparency( context, isTransparent );

    // Low-poly spheres still cast real shadows onto terrain, but they do not
    // receive object shadows. The shadow map is single and terrain-sized, so
    // ball-on-ball receiver shadows alias badly across the large flat facets
    // used by the low-poly beachball style.
    const bool receiveSphereShadows = shadow && shadow->objectsReceive && !useLowPolySphereMesh;
    m_state.sphereBatchReady = BindPrimitiveBatchShader( *m_state.sphereShader,
                                                         { context,
                                                           m_state,
                                                           view,
                                                           proj,
                                                           lightPos,
                                                           m_state.clipPlane,
                                                           cinematic,
                                                           shadow,
                                                           PRIMITIVE_SHAPE_SPHERE,
                                                           receiveSphereShadows,
                                                           materialAlpha } );
    m_state.sphereInstanceData.clear();
}


void RenderHelper::DrawSphereBatchModel( const Matrix4& model, const RenderMaterial& material )
{
    AppendMaterialInstancePayload( m_state.sphereInstanceData, model, material );
}


void RenderHelper::DrawSphereBatchModel( const Matrix4& model,
                                         float tintR,
                                         float tintG,
                                         float tintB,
                                         float colorOverride )
{
    DrawSphereBatchModel( model, MakeRenderMaterialFromLegacyTint( tintR, tintG, tintB, colorOverride ) );
}


void RenderHelper::DrawSphereBatchEnd( const RenderHelperContext& context )
{
    int instanceCount = static_cast<int>( m_state.sphereInstanceData.size() ) / INSTANCE_FLOATS;
    if ( instanceCount > 0 && m_state.activeSphereInstMesh != 0 )
    {
        if ( m_state.sphereBatchReady )
        {
            Commands( context ).UploadInstanceData( m_state.activeSphereInstMesh,
                                                    m_state.sphereInstanceData.data(),
                                                    static_cast<int>( m_state.sphereInstanceData.size() ) );
            Commands( context ).DrawInstancedMesh( m_state.activeSphereInstMesh,
                                                   m_state.activeSphereVertexCount,
                                                   instanceCount );
        }
    }
    EndPrimitiveBatchTransparency( context, m_state.sphereBatchTransparent );
    m_state.sphereBatchTransparent = false;
    m_state.sphereBatchReady = false;
}


void RenderHelper::DrawShadowDepthSphereBatchBegin( const RenderHelperContext& context,
                                                    const Matrix4& view,
                                                    const Matrix4& proj,
                                                    const CinematicRenderConfig* cinematic )
{
    m_state.sphereBatchReady = false;
    // Match the visible sphere mesh selection. If a low-poly style is active,
    // the depth pass also uses the faceted mesh, which prevents a smooth sphere
    // shadow from appearing under a visibly low-poly ball.
    const bool useLowPolySphereMesh = ObjectStyleForMeshSelection( cinematic ) == 6;
    if ( useLowPolySphereMesh )
    {
        if ( m_state.lowPolySphereInstMesh == 0 )
        {
            BuildLowPolySphereMesh( context, 12, 7 );
        }
        m_state.activeSphereInstMesh = m_state.lowPolySphereInstMesh;
        m_state.activeSphereVertexCount = m_state.lowPolySphereVertexCount;
    }
    else
    {
        if ( m_state.sphereInstMesh == 0 )
        {
            BuildSphereMesh( context, 25, 25 );
        }
        m_state.activeSphereInstMesh = m_state.sphereInstMesh;
        m_state.activeSphereVertexCount = m_state.sphereVertexCount;
    }

    EnsureShadowDepthShader( context );
    if ( !m_state.shadowDepthShader || m_state.activeSphereInstMesh == 0 )
    {
        return;
    }
    m_state.shadowDepthShader->Use();
    InstancedShadowDepthConstants constants = {};
    constants.view = view;
    constants.projection = proj;
    constants.clipPlane[0] = m_state.clipPlane[0];
    constants.clipPlane[1] = m_state.clipPlane[1];
    constants.clipPlane[2] = m_state.clipPlane[2];
    constants.clipPlane[3] = m_state.clipPlane[3];
    m_state.sphereBatchReady = m_state.shadowDepthShader->SetConstantBufferBytes( &constants,
                                                                                  sizeof( constants ),
                                                                                  "InstancedShadowDepthConstants" );
    m_state.sphereInstanceData.clear();
}


void RenderHelper::DrawShadowDepthSphereBatchModel( const Matrix4& model )
{
    DrawSphereBatchModel( model, 1.0f, 1.0f, 1.0f, 0.0f );
}


void RenderHelper::DrawShadowDepthSphereBatchEnd( const RenderHelperContext& context )
{
    int instanceCount = static_cast<int>( m_state.sphereInstanceData.size() ) / INSTANCE_FLOATS;
    if ( m_state.sphereBatchReady && instanceCount > 0 && m_state.activeSphereInstMesh != 0 )
    {
        // Upload only the compact per-instance stream, then issue one instanced
        // draw for every sphere caster. This keeps the shadow pass draw-call
        // count predictable even in scenes with hundreds of balls.
        Commands( context ).UploadInstanceData( m_state.activeSphereInstMesh,
                                                m_state.sphereInstanceData.data(),
                                                static_cast<int>( m_state.sphereInstanceData.size() ) );
        Commands( context ).DrawInstancedMesh( m_state.activeSphereInstMesh,
                                               m_state.activeSphereVertexCount,
                                               instanceCount );
    }
    m_state.sphereBatchReady = false;
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


void RenderHelper::BuildBoxMesh( const RenderHelperContext& context )
{
    std::vector<float> verts;
    verts.reserve( PrimitiveMeshes::BoxTriangleVertexCount() * 8 );

    PrimitiveMeshes::EmitUnitBox(
        [&]( const PrimitiveMeshes::VertexPNUV& vertex )
        {
            verts.insert( verts.end(),
                          { vertex.x, vertex.y, vertex.z, vertex.nx, vertex.ny, vertex.nz, vertex.u, vertex.v } );
        } );

    m_state.boxVertexCount = PrimitiveMeshes::BoxTriangleVertexCount();

    int staticAttribSizes[] = { 3, 3, 2 };
    int instanceAttribSizes[] = { 4, 4, 4, 4, 4, 4, 4, 4 };
    m_state.boxInstMesh = Resources( context ).CreateInstancedMesh( verts.data(),
                                                                    m_state.boxVertexCount,
                                                                    8,
                                                                    MAX_GAME_MODELS,
                                                                    INSTANCE_FLOATS,
                                                                    3,
                                                                    instanceAttribSizes,
                                                                    8,
                                                                    staticAttribSizes,
                                                                    3 );

    m_state.boxInstanceData.reserve( MAX_GAME_MODELS * INSTANCE_FLOATS );
}


void RenderHelper::DrawBoxBatchBegin( const RenderHelperContext& context,
                                      const Matrix4& view,
                                      const Matrix4& proj,
                                      const float lightPos[4],
                                      bool isTransparent,
                                      const CinematicRenderConfig* cinematic,
                                      const ShadowFrameData* shadow,
                                      float materialAlpha )
{
    m_state.boxBatchTransparent = isTransparent;
    m_state.boxBatchReady = false;
    if ( m_state.boxInstMesh == 0 )
    {
        BuildBoxMesh( context );
    }

    // Reuse sphere shader (same vertex layout, same lighting model).
    EnsureSphereShader( context );
    if ( !m_state.sphereShader || m_state.boxInstMesh == 0 )
    {
        m_state.boxBatchTransparent = false;
        return;
    }

    BeginPrimitiveBatchTransparency( context, isTransparent );

    m_state.boxBatchReady = BindPrimitiveBatchShader( *m_state.sphereShader,
                                                      { context,
                                                        m_state,
                                                        view,
                                                        proj,
                                                        lightPos,
                                                        m_state.clipPlane,
                                                        cinematic,
                                                        shadow,
                                                        PRIMITIVE_SHAPE_MESH,
                                                        shadow ? shadow->objectsReceive : false,
                                                        materialAlpha } );
    m_state.boxInstanceData.clear();
}


void RenderHelper::DrawBoxBatchModel( const Matrix4& model, const RenderMaterial& material )
{
    AppendMaterialInstancePayload( m_state.boxInstanceData, model, material );
}


void RenderHelper::DrawBoxBatchModel( const Matrix4& model, float tintR, float tintG, float tintB, float colorOverride )
{
    DrawBoxBatchModel( model, MakeRenderMaterialFromLegacyTint( tintR, tintG, tintB, colorOverride ) );
}


void RenderHelper::DrawBoxBatchEnd( const RenderHelperContext& context )
{
    int instanceCount = static_cast<int>( m_state.boxInstanceData.size() ) / INSTANCE_FLOATS;
    if ( m_state.boxBatchReady && instanceCount > 0 )
    {
        Commands( context ).UploadInstanceData( m_state.boxInstMesh,
                                                m_state.boxInstanceData.data(),
                                                static_cast<int>( m_state.boxInstanceData.size() ) );
        Commands( context ).DrawInstancedMesh( m_state.boxInstMesh, m_state.boxVertexCount, instanceCount );
    }
    EndPrimitiveBatchTransparency( context, m_state.boxBatchTransparent );
    m_state.boxBatchTransparent = false;
    m_state.boxBatchReady = false;
}


void RenderHelper::DrawShadowDepthBoxBatchBegin( const RenderHelperContext& context,
                                                 const Matrix4& view,
                                                 const Matrix4& proj )
{
    m_state.boxBatchReady = false;
    if ( m_state.boxInstMesh == 0 )
    {
        BuildBoxMesh( context );
    }
    EnsureShadowDepthShader( context );
    if ( !m_state.shadowDepthShader || m_state.boxInstMesh == 0 )
    {
        return;
    }
    m_state.shadowDepthShader->Use();
    InstancedShadowDepthConstants constants = {};
    constants.view = view;
    constants.projection = proj;
    constants.clipPlane[0] = m_state.clipPlane[0];
    constants.clipPlane[1] = m_state.clipPlane[1];
    constants.clipPlane[2] = m_state.clipPlane[2];
    constants.clipPlane[3] = m_state.clipPlane[3];
    m_state.boxBatchReady = m_state.shadowDepthShader->SetConstantBufferBytes( &constants,
                                                                               sizeof( constants ),
                                                                               "InstancedShadowDepthConstants" );
    m_state.boxInstanceData.clear();
}


void RenderHelper::DrawShadowDepthBoxBatchModel( const Matrix4& model )
{
    DrawBoxBatchModel( model, 1.0f, 1.0f, 1.0f, 0.0f );
}


void RenderHelper::DrawShadowDepthBoxBatchEnd( const RenderHelperContext& context )
{
    int instanceCount = static_cast<int>( m_state.boxInstanceData.size() ) / INSTANCE_FLOATS;
    if ( m_state.boxBatchReady && instanceCount > 0 && m_state.boxInstMesh != 0 )
    {
        // This draw is the box-caster fix point: if a scene has boxes and shadow
        // maps are active, their depth is written here before terrain/objects
        // sample the map in the forward pass.
        Commands( context ).UploadInstanceData( m_state.boxInstMesh,
                                                m_state.boxInstanceData.data(),
                                                static_cast<int>( m_state.boxInstanceData.size() ) );
        Commands( context ).DrawInstancedMesh( m_state.boxInstMesh, m_state.boxVertexCount, instanceCount );
    }
    m_state.boxBatchReady = false;
}

void RenderHelper::DrawConvexHullModel( const RenderHelperContext& context,
                                        const ConvexHullShape& hull,
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
    BindRenderResourceFactory( context.renderResources );
    EnsureConvexHullDynamicVB( context, m_state );
    const std::array<float, INSTANCE_FLOATS> instancePayload = BuildSingleMaterialInstancePayload( model, material );
    const int vertexCount = BuildConvexHullDynamicVertices( hull, instancePayload, m_state );
    if ( m_state.convexHullDynamicVB == 0 || vertexCount <= 0 )
    {
        return;
    }

    EnsureSphereShader( context );
    BeginPrimitiveBatchTransparency( context, isTransparent );
    const bool ready = BindPrimitiveBatchShader( *m_state.sphereShader,
                                                 { context,
                                                   m_state,
                                                   view,
                                                   proj,
                                                   lightPos,
                                                   m_state.clipPlane,
                                                   cinematic,
                                                   shadow,
                                                   PRIMITIVE_SHAPE_MESH,
                                                   shadow ? shadow->objectsReceive : false,
                                                   materialAlpha } );
    if ( ready )
    {
        Commands( context ).UploadAndDrawDynamicVB( m_state.convexHullDynamicVB,
                                                    m_state.convexHullVertexData.data(),
                                                    vertexCount );
    }
    EndPrimitiveBatchTransparency( context, isTransparent );
}

void RenderHelper::DrawShadowDepthConvexHullModel( const RenderHelperContext& context,
                                                   const ConvexHullShape& hull,
                                                   const Matrix4& model,
                                                   const Matrix4& view,
                                                   const Matrix4& proj )
{
    BindRenderResourceFactory( context.renderResources );
    EnsureConvexHullDynamicVB( context, m_state );
    const std::array<float, INSTANCE_FLOATS> instancePayload = BuildSingleMatrixPayload( model );
    const int vertexCount = BuildConvexHullDynamicVertices( hull, instancePayload, m_state );
    if ( m_state.convexHullDynamicVB == 0 || vertexCount <= 0 )
    {
        return;
    }

    EnsureShadowDepthShader( context );
    if ( !m_state.shadowDepthShader )
    {
        return;
    }
    m_state.shadowDepthShader->Use();
    InstancedShadowDepthConstants constants = {};
    constants.view = view;
    constants.projection = proj;
    constants.clipPlane[0] = m_state.clipPlane[0];
    constants.clipPlane[1] = m_state.clipPlane[1];
    constants.clipPlane[2] = m_state.clipPlane[2];
    constants.clipPlane[3] = m_state.clipPlane[3];
    if ( m_state.shadowDepthShader->SetConstantBufferBytes( &constants,
                                                            sizeof( constants ),
                                                            "InstancedShadowDepthConstants" ) )
    {
        Commands( context ).UploadAndDrawDynamicVB( m_state.convexHullDynamicVB,
                                                    m_state.convexHullVertexData.data(),
                                                    vertexCount );
    }
}


void RenderHelper::BuildPineMesh( const RenderHelperContext& context )
{
    std::vector<float> verts;
    verts.reserve( PrimitiveMeshes::PineTriangleVertexCount() * 8 );

    PrimitiveMeshes::EmitUnitPinePyramid(
        [&]( const PrimitiveMeshes::VertexPNUV& vertex )
        {
            verts.insert( verts.end(),
                          { vertex.x, vertex.y, vertex.z, vertex.nx, vertex.ny, vertex.nz, vertex.u, vertex.v } );
        } );

    m_state.pineVertexCount = PrimitiveMeshes::PineTriangleVertexCount();

    int staticAttribSizes[] = { 3, 3, 2 };
    int instanceAttribSizes[] = { 4, 4, 4, 4, 4, 4, 4, 4 };
    m_state.pineInstMesh = Resources( context ).CreateInstancedMesh( verts.data(),
                                                                     m_state.pineVertexCount,
                                                                     8,
                                                                     MAX_GAME_MODELS,
                                                                     INSTANCE_FLOATS,
                                                                     3,
                                                                     instanceAttribSizes,
                                                                     8,
                                                                     staticAttribSizes,
                                                                     3 );

    m_state.pineInstanceData.reserve( MAX_GAME_MODELS * INSTANCE_FLOATS );
}


void RenderHelper::DrawPineBatchBegin( const RenderHelperContext& context,
                                       const Matrix4& view,
                                       const Matrix4& proj,
                                       const float lightPos[4],
                                       bool isTransparent,
                                       const CinematicRenderConfig* cinematic,
                                       const ShadowFrameData* shadow,
                                       float materialAlpha )
{
    m_state.pineBatchTransparent = isTransparent;
    m_state.pineBatchReady = false;
    if ( m_state.pineInstMesh == 0 )
    {
        BuildPineMesh( context );
    }

    EnsureSphereShader( context );
    if ( !m_state.sphereShader || m_state.pineInstMesh == 0 )
    {
        m_state.pineBatchTransparent = false;
        return;
    }

    BeginPrimitiveBatchTransparency( context, isTransparent );

    m_state.pineBatchReady = BindPrimitiveBatchShader( *m_state.sphereShader,
                                                       { context,
                                                         m_state,
                                                         view,
                                                         proj,
                                                         lightPos,
                                                         m_state.clipPlane,
                                                         cinematic,
                                                         shadow,
                                                         PRIMITIVE_SHAPE_MESH,
                                                         shadow ? shadow->objectsReceive : false,
                                                         materialAlpha } );
    m_state.pineInstanceData.clear();
}


void RenderHelper::DrawPineBatchModel( const Matrix4& model, const RenderMaterial& material )
{
    AppendMaterialInstancePayload( m_state.pineInstanceData, model, material );
}


void RenderHelper::DrawPineBatchModel( const Matrix4& model,
                                       float tintR,
                                       float tintG,
                                       float tintB,
                                       float colorOverride )
{
    DrawPineBatchModel( model, MakeRenderMaterialFromLegacyTint( tintR, tintG, tintB, colorOverride ) );
}


void RenderHelper::DrawPineBatchEnd( const RenderHelperContext& context )
{
    int instanceCount = static_cast<int>( m_state.pineInstanceData.size() ) / INSTANCE_FLOATS;
    if ( m_state.pineBatchReady && instanceCount > 0 )
    {
        Commands( context ).UploadInstanceData( m_state.pineInstMesh,
                                                m_state.pineInstanceData.data(),
                                                static_cast<int>( m_state.pineInstanceData.size() ) );
        Commands( context ).DrawInstancedMesh( m_state.pineInstMesh, m_state.pineVertexCount, instanceCount );
    }
    EndPrimitiveBatchTransparency( context, m_state.pineBatchTransparent );
    m_state.pineBatchTransparent = false;
    m_state.pineBatchReady = false;
}


void RenderHelper::DrawShadowDepthPineBatchBegin( const RenderHelperContext& context,
                                                  const Matrix4& view,
                                                  const Matrix4& proj )
{
    m_state.pineBatchReady = false;
    if ( m_state.pineInstMesh == 0 )
    {
        BuildPineMesh( context );
    }
    EnsureShadowDepthShader( context );
    if ( !m_state.shadowDepthShader || m_state.pineInstMesh == 0 )
    {
        return;
    }
    m_state.shadowDepthShader->Use();
    InstancedShadowDepthConstants constants = {};
    constants.view = view;
    constants.projection = proj;
    constants.clipPlane[0] = m_state.clipPlane[0];
    constants.clipPlane[1] = m_state.clipPlane[1];
    constants.clipPlane[2] = m_state.clipPlane[2];
    constants.clipPlane[3] = m_state.clipPlane[3];
    m_state.pineBatchReady = m_state.shadowDepthShader->SetConstantBufferBytes( &constants,
                                                                                sizeof( constants ),
                                                                                "InstancedShadowDepthConstants" );
    m_state.pineInstanceData.clear();
}


void RenderHelper::DrawShadowDepthPineBatchModel( const Matrix4& model )
{
    DrawPineBatchModel( model, 1.0f, 1.0f, 1.0f, 0.0f );
}


void RenderHelper::DrawShadowDepthPineBatchEnd( const RenderHelperContext& context )
{
    int instanceCount = static_cast<int>( m_state.pineInstanceData.size() ) / INSTANCE_FLOATS;
    if ( m_state.pineBatchReady && instanceCount > 0 && m_state.pineInstMesh != 0 )
    {
        Commands( context ).UploadInstanceData( m_state.pineInstMesh,
                                                m_state.pineInstanceData.data(),
                                                static_cast<int>( m_state.pineInstanceData.size() ) );
        Commands( context ).DrawInstancedMesh( m_state.pineInstMesh, m_state.pineVertexCount, instanceCount );
    }
    m_state.pineBatchReady = false;
}


void RenderHelper::StateSetup()
{
    // Initial render state is owned by the DX12 backend. This hook remains as a
    // small extension point for any helper-level setup that must happen after
    // the renderer has initialized.
}
