/*
File: SkullbonezTests/TestSimulationSystem.cpp
Purpose:
  Pins wall-clock fixed-frequency scheduling, effective render-frame lockstep,
  and the presentation fraction between committed Physics ticks.

Summary:
  SimulationSystem owns both wall-clock accumulation and effective render-frame
  lockstep. Tests distinguish elapsed-time scheduling from frame-count-driven
  advancement and prove capped catch-up discards only whole ticks.

Glossary:
  Wall-clock scheduling: Converts elapsed render-frame time into a count of
    fixed-frequency Physics ticks.
  Lifecycle reset: Idempotent pacing reset applied after a scene generation
    reaches the cleared phase.

Invariants:
  - One wall-clock second commits the same physics time at tested uncapped render rates.
  - Explicit render-frame lockstep at unit time scale commits one fixed tick per
    rendered frame when Physics advancement is admitted.
  - Deterministic lockstep and paused simulation publish exact state.
  - Presentation alpha never changes the committed physics tick count.
  - Catch-up accounting drops whole ticks but retains fractional cadence.
  - A scene generation resets pacing at most once; later phase samples cannot
    erase work accumulated after that reset.

Related:
  - SkullbonezSource/Runtime/Simulation/SimulationSystem.h
  - Agentic/Reference/engine-glossary.md
*/
#include "../ThirdPtySource/doctest/doctest.h"

#include "../SkullbonezSource/Physics/PhysicsTimestep.h"
#include "../SkullbonezSource/Runtime/Simulation/SimulationSystem.h"

#include <cmath>
#include <limits>

using namespace SkullbonezCore::Runtime;

namespace
{
constexpr int PHYSICS_TICKS_PER_SECOND = static_cast<int>( PHYSICS_FIXED_TICKS_PER_SECOND );

int CountWallClockTicksForOneSecond( int renderedFrameCount )
{
    SimulationSystem simulation;
    SimulationTickInput input;
    input.secondsPerFrame = 1.0 / static_cast<double>( renderedFrameCount );
    input.canStepPhysics = true;
    input.pacingPolicy = SimulationPacingPolicy::WallClock;
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
    input.pacingPolicy = SimulationPacingPolicy::RenderFrameLockstep;
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
    input.secondsPerFrame = PHYSICS_FIXED_DT_SECONDS * 0.5;
    input.canStepPhysics = true;
    input.physicsAdvance = PhysicsAdvanceState::Running;

    const SimulationTickResult halfTick = simulation.Tick( input );
    CHECK( halfTick.committedPhysicsTicks == 0 );
    CHECK( halfTick.presentationAlpha == doctest::Approx( 0.5f ) );

    const SimulationTickResult boundary = simulation.Tick( input );
    CHECK( boundary.committedPhysicsTicks == 1 );
    CHECK( boundary.presentationAlpha == doctest::Approx( 0.0f ) );
}

TEST_CASE( "SimulationSystem applies scene and target availability gates before pacing" )
{
    SimulationSystem simulation;
    SimulationTickInput input;
    input.secondsPerFrame = PHYSICS_FIXED_DT_SECONDS;
    input.timeScale = 2.0f;
    input.isSceneMode = true;
    input.isScenePhysicsEnabled = false;
    input.pacingPolicy = SimulationPacingPolicy::RenderFrameLockstep;
    input.physicsAdvance = PhysicsAdvanceState::RunWhileStepHeld;
    input.isStepRequested = true;
    input.canStepPhysics = true;

    const SimulationTickResult disabledScene = simulation.Tick( input );
    CHECK_FALSE( disabledScene.shouldUpdateLogic );
    CHECK( disabledScene.committedPhysicsTicks == 0 );

    input.isScenePhysicsEnabled = true;
    input.canStepPhysics = false;
    const SimulationTickResult unavailableTarget = simulation.Tick( input );
    CHECK( unavailableTarget.shouldUpdateLogic );
    CHECK( unavailableTarget.committedPhysicsTicks == 0 );
    CHECK( unavailableTarget.simulationDt == doctest::Approx( PHYSICS_FIXED_DT * 2.0f ) );

    input.canStepPhysics = true;
    const SimulationTickResult admitted = simulation.Tick( input );
    CHECK( admitted.committedPhysicsTicks == 2 );
}

TEST_CASE( "SimulationSystem commits one wall-clock second at tested uncapped render rates" )
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

TEST_CASE( "SimulationSystem resolves scene-requested lockstep only for finite unattended capture" )
{
    CHECK( ResolveSimulationPacingPolicy( false, false, -1, false ) == SimulationPacingPolicy::WallClock );
    CHECK( ResolveSimulationPacingPolicy( false, true, -1, false ) == SimulationPacingPolicy::WallClock );
    CHECK( ResolveSimulationPacingPolicy( false, true, 0, false ) == SimulationPacingPolicy::WallClock );
    CHECK( ResolveSimulationPacingPolicy( false, true, 120, true ) == SimulationPacingPolicy::WallClock );
    CHECK( ResolveSimulationPacingPolicy( false, true, 120, false ) == SimulationPacingPolicy::RenderFrameLockstep );
    CHECK( ResolveSimulationPacingPolicy( false, false, -1, false ) ==
           ResolveSimulationPacingPolicy( false, true, -1, false ) );
    CHECK( ResolveSimulationPacingPolicy( false, false, 120, false ) !=
           ResolveSimulationPacingPolicy( false, true, 120, false ) );

    // Invariant: explicit startup intent wins even for an interactive,
    // unlimited scene; it is the opt-in deterministic automation seam.
    CHECK( ResolveSimulationPacingPolicy( true, false, -1, true ) == SimulationPacingPolicy::RenderFrameLockstep );
}

TEST_CASE( "SimulationSystem routes an unlimited scene request through wall-clock scheduling" )
{
    SimulationSystem simulation;
    SimulationTickInput input;
    input.secondsPerFrame = 1.0 / 300.0;
    input.canStepPhysics = true;
    input.pacingPolicy = ResolveSimulationPacingPolicy( false, true, -1, false );
    input.physicsAdvance = PhysicsAdvanceState::Running;

    int committedTicks = 0;

    for ( int frame = 0; frame < 300; ++frame )
    {
        committedTicks += simulation.Tick( input ).committedPhysicsTicks;
    }

    // Mutation control: the retired scene-request-implies-lockstep rule would
    // commit 300 ticks here instead of one elapsed second's 120 ticks.
    CHECK( committedTicks == PHYSICS_TICKS_PER_SECOND );
    CHECK( committedTicks != 300 );
}

TEST_CASE( "SimulationSystem pause and step-held paths preserve the selected pacing policy" )
{
    SimulationSystem simulation;
    SimulationTickInput input;
    input.secondsPerFrame = 1.0 / 500.0;
    input.canStepPhysics = true;
    input.pacingPolicy = SimulationPacingPolicy::WallClock;
    input.physicsAdvance = PhysicsAdvanceState::RunWhileStepHeld;

    const SimulationTickResult paused = simulation.Tick( input );
    CHECK( paused.committedPhysicsTicks == 0 );
    CHECK( paused.presentationAlpha == doctest::Approx( 1.0f ) );

    input.isStepRequested = true;
    int wallClockTicks = 0;

    for ( int frame = 0; frame < 500; ++frame )
    {
        wallClockTicks += simulation.Tick( input ).committedPhysicsTicks;
    }

    CHECK( wallClockTicks == PHYSICS_TICKS_PER_SECOND );

    input.isStepRequested = false;
    input.pacingPolicy = SimulationPacingPolicy::RenderFrameLockstep;
    CHECK( simulation.Tick( input ).committedPhysicsTicks == 0 );
    input.isStepRequested = true;
    CHECK( simulation.Tick( input ).committedPhysicsTicks == 1 );
}

TEST_CASE( "SimulationSystem pins deterministic and paused presentation to current state" )
{
    SimulationSystem simulation;
    SimulationTickInput input;
    input.secondsPerFrame = 1.0 / 144.0;
    input.canStepPhysics = true;
    input.pacingPolicy = SimulationPacingPolicy::RenderFrameLockstep;
    input.physicsAdvance = PhysicsAdvanceState::Running;

    const SimulationTickResult deterministic = simulation.Tick( input );
    CHECK( deterministic.presentationAlpha == doctest::Approx( 1.0f ) );

    input.pacingPolicy = SimulationPacingPolicy::WallClock;
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
            // Concept: the interpolated point advances by 120/144 of a solver
            // unit on every display frame; authoritative poses still advance in ints.
            CHECK( presentedPose - previousPresentedPose == doctest::Approx( 120.0f / 144.0f ) );
        }
        previousPresentedPose = presentedPose;
    }
    // Invariant: at this uncapped 144 Hz cadence, one wall-clock second commits
    // all 120 fixed Physics ticks; interpolation changes only presentation.
    CHECK( currentPose == PHYSICS_TICKS_PER_SECOND );
}

TEST_CASE( "SimulationSystem wall-clock hitch drops whole ticks and carries only the fraction" )
{
    SimulationSystem simulation;
    SimulationTickInput input;
    input.canStepPhysics = true;
    input.pacingPolicy = SimulationPacingPolicy::WallClock;
    input.physicsAdvance = PhysicsAdvanceState::Running;

    constexpr int excessWholeTicks = 3;
    input.secondsPerFrame = PHYSICS_FIXED_DT_SECONDS *
                            ( static_cast<double>( PHYSICS_MAX_STEPS_PER_FRAME + excessWholeTicks ) + 0.75 );

    const SimulationTickResult hitch = simulation.Tick( input );
    CHECK( hitch.committedPhysicsTicks <= PHYSICS_MAX_STEPS_PER_FRAME );
    CHECK( hitch.committedPhysicsTicks == PHYSICS_MAX_STEPS_PER_FRAME );
    CHECK( hitch.droppedPhysicsTicks == excessWholeTicks );
    CHECK( simulation.DroppedPhysicsTickCount() == static_cast<uint64_t>( excessWholeTicks ) );
    CHECK( simulation.PhysicsHitchEventCount() == 1u );

    // Invariant: the cap discards the three excess whole ticks. The retained
    // 0.75 tick combines with the next quarter tick instead of replaying stale catch-up.
    input.secondsPerFrame = PHYSICS_FIXED_DT_SECONDS * 0.25;
    const SimulationTickResult fractionalCarry = simulation.Tick( input );
    CHECK( fractionalCarry.committedPhysicsTicks == 1 );
    CHECK( fractionalCarry.droppedPhysicsTicks == 0 );
    CHECK( fractionalCarry.presentationAlpha == doctest::Approx( 0.0f ) );
    CHECK( simulation.DroppedPhysicsTickCount() == static_cast<uint64_t>( excessWholeTicks ) );
    CHECK( simulation.PhysicsHitchEventCount() == 1u );
}

TEST_CASE( "SimulationSystem bounds enormous and non-finite scheduling inputs" )
{
    SimulationSystem simulation;
    SimulationTickInput input;
    input.canStepPhysics = true;
    input.pacingPolicy = SimulationPacingPolicy::WallClock;
    input.physicsAdvance = PhysicsAdvanceState::Running;

    input.secondsPerFrame = PHYSICS_FIXED_DT_SECONDS *
                            ( static_cast<double>( (std::numeric_limits<int>::max)() ) + 0.5 );
    const SimulationTickResult enormousWallClock = simulation.Tick( input );
    CHECK( enormousWallClock.committedPhysicsTicks == PHYSICS_MAX_STEPS_PER_FRAME );
    CHECK( enormousWallClock.droppedPhysicsTicks ==
           (std::numeric_limits<int>::max)() - PHYSICS_MAX_STEPS_PER_FRAME );

    input.secondsPerFrame = PHYSICS_FIXED_DT_SECONDS * 0.5;
    CHECK( simulation.Tick( input ).committedPhysicsTicks == 1 );

    simulation.Reset();
    input.secondsPerFrame = (std::numeric_limits<double>::infinity)();
    const SimulationTickResult nonFiniteWallClock = simulation.Tick( input );
    CHECK( nonFiniteWallClock.committedPhysicsTicks == 0 );
    CHECK( nonFiniteWallClock.droppedPhysicsTicks == 0 );
    CHECK( std::isfinite( nonFiniteWallClock.simulationDt ) );
    CHECK( std::isfinite( nonFiniteWallClock.cameraDt ) );

    simulation.Reset();
    input.secondsPerFrame = 1.0 / 60.0;
    input.pacingPolicy = SimulationPacingPolicy::RenderFrameLockstep;
    input.timeScale = (std::numeric_limits<float>::max)();
    const SimulationTickResult enormousLockstep = simulation.Tick( input );
    CHECK( enormousLockstep.committedPhysicsTicks == 5 );
    CHECK( enormousLockstep.droppedPhysicsTicks == (std::numeric_limits<int>::max)() - 5 );

    simulation.Reset();
    input.timeScale = (std::numeric_limits<float>::infinity)();
    const SimulationTickResult nonFiniteLockstep = simulation.Tick( input );
    CHECK( nonFiniteLockstep.committedPhysicsTicks == 0 );
    CHECK( nonFiniteLockstep.droppedPhysicsTicks == 0 );
    CHECK( std::isfinite( nonFiniteLockstep.simulationDt ) );
}

TEST_CASE( "SimulationSystem drops excess render-frame lockstep ticks and retains the fraction" )
{
    SimulationSystem simulation;
    SimulationTickInput input;
    input.canStepPhysics = true;
    input.pacingPolicy = SimulationPacingPolicy::RenderFrameLockstep;
    input.physicsAdvance = PhysicsAdvanceState::Running;
    input.timeScale = 12.75f;

    const SimulationTickResult hitch = simulation.Tick( input );
    CHECK( hitch.committedPhysicsTicks == 5 );
    CHECK( hitch.droppedPhysicsTicks == 7 );
    CHECK( hitch.simulationDt == doctest::Approx( PHYSICS_FIXED_DT * 5.0f ) );
    CHECK( simulation.DroppedPhysicsTickCount() == 7u );
    CHECK( simulation.PhysicsHitchEventCount() == 1u );

    // Invariant: the seven dropped whole ticks never reappear. Only the retained
    // 0.75 fraction combines with the next quarter-tick request.
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
    input.pacingPolicy = SimulationPacingPolicy::RenderFrameLockstep;
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
    input.pacingPolicy = SimulationPacingPolicy::RenderFrameLockstep;
    input.physicsAdvance = PhysicsAdvanceState::Running;
    input.timeScale = 6.0f;
    CHECK( simulation.Tick( input ).droppedPhysicsTicks == 1 );

    uint64_t generation = 1;
    simulation.ObserveSceneLifecycle( generation, false );
    CHECK( simulation.DroppedPhysicsTickCount() == 1u );

    simulation.ObserveSceneLifecycle( generation, true );
    CHECK( simulation.DroppedPhysicsTickCount() == 0u );
    CHECK( simulation.PhysicsHitchEventCount() == 0u );

    CHECK( simulation.Tick( input ).droppedPhysicsTicks == 1 );
    simulation.ObserveSceneLifecycle( generation, true );
    CHECK( simulation.DroppedPhysicsTickCount() == 1u );

    generation = 2;
    simulation.ObserveSceneLifecycle( generation, true );
    CHECK( simulation.DroppedPhysicsTickCount() == 0u );
    CHECK( simulation.PhysicsHitchEventCount() == 0u );
}
