/*
File: SkullbonezSource/SkullbonezSimulationSystem.cpp
Purpose:
  Owns runtime simulation stepping policy and physics accumulators.

Mental model:
  This system preserves the old SkullbonezRun stepping rules while moving the
  accumulator state and tick decision into one owner.

Glossary:
  Fixed-step: Deterministic mode that advances physics by one fixed delta per
  requested tick instead of wall-clock time.
  Accumulator: Stored fractional tick state that carries time across frames.
  UI (User Interface): In-engine diagnostic and control overlay that can pause,
  nudge, or step simulation.

Related:
  - SkullbonezSource/SkullbonezSimulationSystem.h
  - Agentic/Reference/runtime-reference.md
*/
#include "SkullbonezSimulationSystem.h"

#include "SkullbonezCommon.h"
#include "SkullbonezGameModelCollection.h"
#include "SkullbonezProfiler.h"

#include <algorithm>
#include <cmath>

using namespace SkullbonezCore::Basics;

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
        return result;
    }

    result.shouldUpdateLogic = true;
    result.cameraDt = static_cast<float>( input.secondsPerFrame );

    const bool shouldStepPhysics = !input.isFlyMode || input.isNudgeMode || input.isStepRequested;
    if ( input.isFixedStep )
    {
        // Deterministic lock-step: exact fixed-delta ticks driven by time_scale.
        // This ignores wall-clock time so fixed-step scenes reproduce exactly.
        m_fixedStepTickAccumulator += (std::max)( 0.0f, input.timeScale );
        const int ticksThisFrame = (std::min)( static_cast<int>( std::floor( m_fixedStepTickAccumulator ) ), FIXED_STEP_TIME_SCALE_MAX_TICKS_PER_FRAME );
        m_fixedStepTickAccumulator -= static_cast<float>( ticksThisFrame );

        if ( shouldStepPhysics && input.models )
        {
            PROFILE_BEGIN( "Frame/Physics" );
            for ( int tick = 0; tick < ticksThisFrame; ++tick )
            {
                PROFILE_SCOPED( "Frame/Physics/Step" );
                input.models->RunPhysics( PHYSICS_FIXED_DT );
            }
            PROFILE_END( "Frame/Physics" );
        }

        result.simulationDt = PHYSICS_FIXED_DT * static_cast<float>( ticksThisFrame );
        return result;
    }

    const float scaledDt = static_cast<float>( input.secondsPerFrame ) * input.timeScale;
    if ( shouldStepPhysics && input.models )
    {
        PROFILE_BEGIN( "Frame/Physics" );
        // The impulse solver uses discrete overlap tests and needs small fixed
        // steps for stability. Only RunPhysics runs in this loop; camera and
        // miscellaneous UI updates use one frame-level dt from the result.
        m_physicsAccumulator += scaledDt;

        int steps = 0;
        while ( m_physicsAccumulator >= PHYSICS_FIXED_DT && steps < PHYSICS_MAX_STEPS_PER_FRAME )
        {
            PROFILE_SCOPED( "Frame/Physics/Step" );
            input.models->RunPhysics( PHYSICS_FIXED_DT );
            m_physicsAccumulator -= PHYSICS_FIXED_DT;
            ++steps;
        }

        if ( steps == PHYSICS_MAX_STEPS_PER_FRAME )
        {
            m_physicsAccumulator = 0.0f;
        }
        PROFILE_END( "Frame/Physics" );
    }

    result.simulationDt = scaledDt;
    return result;
}
