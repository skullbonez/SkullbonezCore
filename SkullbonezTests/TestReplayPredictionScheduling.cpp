/*
File: TestReplayPredictionScheduling.cpp
Purpose:
  Locks replay prediction scheduling and publication protocols.

Summary:
  Worker timing is supplied as a value, while publication tests exercise the
  release/acquire cursor owner and shared sample lookups without starting a
  physics worker.

Glossary:
  Instant build: One worker submission for the remaining prediction horizon.
  Supersede: Retain one pending restart without cancelling in-flight work.
  Model row hint: Sample-local lookup shortcut; stable scene id remains authority.

Invariants:
  - A zero instant budget always preserves amortized scheduling.
  - Repeated dirty events collapse into one Begin after instant completion.
  - Publication exposes only a contiguous bounded prefix and reset clears failure.
  - Solver lookup may preserve a negative sentinel; prediction-style lookup rejects it.

Related:
  - SkullbonezSource/Runtime/Replay/ReplayPredictionScheduling.h
*/
#include "../ThirdPtySource/doctest/doctest.h"

#include "../SkullbonezSource/Runtime/Replay/ReplayPredictionScheduling.h"
#include "../SkullbonezSource/Runtime/Replay/ReplayPredictionPublication.h"
#include "../SkullbonezSource/Runtime/Replay/ReplayPredictionPublicationOperations.h"

using SkullbonezCore::Runtime::ReplayPredictionBuildMode;
using SkullbonezCore::Runtime::ReplayPredictionCoalescerAction;
using SkullbonezCore::Runtime::ReplayPredictionPublication;
using SkullbonezCore::Runtime::ReplaySolverBodySample;
using SkullbonezCore::Runtime::ReplaySolverFrameSample;
using SkullbonezCore::Runtime::ReplayPredictionPublicationOperations::FindReplayBodyByIdInSample;
using SkullbonezCore::Runtime::ReplayPredictionPublicationOperations::FindReplayBodyByModelIndexInSample;
using SkullbonezCore::Runtime::ReplayPredictionPublicationOperations::SceneObjectIdForModelIndexInSample;
using SkullbonezCore::Runtime::ReplayPredictionSchedulingOperations::ChooseReplayPredictionBuildMode;
using SkullbonezCore::Runtime::ReplayPredictionSchedulingOperations::ChooseReplayPredictionCoalescerAction;

TEST_CASE( "Replay prediction scheduling: measured cost selects instant or amortized mode" )
{
    CHECK( ChooseReplayPredictionBuildMode( 0.0, 2400, 30.0 ) == ReplayPredictionBuildMode::Undecided );
    CHECK( ChooseReplayPredictionBuildMode( -1.0, 2400, 30.0 ) == ReplayPredictionBuildMode::Undecided );
    CHECK( ChooseReplayPredictionBuildMode( 100.0, 2400, 0.0 ) == ReplayPredictionBuildMode::Amortized );
    CHECK( ChooseReplayPredictionBuildMode( 100.0, 2400, 30.0 ) == ReplayPredictionBuildMode::Instant );
    CHECK( ChooseReplayPredictionBuildMode( 50.0, 2400, 30.0 ) == ReplayPredictionBuildMode::Amortized );
}

TEST_CASE( "Replay prediction scheduling: instant dirty work is superseded without cancellation" )
{
    CHECK( ChooseReplayPredictionCoalescerAction( true, true, ReplayPredictionBuildMode::Instant, false ) ==
           ReplayPredictionCoalescerAction::Supersede );
    CHECK( ChooseReplayPredictionCoalescerAction( false, false, ReplayPredictionBuildMode::Instant, true ) ==
           ReplayPredictionCoalescerAction::Begin );
    CHECK( ChooseReplayPredictionCoalescerAction( true, true, ReplayPredictionBuildMode::Amortized, false ) ==
           ReplayPredictionCoalescerAction::CancelAndBegin );
    CHECK( ChooseReplayPredictionCoalescerAction( false, true, ReplayPredictionBuildMode::Instant, false ) ==
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
