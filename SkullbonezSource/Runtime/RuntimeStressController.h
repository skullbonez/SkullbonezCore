/*
File: SkullbonezSource/Runtime/RuntimeStressController.h
Purpose:
  Exposes deterministic UI and graphics stress execution at an explicit owner boundary.

Mental model:
  Stress controllers own their random streams and counters. Each execution call
  borrows the concrete runtime owners through non-copyable frame views; no borrow
  is retained and Run only sequences the harness beside normal frame work.

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
#include "RuntimeFrameViews.h"

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
struct RunStartupState;
struct RunTimerState;
struct RuntimeRenderBackendView;

SbResult RunUIStressActions( RuntimeFrameHostView& host,
                             RuntimeFrameInteractionView& interactionOwners,
                             RuntimeFrameSceneView& sceneOwners,
                             RuntimeFramePresentationView& presentationOwners,
                             RunCameraMode replayRestoreCameraMode );

void ExecuteGraphicsStressFrame( RuntimeFrameHostView& host,
                                 RuntimeFrameInteractionView& interactionOwners,
                                 RuntimeFrameSceneView& sceneOwners,
                                 RuntimeFramePresentationView& presentationOwners,
                                 const Rendering::IRenderDiagnostics& renderDiagnostics );
} // namespace Basics
} // namespace SkullbonezCore
