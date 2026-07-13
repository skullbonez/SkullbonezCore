/*
File: SkullbonezSource/Physics/SimulationSystem.cpp
Purpose:
  Owns runtime simulation stepping policy and physics accumulators.

Summary:
  This system preserves the old Run stepping rules while moving the
  accumulator state and tick decision into one owner.

Glossary:
  Fixed-step: Deterministic mode that advances physics by one fixed delta per
  requested tick instead of wall-clock time.
  Accumulator: Stored fractional tick state that carries time across frames.
  Commit count: Number of fixed physics ticks the runtime owner must execute
    after accumulator state is updated.

Invariants:
  - Fixed-step mode ignores wall-clock accumulation and commits whole
    PHYSICS_FIXED_DT ticks from the time-scale accumulator.
  - Variable-time scenes still run physics in fixed-size steps capped by
    PHYSICS_MAX_STEPS_PER_FRAME to avoid runaway catch-up.
  - This scheduler never touches model owners, physics stores, world forces, or
    worker pools; callers execute the returned committed tick count.

Related:
  - SkullbonezSource/Physics/SimulationSystem.h
  - Agentic/Reference/runtime-reference.md
*/
#include "SimulationSystem.h"
#include "PhysicsTimestep.h"

#include "../Core/Common.h"

#include <algorithm>
#include <cmath>

using namespace SkullbonezCore::Runtime;

namespace
{
constexpr int FIXED_STEP_TIME_SCALE_MAX_TICKS_PER_FRAME = 32;
}

void SimulationSystem::Reset()
{
    m_physicsAccumulator = 0.0f;
    m_fixedStepTickAccumulator = 0.0f;
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

    const bool shouldStepPhysics =
        input.physicsAdvance == PhysicsAdvanceState::Running ||
        ( input.physicsAdvance == PhysicsAdvanceState::RunWhileStepHeld && input.isStepRequested );
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
        const int ticksThisFrame = (std::min)( static_cast<int>( std::floor( m_fixedStepTickAccumulator ) ),
                                               FIXED_STEP_TIME_SCALE_MAX_TICKS_PER_FRAME );
        // Hazard: enormous time_scale values must not create unbounded
        // validation frames. Leftover fractional ticks remain deterministic.
        m_fixedStepTickAccumulator -= static_cast<float>( ticksThisFrame );

        result.committedPhysicsTicks = ticksThisFrame;

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
