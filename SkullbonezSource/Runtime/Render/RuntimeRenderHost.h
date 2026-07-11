/*
File: SkullbonezSource/Runtime/Render/RuntimeRenderHost.h
Purpose:
  Defines the five owner views and active-backend capabilities consumed by
  RuntimeRenderer.

Mental model:
  Run constructs five named views once. RuntimeRenderer borrows those concrete
  owners for its lifetime, then receives immutable frame facts for submission.

Glossary:
  Owner view: Named set of lifetime-stable borrows for one render domain.
  Render backend view: Borrowed active renderer capabilities published by the
    composition root; null pointers mean the backend is not available.
  Submission view: One-frame values sampled only after tool/replay owners have
    completed their bounded draw records.

Invariants:
  - RuntimeRenderer owns renderer scratch state that should not leak back into
    Run.h, including DXR reflection instance transforms.
  - All owner-view references must outlive RuntimeRenderer and its passes.
  - A frame view is read-only after construction and is never cached by a pass.

Related:
  - SkullbonezSource/Runtime/Render/RuntimeRenderer.h
  - SkullbonezSource/Runtime/Render/RuntimeRenderPasses.h
  - Agentic/Plans/TODO/runtime-shell-decomposition.md
*/
#pragma once

#include "../../Core/Common.h"
#include "../../Maths/Matrix4.h"
#include "../../Maths/Vector3.h"
#include "../../Rendering/Shadow.h"
#include "../../UI/UI.h"
#include "../Replay/ReplayRuntime.h"
#include "../Tools/RuntimeTools.h"

#include <array>
#include <cstdint>
#include <memory>

namespace SkullbonezCore
{
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
} // namespace Rendering
namespace Textures
{
class TextureCollection;
}
namespace UI
{
class InGameUI;
}
namespace Basics
{
class Window;
class Profiler;
class LauncherLaser;
class RuntimeInputContext;
class SceneController;
class SceneTerrain;
enum class RunCameraMode;
struct CinematicRenderConfig;
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
struct RunRuntimeSettings;
struct RunRenderPassResources;
struct RunSceneBrowserState;
struct RunSceneState;
struct RunTimerState;
struct RuntimeRenderModelFrameView;
class ReplayRuntime;
struct RuntimeViewModel;

// Concept: RuntimeRenderer receives five named, immutable-at-the-boundary
// views. Each pointer identifies one concrete owner that outlives the renderer;
// callers cannot replace bindings after construction.
struct RenderWorldView
{
    Assets::AssetSystem& assets;
    Environment::CameraCollection& cameras;
    SceneTerrain& terrain;
    Window& window;
    EngineConfig& config;
    RunRuntimeSettings& runtimeSettings;
    Environment::WorldEnvironment& worldEnvironment;
    Physics::CollisionVisualizer& collisionVisualizer;
    Physics::BroadphaseVisualizer& broadphaseVisualizer;
    Physics::PhysicsDebugVisualizer& physicsDebugVisualizer;
    RunDebugState& debug;
    RunTimerState& timers;
    Profiler* profiler = nullptr;
};

struct RenderSceneView
{
    SceneController& sceneController;
    RunSceneBrowserState& sceneBrowser;
};

struct RenderReplayOverlayView
{
    ReplayRuntime& replayRuntime;
    const SceneEntityStore& entities;
    bool scenePhysicsEnabled = false;
    int sceneFrame = 0;
    double frameSeconds = 0.0;
    double totalSeconds = 0.0;
};

struct RenderToolOverlayView
{
    RuntimeTools& tools;
    bool inspectGizmoInteractionActive = false;
    bool controlDown = false;
    int attachedTargetIndex = -1;
    bool attachedFollow = false;
};

struct RenderUiView
{
    UI::InGameUI& ui;
    RuntimeInputContext& runtimeInput;
    RunCameraState& camera;
};

struct RuntimeRenderBackendView
{
    Rendering::IRenderDeviceLifecycle* deviceLifecycle = nullptr; // Startup/present/resize/drain capability.
    Rendering::IRenderCommandContext* renderCommands = nullptr;   // Per-frame draw-state and submission capability.
    Rendering::IRenderResourceFactory* renderResources = nullptr; // Resource creation/rebuild capability.
    Rendering::IRenderDiagnostics* renderDiagnostics = nullptr;   // Capability, draw-trace, timer, and memory snapshots.
    Rendering::IRenderCaptureBackend* captureBackend = nullptr;   // Screenshot/readback capability.
    Rendering::IRenderRayTracing* rayTracingBackend = nullptr;    // Optional DXR facet borrowed from the active renderer.

    // Returns the startup-required capture facet or terminates through Lane F.
    Rendering::IRenderCaptureBackend& RequireCaptureBackend() const;
};

} // namespace Basics
} // namespace SkullbonezCore
