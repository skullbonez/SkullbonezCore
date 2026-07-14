/*
File: InputFrame.h
Purpose:
  Declares the stateless once-per-frame input orchestration boundary.

Summary:
  Run owns top-level frame order and calls ProcessInputFrame once. The function
  receives non-copyable frame views for that turn, while InputRouter alone
  retains device, semantic-action, focus, and pointer-presentation state between
  frames.

Glossary:
  Input turn: Ordered frame interval that samples hardware, offers actions to
    UI/tools/replay, and commits accepted capture/default/scene requests.
  Composition boundary: Stateless sequencing code that connects domain owners
    without taking ownership of their state or decisions.

Invariants:
  - No frame view or referenced owner is retained after ProcessInputFrame returns.
  - The function is called once per rendered frame, after automation injection.
  - InputRouter remains the only owner of sampled device and semantic edge state.

Related:
  - InputRouter.h owns input state and routing policy.
  - RuntimeFrameViews.h defines the stack-only borrow convention.
  - RunFrame.cpp owns top-level frame order.
  - Agentic/Reports/2026-07-11/runtime-shell-final-ownership-review.md owns the extraction.
*/
#pragma once

#include "InputRouter.h"
#include "Replay/ReplayRuntime.h"
#include "RuntimeFrameViews.h"
#include "../UI/UICommands.h"

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
namespace Runtime
{
namespace Audio
{
class ContactAudioService;
}
} // namespace Runtime
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
class SceneController;
class SimulationSystem;
struct RunEditorPlacementState;
struct SceneRequest;
struct RunCameraState;
struct RunDebugState;
struct RunLaunchOptions;
struct RunStartupState;
struct RunTimerState;
struct RuntimeRenderBackendView;
struct RuntimeViewModel;

struct RuntimeUIFrameResult
{
    SkullbonezCore::Core::SbResult status = SkullbonezCore::Core::SbResult::Success();
    ReplayWorkspaceOutput replayWorkspace;
    UI::InGameUICommands commands;
    bool suppressWorldActionThisFrame = false;
    bool frameActive = false;
    bool enterInteractiveScene = false;
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
    int gameModelCapacity = 0;
};

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

RuntimeInputModeState BuildRuntimeInputModeState( RunCameraMode mode,
                                                  const RunEditorPlacementState& editor,
                                                  const RuntimeInteractionGesture& gesture,
                                                  bool attachActiveFollow,
                                                  bool directorGrabbed );
PointerPresentationPolicy EvaluateRuntimePointerPresentation( const InputRouter& inputRouter,
                                                              const RunEditorPlacementState& editor,
                                                              const ReplayRuntime& replayRuntime );
RunCameraMode NormalizeRuntimeCameraMode( RunCameraMode mode, bool authoredScene, uint32_t enabledMask );
uint32_t RuntimeCameraModeEnabledMask( const SceneController& sceneController );
void EnterFlyModeCamera( InputRouter& inputRouter,
                         RunCameraState& camera,
                         Environment::CameraCollection& cameras,
                         bool authoredScene,
                         const RunEditorPlacementState& editor,
                         const ReplayRuntime& replayRuntime );
void ExitFlyModeCamera( InputRouter& inputRouter,
                        RunCameraState& camera,
                        Environment::CameraCollection& cameras,
                        Geometry::Terrain& terrain,
                        bool authoredScene );
RuntimeInputContextMask BuildKeyboardContextMask( const KeyboardContextFacts& facts );
bool IsReplayWorldOwner( WorldInteractionOwner owner );
bool IsEditorWorldOwner( WorldInteractionOwner owner );
RuntimeWorkspace WorkspaceForWorldInteractionOwner( RuntimeWorkspace fallback, WorldInteractionOwner owner );
const char* ReplayOwnerEventName( ReplayOwnerEventCode code );
uint32_t ReplaySceneRequestFlags( const SceneRequest& request );
void ReportRuntimeInputFailure( const SkullbonezCore::Core::SbResult& result );
RuntimeUIFrameResult BeginRuntimeUIFrame( RuntimeFrameHostView& host,
                                          RuntimeFrameInteractionView& interactionOwners,
                                          RuntimeFrameSceneView& sceneOwners,
                                          const ReplayPathPickInput& replayPointerRay,
                                          const RuntimeInputFrameFacts& facts );
RuntimeUIFrameResult ApplyRuntimeUIFrameCommands( RuntimeUIFrameResult result,
                                                  bool keyboardToggleEditorMode,
                                                  RuntimeFrameHostView& host,
                                                  RuntimeFrameInteractionView& interactionOwners,
                                                  RuntimeFrameSceneView& sceneOwners,
                                                  RuntimeFramePresentationView& presentationOwners,
                                                  const RuntimeInputFrameFacts& facts );
RuntimeUIFrameResult FinishRuntimeUIFramePointer( RuntimeUIFrameResult result,
                                                  RuntimeFrameInteractionView& interactionOwners,
                                                  RuntimeFrameSceneView& sceneOwners,
                                                  RunCameraMode replayCurrentCameraMode );

// Executes one input turn through synchronous concrete-owner borrows. This is
// composition, not an owner: all durable input state remains in inputRouter.
void ProcessInputFrame( RuntimeFrameHostView& host,
                        RuntimeFrameInteractionView& interactionOwners,
                        RuntimeFrameSceneView& sceneOwners,
                        RuntimeFramePresentationView& presentationOwners );
} // namespace Runtime
} // namespace SkullbonezCore
