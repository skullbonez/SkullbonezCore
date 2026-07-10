/*
File: SkullbonezSource/Runtime/Run.h
Purpose:
  Coordinates the main game loop and high-level runtime lifecycle.

Mental model:
  Run.h coordinates the main game loop and high-level runtime lifecycle. As a
  public header, keep edits anchored on local owner boundaries and call
  direction and on the glossary/invariants below.

Glossary:
  Attached camera target: Runtime follow selection where Run owns the selected
    identity while physics stores own live target pose and motion.
  DX11/OpenGL: Retired runtime renderers. Their source backends have been
  removed; old command-line values now fail early.
  HUD (Heads-Up Display): On-screen diagnostics and control overlay.
  CLI (Command-Line Interface): Text arguments or scripts used to launch
  validation and tooling paths.
  Lane R result: Recoverable scene-load, capture, renderer-drain, or automation
    failure reported with owner/message diagnostics instead of exceptions.
  Probe failure: CLI validation failure reported as bounded result/report data
    so automation exits nonzero without throwing through the frame loop.

Invariants:
  - Run is the composition root for process-lifetime runtime systems.
  - Scene-lifetime camera, terrain, world, entity, model, and physics state
    belongs to SceneController; Run sequences work without republishing those
    owners.
  - Public startup code should configure Run through the small launch surface
    below instead of reaching into runtime-owned state.
  - Camera follow helpers should take store-sampled body state instead of
    reopening GameModel as a live physics mirror.

Related:
  - SkullbonezSource/Runtime/Run.cpp
  - SkullbonezSource/Runtime/RunSubsystemState.h
  - SkullbonezSource/Runtime/Render/RuntimeRenderResources.h
  - Agentic/Reference/runtime-reference.md
  - Agentic/Reference/comment-style-guide.md
*/
#pragma once


#include <cstddef>
#include <string>
#include <vector>
#include "../Core/Common.h"
#include "../Core/SbResult.h"
#include "ApplicationExitState.h"
#include "AttachedCameraController.h"
#include "CameraCollection.h"
#include "GraphicsStressController.h"
#include "InputController.h"
#include "InputRouter.h"
#include "LiveStyleController.h"
#include "Diagnostics/DiagnosticsRuntime.h"
#include "RenderDefaultsStore.h"
#include "RuntimeInteractionController.h"
#include "RuntimeCameraMode.h"
#include "Allocation/RuntimeAllocationTracker.h"
#include "Audio/ContactAudioService.h"
#include "Render/RuntimeRenderHost.h"
#include "Render/RuntimeRenderInputs.h"
#include "Render/RuntimeRenderer.h"
#include "RunDebugState.h"
#include "RunLaunchOptions.h"
#include "RunCameraState.h"
#include "RunRuntimeSettings.h"
#include "RunSubsystemState.h"
#include "RunInteractionAutomationState.h"
#include "RunStartupState.h"
#include "RunTimerState.h"
#include "RuntimeViewModel.h"
#include "Replay/ReplayRuntime.h"
#include "Scene/SceneAuthoredSetup.h"
#include "Scene/SceneController.h"
#include "Scene/SceneGeneratedSetup.h"
#include "Scene/SceneRuntimeCoordinator.h"
#include "../Physics/SimulationSystem.h"
#include "../Assets/TextureCollection.h"
#include "Window.h"
#include "../Rendering/Text.h"
#include "../World/SkyBox.h"
#include "../Maths/GeometricMath.h"
#include "../Scene/TestScene.h"
#include "Debug/BroadphaseVisualizer.h"
#include "Debug/CollisionVisualizer.h"
#include "Debug/PhysicsDebugVisualizer.h"
#include "Tools/RuntimeTools.h"
#include "../UI/UI.h"


namespace SkullbonezCore
{
namespace Rendering
{
class IRenderDiagnostics;
}
namespace Basics
{
class Profiler;
struct RuntimeInteractionCommand;
struct RuntimeInteractionEvent;

/* -- Skullbonez Run
---------------------------------------------------------------------------------------------------------------------------------------------

    Harness for the Skullbonez Core graphics library.
-----------------------------------------------------------------------------------------------------------------------------------------------------------------*/
class Run
{

  private:
    // Concept: Run is the process composition root. These members either own a
    // top-level subsystem for
    // process lifetime/order, or keep launch/session choices that coordinate
    // multiple subsystems and therefore do not have one narrower owner yet.
    EngineConfig& m_config;                                             // Borrowed process config loaded and CLI-patched by Runtime/Init.cpp.
    SceneController m_sceneController;                                  // Owns scene queue, cameras, world, entities, physics, and models.
    SbResult m_lastSceneLoadResult;                                     // Last queue load outcome observed by startup/load-only paths.
    bool m_skipExecute = false;                                         // Startup-only probes can complete without entering the frame loop.
    RunLaunchOptions m_launchOptions;                                   // CLI/startup policy reapplied across scene loads.
    ApplicationExitState m_applicationExit;                             // First-failure exit latch resolved by the platform message loop.
    CinematicRenderConfig m_defaultCinematicRender;                     // engine.cfg cinematic baseline restored by the Demo Scene cine mode
    RenderDefaultsStore m_renderDefaults;                               // Deferred ordinary/cinematic engine.cfg persistence owner.
    RunStartupState m_startup;                                          // engine.cfg startup capacity/thread defaults restored by demo resets.

    // Subsystem owners below are ordered by lifetime dependency. Render-host
    // bindings borrow from these objects; they do not own them.
    DiagnosticsRuntime m_diagnosticsRuntime;                            // Capture, perf, and queryable physics diagnostics owner.
    RunRuntimeSettings m_runtimeSettings;                               // Scene/app runtime swap policy toggles
    RunTimerState m_timers;                                             // Frame/simulation timers and rolling timing values
    RunSubsystemState m_systems;                                        // Window, texture, skybox, asset, and pass-resource shelf.
    RuntimeInputContext m_runtimeInput;                                 // Semantic input mode/action state owned by input routing.
    InputRouter m_inputRouter;                                          // Owns keyboard/pointer edge memory and binding-context enforcement.
    InputActions m_inputActions;                                        // Fixed ordered semantic events for the current device frame.
    RuntimeInteractionController m_interaction;                         // Authoritative runtime workspace and world-input owner.
    RunInteractionAutomationState
        m_interactionAutomation;                                        // CLI harness that injects runtime mouse input for regression tests.
    RunCameraState m_camera;                                            // Camera/input state and ball-tracking settings
    AttachedCameraController m_attachedCamera;                          // Owns non-serialized Attach target/orbit/follow state.
    SimulationSystem m_simulation;                                      // Simulation timestep policy and physics accumulators
    ReplayRuntime m_replayRuntime;                                      // Owns replay recorders, branch provenance, and replay interaction state.
    ReplayLauncherVisualSample
        m_replayLauncherVisualScratch;                                  // Reused replay capture payload; capacity is kept outside gameplay.
    Runtime::Audio::ContactAudioService m_contactAudio;                 // Presentation-only material impact playback sink.
    LiveStyleController m_liveStyle;                                    // Owns live style tweak/capture harness file-watching state.
    UI::InGameUI m_UI;                                                  // Encapsulated in-game diagnostics window
    RunDebugState m_debug;                                              // Runtime debug/overlay toggles
    GraphicsStressController m_graphicsStress;                          // Deterministic graphics fuzzer state for overnight DX12 runs.
    RuntimeTools m_runtimeTools;                                        // Launcher, editor, manipulator state, and transient render feedback.
    Physics::BroadphaseVisualizer m_broadphaseVisualizer;               // Spatial grid debug overlay (G key toggle)
    Physics::CollisionVisualizer m_collisionVisualizer;                 // Solid collision/sleep model visualizer (V key toggle)
    Physics::PhysicsDebugVisualizer
        m_physicsDebugVisualizer;                                       // Line overlay for object axes, contact manifolds, and sleep state
    RuntimeViewModel m_runtimeViewModel;                                // Scalar runtime snapshot for presentation/diagnostics.
    RuntimeRenderBackendView m_renderBackendView;                       // Borrowed active renderer capabilities for renderer users.
    RuntimeRenderer m_renderer;                                         // Owns runtime render passes and frame render ordering.

    inline static int sPerfPass = 0;
    void Render( const RuntimeRenderModelFrameView&
                     renderModels );                                    // Skips 3D in text-only runs, then records passes for the current camera state.
    RunSceneState& SceneState();                                        // Mutable scene-run state owned by SceneController
    const RunSceneState& SceneState() const;                            // Read-only scene-run state owned by SceneController
    void RefreshRuntimeViewModel();                                     // Rebuilds scalar presentation state from narrow owner borrows
    void RelativeUpdateCamera( uint32_t hash );                         // Keeps non-selected relative cameras inside terrain height limits.
    void UpdateLogic( float simulationDt, float cameraDt );             // simulationDt drives physics; cameraDt is unscaled wall time.
    void TakeInput();                                                   // Applies focused input to camera, UI, scene cycling, diagnostics, and editor tools.
    void TickInteractionAutomationBeforeInput();                        // Applies scripted mouse/button state before normal input routing.
    void TickInteractionAutomationAfterRender();                        // Runs assertions/screenshots and finishes scripted automation.
    void ClearInteractionAutomationInput();                             // Releases input overrides after completion or failure.
    void WriteInteractionAutomationReport();                            // Writes JSON result for --interaction-report.
    bool TryFindInteractionAutomationModel( const char* name, int& outIndex ) const;
    bool TryProjectInteractionAutomationModel( const char* name, POINT& outMouse );
    bool DrainCaptureRequests();                                        // Executes capture-owned input requests against the active backend.
    bool DrainRenderDefaultRequests();                                  // Persists final frame-mutated render values at the input checkpoint.
    void UpdateRuntimeInputModeAfterAction(
        RuntimeInputAction action,
        RuntimeInputActionSource source );                              // Records the mode transition caused by one runtime/tool action.
    bool
    RouteRuntimePointerInput( const RuntimeInputSnapshot& inputSnapshot,
                              const RuntimeMouseEdges& mouseEdges );    // Routes pointer input after snapshot capture.
    bool HandleUnfocusedInputFrame();                                   // Resets transient input when the app loses focus.
    void DispatchPostUIKeyboardActions();                               // Runs capture and late reset keyboard actions after UI input.
    void DispatchAfterUIKeyboardActions(
        bool uiUserInteracted );                                        // Runs ESC/UI dismissal after UI controls get first refusal.
    RuntimeInteractionTransition EnterInteractionForCameraMode(
        RunCameraMode mode );                                           // Converts camera/tool requests into controller workspace transitions.
    void ApplyRuntimeInteractionTransitionCleanup(
        const RuntimeInteractionTransition&
            transition );                                               // Clears stale tool ownership before the new mode consumes input.
    void ClearEditorInteractionForRuntimeTransition(
        bool clearSelection );                                          // Clears editor placement/gizmo ownership before leaving Edit.
    void EnterFlyModeCamera();                                          // Switches camera state into free-flight controls.
    void ExitFlyModeCamera();                                           // Restores terrain camera bounds and leaves launcher mode.
    const char* CameraModeLabel( RunCameraMode mode ) const;            // Compact name for UI and transition diagnostics.
    uint32_t CameraModeEnabledMask() const;                             // One bit per camera mode; disabled modes remain visible in UI.
    bool IsDemoCameraModeAvailable() const;                             // True when Demo can track at least one live model.
    RunCameraMode NormalizeCameraModeForCurrentScene(
        RunCameraMode mode ) const;                                     // Clamps passive camera modes to generated-demo vs authored-scene ownership.
    void SetCameraModeLabelAfterInteractionTransition(
        RunCameraMode mode );                                           // Applies the camera label after controller workspace/tool ownership is chosen.
    RuntimeInteractionTransition SetWorldInteractionOwnerAfterInteractionTransition(
        WorldInteractionOwner owner,
        InteractionExitReason reason );                                 // Applies tool-owner transitions through runtime cleanup.
    void ClearRuntimeInteractionStateForTransition(
        const RuntimeInteractionTransition& transition );               // Clears state owned by the interaction being exited.
    void ApplyCameraMode( RunCameraMode mode,
                          RuntimeInputActionSource source );            // Applies keyboard/UI camera-mode requests.
    void CycleCameraMode();                                             // Tab cycles through enabled explicit camera modes.
    SbResult ReleaseBackendOwnedRenderResources(
        const char* phaseName );                                        // Ordered GPU-resource release hook while the backend is alive.
    SbResult RebuildRegisteredRenderResources();                        // Recreates renderer resources from source asset records.
    void SetViewingOrientation();                                       // Camera-view setup for the current frame.
    SbResult SaveScreenshot( const char* path );                        // Lane R backbuffer capture result; current encoder writes BMP files.
    void EnterInteractiveSceneRun();                                    // Locks scene automation into non-quitting interactive mode
    bool CanSceneAutomationQuit() const;                                // True for CLI suites/tests; false once the user owns scene flow
    void HoldCompletedInteractiveScene();                               // Keep the current scene alive after interactive automation completes
    void MoveCamera( float keyMovementQty,
                     float mouseMovemementQty );                        // Keyboard/mouse deltas dispatched to CameraCollection.
    SbResult RunUIStressActions();                                      // Lane R deterministic UI churn result; stops before unsafe generated rebuilds.
    void RunGraphicsStressActions(
        const Rendering::IRenderDiagnostics&
            renderDiagnostics );                                        // Deterministic render/scene churn used to shake out DX12 crashes.
    void AfterPhysicsStep();                                            // Post-step hooks that must see committed physics state.
    void ApplyMousePickupPhysicsStep();                                 // Manipulator spring impulse before one fixed physics step.
    void RestoreMousePickupAngularVelocity();                           // Holds grabbed body angular velocity stable during drag.
    // --- Per-frame tick helpers (called from Execute()) ---
    void TickPhysics( double dt );                                      // Physics dispatch: fixed-step and variable-step accumulator
    bool TickScreenshots();                                             // Screenshot triggers; returns true when frame should restart (continue)
    void TickLiveStyleControl();                                        // Poll live.style.json/capture.txt and apply look changes without scene reload
    void TickLiveStyleControlCapture();
    void TickAutoCycle();                                               // Auto-cycle ball capture; posts WM_QUIT when all balls captured
    bool TickSceneAdvance();                                            // Frame count, exit/hold on completion, restarts; returns true to continue
    void UpdateWaterHeightControls( float dt );                         // Slide water surface up/down while held
    bool
    TryBuildMouseWorldRay( Math::Vector::Vector3& outOrigin,
                           Math::Vector::Vector3& outDirection,
                           bool clampToViewport = false ) const;        // Mouse position projected into a world-space ray.
    bool TryBuildMouseWorldRayAt( POINT clientPosition,
                                  Math::Vector::Vector3& outOrigin,
                                  Math::Vector::Vector3& outDirection,
                                  bool clampToViewport = false )
        const;                                                          // Explicit-point variant for automation and owner-produced pointer values.
    void TickEditorViewportAndPlacementScaleInput(
        int unhandledWheelDelta );                                      // Updates viewport-look and placement scale/altitude gestures.
    bool TickEditorWorldClick(
        const RuntimeMouseEdges& mouseEdges,
        bool suppressWorldActionThisFrame );                            // Handles editor placement, selection, and gizmo mouse ownership.
#ifdef _DEBUG
    void LogSceneFinished( const char* reason );
    void BeginPhysicsDiagnosticsRun( const char* scenePath );
    void EndPhysicsDiagnosticsRun( const char* status );
#endif

  public:
    Run( Window& window,
         std::vector<std::string> sceneQueue,
         EngineConfig& config,
         Threading::WorkerPool& workerPool,
         Profiler* profiler,
         RuntimeRenderBackendView renderBackendView );                  // sceneQueue empty string selects generated demo mode.
    ~Run();
    void Initialise();                                                  // Initialises shared resources and loads first scene
    const SbResult& LastSceneLoadResult() const;                        // Initialise scene-load result for CLI startup checks.
    void ApplyStartupOverrides(
        const RunStartupOverrides& overrides );                         // Apply parsed CLI/startup policy before Initialise().
    SbResult RunSceneLoadOnly( const char* snapshotOutPath = nullptr ); // Scene-load smoke path; skips the frame loop.
    SbResult Execute();                                                 // Main message loop; returns recoverable runtime failures.
    SbResult SetInteractionAutomation(
        const char* scriptPath,
        const char* reportPath );                                       // CLI harness for deterministic world-click interaction scripts.
    SbResult InteractionAutomationResult() const;                       // Non-throwing CLI automation result after Execute().
    void DumpTextureAssets( FILE* out ) const;
};
} // namespace Basics
} // namespace SkullbonezCore
