/*
File: RuntimeFrameViews.h
Purpose:
  Defines the reference-only calling convention used by top-level frame helpers.

Summary:
  Run constructs these views on the stack for one frame turn. Helpers borrow the
  concrete owners through named fields, perform synchronous work, and return;
  the views never become members, owners, callback packs, or durable state.

Glossary:
  Host view: Process services used for platform work and diagnostics.
  Interaction view: Input, camera, replay, UI, and tool owners that arbitrate
    operator intent.
  Scene view: Scene mutation owners and the shell policy that controls them.
  Presentation view: Render and validation owners used to submit a frame.
  UI text facts: Value snapshots selected after simulation for the late UI
    pass. Stable label pointers name static owner vocabulary.

Invariants:
  - Every capability member is a reference; no view owns subsystem state.
  - Views are constructed at the Run::Execute call site and are never retained.
  - No capability slice spans the complete frame surface; helpers receive only
    the slices required for their operation.
  - Views hold no cross-domain queues, callbacks, void pointers, or copied owner
    state.

Related:
  - RunFrame.cpp constructs the views and owns top-level frame order.
  - InputFrameExecution.cpp consumes them during the input turn.
  - RuntimeStressController.cpp consumes them during deterministic stress work.
  - Agentic/Reports/2026-07-12/frame-view-calling-convention-closure.md records this convention.
*/
#pragma once

#include <cstdint>

namespace SkullbonezCore
{
namespace Core
{
class EngineConfig;
class Profiler;
} // namespace Core
namespace Assets
{
class AssetSystem;
}
namespace Threading
{
class WorkerPool;
}
namespace UI
{
class InGameUI;
} // namespace UI
namespace Rendering
{
class Dx12BackbufferCapture;
class Dx12ShaderDevelopment;
} // namespace Rendering
namespace Runtime
{
class ApplicationExitState;
class AttachedCameraController;
class DiagnosticsRuntime;
class InputRouter;
class RenderDefaultsStore;
class RuntimeInteractionController;
class RuntimeOverlayDiagnostics;
class RuntimeValidationHarness;
class RuntimeRenderer;
class RuntimeTools;
class SceneController;
class SimulationSystem;
class Window;
struct CameraControlState;
struct OverlayDebugState;
struct RunLaunchOptions;
struct RunStartupState;
struct RunTimerState;
enum class RuntimeGizmoDragKind;
enum class RuntimeInteractionGestureKind;

// Lifetime: process services are borrowed synchronously for platform and
// diagnostics work. This slice intentionally excludes input, scene, and render
// authority so it cannot substitute for the application shell.
struct RuntimeFrameHostView
{
    ApplicationExitState& applicationExit;
    DiagnosticsRuntime& diagnosticsRuntime;
    Assets::AssetSystem& assets;
    Threading::WorkerPool& workerPool;
    Window& window;
    Core::Profiler* profiler;

    // C++20 no longer treats a type with a user-declared constructor as an
    // aggregate. Keep copy construction forbidden and make the one valid
    // stack-only borrow-map construction explicit.
    RuntimeFrameHostView( ApplicationExitState& applicationExitValue, DiagnosticsRuntime& diagnosticsRuntimeValue,
                          Assets::AssetSystem& assetsValue, Threading::WorkerPool& workerPoolValue, Window& windowValue,
                          Core::Profiler* profilerValue )
        : applicationExit( applicationExitValue ), diagnosticsRuntime( diagnosticsRuntimeValue ), assets( assetsValue ),
          workerPool( workerPoolValue ), window( windowValue ), profiler( profilerValue )
    {
    }
    RuntimeFrameHostView( const RuntimeFrameHostView& ) = delete;
    RuntimeFrameHostView& operator=( const RuntimeFrameHostView& ) = delete;
};

// Lifetime: this slice exists only while routing one frame of operator or
// automation intent. Durable input, camera, UI, and tool state stays in
// the named owners below.
struct RuntimeFrameInteractionView
{
    InputRouter& inputRouter;
    RuntimeInteractionController& interaction;
    AttachedCameraController& attachedCamera;
    UI::InGameUI& operatorUi;
    RuntimeTools& runtimeTools;
    CameraControlState& camera;

    RuntimeFrameInteractionView( InputRouter& inputRouterValue, RuntimeInteractionController& interactionValue,
                                 AttachedCameraController& attachedCameraValue, UI::InGameUI& operatorUiValue,
                                 RuntimeTools& runtimeToolsValue, CameraControlState& cameraValue )
        : inputRouter( inputRouterValue ), interaction( interactionValue ), attachedCamera( attachedCameraValue ),
          operatorUi( operatorUiValue ), runtimeTools( runtimeToolsValue ), camera( cameraValue )
    {
    }
    RuntimeFrameInteractionView( const RuntimeFrameInteractionView& ) = delete;
    RuntimeFrameInteractionView& operator=( const RuntimeFrameInteractionView& ) = delete;
};

// Lifetime: scene policy and mutation owners are borrowed only for the current
// frame operation. Presentation backends and input devices are deliberately
// absent from this slice.
struct RuntimeFrameSceneView
{
    SkullbonezCore::Core::EngineConfig& config;
    RunLaunchOptions& launchOptions;
    const RunStartupState& startup;
    RunTimerState& timers;
    RuntimeOverlayDiagnostics& overlays;
    SimulationSystem& simulation;
    SceneController& sceneController;

    RuntimeFrameSceneView( SkullbonezCore::Core::EngineConfig& configValue, RunLaunchOptions& launchOptionsValue,
                           const RunStartupState& startupValue, RunTimerState& timersValue,
                           RuntimeOverlayDiagnostics& overlaysValue, SimulationSystem& simulationValue,
                           SceneController& sceneControllerValue )
        : config( configValue ), launchOptions( launchOptionsValue ), startup( startupValue ), timers( timersValue ),
          overlays( overlaysValue ), simulation( simulationValue ), sceneController( sceneControllerValue )
    {
    }
    RuntimeFrameSceneView( const RuntimeFrameSceneView& ) = delete;
    RuntimeFrameSceneView& operator=( const RuntimeFrameSceneView& ) = delete;
};

// Lifetime: render submission and validation controls are exposed only to
// presentation helpers. Scene and input ownership remains behind their own
// capability slices.
struct RuntimeFramePresentationView
{
    RenderDefaultsStore& renderDefaults;
    RuntimeValidationHarness& validationHarness;
    RuntimeRenderer& renderer;
    Rendering::Dx12BackbufferCapture* backbufferCapture;
    Rendering::Dx12ShaderDevelopment* shaderDevelopment;

    RuntimeFramePresentationView( RenderDefaultsStore& renderDefaultsValue, RuntimeValidationHarness& validationHarnessValue,
                                  RuntimeRenderer& rendererValue, Rendering::Dx12BackbufferCapture* backbufferCaptureValue,
                                  Rendering::Dx12ShaderDevelopment* shaderDevelopmentValue )
        : renderDefaults( renderDefaultsValue ), validationHarness( validationHarnessValue ), renderer( rendererValue ),
          backbufferCapture( backbufferCaptureValue ), shaderDevelopment( shaderDevelopmentValue )
    {
    }
    RuntimeFramePresentationView( const RuntimeFramePresentationView& ) = delete;
    RuntimeFramePresentationView& operator=( const RuntimeFramePresentationView& ) = delete;
};

// Value snapshots selected for one late UI pass. Label pointers name stable
// owner vocabulary; mutable output storage is a separate Render parameter and
// is deliberately not hidden inside this facts record.
struct RuntimeUiTextFrameFacts
{
    uint32_t cameraModeEnabledMask = 0u;
    const char* cameraModeLabel = nullptr;
    const char* launcherFireModeLabel = nullptr;
    bool isLauncherCameraMode = false;
    RuntimeInteractionGestureKind interactionGestureKind {};
    RuntimeGizmoDragKind interactionGizmoKind {};
    float presentationAlpha = 0.0f;
    bool presentationPinned = false;
    double secondsPerFrame = 0.0;
    bool legacyDevelopmentUiActive = true;
};
} // namespace Runtime
} // namespace SkullbonezCore
