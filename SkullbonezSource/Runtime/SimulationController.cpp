/*
File: SkullbonezSource/Runtime/SimulationController.cpp
Purpose:
  Implements the runtime simulation controller.

Mental model:
  This is a behavior-preserving facade over SimulationSystem. It gives Run a
  runtime-owned boundary before solver ownership moves into PhysicsScene.

Glossary:
  Simulation tick: One runtime request to advance fixed or variable physics.
  Timestep policy: Rule that decides how wall time becomes physics steps.
  Facade: Small wrapper that names ownership without changing behavior.

Invariants:
  - Tick delegates to SimulationSystem without changing step math.
  - Reset clears accumulated timestep state through the existing owner.

Related:
  - SkullbonezSource/Runtime/SimulationController.h
  - SkullbonezSource/Physics/SimulationSystem.h
*/
#include "SimulationController.h"

namespace SkullbonezCore
{
namespace Basics
{
void SimulationController::Reset()
{
    m_system.Reset();
}


SimulationTickResult SimulationController::Tick( const SimulationTickInput& input )
{
    return m_system.Tick( input );
}


SimulationSystem& SimulationController::System()
{
    return m_system;
}


const SimulationSystem& SimulationController::System() const
{
    return m_system;
}
} // namespace Basics
} // namespace SkullbonezCore
