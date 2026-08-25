/*
Purpose:
  Locks replay prediction scheduling and publication protocols.

Invariants:
  - A zero instant budget always preserves amortized scheduling.
  - Throughput calibration follows changing worker cost without reacting fully
    to one noisy slice.
  - A large-body prediction remains amortized even when its cheap probe reports
    implausibly high throughput.
  - Repeated dirty events cannot cancel an amortized replacement until one
    coherent changed prefix has been presented.
  - Publication exposes only a contiguous bounded prefix and reset clears failure.
  - Continuous rows publish only when every body is complete; wrap retains one
    chronological interval and concurrent readers accept only stable versions.
  - A build-root trajectory exposes exactly the frame-thread presentation prefix.
  - Dormant trajectory reuse selects the smallest sufficient capacity without changing active order.
  - Solver lookup may preserve a negative sentinel; prediction-style lookup rejects it.
  - High detail is the default preference; changing modes restarts prediction,
    clears only prediction inspection, and requests capacity release only when
    entering Low detail.
  - Generic physics-force values cannot select all-body path presentation.
*/
#include "../ThirdPtySource/doctest/doctest.h"

#include "../SkullbonezSource/Runtime/Prediction/ContinuousPredictionSampleRing.h"
#include "../SkullbonezSource/Runtime/Prediction/ReplayPredictionScheduling.h"
#include "../SkullbonezSource/Runtime/Prediction/ReplayPredictionPublication.h"
#include "../SkullbonezSource/Runtime/Prediction/ReplayPredictionPublicationOperations.h"

#include <array>
#include <atomic>
#include <limits>
#include <thread>

using SkullbonezCore::Runtime::ContinuousPredictionSampleRing;
using SkullbonezCore::Runtime::ContinuousPredictionSampleRingSnapshot;
using SkullbonezCore::Runtime::EvaluateReplayPredictionDetailTransition;
using SkullbonezCore::Runtime::ReplayPredictionArchiveDetailCapability;
using SkullbonezCore::Runtime::ReplayPredictionBuildMode;
using SkullbonezCore::Runtime::ReplayPredictionCoalescerAction;
using SkullbonezCore::Runtime::ReplayPredictionDetailMode;
using SkullbonezCore::Runtime::ReplayPredictionDetailModeAfterArchiveLoad;
using SkullbonezCore::Runtime::ReplayPredictionDetailModeAfterGenerationReset;
using SkullbonezCore::Runtime::ReplayPredictionDetailTransitionAction;
using SkullbonezCore::Runtime::ReplayPredictionDetailTransitionHas;
using SkullbonezCore::Runtime::ReplayPredictionGenerationResetReason;
using SkullbonezCore::Runtime::ReplayPredictionPathPresentation;
using SkullbonezCore::Runtime::ReplayPredictionPathPresentationShowsAllBodies;
using SkullbonezCore::Runtime::ReplayPredictionPublication;
using SkullbonezCore::Runtime::ReplaySolverBodySample;
using SkullbonezCore::Runtime::ReplaySolverFrameSample;
using SkullbonezCore::Runtime::ReplayPredictionPublicationOperations::FindReplayBodyByIdInSample;
using SkullbonezCore::Runtime::ReplayPredictionPublicationOperations::FindReplayBodyByModelIndexInSample;
using SkullbonezCore::Runtime::ReplayPredictionPublicationOperations::ReplayPredictionBuildRootPrefixCount;
using SkullbonezCore::Runtime::ReplayPredictionPublicationOperations::SceneObjectIdForModelIndexInSample;
using SkullbonezCore::Runtime::ReplayPredictionSchedulingOperations::ChooseReplayPredictionBuildMode;
using SkullbonezCore::Runtime::ReplayPredictionSchedulingOperations::ChooseReplayPredictionCoalescerAction;
using SkullbonezCore::Runtime::ReplayPredictionSchedulingOperations::UpdateReplayPredictionTicksPerMs;

namespace
{
SkullbonezCore::Math::Vector::Vector3 ContinuousPosition( std::uint64_t tick, std::size_t body )
{
    return { static_cast<float>( tick ), static_cast<float>( body ), static_cast<float>( tick + body ) };
}

bool PublishContinuousRow( ContinuousPredictionSampleRing& ring, std::uint64_t tick )
{
    if ( !ring.BeginRow( tick ) )
    {
        return false;
    }

    for ( std::size_t body = 0u; body < ring.BodyCount(); ++body )
    {
        if ( !ring.WriteBodyPosition( body, ContinuousPosition( tick, body ) ) )
        {
            return false;
        }
    }

    return ring.PublishRow();
}

bool ContinuousSnapshotRowsMatch( const ContinuousPredictionSampleRingSnapshot& snapshot )
{
    std::array<SkullbonezCore::Math::Vector::Vector3, 3> positions;

    if ( snapshot.BodyCount() > positions.size() )
    {
        return false;
    }

    for ( std::size_t logicalRow = 0u; logicalRow < snapshot.RowCount(); ++logicalRow )
    {
        std::uint64_t tick = 0u;

        if ( !snapshot.TryReadRow( logicalRow, std::span( positions ).first( snapshot.BodyCount() ), tick ) )
        {
            return false;
        }

        if ( tick != snapshot.OldestAbsoluteTick() + logicalRow )
        {
            return false;
        }

        for ( std::size_t body = 0u; body < snapshot.BodyCount(); ++body )
        {
            const auto expected = ContinuousPosition( tick, body );

            if ( positions[body].x != expected.x || positions[body].y != expected.y || positions[body].z != expected.z )
            {
                return false;
            }
        }
    }

    return true;
}
} // namespace

TEST_CASE( "Replay prediction detail mode: transitions preserve one operator preference" )
{
    constexpr ReplayPredictionDetailMode defaultMode = ReplayPredictionDetailMode::High;
    CHECK( defaultMode == ReplayPredictionDetailMode::High );

    const ReplayPredictionDetailTransitionAction
        noChange = EvaluateReplayPredictionDetailTransition( defaultMode, ReplayPredictionDetailMode::High );
    CHECK( noChange == ReplayPredictionDetailTransitionAction::None );

    const ReplayPredictionDetailTransitionAction
        selectLow = EvaluateReplayPredictionDetailTransition( defaultMode, ReplayPredictionDetailMode::Low );
    CHECK( ReplayPredictionDetailTransitionHas( selectLow, ReplayPredictionDetailTransitionAction::RestartGeneration ) );
    CHECK( ReplayPredictionDetailTransitionHas( selectLow,
                                                ReplayPredictionDetailTransitionAction::ClearPredictionInspection ) );
    CHECK( ReplayPredictionDetailTransitionHas( selectLow,
                                                ReplayPredictionDetailTransitionAction::ReleaseHighDetailCapacity ) );

    const ReplayPredictionDetailTransitionAction
        selectHigh = EvaluateReplayPredictionDetailTransition( ReplayPredictionDetailMode::Low,
                                                               ReplayPredictionDetailMode::High );
    CHECK( ReplayPredictionDetailTransitionHas( selectHigh, ReplayPredictionDetailTransitionAction::RestartGeneration ) );
    CHECK( ReplayPredictionDetailTransitionHas( selectHigh,
                                                ReplayPredictionDetailTransitionAction::ClearPredictionInspection ) );
    CHECK_FALSE( ReplayPredictionDetailTransitionHas( selectHigh,
                                                      ReplayPredictionDetailTransitionAction::ReleaseHighDetailCapacity ) );
}

TEST_CASE( "Replay prediction detail mode: archive capability cannot replace the active preference" )
{
    CHECK( ReplayPredictionDetailModeAfterArchiveLoad( ReplayPredictionDetailMode::High,
                                                       ReplayPredictionArchiveDetailCapability::Low ) ==
           ReplayPredictionDetailMode::High );
    CHECK( ReplayPredictionDetailModeAfterArchiveLoad( ReplayPredictionDetailMode::Low,
                                                       ReplayPredictionArchiveDetailCapability::High ) ==
           ReplayPredictionDetailMode::Low );
}

TEST_CASE( "Replay prediction detail mode: generation resets preserve the active preference" )
{
    constexpr std::array resetReasons = { ReplayPredictionGenerationResetReason::Scene,
                                          ReplayPredictionGenerationResetReason::Owner,
                                          ReplayPredictionGenerationResetReason::PredictToggle };

    for ( ReplayPredictionGenerationResetReason reason : resetReasons )
    {
        CHECK( ReplayPredictionDetailModeAfterGenerationReset( ReplayPredictionDetailMode::High, reason ) ==
               ReplayPredictionDetailMode::High );
        CHECK( ReplayPredictionDetailModeAfterGenerationReset( ReplayPredictionDetailMode::Low, reason ) ==
               ReplayPredictionDetailMode::Low );
    }
}

TEST_CASE( "Replay prediction path presentation: all-body roots require explicit space policy" )
{
    CHECK_FALSE( ReplayPredictionPathPresentationShowsAllBodies( ReplayPredictionPathPresentation::SelectedCausalTree ) );
    CHECK( ReplayPredictionPathPresentationShowsAllBodies( ReplayPredictionPathPresentation::AllBodiesSpace ) );
}
using SkullbonezCore::Runtime::ReplayTrajectoryLane;
using SkullbonezCore::Runtime::ReplayTrajectoryRecord;
using SkullbonezCore::Runtime::ReplayTrajectoryRecordKey;
using SkullbonezCore::Runtime::ReplayTrajectoryStoreOperations::SelectDormantRecordIndex;

TEST_CASE( "Replay prediction scheduling: measured cost selects instant or amortized mode" )
{
    CHECK( ChooseReplayPredictionBuildMode( 0.0, 2400, 30.0, 1u ) == ReplayPredictionBuildMode::Undecided );
    CHECK( ChooseReplayPredictionBuildMode( -1.0, 2400, 30.0, 1u ) == ReplayPredictionBuildMode::Undecided );
    CHECK( ChooseReplayPredictionBuildMode( 100.0, 2400, 0.0, 1u ) == ReplayPredictionBuildMode::Amortized );
    CHECK( ChooseReplayPredictionBuildMode( 100.0, 2400, 30.0, 1u ) == ReplayPredictionBuildMode::Instant );
    CHECK( ChooseReplayPredictionBuildMode( 50.0, 2400, 30.0, 1u ) == ReplayPredictionBuildMode::Amortized );
    CHECK( ChooseReplayPredictionBuildMode( 10000.0, 2400, 30.0, 216u ) == ReplayPredictionBuildMode::Amortized );
}

TEST_CASE( "Replay prediction scheduling: throughput feedback follows changing worker cost" )
{
    CHECK( UpdateReplayPredictionTicksPerMs( 100.0, 100, 2.0 ) == doctest::Approx( 87.5 ) );
    CHECK( UpdateReplayPredictionTicksPerMs( 0.0, 100, 2.0 ) == doctest::Approx( 50.0 ) );
}

TEST_CASE( "Replay prediction scheduling: instant dirty work is superseded without cancellation" )
{
    CHECK( ChooseReplayPredictionCoalescerAction( true, true, ReplayPredictionBuildMode::Instant, false, false ) ==
           ReplayPredictionCoalescerAction::Supersede );
    CHECK( ChooseReplayPredictionCoalescerAction( false, false, ReplayPredictionBuildMode::Instant, true, false ) ==
           ReplayPredictionCoalescerAction::Begin );
    CHECK( ChooseReplayPredictionCoalescerAction( true, true, ReplayPredictionBuildMode::Amortized, false, false ) ==
           ReplayPredictionCoalescerAction::Supersede );
    CHECK( ChooseReplayPredictionCoalescerAction( true, true, ReplayPredictionBuildMode::Amortized, false, true ) ==
           ReplayPredictionCoalescerAction::PromoteAndBegin );
    CHECK( ChooseReplayPredictionCoalescerAction( false, true, ReplayPredictionBuildMode::Instant, false, false ) ==
           ReplayPredictionCoalescerAction::Nothing );
}

TEST_CASE( "Replay prediction publication: release cursor is bounded and reset clears failure" )
{
    ReplayPredictionPublication publication;
    CHECK( publication.PublishedCount( 4u ) == 0u );

    publication.PublishSlot( 0u, 4u );
    publication.PublishSlot( 2u, 4u );
    CHECK( publication.PublishedCount( 4u ) == 3u );
    CHECK( publication.PublishedCount( 2u ) == 2u );

    publication.MarkWorkerFailed();
    CHECK( publication.WorkerFailed() );
    publication.Reset();
    CHECK( publication.PublishedCount( 4u ) == 0u );
    CHECK_FALSE( publication.WorkerFailed() );
}

TEST_CASE( "Replay prediction publication: build root follows the acquired presentation prefix" )
{
    CHECK( ReplayPredictionBuildRootPrefixCount( 0u, 4u ) == 0u );
    CHECK( ReplayPredictionBuildRootPrefixCount( 3u, 4u ) == 3u );
    CHECK( ReplayPredictionBuildRootPrefixCount( 99u, 4u ) == 4u );
}

TEST_CASE( "Continuous prediction sample ring: empty and partial windows publish complete rows" )
{
    ContinuousPredictionSampleRing ring;
    REQUIRE( ring.Prepare( 4u, 2u ) );
    const std::uint64_t storageGeneration = ring.StorageGeneration();
    const std::size_t retainedBytes = ring.RetainedBytes();
    CHECK( retainedBytes > 0u );
    CHECK( ring.Prepare( 4u, 2u ) );
    CHECK( ring.StorageGeneration() == storageGeneration );
    REQUIRE( ring.Start() );

    const ContinuousPredictionSampleRingSnapshot empty = ring.AcquireSnapshot();
    CHECK( empty.Empty() );
    CHECK( empty.SegmentCount() == 0u );
    CHECK_FALSE( ring.Prepare( 8u, 2u ) );
    CHECK( ring.StorageGeneration() == storageGeneration );
    CHECK( ring.RetainedBytes() == retainedBytes );

    REQUIRE( PublishContinuousRow( ring, 0u ) );
    REQUIRE( PublishContinuousRow( ring, 1u ) );
    const ContinuousPredictionSampleRingSnapshot partial = ring.AcquireSnapshot();
    CHECK( partial.RowCount() == 2u );
    CHECK( partial.OldestAbsoluteTick() == 0u );
    CHECK( partial.NewestAbsoluteTick() == 1u );
    CHECK( partial.SegmentCount() == 1u );
    CHECK( partial.SegmentAt( 0u ).LogicalRowOffset() == 0u );
    CHECK( partial.SegmentAt( 0u ).PhysicalRowOffset() == 0u );
    CHECK( partial.SegmentAt( 0u ).RowCount() == 2u );
    CHECK( ContinuousSnapshotRowsMatch( partial ) );
}

TEST_CASE( "Continuous prediction sample ring: exactly full and one-wrap windows stay chronological" )
{
    ContinuousPredictionSampleRing ring;
    REQUIRE( ring.Prepare( 3u, 2u ) );
    REQUIRE( ring.Start() );

    REQUIRE( PublishContinuousRow( ring, 0u ) );
    REQUIRE( PublishContinuousRow( ring, 1u ) );
    REQUIRE( PublishContinuousRow( ring, 2u ) );
    const ContinuousPredictionSampleRingSnapshot full = ring.AcquireSnapshot();
    CHECK( full.RowCount() == 3u );
    CHECK( full.OldestAbsoluteTick() == 0u );
    CHECK( full.NewestAbsoluteTick() == 2u );
    CHECK( full.SegmentCount() == 1u );
    CHECK( full.SegmentAt( 0u ).RowCount() == 3u );
    CHECK( ContinuousSnapshotRowsMatch( full ) );

    REQUIRE( PublishContinuousRow( ring, 3u ) );
    const ContinuousPredictionSampleRingSnapshot wrapped = ring.AcquireSnapshot();
    CHECK( wrapped.RowCount() == 3u );
    CHECK( wrapped.OldestAbsoluteTick() == 1u );
    CHECK( wrapped.NewestAbsoluteTick() == 3u );
    CHECK( wrapped.SegmentCount() == 2u );
    CHECK( wrapped.SegmentAt( 0u ).LogicalRowOffset() == 0u );
    CHECK( wrapped.SegmentAt( 0u ).PhysicalRowOffset() == 1u );
    CHECK( wrapped.SegmentAt( 0u ).RowCount() == 2u );
    CHECK( wrapped.SegmentAt( 1u ).LogicalRowOffset() == 2u );
    CHECK( wrapped.SegmentAt( 1u ).PhysicalRowOffset() == 0u );
    CHECK( wrapped.SegmentAt( 1u ).RowCount() == 1u );
    CHECK( wrapped.SegmentAt( 2u ).RowCount() == 0u );
    CHECK( ContinuousSnapshotRowsMatch( wrapped ) );
}

TEST_CASE( "Continuous prediction sample ring: multiple wraps retain storage and the newest window" )
{
    ContinuousPredictionSampleRing ring;
    REQUIRE( ring.Prepare( 4u, 3u ) );
    REQUIRE( ring.Start() );
    const std::uint64_t storageGeneration = ring.StorageGeneration();
    const std::size_t retainedBytes = ring.RetainedBytes();

    for ( std::uint64_t tick = 0u; tick < 20u; ++tick )
    {
        REQUIRE( PublishContinuousRow( ring, tick ) );
    }

    const ContinuousPredictionSampleRingSnapshot snapshot = ring.AcquireSnapshot();
    CHECK( snapshot.RowCount() == 4u );
    CHECK( snapshot.OldestAbsoluteTick() == 16u );
    CHECK( snapshot.NewestAbsoluteTick() == 19u );
    CHECK( snapshot.SegmentCount() == 1u );
    CHECK( ContinuousSnapshotRowsMatch( snapshot ) );
    CHECK( ring.StorageGeneration() == storageGeneration );
    CHECK( ring.RetainedBytes() == retainedBytes );
}

TEST_CASE( "Continuous prediction sample ring: cancellation retires an incomplete row without growth" )
{
    ContinuousPredictionSampleRing ring;
    REQUIRE( ring.Prepare( 4u, 2u ) );
    REQUIRE( ring.Start() );
    const std::uint64_t storageGeneration = ring.StorageGeneration();
    const std::size_t retainedBytes = ring.RetainedBytes();
    REQUIRE( PublishContinuousRow( ring, 0u ) );
    REQUIRE( ring.BeginRow( 1u ) );
    REQUIRE( ring.WriteBodyPosition( 0u, ContinuousPosition( 1u, 0u ) ) );

    ring.RequestCancellation();
    CHECK_FALSE( ring.WriteBodyPosition( 1u, ContinuousPosition( 1u, 1u ) ) );
    CHECK_FALSE( ring.PublishRow() );
    CHECK_FALSE( ring.Failed() );
    const ContinuousPredictionSampleRingSnapshot cancelled = ring.AcquireSnapshot();
    CHECK( cancelled.Cancelled() );
    CHECK( cancelled.Empty() );

    ring.ResetAfterJoin();
    CHECK( ring.AcquireSnapshot().Empty() );
    CHECK_FALSE( ring.AcquireSnapshot().Cancelled() );
    CHECK( ring.StorageGeneration() == storageGeneration );
    CHECK( ring.RetainedBytes() == retainedBytes );
    REQUIRE( ring.Start() );
    REQUIRE( PublishContinuousRow( ring, 0u ) );
    CHECK( ContinuousSnapshotRowsMatch( ring.AcquireSnapshot() ) );
}

TEST_CASE( "Continuous prediction sample ring: incomplete rows and counter rollover fail closed" )
{
    ContinuousPredictionSampleRing incomplete;
    REQUIRE( incomplete.Prepare( 2u, 2u ) );
    REQUIRE( incomplete.Start() );
    REQUIRE( incomplete.BeginRow( 0u ) );
    REQUIRE( incomplete.WriteBodyPosition( 0u, ContinuousPosition( 0u, 0u ) ) );
    CHECK_FALSE( incomplete.PublishRow() );
    CHECK( incomplete.Failed() );
    CHECK( incomplete.AcquireSnapshot().Empty() );

    ContinuousPredictionSampleRing overflow;
    REQUIRE( overflow.Prepare( 2u, 1u ) );
    constexpr std::uint64_t lastPublishableTick = ( std::numeric_limits<std::uint64_t>::max )() - 1u;
    REQUIRE( overflow.Start( lastPublishableTick ) );
    REQUIRE( PublishContinuousRow( overflow, lastPublishableTick ) );
    const ContinuousPredictionSampleRingSnapshot finalTick = overflow.AcquireSnapshot();
    CHECK( finalTick.RowCount() == 1u );
    CHECK( finalTick.OldestAbsoluteTick() == lastPublishableTick );
    CHECK( finalTick.NewestAbsoluteTick() == lastPublishableTick );
    CHECK_FALSE( overflow.BeginRow( ( std::numeric_limits<std::uint64_t>::max )() ) );
    CHECK( overflow.Failed() );
    CHECK( overflow.CounterOverflowed() );
    CHECK( overflow.AcquireSnapshot().Empty() );
}

TEST_CASE( "Continuous prediction sample ring: concurrent snapshots never accept a torn row" )
{
    ContinuousPredictionSampleRing ring;
    REQUIRE( ring.Prepare( 32u, 3u ) );
    REQUIRE( ring.Start() );
    constexpr std::uint64_t totalRows = 4096u;
    std::atomic<bool> writerStarted { false };
    std::atomic<bool> writerDone { false };
    std::atomic<bool> writerSucceeded { true };

    std::thread writer(
        [&]()
        {
            writerStarted.store( true, std::memory_order_release );

            for ( std::uint64_t tick = 0u; tick < totalRows; ++tick )
            {
                if ( !PublishContinuousRow( ring, tick ) )
                {
                    writerSucceeded.store( false, std::memory_order_release );
                    break;
                }

                if ( ( tick & 3u ) == 0u )
                {
                    std::this_thread::yield();
                }
            }

            writerDone.store( true, std::memory_order_release );
        } );

    while ( !writerStarted.load( std::memory_order_acquire ) )
    {
        std::this_thread::yield();
    }

    bool coherent = true;
    std::size_t acceptedRows = 0u;

    while ( !writerDone.load( std::memory_order_acquire ) )
    {
        const ContinuousPredictionSampleRingSnapshot snapshot = ring.AcquireSnapshot();
        std::array<SkullbonezCore::Math::Vector::Vector3, 3> positions;

        for ( std::size_t logicalRow = 0u; logicalRow < snapshot.RowCount(); ++logicalRow )
        {
            std::uint64_t tick = 0u;

            if ( !snapshot.TryReadRow( logicalRow, positions, tick ) )
            {
                continue;
            }

            ++acceptedRows;

            for ( std::size_t body = 0u; body < positions.size(); ++body )
            {
                const auto expected = ContinuousPosition( tick, body );
                coherent = coherent && positions[body].x == expected.x && positions[body].y == expected.y &&
                           positions[body].z == expected.z;
            }
        }
    }

    writer.join();
    CHECK( writerSucceeded.load( std::memory_order_acquire ) );
    CHECK( coherent );
    CHECK( acceptedRows > 0u );
    const ContinuousPredictionSampleRingSnapshot finalSnapshot = ring.AcquireSnapshot();
    CHECK( finalSnapshot.RowCount() == 32u );
    CHECK( finalSnapshot.OldestAbsoluteTick() == totalRows - 32u );
    CHECK( finalSnapshot.NewestAbsoluteTick() == totalRows - 1u );
    CHECK( ContinuousSnapshotRowsMatch( finalSnapshot ) );
}

TEST_CASE( "Replay trajectory reuse selects a sufficient dormant record deterministically" )
{
    std::vector<ReplayTrajectoryRecord> records( 4u );
    records[1].key = ReplayTrajectoryRecordKey { { 11u }, ReplayTrajectoryLane::FutureRoot, 0u };
    records[2].key = ReplayTrajectoryRecordKey { { 22u }, ReplayTrajectoryLane::FutureRoot, 0u };
    records[3].key = ReplayTrajectoryRecordKey { { 33u }, ReplayTrajectoryLane::FutureRoot, 0u };
    records[1].points.reserve( 4u );
    records[2].points.reserve( 16u );
    records[3].points.reserve( 8u );

    const ReplayTrajectoryRecordKey matchingKey { { 22u }, ReplayTrajectoryLane::FutureRoot, 0u };
    const ReplayTrajectoryRecordKey newKey { { 44u }, ReplayTrajectoryLane::FutureRoot, 0u };

    CHECK( SelectDormantRecordIndex( records, 1u, matchingKey, 12u ) == 2u );
    CHECK( SelectDormantRecordIndex( records, 1u, newKey, 7u ) == 3u );
    CHECK( SelectDormantRecordIndex( records, 1u, newKey, 32u ) == 2u );
    CHECK( SelectDormantRecordIndex( records, records.size(), newKey, 1u ) == records.size() );
}

TEST_CASE( "Replay sample lookup: stable id and explicit negative-row policy survive fallback scans" )
{
    ReplaySolverFrameSample sample;
    ReplaySolverBodySample sentinelBody;
    sentinelBody.id = { 17u };
    sentinelBody.modelRow.value = -1;
    sample.bodies.push_back( sentinelBody );

    ReplaySolverBodySample movedBody;
    movedBody.id = { 41u };
    movedBody.modelRow.value = 7;
    sample.bodies.push_back( movedBody );

    CHECK( FindReplayBodyByIdInSample<ReplaySolverFrameSample, ReplaySolverBodySample>( sample, { 41u } ) ==
           &sample.bodies[1] );
    CHECK( ( FindReplayBodyByModelIndexInSample<ReplaySolverFrameSample, ReplaySolverBodySample, true>( sample, -1 ) ==
             &sample.bodies[0] ) );
    CHECK( ( FindReplayBodyByModelIndexInSample<ReplaySolverFrameSample, ReplaySolverBodySample, false>( sample, -1 ) ==
             nullptr ) );
    CHECK( ( FindReplayBodyByModelIndexInSample<ReplaySolverFrameSample, ReplaySolverBodySample, true>( sample, 7 ) ==
             &sample.bodies[1] ) );
    CHECK( ( SceneObjectIdForModelIndexInSample<ReplaySolverFrameSample, ReplaySolverBodySample, true>( sample, 7 ).value ==
             41u ) );
}
