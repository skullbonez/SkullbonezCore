/*
File: SkullbonezSource/Runtime/Render/RuntimeRenderer.cpp
Purpose:
  Coordinates render passes for the active scene.

Summary:
  Renderer-facing code builds one frame context and appends named pass
  callbacks to one live graph in the order the image is produced.

Glossary:
  Render pass: Named slice of RuntimeRenderer::RenderPreparedFrame() with explicit
  inputs, outputs, and resource ownership.
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
  Compiled transition: Render-graph resource-state edge emitted immediately
    before its callback records commands.

Invariants:
  - The live RenderGraph owns pass order from world clear through late UI;
    FinalizeFrameGraph adds the sole declaration-only Present edge.
  - Pass resource reset hooks run while the renderer backend is alive, because
    framebuffers, shaders, and dynamic vertex buffers can own backend objects.
  - Pass input/output structs borrow data for one frame only. Do not cache
    pointers returned from ShadowPassOutput or ReflectionPassOutput consumers.
  - UI-text scheduling carries one stack frame record. Font, profiler/timing,
    and ray-tracing presentation capabilities remain inside UiTextPass.

Related:
  - SkullbonezSource/Runtime/Render/RuntimeRenderPasses.h declares pass contracts.
  - SkullbonezSource/Runtime/Render/RuntimeRenderer.h declares the render owner.
  - SkullbonezSource/Rendering/RenderPipeline.h formats the live graph diagnostics.
  - Agentic/Reference/comment-style-guide.md
*/
#include "RuntimeRenderer.h"
#include "../../Rendering/DX12/RenderBackendDX12.h"
#include "../../Assets/AssetKeys.h"
#include "RuntimeRenderPasses.h"
#include "../Camera/CameraCollection.h"
#include "../Camera/CameraControlState.h"
#include "../App/RunTimerState.h"
#include "../Diagnostics/RuntimeDiagnostics.h"
#include "../Diagnostics/RuntimeOverlayDiagnostics.h"
#include "../App/Window.h"
#include "../Tools/RuntimeTools.h"
#include "../Debug/CollisionVisualizer.h"
#include "../Scene/SceneTerrain.h"
#include "../../Core/Allocation/RuntimeAllocationTracker.h"
#include "../../Core/Allocation/RuntimeReserveAllocator.h"
#include "../Replay/ReplayOverlayPackets.h"
#include "../Interaction/OperatorCommandApplier.h"
#include "../Scene/SceneController.h"
#include "../../Assets/TextureCollection.h"
#include "../../Core/FatalError.h"
#include "../../Core/Log.h"
#include "../../Core/Profiler.h"
#include "../../Rendering/PrimitiveBatchRenderer.h"
#include "../../Rendering/DX12/Dx12Diagnostics.h"
#include "../../Rendering/DX12/Dx12FrameOwner.h"
#include "../../Rendering/DX12/RenderDeviceDX12.h"
#include "../../Rendering/RenderGraph.h"
#include "../../Rendering/RenderPipeline.h"
#include "../../UI/UI.h"
#include "../../World/SkyBox.h"
#include "../../World/Terrain.h"
#include "../../World/WorldEnvironment.h"
#if defined( SKULLBONEZ_DEVELOPMENT_TOOLS )
#include "../DevelopmentTools/ImGuiEditorOwner.h"
#endif

#include <algorithm>
#include <chrono>
#include <cstdio>

using SkullbonezCore::Rendering::PrimitiveBatchRenderer;
using SkullbonezCore::Rendering::PrimitiveRenderContext;
#include <cstddef>
#include <fstream>
#include <variant>
#include <vector>

using namespace SkullbonezCore::Runtime;
using namespace SkullbonezCore::Math::CollisionDetection;
using namespace SkullbonezCore::Math::Orientation;
using namespace SkullbonezCore::Math::Transformation;
using namespace SkullbonezCore::Physics;
using namespace SkullbonezCore::Runtime::RunInternal;
namespace Math = SkullbonezCore::Math;
namespace Physics = SkullbonezCore::Physics;
namespace Rendering = SkullbonezCore::Rendering;
namespace CoreAllocation = SkullbonezCore::Core::Allocation;
using SkullbonezCore::Math::Vector::Vector3;

namespace
{
float SampleWorldSurfaceHeight( SkullbonezCore::Geometry::Terrain& surface, float x, float z, float fallback )
{
    return surface.IsInBounds( x, z ) ? surface.GetTerrainHeightAt( x, z ) : fallback;
}

// Concept: RenderGraph callback payloads are RuntimeRenderer-owned one-frame
// scratch records. The graph API invokes C-style callbacks, so each payload
// carries only the pass object and frame/view borrows needed by that pass.
//
// Lifetime: payloads are stack objects consumed during the same RenderPreparedFrame()
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
    const SkullbonezCore::Rendering::RenderGraphCompileResult* compiled = nullptr;
    SkullbonezCore::Rendering::FramebufferDX12* sceneTarget = nullptr;
    SkullbonezCore::Rendering::RenderGraphTextureBinding volumetricLight;
    size_t volumetricTransitionCount = 0;
    size_t tonemapTransitionCount = 0;
    bool volumetricRendered = false;
    bool sceneTargetUnbound = false;
};

struct ShadowGraphCallbackData
{
    ShadowPass* shadowPass = nullptr;
    const RenderFrameContext* frame = nullptr;
    const SkullbonezCore::Core::CinematicRenderConfig* cinematic = nullptr;
    bool terrainHidden = false;
    bool collisionVisualizerVisible = false;
    const SkullbonezCore::Rendering::RenderGraphCompileResult* compiled = nullptr;
    size_t expectedTransitionCount = 0;
    ShadowPassOutput output;
};

struct ReflectionGraphCallbackData
{
    ReflectionPass* reflectionPass = nullptr;
    SkyPass* skyPass = nullptr;
    const RenderFrameContext* frame = nullptr;
    const SkullbonezCore::Core::CinematicRenderConfig* cinematic = nullptr;
    const SkullbonezCore::Rendering::ShadowFrameData* objectShadow = nullptr;
    bool waterRayTracingReflection = false;
    bool waterNoReflection = false;
    bool collisionStateColorsVisible = false;
    bool transparentBodyPass = false;
    float collisionVisualizerAlphaOverride = -1.0f;
    float bodyAlpha = 1.0f;
    float simulationTimeSeconds = 0.0f;
    const SkullbonezCore::Rendering::RenderGraphCompileResult* compiled = nullptr;
    size_t expectedTransitionCount = 0;
    ReflectionPassOutput output;
};

struct ObjectGraphCallbackData
{
    ObjectPass* objectPass = nullptr;
    const RenderFrameContext* frame = nullptr;
    ObjectPassMode mode = ObjectPassMode::Opaque;
    const SkullbonezCore::Core::CinematicRenderConfig* cinematic = nullptr;
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
    const SkullbonezCore::Core::CinematicRenderConfig* cinematic = nullptr;
    const SkullbonezCore::Rendering::ShadowFrameData* shadow = nullptr;
    const SkullbonezCore::Rendering::ShadowFrameData* detailShadow = nullptr;
    bool terrainHidden = false;
};

struct WaterGraphCallbackData
{
    WaterPass* waterPass = nullptr;
    const RenderFrameContext* frame = nullptr;
    const ReflectionPassOutput* reflection = nullptr;
    const SkullbonezCore::Core::CinematicRenderConfig* cinematic = nullptr;
    bool waterHidden = false;
    bool flatWater = false;
    bool noReflection = false;
    bool freezeTime = false;
    float frozenTime = 0.0f;
    float liveWaterTime = 0.0f;
};

struct DebugOverlayGraphCallbackData
{
    DebugOverlayPass* debugOverlayPass = nullptr;
    const RenderFrameContext* frame = nullptr;
    const DebugOverlaySnapshot* snapshot = nullptr;
    RuntimeTools* runtimeTools = nullptr;
    const ReplayVisualPacket* replayVisualPacket = nullptr;
    bool rendered = false;
};

struct SceneTargetGraphCallbackData
{
    SceneTargetPass* sceneTargetPass = nullptr;
    SkyPass* skyPass = nullptr;
    const RenderFrameContext* frame = nullptr;
    const SkullbonezCore::Rendering::RenderGraphCompileResult* compiled = nullptr;
    size_t expectedTransitionCount = 0;
};

struct GraphTransitionCallbackData
{
    SkullbonezCore::Rendering::Dx12GraphTransientPool* renderGraph = nullptr;
    const SkullbonezCore::Rendering::RenderGraphCompileResult* compiled = nullptr;
    size_t expectedTransitionCount = 0;
};

struct BackbufferAcquireGraphCallbackData
{
    SkullbonezCore::Rendering::Dx12GraphTransientPool* renderGraph = nullptr;
    SkullbonezCore::Rendering::Dx12FrameOwner* renderFrame = nullptr;
    const SkullbonezCore::Rendering::RenderGraphCompileResult* compiled = nullptr;
    size_t expectedTransitionCount = 0;
    bool clearFrameTargets = false;
};

struct SkyboxGraphCallbackData
{
    SkyPass* skyPass = nullptr;
    const RenderFrameContext* frame = nullptr;
};

struct UiTextGraphCallbackData
{
    UiTextPass* uiTextPass = nullptr;
    SkullbonezCore::Rendering::Dx12GraphTransientPool* renderGraph = nullptr;
    const UiTextPassInputs* inputs = nullptr;
    const SkullbonezCore::Rendering::RenderGraphCompileResult* compiled = nullptr;
    size_t expectedTransitionCount = 0;
};

struct ReplayGhostGraphCallbackData
{
    SkullbonezCore::Core::Profiler* profiler = nullptr;
    const ReplayVisualPacket* replayVisualPacket = nullptr;
    const RenderFrameContext* frame = nullptr;
    const SkullbonezCore::Core::EngineConfig* config = nullptr;
    const SkullbonezCore::Core::CinematicRenderConfig* cinematic = nullptr;
    const SkullbonezCore::Rendering::ShadowFrameData* shadow = nullptr;
};

#if defined( SKULLBONEZ_DEVELOPMENT_TOOLS )
struct DevelopmentUiGraphCallbackData
{
    SkullbonezCore::Runtime::DevelopmentTools::ImGuiEditorOwner* editor = nullptr;
    SkullbonezCore::Rendering::Dx12GraphTransientPool* renderGraph = nullptr;
    const SkullbonezCore::Rendering::RenderGraphCompileResult* compiled = nullptr;
    size_t expectedTransitionCount = 0;
    SkullbonezCore::Core::SbResult status = SkullbonezCore::Core::SbResult::Success();
};
#endif

size_t CountCompiledTransitionsForPass( const SkullbonezCore::Rendering::RenderGraphCompileResult& compiled,
                                        uint32_t passIndex );

size_t ExecuteRequiredGraphTransitions( const SkullbonezCore::Rendering::RenderGraphPassContext& context,
                                        SkullbonezCore::Rendering::Dx12GraphTransientPool* renderGraph,
                                        const SkullbonezCore::Rendering::RenderGraphCompileResult* compiled,
                                        size_t expectedTransitionCount )
{
    if ( expectedTransitionCount == 0 )
    {
        return 0;
    }
    if ( !renderGraph || !compiled || !context.graph )
    {
        SB_FATAL( "RunRender", "Graph callback missing transition execution data." );
    }
    const size_t emitted = renderGraph->ExecuteGraphTransitions( *context.graph, *compiled, context.passIndex );
    if ( emitted != expectedTransitionCount )
    {
        SB_FATAL( "RunRender",
                  "Graph callback emitted the wrong transition count. pass=%s expected=%zu actual=%zu",
                  context.pass ? context.pass->name : "unknown",
                  expectedTransitionCount,
                  emitted );
    }
    return emitted;
}

void ExecuteGraphTransitionCallback( const SkullbonezCore::Rendering::RenderGraphPassContext& context,
                                     GraphTransitionCallbackData& data )
{
    (void)ExecuteRequiredGraphTransitions( context, data.renderGraph, data.compiled, data.expectedTransitionCount );
}

void ExecuteBackbufferAcquireGraphCallback( const SkullbonezCore::Rendering::RenderGraphPassContext& context,
                                            BackbufferAcquireGraphCallbackData& data )
{
    (void)ExecuteRequiredGraphTransitions( context, data.renderGraph, data.compiled, data.expectedTransitionCount );
    if ( data.clearFrameTargets )
    {
        data.renderFrame->Clear( {} );
    }
}

void ExecuteShadowGraphCallback( const SkullbonezCore::Rendering::RenderGraphPassContext& context,
                                 ShadowGraphCallbackData& data )
{
    if ( !data.shadowPass || !data.frame )
    {
        SB_FATAL( "RunRender", "ShadowMapPass graph callback missing execution data." );
    }
    (void)ExecuteRequiredGraphTransitions( context,
                                           data.frame->renderGraph,
                                           data.compiled,
                                           data.expectedTransitionCount );
    data.output =
        data.shadowPass->Render( { *data.frame, data.cinematic, data.terrainHidden, data.collisionVisualizerVisible } );
}

void ExecuteReflectionGraphCallback( const SkullbonezCore::Rendering::RenderGraphPassContext& context,
                                     ReflectionGraphCallbackData& data )
{
    if ( !data.reflectionPass || !data.skyPass || !data.frame )
    {
        SB_FATAL( "RunRender", "ReflectionPass graph callback missing execution data." );
    }
    (void)ExecuteRequiredGraphTransitions( context,
                                           data.frame->renderGraph,
                                           data.compiled,
                                           data.expectedTransitionCount );
    data.output = data.reflectionPass->Render( { *data.frame,
                                                 data.cinematic,
                                                 data.objectShadow,
                                                 data.waterRayTracingReflection,
                                                 data.waterNoReflection,
                                                 data.collisionStateColorsVisible,
                                                 data.transparentBodyPass,
                                                 data.collisionVisualizerAlphaOverride,
                                                 data.bodyAlpha,
                                                 data.simulationTimeSeconds },
                                               *data.skyPass );
}

void ExecuteObjectGraphCallback( const SkullbonezCore::Rendering::RenderGraphPassContext& /*context*/,
                                 ObjectGraphCallbackData& data )
{
    if ( !data.objectPass || !data.frame )
    {
        SB_FATAL( "RunRender", "ObjectPass graph callback missing execution data." );
    }
    data.objectPass->Render( { *data.frame,
                               data.mode,
                               data.cinematic,
                               data.shadow,
                               data.collisionStateColorsVisible,
                               data.collisionVisualizerAlphaOverride,
                               data.bodyAlpha,
                               data.modelMask,
                               data.drawMaskedModels } );
}

void ExecuteTerrainGraphCallback( const SkullbonezCore::Rendering::RenderGraphPassContext& /*context*/,
                                  TerrainGraphCallbackData& data )
{
    if ( !data.terrainPass || !data.frame )
    {
        SB_FATAL( "RunRender", "TerrainPass graph callback missing execution data." );
    }
    const float* clipPlane = data.frame->primitiveBatches ? data.frame->primitiveBatches->GetClipPlane() : nullptr;
    data.terrainPass->Render(
        { *data.frame, data.cinematic, data.shadow, data.detailShadow, clipPlane, data.terrainHidden } );
}

void ExecuteWaterGraphCallback( const SkullbonezCore::Rendering::RenderGraphPassContext& /*context*/,
                                WaterGraphCallbackData& data )
{
    if ( !data.waterPass || !data.frame || !data.reflection )
    {
        SB_FATAL( "RunRender", "WaterPass graph callback missing execution data." );
    }
    data.waterPass->Render( { *data.frame,
                              *data.reflection,
                              data.cinematic,
                              data.waterHidden,
                              data.flatWater,
                              data.noReflection,
                              data.freezeTime,
                              data.frozenTime,
                              data.liveWaterTime } );
}

void ExecuteDebugOverlayGraphCallback( const SkullbonezCore::Rendering::RenderGraphPassContext& /*context*/,
                                       DebugOverlayGraphCallbackData& data )
{
    if ( !data.debugOverlayPass || !data.frame || !data.snapshot || !data.runtimeTools || !data.replayVisualPacket )
    {
        SB_FATAL( "RunRender", "DebugOverlayPass graph callback missing execution data." );
    }
    data.rendered =
        data.debugOverlayPass->Render( { *data.frame, *data.snapshot, *data.runtimeTools, *data.replayVisualPacket } );
}

void RenderReplayPredictionGhosts( const ReplayVisualPacket& visualPacket,
                                   SkullbonezCore::Core::Profiler* profiler,
                                   const RenderFrameContext& frame,
                                   const SkullbonezCore::Core::EngineConfig& config,
                                   const SkullbonezCore::Core::CinematicRenderConfig* cinematic,
                                   const Rendering::ShadowFrameData* shadow )
{
    PROFILE_SCOPED( profiler, "Frame/Render/ReplayPredictionGhosts" );
    if ( visualPacket.ghostRequests.empty() )
    {
        return;
    }

    // Why: ghost drawing is a render projection path. Shape and material come
    // from the prepared store snapshots so replay visualization does not need
    // the legacy object record collider mirror to stay fresh after physics steps.
    if ( !frame.colliders || !frame.renderInstances )
    {
        return;
    }
    const auto colliders = frame.colliders->Records();
    const auto renderInstances = frame.renderInstances->Records();

    assert( frame.textures && "RenderFrameContext requires a texture collection" );
    const SkullbonezCore::Core::SbResult textureResult = frame.textures->SelectTexture( TEXTURE_BOUNDING_SPHERE );
    if ( !textureResult.ok )
    {
        std::fprintf( stderr,
                      "Frame/Render/ReplayPredictionGhosts texture failure [%s]: %s\n",
                      textureResult.error.owner,
                      textureResult.error.message );
        return;
    }
    assert( frame.renderResources && frame.renderTextures && frame.renderGeometry && frame.renderDiagnostics &&
            frame.assets && frame.primitiveBatches );
    const PrimitiveRenderContext primitiveContext{ *frame.renderResources,
                                                   *frame.renderTextures,
                                                   *frame.renderGeometry,
                                                   *frame.renderDiagnostics,
                                                   *frame.assets,
                                                   config,
                                                   *frame.primitiveBatches };
    auto boxBatch = primitiveContext.renderer.BeginBoxBatch( primitiveContext,
                                                             frame.baseView,
                                                             frame.projection,
                                                             frame.lightPosition,
                                                             true,
                                                             cinematic,
                                                             shadow,
                                                             1.0f );

    for ( const ReplayPredictionGhostDrawRequest& request : visualPacket.ghostRequests )
    {
        if ( request.modelRow.value < 0 || request.modelRow.value >= static_cast<int>( colliders.size() ) ||
             request.modelRow.value >= static_cast<int>( renderInstances.size() ) )
        {
            continue;
        }

        const std::size_t modelIndex = static_cast<std::size_t>( request.modelRow.value );
        const Physics::ColliderRecord& collider = colliders[modelIndex];
        const Math::CollisionDetection::BoundingBox* box =
            std::get_if<Math::CollisionDetection::BoundingBox>( &collider.shape );
        if ( !box )
        {
            continue;
        }

        Rendering::RenderMaterial material = renderInstances[modelIndex].material;
        if ( request.tintStrength > 0.0f )
        {
            // Why: baseline ghosts reuse authored materials for shape/lighting,
            // then tint toward cyan so the cold future separates from the warm
            // live prediction without adding a second render path.
            const float tint = std::clamp( request.tintStrength, 0.0f, 1.0f );
            material.baseColor[0] = material.baseColor[0] * ( 1.0f - tint ) + request.tintR * tint;
            material.baseColor[1] = material.baseColor[1] * ( 1.0f - tint ) + request.tintG * tint;
            material.baseColor[2] = material.baseColor[2] * ( 1.0f - tint ) + request.tintB * tint;
        }
        material.baseColor[3] = request.alpha;
        const Math::Transformation::Matrix4 modelMatrix =
            box->GetModelMatrix( request.position,
                                 Math::Transformation::Matrix4::FromQuaternion( request.orientation ) );
        boxBatch.DrawModel( modelMatrix, material );
    }
}

void ExecuteReplayGhostGraphCallback( const SkullbonezCore::Rendering::RenderGraphPassContext& /*context*/,
                                      ReplayGhostGraphCallbackData& data )
{
    if ( !data.replayVisualPacket || !data.frame || !data.config )
    {
        SB_FATAL( "RunRender", "ReplayPredictionGhostPass graph callback missing execution data." );
    }
    RenderReplayPredictionGhosts( *data.replayVisualPacket,
                                  data.profiler,
                                  *data.frame,
                                  *data.config,
                                  data.cinematic,
                                  data.shadow );
}

#if defined( SKULLBONEZ_DEVELOPMENT_TOOLS )
void ExecuteDevelopmentUiGraphCallback( const SkullbonezCore::Rendering::RenderGraphPassContext& context,
                                        DevelopmentUiGraphCallbackData& data )
{
    if ( !data.editor )
    {
        SB_FATAL( "RunRender", "ImGuiEditorPass graph callback missing its presentation owner." );
    }
    (void)ExecuteRequiredGraphTransitions( context, data.renderGraph, data.compiled, data.expectedTransitionCount );
    data.status = data.editor->RenderPreparedDrawData();
}
#endif

void ExecuteSceneTargetGraphCallback( const SkullbonezCore::Rendering::RenderGraphPassContext& context,
                                      SceneTargetGraphCallbackData& data )
{
    if ( !data.sceneTargetPass || !data.skyPass || !data.frame )
    {
        SB_FATAL( "RunRender", "CinematicSceneBegin graph callback missing execution data." );
    }
    (void)ExecuteRequiredGraphTransitions( context,
                                           data.frame->renderGraph,
                                           data.compiled,
                                           data.expectedTransitionCount );
    data.sceneTargetPass->Begin( *data.frame, *data.skyPass );
}

void ExecuteSkyboxGraphCallback( const SkullbonezCore::Rendering::RenderGraphPassContext& /*context*/,
                                 SkyboxGraphCallbackData& data )
{
    if ( !data.skyPass || !data.frame )
    {
        SB_FATAL( "RunRender", "SkyboxPass graph callback missing execution data." );
    }
    data.skyPass->Render( *data.frame, data.frame->baseView, SkyPassMode::CubemapOnly );
}

void ExecuteUiTextGraphCallback( const SkullbonezCore::Rendering::RenderGraphPassContext& context,
                                 UiTextGraphCallbackData& data )
{
    if ( !data.uiTextPass || !data.renderGraph || !data.inputs || !data.inputs->uiRender.IsReady() )
    {
        SB_FATAL( "RunRender", "UiTextPass graph callback missing execution data." );
    }
    (void)ExecuteRequiredGraphTransitions( context, data.renderGraph, data.compiled, data.expectedTransitionCount );
    data.uiTextPass->Render( *data.inputs );
}

void ExecuteVolumetricGraphCallback( const SkullbonezCore::Rendering::RenderGraphPassContext& context,
                                     CinematicPostGraphCallbackData& data )
{
    if ( !data.volumetricPass || !data.frame || !data.compiled || !context.graph )
    {
        SB_FATAL( "RunRender", "VolumetricLightPass graph callback missing execution data." );
    }
    const SkullbonezCore::Rendering::RenderGraphTextureBinding* graphOutput =
        data.volumetricLight.IsValid() ? &data.volumetricLight : nullptr;
    if ( !data.sceneTarget || !data.frame->renderGraph )
    {
        SB_FATAL( "RunRender", "VolumetricLightPass missing the cinematic target binding." );
    }
    data.sceneTarget->Unbind();
    data.sceneTargetUnbound = true;
    const size_t expectedTransitions = graphOutput ? 3u : 2u;
    data.volumetricTransitionCount =
        data.frame->renderGraph->ExecuteGraphTransitions( *context.graph, *data.compiled, context.passIndex );
    if ( data.volumetricTransitionCount != expectedTransitions )
    {
        // Hazard: sampling the cinematic scene or binding the transient without
        // all compiled producer edges would record an invalid command stream.
        SB_FATAL( "RunRender",
                  "VolumetricLightPass compiled transition count mismatch. expected=%zu actual=%zu",
                  expectedTransitions,
                  data.volumetricTransitionCount );
    }
    data.volumetricRendered = data.volumetricPass->Render( *data.frame, graphOutput );
}

void ExecuteTonemapGraphCallback( const SkullbonezCore::Rendering::RenderGraphPassContext& context,
                                  CinematicPostGraphCallbackData& data )
{
    if ( !data.tonemapPass || !data.frame || !data.compiled || !context.graph )
    {
        SB_FATAL( "RunRender", "ToneMapPass graph callback missing execution data." );
    }
    const SkullbonezCore::Rendering::RenderGraphTextureBinding* graphVolumetric =
        ( data.volumetricRendered && data.volumetricLight.IsValid() ) ? &data.volumetricLight : nullptr;
    const bool sceneNeedsPublish = !data.sceneTargetUnbound;
    if ( sceneNeedsPublish )
    {
        if ( !data.sceneTarget )
        {
            SB_FATAL( "RunRender", "ToneMapPass missing the cinematic target binding." );
        }
        data.sceneTarget->Unbind();
        data.sceneTargetUnbound = true;
    }
    const size_t requiredSceneTransitions = ( graphVolumetric ? 1u : 0u ) + ( sceneNeedsPublish ? 2u : 0u );
    const size_t expectedTransitions = CountCompiledTransitionsForPass( *data.compiled, context.passIndex );
    if ( expectedTransitions < requiredSceneTransitions )
    {
        SB_FATAL( "RunRender",
                  "ToneMapPass omitted a required scene transition. required=%zu compiled=%zu",
                  requiredSceneTransitions,
                  expectedTransitions );
    }
    if ( expectedTransitions > 0 )
    {
        data.tonemapTransitionCount =
            data.frame->renderGraph->ExecuteGraphTransitions( *context.graph, *data.compiled, context.passIndex );
        if ( data.tonemapTransitionCount != expectedTransitions )
        {
            // Hazard: tonemap must not sample until the compiler-selected
            // consumer edge changes the transient from output to shader read.
            SB_FATAL( "RunRender",
                      "ToneMapPass compiled transition count mismatch. expected=%zu actual=%zu",
                      expectedTransitions,
                      data.tonemapTransitionCount );
        }
    }
    data.tonemapPass->Render( *data.frame, true, data.volumetricRendered, graphVolumetric );
}

void WriteCinematicPostGraphEvidence(
    const SkullbonezCore::Rendering::RenderGraph& graph,
    const SkullbonezCore::Rendering::RenderGraphCompileResult& compiled,
    const SkullbonezCore::Rendering::RenderGraphTransientMaterializationStats& materialization,
    const SkullbonezCore::Rendering::RenderGraphTextureBinding& volumetricBinding,
    bool volumetricDeclared,
    size_t volumetricTransitionCount,
    size_t tonemapTransitionCount )
{
    // Why: this human-readable file is diagnostic evidence, not frame storage.
    // The allocation phase must match that policy even when the cinematic post
    // graph is emitted from inside the Render frame scope.
    CoreAllocation::RuntimeAllocationScope diagnosticsAllocationScope(
        CoreAllocation::RuntimeAllocationPhase::Diagnostics );
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
    out << "  volumetric_compiled_transitions_emitted=" << volumetricTransitionCount << "\n";
    out << "  tonemap_compiled_transitions_emitted=" << tonemapTransitionCount << "\n";
    out << "  materialization_failed=" << ( materialization.failed ? "true" : "false" ) << "\n";
    out << "  materialization_failure_stage=" << materialization.failureStage << "\n";
    out << "  materialization_failure_resource=" << materialization.failureResource << "\n";
    out << "  materialization_failure_hresult=0x" << std::hex << materialization.failureHresult << std::dec << "\n";
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

SkullbonezCore::Rendering::RenderGraphResourceHandle
AddBackbufferResource( SkullbonezCore::Rendering::RenderGraph& graph,
                       SkullbonezCore::Rendering::Dx12GraphTransientPool& renderGraph )
{
    const SkullbonezCore::Rendering::RenderGraphBackbufferBinding binding = renderGraph.ResolveGraphBackbufferBinding();
    if ( !binding.IsValid() )
    {
        SB_FATAL( "RunRender", "Executable backbuffer graph pass has no current backend binding." );
    }
    return graph.AddExternalResource( "SwapchainBackbuffer", binding.currentAccess, binding.nativeResource );
}

size_t CountCompiledTransitionsForPass( const SkullbonezCore::Rendering::RenderGraphCompileResult& compiled,
                                        uint32_t passIndex )
{
    size_t count = 0;
    for ( const SkullbonezCore::Rendering::RenderGraphTransitionDesc& transition : compiled.transitions )
    {
        if ( transition.passIndex == passIndex )
        {
            ++count;
        }
    }
    return count;
}

void ExecuteGraphCallbacksOrFatal( const SkullbonezCore::Rendering::RenderGraph& graph,
                                   uint32_t expectedPassCount,
                                   const char* owner )
{
    const size_t passCount = graph.Passes().size();
    if ( expectedPassCount > passCount )
    {
        SB_FATAL( "RunRender",
                  "Executable graph callback range underflow. owner=%s expected=%u passes=%zu",
                  owner ? owner : "unknown",
                  expectedPassCount,
                  passCount );
    }
    const uint32_t firstPass = static_cast<uint32_t>( passCount - expectedPassCount );
    graph.ExecuteCallbacks( SkullbonezCore::Rendering::RenderGraphCallbackExecutionMode::DryRun,
                            firstPass,
                            expectedPassCount );
    const SkullbonezCore::Rendering::RenderGraphCallbackExecutionResult executed =
        graph.ExecuteCallbacks( SkullbonezCore::Rendering::RenderGraphCallbackExecutionMode::Execute,
                                firstPass,
                                expectedPassCount );
    if ( executed.executedPassCount != expectedPassCount )
    {
        SB_FATAL( "RunRender",
                  "Executable graph omitted a callback. owner=%s expected=%u actual=%u",
                  owner ? owner : "unknown",
                  expectedPassCount,
                  executed.executedPassCount );
    }
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

struct GraphFramebufferResources
{
    SkullbonezCore::Rendering::RenderGraphResourceHandle color;
    SkullbonezCore::Rendering::RenderGraphResourceHandle depth;
    size_t transitionCount = 0;
};

GraphFramebufferResources AddGraphFramebuffer( SkullbonezCore::Rendering::RenderGraph& graph,
                                               const RenderFrameContext& frame,
                                               const Rendering::FramebufferDX12* target,
                                               const char* colorName,
                                               const char* depthName )
{
    GraphFramebufferResources resources;
    if ( !target || !frame.renderGraph )
    {
        return resources;
    }
    resources.color =
        graph.AddExternalResource( colorName,
                                   Rendering::RenderGraphResourceAccess::PixelShaderResource,
                                   frame.renderGraph->ResolveGraphResourceToken( target->GetColorTextureHandle() ) );
    resources.depth =
        graph.AddExternalResource( depthName,
                                   Rendering::RenderGraphResourceAccess::PixelShaderResource,
                                   frame.renderGraph->ResolveGraphResourceToken( target->GetDepthTextureHandle() ) );
    resources.transitionCount = 2;
    return resources;
}

void AddFramebufferWrites( Rendering::RenderGraph& graph, uint32_t pass, const GraphFramebufferResources& resources )
{
    if ( resources.transitionCount == 0 )
    {
        return;
    }
    graph.AddWrite( pass, resources.color, Rendering::RenderGraphResourceAccess::RenderTarget );
    graph.AddWrite( pass, resources.depth, Rendering::RenderGraphResourceAccess::DepthWrite );
}

void AddFramebufferReads( Rendering::RenderGraph& graph, uint32_t pass, const GraphFramebufferResources& resources )
{
    if ( resources.transitionCount == 0 )
    {
        return;
    }
    graph.AddRead( pass, resources.color, Rendering::RenderGraphResourceAccess::PixelShaderResource );
    graph.AddRead( pass, resources.depth, Rendering::RenderGraphResourceAccess::PixelShaderResource );
}

} // namespace

void RuntimeRenderer::ExecuteBackbufferAcquireThroughRenderGraph( const BackbufferAcquireGraphInputs& inputs )
{
    Rendering::RenderGraph& graph = BeginRenderPassGraph();
    const Rendering::RenderGraphResourceHandle backbuffer = AddBackbufferResource( graph, inputs.renderGraph );
    const uint32_t acquirePass = graph.AddPass( inputs.clearFrameTargets ? "BackbufferClear" : "UiTargetAcquire" );
    graph.AddWrite( acquirePass, backbuffer, Rendering::RenderGraphResourceAccess::RenderTarget );
    if ( inputs.clearFrameTargets )
    {
        const Rendering::RenderGraphResourceHandle mainDepth =
            graph.AddExternalResource( "MainDepthStencil", Rendering::RenderGraphResourceAccess::DepthWrite );
        graph.AddWrite( acquirePass, mainDepth, Rendering::RenderGraphResourceAccess::DepthWrite );
    }

    BackbufferAcquireGraphCallbackData callbackData;
    callbackData.renderGraph = &inputs.renderGraph;
    callbackData.renderFrame = &inputs.renderFrame;
    callbackData.clearFrameTargets = inputs.clearFrameTargets;
    graph.SetPassCallback<ExecuteBackbufferAcquireGraphCallback>(
        acquirePass,
        callbackData,
        true,
        inputs.clearFrameTargets ? "Frame/BackbufferClear" : "Frame/UI/TargetAcquire" );

    const Rendering::RenderGraphCompileResult& compiled = CompileRenderPassGraph( graph );
    callbackData.compiled = &compiled;
    callbackData.expectedTransitionCount = CountCompiledTransitionsForPass( compiled, acquirePass );
    ExecuteGraphCallbacksOrFatal( graph, 1u, inputs.clearFrameTargets ? "BackbufferClear" : "UiTargetAcquire" );
}

ShadowPassOutput RuntimeRenderer::ExecuteShadowThroughRenderGraph( const ShadowGraphInputs& inputs )
{
    const RenderFrameContext& frame = inputs.frame;
    Rendering::RenderGraph& graph = BeginRenderPassGraph();
    const GraphFramebufferResources terrainShadow =
        AddGraphFramebuffer( graph,
                             frame,
                             m_resources.PassResources().shadows.terrainTarget.get(),
                             "TerrainShadowMapColor",
                             "TerrainShadowMapDepth" );
    const GraphFramebufferResources objectShadow =
        AddGraphFramebuffer( graph,
                             frame,
                             m_resources.PassResources().shadows.objectTarget.get(),
                             "ObjectShadowMapColor",
                             "ObjectShadowMapDepth" );
    const size_t targetTransitionCount = terrainShadow.transitionCount + objectShadow.transitionCount;

    const uint32_t shadowPass = graph.AddPass( "ShadowMapPass", Rendering::RenderGraphQueueType::Graphics );
    AddFramebufferWrites( graph, shadowPass, terrainShadow );
    AddFramebufferWrites( graph, shadowPass, objectShadow );
    if ( targetTransitionCount == 0 )
    {
        // Disabled shadows still schedule the callback that clears stale CPU
        // receiver payloads; this stable no-transition row satisfies the graph
        // callback resource contract without pretending a GPU target exists.
        const Rendering::RenderGraphResourceHandle inactive =
            graph.AddExternalResource( "ShadowPassInactive",
                                       Rendering::RenderGraphResourceAccess::PixelShaderResource );
        graph.AddRead( shadowPass, inactive, Rendering::RenderGraphResourceAccess::PixelShaderResource );
    }

    ShadowGraphCallbackData callbackData;
    callbackData.shadowPass = &m_shadowPass;
    callbackData.frame = &frame;
    callbackData.cinematic = inputs.cinematic;
    callbackData.terrainHidden = inputs.terrainHidden;
    callbackData.collisionVisualizerVisible = inputs.collisionVisualizerVisible;
    callbackData.expectedTransitionCount = targetTransitionCount;
    graph.SetPassCallback<ExecuteShadowGraphCallback>( shadowPass, callbackData, true, "Frame/Shadows/ShadowMap" );

    GraphTransitionCallbackData publishData;
    uint32_t publishPass = 0;
    if ( targetTransitionCount > 0 )
    {
        publishPass = graph.AddPass( "ShadowMapPublishPass", Rendering::RenderGraphQueueType::Graphics );
        AddFramebufferReads( graph, publishPass, terrainShadow );
        AddFramebufferReads( graph, publishPass, objectShadow );
        publishData.renderGraph = frame.renderGraph;
        publishData.expectedTransitionCount = targetTransitionCount;
        graph.SetPassCallback<ExecuteGraphTransitionCallback>( publishPass,
                                                               publishData,
                                                               true,
                                                               "Frame/Shadows/ShadowMapPublish" );
    }

    // Invariant: this wrapper is called only for an active shadow configuration.
    // Disabled scenes clear receiver payloads without adding graph work.
    const Rendering::RenderGraphCompileResult& compiled = CompileRenderPassGraph( graph );
    callbackData.compiled = &compiled;
    publishData.compiled = &compiled;
    ExecuteGraphCallbacksOrFatal( graph, targetTransitionCount > 0 ? 2u : 1u, "ShadowMap" );
    return callbackData.output;
}

void RuntimeRenderer::ExecuteSkyboxThroughRenderGraph( const RenderFrameContext& frame )
{
    Rendering::RenderGraph& graph = BeginRenderPassGraph();
    const Rendering::RenderGraphResourceHandle backbuffer = AddBackbufferResource( graph, *frame.renderGraph );

    const uint32_t skyboxPass = graph.AddPass( "SkyboxPass", Rendering::RenderGraphQueueType::Graphics );
    graph.AddWrite( skyboxPass, backbuffer, Rendering::RenderGraphResourceAccess::RenderTarget );

    SkyboxGraphCallbackData callbackData;
    callbackData.skyPass = &m_skyPass;
    callbackData.frame = &frame;
    graph.SetPassCallback<ExecuteSkyboxGraphCallback>( skyboxPass, callbackData, true, "Frame/Render/Skybox" );

    // Invariant: the ordinary skybox still draws through SkyPass; the graph now
    // owns the scheduling point and resource declaration before live execution.
    CompileRenderPassGraph( graph );
    ExecuteGraphCallbacksOrFatal( graph, 1u, "Skybox" );
}


ReflectionPassOutput RuntimeRenderer::ExecuteReflectionThroughRenderGraph( const ReflectionGraphInputs& inputs )
{
    const RenderFrameContext& frame = inputs.frame;
    Rendering::RenderGraph& graph = BeginRenderPassGraph();
    const bool useDxrCandidate = frame.renderDiagnostics && frame.renderRayTracing &&
                                 frame.renderDiagnostics->GetCapabilities().supportsDxrReflection &&
                                 inputs.waterRayTracingReflection && !inputs.waterNoReflection &&
                                 !inputs.collisionStateColorsVisible && !inputs.transparentBodyPass;

    Rendering::RenderGraphResourceHandle objectShadowResource;
    if ( inputs.objectShadow && inputs.objectShadow->valid )
    {
        objectShadowResource = graph.AddExternalResource( "ObjectShadowMapDepth",
                                                          Rendering::RenderGraphResourceAccess::PixelShaderResource );
    }

    const uint32_t reflectionPass = graph.AddPass(
        useDxrCandidate ? "DxrReflectionPass" : "RasterReflectionPass",
        useDxrCandidate ? Rendering::RenderGraphQueueType::Compute : Rendering::RenderGraphQueueType::Graphics );
    if ( objectShadowResource.IsValid() )
    {
        graph.AddRead( reflectionPass,
                       objectShadowResource,
                       Rendering::RenderGraphResourceAccess::PixelShaderResource );
    }
    GraphFramebufferResources rasterReflection;
    Rendering::RenderGraphResourceHandle producedReflection;
    size_t targetTransitionCount = 0;
    if ( useDxrCandidate )
    {
        const uint32_t reflectionHandle = frame.renderRayTracing->GetReflectionUAVTexture();
        producedReflection =
            graph.AddExternalResource( "DxrReflectionTexture",
                                       Rendering::RenderGraphResourceAccess::PixelShaderResource,
                                       frame.renderGraph->ResolveGraphResourceToken( reflectionHandle ) );
        graph.AddWrite( reflectionPass, producedReflection, Rendering::RenderGraphResourceAccess::UnorderedAccess );
        targetTransitionCount = 1;
    }
    else
    {
        rasterReflection = AddGraphFramebuffer( graph,
                                                frame,
                                                m_resources.PassResources().reflection.target.get(),
                                                "RasterReflectionColor",
                                                "RasterReflectionDepth" );
        AddFramebufferWrites( graph, reflectionPass, rasterReflection );
        targetTransitionCount = rasterReflection.transitionCount;
    }
    if ( targetTransitionCount == 0 && !objectShadowResource.IsValid() )
    {
        const Rendering::RenderGraphResourceHandle inactive =
            graph.AddExternalResource( "ReflectionPassInactive",
                                       Rendering::RenderGraphResourceAccess::PixelShaderResource );
        graph.AddRead( reflectionPass, inactive, Rendering::RenderGraphResourceAccess::PixelShaderResource );
    }

    ReflectionGraphCallbackData callbackData;
    callbackData.reflectionPass = &m_reflectionPass;
    callbackData.skyPass = &m_skyPass;
    callbackData.frame = &frame;
    callbackData.cinematic = inputs.cinematic;
    callbackData.objectShadow = inputs.objectShadow;
    callbackData.waterRayTracingReflection = inputs.waterRayTracingReflection;
    callbackData.waterNoReflection = inputs.waterNoReflection;
    callbackData.collisionStateColorsVisible = inputs.collisionStateColorsVisible;
    callbackData.transparentBodyPass = inputs.transparentBodyPass;
    callbackData.collisionVisualizerAlphaOverride = inputs.collisionVisualizerAlphaOverride;
    callbackData.bodyAlpha = inputs.bodyAlpha;
    callbackData.simulationTimeSeconds = inputs.simulationTimeSeconds;
    callbackData.expectedTransitionCount = targetTransitionCount;
    graph.SetPassCallback<ExecuteReflectionGraphCallback>(
        reflectionPass,
        callbackData,
        true,
        useDxrCandidate ? "Frame/Render/Reflection/DXR" : "Frame/Render/Reflection/Raster" );

    GraphTransitionCallbackData publishData;
    if ( targetTransitionCount > 0 )
    {
        const uint32_t publishPass =
            graph.AddPass( "ReflectionPublishPass", Rendering::RenderGraphQueueType::Graphics );
        if ( useDxrCandidate )
        {
            graph.AddRead( publishPass, producedReflection, Rendering::RenderGraphResourceAccess::PixelShaderResource );
        }
        else
        {
            AddFramebufferReads( graph, publishPass, rasterReflection );
        }
        publishData.renderGraph = frame.renderGraph;
        publishData.expectedTransitionCount = targetTransitionCount;
        graph.SetPassCallback<ExecuteGraphTransitionCallback>( publishPass,
                                                               publishData,
                                                               true,
                                                               "Frame/Render/ReflectionPublish" );
    }

    // Invariant: reflection still chooses DXR or raster in ReflectionPass using
    // the same runtime conditions. The graph now owns the scheduling point and
    // declares the texture family that WaterPass samples later.
    const Rendering::RenderGraphCompileResult& compiled = CompileRenderPassGraph( graph );
    callbackData.compiled = &compiled;
    publishData.compiled = &compiled;
    ExecuteGraphCallbacksOrFatal( graph, targetTransitionCount > 0 ? 2u : 1u, "Reflection" );
    return callbackData.output;
}


void RuntimeRenderer::ExecuteSceneTargetBeginThroughRenderGraph( const RenderFrameContext& frame )
{
    Rendering::RenderGraph& graph = BeginRenderPassGraph();
    const GraphFramebufferResources sceneTarget =
        AddGraphFramebuffer( graph,
                             frame,
                             m_resources.PassResources().cinematicScene.hdrTarget.get(),
                             "CinematicSceneColor",
                             "CinematicSceneDepth" );

    const uint32_t sceneBeginPass = graph.AddPass( "CinematicSceneBegin", Rendering::RenderGraphQueueType::Graphics );
    AddFramebufferWrites( graph, sceneBeginPass, sceneTarget );

    SceneTargetGraphCallbackData callbackData;
    callbackData.sceneTargetPass = &m_sceneTargetPass;
    callbackData.skyPass = &m_skyPass;
    callbackData.frame = &frame;
    callbackData.expectedTransitionCount = sceneTarget.transitionCount;
    graph.SetPassCallback<ExecuteSceneTargetGraphCallback>( sceneBeginPass,
                                                            callbackData,
                                                            true,
                                                            "Frame/Render/CinematicSceneBegin" );

    // Invariant: the producer callback consumes both compiled edges before
    // binding; the cinematic post graph later publishes both resources for reads.
    const Rendering::RenderGraphCompileResult& compiled = CompileRenderPassGraph( graph );
    callbackData.compiled = &compiled;
    ExecuteGraphCallbacksOrFatal( graph, 1u, "CinematicSceneBegin" );
}


void RuntimeRenderer::ExecuteObjectThroughRenderGraph( const ObjectGraphInputs& inputs )
{
    const RenderFrameContext& frame = inputs.frame;
    const Rendering::ShadowFrameData* objectShadow = inputs.shadow;
    Rendering::RenderGraph& graph = BeginRenderPassGraph();
    Rendering::RenderGraphResourceHandle objectShadowResource;
    if ( objectShadow && objectShadow->valid )
    {
        objectShadowResource = graph.AddExternalResource( "ObjectShadowMapDepth",
                                                          Rendering::RenderGraphResourceAccess::PixelShaderResource );
    }

    const char* passName = inputs.mode == ObjectPassMode::Transparent ? "ObjectTransparentPass" : "ObjectOpaquePass";
    const uint32_t objectPass = graph.AddPass( passName, Rendering::RenderGraphQueueType::Graphics );
    if ( objectShadowResource.IsValid() )
    {
        graph.AddRead( objectPass, objectShadowResource, Rendering::RenderGraphResourceAccess::PixelShaderResource );
    }
    AddFrameTargetWrites( graph, objectPass, inputs.useCinematicTarget );

    ObjectGraphCallbackData callbackData;
    callbackData.objectPass = &m_objectPass;
    callbackData.frame = &frame;
    callbackData.mode = inputs.mode;
    callbackData.cinematic = inputs.cinematic;
    callbackData.shadow = objectShadow;
    callbackData.collisionStateColorsVisible = inputs.collisionStateColorsVisible;
    callbackData.collisionVisualizerAlphaOverride = inputs.collisionVisualizerAlphaOverride;
    callbackData.bodyAlpha = inputs.bodyAlpha;
    callbackData.modelMask = inputs.modelMask;
    callbackData.drawMaskedModels = inputs.drawMaskedModels;
    graph.SetPassCallback<ExecuteObjectGraphCallback>( objectPass,
                                                       callbackData,
                                                       true,
                                                       inputs.mode == ObjectPassMode::Transparent
                                                           ? "Frame/Render/Objects/Transparent"
                                                           : "Frame/Render/Objects/Opaque" );

    // Concept: object draw selection still lives in ObjectPassInputs. The graph
    // owns when that selection is scheduled and which frame target/shadow map
    // resources the selected object pass reads and writes.
    CompileRenderPassGraph( graph );
    ExecuteGraphCallbacksOrFatal( graph, 1u, passName );
}


void RuntimeRenderer::ExecuteTerrainThroughRenderGraph( const TerrainGraphInputs& inputs )
{
    const RenderFrameContext& frame = inputs.frame;
    const Rendering::ShadowFrameData* terrainShadow = inputs.shadow;
    const Rendering::ShadowFrameData* objectShadow = inputs.detailShadow;
    Rendering::RenderGraph& graph = BeginRenderPassGraph();
    Rendering::RenderGraphResourceHandle terrainShadowResource;
    if ( terrainShadow && terrainShadow->valid )
    {
        terrainShadowResource = graph.AddExternalResource( "TerrainShadowMapDepth",
                                                           Rendering::RenderGraphResourceAccess::PixelShaderResource );
    }
    Rendering::RenderGraphResourceHandle objectShadowResource;
    if ( objectShadow && objectShadow->valid )
    {
        objectShadowResource = graph.AddExternalResource( "ObjectShadowMapDepth",
                                                          Rendering::RenderGraphResourceAccess::PixelShaderResource );
    }

    const uint32_t terrainPass = graph.AddPass( "TerrainPass", Rendering::RenderGraphQueueType::Graphics );
    if ( terrainShadowResource.IsValid() )
    {
        graph.AddRead( terrainPass, terrainShadowResource, Rendering::RenderGraphResourceAccess::PixelShaderResource );
    }
    if ( objectShadowResource.IsValid() )
    {
        graph.AddRead( terrainPass, objectShadowResource, Rendering::RenderGraphResourceAccess::PixelShaderResource );
    }
    AddFrameTargetWrites( graph, terrainPass, inputs.useCinematicTarget );

    TerrainGraphCallbackData callbackData;
    callbackData.terrainPass = &m_terrainPass;
    callbackData.frame = &frame;
    callbackData.cinematic = inputs.cinematic;
    callbackData.shadow = terrainShadow;
    callbackData.detailShadow = objectShadow;
    callbackData.terrainHidden = inputs.terrainHidden;
    graph.SetPassCallback<ExecuteTerrainGraphCallback>( terrainPass, callbackData, true, "Frame/Render/Terrain" );

    // Invariant: terrain visibility is snapshotted before graph execution, while
    // the graph owns its frame target and shadow-map declaration.
    CompileRenderPassGraph( graph );
    ExecuteGraphCallbacksOrFatal( graph, 1u, "Terrain" );
}


void RuntimeRenderer::ExecuteWaterThroughRenderGraph( const WaterGraphInputs& inputs )
{
    const RenderFrameContext& frame = inputs.frame;
    const ReflectionPassOutput& reflection = inputs.reflection;
    Rendering::RenderGraph& graph = BeginRenderPassGraph();
    if ( reflection.reflectionTextureHandle != 0u && !inputs.noReflection )
    {
        const Rendering::RenderGraphResourceHandle reflectionTexture =
            graph.AddExternalResource( reflection.usedDxr ? "DxrReflectionTexture" : "RasterReflectionColor",
                                       Rendering::RenderGraphResourceAccess::PixelShaderResource );
        const uint32_t waterPass = graph.AddPass( "WaterPass", Rendering::RenderGraphQueueType::Graphics );
        graph.AddRead( waterPass, reflectionTexture, Rendering::RenderGraphResourceAccess::PixelShaderResource );
        AddFrameTargetWrites( graph, waterPass, inputs.useCinematicTarget );

        WaterGraphCallbackData callbackData;
        callbackData.waterPass = &m_waterPass;
        callbackData.frame = &frame;
        callbackData.reflection = &reflection;
        callbackData.cinematic = inputs.cinematic;
        callbackData.waterHidden = inputs.waterHidden;
        callbackData.flatWater = inputs.flatWater;
        callbackData.noReflection = inputs.noReflection;
        callbackData.freezeTime = inputs.freezeTime;
        callbackData.frozenTime = inputs.frozenTime;
        callbackData.liveWaterTime = inputs.liveWaterTime;
        graph.SetPassCallback<ExecuteWaterGraphCallback>( waterPass, callbackData, true, "Frame/Render/Water" );

        CompileRenderPassGraph( graph );
        ExecuteGraphCallbacksOrFatal( graph, 1u, "Water" );
        return;
    }

    const uint32_t waterPass = graph.AddPass( "WaterPass", Rendering::RenderGraphQueueType::Graphics );
    AddFrameTargetWrites( graph, waterPass, inputs.useCinematicTarget );

    WaterGraphCallbackData callbackData;
    callbackData.waterPass = &m_waterPass;
    callbackData.frame = &frame;
    callbackData.reflection = &reflection;
    callbackData.cinematic = inputs.cinematic;
    callbackData.waterHidden = inputs.waterHidden;
    callbackData.flatWater = inputs.flatWater;
    callbackData.noReflection = inputs.noReflection;
    callbackData.freezeTime = inputs.freezeTime;
    callbackData.frozenTime = inputs.frozenTime;
    callbackData.liveWaterTime = inputs.liveWaterTime;
    graph.SetPassCallback<ExecuteWaterGraphCallback>( waterPass, callbackData, true, "Frame/Render/Water" );

    // Invariant: WaterPass may decide to draw the no-reflection shader path or
    // skip hidden water, but the graph owns that decision point and target
    // declaration every frame.
    CompileRenderPassGraph( graph );
    ExecuteGraphCallbacksOrFatal( graph, 1u, "Water" );
}


bool RuntimeRenderer::ExecuteWorldExtensionThroughRenderGraph( const WorldExtensionGraphInputs& inputs )
{
    Rendering::RenderGraph& graph = BeginRenderPassGraph();
    const Rendering::RenderGraphResourceHandle colorTarget =
        graph.AddExternalResource( inputs.useCinematicTarget ? "CinematicSceneColor" : "SwapchainBackbuffer",
                                   Rendering::RenderGraphResourceAccess::RenderTarget );
    const Rendering::RenderGraphResourceHandle depthTarget =
        graph.AddExternalResource( inputs.useCinematicTarget ? "CinematicSceneDepth" : "MainDepthStencil",
                                   Rendering::RenderGraphResourceAccess::DepthWrite );

    const RenderFrameContext& frame = inputs.frame;
    const Rendering::WorldSurfaceHeightView surfaceHeight =
        m_resources.Terrain().Get()
            ? Rendering::WorldSurfaceHeightView::Bind<Geometry::Terrain, &SampleWorldSurfaceHeight>(
                  *m_resources.Terrain().Get() )
            : Rendering::WorldSurfaceHeightView();
    const Rendering::WorldRenderExtensionFrameView frameView{ frame.viewProjection,
                                                              frame.eye,
                                                              frame.viewCenter,
                                                              frame.up,
                                                              *frame.renderTextures,
                                                              *frame.renderGeometry,
                                                              *frame.renderDiagnostics,
                                                              *frame.renderGpuTiming,
                                                              surfaceHeight };
    Rendering::WorldRenderExtensionScope scope( graph,
                                                m_renderPassCompileScratch,
                                                colorTarget,
                                                depthTarget,
                                                frameView );
    return inputs.registration.Register( scope );
}
void RuntimeRenderer::ExecuteReplayGhostsThroughRenderGraph( const ReplayGhostGraphInputs& inputs )
{
    const RenderFrameContext& frame = inputs.frame;
    const Rendering::ShadowFrameData* objectShadow = inputs.shadow;
    Rendering::RenderGraph& graph = BeginRenderPassGraph();
    Rendering::RenderGraphResourceHandle objectShadowResource;
    if ( objectShadow && objectShadow->valid )
    {
        objectShadowResource = graph.AddExternalResource( "ObjectShadowMapDepth",
                                                          Rendering::RenderGraphResourceAccess::PixelShaderResource );
    }

    const uint32_t replayPass = graph.AddPass( "ReplayPredictionGhostPass", Rendering::RenderGraphQueueType::Graphics );
    if ( objectShadowResource.IsValid() )
    {
        graph.AddRead( replayPass, objectShadowResource, Rendering::RenderGraphResourceAccess::PixelShaderResource );
    }
    AddFrameTargetWrites( graph, replayPass, inputs.useCinematicTarget );

    ReplayGhostGraphCallbackData callbackData;
    callbackData.profiler = m_profiler;
    callbackData.replayVisualPacket = &inputs.replayVisualPacket;
    callbackData.frame = &frame;
    callbackData.config = &m_resources.Config();
    callbackData.cinematic = inputs.cinematic;
    callbackData.shadow = objectShadow;
    graph.SetPassCallback<ExecuteReplayGhostGraphCallback>( replayPass,
                                                            callbackData,
                                                            true,
                                                            "Frame/Render/ReplayPredictionGhosts" );

    // Concept: replay ghost rendering is a presentation overlay, but it still
    // writes world color/depth in the same frame slot as transparent objects.
    // The graph now owns that scheduling point instead of leaving it as an
    // ad hoc host call between migrated pass families.
    CompileRenderPassGraph( graph );
    ExecuteGraphCallbacksOrFatal( graph, 1u, "ReplayPredictionGhost" );
}


bool RuntimeRenderer::ExecuteDebugOverlayThroughRenderGraph( const DebugOverlayGraphInputs& inputs )
{
    const RenderFrameContext& frame = inputs.frame;
    Rendering::RenderGraph& graph = BeginRenderPassGraph();
    const Rendering::RenderGraphResourceHandle colorTarget =
        graph.AddExternalResource( inputs.useCinematicTarget ? "CinematicSceneColor" : "SwapchainBackbuffer",
                                   Rendering::RenderGraphResourceAccess::RenderTarget );
    const Rendering::RenderGraphResourceHandle depthTarget =
        graph.AddExternalResource( inputs.useCinematicTarget ? "CinematicSceneDepth" : "MainDepthStencil",
                                   Rendering::RenderGraphResourceAccess::DepthWrite );

    const uint32_t debugPass = graph.AddPass( "DebugOverlayPass", Rendering::RenderGraphQueueType::Graphics );
    graph.AddWrite( debugPass, colorTarget, Rendering::RenderGraphResourceAccess::RenderTarget );
    graph.AddWrite( debugPass, depthTarget, Rendering::RenderGraphResourceAccess::DepthWrite );

    DebugOverlayGraphCallbackData callbackData;
    callbackData.debugOverlayPass = &m_debugOverlayPass;
    callbackData.frame = &frame;
    callbackData.snapshot = &inputs.snapshot;
    callbackData.runtimeTools = &inputs.runtimeTools;
    callbackData.replayVisualPacket = inputs.replayVisualPacket;
    graph.SetPassCallback<ExecuteDebugOverlayGraphCallback>( debugPass,
                                                             callbackData,
                                                             true,
                                                             "Frame/Render/DebugOverlay" );

    // Invariant: debug overlays are optional inside the pass body, but the pass
    // scheduling itself is now graph-scheduled every frame so direct runtime calls
    // cannot creep back beside post-processing callbacks.
    CompileRenderPassGraph( graph );
    ExecuteGraphCallbacksOrFatal( graph, 1u, "DebugOverlay" );
    return callbackData.rendered;
}


DebugOverlaySnapshot RuntimeRenderer::BuildDebugOverlaySnapshot( const RuntimeRenderModelFrameView& models,
                                                                 const RenderToolOverlayView& toolOverlay,
                                                                 const RuntimeRenderFramePolicy& policy ) const
{
    DebugOverlaySnapshot snapshot;
    snapshot.broadphaseOverlayVisible = policy.broadphaseOverlay;
    snapshot.worldExtensionDebugLines = models.worldExtensionDebugLines;
    snapshot.physicsDebugFlags = policy.physicsDebugFlags;
    snapshot.physicsDebugPipelineStageCursor = policy.physicsDebugPipelineStageCursor;
    snapshot.editorOverlayWorkVisible = toolOverlay.editorOverlayWorkVisible;
    return snapshot;
}


RuntimeRenderer::CinematicPostFrameOutput
RuntimeRenderer::ExecuteCinematicPostThroughRenderGraph( const CinematicPostGraphInputs& inputs )
{
    const RenderFrameContext& frame = inputs.frame;
    Rendering::RenderGraph& graph = BeginRenderPassGraph();
    Rendering::FramebufferDX12* sceneTarget = m_resources.PassResources().cinematicScene.hdrTarget.get();
    const Rendering::RenderGraphResourceHandle sceneColor = graph.AddExternalResource(
        "CinematicSceneColor",
        Rendering::RenderGraphResourceAccess::RenderTarget,
        frame.renderGraph->ResolveGraphResourceToken( sceneTarget->GetColorTextureHandle() ) );
    const Rendering::RenderGraphResourceHandle sceneDepth = graph.AddExternalResource(
        "CinematicSceneDepth",
        Rendering::RenderGraphResourceAccess::DepthWrite,
        frame.renderGraph->ResolveGraphResourceToken( sceneTarget->GetDepthTextureHandle() ) );
    const Rendering::RenderGraphResourceHandle backbuffer = AddBackbufferResource( graph, *frame.renderGraph );
    Rendering::RenderGraphResourceHandle volumetricLight;
    const bool volumetricDeclared = m_volumetricPass.CanRender( frame );
    uint32_t expectedCallbacks = 1u;
    uint32_t volumetricPass = 0u;

    if ( volumetricDeclared )
    {
        Rendering::RenderGraphTransientResourceDesc volumetricDesc;
        volumetricDesc.kind = Rendering::RenderGraphResourceKind::Texture2D;
        volumetricDesc.format = Rendering::RenderGraphResourceFormat::RGBA16F;
        volumetricDesc.width = static_cast<uint32_t>( (std::max)( 1, frame.windowWidth / 2 ) );
        volumetricDesc.height = static_cast<uint32_t>( (std::max)( 1, frame.windowHeight / 2 ) );
        volumetricDesc.mipLevels = 1;
        volumetricDesc.descriptors.renderTarget = true;
        volumetricDesc.descriptors.shaderResource = true;
        volumetricLight = graph.AddTransientResource( "VolumetricLight",
                                                      volumetricDesc,
                                                      Rendering::RenderGraphResourceAccess::PixelShaderResource );

        volumetricPass = graph.AddPass( "VolumetricLightPass", Rendering::RenderGraphQueueType::Graphics );
        graph.AddRead( volumetricPass, sceneColor, Rendering::RenderGraphResourceAccess::PixelShaderResource );
        graph.AddRead( volumetricPass, sceneDepth, Rendering::RenderGraphResourceAccess::PixelShaderResource );
        graph.AddWrite( volumetricPass, volumetricLight, Rendering::RenderGraphResourceAccess::RenderTarget );
        ++expectedCallbacks;
    }

    const uint32_t tonemapPass = graph.AddPass( "ToneMapPass", Rendering::RenderGraphQueueType::Graphics );
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
    callbackData.sceneTarget = sceneTarget;
    if ( volumetricDeclared )
    {
        graph.SetPassCallback<ExecuteVolumetricGraphCallback>( volumetricPass,
                                                               callbackData,
                                                               true,
                                                               "Frame/Render/VolumetricLight" );
    }
    graph.SetPassCallback<ExecuteTonemapGraphCallback>( tonemapPass, callbackData, true, "Frame/Render/Tonemap" );

    // Invariant: dry-run executes no draw code. It proves the callback-owned
    // post passes have resource declarations before live callbacks record
    // commands, and the execute path records them in graph order.
    const Rendering::RenderGraphCompileResult& compiled = CompileRenderPassGraph( graph );
    callbackData.compiled = &compiled;
    Rendering::RenderGraphTransientMaterializationStats transientMaterialization;
    if ( frame.renderGraph )
    {
        transientMaterialization = frame.renderGraph->MaterializeGraphTransientResources( graph, compiled );
        if ( volumetricDeclared )
        {
            callbackData.volumetricLight = frame.renderGraph->ResolveGraphTextureBinding( volumetricLight );
            if ( !callbackData.volumetricLight.IsValid() )
            {
                // Lane R: if graph-managed texture allocation fails, the
                // optional volumetric callback records no draw and tonemap
                // proceeds without its sample. Keep the failure visible.
                SkullbonezCore::Core::Log().WriteEventf(
                    "render_graph_volumetric_transient_unavailable materialization_failed=%d "
                    "hresult=0x%08X resource=%s",
                    transientMaterialization.failed ? 1 : 0,
                    transientMaterialization.failureHresult,
                    transientMaterialization.failureResource );
                SkullbonezCore::Core::Log().FlushAll();
            }
        }
    }
    ExecuteGraphCallbacksOrFatal( graph, expectedCallbacks, "CinematicPost" );
    WriteCinematicPostGraphEvidence( graph,
                                     compiled,
                                     transientMaterialization,
                                     callbackData.volumetricLight,
                                     volumetricDeclared,
                                     callbackData.volumetricTransitionCount,
                                     callbackData.tonemapTransitionCount );

    CinematicPostFrameOutput result;
    result.volumetricPassExecuted = volumetricDeclared;
    result.volumetricReady = volumetricDeclared && callbackData.volumetricRendered;
    result.volumetricTextureHandle = callbackData.volumetricLight.textureHandle;
    result.volumetricWidth = callbackData.volumetricLight.width;
    result.volumetricHeight = callbackData.volumetricLight.height;
    return result;
}


void RuntimeRenderer::ExecuteUiTextThroughRenderGraph( const UiTextPassInputs& inputs )
{
    Rendering::RenderGraph& graph = BeginRenderPassGraph();
    if ( !m_resources.Backend().renderGraph )
    {
        SB_FATAL( "RunRender", "UiTextPass graph has no render command context." );
    }
    const Rendering::RenderGraphResourceHandle backbuffer =
        AddBackbufferResource( graph, *m_resources.Backend().renderGraph );

    const uint32_t uiTextPass = graph.AddPass( "UiTextPass", Rendering::RenderGraphQueueType::Graphics );
    graph.AddWrite( uiTextPass, backbuffer, Rendering::RenderGraphResourceAccess::RenderTarget );

    UiTextGraphCallbackData callbackData;
    callbackData.uiTextPass = &m_resources.UiText();
    callbackData.renderGraph = m_resources.Backend().renderGraph;
    callbackData.inputs = &inputs;
    graph.SetPassCallback<ExecuteUiTextGraphCallback>( uiTextPass, callbackData, true, "Frame/UI" );

    // Invariant: UI/text always lands on the presentable backbuffer. Text-only
    // mode skips world rendering before this point but still uses this callback
    // path so the late overlay pass has one scheduling owner.
    const Rendering::RenderGraphCompileResult& compiled = CompileRenderPassGraph( graph );
    callbackData.compiled = &compiled;
    callbackData.expectedTransitionCount = CountCompiledTransitionsForPass( compiled, uiTextPass );
    ExecuteGraphCallbacksOrFatal( graph, 1u, "UiText" );
}


RenderFrameContext
RuntimeRenderer::BuildRenderFrameContext( const RuntimeRenderModelFrameView& models,
                                          bool cinematicRender,
                                          const SkullbonezCore::Core::CinematicRenderConfig& renderConfig )
{
    RenderFrameContext frame;
    frame.cinematicEnabled = cinematicRender;
    frame.cinematic = cinematicRender ? &renderConfig : nullptr;
    frame.renderInstances = &models.renderInstances;
    frame.colliders = &models.colliders;
    frame.bodyStore = &models.bodyStore;
    frame.physicsEngine = &models.physicsEngine;
    frame.presentationRecords = models.presentationRecords;
    frame.collisionVisualContacts = &models.collisionVisualContacts;
    frame.sleepStates = models.sleepStates;
    frame.sleepIslandVisualIds = models.sleepIslandVisualIds;
    frame.sleepSupportedStates = models.sleepSupportedStates;
    frame.sleepInhibitedStates = models.sleepInhibitedStates;
    frame.physicsDebugContacts = &models.physicsDebugContacts;
    frame.physicsPipelineTrace = &models.physicsPipelineTrace;
    frame.renderWorkerPool = models.renderWorkerPool;
    frame.modelCount = models.modelCount;
    frame.renderCollisionVolumes = models.renderCollisionVolumes;
    frame.shadowParallelPrep = models.shadowParallelPrep;
    frame.sceneKineticEnergy = models.sceneKineticEnergy;
    frame.assets = &m_resources.Assets();
    frame.textures = &m_resources.Textures();
    frame.renderResources = m_resources.Backend().renderResources;
    frame.renderTextures = m_resources.Backend().renderTextures;
    frame.renderGeometry = m_resources.Backend().renderGeometry;
    frame.renderFrame = m_resources.Backend().renderFrame;
    frame.renderGraph = m_resources.Backend().renderGraph;
    frame.renderDiagnostics = m_resources.Backend().renderDiagnostics;
    frame.renderGpuTiming = &m_resources.GpuTiming();
    frame.primitiveBatches = &m_resources.PrimitiveBatches();
    frame.renderRayTracing = m_resources.Backend().raytracing;
    frame.windowWidth = (std::max)( 1, m_window.ClientWidth() );
    frame.windowHeight = (std::max)( 1, m_window.ClientHeight() );

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
    frame.baseView = m_cameras.GetViewMatrix();
    frame.projection = m_window.GetProjectionMatrix();
    frame.viewProjection = frame.projection * frame.baseView;
    frame.eye = m_cameras.GetRenderCameraTranslation();
    frame.viewCenter = m_cameras.GetRenderCameraView();
    frame.up = m_cameras.GetRenderCameraUp();
    frame.waterY = m_world.GetFluidSurfaceHeight();
    frame.reflectionEye = Vector3( frame.eye.x, 2.0f * frame.waterY - frame.eye.y, frame.eye.z );
    frame.reflectionCenter =
        Vector3( frame.viewCenter.x, 2.0f * frame.waterY - frame.viewCenter.y, frame.viewCenter.z );
    frame.reflectionUp = Vector3( frame.up.x, -frame.up.y, frame.up.z );
    frame.reflectionView = Matrix4::LookAt( frame.reflectionEye, frame.reflectionCenter, frame.reflectionUp );
    frame.reflectionViewProjection = frame.projection * frame.reflectionView;
    return frame;
}


RenderResourceContext RuntimeRenderer::BuildRenderResourceContext( bool cinematicRender )
{
    return RenderResourceContext{ cinematicRender,
                                  m_resources.Assets(),
                                  *m_resources.Backend().renderResources,
                                  *m_resources.Backend().renderTextures,
                                  *m_resources.Backend().renderGeometry,
                                  (std::max)( 1, m_window.ClientWidth() ),
                                  (std::max)( 1, m_window.ClientHeight() ) };
}


RuntimeRenderer::RuntimeRenderer( RuntimeRenderBackendView backend,
                                  const RenderWorldView& world,
                                  SceneSessionState& scene )
    : m_resources( backend, world, scene ), m_cameras( world.cameras ), m_window( world.window ),
      m_world( world.worldEnvironment ), m_collisionVisualizer( world.overlayResources.m_collisionOverlay ),
      m_broadphaseVisualizer( world.overlayResources.m_broadphaseOverlay ),
      m_physicsDebugVisualizer( world.overlayResources.m_physicsDebugOverlay ), m_profiler( world.profiler ),
      m_fullscreenQuadPass( m_resources.PassResources().fullscreen ), m_skyPass( m_resources.PassResources().sky,
                                                                                 m_resources.PassResources().fullscreen,
                                                                                 m_resources.SkyBoxOwner(),
                                                                                 m_resources.Config(),
                                                                                 m_profiler ),
      m_sceneTargetPass( m_resources.PassResources().cinematicScene, m_profiler ),
      m_shadowPass( m_resources.PassResources().shadows,
                    m_resources.Terrain(),
                    m_resources.Config(),
                    m_resources.Log(),
                    m_profiler ),
      m_reflectionPass( m_resources.PassResources().reflection,
                        m_collisionVisualizer,
                        m_resources.Config(),
                        m_dxrReflectionTransforms.data(),
                        static_cast<int>( m_dxrReflectionTransforms.size() ),
                        m_resources.Log(),
                        m_profiler ),
      m_objectPass( m_collisionVisualizer, m_resources.Config(), m_profiler ),
      m_terrainPass( m_resources.Terrain(), m_resources.Config(), m_profiler ),
      m_waterPass( m_world, m_resources.Config(), m_profiler ), m_debugOverlayPass( m_broadphaseVisualizer,
                                                                                    m_physicsDebugVisualizer,
                                                                                    m_resources.Terrain(),
                                                                                    m_resources.Assets(),
                                                                                    m_profiler ),
      m_volumetricPass( m_resources.PassResources().cinematicScene,
                        m_resources.PassResources().volumetricLight,
                        m_resources.PassResources().fullscreen,
                        m_resources.Config(),
                        m_profiler ),
      m_tonemapPass( m_resources.PassResources().cinematicScene,
                     m_resources.PassResources().volumetricLight,
                     m_resources.PassResources().tonemap,
                     m_resources.PassResources().fullscreen,
                     m_resources.Config(),
                     m_profiler )
{
    m_renderPassGraphScratch.ReserveForRuntimePassGraph();
    m_renderPassCompileScratch.ReserveForRuntimePassGraph();
}


RuntimeRenderer::~RuntimeRenderer() = default;


void RuntimeRenderer::BeginProfilerFrame()
{
    m_resources.GpuTiming().BeginFrame();
}


void RuntimeRenderer::ResetSceneRuntimePolicyFromConfig()
{
    SetVsyncEnabled( m_resources.Config().runtimeRender.vsyncEnabled );
    SetPipelineSyncEnabled( m_resources.Config().runtimeRender.forcePipelineSync );
}


void RuntimeRenderer::RestorePresentationSettings( const RenderPresentationSettings& settings )
{
    m_presentationSettings = settings;
}


bool RuntimeRenderer::VsyncEnabled() const
{
    return m_presentationSettings.vsyncEnabled;
}


void RuntimeRenderer::SetVsyncEnabled( bool enabled )
{
    m_presentationSettings.vsyncEnabled = enabled;
}


bool RuntimeRenderer::PipelineSyncEnabled() const
{
    return m_presentationSettings.pipelineSyncEnabled;
}


void RuntimeRenderer::SetPipelineSyncEnabled( bool enabled )
{
    m_presentationSettings.pipelineSyncEnabled = enabled;
}


SkullbonezCore::Rendering::RenderGraph& RuntimeRenderer::BeginRenderPassGraph()
{
    return m_renderPassGraphScratch;
}


const SkullbonezCore::Rendering::RenderGraphCompileResult&
RuntimeRenderer::CompileRenderPassGraph( SkullbonezCore::Rendering::RenderGraph& graph )
{
    graph.Compile( m_renderPassCompileScratch );
    return m_renderPassCompileScratch;
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


bool RuntimeRenderer::RenderPreparedFrame( const FrameEntryContext& context,
                                           const SkullbonezCore::Core::CinematicRenderConfig& renderConfig,
                                           bool cinematicRender )
{
    // Invariant: Run opens graph ownership before choosing a world or text-only
    // path. RenderPreparedFrame may only append to that active frame and may never
    // replace the graph after earlier frame work has been recorded.
    if ( m_frameGraphFinalized || m_frameGraphRenderGraph != m_resources.Backend().renderGraph )
    {
        SB_FATAL( "RunRender", "World rendering requires the current active frame graph." );
    }

    const RuntimeRenderFramePolicy& policy = context.framePolicy;
    const ReplayRenderFrameView& replayFrame = context.replayOverlay.replayFrame;
    RuntimeTools& runtimeTools = context.toolOverlay.tools;
    m_resources.UiText().SetRayTracingCapability( m_resources.Backend().raytracing );
    const SkullbonezCore::Core::OrdinaryRenderConfig& ordinaryRender = m_resources.Config().ordinaryRender;
    SkullbonezCore::Core::CinematicRenderConfig ordinaryShadowConfig = renderConfig;
    ordinaryShadowConfig.shadow = ordinaryRender.shadow;
    const SkullbonezCore::Core::CinematicRenderConfig& activeShadowStyle =
        cinematicRender ? renderConfig : ordinaryShadowConfig;
    const auto& backend = m_resources.Backend();
    const bool renderReady = backend.renderFrame && backend.renderGraph && backend.renderResources &&
                             backend.renderTextures && backend.renderGeometry && backend.renderDiagnostics;
    const bool shadowMapsEnabled = activeShadowStyle.shadow.enabled && renderReady && !policy.textOnly;

    const RenderResourceContext resourceContext = BuildRenderResourceContext( cinematicRender );
    {
        CoreAllocation::RuntimeAllocationScope allocationScope( CoreAllocation::RuntimeAllocationPhase::BackendInit );
        EnsureFrameResources( resourceContext );
    }

    const bool useCinematicTarget = cinematicRender && m_sceneTargetPass.IsReady();

    // Build the shared pass contract once, after camera update and before any
    // pass can bind targets. All extracted passes consume this same frame view.
    RenderFrameContext frame = BuildRenderFrameContext( context.renderModels, cinematicRender, renderConfig );

    // These passes currently borrow subsystem-owned mesh/material resources,
    // but keeping the ensure calls in the frame story gives future extraction
    // work an obvious place to move those GPU resources.
    {
        CoreAllocation::RuntimeAllocationScope allocationScope( CoreAllocation::RuntimeAllocationPhase::BackendInit );
        m_objectPass.EnsureGpuResources( resourceContext );
        m_terrainPass.EnsureGpuResources( resourceContext );
        m_waterPass.EnsureGpuResources( resourceContext );
        m_debugOverlayPass.EnsureGpuResources( resourceContext );
    }

    // Defer the first DX12 command-list open until after CPU-side model prep so
    // allocator waits do not block work that can overlap the previous frame.
    if ( !useCinematicTarget )
    {
        // If the cinematic target could not be created, this same graph-owned
        // frame edge clears the fallback backbuffer instead of reviving a
        // direct backend transition path.
        ExecuteBackbufferAcquireThroughRenderGraph( { *backend.renderGraph, *backend.renderFrame, true } );
    }

    const SkullbonezCore::Core::CinematicRenderConfig* activeCinematic = frame.cinematic;
    const SkullbonezCore::Core::CinematicRenderConfig* activeShadowConfig =
        shadowMapsEnabled ? &activeShadowStyle : nullptr;
    if ( activeShadowConfig )
    {
        CoreAllocation::RuntimeAllocationScope allocationScope( CoreAllocation::RuntimeAllocationPhase::BackendInit );
        PrimitiveRenderContext primitiveContext{ *backend.renderResources,
                                                 *backend.renderTextures,
                                                 *backend.renderGeometry,
                                                 *backend.renderDiagnostics,
                                                 m_resources.Assets(),
                                                 m_resources.Config(),
                                                 m_resources.PrimitiveBatches() };
        m_resources.PrimitiveBatches().EnsureShadowDepthPrimitiveResources( primitiveContext );
        if ( Geometry::Terrain* terrain = m_resources.Terrain().Get() )
        {
            terrain->EnsureShadowDepthResources();
        }
        m_shadowPass.EnsureGpuResources( resourceContext, *activeShadowConfig );
    }
    ShadowPassOutput shadowPass;
    bool shadowPassExecuted = false;
    if ( activeShadowConfig )
    {
        shadowPass = ExecuteShadowThroughRenderGraph(
            { frame, activeShadowConfig, policy.terrainHidden, policy.collisionVisualizer } );
        shadowPassExecuted = true;
    }
    else
    {
        // Why: disabled shadows still need last-frame receiver handles cleared,
        // but scheduling an empty ShadowMapPass violates the scene's opt-out.
        shadowPass = m_shadowPass.ResetFrameOutputs();
    }
    const Rendering::ShadowFrameData* terrainShadowFrame = shadowPass.terrainShadow;
    const Rendering::ShadowFrameData* objectShadowFrame = shadowPass.objectShadow;
    // The object receiver falls back to the broad map when no tight map was
    // produced. Terrain must not declare and sample that same resource twice;
    // a distinct pointer is the frame-local proof that t5 has a real producer.
    const Rendering::ShadowFrameData* terrainDetailShadowFrame =
        objectShadowFrame != terrainShadowFrame ? objectShadowFrame : nullptr;

    const bool collisionStateColorsVisible = policy.collisionVisualizer;
    const bool debugTransparentBodyPass = policy.physicsDebugTransparent && policy.physicsDebugAlpha < 1.0f;
    const bool replayFocusFadeActive = replayFrame.focusFadeActive;
    const std::vector<uint8_t>* replayFocusModelMask = replayFocusFadeActive ? replayFrame.focusModelMask : nullptr;
    const bool transparentBodyPass = debugTransparentBodyPass || replayFocusFadeActive;
    const float bodyRenderAlpha = debugTransparentBodyPass ? policy.physicsDebugAlpha : 1.0f;
    const float collisionVisualizerAlphaOverride = debugTransparentBodyPass ? bodyRenderAlpha : -1.0f;
    const bool waterModeOff = frame.cinematicEnabled && activeCinematic && activeCinematic->waterMode == 0;
    const bool waterVisibleThisFrame = !policy.waterHidden && !waterModeOff;
    const bool reflectionPassNeeded = waterVisibleThisFrame && !policy.waterNoReflect;

    // Invariant: sky and reflection both consume the interpolated render camera
    // from RenderFrameContext. Using the selected destination camera here would
    // stretch reflected geometry during camera transitions.
    if ( !cinematicRender )
    {
        PROFILE_GPU_BEGIN( &m_resources.GpuTiming(), "Frame/Render/Skybox" );
        {
            DRAW_CALL_TRACE_SCOPE( *backend.renderDiagnostics, "Frame/Render/Skybox" );
            ExecuteSkyboxThroughRenderGraph( frame );
        }
        PROFILE_GPU_END( &m_resources.GpuTiming(), "Frame/Render/Skybox" );
    }

    ReflectionPassOutput reflection;
    reflection.reflectionSampleViewProjection = frame.reflectionViewProjection;
    if ( reflectionPassNeeded )
    {
        {
            CoreAllocation::RuntimeAllocationScope allocationScope(
                CoreAllocation::RuntimeAllocationPhase::BackendInit );
            m_reflectionPass.EnsureGpuResources( resourceContext );
        }
        reflection = ExecuteReflectionThroughRenderGraph( { frame,
                                                            activeCinematic,
                                                            objectShadowFrame,
                                                            collisionStateColorsVisible,
                                                            debugTransparentBodyPass,
                                                            collisionVisualizerAlphaOverride,
                                                            bodyRenderAlpha,
                                                            policy.waterRTReflect,
                                                            policy.waterNoReflect,
                                                            static_cast<float>( policy.totalSimulationSeconds ) } );
    }

    if ( useCinematicTarget )
    {
        ExecuteSceneTargetBeginThroughRenderGraph( frame );
    }

    // Opaque bodies render before terrain/water unless debug transparency asks
    // for a late transparent body pass.
    if ( !debugTransparentBodyPass )
    {
        ExecuteObjectThroughRenderGraph( { frame,
                                           ObjectPassMode::Opaque,
                                           useCinematicTarget,
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
    ExecuteTerrainThroughRenderGraph( { frame,
                                        useCinematicTarget,
                                        activeCinematic,
                                        terrainShadowFrame,
                                        terrainDetailShadowFrame,
                                        policy.terrainHidden } );

    // Water is deliberately downstream of ReflectionPass; it samples the
    // reflection texture but never rebuilds it.
    ExecuteWaterThroughRenderGraph( { frame,
                                      reflection,
                                      useCinematicTarget,
                                      activeCinematic,
                                      policy.waterHidden,
                                      policy.waterFlatDebug,
                                      policy.waterNoReflect,
                                      policy.waterFreezeDebug,
                                      policy.frozenWaterTime,
                                      static_cast<float>( policy.simulationSeconds ) } );

    const bool worldExtensionRendered =
        ExecuteWorldExtensionThroughRenderGraph( { frame, context.worldExtension, useCinematicTarget } );

    if ( debugTransparentBodyPass )
    {
        ExecuteObjectThroughRenderGraph( { frame,
                                           ObjectPassMode::Transparent,
                                           useCinematicTarget,
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
        ExecuteObjectThroughRenderGraph( { frame,
                                           ObjectPassMode::Transparent,
                                           useCinematicTarget,
                                           activeCinematic,
                                           objectShadowFrame,
                                           collisionStateColorsVisible,
                                           collisionVisualizerAlphaOverride,
                                           0.5f,
                                           replayFocusModelMask,
                                           false } );
    }

    {
        CoreAllocation::RuntimeAllocationScope replayAllocationScope( CoreAllocation::RuntimeAllocationPhase::Replay );
        ExecuteReplayGhostsThroughRenderGraph(
            { frame, *replayFrame.visualPacket, useCinematicTarget, activeCinematic, objectShadowFrame } );
    }

    const DebugOverlaySnapshot debugSnapshot =
        BuildDebugOverlaySnapshot( context.renderModels, context.toolOverlay, policy );
    const bool debugOverlayRendered = ExecuteDebugOverlayThroughRenderGraph(
        { frame, debugSnapshot, runtimeTools, replayFrame.visualPacket, useCinematicTarget } );

    bool volumetricReady = false;
    CinematicPostFrameOutput cinematicPostOutput;
    if ( useCinematicTarget )
    {
        cinematicPostOutput = ExecuteCinematicPostThroughRenderGraph( { frame } );
        volumetricReady = cinematicPostOutput.volumetricReady;
    }

    Rendering::RenderSceneSnapshot frameSnapshot;
    frameSnapshot.cinematicRender = cinematicRender;
    frameSnapshot.useCinematicTarget = useCinematicTarget;
    frameSnapshot.terrainShadowValid = terrainShadowFrame && terrainShadowFrame->valid;
    frameSnapshot.objectShadowValid = objectShadowFrame && objectShadowFrame->valid;
    frameSnapshot.shadowPassExecuted = shadowPassExecuted;
    frameSnapshot.reflectionPassExecuted = reflectionPassNeeded;
    frameSnapshot.reflectionUsedDxr = reflection.usedDxr;
    frameSnapshot.objectOpaquePass = !debugTransparentBodyPass;
    frameSnapshot.objectTransparentPass = transparentBodyPass;
    frameSnapshot.terrainPassRendered = !policy.terrainHidden;
    const WaterPassDebugInfo& waterDebug = m_waterPass.LastDebugInfo();
    frameSnapshot.waterPassRendered = waterDebug.rendered;
    frameSnapshot.waterSamplesReflection =
        waterDebug.rendered && !waterDebug.noReflection && waterDebug.reflectionValid;
    frameSnapshot.worldExtensionRendered = worldExtensionRendered;
    frameSnapshot.volumetricPassExecuted = cinematicPostOutput.volumetricPassExecuted;
    frameSnapshot.volumetricReady = volumetricReady;
    if ( volumetricReady )
    {
        frameSnapshot.volumetricTextureHandle = cinematicPostOutput.volumetricTextureHandle;
        frameSnapshot.volumetricWidth = cinematicPostOutput.volumetricWidth;
        frameSnapshot.volumetricHeight = cinematicPostOutput.volumetricHeight;
    }
    m_frameGraphSnapshot = frameSnapshot;
    return debugOverlayRendered;
}


void RuntimeRenderer::ReleaseBackendOwnedResources( Rendering::Dx12GeometryOwner* renderGeometry )
{
    // Lifetime: release pass-owned GPU resources while the renderer backend is
    // still alive. The order keeps consumers ahead of their producers, so cached
    // handles are invalidated before targets die.
    m_tonemapPass.ReleaseGpuResources();
    m_volumetricPass.ReleaseGpuResources();
    m_sceneTargetPass.ReleaseGpuResources();
    m_shadowPass.ReleaseGpuResources();
    m_reflectionPass.ReleaseGpuResources();
    m_waterPass.ReleaseGpuResources();
    m_terrainPass.ReleaseGpuResources();
    m_skyPass.ReleaseGpuResources();
    m_fullscreenQuadPass.ReleaseGpuResources( renderGeometry );
    m_resources.ReleaseUiTextResources();
}


SkullbonezCore::Core::SbResult
RuntimeRenderer::ReleaseBackendOwnedRuntimeResources( const BackendResourceReleaseContext& context )
{
    enum class BackendResourceStep
    {
        WorldEnvironment,
        HelperOwner,
        CollisionVisualizer,
        UIResources,
        RenderPassResources,
        ProfilerQueries,
        TextureCollection,
        CameraCollection,
        SkyBox,
        LauncherLaser
    };

    struct BackendResourcePhase
    {
        const char* name;
        BackendResourceStep step;
    };

    const BackendResourcePhase releaseSteps[] = {
        { "world_environment", BackendResourceStep::WorldEnvironment },
        { "helper_owner", BackendResourceStep::HelperOwner },
        { "collision_visualizer", BackendResourceStep::CollisionVisualizer },
        { "ui_resources", BackendResourceStep::UIResources },
        { "render_pass_resources", BackendResourceStep::RenderPassResources },
        { "profiler_queries", BackendResourceStep::ProfilerQueries },
        { "texture_collection", BackendResourceStep::TextureCollection },
        { "camera_collection", BackendResourceStep::CameraCollection },
        { "skybox", BackendResourceStep::SkyBox },
        { "launcher_laser", BackendResourceStep::LauncherLaser },
    };

    const auto& backend = m_resources.Backend();
    const auto logLifecycleStep = [&]( const char* step ) { m_resources.Log().Write( context.phaseName, step ); };

    if ( backend.renderFrame )
    {
        logLifecycleStep( "flush_before_resource_release" );
        const SkullbonezCore::Core::SbResult flushResult = backend.renderFrame->DrainForResourceRelease();
        if ( !flushResult.ok )
        {
            // Lane R: return before the first release. The destructor caller
            // converts this non-returnable teardown failure to Lane F.
            return flushResult;
        }
    }

    // Lifetime: RuntimeRenderer owns the ordered teardown recipe because pass
    // resources and their consumers must release before backend-owned caches.
    // The successful drain above is the proof that every release is GPU-safe.
    for ( const BackendResourcePhase& phase : releaseSteps )
    {
        logLifecycleStep( phase.name );
        switch ( phase.step )
        {
        case BackendResourceStep::WorldEnvironment:
            m_world.ReleaseRenderResources();
            break;
        case BackendResourceStep::HelperOwner:
            m_resources.ReleaseHelperResources();
            break;
        case BackendResourceStep::CollisionVisualizer:
            m_collisionVisualizer.ResetResources( backend.renderGeometry );
            break;
        case BackendResourceStep::UIResources:
            context.ui.ResetResources( backend.renderGeometry );
            break;
        case BackendResourceStep::RenderPassResources:
            ReleaseBackendOwnedResources( backend.renderGeometry );
            break;
        case BackendResourceStep::ProfilerQueries:
            m_resources.InvalidateProfilerResources();
            break;
        case BackendResourceStep::TextureCollection:
            m_resources.ReleaseTextureResources();
            break;
        case BackendResourceStep::CameraCollection:
            m_cameras.Reset();
            m_cameras.SetTerrain( nullptr );
            break;
        case BackendResourceStep::SkyBox:
            m_resources.ReleaseSkyResources();
            break;
        case BackendResourceStep::LauncherLaser:
            context.tools.Laser().ResetResources( backend.renderGeometry );
            break;
        }
    }
    return SkullbonezCore::Core::SbResult::Success();
}


void RuntimeRenderer::BeginFrameGraph( Rendering::Dx12GraphTransientPool& renderGraph )
{
    if ( m_frameGraphRenderGraph && !m_frameGraphFinalized )
    {
        SB_FATAL( "RunRender", "A new frame cannot replace an unfinished frame graph." );
    }

    // Concept: one graph instance accumulates every production pass for this
    // frame. World and UI wrappers append and synchronously execute only their
    // new callback ranges while resource identity and transition history remain
    // shared until Present or an explicit capture-only completion.
    m_renderPassGraphScratch.Clear();
    m_frameGraphSnapshot = Rendering::RenderSceneSnapshot();
    m_frameGraphRenderGraph = &renderGraph;
    m_frameGraphFinalized = false;
}


void RuntimeRenderer::PrepareUiFrameTarget()
{
    const auto& backend = m_resources.Backend();
    if ( !backend.renderGraph || !backend.renderFrame )
    {
        SB_FATAL( "Runtime/UI", "Operator editor frame has no render command context." );
    }
    // Text-only and ImGui-only frames do not necessarily execute a world or
    // tonemap pass. This graph callback is their normal backbuffer acquisition;
    // on ordinary frames it validates the already-render-target state.
    ExecuteBackbufferAcquireThroughRenderGraph( { *backend.renderGraph, *backend.renderFrame, false } );
}


#if defined( SKULLBONEZ_DEVELOPMENT_TOOLS )
SkullbonezCore::Core::SbResult RuntimeRenderer::RenderDevelopmentUi( DevelopmentTools::ImGuiEditorOwner& editor )
{
    if ( !m_frameGraphRenderGraph )
    {
        return SkullbonezCore::Core::SbResult::Failure( "RuntimeRenderer", "Development UI has no active frame graph" );
    }
    Rendering::RenderGraph& graph = BeginRenderPassGraph();
    const Rendering::RenderGraphResourceHandle backbuffer = AddBackbufferResource( graph, *m_frameGraphRenderGraph );
    const uint32_t pass = graph.AddPass( "ImGuiEditorPass" );
    graph.AddWrite( pass, backbuffer, Rendering::RenderGraphResourceAccess::RenderTarget );

    DevelopmentUiGraphCallbackData callbackData;
    callbackData.editor = &editor;
    callbackData.renderGraph = m_frameGraphRenderGraph;
    graph.SetPassCallback<ExecuteDevelopmentUiGraphCallback>( pass, callbackData, true, "Frame/UI/ImGuiEditor" );
    const Rendering::RenderGraphCompileResult& compiled = CompileRenderPassGraph( graph );
    callbackData.compiled = &compiled;
    callbackData.expectedTransitionCount = CountCompiledTransitionsForPass( compiled, pass );
    ExecuteGraphCallbacksOrFatal( graph, 1u, "ImGuiEditor" );
    return callbackData.status;
}
#endif


void RuntimeRenderer::FinalizeFrameGraph()
{
    FinalizeFrameGraphInternal( "Present", true, false );
}


void RuntimeRenderer::FinalizeCaptureOnlyFrameGraph()
{
    // Capture restart frames intentionally do not reach swap-chain Present.
    // Validate that every recorded pass was callback-owned, then release all
    // scene/resource payload borrows before capture automation can load a new
    // scene in the same Run::TickScreenshots call.
    FinalizeFrameGraphInternal( nullptr, false, true );
}


void RuntimeRenderer::FinalizeFrameGraphInternal( const char* declarationOnlyPassName,
                                                  bool appendPresent,
                                                  bool releaseGraphStorage )
{
    if ( m_frameGraphFinalized || !m_frameGraphRenderGraph )
    {
        SB_FATAL( "RunRender", "Frame graph finalization requires one active, unfinished frame graph." );
    }

    Rendering::RenderGraph& graph = BeginRenderPassGraph();
    if ( appendPresent )
    {
        const Rendering::RenderGraphResourceHandle backbuffer =
            AddBackbufferResource( graph, *m_frameGraphRenderGraph );
        // Declaration-only exception: Present is the external swap-chain/fence
        // boundary after command recording. The backend consumes this final edge.
        const uint32_t presentPass = graph.AddPass( "Present" );
        graph.AddWrite( presentPass, backbuffer, Rendering::RenderGraphResourceAccess::Present );
    }
    CompileRenderPassGraph( graph );

    const Rendering::RenderGraphExecutionContractResult contract =
        graph.ValidateFrameExecutionContract( declarationOnlyPassName );
    if ( !contract.IsValid() ||
         contract.callbackPassCount + contract.declarationOnlyPassCount != graph.Passes().size() )
    {
        SB_FATAL( "RunRender",
                  "Production frame graph violates callback ownership. callbacks=%zu declarations=%zu expected=%zu "
                  "name_match=%d enabled=%d passes=%zu",
                  contract.callbackPassCount,
                  contract.declarationOnlyPassCount,
                  contract.expectedDeclarationOnlyPassCount,
                  contract.declarationOnlyNameMatches ? 1 : 0,
                  contract.allCallbacksEnabled ? 1 : 0,
                  graph.Passes().size() );
    }

    {
        CoreAllocation::RuntimeAllocationScope diagnosticsAllocationScope(
            CoreAllocation::RuntimeAllocationPhase::Diagnostics );
        Rendering::RenderPipeline::DumpExecutedFrameGraphIfChanged( graph, m_frameGraphSnapshot );
    }
    graph.ReleaseCallbackPayloadBorrows();
    m_frameGraphFinalized = true;
    if ( releaseGraphStorage )
    {
        graph.Clear();
        m_frameGraphSnapshot = Rendering::RenderSceneSnapshot();
        m_frameGraphRenderGraph = nullptr;
    }
}


void RuntimeRenderer::RenderUiText( const UiTextPassInputs& inputs )
{
    ExecuteUiTextThroughRenderGraph( inputs );
}


bool RuntimeRenderer::RenderFrameEntry( const FrameEntryContext& context )
{
    m_resources.UiText().SetRayTracingCapability( nullptr );
    const RuntimeRenderFramePolicy& policy = context.framePolicy;

    if ( policy.textOnly )
    {
        return false;
    }

    const auto& backend = m_resources.Backend();
    const bool renderReady = backend.renderFrame != nullptr && backend.renderGraph != nullptr &&
                             backend.renderResources != nullptr && backend.renderTextures != nullptr &&
                             backend.renderGeometry != nullptr && backend.renderDiagnostics != nullptr;
    if ( !renderReady )
    {
        return false;
    }
    const bool cinematicRender = context.cinematicRequested && renderReady && !policy.textOnly;
    // Why: persistent render owners are already members of RuntimeRenderer.
    // Only the top-level frame transaction and derived cinematic values cross
    // into pass sequencing; no duplicate service/input bag is constructed.
    return RenderPreparedFrame( context, context.cinematic, cinematicRender );
}
