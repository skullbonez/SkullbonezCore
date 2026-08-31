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
  - Agentic/Reference/engine-glossary.md
*/
#pragma once

#include "../../Core/Common.h"
#include "../../Maths/Matrix4.h"
#include "../../Maths/Vector3.h"
#include "../../Rendering/Shadow.h"
#include "../Replay/ReplayRuntimePackets.h"

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
class RuntimeInputContext;
enum class RunCameraMode;
struct ReplayPresentationSample;
struct ReplaySolverFrameSample;
struct RunEditorPlacementState;
struct RunLaunchOptions;
struct RunMousePickupState;
struct RunRayCastTestState;
struct RunReplayPredictionFrame;
struct RuntimeRenderPassResources;
struct SceneSessionState;
struct RuntimeRenderModelFrameView;

struct RenderToolOverlayView
{
    struct LauncherShot
    {
        Math::Vector::Vector3 start = Math::Vector::ZERO_VECTOR;
        Math::Vector::Vector3 end = Math::Vector::ZERO_VECTOR;
        Math::Vector::Vector3 cameraRight = Math::Vector::Vector3( 1.0f, 0.0f, 0.0f );
        Math::Vector::Vector3 cameraUp = Math::Vector::Vector3( 0.0f, 1.0f, 0.0f );
        float ageSeconds = 0.0f;
        float lifetimeSeconds = 0.0f;
        bool active = false;
        bool hit = false;
    };

    static constexpr std::size_t LAUNCHER_SHOT_CAPACITY = 32;

    bool editorOverlayWorkVisible = false;
    std::array<LauncherShot, LAUNCHER_SHOT_CAPACITY> launcherShots {};
    std::size_t launcherShotCount = 0;
};

} // namespace Runtime
} // namespace SkullbonezCore
