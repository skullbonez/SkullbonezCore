/*
File: SkullbonezSource/Runtime/Render/RuntimeRenderer.cpp
Purpose:
  Coordinates render passes for the active scene.

Summary:
  Renderer-facing code samples one camera-lighting value, constructs focused
  pass inputs, and appends named callbacks to the live graph in image order.
  Feature owners cross this boundary only through already-published generic values.

Invariants:
  - The live RenderGraph owns pass order from world clear through late UI;
    FinalizeFrameGraph adds the sole declaration-only Present edge.
  - Pass resource reset hooks run while the renderer backend is alive, because
    framebuffers, shaders, and dynamic vertex buffers can own backend objects.
  - Pass input/output structs borrow data for one frame only. Do not cache
    pointers returned from ShadowPassOutput or ReflectionPassOutput consumers.
  - Detached contact geometry reaches DebugOverlayPass as a Rendering value;
    RuntimeRenderer does not inspect its feature origin.
  - UI-text scheduling executes frame metrics before one graph compile, then
    chrome, focused operator projection/submission, HUD overlay, Replay, and
    final flush callbacks in visual order. Font, timing, and ray-tracing
    capabilities remain inside UiTextPass.

Related:
  - SkullbonezSource/Runtime/Render/RuntimeRenderPasses.h declares pass contracts.
  - SkullbonezSource/Runtime/Render/RuntimeRenderer.h declares the render owner.
  - SkullbonezSource/Rendering/RenderPipeline.h formats the live graph diagnostics.
  - Agentic/Reference/engine-glossary.md
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
#include "../Startup/Window.h"
#include "../Tools/RuntimeTools.h"
#include "../Debug/CollisionVisualizer.h"
#include "../Scene/SceneTerrain.h"
#include "../../Core/Allocation/RuntimeAllocationTracker.h"
#include "../../Core/Allocation/RuntimeReserveAllocator.h"
#include "../Planning/ReplayOverlayPackets.h"
#include "../Scene/SceneCinematicPolicy.h"
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
namespace Math = SkullbonezCore::Math;
namespace Physics = SkullbonezCore::Physics;
namespace Rendering = SkullbonezCore::Rendering;
namespace Textures = SkullbonezCore::Textures;
namespace CoreAllocation = SkullbonezCore::Core::Allocation;
using SkullbonezCore::Math::Vector::Vector3;

namespace
{
float SampleWorldSurfaceHeight( SkullbonezCore::Geometry::Terrain& surface, float x, float z, float fallback )
{
    return surface.IsInBounds( x, z ) ? surface.GetTerrainHeightAt( x, z ) : fallback;
}

// Concept: these are narrow ABI invocations, not frame contexts. The graph API
// invokes C-style callbacks, so each invocation names one concrete pass owner
// and only the stack values consumed by that graph node.
//
// Lifetime: payloads are stack objects consumed during the same RenderPreparedFrame()
// call. Never cache these pointers across graph execution or use them as a
// wider runtime service boundary.
//
// Invariant: an invocation must never unite sibling-pass inputs or provide a
// route back to RuntimeRenderer. Once RenderGraph supports capturing typed
// node operations directly, these ABI records disappear with its userData seam.
struct CinematicPostGraphState
{
    // Shared publication between the two concrete post passes. It contains no
    // pass owner and exists only for their ordered graph transition handshake.
    const SkullbonezCore::Rendering::RenderGraphCompileResult* compiled = nullptr;
    SkullbonezCore::Rendering::FramebufferDX12* sceneTarget = nullptr;
    SkullbonezCore::Rendering::RenderGraphTextureBinding volumetricLight;
    size_t volumetricTransitionCount = 0;
    size_t tonemapTransitionCount = 0;
    bool volumetricRendered = false;
    bool sceneTargetUnbound = false;
};

struct VolumetricGraphInvocation
{
    VolumetricPass* pass = nullptr;
    const RenderCameraLighting* camera = nullptr;
    const SkullbonezCore::Core::CinematicRenderConfig* cinematic = nullptr;
    SkullbonezCore::Rendering::Dx12GeometryOwner* renderGeometry = nullptr;
    SkullbonezCore::Rendering::Dx12TextureOwner* renderTextures = nullptr;
    SkullbonezCore::Rendering::Dx12FrameOwner* renderFrame = nullptr;
    SkullbonezCore::Rendering::Dx12GraphTransientPool* renderGraph = nullptr;
    SkullbonezCore::Rendering::Dx12Diagnostics* renderDiagnostics = nullptr;
    SkullbonezCore::Rendering::RenderGpuTimingOwner* gpuTiming = nullptr;
    int windowWidth = 1;
    int windowHeight = 1;
    CinematicPostGraphState* state = nullptr;
};

struct TonemapGraphInvocation
{
    TonemapPass* pass = nullptr;
    const SkullbonezCore::Core::CinematicRenderConfig* cinematic = nullptr;
    SkullbonezCore::Rendering::Dx12GeometryOwner* renderGeometry = nullptr;
    SkullbonezCore::Rendering::Dx12TextureOwner* renderTextures = nullptr;
    SkullbonezCore::Rendering::Dx12FrameOwner* renderFrame = nullptr;
    SkullbonezCore::Rendering::Dx12GraphTransientPool* renderGraph = nullptr;
    SkullbonezCore::Rendering::Dx12Diagnostics* renderDiagnostics = nullptr;
    SkullbonezCore::Rendering::RenderGpuTimingOwner* gpuTiming = nullptr;
    int windowWidth = 1;
    int windowHeight = 1;
    CinematicPostGraphState* state = nullptr;
};

struct ShadowGraphInvocation
{
    ShadowPass* shadowPass = nullptr;
    const ShadowPassInputs* inputs = nullptr;
    SkullbonezCore::Rendering::Dx12GraphTransientPool* renderGraph = nullptr;
    const SkullbonezCore::Rendering::RenderGraphCompileResult* compiled = nullptr;
    size_t expectedTransitionCount = 0;
    ShadowPassOutput output;
};

struct ReflectionGraphInvocation
{
    ReflectionPass* reflectionPass = nullptr;
    const ReflectionPassInputs* inputs = nullptr;
    SkullbonezCore::Rendering::Dx12GraphTransientPool* renderGraph = nullptr;
    const SkullbonezCore::Rendering::RenderGraphCompileResult* compiled = nullptr;
    size_t expectedTransitionCount = 0;
    ReflectionPassOutput output;
};

struct ObjectGraphInvocation
{
    ObjectPass* objectPass = nullptr;
    const ObjectPassInputs* inputs = nullptr;
};

struct TerrainGraphInvocation
{
    TerrainPass* terrainPass = nullptr;
    const TerrainPassInputs* inputs = nullptr;
};

struct WaterGraphInvocation
{
    WaterPass* waterPass = nullptr;
    const WaterPassInputs* inputs = nullptr;
};

struct DebugOverlayGraphInvocation
{
    DebugOverlayPass* debugOverlayPass = nullptr;
    const DebugOverlayPassInputs* inputs = nullptr;
    bool rendered = false;
};

struct SceneTargetGraphInvocation
{
    SceneTargetPass* sceneTargetPass = nullptr;
    const RenderCameraLighting* camera = nullptr;
    const SkullbonezCore::Core::CinematicRenderConfig* cinematic = nullptr;
    SkullbonezCore::Rendering::Dx12FrameOwner* renderFrame = nullptr;
    SkullbonezCore::Rendering::Dx12GeometryOwner* renderGeometry = nullptr;
    SkullbonezCore::Rendering::Dx12TextureOwner* renderTextures = nullptr;
    SkullbonezCore::Rendering::Dx12Diagnostics* renderDiagnostics = nullptr;
    SkullbonezCore::Rendering::RenderGpuTimingOwner* gpuTiming = nullptr;
    SkullbonezCore::Rendering::Dx12GraphTransientPool* renderGraph = nullptr;
    const SkullbonezCore::Rendering::RenderGraphCompileResult* compiled = nullptr;
    size_t expectedTransitionCount = 0;
};

struct GraphTransitionCallbackData
{
    SkullbonezCore::Rendering::Dx12GraphTransientPool* renderGraph = nullptr;
    const SkullbonezCore::Rendering::RenderGraphCompileResult* compiled = nullptr;
    size_t expectedTransitionCount = 0;
};

struct BackbufferAcquireGraphInvocation
{
    SkullbonezCore::Rendering::Dx12GraphTransientPool* renderGraph = nullptr;
    SkullbonezCore::Rendering::Dx12FrameOwner* renderFrame = nullptr;
    const SkullbonezCore::Rendering::RenderGraphCompileResult* compiled = nullptr;
    size_t expectedTransitionCount = 0;
    bool clearFrameTargets = false;
};

struct SkyboxGraphInvocation
{
    SkyPass* skyPass = nullptr;
    const RenderCameraLighting* camera = nullptr;
    SkullbonezCore::Rendering::Dx12GeometryOwner* renderGeometry = nullptr;
    SkullbonezCore::Rendering::Dx12TextureOwner* renderTextures = nullptr;
};

struct UiOperatorPrepareGraphInvocation
{
    UiTextPass* pass = nullptr;
    SkullbonezCore::Rendering::Dx12GraphTransientPool* renderGraph = nullptr;
    SkullbonezCore::UI::InGameUIFrameData* uiData = nullptr;
    UiTextViewport viewport;
    bool drawTestPattern = false;
    SkullbonezCore::Rendering::Dx12TextureOwner* renderTextures = nullptr;
    SkullbonezCore::Rendering::Dx12GeometryOwner* renderGeometry = nullptr;
    SkullbonezCore::Rendering::Dx12Diagnostics* renderDiagnostics = nullptr;
    const SkullbonezCore::Rendering::RenderGraphCompileResult* compiled = nullptr;
    size_t expectedTransitionCount = 0;
};

struct UiOverlayGraphInvocation
{
    UiTextPass* pass = nullptr;
    SkullbonezCore::Rendering::Dx12GraphTransientPool* renderGraph = nullptr;
    UiTextViewport viewport;
    OverlayMode mode = OverlayMode::None;
    int modelCount = 0;
    float rollingFpsTime = 0.0f;
    SkullbonezCore::Rendering::Dx12TextureOwner* renderTextures = nullptr;
    SkullbonezCore::Rendering::Dx12GeometryOwner* renderGeometry = nullptr;
    SkullbonezCore::Rendering::Dx12Diagnostics* renderDiagnostics = nullptr;
    float sceneEnergyForDisplay = 0.0f;
    const SkullbonezCore::Rendering::RenderGraphCompileResult* compiled = nullptr;
    size_t expectedTransitionCount = 0;
};

struct UiFinalizeGraphInvocation
{
    UiTextPass* pass = nullptr;
    SkullbonezCore::Rendering::Dx12GraphTransientPool* renderGraph = nullptr;
    OverlayMode mode = OverlayMode::None;
    SkullbonezCore::Rendering::Dx12TextureOwner* renderTextures = nullptr;
    SkullbonezCore::Rendering::Dx12GeometryOwner* renderGeometry = nullptr;
    SkullbonezCore::Rendering::Dx12Diagnostics* renderDiagnostics = nullptr;
    const SkullbonezCore::Rendering::RenderGraphCompileResult* compiled = nullptr;
    size_t expectedTransitionCount = 0;
};

struct ReplayGhostGraphInvocation
{
    SkullbonezCore::Core::Profiler* profiler = nullptr;
    const ReplayVisualPacket* replayVisualPacket = nullptr;
    const RenderCameraLighting* camera = nullptr;
    const RuntimeRenderModelFrameView* models = nullptr;
    const SkullbonezCore::Rendering::PrimitiveRenderContext* primitive = nullptr;
    SkullbonezCore::Textures::TextureCollection* textures = nullptr;
    const SkullbonezCore::Core::CinematicRenderConfig* cinematic = nullptr;
    const SkullbonezCore::Rendering::ShadowFrameData* shadow = nullptr;
};

#if defined( SKULLBONEZ_DEVELOPMENT_TOOLS )
struct DevelopmentUiGraphInvocation
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
        SB_FATAL( "RunRender", "Graph callback emitted the wrong transition count. pass=%s expected=%zu actual=%zu",
                  context.pass ? context.pass->name : "unknown", expectedTransitionCount, emitted );
    }

    return emitted;
}

void ExecuteGraphTransitionCallback( const SkullbonezCore::Rendering::RenderGraphPassContext& context,
                                     GraphTransitionCallbackData& data )
{
    (void)ExecuteRequiredGraphTransitions( context, data.renderGraph, data.compiled, data.expectedTransitionCount );
}

void ExecuteBackbufferAcquireGraphCallback( const SkullbonezCore::Rendering::RenderGraphPassContext& context,
                                            BackbufferAcquireGraphInvocation& data )
{
    (void)ExecuteRequiredGraphTransitions( context, data.renderGraph, data.compiled, data.expectedTransitionCount );

    if ( data.clearFrameTargets )
    {
        data.renderFrame->Clear( {} );
    }
}

void ExecuteShadowGraphCallback( const SkullbonezCore::Rendering::RenderGraphPassContext& context,
                                 ShadowGraphInvocation& data )
{
    if ( !data.shadowPass || !data.inputs )
    {
        SB_FATAL( "RunRender", "ShadowMapPass graph callback missing execution data." );
    }

    (void)ExecuteRequiredGraphTransitions( context, data.renderGraph, data.compiled, data.expectedTransitionCount );
    data.output = data.shadowPass->Render( *data.inputs );
}

void ExecuteReflectionGraphCallback( const SkullbonezCore::Rendering::RenderGraphPassContext& context,
                                     ReflectionGraphInvocation& data )
{
    if ( !data.reflectionPass || !data.inputs )
    {
        SB_FATAL( "RunRender", "ReflectionPass graph callback missing execution data." );
    }

    (void)ExecuteRequiredGraphTransitions( context, data.renderGraph, data.compiled, data.expectedTransitionCount );
    data.output = data.reflectionPass->Render( *data.inputs );
}

void ExecuteObjectGraphCallback( const SkullbonezCore::Rendering::RenderGraphPassContext& /*context*/,
                                 ObjectGraphInvocation& data )
{
    if ( !data.objectPass || !data.inputs )
    {
        SB_FATAL( "RunRender", "ObjectPass graph callback missing execution data." );
    }

    data.objectPass->Render( *data.inputs );
}

void ExecuteTerrainGraphCallback( const SkullbonezCore::Rendering::RenderGraphPassContext& /*context*/,
                                  TerrainGraphInvocation& data )
{
    if ( !data.terrainPass || !data.inputs )
    {
        SB_FATAL( "RunRender", "TerrainPass graph callback missing execution data." );
    }

    data.terrainPass->Render( *data.inputs );
}

void ExecuteWaterGraphCallback( const SkullbonezCore::Rendering::RenderGraphPassContext& /*context*/,
                                WaterGraphInvocation& data )
{
    if ( !data.waterPass || !data.inputs )
    {
        SB_FATAL( "RunRender", "WaterPass graph callback missing execution data." );
    }

    data.waterPass->Render( *data.inputs );
}

void ExecuteDebugOverlayGraphCallback( const SkullbonezCore::Rendering::RenderGraphPassContext& /*context*/,
                                       DebugOverlayGraphInvocation& data )
{
    if ( !data.debugOverlayPass || !data.inputs )
    {
        SB_FATAL( "RunRender", "DebugOverlayPass graph callback missing execution data." );
    }

    data.rendered = data.debugOverlayPass->Render( *data.inputs );
}

void RenderReplayPredictionGhosts( const ReplayVisualPacket& visualPacket, SkullbonezCore::Core::Profiler*,
                                   const RenderCameraLighting& camera, const RuntimeRenderModelFrameView& models,
                                   const PrimitiveRenderContext& primitive, Textures::TextureCollection& textures,
                                   const SkullbonezCore::Core::CinematicRenderConfig* cinematic,
                                   const Rendering::ShadowFrameData* shadow )
{
    PROFILE_SCOPED( "Frame/Render/ReplayPredictionGhosts" );

    if ( visualPacket.ghostRequests.empty() )
    {
        return;
    }

    // Why: ghost drawing is a render projection path. Shape and material come
    // from the prepared store snapshots so replay visualization does not need
    // the legacy object record collider mirror to stay fresh after physics steps.
    const auto colliders = models.colliders.Records();
    const auto renderInstances = models.renderInstances.Records();

    const SkullbonezCore::Core::SbResult textureResult = textures.SelectTexture( TEXTURE_BOUNDING_SPHERE );

    if ( !textureResult.Ok() )
    {
        std::fprintf( stderr, "Frame/Render/ReplayPredictionGhosts texture failure [%s]: %s\n", textureResult.ErrorOwner(),
                      textureResult.ErrorMessage() );

        return;
    }

    auto boxBatch = primitive.renderer.BeginBoxBatch( primitive, camera.baseView, camera.projection, camera.lightPosition,
                                                      true, cinematic, shadow, 1.0f );

    for ( const ReplayPredictionGhostDrawRequest& request : visualPacket.ghostRequests )
    {
        if ( request.modelRow.value < 0 || request.modelRow.value >= static_cast<int>( colliders.size() ) ||
             request.modelRow.value >= static_cast<int>( renderInstances.size() ) )
        {
            continue;
        }

        const std::size_t modelIndex = static_cast<std::size_t>( request.modelRow.value );
        const Physics::ColliderRecord& collider = colliders[modelIndex];
        const Math::CollisionDetection::BoundingBox*
            box = Math::CollisionDetection::GetShapeIf<Math::CollisionDetection::BoundingBox>( &collider.shape );

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
        const Math::Transformation::Matrix4 modelMatrix = box->GetModelMatrix( request.position,
                                                                               Math::Transformation::Matrix4::FromQuaternion( request.orientation ) );

        boxBatch.DrawModel( modelMatrix, material );
    }
}

void ExecuteReplayGhostGraphCallback( const SkullbonezCore::Rendering::RenderGraphPassContext& /*context*/,
                                      ReplayGhostGraphInvocation& data )
{
    if ( !data.replayVisualPacket || !data.camera || !data.models || !data.primitive || !data.textures )
    {
        SB_FATAL( "RunRender", "ReplayPredictionGhostPass graph callback missing execution data." );
    }

    RenderReplayPredictionGhosts( *data.replayVisualPacket, data.profiler, *data.camera, *data.models, *data.primitive,
                                  *data.textures, data.cinematic, data.shadow );
}

#if defined( SKULLBONEZ_DEVELOPMENT_TOOLS )
void ExecuteDevelopmentUiGraphCallback( const SkullbonezCore::Rendering::RenderGraphPassContext& context,
                                        DevelopmentUiGraphInvocation& data )
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
                                      SceneTargetGraphInvocation& data )
{
    if ( !data.sceneTargetPass || !data.camera || !data.cinematic || !data.renderFrame || !data.renderGeometry ||
         !data.renderTextures || !data.renderDiagnostics )
    {
        SB_FATAL( "RunRender", "CinematicSceneBegin graph callback missing execution data." );
    }

    (void)ExecuteRequiredGraphTransitions( context, data.renderGraph, data.compiled, data.expectedTransitionCount );
    data.sceneTargetPass->Begin( *data.camera, *data.cinematic, *data.renderFrame, *data.renderGeometry,
                                 *data.renderTextures, *data.renderDiagnostics, data.gpuTiming );
}

void ExecuteSkyboxGraphCallback( const SkullbonezCore::Rendering::RenderGraphPassContext& /*context*/,
                                 SkyboxGraphInvocation& data )
{
    if ( !data.skyPass || !data.camera || !data.renderGeometry || !data.renderTextures )
    {
        SB_FATAL( "RunRender", "SkyboxPass graph callback missing execution data." );
    }

    data.skyPass->Render( *data.camera, data.camera->baseView, nullptr, *data.renderGeometry, *data.renderTextures,
                          SkyPassMode::CubemapOnly );
}

void ExecuteUiChromeGraphCallback( const SkullbonezCore::Rendering::RenderGraphPassContext& context,
                                   UiChromeGraphInvocation& data )
{
    if ( !data.pass || !data.renderGraph || !data.debug || !data.scene || !data.camera || !data.replayHud ||
         !data.renderTextures || !data.renderGeometry || !data.renderDiagnostics )
    {
        SB_FATAL( "RunRender", "UI chrome graph callback missing execution data." );
    }

    (void)ExecuteRequiredGraphTransitions( context, data.renderGraph, data.compiled, data.expectedTransitionCount );
    data.pass->RenderChromeStatus( data.viewport, *data.debug, data.crossScenePauseLocked, *data.scene, *data.camera,
                                   data.sceneQueueSize, data.cameraModeLabel, *data.renderTextures, *data.renderGeometry,
                                   *data.renderDiagnostics );

    if ( !data.debug->isTextOnly )
    {
        data.pass->RenderChromeTail( *data.debug, *data.replayHud, data.launcherCameraMode, data.launcherFireModeLabel,
                                     data.reproMessageAgeSeconds, *data.renderGeometry );
    }
}

void ExecuteUiOperatorPrepareGraphCallback( const SkullbonezCore::Rendering::RenderGraphPassContext& context,
                                            UiOperatorPrepareGraphInvocation& data )
{
    if ( !data.pass || !data.renderGraph || !data.uiData || !data.renderTextures || !data.renderGeometry ||
         !data.renderDiagnostics )
    {
        SB_FATAL( "RunRender", "UI operator prepare graph callback missing execution data." );
    }

    (void)ExecuteRequiredGraphTransitions( context, data.renderGraph, data.compiled, data.expectedTransitionCount );
    data.pass->PrepareOperatorFrame( *data.uiData, data.viewport, data.drawTestPattern, *data.renderTextures,
                                     *data.renderGeometry, *data.renderDiagnostics );
}

void ExecuteUiOperatorDiagnosticsGraphCallback( const SkullbonezCore::Rendering::RenderGraphPassContext& context,
                                                UiOperatorDiagnosticsGraphInvocation& data )
{
    if ( !data.pass || !data.renderGraph || !data.uiData || !data.replayHud || !data.timers || !data.models ||
         !data.diagnosticsRuntime || !data.ui || !data.renderDiagnostics )
    {
        SB_FATAL( "RunRender", "UI operator diagnostics graph callback missing execution data." );
    }

    (void)ExecuteRequiredGraphTransitions( context, data.renderGraph, data.compiled, data.expectedTransitionCount );
    data.pass->ProjectOperatorDiagnostics( *data.uiData, *data.replayHud, *data.timers, *data.models,
                                           *data.diagnosticsRuntime, *data.ui, data.workerPool, data.secondsPerFrame,
                                           *data.renderDiagnostics );
}

void ExecuteUiOperatorSettingsGraphCallback( const SkullbonezCore::Rendering::RenderGraphPassContext& context,
                                             UiOperatorSettingsGraphInvocation& data )
{
    if ( !data.pass || !data.renderGraph || !data.uiData || !data.debug || !data.renderPresentation || !data.world ||
         !data.config || !data.cinematic )
    {
        SB_FATAL( "RunRender", "UI operator settings graph callback missing execution data." );
    }

    (void)ExecuteRequiredGraphTransitions( context, data.renderGraph, data.compiled, data.expectedTransitionCount );
    data.pass->ProjectOperatorSettings( *data.uiData, *data.debug, *data.renderPresentation, *data.world, *data.config,
                                        *data.cinematic, data.cinematicRendering );
}

void ExecuteUiOperatorInteractionGraphCallback( const SkullbonezCore::Rendering::RenderGraphPassContext& context,
                                                UiOperatorInteractionGraphInvocation& data )
{
    if ( !data.pass || !data.renderGraph || !data.uiData || !data.rayCastTest || !data.editor || !data.runtimeInput ||
         !data.camera || !data.ui )
    {
        SB_FATAL( "RunRender", "UI operator interaction graph callback missing execution data." );
    }

    (void)ExecuteRequiredGraphTransitions( context, data.renderGraph, data.compiled, data.expectedTransitionCount );
    data.pass->ProjectOperatorInteraction( *data.uiData, *data.rayCastTest, *data.editor, *data.runtimeInput, *data.camera,
                                           *data.ui, data.cameraModeEnabledMask, data.cameraModeLabel );
}

void ExecuteUiOperatorPresentationGraphCallback( const SkullbonezCore::Rendering::RenderGraphPassContext& context,
                                                 UiOperatorPresentationGraphInvocation& data )
{
    if ( !data.pass || !data.renderGraph || !data.uiData || !data.scene || !data.runtimeViewModel || !data.sceneBrowser ||
         !data.operatorEditorView )
    {
        SB_FATAL( "RunRender", "UI operator presentation graph callback missing execution data." );
    }

    (void)ExecuteRequiredGraphTransitions( context, data.renderGraph, data.compiled, data.expectedTransitionCount );
    data.pass->ProjectOperatorPresentation( *data.uiData, *data.scene, *data.runtimeViewModel, *data.sceneBrowser,
                                            *data.operatorEditorView, data.sceneHasCurrentEntry, data.currentScenePath,
                                            data.currentSceneBrowserIndex, data.sceneEnergyForDisplay );
}

void ExecuteUiOperatorSubmissionGraphCallback( const SkullbonezCore::Rendering::RenderGraphPassContext& context,
                                               UiOperatorSubmissionGraphInvocation& data )
{
    if ( !data.pass || !data.renderGraph || !data.uiData || !data.ui || !data.renderTargetPreviews || !data.assets ||
         !data.renderResources || !data.renderTextures || !data.renderGeometry || !data.renderDiagnostics )
    {
        SB_FATAL( "RunRender", "UI operator submission graph callback missing execution data." );
    }

    (void)ExecuteRequiredGraphTransitions( context, data.renderGraph, data.compiled, data.expectedTransitionCount );
    data.pass->SubmitOperatorFrame( *data.uiData, *data.ui, *data.renderTargetPreviews, *data.assets, *data.renderResources,
                                    *data.renderTextures, *data.renderGeometry, *data.renderDiagnostics,
                                    data.uiPassDrawCallStart );
}

void ExecuteUiOverlayGraphCallback( const SkullbonezCore::Rendering::RenderGraphPassContext& context,
                                    UiOverlayGraphInvocation& data )
{
    if ( !data.pass || !data.renderGraph || !data.renderTextures || !data.renderGeometry || !data.renderDiagnostics )
    {
        SB_FATAL( "RunRender", "UI overlay graph callback missing execution data." );
    }

    (void)ExecuteRequiredGraphTransitions( context, data.renderGraph, data.compiled, data.expectedTransitionCount );
    data.pass->RenderOverlayContent( data.viewport, data.mode, data.modelCount, data.rollingFpsTime,
                                     data.sceneEnergyForDisplay, *data.renderTextures, *data.renderGeometry,
                                     *data.renderDiagnostics );
}

void ExecuteUiReplayGraphCallback( const SkullbonezCore::Rendering::RenderGraphPassContext& context,
                                   UiReplayGraphInvocation& data )
{
    if ( !data.pass || !data.renderGraph || !data.overlay || !data.renderTextures || !data.renderGeometry ||
         !data.renderDiagnostics )
    {
        SB_FATAL( "RunRender", "UI Replay graph callback missing execution data." );
    }

    (void)ExecuteRequiredGraphTransitions( context, data.renderGraph, data.compiled, data.expectedTransitionCount );
    data.pass->RenderReplay( *data.overlay, data.profiler, data.gameUiSurfaceActive, data.scenePhysicsEnabled, data.gesture,
                             data.viewport, data.nowSeconds, *data.renderTextures, *data.renderGeometry,
                             *data.renderDiagnostics );
}

void ExecuteUiFinalizeGraphCallback( const SkullbonezCore::Rendering::RenderGraphPassContext& context,
                                     UiFinalizeGraphInvocation& data )
{
    if ( !data.pass || !data.renderGraph || !data.renderTextures || !data.renderGeometry || !data.renderDiagnostics )
    {
        SB_FATAL( "RunRender", "UI finalize graph callback missing execution data." );
    }

    (void)ExecuteRequiredGraphTransitions( context, data.renderGraph, data.compiled, data.expectedTransitionCount );
    data.pass->FinalizeOverlay( data.mode, *data.renderTextures, *data.renderGeometry, *data.renderDiagnostics );
}

void ExecuteVolumetricGraphCallback( const SkullbonezCore::Rendering::RenderGraphPassContext& context,
                                     VolumetricGraphInvocation& data )
{
    if ( !data.pass || !data.camera || !data.cinematic || !data.renderGeometry || !data.renderTextures ||
         !data.renderFrame || !data.renderGraph || !data.renderDiagnostics || !data.state || !data.state->compiled ||
         !context.graph )
    {
        SB_FATAL( "RunRender", "VolumetricLightPass graph callback missing execution data." );
    }

    const SkullbonezCore::Rendering::RenderGraphTextureBinding* graphOutput = data.state->volumetricLight.IsValid()
                                                                                  ? &data.state->volumetricLight
                                                                                  : nullptr;

    if ( !data.state->sceneTarget )
    {
        SB_FATAL( "RunRender", "VolumetricLightPass missing the cinematic target binding." );
    }

    data.state->sceneTarget->Unbind();
    data.state->sceneTargetUnbound = true;
    const size_t expectedTransitions = graphOutput ? 3u : 2u;
    data.state->volumetricTransitionCount = data.renderGraph->ExecuteGraphTransitions( *context.graph, *data.state->compiled,
                                                                                       context.passIndex );

    if ( data.state->volumetricTransitionCount != expectedTransitions )
    {
        // Hazard: sampling the cinematic scene or binding the transient without
        // all compiled producer edges would record an invalid command stream.
        SB_FATAL( "RunRender", "VolumetricLightPass compiled transition count mismatch. expected=%zu actual=%zu",
                  expectedTransitions, data.state->volumetricTransitionCount );
    }

    data.state->volumetricRendered = data.pass->Render( *data.camera, *data.cinematic, *data.renderGeometry,
                                                        *data.renderTextures, *data.renderFrame, *data.renderGraph,
                                                        *data.renderDiagnostics, data.gpuTiming, data.windowWidth,
                                                        data.windowHeight, graphOutput );
}

void ExecuteTonemapGraphCallback( const SkullbonezCore::Rendering::RenderGraphPassContext& context,
                                  TonemapGraphInvocation& data )
{
    if ( !data.pass || !data.cinematic || !data.renderGeometry || !data.renderTextures || !data.renderFrame ||
         !data.renderGraph || !data.renderDiagnostics || !data.state || !data.state->compiled || !context.graph )
    {
        SB_FATAL( "RunRender", "ToneMapPass graph callback missing execution data." );
    }

    const SkullbonezCore::Rendering::RenderGraphTextureBinding* graphVolumetric = ( data.state->volumetricRendered &&
                                                                                    data.state->volumetricLight.IsValid() )
                                                                                      ? &data.state->volumetricLight
                                                                                      : nullptr;

    const bool sceneNeedsPublish = !data.state->sceneTargetUnbound;

    if ( sceneNeedsPublish )
    {
        if ( !data.state->sceneTarget )
        {
            SB_FATAL( "RunRender", "ToneMapPass missing the cinematic target binding." );
        }

        data.state->sceneTarget->Unbind();
        data.state->sceneTargetUnbound = true;
    }

    const size_t requiredSceneTransitions = ( graphVolumetric ? 1u : 0u ) + ( sceneNeedsPublish ? 2u : 0u );
    const size_t expectedTransitions = CountCompiledTransitionsForPass( *data.state->compiled, context.passIndex );

    if ( expectedTransitions < requiredSceneTransitions )
    {
        SB_FATAL( "RunRender", "ToneMapPass omitted a required scene transition. required=%zu compiled=%zu",
                  requiredSceneTransitions, expectedTransitions );
    }

    if ( expectedTransitions > 0 )
    {
        data.state->tonemapTransitionCount = data.renderGraph->ExecuteGraphTransitions( *context.graph,
                                                                                        *data.state->compiled,
                                                                                        context.passIndex );

        if ( data.state->tonemapTransitionCount != expectedTransitions )
        {
            // Hazard: tonemap must not sample until the compiler-selected
            // consumer edge changes the transient from output to shader read.
            SB_FATAL( "RunRender", "ToneMapPass compiled transition count mismatch. expected=%zu actual=%zu",
                      expectedTransitions, data.state->tonemapTransitionCount );
        }
    }

    data.pass->Render( *data.cinematic, *data.renderGeometry, *data.renderTextures, *data.renderFrame,
                       *data.renderDiagnostics, data.gpuTiming, data.windowWidth, data.windowHeight, true,
                       data.state->volumetricRendered, graphVolumetric );
}

void WriteCinematicPostGraphEvidence( const SkullbonezCore::Rendering::RenderGraph& graph, const SkullbonezCore::Rendering::RenderGraphCompileResult& compiled,
                                      const SkullbonezCore::Rendering::RenderGraphTransientMaterializationStats& materialization,
                                      const SkullbonezCore::Rendering::RenderGraphTextureBinding& volumetricBinding, bool volumetricDeclared,
                                      size_t volumetricTransitionCount, size_t tonemapTransitionCount )
{
    // Why: this human-readable file is diagnostic evidence, not frame storage.
    // The allocation phase must match that policy even when the cinematic post
    // graph is emitted from inside the Render frame scope.
    CoreAllocation::RuntimeAllocationScope diagnosticsAllocationScope( CoreAllocation::RuntimeAllocationPhase::Diagnostics );
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
        const SkullbonezCore::Rendering::RenderGraphTransientAllocationDesc& allocation = compiled.transientAllocations[i];

        const SkullbonezCore::Rendering::RenderGraphResourceDesc& resource = graph.Resources()[allocation.resource.index];

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

void ExecuteGraphCallbacksOrFatal( const SkullbonezCore::Rendering::RenderGraph& graph, uint32_t expectedPassCount,
                                   const char* owner )
{
    const size_t passCount = graph.Passes().size();

    if ( expectedPassCount > passCount )
    {
        SB_FATAL( "RunRender", "Executable graph callback range underflow. owner=%s expected=%u passes=%zu",
                  owner ? owner : "unknown", expectedPassCount, passCount );
    }

    const uint32_t firstPass = static_cast<uint32_t>( passCount - expectedPassCount );
    graph.ExecuteCallbacks( SkullbonezCore::Rendering::RenderGraphCallbackExecutionMode::DryRun, firstPass,
                            expectedPassCount );

    const SkullbonezCore::Rendering::RenderGraphCallbackExecutionResult
        executed = graph.ExecuteCallbacks( SkullbonezCore::Rendering::RenderGraphCallbackExecutionMode::Execute, firstPass,
                                           expectedPassCount );

    if ( executed.executedPassCount != expectedPassCount )
    {
        SB_FATAL( "RunRender", "Executable graph omitted a callback. owner=%s expected=%u actual=%u",
                  owner ? owner : "unknown", expectedPassCount, executed.executedPassCount );
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
    const SkullbonezCore::Rendering::RenderGraphResourceHandle colorTarget = AddFrameColorTarget( graph,
                                                                                                  useCinematicTarget );

    const SkullbonezCore::Rendering::RenderGraphResourceHandle depthTarget = AddFrameDepthTarget( graph,
                                                                                                  useCinematicTarget );

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
                                               Rendering::Dx12GraphTransientPool& renderGraph,
                                               const Rendering::FramebufferDX12* target, const char* colorName,
                                               const char* depthName )
{
    GraphFramebufferResources resources;

    if ( !target )
    {
        return resources;
    }

    resources.color = graph.AddExternalResource( colorName, Rendering::RenderGraphResourceAccess::PixelShaderResource,
                                                 renderGraph.ResolveGraphResourceToken( target->GetColorTextureHandle() ) );

    resources.depth = graph.AddExternalResource( depthName, Rendering::RenderGraphResourceAccess::PixelShaderResource,
                                                 renderGraph.ResolveGraphResourceToken( target->GetDepthTextureHandle() ) );

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
        const Rendering::RenderGraphResourceHandle
            mainDepth = graph.AddExternalResource( "MainDepthStencil", Rendering::RenderGraphResourceAccess::DepthWrite );

        graph.AddWrite( acquirePass, mainDepth, Rendering::RenderGraphResourceAccess::DepthWrite );
    }

    BackbufferAcquireGraphInvocation callbackData;
    callbackData.renderGraph = &inputs.renderGraph;
    callbackData.renderFrame = &inputs.renderFrame;
    callbackData.clearFrameTargets = inputs.clearFrameTargets;
    graph.SetPassCallback<ExecuteBackbufferAcquireGraphCallback>( acquirePass, callbackData, true,
                                                                  inputs.clearFrameTargets ? "Frame/BackbufferClear"
                                                                                           : "Frame/UI/TargetAcquire" );

    const Rendering::RenderGraphCompileResult& compiled = CompileRenderPassGraph( graph );
    callbackData.compiled = &compiled;
    callbackData.expectedTransitionCount = CountCompiledTransitionsForPass( compiled, acquirePass );
    ExecuteGraphCallbacksOrFatal( graph, 1u, inputs.clearFrameTargets ? "BackbufferClear" : "UiTargetAcquire" );
}

ShadowPassOutput RuntimeRenderer::ExecuteShadowThroughRenderGraph( const ShadowPassInputs& pass )
{
    Rendering::Dx12GraphTransientPool& renderGraph = m_resources.RenderGraph();
    Rendering::RenderGraph& graph = BeginRenderPassGraph();
    const GraphFramebufferResources
        terrainShadow = AddGraphFramebuffer( graph, renderGraph, m_resources.PassResources().shadows.terrainTarget.get(),
                                             "TerrainShadowMapColor", "TerrainShadowMapDepth" );

    const GraphFramebufferResources
        objectShadow = AddGraphFramebuffer( graph, renderGraph, m_resources.PassResources().shadows.objectTarget.get(),
                                            "ObjectShadowMapColor", "ObjectShadowMapDepth" );

    const size_t targetTransitionCount = terrainShadow.transitionCount + objectShadow.transitionCount;

    const uint32_t shadowPass = graph.AddPass( "ShadowMapPass", Rendering::RenderGraphQueueType::Graphics );
    AddFramebufferWrites( graph, shadowPass, terrainShadow );
    AddFramebufferWrites( graph, shadowPass, objectShadow );

    if ( targetTransitionCount == 0 )
    {
        // Disabled shadows still schedule the callback that clears stale CPU
        // receiver payloads; this stable no-transition row satisfies the graph
        // callback resource contract without pretending a GPU target exists.
        const Rendering::RenderGraphResourceHandle
            inactive = graph.AddExternalResource( "ShadowPassInactive",
                                                  Rendering::RenderGraphResourceAccess::PixelShaderResource );

        graph.AddRead( shadowPass, inactive, Rendering::RenderGraphResourceAccess::PixelShaderResource );
    }

    ShadowGraphInvocation callbackData;
    callbackData.shadowPass = &m_shadowPass;
    callbackData.inputs = &pass;
    callbackData.renderGraph = &renderGraph;
    callbackData.expectedTransitionCount = targetTransitionCount;
    graph.SetPassCallback<ExecuteShadowGraphCallback>( shadowPass, callbackData, true, "Frame/Shadows/ShadowMap" );

    GraphTransitionCallbackData publishData;
    uint32_t publishPass = 0;

    if ( targetTransitionCount > 0 )
    {
        publishPass = graph.AddPass( "ShadowMapPublishPass", Rendering::RenderGraphQueueType::Graphics );
        AddFramebufferReads( graph, publishPass, terrainShadow );
        AddFramebufferReads( graph, publishPass, objectShadow );
        publishData.renderGraph = &renderGraph;
        publishData.expectedTransitionCount = targetTransitionCount;
        graph.SetPassCallback<ExecuteGraphTransitionCallback>( publishPass, publishData, true,
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

void RuntimeRenderer::ExecuteSkyboxThroughRenderGraph( const RenderCameraLighting& camera,
                                                       Rendering::Dx12GeometryOwner& renderGeometry,
                                                       Rendering::Dx12TextureOwner& renderTextures,
                                                       Rendering::Dx12GraphTransientPool& renderGraph )
{
    Rendering::RenderGraph& graph = BeginRenderPassGraph();
    const Rendering::RenderGraphResourceHandle backbuffer = AddBackbufferResource( graph, renderGraph );

    const uint32_t skyboxPass = graph.AddPass( "SkyboxPass", Rendering::RenderGraphQueueType::Graphics );
    graph.AddWrite( skyboxPass, backbuffer, Rendering::RenderGraphResourceAccess::RenderTarget );

    SkyboxGraphInvocation callbackData;
    callbackData.skyPass = &m_skyPass;
    callbackData.camera = &camera;
    callbackData.renderGeometry = &renderGeometry;
    callbackData.renderTextures = &renderTextures;
    graph.SetPassCallback<ExecuteSkyboxGraphCallback>( skyboxPass, callbackData, true, "Frame/Render/Skybox" );

    // Invariant: the ordinary skybox still draws through SkyPass; the graph now
    // owns the scheduling point and resource declaration before live execution.
    CompileRenderPassGraph( graph );
    ExecuteGraphCallbacksOrFatal( graph, 1u, "Skybox" );
}


ReflectionPassOutput RuntimeRenderer::ExecuteReflectionThroughRenderGraph( const ReflectionPassInputs& pass )
{
    Rendering::Dx12GraphTransientPool& renderGraph = m_resources.RenderGraph();
    Rendering::RenderGraph& graph = BeginRenderPassGraph();
    const bool useDxrCandidate = pass.useDxrReflection;

    Rendering::RenderGraphResourceHandle objectShadowResource;

    if ( pass.objectShadow && pass.objectShadow->valid )
    {
        objectShadowResource = graph.AddExternalResource( "ObjectShadowMapDepth",
                                                          Rendering::RenderGraphResourceAccess::PixelShaderResource );
    }

    const uint32_t reflectionPass = graph.AddPass( useDxrCandidate ? "DxrReflectionPass" : "RasterReflectionPass",
                                                   useDxrCandidate ? Rendering::RenderGraphQueueType::Compute
                                                                   : Rendering::RenderGraphQueueType::Graphics );

    if ( objectShadowResource.IsValid() )
    {
        graph.AddRead( reflectionPass, objectShadowResource, Rendering::RenderGraphResourceAccess::PixelShaderResource );
    }

    GraphFramebufferResources rasterReflection;
    Rendering::RenderGraphResourceHandle producedReflection;
    size_t targetTransitionCount = 0;

    if ( useDxrCandidate )
    {
        const uint32_t reflectionHandle = pass.rayTracing.GetReflectionUAVTexture();
        producedReflection = graph.AddExternalResource( "DxrReflectionTexture",
                                                        Rendering::RenderGraphResourceAccess::PixelShaderResource,
                                                        renderGraph.ResolveGraphResourceToken( reflectionHandle ) );

        graph.AddWrite( reflectionPass, producedReflection, Rendering::RenderGraphResourceAccess::UnorderedAccess );
        targetTransitionCount = 1;
    }
    else
    {
        rasterReflection = AddGraphFramebuffer( graph, renderGraph, m_resources.PassResources().reflection.target.get(),
                                                "RasterReflectionColor", "RasterReflectionDepth" );

        AddFramebufferWrites( graph, reflectionPass, rasterReflection );
        targetTransitionCount = rasterReflection.transitionCount;
    }

    if ( targetTransitionCount == 0 && !objectShadowResource.IsValid() )
    {
        const Rendering::RenderGraphResourceHandle
            inactive = graph.AddExternalResource( "ReflectionPassInactive",
                                                  Rendering::RenderGraphResourceAccess::PixelShaderResource );

        graph.AddRead( reflectionPass, inactive, Rendering::RenderGraphResourceAccess::PixelShaderResource );
    }

    ReflectionGraphInvocation callbackData;
    callbackData.reflectionPass = &m_reflectionPass;
    callbackData.inputs = &pass;
    callbackData.renderGraph = &renderGraph;
    callbackData.expectedTransitionCount = targetTransitionCount;
    graph.SetPassCallback<ExecuteReflectionGraphCallback>( reflectionPass, callbackData, true,
                                                           useDxrCandidate ? "Frame/Render/Reflection/DXR"
                                                                           : "Frame/Render/Reflection/Raster" );

    GraphTransitionCallbackData publishData;

    if ( targetTransitionCount > 0 )
    {
        const uint32_t publishPass = graph.AddPass( "ReflectionPublishPass", Rendering::RenderGraphQueueType::Graphics );

        if ( useDxrCandidate )
        {
            graph.AddRead( publishPass, producedReflection, Rendering::RenderGraphResourceAccess::PixelShaderResource );
        }
        else
        {
            AddFramebufferReads( graph, publishPass, rasterReflection );
        }

        publishData.renderGraph = &renderGraph;
        publishData.expectedTransitionCount = targetTransitionCount;
        graph.SetPassCallback<ExecuteGraphTransitionCallback>( publishPass, publishData, true,
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


void RuntimeRenderer::ExecuteSceneTargetBeginThroughRenderGraph( const RenderCameraLighting& camera, const SkullbonezCore::Core::CinematicRenderConfig& cinematic,
                                                                 Rendering::Dx12GeometryOwner& renderGeometry, Rendering::Dx12TextureOwner& renderTextures,
                                                                 Rendering::Dx12FrameOwner& renderFrame, Rendering::Dx12GraphTransientPool& renderGraph,
                                                                 Rendering::Dx12Diagnostics& renderDiagnostics, Rendering::RenderGpuTimingOwner& gpuTiming )
{
    Rendering::RenderGraph& graph = BeginRenderPassGraph();
    const GraphFramebufferResources
        sceneTarget = AddGraphFramebuffer( graph, renderGraph, m_resources.PassResources().cinematicScene.hdrTarget.get(),
                                           "CinematicSceneColor", "CinematicSceneDepth" );

    const uint32_t sceneBeginPass = graph.AddPass( "CinematicSceneBegin", Rendering::RenderGraphQueueType::Graphics );
    AddFramebufferWrites( graph, sceneBeginPass, sceneTarget );

    SceneTargetGraphInvocation callbackData;
    callbackData.sceneTargetPass = &m_sceneTargetPass;
    callbackData.camera = &camera;
    callbackData.cinematic = &cinematic;
    callbackData.renderFrame = &renderFrame;
    callbackData.renderGeometry = &renderGeometry;
    callbackData.renderTextures = &renderTextures;
    callbackData.renderDiagnostics = &renderDiagnostics;
    callbackData.gpuTiming = &gpuTiming;
    callbackData.renderGraph = &renderGraph;
    callbackData.expectedTransitionCount = sceneTarget.transitionCount;
    graph.SetPassCallback<ExecuteSceneTargetGraphCallback>( sceneBeginPass, callbackData, true,
                                                            "Frame/Render/CinematicSceneBegin" );

    // Invariant: the producer callback consumes both compiled edges before
    // binding; the cinematic post graph later publishes both resources for reads.
    const Rendering::RenderGraphCompileResult& compiled = CompileRenderPassGraph( graph );
    callbackData.compiled = &compiled;
    ExecuteGraphCallbacksOrFatal( graph, 1u, "CinematicSceneBegin" );
}


void RuntimeRenderer::ExecuteObjectThroughRenderGraph( const ObjectGraphInputs& inputs )
{
    const ObjectPassInputs& pass = inputs.pass;
    const Rendering::ShadowFrameData* objectShadow = pass.shadow;
    Rendering::RenderGraph& graph = BeginRenderPassGraph();
    Rendering::RenderGraphResourceHandle objectShadowResource;

    if ( objectShadow && objectShadow->valid )
    {
        objectShadowResource = graph.AddExternalResource( "ObjectShadowMapDepth",
                                                          Rendering::RenderGraphResourceAccess::PixelShaderResource );
    }

    const char* passName = pass.mode == ObjectPassMode::Transparent ? "ObjectTransparentPass" : "ObjectOpaquePass";
    const uint32_t objectPass = graph.AddPass( passName, Rendering::RenderGraphQueueType::Graphics );

    if ( objectShadowResource.IsValid() )
    {
        graph.AddRead( objectPass, objectShadowResource, Rendering::RenderGraphResourceAccess::PixelShaderResource );
    }

    AddFrameTargetWrites( graph, objectPass, inputs.useCinematicTarget );

    ObjectGraphInvocation callbackData;
    callbackData.objectPass = &m_objectPass;
    callbackData.inputs = &pass;
    graph.SetPassCallback<ExecuteObjectGraphCallback>( objectPass, callbackData, true,
                                                       pass.mode == ObjectPassMode::Transparent
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
    const TerrainPassInputs& pass = inputs.pass;
    const Rendering::ShadowFrameData* terrainShadow = pass.shadow;
    const Rendering::ShadowFrameData* objectShadow = pass.detailShadow;
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

    TerrainGraphInvocation callbackData;
    callbackData.terrainPass = &m_terrainPass;
    callbackData.inputs = &pass;
    graph.SetPassCallback<ExecuteTerrainGraphCallback>( terrainPass, callbackData, true, "Frame/Render/Terrain" );

    // Invariant: terrain visibility is snapshotted before graph execution, while
    // the graph owns its frame target and shadow-map declaration.
    CompileRenderPassGraph( graph );
    ExecuteGraphCallbacksOrFatal( graph, 1u, "Terrain" );
}


void RuntimeRenderer::ExecuteWaterThroughRenderGraph( const WaterGraphInputs& inputs )
{
    const WaterPassInputs& pass = inputs.pass;
    const ReflectionPassOutput& reflection = pass.reflection;
    Rendering::RenderGraph& graph = BeginRenderPassGraph();

    if ( reflection.reflectionTextureHandle != 0u && !pass.noReflection )
    {
        const Rendering::RenderGraphResourceHandle
            reflectionTexture = graph.AddExternalResource( reflection.usedDxr ? "DxrReflectionTexture"
                                                                              : "RasterReflectionColor",
                                                           Rendering::RenderGraphResourceAccess::PixelShaderResource );

        const uint32_t waterPass = graph.AddPass( "WaterPass", Rendering::RenderGraphQueueType::Graphics );
        graph.AddRead( waterPass, reflectionTexture, Rendering::RenderGraphResourceAccess::PixelShaderResource );
        AddFrameTargetWrites( graph, waterPass, inputs.useCinematicTarget );

        WaterGraphInvocation callbackData;
        callbackData.waterPass = &m_waterPass;
        callbackData.inputs = &pass;
        graph.SetPassCallback<ExecuteWaterGraphCallback>( waterPass, callbackData, true, "Frame/Render/Water" );

        CompileRenderPassGraph( graph );
        ExecuteGraphCallbacksOrFatal( graph, 1u, "Water" );
        return;
    }

    const uint32_t waterPass = graph.AddPass( "WaterPass", Rendering::RenderGraphQueueType::Graphics );
    AddFrameTargetWrites( graph, waterPass, inputs.useCinematicTarget );

    WaterGraphInvocation callbackData;
    callbackData.waterPass = &m_waterPass;
    callbackData.inputs = &pass;
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
    const Rendering::RenderGraphResourceHandle
        colorTarget = graph.AddExternalResource( inputs.useCinematicTarget ? "CinematicSceneColor" : "SwapchainBackbuffer",
                                                 Rendering::RenderGraphResourceAccess::RenderTarget );

    const Rendering::RenderGraphResourceHandle
        depthTarget = graph.AddExternalResource( inputs.useCinematicTarget ? "CinematicSceneDepth" : "MainDepthStencil",
                                                 Rendering::RenderGraphResourceAccess::DepthWrite );

    const Rendering::WorldSurfaceHeightView
        surfaceHeight = m_resources.Terrain().Get()
                            ? Rendering::WorldSurfaceHeightView::Bind<Geometry::Terrain, &SampleWorldSurfaceHeight>( *m_resources.Terrain().Get() )
                            : Rendering::WorldSurfaceHeightView();

    const Rendering::WorldRenderExtensionFrameView frameView { inputs.camera.viewProjection,
                                                               inputs.camera.eye,
                                                               inputs.camera.viewCenter,
                                                               inputs.camera.up,
                                                               inputs.renderTextures,
                                                               inputs.renderGeometry,
                                                               inputs.renderDiagnostics,
                                                               inputs.gpuTiming,
                                                               surfaceHeight };

    Rendering::WorldRenderExtensionScope scope( graph, m_renderPassCompileScratch, colorTarget, depthTarget, frameView );
    return inputs.registration.Register( scope );
}
void RuntimeRenderer::ExecuteReplayGhostsThroughRenderGraph( const ReplayGhostGraphInputs& inputs )
{
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

    ReplayGhostGraphInvocation callbackData;
    callbackData.profiler = m_profiler;
    callbackData.replayVisualPacket = &inputs.replayVisualPacket;
    callbackData.camera = &inputs.camera;
    callbackData.models = &inputs.models;
    callbackData.primitive = &inputs.primitive;
    callbackData.textures = &inputs.textures;
    callbackData.cinematic = inputs.cinematic;
    callbackData.shadow = objectShadow;
    graph.SetPassCallback<ExecuteReplayGhostGraphCallback>( replayPass, callbackData, true,
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
    const DebugOverlayPassInputs& pass = inputs.pass;
    Rendering::RenderGraph& graph = BeginRenderPassGraph();
    const Rendering::RenderGraphResourceHandle
        colorTarget = graph.AddExternalResource( inputs.useCinematicTarget ? "CinematicSceneColor" : "SwapchainBackbuffer",
                                                 Rendering::RenderGraphResourceAccess::RenderTarget );

    const Rendering::RenderGraphResourceHandle
        depthTarget = graph.AddExternalResource( inputs.useCinematicTarget ? "CinematicSceneDepth" : "MainDepthStencil",
                                                 Rendering::RenderGraphResourceAccess::DepthWrite );

    const uint32_t debugPass = graph.AddPass( "DebugOverlayPass", Rendering::RenderGraphQueueType::Graphics );
    graph.AddWrite( debugPass, colorTarget, Rendering::RenderGraphResourceAccess::RenderTarget );
    graph.AddWrite( debugPass, depthTarget, Rendering::RenderGraphResourceAccess::DepthWrite );

    DebugOverlayGraphInvocation callbackData;
    callbackData.debugOverlayPass = &m_debugOverlayPass;
    callbackData.inputs = &pass;
    graph.SetPassCallback<ExecuteDebugOverlayGraphCallback>( debugPass, callbackData, true, "Frame/Render/DebugOverlay" );

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
    Rendering::RenderGraph& graph = BeginRenderPassGraph();
    Rendering::FramebufferDX12* sceneTarget = m_resources.PassResources().cinematicScene.hdrTarget.get();
    const Rendering::RenderGraphResourceHandle
        sceneColor = graph.AddExternalResource( "CinematicSceneColor", Rendering::RenderGraphResourceAccess::RenderTarget,
                                                inputs.renderGraph.ResolveGraphResourceToken( sceneTarget->GetColorTextureHandle() ) );

    const Rendering::RenderGraphResourceHandle
        sceneDepth = graph.AddExternalResource( "CinematicSceneDepth", Rendering::RenderGraphResourceAccess::DepthWrite,
                                                inputs.renderGraph.ResolveGraphResourceToken( sceneTarget->GetDepthTextureHandle() ) );

    const Rendering::RenderGraphResourceHandle backbuffer = AddBackbufferResource( graph, inputs.renderGraph );
    Rendering::RenderGraphResourceHandle volumetricLight;
    const bool volumetricDeclared = m_volumetricPass.CanRender( true, &inputs.cinematic );
    uint32_t expectedCallbacks = 1u;
    uint32_t volumetricPass = 0u;

    if ( volumetricDeclared )
    {
        Rendering::RenderGraphTransientResourceDesc volumetricDesc;
        volumetricDesc.kind = Rendering::RenderGraphResourceKind::Texture2D;
        volumetricDesc.format = Rendering::RenderGraphResourceFormat::RGBA16F;
        volumetricDesc.width = static_cast<uint32_t>( (std::max)( 1, inputs.windowWidth / 2 ) );
        volumetricDesc.height = static_cast<uint32_t>( (std::max)( 1, inputs.windowHeight / 2 ) );
        volumetricDesc.mipLevels = 1;
        volumetricDesc.descriptors.renderTarget = true;
        volumetricDesc.descriptors.shaderResource = true;
        volumetricLight = graph.AddTransientResource( "VolumetricLight", volumetricDesc,
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

    CinematicPostGraphState postState;
    postState.sceneTarget = sceneTarget;
    VolumetricGraphInvocation volumetricInvocation;
    volumetricInvocation.pass = &m_volumetricPass;
    volumetricInvocation.camera = &inputs.camera;
    volumetricInvocation.cinematic = &inputs.cinematic;
    volumetricInvocation.renderGeometry = &inputs.renderGeometry;
    volumetricInvocation.renderTextures = &inputs.renderTextures;
    volumetricInvocation.renderFrame = &inputs.renderFrame;
    volumetricInvocation.renderGraph = &inputs.renderGraph;
    volumetricInvocation.renderDiagnostics = &inputs.renderDiagnostics;
    volumetricInvocation.gpuTiming = &inputs.gpuTiming;
    volumetricInvocation.windowWidth = inputs.windowWidth;
    volumetricInvocation.windowHeight = inputs.windowHeight;
    volumetricInvocation.state = &postState;
    TonemapGraphInvocation tonemapInvocation;
    tonemapInvocation.pass = &m_tonemapPass;
    tonemapInvocation.cinematic = &inputs.cinematic;
    tonemapInvocation.renderGeometry = &inputs.renderGeometry;
    tonemapInvocation.renderTextures = &inputs.renderTextures;
    tonemapInvocation.renderFrame = &inputs.renderFrame;
    tonemapInvocation.renderGraph = &inputs.renderGraph;
    tonemapInvocation.renderDiagnostics = &inputs.renderDiagnostics;
    tonemapInvocation.gpuTiming = &inputs.gpuTiming;
    tonemapInvocation.windowWidth = inputs.windowWidth;
    tonemapInvocation.windowHeight = inputs.windowHeight;
    tonemapInvocation.state = &postState;

    if ( volumetricDeclared )
    {
        graph.SetPassCallback<ExecuteVolumetricGraphCallback>( volumetricPass, volumetricInvocation, true,
                                                               "Frame/Render/VolumetricLight" );
    }

    graph.SetPassCallback<ExecuteTonemapGraphCallback>( tonemapPass, tonemapInvocation, true, "Frame/Render/Tonemap" );

    // Invariant: dry-run executes no draw code. It proves the callback-owned
    // post passes have resource declarations before live callbacks record
    // commands, and the execute path records them in graph order.
    const Rendering::RenderGraphCompileResult& compiled = CompileRenderPassGraph( graph );
    postState.compiled = &compiled;
    Rendering::RenderGraphTransientMaterializationStats transientMaterialization;
    {
        transientMaterialization = inputs.renderGraph.MaterializeGraphTransientResources( graph, compiled );

        if ( volumetricDeclared )
        {
            postState.volumetricLight = inputs.renderGraph.ResolveGraphTextureBinding( volumetricLight );

            if ( !postState.volumetricLight.IsValid() )
            {
                // Recoverable error: if graph-managed texture allocation fails, the
                // optional volumetric callback records no draw and tonemap
                // proceeds without its sample. Keep the failure visible.
                SkullbonezCore::Core::Log()
                    .WriteEventf( "render_graph_volumetric_transient_unavailable materialization_failed=%d "
                                  "hresult=0x%08X resource=%s",
                                  transientMaterialization.failed ? 1 : 0, transientMaterialization.failureHresult,
                                  transientMaterialization.failureResource );

                SkullbonezCore::Core::Log().FlushAll();
            }
        }
    }

    ExecuteGraphCallbacksOrFatal( graph, expectedCallbacks, "CinematicPost" );
    WriteCinematicPostGraphEvidence( graph, compiled, transientMaterialization, postState.volumetricLight,
                                     volumetricDeclared, postState.volumetricTransitionCount,
                                     postState.tonemapTransitionCount );

    CinematicPostFrameOutput result;
    result.volumetricPassExecuted = volumetricDeclared;
    result.volumetricReady = volumetricDeclared && postState.volumetricRendered;
    result.volumetricTextureHandle = postState.volumetricLight.textureHandle;
    result.volumetricWidth = postState.volumetricLight.width;
    result.volumetricHeight = postState.volumetricLight.height;
    return result;
}


int RuntimeRenderer::RenderUiText( RunTimerState& timers, const RuntimeRenderModelFrameView& models, double secondsPerFrame,
                                   UiChromeGraphInvocation& chrome,
                                   UiOperatorDiagnosticsGraphInvocation& operatorDiagnostics,
                                   UiOperatorSettingsGraphInvocation& operatorSettings,
                                   UiOperatorInteractionGraphInvocation& operatorInteraction,
                                   UiOperatorPresentationGraphInvocation& operatorPresentation,
                                   UiOperatorSubmissionGraphInvocation& operatorSubmission, UiReplayGraphInvocation& replay )
{
    // Why: RenderGraph borrows callback userdata until ExecuteGraphCallbacksOrFatal(),
    // so callers own each operation-specific ABI record until this method returns.
    // Invariant: no record collects the UI union; focused passes register in
    // visual order and share exactly one compile/execute cycle.
    Rendering::Dx12Diagnostics* renderDiagnostics = &m_resources.RenderDiagnostics();

    const int drawCallStart = renderDiagnostics->GetFrameDrawCallCount();
    DRAW_CALL_TRACE_SCOPE( *renderDiagnostics, "Frame/UI" );
    const float sceneEnergyForDisplay = m_resources.UiText().BeginFrame( timers, models, secondsPerFrame,
                                                                         chrome.viewport.screenW, chrome.viewport.screenH );

    chrome.reproMessageAgeSeconds = timers.simulationTimer.GetTimeSinceLastStart();

    UI::InGameUIFrameData uiData;
    UiOperatorPrepareGraphInvocation operatorPrepare;
    operatorPrepare.uiData = &uiData;
    operatorPrepare.viewport = chrome.viewport;
    operatorPrepare.drawTestPattern = operatorSettings.debug && operatorSettings.debug->isUITestPattern;

    operatorDiagnostics.uiData = &uiData;
    operatorDiagnostics.timers = &timers;
    operatorDiagnostics.models = &models;
    operatorDiagnostics.secondsPerFrame = secondsPerFrame;
    operatorSettings.uiData = &uiData;
    operatorInteraction.uiData = &uiData;
    operatorPresentation.uiData = &uiData;
    operatorPresentation.sceneEnergyForDisplay = sceneEnergyForDisplay;
    operatorSubmission.uiData = &uiData;
    operatorSubmission.uiPassDrawCallStart = drawCallStart;

    UiOverlayGraphInvocation overlay;
    overlay.viewport = chrome.viewport;
    overlay.mode = chrome.debug ? chrome.debug->overlayMode : OverlayMode::None;
    overlay.modelCount = chrome.scene ? chrome.scene->modelCount : 0;
    overlay.rollingFpsTime = timers.rollingFpsTime;
    overlay.sceneEnergyForDisplay = sceneEnergyForDisplay;

    UiFinalizeGraphInvocation finalize;
    finalize.mode = overlay.mode;

    Rendering::RenderGraph& graph = BeginRenderPassGraph();

    const Rendering::RenderGraphResourceHandle backbuffer = AddBackbufferResource( graph, m_resources.RenderGraph() );

    constexpr uint32_t INVALID_PASS = 0xFFFFFFFFu;
    uint32_t callbackCount = 0;
    const bool textOnly = chrome.debug && chrome.debug->isTextOnly;
    const bool operatorNeeded = operatorSubmission.ui && operatorSubmission.ui->NeedsUiTextPass();
    const bool operatorVisible = operatorSubmission.ui && operatorSubmission.ui->IsVisible();
    const bool profilerBars = overlay.mode == OverlayMode::BarsNormalized || overlay.mode == OverlayMode::BarsAbsolute;

    const uint32_t chromePass = graph.AddPass( "UiChrome", Rendering::RenderGraphQueueType::Graphics );
    graph.AddWrite( chromePass, backbuffer, Rendering::RenderGraphResourceAccess::RenderTarget );
    chrome.pass = &m_resources.UiText();
    chrome.renderGraph = &m_resources.RenderGraph();
    chrome.renderTextures = &m_resources.RenderTextures();
    chrome.renderGeometry = &m_resources.RenderGeometry();
    chrome.renderDiagnostics = renderDiagnostics;
    graph.SetPassCallback<ExecuteUiChromeGraphCallback>( chromePass, chrome, true, "Frame/UI/Chrome" );
    ++callbackCount;

    uint32_t operatorPreparePass = INVALID_PASS;
    uint32_t operatorDiagnosticsPass = INVALID_PASS;
    uint32_t operatorSettingsPass = INVALID_PASS;
    uint32_t operatorInteractionPass = INVALID_PASS;
    uint32_t operatorPresentationPass = INVALID_PASS;
    uint32_t operatorSubmissionPass = INVALID_PASS;

    if ( !textOnly && operatorNeeded )
    {
        // Invariant: one RenderGraph order owns the UIData write sequence.
        // Prepare and submission are scheduled together, which balances the
        // BuildData profile scope and prevents a partially projected draw.
        auto addOperatorPass = [&]( const char* name ) -> uint32_t
        {
            const uint32_t pass = graph.AddPass( name, Rendering::RenderGraphQueueType::Graphics );

            graph.AddWrite( pass, backbuffer, Rendering::RenderGraphResourceAccess::RenderTarget );
            ++callbackCount;
            return pass;
        };

        operatorPreparePass = addOperatorPass( "UiOperatorPrepare" );
        operatorPrepare.pass = &m_resources.UiText();
        operatorPrepare.renderGraph = &m_resources.RenderGraph();
        operatorPrepare.renderTextures = &m_resources.RenderTextures();
        operatorPrepare.renderGeometry = &m_resources.RenderGeometry();
        operatorPrepare.renderDiagnostics = renderDiagnostics;
        graph.SetPassCallback<ExecuteUiOperatorPrepareGraphCallback>( operatorPreparePass, operatorPrepare, true,
                                                                      "Frame/UI/Operator/Prepare" );

        operatorDiagnosticsPass = addOperatorPass( "UiOperatorDiagnostics" );
        operatorDiagnostics.pass = &m_resources.UiText();
        operatorDiagnostics.renderGraph = &m_resources.RenderGraph();
        operatorDiagnostics.renderDiagnostics = renderDiagnostics;
        graph.SetPassCallback<ExecuteUiOperatorDiagnosticsGraphCallback>( operatorDiagnosticsPass, operatorDiagnostics, true,
                                                                          "Frame/UI/Operator/Diagnostics" );

        operatorSettingsPass = addOperatorPass( "UiOperatorSettings" );
        operatorSettings.pass = &m_resources.UiText();
        operatorSettings.renderGraph = &m_resources.RenderGraph();
        graph.SetPassCallback<ExecuteUiOperatorSettingsGraphCallback>( operatorSettingsPass, operatorSettings, true,
                                                                       "Frame/UI/Operator/Settings" );

        operatorInteractionPass = addOperatorPass( "UiOperatorInteraction" );
        operatorInteraction.pass = &m_resources.UiText();
        operatorInteraction.renderGraph = &m_resources.RenderGraph();
        graph.SetPassCallback<ExecuteUiOperatorInteractionGraphCallback>( operatorInteractionPass, operatorInteraction, true,
                                                                          "Frame/UI/Operator/Interaction" );

        operatorPresentationPass = addOperatorPass( "UiOperatorPresentation" );
        operatorPresentation.pass = &m_resources.UiText();
        operatorPresentation.renderGraph = &m_resources.RenderGraph();
        graph.SetPassCallback<ExecuteUiOperatorPresentationGraphCallback>( operatorPresentationPass, operatorPresentation,
                                                                           true, "Frame/UI/Operator/Presentation" );

        operatorSubmissionPass = addOperatorPass( "UiOperatorSubmission" );
        operatorSubmission.pass = &m_resources.UiText();
        operatorSubmission.renderGraph = &m_resources.RenderGraph();
        operatorSubmission.renderResources = &m_resources.RenderResources();
        operatorSubmission.renderTextures = &m_resources.RenderTextures();
        operatorSubmission.renderGeometry = &m_resources.RenderGeometry();
        operatorSubmission.renderDiagnostics = renderDiagnostics;
        graph.SetPassCallback<ExecuteUiOperatorSubmissionGraphCallback>( operatorSubmissionPass, operatorSubmission, true,
                                                                         "Frame/UI/Operator/Submission" );
    }

    uint32_t overlayPass = INVALID_PASS;

    if ( !textOnly && !operatorVisible )
    {
        overlayPass = graph.AddPass( "UiOverlay", Rendering::RenderGraphQueueType::Graphics );
        graph.AddWrite( overlayPass, backbuffer, Rendering::RenderGraphResourceAccess::RenderTarget );
        overlay.pass = &m_resources.UiText();
        overlay.renderGraph = &m_resources.RenderGraph();
        overlay.renderTextures = &m_resources.RenderTextures();
        overlay.renderGeometry = &m_resources.RenderGeometry();
        overlay.renderDiagnostics = renderDiagnostics;
        graph.SetPassCallback<ExecuteUiOverlayGraphCallback>( overlayPass, overlay, true, "Frame/UI/Overlay" );
        ++callbackCount;
    }

    uint32_t replayPass = INVALID_PASS;

    if ( !textOnly )
    {
        replayPass = graph.AddPass( "UiReplay", Rendering::RenderGraphQueueType::Graphics );
        graph.AddWrite( replayPass, backbuffer, Rendering::RenderGraphResourceAccess::RenderTarget );
        replay.pass = &m_resources.UiText();
        replay.renderGraph = &m_resources.RenderGraph();
        replay.renderTextures = &m_resources.RenderTextures();
        replay.renderGeometry = &m_resources.RenderGeometry();
        replay.renderDiagnostics = renderDiagnostics;
        graph.SetPassCallback<ExecuteUiReplayGraphCallback>( replayPass, replay, true, "Frame/UI/Replay" );
        ++callbackCount;
    }

    uint32_t finalizePass = INVALID_PASS;

    if ( !textOnly && !operatorVisible && !profilerBars )
    {
        finalizePass = graph.AddPass( "UiFinalize", Rendering::RenderGraphQueueType::Graphics );
        graph.AddWrite( finalizePass, backbuffer, Rendering::RenderGraphResourceAccess::RenderTarget );
        finalize.pass = &m_resources.UiText();
        finalize.renderGraph = &m_resources.RenderGraph();
        finalize.renderTextures = &m_resources.RenderTextures();
        finalize.renderGeometry = &m_resources.RenderGeometry();
        finalize.renderDiagnostics = renderDiagnostics;
        graph.SetPassCallback<ExecuteUiFinalizeGraphCallback>( finalizePass, finalize, true, "Frame/UI/Finalize" );
        ++callbackCount;
    }

    // Invariant: the focused callbacks retain the historical draw order while
    // sharing one graph compile/execute cycle. Text-only schedules only chrome;
    // visible GameUI skips HUD overlay/final flush but still draws Replay.
    const Rendering::RenderGraphCompileResult& compiled = CompileRenderPassGraph( graph );
    chrome.compiled = &compiled;
    chrome.expectedTransitionCount = CountCompiledTransitionsForPass( compiled, chromePass );
    auto bindCompiledPass = [&]( auto& invocation, uint32_t pass )
    {
        if ( pass != INVALID_PASS )
        {
            invocation.compiled = &compiled;

            invocation.expectedTransitionCount = CountCompiledTransitionsForPass( compiled, pass );
        }
    };
    bindCompiledPass( operatorPrepare, operatorPreparePass );
    bindCompiledPass( operatorDiagnostics, operatorDiagnosticsPass );
    bindCompiledPass( operatorSettings, operatorSettingsPass );
    bindCompiledPass( operatorInteraction, operatorInteractionPass );
    bindCompiledPass( operatorPresentation, operatorPresentationPass );
    bindCompiledPass( operatorSubmission, operatorSubmissionPass );
    bindCompiledPass( overlay, overlayPass );
    bindCompiledPass( replay, replayPass );
    bindCompiledPass( finalize, finalizePass );
    ExecuteGraphCallbacksOrFatal( graph, callbackCount, "UiText" );
    m_resources.UiText().ReportRetainedDrawStats();
    return (std::max)( 0, renderDiagnostics->GetFrameDrawCallCount() - drawCallStart );
}


RenderResourceContext RuntimeRenderer::BuildRenderResourceContext( bool cinematicRender )
{
    return RenderResourceContext { cinematicRender,
                                   m_resources.Assets(),
                                   m_resources.RenderResources(),
                                   m_resources.RenderTextures(),
                                   m_resources.RenderGeometry(),
                                   (std::max)( 1, m_window.ClientWidth() ),
                                   (std::max)( 1, m_window.ClientHeight() ) };
}


RuntimeRenderer::RuntimeRenderer( SkullbonezCore::Core::SbDiagnosticStore& resultDiagnostics,
                                  Rendering::Dx12RenderDevice& renderDevice, Rendering::Dx12FrameOwner& renderFrame,
                                  Rendering::Dx12GraphTransientPool& renderGraph,
                                  Rendering::Dx12ResourceBuilder& renderResources,
                                  Rendering::Dx12TextureOwner& renderTextures, Rendering::Dx12GeometryOwner& renderGeometry,
                                  Rendering::Dx12Diagnostics& renderDiagnostics, Rendering::Dx12RaytracingOwner& raytracing,
                                  bool raytracingAvailable, const RenderWorldView& world, SceneSessionState& scene )
    : m_resultDiagnostics( resultDiagnostics ),
      m_resources( resultDiagnostics, renderDevice, renderFrame, renderGraph, renderResources, renderTextures,
                   renderGeometry, renderDiagnostics, raytracing, raytracingAvailable, world, scene ),
      m_cameras( world.cameras ), m_window( world.window ), m_world( world.worldEnvironment ),
      m_collisionVisualizer( world.overlayResources.m_collisionOverlay ),
      m_broadphaseVisualizer( world.overlayResources.m_broadphaseOverlay ),
      m_physicsDebugVisualizer( world.overlayResources.m_physicsDebugOverlay ), m_profiler( world.profiler ),
      m_fullscreenQuadPass( m_resources.PassResources().fullscreen ),
      m_skyPass( m_resources.PassResources().sky, m_resources.PassResources().fullscreen, m_resources.SkyBoxOwner(),
                 m_resources.Config(), m_profiler ),
      m_sceneTargetPass( m_resources.PassResources().cinematicScene, m_skyPass, m_profiler ),
      m_shadowPass( m_resources.PassResources().shadows, m_resources.Terrain(), m_resources.Config(), m_resources.Log(),
                    m_profiler ),
      m_reflectionPass( m_resources.PassResources().reflection, m_collisionVisualizer, m_skyPass, m_resources.Config(),
                        m_dxrReflectionTransforms.data(), static_cast<int>( m_dxrReflectionTransforms.size() ),
                        m_resources.Log(), m_profiler ),
      m_objectPass( m_collisionVisualizer, m_resources.Config(), m_profiler ),
      m_terrainPass( m_resources.Terrain(), m_resources.Config(), m_profiler ),
      m_waterPass( m_world, m_resources.Config(), m_profiler ),
      m_debugOverlayPass( m_broadphaseVisualizer, m_physicsDebugVisualizer, m_resources.Terrain(), m_resources.Assets(),
                          m_profiler ),
      m_volumetricPass( m_resources.PassResources().cinematicScene, m_resources.PassResources().volumetricLight,
                        m_resources.PassResources().fullscreen, m_resources.Config(), m_profiler ),
      m_tonemapPass( m_resources.PassResources().cinematicScene, m_resources.PassResources().volumetricLight,
                     m_resources.PassResources().tonemap, m_resources.PassResources().fullscreen, m_resources.Config(),
                     m_profiler )
{
    m_renderPassGraphScratch.ReserveForRuntimePassGraph();
    m_renderPassCompileScratch.ReserveForRuntimePassGraph();
}


RuntimeRenderer::~RuntimeRenderer() = default;

const char* RuntimeRenderer::RendererName() const
{
    return m_resources.RenderDiagnostics().GetRendererName();
}


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
    // Lifetime: the schedule publishes the cube-map world owner on every
    // resource epoch. Cinematic-only owners stay lazy until their path is live.
    if ( RuntimeFrameResourcePassRequired( RuntimeFrameResourcePass::Sky, resources.cinematicEnabled ) )
    {
        m_skyPass.EnsureGpuResources( resources );
    }

    if ( RuntimeFrameResourcePassRequired( RuntimeFrameResourcePass::FullscreenQuad, resources.cinematicEnabled ) )
    {
        // Lifetime: cinematic resources are lazy. A window resize or backend
        // rebuild drops them; the next cinematic frame recreates the targets and
        // shader objects with the current window dimensions.
        m_fullscreenQuadPass.EnsureGpuResources( resources );
    }

    if ( RuntimeFrameResourcePassRequired( RuntimeFrameResourcePass::SceneTarget, resources.cinematicEnabled ) )
    {
        m_sceneTargetPass.EnsureGpuResources( resources );
    }

    if ( RuntimeFrameResourcePassRequired( RuntimeFrameResourcePass::Volumetric, resources.cinematicEnabled ) )
    {
        m_volumetricPass.EnsureGpuResources( resources );
    }

    if ( RuntimeFrameResourcePassRequired( RuntimeFrameResourcePass::Tonemap, resources.cinematicEnabled ) )
    {
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
    if ( m_frameGraphFinalized || m_frameGraphRenderGraph != &m_resources.RenderGraph() )
    {
        SB_FATAL( "RunRender", "World rendering requires the current active frame graph." );
    }

    const RuntimeRenderFramePolicy& policy = context.framePolicy;
    const ReplayRenderFrameView& replayFrame = context.replayFrame;
    RuntimeTools& runtimeTools = context.toolOverlay.tools;
    Rendering::Dx12RaytracingOwner& raytracing = m_resources.Raytracing();
    const bool raytracingAvailable = m_resources.RaytracingAvailable();
    m_resources.UiText().SetDxrReflectionPreviewTexture( raytracingAvailable ? raytracing.GetReflectionUAVTexture() : 0 );
    const SkullbonezCore::Core::OrdinaryRenderConfig& ordinaryRender = m_resources.Config().ordinaryRender;
    SkullbonezCore::Core::CinematicRenderConfig ordinaryShadowConfig = renderConfig;
    ordinaryShadowConfig.shadow = ordinaryRender.shadow;
    const SkullbonezCore::Core::CinematicRenderConfig& activeShadowStyle = cinematicRender ? renderConfig
                                                                                           : ordinaryShadowConfig;

    Rendering::Dx12FrameOwner& renderFrame = m_resources.RenderFrame();
    Rendering::Dx12GraphTransientPool& renderGraph = m_resources.RenderGraph();
    Rendering::Dx12ResourceBuilder& renderResources = m_resources.RenderResources();
    Rendering::Dx12TextureOwner& renderTextures = m_resources.RenderTextures();
    Rendering::Dx12GeometryOwner& renderGeometry = m_resources.RenderGeometry();
    Rendering::Dx12Diagnostics& renderDiagnostics = m_resources.RenderDiagnostics();

    const bool shadowMapsEnabled = activeShadowStyle.shadow.enabled && !policy.textOnly;

    const RenderResourceContext resourceContext = BuildRenderResourceContext( cinematicRender );
    {
        CoreAllocation::RuntimeAllocationScope allocationScope( CoreAllocation::RuntimeAllocationPhase::BackendInit );
        EnsureFrameResources( resourceContext );
    }

    const bool useCinematicTarget = cinematicRender && m_sceneTargetPass.IsReady();

    // Invariant: sample the interpolated render camera after SetCamera().
    // Concrete phases borrow this owner-free value so sky, reflection, water,
    // and overlays cannot diverge during camera transitions.
    RenderCameraLighting camera;
    camera.baseView = m_cameras.GetViewMatrix();
    camera.projection = m_window.GetProjectionMatrix();
    camera.viewProjection = camera.projection * camera.baseView;
    camera.eye = m_cameras.GetRenderCameraTranslation();
    camera.viewCenter = m_cameras.GetRenderCameraView();
    camera.up = m_cameras.GetRenderCameraUp();

    if ( cinematicRender )
    {
        const Vector3 sunDirection = CinematicSkySunDirection( renderConfig );
        camera.lightPosition[0] = sunDirection.x;
        camera.lightPosition[1] = sunDirection.y;
        camera.lightPosition[2] = sunDirection.z;
        camera.lightPosition[3] = 0.0f;
    }

    const int windowWidth = (std::max)( 1, m_window.ClientWidth() );
    const int windowHeight = (std::max)( 1, m_window.ClientHeight() );
    const float waterY = m_world.GetFluidSurfaceHeight();
    const Vector3 reflectionEye( camera.eye.x, 2.0f * waterY - camera.eye.y, camera.eye.z );
    const Vector3 reflectionCenter( camera.viewCenter.x, 2.0f * waterY - camera.viewCenter.y, camera.viewCenter.z );

    const Vector3 reflectionUp( camera.up.x, -camera.up.y, camera.up.z );
    const Matrix4 reflectionView = Matrix4::LookAt( reflectionEye, reflectionCenter, reflectionUp );
    const Matrix4 reflectionViewProjection = camera.projection * reflectionView;
    const RuntimeRenderModelFrameView& models = context.renderModels;
    PrimitiveRenderContext primitive { renderResources,
                                       renderTextures,
                                       renderGeometry,
                                       renderDiagnostics,
                                       m_resources.Assets(),
                                       m_resources.Config(),
                                       m_resources.PrimitiveBatches() };

    // Why: these passes borrow subsystem-owned mesh/material resources. Keep
    // readiness checks beside their frame submissions so each draw observes the
    // same resource generation selected for this frame.
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
        ExecuteBackbufferAcquireThroughRenderGraph( { renderGraph, renderFrame, true } );
    }

    const SkullbonezCore::Core::CinematicRenderConfig* activeCinematic = cinematicRender ? &renderConfig : nullptr;
    const SkullbonezCore::Core::CinematicRenderConfig* activeShadowConfig = shadowMapsEnabled ? &activeShadowStyle : nullptr;

    if ( activeShadowConfig )
    {
        CoreAllocation::RuntimeAllocationScope allocationScope( CoreAllocation::RuntimeAllocationPhase::BackendInit );
        m_resources.PrimitiveBatches().EnsureShadowDepthPrimitiveResources( primitive );

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
        const ShadowPassInputs shadowInputs { camera,
                                              models,
                                              primitive,
                                              renderFrame,
                                              renderTextures,
                                              renderDiagnostics,
                                              &m_resources.GpuTiming(),
                                              windowWidth,
                                              windowHeight,
                                              activeShadowConfig,
                                              policy.terrainHidden,
                                              policy.collisionVisualizer };

        shadowPass = ExecuteShadowThroughRenderGraph( shadowInputs );

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
    const Rendering::ShadowFrameData* terrainDetailShadowFrame = objectShadowFrame != terrainShadowFrame ? objectShadowFrame
                                                                                                         : nullptr;

    const bool collisionStateColorsVisible = policy.collisionVisualizer;
    const bool debugTransparentBodyPass = policy.physicsDebugTransparent && policy.physicsDebugAlpha < 1.0f;
    const bool replayFocusFadeActive = replayFrame.focusFadeActive;
    const std::vector<uint8_t>* replayFocusModelMask = replayFocusFadeActive ? replayFrame.focusModelMask : nullptr;
    const bool transparentBodyPass = debugTransparentBodyPass || replayFocusFadeActive;
    const float bodyRenderAlpha = debugTransparentBodyPass ? policy.physicsDebugAlpha : 1.0f;
    const float collisionVisualizerAlphaOverride = debugTransparentBodyPass ? bodyRenderAlpha : -1.0f;
    const bool useDxrReflection = ShouldUseDxrReflection( raytracingAvailable, policy, collisionStateColorsVisible,
                                                          debugTransparentBodyPass );

    const bool waterModeOff = cinematicRender && activeCinematic && activeCinematic->waterMode == 0;
    const bool waterVisibleThisFrame = !policy.waterHidden && !waterModeOff;
    const bool reflectionPassNeeded = waterVisibleThisFrame && !policy.waterNoReflect;

    // Invariant: sky and reflection both consume the interpolated render camera
    // from RenderCameraLighting. Using the selected destination camera here would
    // stretch reflected geometry during camera transitions.
    if ( !cinematicRender )
    {
        PROFILE_GPU_BEGIN( &m_resources.GpuTiming(), "Frame/Render/Skybox" );
        {
            DRAW_CALL_TRACE_SCOPE( renderDiagnostics, "Frame/Render/Skybox" );
            ExecuteSkyboxThroughRenderGraph( camera, renderGeometry, renderTextures, renderGraph );
        }
        PROFILE_GPU_END( &m_resources.GpuTiming(), "Frame/Render/Skybox" );
    }

    ReflectionPassOutput reflection;
    reflection.reflectionSampleViewProjection = reflectionViewProjection;

    if ( reflectionPassNeeded )
    {
        {
            CoreAllocation::RuntimeAllocationScope allocationScope( CoreAllocation::RuntimeAllocationPhase::BackendInit );
            m_reflectionPass.EnsureGpuResources( resourceContext );
        }
        const ReflectionPassInputs reflectionInputs { camera,
                                                      models,
                                                      primitive,
                                                      m_resources.Assets(),
                                                      m_resources.Textures(),
                                                      renderTextures,
                                                      renderFrame,
                                                      renderDiagnostics,
                                                      &m_resources.GpuTiming(),
                                                      raytracing,
                                                      useDxrReflection,
                                                      reflectionView,
                                                      reflectionViewProjection,
                                                      waterY,
                                                      windowWidth,
                                                      windowHeight,
                                                      activeCinematic,
                                                      objectShadowFrame,
                                                      collisionStateColorsVisible,
                                                      collisionVisualizerAlphaOverride,
                                                      bodyRenderAlpha,
                                                      static_cast<float>( policy.totalSimulationSeconds ) };

        reflection = ExecuteReflectionThroughRenderGraph( reflectionInputs );
    }

    if ( useCinematicTarget )
    {
        ExecuteSceneTargetBeginThroughRenderGraph( camera, renderConfig, renderGeometry, renderTextures, renderFrame,
                                                   renderGraph, renderDiagnostics, m_resources.GpuTiming() );
    }

    // Opaque bodies render before terrain/water unless debug transparency asks
    // for a late transparent body pass.
    if ( !debugTransparentBodyPass )
    {
        const ObjectPassInputs objectInputs { camera,
                                              models,
                                              primitive,
                                              m_resources.Assets(),
                                              m_resources.Textures(),
                                              renderTextures,
                                              renderDiagnostics,
                                              &m_resources.GpuTiming(),
                                              ObjectPassMode::Opaque,
                                              activeCinematic,
                                              objectShadowFrame,
                                              collisionStateColorsVisible,
                                              collisionVisualizerAlphaOverride,
                                              1.0f,
                                              replayFocusModelMask,
                                              true };

        ExecuteObjectThroughRenderGraph( { objectInputs, useCinematicTarget } );
    }

    // Terrain receives the broad shadow frame and provides the main world depth
    // that cinematic post passes read later.
    const TerrainPassInputs terrainInputs { camera,
                                            m_resources.Textures(),
                                            renderTextures,
                                            renderDiagnostics,
                                            &m_resources.GpuTiming(),
                                            activeCinematic,
                                            terrainShadowFrame,
                                            terrainDetailShadowFrame,
                                            m_resources.PrimitiveBatches().GetClipPlane(),
                                            policy.terrainHidden };

    ExecuteTerrainThroughRenderGraph( { terrainInputs, useCinematicTarget } );

    // Water is deliberately downstream of ReflectionPass; it samples the
    // reflection texture but never rebuilds it.
    const WaterPassInputs waterInputs { camera,
                                        renderTextures,
                                        renderDiagnostics,
                                        &m_resources.GpuTiming(),
                                        reflection,
                                        activeCinematic,
                                        cinematicRender,
                                        policy.waterHidden,
                                        policy.waterFlatDebug,
                                        policy.waterNoReflect,
                                        policy.waterFreezeDebug,
                                        policy.frozenWaterTime,
                                        static_cast<float>( policy.simulationSeconds ) };

    ExecuteWaterThroughRenderGraph( { waterInputs, useCinematicTarget } );

    const bool worldExtensionRendered = ExecuteWorldExtensionThroughRenderGraph( { camera, context.worldExtension, renderTextures, renderGeometry, renderDiagnostics, m_resources.GpuTiming(),
                                                                                   useCinematicTarget } );

    if ( debugTransparentBodyPass )
    {
        const ObjectPassInputs transparentInputs { camera,
                                                   models,
                                                   primitive,
                                                   m_resources.Assets(),
                                                   m_resources.Textures(),
                                                   renderTextures,
                                                   renderDiagnostics,
                                                   &m_resources.GpuTiming(),
                                                   ObjectPassMode::Transparent,
                                                   activeCinematic,
                                                   objectShadowFrame,
                                                   collisionStateColorsVisible,
                                                   collisionVisualizerAlphaOverride,
                                                   bodyRenderAlpha,
                                                   nullptr,
                                                   true };

        ExecuteObjectThroughRenderGraph( { transparentInputs, useCinematicTarget } );
    }
    else if ( replayFocusFadeActive )
    {
        const ObjectPassInputs fadedInputs { camera,
                                             models,
                                             primitive,
                                             m_resources.Assets(),
                                             m_resources.Textures(),
                                             renderTextures,
                                             renderDiagnostics,
                                             &m_resources.GpuTiming(),
                                             ObjectPassMode::Transparent,
                                             activeCinematic,
                                             objectShadowFrame,
                                             collisionStateColorsVisible,
                                             collisionVisualizerAlphaOverride,
                                             0.5f,
                                             replayFocusModelMask,
                                             false };

        ExecuteObjectThroughRenderGraph( { fadedInputs, useCinematicTarget } );
    }

    {
        CoreAllocation::RuntimeAllocationScope replayAllocationScope( CoreAllocation::RuntimeAllocationPhase::Replay );
        ExecuteReplayGhostsThroughRenderGraph( { camera, models, primitive, m_resources.Textures(),
                                                 *replayFrame.visualPacket, useCinematicTarget, activeCinematic,
                                                 objectShadowFrame } );
    }

    const DebugOverlaySnapshot debugSnapshot = BuildDebugOverlaySnapshot( context.renderModels, context.toolOverlay,
                                                                          policy );

    const DebugOverlayPassInputs debugInputs { camera,
                                               models,
                                               m_resources.Assets(),
                                               renderResources,
                                               renderGeometry,
                                               renderDiagnostics,
                                               &m_resources.GpuTiming(),
                                               debugSnapshot,
                                               runtimeTools,
                                               *replayFrame.visualPacket,
                                               context.retainedOverlay,
                                               replayFrame.contactPresentation };

    const bool debugOverlayRendered = ExecuteDebugOverlayThroughRenderGraph( { debugInputs, useCinematicTarget } );

    bool volumetricReady = false;
    CinematicPostFrameOutput cinematicPostOutput;

    if ( useCinematicTarget )
    {
        cinematicPostOutput = ExecuteCinematicPostThroughRenderGraph( { camera, renderConfig, renderGeometry, renderTextures, renderFrame, renderGraph, renderDiagnostics,
                                                                        m_resources.GpuTiming(), windowWidth, windowHeight } );

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
    frameSnapshot.waterSamplesReflection = waterDebug.rendered && !waterDebug.noReflection && waterDebug.reflectionValid;

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

    const auto logLifecycleStep = [&]( const char* step ) { m_resources.Log().Write( context.phaseName, step ); };

    logLifecycleStep( "flush_before_resource_release" );
    const SkullbonezCore::Core::SbResult flushResult = m_resources.RenderFrame().DrainForResourceRelease();

    if ( !flushResult.Ok() )
    {
        // Recoverable error: return before the first release. The destructor caller
        // converts this non-returnable teardown failure to fatal invariant.
        return flushResult;
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
            m_collisionVisualizer.ResetResources( &m_resources.RenderGeometry() );
            break;
        case BackendResourceStep::UIResources:
            context.ui.ResetPresentationState();
            break;
        case BackendResourceStep::RenderPassResources:
            ReleaseBackendOwnedResources( &m_resources.RenderGeometry() );
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
            context.tools.Laser().ResetResources( &m_resources.RenderGeometry() );
            break;
        }
    }

    return SkullbonezCore::Core::SbResult::Success();
}


void RuntimeRenderer::BeginFrameGraph()
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
    m_frameGraphRenderGraph = &m_resources.RenderGraph();
    m_frameGraphFinalized = false;
}


void RuntimeRenderer::PrepareUiFrameTarget()
{
    // Text-only and ImGui-only frames do not necessarily execute a world or
    // tonemap pass. This graph callback is their normal backbuffer acquisition;
    // on ordinary frames it validates the already-render-target state.
    ExecuteBackbufferAcquireThroughRenderGraph( { m_resources.RenderGraph(), m_resources.RenderFrame(), false } );
}


#if defined( SKULLBONEZ_DEVELOPMENT_TOOLS )
SkullbonezCore::Core::SbResult RuntimeRenderer::RenderDevelopmentUi( DevelopmentTools::ImGuiEditorOwner& editor )
{
    if ( !m_frameGraphRenderGraph )
    {
        return m_resultDiagnostics.Failure( "RuntimeRenderer", "Development UI has no active frame graph" );
    }

    Rendering::RenderGraph& graph = BeginRenderPassGraph();
    const Rendering::RenderGraphResourceHandle backbuffer = AddBackbufferResource( graph, *m_frameGraphRenderGraph );
    const uint32_t pass = graph.AddPass( "ImGuiEditorPass" );
    graph.AddWrite( pass, backbuffer, Rendering::RenderGraphResourceAccess::RenderTarget );

    DevelopmentUiGraphInvocation callbackData;
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


void RuntimeRenderer::FinalizeFrameGraphInternal( const char* declarationOnlyPassName, bool appendPresent,
                                                  bool releaseGraphStorage )
{
    if ( m_frameGraphFinalized || !m_frameGraphRenderGraph )
    {
        SB_FATAL( "RunRender", "Frame graph finalization requires one active, unfinished frame graph." );
    }

    Rendering::RenderGraph& graph = BeginRenderPassGraph();

    if ( appendPresent )
    {
        const Rendering::RenderGraphResourceHandle backbuffer = AddBackbufferResource( graph, *m_frameGraphRenderGraph );

        // Declaration-only exception: Present is the external swap-chain/fence
        // boundary after command recording. The backend consumes this final edge.
        const uint32_t presentPass = graph.AddPass( "Present" );
        graph.AddWrite( presentPass, backbuffer, Rendering::RenderGraphResourceAccess::Present );
    }

    CompileRenderPassGraph( graph );

    const Rendering::RenderGraphExecutionContractResult contract = graph.ValidateFrameExecutionContract( declarationOnlyPassName );

    if ( !contract.IsValid() || contract.callbackPassCount + contract.declarationOnlyPassCount != graph.Passes().size() )
    {
        SB_FATAL( "RunRender",
                  "Production frame graph violates callback ownership. callbacks=%zu declarations=%zu expected=%zu "
                  "name_match=%d enabled=%d passes=%zu",
                  contract.callbackPassCount, contract.declarationOnlyPassCount, contract.expectedDeclarationOnlyPassCount,
                  contract.declarationOnlyNameMatches ? 1 : 0, contract.allCallbacksEnabled ? 1 : 0, graph.Passes().size() );
    }

    {
        CoreAllocation::RuntimeAllocationScope diagnosticsAllocationScope( CoreAllocation::RuntimeAllocationPhase::Diagnostics );
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


RenderDiagnosticsReadout RuntimeRenderer::BuildDiagnosticsReadout() const
{
    Rendering::Dx12Diagnostics* renderDiagnostics = &m_resources.RenderDiagnostics();

    RenderDiagnosticsReadout readout;
    sprintf_s( readout.rendererName.data(), readout.rendererName.size(), "%s", renderDiagnostics->GetRendererName() );
    readout.drawCalls = renderDiagnostics->GetFrameDrawCallCount();
    readout.memory = renderDiagnostics->GetRenderMemoryStats();
    return readout;
}


bool RuntimeRenderer::RenderFrameEntry( const FrameEntryContext& context )
{
    m_resources.UiText().SetDxrReflectionPreviewTexture( 0 );
    const RuntimeRenderFramePolicy& policy = context.framePolicy;

    if ( policy.textOnly )
    {
        return false;
    }

    const bool cinematicRender = context.cinematicRequested && !policy.textOnly;

    // Why: persistent render owners are already members of RuntimeRenderer.
    // Only the top-level frame transaction and derived cinematic values cross
    // into pass sequencing; no duplicate service/input bag is constructed.
    return RenderPreparedFrame( context, context.cinematic, cinematicRender );
}
