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
  - Every PSO-affecting draw selects a complete pass-local raster bucket; no
    pass saves, mutates, or restores another pass's fixed-function state.

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
#include "../../Rendering/DX12/Dx12Diagnostics.h"
#include "../../Rendering/IRenderDeviceLifecycle.h"
#include "../../Rendering/DX12/RenderBackendDX12.h"
#include "../../Rendering/DX12/Dx12ResourceBuilder.h"
#include "../../Rendering/RenderInstanceRenderer.h"
#include "../../Rendering/PrimitiveBatchRenderer.h"
#include "../../Rendering/RenderGpuTimingOwner.h"
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
constexpr SkullbonezCore::Rendering::PassRasterStateBucket FULLSCREEN_OPAQUE_RASTER =
    SkullbonezCore::Rendering::MakePassRasterStateBucket( 0, false, false, false );
constexpr SkullbonezCore::Rendering::PassRasterStateBucket SHADOW_DEPTH_RASTER =
    SkullbonezCore::Rendering::MakePassRasterStateBucket( 0,
                                                          true,
                                                          true,
                                                          false,
                                                          SkullbonezCore::Rendering::BlendFactor::One,
                                                          SkullbonezCore::Rendering::BlendFactor::Zero,
                                                          SkullbonezCore::Rendering::CullMode::Back,
                                                          { true, 4.0f, 2.0f } );
constexpr SkullbonezCore::Rendering::PassRasterStateBucket WATER_RASTER =
    SkullbonezCore::Rendering::MakePassRasterStateBucket( 0,
                                                          true,
                                                          false,
                                                          true,
                                                          SkullbonezCore::Rendering::BlendFactor::SrcAlpha,
                                                          SkullbonezCore::Rendering::BlendFactor::OneMinusSrcAlpha );
constexpr SkullbonezCore::Rendering::PassRasterStateBucket TERRAIN_RASTER =
    SkullbonezCore::Rendering::MakePassRasterStateBucket( 0, true, true, false );
constexpr SkullbonezCore::Rendering::PassRasterStateBucket DEBUG_LINE_RASTER =
    SkullbonezCore::Rendering::MakePassRasterStateBucket( 0,
                                                          false,
                                                          false,
                                                          false,
                                                          SkullbonezCore::Rendering::BlendFactor::One,
                                                          SkullbonezCore::Rendering::BlendFactor::Zero,
                                                          SkullbonezCore::Rendering::CullMode::None );

void ClearRenderTextureSlotsExcept( SkullbonezCore::Rendering::Dx12TextureOwner& renderTextures,
                                    unsigned int keptSlots )
{
    for ( int slot = 0; slot < RENDER_TEXTURE_SLOT_COUNT; ++slot )
    {
        if ( ( keptSlots & ( 1u << slot ) ) == 0u )
        {
            renderTextures.BindTexture( 0, slot );
        }
    }
}

void ClearAllRenderTextureSlots( SkullbonezCore::Rendering::Dx12TextureOwner& renderTextures )
{
    ClearRenderTextureSlotsExcept( renderTextures, 0u );
}

int CopyDxrRenderInstanceMatrices( const SkullbonezCore::Rendering::RenderInstanceStore& renderStore,
                                   Matrix4* outMatrices,
                                   int maxModelCount )
{
    if ( !outMatrices || maxModelCount <= 0 )
    {
        return 0;
    }

    const auto instances = renderStore.Records();
    const int modelCount = (std::min)( static_cast<int>( instances.size() ), maxModelCount );
    for ( int i = 0; i < modelCount; ++i )
    {
        outMatrices[i] = instances[static_cast<std::size_t>( i )].modelMatrix;
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

void BindRenderTextureSlots( SkullbonezCore::Rendering::Dx12TextureOwner& renderTextures,
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
        renderTextures.BindTexture( handles[slot], slot );
    }
}

SkullbonezCore::Rendering::Dx12GeometryOwner& RenderCommands( const RenderFrameContext& frame )
{
    assert( frame.renderGeometry && "RenderFrameContext requires a geometry submission owner" );
    return *frame.renderGeometry;
}

SkullbonezCore::Rendering::Dx12TextureOwner& RenderTextureOwner( const RenderFrameContext& frame )
{
    assert( frame.renderTextures && "RenderFrameContext requires a texture binding owner" );
    return *frame.renderTextures;
}

SkullbonezCore::Rendering::Dx12FrameOwner& RenderFrameOwner( const RenderFrameContext& frame )
{
    assert( frame.renderFrame && "RenderFrameContext requires a frame command owner" );
    return *frame.renderFrame;
}

SkullbonezCore::Rendering::Dx12GraphTransientPool& RenderGraphOwner( const RenderFrameContext& frame )
{
    assert( frame.renderGraph && "RenderFrameContext requires a graph execution owner" );
    return *frame.renderGraph;
}

SkullbonezCore::Rendering::Dx12ResourceBuilder& RenderResources( const RenderResourceContext& resources )
{
    return resources.renderResources;
}

SkullbonezCore::Rendering::Dx12GeometryOwner& RenderGeometry( const RenderResourceContext& resources )
{
    return resources.renderGeometry;
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

SkullbonezCore::Rendering::Dx12ResourceBuilder& RenderResources( const RenderFrameContext& frame )
{
    assert( frame.renderResources && "RenderFrameContext requires a resource builder" );
    return *frame.renderResources;
}

SkullbonezCore::Rendering::Dx12GeometryOwner& RenderGeometry( const RenderFrameContext& frame )
{
    assert( frame.renderGeometry && "RenderFrameContext requires a geometry owner" );
    return *frame.renderGeometry;
}

PrimitiveRenderContext PrimitiveRenderContextForFrame( const RenderFrameContext& frame,
                                                       const SkullbonezCore::Core::EngineConfig& config )
{
    assert( frame.renderDiagnostics && "RenderFrameContext requires a render diagnostics context" );
    assert( frame.primitiveBatches && "RenderFrameContext requires a primitive batch renderer" );
    return PrimitiveRenderContext{ RenderResources( frame ),
                                   *frame.renderTextures,
                                   *frame.renderGeometry,
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

SkullbonezCore::Rendering::Dx12Diagnostics& RenderDiagnostics( const RenderFrameContext& frame )
{
    assert( frame.renderDiagnostics && "RenderFrameContext requires a render diagnostics context" );
    return *frame.renderDiagnostics;
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

void DrawFullscreenQuad( SkullbonezCore::Rendering::Dx12GeometryOwner& renderCommands,
                         uint32_t quadVB,
                         const SkullbonezCore::Rendering::PassRasterStateBucket& rasterState )
{
    // Shared post vertex contract: clip-space xy followed by UV. Keeping one
    // copy prevents sky, volumetric, and tonemap from quietly drifting apart.
    renderCommands.UploadAndDrawDynamicVB( quadVB, FULLSCREEN_QUAD_VERTS, rasterState );
}

void BindSkyPassParams( SkullbonezCore::Rendering::ShaderDX12& shader,
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

void BindVolumetricPassParams( SkullbonezCore::Rendering::ShaderDX12& shader,
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

void BindTonemapPassParams( SkullbonezCore::Rendering::ShaderDX12& shader,
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
        m_resources.quadVB = RenderGeometry( resources ).CreateDynamicVB( attribs, 2, 6 );
    }
}


void FullscreenQuadPass::ReleaseGpuResources( Rendering::Dx12GeometryOwner* renderGeometry )
{
    if ( renderGeometry && m_resources.quadVB != 0 )
    {
        renderGeometry->DestroyDynamicVB( m_resources.quadVB );
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
    // objects before the unique_ptr destructor drops the concrete target owner.
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

    // Concept: the shadow map is a concrete DX12 depth framebuffer. It is
    // intentionally owned outside the cinematic HDR target because the same
    // light-space depth texture is useful in normal backbuffer rendering,
    // cinematic rendering, and screenshot/perf scenes. The cinematic config
    // still supplies map size and bias/softness values, but the feature itself
    // is no longer gated by the cinematic post-processing path.
    const int mapSize = std::clamp( cinematic.shadow.mapSize, 256, 8192 );
    auto ensureTarget = [&]( std::unique_ptr<Rendering::FramebufferDX12>& target )
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


void ShadowPass::RenderShadowMap( Rendering::FramebufferDX12& target,
                                  const PrimitiveRenderContext& primitiveContext,
                                  const Rendering::ShadowFrameData& shadowFrame,
                                  const SkullbonezCore::Core::CinematicRenderConfig& cinematic,
                                  Rendering::Dx12FrameOwner& renderFrame,
                                  Rendering::Dx12TextureOwner& renderTextures,
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
    renderFrame.SetViewport( 0, 0, target.GetWidth(), target.GetHeight() );
    renderFrame.Clear( {} );

    // The bucket applies opaque depth writes, back-face culling, and the
    // rasterizer bias to each caster PSO without mutating later passes.
    // Pass contract: shadow depth shaders write depth only and sample no
    // textures. Clear inherited slots so descriptor state from the visible
    // scene cannot leak into this off-screen pass.
    ClearAllRenderTextureSlots( renderTextures );

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
                                            SHADOW_DEPTH_RASTER,
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

    target.Unbind();
    renderFrame.SetViewport( 0, 0, m_activeWindowWidth, m_activeWindowHeight );
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
        PROFILE_GPU_BEGIN( inputs.frame.renderGpuTiming, "Frame/Shadows/ShadowMap" );
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
                                 RenderFrameOwner( inputs.frame ),
                                 RenderTextureOwner( inputs.frame ),
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
                                 RenderFrameOwner( inputs.frame ),
                                 RenderTextureOwner( inputs.frame ),
                                 false,
                                 true,
                                 *inputs.frame.renderInstances,
                                 *inputs.frame.colliders,
                                 inputs.frame.renderWorkerPool,
                                 inputs.frame.shadowParallelPrep,
                                 &objectCasters );
            }
        }
        PROFILE_GPU_END( inputs.frame.renderGpuTiming, "Frame/Shadows/ShadowMap" );
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
    Rendering::Dx12GeometryOwner& renderGeometry = RenderCommands( frame );
    // Pass contract: this generated sky samples no textures. Clear inherited
    // SRV slots before the fullscreen draw so stale pass inputs cannot be
    // recopied by the backend while the sky shader is active.
    ClearAllRenderTextureSlots( RenderTextureOwner( frame ) );
    m_skyResources.atmosphereShader->Use();
    BindSkyPassParams( *m_skyResources.atmosphereShader, view, frame.projection, cinematic );
    DrawFullscreenQuad( renderGeometry, m_fullscreenResources.quadVB, FULLSCREEN_OPAQUE_RASTER );
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
    ClearRenderTextureSlotsExcept( RenderTextureOwner( frame ), RENDER_TEXTURE_SLOT_0 );
    assert( m_skyBox && "SkyPass requires the world-view sky owner after initialise" );
    ReportRenderTextureResult( "Frame/Render/Skybox", m_skyBox->Render( skyView, frame.projection ) );
}


void SceneTargetPass::Begin( const RenderFrameContext& frame, SkyPass& skyPass )
{
    // Invariant: from this point onward, draw the world into the HDR scene
    // target instead of directly into the window. The post pass later moves it
    // to the backbuffer with the cinematic effects applied.
    m_resources.hdrTarget->Bind();
    Rendering::Dx12FrameOwner& renderFrame = RenderFrameOwner( frame );
    renderFrame.SetViewport( 0, 0, m_resources.hdrTarget->GetWidth(), m_resources.hdrTarget->GetHeight() );
    renderFrame.Clear( {} );

    PROFILE_GPU_BEGIN( frame.renderGpuTiming, "Frame/Render/CinematicSky" );
    {
        DRAW_CALL_TRACE_SCOPE( RenderDiagnostics( frame ), "Frame/Render/CinematicSky" );
        skyPass.Render( frame, frame.baseView, SkyPassMode::CinematicIfEnabled );
    }
    PROFILE_GPU_END( frame.renderGpuTiming, "Frame/Render/CinematicSky" );
}


ReflectionPassOutput ReflectionPass::Render( const ReflectionPassInputs& inputs, SkyPass& skyPass )
{
    ReflectionPassOutput output;

    // Concept: two implementations, one water-pass contract. The planar path
    // renders the above-water scene from a mirrored camera into an FBO. The DXR
    // path rebuilds the raytracing TLAS and writes a screen-space reflection
    // texture directly. Both feed the same water shader later.
    // Hazard: texture resolution and framebuffer creation have recoverable
    // early returns. A lexical scope keeps both profiler stacks balanced on
    // every exit instead of requiring each fallback to duplicate an end call.
    PROFILE_GPU_SCOPED( inputs.frame.renderGpuTiming, "Frame/Render/Reflection" );
    DRAW_CALL_TRACE_SCOPE( RenderDiagnostics( inputs.frame ), "Frame/Render/Reflection" );
    const auto renderCapabilities = RenderDiagnostics( inputs.frame ).GetCapabilities();
    Rendering::Dx12RaytracingOwner* rayTracing = inputs.frame.renderRayTracing;
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
        rayTracing->BuildTLAS(
            std::span<const Matrix4>( m_dxrReflectionTransforms, static_cast<std::size_t>( ballCount ) ) );

        // Ray generation reconstructs world-space rays from screen pixels, so
        // it needs the inverse of the main camera view-projection matrix.
        Rendering::WaterReflectionRayDesc reflection;
        reflection.inverseViewProjection = inputs.frame.viewProjection.Inverse();
        reflection.cameraPosition = inputs.frame.eye;
        reflection.lightPosition =
            Vector3( inputs.frame.lightPosition[0], inputs.frame.lightPosition[1], inputs.frame.lightPosition[2] );
        reflection.waterHeight = inputs.frame.waterY;
        reflection.simulationTimeSeconds = inputs.simulationTimeSeconds;
        // Default colors preserve ordinary-water sky misses; cinematic scenes
        // replace the typed values so ray misses match authored void colors.
        if ( inputs.cinematic && inputs.cinematic->enabled )
        {
            reflection.skyColorTop =
                Vector3( inputs.cinematic->skyZenithR, inputs.cinematic->skyZenithG, inputs.cinematic->skyZenithB );
            reflection.skyColorBottom =
                Vector3( inputs.cinematic->skyHorizonR, inputs.cinematic->skyHorizonG, inputs.cinematic->skyHorizonB );
        }

        Textures::TextureCollection& textures = RenderTextures( inputs.frame );
        if ( !ResolveRenderTextureHandle( textures,
                                          TEXTURE_BOUNDING_SPHERE,
                                          "Frame/Render/Reflection/DXR",
                                          reflection.textures.sphere ) ||
             !ResolveRenderTextureHandle( textures,
                                          TEXTURE_GROUND,
                                          "Frame/Render/Reflection/DXR",
                                          reflection.textures.terrain ) ||
             !ResolveRenderTextureHandle( textures,
                                          TEXTURE_SKY_UP,
                                          "Frame/Render/Reflection/DXR",
                                          reflection.textures.skyUp ) ||
             !ResolveRenderTextureHandle( textures,
                                          TEXTURE_SKY_DOWN,
                                          "Frame/Render/Reflection/DXR",
                                          reflection.textures.skyDown ) ||
             !ResolveRenderTextureHandle( textures,
                                          TEXTURE_SKY_RIGHT,
                                          "Frame/Render/Reflection/DXR",
                                          reflection.textures.skyRight ) ||
             !ResolveRenderTextureHandle( textures,
                                          TEXTURE_SKY_LEFT,
                                          "Frame/Render/Reflection/DXR",
                                          reflection.textures.skyLeft ) ||
             !ResolveRenderTextureHandle( textures,
                                          TEXTURE_SKY_FRONT,
                                          "Frame/Render/Reflection/DXR",
                                          reflection.textures.skyFront ) ||
             !ResolveRenderTextureHandle( textures,
                                          TEXTURE_SKY_BACK,
                                          "Frame/Render/Reflection/DXR",
                                          reflection.textures.skyBack ) )
        {
            return output;
        }
        rayTracing->DispatchReflectionRays( reflection );
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
        Rendering::Dx12TextureOwner& renderTextures = RenderTextureOwner( inputs.frame );
        Rendering::Dx12FrameOwner& renderFrame = RenderFrameOwner( inputs.frame );
        m_resources.target->Bind();
        renderFrame.SetViewport( 0, 0, m_resources.target->GetWidth(), m_resources.target->GetHeight() );
        renderFrame.Clear( {} );

        // Skybox reflected (XZ follows eye; Y anchored at runtime config).
        // Cinematic mode can reflect the generated sunset sky into the water
        // instead of the usual cube-map sky.
        PROFILE_GPU_BEGIN( inputs.frame.renderGpuTiming, "Frame/Render/Reflection/Skybox" );
        {
            DRAW_CALL_TRACE_SCOPE( RenderDiagnostics( inputs.frame ), "Frame/Render/Reflection/Skybox" );
            skyPass.Render( inputs.frame, inputs.frame.reflectionView, SkyPassMode::CinematicIfEnabled );
        }
        PROFILE_GPU_END( inputs.frame.renderGpuTiming, "Frame/Render/Reflection/Skybox" );

        // Why: clip at the water surface so the reflection texture contains only
        // the above-water portion of models. The water shader supplies the
        // below-surface visual from the main scene.
        PROFILE_GPU_BEGIN( inputs.frame.renderGpuTiming, "Frame/Render/Reflection/Balls" );
        DRAW_CALL_TRACE_SCOPE( RenderDiagnostics( inputs.frame ), "Frame/Render/Reflection/Balls" );
        PrimitiveBatchRendererForFrame( inputs.frame ).SetClipPlane( 0.0f, 1.0f, 0.0f, -inputs.frame.waterY );
        m_collisionVisualizer.SetClipPlane( 0.0f, 1.0f, 0.0f, -inputs.frame.waterY );
        if ( inputs.collisionStateColorsVisible )
        {
            // Pass contract: collision-state solids are vertex-colored and do
            // not sample textures.
            ClearAllRenderTextureSlots( renderTextures );
            if ( HasCollisionVisualizerFrameView( inputs.frame ) )
            {
                const CollisionVisualizerFrameView frameView = BuildCollisionVisualizerFrameView( inputs.frame );
                m_collisionVisualizer.SetAlphaOverride( inputs.collisionVisualizerAlphaOverride );
                m_collisionVisualizer.Render( RenderAssets( inputs.frame ),
                                              RenderResources( inputs.frame ),
                                              RenderGeometry( inputs.frame ),
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
                renderTextures,
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
        PrimitiveBatchRendererForFrame( inputs.frame ).SetClipPlane( 0.0f, 1.0f, 0.0f, 1.0e9f );
        m_collisionVisualizer.SetClipPlane( 0.0f, 1.0f, 0.0f, 1.0e9f );
        PROFILE_GPU_END( inputs.frame.renderGpuTiming, "Frame/Render/Reflection/Balls" );

        m_resources.target->Unbind();
        renderFrame.SetViewport( 0, 0, inputs.frame.windowWidth, inputs.frame.windowHeight );
        output.reflectionTextureHandle = m_resources.target->GetColorTextureHandle();
        output.reflectionSampleViewProjection = inputs.frame.reflectionViewProjection;
    }
    return output;
}


void ObjectPass::Render( const ObjectPassInputs& inputs )
{
    const bool transparentPass = inputs.mode == ObjectPassMode::Transparent;
    const char* passName = transparentPass ? "Frame/Render/TransparentBalls" : "Frame/Render/Balls";
    const uint32_t passHash =
        transparentPass ? HashStr( "Frame/Render/TransparentBalls" ) : HashStr( "Frame/Render/Balls" );
#if defined( SKULLBONEZ_PROFILE_ENABLED )
    SkullbonezCore::Rendering::RenderGpuTimingScope profileScope( inputs.frame.renderGpuTiming, passName, passHash );
#endif
    Rendering::DrawCallTraceScope drawTraceScope( RenderDiagnostics( inputs.frame ), passName, passHash );
    Rendering::Dx12TextureOwner& renderTextures = RenderTextureOwner( inputs.frame );

    if ( inputs.collisionStateColorsVisible )
    {
        // Pass contract: collision-state solids are vertex-colored and do not
        // sample textures.
        ClearAllRenderTextureSlots( renderTextures );
        if ( HasCollisionVisualizerFrameView( inputs.frame ) )
        {
            const CollisionVisualizerFrameView frameView = BuildCollisionVisualizerFrameView( inputs.frame );
            m_collisionVisualizer.SetAlphaOverride( inputs.collisionVisualizerAlphaOverride );
            m_collisionVisualizer.Render( RenderAssets( inputs.frame ),
                                          RenderResources( inputs.frame ),
                                          RenderGeometry( inputs.frame ),
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
            renderTextures,
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

    PROFILE_GPU_BEGIN( inputs.frame.renderGpuTiming, "Frame/Render/Terrain" );
    DRAW_CALL_TRACE_SCOPE( RenderDiagnostics( inputs.frame ), "Frame/Render/Terrain" );
    // Pass contract: terrain reads ground albedo from t0, the broad shadow map
    // from t3, and the tight object-shadow map from t5. The material table stays
    // at t4 for instanced object draws and is never repurposed here.
    Rendering::Dx12TextureOwner& renderTextures = RenderTextureOwner( inputs.frame );
    ClearRenderTextureSlotsExcept(
        renderTextures,
        RENDER_TEXTURE_SLOT_0 | ( inputs.shadow && inputs.shadow->valid ? RENDER_TEXTURE_SLOT_3 : 0u ) |
            ( inputs.detailShadow && inputs.detailShadow->valid ? RENDER_TEXTURE_SLOT_5 : 0u ) );
    if ( SelectRenderTexture( inputs.frame, TEXTURE_GROUND, "Frame/Render/Terrain" ) )
    {
        m_terrain.Get()->Render( inputs.frame.baseView,
                                 inputs.frame.projection,
                                 renderTextures,
                                 inputs.frame.lightPosition,
                                 inputs.clipPlane,
                                 TERRAIN_RASTER,
                                 inputs.cinematic,
                                 inputs.shadow,
                                 inputs.detailShadow );
    }
    PROFILE_GPU_END( inputs.frame.renderGpuTiming, "Frame/Render/Terrain" );
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

    PROFILE_GPU_BEGIN( inputs.frame.renderGpuTiming, "Frame/Render/Water" );
    DRAW_CALL_TRACE_SCOPE( RenderDiagnostics( inputs.frame ), "Frame/Render/Water" );
    // Pass contract: water samples only the reflection texture in slot 1.
    Rendering::Dx12TextureOwner& renderTextures = RenderTextureOwner( inputs.frame );
    ClearRenderTextureSlotsExcept( renderTextures, RENDER_TEXTURE_SLOT_1 );
    float waterTime = inputs.freezeTime ? inputs.frozenTime : inputs.liveWaterTime;
    m_debugInfo.rendered = true;
    m_debugInfo.waterTime = waterTime;
    SkullbonezCore::Environment::WaterReflectionInput reflectionInput;
    reflectionInput.sampleViewProjection = inputs.reflection.reflectionSampleViewProjection;
    reflectionInput.textureHandle = inputs.reflection.reflectionTextureHandle;
    reflectionInput.noReflection = inputs.noReflection;
    reflectionInput.raytraced = inputs.reflection.usedDxr;

    m_world.RenderFluid( inputs.frame.baseView,
                         inputs.frame.projection,
                         inputs.frame.eye,
                         renderTextures,
                         reflectionInput,
                         WATER_RASTER,
                         waterTime,
                         inputs.flatWater,
                         inputs.frame.cinematicEnabled,
                         inputs.cinematic );
    PROFILE_GPU_END( inputs.frame.renderGpuTiming, "Frame/Render/Water" );
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
        PROFILE_GPU_BEGIN( inputs.frame.renderGpuTiming, "Frame/Render/DebugOverlay" );
    }
    DRAW_CALL_TRACE_SCOPE( RenderDiagnostics( inputs.frame ), "Frame/Render/DebugOverlay" );
    const DebugOverlaySnapshot& snapshot = inputs.snapshot;
    if ( snapshot.broadphaseOverlayVisible )
    {
        if ( detailMarkers )
        {
            PROFILE_GPU_BEGIN( inputs.frame.renderGpuTiming, "Frame/Render/DebugOverlay/Broadphase" );
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
            PROFILE_GPU_END( inputs.frame.renderGpuTiming, "Frame/Render/DebugOverlay/Broadphase" );
        }
    }

    if ( !snapshot.worldExtensionDebugLines.empty() )
    {
        if ( detailMarkers )
        {
            PROFILE_GPU_BEGIN( inputs.frame.renderGpuTiming, "Frame/Render/DebugOverlay/WorldExtension" );
        }
        DRAW_CALL_TRACE_SCOPE( RenderDiagnostics( inputs.frame ), "WorldExtension" );
        if ( RenderDiagnostics( inputs.frame ).GetCapabilities().supportsDebugLines )
        {
            RenderCommands( inputs.frame )
                .DrawLinesColored( snapshot.worldExtensionDebugLines, inputs.frame.viewProjection, DEBUG_LINE_RASTER );
        }
        if ( detailMarkers )
        {
            PROFILE_GPU_END( inputs.frame.renderGpuTiming, "Frame/Render/DebugOverlay/WorldExtension" );
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
                                        RenderGeometry( inputs.frame ),
                                        RenderCommands( inputs.frame ) );

    if ( snapshot.physicsDebugFlags != PHYSICS_DEBUG_NONE )
    {
        if ( detailMarkers )
        {
            PROFILE_GPU_BEGIN( inputs.frame.renderGpuTiming, "Frame/Render/DebugOverlay/PhysicsDebug" );
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
            PROFILE_GPU_END( inputs.frame.renderGpuTiming, "Frame/Render/DebugOverlay/PhysicsDebug" );
        }
    }
    if ( detailMarkers )
    {
        PROFILE_GPU_END( inputs.frame.renderGpuTiming, "Frame/Render/DebugOverlay" );
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
    if ( !snapshot.worldExtensionDebugLines.empty() )
    {
        return true;
    }
    if ( snapshot.physicsDebugFlags != PHYSICS_DEBUG_NONE )
    {
        return true;
    }

    return snapshot.editorOverlayWorkVisible;
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
        PROFILE_GPU_BEGIN( frame.renderGpuTiming, "Frame/Render/VolumetricLight" );
    }
    DRAW_CALL_TRACE_SCOPE( RenderDiagnostics( frame ), "Frame/Render/VolumetricLight" );
    Rendering::Dx12GeometryOwner& renderGeometry = RenderCommands( frame );
    Rendering::Dx12TextureOwner& renderTextures = RenderTextureOwner( frame );
    Rendering::Dx12FrameOwner& renderFrame = RenderFrameOwner( frame );
    Rendering::Dx12GraphTransientPool& renderGraph = RenderGraphOwner( frame );
    const bool useGraphOutput = graphOutput && graphOutput->IsValid() && graphOutput->renderTarget;
    if ( !useGraphOutput )
    {
        if ( detailMarkers )
        {
            PROFILE_GPU_END( frame.renderGpuTiming, "Frame/Render/VolumetricLight" );
        }
        return false;
    }
    renderGraph.BeginGraphTextureRenderTarget( *graphOutput, "VolumetricLightPass" );
    renderFrame.SetViewport( 0, 0, graphOutput->width, graphOutput->height );

    // This is another screen-space effect, so depth testing and blending are
    // disabled while the full-screen quad is generated.
    {
        if ( detailMarkers )
        {
            PROFILE_GPU_BEGIN( frame.renderGpuTiming, "Frame/Render/VolumetricLight/Draw" );
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
        BindRenderTextureSlots( renderTextures,
                                m_sceneResources.hdrTarget->GetColorTextureHandle(),
                                m_sceneResources.hdrTarget->GetDepthTextureHandle(),
                                0,
                                0 );
        DrawFullscreenQuad( renderGeometry, m_fullscreenResources.quadVB, FULLSCREEN_OPAQUE_RASTER );
        if ( detailMarkers )
        {
            PROFILE_GPU_END( frame.renderGpuTiming, "Frame/Render/VolumetricLight/Draw" );
        }
    }

    renderGraph.EndGraphTextureRenderTarget( *graphOutput, "VolumetricLightPass" );
    renderFrame.SetViewport( 0, 0, frame.windowWidth, frame.windowHeight );
    if ( detailMarkers )
    {
        PROFILE_GPU_END( frame.renderGpuTiming, "Frame/Render/VolumetricLight" );
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
        PROFILE_GPU_BEGIN( frame.renderGpuTiming, "Frame/Render/Tonemap" );
    }
    DRAW_CALL_TRACE_SCOPE( RenderDiagnostics( frame ), "Frame/Render/Tonemap" );
    if ( !sceneAlreadyUnbound )
    {
        m_sceneResources.hdrTarget->Unbind();
    }
    Rendering::Dx12GeometryOwner& renderGeometry = RenderCommands( frame );
    Rendering::Dx12TextureOwner& renderTextures = RenderTextureOwner( frame );
    RenderFrameOwner( frame ).SetViewport( 0, 0, frame.windowWidth, frame.windowHeight );

    // Concept: "resolve" means "turn our off-screen cinematic render target
    // into the final image on the window." This is where the HDR scene becomes
    // normal display color and where bloom/fog/rays are layered in.
    {
        if ( detailMarkers )
        {
            PROFILE_GPU_BEGIN( frame.renderGpuTiming, "Frame/Render/Tonemap/Draw" );
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
        BindRenderTextureSlots( renderTextures,
                                m_sceneResources.hdrTarget->GetColorTextureHandle(),
                                m_sceneResources.hdrTarget->GetDepthTextureHandle(),
                                volumetricTexture,
                                0 );
        DrawFullscreenQuad( renderGeometry, m_fullscreenResources.quadVB, FULLSCREEN_OPAQUE_RASTER );
        if ( detailMarkers )
        {
            PROFILE_GPU_END( frame.renderGpuTiming, "Frame/Render/Tonemap/Draw" );
        }
    }

    if ( detailMarkers )
    {
        PROFILE_GPU_END( frame.renderGpuTiming, "Frame/Render/Tonemap" );
    }
}
