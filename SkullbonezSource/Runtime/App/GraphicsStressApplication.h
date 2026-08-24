/*
File: SkullbonezSource/Runtime/App/GraphicsStressApplication.h
Purpose:
  Declares App-owned application of deterministic graphics-stress policy.

Summary:
  Capture owns deterministic random state and cadence. App remains the ordered
  phase coordinator and supplies only the concrete owners each operation needs.
  These functions are synchronous stress transactions, not a replacement
  runtime owner. They borrow their operands for one call, retain none, and
  return only value decisions needed by the next render-phase step.

Glossary:
  Scene-load plan: Value-only request and logging facts chosen from the stress
    seed before Run performs the scene-load transaction.
  Quiet window: Descriptor-recreation interval in which unrelated random churn
    is suppressed so the descriptor baseline can be verified.

Invariants:
  - Every helper receives twelve or fewer concrete operands.
  - GraphicsStressController remains the sole owner of random sequence state.
  - Scene-load transactions remain ordered by Run's PrepareRenderPhase.
  - No helper retains a borrowed runtime owner or republishes a service bag.

Related:
  - GraphicsStressApplication.cpp
  - ../App/RunFrame.cpp
  - GraphicsStressController.h
*/
#pragma once

#include "../Scene/SceneLoadRequest.h"

namespace SkullbonezCore
{
namespace Assets
{
class AssetSystem;
}
namespace Core
{
class EngineConfig;
struct MainMemoryReplayStats;
struct CinematicRenderConfig;
} // namespace Core
namespace Rendering
{
class Dx12Diagnostics;
}
namespace UI
{
class InGameUI;
}
namespace Runtime
{
class GraphicsStressController;
class RuntimeOverlayDiagnostics;
class RuntimeRenderer;
class SceneController;
class RuntimeTools;
class SimulationSystem;
class Window;
class DiagnosticsRuntime;
struct RunLaunchOptions;
struct RuntimeFrameMetricsSnapshot;
struct CameraControlState;

struct GraphicsStressSceneLoadPlan
{
    SceneLoadRequest request;
    bool scheduled = false;
    int selectedSceneIndex = -1;
    const char* selectedSceneSource = "none";
};

struct GraphicsStressRuntimeActionResult
{
    float previousGravity = 0.0f;
    float previousFluidHeight = 0.0f;
    float previousFluidDensity = 0.0f;
    float gravity = 0.0f;
    float fluidHeight = 0.0f;
    float fluidDensity = 0.0f;
    bool worldOverrideChanged = false;
};

bool PrepareGraphicsStressChurn( GraphicsStressController& stress, Window& window, RuntimeRenderer& renderer,
                                 const Rendering::Dx12Diagnostics& renderDiagnostics );
GraphicsStressSceneLoadPlan PlanGraphicsStressSceneLoad( GraphicsStressController& stress, SceneController& sceneController,
                                                         UI::InGameUI& ui );
void ApplyGraphicsStressPresentationAction( int action, GraphicsStressController& stress, const Assets::AssetSystem& assets,
                                            RunLaunchOptions& launchOptions, Core::EngineConfig& config,
                                            RuntimeOverlayDiagnostics& overlays, SceneController& sceneController,
                                            const RuntimeFrameMetricsSnapshot& timers, UI::InGameUI& ui,
                                            const Core::CinematicRenderConfig& defaultCinematicRender,
                                            RuntimeRenderer& renderer );
GraphicsStressRuntimeActionResult
ApplyGraphicsStressRuntimeAction( int action, GraphicsStressController& stress, RunLaunchOptions& launchOptions,
                                  RuntimeOverlayDiagnostics& overlays, SceneController& sceneController,
                                  CameraControlState& camera, UI::InGameUI& ui, SimulationSystem& simulation,
                                  RuntimeTools& runtimeTools );
void FinishGraphicsStressFrame( GraphicsStressController& stress, DiagnosticsRuntime& diagnosticsRuntime,
                                const RuntimeFrameMetricsSnapshot& timers, SceneController& sceneController,
                                const Core::MainMemoryReplayStats& replayMemory,
                                const Rendering::Dx12Diagnostics& renderDiagnostics );
} // namespace Runtime
} // namespace SkullbonezCore
