/*
File: SkullbonezSource/Runtime/RuntimeInteractionController.h
Purpose:
  Owns high-level runtime workspace, tool ownership, camera-look, and physics
  advance policy.

Mental model:
  Runtime workspaces are mutually exclusive. A transition records what owned
  world input before the new mode starts so Run can clear old mouse capture,
  hover, drag, and replay/editor affordances before applying the new mode.

Related:
  - SkullbonezSource/Runtime/RuntimeInteractionController.cpp
  - SkullbonezSource/Runtime/RunInput.cpp
  - SkullbonezSource/Runtime/RunFrame.cpp
  - Agentic/Plans/runtime-interaction-controller-plan.md
*/
#pragma once

namespace SkullbonezCore
{
namespace Basics
{
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
    GizmoDrag,
    MousePickupDrag,
    ReplayScrubDrag,
    ReplayVelocityDrag,
    ReplayPredictionHorizonDrag,
    ReplayCauseTreeDrag
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
    int modelIndex = -1;
    int axis = -1;
    bool angular = false;
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

struct RuntimeInputSnapshot
{
    RuntimePointerEvent pointer;
    RuntimeInteractionFrameInput frameInput;
    bool appFocused = true;
    bool uiBlocksKeyboard = false;
    bool uiBlocksMouse = false;
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
    RuntimeInteractionTransition SetWorldInteractionOwner( WorldInteractionOwner owner, InteractionExitReason reason );
    RuntimeInteractionTransition SetWorldInteractionOwnerInWorkspace( RuntimeWorkspace workspace,
                                                                      WorldInteractionOwner owner,
                                                                      InteractionExitReason reason );
    RuntimeInteractionTransition BeginGesture( const RuntimeInteractionGesture& gesture,
                                               RuntimePointerCaptureOwner captureOwner,
                                               InteractionExitReason reason );
    RuntimeInteractionTransition EndGesture( InteractionExitReason reason );
    RuntimeInteractionTransition ResetForScene( InteractionExitReason reason );

    RuntimeInteractionFramePolicy BuildFramePolicy( const RuntimeInteractionFrameInput& input ) const;

  private:
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
} // namespace Basics
} // namespace SkullbonezCore
