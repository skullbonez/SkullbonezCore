/*
File: SkullbonezSource/Rendering/PrimitiveBatchRenderer.cpp
Purpose:
  Implements primitive GPU resource creation and bounded batch submission.

Summary:
  PrimitiveBatchRenderer.cpp owns visible and shadow-depth submission for the
  built-in primitive meshes emitted by PrimitiveMeshBuilder.h.

Glossary:
  Cbuffer (Constant Buffer): Shader constant block uploaded once before a draw.
  Material table: Fixed t4 texture that stores default per-kind material response
  values for object shaders.
  Instance payload: Per-object data appended after the model matrix in an
  instanced draw stream.

Invariants:
  - C++ constant-buffer structs must match reflected HLSL cbuffer size and
    field order, or the draw that depends on them must be skipped.
  - Builder-owned mesh/shader handles are backend resources and must be released
    by the renderer destructor before backend teardown or recreation.
  - Visible opaque, visible transparent, and shadow submissions select complete
    raster buckets at the draw; batch begin/end never mutate ambient state.

Related:
  - SkullbonezSource/Rendering/PrimitiveBatchRenderer.h
  - Agentic/Reference/runtime-reference.md
  - Agentic/Reference/comment-style-guide.md
*/
#include "PrimitiveBatchRenderer.h"
#include "../Core/Config.h"
#include "../Core/SceneCapacity.h"
#include "../Assets/AssetSystem.h"
#include "../Physics/ConvexHullShape.h"
#include "../Core/Profiler.h"
#include "IRenderCommandContext.h"
#include "DX12/Dx12ResourceBuilder.h"
#include "DX12/RenderBackendDX12.h"
#include "PrimitiveMeshBuilder.h"

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstring>
#include <vector>


using namespace SkullbonezCore::Rendering;
using namespace SkullbonezCore::Rendering;
using namespace SkullbonezCore::Math::CollisionDetection;
using namespace SkullbonezCore::Math::Transformation;
using namespace SkullbonezCore::Math::Vector;


static constexpr int INSTANCE_MATRIX_FLOATS = PrimitiveBatchRendererState::INSTANCE_MATRIX_FLOATS;
static constexpr int INSTANCE_FLOATS = PrimitiveBatchRendererState::INSTANCE_FLOATS;
static constexpr int HULL_MAX_TRIANGLE_VERTICES = PrimitiveBatchRendererState::HULL_MAX_TRIANGLE_VERTICES;
static constexpr int HULL_DYNAMIC_FLOATS_PER_VERTEX = PrimitiveBatchRendererState::HULL_DYNAMIC_FLOATS_PER_VERTEX;
static constexpr int PRIMITIVE_SHAPE_MESH = 0;
static constexpr int PRIMITIVE_SHAPE_SPHERE = 1;
static constexpr int MATERIAL_TABLE_WIDTH = 16;
static constexpr int MATERIAL_TABLE_TEXTURE_SLOT = 4;

static Dx12ResourceBuilder& Resources( const PrimitiveRenderContext& context )
{
    return context.renderResources;
}

static Dx12TextureOwner& Textures( const PrimitiveRenderContext& context )
{
    return context.renderTextures;
}

static Dx12GeometryOwner& GeometryOwner( const PrimitiveRenderContext& context )
{
    return context.renderGeometry;
}

static IRenderCommandContext& Commands( const PrimitiveRenderContext& context )
{
    return context.renderCommands;
}

static const SkullbonezCore::Assets::AssetSystem& AssetRegistry( const PrimitiveRenderContext& context )
{
    return context.assets;
}

static const SkullbonezCore::Core::EngineConfig& Config( const PrimitiveRenderContext& context )
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

constexpr PassRasterStateBucket PRIMITIVE_OPAQUE_RASTER = MakePassRasterStateBucket( 0, true, true, false );
constexpr PassRasterStateBucket PRIMITIVE_TRANSPARENT_RASTER =
    MakePassRasterStateBucket( 1, true, false, true, BlendFactor::SrcAlpha, BlendFactor::OneMinusSrcAlpha );
// Shadow bias mirrors the pass-owned recipe: constant units first, then slope.
constexpr PassRasterStateBucket PRIMITIVE_SHADOW_RASTER = MakePassRasterStateBucket( 2,
                                                                                     true,
                                                                                     true,
                                                                                     false,
                                                                                     BlendFactor::One,
                                                                                     BlendFactor::Zero,
                                                                                     CullMode::Back,
                                                                                     { true, 4.0f, 2.0f } );

static const PassRasterStateBucket& PrimitiveVisibleRasterState( bool isTransparent )
{
    return isTransparent ? PRIMITIVE_TRANSPARENT_RASTER : PRIMITIVE_OPAQUE_RASTER;
}

static uint8_t MaterialByte( float value )
{
    // The material table is an 8-bit texture, so clamp and round normalized
    // material parameters at the CPU boundary before the shader samples them.
    return static_cast<uint8_t>( std::clamp( value, 0.0f, 1.0f ) * 255.0f + 0.5f );
}

static void EnsureMaterialTableTexture( const PrimitiveRenderContext& context, PrimitiveBatchRendererState& state )
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

    state.materialTableTexture = Textures( context ).CreateTexture2D( rows, MATERIAL_TABLE_WIDTH, 1, 4, false, false );
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

static void EnsureConvexHullDynamicVB( const PrimitiveRenderContext& context, PrimitiveBatchRendererState& state )
{
    if ( state.convexHullDynamicVB != 0 )
    {
        return;
    }

    int attribs[] = { 3, 3, 2, 4, 4, 4, 4, 4, 4, 4, 4 };
    state.convexHullDynamicVB = GeometryOwner( context ).CreateDynamicVB( attribs, 11, HULL_MAX_TRIANGLE_VERTICES );
}

static int BuildConvexHullDynamicVertices( const ConvexHullShape& hull,
                                           const std::array<float, INSTANCE_FLOATS>& instancePayload,
                                           PrimitiveBatchRendererState& state )
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

static void ApplySceneLightConstants( const PrimitiveRenderContext& context, PrimitiveBatchShaderConstants& constants )
{
    const SkullbonezCore::Core::OrdinaryRenderConfig& ordinary = Config( context ).ordinaryRender;
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

static void ApplySceneLightUniforms( const PrimitiveRenderContext& context, ShaderDX12& shader )
{
    const SkullbonezCore::Core::OrdinaryRenderConfig& ordinary = Config( context ).ordinaryRender;
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
                                      const PrimitiveRenderContext& context,
                                      const SkullbonezCore::Core::CinematicRenderConfig* cinematicOverride )
{
    if ( cinematicOverride )
    {
        const SkullbonezCore::Core::CinematicRenderConfig& cinematic = *cinematicOverride;
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

static int ObjectStyleForShader( const SkullbonezCore::Core::CinematicRenderConfig* cinematicOverride )
{
    // Encode render mode separately from the light vector. Negative values mean
    // "cinematic style", while ordinary batches use style 0 and may still use a
    // directional lightPosition.w of 0 for the sun/shadow-map contract.
    return cinematicOverride ? -( cinematicOverride->objectStyle + 1 ) : 0;
}

static int ObjectStyleForMeshSelection( const SkullbonezCore::Core::CinematicRenderConfig* cinematicOverride )
{
    return cinematicOverride ? cinematicOverride->objectStyle : 0;
}

static void FillShadowReceiverConstants( PrimitiveBatchShaderConstants& constants,
                                         const PrimitiveRenderContext& context,
                                         const ShadowFrameData* shadow,
                                         bool receive,
                                         bool objectReceiver )
{
    const bool enabled = shadow && shadow->valid && receive && shadow->depthTextureHandle != 0;
    Matrix4 identity;
    constants.shadowViewProj = enabled ? shadow->lightViewProjection : identity;
    const ShadowReceiverBias bias =
        enabled ? ResolveShadowReceiverBias( *shadow, objectReceiver ) : ShadowReceiverBias();
    constants.shadowParams[0] = enabled ? shadow->strength : 0.0f;
    constants.shadowParams[1] = bias.depth;
    constants.shadowParams[2] = bias.slope;
    constants.shadowParams[3] = enabled ? shadow->texelSize * shadow->softness : 0.0f;
    constants.shadowFlags[0] = enabled ? 1.0f : 0.0f;
    constants.shadowFlags[1] = receive ? 1.0f : 0.0f;
    constants.shadowFlags[2] = enabled ? static_cast<float>( shadow->pcfRadius ) : 0.0f;
    constants.shadowFlags[3] = enabled && shadow->zeroToOneDepth ? 1.0f : 0.0f;

    Commands( context ).BindTexture( enabled ? shadow->depthTextureHandle : 0, SHADOW_TEXTURE_SLOT );
}

struct PrimitiveBatchShaderParams
{
    const PrimitiveRenderContext& context;
    PrimitiveBatchRendererState& builderState;
    const Matrix4& view;
    const Matrix4& projection;
    const float* lightPosition;
    const float* clipPlane;
    const SkullbonezCore::Core::CinematicRenderConfig* cinematic;
    const ShadowFrameData* shadow;
    int primitiveShape;
    bool receiveShadows;
    float materialAlpha;
};

static bool BindPrimitiveBatchShader( ShaderDX12& shader, const PrimitiveBatchShaderParams& params )
{
    EnsureMaterialTableTexture( params.context, params.builderState );

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
    return shader.SetConstantBufferBytes( SkullbonezCore::Core::ObjectBytes( constants ),
                                          "PrimitiveBatchShaderConstants" );
}

void PrimitiveBatchRenderer::SetClipPlane( float x, float y, float z, float w )
{
    m_state.clipPlane[0] = x;
    m_state.clipPlane[1] = y;
    m_state.clipPlane[2] = z;
    m_state.clipPlane[3] = w;
}


const float* PrimitiveBatchRenderer::GetClipPlane() const
{
    return m_state.clipPlane;
}


PrimitiveBatchRenderer::PrimitiveBatchRenderer( Dx12ResourceBuilder* renderResources,
                                                Dx12TextureOwner* renderTextures,
                                                Dx12GeometryOwner* renderGeometry )
{
    m_state.renderResources = renderResources;
    m_state.renderTextures = renderTextures;
    m_state.renderGeometry = renderGeometry;
}


PrimitiveBatchRenderer::~PrimitiveBatchRenderer()
{
    ReleaseOwnedRenderResources();
}


PrimitiveBatchRenderer::PrimitiveBatchScope::PrimitiveBatchScope( PrimitiveBatchRenderer& renderer,
                                                                  const PrimitiveRenderContext& context,
                                                                  PrimitiveBatchKind kind )
    : m_renderer( &renderer ), m_context( &context ), m_kind( kind ), m_active( true )
{
}


PrimitiveBatchRenderer::PrimitiveBatchScope::PrimitiveBatchScope( PrimitiveBatchScope&& other ) noexcept
    : m_renderer( other.m_renderer ), m_context( other.m_context ), m_kind( other.m_kind ), m_active( other.m_active )
{
    other.m_active = false;
}


PrimitiveBatchRenderer::PrimitiveBatchScope&
PrimitiveBatchRenderer::PrimitiveBatchScope::operator=( PrimitiveBatchScope&& other ) noexcept
{
    if ( this != &other )
    {
        EndIfActive();
        m_renderer = other.m_renderer;
        m_context = other.m_context;
        m_kind = other.m_kind;
        m_active = other.m_active;
        other.m_active = false;
    }
    return *this;
}


PrimitiveBatchRenderer::PrimitiveBatchScope::~PrimitiveBatchScope()
{
    EndIfActive();
}


void PrimitiveBatchRenderer::PrimitiveBatchScope::DrawModel( const Matrix4& model, const RenderMaterial& material )
{
    assert( m_renderer && m_active );
    switch ( m_kind )
    {
    case PrimitiveBatchKind::Sphere:
        m_renderer->DrawSphereBatchModel( model, material );
        break;
    case PrimitiveBatchKind::Box:
        m_renderer->DrawBoxBatchModel( model, material );
        break;
    case PrimitiveBatchKind::Pine:
        m_renderer->DrawPineBatchModel( model, material );
        break;
    default:
        assert( false && "DrawModel requires a visible primitive batch scope" );
        break;
    }
}


void PrimitiveBatchRenderer::PrimitiveBatchScope::DrawShadowModel( const Matrix4& model )
{
    assert( m_renderer && m_active );
    switch ( m_kind )
    {
    case PrimitiveBatchKind::ShadowSphere:
        m_renderer->DrawShadowDepthSphereBatchModel( model );
        break;
    case PrimitiveBatchKind::ShadowBox:
        m_renderer->DrawShadowDepthBoxBatchModel( model );
        break;
    case PrimitiveBatchKind::ShadowPine:
        m_renderer->DrawShadowDepthPineBatchModel( model );
        break;
    default:
        assert( false && "DrawShadowModel requires a shadow primitive batch scope" );
        break;
    }
}


void PrimitiveBatchRenderer::PrimitiveBatchScope::EndIfActive()
{
    if ( !m_active || !m_renderer || !m_context )
    {
        return;
    }

    switch ( m_kind )
    {
    case PrimitiveBatchKind::Sphere:
        m_renderer->DrawSphereBatchEnd( *m_context );
        break;
    case PrimitiveBatchKind::Box:
        m_renderer->DrawBoxBatchEnd( *m_context );
        break;
    case PrimitiveBatchKind::Pine:
        m_renderer->DrawPineBatchEnd( *m_context );
        break;
    case PrimitiveBatchKind::ShadowSphere:
        m_renderer->DrawShadowDepthSphereBatchEnd( *m_context );
        break;
    case PrimitiveBatchKind::ShadowBox:
        m_renderer->DrawShadowDepthBoxBatchEnd( *m_context );
        break;
    case PrimitiveBatchKind::ShadowPine:
        m_renderer->DrawShadowDepthPineBatchEnd( *m_context );
        break;
    }

    m_active = false;
}


PrimitiveBatchRenderer::PrimitiveBatchScope
PrimitiveBatchRenderer::BeginSphereBatch( const PrimitiveRenderContext& context,
                                          const Matrix4& view,
                                          const Matrix4& proj,
                                          const float lightPos[4],
                                          bool isTransparent,
                                          const SkullbonezCore::Core::CinematicRenderConfig* cinematic,
                                          const ShadowFrameData* shadow,
                                          float materialAlpha )
{
    BindRenderResourceOwners( context.renderResources, context.renderTextures, context.renderGeometry );
    DrawSphereBatchBegin( context, view, proj, lightPos, isTransparent, cinematic, shadow, materialAlpha );
    return PrimitiveBatchScope( *this, context, PrimitiveBatchKind::Sphere );
}


PrimitiveBatchRenderer::PrimitiveBatchScope
PrimitiveBatchRenderer::BeginBoxBatch( const PrimitiveRenderContext& context,
                                       const Matrix4& view,
                                       const Matrix4& proj,
                                       const float lightPos[4],
                                       bool isTransparent,
                                       const SkullbonezCore::Core::CinematicRenderConfig* cinematic,
                                       const ShadowFrameData* shadow,
                                       float materialAlpha )
{
    BindRenderResourceOwners( context.renderResources, context.renderTextures, context.renderGeometry );
    DrawBoxBatchBegin( context, view, proj, lightPos, isTransparent, cinematic, shadow, materialAlpha );
    return PrimitiveBatchScope( *this, context, PrimitiveBatchKind::Box );
}


PrimitiveBatchRenderer::PrimitiveBatchScope
PrimitiveBatchRenderer::BeginPineBatch( const PrimitiveRenderContext& context,
                                        const Matrix4& view,
                                        const Matrix4& proj,
                                        const float lightPos[4],
                                        bool isTransparent,
                                        const SkullbonezCore::Core::CinematicRenderConfig* cinematic,
                                        const ShadowFrameData* shadow,
                                        float materialAlpha )
{
    BindRenderResourceOwners( context.renderResources, context.renderTextures, context.renderGeometry );
    DrawPineBatchBegin( context, view, proj, lightPos, isTransparent, cinematic, shadow, materialAlpha );
    return PrimitiveBatchScope( *this, context, PrimitiveBatchKind::Pine );
}


PrimitiveBatchRenderer::PrimitiveBatchScope
PrimitiveBatchRenderer::BeginShadowDepthSphereBatch( const PrimitiveRenderContext& context,
                                                     const Matrix4& view,
                                                     const Matrix4& proj,
                                                     const SkullbonezCore::Core::CinematicRenderConfig* cinematic )
{
    BindRenderResourceOwners( context.renderResources, context.renderTextures, context.renderGeometry );
    DrawShadowDepthSphereBatchBegin( context, view, proj, cinematic );
    return PrimitiveBatchScope( *this, context, PrimitiveBatchKind::ShadowSphere );
}


PrimitiveBatchRenderer::PrimitiveBatchScope
PrimitiveBatchRenderer::BeginShadowDepthBoxBatch( const PrimitiveRenderContext& context,
                                                  const Matrix4& view,
                                                  const Matrix4& proj )
{
    BindRenderResourceOwners( context.renderResources, context.renderTextures, context.renderGeometry );
    DrawShadowDepthBoxBatchBegin( context, view, proj );
    return PrimitiveBatchScope( *this, context, PrimitiveBatchKind::ShadowBox );
}


PrimitiveBatchRenderer::PrimitiveBatchScope
PrimitiveBatchRenderer::BeginShadowDepthPineBatch( const PrimitiveRenderContext& context,
                                                   const Matrix4& view,
                                                   const Matrix4& proj )
{
    BindRenderResourceOwners( context.renderResources, context.renderTextures, context.renderGeometry );
    DrawShadowDepthPineBatchBegin( context, view, proj );
    return PrimitiveBatchScope( *this, context, PrimitiveBatchKind::ShadowPine );
}


void PrimitiveBatchRenderer::BindRenderResourceOwners( Dx12ResourceBuilder& renderResources,
                                                       Dx12TextureOwner& renderTextures,
                                                       Dx12GeometryOwner& renderGeometry )
{
    assert( !m_state.renderResources || m_state.renderResources == &renderResources );
    assert( !m_state.renderTextures || m_state.renderTextures == &renderTextures );
    assert( !m_state.renderGeometry || m_state.renderGeometry == &renderGeometry );
    m_state.renderResources = &renderResources;
    m_state.renderTextures = &renderTextures;
    m_state.renderGeometry = &renderGeometry;
}


void PrimitiveBatchRenderer::ReleaseOwnedRenderResources()
{
    Dx12TextureOwner* renderTextures = m_state.renderTextures;
    Dx12GeometryOwner* renderGeometry = m_state.renderGeometry;
    m_state.sphereShader.reset();
    m_state.shadowDepthShader.reset();
    if ( m_state.sphereInstMesh != 0 )
    {
        if ( renderGeometry )
        {
            renderGeometry->DestroyInstancedMesh( m_state.sphereInstMesh );
        }
        m_state.sphereInstMesh = 0;
    }
    if ( m_state.lowPolySphereInstMesh != 0 )
    {
        if ( renderGeometry )
        {
            renderGeometry->DestroyInstancedMesh( m_state.lowPolySphereInstMesh );
        }
        m_state.lowPolySphereInstMesh = 0;
    }
    if ( m_state.boxInstMesh != 0 )
    {
        if ( renderGeometry )
        {
            renderGeometry->DestroyInstancedMesh( m_state.boxInstMesh );
        }
        m_state.boxInstMesh = 0;
    }
    if ( m_state.pineInstMesh != 0 )
    {
        if ( renderGeometry )
        {
            renderGeometry->DestroyInstancedMesh( m_state.pineInstMesh );
        }
        m_state.pineInstMesh = 0;
    }
    if ( m_state.materialTableTexture != 0 )
    {
        if ( renderTextures )
        {
            renderTextures->DeleteTexture( m_state.materialTableTexture );
        }
        m_state.materialTableTexture = 0;
    }
    if ( m_state.convexHullDynamicVB != 0 )
    {
        if ( renderGeometry )
        {
            renderGeometry->DestroyDynamicVB( m_state.convexHullDynamicVB );
        }
        m_state.convexHullDynamicVB = 0;
    }
    m_state.activeSphereInstMesh = 0;
    m_state.activeSphereVertexCount = 0;
    m_state.renderResources = nullptr;
    m_state.renderTextures = nullptr;
    m_state.renderGeometry = nullptr;
}


void PrimitiveBatchRenderer::EnsureSphereShader( const PrimitiveRenderContext& context )
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


void PrimitiveBatchRenderer::EnsureShadowDepthShader( const PrimitiveRenderContext& context )
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


void PrimitiveBatchRenderer::EnsureSphereMesh( const PrimitiveRenderContext& context )
{
    BindRenderResourceOwners( context.renderResources, context.renderTextures, context.renderGeometry );
    if ( m_state.sphereInstMesh == 0 )
    {
        BuildSphereMesh( context, 25, 25 );
    }
    EnsureSphereShader( context );
}


void PrimitiveBatchRenderer::EnsureShadowDepthPrimitiveResources( const PrimitiveRenderContext& context )
{
    BindRenderResourceOwners( context.renderResources, context.renderTextures, context.renderGeometry );
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


void PrimitiveBatchRenderer::BuildSphereMesh( const PrimitiveRenderContext& context, int slices, int stacks )
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
    m_state.sphereInstMesh =
        GeometryOwner( context ).CreateInstancedMesh( verts.data(),
                                                      m_state.sphereVertexCount,
                                                      8,
                                                      SkullbonezCore::Scene::Capacity::MAX_SCENE_OBJECTS,
                                                      INSTANCE_FLOATS,
                                                      3,
                                                      instanceAttribSizes,
                                                      8,
                                                      staticAttribSizes,
                                                      3 );

    m_state.sphereInstanceData.reserve( SkullbonezCore::Scene::Capacity::MAX_SCENE_OBJECTS * INSTANCE_FLOATS );
}


void PrimitiveBatchRenderer::BuildLowPolySphereMesh( const PrimitiveRenderContext& context, int slices, int stacks )
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
    m_state.lowPolySphereInstMesh =
        GeometryOwner( context ).CreateInstancedMesh( verts.data(),
                                                      m_state.lowPolySphereVertexCount,
                                                      8,
                                                      SkullbonezCore::Scene::Capacity::MAX_SCENE_OBJECTS,
                                                      INSTANCE_FLOATS,
                                                      3,
                                                      instanceAttribSizes,
                                                      8,
                                                      staticAttribSizes,
                                                      3 );

    m_state.sphereInstanceData.reserve( SkullbonezCore::Scene::Capacity::MAX_SCENE_OBJECTS * INSTANCE_FLOATS );
}


void PrimitiveBatchRenderer::DrawSphereBatchBegin( const PrimitiveRenderContext& context,
                                                   const Matrix4& view,
                                                   const Matrix4& proj,
                                                   const float lightPos[4],
                                                   bool isTransparent,
                                                   const SkullbonezCore::Core::CinematicRenderConfig* cinematic,
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


void PrimitiveBatchRenderer::DrawSphereBatchModel( const Matrix4& model, const RenderMaterial& material )
{
    AppendMaterialInstancePayload( m_state.sphereInstanceData, model, material );
}


void PrimitiveBatchRenderer::DrawSphereBatchEnd( const PrimitiveRenderContext& context )
{
    int instanceCount = static_cast<int>( m_state.sphereInstanceData.size() ) / INSTANCE_FLOATS;
    if ( instanceCount > 0 && m_state.activeSphereInstMesh != 0 )
    {
        if ( m_state.sphereBatchReady )
        {
            Commands( context ).UploadInstanceData( m_state.activeSphereInstMesh, m_state.sphereInstanceData );
            Commands( context ).DrawInstancedMesh( { m_state.activeSphereInstMesh,
                                                     m_state.activeSphereVertexCount,
                                                     instanceCount,
                                                     PrimitiveVisibleRasterState( m_state.sphereBatchTransparent ) } );
        }
    }
    m_state.sphereBatchTransparent = false;
    m_state.sphereBatchReady = false;
}


void PrimitiveBatchRenderer::DrawShadowDepthSphereBatchBegin(
    const PrimitiveRenderContext& context,
    const Matrix4& view,
    const Matrix4& proj,
    const SkullbonezCore::Core::CinematicRenderConfig* cinematic )
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
    m_state.sphereBatchReady =
        m_state.shadowDepthShader->SetConstantBufferBytes( SkullbonezCore::Core::ObjectBytes( constants ),
                                                           "InstancedShadowDepthConstants" );
    m_state.sphereInstanceData.clear();
}


void PrimitiveBatchRenderer::DrawShadowDepthSphereBatchModel( const Matrix4& model )
{
    DrawSphereBatchModel( model, MakeRenderMaterialFromLegacyTint( 1.0f, 1.0f, 1.0f, 0.0f ) );
}


void PrimitiveBatchRenderer::DrawShadowDepthSphereBatchEnd( const PrimitiveRenderContext& context )
{
    int instanceCount = static_cast<int>( m_state.sphereInstanceData.size() ) / INSTANCE_FLOATS;
    if ( m_state.sphereBatchReady && instanceCount > 0 && m_state.activeSphereInstMesh != 0 )
    {
        // Upload only the compact per-instance stream, then issue one instanced
        // draw for every sphere caster. This keeps the shadow pass draw-call
        // count predictable even in scenes with hundreds of balls.
        Commands( context ).UploadInstanceData( m_state.activeSphereInstMesh, m_state.sphereInstanceData );
        Commands( context ).DrawInstancedMesh(
            { m_state.activeSphereInstMesh, m_state.activeSphereVertexCount, instanceCount, PRIMITIVE_SHADOW_RASTER } );
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


void PrimitiveBatchRenderer::BuildBoxMesh( const PrimitiveRenderContext& context )
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
    m_state.boxInstMesh =
        GeometryOwner( context ).CreateInstancedMesh( verts.data(),
                                                      m_state.boxVertexCount,
                                                      8,
                                                      SkullbonezCore::Scene::Capacity::MAX_SCENE_OBJECTS,
                                                      INSTANCE_FLOATS,
                                                      3,
                                                      instanceAttribSizes,
                                                      8,
                                                      staticAttribSizes,
                                                      3 );

    m_state.boxInstanceData.reserve( SkullbonezCore::Scene::Capacity::MAX_SCENE_OBJECTS * INSTANCE_FLOATS );
}


void PrimitiveBatchRenderer::DrawBoxBatchBegin( const PrimitiveRenderContext& context,
                                                const Matrix4& view,
                                                const Matrix4& proj,
                                                const float lightPos[4],
                                                bool isTransparent,
                                                const SkullbonezCore::Core::CinematicRenderConfig* cinematic,
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


void PrimitiveBatchRenderer::DrawBoxBatchModel( const Matrix4& model, const RenderMaterial& material )
{
    AppendMaterialInstancePayload( m_state.boxInstanceData, model, material );
}


void PrimitiveBatchRenderer::DrawBoxBatchEnd( const PrimitiveRenderContext& context )
{
    int instanceCount = static_cast<int>( m_state.boxInstanceData.size() ) / INSTANCE_FLOATS;
    if ( m_state.boxBatchReady && instanceCount > 0 )
    {
        Commands( context ).UploadInstanceData( m_state.boxInstMesh, m_state.boxInstanceData );
        Commands( context ).DrawInstancedMesh( { m_state.boxInstMesh,
                                                 m_state.boxVertexCount,
                                                 instanceCount,
                                                 PrimitiveVisibleRasterState( m_state.boxBatchTransparent ) } );
    }
    m_state.boxBatchTransparent = false;
    m_state.boxBatchReady = false;
}


void PrimitiveBatchRenderer::DrawShadowDepthBoxBatchBegin( const PrimitiveRenderContext& context,
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
    m_state.boxBatchReady =
        m_state.shadowDepthShader->SetConstantBufferBytes( SkullbonezCore::Core::ObjectBytes( constants ),
                                                           "InstancedShadowDepthConstants" );
    m_state.boxInstanceData.clear();
}


void PrimitiveBatchRenderer::DrawShadowDepthBoxBatchModel( const Matrix4& model )
{
    DrawBoxBatchModel( model, MakeRenderMaterialFromLegacyTint( 1.0f, 1.0f, 1.0f, 0.0f ) );
}


void PrimitiveBatchRenderer::DrawShadowDepthBoxBatchEnd( const PrimitiveRenderContext& context )
{
    int instanceCount = static_cast<int>( m_state.boxInstanceData.size() ) / INSTANCE_FLOATS;
    if ( m_state.boxBatchReady && instanceCount > 0 && m_state.boxInstMesh != 0 )
    {
        // This draw is the box-caster fix point: if a scene has boxes and shadow
        // maps are active, their depth is written here before terrain/objects
        // sample the map in the forward pass.
        Commands( context ).UploadInstanceData( m_state.boxInstMesh, m_state.boxInstanceData );
        Commands( context ).DrawInstancedMesh(
            { m_state.boxInstMesh, m_state.boxVertexCount, instanceCount, PRIMITIVE_SHADOW_RASTER } );
    }
    m_state.boxBatchReady = false;
}

void PrimitiveBatchRenderer::DrawConvexHullModel( const PrimitiveRenderContext& context,
                                                  const ConvexHullShape& hull,
                                                  const Matrix4& model,
                                                  const RenderMaterial& material,
                                                  const Matrix4& view,
                                                  const Matrix4& proj,
                                                  const float lightPos[4],
                                                  bool isTransparent,
                                                  const SkullbonezCore::Core::CinematicRenderConfig* cinematic,
                                                  const ShadowFrameData* shadow,
                                                  float materialAlpha )
{
    BindRenderResourceOwners( context.renderResources, context.renderTextures, context.renderGeometry );
    EnsureConvexHullDynamicVB( context, m_state );
    const std::array<float, INSTANCE_FLOATS> instancePayload = BuildSingleMaterialInstancePayload( model, material );
    const int vertexCount = BuildConvexHullDynamicVertices( hull, instancePayload, m_state );
    if ( m_state.convexHullDynamicVB == 0 || vertexCount <= 0 )
    {
        return;
    }

    EnsureSphereShader( context );
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
        Commands( context ).UploadAndDrawDynamicVB(
            m_state.convexHullDynamicVB,
            std::span<const float>( m_state.convexHullVertexData.data(),
                                    static_cast<size_t>( vertexCount ) * HULL_DYNAMIC_FLOATS_PER_VERTEX ),
            PrimitiveVisibleRasterState( isTransparent ) );
    }
}

void PrimitiveBatchRenderer::DrawShadowDepthConvexHullModel( const PrimitiveRenderContext& context,
                                                             const ConvexHullShape& hull,
                                                             const Matrix4& model,
                                                             const Matrix4& view,
                                                             const Matrix4& proj )
{
    BindRenderResourceOwners( context.renderResources, context.renderTextures, context.renderGeometry );
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
    if ( m_state.shadowDepthShader->SetConstantBufferBytes( SkullbonezCore::Core::ObjectBytes( constants ),
                                                            "InstancedShadowDepthConstants" ) )
    {
        Commands( context ).UploadAndDrawDynamicVB(
            m_state.convexHullDynamicVB,
            std::span<const float>( m_state.convexHullVertexData.data(),
                                    static_cast<size_t>( vertexCount ) * HULL_DYNAMIC_FLOATS_PER_VERTEX ),
            PRIMITIVE_SHADOW_RASTER );
    }
}


void PrimitiveBatchRenderer::BuildPineMesh( const PrimitiveRenderContext& context )
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
    m_state.pineInstMesh =
        GeometryOwner( context ).CreateInstancedMesh( verts.data(),
                                                      m_state.pineVertexCount,
                                                      8,
                                                      SkullbonezCore::Scene::Capacity::MAX_SCENE_OBJECTS,
                                                      INSTANCE_FLOATS,
                                                      3,
                                                      instanceAttribSizes,
                                                      8,
                                                      staticAttribSizes,
                                                      3 );

    m_state.pineInstanceData.reserve( SkullbonezCore::Scene::Capacity::MAX_SCENE_OBJECTS * INSTANCE_FLOATS );
}


void PrimitiveBatchRenderer::DrawPineBatchBegin( const PrimitiveRenderContext& context,
                                                 const Matrix4& view,
                                                 const Matrix4& proj,
                                                 const float lightPos[4],
                                                 bool isTransparent,
                                                 const SkullbonezCore::Core::CinematicRenderConfig* cinematic,
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


void PrimitiveBatchRenderer::DrawPineBatchModel( const Matrix4& model, const RenderMaterial& material )
{
    AppendMaterialInstancePayload( m_state.pineInstanceData, model, material );
}


void PrimitiveBatchRenderer::DrawPineBatchEnd( const PrimitiveRenderContext& context )
{
    int instanceCount = static_cast<int>( m_state.pineInstanceData.size() ) / INSTANCE_FLOATS;
    if ( m_state.pineBatchReady && instanceCount > 0 )
    {
        Commands( context ).UploadInstanceData( m_state.pineInstMesh, m_state.pineInstanceData );
        Commands( context ).DrawInstancedMesh( { m_state.pineInstMesh,
                                                 m_state.pineVertexCount,
                                                 instanceCount,
                                                 PrimitiveVisibleRasterState( m_state.pineBatchTransparent ) } );
    }
    m_state.pineBatchTransparent = false;
    m_state.pineBatchReady = false;
}


void PrimitiveBatchRenderer::DrawShadowDepthPineBatchBegin( const PrimitiveRenderContext& context,
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
    m_state.pineBatchReady =
        m_state.shadowDepthShader->SetConstantBufferBytes( SkullbonezCore::Core::ObjectBytes( constants ),
                                                           "InstancedShadowDepthConstants" );
    m_state.pineInstanceData.clear();
}


void PrimitiveBatchRenderer::DrawShadowDepthPineBatchModel( const Matrix4& model )
{
    DrawPineBatchModel( model, MakeRenderMaterialFromLegacyTint( 1.0f, 1.0f, 1.0f, 0.0f ) );
}


void PrimitiveBatchRenderer::DrawShadowDepthPineBatchEnd( const PrimitiveRenderContext& context )
{
    int instanceCount = static_cast<int>( m_state.pineInstanceData.size() ) / INSTANCE_FLOATS;
    if ( m_state.pineBatchReady && instanceCount > 0 && m_state.pineInstMesh != 0 )
    {
        Commands( context ).UploadInstanceData( m_state.pineInstMesh, m_state.pineInstanceData );
        Commands( context ).DrawInstancedMesh(
            { m_state.pineInstMesh, m_state.pineVertexCount, instanceCount, PRIMITIVE_SHADOW_RASTER } );
    }
    m_state.pineBatchReady = false;
}
