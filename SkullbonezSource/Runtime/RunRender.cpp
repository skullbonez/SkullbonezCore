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
#include "../Rendering/IRenderDiagnostics.h"
#include "../Rendering/RenderGraph.h"
#include "../Rendering/RenderPipeline.h"

#include <fstream>
#include <stdexcept>

using namespace SkullbonezCore::Basics;
using namespace SkullbonezCore::Math::CollisionDetection;
using namespace SkullbonezCore::Math::Orientation;
using namespace SkullbonezCore::Math::Transformation;
using namespace SkullbonezCore::Physics;
using namespace SkullbonezCore::Basics::RunInternal;

namespace
{
// Concept: RenderGraph callback payloads are RuntimeRenderer-owned one-frame
// scratch records. The graph API invokes C-style callbacks, so each payload
// carries only the pass object and frame/view borrows needed by that pass.
//
// Lifetime: payloads are stack objects consumed during the same RenderFrame()
// call. Never cache these pointers across graph execution or use them as a
// wider runtime service boundary.
//
// Deletion condition: once every pass records through typed graph nodes instead
// of C-style userData callbacks, these payload structs and callback-owned
// result flags should disappear together. Checker budget: graph callbacks may
// borrow frame/view records, but must not reintroduce concrete scene-container
// access into production render code.
struct CinematicPostGraphCallbackData
{
    VolumetricPass* volumetricPass = nullptr;
    TonemapPass* tonemapPass = nullptr;
    const RenderFrameContext* frame = nullptr;
    SkullbonezCore::Rendering::RenderGraphTextureBinding volumetricLight;
    bool volumetricRendered = false;
};

struct ShadowGraphCallbackData
{
    ShadowPass* shadowPass = nullptr;
    const RenderFrameContext* frame = nullptr;
    const CinematicRenderConfig* cinematic = nullptr;
    ShadowPassOutput output;
};

struct ReflectionGraphCallbackData
{
    ReflectionPass* reflectionPass = nullptr;
    SkyPass* skyPass = nullptr;
    const RenderFrameContext* frame = nullptr;
    const CinematicRenderConfig* cinematic = nullptr;
    const SkullbonezCore::Rendering::ShadowFrameData* objectShadow = nullptr;
    bool collisionStateColorsVisible = false;
    bool transparentBodyPass = false;
    float collisionVisualizerAlphaOverride = -1.0f;
    float bodyAlpha = 1.0f;
    ReflectionPassOutput output;
};

struct ObjectGraphCallbackData
{
    ObjectPass* objectPass = nullptr;
    const RenderFrameContext* frame = nullptr;
    ObjectPassMode mode = ObjectPassMode::Opaque;
    const CinematicRenderConfig* cinematic = nullptr;
    const SkullbonezCore::Rendering::ShadowFrameData* shadow = nullptr;
    bool collisionStateColorsVisible = false;
    float collisionVisualizerAlphaOverride = -1.0f;
    float bodyAlpha = 1.0f;
    const std::vector<uint8_t>* modelMask = nullptr;
    bool drawMaskedModels = true;
};

struct TerrainGraphCallbackData
{
    TerrainPass* terrainPass = nullptr;
    const RenderFrameContext* frame = nullptr;
    const CinematicRenderConfig* cinematic = nullptr;
    const SkullbonezCore::Rendering::ShadowFrameData* shadow = nullptr;
};

struct WaterGraphCallbackData
{
    WaterPass* waterPass = nullptr;
    const RenderFrameContext* frame = nullptr;
    const ReflectionPassOutput* reflection = nullptr;
    const CinematicRenderConfig* cinematic = nullptr;
    bool waterHidden = false;
    bool flatWater = false;
    bool noReflection = false;
    bool freezeTime = false;
    float frozenTime = 0.0f;
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
    const SkullbonezCore::UI::UIRenderContext* uiRender = nullptr;
    const RuntimeRenderModelFrameView* models = nullptr;
    SkullbonezCore::Rendering::IRenderRayTracing* renderRayTracing = nullptr;
    double secondsPerFrame = 0.0;
};

struct TornadoVisualGraphCallbackData
{
    TornadoVisualPass* tornadoVisualPass = nullptr;
    const RenderFrameContext* frame = nullptr;
    bool rendered = false;
};

struct ReplayGhostGraphCallbackData
{
    RuntimeRenderHost* host = nullptr;
    const RenderFrameContext* frame = nullptr;
    const CinematicRenderConfig* cinematic = nullptr;
    const SkullbonezCore::Rendering::ShadowFrameData* shadow = nullptr;
};

void ExecuteShadowGraphCallback( const SkullbonezCore::Rendering::RenderGraphPassContext& /*context*/, void* userData )
{
    auto* data = static_cast<ShadowGraphCallbackData*>( userData );
    if ( !data || !data->shadowPass || !data->frame )
    {
        throw std::runtime_error( "ShadowMapPass graph callback missing execution data" );
    }
    data->output = data->shadowPass->Render( { *data->frame, data->cinematic } );
}

void ExecuteReflectionGraphCallback( const SkullbonezCore::Rendering::RenderGraphPassContext& /*context*/,
                                     void* userData )
{
    auto* data = static_cast<ReflectionGraphCallbackData*>( userData );
    if ( !data || !data->reflectionPass || !data->skyPass || !data->frame )
    {
        throw std::runtime_error( "ReflectionPass graph callback missing execution data" );
    }
    data->output = data->reflectionPass->Render( { *data->frame,
                                                   data->cinematic,
                                                   data->objectShadow,
                                                   data->collisionStateColorsVisible,
                                                   data->transparentBodyPass,
                                                   data->collisionVisualizerAlphaOverride,
                                                   data->bodyAlpha },
                                                 *data->skyPass );
}

void ExecuteObjectGraphCallback( const SkullbonezCore::Rendering::RenderGraphPassContext& /*context*/, void* userData )
{
    auto* data = static_cast<ObjectGraphCallbackData*>( userData );
    if ( !data || !data->objectPass || !data->frame )
    {
        throw std::runtime_error( "ObjectPass graph callback missing execution data" );
    }
    data->objectPass->Render( { *data->frame,
                                data->mode,
                                data->cinematic,
                                data->shadow,
                                data->collisionStateColorsVisible,
                                data->collisionVisualizerAlphaOverride,
                                data->bodyAlpha,
                                data->modelMask,
                                data->drawMaskedModels } );
}

void ExecuteTerrainGraphCallback( const SkullbonezCore::Rendering::RenderGraphPassContext& /*context*/, void* userData )
{
    auto* data = static_cast<TerrainGraphCallbackData*>( userData );
    if ( !data || !data->terrainPass || !data->frame )
    {
        throw std::runtime_error( "TerrainPass graph callback missing execution data" );
    }
    data->terrainPass->Render( { *data->frame, data->cinematic, data->shadow } );
}

void ExecuteWaterGraphCallback( const SkullbonezCore::Rendering::RenderGraphPassContext& /*context*/, void* userData )
{
    auto* data = static_cast<WaterGraphCallbackData*>( userData );
    if ( !data || !data->waterPass || !data->frame || !data->reflection )
    {
        throw std::runtime_error( "WaterPass graph callback missing execution data" );
    }
    data->waterPass->Render( { *data->frame,
                               *data->reflection,
                               data->cinematic,
                               data->waterHidden,
                               data->flatWater,
                               data->noReflection,
                               data->freezeTime,
                               data->frozenTime } );
}

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

void ExecuteReplayGhostGraphCallback( const SkullbonezCore::Rendering::RenderGraphPassContext& /*context*/,
                                      void* userData )
{
    auto* data = static_cast<ReplayGhostGraphCallbackData*>( userData );
    if ( !data || !data->host || !data->frame )
    {
        throw std::runtime_error( "ReplayPredictionGhostPass graph callback missing execution data" );
    }
    data->host->RenderReplayPredictionGhosts( *data->frame, data->cinematic, data->shadow );
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
    if ( !data || !data->uiTextPass || !data->renderDiagnostics || !data->uiRender || !data->models )
    {
        throw std::runtime_error( "UiTextPass graph callback missing execution data" );
    }
    data->uiTextPass->Render(
        { *data->renderDiagnostics, *data->uiRender, *data->models, data->renderRayTracing, data->secondsPerFrame } );
}

void ExecuteVolumetricGraphCallback( const SkullbonezCore::Rendering::RenderGraphPassContext& /*context*/,
                                     void* userData )
{
    auto* data = static_cast<CinematicPostGraphCallbackData*>( userData );
    if ( !data || !data->volumetricPass || !data->frame )
    {
        throw std::runtime_error( "VolumetricLightPass graph callback missing execution data" );
    }
    const SkullbonezCore::Rendering::RenderGraphTextureBinding* graphOutput =
        data->volumetricLight.IsValid() ? &data->volumetricLight : nullptr;
    data->volumetricRendered = data->volumetricPass->Render( *data->frame, graphOutput );
}

void ExecuteTonemapGraphCallback( const SkullbonezCore::Rendering::RenderGraphPassContext& /*context*/, void* userData )
{
    auto* data = static_cast<CinematicPostGraphCallbackData*>( userData );
    if ( !data || !data->tonemapPass || !data->frame )
    {
        throw std::runtime_error( "ToneMapPass graph callback missing execution data" );
    }
    const SkullbonezCore::Rendering::RenderGraphTextureBinding* graphVolumetric =
        ( data->volumetricRendered && data->volumetricLight.IsValid() ) ? &data->volumetricLight : nullptr;
    data->tonemapPass->Render( *data->frame, data->volumetricRendered, data->volumetricRendered, graphVolumetric );
}

void WriteCinematicPostGraphEvidence(
    const SkullbonezCore::Rendering::RenderGraph& graph,
    const SkullbonezCore::Rendering::RenderGraphCompileResult& compiled,
    const SkullbonezCore::Rendering::RenderGraphTransientMaterializationStats& materialization,
    const SkullbonezCore::Rendering::RenderGraphTextureBinding& volumetricBinding,
    bool volumetricDeclared )
{
    std::ofstream out( "Debug/dx12_cinematic_post_graph.txt", std::ios::trunc );
    if ( !out.is_open() )
    {
        return;
    }

    out << graph.DumpText();
    out << "\nCinematicPostGraphMaterialization:\n";
    out << "  volumetric_declared=" << ( volumetricDeclared ? "true" : "false" ) << "\n";
    out << "  volumetric_binding_valid=" << ( volumetricBinding.IsValid() ? "true" : "false" ) << "\n";
    out << "  volumetric_texture_handle=" << volumetricBinding.textureHandle << "\n";
    out << "  volumetric_size=" << volumetricBinding.width << "x" << volumetricBinding.height << "\n";
    out << "  pool_size=" << materialization.poolSize << "\n";
    out << "  created_this_compile=" << materialization.createdThisCompile << "\n";
    out << "  reused_this_compile=" << materialization.reusedThisCompile << "\n";
    out << "  descriptor_rows_owned=" << materialization.descriptorRowsOwned << "\n";
    out << "  released_at_frame_end=" << materialization.releasedAtFrameEnd << "\n";
    out << "  transient_allocation_count=" << compiled.transientAllocations.size() << "\n";
    for ( size_t i = 0; i < compiled.transientAllocations.size(); ++i )
    {
        const SkullbonezCore::Rendering::RenderGraphTransientAllocationDesc& allocation =
            compiled.transientAllocations[i];
        const SkullbonezCore::Rendering::RenderGraphResourceDesc& resource =
            graph.Resources()[allocation.resource.index];
        out << "  [" << i << "] resource=" << resource.name << " pool_slot=" << allocation.poolSlot
            << " first_pass=" << allocation.firstPass << " last_pass=" << allocation.lastPass << "\n";
    }
}

RuntimeRenderInputs BuildRuntimeRenderInputs( RunSubsystemState& systems,
                                              const RuntimeRenderModelFrameView& models,
                                              SkullbonezCore::Environment::WorldEnvironment& world,
                                              SkullbonezCore::UI::InGameUI& ui,
                                              SkullbonezCore::Rendering::IRenderCommandContext& renderCommands,
                                              SkullbonezCore::Rendering::IRenderResourceFactory& renderResources,
                                              SkullbonezCore::Rendering::IRenderDiagnostics& renderDiagnostics,
                                              SkullbonezCore::Rendering::IRenderRayTracing* renderRayTracing,
                                              bool renderReady )
{
    return RuntimeRenderInputs{ RuntimeRenderServices{ systems.assets,
                                                       *systems.textures,
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

SkullbonezCore::Rendering::RenderGraphResourceHandle AddFrameColorTarget( SkullbonezCore::Rendering::RenderGraph& graph,
                                                                          bool useCinematicTarget )
{
    return graph.AddExternalResource( useCinematicTarget ? "CinematicSceneColor" : "SwapchainBackbuffer",
                                      SkullbonezCore::Rendering::RenderGraphResourceAccess::RenderTarget );
}

SkullbonezCore::Rendering::RenderGraphResourceHandle AddFrameDepthTarget( SkullbonezCore::Rendering::RenderGraph& graph,
                                                                          bool useCinematicTarget )
{
    return graph.AddExternalResource( useCinematicTarget ? "CinematicSceneDepth" : "MainDepthStencil",
                                      SkullbonezCore::Rendering::RenderGraphResourceAccess::DepthWrite );
}

void AddFrameTargetWrites( SkullbonezCore::Rendering::RenderGraph& graph, uint32_t pass, bool useCinematicTarget )
{
    const SkullbonezCore::Rendering::RenderGraphResourceHandle colorTarget =
        AddFrameColorTarget( graph, useCinematicTarget );
    const SkullbonezCore::Rendering::RenderGraphResourceHandle depthTarget =
        AddFrameDepthTarget( graph, useCinematicTarget );
    graph.AddWrite( pass, colorTarget, SkullbonezCore::Rendering::RenderGraphResourceAccess::RenderTarget );
    graph.AddWrite( pass, depthTarget, SkullbonezCore::Rendering::RenderGraphResourceAccess::DepthWrite );
}
} // namespace

RuntimeRenderer::ShadowGraphResult
RuntimeRenderer::ExecuteShadowThroughRenderGraph( const RenderFrameContext& frame,
                                                  const CinematicRenderConfig* activeShadowConfig )
{
    Rendering::RenderGraph graph;
    const Rendering::RenderGraphResourceHandle terrainShadow =
        graph.AddExternalResource( "TerrainShadowMapDepth", Rendering::RenderGraphResourceAccess::PixelShaderResource );
    const Rendering::RenderGraphResourceHandle objectShadow =
        graph.AddExternalResource( "ObjectShadowMapDepth", Rendering::RenderGraphResourceAccess::PixelShaderResource );

    const uint32_t shadowPass = graph.AddPass( "ShadowMapPass",
                                               Rendering::RenderGraphQueueType::Graphics,
                                               Rendering::RenderGraphBarrierPolicy::HandoffValidated );
    graph.AddWrite( shadowPass, terrainShadow, Rendering::RenderGraphResourceAccess::DepthWrite );
    graph.AddWrite( shadowPass, objectShadow, Rendering::RenderGraphResourceAccess::DepthWrite );

    ShadowGraphCallbackData callbackData;
    callbackData.shadowPass = &m_shadowPass;
    callbackData.frame = &frame;
    callbackData.cinematic = activeShadowConfig;
    graph.SetPassCallback( shadowPass, ExecuteShadowGraphCallback, &callbackData, true, "Frame/Shadows/ShadowMap" );

    // Invariant: even when activeShadowConfig is null, ShadowPass::Render clears
    // stale receiver payloads. The graph owns that reset scheduling point and
    // declares the stable shadow-map resources produced when shadows are active.
    graph.Compile();
    graph.ExecuteCallbacks( Rendering::RenderGraphCallbackExecutionMode::DryRun );
    const Rendering::RenderGraphCallbackExecutionResult executed =
        graph.ExecuteCallbacks( Rendering::RenderGraphCallbackExecutionMode::Execute );

    ShadowGraphResult result;
    result.output = callbackData.output;
    result.callbackOwned = executed.executedPassCount == 1u;
    return result;
}

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


RuntimeRenderer::ReflectionGraphResult
RuntimeRenderer::ExecuteReflectionThroughRenderGraph( const RenderFrameContext& frame,
                                                      const CinematicRenderConfig* activeCinematic,
                                                      const Rendering::ShadowFrameData* objectShadow,
                                                      bool collisionStateColorsVisible,
                                                      bool debugTransparentBodyPass,
                                                      float collisionVisualizerAlphaOverride,
                                                      float bodyAlpha )
{
    Rendering::RenderGraph graph;
    const bool useDxrCandidate = frame.renderDiagnostics && frame.renderRayTracing &&
                                 frame.renderDiagnostics->GetCapabilities().supportsDxrReflection &&
                                 m_host.m_debug.isWaterRTReflect && !m_host.m_debug.isWaterNoReflect &&
                                 !collisionStateColorsVisible && !debugTransparentBodyPass;

    Rendering::RenderGraphResourceHandle objectShadowResource;
    if ( objectShadow && objectShadow->valid )
    {
        objectShadowResource = graph.AddExternalResource( "ObjectShadowMapDepth",
                                                          Rendering::RenderGraphResourceAccess::PixelShaderResource );
    }

    const uint32_t reflectionPass = graph.AddPass(
        useDxrCandidate ? "DxrReflectionPass" : "RasterReflectionPass",
        useDxrCandidate ? Rendering::RenderGraphQueueType::Compute : Rendering::RenderGraphQueueType::Graphics,
        Rendering::RenderGraphBarrierPolicy::HandoffValidated );
    if ( objectShadowResource.IsValid() )
    {
        graph.AddRead( reflectionPass,
                       objectShadowResource,
                       Rendering::RenderGraphResourceAccess::PixelShaderResource );
    }
    if ( useDxrCandidate )
    {
        const Rendering::RenderGraphResourceHandle dxrReflection =
            graph.AddExternalResource( "DxrReflectionTexture",
                                       Rendering::RenderGraphResourceAccess::PixelShaderResource );
        graph.AddWrite( reflectionPass, dxrReflection, Rendering::RenderGraphResourceAccess::UnorderedAccess );
    }
    else
    {
        const Rendering::RenderGraphResourceHandle reflectionColor =
            graph.AddExternalResource( "RasterReflectionColor",
                                       Rendering::RenderGraphResourceAccess::PixelShaderResource );
        const Rendering::RenderGraphResourceHandle reflectionDepth =
            graph.AddExternalResource( "RasterReflectionDepth",
                                       Rendering::RenderGraphResourceAccess::PixelShaderResource );
        graph.AddWrite( reflectionPass, reflectionColor, Rendering::RenderGraphResourceAccess::RenderTarget );
        graph.AddWrite( reflectionPass, reflectionDepth, Rendering::RenderGraphResourceAccess::DepthWrite );
    }

    ReflectionGraphCallbackData callbackData;
    callbackData.reflectionPass = &m_reflectionPass;
    callbackData.skyPass = &m_skyPass;
    callbackData.frame = &frame;
    callbackData.cinematic = activeCinematic;
    callbackData.objectShadow = objectShadow;
    callbackData.collisionStateColorsVisible = collisionStateColorsVisible;
    callbackData.transparentBodyPass = debugTransparentBodyPass;
    callbackData.collisionVisualizerAlphaOverride = collisionVisualizerAlphaOverride;
    callbackData.bodyAlpha = bodyAlpha;
    graph.SetPassCallback( reflectionPass,
                           ExecuteReflectionGraphCallback,
                           &callbackData,
                           true,
                           useDxrCandidate ? "Frame/Render/Reflection/DXR" : "Frame/Render/Reflection/Raster" );

    // Invariant: reflection still chooses DXR or raster in ReflectionPass using
    // the same runtime conditions. The graph now owns the scheduling point and
    // declares the texture family that WaterPass samples later.
    graph.Compile();
    graph.ExecuteCallbacks( Rendering::RenderGraphCallbackExecutionMode::DryRun );
    const Rendering::RenderGraphCallbackExecutionResult executed =
        graph.ExecuteCallbacks( Rendering::RenderGraphCallbackExecutionMode::Execute );

    ReflectionGraphResult result;
    result.output = callbackData.output;
    result.callbackOwned = executed.executedPassCount == 1u;
    return result;
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


bool RuntimeRenderer::ExecuteObjectThroughRenderGraph( const RenderFrameContext& frame,
                                                       ObjectPassMode mode,
                                                       bool useCinematicTarget,
                                                       const CinematicRenderConfig* activeCinematic,
                                                       const Rendering::ShadowFrameData* objectShadow,
                                                       bool collisionStateColorsVisible,
                                                       float collisionVisualizerAlphaOverride,
                                                       float bodyAlpha,
                                                       const std::vector<uint8_t>* replayFocusModelMask,
                                                       bool drawMaskedModels )
{
    Rendering::RenderGraph graph;
    Rendering::RenderGraphResourceHandle objectShadowResource;
    if ( objectShadow && objectShadow->valid )
    {
        objectShadowResource = graph.AddExternalResource( "ObjectShadowMapDepth",
                                                          Rendering::RenderGraphResourceAccess::PixelShaderResource );
    }

    const char* passName = mode == ObjectPassMode::Transparent ? "ObjectTransparentPass" : "ObjectOpaquePass";
    const uint32_t objectPass = graph.AddPass( passName,
                                               Rendering::RenderGraphQueueType::Graphics,
                                               Rendering::RenderGraphBarrierPolicy::HandoffValidated );
    if ( objectShadowResource.IsValid() )
    {
        graph.AddRead( objectPass, objectShadowResource, Rendering::RenderGraphResourceAccess::PixelShaderResource );
    }
    AddFrameTargetWrites( graph, objectPass, useCinematicTarget );

    ObjectGraphCallbackData callbackData;
    callbackData.objectPass = &m_objectPass;
    callbackData.frame = &frame;
    callbackData.mode = mode;
    callbackData.cinematic = activeCinematic;
    callbackData.shadow = objectShadow;
    callbackData.collisionStateColorsVisible = collisionStateColorsVisible;
    callbackData.collisionVisualizerAlphaOverride = collisionVisualizerAlphaOverride;
    callbackData.bodyAlpha = bodyAlpha;
    callbackData.modelMask = replayFocusModelMask;
    callbackData.drawMaskedModels = drawMaskedModels;
    graph.SetPassCallback(
        objectPass,
        ExecuteObjectGraphCallback,
        &callbackData,
        true,
        mode == ObjectPassMode::Transparent ? "Frame/Render/Objects/Transparent" : "Frame/Render/Objects/Opaque" );

    // Concept: object draw selection still lives in ObjectPassInputs. The graph
    // owns when that selection is scheduled and which frame target/shadow map
    // resources the selected object pass reads and writes.
    graph.Compile();
    graph.ExecuteCallbacks( Rendering::RenderGraphCallbackExecutionMode::DryRun );
    const Rendering::RenderGraphCallbackExecutionResult executed =
        graph.ExecuteCallbacks( Rendering::RenderGraphCallbackExecutionMode::Execute );
    return executed.executedPassCount == 1u;
}


bool RuntimeRenderer::ExecuteTerrainThroughRenderGraph( const RenderFrameContext& frame,
                                                        bool useCinematicTarget,
                                                        const CinematicRenderConfig* activeCinematic,
                                                        const Rendering::ShadowFrameData* terrainShadow )
{
    Rendering::RenderGraph graph;
    Rendering::RenderGraphResourceHandle terrainShadowResource;
    if ( terrainShadow && terrainShadow->valid )
    {
        terrainShadowResource = graph.AddExternalResource( "TerrainShadowMapDepth",
                                                           Rendering::RenderGraphResourceAccess::PixelShaderResource );
    }

    const uint32_t terrainPass = graph.AddPass( "TerrainPass",
                                                Rendering::RenderGraphQueueType::Graphics,
                                                Rendering::RenderGraphBarrierPolicy::HandoffValidated );
    if ( terrainShadowResource.IsValid() )
    {
        graph.AddRead( terrainPass, terrainShadowResource, Rendering::RenderGraphResourceAccess::PixelShaderResource );
    }
    AddFrameTargetWrites( graph, terrainPass, useCinematicTarget );

    TerrainGraphCallbackData callbackData;
    callbackData.terrainPass = &m_terrainPass;
    callbackData.frame = &frame;
    callbackData.cinematic = activeCinematic;
    callbackData.shadow = terrainShadow;
    graph.SetPassCallback( terrainPass, ExecuteTerrainGraphCallback, &callbackData, true, "Frame/Render/Terrain" );

    // Invariant: terrain visibility/debug decisions remain inside TerrainPass,
    // while the graph now owns its frame target and shadow-map declaration.
    graph.Compile();
    graph.ExecuteCallbacks( Rendering::RenderGraphCallbackExecutionMode::DryRun );
    const Rendering::RenderGraphCallbackExecutionResult executed =
        graph.ExecuteCallbacks( Rendering::RenderGraphCallbackExecutionMode::Execute );
    return executed.executedPassCount == 1u;
}


bool RuntimeRenderer::ExecuteWaterThroughRenderGraph( const RenderFrameContext& frame,
                                                      const ReflectionPassOutput& reflection,
                                                      bool useCinematicTarget,
                                                      const CinematicRenderConfig* activeCinematic,
                                                      bool waterHidden,
                                                      bool flatWater,
                                                      bool noReflection,
                                                      bool freezeTime,
                                                      float frozenTime )
{
    Rendering::RenderGraph graph;
    if ( reflection.reflectionTextureHandle != 0u && !noReflection )
    {
        const Rendering::RenderGraphResourceHandle reflectionTexture =
            graph.AddExternalResource( reflection.usedDxr ? "DxrReflectionTexture" : "RasterReflectionColor",
                                       Rendering::RenderGraphResourceAccess::PixelShaderResource );
        const uint32_t waterPass = graph.AddPass( "WaterPass",
                                                  Rendering::RenderGraphQueueType::Graphics,
                                                  Rendering::RenderGraphBarrierPolicy::HandoffValidated );
        graph.AddRead( waterPass, reflectionTexture, Rendering::RenderGraphResourceAccess::PixelShaderResource );
        AddFrameTargetWrites( graph, waterPass, useCinematicTarget );

        WaterGraphCallbackData callbackData;
        callbackData.waterPass = &m_waterPass;
        callbackData.frame = &frame;
        callbackData.reflection = &reflection;
        callbackData.cinematic = activeCinematic;
        callbackData.waterHidden = waterHidden;
        callbackData.flatWater = flatWater;
        callbackData.noReflection = noReflection;
        callbackData.freezeTime = freezeTime;
        callbackData.frozenTime = frozenTime;
        graph.SetPassCallback( waterPass, ExecuteWaterGraphCallback, &callbackData, true, "Frame/Render/Water" );

        graph.Compile();
        graph.ExecuteCallbacks( Rendering::RenderGraphCallbackExecutionMode::DryRun );
        const Rendering::RenderGraphCallbackExecutionResult executed =
            graph.ExecuteCallbacks( Rendering::RenderGraphCallbackExecutionMode::Execute );
        return executed.executedPassCount == 1u;
    }

    const uint32_t waterPass = graph.AddPass( "WaterPass",
                                              Rendering::RenderGraphQueueType::Graphics,
                                              Rendering::RenderGraphBarrierPolicy::HandoffValidated );
    AddFrameTargetWrites( graph, waterPass, useCinematicTarget );

    WaterGraphCallbackData callbackData;
    callbackData.waterPass = &m_waterPass;
    callbackData.frame = &frame;
    callbackData.reflection = &reflection;
    callbackData.cinematic = activeCinematic;
    callbackData.waterHidden = waterHidden;
    callbackData.flatWater = flatWater;
    callbackData.noReflection = noReflection;
    callbackData.freezeTime = freezeTime;
    callbackData.frozenTime = frozenTime;
    graph.SetPassCallback( waterPass, ExecuteWaterGraphCallback, &callbackData, true, "Frame/Render/Water" );

    // Invariant: WaterPass may decide to draw the no-reflection shader path or
    // skip hidden water, but the graph owns that decision point and target
    // declaration every frame.
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


bool RuntimeRenderer::ExecuteReplayGhostsThroughRenderGraph( const RenderFrameContext& frame,
                                                             bool useCinematicTarget,
                                                             const CinematicRenderConfig* activeCinematic,
                                                             const Rendering::ShadowFrameData* objectShadow )
{
    Rendering::RenderGraph graph;
    Rendering::RenderGraphResourceHandle objectShadowResource;
    if ( objectShadow && objectShadow->valid )
    {
        objectShadowResource = graph.AddExternalResource( "ObjectShadowMapDepth",
                                                          Rendering::RenderGraphResourceAccess::PixelShaderResource );
    }

    const uint32_t replayPass = graph.AddPass( "ReplayPredictionGhostPass",
                                               Rendering::RenderGraphQueueType::Graphics,
                                               Rendering::RenderGraphBarrierPolicy::HandoffValidated );
    if ( objectShadowResource.IsValid() )
    {
        graph.AddRead( replayPass, objectShadowResource, Rendering::RenderGraphResourceAccess::PixelShaderResource );
    }
    AddFrameTargetWrites( graph, replayPass, useCinematicTarget );

    ReplayGhostGraphCallbackData callbackData;
    callbackData.host = &m_host;
    callbackData.frame = &frame;
    callbackData.cinematic = activeCinematic;
    callbackData.shadow = objectShadow;
    graph.SetPassCallback( replayPass,
                           ExecuteReplayGhostGraphCallback,
                           &callbackData,
                           true,
                           "Frame/Render/ReplayPredictionGhosts" );

    // Concept: replay ghost rendering is a presentation overlay, but it still
    // writes world color/depth in the same frame slot as transparent objects.
    // The graph now owns that scheduling point instead of leaving it as an
    // ad hoc host call between migrated pass families.
    graph.Compile();
    graph.ExecuteCallbacks( Rendering::RenderGraphCallbackExecutionMode::DryRun );
    const Rendering::RenderGraphCallbackExecutionResult executed =
        graph.ExecuteCallbacks( Rendering::RenderGraphCallbackExecutionMode::Execute );
    return executed.executedPassCount == 1u;
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

    if ( volumetricDeclared )
    {
        const VolumetricLightPassResources& volumetric = m_host.m_systems.renderPasses.volumetricLight;
        Rendering::RenderGraphTransientResourceDesc volumetricDesc;
        volumetricDesc.kind = Rendering::RenderGraphResourceKind::Texture2D;
        volumetricDesc.format = Rendering::RenderGraphResourceFormat::RGBA16F;
        volumetricDesc.width =
            static_cast<uint32_t>( volumetric.target->GetWidth() > 0 ? volumetric.target->GetWidth() : 1 );
        volumetricDesc.height =
            static_cast<uint32_t>( volumetric.target->GetHeight() > 0 ? volumetric.target->GetHeight() : 1 );
        volumetricDesc.mipLevels = 1;
        volumetricDesc.descriptors.renderTarget = true;
        volumetricDesc.descriptors.shaderResource = true;
        volumetricLight = graph.AddTransientResource( "VolumetricLight",
                                                      volumetricDesc,
                                                      Rendering::RenderGraphResourceAccess::PixelShaderResource );

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
    const Rendering::RenderGraphCompileResult compiled = graph.Compile();
    Rendering::RenderGraphTransientMaterializationStats transientMaterialization;
    if ( frame.renderCommands )
    {
        transientMaterialization = frame.renderCommands->MaterializeGraphTransientResources( graph, compiled );
        if ( volumetricDeclared )
        {
            callbackData.volumetricLight = frame.renderCommands->ResolveGraphTextureBinding( volumetricLight );
            if ( !callbackData.volumetricLight.IsValid() )
            {
                throw std::runtime_error( "VolumetricLight graph transient was not materialized" );
            }
        }
    }
    WriteCinematicPostGraphEvidence( graph,
                                     compiled,
                                     transientMaterialization,
                                     callbackData.volumetricLight,
                                     volumetricDeclared );
    graph.ExecuteCallbacks( Rendering::RenderGraphCallbackExecutionMode::DryRun );
    const Rendering::RenderGraphCallbackExecutionResult executed =
        graph.ExecuteCallbacks( Rendering::RenderGraphCallbackExecutionMode::Execute );

    CinematicPostGraphResult result;
    result.volumetricReady = volumetricDeclared && callbackData.volumetricRendered;
    result.volumetricCallbackOwned = result.volumetricReady;
    result.tonemapCallbackOwned = executed.executedPassCount == expectedCallbacks;
    result.volumetricTextureHandle = callbackData.volumetricLight.textureHandle;
    result.volumetricWidth = callbackData.volumetricLight.width;
    result.volumetricHeight = callbackData.volumetricLight.height;
    return result;
}


bool RuntimeRenderer::ExecuteUiTextThroughRenderGraph( Rendering::IRenderDiagnostics& renderDiagnostics,
                                                       const UI::UIRenderContext& uiRender,
                                                       const RuntimeRenderModelFrameView& models,
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
    callbackData.uiRender = &uiRender;
    callbackData.models = &models;
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
    frame.renderInstances = &services.models.renderInstances;
    frame.colliders = &services.models.colliders;
    frame.bodyStore = &services.models.bodyStore;
    frame.physicsEngine = &services.models.physicsEngine;
    frame.presentationRecords = &services.models.presentationRecords;
    frame.collisionVisualContacts = &services.models.collisionVisualContacts;
    frame.sleepStates = &services.models.sleepStates;
    frame.sleepIslandVisualIds = &services.models.sleepIslandVisualIds;
    frame.sleepSupportedStates = &services.models.sleepSupportedStates;
    frame.sleepInhibitedStates = &services.models.sleepInhibitedStates;
    frame.physicsDebugContacts = &services.models.physicsDebugContacts;
    frame.physicsPipelineTrace = &services.models.physicsPipelineTrace;
    frame.renderWorkerPool = services.models.renderWorkerPool;
    frame.modelCount = services.models.modelCount;
    frame.renderCollisionVolumes = services.models.renderCollisionVolumes;
    frame.shadowParallelPrep = services.models.shadowParallelPrep;
    frame.sceneKineticEnergy = services.models.sceneKineticEnergy;
    frame.tornadoElapsedSeconds = services.models.tornadoElapsedSeconds;
    frame.assets = &services.assets;
    frame.renderResources = &services.renderResources;
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
                                  services.assets,
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
    const ShadowGraphResult shadowGraph = ExecuteShadowThroughRenderGraph( frame, activeShadowConfig );
    ShadowPassOutput shadowPass = shadowGraph.output;
    const bool shadowCallbackOwned = shadowGraph.callbackOwned;
    const Rendering::ShadowFrameData* terrainShadowFrame = shadowPass.terrainShadow;
    const Rendering::ShadowFrameData* objectShadowFrame = shadowPass.objectShadow;

    const bool collisionStateColorsVisible = host.m_debug.isCollisionVisualizer;
    const bool debugTransparentBodyPass =
        host.m_debug.isPhysicsDebugTransparent && host.m_debug.physicsDebugAlpha < 1.0f;
    const bool replayPredictionOverlayActive = host.m_replayRuntime.Prediction().enabled;
    const bool replayFocusFadeActive = !replayPredictionOverlayActive && !collisionStateColorsVisible &&
                                       !debugTransparentBodyPass && host.BuildReplayFocusModelMask( frame );
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
    bool reflectionCallbackOwned = false;
    if ( reflectionPassNeeded )
    {
        m_reflectionPass.EnsureGpuResources( resourceContext );
        const ReflectionGraphResult reflectionGraph =
            ExecuteReflectionThroughRenderGraph( frame,
                                                 activeCinematic,
                                                 objectShadowFrame,
                                                 collisionStateColorsVisible,
                                                 debugTransparentBodyPass,
                                                 collisionVisualizerAlphaOverride,
                                                 bodyRenderAlpha );
        reflection = reflectionGraph.output;
        reflectionCallbackOwned = reflectionGraph.callbackOwned;
    }

    bool sceneTargetCallbackOwned = false;
    if ( useCinematicTarget )
    {
        sceneTargetCallbackOwned = ExecuteSceneTargetBeginThroughRenderGraph( frame );
    }

    // Opaque bodies render before terrain/water unless debug transparency asks
    // for a late transparent body pass.
    bool objectOpaqueCallbackOwned = false;
    bool objectTransparentCallbackOwned = false;
    if ( !debugTransparentBodyPass )
    {
        objectOpaqueCallbackOwned = ExecuteObjectThroughRenderGraph( frame,
                                                                     ObjectPassMode::Opaque,
                                                                     useCinematicTarget,
                                                                     activeCinematic,
                                                                     objectShadowFrame,
                                                                     collisionStateColorsVisible,
                                                                     collisionVisualizerAlphaOverride,
                                                                     1.0f,
                                                                     replayFocusModelMask,
                                                                     true );
    }

    // Terrain receives the broad shadow frame and provides the main world depth
    // that cinematic post passes read later.
    const bool terrainCallbackOwned =
        ExecuteTerrainThroughRenderGraph( frame, useCinematicTarget, activeCinematic, terrainShadowFrame );

    // Water is deliberately downstream of ReflectionPass; it samples the
    // reflection texture but never rebuilds it.
    const bool waterCallbackOwned = ExecuteWaterThroughRenderGraph( frame,
                                                                    reflection,
                                                                    useCinematicTarget,
                                                                    activeCinematic,
                                                                    host.m_debug.isWaterHidden,
                                                                    host.m_debug.isWaterFlatDebug,
                                                                    host.m_debug.isWaterNoReflect,
                                                                    host.m_debug.isWaterFreezeDebug,
                                                                    host.m_debug.frozenWaterTime );

    const GraphPassResult tornadoVisualGraph = ExecuteTornadoVisualThroughRenderGraph( frame, useCinematicTarget );

    if ( debugTransparentBodyPass )
    {
        objectTransparentCallbackOwned = ExecuteObjectThroughRenderGraph( frame,
                                                                          ObjectPassMode::Transparent,
                                                                          useCinematicTarget,
                                                                          activeCinematic,
                                                                          objectShadowFrame,
                                                                          collisionStateColorsVisible,
                                                                          collisionVisualizerAlphaOverride,
                                                                          bodyRenderAlpha,
                                                                          nullptr,
                                                                          true );
    }
    else if ( replayFocusFadeActive )
    {
        objectTransparentCallbackOwned = ExecuteObjectThroughRenderGraph( frame,
                                                                          ObjectPassMode::Transparent,
                                                                          useCinematicTarget,
                                                                          activeCinematic,
                                                                          objectShadowFrame,
                                                                          collisionStateColorsVisible,
                                                                          collisionVisualizerAlphaOverride,
                                                                          0.5f,
                                                                          replayFocusModelMask,
                                                                          false );
    }

    const bool replayGhostCallbackOwned =
        ExecuteReplayGhostsThroughRenderGraph( frame, useCinematicTarget, activeCinematic, objectShadowFrame );

    const bool debugOverlayCallbackOwned = ExecuteDebugOverlayThroughRenderGraph( frame, useCinematicTarget );

    bool volumetricReady = false;
    bool volumetricCallbackOwned = false;
    bool tonemapCallbackOwned = false;
    CinematicPostGraphResult cinematicPostGraph;
    if ( useCinematicTarget )
    {
        cinematicPostGraph = ExecuteCinematicPostThroughRenderGraph( frame );
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
    frameSnapshot.shadowCallbackOwned = shadowCallbackOwned;
    frameSnapshot.skyboxCallbackOwned = skyboxCallbackOwned;
    frameSnapshot.reflectionCallbackOwned = reflectionCallbackOwned;
    frameSnapshot.sceneTargetCallbackOwned = sceneTargetCallbackOwned;
    frameSnapshot.objectOpaqueCallbackOwned = objectOpaqueCallbackOwned;
    frameSnapshot.objectTransparentCallbackOwned = objectTransparentCallbackOwned;
    frameSnapshot.terrainCallbackOwned = terrainCallbackOwned;
    frameSnapshot.waterCallbackOwned = waterCallbackOwned;
    frameSnapshot.tornadoVisualRendered = tornadoVisualGraph.rendered;
    frameSnapshot.tornadoVisualCallbackOwned = tornadoVisualGraph.callbackOwned;
    frameSnapshot.replayGhostCallbackOwned = replayGhostCallbackOwned;
    frameSnapshot.debugOverlayCallbackOwned = debugOverlayCallbackOwned;
    frameSnapshot.volumetricCallbackOwned = volumetricCallbackOwned;
    frameSnapshot.volumetricReady = volumetricReady;
    frameSnapshot.tonemapCallbackOwned = tonemapCallbackOwned;
    if ( volumetricReady )
    {
        frameSnapshot.volumetricTextureHandle = cinematicPostGraph.volumetricTextureHandle;
        frameSnapshot.volumetricWidth = cinematicPostGraph.volumetricWidth;
        frameSnapshot.volumetricHeight = cinematicPostGraph.volumetricHeight;
    }
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
    m_waterPass.ReleaseGpuResources();
    m_terrainPass.ReleaseGpuResources();
    m_skyPass.ReleaseGpuResources();
    m_fullscreenQuadPass.ReleaseGpuResources( renderResources );
    m_uiTextPass.ReleaseGpuResources( renderResources );
    m_uiTextRayTracing = nullptr;
}


void RuntimeRenderer::EnsureUiTextResources( Rendering::IRenderResourceFactory& renderResources,
                                             const Assets::AssetSystem& assets,
                                             int screenW,
                                             int screenH )
{
    m_uiTextPass.EnsureGpuResources( renderResources, assets, screenW, screenH );
}


bool RuntimeRenderer::ShouldRenderUiText() const
{
    return m_uiTextPass.ShouldRender();
}


void RuntimeRenderer::SetUiTextRayTracingCapability( Rendering::IRenderRayTracing* renderRayTracing )
{
    m_uiTextRayTracing = renderRayTracing;
}


void RuntimeRenderer::RenderUiText( Rendering::IRenderDiagnostics& renderDiagnostics,
                                    const UI::UIRenderContext& uiRender,
                                    const RuntimeRenderModelFrameView& models,
                                    double dSecondsPerFrame )
{
    (void)ExecuteUiTextThroughRenderGraph( renderDiagnostics, uiRender, models, m_uiTextRayTracing, dSecondsPerFrame );
}


RuntimeRenderModelFrameView Run::BuildRuntimeRenderModelFrameView()
{
    PhysicsEngine& physics = m_cGameModelCollection.GetPhysicsEngine();
    return RuntimeRenderModelFrameView{ m_cGameModelCollection.RenderInstances(),
                                        m_cGameModelCollection.Colliders(),
                                        m_cGameModelCollection.GetPhysicsBodyStore(),
                                        physics,
                                        m_cGameModelCollection.RenderPresentationRecords(),
                                        m_cGameModelCollection.GetCollisionVisualContacts(),
                                        m_cGameModelCollection.GetSleepStates(),
                                        m_cGameModelCollection.GetSleepIslandVisualIds(),
                                        m_cGameModelCollection.GetSleepSupportedStates(),
                                        m_cGameModelCollection.GetSleepInhibitedStates(),
                                        m_cGameModelCollection.GetPhysicsDebugContacts(),
                                        m_cGameModelCollection.GetPhysicsPipelineTrace(),
                                        m_cGameModelCollection.RenderWorkerPool(),
                                        m_cGameModelCollection.GetModelCount(),
                                        m_cGameModelCollection.ShouldRenderCollisionVolumes(),
                                        m_cGameModelCollection.ShouldUseShadowParallelPrep(),
                                        m_cGameModelCollection.GetSceneKineticEnergy(),
                                        m_cGameModelCollection.GetTornadoSystemElapsedSeconds(),
                                        m_cGameModelCollection.CollectMemoryStats() };
}


void Run::Render( const RuntimeRenderModelFrameView& renderModels )
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
            m_replayRuntime.ApplyPredictionFrameForRender( renderModels.physicsEngine, *predictionFrame );
        }
        else if ( const ReplayPresentationSample* replaySample = m_replayRuntime.CurrentScrubSample() )
        {
            m_replayRuntime.ApplyPresentationSampleForRender( renderModels.physicsEngine, *replaySample );
        }
        else if ( const ReplaySolverFrameSample* solverSample = m_replayRuntime.CurrentSolverScrubSample() )
        {
            m_replayRuntime.ApplySolverSampleForRender( renderModels.physicsEngine, *solverSample );
            applyReplayLauncherVisualSampleForRender( solverSample->launcherVisual );
        }
    };

    const auto restoreReplayRenderStateForFrame = [&]() { restoreReplayLauncherVisualForRender(); };

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
    PROFILE_BEGIN( "Frame/Render/PrepareModels" );
    m_cGameModelCollection.PrepareRenderInstances();
    PROFILE_END( "Frame/Render/PrepareModels" );
    applyReplayRenderStateForFrame();

    m_renderer.RenderFrame( BuildRuntimeRenderInputs( m_systems,
                                                      renderModels,
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
            RenderHelper::ResetRenderResources( m_renderBackendView.renderBackend
                                                    ? &static_cast<SkullbonezCore::Rendering::IRenderResourceFactory&>(
                                                          *m_renderBackendView.renderBackend )
                                                    : nullptr );
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
