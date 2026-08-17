/*
File: PhysicsPoseIntegration.h
Purpose:
  Declares the deterministic scalar kernel that advances one hot physics pose.

Summary:
  Physics stages borrow one body row from PhysicsBodyStore and pass it through
  this owner. The kernel advances position and world-space orientation without
  depending on a processor-selected C runtime transcendental.

Invariants:
  - Angular velocity is expressed in radians per second in world space.
  - The delta quaternion left-multiplies the current orientation.
  - The orientation update is the exponential map of angular velocity held
    constant across deltaSeconds.
  - Any arithmetic change can alter byte-exact Physics baselines.

Related:
  - SkullbonezSource/Physics/PhysicsBodyStore.h
  - SkullbonezSource/Maths/DeterministicMath.h
  - SkullbonezTests/TestPhysicsPoseIntegration.cpp
  - Agentic/Reference/physics-overview.md
*/
#pragma once


namespace SkullbonezCore::Physics
{
struct PhysicsBodyHotState;

// Advances the row's position and orientation from its current velocities.
// deltaSeconds is a positive or zero duration; the caller owns step admission.
void IntegrateBodyRecordPose( PhysicsBodyHotState& hot, float deltaSeconds ) noexcept;
} // namespace SkullbonezCore::Physics
