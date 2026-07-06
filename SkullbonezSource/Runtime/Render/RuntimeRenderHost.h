/*
File: SkullbonezSource/Runtime/Render/RuntimeRenderHost.h
Purpose:
  Names the callback boundary and startup bindings for runtime render passes.

Mental model:
  RuntimeRenderer owns pass order and pass objects. RuntimeRenderHost is now the
  explicit callback bridge for behavior still implemented by Run while renderer
  dependencies travel through named startup bindings.

Glossary:
  Render host: Callback holder used while Run still owns editor overlay and
    lifecycle logging behavior.
  Binding: Pointer set that connects RuntimeRenderer to current runtime owners.
  Callback: Transitional function pointer used for behavior still implemented
    on Run.
  Render backend view: Borrowed active renderer capabilities published by the
    composition root; null pointers mean the backend is not available.
  DXR reflection transform buffer: Host-owned per-frame scratch matrix data
    streamed from the scene view into the DX12 TLAS build.

Invariants:
  - RuntimeRenderHost does not own the callback target.
  - RuntimeRenderer owns renderer scratch state that should not leak back into
    Run.h, including DXR reflection instance transforms.
  - All references must outlive RuntimeRenderer and its passes.
  - Callback functions are bound once by Run construction; they preserve the
    remaining Run-side behavior until later phases move those services behind
    narrower owners.

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
class IRenderResourceFactory;
class IRenderRayTracing;
} // namespace Rendering
namespace UI
{
class InGameUI;
}
namespace Basics
{
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
};

struct RuntimeRenderBackendView
{
    Rendering::IRenderBackend* renderBackend = nullptr;        // Active renderer borrow; null when no backend is ready.
    Rendering::IRenderRayTracing* rayTracingBackend = nullptr; // Optional DXR facet borrowed from the active renderer.
};

// Concept: RuntimeRendererBindings is the startup borrow set for pass owners.
// Owner: RuntimeRenderer. Reason: Run is still the composition root, but render
// pass construction needs explicit long-lived owners instead of a browsable host.
// Deletion condition: replace each borrowed owner with a domain renderer/pass
// owner as plans 01/03/04 drain. Checker budget: bindings may be populated only
// by Run startup code and must not grow per-frame draw decisions.
struct RuntimeRendererBindings
{
    RuntimeRenderBackendView backend;                          // Stable backend capability pointers captured at Run startup.
    RenderRuntimeView runtime;
    RenderWorldView world;
    RenderSceneView scene;
    RenderReplayOverlayView replayOverlay;
    RenderToolOverlayView toolOverlay;
    RenderUiView ui;
    RenderDiagnosticsView diagnostics;
};

// Concept: RuntimeRenderHost owns this callback boundary while Run remains the
// composition root for editor overlays, resource lifecycle logging, and camera
// labels.
//
// Why: those commands still live on Run, but render passes must not borrow the
// whole Run object or the concrete scene container. Keeping the function-pointer
// list here makes each remaining command explicit.
//
// Deletion condition: replace each callback with a domain owner in the runtime
// decomposition plan, then remove the corresponding entry from this struct.
// Checker budget: no concrete scene-container borrow is allowed in Runtime/Render;
// callbacks stay outside per-instance render loops and are audited as this one
// host-owned boundary.
struct RuntimeRenderHostCallbacks
{
    using LogLifecycleStepFn = void ( * )( void* user, const char* phase, const char* step );
    using RenderEditorOverlayFn = void ( * )( void* user,
                                              Rendering::IRenderResourceFactory& renderResources,
                                              const Math::Transformation::Matrix4& viewProjection,
                                              const Math::Vector::Vector3& cameraEye,
                                              const Math::Vector::Vector3& cameraUp );
    using VoidFn = void ( * )( void* user );
    using CameraModeEnabledMaskFn = uint32_t ( * )( void* user );
    using CameraModeLabelFn = const char* (*)( void* user, RunCameraMode mode );

    void* user = nullptr;
    LogLifecycleStepFn logRenderResourceLifecycleStep = nullptr;
    RenderEditorOverlayFn renderEditorOverlay = nullptr;
    VoidFn refreshRuntimeViewModel = nullptr;
    CameraModeEnabledMaskFn cameraModeEnabledMask = nullptr;
    CameraModeLabelFn cameraModeLabel = nullptr;
};

class RuntimeRenderHost
{
  public:
    explicit RuntimeRenderHost( RuntimeRenderHostCallbacks callbacks ) : m_callbacks( callbacks )
    {
    }

    void LogRenderResourceLifecycleStep( const char* phase, const char* step ) const
    {
        m_callbacks.logRenderResourceLifecycleStep( m_callbacks.user, phase, step );
    }

    void RenderEditorOverlay( Rendering::IRenderResourceFactory& renderResources,
                              const Math::Transformation::Matrix4& viewProjection,
                              const Math::Vector::Vector3& cameraEye,
                              const Math::Vector::Vector3& cameraUp ) const
    {
        m_callbacks.renderEditorOverlay( m_callbacks.user, renderResources, viewProjection, cameraEye, cameraUp );
    }

  private:
    RuntimeRenderHostCallbacks m_callbacks;
};

} // namespace Basics
} // namespace SkullbonezCore
