/*
File: SkullbonezTests/TestReplayVisualPacket.cpp
Purpose:
  Locks typed and ordered first-difference behavior for replay visual packets.

Summary:
  Small stack-owned typed records and float arrays model production packet
  spans. Tests prove semantic, causal-topology, and ghost changes are reported
  before geometry, that reordered or truncated submission buffers identify
  their exact owner lane and float, that an in-flight worker cannot change the
  prepared prefix halfway through one rendered frame, and that prediction draw
  commands append without revisiting a stable publication. Prediction frame
  bank tests also prove invalidation hides retained nested storage in O(1), and
  committed-publication snapshots preserve generation-bound visible facts.
  Cross-target promotion tests also lock the hidden publication to the
  promoted source until the coherent bank flip releases the requested target.
  Budget-schedule and fast-completion tests prove hidden prediction work cannot
  change the retained visible bank or the final canonical trajectory output.

Glossary:
  Packet span: Non-owning view of one ordered production submission stream.
  First difference: Earliest semantic field or float where two packets differ.
  Publication token: Monotonic value that invalidates retained draw commands
    only when a reader-visible trajectory prefix changes.
  All-body path: Space-scene future record selected independently of causal
    child topology.
  Retained attachment: Shared packet operation that joins persistent prediction
    geometry with frame-local moving tails without copying either span.
  Retained chunk: Stable compact range whose continuation repairs only the
    previous chunk's open adjacency tail.
  Committed frame prefix: Reader-visible portion of a retained prediction frame
    bank; slots beyond the count are allocation storage, not published future.

Invariants:
  - Packet comparison is bit-exact and order-sensitive.
  - A count mismatch cannot alias an equal common prefix.
  - Presentation keeps the frame prefix prepared on the frame thread even if
    the worker publishes more prediction rows before rendering consumes it.
  - An unchanged publication token and reveal frame cannot mutate draw storage.

Related:
  - SkullbonezSource/Runtime/Replay/ReplayVisualPacket.h
  - SkullbonezSource/Runtime/Prediction/ReplayPredictionDrawing.cpp
*/
#include "../ThirdPtySource/doctest/doctest.h"

#include "../SkullbonezSource/Runtime/Replay/ReplayVisualPacket.h"
#include "../SkullbonezSource/Runtime/Replay/ReplayVisualPacketFingerprint.h"
#include "../SkullbonezSource/Physics/PhysicsEngine.h"
#include "../SkullbonezSource/Runtime/Prediction/ReplayPrediction.h"
#include "../SkullbonezSource/Runtime/Prediction/ReplayPredictionDrawing.h"
#include "../SkullbonezSource/Runtime/Prediction/ReplayPredictionPublication.h"
#include "../SkullbonezSource/Runtime/Prediction/ReplayPredictionPublicationOperations.h"

#include <algorithm>
#include <array>
#include <bit>
#include <chrono>
#include <memory>
#include <optional>
#include <string_view>
#include <vector>

using namespace SkullbonezCore::Runtime;
using namespace SkullbonezCore::Runtime::ReplayPredictionPublicationOperations;
using namespace SkullbonezCore::Runtime::ReplayVisualPacketFingerprintOperations;
using namespace SkullbonezCore::Runtime::ReplayVisualPacketOperations;

namespace SkullbonezCore::Runtime
{
ReplayPredictionIsolatedSimulation::~ReplayPredictionIsolatedSimulation() = default;
RunReplayPredictionState::RunReplayPredictionState() = default;
RunReplayPredictionState::~RunReplayPredictionState() = default;
}

namespace SkullbonezCore::Runtime::ReplayPredictionSchedulingOperations
{
namespace
{
std::optional<std::size_t> forcedBudgetExpiryCheck;
std::size_t budgetExpiryCheckCount = 0u;
}

bool ReplayPredictionBudgetExpired( const std::chrono::steady_clock::time_point& start, double budgetMilliseconds )
{
    // Why: this test target links the production trajectory publisher but not
    // the runtime scheduler. A deterministic check index proves resume cursors
    // without relying on machine speed; unset state preserves production time.
    if ( forcedBudgetExpiryCheck )
    {
        return budgetExpiryCheckCount++ >= *forcedBudgetExpiryCheck;
    }

    return budgetMilliseconds > 0.0 &&
           std::chrono::duration<double, std::milli>( std::chrono::steady_clock::now() - start ).count() >=
               budgetMilliseconds;
}
}

TEST_CASE( "Replay committed frame invalidation retains both allocation banks" )
{
    std::vector<RunReplayPredictionFrame> committedFrames( 2u );
    committedFrames[0].bodies.reserve( 8u );
    std::size_t committedFrameCount = 2u;

    RunReplayPredictionFrame* const committedBank = committedFrames.data();
    const std::size_t committedBodyCapacity = committedFrames[0].bodies.capacity();

    std::vector<RunReplayPredictionFrame> completedBuildFrames( 3u );
    completedBuildFrames[0].bodies.reserve( 16u );
    RunReplayPredictionFrame* const completedBuildBank = completedBuildFrames.data();

    RunReplayPredictionState::PromoteFrameBanks( committedFrames, committedFrameCount, completedBuildFrames, 2u );

    CHECK( committedFrameCount == 2u );
    CHECK( committedFrames.data() == completedBuildBank );
    CHECK( completedBuildFrames.data() == committedBank );
    CHECK( completedBuildFrames[0].bodies.capacity() == committedBodyCapacity );

    RunReplayPredictionState::InvalidateCommittedFrameBank( committedFrameCount );

    CHECK( committedFrameCount == 0u );
    CHECK( committedFrames.size() == 3u );
    CHECK( committedFrames.data() == completedBuildBank );
    CHECK( committedFrames[0].bodies.capacity() == 16u );
}

TEST_CASE( "Replay committed publication retains coherent visible build facts" )
{
    RunReplayPredictionTrajectoryBuildState build;
    build.rootId.value = 41u;
    build.usingBuildFrames = true;
    build.childFrameCount = 14401u;
    build.builtNodeCount = 200u;
    build.topologyVersion = 7u;
    build.valid = true;

    auto visibleCache = std::make_unique<RunReplayPredictionFutureNodeCache>();
    visibleCache->futureNodes.resize( 200u );
    visibleCache->futureNodes[0].id.value = 52u;
    visibleCache->futureNodesTopologyVersion = 7u;
    visibleCache->futureNodesCacheValid = true;
    visibleCache->retainedMarkers[0].id.value = 53u;
    visibleCache->retainedMarkers[0].hasEntryPose = true;
    visibleCache->retainedMarkerCount = 1u;

    auto publication = std::make_unique<ReplayPredictionCommittedPublicationState>();
    publication->visibleFutureNodes.reserve( REPLAY_VISUAL_FUTURE_NODE_CAPACITY );
    REQUIRE( publication->Begin( build, *visibleCache, 9u, 14401u,
                                 SkullbonezCore::Physics::ModelRowHint { 3 }, true, false, 120u, 81u ) );
    build = {};
    visibleCache->futureNodes.clear();
    visibleCache->futureNodesTopologyVersion = 0u;
    visibleCache->futureNodesCacheValid = false;
    visibleCache->retainedMarkerCount = 0u;

    CHECK( publication->pending );
    CHECK( publication->visibleTrajectoryBuild.rootId.value == 41u );
    CHECK( publication->visibleTrajectoryBuild.usingBuildFrames );
    CHECK( publication->visibleTrajectoryBuild.childFrameCount == 14401u );
    CHECK( publication->visibleTrajectoryBuild.builtNodeCount == 200u );
    CHECK( publication->visibleTrajectoryBuild.topologyVersion == 7u );
    CHECK( publication->visibleTrajectoryBuild.valid );
    REQUIRE( publication->visibleFutureNodes.size() == 200u );
    CHECK( publication->visibleFutureNodes[0].id.value == 52u );
    CHECK( publication->visibleTopologyVersion == 7u );
    CHECK( publication->visibleFutureNodesCacheValid );
    REQUIRE( publication->visibleRetainedMarkerCount == 1u );
    CHECK( publication->visibleRetainedMarkers[0].id.value == 53u );
    CHECK( publication->visibleRetainedMarkers[0].hasEntryPose );
    CHECK( RunReplayPredictionState::FutureTreeReadyForDraw(
        publication->visibleTrajectoryBuild, SkullbonezCore::Physics::PhysicsSceneObjectId { 41u }, true, 14401u,
        publication->visibleFutureNodes.size(), publication->visibleTopologyVersion,
        publication->visibleFutureNodesCacheValid ) );
    CHECK( publication->generation == 9u );
    CHECK( publication->sourceFrameCount == 14401u );
    CHECK( publication->visibleFrameCount == 120u );
    CHECK( publication->PresentedTrajectoryPublicationVersion( 99u ) == 81u );
    CHECK( ReplayOverlay::IsReplayPredictionDrawListPublicationStable(
        false, 81u, 2400, publication->PresentedTrajectoryPublicationVersion( 99u ), 2400 ) );

    publication->Reset();
    CHECK_FALSE( publication->pending );
    CHECK( publication->visibleFutureNodes.empty() );
    CHECK( publication->visibleRetainedMarkerCount == 0u );
    CHECK( publication->generation == 0u );
    CHECK( publication->sourceFrameCount == 0u );
    CHECK( publication->PresentedTrajectoryPublicationVersion( 99u ) == 99u );
    CHECK_FALSE( ReplayOverlay::IsReplayPredictionDrawListPublicationStable(
        false, 81u, 2400, publication->PresentedTrajectoryPublicationVersion( 99u ), 2400 ) );
}

TEST_CASE( "Replay committed all-body publication retains its resume cursor" )
{
    const SkullbonezCore::Physics::PhysicsSceneObjectId rootId { 1u };
    RunReplayPredictionTrajectoryBuildState cursor;
    cursor.rootId = rootId;
    cursor.usingBuildFrames = false;
    cursor.allBodyPaths = true;
    cursor.allBodyFrameCount = 0u;
    cursor.builtAllBodyCount = 0u;
    cursor.allBodyBodyCount = 4u;

    CHECK_FALSE( cursor.AllBodyPublicationSourceChanged( rootId, false, 2u, 4u, false ) );
    CHECK( cursor.AllBodyPublicationSourceChanged( rootId, false, 2u, 4u, true ) );
    CHECK( cursor.AllBodyPublicationSourceChanged( rootId, false, 2u, 5u, false ) );

    cursor.builtAllBodyCount = 2u;
    cursor.allBodyFrameCount = 2u;
    CHECK_FALSE( cursor.AllBodyPublicationSourceChanged( rootId, false, 2u, 4u, false ) );
}

TEST_CASE( "Replay committed all-body builder resumes an expired production pass" )
{
    std::vector<RunReplayPredictionFrame> frames( 2u );

    for ( std::size_t frameIndex = 0; frameIndex < frames.size(); ++frameIndex )
    {
        frames[frameIndex].frameIndex = static_cast<ReplayFrameIndex>( frameIndex );
        frames[frameIndex].bodies.resize( 4u );

        for ( std::size_t bodyIndex = 0; bodyIndex < frames[frameIndex].bodies.size(); ++bodyIndex )
        {
            RunReplayPredictionBodySample& body = frames[frameIndex].bodies[bodyIndex];
            body.id.value = static_cast<uint32_t>( bodyIndex + 1u );
            body.modelRow.value = static_cast<int>( bodyIndex );
            body.position.x = static_cast<float>( bodyIndex );
            body.position.y = static_cast<float>( frameIndex );
        }
    }

    auto resumed = std::make_unique<RunReplayPredictionState>();
    auto uninterrupted = std::make_unique<RunReplayPredictionState>();
    const SkullbonezCore::Physics::PhysicsSceneObjectId rootId { 1u };

    for ( RunReplayPredictionState* state : { resumed.get(), uninterrupted.get() } )
    {
        state->simulation.predictionWorldForces.mutualGravity.enabled = true;
        state->trajectoryBuild.rootId = rootId;
        state->trajectoryBuild.usingBuildFrames = false;
    }

    ReplayPredictionSchedulingOperations::budgetExpiryCheckCount = 0u;
    ReplayPredictionSchedulingOperations::forcedBudgetExpiryCheck = 2u;
    UpdateReplayPredictionTrajectoryStore( *resumed, frames, frames.size(), false, rootId,
                                           std::chrono::steady_clock::now(), 1.0 );
    ReplayPredictionSchedulingOperations::forcedBudgetExpiryCheck.reset();
    CHECK( resumed->trajectoryBuild.allBodyPaths );
    CHECK( resumed->trajectoryBuild.builtAllBodyCount > 0u );
    CHECK( resumed->trajectoryBuild.builtAllBodyCount < resumed->trajectoryBuild.allBodyBodyCount );
    CHECK( resumed->trajectoryBuild.allBodyBodyCount == 4u );

    const auto findCommittedBodyRecord = []( const RunReplayPredictionState& state,
                                             SkullbonezCore::Physics::PhysicsSceneObjectId bodyId )
        -> const ReplayTrajectoryRecord*
    {
        const std::span<const ReplayTrajectoryRecord> records = state.trajectoryStore.ActiveRecords();
        const auto found = std::find_if( records.begin(), records.end(),
                                         [bodyId]( const ReplayTrajectoryRecord& record )
                                         {
                                             return record.key.lane == ReplayTrajectoryLane::FutureRoot &&
                                                    record.key.bodyId.value == bodyId.value;
                                         } );
        return found != records.end() ? &*found : nullptr;
    };

    const ReplayTrajectoryRecord* retainedPrefixRecord =
        findCommittedBodyRecord( *resumed, SkullbonezCore::Physics::PhysicsSceneObjectId { 2u } );
    REQUIRE( retainedPrefixRecord );
    const uint32_t retainedPrefixVersion = retainedPrefixRecord->version;

    UpdateReplayPredictionTrajectoryStore( *resumed, frames, frames.size(), false, rootId,
                                           std::chrono::steady_clock::now(), 0.0 );
    UpdateReplayPredictionTrajectoryStore( *uninterrupted, frames, frames.size(), false, rootId,
                                           std::chrono::steady_clock::now(), 0.0 );

    CHECK( resumed->trajectoryBuild.builtAllBodyCount == 4u );
    CHECK( resumed->trajectoryBuild.allBodyFrameCount == frames.size() );
    retainedPrefixRecord =
        findCommittedBodyRecord( *resumed, SkullbonezCore::Physics::PhysicsSceneObjectId { 2u } );
    REQUIRE( retainedPrefixRecord );
    CHECK( retainedPrefixRecord->version == retainedPrefixVersion );
    const std::span<const ReplayTrajectoryRecord> resumedRecords = resumed->trajectoryStore.ActiveRecords();
    const std::span<const ReplayTrajectoryRecord> uninterruptedRecords = uninterrupted->trajectoryStore.ActiveRecords();
    REQUIRE( resumedRecords.size() == uninterruptedRecords.size() );

    for ( std::size_t recordIndex = 0; recordIndex < resumedRecords.size(); ++recordIndex )
    {
        const ReplayTrajectoryRecord& lhs = resumedRecords[recordIndex];
        const ReplayTrajectoryRecord& rhs = uninterruptedRecords[recordIndex];
        CHECK( lhs.key.lane == rhs.key.lane );
        CHECK( lhs.key.bodyId.value == rhs.key.bodyId.value );
        CHECK( lhs.key.branchOrdinal == rhs.key.branchOrdinal );
        CHECK( lhs.version == rhs.version );
        CHECK( lhs.publishedPointCount == rhs.publishedPointCount );
        REQUIRE( lhs.points.size() == rhs.points.size() );

        for ( std::size_t pointIndex = 0; pointIndex < lhs.points.size(); ++pointIndex )
        {
            CHECK( lhs.points[pointIndex].frameIndex == rhs.points[pointIndex].frameIndex );
            CHECK( lhs.points[pointIndex].position.x == rhs.points[pointIndex].position.x );
            CHECK( lhs.points[pointIndex].position.y == rhs.points[pointIndex].position.y );
            CHECK( lhs.points[pointIndex].position.z == rhs.points[pointIndex].position.z );
        }
    }
}

TEST_CASE( "Replay committed trajectory publication is identical across budget schedules" )
{
    constexpr std::size_t frameCount = 6u;
    constexpr std::size_t bodyCount = 5u;
    constexpr std::size_t nodeCount = 3u;
    constexpr uint32_t topologyVersion = 17u;
    const SkullbonezCore::Physics::PhysicsSceneObjectId rootId { 1u };
    std::vector<RunReplayPredictionFrame> frames( frameCount );

    for ( std::size_t frameIndex = 0; frameIndex < frames.size(); ++frameIndex )
    {
        RunReplayPredictionFrame& frame = frames[frameIndex];
        frame.frameIndex = static_cast<ReplayFrameIndex>( frameIndex );
        frame.bodies.resize( bodyCount );

        for ( std::size_t bodyIndex = 0; bodyIndex < frame.bodies.size(); ++bodyIndex )
        {
            RunReplayPredictionBodySample& body = frame.bodies[bodyIndex];
            body.id.value = static_cast<uint32_t>( bodyIndex + 1u );
            body.modelRow.value = static_cast<int>( bodyIndex );
            body.position.x = static_cast<float>( frameIndex * 10u + bodyIndex );
            body.position.y = static_cast<float>( bodyIndex * 3u );
            body.position.z = static_cast<float>( frameIndex + bodyIndex * 2u );
        }
    }

    const auto prepareState = [&]( RunReplayPredictionState& state, bool alternateWorkerScratch )
    {
        state.simulation.frames = frames;
        state.simulation.committedFrameCount = frames.size();
        state.simulation.targetId = rootId;
        state.simulation.targetModelRow.value = 0;
        state.simulation.predictionWorldForces.mutualGravity.enabled = true;
        state.build.generationBeginCount = 3u;
        state.revealClock.presentedFrame = frames.back().frameIndex;

        if ( !state.trajectoryStore.ReserveRecords( 16u, 0 ) )
        {
            return false;
        }

        const auto seedWorkerRecord = [&]( ReplayTrajectoryLane lane, std::size_t pointCount, ReplayFrameIndex firstFrame )
        {
            ReplayTrajectoryRecordKey key;
            key.bodyId.value = 2u;
            key.lane = lane;
            key.branchOrdinal = static_cast<uint16_t>( REPLAY_VISUAL_FUTURE_NODE_CAPACITY );
            ReplayTrajectoryRecord* record = state.trajectoryStore.BeginReplaceRecord( key, 1u, rootId, 1, firstFrame, true,
                                                                                       frames.size() );

            if ( !record || !state.trajectoryStore.ReserveRecordPoints( *record, frames.size(), 0 ) )
            {
                return false;
            }

            for ( std::size_t pointIndex = 0; pointIndex < pointCount; ++pointIndex )
            {
                const ReplayTrajectoryPoint point { static_cast<ReplayFrameIndex>( pointIndex ),
                                                    frames[pointIndex].bodies[1].position };

                if ( !state.trajectoryStore.TryAppendPoint( *record, point ) )
                {
                    return false;
                }

                state.trajectoryStore.PublishPrefix( *record, record->points.size() );
            }

            return true;
        };

        // Hazard: completed worker child records are scratch. Their split and
        // firstFrame can legitimately depend on how much topology presentation
        // observed before the worker finished; final output must retire them.
        const std::size_t incomingPointCount = alternateWorkerScratch ? 3u : 2u;

        if ( !seedWorkerRecord( ReplayTrajectoryLane::FutureChildIncoming, incomingPointCount,
                                static_cast<ReplayFrameIndex>( incomingPointCount ) ) ||
             !seedWorkerRecord( ReplayTrajectoryLane::FutureChildOutgoing, frames.size() - incomingPointCount,
                                static_cast<ReplayFrameIndex>( incomingPointCount ) ) )
        {
            return false;
        }

        if ( !RebuildReplayPredictionCommittedRootTrajectory( state ) )
        {
            return false;
        }

        state.futureNodeCache.futureNodes.resize( nodeCount );

        for ( std::size_t nodeIndex = 0; nodeIndex < nodeCount; ++nodeIndex )
        {
            RunReplayPathTraceNode& node = state.futureNodeCache.futureNodes[nodeIndex];
            node.id.value = static_cast<uint32_t>( nodeIndex + 2u );
            node.parentId = rootId;
            node.modelRow.value = static_cast<int>( nodeIndex + 1u );
            node.parentModelRow.value = 0;
            node.firstFrame = static_cast<ReplayFrameIndex>( nodeIndex + 2u );
            node.depth = static_cast<int>( nodeIndex + 1u );
            node.contactDerived = nodeIndex != 1u;
        }

        state.futureNodeCache.futureNodesTopologyVersion = topologyVersion;
        state.futureNodeCache.futureNodesBuiltFrameCount = frames.size();
        state.futureNodeCache.futureNodesAffectedFrameCount = frames.size();
        state.futureNodeCache.futureNodesAffectedComplete = true;
        state.futureNodeCache.futureNodesBuiltTargetId = rootId;
        state.futureNodeCache.futureNodesBuiltFromBuildFrames = false;
        state.futureNodeCache.futureNodesCacheValid = true;
        state.futureNodeCache.retainedMarkers[0].id.value = 2u;
        state.futureNodeCache.retainedMarkers[0].modelRow.value = 1;
        state.futureNodeCache.retainedMarkers[0].hasEntryPose = true;
        state.futureNodeCache.retainedMarkerCount = 1u;
        state.futureNodeCache.childMarkerScan.nodeCount = nodeCount;
        state.futureNodeCache.childMarkerScan.Commit( state.build.generationBeginCount, topologyVersion, rootId,
                                                      frames.size(), state.revealClock.presentedFrame, false );

        RunReplayPredictionTrajectoryBuildState visibleBuild;
        visibleBuild.rootId = rootId;
        visibleBuild.usingBuildFrames = true;
        visibleBuild.childFrameCount = frames.size();
        visibleBuild.builtNodeCount = nodeCount;
        visibleBuild.topologyVersion = topologyVersion;
        visibleBuild.valid = true;
        state.committedPublication.visibleFutureNodes.reserve( nodeCount );

        if ( !state.committedPublication.CaptureVisible( visibleBuild, state.futureNodeCache,
                                                         state.simulation.targetModelRow, true, true, frames.size(),
                                                         state.trajectoryStore.publicationVersion ) ||
             !state.committedPublication.ActivateCaptured( state.build.generationBeginCount, frames.size() ) )
        {
            return false;
        }

        return true;
    };

    const auto runSchedule = [&]( RunReplayPredictionState& state, std::span<const std::size_t> expiryChecks )
    {
        std::size_t passCount = 0u;

        // Invariant: each forced expiry happens only between indivisible body
        // or child records. Resetting the check counter models a fresh frame
        // budget while the production cursors retain the completed prefix.
        for ( ; passCount < 64u && state.committedPublication.pending; ++passCount )
        {
            ReplayPredictionSchedulingOperations::budgetExpiryCheckCount = 0u;
            const double budgetMilliseconds = expiryChecks.empty() ? 0.0 : 1.0;

            if ( expiryChecks.empty() )
            {
                ReplayPredictionSchedulingOperations::forcedBudgetExpiryCheck.reset();
            }
            else
            {
                ReplayPredictionSchedulingOperations::forcedBudgetExpiryCheck = expiryChecks[passCount %
                                                                                             expiryChecks.size()];
            }

            const auto budgetStart = std::chrono::steady_clock::now();
            UpdateReplayPredictionTrajectoryStore( state, frames, frames.size(), false, rootId, budgetStart,
                                                   budgetMilliseconds );
            (void)TryFlipReplayPredictionCommittedPublication( state, rootId, frames.size(),
                                                               state.revealClock.presentedFrame, budgetStart,
                                                               budgetMilliseconds );
        }

        ReplayPredictionSchedulingOperations::forcedBudgetExpiryCheck.reset();
        return passCount;
    };

    auto narrowSlices = std::make_unique<RunReplayPredictionState>();
    auto variedSlices = std::make_unique<RunReplayPredictionState>();
    auto uninterrupted = std::make_unique<RunReplayPredictionState>();
    REQUIRE( prepareState( *narrowSlices, false ) );
    REQUIRE( prepareState( *variedSlices, true ) );
    REQUIRE( prepareState( *uninterrupted, false ) );
    const ReplayPredictionPresentationView pending = ReplayPrediction::PresentationViewFromState( *narrowSlices, true );
    CHECK( pending.futureTreeReady );
    CHECK_FALSE( pending.showAllFuturePaths );
    CHECK( pending.usingBuildFrames );
    CHECK( pending.retainedMarkers.size() == 1u );
    const std::array<std::size_t, 1u> narrowSchedule { 1u };
    const std::array<std::size_t, 4u> variedSchedule { 3u, 1u, 4u, 2u };
    const std::size_t narrowPassCount = runSchedule( *narrowSlices, narrowSchedule );
    const std::size_t variedPassCount = runSchedule( *variedSlices, variedSchedule );
    const std::size_t uninterruptedPassCount = runSchedule( *uninterrupted, {} );

    CHECK( narrowPassCount > 1u );
    CHECK( variedPassCount > 1u );
    CHECK( narrowPassCount != variedPassCount );
    CHECK( uninterruptedPassCount == 1u );
    CHECK_FALSE( narrowSlices->committedPublication.pending );
    CHECK_FALSE( variedSlices->committedPublication.pending );
    CHECK_FALSE( uninterrupted->committedPublication.pending );
    REQUIRE( narrowSlices->FutureTreeReadyForDraw( rootId, false, frames.size() ) );
    REQUIRE( variedSlices->FutureTreeReadyForDraw( rootId, false, frames.size() ) );
    REQUIRE( uninterrupted->FutureTreeReadyForDraw( rootId, false, frames.size() ) );
    CHECK( narrowSlices->trajectoryBuild.allBodyPaths );
    CHECK( variedSlices->trajectoryBuild.allBodyPaths );
    CHECK( uninterrupted->trajectoryBuild.allBodyPaths );

    const std::span<const ReplayTrajectoryRecord> narrowRecords = narrowSlices->trajectoryStore.ActiveRecords();
    REQUIRE( narrowRecords.size() == 11u );

    for ( const RunReplayPredictionState* candidate : { variedSlices.get(), uninterrupted.get() } )
    {
        CHECK( narrowSlices->trajectoryBuild.rootId.value == candidate->trajectoryBuild.rootId.value );
        CHECK( narrowSlices->trajectoryBuild.usingBuildFrames == candidate->trajectoryBuild.usingBuildFrames );
        CHECK( narrowSlices->trajectoryBuild.childFrameCount == candidate->trajectoryBuild.childFrameCount );
        CHECK( narrowSlices->trajectoryBuild.builtNodeCount == candidate->trajectoryBuild.builtNodeCount );
        CHECK( narrowSlices->trajectoryBuild.allBodyFrameCount == candidate->trajectoryBuild.allBodyFrameCount );
        CHECK( narrowSlices->trajectoryBuild.builtAllBodyCount == candidate->trajectoryBuild.builtAllBodyCount );
        CHECK( narrowSlices->trajectoryBuild.allBodyBodyCount == candidate->trajectoryBuild.allBodyBodyCount );
        CHECK( narrowSlices->trajectoryBuild.topologyVersion == candidate->trajectoryBuild.topologyVersion );
        CHECK( narrowSlices->trajectoryStore.publicationVersion == candidate->trajectoryStore.publicationVersion );
        const std::span<const ReplayTrajectoryRecord> candidateRecords = candidate->trajectoryStore.ActiveRecords();
        REQUIRE( narrowRecords.size() == candidateRecords.size() );

        for ( std::size_t recordIndex = 0; recordIndex < narrowRecords.size(); ++recordIndex )
        {
            const ReplayTrajectoryRecord& lhs = narrowRecords[recordIndex];
            const ReplayTrajectoryRecord& rhs = candidateRecords[recordIndex];
            CHECK( lhs.key.bodyId.value == rhs.key.bodyId.value );
            CHECK( lhs.key.lane == rhs.key.lane );
            CHECK( lhs.key.branchOrdinal == rhs.key.branchOrdinal );
            CHECK( lhs.version == rhs.version );
            CHECK( lhs.publishedPointCount == rhs.publishedPointCount );
            CHECK( lhs.styleId == rhs.styleId );
            CHECK( lhs.parentId.value == rhs.parentId.value );
            CHECK( lhs.depth == rhs.depth );
            CHECK( lhs.firstFrame == rhs.firstFrame );
            CHECK( lhs.contactDerived == rhs.contactDerived );
            REQUIRE( lhs.points.size() == rhs.points.size() );

            for ( std::size_t pointIndex = 0; pointIndex < lhs.points.size(); ++pointIndex )
            {
                CHECK( lhs.points[pointIndex].frameIndex == rhs.points[pointIndex].frameIndex );
                CHECK( lhs.points[pointIndex].position.x == rhs.points[pointIndex].position.x );
                CHECK( lhs.points[pointIndex].position.y == rhs.points[pointIndex].position.y );
                CHECK( lhs.points[pointIndex].position.z == rhs.points[pointIndex].position.z );
            }
        }
    }

    const ReplayPredictionPresentationView narrowView = ReplayPrediction::PresentationViewFromState( *narrowSlices, true );
    const ReplayPredictionPresentationView variedView = ReplayPrediction::PresentationViewFromState( *variedSlices, true );
    CHECK( narrowView.futureTreeReady );
    CHECK( variedView.futureTreeReady );
    CHECK( narrowView.showAllFuturePaths );
    CHECK( variedView.showAllFuturePaths );
    CHECK_FALSE( narrowView.usingBuildFrames );
    CHECK_FALSE( variedView.usingBuildFrames );
    CHECK( narrowView.targetId.value == variedView.targetId.value );
    CHECK( narrowView.topologyVersion == variedView.topologyVersion );
    CHECK( narrowView.trajectoryPublicationVersion == variedView.trajectoryPublicationVersion );
    CHECK( narrowView.frames.size() == variedView.frames.size() );
    CHECK( narrowView.futureNodes.size() == variedView.futureNodes.size() );
    CHECK( narrowView.retainedMarkers.size() == variedView.retainedMarkers.size() );

    ReplayVisualPacket narrowPacket;
    narrowPacket.header.sourceFrame = narrowView.sourceFrame;
    narrowPacket.header.revealFrame = narrowView.revealFrame;
    narrowPacket.header.targetId = narrowView.targetId;
    narrowPacket.header.topologyVersion = narrowView.topologyVersion;
    narrowPacket.header.publishedFrameCount = static_cast<uint32_t>( narrowView.frames.size() );
    narrowPacket.header.futureNodeCount = static_cast<uint32_t>( narrowView.futureNodes.size() );
    narrowPacket.header.predictionEnabled = true;
    narrowPacket.header.predictionComplete = true;
    narrowPacket.trajectoryRecords = narrowView.trajectoryRecords;
    narrowPacket.futureNodes = narrowView.futureNodes;
    narrowPacket.retainedMarkers = narrowView.retainedMarkers;
    ReplayVisualPacket variedPacket = narrowPacket;
    variedPacket.trajectoryRecords = variedView.trajectoryRecords;
    variedPacket.futureNodes = variedView.futureNodes;
    variedPacket.retainedMarkers = variedView.retainedMarkers;
    std::vector<ReplayVisualTrajectoryDigestState> narrowDigests;
    std::vector<ReplayVisualTrajectoryDigestState> variedDigests;
    const ReplayVisualPacketFingerprint narrowFingerprint = BuildReplayVisualPacketFingerprint( narrowPacket,
                                                                                                narrowDigests );
    const ReplayVisualPacketFingerprint variedFingerprint = BuildReplayVisualPacketFingerprint( variedPacket,
                                                                                                variedDigests );
    CHECK( narrowFingerprint.trajectoryStateHash == variedFingerprint.trajectoryStateHash );
    CHECK( narrowFingerprint.visualStateHash == variedFingerprint.visualStateHash );
    CHECK( narrowFingerprint.semanticHash == variedFingerprint.semanticHash );
    CHECK( narrowFingerprint.exactHash == variedFingerprint.exactHash );
}

TEST_CASE( "Replay fast completion keeps committed trajectories visible until the coherent flip" )
{
    constexpr std::size_t frameCount = 4u;
    constexpr uint32_t topologyVersion = 23u;
    const SkullbonezCore::Physics::PhysicsSceneObjectId rootId { 1u };
    const SkullbonezCore::Physics::PhysicsSceneObjectId childId { 2u };
    auto state = std::make_unique<RunReplayPredictionState>();
    std::vector<RunReplayPredictionFrame> oldVisibleFrames( frameCount );
    state->simulation.frames.resize( frameCount );

    for ( std::size_t frameIndex = 0u; frameIndex < frameCount; ++frameIndex )
    {
        RunReplayPredictionFrame& replacementFrame = state->simulation.frames[frameIndex];
        RunReplayPredictionFrame& visibleFrame = oldVisibleFrames[frameIndex];
        replacementFrame.frameIndex = static_cast<ReplayFrameIndex>( frameIndex );
        visibleFrame.frameIndex = replacementFrame.frameIndex;
        replacementFrame.bodies.resize( 2u );
        visibleFrame.bodies.resize( 2u );

        for ( std::size_t bodyIndex = 0u; bodyIndex < 2u; ++bodyIndex )
        {
            const uint32_t bodyValue = static_cast<uint32_t>( bodyIndex + 1u );
            replacementFrame.bodies[bodyIndex].id.value = bodyValue;
            replacementFrame.bodies[bodyIndex].modelRow.value = static_cast<int>( bodyIndex );
            replacementFrame.bodies[bodyIndex].position.x = static_cast<float>( 100u + frameIndex * 10u + bodyIndex );
            visibleFrame.bodies[bodyIndex] = replacementFrame.bodies[bodyIndex];
            visibleFrame.bodies[bodyIndex].position.x = -replacementFrame.bodies[bodyIndex].position.x;
        }
    }

    state->simulation.committedFrameCount = frameCount;
    state->simulation.targetId = rootId;
    state->simulation.targetModelRow.value = 0;
    state->build.buildFrames = oldVisibleFrames;
    state->build.generationBeginCount = 5u;
    state->revealClock.presentedFrame = state->simulation.frames.back().frameIndex;
    REQUIRE( state->trajectoryStore.ReserveRecords( 16u, 0 ) );

    const auto seedVisibleRecord = [&]( SkullbonezCore::Physics::PhysicsSceneObjectId bodyId,
                                        ReplayTrajectoryLane lane,
                                        SkullbonezCore::Physics::PhysicsSceneObjectId parentId )
    {
        ReplayTrajectoryRecordKey key;
        key.bodyId = bodyId;
        key.lane = lane;
        key.branchOrdinal = 0u;
        ReplayTrajectoryRecord* record = state->trajectoryStore.BeginReplaceRecord( key, lane == ReplayTrajectoryLane::FutureRoot ? 0u : 1u,
                                                                                     parentId, lane == ReplayTrajectoryLane::FutureRoot ? 0 : 1,
                                                                                     2u, lane != ReplayTrajectoryLane::FutureRoot,
                                                                                     frameCount );

        if ( !record || !state->trajectoryStore.ReserveRecordPoints( *record, frameCount, 0 ) )
        {
            return false;
        }

        for ( const RunReplayPredictionFrame& frame : oldVisibleFrames )
        {
            const std::size_t bodyIndex = bodyId.value == rootId.value ? 0u : 1u;

            if ( !state->trajectoryStore.TryAppendPoint( *record,
                                                         { frame.frameIndex, frame.bodies[bodyIndex].position } ) )
            {
                return false;
            }
        }

        state->trajectoryStore.PublishPrefix( *record, record->points.size() );
        return true;
    };

    REQUIRE( seedVisibleRecord( rootId, ReplayTrajectoryLane::FutureRoot, {} ) );
    REQUIRE( seedVisibleRecord( childId, ReplayTrajectoryLane::FutureChildIncoming, rootId ) );
    REQUIRE( seedVisibleRecord( childId, ReplayTrajectoryLane::FutureChildOutgoing, rootId ) );

    RunReplayPathTraceNode node;
    node.id = childId;
    node.parentId = rootId;
    node.modelRow.value = 1;
    node.parentModelRow.value = 0;
    node.firstFrame = 2u;
    node.depth = 1;
    node.contactDerived = true;
    state->futureNodeCache.futureNodes.push_back( node );
    state->futureNodeCache.futureNodesTopologyVersion = topologyVersion;
    state->futureNodeCache.futureNodesBuiltFrameCount = frameCount;
    state->futureNodeCache.futureNodesAffectedFrameCount = frameCount;
    state->futureNodeCache.futureNodesAffectedComplete = true;
    state->futureNodeCache.futureNodesBuiltTargetId = rootId;
    state->futureNodeCache.futureNodesBuiltFromBuildFrames = false;
    state->futureNodeCache.futureNodesCacheValid = true;

    RunReplayPredictionTrajectoryBuildState visibleBuild;
    visibleBuild.rootId = rootId;
    visibleBuild.rootFrameCount = frameCount;
    visibleBuild.childFrameCount = frameCount;
    visibleBuild.builtNodeCount = 1u;
    visibleBuild.topologyVersion = topologyVersion;
    visibleBuild.usingBuildFrames = false;
    visibleBuild.valid = true;
    state->committedPublication.visibleFutureNodes.reserve( 1u );
    REQUIRE( state->committedPublication.CaptureVisible( visibleBuild, state->futureNodeCache,
                                                         state->simulation.targetModelRow, true, true, frameCount,
                                                         state->trajectoryStore.publicationVersion ) );
    REQUIRE( state->committedPublication.ActivateCaptured( state->build.generationBeginCount, frameCount ) );

    const auto findRecord = [&]( SkullbonezCore::Physics::PhysicsSceneObjectId bodyId, ReplayTrajectoryLane lane,
                                 uint16_t branchOrdinal ) -> const ReplayTrajectoryRecord*
    {
        ReplayTrajectoryRecordKey key;
        key.bodyId = bodyId;
        key.lane = lane;
        key.branchOrdinal = branchOrdinal;
        return state->trajectoryStore.FindRecord( key );
    };

    const ReplayTrajectoryRecord* visibleRoot = findRecord( rootId, ReplayTrajectoryLane::FutureRoot, 0u );
    const ReplayTrajectoryRecord* visibleIncoming = findRecord( childId, ReplayTrajectoryLane::FutureChildIncoming, 0u );
    REQUIRE( visibleRoot );
    REQUIRE( visibleIncoming );
    const uint32_t visibleRootVersion = visibleRoot->version;
    const uint32_t visibleIncomingVersion = visibleIncoming->version;
    const float visibleRootFirstX = visibleRoot->points[0].position.x;
    const float visibleIncomingFirstX = visibleIncoming->points[0].position.x;
    const uint64_t visiblePublicationVersion = state->committedPublication.visibleTrajectoryPublicationVersion;

    REQUIRE( RebuildReplayPredictionReplacementRootTrajectory( *state,
                                                                ReplayPredictionTrajectoryBank::Build ) );
    state->futureNodeCache.futureNodesBuiltFromBuildFrames = true;
    const auto budgetStart = std::chrono::steady_clock::now();
    UpdateReplayPredictionTrajectoryStore( *state, state->simulation.frames, frameCount, true, rootId, budgetStart, 0.0 );

    // Hazard: this is the completion-before-first-prefix interval. Hidden B
    // records may advance, but A's committed keys, versions, and points remain
    // reader-owned until the full topology/marker bank authorizes the flip.
    visibleRoot = findRecord( rootId, ReplayTrajectoryLane::FutureRoot, 0u );
    visibleIncoming = findRecord( childId, ReplayTrajectoryLane::FutureChildIncoming, 0u );
    REQUIRE( visibleRoot );
    REQUIRE( visibleIncoming );
    CHECK( visibleRoot->version == visibleRootVersion );
    CHECK( visibleIncoming->version == visibleIncomingVersion );
    CHECK( visibleRoot->points[0].position.x == visibleRootFirstX );
    CHECK( visibleIncoming->points[0].position.x == visibleIncomingFirstX );
    CHECK( ReplayPrediction::PresentationViewFromState( *state, true ).trajectoryPublicationVersion ==
           visiblePublicationVersion );

    const ReplayTrajectoryRecord* hiddenRoot = findRecord( rootId, ReplayTrajectoryLane::FutureRoot, 1u );
    REQUIRE( hiddenRoot );
    const uint32_t hiddenRootVersion = hiddenRoot->version;
    CHECK( hiddenRoot->points[0].position.x == state->simulation.frames[0].bodies[0].position.x );
    state->futureNodeCache.childMarkerScan.nodeCount = 1u;
    state->futureNodeCache.childMarkerScan.Commit( state->build.generationBeginCount, topologyVersion, rootId,
                                                   frameCount, state->revealClock.presentedFrame, true );
    REQUIRE( TryFlipReplayPredictionCommittedPublication( *state, rootId, frameCount,
                                                          state->revealClock.presentedFrame,
                                                          std::chrono::steady_clock::now(), 0.0 ) );

    CHECK_FALSE( state->committedPublication.pending );
    CHECK_FALSE( state->trajectoryBuild.usingBuildFrames );
    CHECK_FALSE( state->futureNodeCache.futureNodesBuiltFromBuildFrames );
    CHECK( state->futureNodeCache.childMarkerScan.Matches( state->build.generationBeginCount, topologyVersion, 1u,
                                                           rootId, frameCount, state->revealClock.presentedFrame,
                                                           false ) );
    REQUIRE( state->FutureTreeReadyForDraw( rootId, false, frameCount ) );
    const ReplayTrajectoryRecord* committedRoot = findRecord( rootId, ReplayTrajectoryLane::FutureRoot, 0u );
    const ReplayTrajectoryRecord* committedIncoming = findRecord( childId, ReplayTrajectoryLane::FutureChildIncoming, 0u );
    REQUIRE( committedRoot );
    REQUIRE( committedIncoming );
    CHECK( committedRoot->version == hiddenRootVersion );
    CHECK( committedRoot->points[0].position.x == state->simulation.frames[0].bodies[0].position.x );
    CHECK( committedIncoming->version != visibleIncomingVersion );
    CHECK_FALSE( findRecord( rootId, ReplayTrajectoryLane::FutureRoot, 1u ) );
    CHECK_FALSE( findRecord( childId, ReplayTrajectoryLane::FutureChildIncoming,
                             static_cast<uint16_t>( REPLAY_VISUAL_FUTURE_NODE_CAPACITY ) ) );
}


TEST_CASE( "Replay presentation holds the exact pending frame and version bank" )
{
    auto state = std::make_unique<RunReplayPredictionState>();
    state->simulation.frames.resize( 200u );
    state->simulation.committedFrameCount = 200u;
    state->simulation.targetId.value = 41u;
    state->trajectoryStore.publicationVersion = 99u;
    state->committedPublication.pending = true;
    state->committedPublication.visibleFrameCount = 120u;
    state->committedPublication.visibleTrajectoryPublicationVersion = 81u;
    state->committedPublication.visibleTopologyVersion = 7u;
    state->committedPublication.visibleFutureNodesCacheValid = true;
    state->committedPublication.visibleFutureNodes.resize( 2u );
    state->committedPublication.visibleTrajectoryBuild.rootId.value = 41u;
    state->committedPublication.visibleTrajectoryBuild.usingBuildFrames = true;
    state->committedPublication.visibleTrajectoryBuild.childFrameCount = 120u;
    state->committedPublication.visibleTrajectoryBuild.builtNodeCount = 2u;
    state->committedPublication.visibleTrajectoryBuild.topologyVersion = 7u;
    state->committedPublication.visibleTrajectoryBuild.valid = true;

    const ReplayPredictionPresentationView pending = ReplayPrediction::PresentationViewFromState( *state, true );
    CHECK( pending.frames.size() == 120u );
    CHECK( pending.frames.data() == state->simulation.frames.data() );
    CHECK( pending.futureNodes.data() == state->committedPublication.visibleFutureNodes.data() );
    CHECK( pending.trajectoryPublicationVersion == 81u );
    CHECK( pending.futureTreeReady );

    state->trajectoryStore.publicationVersion = 100u;
    const ReplayPredictionPresentationView hiddenSlice = ReplayPrediction::PresentationViewFromState( *state, true );
    CHECK( hiddenSlice.frames.size() == 120u );
    CHECK( hiddenSlice.trajectoryPublicationVersion == 81u );
    CHECK( ReplayOverlay::IsReplayPredictionDrawListPublicationStable(
        false, pending.trajectoryPublicationVersion, pending.revealFrame, hiddenSlice.trajectoryPublicationVersion,
        hiddenSlice.revealFrame ) );

    state->committedPublication.Reset();
    const ReplayPredictionPresentationView flipped = ReplayPrediction::PresentationViewFromState( *state, true );
    CHECK( flipped.frames.size() == 200u );
    CHECK( flipped.trajectoryPublicationVersion == 100u );
}

TEST_CASE( "Replay fast completion retains the committed bank before first build presentation" )
{
    auto state = std::make_unique<RunReplayPredictionState>();
    state->simulation.frames.resize( 120u );
    state->simulation.committedFrameCount = 120u;
    state->simulation.targetId.value = 41u;
    state->trajectoryStore.publicationVersion = 81u;
    state->trajectoryBuild.rootId.value = 41u;
    state->trajectoryBuild.usingBuildFrames = false;
    state->trajectoryBuild.childFrameCount = 120u;
    state->trajectoryBuild.builtNodeCount = 2u;
    state->trajectoryBuild.topologyVersion = 7u;
    state->trajectoryBuild.valid = true;
    state->futureNodeCache.futureNodes.resize( 2u );
    state->futureNodeCache.futureNodesTopologyVersion = 7u;
    state->futureNodeCache.futureNodesCacheValid = true;
    state->committedPublication.visibleFutureNodes.reserve( 2u );

    REQUIRE( state->committedPublication.CaptureVisible( state->trajectoryBuild, state->futureNodeCache,
                                                         state->simulation.targetModelRow, true, false,
                                                         state->CommittedFrameCount(),
                                                         state->trajectoryStore.publicationVersion ) );

    state->build.buildFrames.resize( 200u );
    state->build.building = true;
    state->trajectoryBuild.usingBuildFrames = true;
    state->trajectoryBuild.topologyVersion = 12u;
    state->futureNodeCache.futureNodesTopologyVersion = 12u;
    state->trajectoryStore.publicationVersion = 100u;

    const ReplayPredictionPresentationView building = ReplayPrediction::PresentationViewFromState( *state, true );
    CHECK( building.frames.size() == 120u );
    CHECK( building.frames.data() == state->simulation.frames.data() );
    CHECK( building.topologyVersion == 7u );
    CHECK( building.trajectoryPublicationVersion == 81u );
    CHECK( building.futureTreeReady );

    state->build.building = false;
    state->trajectoryBuild = {};
    const ReplayPredictionPresentationView failedBegin = ReplayPrediction::PresentationViewFromState( *state, true );
    CHECK( failedBegin.frames.size() == 120u );
    CHECK( failedBegin.frames.data() == state->simulation.frames.data() );
    CHECK( failedBegin.topologyVersion == 7u );
    CHECK( failedBegin.trajectoryPublicationVersion == 81u );
    CHECK( failedBegin.futureTreeReady );

    state->PromoteBuildFramesToCommitted( 200u );
    state->committedPublication.visibleFramesUseBuildBank = true;
    REQUIRE( state->committedPublication.ActivateCaptured( 2u, 200u ) );

    const ReplayPredictionPresentationView pending = ReplayPrediction::PresentationViewFromState( *state, true );
    CHECK( pending.frames.size() == 120u );
    CHECK( pending.frames.data() == state->build.buildFrames.data() );
    CHECK( pending.topologyVersion == 7u );
    CHECK( pending.trajectoryPublicationVersion == 81u );
    CHECK( pending.futureTreeReady );
}

TEST_CASE( "Replay promote and begin defers replacement until the promoted trajectory flips" )
{
    auto state = std::make_unique<RunReplayPredictionState>();
    state->simulation.frames.resize( 120u );
    state->simulation.committedFrameCount = 120u;
    state->simulation.targetId.value = 41u;
    state->build.buildFrames.resize( 200u );
    state->simulation.targetModelRow.value = 0;

    for ( std::size_t frameIndex = 0; frameIndex < state->build.buildFrames.size(); ++frameIndex )
    {
        RunReplayPredictionFrame& frame = state->build.buildFrames[frameIndex];
        frame.frameIndex = static_cast<ReplayFrameIndex>( frameIndex );
        frame.bodies.resize( 1u );
        frame.bodies[0].id.value = 41u;
        frame.bodies[0].modelRow.value = 0;
        frame.bodies[0].position.x = static_cast<float>( frameIndex );
    }

    REQUIRE( PrepareReplayPredictionTrajectoryBuild( *state, state->simulation.targetId,
                                                      state->build.buildFrames.size(), 1u ) );

    for ( std::size_t frameIndex = 0; frameIndex < 150u; ++frameIndex )
    {
        REQUIRE( PublishReplayPredictionRootTrajectoryFrame( *state, state->build.buildFrames[frameIndex],
                                                             frameIndex ) );
    }

    REQUIRE( PublishReplayPredictionBuildRootTrajectoryPrefix( *state, 150u ) );
    state->trajectoryBuild.childFrameCount = 150u;
    state->trajectoryBuild.builtNodeCount = 2u;
    state->trajectoryBuild.topologyVersion = 12u;
    state->trajectoryBuild.valid = true;
    state->futureNodeCache.futureNodes.resize( 2u );
    state->futureNodeCache.futureNodesTopologyVersion = 12u;
    state->futureNodeCache.futureNodesCacheValid = true;
    state->committedPublication.visibleFutureNodes.reserve( 2u );

    const auto findPromotedBuildRoot = [&]() -> const ReplayTrajectoryRecord*
    {
        const std::span<const ReplayTrajectoryRecord> records = state->trajectoryStore.ActiveRecords();
        const auto found = std::find_if( records.begin(), records.end(),
                                         []( const ReplayTrajectoryRecord& record )
                                         {
                                             return record.key.lane == ReplayTrajectoryLane::FutureRoot &&
                                                    record.key.bodyId.value == 41u && record.key.branchOrdinal == 1u;
                                         } );
        return found != records.end() ? &*found : nullptr;
    };

    const ReplayTrajectoryRecord* promotedBuildRoot = findPromotedBuildRoot();
    REQUIRE( promotedBuildRoot );
    const uint32_t promotedVersion = promotedBuildRoot->version;
    const std::size_t promotedPointCount = promotedBuildRoot->publishedPointCount;
    const float promotedFirstPointX = promotedBuildRoot->points[0].position.x;

    REQUIRE( state->committedPublication.CaptureVisible( state->trajectoryBuild, state->futureNodeCache,
                                                         state->simulation.targetModelRow, true, true, 150u,
                                                         state->trajectoryStore.publicationVersion ) );
    state->PromoteBuildFramesToCommitted( 200u );
    state->committedPublication.visibleFramesUseBuildBank = false;
    REQUIRE( RebuildReplayPredictionCommittedRootTrajectory( *state ) );
    REQUIRE( state->committedPublication.ActivateCaptured( 2u, 200u ) );

    const ReplayPredictionPresentationView promoted = ReplayPrediction::PresentationViewFromState( *state, true );
    CHECK( promoted.frames.size() == 150u );
    CHECK( promoted.frames.data() == state->simulation.frames.data() );
    CHECK( promoted.usingBuildFrames );
    CHECK( promoted.topologyVersion == 12u );
    CHECK( promoted.futureTreeReady );
    CHECK( state->committedPublication.pending );

    promotedBuildRoot = findPromotedBuildRoot();
    REQUIRE( promotedBuildRoot );
    CHECK( promotedBuildRoot->key.branchOrdinal == 1u );
    CHECK( promotedBuildRoot->version == promotedVersion );
    CHECK( promotedBuildRoot->publishedPointCount == promotedPointCount );
    CHECK( promotedBuildRoot->points[0].position.x == promotedFirstPointX );
}

TEST_CASE( "Replay pending committed publication binds the promoted target until its coherent flip" )
{
    ReplayPredictionCommittedPublicationState publication;
    publication.visibleTrajectoryBuild.rootId.value = 41u;
    publication.visibleTargetModelRow.value = 3;
    publication.visibleTargetAvailable = true;
    publication.visibleSnapshotCaptured = true;
    REQUIRE( publication.ActivateCaptured( 2u, 200u ) );

    const SkullbonezCore::Physics::PhysicsSceneObjectId requestedTarget { 52u };
    const SkullbonezCore::Physics::ModelRowHint requestedModelRow { 7 };
    CHECK( publication.PublicationTargetId( requestedTarget ).value == 41u );
    CHECK( publication.PublicationTargetModelRow( requestedModelRow ).value == 3 );
    CHECK( publication.PublicationTargetAvailable( false ) );

    publication.Reset();
    CHECK( publication.PublicationTargetId( requestedTarget ).value == 52u );
    CHECK( publication.PublicationTargetModelRow( requestedModelRow ).value == 7 );
    CHECK_FALSE( publication.PublicationTargetAvailable( false ) );
}

TEST_CASE( "Replay committed topology allocates one version across budget slices" )
{
    ReplayPredictionCommittedPublicationState publication;
    uint32_t allocationCount = 0u;
    const auto allocateVersion = [&allocationCount]()
    {
        ++allocationCount;
        return 17u + allocationCount;
    };

    CHECK( publication.AcquireReplacementTopologyVersion( allocateVersion ) == 18u );
    CHECK( publication.AcquireReplacementTopologyVersion( allocateVersion ) == 18u );
    CHECK( allocationCount == 1u );

    publication.Reset();
    CHECK( publication.AcquireReplacementTopologyVersion( allocateVersion ) == 19u );
    CHECK( allocationCount == 2u );
}

TEST_CASE( "Replay child marker scan key rejects every publication input change" )
{
    ReplayPredictionChildMarkerScanState scan;
    const SkullbonezCore::Physics::PhysicsSceneObjectId target { 41u };
    const SkullbonezCore::Physics::PhysicsSceneObjectId otherTarget { 42u };
    CHECK_FALSE( scan.Matches( 3u, 7u, 2u, target, 120u, 99u, false ) );

    scan.nodeCount = 2u;
    scan.Commit( 3u, 7u, target, 120u, 99u, false );
    CHECK( scan.Matches( 3u, 7u, 2u, target, 120u, 99u, false ) );
    CHECK_FALSE( scan.Matches( 4u, 7u, 2u, target, 120u, 99u, false ) );
    CHECK_FALSE( scan.Matches( 3u, 8u, 2u, target, 120u, 99u, false ) );
    CHECK_FALSE( scan.Matches( 3u, 7u, 3u, target, 120u, 99u, false ) );
    CHECK_FALSE( scan.Matches( 3u, 7u, 2u, otherTarget, 120u, 99u, false ) );
    CHECK_FALSE( scan.Matches( 3u, 7u, 2u, target, 121u, 99u, false ) );
    CHECK_FALSE( scan.Matches( 3u, 7u, 2u, target, 120u, 100u, false ) );
    CHECK_FALSE( scan.Matches( 3u, 7u, 2u, target, 120u, 99u, true ) );

    scan.Reset();
    CHECK_FALSE( scan.Matches( 3u, 7u, 2u, target, 120u, 99u, false ) );
}

TEST_CASE( "Replay child marker scan preserves stable-node suffix cursors" )
{
    ReplayPredictionChildMarkerScanState scan;
    RunReplayPathTraceNode firstNode;
    firstNode.id.value = 9u;
    firstNode.modelRow.value = 4;
    firstNode.firstFrame = 11u;

    CHECK_FALSE( scan.PreserveOrResetNode( 0u, 0u, firstNode ) );
    scan.nodeCount = 1u;
    scan.nodes[0].scannedFrameCount = 120u;
    scan.nodes[0].active = true;
    scan.nodes[0].lastMotionFrame = 99u;

    CHECK( scan.PreserveOrResetNode( 0u, scan.nodeCount, firstNode ) );
    CHECK( scan.nodes[0].scannedFrameCount == 120u );
    CHECK( scan.nodes[0].active );
    CHECK( scan.nodes[0].lastMotionFrame == 99u );

    RunReplayPathTraceNode changedNode = firstNode;
    changedNode.firstFrame = 12u;
    CHECK_FALSE( scan.PreserveOrResetNode( 0u, scan.nodeCount, changedNode ) );
    CHECK( scan.nodes[0].scannedFrameCount == 0u );
    CHECK_FALSE( scan.nodes[0].active );
    CHECK( scan.nodes[0].node.firstFrame == 12u );
}

TEST_CASE( "Replay child marker suffix accumulation matches a full scan" )
{
    std::array<RunReplayPredictionBodySample, 5u> samples;

    for ( std::size_t i = 0; i < samples.size(); ++i )
    {
        samples[i].id.value = 9u;
        samples[i].modelRow.value = 4;
        samples[i].position.x = static_cast<float>( i );
    }

    const std::array<bool, 5u> moving = { false, true, false, false, true };
    ReplayPredictionChildMarkerNodeScanState full;
    ReplayPredictionChildMarkerNodeScanState incremental;

    for ( std::size_t i = 0; i < samples.size(); ++i )
    {
        full.ObserveBody( i, samples[i], samples[0], moving[i] );
    }

    for ( std::size_t i = 0; i < 3u; ++i )
    {
        incremental.ObserveBody( i, samples[i], samples[0], moving[i] );
    }

    for ( std::size_t i = 3u; i < samples.size(); ++i )
    {
        incremental.ObserveBody( i, samples[i], samples[0], moving[i] );
    }

    CHECK( incremental.active == full.active );
    CHECK( incremental.hasEntryPose == full.hasEntryPose );
    CHECK( incremental.entryModelIndex == full.entryModelIndex );
    CHECK( incremental.entryPosition == full.entryPosition );
    float incrementalX = 0.0f;
    float incrementalY = 0.0f;
    float incrementalZ = 0.0f;
    float incrementalW = 0.0f;
    float fullX = 0.0f;
    float fullY = 0.0f;
    float fullZ = 0.0f;
    float fullW = 0.0f;
    incremental.entryOrientation.GetComponents( incrementalX, incrementalY, incrementalZ, incrementalW );
    full.entryOrientation.GetComponents( fullX, fullY, fullZ, fullW );
    CHECK( incrementalX == fullX );
    CHECK( incrementalY == fullY );
    CHECK( incrementalZ == fullZ );
    CHECK( incrementalW == fullW );
    CHECK( incremental.lastMotionFrame == full.lastMotionFrame );
    CHECK( incremental.lastMotionFrame == 4u );
}

TEST_CASE( "Replay prediction draw cursor resumes at its suffix and reuses stable tokens" )
{
    CHECK( ReplayOverlay::IsReplayPredictionDrawListPublicationStable( false, 19u, 2400, 19u, 2400 ) );
    CHECK_FALSE( ReplayOverlay::IsReplayPredictionDrawListPublicationStable( true, 19u, 2400, 19u, 2400 ) );
    CHECK_FALSE( ReplayOverlay::IsReplayPredictionDrawListPublicationStable( false, 18u, 2400, 19u, 2400 ) );
    CHECK_FALSE( ReplayOverlay::IsReplayPredictionDrawListPublicationStable( false, 19u, 2399, 19u, 2400 ) );

    CHECK( ReplayOverlay::ReplayPredictionFirstUnconsumedPoint( 0u ) == 1u );
    CHECK( ReplayOverlay::ReplayPredictionFirstUnconsumedPoint( 1u ) == 1u );
    CHECK( ReplayOverlay::ReplayPredictionFirstUnconsumedPoint( 128u ) == 128u );
}

TEST_CASE( "Replay retained prediction attachment reuses cached stable submission facts" )
{
    const std::array<float, 4> retainedRibbon = { 1.0f, 2.0f, 3.0f, 4.0f };
    ReplayVisualPacket retained;
    retained.expandedRibbonVertices = retainedRibbon;
    retained.submission.hasGeometry = true;
    retained.submission.vertexHash = HashReplayVisualFloatBuffer( retainedRibbon );
    retained.submission.vertexBytes = retainedRibbon.size() * sizeof( float );
    retained.submission.vertexCount = 6u;
    retained.submission.segmentCount = 1u;

    ReplayVisualPacket frame;
    AttachRetainedPredictionGeometry( frame, retained, 7u, 11u );

    CHECK( frame.retainedPredictionRibbonVertices.data() == retainedRibbon.data() );
    CHECK( frame.retainedPredictionRibbonVertices.size() == retainedRibbon.size() );
    CHECK( frame.retainedPredictionStreamId == 7u );
    CHECK( frame.retainedPredictionRevision == 11u );
    CHECK( frame.submission.vertexHash == retained.submission.vertexHash );
    CHECK( frame.submission.vertexBytes == retained.submission.vertexBytes );
    CHECK( frame.submission.segmentCount == retained.submission.segmentCount );
}

TEST_CASE( "Replay mixed attachment composes cached hashes without rereading retained geometry" )
{
    std::array<float, 4> retainedLines = { 1.0f, 2.0f, 3.0f, 4.0f };
    const std::array<float, 2> frameLines = { 5.0f, 6.0f };
    ReplayVisualPacket retained;
    retained.ordinaryLines = retainedLines;
    retained.submission.hasGeometry = true;
    retained.submission.ordinaryLineHash = HashReplayVisualFloatBuffer( retainedLines );
    retained.submission.ordinaryLineBytes = retainedLines.size() * sizeof( float );
    retained.submission.ordinaryLineVertexCount = 2u;

    const uint64_t frameHash = HashReplayVisualFloatBuffer( frameLines );
    const uint64_t frameBytes = frameLines.size() * sizeof( float );
    const uint64_t expectedHash = CombineReplayVisualSubmissionHashes(
        retained.submission.ordinaryLineHash, retained.submission.ordinaryLineBytes, frameHash, frameBytes );
    const auto attachFrame = [&]()
    {
        ReplayVisualPacket frame;
        frame.ordinaryLines = frameLines;
        frame.submission.hasGeometry = true;
        frame.submission.ordinaryLineHash = frameHash;
        frame.submission.ordinaryLineBytes = frameBytes;
        frame.submission.ordinaryLineVertexCount = 1u;
        AttachRetainedPredictionGeometry( frame, retained, 7u, 11u );
        return frame;
    };

    const ReplayVisualPacket first = attachFrame();
    CHECK( first.submission.ordinaryLineHash == expectedHash );
    CHECK( first.submission.ordinaryLineBytes == retained.submission.ordinaryLineBytes + frameBytes );
    CHECK( first.submission.ordinaryLineVertexCount == 3u );

    // Hazard: changing retained storage without publishing a revision is an
    // invalid producer action. This deliberate mutation proves attachment uses
    // cached revision facts and does not scan the retained bytes.
    retainedLines[0] = 99.0f;
    const ReplayVisualPacket second = attachFrame();
    CHECK( second.submission.ordinaryLineHash == first.submission.ordinaryLineHash );
}

TEST_CASE( "Replay retained ranges preserve canonical geometry across interleaved appends" )
{
    using SkullbonezCore::Rendering::RetainedGeometryRangeToken;
    using SkullbonezCore::Runtime::ReplayOverlay::AppendPredictionRetainedRecord;
    using SkullbonezCore::Runtime::ReplayOverlay::ReplayPredictionRetainedRecord;
    constexpr std::size_t floatsPerRecord =
        SkullbonezCore::Runtime::ReplayOverlay::PREDICTION_TRAJECTORY_FLOATS_PER_RECORD;
    const auto record = []( float ax, float bx, float r, float g, float b ) {
        const SkullbonezCore::Math::Vector::Vector3 start( ax, 0.0f, 0.0f );
        const SkullbonezCore::Math::Vector::Vector3 end( bx, 0.0f, 0.0f );
        return ReplayPredictionRetainedRecord { start, end, 2.0f, r, g, b, 1.0f, 1.0f, 0.0f, start, end };
    };

    std::vector<float> arena( 3u * floatsPerRecord, 0.0f );
    RetainedGeometryRangeToken rangeA = {};
    rangeA.identity = 101u;
    rangeA.firstRecord = 0u;
    rangeA.recordCapacity = 2u;
    rangeA.sourceVersion = 7u;
    RetainedGeometryRangeToken rangeB = {};
    rangeB.identity = 202u;
    rangeB.firstRecord = 2u;
    rangeB.recordCapacity = 1u;
    rangeB.sourceVersion = 9u;
    REQUIRE( AppendPredictionRetainedRecord( arena, rangeA, record( 0.0f, 1.0f, 0.8f, 0.2f, 0.1f ), 0.0001f ) );
    REQUIRE( AppendPredictionRetainedRecord( arena, rangeB, record( 10.0f, 11.0f, 0.1f, 0.4f, 0.9f ), 0.0001f ) );
    const std::array<float, floatsPerRecord> rangeBSnapshot = [&] {
        std::array<float, floatsPerRecord> result = {};
        std::copy_n( arena.begin() + static_cast<std::ptrdiff_t>( 2u * floatsPerRecord ),
                     floatsPerRecord,
                     result.begin() );
        return result;
    }();
    REQUIRE( AppendPredictionRetainedRecord( arena, rangeA, record( 1.0f, 2.0f, 0.7f, 0.3f, 0.1f ), 0.0001f ) );

    CHECK( rangeA.recordCount == 2u );
    CHECK( rangeB.recordCount == 1u );
    CHECK( std::equal( rangeBSnapshot.begin(),
                       rangeBSnapshot.end(),
                       arena.begin() + static_cast<std::ptrdiff_t>( 2u * floatsPerRecord ) ) );

    auto expectedA0 = record( 0.0f, 1.0f, 0.8f, 0.2f, 0.1f ).Packed();
    expectedA0[16] = 2.0f;
    auto expectedA1 = record( 1.0f, 2.0f, 0.7f, 0.3f, 0.1f ).Packed();
    expectedA1[13] = 0.0f;
    const auto expectedB = record( 10.0f, 11.0f, 0.1f, 0.4f, 0.9f ).Packed();
    CHECK( std::equal( expectedA0.begin(), expectedA0.end(), arena.begin() ) );
    CHECK( std::equal(
        expectedA1.begin(), expectedA1.end(), arena.begin() + static_cast<std::ptrdiff_t>( floatsPerRecord ) ) );
    CHECK( std::equal( expectedB.begin(),
                       expectedB.end(),
                       arena.begin() + static_cast<std::ptrdiff_t>( 2u * floatsPerRecord ) ) );
}

TEST_CASE( "Replay retained continuation chunks repair only their shared adjacency tail" )
{
    using SkullbonezCore::Rendering::RetainedGeometryRangeToken;
    using SkullbonezCore::Runtime::ReplayOverlay::AppendPredictionRetainedContinuation;
    using SkullbonezCore::Runtime::ReplayOverlay::AppendPredictionRetainedRecord;
    using SkullbonezCore::Runtime::ReplayOverlay::ReplayPredictionRetainedRecord;
    constexpr std::size_t floatsPerRecord =
        SkullbonezCore::Runtime::ReplayOverlay::PREDICTION_TRAJECTORY_FLOATS_PER_RECORD;
    const auto record = []( float ax, float bx ) {
        const SkullbonezCore::Math::Vector::Vector3 start( ax, 0.0f, 0.0f );
        const SkullbonezCore::Math::Vector::Vector3 end( bx, 0.0f, 0.0f );
        return ReplayPredictionRetainedRecord {
            start, end, 2.0f, 0.8f, 0.2f, 0.1f, 1.0f, 1.0f, 0.0f, start, end
        };
    };
    std::vector<float> arena( 3u * floatsPerRecord, 0.0f );
    RetainedGeometryRangeToken first = {};
    first.firstRecord = 0u;
    first.recordCapacity = 1u;
    first.sourceVersion = 4u;
    RetainedGeometryRangeToken sibling = {};
    sibling.firstRecord = 1u;
    sibling.recordCapacity = 1u;
    RetainedGeometryRangeToken continuation = {};
    continuation.firstRecord = 2u;
    continuation.recordCapacity = 1u;
    REQUIRE( AppendPredictionRetainedRecord( arena, first, record( 0.0f, 1.0f ), 0.0001f ) );
    REQUIRE( AppendPredictionRetainedRecord( arena, sibling, record( 10.0f, 11.0f ), 0.0001f ) );
    const std::array<float, floatsPerRecord> siblingSnapshot = [&] {
        std::array<float, floatsPerRecord> result = {};
        std::copy_n( arena.begin() + static_cast<std::ptrdiff_t>( floatsPerRecord ),
                     floatsPerRecord,
                     result.begin() );
        return result;
    }();

    REQUIRE( AppendPredictionRetainedContinuation(
        arena, first, continuation, record( 1.0f, 2.0f ), 0.0001f ) );
    CHECK( first.sourceVersion == 5u );
    CHECK( arena[16] == 2.0f );
    CHECK( arena[2u * floatsPerRecord + 13u] == 0.0f );
    CHECK( std::equal( siblingSnapshot.begin(),
                       siblingSnapshot.end(),
                       arena.begin() + static_cast<std::ptrdiff_t>( floatsPerRecord ) ) );
}

TEST_CASE( "Replay space prediction draws every body path instead of causal-only paths" )
{
    ReplayTrajectoryRecordKey selectedRoot;
    selectedRoot.bodyId = SkullbonezCore::Physics::PhysicsSceneObjectId{ 10u };
    selectedRoot.lane = ReplayTrajectoryLane::FutureRoot;
    selectedRoot.branchOrdinal = 0u;

    ReplayTrajectoryRecordKey planetPath = selectedRoot;
    planetPath.bodyId = SkullbonezCore::Physics::PhysicsSceneObjectId{ 20u };
    ReplayTrajectoryRecordKey inactivePlanetPath = planetPath;
    inactivePlanetPath.branchOrdinal = 1u;
    ReplayTrajectoryRecordKey causalChild = planetPath;
    causalChild.lane = ReplayTrajectoryLane::FutureChildOutgoing;
    causalChild.branchOrdinal = 3u;

    CHECK( ReplayOverlay::ReplayPredictionDrawsAllBodyRecord( true, planetPath, 0u, selectedRoot.bodyId ) );
    CHECK_FALSE(
        ReplayOverlay::ReplayPredictionDrawsAllBodyRecord( true, inactivePlanetPath, 0u, selectedRoot.bodyId ) );
    CHECK_FALSE( ReplayOverlay::ReplayPredictionDrawsAllBodyRecord( true, selectedRoot, 0u, selectedRoot.bodyId ) );
    CHECK_FALSE( ReplayOverlay::ReplayPredictionDrawsCausalChildRecord( true, causalChild, 0u, 200u ) );
    CHECK( ReplayOverlay::ReplayPredictionDrawsCausalChildRecord( false, causalChild, 0u, 200u ) );
    CHECK( ReplayOverlay::ReplayPredictionUsesAuthoredBodyColor( true, ReplayTrajectoryLane::FutureRoot ) );
    CHECK_FALSE( ReplayOverlay::ReplayPredictionUsesAuthoredBodyColor( false, ReplayTrajectoryLane::FutureRoot ) );
}

TEST_CASE( "Replay visual presentation keeps one prepared worker prefix for the rendered frame" )
{
    ReplayPredictionPublication workerPublication;
    ReplayPredictionPresentationPublication presentationPublication;
    workerPublication.PublishSlot( 0u, 4u );
    workerPublication.PublishSlot( 1u, 4u );
    presentationPublication.Prepare( workerPublication.PublishedCount( 4u ), 4u );
    REQUIRE( presentationPublication.PresentedCount( workerPublication.PublishedCount( 4u ), 4u ) == 2u );

    // Hazard: the worker may release another completed slot between the frame
    // thread's preparation pass and the renderer's packet build. That slot is
    // next-frame input; exposing it now makes child topology blink while its
    // trajectory cache still describes the two-row prepared prefix.
    workerPublication.PublishSlot( 2u, 4u );
    CHECK( workerPublication.PublishedCount( 4u ) == 3u );
    CHECK( presentationPublication.PresentedCount( workerPublication.PublishedCount( 4u ), 4u ) == 2u );
}

TEST_CASE( "Replay visual archive semantic hash stays canonical and content-sensitive" )
{
    constexpr uint64_t visualStateHash = 0x0123456789ABCDEFull;
    constexpr uint64_t exactPacketHash = 0xFEDCBA9876543210ull;
    constexpr uint32_t topologyVersion = 3u;
    constexpr uint64_t reserveGrowthEvents = 0u;
    const uint64_t expected = BuildCanonicalReplayVisualArchiveSemanticHash( visualStateHash,
                                                                             exactPacketHash,
                                                                             topologyVersion,
                                                                             reserveGrowthEvents );

    CHECK( expected == 0x5F1B931D0EE4051Cull );
    CHECK( expected == BuildCanonicalReplayVisualArchiveSemanticHash( visualStateHash,
                                                                      exactPacketHash,
                                                                      topologyVersion,
                                                                      reserveGrowthEvents ) );
    CHECK( expected != BuildCanonicalReplayVisualArchiveSemanticHash( visualStateHash ^ 1u,
                                                                      exactPacketHash,
                                                                      topologyVersion,
                                                                      reserveGrowthEvents ) );
    CHECK( expected != BuildCanonicalReplayVisualArchiveSemanticHash( visualStateHash,
                                                                      exactPacketHash ^ 1u,
                                                                      topologyVersion,
                                                                      reserveGrowthEvents ) );
    CHECK( expected != BuildCanonicalReplayVisualArchiveSemanticHash( visualStateHash,
                                                                      exactPacketHash,
                                                                      topologyVersion + 1u,
                                                                      reserveGrowthEvents ) );
}

TEST_CASE( "Replay visual packet reports semantic divergence before buffer bytes" )
{
    const std::array<float, 4> expectedFloats = { 1.0f, 2.0f, 3.0f, 4.0f };
    const std::array<float, 4> actualFloats = { 1.0f, 9.0f, 3.0f, 4.0f };
    ReplayVisualPacket expected;
    ReplayVisualPacket actual;
    expected.header.revealFrame = 120;
    actual.header.revealFrame = 121;
    expected.ordinaryRibbonSegments = expectedFloats;
    actual.ordinaryRibbonSegments = actualFloats;

    ReplayVisualPacketDifference difference;
    REQUIRE( FindReplayVisualPacketDifference( expected, actual, difference ) );
    CHECK( difference.field == ReplayVisualPacketField::RevealFrame );
    CHECK( difference.buffer == ReplayVisualPacketBuffer::None );
    CHECK( difference.expectedBits == 120u );
    CHECK( difference.actualBits == 121u );
}

TEST_CASE( "Replay visual packet preserves ordered lane and exact float diagnostics" )
{
    const std::array<float, 4> expectedFloats = { 1.0f, 2.0f, 3.0f, 4.0f };
    const std::array<float, 4> reorderedFloats = { 1.0f, 3.0f, 2.0f, 4.0f };
    ReplayVisualPacket expected;
    ReplayVisualPacket actual;
    expected.priorityLines = expectedFloats;
    actual.priorityLines = reorderedFloats;

    ReplayVisualPacketDifference difference;
    REQUIRE( FindReplayVisualPacketDifference( expected, actual, difference ) );
    CHECK( difference.field == ReplayVisualPacketField::Buffer );
    CHECK( difference.buffer == ReplayVisualPacketBuffer::PriorityLines );
    CHECK( difference.floatIndex == 1u );
    CHECK_FALSE( difference.countMismatch );
}

TEST_CASE( "Replay visual packet rejects a one-micron submitted vertex change" )
{
    const std::array<float, 4> expectedFloats = { 1.0f, 2.0f, 3.0f, 4.0f };
    std::array<float, 4> actualFloats = expectedFloats;
    actualFloats[2] += 0.000001f;
    ReplayVisualPacket expected;
    ReplayVisualPacket actual;
    expected.expandedRibbonVertices = expectedFloats;
    actual.expandedRibbonVertices = actualFloats;

    ReplayVisualPacketDifference difference;
    REQUIRE( FindReplayVisualPacketDifference( expected, actual, difference ) );
    CHECK( difference.field == ReplayVisualPacketField::Buffer );
    CHECK( difference.buffer == ReplayVisualPacketBuffer::ExpandedRibbonVertices );
    CHECK( difference.floatIndex == 2u );
    CHECK( difference.expectedBits != difference.actualBits );
    CHECK( HashReplayVisualFloatBuffer( expected.expandedRibbonVertices ) !=
           HashReplayVisualFloatBuffer( actual.expandedRibbonVertices ) );
}

TEST_CASE( "Replay visual fingerprint hashes renderer spans instead of stale submission telemetry" )
{
    const std::array<float, 6> expectedFloats = { 1.0f, 2.0f, 3.0f, 0.2f, 0.3f, 0.4f };
    std::array<float, 6> miswiredFloats = expectedFloats;
    miswiredFloats[1] += 0.000001f;
    ReplayVisualPacket expected;
    ReplayVisualPacket miswired;
    expected.combinedLines = expectedFloats;
    expected.ordinaryLines = expectedFloats;
    miswired.combinedLines = expectedFloats;
    miswired.ordinaryLines = miswiredFloats;

    auto& submission = expected.submission;
    submission.hasGeometry = true;
    submission.ordinaryLineHash = HashReplayVisualFloatBuffer( expectedFloats );
    submission.ordinaryLineBytes = expectedFloats.size() * sizeof( float );
    submission.ordinaryLineVertexCount = 1u;
    const uint64_t emptyStreamHash = HashReplayVisualFloatBuffer( std::span<const float>{} );
    submission.priorityLineHash = emptyStreamHash;
    submission.ordinaryRibbonHash = emptyStreamHash;
    submission.priorityRibbonHash = emptyStreamHash;
    miswired.submission = submission; // Models stats still describing the original tracer vector.

    std::vector<ReplayVisualTrajectoryDigestState> expectedDigests;
    std::vector<ReplayVisualTrajectoryDigestState> miswiredDigests;
    const ReplayVisualPacketFingerprint expectedHash = BuildReplayVisualPacketFingerprint( expected, expectedDigests );
    const ReplayVisualPacketFingerprint miswiredHash = BuildReplayVisualPacketFingerprint( miswired, miswiredDigests );

    const char* expectedMismatch = FindReplayVisualPacketSubmissionSpanMismatch( expected );
    INFO( "unexpected expected-packet mismatch: ", std::string_view( expectedMismatch ? expectedMismatch : "none" ) );
    CHECK( expectedMismatch == nullptr );
    REQUIRE( FindReplayVisualPacketSubmissionSpanMismatch( miswired ) != nullptr );
    CHECK( std::string_view( FindReplayVisualPacketSubmissionSpanMismatch( miswired ) ) ==
           "submission.ordinaryLineHash" );
    CHECK( expectedHash.exactHash != miswiredHash.exactHash );
}

TEST_CASE( "Replay immutable trajectory digest reuse invalidates replaced records" )
{
    std::array<ReplayTrajectoryRecord, 1> records;
    ReplayTrajectoryRecord& record = records[0];
    record.key.bodyId.value = 42u;
    record.key.lane = ReplayTrajectoryLane::FutureRoot;
    record.version = 3u;
    record.points = { { 0u, { 0.0f, 0.0f, 0.0f } }, { 1u, { 1.0f, 0.0f, 0.0f } } };
    record.publishedPointCount = record.points.size();

    ReplayVisualPacket packet;
    packet.header.targetId.value = record.key.bodyId.value;
    packet.header.predictionComplete = true;
    packet.trajectoryRecords = records;

    std::vector<ReplayVisualTrajectoryDigestState> reusableDigests;
    const ReplayVisualPacketFingerprint initial =
        BuildReplayVisualPacketFingerprint( packet,
                                            reusableDigests,
                                            ReplayVisualTrajectoryDigestPolicy::ReuseImmutableRecords );
    const ReplayVisualPacketFingerprint cached =
        BuildReplayVisualPacketFingerprint( packet,
                                            reusableDigests,
                                            ReplayVisualTrajectoryDigestPolicy::ReuseImmutableRecords );
    CHECK( cached.trajectoryStateHash == initial.trajectoryStateHash );

    record.points[1].position.x = 2.0f;
    ++record.version;
    const ReplayVisualPacketFingerprint replaced =
        BuildReplayVisualPacketFingerprint( packet,
                                            reusableDigests,
                                            ReplayVisualTrajectoryDigestPolicy::ReuseImmutableRecords );
    std::vector<ReplayVisualTrajectoryDigestState> strictDigests;
    const ReplayVisualPacketFingerprint strict = BuildReplayVisualPacketFingerprint( packet, strictDigests );
    CHECK( replaced.trajectoryStateHash == strict.trajectoryStateHash );
    CHECK( replaced.trajectoryStateHash != initial.trajectoryStateHash );
}

TEST_CASE( "Replay visual packet rejects an equal prefix with a missing float" )
{
    const std::array<float, 3> complete = { 1.0f, 2.0f, 3.0f };
    const std::array<float, 2> truncated = { 1.0f, 2.0f };
    ReplayVisualPacket expected;
    ReplayVisualPacket actual;
    expected.expandedRibbonVertices = complete;
    actual.expandedRibbonVertices = truncated;

    ReplayVisualPacketDifference difference;
    REQUIRE( FindReplayVisualPacketDifference( expected, actual, difference ) );
    CHECK( difference.buffer == ReplayVisualPacketBuffer::ExpandedRibbonVertices );
    CHECK( difference.floatIndex == 2u );
    CHECK( difference.countMismatch );
}

TEST_CASE( "Replay visual packet preserves causal topology order before render bytes" )
{
    std::array<RunReplayPathTraceNode, 2> expectedNodes;
    expectedNodes[0].id.value = 11u;
    expectedNodes[0].firstFrame = 120u;
    expectedNodes[1].id.value = 12u;
    expectedNodes[1].parentId.value = 11u;
    expectedNodes[1].firstFrame = 140u;
    std::array<RunReplayPathTraceNode, 2> reorderedNodes = { expectedNodes[1], expectedNodes[0] };
    ReplayVisualPacket expected;
    ReplayVisualPacket actual;
    expected.header.futureNodeCount = 2u;
    actual.header.futureNodeCount = 2u;
    expected.futureNodes = expectedNodes;
    actual.futureNodes = reorderedNodes;

    ReplayVisualPacketDifference difference;
    REQUIRE( FindReplayVisualPacketDifference( expected, actual, difference ) );
    CHECK( difference.field == ReplayVisualPacketField::FutureNode );
    CHECK( difference.recordIndex == 0u );
    CHECK( difference.floatIndex == 0u );
    CHECK( difference.expectedBits == 11u );
    CHECK( difference.actualBits == 12u );
}

TEST_CASE( "Replay visual packet reports an exact ghost presentation component" )
{
    std::array<ReplayPredictionGhostDrawRequest, 1> expectedGhosts;
    std::array<ReplayPredictionGhostDrawRequest, 1> actualGhosts;
    expectedGhosts[0].modelRow.value = 4;
    actualGhosts[0].modelRow.value = 4;
    expectedGhosts[0].alpha = 0.25f;
    actualGhosts[0].alpha = 0.5f;
    ReplayVisualPacket expected;
    ReplayVisualPacket actual;
    expected.header.ghostRequestCount = 1u;
    actual.header.ghostRequestCount = 1u;
    expected.ghostRequests = expectedGhosts;
    actual.ghostRequests = actualGhosts;

    ReplayVisualPacketDifference difference;
    REQUIRE( FindReplayVisualPacketDifference( expected, actual, difference ) );
    CHECK( difference.field == ReplayVisualPacketField::GhostRequest );
    CHECK( difference.recordIndex == 0u );
    CHECK( difference.floatIndex == 8u );
    CHECK( difference.expectedBits == std::bit_cast<uint32_t>( 0.25f ) );
    CHECK( difference.actualBits == std::bit_cast<uint32_t>( 0.5f ) );
}

TEST_CASE( "Replay visual packet reports an exact published trajectory point" )
{
    ReplayTrajectoryRecord expectedRecord;
    ReplayTrajectoryRecord actualRecord;
    expectedRecord.key.bodyId.value = 41u;
    actualRecord.key.bodyId.value = 41u;
    expectedRecord.publishedPointCount = 1u;
    actualRecord.publishedPointCount = 1u;
    expectedRecord.points.push_back( ReplayTrajectoryPoint{ 17u, { 1.0f, 2.0f, 3.0f } } );
    actualRecord.points.push_back( ReplayTrajectoryPoint{ 17u, { 1.0f, 2.000001f, 3.0f } } );
    const std::array<ReplayTrajectoryRecord, 1> expectedRecords = { expectedRecord };
    const std::array<ReplayTrajectoryRecord, 1> actualRecords = { actualRecord };
    ReplayVisualPacket expected;
    ReplayVisualPacket actual;
    expected.trajectoryRecords = expectedRecords;
    actual.trajectoryRecords = actualRecords;

    ReplayVisualPacketDifference difference;
    REQUIRE( FindReplayVisualPacketDifference( expected, actual, difference ) );
    CHECK( difference.field == ReplayVisualPacketField::TrajectoryPoint );
    CHECK( difference.recordIndex == 0u );
    CHECK( difference.floatIndex == 2u );
    CHECK( difference.expectedBits == std::bit_cast<uint32_t>( 2.0f ) );
    CHECK( difference.actualBits == std::bit_cast<uint32_t>( 2.000001f ) );
}

TEST_CASE( "Replay visual packet covers a non-vacuous retained horizon pose" )
{
    std::array<ReplayPredictionRetainedMarker, 1> expectedMarkers;
    std::array<ReplayPredictionRetainedMarker, 1> actualMarkers;
    expectedMarkers[0].id.value = 77u;
    actualMarkers[0].id.value = 77u;
    expectedMarkers[0].hasHorizonPose = true;
    actualMarkers[0].hasHorizonPose = true;
    expectedMarkers[0].horizonPosition = { 4.0f, 5.0f, 6.0f };
    actualMarkers[0].horizonPosition = { 4.0f, 5.0f, 6.000001f };
    ReplayVisualPacket expected;
    ReplayVisualPacket actual;
    expected.retainedMarkers = expectedMarkers;
    actual.retainedMarkers = actualMarkers;

    ReplayVisualPacketDifference difference;
    REQUIRE( FindReplayVisualPacketDifference( expected, actual, difference ) );
    CHECK( difference.field == ReplayVisualPacketField::RetainedMarker );
    CHECK( difference.recordIndex == 0u );
    CHECK( difference.floatIndex == 21u );
    CHECK( difference.expectedBits == std::bit_cast<uint32_t>( 6.0f ) );
    CHECK( difference.actualBits == std::bit_cast<uint32_t>( 6.000001f ) );
}

TEST_CASE( "Replay visual packet reports dropped-segment diagnostics" )
{
    ReplayVisualPacket expected;
    ReplayVisualPacket actual;
    expected.trajectoryDiagnostics.droppedSegments[0] = 12u;
    actual.trajectoryDiagnostics.droppedSegments[0] = 13u;

    ReplayVisualPacketDifference difference;
    REQUIRE( FindReplayVisualPacketDifference( expected, actual, difference ) );
    CHECK( difference.field == ReplayVisualPacketField::TrajectoryDiagnostic );
    CHECK( difference.expectedBits == 12u );
    CHECK( difference.actualBits == 13u );
}

TEST_CASE( "Replay visual fingerprint is stable across an unchanged published prefix" )
{
    ReplayTrajectoryRecord record;
    record.key.bodyId.value = 9u;
    record.version = 3u;
    record.publishedPointCount = 2u;
    record.points.push_back( { 0u, { 1.0f, 2.0f, 3.0f } } );
    record.points.push_back( { 1u, { 4.0f, 5.0f, 6.0f } } );
    const std::array<ReplayTrajectoryRecord, 1> records = { record };
    ReplayVisualPacket packet;
    packet.header.sourceFrame = 44u;
    packet.trajectoryRecords = records;
    packet.submission.ordinaryLineHash = 0x1234u;
    std::vector<ReplayVisualTrajectoryDigestState> digests;

    const ReplayVisualPacketFingerprint first = BuildReplayVisualPacketFingerprint( packet, digests );
    const ReplayVisualPacketFingerprint second = BuildReplayVisualPacketFingerprint( packet, digests );
    CHECK( first.semanticHash == second.semanticHash );
    CHECK( first.exactHash == second.exactHash );
}

TEST_CASE( "Replay visual fingerprint catches mutation inside a published prefix" )
{
    std::array<ReplayTrajectoryRecord, 1> records;
    records[0].key.bodyId.value = 9u;
    records[0].version = 3u;
    records[0].publishedPointCount = 2u;
    records[0].points.push_back( { 0u, { 1.0f, 2.0f, 3.0f } } );
    records[0].points.push_back( { 1u, { 4.0f, 5.0f, 6.0f } } );
    ReplayVisualPacket packet;
    packet.trajectoryRecords = records;
    std::vector<ReplayVisualTrajectoryDigestState> digests;

    const ReplayVisualPacketFingerprint before = BuildReplayVisualPacketFingerprint( packet, digests );
    records[0].points[0].position.y = 2.000001f;
    const ReplayVisualPacketFingerprint after = BuildReplayVisualPacketFingerprint( packet, digests );

    CHECK( before.visualStateHash != after.visualStateHash );
    CHECK( before.semanticHash != after.semanticHash );
    CHECK( before.exactHash != after.exactHash );
}

TEST_CASE( "Replay visual fingerprint excludes the completed prediction worker bank" )
{
    std::array<ReplayTrajectoryRecord, 2> expectedRecords;
    expectedRecords[0].key.bodyId.value = 9u;
    expectedRecords[0].key.lane = ReplayTrajectoryLane::FutureChildOutgoing;
    expectedRecords[0].key.branchOrdinal = 0u;
    expectedRecords[0].publishedPointCount = 1u;
    expectedRecords[0].points.push_back( { 0u, { 1.0f, 2.0f, 3.0f } } );
    expectedRecords[1] = expectedRecords[0];
    expectedRecords[1].key.branchOrdinal = REPLAY_VISUAL_FUTURE_NODE_CAPACITY;
    expectedRecords[1].version = 4u;
    std::array<ReplayTrajectoryRecord, 2> actualRecords = expectedRecords;
    actualRecords[1].points[0].position.y = 200.0f;

    ReplayVisualPacket expected;
    ReplayVisualPacket actual;
    expected.header.predictionComplete = true;
    actual.header.predictionComplete = true;
    expected.header.futureNodeCount = 1u;
    actual.header.futureNodeCount = 1u;
    expected.trajectoryRecords = expectedRecords;
    actual.trajectoryRecords = actualRecords;
    std::vector<ReplayVisualTrajectoryDigestState> expectedDigests;
    std::vector<ReplayVisualTrajectoryDigestState> actualDigests;

    const ReplayVisualPacketFingerprint expectedHash = BuildReplayVisualPacketFingerprint( expected, expectedDigests );
    const ReplayVisualPacketFingerprint inactiveMutationHash =
        BuildReplayVisualPacketFingerprint( actual, actualDigests );
    CHECK( expectedHash.visualStateHash == inactiveMutationHash.visualStateHash );
    CHECK( expectedHash.exactHash == inactiveMutationHash.exactHash );
    CHECK( expectedHash.semanticHash != inactiveMutationHash.semanticHash );

    actualRecords[0].points[0].position.y = 2.000001f;
    const ReplayVisualPacketFingerprint activeMutationHash =
        BuildReplayVisualPacketFingerprint( actual, actualDigests );
    CHECK( expectedHash.visualStateHash != activeMutationHash.visualStateHash );
    CHECK( expectedHash.exactHash != activeMutationHash.exactHash );
}

TEST_CASE( "Replay visual fingerprint covers ghost semantics before submission bytes" )
{
    std::array<ReplayPredictionGhostDrawRequest, 1> expectedGhosts;
    std::array<ReplayPredictionGhostDrawRequest, 1> actualGhosts;
    expectedGhosts[0].modelRow.value = 12;
    actualGhosts[0].modelRow.value = 12;
    expectedGhosts[0].position = { 1.0f, 2.0f, 3.0f };
    actualGhosts[0].position = expectedGhosts[0].position;
    expectedGhosts[0].alpha = 0.25f;
    actualGhosts[0].alpha = 0.250001f;
    ReplayVisualPacket expected;
    ReplayVisualPacket actual;
    expected.ghostRequests = expectedGhosts;
    actual.ghostRequests = actualGhosts;
    expected.submission.vertexHash = 0x55u;
    actual.submission.vertexHash = expected.submission.vertexHash;
    std::vector<ReplayVisualTrajectoryDigestState> expectedDigests;
    std::vector<ReplayVisualTrajectoryDigestState> actualDigests;

    const ReplayVisualPacketFingerprint expectedHash = BuildReplayVisualPacketFingerprint( expected, expectedDigests );
    const ReplayVisualPacketFingerprint actualHash = BuildReplayVisualPacketFingerprint( actual, actualDigests );
    CHECK( expectedHash.semanticHash != actualHash.semanticHash );
    CHECK( expectedHash.exactHash != actualHash.exactHash );
}

TEST_CASE( "Replay visual and exact fingerprints exclude process-local diagnostics" )
{
    ReplayVisualPacket expected;
    ReplayVisualPacket actual;
    expected.header.sourceFrame = 7u;
    actual.header.sourceFrame = 7u;
    expected.trajectoryDiagnostics.droppedSegments[0] = 2u;
    actual.trajectoryDiagnostics.droppedSegments[0] = 3u;
    expected.header.replayReserveGrowthEvents = 11u;
    actual.header.replayReserveGrowthEvents = 12u;
    std::vector<ReplayVisualTrajectoryDigestState> expectedDigests;
    std::vector<ReplayVisualTrajectoryDigestState> actualDigests;

    const ReplayVisualPacketFingerprint expectedHash = BuildReplayVisualPacketFingerprint( expected, expectedDigests );
    const ReplayVisualPacketFingerprint actualHash = BuildReplayVisualPacketFingerprint( actual, actualDigests );
    CHECK( expectedHash.visualStateHash == actualHash.visualStateHash );
    CHECK( expectedHash.semanticHash != actualHash.semanticHash );
    CHECK( expectedHash.exactHash == actualHash.exactHash );
}
