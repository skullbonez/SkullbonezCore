/*
File: SkullbonezSource/Runtime/Simulation/SimulationSystem.cpp
Purpose:
  Owns runtime simulation stepping policy and physics accumulators.

Summary:
  This system owns wall-clock accumulation, effective render-frame lockstep,
  catch-up capping, and dropped-tick diagnostics behind one tick decision.

Invariants:
  - Render-frame lockstep ignores wall-clock accumulation and commits whole
    PHYSICS_FIXED_DT ticks from the time-scale accumulator.
  - Wall-clock scenes still run physics in fixed-size steps capped by
    PHYSICS_MAX_STEPS_PER_FRAME to avoid runaway catch-up.
  - This scheduler never touches model owners, physics stores, world forces, or
    worker pools; callers execute the returned committed tick count.

Related:
  - SkullbonezSource/Runtime/Simulation/SimulationSystem.h
  - Agentic/Reference/runtime-reference.md
  - Agentic/Reference/engine-glossary.md
*/
#include "SimulationSystem.h"

#include "../../Core/Common.h"
#include "../../Physics/PhysicsTimestep.h"

#include <algorithm>
#include <cmath>
#include <limits>

using namespace SkullbonezCore::Runtime;

namespace
{
constexpr int RENDER_FRAME_LOCKSTEP_MAX_TICKS_PER_FRAME = 5;

// Why: accumulated binary frame fractions can land a few picoseconds below an
// exact one-second boundary. This 1e-9-tick tolerance affects only the whole-
// tick comparison; the retained remainder still uses the canonical interval.
constexpr double SCHEDULER_ROUNDING_EPSILON_SECONDS = PHYSICS_FIXED_DT_SECONDS * 1.0e-9;

double FiniteNonNegative( double value ) noexcept
{
    return std::isfinite( value ) ? (std::max)( 0.0, value ) : 0.0;
}

int SaturatingWholeTickCount( double wholeTickCount ) noexcept
{
    if ( !( wholeTickCount > 0.0 ) )
    {
        return 0;
    }

    // Hazard: converting a non-finite or out-of-range floating value to int is
    // undefined. Compare in double and cast only the proven representable case.
    constexpr double maxResult = static_cast<double>( ( std::numeric_limits<int>::max )() );
    return !std::isfinite( wholeTickCount ) || wholeTickCount >= maxResult ? ( std::numeric_limits<int>::max )()
                                                                           : static_cast<int>( wholeTickCount );
}

double RetainSubTickFraction( double accumulator, double tickInterval, int requestedWholeTicks ) noexcept
{
    // Hazard: a saturated count no longer represents every whole tick. fmod
    // removes the entire finite whole-tick prefix without converting it to an
    // integer; ordinary-sized calls retain the cheaper subtraction path.
    if ( requestedWholeTicks == ( std::numeric_limits<int>::max )() )
    {
        return std::fmod( accumulator, tickInterval );
    }

    return (std::max)( 0.0, accumulator - static_cast<double>( requestedWholeTicks ) * tickInterval );
}
} // namespace

void SimulationSystem::Reset()
{
    m_physicsAccumulator = 0.0;
    m_renderFrameLockstepTickAccumulator = 0.0;
    m_droppedPhysicsTickCount = 0;
    m_physicsHitchEventCount = 0;
}

void SimulationSystem::ObserveSceneLifecycle( const SceneLifecyclePacket& packet )
{
    if ( m_sceneResetObserver.ShouldApply( packet, SceneRuntimeLifecycleEvent::AfterSceneCleared ) )
    {
        Reset();
    }
}

SimulationTickResult SimulationSystem::Tick( const SimulationTickInput& input )
{
    SimulationTickResult result;

    if ( input.isSceneMode && !input.isScenePhysicsEnabled )
    {
        m_physicsAccumulator = 0.0;
        m_renderFrameLockstepTickAccumulator = 0.0;
        return result;
    }

    result.shouldUpdateLogic = true;
    const double frameSeconds = FiniteNonNegative( input.secondsPerFrame );
    const double timeScale = FiniteNonNegative( static_cast<double>( input.timeScale ) );
    result.cameraDt = static_cast<float>( (std::min)( frameSeconds, static_cast<double>( ( std::numeric_limits<float>::max )() ) ) );

    const bool shouldStepPhysics = input.physicsAdvance == PhysicsAdvanceState::Running ||
                                   ( input.physicsAdvance == PhysicsAdvanceState::RunWhileStepHeld &&
                                     input.isStepRequested );

    const bool canStepPhysics = shouldStepPhysics && input.canStepPhysics;

    if ( input.pacingPolicy == SimulationPacingPolicy::RenderFrameLockstep )
    {
        m_physicsAccumulator = 0.0;

        if ( !canStepPhysics )
        {
            m_renderFrameLockstepTickAccumulator = 0.0;
            const double requestedSimulationDt = PHYSICS_FIXED_DT_SECONDS * timeScale;
            result.simulationDt = static_cast<float>( (std::min)( requestedSimulationDt, static_cast<double>( ( std::numeric_limits<float>::max )() ) ) );
            return result;
        }

        // Why: deterministic render-frame lockstep uses exact fixed-delta ticks
        // driven by time_scale. Wall time is deliberately irrelevant to the
        // selected render-frame-lockstep automation/capture policy.
        m_renderFrameLockstepTickAccumulator += timeScale;
        const int requestedWholeTicks = SaturatingWholeTickCount( std::floor( m_renderFrameLockstepTickAccumulator ) );
        const int ticksThisFrame = (std::min)( requestedWholeTicks, RENDER_FRAME_LOCKSTEP_MAX_TICKS_PER_FRAME );
        const int droppedTicks = requestedWholeTicks - ticksThisFrame;

        // Hazard: carrying excess whole ticks would turn one hitch into repeated
        // five-step stalls. Remove all requested whole ticks, but retain the
        // fractional remainder so ordinary time-scale cadence stays exact.
        m_renderFrameLockstepTickAccumulator = RetainSubTickFraction( m_renderFrameLockstepTickAccumulator, 1.0,
                                                                      requestedWholeTicks );

        if ( droppedTicks > 0 )
        {
            m_droppedPhysicsTickCount += static_cast<uint64_t>( droppedTicks );
            ++m_physicsHitchEventCount;
        }

        result.committedPhysicsTicks = ticksThisFrame;
        result.droppedPhysicsTicks = droppedTicks;

        result.simulationDt = PHYSICS_FIXED_DT * static_cast<float>( ticksThisFrame );
        result.presentationAlpha = 1.0f;
        return result;
    }

    m_renderFrameLockstepTickAccumulator = 0.0;
    const double unboundedScaledDt = frameSeconds * timeScale;
    const double scaledDt = std::isfinite( unboundedScaledDt ) ? unboundedScaledDt : 0.0;

    if ( canStepPhysics )
    {
        // Why: the impulse solver uses discrete overlap tests and needs small
        // fixed-timestep ticks for stability. The runtime owner executes the
        // returned count; camera and miscellaneous UI updates use one frame-level dt.
        m_physicsAccumulator += scaledDt;
        const int requestedWholeTicks = SaturatingWholeTickCount( std::floor( ( m_physicsAccumulator + SCHEDULER_ROUNDING_EPSILON_SECONDS ) / PHYSICS_FIXED_DT_SECONDS ) );
        const int ticksThisFrame = (std::min)( requestedWholeTicks, PHYSICS_MAX_STEPS_PER_FRAME );
        const int droppedTicks = requestedWholeTicks - ticksThisFrame;

        // Hazard: retain sub-tick time, but never carry capped whole ticks into
        // later frames. Carrying them would turn one slow frame into a train of
        // catch-up stalls and would obscure how much time the cap discarded.
        m_physicsAccumulator = RetainSubTickFraction( m_physicsAccumulator, PHYSICS_FIXED_DT_SECONDS, requestedWholeTicks );

        if ( droppedTicks > 0 )
        {
            m_droppedPhysicsTickCount += static_cast<uint64_t>( droppedTicks );
            ++m_physicsHitchEventCount;
        }

        result.committedPhysicsTicks = ticksThisFrame;
        result.droppedPhysicsTicks = droppedTicks;
    }
    else
    {
        m_physicsAccumulator = 0.0;
    }

    result.simulationDt = static_cast<float>( (std::min)( scaledDt, static_cast<double>( ( std::numeric_limits<float>::max )() ) ) );

    if ( canStepPhysics )
    {
        result.presentationAlpha = static_cast<float>( std::clamp( m_physicsAccumulator / PHYSICS_FIXED_DT_SECONDS, 0.0, 1.0 ) );
    }

    return result;
}

uint64_t SimulationSystem::DroppedPhysicsTickCount() const noexcept
{
    return m_droppedPhysicsTickCount;
}

uint64_t SimulationSystem::PhysicsHitchEventCount() const noexcept
{
    return m_physicsHitchEventCount;
}
