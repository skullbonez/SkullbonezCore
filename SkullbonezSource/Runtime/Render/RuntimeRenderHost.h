/*
File: SkullbonezSource/Runtime/Render/RuntimeRenderHost.h
Purpose:
  Defines stable and frame-scoped render-domain views.

Summary:
  Run constructs stable world/scene views once and replay/tool views per frame.
  RuntimeRenderer retains only render-domain owners, then receives immutable
  frame facts plus synchronous domain borrows for submission.

Glossary:
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
  - Agentic/Reports/2026-07-11/runtime-shell-final-ownership-review.md
  - Agentic/Reference/engine-glossary.md
*/
#pragma once

#include "../../Core/Common.h"
#include "../../Maths/Matrix4.h"
#include "../../Maths/Vector3.h"
#include "../../Rendering/Shadow.h"
#include "../App/ReplayRuntimePackets.h"
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
struct ReplayPresentationSample;
struct ReplaySolverFrameSample;
struct CameraControlState;
struct OverlayDebugState;
struct RunEditorPlacementState;
struct RunLaunchOptions;
struct RunMousePickupState;
struct RunRayCastTestState;
struct RunReplayPredictionFrame;
struct RuntimeRenderPassResources;
struct SceneSessionState;
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

struct RenderToolOverlayView
{
    RuntimeTools& tools;
    bool editorOverlayWorkVisible = false;
    bool inspectGizmoInteractionActive = false;
    bool controlDown = false;
    int attachedTargetIndex = -1;
    bool attachedFollow = false;
};

} // namespace Runtime
} // namespace SkullbonezCore
