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
  - SkullbonezSource/Runtime/Render/RuntimeRenderer.h
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
#include "InteractionAutomationController.h"
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
    // Concept: Run is the process composition root. It constructs concrete
    // subsystem owners and retains only the process borrows and launch/result
    // values needed to sequence startup, frame order, and shutdown.
    Window& m_window;                                                   // Startup-owned native window borrowed for process lifetime.
    Threading::WorkerPool& m_workerPool;                                // Startup-owned worker service borrowed for process lifetime.
    EngineConfig& m_config;                                             // Borrowed process config loaded and CLI-patched by Runtime/Init.cpp.
    Assets::AssetSystem m_assets;                                       // Process source-asset registry shared by scene and renderer owners.
    SceneController m_sceneController;                                  // Owns scene queue, cameras, world, entities, physics, and models.
    SbResult m_lastSceneLoadResult;                                     // Last queue load outcome observed by startup/load-only paths.
    bool m_skipExecute = false;                                         // Startup-only probes can complete without entering the frame loop.
    RunLaunchOptions m_launchOptions;                                   // CLI/startup policy reapplied across scene loads.
    ApplicationExitState m_applicationExit;                             // First-failure exit latch resolved by the platform message loop.
    RenderDefaultsStore m_renderDefaults;                               // Deferred ordinary/cinematic engine.cfg persistence owner.
    RunStartupState m_startup;                                          // engine.cfg startup capacity/thread defaults restored by demo resets.

    // Subsystem owners below are ordered by lifetime dependency. Render-host
    // bindings borrow from these objects; they do not own them.
    DiagnosticsRuntime m_diagnosticsRuntime;                            // Capture, perf, and queryable physics diagnostics owner.
    RunTimerState m_timers;                                             // Frame/simulation timers and rolling timing values
    InputRouter m_inputRouter;                                          // Owns keyboard/pointer edge memory and binding-context enforcement.
    RuntimeInteractionController m_interaction;                         // Authoritative runtime workspace and world-input owner.
    InteractionAutomationController
        m_interactionAutomation;                                        // CLI harness that injects runtime mouse input for regression tests.
    RunCameraState m_camera;                                            // Camera/input state and ball-tracking settings
    AttachedCameraController m_attachedCamera;                          // Owns non-serialized Attach target/orbit/follow state.
    SimulationSystem m_simulation;                                      // Simulation timestep policy and physics accumulators
    ReplayRuntime m_replayRuntime;                                      // Owns replay recorders, branch provenance, and replay interaction state.
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
    RuntimeRenderBackendView m_renderBackendView;                       // Borrowed active renderer capabilities for renderer users.
    RuntimeRenderer m_renderer;                                         // Owns runtime render passes and frame render ordering.

    void Render( const RuntimeRenderModelFrameView&
                     renderModels );                                    // Skips 3D in text-only runs, then records passes for the current camera state.
    void UpdateLogic( float simulationDt, float cameraDt );             // simulationDt drives physics; cameraDt is unscaled wall time.
    void AfterPhysicsStep();                                            // Post-step hooks that must see committed physics state.
    // --- Per-frame tick helpers (called from Execute()) ---
    void TickPhysics( double dt );                                      // Physics dispatch: fixed-step and variable-step accumulator
    bool TickScreenshots();                                             // Screenshot triggers; returns true when frame should restart (continue)
    void TickAutoCycle();                                               // Auto-cycle ball capture; posts WM_QUIT when all balls captured
    bool TickSceneAdvance();                                            // Frame count, exit/hold on completion, restarts; returns true to continue

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
    SbResult ApplyStartupOverrides(
        const RunStartupOverrides& overrides );                         // Apply parsed CLI/startup policy before Initialise().
    SbResult RunSceneLoadOnly( const char* snapshotOutPath = nullptr ); // Scene-load smoke path; skips the frame loop.
    SbResult Execute();                                                 // Main message loop; returns recoverable runtime failures.
};
} // namespace Basics
} // namespace SkullbonezCore
