/*
File: TestReplayPredictionScheduling.cpp
Purpose:
  Locks replay prediction scheduling and publication protocols.

Summary:
  Worker timing is supplied as a value, while publication tests exercise the
  release/acquire cursor owner without starting a physics worker.

Glossary:
  Instant build: One worker submission for the remaining prediction horizon.
  Supersede: Retain one pending restart without cancelling in-flight work.

Invariants:
  - A zero instant budget always preserves amortized scheduling.
  - Repeated dirty events collapse into one Begin after instant completion.
  - Publication exposes only a contiguous bounded prefix and reset clears failure.

Related:
  - SkullbonezSource/Runtime/Replay/ReplayPredictionScheduling.h
*/
#include "../ThirdPtySource/doctest/doctest.h"

#include "../SkullbonezSource/Runtime/Replay/ReplayPredictionScheduling.h"
#include "../SkullbonezSource/Runtime/Replay/ReplayPredictionPublication.h"

using SkullbonezCore::Runtime::ReplayPredictionBuildMode;
using SkullbonezCore::Runtime::ReplayPredictionCoalescerAction;
using SkullbonezCore::Runtime::ReplayPredictionPublication;
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
