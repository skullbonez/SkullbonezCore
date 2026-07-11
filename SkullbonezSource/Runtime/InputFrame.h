/*
File: InputFrame.h
Purpose:
  Declares the stateless once-per-frame input orchestration boundary.

Mental model:
  Run owns top-level frame order and calls ProcessInputFrame once. The function
  borrows concrete owners for that turn, while InputRouter alone retains device,
  semantic-action, focus, and pointer-presentation state between frames.

Glossary:
  Input turn: Ordered frame interval that samples hardware, offers actions to
    UI/tools/replay, and commits accepted capture/default/scene requests.
  Composition boundary: Stateless sequencing code that connects domain owners
    without taking ownership of their state or decisions.

Invariants:
  - No argument is retained after ProcessInputFrame returns.
  - The function is called once per rendered frame, after automation injection.
  - InputRouter remains the only owner of sampled device and semantic edge state.

Related:
  - InputRouter.h owns input state and routing policy.
  - RunFrame.cpp owns top-level frame order.
  - Agentic/Plans/TODO/runtime-shell-decomposition.md owns the extraction.
*/
#pragma once

#include "InputRouter.h"
#include "Replay/ReplayRuntime.h"
#include "../UI/UICommands.h"

namespace SkullbonezCore
{
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
namespace Basics
{
class ApplicationExitState;
class AttachedCameraController;
class DiagnosticsRuntime;
class EngineConfig;
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
struct CinematicRenderConfig;
struct RunCameraState;
struct RunDebugState;
struct RunLaunchOptions;
struct RunRuntimeSettings;
struct RunStartupState;
struct RunSubsystemState;
struct RunTimerState;
struct RuntimeRenderBackendView;
struct RuntimeViewModel;

struct RuntimeUIFrameResult
{
    SbResult status = SbResult::Success();
    ReplayRuntime::ReplayWorkspaceOutput replayWorkspace;
    UI::InGameUICommands commands;
    bool suppressWorldActionThisFrame = false;
    bool frameActive = false;
    bool enterInteractiveScene = false;
    int editorUnhandledWheelDelta = 0;
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
    bool replayRestoreNotConsumed = false;
    bool uiNotInteracted = false;
};

RuntimeInputModeState BuildRuntimeInputModeState( RunCameraMode mode,
                                                  const RunEditorPlacementState& editor,
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
void ReportRuntimeInputFailure( const SbResult& result );
RuntimeUIFrameResult BeginRuntimeUIFrame( RuntimeInputContext& runtimeInput,
                                          InputRouter& inputRouter,
                                          RunCameraState& camera,
                                          RuntimeTools& runtimeTools,
                                          ReplayRuntime& replayRuntime,
                                          const ReplayRuntime::PathPickInput& replayPointerRay,
                                          RunCameraMode replayCurrentCameraMode,
                                          RunCameraMode replayRestoreCameraMode,
                                          AttachedCameraController& attachedCamera,
                                          RuntimeInteractionController& interaction,
                                          RunTimerState& timers,
                                          SceneController& sceneController,
                                          RunSubsystemState& systems,
                                          UI::InGameUI& ui,
                                          uint32_t cameraModeEnabledMask,
                                          bool suppressWorldActionThisFrame );
RuntimeUIFrameResult ApplyRuntimeUIFrameCommands( RuntimeUIFrameResult result,
                                                  bool keyboardToggleEditorMode,
                                                  RuntimeInputContext& runtimeInput,
                                                  InputRouter& inputRouter,
                                                  RunCameraState& camera,
                                                  RuntimeTools& runtimeTools,
                                                  ReplayRuntime& replayRuntime,
                                                  RunCameraMode replayRestoreCameraMode,
                                                  uint32_t cameraModeEnabledMask,
                                                  AttachedCameraController& attachedCamera,
                                                  RuntimeInteractionController& interaction,
                                                  RunTimerState& timers,
                                                  RunDebugState& debug,
                                                  RunLaunchOptions& launchOptions,
                                                  RunRuntimeSettings& runtimeSettings,
                                                  EngineConfig& config,
                                                  SceneController& sceneController,
                                                  RunSubsystemState& systems,
                                                  SimulationSystem& simulation,
                                                  Runtime::Audio::ContactAudioService& contactAudio,
                                                  RuntimeRenderBackendView& renderBackendView,
                                                  RenderDefaultsStore& renderDefaults,
                                                  CinematicRenderConfig& defaultCinematicRender,
                                                  int gameModelCapacity );
RuntimeUIFrameResult FinishRuntimeUIFramePointer( RuntimeUIFrameResult result,
                                                  RuntimeInputContext& runtimeInput,
                                                  InputRouter& inputRouter,
                                                  RunCameraState& camera,
                                                  RuntimeTools& runtimeTools,
                                                  ReplayRuntime& replayRuntime,
                                                  RunCameraMode replayCurrentCameraMode,
                                                  AttachedCameraController& attachedCamera,
                                                  SceneController& sceneController,
                                                  UI::InGameUI& ui );

// Executes one input turn through synchronous concrete-owner borrows. This is
// composition, not an owner: all durable input state remains in inputRouter.
void ProcessInputFrame( InputRouter& inputRouter,
                        EngineConfig& config,
                        RunLaunchOptions& launchOptions,
                        ApplicationExitState& applicationExit,
                        CinematicRenderConfig& defaultCinematicRender,
                        RenderDefaultsStore& renderDefaults,
                        const RunStartupState& startup,
                        DiagnosticsRuntime& diagnosticsRuntime,
                        RunRuntimeSettings& runtimeSettings,
                        RunTimerState& timers,
                        RunSubsystemState& systems,
                        RuntimeInteractionController& interaction,
                        RunCameraState& camera,
                        AttachedCameraController& attachedCamera,
                        SimulationSystem& simulation,
                        ReplayRuntime& replayRuntime,
                        Runtime::Audio::ContactAudioService& contactAudio,
                        UI::InGameUI& ui,
                        RunDebugState& debug,
                        GraphicsStressController& graphicsStress,
                        RuntimeTools& runtimeTools,
                        Physics::PhysicsDebugVisualizer& physicsDebugVisualizer,
                        RuntimeViewModel& runtimeViewModel,
                        RuntimeRenderBackendView& renderBackendView,
                        RuntimeRenderer& renderer,
                        SceneController& sceneController,
                        int& perfPass );
} // namespace Basics
} // namespace SkullbonezCore
