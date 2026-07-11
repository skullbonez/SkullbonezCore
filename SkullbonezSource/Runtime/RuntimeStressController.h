/*
File: SkullbonezSource/Runtime/RuntimeStressController.h
Purpose:
  Exposes deterministic UI and graphics stress execution at an explicit owner boundary.

Mental model:
  Stress controllers own their random streams and counters. Each execution call
  borrows the concrete runtime owners it may deliberately perturb for one frame;
  no borrow is retained and Run only sequences the harness beside normal frame work.

Glossary:
  UI stress: Bounded command churn that exercises control-state transitions.
  Graphics stress: CLI-driven render, scene, and resource-lifetime churn.

Invariants:
  - Stress randomness advances only through its owning controller.
  - Execution borrows are synchronous and are never stored after the call.
  - Recoverable scene-load or renderer failures return through SbResult.

Related:
  - SkullbonezSource/Runtime/RuntimeStressController.cpp
  - SkullbonezSource/Runtime/GraphicsStressController.h
  - Agentic/Reference/comment-style-guide.md
*/
#pragma once

#include "../Core/SbResult.h"
#include "RuntimeCameraMode.h"

namespace SkullbonezCore
{
namespace Assets
{
class AssetSystem;
}
namespace Rendering
{
class IRenderDiagnostics;
}
namespace Threading
{
class WorkerPool;
}
namespace UI
{
class InGameUI;
}
namespace Physics
{
class PhysicsDebugVisualizer;
}
namespace Runtime::Audio
{
class ContactAudioService;
}
namespace Basics
{
class AttachedCameraController;
class DiagnosticsRuntime;
class EngineConfig;
class GraphicsStressController;
class InputRouter;
class ReplayRuntime;
class RuntimeInteractionController;
class RuntimeRenderer;
class RuntimeTools;
class SceneController;
class SimulationSystem;
class Window;
struct CinematicRenderConfig;
struct RunCameraState;
struct RunDebugState;
struct RunLaunchOptions;
struct RunRuntimeSettings;
struct RunStartupState;
struct RunTimerState;
struct RuntimeRenderBackendView;

SbResult RunUIStressActions( DiagnosticsRuntime& diagnosticsRuntime,
                             Window* window,
                             RunTimerState& timers,
                             UI::InGameUI& ui,
                             RunRuntimeSettings& runtimeSettings,
                             RuntimeRenderBackendView& renderBackendView,
                             RunDebugState& debug,
                             SceneController& sceneController,
                             RunCameraState& camera,
                             EngineConfig& config,
                             SimulationSystem& simulation,
                             RuntimeTools& runtimeTools,
                             const RunLaunchOptions& launchOptions,
                             const RunStartupState& startup,
                             ReplayRuntime& replayRuntime,
                             InputRouter& inputRouter,
                             RuntimeInteractionController& interaction,
                             AttachedCameraController& attachedCamera,
                             RunCameraMode replayRestoreCameraMode );

void ExecuteGraphicsStressFrame( GraphicsStressController& stress,
                                 Window* window,
                                 EngineConfig& config,
                                 RunLaunchOptions& launchOptions,
                                 CinematicRenderConfig& defaultCinematicRender,
                                 const RunStartupState& startup,
                                 DiagnosticsRuntime& diagnosticsRuntime,
                                 RunRuntimeSettings& runtimeSettings,
                                 RunTimerState& timers,
                                 Assets::AssetSystem& assets,
                                 Threading::WorkerPool& workerPool,
                                 InputRouter& inputRouter,
                                 RuntimeInteractionController& interaction,
                                 RunCameraState& camera,
                                 AttachedCameraController& attachedCamera,
                                 SimulationSystem& simulation,
                                 ReplayRuntime& replayRuntime,
                                 Runtime::Audio::ContactAudioService& contactAudio,
                                 UI::InGameUI& ui,
                                 RunDebugState& debug,
                                 RuntimeTools& runtimeTools,
                                 Physics::PhysicsDebugVisualizer& physicsDebugVisualizer,
                                 RuntimeRenderBackendView& renderBackendView,
                                 RuntimeRenderer& renderer,
                                 SceneController& sceneController,
                                 int& perfPass,
                                 const Rendering::IRenderDiagnostics& renderDiagnostics );
} // namespace Basics
} // namespace SkullbonezCore
