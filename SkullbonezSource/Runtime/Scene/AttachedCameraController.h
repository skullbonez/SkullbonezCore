/*
File: SkullbonezSource/Runtime/Scene/AttachedCameraController.h
Purpose:
  Owns attach-camera target identity, orbit state, and follow-pose solving.

Summary:
  Scene resolves attach mode against live bodies through stable body/collider
  handles, then turns the current body snapshot and camera pose into the next
  camera pose. AttachedCameraController owns that durable state and performs
  synchronous commands through borrowed model stores and cameras. The same
  owner lends fixed-relative follow to replay inspection while preserving the
  operator's ordinary Attach state for restoration.

Glossary:
  Attach target: Physics body/collider identity plus scene object id used to recover a
    followed model after dense rows move.
  Target snapshot: One-frame position, velocity, rotation, and broad radius read
    from physics stores.
  Pose command: Solved camera eye/view/up plus whether entry tweening should
    start for this solve.

Invariants:
  - Target recovery uses physics-store handles and scene object ids; a dense row is
    retained only as a typed, revalidated cache.
  - The controller does not store borrowed collection or camera pointers.
  - Pose commands are finite and never point eye and view at the same point.
  - App applies Scene's clear event at most once per lifecycle generation.
  - Focused inspection never creates a second retained target owner; its saved
    ordinary state and temporary replay target remain private to this controller.

Related:
  - SkullbonezSource/Runtime/Camera/AttachedCameraValues.h
  - SkullbonezSource/Runtime/App/InputRouter.Interactions.cpp
  - SkullbonezSource/Runtime/App/Run.h
  - Agentic/Reference/engine-glossary.md
*/
#pragma once

#include "../Camera/AttachedCameraValues.h"

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
    bool TickFollow( Runtime::SceneWorld& collection, float orbitYawDelta, float orbitPitchDelta, float presentationAlpha );
    bool CycleMode( Runtime::SceneWorld& collection );
    bool TogglePin( Runtime::SceneWorld& collection );
    bool ApplyOrbitInput( Runtime::SceneWorld& collection, bool attachModeActive, int unhandledWheelDelta,
                          bool uiBlocksCameraMouse );
    bool BeginFocusedInspection( Runtime::SceneWorld& collection, const AttachedCameraFocusRequest& request,
                                 AttachedCameraPose& outPreparedPose );
    bool TickFocusedInspection( Runtime::SceneWorld& collection, float orbitYawDelta, float orbitPitchDelta,
                                int wheelDelta );
    void EndFocusedInspection();
    bool SetTarget( Runtime::SceneWorld& collection, int modelIndex, AttachedCameraTargetSelection& outSelection );
    AttachedCameraSeedResult SeedTarget( Runtime::SceneWorld& collection, int seedModelIndex,
                                         AttachedCameraTargetSelection& outSelection );
    static void Reset( AttachedCameraState& state );
    static void ClearTarget( AttachedCameraState& state );
    static bool TryAttachTargetHandlesFromModelIndex( const Runtime::SceneWorld& collection, int modelIndex,
                                                      AttachedCameraTarget& target );
    static bool TryResolveTargetIdentity( const Runtime::SceneWorld& collection, AttachedCameraTarget& target,
                                          int& outModelIndex );
    static bool TryResolvePhysicsTarget( const Runtime::SceneWorld& collection, AttachedCameraTarget& target,
                                         AttachedCameraPhysicsTarget& outTarget, int* outModelIndex = nullptr );
    static bool TryResolveRagdollHead( const Runtime::SceneWorld& collection, int selectedModelIndex,
                                       int& outHeadModelIndex );
    static bool CycleSubmode( const Runtime::SceneWorld& collection, AttachedCameraState& state,
                              AttachedCameraPhysicsTarget& outTarget, bool& outShouldCaptureFixedOffset );

    static void CaptureFixedOffset( AttachedCameraState& state, const AttachedCameraPose& currentPose,
                                    const AttachedCameraPhysicsTarget& target );
    static void CaptureOrbit( AttachedCameraState& state, const AttachedCameraPose& currentPose,
                              const AttachedCameraPhysicsTarget& target );
    static bool ApplyOrbitWheel( AttachedCameraState& state, const AttachedCameraPhysicsTarget& target,
                                 int unhandledWheelDelta );
    static bool BuildFollowPose( const Runtime::SceneWorld& collection, AttachedCameraState& state,
                                 const AttachedCameraPhysicsTarget& target, int modelIndex,
                                 const AttachedCameraPose& currentPose, float orbitYawDelta, float orbitPitchDelta,
                                 float presentationAlpha, AttachedCameraPoseCommand& outCommand );

    void ResetForSceneLoad()
    {
        Reset( m_state );
        Reset( m_suspendedState );
        m_hasSuspendedState = false;
    }

  private:
    static bool SelectTarget( const Runtime::SceneWorld& collection, AttachedCameraState& state, int modelIndex,
                              AttachedCameraTargetSelection& outSelection );
    AttachedCameraState m_state;
    AttachedCameraState m_suspendedState;
    bool m_hasSuspendedState = false;
};
} // namespace Runtime
} // namespace SkullbonezCore
