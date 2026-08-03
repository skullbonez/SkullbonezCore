/*
File: SkullbonezSource/Runtime/Capture/RuntimeStressController.h
Purpose:
  Declares concrete graphics-stress operations used by the render phase.

Summary:
  Graphics stress is split into deterministic planning, descriptor churn,
  bounded action groups, and diagnostics. Run remains the ordered phase
  coordinator and supplies only the concrete owners each operation needs.
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
  - RuntimeStressController.cpp
  - ../App/RunFrame.cpp
  - GraphicsStressController.h
  - Agentic/Reference/comment-style-guide.md
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
class ReplayRuntime;
class RuntimeTools;
class SimulationSystem;
class Window;
class DiagnosticsRuntime;
struct RunLaunchOptions;
struct RunTimerState;
struct CameraControlState;

struct GraphicsStressSceneLoadPlan
{
    SceneLoadRequest request;
    bool scheduled = false;
    int selectedSceneIndex = -1;
    const char* selectedSceneSource = "none";
};

bool PrepareGraphicsStressChurn( GraphicsStressController& stress, Window& window, RuntimeRenderer& renderer,
                                 const Rendering::Dx12Diagnostics& renderDiagnostics );
GraphicsStressSceneLoadPlan PlanGraphicsStressSceneLoad( GraphicsStressController& stress, SceneController& sceneController,
                                                         UI::InGameUI& ui );
void ApplyGraphicsStressPresentationAction( int action, GraphicsStressController& stress, const Assets::AssetSystem& assets,
                                            RunLaunchOptions& launchOptions, Core::EngineConfig& config,
                                            RuntimeOverlayDiagnostics& overlays, SceneController& sceneController,
                                            RunTimerState& timers, UI::InGameUI& ui,
                                            const Core::CinematicRenderConfig& defaultCinematicRender,
                                            RuntimeRenderer& renderer );
void ApplyGraphicsStressRuntimeAction( int action, GraphicsStressController& stress, RunLaunchOptions& launchOptions,
                                       RuntimeOverlayDiagnostics& overlays, SceneController& sceneController,
                                       CameraControlState& camera, UI::InGameUI& ui, SimulationSystem& simulation,
                                       RuntimeTools& runtimeTools, ReplayRuntime& replayRuntime );
void FinishGraphicsStressFrame( GraphicsStressController& stress, DiagnosticsRuntime& diagnosticsRuntime,
                                RunTimerState& timers, SceneController& sceneController, ReplayRuntime& replayRuntime,
                                const Rendering::Dx12Diagnostics& renderDiagnostics );
} // namespace Runtime
} // namespace SkullbonezCore
