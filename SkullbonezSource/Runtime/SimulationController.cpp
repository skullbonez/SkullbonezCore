/*
File: SkullbonezSource/Runtime/SimulationController.cpp
Purpose:
  Implements the runtime simulation controller.

Mental model:
  This is a behavior-preserving facade over SimulationSystem. It gives Run a
  runtime-owned seam before solver ownership moves into PhysicsScene.
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
