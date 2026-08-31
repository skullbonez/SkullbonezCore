/*
File: SkullbonezSource/Runtime/App/Run.h
Purpose:
  Coordinates the main game loop and high-level runtime lifecycle.

Summary:
  Run constructs process-lifetime owners, sequences their fixed frame phases,
  and passes typed values between them without absorbing domain business state.
  Ordered coordinators may reach composed members directly; delegated domain
  operations receive only the concrete owners and values they use.

Glossary:
  Attached camera target: Runtime follow selection where AttachedCameraController
    owns the selected identity while physics stores own live target pose and motion.
  DX11/OpenGL: Retired runtime renderers. Their source backends have been
  removed; old command-line values now fail early.
Invariants:
  - Run is the composition root for process-lifetime runtime systems.
  - Scene-lifetime camera, terrain, world, entity, model, and physics state
    belongs to SceneController; Run sequences work without republishing those
    owners.
  - Public startup code should configure Run through the small launch surface
    below instead of reaching into runtime-owned state.
  - The renderer unique_ptr is the sole startup-to-shutdown lifecycle truth;
    every mandatory access uses the same always-on non-null guard.
  - Camera follow helpers should take store-sampled body state instead of
    reopening legacy object record as a live physics mirror.
  - Physics, capture, auto-cycle, and scene completion share one scene-owned
    proceed policy sampled after input for the current frame.

Related:
  - SkullbonezSource/Runtime/App/Run.cpp
  - SkullbonezSource/Runtime/App/InputFrame.h
  - SkullbonezSource/Runtime/Render/RuntimeRenderer.h
  - SkullbonezSource/Runtime/Render/RuntimeRenderResources.h
  - Agentic/Reference/runtime-reference.md
  - Agentic/Reference/engine-glossary.md
*/
#pragma once


#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <vector>
#include "../../Core/FatalError.h"
#include "../../Core/SbResult.h"
#include "../../Assets/AssetSystem.h"
#include "ApplicationExitState.h"
#include "InputFrame.h"
#include "SceneLoadApplication.h"
#include "../Scene/AttachedCameraController.h"
#include "../Direction/LookLabController.h"
#include "../Input/InputRouter.h"
#include "../Diagnostics/DiagnosticsRuntime.h"
#include "../Capture/CaptureController.h"
#include "../Capture/GraphicsStressController.h"
#include "../Direction/LiveStyleController.h"
#include "../Render/RenderDefaultsStore.h"
#include "../Interaction/RuntimeInteractionController.h"
#include "../Render/RuntimeRenderHost.h"
#include "../Render/RuntimeRenderer.h"
#if defined( SKULLBONEZ_DEVELOPMENT_TOOLS )
#include "../DevelopmentTools/ImGuiEditorOwner.h"
#endif
#include "../Startup/RunLaunchOptions.h"
#include "../Camera/CameraControlState.h"
#include "../Automation/InteractionAutomationRecorder.h"
#if defined( SKULLBONEZ_AUTOMATION_DIAGNOSTICS )
#include "../Automation/InteractionAutomationController.h"
#endif
#include "../Startup/RunStartupState.h"
#include "ReplayRuntime.h"
#include "../Planning/ContinuousOrbitalForecast.h"
#include "../Scene/SceneController.h"
#include "../Simulation/SimulationSystem.h"
#include "../Editor/EditorTools.h"
#include "../Tools/RuntimeTools.h"


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
class Dx12BackbufferCapture;
class Dx12Diagnostics;
class Dx12FrameOwner;
class Dx12GeometryOwner;
class Dx12GraphTransientPool;
#if defined( SKULLBONEZ_DEVELOPMENT_TOOLS )
class Dx12ImGuiRendererOwner;
#endif
class Dx12RaytracingOwner;
class Dx12RenderDevice;
class Dx12ResourceBuilder;
class Dx12ShaderDevelopment;
class Dx12TextureOwner;
class RenderBackendDX12;
} // namespace Rendering
namespace Threading
{
class WorkerPool;
}
namespace UI
{
class InGameUI;
struct InGameUICommands;
struct OperatorEditorArbitrationResult;
struct OperatorEditorCommandQueues;
struct OperatorEditorFrameView;
} // namespace UI
namespace Runtime
{
class Window;
struct RunRendererLifecycleTestAccess;
class RuntimeOverlayDiagnostics;
struct RuntimeOverlayFramePolicy;
class RuntimeValidationHarness;
struct DemoDirectorTickResult;
struct InteractionAutomationFrameResult;
struct ReplayPathPickInput;
struct RuntimeRenderModelFrameView;
struct RuntimeUiTextFrameFacts;
struct OperatorUiProcessCommands;
struct OperatorUiSecondaryDiagnosticsFacts;
class SceneLoadTransaction;
class OperatorCommandTransaction;
struct OperatorCommandAcceptanceLedger;
namespace ReplayOverlay
{
struct ReplayOverlayStateView;
}

class Run
{

  private:
    friend struct RunRendererLifecycleTestAccess;

    // Fatal invariant: the unique_ptr is the sole renderer-lifecycle truth. Keeping the
    // always-on guard stateless prevents a second retained epoch bit from
    // diverging from the concrete owner during startup or shutdown.
    static RuntimeRenderer* RequireRenderer( RuntimeRenderer* renderer, const char* operation )
    {
        if ( !renderer )
        {
            SB_FATAL( "Runtime/Run", "%s requires the live renderer owner. renderer=%p", operation,
                      static_cast<void*>( renderer ) );
        }

        return renderer;
    }

    // Concept: Run is the process composition root. It constructs concrete
    // subsystem owners and retains only the process borrows and launch/result
    // values needed to sequence startup, frame order, and shutdown.
    SkullbonezCore::Core::SbDiagnosticStore& m_resultDiagnostics; // App-owned immutable diagnostic lease store.
    Window& m_window;                             // Startup-owned native window borrowed for process lifetime.
    Threading::WorkerPool& m_workerPool;          // Startup-owned worker service borrowed for process lifetime.
    SkullbonezCore::Core::EngineConfig& m_config; // Borrowed process config loaded and CLI-patched by Runtime/App/Init.cpp.
    SkullbonezCore::Core::Profiler* m_profiler;   // Startup-owned profiler borrow; null outside profiling builds.
    SkullbonezCore::Core::DevelopmentTools::TracyClientOwner*
        m_tracyClientOwner;            // Startup-owned development profiler lifetime borrow; null when unavailable.
    Assets::AssetSystem m_assets;      // Process source-asset registry shared by scene and renderer owners.
    SceneController m_sceneController; // Owns scene queue, cameras, world, entities, physics, and models.
    SkullbonezCore::Core::SbResult m_lastSceneLoadResult; // Last queue load outcome observed by startup/load-only paths.
    bool m_skipExecute = false;       // Startup-only probes can complete without entering the frame loop.
    RunLaunchOptions m_launchOptions; // CLI/startup policy reapplied across scene loads.

    // Invariant: --predict is a one-shot arming request, not a mode. Once the
    // target resolves and the intent is applied this latches, so later operator
    // predict/target/pause edits are never overwritten by the launch flag.
    bool m_startupPredictionApplied = false;
    ApplicationExitState m_applicationExit; // First-failure exit latch resolved by the platform message loop.
    RenderDefaultsStore m_renderDefaults;   // Deferred ordinary/cinematic engine.cfg persistence owner.
    RunStartupState m_startup;              // engine.cfg startup capacity/thread defaults restored by demo resets.

    // Subsystem owners below are ordered by lifetime dependency. Renderer and
    // frame bindings borrow from these objects; they do not own them.
    CaptureController m_capture;               // Capture-owned screenshot and automation state.
    GraphicsStressController m_graphicsStress; // Capture-owned deterministic render/runtime churn policy.
    LiveStyleController m_liveStyle;           // Direction-owned live presentation command source.
    DiagnosticsRuntime m_diagnosticsRuntime;   // Perf, memory, and queryable physics diagnostics owner.
    RuntimeFrameMetricsOwner m_timers;         // Sole owner of frame/simulation timing and metric publication.
    RuntimeFrameMetricsLifecyclePolicy
        m_metricsSceneLifecyclePolicy; // App maps Scene generations to timing reset/activation operations.
    SceneLifecycleGenerationObserver
        m_overlaySceneLifecycleObserver; // App publishes detached Scene presentation once after each clear.
    SceneLifecycleGenerationObserver
        m_graphicsStressSceneObserver; // App resumes Capture stress once after each populated scene.
    SceneLifecycleGenerationObserver
        m_inputSceneLifecycleObserver;          // App applies scene-activation input presentation once per generation.
    InputRouter m_inputRouter;                  // Owns keyboard/pointer edge memory and binding-context enforcement.
    RuntimeInteractionController m_interaction; // Authoritative runtime workspace and world-input owner.
    InteractionAutomationRecorder
        m_interactionRecorder; // Interactive test recorder capturing human input into resolution-independent scripts.
#if defined( SKULLBONEZ_AUTOMATION_DIAGNOSTICS )
    InteractionAutomationController
        m_interactionAutomation; // Automation-build CLI harness that injects runtime mouse input for regression tests.
#endif
    CameraControlState m_camera; // Camera/input state and ball-tracking settings
    SceneLifecycleGenerationObserver
        m_cameraSceneLifecycleObserver; // App applies detached camera state once after each clear.
    SceneLifecycleGenerationObserver
        m_attachedCameraSceneLifecycleObserver;     // App resets the Scene-owned attach target once after each clear.
    AttachedCameraController m_attachedCamera;      // Owns non-serialized Attach target/orbit/follow state.
    LookLabController m_lookLab;                    // Owns the current presentation-only authoring candidate.
    SimulationSystem m_simulation;                  // Simulation timestep policy and physics accumulators
    ReplayRuntime m_replayRuntime;                  // Constructs and sequences the concrete replay domain owners.
    ContinuousOrbitalForecast m_continuousForecast; // Planning-owned private forecast lifecycle and detached diagnostics.
    EditorToolsOwner m_editorTools;                 // Retains editor placement, selection, gizmo, and history authority.
    RuntimeTools m_runtimeTools;                    // Launcher, manipulator, and transient render feedback.
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
    std::unique_ptr<RuntimeValidationHarness> m_validationHarness; // Owns authored Automation scene gates only.
    Rendering::Dx12BackbufferCapture& m_backbufferCapture;         // Required process-lifetime screenshot/readback owner.
    std::unique_ptr<RuntimeRenderer> m_renderer;                   // Created once startup binds the concrete backend owners.
    std::optional<std::reference_wrapper<Rendering::Dx12ShaderDevelopment>>
        m_shaderDevelopment; // Explicit developer-only shader reload capability.

    RuntimeRenderer& Renderer( const char* operation = "Run::Renderer" )
    {
        return *RequireRenderer( m_renderer.get(), operation );
    }
    const RuntimeRenderer& Renderer( const char* operation = "Run::Renderer" ) const
    {
        return *RequireRenderer( m_renderer.get(), operation );
    }
    Rendering::Dx12BackbufferCapture& BackbufferCapture() const
    {
        return m_backbufferCapture;
    }

    static RuntimeRenderFramePolicy ProjectRenderFramePolicy( const RuntimeOverlayFramePolicy& overlay );

    bool PumpFrameMessages( int& messageExitCode ); // Bounded Win32 drain; true ends the frame loop.
    double BeginFrameTurn();                        // Starts timing/profiling and validates renderer composition.
    void AdvanceInteractionRecordingBoundary();     // Commits the prior pending turn or captures an armed baseline.
    void CaptureInteractionRecordingTurn( double secondsPerFrame ); // Copies the routed device frame after input completes.
    SkullbonezCore::Core::SbResult
    ResolveExecuteExit( int messageExitCode ); // Finalizes recorder evidence before publishing process status.
    void BeginFrameDiagnosticsPhase();         // Publishes prior GPU timing, then resets draw counters.
#if defined( SKULLBONEZ_AUTOMATION_DIAGNOSTICS )
    SceneFrameProceedPolicy RunAutomationAndInputPhase( bool& gameUiActive, RecordedCursorFrame& recordedCursor );
    InteractionAutomationFrameResult RunInteractionAutomationBeforeInput();
    InteractionAutomationFrameResult RunInteractionAutomationAfterRender( bool gameUiActive );
#endif
    SceneFrameProceedPolicy RunInputPhase( const InteractionAutomationFrameResult* automationBeforeInput,
                                           bool& gameUiActive );
    float RunSimulationPhase( double secondsPerFrame, const SceneFrameProceedPolicy& proceedPolicy,
                              bool& capturePresentationPinned );
    float PrepareRenderPhase( bool gameUiActive, bool capturePresentationPinned, float interpolationAlpha );
    RuntimeRenderModelFrameView PublishRenderModelsPhase();
    void RenderWorldPhase( const RuntimeRenderModelFrameView& renderModels, float presentationAlpha );

    OperatorUiProcessCommands RenderOperatorUiPhase( const RuntimeRenderModelFrameView& renderModels,
                                                     float presentationAlpha, bool capturePresentationPinned,
                                                     double secondsPerFrame, bool gameUiActive,
                                                     const RuntimeFrameMetricsSnapshot& frameMetrics );
    void SampleSecondaryOperatorDiagnostics( const RuntimeFrameMetricsSnapshot& frameMetrics,
                                             const RuntimeUiTextFrameFacts& uiTextFacts, const OverlayDebugState& debug,
                                             bool shadowsEnabled, bool cinematicRendering,
                                             const Core::CinematicRenderConfig& cinematic,
                                             RuntimeViewModel& runtimeViewModel,
                                             RuntimeRenderTargetPreviewSnapshot& renderTargetPreviews,
                                             RenderDiagnosticsReadout& renderDiagnosticsReadout,
                                             OperatorUiSecondaryDiagnosticsFacts& facts );
    void ApplyOperatorUiProcessCommands( const OperatorUiProcessCommands& commands );
    void RunPostDrawDiagnosticsPhase( bool gameUiActive );
    void FinishFrameWorkPhase( const SceneFrameProceedPolicy& proceedPolicy );
    void PresentFramePhase();
    bool CompleteFramePhase( const SceneFrameProceedPolicy& proceedPolicy );

    // Ordered frame sub-coordinators retain direct composition-root reach. The
    // domain operations they call receive concrete operands only.
    RuntimeUIFrameResult ApplyInputCommandsPhase( RuntimeUIFrameResult result, bool keyboardToggleEditorMode,
                                                  const RuntimeInputFrameFacts& facts );
    UI::OperatorEditorArbitrationResult PrepareOperatorInputCommands( RuntimeUIFrameResult& result,
                                                                      const RuntimeInputFrameFacts& facts );
    void ApplyReplayTransportCommand( RuntimeUIFrameResult& result, const RuntimeInputFrameFacts& facts,
                                      const ReplayTransportCommand& command );
    void ApplyReplayOperatorCommands( RuntimeUIFrameResult& result, const RuntimeInputFrameFacts& facts,
                                      const UI::OperatorEditorCommandQueues& commands );
    void ApplyForecastOperatorCommands( RuntimeUIFrameResult& result, const RuntimeInputFrameFacts& facts,
                                        const UI::OperatorEditorCommandQueues& commands );
    void RecordInputModeAction( RuntimeInputAction action, RuntimeInputActionSource source );
    void ApplyEditorPlacementModeCommand( RuntimeUIFrameResult& result, const RuntimeInputFrameFacts& facts, bool toggle );
    void ApplyEditorModeToggleCommand( RuntimeUIFrameResult& result, const RuntimeInputFrameFacts& facts,
                                       RuntimeInputActionSource source );
    void ApplyEditorModeCommands( RuntimeUIFrameResult& result, bool keyboardToggleEditorMode,
                                  const RuntimeInputFrameFacts& facts, const UI::InGameUICommands& commands );
    void ApplyEditorSceneCommands( RuntimeUIFrameResult& result, const UI::InGameUICommands& commands );
    void ApplyRuntimePresentationCommands( RuntimeUIFrameResult& result, OperatorCommandTransaction& transaction,
                                           const OperatorCommandAcceptanceLedger& acceptance );
    void ApplyReplayAndPhysicsTuningCommands( const UI::InGameUICommands& commands, OperatorCommandTransaction& transaction,
                                              const OperatorCommandAcceptanceLedger& acceptance );
    bool ApplyGeneratedSceneCommands( RuntimeUIFrameResult& result, const RuntimeInputFrameFacts& facts,
                                      const OperatorCommandAcceptanceLedger& acceptance );
    void ApplyWorldAndCinematicCommands( RuntimeUIFrameResult& result, const UI::InGameUICommands& commands,
                                         OperatorCommandTransaction& transaction,
                                         const OperatorCommandAcceptanceLedger& acceptance );
    RuntimeUIFrameResult BeginRuntimeUIFrame( const ReplayPathPickInput& replayPointerRay,
                                              const RuntimeInputFrameFacts& facts );
    RuntimePointerRouteResult RouteRuntimePointer( const RuntimePointerEvent& pointer, bool replayInspectionActive,
                                                   int activeModelCapacity, RunCameraMode replayRestoreCameraMode );
    void PublishLookLabStatusView();          // Pushes one changed detached status into the UI cache.
    bool ApplyLookLabSeed( uint64_t seed );   // Resolves and applies one presentation-only candidate.
    void BeginLookLabSave();                  // Starts one style/receipt/capture transaction for the current candidate.
    void CompleteLookLabPostRenderCaptures(); // Returns Capture results to the matching Look Lab transaction.
    void ApplyDemoDirectorTickResult(
        const DemoDirectorTickResult& result ); // Applies Direction's style/reveal/camera commands in authored order.
    void CancelPendingLookLabSave( const char* reason ); // Finalizes a pending receipt before scene or process teardown.
    void PrepareSceneScopedOwnersForTransition(); // Joins forecast work and clears presentation candidates before load.
    SkullbonezCore::Core::SbResult LoadSceneRequest( SceneLoadTransaction& transaction, const SceneLoadRequest& request );
    bool ExecutePendingSceneRequests( SceneLoadTransaction& transaction );
    void ApplySceneLoadRuntimeReactions( SceneLoadTransaction& transaction );
    SkullbonezCore::Core::SbResult RunUIStressActions();

    void Render( const RuntimeRenderModelFrameView& renderModels,
                 float presentationAlpha ); // Skips 3D in text-only runs, then records passes for the current camera state.
    void UpdateLogic( float simulationDt, float cameraDt,
                      float presentationAlpha ); // Scaled frame logic and unscaled camera time; any solver ticks have

    // already committed at fixed timestep.
    void AfterPhysicsStep(); // Post-step hooks that must see committed physics state.

    // Per-frame tick helpers (called from Execute()):
    float TickPhysics( double dt, bool capturePresentationPinned,
                       const SceneFrameProceedPolicy&
                           proceedPolicy ); // Returns scheduler alpha; render applies interpolation/capture pinning.
    bool TickScreenshots( const SceneFrameProceedPolicy& proceedPolicy );  // Screenshot triggers; true restarts frame.
    void TickAutoCycle( const SceneFrameProceedPolicy& proceedPolicy );    // Auto-cycle capture; may post WM_QUIT.
    bool TickSceneAdvance( const SceneFrameProceedPolicy& proceedPolicy ); // Completion/load policy; true restarts frame.
#ifdef _DEBUG
    void LogSceneFinished( const char* reason );
#endif

    // Lifetime: replays the operator's scrubber/predict/target/pause sequence
    // once, then never runs again in this process. It cannot run from
    // ApplyStartupOverrides because that boundary precedes the first scene load,
    // so the named body does not exist yet; the frame loop retries until it does.
    void ApplyStartupPredictionRequest();
#if defined( SKULLBONEZ_DEVELOPMENT_TOOLS )
    void ApplyDevelopmentUiMode(); // Reapplies the process-lifetime surface selection.
    void SelectDevelopmentUiSurface(
        DevelopmentUiMode surface ); // Atomically hides the source before showing the target surface.
#endif

  public:
    Run( SkullbonezCore::Core::SbDiagnosticStore& resultDiagnostics, Window& window, std::vector<std::string> sceneQueue,
         SkullbonezCore::Core::EngineConfig& config, Threading::WorkerPool& workerPool,
         SkullbonezCore::Core::Profiler* profiler, Rendering::Dx12BackbufferCapture& backbufferCapture,
         SkullbonezCore::Core::DevelopmentTools::TracyClientOwner* tracyClientOwner =
             nullptr ); // sceneQueue empty string selects generated demo mode.
    SkullbonezCore::Core::SbResult BindRenderBackend( Rendering::RenderBackendDX12& backend );
    ~Run();
    void Initialise(); // Initialises shared resources and loads first scene
    const SkullbonezCore::Core::SbResult&
    LastSceneLoadResult() const; // Initialise scene-load result for CLI startup checks.
    SkullbonezCore::Core::SbResult
    ApplyStartupOverrides( const RunStartupOverrides& overrides ); // Apply parsed CLI/startup policy before Initialise().
    SkullbonezCore::Core::SbResult
    FinalizeInteractionAutomationReport( const SkullbonezCore::Core::SbResult&
                                             processStatus ); // Publishes required report before startup or Execute returns.
    SkullbonezCore::Core::SbResult
    RunSceneLoadOnly( const char* snapshotOutPath = nullptr ); // Scene-load smoke path; skips the frame loop.
    SkullbonezCore::Core::SbResult Execute();                  // Main message loop; returns recoverable runtime failures.
};
} // namespace Runtime
} // namespace SkullbonezCore
