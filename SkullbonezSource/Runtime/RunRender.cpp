/*
File: SkullbonezSource/Runtime/RunRender.cpp
Purpose:
  Coordinates render passes for the active scene.

Mental model:
  Renderer-facing code builds one frame context and runs named passes in
  the same order the image is produced.

Glossary:
  Render pass: Named slice of RuntimeRenderer::RenderFrame() with explicit
  inputs, outputs, and resource ownership.
  DXR (DirectX Raytracing): DX12 API used here for optional raytraced water
  reflection dispatch.
  Descriptor: Small binding record that tells a renderer how to interpret a
  resource.
  Back buffer: Swap-chain image that will be presented to the window.
  HDR (High Dynamic Range): Floating-point scene color that preserves bright
  lighting until the tonemap pass resolves it to display color.
  FBO (Framebuffer Object): Engine-neutral off-screen render target wrapper.
  BLAS (Bottom-Level Acceleration Structure): Raytracing spatial index for one
  mesh or procedural object owned by the DX12 backend.
  TLAS (Top-Level Acceleration Structure): Raytracing scene-instance table built
  before reflection rays are dispatched.

Invariants:
  - RuntimeRenderer::RenderFrame() owns pass order. Pass classes may bind targets and
    restore local render state, but they must not present or advance the frame.
  - Pass resource reset hooks run while the renderer backend is alive, because
    framebuffers, shaders, and dynamic vertex buffers can own backend objects.
  - Pass input/output structs borrow data for one frame only. Do not cache
    pointers returned from ShadowPassOutput or ReflectionPassOutput consumers.

Related:
  - SkullbonezSource/Runtime/Render/RuntimeRenderPasses.h declares pass contracts.
  - SkullbonezSource/Runtime/Run.h owns the runtime state borrowed by RuntimeRenderHost.
  - SkullbonezSource/Rendering/RenderPipeline.h owns executed frame graph diagnostics.
  - Agentic/Reference/comment-style-guide.md
*/
#include "RunInternal.h"
#include "RuntimeTuning.h"
#include "../Rendering/RenderGraph.h"
#include "../Rendering/RenderPipeline.h"

#include <stdexcept>

using namespace SkullbonezCore::Basics;
using namespace SkullbonezCore::Math::CollisionDetection;
using namespace SkullbonezCore::Math::Orientation;
using namespace SkullbonezCore::Math::Transformation;
using namespace SkullbonezCore::Physics;
using namespace SkullbonezCore::Basics::RunInternal;

namespace
{
struct CinematicPostGraphCallbackData
{
    VolumetricPass* volumetricPass = nullptr;
    TonemapPass* tonemapPass = nullptr;
    const RenderFrameContext* frame = nullptr;
    bool volumetricRendered = false;
};

struct DebugOverlayGraphCallbackData
{
    DebugOverlayPass* debugOverlayPass = nullptr;
    const RenderFrameContext* frame = nullptr;
};

struct SceneTargetGraphCallbackData
{
    SceneTargetPass* sceneTargetPass = nullptr;
    SkyPass* skyPass = nullptr;
    const RenderFrameContext* frame = nullptr;
};

struct SkyboxGraphCallbackData
{
    SkyPass* skyPass = nullptr;
    const RenderFrameContext* frame = nullptr;
};

struct UiTextGraphCallbackData
{
    UiTextPass* uiTextPass = nullptr;
    SkullbonezCore::Rendering::IRenderDiagnostics* renderDiagnostics = nullptr;
    SkullbonezCore::Rendering::IRenderRayTracing* renderRayTracing = nullptr;
    double secondsPerFrame = 0.0;
};

struct TornadoVisualGraphCallbackData
{
    TornadoVisualPass* tornadoVisualPass = nullptr;
    const RenderFrameContext* frame = nullptr;
    bool rendered = false;
};

void ExecuteTornadoVisualGraphCallback( const SkullbonezCore::Rendering::RenderGraphPassContext& /*context*/,
                                        void* userData )
{
    auto* data = static_cast<TornadoVisualGraphCallbackData*>( userData );
    if ( !data || !data->tornadoVisualPass || !data->frame )
    {
        throw std::runtime_error( "TornadoVisualPass graph callback missing execution data" );
    }
    data->rendered = data->tornadoVisualPass->Render( { *data->frame } );
}

void ExecuteDebugOverlayGraphCallback( const SkullbonezCore::Rendering::RenderGraphPassContext& /*context*/,
                                       void* userData )
{
    auto* data = static_cast<DebugOverlayGraphCallbackData*>( userData );
    if ( !data || !data->debugOverlayPass || !data->frame )
    {
        throw std::runtime_error( "DebugOverlayPass graph callback missing execution data" );
    }
    data->debugOverlayPass->Render( { *data->frame } );
}

void ExecuteSceneTargetGraphCallback( const SkullbonezCore::Rendering::RenderGraphPassContext& /*context*/,
                                      void* userData )
{
    auto* data = static_cast<SceneTargetGraphCallbackData*>( userData );
    if ( !data || !data->sceneTargetPass || !data->skyPass || !data->frame )
    {
        throw std::runtime_error( "CinematicSceneBegin graph callback missing execution data" );
    }
    data->sceneTargetPass->Begin( *data->frame, *data->skyPass );
}

void ExecuteSkyboxGraphCallback( const SkullbonezCore::Rendering::RenderGraphPassContext& /*context*/, void* userData )
{
    auto* data = static_cast<SkyboxGraphCallbackData*>( userData );
    if ( !data || !data->skyPass || !data->frame )
    {
        throw std::runtime_error( "SkyboxPass graph callback missing execution data" );
    }
    data->skyPass->Render( *data->frame, data->frame->baseView, SkyPassMode::CubemapOnly );
}

void ExecuteUiTextGraphCallback( const SkullbonezCore::Rendering::RenderGraphPassContext& /*context*/, void* userData )
{
    auto* data = static_cast<UiTextGraphCallbackData*>( userData );
    if ( !data || !data->uiTextPass || !data->renderDiagnostics )
    {
        throw std::runtime_error( "UiTextPass graph callback missing execution data" );
    }
    data->uiTextPass->Render( { *data->renderDiagnostics, data->renderRayTracing, data->secondsPerFrame } );
}

void ExecuteVolumetricGraphCallback( const SkullbonezCore::Rendering::RenderGraphPassContext& /*context*/,
                                     void* userData )
{
    auto* data = static_cast<CinematicPostGraphCallbackData*>( userData );
    if ( !data || !data->volumetricPass || !data->frame )
    {
        throw std::runtime_error( "VolumetricLightPass graph callback missing execution data" );
    }
    data->volumetricRendered = data->volumetricPass->Render( *data->frame );
}

void ExecuteTonemapGraphCallback( const SkullbonezCore::Rendering::RenderGraphPassContext& /*context*/, void* userData )
{
    auto* data = static_cast<CinematicPostGraphCallbackData*>( userData );
    if ( !data || !data->tonemapPass || !data->frame )
    {
        throw std::runtime_error( "ToneMapPass graph callback missing execution data" );
    }
    data->tonemapPass->Render( *data->frame, data->volumetricRendered, data->volumetricRendered );
}

RuntimeRenderInputs BuildRuntimeRenderInputs( RunSubsystemState& systems,
                                              SkullbonezCore::GameObjects::GameModelCollection& models,
                                              SkullbonezCore::Environment::WorldEnvironment& world,
                                              SkullbonezCore::UI::InGameUI& ui,
                                              SkullbonezCore::Rendering::IRenderCommandContext& renderCommands,
                                              SkullbonezCore::Rendering::IRenderResourceFactory& renderResources,
                                              SkullbonezCore::Rendering::IRenderDiagnostics& renderDiagnostics,
                                              SkullbonezCore::Rendering::IRenderRayTracing* renderRayTracing,
                                              bool renderReady )
{
    return RuntimeRenderInputs{ RuntimeRenderServices{ *systems.textures,
                                                       models,
                                                       world,
                                                       systems.terrain.get(),
                                                       *systems.cameras,
                                                       *systems.window,
                                                       ui,
                                                       systems.skyBox,
                                                       renderCommands,
                                                       renderResources,
                                                       renderDiagnostics,
                                                       renderRayTracing,
                                                       renderReady } };
}
} // namespace

bool RuntimeRenderer::ExecuteSkyboxThroughRenderGraph( const RenderFrameContext& frame )
{
    Rendering::RenderGraph graph;
    const Rendering::RenderGraphResourceHandle backbuffer =
        graph.AddExternalResource( "SwapchainBackbuffer", Rendering::RenderGraphResourceAccess::RenderTarget );

    const uint32_t skyboxPass = graph.AddPass( "SkyboxPass",
                                               Rendering::RenderGraphQueueType::Graphics,
                                               Rendering::RenderGraphBarrierPolicy::HandoffValidated );
    graph.AddWrite( skyboxPass, backbuffer, Rendering::RenderGraphResourceAccess::RenderTarget );

    SkyboxGraphCallbackData callbackData;
    callbackData.skyPass = &m_skyPass;
    callbackData.frame = &frame;
    graph.SetPassCallback( skyboxPass, ExecuteSkyboxGraphCallback, &callbackData, true, "Frame/Render/Skybox" );

    // Invariant: the ordinary skybox still draws through SkyPass; the graph now
    // owns the scheduling point and resource declaration before live execution.
    graph.Compile();
    graph.ExecuteCallbacks( Rendering::RenderGraphCallbackExecutionMode::DryRun );
    const Rendering::RenderGraphCallbackExecutionResult executed =
        graph.ExecuteCallbacks( Rendering::RenderGraphCallbackExecutionMode::Execute );
    return executed.executedPassCount == 1u;
}


bool RuntimeRenderer::ExecuteSceneTargetBeginThroughRenderGraph( const RenderFrameContext& frame )
{
    Rendering::RenderGraph graph;
    const Rendering::RenderGraphResourceHandle sceneColor =
        graph.AddExternalResource( "CinematicSceneColor", Rendering::RenderGraphResourceAccess::PixelShaderResource );
    // Handoff: FramebufferDX12 tracks whether depth starts this pass as a fresh
    // DepthWrite texture or a shader-readable texture from the previous post
    // chain. Keep the initial graph state unknown until the graph owns FBO state.
    const Rendering::RenderGraphResourceHandle sceneDepth =
        graph.AddExternalResource( "CinematicSceneDepth", Rendering::RenderGraphResourceAccess::Unknown );

    const uint32_t sceneBeginPass = graph.AddPass( "CinematicSceneBegin",
                                                   Rendering::RenderGraphQueueType::Graphics,
                                                   Rendering::RenderGraphBarrierPolicy::HandoffValidated );
    graph.AddWrite( sceneBeginPass, sceneColor, Rendering::RenderGraphResourceAccess::RenderTarget );
    graph.AddWrite( sceneBeginPass, sceneDepth, Rendering::RenderGraphResourceAccess::DepthWrite );

    SceneTargetGraphCallbackData callbackData;
    callbackData.sceneTargetPass = &m_sceneTargetPass;
    callbackData.skyPass = &m_skyPass;
    callbackData.frame = &frame;
    graph.SetPassCallback( sceneBeginPass,
                           ExecuteSceneTargetGraphCallback,
                           &callbackData,
                           true,
                           "Frame/Render/CinematicSceneBegin" );

    // Invariant: cinematic scene targets rest as shader resources after the
    // post chain consumes them. SceneTargetPass::Begin still performs the live
    // bind/clear handoff, while this graph records the transition intent and
    // owns the callback scheduling point.
    graph.Compile();
    graph.ExecuteCallbacks( Rendering::RenderGraphCallbackExecutionMode::DryRun );
    const Rendering::RenderGraphCallbackExecutionResult executed =
        graph.ExecuteCallbacks( Rendering::RenderGraphCallbackExecutionMode::Execute );
    return executed.executedPassCount == 1u;
}


RuntimeRenderer::GraphPassResult
RuntimeRenderer::ExecuteTornadoVisualThroughRenderGraph( const RenderFrameContext& frame, bool useCinematicTarget )
{
    Rendering::RenderGraph graph;
    const Rendering::RenderGraphResourceHandle colorTarget =
        graph.AddExternalResource( useCinematicTarget ? "CinematicSceneColor" : "SwapchainBackbuffer",
                                   Rendering::RenderGraphResourceAccess::RenderTarget );
    const Rendering::RenderGraphResourceHandle depthTarget =
        graph.AddExternalResource( useCinematicTarget ? "CinematicSceneDepth" : "MainDepthStencil",
                                   Rendering::RenderGraphResourceAccess::DepthWrite );

    const uint32_t tornadoPass = graph.AddPass( "TornadoVisualPass",
                                                Rendering::RenderGraphQueueType::Graphics,
                                                Rendering::RenderGraphBarrierPolicy::HandoffValidated );
    graph.AddWrite( tornadoPass, colorTarget, Rendering::RenderGraphResourceAccess::RenderTarget );
    graph.AddWrite( tornadoPass, depthTarget, Rendering::RenderGraphResourceAccess::DepthWrite );

    TornadoVisualGraphCallbackData callbackData;
    callbackData.tornadoVisualPass = &m_tornadoVisualPass;
    callbackData.frame = &frame;
    graph.SetPassCallback( tornadoPass,
                           ExecuteTornadoVisualGraphCallback,
                           &callbackData,
                           true,
                           "Frame/Render/TornadoVisual" );

    // Invariant: TornadoVisualPass may skip drawing after rebuilding its
    // transient vertex list. Callback ownership is still true when the graph
    // schedules that decision point in frame order.
    graph.Compile();
    graph.ExecuteCallbacks( Rendering::RenderGraphCallbackExecutionMode::DryRun );
    const Rendering::RenderGraphCallbackExecutionResult executed =
        graph.ExecuteCallbacks( Rendering::RenderGraphCallbackExecutionMode::Execute );

    GraphPassResult result;
    result.rendered = callbackData.rendered;
    result.callbackOwned = executed.executedPassCount == 1u;
    return result;
}


bool RuntimeRenderer::ExecuteDebugOverlayThroughRenderGraph( const RenderFrameContext& frame, bool useCinematicTarget )
{
    Rendering::RenderGraph graph;
    const Rendering::RenderGraphResourceHandle colorTarget =
        graph.AddExternalResource( useCinematicTarget ? "CinematicSceneColor" : "SwapchainBackbuffer",
                                   Rendering::RenderGraphResourceAccess::RenderTarget );
    const Rendering::RenderGraphResourceHandle depthTarget =
        graph.AddExternalResource( useCinematicTarget ? "CinematicSceneDepth" : "MainDepthStencil",
                                   Rendering::RenderGraphResourceAccess::DepthWrite );

    const uint32_t debugPass = graph.AddPass( "DebugOverlayPass",
                                              Rendering::RenderGraphQueueType::Graphics,
                                              Rendering::RenderGraphBarrierPolicy::HandoffValidated );
    graph.AddWrite( debugPass, colorTarget, Rendering::RenderGraphResourceAccess::RenderTarget );
    graph.AddWrite( debugPass, depthTarget, Rendering::RenderGraphResourceAccess::DepthWrite );

    DebugOverlayGraphCallbackData callbackData;
    callbackData.debugOverlayPass = &m_debugOverlayPass;
    callbackData.frame = &frame;
    graph.SetPassCallback( debugPass,
                           ExecuteDebugOverlayGraphCallback,
                           &callbackData,
                           true,
                           "Frame/Render/DebugOverlay" );

    // Invariant: debug overlays are optional inside the pass body, but the pass
    // scheduling itself is now graph-owned every frame so direct runtime calls
    // cannot creep back beside post-processing callbacks.
    graph.Compile();
    graph.ExecuteCallbacks( Rendering::RenderGraphCallbackExecutionMode::DryRun );
    const Rendering::RenderGraphCallbackExecutionResult executed =
        graph.ExecuteCallbacks( Rendering::RenderGraphCallbackExecutionMode::Execute );
    return executed.executedPassCount == 1u;
}


RuntimeRenderer::CinematicPostGraphResult
RuntimeRenderer::ExecuteCinematicPostThroughRenderGraph( const RenderFrameContext& frame )
{
    Rendering::RenderGraph graph;
    const Rendering::RenderGraphResourceHandle sceneColor =
        graph.AddExternalResource( "CinematicSceneColor", Rendering::RenderGraphResourceAccess::PixelShaderResource );
    const Rendering::RenderGraphResourceHandle sceneDepth =
        graph.AddExternalResource( "CinematicSceneDepth", Rendering::RenderGraphResourceAccess::PixelShaderResource );
    const Rendering::RenderGraphResourceHandle backbuffer =
        graph.AddExternalResource( "SwapchainBackbuffer", Rendering::RenderGraphResourceAccess::RenderTarget );
    Rendering::RenderGraphResourceHandle volumetricLight;
    const bool volumetricDeclared = m_volumetricPass.CanRender( frame );
    uint32_t expectedCallbacks = 1u;
    uint32_t volumetricPass = 0u;

    // Invariant: framebuffer color targets rest in shader-resource state when
    // they are not actively bound. VolumetricPass::Render transitions the
    // texture to render-target state through FramebufferDX12::Bind().
    if ( volumetricDeclared )
    {
        volumetricLight =
            graph.AddExternalResource( "VolumetricLight", Rendering::RenderGraphResourceAccess::PixelShaderResource );

        volumetricPass = graph.AddPass( "VolumetricLightPass",
                                        Rendering::RenderGraphQueueType::Graphics,
                                        Rendering::RenderGraphBarrierPolicy::HandoffValidated );
        graph.AddRead( volumetricPass, sceneColor, Rendering::RenderGraphResourceAccess::PixelShaderResource );
        graph.AddRead( volumetricPass, sceneDepth, Rendering::RenderGraphResourceAccess::PixelShaderResource );
        graph.AddWrite( volumetricPass, volumetricLight, Rendering::RenderGraphResourceAccess::RenderTarget );
        ++expectedCallbacks;
    }

    const uint32_t tonemapPass = graph.AddPass( "ToneMapPass",
                                                Rendering::RenderGraphQueueType::Graphics,
                                                Rendering::RenderGraphBarrierPolicy::HandoffValidated );
    graph.AddRead( tonemapPass, sceneColor, Rendering::RenderGraphResourceAccess::PixelShaderResource );
    graph.AddRead( tonemapPass, sceneDepth, Rendering::RenderGraphResourceAccess::PixelShaderResource );
    if ( volumetricDeclared )
    {
        graph.AddRead( tonemapPass, volumetricLight, Rendering::RenderGraphResourceAccess::PixelShaderResource );
    }
    graph.AddWrite( tonemapPass, backbuffer, Rendering::RenderGraphResourceAccess::RenderTarget );

    CinematicPostGraphCallbackData callbackData;
    callbackData.volumetricPass = &m_volumetricPass;
    callbackData.tonemapPass = &m_tonemapPass;
    callbackData.frame = &frame;
    if ( volumetricDeclared )
    {
        graph.SetPassCallback( volumetricPass,
                               ExecuteVolumetricGraphCallback,
                               &callbackData,
                               true,
                               "Frame/Render/VolumetricLight" );
    }
    graph.SetPassCallback( tonemapPass, ExecuteTonemapGraphCallback, &callbackData, true, "Frame/Render/Tonemap" );

    // Invariant: dry-run executes no draw code. It proves the callback-owned
    // post passes have resource declarations before live callbacks record
    // commands, and the execute path records them in graph order.
    graph.Compile();
    graph.ExecuteCallbacks( Rendering::RenderGraphCallbackExecutionMode::DryRun );
    const Rendering::RenderGraphCallbackExecutionResult executed =
        graph.ExecuteCallbacks( Rendering::RenderGraphCallbackExecutionMode::Execute );

    CinematicPostGraphResult result;
    result.volumetricReady = volumetricDeclared && callbackData.volumetricRendered;
    result.volumetricCallbackOwned = result.volumetricReady;
    result.tonemapCallbackOwned = executed.executedPassCount == expectedCallbacks;
    return result;
}


bool RuntimeRenderer::ExecuteUiTextThroughRenderGraph( Rendering::IRenderDiagnostics& renderDiagnostics,
                                                       Rendering::IRenderRayTracing* renderRayTracing,
                                                       double secondsPerFrame )
{
    Rendering::RenderGraph graph;
    const Rendering::RenderGraphResourceHandle backbuffer =
        graph.AddExternalResource( "SwapchainBackbuffer", Rendering::RenderGraphResourceAccess::RenderTarget );

    const uint32_t uiTextPass = graph.AddPass( "UiTextPass",
                                               Rendering::RenderGraphQueueType::Graphics,
                                               Rendering::RenderGraphBarrierPolicy::HandoffValidated );
    graph.AddWrite( uiTextPass, backbuffer, Rendering::RenderGraphResourceAccess::RenderTarget );

    UiTextGraphCallbackData callbackData;
    callbackData.uiTextPass = &m_uiTextPass;
    callbackData.renderDiagnostics = &renderDiagnostics;
    callbackData.renderRayTracing = renderRayTracing;
    callbackData.secondsPerFrame = secondsPerFrame;
    graph.SetPassCallback( uiTextPass, ExecuteUiTextGraphCallback, &callbackData, true, "Frame/UI" );

    // Invariant: UI/text always lands on the presentable backbuffer. Text-only
    // mode skips world rendering before this point but still uses this callback
    // path so the late overlay pass has one scheduling owner.
    graph.Compile();
    graph.ExecuteCallbacks( Rendering::RenderGraphCallbackExecutionMode::DryRun );
    const Rendering::RenderGraphCallbackExecutionResult executed =
        graph.ExecuteCallbacks( Rendering::RenderGraphCallbackExecutionMode::Execute );
    return executed.executedPassCount == 1u;
}


RenderFrameContext RuntimeRenderer::BuildRenderFrameContext( const RuntimeRenderInputs& renderInputs,
                                                             bool cinematicRender,
                                                             const CinematicRenderConfig& renderConfig ) const
{
    const RuntimeRenderServices& services = renderInputs.services;
    RenderFrameContext frame;
    frame.cinematicEnabled = cinematicRender;
    frame.cinematic = cinematicRender ? &renderConfig : nullptr;
    frame.scene = &services.models;
    frame.renderCommands = &services.renderCommands;
    frame.renderDiagnostics = &services.renderDiagnostics;
    frame.renderRayTracing = services.renderRayTracing;
    frame.windowWidth = (std::max)( 1, m_host.WindowScreenWidth() );
    frame.windowHeight = (std::max)( 1, m_host.WindowScreenHeight() );

    // Ordinary and cinematic rendering both use a directional sun (w = 0).
    // Keeping one sun-vector contract makes direct BRDF lighting and shadow-map
    // visibility block the same light contribution.
    if ( frame.cinematicEnabled )
    {
        const Vector3 sunDirection = CinematicSkySunDirection( renderConfig );
        frame.lightPosition[0] = sunDirection.x;
        frame.lightPosition[1] = sunDirection.y;
        frame.lightPosition[2] = sunDirection.z;
        frame.lightPosition[3] = 0.0f;
    }

    // Invariant: build the pass context after SetCamera(). During camera
    // transitions, the selected camera and render camera can differ; all passes
    // must consume the interpolated render camera so reflection, sky, and water
    // sample the same view.
    frame.baseView = services.cameras.GetViewMatrix();
    frame.projection = services.window.GetProjectionMatrix();
    frame.viewProjection = frame.projection * frame.baseView;
    frame.eye = services.cameras.GetRenderCameraTranslation();
    frame.viewCenter = services.cameras.GetRenderCameraView();
    frame.up = services.cameras.GetRenderCameraUp();
    frame.waterY = services.world.GetFluidSurfaceHeight();
    frame.reflectionEye = Vector3( frame.eye.x, 2.0f * frame.waterY - frame.eye.y, frame.eye.z );
    frame.reflectionCenter =
        Vector3( frame.viewCenter.x, 2.0f * frame.waterY - frame.viewCenter.y, frame.viewCenter.z );
    frame.reflectionUp = Vector3( frame.up.x, -frame.up.y, frame.up.z );
    frame.reflectionView = Matrix4::LookAt( frame.reflectionEye, frame.reflectionCenter, frame.reflectionUp );
    frame.reflectionViewProjection = frame.projection * frame.reflectionView;
    return frame;
}


RenderResourceContext RuntimeRenderer::BuildRenderResourceContext( const RuntimeRenderInputs& renderInputs,
                                                                   bool cinematicRender ) const
{
    const RuntimeRenderServices& services = renderInputs.services;
    return RenderResourceContext{ cinematicRender,
                                  services.renderResources,
                                  (std::max)( 1, m_host.WindowScreenWidth() ),
                                  (std::max)( 1, m_host.WindowScreenHeight() ) };
}


RuntimeRenderer::RuntimeRenderer( RuntimeRenderHost& host )
    : m_host( host ), m_fullscreenQuadPass( host ), m_skyPass( host ), m_sceneTargetPass( host ), m_shadowPass( host ),
      m_reflectionPass( host ), m_objectPass( host ), m_terrainPass( host ), m_waterPass( host ),
      m_tornadoVisualPass( host ), m_debugOverlayPass( host ), m_volumetricPass( host ), m_tonemapPass( host ),
      m_uiTextPass( host )
{
}


void RuntimeRenderer::EnsureFrameResources( const RenderResourceContext& resources )
{
    if ( resources.cinematicEnabled )
    {
        // Lifetime: cinematic resources are lazy. A window resize or backend
        // rebuild drops them; the next cinematic frame recreates the targets and
        // shader objects with the current window dimensions.
        m_fullscreenQuadPass.EnsureGpuResources( resources );
        m_skyPass.EnsureGpuResources( resources );
        m_sceneTargetPass.EnsureGpuResources( resources );
        m_volumetricPass.EnsureGpuResources( resources );
        m_tonemapPass.EnsureGpuResources( resources );
    }
}


void RuntimeRenderer::RenderFrame( const RuntimeRenderInputs& renderInputs )
{
    RuntimeRenderHost& host = m_host;
    const RuntimeRenderServices& services = renderInputs.services;
    m_uiTextRayTracing = services.renderRayTracing;
    const bool cinematicRender = host.IsCinematicRenderingEnabled();
    const CinematicRenderConfig& renderConfig = host.ActiveCinematicConfig();
    const OrdinaryRenderConfig& ordinaryRender = Cfg().ordinaryRender;
    CinematicRenderConfig ordinaryShadowConfig = renderConfig;
    ordinaryShadowConfig.shadowsEnabled = ordinaryRender.shadowsEnabled;
    ordinaryShadowConfig.shadowTerrainCasts = ordinaryRender.shadowTerrainCasts;
    ordinaryShadowConfig.shadowObjectsCast = ordinaryRender.shadowObjectsCast;
    ordinaryShadowConfig.shadowTerrainReceives = ordinaryRender.shadowTerrainReceives;
    ordinaryShadowConfig.shadowObjectsReceive = ordinaryRender.shadowObjectsReceive;
    ordinaryShadowConfig.shadowMapSize = ordinaryRender.shadowMapSize;
    ordinaryShadowConfig.shadowPcfRadius = ordinaryRender.shadowPcfRadius;
    ordinaryShadowConfig.shadowStrength = ordinaryRender.shadowStrength;
    ordinaryShadowConfig.shadowSoftness = ordinaryRender.shadowSoftness;
    ordinaryShadowConfig.shadowDepthBias = ordinaryRender.shadowDepthBias;
    ordinaryShadowConfig.shadowSlopeBias = ordinaryRender.shadowSlopeBias;
    ordinaryShadowConfig.shadowMaxDistance = ordinaryRender.shadowMaxDistance;
    const CinematicRenderConfig& activeShadowStyle = cinematicRender ? renderConfig : ordinaryShadowConfig;
    const bool shadowMapsEnabled = activeShadowStyle.shadowsEnabled && services.renderReady && !host.m_debug.isTextOnly;

    const RenderResourceContext resourceContext = BuildRenderResourceContext( renderInputs, cinematicRender );
    EnsureFrameResources( resourceContext );

    const bool useCinematicTarget = cinematicRender && m_sceneTargetPass.IsReady();
    if ( cinematicRender && !useCinematicTarget )
    {
        // If the cinematic target could not be created, fall back to the normal
        // backbuffer clear so the frame still renders instead of showing stale data.
        services.renderCommands.Clear( true, true );
    }

    // Build the shared pass contract once, after camera update and before any
    // pass can bind targets. All extracted passes consume this same frame view.
    RenderFrameContext frame = BuildRenderFrameContext( renderInputs, cinematicRender, renderConfig );

    PROFILE_BEGIN( "Frame/Render/PrepareModels" );
    host.m_cGameModelCollection.PrepareRenderStreams();
    PROFILE_END( "Frame/Render/PrepareModels" );

    // These passes currently borrow subsystem-owned mesh/material resources,
    // but keeping the ensure calls in the frame story gives future extraction
    // work an obvious place to move those GPU resources.
    m_objectPass.EnsureGpuResources( resourceContext );
    m_terrainPass.EnsureGpuResources( resourceContext );
    m_waterPass.EnsureGpuResources( resourceContext );
    m_tornadoVisualPass.EnsureGpuResources( resourceContext );
    m_debugOverlayPass.EnsureGpuResources( resourceContext );

    // Defer the first DX12 command-list open until after CPU-side model prep so
    // allocator waits do not block work that can overlap the previous frame.
    if ( !cinematicRender )
    {
        services.renderCommands.Clear( true, true );
    }

    const CinematicRenderConfig* activeCinematic = frame.cinematic;
    const CinematicRenderConfig* activeShadowConfig = shadowMapsEnabled ? &activeShadowStyle : nullptr;
    if ( activeShadowConfig )
    {
        m_shadowPass.EnsureGpuResources( resourceContext, *activeShadowConfig );
    }
    ShadowPassOutput shadowPass = m_shadowPass.Render( { frame, activeShadowConfig } );
    const Rendering::ShadowFrameData* terrainShadowFrame = shadowPass.terrainShadow;
    const Rendering::ShadowFrameData* objectShadowFrame = shadowPass.objectShadow;

    const bool collisionStateColorsVisible = host.m_debug.isCollisionVisualizer;
    const bool debugTransparentBodyPass =
        host.m_debug.isPhysicsDebugTransparent && host.m_debug.physicsDebugAlpha < 1.0f;
    const bool replayPredictionOverlayActive = host.m_replayRuntime.Prediction().enabled;
    const bool replayFocusFadeActive = !replayPredictionOverlayActive && !collisionStateColorsVisible &&
                                       !debugTransparentBodyPass && host.BuildReplayFocusModelMask();
    const std::vector<uint8_t>* replayFocusModelMask =
        replayFocusFadeActive ? &host.m_replayRuntime.FocusModelMask() : nullptr;
    const bool transparentBodyPass = debugTransparentBodyPass || replayFocusFadeActive;
    const float bodyRenderAlpha = debugTransparentBodyPass ? host.m_debug.physicsDebugAlpha : 1.0f;
    const float collisionVisualizerAlphaOverride = debugTransparentBodyPass ? bodyRenderAlpha : -1.0f;
    const bool waterModeOff = frame.cinematicEnabled && activeCinematic && activeCinematic->waterMode == 0;
    const bool waterVisibleThisFrame = !host.m_debug.isWaterHidden && !waterModeOff;
    const bool reflectionPassNeeded = waterVisibleThisFrame && !host.m_debug.isWaterNoReflect;

    // Invariant: sky and reflection both consume the interpolated render camera
    // from RenderFrameContext. Using the selected destination camera here would
    // stretch reflected geometry during camera transitions.
    bool skyboxCallbackOwned = false;
    if ( !cinematicRender )
    {
        PROFILE_GPU_BEGIN( "Frame/Render/Skybox" );
        {
            DRAW_CALL_TRACE_SCOPE( "Frame/Render/Skybox" );
            skyboxCallbackOwned = ExecuteSkyboxThroughRenderGraph( frame );
        }
        PROFILE_GPU_END( "Frame/Render/Skybox" );
    }

    ReflectionPassOutput reflection;
    reflection.reflectionSampleViewProjection = frame.reflectionViewProjection;
    if ( reflectionPassNeeded )
    {
        m_reflectionPass.EnsureGpuResources( resourceContext );
        reflection = m_reflectionPass.Render( { frame,
                                                activeCinematic,
                                                objectShadowFrame,
                                                collisionStateColorsVisible,
                                                debugTransparentBodyPass,
                                                collisionVisualizerAlphaOverride,
                                                bodyRenderAlpha },
                                              m_skyPass );
    }

    bool sceneTargetCallbackOwned = false;
    if ( useCinematicTarget )
    {
        sceneTargetCallbackOwned = ExecuteSceneTargetBeginThroughRenderGraph( frame );
    }

    // Opaque bodies render before terrain/water unless debug transparency asks
    // for a late transparent body pass.
    if ( !debugTransparentBodyPass )
    {
        m_objectPass.Render( { frame,
                               ObjectPassMode::Opaque,
                               activeCinematic,
                               objectShadowFrame,
                               collisionStateColorsVisible,
                               collisionVisualizerAlphaOverride,
                               1.0f,
                               replayFocusModelMask,
                               true } );
    }

    // Terrain receives the broad shadow frame and provides the main world depth
    // that cinematic post passes read later.
    m_terrainPass.Render( { frame, activeCinematic, terrainShadowFrame } );

    // Water is deliberately downstream of ReflectionPass; it samples the
    // reflection texture but never rebuilds it.
    m_waterPass.Render( { frame,
                          reflection,
                          activeCinematic,
                          host.m_debug.isWaterHidden,
                          host.m_debug.isWaterFlatDebug,
                          host.m_debug.isWaterNoReflect,
                          host.m_debug.isWaterFreezeDebug,
                          host.m_debug.frozenWaterTime } );

    const GraphPassResult tornadoVisualGraph = ExecuteTornadoVisualThroughRenderGraph( frame, useCinematicTarget );

    if ( debugTransparentBodyPass )
    {
        m_objectPass.Render( { frame,
                               ObjectPassMode::Transparent,
                               activeCinematic,
                               objectShadowFrame,
                               collisionStateColorsVisible,
                               collisionVisualizerAlphaOverride,
                               bodyRenderAlpha,
                               nullptr,
                               true } );
    }
    else if ( replayFocusFadeActive )
    {
        m_objectPass.Render( { frame,
                               ObjectPassMode::Transparent,
                               activeCinematic,
                               objectShadowFrame,
                               collisionStateColorsVisible,
                               collisionVisualizerAlphaOverride,
                               0.5f,
                               replayFocusModelMask,
                               false } );
    }

    host.RenderReplayPredictionGhosts( frame, activeCinematic, objectShadowFrame );

    const bool debugOverlayCallbackOwned = ExecuteDebugOverlayThroughRenderGraph( frame, useCinematicTarget );

    bool volumetricReady = false;
    bool volumetricCallbackOwned = false;
    bool tonemapCallbackOwned = false;
    if ( useCinematicTarget )
    {
        const CinematicPostGraphResult cinematicPostGraph = ExecuteCinematicPostThroughRenderGraph( frame );
        volumetricReady = cinematicPostGraph.volumetricReady;
        volumetricCallbackOwned = cinematicPostGraph.volumetricCallbackOwned;
        tonemapCallbackOwned = cinematicPostGraph.tonemapCallbackOwned;
    }

    Rendering::RenderSceneSnapshot frameSnapshot;
    frameSnapshot.cinematicRender = cinematicRender;
    frameSnapshot.useCinematicTarget = useCinematicTarget;
    frameSnapshot.terrainShadowValid = terrainShadowFrame && terrainShadowFrame->valid;
    frameSnapshot.objectShadowValid = objectShadowFrame && objectShadowFrame->valid;
    frameSnapshot.reflectionUsedDxr = reflection.usedDxr;
    frameSnapshot.objectOpaquePass = !debugTransparentBodyPass;
    frameSnapshot.objectTransparentPass = transparentBodyPass;
    frameSnapshot.terrainPassRendered = !host.m_debug.isTerrainHidden;
    const WaterPassDebugInfo& waterDebug = m_waterPass.LastDebugInfo();
    frameSnapshot.waterPassRendered = waterDebug.rendered;
    frameSnapshot.waterSamplesReflection =
        waterDebug.rendered && !waterDebug.noReflection && waterDebug.reflectionValid;
    frameSnapshot.skyboxCallbackOwned = skyboxCallbackOwned;
    frameSnapshot.sceneTargetCallbackOwned = sceneTargetCallbackOwned;
    frameSnapshot.tornadoVisualRendered = tornadoVisualGraph.rendered;
    frameSnapshot.tornadoVisualCallbackOwned = tornadoVisualGraph.callbackOwned;
    frameSnapshot.debugOverlayCallbackOwned = debugOverlayCallbackOwned;
    frameSnapshot.volumetricCallbackOwned = volumetricCallbackOwned;
    frameSnapshot.volumetricReady = volumetricReady;
    frameSnapshot.tonemapCallbackOwned = tonemapCallbackOwned;
    Rendering::RenderPipeline::DumpExecutedFrameGraphIfChanged( frameSnapshot );
}


void RuntimeRenderer::ReleaseBackendOwnedResources( Rendering::IRenderResourceFactory* renderResources )
{
    // Lifetime: release pass-owned GPU resources while the renderer backend is
    // still alive. The order keeps consumers ahead of their producers, so cached
    // handles are invalidated before targets die.
    m_tonemapPass.ReleaseGpuResources();
    m_volumetricPass.ReleaseGpuResources();
    m_tornadoVisualPass.ReleaseGpuResources();
    m_sceneTargetPass.ReleaseGpuResources();
    m_shadowPass.ReleaseGpuResources();
    m_reflectionPass.ReleaseGpuResources();
    m_skyPass.ReleaseGpuResources();
    m_fullscreenQuadPass.ReleaseGpuResources( renderResources );
    m_uiTextPass.ReleaseGpuResources();
    m_uiTextRayTracing = nullptr;
}


void RuntimeRenderer::EnsureUiTextResources()
{
    m_uiTextPass.EnsureGpuResources();
}


bool RuntimeRenderer::ShouldRenderUiText() const
{
    return m_uiTextPass.ShouldRender();
}


void RuntimeRenderer::SetUiTextRayTracingCapability( Rendering::IRenderRayTracing* renderRayTracing )
{
    m_uiTextRayTracing = renderRayTracing;
}


void RuntimeRenderer::RenderUiText( Rendering::IRenderDiagnostics& renderDiagnostics, double dSecondsPerFrame )
{
    (void)ExecuteUiTextThroughRenderGraph( renderDiagnostics, m_uiTextRayTracing, dSecondsPerFrame );
}


void Run::Render()
{
    m_renderer.SetUiTextRayTracingCapability( nullptr );

    // In text_only mode all 3D rendering is skipped. UiTextPass handles the display.
    if ( m_debug.isTextOnly )
    {
        return;
    }

    // Update the active camera selection and any transition/tween state before
    // rendering asks for view matrices.
    SetViewingOrientation();

    // Selected camera state is copied into the camera collection so render code below
    // reads one coherent eye/view/up triple for this frame.
    m_systems.cameras->SetCamera();

    const auto applyReplayLauncherVisualSampleForRender = [&]( const ReplayLauncherVisualSample& sample )
    {
        if ( m_replayRuntime.HasLauncherVisualBackup() )
        {
            return;
        }

        ReplayLauncherVisualSample liveSample;
        m_runtimeTools.BuildReplayLauncherVisualSample( liveSample );
        m_replayRuntime.StoreLauncherVisualBackup( liveSample );
        m_runtimeTools.RestoreReplayLauncherVisualSample( sample );
    };

    const auto restoreReplayLauncherVisualForRender = [&]()
    {
        if ( !m_replayRuntime.HasLauncherVisualBackup() )
        {
            return;
        }

        m_runtimeTools.RestoreReplayLauncherVisualSample( m_replayRuntime.LauncherVisualBackup() );
        m_replayRuntime.ClearLauncherVisualBackup();
    };

    const auto applyReplayRenderStateForFrame = [&]()
    {
        if ( const RunReplayPredictionFrame* predictionFrame = m_replayRuntime.CurrentPredictionScrubFrame() )
        {
            m_replayRuntime.ApplyPredictionFrameForRender( m_cGameModelCollection, *predictionFrame );
        }
        else if ( const ReplayPresentationSample* replaySample = m_replayRuntime.CurrentScrubSample() )
        {
            m_replayRuntime.ApplyPresentationSampleForRender( m_cGameModelCollection, *replaySample );
        }
        else if ( const ReplaySolverFrameSample* solverSample = m_replayRuntime.CurrentSolverScrubSample() )
        {
            m_replayRuntime.ApplySolverSampleForRender( m_cGameModelCollection, *solverSample );
            applyReplayLauncherVisualSampleForRender( solverSample->launcherVisual );
        }
    };

    const auto restoreReplayRenderStateForFrame = [&]()
    {
        m_replayRuntime.RestoreRenderPose( m_cGameModelCollection );
        restoreReplayLauncherVisualForRender();
    };

    applyReplayRenderStateForFrame();

    const bool renderReady = IsGfxReady();
    if ( !renderReady )
    {
        restoreReplayRenderStateForFrame();
        return;
    }

    // Invariant: render inputs only borrow command capabilities after the
    // process-bound backend is ready. The captured flag records that guard for
    // frame decisions without making the command context nullable.
    IRenderBackend& renderBackend = Gfx();
    SkullbonezCore::Rendering::IRenderCommandContext& renderCommands =
        static_cast<SkullbonezCore::Rendering::IRenderCommandContext&>( renderBackend );
    SkullbonezCore::Rendering::IRenderResourceFactory& renderResources =
        static_cast<SkullbonezCore::Rendering::IRenderResourceFactory&>( renderBackend );
    SkullbonezCore::Rendering::IRenderDiagnostics& renderDiagnostics =
        static_cast<SkullbonezCore::Rendering::IRenderDiagnostics&>( renderBackend );
    SkullbonezCore::Rendering::IRenderRayTracing* renderRayTracing =
        IsGfxRayTracingReady() ? &GfxRayTracing() : nullptr;
    m_renderer.SetUiTextRayTracingCapability( renderRayTracing );
    m_renderer.RenderFrame( BuildRuntimeRenderInputs( m_systems,
                                                      m_cGameModelCollection,
                                                      m_cWorldEnvironment,
                                                      m_UI,
                                                      renderCommands,
                                                      renderResources,
                                                      renderDiagnostics,
                                                      renderRayTracing,
                                                      renderReady ) );
    restoreReplayRenderStateForFrame();
}


void Run::RebuildRegisteredRenderResources()
{
    enum class RebuildStep
    {
        ResetHelperCache,
        RegisterBuiltInSources,
        RebuildTextures
    };

    struct RebuildPhase
    {
        const char* name;
        RebuildStep step;
    };

    const RebuildPhase rebuildSteps[] = {
        { "reset_helper_cache", RebuildStep::ResetHelperCache },
        { "register_builtin_source_records", RebuildStep::RegisterBuiltInSources },
        { "rebuild_textures_from_source_assets", RebuildStep::RebuildTextures },
    };

    for ( const RebuildPhase& phase : rebuildSteps )
    {
        LogRenderResourceLifecycleStep( "backend_rebuild", phase.name );
        switch ( phase.step )
        {
        case RebuildStep::ResetHelperCache:
            RenderHelper::ResetRenderResources();
            break;
        case RebuildStep::RegisterBuiltInSources:
            RegisterBuiltInAssets();
            break;
        case RebuildStep::RebuildTextures:
            // Recreate backend texture handles from stable source asset records.
            m_systems.textures->RebuildTexturesFromSourceAssets();
            break;
        }
    }
}


void Run::SetViewingOrientation()
{
    if ( m_replayRuntime.Camera().active )
    {
        PROFILE_SCOPED( "Frame/Replay/Camera" );
        m_camera.cameraTime = 0.0f;
        m_timers.cameraTimer.StopTimer();
        m_timers.cameraTimer.StartTimer();
        return;
    }

    // In scene mode, use the authored camera without generated-demo tracking or cycling.
    if ( SceneState().isSceneMode )
    {
        return;
    }

    // Momentary right-mouse camera look should not fight generated camera cycling.
    if ( IsFlyCameraMode() || MouseLookOwnsCursor() )
    {
        m_camera.cameraTime = 0.0f;
        m_timers.cameraTimer.StopTimer();
        m_timers.cameraTimer.StartTimer();
        return;
    }

    /*
        if(Input::IsKeyDown('1')) m_camera.selectedCamera = 0;
        if(Input::IsKeyDown('2')) m_camera.selectedCamera = 1;
        if(Input::IsKeyDown('3')) m_camera.selectedCamera = 2;
    */

    // maintain the camera timer
    m_timers.cameraTimer.StopTimer();
    m_camera.cameraTime += static_cast<float>( m_timers.cameraTimer.GetElapsedTime() );
    m_timers.cameraTimer.StartTimer();

    // change the viewing camera automatically
    if ( m_camera.cameraTime > 5.0f )
    {
        ++m_camera.selectedCamera;
        if ( m_camera.selectedCamera == 3 )
        {
            m_camera.selectedCamera = 0;
        }
        m_camera.cameraTime = 0.0f;
    }

    // select camera based on input
    switch ( m_camera.selectedCamera )
    {
    case 0:
        m_systems.cameras->SelectCamera( CAMERA_GAME_MODEL_1, true );
        break;
    case 1:
        m_systems.cameras->SelectCamera( CAMERA_GAME_MODEL_2, true );
        break;
    case 2:
        m_systems.cameras->SelectCamera( CAMERA_FREE, true );
        break;
    }

    // Object-follow cameras keep their eye fixed and retarget their view point
    // to the tracked model each frame.
    if ( m_systems.cameras->IsCameraSelected( CAMERA_GAME_MODEL_1 ) )
    {
        m_systems.cameras->SetViewCoordinates( m_cGameModelCollection.GetModelPosition( 0 ) );
    }
    if ( m_systems.cameras->IsCameraSelected( CAMERA_GAME_MODEL_2 ) )
    {
        m_systems.cameras->SetViewCoordinates( m_cGameModelCollection.GetModelPosition( 1 ) );
    }

    /*
        // New synchronization requests start a fresh relative-camera baseline.
        if(m_camera.input.Get( InputState::Aux1 )) m_systems.cameras->ResetRelativity();

        // sync m_cameras if in sync mode
        if(m_camera.input.Get( InputState::Aux2 ))
        {
            // perform the relative update
            RelativeUpdateCamera(CAMERA_GAME_MODEL_1);
            RelativeUpdateCamera(CAMERA_GAME_MODEL_2);
            RelativeUpdateCamera(CAMERA_FREE);

            // The requested relative update has been consumed for this frame.
            m_systems.cameras->ResetRelativity();
        }
    */
}


void Run::RelativeUpdateCamera( uint32_t hash )
{
    if ( !m_systems.cameras->IsCameraSelected( hash ) )
    {
        Vector3 translatedCameraPosition = m_systems.cameras->GetCameraTranslation( hash );
        float minY =
            m_systems.terrain->GetTerrainHeightAt( translatedCameraPosition.x, translatedCameraPosition.z, true ) +
            Cfg().minCameraHeight;
        m_systems.cameras->RelativeUpdate( hash, minY, Cfg().maxCameraHeight );
    }
}
