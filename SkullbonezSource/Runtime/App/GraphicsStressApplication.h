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

#include <cstdint>

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

enum class GraphicsStressActionOwner : uint8_t
{
    Invalid,
    Cinematic,
    SceneBrowser,
    Renderer,
    PresentationOverlay,
    TimeScale,
    World,
    GeneratedScene,
    Tornado,
    OperatorUi,
    ScenePhysics,
    RuntimeTool,
    Camera,
    RuntimeOverlay
};

constexpr GraphicsStressActionOwner GraphicsStressOwnerForAction( int action )
{
    if ( action < 0 || action >= 32 )
    {
        return GraphicsStressActionOwner::Invalid;
    }
    if ( action <= 2 )
    {
        return GraphicsStressActionOwner::Cinematic;
    }
    if ( action == 3 )
    {
        return GraphicsStressActionOwner::SceneBrowser;
    }
    if ( action <= 5 )
    {
        return GraphicsStressActionOwner::Renderer;
    }
    if ( action <= 14 )
    {
        return GraphicsStressActionOwner::PresentationOverlay;
    }
    if ( action == 15 )
    {
        return GraphicsStressActionOwner::TimeScale;
    }
    if ( action == 16 )
    {
        return GraphicsStressActionOwner::World;
    }
    if ( action == 18 || action == 28 )
    {
        return GraphicsStressActionOwner::GeneratedScene;
    }
    if ( action == 19 || action == 20 )
    {
        return GraphicsStressActionOwner::Tornado;
    }
    if ( action == 17 || action == 21 || action == 29 || action == 30 )
    {
        return GraphicsStressActionOwner::OperatorUi;
    }
    if ( action == 22 || action == 23 )
    {
        return GraphicsStressActionOwner::ScenePhysics;
    }
    if ( action == 26 )
    {
        return GraphicsStressActionOwner::RuntimeTool;
    }
    if ( action == 27 )
    {
        return GraphicsStressActionOwner::Camera;
    }
    return GraphicsStressActionOwner::RuntimeOverlay;
}

bool PrepareGraphicsStressChurn( GraphicsStressController& stress, Window& window, RuntimeRenderer& renderer,
                                 const Rendering::Dx12Diagnostics& renderDiagnostics );
GraphicsStressSceneLoadPlan PlanGraphicsStressSceneLoad( GraphicsStressController& stress, SceneController& sceneController,
                                                         UI::InGameUI& ui );
void ApplyGraphicsStressCinematicAction( int action, GraphicsStressController& stress, RunLaunchOptions& launchOptions,
                                         Core::EngineConfig& config, SceneController& sceneController );
void ApplyGraphicsStressSceneBrowserAction( GraphicsStressController& stress, const Assets::AssetSystem& assets,
                                            RunLaunchOptions& launchOptions, Core::EngineConfig& config,
                                            SceneController& sceneController, UI::InGameUI& ui,
                                            const Core::CinematicRenderConfig& defaultCinematicRender );
void ApplyGraphicsStressRendererAction( int action, RuntimeRenderer& renderer );
void ApplyGraphicsStressPresentationOverlayAction( int action, GraphicsStressController& stress,
                                                   RuntimeOverlayDiagnostics& overlays,
                                                   const RuntimeFrameMetricsSnapshot& timers );
void ApplyGraphicsStressTimeScaleAction( GraphicsStressController& stress, SceneController& sceneController,
                                         UI::InGameUI& ui, SimulationSystem& simulation );
GraphicsStressRuntimeActionResult ApplyGraphicsStressWorldAction( GraphicsStressController& stress,
                                                                  SceneController& sceneController );
void ApplyGraphicsStressGeneratedSceneAction( GraphicsStressController& stress, RunLaunchOptions& launchOptions );
void ApplyGraphicsStressTornadoAction( int action, GraphicsStressController& stress, SceneController& sceneController );
void ApplyGraphicsStressOperatorUiAction( int action, GraphicsStressController& stress, UI::InGameUI& ui );
void ApplyGraphicsStressScenePhysicsAction( int action, SceneController& sceneController, SimulationSystem& simulation );
void ApplyGraphicsStressRuntimeToolAction( RuntimeTools& runtimeTools );
void ApplyGraphicsStressCameraAction( GraphicsStressController& stress, CameraControlState& camera );
void ApplyGraphicsStressRuntimeOverlayAction( int action, GraphicsStressController& stress,
                                              RuntimeOverlayDiagnostics& overlays );
void FinishGraphicsStressFrame( GraphicsStressController& stress, DiagnosticsRuntime& diagnosticsRuntime,
                                const RuntimeFrameMetricsSnapshot& timers, SceneController& sceneController,
                                const Core::MainMemoryReplayStats& replayMemory,
                                const Rendering::Dx12Diagnostics& renderDiagnostics );
} // namespace Runtime
} // namespace SkullbonezCore
