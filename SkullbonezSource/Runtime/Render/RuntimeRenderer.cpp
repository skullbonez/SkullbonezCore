/*
File: SkullbonezSource/Runtime/Render/RuntimeRenderer.cpp
Purpose:
  Coordinates render passes for the active scene.

Summary:
  Renderer-facing code builds one frame context and runs named passes in
  the same order the image is produced.

Glossary:
  Render pass: Named slice of RuntimeRenderer::RenderFrame() with explicit
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

Invariants:
  - RuntimeRenderer::RenderFrameEntry() owns pass order. Pass classes may bind
    targets and restore local render state, but they must not present or advance
    the frame.
  - Pass resource reset hooks run while the renderer backend is alive, because
    framebuffers, shaders, and dynamic vertex buffers can own backend objects.
  - Pass input/output structs borrow data for one frame only. Do not cache
    pointers returned from ShadowPassOutput or ReflectionPassOutput consumers.

Related:
  - SkullbonezSource/Runtime/Render/RuntimeRenderPasses.h declares pass contracts.
  - SkullbonezSource/Runtime/Render/RuntimeRenderer.h declares the render owner.
  - SkullbonezSource/Rendering/RenderPipeline.h owns executed frame graph diagnostics.
  - Agentic/Reference/comment-style-guide.md
*/
#include "RuntimeRenderer.h"
#include "../../Rendering/IRenderRayTracing.h"
#include "../../Assets/AssetKeys.h"
#include "RuntimeRenderPasses.h"
#include "../CameraCollection.h"
#include "../RunCameraState.h"
#include "../RunTimerState.h"
#include "../RuntimeDiagnostics.h"
#include "../Diagnostics/SceneMemoryDiagnostics.h"
#include "../RuntimeOverlayDiagnostics.h"
#include "../Window.h"
#include "../Scene/SceneWorld.h"
#include "../Tools/RuntimeTools.h"
#include "../Debug/CollisionVisualizer.h"
#include "../Scene/SceneTerrain.h"
#include "../Allocation/RuntimeAllocationTracker.h"
#include "../Allocation/RuntimeReserveAllocator.h"
#include "../Replay/ReplayOverlayRenderer.h"
#include "../RuntimeTuning.h"
#include "../Scene/SceneController.h"
#include "../../Assets/TextureCollection.h"
#include "../../Core/FatalError.h"
#include "../../Core/Log.h"
#include "../../Core/Profiler.h"
#include "../../Physics/ColliderStore.h"
#include "../../Physics/PhysicsEngine.h"
#include "../../Rendering/PrimitiveBatchRenderer.h"
#include "../../Rendering/IRenderDiagnostics.h"
#include "../../Rendering/IRenderDeviceLifecycle.h"
#include "../../Rendering/RenderInstanceStore.h"
#include "../../Rendering/RenderGraph.h"
#include "../../Rendering/RenderPipeline.h"
#include "../../UI/UI.h"
#include "../../World/SkyBox.h"
#include "../../World/WorldEnvironment.h"

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
namespace RuntimeAllocation = SkullbonezCore::Runtime::Allocation;
using SkullbonezCore::Math::Vector::Vector3;

namespace
{
float LerpFloat( float from, float to, float t )
{
    return from + ( to - from ) * t;
}

float ApproachFloat( float current, float target, float dtSeconds, float secondsToTarget )
{
    if ( secondsToTarget <= 0.0f )
    {
        return target;
    }
    const float step = std::clamp( dtSeconds / secondsToTarget, 0.0f, 1.0f );
    if ( current < target )
    {
        return (std::min)( target, current + step );
    }
    return (std::max)( target, current - step );
}

void ApplyConsequenceGrade( SkullbonezCore::Core::CinematicRenderConfig& cinematic, float strength )
{
    const float s = std::clamp( strength, 0.0f, 1.0f );
    if ( s <= 0.0f )
    {
        return;
    }

    // Concept: the consequence grade is a frame-local presentation override.
    // It pushes the world down into cool silhouettes beneath a near-black dome
    // while replay ribbons remain the primary causality read.
    constexpr float atmosphereDisableStrength = 0.15f;
    const float atmosphereFade = std::clamp( s / atmosphereDisableStrength, 0.0f, 1.0f );
    cinematic.enabled = true;
    cinematic.exposure = LerpFloat( cinematic.exposure, (std::max)( 0.05f, cinematic.exposure * 0.36f ), s );
    cinematic.styleSaturation = LerpFloat( cinematic.styleSaturation, 0.24f, s );
    cinematic.styleContrast = LerpFloat( cinematic.styleContrast, 1.12f, s );
    cinematic.styleVignette = LerpFloat( cinematic.styleVignette, (std::max)( cinematic.styleVignette, 0.62f ), s );
    cinematic.bloomEnabled = true;
    cinematic.bloomThreshold = LerpFloat( cinematic.bloomThreshold, 0.62f, s );
    cinematic.bloomKnee = LerpFloat( cinematic.bloomKnee, 0.72f, s );
    cinematic.bloomStrength = LerpFloat( cinematic.bloomStrength, (std::max)( cinematic.bloomStrength, 0.62f ), s );
    cinematic.bloomRadius = LerpFloat( cinematic.bloomRadius, (std::max)( cinematic.bloomRadius, 4.8f ), s );
    cinematic.fogEnabled = true;
    cinematic.fogColorR = LerpFloat( cinematic.fogColorR, 0.18f, s );
    cinematic.fogColorG = LerpFloat( cinematic.fogColorG, 0.24f, s );
    cinematic.fogColorB = LerpFloat( cinematic.fogColorB, 0.34f, s );
    cinematic.fogMaxOpacity = LerpFloat( cinematic.fogMaxOpacity, (std::max)( cinematic.fogMaxOpacity, 0.28f ), s );
    // Why: boolean atmosphere switches cannot interpolate. Fade their energy to
    // zero over the first part of the grade, then disable the passes so clouds,
    // the solar disc, and shafts cannot re-light the prediction sky.
    cinematic.cloudIntensity = LerpFloat( cinematic.cloudIntensity, 0.0f, atmosphereFade );
    cinematic.sunIntensity = LerpFloat( cinematic.sunIntensity, 0.0f, atmosphereFade );
    cinematic.skyGlowStrength = LerpFloat( cinematic.skyGlowStrength, 0.0f, atmosphereFade );
    cinematic.sunShaftStrength = LerpFloat( cinematic.sunShaftStrength, 0.0f, atmosphereFade );
    cinematic.volumetricStrength = LerpFloat( cinematic.volumetricStrength, 0.0f, atmosphereFade );
    if ( s >= atmosphereDisableStrength )
    {
        cinematic.cloudsEnabled = false;
        cinematic.godRaysEnabled = false;
    }
    // The target is deliberately a little above literal black because the
    // consequence exposure reduction follows this pass; after tonemapping it
    // reads as only a faint blue horizon beneath the black zenith.
    cinematic.skyHorizonR = LerpFloat( cinematic.skyHorizonR, 0.035f, s );
    cinematic.skyHorizonG = LerpFloat( cinematic.skyHorizonG, 0.05f, s );
    cinematic.skyHorizonB = LerpFloat( cinematic.skyHorizonB, 0.08f, s );
    cinematic.skyZenithR = LerpFloat( cinematic.skyZenithR, 0.0f, s );
    cinematic.skyZenithG = LerpFloat( cinematic.skyZenithG, 0.0f, s );
    cinematic.skyZenithB = LerpFloat( cinematic.skyZenithB, 0.0f, s );
    cinematic.terrainTintR = LerpFloat( cinematic.terrainTintR, 0.08f, s );
    cinematic.terrainTintG = LerpFloat( cinematic.terrainTintG, 0.14f, s );
    cinematic.terrainTintB = LerpFloat( cinematic.terrainTintB, 0.18f, s );
    cinematic.terrainAccentR = LerpFloat( cinematic.terrainAccentR, 0.02f, s );
    cinematic.terrainAccentG = LerpFloat( cinematic.terrainAccentG, 0.07f, s );
    cinematic.terrainAccentB = LerpFloat( cinematic.terrainAccentB, 0.12f, s );
}

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
    const SkullbonezCore::Core::CinematicRenderConfig* cinematic = nullptr;
    bool terrainHidden = false;
    bool collisionVisualizerVisible = false;
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
    SkullbonezCore::Core::Profiler* profiler = nullptr;
    SkullbonezCore::Text::TextBatch* textBatch = nullptr;
    const SkullbonezCore::UI::UIRenderContext* uiRender = nullptr;
    const UiTextPassState* state = nullptr;
    RunTimerState* timers = nullptr;
    SkullbonezCore::UI::InGameUI* ui = nullptr;
    const RuntimeRenderModelFrameView* models = nullptr;
    DiagnosticsRuntime* diagnosticsRuntime = nullptr;
    const ReplayHudStatus* replayHud = nullptr;
    const ReplayOverlay::ReplayOverlayRenderContext* replayOverlayContext = nullptr;
    const SkullbonezCore::Core::CinematicRenderConfig* cinematic = nullptr;
    bool cinematicRendering = false;
    SkullbonezCore::Rendering::IRenderRayTracing* renderRayTracing = nullptr;
    double secondsPerFrame = 0.0;
};

struct TornadoVisualGraphCallbackData
{
    TornadoVisualPass* tornadoVisualPass = nullptr;
    const RenderFrameContext* frame = nullptr;
    const TornadoVisualSnapshot* snapshot = nullptr;
    bool rendered = false;
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

void ExecuteShadowGraphCallback( const SkullbonezCore::Rendering::RenderGraphPassContext& /*context*/, void* userData )
{
    auto* data = static_cast<ShadowGraphCallbackData*>( userData );
    if ( !data || !data->shadowPass || !data->frame )
    {
        SB_FATAL( "RunRender", "ShadowMapPass graph callback missing execution data." );
    }
    data->output = data->shadowPass->Render(
        { *data->frame, data->cinematic, data->terrainHidden, data->collisionVisualizerVisible } );
}

void ExecuteReflectionGraphCallback( const SkullbonezCore::Rendering::RenderGraphPassContext& /*context*/,
                                     void* userData )
{
    auto* data = static_cast<ReflectionGraphCallbackData*>( userData );
    if ( !data || !data->reflectionPass || !data->skyPass || !data->frame )
    {
        SB_FATAL( "RunRender", "ReflectionPass graph callback missing execution data." );
    }
    data->output = data->reflectionPass->Render( { *data->frame,
                                                   data->cinematic,
                                                   data->objectShadow,
                                                   data->waterRayTracingReflection,
                                                   data->waterNoReflection,
                                                   data->collisionStateColorsVisible,
                                                   data->transparentBodyPass,
                                                   data->collisionVisualizerAlphaOverride,
                                                   data->bodyAlpha,
                                                   data->simulationTimeSeconds },
                                                 *data->skyPass );
}

void ExecuteObjectGraphCallback( const SkullbonezCore::Rendering::RenderGraphPassContext& /*context*/, void* userData )
{
    auto* data = static_cast<ObjectGraphCallbackData*>( userData );
    if ( !data || !data->objectPass || !data->frame )
    {
        SB_FATAL( "RunRender", "ObjectPass graph callback missing execution data." );
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
        SB_FATAL( "RunRender", "TerrainPass graph callback missing execution data." );
    }
    const float* clipPlane = data->frame->primitiveBatches ? data->frame->primitiveBatches->GetClipPlane() : nullptr;
    data->terrainPass->Render(
        { *data->frame, data->cinematic, data->shadow, data->detailShadow, clipPlane, data->terrainHidden } );
}

void ExecuteWaterGraphCallback( const SkullbonezCore::Rendering::RenderGraphPassContext& /*context*/, void* userData )
{
    auto* data = static_cast<WaterGraphCallbackData*>( userData );
    if ( !data || !data->waterPass || !data->frame || !data->reflection )
    {
        SB_FATAL( "RunRender", "WaterPass graph callback missing execution data." );
    }
    data->waterPass->Render( { *data->frame,
                               *data->reflection,
                               data->cinematic,
                               data->waterHidden,
                               data->flatWater,
                               data->noReflection,
                               data->freezeTime,
                               data->frozenTime,
                               data->liveWaterTime } );
}

void ExecuteTornadoVisualGraphCallback( const SkullbonezCore::Rendering::RenderGraphPassContext& /*context*/,
                                        void* userData )
{
    auto* data = static_cast<TornadoVisualGraphCallbackData*>( userData );
    if ( !data || !data->tornadoVisualPass || !data->frame || !data->snapshot )
    {
        SB_FATAL( "RunRender", "TornadoVisualPass graph callback missing execution data." );
    }
    data->rendered = data->tornadoVisualPass->Render( { *data->frame, *data->snapshot } );
}

void ExecuteDebugOverlayGraphCallback( const SkullbonezCore::Rendering::RenderGraphPassContext& /*context*/,
                                       void* userData )
{
    auto* data = static_cast<DebugOverlayGraphCallbackData*>( userData );
    if ( !data || !data->debugOverlayPass || !data->frame || !data->snapshot || !data->runtimeTools ||
         !data->replayVisualPacket )
    {
        SB_FATAL( "RunRender", "DebugOverlayPass graph callback missing execution data." );
    }
    data->rendered = data->debugOverlayPass->Render(
        { *data->frame, *data->snapshot, *data->runtimeTools, *data->replayVisualPacket } );
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
    assert( frame.renderResources && frame.renderCommands && frame.renderDiagnostics && frame.assets &&
            frame.primitiveBatches );
    const PrimitiveRenderContext primitiveContext{ *frame.renderResources,
                                                   *frame.renderCommands,
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
                                      void* userData )
{
    auto* data = static_cast<ReplayGhostGraphCallbackData*>( userData );
    if ( !data || !data->replayVisualPacket || !data->frame || !data->config )
    {
        SB_FATAL( "RunRender", "ReplayPredictionGhostPass graph callback missing execution data." );
    }
    RenderReplayPredictionGhosts( *data->replayVisualPacket,
                                  data->profiler,
                                  *data->frame,
                                  *data->config,
                                  data->cinematic,
                                  data->shadow );
}

void ExecuteSceneTargetGraphCallback( const SkullbonezCore::Rendering::RenderGraphPassContext& /*context*/,
                                      void* userData )
{
    auto* data = static_cast<SceneTargetGraphCallbackData*>( userData );
    if ( !data || !data->sceneTargetPass || !data->skyPass || !data->frame )
    {
        SB_FATAL( "RunRender", "CinematicSceneBegin graph callback missing execution data." );
    }
    data->sceneTargetPass->Begin( *data->frame, *data->skyPass );
}

void ExecuteSkyboxGraphCallback( const SkullbonezCore::Rendering::RenderGraphPassContext& /*context*/, void* userData )
{
    auto* data = static_cast<SkyboxGraphCallbackData*>( userData );
    if ( !data || !data->skyPass || !data->frame )
    {
        SB_FATAL( "RunRender", "SkyboxPass graph callback missing execution data." );
    }
    data->skyPass->Render( *data->frame, data->frame->baseView, SkyPassMode::CubemapOnly );
}

void ExecuteUiTextGraphCallback( const SkullbonezCore::Rendering::RenderGraphPassContext& /*context*/, void* userData )
{
    auto* data = static_cast<UiTextGraphCallbackData*>( userData );
    if ( !data || !data->uiTextPass || !data->renderDiagnostics || !data->textBatch || !data->uiRender ||
         !data->state || !data->timers || !data->ui || !data->models || !data->diagnosticsRuntime || !data->replayHud ||
         !data->replayOverlayContext || !data->cinematic || !data->uiRender->IsReady() )
    {
        SB_FATAL( "RunRender", "UiTextPass graph callback missing execution data." );
    }
    data->uiTextPass->Render( { *data->state,
                                *data->timers,
                                *data->ui,
                                *data->renderDiagnostics,
                                data->profiler,
                                *data->textBatch,
                                *data->uiRender,
                                *data->models,
                                *data->diagnosticsRuntime,
                                *data->replayHud,
                                *data->replayOverlayContext,
                                *data->cinematic,
                                data->cinematicRendering,
                                data->renderRayTracing,
                                data->secondsPerFrame } );
}

void ExecuteVolumetricGraphCallback( const SkullbonezCore::Rendering::RenderGraphPassContext& /*context*/,
                                     void* userData )
{
    auto* data = static_cast<CinematicPostGraphCallbackData*>( userData );
    if ( !data || !data->volumetricPass || !data->frame )
    {
        SB_FATAL( "RunRender", "VolumetricLightPass graph callback missing execution data." );
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
        SB_FATAL( "RunRender", "ToneMapPass graph callback missing execution data." );
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

bool TornadoSystemVectorsVisible( const Physics::TornadoSystemConfig& config )
{
    if ( config.visualizeVelocityField )
    {
        return true;
    }
    for ( const Physics::TornadoVortexConfig& vortex : config.vortices )
    {
        if ( vortex.field.visualizeVelocityField )
        {
            return true;
        }
    }
    return false;
}

} // namespace

RuntimeRenderer::ShadowGraphResult
RuntimeRenderer::ExecuteShadowThroughRenderGraph( const RenderFrameContext& frame,
                                                  const SkullbonezCore::Core::CinematicRenderConfig* activeShadowConfig,
                                                  bool terrainHidden,
                                                  bool collisionVisualizerVisible )
{
    Rendering::RenderGraph& graph = BeginRenderPassGraph();
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
    callbackData.terrainHidden = terrainHidden;
    callbackData.collisionVisualizerVisible = collisionVisualizerVisible;
    graph.SetPassCallback( shadowPass, ExecuteShadowGraphCallback, &callbackData, true, "Frame/Shadows/ShadowMap" );

    // Invariant: even when activeShadowConfig is null, ShadowPass::Render clears
    // stale receiver payloads. The graph owns that reset scheduling point and
    // declares the stable shadow-map resources produced when shadows are active.
    CompileRenderPassGraph( graph );
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
    Rendering::RenderGraph& graph = BeginRenderPassGraph();
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
    CompileRenderPassGraph( graph );
    graph.ExecuteCallbacks( Rendering::RenderGraphCallbackExecutionMode::DryRun );
    const Rendering::RenderGraphCallbackExecutionResult executed =
        graph.ExecuteCallbacks( Rendering::RenderGraphCallbackExecutionMode::Execute );
    return executed.executedPassCount == 1u;
}


RuntimeRenderer::ReflectionGraphResult RuntimeRenderer::ExecuteReflectionThroughRenderGraph(
    const RenderFrameContext& frame,
    const SkullbonezCore::Core::CinematicRenderConfig* activeCinematic,
    const Rendering::ShadowFrameData* objectShadow,
    bool collisionStateColorsVisible,
    bool debugTransparentBodyPass,
    float collisionVisualizerAlphaOverride,
    float bodyAlpha,
    bool waterRayTracingReflection,
    bool waterNoReflection,
    float simulationTimeSeconds )
{
    Rendering::RenderGraph& graph = BeginRenderPassGraph();
    const bool useDxrCandidate = frame.renderDiagnostics && frame.renderRayTracing &&
                                 frame.renderDiagnostics->GetCapabilities().supportsDxrReflection &&
                                 waterRayTracingReflection && !waterNoReflection && !collisionStateColorsVisible &&
                                 !debugTransparentBodyPass;

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
    callbackData.waterRayTracingReflection = waterRayTracingReflection;
    callbackData.waterNoReflection = waterNoReflection;
    callbackData.collisionStateColorsVisible = collisionStateColorsVisible;
    callbackData.transparentBodyPass = debugTransparentBodyPass;
    callbackData.collisionVisualizerAlphaOverride = collisionVisualizerAlphaOverride;
    callbackData.bodyAlpha = bodyAlpha;
    callbackData.simulationTimeSeconds = simulationTimeSeconds;
    graph.SetPassCallback( reflectionPass,
                           ExecuteReflectionGraphCallback,
                           &callbackData,
                           true,
                           useDxrCandidate ? "Frame/Render/Reflection/DXR" : "Frame/Render/Reflection/Raster" );

    // Invariant: reflection still chooses DXR or raster in ReflectionPass using
    // the same runtime conditions. The graph now owns the scheduling point and
    // declares the texture family that WaterPass samples later.
    CompileRenderPassGraph( graph );
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
    Rendering::RenderGraph& graph = BeginRenderPassGraph();
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
    CompileRenderPassGraph( graph );
    graph.ExecuteCallbacks( Rendering::RenderGraphCallbackExecutionMode::DryRun );
    const Rendering::RenderGraphCallbackExecutionResult executed =
        graph.ExecuteCallbacks( Rendering::RenderGraphCallbackExecutionMode::Execute );
    return executed.executedPassCount == 1u;
}


bool RuntimeRenderer::ExecuteObjectThroughRenderGraph(
    const RenderFrameContext& frame,
    ObjectPassMode mode,
    bool useCinematicTarget,
    const SkullbonezCore::Core::CinematicRenderConfig* activeCinematic,
    const Rendering::ShadowFrameData* objectShadow,
    bool collisionStateColorsVisible,
    float collisionVisualizerAlphaOverride,
    float bodyAlpha,
    const std::vector<uint8_t>* replayFocusModelMask,
    bool drawMaskedModels )
{
    Rendering::RenderGraph& graph = BeginRenderPassGraph();
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
    CompileRenderPassGraph( graph );
    graph.ExecuteCallbacks( Rendering::RenderGraphCallbackExecutionMode::DryRun );
    const Rendering::RenderGraphCallbackExecutionResult executed =
        graph.ExecuteCallbacks( Rendering::RenderGraphCallbackExecutionMode::Execute );
    return executed.executedPassCount == 1u;
}


bool RuntimeRenderer::ExecuteTerrainThroughRenderGraph(
    const RenderFrameContext& frame,
    bool useCinematicTarget,
    const SkullbonezCore::Core::CinematicRenderConfig* activeCinematic,
    const Rendering::ShadowFrameData* terrainShadow,
    const Rendering::ShadowFrameData* objectShadow,
    bool terrainHidden )
{
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

    const uint32_t terrainPass = graph.AddPass( "TerrainPass",
                                                Rendering::RenderGraphQueueType::Graphics,
                                                Rendering::RenderGraphBarrierPolicy::HandoffValidated );
    if ( terrainShadowResource.IsValid() )
    {
        graph.AddRead( terrainPass, terrainShadowResource, Rendering::RenderGraphResourceAccess::PixelShaderResource );
    }
    if ( objectShadowResource.IsValid() )
    {
        graph.AddRead( terrainPass, objectShadowResource, Rendering::RenderGraphResourceAccess::PixelShaderResource );
    }
    AddFrameTargetWrites( graph, terrainPass, useCinematicTarget );

    TerrainGraphCallbackData callbackData;
    callbackData.terrainPass = &m_terrainPass;
    callbackData.frame = &frame;
    callbackData.cinematic = activeCinematic;
    callbackData.shadow = terrainShadow;
    callbackData.detailShadow = objectShadow;
    callbackData.terrainHidden = terrainHidden;
    graph.SetPassCallback( terrainPass, ExecuteTerrainGraphCallback, &callbackData, true, "Frame/Render/Terrain" );

    // Invariant: terrain visibility is snapshotted before graph execution, while
    // the graph owns its frame target and shadow-map declaration.
    CompileRenderPassGraph( graph );
    graph.ExecuteCallbacks( Rendering::RenderGraphCallbackExecutionMode::DryRun );
    const Rendering::RenderGraphCallbackExecutionResult executed =
        graph.ExecuteCallbacks( Rendering::RenderGraphCallbackExecutionMode::Execute );
    return executed.executedPassCount == 1u;
}


bool RuntimeRenderer::ExecuteWaterThroughRenderGraph(
    const RenderFrameContext& frame,
    const ReflectionPassOutput& reflection,
    bool useCinematicTarget,
    const SkullbonezCore::Core::CinematicRenderConfig* activeCinematic,
    bool waterHidden,
    bool flatWater,
    bool noReflection,
    bool freezeTime,
    float frozenTime,
    float liveWaterTime )
{
    Rendering::RenderGraph& graph = BeginRenderPassGraph();
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
        callbackData.liveWaterTime = liveWaterTime;
        graph.SetPassCallback( waterPass, ExecuteWaterGraphCallback, &callbackData, true, "Frame/Render/Water" );

        CompileRenderPassGraph( graph );
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
    callbackData.liveWaterTime = liveWaterTime;
    graph.SetPassCallback( waterPass, ExecuteWaterGraphCallback, &callbackData, true, "Frame/Render/Water" );

    // Invariant: WaterPass may decide to draw the no-reflection shader path or
    // skip hidden water, but the graph owns that decision point and target
    // declaration every frame.
    CompileRenderPassGraph( graph );
    graph.ExecuteCallbacks( Rendering::RenderGraphCallbackExecutionMode::DryRun );
    const Rendering::RenderGraphCallbackExecutionResult executed =
        graph.ExecuteCallbacks( Rendering::RenderGraphCallbackExecutionMode::Execute );
    return executed.executedPassCount == 1u;
}


RuntimeRenderer::GraphPassResult
RuntimeRenderer::ExecuteTornadoVisualThroughRenderGraph( const RenderFrameContext& frame,
                                                         bool useCinematicTarget,
                                                         const TornadoVisualSnapshot& snapshot )
{
    Rendering::RenderGraph& graph = BeginRenderPassGraph();
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
    callbackData.snapshot = &snapshot;
    graph.SetPassCallback( tornadoPass,
                           ExecuteTornadoVisualGraphCallback,
                           &callbackData,
                           true,
                           "Frame/Render/TornadoVisual" );

    // Invariant: TornadoVisualPass may skip drawing after rebuilding its
    // transient vertex list. Callback ownership is still true when the graph
    // schedules that decision point in frame order.
    CompileRenderPassGraph( graph );
    graph.ExecuteCallbacks( Rendering::RenderGraphCallbackExecutionMode::DryRun );
    const Rendering::RenderGraphCallbackExecutionResult executed =
        graph.ExecuteCallbacks( Rendering::RenderGraphCallbackExecutionMode::Execute );

    GraphPassResult result;
    result.rendered = callbackData.rendered;
    result.callbackOwned = executed.executedPassCount == 1u;
    return result;
}


TornadoVisualSnapshot RuntimeRenderer::BuildTornadoVisualSnapshot( const RenderFrameContext& frame,
                                                                   const RuntimeRenderServices& services ) const
{
    const ReplayRenderFrameView& replayFrame = services.replayFrame;

    TornadoVisualSnapshot snapshot;
    snapshot.visual = &m_presentationSettings.tornadoVisual;
    snapshot.tornadoSystem = frame.physicsEngine ? &frame.physicsEngine->GetTornadoSystemConfig() : nullptr;
    snapshot.tornadoField = frame.physicsEngine ? &frame.physicsEngine->GetTornadoFieldConfig() : nullptr;
    snapshot.replaySample = replayFrame.presentationSample;
    snapshot.solverSample = replayFrame.solverSample;
    snapshot.predictionFrame = replayFrame.predictionFrame;
    snapshot.replayLiveAdvanceHeld = replayFrame.liveAdvanceHeld;
    snapshot.simulationSourceSeconds = services.framePolicy.simulationSeconds;
    return snapshot;
}


bool RuntimeRenderer::ExecuteReplayGhostsThroughRenderGraph(
    const RenderFrameContext& frame,
    const ReplayVisualPacket& replayVisualPacket,
    bool useCinematicTarget,
    const SkullbonezCore::Core::CinematicRenderConfig* activeCinematic,
    const Rendering::ShadowFrameData* objectShadow )
{
    Rendering::RenderGraph& graph = BeginRenderPassGraph();
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
    callbackData.profiler = m_profiler;
    callbackData.replayVisualPacket = &replayVisualPacket;
    callbackData.frame = &frame;
    callbackData.config = &m_config;
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
    CompileRenderPassGraph( graph );
    graph.ExecuteCallbacks( Rendering::RenderGraphCallbackExecutionMode::DryRun );
    const Rendering::RenderGraphCallbackExecutionResult executed =
        graph.ExecuteCallbacks( Rendering::RenderGraphCallbackExecutionMode::Execute );
    return executed.executedPassCount == 1u;
}


RuntimeRenderer::GraphPassResult
RuntimeRenderer::ExecuteDebugOverlayThroughRenderGraph( const RenderFrameContext& frame,
                                                        const RuntimeRenderServices& services,
                                                        bool useCinematicTarget )
{
    Rendering::RenderGraph& graph = BeginRenderPassGraph();
    const DebugOverlaySnapshot snapshot = BuildDebugOverlaySnapshot( frame, services );
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
    callbackData.snapshot = &snapshot;
    callbackData.runtimeTools = &services.runtimeTools;
    callbackData.replayVisualPacket = services.replayFrame.visualPacket;
    graph.SetPassCallback( debugPass,
                           ExecuteDebugOverlayGraphCallback,
                           &callbackData,
                           true,
                           "Frame/Render/DebugOverlay" );

    // Invariant: debug overlays are optional inside the pass body, but the pass
    // scheduling itself is now graph-scheduled every frame so direct runtime calls
    // cannot creep back beside post-processing callbacks.
    CompileRenderPassGraph( graph );
    graph.ExecuteCallbacks( Rendering::RenderGraphCallbackExecutionMode::DryRun );
    const Rendering::RenderGraphCallbackExecutionResult executed =
        graph.ExecuteCallbacks( Rendering::RenderGraphCallbackExecutionMode::Execute );
    GraphPassResult result;
    result.rendered = callbackData.rendered;
    result.callbackOwned = executed.executedPassCount == 1u;
    return result;
}


DebugOverlaySnapshot RuntimeRenderer::BuildDebugOverlaySnapshot( const RenderFrameContext& frame,
                                                                 const RuntimeRenderServices& services ) const
{
    const RuntimeRenderFramePolicy& policy = services.framePolicy;
    DebugOverlaySnapshot snapshot;
    snapshot.broadphaseOverlayVisible = policy.broadphaseOverlay;
    const Physics::TornadoFieldConfig* tornadoField =
        frame.physicsEngine ? &frame.physicsEngine->GetTornadoFieldConfig() : nullptr;
    const Physics::TornadoSystemConfig* tornadoSystem =
        frame.physicsEngine ? &frame.physicsEngine->GetTornadoSystemConfig() : nullptr;
    snapshot.tornadoVectorsVisible =
        tornadoField && tornadoSystem &&
        ( tornadoField->visualizeVelocityField || TornadoSystemVectorsVisible( *tornadoSystem ) );
    snapshot.tornadoOverlayWorkVisible =
        tornadoField && tornadoSystem &&
        ( tornadoField->visualizeVelocityField || tornadoSystem->visualizeVelocityField );
    snapshot.tornadoSystem = tornadoSystem;
    snapshot.tornadoField = tornadoField;
    snapshot.physicsDebugFlags = policy.physicsDebugFlags;
    snapshot.physicsDebugPipelineStageCursor = policy.physicsDebugPipelineStageCursor;
    snapshot.editorOverlayWorkVisible = services.toolOverlay.editorOverlayWorkVisible;
    return snapshot;
}


RuntimeRenderer::CinematicPostGraphResult
RuntimeRenderer::ExecuteCinematicPostThroughRenderGraph( const RenderFrameContext& frame )
{
    Rendering::RenderGraph& graph = BeginRenderPassGraph();
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
        const VolumetricLightPassResources& volumetric = m_passResources.volumetricLight;
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
    const Rendering::RenderGraphCompileResult& compiled = CompileRenderPassGraph( graph );
    Rendering::RenderGraphTransientMaterializationStats transientMaterialization;
    if ( frame.renderCommands )
    {
        transientMaterialization = frame.renderCommands->MaterializeGraphTransientResources( graph, compiled );
        if ( volumetricDeclared )
        {
            callbackData.volumetricLight = frame.renderCommands->ResolveGraphTextureBinding( volumetricLight );
            if ( !callbackData.volumetricLight.IsValid() )
            {
                // Lane R: if the graph-managed texture allocation fails, the
                // volumetric callback can still render through its legacy
                // framebuffer target. Keep the failure visible in logs/evidence.
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


bool RuntimeRenderer::ExecuteUiTextThroughRenderGraph(
    Rendering::IRenderDiagnostics& renderDiagnostics,
    const UI::UIRenderContext& uiRender,
    const UiTextPassState& state,
    RunTimerState& timers,
    UI::InGameUI& ui,
    const RuntimeRenderModelFrameView& models,
    DiagnosticsRuntime& diagnosticsRuntime,
    const ReplayHudStatus& replayHud,
    const ReplayOverlay::ReplayOverlayRenderContext& replayOverlayContext,
    const SkullbonezCore::Core::CinematicRenderConfig& cinematic,
    bool cinematicRendering,
    Rendering::IRenderRayTracing* renderRayTracing,
    double secondsPerFrame )
{
    Rendering::RenderGraph& graph = BeginRenderPassGraph();
    const Rendering::RenderGraphResourceHandle backbuffer =
        graph.AddExternalResource( "SwapchainBackbuffer", Rendering::RenderGraphResourceAccess::RenderTarget );

    const uint32_t uiTextPass = graph.AddPass( "UiTextPass",
                                               Rendering::RenderGraphQueueType::Graphics,
                                               Rendering::RenderGraphBarrierPolicy::HandoffValidated );
    graph.AddWrite( uiTextPass, backbuffer, Rendering::RenderGraphResourceAccess::RenderTarget );

    UiTextGraphCallbackData callbackData;
    callbackData.uiTextPass = &m_uiTextPass;
    callbackData.renderDiagnostics = &renderDiagnostics;
    callbackData.profiler = m_profiler;
    callbackData.textBatch = &m_textBatch;
    callbackData.uiRender = &uiRender;
    callbackData.state = &state;
    callbackData.timers = &timers;
    callbackData.ui = &ui;
    callbackData.models = &models;
    callbackData.diagnosticsRuntime = &diagnosticsRuntime;
    callbackData.replayHud = &replayHud;
    callbackData.replayOverlayContext = &replayOverlayContext;
    callbackData.cinematic = &cinematic;
    callbackData.cinematicRendering = cinematicRendering;
    callbackData.renderRayTracing = renderRayTracing;
    callbackData.secondsPerFrame = secondsPerFrame;
    graph.SetPassCallback( uiTextPass, ExecuteUiTextGraphCallback, &callbackData, true, "Frame/UI" );

    // Invariant: UI/text always lands on the presentable backbuffer. Text-only
    // mode skips world rendering before this point but still uses this callback
    // path so the late overlay pass has one scheduling owner.
    CompileRenderPassGraph( graph );
    graph.ExecuteCallbacks( Rendering::RenderGraphCallbackExecutionMode::DryRun );
    const Rendering::RenderGraphCallbackExecutionResult executed =
        graph.ExecuteCallbacks( Rendering::RenderGraphCallbackExecutionMode::Execute );
    return executed.executedPassCount == 1u;
}


RenderFrameContext
RuntimeRenderer::BuildRenderFrameContext( const RuntimeRenderInputs& renderInputs,
                                          bool cinematicRender,
                                          const SkullbonezCore::Core::CinematicRenderConfig& renderConfig )
{
    const RuntimeRenderServices& services = renderInputs.services;
    RenderFrameContext frame;
    frame.cinematicEnabled = cinematicRender;
    frame.cinematic = cinematicRender ? &renderConfig : nullptr;
    frame.renderInstances = &services.models.renderInstances;
    frame.colliders = &services.models.colliders;
    frame.bodyStore = &services.models.bodyStore;
    frame.physicsEngine = &services.models.physicsEngine;
    frame.presentationRecords = services.models.presentationRecords;
    frame.collisionVisualContacts = &services.models.collisionVisualContacts;
    frame.sleepStates = services.models.sleepStates;
    frame.sleepIslandVisualIds = services.models.sleepIslandVisualIds;
    frame.sleepSupportedStates = services.models.sleepSupportedStates;
    frame.sleepInhibitedStates = services.models.sleepInhibitedStates;
    frame.physicsDebugContacts = &services.models.physicsDebugContacts;
    frame.physicsPipelineTrace = &services.models.physicsPipelineTrace;
    frame.renderWorkerPool = services.models.renderWorkerPool;
    frame.modelCount = services.models.modelCount;
    frame.renderCollisionVolumes = services.models.renderCollisionVolumes;
    frame.shadowParallelPrep = services.models.shadowParallelPrep;
    frame.sceneKineticEnergy = services.models.sceneKineticEnergy;
    frame.tornadoElapsedSeconds = services.models.tornadoElapsedSeconds;
    frame.assets = &services.assets;
    frame.textures = &services.textures;
    frame.renderResources = &services.renderResources;
    frame.renderCommands = &services.renderCommands;
    frame.renderDiagnostics = &services.renderDiagnostics;
    frame.primitiveBatches = &PrimitiveBatches();
    frame.renderRayTracing = services.renderRayTracing;
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
                                  (std::max)( 1, m_window.ClientWidth() ),
                                  (std::max)( 1, m_window.ClientHeight() ) };
}


RuntimeRenderer::RuntimeRenderer( RuntimeRenderBackendView backend, const RenderWorldView& world, RunSceneState& scene )
    : m_lifecycleLog( backend.deviceLifecycle, scene ), m_assets( world.assets ), m_cameras( world.cameras ),
      m_terrain( world.terrain ), m_window( world.window ), m_config( world.config ), m_world( world.worldEnvironment ),
      m_primitiveBatches( std::in_place, backend.renderResources ),
      m_collisionVisualizer( world.overlayResources.m_collisionOverlay ),
      m_broadphaseVisualizer( world.overlayResources.m_broadphaseOverlay ),
      m_physicsDebugVisualizer( world.overlayResources.m_physicsDebugOverlay ), m_profiler( world.profiler ),
      m_fullscreenQuadPass( m_passResources.fullscreen ),
      m_skyPass( m_passResources.sky, m_passResources.fullscreen, m_skyBox, m_config, m_profiler ),
      m_sceneTargetPass( m_passResources.cinematicScene, m_profiler ),
      m_shadowPass( m_passResources.shadows, m_terrain, m_config, m_lifecycleLog, m_profiler ),
      m_reflectionPass( m_passResources.reflection,
                        m_collisionVisualizer,
                        m_config,
                        m_dxrReflectionTransforms.data(),
                        static_cast<int>( m_dxrReflectionTransforms.size() / 16 ),
                        m_lifecycleLog,
                        m_profiler ),
      m_objectPass( m_collisionVisualizer, m_config, m_profiler ), m_terrainPass( m_terrain, m_config, m_profiler ),
      m_waterPass( m_world, m_config, m_profiler ), m_tornadoVisualPass( m_terrain, m_profiler ),
      m_debugOverlayPass( m_broadphaseVisualizer, m_physicsDebugVisualizer, m_terrain, m_assets, m_profiler ),
      m_volumetricPass( m_passResources.cinematicScene,
                        m_passResources.volumetricLight,
                        m_passResources.fullscreen,
                        m_config,
                        m_profiler ),
      m_tonemapPass( m_passResources.cinematicScene,
                     m_passResources.volumetricLight,
                     m_passResources.tonemap,
                     m_passResources.fullscreen,
                     m_config,
                     m_profiler ),
      m_uiTextPass()
{
    m_renderPassGraphScratch.ReserveForRuntimePassGraph();
    m_renderPassCompileScratch.ReserveForRuntimePassGraph();
}


RuntimeRenderer::~RuntimeRenderer() = default;


void RuntimeRenderer::ResetSceneRuntimePolicyFromConfig()
{
    SetVsyncEnabled( m_config.runtimeRender.vsyncEnabled );
    SetPipelineSyncEnabled( m_config.runtimeRender.forcePipelineSync );
}


SkullbonezCore::Core::SbResult RuntimeRenderer::InitialiseSceneRayTracing( const RuntimeRenderBackendView& backend,
                                                                           int modelCapacity )
{
    Rendering::IRenderRayTracing* rayTracing = backend.rayTracingBackend;
    Rendering::IRenderDiagnostics* renderDiagnostics = backend.renderDiagnostics;
    const bool supported =
        renderDiagnostics && renderDiagnostics->GetCapabilities().supportsDxrReflection && rayTracing;
    if ( !supported )
    {
        return SkullbonezCore::Core::SbResult::Success();
    }

    Rendering::PrimitiveMeshGeometryView sphereGeometry = PrimitiveBatches().SphereGeometry();
    if ( sphereGeometry.instancedMeshHandle == 0 )
    {
        if ( !backend.renderResources || !backend.renderCommands )
        {
            // Lane F: capability publication without the resource facets needed
            // to build the renderer-owned primitive mesh is invalid backend wiring.
            SB_FATAL( "RuntimeRenderer",
                      "DXR reflection initialization requires resource and command facets. resources=%d commands=%d",
                      backend.renderResources ? 1 : 0,
                      backend.renderCommands ? 1 : 0 );
        }
        const PrimitiveRenderContext primitiveContext{ *backend.renderResources,
                                                       *backend.renderCommands,
                                                       *renderDiagnostics,
                                                       m_assets,
                                                       m_config,
                                                       PrimitiveBatches() };
        PrimitiveBatches().EnsureSphereMesh( primitiveContext );
        sphereGeometry = PrimitiveBatches().SphereGeometry();
    }

    if ( !m_terrain.Get() || !m_terrain.Get()->GetMesh() )
    {
        return SkullbonezCore::Core::SbResult::Success();
    }

    Rendering::IMesh* terrainMesh = m_terrain.Get()->GetMesh();
    const uint64_t terrainVBVA = terrainMesh->GetVertexBufferGPUVA();
    const uint32_t sphereHandle = sphereGeometry.instancedMeshHandle;
    const uint64_t sphereVBVA = rayTracing->GetInstancedMeshStaticVBVA( sphereHandle );
    if ( terrainVBVA == 0 || sphereVBVA == 0 )
    {
        return SkullbonezCore::Core::SbResult::Success();
    }

    // Lane R: device resource creation and shader bytecode failures remain a
    // recoverable renderer result reported through the scene-load transaction.
    return rayTracing->InitDXR( terrainVBVA,
                                terrainMesh->GetVertexCount(),
                                terrainMesh->GetStride(),
                                sphereVBVA,
                                sphereGeometry.vertexCount,
                                rayTracing->GetInstancedMeshStaticStride( sphereHandle ),
                                modelCapacity );
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


const TornadoVisualSettings& RuntimeRenderer::TornadoVisualSettingsSnapshot() const
{
    return m_presentationSettings.tornadoVisual;
}


void RuntimeRenderer::SetTornadoVisualSettings( const TornadoVisualSettings& settings )
{
    m_presentationSettings.tornadoVisual = settings;
}


bool RuntimeRenderer::TornadoVisualAutoEnableWithTornado() const
{
    return m_presentationSettings.tornadoVisual.autoEnableWithTornado;
}


void RuntimeRenderer::SetTornadoVisualEnabled( bool enabled )
{
    m_presentationSettings.tornadoVisual.enabled = enabled;
}


RuntimeRenderTargetPreviewSnapshot RuntimeRenderer::BuildRenderTargetPreviewSnapshot( bool shadowsAvailable,
                                                                                      bool cinematicTargetsAvailable,
                                                                                      bool volumetricAvailable ) const
{
    RuntimeRenderTargetPreviewSnapshot snapshot;
    const auto append = [&]( const char* label, const Rendering::IFramebuffer* target, bool depth, bool available )
    {
        assert( snapshot.count < static_cast<int>( snapshot.targets.size() ) );
        RuntimeRenderTargetPreview& preview = snapshot.targets[static_cast<size_t>( snapshot.count++ )];
        preview.label = label;
        preview.textureHandle =
            target ? ( depth ? target->GetDepthTextureHandle() : target->GetColorTextureHandle() ) : 0;
        preview.width = target ? target->GetWidth() : 0;
        preview.height = target ? target->GetHeight() : 0;
        preview.available = available && preview.textureHandle != 0 && preview.width > 0 && preview.height > 0;
        preview.depth = depth;
        preview.hdr = target && !depth && target->GetColorFormat() == Rendering::FramebufferColorFormat::RGBA16F;
    };

    append( "Reflection Color", m_passResources.reflection.target.get(), false, true );
    append( "Reflection Depth", m_passResources.reflection.target.get(), true, true );
    append( "Terrain Shadow Depth", m_passResources.shadows.terrainTarget.get(), true, shadowsAvailable );
    append( "Object Shadow Depth", m_passResources.shadows.objectTarget.get(), true, shadowsAvailable );
    append( "Terrain Shadow Color", m_passResources.shadows.terrainTarget.get(), false, shadowsAvailable );
    append( "Object Shadow Color", m_passResources.shadows.objectTarget.get(), false, shadowsAvailable );
    append( "Cinematic Scene Color", m_passResources.cinematicScene.hdrTarget.get(), false, cinematicTargetsAvailable );
    append( "Cinematic Scene Depth", m_passResources.cinematicScene.hdrTarget.get(), true, cinematicTargetsAvailable );
    append( "Volumetric Color", m_passResources.volumetricLight.target.get(), false, volumetricAvailable );
    append( "Volumetric Depth", m_passResources.volumetricLight.target.get(), true, volumetricAvailable );
    return snapshot;
}


SkullbonezCore::Rendering::RenderGraph& RuntimeRenderer::BeginRenderPassGraph()
{
    m_renderPassGraphScratch.Clear();
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


bool RuntimeRenderer::RenderFrame( const RuntimeRenderInputs& renderInputs )
{
    const RuntimeRenderServices& services = renderInputs.services;
    const RuntimeRenderFramePolicy& policy = services.framePolicy;
    const ReplayRenderFrameView& replayFrame = services.replayFrame;
    m_uiTextRayTracing = services.renderRayTracing;
    const bool cinematicRender = services.cinematicEnabled;
    const SkullbonezCore::Core::CinematicRenderConfig& renderConfig = services.cinematic;
    const SkullbonezCore::Core::OrdinaryRenderConfig& ordinaryRender = m_config.ordinaryRender;
    SkullbonezCore::Core::CinematicRenderConfig ordinaryShadowConfig = renderConfig;
    ordinaryShadowConfig.shadow = ordinaryRender.shadow;
    const SkullbonezCore::Core::CinematicRenderConfig& activeShadowStyle =
        cinematicRender ? renderConfig : ordinaryShadowConfig;
    const bool shadowMapsEnabled = activeShadowStyle.shadow.enabled && services.renderReady && !policy.textOnly;

    const RenderResourceContext resourceContext = BuildRenderResourceContext( renderInputs, cinematicRender );
    {
        RuntimeAllocation::RuntimeAllocationScope allocationScope(
            RuntimeAllocation::RuntimeAllocationPhase::BackendInit );
        EnsureFrameResources( resourceContext );
    }

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
    const TornadoVisualSnapshot tornadoVisual = BuildTornadoVisualSnapshot( frame, services );

    // These passes currently borrow subsystem-owned mesh/material resources,
    // but keeping the ensure calls in the frame story gives future extraction
    // work an obvious place to move those GPU resources.
    {
        RuntimeAllocation::RuntimeAllocationScope allocationScope(
            RuntimeAllocation::RuntimeAllocationPhase::BackendInit );
        m_objectPass.EnsureGpuResources( resourceContext );
        m_terrainPass.EnsureGpuResources( resourceContext );
        m_waterPass.EnsureGpuResources( resourceContext );
        m_tornadoVisualPass.EnsureGpuResources( resourceContext, tornadoVisual );
        m_debugOverlayPass.EnsureGpuResources( resourceContext );
    }

    // Defer the first DX12 command-list open until after CPU-side model prep so
    // allocator waits do not block work that can overlap the previous frame.
    if ( !cinematicRender )
    {
        services.renderCommands.Clear( true, true );
    }

    const SkullbonezCore::Core::CinematicRenderConfig* activeCinematic = frame.cinematic;
    const SkullbonezCore::Core::CinematicRenderConfig* activeShadowConfig =
        shadowMapsEnabled ? &activeShadowStyle : nullptr;
    if ( activeShadowConfig )
    {
        RuntimeAllocation::RuntimeAllocationScope allocationScope(
            RuntimeAllocation::RuntimeAllocationPhase::BackendInit );
        PrimitiveRenderContext primitiveContext{ services.renderResources,
                                                 services.renderCommands,
                                                 services.renderDiagnostics,
                                                 services.assets,
                                                 m_config,
                                                 PrimitiveBatches() };
        PrimitiveBatches().EnsureShadowDepthPrimitiveResources( primitiveContext );
        if ( services.terrain )
        {
            services.terrain->EnsureShadowDepthResources();
        }
        m_shadowPass.EnsureGpuResources( resourceContext, *activeShadowConfig );
    }
    const ShadowGraphResult shadowGraph =
        ExecuteShadowThroughRenderGraph( frame, activeShadowConfig, policy.terrainHidden, policy.collisionVisualizer );
    ShadowPassOutput shadowPass = shadowGraph.output;
    const bool shadowCallbackOwned = shadowGraph.callbackOwned;
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
    bool skyboxCallbackOwned = false;
    if ( !cinematicRender )
    {
        PROFILE_GPU_BEGIN( m_profiler, "Frame/Render/Skybox" );
        {
            DRAW_CALL_TRACE_SCOPE( services.renderDiagnostics, "Frame/Render/Skybox" );
            skyboxCallbackOwned = ExecuteSkyboxThroughRenderGraph( frame );
        }
        PROFILE_GPU_END( m_profiler, "Frame/Render/Skybox" );
    }

    ReflectionPassOutput reflection;
    reflection.reflectionSampleViewProjection = frame.reflectionViewProjection;
    bool reflectionCallbackOwned = false;
    if ( reflectionPassNeeded )
    {
        {
            RuntimeAllocation::RuntimeAllocationScope allocationScope(
                RuntimeAllocation::RuntimeAllocationPhase::BackendInit );
            m_reflectionPass.EnsureGpuResources( resourceContext );
        }
        const ReflectionGraphResult reflectionGraph =
            ExecuteReflectionThroughRenderGraph( frame,
                                                 activeCinematic,
                                                 objectShadowFrame,
                                                 collisionStateColorsVisible,
                                                 debugTransparentBodyPass,
                                                 collisionVisualizerAlphaOverride,
                                                 bodyRenderAlpha,
                                                 policy.waterRTReflect,
                                                 policy.waterNoReflect,
                                                 static_cast<float>( policy.totalSimulationSeconds ) );
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
    const bool terrainCallbackOwned = ExecuteTerrainThroughRenderGraph( frame,
                                                                        useCinematicTarget,
                                                                        activeCinematic,
                                                                        terrainShadowFrame,
                                                                        terrainDetailShadowFrame,
                                                                        policy.terrainHidden );

    // Water is deliberately downstream of ReflectionPass; it samples the
    // reflection texture but never rebuilds it.
    const bool waterCallbackOwned = ExecuteWaterThroughRenderGraph( frame,
                                                                    reflection,
                                                                    useCinematicTarget,
                                                                    activeCinematic,
                                                                    policy.waterHidden,
                                                                    policy.waterFlatDebug,
                                                                    policy.waterNoReflect,
                                                                    policy.waterFreezeDebug,
                                                                    policy.frozenWaterTime,
                                                                    static_cast<float>( policy.simulationSeconds ) );

    const GraphPassResult tornadoVisualGraph =
        ExecuteTornadoVisualThroughRenderGraph( frame, useCinematicTarget, tornadoVisual );

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

    bool replayGhostCallbackOwned = false;
    {
        RuntimeAllocation::RuntimeAllocationScope replayAllocationScope(
            RuntimeAllocation::RuntimeAllocationPhase::Replay );
        replayGhostCallbackOwned = ExecuteReplayGhostsThroughRenderGraph( frame,
                                                                          *replayFrame.visualPacket,
                                                                          useCinematicTarget,
                                                                          activeCinematic,
                                                                          objectShadowFrame );
    }

    const GraphPassResult debugOverlayGraph =
        ExecuteDebugOverlayThroughRenderGraph( frame, services, useCinematicTarget );

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
    frameSnapshot.terrainPassRendered = !policy.terrainHidden;
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
    frameSnapshot.debugOverlayCallbackOwned = debugOverlayGraph.callbackOwned;
    frameSnapshot.volumetricCallbackOwned = volumetricCallbackOwned;
    frameSnapshot.volumetricReady = volumetricReady;
    frameSnapshot.tonemapCallbackOwned = tonemapCallbackOwned;
    if ( volumetricReady )
    {
        frameSnapshot.volumetricTextureHandle = cinematicPostGraph.volumetricTextureHandle;
        frameSnapshot.volumetricWidth = cinematicPostGraph.volumetricWidth;
        frameSnapshot.volumetricHeight = cinematicPostGraph.volumetricHeight;
    }
    {
        RuntimeAllocation::RuntimeAllocationScope diagnosticsAllocationScope(
            RuntimeAllocation::RuntimeAllocationPhase::Diagnostics );
        Rendering::RenderPipeline::DumpExecutedFrameGraphIfChanged( frameSnapshot );
    }
    return debugOverlayGraph.rendered;
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
    m_uiTextPass.ReleaseGpuResources( m_textBatch, renderResources );
    m_uiTextRayTracing = nullptr;
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

    const auto logLifecycleStep = [&]( const char* step ) { m_lifecycleLog.Write( context.phaseName, step ); };

    if ( context.deviceLifecycle )
    {
        logLifecycleStep( "flush_before_resource_release" );
        const SkullbonezCore::Core::SbResult flushResult = context.deviceLifecycle->DrainForResourceRelease();
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
            m_primitiveBatches.reset();
            break;
        case BackendResourceStep::CollisionVisualizer:
            m_collisionVisualizer.ResetResources( context.renderResources );
            break;
        case BackendResourceStep::UIResources:
            context.ui.ResetResources( context.renderResources );
            break;
        case BackendResourceStep::RenderPassResources:
            ReleaseBackendOwnedResources( context.renderResources );
            break;
        case BackendResourceStep::ProfilerQueries:
#if defined( SKULLBONEZ_PROFILE_ENABLED )
            RuntimeDiagnostics::InvalidateProfilerGpuQueries( m_profiler );
#endif
            break;
        case BackendResourceStep::TextureCollection:
            m_textures.DeleteAllTextures();
            m_textures.BindAssetSystem( nullptr );
            m_textures.BindRenderContexts( nullptr, nullptr );
            break;
        case BackendResourceStep::CameraCollection:
            m_cameras.Reset();
            m_cameras.SetTerrain( nullptr );
            break;
        case BackendResourceStep::SkyBox:
            if ( m_skyBox )
            {
                m_skyBox->ReleaseRenderResources();
                m_skyBox.reset();
            }
            break;
        case BackendResourceStep::LauncherLaser:
            context.tools.Laser().ResetResources( context.renderResources );
            break;
        }
    }
    return SkullbonezCore::Core::SbResult::Success();
}


SkullbonezCore::Core::SbResult
RuntimeRenderer::InitialiseProcessResources( Rendering::IRenderResourceFactory& renderResources,
                                             Rendering::IRenderCommandContext& renderCommands,
                                             const SkullbonezCore::Core::EngineConfig& config,
                                             bool dumpTextureAssets )
{
    m_textures.BindAssetSystem( &m_assets );
    m_textures.BindRenderContexts( &renderResources, &renderCommands );
    enum class RebuildStep
    {
        RecreateHelperOwner,
        RegisterBuiltInSources,
        RebuildTextures
    };

    struct RebuildPhase
    {
        const char* name;
        RebuildStep step;
    };

    const RebuildPhase rebuildSteps[] = {
        { "recreate_helper_owner", RebuildStep::RecreateHelperOwner },
        { "register_builtin_source_records", RebuildStep::RegisterBuiltInSources },
        { "rebuild_textures_from_source_assets", RebuildStep::RebuildTextures },
    };

    for ( const RebuildPhase& phase : rebuildSteps )
    {
        m_lifecycleLog.Write( "backend_rebuild", phase.name );

        switch ( phase.step )
        {
        case RebuildStep::RecreateHelperOwner:
            m_primitiveBatches.emplace( &renderResources );
            break;
        case RebuildStep::RegisterBuiltInSources:
            m_assets.RegisterBuiltInSourceAssets( config );
            break;
        case RebuildStep::RebuildTextures:
            // Recreate backend texture handles from stable source asset records.
            {
                const SkullbonezCore::Core::SbResult textureResult = m_textures.RebuildTexturesFromSourceAssets();
                if ( !textureResult.ok )
                {
                    return textureResult;
                }
            }
            break;
        }
    }
    if ( dumpTextureAssets )
    {
        m_textures.DumpTextureAssets( stdout );
    }

    m_skyBox = std::make_unique<Geometry::SkyBox>( -250, 300, -300, 300, -250, 300 );
    m_skyBox->BindTextures( m_textures );
    m_skyBox->BindRenderContexts( config, m_assets, renderResources );
    const SkullbonezCore::Core::SbResult skyBoxResult = m_skyBox->ResetRenderResources();
    if ( !skyBoxResult.ok )
    {
        return skyBoxResult;
    }
    return SkullbonezCore::Core::SbResult::Success();
}


SkullbonezCore::Core::SbResult
RuntimeRenderer::EnsureUiTextResources( Rendering::IRenderResourceFactory& renderResources,
                                        const Assets::AssetSystem& assets,
                                        int screenW,
                                        int screenH )
{
    RuntimeAllocation::RuntimeAllocationScope allocationScope( RuntimeAllocation::RuntimeAllocationPhase::BackendInit );
    return m_uiTextPass.EnsureGpuResources( m_textBatch, renderResources, assets, screenW, screenH );
}


bool RuntimeRenderer::ShouldRenderUiText( const UiTextPassState& state, const UI::InGameUI& ui ) const
{
    return m_uiTextPass.ShouldRender( state, ui );
}


void RuntimeRenderer::SetUiTextRayTracingCapability( Rendering::IRenderRayTracing* renderRayTracing )
{
    m_uiTextRayTracing = renderRayTracing;
}


void RuntimeRenderer::RenderUiText( Rendering::IRenderDiagnostics& renderDiagnostics,
                                    const UI::UIRenderContext& uiRender,
                                    const UiTextPassState& state,
                                    RunTimerState& timers,
                                    UI::InGameUI& ui,
                                    const RuntimeRenderModelFrameView& models,
                                    DiagnosticsRuntime& diagnosticsRuntime,
                                    const ReplayHudStatus& replayHud,
                                    const ReplayOverlay::ReplayOverlayRenderContext& replayOverlayContext,
                                    const SkullbonezCore::Core::CinematicRenderConfig& cinematic,
                                    bool cinematicRendering,
                                    double dSecondsPerFrame )
{
    (void)ExecuteUiTextThroughRenderGraph( renderDiagnostics,
                                           uiRender,
                                           state,
                                           timers,
                                           ui,
                                           models,
                                           diagnosticsRuntime,
                                           replayHud,
                                           replayOverlayContext,
                                           cinematic,
                                           cinematicRendering,
                                           m_uiTextRayTracing,
                                           dSecondsPerFrame );
}


RuntimeRenderModelFrameView
RuntimeRenderer::BuildModelFrameView( SkullbonezCore::Runtime::SceneWorld& scene,
                                      Threading::WorkerPool& workerPool,
                                      const SkullbonezCore::Core::EngineConfig& config ) const
{
    PhysicsEngine& physics = scene.Physics();
    return RuntimeRenderModelFrameView{
        scene.MutableRenderInstances(),
        SkullbonezCore::Physics::PhysicsEngine::ReadColliders( physics ),
        SkullbonezCore::Physics::PhysicsEngine::ReadBodies( physics ),
        physics,
        scene.RenderPresentationRecords(),
        SkullbonezCore::Physics::PhysicsEngine::ReadCollisionVisualContacts( physics ),
        SkullbonezCore::Physics::PhysicsEngine::ReadSleepStates( physics ),
        SkullbonezCore::Physics::PhysicsEngine::ReadSleepIslandVisualIds( physics ),
        SkullbonezCore::Physics::PhysicsEngine::ReadSleepSupportedStates( physics ),
        SkullbonezCore::Physics::PhysicsEngine::ReadSleepInhibitedStates( physics ),
        SkullbonezCore::Physics::PhysicsEngine::ReadDebugContacts( physics ),
        SkullbonezCore::Physics::PhysicsEngine::ReadPipelineTrace( physics ),
        &workerPool,
        scene.SceneEntityCount(),
        config.runtimeRender.renderCollisionVolumes,
        config.runtimeRender.shadowParallelPrep,
        scene.GetSceneKineticEnergy(),
        physics.GetTornadoSystemElapsedSeconds(),
        CollectSceneMemoryStats( SceneMemoryDiagnosticsView{ scene.Entities(), physics, scene.RenderInstances() } ) };
}


bool RuntimeRenderer::RenderFrameEntry( const FrameEntryContext& context )
{
    m_uiTextRayTracing = nullptr;
    const ReplayRenderFrameView& replayFrame = context.replayOverlay.replayFrame;
    RuntimeTools& runtimeTools = context.toolOverlay.tools;
    const RuntimeRenderFramePolicy& policy = context.framePolicy;

    if ( policy.textOnly )
    {
        return false;
    }

    SkullbonezCore::Rendering::IRenderCommandContext* renderCommands = context.backend.renderCommands;
    SkullbonezCore::Rendering::IRenderResourceFactory* renderResources = context.backend.renderResources;
    SkullbonezCore::Rendering::IRenderDiagnostics* renderDiagnostics = context.backend.renderDiagnostics;
    const bool renderReady = renderCommands != nullptr && renderResources != nullptr && renderDiagnostics != nullptr;
    if ( !renderReady )
    {
        return false;
    }
    SkullbonezCore::Rendering::IRenderRayTracing* renderRayTracing = context.backend.rayTracingBackend;

    const auto gradeNow = std::chrono::steady_clock::now();
    float gradeDtSeconds = 0.0f;
    if ( m_consequenceGradeLastTick.time_since_epoch().count() != 0 )
    {
        gradeDtSeconds =
            std::clamp( std::chrono::duration<float>( gradeNow - m_consequenceGradeLastTick ).count(), 0.0f, 0.10f );
    }
    m_consequenceGradeLastTick = gradeNow;
    const float consequenceGradeTarget = context.consequenceGradeRequested ? 1.0f : 0.0f;
    m_consequenceGradeStrength =
        ApproachFloat( m_consequenceGradeStrength, consequenceGradeTarget, gradeDtSeconds, 1.0f );

    SkullbonezCore::Core::CinematicRenderConfig frameCinematic = context.cinematic;
    ApplyConsequenceGrade( frameCinematic, m_consequenceGradeStrength );

    const bool cinematicRender =
        ( context.cinematicRequested || m_consequenceGradeStrength > 0.01f ) && renderReady && !policy.textOnly;
    // Why: this call constructs the one-frame render record directly. Field
    // labels keep every live borrow visible and prevent positional drift when
    // RuntimeRenderServices changes; RenderFrame does not retain the record.
    return RenderFrame( RuntimeRenderInputs{ .services = RuntimeRenderServices{ .assets = m_assets,
                                                                                .textures = m_textures,
                                                                                .models = context.renderModels,
                                                                                .world = m_world,
                                                                                .terrain = m_terrain.Get(),
                                                                                .cameras = m_cameras,
                                                                                .window = m_window,
                                                                                .ui = context.ui,
                                                                                .runtimeTools = runtimeTools,
                                                                                .replayFrame = replayFrame,
                                                                                .toolOverlay = context.toolOverlay,
                                                                                .framePolicy = policy,
                                                                                .skyBox = m_skyBox.get(),
                                                                                .cinematic = frameCinematic,
                                                                                .cinematicEnabled = cinematicRender,
                                                                                .renderCommands = *renderCommands,
                                                                                .renderResources = *renderResources,
                                                                                .renderDiagnostics = *renderDiagnostics,
                                                                                .renderRayTracing = renderRayTracing,
                                                                                .renderReady = renderReady } } );
}
