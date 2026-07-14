/*
File: SkullbonezSource/Runtime/RuntimeInteractionController.h
Purpose:
  Owns high-level runtime workspace, tool ownership, camera-look, and physics
  advance policy.

Summary:
  Runtime workspaces are mutually exclusive. A transition records what owned
  world input before the new mode starts so InputRouter and domain owners can
  clear capture and payload before applying the new mode.

Glossary:
  Workspace: Coarse runtime mode such as live, inspect, edit, or replay.
  Owner: The tool or subsystem currently allowed to consume world input.
  Gesture: Active pointer operation that owns capture until it ends.
  Physics advance: Per-frame policy that decides whether the physics step runs.

Invariants:
  - RuntimeInteractionTransition is a diff record; callers must compare previous
    and current fields instead of inferring cleanup from the requested command.
  - Gesture and pointer-capture state must be cleared together on mode changes.
  - Object gestures retain a body handle; dense rows are resolved only by the
    owner that consumes the gesture.
  - Active drag kind, axis, and angular/scale mode exist only in the typed
    gesture; replay and editor payload owners must not mirror them in booleans.

Related:
  - SkullbonezSource/Runtime/RuntimeInteractionController.cpp
  - SkullbonezSource/Runtime/RunInput.cpp
  - SkullbonezSource/Runtime/RunFrame.cpp
  - Agentic/Reports/2026-07-11/interaction-state-machine-closure-review.md
*/
#pragma once

#include "RuntimeCameraMode.h"
#include "../Physics/PhysicsHandles.h"

namespace SkullbonezCore
{
namespace Runtime
{
struct RuntimeGestureCommand;
struct RuntimeGestureEvent;
enum class RuntimeWorkspace
{
    Live,
    Inspect,
    Edit,
    Replay
};

enum class WorldInteractionOwner
{
    None,
    InspectGizmo,
    EditorPlacement,
    EditorGizmo,
    ReplayScrub,
    ReplayVelocityEdit,
    ReplayPrediction,
    ReplayBranchTarget,
    ReplayCauseTree,
    Launcher,
    Manipulator
};

enum class PhysicsAdvanceState
{
    Disabled,
    Paused,
    Running,
    RunWhileStepHeld
};

enum class CameraLookState
{
    Passive,
    RightMouseLook,
    EditorViewportLook,
    ReplayInspectionLook
};

enum class RuntimePointerButton
{
    None,
    Left,
    Middle,
    Right
};

enum class RuntimePointerCaptureOwner
{
    None,
    UI,
    CameraLook,
    ToolGesture
};

enum class RuntimeInteractionGestureKind
{
    None,
    CameraLook,
    ObjectPick,
    EditorPlacementScaleDrag,
    GizmoDrag,
    MousePickupDrag,
    ReplayScrubDrag,
    ReplayVelocityDrag,
    ReplayPredictionHorizonDrag,
    ReplayCauseTreeDrag
};

enum class RuntimeGizmoDragKind
{
    None,
    Translate,
    Rotate,
    Scale
};

enum class InteractionExitReason
{
    EnterLive,
    EnterInspect,
    EnterEdit,
    EnterReplay,
    EnterLauncher,
    EnterManipulator,
    BeginGesture,
    EndGesture,
    ResetScene,
    LoadScene
};

struct RuntimeInteractionGesture
{
    RuntimeInteractionGestureKind kind = RuntimeInteractionGestureKind::None;
    RuntimePointerButton button = RuntimePointerButton::None;
    int startX = 0;
    int startY = 0;
    Physics::PhysicsBodyHandle body;
    int axis = -1;
    bool angular = false;
    RuntimeGizmoDragKind gizmoKind = RuntimeGizmoDragKind::None;
};

struct RuntimeInteractionTransition
{
    RuntimeWorkspace previousWorkspace = RuntimeWorkspace::Live;
    RuntimeWorkspace workspace = RuntimeWorkspace::Live;
    WorldInteractionOwner previousOwner = WorldInteractionOwner::None;
    WorldInteractionOwner owner = WorldInteractionOwner::None;
    CameraLookState previousCameraLook = CameraLookState::Passive;
    CameraLookState cameraLook = CameraLookState::Passive;
    PhysicsAdvanceState previousPhysicsAdvance = PhysicsAdvanceState::Running;
    PhysicsAdvanceState physicsAdvance = PhysicsAdvanceState::Running;
    RuntimeInteractionGesture previousGesture;
    RuntimeInteractionGesture gesture;
    RuntimePointerCaptureOwner previousPointerCapture = RuntimePointerCaptureOwner::None;
    RuntimePointerCaptureOwner pointerCapture = RuntimePointerCaptureOwner::None;
    InteractionExitReason reason = InteractionExitReason::EnterLive;
    bool workspaceChanged = false;
    bool ownerChanged = false;
    bool cameraLookChanged = false;
    bool physicsAdvanceChanged = false;
    bool gestureChanged = false;
    bool pointerCaptureChanged = false;
};

struct RuntimeInteractionFrameInput
{
    bool scenePhysicsEnabled = true;
    bool stepHeld = false;
    bool replayScrubbedHistoricalSample = false;
    bool replayLiveHeldAtCurrentFrame = false;
    bool rightMouseLookHeld = false;
    bool editorViewportLookActive = false;
    bool replayInspectionLookActive = false;
    bool forcePhysicsRunning = false;
    float sceneTimeScale = 1.0f;
};

struct RuntimePointerEvent
{
    RuntimePointerButton button = RuntimePointerButton::None;
    int clientX = 0;
    int clientY = 0;
    bool hasClientPosition = false;
    bool leftDown = false;
    bool leftPressed = false;
    bool leftReleased = false;
    bool rightDown = false;
    bool rightPressed = false;
    bool rightReleased = false;
    bool controlDown = false;
    bool shiftDown = false;
    bool uiWantsNativeMouseCursor = false;
    bool uiBlocksCameraMouse = false;
    bool suppressWorldAction = false;
};

// Concept: the published post-UI snapshot is the only input value later
// physics, replay, and render phases may observe. DeviceInputFrame remains
// private to the input turn that sampled hardware.
struct RuntimeInputSnapshot
{
    RuntimePointerEvent pointer;
    RuntimeInteractionFrameInput frameInput;
    bool appFocused = true;
    bool uiBlocksKeyboard = false;
    bool uiBlocksMouse = false;
    bool enterDown = false; // Replay restore level sampled with this frame.
    bool pageDown = false;  // Water-height decrease level.
    bool pageUp = false;    // Water-height increase level.
};

struct RuntimeInteractionFramePolicy
{
    RuntimeWorkspace workspace = RuntimeWorkspace::Live;
    WorldInteractionOwner owner = WorldInteractionOwner::None;
    PhysicsAdvanceState physicsAdvance = PhysicsAdvanceState::Running;
    CameraLookState cameraLook = CameraLookState::Passive;
    RuntimeInteractionGestureKind gesture = RuntimeInteractionGestureKind::None;
    RuntimePointerCaptureOwner pointerCapture = RuntimePointerCaptureOwner::None;
    float physicsTimeScale = 1.0f;
    bool cameraMouseLookActive = false;
    bool cameraKeyboardControlsActive = false;
    bool launcherActive = false;
    bool manipulatorActive = false;
};

class RuntimeInteractionController
{
  public:
    RuntimeWorkspace Workspace() const;
    WorldInteractionOwner Owner() const;
    CameraLookState CameraLook() const;
    PhysicsAdvanceState PhysicsAdvance() const;
    const RuntimeInteractionGesture& Gesture() const;
    RuntimePointerCaptureOwner PointerCapture() const;

    RuntimeInteractionTransition EnterLive();
    RuntimeInteractionTransition EnterInspect();
    RuntimeInteractionTransition EnterEdit();
    RuntimeInteractionTransition EnterReplay();
    RuntimeInteractionTransition EnterLauncher();
    RuntimeInteractionTransition EnterManipulator();
    RuntimeInteractionTransition EnterCameraMode( RunCameraMode mode );
    RuntimeInteractionTransition SetWorldInteractionOwner( WorldInteractionOwner owner, InteractionExitReason reason );
    RuntimeInteractionTransition SetWorldInteractionOwnerInWorkspace( RuntimeWorkspace workspace,
                                                                      WorldInteractionOwner owner,
                                                                      InteractionExitReason reason );
    // Commands are accepted only by the current owner; events publish the
    // resulting state after a successful mutation.
    bool ApplyGestureCommand( const RuntimeGestureCommand& command, RuntimeGestureEvent& outEvent );
    // Claims the workspace/owner, clears any previous interaction, and begins
    // one typed tool drag. A false result means the gesture/owner pairing is
    // invalid; the requested workspace and owner remain active.
    bool BeginOwnedToolGesture( RuntimeWorkspace workspace,
                                WorldInteractionOwner owner,
                                const RuntimeInteractionGesture& gesture );
    // Ends only the named active gesture so stale release events cannot cancel
    // a newer tool that already owns pointer capture.
    void EndGestureIfKind( RuntimeInteractionGestureKind kind );
    // Camera-look gesture ownership is interaction policy, not Run routing.
    // Sync begins only from an idle pointer owner and cancels on focus/policy exit.
    void SyncCameraLookGesture( const RuntimeInputSnapshot& input,
                                const RuntimeInteractionFramePolicy& policy,
                                bool mouseLookOwnsCursor );
    void CancelCameraLookGesture();
    RuntimeInteractionTransition ResetForScene( InteractionExitReason reason );

    RuntimeInteractionFramePolicy BuildFramePolicy( const RuntimeInteractionFrameInput& input ) const;

  private:
    RuntimeInteractionTransition BeginGesture( const RuntimeInteractionGesture& gesture,
                                               RuntimePointerCaptureOwner captureOwner,
                                               InteractionExitReason reason );
    RuntimeInteractionTransition EndGesture( InteractionExitReason reason );
    RuntimeInteractionTransition CaptureTransition( RuntimeWorkspace previousWorkspace,
                                                    WorldInteractionOwner previousOwner,
                                                    CameraLookState previousCameraLook,
                                                    PhysicsAdvanceState previousPhysicsAdvance,
                                                    const RuntimeInteractionGesture& previousGesture,
                                                    RuntimePointerCaptureOwner previousPointerCapture,
                                                    InteractionExitReason reason ) const;
    RuntimeInteractionTransition
    TransitionTo( RuntimeWorkspace workspace, WorldInteractionOwner owner, InteractionExitReason reason );
    void ValidateState() const;

    RuntimeWorkspace m_workspace = RuntimeWorkspace::Live;
    WorldInteractionOwner m_owner = WorldInteractionOwner::None;
    CameraLookState m_cameraLook = CameraLookState::Passive;
    PhysicsAdvanceState m_physicsAdvance = PhysicsAdvanceState::Running;
    RuntimeInteractionGesture m_gesture;
    RuntimePointerCaptureOwner m_pointerCapture = RuntimePointerCaptureOwner::None;
};
} // namespace Runtime
} // namespace SkullbonezCore
