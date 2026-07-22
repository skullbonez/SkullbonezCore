/*
File: SkullbonezSource/Runtime/Run.h
Purpose:
  Coordinates the main game loop and high-level runtime lifecycle.

Summary:
  Run.h coordinates the main game loop and high-level runtime lifecycle. As a
  public header, keep edits anchored on local owner boundaries and call
  direction and on the glossary/invariants below.

Mental model:
  Run is the process composition root and sequencer. It borrows concrete owners,
  constructs stack-only frame views, and calls narrow phases without becoming
  the storage owner for input, scene, replay, rendering, or UI policy.

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
  Frame phase result: Small value-only decision passed between adjacent frame
    phases; it is never retained as process or subsystem state.

Invariants:
  - Run is the composition root for process-lifetime runtime systems.
  - Scene-lifetime camera, terrain, world, entity, model, and physics state
    belongs to SceneController; Run sequences work without republishing those
    owners.
  - Public startup code should configure Run through the small launch surface
    below instead of reaching into runtime-owned state.
  - Camera follow helpers should take store-sampled body state instead of
    reopening legacy object record as a live physics mirror.
  - Physics, capture, auto-cycle, and scene completion share one scene-owned
    proceed policy sampled after input for the current frame.

Related:
  - SkullbonezSource/Runtime/Run.cpp
  - SkullbonezSource/Runtime/RuntimeFrameViews.h
  - SkullbonezSource/Runtime/Render/RuntimeRenderer.h
  - SkullbonezSource/Runtime/Render/RuntimeRenderResources.h
  - Agentic/Reference/runtime-reference.md
  - Agentic/Reference/comment-style-guide.md
*/
#pragma once


#include <memory>
#include <string>
#include <vector>
#include "../Core/SbResult.h"
#include "../Assets/AssetSystem.h"
#include "ApplicationExitState.h"
#include "AttachedCameraController.h"
#include "InputRouter.h"
#include "Diagnostics/DiagnosticsRuntime.h"
#include "RenderDefaultsStore.h"
#include "RuntimeInteractionController.h"
#include "Render/RuntimeRenderHost.h"
#include "Render/RuntimeRenderer.h"
#if defined( SKULLBONEZ_DEVELOPMENT_TOOLS )
#include "DevelopmentTools/ImGuiEditorOwner.h"
#endif
#include "RunLaunchOptions.h"
#include "RunCameraState.h"
#if defined( SKULLBONEZ_AUTOMATION_DIAGNOSTICS )
#include "InteractionAutomationController.h"
#endif
#include "RunStartupState.h"
#include "RunTimerState.h"
#include "Replay/ReplayRuntime.h"
#include "Scene/SceneController.h"
#include "SimulationSystem.h"
#include "Tools/RuntimeTools.h"


namespace SkullbonezCore
{
namespace Core
{
class Profiler;
namespace DevelopmentTools
{
class TracyClientOwner;
}
} // namespace Core
namespace Rendering
{
class Dx12Diagnostics;
}
namespace Threading
{
class WorkerPool;
}
namespace UI
{
class InGameUI;
}
namespace Runtime
{
class Window;
class RuntimeOverlayDiagnostics;
class RuntimeValidationHarness;
struct InteractionAutomationFrameResult;
struct RuntimeFrameHostView;
struct RuntimeFrameInteractionView;
struct RuntimeFramePresentationView;
struct RuntimeFrameSceneView;
struct RuntimeRenderModelFrameView;

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
    Window& m_window;                                                      // Startup-owned native window borrowed for process lifetime.
    Threading::WorkerPool& m_workerPool;                                   // Startup-owned worker service borrowed for process lifetime.
    SkullbonezCore::Core::EngineConfig& m_config;                          // Borrowed process config loaded and CLI-patched by Runtime/Init.cpp.
    SkullbonezCore::Core::Profiler* m_profiler;                            // Startup-owned profiler borrow; null outside profiling builds.
    SkullbonezCore::Core::DevelopmentTools::TracyClientOwner*
        m_tracyClientOwner;                                                // Startup-owned development profiler lifetime borrow; null when unavailable.
    Assets::AssetSystem m_assets;                                          // Process source-asset registry shared by scene and renderer owners.
    SceneController m_sceneController;                                     // Owns scene queue, cameras, world, entities, physics, and models.
    SkullbonezCore::Core::SbResult m_lastSceneLoadResult;                  // Last queue load outcome observed by startup/load-only paths.
    bool m_skipExecute = false;                                            // Startup-only probes can complete without entering the frame loop.
    RunLaunchOptions m_launchOptions;                                      // CLI/startup policy reapplied across scene loads.
    ApplicationExitState m_applicationExit;                                // First-failure exit latch resolved by the platform message loop.
    RenderDefaultsStore m_renderDefaults;                                  // Deferred ordinary/cinematic engine.cfg persistence owner.
    RunStartupState m_startup;                                             // engine.cfg startup capacity/thread defaults restored by demo resets.

    // Subsystem owners below are ordered by lifetime dependency. Render-host
    // bindings borrow from these objects; they do not own them.
    DiagnosticsRuntime m_diagnosticsRuntime;                               // Capture, perf, and queryable physics diagnostics owner.
    RunTimerState m_timers;                                                // Frame/simulation timers and rolling timing values
    InputRouter m_inputRouter;                                             // Owns keyboard/pointer edge memory and binding-context enforcement.
    RuntimeInteractionController m_interaction;                            // Authoritative runtime workspace and world-input owner.
#if defined( SKULLBONEZ_AUTOMATION_DIAGNOSTICS )
    InteractionAutomationController
        m_interactionAutomation;                                           // Automation-build CLI harness that injects runtime mouse input for regression tests.
#endif
    RunCameraState m_camera;                                               // Camera/input state and ball-tracking settings
    AttachedCameraController m_attachedCamera;                             // Owns non-serialized Attach target/orbit/follow state.
    SimulationSystem m_simulation;                                         // Simulation timestep policy and physics accumulators
    ReplayRuntime m_replayRuntime;                                         // Constructs and sequences the concrete replay domain owners.
    RuntimeTools m_runtimeTools;                                           // Launcher, editor, manipulator state, and transient render feedback.
#if defined( SKULLBONEZ_DEVELOPMENT_TOOLS )
    // Lifetime: the development editor owns only its ImGui CPU context and
    // presentation lifecycle; it receives no subsystem owner references.
    DevelopmentTools::ImGuiEditorOwner m_imguiEditor;
#endif
    // Lifetime: renderer and frame helpers borrow this cohesive UI owner; the
    // opaque allocation keeps UI.h out of the composition-root header.
    std::unique_ptr<UI::InGameUI> m_operatorUi;
    // Lifetime: the renderer borrows visualizers from this startup-created
    // owner, so declaration order destroys the renderer first.
    std::unique_ptr<RuntimeOverlayDiagnostics> m_overlayDiagnostics;
    std::unique_ptr<RuntimeValidationHarness> m_validationHarness;         // Owns opt-in live-style and graphics-stress controls.
    RuntimeRenderBackendView m_renderBackendView;                          // Borrowed active renderer capabilities for renderer users.
    RuntimeRenderer m_renderer;                                            // Owns runtime render passes and frame render ordering.

    // Concept: these value-only results carry decisions between adjacent frame
    // phases. They are stack state, not replacement owners or retained context.
    struct FrameInputPhaseResult;
    struct FrameSimulationPhaseResult;
    struct FrameRenderPhaseResult;
    struct FramePresentationFacts;

    bool PumpFrameMessages( int& messageExitCode );                        // Bounded Win32 drain; true ends the frame loop.
    double BeginFrameTurn();                                               // Starts timing/profiling and validates renderer composition.
    RuntimeFrameHostView BuildFrameHostView();                             // Constructs this turn's process-service borrow slice.
    RuntimeFrameInteractionView BuildFrameInteractionView();               // Constructs this turn's input/UI borrow slice.
    RuntimeFrameSceneView BuildFrameSceneView();                           // Constructs this turn's scene-policy borrow slice.
    RuntimeFramePresentationView BuildFramePresentationView();             // Constructs this turn's render/validation borrow slice.
    void BeginFrameDiagnosticsPhase();                                     // Publishes prior GPU timing, then resets draw counters.
#if defined( SKULLBONEZ_AUTOMATION_DIAGNOSTICS )
    InteractionAutomationFrameResult RunAutomationBeforeInputPhase( RuntimeFrameInteractionView& interaction,
                                                                    RuntimeFrameSceneView& scene );
#endif
    FrameInputPhaseResult RunInputPhase( RuntimeFrameHostView& host,
                                         RuntimeFrameInteractionView& interaction,
                                         RuntimeFrameSceneView& scene,
                                         RuntimeFramePresentationView& presentation,
                                         const InteractionAutomationFrameResult* automationBeforeInput );
    FrameSimulationPhaseResult RunSimulationPhase( RuntimeFrameSceneView& scene,
                                                   double secondsPerFrame,
                                                   const SceneFrameProceedPolicy& proceedPolicy );
    FrameRenderPhaseResult PrepareRenderPhase( RuntimeFrameHostView& host,
                                               RuntimeFrameInteractionView& interaction,
                                               RuntimeFrameSceneView& scene,
                                               RuntimeFramePresentationView& presentation,
                                               bool legacyDevelopmentUiActive,
                                               const FrameSimulationPhaseResult& simulation );
    RuntimeRenderModelFrameView PublishRenderModelsPhase();
    void RenderWorldPhase( const RuntimeRenderModelFrameView& renderModels, float presentationAlpha );
    SkullbonezCore::Core::SbResult RenderOperatorUiPhase( RuntimeFrameHostView& host,
                                                          RuntimeFrameInteractionView& interaction,
                                                          RuntimeFrameSceneView& scene,
                                                          RuntimeFramePresentationView& presentation,
                                                          const RuntimeRenderModelFrameView& renderModels,
                                                          const FramePresentationFacts& facts );
    void RunPostDrawDiagnosticsPhase( RuntimeFrameInteractionView& interaction, bool legacyDevelopmentUiActive );
    void FinishFrameWorkPhase( const SceneFrameProceedPolicy& proceedPolicy );
    SkullbonezCore::Core::SbResult PresentFramePhase();
    bool CompleteFramePhase( const SceneFrameProceedPolicy& proceedPolicy );

    void
    Render( const RuntimeRenderModelFrameView& renderModels,
            float presentationAlpha );                                     // Skips 3D in text-only runs, then records passes for the current camera state.
    void UpdateLogic( float simulationDt,
                      float cameraDt,
                      float presentationAlpha );                           // simulationDt drives physics; cameraDt is unscaled wall time.
    void AfterPhysicsStep();                                               // Post-step hooks that must see committed physics state.
    // --- Per-frame tick helpers (called from Execute()) ---
    float
    TickPhysics( double dt,
                 bool capturePresentationPinned,
                 const SceneFrameProceedPolicy& proceedPolicy );           // Returns the live fixed-tick interpolation fraction.
    bool TickScreenshots( const SceneFrameProceedPolicy& proceedPolicy );  // Screenshot triggers; true restarts frame.
    void TickAutoCycle( const SceneFrameProceedPolicy& proceedPolicy );    // Auto-cycle capture; may post WM_QUIT.
    bool TickSceneAdvance( const SceneFrameProceedPolicy& proceedPolicy ); // Completion/load policy; true restarts frame.
#if defined( SKULLBONEZ_DEVELOPMENT_TOOLS )
    void ApplyDevelopmentUiMode();                                         // Reapplies the process-lifetime surface selection.
    void SelectDevelopmentUiSurface(
        DevelopmentUiMode surface );                                       // Atomically hides the source before showing the target surface.
#endif

  public:
    Run( Window& window,
         std::vector<std::string> sceneQueue,
         SkullbonezCore::Core::EngineConfig& config,
         Threading::WorkerPool& workerPool,
         SkullbonezCore::Core::Profiler* profiler,
         RuntimeRenderBackendView renderBackendView,
         SkullbonezCore::Core::DevelopmentTools::TracyClientOwner* tracyClientOwner =
             nullptr );                                                    // sceneQueue empty string selects generated demo mode.
    ~Run();
    void Initialise();                                                     // Initialises shared resources and loads first scene
    const SkullbonezCore::Core::SbResult&
    LastSceneLoadResult() const;                                           // Initialise scene-load result for CLI startup checks.
    SkullbonezCore::Core::SbResult ApplyStartupOverrides(
        const RunStartupOverrides& overrides );                            // Apply parsed CLI/startup policy before Initialise().
    SkullbonezCore::Core::SbResult
    RunSceneLoadOnly( const char* snapshotOutPath = nullptr );             // Scene-load smoke path; skips the frame loop.
    SkullbonezCore::Core::SbResult Execute();                              // Main message loop; returns recoverable runtime failures.
};
} // namespace Runtime
} // namespace SkullbonezCore
