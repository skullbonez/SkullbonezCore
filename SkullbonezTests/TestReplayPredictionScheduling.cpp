/*
File: TestReplayPredictionScheduling.cpp
Purpose:
  Locks replay prediction mode selection and latest-wins coalescing behavior.

Summary:
  These are pure policy tests. Worker timing is supplied as a value, while the
  coalescer receives one frame-thread state snapshot and returns one action.

Glossary:
  Instant build: One worker submission for the remaining prediction horizon.
  Supersede: Retain one pending restart without cancelling in-flight work.

Invariants:
  - A zero instant budget always preserves amortized scheduling.
  - Repeated dirty events collapse into one Begin after instant completion.

Related:
  - SkullbonezSource/Runtime/Replay/ReplayPredictionScheduling.h
*/
#include "../ThirdPtySource/doctest/doctest.h"

#include "../SkullbonezSource/Runtime/Replay/ReplayPredictionScheduling.h"

using SkullbonezCore::Runtime::ChooseReplayPredictionBuildMode;
using SkullbonezCore::Runtime::ChooseReplayPredictionCoalescerAction;
using SkullbonezCore::Runtime::ReplayPredictionBuildMode;
using SkullbonezCore::Runtime::ReplayPredictionCoalescerAction;

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
    CHECK( ChooseReplayPredictionCoalescerAction( true,
                                                  true,
                                                  ReplayPredictionBuildMode::Instant,
                                                  false ) == ReplayPredictionCoalescerAction::Supersede );
    CHECK( ChooseReplayPredictionCoalescerAction( false,
                                                  false,
                                                  ReplayPredictionBuildMode::Instant,
                                                  true ) == ReplayPredictionCoalescerAction::Begin );
    CHECK( ChooseReplayPredictionCoalescerAction( true,
                                                  true,
                                                  ReplayPredictionBuildMode::Amortized,
                                                  false ) ==
           ReplayPredictionCoalescerAction::CancelAndBegin );
    CHECK( ChooseReplayPredictionCoalescerAction( false,
                                                  true,
                                                  ReplayPredictionBuildMode::Instant,
                                                  false ) == ReplayPredictionCoalescerAction::Nothing );
}
