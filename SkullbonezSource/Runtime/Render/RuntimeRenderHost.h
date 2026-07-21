/*
File: SkullbonezSource/Runtime/Render/RuntimeRenderHost.h
Purpose:
  Defines stable and frame-scoped owner views plus concrete DX12 cold owners
  consumed by RuntimeRenderer.

Summary:
  Run constructs stable world/scene views once and replay/tool views per frame.
  RuntimeRenderer retains only render-domain owners, then receives immutable
  frame facts plus synchronous domain borrows for submission.

Glossary:
  Owner view: Named set of lifetime-stable borrows for one render domain.
  Render backend view: Borrowed concrete renderer owners published by the
    composition root; null pointers mean startup did not bind that owner.
  Submission view: One-frame values sampled only after tool/replay owners have
    completed their bounded draw records.
  Shader development owner: Concrete DX12 owner for the explicit,
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
class Dx12BackbufferCapture;
class IRenderCommandContext;
#if defined( SKULLBONEZ_DEVELOPMENT_TOOLS )
class Dx12ImGuiRendererOwner;
#endif
class IRenderDeviceLifecycle;
class IRenderDiagnostics;
class IRenderResourceFactory;
class Dx12RaytracingOwner;
class Dx12ShaderDevelopment;
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
    Rendering::IRenderDeviceLifecycle* deviceLifecycle = nullptr;       // Startup/present/resize/drain capability.
    Rendering::IRenderCommandContext* renderCommands = nullptr;         // Per-frame draw-state and submission capability.
    Rendering::IRenderResourceFactory* renderResources = nullptr;       // Resource creation/rebuild capability.
    Rendering::IRenderDiagnostics* renderDiagnostics = nullptr;         // Capability, draw-trace, timer, and memory snapshots.
    Rendering::Dx12BackbufferCapture* backbufferCapture = nullptr;      // Concrete screenshot/readback owner.
    Rendering::Dx12RaytracingOwner* raytracing = nullptr;               // Optional concrete reflection owner.
    Rendering::Dx12ShaderDevelopment* shaderDevelopment = nullptr;      // Explicit offline-DXC reload owner.
#if defined( SKULLBONEZ_DEVELOPMENT_TOOLS )
    Rendering::Dx12ImGuiRendererOwner* developmentUiRenderer = nullptr; // Narrow development-only DX12 UI recorder.
#endif

    // Returns the startup-required capture owner or terminates through Lane F.
    Rendering::Dx12BackbufferCapture& RequireBackbufferCapture() const;
};

} // namespace Runtime
} // namespace SkullbonezCore
