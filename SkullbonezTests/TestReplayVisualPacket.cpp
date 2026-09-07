/*
Purpose:
  Locks typed and ordered first-difference behavior for replay visual packets.

Invariants:
  - Packet comparison is bit-exact and order-sensitive.
  - A count mismatch cannot alias an equal common prefix.
  - Presentation keeps the frame prefix prepared on the frame thread even if
    the worker publishes more prediction rows before rendering consumes it.
  - An unchanged publication token and reveal frame cannot mutate draw storage.
  - Selected-causal presentation has exactly one selected root and every child
    trajectory agrees with its future-node ancestry; generic force flags do not
    relax that rule.
*/
#include "../ThirdPtySource/doctest/doctest.h"

#include "../SkullbonezSource/Runtime/Replay/ReplayVisualPacket.h"
#include "../SkullbonezSource/Runtime/Replay/ReplayVisualPacketFingerprint.h"
#include "../SkullbonezSource/Physics/PhysicsEngine.h"
#include "../SkullbonezSource/Runtime/Prediction/ReplayPrediction.h"
#include "../SkullbonezSource/Runtime/Prediction/ReplayPredictionArchive.h"
#include "../SkullbonezSource/Runtime/Prediction/ReplayPredictionSolverEvidenceStore.h"
#include "../SkullbonezSource/Runtime/App/ReplayPredictionDrawing.h"
#include "../SkullbonezSource/Runtime/Prediction/ReplayPredictionPublication.h"
#include "../SkullbonezSource/Runtime/Prediction/ReplayPredictionPublicationOperations.h"
#include "../SkullbonezSource/Runtime/Prediction/ReplayPredictionPublication.MarkerScan.inl"
#include "../SkullbonezSource/Runtime/Replay/ReplayPathPackets.h"

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
RunReplayPredictionState::~RunReplayPredictionState() = default;
} // namespace SkullbonezCore::Runtime

namespace SkullbonezCore::Runtime::ReplayPredictionSchedulingOperations
{
namespace
{
std::optional<std::size_t> forcedBudgetExpiryCheck;
std::size_t budgetExpiryCheckCount = 0u;
} // namespace

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
} // namespace SkullbonezCore::Runtime::ReplayPredictionSchedulingOperations

namespace
{
// Concept: this test oracle validates published shape, not the implementation
// branch that produced it. Selected-causal rows must agree with the durable
// future-node ancestry, while explicit space presentation is a root-only forest.
bool PredictionTopologyMatchesPresentation( std::span<const ReplayTrajectoryRecord> records,
                                            std::span<const RunReplayPathTraceNode> nodes,
                                            SkullbonezCore::Physics::PhysicsSceneObjectId selectedId,
                                            ReplayPredictionPathPresentation presentation )
{
    bool selectedRootPresent = false;

    for ( const ReplayTrajectoryRecord& record : records )
    {
        if ( record.key.lane == ReplayTrajectoryLane::FutureRoot )
        {
            selectedRootPresent = selectedRootPresent || record.key.bodyId.value == selectedId.value;

            if ( presentation == ReplayPredictionPathPresentation::SelectedCausalTree &&
                 record.key.bodyId.value != selectedId.value )
            {
                return false;
            }
        }

        if ( presentation == ReplayPredictionPathPresentation::AllBodiesSpace &&
             ( record.key.lane == ReplayTrajectoryLane::FutureChildIncoming ||
               record.key.lane == ReplayTrajectoryLane::FutureChildOutgoing ) )
        {
            return false;
        }
    }

    if ( presentation == ReplayPredictionPathPresentation::AllBodiesSpace )
    {
        return selectedRootPresent;
    }

    if ( !selectedRootPresent )
    {
        return false;
    }

    // Invariant: node order is causal publication order. A parent must already
    // be reachable from the selected root before its child can become visible.
    std::vector<SkullbonezCore::Physics::PhysicsSceneObjectId> reachableIds = { selectedId };

    for ( const RunReplayPathTraceNode& node : nodes )
    {
        if ( !node.contactDerived )
        {
            return false;
        }

        const bool parentReachable = std::any_of( reachableIds.begin(), reachableIds.end(),
                                                  [&node]( SkullbonezCore::Physics::PhysicsSceneObjectId id )
                                                  { return id.value == node.parentId.value; } );
        const bool alreadyReachable = std::any_of( reachableIds.begin(), reachableIds.end(),
                                                   [&node]( SkullbonezCore::Physics::PhysicsSceneObjectId id )
                                                   { return id.value == node.id.value; } );

        if ( !parentReachable || alreadyReachable )
        {
            return false;
        }

        reachableIds.push_back( node.id );
    }

    for ( const RunReplayPathTraceNode& node : nodes )
    {
        if ( node.id.value == selectedId.value )
        {
            continue;
        }

        const auto matchesNode = [&node]( const ReplayTrajectoryRecord& record )
        {
            return record.key.bodyId.value == node.id.value && record.parentId.value == node.parentId.value &&
                   record.depth == node.depth && record.firstFrame == node.firstFrame;
        };

        const bool hasIncoming = std::any_of( records.begin(), records.end(),
                                              [&]( const ReplayTrajectoryRecord& record )
                                              {
                                                  return record.key.lane == ReplayTrajectoryLane::FutureChildIncoming &&
                                                         matchesNode( record );
                                              } );
        const bool hasOutgoing = std::any_of( records.begin(), records.end(),
                                              [&]( const ReplayTrajectoryRecord& record )
                                              {
                                                  return record.key.lane == ReplayTrajectoryLane::FutureChildOutgoing &&
                                                         matchesNode( record );
                                              } );

        if ( !hasIncoming || !hasOutgoing )
        {
            return false;
        }
    }

    for ( const ReplayTrajectoryRecord& record : records )
    {
        if ( record.key.lane != ReplayTrajectoryLane::FutureChildIncoming &&
             record.key.lane != ReplayTrajectoryLane::FutureChildOutgoing )
        {
            continue;
        }

        const bool hasNode = std::any_of( nodes.begin(), nodes.end(),
                                          [&record]( const RunReplayPathTraceNode& node )
                                          {
                                              return node.id.value == record.key.bodyId.value &&
                                                     node.parentId.value == record.parentId.value &&
                                                     node.depth == record.depth && node.firstFrame == record.firstFrame;
                                          } );

        if ( !hasNode )
        {
            return false;
        }
    }

    return true;
}
} // namespace

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

    auto state = std::make_unique<RunReplayPredictionState>();
    state->simulation.frames.resize( 4u );
    state->simulation.committedFrameCount = 2u;
    CHECK( state->CommittedFrameCount() == 2u );
    CHECK( state->CommittedFrames().size() == 2u );
    state->InvalidateCommittedFrames();
    CHECK( state->CommittedFrames().empty() );
    CHECK( state->simulation.frames.size() == 4u );
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
    REQUIRE( publication->Begin( build, *visibleCache, 9u, 14401u, SkullbonezCore::Physics::ModelRowHint { 3 }, true, false,
                                 120u, 81u ) );
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
    CHECK( RunReplayPredictionState::FutureTreeReadyForDraw( publication->visibleTrajectoryBuild,
                                                             SkullbonezCore::Physics::PhysicsSceneObjectId { 41u }, true,
                                                             14401u, publication->visibleFutureNodes.size(),
                                                             publication->visibleTopologyVersion,
                                                             publication->visibleFutureNodesCacheValid ) );
    CHECK( publication->generation == 9u );
    CHECK( publication->sourceFrameCount == 14401u );
    CHECK( publication->visibleFrameCount == 120u );
    CHECK( publication->PresentedTrajectoryPublicationVersion( 99u ) == 81u );
    CHECK( ReplayOverlay::IsReplayPredictionDrawListPublicationStable( false, 81u, 2400,
                                                                       publication->PresentedTrajectoryPublicationVersion(
                                                                           99u ),
                                                                       2400 ) );

    publication->Reset();
    CHECK_FALSE( publication->pending );
    CHECK( publication->visibleFutureNodes.empty() );
    CHECK( publication->visibleRetainedMarkerCount == 0u );
    CHECK( publication->generation == 0u );
    CHECK( publication->sourceFrameCount == 0u );
    CHECK( publication->PresentedTrajectoryPublicationVersion( 99u ) == 99u );
    CHECK_FALSE(
        ReplayOverlay::IsReplayPredictionDrawListPublicationStable( false, 81u, 2400,
                                                                    publication->PresentedTrajectoryPublicationVersion(
                                                                        99u ),
                                                                    2400 ) );
}

TEST_CASE( "Replay committed all-body publication retains its resume cursor" )
{
    const SkullbonezCore::Physics::PhysicsSceneObjectId rootId { 1u };
    RunReplayPredictionTrajectoryBuildState cursor;
    cursor.rootId = rootId;
    cursor.usingBuildFrames = false;
    cursor.pathPresentation = ReplayPredictionPathPresentation::AllBodiesSpace;
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

TEST_CASE( "Replay committed all-body builder publishes coherent frame slices across budget passes" )
{
    std::vector<RunReplayPredictionFrame> frames( 8u );

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
        state->trajectoryBuild.rootId = rootId;
        state->trajectoryBuild.usingBuildFrames = false;
        state->trajectoryBuild.pathPresentation = ReplayPredictionPathPresentation::AllBodiesSpace;
    }

    ReplayPredictionSchedulingOperations::budgetExpiryCheckCount = 0u;
    ReplayPredictionSchedulingOperations::forcedBudgetExpiryCheck = 6u;
    UpdateReplayPredictionTrajectoryStore( *resumed, frames, frames.size(), false, rootId, std::chrono::steady_clock::now(),
                                           1.0 );
    ReplayPredictionSchedulingOperations::forcedBudgetExpiryCheck.reset();
    CHECK( resumed->trajectoryBuild.pathPresentation == ReplayPredictionPathPresentation::AllBodiesSpace );
    CHECK( resumed->trajectoryBuild.builtAllBodyCount == resumed->trajectoryBuild.allBodyBodyCount );
    CHECK( resumed->trajectoryBuild.allBodyBodyCount == 4u );
    CHECK( resumed->trajectoryBuild.allBodyFrameCount > 0u );
    CHECK( resumed->trajectoryBuild.allBodyFrameCount < frames.size() );

    const auto findCommittedBodyRecord =
        []( const RunReplayPredictionState& state,
            SkullbonezCore::Physics::PhysicsSceneObjectId bodyId ) -> const ReplayTrajectoryRecord*
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

    std::array<uint32_t, 3u> retainedVersions = {};

    for ( std::size_t bodyIndex = 0; bodyIndex < retainedVersions.size(); ++bodyIndex )
    {
        const ReplayTrajectoryRecord* record = findCommittedBodyRecord( *resumed,
                                                                        SkullbonezCore::Physics::PhysicsSceneObjectId {
                                                                            static_cast<uint32_t>( bodyIndex + 2u ) } );
        REQUIRE( record );
        CHECK( record->publishedPointCount == resumed->trajectoryBuild.allBodyFrameCount );
        retainedVersions[bodyIndex] = record->version;
    }

    UpdateReplayPredictionTrajectoryStore( *resumed, frames, frames.size(), false, rootId, std::chrono::steady_clock::now(),
                                           0.0 );
    UpdateReplayPredictionTrajectoryStore( *uninterrupted, frames, frames.size(), false, rootId,
                                           std::chrono::steady_clock::now(), 0.0 );

    CHECK( resumed->trajectoryBuild.builtAllBodyCount == 4u );
    CHECK( resumed->trajectoryBuild.allBodyFrameCount == frames.size() );
    for ( std::size_t bodyIndex = 0; bodyIndex < retainedVersions.size(); ++bodyIndex )
    {
        const ReplayTrajectoryRecord* record = findCommittedBodyRecord( *resumed,
                                                                        SkullbonezCore::Physics::PhysicsSceneObjectId {
                                                                            static_cast<uint32_t>( bodyIndex + 2u ) } );
        REQUIRE( record );
        CHECK( record->version == retainedVersions[bodyIndex] );
    }
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

bool SeedCommittedTrajectoryScheduleRecord( RunReplayPredictionState& state,
                                            std::span<const RunReplayPredictionFrame> frames,
                                            SkullbonezCore::Physics::PhysicsSceneObjectId rootId, ReplayTrajectoryLane lane,
                                            std::size_t pointCount, ReplayFrameIndex firstFrame )
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
}

bool PrepareCommittedTrajectoryScheduleState( RunReplayPredictionState& state,
                                              std::span<const RunReplayPredictionFrame> frames,
                                              SkullbonezCore::Physics::PhysicsSceneObjectId rootId, uint32_t topologyVersion,
                                              std::size_t nodeCount )
{
    state.simulation.frames.assign( frames.begin(), frames.end() );
    state.simulation.committedFrameCount = frames.size();
    state.simulation.targetId = rootId;
    state.simulation.targetModelRow.value = 0;
    state.trajectoryBuild.pathPresentation = ReplayPredictionPathPresentation::AllBodiesSpace;
    state.build.generationBeginCount = 3u;
    state.revealClock.presentedFrame = frames.back().frameIndex;

    if ( !state.trajectoryStore.ReserveRecords( 16u, 0 ) )
    {
        return false;
    }

    // Invariant: these build-child rows model the already-presented first
    // generation. The coherent flip preserves them for legacy packet
    // fidelity; the next replacement generation owns retiring the bank.
    constexpr std::size_t incomingPointCount = 2u;

    if ( !SeedCommittedTrajectoryScheduleRecord( state, frames, rootId, ReplayTrajectoryLane::FutureChildIncoming,
                                                 incomingPointCount, static_cast<ReplayFrameIndex>( incomingPointCount ) ) ||
         !SeedCommittedTrajectoryScheduleRecord( state, frames, rootId, ReplayTrajectoryLane::FutureChildOutgoing,
                                                 frames.size() - incomingPointCount,
                                                 static_cast<ReplayFrameIndex>( incomingPointCount ) ) ||
         !RebuildReplayPredictionCommittedRootTrajectory( state ) )
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
    state.futureNodeCache.futureNodesBuiltTargetId = rootId;
    state.futureNodeCache.futureNodesBuiltFromBuildFrames = false;
    state.futureNodeCache.futureNodesCacheValid = true;
    state.futureNodeCache.retainedMarkers[0].id.value = 2u;
    state.futureNodeCache.retainedMarkers[0].modelRow.value = 1;
    state.futureNodeCache.retainedMarkers[0].hasEntryPose = true;
    state.futureNodeCache.retainedMarkerCount = 1u;
    state.futureNodeCache.childMarkerScan.nodeCount = nodeCount;
    state.futureNodeCache.childMarkerScan.Commit( state.build.generationBeginCount, topologyVersion, rootId, frames.size(),
                                                  state.revealClock.presentedFrame, false, true );

    RunReplayPredictionTrajectoryBuildState visibleBuild;
    visibleBuild.rootId = rootId;
    visibleBuild.usingBuildFrames = true;
    visibleBuild.childFrameCount = frames.size();
    visibleBuild.builtNodeCount = nodeCount;
    visibleBuild.topologyVersion = topologyVersion;
    visibleBuild.valid = true;
    state.committedPublication.visibleFutureNodes.reserve( nodeCount );
    return state.committedPublication.CaptureVisible( visibleBuild, state.futureNodeCache, state.simulation.targetModelRow,
                                                      true, true, frames.size(),
                                                      state.trajectoryStore.publicationVersion ) &&
           state.committedPublication.ActivateCaptured( state.build.generationBeginCount, frames.size() );
}

std::size_t RunCommittedTrajectorySchedule( RunReplayPredictionState& state,
                                            const std::vector<RunReplayPredictionFrame>& frames,
                                            SkullbonezCore::Physics::PhysicsSceneObjectId rootId,
                                            std::span<const std::size_t> expiryChecks )
{
    std::size_t passCount = 0u;

    // Invariant: each forced expiry happens only between indivisible body or
    // child records. A fresh check counter models each frame budget while the
    // production cursors retain the completed prefix.
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
            ReplayPredictionSchedulingOperations::forcedBudgetExpiryCheck = expiryChecks[passCount % expiryChecks.size()];
        }

        const auto budgetStart = std::chrono::steady_clock::now();
        UpdateReplayPredictionTrajectoryStore( state, frames, frames.size(), false, rootId, budgetStart,
                                               budgetMilliseconds );
        const ReplayPredictionPresentationView beforeFlip = ReplayPrediction::PresentationViewFromState( state, true );
        CHECK( beforeFlip.pathPresentation == ReplayPredictionPathPresentation::SelectedCausalTree );
        (void)TryFlipReplayPredictionCommittedPublication( state, rootId, frames.size(), state.revealClock.presentedFrame,
                                                           budgetStart, budgetMilliseconds );
    }

    ReplayPredictionSchedulingOperations::forcedBudgetExpiryCheck.reset();
    return passCount;
}

std::size_t RunCommittedTrajectoryAppendSchedule( RunReplayPredictionState& state,
                                                  const std::vector<RunReplayPredictionFrame>& frames,
                                                  SkullbonezCore::Physics::PhysicsSceneObjectId rootId,
                                                  std::span<const std::size_t> expiryChecks, bool& observedPartialAppend )
{
    std::size_t passCount = 0u;

    for ( ; passCount < 64u && state.trajectoryBuild.childFrameCount < frames.size(); ++passCount )
    {
        ReplayPredictionSchedulingOperations::budgetExpiryCheckCount = 0u;
        const double budgetMilliseconds = expiryChecks.empty() ? 0.0 : 1.0;

        if ( expiryChecks.empty() )
        {
            ReplayPredictionSchedulingOperations::forcedBudgetExpiryCheck.reset();
        }
        else
        {
            ReplayPredictionSchedulingOperations::forcedBudgetExpiryCheck = expiryChecks[passCount % expiryChecks.size()];
        }

        UpdateReplayPredictionTrajectoryStore( state, frames, frames.size(), false, rootId, std::chrono::steady_clock::now(),
                                               budgetMilliseconds );
        observedPartialAppend = observedPartialAppend || ( state.trajectoryBuild.childAppendNodeIndex > 0u &&
                                                           state.trajectoryBuild.childFrameCount < frames.size() );
    }

    ReplayPredictionSchedulingOperations::forcedBudgetExpiryCheck.reset();
    return passCount;
}

void CheckCommittedTrajectoryRecordsEqual( std::span<const ReplayTrajectoryRecord> expectedRecords,
                                           std::span<const ReplayTrajectoryRecord> actualRecords )
{
    REQUIRE( expectedRecords.size() == actualRecords.size() );

    for ( std::size_t recordIndex = 0; recordIndex < expectedRecords.size(); ++recordIndex )
    {
        const ReplayTrajectoryRecord& lhs = expectedRecords[recordIndex];
        const ReplayTrajectoryRecord& rhs = actualRecords[recordIndex];
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

    auto narrowSlices = std::make_unique<RunReplayPredictionState>();
    auto variedSlices = std::make_unique<RunReplayPredictionState>();
    auto uninterrupted = std::make_unique<RunReplayPredictionState>();
    REQUIRE( PrepareCommittedTrajectoryScheduleState( *narrowSlices, frames, rootId, topologyVersion, nodeCount ) );
    REQUIRE( PrepareCommittedTrajectoryScheduleState( *variedSlices, frames, rootId, topologyVersion, nodeCount ) );
    REQUIRE( PrepareCommittedTrajectoryScheduleState( *uninterrupted, frames, rootId, topologyVersion, nodeCount ) );
    const ReplayPredictionPresentationView pending = ReplayPrediction::PresentationViewFromState( *narrowSlices, true );
    CHECK( pending.topology.treeReady );
    CHECK( pending.pathPresentation == ReplayPredictionPathPresentation::SelectedCausalTree );
    CHECK( pending.timeline.usingBuildFrames );
    CHECK( pending.markers.retainedMarkers.size() == 1u );
    const std::array<std::size_t, 1u> narrowSchedule { 1u };
    const std::array<std::size_t, 4u> variedSchedule { 3u, 1u, 4u, 2u };
    const std::size_t narrowPassCount = RunCommittedTrajectorySchedule( *narrowSlices, frames, rootId, narrowSchedule );
    const std::size_t variedPassCount = RunCommittedTrajectorySchedule( *variedSlices, frames, rootId, variedSchedule );
    const std::size_t uninterruptedPassCount = RunCommittedTrajectorySchedule( *uninterrupted, frames, rootId, {} );

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
    CHECK( narrowSlices->trajectoryBuild.pathPresentation == ReplayPredictionPathPresentation::AllBodiesSpace );
    CHECK( variedSlices->trajectoryBuild.pathPresentation == ReplayPredictionPathPresentation::AllBodiesSpace );
    CHECK( uninterrupted->trajectoryBuild.pathPresentation == ReplayPredictionPathPresentation::AllBodiesSpace );

    const std::size_t originalFrameCount = frames.size();
    frames.resize( originalFrameCount + 3u );

    for ( std::size_t frameIndex = originalFrameCount; frameIndex < frames.size(); ++frameIndex )
    {
        RunReplayPredictionFrame& frame = frames[frameIndex];
        frame.frameIndex = static_cast<ReplayFrameIndex>( frameIndex );
        frame.bodies.resize( bodyCount );

        for ( std::size_t bodyIndex = 0u; bodyIndex < frame.bodies.size(); ++bodyIndex )
        {
            RunReplayPredictionBodySample& body = frame.bodies[bodyIndex];
            body.id.value = static_cast<uint32_t>( bodyIndex + 1u );
            body.modelRow.value = static_cast<int>( bodyIndex );
            body.position.x = static_cast<float>( frameIndex * 10u + bodyIndex );
            body.position.y = static_cast<float>( bodyIndex * 3u );
            body.position.z = static_cast<float>( frameIndex + bodyIndex * 2u );
        }
    }

    const std::array<std::size_t, 2u> variedAppendSchedule { 2u, 1u };
    bool narrowObservedPartialAppend = false;
    bool variedObservedPartialAppend = false;
    bool uninterruptedObservedPartialAppend = false;
    const std::size_t narrowAppendPassCount = RunCommittedTrajectoryAppendSchedule( *narrowSlices, frames, rootId,
                                                                                    narrowSchedule,
                                                                                    narrowObservedPartialAppend );
    const std::size_t variedAppendPassCount = RunCommittedTrajectoryAppendSchedule( *variedSlices, frames, rootId,
                                                                                    variedAppendSchedule,
                                                                                    variedObservedPartialAppend );
    const std::size_t
        uninterruptedAppendPassCount = RunCommittedTrajectoryAppendSchedule( *uninterrupted, frames, rootId, {},
                                                                             uninterruptedObservedPartialAppend );
    CHECK( narrowAppendPassCount > 1u );
    CHECK( variedAppendPassCount > 1u );
    CHECK( uninterruptedAppendPassCount == 1u );
    CHECK( narrowObservedPartialAppend );
    CHECK( variedObservedPartialAppend );
    CHECK_FALSE( uninterruptedObservedPartialAppend );
    CHECK( narrowSlices->trajectoryBuild.childFrameCount == frames.size() );
    CHECK( variedSlices->trajectoryBuild.childFrameCount == frames.size() );
    CHECK( uninterrupted->trajectoryBuild.childFrameCount == frames.size() );
    CHECK( narrowSlices->trajectoryBuild.childAppendNodeIndex == 0u );
    CHECK( variedSlices->trajectoryBuild.childAppendNodeIndex == 0u );

    const std::span<const ReplayTrajectoryRecord> narrowRecords = narrowSlices->trajectoryStore.ActiveRecords();
    // Invariant: the first coherent flip preserves the two already-presented
    // build child records beside the canonical five-body and six-child bank.
    // The next replacement generation owns retiring those build rows.
    REQUIRE( narrowRecords.size() == bodyCount + nodeCount * 2u + 2u );

    for ( const RunReplayPredictionState* candidate : { variedSlices.get(), uninterrupted.get() } )
    {
        CHECK( narrowSlices->trajectoryBuild.rootId.value == candidate->trajectoryBuild.rootId.value );
        CHECK( narrowSlices->trajectoryBuild.usingBuildFrames == candidate->trajectoryBuild.usingBuildFrames );
        CHECK( narrowSlices->trajectoryBuild.childFrameCount == candidate->trajectoryBuild.childFrameCount );
        CHECK( narrowSlices->trajectoryBuild.builtNodeCount == candidate->trajectoryBuild.builtNodeCount );
        CHECK( narrowSlices->trajectoryBuild.childAppendTargetFrameCount ==
               candidate->trajectoryBuild.childAppendTargetFrameCount );
        CHECK( narrowSlices->trajectoryBuild.childAppendNodeIndex == candidate->trajectoryBuild.childAppendNodeIndex );
        CHECK( narrowSlices->trajectoryBuild.allBodyFrameCount == candidate->trajectoryBuild.allBodyFrameCount );
        CHECK( narrowSlices->trajectoryBuild.builtAllBodyCount == candidate->trajectoryBuild.builtAllBodyCount );
        CHECK( narrowSlices->trajectoryBuild.allBodyBodyCount == candidate->trajectoryBuild.allBodyBodyCount );
        CHECK( narrowSlices->trajectoryBuild.topologyVersion == candidate->trajectoryBuild.topologyVersion );
        CHECK( narrowSlices->trajectoryStore.publicationVersion == candidate->trajectoryStore.publicationVersion );
        const std::span<const ReplayTrajectoryRecord> candidateRecords = candidate->trajectoryStore.ActiveRecords();
        CheckCommittedTrajectoryRecordsEqual( narrowRecords, candidateRecords );
    }

    const ReplayPredictionPresentationView narrowView = ReplayPrediction::PresentationViewFromState( *narrowSlices, true );
    const ReplayPredictionPresentationView variedView = ReplayPrediction::PresentationViewFromState( *variedSlices, true );
    CHECK( narrowView.topology.treeReady );
    CHECK( variedView.topology.treeReady );
    CHECK( narrowView.pathPresentation == ReplayPredictionPathPresentation::AllBodiesSpace );
    CHECK( variedView.pathPresentation == ReplayPredictionPathPresentation::AllBodiesSpace );
    CHECK_FALSE( narrowView.timeline.usingBuildFrames );
    CHECK_FALSE( variedView.timeline.usingBuildFrames );
    CHECK( narrowView.topology.targetId.value == variedView.topology.targetId.value );
    CHECK( narrowView.topology.version == variedView.topology.version );
    CHECK( narrowView.trajectory.publicationVersion == variedView.trajectory.publicationVersion );
    CHECK( narrowView.timeline.frames.size() == variedView.timeline.frames.size() );
    CHECK( narrowView.topology.futureNodes.size() == variedView.topology.futureNodes.size() );
    CHECK( narrowView.markers.retainedMarkers.size() == variedView.markers.retainedMarkers.size() );

    ReplayVisualPacket narrowPacket;
    narrowPacket.header.sourceFrame = narrowView.timeline.sourceFrame;
    narrowPacket.header.revealFrame = narrowView.timeline.revealFrame;
    narrowPacket.header.targetId = narrowView.topology.targetId;
    narrowPacket.header.topologyVersion = narrowView.topology.version;
    narrowPacket.header.publishedFrameCount = static_cast<uint32_t>( narrowView.timeline.frames.size() );
    narrowPacket.header.futureNodeCount = static_cast<uint32_t>( narrowView.topology.futureNodes.size() );
    narrowPacket.header.predictionEnabled = true;
    narrowPacket.header.predictionComplete = true;
    narrowPacket.trajectoryRecords = narrowView.trajectory.records;
    narrowPacket.futureNodes = narrowView.topology.futureNodes;
    narrowPacket.retainedMarkers = narrowView.markers.retainedMarkers;
    ReplayVisualPacket variedPacket = narrowPacket;
    variedPacket.trajectoryRecords = variedView.trajectory.records;
    variedPacket.futureNodes = variedView.topology.futureNodes;
    variedPacket.retainedMarkers = variedView.markers.retainedMarkers;
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

TEST_CASE( "Replay child outgoing trajectory starts at the collision frame" )
{
    constexpr std::size_t frameCount = 4u;
    const SkullbonezCore::Physics::PhysicsSceneObjectId rootId { 1u };
    const SkullbonezCore::Physics::PhysicsSceneObjectId childId { 2u };
    auto state = std::make_unique<RunReplayPredictionState>();
    std::vector<RunReplayPredictionFrame> frames( frameCount );

    for ( std::size_t frameIndex = 0; frameIndex < frames.size(); ++frameIndex )
    {
        frames[frameIndex].frameIndex = static_cast<ReplayFrameIndex>( frameIndex );
        frames[frameIndex].bodies.resize( 2u );
        frames[frameIndex].bodies[0].id = rootId;
        frames[frameIndex].bodies[0].modelRow.value = 0;
        frames[frameIndex].bodies[1].id = childId;
        frames[frameIndex].bodies[1].modelRow.value = 1;
        frames[frameIndex].bodies[1].position.x = static_cast<float>( 100u + frameIndex );
    }

    RunReplayPathTraceNode node;
    node.id = childId;
    node.parentId = rootId;
    node.modelRow.value = 1;
    node.parentModelRow.value = 0;
    node.firstFrame = 2u;
    node.depth = 1;
    node.contactDerived = true;
    state->futureNodeCache.futureNodes.push_back( node );
    state->futureNodeCache.futureNodesTopologyVersion = 1u;
    state->trajectoryBuild.rootId = rootId;
    state->trajectoryBuild.usingBuildFrames = false;
    state->trajectoryBuild.pathPresentation = ReplayPredictionPathPresentation::SelectedCausalTree;
    state->trajectoryBuild.valid = true;
    REQUIRE( state->trajectoryStore.ReserveRecords( 4u, 0 ) );

    UpdateReplayPredictionTrajectoryStore( *state, frames, frames.size(), false, rootId, std::chrono::steady_clock::now(),
                                           0.0 );

    ReplayTrajectoryRecordKey incomingKey { childId, ReplayTrajectoryLane::FutureChildIncoming, 0u };
    ReplayTrajectoryRecordKey outgoingKey { childId, ReplayTrajectoryLane::FutureChildOutgoing, 0u };
    const ReplayTrajectoryRecord* incoming = state->trajectoryStore.FindRecord( incomingKey );
    const ReplayTrajectoryRecord* outgoing = state->trajectoryStore.FindRecord( outgoingKey );
    REQUIRE( incoming );
    REQUIRE( outgoing );
    REQUIRE( incoming->points.size() == 3u );
    REQUIRE( outgoing->points.size() == 2u );
    CHECK( incoming->points.front().frameIndex == 0u );
    CHECK( incoming->points.back().frameIndex == node.firstFrame );
    CHECK( outgoing->points.front().frameIndex == node.firstFrame );
    CHECK( outgoing->points.front().position.x == frames[node.firstFrame].bodies[1].position.x );
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

    const auto seedVisibleRecord = [&]( SkullbonezCore::Physics::PhysicsSceneObjectId bodyId, ReplayTrajectoryLane lane,
                                        SkullbonezCore::Physics::PhysicsSceneObjectId parentId )
    {
        ReplayTrajectoryRecordKey key;
        key.bodyId = bodyId;
        key.lane = lane;
        key.branchOrdinal = 0u;
        ReplayTrajectoryRecord* record = state->trajectoryStore
                                             .BeginReplaceRecord( key, lane == ReplayTrajectoryLane::FutureRoot ? 0u : 1u,
                                                                  parentId, lane == ReplayTrajectoryLane::FutureRoot ? 0 : 1,
                                                                  2u, lane != ReplayTrajectoryLane::FutureRoot, frameCount );

        if ( !record || !state->trajectoryStore.ReserveRecordPoints( *record, frameCount, 0 ) )
        {
            return false;
        }

        for ( const RunReplayPredictionFrame& frame : oldVisibleFrames )
        {
            const std::size_t bodyIndex = bodyId.value == rootId.value ? 0u : 1u;

            if ( !state->trajectoryStore.TryAppendPoint( *record, { frame.frameIndex, frame.bodies[bodyIndex].position } ) )
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

    REQUIRE( RebuildReplayPredictionReplacementRootTrajectory( *state, ReplayPredictionTrajectoryBank::Build ) );
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
    CHECK( ReplayPrediction::PresentationViewFromState( *state, true ).trajectory.publicationVersion ==
           visiblePublicationVersion );

    const ReplayTrajectoryRecord* hiddenRoot = findRecord( rootId, ReplayTrajectoryLane::FutureRoot, 1u );
    REQUIRE( hiddenRoot );
    const uint32_t hiddenRootVersion = hiddenRoot->version;
    CHECK( hiddenRoot->points[0].position.x == state->simulation.frames[0].bodies[0].position.x );
    state->futureNodeCache.childMarkerScan.nodeCount = 1u;
    state->futureNodeCache.childMarkerScan.Commit( state->build.generationBeginCount, topologyVersion, rootId, frameCount,
                                                   state->revealClock.presentedFrame, true, false );
    CHECK_FALSE( TryFlipReplayPredictionCommittedPublication( *state, rootId, frameCount, state->revealClock.presentedFrame,
                                                              std::chrono::steady_clock::now(), 0.0 ) );
    CHECK( state->committedPublication.pending );
    state->futureNodeCache.childMarkerScan.Commit( state->build.generationBeginCount, topologyVersion, rootId, frameCount,
                                                   state->revealClock.presentedFrame, true, true );
    REQUIRE( TryFlipReplayPredictionCommittedPublication( *state, rootId, frameCount, state->revealClock.presentedFrame,
                                                          std::chrono::steady_clock::now(), 0.0 ) );

    CHECK_FALSE( state->committedPublication.pending );
    CHECK_FALSE( state->trajectoryBuild.usingBuildFrames );
    CHECK_FALSE( state->futureNodeCache.futureNodesBuiltFromBuildFrames );
    CHECK( state->futureNodeCache.childMarkerScan.Matches( state->build.generationBeginCount, topologyVersion, 1u, rootId,
                                                           frameCount, state->revealClock.presentedFrame, false, true ) );
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
    state->simulation.targetId.value = 99u;
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
    CHECK( pending.timeline.frames.size() == 120u );
    CHECK( pending.timeline.frames.data() == state->simulation.frames.data() );
    CHECK( pending.topology.futureNodes.data() == state->committedPublication.visibleFutureNodes.data() );
    CHECK( pending.topology.targetId.value == 41u );
    CHECK( pending.trajectory.publicationVersion == 81u );
    CHECK( pending.topology.treeReady );

    state->trajectoryStore.publicationVersion = 100u;
    const ReplayPredictionPresentationView hiddenSlice = ReplayPrediction::PresentationViewFromState( *state, true );
    CHECK( hiddenSlice.timeline.frames.size() == 120u );
    CHECK( hiddenSlice.trajectory.publicationVersion == 81u );
    CHECK( ReplayOverlay::IsReplayPredictionDrawListPublicationStable( false, pending.trajectory.publicationVersion,
                                                                       pending.timeline.revealFrame,
                                                                       hiddenSlice.trajectory.publicationVersion,
                                                                       hiddenSlice.timeline.revealFrame ) );

    state->committedPublication.Reset();
    const ReplayPredictionPresentationView flipped = ReplayPrediction::PresentationViewFromState( *state, true );
    CHECK( flipped.timeline.frames.size() == 200u );
    CHECK( flipped.trajectory.publicationVersion == 100u );
}

TEST_CASE( "Replay prediction archive keeps one coherent pending publication" )
{
    auto state = std::make_unique<RunReplayPredictionState>();
    state->build.complete = true;
    state->build.buildFrames.resize( 2u );
    state->build.buildFrames[0].frameIndex = 10u;
    state->build.buildFrames[1].frameIndex = 11u;
    state->simulation.targetId.value = 41u;
    state->simulation.targetModelRow.value = 3;

    ReplayTrajectoryRecordKey visibleKey;
    visibleKey.bodyId.value = 52u;
    visibleKey.lane = ReplayTrajectoryLane::FutureChildIncoming;
    visibleKey.branchOrdinal = static_cast<uint16_t>( REPLAY_VISUAL_FUTURE_NODE_CAPACITY );
    REQUIRE( state->trajectoryStore.ReserveRecords( 2u, 0 ) );
    ReplayTrajectoryRecord* visibleRecord = state->trajectoryStore.BeginReplaceRecord( visibleKey, 7u,
                                                                                       state->simulation.targetId, 1, 10u,
                                                                                       true, 1u );
    REQUIRE( visibleRecord );
    REQUIRE( state->trajectoryStore.ReserveRecordPoints( *visibleRecord, 1u, 0 ) );
    REQUIRE( state->trajectoryStore.TryAppendPoint( *visibleRecord, { 10u, { 3.0f, 4.0f, 5.0f } } ) );
    state->trajectoryStore.PublishPrefix( *visibleRecord, 1u );

    ReplayTrajectoryRecordKey inactiveKey = visibleKey;
    inactiveKey.bodyId.value = 53u;
    inactiveKey.branchOrdinal = 0u;
    ReplayTrajectoryRecord* inactiveRecord = state->trajectoryStore.BeginReplaceRecord( inactiveKey, 9u,
                                                                                        state->simulation.targetId, 1, 10u,
                                                                                        true, 1u );
    REQUIRE( inactiveRecord );
    REQUIRE( state->trajectoryStore.ReserveRecordPoints( *inactiveRecord, 1u, 0 ) );
    REQUIRE( state->trajectoryStore.TryAppendPoint( *inactiveRecord, { 10u, { 90.0f, 91.0f, 92.0f } } ) );
    state->trajectoryStore.PublishPrefix( *inactiveRecord, 1u );
    inactiveRecord->publishedPointCount = 2u;

    RunReplayPathTraceNode visibleNode;
    visibleNode.id = visibleKey.bodyId;
    visibleNode.parentId = state->simulation.targetId;
    visibleNode.modelRow.value = 5;
    visibleNode.parentModelRow = state->simulation.targetModelRow;
    visibleNode.firstFrame = 10u;
    visibleNode.depth = 1;
    visibleNode.contactDerived = true;
    state->futureNodeCache.futureNodes.push_back( visibleNode );
    state->futureNodeCache.futureNodesTopologyVersion = 12u;
    state->futureNodeCache.futureNodesCacheValid = true;
    state->futureNodeCache.futureNodesBuiltRagdollVisuals = true;
    state->futureNodeCache.retainedMarkers[0].id = visibleKey.bodyId;
    state->futureNodeCache.retainedMarkers[0].modelRow = visibleNode.modelRow;
    state->futureNodeCache.retainedMarkers[0].hasEntryPose = true;
    state->futureNodeCache.retainedMarkers[0].entryPosition = { 6.0f, 7.0f, 8.0f };
    state->futureNodeCache.retainedMarkerCount = 1u;
    RunReplayPredictionTrajectoryBuildState visibleBuild;
    visibleBuild.rootId = state->simulation.targetId;
    visibleBuild.usingBuildFrames = true;
    visibleBuild.childFrameCount = state->build.buildFrames.size();
    visibleBuild.builtNodeCount = 1u;
    visibleBuild.topologyVersion = state->futureNodeCache.futureNodesTopologyVersion;
    visibleBuild.valid = true;
    state->committedPublication.visibleFutureNodes.reserve( 1u );
    REQUIRE( state->committedPublication.Begin( visibleBuild, state->futureNodeCache, 4u, state->build.buildFrames.size(),
                                                state->simulation.targetModelRow, true, true,
                                                state->build.buildFrames.size(),
                                                state->trajectoryStore.publicationVersion ) );

    // Hazard: worker setup may immediately repurpose the live cache and the
    // opposite trajectory bank. Saving must still consume only the captured
    // publication that readers can see until the coherent flip.
    state->futureNodeCache.futureNodes[0].id.value = 99u;
    state->futureNodeCache.futureNodesTopologyVersion = 44u;
    state->futureNodeCache.futureNodesBuiltRagdollVisuals = false;
    state->futureNodeCache.retainedMarkers[0].id.value = 98u;
    state->futureNodeCache.retainedMarkers[0].entryPosition = { 96.0f, 97.0f, 98.0f };

    RunReplayPathVisualizerState pathVisualizer;
    auto evidence = std::make_unique<ReplayPredictionSolverEvidenceBanks>();
    std::vector<uint8_t> archiveBytes;
    REQUIRE( ReplayPredictionArchiveOperations::BuildReplayPredictionArchive( pathVisualizer, *state,
                                                                              ReplayPredictionDetailMode::Low,
                                                                              evidence->Committed(), archiveBytes ) );

    RunReplayPathVisualizerState restoredPathVisualizer;
    auto restored = std::make_unique<RunReplayPredictionState>();
    auto restoredEvidence = std::make_unique<ReplayPredictionSolverEvidenceBanks>();
    ReplayPredictionArchiveDetailCapability restoredCapability = ReplayPredictionArchiveDetailCapability::High;
    char reason[256] = {};
    REQUIRE(
        ReplayPredictionArchiveOperations::LoadReplayPredictionArchive( archiveBytes, restoredPathVisualizer, *restored,
                                                                        *restoredEvidence, ReplayPredictionDetailMode::Low,
                                                                        restoredCapability, reason, sizeof( reason ) ) );
    CHECK( restoredCapability == ReplayPredictionArchiveDetailCapability::Low );

    const ReplayTrajectoryRecord* restoredRecord = restored->trajectoryStore.FindRecord( visibleKey );
    REQUIRE( restoredRecord );
    REQUIRE( restoredRecord->points.size() == 1u );
    CHECK( restoredRecord->publishedPointCount == 1u );
    CHECK( restoredRecord->points[0].position.x == 3.0f );
    CHECK( restoredRecord->points[0].position.y == 4.0f );
    CHECK( restoredRecord->points[0].position.z == 5.0f );
    CHECK_FALSE( restored->trajectoryStore.FindRecord( inactiveKey ) );
    REQUIRE( restored->futureNodeCache.futureNodes.size() == 1u );
    CHECK( restored->futureNodeCache.futureNodes[0].id.value == visibleKey.bodyId.value );
    CHECK( restored->futureNodeCache.futureNodesTopologyVersion == restored->trajectoryBuild.topologyVersion );
    REQUIRE( restored->futureNodeCache.retainedMarkerCount == 1u );
    CHECK( restored->futureNodeCache.retainedMarkers[0].id.value == visibleKey.bodyId.value );
    CHECK( restored->futureNodeCache.retainedMarkers[0].entryPosition.x == 6.0f );
    CHECK( restored->futureNodeCache.retainedMarkers[0].entryPosition.y == 7.0f );
    CHECK( restored->futureNodeCache.retainedMarkers[0].entryPosition.z == 8.0f );
    CHECK( restored->futureNodeCache.futureNodesBuiltRagdollVisuals );
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
    CHECK( building.timeline.frames.size() == 120u );
    CHECK( building.timeline.frames.data() == state->simulation.frames.data() );
    CHECK( building.topology.version == 7u );
    CHECK( building.trajectory.publicationVersion == 81u );
    CHECK( building.topology.treeReady );

    state->build.building = false;
    state->trajectoryBuild = {};
    const ReplayPredictionPresentationView failedBegin = ReplayPrediction::PresentationViewFromState( *state, true );
    CHECK( failedBegin.timeline.frames.size() == 120u );
    CHECK( failedBegin.timeline.frames.data() == state->simulation.frames.data() );
    CHECK( failedBegin.topology.version == 7u );
    CHECK( failedBegin.trajectory.publicationVersion == 81u );
    CHECK( failedBegin.topology.treeReady );

    state->PromoteBuildFramesToCommitted( 200u );
    state->committedPublication.visibleFramesUseBuildBank = true;
    REQUIRE( state->committedPublication.ActivateCaptured( 2u, 200u ) );

    const ReplayPredictionPresentationView pending = ReplayPrediction::PresentationViewFromState( *state, true );
    CHECK( pending.timeline.frames.size() == 120u );
    CHECK( pending.timeline.frames.data() == state->build.buildFrames.data() );
    CHECK( pending.topology.version == 7u );
    CHECK( pending.trajectory.publicationVersion == 81u );
    CHECK( pending.topology.treeReady );
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

    REQUIRE( PrepareReplayPredictionTrajectoryBuild( *state, state->simulation.targetId, state->build.buildFrames.size(), 1u,
                                                     ReplayPredictionPathPresentation::SelectedCausalTree ) );

    for ( std::size_t frameIndex = 0; frameIndex < 150u; ++frameIndex )
    {
        REQUIRE( PublishReplayPredictionRootTrajectoryFrame( *state, state->build.buildFrames[frameIndex], frameIndex ) );
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
    CHECK( promoted.timeline.frames.size() == 150u );
    CHECK( promoted.timeline.frames.data() == state->simulation.frames.data() );
    CHECK( promoted.timeline.usingBuildFrames );
    CHECK( promoted.topology.version == 12u );
    CHECK( promoted.topology.treeReady );
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
    CHECK_FALSE( scan.Matches( 3u, 7u, 2u, target, 120u, 99u, false, false ) );

    scan.nodeCount = 2u;
    scan.Commit( 3u, 7u, target, 120u, 99u, false, false );
    CHECK( scan.Matches( 3u, 7u, 2u, target, 120u, 99u, false, false ) );
    CHECK_FALSE( scan.Matches( 4u, 7u, 2u, target, 120u, 99u, false, false ) );
    CHECK_FALSE( scan.Matches( 3u, 8u, 2u, target, 120u, 99u, false, false ) );
    CHECK_FALSE( scan.Matches( 3u, 7u, 3u, target, 120u, 99u, false, false ) );
    CHECK_FALSE( scan.Matches( 3u, 7u, 2u, otherTarget, 120u, 99u, false, false ) );
    CHECK_FALSE( scan.Matches( 3u, 7u, 2u, target, 121u, 99u, false, false ) );
    CHECK_FALSE( scan.Matches( 3u, 7u, 2u, target, 120u, 100u, false, false ) );
    CHECK_FALSE( scan.Matches( 3u, 7u, 2u, target, 120u, 99u, true, false ) );
    CHECK_FALSE( scan.Matches( 3u, 7u, 2u, target, 120u, 99u, false, true ) );

    scan.Reset();
    CHECK_FALSE( scan.Matches( 3u, 7u, 2u, target, 120u, 99u, false, false ) );
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
        full.ObserveBody( i, samples[i], moving[i] );
    }

    for ( std::size_t i = 0; i < 3u; ++i )
    {
        incremental.ObserveBody( i, samples[i], moving[i] );
    }

    for ( std::size_t i = 3u; i < samples.size(); ++i )
    {
        incremental.ObserveBody( i, samples[i], moving[i] );
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

TEST_CASE( "Replay child marker retains the predicted collision pose" )
{
    constexpr ReplayFrameIndex collisionFrame = 2u;
    const SkullbonezCore::Physics::PhysicsSceneObjectId rootId { 1u };
    const SkullbonezCore::Physics::PhysicsSceneObjectId childId { 2u };
    auto prediction = std::make_unique<RunReplayPredictionState>();
    std::vector<RunReplayPredictionFrame> frames( 5u );

    for ( std::size_t frameIndex = 0u; frameIndex < frames.size(); ++frameIndex )
    {
        RunReplayPredictionFrame& frame = frames[frameIndex];
        frame.frameIndex = static_cast<ReplayFrameIndex>( frameIndex );
        frame.bodies.resize( 1u );
        RunReplayPredictionBodySample& child = frame.bodies[0];
        child.id = childId;
        child.modelRow.value = 1;
        child.position.x = static_cast<float>( 100u + frameIndex );
        child.linearVelocity.x = frameIndex > collisionFrame ? 20.0f : 0.0f;
    }

    RunReplayPathTraceNode node;
    node.id = childId;
    node.parentId = rootId;
    node.modelRow.value = 1;
    node.parentModelRow.value = 0;
    node.firstFrame = collisionFrame;
    node.depth = 1;
    node.contactDerived = true;
    prediction->futureNodeCache.futureNodes.push_back( node );
    prediction->futureNodeCache.futureNodesTopologyVersion = 1u;

    ReplayPredictionChildMarkerScanState scan;
    REQUIRE( AdvanceReplayPredictionChildMarkerScan( scan, *prediction, frames, frames.size(), frames.back().frameIndex, 1u,
                                                     rootId, false, true, std::chrono::steady_clock::now(), 1000.0 ) );
    REQUIRE( scan.nodeCount == 1u );
    CHECK( scan.nodes[0].hasEntryPose );
    CHECK( scan.nodes[0].active );
    CHECK( scan.nodes[0].entryPosition.x == frames[collisionFrame].bodies[0].position.x );

    RetainReplayPredictionCausalMarkers( *prediction, scan, frames.back().frameIndex, &frames, frames.size() );
    REQUIRE( prediction->futureNodeCache.retainedMarkerCount == 1u );
    const ReplayPredictionRetainedMarker& marker = prediction->futureNodeCache.retainedMarkers[0];
    CHECK( marker.hasEntryPose );
    CHECK( marker.entryPosition.x == frames[collisionFrame].bodies[0].position.x );
}

void InitializeReplayChildMarkerNode( RunReplayPredictionState& prediction, std::size_t nodeIndex,
                                      SkullbonezCore::Physics::PhysicsSceneObjectId rootId )
{
    RunReplayPathTraceNode& node = prediction.futureNodeCache.futureNodes[nodeIndex];
    node.id.value = static_cast<uint32_t>( nodeIndex + 2u );
    node.parentId = rootId;
    node.modelRow.value = static_cast<int>( nodeIndex + 1u );
    node.parentModelRow.value = 0;
    node.firstFrame = static_cast<ReplayFrameIndex>( nodeIndex % 29u );
    node.depth = 1 + static_cast<int>( nodeIndex % 3u );
    node.contactDerived = nodeIndex % 2u == 0u;
}

ReplayPredictionChildMarkerScanState BuildLegacyReplayChildMarkerScan( const RunReplayPredictionState& prediction,
                                                                       std::span<const RunReplayPredictionFrame> frames,
                                                                       std::size_t frameCount, ReplayFrameIndex revealFrame,
                                                                       uint32_t generation,
                                                                       SkullbonezCore::Physics::PhysicsSceneObjectId rootId,
                                                                       bool usingBuildFrames )
{
    ReplayPredictionChildMarkerScanState full;
    frameCount = (std::min)( frameCount, frames.size() );
    full.nodeCount = (std::min)( prediction.futureNodeCache.futureNodes.size(),
                                 static_cast<std::size_t>( REPLAY_VISUAL_FUTURE_NODE_CAPACITY ) );

    for ( std::size_t nodeIndex = 0u; nodeIndex < full.nodeCount; ++nodeIndex )
    {
        full.nodes[nodeIndex].node = prediction.futureNodeCache.futureNodes[nodeIndex];
    }

    std::size_t visibleFrameCount = 0u;

    for ( std::size_t frameIndex = 0u; frameIndex < frameCount; ++frameIndex )
    {
        const RunReplayPredictionFrame& frame = frames[frameIndex];

        if ( frame.frameIndex > revealFrame )
        {
            break;
        }

        ++visibleFrameCount;

        for ( std::size_t nodeIndex = 0u; nodeIndex < full.nodeCount; ++nodeIndex )
        {
            ReplayPredictionChildMarkerNodeScanState& drawState = full.nodes[nodeIndex];

            if ( frame.frameIndex < drawState.node.firstFrame )
            {
                continue;
            }

            const RunReplayPredictionBodySample*
                body = FindReplayPredictionBodyByIdWithHint( frame, drawState.node.id, drawState.node.modelRow.value );

            if ( !body )
            {
                continue;
            }

            if ( !drawState.hasEntryPose )
            {
                drawState.hasEntryPose = true;
                drawState.entryModelIndex = body->modelRow.value;
                drawState.entryPosition = body->position;
                drawState.entryOrientation = body->orientation;
                drawState.entryOrientation.Normalise();
            }

            if ( ReplayPredictionBodyHasVisibleLinearMotion( *body ) )
            {
                drawState.active = true;
                drawState.lastMotionFrame = frame.frameIndex;
            }
        }
    }

    for ( std::size_t nodeIndex = 0u; nodeIndex < full.nodeCount; ++nodeIndex )
    {
        full.nodes[nodeIndex].scannedFrameCount = visibleFrameCount;
    }

    full.Commit( generation, prediction.futureNodeCache.futureNodesTopologyVersion, rootId, frameCount, revealFrame,
                 usingBuildFrames, frameCount == frames.size() );
    return full;
}

void CheckReplayMarkerOrientation( const SkullbonezCore::Math::Orientation::Quaternion& actual,
                                   const SkullbonezCore::Math::Orientation::Quaternion& expected )
{
    float actualX = 0.0f;
    float actualY = 0.0f;
    float actualZ = 0.0f;
    float actualW = 0.0f;
    float expectedX = 0.0f;
    float expectedY = 0.0f;
    float expectedZ = 0.0f;
    float expectedW = 0.0f;
    actual.GetComponents( actualX, actualY, actualZ, actualW );
    expected.GetComponents( expectedX, expectedY, expectedZ, expectedW );
    CHECK( actualX == expectedX );
    CHECK( actualY == expectedY );
    CHECK( actualZ == expectedZ );
    CHECK( actualW == expectedW );
}

void CompareReplayChildMarkerScanWithLegacy( ReplayPredictionChildMarkerScanState& incremental,
                                             RunReplayPredictionState& prediction,
                                             const std::vector<RunReplayPredictionFrame>& frames,
                                             SkullbonezCore::Physics::PhysicsSceneObjectId rootId, std::size_t frameCount,
                                             ReplayFrameIndex revealFrame, uint32_t generation, bool usingBuildFrames,
                                             bool& observedPartialMarkerScan )
{
    constexpr std::array<std::size_t, 4u> expirySchedule { 3u, 1u, 4u, 2u };
    const bool bufferComplete = (std::min)( frameCount, frames.size() ) == frames.size();
    bool scanComplete = false;
    std::size_t passCount = 0u;

    // Invariant: every pass gets a fresh deterministic budget while the
    // production per-node cursors retain the interrupted scan prefix.
    for ( ; passCount < 2048u && !scanComplete; ++passCount )
    {
        ReplayPredictionSchedulingOperations::budgetExpiryCheckCount = 0u;
        ReplayPredictionSchedulingOperations::forcedBudgetExpiryCheck = expirySchedule[passCount % expirySchedule.size()];
        const auto budgetStart = std::chrono::steady_clock::now();
        scanComplete = AdvanceReplayPredictionChildMarkerScan( incremental, prediction, frames, frameCount, revealFrame,
                                                               generation, rootId, usingBuildFrames, bufferComplete,
                                                               budgetStart, 1.0 );

        if ( !scanComplete )
        {
            const std::size_t visibleFrameCount = (std::min)( frameCount, static_cast<std::size_t>( revealFrame ) + 1u );
            const bool anyProgress = std::any_of( incremental.nodes.begin(),
                                                  incremental.nodes.begin() + incremental.nodeCount,
                                                  []( const ReplayPredictionChildMarkerNodeScanState& node )
                                                  { return node.scannedFrameCount > 0u; } );
            const bool anyIncomplete = std::any_of( incremental.nodes.begin(),
                                                    incremental.nodes.begin() + incremental.nodeCount,
                                                    [visibleFrameCount](
                                                        const ReplayPredictionChildMarkerNodeScanState& node )
                                                    { return node.scannedFrameCount < visibleFrameCount; } );
            observedPartialMarkerScan = observedPartialMarkerScan || ( anyProgress && anyIncomplete );
        }
    }

    ReplayPredictionSchedulingOperations::forcedBudgetExpiryCheck.reset();
    REQUIRE( scanComplete );
    const ReplayPredictionChildMarkerScanState full = BuildLegacyReplayChildMarkerScan( prediction, frames, frameCount,
                                                                                        revealFrame, generation, rootId,
                                                                                        usingBuildFrames );
    REQUIRE( incremental.nodeCount == full.nodeCount );
    CHECK( incremental.Matches( generation, prediction.futureNodeCache.futureNodesTopologyVersion, full.nodeCount, rootId,
                                (std::min)( frameCount, frames.size() ), revealFrame, usingBuildFrames, bufferComplete ) );

    for ( std::size_t nodeIndex = 0u; nodeIndex < full.nodeCount; ++nodeIndex )
    {
        const ReplayPredictionChildMarkerNodeScanState& actual = incremental.nodes[nodeIndex];
        const ReplayPredictionChildMarkerNodeScanState& expected = full.nodes[nodeIndex];
        CHECK( actual.node.id.value == expected.node.id.value );
        CHECK( actual.node.modelRow.value == expected.node.modelRow.value );
        CHECK( actual.node.firstFrame == expected.node.firstFrame );
        CHECK( actual.scannedFrameCount == expected.scannedFrameCount );
        CHECK( actual.active == expected.active );
        CHECK( actual.hasEntryPose == expected.hasEntryPose );
        CHECK( actual.entryModelIndex == expected.entryModelIndex );
        CHECK( actual.entryPosition == expected.entryPosition );
        CHECK( actual.lastMotionFrame == expected.lastMotionFrame );
        CheckReplayMarkerOrientation( actual.entryOrientation, expected.entryOrientation );
    }

    auto actualMarkers = std::make_unique<RunReplayPredictionState>();
    auto expectedMarkers = std::make_unique<RunReplayPredictionState>();
    const std::vector<RunReplayPredictionFrame>* completeFrames = bufferComplete ? &frames : nullptr;
    const std::size_t completeFrameCount = bufferComplete ? frames.size() : 0u;
    RetainReplayPredictionCausalMarkers( *actualMarkers, incremental, revealFrame, completeFrames, completeFrameCount );
    RetainReplayPredictionCausalMarkers( *expectedMarkers, full, revealFrame, completeFrames, completeFrameCount );
    REQUIRE( actualMarkers->futureNodeCache.retainedMarkerCount == expectedMarkers->futureNodeCache.retainedMarkerCount );

    for ( std::size_t markerIndex = 0u; markerIndex < actualMarkers->futureNodeCache.retainedMarkerCount; ++markerIndex )
    {
        const ReplayPredictionRetainedMarker& actual = actualMarkers->futureNodeCache.retainedMarkers[markerIndex];
        const ReplayPredictionRetainedMarker& expected = expectedMarkers->futureNodeCache.retainedMarkers[markerIndex];
        CHECK( actual.id.value == expected.id.value );
        CHECK( actual.modelRow.value == expected.modelRow.value );
        CHECK( actual.hasEntryPose == expected.hasEntryPose );
        CHECK( actual.hasRestPose == expected.hasRestPose );
        CHECK( actual.hasHorizonPose == expected.hasHorizonPose );
        CHECK( actual.entryPosition == expected.entryPosition );
        CheckReplayMarkerOrientation( actual.entryOrientation, expected.entryOrientation );
        CHECK( actual.restPosition == expected.restPosition );
        CheckReplayMarkerOrientation( actual.restOrientation, expected.restOrientation );
        CHECK( actual.horizonPosition == expected.horizonPosition );
        CheckReplayMarkerOrientation( actual.horizonOrientation, expected.horizonOrientation );
    }
}

TEST_CASE( "Replay incremental child marker scan matches the legacy full scan across publication transitions" )
{
    constexpr std::size_t completedFrameCount = 361u;
    constexpr std::size_t childCount = REPLAY_VISUAL_FUTURE_NODE_CAPACITY;
    const SkullbonezCore::Physics::PhysicsSceneObjectId rootId { 1u };
    auto prediction = std::make_unique<RunReplayPredictionState>();
    std::vector<RunReplayPredictionFrame> frames( completedFrameCount );

    for ( std::size_t frameIndex = 0u; frameIndex < frames.size(); ++frameIndex )
    {
        RunReplayPredictionFrame& frame = frames[frameIndex];
        frame.frameIndex = static_cast<ReplayFrameIndex>( frameIndex );
        frame.bodies.resize( childCount + 1u );

        for ( std::size_t bodyIndex = 0u; bodyIndex < frame.bodies.size(); ++bodyIndex )
        {
            RunReplayPredictionBodySample& body = frame.bodies[bodyIndex];
            body.id.value = static_cast<uint32_t>( bodyIndex + 1u );
            body.modelRow.value = static_cast<int>( bodyIndex );
            body.position.x = static_cast<float>( bodyIndex * 10u + frameIndex );
            body.position.y = static_cast<float>( bodyIndex % 17u );
            const bool visibleMotion = bodyIndex > 0u && frameIndex >= bodyIndex % 29u &&
                                       ( frameIndex + bodyIndex * 3u ) % 53u < 2u;
            body.linearVelocity.x = visibleMotion ? 20.0f : 0.0f;
        }
    }

    prediction->futureNodeCache.futureNodes.resize( 32u );

    for ( std::size_t nodeIndex = 0u; nodeIndex < prediction->futureNodeCache.futureNodes.size(); ++nodeIndex )
    {
        InitializeReplayChildMarkerNode( *prediction, nodeIndex, rootId );
    }

    prediction->futureNodeCache.futureNodesTopologyVersion = 1u;
    ReplayPredictionChildMarkerScanState incremental;

    bool observedPartialMarkerScan = false;
    const auto compareWithLegacy =
        [&]( std::size_t frameCount, ReplayFrameIndex revealFrame, uint32_t generation, bool usingBuildFrames )
    {
        CompareReplayChildMarkerScanWithLegacy( incremental, *prediction, frames, rootId, frameCount, revealFrame,
                                                generation, usingBuildFrames, observedPartialMarkerScan );
    };
    compareWithLegacy( 64u, 30u, 1u, false );
    CHECK( incremental.nodes[0].scannedFrameCount == 31u );
    compareWithLegacy( 128u, 100u, 1u, false );
    CHECK( incremental.nodes[0].scannedFrameCount == 101u );

    const std::size_t previousNodeCount = prediction->futureNodeCache.futureNodes.size();
    prediction->futureNodeCache.futureNodes.resize( childCount );

    for ( std::size_t nodeIndex = previousNodeCount; nodeIndex < childCount; ++nodeIndex )
    {
        InitializeReplayChildMarkerNode( *prediction, nodeIndex, rootId );
    }

    prediction->futureNodeCache.futureNodesTopologyVersion = 2u;
    compareWithLegacy( 240u, 180u, 1u, false );
    CHECK( observedPartialMarkerScan );
    CHECK( incremental.nodeCount == childCount );
    CHECK( incremental.nodes[0].scannedFrameCount == 181u );
    CHECK( incremental.nodes[childCount - 1u].scannedFrameCount == 181u );

    const std::size_t stableCursor = incremental.nodes[0].scannedFrameCount;
    prediction->futureNodeCache.futureNodes[5].firstFrame += 17u;
    prediction->futureNodeCache.futureNodesTopologyVersion = 3u;
    compareWithLegacy( 240u, 180u, 1u, false );
    CHECK( incremental.nodes[0].scannedFrameCount == stableCursor );
    CHECK( incremental.nodes[5].scannedFrameCount == 181u );

    compareWithLegacy( completedFrameCount, static_cast<ReplayFrameIndex>( completedFrameCount - 1u ), 1u, false );
    CHECK( incremental.nodeCount == childCount );

    for ( RunReplayPredictionFrame& frame : frames )
    {
        frame.bodies[1].linearVelocity.x = 0.0f;
        frame.bodies[1].position.x += 5000.0f;
    }

    prediction->futureNodeCache.futureNodesTopologyVersion = 4u;
    compareWithLegacy( completedFrameCount, static_cast<ReplayFrameIndex>( completedFrameCount - 1u ), 2u, false );
    CHECK_FALSE( incremental.nodes[0].active );
    CHECK( incremental.nodes[0].hasEntryPose );
    compareWithLegacy( completedFrameCount, 50u, 2u, false );
    CHECK( incremental.nodes[0].scannedFrameCount == 51u );

    // Hazard: changing only the bank token would let a stale completed cursor
    // falsely pass. This second bank gives the first child a new entry/rest
    // story inside the same reveal window, so replacement must restart work.
    for ( std::size_t frameIndex = 0u; frameIndex < frames.size(); ++frameIndex )
    {
        RunReplayPredictionBodySample& child = frames[frameIndex].bodies[1];
        child.position.x = -1000.0f + static_cast<float>( frameIndex );
        child.linearVelocity.x = frameIndex >= 5u && frameIndex <= 7u ? 20.0f : 0.0f;
    }

    compareWithLegacy( completedFrameCount, 50u, 2u, true );
    CHECK( incremental.usingBuildFrames );
    CHECK( incremental.nodes[0].active );
    CHECK( incremental.nodes[0].entryPosition.x == -1000.0f );
}

TEST_CASE( "Replay prediction draw cursor resumes at its suffix and reuses stable tokens" )
{
    CHECK( ReplayOverlay::IsReplayPredictionDrawListPublicationStable( false, 19u, 2400, 19u, 2400 ) );
    CHECK_FALSE( ReplayOverlay::IsReplayPredictionDrawListPublicationStable( true, 19u, 2400, 19u, 2400 ) );
    CHECK_FALSE( ReplayOverlay::IsReplayPredictionDrawListPublicationStable( false, 18u, 2400, 19u, 2400 ) );
    CHECK_FALSE( ReplayOverlay::IsReplayPredictionDrawListPublicationStable( false, 19u, 2399, 19u, 2400 ) );
    CHECK_FALSE( ReplayOverlay::IsReplayPredictionDrawListPublicationStable( false, 19u, 2400, 19u, 2400, 7u, 8u ) );

    CHECK( ReplayOverlay::ReplayPredictionFirstUnconsumedPoint( 0u ) == 1u );
    CHECK( ReplayOverlay::ReplayPredictionFirstUnconsumedPoint( 1u ) == 1u );
    CHECK( ReplayOverlay::ReplayPredictionFirstUnconsumedPoint( 128u ) == 128u );

    CHECK_FALSE( ReplayOverlay::ReplayPredictionCanSkipSaturatedDrawList( true, 7u, 8u, 12u, 12u ) );
    CHECK_FALSE( ReplayOverlay::ReplayPredictionCanSkipSaturatedDrawList( true, 8u, 8u, 11u, 12u ) );
    CHECK( ReplayOverlay::ReplayPredictionCanSkipSaturatedDrawList( true, 8u, 8u, 12u, 12u ) );
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
    const uint64_t expectedHash = CombineReplayVisualSubmissionHashes( retained.submission.ordinaryLineHash,
                                                                       retained.submission.ordinaryLineBytes, frameHash,
                                                                       frameBytes );
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
    constexpr std::size_t floatsPerRecord = SkullbonezCore::Runtime::ReplayOverlay::PREDICTION_TRAJECTORY_FLOATS_PER_RECORD;
    const auto record = []( float ax, float bx, float r, float g, float b )
    {
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
    const std::array<float, floatsPerRecord> rangeBSnapshot = [&]
    {
        std::array<float, floatsPerRecord> result = {};
        std::copy_n( arena.begin() + static_cast<std::ptrdiff_t>( 2u * floatsPerRecord ), floatsPerRecord, result.begin() );
        return result;
    }();
    REQUIRE( AppendPredictionRetainedRecord( arena, rangeA, record( 1.0f, 2.0f, 0.7f, 0.3f, 0.1f ), 0.0001f ) );

    CHECK( rangeA.recordCount == 2u );
    CHECK( rangeB.recordCount == 1u );
    CHECK( std::equal( rangeBSnapshot.begin(), rangeBSnapshot.end(),
                       arena.begin() + static_cast<std::ptrdiff_t>( 2u * floatsPerRecord ) ) );

    auto expectedA0 = record( 0.0f, 1.0f, 0.8f, 0.2f, 0.1f ).Packed();
    expectedA0[16] = 2.0f;
    auto expectedA1 = record( 1.0f, 2.0f, 0.7f, 0.3f, 0.1f ).Packed();
    expectedA1[13] = 0.0f;
    const auto expectedB = record( 10.0f, 11.0f, 0.1f, 0.4f, 0.9f ).Packed();
    CHECK( std::equal( expectedA0.begin(), expectedA0.end(), arena.begin() ) );
    CHECK(
        std::equal( expectedA1.begin(), expectedA1.end(), arena.begin() + static_cast<std::ptrdiff_t>( floatsPerRecord ) ) );
    CHECK( std::equal( expectedB.begin(), expectedB.end(),
                       arena.begin() + static_cast<std::ptrdiff_t>( 2u * floatsPerRecord ) ) );
}

TEST_CASE( "Replay retained continuation chunks repair only their shared adjacency tail" )
{
    using SkullbonezCore::Rendering::RetainedGeometryRangeToken;
    using SkullbonezCore::Runtime::ReplayOverlay::AppendPredictionRetainedContinuation;
    using SkullbonezCore::Runtime::ReplayOverlay::AppendPredictionRetainedRecord;
    using SkullbonezCore::Runtime::ReplayOverlay::ReplayPredictionRetainedRecord;
    constexpr std::size_t floatsPerRecord = SkullbonezCore::Runtime::ReplayOverlay::PREDICTION_TRAJECTORY_FLOATS_PER_RECORD;
    const auto record = []( float ax, float bx )
    {
        const SkullbonezCore::Math::Vector::Vector3 start( ax, 0.0f, 0.0f );
        const SkullbonezCore::Math::Vector::Vector3 end( bx, 0.0f, 0.0f );
        return ReplayPredictionRetainedRecord { start, end, 2.0f, 0.8f, 0.2f, 0.1f, 1.0f, 1.0f, 0.0f, start, end };
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
    const std::array<float, floatsPerRecord> siblingSnapshot = [&]
    {
        std::array<float, floatsPerRecord> result = {};
        std::copy_n( arena.begin() + static_cast<std::ptrdiff_t>( floatsPerRecord ), floatsPerRecord, result.begin() );
        return result;
    }();

    REQUIRE( AppendPredictionRetainedContinuation( arena, first, continuation, record( 1.0f, 2.0f ), 0.0001f ) );
    CHECK( first.sourceVersion == 5u );
    CHECK( arena[16] == 2.0f );
    CHECK( arena[2u * floatsPerRecord + 13u] == 0.0f );
    CHECK( std::equal( siblingSnapshot.begin(), siblingSnapshot.end(),
                       arena.begin() + static_cast<std::ptrdiff_t>( floatsPerRecord ) ) );
}

TEST_CASE( "Replay causal focus fades only unrelated retained path identities" )
{
    using SkullbonezCore::Runtime::ReplayOverlay::REPLAY_INSPECTION_CONTEXT_PATH_OPACITY;
    using SkullbonezCore::Runtime::ReplayOverlay::ReplayPredictionPathFocus;

    const ReplayPredictionPathFocus focus { 10u, 11u };
    CHECK( focus.Active() );
    CHECK( focus.Contains( 10u ) );
    CHECK( focus.Contains( 11u ) );
    CHECK_FALSE( focus.Contains( 12u ) );
    CHECK( focus.OpacityFor( 10u ) == doctest::Approx( 1.0f ) );
    CHECK( focus.OpacityFor( 11u ) == doctest::Approx( 1.0f ) );
    CHECK( focus.OpacityFor( 12u ) == doctest::Approx( REPLAY_INSPECTION_CONTEXT_PATH_OPACITY ) );

    const ReplayPredictionPathFocus inactive;
    CHECK_FALSE( inactive.Active() );
    CHECK( inactive.OpacityFor( 12u ) == doctest::Approx( 1.0f ) );
}

TEST_CASE( "Replay space prediction draws every body path instead of causal-only paths" )
{
    ReplayTrajectoryRecordKey selectedRoot;
    selectedRoot.bodyId = SkullbonezCore::Physics::PhysicsSceneObjectId { 10u };
    selectedRoot.lane = ReplayTrajectoryLane::FutureRoot;
    selectedRoot.branchOrdinal = 0u;

    ReplayTrajectoryRecordKey planetPath = selectedRoot;
    planetPath.bodyId = SkullbonezCore::Physics::PhysicsSceneObjectId { 20u };
    ReplayTrajectoryRecordKey inactivePlanetPath = planetPath;
    inactivePlanetPath.branchOrdinal = 1u;
    ReplayTrajectoryRecordKey causalChild = planetPath;
    causalChild.lane = ReplayTrajectoryLane::FutureChildOutgoing;
    causalChild.branchOrdinal = 3u;

    CHECK( ReplayOverlay::ReplayPredictionDrawsAllBodyRecord( ReplayPredictionPathPresentation::AllBodiesSpace, planetPath,
                                                              0u, selectedRoot.bodyId ) );
    CHECK_FALSE( ReplayOverlay::ReplayPredictionDrawsAllBodyRecord( ReplayPredictionPathPresentation::AllBodiesSpace,
                                                                    inactivePlanetPath, 0u, selectedRoot.bodyId ) );
    CHECK_FALSE( ReplayOverlay::ReplayPredictionDrawsAllBodyRecord( ReplayPredictionPathPresentation::AllBodiesSpace,
                                                                    selectedRoot, 0u, selectedRoot.bodyId ) );
    CHECK_FALSE( ReplayOverlay::ReplayPredictionDrawsCausalChildRecord( ReplayPredictionPathPresentation::AllBodiesSpace,
                                                                        causalChild, 0u, 200u ) );
    CHECK( ReplayOverlay::ReplayPredictionDrawsCausalChildRecord( ReplayPredictionPathPresentation::SelectedCausalTree,
                                                                  causalChild, 0u, 200u ) );
    CHECK( ReplayOverlay::ReplayPredictionUsesAuthoredBodyColor( ReplayPredictionPathPresentation::AllBodiesSpace,
                                                                 ReplayTrajectoryLane::FutureRoot ) );
    CHECK_FALSE( ReplayOverlay::ReplayPredictionUsesAuthoredBodyColor( ReplayPredictionPathPresentation::SelectedCausalTree,
                                                                       ReplayTrajectoryLane::FutureRoot ) );
}

TEST_CASE( "Replay prediction topology oracle rejects all-body roots without explicit space presentation" )
{
    const SkullbonezCore::Physics::PhysicsSceneObjectId selectedId { 10u };
    const SkullbonezCore::Physics::PhysicsSceneObjectId childId { 20u };
    const SkullbonezCore::Physics::PhysicsSceneObjectId disconnectedId { 30u };

    std::array<ReplayTrajectoryRecord, 3> causalRecords;
    causalRecords[0].key = { selectedId, ReplayTrajectoryLane::FutureRoot, 0u };
    causalRecords[1].key = { childId, ReplayTrajectoryLane::FutureChildIncoming, 0u };
    causalRecords[1].parentId = selectedId;
    causalRecords[1].depth = 1;
    causalRecords[1].firstFrame = 40u;
    causalRecords[2] = causalRecords[1];
    causalRecords[2].key.lane = ReplayTrajectoryLane::FutureChildOutgoing;

    RunReplayPathTraceNode child;
    child.id = childId;
    child.parentId = selectedId;
    child.depth = 1;
    child.firstFrame = 40u;
    child.contactDerived = true;
    const std::array causalNodes = { child };

    REQUIRE( PredictionTopologyMatchesPresentation( causalRecords, causalNodes, selectedId,
                                                    ReplayPredictionPathPresentation::SelectedCausalTree ) );

    std::array<ReplayTrajectoryRecord, 4> leakedAllBodyRecords;
    std::copy( causalRecords.begin(), causalRecords.end(), leakedAllBodyRecords.begin() );
    leakedAllBodyRecords[3].key = { disconnectedId, ReplayTrajectoryLane::FutureRoot, 0u };

    SkullbonezCore::Physics::PhysicsWorldForces genericForces;
    genericForces.mutualGravity.enabled = true;
    REQUIRE( genericForces.mutualGravity.enabled );
    CHECK_FALSE( PredictionTopologyMatchesPresentation( leakedAllBodyRecords, causalNodes, selectedId,
                                                        ReplayPredictionPathPresentation::SelectedCausalTree ) );

    std::array<ReplayTrajectoryRecord, 4> orphanChildRecords;
    std::copy( causalRecords.begin(), causalRecords.end(), orphanChildRecords.begin() );
    orphanChildRecords[3] = causalRecords[1];
    orphanChildRecords[3].key.bodyId = disconnectedId;
    CHECK_FALSE( PredictionTopologyMatchesPresentation( orphanChildRecords, causalNodes, selectedId,
                                                        ReplayPredictionPathPresentation::SelectedCausalTree ) );

    RunReplayPathTraceNode motionOnlyChild = child;
    motionOnlyChild.contactDerived = false;
    const std::array motionOnlyNodes = { motionOnlyChild };
    CHECK_FALSE( PredictionTopologyMatchesPresentation( causalRecords, motionOnlyNodes, selectedId,
                                                        ReplayPredictionPathPresentation::SelectedCausalTree ) );

    RunReplayPathTraceNode cycleBackToRoot = child;
    cycleBackToRoot.id = selectedId;
    cycleBackToRoot.parentId = childId;
    cycleBackToRoot.depth = 2;
    const std::array cyclicNodes = { child, cycleBackToRoot };
    CHECK_FALSE( PredictionTopologyMatchesPresentation( causalRecords, cyclicNodes, selectedId,
                                                        ReplayPredictionPathPresentation::SelectedCausalTree ) );

    std::array<ReplayTrajectoryRecord, 3> authoredSpaceRecords;
    authoredSpaceRecords[0].key = { selectedId, ReplayTrajectoryLane::FutureRoot, 0u };
    authoredSpaceRecords[1].key = { childId, ReplayTrajectoryLane::FutureRoot, 0u };
    authoredSpaceRecords[2].key = { disconnectedId, ReplayTrajectoryLane::FutureRoot, 0u };
    CHECK( PredictionTopologyMatchesPresentation( authoredSpaceRecords, {}, selectedId,
                                                  ReplayPredictionPathPresentation::AllBodiesSpace ) );
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
    const uint64_t expected = BuildCanonicalReplayVisualArchiveSemanticHash( visualStateHash, exactPacketHash,
                                                                             topologyVersion, reserveGrowthEvents );

    CHECK( expected == 0x5F1B931D0EE4051Cull );
    CHECK( expected == BuildCanonicalReplayVisualArchiveSemanticHash( visualStateHash, exactPacketHash, topologyVersion,
                                                                      reserveGrowthEvents ) );
    CHECK( expected != BuildCanonicalReplayVisualArchiveSemanticHash( visualStateHash ^ 1u, exactPacketHash, topologyVersion,
                                                                      reserveGrowthEvents ) );
    CHECK( expected != BuildCanonicalReplayVisualArchiveSemanticHash( visualStateHash, exactPacketHash ^ 1u, topologyVersion,
                                                                      reserveGrowthEvents ) );
    CHECK( expected != BuildCanonicalReplayVisualArchiveSemanticHash( visualStateHash, exactPacketHash, topologyVersion + 1u,
                                                                      reserveGrowthEvents ) );
}

TEST_CASE( "Replay visual topology versions canonicalize by first publication" )
{
    ReplayVisualTopologyVersionCanonicalizer versions;

    CHECK( versions.Observe( 0u ) == 0u );
    CHECK( versions.Observe( 12u ) == 1u );
    CHECK( versions.Observe( 12u ) == 1u );
    CHECK( versions.Observe( 4u ) == 2u );
    CHECK( versions.Observe( 0u ) == 0u );
    CHECK( versions.Observe( 9u ) == 3u );
    CHECK( versions.Observe( 4u ) == 2u );
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
    const uint64_t emptyStreamHash = HashReplayVisualFloatBuffer( std::span<const float> {} );
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
    CHECK( std::string_view( FindReplayVisualPacketSubmissionSpanMismatch( miswired ) ) == "submission.ordinaryLineHash" );
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
    const ReplayVisualPacketFingerprint
        initial = BuildReplayVisualPacketFingerprint( packet, reusableDigests,
                                                      ReplayVisualTrajectoryDigestPolicy::ReuseImmutableRecords );
    const ReplayVisualPacketFingerprint
        cached = BuildReplayVisualPacketFingerprint( packet, reusableDigests,
                                                     ReplayVisualTrajectoryDigestPolicy::ReuseImmutableRecords );
    CHECK( cached.trajectoryStateHash == initial.trajectoryStateHash );

    record.points[1].position.x = 2.0f;
    ++record.version;
    const ReplayVisualPacketFingerprint
        replaced = BuildReplayVisualPacketFingerprint( packet, reusableDigests,
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
    expectedRecord.points.push_back( ReplayTrajectoryPoint { 17u, { 1.0f, 2.0f, 3.0f } } );
    actualRecord.points.push_back( ReplayTrajectoryPoint { 17u, { 1.0f, 2.000001f, 3.0f } } );
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
    const ReplayVisualPacketFingerprint inactiveMutationHash = BuildReplayVisualPacketFingerprint( actual, actualDigests );
    CHECK( expectedHash.visualStateHash == inactiveMutationHash.visualStateHash );
    CHECK( expectedHash.exactHash == inactiveMutationHash.exactHash );
    CHECK( expectedHash.semanticHash != inactiveMutationHash.semanticHash );

    actualRecords[0].points[0].position.y = 2.000001f;
    const ReplayVisualPacketFingerprint activeMutationHash = BuildReplayVisualPacketFingerprint( actual, actualDigests );
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

namespace
{
uint64_t ReadArchiveU64( const std::vector<uint8_t>& bytes, std::size_t offset )
{
    uint64_t result = 0;
    for ( std::size_t byte = 0; byte < 8u; ++byte )
    {
        result |= static_cast<uint64_t>( bytes[offset + byte] ) << ( byte * 8u );
    }
    return result;
}

void WriteArchiveU64( std::vector<uint8_t>& bytes, std::size_t offset, uint64_t value )
{
    for ( std::size_t byte = 0; byte < 8u; ++byte )
    {
        bytes[offset + byte] = static_cast<uint8_t>( value >> ( byte * 8u ) );
    }
}
} // namespace

TEST_CASE( "Replay prediction archive preserves bounded high-detail coverage and later causal nodes" )
{
    auto state = std::make_unique<RunReplayPredictionState>();
    state->build.complete = true;
    state->build.generationBeginCount = 3u;
    state->simulation.frames.resize( 5u );
    state->simulation.committedFrameCount = 5u;
    for ( std::size_t i = 0; i < 5u; ++i )
    {
        state->simulation.frames[i].frameIndex = i;
        state->simulation.frames[i].bodies.resize( 1u );
    }
    for ( const ReplayFrameIndex frame : { 1u, 4u } )
    {
        RunReplayPathTraceNode node;
        node.id.value = static_cast<uint32_t>( frame + 10u );
        node.contactDerived = true;
        node.firstFrame = frame;
        state->futureNodeCache.futureNodes.push_back( node );
    }
    state->futureNodeCache.futureNodesCacheValid = true;
    state->futureNodeCache.futureNodesTopologyVersion = 1u;
    state->trajectoryBuild.valid = true;
    state->trajectoryBuild.topologyVersion = 1u;
    state->trajectoryBuild.builtNodeCount = 2u;

    auto evidence = std::make_unique<ReplayPredictionSolverEvidenceBanks>();
    evidence->BeginBuild( 3u, ReplayPredictionDetailMode::High );
    REQUIRE( evidence->ReserveBuild( 5u, 0u, 0u, 0 ) );
    REQUIRE( evidence->AppendBuildFrame( 0u, 1u, 1u, {}, {}, 0 ) );
    bool fullCoverage = false;
    SUBCASE( "partial prefix remains high detail" )
    {
    }
    SUBCASE( "historical full-coverage section remains readable" )
    {
        fullCoverage = true;
    }
    SUBCASE( "missing event inside coverage is rejected" )
    {
        REQUIRE( evidence->AppendBuildFrame( 2u, 1u, 1u, {}, {}, 2 ) );
        REQUIRE( evidence->PromoteBuild() );
        std::vector<uint8_t> invalidBytes;
        RunReplayPathVisualizerState path;
        CHECK_FALSE(
            ReplayPredictionArchiveOperations::BuildReplayPredictionArchive( path, *state, ReplayPredictionDetailMode::High,
                                                                             evidence->Committed(), invalidBytes ) );
        return;
    }
    for ( ReplayFrameIndex frame = 1u; frame < ( fullCoverage ? 5u : 3u ); ++frame )
    {
        REQUIRE( evidence->AppendBuildFrame( frame, 1u, 1u, {}, {}, static_cast<int>( frame ) ) );
    }
    REQUIRE( evidence->PromoteBuild() );
    RunReplayPathVisualizerState path;
    std::vector<uint8_t> bytes;
    REQUIRE( ReplayPredictionArchiveOperations::BuildReplayPredictionArchive( path, *state, ReplayPredictionDetailMode::High,
                                                                              evidence->Committed(), bytes ) );
    const std::size_t evidenceOffset = static_cast<std::size_t>( ReadArchiveU64( bytes, 56u ) );
    REQUIRE( bytes[4] == 7u );
    CHECK( ReadArchiveU64( bytes, evidenceOffset ) == ( fullCoverage ? 5u : 3u ) );
    const auto currentBytes = bytes;
    if ( fullCoverage )
    {
        // Independent v6 fixture: its evidence section begins with event count,
        // and has no coverage scalar. Descriptor and total sizes shrink by eight.
        bytes.erase( bytes.begin() + evidenceOffset, bytes.begin() + evidenceOffset + 8u );
        bytes[4] = 6u;
        WriteArchiveU64( bytes, 16u, bytes.size() );
        WriteArchiveU64( bytes, 64u, ReadArchiveU64( currentBytes, 64u ) - 8u );
    }
    auto restored = std::make_unique<RunReplayPredictionState>();
    auto restoredEvidence = std::make_unique<ReplayPredictionSolverEvidenceBanks>();
    ReplayPredictionArchiveDetailCapability capability = ReplayPredictionArchiveDetailCapability::Low;
    char reason[256] = {};
    INFO( reason );
    REQUIRE( ReplayPredictionArchiveOperations::LoadReplayPredictionArchive( bytes, path, *restored, *restoredEvidence,
                                                                             ReplayPredictionDetailMode::High, capability,
                                                                             reason, sizeof( reason ) ) );
    CHECK( capability == ReplayPredictionArchiveDetailCapability::High );
    CHECK( restored->archiveEvidenceEndFrame == ( fullCoverage ? 5u : 3u ) );
    REQUIRE( restored->futureNodeCache.futureNodes.size() == 2u );
    CHECK( restored->futureNodeCache.futureNodes[1].firstFrame == 4u );
    CHECK( restored->CommittedFrames().size() == 5u );
    CHECK( restoredEvidence->Committed().PublishedFrameCount() == ( fullCoverage ? 3u : 2u ) );
    std::vector<uint8_t> rebuilt;
    REQUIRE( ReplayPredictionArchiveOperations::BuildReplayPredictionArchive( path, *restored,
                                                                              ReplayPredictionDetailMode::High,
                                                                              restoredEvidence->Committed(), rebuilt ) );
    CHECK( rebuilt == currentBytes );

    // Rejection must preserve both sparse evidence and its coverage identity.
    for ( const uint64_t boundary : { 0u, 1u, 6u } )
    {
        auto invalid = currentBytes;
        WriteArchiveU64( invalid, evidenceOffset, boundary );
        CHECK_FALSE(
            ReplayPredictionArchiveOperations::LoadReplayPredictionArchive( invalid, path, *restored, *restoredEvidence,
                                                                            ReplayPredictionDetailMode::High, capability,
                                                                            reason, sizeof( reason ) ) );
        REQUIRE( ReplayPredictionArchiveOperations::BuildReplayPredictionArchive( path, *restored,
                                                                                  ReplayPredictionDetailMode::High,
                                                                                  restoredEvidence->Committed(), rebuilt ) );
        CHECK( rebuilt == currentBytes );
    }
    for ( const bool missingTimelineFrame : { false, true } )
    {
        state->futureNodeCache.futureNodes[1].firstFrame = missingTimelineFrame ? 4u : 999u;
        state->simulation.frames[4].frameIndex = missingTimelineFrame ? 5u : 4u;
        std::vector<uint8_t> malformedLightweight;
        REQUIRE(
            ReplayPredictionArchiveOperations::BuildReplayPredictionArchive( path, *state, ReplayPredictionDetailMode::Low,
                                                                             evidence->Committed(), malformedLightweight ) );
        auto invalid = currentBytes;
        REQUIRE( malformedLightweight.size() - 48u == evidenceOffset - 72u );
        std::copy( malformedLightweight.begin() + 48u, malformedLightweight.end(), invalid.begin() + 72u );
        CHECK_FALSE(
            ReplayPredictionArchiveOperations::LoadReplayPredictionArchive( invalid, path, *restored, *restoredEvidence,
                                                                            ReplayPredictionDetailMode::High, capability,
                                                                            reason, sizeof( reason ) ) );
        REQUIRE( ReplayPredictionArchiveOperations::BuildReplayPredictionArchive( path, *restored,
                                                                                  ReplayPredictionDetailMode::High,
                                                                                  restoredEvidence->Committed(), rebuilt ) );
        CHECK( rebuilt == currentBytes );
        CHECK_FALSE( ReplayPredictionArchiveOperations::BuildReplayPredictionArchive( path, *state,
                                                                                      ReplayPredictionDetailMode::High,
                                                                                      evidence->Committed(), rebuilt ) );
    }
}
