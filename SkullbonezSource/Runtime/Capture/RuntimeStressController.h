/*
File: SkullbonezSource/Runtime/Capture/RuntimeStressController.h
Purpose:
  Exposes deterministic UI and graphics stress execution at an explicit owner boundary.

Summary:
  Stress controllers own their random streams and counters. Each execution call
  borrows the concrete runtime owners through non-copyable frame views; no borrow
  is retained and Run only sequences the harness beside normal frame work.

Glossary:
  UI stress: Bounded command churn that exercises control-state transitions.
  Graphics stress: CLI-driven render, scene, and resource-lifetime churn.

Invariants:
  - Stress randomness advances only through its owning controller.
  - Execution borrows are synchronous and are never stored after the call.
  - Recoverable scene-load or renderer failures return through SkullbonezCore::Core::SbResult.

Related:
  - SkullbonezSource/Runtime/Capture/RuntimeStressController.cpp
  - SkullbonezSource/Runtime/Capture/GraphicsStressController.h
  - Agentic/Reference/comment-style-guide.md
*/
#pragma once

#include "../../Core/SbResult.h"
#include "../Camera/RuntimeCameraMode.h"
#include "../RuntimeFrameViews.h"

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
namespace Rendering
{
class Dx12Diagnostics;
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
namespace Runtime
{
class AttachedCameraController;
class DiagnosticsRuntime;
class GraphicsStressController;
class InputRouter;
class ReplayRuntime;
class RuntimeInteractionController;
class RuntimeRenderer;
class RuntimeTools;
class SceneController;
class SimulationSystem;
class Window;
struct CameraControlState;
struct OverlayDebugState;
struct RunLaunchOptions;
struct RunStartupState;
struct RunTimerState;

SkullbonezCore::Core::SbResult RunUIStressActions( RuntimeFrameHostView& host,
                                                   RuntimeFrameInteractionView& interactionOwners,
                                                   RuntimeFrameSceneView& sceneOwners, RuntimeRenderer& renderer,
                                                   ReplayRuntime& replayRuntime, RunCameraMode replayRestoreCameraMode );

} // namespace Runtime
} // namespace SkullbonezCore
