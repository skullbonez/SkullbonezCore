/*
File: SkullbonezSource/Runtime/SimulationController.h
Purpose:
  Owns the runtime simulation stepping boundary for Run.

Mental model:
  SimulationController keeps timestep policy behind a runtime-level API while
  the physics implementation remains in Physics/SimulationSystem.

Glossary:
  Simulation tick: One runtime request to advance fixed or variable physics.
  Timestep policy: Rule that decides how wall time becomes physics steps.
  Accumulator: Stored leftover time carried between fixed-step frames.
  Facade: Small wrapper that names ownership without changing behavior.

Invariants:
  - Controller calls preserve SimulationSystem step ordering.
  - Physics ownership remains in Physics/ until a separately validated slice.

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
