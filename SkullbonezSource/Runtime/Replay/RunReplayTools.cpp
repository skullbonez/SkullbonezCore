/*
File: SkullbonezSource/Runtime/Replay/RunReplayTools.cpp
Purpose:
  Owns replay path visualization, cause-focus overlays, and prediction-preview
  helpers as one real translation unit after deleting the replay text splices.

Mental model:
  Replay tools read two timelines. Retained solver samples describe what already
  happened; prediction samples advance a private replay-owned physics engine.
  The renderer only receives lightweight overlay geometry.

Glossary:
  Path visualizer: Overlay that draws past/future body trajectories and contact
    handoffs.
  Replay target marker: Overlay outline/ring drawn around the replay-selected
    body from live body/collider store rows.
  Prediction slice: Time-budgeted replay preview work performed inside a render
    frame.
  Prediction physics tick: Replay-owned fixed step against the private
    prediction engine.
  Future node: Body discovered by following contacts or predicted movement
    outward from a selected root body.
  Replay ribbon: Camera-facing overlay stroke emitted through RunEditorTracer's
    fixed-capacity ordinary or priority ribbon buffers.
  Ribbon quota: Frame-local count of ordinary replay ribbon records that path
    drawing may spend before it stops emitting trajectory segments.
  ReplayBodyId: Stable runtime id used across retained samples even when vector
    indices are only local hints.
  Model row hint: Cached live body row paired with ReplayBodyId; replay tools
    may keep it only as a repairable lookup shortcut.
  Solver snapshot: Physics cache state that must be restored to make the next
    fixed step reproduce.
  WorkerPool: Persistent engine worker threads used only for large, independent
    fork-join loops.

Invariants:
  - Prediction must never write live physics stores; private engine state owns
    all future ticks and samples.
  - Prediction stepping and future-node discovery share a per-frame wall-clock
    budget; visible path drawing spends the tracer's fixed ribbon quota.
  - Physics steps stay serial; only read-only body capture is parallelized.

Related:
  - SkullbonezSource/Runtime/Replay/RunReplayScrubberTools.cpp
  - SkullbonezSource/Runtime/Replay/RunReplayCauseTreeTools.cpp
  - Agentic/Reference/runtime-reference.md
  - Agentic/Reference/comment-style-guide.md
*/
#include "../RunInternal.h"
#include "../Editor/EditorHullAssets.h"
#include "../InputController.h"
#include "ReplayInteractionController.h"
#include "ReplayOverlayLayout.h"
#include "ReplayOverlayRenderer.h"
#include "RunReplayImportExport.h"
#include "../RuntimePickService.h"
#include "../Allocation/RuntimeAllocationTracker.h"
#include "../Allocation/RuntimeReserveAllocator.h"
#include "../../Physics/ColliderStore.h"
#include "../../Physics/PhysicsBodyStore.h"
#include "../../Physics/PhysicsMass.h"
#include "../../Physics/PhysicsTimestep.h"
#include "../RuntimeFileWriter.h"
#include "../../Core/WorkerPool.h"
#include "../../UI/UIInput.h"
#include "../../UI/UILayout.h"

#include <algorithm>
#include <chrono>
#include <cfloat>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <limits>

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
namespace RuntimeAllocation = SkullbonezCore::Runtime::Allocation;

namespace
{
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
    // markers read store rows. This avoids scanning the GameModel mirror just
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
constexpr std::size_t REPLAY_RIBBON_SEGMENTS_PER_PATH_SEGMENT = 2;
constexpr float REPLAY_PATH_MIN_SEGMENT_DISTANCE_SQ = 0.0001f;
// Why: sleeping or contact-propagated bodies can wake without translating. Child
// prediction outlines wait for real linear speed so the wall blooms outward
// only when bricks are actually about to move.
constexpr float REPLAY_PREDICTION_CHILD_LINEAR_SPEED_SQ = 8.0f * 8.0f;

// Invariant: Worker dispatch is only worth it for large body snapshots. Small
// scenes stay serial so replay overlays do not pay thread wakeup cost to copy a
// few kilobytes.
constexpr int REPLAY_PREDICTION_PARALLEL_BODY_MIN = 2048;

struct ReplayRibbonDrawQuota
{
    // Counts internal ribbon records, not logical trajectory lines. Each
    // AddReplayPathSegment call emits a glow and a core record.
    std::size_t remainingRibbonSegments = 0;
    bool exhausted = false;
};

ReplayRibbonDrawQuota BeginReplayRibbonDrawQuota( const RunEditorTracer& tracer )
{
    ReplayRibbonDrawQuota quota;
    quota.remainingRibbonSegments = tracer.ReplayPathRibbonSegmentCapacityRemaining();
    return quota;
}

bool ReplayRibbonDrawQuotaExhausted( const ReplayRibbonDrawQuota* quota )
{
    return quota && quota->exhausted;
}

bool TryReserveReplayPathRibbonSegment( ReplayRibbonDrawQuota* quota )
{
    if ( !quota )
    {
        return true;
    }
    if ( quota->remainingRibbonSegments < REPLAY_RIBBON_SEGMENTS_PER_PATH_SEGMENT )
    {
        quota->exhausted = true;
        quota->remainingRibbonSegments = 0;
        return false;
    }

    quota->remainingRibbonSegments -= REPLAY_RIBBON_SEGMENTS_PER_PATH_SEGMENT;
    return true;
}

bool TryAddReplayPathSegment( RunEditorTracer& tracer,
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
            quota->exhausted = true;
            quota->remainingRibbonSegments = 0;
        }
        return false;
    }
    if ( !TryReserveReplayPathRibbonSegment( quota ) )
    {
        return false;
    }

    tracer.AddReplayPathSegment( start, end, r, g, b, lane );
    return true;
}

bool TryAddReplayBaselinePathSegment( RunEditorTracer& tracer,
                                      ReplayRibbonDrawQuota* quota,
                                      const Vector3& start,
                                      const Vector3& end )
{
    if ( tracer.ReplayPathRibbonSegmentCapacityRemaining() < REPLAY_RIBBON_SEGMENTS_PER_PATH_SEGMENT )
    {
        if ( quota )
        {
            quota->exhausted = true;
            quota->remainingRibbonSegments = 0;
        }
        return false;
    }
    if ( !TryReserveReplayPathRibbonSegment( quota ) )
    {
        return false;
    }

    tracer.AddReplayBaselinePathSegment( start, end );
    return true;
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

constexpr const char* REPLAY_PREDICTION_RESERVE_OWNER = "replay_prediction_working_set";
constexpr int REPLAY_PREDICTION_FRAME_CAPACITY =
    static_cast<int>( REPLAY_PREDICTION_MAX_SECONDS / PHYSICS_FIXED_DT ) + 2;
constexpr int REPLAY_PREDICTION_PATH_BUDGET = 100;
constexpr int REPLAY_PREDICTION_RESERVE_HARD_BYTES = 256 * 1024 * 1024;
constexpr std::size_t REPLAY_PREDICTION_DEBUG_CONTACT_INITIAL_MIN = 512u;
constexpr std::size_t REPLAY_PREDICTION_DEBUG_CONTACT_INITIAL_MAX = 2048u;
constexpr std::size_t REPLAY_PREDICTION_DEBUG_CONTACT_GROWTH_CHUNK = 4096u;
// Runtime allocation policy: prediction scratch can grow as the user explores
// larger retained paths. The registered hard cap is a real byte ceiling, not a
// theoretical element-count product; growth count is telemetry so interactive
// replay does not trip a per-run count fuse.
constexpr int REPLAY_PREDICTION_RESERVE_GROWTH_LIMIT = RuntimeAllocation::RUNTIME_RESERVE_REPLAY_GROWTH_LIMIT_UNBOUNDED;

RuntimeAllocation::RuntimeReserveOwnerHandle ReplayPredictionReserveOwner()
{
    static const RuntimeAllocation::RuntimeReserveOwnerHandle owner =
        RuntimeAllocation::RuntimeReserveAllocator::RegisterOwner(
            { REPLAY_PREDICTION_RESERVE_OWNER,
              RuntimeAllocation::RuntimeReserveSubsystem::Replay,
              RuntimeAllocation::RuntimeReservePhase::Replay,
              0,
              REPLAY_PREDICTION_RESERVE_HARD_BYTES,
              REPLAY_PREDICTION_RESERVE_GROWTH_LIMIT,
              true,
              "replay prediction supports large retained path visualization under a hard byte budget" } );
    return owner;
}

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
    bytes += static_cast<uint64_t>( engine.BodyStore().Records().capacity() ) * sizeof( PhysicsBodyRecord );
    bytes += static_cast<uint64_t>( engine.Colliders().Records().capacity() ) * sizeof( ColliderRecord );
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

    const RuntimeAllocation::RuntimeReserveOwnerHandle owner = ReplayPredictionReserveOwner();
    const RuntimeAllocation::RuntimeReserveGrowthRequest request = { REPLAY_PREDICTION_RESERVE_OWNER,
                                                                     targetName,
                                                                     RuntimeAllocation::RuntimeReservePhase::Replay,
                                                                     frameNumber,
                                                                     static_cast<int>( oldBytes ),
                                                                     static_cast<int>( requestedBytes ),
                                                                     1 };
    const RuntimeAllocation::RuntimeReserveGrowthResult result =
        RuntimeAllocation::RuntimeReserveAllocator::RequestGrowth( owner, request );
    if ( !result.granted )
    {
        return false;
    }

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

    const RuntimeAllocation::RuntimeReserveOwnerHandle owner = ReplayPredictionReserveOwner();
    const RuntimeAllocation::RuntimeReserveGrowthRequest request = { REPLAY_PREDICTION_RESERVE_OWNER,
                                                                     targetName,
                                                                     RuntimeAllocation::RuntimeReservePhase::Replay,
                                                                     frameNumber,
                                                                     static_cast<int>( oldBytes ),
                                                                     static_cast<int>( requestedBytes ),
                                                                     1 };
    const RuntimeAllocation::RuntimeReserveGrowthResult result =
        RuntimeAllocation::RuntimeReserveAllocator::RequestGrowth( owner, request );
    if ( !result.granted )
    {
        return false;
    }

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
    prediction.futureNodeCache.futureNodesBuiltRagdollVisuals = prediction.ragdollVisualsEnabled;
    prediction.futureNodeCache.futureNodesBuiltFromBuildFrames = false;
    prediction.futureNodeCache.futureNodesCacheValid = false;
    prediction.futureNodeCache.retainedMarkerCount = 0;
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
        if ( body.modelIndex == modelIndex )
        {
            return &body;
        }
    }

    for ( const BodySample& body : sample.bodies )
    {
        if ( body.modelIndex == modelIndex )
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

ReplayBodyId ReplayBodyIdForModelIndex( const ReplaySolverFrameSample& sample, int modelIndex )
{
    return ReplayBodyIdForModelIndexInSample<ReplaySolverFrameSample, ReplaySolverBodySample, false>( sample,
                                                                                                      modelIndex );
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

// Why: modelIndex is a cache hint, not identity. The replay id check protects
// against stale hints after body lists are rebuilt or ragdoll parts are folded
// to their collection root.
const ReplaySolverBodySample*
FindReplayBodyByIdWithHint( const ReplaySolverFrameSample& sample, ReplayBodyId id, int modelIndex )
{
    if ( const ReplaySolverBodySample* body = FindReplayBodyByModelIndex( sample, modelIndex ) )
    {
        if ( body->id.value == id.value )
        {
            return body;
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

bool ReplayModelIndexIsRagdollPart( const SkullbonezCore::GameObjects::GameModelCollection& collection, int modelIndex )
{
    return ReplayModelIsRagdollPart( collection, modelIndex );
}

int ReplayRagdollTorsoModelIndexForPart( const SkullbonezCore::GameObjects::GameModelCollection& collection,
                                         int modelIndex )
{
    return collection.RagdollRootModelIndexForPart( modelIndex );
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

std::size_t ReplayPathStrideForSampleCount( std::size_t sampleCount )
{
    if ( sampleCount <= REPLAY_PATH_MAX_SEGMENTS )
    {
        return 1;
    }
    return ( sampleCount + REPLAY_PATH_MAX_SEGMENTS - 1 ) / REPLAY_PATH_MAX_SEGMENTS;
}

void ClearReplayPredictionBaseline( ReplayPredictionBaselineSnapshot& baseline )
{
    baseline.valid = false;
    baseline.comparisonActive = false;
    baseline.rootId = ReplayBodyId{};
    baseline.rootModelIndex = -1;
    baseline.lastFrame = 0;
    baseline.rootPolyline.clear();
    baseline.bodyPoses.clear();
    baseline.divergenceValid = false;
    baseline.divergenceUnits = 0.0f;
}

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
    prediction.baseline.rootModelIndex = rootModelIndex;
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
        if ( !ReplayPredictionBodyRestingPose( frames,
                                               frameCount,
                                               body.id,
                                               body.modelIndex,
                                               restPosition,
                                               restOrientation ) )
        {
            continue;
        }

        ReplayPredictionBaselineBodyPose pose;
        pose.id = body.id;
        pose.modelIndex = body.modelIndex;
        pose.hasEntryPose = true;
        pose.hasRestPose = true;
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
    return prediction.baseline.valid;
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
        if ( !baselinePose.hasRestPose || baselinePose.id.value == 0 )
        {
            continue;
        }

        Vector3 restPosition = SkullbonezCore::Math::Vector::ZERO_VECTOR;
        Quaternion restOrientation = IDENTITY_QUATERNION;
        if ( !ReplayPredictionBodyRestingPose( frames,
                                               frameCount,
                                               baselinePose.id,
                                               baselinePose.modelIndex,
                                               restPosition,
                                               restOrientation ) )
        {
            continue;
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
void DrawReplayPredictionBaselineSnapshot( const ReplayPredictionBaselineSnapshot& baseline,
                                           const ColliderStore& colliderStore,
                                           RunEditorTracer& tracer,
                                           ReplayRibbonDrawQuota& ribbonQuota )
{
    if ( !baseline.valid )
    {
        return;
    }

    for ( std::size_t i = 1; i < baseline.rootPolyline.size(); ++i )
    {
        const Vector3& previous = baseline.rootPolyline[i - 1].position;
        const Vector3& current = baseline.rootPolyline[i].position;
        if ( VectorMagSquared( current - previous ) > REPLAY_PATH_MIN_SEGMENT_DISTANCE_SQ )
        {
            if ( !TryAddReplayBaselinePathSegment( tracer, &ribbonQuota, previous, current ) )
            {
                break;
            }
        }
    }

    for ( const ReplayPredictionBaselineBodyPose& pose : baseline.bodyPoses )
    {
        const ColliderRecord* collider = ReplayColliderRecordForModelIndex( &colliderStore, pose.modelIndex );
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

struct ReplayPathBoundsContext
{
    bool hasSample = false;
    ReplayFrameIndex firstFrame = 0;
    ReplayFrameIndex lastFrame = 0;
};

void CaptureReplayPathBounds( const ReplaySolverFrameSample& sample, void* userData )
{
    ReplayPathBoundsContext& context = *static_cast<ReplayPathBoundsContext*>( userData );
    if ( !context.hasSample )
    {
        context.hasSample = true;
        context.firstFrame = sample.frameIndex;
    }
    context.lastFrame = sample.frameIndex;
}

struct ReplayPathFutureContext
{
    RunReplayPathVisualizerState* visualizer = nullptr;
    const SkullbonezCore::GameObjects::GameModelCollection* collection = nullptr;
    const std::chrono::steady_clock::time_point* budgetStart = nullptr;
    ReplayBodyId rootId;
    ReplayFrameIndex presentFrame = 0;
    double budgetMilliseconds = 0.0;
    bool includeRagdollVisuals = true;
    bool budgetExpired = false;
};

// Why: the recorder visitor API cannot early-out. The callback records budget
// expiry in the context and turns later visits into cheap no-ops.
bool ReplayPathContextBudgetExpired( ReplayPathFutureContext& context )
{
    if ( context.budgetStart && ReplayPredictionBudgetExpired( *context.budgetStart, context.budgetMilliseconds ) )
    {
        context.budgetExpired = true;
    }
    return context.budgetExpired;
}

// Concept: retained replay and prediction use the same future-node tree rules.
//
// The owner boundary differs: retained replay writes to the visualizer cache,
// while prediction can build into scratch and later replace a motion-inferred
// child with a contact-derived child. These helpers keep that policy at the
// wrapper edge instead of duplicating the contact traversal.
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
    node.modelIndex = modelIndex;
    node.parentModelIndex = parentModelIndex;
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

template <typename ContactRange,
          typename BodyIdResolver,
          typename DepthResolver,
          typename NodeAdder,
          typename BudgetExpired>
bool BuildReplayFutureNodesFromContacts( const ContactRange& contacts,
                                         ReplayFrameIndex frameIndex,
                                         std::size_t startContactIndex,
                                         const SkullbonezCore::GameObjects::GameModelCollection* collection,
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

bool TryGetReplayFutureDepth( const ReplayPathFutureContext& context,
                              ReplayBodyId id,
                              ReplayFrameIndex frame,
                              int& outDepth )
{
    return TryGetReplayFutureDepthInNodes( context.visualizer->futureNodes,
                                           context.rootId,
                                           context.presentFrame,
                                           true,
                                           id,
                                           frame,
                                           outDepth );
}

RunReplayPathTarget* FindReplayPathTarget( RunReplayPathVisualizerState& visualizer, ReplayBodyId id )
{
    for ( RunReplayPathTarget& target : visualizer.targets )
    {
        if ( target.id.value == id.value )
        {
            return &target;
        }
    }
    return nullptr;
}

void ApplyPrimaryReplayPathTarget( RunReplayPathVisualizerState& visualizer,
                                   ReplayBodyId id,
                                   int modelIndex,
                                   const char* name )
{
    visualizer.hasTarget = id.value != 0;
    visualizer.targetId = id;
    visualizer.targetModelIndex = modelIndex;
    visualizer.targetName[0] = '\0';
    if ( name && name[0] != '\0' )
    {
        strncpy_s( visualizer.targetName, sizeof( visualizer.targetName ), name, _TRUNCATE );
    }
}

void AddReplayFutureNode( ReplayPathFutureContext& context,
                          ReplayBodyId parentId,
                          int parentModelIndex,
                          ReplayBodyId id,
                          int modelIndex,
                          ReplayFrameIndex firstFrame,
                          const Vector3& contactPoint,
                          const Vector3& contactNormal,
                          int depth )
{
    AddReplayFutureNodeToNodes( context.visualizer->futureNodes,
                                context.rootId,
                                parentId,
                                parentModelIndex,
                                id,
                                modelIndex,
                                firstFrame,
                                contactPoint,
                                contactNormal,
                                depth,
                                true,
                                false );
}

void BuildReplayFutureNodes( const ReplaySolverFrameSample& sample, void* userData )
{
    ReplayPathFutureContext& context = *static_cast<ReplayPathFutureContext*>( userData );
    if ( !context.visualizer || sample.frameIndex < context.presentFrame || ReplayPathContextBudgetExpired( context ) )
    {
        return;
    }

    std::size_t nextContactIndex = 0;
    (void)BuildReplayFutureNodesFromContacts(
        sample.worldSnapshot.debugContacts,
        sample.frameIndex,
        0,
        context.collection,
        context.includeRagdollVisuals,
        [&]( int modelIndex ) { return ReplayBodyIdForModelIndex( sample, modelIndex ); },
        [&]( ReplayBodyId id, ReplayFrameIndex frameIndex, int& outDepth )
        { return TryGetReplayFutureDepth( context, id, frameIndex, outDepth ); },
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
            (void)contactDerived;
            AddReplayFutureNode( context,
                                 parentId,
                                 parentModelIndex,
                                 id,
                                 modelIndex,
                                 firstFrame,
                                 contactPoint,
                                 contactNormal,
                                 depth );
        },
        [&]() { return ReplayPathContextBudgetExpired( context ); },
        nextContactIndex );
}

// Invariant: path thinning is anchored to solver frame indices, not visitor
// ordinal. Partial scans may resume at different offsets, but the same replay
// tick must always keep or drop the same visual segment.
bool ShouldDrawReplayPathFrame( ReplayFrameIndex frameIndex, std::size_t stride )
{
    return stride <= 1 || ( frameIndex % static_cast<ReplayFrameIndex>( stride ) ) == 0;
}

struct ReplayPathRootDrawContext
{
    RunEditorTracer* tracer = nullptr;
    ReplayRibbonDrawQuota* ribbonQuota = nullptr;
    ReplayBodyId rootId;
    ReplayFrameIndex firstFrame = 0;
    ReplayFrameIndex presentFrame = 0;
    ReplayFrameIndex lastFrame = 0;
    std::size_t sampleStride = 1;
    bool hasPastPrevious = false;
    bool hasFuturePrevious = false;
    Vector3 pastPrevious = SkullbonezCore::Math::Vector::ZERO_VECTOR;
    Vector3 futurePrevious = SkullbonezCore::Math::Vector::ZERO_VECTOR;
};

void DrawReplayRootPath( const ReplaySolverFrameSample& sample, void* userData )
{
    ReplayPathRootDrawContext& context = *static_cast<ReplayPathRootDrawContext*>( userData );
    if ( ReplayRibbonDrawQuotaExhausted( context.ribbonQuota ) )
    {
        return;
    }
    if ( sample.frameIndex != context.presentFrame && sample.frameIndex != context.lastFrame &&
         !ShouldDrawReplayPathFrame( sample.frameIndex, context.sampleStride ) )
    {
        return;
    }

    const ReplaySolverBodySample* body = FindReplayBodyById( sample, context.rootId );
    if ( !body )
    {
        return;
    }

    if ( sample.frameIndex <= context.presentFrame )
    {
        if ( context.hasPastPrevious &&
             VectorMagSquared( body->position - context.pastPrevious ) > REPLAY_PATH_MIN_SEGMENT_DISTANCE_SQ )
        {
            const float t = ReplayPathFrameT( sample.frameIndex, context.firstFrame, context.presentFrame );
            if ( !TryAddReplayPathSegment( *context.tracer,
                                           context.ribbonQuota,
                                           context.pastPrevious,
                                           body->position,
                                           1.0f,
                                           t,
                                           t,
                                           MainMemoryReplayTrajectoryLane::PastRoot ) )
            {
                return;
            }
        }
        context.pastPrevious = body->position;
        context.hasPastPrevious = true;
    }

    if ( sample.frameIndex >= context.presentFrame )
    {
        if ( context.hasFuturePrevious &&
             VectorMagSquared( body->position - context.futurePrevious ) > REPLAY_PATH_MIN_SEGMENT_DISTANCE_SQ )
        {
            const float t = ReplayPathFrameT( sample.frameIndex, context.presentFrame, context.lastFrame );
            if ( !TryAddReplayPathSegment( *context.tracer,
                                           context.ribbonQuota,
                                           context.futurePrevious,
                                           body->position,
                                           1.0f - t,
                                           1.0f,
                                           1.0f - t,
                                           MainMemoryReplayTrajectoryLane::FutureRoot ) )
            {
                return;
            }
        }
        context.futurePrevious = body->position;
        context.hasFuturePrevious = true;
    }
}

struct ReplayPathChildDrawState
{
    RunReplayPathTraceNode node;
    bool active = false;
    bool hasIncomingPrevious = false;
    bool hasPrevious = false;
    bool hasMarkerPose = false;
    int markerModelIndex = -1;
    Vector3 incomingPrevious = SkullbonezCore::Math::Vector::ZERO_VECTOR;
    Vector3 previous = SkullbonezCore::Math::Vector::ZERO_VECTOR;
    Vector3 markerPosition = SkullbonezCore::Math::Vector::ZERO_VECTOR;
    Quaternion markerOrientation = IDENTITY_QUATERNION;
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
    RunEditorTracer* tracer = nullptr;
    const ColliderStore* colliderStore = nullptr;
    ReplayRibbonDrawQuota* ribbonQuota = nullptr;
    std::array<ReplayPathChildDrawState, REPLAY_PATH_MAX_FUTURE_NODES> nodes = {};
    std::size_t nodeCount = 0;
    ReplayFrameIndex presentFrame = 0;
    ReplayFrameIndex lastFrame = 0;
    std::size_t sampleStride = 1;
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
                marker.modelIndex = modelIndex;
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
    marker.modelIndex = modelIndex;
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

void DrawReplayPredictionRetainedMarkerTrail( const ReplayPredictionRetainedMarker& marker,
                                              const std::vector<RunReplayPredictionFrame>& frames,
                                              std::size_t frameCount,
                                              ReplayFrameIndex revealFrame,
                                              std::size_t trailOrdinal,
                                              RunEditorTracer& tracer )
{
    frameCount = (std::min)( frameCount, frames.size() );
    if ( !marker.hasEntryPose || marker.id.value == 0 || frameCount < 2 )
    {
        return;
    }

    const ReplayFrameIndex lastFrame = frames[frameCount - 1].frameIndex;
    const std::size_t sampleStride = ReplayRetainedMarkerTrailStrideForFrameCount( frameCount );
    bool hasPrevious = true;
    Vector3 previous = marker.entryPosition;
    const bool horizonGhost = marker.hasHorizonPose && !marker.hasRestPose;

    for ( std::size_t frameSlot = 1; frameSlot < frameCount; ++frameSlot )
    {
        const RunReplayPredictionFrame& frame = frames[frameSlot];
        if ( frame.frameIndex > revealFrame )
        {
            break;
        }

        // Invariant: retained marker trails are sampled polylines, never a
        // direct entry-to-rest chord. Always include the visible reveal edge and
        // completed horizon endpoint, then thin the interior samples.
        const bool endpointFrame = frame.frameIndex == revealFrame || frame.frameIndex == lastFrame;
        if ( !endpointFrame && !ShouldDrawReplayPathFrame( frame.frameIndex, sampleStride ) )
        {
            continue;
        }

        const RunReplayPredictionBodySample* body =
            FindReplayPredictionBodyByIdWithHint( frame, marker.id, marker.modelIndex );
        if ( !body )
        {
            continue;
        }

        if ( hasPrevious && VectorMagSquared( body->position - previous ) > REPLAY_PATH_MIN_SEGMENT_DISTANCE_SQ )
        {
            const float t = ReplayPathFrameT( frame.frameIndex, 0, lastFrame );
            float r = 0.82f;
            float g = 0.82f;
            float b = 0.88f;
            ReplayRetainedMarkerTrailColor( trailOrdinal, t, horizonGhost, r, g, b );
            tracer.AddReplayCausalTrailSegment( previous, body->position, r, g, b );
        }
        previous = body->position;
        hasPrevious = true;
    }
}

void DrawReplayPredictionRetainedMarkers( const RunReplayPredictionState& prediction,
                                          const std::vector<RunReplayPredictionFrame>& frames,
                                          std::size_t frameCount,
                                          ReplayFrameIndex revealFrame,
                                          const ColliderStore& colliderStore,
                                          RunEditorTracer& tracer )
{
    // Invariant: marker emission is bounded by MAX_GAME_MODELS and independent
    // of the visualizer budget. Lines may degrade under load; already-revealed
    // yellow/grey boxes must not.
    for ( std::size_t i = 0; i < prediction.futureNodeCache.retainedMarkerCount; ++i )
    {
        const ReplayPredictionRetainedMarker& marker = prediction.futureNodeCache.retainedMarkers[i];
        const ColliderRecord* collider = ReplayColliderRecordForModelIndex( &colliderStore, marker.modelIndex );
        if ( !collider )
        {
            continue;
        }
        DrawReplayPredictionRetainedMarkerTrail( marker, frames, frameCount, revealFrame, i, tracer );
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
                                              marker.modelIndex,
                                              restPosition,
                                              restOrientation ) )
        {
            RetainReplayPredictionRestMarker( prediction, marker.id, marker.modelIndex, restPosition, restOrientation );
            continue;
        }

        const RunReplayPredictionBodySample* finalBody =
            FindReplayPredictionBodyByIdWithHint( completeFrames[completeFrameCount - 1],
                                                  marker.id,
                                                  marker.modelIndex );
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
                                             finalBody->modelIndex,
                                             finalBody->position,
                                             finalBody->orientation );
    }
}

void CaptureReplayChildMarkerPose( ReplayPathChildDrawState& drawState,
                                   const Vector3& position,
                                   const Quaternion& orientation,
                                   int modelIndex )
{
    drawState.markerPosition = position;
    drawState.markerOrientation = orientation;
    drawState.markerModelIndex = modelIndex;
    drawState.hasMarkerPose = true;
}

void DrawReplayChildFinalMarkers( ReplayPathChildDrawContext& context )
{
    // Why: downstream body markers summarize where each transferred body ends
    // up in the visible prefix. Stamping contact-time poses made boxes look cut
    // short while their gray future trails continued past the outline.
    for ( std::size_t i = 0; i < context.nodeCount; ++i )
    {
        const ReplayPathChildDrawState& drawState = context.nodes[i];
        if ( !drawState.hasMarkerPose )
        {
            continue;
        }

        if ( const ColliderRecord* collider =
                 ReplayColliderRecordForModelIndex( context.colliderStore, drawState.markerModelIndex ) )
        {
            context.tracer->AddReplayFutureTargetMarker( drawState.markerPosition,
                                                         drawState.markerOrientation,
                                                         collider->shape,
                                                         drawState.node.depth );
        }
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
                                               drawState.node.modelIndex,
                                               restPosition,
                                               restOrientation ) )
        {
            continue;
        }
        RetainReplayPredictionRestMarker( prediction,
                                          drawState.node.id,
                                          drawState.node.modelIndex,
                                          restPosition,
                                          restOrientation );
    }
}

void ReplayChildIncomingColor( int depth, float t, float& r, float& g, float& b )
{
    const float depthFade = std::clamp( static_cast<float>( depth - 1 ) * 0.10f, 0.0f, 0.36f );
    r = std::clamp( 0.96f - depthFade * 0.55f, 0.44f, 1.0f );
    g = std::clamp( 0.48f + t * 0.34f - depthFade * 0.36f, 0.28f, 0.88f );
    b = std::clamp( 0.16f + t * 0.20f - depthFade * 0.18f, 0.10f, 0.52f );
}

void ReplayChildFutureColor( int depth, float t, float& r, float& g, float& b )
{
    const float depthFade = std::clamp( static_cast<float>( depth - 1 ) * 0.08f, 0.0f, 0.30f );
    const float shade = std::clamp( 0.48f + t * 0.28f - depthFade, 0.25f, 0.78f );
    r = shade;
    g = shade;
    b = shade + 0.06f;
}

void DrawReplayPredictionRagdollTorsoTrails( const std::vector<RunReplayPredictionFrame>& frames,
                                             std::size_t frameCount,
                                             ReplayFrameIndex revealFrame,
                                             const SkullbonezCore::GameObjects::GameModelCollection& collection,
                                             RunEditorTracer& tracer,
                                             ReplayRibbonDrawQuota& ribbonQuota )
{
    const int modelCount = collection.SceneEntityCount();
    frameCount = (std::min)( frameCount, frames.size() );
    if ( frameCount < 2 || modelCount <= 0 )
    {
        return;
    }

    const ReplayFrameIndex lastFrame = frames[frameCount - 1].frameIndex;
    const std::size_t sampleStride = ReplayPathStrideForSampleCount( frameCount );
    for ( int modelIndex = 0; modelIndex < modelCount; ++modelIndex )
    {
        if ( ReplayRibbonDrawQuotaExhausted( &ribbonQuota ) )
        {
            return;
        }
        if ( !ReplayModelIsRagdollTorso( collection, modelIndex ) )
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
                if ( !TryAddReplayPathSegment( tracer,
                                               &ribbonQuota,
                                               previous,
                                               body->position,
                                               0.50f + 0.28f * ( 1.0f - t ),
                                               0.96f,
                                               0.92f,
                                               MainMemoryReplayTrajectoryLane::AuxiliaryTrail ) )
                {
                    return;
                }
            }
            previous = body->position;
            hasPrevious = true;
        }
    }
}

struct ReplayPredictionAffectedBodyTrail
{
    ReplayBodyId id;
    int modelIndex = -1;
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
                                             const SkullbonezCore::GameObjects::GameModelCollection& collection,
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
             initialBody.modelIndex == rootModelIndex ||
             ReplayPredictionIdInFutureNodes( futureNodes, initialBody.id ) )
        {
            continue;
        }
        if ( ReplayModelIndexIsRagdollPart( collection, initialBody.modelIndex ) )
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
                FindReplayPredictionBodyByIdWithHint( frames[frameSlot], initialBody.id, initialBody.modelIndex );
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
            trail.modelIndex = body->modelIndex;
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
    for ( std::size_t trailIndex = 0; trailIndex < trailCount && !ReplayRibbonDrawQuotaExhausted( &ribbonQuota );
          ++trailIndex )
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
                FindReplayPredictionBodyByIdWithHint( frame, trail.id, trail.modelIndex );
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
                if ( !TryAddReplayPathSegment( tracer,
                                               &ribbonQuota,
                                               trail.previous,
                                               body->position,
                                               r,
                                               g,
                                               b,
                                               MainMemoryReplayTrajectoryLane::AuxiliaryTrail ) )
                {
                    break;
                }
            }

            if ( ReplayPredictionBodyHasVisibleLinearMotion( *body ) )
            {
                trail.lastMotionFrame = frame.frameIndex;
            }
            trail.previous = body->position;
            trail.modelIndex = body->modelIndex;
        }
    }

    // Why: marker emission is bounded and cheap, and "once rendered, a causal
    // box never leaves" outranks draw-time degradation.
    for ( std::size_t trailIndex = 0; trailIndex < trailCount; ++trailIndex )
    {
        const ReplayPredictionAffectedBodyTrail& trail = trails[trailIndex];
        if ( !ReplayColliderRecordForModelIndex( &colliderStore, trail.modelIndex ) )
        {
            continue;
        }

        RetainReplayPredictionEntryMarker( prediction,
                                           trail.id,
                                           trail.modelIndex,
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
                                              trail.modelIndex,
                                              restPosition,
                                              restOrientation ) )
        {
            RetainReplayPredictionRestMarker( prediction, trail.id, trail.modelIndex, restPosition, restOrientation );
        }
    }
}

void DrawReplayChildPaths( const ReplaySolverFrameSample& sample, void* userData )
{
    ReplayPathChildDrawContext& context = *static_cast<ReplayPathChildDrawContext*>( userData );
    if ( ReplayRibbonDrawQuotaExhausted( context.ribbonQuota ) )
    {
        return;
    }
    bool importantChildFrame = sample.frameIndex == context.presentFrame;
    for ( std::size_t i = 0; i < context.nodeCount; ++i )
    {
        if ( sample.frameIndex == context.nodes[i].node.firstFrame )
        {
            importantChildFrame = true;
            break;
        }
    }
    const bool skipSample = sample.frameIndex < context.presentFrame ||
                            ( sample.frameIndex != context.lastFrame && !importantChildFrame &&
                              !ShouldDrawReplayPathFrame( sample.frameIndex, context.sampleStride ) );
    if ( skipSample )
    {
        return;
    }

    for ( std::size_t i = 0; i < context.nodeCount; ++i )
    {
        ReplayPathChildDrawState& drawState = context.nodes[i];
        const ReplaySolverBodySample* body =
            FindReplayBodyByIdWithHint( sample, drawState.node.id, drawState.node.modelIndex );
        if ( !body )
        {
            continue;
        }

        if ( sample.frameIndex <= drawState.node.firstFrame )
        {
            if ( drawState.hasIncomingPrevious &&
                 VectorMagSquared( body->position - drawState.incomingPrevious ) > REPLAY_PATH_MIN_SEGMENT_DISTANCE_SQ )
            {
                const float t = ReplayPathFrameT( sample.frameIndex, context.presentFrame, drawState.node.firstFrame );
                float r = 0.92f;
                float g = 0.54f;
                float b = 0.18f;
                ReplayChildIncomingColor( drawState.node.depth, t, r, g, b );
                if ( !TryAddReplayPathSegment( *context.tracer,
                                               context.ribbonQuota,
                                               drawState.incomingPrevious,
                                               body->position,
                                               r,
                                               g,
                                               b,
                                               MainMemoryReplayTrajectoryLane::FutureChildIncoming ) )
                {
                    return;
                }
            }
            drawState.incomingPrevious = body->position;
            drawState.hasIncomingPrevious = true;
        }

        if ( sample.frameIndex >= drawState.node.firstFrame && drawState.hasPrevious &&
             VectorMagSquared( body->position - drawState.previous ) > REPLAY_PATH_MIN_SEGMENT_DISTANCE_SQ )
        {
            const float t = ReplayPathFrameT( sample.frameIndex, drawState.node.firstFrame, context.lastFrame );
            float r = 0.5f;
            float g = 0.5f;
            float b = 0.56f;
            ReplayChildFutureColor( drawState.node.depth, t, r, g, b );
            if ( !TryAddReplayPathSegment( *context.tracer,
                                           context.ribbonQuota,
                                           drawState.previous,
                                           body->position,
                                           r,
                                           g,
                                           b,
                                           MainMemoryReplayTrajectoryLane::FutureChildOutgoing ) )
            {
                return;
            }
        }
        if ( sample.frameIndex >= drawState.node.firstFrame )
        {
            CaptureReplayChildMarkerPose( drawState,
                                          body->position,
                                          ReplaySolverBodyOrientation( *body ),
                                          body->modelIndex );
            drawState.previous = body->position;
            drawState.hasPrevious = true;
        }
    }
}

struct ReplayPredictionFutureContext
{
    RunReplayPredictionState* prediction = nullptr;
    std::vector<RunReplayPathTraceNode>* nodes = nullptr;
    const SkullbonezCore::GameObjects::GameModelCollection* collection = nullptr;
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
    const int rootModelIndex = rootBody ? rootBody->modelIndex : -1;

    // Concept: motion-derived future nodes populate the cause window while the
    // contact graph is still sparse. A later contact-derived node replaces this
    // fallback, so the tree becomes more causal as solver contact data arrives.
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
             ( rootModelIndex >= 0 && initialBody.modelIndex == rootModelIndex ) )
        {
            continue;
        }
        if ( context.collection && ReplayModelIndexIsRagdollPart( *context.collection, initialBody.modelIndex ) )
        {
            continue;
        }

        for ( std::size_t frameSlot = 1; frameSlot < frameCount; ++frameSlot )
        {
            if ( ReplayPredictionBudgetExpired( budgetStart, budgetMilliseconds ) )
            {
                return false;
            }

            const RunReplayPredictionBodySample* body =
                FindReplayPredictionBodyByIdWithHint( frames[frameSlot], initialBody.id, initialBody.modelIndex );
            if ( !body )
            {
                continue;
            }

            if ( !ReplayPredictionBodyHasVisibleLinearMotion( *body ) )
            {
                continue;
            }

            AddReplayPredictionFutureNode( context,
                                           context.rootId,
                                           rootModelIndex,
                                           initialBody.id,
                                           body->modelIndex,
                                           frames[frameSlot].frameIndex,
                                           body->position,
                                           ReplayNormalizeOr( body->linearVelocity, Vector3( 0.0f, 1.0f, 0.0f ) ),
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
                                            const SkullbonezCore::GameObjects::GameModelCollection& collection,
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
        prediction.futureNodeCache.futureNodes = prediction.futureNodeCache.futureNodeBuildScratch;
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

    publishScratch();
}

bool CaptureReplayPredictionBodyState( SkullbonezCore::GameObjects::GameModelCollection& modelCollection,
                                       const PhysicsBodyStore& bodyStore,
                                       SkullbonezCore::Threading::WorkerPool& workerPool,
                                       std::vector<RunReplayPredictionBodyBackup>& outBodies )
{
    PROFILE_SCOPED( "Frame/Replay/Prediction/CaptureBodyState" );
    const int modelCount = modelCollection.SceneEntityCount();
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
        const GameModel* model = modelCollection.TryGetModel( i );
        if ( !model )
        {
            return;
        }

        const PhysicsBodyRecord& body = bodyRecords[static_cast<std::size_t>( i )];
        RunReplayPredictionBodyBackup backup;
        backup.id.value = body.replayBodyId;
        backup.modelIndex = i;
        backup.position = body.position;
        backup.orientation = body.orientation;
        backup.linearVelocity = body.linearVelocity;
        backup.angularVelocity = body.angularVelocity;
        backup.mass = body.mass;
        backup.inverseMass = body.invMass;
        backup.rotationalInertia = body.rotationalInertia;
        backup.inverseRotationalInertia = body.invRotationalInertia;
        backup.fixedContactHighlightSeconds = model->GetFixedContactHighlightSeconds();
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
    const PhysicsBodyStore& bodyStore = physicsEngine.BodyStore();
    if ( bodies.size() != static_cast<std::size_t>( bodyStore.Count() ) )
    {
        return false;
    }

    for ( const RunReplayPredictionBodyBackup& backup : bodies )
    {
        const PhysicsBodyHandle bodyHandle = bodyStore.HandleForModelIndex( backup.modelIndex );
        const PhysicsBodyRecord* bodyRecord = bodyStore.RecordForHandle( bodyHandle );
        if ( !bodyRecord || bodyStore.ModelIndexForHandle( bodyHandle ) != backup.modelIndex ||
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
        const RuntimeAllocation::RuntimeReserveGrowthRequest request = {
            REPLAY_PREDICTION_RESERVE_OWNER,
            "RunReplayPredictionSimulationState::predictionEngine",
            RuntimeAllocation::RuntimeReservePhase::Replay,
            0,
            currentBytes,
            requestedBytes,
            1 };
        result = RuntimeAllocation::RuntimeReserveAllocator::RequestGrowth( owner, request );
        if ( !result.granted )
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
         !predictionEngine.RestoreReplaySolverSnapshot( prediction.simulation.predictionWorld, modelCount ) )
    {
        return false;
    }
    prediction.simulation.predictionEngineReady = true;
    return true;
}


bool CaptureReplayPredictionFrame( ReplayRuntime& replayRuntime,
                                   SkullbonezCore::GameObjects::GameModelCollection& modelCollection,
                                   const PhysicsEngine& physicsEngine,
                                   SkullbonezCore::Threading::WorkerPool& workerPool,
                                   ReplayFrameIndex frameIndex )
{
    PROFILE_SCOPED( "Frame/Replay/Prediction/CaptureSample" );
    const int modelCount = modelCollection.SceneEntityCount();
    const PhysicsBodyStore& bodyStore = physicsEngine.BodyStore();
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
        body.modelIndex = i;
        body.position = source.position;
        body.orientation = source.orientation;
        body.linearVelocity = source.linearVelocity;
        frame.bodies[static_cast<std::size_t>( i )] = body;
    };

    // Invariant: capture reads the store rows advanced by the prediction step.
    // A replay-only GameModel writeback would copy every temporary pose just so
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
    const std::vector<PhysicsDebugContact>& debugContacts = physicsEngine.GetPhysicsDebugContacts();
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
            prediction.PublishBuildFrameSlot( frameSlot );
            return true;
        }
    }
    frame.debugContacts = debugContacts;
    frame.contactsIncomplete = false;
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
bool BeginReplayPredictionJob( ReplayRuntime& replayRuntime,
                               SkullbonezCore::GameObjects::GameModelCollection& modelCollection,
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
    if ( const ReplaySolverFrameSample* latest = replayRuntime.Solver().LatestSample() )
    {
        prediction.simulation.sourceSimulationSeconds = latest->simulationSeconds;
    }
    else
    {
        prediction.simulation.sourceSimulationSeconds = fallbackSourceSimulationSeconds;
    }
    prediction.build.lastBuildTime = simulationTotalSeconds;

    if ( !modelCollection.RepairPhysicsBodyAndColliderTopology() )
    {
        prediction.build.dirty = true;
        return false;
    }
    PhysicsEngine& physicsEngine = modelCollection.GetPhysicsEngine();
    const int modelCount = physicsEngine.BodyStore().Count();
    const PhysicsBodyStore& liveBodyStore = physicsEngine.BodyStore();
    if ( replayRuntime.PathVisualizer().hasTarget && replayRuntime.PathVisualizer().targetId.value != 0 )
    {
        ModelRowHint targetHint;
        targetHint.value = replayRuntime.PathVisualizer().targetModelIndex;
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
            replayRuntime.PathVisualizer().targetModelIndex = targetHint.value;
            return false;
        }
        prediction.simulation.targetModelIndex = targetIndex;
        replayRuntime.PathVisualizer().targetModelIndex = targetHint.value;
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

    if ( !CaptureReplayPredictionBodyState( modelCollection,
                                            liveBodyStore,
                                            workerPool,
                                            prediction.simulation.predictionBodies ) )
    {
        replayRuntime.CancelPredictionJob( clearSamplesOnCancel );
        return false;
    }

    physicsEngine.CaptureReplaySolverSnapshot( prediction.simulation.predictionWorld, modelCount );

    if ( !SeedReplayPredictionEngine( prediction, physicsEngine, config, worldForces, modelCount ) )
    {
        replayRuntime.CancelPredictionJob( clearSamplesOnCancel );
        prediction.build.dirty = true;
        return false;
    }

    if ( !prediction.simulation.predictionEngine ||
         !CaptureReplayPredictionFrame( replayRuntime,
                                        modelCollection,
                                        *prediction.simulation.predictionEngine,
                                        workerPool,
                                        0 ) )
    {
        replayRuntime.CancelPredictionJob( clearSamplesOnCancel );
        prediction.build.dirty = true;
        return false;
    }
    prediction.build.building = true;

    return !prediction.build.buildFrames.empty();
}


bool StepReplayPredictionJob( ReplayRuntime& replayRuntime,
                              SkullbonezCore::GameObjects::GameModelCollection& modelCollection,
                              const EngineConfig& config,
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

    if ( !replayRuntime.Prediction().simulation.predictionEngineReady ||
         !replayRuntime.Prediction().simulation.predictionEngine )
    {
        const bool preserveCommittedFuture = replayRuntime.Prediction().simulation.frames.size() >= 2u;
        replayRuntime.CancelPredictionJob( !preserveCommittedFuture );
        replayRuntime.Prediction().build.dirty = true;
        return false;
    }
    PhysicsEngine& predictionEngine = *replayRuntime.Prediction().simulation.predictionEngine;

    bool progressed = false;
    bool predictionStepFailed = false;

    {
        PROFILE_SCOPED( "Frame/Replay/Prediction/Steps" );
        while ( replayRuntime.Prediction().build.nextTick <= replayRuntime.Prediction().build.targetTickCount )
        {
            // Why: a large prediction can spend most of the slice on frame
            // capture. Still take one tick per entered slice so the visible
            // build prefix advances instead of stalling forever.
            if ( progressed && ReplayPredictionBudgetExpiredForPass( replayRuntime,
                                                                     MainMemoryReplayBudgetPass::PredictionStep,
                                                                     budgetStart,
                                                                     budgetMilliseconds ) )
            {
                break;
            }

            {
                PROFILE_SCOPED( "Frame/Replay/Prediction/StepPhysics" );
                if ( !StepPredictionEngineTick( predictionEngine,
                                                PHYSICS_FIXED_DT,
                                                config,
                                                replayRuntime.Prediction().simulation.predictionWorldForces,
                                                workerPool ) )
                {
                    predictionStepFailed = true;
                    replayRuntime.Prediction().build.dirty = true;
                    break;
                }
            }
            if ( !CaptureReplayPredictionFrame(
                     replayRuntime,
                     modelCollection,
                     predictionEngine,
                     workerPool,
                     static_cast<ReplayFrameIndex>( replayRuntime.Prediction().build.nextTick ) ) )
            {
                predictionStepFailed = true;
                replayRuntime.Prediction().build.dirty = true;
                break;
            }
            ++replayRuntime.Prediction().build.nextTick;
            progressed = true;

            if ( ReplayPredictionBudgetExpiredForPass( replayRuntime,
                                                       MainMemoryReplayBudgetPass::PredictionStep,
                                                       budgetStart,
                                                       budgetMilliseconds ) )
            {
                break;
            }
        }
    }

    if ( progressed )
    {
        PROFILE_SCOPED( "Frame/Replay/Prediction/CaptureJobState" );
        predictionEngine.CaptureReplaySolverSnapshot( replayRuntime.Prediction().simulation.predictionWorld,
                                                      predictionEngine.BodyStore().Count() );
    }

    if ( predictionStepFailed )
    {
        const bool preserveCommittedFuture = replayRuntime.Prediction().simulation.frames.size() >= 2u;
        replayRuntime.CancelPredictionJob( !preserveCommittedFuture );
        replayRuntime.Prediction().build.dirty = true;
        return false;
    }

    if ( replayRuntime.Prediction().build.nextTick > replayRuntime.Prediction().build.targetTickCount )
    {
        const float previousPresentT = replayRuntime.SolverPresentTrackPosition();
        const float previousSolverPosition = replayRuntime.TrackPosition( RunReplayTrack::Solver );
        const bool hadCommittedPredictionFrames = replayRuntime.Prediction().simulation.frames.size() >= 2;
        const bool solverWasOldLiveEdge =
            !hadCommittedPredictionFrames && ReplayRuntime::AtPresentTrackPosition( previousSolverPosition, 1.0f );
        const bool scrubberWasPinnedToPresent =
            !replayRuntime.Scrubber().historicalSamplePaused ||
            ReplayRuntime::AtPresentTrackPosition( previousSolverPosition, previousPresentT ) || solverWasOldLiveEdge;

        replayRuntime.Prediction().build.building = false;
        replayRuntime.Prediction().build.complete = true;
        replayRuntime.Prediction().simulation.frames.swap( replayRuntime.Prediction().build.buildFrames );
        replayRuntime.Prediction().build.buildFrames.clear();
        replayRuntime.Prediction().ResetBuildFramePublication();
        if ( replayRuntime.Prediction().baseline.valid )
        {
            UpdateReplayPredictionBaselineDivergence( replayRuntime.Prediction(),
                                                      replayRuntime.Prediction().simulation.frames,
                                                      replayRuntime.Prediction().simulation.frames.size() );
        }
        if ( scrubberWasPinnedToPresent )
        {
            // Why: prediction extends the normalized solver track by moving the
            // present marker left. A scrub value that meant "live/present"
            // before the swap must remain present, or render will preview the
            // far future and make the selected body appear to move.
            replayRuntime.SetTrackPosition( RunReplayTrack::Solver, replayRuntime.SolverPresentTrackPosition() );
            if ( replayRuntime.Scrubber().activeTrack == RunReplayTrack::Solver )
            {
                replayRuntime.Scrubber().historicalSamplePaused = false;
            }
        }
        // Why: future-node scratch was built from buildFrames. After this swap
        // those samples are the final frames, so keeping the cache preserves
        // progressively revealed child paths through the completion frame.
        replayRuntime.Prediction().build.lastBuildTime = simulationTotalSeconds;
    }

    return progressed || replayRuntime.Prediction().build.complete;
}


bool DrawReplayPredictionOverlay( ReplayRuntime& replayRuntime,
                                  const SkullbonezCore::GameObjects::GameModelCollection& modelCollection,
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
    const ReplayFrameIndex lastFrame = activePredictionFrames[activePredictionFrameCount - 1].frameIndex;
    const ReplayFrameIndex revealFrame = ReplayPredictionRevealFrameIndex( replayRuntime.Prediction(), lastFrame );
    // Why: while the job is still building there is no authoritative ending,
    // so no grey resting box may be derived from the growing prefix.
    const bool bufferComplete = !usingBuildFrames;
    DrawReplayPredictionBaselineSnapshot( replayRuntime.Prediction().baseline, colliderStore, tracer, ribbonQuota );

    if ( !replayRuntime.PathVisualizer().hasTarget || replayRuntime.PathVisualizer().targetId.value == 0 )
    {
        replayRuntime.ClearPredictionFutureNodeCache();
        if ( replayRuntime.Prediction().ragdollVisualsEnabled )
        {
            DrawReplayPredictionRagdollTorsoTrails( activePredictionFrames,
                                                    activePredictionFrameCount,
                                                    revealFrame,
                                                    modelCollection,
                                                    tracer,
                                                    ribbonQuota );
        }
        return true;
    }

    const std::size_t sampleStride = ReplayPathStrideForSampleCount( activePredictionFrameCount );
    {
        PROFILE_SCOPED( "Frame/Replay/Prediction/DrawRoot" );
        bool hasPrevious = false;
        Vector3 previous = SkullbonezCore::Math::Vector::ZERO_VECTOR;
        ReplayFrameIndex rootLastMotionFrame = 0;
        for ( std::size_t frameIndex = 0; frameIndex < activePredictionFrameCount; ++frameIndex )
        {
            const RunReplayPredictionFrame& frame = activePredictionFrames[frameIndex];
            if ( frame.frameIndex > revealFrame )
            {
                break;
            }

            // Why: the reveal-edge frame always draws so the line tip grows
            // smoothly instead of jumping ahead one stride at a time.
            if ( frame.frameIndex != lastFrame && frame.frameIndex != revealFrame &&
                 !ShouldDrawReplayPathFrame( frame.frameIndex, sampleStride ) )
            {
                continue;
            }
            const RunReplayPredictionBodySample* body =
                FindReplayPredictionBodyByIdWithHint( frame,
                                                      replayRuntime.PathVisualizer().targetId,
                                                      replayRuntime.PathVisualizer().targetModelIndex );
            if ( !body )
            {
                continue;
            }

            if ( hasPrevious && VectorMagSquared( body->position - previous ) > REPLAY_PATH_MIN_SEGMENT_DISTANCE_SQ )
            {
                const float t = ReplayPathFrameT( frame.frameIndex, 0, lastFrame );
                if ( !TryAddReplayPathSegment( tracer,
                                               &ribbonQuota,
                                               previous,
                                               body->position,
                                               1.0f - t * 0.85f,
                                               1.0f,
                                               1.0f - t * 0.72f,
                                               MainMemoryReplayTrajectoryLane::FutureRoot ) )
                {
                    break;
                }
            }
            previous = body->position;
            hasPrevious = true;
            if ( ReplayPredictionBodyHasVisibleLinearMotion( *body ) )
            {
                rootLastMotionFrame = frame.frameIndex;
            }
        }

        // Why: the root gets no yellow entry box — the white selection marker
        // already anchors where its story starts. Grey follows the same rule
        // as every child: only a completed prediction that actually ends at
        // rest may place a resting box, and only after the reveal cursor has
        // watched the root stop moving.
        if ( bufferComplete && revealFrame >= rootLastMotionFrame + REPLAY_PREDICTION_REST_GRACE_FRAMES )
        {
            Vector3 rootRestPosition = SkullbonezCore::Math::Vector::ZERO_VECTOR;
            Quaternion rootRestOrientation = IDENTITY_QUATERNION;
            if ( ReplayPredictionBodyRestingPose( activePredictionFrames,
                                                  activePredictionFrameCount,
                                                  replayRuntime.PathVisualizer().targetId,
                                                  replayRuntime.PathVisualizer().targetModelIndex,
                                                  rootRestPosition,
                                                  rootRestOrientation ) )
            {
                if ( ReplayColliderRecordForModelIndex( &colliderStore,
                                                        replayRuntime.PathVisualizer().targetModelIndex ) )
                {
                    RetainReplayPredictionRestMarker( replayRuntime.Prediction(),
                                                      replayRuntime.PathVisualizer().targetId,
                                                      replayRuntime.PathVisualizer().targetModelIndex,
                                                      rootRestPosition,
                                                      rootRestOrientation );
                }
            }
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
            (void)ReplayPredictionBudgetExpiredForPass( replayRuntime,
                                                        MainMemoryReplayBudgetPass::PredictionBuildTree,
                                                        buildBudgetStart,
                                                        budgetMilliseconds );
            drawFutureTree = replayRuntime.Prediction().futureNodeCache.futureNodesCacheValid &&
                             !replayRuntime.Prediction().futureNodeCache.futureNodes.empty();
        }
        else
        {
            // Why: live play freezes prediction visualization. Keep drawing the
            // committed topology, but do not discover new child nodes while the
            // real simulation advances underneath the overlay.
            drawFutureTree = !replayRuntime.Prediction().futureNodeCache.futureNodes.empty();
        }
    }
    if ( drawFutureTree )
    {
        PROFILE_SCOPED( "Frame/Replay/Prediction/DrawChildren" );
        ReplayPathChildDrawContext childDraw;
        childDraw.tracer = &tracer;
        childDraw.colliderStore = &colliderStore;
        childDraw.ribbonQuota = &ribbonQuota;
        childDraw.presentFrame = 0;
        childDraw.lastFrame = lastFrame;
        childDraw.sampleStride = sampleStride;
        childDraw.nodeCount =
            (std::min)( replayRuntime.Prediction().futureNodeCache.futureNodes.size(), REPLAY_PATH_MAX_FUTURE_NODES );
        for ( std::size_t i = 0; i < childDraw.nodeCount; ++i )
        {
            childDraw.nodes[i].node = replayRuntime.Prediction().futureNodeCache.futureNodes[i];
        }

        // Invariant: child drawing has no wall-clock bailout. The future-node
        // cap and frame-index stride bound marker work, and a revealed causal
        // segment must remain visible once it enters the overlay.
        for ( std::size_t frameIndex = 0; frameIndex < activePredictionFrameCount; ++frameIndex )
        {
            if ( ReplayRibbonDrawQuotaExhausted( &ribbonQuota ) )
            {
                break;
            }
            const RunReplayPredictionFrame& frame = activePredictionFrames[frameIndex];
            if ( frame.frameIndex > revealFrame )
            {
                break;
            }

            bool importantChildFrame =
                frame.frameIndex == 0 || frame.frameIndex == lastFrame || frame.frameIndex == revealFrame;
            for ( std::size_t i = 0; i < childDraw.nodeCount; ++i )
            {
                if ( frame.frameIndex == childDraw.nodes[i].node.firstFrame )
                {
                    importantChildFrame = true;
                    break;
                }
            }
            if ( !importantChildFrame && !ShouldDrawReplayPathFrame( frame.frameIndex, sampleStride ) )
            {
                continue;
            }

            for ( std::size_t i = 0; i < childDraw.nodeCount; ++i )
            {
                ReplayPathChildDrawState& drawState = childDraw.nodes[i];
                const RunReplayPredictionBodySample* body =
                    FindReplayPredictionBodyByIdWithHint( frame, drawState.node.id, drawState.node.modelIndex );
                if ( !body )
                {
                    continue;
                }

                if ( frame.frameIndex <= drawState.node.firstFrame )
                {
                    if ( drawState.hasIncomingPrevious &&
                         VectorMagSquared( body->position - drawState.incomingPrevious ) >
                             REPLAY_PATH_MIN_SEGMENT_DISTANCE_SQ )
                    {
                        const float t = ReplayPathFrameT( frame.frameIndex, 0, drawState.node.firstFrame );
                        float r = 0.92f;
                        float g = 0.54f;
                        float b = 0.18f;
                        ReplayChildIncomingColor( drawState.node.depth, t, r, g, b );
                        if ( !TryAddReplayPathSegment( tracer,
                                                       &ribbonQuota,
                                                       drawState.incomingPrevious,
                                                       body->position,
                                                       r,
                                                       g,
                                                       b,
                                                       MainMemoryReplayTrajectoryLane::FutureChildIncoming ) )
                        {
                            break;
                        }
                    }
                    drawState.incomingPrevious = body->position;
                    drawState.hasIncomingPrevious = true;
                }

                if ( frame.frameIndex >= drawState.node.firstFrame && !drawState.active )
                {
                    // Why: contact propagation can wake a wall without giving
                    // every brick visible translation. Delay outlines and child
                    // trails until the sampled body has meaningful linear speed.
                    if ( !ReplayPredictionBodyHasVisibleLinearMotion( *body ) )
                    {
                        continue;
                    }
                    drawState.active = true;
                    // Concept: entry is the body's IN-PLACE pose from
                    // prediction frame 0 — the wall exactly as the live scene
                    // knows it, never a sampled pose from after the impulse
                    // arrived. Anchoring the trail start there makes lines
                    // grow straight out of the yellow formation box.
                    const RunReplayPredictionBodySample* initialSample =
                        FindReplayPredictionBodyByIdWithHint( activePredictionFrames[0],
                                                              drawState.node.id,
                                                              body->modelIndex );
                    drawState.hasEntryPose = true;
                    drawState.entryModelIndex = body->modelIndex;
                    drawState.entryPosition = initialSample ? initialSample->position : body->position;
                    drawState.entryOrientation = initialSample ? initialSample->orientation : body->orientation;
                    drawState.previous = drawState.entryPosition;
                    drawState.hasPrevious = true;
                    drawState.lastMotionFrame = frame.frameIndex;
                    continue;
                }

                if ( frame.frameIndex >= drawState.node.firstFrame && drawState.hasPrevious &&
                     VectorMagSquared( body->position - drawState.previous ) > REPLAY_PATH_MIN_SEGMENT_DISTANCE_SQ )
                {
                    const float t = ReplayPathFrameT( frame.frameIndex, drawState.node.firstFrame, lastFrame );
                    float r = 0.5f;
                    float g = 0.5f;
                    float b = 0.56f;
                    ReplayChildFutureColor( drawState.node.depth, t, r, g, b );
                    if ( !TryAddReplayPathSegment( tracer,
                                                   &ribbonQuota,
                                                   drawState.previous,
                                                   body->position,
                                                   r,
                                                   g,
                                                   b,
                                                   MainMemoryReplayTrajectoryLane::FutureChildOutgoing ) )
                    {
                        break;
                    }
                }
                if ( frame.frameIndex >= drawState.node.firstFrame && drawState.active )
                {
                    if ( ReplayPredictionBodyHasVisibleLinearMotion( *body ) )
                    {
                        drawState.lastMotionFrame = frame.frameIndex;
                    }
                    drawState.previous = body->position;
                    drawState.hasPrevious = true;
                }
            }
        }

        DrawReplayPredictionCausalMarkers( replayRuntime.Prediction(),
                                           childDraw,
                                           revealFrame,
                                           bufferComplete ? &activePredictionFrames : nullptr,
                                           bufferComplete ? activePredictionFrameCount : 0 );
    }

    {
        PROFILE_SCOPED( "Frame/Replay/Prediction/DrawAffectedBodies" );
        DrawReplayPredictionAffectedBodyTrails( activePredictionFrames,
                                                activePredictionFrameCount,
                                                replayRuntime.Prediction(),
                                                revealFrame,
                                                bufferComplete,
                                                replayRuntime.PathVisualizer().targetId,
                                                replayRuntime.PathVisualizer().targetModelIndex,
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
                                                revealFrame,
                                                modelCollection,
                                                tracer,
                                                ribbonQuota );
    }
    if ( bufferComplete )
    {
        RetainReplayPredictionEndStateMarkers( replayRuntime.Prediction(),
                                               revealFrame,
                                               activePredictionFrames,
                                               activePredictionFrameCount );
    }
    DrawReplayPredictionRetainedMarkers( replayRuntime.Prediction(),
                                         activePredictionFrames,
                                         activePredictionFrameCount,
                                         revealFrame,
                                         colliderStore,
                                         tracer );
    return true;
}


void RenderReplayPredictionVisualizer( ReplayRuntime& replayRuntime,
                                       SkullbonezCore::GameObjects::GameModelCollection& modelCollection,
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
        const ColliderStore& colliderStore = modelCollection.GetPhysicsEngine().Colliders();
        DrawReplayPredictionOverlay( replayRuntime,
                                     modelCollection,
                                     colliderStore,
                                     tracer,
                                     ribbonQuota,
                                     budgetMilliseconds );
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
    if ( replayRuntime.Prediction().build.dirty ||
         ( allowAutomaticRefresh && !replayRuntime.Prediction().build.building && sourceChanged && refreshDue ) )
    {
        if ( ReplayPredictionBudgetExpiredForPass( replayRuntime,
                                                   MainMemoryReplayBudgetPass::PredictionBegin,
                                                   budgetStart,
                                                   budgetMilliseconds ) )
        {
            return;
        }
        RunReplayPredictionState& prediction = replayRuntime.Prediction();
        if ( prediction.build.dirty )
        {
            replayRuntime.RecordReplayTrajectoryRebuildCause( MainMemoryReplayRebuildCause::Dirty );
        }
        else
        {
            replayRuntime.RecordReplayTrajectoryRebuildCause( MainMemoryReplayRebuildCause::AutomaticRefresh );
        }
        if ( prediction.baseline.comparisonActive && !prediction.baseline.valid &&
             prediction.simulation.frames.size() >= 2 )
        {
            if ( !CaptureReplayPredictionBaselineSnapshot( prediction,
                                                           prediction.simulation.frames,
                                                           prediction.simulation.frames.size(),
                                                           replayRuntime.PathVisualizer().targetId,
                                                           replayRuntime.PathVisualizer().targetModelIndex ) )
            {
                prediction.baseline.comparisonActive = false;
            }
        }
        BeginReplayPredictionJob( replayRuntime,
                                  modelCollection,
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
        if ( ReplayPredictionBudgetExpiredForPass( replayRuntime,
                                                   MainMemoryReplayBudgetPass::PredictionBegin,
                                                   budgetStart,
                                                   budgetMilliseconds ) )
        {
            return;
        }
    }
    const ColliderStore& colliderStore = modelCollection.GetPhysicsEngine().Colliders();
    if ( replayRuntime.Prediction().build.building )
    {
        const double remainingMilliseconds = ReplayPredictionRemainingMilliseconds( budgetStart, budgetMilliseconds );
        if ( remainingMilliseconds > 0.0 )
        {
            StepReplayPredictionJob( replayRuntime,
                                     modelCollection,
                                     config,
                                     workerPool,
                                     simulationTotalSeconds,
                                     budgetStart,
                                     budgetMilliseconds );
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

    // Why: prediction stepping and future-node discovery still share the
    // private-engine budget, but draw work is capped by frame-index strides and
    // the fixed ribbon quota so visible trajectory lines do not flicker under
    // load.
    DrawReplayPredictionOverlay( replayRuntime,
                                 modelCollection,
                                 colliderStore,
                                 tracer,
                                 ribbonQuota,
                                 budgetMilliseconds );
}
} // namespace

namespace SkullbonezCore::Basics::ReplayOverlay
{
void RenderReplayPathVisualizer( const ReplayPathVisualizerRenderContext& context )
{
    PROFILE_SCOPED( "Frame/Replay/PathVisualizer" );
    // Concept: this marker owns replay visualizer budgeting.
    //
    // Prediction stepping plus retained/future-node build work share the
    // wall-clock deadline. Visible trajectory drawing spends a fixed ribbon
    // quota instead, so completed segments do not flicker under transient load.
    const auto visualizerStart = std::chrono::steady_clock::now();
    ReplayRibbonDrawQuota ribbonQuota = BeginReplayRibbonDrawQuota( context.tracer );
    RenderReplayPredictionVisualizer( context.replayRuntime,
                                      context.models,
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
        // when the user has hidden the past lane, do not rebuild retained child
        // paths from the advancing live timeline behind that frozen preview.
        return;
    }
    if ( ReplayPredictionBudgetExpiredForPass( context.replayRuntime,
                                               MainMemoryReplayBudgetPass::RetainedBounds,
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
        target.modelIndex = context.replayRuntime.PathVisualizer().targetModelIndex;
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

    ReplayPathBoundsContext bounds;
    context.replayRuntime.Solver().ForEachSampleChronological( CaptureReplayPathBounds, &bounds );
    if ( ReplayPredictionBudgetExpiredForPass( context.replayRuntime,
                                               MainMemoryReplayBudgetPass::RetainedBounds,
                                               visualizerStart,
                                               REPLAY_PREDICTION_MAX_WORK_MILLISECONDS ) )
    {
        return;
    }
    if ( !bounds.hasSample )
    {
        return;
    }

    const ReplayFrameIndex presentFrame = std::clamp( presentSample->frameIndex, bounds.firstFrame, bounds.lastFrame );
    const ReplayRecorderStats stats = context.replayRuntime.Solver().GetStats();
    const std::size_t sampleStride = ReplayPathStrideForSampleCount( stats.sampleCount );

    context.replayRuntime.PathVisualizer().futureNodes.clear();
    const PhysicsBodyStore& bodyStore = context.models.GetPhysicsEngine().BodyStore();
    const ColliderStore& colliderStore = context.models.GetPhysicsEngine().Colliders();
    for ( RunReplayPathTarget& target : context.replayRuntime.PathVisualizer().targets )
    {
        if ( ReplayPredictionBudgetExpiredForPass( context.replayRuntime,
                                                   MainMemoryReplayBudgetPass::RetainedBuildTree,
                                                   visualizerStart,
                                                   REPLAY_PREDICTION_MAX_WORK_MILLISECONDS ) )
        {
            return;
        }

        if ( target.id.value == 0 )
        {
            continue;
        }

        PROFILE_SCOPED( "Frame/Replay/PathVisualizer/RetainedTarget" );
        RunReplayPathVisualizerState targetVisualizer;
        ApplyPrimaryReplayPathTarget( targetVisualizer, target.id, target.modelIndex, target.name );

        {
            PROFILE_SCOPED( "Frame/Replay/PathVisualizer/RetainedTarget/BuildTree" );
            ReplayPathFutureContext futureContext;
            futureContext.visualizer = &targetVisualizer;
            futureContext.collection = &context.models;
            futureContext.budgetStart = &visualizerStart;
            futureContext.rootId = target.id;
            futureContext.presentFrame = presentFrame;
            futureContext.budgetMilliseconds = REPLAY_PREDICTION_MAX_WORK_MILLISECONDS;
            futureContext.includeRagdollVisuals = context.replayRuntime.Prediction().ragdollVisualsEnabled;
            context.replayRuntime.Solver().ForEachSampleChronological( BuildReplayFutureNodes, &futureContext );
        }
        if ( ReplayPredictionBudgetExpiredForPass( context.replayRuntime,
                                                   MainMemoryReplayBudgetPass::RetainedBuildTree,
                                                   visualizerStart,
                                                   REPLAY_PREDICTION_MAX_WORK_MILLISECONDS ) )
        {
            return;
        }

        {
            PROFILE_SCOPED( "Frame/Replay/PathVisualizer/RetainedTarget/DrawRoot" );
            ReplayPathRootDrawContext rootDraw;
            rootDraw.tracer = &context.tracer;
            rootDraw.ribbonQuota = &ribbonQuota;
            rootDraw.rootId = target.id;
            rootDraw.firstFrame = bounds.firstFrame;
            rootDraw.presentFrame = presentFrame;
            rootDraw.lastFrame = bounds.lastFrame;
            rootDraw.sampleStride = sampleStride;
            context.replayRuntime.Solver().ForEachSampleChronological( DrawReplayRootPath, &rootDraw );
        }

        ReplayPathChildDrawContext childDraw;
        childDraw.tracer = &context.tracer;
        childDraw.colliderStore = &colliderStore;
        childDraw.ribbonQuota = &ribbonQuota;
        childDraw.presentFrame = presentFrame;
        childDraw.lastFrame = bounds.lastFrame;
        childDraw.sampleStride = sampleStride;
        childDraw.nodeCount = (std::min)( targetVisualizer.futureNodes.size(), REPLAY_PATH_MAX_FUTURE_NODES );
        for ( std::size_t i = 0; i < childDraw.nodeCount; ++i )
        {
            childDraw.nodes[i].node = targetVisualizer.futureNodes[i];
        }
        if ( childDraw.nodeCount > 0 )
        {
            PROFILE_SCOPED( "Frame/Replay/PathVisualizer/RetainedTarget/DrawChildren" );
            context.replayRuntime.Solver().ForEachSampleChronological( DrawReplayChildPaths, &childDraw );
            DrawReplayChildFinalMarkers( childDraw );
        }

        if ( target.id.value == context.replayRuntime.PathVisualizer().targetId.value )
        {
            context.replayRuntime.PathVisualizer().futureNodes = targetVisualizer.futureNodes;
        }

        {
            PROFILE_SCOPED( "Frame/Replay/PathVisualizer/RetainedTarget/DrawMarker" );
            ModelRowHint targetHint;
            targetHint.value = target.modelIndex;
            int markerIndex = -1;
            const bool markerResolved = TryResolveReplayBodyModelIndex( bodyStore,
                                                                        target.id,
                                                                        targetHint,
                                                                        context.models.SceneEntityCount(),
                                                                        markerIndex );
            target.modelIndex = targetHint.value;
            if ( target.id.value == context.replayRuntime.PathVisualizer().targetId.value )
            {
                context.replayRuntime.PathVisualizer().targetModelIndex = targetHint.value;
            }
            if ( markerResolved )
            {
                TryAddReplayTargetMarkerFromStores( context.tracer, bodyStore, colliderStore, markerIndex );
            }
        }
    }
}
} // namespace SkullbonezCore::Basics::ReplayOverlay

void Run::RenderReplayPathVisualizer( RunEditorTracer& tracer )
{
    tracer.ClearReplayTrajectoryStats();
    const auto physicsWorldForces = m_cWorldEnvironment.GetPhysicsWorldForces();
    // Lifetime: physicsWorldForces is a local value because the overlay context
    // borrows it by reference for the immediate delegate call.
    const SkullbonezCore::Basics::ReplayOverlay::ReplayPathVisualizerRenderContext context{
        m_replayRuntime,
        m_cGameModelCollection,
        *m_systems.config,
        physicsWorldForces,
        *m_systems.workerPool,
        tracer,
        SceneState().isScenePhysics,
        SceneState().currentFrame,
        m_timers.simulationTimer.GetTimeSinceLastStart(),
        m_timers.simulationTimer.GetTotalTime() };
    SkullbonezCore::Basics::ReplayOverlay::RenderReplayPathVisualizer( context );
    m_replayRuntime.RecordReplayTrajectoryFrameStats( tracer.ReplayTrajectoryStats() );
}


void Run::RenderReplayCauseFocusOverlay( RunEditorTracer& tracer )
{
    if ( m_replayRuntime.Camera().focusKind == RunReplayCameraFocusKind::None )
    {
        return;
    }

    if ( m_replayRuntime.Camera().focusKind == RunReplayCameraFocusKind::Body )
    {
        const PhysicsBodyStore& bodyStore = m_cGameModelCollection.GetPhysicsEngine().BodyStore();
        const ColliderStore& colliderStore = m_cGameModelCollection.GetPhysicsEngine().Colliders();
        ModelRowHint focusHint;
        focusHint.value = m_replayRuntime.Camera().focusModelIndex;
        int focusedModelIndex = -1;
        if ( TryResolveReplayBodyModelIndex( bodyStore,
                                             m_replayRuntime.Camera().focusedId,
                                             focusHint,
                                             bodyStore.Count(),
                                             focusedModelIndex ) )
        {
            m_replayRuntime.Camera().focusModelIndex = focusHint.value;
            TryAddReplayTargetMarkerFromStores( tracer, bodyStore, colliderStore, focusedModelIndex );
            return;
        }
        m_replayRuntime.Camera().focusModelIndex = focusHint.value;
    }

    if ( m_replayRuntime.Camera().focusKind == RunReplayCameraFocusKind::Manifold ||
         m_replayRuntime.Camera().focusKind == RunReplayCameraFocusKind::PredictionContact ||
         m_replayRuntime.Camera().focusKind == RunReplayCameraFocusKind::PredictionMotion )
    {
        if ( m_replayRuntime.Camera().focusKind == RunReplayCameraFocusKind::Manifold )
        {
            const ReplaySolverFrameSample* sample = m_replayRuntime.CurrentSolverScrubSample();
            if ( sample )
            {
                const ReplaySolverBodySample* focusedBody =
                    FindReplayBodyById( *sample, m_replayRuntime.Camera().focusedId );
                const ReplaySolverBodySample* counterpartBody =
                    FindReplayBodyById( *sample, m_replayRuntime.Camera().counterpartId );
                if ( focusedBody )
                {
                    bool drewContact = false;
                    for ( const ReplaySolverPersistentContactSample& contact :
                          sample->worldSnapshot.persistentContacts )
                    {
                        if ( !ReplayContactHasModelIndex( contact, focusedBody->modelIndex ) )
                        {
                            continue;
                        }
                        const int otherModelIndex = ReplayContactOtherModelIndex( contact, focusedBody->modelIndex );
                        const bool terrain = contact.isTerrain || otherModelIndex < 0;
                        if ( m_replayRuntime.Camera().focusTerrain != terrain )
                        {
                            continue;
                        }
                        if ( !terrain && ( !counterpartBody || counterpartBody->modelIndex != otherModelIndex ) )
                        {
                            continue;
                        }
                        tracer.AddReplayContactMarker( ReplayContactPoint( *sample, contact ),
                                                       ReplayContactNormalForModel( contact, focusedBody->modelIndex ),
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
        else if ( m_replayRuntime.Camera().focusKind == RunReplayCameraFocusKind::PredictionContact )
        {
            ReplayFrameIndex focusFrame = 0;
            int focusedModelIndex = m_replayRuntime.Camera().focusModelIndex;
            int counterpartModelIndex = m_replayRuntime.Camera().focusCounterpartModelIndex;

            const RunReplayCauseTreeState& causeTree = m_replayRuntime.CauseTree();
            if ( causeTree.selectedRow >= 0 && causeTree.selectedRow < static_cast<int>( causeTree.rows.size() ) )
            {
                const RunReplayCauseTreeRow& row = causeTree.rows[static_cast<std::size_t>( causeTree.selectedRow )];
                if ( row.kind == RunReplayCauseTreeRowKind::PredictionContact &&
                     row.id.value == m_replayRuntime.Camera().focusedId.value )
                {
                    focusFrame = row.firstFrame;
                    focusedModelIndex = row.modelIndex;
                    counterpartModelIndex = row.counterpartModelIndex;
                }
            }
            else if ( m_replayRuntime.Camera().focusContactIndex >= 0 &&
                      m_replayRuntime.Camera().focusContactIndex <
                          static_cast<int>( m_replayRuntime.Prediction().futureNodeCache.futureNodes.size() ) )
            {
                const RunReplayPathTraceNode& node =
                    m_replayRuntime.Prediction().futureNodeCache.futureNodes[static_cast<std::size_t>(
                        m_replayRuntime.Camera().focusContactIndex )];
                if ( node.id.value == m_replayRuntime.Camera().focusedId.value && node.contactDerived )
                {
                    focusFrame = node.firstFrame;
                    focusedModelIndex = node.modelIndex;
                    counterpartModelIndex = node.parentModelIndex;
                }
            }

            bool drewPredictionManifold = false;
            const std::vector<RunReplayPredictionFrame>& frames = m_replayRuntime.ActivePredictionFrames();
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
                    const int contactModelA =
                        ReplayRagdollTorsoModelIndexForPart( m_cGameModelCollection, contact.bodyA );
                    const int contactModelB =
                        contact.bodyB >= 0
                            ? ReplayRagdollTorsoModelIndexForPart( m_cGameModelCollection, contact.bodyB )
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
        tracer.AddReplayContactMarker( m_replayRuntime.Camera().targetPoint,
                                       m_replayRuntime.Camera().targetNormal,
                                       0.1f,
                                       0.95f,
                                       1.0f );
        return;
    }

    if ( m_replayRuntime.Camera().focusKind == RunReplayCameraFocusKind::SolverRow )
    {
        tracer.AddReplayContactMarker( m_replayRuntime.Camera().targetPoint,
                                       m_replayRuntime.Camera().targetNormal,
                                       0.2f,
                                       0.85f,
                                       1.0f );
        tracer.AddReplayImpulseVector( m_replayRuntime.Camera().targetPoint,
                                       m_replayRuntime.Camera().impulseVector,
                                       1.0f,
                                       0.32f,
                                       0.12f );
    }
}
