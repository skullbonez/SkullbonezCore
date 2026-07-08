/*
File: SkullbonezSource/Runtime/RunFrame.cpp
Purpose:
  Runs one frame of input, simulation, rendering, profiling, and presentation.

Mental model:
  Runtime code connects authored scene data, input, simulation, render
  backends, and validation-oriented launch modes. Follow who owns state and
  when that state changes.

Glossary:
  Simulation tick: One runtime decision about whether to advance logic, camera,
    and zero or more fixed physics steps this frame.
  Contact-audio flash mode: Render-only diagnostic selector that decides which
    completed audio decisions paint body flashes after a fixed physics step.
  Contact-audio simple mode: Presentation-only path that emits from body linear
    velocity changes rather than solver contact rows.
  Fixed-step edge: Runtime-owned code that repairs model/body topology before
    PhysicsEngine::Step and applies presentation-only refresh work after it.
  PhysicsBodyStore: Physics-owned body rows for live pose, velocity, fixed
    state, and replay identity.
  ColliderStore: Physics-owned collider rows for exact shape variants, material
    parameters, and broadphase radius.
  Lane R result: Recoverable scene-control failure that stops a reload action
    from being reported as a successful frame transition.
  Replay event payload: Saved event data that must be decoded exactly so replay
    restore and validation compare the same floating-point bits.
  Validation gate: Repository script that proves a class of changes before
  commit or PR.

Invariants:
  - Replay event payload bits are decoded without numeric conversion so saved
    timelines reproduce exact float values.
  - Frame work updates input, simulation, capture, rendering, and diagnostics
    in a stable order used by validation and replay comparisons.

Related:
  - Agentic/Reference/runtime-reference.md
  - Agentic/Reference/comment-style-guide.md
*/
#include "RunInternal.h"
#include "RunDemoDirector.h"
#include "Scene/SceneRuntimeLoad.h"

#include "CaptureSystem.h"
#include "Editor/EditorTools.h"
#include "Replay/ReplayV2Artifact.h"
#include "Allocation/RuntimeAllocationTracker.h"
#include "RuntimeTuning.h"
#include "Scene/SceneRuntimeStyle.h"

#include "../Core/Log.h"
#include "../Physics/ColliderStore.h"
#include "../Physics/PhysicsApi.h"
#include "../Physics/PhysicsDiagnosticsSink.h"
#include "../Physics/PhysicsTimestep.h"
#include "../Rendering/RenderInstanceStore.h"

#include <cmath>
#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <limits>
#include <stdexcept>
#include <utility>
#include <vector>

using namespace SkullbonezCore::Basics;
using namespace SkullbonezCore::Math::CollisionDetection;
using namespace SkullbonezCore::Math::Orientation;
using namespace SkullbonezCore::Math::Transformation;
using namespace SkullbonezCore::Physics;
using namespace SkullbonezCore::Basics::RunInternal;
using SkullbonezCore::Math::Vector::Vector3;
namespace RuntimeAllocation = SkullbonezCore::Runtime::Allocation;

namespace
{
// Why: Profile builds do not emit Debug-only scene-finished telemetry, so
// automation exits need an explicit stdout breadcrumb near the quit request.
void PrintRuntimeExitReason( const char* reason )
{
    printf( "[runtime-exit] %s\n", reason );
    fflush( stdout );
}

#ifdef _DEBUG
constexpr const char* REPLAY_PROBE_OWNER = "ReplayProbe";

SbResult ReplayProbeFailure( const char* message )
{
    return SbResult::Failure( REPLAY_PROBE_OWNER, "%s", message );
}

void WriteRuntimePhysicsDiagnosticsCsv( void*, const char* fileName, const char* fmt, va_list args )
{
    // Why: Physics receives only a value writer; the runtime frame edge owns the
    // decision to route that writer to the process debug log singleton.
    Log().WriteVf( fileName, fmt, args );
}
#endif

bool ShouldFlashContactAudioDecision( ContactAudioFlashMode mode,
                                      const SkullbonezCore::Runtime::Audio::ContactAudioDecision& decision )
{
    switch ( mode )
    {
    case ContactAudioFlashMode::Off:
        return false;
    case ContactAudioFlashMode::Emitted:
        return decision.submitted && decision.flashEligible;
    case ContactAudioFlashMode::Candidates:
        return true;
    case ContactAudioFlashMode::Rejected:
        return !decision.submitted;
    default:
        return decision.submitted && decision.flashEligible;
    }
}


struct ExecuteUiTextFrameContext
{
    RuntimeRenderer& renderer;
    SkullbonezCore::Rendering::IRenderDiagnostics& renderDiagnostics;
    const SkullbonezCore::UI::UIRenderContext& uiRender;
    const RuntimeRenderModelFrameView& renderModels;
    DiagnosticsRuntime& diagnosticsRuntime;
    ReplayRuntime& replayRuntime;
    RunTimerState& timers;
    RunDebugState& debug;
    RunSceneState& scene;
    RunRuntimeSettings& runtimeSettings;
    EngineConfig& config;
    SkullbonezCore::Environment::WorldEnvironment& worldEnvironment;
    RuntimeTools& runtimeTools;
    SkullbonezCore::UI::InGameUI& ui;
    RuntimeInputContext& runtimeInput;
    RunCameraState& camera;
    RuntimeViewModel& runtimeViewModel;
    SceneController& sceneController;
    RunSubsystemState& systems;
    RunLaunchOptions& launchOptions;
    uint32_t cameraModeEnabledMask = 0;
    const char* cameraModeLabel = nullptr;
    const char* launcherFireModeLabel = nullptr;
    bool isLauncherCameraMode = false;
    double secondsPerFrame = 0.0;
};


template <typename RefreshRuntimeViewModel>
void RenderExecuteUiTextFrame( ExecuteUiTextFrameContext& context, RefreshRuntimeViewModel refreshRuntimeViewModel )
{
    // Concept: frame-loop UI text is a late render pass. Keep the state package
    // rebuilt here and refresh the scalar view only after the renderer says the
    // pass will execute, matching the previous Run::Execute ordering.
    const RunSceneBrowserState& uiSceneBrowser = context.sceneController.Browser();
    const std::string* uiScenePath = context.sceneController.CurrentPath();
    const UiTextPassState uiTextState{
        context.debug,
        context.timers,
        context.scene,
        context.runtimeSettings,
        context.config,
        context.worldEnvironment,
        context.runtimeTools.RayCastTest(),
        context.runtimeTools.Editor(),
        context.ui,
        context.runtimeInput,
        context.camera,
        context.runtimeViewModel,
        uiSceneBrowser,
        context.systems.renderPasses,
        context.systems.workerPool,
        RuntimeWindowScreenWidth( context.systems, context.config ),
        RuntimeWindowScreenHeight( context.systems, context.config ),
        context.sceneController.QueueSize(),
        context.sceneController.HasCurrentEntry(),
        uiScenePath ? uiScenePath->c_str() : nullptr,
        CurrentSceneBrowserIndex( context.sceneController, uiSceneBrowser ),
        context.cameraModeEnabledMask,
        context.cameraModeLabel,
        context.launcherFireModeLabel,
        context.isLauncherCameraMode,
        context.replayRuntime.ShouldRenderScrubber( context.runtimeTools.Editor().editorModeEnabled,
                                                    context.ui.IsVisible(),
                                                    context.ui.IsMinimized() ),
        context.replayRuntime.HasPathVisualizerTarget() };

    if ( context.renderer.ShouldRenderUiText( uiTextState ) )
    {
        refreshRuntimeViewModel();
        const CinematicRenderConfig& uiCinematic = RuntimeActiveCinematicConfig( context.scene, context.config );
        const bool uiCinematicRendering = RuntimeCinematicRenderingEnabled( context.scene,
                                                                            context.config,
                                                                            context.launchOptions,
                                                                            context.debug,
                                                                            true );
        const ReplayOverlayFrameState replayOverlay{
            context.runtimeTools.Editor().editorModeEnabled,
            context.ui.IsVisible(),
            context.ui.IsMinimized(),
            context.scene.isScenePhysics,
            RuntimeWindowScreenWidth( context.systems, context.config ),
            RuntimeWindowScreenHeight( context.systems, context.config ),
            context.timers.simulationTimer.GetTotalTime(),
        };
        const int uiDrawCallStart = context.renderDiagnostics.GetFrameDrawCallCount();
        PROFILE_BEGIN( "Frame/UI" );
        {
            RuntimeAllocation::RuntimeAllocationScope allocationScope(
                RuntimeAllocation::RuntimeAllocationPhase::Render );
            DRAW_CALL_TRACE_SCOPE( context.renderDiagnostics, "Frame/UI" );
            context.renderer.RenderUiText( context.renderDiagnostics,
                                           context.uiRender,
                                           uiTextState,
                                           context.renderModels,
                                           context.diagnosticsRuntime,
                                           context.replayRuntime,
                                           replayOverlay,
                                           uiCinematic,
                                           uiCinematicRendering,
                                           context.secondsPerFrame );
        }
        PROFILE_END( "Frame/UI" );
        const int uiDrawCallEnd = context.renderDiagnostics.GetFrameDrawCallCount();
        context.timers.lastUIDrawCalls = (std::max)( 0, uiDrawCallEnd - uiDrawCallStart );
    }
    else
    {
        context.timers.lastUIDrawCalls = 0;
    }
}


struct ExecutePostPhysicsVisualizationContext
{
    RunDebugState& debug;
    SkullbonezCore::GameObjects::GameModelCollection& models;
    BroadphaseVisualizer& broadphaseVisualizer;
    CollisionVisualizer& collisionVisualizer;
    PhysicsDebugVisualizer& physicsDebugVisualizer;
};


template <typename UpdateRequiredBroadphaseXCells, typename UpdateRequiredContacts>
void TickExecutePostPhysicsVisualizers( ExecutePostPhysicsVisualizationContext& context,
                                        double secondsPerFrame,
                                        UpdateRequiredBroadphaseXCells updateRequiredBroadphaseXCells,
                                        UpdateRequiredContacts updateRequiredContacts )
{
    PROFILE_BEGIN( "Frame/PostPhysics" );

    PROFILE_BEGIN( "Frame/PostPhysics/BroadphaseVisualizer" );
    // Why: broadphase visualizer state runs even when the overlay is hidden so
    // cell fades and scene-gate checks stay coherent across toggles.
    {
        context.broadphaseVisualizer.SetEnabled( context.debug.isBroadphaseOverlay );
        context.broadphaseVisualizer.SetCellSize( context.models.GetSpatialGrid().GetCellSize() );
        const SpatialGrid& grid = context.models.GetSpatialGrid();
        SpatialGrid::ActiveCell activeCellBuf[SpatialGrid::MAX_BUCKETS];
        int activeCellCount = grid.GetActiveCellCount();
        grid.GetActiveCells( activeCellBuf, SpatialGrid::MAX_BUCKETS );
        const std::vector<int64_t>& collisionKeys = context.models.GetCollisionCellKeys();
        context.broadphaseVisualizer.Update( static_cast<float>( secondsPerFrame ),
                                             activeCellBuf,
                                             activeCellCount,
                                             collisionKeys.data(),
                                             static_cast<int>( collisionKeys.size() ) );
        updateRequiredBroadphaseXCells( activeCellBuf, (std::min)( activeCellCount, SpatialGrid::MAX_BUCKETS ) );
    }
    PROFILE_END( "Frame/PostPhysics/BroadphaseVisualizer" );

    PROFILE_BEGIN( "Frame/PostPhysics/CollisionVisualizer" );
    context.collisionVisualizer.SetEnabled( context.debug.isCollisionVisualizer );
    context.models.UpdateCollisionVisualizer( context.collisionVisualizer, static_cast<float>( secondsPerFrame ) );
    PROFILE_END( "Frame/PostPhysics/CollisionVisualizer" );

    PROFILE_BEGIN( "Frame/PostPhysics/PhysicsDebugVisualizer" );
    context.physicsDebugVisualizer.SetFlags( context.debug.physicsDebugFlags );
    context.physicsDebugVisualizer.SetContactLingerSeconds( context.debug.physicsDebugContactLinger );
    context.physicsDebugVisualizer.SetPipelineStageCursor( context.debug.physicsDebugPipelineStageCursor );
    context.models.UpdatePhysicsDebugVisualizer( context.physicsDebugVisualizer,
                                                 static_cast<float>( secondsPerFrame ) );
    updateRequiredContacts();
    PROFILE_END( "Frame/PostPhysics/PhysicsDebugVisualizer" );

    PROFILE_BEGIN( "Frame/PostPhysics/EndCollisionVisualFrame" );
    context.models.EndCollisionVisualFrame();
    PROFILE_END( "Frame/PostPhysics/EndCollisionVisualFrame" );

    PROFILE_END( "Frame/PostPhysics" );
}


Vector3 RenderProbeMatrixTranslation( const Matrix4& matrix )
{
    return Vector3( matrix.m[12], matrix.m[13], matrix.m[14] );
}

bool TryPrepareReplayProbeRenderPosition( SkullbonezCore::GameObjects::GameModelCollection& collection,
                                          int modelIndex,
                                          Vector3& outPosition )
{
    const auto& instances = collection.RenderInstances().Records();
    if ( modelIndex < 0 || modelIndex >= static_cast<int>( instances.size() ) )
    {
        return false;
    }

    outPosition = RenderProbeMatrixTranslation( instances[static_cast<std::size_t>( modelIndex )].modelMatrix );
    return true;
}

bool ApplyReplayProbePresentationSampleForRender( SkullbonezCore::GameObjects::GameModelCollection& collection,
                                                  ReplayRuntime& replayRuntime,
                                                  const ReplayPresentationSample& sample )
{
    // Why: probes consume replay scrub poses exactly where the renderer consumes
    // them: after the live render snapshot refresh and before draw submission.
    // This proves presentation overrides do not mutate live body rows.
    collection.PrepareRenderInstances();
    PhysicsEngine& physics = collection.GetPhysicsEngine();
    return replayRuntime.ApplyPresentationSampleForRender( physics, sample );
}

void RestoreReplayProbeRenderInstances( SkullbonezCore::GameObjects::GameModelCollection& collection )
{
    collection.PrepareRenderInstances();
}

const PhysicsBodyRecord*
TryGetReplayProbeBodyRecord( const SkullbonezCore::GameObjects::GameModelCollection& collection, int modelIndex )
{
    const PhysicsBodyStore& bodyStore = collection.GetPhysicsEngine().BodyStore();
    const PhysicsBodyHandle bodyHandle = bodyStore.HandleForModelIndex( modelIndex );
    const PhysicsBodyRecord* body = bodyStore.RecordForHandle( bodyHandle );
    if ( !body || bodyStore.ModelIndexForHandle( bodyHandle ) != modelIndex )
    {
        return nullptr;
    }
    return body;
}


// Why: editor transform replay still mutates authoring data, but the shape it
// scales from is already owned by ColliderStore. Reading the store row here
// avoids treating presentation data as collision authority.
const ColliderRecord*
TryGetEditorTransformColliderRecord( const SkullbonezCore::GameObjects::GameModelCollection& collection,
                                     PhysicsColliderHandle colliderHandle,
                                     int modelIndex,
                                     uint32_t replayBodyId )
{
    const ColliderStore& colliderStore = collection.Colliders();
    const PhysicsBodyStore& bodyStore = collection.GetPhysicsEngine().BodyStore();
    const PhysicsBodyHandle bodyHandle = replayBodyId != 0u
                                             ? bodyStore.HandleForReplayBodyId( replayBodyId, modelIndex )
                                             : bodyStore.HandleForModelIndex( modelIndex );
    const PhysicsColliderHandle resolvedHandle =
        colliderHandle.IsValid() ? colliderHandle : colliderStore.HandleForBodyHandle( bodyHandle );
    const ColliderRecord* collider = colliderStore.RecordForHandle( resolvedHandle );
    if ( !collider || colliderStore.ModelIndexForHandle( resolvedHandle ) != modelIndex )
    {
        return nullptr;
    }
    if ( replayBodyId != 0 && collider->replayBodyId != replayBodyId )
    {
        return nullptr;
    }
    return collider;
}

constexpr uint32_t REPLAY_WORLD_OVERRIDE_GRAVITY_CHANGED = 1u;
constexpr uint32_t REPLAY_WORLD_OVERRIDE_FLUID_HEIGHT_CHANGED = 2u;
constexpr uint32_t REPLAY_WORLD_OVERRIDE_FLUID_DENSITY_CHANGED = 4u;
constexpr uint32_t REPLAY_LAUNCHER_FIRE_PROJECTILE = 1u;
constexpr uint32_t REPLAY_EDITOR_PLACE_FIXED = 1u;
constexpr uint32_t REPLAY_EDITOR_PLACE_TERRAIN_ALIGN = 2u;
constexpr uint32_t REPLAY_EDITOR_TRANSFORM_TRANSLATE = 1u;
constexpr uint32_t REPLAY_EDITOR_TRANSFORM_ROTATE = 2u;
constexpr uint32_t REPLAY_EDITOR_TRANSFORM_SCALE = 4u;
constexpr uint32_t REPLAY_EDITOR_TRANSFORM_SUPPORTED =
    REPLAY_EDITOR_TRANSFORM_TRANSLATE | REPLAY_EDITOR_TRANSFORM_ROTATE | REPLAY_EDITOR_TRANSFORM_SCALE;
constexpr uint32_t REPLAY_GENERATED_SCENE_EXACT_SOLVER_COUNTS = 1u;
constexpr uint32_t REPLAY_GENERATED_SCENE_UI_MODEL_COUNT = 2u;
constexpr uint32_t REPLAY_GENERATED_SCENE_UI_SOLVER_COUNTS = 4u;
constexpr uint32_t REPLAY_GENERATED_SCENE_OVERRIDE_SHIFT = 8u;
constexpr uint32_t REPLAY_GENERATED_SCENE_OVERRIDE_MASK = 3u << REPLAY_GENERATED_SCENE_OVERRIDE_SHIFT;

#ifdef _DEBUG
float ReplaySaveProbeDistanceSquared( const Vector3& a, const Vector3& b )
{
    const Vector3 delta = a - b;
    return delta.x * delta.x + delta.y * delta.y + delta.z * delta.z;
}


struct ReplaySaveProbeEventCoverageContext
{
    RunReplaySaveProbeState& saveProbe;
    ReplayRuntime& replayRuntime;
    RuntimeTools& runtimeTools;
    RunSceneState& scene;
    RunSubsystemState& systems;
    SkullbonezCore::Environment::WorldEnvironment& world;
    SkullbonezCore::GameObjects::GameModelCollection& models;
    int gameModelCapacity = 0;
};


template <typename EnterInteractiveSceneRun>
SbResult InjectReplaySaveProbeEventCoverage( ReplaySaveProbeEventCoverageContext& context,
                                             EnterInteractiveSceneRun enterInteractiveSceneRun )
{
    context.saveProbe.eventCoverageInjected = true;
    const float currentGravity = context.world.GetGravity();
    const float probeGravity = currentGravity != 0.0f ? currentGravity * 0.95f : -0.25f;
    ApplyUIWorldOverride( context.world,
                          context.replayRuntime,
                          probeGravity,
                          context.world.GetFluidSurfaceHeight(),
                          context.world.GetFluidDensity() );
    context.runtimeTools.Editor().placementScale = Vector3( 2.0f, 2.0f, 2.0f );
    context.runtimeTools.Editor().autoTerrainAlign = false;
    const int modelCountBeforePlace = context.models.SceneEntityCount();
    EditorObjectPlacementContext placementContext{ context.runtimeTools.Editor(),
                                                   context.models,
                                                   context.scene,
                                                   context.world,
                                                   context.systems.terrain.get(),
                                                   context.systems.assets,
                                                   context.gameModelCapacity };
    EditorObjectPlacementRequest placementRequest{ SkullbonezCore::UI::EditorTab::OBJECT_BOX,
                                                   true,
                                                   Vector3( 18.0f, 0.0f, 18.0f ) };
    EditorObjectPlacementResult placementResult;
    if ( CanPlaceEditorObjectAtTerrainPoint( placementContext, placementRequest ) )
    {
        enterInteractiveSceneRun();
        PlaceEditorObjectAtTerrainPoint( placementContext, placementRequest, placementResult );
    }
    if ( placementResult.placed )
    {
        context.replayRuntime.RecordEditorPlaceEvent( placementResult.objectType,
                                                      placementResult.fixedObject,
                                                      placementResult.autoTerrainAlign,
                                                      placementResult.modelCountBefore,
                                                      placementResult.terrainPoint,
                                                      placementResult.placementScale,
                                                      placementResult.placementYawRadians );
        const PhysicsBodyRecord* placedBodyBeforeEdit =
            context.models.GetPhysicsEngine().BodyStore().RecordForHandle( placementResult.placedBody );
        if ( !placedBodyBeforeEdit )
        {
            return ReplayProbeFailure( "replay save probe failed to resolve placed body record" );
        }
        // Why: placement has already registered a PhysicsBodyHandle. Use the
        // authoritative body row as the starting transform, then commit the
        // edited descriptor back into the stores below.
        SkullbonezCore::GameObjects::PhysicsBodyStateEdit placedBodyEdit;
        placedBodyEdit.hasPosition = true;
        placedBodyEdit.position = placedBodyBeforeEdit->position + Vector3( 4.0f, 0.0f, 0.0f );
        Quaternion placedOrientation = placedBodyBeforeEdit->orientation;
        placedOrientation.RotateAboutAxis( Vector3( 0.0f, 1.0f, 0.0f ), 0.25f );
        placedBodyEdit.hasOrientation = true;
        placedBodyEdit.orientation = placedOrientation;
        const ColliderRecord* placedColliderBeforeEdit =
            TryGetEditorTransformColliderRecord( context.models,
                                                 placementResult.placedCollider,
                                                 modelCountBeforePlace,
                                                 placedBodyBeforeEdit->replayBodyId );
        if ( !placedColliderBeforeEdit )
        {
            return ReplayProbeFailure( "replay save probe failed to resolve placed collider record" );
        }
        const CollisionShape placedShapeBeforeScale = placedColliderBeforeEdit->shape;
        constexpr int PROBE_SCALE_AXIS = 0;
        constexpr float PROBE_SCALE_FACTOR = 1.5f;
        CollisionShape placedShapeAfterScale;
        if ( !ScaleShapeAxisFromBase( placedShapeBeforeScale,
                                      PROBE_SCALE_AXIS,
                                      PROBE_SCALE_FACTOR,
                                      placedShapeAfterScale ) )
        {
            return ReplayProbeFailure( "replay save probe failed to apply editor transform scale" );
        }
        placedBodyEdit.hasLinearVelocity = true;
        placedBodyEdit.linearVelocity = Vector3( 0.0f, 0.0f, 0.0f );
        placedBodyEdit.hasAngularVelocity = true;
        placedBodyEdit.angularVelocity = Vector3( 0.0f, 0.0f, 0.0f );
        // Invariant: the replay probe exercises the same explicit collider
        // edit command as the editor instead of relying on a model recapture.
        context.models.ApplyPhysicsBodyColliderEdit(
            modelCountBeforePlace,
            placedBodyEdit,
            MakeColliderCreateDesc( std::move( placedShapeAfterScale ),
                                    placedColliderBeforeEdit->restitution,
                                    placedColliderBeforeEdit->contactMaterialId ) );
        const PhysicsBodyRecord* placedBodyAfterEdit =
            context.models.GetPhysicsEngine().BodyStore().RecordForModelIndex( modelCountBeforePlace );
        if ( !placedBodyAfterEdit || placedBodyAfterEdit->replayBodyId == 0 )
        {
            return ReplayProbeFailure( "replay save probe failed to capture edited body record" );
        }
        context.replayRuntime.RecordEditorTransformEvent(
            modelCountBeforePlace,
            REPLAY_EDITOR_TRANSFORM_TRANSLATE | REPLAY_EDITOR_TRANSFORM_ROTATE | REPLAY_EDITOR_TRANSFORM_SCALE,
            placedBodyAfterEdit->replayBodyId,
            placedBodyAfterEdit->position,
            placedBodyAfterEdit->orientation,
            context.models.SceneEntityCount(),
            PROBE_SCALE_AXIS,
            PROBE_SCALE_FACTOR );
    }
    context.runtimeTools.RayCastTest().projectileSpeed += 1.0f;
    context.replayRuntime.RecordLauncherConfigEvent( 2u,
                                                     context.runtimeTools.RayCastTest().impulseStrength,
                                                     context.runtimeTools.RayCastTest().projectileSpeed );
    Vector3 rayOrigin;
    Vector3 rayDirection;
    Vector3 cameraUp;
    if ( context.runtimeTools.TryBuildLauncherCameraRay( context.systems.cameras, rayOrigin, rayDirection, cameraUp ) )
    {
        context.replayRuntime.RecordLauncherFireEvent(
            rayOrigin,
            rayDirection,
            cameraUp,
            context.runtimeTools.RayCastTest().fireMode == RunLauncherFireMode::Projectile,
            context.runtimeTools.RayCastTest().impulseStrength,
            context.runtimeTools.RayCastTest().projectileSpeed,
            context.models.SceneEntityCount() );
        // Why: RuntimeTools now fails closed unless Run has completed the cold
        // collection-to-store topology repair at the owner boundary.
        const bool launcherStoresReady = context.models.RepairPhysicsBodyAndColliderTopology();
        if ( launcherStoresReady && context.runtimeTools.FireLauncherRay( context.models,
                                                                          context.scene,
                                                                          context.systems.terrain.get(),
                                                                          context.gameModelCapacity,
                                                                          rayOrigin,
                                                                          rayDirection,
                                                                          cameraUp ) )
        {
            context.scene.modelCount = context.models.SceneEntityCount();
        }
    }
    return SbResult::Success();
}


struct ReplaySaveProbeArtifactContext
{
    RunReplaySaveProbeState& saveProbe;
    ReplayRuntime& replayRuntime;
    SkullbonezCore::GameObjects::GameModelCollection& models;
};


SbResult ValidateReplaySaveProbeArtifact( ReplaySaveProbeArtifactContext& context )
{
    ReplayV2SaveResult result;
    if ( !context.replayRuntime.SavePresentationWithSolverHashes( context.saveProbe.path, &result ) )
    {
        return ReplayProbeFailure( "replay save probe failed to write v2 presentation artifact" );
    }
    if ( result.solverHashCount < result.sampleCount )
    {
        return ReplayProbeFailure( "replay save probe wrote v2 artifact without a full solver hash track" );
    }
    if ( result.solverCheckpointCount == 0 )
    {
        return ReplayProbeFailure( "replay save probe wrote v2 artifact without solver checkpoint chunks" );
    }
    if ( result.eventCount == 0 )
    {
        return ReplayProbeFailure( "replay save probe wrote v2 artifact without event chunks" );
    }
    if ( result.eventCursorCount == 0 )
    {
        return ReplayProbeFailure( "replay save probe wrote v2 artifact without checkpoint event cursors" );
    }

    std::vector<ReplayPresentationSample> loadedSamples;
    ReplayV2LoadResult loadResult;
    if ( !ReplayV2Artifact::LoadPresentation( context.saveProbe.path, loadedSamples, &loadResult ) )
    {
        return ReplayProbeFailure( "replay save probe failed to reload v2 presentation artifact" );
    }
    if ( loadedSamples.size() < 2 )
    {
        return ReplayProbeFailure( "replay save probe loaded too few v2 presentation samples" );
    }

    const std::size_t selectedIndex = (std::min)( loadedSamples.size() / 4, loadedSamples.size() - 2 );
    const ReplayPresentationSample& selected = loadedSamples[selectedIndex];
    const ReplayPresentationSample& live = loadedSamples.back();
    if ( selected.frameIndex >= live.frameIndex )
    {
        return ReplayProbeFailure( "replay save probe could not seek to an older loaded v2 sample" );
    }

    const ReplayBodyPresentationSample* selectedBody = nullptr;
    const ReplayBodyPresentationSample* liveBody = nullptr;
    float bestDistanceSquared = 0.0f;
    for ( const ReplayBodyPresentationSample& candidate : selected.bodies )
    {
        for ( const ReplayBodyPresentationSample& liveCandidate : live.bodies )
        {
            if ( liveCandidate.id.value != candidate.id.value )
            {
                continue;
            }

            const float candidateDistanceSquared =
                ReplaySaveProbeDistanceSquared( liveCandidate.position, candidate.position );
            if ( candidateDistanceSquared > bestDistanceSquared )
            {
                bestDistanceSquared = candidateDistanceSquared;
                selectedBody = &candidate;
                liveBody = &liveCandidate;
            }
            break;
        }
    }
    if ( !selectedBody || !liveBody || bestDistanceSquared < 0.0001f )
    {
        return ReplayProbeFailure( "replay save probe did not find a moved body in the loaded v2 artifact" );
    }

    const int probedModelIndex = liveBody->modelIndex;
    const PhysicsBodyRecord* probedBody = TryGetReplayProbeBodyRecord( context.models, probedModelIndex );
    if ( !probedBody )
    {
        return ReplayProbeFailure( "replay save probe loaded an invalid live body index" );
    }

    const Vector3 preApplyPosition = probedBody->position;
    const float preLiveDeltaSquared = ReplaySaveProbeDistanceSquared( preApplyPosition, liveBody->position );
    if ( preLiveDeltaSquared > 0.0001f )
    {
        return ReplayProbeFailure( "replay save probe live body did not match the loaded v2 live sample" );
    }

    const bool applied = ApplyReplayProbePresentationSampleForRender( context.models, context.replayRuntime, selected );
    if ( !applied )
    {
        return ReplayProbeFailure( "replay save probe failed to apply the loaded v2 presentation sample" );
    }
    const PhysicsBodyRecord* appliedBody = TryGetReplayProbeBodyRecord( context.models, probedModelIndex );
    if ( !appliedBody )
    {
        RestoreReplayProbeRenderInstances( context.models );
        return ReplayProbeFailure( "replay save probe lost the selected live body after applying the v2 sample" );
    }
    const Vector3 liveAfterApplyPosition = appliedBody->position;
    const float livePreservedDeltaSquared = ReplaySaveProbeDistanceSquared( liveAfterApplyPosition, preApplyPosition );
    if ( livePreservedDeltaSquared > 0.0001f )
    {
        RestoreReplayProbeRenderInstances( context.models );
        return ReplayProbeFailure( "replay save probe mutated the live body while applying the v2 sample" );
    }

    Vector3 appliedRenderPosition;
    if ( !TryPrepareReplayProbeRenderPosition( context.models, probedModelIndex, appliedRenderPosition ) )
    {
        RestoreReplayProbeRenderInstances( context.models );
        return ReplayProbeFailure( "replay save probe lost the selected render instance after applying the v2 sample" );
    }
    const float appliedDeltaSquared = ReplaySaveProbeDistanceSquared( appliedRenderPosition, selectedBody->position );
    if ( appliedDeltaSquared > 0.0001f )
    {
        RestoreReplayProbeRenderInstances( context.models );
        return ReplayProbeFailure( "replay save probe did not move the render instance to the loaded v2 sample" );
    }

    RestoreReplayProbeRenderInstances( context.models );
    const PhysicsBodyRecord* restoredBody = TryGetReplayProbeBodyRecord( context.models, probedModelIndex );
    if ( !restoredBody )
    {
        return ReplayProbeFailure( "replay save probe lost the selected live body after restoring the v2 sample" );
    }
    const Vector3 restoredPosition = restoredBody->position;
    const float restoredDeltaSquared = ReplaySaveProbeDistanceSquared( restoredPosition, preApplyPosition );
    if ( restoredDeltaSquared > 0.0001f )
    {
        return ReplayProbeFailure( "replay save probe live body changed after applying the loaded v2 sample" );
    }

    context.saveProbe.completed = true;
    printf( "[replay] Save probe wrote: path=%s samples=%llu bodies=%llu solver_hashes=%llu "
            "solver_checkpoints=%llu events=%llu event_cursors=%llu bytes=%llu\n",
            context.saveProbe.path,
            static_cast<unsigned long long>( result.sampleCount ),
            static_cast<unsigned long long>( result.bodyDictionaryCount ),
            static_cast<unsigned long long>( result.solverHashCount ),
            static_cast<unsigned long long>( result.solverCheckpointCount ),
            static_cast<unsigned long long>( result.eventCount ),
            static_cast<unsigned long long>( result.eventCursorCount ),
            static_cast<unsigned long long>( result.fileBytes ) );
    printf( "[replay] Save probe loaded: samples=%llu bodies=%llu first_frame=%llu selected_frame=%llu "
            "latest_frame=%llu body_id=%u distance_sq=%.6f\n",
            static_cast<unsigned long long>( loadResult.sampleCount ),
            static_cast<unsigned long long>( loadResult.bodyDictionaryCount ),
            static_cast<unsigned long long>( loadResult.firstFrame ),
            static_cast<unsigned long long>( selected.frameIndex ),
            static_cast<unsigned long long>( live.frameIndex ),
            selectedBody->id.value,
            bestDistanceSquared );
    PostQuitMessage( 0 );
    return SbResult::Success();
}
#endif

// Concept: replay flags are compact wire-format fields. Keep these masks local
// to decode logic so new replay payload versions do not inherit accidental UI
// or runtime enum values.

// Concept: fixed-step edge.
//
// PhysicsEngine::Step owns the store-backed solve. GameModelCollection still
// owns topology repair, model highlight timers, and Debug presentation names,
// so the runtime frame applies those owner-side edges explicitly around the
// store-owned engine step.
void StepRuntimePhysicsTick( SkullbonezCore::GameObjects::GameModelCollection& modelCollection,
                             float fixedDt,
                             const EngineConfig& config,
                             const PhysicsWorldForces& worldForces,
                             SkullbonezCore::Threading::WorkerPool& workerPool )
{
    const int modelCount = modelCollection.SceneEntityCount();
    // Invariant: PhysicsBodyStore is the per-tick body authority. Descriptor
    // sidecars are imported only when model/body/collider topology changes;
    // same-count editor or replay mutations must commit explicitly before this
    // step reads store rows.
    modelCollection.RepairPhysicsBodyAndColliderTopology();
    modelCollection.TickContactHighlights( modelCount, fixedDt );

    PhysicsEngine& physicsEngine = modelCollection.GetPhysicsEngine();
    const char* const* diagnosticNames = nullptr;
    int diagnosticNameCount = 0;
    PhysicsDiagnosticsCsvWriter diagnosticsCsvWriter;
#ifdef _DEBUG
    diagnosticsCsvWriter.writeVf = WriteRuntimePhysicsDiagnosticsCsv;
    std::vector<const char*> physicsDiagnosticsModelNames;
    if ( physicsEngine.ShouldEmitStepDiagnostics() || physicsEngine.ShouldEmitCollisionTimeDiagnostics() )
    {
        // Lifetime: Debug diagnostics borrow model name pointers only until
        // Step returns; physics never retains this presentation table after
        // emitting frame diagnostics.
        modelCollection.FillPhysicsDiagnosticsNames( physicsEngine.BodyStore().Count(), physicsDiagnosticsModelNames );
        diagnosticNames = physicsDiagnosticsModelNames.empty() ? nullptr : physicsDiagnosticsModelNames.data();
        diagnosticNameCount = static_cast<int>( physicsDiagnosticsModelNames.size() );
    }
#endif
    physicsEngine
        .Step( fixedDt, config, worldForces, workerPool, diagnosticNames, diagnosticNameCount, diagnosticsCsvWriter );

    // Why: fixed-contact highlights are presentation feedback, not solver
    // state. Keeping this edge here leaves the normal step visibly store-owned
    // instead of hiding side effects in GameModelCollection.
    for ( int index : physicsEngine.GetFixedContactHighlightBodies() )
    {
        modelCollection.NotifyFixedContact( index, 0.5f );
    }
}

struct SimulationPostStepPipelineContext
{
    SkullbonezCore::Runtime::Audio::ContactAudioService& contactAudio;
    RunRuntimeSettings& runtimeSettings;
    RunTimerState& timers;
    DiagnosticsRuntime& diagnosticsRuntime;
    RunSceneState& scene;
    RunDebugState& debug;
    RunSubsystemState& systems;
    RuntimeTools& runtimeTools;
    ReplayRuntime& replayRuntime;
    ReplayLauncherVisualSample& replayLauncherVisualScratch;
    SkullbonezCore::Environment::WorldEnvironment& world;
    SkullbonezCore::GameObjects::GameModelCollection& models;
};

struct SimulationPostStepPipelineResult
{
    bool replayCaptured = false;
};

class SimulationPostStepPipeline
{
  public:
    static SimulationPostStepPipelineResult Run( SimulationPostStepPipelineContext& context )
    {
        SimulationPostStepPipelineResult result;
        if ( context.contactAudio.IsEnabled() )
        {
            RunContactAudio( context );
        }
        if ( context.replayRuntime.IsCaptureEnabled() )
        {
            CaptureReplayFrame( context );
            result.replayCaptured = true;
        }
        return result;
    }

  private:
    static void RunContactAudio( SimulationPostStepPipelineContext& context )
    {
        PROFILE_SCOPED( "Frame/Physics/Step/ContactAudio" );

        const Vector3 listenerPosition = context.systems.cameras ? context.systems.cameras->GetRenderCameraTranslation()
                                                                 : SkullbonezCore::Math::Vector::ZERO_VECTOR;
        context.contactAudio.BeginPhysicsStep( PHYSICS_FIXED_DT, listenerPosition );

        const auto& colliderRecords = context.models.GetPhysicsEngine().Colliders().Records();
        auto materialForBody = [&]( int bodyIndex ) -> uint32_t
        {
            if ( bodyIndex >= 0 && bodyIndex < static_cast<int>( colliderRecords.size() ) )
            {
                return colliderRecords[static_cast<std::size_t>( bodyIndex )].contactMaterialId;
            }
            return HashStr( "default" );
        };

        if ( context.contactAudio.SimpleModeEnabled() )
        {
            // Why: Simple Mode answers the practical sound question directly:
            // did a dynamic body experience enough mass-scaled linear velocity
            // change to be heard? Motion comes from PhysicsBodyStore and contact
            // material comes from the paired ColliderStore row.
            const auto& bodyRecords = context.models.GetPhysicsEngine().BodyStore().Records();
            const int simpleBodyCount = static_cast<int>(
                bodyRecords.size() < colliderRecords.size() ? bodyRecords.size() : colliderRecords.size() );
            context.contactAudio.BeginSimpleLinearStep( simpleBodyCount );
            for ( int bodyIndex = 0; bodyIndex < simpleBodyCount; ++bodyIndex )
            {
                const PhysicsBodyRecord& body = bodyRecords[static_cast<std::size_t>( bodyIndex )];
                if ( body.isFixed )
                {
                    continue;
                }
                context.contactAudio.SubmitLinearMotion(
                    bodyIndex,
                    colliderRecords[static_cast<std::size_t>( bodyIndex )].contactMaterialId,
                    body.position,
                    body.linearVelocity,
                    body.mass );
            }
        }
        else
        {
            // Why: PhysicsDebugContact rows are emitted after accumulated normal
            // impulses are known. Audio can consume those facts without entering
            // solver math or changing deterministic physics state.
            const std::vector<PhysicsDebugContact>& contacts = context.models.GetPhysicsDebugContacts();
            for ( const PhysicsDebugContact& contact : contacts )
            {
                if ( contact.bodyA < 0 || contact.normalImpulse <= 0.0f )
                {
                    continue;
                }

                SkullbonezCore::Runtime::Audio::ContactAudioEvent event;
                event.bodyA = contact.bodyA;
                event.bodyB = contact.bodyB;
                event.featureId = contact.featureId;
                event.materialA = materialForBody( contact.bodyA );
                event.materialB = materialForBody( contact.bodyB );
                event.point = contact.point;
                event.normal = contact.normal;
                event.normalImpulse = contact.normalImpulse;
                // Why: sound uses pre-solve relative motion so stationary wall bricks
                // receiving propagated constraint force do not all become emitters.
                event.normalClosingSpeed = contact.preSolveClosingSpeed;
                event.tangentSlipSpeed = contact.preSolveSlipSpeed;
                event.isTerrain = contact.bodyB < 0;
                event.hasMotionData = true;
                context.contactAudio.SubmitContact( event );
            }
        }

        context.contactAudio.EndPhysicsStep();
#ifdef _DEBUG
        if ( context.diagnosticsRuntime.PhysicsDiagnosticsEnabled() )
        {
            RuntimeDiagnostics::LogContactAudioStepStats( context.diagnosticsRuntime.PhysicsDiagnostics(),
                                                          context.scene,
                                                          context.contactAudio.StepStats() );
            const int decisionCount = context.contactAudio.DecisionCount();
            for ( int i = 0; i < decisionCount; ++i )
            {
                SkullbonezCore::Runtime::Audio::ContactAudioDecision decision;
                if ( context.contactAudio.GetDecision( i, decision ) )
                {
                    RuntimeDiagnostics::LogContactAudioDecision( context.diagnosticsRuntime.PhysicsDiagnostics(),
                                                                 context.scene,
                                                                 decision );
                }
            }
        }
#endif
        if ( context.runtimeSettings.contactAudioFlashMode != ContactAudioFlashMode::Off )
        {
            // Why: Sound-tab diagnostics can visualize emitted sounds, all
            // candidates, or rejected candidates without touching physics state.
            constexpr float CONTACT_AUDIO_FLASH_SECONDS = 0.1f;
            const int decisionCount = context.contactAudio.DecisionCount();
            for ( int i = 0; i < decisionCount; ++i )
            {
                SkullbonezCore::Runtime::Audio::ContactAudioDecision decision;
                if ( !context.contactAudio.GetDecision( i, decision ) ||
                     !ShouldFlashContactAudioDecision( context.runtimeSettings.contactAudioFlashMode, decision ) )
                {
                    continue;
                }

                context.models.NotifyAudioContact( decision.event.bodyA, CONTACT_AUDIO_FLASH_SECONDS );
                context.models.NotifyAudioContact( decision.event.bodyB, CONTACT_AUDIO_FLASH_SECONDS );
            }
        }
        if ( context.runtimeSettings.contactAudioDebugCounters )
        {
            context.timers.contactAudioStatsLogTime += PHYSICS_FIXED_DT;
            if ( context.timers.contactAudioStatsLogTime >= 1.0f )
            {
                const SkullbonezCore::Runtime::Audio::ContactAudioStats& stats = context.contactAudio.Stats();
                printf( "[audio] contact stats facts=%u patches=%u merged=%u threshold=%u cooldown=%u "
                        "submitted=%u rolling=%u/%u budget=%u dropped=%u\n",
                        stats.eventsSeen,
                        stats.patchCandidates,
                        stats.mergedCandidates,
                        stats.rejectedByThreshold,
                        stats.rejectedByCooldown,
                        stats.submittedVoices,
                        stats.rollingSubmittedVoices,
                        stats.rollingCandidates,
                        stats.candidateOverflows + stats.burstWindowSkippedCandidates + stats.budgetRejectedCandidates,
                        stats.droppedVoices );
                context.contactAudio.ResetFrameStats();
                context.timers.contactAudioStatsLogTime = 0.0f;
            }
        }
    }

    static void CaptureReplayFrame( SimulationPostStepPipelineContext& context )
    {
        RuntimeAllocation::RuntimeAllocationScope allocationScope( RuntimeAllocation::RuntimeAllocationPhase::Replay );
        PROFILE_SCOPED( "Frame/Physics/Step/ReplayCapture" );
        context.runtimeTools.BuildReplayLauncherVisualSample( context.replayLauncherVisualScratch );

        ReplayCaptureInput input;
        input.sceneFrame = context.scene.currentFrame;
        input.simulationSeconds = context.timers.simulationTimer.GetTimeSinceLastStart();
        input.physicsDt = PHYSICS_FIXED_DT;
        input.fixedStep = context.scene.isFixedStep;
        input.scenePhysicsEnabled = context.scene.isScenePhysics;
        input.sceneTextEnabled = context.scene.isSceneText;
        input.waterHidden = context.debug.isWaterHidden;
        input.terrainHidden = context.debug.isTerrainHidden;
        input.cameras = context.systems.cameras;
        input.world = &context.world;
        input.models = &context.models;
        input.bodyStore = &context.models.GetPhysicsEngine().BodyStore();
        input.colliderStore = &context.models.GetPhysicsEngine().Colliders();
        input.launcherVisual = &context.replayLauncherVisualScratch;
        context.replayRuntime.CaptureFrame( input );
    }
};

SceneGeneratedModelContext BuildSceneGeneratedModelContext( RunSceneState& scene,
                                                            const EngineConfig& config,
                                                            SkullbonezCore::Environment::WorldEnvironment& world,
                                                            SkullbonezCore::Geometry::Terrain* terrain,
                                                            SkullbonezCore::GameObjects::GameModelCollection& models,
                                                            SkullbonezCore::Physics::PhysicsEngine& physics,
                                                            GeneratedObjectTypeOverride objectTypeOverride )
{
    return SceneGeneratedModelContext{ scene, config, world, terrain, models, physics, objectTypeOverride };
}

float ReplayEventFloatFromBits( int32_t signedBits )
{
    uint32_t bits = 0;
    float value = 0.0f;
    static_assert( sizeof( bits ) == sizeof( signedBits ), "Replay event float bits must be 32-bit." );
    static_assert( sizeof( bits ) == sizeof( value ), "Replay event float payloads assume 32-bit floats." );
    std::memcpy( &bits, &signedBits, sizeof( bits ) );
    std::memcpy( &value, &bits, sizeof( value ) );
    return value;
}

int ReplayHexNibble( char value )
{
    if ( value >= '0' && value <= '9' )
    {
        return value - '0';
    }
    if ( value >= 'a' && value <= 'f' )
    {
        return value - 'a' + 10;
    }
    if ( value >= 'A' && value <= 'F' )
    {
        return value - 'A' + 10;
    }
    return -1;
}

bool ReadReplayHexFloat( const char*& cursor, float& outValue )
{
    uint32_t bits = 0;
    for ( int i = 0; i < 8; ++i )
    {
        const int nibble = ReplayHexNibble( cursor[i] );
        if ( nibble < 0 )
        {
            return false;
        }
        bits = ( bits << 4 ) | static_cast<uint32_t>( nibble );
    }
    cursor += 8;
    std::memcpy( &outValue, &bits, sizeof( outValue ) );
    return true;
}

bool DecodeReplayRay9Payload( const ReplayEventSample& event,
                              Vector3& outOrigin,
                              Vector3& outDirection,
                              Vector3& outCameraUp )
{
    constexpr char prefix[] = "ray9:";
    if ( std::strncmp( event.text, prefix, sizeof( prefix ) - 1 ) != 0 )
    {
        return false;
    }

    const char* cursor = event.text + sizeof( prefix ) - 1;
    float values[9] = {};
    for ( float& value : values )
    {
        if ( !ReadReplayHexFloat( cursor, value ) )
        {
            return false;
        }
    }

    outOrigin = Vector3( values[0], values[1], values[2] );
    outDirection = Vector3( values[3], values[4], values[5] );
    outCameraUp = Vector3( values[6], values[7], values[8] );
    return true;
}

bool DecodeReplayPlacePayload( const ReplayEventSample& event,
                               Vector3& outTerrainPoint,
                               Vector3& outPlacementScale,
                               float& outPlacementYawRadians )
{
    constexpr char prefix6[] = "place6:";
    constexpr char prefix7[] = "place7:";
    int valueCount = 0;
    const char* cursor = nullptr;
    if ( std::strncmp( event.text, prefix7, sizeof( prefix7 ) - 1 ) == 0 )
    {
        valueCount = 7;
        cursor = event.text + sizeof( prefix7 ) - 1;
    }
    else if ( std::strncmp( event.text, prefix6, sizeof( prefix6 ) - 1 ) == 0 )
    {
        valueCount = 6;
        cursor = event.text + sizeof( prefix6 ) - 1;
    }
    else
    {
        return false;
    }

    float values[7] = {};
    for ( int i = 0; i < valueCount; ++i )
    {
        if ( !ReadReplayHexFloat( cursor, values[i] ) )
        {
            return false;
        }
    }

    outTerrainPoint = Vector3( values[0], values[1], values[2] );
    outPlacementScale = Vector3( values[3], values[4], values[5] );
    outPlacementYawRadians = valueCount >= 7 ? values[6] : 0.0f;
    return true;
}


bool DecodeReplayTransformPayload( const ReplayEventSample& event,
                                   Vector3& outPosition,
                                   Quaternion& outOrientation,
                                   float& outScaleFactor,
                                   bool& outHasScaleFactor )
{
    constexpr char prefix7[] = "xform7:";
    constexpr char prefix8[] = "xform8:";
    int valueCount = 0;
    const char* cursor = nullptr;
    if ( std::strncmp( event.text, prefix7, sizeof( prefix7 ) - 1 ) == 0 )
    {
        valueCount = 7;
        cursor = event.text + sizeof( prefix7 ) - 1;
        outHasScaleFactor = false;
    }
    else if ( std::strncmp( event.text, prefix8, sizeof( prefix8 ) - 1 ) == 0 )
    {
        valueCount = 8;
        cursor = event.text + sizeof( prefix8 ) - 1;
        outHasScaleFactor = true;
    }
    else
    {
        return false;
    }

    float values[8] = {};
    for ( int i = 0; i < valueCount; ++i )
    {
        if ( !ReadReplayHexFloat( cursor, values[i] ) )
        {
            return false;
        }
    }

    outPosition = Vector3( values[0], values[1], values[2] );
    outOrientation = Quaternion( values[3], values[4], values[5], values[6] );
    outOrientation.Normalise();
    outScaleFactor = outHasScaleFactor ? values[7] : 1.0f;
    return true;
}


const ReplayV2SolverHashSample* FindReplaySolverHashForFrame( const std::vector<ReplayV2SolverHashSample>& hashes,
                                                              ReplayFrameIndex frameIndex )
{
    for ( const ReplayV2SolverHashSample& hash : hashes )
    {
        if ( hash.frameIndex == frameIndex )
        {
            return &hash;
        }
    }
    return nullptr;
}

const ReplayPresentationSample* FindReplayPresentationForFrame( const std::vector<ReplayPresentationSample>& samples,
                                                                ReplayFrameIndex frameIndex )
{
    for ( const ReplayPresentationSample& sample : samples )
    {
        if ( sample.frameIndex == frameIndex )
        {
            return &sample;
        }
    }
    return nullptr;
}

void WriteReplayProbeReason( char* outReason, std::size_t reasonSize, const char* reason )
{
    if ( outReason && reasonSize > 0 )
    {
        strncpy_s( outReason, reasonSize, reason ? reason : "event replay failed", _TRUNCATE );
    }
}

class ScopedReplayProbeProfilerFrame
{
  public:
    ScopedReplayProbeProfilerFrame()
    {
        PROFILE_FRAME_BEGIN();
    }
    ~ScopedReplayProbeProfilerFrame()
    {
        PROFILE_FRAME_END();
    }
};
} // namespace

void Run::Execute()
{
    MSG msg;

    for ( ;; )
    {
        if ( PeekMessage( &msg, nullptr, 0, 0, PM_REMOVE ) )
        {
            if ( msg.message == WM_QUIT )
            {
                if ( m_graphicsStress.IsEnabled() )
                {
                    printf( "[graphics-stress] WM_QUIT received at frame=%d scene_frame=%d scene_loads=%d\n",
                            m_graphicsStress.FramesRun(),
                            SceneState().currentFrame,
                            m_graphicsStress.SceneLoadsRequested() );
                    fflush( stdout );
                }
                break;
            }
            TranslateMessage( &msg );
            DispatchMessage( &msg );
        }
        else
        {
            RuntimeAllocation::RuntimeAllocationScope frameAllocationScope(
                RuntimeAllocation::RuntimeAllocationPhase::SteadyGameplay );
            double secondsPerFrame = m_timers.frameTimer.GetElapsedTime();
            secondsPerFrame = std::clamp( secondsPerFrame, 0.0, 0.05 );

            m_timers.frameTimer.StartTimer();
            PROFILE_FRAME_BEGIN();
            m_timers.workTimer.StartTimer();
            // Lifetime: borrow the startup-owned renderer once for this frame
            // turn. Narrow facets keep reset, GPU-drain, UI accounting, and
            // present from each reaching through the process-global service.
            if ( !m_renderBackendView.deviceLifecycle || !m_renderBackendView.renderDiagnostics ||
                 !m_renderBackendView.renderResources || !m_renderBackendView.renderCommands )
            {
                throw std::runtime_error( "Run::Execute requires a render backend" );
            }
            SkullbonezCore::Rendering::IRenderDiagnostics& frameRenderDiagnostics =
                *m_renderBackendView.renderDiagnostics;
            SkullbonezCore::Rendering::IRenderDeviceLifecycle& renderLifecycle = *m_renderBackendView.deviceLifecycle;
            SkullbonezCore::Rendering::IRenderResourceFactory& frameRenderResources =
                *m_renderBackendView.renderResources;
            SkullbonezCore::Rendering::IRenderCommandContext& frameRenderCommands = *m_renderBackendView.renderCommands;
            const SkullbonezCore::UI::UIRenderContext uiRender = { &m_systems.assets,
                                                                   &frameRenderResources,
                                                                   &frameRenderCommands,
                                                                   &frameRenderDiagnostics };
            frameRenderDiagnostics.ResetFrameDrawCalls();

            PROFILE_BEGIN( "Frame/Input" );
            TickInteractionAutomationBeforeInput();
            TakeInput();
            TickLiveStyleControl();
            PROFILE_END( "Frame/Input" );

            m_cGameModelCollection.BeginCollisionVisualFrame();
            {
                RuntimeAllocation::RuntimeAllocationScope allocationScope(
                    RuntimeAllocation::RuntimeAllocationPhase::Physics );
                TickPhysics( secondsPerFrame );
            }

            ExecutePostPhysicsVisualizationContext postPhysicsVisualizationContext{ m_debug,
                                                                                    m_cGameModelCollection,
                                                                                    m_broadphaseVisualizer,
                                                                                    m_collisionVisualizer,
                                                                                    m_physicsDebugVisualizer };
            TickExecutePostPhysicsVisualizers(
                postPhysicsVisualizationContext,
                secondsPerFrame,
                [this]( const SpatialGrid::ActiveCell* activeCells, int activeCellCount )
                { UpdateRequiredSceneBroadphaseXCells( activeCells, activeCellCount ); },
                [this]() { UpdateRequiredSceneContacts(); } );

            // Concept: graphics stress is render/runtime churn, not UI command
            // processing. Tick it once per rendered frame so headless and
            // overnight launches keep mutating DX12 state even when the UI
            // command panel is not producing control messages.
            RunGraphicsStressActions( frameRenderDiagnostics );

            if ( m_runtimeSettings.isPipelineSyncEnabled )
            {
                PROFILE_BEGIN( "Frame/PipelineSync" );
                {
                    RuntimeAllocation::RuntimeAllocationScope allocationScope(
                        RuntimeAllocation::RuntimeAllocationPhase::Render );
                    renderLifecycle.Finish();
                }
                PROFILE_END( "Frame/PipelineSync" );
            }

            RuntimeRenderModelFrameView renderModels = m_renderer.BuildModelFrameView( m_cGameModelCollection );

            PROFILE_BEGIN( "Frame/Render" );
            {
                RuntimeAllocation::RuntimeAllocationScope allocationScope(
                    RuntimeAllocation::RuntimeAllocationPhase::Render );
                DRAW_CALL_TRACE_SCOPE( frameRenderDiagnostics, "Frame/Render" );
                Render( renderModels );
            }
            PROFILE_END( "Frame/Render" );

            ExecuteUiTextFrameContext uiTextFrameContext{ m_renderer,
                                                          frameRenderDiagnostics,
                                                          uiRender,
                                                          renderModels,
                                                          m_diagnosticsRuntime,
                                                          m_replayRuntime,
                                                          m_timers,
                                                          m_debug,
                                                          SceneState(),
                                                          m_runtimeSettings,
                                                          m_config,
                                                          m_cWorldEnvironment,
                                                          m_runtimeTools,
                                                          m_UI,
                                                          m_runtimeInput,
                                                          m_camera,
                                                          m_runtimeViewModel,
                                                          m_sceneController,
                                                          m_systems,
                                                          m_launchOptions,
                                                          CameraModeEnabledMask(),
                                                          CameraModeLabel( m_camera.mode ),
                                                          m_runtimeTools.LauncherFireModeLabel(),
                                                          IsLauncherCameraMode(),
                                                          secondsPerFrame };
            RenderExecuteUiTextFrame( uiTextFrameContext, [this]() { RefreshRuntimeViewModel(); } );

            PROFILE_BEGIN( "Frame/PostDraw/LiveStyleCapture" );
            {
                RuntimeAllocation::RuntimeAllocationScope allocationScope(
                    RuntimeAllocation::RuntimeAllocationPhase::Capture );
                TickLiveStyleControlCapture();
            }
            PROFILE_END( "Frame/PostDraw/LiveStyleCapture" );

            PROFILE_BEGIN( "Frame/PostDraw/InteractionAutomation" );
            TickInteractionAutomationAfterRender();
            PROFILE_END( "Frame/PostDraw/InteractionAutomation" );

            {
                RuntimeAllocation::RuntimeAllocationScope allocationScope(
                    RuntimeAllocation::RuntimeAllocationPhase::Capture );
                if ( TickScreenshots() )
                {
                    continue;
                }
            }

            PROFILE_BEGIN( "Frame/PostDraw/AutoCycle" );
            TickAutoCycle();
            PROFILE_END( "Frame/PostDraw/AutoCycle" );

            m_timers.workTimer.StopTimer();
            m_timers.cpuFrameWorkMs =
                static_cast<float>( std::clamp( m_timers.workTimer.GetElapsedTime(), 0.0, 0.25 ) * 1000.0 );

            PROFILE_BEGIN( "Frame/VsyncWait" );
            {
                RuntimeAllocation::RuntimeAllocationScope allocationScope(
                    RuntimeAllocation::RuntimeAllocationPhase::Render );
                renderLifecycle.Present();
            }
            PROFILE_END( "Frame/VsyncWait" );

            m_timers.frameTimer.StopTimer();
            PROFILE_FRAME_END();

#if defined( SKULLBONEZ_PROFILE_ENABLED )
            {
                const RuntimeProfilerFrameTimes profilerTimes = m_diagnosticsRuntime.SampleProfilerFrameTimes();
                m_timers.physicsTime = profilerTimes.physicsTimeSeconds;
                m_timers.renderTime = profilerTimes.renderTimeSeconds;
                m_timers.gpuFrameWorkMs = profilerTimes.gpuFrameWorkMs;
            }
#endif

            m_diagnosticsRuntime.TickPerfLog( RuntimePerfTickContext{ sPerfPass + 1,
                                                                      SceneState().currentFrame + 1,
                                                                      m_timers.physicsTime,
                                                                      m_timers.renderTime } );

            if ( TickSceneAdvance() )
            {
                continue;
            }
        }
    }
}


void Run::TickPhysics( double secondsPerFrame )
{
    if ( m_replayRuntime.IsScrubPaused() )
    {
        PROFILE_SCOPED( "Frame/Replay/ScrubCamera" );
        UpdateLogic( 0.0f, static_cast<float>( secondsPerFrame ) );
        return;
    }

    const bool replayLiveAdvanceHeld = m_replayRuntime.Scrubber().liveAdvanceHeld;
    const bool stepRequested = Input::IsKeyDown( VK_SPACE );
    const bool replayCapture = m_replayRuntime.IsCaptureEnabled();
#ifdef _DEBUG
    const bool physicsCapture = m_diagnosticsRuntime.PerfLog().physicsRegressionLogOverride[0] != '\0' ||
                                m_diagnosticsRuntime.PerfLog().physicsCollisionTimeLogOverride[0] != '\0' ||
                                m_diagnosticsRuntime.PhysicsDiagnostics().isEnabled;
#else
    constexpr bool physicsCapture = false;
#endif
    RuntimeInteractionFramePolicy policy = m_interaction.BuildFramePolicy(
        RuntimeInteractionFrameInput{ SceneState().isScenePhysics,
                                      stepRequested,
                                      false,
                                      replayLiveAdvanceHeld,
                                      Input::IsRightMouseDown(),
                                      m_runtimeTools.Editor().viewportLookActive,
                                      m_replayRuntime.InspectionMouseLookActive( Input::IsRightMouseDown(),
                                                                                 m_UI.WantsNativeMouseCursor(),
                                                                                 m_UI.BlocksCameraMouse() ),
                                      physicsCapture,
                                      SceneState().timeScale } );
    if ( m_debug.isCrossScenePauseLocked )
    {
        // Invariant: the P-key pause lock outranks camera/tool mode. Launcher
        // and passive scene cameras normally keep physics running, but the lock
        // requires Space before any simulation step can proceed.
        policy.physicsAdvance = PhysicsAdvanceState::RunWhileStepHeld;
        if ( !stepRequested )
        {
            policy.physicsTimeScale = 0.0f;
        }
    }
    const bool manipulatorPhysics = policy.manipulatorActive;
    const bool contactAudioStep = m_contactAudio.IsEnabled();
    const auto physicsWorldForces = m_cWorldEnvironment.GetPhysicsWorldForces();
    const bool canStepPhysics = m_systems.config != nullptr && m_systems.workerPool != nullptr;
    const SimulationTickResult tick = m_simulation.Tick( SimulationTickInput{ secondsPerFrame,
                                                                              policy.physicsTimeScale,
                                                                              SceneState().isSceneMode,
                                                                              SceneState().isScenePhysics,
                                                                              SceneState().isFixedStep,
                                                                              policy.physicsAdvance,
                                                                              stepRequested,
                                                                              canStepPhysics } );
    if ( tick.committedPhysicsTicks > 0 && canStepPhysics )
    {
        PROFILE_BEGIN( "Frame/Physics" );
        // Why: SimulationSystem now returns only a deterministic tick count.
        // Runtime executes the store-owned physics step directly, then applies
        // the remaining model-owned presentation sync as explicit edge work.
        for ( int tickIndex = 0; tickIndex < tick.committedPhysicsTicks; ++tickIndex )
        {
            PROFILE_SCOPED( "Frame/Physics/Step" );
            if ( manipulatorPhysics )
            {
                ApplyMousePickupPhysicsStep();
            }

            StepRuntimePhysicsTick( m_cGameModelCollection,
                                    PHYSICS_FIXED_DT,
                                    *m_systems.config,
                                    physicsWorldForces,
                                    *m_systems.workerPool );

            if ( manipulatorPhysics || replayCapture || contactAudioStep )
            {
                AfterPhysicsStep();
            }
        }
        PROFILE_END( "Frame/Physics" );
    }
    m_runtimeTools.TickRayCastTestLines( static_cast<float>( secondsPerFrame ) );
    m_runtimeTools.Laser().Update( static_cast<float>( secondsPerFrame ) );
    if ( tick.shouldUpdateLogic )
    {
        UpdateLogic( tick.simulationDt, tick.cameraDt );
    }
    else
    {
        // Why: Scene-mode, no-physics harnesses intentionally skip simulation
        // UpdateLogic, but Director is presentation state. It still needs phase
        // style/camera entry work so authored show decks behave in static scenes.
        DemoDirectorPlayback::Tick( m_camera,
                                    m_systems,
                                    m_replayRuntime.Prediction(),
                                    SceneRuntimeStyleContext{ m_launchOptions,
                                                              SceneState(),
                                                              m_sceneController.Browser(),
                                                              m_cGameModelCollection,
                                                              m_systems.assets,
                                                              RuntimeActiveCinematicConfig( SceneState(), m_config ),
                                                              m_defaultCinematicRender },
                                    static_cast<float>( secondsPerFrame ) );
    }
}


void Run::AfterPhysicsStep()
{
    RestoreMousePickupAngularVelocity();
    SimulationPostStepPipelineContext context{ m_contactAudio,
                                               m_runtimeSettings,
                                               m_timers,
                                               m_diagnosticsRuntime,
                                               SceneState(),
                                               m_debug,
                                               m_systems,
                                               m_runtimeTools,
                                               m_replayRuntime,
                                               m_replayLauncherVisualScratch,
                                               m_cWorldEnvironment,
                                               m_cGameModelCollection };
    const SimulationPostStepPipelineResult result = SimulationPostStepPipeline::Run( context );
#ifdef _DEBUG
    if ( result.replayCaptured )
    {
        // Why: WM_QUIT's code is not WinMain's final status in this app. Store
        // the probe failure on Run so Runtime/Init can return the CLI-visible
        // nonzero result after Execute() unwinds normally.
        auto handleReplayProbeResult = [this]( const SbResult& probeResult ) -> bool
        {
            if ( probeResult.ok )
            {
                return false;
            }
            RecordReplayProbeFailure( probeResult );
            PostQuitMessage( 0 );
            return true;
        };
        if ( handleReplayProbeResult( TickReplayScrubProbe() ) )
        {
            return;
        }
        if ( handleReplayProbeResult( TickReplayRestoreProbe() ) )
        {
            return;
        }
        if ( handleReplayProbeResult( TickReplaySaveProbe() ) )
        {
            return;
        }
    }
#endif
}


#ifdef _DEBUG
SbResult Run::TickReplayScrubProbe()
{
    auto distanceSquared = []( const Math::Vector::Vector3& a, const Math::Vector::Vector3& b ) -> float
    {
        const Math::Vector::Vector3 delta = a - b;
        return delta.x * delta.x + delta.y * delta.y + delta.z * delta.z;
    };

    if ( !m_replayProbes.scrub.enabled || m_replayProbes.scrub.completed )
    {
        return SbResult::Success();
    }

    const ReplayRecorderStats stats = m_replayRuntime.Presentation().GetStats();
    if ( stats.sampleCount < static_cast<std::size_t>( m_replayProbes.scrub.minSampleCount ) )
    {
        return SbResult::Success();
    }

    const ReplayPresentationSample* selected =
        m_replayRuntime.Presentation().SampleAtNormalized( m_replayProbes.scrub.normalized );
    const ReplayPresentationSample* live = m_replayRuntime.Presentation().LatestSample();
    if ( !selected || !live || selected->frameIndex >= live->frameIndex )
    {
        return ReplayProbeFailure( "replay scrub probe could not select an older replay sample" );
    }

    const ReplayBodyPresentationSample* selectedBody = nullptr;
    const ReplayBodyPresentationSample* liveBody = nullptr;
    float bestDistanceSquared = 0.0f;
    for ( const ReplayBodyPresentationSample& candidate : selected->bodies )
    {
        if ( candidate.fixed )
        {
            continue;
        }

        for ( const ReplayBodyPresentationSample& liveCandidate : live->bodies )
        {
            if ( liveCandidate.id.value != candidate.id.value )
            {
                continue;
            }

            const float candidateDistanceSquared = distanceSquared( liveCandidate.position, candidate.position );
            if ( candidateDistanceSquared > bestDistanceSquared )
            {
                bestDistanceSquared = candidateDistanceSquared;
                selectedBody = &candidate;
                liveBody = &liveCandidate;
            }
            break;
        }
    }

    if ( !selectedBody || !liveBody || bestDistanceSquared < m_replayProbes.scrub.minDistanceSquared )
    {
        return ReplayProbeFailure( "replay scrub probe did not find a moved body in the selected replay window" );
    }

    const int probedModelIndex = liveBody->modelIndex;
    const PhysicsBodyRecord* probedBody = TryGetReplayProbeBodyRecord( m_cGameModelCollection, probedModelIndex );
    if ( !probedBody )
    {
        return ReplayProbeFailure( "replay scrub probe selected an invalid live body index" );
    }

    // Why: scrub probes prove presentation overrides do not mutate live
    // simulation state. Read that state from PhysicsBodyStore so the proof does
    // not depend on temporary presentation rows.
    const Math::Vector::Vector3 preApplyPosition = probedBody->position;
    const float preLiveDeltaSquared = distanceSquared( preApplyPosition, liveBody->position );
    if ( preLiveDeltaSquared > m_replayProbes.scrub.minDistanceSquared )
    {
        return ReplayProbeFailure(
            "replay scrub probe live body did not match the current replay sample before applying scrub state" );
    }

    const bool applied =
        ApplyReplayProbePresentationSampleForRender( m_cGameModelCollection, m_replayRuntime, *selected );
    if ( !applied )
    {
        return ReplayProbeFailure( "replay scrub probe failed to apply the selected presentation sample" );
    }
    const PhysicsBodyRecord* appliedBody = TryGetReplayProbeBodyRecord( m_cGameModelCollection, probedModelIndex );
    if ( !appliedBody )
    {
        RestoreReplayProbeRenderInstances( m_cGameModelCollection );
        return ReplayProbeFailure( "replay scrub probe lost the selected live body after applying scrub state" );
    }
    const Math::Vector::Vector3 liveAfterApplyPosition = appliedBody->position;
    const float livePreservedDeltaSquared = distanceSquared( liveAfterApplyPosition, preApplyPosition );
    if ( livePreservedDeltaSquared > m_replayProbes.scrub.minDistanceSquared )
    {
        RestoreReplayProbeRenderInstances( m_cGameModelCollection );
        return ReplayProbeFailure( "replay scrub probe mutated the live body while applying scrub state" );
    }

    Math::Vector::Vector3 appliedRenderPosition;
    if ( !TryPrepareReplayProbeRenderPosition( m_cGameModelCollection, probedModelIndex, appliedRenderPosition ) )
    {
        RestoreReplayProbeRenderInstances( m_cGameModelCollection );
        return ReplayProbeFailure( "replay scrub probe lost the selected render instance after applying scrub state" );
    }
    const float appliedDeltaSquared = distanceSquared( appliedRenderPosition, selectedBody->position );
    if ( appliedDeltaSquared > m_replayProbes.scrub.minDistanceSquared )
    {
        RestoreReplayProbeRenderInstances( m_cGameModelCollection );
        return ReplayProbeFailure(
            "replay scrub probe did not move the render instance to the selected replay sample" );
    }

    RestoreReplayProbeRenderInstances( m_cGameModelCollection );
    const PhysicsBodyRecord* restoredBody = TryGetReplayProbeBodyRecord( m_cGameModelCollection, probedModelIndex );
    if ( !restoredBody )
    {
        return ReplayProbeFailure( "replay scrub probe lost the selected live body after restoring scrub state" );
    }
    const Math::Vector::Vector3 restoredPosition = restoredBody->position;
    const float restoredDeltaSquared = distanceSquared( restoredPosition, preApplyPosition );
    const bool restored = restoredDeltaSquared <= m_replayProbes.scrub.minDistanceSquared;
    if ( !restored )
    {
        return ReplayProbeFailure(
            "replay scrub probe did not restore the live model after applying the selected sample" );
    }

    m_diagnosticsRuntime.LogReplayScrubProbe( SceneState(),
                                              *selected,
                                              *live,
                                              *selectedBody,
                                              *liveBody,
                                              m_replayProbes.scrub.normalized,
                                              bestDistanceSquared,
                                              applied,
                                              restored,
                                              preLiveDeltaSquared,
                                              appliedDeltaSquared,
                                              restoredDeltaSquared );

    m_replayProbes.scrub.completed = true;
    printf(
        "[replay] Scrub probe passed: selected_replay_frame=%llu live_replay_frame=%llu body_id=%u distance_sq=%.6f\n",
        static_cast<unsigned long long>( selected->frameIndex ),
        static_cast<unsigned long long>( live->frameIndex ),
        selectedBody->id.value,
        bestDistanceSquared );
    PostQuitMessage( 0 );
    return SbResult::Success();
}

SbResult Run::TickReplayRestoreProbe()
{
    if ( !m_replayProbes.restore.enabled || m_replayProbes.restore.completed )
    {
        return SbResult::Success();
    }

    const ReplayRecorderStats stats = m_replayRuntime.Solver().GetStats();
    if ( stats.sampleCount < static_cast<std::size_t>( m_replayProbes.restore.minSampleCount ) )
    {
        return SbResult::Success();
    }

    const ReplaySolverFrameSample* selectedSample =
        m_replayRuntime.Solver().SampleAtNormalized( m_replayProbes.restore.normalized );
    const ReplaySolverFrameSample* latestSample = m_replayRuntime.Solver().LatestSample();
    if ( !selectedSample || !latestSample )
    {
        return ReplayProbeFailure( "replay restore probe could not select retained solver samples" );
    }
    if ( selectedSample->frameIndex >= latestSample->frameIndex )
    {
        return ReplayProbeFailure( "replay restore probe did not select an older solver sample" );
    }

    const ReplaySolverFrameSample selected = *selectedSample;
    const ReplayFrameIndex latestFrame = latestSample->frameIndex;
    const uint64_t selectedHash = selected.solverHash;
    char reason[160] = {};
    const bool restored = RestoreReplaySolverSampleAsLive( selected, reason, sizeof( reason ) );
    if ( !restored )
    {
        return SbResult::Failure( REPLAY_PROBE_OWNER,
                                  "replay restore probe failed: %s",
                                  reason[0] != '\0' ? reason : "unknown restore failure" );
    }

    m_replayProbes.restore.completed = true;
    printf( "[replay] Restore probe passed: target_replay_frame=%llu previous_live_replay_frame=%llu "
            "solver_hash=0x%016llX\n",
            static_cast<unsigned long long>( selected.frameIndex ),
            static_cast<unsigned long long>( latestFrame ),
            static_cast<unsigned long long>( selectedHash ) );
    PostQuitMessage( 0 );
    return SbResult::Success();
}

SbResult Run::TickReplaySaveProbe()
{
    if ( !m_replayProbes.save.enabled || m_replayProbes.save.completed )
    {
        return SbResult::Success();
    }

    const ReplayRecorderStats stats = m_replayRuntime.Presentation().GetStats();
    if ( !m_replayProbes.save.runtimeResetCoverageInjected && stats.sampleCount >= 4 )
    {
        m_replayProbes.save.runtimeResetCoverageInjected = true;
        m_replayProbes.save.eventCoverageInjected = false;
        m_runtimeCommands.Push( RuntimeCommand{ RuntimeCommandType::ResetCurrentScene } );
        return SbResult::Success();
    }

    if ( !m_replayProbes.save.eventCoverageInjected && stats.sampleCount >= 4 )
    {
        ReplaySaveProbeEventCoverageContext eventCoverageContext{ m_replayProbes.save,
                                                                  m_replayRuntime,
                                                                  m_runtimeTools,
                                                                  SceneState(),
                                                                  m_systems,
                                                                  m_cWorldEnvironment,
                                                                  m_cGameModelCollection,
                                                                  m_startup.gameModelCapacity };
        const SbResult eventCoverageResult =
            InjectReplaySaveProbeEventCoverage( eventCoverageContext, [this]() { EnterInteractiveSceneRun(); } );
        if ( !eventCoverageResult.ok )
        {
            return eventCoverageResult;
        }
    }
    if ( stats.sampleCount < static_cast<std::size_t>( m_replayProbes.save.minSampleCount ) )
    {
        return SbResult::Success();
    }

    ReplaySaveProbeArtifactContext artifactContext{ m_replayProbes.save, m_replayRuntime, m_cGameModelCollection };
    return ValidateReplaySaveProbeArtifact( artifactContext );
}

SbResult Run::VerifyLoadedReplayPresentationProbe( float normalized )
{
    auto distanceSquared = []( const Math::Vector::Vector3& a, const Math::Vector::Vector3& b ) -> float
    {
        const Math::Vector::Vector3 delta = a - b;
        return delta.x * delta.x + delta.y * delta.y + delta.z * delta.z;
    };

    if ( !m_replayRuntime.HasLoadedPresentation() )
    {
        return ReplayProbeFailure( "replay load probe requires a loaded v2 presentation artifact" );
    }

    if ( m_replayRuntime.Scrubber().liveAdvanceHeld )
    {
        m_replayRuntime.SetLiveAdvanceHeld( false );
    }
    CancelReplayToolDragState();

    m_replayRuntime.ClearCameraFocusForRestore();
    ExitReplayInspectionCamera();
    const bool armed = m_replayRuntime.ArmLoadedPresentationScrubber( std::clamp( normalized, 0.0f, 1.0f ),
                                                                      m_timers.simulationTimer.GetTotalTime() );
    if ( !armed )
    {
        return ReplayProbeFailure( "replay load probe could not arm the loaded presentation scrubber" );
    }
    SetWorldInteractionOwnerAfterInteractionTransition( WorldInteractionOwner::ReplayScrub,
                                                        InteractionExitReason::EnterReplay );
    if ( m_replayRuntime.ShouldUseInspectionCamera() )
    {
        EnterReplayInspectionCamera();
    }
    else
    {
        ExitReplayInspectionCamera();
    }
    const ReplayPresentationSample* selected = m_replayRuntime.CurrentScrubSample();
    const ReplayPresentationSample* latest = m_replayRuntime.LoadedPresentationLatestSample();
    if ( !selected || !latest )
    {
        return ReplayProbeFailure( "replay load probe could not select a loaded presentation sample" );
    }
    if ( selected->frameIndex >= latest->frameIndex )
    {
        return ReplayProbeFailure( "replay load probe did not select an older v2 presentation sample" );
    }

    const ReplayBodyPresentationSample* selectedBody = nullptr;
    const ReplayBodyPresentationSample* latestBody = nullptr;
    float bestDistanceSquared = 0.0f;
    for ( const ReplayBodyPresentationSample& candidate : selected->bodies )
    {
        for ( const ReplayBodyPresentationSample& latestCandidate : latest->bodies )
        {
            if ( latestCandidate.id.value != candidate.id.value )
            {
                continue;
            }

            const float candidateDistanceSquared = distanceSquared( latestCandidate.position, candidate.position );
            if ( candidateDistanceSquared > bestDistanceSquared )
            {
                bestDistanceSquared = candidateDistanceSquared;
                selectedBody = &candidate;
                latestBody = &latestCandidate;
            }
            break;
        }
    }
    if ( !selectedBody || !latestBody || bestDistanceSquared < 0.0001f )
    {
        return ReplayProbeFailure( "replay load probe did not find a moved body in the loaded v2 artifact" );
    }

    const int probedModelIndex = selectedBody->modelIndex;
    const PhysicsBodyRecord* probedBody = TryGetReplayProbeBodyRecord( m_cGameModelCollection, probedModelIndex );
    if ( !probedBody )
    {
        return ReplayProbeFailure( "replay load probe loaded an invalid body index" );
    }

    const Math::Vector::Vector3 preApplyPosition = probedBody->position;
    const bool applied =
        ApplyReplayProbePresentationSampleForRender( m_cGameModelCollection, m_replayRuntime, *selected );
    if ( !applied )
    {
        return ReplayProbeFailure( "replay load probe failed to apply the selected loaded v2 sample" );
    }

    const PhysicsBodyRecord* appliedBody = TryGetReplayProbeBodyRecord( m_cGameModelCollection, probedModelIndex );
    if ( !appliedBody )
    {
        RestoreReplayProbeRenderInstances( m_cGameModelCollection );
        return ReplayProbeFailure( "replay load probe lost the selected body after applying the v2 sample" );
    }
    const Math::Vector::Vector3 liveAfterApplyPosition = appliedBody->position;
    const float livePreservedDeltaSquared = distanceSquared( liveAfterApplyPosition, preApplyPosition );
    if ( livePreservedDeltaSquared > 0.0001f )
    {
        RestoreReplayProbeRenderInstances( m_cGameModelCollection );
        return ReplayProbeFailure( "replay load probe mutated the live body while applying the v2 sample" );
    }

    Math::Vector::Vector3 appliedRenderPosition;
    if ( !TryPrepareReplayProbeRenderPosition( m_cGameModelCollection, probedModelIndex, appliedRenderPosition ) )
    {
        RestoreReplayProbeRenderInstances( m_cGameModelCollection );
        return ReplayProbeFailure( "replay load probe lost the selected render instance after applying the v2 sample" );
    }
    const float appliedDeltaSquared = distanceSquared( appliedRenderPosition, selectedBody->position );
    if ( appliedDeltaSquared > 0.0001f )
    {
        RestoreReplayProbeRenderInstances( m_cGameModelCollection );
        return ReplayProbeFailure(
            "replay load probe did not move the render instance to the selected loaded v2 sample" );
    }

    RestoreReplayProbeRenderInstances( m_cGameModelCollection );
    const PhysicsBodyRecord* restoredBody = TryGetReplayProbeBodyRecord( m_cGameModelCollection, probedModelIndex );
    if ( !restoredBody )
    {
        return ReplayProbeFailure( "replay load probe lost the selected body after restoring the v2 sample" );
    }
    const Math::Vector::Vector3 restoredPosition = restoredBody->position;
    const float restoredDeltaSquared = distanceSquared( restoredPosition, preApplyPosition );
    if ( restoredDeltaSquared > 0.0001f )
    {
        return ReplayProbeFailure( "replay load probe live body changed after applying the selected loaded v2 sample" );
    }

    printf( "[replay] Load probe passed: path=%s samples=%llu bodies=%llu first_frame=%llu selected_frame=%llu "
            "latest_frame=%llu body_id=%u distance_sq=%.6f\n",
            m_replayRuntime.LoadedPresentation().path,
            static_cast<unsigned long long>( m_replayRuntime.LoadedPresentation().samples.size() ),
            static_cast<unsigned long long>( m_replayRuntime.LoadedPresentation().bodyDictionaryCount ),
            static_cast<unsigned long long>( m_replayRuntime.LoadedPresentation().firstFrame ),
            static_cast<unsigned long long>( selected->frameIndex ),
            static_cast<unsigned long long>( latest->frameIndex ),
            selectedBody->id.value,
            bestDistanceSquared );
    return SbResult::Success();
}

SbResult Run::VerifyReplaySolverCheckpointFileProbe( const char* path )
{
    if ( !path || path[0] == '\0' )
    {
        return ReplayProbeFailure( "replay restore file probe requires a v2 artifact path" );
    }

    std::vector<ReplaySolverFrameSample> checkpoints;
    ReplayV2SolverCheckpointLoadResult result;
    if ( !ReplayV2Artifact::LoadSolverCheckpoints( path, checkpoints, &result ) )
    {
        return ReplayProbeFailure( "replay restore file probe failed to load v2 solver checkpoints" );
    }
    if ( checkpoints.empty() )
    {
        return ReplayProbeFailure( "replay restore file probe found no v2 solver checkpoints" );
    }

    const ReplaySolverFrameSample& checkpoint = checkpoints.front();
    if ( checkpoint.eventCursor == 0 )
    {
        return ReplayProbeFailure( "replay restore file probe loaded a checkpoint without an event cursor" );
    }
    char reason[160] = {};
    if ( !RestoreReplaySolverSampleAsLive( checkpoint, reason, sizeof( reason ) ) )
    {
        return SbResult::Failure( REPLAY_PROBE_OWNER,
                                  "replay restore file probe failed: %s",
                                  reason[0] != '\0' ? reason : "unknown restore failure" );
    }

    printf( "[replay] Restore file probe passed: path=%s checkpoints=%llu first_frame=%llu target_frame=%llu "
            "event_cursor=%u bodies=%llu solver_hash=0x%016llX bytes=%llu\n",
            path,
            static_cast<unsigned long long>( result.checkpointCount ),
            static_cast<unsigned long long>( result.firstFrame ),
            static_cast<unsigned long long>( checkpoint.frameIndex ),
            checkpoint.eventCursor,
            static_cast<unsigned long long>( checkpoint.bodies.size() ),
            static_cast<unsigned long long>( checkpoint.solverHash ),
            static_cast<unsigned long long>( result.fileBytes ) );
    return SbResult::Success();
}
#endif

bool Run::RestoreReplayV2ArtifactTargetState( const char* path,
                                              ReplayFrameIndex requestedFrame,
                                              bool makeLiveBranch,
                                              RunReplayV2TargetRestoreResult& outResult,
                                              char* outReason,
                                              std::size_t reasonSize )
{
    outResult = RunReplayV2TargetRestoreResult();
    auto writeReason = [outReason, reasonSize]( const char* reason )
    { WriteReplayProbeReason( outReason, reasonSize, reason ); };
    constexpr ReplayFrameIndex LATEST_NON_CHECKPOINT_TARGET = ( std::numeric_limits<ReplayFrameIndex>::max )();
    const char* restoreSource = makeLiveBranch ? "v2_file_branch" : "v2_file_target";
#ifndef _DEBUG
    (void)restoreSource;
#endif
    const ReplayV2SolverHashSample* target = nullptr;
    const ReplaySolverFrameSample* checkpoint = nullptr;

    auto logRestoreDiagnostic = [&]( const char* failureReason,
                                     const ReplayV2SolverHashSample* diagnosticTarget,
                                     const ReplaySolverFrameSample* diagnosticCheckpoint,
                                     uint64_t restoredSolverHash,
                                     uint64_t restoredPresentationHash,
                                     std::size_t restoredBodyCount,
                                     bool hashCaptured,
                                     bool hashMatched,
                                     bool fallbackAttempted,
                                     bool fallbackRestored )
    {
#ifdef _DEBUG
        const ReplayFrameIndex targetFrame =
            diagnosticTarget ? diagnosticTarget->frameIndex
                             : ( requestedFrame == LATEST_NON_CHECKPOINT_TARGET ? 0 : requestedFrame );
        m_diagnosticsRuntime.LogReplayRestoreResult(
            SceneState(),
            restoreSource,
            targetFrame,
            diagnosticTarget ? diagnosticTarget->sceneFrame : SceneState().currentFrame,
            diagnosticCheckpoint ? diagnosticCheckpoint->frameIndex : 0,
            diagnosticTarget ? diagnosticTarget->solverHash : 0,
            diagnosticTarget ? diagnosticTarget->presentationHash : 0,
            diagnosticTarget ? diagnosticTarget->bodyCount : 0,
            restoredSolverHash,
            restoredPresentationHash,
            restoredBodyCount,
            diagnosticCheckpoint ? diagnosticCheckpoint->contactCount : 0,
            diagnosticCheckpoint ? diagnosticCheckpoint->pipelineRecordCount : 0,
            diagnosticCheckpoint ? diagnosticCheckpoint->checkpointBoundary : false,
            hashCaptured,
            hashMatched,
            fallbackAttempted,
            fallbackRestored,
            failureReason );
#else
        (void)failureReason;
        (void)diagnosticTarget;
        (void)diagnosticCheckpoint;
        (void)restoredSolverHash;
        (void)restoredPresentationHash;
        (void)restoredBodyCount;
        (void)hashCaptured;
        (void)hashMatched;
        (void)fallbackAttempted;
        (void)fallbackRestored;
#endif
    };

    auto failWithDiagnostic = [&]( const char* message,
                                   const ReplayV2SolverHashSample* diagnosticTarget,
                                   const ReplaySolverFrameSample* diagnosticCheckpoint,
                                   uint64_t restoredSolverHash = 0,
                                   uint64_t restoredPresentationHash = 0,
                                   std::size_t restoredBodyCount = 0,
                                   bool hashCaptured = false,
                                   bool hashMatched = false,
                                   bool fallbackAttempted = false,
                                   bool fallbackRestored = false ) -> bool
    {
        logRestoreDiagnostic( message,
                              diagnosticTarget,
                              diagnosticCheckpoint,
                              restoredSolverHash,
                              restoredPresentationHash,
                              restoredBodyCount,
                              hashCaptured,
                              hashMatched,
                              fallbackAttempted,
                              fallbackRestored );
        writeReason( message );
        return false;
    };

    auto applyReplayEventForRestoreTarget =
        [&]( const ReplayEventSample& event, char* eventOutReason, std::size_t eventReasonSize ) -> bool
    {
        if ( event.payloadVersion != 1 )
        {
            WriteReplayProbeReason( eventOutReason, eventReasonSize, "unsupported replay event payload version" );
            return false;
        }

        switch ( event.kind )
        {
        case ReplayEventKind::TimelineStart:
            WriteReplayProbeReason( eventOutReason, eventReasonSize, "ignored" );
            return true;
        case ReplayEventKind::RuntimeCommand:
        {
            const RuntimeCommandType commandType = static_cast<RuntimeCommandType>( event.value0 );
            switch ( commandType )
            {
            case RuntimeCommandType::SaveScreenshot:
            case RuntimeCommandType::SaveSceneDefaults:
            case RuntimeCommandType::SaveRenderDefaults:
            case RuntimeCommandType::SaveSkyDefaults:
            case RuntimeCommandType::Quit:
            case RuntimeCommandType::None:
                WriteReplayProbeReason( eventOutReason, eventReasonSize, "ignored non-solver runtime command" );
                return true;
            case RuntimeCommandType::ResetCurrentScene:
            case RuntimeCommandType::LoadSceneIndex:
            case RuntimeCommandType::LoadDemoScene:
            case RuntimeCommandType::CreateScene:
            case RuntimeCommandType::AdvanceScene:
            default:
                WriteReplayProbeReason( eventOutReason,
                                        eventReasonSize,
                                        "unsupported runtime timeline mutation event" );
                return false;
            }
        }
        case ReplayEventKind::BranchRestore:
            WriteReplayProbeReason( eventOutReason, eventReasonSize, "unsupported timeline mutation event" );
            return false;
        case ReplayEventKind::WorldOverride:
            if ( event.flags & REPLAY_WORLD_OVERRIDE_GRAVITY_CHANGED )
            {
                m_cWorldEnvironment.SetGravity( ReplayEventFloatFromBits( event.value0 ) );
            }
            if ( event.flags & REPLAY_WORLD_OVERRIDE_FLUID_HEIGHT_CHANGED )
            {
                m_cWorldEnvironment.SetFluidSurfaceHeight( ReplayEventFloatFromBits( event.value1 ) );
            }
            if ( event.flags & REPLAY_WORLD_OVERRIDE_FLUID_DENSITY_CHANGED )
            {
                m_cWorldEnvironment.SetFluidDensity( ReplayEventFloatFromBits( event.value2 ) );
            }
            WriteReplayProbeReason( eventOutReason, eventReasonSize, "applied world override" );
            return true;
        case ReplayEventKind::LauncherConfig:
            m_runtimeTools.RayCastTest().impulseStrength = ReplayEventFloatFromBits( event.value0 );
            m_runtimeTools.RayCastTest().projectileSpeed = ReplayEventFloatFromBits( event.value1 );
            WriteReplayProbeReason( eventOutReason, eventReasonSize, "applied launcher config" );
            return true;
        case ReplayEventKind::LauncherFire:
        {
            Vector3 rayOrigin;
            Vector3 rayDirection;
            Vector3 cameraUp;
            if ( !DecodeReplayRay9Payload( event, rayOrigin, rayDirection, cameraUp ) )
            {
                WriteReplayProbeReason( eventOutReason, eventReasonSize, "invalid launcher fire payload" );
                return false;
            }
            m_runtimeTools.RayCastTest().fireMode = ( event.flags & REPLAY_LAUNCHER_FIRE_PROJECTILE ) != 0
                                                        ? RunLauncherFireMode::Projectile
                                                        : RunLauncherFireMode::Laser;
            m_runtimeTools.RayCastTest().impulseStrength = ReplayEventFloatFromBits( event.value1 );
            m_runtimeTools.RayCastTest().projectileSpeed = ReplayEventFloatFromBits( event.value2 );
            // Why: RuntimeTools now fails closed unless Run has completed the
            // cold collection-to-store topology repair at the owner boundary.
            const bool launcherStoresReady = m_cGameModelCollection.RepairPhysicsBodyAndColliderTopology();
            if ( launcherStoresReady && m_runtimeTools.FireLauncherRay( m_cGameModelCollection,
                                                                        SceneState(),
                                                                        m_systems.terrain.get(),
                                                                        m_startup.gameModelCapacity,
                                                                        rayOrigin,
                                                                        rayDirection,
                                                                        cameraUp ) )
            {
                SceneState().modelCount = m_cGameModelCollection.SceneEntityCount();
            }
            WriteReplayProbeReason( eventOutReason, eventReasonSize, "applied launcher fire" );
            return true;
        }
        case ReplayEventKind::GeneratedSceneConfig:
            if ( SceneState().modelCount != event.value0 || SceneState().solverBallCount != event.value1 ||
                 SceneState().solverBoxCount != event.value2 ||
                 static_cast<int32_t>( SceneState().rngSeed ) != event.value3 )
            {
                WriteReplayProbeReason( eventOutReason,
                                        eventReasonSize,
                                        "generated scene config event does not match live state" );
                return false;
            }
            WriteReplayProbeReason( eventOutReason, eventReasonSize, "verified generated scene config" );
            return true;
        case ReplayEventKind::EditorPlace:
        {
            Vector3 terrainPoint;
            Vector3 placementScale;
            float placementYawRadians = 0.0f;
            if ( !DecodeReplayPlacePayload( event, terrainPoint, placementScale, placementYawRadians ) )
            {
                WriteReplayProbeReason( eventOutReason, eventReasonSize, "invalid editor placement payload" );
                return false;
            }

            const int modelCountBefore = m_cGameModelCollection.SceneEntityCount();
            if ( event.value3 != modelCountBefore )
            {
                WriteReplayProbeReason( eventOutReason,
                                        eventReasonSize,
                                        "editor placement model count precondition mismatch" );
                return false;
            }

            const Vector3 previousPlacementScale = m_runtimeTools.Editor().placementScale;
            const bool previousTerrainAlign = m_runtimeTools.Editor().autoTerrainAlign;
            const float previousPlacementYawRadians = m_runtimeTools.Editor().placementYawRadians;
            m_runtimeTools.Editor().placementScale = placementScale;
            m_runtimeTools.Editor().autoTerrainAlign = ( event.flags & REPLAY_EDITOR_PLACE_TERRAIN_ALIGN ) != 0;
            m_runtimeTools.Editor().placementYawRadians = placementYawRadians;
            EditorObjectPlacementContext placementContext{ m_runtimeTools.Editor(),
                                                           m_cGameModelCollection,
                                                           SceneState(),
                                                           m_cWorldEnvironment,
                                                           m_systems.terrain.get(),
                                                           m_systems.assets,
                                                           m_startup.gameModelCapacity };
            EditorObjectPlacementRequest placementRequest{ event.value0,
                                                           ( event.flags & REPLAY_EDITOR_PLACE_FIXED ) != 0,
                                                           terrainPoint };
            EditorObjectPlacementResult placementResult;
            bool placed = false;
            if ( CanPlaceEditorObjectAtTerrainPoint( placementContext, placementRequest ) )
            {
                EnterInteractiveSceneRun();
                placed = PlaceEditorObjectAtTerrainPoint( placementContext, placementRequest, placementResult );
            }
            m_runtimeTools.Editor().placementScale = previousPlacementScale;
            m_runtimeTools.Editor().autoTerrainAlign = previousTerrainAlign;
            m_runtimeTools.Editor().placementYawRadians = previousPlacementYawRadians;
            if ( !placed )
            {
                WriteReplayProbeReason( eventOutReason, eventReasonSize, "failed to replay editor placement" );
                return false;
            }
            WriteReplayProbeReason( eventOutReason, eventReasonSize, "applied editor placement" );
            return true;
        }
        case ReplayEventKind::EditorTransform:
        {
            if ( event.flags == 0 || ( event.flags & ~REPLAY_EDITOR_TRANSFORM_SUPPORTED ) != 0 )
            {
                WriteReplayProbeReason( eventOutReason, eventReasonSize, "unsupported editor transform flags" );
                return false;
            }

            Vector3 position;
            Quaternion orientation;
            float scaleFactor = 1.0f;
            bool hasScaleFactor = false;
            if ( !DecodeReplayTransformPayload( event, position, orientation, scaleFactor, hasScaleFactor ) )
            {
                WriteReplayProbeReason( eventOutReason, eventReasonSize, "invalid editor transform payload" );
                return false;
            }
            if ( ( event.flags & REPLAY_EDITOR_TRANSFORM_SCALE ) != 0 &&
                 ( !hasScaleFactor || event.value3 < 0 || event.value3 > 2 || !std::isfinite( scaleFactor ) ||
                   scaleFactor <= 0.0f ) )
            {
                WriteReplayProbeReason( eventOutReason, eventReasonSize, "invalid editor transform scale payload" );
                return false;
            }

            if ( event.value2 != m_cGameModelCollection.SceneEntityCount() )
            {
                WriteReplayProbeReason( eventOutReason,
                                        eventReasonSize,
                                        "editor transform model count precondition mismatch" );
                return false;
            }
            if ( event.value0 < 0 || event.value0 >= m_cGameModelCollection.SceneEntityCount() )
            {
                WriteReplayProbeReason( eventOutReason,
                                        eventReasonSize,
                                        "editor transform model index is out of range" );
                return false;
            }

            const PhysicsBodyStore& bodyStoreBeforeEdit = m_cGameModelCollection.GetPhysicsEngine().BodyStore();
            const PhysicsBodyHandle eventBody =
                bodyStoreBeforeEdit.HandleForReplayBodyId( static_cast<uint32_t>( event.value1 ), event.value0 );
            const PhysicsBodyRecord* eventBodyRecord = bodyStoreBeforeEdit.RecordForHandle( eventBody );
            if ( !eventBodyRecord || bodyStoreBeforeEdit.ModelIndexForHandle( eventBody ) != event.value0 ||
                 eventBodyRecord->replayBodyId != static_cast<uint32_t>( event.value1 ) )
            {
                WriteReplayProbeReason( eventOutReason, eventReasonSize, "editor transform replay body id mismatch" );
                return false;
            }

            SkullbonezCore::GameObjects::PhysicsBodyStateEdit bodyEdit;
            if ( event.flags & REPLAY_EDITOR_TRANSFORM_TRANSLATE )
            {
                bodyEdit.hasPosition = true;
                bodyEdit.position = position;
            }
            if ( event.flags & REPLAY_EDITOR_TRANSFORM_ROTATE )
            {
                bodyEdit.hasOrientation = true;
                bodyEdit.orientation = orientation;
            }
            PhysicsColliderCreateDesc editedColliderDesc;
            bool hasEditedColliderDesc = false;
            if ( event.flags & REPLAY_EDITOR_TRANSFORM_SCALE )
            {
                const ColliderRecord* colliderBeforeScale =
                    TryGetEditorTransformColliderRecord( m_cGameModelCollection,
                                                         PhysicsColliderHandle{},
                                                         event.value0,
                                                         eventBodyRecord->replayBodyId );
                if ( !colliderBeforeScale )
                {
                    WriteReplayProbeReason( eventOutReason, eventReasonSize, "editor transform collider row missing" );
                    return false;
                }
                const CollisionShape baseShape = colliderBeforeScale->shape;
                CollisionShape scaledShape;
                if ( !ScaleShapeAxisFromBase( baseShape, event.value3, scaleFactor, scaledShape ) )
                {
                    WriteReplayProbeReason( eventOutReason,
                                            eventReasonSize,
                                            "failed to replay editor transform scale" );
                    return false;
                }
                // Invariant: restore reuses the previous collider material and
                // replaces only the decoded scale shape, keeping replay payload
                // semantics independent from legacy model-side recapture.
                editedColliderDesc = MakeColliderCreateDesc( std::move( scaledShape ),
                                                             colliderBeforeScale->restitution,
                                                             colliderBeforeScale->contactMaterialId );
                hasEditedColliderDesc = true;
            }
            bodyEdit.hasLinearVelocity = true;
            bodyEdit.linearVelocity = Vector3( 0.0f, 0.0f, 0.0f );
            bodyEdit.hasAngularVelocity = true;
            bodyEdit.angularVelocity = Vector3( 0.0f, 0.0f, 0.0f );
            if ( hasEditedColliderDesc )
            {
                m_cGameModelCollection.ApplyPhysicsBodyColliderEdit( event.value0,
                                                                     bodyEdit,
                                                                     std::move( editedColliderDesc ) );
            }
            else
            {
                m_cGameModelCollection.ApplyPhysicsBodyEdit( event.value0, bodyEdit );
            }
            // Why: the edited-state commit has already refreshed the
            // edited body row. The wake decision should read the committed
            // PhysicsBodyStore record, not presentation/authored pose data.
            PhysicsEngine& physics = m_cGameModelCollection.GetPhysicsEngine();
            const PhysicsBodyStore& bodyStore = physics.BodyStore();
            const PhysicsBodyHandle body =
                bodyStore.HandleForReplayBodyId( static_cast<uint32_t>( event.value1 ), event.value0 );
            const PhysicsBodyRecord* bodyRecord = bodyStore.RecordForHandle( body );
            if ( bodyRecord && !bodyRecord->isFixed )
            {
                physics.WakeBody( body );
            }
            WriteReplayProbeReason( eventOutReason, eventReasonSize, "applied editor transform" );
            return true;
        }
        default:
            WriteReplayProbeReason( eventOutReason, eventReasonSize, "unsupported replay event kind" );
            return false;
        }
    };

    if ( !path || path[0] == '\0' )
    {
        return failWithDiagnostic( "replay v2 target restore requires a v2 artifact path", target, checkpoint );
    }

    std::vector<ReplaySolverFrameSample> checkpoints;
    ReplayV2SolverCheckpointLoadResult checkpointResult;
    if ( !ReplayV2Artifact::LoadSolverCheckpoints( path, checkpoints, &checkpointResult ) )
    {
        return failWithDiagnostic( "failed to load v2 solver checkpoints", target, checkpoint );
    }

    std::vector<ReplayV2SolverHashSample> hashes;
    ReplayV2SolverHashLoadResult hashResult;
    if ( !ReplayV2Artifact::LoadSolverHashes( path, hashes, &hashResult ) )
    {
        return failWithDiagnostic( "failed to load v2 solver hashes", target, checkpoint );
    }

    std::vector<ReplayEventSample> events;
    ReplayV2EventLoadResult eventResult;
    if ( !ReplayV2Artifact::LoadEvents( path, events, &eventResult ) )
    {
        return failWithDiagnostic( "failed to load v2 events", target, checkpoint );
    }

    std::vector<ReplayPresentationSample> presentationSamples;
    ReplayV2LoadResult presentationResult;
    if ( !ReplayV2Artifact::LoadPresentation( path, presentationSamples, &presentationResult ) )
    {
        return failWithDiagnostic( "failed to load v2 presentation frames", target, checkpoint );
    }

    if ( requestedFrame == LATEST_NON_CHECKPOINT_TARGET )
    {
        for ( auto it = hashes.rbegin(); it != hashes.rend(); ++it )
        {
            if ( !it->checkpointBoundary )
            {
                target = &*it;
                break;
            }
        }
        if ( !target )
        {
            return failWithDiagnostic( "found no saved non-checkpoint target hash", target, checkpoint );
        }
    }
    else
    {
        for ( const ReplayV2SolverHashSample& hash : hashes )
        {
            if ( hash.frameIndex == requestedFrame )
            {
                target = &hash;
                break;
            }
        }
        if ( !target )
        {
            char message[192] = {};
            sprintf_s( message,
                       sizeof( message ),
                       "found no saved hash for requested target frame %llu",
                       static_cast<unsigned long long>( requestedFrame ) );
            return failWithDiagnostic( message, target, checkpoint );
        }
    }

    for ( const ReplaySolverFrameSample& candidate : checkpoints )
    {
        if ( candidate.frameIndex <= target->frameIndex &&
             ( !checkpoint || candidate.frameIndex > checkpoint->frameIndex ) )
        {
            checkpoint = &candidate;
        }
    }
    if ( !checkpoint )
    {
        return failWithDiagnostic( "found no checkpoint before target hash", target, checkpoint );
    }
    if ( checkpoint->frameIndex > target->frameIndex )
    {
        return failWithDiagnostic( "selected checkpoint after target frame", target, checkpoint );
    }
    if ( checkpoint->eventCursor == 0 )
    {
        return failWithDiagnostic( "loaded a checkpoint without an event cursor", target, checkpoint );
    }
    if ( target->frameIndex - checkpoint->frameIndex >
         static_cast<ReplayFrameIndex>( hashes.size() + events.size() + 1u ) )
    {
        return failWithDiagnostic( "selected an implausibly distant target frame", target, checkpoint );
    }

    ReplaySolverFrameSample liveBackup;
    bool hasLiveBackup = false;
    if ( const ReplaySolverFrameSample* latest = m_replayRuntime.Solver().LatestSample() )
    {
        liveBackup = *latest;
        hasLiveBackup = true;
    }
    bool stateMutated = false;

    auto checkpointTopologyMatchesLive = [&]() -> bool
    {
        const int liveModelCount = m_cGameModelCollection.SceneEntityCount();
        if ( checkpoint->bodies.size() > static_cast<std::size_t>( liveModelCount ) )
        {
            return false;
        }
        for ( const ReplaySolverBodySample& body : checkpoint->bodies )
        {
            if ( body.modelIndex < 0 || body.modelIndex >= liveModelCount )
            {
                return false;
            }
            const PhysicsBodyRecord* bodyRecord =
                TryGetReplayProbeBodyRecord( m_cGameModelCollection, body.modelIndex );
            if ( !bodyRecord || bodyRecord->replayBodyId != body.id.value )
            {
                return false;
            }
        }
        return true;
    };

    auto latestGeneratedSceneConfigBeforeCheckpoint = [&]() -> const ReplayEventSample*
    {
        const ReplayEventSample* generatedConfig = nullptr;
        for ( const ReplayEventSample& event : events )
        {
            if ( event.kind != ReplayEventKind::GeneratedSceneConfig || event.frameIndex > checkpoint->frameIndex ||
                 event.sequence >= checkpoint->eventCursor )
            {
                continue;
            }
            if ( event.branch.branchId != checkpoint->branch.branchId )
            {
                continue;
            }
            generatedConfig = &event;
        }
        return generatedConfig;
    };

    auto rebuildGeneratedSceneTopology =
        [&]( const ReplayEventSample& event, char* rebuildReason, std::size_t rebuildReasonSize ) -> bool
    {
        if ( event.value0 < 0 || event.value1 < 0 || event.value2 < 0 || event.value3 <= 0 )
        {
            WriteReplayProbeReason( rebuildReason,
                                    rebuildReasonSize,
                                    "generated scene config contains invalid counts" );
            return false;
        }

        const uint32_t overrideBits =
            ( event.flags & REPLAY_GENERATED_SCENE_OVERRIDE_MASK ) >> REPLAY_GENERATED_SCENE_OVERRIDE_SHIFT;
        if ( overrideBits > static_cast<uint32_t>( GeneratedObjectTypeOverride::AllBoxes ) )
        {
            WriteReplayProbeReason( rebuildReason,
                                    rebuildReasonSize,
                                    "generated scene config has invalid override bits" );
            return false;
        }

        const bool exactSolverCounts = ( event.flags & REPLAY_GENERATED_SCENE_EXACT_SOLVER_COUNTS ) != 0;
        const bool uiSolverCounts = ( event.flags & REPLAY_GENERATED_SCENE_UI_SOLVER_COUNTS ) != 0;
        const bool uiModelCount = ( event.flags & REPLAY_GENERATED_SCENE_UI_MODEL_COUNT ) != 0;
        if ( exactSolverCounts && event.value1 + event.value2 != event.value0 )
        {
            WriteReplayProbeReason( rebuildReason,
                                    rebuildReasonSize,
                                    "generated solver counts do not match model count" );
            return false;
        }
        if ( event.value0 > m_startup.gameModelCapacity )
        {
            WriteReplayProbeReason( rebuildReason,
                                    rebuildReasonSize,
                                    "generated scene model count exceeds active capacity" );
            return false;
        }

        m_cGameModelCollection.Clear();
        m_runtimeTools.ClearRayCastTestLines();
        m_simulation.Reset();
        SceneState().rngSeed = static_cast<unsigned int>( event.value3 );
        SceneState().rngState = static_cast<unsigned int>( event.value3 );
        m_launchOptions.generatedObjectTypeOverride = static_cast<GeneratedObjectTypeOverride>( overrideBits );
        m_sceneController.UIOverrides().modelCountOverride = uiModelCount ? event.value0 : -1;
        m_sceneController.UIOverrides().solverBallCountOverride =
            uiSolverCounts || exactSolverCounts ? event.value1 : -1;
        m_sceneController.UIOverrides().solverBoxCountOverride =
            uiSolverCounts || exactSolverCounts ? event.value2 : -1;

        if ( exactSolverCounts || uiSolverCounts )
        {
            const SbResult setupResult = SceneGeneratedSetup::SetUpSolverObjects(
                BuildSceneGeneratedModelContext( SceneState(),
                                                 m_config,
                                                 m_cWorldEnvironment,
                                                 m_systems.terrain.get(),
                                                 m_cGameModelCollection,
                                                 m_cGameModelCollection.GetPhysicsEngine(),
                                                 m_launchOptions.generatedObjectTypeOverride ),
                event.value1,
                event.value2 );
            if ( !setupResult.ok )
            {
                WriteReplayProbeReason( rebuildReason, rebuildReasonSize, setupResult.error.message );
                return false;
            }
        }
        else
        {
            const SbResult setupResult = SceneGeneratedSetup::SetUpGameModels(
                BuildSceneGeneratedModelContext( SceneState(),
                                                 m_config,
                                                 m_cWorldEnvironment,
                                                 m_systems.terrain.get(),
                                                 m_cGameModelCollection,
                                                 m_cGameModelCollection.GetPhysicsEngine(),
                                                 m_launchOptions.generatedObjectTypeOverride ),
                event.value0 );
            if ( !setupResult.ok )
            {
                WriteReplayProbeReason( rebuildReason, rebuildReasonSize, setupResult.error.message );
                return false;
            }
        }
        if ( !checkpointTopologyMatchesLive() )
        {
            WriteReplayProbeReason( rebuildReason,
                                    rebuildReasonSize,
                                    "rebuilt generated topology still mismatches checkpoint" );
            return false;
        }
        WriteReplayProbeReason( rebuildReason, rebuildReasonSize, "rebuilt generated topology" );
        return true;
    };

    auto failAfterMutation = [&]( const char* message,
                                  const ReplayV2SolverHashSample* diagnosticTarget,
                                  uint64_t restoredSolverHash = 0,
                                  uint64_t restoredPresentationHash = 0,
                                  std::size_t restoredBodyCount = 0,
                                  bool hashCaptured = false,
                                  bool hashMatched = false ) -> bool
    {
        bool fallbackRestored = false;
        if ( stateMutated && hasLiveBackup )
        {
            char fallbackReason[128] = {};
            fallbackRestored = ApplyReplaySolverSampleState( liveBackup, fallbackReason, sizeof( fallbackReason ) );
        }
        return failWithDiagnostic( message,
                                   diagnosticTarget,
                                   checkpoint,
                                   restoredSolverHash,
                                   restoredPresentationHash,
                                   restoredBodyCount,
                                   hashCaptured,
                                   hashMatched,
                                   stateMutated && hasLiveBackup,
                                   fallbackRestored );
    };

    bool generatedTopologyRebuilt = false;
    if ( !checkpointTopologyMatchesLive() )
    {
        const ReplayEventSample* generatedConfig = latestGeneratedSceneConfigBeforeCheckpoint();
        if ( !generatedConfig )
        {
            return failAfterMutation( "checkpoint topology does not match live scene and no generated config was saved",
                                      target );
        }

        char rebuildReason[160] = {};
        stateMutated = true;
        if ( !rebuildGeneratedSceneTopology( *generatedConfig, rebuildReason, sizeof( rebuildReason ) ) )
        {
            char message[320] = {};
            sprintf_s( message,
                       sizeof( message ),
                       "failed to rebuild generated scene topology: %s",
                       rebuildReason[0] != '\0' ? rebuildReason : "unknown rebuild failure" );
            return failAfterMutation( message, target );
        }
        generatedTopologyRebuilt = true;
    }

    char reason[192] = {};
    if ( !ApplyReplaySolverSampleState( *checkpoint, reason, sizeof( reason ) ) )
    {
        char message[288] = {};
        sprintf_s( message,
                   sizeof( message ),
                   "failed to apply checkpoint: %s",
                   reason[0] != '\0' ? reason : "unknown restore failure" );
        return failWithDiagnostic( message, target, checkpoint );
    }
    stateMutated = true;

    ReplayFrameIndex currentFrame = checkpoint->frameIndex;
    int currentSceneFrame = checkpoint->sceneFrame;
    uint32_t eventCursor = checkpoint->eventCursor;
    std::size_t eventsApplied = 0;
    std::size_t unsupportedEvents = 0;
    SceneState().currentFrame = currentSceneFrame;

    {
        ScopedReplayProbeProfilerFrame profilerFrame;
        while ( currentFrame < target->frameIndex )
        {
            const ReplayFrameIndex nextFrame = currentFrame + 1u;

            for ( const ReplayEventSample& event : events )
            {
                if ( event.frameIndex != nextFrame || event.sequence < eventCursor )
                {
                    continue;
                }
                if ( event.branch.branchId != checkpoint->branch.branchId )
                {
                    ++unsupportedEvents;
                    continue;
                }

                char eventReason[160] = {};
                if ( !applyReplayEventForRestoreTarget( event, eventReason, sizeof( eventReason ) ) )
                {
                    char message[320] = {};
                    sprintf_s( message,
                               sizeof( message ),
                               "replay restore target probe failed to apply event sequence %u at frame %llu: %s",
                               event.sequence,
                               static_cast<unsigned long long>( event.frameIndex ),
                               eventReason[0] != '\0' ? eventReason : "unknown event replay failure" );
                    return failAfterMutation( message, target );
                }
                eventCursor = (std::max)( eventCursor, event.sequence + 1u );
                ++eventsApplied;
            }

            m_runtimeTools.TickRayCastTestLines( PHYSICS_FIXED_DT );
            m_runtimeTools.Laser().Update( PHYSICS_FIXED_DT );
            m_cGameModelCollection.EndCollisionVisualFrame();
            ++currentSceneFrame;
            SceneState().currentFrame = currentSceneFrame;
            m_cGameModelCollection.BeginCollisionVisualFrame();

            const auto physicsWorldForces = m_cWorldEnvironment.GetPhysicsWorldForces();
            StepRuntimePhysicsTick( m_cGameModelCollection,
                                    PHYSICS_FIXED_DT,
                                    *m_systems.config,
                                    physicsWorldForces,
                                    *m_systems.workerPool );
            currentFrame = nextFrame;

            const ReplayV2SolverHashSample* expectedHash = FindReplaySolverHashForFrame( hashes, currentFrame );
            if ( !expectedHash )
            {
                return failAfterMutation( "could not find stepped hash metadata", target );
            }

            ReplaySolverFrameSample stepReference;
            stepReference.frameIndex = expectedHash->frameIndex;
            stepReference.branch = checkpoint->branch;
            stepReference.eventCursor = eventCursor;
            stepReference.sceneFrame = expectedHash->sceneFrame;
            stepReference.simulationSeconds = expectedHash->simulationSeconds;
            stepReference.physicsDt = PHYSICS_FIXED_DT;

            uint64_t stepSolverHash = 0;
            uint64_t stepPresentationHash = 0;
            std::size_t stepBodyCount = 0;
            if ( !CaptureCurrentReplaySolverHash( stepReference, stepSolverHash, stepPresentationHash, stepBodyCount ) )
            {
                return failAfterMutation( "failed to capture stepped hash", expectedHash );
            }
            if ( stepBodyCount != expectedHash->bodyCount || stepSolverHash != expectedHash->solverHash )
            {
                char message[1024] = {};
                const ReplayPresentationSample* expectedPresentation =
                    FindReplayPresentationForFrame( presentationSamples, currentFrame );
                const PhysicsBodyRecord* restoredBody = TryGetReplayProbeBodyRecord( m_cGameModelCollection, 0 );
                if ( expectedPresentation && !expectedPresentation->bodies.empty() && restoredBody )
                {
                    const ReplayBodyPresentationSample& expectedBody = expectedPresentation->bodies[0];
                    const Vector3& restoredPosition = restoredBody->position;
                    const Vector3& restoredVelocity = restoredBody->linearVelocity;
                    float restoredQx = 0.0f;
                    float restoredQy = 0.0f;
                    float restoredQz = 0.0f;
                    float restoredQw = 1.0f;
                    restoredBody->orientation.GetComponents( restoredQx, restoredQy, restoredQz, restoredQw );

                    sprintf_s( message,
                               sizeof( message ),
                               "replay restore target probe diverged at frame %llu: restored=0x%016llX "
                               "expected=0x%016llX restored_presentation=0x%016llX expected_presentation=0x%016llX "
                               "restored_pos=(%.6f,%.6f,%.6f) expected_pos=(%.6f,%.6f,%.6f) "
                               "restored_vel=(%.6f,%.6f,%.6f) restored_q=(%.6f,%.6f,%.6f,%.6f) "
                               "expected_q=(%.6f,%.6f,%.6f,%.6f) restored_body_id=%u expected_body_id=%u "
                               "events_applied=%llu",
                               static_cast<unsigned long long>( currentFrame ),
                               static_cast<unsigned long long>( stepSolverHash ),
                               static_cast<unsigned long long>( expectedHash->solverHash ),
                               static_cast<unsigned long long>( stepPresentationHash ),
                               static_cast<unsigned long long>( expectedHash->presentationHash ),
                               restoredPosition.x,
                               restoredPosition.y,
                               restoredPosition.z,
                               expectedBody.position.x,
                               expectedBody.position.y,
                               expectedBody.position.z,
                               restoredVelocity.x,
                               restoredVelocity.y,
                               restoredVelocity.z,
                               restoredQx,
                               restoredQy,
                               restoredQz,
                               restoredQw,
                               expectedBody.orientation[0],
                               expectedBody.orientation[1],
                               expectedBody.orientation[2],
                               expectedBody.orientation[3],
                               restoredBody->replayBodyId,
                               expectedBody.id.value,
                               static_cast<unsigned long long>( eventsApplied ) );
                }
                else
                {
                    sprintf_s( message,
                               sizeof( message ),
                               "replay restore target probe diverged at frame %llu: restored=0x%016llX "
                               "expected=0x%016llX restored_presentation=0x%016llX expected_presentation=0x%016llX "
                               "restored_bodies=%llu expected_bodies=%u events_applied=%llu",
                               static_cast<unsigned long long>( currentFrame ),
                               static_cast<unsigned long long>( stepSolverHash ),
                               static_cast<unsigned long long>( expectedHash->solverHash ),
                               static_cast<unsigned long long>( stepPresentationHash ),
                               static_cast<unsigned long long>( expectedHash->presentationHash ),
                               static_cast<unsigned long long>( stepBodyCount ),
                               expectedHash->bodyCount,
                               static_cast<unsigned long long>( eventsApplied ) );
                }
                return failAfterMutation( message,
                                          expectedHash,
                                          stepSolverHash,
                                          stepPresentationHash,
                                          stepBodyCount,
                                          true );
            }
        }
    }

    if ( unsupportedEvents != 0 )
    {
        return failAfterMutation( "encountered unsupported branch events before target", target );
    }

    ReplaySolverFrameSample reference;
    reference.frameIndex = target->frameIndex;
    reference.branch = checkpoint->branch;
    reference.eventCursor = eventCursor;
    reference.sceneFrame = target->sceneFrame;
    reference.simulationSeconds = target->simulationSeconds;
    reference.physicsDt = PHYSICS_FIXED_DT;

    uint64_t restoredSolverHash = 0;
    uint64_t restoredPresentationHash = 0;
    std::size_t restoredBodyCount = 0;
    if ( !CaptureCurrentReplaySolverHash( reference, restoredSolverHash, restoredPresentationHash, restoredBodyCount ) )
    {
        return failAfterMutation( "failed to capture target hash", target );
    }
    if ( restoredBodyCount != target->bodyCount )
    {
        char message[256] = {};
        sprintf_s( message,
                   sizeof( message ),
                   "replay restore target probe body count mismatch: restored=%llu expected=%u",
                   static_cast<unsigned long long>( restoredBodyCount ),
                   target->bodyCount );
        return failAfterMutation( message,
                                  target,
                                  restoredSolverHash,
                                  restoredPresentationHash,
                                  restoredBodyCount,
                                  true );
    }
    if ( restoredSolverHash != target->solverHash )
    {
        char message[320] = {};
        sprintf_s( message,
                   sizeof( message ),
                   "replay restore target probe solver hash mismatch: restored=0x%016llX expected=0x%016llX",
                   static_cast<unsigned long long>( restoredSolverHash ),
                   static_cast<unsigned long long>( target->solverHash ) );
        return failAfterMutation( message,
                                  target,
                                  restoredSolverHash,
                                  restoredPresentationHash,
                                  restoredBodyCount,
                                  true );
    }

    outResult.checkpointCount = checkpointResult.checkpointCount;
    outResult.eventCount = eventResult.eventCount;
    outResult.hashCount = hashResult.hashCount;
    outResult.eventsApplied = eventsApplied;
    outResult.bodyCount = restoredBodyCount;
    outResult.fileBytes = hashResult.fileBytes;
    outResult.checkpointFrame = checkpoint->frameIndex;
    outResult.targetFrame = target->frameIndex;
    outResult.eventCursor = eventCursor;
    outResult.solverHash = restoredSolverHash;
    outResult.presentationHash = restoredPresentationHash;
    outResult.generatedTopologyRebuilt = generatedTopologyRebuilt;

    logRestoreDiagnostic( "",
                          target,
                          checkpoint,
                          restoredSolverHash,
                          restoredPresentationHash,
                          restoredBodyCount,
                          true,
                          true,
                          false,
                          false );

    if ( makeLiveBranch )
    {
        const uint32_t parentBranchId =
            checkpoint->branch.branchId != 0
                ? checkpoint->branch.branchId
                : ( m_replayRuntime.Branch().branchId != 0 ? m_replayRuntime.Branch().branchId : 1u );
        ReplayBranchInfo restoredBranch;
        restoredBranch.branchId = (std::max)( m_replayRuntime.Branch().branchId, parentBranchId ) + 1u;
        restoredBranch.parentBranchId = parentBranchId;
        restoredBranch.startFrame = 0;
        restoredBranch.sourceFrame = target->frameIndex;
        restoredBranch.sourceSolverHash = target->solverHash;
        m_replayRuntime.Branch() = restoredBranch;
        ResetReplayTimelineForActiveScene( true );
        m_replayRuntime.RecordEvent( ReplayEventKind::BranchRestore,
                                     0,
                                     0,
                                     static_cast<int32_t>( parentBranchId ),
                                     target->sceneFrame,
                                     0,
                                     0,
                                     target->solverHash,
                                     "hash-verified v2 file restore" );
        outResult.branchId = restoredBranch.branchId;
        outResult.parentBranchId = parentBranchId;
        outResult.madeLiveBranch = true;
    }

    writeReason( "restored hash match" );
    return true;
}

#ifdef _DEBUG
SbResult Run::VerifyReplaySolverTargetFileProbe( const char* path )
{
    RunReplayV2TargetRestoreResult result;
    char reason[256] = {};
    if ( !RestoreReplayV2ArtifactTargetState( path,
                                              ( std::numeric_limits<ReplayFrameIndex>::max )(),
                                              false,
                                              result,
                                              reason,
                                              sizeof( reason ) ) )
    {
        return SbResult::Failure( REPLAY_PROBE_OWNER,
                                  "replay restore target probe failed: %s",
                                  reason[0] != '\0' ? reason : "unknown restore failure" );
    }

    printf( "[replay] Restore target probe passed: path=%s checkpoints=%llu events=%llu hashes=%llu "
            "checkpoint_frame=%llu target_frame=%llu event_cursor=%u events_applied=%llu bodies=%llu "
            "generated_topology_rebuilt=%d "
            "solver_hash=0x%016llX presentation_hash=0x%016llX bytes=%llu\n",
            path,
            static_cast<unsigned long long>( result.checkpointCount ),
            static_cast<unsigned long long>( result.eventCount ),
            static_cast<unsigned long long>( result.hashCount ),
            static_cast<unsigned long long>( result.checkpointFrame ),
            static_cast<unsigned long long>( result.targetFrame ),
            result.eventCursor,
            static_cast<unsigned long long>( result.eventsApplied ),
            static_cast<unsigned long long>( result.bodyCount ),
            result.generatedTopologyRebuilt ? 1 : 0,
            static_cast<unsigned long long>( result.solverHash ),
            static_cast<unsigned long long>( result.presentationHash ),
            static_cast<unsigned long long>( result.fileBytes ) );
    PostQuitMessage( 0 );
    return SbResult::Success();
}

SbResult Run::VerifyReplaySolverFailureFileProbe( const char* path )
{
    constexpr ReplayFrameIndex MISSING_TARGET_FRAME = 999999999u;
    RunReplayV2TargetRestoreResult result;
    char reason[256] = {};
    if ( RestoreReplayV2ArtifactTargetState( path, MISSING_TARGET_FRAME, false, result, reason, sizeof( reason ) ) )
    {
        return ReplayProbeFailure( "replay restore failure probe unexpectedly restored a missing target frame" );
    }
    if ( strstr( reason, "found no saved hash for requested target frame" ) == nullptr )
    {
        return SbResult::Failure( REPLAY_PROBE_OWNER,
                                  "replay restore failure probe produced an unexpected reason: %s",
                                  reason[0] != '\0' ? reason : "unknown restore failure" );
    }

    printf( "[replay] Restore failure probe passed: path=%s missing_frame=%llu reason=\"%s\"\n",
            path,
            static_cast<unsigned long long>( MISSING_TARGET_FRAME ),
            reason );
    return SbResult::Success();
}

SbResult Run::VerifyReplaySolverBranchFileProbe( const char* path )
{
    if ( !LoadReplayPresentationArtifact( path, true ) )
    {
        return ReplayProbeFailure( "replay restore branch probe failed to load v2 presentation scrub source" );
    }
    m_replayRuntime.Scrubber().historicalSamplePaused = true;
    m_replayRuntime.Scrubber().activeTrack = RunReplayTrack::Presentation;
    m_replayRuntime.SetTrackPosition( RunReplayTrack::Presentation, 1.0f );

    RunReplayV2TargetRestoreResult result;
    char reason[256] = {};
    if ( !RestoreReplayScrubberSelectionAsLive( m_timers.simulationTimer.GetTotalTime(),
                                                &result,
                                                reason,
                                                sizeof( reason ) ) )
    {
        return SbResult::Failure( REPLAY_PROBE_OWNER,
                                  "replay restore branch probe failed: %s",
                                  reason[0] != '\0' ? reason : "unknown restore failure" );
    }
    if ( !result.madeLiveBranch || result.branchId == 0 )
    {
        return ReplayProbeFailure( "replay restore branch probe did not create a scrubber live branch" );
    }

    printf( "[replay] Restore branch probe passed: path=%s checkpoints=%llu events=%llu hashes=%llu "
            "checkpoint_frame=%llu target_frame=%llu event_cursor=%u events_applied=%llu bodies=%llu "
            "generated_topology_rebuilt=%d "
            "branch_id=%u parent_branch_id=%u solver_hash=0x%016llX presentation_hash=0x%016llX bytes=%llu\n",
            path,
            static_cast<unsigned long long>( result.checkpointCount ),
            static_cast<unsigned long long>( result.eventCount ),
            static_cast<unsigned long long>( result.hashCount ),
            static_cast<unsigned long long>( result.checkpointFrame ),
            static_cast<unsigned long long>( result.targetFrame ),
            result.eventCursor,
            static_cast<unsigned long long>( result.eventsApplied ),
            static_cast<unsigned long long>( result.bodyCount ),
            result.generatedTopologyRebuilt ? 1 : 0,
            result.branchId,
            result.parentBranchId,
            static_cast<unsigned long long>( result.solverHash ),
            static_cast<unsigned long long>( result.presentationHash ),
            static_cast<unsigned long long>( result.fileBytes ) );
    return SbResult::Success();
}
#endif


void Run::EnterInteractiveSceneRun()
{
    SceneState().isInteractiveRun = true;
    SceneState().isExitOnComplete = false;
    m_diagnosticsRuntime.Capture().Screenshot().isScreenshotAndExit = false;
}


bool Run::CanSceneAutomationQuit() const
{
    return !SceneState().isInteractiveRun;
}


void Run::HoldCompletedInteractiveScene()
{
    SceneState().isTestComplete = true;
    SceneState().isExitOnComplete = false;
    m_diagnosticsRuntime.Capture().Screenshot().isScreenshotAndExit = false;
    m_camera.autoCycleInterval = -1.0f;
    m_camera.autoCycleAccum = 0.0f;
}


bool Run::TickScreenshots()
{
    PROFILE_BEGIN( "Frame/PostDraw/Screenshots" );
    if ( m_debug.isCrossScenePauseLocked && !Input::IsKeyDown( VK_SPACE ) )
    {
        PROFILE_END( "Frame/PostDraw/Screenshots" );
        return false;
    }

    const RuntimeCaptureSink sink{ this,
                                   []( void* context, const char* path )
                                   { static_cast<Run*>( context )->SaveScreenshot( path ); } };
    const std::string* scenePath = m_sceneController.CurrentPath();
    const RuntimeCaptureResult result = m_diagnosticsRuntime.Capture().TickScreenshots(
        RuntimeCaptureSceneContext{ SceneState().isSceneMode,
                                    SceneState().isInteractiveRun,
                                    SceneState().currentFrame,
                                    m_timers.simulationTimer.GetTimeSinceLastStart() * 1000.0,
                                    scenePath ? scenePath->c_str() : nullptr },
        sink );

    PROFILE_END( "Frame/PostDraw/Screenshots" );

    if ( result.restartFrame )
    {
        PROFILE_FRAME_END();
    }

#ifdef _DEBUG
    if ( result.completion == RuntimeCaptureCompletion::ScreenshotAndExit )
    {
        LogSceneFinished( "screenshot_and_exit" );
    }
    else if ( result.completion == RuntimeCaptureCompletion::Screenshot )
    {
        LogSceneFinished( "screenshot" );
    }
#endif

    switch ( result.automation )
    {
    case RuntimeCaptureAutomation::Quit:
        if ( result.completion == RuntimeCaptureCompletion::ScreenshotAndExit )
        {
            PrintRuntimeExitReason( "Exiting because screenshot-and-exit capture completed." );
        }
        else if ( result.completion == RuntimeCaptureCompletion::AutoCycle )
        {
            PrintRuntimeExitReason( "Exiting because auto-cycle screenshot capture completed." );
        }
        PostQuitMessage( 0 );
        break;
    case RuntimeCaptureAutomation::AdvanceSceneOrQuit:
    {
        SceneRuntimeControlExecutionContext sceneControlContext{
            this,
            []( void* context ) { static_cast<Run*>( context )->EnterInteractiveSceneRun(); },
            []( void* context, int index, bool preserveUIState, bool suppressExitOnComplete, bool preserveRuntimeState )
                -> bool
            {
                return static_cast<Run*>( context )
                    ->LoadScene( index, preserveUIState, suppressExitOnComplete, preserveRuntimeState )
                    .ok;
            },
            SceneState(),
            m_diagnosticsRuntime.Capture().Screenshot().isScreenshotAndExit,
            SceneRuntimeStyleContext{ m_launchOptions,
                                      SceneState(),
                                      m_sceneController.Browser(),
                                      m_cGameModelCollection,
                                      m_systems.assets,
                                      RuntimeActiveCinematicConfig( SceneState(), m_config ),
                                      m_defaultCinematicRender },
        };
        const SceneRuntimeControlAction action = m_sceneCoordinator.AdvanceScene( m_diagnosticsRuntime.PerfTestActive(),
                                                                                  sPerfPass,
                                                                                  SceneState().isInteractiveRun );
        if ( !ExecuteSceneRuntimeControlAction( sceneControlContext, action ) )
        {
            if ( result.completion == RuntimeCaptureCompletion::Screenshot )
            {
                PrintRuntimeExitReason(
                    "Exiting because scene screenshot capture completed and no next scene is queued." );
            }
            PostQuitMessage( 0 );
        }
        break;
    }
    case RuntimeCaptureAutomation::HoldInteractive:
        HoldCompletedInteractiveScene();
        break;
    case RuntimeCaptureAutomation::None:
        break;
    }

    return result.restartFrame;
}


void Run::TickAutoCycle()
{
    if ( m_debug.isCrossScenePauseLocked && !Input::IsKeyDown( VK_SPACE ) )
    {
        return;
    }

    const RuntimeCaptureSink sink{ this,
                                   []( void* context, const char* path )
                                   { static_cast<Run*>( context )->SaveScreenshot( path ); } };
    const RuntimeCaptureResult result =
        m_diagnosticsRuntime.Capture().TickAutoCycle( SceneState().isSceneMode,
                                                      SceneState().isInteractiveRun,
                                                      m_cGameModelCollection.SceneEntityCount(),
                                                      m_camera.autoCycleInterval,
                                                      m_camera.autoCycleAccum,
                                                      m_camera.autoCycleShotsTaken,
                                                      m_camera.trackBallIndex,
                                                      sink );

    if ( result.completion != RuntimeCaptureCompletion::AutoCycle )
    {
        return;
    }

#ifdef _DEBUG
    LogSceneFinished( "auto_cycle" );
#endif

    if ( result.automation == RuntimeCaptureAutomation::Quit )
    {
        PostQuitMessage( 0 );
    }
    else if ( result.automation == RuntimeCaptureAutomation::HoldInteractive )
    {
        HoldCompletedInteractiveScene();
    }
}


bool Run::TickSceneAdvance()
{
    SceneRuntimeControlExecutionContext sceneControlContext{
        this,
        []( void* context ) { static_cast<Run*>( context )->EnterInteractiveSceneRun(); },
        []( void* context, int index, bool preserveUIState, bool suppressExitOnComplete, bool preserveRuntimeState )
            -> bool
        {
            return static_cast<Run*>( context )
                ->LoadScene( index, preserveUIState, suppressExitOnComplete, preserveRuntimeState )
                .ok;
        },
        SceneState(),
        m_diagnosticsRuntime.Capture().Screenshot().isScreenshotAndExit,
        SceneRuntimeStyleContext{ m_launchOptions,
                                  SceneState(),
                                  m_sceneController.Browser(),
                                  m_cGameModelCollection,
                                  m_systems.assets,
                                  RuntimeActiveCinematicConfig( SceneState(), m_config ),
                                  m_defaultCinematicRender },
    };
    const bool sceneProceedAllowed = !m_debug.isCrossScenePauseLocked || Input::IsKeyDown( VK_SPACE );
    if ( !sceneProceedAllowed )
    {
        return false;
    }

    ++SceneState().currentFrame;

    const bool hasRequiredContactGate = !m_requiredSceneContacts.empty();
    const bool hasRequiredBroadphaseGate = !m_requiredBroadphaseXCells.empty();
    const bool hasRequiredSceneGate = hasRequiredContactGate || hasRequiredBroadphaseGate;
    const bool requiredContactsComplete = RequiredSceneContactsComplete();
    const bool requiredBroadphaseComplete = RequiredSceneBroadphaseXCellsComplete();
    const bool requiredSceneComplete = requiredContactsComplete && requiredBroadphaseComplete;
    if ( hasRequiredSceneGate && requiredSceneComplete && !SceneState().isTestComplete )
    {
#ifdef _DEBUG
        LogSceneFinished( "required_scene_gates" );
#endif
        if ( SceneState().isExitOnComplete && CanSceneAutomationQuit() )
        {
            if ( !ExecuteSceneRuntimeControlAction(
                     sceneControlContext,
                     m_sceneCoordinator.AdvanceScene( m_diagnosticsRuntime.PerfTestActive(),
                                                      sPerfPass,
                                                      SceneState().isInteractiveRun ) ) )
            {
                PostQuitMessage( 0 );
            }
            return true;
        }

        if ( CanSceneAutomationQuit() )
        {
            SceneState().isTestComplete = true;
        }
        else
        {
            HoldCompletedInteractiveScene();
        }
    }

    // Check if target frame count is reached (skip if screenshot auto-exit is still pending)
    if ( SceneState().targetFrameCount > 0 && !m_diagnosticsRuntime.Capture().Screenshot().isScreenshotSaved )
    {
        if ( SceneState().currentFrame >= SceneState().targetFrameCount )
        {
            const bool frameCountCompletesScene = !hasRequiredSceneGate || requiredSceneComplete;
#ifdef _DEBUG
            if ( !SceneState().isTestComplete &&
                 ( frameCountCompletesScene || SceneState().currentFrame == SceneState().targetFrameCount ) )
            {
                LogSceneFinished( frameCountCompletesScene ? "frame_count" : "required_scene_gates_missing" );
                if ( !frameCountCompletesScene )
                {
                    for ( const RunRequiredContactState& contact : m_requiredSceneContacts )
                    {
                        if ( contact.bodyA < 0 || contact.bodyB < 0 || !contact.touched )
                        {
                            fprintf( stderr,
                                     "[scene] required_contact missing: %s <-> %s\n",
                                     contact.nameA,
                                     contact.nameB );
                        }
                    }
                    for ( const RunRequiredBroadphaseXCellsState& cells : m_requiredBroadphaseXCells )
                    {
                        if ( !cells.activated )
                        {
                            fprintf( stderr,
                                     "[scene] required_broadphase_x_cells missing: x %d..%d y %d z %d first_missing=%d "
                                     "active_cells=%d observed_x=%s%d..%d\n",
                                     cells.minCellX,
                                     cells.maxCellX,
                                     cells.cellY,
                                     cells.cellZ,
                                     cells.lastMissingCellX,
                                     cells.lastActiveCellCount,
                                     cells.hasObservedXRange ? "" : "none ",
                                     cells.lastObservedMinX,
                                     cells.lastObservedMaxX );
                        }
                    }
                }
            }
#endif
            if ( !frameCountCompletesScene )
            {
                return false;
            }

            if ( SceneState().isExitOnComplete && CanSceneAutomationQuit() )
            {
                if ( !ExecuteSceneRuntimeControlAction(
                         sceneControlContext,
                         m_sceneCoordinator.AdvanceScene( m_diagnosticsRuntime.PerfTestActive(),
                                                          sPerfPass,
                                                          SceneState().isInteractiveRun ) ) )
                {
                    PostQuitMessage( 0 );
                }
                return true;
            }
            else
            {
                if ( frameCountCompletesScene && CanSceneAutomationQuit() )
                {
                    SceneState().isTestComplete = true;
                }
                else if ( frameCountCompletesScene )
                {
                    HoldCompletedInteractiveScene();
                }
            }
        }
    }

    // Generated demo mode: restart every 20s to keep the sandbox moving indefinitely.
    if ( !SceneState().isSceneMode && !IsManualCameraMode() && m_timers.simulationTimer.GetTimeSinceLastStart() > 20.0 )
    {
        const SbResult loadResult = LoadScene( SceneState().currentSceneIndex,
                                               SceneState().isInteractiveRun,
                                               SceneState().isInteractiveRun,
                                               SceneState().isInteractiveRun );
        if ( !loadResult.ok )
        {
            return false;
        }
        m_timers.simulationTimer.StartTimer();
        return true;
    }

    // Perf-log scenes without an explicit frame count still use a timed pass duration.
    if ( m_diagnosticsRuntime.PerfTestActive() && SceneState().targetFrameCount <= 0 &&
         m_timers.simulationTimer.GetTimeSinceLastStart() > PERF_TEST_PASS_SECONDS )
    {
#ifdef _DEBUG
        LogSceneFinished( "perf_duration" );
#endif
        if ( !ExecuteSceneRuntimeControlAction( sceneControlContext,
                                                m_sceneCoordinator.AdvanceScene( m_diagnosticsRuntime.PerfTestActive(),
                                                                                 sPerfPass,
                                                                                 SceneState().isInteractiveRun ) ) )
        {
            if ( CanSceneAutomationQuit() )
            {
                PostQuitMessage( 0 );
            }
            else
            {
                HoldCompletedInteractiveScene();
            }
        }
        return true;
    }

    return false;
}


void Run::UpdateLogic( float simulationDt, float cameraDt )
{
    // Auto-cycle
    if ( SceneState().isSceneMode && m_camera.autoCycleInterval > 0.0f )
    {
        m_camera.autoCycleAccum += simulationDt;
    }

    // Camera controls are presentation-time behavior, not simulation-time
    // behavior. Keyboard travel is velocity-based, so it consumes unscaled real
    // frame time. Mouse look consumes a per-frame cursor delta, so using live dt
    // would make sensitivity vary with FPS; the fixed reference preserves the
    // existing 60 Hz tuning while making the result frame-rate independent.
    const EngineConfig& cfg = m_config;
    MoveCamera( cameraDt * cfg.keySpeed, CAMERA_MOUSE_REFERENCE_DT * cfg.mouseSensitivity );
    TickAttachedCamera();
    DemoDirectorPlayback::Tick( m_camera,
                                m_systems,
                                m_replayRuntime.Prediction(),
                                SceneRuntimeStyleContext{ m_launchOptions,
                                                          SceneState(),
                                                          m_sceneController.Browser(),
                                                          m_cGameModelCollection,
                                                          m_systems.assets,
                                                          RuntimeActiveCinematicConfig( SceneState(), m_config ),
                                                          m_defaultCinematicRender },
                                cameraDt );

    UpdateWaterHeightControls( simulationDt );

    // Tween speed is also presentation-time behavior. The selected destination
    // camera can still track moving scene objects, but the interpolation rate
    // itself should be stable in real seconds instead of following time_scale.
    m_systems.cameras->SetTweenSpeed( cfg.cameraTweenRate * cameraDt );
}


void Run::UpdateWaterHeightControls( float dt )
{
    const bool downNow = Input::IsKeyDown( VK_NEXT );
    const bool upNow = Input::IsKeyDown( VK_PRIOR );
    if ( downNow == upNow )
    {
        return;
    }

    const float direction = upNow ? 1.0f : -1.0f;
    const float height = m_cWorldEnvironment.GetFluidSurfaceHeight() + direction * WATER_HEIGHT_CONTROL_SPEED * dt;
    m_cWorldEnvironment.SetFluidSurfaceHeight( height );
}
