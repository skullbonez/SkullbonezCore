/*
File: SkullbonezSource/Runtime/Interaction/PhysicsAdvanceState.h
Purpose:
  Defines the detached operator policy for advancing physics.

Summary:
  Interaction selects one frame-local advance mode. Simulation consumes the
  value without borrowing the interaction controller or its retained state.

Invariants:
  - The value contains no owner pointer, callback, or device state.
  - Simulation interprets RunWhileStepHeld only with the sampled step edge.

Related:
  - SkullbonezSource/Runtime/Interaction/RuntimeInteractionController.h
  - SkullbonezSource/Runtime/Simulation/SimulationSystem.h
*/
#pragma once

namespace SkullbonezCore::Runtime
{
enum class PhysicsAdvanceState
{
    Disabled,
    Paused,
    Running,
    RunWhileStepHeld
};
} // namespace SkullbonezCore::Runtime
