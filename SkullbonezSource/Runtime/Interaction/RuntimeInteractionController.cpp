/*
File: SkullbonezSource/Runtime/Interaction/RuntimeInteractionController.cpp
Purpose:
  Implements authoritative runtime workspace and frame policy transitions.

Summary:
  The controller has no domain-side effects. It records ownership and produces
  transition/policy records; composition code performs subsystem-specific
  cleanup from those records.

Glossary:
  Workspace: Coarse runtime mode such as live, inspect, edit, or replay.
  Owner: The tool or subsystem currently allowed to consume world input.
  Gesture: Active pointer operation that owns capture until it ends.
  Cross-scene pause lock: Explicit policy fact that outranks normal live/tool
    physics advance until the sampled step input is held.

Invariants:
  - The controller does not clear hover, replay, editor, or physics state
    directly; it returns transition records for Run to apply.
  - Pointer capture, owner, and gesture must describe the same active operation.
  - Returned frame policy is complete; Run consumes it without policy repair.

Related:
  - SkullbonezSource/Runtime/Interaction/RuntimeInteractionController.h
  - SkullbonezSource/Runtime/App/InputRouter.Interactions.cpp
*/
#include "RuntimeInteractionController.h"
#include "RuntimeInteractionCommands.h"

#include <algorithm>
#include <cassert>

namespace SkullbonezCore
{
namespace Runtime
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


bool IsActiveGestureValid(
    const RuntimeInteractionGesture& gesture,
    RuntimePointerCaptureOwner captureOwner,
    WorldInteractionOwner owner
)
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
    case RuntimeInteractionGestureKind::EditorPlacementScaleDrag:
        return captureOwner == RuntimePointerCaptureOwner::ToolGesture &&
               owner == WorldInteractionOwner::EditorPlacement;
    case RuntimeInteractionGestureKind::GizmoDrag:
        return captureOwner == RuntimePointerCaptureOwner::ToolGesture && IsGizmoOwner( owner ) &&
               gesture.gizmoKind != RuntimeGizmoDragKind::None && gesture.axis >= 0 && gesture.body.IsValid();
    case RuntimeInteractionGestureKind::MousePickupDrag:
        return captureOwner == RuntimePointerCaptureOwner::ToolGesture && owner == WorldInteractionOwner::Manipulator &&
               gesture.body.IsValid();
    case RuntimeInteractionGestureKind::ReplayScrubDrag:
    case RuntimeInteractionGestureKind::ReplayPredictionHorizonDrag:
        return captureOwner == RuntimePointerCaptureOwner::ToolGesture && IsReplayOwner( owner );
    case RuntimeInteractionGestureKind::ReplayVelocityDrag:
        return captureOwner == RuntimePointerCaptureOwner::ToolGesture &&
               owner == WorldInteractionOwner::ReplayVelocityEdit && gesture.body.IsValid() && gesture.axis >= 0;
    case RuntimeInteractionGestureKind::ReplayCauseTreeDrag:
        return captureOwner == RuntimePointerCaptureOwner::ToolGesture &&
               owner == WorldInteractionOwner::ReplayCauseTree && ( gesture.axis == 0 || gesture.axis == 1 );
    }

    return false;
}


bool CanBeginGesture(
    const RuntimeInteractionGesture& gesture,
    RuntimePointerCaptureOwner captureOwner,
    WorldInteractionOwner owner
)
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
    return TransitionTo(
        RuntimeWorkspace::Edit,
        WorldInteractionOwner::EditorPlacement,
        InteractionExitReason::EnterEdit
    );
}


RuntimeInteractionTransition RuntimeInteractionController::EnterReplay()
{
    return TransitionTo(
        RuntimeWorkspace::Replay,
        WorldInteractionOwner::ReplayScrub,
        InteractionExitReason::EnterReplay
    );
}


RuntimeInteractionTransition RuntimeInteractionController::EnterLauncher()
{
    return TransitionTo(
        RuntimeWorkspace::Live,
        WorldInteractionOwner::Launcher,
        InteractionExitReason::EnterLauncher
    );
}


RuntimeInteractionTransition RuntimeInteractionController::EnterManipulator()
{
    return TransitionTo(
        RuntimeWorkspace::Live,
        WorldInteractionOwner::Manipulator,
        InteractionExitReason::EnterManipulator
    );
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
    case RunCameraMode::Director:
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


RuntimeInteractionTransition
RuntimeInteractionController::SetWorldInteractionOwner( WorldInteractionOwner owner, InteractionExitReason reason )
{
    return TransitionTo( m_workspace, owner, reason );
}

RuntimeWorkspace RuntimeInteractionController::WorkspaceForOwner( WorldInteractionOwner owner ) const
{
    // Concept: workspace classification is interaction-domain vocabulary. Tool
    // routers ask this owner instead of duplicating replay/edit/live mappings.
    if ( owner == WorldInteractionOwner::ReplayScrub || owner == WorldInteractionOwner::ReplayVelocityEdit ||
         owner == WorldInteractionOwner::ReplayPrediction || owner == WorldInteractionOwner::ReplayBranchTarget ||
         owner == WorldInteractionOwner::ReplayCauseTree )
    {
        return RuntimeWorkspace::Replay;
    }
    if ( owner == WorldInteractionOwner::InspectGizmo )
    {
        return RuntimeWorkspace::Inspect;
    }
    if ( owner == WorldInteractionOwner::EditorPlacement || owner == WorldInteractionOwner::EditorGizmo )
    {
        return RuntimeWorkspace::Edit;
    }
    if ( owner == WorldInteractionOwner::Launcher || owner == WorldInteractionOwner::Manipulator )
    {
        return RuntimeWorkspace::Live;
    }
    return m_workspace;
}


RuntimeInteractionTransition RuntimeInteractionController::SetWorldInteractionOwnerInWorkspace(
    RuntimeWorkspace workspace,
    WorldInteractionOwner owner,
    InteractionExitReason reason
)
{
    return TransitionTo( workspace, owner, reason );
}


RuntimeInteractionTransition RuntimeInteractionController::BeginGesture(
    const RuntimeInteractionGesture& gesture,
    RuntimePointerCaptureOwner captureOwner,
    InteractionExitReason reason
)
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
    if ( !canBegin )
    {
        return CaptureTransition(
            previousWorkspace,
            previousOwner,
            previousCameraLook,
            previousPhysicsAdvance,
            previousGesture,
            previousPointerCapture,
            reason
        );
    }

    m_gesture = gesture;
    m_pointerCapture = captureOwner;
    ValidateState();
    return CaptureTransition(
        previousWorkspace,
        previousOwner,
        previousCameraLook,
        previousPhysicsAdvance,
        previousGesture,
        previousPointerCapture,
        reason
    );
}


RuntimeInteractionTransition RuntimeInteractionController::EndGesture( InteractionExitReason reason )
{
    const RuntimeWorkspace previousWorkspace = m_workspace;
    const WorldInteractionOwner previousOwner = m_owner;
    const CameraLookState previousCameraLook = m_cameraLook;
    const PhysicsAdvanceState previousPhysicsAdvance = m_physicsAdvance;
    const RuntimeInteractionGesture previousGesture = m_gesture;
    const RuntimePointerCaptureOwner previousPointerCapture = m_pointerCapture;

    m_gesture = RuntimeInteractionGesture {};
    m_pointerCapture = RuntimePointerCaptureOwner::None;
    ValidateState();
    return CaptureTransition(
        previousWorkspace,
        previousOwner,
        previousCameraLook,
        previousPhysicsAdvance,
        previousGesture,
        previousPointerCapture,
        reason
    );
}


bool RuntimeInteractionController::ApplyGestureCommand(
    const RuntimeGestureCommand& command,
    RuntimeGestureEvent& outEvent
)
{
    outEvent = RuntimeGestureEvent {};
    RuntimeInteractionTransition transition;
    if ( command.action == RuntimeGestureCommandAction::Begin )
    {
        transition = BeginGesture( command.gesture, command.captureOwner, command.reason );
        if ( !transition.gestureChanged || m_gesture.kind != command.gesture.kind )
        {
            return false;
        }
        outEvent.type = RuntimeGestureEventType::Began;
    }
    else
    {
        if ( command.gesture.kind == RuntimeInteractionGestureKind::None || m_gesture.kind != command.gesture.kind )
        {
            return false;
        }
        transition = EndGesture( command.reason );
        if ( !transition.gestureChanged )
        {
            return false;
        }
        outEvent.type = RuntimeGestureEventType::Ended;
    }

    // Invariant: notification is filled only after the controller mutation won.
    outEvent.previousGesture = transition.previousGesture;
    outEvent.gesture = transition.gesture;
    outEvent.previousPointerCapture = transition.previousPointerCapture;
    outEvent.pointerCapture = transition.pointerCapture;
    return true;
}


bool RuntimeInteractionController::BeginOwnedToolGesture(
    RuntimeWorkspace workspace,
    WorldInteractionOwner owner,
    const RuntimeInteractionGesture& gesture
)
{
    // Invariant: owner selection and gesture capture share one controller
    // boundary. Domain tools must not mirror either half in replay/root state.
    SetWorldInteractionOwnerInWorkspace( workspace, owner, InteractionExitReason::BeginGesture );
    return BeginGesture( gesture, RuntimePointerCaptureOwner::ToolGesture, InteractionExitReason::BeginGesture )
        .gestureChanged;
}


void RuntimeInteractionController::EndGestureIfKind( RuntimeInteractionGestureKind kind )
{
    if ( m_gesture.kind == kind )
    {
        EndGesture( InteractionExitReason::EndGesture );
    }
}


void RuntimeInteractionController::CancelCameraLookGesture()
{
    if ( m_pointerCapture == RuntimePointerCaptureOwner::CameraLook )
    {
        EndGesture( InteractionExitReason::EndGesture );
    }
}


void RuntimeInteractionController::SyncCameraLookGesture(
    const RuntimeInputSnapshot& input,
    const RuntimeInteractionFramePolicy& policy,
    bool mouseLookOwnsCursor
)
{
    const bool wantsCameraLook = input.appFocused && mouseLookOwnsCursor && policy.cameraMouseLookActive;
    if ( !wantsCameraLook )
    {
        CancelCameraLookGesture();
        return;
    }
    if ( m_pointerCapture == RuntimePointerCaptureOwner::CameraLook ||
         m_pointerCapture != RuntimePointerCaptureOwner::None || m_gesture.kind != RuntimeInteractionGestureKind::None )
    {
        return;
    }

    RuntimeInteractionGesture gesture;
    gesture.kind = RuntimeInteractionGestureKind::CameraLook;
    gesture.button = input.pointer.rightDown ? RuntimePointerButton::Right : RuntimePointerButton::None;
    gesture.startX = input.pointer.clientX;
    gesture.startY = input.pointer.clientY;
    BeginGesture( gesture, RuntimePointerCaptureOwner::CameraLook, InteractionExitReason::BeginGesture );
}


RuntimeInteractionTransition RuntimeInteractionController::ResetForScene( InteractionExitReason reason )
{
    return TransitionTo( RuntimeWorkspace::Live, WorldInteractionOwner::None, reason );
}


void RuntimeInteractionController::ObserveSceneLifecycle(
    const SceneLifecyclePacket& packet,
    bool enterInspectAfterActivation
)
{
    if ( m_sceneResetObserver.ShouldApply( packet, SceneRuntimeLifecycleEvent::AfterSceneCleared ) )
    {
        ResetForScene( InteractionExitReason::LoadScene );
    }
    if ( enterInspectAfterActivation &&
         m_sceneActivationObserver.ShouldApply( packet, SceneRuntimeLifecycleEvent::AfterSceneActivated ) )
    {
        EnterInspect();
    }
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
    else if (
        m_workspace == RuntimeWorkspace::Inspect || m_workspace == RuntimeWorkspace::Edit ||
        m_workspace == RuntimeWorkspace::Replay
    )
    {
        policy.physicsAdvance = PhysicsAdvanceState::RunWhileStepHeld;
    }
    else
    {
        policy.physicsAdvance = PhysicsAdvanceState::Running;
    }
    if ( input.crossScenePauseLocked )
    {
        // Invariant: the explicit cross-scene lock outranks camera, workspace,
        // launcher, and tool policy. Space remains the sole step-level release.
        policy.physicsAdvance = PhysicsAdvanceState::RunWhileStepHeld;
        if ( !input.stepHeld )
        {
            policy.physicsTimeScale = 0.0f;
        }
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


RuntimeInteractionTransition RuntimeInteractionController::CaptureTransition(
    RuntimeWorkspace previousWorkspace,
    WorldInteractionOwner previousOwner,
    CameraLookState previousCameraLook,
    PhysicsAdvanceState previousPhysicsAdvance,
    const RuntimeInteractionGesture& previousGesture,
    RuntimePointerCaptureOwner previousPointerCapture,
    InteractionExitReason reason
) const
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
                                transition.previousGesture.body != transition.gesture.body ||
                                transition.previousGesture.axis != transition.gesture.axis ||
                                transition.previousGesture.angular != transition.gesture.angular;
    transition.pointerCaptureChanged = transition.previousPointerCapture != transition.pointerCapture;
    return transition;
}


RuntimeInteractionTransition RuntimeInteractionController::TransitionTo(
    RuntimeWorkspace workspace,
    WorldInteractionOwner owner,
    InteractionExitReason reason
)
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
    m_gesture = RuntimeInteractionGesture {};
    m_pointerCapture = RuntimePointerCaptureOwner::None;

    ValidateState();
    return CaptureTransition(
        previousWorkspace,
        previousOwner,
        previousCameraLook,
        previousPhysicsAdvance,
        previousGesture,
        previousPointerCapture,
        reason
    );
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
} // namespace Runtime
} // namespace SkullbonezCore
