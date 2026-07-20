/*
File: SkullbonezSource/Runtime/Replay/ReplayPrediction.cpp
Purpose:
  Owns replay prediction scheduling, private-engine stepping, and publication.

Summary:
  Replay tools read two timelines. Retained solver samples describe what already
  happened; prediction samples advance a private replay-owned physics engine.
  Frame update schedules and publishes prediction before the separate drawing
  unit consumes the published state as lightweight overlay geometry.

Glossary:
  Path visualizer: Overlay that draws past/future body trajectories and contact
    handoffs.
  Replay target marker: Overlay outline/ring drawn around the replay-selected
    body from live body/collider store rows.
  Prediction slice: Bounded worker chunk that advances the private prediction
    engine and publishes a coherent frame prefix.
  Prediction physics tick: Replay-owned fixed step against the private
    prediction engine.
  Future node: Body discovered by following contacts or predicted movement
    outward from a selected root body.
  ReplayBodyId: Stable runtime id used across retained samples even when vector
    indices are only local hints.
  Model row hint: Cached live body row paired with ReplayBodyId; replay tools
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

Related:
  - SkullbonezSource/Runtime/Replay/ReplayScrubberTools.cpp
  - SkullbonezSource/Runtime/Replay/ReplayAuthoringCauseTree.cpp
  - SkullbonezSource/Runtime/Replay/ReplayPredictionDrawing.cpp
  - Agentic/Reference/runtime-reference.md
  - Agentic/Reference/comment-style-guide.md
*/
#include "ReplayPrediction.h"
#include "../Scene/SceneEntityStore.h"
#include "../Editor/EditorHullAssets.h"
#include "ReplayOverlayLayout.h"
#include "ReplayPredictionArchive.h"
#include "ReplayPredictionReserve.h"
#include "ReplayScrubber.h"
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
#include <limits>
#include <memory>
#include <thread>

using namespace SkullbonezCore::Runtime;
using namespace SkullbonezCore::Runtime::ReplayPredictionArchiveOperations;
using namespace SkullbonezCore::Runtime::ReplayPredictionReserveOperations;
using namespace SkullbonezCore::Runtime::ReplayPredictionSchedulingOperations;
using namespace SkullbonezCore::Runtime::ReplayScrubberOperations;
namespace Gameplay = SkullbonezCore::Gameplay;
using namespace SkullbonezCore::Math::CollisionDetection;
using namespace SkullbonezCore::Math::Orientation;
using namespace SkullbonezCore::Math::Transformation;
using namespace SkullbonezCore::Physics;
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
                                     ReplayBodyId id,
                                     int modelIndexHint,
                                     int modelCount,
                                     int& outModelIndex )
{
    if ( id.value == 0 )
    {
        return false;
    }

    const PhysicsBodyHandle body = bodyStore.HandleForReplayBodyId( id.value, modelIndexHint );
    const int modelIndex = bodyStore.ModelIndexForHandle( body );
    if ( modelIndex < 0 || modelIndex >= modelCount )
    {
        return false;
    }

    outModelIndex = modelIndex;
    return true;
}


bool TryResolveReplayBodyModelIndex( const PhysicsBodyStore& bodyStore,
                                     ReplayBodyId id,
                                     ModelRowHint& hint,
                                     int modelCount,
                                     int& outModelIndex )
{
    // Why: retained replay UI state still carries modelIndex integers until the
    // fable-06 conversion rows are complete. Naming the cache as ModelRowHint
    // keeps stable replay identity in ReplayBodyId while this resolver heals or
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
    const SkullbonezCore::Physics::ExternalForceFrameInput externalForces =
        tornadoGameplay.BuildForceFrame( fixedDt,
                                         SkullbonezCore::Physics::PhysicsEngine::ReadBodies( engine ).Count() );
    engine.Step( fixedDt, worldForces, externalForces, workerPool, nullptr, 0, PhysicsDiagnosticsCsvWriter{} );
    return true;
}


// Why: the 200-brick prediction scene needs more than the old 100-node cap to
// show the full contact spread instead of clipping the visual explanation.
constexpr std::size_t REPLAY_PATH_MAX_FUTURE_NODES = REPLAY_VISUAL_FUTURE_NODE_CAPACITY;
constexpr std::size_t REPLAY_PATH_MAX_SEGMENTS = 260;
constexpr std::size_t REPLAY_RIBBON_SEGMENTS_PER_PATH_SEGMENT = 1;
constexpr float REPLAY_PATH_MIN_SEGMENT_DISTANCE_SQ = 0.0001f;
// Why: rest markers and auxiliary trails still need an instantaneous "moving"
// test, but child activation below uses contact ticks plus accumulated
// displacement so one-frame velocity spikes cannot reorder the cause tree.
constexpr float REPLAY_PREDICTION_CHILD_LINEAR_SPEED_SQ = 8.0f * 8.0f;
constexpr float REPLAY_PREDICTION_CHILD_ACTIVATION_DISTANCE = 0.05f;
constexpr float REPLAY_PREDICTION_CHILD_ACTIVATION_DISTANCE_SQ =
    REPLAY_PREDICTION_CHILD_ACTIVATION_DISTANCE * REPLAY_PREDICTION_CHILD_ACTIVATION_DISTANCE;

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
constexpr ReplayFrameIndex REPLAY_PREDICTION_REST_GRACE_FRAMES =
    static_cast<ReplayFrameIndex>( REPLAY_PREDICTION_REST_GRACE_SECONDS / PHYSICS_FIXED_DT );
constexpr float REPLAY_PREDICTION_REST_POSITION_EPSILON_SQ = 0.5f * 0.5f;

constexpr uint32_t REPLAY_PREDICTION_CAPTURE_BODY_WORKER_HASH =
    HashStr( "Frame/Replay/Prediction/CaptureBodyState/WorkerBodies" );
constexpr uint32_t REPLAY_PREDICTION_CAPTURE_SAMPLE_WORKER_HASH =
    HashStr( "Frame/Replay/Prediction/CaptureSample/WorkerBodies" );

// Concept: prediction helper section.
//
// These helpers remain in this translation unit because retained path drawing,
// cause-focus markers, and prediction drawing still share file-local constants
// and helper templates.
double ReplayPredictionElapsedMilliseconds( const std::chrono::steady_clock::time_point& start )
{
    return std::chrono::duration<double, std::milli>( std::chrono::steady_clock::now() - start ).count();
}

bool ReplayPredictionBudgetExpired( const std::chrono::steady_clock::time_point& start, double budgetMilliseconds )
{
    return budgetMilliseconds > 0.0 && ReplayPredictionElapsedMilliseconds( start ) >= budgetMilliseconds;
}

// Why: Stage-0 replay diagnostics need to know which visualizer pass lost work.
// Keep the accounting beside the existing budget checks so later stages can
// delete the budgets without hunting for a separate telemetry path.
bool ReplayPredictionBudgetExpiredForPass( ReplayPredictionUpdateResult& result,
                                           SkullbonezCore::Core::MainMemoryReplayBudgetPass pass,
                                           const std::chrono::steady_clock::time_point& start,
                                           double budgetMilliseconds )
{
    if ( !ReplayPredictionBudgetExpired( start, budgetMilliseconds ) )
    {
        return false;
    }
    const std::size_t passIndex = static_cast<std::size_t>( pass );
    if ( passIndex < result.budgetExpiries.size() )
    {
        ++result.budgetExpiries[passIndex];
    }
    return true;
}

double ReplayPredictionRemainingMilliseconds( const std::chrono::steady_clock::time_point& start,
                                              double budgetMilliseconds )
{
    if ( budgetMilliseconds <= 0.0 )
    {
        return 0.0;
    }
    return (std::max)( 0.0, budgetMilliseconds - ReplayPredictionElapsedMilliseconds( start ) );
}

double ReplayPredictionRevealSecondsPerSecond( const RunReplayPredictionState& prediction )
{
    // Why: authored shot-list data is allowed to be imperfect. Non-positive
    // rates fall back to real-time pacing instead of freezing the reveal cursor
    // or dividing by zero while the prediction build catches up.
    return prediction.revealClock.secondsPerSecond > 0.0 ? prediction.revealClock.secondsPerSecond : 1.0;
}

// Concept: reveal cursor Ã¢â‚¬â€ the wall-clock playhead of the causal-unfold animation.
//
// Every prediction draw pass clamps to the frame this returns, so the pace of
// the visible tree comes from real time, not from how fast the build job
// happened to finish. While the job is still building, the cursor also clamps
// to the populated prefix and re-anchors at that edge, so a slow build paces
// the unfold without banking "reveal debt" that would snap the animation
// forward the moment the job completes.
// Invariant: the cursor is MONOTONIC per prediction. It plays 0 -> horizon
// exactly once and then holds there, so every revealed line and causal box
// stays on screen. A refresh with no committed same-target future resets the
// anchor; same-target auto-refresh keeps the anchor and waits until the new
// prefix reaches the visible cursor.
ReplayFrameIndex ReplayPredictionRevealFrameIndex( RunReplayPredictionState& prediction,
                                                   ReplayFrameIndex lastAvailableFrame )
{
    if ( prediction.revealClock.deterministicFrameEnabled )
    {
        prediction.revealClock.presentedFrame =
            (std::min)( lastAvailableFrame, prediction.revealClock.deterministicFrame );
        return prediction.revealClock.presentedFrame;
    }
    if ( prediction.build.buildMode == ReplayPredictionBuildMode::Instant )
    {
        // Why: instant mode presents the completed future at once. The causal
        // unfold clock remains an amortized-mode presentation affordance.
        prediction.revealClock.presentedFrame = lastAvailableFrame;
        return prediction.revealClock.presentedFrame;
    }
    const auto now = std::chrono::steady_clock::now();
    if ( !prediction.revealClock.anchorValid )
    {
        prediction.revealClock.anchor = now;
        prediction.revealClock.anchorValid = true;
        prediction.revealClock.presentedFrame = 0;
        return prediction.revealClock.presentedFrame;
    }

    const double availableSeconds = static_cast<double>( lastAvailableFrame ) * PHYSICS_FIXED_DT;
    const double elapsedSeconds =
        (std::max)( 0.0, std::chrono::duration<double>( now - prediction.revealClock.anchor ).count() );
    const double revealSecondsPerSecond = ReplayPredictionRevealSecondsPerSecond( prediction );
    double revealSeconds = elapsedSeconds * revealSecondsPerSecond;
    if ( prediction.build.building && revealSeconds > availableSeconds )
    {
        revealSeconds = availableSeconds;
        prediction.revealClock.anchor =
            now - std::chrono::duration_cast<std::chrono::steady_clock::duration>(
                      std::chrono::duration<double>( availableSeconds / revealSecondsPerSecond ) );
    }

    const double revealFrame = revealSeconds / static_cast<double>( PHYSICS_FIXED_DT );
    prediction.revealClock.presentedFrame =
        (std::min)( lastAvailableFrame, static_cast<ReplayFrameIndex>( revealFrame ) );
    return prediction.revealClock.presentedFrame;
}

std::size_t ReplayPredictionBuildPresentationFrameCountForRefresh( RunReplayPredictionState& prediction,
                                                                   ReplayBodyId requestedTargetId )
{
    if ( requestedTargetId.value == 0 || prediction.simulation.targetId.value != requestedTargetId.value ||
         prediction.simulation.frames.size() < 2u )
    {
        return 2u;
    }

    // Why: auto-refresh should replace the old future only after the rebuilding
    // prefix catches the causal story the user can already see.
    const ReplayFrameIndex lastCommittedFrame = prediction.simulation.frames.back().frameIndex;
    const ReplayFrameIndex revealFrame = ReplayPredictionRevealFrameIndex( prediction, lastCommittedFrame );
    return (std::max)( std::size_t{ 2u }, static_cast<std::size_t>( revealFrame ) + 1u );
}

constexpr int REPLAY_PREDICTION_FRAME_CAPACITY =
    static_cast<int>( ReplayOverlay::REPLAY_PREDICTION_MAX_SECONDS / PHYSICS_FIXED_DT ) + 2;
constexpr int REPLAY_PREDICTION_PATH_BUDGET = 100;
constexpr int REPLAY_PREDICTION_TICKS_PER_WORKER_SUBMIT = 8;
constexpr std::size_t REPLAY_PREDICTION_DEBUG_CONTACT_INITIAL_MIN = 512u;
constexpr std::size_t REPLAY_PREDICTION_DEBUG_CONTACT_INITIAL_MAX = 2048u;
constexpr std::size_t REPLAY_PREDICTION_DEBUG_CONTACT_GROWTH_CHUNK = 4096u;

template <typename T> bool ReplayPredictionCapacityBytes( std::size_t capacity, uint64_t& outBytes )
{
    constexpr uint64_t elementBytes = static_cast<uint64_t>( sizeof( T ) );
    const uint64_t maxCapacity = ( std::numeric_limits<uint64_t>::max )() / elementBytes;
    if ( capacity > maxCapacity )
    {
        return false;
    }
    outBytes = static_cast<uint64_t>( capacity ) * elementBytes;
    return true;
}

template <typename T> uint64_t ReplayPredictionVectorCapacityBytes( const std::vector<T>& values )
{
    uint64_t bytes = 0;
    return ReplayPredictionCapacityBytes<T>( values.capacity(), bytes ) ? bytes : 0;
}

uint64_t ReplayPredictionWorldSnapshotMemoryBytes( const SkullbonezCore::Runtime::ReplaySolverWorldSnapshot& snapshot )
{
    const SkullbonezCore::Physics::PhysicsSolverSnapshot& physics = snapshot.physics;
    uint64_t bytes = 0;
    bytes += ReplayPredictionVectorCapacityBytes( physics.timeRemaining );
    bytes += ReplayPredictionVectorCapacityBytes( physics.sleepSupportedThisFrame );
    bytes += ReplayPredictionVectorCapacityBytes( physics.sleepInhibitedThisFrame );
    bytes += ReplayPredictionVectorCapacityBytes( physics.sleepState );
    bytes += ReplayPredictionVectorCapacityBytes( physics.sleepCounter );
    bytes += ReplayPredictionVectorCapacityBytes( physics.underwaterSleepLocked );
    bytes += ReplayPredictionVectorCapacityBytes( snapshot.tornadoCaptureSeconds );
    bytes += ReplayPredictionVectorCapacityBytes( snapshot.tornadoEjectCooldownSeconds );
    bytes += ReplayPredictionVectorCapacityBytes( physics.collisionVisualContacts );
    bytes += ReplayPredictionVectorCapacityBytes( physics.sleepIslandVisualId );
    bytes += ReplayPredictionVectorCapacityBytes( physics.sleepIslandAssignedVisualId );
    bytes += ReplayPredictionVectorCapacityBytes( physics.sleepSupportEdges );
    bytes += ReplayPredictionVectorCapacityBytes( physics.sleepIslandParent );
    bytes += ReplayPredictionVectorCapacityBytes( physics.sleepIslandRank );
    bytes += ReplayPredictionVectorCapacityBytes( physics.sleepIslandHasAwake );
    bytes += ReplayPredictionVectorCapacityBytes( physics.sleepIslandHasSupportAnchor );
    bytes += ReplayPredictionVectorCapacityBytes( physics.sleepIslandEligible );
    bytes += ReplayPredictionVectorCapacityBytes( physics.sleepIslandCanSleep );
    bytes += ReplayPredictionVectorCapacityBytes( physics.persistentContacts );
    bytes += ReplayPredictionVectorCapacityBytes( physics.persistentContactCache );
    bytes += ReplayPredictionVectorCapacityBytes( physics.persistentContactCounts );
    bytes += ReplayPredictionVectorCapacityBytes( physics.persistentRestingContactCounts );
    bytes += ReplayPredictionVectorCapacityBytes( physics.debugContacts );
    bytes += ReplayPredictionVectorCapacityBytes( physics.pipelineTrace );
    bytes += ReplayPredictionVectorCapacityBytes( physics.collisionCellKeys );
    return bytes;
}

void AddReplayPredictionFrameCategoryBytes( SkullbonezCore::Core::MainMemoryReplayCategoryBytes& categories,
                                            const RunReplayPredictionFrame& frame )
{
    SkullbonezCore::Core::MainMemoryAddReplayCategoryBytes(
        categories,
        SkullbonezCore::Core::MainMemoryReplayByteCategory::PredictionFrameBodies,
        ReplayPredictionVectorCapacityBytes( frame.bodies ) );
    SkullbonezCore::Core::MainMemoryAddReplayCategoryBytes(
        categories,
        SkullbonezCore::Core::MainMemoryReplayByteCategory::PredictionDebugContacts,
        ReplayPredictionVectorCapacityBytes( frame.debugContacts ) );
}

template <typename T>
bool ReplayPredictionFramePayloadBytes( std::size_t frameCount, std::size_t capacityPerFrame, uint64_t& outBytes )
{
    uint64_t bytesPerFrame = 0;
    if ( !ReplayPredictionCapacityBytes<T>( capacityPerFrame, bytesPerFrame ) )
    {
        return false;
    }
    const uint64_t maxValue = ( std::numeric_limits<uint64_t>::max )();
    const uint64_t maxFrameCount = bytesPerFrame > 0 ? maxValue / bytesPerFrame : maxValue;
    if ( frameCount > maxFrameCount )
    {
        return false;
    }
    outBytes = static_cast<uint64_t>( frameCount ) * bytesPerFrame;
    return true;
}

std::size_t RoundUpReplayPredictionCapacity( std::size_t requestedCapacity, std::size_t chunk )
{
    if ( chunk == 0 || requestedCapacity == 0 )
    {
        return requestedCapacity;
    }
    const std::size_t remainder = requestedCapacity % chunk;
    return remainder == 0 ? requestedCapacity : requestedCapacity + ( chunk - remainder );
}

std::size_t ReplayPredictionInitialDebugContactCapacity( int modelCount )
{
    const std::size_t modelScaled = static_cast<std::size_t>( (std::max)( modelCount, 1 ) ) * 8u;
    return std::clamp( modelScaled,
                       REPLAY_PREDICTION_DEBUG_CONTACT_INITIAL_MIN,
                       REPLAY_PREDICTION_DEBUG_CONTACT_INITIAL_MAX );
}

std::size_t ReplayPredictionNextDebugContactCapacity( std::size_t currentCapacity, std::size_t requiredCapacity )
{
    const std::size_t chunked =
        RoundUpReplayPredictionCapacity( requiredCapacity, REPLAY_PREDICTION_DEBUG_CONTACT_GROWTH_CHUNK );
    const std::size_t doubled =
        currentCapacity > 0 ? currentCapacity * 2u : REPLAY_PREDICTION_DEBUG_CONTACT_INITIAL_MIN;
    return (std::max)( chunked, doubled );
}

uint64_t ReplayPredictionEngineMemoryBytes( const PhysicsEngine& engine )
{
    // Why: seeding the private engine copies several physics-owned vectors.
    // Estimate the live working set before requesting the replay growth scope so
    // those copy allocations are approved under one bounded prediction owner.
    uint64_t bytes = static_cast<uint64_t>( sizeof( PhysicsEngine ) );
    bytes += engine.CollectPhysicsWorldMemoryBytes();
    bytes += engine.CollectDebugAndBroadphaseMemoryBytes();
    bytes += static_cast<uint64_t>( SkullbonezCore::Physics::PhysicsEngine::ReadBodies( engine ).RecordCapacity() ) *
             sizeof( PhysicsBodyRecord );
    bytes += static_cast<uint64_t>( SkullbonezCore::Physics::PhysicsEngine::ReadColliders( engine ).RecordCapacity() ) *
             sizeof( ColliderRecord );
    return bytes;
}

int ReplayPredictionEngineReserveBytes( const PhysicsEngine& engine )
{
    const uint64_t bytes = ReplayPredictionEngineMemoryBytes( engine );
    if ( bytes == 0 || bytes > static_cast<uint64_t>( REPLAY_PREDICTION_RESERVE_HARD_BYTES ) ||
         bytes > static_cast<uint64_t>( ( std::numeric_limits<int>::max )() ) )
    {
        return 0;
    }
    return static_cast<int>( bytes );
}

template <typename T>
bool ReserveReplayPredictionVector( std::vector<T>& values,
                                    std::size_t requestedCapacity,
                                    int frameNumber,
                                    const char* targetName )
{
    if ( requestedCapacity <= values.capacity() )
    {
        return true;
    }
    uint64_t oldBytes = 0;
    uint64_t requestedBytes = 0;
    if ( !ReplayPredictionCapacityBytes<T>( values.capacity(), oldBytes ) ||
         !ReplayPredictionCapacityBytes<T>( requestedCapacity, requestedBytes ) ||
         requestedBytes > static_cast<uint64_t>( REPLAY_PREDICTION_RESERVE_HARD_BYTES ) )
    {
        return false;
    }

    CoreAllocation::RuntimeReserveGrowthResult result = {};
    if ( !RequestReplayPredictionReserveGrowth( targetName,
                                                frameNumber,
                                                static_cast<int>( oldBytes ),
                                                static_cast<int>( requestedBytes ),
                                                1,
                                                result ) )
    {
        return false;
    }

    const CoreAllocation::RuntimeReserveOwnerHandle owner = ReplayPredictionReserveOwner();
    CoreAllocation::RuntimeAllocationScope replayAllocationScope( CoreAllocation::RuntimeAllocationPhase::Replay );
    CoreAllocation::RuntimeReserveOwnerScope ownerScope( owner );
    CoreAllocation::RuntimeReserveGrowthScope growthScope( owner, CoreAllocation::RuntimeReservePhase::Replay, result );
    values.reserve( requestedCapacity );
    return requestedCapacity <= values.capacity();
}

template <typename T>
bool ReserveReplayPredictionFramePayloadVectors( std::vector<RunReplayPredictionFrame>& frames,
                                                 std::size_t requestedFrameCount,
                                                 std::size_t requestedCapacityPerFrame,
                                                 int frameNumber,
                                                 const char* targetName,
                                                 std::vector<T> RunReplayPredictionFrame::* member )
{
    // Runtime allocation policy: prediction captures many future frames. Batch
    // the per-frame payload reserves under one replay approval so validation
    // sees one setup event instead of one growth request per future frame.
    if ( requestedCapacityPerFrame == 0 )
    {
        return true;
    }

    uint64_t oldBytes = 0;
    for ( std::size_t i = 0; i < requestedFrameCount; ++i )
    {
        uint64_t frameBytes = 0;
        if ( !ReplayPredictionCapacityBytes<T>( ( frames[i].*member ).capacity(), frameBytes ) ||
             oldBytes > ( std::numeric_limits<uint64_t>::max )() - frameBytes )
        {
            return false;
        }
        oldBytes += frameBytes;
    }
    uint64_t requestedBytes = 0;
    if ( !ReplayPredictionFramePayloadBytes<T>( requestedFrameCount, requestedCapacityPerFrame, requestedBytes ) )
    {
        return false;
    }
    if ( requestedBytes <= oldBytes )
    {
        return true;
    }
    if ( requestedBytes > static_cast<uint64_t>( REPLAY_PREDICTION_RESERVE_HARD_BYTES ) )
    {
        return false;
    }

    CoreAllocation::RuntimeReserveGrowthResult result = {};
    if ( !RequestReplayPredictionReserveGrowth( targetName,
                                                frameNumber,
                                                static_cast<int>( oldBytes ),
                                                static_cast<int>( requestedBytes ),
                                                1,
                                                result ) )
    {
        return false;
    }

    const CoreAllocation::RuntimeReserveOwnerHandle owner = ReplayPredictionReserveOwner();
    CoreAllocation::RuntimeAllocationScope replayAllocationScope( CoreAllocation::RuntimeAllocationPhase::Replay );
    CoreAllocation::RuntimeReserveOwnerScope ownerScope( owner );
    CoreAllocation::RuntimeReserveGrowthScope growthScope( owner, CoreAllocation::RuntimeReservePhase::Replay, result );
    for ( std::size_t i = 0; i < requestedFrameCount; ++i )
    {
        ( frames[i].*member ).reserve( requestedCapacityPerFrame );
    }
    return true;
}

// Concept: future-node building is an incremental cache.
//
// Prediction can hold thousands of future frames. Clearing and rebuilding the
// future-impact tree every render frame makes the path visualizer scale with the
// full horizon. These cursors let each frame continue where the last frame stopped.
void ClearReplayPredictionFutureNodeCache( RunReplayPredictionState& prediction )
{
    prediction.futureNodeCache.futureNodes.clear();
    prediction.futureNodeCache.futureNodeBuildScratch.clear();
    prediction.futureNodeCache.futureNodesBuiltFrameCount = 0;
    prediction.futureNodeCache.futureNodesBuiltContactIndex = 0;
    prediction.futureNodeCache.futureNodesBuiltTargetId = ReplayBodyId{};
    prediction.futureNodeCache.futureNodesTopologyVersion = 0;
    prediction.futureNodeCache.futureNodesBuiltRagdollVisuals = prediction.ragdollVisualsEnabled;
    prediction.futureNodeCache.futureNodesBuiltFromBuildFrames = false;
    prediction.futureNodeCache.futureNodesCacheValid = false;
    prediction.futureNodeCache.retainedMarkerCount = 0;
    prediction.trajectoryBuild.childFrameCount = 0;
    prediction.trajectoryBuild.builtNodeCount = 0;
    prediction.trajectoryBuild.topologyVersion = 0;
}


// Concept: retained replay and prediction samples share body identity rules.
//
// ReplayBodyId is authority; modelIndex is a cache hint into the current sample.
// Invariant: solver lookup preserves its legacy negative-sentinel scan, while
// prediction lookup rejects negative hints before scanning.
template <typename FrameSample, typename BodySample>
const BodySample* FindReplayBodyByIdInSample( const FrameSample& sample, ReplayBodyId id )
{
    for ( const BodySample& body : sample.bodies )
    {
        if ( body.id.value == id.value )
        {
            return &body;
        }
    }
    return nullptr;
}

template <typename FrameSample, typename BodySample, bool AllowNegativeModelIndex>
const BodySample* FindReplayBodyByModelIndexInSample( const FrameSample& sample, int modelIndex )
{
    if constexpr ( !AllowNegativeModelIndex )
    {
        if ( modelIndex < 0 )
        {
            return nullptr;
        }
    }

    if ( modelIndex >= 0 && modelIndex < static_cast<int>( sample.bodies.size() ) )
    {
        const BodySample& body = sample.bodies[static_cast<std::size_t>( modelIndex )];
        if ( body.modelRow.value == modelIndex )
        {
            return &body;
        }
    }

    for ( const BodySample& body : sample.bodies )
    {
        if ( body.modelRow.value == modelIndex )
        {
            return &body;
        }
    }
    return nullptr;
}

template <typename FrameSample, typename BodySample, bool AllowNegativeModelIndex>
ReplayBodyId ReplayBodyIdForModelIndexInSample( const FrameSample& sample, int modelIndex )
{
    if ( const BodySample* body =
             FindReplayBodyByModelIndexInSample<FrameSample, BodySample, AllowNegativeModelIndex>( sample,
                                                                                                   modelIndex ) )
    {
        return body->id;
    }
    return ReplayBodyId{};
}

const ReplaySolverBodySample* FindReplayBodyById( const ReplaySolverFrameSample& sample, ReplayBodyId id )
{
    return FindReplayBodyByIdInSample<ReplaySolverFrameSample, ReplaySolverBodySample>( sample, id );
}

const RunReplayPredictionBodySample* FindReplayPredictionBodyById( const RunReplayPredictionFrame& frame,
                                                                   ReplayBodyId id )
{
    return FindReplayBodyByIdInSample<RunReplayPredictionFrame, RunReplayPredictionBodySample>( frame, id );
}

const RunReplayPredictionBodySample* FindReplayPredictionBodyByModelIndex( const RunReplayPredictionFrame& frame,
                                                                           int modelIndex )
{
    return FindReplayBodyByModelIndexInSample<RunReplayPredictionFrame, RunReplayPredictionBodySample, false>(
        frame,
        modelIndex );
}

const ReplaySolverBodySample* FindReplayBodyByModelIndex( const ReplaySolverFrameSample& sample, int modelIndex )
{
    return FindReplayBodyByModelIndexInSample<ReplaySolverFrameSample, ReplaySolverBodySample, true>( sample,
                                                                                                      modelIndex );
}

const ReplaySolverBodySample*
FindReplayBodyByIdWithHint( const ReplaySolverFrameSample& sample, ReplayBodyId id, int modelIndex )
{
    if ( const ReplaySolverBodySample* hinted = FindReplayBodyByModelIndex( sample, modelIndex ) )
    {
        if ( hinted->id.value == id.value )
        {
            return hinted;
        }
    }
    return FindReplayBodyById( sample, id );
}

ReplayBodyId ReplayPredictionBodyIdForModelIndex( const RunReplayPredictionFrame& frame, int modelIndex )
{
    return ReplayBodyIdForModelIndexInSample<RunReplayPredictionFrame, RunReplayPredictionBodySample, false>(
        frame,
        modelIndex );
}

bool ReplayModelIndexIsRagdollPart( const SceneEntityStore& entities, int modelIndex )
{
    // Hazard: physics debug contacts use -1 for terrain/world counterparts.
    // That sentinel is not a scene row and must never reach group metadata.
    if ( modelIndex < 0 || modelIndex >= entities.Count() )
    {
        return false;
    }
    const SceneEntityRecord* entity = entities.TryGet( modelIndex );
    return entity && entity->behaviorGroup.kind == SceneBehaviorGroupKind::SimpleRagdoll;
}

int ReplayRagdollTorsoModelIndexForPart( const SceneEntityStore& entities, int modelIndex )
{
    const SceneEntityRecord* entity = entities.TryGet( modelIndex );
    if ( !entity || entity->behaviorGroup.kind != SceneBehaviorGroupKind::SimpleRagdoll )
    {
        return modelIndex;
    }
    const int rootRow = entities.FindBySceneObjectId( entity->behaviorGroup.rootObjectId );
    return rootRow >= 0 ? rootRow : modelIndex;
}

Vector3 ReplayNormalizeOr( Vector3 value, const Vector3& fallback )
{
    const float magSq = VectorMagSquared( value );
    if ( magSq <= TOLERANCE * TOLERANCE )
    {
        return fallback;
    }
    value /= sqrtf( magSq );
    return value;
}

Quaternion ReplaySolverBodyOrientation( const ReplaySolverBodySample& body )
{
    Quaternion orientation( body.orientation[0], body.orientation[1], body.orientation[2], body.orientation[3] );
    orientation.Normalise();
    return orientation;
}

const RunReplayPredictionBodySample*
FindReplayPredictionBodyByIdWithHint( const RunReplayPredictionFrame& frame, ReplayBodyId id, int modelIndex )
{
    if ( const RunReplayPredictionBodySample* body = FindReplayPredictionBodyByModelIndex( frame, modelIndex ) )
    {
        if ( body->id.value == id.value )
        {
            return body;
        }
    }
    return FindReplayPredictionBodyById( frame, id );
}

// Concept: TrajectoryStore records are the future line-draw source. Branch
// ordinal 0 is the committed prediction; branch 1 is the in-progress build
// preview; child branches are offset by source so same-target refreshes do not
// overwrite the old visible future before the published prefix catches up.
constexpr uint16_t REPLAY_TRAJECTORY_COMMITTED_BRANCH = 0;
constexpr uint16_t REPLAY_TRAJECTORY_BUILD_BRANCH = 1;

int ReplayTrajectoryFrameNumberForReserve( ReplayFrameIndex frameIndex )
{
    return static_cast<int>(
        (std::min)( frameIndex, static_cast<ReplayFrameIndex>( ( std::numeric_limits<int>::max )() ) ) );
}

ReplayTrajectoryRecordKey ReplayTrajectoryKey( ReplayBodyId bodyId, ReplayTrajectoryLane lane, uint16_t branchOrdinal )
{
    ReplayTrajectoryRecordKey key;
    key.bodyId = bodyId;
    key.lane = lane;
    key.branchOrdinal = branchOrdinal;
    return key;
}

bool ReserveReplayTrajectoryRecordSlot( ReplayTrajectoryStore& store,
                                        const ReplayTrajectoryRecordKey& key,
                                        int frameNumber )
{
    return store.FindRecord( key ) || store.ReserveRecords( store.RecordCount() + 1u, frameNumber );
}

ReplayTrajectoryRecord* BeginReplayTrajectoryRecord( ReplayTrajectoryStore& store,
                                                     const ReplayTrajectoryRecordKey& key,
                                                     uint16_t styleId,
                                                     ReplayBodyId parentId,
                                                     int depth,
                                                     ReplayFrameIndex firstFrame,
                                                     bool contactDerived,
                                                     std::size_t pointCapacity )
{
    const int frameNumber = ReplayTrajectoryFrameNumberForReserve( firstFrame );
    if ( !ReserveReplayTrajectoryRecordSlot( store, key, frameNumber ) )
    {
        return nullptr;
    }

    ReplayTrajectoryRecord* record =
        store.BeginReplaceRecord( key, styleId, parentId, depth, firstFrame, contactDerived );
    if ( !record || !store.ReserveRecordPoints( *record, pointCapacity, frameNumber ) )
    {
        return nullptr;
    }
    return record;
}

bool AppendReplayTrajectoryPoint( ReplayTrajectoryStore& store,
                                  ReplayTrajectoryRecord& record,
                                  ReplayFrameIndex frameIndex,
                                  const Vector3& position )
{
    if ( !store.TryAppendPoint( record, { frameIndex, position } ) )
    {
        return false;
    }
    store.PublishPrefix( record, record.points.size() );
    return true;
}

ReplayFrameIndex ReplayOldestFrameFromStats( const ReplayRecorderStats& stats )
{
    return stats.nextFrameIndex > static_cast<ReplayFrameIndex>( stats.sampleCount )
               ? stats.nextFrameIndex - static_cast<ReplayFrameIndex>( stats.sampleCount )
               : 0;
}

// Concept: the past-root trajectory mirrors the solver recorder window. Rebuild
// handles target changes and ring eviction; capture-time append handles the
// ordinary newest-sample case without re-walking retained history.
ReplayTrajectoryRecordKey ReplayPastRootTrajectoryKey( ReplayBodyId targetId )
{
    return ReplayTrajectoryKey( targetId, ReplayTrajectoryLane::PastRoot, 0 );
}

ReplayTrajectoryRecord* BeginReplayPastRootTrajectoryRecord( ReplayTrajectoryStore& store,
                                                             ReplayBodyId targetId,
                                                             std::size_t pointCapacity,
                                                             int frameNumber )
{
    return BeginReplayTrajectoryRecord( store,
                                        ReplayPastRootTrajectoryKey( targetId ),
                                        0,
                                        ReplayBodyId{},
                                        0,
                                        static_cast<ReplayFrameIndex>( frameNumber ),
                                        false,
                                        pointCapacity );
}

struct ReplayPastRootRebuildContext
{
    ReplayTrajectoryStore* store = nullptr;
    ReplayTrajectoryRecord* record = nullptr;
    SkullbonezCore::Physics::ModelRowHint targetModelRow;
    ReplayFrameIndex firstFrame = 0;
    bool hasSample = false;
    bool ok = true;
};

void WaitForReplayPredictionWorkerIdle( RunReplayPredictionState& prediction )
{
    while ( prediction.build.workerTask && prediction.build.workerTask->IsInFlight() )
    {
        // Hazard: cancellation is a scene/branch mutation edge. The worker task
        // owns buildFrames and prediction trajectory slots until it drops
        // in-flight, so clearing those arrays before this wait would let render
        // read freed scratch.
        std::this_thread::yield();
    }
}

std::size_t ReplayPredictionTrajectoryRecordCapacity()
{
    return 2u + REPLAY_PATH_MAX_FUTURE_NODES * 4u + REPLAY_PATH_MAX_ROOT_TARGETS;
}

uint16_t ReplayPredictionChildTrajectoryBranch( std::size_t nodeIndex, bool usingBuildFrames )
{
    const std::size_t branchBase = usingBuildFrames ? REPLAY_PATH_MAX_FUTURE_NODES : 0u;
    return static_cast<uint16_t>(
        (std::min)( branchBase + nodeIndex, static_cast<std::size_t>( ( std::numeric_limits<uint16_t>::max )() ) ) );
}

bool PrepareReplayPredictionTrajectoryBuild( RunReplayPredictionState& prediction,
                                             ReplayBodyId rootId,
                                             std::size_t frameCapacity )
{
    prediction.trajectoryBuild = RunReplayPredictionTrajectoryBuildState{};
    if ( rootId.value == 0 )
    {
        return true;
    }

    const std::size_t recordCapacity =
        (std::max)( prediction.trajectoryStore.RecordCount(), ReplayPredictionTrajectoryRecordCapacity() );
    if ( !prediction.trajectoryStore.ReserveRecords( recordCapacity, 0 ) )
    {
        return false;
    }

    ReplayTrajectoryRecord* rootRecord = BeginReplayTrajectoryRecord(
        prediction.trajectoryStore,
        ReplayTrajectoryKey( rootId, ReplayTrajectoryLane::FutureRoot, REPLAY_TRAJECTORY_BUILD_BRANCH ),
        0,
        ReplayBodyId{},
        0,
        0,
        false,
        frameCapacity );
    if ( !rootRecord )
    {
        return false;
    }
    rootRecord->points.resize( frameCapacity );

    // Invariant: branch 1 is the in-progress prediction record. Branch 0 stays
    // as the committed future until the build swap publishes the completed
    // frame vector.
    prediction.trajectoryBuild.rootId = rootId;
    prediction.trajectoryBuild.usingBuildFrames = true;
    prediction.trajectoryBuild.valid = true;
    return true;
}

bool PublishReplayPredictionRootTrajectoryFrame( RunReplayPredictionState& prediction,
                                                 const RunReplayPredictionFrame& frame,
                                                 std::size_t frameSlot )
{
    if ( !prediction.trajectoryBuild.valid || prediction.trajectoryBuild.rootId.value == 0 ||
         !prediction.trajectoryBuild.usingBuildFrames )
    {
        return true;
    }

    ReplayTrajectoryRecord* record =
        prediction.trajectoryStore.FindRecord( ReplayTrajectoryKey( prediction.trajectoryBuild.rootId,
                                                                    ReplayTrajectoryLane::FutureRoot,
                                                                    REPLAY_TRAJECTORY_BUILD_BRANCH ) );
    if ( !record || frameSlot >= record->points.size() )
    {
        prediction.trajectoryBuild.valid = false;
        return false;
    }

    const RunReplayPredictionBodySample* body =
        FindReplayPredictionBodyByIdWithHint( frame,
                                              prediction.trajectoryBuild.rootId,
                                              prediction.simulation.targetModelRow.value );
    if ( !body )
    {
        prediction.trajectoryBuild.valid = false;
        return false;
    }

    record->points[frameSlot] = { frame.frameIndex, body->position };
    prediction.trajectoryBuild.rootFrameCount = frameSlot + 1u;
    return true;
}

bool RebuildReplayPredictionCommittedRootTrajectory( RunReplayPredictionState& prediction )
{
    if ( prediction.simulation.targetId.value == 0 || prediction.simulation.frames.size() < 2u )
    {
        return true;
    }

    ReplayTrajectoryRecord* record =
        BeginReplayTrajectoryRecord( prediction.trajectoryStore,
                                     ReplayTrajectoryKey( prediction.simulation.targetId,
                                                          ReplayTrajectoryLane::FutureRoot,
                                                          REPLAY_TRAJECTORY_COMMITTED_BRANCH ),
                                     0,
                                     ReplayBodyId{},
                                     0,
                                     0,
                                     false,
                                     prediction.simulation.frames.size() );
    if ( !record )
    {
        prediction.trajectoryBuild.valid = false;
        return false;
    }

    for ( const RunReplayPredictionFrame& frame : prediction.simulation.frames )
    {
        const RunReplayPredictionBodySample* body =
            FindReplayPredictionBodyByIdWithHint( frame,
                                                  prediction.simulation.targetId,
                                                  prediction.simulation.targetModelRow.value );
        if ( body &&
             !AppendReplayTrajectoryPoint( prediction.trajectoryStore, *record, frame.frameIndex, body->position ) )
        {
            prediction.trajectoryBuild.valid = false;
            return false;
        }
    }
    prediction.trajectoryBuild.rootId = prediction.simulation.targetId;
    prediction.trajectoryBuild.usingBuildFrames = false;
    prediction.trajectoryBuild.rootFrameCount = record->points.size();
    prediction.trajectoryBuild.childFrameCount = 0;
    prediction.trajectoryBuild.builtNodeCount = 0;
    prediction.trajectoryBuild.topologyVersion = 0;
    prediction.trajectoryBuild.valid = true;
    return true;
}

bool BuildReplayPredictionChildTrajectoryRecord( RunReplayPredictionState& prediction,
                                                 const std::vector<RunReplayPredictionFrame>& frames,
                                                 std::size_t frameCount,
                                                 const RunReplayPathTraceNode& node,
                                                 std::size_t nodeIndex,
                                                 bool usingBuildFrames,
                                                 ReplayTrajectoryLane lane,
                                                 bool seedOutgoingEntry )
{
    if ( frameCount == 0 )
    {
        return true;
    }

    const uint16_t branchOrdinal = ReplayPredictionChildTrajectoryBranch( nodeIndex, usingBuildFrames );
    ReplayTrajectoryRecord* record =
        BeginReplayTrajectoryRecord( prediction.trajectoryStore,
                                     ReplayTrajectoryKey( node.id, lane, branchOrdinal ),
                                     static_cast<uint16_t>( std::clamp( node.depth, 0, 0xFFFF ) ),
                                     node.parentId,
                                     node.depth,
                                     node.firstFrame,
                                     node.contactDerived,
                                     frameCount + ( seedOutgoingEntry ? 1u : 0u ) );
    if ( !record )
    {
        prediction.trajectoryBuild.valid = false;
        return false;
    }

    if ( seedOutgoingEntry )
    {
        const RunReplayPredictionBodySample* initial =
            FindReplayPredictionBodyByIdWithHint( frames[0], node.id, node.modelRow.value );
        if ( initial && !AppendReplayTrajectoryPoint( prediction.trajectoryStore,
                                                      *record,
                                                      frames[0].frameIndex,
                                                      initial->position ) )
        {
            prediction.trajectoryBuild.valid = false;
            return false;
        }
    }

    for ( std::size_t frameIndex = 0; frameIndex < frameCount; ++frameIndex )
    {
        const RunReplayPredictionFrame& frame = frames[frameIndex];
        if ( seedOutgoingEntry && lane == ReplayTrajectoryLane::FutureChildOutgoing && frameIndex == 0u )
        {
            continue;
        }
        const bool includeFrame = lane == ReplayTrajectoryLane::FutureChildIncoming
                                      ? frame.frameIndex <= node.firstFrame
                                      : frame.frameIndex >= node.firstFrame;
        if ( !includeFrame )
        {
            continue;
        }

        const RunReplayPredictionBodySample* body =
            FindReplayPredictionBodyByIdWithHint( frame, node.id, node.modelRow.value );
        if ( body &&
             !AppendReplayTrajectoryPoint( prediction.trajectoryStore, *record, frame.frameIndex, body->position ) )
        {
            prediction.trajectoryBuild.valid = false;
            return false;
        }
    }
    return true;
}

bool AppendReplayPredictionChildTrajectoryFrames( RunReplayPredictionState& prediction,
                                                  const std::vector<RunReplayPredictionFrame>& frames,
                                                  std::size_t beginFrame,
                                                  std::size_t frameCount,
                                                  const RunReplayPathTraceNode& node,
                                                  std::size_t nodeIndex,
                                                  bool usingBuildFrames,
                                                  ReplayTrajectoryLane lane )
{
    const uint16_t branchOrdinal = ReplayPredictionChildTrajectoryBranch( nodeIndex, usingBuildFrames );
    ReplayTrajectoryRecord* record =
        prediction.trajectoryStore.FindRecord( ReplayTrajectoryKey( node.id, lane, branchOrdinal ) );
    if ( !record )
    {
        return BuildReplayPredictionChildTrajectoryRecord( prediction,
                                                           frames,
                                                           frameCount,
                                                           node,
                                                           nodeIndex,
                                                           usingBuildFrames,
                                                           lane,
                                                           lane == ReplayTrajectoryLane::FutureChildOutgoing );
    }

    const int frameNumber =
        frameCount > 0u ? ReplayTrajectoryFrameNumberForReserve( frames[frameCount - 1u].frameIndex ) : 0;
    if ( !prediction.trajectoryStore.ReserveRecordPoints( *record, frameCount + 1u, frameNumber ) )
    {
        prediction.trajectoryBuild.valid = false;
        return false;
    }

    for ( std::size_t frameIndex = beginFrame; frameIndex < frameCount; ++frameIndex )
    {
        const RunReplayPredictionFrame& frame = frames[frameIndex];
        const bool includeFrame = lane == ReplayTrajectoryLane::FutureChildIncoming
                                      ? frame.frameIndex <= node.firstFrame
                                      : frame.frameIndex >= node.firstFrame;
        if ( !includeFrame )
        {
            continue;
        }

        const RunReplayPredictionBodySample* body =
            FindReplayPredictionBodyByIdWithHint( frame, node.id, node.modelRow.value );
        if ( body &&
             !AppendReplayTrajectoryPoint( prediction.trajectoryStore, *record, frame.frameIndex, body->position ) )
        {
            prediction.trajectoryBuild.valid = false;
            return false;
        }
    }
    return true;
}

void UpdateReplayPredictionTrajectoryStore( RunReplayPredictionState& prediction,
                                            const std::vector<RunReplayPredictionFrame>& frames,
                                            std::size_t frameCount,
                                            bool usingBuildFrames,
                                            ReplayBodyId rootId )
{
    frameCount = (std::min)( frameCount, frames.size() );
    if ( rootId.value == 0 || frameCount < 2u )
    {
        prediction.trajectoryBuild.childFrameCount = 0;
        prediction.trajectoryBuild.builtNodeCount = 0;
        return;
    }

    const std::size_t nodeCount =
        (std::min)( prediction.futureNodeCache.futureNodes.size(), REPLAY_PATH_MAX_FUTURE_NODES );
    const uint32_t topologyVersion = prediction.futureNodeCache.futureNodesTopologyVersion;
    const bool sourceChanged = !prediction.trajectoryBuild.valid ||
                               prediction.trajectoryBuild.rootId.value != rootId.value ||
                               prediction.trajectoryBuild.usingBuildFrames != usingBuildFrames ||
                               prediction.trajectoryBuild.topologyVersion != topologyVersion ||
                               prediction.trajectoryBuild.childFrameCount > frameCount ||
                               prediction.trajectoryBuild.builtNodeCount > nodeCount;
    if ( !sourceChanged && prediction.trajectoryBuild.childFrameCount == frameCount &&
         prediction.trajectoryBuild.builtNodeCount == nodeCount )
    {
        return;
    }

    // Hazard: child records depend on the frozen future-node order. When the
    // source prefix or topology changes, replace the affected records instead
    // of mutating already-published points under an old version.
    if ( !sourceChanged && prediction.trajectoryBuild.childFrameCount < frameCount )
    {
        const std::size_t existingNodeCount = (std::min)( prediction.trajectoryBuild.builtNodeCount, nodeCount );
        for ( std::size_t i = 0; i < existingNodeCount; ++i )
        {
            const RunReplayPathTraceNode& node = prediction.futureNodeCache.futureNodes[i];
            if ( !AppendReplayPredictionChildTrajectoryFrames( prediction,
                                                               frames,
                                                               prediction.trajectoryBuild.childFrameCount,
                                                               frameCount,
                                                               node,
                                                               i,
                                                               usingBuildFrames,
                                                               ReplayTrajectoryLane::FutureChildIncoming ) ||
                 !AppendReplayPredictionChildTrajectoryFrames( prediction,
                                                               frames,
                                                               prediction.trajectoryBuild.childFrameCount,
                                                               frameCount,
                                                               node,
                                                               i,
                                                               usingBuildFrames,
                                                               ReplayTrajectoryLane::FutureChildOutgoing ) )
            {
                return;
            }
        }
    }

    const std::size_t firstNode = sourceChanged ? 0u : prediction.trajectoryBuild.builtNodeCount;
    for ( std::size_t i = firstNode; i < nodeCount; ++i )
    {
        const RunReplayPathTraceNode& node = prediction.futureNodeCache.futureNodes[i];
        if ( !BuildReplayPredictionChildTrajectoryRecord( prediction,
                                                          frames,
                                                          frameCount,
                                                          node,
                                                          i,
                                                          usingBuildFrames,
                                                          ReplayTrajectoryLane::FutureChildIncoming,
                                                          false ) ||
             !BuildReplayPredictionChildTrajectoryRecord( prediction,
                                                          frames,
                                                          frameCount,
                                                          node,
                                                          i,
                                                          usingBuildFrames,
                                                          ReplayTrajectoryLane::FutureChildOutgoing,
                                                          true ) )
        {
            return;
        }
    }

    if ( sourceChanged )
    {
        prediction.trajectoryBuild.rootId = rootId;
        prediction.trajectoryBuild.usingBuildFrames = usingBuildFrames;
        prediction.trajectoryBuild.topologyVersion = topologyVersion;
        prediction.trajectoryBuild.valid = true;
    }
    prediction.trajectoryBuild.childFrameCount = frameCount;
    prediction.trajectoryBuild.builtNodeCount = nodeCount;
}

bool ReplayPredictionFutureTreeReadyForDraw( const RunReplayPredictionState& prediction,
                                             ReplayBodyId rootId,
                                             bool usingBuildFrames,
                                             std::size_t frameCount )
{
    const std::size_t nodeCount =
        (std::min)( prediction.futureNodeCache.futureNodes.size(), REPLAY_PATH_MAX_FUTURE_NODES );
    return nodeCount > 0 && prediction.futureNodeCache.futureNodesCacheValid &&
           prediction.futureNodeCache.futureNodesTopologyVersion != 0 && prediction.trajectoryBuild.valid &&
           prediction.trajectoryBuild.rootId.value == rootId.value &&
           prediction.trajectoryBuild.usingBuildFrames == usingBuildFrames &&
           prediction.trajectoryBuild.topologyVersion == prediction.futureNodeCache.futureNodesTopologyVersion &&
           prediction.trajectoryBuild.builtNodeCount == nodeCount &&
           prediction.trajectoryBuild.childFrameCount >= frameCount;
}

bool ReplayPredictionBodyHasVisibleLinearMotion( const RunReplayPredictionBodySample& body )
{
    return VectorMagSquared( body.linearVelocity ) >= REPLAY_PREDICTION_CHILD_LINEAR_SPEED_SQ;
}

// Concept: rest is decided by how the story ends, never by a momentary pause.
//
// A body has a resting pose only when the COMPLETED prediction ends with it
// visibly still and it has not drifted across the final grace window. Bodies
// still moving at the horizon end return false: they get a travel line and no
// grey box, because any resting pose we could draw for them would be a guess.
// Invariant: callers must pass a completed frame buffer; a growing build
// prefix has no authoritative final frame.
bool ReplayPredictionBodyRestingPose( const std::vector<RunReplayPredictionFrame>& frames,
                                      std::size_t frameCount,
                                      ReplayBodyId id,
                                      int modelIndexHint,
                                      Vector3& outPosition,
                                      Quaternion& outOrientation )
{
    frameCount = (std::min)( frameCount, frames.size() );
    if ( frameCount < 2 || id.value == 0 )
    {
        return false;
    }

    const RunReplayPredictionBodySample* finalBody =
        FindReplayPredictionBodyByIdWithHint( frames[frameCount - 1], id, modelIndexHint );
    if ( !finalBody || ReplayPredictionBodyHasVisibleLinearMotion( *finalBody ) )
    {
        return false;
    }

    const std::size_t graceSlots =
        (std::min)( static_cast<std::size_t>( REPLAY_PREDICTION_REST_GRACE_FRAMES ), frameCount - 1 );
    const RunReplayPredictionBodySample* graceBody =
        FindReplayPredictionBodyByIdWithHint( frames[frameCount - 1 - graceSlots], id, modelIndexHint );
    if ( !graceBody || ReplayPredictionBodyHasVisibleLinearMotion( *graceBody ) ||
         VectorMagSquared( finalBody->position - graceBody->position ) > REPLAY_PREDICTION_REST_POSITION_EPSILON_SQ )
    {
        return false;
    }

    outPosition = finalBody->position;
    outOrientation = finalBody->orientation;
    return true;
}

bool ReplayContactHasModelIndex( const SkullbonezCore::Physics::PhysicsSolverPersistentContactSample& contact,
                                 int modelIndex )
{
    return modelIndex >= 0 && ( contact.bodyA == modelIndex || contact.bodyB == modelIndex );
}

int ReplayContactOtherModelIndex( const SkullbonezCore::Physics::PhysicsSolverPersistentContactSample& contact,
                                  int modelIndex )
{
    if ( contact.bodyA == modelIndex )
    {
        return contact.bodyB;
    }
    if ( contact.bodyB == modelIndex )
    {
        return contact.bodyA;
    }
    return -1;
}

Vector3 ReplayContactPoint( const ReplaySolverFrameSample& sample,
                            const SkullbonezCore::Physics::PhysicsSolverPersistentContactSample& contact )
{
    if ( const ReplaySolverBodySample* bodyA = FindReplayBodyByModelIndex( sample, contact.bodyA ) )
    {
        return bodyA->position + contact.rA;
    }
    if ( const ReplaySolverBodySample* bodyB = FindReplayBodyByModelIndex( sample, contact.bodyB ) )
    {
        return bodyB->position + contact.rB;
    }
    return SkullbonezCore::Math::Vector::ZERO_VECTOR;
}

Vector3 ReplayContactNormalForModel( const SkullbonezCore::Physics::PhysicsSolverPersistentContactSample& contact,
                                     int modelIndex )
{
    Vector3 normal = contact.normal;
    if ( contact.isTerrain && VectorMagSquared( contact.terrainNormal ) > TOLERANCE * TOLERANCE )
    {
        normal = contact.terrainNormal;
    }
    if ( contact.bodyB == modelIndex && !contact.isTerrain )
    {
        normal = normal * -1.0f;
    }
    return ReplayNormalizeOr( normal, Vector3( 0.0f, 1.0f, 0.0f ) );
}

Vector3 ReplayContactImpulseForModel( const SkullbonezCore::Physics::PhysicsSolverPersistentContactSample& contact,
                                      int modelIndex )
{
    const Vector3 rowImpulse =
        contact.normal * contact.accN + contact.tangent1 * contact.accT1 + contact.tangent2 * contact.accT2;
    if ( contact.bodyB == modelIndex && !contact.isTerrain )
    {
        return rowImpulse;
    }
    return rowImpulse * -1.0f;
}

int ReplayFindPipelineIndexForContact( const SkullbonezCore::Physics::PhysicsSolverSnapshot& snapshot,
                                       const SkullbonezCore::Physics::PhysicsSolverPersistentContactSample& contact )
{
    for ( int i = 0; i < static_cast<int>( snapshot.pipelineTrace.size() ); ++i )
    {
        const PhysicsPipelineRecord& record = snapshot.pipelineTrace[static_cast<std::size_t>( i )];
        if ( record.featureId == contact.featureId &&
             ( ( record.bodyA == contact.bodyA && record.bodyB == contact.bodyB ) ||
               ( record.bodyA == contact.bodyB && record.bodyB == contact.bodyA ) ) )
        {
            return i;
        }
    }
    return -1;
}


std::size_t ReplayPathStrideForSampleCount( std::size_t sampleCount )
{
    if ( sampleCount <= REPLAY_PATH_MAX_SEGMENTS )
    {
        return 1;
    }
    return ( sampleCount + REPLAY_PATH_MAX_SEGMENTS - 1 ) / REPLAY_PATH_MAX_SEGMENTS;
}

struct ReplayPredictionDrawFrameWindow
{
    ReplayFrameIndex lastFrame = 0;
    ReplayFrameIndex revealFrame = 0;
    std::size_t sampleStride = 1;
};


ReplayPredictionDrawFrameWindow
PrepareReplayPredictionDrawFrameWindow( RunReplayPredictionState& prediction,
                                        const std::vector<RunReplayPredictionFrame>& frames,
                                        std::size_t frameCount )
{
    ReplayPredictionDrawFrameWindow window;
    frameCount = (std::min)( frameCount, frames.size() );
    if ( frameCount == 0 )
    {
        return window;
    }

    // Invariant: root, child, marker, affected-body, and ragdoll lanes all draw
    // against this one reveal clamp for the selected prediction prefix.
    window.lastFrame = frames[frameCount - 1].frameIndex;
    window.revealFrame = ReplayPredictionRevealFrameIndex( prediction, window.lastFrame );
    window.sampleStride = ReplayPathStrideForSampleCount( frameCount );
    return window;
}


void ClearReplayPredictionBaseline( ReplayPredictionBaselineSnapshot& baseline )
{
    baseline.valid = false;
    baseline.comparisonActive = false;
    baseline.rootId = ReplayBodyId{};
    baseline.rootModelRow.value = -1;
    baseline.lastFrame = 0;
    baseline.rootPolyline.clear();
    baseline.bodyPoses.clear();
    baseline.divergenceValid = false;
    baseline.divergenceUnits = 0.0f;
}

bool PublishReplayPredictionBaselineRootTrajectory( RunReplayPredictionState& prediction );
std::size_t ReplayTrajectoryPublishedPointCount( const ReplayTrajectoryRecord& record );
const ReplayTrajectoryRecord* ReplayTrajectoryRecordForDraw( const ReplayTrajectoryStore& store,
                                                             ReplayBodyId id,
                                                             ReplayTrajectoryLane lane,
                                                             uint16_t branchOrdinal );

// Concept: baseline capture freezes the old committed future before a velocity
// edit. It keeps a bounded root path plus completed entry/rest poses so the
// renderer can contrast "what would have happened" against the nudged rebuild.
bool CaptureReplayPredictionBaselineSnapshot( RunReplayPredictionState& prediction,
                                              const std::vector<RunReplayPredictionFrame>& frames,
                                              std::size_t frameCount,
                                              ReplayBodyId rootId,
                                              int rootModelIndex )
{
    frameCount = (std::min)( frameCount, frames.size() );
    ClearReplayPredictionBaseline( prediction.baseline );
    if ( frameCount < 2 || rootId.value == 0 )
    {
        return false;
    }

    const RunReplayPredictionFrame& firstFrame = frames.front();
    const RunReplayPredictionFrame& lastFrame = frames[frameCount - 1];
    const std::size_t bodyCapacity =
        (std::min)( static_cast<std::size_t>( SkullbonezCore::Scene::Capacity::MAX_SCENE_OBJECTS ),
                    firstFrame.bodies.size() );
    const int reserveFrame = static_cast<int>( lastFrame.frameIndex );
    if ( !ReserveReplayPredictionVector( prediction.baseline.rootPolyline,
                                         REPLAY_PREDICTION_BASELINE_ROOT_POINT_CAPACITY,
                                         reserveFrame,
                                         "ReplayPredictionBaselineRootPoint[]" ) ||
         !ReserveReplayPredictionVector( prediction.baseline.bodyPoses,
                                         bodyCapacity,
                                         reserveFrame,
                                         "ReplayPredictionBaselineBodyPose[]" ) )
    {
        ClearReplayPredictionBaseline( prediction.baseline );
        return false;
    }

    prediction.baseline.rootId = rootId;
    prediction.baseline.rootModelRow.value = rootModelIndex;
    prediction.baseline.lastFrame = lastFrame.frameIndex;

    const std::size_t rootStride = frameCount <= REPLAY_PREDICTION_BASELINE_ROOT_POINT_CAPACITY
                                       ? 1u
                                       : ( frameCount + REPLAY_PREDICTION_BASELINE_ROOT_POINT_CAPACITY - 1u ) /
                                             REPLAY_PREDICTION_BASELINE_ROOT_POINT_CAPACITY;
    for ( std::size_t frameSlot = 0; frameSlot < frameCount; ++frameSlot )
    {
        const RunReplayPredictionFrame& frame = frames[frameSlot];
        const bool endpointFrame = frameSlot == 0 || frameSlot + 1 == frameCount;
        if ( !endpointFrame && rootStride > 1u &&
             ( frame.frameIndex % static_cast<ReplayFrameIndex>( rootStride ) ) != 0u )
        {
            continue;
        }

        const RunReplayPredictionBodySample* body =
            FindReplayPredictionBodyByIdWithHint( frame, rootId, rootModelIndex );
        if ( !body )
        {
            continue;
        }

        ReplayPredictionBaselineRootPoint point;
        point.frameIndex = frame.frameIndex;
        point.position = body->position;
        if ( prediction.baseline.rootPolyline.size() < REPLAY_PREDICTION_BASELINE_ROOT_POINT_CAPACITY )
        {
            prediction.baseline.rootPolyline.push_back( point );
        }
        else if ( endpointFrame && !prediction.baseline.rootPolyline.empty() )
        {
            prediction.baseline.rootPolyline.back() = point;
        }
    }

    for ( const RunReplayPredictionBodySample& body : firstFrame.bodies )
    {
        if ( prediction.baseline.bodyPoses.size() >= bodyCapacity || body.id.value == 0 )
        {
            break;
        }

        Vector3 restPosition = SkullbonezCore::Math::Vector::ZERO_VECTOR;
        Quaternion restOrientation = IDENTITY_QUATERNION;
        const bool hasRestPose = ReplayPredictionBodyRestingPose( frames,
                                                                  frameCount,
                                                                  body.id,
                                                                  body.modelRow.value,
                                                                  restPosition,
                                                                  restOrientation );
        if ( !hasRestPose )
        {
            const RunReplayPredictionBodySample* horizonBody =
                FindReplayPredictionBodyByIdWithHint( lastFrame, body.id, body.modelRow.value );
            if ( !horizonBody )
            {
                continue;
            }
            // Why: orbital bodies never reach a resting pose. Retain their
            // horizon endpoint for divergence math, but keep hasRestPose false
            // so the renderer does not mislabel it as a grey rest marker.
            restPosition = horizonBody->position;
            restOrientation = horizonBody->orientation;
        }

        ReplayPredictionBaselineBodyPose pose;
        pose.id = body.id;
        pose.modelRow.value = body.modelRow.value;
        pose.hasEntryPose = true;
        pose.hasRestPose = hasRestPose;
        pose.entryPosition = body.position;
        pose.entryOrientation = body.orientation;
        pose.entryOrientation.Normalise();
        pose.restPosition = restPosition;
        pose.restOrientation = restOrientation;
        pose.restOrientation.Normalise();
        prediction.baseline.bodyPoses.push_back( pose );
    }

    prediction.baseline.valid = prediction.baseline.rootPolyline.size() >= 2 || !prediction.baseline.bodyPoses.empty();
    prediction.baseline.comparisonActive = prediction.baseline.valid;
    if ( prediction.baseline.valid && !PublishReplayPredictionBaselineRootTrajectory( prediction ) )
    {
        ClearReplayPredictionBaseline( prediction.baseline );
        return false;
    }
    return prediction.baseline.valid;
}

bool PublishReplayPredictionBaselineRootTrajectory( RunReplayPredictionState& prediction )
{
    const ReplayPredictionBaselineSnapshot& baseline = prediction.baseline;
    if ( !baseline.valid || baseline.rootId.value == 0 || baseline.rootPolyline.size() < 2u )
    {
        return true;
    }

    ReplayTrajectoryRecord* record = BeginReplayTrajectoryRecord(
        prediction.trajectoryStore,
        ReplayTrajectoryKey( baseline.rootId, ReplayTrajectoryLane::BaselineRoot, REPLAY_TRAJECTORY_COMMITTED_BRANCH ),
        0,
        ReplayBodyId{},
        0,
        0,
        false,
        baseline.rootPolyline.size() );
    if ( !record )
    {
        return false;
    }

    for ( const ReplayPredictionBaselineRootPoint& point : baseline.rootPolyline )
    {
        if ( !AppendReplayTrajectoryPoint( prediction.trajectoryStore, *record, point.frameIndex, point.position ) )
        {
            return false;
        }
    }
    return true;
}

// Concept: divergence is a demo-facing separation metric, not physics authority.
// It sums how far matched bodies' resting endpoints moved between the cold
// baseline and the rebuilt prediction.
void UpdateReplayPredictionBaselineDivergence( RunReplayPredictionState& prediction,
                                               const std::vector<RunReplayPredictionFrame>& frames,
                                               std::size_t frameCount )
{
    ReplayPredictionBaselineSnapshot& baseline = prediction.baseline;
    baseline.divergenceValid = false;
    baseline.divergenceUnits = 0.0f;
    frameCount = (std::min)( frameCount, frames.size() );
    if ( !baseline.valid || frameCount < 2 || baseline.bodyPoses.empty() )
    {
        return;
    }

    float divergence = 0.0f;
    int matchedBodies = 0;
    for ( const ReplayPredictionBaselineBodyPose& baselinePose : baseline.bodyPoses )
    {
        if ( baselinePose.id.value == 0 )
        {
            continue;
        }

        Vector3 restPosition = SkullbonezCore::Math::Vector::ZERO_VECTOR;
        Quaternion restOrientation = IDENTITY_QUATERNION;
        if ( baselinePose.hasRestPose )
        {
            if ( !ReplayPredictionBodyRestingPose( frames,
                                                   frameCount,
                                                   baselinePose.id,
                                                   baselinePose.modelRow.value,
                                                   restPosition,
                                                   restOrientation ) )
            {
                continue;
            }
        }
        else
        {
            const RunReplayPredictionBodySample* horizonBody =
                FindReplayPredictionBodyByIdWithHint( frames[frameCount - 1u],
                                                      baselinePose.id,
                                                      baselinePose.modelRow.value );
            if ( !horizonBody )
            {
                continue;
            }
            restPosition = horizonBody->position;
        }

        divergence += VectorMag( restPosition - baselinePose.restPosition );
        ++matchedBodies;
    }

    baseline.divergenceUnits = divergence;
    baseline.divergenceValid = matchedBodies > 0;
}

const ColliderRecord* ReplayColliderRecordForModelIndex( const ColliderStore* colliderStore, int modelIndex );

// Concept: cold baseline drawing deliberately reuses the smooth replay ribbon
// path. It should read as the old future's ghost, never as jaggy debug wire.

// Concept: prediction future-node discovery can replace a motion-inferred child
// with a contact-derived child. These helpers keep that policy at the wrapper
// edge instead of duplicating the contact traversal.
template <typename NodeRange>
bool TryGetReplayFutureDepthInNodes( const NodeRange& nodes,
                                     ReplayBodyId rootId,
                                     ReplayFrameIndex rootFrame,
                                     bool requireRootFrame,
                                     ReplayBodyId id,
                                     ReplayFrameIndex frame,
                                     int& outDepth )
{
    if ( id.value == 0 )
    {
        return false;
    }
    if ( id.value == rootId.value )
    {
        outDepth = 0;
        return !requireRootFrame || frame >= rootFrame;
    }

    for ( const RunReplayPathTraceNode& node : nodes )
    {
        if ( node.id.value == id.value && frame >= node.firstFrame )
        {
            outDepth = node.depth;
            return true;
        }
    }
    return false;
}

template <typename NodeRange> RunReplayPathTraceNode* FindReplayFutureNodeInNodes( NodeRange& nodes, ReplayBodyId id )
{
    for ( RunReplayPathTraceNode& node : nodes )
    {
        if ( node.id.value == id.value )
        {
            return &node;
        }
    }
    return nullptr;
}

void AssignReplayFutureNode( RunReplayPathTraceNode& node,
                             ReplayBodyId parentId,
                             int parentModelIndex,
                             ReplayBodyId id,
                             int modelIndex,
                             ReplayFrameIndex firstFrame,
                             const Vector3& contactPoint,
                             const Vector3& contactNormal,
                             int depth,
                             bool contactDerived )
{
    node.id = id;
    node.parentId = parentId;
    node.modelRow.value = modelIndex;
    node.parentModelRow.value = parentModelIndex;
    node.firstFrame = firstFrame;
    node.contactPoint = contactPoint;
    node.contactNormal = contactNormal;
    node.depth = depth;
    node.contactDerived = contactDerived;
}

// Value payload for one future-node insertion. Vector storage remains the
// operation-specific argument so this record cannot retain cache ownership.
struct ReplayFutureNodeDesc
{
    ReplayBodyId rootId;
    ReplayBodyId parentId;
    int parentModelIndex = -1;
    ReplayBodyId id;
    int modelIndex = -1;
    ReplayFrameIndex firstFrame;
    const Vector3& contactPoint;
    const Vector3& contactNormal;
    int depth = 0;
    bool contactDerived = false;
    bool replaceMotionFallback = false;
};

template <typename NodeContainer>
void AddReplayFutureNodeToNodes( NodeContainer& nodes, const ReplayFutureNodeDesc& desc )
{
    const ReplayBodyId rootId = desc.rootId;
    const ReplayBodyId parentId = desc.parentId;
    const int parentModelIndex = desc.parentModelIndex;
    const ReplayBodyId id = desc.id;
    const int modelIndex = desc.modelIndex;
    const ReplayFrameIndex firstFrame = desc.firstFrame;
    const Vector3& contactPoint = desc.contactPoint;
    const Vector3& contactNormal = desc.contactNormal;
    const int depth = desc.depth;
    const bool contactDerived = desc.contactDerived;
    const bool replaceMotionFallback = desc.replaceMotionFallback;
    if ( id.value == 0 || id.value == rootId.value )
    {
        return;
    }

    if ( RunReplayPathTraceNode* existing = FindReplayFutureNodeInNodes( nodes, id ) )
    {
        if ( replaceMotionFallback && contactDerived && !existing->contactDerived )
        {
            AssignReplayFutureNode( *existing,
                                    parentId,
                                    parentModelIndex,
                                    id,
                                    modelIndex,
                                    firstFrame,
                                    contactPoint,
                                    contactNormal,
                                    depth,
                                    true );
        }
        return;
    }

    if ( nodes.size() >= REPLAY_PATH_MAX_FUTURE_NODES )
    {
        return;
    }

    RunReplayPathTraceNode node;
    AssignReplayFutureNode( node,
                            parentId,
                            parentModelIndex,
                            id,
                            modelIndex,
                            firstFrame,
                            contactPoint,
                            contactNormal,
                            depth,
                            contactDerived );
    nodes.push_back( node );
}

bool ReplayFutureNodeTopologyEquals( const RunReplayPathTraceNode& a, const RunReplayPathTraceNode& b )
{
    return a.id.value == b.id.value && a.parentId.value == b.parentId.value && a.modelRow.value == b.modelRow.value &&
           a.parentModelRow.value == b.parentModelRow.value && a.firstFrame == b.firstFrame && a.depth == b.depth &&
           a.contactDerived == b.contactDerived;
}


bool ReplayFutureNodeTopologyEquals( const std::vector<RunReplayPathTraceNode>& a,
                                     const std::vector<RunReplayPathTraceNode>& b )
{
    if ( a.size() != b.size() )
    {
        return false;
    }
    for ( std::size_t i = 0; i < a.size(); ++i )
    {
        if ( !ReplayFutureNodeTopologyEquals( a[i], b[i] ) )
        {
            return false;
        }
    }
    return true;
}


uint32_t AllocateReplayFutureNodeTopologyVersion( RunReplayPredictionFutureNodeCache& cache )
{
    uint32_t version = cache.nextFutureNodesTopologyVersion;
    ++cache.nextFutureNodesTopologyVersion;
    if ( cache.nextFutureNodesTopologyVersion == 0 )
    {
        cache.nextFutureNodesTopologyVersion = 1;
    }
    if ( version == 0 )
    {
        version = cache.nextFutureNodesTopologyVersion;
        ++cache.nextFutureNodesTopologyVersion;
    }
    return version;
}

template <typename ContactRange,
          typename BodyIdResolver,
          typename DepthResolver,
          typename NodeAdder,
          typename BudgetExpired>
bool BuildReplayFutureNodesFromContacts( const ContactRange& contacts,
                                         ReplayFrameIndex frameIndex,
                                         std::size_t startContactIndex,
                                         const SceneEntityStore* collection,
                                         bool includeRagdollVisuals,
                                         BodyIdResolver bodyIdForModelIndex,
                                         DepthResolver tryGetDepth,
                                         NodeAdder addNode,
                                         BudgetExpired budgetExpired,
                                         std::size_t& outNextContactIndex )
{
    outNextContactIndex = (std::min)( startContactIndex, contacts.size() );
    for ( std::size_t contactIndex = outNextContactIndex; contactIndex < contacts.size(); ++contactIndex )
    {
        // Invariant: callers that slice a frame on budget exhaustion must resume
        // from this contact index before advancing the frame cursor.
        if ( budgetExpired() )
        {
            return false;
        }

        const auto& contact = contacts[contactIndex];
        const bool ragdollA = collection && ReplayModelIndexIsRagdollPart( *collection, contact.bodyA );
        const bool ragdollB = collection && ReplayModelIndexIsRagdollPart( *collection, contact.bodyB );
        const int modelIndexA =
            collection ? ReplayRagdollTorsoModelIndexForPart( *collection, contact.bodyA ) : contact.bodyA;
        const int modelIndexB =
            collection ? ReplayRagdollTorsoModelIndexForPart( *collection, contact.bodyB ) : contact.bodyB;
        const ReplayBodyId idA = bodyIdForModelIndex( modelIndexA );
        const ReplayBodyId idB = bodyIdForModelIndex( modelIndexB );
        int depthA = -1;
        int depthB = -1;
        const bool activeA = tryGetDepth( idA, frameIndex, depthA );
        const bool activeB = tryGetDepth( idB, frameIndex, depthB );
        if ( activeA && !activeB && ( includeRagdollVisuals || !ragdollB ) )
        {
            addNode( idA, modelIndexA, idB, modelIndexB, frameIndex, contact.point, contact.normal, depthA + 1, true );
        }
        else if ( activeB && !activeA && ( includeRagdollVisuals || !ragdollA ) )
        {
            addNode( idB,
                     modelIndexB,
                     idA,
                     modelIndexA,
                     frameIndex,
                     contact.point,
                     contact.normal * -1.0f,
                     depthB + 1,
                     true );
        }
        outNextContactIndex = contactIndex + 1;
    }
    outNextContactIndex = 0;
    return true;
}

// Invariant: path thinning is anchored to solver frame indices, not visitor
// ordinal. Partial scans may resume at different offsets, but the same replay
// tick must always keep or drop the same visual segment.

struct ReplayPathChildDrawState
{
    RunReplayPathTraceNode node;
    bool active = false;
    // Concept: the two-box causal story. Entry is the body's IN-PLACE pose
    // from prediction frame 0 Ã¢â‚¬â€ the wall exactly as the live scene knows it.
    // It is drawn yellow the moment the body visibly moves and never slides.
    // lastMotionFrame times when the grey resting box may pop in.
    bool hasEntryPose = false;
    int entryModelIndex = -1;
    ReplayFrameIndex lastMotionFrame = 0;
    Vector3 entryPosition = SkullbonezCore::Math::Vector::ZERO_VECTOR;
    Quaternion entryOrientation = IDENTITY_QUATERNION;
};

struct ReplayPathChildDrawContext
{
    std::array<ReplayPathChildDrawState, REPLAY_PATH_MAX_FUTURE_NODES> nodes = {};
    std::size_t nodeCount = 0;
};

// Why: downstream replay markers should show the collider's real authored
// shape, not the broadphase radius used for cheap collision culling.
const ColliderRecord* ReplayColliderRecordForModelIndex( const ColliderStore* colliderStore, int modelIndex )
{
    if ( !colliderStore )
    {
        return nullptr;
    }

    // Why: retained prediction markers store historical model-index samples, not
    // live body handles. Use this only for presentation fallback; store-edit
    // paths resolve through PhysicsBodyHandle before reading collider rows.
    const PhysicsColliderHandle colliderHandle = colliderStore->HandleForModelIndex( modelIndex );
    const ColliderRecord* collider = colliderStore->RecordForHandle( colliderHandle );
    if ( !collider || colliderStore->ModelIndexForHandle( colliderHandle ) != modelIndex )
    {
        return nullptr;
    }
    return collider;
}

ReplayPredictionRetainedMarker*
FindOrAddReplayPredictionRetainedMarker( RunReplayPredictionState& prediction, ReplayBodyId id, int modelIndex )
{
    if ( id.value == 0 )
    {
        return nullptr;
    }
    for ( std::size_t i = 0; i < prediction.futureNodeCache.retainedMarkerCount; ++i )
    {
        ReplayPredictionRetainedMarker& marker = prediction.futureNodeCache.retainedMarkers[i];
        if ( marker.id.value == id.value )
        {
            if ( modelIndex >= 0 )
            {
                marker.modelRow.value = modelIndex;
            }
            return &marker;
        }
    }
    if ( prediction.futureNodeCache.retainedMarkerCount >= prediction.futureNodeCache.retainedMarkers.size() )
    {
        return nullptr;
    }

    ReplayPredictionRetainedMarker& marker =
        prediction.futureNodeCache.retainedMarkers[prediction.futureNodeCache.retainedMarkerCount++];
    marker = ReplayPredictionRetainedMarker{};
    marker.id = id;
    marker.modelRow.value = modelIndex;
    return &marker;
}

void RetainReplayPredictionEntryMarker( RunReplayPredictionState& prediction,
                                        ReplayBodyId id,
                                        int modelIndex,
                                        const Vector3& position,
                                        Quaternion orientation )
{
    if ( ReplayPredictionRetainedMarker* marker =
             FindOrAddReplayPredictionRetainedMarker( prediction, id, modelIndex ) )
    {
        marker->hasEntryPose = true;
        marker->entryPosition = position;
        marker->entryOrientation = orientation;
        marker->entryOrientation.Normalise();
    }
}

void RetainReplayPredictionRestMarker( RunReplayPredictionState& prediction,
                                       ReplayBodyId id,
                                       int modelIndex,
                                       const Vector3& position,
                                       Quaternion orientation )
{
    if ( ReplayPredictionRetainedMarker* marker =
             FindOrAddReplayPredictionRetainedMarker( prediction, id, modelIndex ) )
    {
        marker->hasRestPose = true;
        marker->hasHorizonPose = false;
        marker->restPosition = position;
        marker->restOrientation = orientation;
        marker->restOrientation.Normalise();
    }
}

void RetainReplayPredictionHorizonMarker( RunReplayPredictionState& prediction,
                                          ReplayBodyId id,
                                          int modelIndex,
                                          const Vector3& position,
                                          Quaternion orientation )
{
    if ( ReplayPredictionRetainedMarker* marker =
             FindOrAddReplayPredictionRetainedMarker( prediction, id, modelIndex ) )
    {
        if ( marker->hasRestPose )
        {
            return;
        }
        marker->hasHorizonPose = true;
        marker->horizonPosition = position;
        marker->horizonOrientation = orientation;
        marker->horizonOrientation.Normalise();
    }
}


void RetainReplayPredictionEndStateMarkers( RunReplayPredictionState& prediction,
                                            ReplayFrameIndex revealFrame,
                                            const std::vector<RunReplayPredictionFrame>& completeFrames,
                                            std::size_t completeFrameCount )
{
    completeFrameCount = (std::min)( completeFrameCount, completeFrames.size() );
    if ( completeFrameCount < 2 || revealFrame < completeFrames[completeFrameCount - 1].frameIndex )
    {
        return;
    }

    // Why: late in the 200-brick wall prediction, ownership can move from the
    // affected-body fallback into the future-node tree faster than the budgeted
    // line scan can rediscover every brick. The stable end state is cheap to
    // prove from the final and grace frames. Resting bodies get grey boxes;
    // bodies still moving when the event horizon ends get a ghost endpoint.
    for ( std::size_t i = 0; i < prediction.futureNodeCache.retainedMarkerCount; ++i )
    {
        ReplayPredictionRetainedMarker& marker = prediction.futureNodeCache.retainedMarkers[i];
        if ( !marker.hasEntryPose || marker.hasRestPose || marker.hasHorizonPose )
        {
            continue;
        }

        Vector3 restPosition = SkullbonezCore::Math::Vector::ZERO_VECTOR;
        Quaternion restOrientation = IDENTITY_QUATERNION;
        if ( ReplayPredictionBodyRestingPose( completeFrames,
                                              completeFrameCount,
                                              marker.id,
                                              marker.modelRow.value,
                                              restPosition,
                                              restOrientation ) )
        {
            RetainReplayPredictionRestMarker( prediction,
                                              marker.id,
                                              marker.modelRow.value,
                                              restPosition,
                                              restOrientation );
            continue;
        }

        const RunReplayPredictionBodySample* finalBody =
            FindReplayPredictionBodyByIdWithHint( completeFrames[completeFrameCount - 1],
                                                  marker.id,
                                                  marker.modelRow.value );
        if ( !finalBody )
        {
            continue;
        }
        if ( VectorMagSquared( finalBody->position - marker.entryPosition ) <= REPLAY_PATH_MIN_SEGMENT_DISTANCE_SQ &&
             !ReplayPredictionBodyHasVisibleLinearMotion( *finalBody ) )
        {
            continue;
        }

        RetainReplayPredictionHorizonMarker( prediction,
                                             marker.id,
                                             finalBody->modelRow.value,
                                             finalBody->position,
                                             finalBody->orientation );
    }
}

// Concept: causal markers are the two-box story of each affected body.
//
// Yellow is fixed at the body's last still pose before it visibly moved Ã¢â‚¬â€ for
// a wall brick, its perfect-formation pose. Grey pops in ONLY at the body's
// final resting pose, and only when the completed prediction actually ends
// with it at rest; a body still moving at the horizon end gets a travel line
// and nothing else. Neither box ever slides.
void RetainReplayPredictionCausalMarkers( RunReplayPredictionState& prediction,
                                          ReplayPathChildDrawContext& context,
                                          ReplayFrameIndex revealFrame,
                                          const std::vector<RunReplayPredictionFrame>* completeFrames,
                                          std::size_t completeFrameCount )
{
    for ( std::size_t i = 0; i < context.nodeCount; ++i )
    {
        const ReplayPathChildDrawState& drawState = context.nodes[i];
        if ( drawState.hasEntryPose )
        {
            RetainReplayPredictionEntryMarker( prediction,
                                               drawState.node.id,
                                               drawState.entryModelIndex,
                                               drawState.entryPosition,
                                               drawState.entryOrientation );
        }

        // Why: completeFrames is null while the job is still building Ã¢â‚¬â€ a
        // growing prefix has no authoritative ending, so no grey box may
        // exist yet. The reveal timing check keeps the grey pop causal: it
        // appears only after the cursor has watched the body stop.
        if ( !drawState.active || !completeFrames )
        {
            continue;
        }
        if ( revealFrame < drawState.lastMotionFrame + REPLAY_PREDICTION_REST_GRACE_FRAMES )
        {
            continue;
        }
        Vector3 restPosition = SkullbonezCore::Math::Vector::ZERO_VECTOR;
        Quaternion restOrientation = IDENTITY_QUATERNION;
        if ( !ReplayPredictionBodyRestingPose( *completeFrames,
                                               completeFrameCount,
                                               drawState.node.id,
                                               drawState.node.modelRow.value,
                                               restPosition,
                                               restOrientation ) )
        {
            continue;
        }
        RetainReplayPredictionRestMarker( prediction,
                                          drawState.node.id,
                                          drawState.node.modelRow.value,
                                          restPosition,
                                          restOrientation );
    }
}

void BuildReplayPredictionChildMarkerContext( ReplayPathChildDrawContext& context,
                                              const RunReplayPredictionState& prediction,
                                              const std::vector<RunReplayPredictionFrame>& frames,
                                              std::size_t frameCount,
                                              ReplayFrameIndex revealFrame )
{
    frameCount = (std::min)( frameCount, frames.size() );
    context = ReplayPathChildDrawContext{};
    context.nodeCount = (std::min)( prediction.futureNodeCache.futureNodes.size(), REPLAY_PATH_MAX_FUTURE_NODES );
    for ( std::size_t i = 0; i < context.nodeCount; ++i )
    {
        context.nodes[i].node = prediction.futureNodeCache.futureNodes[i];
    }
    if ( frameCount < 2 || context.nodeCount == 0 )
    {
        return;
    }

    for ( std::size_t frameIndex = 0; frameIndex < frameCount; ++frameIndex )
    {
        const RunReplayPredictionFrame& frame = frames[frameIndex];
        if ( frame.frameIndex > revealFrame )
        {
            break;
        }
        for ( std::size_t i = 0; i < context.nodeCount; ++i )
        {
            ReplayPathChildDrawState& drawState = context.nodes[i];
            if ( frame.frameIndex < drawState.node.firstFrame )
            {
                continue;
            }

            const RunReplayPredictionBodySample* body =
                FindReplayPredictionBodyByIdWithHint( frame, drawState.node.id, drawState.node.modelRow.value );
            if ( !body )
            {
                continue;
            }
            if ( !drawState.active )
            {
                if ( !ReplayPredictionBodyHasVisibleLinearMotion( *body ) )
                {
                    continue;
                }
                const RunReplayPredictionBodySample* initialSample =
                    FindReplayPredictionBodyByIdWithHint( frames[0], drawState.node.id, body->modelRow.value );
                drawState.active = true;
                drawState.hasEntryPose = true;
                drawState.entryModelIndex = body->modelRow.value;
                drawState.entryPosition = initialSample ? initialSample->position : body->position;
                drawState.entryOrientation = initialSample ? initialSample->orientation : body->orientation;
                drawState.entryOrientation.Normalise();
                drawState.lastMotionFrame = frame.frameIndex;
                continue;
            }
            if ( ReplayPredictionBodyHasVisibleLinearMotion( *body ) )
            {
                drawState.lastMotionFrame = frame.frameIndex;
            }
        }
    }
}

ReplayFrameIndex ReplayPredictionVisibleRootMotionFrame( const std::vector<RunReplayPredictionFrame>& frames,
                                                         std::size_t frameCount,
                                                         ReplayFrameIndex revealFrame,
                                                         ReplayBodyId rootId,
                                                         int rootModelIndex )
{
    frameCount = (std::min)( frameCount, frames.size() );
    ReplayFrameIndex rootLastMotionFrame = 0;
    for ( std::size_t frameIndex = 0; frameIndex < frameCount; ++frameIndex )
    {
        const RunReplayPredictionFrame& frame = frames[frameIndex];
        if ( frame.frameIndex > revealFrame )
        {
            break;
        }
        const RunReplayPredictionBodySample* body =
            FindReplayPredictionBodyByIdWithHint( frame, rootId, rootModelIndex );
        if ( body && ReplayPredictionBodyHasVisibleLinearMotion( *body ) )
        {
            rootLastMotionFrame = frame.frameIndex;
        }
    }
    return rootLastMotionFrame;
}

void RetainReplayPredictionRootRestMarker( RunReplayPredictionState& prediction,
                                           const std::vector<RunReplayPredictionFrame>& frames,
                                           std::size_t frameCount,
                                           ReplayFrameIndex revealFrame,
                                           ReplayBodyId rootId,
                                           int rootModelIndex,
                                           const ColliderStore& colliderStore )
{
    const ReplayFrameIndex rootLastMotionFrame =
        ReplayPredictionVisibleRootMotionFrame( frames, frameCount, revealFrame, rootId, rootModelIndex );
    if ( revealFrame < rootLastMotionFrame + REPLAY_PREDICTION_REST_GRACE_FRAMES )
    {
        return;
    }

    Vector3 rootRestPosition = SkullbonezCore::Math::Vector::ZERO_VECTOR;
    Quaternion rootRestOrientation = IDENTITY_QUATERNION;
    if ( ReplayPredictionBodyRestingPose( frames,
                                          frameCount,
                                          rootId,
                                          rootModelIndex,
                                          rootRestPosition,
                                          rootRestOrientation ) &&
         ReplayColliderRecordForModelIndex( &colliderStore, rootModelIndex ) )
    {
        RetainReplayPredictionRestMarker( prediction, rootId, rootModelIndex, rootRestPosition, rootRestOrientation );
    }
}


struct ReplayPredictionAffectedBodyTrail
{
    ReplayBodyId id;
    ModelRowHint modelRow;
    std::size_t firstFrameSlot = 0;
    ReplayFrameIndex firstFrame = 0;
    // Concept: same two-box causal story as ReplayPathChildDrawState. Entry is
    // the body's in-place pose from prediction frame 0 (yellow, fixed);
    // lastMotionFrame times when the grey resting box may pop in. The grey
    // pose itself always comes from the completed buffer's final frame.
    ReplayFrameIndex lastMotionFrame = 0;
    Vector3 previous = SkullbonezCore::Math::Vector::ZERO_VECTOR;
    Vector3 entryPosition = SkullbonezCore::Math::Vector::ZERO_VECTOR;
    Quaternion entryOrientation = IDENTITY_QUATERNION;
};


bool ReplayPredictionIdInFutureNodes( const std::vector<RunReplayPathTraceNode>& nodes, ReplayBodyId id )
{
    for ( const RunReplayPathTraceNode& node : nodes )
    {
        if ( node.id.value == id.value )
        {
            return true;
        }
    }
    return false;
}

std::size_t BuildReplayPredictionAffectedBodyTrails(
    const std::vector<RunReplayPredictionFrame>& frames,
    std::size_t frameCount,
    ReplayFrameIndex revealFrame,
    ReplayBodyId rootId,
    int rootModelIndex,
    const std::vector<RunReplayPathTraceNode>& futureNodes,
    const SceneEntityStore& collection,
    std::array<ReplayPredictionAffectedBodyTrail, REPLAY_PATH_MAX_FUTURE_NODES>& trails )
{
    frameCount = (std::min)( frameCount, frames.size() );
    if ( frameCount < 2 || rootId.value == 0 )
    {
        return 0;
    }

    // Concept: affected-body trails are visual evidence, not contact authority.
    //
    // The future-node cache feeds both the cause window and child path renderer.
    // This pass exists only as a visual fallback while that cache has not yet
    // published a body; it skips ids already represented by either contact- or
    // motion-derived nodes.
    std::size_t trailCount = 0;
    const RunReplayPredictionFrame& firstFrame = frames.front();
    for ( const RunReplayPredictionBodySample& initialBody : firstFrame.bodies )
    {
        if ( trailCount >= REPLAY_PATH_MAX_FUTURE_NODES )
        {
            break;
        }
        if ( initialBody.id.value == 0 || initialBody.id.value == rootId.value ||
             initialBody.modelRow.value == rootModelIndex ||
             ReplayPredictionIdInFutureNodes( futureNodes, initialBody.id ) )
        {
            continue;
        }
        if ( ReplayModelIndexIsRagdollPart( collection, initialBody.modelRow.value ) )
        {
            continue;
        }

        for ( std::size_t frameSlot = 1; frameSlot < frameCount; ++frameSlot )
        {
            // Why: a body whose first movement lies past the reveal cursor is
            // not part of the story yet. Skipping it here keeps its trail and
            // outline from pre-spawning ahead of the causal unfold.
            if ( frames[frameSlot].frameIndex > revealFrame )
            {
                break;
            }

            const RunReplayPredictionBodySample* body =
                FindReplayPredictionBodyByIdWithHint( frames[frameSlot], initialBody.id, initialBody.modelRow.value );
            if ( !body )
            {
                continue;
            }
            if ( !ReplayPredictionBodyHasVisibleLinearMotion( *body ) )
            {
                continue;
            }

            // Why: entry is the body's IN-PLACE pose from prediction frame 0 Ã¢â‚¬â€
            // the wall exactly as the live scene knows it. Never a sampled
            // pose from after the impulse arrived.
            ReplayPredictionAffectedBodyTrail& trail = trails[trailCount++];
            trail.id = initialBody.id;
            trail.modelRow.value = body->modelRow.value;
            trail.firstFrameSlot = frameSlot;
            trail.firstFrame = frames[frameSlot].frameIndex;
            trail.lastMotionFrame = frames[frameSlot].frameIndex;
            trail.previous = initialBody.position;
            trail.entryPosition = initialBody.position;
            trail.entryOrientation = initialBody.orientation;
            trail.entryOrientation.Normalise();
            break;
        }
    }

    return trailCount;
}


void RetainReplayPredictionAffectedBodyMarkers( const std::vector<RunReplayPredictionFrame>& frames,
                                                std::size_t frameCount,
                                                RunReplayPredictionState& prediction,
                                                ReplayFrameIndex revealFrame,
                                                bool bufferComplete,
                                                ReplayBodyId rootId,
                                                int rootModelIndex,
                                                const std::vector<RunReplayPathTraceNode>& futureNodes,
                                                const SceneEntityStore& collection,
                                                const ColliderStore& colliderStore )
{
    std::array<ReplayPredictionAffectedBodyTrail, REPLAY_PATH_MAX_FUTURE_NODES> trails = {};
    const std::size_t trailCount = BuildReplayPredictionAffectedBodyTrails( frames,
                                                                            frameCount,
                                                                            revealFrame,
                                                                            rootId,
                                                                            rootModelIndex,
                                                                            futureNodes,
                                                                            collection,
                                                                            trails );
    frameCount = (std::min)( frameCount, frames.size() );
    // Why: marker publication is bounded and independent of render traversal.
    // Once revealed, a causal box stays in the prediction owner's published
    // cache even when the draw quota later degrades line work.
    for ( std::size_t trailIndex = 0; trailIndex < trailCount; ++trailIndex )
    {
        const ReplayPredictionAffectedBodyTrail& trail = trails[trailIndex];
        if ( !ReplayColliderRecordForModelIndex( &colliderStore, trail.modelRow.value ) )
        {
            continue;
        }

        RetainReplayPredictionEntryMarker( prediction,
                                           trail.id,
                                           trail.modelRow.value,
                                           trail.entryPosition,
                                           trail.entryOrientation );
        // Why: grey exists only for stories that end at rest inside the
        // completed horizon Ã¢â‚¬â€ see RetainReplayPredictionCausalMarkers.
        if ( !bufferComplete || revealFrame < trail.lastMotionFrame + REPLAY_PREDICTION_REST_GRACE_FRAMES )
        {
            continue;
        }
        Vector3 restPosition = SkullbonezCore::Math::Vector::ZERO_VECTOR;
        Quaternion restOrientation = IDENTITY_QUATERNION;
        if ( ReplayPredictionBodyRestingPose( frames,
                                              frameCount,
                                              trail.id,
                                              trail.modelRow.value,
                                              restPosition,
                                              restOrientation ) )
        {
            RetainReplayPredictionRestMarker( prediction,
                                              trail.id,
                                              trail.modelRow.value,
                                              restPosition,
                                              restOrientation );
        }
    }
}

struct ReplayPredictionFutureContext
{
    RunReplayPredictionState* prediction = nullptr;
    std::vector<RunReplayPathTraceNode>* nodes = nullptr;
    const SceneEntityStore* collection = nullptr;
    ReplayBodyId rootId;
    bool includeRagdollVisuals = true;
};

bool TryGetReplayPredictionFutureDepth( const ReplayPredictionFutureContext& context,
                                        ReplayBodyId id,
                                        ReplayFrameIndex frame,
                                        int& outDepth )
{
    const std::vector<RunReplayPathTraceNode>& nodes =
        context.nodes ? *context.nodes : context.prediction->futureNodeCache.futureNodeBuildScratch;
    return TryGetReplayFutureDepthInNodes( nodes, context.rootId, 0, false, id, frame, outDepth );
}

void AddReplayPredictionFutureNode( ReplayPredictionFutureContext& context,
                                    ReplayBodyId parentId,
                                    int parentModelIndex,
                                    ReplayBodyId id,
                                    int modelIndex,
                                    ReplayFrameIndex firstFrame,
                                    const Vector3& contactPoint,
                                    const Vector3& contactNormal,
                                    int depth,
                                    bool contactDerived )
{
    if ( id.value == 0 || id.value == context.rootId.value || !context.nodes )
    {
        return;
    }

    AddReplayFutureNodeToNodes( *context.nodes,
                                ReplayFutureNodeDesc{ .rootId = context.rootId,
                                                      .parentId = parentId,
                                                      .parentModelIndex = parentModelIndex,
                                                      .id = id,
                                                      .modelIndex = modelIndex,
                                                      .firstFrame = firstFrame,
                                                      .contactPoint = contactPoint,
                                                      .contactNormal = contactNormal,
                                                      .depth = depth,
                                                      .contactDerived = contactDerived,
                                                      .replaceMotionFallback = true } );
}

bool BuildReplayPredictionFutureNodes( const RunReplayPredictionFrame& frame,
                                       ReplayPredictionFutureContext& context,
                                       std::size_t startContactIndex,
                                       const std::chrono::steady_clock::time_point& budgetStart,
                                       double budgetMilliseconds,
                                       std::size_t& outNextContactIndex )
{
    return BuildReplayFutureNodesFromContacts(
        frame.debugContacts,
        frame.frameIndex,
        startContactIndex,
        context.collection,
        context.includeRagdollVisuals,
        [&]( int modelIndex ) { return ReplayPredictionBodyIdForModelIndex( frame, modelIndex ); },
        [&]( ReplayBodyId id, ReplayFrameIndex frameIndex, int& outDepth )
        { return TryGetReplayPredictionFutureDepth( context, id, frameIndex, outDepth ); },
        [&]( ReplayBodyId parentId,
             int parentModelIndex,
             ReplayBodyId id,
             int modelIndex,
             ReplayFrameIndex firstFrame,
             const Vector3& contactPoint,
             const Vector3& contactNormal,
             int depth,
             bool contactDerived )
        {
            AddReplayPredictionFutureNode( context,
                                           parentId,
                                           parentModelIndex,
                                           id,
                                           modelIndex,
                                           firstFrame,
                                           contactPoint,
                                           contactNormal,
                                           depth,
                                           contactDerived );
        },
        [&]() { return ReplayPredictionBudgetExpired( budgetStart, budgetMilliseconds ); },
        outNextContactIndex );
}

bool ReplayPredictionBodyReachedActivationDisplacement( const RunReplayPredictionBodySample& initialBody,
                                                        const RunReplayPredictionBodySample& body,
                                                        Vector3& previousPosition,
                                                        float& accumulatedDisplacement,
                                                        Vector3& outActivationDelta )
{
    const Vector3 frameDelta = body.position - previousPosition;
    previousPosition = body.position;
    accumulatedDisplacement += VectorMag( frameDelta );
    outActivationDelta = body.position - initialBody.position;

    return accumulatedDisplacement >= REPLAY_PREDICTION_CHILD_ACTIVATION_DISTANCE ||
           VectorMagSquared( outActivationDelta ) >= REPLAY_PREDICTION_CHILD_ACTIVATION_DISTANCE_SQ;
}

bool BuildReplayPredictionAffectedFutureNodes( const std::vector<RunReplayPredictionFrame>& frames,
                                               std::size_t frameCount,
                                               ReplayPredictionFutureContext& context,
                                               const std::chrono::steady_clock::time_point& budgetStart,
                                               double budgetMilliseconds )
{
    frameCount = (std::min)( frameCount, frames.size() );
    if ( frameCount < 2 || context.rootId.value == 0 || !context.nodes )
    {
        return true;
    }

    const RunReplayPredictionFrame& firstFrame = frames.front();
    const RunReplayPredictionBodySample* rootBody = FindReplayPredictionBodyById( firstFrame, context.rootId );
    const int rootModelIndex = rootBody ? rootBody->modelRow.value : -1;

    // Concept: contact-derived nodes own the authoritative firstFrame whenever
    // the solver captured a contact tick. This sparse-contact fallback waits
    // for measured displacement from the first prediction sample instead of a
    // one-frame speed spike, so slow-pushed bodies join on the tick they
    // actually begin to move.
    for ( const RunReplayPredictionBodySample& initialBody : firstFrame.bodies )
    {
        if ( ReplayPredictionBudgetExpired( budgetStart, budgetMilliseconds ) )
        {
            return false;
        }
        if ( context.nodes->size() >= REPLAY_PATH_MAX_FUTURE_NODES )
        {
            return true;
        }
        if ( initialBody.id.value == 0 || initialBody.id.value == context.rootId.value ||
             ( rootModelIndex >= 0 && initialBody.modelRow.value == rootModelIndex ) )
        {
            continue;
        }
        if ( context.collection && ReplayModelIndexIsRagdollPart( *context.collection, initialBody.modelRow.value ) )
        {
            continue;
        }

        Vector3 previousPosition = initialBody.position;
        float accumulatedDisplacement = 0.0f;
        for ( std::size_t frameSlot = 1; frameSlot < frameCount; ++frameSlot )
        {
            if ( ReplayPredictionBudgetExpired( budgetStart, budgetMilliseconds ) )
            {
                return false;
            }

            const RunReplayPredictionBodySample* body =
                FindReplayPredictionBodyByIdWithHint( frames[frameSlot], initialBody.id, initialBody.modelRow.value );
            if ( !body )
            {
                continue;
            }

            Vector3 activationDelta;
            if ( !ReplayPredictionBodyReachedActivationDisplacement( initialBody,
                                                                     *body,
                                                                     previousPosition,
                                                                     accumulatedDisplacement,
                                                                     activationDelta ) )
            {
                continue;
            }

            AddReplayPredictionFutureNode(
                context,
                context.rootId,
                rootModelIndex,
                initialBody.id,
                body->modelRow.value,
                frames[frameSlot].frameIndex,
                body->position,
                ReplayNormalizeOr( activationDelta,
                                   ReplayNormalizeOr( body->linearVelocity, Vector3( 0.0f, 1.0f, 0.0f ) ) ),
                1,
                false );
            break;
        }
    }
    return true;
}

void UpdateReplayPredictionFutureNodeCache( RunReplayPredictionState& prediction,
                                            const std::vector<RunReplayPredictionFrame>& frames,
                                            std::size_t frameCount,
                                            bool usingBuildFrames,
                                            const SceneEntityStore& collection,
                                            ReplayBodyId rootId,
                                            const std::chrono::steady_clock::time_point& budgetStart,
                                            double budgetMilliseconds )
{
    // Invariant: frameCount is the populated prefix of frames. buildFrames is
    // pre-sized for the whole prediction horizon, so using frames.size() while
    // building would scan empty rows and mark the future-node cache complete
    // before contacts have been captured.
    frameCount = (std::min)( frameCount, frames.size() );
    const bool completingBuildFrames = !usingBuildFrames &&
                                       prediction.futureNodeCache.futureNodesBuiltFromBuildFrames &&
                                       prediction.futureNodeCache.futureNodesBuiltFrameCount <= frameCount;
    const bool sourceMismatch =
        prediction.futureNodeCache.futureNodesBuiltFromBuildFrames != usingBuildFrames && !completingBuildFrames;
    // Invariant: these inputs define the meaning of the cached tree. Any change
    // means old future nodes may point at the wrong root or include the wrong
    // ragdoll aggregation policy.
    const bool cacheMismatch =
        !prediction.futureNodeCache.futureNodesCacheValid ||
        prediction.futureNodeCache.futureNodesBuiltTargetId.value != rootId.value ||
        prediction.futureNodeCache.futureNodesBuiltRagdollVisuals != prediction.ragdollVisualsEnabled ||
        sourceMismatch || prediction.futureNodeCache.futureNodesBuiltFrameCount > frameCount;
    if ( cacheMismatch )
    {
        ClearReplayPredictionFutureNodeCache( prediction );
        prediction.futureNodeCache.futureNodesBuiltTargetId = rootId;
        prediction.futureNodeCache.futureNodesBuiltRagdollVisuals = prediction.ragdollVisualsEnabled;
        prediction.futureNodeCache.futureNodesBuiltFromBuildFrames = usingBuildFrames;
        prediction.futureNodeCache.futureNodesCacheValid = rootId.value != 0;
    }
    else if ( completingBuildFrames )
    {
        prediction.futureNodeCache.futureNodesBuiltFromBuildFrames = false;
    }

    if ( rootId.value == 0 || frameCount == 0 || !prediction.futureNodeCache.futureNodesCacheValid )
    {
        return;
    }

    auto publishScratch = [&]()
    {
        // Why: the renderer reads futureNodes only after this builder returns.
        // Copying the scratch prefix here lets cause/effect paths grow over
        // frames without exposing a vector while it is being mutated.
        const bool topologyChanged =
            !ReplayFutureNodeTopologyEquals( prediction.futureNodeCache.futureNodes,
                                             prediction.futureNodeCache.futureNodeBuildScratch );
        prediction.futureNodeCache.futureNodes = prediction.futureNodeCache.futureNodeBuildScratch;
        if ( topologyChanged )
        {
            prediction.futureNodeCache.futureNodesTopologyVersion =
                AllocateReplayFutureNodeTopologyVersion( prediction.futureNodeCache );
        }
    };

    if ( prediction.futureNodeCache.futureNodeBuildScratch.size() >= REPLAY_PATH_MAX_FUTURE_NODES )
    {
        prediction.futureNodeCache.futureNodesBuiltFrameCount = frameCount;
        prediction.futureNodeCache.futureNodesBuiltContactIndex = 0;
        publishScratch();
        return;
    }

    ReplayPredictionFutureContext futureContext;
    futureContext.prediction = &prediction;
    futureContext.nodes = &prediction.futureNodeCache.futureNodeBuildScratch;
    futureContext.collection = &collection;
    futureContext.rootId = rootId;
    futureContext.includeRagdollVisuals = prediction.ragdollVisualsEnabled;

    while ( prediction.futureNodeCache.futureNodesBuiltFrameCount < frameCount )
    {
        const std::size_t frameIndex = prediction.futureNodeCache.futureNodesBuiltFrameCount;
        std::size_t nextContactIndex = prediction.futureNodeCache.futureNodesBuiltContactIndex;
        if ( !BuildReplayPredictionFutureNodes( frames[frameIndex],
                                                futureContext,
                                                prediction.futureNodeCache.futureNodesBuiltContactIndex,
                                                budgetStart,
                                                budgetMilliseconds,
                                                nextContactIndex ) )
        {
            prediction.futureNodeCache.futureNodesBuiltContactIndex = nextContactIndex;
            publishScratch();
            return;
        }
        prediction.futureNodeCache.futureNodesBuiltContactIndex = 0;
        ++prediction.futureNodeCache.futureNodesBuiltFrameCount;

        if ( prediction.futureNodeCache.futureNodeBuildScratch.size() >= REPLAY_PATH_MAX_FUTURE_NODES )
        {
            prediction.futureNodeCache.futureNodesBuiltFrameCount = frameCount;
            prediction.futureNodeCache.futureNodesBuiltContactIndex = 0;
            break;
        }

        if ( ReplayPredictionBudgetExpired( budgetStart, budgetMilliseconds ) )
        {
            publishScratch();
            return;
        }
    }

    if ( prediction.futureNodeCache.futureNodeBuildScratch.size() < REPLAY_PATH_MAX_FUTURE_NODES &&
         !BuildReplayPredictionAffectedFutureNodes( frames,
                                                    frameCount,
                                                    futureContext,
                                                    budgetStart,
                                                    budgetMilliseconds ) )
    {
        publishScratch();
        return;
    }

    if ( prediction.futureNodeCache.futureNodeBuildScratch.size() >= REPLAY_PATH_MAX_FUTURE_NODES )
    {
        // Invariant: once the fixed topology cap is saturated, later frames
        // cannot publish additional nodes. Mark the cache complete for the
        // visible prefix so reports do not encode the frame-budget slice that
        // happened to discover the final retained node.
        prediction.futureNodeCache.futureNodesBuiltFrameCount = frameCount;
        prediction.futureNodeCache.futureNodesBuiltContactIndex = 0;
    }

    publishScratch();
}

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
        backup.id.value = body.replayBodyId;
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
             bodyRecord->replayBodyId != backup.id.value )
        {
            return false;
        }
        if ( !physicsEngine.RestoreReplayBodyState( bodyHandle,
                                                    backup.id.value,
                                                    backup.fixed,
                                                    backup.position,
                                                    backup.orientation,
                                                    backup.linearVelocity,
                                                    backup.angularVelocity,
                                                    backup.mass,
                                                    backup.inverseMass,
                                                    backup.rotationalInertia,
                                                    backup.inverseRotationalInertia ) )
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
        body.id.value = source.replayBodyId;
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
    const std::vector<PhysicsDebugContact>& debugContacts =
        SkullbonezCore::Physics::PhysicsEngine::ReadDebugContacts( physicsEngine );
    if ( debugContacts.size() > frame.debugContacts.capacity() )
    {
        // Why: debug contacts feed the optional future-impact tree; the root
        // trajectory line only needs body samples. If a dense contact frame asks
        // for more replay scratch, batch the reserve across every prediction
        // frame so the byte cap covers the whole debug-contact payload set. If
        // the replay reserve refuses, keep the frame and drop contacts rather
        // than cancelling prediction.
        const std::size_t requestedDebugContactCapacity =
            ReplayPredictionNextDebugContactCapacity( frame.debugContacts.capacity(), debugContacts.size() );
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
    prediction.build.workerFailed.store( true, std::memory_order_release );
}

void RunReplayPredictionWorkerRange( RunReplayPredictionState& prediction,
                                     SkullbonezCore::Core::Profiler* profiler,
                                     const SkullbonezCore::Core::EngineConfig& config,
                                     SkullbonezCore::Threading::WorkerPool& workerPool,
                                     int modelCount,
                                     int beginTickIndex,
                                     int endTickIndex )
{
    if ( prediction.build.workerFailed.load( std::memory_order_acquire ) ||
         !prediction.simulation.predictionEngineReady || !prediction.simulation.predictionEngine )
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

        // Hazard: worker slices hold only replay-owned values: the private
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
        const double elapsedMs =
            std::chrono::duration<double, std::milli>( std::chrono::steady_clock::now() - probeStart ).count();
        prediction.simulation.probeElapsedMs += elapsedMs;
        prediction.simulation.probeTicksCompleted += completedTicks;
        if ( prediction.simulation.probeTicksCompleted >= config.replayPrediction.probeTicks &&
             prediction.simulation.probeElapsedMs > 0.0 )
        {
            const double ticksPerMs =
                static_cast<double>( prediction.simulation.probeTicksCompleted ) / prediction.simulation.probeElapsedMs;
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

void ReplayPredictionWorkerOperation::operator()( int beginTickIndex, int endTickIndex ) const
{
    // Lifetime: CancelPredictionJob waits for the enclosing AmortizedTask before
    // any of these replay-owned borrows can be cleared or replaced.
    if ( prediction && config && workerPool )
    {
        prediction->RunWorkerRange( *config, *workerPool, modelCount, beginTickIndex, endTickIndex );
    }
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
    if ( prediction.build.workerFailed.load( std::memory_order_acquire ) )
    {
        const bool preserveCommittedFuture = prediction.simulation.frames.size() >= 2u;
        predictionOwner.CancelJob( !preserveCommittedFuture );
        prediction.build.dirty = true;
        return false;
    }

    if ( !prediction.build.workerTask || !prediction.build.workerTask->IsComplete() ||
         prediction.build.workerTask->IsInFlight() )
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
    const bool solverWasOldLiveEdge =
        !hadCommittedPredictionFrames && ReplayAtPresentTrackPosition( solverTrackPosition, 1.0f );
    const bool scrubberWasPinnedToPresent =
        !historicalSamplePaused || ReplayAtPresentTrackPosition( solverTrackPosition, solverPresentTrackPosition ) ||
        solverWasOldLiveEdge;

    prediction.build.workerTask.reset();
    prediction.build.building = false;
    prediction.build.complete = true;
    prediction.build.lastBuildWallMs =
        std::chrono::duration<double, std::milli>( std::chrono::steady_clock::now() - prediction.build.jobStart )
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

// Lifetime: the desc itself is synchronous. Begin copies predictionOwner,
// config, and workerPool pointers into ReplayPredictionWorkerOperation; those
// owners must outlive the task until cancellation waits for in-flight work.
// Every other reference is consumed before BeginReplayPredictionJob returns.
struct ReplayPredictionJobDesc
{
    ReplayPrediction& predictionOwner;
    RunReplayPredictionState& prediction;
    PhysicsEngine& physicsEngine;
    const Gameplay::TornadoGameplay& tornadoGameplay;
    const SceneEntityStore& entities;
    const SkullbonezCore::Core::EngineConfig& config;
    const SkullbonezCore::Physics::PhysicsWorldForces& worldForces;
    SkullbonezCore::Threading::WorkerPool& workerPool;
    bool scenePhysics = false;
    double fallbackSourceSimulationSeconds = 0.0;
    double simulationTotalSeconds = 0.0;
    const ReplaySolverFrameSample* latestSolverSample = nullptr;
    ReplayBodyId requestedTargetId;
    ModelRowHint requestedTargetModelRow;
    bool targetAvailable = false;
    ReplayFrameIndex sourceFrameIndex;
    uint64_t sourceSolverHash = 0;
    const std::chrono::steady_clock::time_point& budgetStart;
    double budgetMilliseconds = 0.0;
    ReplayPredictionUpdateResult& result;
};

bool BeginReplayPredictionJob( const ReplayPredictionJobDesc& desc )
{
    ReplayPrediction& predictionOwner = desc.predictionOwner;
    RunReplayPredictionState& prediction = desc.prediction;
    PhysicsEngine& physicsEngine = desc.physicsEngine;
    const Gameplay::TornadoGameplay& tornadoGameplay = desc.tornadoGameplay;
    const SceneEntityStore& entities = desc.entities;
    const SkullbonezCore::Core::EngineConfig& config = desc.config;
    const SkullbonezCore::Physics::PhysicsWorldForces& worldForces = desc.worldForces;
    SkullbonezCore::Threading::WorkerPool& workerPool = desc.workerPool;
    const bool scenePhysics = desc.scenePhysics;
    const double fallbackSourceSimulationSeconds = desc.fallbackSourceSimulationSeconds;
    const double simulationTotalSeconds = desc.simulationTotalSeconds;
    const ReplaySolverFrameSample* latestSolverSample = desc.latestSolverSample;
    const ReplayBodyId requestedTargetId = desc.requestedTargetId;
    const ModelRowHint requestedTargetModelRow = desc.requestedTargetModelRow;
    const bool targetAvailable = desc.targetAvailable;
    const ReplayFrameIndex sourceFrameIndex = desc.sourceFrameIndex;
    const uint64_t sourceSolverHash = desc.sourceSolverHash;
    const std::chrono::steady_clock::time_point& budgetStart = desc.budgetStart;
    const double budgetMilliseconds = desc.budgetMilliseconds;
    ReplayPredictionUpdateResult& result = desc.result;
    PROFILE_SCOPED( predictionOwner.ProfilerBorrow(), "Frame/Replay/Prediction/BeginJob" );
    if ( !predictionOwner.GenerationPermitted() )
    {
        // Invariant: artifact verification is load-only. Returning before any
        // snapshot, reserve, worker, or trajectory mutation makes a second
        // visual prediction impossible in that process.
        prediction.build.dirty = false;
        return false;
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
        return false;
    }

    const ReplayFrameIndex previousSourceFrameIndex = prediction.simulation.sourceFrameIndex;
    const uint64_t previousSourceSolverHash = prediction.simulation.sourceSolverHash;
    const bool preserveCommittedFuture = prediction.enabled && scenePhysics && requestedTargetId.value != 0 &&
                                         prediction.simulation.targetId.value == requestedTargetId.value &&
                                         prediction.simulation.frames.size() >= 2u;
    const std::size_t buildPresentationFrameCount =
        preserveCommittedFuture ? ReplayPredictionBuildPresentationFrameCountForRefresh( prediction, requestedTargetId )
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
        return false;
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
            return false;
        }
        if ( !TryResolveReplayBodyModelIndex( liveBodyStore, requestedTargetId, targetHint, modelCount, targetIndex ) )
        {
            result.repairedTargetModelRow = targetHint;
            result.targetModelRowRepaired = true;
            return false;
        }
        prediction.simulation.targetModelRow.value = targetIndex;
        result.repairedTargetModelRow = targetHint;
        result.targetModelRowRepaired = true;
    }

    prediction.simulation.horizonSeconds = std::clamp( prediction.simulation.horizonSeconds,
                                                       ReplayOverlay::REPLAY_PREDICTION_MIN_SECONDS,
                                                       ReplayOverlay::REPLAY_PREDICTION_MAX_SECONDS );
    const int predictionTicks =
        (std::max)( 1, static_cast<int>( std::ceil( prediction.simulation.horizonSeconds / PHYSICS_FIXED_DT ) ) );
    prediction.build.targetTickCount = predictionTicks;
    prediction.build.nextTick = 1;
    const std::size_t buildFrameCapacity = static_cast<std::size_t>( predictionTicks + 1 );
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
    if ( !PrepareReplayPredictionTrajectoryBuild( prediction, prediction.simulation.targetId, buildFrameCapacity ) )
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
        prediction.build.workerTask = std::make_unique<ReplayPredictionAmortizedTask>(
            prediction.build.targetTickCount,
            REPLAY_PREDICTION_TICKS_PER_WORKER_SUBMIT,
            ReplayPredictionWorkerOperation{ &predictionOwner, &config, &workerPool, modelCount } );
        prediction.build.workerTask->SetBudget( REPLAY_PREDICTION_TICKS_PER_WORKER_SUBMIT );
    }
    prediction.build.building = true;
    ++prediction.build.generationBeginCount;

    return !prediction.build.buildFrames.empty();
}


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
         !prediction.build.workerTask )
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
        prediction.build.buildMode =
            ChooseReplayPredictionBuildMode( measuredTicksPerMs,
                                             (std::max)( 0, prediction.build.targetTickCount - completedTicks ),
                                             prediction.build.instantBudgetMs );
    }

    // Why: the frame loop still submits once per pass. Instant mode expands only
    // the worker budget; main-thread begin, tree, and draw budgets stay bounded.
    if ( prediction.build.buildMode == ReplayPredictionBuildMode::Instant )
    {
        PROFILE_SCOPED( predictionOwner.ProfilerBorrow(), "Frame/Replay/Prediction/Slice/Instant" );
        prediction.build.workerTask->SetBudget( prediction.build.targetTickCount );
    }
    else if ( prediction.build.buildMode == ReplayPredictionBuildMode::Undecided )
    {
        PROFILE_SCOPED( predictionOwner.ProfilerBorrow(), "Frame/Replay/Prediction/Slice/Probe" );
        prediction.build.workerTask->SetBudget( prediction.build.probeTickBudget );
    }
    else
    {
        PROFILE_SCOPED( predictionOwner.ProfilerBorrow(), "Frame/Replay/Prediction/Slice/Amortized" );
        prediction.build.workerTask->SetBudget( REPLAY_PREDICTION_TICKS_PER_WORKER_SUBMIT );
    }
    prediction.build.workerTask->SubmitTick( workerPool );

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

    if ( prediction.build.workerFailed.load( std::memory_order_acquire ) )
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


void RebuildReplayPredictionCommittedTreeAfterWorkerCompletion( RunReplayPredictionState& prediction,
                                                                const SceneEntityStore& modelCollection,
                                                                ReplayBodyId rootId )
{
    if ( rootId.value == 0 || prediction.simulation.frames.size() < 2u )
    {
        return;
    }

    // Why: the worker publishes physics frames; the frame thread owns topology.
    // Build the committed child tree once from the full finished buffer so
    // automation and draw records do not depend on how many budgeted render
    // passes ran before the worker completed.
    ClearReplayPredictionFutureNodeCache( prediction );
    const auto rebuildStart = std::chrono::steady_clock::now();
    UpdateReplayPredictionFutureNodeCache( prediction,
                                           prediction.simulation.frames,
                                           prediction.simulation.frames.size(),
                                           false,
                                           modelCollection,
                                           rootId,
                                           rebuildStart,
                                           0.0 );
    UpdateReplayPredictionTrajectoryStore( prediction,
                                           prediction.simulation.frames,
                                           prediction.simulation.frames.size(),
                                           false,
                                           rootId );
}


void PrepareReplayPredictionOverlay( RunReplayPredictionState& prediction,
                                     const SceneEntityStore& modelCollection,
                                     const ColliderStore& colliderStore,
                                     ReplayBodyId targetId,
                                     ModelRowHint targetModelRow,
                                     bool targetAvailable,
                                     double budgetMilliseconds,
                                     ReplayPredictionUpdateResult& result )
{
    const bool usingBuildFrames = prediction.BuildPrefixShouldBePresented();
    const std::vector<RunReplayPredictionFrame>& activePredictionFrames =
        usingBuildFrames ? prediction.build.buildFrames : prediction.simulation.frames;
    const std::size_t activePredictionFrameCount =
        usingBuildFrames ? prediction.PublishedBuildFrameCount() : activePredictionFrames.size();
    if ( activePredictionFrameCount < 2 )
    {
        return;
    }

    // Invariant: reveal advancement and derived-cache publication happen
    // before rendering. The overlay receives one immutable visible prefix and
    // cannot change which causal evidence later passes observe in this frame.
    const ReplayPredictionDrawFrameWindow drawWindow =
        PrepareReplayPredictionDrawFrameWindow( prediction, activePredictionFrames, activePredictionFrameCount );
    const bool bufferComplete = !usingBuildFrames;
    if ( !targetAvailable || targetId.value == 0 )
    {
        return;
    }

    if ( bufferComplete )
    {
        RetainReplayPredictionRootRestMarker( prediction,
                                              activePredictionFrames,
                                              activePredictionFrameCount,
                                              drawWindow.revealFrame,
                                              targetId,
                                              targetModelRow.value,
                                              colliderStore );
    }

    const auto buildBudgetStart = std::chrono::steady_clock::now();
    if ( prediction.enabled )
    {
        UpdateReplayPredictionFutureNodeCache( prediction,
                                               activePredictionFrames,
                                               activePredictionFrameCount,
                                               usingBuildFrames,
                                               modelCollection,
                                               targetId,
                                               buildBudgetStart,
                                               budgetMilliseconds );
        UpdateReplayPredictionTrajectoryStore( prediction,
                                               activePredictionFrames,
                                               activePredictionFrameCount,
                                               usingBuildFrames,
                                               targetId );
        (void)ReplayPredictionBudgetExpiredForPass(
            result,
            SkullbonezCore::Core::MainMemoryReplayBudgetPass::PredictionBuildTree,
            buildBudgetStart,
            budgetMilliseconds );
    }

    const bool futureTreeReady =
        ReplayPredictionFutureTreeReadyForDraw( prediction, targetId, usingBuildFrames, activePredictionFrameCount );
    if ( futureTreeReady )
    {
        ReplayPathChildDrawContext childDraw;
        BuildReplayPredictionChildMarkerContext( childDraw,
                                                 prediction,
                                                 activePredictionFrames,
                                                 activePredictionFrameCount,
                                                 drawWindow.revealFrame );
        RetainReplayPredictionCausalMarkers( prediction,
                                             childDraw,
                                             drawWindow.revealFrame,
                                             bufferComplete ? &activePredictionFrames : nullptr,
                                             bufferComplete ? activePredictionFrameCount : 0 );
    }

    RetainReplayPredictionAffectedBodyMarkers( activePredictionFrames,
                                               activePredictionFrameCount,
                                               prediction,
                                               drawWindow.revealFrame,
                                               bufferComplete,
                                               targetId,
                                               targetModelRow.value,
                                               prediction.futureNodeCache.futureNodes,
                                               modelCollection,
                                               colliderStore );
    if ( bufferComplete )
    {
        RetainReplayPredictionEndStateMarkers( prediction,
                                               drawWindow.revealFrame,
                                               activePredictionFrames,
                                               activePredictionFrameCount );
    }
}


void UpdateReplayPrediction( ReplayPrediction& predictionOwner,
                             RunReplayPredictionState& prediction,
                             PhysicsEngine& physicsEngine,
                             const Gameplay::TornadoGameplay& tornadoGameplay,
                             const SceneEntityStore& entities,
                             const SkullbonezCore::Core::EngineConfig& config,
                             const SkullbonezCore::Physics::PhysicsWorldForces& worldForces,
                             SkullbonezCore::Threading::WorkerPool& workerPool,
                             const ReplaySolverFrameSample* latestSolverSample,
                             ReplayBodyId targetId,
                             ModelRowHint targetModelRow,
                             bool targetAvailable,
                             bool liveAdvanceHeld,
                             bool historicalSamplePaused,
                             float solverTrackPosition,
                             float solverPresentTrackPosition,
                             bool scenePhysics,
                             double fallbackSourceSimulationSeconds,
                             double simulationTotalSeconds,
                             const std::chrono::steady_clock::time_point& budgetStart,
                             double budgetMilliseconds,
                             ReplayPredictionUpdateResult& result )
{
    PROFILE_SCOPED( predictionOwner.ProfilerBorrow(), "Frame/Replay/Prediction/Update" );
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
        return;
    }
    if ( !predictionOwner.GenerationPermitted() )
    {
        // Probe assertion lane: the archive may remain visually enabled, but
        // this branch draws only restored values and never reaches a snapshot,
        // reserve, worker, or future-simulation path.
        prediction.build.dirty = false;
        prediction.build.pendingLatestRestart = false;
        // Invariant: EnterOfflinePredictionVerification already joined and
        // retired the worker. Cancelling here would invalidate the restored
        // complete/build and trajectory state before the CPU projection reads
        // it, producing a different packet without starting new simulation.
        return;
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
    const ReplayPredictionCoalescerAction coalescerAction =
        ChooseReplayPredictionCoalescerAction( prediction.build.dirty,
                                               prediction.build.building,
                                               prediction.build.buildMode,
                                               prediction.build.pendingLatestRestart );
    if ( coalescerAction == ReplayPredictionCoalescerAction::Supersede )
    {
        ++prediction.build.supersededRestartCount;
        prediction.build.pendingLatestRestart = true;
        prediction.build.dirty = false;
    }
    const bool automaticRefreshRequested =
        allowAutomaticRefresh && !prediction.build.building && sourceChanged && refreshDue;
    const bool beginRequested = coalescerAction == ReplayPredictionCoalescerAction::Begin ||
                                coalescerAction == ReplayPredictionCoalescerAction::CancelAndBegin ||
                                automaticRefreshRequested;
    if ( beginRequested )
    {
        if ( ReplayPredictionBudgetExpiredForPass( result,
                                                   SkullbonezCore::Core::MainMemoryReplayBudgetPass::PredictionBegin,
                                                   budgetStart,
                                                   budgetMilliseconds ) )
        {
            return;
        }
        if ( prediction.build.dirty )
        {
            ++result
                  .rebuildCauses[static_cast<std::size_t>( SkullbonezCore::Core::MainMemoryReplayRebuildCause::Dirty )];
        }
        else
        {
            ++result.rebuildCauses[static_cast<std::size_t>(
                SkullbonezCore::Core::MainMemoryReplayRebuildCause::AutomaticRefresh )];
        }
        const bool wasDirty = prediction.build.dirty;
        const bool wasPendingLatestRestart = prediction.build.pendingLatestRestart;
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
        const bool began = BeginReplayPredictionJob(
            ReplayPredictionJobDesc{ .predictionOwner = predictionOwner,
                                     .prediction = prediction,
                                     .physicsEngine = physicsEngine,
                                     .tornadoGameplay = tornadoGameplay,
                                     .entities = entities,
                                     .config = config,
                                     .worldForces = worldForces,
                                     .workerPool = workerPool,
                                     .scenePhysics = scenePhysics,
                                     .fallbackSourceSimulationSeconds = fallbackSourceSimulationSeconds,
                                     .simulationTotalSeconds = simulationTotalSeconds,
                                     .latestSolverSample = latestSolverSample,
                                     .requestedTargetId = targetId,
                                     .requestedTargetModelRow = targetModelRow,
                                     .targetAvailable = targetAvailable,
                                     .sourceFrameIndex = latestFrame,
                                     .sourceSolverHash = latestHash,
                                     .budgetStart = budgetStart,
                                     .budgetMilliseconds = budgetMilliseconds,
                                     .result = result } );
        if ( began )
        {
            if ( wasPendingLatestRestart )
            {
                ++prediction.build.latestRestartBeginCount;
            }
            prediction.build.pendingLatestRestart = false;
        }
        else
        {
            // Hazard: begin can decline after the shared frame budget expires.
            // Restore the request token so the newest velocity is retried next
            // pass instead of leaving the previous committed future visible.
            prediction.build.dirty = prediction.build.dirty || wasDirty;
            prediction.build.pendingLatestRestart = prediction.build.pendingLatestRestart || wasPendingLatestRestart;
        }
        if ( ReplayPredictionBudgetExpiredForPass( result,
                                                   SkullbonezCore::Core::MainMemoryReplayBudgetPass::PredictionBegin,
                                                   budgetStart,
                                                   budgetMilliseconds ) )
        {
            return;
        }
    }
    bool predictionCompletedThisPass = false;
    if ( prediction.build.building )
    {
        const double remainingMilliseconds = ReplayPredictionRemainingMilliseconds( budgetStart, budgetMilliseconds );
        if ( remainingMilliseconds > 0.0 )
        {
            const bool wasBuilding = prediction.build.building;
            StepReplayPredictionJob( predictionOwner,
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
    if ( predictionCompletedThisPass )
    {
        RebuildReplayPredictionCommittedTreeAfterWorkerCompletion( prediction, entities, targetId );
    }
}


} // namespace

void ReplayPrediction::UpdateFrame( PhysicsEngine& physicsEngine,
                                    const Gameplay::TornadoGameplay& tornadoGameplay,
                                    const SceneEntityStore& entities,
                                    const SkullbonezCore::Core::EngineConfig& config,
                                    const SkullbonezCore::Physics::PhysicsWorldForces& worldForces,
                                    SkullbonezCore::Threading::WorkerPool& workerPool,
                                    const ReplaySolverFrameSample* latestSolverSample,
                                    ReplayBodyId targetId,
                                    ModelRowHint targetModelRow,
                                    bool targetAvailable,
                                    bool liveAdvanceHeld,
                                    bool historicalSamplePaused,
                                    float solverTrackPosition,
                                    float solverPresentTrackPosition,
                                    bool scenePhysics,
                                    double fallbackSourceSimulationSeconds,
                                    double simulationTotalSeconds,
                                    double budgetMilliseconds,
                                    ReplayPredictionUpdateResult& result )
{
    const auto budgetStart = std::chrono::steady_clock::now();
    UpdateReplayPrediction( *this,
                            m_state,
                            physicsEngine,
                            tornadoGameplay,
                            entities,
                            config,
                            worldForces,
                            workerPool,
                            latestSolverSample,
                            targetId,
                            targetModelRow,
                            targetAvailable,
                            liveAdvanceHeld,
                            historicalSamplePaused,
                            solverTrackPosition,
                            solverPresentTrackPosition,
                            scenePhysics,
                            fallbackSourceSimulationSeconds,
                            simulationTotalSeconds,
                            budgetStart,
                            budgetMilliseconds,
                            result );
}


void ReplayPrediction::PreparePresentation( const SceneEntityStore& entities,
                                            const ColliderStore& colliderStore,
                                            ReplayBodyId targetId,
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
                                              float minHorizonSeconds,
                                              float maxHorizonSeconds ) noexcept
{
    if ( enablePrediction )
    {
        m_state.enabled = true;
        m_state.simulation.horizonSeconds =
            std::clamp( m_state.simulation.horizonSeconds, minHorizonSeconds, maxHorizonSeconds );
    }
    if ( refreshPrediction )
    {
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
    const std::vector<RunReplayPredictionFrame>& frames =
        usingBuildFrames ? m_state.build.buildFrames : m_state.simulation.frames;
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
    const double elapsedSeconds =
        (std::max)( 0.0, std::chrono::duration<double>( now - m_state.revealClock.anchor ).count() );
    const double revealRate = m_state.revealClock.secondsPerSecond > 0.0 ? m_state.revealClock.secondsPerSecond : 1.0;
    const double revealedSeconds = (std::min)( availableSeconds, elapsedSeconds * revealRate );
    const double revealFrame = revealedSeconds / static_cast<double>( PHYSICS_FIXED_DT );
    outProgress = std::clamp( static_cast<float>( revealFrame / static_cast<double>( lastFrame ) ), 0.0f, 1.0f );
    return true;
}

void ReplayPrediction::SetRevealRatePreservingCursor( double revealRate ) noexcept
{
    const double normalizedRevealRate = revealRate > 0.0 ? revealRate : 1.0;
    const double previousRevealRate =
        m_state.revealClock.secondsPerSecond > 0.0 ? m_state.revealClock.secondsPerSecond : 1.0;
    if ( m_state.revealClock.anchorValid )
    {
        const auto now = std::chrono::steady_clock::now();
        const double elapsedSeconds =
            (std::max)( 0.0, std::chrono::duration<double>( now - m_state.revealClock.anchor ).count() );
        const double revealedSeconds = elapsedSeconds * previousRevealRate;
        m_state.revealClock.anchor =
            now - std::chrono::duration_cast<std::chrono::steady_clock::duration>(
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

RunReplayPredictionState::RunReplayPredictionState() = default;

RunReplayPredictionState::~RunReplayPredictionState()
{
    // Hazard: WorkerPool tasks capture this replay state by reference. Destruct
    // only after the in-flight slice has dropped ownership of build scratch.
    WaitForReplayPredictionWorkerIdle( *this );
}

void ReplayPrediction::ClearFutureNodeCache()
{
    m_state.futureNodeCache.futureNodes.clear();
    m_state.futureNodeCache.futureNodeBuildScratch.clear();
    m_state.futureNodeCache.futureNodesBuiltFrameCount = 0;
    m_state.futureNodeCache.futureNodesBuiltContactIndex = 0;
    m_state.futureNodeCache.futureNodesBuiltTargetId = ReplayBodyId{};
    m_state.futureNodeCache.futureNodesBuiltRagdollVisuals = m_state.ragdollVisualsEnabled;
    m_state.futureNodeCache.futureNodesBuiltFromBuildFrames = false;
    m_state.futureNodeCache.futureNodesCacheValid = false;
    m_state.futureNodeCache.retainedMarkerCount = 0;
    m_state.trajectoryBuild.childFrameCount = 0;
    m_state.trajectoryBuild.builtNodeCount = 0;
}

void ReplayPrediction::WaitForJobIdle()
{
    WaitForReplayPredictionWorkerIdle( m_state );
}

bool ReplayPrediction::PromoteBuildPrefixToCommitted()
{
    if ( !m_state.BuildPrefixShouldBePresented() )
    {
        return false;
    }
    WaitForJobIdle();
    const std::size_t promotedFrameCount = m_state.PublishedBuildFrameCount();
    if ( promotedFrameCount < 2u || promotedFrameCount > m_state.build.buildFrames.size() )
    {
        return false;
    }

    // Hazard: this is the Play-button ownership transfer. The worker has
    // released buildFrames before the visible prefix becomes committed state.
    m_state.build.workerTask.reset();
    m_state.build.building = false;
    m_state.build.complete = true;
    m_state.simulation.frames.swap( m_state.build.buildFrames );
    m_state.simulation.frames.resize( promotedFrameCount );
    m_state.ResetBuildFramePublication();
    if ( !RebuildReplayPredictionCommittedRootTrajectory( m_state ) )
    {
        return false;
    }
    m_state.simulation.predictionEngineReady = false;
    m_state.simulation.predictionBodies.clear();
    m_state.simulation.predictionTornadoGameplay.Clear();
    m_state.simulation.predictionWorld = SkullbonezCore::Runtime::ReplaySolverWorldSnapshot();
    return true;
}

void ReplayPrediction::CancelJob( bool clearSamples )
{
    WaitForJobIdle();
    m_state.build.workerTask.reset();
    m_state.build.building = false;
    m_state.build.complete = false;
    m_state.build.buildMode = ReplayPredictionBuildMode::Undecided;
    m_state.build.pendingLatestRestart = false;
    m_state.simulation.targetModelRow.value = -1;
    m_state.build.nextTick = 1;
    m_state.build.targetTickCount = 0;
    m_state.simulation.predictionEngineReady = false;
    m_state.simulation.predictionBodies.clear();
    m_state.simulation.predictionTornadoGameplay.Clear();
    m_state.simulation.predictionWorld = SkullbonezCore::Runtime::ReplaySolverWorldSnapshot();
    // Runtime allocation policy: cancellation invalidates publication but keeps
    // the double-buffered frame payloads warm for the next replay rebuild.
    m_state.ResetBuildFramePublication();
    m_state.trajectoryBuild = RunReplayPredictionTrajectoryBuildState{};
    if ( clearSamples )
    {
        m_state.build.supersededRestartCount = 0;
        m_state.build.latestRestartBeginCount = 0;
        m_state.simulation.measuredTicksPerMs.store( 0.0, std::memory_order_release );
        m_state.simulation.probeElapsedMs = 0.0;
        m_state.simulation.probeTicksCompleted = 0;
        m_state.simulation.calibratedModelCount = -1;
        m_state.simulation.frames.clear();
        m_state.trajectoryStore.Clear();
        ClearFutureNodeCache();
    }
}

void ReplayPrediction::ClearCache()
{
    CancelJob( true );
    m_state.simulation.targetId = ReplayBodyId{};
    m_state.simulation.sourceFrameIndex = 0;
    m_state.simulation.sourceSolverHash = 0;
    m_state.simulation.sourceSimulationSeconds = 0.0;
    m_state.build.lastBuildTime = 0.0;
    m_state.trajectoryBuild = RunReplayPredictionTrajectoryBuildState{};
    m_state.trajectoryStore.Clear();
    m_state.baseline = ReplayPredictionBaselineSnapshot{};
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
    ReplayTrajectoryRecord* record =
        BeginReplayPastRootTrajectoryRecord( m_state.trajectoryStore, path.targetId, stats.sampleCount, frameNumber );
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
        stats.trajectory.publishedPointCount +=
            static_cast<uint64_t>( (std::min)( record.publishedPointCount, record.points.size() ) );
        stats.trajectory.maxRecordVersion = (std::max)( stats.trajectory.maxRecordVersion, record.version );
    }
    return stats;
}
