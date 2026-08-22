/*
File: SkullbonezTests/TestSimulationSystem.cpp
Purpose:
  Pins wall-clock fixed-frequency scheduling, explicit render-frame lockstep,
  and the presentation fraction between committed Physics ticks.

Summary:
  SimulationSystem owns both wall-clock accumulation and explicit render-frame
  lockstep. Tests distinguish elapsed-time scheduling from frame-count-driven
  advancement and prove capped catch-up discards only whole ticks.

Glossary:
  Wall-clock scheduling: Converts elapsed render-frame time into a count of
    fixed-frequency Physics ticks.
  Render-frame lockstep: Deterministic policy that commits one Physics tick for
    each rendered frame regardless of elapsed wall time.
  Presentation alpha: Fraction between the previous and current completed
    physics poses, derived from time left in the fixed-step accumulator.
  Hitch event: One scheduling call that requests more whole ticks than its
    pacing policy permits.
  Lifecycle reset: Idempotent pacing reset applied after a scene generation
    reaches the cleared phase.

Invariants:
  - One wall-clock second commits the same physics time at every render rate.
  - Explicit render-frame lockstep commits one fixed tick per rendered frame.
  - Deterministic lockstep and paused simulation publish exact state.
  - Presentation alpha never changes the committed physics tick count.
  - Catch-up accounting drops whole ticks but retains fractional cadence.
  - A scene generation resets pacing at most once; later phase samples cannot
    erase work accumulated after that reset.

Related:
  - SkullbonezSource/Runtime/Simulation/SimulationSystem.h
*/
#include "../ThirdPtySource/doctest/doctest.h"

#include "../SkullbonezSource/Physics/PhysicsTimestep.h"
#include "../SkullbonezSource/Runtime/Simulation/SimulationSystem.h"

using namespace SkullbonezCore::Runtime;

namespace
{
constexpr int PHYSICS_TICKS_PER_SECOND = 120;

int CountWallClockTicksForOneSecond( int renderedFrameCount )
{
    SimulationSystem simulation;
    SimulationTickInput input;
    input.secondsPerFrame = 1.0 / static_cast<double>( renderedFrameCount );
    input.canStepPhysics = true;
    input.isFixedStep = false;
    input.physicsAdvance = PhysicsAdvanceState::Running;

    int committedTicks = 0;

    for ( int frame = 0; frame < renderedFrameCount; ++frame )
    {
        const SimulationTickResult result = simulation.Tick( input );
        CHECK( result.droppedPhysicsTicks == 0 );
        committedTicks += result.committedPhysicsTicks;
    }

    return committedTicks;
}

int CountRenderFrameLockstepTicksForOneSecond( int renderedFrameCount )
{
    SimulationSystem simulation;
    SimulationTickInput input;
    input.secondsPerFrame = 1.0 / static_cast<double>( renderedFrameCount );
    input.canStepPhysics = true;
    input.isFixedStep = true;
    input.physicsAdvance = PhysicsAdvanceState::Running;

    int committedTicks = 0;

    for ( int frame = 0; frame < renderedFrameCount; ++frame )
    {
        const SimulationTickResult result = simulation.Tick( input );
        CHECK( result.committedPhysicsTicks == 1 );
        CHECK( result.droppedPhysicsTicks == 0 );
        committedTicks += result.committedPhysicsTicks;
    }

    return committedTicks;
}
} // namespace

TEST_CASE( "SimulationSystem exposes the leftover live fixed-tick fraction" )
{
    SimulationSystem simulation;
    SimulationTickInput input;
    input.secondsPerFrame = PHYSICS_FIXED_DT * 0.5;
    input.canStepPhysics = true;
    input.physicsAdvance = PhysicsAdvanceState::Running;

    const SimulationTickResult halfTick = simulation.Tick( input );
    CHECK( halfTick.committedPhysicsTicks == 0 );
    CHECK( halfTick.presentationAlpha == doctest::Approx( 0.5f ) );

    const SimulationTickResult boundary = simulation.Tick( input );
    CHECK( boundary.committedPhysicsTicks == 1 );
    CHECK( boundary.presentationAlpha == doctest::Approx( 0.0f ) );
}

TEST_CASE( "SimulationSystem commits one wall-clock second independently of render rate" )
{
    const int ticksAt60Fps = CountWallClockTicksForOneSecond( 60 );
    const int ticksAt300Fps = CountWallClockTicksForOneSecond( 300 );
    const int ticksAt500Fps = CountWallClockTicksForOneSecond( 500 );

    CHECK( ticksAt60Fps == PHYSICS_TICKS_PER_SECOND );
    CHECK( ticksAt300Fps == PHYSICS_TICKS_PER_SECOND );
    CHECK( ticksAt500Fps == PHYSICS_TICKS_PER_SECOND );
    CHECK( ticksAt60Fps == ticksAt300Fps );
    CHECK( ticksAt300Fps == ticksAt500Fps );
}

TEST_CASE( "SimulationSystem render-frame lockstep remains frame-count driven" )
{
    CHECK( CountRenderFrameLockstepTicksForOneSecond( 60 ) == 60 );
    CHECK( CountRenderFrameLockstepTicksForOneSecond( 300 ) == 300 );
    CHECK( CountRenderFrameLockstepTicksForOneSecond( 500 ) == 500 );
}

TEST_CASE( "SimulationSystem pins deterministic and paused presentation to current state" )
{
    SimulationSystem simulation;
    SimulationTickInput input;
    input.secondsPerFrame = 1.0 / 144.0;
    input.canStepPhysics = true;
    input.isFixedStep = true;
    input.physicsAdvance = PhysicsAdvanceState::Running;

    const SimulationTickResult deterministic = simulation.Tick( input );
    CHECK( deterministic.presentationAlpha == doctest::Approx( 1.0f ) );

    input.isFixedStep = false;
    input.physicsAdvance = PhysicsAdvanceState::Paused;
    const SimulationTickResult paused = simulation.Tick( input );
    CHECK( paused.committedPhysicsTicks == 0 );
    CHECK( paused.presentationAlpha == doctest::Approx( 1.0f ) );
}

TEST_CASE( "SimulationSystem produces even 144 Hz presentation cadence over 120 Hz physics" )
{
    SimulationSystem simulation;
    SimulationTickInput input;
    input.secondsPerFrame = 1.0 / 144.0;
    input.canStepPhysics = true;
    input.physicsAdvance = PhysicsAdvanceState::Running;

    int currentPose = 0;
    float previousPresentedPose = 0.0f;
    for ( int frame = 0; frame < 144; ++frame )
    {
        const SimulationTickResult result = simulation.Tick( input );
        currentPose += result.committedPhysicsTicks;
        const int previousPose = ( currentPose > 0 ) ? currentPose - 1 : 0;
        const float presentedPose = static_cast<float>( previousPose ) +
                                    result.presentationAlpha * static_cast<float>( currentPose - previousPose );

        if ( frame >= 2 )
        {
            // The interpolated point advances by 120/144 of a solver unit on
            // every display frame; authoritative poses still advance in ints.
            CHECK( presentedPose - previousPresentedPose == doctest::Approx( 120.0f / 144.0f ) );
        }
        previousPresentedPose = presentedPose;
    }
    // Invariant: one wall-clock second commits all 120 fixed Physics ticks;
    // interpolation changes only presentation, never authoritative time.
    CHECK( currentPose == PHYSICS_TICKS_PER_SECOND );
}

TEST_CASE( "SimulationSystem wall-clock hitch drops whole ticks and carries only the fraction" )
{
    SimulationSystem simulation;
    SimulationTickInput input;
    input.canStepPhysics = true;
    input.isFixedStep = false;
    input.physicsAdvance = PhysicsAdvanceState::Running;

    constexpr int excessWholeTicks = 3;
    input.secondsPerFrame = static_cast<double>( PHYSICS_FIXED_DT ) *
                            ( static_cast<double>( PHYSICS_MAX_STEPS_PER_FRAME + excessWholeTicks ) + 0.75 );

    const SimulationTickResult hitch = simulation.Tick( input );
    CHECK( hitch.committedPhysicsTicks <= PHYSICS_MAX_STEPS_PER_FRAME );
    CHECK( hitch.committedPhysicsTicks == PHYSICS_MAX_STEPS_PER_FRAME );
    CHECK( hitch.droppedPhysicsTicks == excessWholeTicks );
    CHECK( simulation.DroppedPhysicsTickCount() == static_cast<uint64_t>( excessWholeTicks ) );
    CHECK( simulation.PhysicsHitchEventCount() == 1u );

    // The cap discards the three excess whole ticks. The retained 0.75 tick
    // combines with the next quarter tick instead of replaying stale catch-up.
    input.secondsPerFrame = static_cast<double>( PHYSICS_FIXED_DT ) * 0.25;
    const SimulationTickResult fractionalCarry = simulation.Tick( input );
    CHECK( fractionalCarry.committedPhysicsTicks == 1 );
    CHECK( fractionalCarry.droppedPhysicsTicks == 0 );
    CHECK( fractionalCarry.presentationAlpha == doctest::Approx( 0.0f ) );
    CHECK( simulation.DroppedPhysicsTickCount() == static_cast<uint64_t>( excessWholeTicks ) );
    CHECK( simulation.PhysicsHitchEventCount() == 1u );
}

TEST_CASE( "SimulationSystem drops excess fixed-step catch-up ticks and retains the fraction" )
{
    SimulationSystem simulation;
    SimulationTickInput input;
    input.canStepPhysics = true;
    input.isFixedStep = true;
    input.physicsAdvance = PhysicsAdvanceState::Running;
    input.timeScale = 12.75f;

    const SimulationTickResult hitch = simulation.Tick( input );
    CHECK( hitch.committedPhysicsTicks == 5 );
    CHECK( hitch.droppedPhysicsTicks == 7 );
    CHECK( hitch.simulationDt == doctest::Approx( PHYSICS_FIXED_DT * 5.0f ) );
    CHECK( simulation.DroppedPhysicsTickCount() == 7u );
    CHECK( simulation.PhysicsHitchEventCount() == 1u );

    // The seven dropped whole ticks never reappear. Only the retained 0.75
    // fraction combines with the next quarter-tick request.
    input.timeScale = 0.25f;
    const SimulationTickResult fractionalCarry = simulation.Tick( input );
    CHECK( fractionalCarry.committedPhysicsTicks == 1 );
    CHECK( fractionalCarry.droppedPhysicsTicks == 0 );
    CHECK( simulation.DroppedPhysicsTickCount() == 7u );
    CHECK( simulation.PhysicsHitchEventCount() == 1u );
}

TEST_CASE( "SimulationSystem accumulates hitch diagnostics and Reset clears the owner counters" )
{
    SimulationSystem simulation;
    SimulationTickInput input;
    input.canStepPhysics = true;
    input.isFixedStep = true;
    input.physicsAdvance = PhysicsAdvanceState::Running;

    input.timeScale = 6.0f;
    CHECK( simulation.Tick( input ).droppedPhysicsTicks == 1 );
    input.timeScale = 9.5f;
    CHECK( simulation.Tick( input ).droppedPhysicsTicks == 4 );
    CHECK( simulation.DroppedPhysicsTickCount() == 5u );
    CHECK( simulation.PhysicsHitchEventCount() == 2u );

    simulation.Reset();
    CHECK( simulation.DroppedPhysicsTickCount() == 0u );
    CHECK( simulation.PhysicsHitchEventCount() == 0u );

    input.timeScale = 5.0f;
    const SimulationTickResult boundedNormalFrame = simulation.Tick( input );
    CHECK( boundedNormalFrame.committedPhysicsTicks == 5 );
    CHECK( boundedNormalFrame.droppedPhysicsTicks == 0 );
    CHECK( simulation.DroppedPhysicsTickCount() == 0u );
    CHECK( simulation.PhysicsHitchEventCount() == 0u );
}

TEST_CASE( "SimulationSystem observes each cleared scene generation exactly once" )
{
    SimulationSystem simulation;
    SimulationTickInput input;
    input.canStepPhysics = true;
    input.isFixedStep = true;
    input.physicsAdvance = PhysicsAdvanceState::Running;
    input.timeScale = 6.0f;
    CHECK( simulation.Tick( input ).droppedPhysicsTicks == 1 );

    SceneLifecyclePacket lifecycle;
    lifecycle.generation = 1;
    lifecycle.event = SceneRuntimeLifecycleEvent::BeforeSceneUnload;
    simulation.ObserveSceneLifecycle( lifecycle );
    CHECK( simulation.DroppedPhysicsTickCount() == 1u );

    lifecycle.event = SceneRuntimeLifecycleEvent::AfterSceneCleared;
    simulation.ObserveSceneLifecycle( lifecycle );
    CHECK( simulation.DroppedPhysicsTickCount() == 0u );
    CHECK( simulation.PhysicsHitchEventCount() == 0u );

    CHECK( simulation.Tick( input ).droppedPhysicsTicks == 1 );
    lifecycle.event = SceneRuntimeLifecycleEvent::AfterSceneActivated;
    simulation.ObserveSceneLifecycle( lifecycle );
    CHECK( simulation.DroppedPhysicsTickCount() == 1u );

    lifecycle.generation = 2;
    lifecycle.event = SceneRuntimeLifecycleEvent::AfterSceneCleared;
    simulation.ObserveSceneLifecycle( lifecycle );
    CHECK( simulation.DroppedPhysicsTickCount() == 0u );
    CHECK( simulation.PhysicsHitchEventCount() == 0u );
}
