/*
File: SkullbonezSource/Runtime/Replay/RunReplayTools.cpp
Purpose:
  Owns replay path visualization, cause-focus overlays, and prediction-preview
  helpers as one real translation unit after deleting the replay text splices.

Summary:
  Replay tools read two timelines. Retained solver samples describe what already
  happened; prediction samples advance a private replay-owned physics engine.
  The renderer only receives lightweight overlay geometry.

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
  Replay ribbon: Screen-space-width overlay stroke emitted through
    RunEditorTracer's fixed-capacity ordinary or priority ribbon buffers.
  Ribbon quota: Frame-local count of ordinary replay ribbon records that path
    drawing may spend before it stops emitting trajectory segments.
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
  - Physics steps stay serial per prediction engine; body capture may fan out
    only after the step, and each worker writes a distinct pre-sized frame row.

Related:
  - SkullbonezSource/Runtime/Replay/RunReplayScrubberTools.cpp
  - SkullbonezSource/Runtime/Replay/RunReplayCauseTreeTools.cpp
  - Agentic/Reference/runtime-reference.md
  - Agentic/Reference/comment-style-guide.md
*/
#include "ReplayRuntime.h"
#include "../Editor/EditorTools.h"
#include "../Tools/RuntimeTools.h"
#include "../Scene/SceneEntityStore.h"
#include "../Editor/EditorHullAssets.h"
#include "../InputController.h"
#include "ReplayInteractionController.h"
#include "ReplayOverlayLayout.h"
#include "ReplayOverlayRenderer.h"
#include "ReplayPredictionReserve.h"
#include "../RuntimePickService.h"
#include "../Allocation/RuntimeAllocationTracker.h"
#include "../Allocation/RuntimeReserveAllocator.h"
#include "../../Physics/ColliderStore.h"
#include "../../Physics/PhysicsBodyStore.h"
#include "../../Physics/PhysicsApi.h"
#include "../../Physics/PhysicsEngine.h"
#include "../../Physics/PhysicsMass.h"
#include "../../Physics/PhysicsTimestep.h"
#include "../RuntimeFileWriter.h"
#include "../../Core/AmortizedTask.h"
#include "../../Core/Config.h"
#include "../../Core/WorkerPool.h"
#include "../../UI/UILayout.h"

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

#include <commdlg.h>

using namespace SkullbonezCore::Basics;
using namespace SkullbonezCore::Math::CollisionDetection;
using namespace SkullbonezCore::Math::Orientation;
using namespace SkullbonezCore::Math::Transformation;
using namespace SkullbonezCore::Physics;
using namespace SkullbonezCore::UI::Layout;
using namespace SkullbonezCore::Basics::RunInternal;
using namespace SkullbonezCore::Basics::ReplayOverlay;
using SkullbonezCore::Assets::EDITOR_HULL_ASSET_COUNT;
using SkullbonezCore::Assets::EDITOR_HULL_ASSETS;
using SkullbonezCore::Assets::EditorHullAsset;
using SkullbonezCore::Assets::EditorHullAssetDefaultsToContactRelease;
using SkullbonezCore::Assets::EditorHullAssetPath;
using SkullbonezCore::Assets::EditorHullAssetToken;
using SkullbonezCore::Math::Vector::Vector3;
namespace RuntimeAllocation = SkullbonezCore::Runtime::Allocation;

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


bool TryAddReplayTargetMarkerFromStores( RunEditorTracer& tracer,
                                         const PhysicsBodyStore& bodyStore,
                                         const ColliderStore& colliderStore,
                                         int modelIndex )
{
    const PhysicsBodyHandle bodyHandle = bodyStore.HandleForModelIndex( modelIndex );
    const PhysicsColliderHandle colliderHandle = colliderStore.HandleForBodyHandle( bodyHandle );
    const PhysicsBodyRecord* body = bodyStore.RecordForHandle( bodyHandle );
    const ColliderRecord* collider = colliderStore.RecordForHandle( colliderHandle );
    if ( !body || !collider || bodyStore.ModelIndexForHandle( bodyHandle ) != modelIndex ||
         colliderStore.ModelIndexForHandle( colliderHandle ) != modelIndex || collider->body != bodyHandle )
    {
        return false;
    }

    // Invariant: replay target identity resolves through body handles before
    // markers read store rows. This avoids scanning the legacy object record mirror just
    // to recover a stable ReplayBodyId that PhysicsBodyStore already owns.
    const float radius = (std::max)( 1.0f, (std::max)( body->boundingRadius, collider->boundingRadius ) ) * 1.18f;
    tracer.AddReplayTargetMarker( body->position, body->orientation, collider->shape, radius );
    return true;
}


// Concept: prediction stepping is pure physics. Contact-highlight and
// diagnostics-name presentation belongs to the live engine only; prediction
// samples read the private engine's body records directly.
bool StepPredictionEngineTick( PhysicsEngine& engine,
                               float fixedDt,
                               const EngineConfig& config,
                               const PhysicsWorldForces& worldForces,
                               SkullbonezCore::Threading::WorkerPool& workerPool )
{
    RuntimeAllocation::RuntimeAllocationScope replayAllocationScope(
        RuntimeAllocation::RuntimeAllocationPhase::Replay );
    engine.Step( fixedDt, config, worldForces, workerPool, nullptr, 0, PhysicsDiagnosticsCsvWriter{} );
    return true;
}


// Why: the 200-brick prediction scene needs more than the old 100-node cap to
// show the full contact spread instead of clipping the visual explanation.
constexpr std::size_t REPLAY_PATH_MAX_FUTURE_NODES = 240;
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

struct ReplayRibbonDrawQuota
{
    // Counts internal ribbon records, not logical trajectory lines. The
    // trajectory shader folds glow and core into one record per path segment.
    std::size_t remainingRibbonSegments = 0;
};

ReplayRibbonDrawQuota BeginReplayRibbonDrawQuota( const RunEditorTracer& tracer )
{
    ReplayRibbonDrawQuota quota;
    quota.remainingRibbonSegments = tracer.ReplayPathRibbonSegmentCapacityRemaining();
    return quota;
}

bool TryReserveReplayPathRibbonSegment( ReplayRibbonDrawQuota* quota )
{
    if ( !quota )
    {
        return true;
    }
    if ( quota->remainingRibbonSegments < REPLAY_RIBBON_SEGMENTS_PER_PATH_SEGMENT )
    {
        quota->remainingRibbonSegments = 0;
        return false;
    }

    quota->remainingRibbonSegments -= REPLAY_RIBBON_SEGMENTS_PER_PATH_SEGMENT;
    return true;
}

// Invariant: traversal continues after quota exhaustion. Every later logical
// segment is cheap to inspect and must be counted in its lane even though no
// vertex payload is emitted.
void AddOrAccountReplayPathSegment( RunEditorTracer& tracer,
                                    ReplayRibbonDrawQuota* quota,
                                    const Vector3& start,
                                    const Vector3& end,
                                    float r,
                                    float g,
                                    float b,
                                    MainMemoryReplayTrajectoryLane lane )
{
    if ( tracer.ReplayPathRibbonSegmentCapacityRemaining() < REPLAY_RIBBON_SEGMENTS_PER_PATH_SEGMENT )
    {
        if ( quota )
        {
            quota->remainingRibbonSegments = 0;
        }
        tracer.RecordReplayRibbonDroppedSegments( lane );
        return;
    }
    if ( !TryReserveReplayPathRibbonSegment( quota ) )
    {
        tracer.RecordReplayRibbonDroppedSegments( lane );
        return;
    }

    tracer.AddReplayPathSegment( start, end, r, g, b, lane );
}

void AddOrAccountReplayBaselinePathSegment( RunEditorTracer& tracer,
                                            ReplayRibbonDrawQuota* quota,
                                            const Vector3& start,
                                            const Vector3& end )
{
    if ( tracer.ReplayPathRibbonSegmentCapacityRemaining() < REPLAY_RIBBON_SEGMENTS_PER_PATH_SEGMENT )
    {
        if ( quota )
        {
            quota->remainingRibbonSegments = 0;
        }
        tracer.RecordReplayRibbonDroppedSegments( MainMemoryReplayTrajectoryLane::BaselineRoot );
        return;
    }
    if ( !TryReserveReplayPathRibbonSegment( quota ) )
    {
        tracer.RecordReplayRibbonDroppedSegments( MainMemoryReplayTrajectoryLane::BaselineRoot );
        return;
    }

    tracer.AddReplayBaselinePathSegment( start, end );
}

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
bool ReplayPredictionBudgetExpiredForPass( ReplayRuntime& replayRuntime,
                                           MainMemoryReplayBudgetPass pass,
                                           const std::chrono::steady_clock::time_point& start,
                                           double budgetMilliseconds )
{
    if ( !ReplayPredictionBudgetExpired( start, budgetMilliseconds ) )
    {
        return false;
    }
    replayRuntime.RecordReplayTrajectoryBudgetExpiry( pass );
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

// Concept: reveal cursor — the wall-clock playhead of the causal-unfold animation.
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
    if ( prediction.build.buildMode == ReplayPredictionBuildMode::Instant )
    {
        // Why: instant mode presents the completed future at once. The causal
        // unfold clock remains an amortized-mode presentation affordance.
        return lastAvailableFrame;
    }
    const auto now = std::chrono::steady_clock::now();
    if ( !prediction.revealClock.anchorValid )
    {
        prediction.revealClock.anchor = now;
        prediction.revealClock.anchorValid = true;
        return 0;
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
    return (std::min)( lastAvailableFrame, static_cast<ReplayFrameIndex>( revealFrame ) );
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
    static_cast<int>( REPLAY_PREDICTION_MAX_SECONDS / PHYSICS_FIXED_DT ) + 2;
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

int ReplayPredictionEngineReserveBytes( const PhysicsEngine& engine )
{
    // Why: seeding the private engine copies several physics-owned vectors.
    // Estimate the live working set before requesting the replay growth scope so
    // those copy allocations are approved under one bounded prediction owner.
    uint64_t bytes = static_cast<uint64_t>( sizeof( PhysicsEngine ) );
    bytes += engine.CollectPhysicsWorldMemoryBytes();
    bytes += engine.CollectDebugAndBroadphaseMemoryBytes();
    bytes +=
        static_cast<uint64_t>( SkullbonezCore::Physics::PhysicsEngine::ReadBodies( engine ).Records().capacity() ) *
        sizeof( PhysicsBodyRecord );
    bytes +=
        static_cast<uint64_t>( SkullbonezCore::Physics::PhysicsEngine::ReadColliders( engine ).Records().capacity() ) *
        sizeof( ColliderRecord );
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

    RuntimeAllocation::RuntimeReserveGrowthResult result = {};
    if ( !RequestReplayPredictionReserveGrowth( targetName,
                                                frameNumber,
                                                static_cast<int>( oldBytes ),
                                                static_cast<int>( requestedBytes ),
                                                1,
                                                result ) )
    {
        return false;
    }

    const RuntimeAllocation::RuntimeReserveOwnerHandle owner = ReplayPredictionReserveOwner();
    RuntimeAllocation::RuntimeAllocationScope replayAllocationScope(
        RuntimeAllocation::RuntimeAllocationPhase::Replay );
    RuntimeAllocation::RuntimeReserveOwnerScope ownerScope( owner );
    RuntimeAllocation::RuntimeReserveGrowthScope growthScope( owner,
                                                              RuntimeAllocation::RuntimeReservePhase::Replay,
                                                              result );
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

    RuntimeAllocation::RuntimeReserveGrowthResult result = {};
    if ( !RequestReplayPredictionReserveGrowth( targetName,
                                                frameNumber,
                                                static_cast<int>( oldBytes ),
                                                static_cast<int>( requestedBytes ),
                                                1,
                                                result ) )
    {
        return false;
    }

    const RuntimeAllocation::RuntimeReserveOwnerHandle owner = ReplayPredictionReserveOwner();
    RuntimeAllocation::RuntimeAllocationScope replayAllocationScope(
        RuntimeAllocation::RuntimeAllocationPhase::Replay );
    RuntimeAllocation::RuntimeReserveOwnerScope ownerScope( owner );
    RuntimeAllocation::RuntimeReserveGrowthScope growthScope( owner,
                                                              RuntimeAllocation::RuntimeReservePhase::Replay,
                                                              result );
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

bool ReplayContactHasModelIndex( const ReplaySolverPersistentContactSample& contact, int modelIndex )
{
    return modelIndex >= 0 && ( contact.bodyA == modelIndex || contact.bodyB == modelIndex );
}

int ReplayContactOtherModelIndex( const ReplaySolverPersistentContactSample& contact, int modelIndex )
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

Vector3 ReplayContactPoint( const ReplaySolverFrameSample& sample, const ReplaySolverPersistentContactSample& contact )
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

Vector3 ReplayContactNormalForModel( const ReplaySolverPersistentContactSample& contact, int modelIndex )
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

Vector3 ReplayContactImpulseForModel( const ReplaySolverPersistentContactSample& contact, int modelIndex )
{
    const Vector3 rowImpulse =
        contact.normal * contact.accN + contact.tangent1 * contact.accT1 + contact.tangent2 * contact.accT2;
    if ( contact.bodyB == modelIndex && !contact.isTerrain )
    {
        return rowImpulse;
    }
    return rowImpulse * -1.0f;
}

int ReplayFindPipelineIndexForContact( const ReplaySolverWorldSnapshot& snapshot,
                                       const ReplaySolverPersistentContactSample& contact )
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

float ReplayPathFrameT( ReplayFrameIndex frame, ReplayFrameIndex start, ReplayFrameIndex end )
{
    if ( end <= start || frame <= start )
    {
        return 0.0f;
    }
    if ( frame >= end )
    {
        return 1.0f;
    }
    const double numerator = static_cast<double>( frame - start );
    const double denominator = static_cast<double>( end - start );
    return static_cast<float>( std::clamp( numerator / denominator, 0.0, 1.0 ) );
}

float ReplayColorLerp( float a, float b, float t )
{
    return a + ( b - a ) * std::clamp( t, 0.0f, 1.0f );
}

void ReplayPastRootColor( float t, float& r, float& g, float& b )
{
    const float ageT = std::clamp( t, 0.0f, 1.0f );
    const float ageFade = 0.34f + ageT * 0.66f;
    r = std::clamp( ReplayColorLerp( 0.76f, 1.00f, ageT ) * ageFade, 0.0f, 1.0f );
    g = std::clamp( ReplayColorLerp( 0.18f, 0.92f, ageT ) * ageFade, 0.0f, 1.0f );
    b = std::clamp( ReplayColorLerp( 0.28f, 0.96f, ageT ) * ageFade, 0.0f, 1.0f );
}

void ReplayFutureRootColor( float t, float& r, float& g, float& b )
{
    const float futureT = std::clamp( t, 0.0f, 1.0f );
    r = std::clamp( ReplayColorLerp( 0.88f, 0.38f, futureT ), 0.0f, 1.0f );
    g = std::clamp( ReplayColorLerp( 1.00f, 0.94f, futureT ), 0.0f, 1.0f );
    b = std::clamp( ReplayColorLerp( 0.72f, 0.92f, futureT ), 0.0f, 1.0f );
}

void ReplayDepthPalette( int depth, float& r, float& g, float& b )
{
    // Concept: child depth is encoded as hue first and brightness second. This
    // keeps grandchildren readable even when many branches overlap in the same
    // prediction horizon.
    switch ( std::clamp( depth - 1, 0, 4 ) )
    {
    case 0:
        r = 1.00f;
        g = 0.58f;
        b = 0.18f;
        break;
    case 1:
        r = 0.76f;
        g = 0.92f;
        b = 0.24f;
        break;
    case 2:
        r = 0.26f;
        g = 0.88f;
        b = 0.96f;
        break;
    case 3:
        r = 0.54f;
        g = 0.62f;
        b = 1.00f;
        break;
    default:
        r = 0.96f;
        g = 0.46f;
        b = 0.76f;
        break;
    }

    const float depthDim = std::clamp( static_cast<float>( depth - 1 ) * 0.055f, 0.0f, 0.28f );
    r = std::clamp( r * ( 1.0f - depthDim ) + 0.18f * depthDim, 0.0f, 1.0f );
    g = std::clamp( g * ( 1.0f - depthDim ) + 0.20f * depthDim, 0.0f, 1.0f );
    b = std::clamp( b * ( 1.0f - depthDim ) + 0.24f * depthDim, 0.0f, 1.0f );
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


ReplayPredictionDrawFrameWindow ReplayPredictionDrawFrameWindowFor( RunReplayPredictionState& prediction,
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
        (std::min)( static_cast<std::size_t>( MAX_GAME_MODELS ), firstFrame.bodies.size() );
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
void DrawReplayPredictionBaselineSnapshot( const RunReplayPredictionState& prediction,
                                           const ColliderStore& colliderStore,
                                           RunEditorTracer& tracer,
                                           ReplayRibbonDrawQuota& ribbonQuota )
{
    const ReplayPredictionBaselineSnapshot& baseline = prediction.baseline;
    if ( !baseline.valid )
    {
        return;
    }

    if ( const ReplayTrajectoryRecord* record = ReplayTrajectoryRecordForDraw( prediction.trajectoryStore,
                                                                               baseline.rootId,
                                                                               ReplayTrajectoryLane::BaselineRoot,
                                                                               REPLAY_TRAJECTORY_COMMITTED_BRANCH ) )
    {
        const std::size_t pointCount = ReplayTrajectoryPublishedPointCount( *record );
        bool hasPrevious = false;
        Vector3 previous = SkullbonezCore::Math::Vector::ZERO_VECTOR;
        for ( std::size_t i = 0; i < pointCount; ++i )
        {
            const ReplayTrajectoryPoint& point = record->points[i];
            if ( hasPrevious && VectorMagSquared( point.position - previous ) > REPLAY_PATH_MIN_SEGMENT_DISTANCE_SQ )
            {
                AddOrAccountReplayBaselinePathSegment( tracer, &ribbonQuota, previous, point.position );
            }
            previous = point.position;
            hasPrevious = true;
        }
    }

    for ( const ReplayPredictionBaselineBodyPose& pose : baseline.bodyPoses )
    {
        const ColliderRecord* collider = ReplayColliderRecordForModelIndex( &colliderStore, pose.modelRow.value );
        if ( !collider )
        {
            continue;
        }
        if ( pose.hasEntryPose )
        {
            tracer.AddReplayBaselineEntryMarker( pose.entryPosition, pose.entryOrientation, collider->shape );
        }
        if ( pose.hasRestPose )
        {
            tracer.AddReplayBaselineRestMarker( pose.restPosition, pose.restOrientation, collider->shape );
        }
    }
}

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

template <typename NodeContainer>
void AddReplayFutureNodeToNodes( NodeContainer& nodes,
                                 ReplayBodyId rootId,
                                 ReplayBodyId parentId,
                                 int parentModelIndex,
                                 ReplayBodyId id,
                                 int modelIndex,
                                 ReplayFrameIndex firstFrame,
                                 const Vector3& contactPoint,
                                 const Vector3& contactNormal,
                                 int depth,
                                 bool contactDerived,
                                 bool replaceMotionFallback )
{
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
bool ShouldDrawReplayPathFrame( ReplayFrameIndex frameIndex, std::size_t stride )
{
    return stride <= 1 || ( frameIndex % static_cast<ReplayFrameIndex>( stride ) ) == 0;
}

std::size_t ReplayTrajectoryPublishedPointCount( const ReplayTrajectoryRecord& record )
{
    return (std::min)( record.publishedPointCount, record.points.size() );
}

const ReplayTrajectoryRecord* ReplayTrajectoryRecordForDraw( const ReplayTrajectoryStore& store,
                                                             ReplayBodyId id,
                                                             ReplayTrajectoryLane lane,
                                                             uint16_t branchOrdinal )
{
    if ( id.value == 0 )
    {
        return nullptr;
    }

    return store.FindRecord( ReplayTrajectoryKey( id, lane, branchOrdinal ) );
}

template <typename ColorForFrame>
void DrawReplayTrajectoryRecordSegments( const ReplayTrajectoryRecord& record,
                                         std::size_t pointCount,
                                         ReplayFrameIndex rangeStart,
                                         ReplayFrameIndex rangeEnd,
                                         ReplayFrameIndex forcedFrame,
                                         std::size_t sampleStride,
                                         RunEditorTracer& tracer,
                                         ReplayRibbonDrawQuota& ribbonQuota,
                                         MainMemoryReplayTrajectoryLane lane,
                                         ColorForFrame colorForFrame )
{
    pointCount = (std::min)( pointCount, record.points.size() );
    if ( pointCount < 2 || rangeEnd < rangeStart )
    {
        return;
    }

    bool hasPrevious = false;
    Vector3 previous = SkullbonezCore::Math::Vector::ZERO_VECTOR;
    for ( std::size_t i = 0; i < pointCount; ++i )
    {

        const ReplayTrajectoryPoint& point = record.points[i];
        if ( point.frameIndex < rangeStart )
        {
            continue;
        }
        if ( point.frameIndex > rangeEnd )
        {
            break;
        }
        const bool endpointFrame = point.frameIndex == rangeStart || point.frameIndex == rangeEnd ||
                                   point.frameIndex == forcedFrame || i == 0u || i + 1u == pointCount;
        if ( !endpointFrame && !ShouldDrawReplayPathFrame( point.frameIndex, sampleStride ) )
        {
            continue;
        }

        if ( hasPrevious && VectorMagSquared( point.position - previous ) > REPLAY_PATH_MIN_SEGMENT_DISTANCE_SQ )
        {
            float r = 1.0f;
            float g = 1.0f;
            float b = 1.0f;
            colorForFrame( point.frameIndex, r, g, b );
            AddOrAccountReplayPathSegment( tracer, &ribbonQuota, previous, point.position, r, g, b, lane );
        }
        previous = point.position;
        hasPrevious = true;
    }
}

struct ReplayPathChildDrawState
{
    RunReplayPathTraceNode node;
    bool active = false;
    // Concept: the two-box causal story. Entry is the body's IN-PLACE pose
    // from prediction frame 0 — the wall exactly as the live scene knows it.
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

std::size_t ReplayRetainedMarkerTrailStrideForFrameCount( std::size_t frameCount )
{
    constexpr std::size_t retainedTrailMaxSegments = 96;
    if ( frameCount <= retainedTrailMaxSegments )
    {
        return 1;
    }
    return ( frameCount + retainedTrailMaxSegments - 1 ) / retainedTrailMaxSegments;
}

void ReplayRetainedMarkerTrailColor( std::size_t trailOrdinal,
                                     float t,
                                     bool horizonGhost,
                                     float& r,
                                     float& g,
                                     float& b )
{
    const float laneOffset = std::clamp( static_cast<float>( trailOrdinal % 8u ) * 0.016f, 0.0f, 0.10f );
    if ( horizonGhost )
    {
        r = std::clamp( 0.42f + t * 0.20f + laneOffset, 0.38f, 0.74f );
        g = std::clamp( 0.78f + t * 0.16f, 0.72f, 1.0f );
        b = std::clamp( 0.95f + laneOffset * 0.30f, 0.86f, 1.0f );
        return;
    }

    r = std::clamp( 0.86f - t * 0.28f - laneOffset, 0.50f, 0.92f );
    g = std::clamp( 0.84f - t * 0.20f - laneOffset, 0.50f, 0.90f );
    b = std::clamp( 0.90f - t * 0.10f, 0.62f, 0.96f );
}

const ReplayTrajectoryRecord* FindReplayPredictionMarkerTrailRecord( const RunReplayPredictionState& prediction,
                                                                     ReplayBodyId id,
                                                                     bool usingBuildFrames )
{
    const uint16_t branchBase = usingBuildFrames ? static_cast<uint16_t>( REPLAY_PATH_MAX_FUTURE_NODES ) : 0u;
    const uint16_t branchEnd =
        static_cast<uint16_t>( branchBase + static_cast<uint16_t>( REPLAY_PATH_MAX_FUTURE_NODES ) );
    for ( const ReplayTrajectoryRecord& record : prediction.trajectoryStore.records )
    {
        if ( record.key.bodyId.value == id.value && record.key.lane == ReplayTrajectoryLane::FutureChildOutgoing &&
             record.key.branchOrdinal >= branchBase && record.key.branchOrdinal < branchEnd )
        {
            return &record;
        }
    }
    return nullptr;
}

void DrawReplayPredictionRetainedMarkerTrailFromStore( const RunReplayPredictionState& prediction,
                                                       const ReplayPredictionRetainedMarker& marker,
                                                       bool usingBuildFrames,
                                                       ReplayFrameIndex revealFrame,
                                                       ReplayFrameIndex lastFrame,
                                                       std::size_t trailOrdinal,
                                                       RunEditorTracer& tracer )
{
    const ReplayTrajectoryRecord* record =
        FindReplayPredictionMarkerTrailRecord( prediction, marker.id, usingBuildFrames );
    if ( !record )
    {
        return;
    }

    const std::size_t pointCount = ReplayTrajectoryPublishedPointCount( *record );
    if ( pointCount < 2 )
    {
        return;
    }

    const bool horizonGhost = marker.hasHorizonPose && !marker.hasRestPose;
    const std::size_t sampleStride = ReplayRetainedMarkerTrailStrideForFrameCount( pointCount );
    bool hasPrevious = false;
    Vector3 previous = SkullbonezCore::Math::Vector::ZERO_VECTOR;
    for ( std::size_t i = 0; i < pointCount; ++i )
    {
        const ReplayTrajectoryPoint& point = record->points[i];
        if ( point.frameIndex > revealFrame )
        {
            break;
        }
        const bool endpointFrame =
            point.frameIndex == revealFrame || point.frameIndex == lastFrame || i + 1u == pointCount;
        if ( !endpointFrame && !ShouldDrawReplayPathFrame( point.frameIndex, sampleStride ) )
        {
            continue;
        }

        if ( hasPrevious && VectorMagSquared( point.position - previous ) > REPLAY_PATH_MIN_SEGMENT_DISTANCE_SQ )
        {
            const float t = ReplayPathFrameT( point.frameIndex, 0, lastFrame );
            float r = 0.82f;
            float g = 0.82f;
            float b = 0.88f;
            ReplayRetainedMarkerTrailColor( trailOrdinal, t, horizonGhost, r, g, b );
            tracer.AddReplayCausalTrailSegment( previous, point.position, r, g, b );
        }
        previous = point.position;
        hasPrevious = true;
    }
}

void DrawReplayPredictionRetainedMarkers( const RunReplayPredictionState& prediction,
                                          bool usingBuildFrames,
                                          ReplayFrameIndex revealFrame,
                                          ReplayFrameIndex lastFrame,
                                          const ColliderStore& colliderStore,
                                          RunEditorTracer& tracer )
{
    // Invariant: marker emission is bounded by MAX_GAME_MODELS and independent
    // of the visualizer budget. Lines may degrade under load; already-revealed
    // yellow/grey boxes must not.
    for ( std::size_t i = 0; i < prediction.futureNodeCache.retainedMarkerCount; ++i )
    {
        const ReplayPredictionRetainedMarker& marker = prediction.futureNodeCache.retainedMarkers[i];
        const ColliderRecord* collider = ReplayColliderRecordForModelIndex( &colliderStore, marker.modelRow.value );
        if ( !collider )
        {
            continue;
        }
        DrawReplayPredictionRetainedMarkerTrailFromStore( prediction,
                                                          marker,
                                                          usingBuildFrames,
                                                          revealFrame,
                                                          lastFrame,
                                                          i,
                                                          tracer );
        if ( marker.hasEntryPose )
        {
            tracer.AddReplayCausalEntryMarker( marker.entryPosition, marker.entryOrientation, collider->shape );
        }
        if ( marker.hasRestPose )
        {
            tracer.AddReplayCausalRestMarker( marker.restPosition, marker.restOrientation, collider->shape );
        }
        else if ( marker.hasHorizonPose )
        {
            tracer.AddReplayCausalHorizonMarker( marker.horizonPosition, marker.horizonOrientation, collider->shape );
        }
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
// Yellow is fixed at the body's last still pose before it visibly moved — for
// a wall brick, its perfect-formation pose. Grey pops in ONLY at the body's
// final resting pose, and only when the completed prediction actually ends
// with it at rest; a body still moving at the horizon end gets a travel line
// and nothing else. Neither box ever slides.
void DrawReplayPredictionCausalMarkers( RunReplayPredictionState& prediction,
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

        // Why: completeFrames is null while the job is still building — a
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

void ReplayChildIncomingColor( int depth, float t, float& r, float& g, float& b )
{
    float baseR = 1.0f;
    float baseG = 1.0f;
    float baseB = 1.0f;
    ReplayDepthPalette( depth, baseR, baseG, baseB );
    const float arrivalT = std::clamp( t, 0.0f, 1.0f );
    const float lift = 0.16f * ( 1.0f - arrivalT );
    const float emphasis = 0.62f + arrivalT * 0.38f;
    r = std::clamp( baseR * emphasis + lift, 0.10f, 1.0f );
    g = std::clamp( baseG * emphasis + lift * 0.75f, 0.10f, 1.0f );
    b = std::clamp( baseB * emphasis + lift * 0.55f, 0.10f, 1.0f );
}

void ReplayChildFutureColor( int depth, float t, float& r, float& g, float& b )
{
    float baseR = 1.0f;
    float baseG = 1.0f;
    float baseB = 1.0f;
    ReplayDepthPalette( depth, baseR, baseG, baseB );
    const float horizonT = std::clamp( t, 0.0f, 1.0f );
    const float dim = 0.42f + horizonT * 0.24f;
    const float grey = 0.18f + horizonT * 0.10f;
    r = std::clamp( baseR * dim + grey, 0.12f, 0.92f );
    g = std::clamp( baseG * dim + grey, 0.12f, 0.94f );
    b = std::clamp( baseB * dim + grey + 0.04f, 0.16f, 0.98f );
}

uint16_t ReplayPredictionDrawBranch( bool usingBuildFrames )
{
    return usingBuildFrames ? REPLAY_TRAJECTORY_BUILD_BRANCH : REPLAY_TRAJECTORY_COMMITTED_BRANCH;
}

void DrawReplayPredictionRootTrajectoryFromStore( const RunReplayPredictionState& prediction,
                                                  ReplayBodyId rootId,
                                                  bool usingBuildFrames,
                                                  ReplayFrameIndex lastFrame,
                                                  ReplayFrameIndex revealFrame,
                                                  std::size_t sampleStride,
                                                  RunEditorTracer& tracer,
                                                  ReplayRibbonDrawQuota& ribbonQuota )
{
    const ReplayTrajectoryRecord* record =
        ReplayTrajectoryRecordForDraw( prediction.trajectoryStore,
                                       rootId,
                                       ReplayTrajectoryLane::FutureRoot,
                                       ReplayPredictionDrawBranch( usingBuildFrames ) );
    if ( !record )
    {
        return;
    }

    const std::size_t pointCount =
        usingBuildFrames ? prediction.PublishedBuildFrameCount() : ReplayTrajectoryPublishedPointCount( *record );
    DrawReplayTrajectoryRecordSegments( *record,
                                        pointCount,
                                        0,
                                        revealFrame,
                                        revealFrame,
                                        sampleStride,
                                        tracer,
                                        ribbonQuota,
                                        MainMemoryReplayTrajectoryLane::FutureRoot,
                                        [&]( ReplayFrameIndex frameIndex, float& r, float& g, float& b )
                                        {
                                            const float t = ReplayPathFrameT( frameIndex, 0, lastFrame );
                                            ReplayFutureRootColor( t, r, g, b );
                                        } );
}

void DrawReplayPredictionSmallSceneBodyTrajectories( const std::vector<RunReplayPredictionFrame>& frames,
                                                     std::size_t frameCount,
                                                     ReplayBodyId selectedId,
                                                     ReplayFrameIndex revealFrame,
                                                     std::size_t requestedStride,
                                                     RunEditorTracer& tracer,
                                                     ReplayRibbonDrawQuota& ribbonQuota )
{
    constexpr std::size_t MAX_ALL_BODY_PREDICTION_COUNT = 8u;
    frameCount = (std::min)( frameCount, frames.size() );
    if ( frameCount < 2u || frames[0].bodies.size() < 2u || frames[0].bodies.size() > MAX_ALL_BODY_PREDICTION_COUNT )
    {
        return;
    }

    const std::size_t auxiliaryBodyCount = frames[0].bodies.size() - 1u;
    const std::size_t logicalSegmentsRemaining =
        ribbonQuota.remainingRibbonSegments / REPLAY_RIBBON_SEGMENTS_PER_PATH_SEGMENT;
    const std::size_t segmentsPerBody = (std::max)( std::size_t{ 1 }, logicalSegmentsRemaining / auxiliaryBodyCount );
    // Why: all-body chaos paths share the existing fixed ribbon quota. Increase
    // sample stride as bodies/horizon grow instead of increasing runtime storage.
    const std::size_t quotaStride = ( frameCount + segmentsPerBody - 1u ) / segmentsPerBody;
    const std::size_t sampleStride = (std::max)( requestedStride, quotaStride );

    for ( std::size_t bodyIndex = 0; bodyIndex < frames[0].bodies.size(); ++bodyIndex )
    {
        const RunReplayPredictionBodySample& seedBody = frames[0].bodies[bodyIndex];
        if ( seedBody.id.value == 0 || seedBody.id.value == selectedId.value )
        {
            continue;
        }

        bool hasPrevious = false;
        Vector3 previous = SkullbonezCore::Math::Vector::ZERO_VECTOR;
        for ( std::size_t frameIndex = 0; frameIndex < frameCount; ++frameIndex )
        {
            const RunReplayPredictionFrame& frame = frames[frameIndex];
            if ( frame.frameIndex > revealFrame )
            {
                break;
            }
            const bool endpoint = frameIndex == 0u || frameIndex + 1u == frameCount || frame.frameIndex == revealFrame;
            if ( !endpoint && !ShouldDrawReplayPathFrame( frame.frameIndex, sampleStride ) )
            {
                if ( sampleStride > requestedStride && ShouldDrawReplayPathFrame( frame.frameIndex, requestedStride ) )
                {
                    // The adaptive quota deliberately merges this logical
                    // segment into a longer ribbon. Count the omission in the
                    // same lane the all-body preview would have emitted.
                    tracer.RecordReplayRibbonDroppedSegments( MainMemoryReplayTrajectoryLane::FutureRoot );
                }
                continue;
            }
            const RunReplayPredictionBodySample* body =
                FindReplayPredictionBodyByIdWithHint( frame, seedBody.id, seedBody.modelRow.value );
            if ( !body )
            {
                continue;
            }
            if ( hasPrevious && VectorMagSquared( body->position - previous ) > REPLAY_PATH_MIN_SEGMENT_DISTANCE_SQ )
            {
                float r = 1.0f;
                float g = 1.0f;
                float b = 1.0f;
                ReplayDepthPalette( static_cast<int>( bodyIndex ) + 1, r, g, b );
                AddOrAccountReplayPathSegment( tracer,
                                               &ribbonQuota,
                                               previous,
                                               body->position,
                                               r,
                                               g,
                                               b,
                                               MainMemoryReplayTrajectoryLane::FutureRoot );
            }
            previous = body->position;
            hasPrevious = true;
        }
    }
}

void DrawReplayPredictionChildTrajectoryRecord( const RunReplayPredictionState& prediction,
                                                const RunReplayPathTraceNode& node,
                                                std::size_t nodeIndex,
                                                bool usingBuildFrames,
                                                ReplayTrajectoryLane lane,
                                                ReplayFrameIndex revealFrame,
                                                ReplayFrameIndex lastFrame,
                                                std::size_t sampleStride,
                                                RunEditorTracer& tracer,
                                                ReplayRibbonDrawQuota& ribbonQuota )
{
    const ReplayTrajectoryRecord* record =
        ReplayTrajectoryRecordForDraw( prediction.trajectoryStore,
                                       node.id,
                                       lane,
                                       ReplayPredictionChildTrajectoryBranch( nodeIndex, usingBuildFrames ) );
    if ( !record )
    {
        return;
    }

    if ( lane == ReplayTrajectoryLane::FutureChildIncoming )
    {
        const ReplayFrameIndex endFrame = (std::min)( revealFrame, node.firstFrame );
        DrawReplayTrajectoryRecordSegments( *record,
                                            ReplayTrajectoryPublishedPointCount( *record ),
                                            0,
                                            endFrame,
                                            endFrame,
                                            sampleStride,
                                            tracer,
                                            ribbonQuota,
                                            MainMemoryReplayTrajectoryLane::FutureChildIncoming,
                                            [&]( ReplayFrameIndex frameIndex, float& r, float& g, float& b )
                                            {
                                                const float t = ReplayPathFrameT( frameIndex, 0, node.firstFrame );
                                                ReplayChildIncomingColor( node.depth, t, r, g, b );
                                            } );
        return;
    }

    const std::size_t pointCount = ReplayTrajectoryPublishedPointCount( *record );
    if ( pointCount < 2 || revealFrame <= node.firstFrame )
    {
        return;
    }

    bool hasPrevious = false;
    Vector3 previous = SkullbonezCore::Math::Vector::ZERO_VECTOR;
    for ( std::size_t i = 0; i < pointCount; ++i )
    {

        const ReplayTrajectoryPoint& point = record->points[i];
        if ( point.frameIndex < node.firstFrame )
        {
            previous = point.position;
            hasPrevious = true;
            continue;
        }
        if ( point.frameIndex > revealFrame )
        {
            break;
        }
        if ( point.frameIndex == node.firstFrame )
        {
            continue;
        }
        const bool endpointFrame =
            point.frameIndex == revealFrame || point.frameIndex == lastFrame || i + 1u == pointCount;
        if ( !endpointFrame && !ShouldDrawReplayPathFrame( point.frameIndex, sampleStride ) )
        {
            continue;
        }
        if ( hasPrevious && VectorMagSquared( point.position - previous ) > REPLAY_PATH_MIN_SEGMENT_DISTANCE_SQ )
        {
            const float t = ReplayPathFrameT( point.frameIndex, node.firstFrame, lastFrame );
            float r = 0.5f;
            float g = 0.5f;
            float b = 0.56f;
            ReplayChildFutureColor( node.depth, t, r, g, b );
            AddOrAccountReplayPathSegment( tracer,
                                           &ribbonQuota,
                                           previous,
                                           point.position,
                                           r,
                                           g,
                                           b,
                                           MainMemoryReplayTrajectoryLane::FutureChildOutgoing );
        }
        previous = point.position;
        hasPrevious = true;
    }
}

void DrawReplayPredictionChildTrajectoriesFromStore( const RunReplayPredictionState& prediction,
                                                     bool usingBuildFrames,
                                                     ReplayFrameIndex revealFrame,
                                                     ReplayFrameIndex lastFrame,
                                                     std::size_t sampleStride,
                                                     RunEditorTracer& tracer,
                                                     ReplayRibbonDrawQuota& ribbonQuota )
{
    const std::size_t nodeCount =
        (std::min)( prediction.futureNodeCache.futureNodes.size(), REPLAY_PATH_MAX_FUTURE_NODES );
    for ( std::size_t i = 0; i < nodeCount; ++i )
    {
        const RunReplayPathTraceNode& node = prediction.futureNodeCache.futureNodes[i];
        DrawReplayPredictionChildTrajectoryRecord( prediction,
                                                   node,
                                                   i,
                                                   usingBuildFrames,
                                                   ReplayTrajectoryLane::FutureChildIncoming,
                                                   revealFrame,
                                                   lastFrame,
                                                   sampleStride,
                                                   tracer,
                                                   ribbonQuota );
        DrawReplayPredictionChildTrajectoryRecord( prediction,
                                                   node,
                                                   i,
                                                   usingBuildFrames,
                                                   ReplayTrajectoryLane::FutureChildOutgoing,
                                                   revealFrame,
                                                   lastFrame,
                                                   sampleStride,
                                                   tracer,
                                                   ribbonQuota );
    }
}

void DrawReplayPastRootTrajectoryFromStore( const RunReplayPredictionState& prediction,
                                            ReplayBodyId rootId,
                                            ReplayFrameIndex presentFrame,
                                            RunEditorTracer& tracer,
                                            ReplayRibbonDrawQuota& ribbonQuota )
{
    const ReplayTrajectoryRecord* record = ReplayTrajectoryRecordForDraw( prediction.trajectoryStore,
                                                                          rootId,
                                                                          ReplayTrajectoryLane::PastRoot,
                                                                          REPLAY_TRAJECTORY_COMMITTED_BRANCH );
    if ( !record )
    {
        return;
    }

    const std::size_t pointCount = ReplayTrajectoryPublishedPointCount( *record );
    if ( pointCount < 2 )
    {
        return;
    }

    const ReplayFrameIndex firstFrame = record->points[0].frameIndex;
    const ReplayFrameIndex lastFrame = record->points[pointCount - 1u].frameIndex;
    const ReplayFrameIndex clampedPresent = std::clamp( presentFrame, firstFrame, lastFrame );
    const std::size_t sampleStride = ReplayPathStrideForSampleCount( pointCount );
    // Concept: a single PastRoot store record contains the retained solver
    // window. Draw-time presentFrame only recolors the already-published prefix
    // into "history" and "recorded future" halves; it never rebuilds samples.
    DrawReplayTrajectoryRecordSegments( *record,
                                        pointCount,
                                        firstFrame,
                                        clampedPresent,
                                        clampedPresent,
                                        sampleStride,
                                        tracer,
                                        ribbonQuota,
                                        MainMemoryReplayTrajectoryLane::PastRoot,
                                        [&]( ReplayFrameIndex frameIndex, float& r, float& g, float& b )
                                        {
                                            const float t = ReplayPathFrameT( frameIndex, firstFrame, clampedPresent );
                                            ReplayPastRootColor( t, r, g, b );
                                        } );
    DrawReplayTrajectoryRecordSegments( *record,
                                        pointCount,
                                        clampedPresent,
                                        lastFrame,
                                        lastFrame,
                                        sampleStride,
                                        tracer,
                                        ribbonQuota,
                                        MainMemoryReplayTrajectoryLane::FutureRoot,
                                        [&]( ReplayFrameIndex frameIndex, float& r, float& g, float& b )
                                        {
                                            const float t = ReplayPathFrameT( frameIndex, clampedPresent, lastFrame );
                                            ReplayFutureRootColor( t, r, g, b );
                                        } );
}

void DrawReplayPredictionRagdollTorsoTrails( const std::vector<RunReplayPredictionFrame>& frames,
                                             std::size_t frameCount,
                                             ReplayFrameIndex revealFrame,
                                             const SceneEntityStore& collection,
                                             RunEditorTracer& tracer,
                                             ReplayRibbonDrawQuota& ribbonQuota )
{
    const int modelCount = collection.Count();
    frameCount = (std::min)( frameCount, frames.size() );
    if ( frameCount < 2 || modelCount <= 0 )
    {
        return;
    }

    const ReplayFrameIndex lastFrame = frames[frameCount - 1].frameIndex;
    const std::size_t sampleStride = ReplayPathStrideForSampleCount( frameCount );
    for ( int modelIndex = 0; modelIndex < modelCount; ++modelIndex )
    {
        const SceneEntityRecord* entity = collection.TryGet( modelIndex );
        if ( !entity || entity->behaviorGroup.kind != SceneBehaviorGroupKind::SimpleRagdoll ||
             entity->behaviorGroup.partIndex != 0 )
        {
            continue;
        }

        bool hasPrevious = false;
        Vector3 previous = SkullbonezCore::Math::Vector::ZERO_VECTOR;
        for ( std::size_t frameIndex = 0; frameIndex < frameCount; ++frameIndex )
        {
            const RunReplayPredictionFrame& frame = frames[frameIndex];
            if ( frame.frameIndex > revealFrame )
            {
                break;
            }

            // Why: the reveal-edge frame must always draw, or trail tips would
            // advance in visible stride-sized jumps instead of growing smoothly.
            if ( frame.frameIndex != lastFrame && frame.frameIndex != revealFrame &&
                 !ShouldDrawReplayPathFrame( frame.frameIndex, sampleStride ) )
            {
                continue;
            }

            const RunReplayPredictionBodySample* body = FindReplayPredictionBodyByModelIndex( frame, modelIndex );
            if ( !body )
            {
                continue;
            }

            if ( hasPrevious && VectorMagSquared( body->position - previous ) > REPLAY_PATH_MIN_SEGMENT_DISTANCE_SQ )
            {
                const float t = ReplayPathFrameT( frame.frameIndex, 0, lastFrame );
                AddOrAccountReplayPathSegment( tracer,
                                               &ribbonQuota,
                                               previous,
                                               body->position,
                                               0.50f + 0.28f * ( 1.0f - t ),
                                               0.96f,
                                               0.92f,
                                               MainMemoryReplayTrajectoryLane::AuxiliaryTrail );
            }
            previous = body->position;
            hasPrevious = true;
        }
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

void ReplayAffectedBodyTrailColor( std::size_t trailOrdinal, float t, float& r, float& g, float& b )
{
    const float laneOffset = std::clamp( static_cast<float>( trailOrdinal % 6u ) * 0.025f, 0.0f, 0.125f );
    r = std::clamp( 1.00f - t * 0.32f - laneOffset * 0.40f, 0.55f, 1.00f );
    g = std::clamp( 0.58f + t * 0.28f + laneOffset, 0.48f, 0.94f );
    b = std::clamp( 0.14f + t * 0.42f + laneOffset * 0.50f, 0.10f, 0.72f );
}

void DrawReplayPredictionAffectedBodyTrails( const std::vector<RunReplayPredictionFrame>& frames,
                                             std::size_t frameCount,
                                             RunReplayPredictionState& prediction,
                                             ReplayFrameIndex revealFrame,
                                             bool bufferComplete,
                                             ReplayBodyId rootId,
                                             int rootModelIndex,
                                             const std::vector<RunReplayPathTraceNode>& futureNodes,
                                             const SceneEntityStore& collection,
                                             const ColliderStore& colliderStore,
                                             RunEditorTracer& tracer,
                                             ReplayRibbonDrawQuota& ribbonQuota )
{
    frameCount = (std::min)( frameCount, frames.size() );
    if ( frameCount < 2 || rootId.value == 0 )
    {
        return;
    }

    // Concept: affected-body trails are visual evidence, not contact authority.
    //
    // The future-node cache feeds both the cause window and child path renderer.
    // This pass exists only as a visual fallback while that cache has not yet
    // published a body; it skips ids already represented by either contact- or
    // motion-derived nodes.
    std::array<ReplayPredictionAffectedBodyTrail, REPLAY_PATH_MAX_FUTURE_NODES> trails = {};
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

            // Why: entry is the body's IN-PLACE pose from prediction frame 0 —
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

    if ( trailCount == 0 )
    {
        return;
    }

    const ReplayFrameIndex lastFrame = frames[frameCount - 1].frameIndex;
    const std::size_t sampleStride = ReplayPathStrideForSampleCount( frameCount );
    for ( std::size_t trailIndex = 0; trailIndex < trailCount; ++trailIndex )
    {
        ReplayPredictionAffectedBodyTrail& trail = trails[trailIndex];
        for ( std::size_t frameSlot = trail.firstFrameSlot + 1; frameSlot < frameCount; ++frameSlot )
        {
            if ( frames[frameSlot].frameIndex > revealFrame )
            {
                break;
            }

            const RunReplayPredictionFrame& frame = frames[frameSlot];
            if ( frame.frameIndex != lastFrame && frame.frameIndex != revealFrame &&
                 !ShouldDrawReplayPathFrame( frame.frameIndex, sampleStride ) )
            {
                continue;
            }

            const RunReplayPredictionBodySample* body =
                FindReplayPredictionBodyByIdWithHint( frame, trail.id, trail.modelRow.value );
            if ( !body )
            {
                continue;
            }

            if ( VectorMagSquared( body->position - trail.previous ) > REPLAY_PATH_MIN_SEGMENT_DISTANCE_SQ )
            {
                const float t = ReplayPathFrameT( frame.frameIndex, trail.firstFrame, lastFrame );
                float r = 1.0f;
                float g = 0.65f;
                float b = 0.18f;
                ReplayAffectedBodyTrailColor( trailIndex, t, r, g, b );
                AddOrAccountReplayPathSegment( tracer,
                                               &ribbonQuota,
                                               trail.previous,
                                               body->position,
                                               r,
                                               g,
                                               b,
                                               MainMemoryReplayTrajectoryLane::AuxiliaryTrail );
            }

            if ( ReplayPredictionBodyHasVisibleLinearMotion( *body ) )
            {
                trail.lastMotionFrame = frame.frameIndex;
            }
            trail.previous = body->position;
            trail.modelRow.value = body->modelRow.value;
        }
    }

    // Why: marker emission is bounded and cheap, and "once rendered, a causal
    // box never leaves" outranks draw-time degradation.
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
        // completed horizon — see DrawReplayPredictionCausalMarkers.
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
                                context.rootId,
                                parentId,
                                parentModelIndex,
                                id,
                                modelIndex,
                                firstFrame,
                                contactPoint,
                                contactNormal,
                                depth,
                                contactDerived,
                                true );
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
                                       std::vector<RunReplayPredictionBodyBackup>& outBodies )
{
    PROFILE_SCOPED( "Frame/Replay/Prediction/CaptureBodyState" );
    const int modelCount = bodyStore.Count();
    const auto& bodyRecords = bodyStore.Records();
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
        RuntimeAllocation::RuntimeAllocationScope replayAllocationScope(
            RuntimeAllocation::RuntimeAllocationPhase::Replay );
        const PhysicsBodyRecord& body = bodyRecords[static_cast<std::size_t>( i )];
        RunReplayPredictionBodyBackup backup;
        backup.id.value = body.replayBodyId;
        backup.modelRow.value = i;
        backup.position = body.position;
        backup.orientation = body.orientation;
        backup.linearVelocity = body.linearVelocity;
        backup.angularVelocity = body.angularVelocity;
        backup.mass = body.mass;
        backup.inverseMass = body.invMass;
        backup.rotationalInertia = body.rotationalInertia;
        backup.inverseRotationalInertia = body.invRotationalInertia;
        backup.fixed = body.isFixed;
        outBodies[static_cast<std::size_t>( i )] = backup;
    };

    // Invariant: this loop reads authoritative body records and one
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
                                 const PhysicsEngine& liveEngine,
                                 const EngineConfig& config,
                                 const PhysicsWorldForces& worldForces,
                                 int modelCount )
{
    PROFILE_SCOPED( "Frame/Replay/Prediction/SeedPrivateEngine" );
    const RuntimeAllocation::RuntimeReserveOwnerHandle owner = ReplayPredictionReserveOwner();
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

    RuntimeAllocation::RuntimeReserveGrowthResult result = {};
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

    RuntimeAllocation::RuntimeAllocationScope replayAllocationScope(
        RuntimeAllocation::RuntimeAllocationPhase::Replay );
    RuntimeAllocation::RuntimeReserveOwnerScope ownerScope( owner );
    RuntimeAllocation::RuntimeReserveGrowthScope growthScope( owner,
                                                              RuntimeAllocation::RuntimeReservePhase::Replay,
                                                              result );
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
    predictionEngine.ApplyRuntimeConfig( config );
    prediction.simulation.predictionWorldForces = worldForces;
    if ( !ApplyReplayPredictionBodyState( predictionEngine, prediction.simulation.predictionBodies ) ||
         !predictionEngine.RestoreReplaySolverSnapshot( prediction.simulation.predictionWorld,
                                                        MakePhysicsBodyCountFromNonNegativeInt( modelCount ) ) )
    {
        return false;
    }
    prediction.simulation.predictionEngineReady = true;
    return true;
}


bool CaptureReplayPredictionFrame( ReplayRuntime& replayRuntime,
                                   const PhysicsEngine& physicsEngine,
                                   SkullbonezCore::Threading::WorkerPool& workerPool,
                                   int modelCount,
                                   ReplayFrameIndex frameIndex )
{
    PROFILE_SCOPED( "Frame/Replay/Prediction/CaptureSample" );
    const PhysicsBodyStore& bodyStore = SkullbonezCore::Physics::PhysicsEngine::ReadBodies( physicsEngine );
    const auto& bodyRecords = bodyStore.Records();
    if ( static_cast<int>( bodyRecords.size() ) < modelCount )
    {
        return false;
    }

    RunReplayPredictionState& prediction = replayRuntime.Prediction();
    const std::size_t frameSlot = static_cast<std::size_t>( frameIndex );
    if ( frameSlot >= prediction.build.buildFrames.size() )
    {
        return false;
    }

    RunReplayPredictionFrame& frame = prediction.build.buildFrames[frameSlot];
    frame.frameIndex = frameIndex;
    frame.simulationSeconds = prediction.simulation.sourceSimulationSeconds +
                              static_cast<double>( frameIndex ) * static_cast<double>( PHYSICS_FIXED_DT );
    frame.tornadoSystemElapsedSeconds = physicsEngine.GetTornadoSystemElapsedSeconds();
    frame.contactsIncomplete = false;
    if ( static_cast<std::size_t>( modelCount ) > frame.bodies.capacity() )
    {
        return false;
    }
    frame.bodies.resize( static_cast<std::size_t>( modelCount ) );

    const auto captureBody = [&]( int i )
    {
        RuntimeAllocation::RuntimeAllocationScope replayAllocationScope(
            RuntimeAllocation::RuntimeAllocationPhase::Replay );
        const PhysicsBodyRecord& source = bodyRecords[static_cast<std::size_t>( i )];
        RunReplayPredictionBodySample body;
        body.id.value = source.replayBodyId;
        body.modelRow.value = i;
        body.position = source.position;
        body.orientation = source.orientation;
        body.linearVelocity = source.linearVelocity;
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

void RunReplayPredictionWorkerRange( ReplayRuntime& replayRuntime,
                                     const EngineConfig& config,
                                     SkullbonezCore::Threading::WorkerPool& workerPool,
                                     int modelCount,
                                     int beginTickIndex,
                                     int endTickIndex )
{
    RunReplayPredictionState& prediction = replayRuntime.Prediction();
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
                                        PHYSICS_FIXED_DT,
                                        config,
                                        prediction.simulation.predictionWorldForces,
                                        workerPool ) ||
             !CaptureReplayPredictionFrame( replayRuntime,
                                            predictionEngine,
                                            workerPool,
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

void ReplayPredictionWorkerOperation::operator()( int beginTickIndex, int endTickIndex ) const
{
    // Lifetime: CancelPredictionJob waits for the enclosing AmortizedTask before
    // any of these replay-owned borrows can be cleared or replaced.
    if ( replayRuntime && config && workerPool )
    {
        RunReplayPredictionWorkerRange( *replayRuntime,
                                        *config,
                                        *workerPool,
                                        modelCount,
                                        beginTickIndex,
                                        endTickIndex );
    }
}

namespace
{
bool CompleteReplayPredictionJobOnFrameThread( ReplayRuntime& replayRuntime, double simulationTotalSeconds )
{
    RunReplayPredictionState& prediction = replayRuntime.Prediction();
    if ( prediction.build.workerFailed.load( std::memory_order_acquire ) )
    {
        const bool preserveCommittedFuture = prediction.simulation.frames.size() >= 2u;
        replayRuntime.CancelPredictionJob( !preserveCommittedFuture );
        replayRuntime.Prediction().build.dirty = true;
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
            prediction.simulation.predictionWorld,
            MakePhysicsBodyCountFromNonNegativeInt(
                SkullbonezCore::Physics::PhysicsEngine::ReadBodies( *prediction.simulation.predictionEngine )
                    .Count() ) );
    }

    const float previousPresentT = replayRuntime.SolverPresentTrackPosition();
    const float previousSolverPosition = replayRuntime.TrackPosition( RunReplayTrack::Solver );
    const bool hadCommittedPredictionFrames = prediction.simulation.frames.size() >= 2;
    const bool solverWasOldLiveEdge =
        !hadCommittedPredictionFrames && ReplayRuntime::AtPresentTrackPosition( previousSolverPosition, 1.0f );
    const bool scrubberWasPinnedToPresent =
        !replayRuntime.Scrubber().historicalSamplePaused ||
        ReplayRuntime::AtPresentTrackPosition( previousSolverPosition, previousPresentT ) || solverWasOldLiveEdge;

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
        replayRuntime.SetTrackPosition( RunReplayTrack::Solver, replayRuntime.SolverPresentTrackPosition() );
        if ( replayRuntime.Scrubber().activeTrack == RunReplayTrack::Solver )
        {
            replayRuntime.Scrubber().historicalSamplePaused = false;
        }
    }
    // Why: worker timing decides how much build-frame topology render had seen
    // before the swap. Rebuild the child cache from the committed full buffer so
    // the final trajectory store and automation fingerprint are scheduler-stable.
    ClearReplayPredictionFutureNodeCache( prediction );
    prediction.build.lastBuildTime = simulationTotalSeconds;
    return true;
}

bool BeginReplayPredictionJob( ReplayRuntime& replayRuntime,
                               PhysicsEngine& physicsEngine,
                               const SceneEntityStore& entities,
                               const EngineConfig& config,
                               const SkullbonezCore::Physics::PhysicsWorldForces& worldForces,
                               SkullbonezCore::Threading::WorkerPool& workerPool,
                               bool scenePhysics,
                               double fallbackSourceSimulationSeconds,
                               double simulationTotalSeconds,
                               ReplayFrameIndex sourceFrameIndex,
                               uint64_t sourceSolverHash,
                               const std::chrono::steady_clock::time_point& budgetStart,
                               double budgetMilliseconds )
{
    PROFILE_SCOPED( "Frame/Replay/Prediction/BeginJob" );
    // Hazard: begin captures the initial prediction snapshot. Budget may stop
    // us before setup starts, but once replay scratch and solver state are
    // reserved we must publish frame 0 so large predictions can draw progress
    // instead of thrashing a dirty begin job every render frame.
    if ( ReplayPredictionBudgetExpiredForPass( replayRuntime,
                                               MainMemoryReplayBudgetPass::PredictionBegin,
                                               budgetStart,
                                               budgetMilliseconds ) )
    {
        return false;
    }

    RunReplayPredictionState& prediction = replayRuntime.Prediction();
    const ReplayBodyId requestedTargetId = replayRuntime.PathVisualizer().targetId;
    const ReplayFrameIndex previousSourceFrameIndex = prediction.simulation.sourceFrameIndex;
    const uint64_t previousSourceSolverHash = prediction.simulation.sourceSolverHash;
    const bool preserveCommittedFuture = prediction.enabled && scenePhysics && requestedTargetId.value != 0 &&
                                         prediction.simulation.targetId.value == requestedTargetId.value &&
                                         prediction.simulation.frames.size() >= 2u;
    const std::size_t buildPresentationFrameCount =
        preserveCommittedFuture ? ReplayPredictionBuildPresentationFrameCountForRefresh( prediction, requestedTargetId )
                                : 2u;
    const bool clearSamplesOnCancel = !preserveCommittedFuture;
    replayRuntime.CancelPredictionJob( clearSamplesOnCancel );
    if ( clearSamplesOnCancel )
    {
        replayRuntime.ClearPredictionFutureNodeCache();
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
    if ( const ReplaySolverFrameSample* latest = replayRuntime.Solver().LatestSample() )
    {
        prediction.simulation.sourceSimulationSeconds = latest->simulationSeconds;
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
    if ( replayRuntime.PathVisualizer().hasTarget && replayRuntime.PathVisualizer().targetId.value != 0 )
    {
        ModelRowHint targetHint;
        targetHint.value = replayRuntime.PathVisualizer().targetModelRow.value;
        int targetIndex = -1;
        if ( ReplayPredictionBudgetExpiredForPass( replayRuntime,
                                                   MainMemoryReplayBudgetPass::PredictionBegin,
                                                   budgetStart,
                                                   budgetMilliseconds ) )
        {
            prediction.build.dirty = true;
            return false;
        }
        if ( !TryResolveReplayBodyModelIndex( liveBodyStore,
                                              replayRuntime.PathVisualizer().targetId,
                                              targetHint,
                                              modelCount,
                                              targetIndex ) )
        {
            replayRuntime.PathVisualizer().targetModelRow.value = targetHint.value;
            return false;
        }
        prediction.simulation.targetModelRow.value = targetIndex;
        replayRuntime.PathVisualizer().targetModelRow.value = targetHint.value;
    }

    prediction.simulation.horizonSeconds = std::clamp( prediction.simulation.horizonSeconds,
                                                       REPLAY_PREDICTION_MIN_SECONDS,
                                                       REPLAY_PREDICTION_MAX_SECONDS );
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
        replayRuntime.CancelPredictionJob( clearSamplesOnCancel );
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
        replayRuntime.CancelPredictionJob( clearSamplesOnCancel );
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
        replayRuntime.CancelPredictionJob( clearSamplesOnCancel );
        prediction.build.dirty = true;
        return false;
    }
    if ( !PrepareReplayPredictionTrajectoryBuild( prediction, prediction.simulation.targetId, buildFrameCapacity ) )
    {
        replayRuntime.CancelPredictionJob( clearSamplesOnCancel );
        prediction.build.dirty = true;
        return false;
    }

    if ( modelCount != SkullbonezCore::Physics::PhysicsEngine::ReadColliders( physicsEngine ).Count() ||
         modelCount != entities.Count() ||
         !CaptureReplayPredictionBodyState( liveBodyStore, workerPool, prediction.simulation.predictionBodies ) )
    {
        replayRuntime.CancelPredictionJob( clearSamplesOnCancel );
        return false;
    }

    physicsEngine.CaptureReplaySolverSnapshot( prediction.simulation.predictionWorld,
                                               MakePhysicsBodyCountFromNonNegativeInt( modelCount ) );

    if ( !SeedReplayPredictionEngine( prediction, physicsEngine, config, worldForces, modelCount ) )
    {
        replayRuntime.CancelPredictionJob( clearSamplesOnCancel );
        prediction.build.dirty = true;
        return false;
    }

    if ( !prediction.simulation.predictionEngine ||
         !CaptureReplayPredictionFrame( replayRuntime,
                                        *prediction.simulation.predictionEngine,
                                        workerPool,
                                        modelCount,
                                        0 ) )
    {
        replayRuntime.CancelPredictionJob( clearSamplesOnCancel );
        prediction.build.dirty = true;
        return false;
    }
    {
        RuntimeAllocation::RuntimeAllocationScope replayAllocationScope(
            RuntimeAllocation::RuntimeAllocationPhase::Replay );
        RuntimeAllocation::RuntimeReserveOwnerScope ownerScope( ReplayPredictionReserveOwner() );
        prediction.build.workerTask = std::make_unique<ReplayPredictionAmortizedTask>(
            prediction.build.targetTickCount,
            REPLAY_PREDICTION_TICKS_PER_WORKER_SUBMIT,
            ReplayPredictionWorkerOperation{ &replayRuntime, &config, &workerPool, modelCount } );
        prediction.build.workerTask->SetBudget( REPLAY_PREDICTION_TICKS_PER_WORKER_SUBMIT );
    }
    prediction.build.building = true;

    return !prediction.build.buildFrames.empty();
}


bool StepReplayPredictionJob( ReplayRuntime& replayRuntime,
                              SkullbonezCore::Threading::WorkerPool& workerPool,
                              double simulationTotalSeconds,
                              const std::chrono::steady_clock::time_point& budgetStart,
                              double budgetMilliseconds )
{
    PROFILE_SCOPED( "Frame/Replay/Prediction/Slice" );
    if ( !replayRuntime.Prediction().build.building )
    {
        return replayRuntime.Prediction().build.complete;
    }

    if ( ReplayPredictionBudgetExpiredForPass( replayRuntime,
                                               MainMemoryReplayBudgetPass::PredictionStep,
                                               budgetStart,
                                               budgetMilliseconds ) )
    {
        return false;
    }

    RunReplayPredictionState& prediction = replayRuntime.Prediction();
    if ( !prediction.simulation.predictionEngineReady || !prediction.simulation.predictionEngine ||
         !prediction.build.workerTask )
    {
        const bool preserveCommittedFuture = prediction.simulation.frames.size() >= 2u;
        replayRuntime.CancelPredictionJob( !preserveCommittedFuture );
        replayRuntime.Prediction().build.dirty = true;
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
        PROFILE_SCOPED( "Frame/Replay/Prediction/Slice/Instant" );
        prediction.build.workerTask->SetBudget( prediction.build.targetTickCount );
    }
    else if ( prediction.build.buildMode == ReplayPredictionBuildMode::Undecided )
    {
        PROFILE_SCOPED( "Frame/Replay/Prediction/Slice/Probe" );
        prediction.build.workerTask->SetBudget( prediction.build.probeTickBudget );
    }
    else
    {
        PROFILE_SCOPED( "Frame/Replay/Prediction/Slice/Amortized" );
        prediction.build.workerTask->SetBudget( REPLAY_PREDICTION_TICKS_PER_WORKER_SUBMIT );
    }
    prediction.build.workerTask->SubmitTick( workerPool );

    if ( CompleteReplayPredictionJobOnFrameThread( replayRuntime, simulationTotalSeconds ) )
    {
        return true;
    }

    if ( prediction.build.workerFailed.load( std::memory_order_acquire ) )
    {
        (void)CompleteReplayPredictionJobOnFrameThread( replayRuntime, simulationTotalSeconds );
        return false;
    }

    return prediction.PublishedBuildFrameCount() >= 2u || prediction.build.complete;
}


void RebuildReplayPredictionCommittedTreeAfterWorkerCompletion( ReplayRuntime& replayRuntime,
                                                                const SceneEntityStore& modelCollection )
{
    RunReplayPredictionState& prediction = replayRuntime.Prediction();
    const ReplayBodyId rootId = replayRuntime.PathVisualizer().targetId;
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


bool DrawReplayPredictionOverlay( ReplayRuntime& replayRuntime,
                                  const SceneEntityStore& modelCollection,
                                  const ColliderStore& colliderStore,
                                  RunEditorTracer& tracer,
                                  ReplayRibbonDrawQuota& ribbonQuota,
                                  double budgetMilliseconds )
{
    const RunReplayPredictionState& prediction = replayRuntime.Prediction();
    const bool usingBuildFrames = prediction.BuildPrefixShouldBePresented();
    const std::vector<RunReplayPredictionFrame>& activePredictionFrames =
        usingBuildFrames ? prediction.build.buildFrames : prediction.simulation.frames;
    const std::size_t activePredictionFrameCount =
        usingBuildFrames ? prediction.PublishedBuildFrameCount() : activePredictionFrames.size();
    if ( activePredictionFrameCount < 2 )
    {
        return false;
    }

    // Concept: every pass below draws only frames at or before the reveal
    // cursor. That single clamp is what turns a finished prediction buffer into
    // an unfolding animation: the root line grows first, and each child starts
    // drawing when the cursor passes the frame where its cause happened.
    const ReplayPredictionDrawFrameWindow drawWindow = ReplayPredictionDrawFrameWindowFor( replayRuntime.Prediction(),
                                                                                           activePredictionFrames,
                                                                                           activePredictionFrameCount );
    // Why: while the job is still building there is no authoritative ending,
    // so no grey resting box may be derived from the growing prefix.
    const bool bufferComplete = !usingBuildFrames;
    DrawReplayPredictionBaselineSnapshot( replayRuntime.Prediction(), colliderStore, tracer, ribbonQuota );

    if ( !replayRuntime.PathVisualizer().hasTarget || replayRuntime.PathVisualizer().targetId.value == 0 )
    {
        replayRuntime.ClearPredictionFutureNodeCache();
        if ( replayRuntime.Prediction().ragdollVisualsEnabled )
        {
            DrawReplayPredictionRagdollTorsoTrails( activePredictionFrames,
                                                    activePredictionFrameCount,
                                                    drawWindow.revealFrame,
                                                    modelCollection,
                                                    tracer,
                                                    ribbonQuota );
        }
        return true;
    }

    {
        PROFILE_SCOPED( "Frame/Replay/Prediction/DrawRoot" );
        DrawReplayPredictionRootTrajectoryFromStore( replayRuntime.Prediction(),
                                                     replayRuntime.PathVisualizer().targetId,
                                                     usingBuildFrames,
                                                     drawWindow.lastFrame,
                                                     drawWindow.revealFrame,
                                                     drawWindow.sampleStride,
                                                     tracer,
                                                     ribbonQuota );
        DrawReplayPredictionSmallSceneBodyTrajectories( activePredictionFrames,
                                                        activePredictionFrameCount,
                                                        replayRuntime.PathVisualizer().targetId,
                                                        drawWindow.revealFrame,
                                                        drawWindow.sampleStride,
                                                        tracer,
                                                        ribbonQuota );

        // Why: the root gets no yellow entry box — the white selection marker
        // already anchors where its story starts. Grey follows the same rule
        // as every child: only a completed prediction that actually ends at
        // rest may place a resting box, and only after the reveal cursor has
        // watched the root stop moving.
        if ( bufferComplete )
        {
            RetainReplayPredictionRootRestMarker( replayRuntime.Prediction(),
                                                  activePredictionFrames,
                                                  activePredictionFrameCount,
                                                  drawWindow.revealFrame,
                                                  replayRuntime.PathVisualizer().targetId,
                                                  replayRuntime.PathVisualizer().targetModelRow.value,
                                                  colliderStore );
        }
    }
    const auto buildBudgetStart = std::chrono::steady_clock::now();
    bool drawFutureTree = false;
    {
        PROFILE_SCOPED( "Frame/Replay/Prediction/BuildTree" );
        if ( replayRuntime.Prediction().enabled )
        {
            // Why: downstream child paths must advance with the same populated
            // prediction prefix as the root line. The cache builder accepts growing
            // buildFrames and only publishes coherent node prefixes.
            UpdateReplayPredictionFutureNodeCache( replayRuntime.Prediction(),
                                                   activePredictionFrames,
                                                   activePredictionFrameCount,
                                                   usingBuildFrames,
                                                   modelCollection,
                                                   replayRuntime.PathVisualizer().targetId,
                                                   buildBudgetStart,
                                                   budgetMilliseconds );
            // Invariant: child trajectory publication follows the exact same
            // populated build prefix as the root. Waiting for the complete
            // buffer makes the striker cross an obstacle alone, then reveals
            // every impacted body's future in one visually false batch.
            UpdateReplayPredictionTrajectoryStore( replayRuntime.Prediction(),
                                                   activePredictionFrames,
                                                   activePredictionFrameCount,
                                                   usingBuildFrames,
                                                   replayRuntime.PathVisualizer().targetId );
            (void)ReplayPredictionBudgetExpiredForPass( replayRuntime,
                                                        MainMemoryReplayBudgetPass::PredictionBuildTree,
                                                        buildBudgetStart,
                                                        budgetMilliseconds );
            drawFutureTree = ReplayPredictionFutureTreeReadyForDraw( replayRuntime.Prediction(),
                                                                     replayRuntime.PathVisualizer().targetId,
                                                                     usingBuildFrames,
                                                                     activePredictionFrameCount );
        }
        else
        {
            // Why: live play freezes prediction visualization. Keep drawing the
            // committed topology, but do not discover new child nodes while the
            // real simulation advances underneath the overlay.
            drawFutureTree = ReplayPredictionFutureTreeReadyForDraw( replayRuntime.Prediction(),
                                                                     replayRuntime.PathVisualizer().targetId,
                                                                     usingBuildFrames,
                                                                     activePredictionFrameCount );
        }
    }
    if ( drawFutureTree )
    {
        PROFILE_SCOPED( "Frame/Replay/Prediction/DrawChildren" );
        ReplayPathChildDrawContext childDraw;
        DrawReplayPredictionChildTrajectoriesFromStore( replayRuntime.Prediction(),
                                                        usingBuildFrames,
                                                        drawWindow.revealFrame,
                                                        drawWindow.lastFrame,
                                                        drawWindow.sampleStride,
                                                        tracer,
                                                        ribbonQuota );
        BuildReplayPredictionChildMarkerContext( childDraw,
                                                 replayRuntime.Prediction(),
                                                 activePredictionFrames,
                                                 activePredictionFrameCount,
                                                 drawWindow.revealFrame );

        DrawReplayPredictionCausalMarkers( replayRuntime.Prediction(),
                                           childDraw,
                                           drawWindow.revealFrame,
                                           bufferComplete ? &activePredictionFrames : nullptr,
                                           bufferComplete ? activePredictionFrameCount : 0 );
    }

    {
        PROFILE_SCOPED( "Frame/Replay/Prediction/DrawAffectedBodies" );
        DrawReplayPredictionAffectedBodyTrails( activePredictionFrames,
                                                activePredictionFrameCount,
                                                replayRuntime.Prediction(),
                                                drawWindow.revealFrame,
                                                bufferComplete,
                                                replayRuntime.PathVisualizer().targetId,
                                                replayRuntime.PathVisualizer().targetModelRow.value,
                                                replayRuntime.Prediction().futureNodeCache.futureNodes,
                                                modelCollection,
                                                colliderStore,
                                                tracer,
                                                ribbonQuota );
    }

    if ( replayRuntime.Prediction().ragdollVisualsEnabled )
    {
        DrawReplayPredictionRagdollTorsoTrails( activePredictionFrames,
                                                activePredictionFrameCount,
                                                drawWindow.revealFrame,
                                                modelCollection,
                                                tracer,
                                                ribbonQuota );
    }
    if ( bufferComplete )
    {
        RetainReplayPredictionEndStateMarkers( replayRuntime.Prediction(),
                                               drawWindow.revealFrame,
                                               activePredictionFrames,
                                               activePredictionFrameCount );
    }
    DrawReplayPredictionRetainedMarkers( replayRuntime.Prediction(),
                                         usingBuildFrames,
                                         drawWindow.revealFrame,
                                         drawWindow.lastFrame,
                                         colliderStore,
                                         tracer );
    return true;
}


void RenderReplayPredictionVisualizer( ReplayRuntime& replayRuntime,
                                       PhysicsEngine& physicsEngine,
                                       const SceneEntityStore& entities,
                                       const EngineConfig& config,
                                       const SkullbonezCore::Physics::PhysicsWorldForces& worldForces,
                                       SkullbonezCore::Threading::WorkerPool& workerPool,
                                       bool scenePhysics,
                                       double fallbackSourceSimulationSeconds,
                                       double simulationTotalSeconds,
                                       RunEditorTracer& tracer,
                                       ReplayRibbonDrawQuota& ribbonQuota,
                                       const std::chrono::steady_clock::time_point& budgetStart,
                                       double budgetMilliseconds )
{
    PROFILE_SCOPED( "Frame/Replay/PathVisualizer/Prediction" );
    if ( !replayRuntime.Prediction().enabled )
    {
        if ( replayRuntime.Prediction().build.building )
        {
            replayRuntime.CancelPredictionJob( false );
        }
        const ColliderStore& colliderStore = PhysicsEngine::ReadColliders( physicsEngine );
        DrawReplayPredictionOverlay( replayRuntime, entities, colliderStore, tracer, ribbonQuota, budgetMilliseconds );
        return;
    }

    const ReplaySolverFrameSample* latest = replayRuntime.Solver().LatestSample();
    const ReplayFrameIndex latestFrame = latest ? latest->frameIndex : 0;
    const uint64_t latestHash = latest ? latest->solverHash : 0;
    const double now = simulationTotalSeconds;
    const bool sourceChanged =
        replayRuntime.Prediction().simulation.targetId.value != replayRuntime.PathVisualizer().targetId.value ||
        replayRuntime.Prediction().simulation.sourceFrameIndex != latestFrame ||
        replayRuntime.Prediction().simulation.sourceSolverHash != latestHash;
    const bool refreshDue =
        ( now - replayRuntime.Prediction().build.lastBuildTime ) >= REPLAY_PREDICTION_REFRESH_SECONDS;
    const bool hasCommittedPrediction = replayRuntime.Prediction().simulation.frames.size() >= 2;
    // Invariant: a committed prediction is a frozen future for the current
    // branch. Space-stepping the paused live scene changes solver frame/hash,
    // but must not redraw the preview; explicit dirty events such as branch,
    // target, horizon, or predict toggles are the only rebuild triggers.
    const bool allowAutomaticRefresh = !replayRuntime.Scrubber().liveAdvanceHeld && !hasCommittedPrediction;
    RunReplayPredictionState& prediction = replayRuntime.Prediction();
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
        if ( ReplayPredictionBudgetExpiredForPass( replayRuntime,
                                                   MainMemoryReplayBudgetPass::PredictionBegin,
                                                   budgetStart,
                                                   budgetMilliseconds ) )
        {
            return;
        }
        if ( prediction.build.dirty )
        {
            replayRuntime.RecordReplayTrajectoryRebuildCause( MainMemoryReplayRebuildCause::Dirty );
        }
        else
        {
            replayRuntime.RecordReplayTrajectoryRebuildCause( MainMemoryReplayRebuildCause::AutomaticRefresh );
        }
        const bool wasDirty = prediction.build.dirty;
        const bool wasPendingLatestRestart = prediction.build.pendingLatestRestart;
        if ( prediction.baseline.comparisonActive && !prediction.baseline.valid &&
             prediction.simulation.frames.size() >= 2 )
        {
            if ( !CaptureReplayPredictionBaselineSnapshot( prediction,
                                                           prediction.simulation.frames,
                                                           prediction.simulation.frames.size(),
                                                           replayRuntime.PathVisualizer().targetId,
                                                           replayRuntime.PathVisualizer().targetModelRow.value ) )
            {
                prediction.baseline.comparisonActive = false;
            }
        }
        const bool began = BeginReplayPredictionJob( replayRuntime,
                                                     physicsEngine,
                                                     entities,
                                                     config,
                                                     worldForces,
                                                     workerPool,
                                                     scenePhysics,
                                                     fallbackSourceSimulationSeconds,
                                                     simulationTotalSeconds,
                                                     latestFrame,
                                                     latestHash,
                                                     budgetStart,
                                                     budgetMilliseconds );
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
        if ( ReplayPredictionBudgetExpiredForPass( replayRuntime,
                                                   MainMemoryReplayBudgetPass::PredictionBegin,
                                                   budgetStart,
                                                   budgetMilliseconds ) )
        {
            return;
        }
    }
    const ColliderStore& colliderStore = PhysicsEngine::ReadColliders( physicsEngine );
    bool predictionCompletedThisPass = false;
    if ( replayRuntime.Prediction().build.building )
    {
        const double remainingMilliseconds = ReplayPredictionRemainingMilliseconds( budgetStart, budgetMilliseconds );
        if ( remainingMilliseconds > 0.0 )
        {
            const bool wasBuilding = replayRuntime.Prediction().build.building;
            StepReplayPredictionJob( replayRuntime,
                                     workerPool,
                                     simulationTotalSeconds,
                                     budgetStart,
                                     budgetMilliseconds );
            predictionCompletedThisPass =
                wasBuilding && replayRuntime.Prediction().build.complete && !replayRuntime.Prediction().build.building;
            (void)ReplayPredictionBudgetExpiredForPass( replayRuntime,
                                                        MainMemoryReplayBudgetPass::PredictionStep,
                                                        budgetStart,
                                                        budgetMilliseconds );
        }
        else
        {
            (void)ReplayPredictionBudgetExpiredForPass( replayRuntime,
                                                        MainMemoryReplayBudgetPass::PredictionStep,
                                                        budgetStart,
                                                        budgetMilliseconds );
        }
    }
    if ( predictionCompletedThisPass )
    {
        RebuildReplayPredictionCommittedTreeAfterWorkerCompletion( replayRuntime, entities );
    }

    // Why: prediction stepping and future-node discovery still share the
    // private-engine budget, but draw work is capped by frame-index strides and
    // the fixed ribbon quota so visible trajectory lines do not flicker under
    // load.
    DrawReplayPredictionOverlay( replayRuntime, entities, colliderStore, tracer, ribbonQuota, budgetMilliseconds );
}

} // namespace

namespace SkullbonezCore::Basics::ReplayOverlay
{
void RenderReplayPathVisualizer( const ReplayPathVisualizerRenderContext& context )
{
    PROFILE_SCOPED( "Frame/Replay/PathVisualizer" );
    // Concept: this marker owns replay visualizer budgeting.
    //
    // Prediction stepping plus future-node build work share the wall-clock
    // deadline. Visible trajectory drawing spends a fixed ribbon quota instead,
    // so completed segments do not flicker under transient load.
    const auto visualizerStart = std::chrono::steady_clock::now();
    ReplayRibbonDrawQuota ribbonQuota = BeginReplayRibbonDrawQuota( context.tracer );
    RenderReplayPredictionVisualizer( context.replayRuntime,
                                      context.physics,
                                      context.entities,
                                      context.config,
                                      context.worldForces,
                                      context.workerPool,
                                      context.scenePhysicsEnabled,
                                      context.simulationTimeSinceLastStart,
                                      context.simulationTotalTime,
                                      context.tracer,
                                      ribbonQuota,
                                      visualizerStart,
                                      REPLAY_PREDICTION_MAX_WORK_MILLISECONDS );
    const RunReplayPredictionState& prediction = context.replayRuntime.Prediction();
    if ( !prediction.enabled && prediction.simulation.frames.size() >= 2 &&
         context.replayRuntime.PathVisualizer().hasTarget && !context.replayRuntime.PathVisualizer().pastPathVisible &&
         prediction.simulation.targetId.value == context.replayRuntime.PathVisualizer().targetId.value )
    {
        // Why: Play disables prediction but keeps the committed path preview;
        // when the user has hidden the past lane, do not refresh retained store
        // data from the advancing live timeline behind that frozen preview.
        return;
    }
    if ( ReplayPredictionBudgetExpiredForPass( context.replayRuntime,
                                               MainMemoryReplayBudgetPass::RetainedRefresh,
                                               visualizerStart,
                                               REPLAY_PREDICTION_MAX_WORK_MILLISECONDS ) )
    {
        return;
    }

    if ( !context.replayRuntime.PathVisualizer().hasTarget )
    {
        context.replayRuntime.PathVisualizer().futureNodes.clear();
        return;
    }

    if ( !context.replayRuntime.PathVisualizer().pastPathVisible )
    {
        // Why: hiding the past lane must stop both drawing and the retained
        // node report. Prediction keeps its separate future-node cache.
        context.replayRuntime.PathVisualizer().futureNodes.clear();
        return;
    }

    if ( !context.replayRuntime.Solver().IsEnabled() )
    {
        return;
    }
    context.replayRuntime.RefreshPastTrajectoryStoreFromSolverSamples();

    if ( context.replayRuntime.PathVisualizer().targets.empty() &&
         context.replayRuntime.PathVisualizer().targetId.value != 0 )
    {
        if ( !ReserveReplayPredictionVector( context.replayRuntime.PathVisualizer().targets,
                                             REPLAY_PATH_MAX_ROOT_TARGETS,
                                             context.sceneCurrentFrame,
                                             "RunReplayPathVisualizer::targets" ) )
        {
            return;
        }
        RunReplayPathTarget target;
        target.id = context.replayRuntime.PathVisualizer().targetId;
        target.modelRow.value = context.replayRuntime.PathVisualizer().targetModelRow.value;
        if ( context.replayRuntime.PathVisualizer().targetName[0] != '\0' )
        {
            strncpy_s( target.name,
                       sizeof( target.name ),
                       context.replayRuntime.PathVisualizer().targetName,
                       _TRUNCATE );
        }
        context.replayRuntime.PathVisualizer().targets.push_back( target );
    }

    const ReplaySolverFrameSample* presentSample = context.replayRuntime.CurrentSolverScrubSample();
    if ( !presentSample )
    {
        presentSample = context.replayRuntime.Solver().LatestSample();
    }
    if ( !presentSample )
    {
        return;
    }

    const ReplayFrameIndex presentFrame = presentSample->frameIndex;
    context.replayRuntime.PathVisualizer().futureNodes.clear();
    const PhysicsBodyStore& bodyStore = Physics::PhysicsEngine::ReadBodies( context.physics );
    const ColliderStore& colliderStore = Physics::PhysicsEngine::ReadColliders( context.physics );
    for ( RunReplayPathTarget& target : context.replayRuntime.PathVisualizer().targets )
    {
        if ( target.id.value == 0 )
        {
            continue;
        }

        PROFILE_SCOPED( "Frame/Replay/PathVisualizer/RetainedTarget" );
        if ( target.id.value == context.replayRuntime.PathVisualizer().targetId.value )
        {
            PROFILE_SCOPED( "Frame/Replay/PathVisualizer/RetainedTarget/DrawRoot" );
            DrawReplayPastRootTrajectoryFromStore( context.replayRuntime.Prediction(),
                                                   target.id,
                                                   presentFrame,
                                                   context.tracer,
                                                   ribbonQuota );
        }

        {
            PROFILE_SCOPED( "Frame/Replay/PathVisualizer/RetainedTarget/DrawMarker" );
            ModelRowHint targetHint;
            targetHint.value = target.modelRow.value;
            int markerIndex = -1;
            const bool markerResolved =
                TryResolveReplayBodyModelIndex( bodyStore, target.id, targetHint, bodyStore.Count(), markerIndex );
            target.modelRow.value = targetHint.value;
            if ( target.id.value == context.replayRuntime.PathVisualizer().targetId.value )
            {
                context.replayRuntime.PathVisualizer().targetModelRow.value = targetHint.value;
            }
            if ( markerResolved )
            {
                TryAddReplayTargetMarkerFromStores( context.tracer, bodyStore, colliderStore, markerIndex );
            }
        }
    }
}
} // namespace SkullbonezCore::Basics::ReplayOverlay

void ReplayRuntime::RenderPathVisualizer( PhysicsEngine& physics,
                                          const SceneEntityStore& entities,
                                          const EngineConfig& config,
                                          const Physics::PhysicsWorldForces& worldForces,
                                          Threading::WorkerPool& workerPool,
                                          RunEditorTracer& tracer,
                                          bool scenePhysicsEnabled,
                                          int currentFrame,
                                          double frameSeconds,
                                          double totalSeconds )
{
    tracer.ClearReplayTrajectoryStats();
    const SkullbonezCore::Basics::ReplayOverlay::ReplayPathVisualizerRenderContext context{ *this,
                                                                                            physics,
                                                                                            entities,
                                                                                            config,
                                                                                            worldForces,
                                                                                            workerPool,
                                                                                            tracer,
                                                                                            scenePhysicsEnabled,
                                                                                            currentFrame,
                                                                                            frameSeconds,
                                                                                            totalSeconds };
    SkullbonezCore::Basics::ReplayOverlay::RenderReplayPathVisualizer( context );
    RecordReplayTrajectoryFrameStats( tracer.ReplayTrajectoryStats() );
}


void ReplayRuntime::RenderCauseFocusOverlay( const PhysicsBodyStore& bodyStore,
                                             const ColliderStore& colliderStore,
                                             const SceneEntityStore& entities,
                                             RunEditorTracer& tracer )
{
    if ( Camera().focusKind == RunReplayCameraFocusKind::None )
    {
        return;
    }

    if ( Camera().focusKind == RunReplayCameraFocusKind::Body )
    {
        ModelRowHint focusHint;
        focusHint.value = Camera().focusModelRow.value;
        int focusedModelIndex = -1;
        if ( TryResolveReplayBodyModelIndex( bodyStore,
                                             Camera().focusedId,
                                             focusHint,
                                             bodyStore.Count(),
                                             focusedModelIndex ) )
        {
            Camera().focusModelRow.value = focusHint.value;
            TryAddReplayTargetMarkerFromStores( tracer, bodyStore, colliderStore, focusedModelIndex );
            return;
        }
        Camera().focusModelRow.value = focusHint.value;
    }

    if ( Camera().focusKind == RunReplayCameraFocusKind::Manifold ||
         Camera().focusKind == RunReplayCameraFocusKind::PredictionContact ||
         Camera().focusKind == RunReplayCameraFocusKind::PredictionMotion )
    {
        if ( Camera().focusKind == RunReplayCameraFocusKind::Manifold )
        {
            const ReplaySolverFrameSample* sample = CurrentSolverScrubSample();
            if ( sample )
            {
                const ReplaySolverBodySample* focusedBody = FindReplayBodyById( *sample, Camera().focusedId );
                const ReplaySolverBodySample* counterpartBody = FindReplayBodyById( *sample, Camera().counterpartId );
                if ( focusedBody )
                {
                    bool drewContact = false;
                    for ( const ReplaySolverPersistentContactSample& contact :
                          sample->worldSnapshot.persistentContacts )
                    {
                        if ( !ReplayContactHasModelIndex( contact, focusedBody->modelRow.value ) )
                        {
                            continue;
                        }
                        const int otherModelIndex =
                            ReplayContactOtherModelIndex( contact, focusedBody->modelRow.value );
                        const bool terrain = contact.isTerrain || otherModelIndex < 0;
                        if ( Camera().focusTerrain != terrain )
                        {
                            continue;
                        }
                        if ( !terrain && ( !counterpartBody || counterpartBody->modelRow.value != otherModelIndex ) )
                        {
                            continue;
                        }
                        tracer.AddReplayContactMarker(
                            ReplayContactPoint( *sample, contact ),
                            ReplayContactNormalForModel( contact, focusedBody->modelRow.value ),
                            0.1f,
                            0.95f,
                            1.0f );
                        drewContact = true;
                    }
                    if ( drewContact )
                    {
                        return;
                    }
                }
            }
        }
        else if ( Camera().focusKind == RunReplayCameraFocusKind::PredictionContact )
        {
            ReplayFrameIndex focusFrame = 0;
            int focusedModelIndex = Camera().focusModelRow.value;
            int counterpartModelIndex = Camera().focusCounterpartModelRow.value;

            const RunReplayCauseTreeState& causeTree = CauseTree();
            if ( causeTree.selectedRow >= 0 && causeTree.selectedRow < static_cast<int>( causeTree.rows.size() ) )
            {
                const RunReplayCauseTreeRow& row = causeTree.rows[static_cast<std::size_t>( causeTree.selectedRow )];
                if ( row.kind == RunReplayCauseTreeRowKind::PredictionContact &&
                     row.id.value == Camera().focusedId.value )
                {
                    focusFrame = row.firstFrame;
                    focusedModelIndex = row.modelRow.value;
                    counterpartModelIndex = row.counterpartModelRow.value;
                }
            }
            else if ( Camera().focusContactIndex >= 0 &&
                      Camera().focusContactIndex < static_cast<int>( Prediction().futureNodeCache.futureNodes.size() ) )
            {
                const RunReplayPathTraceNode& node =
                    Prediction().futureNodeCache.futureNodes[static_cast<std::size_t>( Camera().focusContactIndex )];
                if ( node.id.value == Camera().focusedId.value && node.contactDerived )
                {
                    focusFrame = node.firstFrame;
                    focusedModelIndex = node.modelRow.value;
                    counterpartModelIndex = node.parentModelRow.value;
                }
            }

            bool drewPredictionManifold = false;
            const std::vector<RunReplayPredictionFrame>& frames = ActivePredictionFrames();
            for ( const RunReplayPredictionFrame& frame : frames )
            {
                if ( frame.frameIndex != focusFrame )
                {
                    continue;
                }

                // Why: prediction contacts are selected from the future-node
                // tree, but the full manifold lives in the frame's debug
                // contacts. Match by the selected child/parent body pair so the
                // manifold marker remains visible while that collision row is
                // selected.
                for ( const PhysicsDebugContact& contact : frame.debugContacts )
                {
                    const int contactModelA = ReplayRagdollTorsoModelIndexForPart( entities, contact.bodyA );
                    const int contactModelB = contact.bodyB >= 0
                                                  ? ReplayRagdollTorsoModelIndexForPart( entities, contact.bodyB )
                                                  : contact.bodyB;
                    const bool selectedPairAB = contactModelA == focusedModelIndex &&
                                                ( counterpartModelIndex < 0 || contactModelB == counterpartModelIndex );
                    const bool selectedPairBA = contactModelB == focusedModelIndex &&
                                                ( counterpartModelIndex < 0 || contactModelA == counterpartModelIndex );
                    if ( !selectedPairAB && !selectedPairBA )
                    {
                        continue;
                    }

                    Vector3 normal = contact.normal;
                    if ( selectedPairBA && contactModelB >= 0 )
                    {
                        normal = normal * -1.0f;
                    }
                    tracer.AddReplayContactMarker( contact.point,
                                                   ReplayNormalizeOr( normal, Vector3( 0.0f, 1.0f, 0.0f ) ),
                                                   0.1f,
                                                   0.95f,
                                                   1.0f );
                    drewPredictionManifold = true;
                }
                break;
            }
            if ( drewPredictionManifold )
            {
                return;
            }
        }
        tracer.AddReplayContactMarker( Camera().targetPoint, Camera().targetNormal, 0.1f, 0.95f, 1.0f );
        return;
    }

    if ( Camera().focusKind == RunReplayCameraFocusKind::SolverRow )
    {
        tracer.AddReplayContactMarker( Camera().targetPoint, Camera().targetNormal, 0.2f, 0.85f, 1.0f );
        tracer.AddReplayImpulseVector( Camera().targetPoint, Camera().impulseVector, 1.0f, 0.32f, 0.12f );
    }
}
