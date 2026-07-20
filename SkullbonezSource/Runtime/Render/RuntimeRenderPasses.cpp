/*
File: SkullbonezSource/Runtime/Render/RuntimeRenderPasses.cpp
Purpose:
  Implements the named world-render pass classes owned by RuntimeRenderer.

Summary:
  RuntimeRenderer.cpp should read like a frame story: build one frame
  context, run named passes, then hand each pass output to its downstream
  consumer. This file owns the rendering guts and GPU lifetime hooks for those
  passes so pass contracts are visible where the work happens.

Glossary:
  DXR (DirectX Raytracing): DX12 API used for hardware ray traversal and
  reflection dispatch.
  BLAS (Bottom-Level Acceleration Structure): Raytracing spatial index for one
  mesh's triangles.
  TLAS (Top-Level Acceleration Structure): Raytracing spatial index for scene
  instances that point at BLAS geometry.
  Render pass: A named slice of frame rendering with explicit inputs, outputs,
  and GPU resource ownership.
  GPU resource: Backend-owned texture, framebuffer, shader, descriptor, or
  dynamic vertex buffer that must be released before backend teardown.
  HDR (High Dynamic Range): Floating-point scene color that can hold values
  brighter than display white until tonemapping resolves it.
  FBO (Framebuffer Object): Engine shorthand for an off-screen render target
  exposed through the renderer abstraction.
  SRV (Shader Resource View): Descriptor row used when shaders read textures.

Invariants:
  - EnsureGpuResources may lazily create or resize backend resources, but must
    not change pass order or draw output.
  - ReleaseGpuResources must be safe during backend teardown and must leave
    resource structs in a null/zero state for the next ensure.
  - Render methods consume only the current frame's input structs; they must not
    cache borrowed pointers from those structs.

Related:
  - SkullbonezSource/Runtime/Render/RuntimeRenderPasses.h declares pass contracts.
  - SkullbonezSource/Runtime/Render/RuntimeRenderer.cpp owns frame orchestration.
  - Agentic/Reference/comment-style-guide.md
*/
#include "RuntimeRenderPasses.h"
#include "../../Assets/AssetKeys.h"
#include "RuntimeRenderResources.h"
#include "../Debug/CollisionVisualizer.h"
#include "../Debug/PhysicsDebugVisualizer.h"
#include "../Debug/BroadphaseVisualizer.h"
#include "../Replay/ReplayVisualPacket.h"
#include "../Replay/ReplayPrediction.h"
#include "../Tools/RuntimeTools.h"
#include "../Scene/SceneTerrain.h"
#include "../OperatorCommandApplier.h"
#include "../../Assets/TextureCollection.h"
#include "../../Core/PlatformProfiler.h"
#include "../../Core/Profiler.h"
#include "../../Core/Log.h"
#include "../../Rendering/IRenderDiagnostics.h"
#include "../../Rendering/IRenderDeviceLifecycle.h"
#include "../../Rendering/IRenderRayTracing.h"
#include "../../Rendering/IRenderResourceFactory.h"
#include "../../Rendering/RenderInstanceRenderer.h"
#include "../../Rendering/PrimitiveBatchRenderer.h"
#include "../../Rendering/RenderGraph.h"
#include "../../Rendering/RenderInstanceStore.h"
#include "../../Rendering/RenderRasterBindingContract.h"
#include "../../World/Terrain.h"
#include "../../World/SkyBox.h"
#include "../../World/WorldEnvironment.h"

#include <cstdio>

using namespace SkullbonezCore::Runtime;
using SkullbonezCore::Rendering::PrimitiveBatchRenderer;
using SkullbonezCore::Rendering::PrimitiveRenderContext;
using namespace SkullbonezCore::Math::CollisionDetection;
using namespace SkullbonezCore::Math::Transformation;
using namespace SkullbonezCore::Physics;
using namespace SkullbonezCore::Runtime::RunInternal;
namespace Textures = SkullbonezCore::Textures;
using SkullbonezCore::Geometry::XZBounds;
using SkullbonezCore::Math::Vector::Vector3;

namespace
{
constexpr int RENDER_TEXTURE_SLOT_COUNT = SkullbonezCore::Rendering::TEXTURE_SLOT_COUNT;
constexpr unsigned int RENDER_TEXTURE_SLOT_0 = 1u << 0;
constexpr unsigned int RENDER_TEXTURE_SLOT_1 = 1u << 1;
constexpr unsigned int RENDER_TEXTURE_SLOT_2 = 1u << 2;
constexpr unsigned int RENDER_TEXTURE_SLOT_3 = 1u << 3;
constexpr unsigned int RENDER_TEXTURE_SLOT_5 = 1u << 5;
constexpr std::size_t TORNADO_VISUAL_FLOATS_PER_VERTEX = 11u;
constexpr float TORNADO_FX_KIND_RIBBON = 0.0f;
constexpr float TORNADO_FX_KIND_DUST = 1.0f;

void ClearRenderTextureSlotsExcept( SkullbonezCore::Rendering::IRenderCommandContext& renderCommands,
                                    unsigned int keptSlots )
{
    for ( int slot = 0; slot < RENDER_TEXTURE_SLOT_COUNT; ++slot )
    {
        if ( ( keptSlots & ( 1u << slot ) ) == 0u )
        {
            renderCommands.BindTexture( 0, slot );
        }
    }
}

void ClearAllRenderTextureSlots( SkullbonezCore::Rendering::IRenderCommandContext& renderCommands )
{
    ClearRenderTextureSlotsExcept( renderCommands, 0u );
}

int CopyDxrRenderInstanceMatrices( const SkullbonezCore::Rendering::RenderInstanceStore& renderStore,
                                   float* outMatrixFloats,
                                   int maxModelCount )
{
    if ( !outMatrixFloats || maxModelCount <= 0 )
    {
        return 0;
    }

    const auto instances = renderStore.Records();
    const int modelCount = (std::min)( static_cast<int>( instances.size() ), maxModelCount );
    for ( int i = 0; i < modelCount; ++i )
    {
        const Matrix4& modelMatrix = instances[static_cast<std::size_t>( i )].modelMatrix;
        memcpy( outMatrixFloats + static_cast<std::size_t>( i ) * 16u, modelMatrix.Data(), 16u * sizeof( float ) );
    }
    return modelCount;
}

bool HasCollisionVisualizerFrameView( const RenderFrameContext& frame )
{
    return frame.bodyStore && frame.colliders && frame.renderInstances && frame.collisionVisualContacts;
}

CollisionVisualizerFrameView BuildCollisionVisualizerFrameView( const RenderFrameContext& frame )
{
    return CollisionVisualizerFrameView{ *frame.bodyStore,
                                         *frame.colliders,
                                         *frame.renderInstances,
                                         *frame.collisionVisualContacts,
                                         frame.sleepStates,
                                         frame.sleepIslandVisualIds,
                                         frame.modelCount };
}

bool HasPhysicsDebugFrameView( const RenderFrameContext& frame )
{
    return frame.bodyStore && frame.colliders && frame.physicsDebugContacts && frame.physicsPipelineTrace;
}

PhysicsDebugFrameView BuildPhysicsDebugFrameView( const RenderFrameContext& frame )
{
    return PhysicsDebugFrameView{ *frame.bodyStore,
                                  *frame.colliders,
                                  frame.sleepStates,
                                  frame.sleepSupportedStates,
                                  frame.sleepInhibitedStates,
                                  *frame.physicsDebugContacts,
                                  *frame.physicsPipelineTrace,
                                  frame.modelCount };
}

void BindRenderTextureSlots( SkullbonezCore::Rendering::IRenderCommandContext& renderCommands,
                             uint32_t slot0,
                             uint32_t slot1,
                             uint32_t slot2,
                             uint32_t slot3,
                             uint32_t slot4 = 0,
                             uint32_t slot5 = 0 )
{
    // Contract: ordinary raster shaders expose t0..t5. Slot t4 is reserved for
    // the object material table, but pass hygiene still clears it to the typed
    // null SRV; object batches bind the material table again immediately before
    // drawing. Terrain alone uses t5 for the tight object-shadow map.
    const uint32_t handles[RENDER_TEXTURE_SLOT_COUNT] = { slot0, slot1, slot2, slot3, slot4, slot5 };
    for ( int slot = 0; slot < RENDER_TEXTURE_SLOT_COUNT; ++slot )
    {
        renderCommands.BindTexture( handles[slot], slot );
    }
}

SkullbonezCore::Rendering::IRenderCommandContext& RenderCommands( const RenderFrameContext& frame )
{
    assert( frame.renderCommands && "RenderFrameContext requires a render command context" );
    return *frame.renderCommands;
}

SkullbonezCore::Rendering::IRenderResourceFactory& RenderResources( const RenderResourceContext& resources )
{
    return resources.renderResources;
}

SkullbonezCore::Assets::AssetSystem& RenderAssets( const RenderFrameContext& frame )
{
    assert( frame.assets && "RenderFrameContext requires an asset registry" );
    return *frame.assets;
}

SkullbonezCore::Textures::TextureCollection& RenderTextures( const RenderFrameContext& frame )
{
    assert( frame.textures && "RenderFrameContext requires a texture collection" );
    return *frame.textures;
}

bool ReportRenderTextureResult( const char* passName, const SkullbonezCore::Core::SbResult& result )
{
    if ( result.ok )
    {
        return true;
    }

    // Why: render passes are void frame steps, so recoverable texture failures
    // surface at the pass boundary and the affected draw is skipped.
    std::fprintf( stderr,
                  "%s texture failure [%s]: %s\n",
                  passName ? passName : "Frame/Render",
                  result.error.owner,
                  result.error.message );
    return false;
}

bool SelectRenderTexture( const RenderFrameContext& frame, uint32_t hash, const char* passName )
{
    return ReportRenderTextureResult( passName, RenderTextures( frame ).SelectTexture( hash ) );
}

bool ResolveRenderTextureHandle( Textures::TextureCollection& textures,
                                 uint32_t hash,
                                 const char* passName,
                                 uint32_t& outHandle )
{
    const Textures::TextureCollection::TextureHandleResult result = textures.GetTextureHandle( hash );
    if ( !ReportRenderTextureResult( passName, result.result ) )
    {
        return false;
    }
    outHandle = result.handle;
    return true;
}

SkullbonezCore::Rendering::IRenderResourceFactory& RenderResources( const RenderFrameContext& frame )
{
    assert( frame.renderResources && "RenderFrameContext requires a render resource factory" );
    return *frame.renderResources;
}

PrimitiveRenderContext PrimitiveRenderContextForFrame( const RenderFrameContext& frame,
                                                       const SkullbonezCore::Core::EngineConfig& config )
{
    assert( frame.renderDiagnostics && "RenderFrameContext requires a render diagnostics context" );
    assert( frame.primitiveBatches && "RenderFrameContext requires a primitive batch renderer" );
    return PrimitiveRenderContext{ RenderResources( frame ),
                                   RenderCommands( frame ),
                                   *frame.renderDiagnostics,
                                   RenderAssets( frame ),
                                   config,
                                   *frame.primitiveBatches };
}

PrimitiveBatchRenderer& PrimitiveBatchRendererForFrame( const RenderFrameContext& frame )
{
    assert( frame.primitiveBatches && "RenderFrameContext requires a primitive batch renderer" );
    return *frame.primitiveBatches;
}

SkullbonezCore::Rendering::IRenderDiagnostics& RenderDiagnostics( const RenderFrameContext& frame )
{
    assert( frame.renderDiagnostics && "RenderFrameContext requires a render diagnostics context" );
    return *frame.renderDiagnostics;
}

float Clamp01( float value )
{
    return std::clamp( value, 0.0f, 1.0f );
}

float HashUnitFloat( uint32_t value )
{
    value ^= value >> 16;
    value *= 0x7feb352du;
    value ^= value >> 15;
    value *= 0x846ca68bu;
    value ^= value >> 16;
    return static_cast<float>( value & 0x00ffffffu ) / static_cast<float>( 0x01000000u );
}

Vector3 NormalizeOr( Vector3 value, const Vector3& fallback )
{
    if ( VectorMagSquared( value ) <= 1.0e-8f )
    {
        return fallback;
    }
    value.Normalise();
    return value;
}

Vector3 CylindricalOffset( float radius, float angle )
{
    return Vector3( cosf( angle ) * radius, 0.0f, sinf( angle ) * radius );
}

void EmitFxVertex( std::vector<float>& vertices,
                   const Vector3& position,
                   float r,
                   float g,
                   float b,
                   float a,
                   float u,
                   float v,
                   float fxKind,
                   float terrainY )
{
    vertices.push_back( position.x );
    vertices.push_back( position.y );
    vertices.push_back( position.z );
    vertices.push_back( r );
    vertices.push_back( g );
    vertices.push_back( b );
    vertices.push_back( a );
    vertices.push_back( u );
    vertices.push_back( v );
    vertices.push_back( fxKind );
    vertices.push_back( terrainY );
}

void EmitFxQuad( std::vector<float>& vertices,
                 const Vector3& a,
                 const Vector3& b,
                 const Vector3& c,
                 const Vector3& d,
                 float r,
                 float g,
                 float blue,
                 float alpha,
                 float fxKind,
                 float terrainA,
                 float terrainB,
                 float terrainC,
                 float terrainD )
{
    EmitFxVertex( vertices, a, r, g, blue, alpha, 0.0f, 0.0f, fxKind, terrainA );
    EmitFxVertex( vertices, b, r, g, blue, alpha, 1.0f, 0.0f, fxKind, terrainB );
    EmitFxVertex( vertices, c, r, g, blue, alpha, 1.0f, 1.0f, fxKind, terrainC );
    EmitFxVertex( vertices, a, r, g, blue, alpha, 0.0f, 0.0f, fxKind, terrainA );
    EmitFxVertex( vertices, c, r, g, blue, alpha, 1.0f, 1.0f, fxKind, terrainC );
    EmitFxVertex( vertices, d, r, g, blue, alpha, 0.0f, 1.0f, fxKind, terrainD );
}

Vector3 NormalizeShadowLightDirection( Vector3 lightDirectionWorld )
{
    // Why: scene/config data can omit or zero the sun vector. Shadows still need
    // a normalized direction so matrix construction cannot divide by a zero
    // length vector.
    if ( VectorMag( lightDirectionWorld ) < 1.0e-5f )
    {
        lightDirectionWorld = Vector3( -0.68f, 0.22f, -0.70f );
    }
    lightDirectionWorld.Normalise();
    return lightDirectionWorld;
}


struct ScreenSunPosition
{
    float x = -10.0f;
    float y = -10.0f;
};


ScreenSunPosition ProjectCinematicSunToScreen( const Vector3& eye,
                                               const Matrix4& viewProjection,
                                               const SkullbonezCore::Core::CinematicRenderConfig& cinematic )
{
    const Vector3 sunPoint = eye + CinematicSkySunDirection( cinematic ) * 1000.0f;
    const Matrix4& vp = viewProjection;
    const float clipX = vp.m[0] * sunPoint.x + vp.m[4] * sunPoint.y + vp.m[8] * sunPoint.z + vp.m[12];
    const float clipY = vp.m[1] * sunPoint.x + vp.m[5] * sunPoint.y + vp.m[9] * sunPoint.z + vp.m[13];
    const float clipW = vp.m[3] * sunPoint.x + vp.m[7] * sunPoint.y + vp.m[11] * sunPoint.z + vp.m[15];
    if ( clipW <= 0.0001f )
    {
        return {};
    }

    const float invW = 1.0f / clipW;
    return { clipX * invW * 0.5f + 0.5f, clipY * invW * 0.5f + 0.5f };
}


constexpr float FULLSCREEN_QUAD_VERTS[] = {
    -1.0f, -1.0f, 0.0f, 0.0f, 1.0f, -1.0f, 1.0f, 0.0f, 1.0f,  1.0f, 1.0f, 1.0f,
    -1.0f, -1.0f, 0.0f, 0.0f, 1.0f, 1.0f,  1.0f, 1.0f, -1.0f, 1.0f, 0.0f, 1.0f,
};

void DrawFullscreenQuad( SkullbonezCore::Rendering::IRenderCommandContext& renderCommands, uint32_t quadVB )
{
    // Shared post vertex contract: clip-space xy followed by UV. Keeping one
    // copy prevents sky, volumetric, and tonemap from quietly drifting apart.
    renderCommands.UploadAndDrawDynamicVB( quadVB, FULLSCREEN_QUAD_VERTS, 6 );
}

void BindSkyPassParams( SkullbonezCore::Rendering::IShader& shader,
                        const Matrix4& view,
                        const Matrix4& projection,
                        const SkullbonezCore::Core::CinematicRenderConfig& cinematic )
{
    shader.SetVec4( "uSunParams",
                    cinematic.sunAzimuth,
                    cinematic.sunElevation,
                    cinematic.sunIntensity,
                    cinematic.skyGlowStrength );
    shader.SetVec3( "uSunColor", cinematic.sunColorR, cinematic.sunColorG, cinematic.sunColorB );
    shader.SetVec3( "uHorizonColor", cinematic.skyHorizonR, cinematic.skyHorizonG, cinematic.skyHorizonB );
    shader.SetVec3( "uZenithColor", cinematic.skyZenithR, cinematic.skyZenithG, cinematic.skyZenithB );
    shader.SetMat4( "uInvView", view.Inverse() );
    shader.SetMat4( "uInvProjection", projection.Inverse() );
    shader.SetInt( "uSkyMode", cinematic.skyMode );
    shader.SetVec4( "uCloudParams",
                    cinematic.cloudCoverage,
                    cinematic.cloudSoftness,
                    cinematic.cloudScale,
                    cinematic.cloudsEnabled ? cinematic.cloudIntensity : 0.0f );
}

void BindVolumetricPassParams( SkullbonezCore::Rendering::IShader& shader,
                               const Vector3& eye,
                               const Matrix4& viewProjection,
                               const SkullbonezCore::Core::CinematicRenderConfig& cinematic,
                               float frustumNear,
                               float frustumFar )
{
    const ScreenSunPosition sunScreen = ProjectCinematicSunToScreen( eye, viewProjection, cinematic );
    shader.SetInt( "uSceneTex", 0 );
    shader.SetInt( "uDepthTex", 1 );
    shader.SetVec4( "uDepthParams", frustumNear, frustumFar, 0.0f, 0.0f );
    shader.SetVec4( "uSunShaftParams",
                    sunScreen.x,
                    sunScreen.y,
                    cinematic.godRaysEnabled ? cinematic.sunShaftStrength : 0.0f,
                    cinematic.sunShaftFalloff );
    shader.SetVec3( "uSunColor", cinematic.sunColorR, cinematic.sunColorG, cinematic.sunColorB );
    shader.SetVec4( "uVolumetricParams",
                    cinematic.volumetricStrength,
                    cinematic.volumetricDensity,
                    cinematic.volumetricDecay,
                    cinematic.fogDensity );
}

void BindTonemapPassParams( SkullbonezCore::Rendering::IShader& shader,
                            const SkullbonezCore::Core::CinematicRenderConfig& cinematic,
                            float frustumNear,
                            float frustumFar,
                            int sceneWidth,
                            int sceneHeight,
                            bool volumetricReady )
{
    shader.SetInt( "uSceneTex", 0 );
    shader.SetInt( "uDepthTex", 1 );
    shader.SetInt( "uVolumetricTex", 2 );
    shader.SetFloat( "uExposure", cinematic.exposure );
    shader.SetFloat( "uGamma", cinematic.gamma );
    shader.SetVec4( "uDepthParams", frustumNear, frustumFar, 0.0f, 0.0f );
    shader.SetVec4( "uFogParams",
                    cinematic.fogStart,
                    cinematic.fogEnd,
                    cinematic.fogEnabled ? cinematic.fogDensity : 0.0f,
                    cinematic.fogEnabled ? cinematic.fogMaxOpacity : 0.0f );
    shader.SetVec3( "uFogColor", cinematic.fogColorR, cinematic.fogColorG, cinematic.fogColorB );
    // Invariant: these are the actual HDR scene-target dimensions, not assumed
    // window dimensions. Supplying their inverse here keeps the per-pixel shader
    // free of GetDimensions queries while resize updates the bloom footprint.
    const float inverseSceneWidth = 1.0f / static_cast<float>( sceneWidth > 0 ? sceneWidth : 1 );
    const float inverseSceneHeight = 1.0f / static_cast<float>( sceneHeight > 0 ? sceneHeight : 1 );
    shader.SetVec4( "uBloomTexelSize", inverseSceneWidth, inverseSceneHeight, 0.0f, 0.0f );
    shader.SetVec4( "uBloomParams",
                    cinematic.bloomThreshold,
                    cinematic.bloomKnee,
                    cinematic.bloomEnabled ? cinematic.bloomStrength : 0.0f,
                    cinematic.bloomRadius );
    shader.SetVec4( "uStyleGrade",
                    cinematic.styleSaturation,
                    cinematic.styleContrast,
                    cinematic.styleVignette,
                    static_cast<float>( cinematic.skyMode ) );
    shader.SetFloat( "uVolumetricCompositeStrength",
                     volumetricReady && cinematic.volumetricLightingEnabled ? 1.0f : 0.0f );
}

} // namespace

void RenderResourceLifecycleLog::Write( const char* phase, const char* step ) const
{
    const bool backendReady = m_deviceLifecycle != nullptr;
    const int backendWidth = m_deviceLifecycle ? m_deviceLifecycle->GetWidth() : 0;
    const int backendHeight = m_deviceLifecycle ? m_deviceLifecycle->GetHeight() : 0;
    SkullbonezCore::Core::Log().WriteEventf(
        "render_resource_lifecycle phase=%s step=%s gfx_ready=%d backend_width=%d backend_height=%d "
        "scene_index=%d load=%d",
        phase ? phase : "unknown",
        step ? step : "unknown",
        backendReady ? 1 : 0,
        backendWidth,
        backendHeight,
        m_scene.currentSceneIndex,
        m_scene.loadCount );
}


void FullscreenQuadPass::EnsureGpuResources( const RenderResourceContext& resources )
{
    if ( !resources.cinematicEnabled )
    {
        return;
    }

    if ( m_resources.quadVB == 0 )
    {
        // Full-screen shaders draw one rectangle; each vertex stores screen xy
        // plus uv, and every pass gives that same geometry its own shader meaning.
        const int attribs[] = { 2, 2 };
        m_resources.quadVB = RenderResources( resources ).CreateDynamicVB( attribs, 2, 6 );
    }
}


void FullscreenQuadPass::ReleaseGpuResources( Rendering::IRenderResourceFactory* renderResources )
{
    if ( renderResources && m_resources.quadVB != 0 )
    {
        renderResources->DestroyDynamicVB( m_resources.quadVB );
    }
    m_resources.quadVB = 0;
}


uint32_t FullscreenQuadPass::QuadVB() const
{
    return m_resources.quadVB;
}


void SkyPass::EnsureGpuResources( const RenderResourceContext& resources )
{
    if ( !resources.cinematicEnabled )
    {
        return;
    }

    if ( !m_skyResources.atmosphereShader )
    {
        // Procedural sky shader: draws generated sunset/cloud color when the
        // cinematic config opts out of the authored cube-map skybox.
        m_skyResources.atmosphereShader =
            resources.assets.CreateShader( RenderResources( resources ), "shader.sky_atmosphere" );
    }
}


void SkyPass::ReleaseGpuResources()
{
    m_skyResources.atmosphereShader.reset();
}


void SceneTargetPass::EnsureGpuResources( const RenderResourceContext& resources )
{
    if ( !resources.cinematicEnabled )
    {
        return;
    }

    const int w = resources.windowWidth;
    const int h = resources.windowHeight;
    const bool needsSceneTarget =
        !m_resources.hdrTarget || m_resources.hdrTarget->GetWidth() != w || m_resources.hdrTarget->GetHeight() != h ||
        m_resources.hdrTarget->GetColorFormat() != SkullbonezCore::Rendering::FramebufferColorFormat::RGBA16F;
    if ( needsSceneTarget )
    {
        // RGBA16F preserves bright sky/fog values until TonemapPass compresses
        // them back to display color on the window backbuffer.
        if ( m_resources.hdrTarget )
        {
            m_resources.hdrTarget->ResetResources();
        }
        m_resources.hdrTarget.reset();
        m_resources.hdrTarget =
            RenderResources( resources )
                .CreateFramebuffer( w, h, SkullbonezCore::Rendering::FramebufferColorFormat::RGBA16F );
    }
}


void SceneTargetPass::ReleaseGpuResources()
{
    if ( m_resources.hdrTarget )
    {
        m_resources.hdrTarget->ResetResources();
    }
    m_resources.hdrTarget.reset();
}


bool SceneTargetPass::IsReady() const
{
    return m_resources.hdrTarget != nullptr;
}


void ReflectionPass::EnsureGpuResources( const RenderResourceContext& resources )
{
    // Why: the reflection texture is intentionally supersampled relative to the
    // window. Water can then sample it at grazing angles without making the
    // mirrored scene look blocky.
    const int fboW = resources.windowWidth * 2;
    const int fboH = resources.windowHeight * 2;
    const bool needsReflectionTarget =
        !m_resources.target || m_resources.target->GetWidth() != fboW || m_resources.target->GetHeight() != fboH ||
        m_resources.target->GetColorFormat() != SkullbonezCore::Rendering::FramebufferColorFormat::RGBA8;

    if ( needsReflectionTarget )
    {
        LogResourceLifecycleStep( "window_resize", "reflection_target_recreate_if_needed" );
        if ( m_resources.target )
        {
            m_resources.target->ResetResources();
        }
        m_resources.target.reset();
        m_resources.target = RenderResources( resources ).CreateFramebuffer( fboW, fboH );
    }
}


void ReflectionPass::ReleaseGpuResources()
{
    LogResourceLifecycleStep( "reflection_reset", "reflection_target" );
    // Lifetime: ResetResources gives the backend a chance to release device
    // objects before the unique_ptr destructor drops the renderer-neutral shell.
    if ( m_resources.target )
    {
        m_resources.target->ResetResources();
    }
    m_resources.target.reset();
}


void ReflectionPass::LogResourceLifecycleStep( const char* phase, const char* step ) const
{
    m_lifecycleLog.Write( phase, step );
}


void ShadowPass::EnsureGpuResources( const RenderResourceContext& resources,
                                     const SkullbonezCore::Core::CinematicRenderConfig& cinematic )
{
    if ( !cinematic.shadow.enabled )
    {
        return;
    }
    PROFILE_SCOPED( m_profiler, "Frame/Shadows/ShadowMap/EnsureResources" );

    // Concept: the shadow map is a renderer-neutral depth framebuffer. It is
    // intentionally owned outside the cinematic HDR target because the same
    // light-space depth texture is useful in normal backbuffer rendering,
    // cinematic rendering, and screenshot/perf scenes. The cinematic config
    // still supplies map size and bias/softness values, but the feature itself
    // is no longer gated by the cinematic post-processing path.
    const int mapSize = std::clamp( cinematic.shadow.mapSize, 256, 8192 );
    auto ensureTarget = [&]( std::unique_ptr<Rendering::IFramebuffer>& target )
    {
        const bool needsTarget = !target || target->GetWidth() != mapSize || target->GetHeight() != mapSize;
        if ( needsTarget )
        {
            target.reset();
            target = RenderResources( resources ).CreateFramebuffer( mapSize, mapSize );
        }
    };
    ensureTarget( m_resources.terrainTarget );
    ensureTarget( m_resources.objectTarget );
}


void ShadowPass::LogResourceLifecycleStep( const char* phase, const char* step ) const
{
    m_lifecycleLog.Write( phase, step );
}


void ShadowPass::ReleaseGpuResources()
{
    // Lifetime: drop both the backing framebuffer and the per-frame payload.
    // Framebuffer handles are owned by the current device/backend, so any
    // device reset, resize rebuild, or future backend bring-up must force a
    // clean recreate before the next shadow pass. The payload is reset too so
    // receivers cannot accidentally sample an old depth texture after the
    // resource dies.
    enum class ShadowResetStep
    {
        TerrainShadowFBO,
        ObjectShadowFBO,
        FramePayloads
    };

    struct ShadowResetPhase
    {
        const char* name;
        ShadowResetStep step;
    };

    const ShadowResetPhase resetSteps[] = {
        { "terrain_shadow_target", ShadowResetStep::TerrainShadowFBO },
        { "object_shadow_target", ShadowResetStep::ObjectShadowFBO },
        { "shadow_frame_payloads", ShadowResetStep::FramePayloads },
    };

    for ( const ShadowResetPhase& phase : resetSteps )
    {
        LogResourceLifecycleStep( "shadow_reset", phase.name );
        switch ( phase.step )
        {
        case ShadowResetStep::TerrainShadowFBO:
            if ( m_resources.terrainTarget )
            {
                m_resources.terrainTarget->ResetResources();
            }
            m_resources.terrainTarget.reset();
            break;
        case ShadowResetStep::ObjectShadowFBO:
            if ( m_resources.objectTarget )
            {
                m_resources.objectTarget->ResetResources();
            }
            m_resources.objectTarget.reset();
            break;
        case ShadowResetStep::FramePayloads:
            m_resources.terrainFrame = Rendering::ShadowFrameData();
            m_resources.objectFrame = Rendering::ShadowFrameData();
            m_resources.objectCasterBatches.Clear();
            break;
        }
    }
}


SkullbonezCore::Rendering::ShadowFrameData
ShadowPass::BuildTerrainFrameData( const SkullbonezCore::Core::CinematicRenderConfig& cinematic,
                                   const Math::Vector::Vector3& lightDirectionWorld ) const
{
    PROFILE_SCOPED( m_profiler, "Frame/Shadows/ShadowMap/BuildTerrainFrame" );

    Rendering::ShadowFrameData shadowFrame;
    if ( !m_terrain.Get() || !m_resources.terrainTarget )
    {
        return shadowFrame;
    }

    // Shadow maps need a stable light-space camera. `lightDirectionWorld` is
    // treated as the vector from the scene toward the light source. The visible
    // ordinary and cinematic shaders use the same directional-sun contract, so
    // shadow visibility blocks the direct light the BRDF is actually shading.
    Vector3 lightDir = NormalizeShadowLightDirection( lightDirectionWorld );

    const XZBounds terrainBounds = m_terrain.Get()->GetXZBounds();
    const float extentX = (std::max)( terrainBounds.m_xMax - terrainBounds.m_xMin, 1.0f );
    const float extentZ = (std::max)( terrainBounds.m_zMax - terrainBounds.m_zMin, 1.0f );
    const float terrainHeightRange =
        (std::max)( m_terrain.Get()->GetMaxHeight() - m_terrain.Get()->GetMinHeight(), 64.0f );
    const float terrainRadius = (std::max)( extentX, extentZ ) * 0.5f;
    const float shadowRadius =
        std::clamp( terrainRadius + 180.0f, 128.0f, (std::max)( cinematic.shadow.maxDistance, 128.0f ) );

    // Center the orthographic projection over the whole terrain instead of the
    // camera. This is a simple single-map v1: it avoids camera-dependent popping
    // and makes screenshots deterministic, at the cost of spreading resolution
    // across the authored terrain bounds instead of using cascades.
    const Vector3 focus( ( terrainBounds.m_xMin + terrainBounds.m_xMax ) * 0.5f,
                         ( m_terrain.Get()->GetMinHeight() + m_terrain.Get()->GetMaxHeight() ) * 0.5f,
                         ( terrainBounds.m_zMin + terrainBounds.m_zMax ) * 0.5f );
    const float lightBackDistance = shadowRadius + terrainHeightRange + 650.0f;
    const Vector3 lightEye = focus + lightDir * lightBackDistance;
    const Vector3 lightUp = fabsf( lightDir.y ) > 0.92f ? Vector3( 0.0f, 0.0f, 1.0f ) : Vector3( 0.0f, 1.0f, 0.0f );
    const float nearPlane = 1.0f;
    const float farPlane = lightBackDistance * 2.0f + terrainHeightRange + shadowRadius;

    shadowFrame.lightView = Matrix4::LookAt( lightEye, focus, lightUp );
    shadowFrame.lightProjection =
        Matrix4::OrthoZeroToOne( -shadowRadius, shadowRadius, -shadowRadius, shadowRadius, nearPlane, farPlane );
    shadowFrame.mapSize = m_resources.terrainTarget->GetWidth();
    Rendering::SnapShadowProjectionToTexelGrid( shadowFrame.lightProjection,
                                                shadowFrame.lightView,
                                                shadowFrame.mapSize );
    shadowFrame.lightViewProjection = shadowFrame.lightProjection * shadowFrame.lightView;
    shadowFrame.lightDirectionWorld = lightDir;
    shadowFrame.depthTextureHandle = m_resources.terrainTarget->GetDepthTextureHandle();

    // Everything below is copied into shader uniforms by ApplyShadowReceiverUniforms.
    // Keeping the values in one payload makes balls, boxes, terrain, and any
    // future backend consume the same shadow decision for the frame.
    shadowFrame.pcfRadius = std::clamp( cinematic.shadow.pcfRadius, 0, 3 );
    shadowFrame.strength = std::clamp( cinematic.shadow.strength, 0.0f, 1.0f );
    shadowFrame.depthBias = (std::max)( cinematic.shadow.depthBias, 0.0f );
    shadowFrame.slopeBias = (std::max)( cinematic.shadow.slopeBias, 0.0f );
    shadowFrame.texelSize = shadowFrame.mapSize > 0 ? 1.0f / static_cast<float>( shadowFrame.mapSize ) : 0.0f;
    shadowFrame.softness = (std::max)( cinematic.shadow.softness, 0.25f );
    shadowFrame.zeroToOneDepth = true;
    shadowFrame.terrainReceives = cinematic.shadow.terrainReceives;
    shadowFrame.objectsReceive = cinematic.shadow.objectsReceive;
    shadowFrame.valid = shadowFrame.depthTextureHandle != 0 && shadowFrame.mapSize > 0;
    return shadowFrame;
}


SkullbonezCore::Rendering::ShadowFrameData
ShadowPass::BuildObjectFrameData( const SkullbonezCore::Core::CinematicRenderConfig& cinematic,
                                  const Math::Vector::Vector3& lightDirectionWorld,
                                  const Math::Vector::Vector3& focusHint,
                                  const Rendering::RenderInstanceStore& renderInstances,
                                  SkullbonezCore::Threading::WorkerPool* renderWorkerPool,
                                  bool shadowParallelPrep )
{
    PROFILE_SCOPED( m_profiler, "Frame/Shadows/ShadowMap/BuildObjectFrame" );

    Rendering::ShadowFrameData shadowFrame;
    if ( !m_resources.objectTarget || !cinematic.shadow.objectsCast || !cinematic.shadow.objectsReceive )
    {
        return shadowFrame;
    }

    Vector3 focus;
    float shadowRadius = 0.0f;
    float heightRange = 0.0f;
    const float objectSearchDistance = std::clamp( cinematic.shadow.maxDistance * 0.15f, 180.0f, 320.0f );
    if ( !Rendering::RenderInstanceRenderer::GetObjectShadowBounds( m_profiler,
                                                                    renderInstances,
                                                                    renderWorkerPool,
                                                                    shadowParallelPrep,
                                                                    focusHint,
                                                                    objectSearchDistance,
                                                                    focus,
                                                                    shadowRadius,
                                                                    heightRange ) )
    {
        return shadowFrame;
    }

    Vector3 lightDir = NormalizeShadowLightDirection( lightDirectionWorld );
    const float lightBackDistance = shadowRadius + heightRange + 220.0f;
    const Vector3 lightEye = focus + lightDir * lightBackDistance;
    const Vector3 lightUp = fabsf( lightDir.y ) > 0.92f ? Vector3( 0.0f, 0.0f, 1.0f ) : Vector3( 0.0f, 1.0f, 0.0f );
    const float nearPlane = 1.0f;
    const float farPlane = lightBackDistance * 2.0f + heightRange + shadowRadius;

    shadowFrame.lightView = Matrix4::LookAt( lightEye, focus, lightUp );
    shadowFrame.lightProjection =
        Matrix4::OrthoZeroToOne( -shadowRadius, shadowRadius, -shadowRadius, shadowRadius, nearPlane, farPlane );
    shadowFrame.mapSize = m_resources.objectTarget->GetWidth();
    Rendering::SnapShadowProjectionToTexelGrid( shadowFrame.lightProjection,
                                                shadowFrame.lightView,
                                                shadowFrame.mapSize );
    shadowFrame.lightViewProjection = shadowFrame.lightProjection * shadowFrame.lightView;
    shadowFrame.lightDirectionWorld = lightDir;
    shadowFrame.depthTextureHandle = m_resources.objectTarget->GetDepthTextureHandle();
    shadowFrame.pcfRadius = std::clamp( cinematic.shadow.pcfRadius, 0, 3 );
    shadowFrame.strength = std::clamp( cinematic.shadow.strength, 0.0f, 1.0f );
    shadowFrame.depthBias = (std::max)( cinematic.shadow.depthBias, 0.0f );
    shadowFrame.slopeBias = (std::max)( cinematic.shadow.slopeBias, 0.0f );
    shadowFrame.texelSize = shadowFrame.mapSize > 0 ? 1.0f / static_cast<float>( shadowFrame.mapSize ) : 0.0f;
    shadowFrame.softness = (std::max)( cinematic.shadow.softness, 0.25f );
    shadowFrame.zeroToOneDepth = true;
    shadowFrame.terrainReceives = false;
    shadowFrame.objectsReceive = cinematic.shadow.objectsReceive;
    shadowFrame.valid = shadowFrame.depthTextureHandle != 0 && shadowFrame.mapSize > 0;
    return shadowFrame;
}


void ShadowPass::RenderShadowMap( Rendering::IFramebuffer& target,
                                  const PrimitiveRenderContext& primitiveContext,
                                  const Rendering::ShadowFrameData& shadowFrame,
                                  const SkullbonezCore::Core::CinematicRenderConfig& cinematic,
                                  Rendering::IRenderCommandContext& renderCommands,
                                  bool renderTerrain,
                                  bool renderObjects,
                                  const Rendering::RenderInstanceStore& renderInstances,
                                  const Physics::ColliderStore& colliders,
                                  SkullbonezCore::Threading::WorkerPool* renderWorkerPool,
                                  bool shadowParallelPrep,
                                  const Rendering::ShadowCasterBatches* objectCasters )
{
    PROFILE_SCOPED( m_profiler, "Frame/Shadows/ShadowMap/RenderMap" );
    DRAW_CALL_TRACE_SCOPE( primitiveContext.renderDiagnostics, "Frame/Shadows/ShadowMap/RenderMap" );

    if ( !shadowFrame.valid )
    {
        return;
    }
    if ( ( !renderTerrain || !cinematic.shadow.terrainCasts ) && ( !renderObjects || !cinematic.shadow.objectsCast ) )
    {
        return;
    }

    // Render from the light's point of view into the depth attachment. The pass
    // does not need color output; the framebuffer abstraction still gives us a
    // color target on some backends, but receivers sample only the depth texture
    // handle stored in ShadowFrameData.
    target.Bind();
    renderCommands.SetViewport( 0, 0, target.GetWidth(), target.GetHeight() );
    renderCommands.Clear( true, true );

    // Shadow depth writes must be opaque and depth-only. Save the caller's
    // blend/depth state because this pass runs in the middle of RenderFrame()
    // before reflection, world rendering, water, UI, and debug overlays.
    const bool depthWasEnabled = renderCommands.IsDepthTestEnabled();
    const bool blendWasEnabled = renderCommands.IsBlendEnabled();
    renderCommands.SetDepthTest( true );
    renderCommands.SetDepthWrite( true );
    renderCommands.SetBlend( false );
    renderCommands.SetCullFace( true );

    // Polygon offset reduces self-shadow acne on terrain and object faces. The
    // shader-side receiver bias handles comparison precision; this rasterizer
    // bias handles the depth values written into the map.
    renderCommands.SetPolygonOffset( true, 2.0f, 4.0f );
    // Pass contract: shadow depth shaders write depth only and sample no
    // textures. Clear inherited slots so descriptor state from the visible
    // scene cannot leak into this off-screen pass.
    ClearAllRenderTextureSlots( renderCommands );

    if ( renderTerrain && cinematic.shadow.terrainCasts && !m_activeTerrainHidden && m_terrain.Get() )
    {
        PROFILE_SCOPED( m_profiler, "Frame/Shadows/ShadowMap/RenderMap/TerrainCasters" );
        DRAW_CALL_TRACE_SCOPE( primitiveContext.renderDiagnostics, "Frame/Shadows/ShadowMap/RenderMap/TerrainCasters" );

        // Terrain must cast with the same optional render-only relief that the
        // visible terrain uses. Otherwise cinematic basin relief would receive
        // shadows from the flat CPU height map and the contact would visibly
        // detach. With normal rendering the relief amount is zero by default.
        m_terrain.Get()->RenderShadowDepth( m_profiler,
                                            shadowFrame.lightView,
                                            shadowFrame.lightProjection,
                                            &cinematic );
    }

    if ( renderObjects && cinematic.shadow.objectsCast && !m_activeCollisionVisualizerVisible )
    {
        PROFILE_SCOPED( m_profiler, "Frame/Shadows/ShadowMap/RenderMap/ObjectCasters" );
        DRAW_CALL_TRACE_SCOPE( primitiveContext.renderDiagnostics, "Frame/Shadows/ShadowMap/RenderMap/ObjectCasters" );

        // Balls, boxes, and pine-style box visuals all write depth here. The
        // prepared render store keeps separate instanced batches so each caster
        // shape uses the same mesh silhouette as the visible forward pass.
        // Why: both passes submit the same prepared caster shape, so the map
        // selection at this orchestration boundary preserves per-view evidence.
        const Rendering::RenderVisibilityView visibilityView = renderTerrain
                                                                   ? Rendering::RenderVisibilityView::TerrainShadow
                                                                   : Rendering::RenderVisibilityView::ObjectShadow;
        if ( objectCasters )
        {
            Rendering::RenderInstanceRenderer::SubmitShadowCasterBatches( m_profiler,
                                                                          primitiveContext,
                                                                          *objectCasters,
                                                                          shadowFrame.lightView,
                                                                          shadowFrame.lightProjection,
                                                                          &cinematic,
                                                                          visibilityView );
        }
        else
        {
            Rendering::RenderInstanceRenderer::RenderShadowCasters( m_profiler,
                                                                    primitiveContext,
                                                                    renderInstances,
                                                                    colliders,
                                                                    renderWorkerPool,
                                                                    shadowParallelPrep,
                                                                    shadowFrame.lightView,
                                                                    shadowFrame.lightProjection,
                                                                    &cinematic,
                                                                    visibilityView );
        }
    }

    renderCommands.SetPolygonOffset( false );
    renderCommands.SetDepthTest( depthWasEnabled );
    renderCommands.SetDepthWrite( depthWasEnabled );
    renderCommands.SetBlend( blendWasEnabled );
    target.Unbind();
    renderCommands.SetViewport( 0, 0, m_activeWindowWidth, m_activeWindowHeight );
}


ShadowPassOutput ShadowPass::Render( const ShadowPassInputs& inputs )
{
    // Invariant: always clear the receiver payloads at the start of the pass.
    // If shadows are disabled, downstream terrain/object passes must see null
    // outputs instead of last frame's depth texture handles.
    m_resources.terrainFrame = Rendering::ShadowFrameData();
    m_resources.objectFrame = Rendering::ShadowFrameData();
    m_resources.objectCasterBatches.Clear();
    if ( inputs.cinematic )
    {
        if ( !inputs.frame.renderInstances || !inputs.frame.colliders )
        {
            return ShadowPassOutput();
        }

        // Build shadow maps before any receiver pass. Terrain receives the broad
        // map, while objects receive a second tight map centered on nearby bodies
        // so ball-on-ball shadows have enough texel density.
        PROFILE_SCOPED( m_profiler, "Frame/Shadows" );
        DRAW_CALL_TRACE_SCOPE( RenderDiagnostics( inputs.frame ), "Frame/Shadows" );
        PROFILE_GPU_BEGIN( m_profiler, "Frame/Shadows/ShadowMap" );
        {
            DRAW_CALL_TRACE_SCOPE( RenderDiagnostics( inputs.frame ), "Frame/Shadows/ShadowMap" );
            Vector3 lightDirection( inputs.frame.lightPosition[0],
                                    inputs.frame.lightPosition[1],
                                    inputs.frame.lightPosition[2] );
            Rendering::ShadowCasterBatches& objectCasters = m_resources.objectCasterBatches;
            const bool shouldBuildObjectCasters =
                inputs.cinematic->shadow.objectsCast && !inputs.collisionVisualizerVisible;
            if ( shouldBuildObjectCasters )
            {
                Rendering::RenderInstanceRenderer::BuildShadowCasterBatches( m_profiler,
                                                                             *inputs.frame.renderInstances,
                                                                             *inputs.frame.colliders,
                                                                             inputs.frame.renderWorkerPool,
                                                                             inputs.frame.shadowParallelPrep,
                                                                             objectCasters );
            }
            m_activeTerrainHidden = inputs.terrainHidden;
            m_activeCollisionVisualizerVisible = inputs.collisionVisualizerVisible;
            m_activeWindowWidth = inputs.frame.windowWidth;
            m_activeWindowHeight = inputs.frame.windowHeight;
            m_resources.terrainFrame = BuildTerrainFrameData( *inputs.cinematic, lightDirection );
            if ( m_resources.terrainTarget )
            {
                RenderShadowMap( *m_resources.terrainTarget,
                                 PrimitiveRenderContextForFrame( inputs.frame, m_config ),
                                 m_resources.terrainFrame,
                                 *inputs.cinematic,
                                 RenderCommands( inputs.frame ),
                                 true,
                                 true,
                                 *inputs.frame.renderInstances,
                                 *inputs.frame.colliders,
                                 inputs.frame.renderWorkerPool,
                                 inputs.frame.shadowParallelPrep,
                                 &objectCasters );
            }
            // Anchor the tight object-shadow map to the render look target, not
            // the eye. Locked/inspect zoom moves the eye around a stable target;
            // using the eye makes nearby-object bounds pop as the user zooms.
            m_resources.objectFrame = BuildObjectFrameData( *inputs.cinematic,
                                                            lightDirection,
                                                            inputs.frame.viewCenter,
                                                            *inputs.frame.renderInstances,
                                                            inputs.frame.renderWorkerPool,
                                                            inputs.frame.shadowParallelPrep );
            if ( m_resources.objectTarget )
            {
                RenderShadowMap( *m_resources.objectTarget,
                                 PrimitiveRenderContextForFrame( inputs.frame, m_config ),
                                 m_resources.objectFrame,
                                 *inputs.cinematic,
                                 RenderCommands( inputs.frame ),
                                 false,
                                 true,
                                 *inputs.frame.renderInstances,
                                 *inputs.frame.colliders,
                                 inputs.frame.renderWorkerPool,
                                 inputs.frame.shadowParallelPrep,
                                 &objectCasters );
            }
        }
        PROFILE_GPU_END( m_profiler, "Frame/Shadows/ShadowMap" );
    }

    ShadowPassOutput output;
    output.terrainShadow = m_resources.terrainFrame.valid ? &m_resources.terrainFrame : nullptr;
    output.objectShadow = m_resources.objectFrame.valid ? &m_resources.objectFrame : output.terrainShadow;
    return output;
}


void SkyPass::RenderCinematicSky( const RenderFrameContext& frame, const Math::Transformation::Matrix4& view )
{
    assert( frame.cinematic && "Cinematic sky requires a frame cinematic snapshot" );
    // Invariant: the active cinematic choice is a frame snapshot, while the
    // generated-sky shader and fullscreen vertex buffer are pass resources.
    // This path should not reach back through Run state for either.
    const SkullbonezCore::Core::CinematicRenderConfig& cinematic = *frame.cinematic;
    if ( !cinematic.skyAtmosphereEnabled || !m_skyResources.atmosphereShader || m_fullscreenResources.quadVB == 0 )
    {
        return;
    }

    // The sky is painted as a full-screen background. It should not test against
    // terrain depth and it should not blend with whatever was previously there.
    Rendering::IRenderCommandContext& renderCommands = RenderCommands( frame );
    const bool depthWasEnabled = renderCommands.IsDepthTestEnabled();
    const bool blendWasEnabled = renderCommands.IsBlendEnabled();
    renderCommands.SetDepthTest( false );
    renderCommands.SetDepthWrite( false );
    renderCommands.SetBlend( false );

    // Pass contract: this generated sky samples no textures. Clear inherited
    // SRV slots before the fullscreen draw so stale pass inputs cannot be
    // recopied by the backend while the sky shader is active.
    ClearAllRenderTextureSlots( renderCommands );
    m_skyResources.atmosphereShader->Use();
    BindSkyPassParams( *m_skyResources.atmosphereShader, view, frame.projection, cinematic );
    DrawFullscreenQuad( renderCommands, m_fullscreenResources.quadVB );

    renderCommands.SetDepthTest( depthWasEnabled );
    renderCommands.SetDepthWrite( depthWasEnabled );
    renderCommands.SetBlend( blendWasEnabled );
}


void SkyPass::Render( const RenderFrameContext& frame, const Math::Transformation::Matrix4& view, SkyPassMode mode )
{
    const bool useCinematicAtmosphere =
        mode == SkyPassMode::CinematicIfEnabled && frame.cinematic && frame.cinematic->skyAtmosphereEnabled;
    if ( useCinematicAtmosphere )
    {
        RenderCinematicSky( frame, view );
        return;
    }

    // The cube-map sky follows camera X/Z so the box feels infinitely far away,
    // while its Y stays authored by config to preserve the long-standing horizon.
    Matrix4 skyView = view * Matrix4::Translate( frame.eye.x, m_config.skybox.renderHeight, frame.eye.z ) *
                      Matrix4::Scale( m_config.skybox.scale );
    // Pass contract: cube-map skybox faces sample only slot 0. Slots owned by
    // water, post, or shadows must not leak into these six mesh draws.
    ClearRenderTextureSlotsExcept( RenderCommands( frame ), RENDER_TEXTURE_SLOT_0 );
    assert( m_skyBox && "SkyPass requires the world-view sky owner after initialise" );
    ReportRenderTextureResult( "Frame/Render/Skybox", m_skyBox->Render( skyView, frame.projection ) );
}


void SceneTargetPass::Begin( const RenderFrameContext& frame, SkyPass& skyPass )
{
    // Invariant: from this point onward, draw the world into the HDR scene
    // target instead of directly into the window. The post pass later moves it
    // to the backbuffer with the cinematic effects applied.
    m_resources.hdrTarget->Bind();
    Rendering::IRenderCommandContext& renderCommands = RenderCommands( frame );
    renderCommands.SetViewport( 0, 0, m_resources.hdrTarget->GetWidth(), m_resources.hdrTarget->GetHeight() );
    renderCommands.Clear( true, true );

    PROFILE_GPU_BEGIN( m_profiler, "Frame/Render/CinematicSky" );
    {
        DRAW_CALL_TRACE_SCOPE( RenderDiagnostics( frame ), "Frame/Render/CinematicSky" );
        skyPass.Render( frame, frame.baseView, SkyPassMode::CinematicIfEnabled );
    }
    PROFILE_GPU_END( m_profiler, "Frame/Render/CinematicSky" );
}


ReflectionPassOutput ReflectionPass::Render( const ReflectionPassInputs& inputs, SkyPass& skyPass )
{
    ReflectionPassOutput output;

    // Concept: two implementations, one water-pass contract. The planar path
    // renders the above-water scene from a mirrored camera into an FBO. The DXR
    // path rebuilds the raytracing TLAS and writes a screen-space reflection
    // texture directly. Both feed the same water shader later.
    PROFILE_GPU_BEGIN( m_profiler, "Frame/Render/Reflection" );
    DRAW_CALL_TRACE_SCOPE( RenderDiagnostics( inputs.frame ), "Frame/Render/Reflection" );
    const auto renderCapabilities = RenderDiagnostics( inputs.frame ).GetCapabilities();
    Rendering::IRenderRayTracing* rayTracing = inputs.frame.renderRayTracing;
    const bool useDxrReflection = renderCapabilities.supportsDxrReflection && rayTracing &&
                                  inputs.waterRayTracingReflection && !inputs.waterNoReflection &&
                                  !inputs.collisionStateColorsVisible && !inputs.transparentBodyPass;
    output.usedDxr = useDxrReflection;

    if ( useDxrReflection )
    {
        // Lifetime: the DX12 backend owns the raytracing acceleration
        // structures. The prepared render store streams current per-model
        // transforms into the TLAS before one reflection ray per texture pixel.
        const int ballCount = inputs.frame.renderInstances && m_dxrReflectionTransforms
                                  ? CopyDxrRenderInstanceMatrices( *inputs.frame.renderInstances,
                                                                   m_dxrReflectionTransforms,
                                                                   m_dxrReflectionTransformCapacity )
                                  : 0;

        // Terrain/sphere BLAS objects are owned by the DX12 backend, so the
        // runtime supplies only per-instance sphere transforms here.
        rayTracing->BuildTLAS( m_dxrReflectionTransforms, ballCount, 0, 0 );

        // Ray generation reconstructs world-space rays from screen pixels, so
        // it needs the inverse of the main camera view-projection matrix.
        Matrix4 invVP = inputs.frame.viewProjection.Inverse();
        float cameraPos[3] = { inputs.frame.eye.x, inputs.frame.eye.y, inputs.frame.eye.z };
        float simTime = inputs.simulationTimeSeconds;
        // Why: nullptr preserves the backend's legacy sky colors for ordinary
        // water while cinematic scenes can make DXR misses match authored voids.
        float cinematicSkyColorTop[3] = {};
        float cinematicSkyColorBottom[3] = {};
        const float* skyColorTop = nullptr;
        const float* skyColorBottom = nullptr;
        if ( inputs.cinematic && inputs.cinematic->enabled )
        {
            cinematicSkyColorTop[0] = inputs.cinematic->skyZenithR;
            cinematicSkyColorTop[1] = inputs.cinematic->skyZenithG;
            cinematicSkyColorTop[2] = inputs.cinematic->skyZenithB;
            cinematicSkyColorBottom[0] = inputs.cinematic->skyHorizonR;
            cinematicSkyColorBottom[1] = inputs.cinematic->skyHorizonG;
            cinematicSkyColorBottom[2] = inputs.cinematic->skyHorizonB;
            skyColorTop = cinematicSkyColorTop;
            skyColorBottom = cinematicSkyColorBottom;
        }

        Textures::TextureCollection& textures = RenderTextures( inputs.frame );
        uint32_t sphereHandle = 0;
        uint32_t terrainHandle = 0;
        uint32_t skyUpHandle = 0;
        uint32_t skyDownHandle = 0;
        uint32_t skyRightHandle = 0;
        uint32_t skyLeftHandle = 0;
        uint32_t skyFrontHandle = 0;
        uint32_t skyBackHandle = 0;
        if ( !ResolveRenderTextureHandle( textures,
                                          TEXTURE_BOUNDING_SPHERE,
                                          "Frame/Render/Reflection/DXR",
                                          sphereHandle ) ||
             !ResolveRenderTextureHandle( textures, TEXTURE_GROUND, "Frame/Render/Reflection/DXR", terrainHandle ) ||
             !ResolveRenderTextureHandle( textures, TEXTURE_SKY_UP, "Frame/Render/Reflection/DXR", skyUpHandle ) ||
             !ResolveRenderTextureHandle( textures, TEXTURE_SKY_DOWN, "Frame/Render/Reflection/DXR", skyDownHandle ) ||
             !ResolveRenderTextureHandle( textures,
                                          TEXTURE_SKY_RIGHT,
                                          "Frame/Render/Reflection/DXR",
                                          skyRightHandle ) ||
             !ResolveRenderTextureHandle( textures, TEXTURE_SKY_LEFT, "Frame/Render/Reflection/DXR", skyLeftHandle ) ||
             !ResolveRenderTextureHandle( textures,
                                          TEXTURE_SKY_FRONT,
                                          "Frame/Render/Reflection/DXR",
                                          skyFrontHandle ) ||
             !ResolveRenderTextureHandle( textures, TEXTURE_SKY_BACK, "Frame/Render/Reflection/DXR", skyBackHandle ) )
        {
            PROFILE_GPU_END( m_profiler, "Frame/Render/Reflection" );
            return output;
        }
        rayTracing->DispatchReflectionRays( invVP.Data(),
                                            cameraPos,
                                            inputs.frame.waterY,
                                            simTime,
                                            inputs.frame.lightPosition,
                                            skyColorTop,
                                            skyColorBottom,
                                            inputs.frame.windowWidth * 2,
                                            inputs.frame.windowHeight * 2,
                                            sphereHandle,
                                            terrainHandle,
                                            skyUpHandle,
                                            skyDownHandle,
                                            skyRightHandle,
                                            skyLeftHandle,
                                            skyFrontHandle,
                                            skyBackHandle );
        output.reflectionTextureHandle = rayTracing->GetReflectionUAVTexture();
        output.reflectionSampleViewProjection = inputs.frame.viewProjection;
    }
    else
    {
        if ( !m_resources.target )
        {
            return output;
        }

        // Invariant: the planar path binds only its own reflection target and
        // restores the viewport to the window size before water renders.
        Rendering::IRenderCommandContext& renderCommands = RenderCommands( inputs.frame );
        m_resources.target->Bind();
        renderCommands.SetViewport( 0, 0, m_resources.target->GetWidth(), m_resources.target->GetHeight() );
        renderCommands.Clear( true, true );

        // Skybox reflected (XZ follows eye; Y anchored at runtime config).
        // Cinematic mode can reflect the generated sunset sky into the water
        // instead of the usual cube-map sky.
        PROFILE_GPU_BEGIN( m_profiler, "Frame/Render/Reflection/Skybox" );
        {
            DRAW_CALL_TRACE_SCOPE( RenderDiagnostics( inputs.frame ), "Frame/Render/Reflection/Skybox" );
            skyPass.Render( inputs.frame, inputs.frame.reflectionView, SkyPassMode::CinematicIfEnabled );
        }
        PROFILE_GPU_END( m_profiler, "Frame/Render/Reflection/Skybox" );

        // Why: clip at the water surface so the reflection texture contains only
        // the above-water portion of models. The water shader supplies the
        // below-surface visual from the main scene.
        PROFILE_GPU_BEGIN( m_profiler, "Frame/Render/Reflection/Balls" );
        DRAW_CALL_TRACE_SCOPE( RenderDiagnostics( inputs.frame ), "Frame/Render/Reflection/Balls" );
        renderCommands.SetClipPlane( 0, true );
        PrimitiveBatchRendererForFrame( inputs.frame ).SetClipPlane( 0.0f, 1.0f, 0.0f, -inputs.frame.waterY );
        m_collisionVisualizer.SetClipPlane( 0.0f, 1.0f, 0.0f, -inputs.frame.waterY );
        if ( inputs.collisionStateColorsVisible )
        {
            // Pass contract: collision-state solids are vertex-colored and do
            // not sample textures.
            ClearAllRenderTextureSlots( renderCommands );
            if ( HasCollisionVisualizerFrameView( inputs.frame ) )
            {
                const CollisionVisualizerFrameView frameView = BuildCollisionVisualizerFrameView( inputs.frame );
                m_collisionVisualizer.SetAlphaOverride( inputs.collisionVisualizerAlphaOverride );
                m_collisionVisualizer.Render( RenderAssets( inputs.frame ),
                                              RenderResources( inputs.frame ),
                                              renderCommands,
                                              RenderDiagnostics( inputs.frame ),
                                              frameView,
                                              inputs.frame.reflectionView,
                                              inputs.frame.projection,
                                              inputs.frame.lightPosition );
                m_collisionVisualizer.SetAlphaOverride( -1.0f );
            }
        }
        else
        {
            // Pass contract: reflected lit models read material color from slot
            // 0 and optional shadow depth from slot 3.
            ClearRenderTextureSlotsExcept(
                renderCommands,
                RENDER_TEXTURE_SLOT_0 |
                    ( inputs.objectShadow && inputs.objectShadow->valid ? RENDER_TEXTURE_SLOT_3 : 0u ) );
            if ( SelectRenderTexture( inputs.frame, TEXTURE_BOUNDING_SPHERE, "Frame/Render/Reflection/Balls" ) &&
                 inputs.frame.renderInstances && inputs.frame.colliders )
            {
                Rendering::RenderInstanceRenderer::RenderModels(
                    PrimitiveRenderContextForFrame( inputs.frame, m_config ),
                    *inputs.frame.renderInstances,
                    *inputs.frame.colliders,
                    inputs.frame.renderCollisionVolumes,
                    inputs.frame.reflectionView,
                    inputs.frame.projection,
                    inputs.frame.lightPosition,
                    inputs.cinematic,
                    inputs.objectShadow,
                    inputs.bodyAlpha,
                    nullptr,
                    true,
                    Rendering::RenderVisibilityView::Reflection );
            }
        }
        renderCommands.SetClipPlane( 0, false );
        PrimitiveBatchRendererForFrame( inputs.frame ).SetClipPlane( 0.0f, 1.0f, 0.0f, 1.0e9f );
        m_collisionVisualizer.SetClipPlane( 0.0f, 1.0f, 0.0f, 1.0e9f );
        PROFILE_GPU_END( m_profiler, "Frame/Render/Reflection/Balls" );

        m_resources.target->Unbind();
        renderCommands.SetViewport( 0, 0, inputs.frame.windowWidth, inputs.frame.windowHeight );
        output.reflectionTextureHandle = m_resources.target->GetColorTextureHandle();
        output.reflectionSampleViewProjection = inputs.frame.reflectionViewProjection;
    }
    PROFILE_GPU_END( m_profiler, "Frame/Render/Reflection" );
    return output;
}


void ObjectPass::Render( const ObjectPassInputs& inputs )
{
    const bool transparentPass = inputs.mode == ObjectPassMode::Transparent;
    const char* passName = transparentPass ? "Frame/Render/TransparentBalls" : "Frame/Render/Balls";
    const uint32_t passHash =
        transparentPass ? HashStr( "Frame/Render/TransparentBalls" ) : HashStr( "Frame/Render/Balls" );
#if defined( SKULLBONEZ_PROFILE_ENABLED )
    SkullbonezCore::Core::GpuProfilerScope profileScope( m_profiler, passName, passHash );
#endif
    Rendering::DrawCallTraceScope drawTraceScope( RenderDiagnostics( inputs.frame ), passName, passHash );
    Rendering::IRenderCommandContext& renderCommands = RenderCommands( inputs.frame );

    if ( inputs.collisionStateColorsVisible )
    {
        // Pass contract: collision-state solids are vertex-colored and do not
        // sample textures.
        ClearAllRenderTextureSlots( renderCommands );
        if ( HasCollisionVisualizerFrameView( inputs.frame ) )
        {
            const CollisionVisualizerFrameView frameView = BuildCollisionVisualizerFrameView( inputs.frame );
            m_collisionVisualizer.SetAlphaOverride( inputs.collisionVisualizerAlphaOverride );
            m_collisionVisualizer.Render( RenderAssets( inputs.frame ),
                                          RenderResources( inputs.frame ),
                                          renderCommands,
                                          RenderDiagnostics( inputs.frame ),
                                          frameView,
                                          inputs.frame.baseView,
                                          inputs.frame.projection,
                                          inputs.frame.lightPosition );
            m_collisionVisualizer.SetAlphaOverride( -1.0f );
        }
    }
    else
    {
        // Pass contract: lit model shaders read the material texture in slot 0
        // and optionally the shadow depth texture in slot 3.
        ClearRenderTextureSlotsExcept(
            renderCommands,
            RENDER_TEXTURE_SLOT_0 | ( inputs.shadow && inputs.shadow->valid ? RENDER_TEXTURE_SLOT_3 : 0u ) );
        if ( SelectRenderTexture( inputs.frame, TEXTURE_BOUNDING_SPHERE, passName ) && inputs.frame.renderInstances &&
             inputs.frame.colliders )
        {
            Rendering::RenderInstanceRenderer::RenderModels( PrimitiveRenderContextForFrame( inputs.frame, m_config ),
                                                             *inputs.frame.renderInstances,
                                                             *inputs.frame.colliders,
                                                             inputs.frame.renderCollisionVolumes,
                                                             inputs.frame.baseView,
                                                             inputs.frame.projection,
                                                             inputs.frame.lightPosition,
                                                             inputs.cinematic,
                                                             inputs.shadow,
                                                             inputs.bodyAlpha,
                                                             inputs.modelMask,
                                                             inputs.drawMaskedModels );
        }
    }
}


void ObjectPass::EnsureGpuResources( const RenderResourceContext& /*resources*/ )
{
    // Object mesh/shader resources live behind the scene view; this pass owns
    // the draw contract and texture-slot hygiene, not the model cache.
}


void ObjectPass::ReleaseGpuResources()
{
    // Nothing to release until body shaders/materials move behind this pass.
}


void TerrainPass::Render( const TerrainPassInputs& inputs )
{
    if ( inputs.terrainHidden || !m_terrain.Get() )
    {
        return;
    }

    PROFILE_GPU_BEGIN( m_profiler, "Frame/Render/Terrain" );
    DRAW_CALL_TRACE_SCOPE( RenderDiagnostics( inputs.frame ), "Frame/Render/Terrain" );
    // Pass contract: terrain reads ground albedo from t0, the broad shadow map
    // from t3, and the tight object-shadow map from t5. The material table stays
    // at t4 for instanced object draws and is never repurposed here.
    Rendering::IRenderCommandContext& renderCommands = RenderCommands( inputs.frame );
    ClearRenderTextureSlotsExcept(
        renderCommands,
        RENDER_TEXTURE_SLOT_0 | ( inputs.shadow && inputs.shadow->valid ? RENDER_TEXTURE_SLOT_3 : 0u ) |
            ( inputs.detailShadow && inputs.detailShadow->valid ? RENDER_TEXTURE_SLOT_5 : 0u ) );
    if ( SelectRenderTexture( inputs.frame, TEXTURE_GROUND, "Frame/Render/Terrain" ) )
    {
        m_terrain.Get()->Render( inputs.frame.baseView,
                                 inputs.frame.projection,
                                 renderCommands,
                                 inputs.frame.lightPosition,
                                 inputs.clipPlane,
                                 inputs.cinematic,
                                 inputs.shadow,
                                 inputs.detailShadow );
    }
    PROFILE_GPU_END( m_profiler, "Frame/Render/Terrain" );
}


void TerrainPass::EnsureGpuResources( const RenderResourceContext& resources )
{
    // Terrain mesh/material resources live on Terrain; this pass owns ordering
    // and the receiver texture-slot contract.
    if ( m_terrain.Get() )
    {
        m_terrain.Get()->EnsureRenderResources( m_config, resources.assets, RenderResources( resources ) );
    }
}


void TerrainPass::ReleaseGpuResources()
{
    if ( m_terrain.Get() )
    {
        m_terrain.Get()->ReleaseRenderResources();
    }
}


void WaterPass::Render( const WaterPassInputs& inputs )
{
    m_debugInfo = WaterPassDebugInfo();
    m_debugInfo.skippedHidden = inputs.waterHidden;
    m_debugInfo.skippedModeOff = inputs.frame.cinematicEnabled && inputs.cinematic && inputs.cinematic->waterMode == 0;
    m_debugInfo.reflectionTextureHandle = inputs.reflection.reflectionTextureHandle;
    m_debugInfo.reflectionValid = inputs.reflection.reflectionTextureHandle != 0;
    m_debugInfo.reflectionRaytraced = inputs.reflection.usedDxr;
    m_debugInfo.noReflection = inputs.noReflection;
    m_debugInfo.flatWater = inputs.flatWater;
    m_debugInfo.freezeTime = inputs.freezeTime;
    m_debugInfo.styleWaterMode = inputs.cinematic ? inputs.cinematic->waterMode : -1;

    if ( m_debugInfo.skippedHidden || m_debugInfo.skippedModeOff )
    {
        return;
    }

    PROFILE_GPU_BEGIN( m_profiler, "Frame/Render/Water" );
    DRAW_CALL_TRACE_SCOPE( RenderDiagnostics( inputs.frame ), "Frame/Render/Water" );
    // Pass contract: water samples only the reflection texture in slot 1.
    Rendering::IRenderCommandContext& renderCommands = RenderCommands( inputs.frame );
    ClearRenderTextureSlotsExcept( renderCommands, RENDER_TEXTURE_SLOT_1 );
    float waterTime = inputs.freezeTime ? inputs.frozenTime : inputs.liveWaterTime;
    m_debugInfo.rendered = true;
    m_debugInfo.waterTime = waterTime;
    SkullbonezCore::Environment::WaterReflectionInput reflectionInput;
    reflectionInput.sampleViewProjection = inputs.reflection.reflectionSampleViewProjection;
    reflectionInput.textureHandle = inputs.reflection.reflectionTextureHandle;
    reflectionInput.noReflection = inputs.noReflection;
    reflectionInput.raytraced = inputs.reflection.usedDxr;

    const bool depthTestWasEnabled = renderCommands.IsDepthTestEnabled();
    const bool depthWriteWasEnabled = renderCommands.IsDepthWriteEnabled();
    const bool blendWasEnabled = renderCommands.IsBlendEnabled();
    Rendering::BlendFactor blendSrc = Rendering::BlendFactor::One;
    Rendering::BlendFactor blendDst = Rendering::BlendFactor::Zero;
    renderCommands.GetBlendFunc( blendSrc, blendDst );
    renderCommands.SetBlend( true );
    renderCommands.SetBlendFunc( Rendering::BlendFactor::SrcAlpha, Rendering::BlendFactor::OneMinusSrcAlpha );
    renderCommands.SetDepthTest( true );
    renderCommands.SetDepthWrite( false );
    m_world.RenderFluid( inputs.frame.baseView,
                         inputs.frame.projection,
                         inputs.frame.eye,
                         renderCommands,
                         reflectionInput,
                         waterTime,
                         inputs.flatWater,
                         inputs.frame.cinematicEnabled,
                         inputs.cinematic );
    renderCommands.SetDepthWrite( depthWriteWasEnabled );
    renderCommands.SetDepthTest( depthTestWasEnabled );
    renderCommands.SetBlendFunc( blendSrc, blendDst );
    renderCommands.SetBlend( blendWasEnabled );
    PROFILE_GPU_END( m_profiler, "Frame/Render/Water" );
}


void WaterPass::EnsureGpuResources( const RenderResourceContext& resources )
{
    // Water shader/mesh resources are owned by WorldEnvironment; this pass
    // makes reflection input explicit and keeps water downstream of reflection.
    m_world.EnsureRenderResources( m_config, resources.assets, RenderResources( resources ) );
}


void WaterPass::ReleaseGpuResources()
{
    // WorldEnvironment owns fluid render resources.
}


void TornadoVisualPass::EnsureGpuResources( const RenderResourceContext& /*resources*/,
                                            const TornadoVisualSnapshot& snapshot )
{
    assert( snapshot.visual && snapshot.tornadoSystem && "TornadoVisualPass requires tornado settings snapshot" );
    const TornadoVisualSettings& visual = *snapshot.visual;
    const int ribbonCount = std::clamp( visual.ribbonCount, 0, 16 );
    const int ribbonSegments = std::clamp( visual.ribbonSegments, 2, 96 );
    const int particleCount = std::clamp( visual.particleCount, 0, 256 );
    constexpr int dustBands = 3;
    constexpr int dustSegments = 56;
    const int authoredVortexCount = snapshot.tornadoSystem->enabled
                                        ? (std::max)( 1, static_cast<int>( snapshot.tornadoSystem->vortices.size() ) )
                                        : 1;
    const int vertexCount =
        authoredVortexCount * ( ribbonCount * ribbonSegments * 6 + dustBands * dustSegments * 6 + particleCount * 6 );
    const std::size_t floatCapacity =
        static_cast<std::size_t>( (std::max)( vertexCount, 0 ) ) * TORNADO_VISUAL_FLOATS_PER_VERTEX;
    if ( m_vertices.capacity() < floatCapacity )
    {
        m_vertices.reserve( floatCapacity );
    }
}


void TornadoVisualPass::ReleaseGpuResources()
{
    m_vertices.clear();
    m_vertices.shrink_to_fit();
    m_activeVisualVortices.clear();
    m_activeVisualVortices.shrink_to_fit();
    m_liveVisualTimeSeconds = 0.0f;
    m_lastLiveVisualSourceSeconds = 0.0;
    m_hasLiveVisualTime = false;
}


bool TornadoVisualPass::Render( const TornadoVisualPassInputs& inputs )
{
    const TornadoVisualSnapshot& snapshot = inputs.snapshot;
    assert( snapshot.visual && snapshot.tornadoSystem && snapshot.tornadoField &&
            "TornadoVisualPass requires tornado settings snapshot" );
    const TornadoVisualSettings& visual = *snapshot.visual;
    // Why: backend readiness is already expressed as a frame-borrowed command
    // context. The pass should not reopen the process-global renderer service.
    if ( !visual.enabled || !inputs.frame.renderCommands )
    {
        return false;
    }

    const int ribbonCount = std::clamp( visual.ribbonCount, 0, 16 );
    const int ribbonSegments = std::clamp( visual.ribbonSegments, 2, 96 );
    const int particleCount = std::clamp( visual.particleCount, 0, 256 );
    const float shellAlpha = std::clamp( visual.shellAlpha, 0.0f, 0.30f );
    const float dustAlpha = std::clamp( visual.dustAlpha, 0.0f, 0.30f );
    if ( ( ribbonCount <= 0 || shellAlpha <= 0.0f ) && dustAlpha <= 0.0f )
    {
        return false;
    }

    const float twoPi = 6.28318530718f;
    const ReplayPresentationSample* replaySample = snapshot.replaySample;
    const ReplaySolverFrameSample* solverSample = replaySample ? nullptr : snapshot.solverSample;
    const RunReplayPredictionFrame* predictionFrame =
        ( replaySample || solverSample ) ? nullptr : snapshot.predictionFrame;
    const bool useReplayTime = replaySample != nullptr || solverSample != nullptr || predictionFrame != nullptr;
    const Physics::TornadoSystemConfig& tornadoSystem = *snapshot.tornadoSystem;
    const bool useTornadoSystem = tornadoSystem.enabled && !tornadoSystem.vortices.empty();
    const double sourceSeconds = snapshot.simulationSourceSeconds;
    if ( !m_hasLiveVisualTime || sourceSeconds < m_lastLiveVisualSourceSeconds )
    {
        m_liveVisualTimeSeconds = static_cast<float>( sourceSeconds );
        m_hasLiveVisualTime = true;
    }
    else if ( !useReplayTime && !snapshot.replayLiveAdvanceHeld )
    {
        m_liveVisualTimeSeconds += static_cast<float>( sourceSeconds - m_lastLiveVisualSourceSeconds );
    }
    m_lastLiveVisualSourceSeconds = sourceSeconds;

    float time = m_liveVisualTimeSeconds;
    if ( replaySample )
    {
        time = static_cast<float>( replaySample->simulationSeconds );
    }
    else if ( solverSample )
    {
        time = useTornadoSystem ? solverSample->worldSnapshot.tornadoSystemElapsedSeconds
                                : static_cast<float>( solverSample->simulationSeconds );
    }
    else if ( predictionFrame )
    {
        time = useTornadoSystem ? predictionFrame->tornadoSystemElapsedSeconds
                                : static_cast<float>( predictionFrame->simulationSeconds );
    }
    else if ( useTornadoSystem )
    {
        time = inputs.frame.tornadoElapsedSeconds;
    }

    m_activeVisualVortices.clear();
    if ( useTornadoSystem )
    {
        Physics::TornadoSystem::BuildActiveVortices( tornadoSystem, time, m_activeVisualVortices );
    }
    else
    {
        const Physics::TornadoFieldConfig& field = *snapshot.tornadoField;
        if ( field.enabled && field.radius > 1.0f && field.height > 1.0f )
        {
            Physics::TornadoActiveVortex active;
            active.field = field;
            active.strength = 1.0f;
            active.ageSeconds = time;
            active.sourceIndex = 0;
            m_activeVisualVortices.push_back( active );
        }
    }
    if ( m_activeVisualVortices.empty() )
    {
        return false;
    }

    m_vertices.clear();
    const Vector3 cameraForward =
        NormalizeOr( inputs.frame.viewCenter - inputs.frame.eye, Vector3( 0.0f, 0.0f, 1.0f ) );
    const Vector3 cameraUp = NormalizeOr( inputs.frame.up, Vector3( 0.0f, 1.0f, 0.0f ) );
    const Vector3 cameraRight = NormalizeOr( CrossProduct( cameraForward, cameraUp ), Vector3( 1.0f, 0.0f, 0.0f ) );
    const Vector3 billboardUp = NormalizeOr( CrossProduct( cameraRight, cameraForward ), cameraUp );
    const auto terrainHeightFor = [&]( const Vector3& position )
    {
        if ( m_terrain.Get() && m_terrain.Get()->IsInBounds( position.x, position.z ) )
        {
            return m_terrain.Get()->GetTerrainHeightAt( position.x, position.z );
        }
        return position.y - 64.0f;
    };

    for ( const Physics::TornadoActiveVortex& activeVortex : m_activeVisualVortices )
    {
        const Physics::TornadoFieldConfig& field = activeVortex.field;
        const float rotation = time * visual.rotationSpeed + static_cast<float>( activeVortex.sourceIndex ) * 1.73f;
        const float radius = field.radius;
        const float height = field.height;

        if ( shellAlpha > 0.0f )
        {
            constexpr float shellTurns = 2.85f;
            for ( int ribbon = 0; ribbon < ribbonCount; ++ribbon )
            {
                const float ribbonSeed = HashUnitFloat( 41u + static_cast<uint32_t>( ribbon ) * 97u );
                const float phase = static_cast<float>( ribbon ) * twoPi / static_cast<float>( ribbonCount ) +
                                    rotation + ribbonSeed * 0.45f;
                for ( int segment = 0; segment < ribbonSegments; ++segment )
                {
                    const float t0 = static_cast<float>( segment ) / static_cast<float>( ribbonSegments );
                    const float t1 = static_cast<float>( segment + 1 ) / static_cast<float>( ribbonSegments );
                    const float angle0 = phase + t0 * shellTurns * twoPi;
                    const float angle1 = phase + t1 * shellTurns * twoPi;
                    const float radius0 =
                        radius * ( 0.32f + 0.46f * t0 + 0.035f * sinf( angle0 * 1.7f + ribbonSeed * twoPi ) );
                    const float radius1 =
                        radius * ( 0.32f + 0.46f * t1 + 0.035f * sinf( angle1 * 1.7f + ribbonSeed * twoPi ) );
                    const Vector3 p0 =
                        field.center + CylindricalOffset( radius0, angle0 ) + Vector3( 0.0f, t0 * height, 0.0f );
                    const Vector3 p1 =
                        field.center + CylindricalOffset( radius1, angle1 ) + Vector3( 0.0f, t1 * height, 0.0f );
                    const Vector3 segmentCenter = ( p0 + p1 ) * 0.5f;
                    const Vector3 viewDir = NormalizeOr( inputs.frame.eye - segmentCenter, -cameraForward );
                    const Vector3 tangent = NormalizeOr( p1 - p0, Vector3( 0.0f, 1.0f, 0.0f ) );
                    const Vector3 side = NormalizeOr( CrossProduct( viewDir, tangent ), cameraRight );
                    const float width = (std::max)( 1.0f, visual.ribbonWidth * ( 0.78f + 0.34f * t0 ) );
                    const float baseFade = Clamp01( t0 / 0.16f );
                    const float topFade = Clamp01( ( 1.0f - t0 ) / 0.18f );
                    const float gapFade = 0.72f + 0.28f * sinf( phase + t0 * twoPi * 4.0f );
                    const float alpha = shellAlpha * baseFade * topFade * gapFade;
                    const float cool = 0.72f + 0.08f * t0;
                    EmitFxQuad( m_vertices,
                                p0 - side * width,
                                p1 - side * width,
                                p1 + side * width,
                                p0 + side * width,
                                cool,
                                0.78f,
                                0.84f,
                                alpha,
                                TORNADO_FX_KIND_RIBBON,
                                0.0f,
                                0.0f,
                                0.0f,
                                0.0f );
                }
            }
        }

        if ( dustAlpha > 0.0f )
        {
            constexpr int dustBands = 3;
            constexpr int dustSegments = 56;
            for ( int band = 0; band < dustBands; ++band )
            {
                const float bandT = static_cast<float>( band ) / static_cast<float>( dustBands - 1 );
                const float phase = rotation * ( 1.15f + bandT * 0.35f ) + bandT * twoPi * 0.37f;
                for ( int segment = 0; segment < dustSegments; ++segment )
                {
                    if ( ( segment + band * 3 ) % 5 == 0 || ( segment + band ) % 11 == 0 )
                    {
                        continue;
                    }
                    const float t0 = static_cast<float>( segment ) / static_cast<float>( dustSegments );
                    const float t1 = static_cast<float>( segment + 1 ) / static_cast<float>( dustSegments );
                    const float angle0 = phase + t0 * twoPi * 1.18f;
                    const float angle1 = phase + t1 * twoPi * 1.18f;
                    const float bandRadius = radius * ( 0.58f + 0.16f * static_cast<float>( band ) );
                    const float innerRadius = bandRadius - radius * 0.015f;
                    const float outerRadius = bandRadius + radius * ( 0.024f + 0.008f * bandT );
                    const float y0 =
                        field.center.y + height * ( 0.018f + 0.030f * bandT ) + sinf( angle0 * 2.0f ) * 1.6f;
                    const float y1 =
                        field.center.y + height * ( 0.018f + 0.030f * bandT ) + sinf( angle1 * 2.0f ) * 1.6f;
                    const Vector3 a = field.center + CylindricalOffset( innerRadius, angle0 ) +
                                      Vector3( 0.0f, y0 - field.center.y, 0.0f );
                    const Vector3 b = field.center + CylindricalOffset( innerRadius, angle1 ) +
                                      Vector3( 0.0f, y1 - field.center.y, 0.0f );
                    const Vector3 c = field.center + CylindricalOffset( outerRadius, angle1 ) +
                                      Vector3( 0.0f, y1 - field.center.y, 0.0f );
                    const Vector3 d = field.center + CylindricalOffset( outerRadius, angle0 ) +
                                      Vector3( 0.0f, y0 - field.center.y, 0.0f );
                    const float alpha = dustAlpha * ( 0.42f - 0.08f * bandT );
                    EmitFxQuad( m_vertices,
                                a,
                                b,
                                c,
                                d,
                                0.58f,
                                0.47f,
                                0.31f,
                                alpha,
                                TORNADO_FX_KIND_DUST,
                                terrainHeightFor( a ),
                                terrainHeightFor( b ),
                                terrainHeightFor( c ),
                                terrainHeightFor( d ) );
                }
            }

            for ( int particle = 0; particle < particleCount; ++particle )
            {
                const uint32_t seed = 0x9e3779b9u + static_cast<uint32_t>( particle ) * 0x85ebca6bu;
                const float h0 = HashUnitFloat( seed );
                const float h1 = HashUnitFloat( seed ^ 0x68bc21ebu );
                const float h2 = HashUnitFloat( seed ^ 0x02e5be93u );
                const float heightT = powf( h0, 1.45f );
                const float angularSpeed = 0.65f + heightT * 1.25f;
                const float angle = h1 * twoPi + rotation * angularSpeed + heightT * twoPi * 2.2f;
                const float particleRadius = radius * ( 0.55f + 0.43f * h2 );
                const Vector3 center =
                    field.center + CylindricalOffset( particleRadius, angle ) + Vector3( 0.0f, height * heightT, 0.0f );
                const float size = std::clamp( radius * ( 0.010f + 0.020f * ( 1.0f - heightT ) ), 2.0f, 9.0f );
                const float alpha = dustAlpha * ( 0.38f + 0.42f * ( 1.0f - heightT ) ) * ( 0.55f + 0.45f * h1 );
                const Vector3 right = cameraRight * size;
                const Vector3 up = billboardUp * ( size * ( 0.70f + 0.50f * h2 ) );
                const Vector3 a = center - right - up;
                const Vector3 b = center + right - up;
                const Vector3 c = center + right + up;
                const Vector3 d = center - right + up;
                EmitFxQuad( m_vertices,
                            a,
                            b,
                            c,
                            d,
                            0.68f,
                            0.52f,
                            0.34f,
                            alpha,
                            TORNADO_FX_KIND_DUST,
                            terrainHeightFor( a ),
                            terrainHeightFor( b ),
                            terrainHeightFor( c ),
                            terrainHeightFor( d ) );
            }
        }
    }

    if ( m_vertices.empty() )
    {
        return false;
    }

    PROFILE_GPU_BEGIN( m_profiler, "Frame/Render/TornadoVisual" );
    DRAW_CALL_TRACE_SCOPE( RenderDiagnostics( inputs.frame ), "Frame/Render/TornadoVisual" );
    Rendering::IRenderCommandContext& renderCommands = RenderCommands( inputs.frame );
    ClearAllRenderTextureSlots( renderCommands );
    const bool depthTestWasEnabled = renderCommands.IsDepthTestEnabled();
    const bool depthWriteWasEnabled = renderCommands.IsDepthWriteEnabled();
    const bool blendWasEnabled = renderCommands.IsBlendEnabled();
    const bool cullWasEnabled = renderCommands.IsCullFaceEnabled();
    Rendering::BlendFactor blendSrc = Rendering::BlendFactor::One;
    Rendering::BlendFactor blendDst = Rendering::BlendFactor::Zero;
    renderCommands.GetBlendFunc( blendSrc, blendDst );

    renderCommands.SetDepthTest( true );
    renderCommands.SetDepthWrite( false );
    renderCommands.SetBlend( true );
    renderCommands.SetBlendFunc( Rendering::BlendFactor::SrcAlpha, Rendering::BlendFactor::OneMinusSrcAlpha );
    renderCommands.SetCullFace( false );
    renderCommands.DrawTransientColoredTriangles(
        m_vertices.data(),
        static_cast<int>( m_vertices.size() / TORNADO_VISUAL_FLOATS_PER_VERTEX ),
        inputs.frame.viewProjection.Data() );
    renderCommands.SetCullFace( cullWasEnabled );
    renderCommands.SetBlendFunc( blendSrc, blendDst );
    renderCommands.SetBlend( blendWasEnabled );
    renderCommands.SetDepthWrite( depthWriteWasEnabled );
    renderCommands.SetDepthTest( depthTestWasEnabled );
    PROFILE_GPU_END( m_profiler, "Frame/Render/TornadoVisual" );
    return true;
}


bool DebugOverlayPass::Render( const DebugOverlayPassInputs& inputs )
{
    // Debug overlays intentionally stay out of the object/material pass. They
    // draw diagnostic geometry over the final world view and should not inherit
    // production material binding assumptions.
    if ( !HasOverlayWork( inputs ) )
    {
        return false;
    }

    const bool detailMarkers = SkullbonezCore::Core::PlatformProfiler::AreDetailedRangesEnabled();
    if ( detailMarkers )
    {
        PROFILE_GPU_BEGIN( m_profiler, "Frame/Render/DebugOverlay" );
    }
    DRAW_CALL_TRACE_SCOPE( RenderDiagnostics( inputs.frame ), "Frame/Render/DebugOverlay" );
    const DebugOverlaySnapshot& snapshot = inputs.snapshot;
    if ( snapshot.broadphaseOverlayVisible )
    {
        if ( detailMarkers )
        {
            PROFILE_GPU_BEGIN( m_profiler, "Frame/Render/DebugOverlay/Broadphase" );
        }
        DRAW_CALL_TRACE_SCOPE( RenderDiagnostics( inputs.frame ), "Broadphase" );
        // Pass contract: broadphase owns grid-line generation, while renderer
        // readiness/capability stays with the one-frame debug overlay context.
        const bool supportsDebugLines = RenderDiagnostics( inputs.frame ).GetCapabilities().supportsDebugLines;
        m_broadphaseVisualizer.Render( inputs.frame.viewProjection,
                                       RenderCommands( inputs.frame ),
                                       supportsDebugLines );
        if ( detailMarkers )
        {
            PROFILE_GPU_END( m_profiler, "Frame/Render/DebugOverlay/Broadphase" );
        }
    }

    if ( snapshot.tornadoVectorsVisible )
    {
        if ( detailMarkers )
        {
            PROFILE_GPU_BEGIN( m_profiler, "Frame/Render/DebugOverlay/TornadoField" );
        }
        DRAW_CALL_TRACE_SCOPE( RenderDiagnostics( inputs.frame ), "TornadoField" );
        RenderTornadoVectorOverlay( inputs );
        if ( detailMarkers )
        {
            PROFILE_GPU_END( m_profiler, "Frame/Render/DebugOverlay/TornadoField" );
        }
    }

    RunEditorTracer& tracer = inputs.runtimeTools.EditorTracer();
    // Invariant: production submission and validation observe this same
    // replay-owned packet; neither may rebuild geometry from tracer internals.
    tracer.Render( inputs.replayVisualPacket, inputs.frame.viewProjection, RenderCommands( inputs.frame ) );
    inputs.runtimeTools.Laser().Render( inputs.frame.viewProjection,
                                        inputs.frame.eye,
                                        inputs.frame.up,
                                        m_assets,
                                        RenderResources( inputs.frame ),
                                        RenderCommands( inputs.frame ) );

    if ( snapshot.physicsDebugFlags != PHYSICS_DEBUG_NONE )
    {
        if ( detailMarkers )
        {
            PROFILE_GPU_BEGIN( m_profiler, "Frame/Render/DebugOverlay/PhysicsDebug" );
        }
        DRAW_CALL_TRACE_SCOPE( RenderDiagnostics( inputs.frame ), "PhysicsDebug" );
        m_physicsDebugVisualizer.SetFlags( snapshot.physicsDebugFlags );
        m_physicsDebugVisualizer.SetPipelineStageCursor( snapshot.physicsDebugPipelineStageCursor );
        if ( HasPhysicsDebugFrameView( inputs.frame ) )
        {
            const PhysicsDebugFrameView frameView = BuildPhysicsDebugFrameView( inputs.frame );
            // Pass contract: physics debug owns diagnostic line generation,
            // while renderer readiness/capability stays with this frame pass.
            const bool supportsDebugLines = RenderDiagnostics( inputs.frame ).GetCapabilities().supportsDebugLines;
            m_physicsDebugVisualizer.Render( frameView,
                                             inputs.frame.viewProjection,
                                             RenderCommands( inputs.frame ),
                                             supportsDebugLines,
                                             m_terrain.Get() );
        }
        if ( detailMarkers )
        {
            PROFILE_GPU_END( m_profiler, "Frame/Render/DebugOverlay/PhysicsDebug" );
        }
    }
    if ( detailMarkers )
    {
        PROFILE_GPU_END( m_profiler, "Frame/Render/DebugOverlay" );
    }
    return true;
}


bool DebugOverlayPass::HasOverlayWork( const DebugOverlayPassInputs& inputs ) const
{
    const DebugOverlaySnapshot& snapshot = inputs.snapshot;
    if ( snapshot.broadphaseOverlayVisible )
    {
        return true;
    }
    if ( snapshot.tornadoOverlayWorkVisible || snapshot.tornadoVectorsVisible )
    {
        return true;
    }
    if ( snapshot.physicsDebugFlags != PHYSICS_DEBUG_NONE )
    {
        return true;
    }

    return snapshot.editorOverlayWorkVisible;
}


void DebugOverlayPass::RenderTornadoVectorOverlay( const DebugOverlayPassInputs& inputs )
{
    const DebugOverlaySnapshot& snapshot = inputs.snapshot;
    if ( !snapshot.tornadoField || !snapshot.tornadoSystem )
    {
        return;
    }
    const bool supportsDebugLines = RenderDiagnostics( inputs.frame ).GetCapabilities().supportsDebugLines;
    if ( !supportsDebugLines )
    {
        return;
    }

    const Physics::TornadoSystemConfig& tornadoSystem = *snapshot.tornadoSystem;
    const bool useTornadoSystem = tornadoSystem.enabled && !tornadoSystem.vortices.empty();
    m_tornadoVectorVortices.clear();
    if ( useTornadoSystem )
    {
        Physics::TornadoSystem::BuildActiveVortices( tornadoSystem,
                                                     inputs.frame.tornadoElapsedSeconds,
                                                     m_tornadoVectorVortices );
    }
    else
    {
        const Physics::TornadoFieldConfig& field = *snapshot.tornadoField;
        if ( field.enabled )
        {
            Physics::TornadoActiveVortex active;
            active.field = field;
            active.strength = 1.0f;
            active.ageSeconds = inputs.frame.tornadoElapsedSeconds;
            active.sourceIndex = 0;
            m_tornadoVectorVortices.push_back( active );
        }
    }

    Rendering::IRenderCommandContext& renderCommands = RenderCommands( inputs.frame );
    constexpr int ANGLE_STEPS = 12;
    constexpr int RADIUS_STEPS = 4;
    constexpr int HEIGHT_STEPS = 5;
    constexpr float PI = 3.1415926535f;
    for ( const Physics::TornadoActiveVortex& vortex : m_tornadoVectorVortices )
    {
        const Physics::TornadoFieldConfig& fieldConfig = vortex.field;
        if ( !fieldConfig.visualizeVelocityField )
        {
            continue;
        }

        // Concept: the runtime overlay samples physics-owned tornado math, then
        // submits line vertices through the frame command context. The solver
        // still owns forces; rendering owns every draw call and scratch buffer.
        m_tornadoVectorLineData.clear();
        const float maxFieldSpeed = (std::max)( 1.0f,
                                                sqrtf( fieldConfig.inwardAcceleration * fieldConfig.inwardAcceleration +
                                                       fieldConfig.swirlAcceleration * fieldConfig.swirlAcceleration +
                                                       fieldConfig.liftAcceleration * fieldConfig.liftAcceleration ) );
        const auto emit = [&]( const Vector3& a, const Vector3& b, float r, float g, float bColor )
        {
            m_tornadoVectorLineData.push_back( a.x );
            m_tornadoVectorLineData.push_back( a.y );
            m_tornadoVectorLineData.push_back( a.z );
            m_tornadoVectorLineData.push_back( r );
            m_tornadoVectorLineData.push_back( g );
            m_tornadoVectorLineData.push_back( bColor );
            m_tornadoVectorLineData.push_back( b.x );
            m_tornadoVectorLineData.push_back( b.y );
            m_tornadoVectorLineData.push_back( b.z );
            m_tornadoVectorLineData.push_back( r );
            m_tornadoVectorLineData.push_back( g );
            m_tornadoVectorLineData.push_back( bColor );
        };

        for ( int h = 0; h < HEIGHT_STEPS; ++h )
        {
            const float height01 = 0.12f + static_cast<float>( h ) * ( 0.78f / static_cast<float>( HEIGHT_STEPS - 1 ) );
            const float y = fieldConfig.center.y + fieldConfig.height * height01;
            for ( int rIndex = 0; rIndex < RADIUS_STEPS; ++rIndex )
            {
                const float radial01 =
                    0.22f + static_cast<float>( rIndex ) * ( 0.72f / static_cast<float>( RADIUS_STEPS - 1 ) );
                const float radius = fieldConfig.radius * radial01;
                for ( int aIndex = 0; aIndex < ANGLE_STEPS; ++aIndex )
                {
                    const float angle =
                        ( static_cast<float>( aIndex ) / static_cast<float>( ANGLE_STEPS ) ) * PI * 2.0f;
                    Vector3 start( fieldConfig.center.x + cosf( angle ) * radius,
                                   y,
                                   fieldConfig.center.z + sinf( angle ) * radius );
                    Vector3 field = Physics::TornadoField::SampleAccelerationForConfig( fieldConfig, start );
                    const float speed = SkullbonezCore::Math::Vector::VectorMag( field );
                    if ( speed <= TOLERANCE )
                    {
                        continue;
                    }

                    const float t = std::clamp( speed / maxFieldSpeed, 0.0f, 1.0f );
                    const float red = t;
                    const float green = 1.0f - t;
                    const float arrowLength = 9.0f + 23.0f * t;
                    Vector3 dir = field / speed;
                    Vector3 end = start + dir * arrowLength;
                    emit( start, end, red, green, 0.0f );

                    Vector3 side( -dir.z, 0.0f, dir.x );
                    const float sideMag = SkullbonezCore::Math::Vector::VectorMag( side );
                    if ( sideMag > TOLERANCE )
                    {
                        side /= sideMag;
                    }
                    else
                    {
                        side = Vector3( 1.0f, 0.0f, 0.0f );
                    }
                    Vector3 headBase = end - dir * 4.4f;
                    emit( end, headBase + side * 2.4f, red, green, 0.0f );
                    emit( end, headBase - side * 2.4f, red, green, 0.0f );
                }
            }
        }

        if ( !m_tornadoVectorLineData.empty() )
        {
            const int vertCount = static_cast<int>( m_tornadoVectorLineData.size() / 6 );
            renderCommands.DrawLinesColored( m_tornadoVectorLineData.data(),
                                             vertCount,
                                             inputs.frame.viewProjection.Data() );
        }
    }
}


void DebugOverlayPass::EnsureGpuResources( const RenderResourceContext& /*resources*/ )
{
    // Debug visualizers own their transient geometry; this pass owns late-frame
    // ordering so diagnostics draw over production geometry.
}


void DebugOverlayPass::ReleaseGpuResources()
{
    // Current debug visualizers release with their owning systems.
}


void VolumetricPass::EnsureGpuResources( const RenderResourceContext& resources )
{
    if ( !resources.cinematicEnabled )
    {
        return;
    }

    if ( !m_volumetricResources.shader )
    {
        // Half-resolution pass: creates warm light shafts that tonemap can add
        // without making every world shader understand volumetric lighting.
        m_volumetricResources.shader =
            resources.assets.CreateShader( RenderResources( resources ), "shader.post_volumetric_light" );
    }
}


void VolumetricPass::ReleaseGpuResources()
{
    m_volumetricResources.shader.reset();
}


bool VolumetricPass::CanRender( const RenderFrameContext& frame ) const
{
    const SkullbonezCore::Core::CinematicRenderConfig* cinematic = frame.cinematic;
    return frame.cinematicEnabled && cinematic && cinematic->volumetricLightingEnabled && m_sceneResources.hdrTarget &&
           m_volumetricResources.shader && m_fullscreenResources.quadVB != 0;
}


bool VolumetricPass::Render( const RenderFrameContext& frame, const Rendering::RenderGraphTextureBinding* graphOutput )
{
    if ( !CanRender( frame ) )
    {
        return false;
    }

    assert( frame.cinematic && "Volumetric pass requires a frame cinematic snapshot" );
    const SkullbonezCore::Core::CinematicRenderConfig& cinematic = *frame.cinematic;

    const bool detailMarkers = SkullbonezCore::Core::PlatformProfiler::AreDetailedRangesEnabled();
    if ( detailMarkers )
    {
        PROFILE_GPU_BEGIN( m_profiler, "Frame/Render/VolumetricLight" );
    }
    DRAW_CALL_TRACE_SCOPE( RenderDiagnostics( frame ), "Frame/Render/VolumetricLight" );
    Rendering::IRenderCommandContext& renderCommands = RenderCommands( frame );
    const bool useGraphOutput = graphOutput && graphOutput->IsValid() && graphOutput->renderTarget;
    if ( !useGraphOutput )
    {
        return false;
    }
    renderCommands.BeginGraphTextureRenderTarget( *graphOutput, "VolumetricLightPass" );
    renderCommands.SetViewport( 0, 0, graphOutput->width, graphOutput->height );

    // This is another screen-space effect, so depth testing and blending are
    // disabled while the full-screen quad is generated.
    const bool depthWasEnabled = renderCommands.IsDepthTestEnabled();
    const bool blendWasEnabled = renderCommands.IsBlendEnabled();
    renderCommands.SetDepthTest( false );
    renderCommands.SetDepthWrite( false );
    renderCommands.SetBlend( false );

    {
        if ( detailMarkers )
        {
            PROFILE_GPU_BEGIN( m_profiler, "Frame/Render/VolumetricLight/Draw" );
        }
        DRAW_CALL_TRACE_SCOPE( RenderDiagnostics( frame ), "Draw" );
        m_volumetricResources.shader->Use();
        BindVolumetricPassParams( *m_volumetricResources.shader,
                                  frame.eye,
                                  frame.viewProjection,
                                  cinematic,
                                  m_config.camera.frustumNear,
                                  m_config.camera.frustumFar );
        // Pass contract: texture slot 0 is rendered color, slot 1 is rendered
        // depth. The shader uses depth to tell sky pixels from solid geometry so
        // rays pass through sky and fade when they cross hills/balls.
        BindRenderTextureSlots( renderCommands,
                                m_sceneResources.hdrTarget->GetColorTextureHandle(),
                                m_sceneResources.hdrTarget->GetDepthTextureHandle(),
                                0,
                                0 );
        DrawFullscreenQuad( renderCommands, m_fullscreenResources.quadVB );
        if ( detailMarkers )
        {
            PROFILE_GPU_END( m_profiler, "Frame/Render/VolumetricLight/Draw" );
        }
    }

    renderCommands.SetDepthTest( depthWasEnabled );
    renderCommands.SetDepthWrite( depthWasEnabled );
    renderCommands.SetBlend( blendWasEnabled );
    renderCommands.EndGraphTextureRenderTarget( *graphOutput, "VolumetricLightPass" );
    renderCommands.SetViewport( 0, 0, frame.windowWidth, frame.windowHeight );
    if ( detailMarkers )
    {
        PROFILE_GPU_END( m_profiler, "Frame/Render/VolumetricLight" );
    }
    return true;
}


void TonemapPass::EnsureGpuResources( const RenderResourceContext& resources )
{
    if ( !resources.cinematicEnabled )
    {
        return;
    }

    if ( !m_tonemapResources.shader )
    {
        // Final full-screen shader: combines HDR scene color, depth fog, bloom,
        // grade, vignette, and optional volumetric light into the backbuffer.
        m_tonemapResources.shader =
            resources.assets.CreateShader( RenderResources( resources ), "shader.post_tonemap" );
    }
}


void TonemapPass::ReleaseGpuResources()
{
    m_tonemapResources.shader.reset();
}


void TonemapPass::Render( const RenderFrameContext& frame,
                          bool sceneAlreadyUnbound,
                          bool volumetricReady,
                          const Rendering::RenderGraphTextureBinding* graphVolumetric )
{
    if ( !m_sceneResources.hdrTarget || !m_tonemapResources.shader || m_fullscreenResources.quadVB == 0 )
    {
        return;
    }

    const bool detailMarkers = SkullbonezCore::Core::PlatformProfiler::AreDetailedRangesEnabled();
    if ( detailMarkers )
    {
        PROFILE_GPU_BEGIN( m_profiler, "Frame/Render/Tonemap" );
    }
    DRAW_CALL_TRACE_SCOPE( RenderDiagnostics( frame ), "Frame/Render/Tonemap" );
    if ( !sceneAlreadyUnbound )
    {
        m_sceneResources.hdrTarget->Unbind();
    }
    Rendering::IRenderCommandContext& renderCommands = RenderCommands( frame );
    renderCommands.SetViewport( 0, 0, frame.windowWidth, frame.windowHeight );

    const bool depthWasEnabled = renderCommands.IsDepthTestEnabled();
    const bool blendWasEnabled = renderCommands.IsBlendEnabled();
    renderCommands.SetDepthTest( false );
    renderCommands.SetDepthWrite( false );
    renderCommands.SetBlend( false );

    // Concept: "resolve" means "turn our off-screen cinematic render target
    // into the final image on the window." This is where the HDR scene becomes
    // normal display color and where bloom/fog/rays are layered in.
    {
        if ( detailMarkers )
        {
            PROFILE_GPU_BEGIN( m_profiler, "Frame/Render/Tonemap/Draw" );
        }
        DRAW_CALL_TRACE_SCOPE( RenderDiagnostics( frame ), "Draw" );
        m_tonemapResources.shader->Use();
        assert( frame.cinematic && "Tonemap pass requires a frame cinematic snapshot" );
        const SkullbonezCore::Core::CinematicRenderConfig& cinematic = *frame.cinematic;
        BindTonemapPassParams( *m_tonemapResources.shader,
                               cinematic,
                               m_config.camera.frustumNear,
                               m_config.camera.frustumFar,
                               m_sceneResources.hdrTarget->GetWidth(),
                               m_sceneResources.hdrTarget->GetHeight(),
                               volumetricReady );
        const bool useGraphVolumetric =
            volumetricReady && graphVolumetric && graphVolumetric->IsValid() && graphVolumetric->shaderResource;
        const uint32_t volumetricTexture =
            useGraphVolumetric ? graphVolumetric->textureHandle : m_sceneResources.hdrTarget->GetColorTextureHandle();
        // Pass contract: slot 0 is the bright HDR scene, slot 1 is its depth
        // buffer for fog, and slot 2 is the sole completed shaft texture or a
        // harmless fallback when the volumetric pass is disabled.
        BindRenderTextureSlots( renderCommands,
                                m_sceneResources.hdrTarget->GetColorTextureHandle(),
                                m_sceneResources.hdrTarget->GetDepthTextureHandle(),
                                volumetricTexture,
                                0 );
        DrawFullscreenQuad( renderCommands, m_fullscreenResources.quadVB );
        if ( detailMarkers )
        {
            PROFILE_GPU_END( m_profiler, "Frame/Render/Tonemap/Draw" );
        }
    }

    renderCommands.SetDepthTest( depthWasEnabled );
    renderCommands.SetDepthWrite( depthWasEnabled );
    renderCommands.SetBlend( blendWasEnabled );
    if ( detailMarkers )
    {
        PROFILE_GPU_END( m_profiler, "Frame/Render/Tonemap" );
    }
}
