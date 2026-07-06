/*
File: SkullbonezSource/Runtime/RuntimeInteractionController.cpp
Purpose:
  Implements authoritative runtime workspace and frame policy transitions.

Mental model:
  The controller is intentionally side-effect free. It records ownership and
  produces transition/policy records; Run performs subsystem-specific cleanup
  from those records.

Glossary:
  Workspace: Coarse runtime mode such as live, inspect, edit, or replay.
  Owner: The tool or subsystem currently allowed to consume world input.
  Gesture: Active pointer operation that owns capture until it ends.

Invariants:
  - The controller does not clear hover, replay, editor, or physics state
    directly; it returns transition records for Run to apply.
  - Pointer capture, owner, and gesture must describe the same active operation.

Related:
  - SkullbonezSource/Runtime/RuntimeInteractionController.h
  - SkullbonezSource/Runtime/RunInput.cpp
*/
#include "RuntimeInteractionController.h"

#include <algorithm>
#include <cassert>

namespace SkullbonezCore
{
namespace Basics
{
namespace
{
bool IsReplayOwner( WorldInteractionOwner owner )
{
    return owner == WorldInteractionOwner::ReplayScrub || owner == WorldInteractionOwner::ReplayVelocityEdit ||
           owner == WorldInteractionOwner::ReplayPrediction || owner == WorldInteractionOwner::ReplayBranchTarget ||
           owner == WorldInteractionOwner::ReplayCauseTree;
}


bool IsGizmoOwner( WorldInteractionOwner owner )
{
    return owner == WorldInteractionOwner::EditorGizmo || owner == WorldInteractionOwner::InspectGizmo;
}


bool IsActiveGestureValid( const RuntimeInteractionGesture& gesture,
                           RuntimePointerCaptureOwner captureOwner,
                           WorldInteractionOwner owner )
{
    // Invariant: every non-empty gesture must have both a compatible pointer
    // capture owner and a world owner. This keeps replay, editor, and
    // manipulator drags from overlapping after mode transitions.
    switch ( gesture.kind )
    {
    case RuntimeInteractionGestureKind::None:
        return captureOwner == RuntimePointerCaptureOwner::None;
    case RuntimeInteractionGestureKind::CameraLook:
        return captureOwner == RuntimePointerCaptureOwner::CameraLook;
    case RuntimeInteractionGestureKind::ObjectPick:
        return captureOwner == RuntimePointerCaptureOwner::ToolGesture && owner != WorldInteractionOwner::None;
    case RuntimeInteractionGestureKind::GizmoDrag:
        return captureOwner == RuntimePointerCaptureOwner::ToolGesture && IsGizmoOwner( owner );
    case RuntimeInteractionGestureKind::MousePickupDrag:
        return captureOwner == RuntimePointerCaptureOwner::ToolGesture && owner == WorldInteractionOwner::Manipulator;
    case RuntimeInteractionGestureKind::ReplayScrubDrag:
    case RuntimeInteractionGestureKind::ReplayVelocityDrag:
    case RuntimeInteractionGestureKind::ReplayPredictionHorizonDrag:
    case RuntimeInteractionGestureKind::ReplayCauseTreeDrag:
        return captureOwner == RuntimePointerCaptureOwner::ToolGesture && IsReplayOwner( owner );
    }

    return false;
}


bool CanBeginGesture( const RuntimeInteractionGesture& gesture,
                      RuntimePointerCaptureOwner captureOwner,
                      WorldInteractionOwner owner )
{
    return gesture.kind != RuntimeInteractionGestureKind::None && IsActiveGestureValid( gesture, captureOwner, owner );
}
} // namespace

RuntimeWorkspace RuntimeInteractionController::Workspace() const
{
    return m_workspace;
}


WorldInteractionOwner RuntimeInteractionController::Owner() const
{
    return m_owner;
}


CameraLookState RuntimeInteractionController::CameraLook() const
{
    return m_cameraLook;
}


PhysicsAdvanceState RuntimeInteractionController::PhysicsAdvance() const
{
    return m_physicsAdvance;
}


const RuntimeInteractionGesture& RuntimeInteractionController::Gesture() const
{
    return m_gesture;
}


RuntimePointerCaptureOwner RuntimeInteractionController::PointerCapture() const
{
    return m_pointerCapture;
}


RuntimeInteractionTransition RuntimeInteractionController::EnterLive()
{
    return TransitionTo( RuntimeWorkspace::Live, WorldInteractionOwner::None, InteractionExitReason::EnterLive );
}


RuntimeInteractionTransition RuntimeInteractionController::EnterInspect()
{
    return TransitionTo( RuntimeWorkspace::Inspect, WorldInteractionOwner::None, InteractionExitReason::EnterInspect );
}


RuntimeInteractionTransition RuntimeInteractionController::EnterEdit()
{
    return TransitionTo( RuntimeWorkspace::Edit,
                         WorldInteractionOwner::EditorPlacement,
                         InteractionExitReason::EnterEdit );
}


RuntimeInteractionTransition RuntimeInteractionController::EnterReplay()
{
    return TransitionTo( RuntimeWorkspace::Replay,
                         WorldInteractionOwner::ReplayScrub,
                         InteractionExitReason::EnterReplay );
}


RuntimeInteractionTransition RuntimeInteractionController::EnterLauncher()
{
    return TransitionTo( RuntimeWorkspace::Live,
                         WorldInteractionOwner::Launcher,
                         InteractionExitReason::EnterLauncher );
}


RuntimeInteractionTransition RuntimeInteractionController::EnterManipulator()
{
    return TransitionTo( RuntimeWorkspace::Live,
                         WorldInteractionOwner::Manipulator,
                         InteractionExitReason::EnterManipulator );
}


RuntimeInteractionTransition RuntimeInteractionController::EnterCameraMode( RunCameraMode mode )
{
    // Why: camera mode is the user-facing command, while workspace/owner is the
    // interaction contract. Keeping this mapping here makes mode transitions use
    // the same cleanup metadata as direct tool and replay owner transitions.
    switch ( mode )
    {
    case RunCameraMode::Demo:
    case RunCameraMode::Scene:
        return EnterLive();
    case RunCameraMode::Inspect:
    case RunCameraMode::Attach:
        return EnterInspect();
    case RunCameraMode::Launcher:
        return EnterLauncher();
    case RunCameraMode::Manipulator:
        return EnterManipulator();
    case RunCameraMode::Count:
        break;
    }

    return EnterLive();
}


RuntimeInteractionTransition RuntimeInteractionController::SetWorldInteractionOwner( WorldInteractionOwner owner,
                                                                                     InteractionExitReason reason )
{
    return TransitionTo( m_workspace, owner, reason );
}


RuntimeInteractionTransition
RuntimeInteractionController::SetWorldInteractionOwnerInWorkspace( RuntimeWorkspace workspace,
                                                                   WorldInteractionOwner owner,
                                                                   InteractionExitReason reason )
{
    return TransitionTo( workspace, owner, reason );
}


RuntimeInteractionTransition RuntimeInteractionController::BeginGesture( const RuntimeInteractionGesture& gesture,
                                                                         RuntimePointerCaptureOwner captureOwner,
                                                                         InteractionExitReason reason )
{
    const RuntimeWorkspace previousWorkspace = m_workspace;
    const WorldInteractionOwner previousOwner = m_owner;
    const CameraLookState previousCameraLook = m_cameraLook;
    const PhysicsAdvanceState previousPhysicsAdvance = m_physicsAdvance;
    const RuntimeInteractionGesture previousGesture = m_gesture;
    const RuntimePointerCaptureOwner previousPointerCapture = m_pointerCapture;

    const bool canBegin = previousGesture.kind == RuntimeInteractionGestureKind::None &&
                          previousPointerCapture == RuntimePointerCaptureOwner::None &&
                          CanBeginGesture( gesture, captureOwner, m_owner );
#ifdef _DEBUG
    assert( canBegin );
#endif
    if ( !canBegin )
    {
        return CaptureTransition( previousWorkspace,
                                  previousOwner,
                                  previousCameraLook,
                                  previousPhysicsAdvance,
                                  previousGesture,
                                  previousPointerCapture,
                                  reason );
    }

    m_gesture = gesture;
    m_pointerCapture = captureOwner;
    ValidateState();
    return CaptureTransition( previousWorkspace,
                              previousOwner,
                              previousCameraLook,
                              previousPhysicsAdvance,
                              previousGesture,
                              previousPointerCapture,
                              reason );
}


RuntimeInteractionTransition RuntimeInteractionController::EndGesture( InteractionExitReason reason )
{
    const RuntimeWorkspace previousWorkspace = m_workspace;
    const WorldInteractionOwner previousOwner = m_owner;
    const CameraLookState previousCameraLook = m_cameraLook;
    const PhysicsAdvanceState previousPhysicsAdvance = m_physicsAdvance;
    const RuntimeInteractionGesture previousGesture = m_gesture;
    const RuntimePointerCaptureOwner previousPointerCapture = m_pointerCapture;

    m_gesture = RuntimeInteractionGesture{};
    m_pointerCapture = RuntimePointerCaptureOwner::None;
    ValidateState();
    return CaptureTransition( previousWorkspace,
                              previousOwner,
                              previousCameraLook,
                              previousPhysicsAdvance,
                              previousGesture,
                              previousPointerCapture,
                              reason );
}


RuntimeInteractionTransition RuntimeInteractionController::ResetForScene( InteractionExitReason reason )
{
    return TransitionTo( RuntimeWorkspace::Live, WorldInteractionOwner::None, reason );
}


RuntimeInteractionFramePolicy
RuntimeInteractionController::BuildFramePolicy( const RuntimeInteractionFrameInput& input ) const
{
    ValidateState();

    RuntimeInteractionFramePolicy policy;
    policy.workspace = m_workspace;
    policy.owner = m_owner;
    policy.gesture = m_gesture.kind;
    policy.pointerCapture = m_pointerCapture;
    policy.launcherActive = m_owner == WorldInteractionOwner::Launcher;
    policy.manipulatorActive = m_owner == WorldInteractionOwner::Manipulator;
    policy.physicsTimeScale = (std::max)( 0.0f, input.sceneTimeScale );

    if ( !input.scenePhysicsEnabled || input.replayScrubbedHistoricalSample )
    {
        policy.physicsAdvance = PhysicsAdvanceState::Disabled;
        policy.physicsTimeScale = 0.0f;
    }
    else if ( input.forcePhysicsRunning || policy.launcherActive )
    {
        policy.physicsAdvance = PhysicsAdvanceState::Running;
    }
    else if ( input.replayLiveHeldAtCurrentFrame )
    {
        policy.physicsAdvance = PhysicsAdvanceState::RunWhileStepHeld;
        if ( !input.stepHeld )
        {
            policy.physicsTimeScale = 0.0f;
        }
    }
    else if ( m_gesture.kind == RuntimeInteractionGestureKind::MousePickupDrag )
    {
        policy.physicsAdvance = PhysicsAdvanceState::Running;
    }
    else if ( m_workspace == RuntimeWorkspace::Inspect || m_workspace == RuntimeWorkspace::Edit ||
              m_workspace == RuntimeWorkspace::Replay )
    {
        policy.physicsAdvance = PhysicsAdvanceState::RunWhileStepHeld;
    }
    else
    {
        policy.physicsAdvance = PhysicsAdvanceState::Running;
    }

    const bool toolGestureCaptured = m_pointerCapture == RuntimePointerCaptureOwner::ToolGesture;
    if ( toolGestureCaptured )
    {
        policy.cameraLook = CameraLookState::Passive;
    }
    else if ( input.editorViewportLookActive )
    {
        policy.cameraLook = CameraLookState::EditorViewportLook;
    }
    else if ( input.replayInspectionLookActive )
    {
        policy.cameraLook = CameraLookState::ReplayInspectionLook;
    }
    else if ( input.rightMouseLookHeld )
    {
        policy.cameraLook = CameraLookState::RightMouseLook;
    }
    else
    {
        policy.cameraLook = CameraLookState::Passive;
    }

    policy.cameraMouseLookActive = policy.cameraLook != CameraLookState::Passive;
    policy.cameraKeyboardControlsActive = true;

    return policy;
}


RuntimeInteractionTransition
RuntimeInteractionController::CaptureTransition( RuntimeWorkspace previousWorkspace,
                                                 WorldInteractionOwner previousOwner,
                                                 CameraLookState previousCameraLook,
                                                 PhysicsAdvanceState previousPhysicsAdvance,
                                                 const RuntimeInteractionGesture& previousGesture,
                                                 RuntimePointerCaptureOwner previousPointerCapture,
                                                 InteractionExitReason reason ) const
{
    RuntimeInteractionTransition transition;
    transition.previousWorkspace = previousWorkspace;
    transition.previousOwner = previousOwner;
    transition.previousCameraLook = previousCameraLook;
    transition.previousPhysicsAdvance = previousPhysicsAdvance;
    transition.previousGesture = previousGesture;
    transition.previousPointerCapture = previousPointerCapture;
    transition.workspace = m_workspace;
    transition.owner = m_owner;
    transition.cameraLook = m_cameraLook;
    transition.physicsAdvance = m_physicsAdvance;
    transition.gesture = m_gesture;
    transition.pointerCapture = m_pointerCapture;
    transition.reason = reason;

    transition.workspaceChanged = transition.previousWorkspace != transition.workspace;
    transition.ownerChanged = transition.previousOwner != transition.owner;
    transition.cameraLookChanged = transition.previousCameraLook != transition.cameraLook;
    transition.physicsAdvanceChanged = transition.previousPhysicsAdvance != transition.physicsAdvance;
    transition.gestureChanged = transition.previousGesture.kind != transition.gesture.kind ||
                                transition.previousGesture.button != transition.gesture.button ||
                                transition.previousGesture.startX != transition.gesture.startX ||
                                transition.previousGesture.startY != transition.gesture.startY ||
                                transition.previousGesture.modelIndex != transition.gesture.modelIndex ||
                                transition.previousGesture.axis != transition.gesture.axis ||
                                transition.previousGesture.angular != transition.gesture.angular;
    transition.pointerCaptureChanged = transition.previousPointerCapture != transition.pointerCapture;
    return transition;
}


RuntimeInteractionTransition RuntimeInteractionController::TransitionTo( RuntimeWorkspace workspace,
                                                                         WorldInteractionOwner owner,
                                                                         InteractionExitReason reason )
{
    const RuntimeWorkspace previousWorkspace = m_workspace;
    const WorldInteractionOwner previousOwner = m_owner;
    const CameraLookState previousCameraLook = m_cameraLook;
    const PhysicsAdvanceState previousPhysicsAdvance = m_physicsAdvance;
    const RuntimeInteractionGesture previousGesture = m_gesture;
    const RuntimePointerCaptureOwner previousPointerCapture = m_pointerCapture;

    m_workspace = workspace;
    m_owner = owner;
    m_cameraLook = CameraLookState::Passive;
    m_physicsAdvance = PhysicsAdvanceState::Running;
    m_gesture = RuntimeInteractionGesture{};
    m_pointerCapture = RuntimePointerCaptureOwner::None;

    ValidateState();
    return CaptureTransition( previousWorkspace,
                              previousOwner,
                              previousCameraLook,
                              previousPhysicsAdvance,
                              previousGesture,
                              previousPointerCapture,
                              reason );
}


void RuntimeInteractionController::ValidateState() const
{
#ifdef _DEBUG
    assert( IsActiveGestureValid( m_gesture, m_pointerCapture, m_owner ) );

    if ( m_pointerCapture == RuntimePointerCaptureOwner::CameraLook )
    {
        assert( m_gesture.kind == RuntimeInteractionGestureKind::CameraLook );
    }
    if ( m_pointerCapture == RuntimePointerCaptureOwner::ToolGesture )
    {
        assert( m_owner != WorldInteractionOwner::None );
        assert( m_gesture.kind != RuntimeInteractionGestureKind::None );
        assert( m_gesture.kind != RuntimeInteractionGestureKind::CameraLook );
    }

    switch ( m_gesture.kind )
    {
    case RuntimeInteractionGestureKind::None:
        break;
    case RuntimeInteractionGestureKind::CameraLook:
        assert( m_pointerCapture == RuntimePointerCaptureOwner::CameraLook );
        break;
    case RuntimeInteractionGestureKind::ObjectPick:
        assert( m_owner != WorldInteractionOwner::None );
        break;
    case RuntimeInteractionGestureKind::GizmoDrag:
        assert( IsGizmoOwner( m_owner ) );
        break;
    case RuntimeInteractionGestureKind::MousePickupDrag:
        assert( m_owner == WorldInteractionOwner::Manipulator );
        break;
    case RuntimeInteractionGestureKind::ReplayScrubDrag:
    case RuntimeInteractionGestureKind::ReplayVelocityDrag:
    case RuntimeInteractionGestureKind::ReplayPredictionHorizonDrag:
    case RuntimeInteractionGestureKind::ReplayCauseTreeDrag:
        assert( IsReplayOwner( m_owner ) );
        break;
    }
#endif
}
} // namespace Basics
} // namespace SkullbonezCore
