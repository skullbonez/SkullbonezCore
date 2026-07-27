/*
File: SkullbonezSource/Runtime/App/InputFrame.h
Purpose:
  Declares the stateless once-per-frame input orchestration boundary.

Summary:
  Run owns top-level frame order and coordinates this input turn directly.
  InputRouter alone retains device, semantic-action, focus, and
  pointer-presentation state between frames. The selected operator surface and
  optional automation/probe queues converge before established domain-owner
  commands are applied.

Glossary:
  Input turn: Ordered frame interval that samples hardware, offers actions to
    UI/tools/replay, and commits accepted capture/default/scene requests.
  Composition boundary: Stateless sequencing code that connects domain owners
    without taking ownership of their state or decisions.
  Editor arbitration: Deterministic merge that keeps the canonical legacy lane
    first, coalesces exact duplicate injected intent, and rejects conflicts.
  Input turn result: Value-only process request emitted after semantic actions
    are interpreted; Run applies process-wide policy without rescanning input.

Invariants:
  - No borrowed owner is retained after the input coordinator returns.
  - The coordinator is called once per rendered frame, after automation injection.
  - InputRouter remains the only owner of sampled device and semantic edge state.
  - Process requests contain no owner references and are consumed in the same
    frame immediately after ProcessInputFrame returns.

Related:
  - InputRouter.h owns input state and routing policy.
  - Run.h defines the direct coordinator/concrete delegation convention.
  - RunFrame.cpp owns top-level frame order.
  - Agentic/Reports/2026-07-11/runtime-shell-final-ownership-review.md owns the extraction.
*/
#pragma once

#include "../Input/InputRouter.h"
#include "ReplayRuntime.h"
#include "../../UI/UICommands.h"
#include "../../UI/UIInput.h"

namespace SkullbonezCore
{
namespace Core
{
class EngineConfig;
struct CinematicRenderConfig;
} // namespace Core
namespace Assets
{
class AssetSystem;
}
namespace Threading
{
class WorkerPool;
}
namespace Physics
{
class PhysicsDebugVisualizer;
}
namespace UI
{
class InGameUI;
}
namespace Runtime
{
class ApplicationExitState;
class AttachedCameraController;
class DiagnosticsRuntime;
class GraphicsStressController;
class RenderDefaultsStore;
class ReplayRuntime;
class RuntimeInteractionController;
class RuntimeRenderer;
class RuntimeTools;
class SimulationSystem;
struct RunEditorPlacementState;
struct SceneRequest;
struct CameraControlState;
struct OverlayDebugState;
struct RunLaunchOptions;
struct RunStartupState;
struct RunTimerState;
struct RuntimeViewModel;

struct RuntimeUIFrameResult
{
    SkullbonezCore::Core::SbResult status = SkullbonezCore::Core::SbResult::Success();
    ReplayWorkspaceOutput replayWorkspace;
    UI::InGameUICommands commands;
    bool suppressWorldActionThisFrame = false;
    bool frameActive = false;
    bool enterInteractiveScene = false;
    bool requestSceneStep = false; // One accepted paused-scene step for the later runtime-input snapshot.
    int editorUnhandledWheelDelta = 0;
};

// Value facts shared by UI sampling and command application during one input
// turn. Owner access remains in the narrow capability views passed separately.
struct RuntimeInputFrameFacts
{
    RunCameraMode replayCurrentCameraMode = RunCameraMode::Inspect;
    RunCameraMode replayRestoreCameraMode = RunCameraMode::Inspect;
    uint32_t cameraModeEnabledMask = 0u;
    bool suppressWorldActionThisFrame = false;
    int sceneObjectCapacity = 0;
    UiInputCaptureIntent externalUiCapture;

    // Previous completed secondary-surface frame. This value queue is consumed
    // synchronously and never retained by input orchestration.
    UI::OperatorEditorCommandQueues externalEditorCommands;

    // Invariant: only the selected Legacy surface may sample its pointer tools
    // or scene-authored stress actions during this input turn.
    bool legacyDevelopmentUiActive = true;
};

// Copies one sampled Runtime input turn into the passive UI-owned value. The
// returned snapshot retains no router, device-frame, or UI-owner reference.
inline UI::InputControl::UIInputSnapshot BuildUIInputSnapshot( const DeviceInputFrame& frame, const RuntimeMouseEdges& mouse,
                                                               UI::InputControl::UIPointerOverride pointerOverride )
{
    UI::InputControl::UIInputSnapshot snapshot;
    snapshot.keyWords = frame.keys.Words();
    snapshot.wheelDelta = frame.wheelDelta;

    if ( pointerOverride.enabled )
    {
        snapshot.mouseX = pointerOverride.x;
        snapshot.mouseY = pointerOverride.y;
    }
    else if ( frame.hasClientPosition )
    {
        snapshot.mouseX = frame.clientX;
        snapshot.mouseY = frame.clientY;
    }

    snapshot.leftDown = mouse.leftDown;
    snapshot.leftPressed = mouse.leftPressed;
    snapshot.leftReleased = mouse.leftReleased;
    return snapshot;
}

// Shared value-policy helpers used by the stateless coordinator and the
// InputRouter methods that commit accepted transitions.
struct KeyboardContextFacts
{
    bool keyboardUnblocked = false;
    bool scene = false;
    bool flyCamera = false;
    bool launcher = false;
    bool attachedCamera = false;
    bool director = false;
    bool directorAuthoring = false;
    bool editor = false;
    bool replayRestoreNotConsumed = false;
    bool uiNotInteracted = false;
};

RuntimeInputModeState BuildRuntimeInputModeState( RunCameraMode mode, const RunEditorPlacementState& editor,
                                                  const RuntimeInteractionGesture& gesture, bool attachActiveFollow,
                                                  bool directorGrabbed );
PointerPresentationPolicy EvaluateRuntimePointerPresentation( const InputRouter& inputRouter,
                                                              const RunEditorPlacementState& editor,
                                                              const ReplayInputView& replayInput );
RunCameraMode NormalizeRuntimeCameraMode( RunCameraMode mode, bool authoredScene, uint32_t enabledMask );

// Computes camera capabilities from value facts captured at the frame boundary;
// input policy cannot traverse scene lifecycle or world ownership.
uint32_t RuntimeCameraModeEnabledMask( bool authoredScene, int sceneEntityCount );
void EnterFlyModeCamera( InputRouter& inputRouter, CameraControlState& camera, Environment::CameraCollection& cameras,
                         bool authoredScene, const RunEditorPlacementState& editor, const ReplayInputView& replayInput );
void ExitFlyModeCamera( InputRouter& inputRouter, CameraControlState& camera, Environment::CameraCollection& cameras,
                        Geometry::Terrain& terrain, bool authoredScene );
RuntimeInputContextMask BuildKeyboardContextMask( const KeyboardContextFacts& facts );
bool IsReplayWorldOwner( WorldInteractionOwner owner );
bool IsEditorWorldOwner( WorldInteractionOwner owner );
const char* ReplayOwnerEventName( ReplayOwnerEventCode code );
uint32_t ReplaySceneRequestFlags( const SceneRequest& request );
void ReportRuntimeInputFailure( const SkullbonezCore::Core::SbResult& result );
struct InputFrameExecutionResult
{
    bool requestDevelopmentUiSurfaceSwap = false;
};

} // namespace Runtime
} // namespace SkullbonezCore
