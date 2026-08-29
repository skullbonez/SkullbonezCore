/*
File: SkullbonezSource/Runtime/App/ReplayValidation.Probes.cpp
Purpose:
  Owns the legacy Debug replay probes that exercise production replay restore,
  scrub, save/load, failure, branch, and visual reconstruction paths.

Summary:
  Debug command-line workflows call the same ReplayRuntime, store, artifact,
  and presentation operations as production. This translation unit contributes
  no object or declaration surface to Release, Profile, or Automation builds.

Glossary:
  Visual projection: Rebuilding renderer-bound replay packet values from a
    durable artifact without scheduling a new prediction.

Invariants:
  - Probe flags, messages, call order, and exit behavior are frozen.
  - The probes borrow concrete product owners only for one synchronous action.
  - Visual verification never starts a second prediction generation.
  - This file is linked only by the Debug engine configuration.

Related:
  - SkullbonezSource/Runtime/App/ReplayValidation.cpp
  - SkullbonezSource/Runtime/App/ReplayValidation.Internal.h
  - SkullbonezSource/Runtime/Replay/ReplayProbeState.h
  - Agentic/Reference/engine-glossary.md
*/
#include "../Replay/ReplayPresentation.h"
#include "../Replay/ReplayOverlayLayout.h"
#include "../Replay/ReplayScrubber.h"
#include "../Replay/ReplayTimeline.h"
#include "ReplayRuntime.h"
#include "ReplayPredictionComposition.h"
#include "SceneLoadApplication.h"
#include "../Input/InputRouter.h"
#include "../Diagnostics/DiagnosticsRuntime.h"
#include "../Scene/SceneController.h"
#include "../../Assets/AssetSystem.h"
#include "../../Core/WorkerPool.h"
#include "../Editor/EditorTools.h"
#include "ReplayRestoreOperations.h"
#include "../Replay/ReplayRestoreTransactions.h"
#include "ReplayValidation.Internal.h"
#include "../Replay/ReplayVisualPacketFingerprint.h"
#include "../Replay/ReplayV2Artifact.h"

#include "../../Core/FatalError.h"
#include "../../Core/SbDiagnosticStore.h"
#include "../../Core/Profiler.h"
#include "../Simulation/SimulationSystem.h"
#include "../../Physics/ColliderStore.h"
#include "../../Physics/PhysicsApi.h"
#include "../../Physics/PhysicsEngine.h"
#include "../../Physics/PhysicsTimestep.h"
#include "../../Rendering/RenderInstanceStore.h"

#include <bit>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <limits>
#include <utility>
#include <vector>

using namespace SkullbonezCore::Runtime;
using namespace SkullbonezCore::Runtime::ReplayScrubberOperations;
using namespace SkullbonezCore::Runtime::ReplayValidationInternal;
using namespace SkullbonezCore::Runtime::ReplayVisualPacketFingerprintOperations;
using namespace SkullbonezCore::Math::CollisionDetection;

using namespace SkullbonezCore::Math::Orientation;
using namespace SkullbonezCore::Math::Transformation;
using namespace SkullbonezCore::Physics;
using SkullbonezCore::Math::Vector::Vector3;

namespace
{
constexpr const char* REPLAY_PROBE_OWNER = "ReplayProbe";

bool TryGetReplayProbeBodyHotState( const SceneWorld& world, int modelIndex, PhysicsBodyHotState& outState )
{
    const PhysicsBodyStore& bodyStore = world.BodyStore();

    if ( !TryGetReplayProbeBodyRecord( world, modelIndex ) || modelIndex < 0 || modelIndex >= bodyStore.Count() )
    {
        return false;
    }

    outState = LoadPhysicsBodyHotState( bodyStore.HotFields(), static_cast<std::size_t>( modelIndex ) );
    return true;
}

float ReplayProbePredictionFutureSeconds( const RunReplayPredictionState& prediction )
{
    // Concept: validation projects whichever immutable bank is currently
    // presentation-coherent; it never opens the prediction worker's write bank.
    std::span<const RunReplayPredictionFrame> frames;

    if ( prediction.BuildPrefixShouldBePresented() )
    {
        frames = { prediction.build.buildFrames.data(), prediction.PublishedBuildFrameCount() };
    }
    else if ( prediction.BuildFramesAreComplete() )
    {
        frames = prediction.build.buildFrames;
    }
    else
    {
        frames = prediction.CommittedFrames();
    }

    return frames.size() < 2 ? 0.0f : static_cast<float>( frames.back().frameIndex ) * PHYSICS_FIXED_DT;
}

float ReplayProbeSolverPresentTrackPosition( const ReplayTimeline& timeline, const ReplayPrediction& prediction )
{
    // Units: past and future spans are seconds derived from fixed-step frame
    // indices before they are normalized onto the combined scrubber track.
    const ReplayRecorderStats stats = timeline.Solver().GetStats();
    const float pastSeconds = !stats.enabled || stats.sampleCount < 2
                                  ? PHYSICS_FIXED_DT
                                  : static_cast<float>( stats.sampleCount - 1 ) * PHYSICS_FIXED_DT;

    const float futureSeconds = ReplayProbePredictionFutureSeconds( prediction.State() );
    return futureSeconds <= PHYSICS_FIXED_DT ? 1.0f
                                             : std::clamp( pastSeconds / ( pastSeconds + futureSeconds ), 0.05f, 0.995f );
}

void ApplyReplayProbePredictionResult( const ReplayPredictionUpdateResult& result, ReplayTimeline& timeline,
                                       ReplayScrubber& scrubber, ReplayPresentation& presentation,
                                       ReplayPrediction& prediction, ReplayPredictionPresentation& predictionPresentation )
{
    // Invariant: the probe applies the same owner-to-owner publication facts as
    // production without receiving mutable prediction storage.
    if ( result.targetModelRowRepaired )
    {
        presentation.SetPathTargetModelRow( result.repairedTargetModelRow );
    }

    if ( result.pinSolverScrubberToPresent )
    {
        scrubber.SetTrackPosition( RunReplayTrack::Solver, ReplayProbeSolverPresentTrackPosition( timeline, prediction ) );

        if ( scrubber.View().activeTrack == RunReplayTrack::Solver )
        {
            scrubber.SetHistoricalSamplePaused( false );
        }
    }

    for ( std::size_t passIndex = 0; passIndex < result.budgetExpiries.size(); ++passIndex )
    {
        for ( uint32_t count = 0; count < result.budgetExpiries[passIndex]; ++count )
        {
            predictionPresentation.RecordTrajectoryBudgetExpiry(
                static_cast<SkullbonezCore::Core::MainMemoryReplayBudgetPass>( passIndex ) );
        }
    }

    for ( std::size_t causeIndex = 0; causeIndex < result.rebuildCauses.size(); ++causeIndex )
    {
        for ( uint32_t count = 0; count < result.rebuildCauses[causeIndex]; ++count )
        {
            predictionPresentation.RecordTrajectoryRebuildCause(
                static_cast<SkullbonezCore::Core::MainMemoryReplayRebuildCause>( causeIndex ) );
        }
    }
}

void PrepareReplayProbePredictionPresentation( ReplayTimeline& timeline, ReplayScrubber& scrubber,
                                               ReplayPresentation& presentation, ReplayPrediction& prediction,
                                               ReplayPredictionPresentation& predictionPresentation, PhysicsEngine& physics,
                                               const SceneEntityStore& entities )
{
    // Why: durable visual verification runs after the normal scheduler stops,
    // so it prepares CPU presentation explicitly without scheduling new work.
    const RunReplayPathVisualizerState& path = presentation.PathVisualizer();
    ReplayPredictionUpdateResult result;
    std::array<ReplayPredictionSceneEntityFact, SkullbonezCore::Scene::Capacity::MAX_SCENE_OBJECTS> sceneFacts = {};
    prediction.PreparePresentation( BuildReplayPredictionSceneView( entities, sceneFacts ),
                                    PhysicsEngine::ReadColliders( physics ), path.targetId, path.targetModelRow,
                                    path.hasTarget, 5.0, result );

    ApplyReplayProbePredictionResult( result, timeline, scrubber, presentation, prediction, predictionPresentation );

    if ( prediction.PresentationView().generationPermitted )
    {
        const ReplayPastTrajectoryUpdate update = RefreshReplayPastTrajectory( prediction, timeline.Solver(),
                                                                               presentation.PastTrajectoryView() );

        if ( update.apply )
        {
            presentation.ApplyPastTrajectoryUpdate( update.targetId, update.firstFrame, update.builtThroughFrame,
                                                    update.totalFramesEvicted, update.fullRebuildCount,
                                                    update.incrementalTrimCount, update.valid, update.targetModelRow,
                                                    update.targetModelRowRepaired );
        }
    }

    presentation.PreparePathDrawing( PhysicsEngine::ReadBodies( physics ) );
}

const ReplaySolverFrameSample* ReplayProbeCurrentSolverSample( const ReplayTimeline& timeline,
                                                               const ReplayScrubber& scrubber,
                                                               const ReplayPrediction& prediction )
{
    const ReplayScrubberView view = scrubber.View();

    if ( view.activeTrack != RunReplayTrack::Solver || !view.historicalSamplePaused )
    {
        return nullptr;
    }

    const float position = scrubber.TrackPosition( RunReplayTrack::Solver );
    const float present = ReplayProbeSolverPresentTrackPosition( timeline, prediction );

    if ( ReplayTrackPositionIsFuture( position, present ) )
    {
        return nullptr;
    }

    return timeline.Solver().SampleAtNormalized( ReplaySolverNormalizedFromTrack( position, present ) );
}

Vector3 RenderProbeMatrixTranslation( const Matrix4& matrix )
{
    return Vector3( matrix.m[12], matrix.m[13], matrix.m[14] );
}

bool TryPrepareReplayProbeRenderPosition( SceneWorld& world, int modelIndex, Vector3& outPosition )
{
    const auto instances = world.RenderInstances().Records();

    if ( modelIndex < 0 || modelIndex >= static_cast<int>( instances.size() ) )
    {
        return false;
    }

    outPosition = RenderProbeMatrixTranslation( instances[static_cast<std::size_t>( modelIndex )].modelMatrix );
    return true;
}

bool ApplyReplayProbePresentationSampleForRender( SceneWorld& world, ReplayPresentation& presentation,
                                                  const ReplayPresentationSample& sample )
{
    // Why: probes consume replay scrub poses exactly where the renderer consumes
    // them: after the live render snapshot refresh and before draw submission.
    // This proves presentation overrides do not mutate live body rows.
    world.PrepareRenderInstances();
    return presentation.ApplyPresentationSampleForRender( world.MutableRenderInstances(), world.BodyStore(),
                                                          world.Colliders(), sample );
}

void RestoreReplayProbeRenderInstances( SceneWorld& world )
{
    world.PrepareRenderInstances();
}

float ReplaySaveProbeDistanceSquared( const Vector3& a, const Vector3& b )
{
    const Vector3 delta = a - b;
    return delta.x * delta.x + delta.y * delta.y + delta.z * delta.z;
}


// Value-only replay effects emitted by the external save-probe fixture. The
// fixture may mutate its temporary scene, but it cannot retain or call the
// replay composition root; TickProbes applies these commands in event order.
struct ReplaySaveProbeEventCommands
{
    bool requestInteractiveScene = false;

    bool recordWorldOverride = false;
    float previousGravity = 0.0f;
    float previousFluidHeight = 0.0f;
    float previousFluidDensity = 0.0f;
    float gravity = 0.0f;
    float fluidHeight = 0.0f;
    float fluidDensity = 0.0f;

    bool recordEditorPlace = false;
    int placedObjectType = 0;
    bool placedFixedObject = false;
    bool placedAutoTerrainAlign = false;
    int placedModelCountBefore = 0;
    Vector3 placedTerrainPoint;
    Vector3 placedScale;
    float placedYawRadians = 0.0f;

    bool recordEditorTransform = false;
    int transformedModelIndex = -1;
    PhysicsSceneObjectId transformedSceneObjectId;
    Vector3 transformedPosition;
    Quaternion transformedOrientation;
    int transformedModelCount = 0;
    int transformedScaleAxis = -1;
    float transformedScaleFactor = 1.0f;

    bool recordLauncherConfig = false;
    float launcherImpulseStrength = 0.0f;
    float launcherProjectileSpeed = 0.0f;

    bool recordLauncherFire = false;
    Vector3 launcherRayOrigin;
    Vector3 launcherRayDirection;
    Vector3 launcherCameraUp;
    bool launcherProjectile = false;
    int launcherModelCount = 0;
};


// Lifetime: each fixture action borrows only the concrete owners needed for
// that synchronous mutation. Keeping the actions separate prevents validation
// from rebuilding the application shell as a retained multi-domain context.
void InjectReplaySaveProbeWorldCoverage( SkullbonezCore::Environment::WorldEnvironment& world,
                                         ReplaySaveProbeEventCommands& commands )
{
    const float currentGravity = world.GetGravity();
    const float currentFluidHeight = world.GetFluidSurfaceHeight();
    const float currentFluidDensity = world.GetFluidDensity();
    const float probeGravity = currentGravity != 0.0f ? currentGravity * 0.95f : -0.25f;
    world.SetGravity( probeGravity );
    world.SetFluidSurfaceHeight( currentFluidHeight );
    world.SetFluidDensity( currentFluidDensity );
    commands.recordWorldOverride = true;
    commands.previousGravity = currentGravity;
    commands.previousFluidHeight = currentFluidHeight;
    commands.previousFluidDensity = currentFluidDensity;
    commands.gravity = probeGravity;
    commands.fluidHeight = currentFluidHeight;
    commands.fluidDensity = currentFluidDensity;
}


SkullbonezCore::Core::SbResult InjectReplaySaveProbePlacementCoverage( SkullbonezCore::Core::SbDiagnosticStore& diagnostics,
                                                                       EditorToolsOwner& editorTools, SceneWorld& world,
                                                                       SceneSessionState& scene,
                                                                       SkullbonezCore::Assets::AssetSystem& assets,
                                                                       int sceneObjectCapacity,
                                                                       ReplaySaveProbeEventCommands& commands )
{
    const auto ReplayProbeFailure = [&diagnostics]( const char* message )
    { return diagnostics.Failure( REPLAY_PROBE_OWNER, "%s", message ); };

    editorTools.Editor().placementScale = Vector3( 2.0f, 2.0f, 2.0f );
    editorTools.Editor().autoTerrainAlign = false;
    PhysicsEngine& physics = world.Physics();
    const int modelCountBeforePlace = world.SceneEntityCount();
    EditorObjectPlacementRequest placementRequest { SkullbonezCore::UI::EditorTab::OBJECT_BOX, true,
                                                    Vector3( 18.0f, 0.0f, 18.0f ) };

    EditorObjectPlacementResult placementResult;

    if ( CanPlaceEditorObjectAtTerrainPoint( world, assets, sceneObjectCapacity, placementRequest ) )
    {
        commands.requestInteractiveScene = true;
        PlaceEditorObjectAtTerrainPoint( diagnostics, editorTools.Editor(), world, scene, assets, sceneObjectCapacity,
                                         placementRequest, placementResult );
    }

    if ( placementResult.placed )
    {
        commands.recordEditorPlace = true;
        commands.placedObjectType = placementResult.objectType;
        commands.placedFixedObject = placementResult.fixedObject;
        commands.placedAutoTerrainAlign = placementResult.autoTerrainAlign;
        commands.placedModelCountBefore = placementResult.modelCountBefore;
        commands.placedTerrainPoint = placementResult.terrainPoint;
        commands.placedScale = placementResult.placementScale;
        commands.placedYawRadians = placementResult.placementYawRadians;
        const PhysicsBodyRecord* placedBodyBeforeEdit = world.BodyStore().RecordForHandle( placementResult.placedBody );
        PhysicsBodyHotState placedHotBeforeEdit;

        if ( !placedBodyBeforeEdit || !TryGetReplayProbeBodyHotState( world, modelCountBeforePlace, placedHotBeforeEdit ) )
        {
            return ReplayProbeFailure( "replay save probe failed to resolve placed body record" );
        }

        // Why: placement has already registered a PhysicsBodyHandle. Use the
        // authoritative body row as the starting transform, then commit the
        // edited descriptor back into the stores below.
        PhysicsBodyUpdateDesc placedBodyEdit;
        placedBodyEdit.body = placementResult.placedBody;
        placedBodyEdit.updateMask = PHYSICS_BODY_UPDATE_POSE | PHYSICS_BODY_UPDATE_VELOCITY;
        placedBodyEdit.position = placedHotBeforeEdit.position + Vector3( 4.0f, 0.0f, 0.0f );
        Quaternion placedOrientation = placedHotBeforeEdit.orientation;
        placedOrientation.RotateAboutAxis( Vector3( 0.0f, 1.0f, 0.0f ), 0.25f );
        placedBodyEdit.orientation = placedOrientation;
        const ColliderRecord*
            placedColliderBeforeEdit = TryGetEditorTransformColliderRecord( world, placementResult.placedCollider,
                                                                            modelCountBeforePlace,
                                                                            placedBodyBeforeEdit->sceneObjectId );

        if ( !placedColliderBeforeEdit )
        {
            return ReplayProbeFailure( "replay save probe failed to resolve placed collider record" );
        }

        const CollisionShape placedShapeBeforeScale = CopyCollisionShape( placedColliderBeforeEdit->shape );
        constexpr int PROBE_SCALE_AXIS = 0;
        constexpr float PROBE_SCALE_FACTOR = 1.5f;
        CollisionShape placedShapeAfterScale;

        if ( !ScaleShapeAxisFromBase( placedShapeBeforeScale, PROBE_SCALE_AXIS, PROBE_SCALE_FACTOR, placedShapeAfterScale ) )
        {
            return ReplayProbeFailure( "replay save probe failed to apply editor transform scale" );
        }

        placedBodyEdit.linearVelocity = Vector3( 0.0f, 0.0f, 0.0f );
        placedBodyEdit.angularVelocity = Vector3( 0.0f, 0.0f, 0.0f );

        // Invariant: the replay probe exercises the same explicit collider
        // edit command as the editor instead of relying on a model recapture.
        if ( !physics
                  .UpdateAuthoredBodyAndCollider( placedBodyEdit,
                                                  MakeColliderCreateDesc( std::move( placedShapeAfterScale ),
                                                                          placedColliderBeforeEdit->restitution,
                                                                          placedColliderBeforeEdit->contactMaterialId ) ) )
        {
            return ReplayProbeFailure( "replay save probe failed to commit edited physics rows" );
        }

        const PhysicsBodyRecord* placedBodyAfterEdit = world.BodyStore().RecordForModelIndex( modelCountBeforePlace );
        PhysicsBodyHotState placedHotAfterEdit;

        if ( !placedBodyAfterEdit || !placedBodyAfterEdit->sceneObjectId.IsValid() ||
             !TryGetReplayProbeBodyHotState( world, modelCountBeforePlace, placedHotAfterEdit ) )
        {
            return ReplayProbeFailure( "replay save probe failed to capture edited body record" );
        }

        commands.recordEditorTransform = true;
        commands.transformedModelIndex = modelCountBeforePlace;
        commands.transformedSceneObjectId = placedBodyAfterEdit->sceneObjectId;
        commands.transformedPosition = placedHotAfterEdit.position;
        commands.transformedOrientation = placedHotAfterEdit.orientation;
        commands.transformedModelCount = world.SceneEntityCount();
        commands.transformedScaleAxis = PROBE_SCALE_AXIS;
        commands.transformedScaleFactor = PROBE_SCALE_FACTOR;
    }

    return SkullbonezCore::Core::SbResult::Success();
}


void InjectReplaySaveProbeLauncherCoverage( RuntimeTools& runtimeTools, SceneWorld& world, SceneSessionState& scene,
                                            int sceneObjectCapacity, ReplaySaveProbeEventCommands& commands )
{
    runtimeTools.RayCastTest().projectileSpeed += 1.0f;
    commands.recordLauncherConfig = true;
    commands.launcherImpulseStrength = runtimeTools.RayCastTest().impulseStrength;
    commands.launcherProjectileSpeed = runtimeTools.RayCastTest().projectileSpeed;
    Vector3 rayOrigin;
    Vector3 rayDirection;
    Vector3 cameraUp;

    if ( runtimeTools.TryBuildLauncherCameraRay( &world.Cameras(), rayOrigin, rayDirection, cameraUp ) )
    {
        commands.recordLauncherFire = true;
        commands.launcherRayOrigin = rayOrigin;
        commands.launcherRayDirection = rayDirection;
        commands.launcherCameraUp = cameraUp;
        commands.launcherProjectile = runtimeTools.RayCastTest().fireMode == RunLauncherFireMode::Projectile;
        commands.launcherModelCount = world.SceneEntityCount();

        // Why: RuntimeTools now fails closed unless Run has completed the cold
        // world-to-store topology repair at the owner boundary.
        const bool launcherStoresReady = world.RepairPhysicsBodyAndColliderTopology();

        if ( launcherStoresReady &&
             runtimeTools.FireLauncherRay( world, scene, sceneObjectCapacity, rayOrigin, rayDirection, cameraUp ) )
        {
            scene.modelCount = world.SceneEntityCount();
        }
    }
}


SkullbonezCore::Core::SbResult ValidateReplaySaveProbeArtifact( SkullbonezCore::Core::SbDiagnosticStore& diagnostics,
                                                                const ReplayTimeline& timeline, SceneWorld& world,
                                                                ReplayPresentation& presentation, const char* path )
{
    const auto ReplayProbeFailure = [&diagnostics]( const char* message )
    { return diagnostics.Failure( REPLAY_PROBE_OWNER, "%s", message ); };

    ReplayV2SaveResult result;

    // Invariant: one ReplayTimeline supplies all three recorded tracks. Taking
    // separate recorder borrows would let a probe serialize unrelated capture
    // generations. The path names one cold artifact identity used for both the
    // write and the reload validation below.
    if ( !ReplayV2Artifact::SavePresentationWithSolverHashes( timeline.Presentation(), timeline.Solver(), timeline.Events(),
                                                              path, &result ) )
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

    if ( !ReplayV2Artifact::LoadPresentation( path, loadedSamples, &loadResult ) )
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

            const float candidateDistanceSquared = ReplaySaveProbeDistanceSquared( liveCandidate.position,
                                                                                   candidate.position );

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

    const int probedModelIndex = liveBody->modelRow.value;
    const PhysicsBodyRecord* probedBody = TryGetReplayProbeBodyRecord( world, probedModelIndex );
    PhysicsBodyHotState probedHotState;

    if ( !probedBody || !TryGetReplayProbeBodyHotState( world, probedModelIndex, probedHotState ) )
    {
        return ReplayProbeFailure( "replay save probe loaded an invalid live body index" );
    }

    const Vector3 preApplyPosition = probedHotState.position;
    const float preLiveDeltaSquared = ReplaySaveProbeDistanceSquared( preApplyPosition, liveBody->position );

    if ( preLiveDeltaSquared > 0.0001f )
    {
        return ReplayProbeFailure( "replay save probe live body did not match the loaded v2 live sample" );
    }

    const bool applied = ApplyReplayProbePresentationSampleForRender( world, presentation, selected );

    if ( !applied )
    {
        return ReplayProbeFailure( "replay save probe failed to apply the loaded v2 presentation sample" );
    }

    const PhysicsBodyRecord* appliedBody = TryGetReplayProbeBodyRecord( world, probedModelIndex );
    PhysicsBodyHotState appliedHotState;

    if ( !appliedBody || !TryGetReplayProbeBodyHotState( world, probedModelIndex, appliedHotState ) )
    {
        RestoreReplayProbeRenderInstances( world );
        return ReplayProbeFailure( "replay save probe lost the selected live body after applying the v2 sample" );
    }

    const Vector3 liveAfterApplyPosition = appliedHotState.position;
    const float livePreservedDeltaSquared = ReplaySaveProbeDistanceSquared( liveAfterApplyPosition, preApplyPosition );

    if ( livePreservedDeltaSquared > 0.0001f )
    {
        RestoreReplayProbeRenderInstances( world );
        return ReplayProbeFailure( "replay save probe mutated the live body while applying the v2 sample" );
    }

    Vector3 appliedRenderPosition;

    if ( !TryPrepareReplayProbeRenderPosition( world, probedModelIndex, appliedRenderPosition ) )
    {
        RestoreReplayProbeRenderInstances( world );
        return ReplayProbeFailure( "replay save probe lost the selected render instance after applying the v2 sample" );
    }

    const float appliedDeltaSquared = ReplaySaveProbeDistanceSquared( appliedRenderPosition, selectedBody->position );

    if ( appliedDeltaSquared > 0.0001f )
    {
        RestoreReplayProbeRenderInstances( world );
        return ReplayProbeFailure( "replay save probe did not move the render instance to the loaded v2 sample" );
    }

    RestoreReplayProbeRenderInstances( world );
    const PhysicsBodyRecord* restoredBody = TryGetReplayProbeBodyRecord( world, probedModelIndex );
    PhysicsBodyHotState restoredHotState;

    if ( !restoredBody || !TryGetReplayProbeBodyHotState( world, probedModelIndex, restoredHotState ) )
    {
        return ReplayProbeFailure( "replay save probe lost the selected live body after restoring the v2 sample" );
    }

    const Vector3 restoredPosition = restoredHotState.position;
    const float restoredDeltaSquared = ReplaySaveProbeDistanceSquared( restoredPosition, preApplyPosition );

    if ( restoredDeltaSquared > 0.0001f )
    {
        return ReplayProbeFailure( "replay save probe live body changed after applying the loaded v2 sample" );
    }

    printf( "[replay] Save probe wrote: path=%s samples=%llu bodies=%llu solver_hashes=%llu "
            "solver_checkpoints=%llu events=%llu event_cursors=%llu bytes=%llu\n",
            path, static_cast<unsigned long long>( result.sampleCount ),
            static_cast<unsigned long long>( result.bodyDictionaryCount ),
            static_cast<unsigned long long>( result.solverHashCount ),
            static_cast<unsigned long long>( result.solverCheckpointCount ),
            static_cast<unsigned long long>( result.eventCount ), static_cast<unsigned long long>( result.eventCursorCount ),
            static_cast<unsigned long long>( result.fileBytes ) );

    printf( "[replay] Save probe loaded: samples=%llu bodies=%llu first_frame=%llu selected_frame=%llu "
            "latest_frame=%llu body_id=%u distance_sq=%.6f\n",
            static_cast<unsigned long long>( loadResult.sampleCount ),
            static_cast<unsigned long long>( loadResult.bodyDictionaryCount ),
            static_cast<unsigned long long>( loadResult.firstFrame ), static_cast<unsigned long long>( selected.frameIndex ),
            static_cast<unsigned long long>( live.frameIndex ), selectedBody->id.value, bestDistanceSquared );

    PostQuitMessage( 0 );
    return SkullbonezCore::Core::SbResult::Success();
}
} // namespace


SkullbonezCore::Core::SbResult ReplayProbeRunner::ReplayProbeFailure( const char* message ) const
{
    return m_resultDiagnostics.Failure( REPLAY_PROBE_OWNER, "%s", message );
}


ReplayProbeTickResult
ReplayRuntime::TickProbes( SceneController& sceneController, OverlayDebugState& debug, EditorToolsOwner& editorTools,
                           RuntimeTools& runtimeTools, const SkullbonezCore::Core::EngineConfig& config,
                           SkullbonezCore::Assets::AssetSystem& assets, const ReplaySceneTimelineResetInput& timelineReset,
                           DiagnosticsRuntime& diagnosticsRuntime, InputRouter& inputRouter,
                           RuntimeInteractionController& interaction, const ReplayProbeRestoreCameraState& restoreCamera )
{
    // Invariant: each probe receives only the restore/topology authority its
    // replay operation already requires; adding a whole-world fixture here
    // would recreate the application shell behind a Debug-only name.
    ReplayProbeTickResult result;
    SceneWorld& world = sceneController.Scene();
    SceneSessionState& scene = sceneController.State();
    ReplayScrubProbeDiagnostic scrubDiagnostic;
    result.status = m_probeRunner.TickScrubProbe( world, m_timeline, m_visualPresentation, &scrubDiagnostic );

    if ( result.status.Ok() && scrubDiagnostic.bodyName )
    {
        diagnosticsRuntime.LogReplayScrubProbe( ProjectSceneDiagnosticFacts( scene ), scrubDiagnostic );
    }

    if ( result.status.Ok() )
    {
        const ReplayProbeRestoreRequest restoreRequest = m_probeRunner.PrepareRestoreProbe( m_timeline );
        result.status = restoreRequest.status;

        if ( result.status.Ok() && restoreRequest.sample )
        {
            // The successful restore resets recorder storage, so copy the
            // selected sample before executing the owner-to-owner transaction.
            const ReplaySolverFrameSample selected = *restoreRequest.sample;

            ReplayRestoreTransaction restoreTransaction { timelineReset };
            const bool restored = RestoreSolverSampleAsLive( restoreTransaction, world, scene, debug, runtimeTools,
                                                             selected );

            ReplayLiveRestoreRequest liveRequest;
            liveRequest.kind = ReplayLiveRestoreKind::SolverSample;
            liveRequest.solverSample = &selected;
            ReplayLiveRestoreOutcome outcome;
            outcome.restored = restored;
            strncpy_s( outcome.reason, restoreTransaction.FailureReason(), _TRUNCATE );
            PublishRestoreDiagnostic( restoreTransaction, diagnosticsRuntime, scene );
            ApplyRestoredBranchTimeline( restoreTransaction, outcome, sceneController, inputRouter, interaction,
                                         restoreCamera.camera, restoreCamera.restoreMode, restoreCamera.attachedFollow,
                                         restoreCamera.camera.director.grabbed );

            CompleteLiveRestoreScrubber( restoreTransaction, liveRequest, outcome );

            result.status = m_probeRunner.CompleteRestoreProbe( restoreRequest, outcome.restored, outcome.reason );
        }
    }

    if ( result.status.Ok() )
    {
        const ReplayProbeSaveRequest saveRequest = m_probeRunner.PrepareSaveProbe( m_timeline );

        switch ( saveRequest.action )
        {
        case ReplayProbeSaveAction::ResetScene:

            // Value-only output keeps the Debug probe outside lifecycle request
            // authority; the application boundary submits it after success.
            result.resetCurrentScene = true;
            break;
        case ReplayProbeSaveAction::InjectEventCoverage:
        {
            ReplaySaveProbeEventCommands commands;
            InjectReplaySaveProbeWorldCoverage( world.Environment(), commands );
            result.status = InjectReplaySaveProbePlacementCoverage( m_resultDiagnostics, editorTools, world, scene, assets,
                                                                    SkullbonezCore::Core::ActiveSceneObjectCapacity(
                                                                        config ),
                                                                    commands );

            if ( result.status.Ok() )
            {
                InjectReplaySaveProbeLauncherCoverage( runtimeTools, world, scene,
                                                       SkullbonezCore::Core::ActiveSceneObjectCapacity( config ), commands );
            }

            result.enterInteractive = result.enterInteractive || commands.requestInteractiveScene;

            // Invariant: the external fixture returns facts only. Apply replay
            // events here in the same order as the live actions so recorder
            // sequence numbers and artifact bytes remain unchanged.
            if ( commands.recordWorldOverride )
            {
                SubmitEvent(
                    ReplayEventCommandOperations::BuildWorldOverride( commands.previousGravity, commands.previousFluidHeight,
                                                                      commands.previousFluidDensity, commands.gravity,
                                                                      commands.fluidHeight, commands.fluidDensity ) );
            }

            if ( commands.recordEditorPlace )
            {
                SubmitEvent(
                    ReplayEventCommandOperations::BuildEditorPlace( commands.placedObjectType, commands.placedFixedObject,
                                                                    commands.placedAutoTerrainAlign,
                                                                    commands.placedModelCountBefore,
                                                                    commands.placedTerrainPoint, commands.placedScale,
                                                                    commands.placedYawRadians ) );
            }

            if ( commands.recordEditorTransform )
            {
                SubmitEvent( ReplayEventCommandOperations::BuildEditorTransform( commands.transformedModelIndex,
                                                                                 REPLAY_EDITOR_TRANSFORM_TRANSLATE |
                                                                                     REPLAY_EDITOR_TRANSFORM_ROTATE |
                                                                                     REPLAY_EDITOR_TRANSFORM_SCALE,
                                                                                 commands.transformedSceneObjectId,
                                                                                 commands.transformedPosition,
                                                                                 commands.transformedOrientation,
                                                                                 commands.transformedModelCount,
                                                                                 commands.transformedScaleAxis,
                                                                                 commands.transformedScaleFactor ) );
            }

            if ( commands.recordLauncherConfig )
            {
                SubmitEvent( ReplayEventCommandOperations::BuildLauncherConfig( 2u, commands.launcherImpulseStrength,
                                                                                commands.launcherProjectileSpeed ) );
            }

            if ( commands.recordLauncherFire )
            {
                SubmitEvent( ReplayEventCommandOperations::BuildLauncherFire( commands.launcherRayOrigin,
                                                                              commands.launcherRayDirection,
                                                                              commands.launcherCameraUp,
                                                                              commands.launcherProjectile,
                                                                              commands.launcherImpulseStrength,
                                                                              commands.launcherProjectileSpeed,
                                                                              commands.launcherModelCount ) );
            }

            break;
        }
        case ReplayProbeSaveAction::ValidateArtifact:
        {
            result.status = ValidateReplaySaveProbeArtifact( m_resultDiagnostics, m_timeline, world, m_visualPresentation,
                                                             saveRequest.path );
            break;
        }
        case ReplayProbeSaveAction::None:
        default:
            break;
        }

        m_probeRunner.CompleteSaveProbe( saveRequest, result.status );
    }

    if ( !result.status.Ok() )
    {
        m_probeRunner.RecordFailure( result.status );
    }

    return result;
}


SkullbonezCore::Core::SbResult ReplayProbeRunner::TickScrubProbe( SceneWorld& world, const ReplayTimeline& timeline,
                                                                  ReplayPresentation& presentation,
                                                                  ReplayScrubProbeDiagnostic* outDiagnostic )
{
    RunReplayScrubProbeState& probe = m_probes.scrub;
    auto distanceSquared = []( const Math::Vector::Vector3& a, const Math::Vector::Vector3& b ) -> float
    {
        const Math::Vector::Vector3 delta = a - b;

        return delta.x * delta.x + delta.y * delta.y + delta.z * delta.z;
    };

    if ( !probe.enabled || probe.completed )
    {
        return SkullbonezCore::Core::SbResult::Success();
    }

    const ReplayRecorderStats stats = timeline.Presentation().GetStats();

    if ( stats.sampleCount < static_cast<std::size_t>( probe.minSampleCount ) )
    {
        return SkullbonezCore::Core::SbResult::Success();
    }

    const ReplayPresentationSample* selected = timeline.Presentation().SampleAtNormalized( probe.normalized );
    const ReplayPresentationSample* live = timeline.Presentation().LatestSample();

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

    if ( !selectedBody || !liveBody || bestDistanceSquared < probe.minDistanceSquared )
    {
        return ReplayProbeFailure( "replay scrub probe did not find a moved body in the selected replay window" );
    }

    const int probedModelIndex = liveBody->modelRow.value;
    const PhysicsBodyRecord* probedBody = TryGetReplayProbeBodyRecord( world, probedModelIndex );

    PhysicsBodyHotState probedHotState;

    if ( !probedBody || !TryGetReplayProbeBodyHotState( world, probedModelIndex, probedHotState ) )
    {
        return ReplayProbeFailure( "replay scrub probe selected an invalid live body index" );
    }

    // Why: scrub probes prove presentation overrides do not mutate live
    // simulation state. Read that state from PhysicsBodyStore so the proof does
    // not depend on temporary presentation rows.
    const Math::Vector::Vector3 preApplyPosition = probedHotState.position;
    const float preLiveDeltaSquared = distanceSquared( preApplyPosition, liveBody->position );

    if ( preLiveDeltaSquared > probe.minDistanceSquared )
    {
        return ReplayProbeFailure(
            "replay scrub probe live body did not match the current replay sample before applying scrub state" );
    }

    const bool applied = ApplyReplayProbePresentationSampleForRender( world, presentation, *selected );

    if ( !applied )
    {
        return ReplayProbeFailure( "replay scrub probe failed to apply the selected presentation sample" );
    }

    const PhysicsBodyRecord* appliedBody = TryGetReplayProbeBodyRecord( world, probedModelIndex );

    PhysicsBodyHotState appliedHotState;

    if ( !appliedBody || !TryGetReplayProbeBodyHotState( world, probedModelIndex, appliedHotState ) )
    {
        RestoreReplayProbeRenderInstances( world );
        return ReplayProbeFailure( "replay scrub probe lost the selected live body after applying scrub state" );
    }

    const Math::Vector::Vector3 liveAfterApplyPosition = appliedHotState.position;
    const float livePreservedDeltaSquared = distanceSquared( liveAfterApplyPosition, preApplyPosition );

    if ( livePreservedDeltaSquared > probe.minDistanceSquared )
    {
        RestoreReplayProbeRenderInstances( world );
        return ReplayProbeFailure( "replay scrub probe mutated the live body while applying scrub state" );
    }

    Math::Vector::Vector3 appliedRenderPosition;

    if ( !TryPrepareReplayProbeRenderPosition( world, probedModelIndex, appliedRenderPosition ) )
    {
        RestoreReplayProbeRenderInstances( world );
        return ReplayProbeFailure( "replay scrub probe lost the selected render instance after applying scrub state" );
    }

    const float appliedDeltaSquared = distanceSquared( appliedRenderPosition, selectedBody->position );

    if ( appliedDeltaSquared > probe.minDistanceSquared )
    {
        RestoreReplayProbeRenderInstances( world );
        return ReplayProbeFailure( "replay scrub probe did not move the render instance to the selected replay sample" );
    }

    RestoreReplayProbeRenderInstances( world );
    const PhysicsBodyRecord* restoredBody = TryGetReplayProbeBodyRecord( world, probedModelIndex );

    PhysicsBodyHotState restoredHotState;

    if ( !restoredBody || !TryGetReplayProbeBodyHotState( world, probedModelIndex, restoredHotState ) )
    {
        return ReplayProbeFailure( "replay scrub probe lost the selected live body after restoring scrub state" );
    }

    const Math::Vector::Vector3 restoredPosition = restoredHotState.position;
    const float restoredDeltaSquared = distanceSquared( restoredPosition, preApplyPosition );
    const bool restored = restoredDeltaSquared <= probe.minDistanceSquared;

    if ( !restored )
    {
        return ReplayProbeFailure( "replay scrub probe did not restore the live model after applying the selected sample" );
    }

    ReplayScrubProbeDiagnostic scrubDiagnostic;
    scrubDiagnostic.selectedReplayFrame = selected->frameIndex;
    scrubDiagnostic.liveReplayFrame = live->frameIndex;
    scrubDiagnostic.selectedSceneFrame = selected->sceneFrame;
    scrubDiagnostic.liveSceneFrame = live->sceneFrame;
    scrubDiagnostic.selectedStateHash = selected->stateHash;
    scrubDiagnostic.liveStateHash = live->stateHash;
    scrubDiagnostic.bodyId = selectedBody->id.value;
    scrubDiagnostic.modelIndex = liveBody->modelRow.value;
    scrubDiagnostic.bodyName = selectedBody->name;
    scrubDiagnostic.selectedPosition[0] = selectedBody->position.x;
    scrubDiagnostic.selectedPosition[1] = selectedBody->position.y;
    scrubDiagnostic.selectedPosition[2] = selectedBody->position.z;
    scrubDiagnostic.livePosition[0] = liveBody->position.x;
    scrubDiagnostic.livePosition[1] = liveBody->position.y;
    scrubDiagnostic.livePosition[2] = liveBody->position.z;
    scrubDiagnostic.normalized = probe.normalized;
    scrubDiagnostic.distanceSquared = bestDistanceSquared;
    scrubDiagnostic.selectedBodyCount = selected->bodies.size();
    scrubDiagnostic.liveBodyCount = live->bodies.size();
    scrubDiagnostic.applied = applied;
    scrubDiagnostic.restored = restored;
    scrubDiagnostic.preLiveDeltaSquared = preLiveDeltaSquared;
    scrubDiagnostic.appliedDeltaSquared = appliedDeltaSquared;
    scrubDiagnostic.restoredDeltaSquared = restoredDeltaSquared;

    if ( outDiagnostic )
    {
        *outDiagnostic = scrubDiagnostic;
    }

    probe.completed = true;
    printf( "[replay] Scrub probe passed: selected_replay_frame=%llu live_replay_frame=%llu body_id=%u "
            "distance_sq=%.6f\n",
            static_cast<unsigned long long>( selected->frameIndex ), static_cast<unsigned long long>( live->frameIndex ),
            selectedBody->id.value, bestDistanceSquared );

    PostQuitMessage( 0 );
    return SkullbonezCore::Core::SbResult::Success();
}

ReplayProbeRestoreRequest ReplayProbeRunner::PrepareRestoreProbe( const ReplayTimeline& timeline )
{
    ReplayProbeRestoreRequest request;
    RunReplayRestoreProbeState& probe = m_probes.restore;

    if ( !probe.enabled || probe.completed )
    {
        return request;
    }

    const ReplayRecorderStats stats = timeline.Solver().GetStats();

    if ( stats.sampleCount < static_cast<std::size_t>( probe.minSampleCount ) )
    {
        return request;
    }

    const ReplaySolverFrameSample* selectedSample = timeline.Solver().SampleAtNormalized( probe.normalized );
    const ReplaySolverFrameSample* latestSample = timeline.Solver().LatestSample();

    if ( !selectedSample || !latestSample )
    {
        request.status = ReplayProbeFailure( "replay restore probe could not select retained solver samples" );
        return request;
    }

    if ( selectedSample->frameIndex >= latestSample->frameIndex )
    {
        request.status = ReplayProbeFailure( "replay restore probe did not select an older solver sample" );
        return request;
    }

    request.sample = selectedSample;
    request.selectedFrame = selectedSample->frameIndex;
    request.latestFrame = latestSample->frameIndex;
    request.selectedHash = selectedSample->solverHash;
    return request;
}


SkullbonezCore::Core::SbResult ReplayProbeRunner::CompleteRestoreProbe( const ReplayProbeRestoreRequest& request,
                                                                        bool restored, const char* reason )
{
    if ( !restored )
    {
        return m_resultDiagnostics.Failure( REPLAY_PROBE_OWNER, "replay restore probe failed: %s",
                                            reason && reason[0] != '\0' ? reason : "unknown restore failure" );
    }

    RunReplayRestoreProbeState& probe = m_probes.restore;
    probe.completed = true;
    printf( "[replay] Restore probe passed: target_replay_frame=%llu previous_live_replay_frame=%llu "
            "solver_hash=0x%016llX\n",
            static_cast<unsigned long long>( request.selectedFrame ), static_cast<unsigned long long>( request.latestFrame ),
            static_cast<unsigned long long>( request.selectedHash ) );

    PostQuitMessage( 0 );
    return SkullbonezCore::Core::SbResult::Success();
}

ReplayProbeSaveRequest ReplayProbeRunner::PrepareSaveProbe( const ReplayTimeline& timeline )
{
    ReplayProbeSaveRequest request;
    RunReplaySaveProbeState& probe = m_probes.save;

    if ( !probe.enabled || probe.completed )
    {
        return request;
    }

    const ReplayRecorderStats stats = timeline.Presentation().GetStats();

    if ( !probe.runtimeResetCoverageInjected && stats.sampleCount >= 4 )
    {
        probe.runtimeResetCoverageInjected = true;
        probe.eventCoverageInjected = false;
        request.action = ReplayProbeSaveAction::ResetScene;
        return request;
    }

    if ( !probe.eventCoverageInjected && stats.sampleCount >= 4 )
    {
        probe.eventCoverageInjected = true;
        request.action = ReplayProbeSaveAction::InjectEventCoverage;
        return request;
    }

    if ( stats.sampleCount < static_cast<std::size_t>( probe.minSampleCount ) )
    {
        return request;
    }

    request.action = ReplayProbeSaveAction::ValidateArtifact;
    strcpy_s( request.path, sizeof( request.path ), probe.path );
    return request;
}

void ReplayProbeRunner::CompleteSaveProbe( const ReplayProbeSaveRequest& request,
                                           const SkullbonezCore::Core::SbResult& result )
{
    if ( request.action == ReplayProbeSaveAction::ValidateArtifact && result.Ok() )
    {
        m_probes.save.completed = true;
    }
}

SkullbonezCore::Core::SbResult ReplayProbeRunner::CurrentFailure() const
{
    return m_probes.Failed() ? m_resultDiagnostics.Failure( m_probes.FailureOwner(), m_probes.FailureMessage() )
                             : SkullbonezCore::Core::SbResult::Success();
}

void ReplayProbeRunner::RecordFailure( const SkullbonezCore::Core::SbResult& result )
{
    m_probes.RecordFailure( result );
}

SkullbonezCore::Core::SbResult ReplayProbeRunner::VerifyLoadedPresentationBeforeActivation(
    ReplayTimeline& timeline, ReplayScrubber& scrubber, ReplayPresentation& presentation, ReplayAuthoring& authoring,
    ReplayPrediction& prediction, ReplayPredictionPresentation& predictionPresentation, SceneWorld& world,
    EditorToolsOwner& editorTools, RuntimeTools& runtimeTools, std::size_t& outVisualPacketCount,
    std::size_t& outVisualPredictionBytes )
{
    outVisualPacketCount = 0;
    outVisualPredictionBytes = 0;

    if ( prediction.GenerationPermitted() )
    {
        return ReplayProbeFailure( "replay load probe did not disable prediction generation" );
    }

    const auto hasLoadedPresentation = [&timeline]()
    { return timeline.LoadedPresentation().enabled && timeline.LoadedPresentation().samples.size() >= 2; };

    if ( !hasLoadedPresentation() )
    {
        return ReplayProbeFailure( "replay load probe requires a loaded v2 presentation artifact" );
    }

    std::vector<ReplayVisualArchiveSample> visualPackets;
    const bool hasVisualPackets = ReplayV2Artifact::LoadVisualPackets( timeline.LoadedPresentation().path, visualPackets );
    outVisualPacketCount = visualPackets.size();

    if ( hasVisualPackets )
    {
        for ( std::size_t index = 0; index < visualPackets.size(); ++index )
        {
            const ReplayVisualArchiveSample& packet = visualPackets[index];

            if ( packet.revealFrame != index || packet.sourceFrame == 0 || packet.semanticHash == 0 ||
                 packet.exactPacketHash == 0 )
            {
                return ReplayProbeFailure( "replay load probe found an invalid durable visual-packet row" );
            }
        }

        std::vector<uint8_t> visualPredictionState;

        if ( !ReplayV2Artifact::LoadVisualPredictionState( timeline.LoadedPresentation().path, visualPredictionState ) )
        {
            return ReplayProbeFailure( "replay load probe could not load the durable prediction state" );
        }

        outVisualPredictionBytes = visualPredictionState.size();
        char archiveReason[192] = {};
        RunReplayPathVisualizerState archivePath;

        if ( !prediction.LoadArchive( visualPredictionState, archivePath, archiveReason, sizeof( archiveReason ) ) )
        {
            return m_resultDiagnostics.Failure( REPLAY_PROBE_OWNER, "replay prediction archive rejected: %s",
                                                archiveReason );
        }

        // Invariant: this loop calls only the presentation half of replay. The
        // loaded state remains presentation-visible, while the capability gate
        // forbids BeginReplayPredictionJob even if a later edit regresses that bit.
        EditorTracer& tracer = runtimeTools.Tracer();
        presentation.ApplyArchivePathState( archivePath );
        predictionPresentation.ResetTrajectoryVisualStats();
        prediction.ResetVerificationMarkers();

        // The archive retains the final marker prefix exactly. This optional
        // presenting Debug probe deliberately replays first appearance from
        // frame zero, so only the probe resets publication state.
        std::vector<ReplayVisualTrajectoryDigestState> trajectoryDigests;
        ReplayVisualPacketOperations::ReplayVisualTopologyVersionCanonicalizer topologyVersions;

        for ( const ReplayVisualArchiveSample& expected : visualPackets )
        {
            prediction.SetVerificationRevealFrame( expected.revealFrame );
            tracer.Clear();
            PrepareReplayProbePredictionPresentation( timeline, scrubber, presentation, prediction, predictionPresentation,
                                                      world.Physics(), world.Entities() );

            // Invariant: this cold probe runs production overlay and
            // publication phases directly. Every borrowed reference is loop-local;
            // no probe-only parameter packet can become a second owner graph.
            const ReplayPredictionPresentationView predictionView = prediction.PresentationView();
            const ReplaySolverFrameSample* currentSolver = ReplayProbeCurrentSolverSample( timeline, scrubber, prediction );

            const ReplaySolverFrameSample* presentSolver = currentSolver ? currentSolver : timeline.Solver().LatestSample();

            predictionPresentation.RenderPathVisualizer( predictionView, presentation.PathVisualizer(), presentSolver,
                                                         world.Physics(), world.Entities(), tracer );

            predictionPresentation.RenderCauseFocusOverlay( presentation.CameraView(), authoring.CauseTree(), predictionView,
                                                            currentSolver, world.BodyStore(),
                                                            PhysicsEngine::ReadColliders( world.Physics() ),
                                                            world.Entities(), tracer );

            const RunReplayPathVisualizerState& path = presentation.PathVisualizer();
            ReplayVelocityOverlayCommand velocityOverlay;

            if ( authoring.BuildVelocityOverlayCommand( path.targetId, path.targetModelRow, world.Physics(),
                                                        editorTools.Editor().editorModeEnabled, ReplayToolGestureView {},
                                                        velocityOverlay ) )
            {
                tracer.AddReplayVelocityGizmo( velocityOverlay.origin, velocityOverlay.orientation, velocityOverlay.shape,
                                               velocityOverlay.radius, velocityOverlay.linearVelocity,
                                               velocityOverlay.angularVelocity, velocityOverlay.hotLinearAxis,
                                               velocityOverlay.hotAngularAxis, velocityOverlay.activeAxis,
                                               velocityOverlay.activeAngular );
            }

            (void)predictionPresentation.BuildGhostDrawRequests( predictionView, world.RenderPresentationRecords(),
                                                                 world.BodyStore() );

            ReplayVisualPacket packet = tracer.BuildReplayVisualPacket( expected.cameraEye, expected.cameraUp );
            predictionPresentation.PublishVisualPacket( packet, predictionView, presentation.PathVisualizer().targetId,
                                                        timeline.Solver().LatestSample(),
                                                        expected.replayReserveGrowthEvents );

            ReplayVisualPacket projected = predictionPresentation.PublishedVisualPacketView();
            projected.header.topologyVersion = topologyVersions.Observe( projected.header.topologyVersion );
            const ReplayVisualPacketFingerprint fingerprint = BuildReplayVisualPacketFingerprint( projected,
                                                                                                  trajectoryDigests );

            if ( fingerprint.visualStateHash != expected.visualStateHash )
            {
                return m_resultDiagnostics
                    .Failure( REPLAY_PROBE_OWNER,
                              "visual packet state mismatch at reveal %llu: expected=0x%016llX actual=0x%016llX",
                              static_cast<unsigned long long>( expected.revealFrame ),
                              static_cast<unsigned long long>( expected.visualStateHash ),
                              static_cast<unsigned long long>( fingerprint.visualStateHash ) );
            }

            char difference[192] = {};

            if ( !ReplayVisualPacketMatchesArchiveSample( projected, expected, difference, sizeof( difference ) ) )
            {
                return m_resultDiagnostics.Failure( REPLAY_PROBE_OWNER, "%s", difference );
            }
        }
    }

    return SkullbonezCore::Core::SbResult::Success();
}


SkullbonezCore::Core::SbResult ReplayProbeRunner::VerifyLoadedPresentationAfterActivation(
    ReplayTimeline& timeline, ReplayScrubber& scrubber, ReplayPresentation& presentation, SceneWorld& world,
    std::size_t visualPacketCount, std::size_t visualPredictionBytes )
{
    auto distanceSquared = []( const Math::Vector::Vector3& a, const Math::Vector::Vector3& b ) -> float
    {
        const Math::Vector::Vector3 delta = a - b;

        return delta.x * delta.x + delta.y * delta.y + delta.z * delta.z;
    };

    const auto hasLoadedPresentation = [&timeline]()
    { return timeline.LoadedPresentation().enabled && timeline.LoadedPresentation().samples.size() >= 2; };

    const auto& loaded = timeline.LoadedPresentation();
    const ReplayPresentationSample*
        selected = scrubber.View().historicalSamplePaused
                       ? &loaded.samples[(std::min)( loaded.samples.size() - 1,
                                                     static_cast<std::size_t>(
                                                         std::clamp( scrubber.TrackPosition( RunReplayTrack::Presentation ),
                                                                     0.0f, 1.0f ) *
                                                             static_cast<float>( loaded.samples.size() - 1 ) +
                                                         0.5f ) )]
                       : nullptr;

    const ReplayPresentationSample* latest = hasLoadedPresentation() ? &loaded.samples.back() : nullptr;

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

    const int probedModelIndex = selectedBody->modelRow.value;
    const PhysicsBodyRecord* probedBody = TryGetReplayProbeBodyRecord( world, probedModelIndex );

    PhysicsBodyHotState probedHotState;

    if ( !probedBody || !TryGetReplayProbeBodyHotState( world, probedModelIndex, probedHotState ) )
    {
        return ReplayProbeFailure( "replay load probe loaded an invalid body index" );
    }

    const Math::Vector::Vector3 preApplyPosition = probedHotState.position;
    const bool applied = ApplyReplayProbePresentationSampleForRender( world, presentation, *selected );

    if ( !applied )
    {
        return ReplayProbeFailure( "replay load probe failed to apply the selected loaded v2 sample" );
    }

    const PhysicsBodyRecord* appliedBody = TryGetReplayProbeBodyRecord( world, probedModelIndex );

    PhysicsBodyHotState appliedHotState;

    if ( !appliedBody || !TryGetReplayProbeBodyHotState( world, probedModelIndex, appliedHotState ) )
    {
        RestoreReplayProbeRenderInstances( world );
        return ReplayProbeFailure( "replay load probe lost the selected body after applying the v2 sample" );
    }

    const Math::Vector::Vector3 liveAfterApplyPosition = appliedHotState.position;
    const float livePreservedDeltaSquared = distanceSquared( liveAfterApplyPosition, preApplyPosition );

    if ( livePreservedDeltaSquared > 0.0001f )
    {
        RestoreReplayProbeRenderInstances( world );
        return ReplayProbeFailure( "replay load probe mutated the live body while applying the v2 sample" );
    }

    Math::Vector::Vector3 appliedRenderPosition;

    if ( !TryPrepareReplayProbeRenderPosition( world, probedModelIndex, appliedRenderPosition ) )
    {
        RestoreReplayProbeRenderInstances( world );
        return ReplayProbeFailure( "replay load probe lost the selected render instance after applying the v2 sample" );
    }

    const float appliedDeltaSquared = distanceSquared( appliedRenderPosition, selectedBody->position );

    if ( appliedDeltaSquared > 0.0001f )
    {
        RestoreReplayProbeRenderInstances( world );
        return ReplayProbeFailure( "replay load probe did not move the render instance to the selected loaded v2 sample" );
    }

    RestoreReplayProbeRenderInstances( world );
    const PhysicsBodyRecord* restoredBody = TryGetReplayProbeBodyRecord( world, probedModelIndex );

    PhysicsBodyHotState restoredHotState;

    if ( !restoredBody || !TryGetReplayProbeBodyHotState( world, probedModelIndex, restoredHotState ) )
    {
        return ReplayProbeFailure( "replay load probe lost the selected body after restoring the v2 sample" );
    }

    const Math::Vector::Vector3 restoredPosition = restoredHotState.position;
    const float restoredDeltaSquared = distanceSquared( restoredPosition, preApplyPosition );

    if ( restoredDeltaSquared > 0.0001f )
    {
        return ReplayProbeFailure( "replay load probe live body changed after applying the selected loaded v2 sample" );
    }

    printf( "[replay] Load probe passed: prediction_generation=disabled visual_packets=%llu prediction_bytes=%llu "
            "path=%s samples=%llu bodies=%llu first_frame=%llu selected_frame=%llu "
            "latest_frame=%llu body_id=%u distance_sq=%.6f\n",
            static_cast<unsigned long long>( visualPacketCount ), static_cast<unsigned long long>( visualPredictionBytes ),
            timeline.LoadedPresentation().path,
            static_cast<unsigned long long>( timeline.LoadedPresentation().samples.size() ),
            static_cast<unsigned long long>( timeline.LoadedPresentation().bodyDictionaryCount ),
            static_cast<unsigned long long>( timeline.LoadedPresentation().firstFrame ),
            static_cast<unsigned long long>( selected->frameIndex ), static_cast<unsigned long long>( latest->frameIndex ),
            selectedBody->id.value, bestDistanceSquared );

    return SkullbonezCore::Core::SbResult::Success();
}

SkullbonezCore::Core::SbResult
ReplayProbeRunner::PrepareCheckpointFileProbe( const char* path, ReplaySolverFrameSample& outCheckpoint,
                                               ReplayV2SolverCheckpointLoadResult& outLoadResult )
{
    if ( !path || path[0] == '\0' )
    {
        return ReplayProbeFailure( "replay restore file probe requires a v2 artifact path" );
    }

    std::vector<ReplaySolverFrameSample> checkpoints;

    if ( !ReplayV2Artifact::LoadSolverCheckpoints( path, checkpoints, &outLoadResult ) )
    {
        return ReplayProbeFailure( "replay restore file probe failed to load v2 solver checkpoints" );
    }

    if ( checkpoints.empty() )
    {
        return ReplayProbeFailure( "replay restore file probe found no v2 solver checkpoints" );
    }

    if ( checkpoints.front().eventCursor == 0 )
    {
        return ReplayProbeFailure( "replay restore file probe loaded a checkpoint without an event cursor" );
    }

    outCheckpoint = std::move( checkpoints.front() );
    return SkullbonezCore::Core::SbResult::Success();
}

SkullbonezCore::Core::SbResult
ReplayProbeRunner::CompleteCheckpointFileProbe( const char* path, const ReplaySolverFrameSample& checkpoint,
                                                const ReplayV2SolverCheckpointLoadResult& loadResult, bool restored,
                                                const char* reason )
{
    if ( !restored )
    {
        return m_resultDiagnostics.Failure( REPLAY_PROBE_OWNER, "replay restore file probe failed: %s",
                                            reason && reason[0] != '\0' ? reason : "unknown restore failure" );
    }

    printf( "[replay] Restore file probe passed: path=%s checkpoints=%llu first_frame=%llu target_frame=%llu "
            "event_cursor=%u bodies=%llu solver_hash=0x%016llX bytes=%llu\n",
            path, static_cast<unsigned long long>( loadResult.checkpointCount ),
            static_cast<unsigned long long>( loadResult.firstFrame ),
            static_cast<unsigned long long>( checkpoint.frameIndex ), checkpoint.eventCursor,
            static_cast<unsigned long long>( checkpoint.bodies.size() ),
            static_cast<unsigned long long>( checkpoint.solverHash ),
            static_cast<unsigned long long>( loadResult.fileBytes ) );

    return SkullbonezCore::Core::SbResult::Success();
}

SkullbonezCore::Core::SbResult ReplayProbeRunner::CompleteTargetFileProbe( const char* path,
                                                                           const RunReplayV2TargetRestoreResult& result,
                                                                           bool restored, const char* reason )
{
    if ( !restored )
    {
        return m_resultDiagnostics.Failure( REPLAY_PROBE_OWNER, "replay restore target probe failed: %s",
                                            reason && reason[0] != '\0' ? reason : "unknown restore failure" );
    }

    printf( "[replay] Restore target probe passed: path=%s checkpoints=%llu events=%llu hashes=%llu "
            "checkpoint_frame=%llu target_frame=%llu event_cursor=%u events_applied=%llu bodies=%llu "
            "generated_topology_rebuilt=%d "
            "solver_hash=0x%016llX presentation_hash=0x%016llX bytes=%llu\n",
            path, static_cast<unsigned long long>( result.checkpointCount ),
            static_cast<unsigned long long>( result.eventCount ), static_cast<unsigned long long>( result.hashCount ),
            static_cast<unsigned long long>( result.checkpointFrame ), static_cast<unsigned long long>( result.targetFrame ),
            result.eventCursor, static_cast<unsigned long long>( result.eventsApplied ),
            static_cast<unsigned long long>( result.bodyCount ), result.generatedTopologyRebuilt ? 1 : 0,
            static_cast<unsigned long long>( result.solverHash ), static_cast<unsigned long long>( result.presentationHash ),
            static_cast<unsigned long long>( result.fileBytes ) );

    PostQuitMessage( 0 );
    return SkullbonezCore::Core::SbResult::Success();
}

ReplayFailureProbeRequest ReplayProbeRunner::BeginFailureFileProbe( const char* path )
{
    constexpr ReplayFrameIndex MISSING_TARGET_FRAME = 999999999u;
    m_failureFile = ReplayFailureFileProbeState {};

    strcpy_s( m_failureFile.path, sizeof( m_failureFile.path ), path ? path : "" );
    m_failureFile.missingTargetFrame = MISSING_TARGET_FRAME;

    ReplayFailureProbeRequest request;
    request.action = ReplayFailureProbeAction::RestoreMissingTarget;
    request.targetFrame = MISSING_TARGET_FRAME;
    return request;
}

ReplayFailureProbeRequest ReplayProbeRunner::AdvanceFailureFileProbe( const ReplayFailureProbeRequest& request,
                                                                      const ReplayFailureProbeStepResult& result )
{
    // Concept: the runner owns expected-failure order and verdicts; the replay
    // composition root merely executes the requested restore/capture primitive.
    ReplayFailureProbeRequest next;
    next.rollbackReference = request.rollbackReference;

    if ( request.action == ReplayFailureProbeAction::RestoreMissingTarget )
    {
        if ( result.succeeded )
        {
            next.status = ReplayProbeFailure( "replay restore failure probe unexpectedly restored a missing target frame" );

            return next;
        }

        if ( !result.reason || strstr( result.reason, "found no saved hash for requested target frame" ) == nullptr )
        {
            next.status = m_resultDiagnostics.Failure( REPLAY_PROBE_OWNER,
                                                       "replay restore failure probe produced an unexpected reason: %s",
                                                       result.reason && result.reason[0] != '\0'
                                                           ? result.reason
                                                           : "unknown restore failure" );

            return next;
        }

        strcpy_s( m_failureFile.missingTargetReason, sizeof( m_failureFile.missingTargetReason ), result.reason );
        next.action = ReplayFailureProbeAction::CaptureRollbackSample;
        return next;
    }

    if ( request.action == ReplayFailureProbeAction::CaptureRollbackSample )
    {
        if ( !result.succeeded || !result.capturedSample )
        {
            next.status = ReplayProbeFailure( "replay restore failure probe could not capture the live rollback sample" );

            return next;
        }

        next.action = ReplayFailureProbeAction::RestoreCorruptedTarget;
        next.rollbackReference = result.capturedSample;
        next.forceHashMismatch = true;
        return next;
    }

    if ( request.action == ReplayFailureProbeAction::RestoreCorruptedTarget )
    {
        if ( result.succeeded )
        {
            next.status = ReplayProbeFailure( "replay restore hash-failure probe unexpectedly restored a corrupted target" );

            return next;
        }

        if ( !result.reason || strstr( result.reason, "solver hash mismatch" ) == nullptr )
        {
            next.status = m_resultDiagnostics.Failure( REPLAY_PROBE_OWNER,
                                                       "replay restore hash-failure probe produced an unexpected reason: %s",
                                                       result.reason && result.reason[0] != '\0'
                                                           ? result.reason
                                                           : "unknown restore failure" );

            return next;
        }

        strcpy_s( m_failureFile.hashFailureReason, sizeof( m_failureFile.hashFailureReason ), result.reason );
        next.action = ReplayFailureProbeAction::CaptureRollbackHash;
        return next;
    }

    if ( request.action == ReplayFailureProbeAction::CaptureRollbackHash )
    {
        if ( !result.succeeded || !request.rollbackReference )
        {
            next.status = ReplayProbeFailure(
                "replay restore hash-failure probe could not capture the rolled-back live solver" );

            return next;
        }

        if ( result.solverHash != request.rollbackReference->solverHash )
        {
            next.status = m_resultDiagnostics
                              .Failure( REPLAY_PROBE_OWNER,
                                        "replay restore hash-failure probe did not roll back the live solver: "
                                        "restored=0x%016llX expected=0x%016llX",
                                        static_cast<unsigned long long>( result.solverHash ),
                                        static_cast<unsigned long long>( request.rollbackReference->solverHash ) );

            return next;
        }

        printf( "[replay] Restore failure probe passed: path=%s missing_frame=%llu reason=\"%s\" "
                "rollback_solver_hash=0x%016llX hash_failure_reason=\"%s\"\n",
                m_failureFile.path, static_cast<unsigned long long>( m_failureFile.missingTargetFrame ),
                m_failureFile.missingTargetReason, static_cast<unsigned long long>( result.solverHash ),
                m_failureFile.hashFailureReason );

        return next;
    }

    next.status = ReplayProbeFailure( "replay restore failure probe received an unknown action" );
    return next;
}

SkullbonezCore::Core::SbResult ReplayProbeRunner::BeginBranchFileProbe( ReplayTimeline& timeline, const char* path )
{
    if ( !timeline.LoadPresentationArtifact( path ) )
    {
        return ReplayProbeFailure( "replay restore branch probe failed to load v2 presentation scrub source" );
    }

    return SkullbonezCore::Core::SbResult::Success();
}


SkullbonezCore::Core::SbResult ReplayProbeRunner::CompleteBranchFileProbePreparation( ReplayTimeline& timeline,
                                                                                      ReplayScrubber& scrubber, double now,
                                                                                      ReplayLiveRestoreRequest& outRequest )
{

    scrubber.SetHistoricalSamplePaused( true );
    scrubber.SelectTrack( RunReplayTrack::Presentation );
    scrubber.SetTrackPosition( RunReplayTrack::Presentation, 1.0f );

    char reason[256] = {};
    ReplayScrubberRestoreSources sources;
    const RunLoadedReplayPresentationState& loaded = timeline.LoadedPresentation();
    sources.hasLoadedPresentation = loaded.enabled && loaded.samples.size() >= 2;
    sources.presentationSample = sources.hasLoadedPresentation ? &loaded.samples.back() : nullptr;
    sources.loadedPresentationPath = loaded.path;

    if ( !scrubber.BuildRestoreRequest( sources, now, outRequest, reason, sizeof( reason ) ) )
    {
        return m_resultDiagnostics.Failure( REPLAY_PROBE_OWNER, "replay restore branch probe failed: %s",
                                            reason[0] != '\0' ? reason : "failed to build restore request" );
    }

    return SkullbonezCore::Core::SbResult::Success();
}

SkullbonezCore::Core::SbResult ReplayProbeRunner::CompleteBranchFileProbe( const char* path,
                                                                           const ReplayLiveRestoreOutcome& outcome )
{
    const RunReplayV2TargetRestoreResult& result = outcome.v2Result;

    if ( !outcome.restored )
    {
        return m_resultDiagnostics.Failure( REPLAY_PROBE_OWNER, "replay restore branch probe failed: %s",
                                            outcome.reason[0] != '\0' ? outcome.reason : "unknown restore failure" );
    }

    if ( !result.madeLiveBranch || result.branchId == 0 )
    {
        return ReplayProbeFailure( "replay restore branch probe did not create a scrubber live branch" );
    }

    printf( "[replay] Restore branch probe passed: path=%s checkpoints=%llu events=%llu hashes=%llu "
            "checkpoint_frame=%llu target_frame=%llu event_cursor=%u events_applied=%llu bodies=%llu "
            "generated_topology_rebuilt=%d "
            "branch_id=%u parent_branch_id=%u solver_hash=0x%016llX presentation_hash=0x%016llX bytes=%llu\n",
            path, static_cast<unsigned long long>( result.checkpointCount ),
            static_cast<unsigned long long>( result.eventCount ), static_cast<unsigned long long>( result.hashCount ),
            static_cast<unsigned long long>( result.checkpointFrame ), static_cast<unsigned long long>( result.targetFrame ),
            result.eventCursor, static_cast<unsigned long long>( result.eventsApplied ),
            static_cast<unsigned long long>( result.bodyCount ), result.generatedTopologyRebuilt ? 1 : 0, result.branchId,
            result.parentBranchId, static_cast<unsigned long long>( result.solverHash ),
            static_cast<unsigned long long>( result.presentationHash ),
            static_cast<unsigned long long>( result.fileBytes ) );

    return SkullbonezCore::Core::SbResult::Success();
}

void ReplayProbeRunner::ConfigureDebug( const ReplayStartupRequest& request )
{
    auto copyPath = []( char* destination, std::size_t destinationSize, const char* source )
    {
        if ( source && source[0] != '\0' )
        {
            strncpy_s( destination, destinationSize, source, _TRUNCATE );
        }
    };

    copyPath( m_startup.checkpointProbePath, sizeof( m_startup.checkpointProbePath ), request.checkpointProbePath );
    copyPath( m_startup.targetProbePath, sizeof( m_startup.targetProbePath ), request.targetProbePath );
    copyPath( m_startup.branchProbePath, sizeof( m_startup.branchProbePath ), request.branchProbePath );
    copyPath( m_startup.failureProbePath, sizeof( m_startup.failureProbePath ), request.failureProbePath );

    // Probe assertion lane: launch configuration is an owner command. Run
    // supplies value-only CLI facts and cannot mutate completion/failure state.
    if ( request.scrubProbe )
    {
        m_probes.scrub.enabled = true;
        m_probes.scrub.completed = false;
        m_probes.scrub.normalized = std::clamp( request.scrubProbeNormalized, 0.0f, 0.99f );
        printf( "[replay] Scrub probe enabled: normalized=%.3f\n", m_probes.scrub.normalized );
    }

    if ( request.restoreProbe )
    {
        m_probes.restore.enabled = true;
        m_probes.restore.completed = false;
        m_probes.restore.normalized = std::clamp( request.restoreProbeNormalized, 0.0f, 0.99f );
        printf( "[replay] Restore probe enabled: normalized=%.3f\n", m_probes.restore.normalized );
    }

    if ( request.saveProbe )
    {
        if ( !request.saveProbePath || request.saveProbePath[0] == '\0' )
        {
            m_probes.RecordFailure(
                m_resultDiagnostics.Failure( "ReplayProbe", "replay save probe requires an output path" ) );
        }
        else
        {
            m_probes.save.enabled = true;
            m_probes.save.completed = false;
            strcpy_s( m_probes.save.path, sizeof( m_probes.save.path ), request.saveProbePath );
            printf( "[replay] Save probe enabled: path=%s\n", m_probes.save.path );
        }
    }
}

void ReplayStartupProbeContinuation::AdvanceOrFatal( Phase next, const char* operation )
{
    RequireLegalTransitionOrFatal( m_phase, next, operation );

    m_phase = next;
}


ReplayStartupResult ReplayRuntime::AdvanceStartupProbeWorkflows(
    ReplayStartupProbeContinuation& continuation, SceneController& sceneController, DiagnosticsRuntime& diagnosticsRuntime,
    OverlayDebugState& debug, EditorToolsOwner& editorTools, RuntimeTools& runtimeTools, SimulationSystem& simulation,
    const SkullbonezCore::Core::EngineConfig& config, SkullbonezCore::Assets::AssetSystem& assets,
    SkullbonezCore::Threading::WorkerPool& workerPool, const ReplayStartupProbeSceneOverrides& sceneOverrides )
{
    ReplayStartupResult result;
    const ReplayStartupWorkflowState& startup = m_probeRunner.Startup();
    result.skipExecute = startup.loadProbe || startup.checkpointProbePath[0] != '\0' || startup.targetProbePath[0] != '\0' ||
                         startup.branchProbePath[0] != '\0' || startup.failureProbePath[0] != '\0';

    if ( continuation.m_phase == ReplayStartupProbeContinuation::Phase::Idle )
    {
        continuation.AdvanceOrFatal( ReplayStartupProbeContinuation::Phase::Running, "Begin" );
    }
    else if ( continuation.m_phase == ReplayStartupProbeContinuation::Phase::ApplicationApplied )
    {
        continuation.AdvanceOrFatal( ReplayStartupProbeContinuation::Phase::Running, "Resume" );
    }
    else
    {
        SB_FATAL( "Runtime/ReplayStartupProbeContinuation",
                  "Startup probe advanced without a new or serviced continuation. phase=%u",
                  static_cast<unsigned int>( continuation.m_phase ) );
    }

    SceneWorld& world = sceneController.Scene();
    SceneSessionState& scene = sceneController.State();
    ReplaySceneTimelineResetInput timelineReset = ReplayTimelineOperations::
        DescribeReplaySceneTimeline( sceneController, sceneOverrides.uiOverrides, scene,
                                     SkullbonezCore::Core::ActiveSceneObjectCapacity( config ),
                                     static_cast<uint32_t>( sceneOverrides.generatedObjectType ) );
    strncpy_s( continuation.m_sceneLabel, timelineReset.sceneLabel ? timelineReset.sceneLabel : "generated", _TRUNCATE );
    timelineReset.sceneLabel = continuation.m_sceneLabel;

    auto acceptProbe = [&]( const SkullbonezCore::Core::SbResult& probeResult ) -> bool
    {
        if ( !probeResult.Ok() )
        {
            result.status = probeResult;
            continuation.AdvanceOrFatal( ReplayStartupProbeContinuation::Phase::Failed, "Fail" );

            return false;
        }

        result.skipExecute = true;
        return true;
    };

    if ( continuation.m_resume == ReplayStartupProbeContinuation::Resume::CompleteLoad )
    {
        continuation.m_resume = ReplayStartupProbeContinuation::Resume::None;

        if ( !acceptProbe(
                 m_probeRunner.VerifyLoadedPresentationAfterActivation( m_timeline, m_scrubberOwner, m_visualPresentation,
                                                                        world, continuation.m_loadedVisualPacketCount,
                                                                        continuation.m_loadedVisualPredictionBytes ) ) )
        {
            return result;
        }

        continuation.m_step = ReplayStartupProbeContinuation::Step::Checkpoint;
    }

    if ( continuation.m_step == ReplayStartupProbeContinuation::Step::Load )
    {
        if ( startup.loadProbe )
        {
            if ( startup.loadPath[0] == '\0' )
            {
                acceptProbe( m_resultDiagnostics.Failure( "ReplayProbe", "replay load probe requires a replay path" ) );
                return result;
            }

            if ( !acceptProbe(
                     m_probeRunner.VerifyLoadedPresentationBeforeActivation( m_timeline, m_scrubberOwner,
                                                                             m_visualPresentation, m_authoring,
                                                                             m_predictionOwner, m_predictionPresentation,
                                                                             world, editorTools, runtimeTools,
                                                                             continuation.m_loadedVisualPacketCount,
                                                                             continuation.m_loadedVisualPredictionBytes ) ) )
            {
                return result;
            }

            continuation.m_pendingAction = ReplayStartupProbeContinuation::PendingAction::ActivateLoadedPresentation;
            continuation.m_resume = ReplayStartupProbeContinuation::Resume::CompleteLoad;
            continuation.AdvanceOrFatal( ReplayStartupProbeContinuation::Phase::AwaitingApplication,
                                         "RequestLoadActivation" );
            return result;
        }

        continuation.m_step = ReplayStartupProbeContinuation::Step::Checkpoint;
    }

    if ( continuation.m_resume == ReplayStartupProbeContinuation::Resume::CompleteCheckpoint )
    {
        continuation.m_resume = ReplayStartupProbeContinuation::Resume::None;
        continuation.m_restoreRequest.solverSample = &continuation.m_checkpoint;
        CompleteLiveRestoreScrubber( *continuation.m_restore, continuation.m_restoreRequest, continuation.m_restoreOutcome );
        continuation.m_restoreRequest.solverSample = nullptr;
        const SkullbonezCore::Core::SbResult
            probeResult = m_probeRunner.CompleteCheckpointFileProbe( startup.checkpointProbePath, continuation.m_checkpoint,
                                                                     continuation.m_checkpointLoadResult,
                                                                     continuation.m_restoreOutcome.restored,
                                                                     continuation.m_restoreOutcome.reason );

        if ( !acceptProbe( probeResult ) )
        {
            return result;
        }

        continuation.m_step = ReplayStartupProbeContinuation::Step::Target;
    }

    if ( continuation.m_step == ReplayStartupProbeContinuation::Step::Checkpoint )
    {
        if ( startup.checkpointProbePath[0] != '\0' )
        {
            SkullbonezCore::Core::SbResult
                probeResult = m_probeRunner.PrepareCheckpointFileProbe( startup.checkpointProbePath,
                                                                        continuation.m_checkpoint,
                                                                        continuation.m_checkpointLoadResult );

            if ( probeResult.Ok() )
            {
                continuation.m_restore.emplace( timelineReset );
                const bool restored = RestoreSolverSampleAsLive( *continuation.m_restore, world, scene, debug, runtimeTools,
                                                                 continuation.m_checkpoint );

                continuation.m_restoreRequest = ReplayLiveRestoreRequest {};
                continuation.m_restoreRequest.kind = ReplayLiveRestoreKind::SolverSample;
                continuation.m_restoreOutcome = ReplayLiveRestoreOutcome {};
                continuation.m_restoreOutcome.restored = restored;
                strncpy_s( continuation.m_restoreOutcome.reason, continuation.m_restore->FailureReason(), _TRUNCATE );
                PublishRestoreDiagnostic( *continuation.m_restore, diagnosticsRuntime, scene );
                continuation.m_pendingAction = ReplayStartupProbeContinuation::PendingAction::ApplyRestoredBranchTimeline;
                continuation.m_resume = ReplayStartupProbeContinuation::Resume::CompleteCheckpoint;
                continuation.AdvanceOrFatal( ReplayStartupProbeContinuation::Phase::AwaitingApplication,
                                             "RequestCheckpointApplication" );
                return result;
            }

            if ( !acceptProbe( probeResult ) )
            {
                return result;
            }
        }

        continuation.m_step = ReplayStartupProbeContinuation::Step::Target;
    }

    if ( continuation.m_step == ReplayStartupProbeContinuation::Step::Target )
    {
        if ( startup.targetProbePath[0] != '\0' )
        {
            ReplayLiveRestoreRequest restoreRequest;
            restoreRequest.kind = ReplayLiveRestoreKind::V2ArtifactTarget;
            strncpy_s( restoreRequest.path, startup.targetProbePath, _TRUNCATE );
            restoreRequest.requestedFrame = ( std::numeric_limits<ReplayFrameIndex>::max )();
            ReplayRestoreTransaction transaction { timelineReset };
            transaction.SetArtifactRequest( restoreRequest );
            const bool restored = RestoreV2ArtifactTargetState( transaction, sceneController, debug, editorTools,
                                                                runtimeTools, simulation, config, assets, workerPool,
                                                                sceneOverrides.uiOverrides,
                                                                sceneOverrides.generatedObjectType );

            PublishRestoreDiagnostic( transaction, diagnosticsRuntime, scene );

            if ( !acceptProbe( m_probeRunner.CompleteTargetFileProbe( startup.targetProbePath, transaction.Result(),
                                                                      restored, transaction.FailureReason() ) ) )
            {
                return result;
            }
        }

        continuation.m_step = ReplayStartupProbeContinuation::Step::Branch;
    }

    if ( continuation.m_resume == ReplayStartupProbeContinuation::Resume::CompleteBranchPreparation )
    {
        continuation.m_resume = ReplayStartupProbeContinuation::Resume::None;
        SkullbonezCore::Core::SbResult
            probeResult = m_probeRunner.CompleteBranchFileProbePreparation( m_timeline, m_scrubberOwner,
                                                                            continuation.m_applicationTimeSeconds,
                                                                            continuation.m_restoreRequest );

        if ( probeResult.Ok() )
        {
            continuation.m_restore.emplace( timelineReset );
            continuation.m_restore->SetArtifactRequest( continuation.m_restoreRequest );
            const bool restored = RestoreV2ArtifactTargetState( *continuation.m_restore, sceneController, debug, editorTools,
                                                                runtimeTools, simulation, config, assets, workerPool,
                                                                sceneOverrides.uiOverrides,
                                                                sceneOverrides.generatedObjectType );

            continuation.m_restoreOutcome = ReplayLiveRestoreOperations::BuildOutcome( *continuation.m_restore,
                                                                                       continuation.m_restoreRequest.kind,
                                                                                       restored );
            PublishRestoreDiagnostic( *continuation.m_restore, diagnosticsRuntime, scene );
            continuation.m_pendingAction = ReplayStartupProbeContinuation::PendingAction::ApplyRestoredBranchTimeline;
            continuation.m_resume = ReplayStartupProbeContinuation::Resume::CompleteBranchRestore;
            continuation.AdvanceOrFatal( ReplayStartupProbeContinuation::Phase::AwaitingApplication,
                                         "RequestBranchRestoreApplication" );
            return result;
        }

        if ( !acceptProbe( probeResult ) )
        {
            return result;
        }
    }

    if ( continuation.m_resume == ReplayStartupProbeContinuation::Resume::CompleteBranchRestore )
    {
        continuation.m_resume = ReplayStartupProbeContinuation::Resume::None;
        CompleteLiveRestoreScrubber( *continuation.m_restore, continuation.m_restoreRequest, continuation.m_restoreOutcome );

        if ( !acceptProbe(
                 m_probeRunner.CompleteBranchFileProbe( startup.branchProbePath, continuation.m_restoreOutcome ) ) )
        {
            return result;
        }

        continuation.m_step = ReplayStartupProbeContinuation::Step::Failure;
    }

    if ( continuation.m_step == ReplayStartupProbeContinuation::Step::Branch )
    {
        if ( startup.branchProbePath[0] != '\0' )
        {
            if ( !acceptProbe( m_probeRunner.BeginBranchFileProbe( m_timeline, startup.branchProbePath ) ) )
            {
                return result;
            }

            continuation.m_pendingAction = ReplayStartupProbeContinuation::PendingAction::ActivateLoadedPresentation;
            continuation.m_resume = ReplayStartupProbeContinuation::Resume::CompleteBranchPreparation;
            continuation.AdvanceOrFatal( ReplayStartupProbeContinuation::Phase::AwaitingApplication,
                                         "RequestBranchActivation" );
            return result;
        }

        continuation.m_step = ReplayStartupProbeContinuation::Step::Failure;
    }

    if ( continuation.m_step == ReplayStartupProbeContinuation::Step::Failure && startup.failureProbePath[0] != '\0' )
    {
        ReplaySolverFrameSample liveBackup;
        ReplayFailureProbeRequest request = m_probeRunner.BeginFailureFileProbe( startup.failureProbePath );

        // Lifetime: each result is consumed immediately. The request may borrow
        // liveBackup only until this synchronous startup loop finishes.
        while ( request.status.Ok() && request.action != ReplayFailureProbeAction::None )
        {
            ReplayFailureProbeStepResult step;
            char reason[256] = {};

            if ( request.action == ReplayFailureProbeAction::RestoreMissingTarget )
            {
                ReplayLiveRestoreRequest restoreRequest;
                restoreRequest.kind = ReplayLiveRestoreKind::V2ArtifactTarget;
                restoreRequest.requestedFrame = request.targetFrame;
                strncpy_s( restoreRequest.path, startup.failureProbePath, _TRUNCATE );
                ReplayRestoreTransaction transaction { timelineReset };
                transaction.SetArtifactRequest( restoreRequest );
                step.succeeded = RestoreV2ArtifactTargetState( transaction, sceneController, debug, editorTools,
                                                               runtimeTools, simulation, config, assets, workerPool,
                                                               sceneOverrides.uiOverrides,
                                                               sceneOverrides.generatedObjectType );

                PublishRestoreDiagnostic( transaction, diagnosticsRuntime, scene );
                strncpy_s( reason, transaction.FailureReason(), _TRUNCATE );
                step.reason = reason;
            }
            else if ( request.action == ReplayFailureProbeAction::CaptureRollbackSample )
            {
                ReplaySolverFrameSample liveReference;
                liveReference.physicsDt = PHYSICS_FIXED_DT;
                ReplayLauncherVisualSample launcherVisual;
                runtimeTools.BuildReplayLauncherVisualSample( launcherVisual );
                step.succeeded = ReplayRestoreOperations::CaptureCurrentSolverSample( world, scene, debug, launcherVisual,
                                                                                      liveReference, liveBackup );

                step.capturedSample = step.succeeded ? &liveBackup : nullptr;
            }
            else if ( request.action == ReplayFailureProbeAction::RestoreCorruptedTarget )
            {
                ReplayLiveRestoreRequest restoreRequest;
                restoreRequest.kind = ReplayLiveRestoreKind::V2ArtifactTarget;
                restoreRequest.requestedFrame = ( std::numeric_limits<ReplayFrameIndex>::max )();
                restoreRequest.injectTargetHashMismatchForProbe = request.forceHashMismatch;
                strncpy_s( restoreRequest.path, startup.failureProbePath, _TRUNCATE );
                ReplayRestoreTransaction transaction { timelineReset };
                transaction.SetArtifactRequest( restoreRequest );
                step.succeeded = RestoreV2ArtifactTargetState( transaction, sceneController, debug, editorTools,
                                                               runtimeTools, simulation, config, assets, workerPool,
                                                               sceneOverrides.uiOverrides,
                                                               sceneOverrides.generatedObjectType );

                PublishRestoreDiagnostic( transaction, diagnosticsRuntime, scene );
                strncpy_s( reason, transaction.FailureReason(), _TRUNCATE );
                step.reason = reason;
            }
            else if ( request.action == ReplayFailureProbeAction::CaptureRollbackHash )
            {
                uint64_t presentationHash = 0;
                std::size_t bodyCount = 0;
                ReplayLauncherVisualSample launcherVisual;
                runtimeTools.BuildReplayLauncherVisualSample( launcherVisual );
                step.succeeded = request.rollbackReference &&
                                 ReplayRestoreOperations::CaptureCurrentSolverHash( world, scene, debug, launcherVisual,
                                                                                    *request.rollbackReference,
                                                                                    step.solverHash, presentationHash,
                                                                                    bodyCount );
            }

            request = m_probeRunner.AdvanceFailureFileProbe( request, step );
        }

        if ( !acceptProbe( request.status ) )
        {
            return result;
        }
    }

    continuation.m_step = ReplayStartupProbeContinuation::Step::Complete;
    continuation.AdvanceOrFatal( ReplayStartupProbeContinuation::Phase::Complete, "Complete" );
    return result;
}


ReplayStartupResult ReplayRuntime::ApplyStartupProbeApplicationAction(
    ReplayStartupProbeContinuation& continuation, SceneController& sceneController, CameraControlState& camera,
    RunCameraMode normalizedCurrentMode, RunCameraMode normalizedRestoreMode, bool attachedFollow, bool directorGrabbed,
    RuntimeInteractionController& interaction, InputRouter& inputRouter, RunMousePickupState& mousePickup )
{
    ReplayStartupResult result;
    result.skipExecute = true;

    ReplayStartupProbeContinuation::RequireApplicationStateOrFatal( continuation.m_phase, continuation.m_pendingAction,
                                                                    continuation.m_restore.has_value(),
                                                                    "ApplyApplicationAction" );

    if ( continuation.m_pendingAction == ReplayStartupProbeContinuation::PendingAction::ActivateLoadedPresentation )
    {
        ReplayStartupResult activation;
        activation.applicationAction = ReplayStartupApplicationAction::ActivateLoadedPresentation;
        activation.applicationTimeSeconds = continuation.m_applicationTimeSeconds;
        activation.presentationTrackPosition = 0.25f;

        if ( !ApplyStartupApplicationAction( activation, sceneController, camera, normalizedCurrentMode,
                                             normalizedRestoreMode, attachedFollow, directorGrabbed, interaction,
                                             inputRouter, mousePickup ) )
        {
            result.status = m_resultDiagnostics
                                .Failure( "ReplayProbe",
                                          "replay startup probe could not arm the loaded presentation scrubber" );
            continuation.RejectPendingApplicationOrFatal( "RejectPresentationActivation" );
            return result;
        }
    }
    else if ( continuation.m_pendingAction == ReplayStartupProbeContinuation::PendingAction::ApplyRestoredBranchTimeline )
    {
        ApplyRestoredBranchTimeline( *continuation.m_restore, continuation.m_restoreOutcome, sceneController, inputRouter,
                                     interaction, camera, normalizedRestoreMode, attachedFollow, directorGrabbed );
    }
    else
    {
        SB_FATAL( "Runtime/ReplayStartupProbeContinuation", "Pending startup probe action is empty." );
    }

    continuation.m_pendingAction = ReplayStartupProbeContinuation::PendingAction::None;
    continuation.AdvanceOrFatal( ReplayStartupProbeContinuation::Phase::ApplicationApplied,
                                 "ApplyPendingApplicationAction" );
    return result;
}
