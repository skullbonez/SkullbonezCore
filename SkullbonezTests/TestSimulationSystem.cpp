/*
File: SkullbonezTests/TestSimulationSystem.cpp
Purpose:
  Proves fixed-tick scheduling exposes a bounded presentation fraction without
  changing committed physics tick counts.

Summary:
  SimulationSystem owns time accumulation. Rendering may read the leftover
  fraction, but solver commits remain the same integer schedule as before.

Glossary:
  Presentation alpha: Fraction between the previous and current completed
    physics poses, derived from time left in the fixed-step accumulator.

Invariants:
  - Deterministic fixed-step scenes and paused simulation publish exact state.
  - Presentation alpha never changes the committed physics tick count.

Related:
  - SkullbonezSource/Physics/SimulationSystem.h
  - Agentic/Reports/2026-07-12/sim-render-interpolation-closure.md
*/
#include "../ThirdPtySource/doctest/doctest.h"

#include "../SkullbonezSource/Physics/PhysicsTimestep.h"
#include "../SkullbonezSource/Physics/SimulationSystem.h"

using namespace SkullbonezCore::Basics;

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
        const float presentedPose = static_cast<float>( previousPose )
            + result.presentationAlpha * static_cast<float>( currentPose - previousPose );

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
