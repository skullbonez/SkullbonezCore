/*
File: SkullbonezSource/Runtime/AttachedCameraController.h
Purpose:
  Owns attach-camera target identity, orbit state, and follow-pose solving.

Mental model:
  Attach mode follows a physics body through stable body/collider handles, then
  turns the current body snapshot and camera pose into the next camera pose.
  Run applies the resulting pose to CameraCollection because it still owns
  camera lifetime and tween side effects.

Glossary:
  Attach target: Physics body/collider identity plus replay id used to recover a
    followed model after dense rows move.
  Target snapshot: One-frame position, velocity, rotation, and broad radius read
    from physics stores.
  Pose command: Solved camera eye/view/up plus whether entry tweening should
    start for this solve.

Invariants:
  - Target recovery uses physics-store handles and replay ids before dense model
    indices.
  - The controller does not store borrowed collection or camera pointers.
  - Pose commands are finite and never point eye and view at the same point.

Related:
  - SkullbonezSource/Runtime/RunInput.cpp
  - SkullbonezSource/Runtime/RunState.h
  - Agentic/Plans/In_Progress/authoritative-plan-01-run-composition-root.csv
*/
#pragma once

#include "RunState.h"
#include "../Maths/RotationMatrix.h"
#include "../Maths/Vector3.h"

namespace SkullbonezCore
{
namespace GameObjects
{
class GameModelCollection;
}

namespace Basics
{
struct AttachedCameraPose
{
    Math::Vector::Vector3 eye = Math::Vector::ZERO_VECTOR;
    Math::Vector::Vector3 view = Math::Vector::Vector3( 0.0f, 0.0f, 1.0f );
    Math::Vector::Vector3 up = Math::Vector::Vector3( 0.0f, 1.0f, 0.0f );
};

struct AttachedCameraPhysicsTarget
{
    Math::Vector::Vector3 position = Math::Vector::ZERO_VECTOR;
    Math::Vector::Vector3 linearVelocity = Math::Vector::ZERO_VECTOR;
    Math::Transformation::RotationMatrix rotation;
    float radius = 1.0f;
};

struct AttachedCameraPoseCommand
{
    AttachedCameraPose pose;
    bool startEntryTween = false;
};

class AttachedCameraController
{
  public:
    static void ClearTarget( AttachedCameraState& state );
    static bool TryAttachTargetHandlesFromModelIndex( const GameObjects::GameModelCollection& collection,
                                                      int modelIndex,
                                                      AttachedCameraTarget& target );
    static bool TryResolveTargetIdentity( const GameObjects::GameModelCollection& collection,
                                          AttachedCameraTarget& target,
                                          int& outModelIndex );
    static bool TryResolvePhysicsTarget( const GameObjects::GameModelCollection& collection,
                                         AttachedCameraTarget& target,
                                         AttachedCameraPhysicsTarget& outTarget,
                                         int* outModelIndex = nullptr );
    static bool TryResolveRagdollHead( const GameObjects::GameModelCollection& collection,
                                       int selectedModelIndex,
                                       int& outHeadModelIndex );

    static void CaptureFixedOffset( AttachedCameraState& state,
                                    const AttachedCameraPose& currentPose,
                                    const AttachedCameraPhysicsTarget& target );
    static void CaptureOrbit( AttachedCameraState& state,
                              const AttachedCameraPose& currentPose,
                              const AttachedCameraPhysicsTarget& target );
    static bool
    ApplyOrbitWheel( AttachedCameraState& state, const AttachedCameraPhysicsTarget& target, int unhandledWheelDelta );
    static bool BuildFollowPose( const GameObjects::GameModelCollection& collection,
                                 AttachedCameraState& state,
                                 const AttachedCameraPhysicsTarget& target,
                                 int modelIndex,
                                 const AttachedCameraPose& currentPose,
                                 float orbitYawDelta,
                                 float orbitPitchDelta,
                                 AttachedCameraPoseCommand& outCommand );
};
} // namespace Basics
} // namespace SkullbonezCore
