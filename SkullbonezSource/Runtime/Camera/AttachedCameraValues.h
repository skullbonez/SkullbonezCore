/*
File: SkullbonezSource/Runtime/Camera/AttachedCameraValues.h
Purpose:
  Defines retained attach-camera intent and detached pose/target values.

Summary:
  Camera owns the target identity, orbit intent, and pose commands that cross
  the Scene application boundary. Scene resolves these values against its live
  stores without retaining store or camera borrows in the Camera package.

Invariants:
  - Model rows are revalidated hints; handles and scene object ids own identity.
  - Pose commands contain values only and never retain a SceneWorld borrow.

Related:
  - SkullbonezSource/Runtime/Scene/AttachedCameraController.h
  - SkullbonezSource/Runtime/Camera/AttachedCameraController.InspectionPolicy.h
*/
#pragma once

#include "../../Assets/AssetKeys.h"
#include "../../Maths/RotationMatrix.h"
#include "../../Maths/Vector3.h"
#include "../../Physics/PhysicsHandles.h"
#include "RuntimeCameraMode.h"

#include <cstdint>

namespace SkullbonezCore::Runtime
{
enum class AttachedCameraSubmode
{
    FixedRelative,
    VelocityForward,
    RagdollEyes,
    Count
};

struct AttachedCameraTarget
{
    Physics::PhysicsBodyHandle body;
    Physics::PhysicsColliderHandle collider;
    Physics::ModelRowHint modelRow;
    Physics::PhysicsSceneObjectId sceneObjectId;
    char name[64] = {};
};

struct AttachedCameraState
{
    AttachedCameraTarget target;
    AttachedCameraSubmode submode = AttachedCameraSubmode::FixedRelative;
    bool activeFollow = true;
    bool hasFixedOffset = false;
    bool hasOrbit = false;
    bool hasLastLookDirection = false;
    bool hasReturnCameraPose = false;
    bool needsEntryTween = false;
    RunCameraMode returnMode = RunCameraMode::Inspect;
    uint32_t returnCameraHash = CAMERA_FREE;
    float orbitYawRadians = 0.0f;
    float orbitPitchRadians = 0.30f;
    float orbitDistance = 8.0f;
    Math::Vector::Vector3 localEyeOffset = Math::Vector::ZERO_VECTOR;
    Math::Vector::Vector3 localViewOffset = Math::Vector::Vector3( 0.0f, 0.0f, 1.0f );
    Math::Vector::Vector3 localUp = Math::Vector::Vector3( 0.0f, 1.0f, 0.0f );
    Math::Vector::Vector3 lastLookDirection = Math::Vector::Vector3( 0.0f, 0.0f, 1.0f );
    Math::Vector::Vector3 returnEye = Math::Vector::ZERO_VECTOR;
    Math::Vector::Vector3 returnView = Math::Vector::Vector3( 0.0f, 0.0f, 1.0f );
    Math::Vector::Vector3 returnUp = Math::Vector::Vector3( 0.0f, 1.0f, 0.0f );
};

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

struct AttachedCameraTargetSelection
{
    AttachedCameraPhysicsTarget physics;
    Physics::PhysicsBodyHandle body;
    Physics::PhysicsColliderHandle collider;
    Physics::ModelRowHint modelRow;
};

enum class AttachedCameraSeedResult
{
    Failed,
    ReusedTarget,
    SelectedSeed,
    NoSeed
};
} // namespace SkullbonezCore::Runtime
