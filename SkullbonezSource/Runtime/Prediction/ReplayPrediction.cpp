/*
Purpose:
  Owns isolated replay simulation setup and frame-thread prediction orchestration.

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
  - A presented evidence view comes from the same Build-or-Committed bank choice
    as the visible prediction frames and is never retained by this coordinator.
*/
#include "ReplayPrediction.h"
#include "../../Assets/EditorHullAssets.h"
#include "ReplayPredictionArchive.h"
#include "ReplayPredictionPublicationOperations.h"
#include "ReplayPredictionReserve.h"
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
namespace Gameplay = SkullbonezCore::Gameplay;
using namespace SkullbonezCore::Math::CollisionDetection;
using namespace SkullbonezCore::Math::Orientation;
using namespace SkullbonezCore::Math::Transformation;
using namespace SkullbonezCore::Physics;

SkullbonezCore::Runtime::ReplayPredictionIsolatedSimulation::~ReplayPredictionIsolatedSimulation() = default;
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
constexpr float REPLAY_PREDICTION_LIVE_THRESHOLD = 0.995f;
constexpr float REPLAY_PREDICTION_PRESENT_EPSILON = 0.0035f;

bool ReplayPredictionAtPresentTrackPosition( float position, float presentPosition ) noexcept
{
    if ( presentPosition >= REPLAY_PREDICTION_LIVE_THRESHOLD )
    {
        return position >= REPLAY_PREDICTION_LIVE_THRESHOLD;
    }

    return std::fabs( position - presentPosition ) <= REPLAY_PREDICTION_PRESENT_EPSILON;
}

// Why: a time budget completes a different number of ticks per frame on every
// CPU, so the frame a horizon finishes on is machine-dependent. Automation that
// captures a frame-exact reveal needs the opposite - a fixed tick count per
// submit, which lands completion on the same frame everywhere. Deterministic
// reveal is the automation-owned flag, so it selects this pacing too.
constexpr int REPLAY_PREDICTION_DETERMINISTIC_TICKS_PER_SUBMIT = 8;

bool TryResolveReplayBodyModelIndex( const PhysicsBodyStore& bodyStore, Physics::PhysicsSceneObjectId id, int modelIndexHint,
                                     int modelCount, int& outModelIndex )
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


bool TryResolveReplayBodyModelIndex( const PhysicsBodyStore& bodyStore, Physics::PhysicsSceneObjectId id, ModelRowHint& hint,
                                     int modelCount, int& outModelIndex )
{
    // Why: retained replay UI state carries modelIndex integers as staleable
    // hints. Naming the cache as ModelRowHint keeps stable scene object identity
    // in Physics::PhysicsSceneObjectId while this resolver heals or invalidates
    // the dense-row guess.
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
bool StepPredictionEngineTick( PhysicsEngine& engine, Gameplay::TornadoGameplay& tornadoGameplay, float fixedDt,
                               const PhysicsWorldForces& worldForces, SkullbonezCore::Threading::WorkerPool& workerPool )
{
    CoreAllocation::RuntimeAllocationScope replayAllocationScope( CoreAllocation::RuntimeAllocationPhase::Replay );
    const SkullbonezCore::Physics::ExternalForceFrameInput
        externalForces = tornadoGameplay
                             .BuildForceFrame( fixedDt,
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

// Why: Worker dispatch is only worth it for large body snapshots. Small
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
constexpr ReplayFrameIndex REPLAY_PREDICTION_REST_GRACE_FRAMES = static_cast<ReplayFrameIndex>(
    REPLAY_PREDICTION_REST_GRACE_SECONDS / PHYSICS_FIXED_DT );
constexpr float REPLAY_PREDICTION_REST_POSITION_EPSILON_SQ = 0.5f * 0.5f;

constexpr uint32_t REPLAY_PREDICTION_CAPTURE_BODY_WORKER_HASH = HashStr(
    "Frame/Replay/Prediction/CaptureBodyState/WorkerBodies" );
constexpr uint32_t REPLAY_PREDICTION_CAPTURE_SAMPLE_WORKER_HASH = HashStr(
    "Frame/Replay/Prediction/CaptureSample/WorkerBodies" );

// Concept: future-node building is an incremental cache.
//
// Prediction can hold thousands of future frames. Clearing and rebuilding the
// future-impact tree every render frame makes the path visualizer scale with the
// full horizon. These cursors let each frame continue where the last frame stopped.
bool CaptureReplayPredictionBodyState( const PhysicsBodyStore& bodyStore, SkullbonezCore::Threading::WorkerPool& workerPool,
                                       SkullbonezCore::Core::Profiler*,
                                       std::vector<RunReplayPredictionBodyBackup>& outBodies )
{
    PROFILE_SCOPED( "Frame/Replay/Prediction/CaptureBodyState" );
    const int modelCount = bodyStore.Count();
    const auto bodyRecords = bodyStore.Records();
    const auto hotFields = bodyStore.HotFields();

    if ( static_cast<int>( bodyRecords.size() ) < modelCount )
    {
        return false;
    }

    outBodies.clear();

    if ( !ReserveReplayPredictionVector( outBodies, static_cast<std::size_t>( modelCount ), 0,
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
        workerPool.ParallelForNoAlloc( 0, modelCount, captureBody, REPLAY_PREDICTION_PARALLEL_BODY_MIN,
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


bool ApplyReplayPredictionBodyState( PhysicsEngine& physicsEngine, SkullbonezCore::Core::Profiler*,
                                     const std::vector<RunReplayPredictionBodyBackup>& bodies )
{
    PROFILE_SCOPED( "Frame/Replay/Prediction/ApplyBodyState" );
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


bool SeedReplayPredictionEngine( RunReplayPredictionState& prediction, SkullbonezCore::Core::Profiler* profiler,
                                 const PhysicsEngine& liveEngine, const SkullbonezCore::Core::EngineConfig& config,
                                 const PhysicsWorldForces& worldForces, int modelCount )
{
    PROFILE_SCOPED( "Frame/Replay/Prediction/SeedPrivateEngine" );
    const int currentBytes = prediction.simulation.predictionEngineReserveBytes;

    if ( prediction.simulation.predictionEngine && currentBytes <= 0 )
    {
        return false;
    }

    prediction.simulation.predictionEngineReady = false;

    // Invariant: predictionEngineReady remains false across the synchronous
    // Physics seed and both restores below. No worker may observe or step the
    // intentionally partial topology/store seed before body and solver state
    // are coherent.
    int reservedBytes = 0;

    if ( !SeedReplayPredictionEngineStorage( prediction.simulation.predictionEngine, liveEngine, currentBytes,
                                             reservedBytes ) )
    {
        return false;
    }

    // Invariant: seeding starts from the live facade's topology and cold policy,
    // then restores the captured prediction values into the private engine. The
    // live engine is never passed to prediction stepping after this point.
    PhysicsEngine& predictionEngine = *prediction.simulation.predictionEngine;
    prediction.simulation.predictionEngineReserveBytes = reservedBytes;
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


bool CaptureReplayPredictionFrame( ReplayPrediction& predictionOwner, RunReplayPredictionState& prediction,
                                   const PhysicsEngine& physicsEngine, SkullbonezCore::Threading::WorkerPool& workerPool,
                                   int modelCount, ReplayFrameIndex frameIndex )
{
    PROFILE_SCOPED( "Frame/Replay/Prediction/CaptureSample" );
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
        workerPool.ParallelForNoAlloc( 0, modelCount, captureBody, REPLAY_PREDICTION_PARALLEL_BODY_MIN,
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

    const auto debugContacts = SkullbonezCore::Physics::PhysicsEngine::ReadDebugContacts( physicsEngine );
    frame.debugContacts.clear();

    // Why: persistent solver manifolds can contain thousands of rows on every
    // tick. The cause tree consumes only the first edge that makes each body
    // reachable from the selected root. Retaining those activation edges keeps
    // the exact point/normal used by inspection while preventing two 20-second
    // frame banks from exhausting Prediction's working-set cap.
    bool activatedBody = true;

    while ( activatedBody && prediction.build.causalContactNodeCount < REPLAY_PATH_MAX_FUTURE_NODES )
    {
        activatedBody = false;

        for ( const Physics::PhysicsDebugContact& contact : debugContacts )
        {
            const bool bodyAValid = contact.bodyA >= 0 && contact.bodyA < modelCount;
            const bool bodyBValid = contact.bodyB >= 0 && contact.bodyB < modelCount;

            if ( !bodyAValid || !bodyBValid )
            {
                continue;
            }

            const bool bodyAActive = prediction.build.causalContactActiveModels[static_cast<std::size_t>( contact.bodyA )] !=
                                     0u;
            const bool bodyBActive = prediction.build.causalContactActiveModels[static_cast<std::size_t>( contact.bodyB )] !=
                                     0u;

            if ( bodyAActive == bodyBActive )
            {
                continue;
            }

            if ( frame.debugContacts.size() >= frame.debugContacts.capacity() )
            {
                frame.contactsIncomplete = true;
                activatedBody = false;
                break;
            }

            frame.debugContacts.push_back( contact );
            const int activatedModel = bodyAActive ? contact.bodyB : contact.bodyA;
            prediction.build.causalContactActiveModels[static_cast<std::size_t>( activatedModel )] = 1u;
            ++prediction.build.causalContactNodeCount;
            activatedBody = true;

            if ( prediction.build.causalContactNodeCount >= REPLAY_PATH_MAX_FUTURE_NODES )
            {
                break;
            }
        }
    }

    if ( !PublishReplayPredictionRootTrajectoryFrame( prediction, frame, frameSlot ) )
    {
        return false;
    }

    if ( !predictionOwner.SealSolverEvidenceFrame( frameIndex ) )
    {
        return false;
    }

    // Invariant: the frame prefix is the outer publication edge. Before an
    // evidence bank reaches its hard cap, High detail has already copied and
    // release-published the matching row; later frames deliberately expose no
    // evidence view while the authoritative trajectory continues.
    prediction.PublishBuildFrameSlot( frameSlot );
    return true;
}
} // namespace

namespace
{
// Concept: prediction visualizer orchestration.
//
// These operations share the private setup, stepping, and publication helpers
// above; cross-unit declarations remain on the named Prediction owner headers.
void MarkReplayPredictionWorkerFailed( RunReplayPredictionState& prediction )
{
    prediction.build.publication.MarkWorkerFailed();
}

int RunReplayPredictionWorkerRange( ReplayPrediction& predictionOwner, RunReplayPredictionState& prediction,
                                    const SkullbonezCore::Core::EngineConfig& config,
                                    SkullbonezCore::Threading::WorkerPool& workerPool, int modelCount, int beginTickIndex,
                                    int endTickIndex )
{
    PROFILE_SCOPED( "Frame/Replay/Prediction/WorkerRange" );

    if ( prediction.build.publication.WorkerFailed() || !prediction.simulation.predictionEngineReady ||
         !prediction.simulation.predictionEngine )
    {
        MarkReplayPredictionWorkerFailed( prediction );
        return 0;
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

        bool stepSucceeded = false;

        {
            PROFILE_SCOPED( "Frame/Replay/Prediction/WorkerRange/PhysicsStep" );
            stepSucceeded = StepPredictionEngineTick( predictionEngine, prediction.simulation.predictionTornadoGameplay,
                                                      PHYSICS_FIXED_DT, prediction.simulation.predictionWorldForces,
                                                      workerPool );
        }

        bool captureSucceeded = false;

        if ( stepSucceeded )
        {
            PROFILE_SCOPED( "Frame/Replay/Prediction/WorkerRange/CaptureSample" );
            captureSucceeded = predictionOwner.RefreshSolverEvidenceSource( predictionEngine, modelCount ) &&
                               CaptureReplayPredictionFrame( predictionOwner, prediction, predictionEngine, workerPool,
                                                             modelCount, static_cast<ReplayFrameIndex>( predictionTick ) );
        }

        if ( !stepSucceeded || !captureSucceeded )
        {
            MarkReplayPredictionWorkerFailed( prediction );
            return completedTicks;
        }

        prediction.build.nextTick = predictionTick + 1;
        ++completedTicks;

        // Invariant: a physics tick is indivisible. Check the real worker
        // clock only after publishing the completed tick, then leave the
        // unprocessed range at the task cursor for the next frame.
        if ( !prediction.revealClock.deterministicFrameEnabled &&
             std::chrono::duration<double, std::milli>( std::chrono::steady_clock::now() - probeStart ).count() >=
                 REPLAY_PREDICTION_MAX_WORK_MILLISECONDS )
        {
            // Invariant: deterministic capture stops on the submitted tick count
            // instead, so the same range completes on the same frame regardless
            // of how much work this machine fits into five milliseconds.
            break;
        }
    }

    if ( completedTicks > 0 )
    {
        const double elapsedMs = std::chrono::duration<double, std::milli>( std::chrono::steady_clock::now() - probeStart )
                                     .count();

        const double measuredTicksPerMs = prediction.simulation.measuredTicksPerMs.load( std::memory_order_relaxed );

        if ( measuredTicksPerMs <= 0.0 )
        {
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
        else
        {
            prediction.simulation.measuredTicksPerMs.store( UpdateReplayPredictionTicksPerMs( measuredTicksPerMs,
                                                                                              completedTicks, elapsedMs ),
                                                            std::memory_order_release );
        }
    }

    return completedTicks;
}

} // namespace

int ReplayPrediction::RunWorkerRange( const SkullbonezCore::Core::EngineConfig& config,
                                      SkullbonezCore::Threading::WorkerPool& workerPool, int modelCount, int beginTickIndex,
                                      int endTickIndex )
{
    return RunReplayPredictionWorkerRange( *this, m_state, config, workerPool, modelCount, beginTickIndex, endTickIndex );
}

namespace
{
bool CompleteReplayPredictionJobOnFrameThread( ReplayPrediction& predictionOwner, RunReplayPredictionState& prediction,
                                               double simulationTotalSeconds, bool historicalSamplePaused,
                                               float solverTrackPosition, float solverPresentTrackPosition,
                                               ReplayPredictionUpdateResult& result )
{
    if ( prediction.build.publication.WorkerFailed() )
    {
        const bool preserveCommittedFuture = prediction.HasCommittedFramePrefix();
        predictionOwner.CancelJob( !preserveCommittedFuture, preserveCommittedFuture );
        prediction.build.dirty = true;
        return false;
    }

    if ( !prediction.build.schedule.CompleteAndIdle() )
    {
        return false;
    }

    if ( prediction.simulation.predictionEngine )
    {
        const Gameplay::TornadoGameplay& tornadoGameplay = prediction.simulation.predictionTornadoGameplay;
        prediction.simulation.predictionWorld.tornadoConfig = tornadoGameplay.GetFieldConfig();
        prediction.simulation.predictionWorld.tornadoSystemConfig = tornadoGameplay.GetSystemConfig();
        prediction.simulation.predictionWorld.tornadoSystemElapsedSeconds = tornadoGameplay.GetSystemElapsedSeconds();
        prediction.simulation.predictionWorld.tornadoCaptureSeconds = tornadoGameplay.CaptureSeconds();
        prediction.simulation.predictionWorld.tornadoEjectCooldownSeconds = tornadoGameplay.EjectCooldownSeconds();
    }

    const bool hadCommittedPredictionFrames = prediction.HasCommittedFramePrefix();
    const bool solverWasOldLiveEdge = !hadCommittedPredictionFrames &&
                                      ReplayPredictionAtPresentTrackPosition( solverTrackPosition, 1.0f );

    const bool scrubberWasPinnedToPresent = !historicalSamplePaused ||
                                            ReplayPredictionAtPresentTrackPosition( solverTrackPosition,
                                                                                    solverPresentTrackPosition ) ||
                                            solverWasOldLiveEdge;

    // Invariant: the trajectory builder switches to the build bank before its
    // prefix is necessarily presented. Only this presentation latch proves the
    // completed build replaced the coherent snapshot captured at job start.
    const bool buildPrefixWasPresented = prediction.BuildPrefixHasBeenPresented();

    prediction.build.schedule.Reset();
    prediction.build.building = false;
    prediction.build.complete = true;
    prediction.build.lastBuildWallMs = std::chrono::duration<double, std::milli>( std::chrono::steady_clock::now() -
                                                                                  prediction.build.jobStart )
                                           .count();

    const std::size_t completedFrameCount = prediction.build.buildFrames.size();
    const std::size_t presentedBuildFrameCount = prediction.build.presentationPublication
                                                     .PresentedCount( prediction.PublishedBuildFrameCount(),
                                                                      completedFrameCount );

    if ( !predictionOwner.PromoteSolverEvidenceBuild() )
    {
        predictionOwner.CancelJob( !hadCommittedPredictionFrames, hadCommittedPredictionFrames );
        prediction.build.dirty = true;
        return false;
    }

    prediction.PromoteBuildFramesToCommitted( completedFrameCount );

    // Why: the swapped-out committed bank is the next build's allocation-free
    // scratch. Reset publication below; do not destroy its per-frame capacities.
    prediction.ResetBuildFramePublication();

    const bool retainCapturedCommittedBank = hadCommittedPredictionFrames && !buildPrefixWasPresented;
    const std::size_t visibleFrameCount = buildPrefixWasPresented ? presentedBuildFrameCount : completedFrameCount;

    if ( retainCapturedCommittedBank )
    {
        // Lifetime: the prior committed bank moved to build storage in the
        // frame swap above; its trajectory branch identity does not change.
        prediction.committedPublication.visibleFramesUseBuildBank = true;
    }

    const bool publicationBegan = retainCapturedCommittedBank
                                      ? prediction.committedPublication
                                            .ActivateCaptured( prediction.build.generationBeginCount, completedFrameCount )
                                      : prediction.committedPublication
                                            .Begin( prediction.trajectoryBuild, prediction.futureNodeCache,
                                                    prediction.build.generationBeginCount, completedFrameCount,
                                                    prediction.simulation.targetModelRow, true, false, visibleFrameCount,
                                                    prediction.trajectoryStore.publicationVersion );

    if ( !publicationBegan )
    {
        // Runtime allocation policy: begin-job reserve owns this fixed snapshot
        // capacity; failure never permits opportunistic growth.
        // Invariant: reserve failure rejects the generation before a mixed bank
        // can become visible on the completion frame.
        prediction.build.dirty = true;
        return false;
    }

    const ReplayPredictionTrajectoryBank replacementBank = prediction.committedPublication.ReplacementTrajectoryBank();

    if ( !RebuildReplayPredictionReplacementRootTrajectory( prediction, replacementBank ) )
    {
        prediction.build.dirty = true;
        return false;
    }

    if ( prediction.baseline.valid )
    {
        UpdateReplayPredictionBaselineDivergence( prediction, prediction.simulation.frames,
                                                  prediction.CommittedFrameCount() );
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

    // Invariant: the approximate selected path remains visible through the
    // worker swap. Only the generation armed by the release edge may replace
    // it with authoritative committed trajectory data.
    (void)prediction.velocityDragPreview.ClearAfterGeneration( prediction.build.generationBeginCount );
    prediction.build.lastBuildTime = simulationTotalSeconds;
    return true;
}

} // namespace

ReplayPredictionSourcePreparation ReplayPrediction::BeginFrameSource(
    PhysicsEngine& physicsEngine, const SkullbonezCore::Core::EngineConfig& config, bool scenePhysics,
    double fallbackSourceSimulationSeconds, double simulationTotalSeconds, const ReplaySolverFrameSample* latestSolverSample,
    const ReplayPastTrajectoryView& requestedPath, const std::chrono::steady_clock::time_point& budgetStart,
    double budgetMilliseconds, ReplayPredictionUpdateResult& result )
{
    ReplayPrediction& predictionOwner = *this;
    RunReplayPredictionState& prediction = m_state;
    PROFILE_SCOPED( "Frame/Replay/Prediction/BeginJob" );

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
    if ( ReplayPredictionBudgetExpiredForPass( result, SkullbonezCore::Core::MainMemoryReplayBudgetPass::PredictionBegin,
                                               budgetStart, budgetMilliseconds ) )
    {
        return ReplayPredictionSourcePreparation::Declined;
    }

    const ReplayFrameIndex sourceFrameIndex = latestSolverSample ? latestSolverSample->frameIndex : 0;
    const uint64_t sourceSolverHash = latestSolverSample ? latestSolverSample->solverHash : 0;
    const ReplayFrameIndex previousSourceFrameIndex = prediction.simulation.sourceFrameIndex;
    const uint64_t previousSourceSolverHash = prediction.simulation.sourceSolverHash;
    const bool sameTargetRefresh = prediction.simulation.targetId.value == requestedPath.targetId.value;
    const bool sameVisibleTarget = prediction.committedPublication.PublicationTargetId( requestedPath.targetId ).value ==
                                   requestedPath.targetId.value;
    const bool preserveCommittedFuture = prediction.enabled && scenePhysics && requestedPath.targetId.value != 0 &&
                                         sameTargetRefresh && sameVisibleTarget && prediction.HasCommittedFramePrefix();

    const std::size_t
        buildPresentationFrameCount = preserveCommittedFuture
                                          ? ReplayPredictionBuildPresentationFrameCountForRefresh( prediction,
                                                                                                   requestedPath.targetId )
                                          : 2u;

    const bool clearSamplesOnCancel = !preserveCommittedFuture;

    if ( preserveCommittedFuture && !prediction.committedPublication.visibleSnapshotCaptured &&
         !prediction.committedPublication.CaptureVisible( prediction.trajectoryBuild, prediction.futureNodeCache,
                                                          prediction.simulation.targetModelRow, true, false,
                                                          prediction.CommittedFrameCount(),
                                                          prediction.trajectoryStore.publicationVersion ) )
    {
        prediction.build.dirty = true;
        return ReplayPredictionSourcePreparation::Declined;
    }

    // Lifetime: same-target cancellation retains the coherent reader bank
    // captured above until its replacement prefix is presented. A new target,
    // including one queued while a rootless build completes, starts without
    // that bank because its publication root belongs to the previous request.
    predictionOwner.CancelJob( clearSamplesOnCancel, preserveCommittedFuture );

    if ( clearSamplesOnCancel )
    {
        predictionOwner.ClearFutureNodeCache();

        // Why: only a genuinely empty replacement should replay the causal
        // story from the root. Same-target refreshes preserve the anchor and
        // keep showing the committed future until the new prefix catches up.
        prediction.revealClock.anchor = std::chrono::steady_clock::now();
        prediction.revealClock.anchorValid = true;
    }

    prediction.simulation.targetId = requestedPath.targetId;
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

    if ( requestedPath.hasTarget && requestedPath.targetId.value != 0 )
    {
        ModelRowHint targetHint = requestedPath.targetModelRow;
        int targetIndex = -1;

        if ( ReplayPredictionBudgetExpiredForPass( result, SkullbonezCore::Core::MainMemoryReplayBudgetPass::PredictionBegin,
                                                   budgetStart, budgetMilliseconds ) )
        {
            prediction.build.dirty = true;
            return ReplayPredictionSourcePreparation::Declined;
        }

        if ( !TryResolveReplayBodyModelIndex( liveBodyStore, requestedPath.targetId, targetHint, modelCount, targetIndex ) )
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


bool ReplayPrediction::BeginFrameSimulation( PhysicsEngine& physicsEngine, const Gameplay::TornadoGameplay& tornadoGameplay,
                                             int sceneEntityCount, const SkullbonezCore::Core::EngineConfig& config,
                                             const SkullbonezCore::Physics::PhysicsWorldForces& worldForces,
                                             ReplayPredictionPathPresentation pathPresentation, float minHorizonSeconds,
                                             float maxHorizonSeconds, SkullbonezCore::Threading::WorkerPool& workerPool,
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

    prediction.simulation.horizonSeconds = std::clamp( prediction.simulation.horizonSeconds, minHorizonSeconds,
                                                       maxHorizonSeconds );

    const int predictionTicks = (std::max)( 1, static_cast<int>(
                                                   std::ceil( prediction.simulation.horizonSeconds / PHYSICS_FIXED_DT ) ) );

    prediction.build.targetTickCount = predictionTicks;
    prediction.build.nextTick = 1;
    const std::size_t buildFrameCapacity = static_cast<std::size_t>( predictionTicks + 1 );
    const std::size_t buildPresentationFrameCount = prediction.build.buildPresentationFrameCount;

    if ( !ReserveReplayPredictionVector( prediction.build.buildFrames, buildFrameCapacity, 0,
                                         "RunReplayPredictionBuildState::buildFrames" ) )
    {
        predictionOwner.CancelJob( clearSamplesOnCancel, !clearSamplesOnCancel );
        prediction.build.dirty = true;
        return false;
    }

    prediction.build.buildFrames.resize( buildFrameCapacity );
    prediction.ResetBuildFramePublication();

    // Why: ResetBuildFramePublication clears stale bank state, while this
    // generation's threshold was chosen from its request kind before reserve.
    prediction.build.buildPresentationFrameCount = buildPresentationFrameCount;

    if ( !ReserveReplayPredictionFramePayloadVectors( prediction.build.buildFrames, buildFrameCapacity,
                                                      static_cast<std::size_t>( modelCount ), 0,
                                                      "RunReplayPredictionFrame::bodies",
                                                      &RunReplayPredictionFrame::bodies ) )
    {
        predictionOwner.CancelJob( clearSamplesOnCancel, !clearSamplesOnCancel );
        prediction.build.dirty = true;
        return false;
    }

    // Why: each frame retains at most one activation edge per causal node. The
    // reserve therefore follows the tree cap instead of the Physics manifold
    // count, which can remain dense for every tick of a long horizon.
    const std::size_t initialDebugContactCapacity = ReplayPredictionInitialDebugContactCapacity( modelCount );
    (void)ReserveReplayPredictionFramePayloadVectors( prediction.build.buildFrames, buildFrameCapacity,
                                                      initialDebugContactCapacity, 0,
                                                      "RunReplayPredictionFrame::debugContacts",
                                                      &RunReplayPredictionFrame::debugContacts );

    if ( !ReserveReplayPredictionVector( prediction.build.causalContactActiveModels, static_cast<std::size_t>( modelCount ),
                                         0, "RunReplayPredictionBuildState::causalContactActiveModels" ) )
    {
        predictionOwner.CancelJob( clearSamplesOnCancel, !clearSamplesOnCancel );
        prediction.build.dirty = true;
        return false;
    }

    prediction.build.causalContactActiveModels.assign( static_cast<std::size_t>( modelCount ), uint8_t { 0u } );
    prediction.build.causalContactNodeCount = 0u;

    if ( prediction.simulation.targetModelRow.value >= 0 && prediction.simulation.targetModelRow.value < modelCount )
    {
        prediction.build
            .causalContactActiveModels[static_cast<std::size_t>( prediction.simulation.targetModelRow.value )] = 1u;
    }

    if ( !ReserveReplayPredictionVector( prediction.futureNodeCache.futureNodes, REPLAY_PATH_MAX_FUTURE_NODES, 0,
                                         "RunReplayPredictionFutureNodeCache::futureNodes" ) ||
         !ReserveReplayPredictionVector( prediction.futureNodeCache.futureNodeBuildScratch, REPLAY_PATH_MAX_FUTURE_NODES, 0,
                                         "RunReplayPredictionFutureNodeCache::futureNodeBuildScratch" ) ||
         !ReserveReplayPredictionVector( prediction.committedPublication.visibleFutureNodes, REPLAY_PATH_MAX_FUTURE_NODES, 0,
                                         "ReplayPredictionCommittedPublicationState::visibleFutureNodes" ) )
    {
        predictionOwner.CancelJob( clearSamplesOnCancel, !clearSamplesOnCancel );
        prediction.build.dirty = true;
        return false;
    }

    if ( !PrepareReplayPredictionTrajectoryBuild( prediction, prediction.simulation.targetId, buildFrameCapacity,
                                                  static_cast<std::size_t>( modelCount ), pathPresentation ) )
    {
        predictionOwner.CancelJob( clearSamplesOnCancel, !clearSamplesOnCancel );
        prediction.build.dirty = true;
        return false;
    }

    if ( modelCount != SkullbonezCore::Physics::PhysicsEngine::ReadColliders( physicsEngine ).Count() ||
         modelCount != sceneEntityCount ||
         !CaptureReplayPredictionBodyState( liveBodyStore, workerPool, predictionOwner.ProfilerBorrow(),
                                            prediction.simulation.predictionBodies ) )
    {
        predictionOwner.CancelJob( clearSamplesOnCancel, !clearSamplesOnCancel );
        return false;
    }

    physicsEngine.CaptureReplaySimulationSnapshot( prediction.simulation.predictionWorld.physics,
                                                   MakePhysicsBodyCountFromNonNegativeInt( modelCount ) );

    prediction.simulation.predictionTornadoGameplay.SetReplayState( tornadoGameplay.CaptureSeconds(),
                                                                    tornadoGameplay.EjectCooldownSeconds(),
                                                                    tornadoGameplay.GetFieldConfig(),
                                                                    tornadoGameplay.GetSystemConfig(),
                                                                    tornadoGameplay.GetSystemElapsedSeconds() );

    prediction.simulation.predictionTornadoGameplay.SetParallelForceEvaluation( tornadoGameplay.ParallelForceEvaluation() );
    prediction.simulation.predictionWorld.tornadoConfig = tornadoGameplay.GetFieldConfig();
    prediction.simulation.predictionWorld.tornadoSystemConfig = tornadoGameplay.GetSystemConfig();
    prediction.simulation.predictionWorld.tornadoSystemElapsedSeconds = tornadoGameplay.GetSystemElapsedSeconds();
    prediction.simulation.predictionWorld.tornadoCaptureSeconds = tornadoGameplay.CaptureSeconds();
    prediction.simulation.predictionWorld.tornadoEjectCooldownSeconds = tornadoGameplay.EjectCooldownSeconds();

    if ( !SeedReplayPredictionEngine( prediction, predictionOwner.ProfilerBorrow(), physicsEngine, config, worldForces,
                                      modelCount ) )
    {
        predictionOwner.CancelJob( clearSamplesOnCancel, !clearSamplesOnCancel );
        prediction.build.dirty = true;
        return false;
    }

    if ( !prediction.simulation.predictionEngine ||
         !predictionOwner.BeginSolverEvidenceBuild( prediction.build.generationBeginCount + 1u ) ||
         !CaptureReplayPredictionFrame( predictionOwner, prediction, *prediction.simulation.predictionEngine, workerPool,
                                        modelCount, 0 ) )
    {
        predictionOwner.CancelJob( clearSamplesOnCancel, !clearSamplesOnCancel );
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
        prediction.build.schedule.Begin( prediction.build.targetTickCount, 1,
                                         ReplayPredictionSimulationSlice { &predictionOwner, &config, &workerPool,
                                                                           modelCount } );

        prediction.build.schedule.SetBudget( 1 );
    }
    prediction.build.building = true;
    ++prediction.build.generationBeginCount;

    return !prediction.build.buildFrames.empty();
}

namespace
{

bool StepReplayPredictionJob( ReplayPrediction& predictionOwner, RunReplayPredictionState& prediction,
                              SkullbonezCore::Threading::WorkerPool& workerPool, double simulationTotalSeconds,
                              bool historicalSamplePaused, float solverTrackPosition, float solverPresentTrackPosition,
                              const std::chrono::steady_clock::time_point& budgetStart, double budgetMilliseconds,
                              ReplayPredictionUpdateResult& result )
{
    PROFILE_SCOPED( "Frame/Replay/Prediction/Slice" );

    if ( !prediction.build.building )
    {
        return prediction.build.complete;
    }

    if ( ReplayPredictionBudgetExpiredForPass( result, SkullbonezCore::Core::MainMemoryReplayBudgetPass::PredictionStep,
                                               budgetStart, budgetMilliseconds ) )
    {
        return false;
    }

    if ( !prediction.simulation.predictionEngineReady || !prediction.simulation.predictionEngine ||
         !prediction.build.schedule.Active() )
    {
        const bool preserveCommittedFuture = prediction.HasCommittedFramePrefix();
        predictionOwner.CancelJob( !preserveCommittedFuture, preserveCommittedFuture );
        prediction.build.dirty = true;
        return false;
    }

    if ( prediction.revealClock.deterministicFrameEnabled )
    {
        // Invariant: an Automation frame owns exactly one fixed-size worker
        // slice. Without this join, the headless frame loop can outrun the
        // worker and reach its fixed reveal frame with an incomplete horizon;
        // machine speed would then decide whether identical evidence passes.
        prediction.build.schedule.WaitForIdle();
    }

    if ( prediction.build.buildMode == ReplayPredictionBuildMode::Undecided )
    {
        const double measuredTicksPerMs = prediction.simulation.measuredTicksPerMs.load( std::memory_order_acquire );
        const std::size_t publishedFrameCount = prediction.PublishedBuildFrameCount();
        const int completedTicks = static_cast<int>( publishedFrameCount > 0u ? publishedFrameCount - 1u : 0u );
        const double instantBudgetMilliseconds = (std::min)( prediction.build.instantBudgetMs, budgetMilliseconds );
        prediction.build.buildMode = ChooseReplayPredictionBuildMode( measuredTicksPerMs,
                                                                      (std::max)( 0, prediction.build.targetTickCount -
                                                                                         completedTicks ),
                                                                      instantBudgetMilliseconds,
                                                                      prediction.simulation.predictionBodies.size() );
    }

    const std::size_t publishedFrameCount = prediction.PublishedBuildFrameCount();
    const int completedTicks = static_cast<int>( publishedFrameCount > 0u ? publishedFrameCount - 1u : 0u );
    const int remainingTicks = (std::max)( 0, prediction.build.targetTickCount - completedTicks );

    // Invariant: every mode offers the worker the complete remaining horizon.
    // The worker's steady-clock check, not a predicted tick count, stops the
    // submitted range at the first completed tick at or after five milliseconds.
    if ( prediction.build.buildMode == ReplayPredictionBuildMode::Instant )
    {
        PROFILE_SCOPED( "Frame/Replay/Prediction/Slice/Instant" );
        prediction.build.schedule.SetBudget( remainingTicks );
    }
    else if ( prediction.build.buildMode == ReplayPredictionBuildMode::Undecided )
    {
        PROFILE_SCOPED( "Frame/Replay/Prediction/Slice/Probe" );
        prediction.build.schedule.SetBudget( remainingTicks );
    }
    else
    {
        PROFILE_SCOPED( "Frame/Replay/Prediction/Slice/Amortized" );
        prediction.build.schedule.SetBudget( remainingTicks );
    }

    if ( prediction.revealClock.deterministicFrameEnabled )
    {
        // Why: the submitted range, not a clock, is the stop condition under
        // deterministic capture. Offering the whole remaining horizon here would
        // let a fast machine finish it in one submit and a slow one take many.
        prediction.build.schedule.SetBudget( REPLAY_PREDICTION_DETERMINISTIC_TICKS_PER_SUBMIT );
    }

    prediction.build.schedule.SubmitTick( workerPool );

    if ( CompleteReplayPredictionJobOnFrameThread( predictionOwner, prediction, simulationTotalSeconds,
                                                   historicalSamplePaused, solverTrackPosition, solverPresentTrackPosition,
                                                   result ) )
    {
        return true;
    }

    if ( prediction.build.publication.WorkerFailed() )
    {
        (void)CompleteReplayPredictionJobOnFrameThread( predictionOwner, prediction, simulationTotalSeconds,
                                                        historicalSamplePaused, solverTrackPosition,
                                                        solverPresentTrackPosition, result );
        return false;
    }

    return prediction.PublishedBuildFrameCount() >= 2u || prediction.build.complete;
}


} // namespace

ReplayPredictionFrameSourceAction ReplayPrediction::SelectFrameSource( const ReplaySolverFrameSample* latestSolverSample,
                                                                       Physics::PhysicsSceneObjectId targetId,
                                                                       bool targetAvailable, bool liveAdvanceHeld,
                                                                       double simulationTotalSeconds, bool& outWasDirty,
                                                                       bool& outWasPendingLatestRestart )
{
    ReplayPrediction& predictionOwner = *this;
    RunReplayPredictionState& prediction = m_state;
    outWasDirty = false;
    outWasPendingLatestRestart = false;

    PROFILE_SCOPED( "Frame/Replay/Prediction/SelectSource" );

    if ( !prediction.enabled )
    {
        if ( prediction.build.building )
        {
            predictionOwner.CancelJob( false );
        }

        return ReplayPredictionFrameSourceAction::Stop;
    }

    const ReplayPredictionPendingPublicationAction
        pendingPublicationAction = ChooseReplayPredictionPendingPublicationAction( prediction.committedPublication.pending,
                                                                                   prediction.simulation.targetId, targetId,
                                                                                   prediction.committedPublication
                                                                                       .visibleTrajectoryBuild.rootId );

    if ( pendingPublicationAction == ReplayPredictionPendingPublicationAction::Wait )
    {
        // Lifetime: completion may still present the swapped-out committed
        // frame bank. Defer a same-target refresh until the hidden replacement
        // flips, so begin-job reserve cannot resize that visible storage.
        return ReplayPredictionFrameSourceAction::Continue;
    }

    if ( pendingPublicationAction == ReplayPredictionPendingPublicationAction::Discard )
    {
        // Hazard: Predict may finish before the first object is selected. That
        // rootless publication cannot satisfy the overlay flip. A different
        // target likewise supersedes this hidden duplicate; retaining either
        // pending token would prevent the latest target from starting.
        prediction.committedPublication.Reset();
    }

    if ( !targetAvailable )
    {
        predictionOwner.ClearFutureNodeCache();
    }

    if ( !predictionOwner.GenerationPermitted() )
    {
        // Test probe: the archive may remain visually enabled, but
        // this branch draws only restored values and never reaches a snapshot,
        // reserve, worker, or future-simulation path.
        prediction.build.dirty = false;
        prediction.build.pendingLatestRestart = false;

        // Invariant: ReplayPrediction::EnterOfflineVerification already joined
        // and retired the worker. Cancelling here would invalidate the restored
        // complete/build and trajectory state before the CPU projection reads
        // it, producing a different packet without starting new simulation.
        return ReplayPredictionFrameSourceAction::Stop;
    }

    const ReplayFrameIndex latestFrame = latestSolverSample ? latestSolverSample->frameIndex : 0;
    const uint64_t latestHash = latestSolverSample ? latestSolverSample->solverHash : 0;
    const double now = simulationTotalSeconds;
    const bool targetChanged = prediction.simulation.targetId.value != targetId.value;
    prediction.build.dirty = ReplayPredictionExplicitRestartRequested( prediction.build.dirty,
                                                                       prediction.simulation.targetId, targetId );
    const bool sourceChanged = targetChanged || prediction.simulation.sourceFrameIndex != latestFrame ||
                               prediction.simulation.sourceSolverHash != latestHash;

    const bool refreshDue = ( now - prediction.build.lastBuildTime ) >= REPLAY_PREDICTION_REFRESH_SECONDS;
    const bool hasCommittedPrediction = prediction.HasCommittedFramePrefix();

    // Invariant: a committed prediction is a frozen future for the current
    // branch. Space-stepping the paused live scene changes solver frame/hash,
    // but must not redraw the preview; explicit dirty events such as branch,
    // target, horizon, or predict toggles are the only rebuild triggers.
    const bool allowAutomaticRefresh = !liveAdvanceHeld && !hasCommittedPrediction;
    const ReplayPredictionCoalescerAction
        coalescerAction = ChooseReplayPredictionCoalescerAction( prediction.build.dirty, prediction.build.building,
                                                                 prediction.build.buildMode,
                                                                 prediction.build.pendingLatestRestart,
                                                                 prediction.BuildPrefixHasBeenPresented() );

    if ( coalescerAction == ReplayPredictionCoalescerAction::PromoteAndBegin )
    {
        if ( !predictionOwner.PromoteBuildPrefixToCommitted() )
        {
            // Hazard: retain the dirty and pending-restart tokens if promotion
            // cannot acquire a coherent prefix. The next frame retries without
            // discarding the path visible when this request arrived.
            return ReplayPredictionFrameSourceAction::Continue;
        }

        // Lifetime: the promoted build trajectory remains the visible branch
        // until budgeted committed duplication flips it. Preserve the dirty
        // restart request; the pending-state guard begins it after that flip.
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


void ReplayPrediction::PrepareFrameRebuild( Physics::PhysicsSceneObjectId targetId, ModelRowHint targetModelRow,
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

    if ( prediction.baseline.comparisonActive && !prediction.baseline.valid && prediction.HasCommittedFramePrefix() )
    {
        if ( !CaptureReplayPredictionBaselineSnapshot( prediction, prediction.simulation.frames,
                                                       prediction.CommittedFrameCount(), targetId, targetModelRow.value ) )
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
                                                double budgetMilliseconds, ReplayPredictionUpdateResult& result )
{
    return ReplayPredictionBudgetExpiredForPass( result, SkullbonezCore::Core::MainMemoryReplayBudgetPass::PredictionBegin,
                                                 budgetStart, budgetMilliseconds );
}


bool ReplayPrediction::AdvanceFrameWorker( SkullbonezCore::Threading::WorkerPool& workerPool, double simulationTotalSeconds,
                                           bool historicalSamplePaused, float solverTrackPosition,
                                           float solverPresentTrackPosition,
                                           const std::chrono::steady_clock::time_point& budgetStart,
                                           double budgetMilliseconds, ReplayPredictionUpdateResult& result )
{
    RunReplayPredictionState& prediction = m_state;
    bool predictionCompletedThisPass = false;

    if ( prediction.build.building )
    {
        const double remainingMilliseconds = ReplayPredictionRemainingMilliseconds( budgetStart, budgetMilliseconds );

        if ( remainingMilliseconds > 0.0 )
        {
            const bool wasBuilding = prediction.build.building;
            StepReplayPredictionJob( *this, prediction, workerPool, simulationTotalSeconds, historicalSamplePaused,
                                     solverTrackPosition, solverPresentTrackPosition, budgetStart, budgetMilliseconds,
                                     result );

            predictionCompletedThisPass = wasBuilding && prediction.build.complete && !prediction.build.building;
            (void)ReplayPredictionBudgetExpiredForPass( result,
                                                        SkullbonezCore::Core::MainMemoryReplayBudgetPass::PredictionStep,
                                                        budgetStart, budgetMilliseconds );
        }
        else
        {
            (void)ReplayPredictionBudgetExpiredForPass( result,
                                                        SkullbonezCore::Core::MainMemoryReplayBudgetPass::PredictionStep,
                                                        budgetStart, budgetMilliseconds );
        }
    }

    return predictionCompletedThisPass;
}


void ReplayPrediction::PublishCompletedFrame( Physics::PhysicsSceneObjectId targetId )
{
    PROFILE_SCOPED( "Frame/Replay/Prediction/PublishCompletedFrame" );
    (void)targetId;

    // Why: the completed build bank remains the coherent visible publication.
    // PrepareOverlay duplicates it into the committed bank under its existing
    // frame budget, then flips branches when every node is ready.
}


void ReplayPrediction::PreparePresentation( ReplayPredictionSceneView scene, const ColliderStore& colliderStore,
                                            Physics::PhysicsSceneObjectId targetId, ModelRowHint targetModelRow,
                                            bool targetAvailable, double budgetMilliseconds,
                                            ReplayPredictionUpdateResult& result )
{
    PROFILE_SCOPED( "Frame/Replay/Prediction/PrepareOverlay" );
    PrepareReplayPredictionOverlay( m_state, scene, colliderStore,
                                    ReplayPredictionOverlayRequest { targetId, targetModelRow, targetAvailable,
                                                                     budgetMilliseconds },
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
}

void ReplayPrediction::ResetVerificationMarkers() noexcept
{
    m_state.futureNodeCache.ResetRetainedMarkers();
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

ReplayPrediction::~ReplayPrediction()
{
    // Hazard: the worker may be sealing a frame into m_solverEvidence. Join it
    // before member destruction reverses declaration order and retires the bank.
    WaitForJobIdle();
    CancelSolverEvidenceBuild();
}

ReplayPredictionSolverEvidenceCaptureStats ReplayPrediction::SolverEvidenceCaptureStats() const noexcept
{
    return m_solverEvidenceCaptureStats;
}

ReplayPredictionSolverEvidenceFrameView
ReplayPrediction::SolverEvidenceForPresentedFrame( ReplayFrameIndex frame ) const noexcept
{
    if ( m_detailMode != ReplayPredictionDetailMode::High )
    {
        return {};
    }

    const ReplayPredictionSolverEvidenceStore& store = m_state.BuildPrefixShouldBePresented() ? m_solverEvidence.Build()
                                                                                              : m_solverEvidence.Committed();

    // Why: frame replacement can reuse the numeric frame index in a new bank
    // epoch. The cause row copies the returned full identity and later detail
    // lookup requires that exact frame record to remain published.
    for ( std::size_t index = store.PublishedFrameCount(); index > 0u; --index )
    {
        const ReplayPredictionSolverEvidenceFrame* evidence = store.PublishedFrame( index - 1u );

        if ( evidence && evidence->complete && evidence->identity.frame == frame &&
             evidence->identity.mode == ReplayPredictionDetailMode::High )
        {
            return { &store, evidence };
        }
    }

    return {};
}

bool ReplayPrediction::CopyCauseEvidence( const ReplayPredictionCauseEvidenceQuery& query,
                                          ReplayPredictionCauseEvidencePacket& outPacket ) const noexcept
{
    // Hazard: frame numbers can repeat after a bank flip. The complete identity
    // must resolve in the currently presented bank before any row is copied.
    outPacket = {};

    if ( !query.sourceHighDetail || query.identity.mode != ReplayPredictionDetailMode::High ||
         m_detailMode != ReplayPredictionDetailMode::High )
    {
        return false;
    }

    const ReplayPredictionSolverEvidenceStore& store = m_state.BuildPrefixShouldBePresented() ? m_solverEvidence.Build()
                                                                                              : m_solverEvidence.Committed();
    const ReplayPredictionSolverEvidenceFrame* frame = store.FindPublishedFrame( query.identity );

    if ( !frame || !frame->complete || frame->identity != query.identity || query.contactIndex < 0 ||
         query.pipelineIndex < 0 || static_cast<std::size_t>( query.contactIndex ) >= frame->contacts.count ||
         static_cast<std::size_t>( query.pipelineIndex ) >= frame->pipeline.count )
    {
        return false;
    }

    const auto pairMatches = []( int bodyA, int bodyB, int expectedA, int expectedB, bool terrain )
    {
        if ( terrain )
        {
            return bodyA == expectedA && ( bodyB < 0 || bodyB == expectedB );
        }

        return ( bodyA == expectedA && bodyB == expectedB ) || ( bodyA == expectedB && bodyB == expectedA );
    };
    const auto contactMatches =
        [&]( const Physics::PhysicsSolverPersistentContactSample& contact, int bodyA, int bodyB, bool terrain )
    { return pairMatches( contact.bodyA, contact.bodyB, bodyA, bodyB, terrain ); };
    const auto solverStage = []( Physics::PhysicsPipelineStage stage )
    {
        switch ( stage )
        {
        case Physics::PhysicsPipelineStage::ManifoldRow:
        case Physics::PhysicsPipelineStage::WarmStart:
        case Physics::PhysicsPipelineStage::SolverIteration:
        case Physics::PhysicsPipelineStage::VelocityWriteback:
        case Physics::PhysicsPipelineStage::PositionCorrection:
        case Physics::PhysicsPipelineStage::CacheStore:
            return true;
        default:
            return false;
        }
    };

    const Physics::PhysicsSolverPersistentContactSample* anchor = store.Contact( frame->contacts, static_cast<std::size_t>(
                                                                                                      query.contactIndex ) );
    const Physics::PhysicsPipelineRecord* sequenceAnchor = store.Pipeline( frame->pipeline,
                                                                           static_cast<std::size_t>( query.pipelineIndex ) );

    if ( !anchor || !sequenceAnchor || query.featureId < 0 ||
         static_cast<uint32_t>( query.featureId ) != anchor->featureId ||
         ( anchor->isTerrain || anchor->bodyB < 0 ) != query.terrain ||
         ( query.focusedBody != anchor->bodyA && query.focusedBody != anchor->bodyB ) )
    {
        return false;
    }

    const int otherBody = query.focusedBody == anchor->bodyA ? anchor->bodyB : anchor->bodyA;

    if ( ( query.terrain ? query.counterpartBody >= 0 : query.counterpartBody != otherBody ) ||
         !solverStage( sequenceAnchor->stage ) || sequenceAnchor->featureId != anchor->featureId ||
         !pairMatches( sequenceAnchor->bodyA, sequenceAnchor->bodyB, anchor->bodyA, anchor->bodyB, query.terrain ) )
    {
        return false;
    }

    outPacket.identity = query.identity;
    outPacket.query = query;
    outPacket.bodyA = anchor->bodyA;
    outPacket.bodyB = anchor->bodyB;
    outPacket.terrain = query.terrain;

    for ( std::size_t index = 0; index < frame->contacts.count; ++index )
    {
        const Physics::PhysicsSolverPersistentContactSample* contact = store.Contact( frame->contacts, index );

        if ( !contact || !contactMatches( *contact, outPacket.bodyA, outPacket.bodyB, outPacket.terrain ) )
        {
            continue;
        }

        // Invariant: Planning's published detail owns the same eight-row cap.
        // Refuse the packet instead of presenting a partial contact patch.
        if ( outPacket.contactCount >= outPacket.contacts.size() )
        {
            outPacket = {};
            return false;
        }

        if ( index == static_cast<std::size_t>( query.contactIndex ) || contact->featureId == anchor->featureId )
        {
            outPacket.selectedContactRow = static_cast<int>( outPacket.contactCount );
        }

        outPacket.contacts[outPacket.contactCount++] = *contact;
    }

    if ( outPacket.contactCount == 0u )
    {
        outPacket = {};
        return false;
    }

    if ( outPacket.selectedContactRow < 0 )
    {
        outPacket.selectedContactRow = 0;
    }

    for ( std::size_t index = 0; index < frame->pipeline.count; ++index )
    {
        const Physics::PhysicsPipelineRecord* record = store.Pipeline( frame->pipeline, index );

        if ( !record || !solverStage( record->stage ) )
        {
            continue;
        }

        const bool bodyMatch = record->stage == Physics::PhysicsPipelineStage::VelocityWriteback
                                   ? ( record->bodyA == outPacket.bodyA ||
                                       ( !outPacket.terrain && record->bodyA == outPacket.bodyB ) )
                                   : pairMatches( record->bodyA, record->bodyB, outPacket.bodyA, outPacket.bodyB,
                                                  outPacket.terrain );

        if ( !bodyMatch )
        {
            continue;
        }

        const bool featureMatch = std::any_of( outPacket.contacts.begin(),
                                               outPacket.contacts.begin() + outPacket.contactCount,
                                               [&]( const auto& contact )
                                               { return contact.featureId == record->featureId; } );

        if ( !featureMatch || outPacket.pipelineCount >= outPacket.pipeline.size() )
        {
            continue;
        }

        outPacket.pipeline[outPacket.pipelineCount++] = *record;
    }

    outPacket.available = true;
    return true;
}

bool ReplayPrediction::BeginSolverEvidenceBuild( uint32_t generation )
{
    if ( !m_state.simulation.predictionEngine )
    {
        return false;
    }

    PhysicsEngine& predictionEngine = *m_state.simulation.predictionEngine;

    // Invariant: every new bank generation first closes any prior diagnostics
    // consumer. This keeps acquire/release accounting correct even if a future
    // restart path reaches Begin without an explicit CancelJob edge.
    CancelSolverEvidenceBuild();
    m_solverEvidenceCaptureStats.capacityTruncated = false;
    m_solverEvidenceCaptureStats.firstTruncatedFrame = 0;

    if ( m_detailMode == ReplayPredictionDetailMode::Low )
    {
        // Why: Physics continues counting pipeline events for solver identity,
        // but Low prediction neither retains their payload nor enters the
        // segmented evidence reserve/copy path.
        predictionEngine.SetPipelineTraceFullRecordConsumerActive( false );
        m_solverEvidenceCaptureStats.consumerActive = false;
        return true;
    }

    predictionEngine.SetPipelineTraceFullRecordConsumerActive( true );
    m_solverEvidence.BeginBuild( generation, ReplayPredictionDetailMode::High );
    ++m_solverEvidenceCaptureStats.buildBeginCount;
    ++m_solverEvidenceCaptureStats.consumerAcquireCount;
    m_solverEvidenceCaptureStats.consumerActive = true;
    return true;
}

bool ReplayPrediction::RefreshSolverEvidenceSource( PhysicsEngine& predictionEngine, int modelCount )
{
    if ( m_detailMode == ReplayPredictionDetailMode::Low )
    {
        return true;
    }

    if ( m_solverEvidenceCaptureStats.capacityTruncated )
    {
        // Why: exact solver evidence is an optional High-detail prefix. Once
        // its bounded bank is full, the authoritative trajectory continues
        // without repeatedly capturing rows that cannot be published.
        return true;
    }

    if ( !m_solverEvidenceCaptureStats.consumerActive || m_state.simulation.predictionEngine.get() != &predictionEngine )
    {
        return false;
    }

    // Concept: the Physics snapshot is frame-local staging here. Capture runs
    // only after Step completes, and SealSolverEvidenceFrame detaches the two
    // exact spans before the outer prediction frame becomes visible.
    predictionEngine.CaptureReplaySolverSnapshot( m_state.simulation.predictionWorld.physics,
                                                  MakePhysicsBodyCountFromNonNegativeInt( modelCount ) );
    return true;
}

bool ReplayPrediction::SealSolverEvidenceFrame( ReplayFrameIndex frame )
{
    if ( m_detailMode == ReplayPredictionDetailMode::Low )
    {
        return true;
    }

    if ( m_solverEvidenceCaptureStats.capacityTruncated )
    {
        return true;
    }

    if ( !m_solverEvidenceCaptureStats.consumerActive )
    {
        return false;
    }

    const Physics::PhysicsSolverSnapshot& snapshot = m_state.simulation.predictionWorld.physics;
    const ReplayPredictionEvidenceAppendResult
        appendResult = m_solverEvidence.AppendBuildFrameResult( frame, m_state.trajectoryBuild.topologyVersion,
                                                                m_state.trajectoryStore.publicationVersion,
                                                                snapshot.persistentContacts, snapshot.pipelineTrace,
                                                                static_cast<int>( frame ) );

    if ( appendResult == ReplayPredictionEvidenceAppendResult::Appended )
    {
        ++m_solverEvidenceCaptureStats.sealedFrameCount;
        m_solverEvidenceCaptureStats.copiedContactCount += snapshot.persistentContacts.size();
        m_solverEvidenceCaptureStats.copiedPipelineCount += snapshot.pipelineTrace.size();
        return true;
    }

    if ( appendResult != ReplayPredictionEvidenceAppendResult::CapacityDenied )
    {
        return false;
    }

    // Hazard: treating optional evidence exhaustion as a failed prediction
    // marks the unchanged source dirty and restarts the same doomed generation
    // forever. Close the private Physics consumer once, retain the exact sealed
    // prefix, and let the authoritative trajectory finish without more rows.
    if ( m_state.simulation.predictionEngine )
    {
        m_state.simulation.predictionEngine->SetPipelineTraceFullRecordConsumerActive( false );
    }

    ++m_solverEvidenceCaptureStats.consumerReleaseCount;
    ++m_solverEvidenceCaptureStats.capacityTruncationCount;
    m_solverEvidenceCaptureStats.firstTruncatedFrame = frame;
    m_solverEvidenceCaptureStats.consumerActive = false;
    m_solverEvidenceCaptureStats.capacityTruncated = true;
    return true;
}

bool ReplayPrediction::PromoteSolverEvidenceBuild() noexcept
{
    if ( m_detailMode == ReplayPredictionDetailMode::Low )
    {
        return true;
    }

    if ( m_solverEvidenceCaptureStats.consumerActive )
    {
        if ( m_state.simulation.predictionEngine )
        {
            m_state.simulation.predictionEngine->SetPipelineTraceFullRecordConsumerActive( false );
        }

        ++m_solverEvidenceCaptureStats.consumerReleaseCount;
        m_solverEvidenceCaptureStats.consumerActive = false;
    }

    return m_solverEvidence.PromoteBuild();
}

void ReplayPrediction::CancelSolverEvidenceBuild() noexcept
{
    if ( m_solverEvidenceCaptureStats.consumerActive )
    {
        if ( m_state.simulation.predictionEngine )
        {
            m_state.simulation.predictionEngine->SetPipelineTraceFullRecordConsumerActive( false );
        }

        ++m_solverEvidenceCaptureStats.consumerReleaseCount;
        m_solverEvidenceCaptureStats.consumerActive = false;
    }

    m_solverEvidence.CancelBuild();
}

ReplayPredictionDetailTransitionAction ReplayPrediction::ApplyDetailModeCommand( ReplayPredictionDetailModeCommand command )
{
    const ReplayPredictionDetailTransitionAction actions = EvaluateReplayPredictionDetailTransition( m_detailMode,
                                                                                                     command.mode );

    if ( actions == ReplayPredictionDetailTransitionAction::None )
    {
        return actions;
    }

    // Invariant: no publication may span two detail modes. Joining and
    // retiring both banks precedes the retained preference change; the next
    // frame then seeds one fresh exact-source generation in the new mode.
    ClearCache();

    if ( ReplayPredictionDetailTransitionHas( actions, ReplayPredictionDetailTransitionAction::ReleaseHighDetailCapacity ) )
    {
        // Invariant: Low is the explicit release boundary. Both evidence banks
        // are unreachable after ClearCache joins the worker, so their backing
        // segments can be destroyed before any lightweight rebuild starts.
        m_solverEvidence.ReleaseCapacity();
    }

    m_detailMode = command.mode;
    MarkDirty();
    return actions;
}

void ReplayPrediction::ApplyAuthoringRequest( const ReplayPredictionAuthoringCommand& request, float minHorizonSeconds,
                                              float maxHorizonSeconds )
{
    if ( request.prepareVelocityMutationBaseline )
    {
        (void)PrepareVelocityMutationBaseline();
    }

    if ( request.clearPredictionCache )
    {
        ClearCache();
    }

    if ( request.updateVelocityPreview )
    {
        m_state.velocityDragPreview.Update( request.velocityPreviewTargetId, request.velocityPreviewDelta );
    }

    if ( request.finishVelocityPreview )
    {
        (void)m_state.velocityDragPreview.Finish( m_state.build.generationBeginCount + 1u );
    }

    if ( request.enablePrediction )
    {
        m_state.enabled = true;
        m_state.simulation.horizonSeconds = std::clamp( m_state.simulation.horizonSeconds, minHorizonSeconds,
                                                        maxHorizonSeconds );
    }

    if ( request.refreshPrediction )
    {
        MarkDirty();
    }
}

void ReplayPrediction::DisableAndClearCache()
{
    m_state.enabled = false;
    ClearCache();
}

bool ReplayPrediction::LoadArchive( std::span<const uint8_t> bytes, RunReplayPathVisualizerState& pathVisualizer,
                                    char* outReason, std::size_t reasonSize )
{
    ReplayPredictionArchiveDetailCapability capturedCapability = ReplayPredictionArchiveDetailCapability::Low;

    if ( !LoadReplayPredictionArchive( bytes, pathVisualizer, m_state, m_solverEvidence, m_detailMode, capturedCapability,
                                       outReason, reasonSize ) )
    {
        return false;
    }

    m_loadedArchiveCapability = capturedCapability;
    m_hasLoadedArchiveCapability = true;
    return true;
}

bool ReplayPrediction::BuildArchive( const RunReplayPathVisualizerState& pathVisualizer,
                                     std::vector<uint8_t>& outBytes ) const
{
    return BuildReplayPredictionArchive( pathVisualizer, m_state, m_detailMode, m_solverEvidence.Committed(), outBytes );
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

    const std::size_t frameCount = usingBuildFrames ? m_state.PublishedBuildFrameCount() : m_state.CommittedFrameCount();

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
    const double elapsedSeconds = (std::max)( 0.0,
                                              std::chrono::duration<double>( now - m_state.revealClock.anchor ).count() );

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
        const double elapsedSeconds = (std::max)( 0.0, std::chrono::duration<double>( now - m_state.revealClock.anchor )
                                                           .count() );

        const double revealedSeconds = elapsedSeconds * previousRevealRate;
        m_state.revealClock.anchor = now - std::chrono::duration_cast<std::chrono::steady_clock::duration>(
                                               std::chrono::duration<double>( revealedSeconds / normalizedRevealRate ) );
    }

    m_state.revealClock.secondsPerSecond = normalizedRevealRate;
}

bool ReplayPrediction::PrepareVelocityMutationBaseline() noexcept
{
    if ( ( !m_state.build.complete || !m_state.HasCommittedFramePrefix() ) && !m_state.baseline.comparisonActive )
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
    return !m_state.build.building && m_state.HasCommittedFramePrefix() && m_state.build.complete;
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
    m_state.futureNodeCache.ResetRetainedMarkers();
    m_state.trajectoryBuild.childFrameCount = 0;
    m_state.trajectoryBuild.builtNodeCount = 0;
    m_state.trajectoryBuild.childAppendTargetFrameCount = 0;
    m_state.trajectoryBuild.childAppendNodeIndex = 0;
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
    m_state.velocityDragPreview.Clear();
    m_loadedArchiveCapability = ReplayPredictionArchiveDetailCapability::Low;
    m_hasLoadedArchiveCapability = false;
}

void ReplayPrediction::ClearCacheFromReplayInput()
{
    PROFILE_SCOPED( "Frame/Replay/Prediction/ClearCache" );

    {
        PROFILE_SCOPED( "Frame/Replay/Prediction/ClearCache/CancelJob" );
        CancelJob( false );
    }

    {
        PROFILE_SCOPED( "Frame/Replay/Prediction/ClearCache/ResetSampleMetadata" );
        m_state.build.supersededRestartCount = 0;
        m_state.build.latestRestartBeginCount = 0;
        m_state.simulation.measuredTicksPerMs.store( 0.0, std::memory_order_release );
        m_state.simulation.probeElapsedMs = 0.0;
        m_state.simulation.probeTicksCompleted = 0;
        m_state.simulation.calibratedModelCount = -1;
    }

    {
        PROFILE_SCOPED( "Frame/Replay/Prediction/ClearCache/InvalidateFrames" );

        // Why: the two prediction banks retain millions of nested payload
        // slots. Publication count is the authority boundary, so Predict-off
        // can hide the committed bank without paying its destructor walk.
        m_state.InvalidateCommittedFrames();
        m_state.committedPublication.Reset();
    }

    {
        PROFILE_SCOPED( "Frame/Replay/Prediction/ClearCache/InvalidateWorkerTrajectoryStore" );
        m_state.trajectoryStore.Clear();
    }

    {
        PROFILE_SCOPED( "Frame/Replay/Prediction/ClearCache/InvalidateFutureNodeCache" );
        ClearFutureNodeCache();
    }

    {
        PROFILE_SCOPED( "Frame/Replay/Prediction/ClearCache/ResetSource" );
        m_state.simulation.targetId = Physics::PhysicsSceneObjectId {};
        m_state.simulation.sourceFrameIndex = 0;
        m_state.simulation.sourceSolverHash = 0;
        m_state.simulation.sourceSimulationSeconds = 0.0;
        m_state.build.lastBuildTime = 0.0;
    }

    {
        PROFILE_SCOPED( "Frame/Replay/Prediction/ClearCache/ResetTrajectoryBuild" );
        m_state.trajectoryBuild = RunReplayPredictionTrajectoryBuildState {};
    }

    {
        PROFILE_SCOPED( "Frame/Replay/Prediction/ClearCache/InvalidateTrajectoryStore" );
        m_state.trajectoryStore.Clear();
    }

    {
        PROFILE_SCOPED( "Frame/Replay/Prediction/ClearCache/ResetBaseline" );
        m_state.baseline = ReplayPredictionBaselineSnapshot {};
    }

    {
        PROFILE_SCOPED( "Frame/Replay/Prediction/ClearCache/ResetVelocityPreview" );
        m_state.velocityDragPreview.Clear();
    }

    m_loadedArchiveCapability = ReplayPredictionArchiveDetailCapability::Low;
    m_hasLoadedArchiveCapability = false;
}

ReplayPastTrajectoryRefreshPlan ReplayPrediction::BeginPastTrajectoryRefresh( ReplayPredictionRecorderWindow recorder,
                                                                              const ReplayPastTrajectoryView& path )
{
    ReplayPastTrajectoryRefreshPlan plan;

    if ( !path.hasTarget || path.targetId.value == 0 || !recorder.enabled || recorder.sampleCount == 0 ||
         recorder.nextFrameIndex == 0 )
    {
        plan.update.apply = true;
        return plan;
    }

    const ReplayFrameIndex oldestFrame = ReplayOldestFrameFromStats( recorder );
    const ReplayFrameIndex newestFrame = recorder.nextFrameIndex - 1u;
    const bool needsRebuild = !path.valid || path.retainedTargetId.value != path.targetId.value ||
                              path.totalFramesEvicted != recorder.totalFramesEvicted || path.firstFrame != oldestFrame ||
                              path.builtThroughFrame < newestFrame;

    if ( !needsRebuild )
    {
        return plan;
    }

    ReplayTrajectoryRecord* record = BeginReplayPastRootTrajectoryRecord( m_state.trajectoryStore, path.targetId,
                                                                          recorder.sampleCount,
                                                                          ReplayTrajectoryFrameNumberForReserve(
                                                                              newestFrame ) );

    if ( !record )
    {
        plan.update.apply = true;
        return plan;
    }

    plan.targetId = path.targetId;
    plan.oldestFrame = oldestFrame;
    plan.newestFrame = newestFrame;
    plan.totalFramesEvicted = recorder.totalFramesEvicted;
    plan.fullRebuildCount = path.fullRebuildCount + 1u;
    plan.incrementalTrimCount = path.incrementalTrimCount;
    plan.appendSamples = true;
    return plan;
}

bool ReplayPrediction::AppendPastTrajectoryRefreshPoint( Physics::PhysicsSceneObjectId targetId, ReplayFrameIndex frame,
                                                         Physics::ModelRowHint modelRow, const Vector3& position )
{
    (void)modelRow;
    ReplayTrajectoryRecord* record = m_state.trajectoryStore.FindRecord( ReplayPastRootTrajectoryKey( targetId ) );
    return record && AppendReplayTrajectoryPoint( m_state.trajectoryStore, *record, frame, position );
}

ReplayPastTrajectoryUpdate ReplayPrediction::CompletePastTrajectoryRefresh( const ReplayPastTrajectoryRefreshPlan& plan,
                                                                            bool traversalOk, bool hasSample,
                                                                            ReplayFrameIndex firstFrame,
                                                                            Physics::ModelRowHint targetModelRow )
{
    ReplayPastTrajectoryUpdate update = plan.update;
    update.apply = true;

    ReplayTrajectoryRecord* record = m_state.trajectoryStore.FindRecord( ReplayPastRootTrajectoryKey( plan.targetId ) );

    if ( !plan.appendSamples || !record || !traversalOk || !hasSample )
    {
        return update;
    }

    record->firstFrame = firstFrame;
    update.targetId = plan.targetId;
    update.firstFrame = plan.oldestFrame;
    update.builtThroughFrame = plan.newestFrame;
    update.totalFramesEvicted = plan.totalFramesEvicted;
    update.fullRebuildCount = plan.fullRebuildCount;
    update.incrementalTrimCount = plan.incrementalTrimCount;
    update.targetModelRow = targetModelRow;
    update.targetModelRowRepaired = true;
    update.valid = true;
    return update;
}

void ReplayPrediction::AppendPastTrajectorySample( ReplayPredictionRecorderWindow solverStats,
                                                   const ReplayPastTrajectoryView& path,
                                                   const ReplaySolverFrameSample& sample,
                                                   ReplayPastTrajectoryUpdate& update )
{
    if ( !path.hasTarget || path.targetId.value == 0 || !path.valid || path.retainedTargetId.value != path.targetId.value )
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
    stats.evidence = m_solverEvidence.CollectMemoryStats();
    SkullbonezCore::Core::
        MainMemoryAddReplayCategoryBytes( stats.categoryBytes,
                                          SkullbonezCore::Core::MainMemoryReplayByteCategory::PredictionOwner,
                                          static_cast<uint64_t>( sizeof( m_state ) ) );

    if ( m_state.simulation.predictionEngine )
    {
        SkullbonezCore::Core::
            MainMemoryAddReplayCategoryBytes( stats.categoryBytes,
                                              SkullbonezCore::Core::MainMemoryReplayByteCategory::PredictionEngine,
                                              ReplayPredictionEngineMemoryBytes( *m_state.simulation.predictionEngine ) );
    }

    SkullbonezCore::Core::
        MainMemoryAddReplayCategoryBytes( stats.categoryBytes,
                                          SkullbonezCore::Core::MainMemoryReplayByteCategory::PredictionWorldState,
                                          ReplayPredictionWorldSnapshotMemoryBytes( m_state.simulation.predictionWorld ) );

    SkullbonezCore::Core::
        MainMemoryAddReplayCategoryBytes( stats.categoryBytes,
                                          SkullbonezCore::Core::MainMemoryReplayByteCategory::PredictionBodyState,
                                          ReplayPredictionVectorCapacityBytes( m_state.simulation.predictionBodies ) );

    SkullbonezCore::Core::
        MainMemoryAddReplayCategoryBytes( stats.categoryBytes,
                                          SkullbonezCore::Core::MainMemoryReplayByteCategory::PredictionFrameRecords,
                                          ReplayPredictionVectorCapacityBytes( m_state.simulation.frames ) +
                                              ReplayPredictionVectorCapacityBytes( m_state.build.buildFrames ) );

    SkullbonezCore::Core::
        MainMemoryAddReplayCategoryBytes( stats.categoryBytes,
                                          SkullbonezCore::Core::MainMemoryReplayByteCategory::PredictionFutureTree,
                                          ReplayPredictionVectorCapacityBytes( m_state.futureNodeCache.futureNodes ) +
                                              ReplayPredictionVectorCapacityBytes(
                                                  m_state.futureNodeCache.futureNodeBuildScratch ) +
                                              ReplayPredictionVectorCapacityBytes(
                                                  m_state.committedPublication.visibleFutureNodes ) +
                                              ReplayPredictionVectorCapacityBytes(
                                                  m_state.build.causalContactActiveModels ) );

    SkullbonezCore::Core::MainMemoryAddReplayCategoryBytes( stats.categoryBytes,
                                                            SkullbonezCore::Core::MainMemoryReplayByteCategory::
                                                                PredictionSolverContactEvidence,
                                                            stats.evidence.currentContactCapacityBytes );
    SkullbonezCore::Core::
        MainMemoryAddReplayCategoryBytes( stats.categoryBytes,
                                          SkullbonezCore::Core::MainMemoryReplayByteCategory::PredictionPipelineEvidence,
                                          stats.evidence.currentPipelineCapacityBytes +
                                              stats.evidence.currentFrameCapacityBytes );

    for ( const RunReplayPredictionFrame& frame : m_state.simulation.frames )
    {
        AddReplayPredictionFrameCategoryBytes( stats.categoryBytes, frame );
    }

    for ( const RunReplayPredictionFrame& frame : m_state.build.buildFrames )
    {
        AddReplayPredictionFrameCategoryBytes( stats.categoryBytes, frame );
    }

    SkullbonezCore::Core::
        MainMemoryAddReplayCategoryBytes( stats.categoryBytes,
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

    for ( const ReplayTrajectoryRecord& record : m_state.trajectoryStore.ActiveRecords() )
    {
        stats.trajectory.publishedPointCount += static_cast<uint64_t>(
            (std::min)( record.publishedPointCount, record.points.size() ) );
        stats.trajectory.maxRecordVersion = (std::max)( stats.trajectory.maxRecordVersion, record.version );
    }

    return stats;
}
