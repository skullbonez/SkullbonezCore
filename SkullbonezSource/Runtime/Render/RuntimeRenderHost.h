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
#include "../Replay/ReplayPresentationPackets.h"
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
class Dx12GeometryOwner;
class Dx12FrameOwner;
class Dx12GraphTransientPool;
class Dx12RenderDevice;
#if defined( SKULLBONEZ_DEVELOPMENT_TOOLS )
class Dx12ImGuiRendererOwner;
#endif
class Dx12Diagnostics;
class Dx12ResourceBuilder;
class Dx12TextureOwner;
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
    Rendering::Dx12RenderDevice* renderDevice = nullptr;                // Extent, VSync, and native device-state owner.
    Rendering::Dx12FrameOwner* renderFrame = nullptr;                   // Frame/output, present, drain, and resize owner.
    Rendering::Dx12GraphTransientPool* renderGraph = nullptr;           // Render-graph materialization and transition owner.
    Rendering::Dx12ResourceBuilder* renderResources = nullptr;          // Resource creation/rebuild capability.
    Rendering::Dx12TextureOwner* renderTextures = nullptr;              // Texture registry and cold texture IO owner.
    Rendering::Dx12GeometryOwner* renderGeometry = nullptr;             // Bounded dynamic/instanced geometry owner.
    Rendering::Dx12Diagnostics* renderDiagnostics = nullptr;            // Capability, draw-trace, timer, and memory snapshots.
    Rendering::Dx12BackbufferCapture* backbufferCapture = nullptr;      // Concrete screenshot/readback owner.
    Rendering::Dx12RaytracingOwner* raytracing = nullptr;               // Optional concrete reflection owner.
    Rendering::Dx12ShaderDevelopment* shaderDevelopment = nullptr;      // Explicit offline-DXC reload owner.
#if defined( SKULLBONEZ_DEVELOPMENT_TOOLS )
    Rendering::Dx12ImGuiRendererOwner* developmentUiRenderer = nullptr; // Narrow development-only DX12 UI recorder.
#endif

    // Returns the startup-required capture owner or terminates through Lane F.
    Rendering::Dx12BackbufferCapture& RequireBackbufferCapture() const;
    // Samples stable renderer vocabulary at the composition boundary so cold
    // scene transactions never receive the complete backend capability view.
    const char* RendererName() const;
};

} // namespace Runtime
} // namespace SkullbonezCore
