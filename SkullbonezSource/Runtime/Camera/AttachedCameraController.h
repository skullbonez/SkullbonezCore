/*
File: SkullbonezSource/Runtime/Camera/AttachedCameraController.h
Purpose:
  Owns attach-camera target identity, orbit state, and follow-pose solving.

Summary:
  Attach mode selects and follows a physics body through stable body/collider
  handles, then turns the current body snapshot and camera pose into the next
  camera pose. AttachedCameraController owns that durable state and performs
  synchronous commands through borrowed model stores and cameras.

Glossary:
  Attach target: Physics body/collider identity plus scene object id used to recover a
    followed model after dense rows move.
  Target snapshot: One-frame position, velocity, rotation, and broad radius read
    from physics stores.
  Pose command: Solved camera eye/view/up plus whether entry tweening should
    start for this solve.
  Lifecycle generation: Identity of one accepted scene-load attempt, used to
    reset attach state once even when the load later fails.

Invariants:
  - Target recovery uses physics-store handles and scene object ids; a dense row is
    retained only as a typed, revalidated cache.
  - The controller does not store borrowed collection or camera pointers.
  - Pose commands are finite and never point eye and view at the same point.
  - Scene clearing invalidates the attached target at most once per lifecycle
    generation.

Related:
  - SkullbonezSource/Runtime/App/InputRouter.Interactions.cpp
  - SkullbonezSource/Runtime/App/Run.h
*/
#pragma once

#include "../../Assets/AssetKeys.h"
#include "../../Maths/RotationMatrix.h"
#include "../../Maths/Vector3.h"
#include "../../Physics/PhysicsHandles.h"
#include "RuntimeCameraMode.h"
#include "../Scene/SceneLifecycle.h"

#include <cstdint>

namespace SkullbonezCore
{
namespace Runtime
{
class SceneWorld;
}
namespace Environment
{
class CameraCollection;
}

namespace Runtime
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
    Physics::PhysicsBodyHandle body;                   // Primary live physics identity for follow/orbit sampling.
    Physics::PhysicsColliderHandle collider;           // Shape/radius identity paired with body.
    Physics::ModelRowHint modelRow;                    // Cache only; body/scene object id remain authoritative.
    Physics::PhysicsSceneObjectId sceneObjectId;       // Stable scene-local identity used to recover stale indices.
    char name[64] = {};                                // Human/debug fallback when scene object id cannot recover the target.
};

struct AttachedCameraState
{
    AttachedCameraTarget target;                       // Camera-owned target; replay/editor selections are only seeds.
    AttachedCameraSubmode submode = AttachedCameraSubmode::FixedRelative;
    bool activeFollow = true;                          // false means pinned in world space with mouse released to UI.
    bool hasFixedOffset = false;
    bool hasOrbit = false;
    bool hasLastLookDirection = false;
    bool hasReturnCameraPose = false;
    bool needsEntryTween = false;                      // Next valid follow solve should glide from the visible pose.
    RunCameraMode returnMode = RunCameraMode::Inspect; // Logical workspace restored when Attach exits.
    uint32_t returnCameraHash = CAMERA_FREE;           // Selected slot Attach should restore before applying returnEye/view/up.
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
    AttachedCameraPhysicsTarget physics;               // Snapshot used to capture the initial camera-relative offset.
    Physics::PhysicsBodyHandle body;                   // Exact selected body identity published to interaction composition.
    Physics::PhysicsColliderHandle collider;           // Collider paired with body in the same store snapshot.
    Physics::ModelRowHint modelRow;                    // Dense row valid for this synchronous command only.
};

enum class AttachedCameraSeedResult
{
    Failed,                                            // A supplied seed no longer resolved; no presentation effect should be published.
    ReusedTarget,                                      // Existing stable identity was refreshed without changing editor selection.
    SelectedSeed,                                      // The supplied replay/editor hint became the Attach target.
    NoSeed                                             // Attach remains active and waits for a world click.
};

class AttachedCameraController
{
  public:
    AttachedCameraState& State();
    const AttachedCameraState& State() const;
    // Lifetime: returns thread-local presentation text valid until the next
    // ModeLabel call on the same thread; callers must not retain the pointer.
    const char* ModeLabel() const;
    void CaptureReturnState( RunCameraMode previousMode, Environment::CameraCollection& cameras );
    void RestoreReturnState( Environment::CameraCollection& cameras );
    bool ResolveTargetIdentity( const Runtime::SceneWorld& collection, int& outModelIndex );
    // Lifetime: attached-camera operations borrow one SceneWorld and derive its
    // camera collection locally, preventing callers from pairing mismatched owners.
    bool
    TickFollow( Runtime::SceneWorld& collection, float orbitYawDelta, float orbitPitchDelta, float presentationAlpha );
    bool CycleMode( Runtime::SceneWorld& collection );
    bool TogglePin( Runtime::SceneWorld& collection );
    bool ApplyOrbitInput( Runtime::SceneWorld& collection,
                          bool attachModeActive,
                          int unhandledWheelDelta,
                          bool uiBlocksCameraMouse );
    bool SetTarget( Runtime::SceneWorld& collection, int modelIndex, AttachedCameraTargetSelection& outSelection );
    AttachedCameraSeedResult
    SeedTarget( Runtime::SceneWorld& collection, int seedModelIndex, AttachedCameraTargetSelection& outSelection );
    bool PickTarget( Runtime::SceneWorld& collection,
                     bool hasWorldRay,
                     const Math::Vector::Vector3& rayOrigin,
                     const Math::Vector::Vector3& rayDirection,
                     AttachedCameraTargetSelection& outSelection );

    static void Reset( AttachedCameraState& state );
    static void ClearTarget( AttachedCameraState& state );
    static bool TryAttachTargetHandlesFromModelIndex( const Runtime::SceneWorld& collection,
                                                      int modelIndex,
                                                      AttachedCameraTarget& target );
    static bool
    TryResolveTargetIdentity( const Runtime::SceneWorld& collection, AttachedCameraTarget& target, int& outModelIndex );
    static bool TryResolvePhysicsTarget( const Runtime::SceneWorld& collection,
                                         AttachedCameraTarget& target,
                                         AttachedCameraPhysicsTarget& outTarget,
                                         int* outModelIndex = nullptr );
    static bool
    TryResolveRagdollHead( const Runtime::SceneWorld& collection, int selectedModelIndex, int& outHeadModelIndex );
    static bool CycleSubmode( const Runtime::SceneWorld& collection,
                              AttachedCameraState& state,
                              AttachedCameraPhysicsTarget& outTarget,
                              bool& outShouldCaptureFixedOffset );

    static void CaptureFixedOffset( AttachedCameraState& state,
                                    const AttachedCameraPose& currentPose,
                                    const AttachedCameraPhysicsTarget& target );
    static void CaptureOrbit( AttachedCameraState& state,
                              const AttachedCameraPose& currentPose,
                              const AttachedCameraPhysicsTarget& target );
    static bool
    ApplyOrbitWheel( AttachedCameraState& state, const AttachedCameraPhysicsTarget& target, int unhandledWheelDelta );
    static bool BuildFollowPose( const Runtime::SceneWorld& collection,
                                 AttachedCameraState& state,
                                 const AttachedCameraPhysicsTarget& target,
                                 int modelIndex,
                                 const AttachedCameraPose& currentPose,
                                 float orbitYawDelta,
                                 float orbitPitchDelta,
                                 float presentationAlpha,
                                 AttachedCameraPoseCommand& outCommand );

    void ObserveSceneLifecycle( const SceneLifecyclePacket& packet )
    {
        if ( m_sceneLifecycleObserver.ShouldApply( packet, SceneRuntimeLifecycleEvent::AfterSceneCleared ) )
        {
            Reset( m_state );
        }
    }

  private:
    static bool SelectTarget( const Runtime::SceneWorld& collection,
                              AttachedCameraState& state,
                              int modelIndex,
                              AttachedCameraTargetSelection& outSelection );
    AttachedCameraState m_state;
    SceneLifecycleGenerationObserver m_sceneLifecycleObserver;
};
} // namespace Runtime
} // namespace SkullbonezCore
