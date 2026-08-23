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
  Composition boundary: Stateless sequencing code that connects domain owners
    without taking ownership of their state or decisions.
  Editor arbitration: Deterministic merge that keeps the canonical GameUI lane
    first, coalesces exact duplicate injected intent, and rejects conflicts.

Invariants:
  - No borrowed owner is retained after the input coordinator returns.
  - The coordinator is called once per rendered frame, after automation injection.
  - InputRouter remains the only owner of sampled device and semantic edge state.
  - Process requests contain no owner references and are consumed in the same
    frame immediately after RunInputPhase returns.

Related:
  - InputRouter.h owns input state and routing policy.
  - Run.h defines the direct coordinator/concrete delegation convention.
  - RunFrame.cpp owns top-level frame order.
  - Agentic/Reference/engine-glossary.md
*/
#pragma once

#include "../Input/InputRouter.h"
#include "../Input/InputFrameValues.h"
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
class RuntimeFrameMetricsOwner;
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

RuntimeInputModeState BuildRuntimeInputModeState( RunCameraMode mode, const RunEditorPlacementState& editor,
                                                  const RuntimeInteractionGesture& gesture, bool attachActiveFollow,
                                                  bool directorGrabbed );
PointerPresentationPolicy EvaluateRuntimePointerPresentation( const InputRouter& inputRouter,
                                                              const RunEditorPlacementState& editor,
                                                              const ReplayInputView& replayInput );
RunCameraMode NormalizeRuntimeCameraMode( RunCameraMode mode, bool authoredScene, uint32_t enabledMask );
inline RuntimeInteractionTransition EnterInteractionForCameraMode( RuntimeInteractionController& interaction,
                                                                   RunCameraMode mode )
{
    // Camera owns the user-facing mode while Interaction owns workspace and
    // gesture cleanup. App is the only owner allowed to translate between them.
    switch ( mode )
    {
    case RunCameraMode::Demo:
    case RunCameraMode::Scene:
    case RunCameraMode::Director:
        return interaction.EnterLive();
    case RunCameraMode::Inspect:
    case RunCameraMode::Attach:
        return interaction.EnterInspect();
    case RunCameraMode::Launcher:
        return interaction.EnterLauncher();
    case RunCameraMode::Manipulator:
        return interaction.EnterManipulator();
    case RunCameraMode::Count:
        break;
    }

    return interaction.EnterLive();
}

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
void ReportRuntimeInputFailure( const SkullbonezCore::Core::SbResult& result );
RuntimeUIFrameResult BeginRuntimeUIFrame( SkullbonezCore::Core::SbDiagnosticStore& diagnostics, Window& window,
                                          InputRouter& inputRouter, CameraControlState& camera, RuntimeTools& runtimeTools,
                                          AttachedCameraController& attachedCamera,
                                          RuntimeInteractionController& interaction, UI::InGameUI& ui,
                                          RuntimeFrameMetricsOwner& timers, SceneController& sceneController,
                                          ReplayRuntime& replayRuntime, const ReplayPathPickInput& replayPointerRay,
                                          const RuntimeInputFrameFacts& facts );
RuntimeUIFrameResult FinishRuntimeUIFramePointer( RuntimeUIFrameResult result, InputRouter& inputRouter,
                                                  CameraControlState& camera, RuntimeTools& runtimeTools,
                                                  RuntimeInteractionController& interaction,
                                                  AttachedCameraController& attachedCamera, UI::InGameUI& ui,
                                                  SceneController& sceneController, ReplayRuntime& replayRuntime,
                                                  RunCameraMode replayCurrentCameraMode );
} // namespace Runtime
} // namespace SkullbonezCore
