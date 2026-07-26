/*
File: SkullbonezSource/Runtime/Prediction/ReplayPrediction.cpp
Purpose:
  Owns isolated replay simulation setup and frame-thread prediction orchestration.

Summary:
  Replay tools read two timelines. Retained solver samples describe what already
  happened; prediction samples advance a private prediction-owned physics
  engine.
  Frame update prepares private-engine work, delegates worker lifetime to the
  schedule owner, and delegates trajectory/topology publication before drawing.

Glossary:
  Path visualizer: Overlay that draws past/future body trajectories and contact
    handoffs.
  Replay target marker: Overlay outline/ring drawn around the replay-selected
    body from live body/collider store rows.
  Prediction slice: Bounded worker chunk that advances the private prediction
    engine and publishes a coherent frame prefix.
  Live edit replacement: Coalesced held-drag generation that must publish and
    promote one coherent prefix before a newer velocity can replace it.
  Prediction physics tick: Prediction-owned fixed step against the private
    prediction engine.
  Future node: Body discovered by following contacts or predicted movement
    outward from a selected root body.
  Physics::PhysicsSceneObjectId: Stable runtime id used across retained samples even when vector
    indices are only local hints.
  Model row hint: Cached live body row paired with Physics::PhysicsSceneObjectId; replay tools
    may keep it only as a repairable lookup shortcut.
  Solver snapshot: Physics cache state that must be restored to make the next
    fixed step reproduce.
  WorkerPool: Persistent engine worker threads used for fork-join loops and
    amortized replay prediction chunks.

Invariants:
  - Prediction must never write live physics stores; private engine state owns
    all future ticks and samples.
  - Prediction stepping is single-writer worker work; the render frame consumes
    only the published prefix and fixed trajectory slots.
  - Drawing is implemented in ReplayPredictionDrawing.cpp and cannot start,
    advance, cancel, or complete prediction work.
  - Physics steps stay serial per prediction engine; body capture may fan out
    only after the step, and each worker writes a distinct pre-sized frame row.
  - Scheduling/cancellation and release/acquire publication each have one named
    owner; this coordinator does not manipulate their atomics or task storage.

Related:
  - SkullbonezSource/Runtime/App/ReplayScrubberTools.cpp
  - SkullbonezSource/Runtime/Prediction/ReplayAuthoringCauseTree.cpp
  - SkullbonezSource/Runtime/Prediction/ReplayPredictionDrawing.cpp
  - SkullbonezSource/Runtime/Prediction/ReplayPredictionScheduling.cpp
  - SkullbonezSource/Runtime/Prediction/ReplayPredictionPublication.cpp
  - Agentic/Reference/runtime-reference.md
  - Agentic/Reference/comment-style-guide.md
*/
#include "ReplayPrediction.h"
#include "../Scene/SceneEntityStore.h"
#include "../Editor/EditorHullAssets.h"
#include "../Replay/ReplayOverlayLayout.h"
#include "ReplayPredictionArchive.h"
#include "ReplayPredictionPublicationOperations.h"
#include "ReplayPredictionReserve.h"
#include "../Replay/ReplayScrubber.h"
#include "../../Core/Allocation/RuntimeAllocationTracker.h"
#include "../../Core/Allocation/RuntimeReserveAllocator.h"
#include "../../Physics/ColliderStore.h"
#include "../../Physics/PhysicsBodyStore.h"
#include "../../Physics/PhysicsApi.h"
#include "../../Physics/PhysicsEngine.h"
#include "../../Physics/PhysicsMass.h"
#include "../../Physics/PhysicsTimestep.h"
#include "../../Core/AmortizedTask.h"
#include "../../Core/Config.h"
#include "../../Core/SceneCapacity.h"
#include "../../Core/WorkerPool.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cfloat>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <memory>
#include <thread>

using namespace SkullbonezCore::Runtime;
using namespace SkullbonezCore::Runtime::ReplayPredictionArchiveOperations;
using namespace SkullbonezCore::Runtime::ReplayPredictionPublicationOperations;
using namespace SkullbonezCore::Runtime::ReplayPredictionReserveOperations;
using namespace SkullbonezCore::Runtime::ReplayPredictionSchedulingOperations;
using namespace SkullbonezCore::Runtime::ReplayScrubberOperations;
namespace Gameplay = SkullbonezCore::Gameplay;
using namespace SkullbonezCore::Math::CollisionDetection;
using namespace SkullbonezCore::Math::Orientation;
using namespace SkullbonezCore::Math::Transformation;
using namespace SkullbonezCore::Physics;
namespace Physics = SkullbonezCore::Physics;
using SkullbonezCore::Assets::EDITOR_HULL_ASSET_COUNT;
using SkullbonezCore::Assets::EDITOR_HULL_ASSETS;
using SkullbonezCore::Assets::EditorHullAsset;
using SkullbonezCore::Assets::EditorHullAssetDefaultsToContactRelease;
using SkullbonezCore::Assets::EditorHullAssetPath;
using SkullbonezCore::Assets::EditorHullAssetToken;
using SkullbonezCore::Math::Vector::Vector3;
namespace CoreAllocation = SkullbonezCore::Core::Allocation;

namespace
{
constexpr double REPLAY_PREDICTION_REFRESH_SECONDS = 0.35;
constexpr double REPLAY_PREDICTION_MAX_WORK_MILLISECONDS = 5.0;

bool TryResolveReplayBodyModelIndex( const PhysicsBodyStore& bodyStore,
                                     Physics::PhysicsSceneObjectId id,
                                     int modelIndexHint,
                                     int modelCount,
                                     int& outModelIndex )
{
    if ( id.value == 0 )
    {
        return false;
    }

    const PhysicsBodyHandle body = bodyStore.HandleForSceneObjectId( id, modelIndexHint );
    const int modelIndex = bodyStore.ModelIndexForHandle( body );
    if ( modelIndex < 0 || modelIndex >= modelCount )
    {
        return false;
    }

    outModelIndex = modelIndex;
    return true;
}


bool TryResolveReplayBodyModelIndex( const PhysicsBodyStore& bodyStore,
                                     Physics::PhysicsSceneObjectId id,
                                     ModelRowHint& hint,
                                     int modelCount,
                                     int& outModelIndex )
{
    // Why: retained replay UI state still carries modelIndex integers until the
    // fable-06 conversion rows are complete. Naming the cache as ModelRowHint
    // keeps stable scene object identity in Physics::PhysicsSceneObjectId while this resolver heals or
    // invalidates the dense-row guess.
    if ( !TryResolveReplayBodyModelIndex( bodyStore, id, hint.value, modelCount, outModelIndex ) )
    {
        hint.value = -1;
        return false;
    }

    hint.value = outModelIndex;
    return true;
}


// Concept: prediction stepping is pure physics. Contact-highlight and
// diagnostics-name presentation belongs to the live engine only; prediction
// samples read the private engine's hot-field arrays directly.
bool StepPredictionEngineTick( PhysicsEngine& engine,
                               Gameplay::TornadoGameplay& tornadoGameplay,
                               float fixedDt,
                               const PhysicsWorldForces& worldForces,
                               SkullbonezCore::Threading::WorkerPool& workerPool )
{
    CoreAllocation::RuntimeAllocationScope replayAllocationScope( CoreAllocation::RuntimeAllocationPhase::Replay );
    const SkullbonezCore::Physics::ExternalForceFrameInput externalForces = tornadoGameplay.BuildForceFrame(
        fixedDt,
        SkullbonezCore::Physics::PhysicsEngine::ReadBodies( engine ).Count() );

    engine.Step( fixedDt, worldForces, externalForces, workerPool, PhysicsDiagnosticsCsvWriter {} );
    return true;
}


// Why: the 200-brick prediction scene needs more than the old 100-node cap to
// show the full contact spread instead of clipping the visual explanation.
constexpr std::size_t REPLAY_PATH_MAX_FUTURE_NODES = REPLAY_VISUAL_FUTURE_NODE_CAPACITY;
constexpr std::size_t REPLAY_RIBBON_SEGMENTS_PER_PATH_SEGMENT = 1;
constexpr float REPLAY_PATH_MIN_SEGMENT_DISTANCE_SQ = 0.0001f;
// Why: rest markers and auxiliary trails still need an instantaneous "moving"
// test, but child activation below uses contact ticks plus accumulated
// displacement so one-frame velocity spikes cannot reorder the cause tree.
constexpr float REPLAY_PREDICTION_CHILD_LINEAR_SPEED_SQ = 8.0f * 8.0f;
constexpr float REPLAY_PREDICTION_CHILD_ACTIVATION_DISTANCE = 0.05f;
constexpr float REPLAY_PREDICTION_CHILD_ACTIVATION_DISTANCE_SQ = REPLAY_PREDICTION_CHILD_ACTIVATION_DISTANCE *
                                                                 REPLAY_PREDICTION_CHILD_ACTIVATION_DISTANCE;

// Invariant: Worker dispatch is only worth it for large body snapshots. Small
// scenes stay serial so replay overlays do not pay thread wakeup cost to copy a
// few kilobytes.
constexpr int REPLAY_PREDICTION_PARALLEL_BODY_MIN = 2048;


// Concept: the prediction overlay is a play-once causal animation, not a
// static plot. A wall-clock reveal cursor sweeps the predicted frames so the
// root line grows first and each child line starts only when its causing frame
// is revealed; after the sweep the finished tree holds until the prediction is
// rebuilt. The per-run rate lives on RunReplayPredictionState so authored
// director phases can slow the money-shot unfold without touching physics.
// Why: "at rest" for the causal overlay is decided from the END of the
// completed prediction, never from a momentary pause. A body rests only when
// the final frame shows no visible motion and it has not drifted across the
// final grace window; otherwise it has no resting pose and gets no grey box.
constexpr double REPLAY_PREDICTION_REST_GRACE_SECONDS = 0.4;
constexpr ReplayFrameIndex REPLAY_PREDICTION_REST_GRACE_FRAMES = static_cast<ReplayFrameIndex>( REPLAY_PREDICTION_REST_GRACE_SECONDS / PHYSICS_FIXED_DT );
constexpr float REPLAY_PREDICTION_REST_POSITION_EPSILON_SQ = 0.5f * 0.5f;

constexpr uint32_t REPLAY_PREDICTION_CAPTURE_BODY_WORKER_HASH = HashStr( "Frame/Replay/Prediction/CaptureBodyState/WorkerBodies" );
constexpr uint32_t REPLAY_PREDICTION_CAPTURE_SAMPLE_WORKER_HASH = HashStr( "Frame/Replay/Prediction/CaptureSample/WorkerBodies" );

constexpr int REPLAY_PREDICTION_TICKS_PER_WORKER_SUBMIT = 8;

// Concept: future-node building is an incremental cache.
//
// Prediction can hold thousands of future frames. Clearing and rebuilding the
// future-impact tree every render frame makes the path visualizer scale with the
// full horizon. These cursors let each frame continue where the last frame stopped.
bool CaptureReplayPredictionBodyState( const PhysicsBodyStore& bodyStore,
                                       SkullbonezCore::Threading::WorkerPool& workerPool,
                                       SkullbonezCore::Core::Profiler* profiler,
                                       std::vector<RunReplayPredictionBodyBackup>& outBodies )
{
    PROFILE_SCOPED( profiler, "Frame/Replay/Prediction/CaptureBodyState" );
    const int modelCount = bodyStore.Count();
    const auto bodyRecords = bodyStore.Records();
    const auto hotFields = bodyStore.HotFields();
    if ( static_cast<int>( bodyRecords.size() ) < modelCount )
    {
        return false;
    }

    outBodies.clear();
    if ( !ReserveReplayPredictionVector( outBodies,
                                         static_cast<std::size_t>( modelCount ),
                                         0,
                                         "RunReplayPredictionBodyBackup[]" ) )
    {
        return false;
    }

    outBodies.resize( static_cast<std::size_t>( modelCount ) );

    const auto captureBody = [&]( int i )
    {
        CoreAllocation::RuntimeAllocationScope replayAllocationScope( CoreAllocation::RuntimeAllocationPhase::Replay );

        const std::size_t bodyIndex = static_cast<std::size_t>( i );
        const PhysicsBodyRecord& body = bodyRecords[bodyIndex];
        RunReplayPredictionBodyBackup backup;
        backup.id = body.sceneObjectId;
        backup.modelRow.value = i;
        backup.position = PhysicsBodyPosition( hotFields, bodyIndex );
        backup.orientation = PhysicsBodyOrientation( hotFields, bodyIndex );
        backup.linearVelocity = PhysicsBodyLinearVelocity( hotFields, bodyIndex );
        backup.angularVelocity = PhysicsBodyAngularVelocity( hotFields, bodyIndex );
        backup.mass = body.mass;
        backup.inverseMass = hotFields.inverseMass[bodyIndex];
        backup.rotationalInertia = body.rotationalInertia;
        backup.inverseRotationalInertia = PhysicsBodyInverseInertia( hotFields, bodyIndex );
        backup.fixed = hotFields.fixed[bodyIndex] != 0u;
        outBodies[static_cast<std::size_t>( i )] = backup;
    };

    // Invariant: this loop reads authoritative hot-field rows and one
    // presentation timer, then writes one output slot per body. Applying
    // backups remains serial because it mutates physics body state.
    if ( modelCount >= REPLAY_PREDICTION_PARALLEL_BODY_MIN )
    {
        workerPool.ParallelForNoAlloc( 0,
                                       modelCount,
                                       captureBody,
                                       REPLAY_PREDICTION_PARALLEL_BODY_MIN,
                                       "Frame/Replay/Prediction/CaptureBodyState/WorkerBodies",
                                       REPLAY_PREDICTION_CAPTURE_BODY_WORKER_HASH );
    }
    else
    {
        for ( int i = 0; i < modelCount; ++i )
        {
            captureBody( i );
        }
    }

    return true;
}


bool ApplyReplayPredictionBodyState( PhysicsEngine& physicsEngine,
                                     SkullbonezCore::Core::Profiler* profiler,
                                     const std::vector<RunReplayPredictionBodyBackup>& bodies )
{
    PROFILE_SCOPED( profiler, "Frame/Replay/Prediction/ApplyBodyState" );
    const PhysicsBodyStore& bodyStore = SkullbonezCore::Physics::PhysicsEngine::ReadBodies( physicsEngine );
    if ( bodies.size() != static_cast<std::size_t>( bodyStore.Count() ) )
    {
        return false;
    }

    for ( const RunReplayPredictionBodyBackup& backup : bodies )
    {
        const PhysicsBodyHandle bodyHandle = bodyStore.HandleForModelIndex( backup.modelRow.value );
        const PhysicsBodyRecord* bodyRecord = bodyStore.RecordForHandle( bodyHandle );
        if ( !bodyRecord || bodyStore.ModelIndexForHandle( bodyHandle ) != backup.modelRow.value ||
             bodyRecord->sceneObjectId != backup.id )
        {
            return false;
        }

        const PhysicsBodyRestoreState restore { bodyHandle,
                                                backup.id,
                                                backup.fixed,
                                                backup.position,
                                                backup.orientation,
                                                backup.linearVelocity,
                                                backup.angularVelocity,
                                                backup.mass,
                                                backup.inverseMass,
                                                backup.rotationalInertia,
                                                backup.inverseRotationalInertia };

        if ( !physicsEngine.RestoreReplayBodyState( restore ) )
        {
            return false;
        }
    }

    return true;
}


bool SeedReplayPredictionEngine( RunReplayPredictionState& prediction,
                                 SkullbonezCore::Core::Profiler* profiler,
                                 const PhysicsEngine& liveEngine,
                                 const SkullbonezCore::Core::EngineConfig& config,
                                 const PhysicsWorldForces& worldForces,
                                 int modelCount )
{
    PROFILE_SCOPED( profiler, "Frame/Replay/Prediction/SeedPrivateEngine" );
    const CoreAllocation::RuntimeReserveOwnerHandle owner = ReplayPredictionReserveOwner();
    const int requestedBytes = ReplayPredictionEngineReserveBytes( liveEngine );
    if ( requestedBytes <= 0 )
    {
        return false;
    }

    const int currentBytes = prediction.simulation.predictionEngine
                                 ? ReplayPredictionEngineReserveBytes( *prediction.simulation.predictionEngine )
                                 : 0;

    if ( prediction.simulation.predictionEngine && currentBytes <= 0 )
    {
        return false;
    }

    CoreAllocation::RuntimeReserveGrowthResult result = {};
    if ( requestedBytes > currentBytes )
    {
        // Why: the private engine is retained across prediction rebuilds. Only
        // real capacity increases should consume replay growth events; same-size
        // reseeds just reuse the previous bounded reservation.
        if ( !RequestReplayPredictionReserveGrowth( "RunReplayPredictionSimulationState::predictionEngine",
                                                    0,
                                                    currentBytes,
                                                    requestedBytes,
                                                    1,
                                                    result ) )
        {
            return false;
        }
    }

    CoreAllocation::RuntimeAllocationScope replayAllocationScope( CoreAllocation::RuntimeAllocationPhase::Replay );
    CoreAllocation::RuntimeReserveOwnerScope ownerScope( owner );
    CoreAllocation::RuntimeReserveGrowthScope growthScope( owner, CoreAllocation::RuntimeReservePhase::Replay, result );
    prediction.simulation.predictionEngineReady = false;
    if ( !prediction.simulation.predictionEngine )
    {
        prediction.simulation.predictionEngine = std::make_unique<PhysicsEngine>();
    }

    // Invariant: seeding starts from the live facade's topology and cold policy,
    // then restores the captured prediction values into the private engine. The
    // live engine is never passed to prediction stepping after this point.
    PhysicsEngine& predictionEngine = *prediction.simulation.predictionEngine;
    predictionEngine = liveEngine;
    // Invariant: std::vector copy assignment preserves element values, not the
    // source's spare capacity. The live mutual-gravity pair table is empty at
    // capture time, so the private engine must re-establish its known body-count
    // scratch while the registered replay reserve scope is still active.
    predictionEngine.ReserveAuthoredBodyCapacity( static_cast<std::size_t>( (std::max)( 0, modelCount ) ) );
    predictionEngine.BindProfiler( profiler );
    predictionEngine.ApplyRuntimeConfig( config );
    prediction.simulation.predictionWorldForces = worldForces;
    if ( !ApplyReplayPredictionBodyState( predictionEngine, profiler, prediction.simulation.predictionBodies ) ||
         !predictionEngine.RestoreReplaySolverSnapshot( prediction.simulation.predictionWorld.physics,
                                                        MakePhysicsBodyCountFromNonNegativeInt( modelCount ) ) )
    {
        return false;
    }

    prediction.simulation.predictionEngineReady = true;
    return true;
}


bool CaptureReplayPredictionFrame( RunReplayPredictionState& prediction,
                                   const PhysicsEngine& physicsEngine,
                                   SkullbonezCore::Threading::WorkerPool& workerPool,
                                   SkullbonezCore::Core::Profiler* profiler,
                                   int modelCount,
                                   ReplayFrameIndex frameIndex )
{
    PROFILE_SCOPED( profiler, "Frame/Replay/Prediction/CaptureSample" );
    const PhysicsBodyStore& bodyStore = SkullbonezCore::Physics::PhysicsEngine::ReadBodies( physicsEngine );
    const auto bodyRecords = bodyStore.Records();
    const auto hotFields = bodyStore.HotFields();
    if ( static_cast<int>( bodyRecords.size() ) < modelCount )
    {
        return false;
    }

    const std::size_t frameSlot = static_cast<std::size_t>( frameIndex );
    if ( frameSlot >= prediction.build.buildFrames.size() )
    {
        return false;
    }

    RunReplayPredictionFrame& frame = prediction.build.buildFrames[frameSlot];
    frame.frameIndex = frameIndex;
    frame.simulationSeconds = prediction.simulation.sourceSimulationSeconds +
                              static_cast<double>( frameIndex ) * static_cast<double>( PHYSICS_FIXED_DT );

    frame.tornadoSystemElapsedSeconds = prediction.simulation.predictionTornadoGameplay.GetSystemElapsedSeconds();
    frame.contactsIncomplete = false;
    if ( static_cast<std::size_t>( modelCount ) > frame.bodies.capacity() )
    {
        return false;
    }

    frame.bodies.resize( static_cast<std::size_t>( modelCount ) );

    const auto captureBody = [&]( int i )
    {
        CoreAllocation::RuntimeAllocationScope replayAllocationScope( CoreAllocation::RuntimeAllocationPhase::Replay );

        const std::size_t bodyIndex = static_cast<std::size_t>( i );
        const PhysicsBodyRecord& source = bodyRecords[bodyIndex];
        RunReplayPredictionBodySample body;
        body.id = source.sceneObjectId;
        body.modelRow.value = i;
        body.position = PhysicsBodyPosition( hotFields, bodyIndex );
        body.orientation = PhysicsBodyOrientation( hotFields, bodyIndex );
        body.linearVelocity = PhysicsBodyLinearVelocity( hotFields, bodyIndex );
        body.sleeping = hotFields.awake[bodyIndex] == 0u;
        frame.bodies[static_cast<std::size_t>( i )] = body;
    };

    // Invariant: capture reads the store rows advanced by the prediction step.
    // A replay-only legacy object record writeback would copy every temporary pose just so
    // this loop could read the same values back into prediction samples.
    if ( modelCount >= REPLAY_PREDICTION_PARALLEL_BODY_MIN )
    {
        workerPool.ParallelForNoAlloc( 0,
                                       modelCount,
                                       captureBody,
                                       REPLAY_PREDICTION_PARALLEL_BODY_MIN,
                                       "Frame/Replay/Prediction/CaptureSample/WorkerBodies",
                                       REPLAY_PREDICTION_CAPTURE_SAMPLE_WORKER_HASH );
    }
    else
    {
        for ( int i = 0; i < modelCount; ++i )
        {
            captureBody( i );
        }
    }

    const std::vector<PhysicsDebugContact>& debugContacts = SkullbonezCore::Physics::PhysicsEngine::ReadDebugContacts( physicsEngine );

    if ( debugContacts.size() > frame.debugContacts.capacity() )
    {
        // Why: debug contacts feed the optional future-impact tree; the root
        // trajectory line only needs body samples. If a dense contact frame asks
        // for more replay scratch, batch the reserve across every prediction
        // frame so the byte cap covers the whole debug-contact payload set. If
        // the replay reserve refuses, keep the frame and drop contacts rather
        // than cancelling prediction.
        const std::size_t requestedDebugContactCapacity = ReplayPredictionNextDebugContactCapacity(
            frame.debugContacts.capacity(),
            debugContacts.size() );

        if ( !ReserveReplayPredictionFramePayloadVectors( prediction.build.buildFrames,
                                                          prediction.build.buildFrames.size(),
                                                          requestedDebugContactCapacity,
                                                          static_cast<int>( frameIndex ),
                                                          "RunReplayPredictionFrame::debugContacts",
                                                          &RunReplayPredictionFrame::debugContacts ) )
        {
            frame.debugContacts.clear();
            frame.contactsIncomplete = true;
            if ( !PublishReplayPredictionRootTrajectoryFrame( prediction, frame, frameSlot ) )
            {
                return false;
            }

            prediction.PublishBuildFrameSlot( frameSlot );
            return true;
        }
    }

    frame.debugContacts = debugContacts;
    frame.contactsIncomplete = false;
    if ( !PublishReplayPredictionRootTrajectoryFrame( prediction, frame, frameSlot ) )
    {
        return false;
    }

    prediction.PublishBuildFrameSlot( frameSlot );
    return true;
}
} // namespace

namespace
{
// Concept: prediction visualizer section.
//
// This block stays after the helper section so job setup, stepping, and drawing
// can keep using the replay prediction helpers without a new compatibility
// header.
void MarkReplayPredictionWorkerFailed( RunReplayPredictionState& prediction )
{
    prediction.build.publication.MarkWorkerFailed();
}

void RunReplayPredictionWorkerRange( RunReplayPredictionState& prediction,
                                     SkullbonezCore::Core::Profiler* profiler,
                                     const SkullbonezCore::Core::EngineConfig& config,
                                     SkullbonezCore::Threading::WorkerPool& workerPool,
                                     int modelCount,
                                     int beginTickIndex,
                                     int endTickIndex )
{
    if ( prediction.build.publication.WorkerFailed() || !prediction.simulation.predictionEngineReady ||
         !prediction.simulation.predictionEngine )
    {
        MarkReplayPredictionWorkerFailed( prediction );
        return;
    }

    PhysicsEngine& predictionEngine = *prediction.simulation.predictionEngine;
    const auto probeStart = std::chrono::steady_clock::now();
    int completedTicks = 0;
    for ( int tickIndex = beginTickIndex; tickIndex < endTickIndex; ++tickIndex )
    {
        const int predictionTick = tickIndex + 1;
        if ( predictionTick > prediction.build.targetTickCount )
        {
            break;
        }

        // Hazard: worker slices hold only prediction-owned values: the private
        // prediction engine, pre-sized build frames, and stable trajectory slots.
        // Scene mutation paths must cancel and wait before live stores are
        // reloaded, because this worker never borrows legacy object record rows.
        if ( !StepPredictionEngineTick( predictionEngine,
                                        prediction.simulation.predictionTornadoGameplay,
                                        PHYSICS_FIXED_DT,
                                        prediction.simulation.predictionWorldForces,
                                        workerPool ) ||
             !CaptureReplayPredictionFrame( prediction,
                                            predictionEngine,
                                            workerPool,
                                            profiler,
                                            modelCount,
                                            static_cast<ReplayFrameIndex>( predictionTick ) ) )
        {
            MarkReplayPredictionWorkerFailed( prediction );
            return;
        }

        prediction.build.nextTick = predictionTick + 1;
        ++completedTicks;
    }

    if ( completedTicks > 0 && prediction.simulation.measuredTicksPerMs.load( std::memory_order_relaxed ) <= 0.0 )
    {
        const double elapsedMs = std::chrono::duration<double, std::milli>( std::chrono::steady_clock::now() -
                                                                            probeStart )
                                     .count();

        prediction.simulation.probeElapsedMs += elapsedMs;
        prediction.simulation.probeTicksCompleted += completedTicks;
        if ( prediction.simulation.probeTicksCompleted >= config.replayPrediction.probeTicks &&
             prediction.simulation.probeElapsedMs > 0.0 )
        {
            const double ticksPerMs = static_cast<double>( prediction.simulation.probeTicksCompleted ) /
                                      prediction.simulation.probeElapsedMs;

            prediction.simulation.measuredTicksPerMs.store( ticksPerMs, std::memory_order_release );
        }
    }
}

} // namespace

void ReplayPrediction::RunWorkerRange( const SkullbonezCore::Core::EngineConfig& config,
                                       SkullbonezCore::Threading::WorkerPool& workerPool,
                                       int modelCount,
                                       int beginTickIndex,
                                       int endTickIndex )
{
    RunReplayPredictionWorkerRange( m_state, m_profiler, config, workerPool, modelCount, beginTickIndex, endTickIndex );
}

namespace
{
bool CompleteReplayPredictionJobOnFrameThread( ReplayPrediction& predictionOwner,
                                               RunReplayPredictionState& prediction,
                                               double simulationTotalSeconds,
                                               bool historicalSamplePaused,
                                               float solverTrackPosition,
                                               float solverPresentTrackPosition,
                                               ReplayPredictionUpdateResult& result )
{
    if ( prediction.build.publication.WorkerFailed() )
    {
        const bool preserveCommittedFuture = prediction.simulation.frames.size() >= 2u;
        predictionOwner.CancelJob( !preserveCommittedFuture );
        prediction.build.dirty = true;
        return false;
    }

    if ( !prediction.build.schedule.CompleteAndIdle() )
    {
        return false;
    }

    if ( prediction.simulation.predictionEngine )
    {
        prediction.simulation.predictionEngine->CaptureReplaySolverSnapshot(
            prediction.simulation.predictionWorld.physics,
            MakePhysicsBodyCountFromNonNegativeInt(
                SkullbonezCore::Physics::PhysicsEngine::ReadBodies( *prediction.simulation.predictionEngine )
                    .Count() ) );

        const Gameplay::TornadoGameplay& tornadoGameplay = prediction.simulation.predictionTornadoGameplay;
        prediction.simulation.predictionWorld.tornadoConfig = tornadoGameplay.GetFieldConfig();
        prediction.simulation.predictionWorld.tornadoSystemConfig = tornadoGameplay.GetSystemConfig();
        prediction.simulation.predictionWorld.tornadoSystemElapsedSeconds = tornadoGameplay.GetSystemElapsedSeconds();
        prediction.simulation.predictionWorld.tornadoCaptureSeconds = tornadoGameplay.CaptureSeconds();
        prediction.simulation.predictionWorld.tornadoEjectCooldownSeconds = tornadoGameplay.EjectCooldownSeconds();
    }

    const bool hadCommittedPredictionFrames = prediction.simulation.frames.size() >= 2;
    const bool solverWasOldLiveEdge = !hadCommittedPredictionFrames &&
                                      ReplayAtPresentTrackPosition( solverTrackPosition, 1.0f );

    const bool scrubberWasPinnedToPresent = !historicalSamplePaused ||
                                            ReplayAtPresentTrackPosition( solverTrackPosition,
                                                                          solverPresentTrackPosition ) ||
                                            solverWasOldLiveEdge;

    prediction.build.schedule.Reset();
    prediction.build.building = false;
    prediction.build.complete = true;
    prediction.build.lastBuildWallMs = std::chrono::duration<double, std::milli>( std::chrono::steady_clock::now() -
                                                                                  prediction.build.jobStart )
                                           .count();

    prediction.simulation.frames.swap( prediction.build.buildFrames );
    // Why: the swapped-out committed bank is the next build's allocation-free
    // scratch. Reset publication below; do not destroy its per-frame capacities.
    prediction.ResetBuildFramePublication();
    (void)RebuildReplayPredictionCommittedRootTrajectory( prediction );
    if ( prediction.baseline.valid )
    {
        UpdateReplayPredictionBaselineDivergence( prediction,
                                                  prediction.simulation.frames,
                                                  prediction.simulation.frames.size() );
    }

    if ( scrubberWasPinnedToPresent )
    {
        // Why: prediction extends the normalized solver track by moving the
        // present marker left. A scrub value that meant "live/present" before
        // the swap must remain present, or render will preview the far future and
        // make the selected body appear to move.
        result.pinSolverScrubberToPresent = true;
    }

    // Why: worker timing decides how much build-frame topology render had seen
    // before the swap. Rebuild the child cache from the committed full buffer so
    // the final trajectory store and automation fingerprint are scheduler-stable.
    ClearReplayPredictionFutureNodeCache( prediction );
    prediction.build.lastBuildTime = simulationTotalSeconds;
    return true;
}

} // namespace

ReplayPredictionSourcePreparation
ReplayPrediction::BeginFrameSource( PhysicsEngine& physicsEngine,
                                    const SkullbonezCore::Core::EngineConfig& config,
                                    bool scenePhysics,
                                    double fallbackSourceSimulationSeconds,
                                    double simulationTotalSeconds,
                                    const ReplaySolverFrameSample* latestSolverSample,
                                    Physics::PhysicsSceneObjectId requestedTargetId,
                                    ModelRowHint requestedTargetModelRow,
                                    bool targetAvailable,
                                    const std::chrono::steady_clock::time_point& budgetStart,
                                    double budgetMilliseconds,
                                    ReplayPredictionUpdateResult& result )
{
    ReplayPrediction& predictionOwner = *this;
    RunReplayPredictionState& prediction = m_state;
    PROFILE_SCOPED( predictionOwner.ProfilerBorrow(), "Frame/Replay/Prediction/BeginJob" );
    if ( !predictionOwner.GenerationPermitted() )
    {
        // Invariant: artifact verification is load-only. Returning before any
        // snapshot, reserve, worker, or trajectory mutation makes a second
        // visual prediction impossible in that process.
        prediction.build.dirty = false;
        return ReplayPredictionSourcePreparation::Declined;
    }

    // Hazard: begin captures the initial prediction snapshot. Budget may stop
    // us before setup starts, but once replay scratch and solver state are
    // reserved we must publish frame 0 so large predictions can draw progress
    // instead of thrashing a dirty begin job every render frame.
    if ( ReplayPredictionBudgetExpiredForPass( result,
                                               SkullbonezCore::Core::MainMemoryReplayBudgetPass::PredictionBegin,
                                               budgetStart,
                                               budgetMilliseconds ) )
    {
        return ReplayPredictionSourcePreparation::Declined;
    }

    const ReplayFrameIndex sourceFrameIndex = latestSolverSample ? latestSolverSample->frameIndex : 0;
    const uint64_t sourceSolverHash = latestSolverSample ? latestSolverSample->solverHash : 0;
    const ReplayFrameIndex previousSourceFrameIndex = prediction.simulation.sourceFrameIndex;
    const uint64_t previousSourceSolverHash = prediction.simulation.sourceSolverHash;
    const bool preserveCommittedFuture = prediction.enabled && scenePhysics && requestedTargetId.value != 0 &&
                                         prediction.simulation.targetId.value == requestedTargetId.value &&
                                         prediction.simulation.frames.size() >= 2u;

    const std::size_t buildPresentationFrameCount = preserveCommittedFuture &&
                                                            !prediction.build.liveVelocityEditRefreshPending
                                                        ? ReplayPredictionBuildPresentationFrameCountForRefresh(
                                                              prediction,
                                                              requestedTargetId )
                                                        : 2u;

    const bool clearSamplesOnCancel = !preserveCommittedFuture;
    predictionOwner.CancelJob( clearSamplesOnCancel );
    if ( clearSamplesOnCancel )
    {
        predictionOwner.ClearFutureNodeCache();
        // Why: only a genuinely empty replacement should replay the causal
        // story from the root. Same-target refreshes preserve the anchor and
        // keep showing the committed future until the new prefix catches up.
        prediction.revealClock.anchor = std::chrono::steady_clock::now();
        prediction.revealClock.anchorValid = true;
    }

    prediction.simulation.targetId = requestedTargetId;
    prediction.build.dirty = false;

    if ( !prediction.enabled || !scenePhysics )
    {
        return ReplayPredictionSourcePreparation::Declined;
    }

    prediction.simulation.sourceFrameIndex = sourceFrameIndex;
    prediction.simulation.sourceSolverHash = sourceSolverHash;
    prediction.build.instantBudgetMs = static_cast<double>( config.replayPrediction.instantBudgetMs );
    prediction.build.probeTickBudget = config.replayPrediction.probeTicks;
    prediction.build.jobStart = std::chrono::steady_clock::now();
    if ( latestSolverSample )
    {
        prediction.simulation.sourceSimulationSeconds = latestSolverSample->simulationSeconds;
    }
    else
    {
        prediction.simulation.sourceSimulationSeconds = fallbackSourceSimulationSeconds;
    }

    prediction.build.lastBuildTime = simulationTotalSeconds;

    const int modelCount = SkullbonezCore::Physics::PhysicsEngine::ReadBodies( physicsEngine ).Count();
    const bool calibrationSourceChanged = previousSourceFrameIndex != sourceFrameIndex ||
                                          previousSourceSolverHash != sourceSolverHash ||
                                          prediction.simulation.calibratedModelCount != modelCount;

    if ( calibrationSourceChanged )
    {
        prediction.simulation.measuredTicksPerMs.store( 0.0, std::memory_order_release );
        prediction.simulation.probeElapsedMs = 0.0;
        prediction.simulation.probeTicksCompleted = 0;
        prediction.simulation.calibratedModelCount = modelCount;
    }

    prediction.build.buildMode = ReplayPredictionBuildMode::Undecided;
    prediction.build.buildPresentationFrameCount = buildPresentationFrameCount;
    const PhysicsBodyStore& liveBodyStore = SkullbonezCore::Physics::PhysicsEngine::ReadBodies( physicsEngine );
    if ( targetAvailable && requestedTargetId.value != 0 )
    {
        ModelRowHint targetHint = requestedTargetModelRow;
        int targetIndex = -1;
        if ( ReplayPredictionBudgetExpiredForPass( result,
                                                   SkullbonezCore::Core::MainMemoryReplayBudgetPass::PredictionBegin,
                                                   budgetStart,
                                                   budgetMilliseconds ) )
        {
            prediction.build.dirty = true;
            return ReplayPredictionSourcePreparation::Declined;
        }

        if ( !TryResolveReplayBodyModelIndex( liveBodyStore, requestedTargetId, targetHint, modelCount, targetIndex ) )
        {
            result.repairedTargetModelRow = targetHint;
            result.targetModelRowRepaired = true;
            return ReplayPredictionSourcePreparation::Declined;
        }

        prediction.simulation.targetModelRow.value = targetIndex;
        result.repairedTargetModelRow = targetHint;
        result.targetModelRowRepaired = true;
    }

    return clearSamplesOnCancel ? ReplayPredictionSourcePreparation::ClearCommitted
                                : ReplayPredictionSourcePreparation::PreserveCommitted;
}


bool ReplayPrediction::BeginFrameSimulation( PhysicsEngine& physicsEngine,
                                             const Gameplay::TornadoGameplay& tornadoGameplay,
                                             const SceneEntityStore& entities,
                                             const SkullbonezCore::Core::EngineConfig& config,
                                             const SkullbonezCore::Physics::PhysicsWorldForces& worldForces,
                                             SkullbonezCore::Threading::WorkerPool& workerPool,
                                             ReplayPredictionSourcePreparation preparation )
{
    ReplayPrediction& predictionOwner = *this;
    RunReplayPredictionState& prediction = m_state;
    if ( preparation == ReplayPredictionSourcePreparation::Declined )
    {
        return false;
    }

    const bool clearSamplesOnCancel = preparation == ReplayPredictionSourcePreparation::ClearCommitted;
    const int modelCount = SkullbonezCore::Physics::PhysicsEngine::ReadBodies( physicsEngine ).Count();
    const PhysicsBodyStore& liveBodyStore = SkullbonezCore::Physics::PhysicsEngine::ReadBodies( physicsEngine );

    prediction.simulation.horizonSeconds = std::clamp( prediction.simulation.horizonSeconds,
                                                       ReplayOverlay::REPLAY_PREDICTION_MIN_SECONDS,
                                                       ReplayOverlay::REPLAY_PREDICTION_MAX_SECONDS );

    const int predictionTicks = (std::max)( 1,
                                            static_cast<int>( std::ceil( prediction.simulation.horizonSeconds /
                                                                         PHYSICS_FIXED_DT ) ) );

    prediction.build.targetTickCount = predictionTicks;
    prediction.build.nextTick = 1;
    const std::size_t buildFrameCapacity = static_cast<std::size_t>( predictionTicks + 1 );
    const std::size_t buildPresentationFrameCount = prediction.build.buildPresentationFrameCount;
    if ( !ReserveReplayPredictionVector( prediction.build.buildFrames,
                                         buildFrameCapacity,
                                         0,
                                         "RunReplayPredictionBuildState::buildFrames" ) )
    {
        predictionOwner.CancelJob( clearSamplesOnCancel );
        prediction.build.dirty = true;
        return false;
    }

    prediction.build.buildFrames.resize( buildFrameCapacity );
    prediction.ResetBuildFramePublication();
    // Why: ResetBuildFramePublication clears stale bank state, while this
    // generation's threshold was chosen from its request kind before reserve.
    prediction.build.buildPresentationFrameCount = buildPresentationFrameCount;
    if ( !ReserveReplayPredictionFramePayloadVectors( prediction.build.buildFrames,
                                                      buildFrameCapacity,
                                                      static_cast<std::size_t>( modelCount ),
                                                      0,
                                                      "RunReplayPredictionFrame::bodies",
                                                      &RunReplayPredictionFrame::bodies ) )
    {
        predictionOwner.CancelJob( clearSamplesOnCancel );
        prediction.build.dirty = true;
        return false;
    }

    // Why: replay prediction is exploratory UI, so the initial contact payload
    // reserve is intentionally generous and later growth is rounded to large
    // chunks. The root trajectory still publishes even if optional contact-tree
    // payloads outgrow the reserve.
    const std::size_t initialDebugContactCapacity = ReplayPredictionInitialDebugContactCapacity( modelCount );
    (void)ReserveReplayPredictionFramePayloadVectors( prediction.build.buildFrames,
                                                      buildFrameCapacity,
                                                      initialDebugContactCapacity,
                                                      0,
                                                      "RunReplayPredictionFrame::debugContacts",
                                                      &RunReplayPredictionFrame::debugContacts );

    if ( !ReserveReplayPredictionVector( prediction.futureNodeCache.futureNodes,
                                         REPLAY_PATH_MAX_FUTURE_NODES,
                                         0,
                                         "RunReplayPredictionFutureNodeCache::futureNodes" ) ||
         !ReserveReplayPredictionVector( prediction.futureNodeCache.futureNodeBuildScratch,
                                         REPLAY_PATH_MAX_FUTURE_NODES,
                                         0,
                                         "RunReplayPredictionFutureNodeCache::futureNodeBuildScratch" ) )
    {
        predictionOwner.CancelJob( clearSamplesOnCancel );
        prediction.build.dirty = true;
        return false;
    }

    if ( !PrepareReplayPredictionTrajectoryBuild( prediction,
                                                  prediction.simulation.targetId,
                                                  buildFrameCapacity,
                                                  static_cast<std::size_t>( modelCount ) ) )
    {
        predictionOwner.CancelJob( clearSamplesOnCancel );
        prediction.build.dirty = true;
        return false;
    }

    if ( modelCount != SkullbonezCore::Physics::PhysicsEngine::ReadColliders( physicsEngine ).Count() ||
         modelCount != entities.Count() ||
         !CaptureReplayPredictionBodyState( liveBodyStore,
                                            workerPool,
                                            predictionOwner.ProfilerBorrow(),
                                            prediction.simulation.predictionBodies ) )
    {
        predictionOwner.CancelJob( clearSamplesOnCancel );
        return false;
    }

    physicsEngine.CaptureReplaySolverSnapshot( prediction.simulation.predictionWorld.physics,
                                               MakePhysicsBodyCountFromNonNegativeInt( modelCount ) );

    prediction.simulation.predictionTornadoGameplay.SetReplayState( tornadoGameplay.CaptureSeconds(),
                                                                    tornadoGameplay.EjectCooldownSeconds(),
                                                                    tornadoGameplay.GetFieldConfig(),
                                                                    tornadoGameplay.GetSystemConfig(),
                                                                    tornadoGameplay.GetSystemElapsedSeconds() );

    prediction.simulation.predictionTornadoGameplay.SetParallelForceEvaluation(
        tornadoGameplay.ParallelForceEvaluation() );
    prediction.simulation.predictionWorld.tornadoConfig = tornadoGameplay.GetFieldConfig();
    prediction.simulation.predictionWorld.tornadoSystemConfig = tornadoGameplay.GetSystemConfig();
    prediction.simulation.predictionWorld.tornadoSystemElapsedSeconds = tornadoGameplay.GetSystemElapsedSeconds();
    prediction.simulation.predictionWorld.tornadoCaptureSeconds = tornadoGameplay.CaptureSeconds();
    prediction.simulation.predictionWorld.tornadoEjectCooldownSeconds = tornadoGameplay.EjectCooldownSeconds();

    if ( !SeedReplayPredictionEngine( prediction,
                                      predictionOwner.ProfilerBorrow(),
                                      physicsEngine,
                                      config,
                                      worldForces,
                                      modelCount ) )
    {
        predictionOwner.CancelJob( clearSamplesOnCancel );
        prediction.build.dirty = true;
        return false;
    }

    if ( !prediction.simulation.predictionEngine ||
         !CaptureReplayPredictionFrame( prediction,
                                        *prediction.simulation.predictionEngine,
                                        workerPool,
                                        predictionOwner.ProfilerBorrow(),
                                        modelCount,
                                        0 ) )
    {
        predictionOwner.CancelJob( clearSamplesOnCancel );
        prediction.build.dirty = true;
        return false;
    }

    {
        CoreAllocation::RuntimeAllocationScope replayAllocationScope( CoreAllocation::RuntimeAllocationPhase::Replay );
        CoreAllocation::RuntimeReserveOwnerScope ownerScope( ReplayPredictionReserveOwner() );
        // Runtime allocation policy: the task lives in the build state's fixed
        // optional slot; each generation reconstructs it without heap growth.
        // Lifetime: the slice borrows EngineConfig and WorkerPool from Run's
        // process-lifetime owners. CancelJob/WaitForJobIdle joins the slice
        // before either borrow can be retired or replay build state is cleared.
        prediction.build.schedule.Begin(
            prediction.build.targetTickCount,
            REPLAY_PREDICTION_TICKS_PER_WORKER_SUBMIT,
            ReplayPredictionSimulationSlice { &predictionOwner, &config, &workerPool, modelCount } );
        prediction.build.schedule.SetBudget( REPLAY_PREDICTION_TICKS_PER_WORKER_SUBMIT );
    }
    prediction.build.building = true;
    prediction.build.liveVelocityEditRefreshPending = false;
    ++prediction.build.generationBeginCount;

    return !prediction.build.buildFrames.empty();
}

namespace
{

bool StepReplayPredictionJob( ReplayPrediction& predictionOwner,
                              RunReplayPredictionState& prediction,
                              SkullbonezCore::Threading::WorkerPool& workerPool,
                              double simulationTotalSeconds,
                              bool historicalSamplePaused,
                              float solverTrackPosition,
                              float solverPresentTrackPosition,
                              const std::chrono::steady_clock::time_point& budgetStart,
                              double budgetMilliseconds,
                              ReplayPredictionUpdateResult& result )
{
    PROFILE_SCOPED( predictionOwner.ProfilerBorrow(), "Frame/Replay/Prediction/Slice" );
    if ( !prediction.build.building )
    {
        return prediction.build.complete;
    }

    if ( ReplayPredictionBudgetExpiredForPass( result,
                                               SkullbonezCore::Core::MainMemoryReplayBudgetPass::PredictionStep,
                                               budgetStart,
                                               budgetMilliseconds ) )
    {
        return false;
    }

    if ( !prediction.simulation.predictionEngineReady || !prediction.simulation.predictionEngine ||
         !prediction.build.schedule.Active() )
    {
        const bool preserveCommittedFuture = prediction.simulation.frames.size() >= 2u;
        predictionOwner.CancelJob( !preserveCommittedFuture );
        prediction.build.dirty = true;
        return false;
    }

    if ( prediction.build.buildMode == ReplayPredictionBuildMode::Undecided )
    {
        const double measuredTicksPerMs = prediction.simulation.measuredTicksPerMs.load( std::memory_order_acquire );
        const std::size_t publishedFrameCount = prediction.PublishedBuildFrameCount();
        const int completedTicks = static_cast<int>( publishedFrameCount > 0u ? publishedFrameCount - 1u : 0u );
        prediction.build.buildMode = ChooseReplayPredictionBuildMode(
            measuredTicksPerMs,
            (std::max)( 0, prediction.build.targetTickCount - completedTicks ),
            prediction.build.instantBudgetMs,
            prediction.simulation.predictionBodies.size() );
    }

    // Why: the frame loop still submits once per pass. Instant mode expands only
    // the worker budget; main-thread begin, tree, and draw budgets stay bounded.
    if ( prediction.build.buildMode == ReplayPredictionBuildMode::Instant )
    {
        PROFILE_SCOPED( predictionOwner.ProfilerBorrow(), "Frame/Replay/Prediction/Slice/Instant" );
        prediction.build.schedule.SetBudget( prediction.build.targetTickCount );
    }
    else if ( prediction.build.buildMode == ReplayPredictionBuildMode::Undecided )
    {
        PROFILE_SCOPED( predictionOwner.ProfilerBorrow(), "Frame/Replay/Prediction/Slice/Probe" );
        prediction.build.schedule.SetBudget( prediction.build.probeTickBudget );
    }
    else
    {
        PROFILE_SCOPED( predictionOwner.ProfilerBorrow(), "Frame/Replay/Prediction/Slice/Amortized" );
        prediction.build.schedule.SetBudget( REPLAY_PREDICTION_TICKS_PER_WORKER_SUBMIT );
    }

    prediction.build.schedule.SubmitTick( workerPool );

    if ( CompleteReplayPredictionJobOnFrameThread( predictionOwner,
                                                   prediction,
                                                   simulationTotalSeconds,
                                                   historicalSamplePaused,
                                                   solverTrackPosition,
                                                   solverPresentTrackPosition,
                                                   result ) )
    {
        return true;
    }

    if ( prediction.build.publication.WorkerFailed() )
    {
        (void)CompleteReplayPredictionJobOnFrameThread( predictionOwner,
                                                        prediction,
                                                        simulationTotalSeconds,
                                                        historicalSamplePaused,
                                                        solverTrackPosition,
                                                        solverPresentTrackPosition,
                                                        result );
        return false;
    }

    return prediction.PublishedBuildFrameCount() >= 2u || prediction.build.complete;
}


} // namespace

ReplayPredictionFrameSourceAction
ReplayPrediction::SelectFrameSource( const ReplaySolverFrameSample* latestSolverSample,
                                     Physics::PhysicsSceneObjectId targetId,
                                     bool targetAvailable,
                                     bool liveAdvanceHeld,
                                     double simulationTotalSeconds,
                                     bool& outWasDirty,
                                     bool& outWasPendingLatestRestart )
{
    ReplayPrediction& predictionOwner = *this;
    RunReplayPredictionState& prediction = m_state;
    outWasDirty = false;
    outWasPendingLatestRestart = false;

    PROFILE_SCOPED( predictionOwner.ProfilerBorrow(), "Frame/Replay/Prediction/SelectSource" );
    if ( !targetAvailable )
    {
        predictionOwner.ClearFutureNodeCache();
    }

    if ( !prediction.enabled )
    {
        if ( prediction.build.building )
        {
            predictionOwner.CancelJob( false );
        }

        return ReplayPredictionFrameSourceAction::Stop;
    }

    if ( !predictionOwner.GenerationPermitted() )
    {
        // Probe assertion lane: the archive may remain visually enabled, but
        // this branch draws only restored values and never reaches a snapshot,
        // reserve, worker, or future-simulation path.
        prediction.build.dirty = false;
        prediction.build.pendingLatestRestart = false;
        prediction.build.liveVelocityEditRefreshPending = false;
        // Invariant: EnterOfflinePredictionVerification already joined and
        // retired the worker. Cancelling here would invalidate the restored
        // complete/build and trajectory state before the CPU projection reads
        // it, producing a different packet without starting new simulation.
        return ReplayPredictionFrameSourceAction::Stop;
    }

    const ReplayFrameIndex latestFrame = latestSolverSample ? latestSolverSample->frameIndex : 0;
    const uint64_t latestHash = latestSolverSample ? latestSolverSample->solverHash : 0;
    const double now = simulationTotalSeconds;
    const bool sourceChanged = prediction.simulation.targetId.value != targetId.value ||
                               prediction.simulation.sourceFrameIndex != latestFrame ||
                               prediction.simulation.sourceSolverHash != latestHash;

    const bool refreshDue = ( now - prediction.build.lastBuildTime ) >= REPLAY_PREDICTION_REFRESH_SECONDS;
    const bool hasCommittedPrediction = prediction.simulation.frames.size() >= 2;
    // Invariant: a committed prediction is a frozen future for the current
    // branch. Space-stepping the paused live scene changes solver frame/hash,
    // but must not redraw the preview; explicit dirty events such as branch,
    // target, horizon, or predict toggles are the only rebuild triggers.
    const bool allowAutomaticRefresh = !liveAdvanceHeld && !hasCommittedPrediction;
    const ReplayPredictionCoalescerAction coalescerAction = ChooseReplayPredictionCoalescerAction(
        prediction.build.dirty,
        prediction.build.building,
        prediction.build.buildMode,
        prediction.build.pendingLatestRestart,
        prediction.BuildPrefixHasBeenPresented() );

    if ( coalescerAction == ReplayPredictionCoalescerAction::PromoteAndBegin &&
         !predictionOwner.PromoteBuildPrefixToCommitted() )
    {
        // Hazard: retain both newest-state tokens if promotion cannot acquire a
        // coherent prefix. The next frame retries without discarding the path
        // that was visible when this edit arrived.
        return ReplayPredictionFrameSourceAction::Continue;
    }

    if ( coalescerAction == ReplayPredictionCoalescerAction::Supersede )
    {
        ++prediction.build.supersededRestartCount;
        prediction.build.pendingLatestRestart = true;
        prediction.build.dirty = false;
    }

    const bool automaticRefreshRequested = allowAutomaticRefresh && !prediction.build.building && sourceChanged &&
                                           refreshDue;

    const bool beginRequested = coalescerAction == ReplayPredictionCoalescerAction::Begin ||
                                coalescerAction == ReplayPredictionCoalescerAction::PromoteAndBegin ||
                                automaticRefreshRequested;

    if ( !beginRequested )
    {
        return ReplayPredictionFrameSourceAction::Continue;
    }

    outWasDirty = prediction.build.dirty;
    outWasPendingLatestRestart = prediction.build.pendingLatestRestart;
    return ReplayPredictionFrameSourceAction::Begin;
}


void ReplayPrediction::PrepareFrameRebuild( Physics::PhysicsSceneObjectId targetId,
                                            ModelRowHint targetModelRow,
                                            ReplayPredictionUpdateResult& result )
{
    RunReplayPredictionState& prediction = m_state;
    // Invariant: the caller performs the outer begin-budget check first. Cause
    // accounting and baseline capture therefore occur only on a pass that is
    // actually allowed to begin replacement work, matching the original
    // single-operation ordering.
    if ( prediction.build.dirty )
    {
        ++result.rebuildCauses[static_cast<std::size_t>( SkullbonezCore::Core::MainMemoryReplayRebuildCause::Dirty )];
    }
    else
    {
        ++result.rebuildCauses[static_cast<std::size_t>(
            SkullbonezCore::Core::MainMemoryReplayRebuildCause::AutomaticRefresh )];
    }

    if ( prediction.baseline.comparisonActive && !prediction.baseline.valid &&
         prediction.simulation.frames.size() >= 2 )
    {
        if ( !CaptureReplayPredictionBaselineSnapshot( prediction,
                                                       prediction.simulation.frames,
                                                       prediction.simulation.frames.size(),
                                                       targetId,
                                                       targetModelRow.value ) )
        {
            prediction.baseline.comparisonActive = false;
        }
    }
}


void ReplayPrediction::CompleteFrameSourceBegin( bool began, bool wasDirty, bool wasPendingLatestRestart ) noexcept
{
    if ( began )
    {
        if ( wasPendingLatestRestart )
        {
            ++m_state.build.latestRestartBeginCount;
        }

        m_state.build.pendingLatestRestart = false;
        return;
    }

    // Hazard: begin can decline after the shared frame budget expires. Restore
    // the request token so the newest velocity is retried next pass.
    m_state.build.dirty = m_state.build.dirty || wasDirty;
    m_state.build.pendingLatestRestart = m_state.build.pendingLatestRestart || wasPendingLatestRestart;
}


bool ReplayPrediction::BeginFrameBudgetExpired( const std::chrono::steady_clock::time_point& budgetStart,
                                                double budgetMilliseconds,
                                                ReplayPredictionUpdateResult& result )
{
    return ReplayPredictionBudgetExpiredForPass( result,
                                                 SkullbonezCore::Core::MainMemoryReplayBudgetPass::PredictionBegin,
                                                 budgetStart,
                                                 budgetMilliseconds );
}


bool ReplayPrediction::AdvanceFrameWorker( SkullbonezCore::Threading::WorkerPool& workerPool,
                                           double simulationTotalSeconds,
                                           bool historicalSamplePaused,
                                           float solverTrackPosition,
                                           float solverPresentTrackPosition,
                                           const std::chrono::steady_clock::time_point& budgetStart,
                                           double budgetMilliseconds,
                                           ReplayPredictionUpdateResult& result )
{
    RunReplayPredictionState& prediction = m_state;
    bool predictionCompletedThisPass = false;
    if ( prediction.build.building )
    {
        const double remainingMilliseconds = ReplayPredictionRemainingMilliseconds( budgetStart, budgetMilliseconds );
        if ( remainingMilliseconds > 0.0 )
        {
            const bool wasBuilding = prediction.build.building;
            StepReplayPredictionJob( *this,
                                     prediction,
                                     workerPool,
                                     simulationTotalSeconds,
                                     historicalSamplePaused,
                                     solverTrackPosition,
                                     solverPresentTrackPosition,
                                     budgetStart,
                                     budgetMilliseconds,
                                     result );

            predictionCompletedThisPass = wasBuilding && prediction.build.complete && !prediction.build.building;
            (void)ReplayPredictionBudgetExpiredForPass(
                result,
                SkullbonezCore::Core::MainMemoryReplayBudgetPass::PredictionStep,
                budgetStart,
                budgetMilliseconds );
        }
        else
        {
            (void)ReplayPredictionBudgetExpiredForPass(
                result,
                SkullbonezCore::Core::MainMemoryReplayBudgetPass::PredictionStep,
                budgetStart,
                budgetMilliseconds );
        }
    }

    return predictionCompletedThisPass;
}


void ReplayPrediction::PublishCompletedFrame( const SceneEntityStore& entities, Physics::PhysicsSceneObjectId targetId )
{
    RebuildReplayPredictionCommittedTreeAfterWorkerCompletion( m_state, entities, targetId );
}


void ReplayPrediction::PreparePresentation( const SceneEntityStore& entities,
                                            const ColliderStore& colliderStore,
                                            Physics::PhysicsSceneObjectId targetId,
                                            ModelRowHint targetModelRow,
                                            bool targetAvailable,
                                            double budgetMilliseconds,
                                            ReplayPredictionUpdateResult& result )
{
    PrepareReplayPredictionOverlay( m_state,
                                    entities,
                                    colliderStore,
                                    targetId,
                                    targetModelRow,
                                    targetAvailable,
                                    budgetMilliseconds,
                                    result );
}
void ReplayPrediction::MarkDirty() noexcept
{
    if ( !m_generationPermitted )
    {
        // Why: load-only verification may consume the frozen artifact but can
        // never request a replacement future generation.
        m_state.build.dirty = false;
        return;
    }

    m_state.build.dirty = true;
}

void ReplayPrediction::EnterOfflineVerification()
{
    WaitForJobIdle();
    ForbidGeneration();
    m_state.build.dirty = false;
    m_state.build.pendingLatestRestart = false;
    m_state.build.liveVelocityEditRefreshPending = false;
}

void ReplayPrediction::ResetVerificationMarkers() noexcept
{
    m_state.futureNodeCache.retainedMarkerCount = 0;
}

void ReplayPrediction::SetVerificationRevealFrame( ReplayFrameIndex frame ) noexcept
{
    m_state.revealClock.deterministicFrame = frame;
    m_state.revealClock.presentedFrame = frame;
}

void ReplayPrediction::SetEnabled( bool enabled ) noexcept
{
    m_state.enabled = enabled;
    MarkDirty();
}

void ReplayPrediction::ApplyAuthoringRequest( bool enablePrediction,
                                              bool refreshPrediction,
                                              bool liveVelocityEdit,
                                              float minHorizonSeconds,
                                              float maxHorizonSeconds ) noexcept
{
    if ( enablePrediction )
    {
        m_state.enabled = true;
        m_state.simulation.horizonSeconds = std::clamp( m_state.simulation.horizonSeconds,
                                                        minHorizonSeconds,
                                                        maxHorizonSeconds );
    }

    if ( refreshPrediction )
    {
        m_state.build.liveVelocityEditRefreshPending = m_state.build.liveVelocityEditRefreshPending || liveVelocityEdit;
        MarkDirty();
    }
}

void ReplayPrediction::DisableAndClearCache()
{
    m_state.enabled = false;
    ClearCache();
}

bool ReplayPrediction::LoadArchive( std::span<const uint8_t> bytes,
                                    RunReplayPathVisualizerState& pathVisualizer,
                                    char* outReason,
                                    std::size_t reasonSize )
{
    return LoadReplayPredictionArchive( bytes, pathVisualizer, m_state, outReason, reasonSize );
}

bool ReplayPrediction::BuildArchive( const RunReplayPathVisualizerState& pathVisualizer,
                                     std::vector<uint8_t>& outBytes ) const
{
    return BuildReplayPredictionArchive( pathVisualizer, m_state, outBytes );
}

void ReplayPrediction::SetHorizonSeconds( float horizonSeconds ) noexcept
{
    if ( m_state.simulation.horizonSeconds == horizonSeconds )
    {
        return;
    }

    m_state.simulation.horizonSeconds = horizonSeconds;
    MarkDirty();
}

bool ReplayPrediction::RevealProgress01( float& outProgress ) const noexcept
{
    const bool usingBuildFrames = m_state.BuildPrefixShouldBePresented();
    const std::vector<RunReplayPredictionFrame>& frames = usingBuildFrames ? m_state.build.buildFrames
                                                                           : m_state.simulation.frames;

    const std::size_t frameCount = usingBuildFrames ? m_state.PublishedBuildFrameCount() : frames.size();
    if ( frameCount < 2u || !m_state.revealClock.anchorValid )
    {
        outProgress = 0.0f;
        return false;
    }

    const ReplayFrameIndex lastFrame = frames[frameCount - 1u].frameIndex;
    if ( lastFrame == 0 )
    {
        outProgress = 0.0f;
        return false;
    }

    const double availableSeconds = static_cast<double>( lastFrame ) * PHYSICS_FIXED_DT;
    const auto now = std::chrono::steady_clock::now();
    const double
        elapsedSeconds = (std::max)( 0.0, std::chrono::duration<double>( now - m_state.revealClock.anchor ).count() );

    const double revealRate = m_state.revealClock.secondsPerSecond > 0.0 ? m_state.revealClock.secondsPerSecond : 1.0;
    const double revealedSeconds = (std::min)( availableSeconds, elapsedSeconds * revealRate );
    const double revealFrame = revealedSeconds / static_cast<double>( PHYSICS_FIXED_DT );
    outProgress = std::clamp( static_cast<float>( revealFrame / static_cast<double>( lastFrame ) ), 0.0f, 1.0f );
    return true;
}

void ReplayPrediction::SetRevealRatePreservingCursor( double revealRate ) noexcept
{
    const double normalizedRevealRate = revealRate > 0.0 ? revealRate : 1.0;
    const double previousRevealRate = m_state.revealClock.secondsPerSecond > 0.0 ? m_state.revealClock.secondsPerSecond
                                                                                 : 1.0;

    if ( m_state.revealClock.anchorValid )
    {
        const auto now = std::chrono::steady_clock::now();
        const double elapsedSeconds = (std::max)( 0.0,
                                                  std::chrono::duration<double>( now - m_state.revealClock.anchor )
                                                      .count() );

        const double revealedSeconds = elapsedSeconds * previousRevealRate;
        m_state.revealClock.anchor = now -
                                     std::chrono::duration_cast<std::chrono::steady_clock::duration>(
                                         std::chrono::duration<double>( revealedSeconds / normalizedRevealRate ) );
    }

    m_state.revealClock.secondsPerSecond = normalizedRevealRate;
}

bool ReplayPrediction::PrepareVelocityMutationBaseline() noexcept
{
    if ( ( !m_state.build.complete || m_state.simulation.frames.size() < 2u ) && !m_state.baseline.comparisonActive )
    {
        return false;
    }

    if ( m_state.build.complete )
    {
        m_state.baseline.valid = false;
        m_state.baseline.comparisonActive = true;
        m_state.baseline.divergenceValid = false;
        m_state.baseline.divergenceUnits = 0.0f;
    }

    return true;
}

void ReplayPrediction::CommitVelocityMutation() noexcept
{
    m_state.enabled = true;
    MarkDirty();
}

bool ReplayPrediction::ReadyForDeterministicReveal() const noexcept
{
    return !m_state.build.building && m_state.simulation.frames.size() >= 2u && m_state.build.complete;
}

void ReplayPrediction::ArmDeterministicReveal( ReplayFrameIndex frame, bool resetPresentedFrame ) noexcept
{
    m_state.revealClock.deterministicFrameEnabled = true;
    m_state.revealClock.deterministicFrame = frame;
    if ( resetPresentedFrame )
    {
        m_state.revealClock.presentedFrame = frame;
    }
}

RunReplayPredictionState::RunReplayPredictionState()
{
    // Runtime allocation policy: prediction reuses this snapshot throughout
    // the session. Reserve every Gameplay row at construction so per-build
    // seeding and per-tick capture only change logical sizes.
    simulation.predictionWorld.tornadoSystemConfig.vortices.reserve( Gameplay::TornadoGameplay::MAX_ACTIVE_FORCE_FIELDS );
    simulation.predictionWorld.tornadoCaptureSeconds.reserve( SkullbonezCore::Scene::Capacity::MAX_SCENE_OBJECTS );
    simulation.predictionWorld.tornadoEjectCooldownSeconds.reserve( SkullbonezCore::Scene::Capacity::MAX_SCENE_OBJECTS );
}

void ReplayPrediction::ClearFutureNodeCache()
{
    m_state.futureNodeCache.futureNodes.clear();
    m_state.futureNodeCache.futureNodeBuildScratch.clear();
    m_state.futureNodeCache.futureNodesBuiltFrameCount = 0;
    m_state.futureNodeCache.futureNodesBuiltContactIndex = 0;
    m_state.futureNodeCache.futureNodesBuiltTargetId = Physics::PhysicsSceneObjectId {};
    m_state.futureNodeCache.futureNodesBuiltRagdollVisuals = m_state.ragdollVisualsEnabled;
    m_state.futureNodeCache.futureNodesBuiltFromBuildFrames = false;
    m_state.futureNodeCache.futureNodesCacheValid = false;
    m_state.futureNodeCache.retainedMarkerCount = 0;
    m_state.trajectoryBuild.childFrameCount = 0;
    m_state.trajectoryBuild.builtNodeCount = 0;
}

void ReplayPrediction::ClearCache()
{
    CancelJob( true );
    m_state.simulation.targetId = Physics::PhysicsSceneObjectId {};
    m_state.simulation.sourceFrameIndex = 0;
    m_state.simulation.sourceSolverHash = 0;
    m_state.simulation.sourceSimulationSeconds = 0.0;
    m_state.build.lastBuildTime = 0.0;
    m_state.trajectoryBuild = RunReplayPredictionTrajectoryBuildState {};
    m_state.trajectoryStore.Clear();
    m_state.baseline = ReplayPredictionBaselineSnapshot {};
}

ReplayPastTrajectoryUpdate ReplayPrediction::RefreshPastTrajectoryStore( const ReplaySolverRecorder& solver,
                                                                         const ReplayPastTrajectoryView& path )
{
    ReplayPastTrajectoryUpdate update;
    if ( !path.hasTarget || path.targetId.value == 0 )
    {
        update.apply = true;
        return update;
    }

    const ReplayRecorderStats stats = solver.GetStats();
    if ( !stats.enabled || stats.sampleCount == 0 || stats.nextFrameIndex == 0 )
    {
        update.apply = true;
        return update;
    }

    const ReplayFrameIndex oldestFrame = ReplayOldestFrameFromStats( stats );
    const ReplayFrameIndex newestFrame = stats.nextFrameIndex - 1u;
    const bool needsRebuild = !path.valid || path.retainedTargetId.value != path.targetId.value ||
                              path.totalFramesEvicted != stats.totalFramesEvicted || path.firstFrame != oldestFrame ||
                              path.builtThroughFrame < newestFrame;

    if ( !needsRebuild )
    {
        return update;
    }

    const int frameNumber = ReplayTrajectoryFrameNumberForReserve( newestFrame );
    ReplayTrajectoryRecord* record = BeginReplayPastRootTrajectoryRecord( m_state.trajectoryStore,
                                                                          path.targetId,
                                                                          stats.sampleCount,
                                                                          frameNumber );

    if ( !record )
    {
        update.apply = true;
        return update;
    }

    ReplayPastRootRebuildContext rebuild;
    rebuild.store = &m_state.trajectoryStore;
    rebuild.record = record;
    const bool traversalOk = solver.ForEachBodyPositionChronological(
        path.targetId,
        [&]( ReplayFrameIndex frameIndex, SkullbonezCore::Physics::ModelRowHint modelRow, const Vector3& position )
        {
            if ( !rebuild.ok )
            {
                return;
            }

            rebuild.ok = AppendReplayTrajectoryPoint( *rebuild.store, *rebuild.record, frameIndex, position );
            if ( rebuild.ok )
            {
                if ( !rebuild.hasSample )
                {
                    rebuild.firstFrame = frameIndex;
                    rebuild.hasSample = true;
                }

                rebuild.targetModelRow = modelRow;
            }
        } );

    if ( !traversalOk || !rebuild.ok || !rebuild.hasSample )
    {
        update.apply = true;
        return update;
    }

    record->firstFrame = rebuild.firstFrame;
    update.targetId = path.targetId;
    update.firstFrame = oldestFrame;
    update.builtThroughFrame = newestFrame;
    update.totalFramesEvicted = stats.totalFramesEvicted;
    update.fullRebuildCount = path.fullRebuildCount + 1u;
    update.incrementalTrimCount = path.incrementalTrimCount;
    update.targetModelRow = rebuild.targetModelRow;
    update.apply = true;
    update.targetModelRowRepaired = true;
    update.valid = true;
    return update;
}

void ReplayPrediction::AppendPastTrajectorySample( const ReplayRecorderStats& solverStats,
                                                   const ReplayPastTrajectoryView& path,
                                                   const ReplaySolverFrameSample& sample,
                                                   ReplayPastTrajectoryUpdate& update )
{
    if ( !path.hasTarget || path.targetId.value == 0 || !path.valid ||
         path.retainedTargetId.value != path.targetId.value )
    {
        return;
    }

    ReplayTrajectoryRecord* record = m_state.trajectoryStore.FindRecord( ReplayPastRootTrajectoryKey( path.targetId ) );
    if ( !record )
    {
        update.apply = true;
        update.valid = false;
        return;
    }

    update.targetId = path.targetId;
    update.firstFrame = path.firstFrame;
    update.builtThroughFrame = path.builtThroughFrame;
    update.totalFramesEvicted = path.totalFramesEvicted;
    update.fullRebuildCount = path.fullRebuildCount;
    update.incrementalTrimCount = path.incrementalTrimCount;
    update.valid = path.valid;

    const ReplayFrameIndex oldestFrame = ReplayOldestFrameFromStats( solverStats );
    if ( path.totalFramesEvicted != solverStats.totalFramesEvicted || path.firstFrame != oldestFrame )
    {
        // Why: ring eviction advances every live capture once retention is
        // full. Slide the already-published record in place; rebuilding compact
        // solver history here would reconstruct every world snapshot and would
        // also replace the record version that prevents path flicker.
        m_state.trajectoryStore.TrimPublishedPointsBeforeFrame( *record, oldestFrame );
        update.firstFrame = oldestFrame;
        update.totalFramesEvicted = solverStats.totalFramesEvicted;
        ++update.incrementalTrimCount;
        update.apply = true;
    }

    if ( sample.frameIndex <= path.builtThroughFrame )
    {
        return;
    }

    const ReplaySolverBodySample* body = FindReplayBodyByIdWithHint( sample, path.targetId, path.targetModelRow.value );
    if ( !body )
    {
        // The frame was inspected even when the selected body no longer exists;
        // do not trigger a full historical rebuild on the next render pass.
        update.builtThroughFrame = sample.frameIndex;
        update.apply = true;
        return;
    }

    if ( !AppendReplayTrajectoryPoint( m_state.trajectoryStore, *record, sample.frameIndex, body->position ) )
    {
        update.valid = false;
        update.apply = true;
        return;
    }

    update.targetModelRow = body->modelRow;
    update.targetModelRowRepaired = true;
    update.builtThroughFrame = sample.frameIndex;
    update.apply = true;
}

ReplayPredictionMemoryStats ReplayPrediction::CollectMemoryStats() const
{
    ReplayPredictionMemoryStats stats;
    SkullbonezCore::Core::MainMemoryAddReplayCategoryBytes(
        stats.categoryBytes,
        SkullbonezCore::Core::MainMemoryReplayByteCategory::PredictionOwner,
        static_cast<uint64_t>( sizeof( m_state ) ) );

    if ( m_state.simulation.predictionEngine )
    {
        SkullbonezCore::Core::MainMemoryAddReplayCategoryBytes(
            stats.categoryBytes,
            SkullbonezCore::Core::MainMemoryReplayByteCategory::PredictionEngine,
            ReplayPredictionEngineMemoryBytes( *m_state.simulation.predictionEngine ) );
    }

    SkullbonezCore::Core::MainMemoryAddReplayCategoryBytes(
        stats.categoryBytes,
        SkullbonezCore::Core::MainMemoryReplayByteCategory::PredictionWorldState,
        ReplayPredictionWorldSnapshotMemoryBytes( m_state.simulation.predictionWorld ) );

    SkullbonezCore::Core::MainMemoryAddReplayCategoryBytes(
        stats.categoryBytes,
        SkullbonezCore::Core::MainMemoryReplayByteCategory::PredictionBodyState,
        ReplayPredictionVectorCapacityBytes( m_state.simulation.predictionBodies ) );

    SkullbonezCore::Core::MainMemoryAddReplayCategoryBytes(
        stats.categoryBytes,
        SkullbonezCore::Core::MainMemoryReplayByteCategory::PredictionFrameRecords,
        ReplayPredictionVectorCapacityBytes( m_state.simulation.frames ) +
            ReplayPredictionVectorCapacityBytes( m_state.build.buildFrames ) );

    SkullbonezCore::Core::MainMemoryAddReplayCategoryBytes(
        stats.categoryBytes,
        SkullbonezCore::Core::MainMemoryReplayByteCategory::PredictionFutureTree,
        ReplayPredictionVectorCapacityBytes( m_state.futureNodeCache.futureNodes ) +
            ReplayPredictionVectorCapacityBytes( m_state.futureNodeCache.futureNodeBuildScratch ) );

    for ( const RunReplayPredictionFrame& frame : m_state.simulation.frames )
    {
        AddReplayPredictionFrameCategoryBytes( stats.categoryBytes, frame );
    }

    for ( const RunReplayPredictionFrame& frame : m_state.build.buildFrames )
    {
        AddReplayPredictionFrameCategoryBytes( stats.categoryBytes, frame );
    }

    SkullbonezCore::Core::MainMemoryAddReplayCategoryBytes(
        stats.categoryBytes,
        SkullbonezCore::Core::MainMemoryReplayByteCategory::TrajectoryStore,
        m_state.trajectoryStore.CapacityBytes() );

    stats.frameCount = m_state.simulation.frames.size() + m_state.build.buildFrames.size();
    stats.futureNodeCount = m_state.futureNodeCache.futureNodes.size();
    stats.trajectory.storeBytes = m_state.trajectoryStore.CapacityBytes();
    stats.trajectory.recordCount = static_cast<uint64_t>( m_state.trajectoryStore.RecordCount() );
    stats.trajectory.pointCount = static_cast<uint64_t>( m_state.trajectoryStore.PointCount() );
    stats.trajectory.versionChurn = m_state.trajectoryStore.nextVersion > 0u
                                        ? static_cast<uint64_t>( m_state.trajectoryStore.nextVersion - 1u )
                                        : 0u;

    for ( const ReplayTrajectoryRecord& record : m_state.trajectoryStore.records )
    {
        stats.trajectory.publishedPointCount += static_cast<uint64_t>(
            (std::min)( record.publishedPointCount, record.points.size() ) );
        stats.trajectory.maxRecordVersion = (std::max)( stats.trajectory.maxRecordVersion, record.version );
    }

    return stats;
}
