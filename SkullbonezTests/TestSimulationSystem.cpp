/*
File: SkullbonezTests/TestSimulationSystem.cpp
Purpose:
  Proves fixed-tick scheduling exposes a bounded presentation fraction without
  changing committed physics tick counts.

Summary:
  SimulationSystem owns time accumulation. Rendering may read the leftover
  fraction, while hitch tests prove excess whole ticks are dropped visibly.

Glossary:
  Presentation alpha: Fraction between the previous and current completed
    physics poses, derived from time left in the fixed-step accumulator.
  Hitch event: One fixed-step scheduling call that requests more whole ticks
    than the five-tick catch-up cap.

Invariants:
  - Deterministic fixed-step scenes and paused simulation publish exact state.
  - Presentation alpha never changes the committed physics tick count.
  - Catch-up accounting drops whole ticks but retains fractional cadence.

Related:
  - SkullbonezSource/Runtime/SimulationSystem.h
  - Agentic/Reports/2026-07-12/sim-render-interpolation-closure.md
*/
#include "../ThirdPtySource/doctest/doctest.h"

#include "../SkullbonezSource/Physics/PhysicsTimestep.h"
#include "../SkullbonezSource/Runtime/SimulationSystem.h"

using namespace SkullbonezCore::Runtime;

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
    // Retain the scheduler's existing float-accumulator boundary behavior;
    // interpolation changes only presentation, never committed tick count.
    CHECK( currentPose == 119 );
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
