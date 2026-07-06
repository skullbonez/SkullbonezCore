/*
File: SkullbonezSource/Runtime/Render/RuntimeRenderHost.h
Purpose:
  Names the non-render services borrowed by runtime render passes.

Mental model:
  RuntimeRenderer owns pass order and pass objects. RuntimeRenderHost is the
  explicit bridge to runtime services while later phases continue moving editor,
  scene, and UI presentation behind narrower services.

Glossary:
  Render host: Borrowed service view used by render passes while Run remains
    the broader composition root.
  Binding: Pointer set that connects host methods to current runtime owners.
  Callback: Transitional function pointer used for behavior still implemented
    on Run.
  Render backend view: Borrowed active renderer capabilities published by the
    composition root; null pointers mean the backend is not available.
  DXR reflection transform buffer: Host-owned per-frame scratch matrix data
    streamed from the scene view into the DX12 TLAS build.

Invariants:
  - RuntimeRenderHost does not own the referenced state.
  - RuntimeRenderHost owns renderer scratch state that should not leak back
    into Run.h, including DXR reflection instance transforms.
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

struct RuntimeRenderHostBindings
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
    RuntimeRenderHost( RuntimeRenderHostBindings bindings, RuntimeRenderHostCallbacks callbacks )
        : m_renderBackend( bindings.backend ), m_systems( *bindings.runtime.systems ),
          m_debug( *bindings.diagnostics.debug ), m_timers( *bindings.diagnostics.timers ),
          m_config( *bindings.runtime.config ), m_launchOptions( *bindings.runtime.launchOptions ),
          m_runtimeSettings( *bindings.runtime.runtimeSettings ),
          m_cWorldEnvironment( *bindings.world.worldEnvironment ),
          m_collisionVisualizer( *bindings.world.collisionVisualizer ),
          m_broadphaseVisualizer( *bindings.world.broadphaseVisualizer ),
          m_physicsDebugVisualizer( *bindings.world.physicsDebugVisualizer ),
          m_runtimeTools( *bindings.toolOverlay.tools ), m_rayCastTest( m_runtimeTools.RayCastTest() ),
          m_editor( m_runtimeTools.Editor() ), m_mousePickup( m_runtimeTools.MousePickup() ),
          m_replayRuntime( *bindings.replayOverlay.replayRuntime ), m_launcherLaser( m_runtimeTools.Laser() ),
          m_UI( *bindings.ui.ui ), m_runtimeInput( *bindings.ui.runtimeInput ), m_camera( *bindings.ui.camera ),
          m_runtimeViewModel( *bindings.ui.runtimeViewModel ), m_sceneController( *bindings.scene.sceneController ),
          m_sceneBrowser( *bindings.scene.sceneBrowser ), m_callbacks( callbacks )
    {
    }

    bool IsLauncherCameraMode() const;

    int WindowScreenWidth() const;

    int WindowScreenHeight() const;

    Rendering::IRenderBackend* ActiveRenderBackend() const;

    Rendering::IRenderRayTracing* ActiveRayTracingBackend() const;

    const char* RendererNameOrDefault( const char* fallbackName ) const;

    bool SupportsDxrReflection() const;

    void SetVsyncEnabled( bool enabled ) const;

    void LogRenderResourceLifecycleStep( const char* phase, const char* step ) const
    {
        m_callbacks.logRenderResourceLifecycleStep( m_callbacks.user, phase, step );
    }

    const ReplayPresentationSample* CurrentReplayScrubSample() const
    {
        return m_replayRuntime.CurrentScrubSample();
    }

    const ReplaySolverFrameSample* CurrentReplaySolverScrubSample() const
    {
        return m_replayRuntime.CurrentSolverScrubSample();
    }

    const RunReplayPredictionFrame* CurrentReplayPredictionScrubFrame() const
    {
        return m_replayRuntime.CurrentPredictionScrubFrame();
    }

    void RenderEditorOverlay( Rendering::IRenderResourceFactory& renderResources,
                              const Math::Transformation::Matrix4& viewProjection,
                              const Math::Vector::Vector3& cameraEye,
                              const Math::Vector::Vector3& cameraUp ) const
    {
        m_callbacks.renderEditorOverlay( m_callbacks.user, renderResources, viewProjection, cameraEye, cameraUp );
    }

    void RefreshRuntimeViewModel() const
    {
        m_callbacks.refreshRuntimeViewModel( m_callbacks.user );
    }

    const RunSceneState& SceneState() const;

    bool ShouldRenderReplayScrubber() const
    {
        return m_replayRuntime.ShouldRenderScrubber( m_editor.editorModeEnabled, m_UI.IsVisible(), m_UI.IsMinimized() );
    }

    bool ReplayLiveAdvanceHeld() const
    {
        return m_replayRuntime.LiveAdvanceHeld();
    }

    bool ReplayPathVisualizerHasTarget() const
    {
        return m_replayRuntime.HasPathVisualizerTarget();
    }

    bool ReplayHasCameraFocus() const
    {
        return m_replayRuntime.HasCameraFocus();
    }

    bool ReplayVelocityEditActive() const
    {
        return m_replayRuntime.VelocityEditActive();
    }

    bool ToolHasLingeredRayCastLine( float maxAgeSeconds ) const
    {
        return m_runtimeTools.HasLingeredRayCastLine( maxAgeSeconds );
    }

    bool ToolHasSelectionOverlayWork( int modelCount ) const;

    bool ToolHasMousePickupOverlayWork( int modelCount ) const;

    bool ToolHasLauncherShots() const
    {
        return m_runtimeTools.HasLauncherShots();
    }

    const char* LauncherFireModeLabel() const
    {
        return m_runtimeTools.LauncherFireModeLabel();
    }

    int CurrentSceneBrowserIndex() const;

    uint32_t CameraModeEnabledMask() const
    {
        return m_callbacks.cameraModeEnabledMask( m_callbacks.user );
    }

    const char* CameraModeLabel( RunCameraMode mode ) const
    {
        return m_callbacks.cameraModeLabel( m_callbacks.user, mode );
    }

    bool BuildReplayFocusModelMask( const RenderFrameContext& frame ) const;

    RuntimeRenderBackendView m_renderBackend;
    RunSubsystemState& m_systems;
    RunDebugState& m_debug;
    RunTimerState& m_timers;
    EngineConfig& m_config;
    const RunLaunchOptions& m_launchOptions;
    RunRuntimeSettings& m_runtimeSettings;
    Environment::WorldEnvironment& m_cWorldEnvironment;
    Physics::CollisionVisualizer& m_collisionVisualizer;
    Physics::BroadphaseVisualizer& m_broadphaseVisualizer;
    Physics::PhysicsDebugVisualizer& m_physicsDebugVisualizer;
    std::array<float, MAX_GAME_MODELS * 16> m_dxrReflectionTransforms =
        {};                                                    // Scratch matrices for DXR TLAS instance upload.
    RuntimeTools& m_runtimeTools;
    RunRayCastTestState& m_rayCastTest;
    RunEditorPlacementState& m_editor;
    RunMousePickupState& m_mousePickup;
    ReplayRuntime& m_replayRuntime;
    LauncherLaser& m_launcherLaser;
    UI::InGameUI& m_UI;
    RuntimeInputContext& m_runtimeInput;
    RunCameraState& m_camera;
    RuntimeViewModel& m_runtimeViewModel;
    SceneController& m_sceneController;
    RunSceneBrowserState& m_sceneBrowser;

  private:
    RuntimeRenderHostCallbacks m_callbacks;
};

} // namespace Basics
} // namespace SkullbonezCore
