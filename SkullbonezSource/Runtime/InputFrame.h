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
