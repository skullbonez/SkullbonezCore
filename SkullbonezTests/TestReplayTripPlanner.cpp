/*
File: SkullbonezTests/TestReplayTripPlanner.cpp
Purpose:
  Pins the bounded trip-planner state machine, correction math, and abort edges.

Summary:
  Synthetic live orbital values seed the same Lambert path used by Runtime.
  Completed prediction witnesses then drive convergence, correction, failure,
  commit, cancel, and safety reset without constructing an engine.

Glossary:
  Mutation: One value request for Runtime to apply to the live ship.
  Witness generation: Completed prediction prefix observed once by the planner.

Invariants:
  - Tests assert semantic state and values rather than private representation.
  - Synthetic frame storage is test-only; the production owner retains fixed arrays.

Related:
  - SkullbonezSource/Runtime/Replay/ReplayTripPlanner.h
*/
#include "doctest/doctest.h"
#include "../SkullbonezSource/Runtime/Replay/ReplayTripPlanner.h"

#include <cmath>
#include <vector>

namespace
{
using SkullbonezCore::Math::Vector::Vector3;
using SkullbonezCore::Runtime::ReplayTripPlanner;
using SkullbonezCore::Runtime::ReplayTripPlannerBodyState;
using SkullbonezCore::Runtime::ReplayTripPlannerCommand;
using SkullbonezCore::Runtime::ReplayTripPlannerCommandKind;
using SkullbonezCore::Runtime::ReplayTripPlannerLiveInput;
using SkullbonezCore::Runtime::ReplayTripPlannerPredictionInput;
using SkullbonezCore::Runtime::ReplayTripPlannerState;
using SkullbonezCore::Runtime::RunReplayPredictionBodySample;
using SkullbonezCore::Runtime::RunReplayPredictionFrame;

ReplayTripPlannerBodyState Body( uint32_t id, const Vector3& position, const Vector3& velocity, float mass = 1.0f )
{
    ReplayTripPlannerBodyState body;
    body.id.value = id;
    body.position = position;
    body.linearVelocity = velocity;
    body.mass = mass;
    body.valid = true;
    return body;
}

ReplayTripPlannerLiveInput DesignWindow()
{
    constexpr float marsPhase = 44.1f * 3.14159265358979323846f / 180.0f;
    ReplayTripPlannerLiveInput input;
    input.sun = Body( 1, Vector3( 0.0f, 0.0f, 0.0f ), Vector3( 0.0f, 0.0f, 0.0f ), 10000.0f );
    input.ship = Body( 2, Vector3( 80.0f, 0.0f, 0.0f ), Vector3( 0.0f, std::sqrt( 500.0f ), 0.0f ) );
    input.target = Body( 3,
                         Vector3( 121.6f * std::cos( marsPhase ), 121.6f * std::sin( marsPhase ), 0.0f ),
                         Vector3( -std::sqrt( 40000.0f / 121.6f ) * std::sin( marsPhase ),
                                  std::sqrt( 40000.0f / 121.6f ) * std::cos( marsPhase ),
                                  0.0f ) );
    input.gravitationalConstant = 4.0f;
    input.predictionHorizonSeconds = 20.0f;
    input.mutualGravityEnabled = true;
    input.targetSelected = true;
    input.targetName = "Mars";
    return input;
}

RunReplayPredictionFrame Frame( uint32_t frameIndex, const Vector3& shipPosition, const Vector3& targetPosition )
{
    RunReplayPredictionFrame frame;
    frame.frameIndex = frameIndex;
    RunReplayPredictionBodySample ship;
    ship.id.value = 2;
    ship.position = shipPosition;
    RunReplayPredictionBodySample target;
    target.id.value = 3;
    target.position = targetPosition;
    frame.bodies.push_back( ship );
    frame.bodies.push_back( target );
    return frame;
}

ReplayTripPlannerPredictionInput Prediction( const std::vector<RunReplayPredictionFrame>& frames,
                                             uint32_t generation,
                                             float miss,
                                             float eta,
                                             const Vector3& shipPosition,
                                             const Vector3& targetPosition )
{
    ReplayTripPlannerPredictionInput input;
    input.frames = frames;
    input.intercept.valid = true;
    input.intercept.shipId.value = 2;
    input.intercept.targetId.value = 3;
    input.intercept.missDistance = miss;
    input.intercept.intercept = miss <= SkullbonezCore::Runtime::REPLAY_TRIP_PLANNER_INTERCEPT_DISTANCE +
                                           SkullbonezCore::Runtime::REPLAY_INTERCEPT_CONTACT_SLOP;
    input.intercept.etaSeconds = eta;
    input.intercept.shipPosition = shipPosition;
    input.intercept.targetPosition = targetPosition;
    input.shipId.value = 2;
    input.targetId.value = 3;
    input.generation = generation;
    input.complete = true;
    input.targetAvailable = true;
    return input;
}

void BeginDesignPlan( ReplayTripPlanner& planner, const ReplayTripPlannerLiveInput& live )
{
    REQUIRE( planner.QueueCommand( { ReplayTripPlannerCommandKind::Plan } ) );
    const auto mutation = planner.BeginFrame( live );
    REQUIRE( mutation.requested );
    CHECK( mutation.prepareBaseline );
    CHECK( mutation.bodyId.value == 2 );
    CHECK( planner.View().state == ReplayTripPlannerState::Seeding );
    planner.ConfirmVelocityApplied();
    REQUIRE( planner.View().state == ReplayTripPlannerState::AwaitingPrediction );
}
} // namespace

TEST_CASE( "Replay trip planner converges and commits a real prediction witness" )
{
    ReplayTripPlanner planner;
    const ReplayTripPlannerLiveInput live = DesignWindow();
    BeginDesignPlan( planner, live );

    std::vector<RunReplayPredictionFrame> frames;
    frames.push_back( Frame( 0, Vector3( 80.0f, 0.0f, 0.0f ), Vector3( 87.0f, 84.0f, 0.0f ) ) );
    frames.push_back( Frame( 954, Vector3( -121.0f, 0.0f, 0.0f ), Vector3( -120.0f, 0.0f, 0.0f ) ) );
    const auto result = planner.ObservePrediction(
        Prediction( frames, 1, 1.0f, 15.9f, Vector3( -121.0f, 0.0f, 0.0f ), Vector3( -120.0f, 0.0f, 0.0f ) ) );
    CHECK_FALSE( result.requested );
    CHECK( planner.View().state == ReplayTripPlannerState::Converged );
    CHECK( planner.View().iteration == 1 );
    CHECK( planner.View().ghostCount == 1 );
    CHECK( planner.View().ghosts[0].pointCount == 2 );

    REQUIRE( planner.QueueCommand( { ReplayTripPlannerCommandKind::Commit } ) );
    CHECK_FALSE( planner.BeginFrame( live ).requested );
    CHECK( planner.View().state == ReplayTripPlannerState::Idle );
    CHECK( planner.View().ghostCount == 0 );
}

TEST_CASE( "Replay trip planner applies bound correction then reports honest failure" )
{
    ReplayTripPlanner planner;
    const ReplayTripPlannerLiveInput live = DesignWindow();
    BeginDesignPlan( planner, live );
    const Vector3 seed = planner.View().candidateVelocity;
    const Vector3 shipAtClosest( 10.0f, -2.0f, 0.0f );
    const Vector3 targetAtClosest( 20.0f, 3.0f, 0.0f );
    std::vector<RunReplayPredictionFrame> frames{ Frame( 0, live.ship.position, live.target.position ),
                                                  Frame( 600, shipAtClosest, targetAtClosest ) };

    const auto correction =
        planner.ObservePrediction( Prediction( frames, 1, 10.0f, 10.0f, shipAtClosest, targetAtClosest ) );
    REQUIRE( correction.requested );
    CHECK( planner.View().state == ReplayTripPlannerState::Correcting );
    CHECK( planner.View().iteration == 2 );
    CHECK( correction.linearVelocity.x == doctest::Approx( seed.x + 0.8f ) );
    CHECK( correction.linearVelocity.y == doctest::Approx( seed.y + 0.4f ) );
    planner.ConfirmVelocityApplied();

    const auto repeated =
        planner.ObservePrediction( Prediction( frames, 2, 12.0f, 10.0f, shipAtClosest, targetAtClosest ) );
    CHECK_FALSE( repeated.requested );
    CHECK( planner.View().state == ReplayTripPlannerState::Failed );
    CHECK( planner.View().noSolution );

    REQUIRE( planner.QueueCommand( { ReplayTripPlannerCommandKind::Cancel } ) );
    const auto restore = planner.BeginFrame( live );
    REQUIRE( restore.requested );
    CHECK( restore.linearVelocity.x == doctest::Approx( live.ship.linearVelocity.x ) );
    CHECK( restore.linearVelocity.y == doctest::Approx( live.ship.linearVelocity.y ) );
    CHECK( planner.View().state == ReplayTripPlannerState::Idle );
}

TEST_CASE( "Replay trip planner abort edges clear transient state" )
{
    ReplayTripPlanner planner;
    ReplayTripPlannerLiveInput live = DesignWindow();
    BeginDesignPlan( planner, live );
    live.liveAdvanceHeld = true;
    CHECK_FALSE( planner.BeginFrame( live ).requested );
    CHECK( planner.View().state == ReplayTripPlannerState::Idle );

    live = DesignWindow();
    BeginDesignPlan( planner, live );
    live.targetSelected = false;
    CHECK_FALSE( planner.BeginFrame( live ).requested );
    CHECK( planner.View().state == ReplayTripPlannerState::Idle );

    live = DesignWindow();
    BeginDesignPlan( planner, live );
    std::vector<RunReplayPredictionFrame> frames;
    ReplayTripPlannerPredictionInput cancelled;
    cancelled.shipId.value = 2;
    cancelled.targetId.value = 3;
    cancelled.cancelled = true;
    cancelled.targetAvailable = true;
    CHECK_FALSE( planner.ObservePrediction( cancelled ).requested );
    CHECK( planner.View().state == ReplayTripPlannerState::Idle );

    BeginDesignPlan( planner, live );
    planner.Reset();
    CHECK( planner.View().state == ReplayTripPlannerState::Idle );
    CHECK_FALSE( planner.View().visible );
}

TEST_CASE( "Replay trip planner TOF and command queue remain bounded" )
{
    ReplayTripPlanner planner;
    CHECK_FALSE( planner.RequiresLiveInput() );
    CHECK_FALSE( planner.AwaitingPrediction() );
    ReplayTripPlannerLiveInput live = DesignWindow();
    CHECK( planner.QueueCommand( { ReplayTripPlannerCommandKind::SetTimeOfFlight, 100.0f } ) );
    CHECK( planner.RequiresLiveInput() );
    CHECK( planner.QueueCommand( { ReplayTripPlannerCommandKind::TogglePanel } ) );
    for ( std::size_t index = 2; index < SkullbonezCore::Runtime::REPLAY_TRIP_PLANNER_COMMAND_CAPACITY; ++index )
    {
        CHECK( planner.QueueCommand( { ReplayTripPlannerCommandKind::DecreaseTimeOfFlight } ) );
    }
    CHECK_FALSE( planner.QueueCommand( { ReplayTripPlannerCommandKind::IncreaseTimeOfFlight } ) );
    (void)planner.BeginFrame( live );
    CHECK( planner.View().visible );
    CHECK( planner.RequiresLiveInput() );
    CHECK( planner.View().timeOfFlightSeconds <= live.predictionHorizonSeconds );
    CHECK( planner.View().timeOfFlightSeconds >= SkullbonezCore::Runtime::REPLAY_TRIP_PLANNER_MIN_TOF_SECONDS );
}
