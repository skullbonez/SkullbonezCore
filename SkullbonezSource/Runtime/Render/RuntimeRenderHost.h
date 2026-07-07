/*
File: SkullbonezSource/Runtime/Render/RuntimeRenderHost.h
Purpose:
  Names the startup bindings for runtime render passes.

Mental model:
  RuntimeRenderer owns pass order and pass objects. Renderer dependencies travel
  through named startup bindings while Run stays the composition root.

Glossary:
  Binding: Pointer set that connects RuntimeRenderer to current runtime owners.
  Render backend view: Borrowed active renderer capabilities published by the
    composition root; null pointers mean the backend is not available.
  DXR reflection transform buffer: Host-owned per-frame scratch matrix data
    streamed from the scene view into the DX12 TLAS build.

Invariants:
  - RuntimeRenderer owns renderer scratch state that should not leak back into
    Run.h, including DXR reflection instance transforms.
  - All references must outlive RuntimeRenderer and its passes.

Related:
  - SkullbonezSource/Runtime/Render/RuntimeRenderer.h
  - SkullbonezSource/Runtime/Render/RuntimeRenderPasses.h
  - Agentic/Plans/runtime-run-decomposition-plan.md
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

namespace SkullbonezCore
{
namespace Environment
{
class WorldEnvironment;
}
namespace Physics
{
class BroadphaseVisualizer;
class CollisionVisualizer;
class PhysicsDebugVisualizer;
} // namespace Physics
namespace Rendering
{
class IRenderBackend;
class IRenderCaptureBackend;
class IRenderCommandContext;
class IRenderDeviceLifecycle;
class IRenderDiagnostics;
class IRenderResourceFactory;
class IRenderRayTracing;
} // namespace Rendering
namespace UI
{
class InGameUI;
}
namespace Basics
{
class Profiler;
class LauncherLaser;
class RuntimeInputContext;
class SceneController;
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
struct RunSceneBrowserState;
struct RunSceneState;
struct RunSubsystemState;
struct RunTimerState;
struct RuntimeRenderModelFrameView;
class ReplayRuntime;
struct RuntimeViewModel;

// Concept: Render passes borrow grouped views from the runtime shell. The groups
// make new dependencies choose an owning view instead of growing one flat bag.
struct RenderRuntimeView
{
    RunSubsystemState* systems = nullptr;
    EngineConfig* config = nullptr;
    const RunLaunchOptions* launchOptions = nullptr;
    RunRuntimeSettings* runtimeSettings = nullptr;
};

struct RenderWorldView
{
    Environment::WorldEnvironment* worldEnvironment = nullptr;
    Physics::CollisionVisualizer* collisionVisualizer = nullptr;
    Physics::BroadphaseVisualizer* broadphaseVisualizer = nullptr;
    Physics::PhysicsDebugVisualizer* physicsDebugVisualizer = nullptr;
};

struct RenderSceneView
{
    SceneController* sceneController = nullptr;
    RunSceneBrowserState* sceneBrowser = nullptr;
};

struct RenderReplayOverlayView
{
    ReplayRuntime* replayRuntime = nullptr;
};

struct RenderToolOverlayView
{
    RuntimeTools* tools = nullptr;
};

struct RenderUiView
{
    UI::InGameUI* ui = nullptr;
    RuntimeInputContext* runtimeInput = nullptr;
    RunCameraState* camera = nullptr;
    RuntimeViewModel* runtimeViewModel = nullptr;
};

struct RenderDiagnosticsView
{
    RunDebugState* debug = nullptr;
    RunTimerState* timers = nullptr;
    Profiler* profiler = nullptr;                                 // Startup-bound diagnostics source; null in non-profile builds.
};

struct RuntimeRenderBackendView
{
    Rendering::IRenderBackend* renderBackend = nullptr;           // Compatibility aggregate borrow for legacy runtime callers.
    Rendering::IRenderDeviceLifecycle* deviceLifecycle = nullptr; // Startup/present/resize/drain capability.
    Rendering::IRenderCommandContext* renderCommands = nullptr;   // Per-frame draw-state and submission capability.
    Rendering::IRenderResourceFactory* renderResources = nullptr; // Resource creation/rebuild capability.
    Rendering::IRenderDiagnostics* renderDiagnostics = nullptr;   // Capability, draw-trace, timer, and memory snapshots.
    Rendering::IRenderCaptureBackend* captureBackend = nullptr;   // Screenshot/readback capability.
    Rendering::IRenderRayTracing* rayTracingBackend = nullptr;    // Optional DXR facet borrowed from the active renderer.
};

// Concept: RuntimeRendererBindings is the startup borrow set for pass owners.
// Owner: RuntimeRenderer. Reason: Run is still the composition root, but render
// pass construction needs explicit long-lived owners instead of a browsable host.
// Deletion condition: replace each borrowed owner with a domain renderer/pass
// owner as plans 01/03/04 drain. Checker budget: bindings may be populated only
// by Run startup code and must not grow per-frame draw decisions.
struct RuntimeRendererBindings
{
    RuntimeRenderBackendView backend;                             // Stable backend capability pointers captured at Run startup.
    RenderRuntimeView runtime;
    RenderWorldView world;
    RenderSceneView scene;
    RenderReplayOverlayView replayOverlay;
    RenderToolOverlayView toolOverlay;
    RenderUiView ui;
    RenderDiagnosticsView diagnostics;
};

} // namespace Basics
} // namespace SkullbonezCore
