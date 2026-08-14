/*
File: TestReplayPredictionScheduling.cpp
Purpose:
  Locks replay prediction scheduling and publication protocols.

Summary:
  Worker timing is supplied as a value for build-mode and feedback decisions,
  while publication tests exercise the release/acquire cursor owner, build-root
  visibility, and shared sample lookups without starting a physics worker.

Glossary:
  Instant build: One worker submission for the remaining prediction horizon.
  Supersede: Retain one pending restart without cancelling in-flight work.
  Presented replacement: Build prefix prepared coherently by the frame thread
    at least once, so a newer velocity may safely replace it.
  Model row hint: Sample-local lookup shortcut; stable scene id remains authority.

Invariants:
  - A zero instant budget always preserves amortized scheduling.
  - Throughput calibration follows changing worker cost without reacting fully
    to one noisy slice.
  - A large-body prediction remains amortized even when its cheap probe reports
    implausibly high throughput.
  - Repeated dirty events cannot cancel an amortized replacement until one
    coherent changed prefix has been presented.
  - Publication exposes only a contiguous bounded prefix and reset clears failure.
  - A build-root trajectory exposes exactly the frame-thread presentation prefix.
  - Dormant trajectory reuse selects sufficient capacity without changing active order.
  - Solver lookup may preserve a negative sentinel; prediction-style lookup rejects it.

Related:
  - SkullbonezSource/Runtime/Prediction/ReplayPredictionScheduling.h
*/
#include "../ThirdPtySource/doctest/doctest.h"

#include "../SkullbonezSource/Runtime/Prediction/ReplayPredictionScheduling.h"
#include "../SkullbonezSource/Runtime/Prediction/ReplayPredictionPublication.h"
#include "../SkullbonezSource/Runtime/Prediction/ReplayPredictionPublicationOperations.h"

using SkullbonezCore::Runtime::ReplayPredictionBuildMode;
using SkullbonezCore::Runtime::ReplayPredictionCoalescerAction;
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
    CHECK( ChooseReplayPredictionBuildMode( 10000.0, 2400, 30.0, 216u ) ==
           ReplayPredictionBuildMode::Amortized );
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
    CHECK( SelectDormantRecordIndex( records, 1u, newKey, 7u ) == 2u );
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
    CHECK( (
        SceneObjectIdForModelIndexInSample<ReplaySolverFrameSample, ReplaySolverBodySample, true>( sample, 7 ).value ==
        41u ) );
}
