/*
File: SkullbonezSource/Runtime/Simulation/SimulationSystem.cpp
Purpose:
  Owns runtime simulation stepping policy and physics accumulators.

Summary:
  This system preserves the old Run stepping rules while moving the
  accumulator state and tick decision into one owner.

Invariants:
  - Fixed-step mode ignores wall-clock accumulation and commits whole
    PHYSICS_FIXED_DT ticks from the time-scale accumulator.
  - Variable-time scenes still run physics in fixed-size steps capped by
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

using namespace SkullbonezCore::Runtime;

namespace
{
constexpr int FIXED_STEP_TIME_SCALE_MAX_TICKS_PER_FRAME = 5;
}

void SimulationSystem::Reset()
{
    m_physicsAccumulator = 0.0f;
    m_fixedStepTickAccumulator = 0.0f;
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
        m_physicsAccumulator = 0.0f;
        m_fixedStepTickAccumulator = 0.0f;
        return result;
    }

    result.shouldUpdateLogic = true;
    result.cameraDt = static_cast<float>( input.secondsPerFrame );

    const bool shouldStepPhysics = input.physicsAdvance == PhysicsAdvanceState::Running ||
                                   ( input.physicsAdvance == PhysicsAdvanceState::RunWhileStepHeld &&
                                     input.isStepRequested );

    const bool canStepPhysics = shouldStepPhysics && input.canStepPhysics;

    if ( input.isFixedStep )
    {

        if ( !canStepPhysics )
        {
            m_physicsAccumulator = 0.0f;
            m_fixedStepTickAccumulator = 0.0f;
            result.simulationDt = PHYSICS_FIXED_DT * (std::max)( 0.0f, input.timeScale );
            return result;
        }

        // Deterministic lock-step: exact fixed-delta ticks driven by time_scale.
        // This ignores wall-clock time so fixed-step scenes reproduce exactly.
        m_fixedStepTickAccumulator += (std::max)( 0.0f, input.timeScale );
        const int requestedWholeTicks = static_cast<int>( std::floor( m_fixedStepTickAccumulator ) );
        const int ticksThisFrame = (std::min)( requestedWholeTicks, FIXED_STEP_TIME_SCALE_MAX_TICKS_PER_FRAME );
        const int droppedTicks = requestedWholeTicks - ticksThisFrame;

        // Hazard: carrying excess whole ticks would turn one hitch into repeated
        // five-step stalls. Remove all requested whole ticks, but retain the
        // fractional remainder so ordinary time-scale cadence stays exact.
        m_fixedStepTickAccumulator -= static_cast<float>( requestedWholeTicks );

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

    const float scaledDt = static_cast<float>( input.secondsPerFrame ) * input.timeScale;

    if ( canStepPhysics )
    {

        // The impulse solver uses discrete overlap tests and needs small fixed
        // steps for stability. The runtime owner executes the returned count;
        // camera and miscellaneous UI updates use one frame-level dt.
        m_physicsAccumulator += scaledDt;

        int steps = 0;

        while ( m_physicsAccumulator >= PHYSICS_FIXED_DT && steps < PHYSICS_MAX_STEPS_PER_FRAME )
        {
            m_physicsAccumulator -= PHYSICS_FIXED_DT;
            ++steps;
        }

        result.committedPhysicsTicks = steps;

        if ( steps == PHYSICS_MAX_STEPS_PER_FRAME )
        {
            m_physicsAccumulator = 0.0f;
        }
    }
    else
    {
        m_physicsAccumulator = 0.0f;
        m_fixedStepTickAccumulator = 0.0f;
    }

    result.simulationDt = scaledDt;

    if ( canStepPhysics )
    {
        result.presentationAlpha = std::clamp( m_physicsAccumulator / PHYSICS_FIXED_DT, 0.0f, 1.0f );
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
