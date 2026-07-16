/*
File: SkullbonezSource/Runtime/Render/RuntimeRenderHost.h
Purpose:
  Defines stable and frame-scoped owner views plus active-backend capabilities
  consumed by RuntimeRenderer.

Summary:
  Run constructs stable world/scene views once and replay/tool views per frame.
  RuntimeRenderer retains only render-domain owners, then receives immutable
  frame facts plus synchronous domain borrows for submission.

Glossary:
  Owner view: Named set of lifetime-stable borrows for one render domain.
  Render backend view: Borrowed active renderer capabilities published by the
    composition root; null pointers mean the backend is not available.
  Submission view: One-frame values sampled only after tool/replay owners have
    completed their bounded draw records.
  Shader development facet: Optional backend capability for the explicit,
    offline-DXC manual reload transaction.

Invariants:
  - RuntimeRenderer owns renderer scratch state that should not leak back into
    Run.h, including DXR reflection instance transforms.
  - All owner-view references must outlive RuntimeRenderer and its passes.
  - A frame view is read-only after construction and is never cached by a pass.

Related:
  - SkullbonezSource/Runtime/Render/RuntimeRenderer.h
  - SkullbonezSource/Runtime/Render/RuntimeRenderPasses.h
  - Agentic/Reports/2026-07-11/runtime-shell-final-ownership-review.md
*/
#pragma once

#include "../../Core/Common.h"
#include "../../Maths/Matrix4.h"
#include "../../Maths/Vector3.h"
#include "../../Rendering/Shadow.h"
#include "../Replay/ReplayPresentation.h"
#include "../Tools/RuntimeTools.h"

#include <array>
#include <cstdint>
#include <memory>

namespace SkullbonezCore
{
namespace Core
{
class Profiler;
class EngineConfig;
struct CinematicRenderConfig;
} // namespace Core
namespace Assets
{
class AssetSystem;
}
namespace Environment
{
class CameraCollection;
class WorldEnvironment;
} // namespace Environment
namespace Geometry
{
class SkyBox;
class Terrain;
} // namespace Geometry
namespace Physics
{
class BroadphaseVisualizer;
class CollisionVisualizer;
class PhysicsDebugVisualizer;
} // namespace Physics
namespace Rendering
{
class IRenderCaptureBackend;
class IRenderCommandContext;
class IRenderDeviceLifecycle;
class IRenderDiagnostics;
class IRenderResourceFactory;
class IRenderRayTracing;
class IRenderShaderDevelopment;
} // namespace Rendering
namespace Textures
{
class TextureCollection;
}
namespace UI
{
class InGameUI;
}
namespace Runtime
{
class Window;
class LauncherLaser;
class RuntimeInputContext;
class RuntimeOverlayRenderResources;
class SceneController;
class SceneTerrain;
enum class RunCameraMode;
struct RenderFrameContext;
struct ReplayPresentationSample;
struct ReplaySolverFrameSample;
struct RunCameraState;
struct RunDebugState;
struct RunEditorPlacementState;
struct RunLaunchOptions;
struct RunMousePickupState;
struct RunRayCastTestState;
struct RunReplayPredictionFrame;
struct RuntimeRenderPassResources;
struct RunSceneBrowserState;
struct RunSceneState;
struct RunTimerState;
struct RuntimeRenderModelFrameView;
struct RuntimeViewModel;

// Concept: RuntimeRenderer retains this immutable-at-the-boundary world view.
// Each reference identifies one render-domain owner that outlives the renderer;
// callers cannot replace bindings after construction.
struct RenderWorldView
{
    Assets::AssetSystem& assets;
    Environment::CameraCollection& cameras;
    SceneTerrain& terrain;
    Window& window;
    SkullbonezCore::Core::EngineConfig& config;
    Environment::WorldEnvironment& worldEnvironment;
    RuntimeOverlayRenderResources& overlayResources;
    SkullbonezCore::Core::Profiler* profiler = nullptr;
};

struct RenderSceneView
{
    SceneController& sceneController;
    RunSceneBrowserState& sceneBrowser;
};

struct RenderReplayOverlayView
{
    const ReplayRenderFrameView& replayFrame;
};

struct RenderToolOverlayView
{
    RuntimeTools& tools;
    bool editorOverlayWorkVisible = false;
    bool inspectGizmoInteractionActive = false;
    bool controlDown = false;
    int attachedTargetIndex = -1;
    bool attachedFollow = false;
};

struct RuntimeRenderBackendView
{
    Rendering::IRenderDeviceLifecycle* deviceLifecycle = nullptr;     // Startup/present/resize/drain capability.
    Rendering::IRenderCommandContext* renderCommands = nullptr;       // Per-frame draw-state and submission capability.
    Rendering::IRenderResourceFactory* renderResources = nullptr;     // Resource creation/rebuild capability.
    Rendering::IRenderDiagnostics* renderDiagnostics = nullptr;       // Capability, draw-trace, timer, and memory snapshots.
    Rendering::IRenderCaptureBackend* captureBackend = nullptr;       // Screenshot/readback capability.
    Rendering::IRenderRayTracing* rayTracingBackend = nullptr;        // Optional DXR facet borrowed from the active renderer.
    Rendering::IRenderShaderDevelopment* shaderDevelopment = nullptr; // Optional manual offline-DXC reload capability.

    // Returns the startup-required capture facet or terminates through Lane F.
    Rendering::IRenderCaptureBackend& RequireCaptureBackend() const;
};

} // namespace Runtime
} // namespace SkullbonezCore
