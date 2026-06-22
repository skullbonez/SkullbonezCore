/*
File: SkullbonezSource/Runtime/SimulationController.h
Purpose:
  Owns the runtime simulation stepping boundary for Run.

Mental model:
  SimulationController keeps timestep policy behind a runtime-level API while
  the physics implementation remains in Physics/SimulationSystem.

Related:
  - SkullbonezSource/Physics/SimulationSystem.h
  - SkullbonezSource/Runtime/RunFrame.cpp
*/
#pragma once

#include "../Physics/SimulationSystem.h"

namespace SkullbonezCore
{
namespace Basics
{
class SimulationController
{
  public:
    void Reset();
    SimulationTickResult Tick( const SimulationTickInput& input );

    SimulationSystem& System();
    const SimulationSystem& System() const;

  private:
    SimulationSystem m_system; // Fixed/variable-step accumulator owner
};
} // namespace Basics
} // namespace SkullbonezCore
