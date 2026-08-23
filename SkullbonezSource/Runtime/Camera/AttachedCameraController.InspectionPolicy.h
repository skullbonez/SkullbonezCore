/*
File: SkullbonezSource/Runtime/Camera/AttachedCameraController.InspectionPolicy.h
Purpose:
  Declares the stateless pose policy used by attached and replay-focused cameras.

Summary:
  AttachedCameraController remains the sole retained target and suspension owner.
  These functions transform its borrowed state, a visible pose, and one target
  snapshot into the next pose command so focused inspection behavior is testable
  without constructing the engine-loop SceneWorld host.

Invariants:
  - Seeding uses the visible render pose, not a hidden destination pose.
  - Exactly the first valid solve consumes needsEntryTween.
  - A moved target retargets the command without resetting transition progress.

Related:
  - SkullbonezSource/Runtime/Camera/AttachedCameraValues.h
  - SkullbonezSource/Runtime/Scene/AttachedCameraController.cpp
  - SkullbonezTests/TestCamera.cpp
*/
#pragma once

#include "AttachedCameraValues.h"

namespace SkullbonezCore::Runtime
{
void CaptureAttachedCameraOrbit( AttachedCameraState& state, const AttachedCameraPose& currentPose,
                                 const AttachedCameraPhysicsTarget& target );
void SeedAttachedCameraFixedRelative( AttachedCameraState& state, const AttachedCameraPose& currentPose,
                                      const AttachedCameraPhysicsTarget& target );
bool ApplyAttachedCameraOrbitWheel( AttachedCameraState& state, const AttachedCameraPhysicsTarget& target,
                                    int unhandledWheelDelta );
bool BuildAttachedCameraOrbitPose( AttachedCameraState& state, const AttachedCameraPhysicsTarget& target,
                                   const AttachedCameraPose& currentPose, float orbitYawDelta, float orbitPitchDelta,
                                   AttachedCameraPoseCommand& outCommand );
} // namespace SkullbonezCore::Runtime
