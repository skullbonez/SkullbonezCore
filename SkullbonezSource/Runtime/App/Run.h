/*
File: SkullbonezSource/Runtime/App/Run.h
Purpose:
  Coordinates the main game loop and high-level runtime lifecycle.

Summary:
  Run constructs process-lifetime owners, sequences their fixed frame phases,
  and passes typed values between them without absorbing domain business state.

Mental model:
  Run is the process composition root and frame sequencer. Its ordered
  coordinators may reach composed members directly; delegated domain operations
  receive only the concrete owners and values they use.

Glossary:
  Attached camera target: Runtime follow selection where Run owns the selected
    identity while physics stores own live target pose and motion.
  DX11/OpenGL: Retired runtime renderers. Their source backends have been
  removed; old command-line values now fail early.
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
  - SkullbonezSource/Runtime/App/Run.cpp
  - SkullbonezSource/Runtime/App/InputFrame.h
  - SkullbonezSource/Runtime/Render/RuntimeRenderer.h
  - SkullbonezSource/Runtime/Render/RuntimeRenderResources.h
  - Agentic/Reference/runtime-reference.md
  - Agentic/Reference/comment-style-guide.md
  - Agentic/Reference/engine-glossary.md
*/
#pragma once


#include <cassert>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <vector>
#include "../../Core/SbResult.h"
#include "../../Assets/AssetSystem.h"
#include "ApplicationExitState.h"
#include "InputFrame.h"
#include "../Camera/AttachedCameraController.h"
#include "../Direction/LookLabController.h"
#include "../Input/InputRouter.h"
#include "../Diagnostics/DiagnosticsRuntime.h"
#include "../Render/RenderDefaultsStore.h"
#include "../Interaction/RuntimeInteractionController.h"
#include "../Render/RuntimeRenderHost.h"
#include "../Render/RuntimeRenderer.h"
#if defined( SKULLBONEZ_DEVELOPMENT_TOOLS )
#include "../DevelopmentTools/ImGuiEditorOwner.h"
#endif
#include "RunLaunchOptions.h"
#include "../Camera/CameraControlState.h"
#if defined( SKULLBONEZ_AUTOMATION_DIAGNOSTICS )
#include "../Automation/InteractionAutomationController.h"
#endif
#include "RunStartupState.h"
#include "RunTimerState.h"
#include "ReplayRuntime.h"
#include "../Scene/SceneController.h"
#include "../Simulation/SimulationSystem.h"
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
} // namespace Rendering
namespace Threading
{
class WorkerPool;
}
namespace UI
{
class InGameUI;
struct OperatorEditorFrameView;
} // namespace UI
namespace Runtime
{
class Window;
class RuntimeOverlayDiagnostics;
class RuntimeValidationHarness;
struct InteractionAutomationFrameResult;
struct ReplayPathPickInput;
struct RuntimeRenderModelFrameView;
struct RuntimeUiTextFrameFacts;
namespace ReplayOverlay
{
struct ReplayOverlayStateView;
}

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
    SkullbonezCore::Core::SbDiagnosticStore& m_resultDiagnostics;                                // App-owned immutable diagnostic lease store.
    Window& m_window;                                                                            // Startup-owned native window borrowed for process lifetime.
    Threading::WorkerPool& m_workerPool;                                                         // Startup-owned worker service borrowed for process lifetime.
    SkullbonezCore::Core::EngineConfig& m_config;                                                // Borrowed process config loaded and CLI-patched by Runtime/App/Init.cpp.
    SkullbonezCore::Core::Profiler* m_profiler;                                                  // Startup-owned profiler borrow; null outside profiling builds.
    SkullbonezCore::Core::DevelopmentTools::TracyClientOwner*
        m_tracyClientOwner;                                                                      // Startup-owned development profiler lifetime borrow; null when unavailable.
    Assets::AssetSystem m_assets;                                                                // Process source-asset registry shared by scene and renderer owners.
    SceneController m_sceneController;                                                           // Owns scene queue, cameras, world, entities, physics, and models.
    SkullbonezCore::Core::SbResult m_lastSceneLoadResult;                                        // Last queue load outcome observed by startup/load-only paths.
    bool m_skipExecute = false;                                                                  // Startup-only probes can complete without entering the frame loop.
    RunLaunchOptions m_launchOptions;                                                            // CLI/startup policy reapplied across scene loads.
    ApplicationExitState m_applicationExit;                                                      // First-failure exit latch resolved by the platform message loop.
    RenderDefaultsStore m_renderDefaults;                                                        // Deferred ordinary/cinematic engine.cfg persistence owner.
    RunStartupState m_startup;                                                                   // engine.cfg startup capacity/thread defaults restored by demo resets.

    // Subsystem owners below are ordered by lifetime dependency. Renderer and
    // frame bindings borrow from these objects; they do not own them.
    DiagnosticsRuntime m_diagnosticsRuntime;                                                     // Capture, perf, and queryable physics diagnostics owner.
    RunTimerState m_timers;                                                                      // Frame/simulation timers and rolling timing values
    InputRouter m_inputRouter;                                                                   // Owns keyboard/pointer edge memory and binding-context enforcement.
    RuntimeInteractionController m_interaction;                                                  // Authoritative runtime workspace and world-input owner.
#if defined( SKULLBONEZ_AUTOMATION_DIAGNOSTICS )
    InteractionAutomationController
        m_interactionAutomation;                                                                 // Automation-build CLI harness that injects runtime mouse input for regression tests.
#endif
    CameraControlState m_camera;                                                                 // Camera/input state and ball-tracking settings
    AttachedCameraController m_attachedCamera;                                                   // Owns non-serialized Attach target/orbit/follow state.
    LookLabController m_lookLab;                                                                 // Owns the current presentation-only authoring candidate.
    SimulationSystem m_simulation;                                                               // Simulation timestep policy and physics accumulators
    ReplayRuntime m_replayRuntime;                                                               // Constructs and sequences the concrete replay domain owners.
    RuntimeTools m_runtimeTools;                                                                 // Launcher, editor, manipulator state, and transient render feedback.
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
    std::unique_ptr<RuntimeValidationHarness> m_validationHarness;                               // Owns opt-in live-style and graphics-stress controls.
    Rendering::Dx12BackbufferCapture& m_backbufferCapture;                                       // Required process-lifetime screenshot/readback owner.
    std::unique_ptr<RuntimeRenderer> m_renderer;                                                 // Created once startup binds the concrete backend owners.
    std::optional<std::reference_wrapper<Rendering::Dx12ShaderDevelopment>>
        m_shaderDevelopment;                                                                     // Explicit developer-only shader reload capability.

    // Concept: these value-only results carry decisions between adjacent frame
    // phases. They are stack state, not replacement owners or retained context.
    struct FrameInputPhaseResult
    {
        SceneFrameProceedPolicy proceedPolicy;
        bool legacyDevelopmentUiActive = true;
    };
    struct FrameSimulationPhaseResult;
    struct FrameRenderPhaseResult;
    struct FramePresentationFacts
    {
        float presentationAlpha = 1.0f;
        bool capturePresentationPinned = false;
        double secondsPerFrame = 0.0;
        bool legacyDevelopmentUiActive = true;
    };

    RuntimeRenderer& Renderer()
    {
        assert( m_renderer );
        return *m_renderer;
    }
    const RuntimeRenderer& Renderer() const
    {
        assert( m_renderer );
        return *m_renderer;
    }
    Rendering::Dx12BackbufferCapture& BackbufferCapture() const
    {
        return m_backbufferCapture;
    }

    bool PumpFrameMessages( int& messageExitCode );                                              // Bounded Win32 drain; true ends the frame loop.
    double BeginFrameTurn();                                                                     // Starts timing/profiling and validates renderer composition.
    void BeginFrameDiagnosticsPhase();                                                           // Publishes prior GPU timing, then resets draw counters.
#if defined( SKULLBONEZ_AUTOMATION_DIAGNOSTICS )
    FrameInputPhaseResult RunAutomationAndInputPhase();
#endif
    FrameInputPhaseResult RunInputPhase( const InteractionAutomationFrameResult* automationBeforeInput );
    FrameSimulationPhaseResult RunSimulationPhase( double secondsPerFrame, const SceneFrameProceedPolicy& proceedPolicy );
    FrameRenderPhaseResult PrepareRenderPhase( bool legacyDevelopmentUiActive,
                                               const FrameSimulationPhaseResult& simulation );
    RuntimeRenderModelFrameView PublishRenderModelsPhase();
    void RenderWorldPhase( const RuntimeRenderModelFrameView& renderModels, float presentationAlpha );

    void RenderOperatorUiPhase( const RuntimeRenderModelFrameView& renderModels, const FramePresentationFacts& facts );
    void RunPostDrawDiagnosticsPhase( bool legacyDevelopmentUiActive );
    void FinishFrameWorkPhase( const SceneFrameProceedPolicy& proceedPolicy );
    void PresentFramePhase();
    bool CompleteFramePhase( const SceneFrameProceedPolicy& proceedPolicy );

    // Ordered frame sub-coordinators retain direct composition-root reach. The
    // domain operations they call receive concrete operands only.
    RuntimeUIFrameResult ApplyInputCommandsPhase( RuntimeUIFrameResult result, bool keyboardToggleEditorMode,
                                                  const RuntimeInputFrameFacts& facts );
    bool ApplyLookLabSeed( uint64_t seed );                                                      // Resolves and applies one presentation-only candidate.
    void BeginLookLabSave();                                                                     // Starts one style/receipt/capture transaction for the current candidate.
    void CompleteLookLabPostRenderCaptures();                                                    // Returns Capture results to the matching Look Lab transaction.
    void CancelPendingLookLabSave( const char* reason );                                         // Finalizes a pending receipt before scene or process teardown.
    void PrepareLookLabForSceneTransition();                                                     // Clears the candidate and restores process presentation defaults.
    SkullbonezCore::Core::SbResult RunUIStressActions( RunCameraMode replayRestoreCameraMode );

    void Render( const RuntimeRenderModelFrameView& renderModels,
                 float presentationAlpha );                                                      // Skips 3D in text-only runs, then records passes for the current camera state.
    void UpdateLogic( float simulationDt, float cameraDt,
                      float presentationAlpha );                                                 // simulationDt drives physics; cameraDt is unscaled wall time.
    void AfterPhysicsStep();                                                                     // Post-step hooks that must see committed physics state.

    // --- Per-frame tick helpers (called from Execute()) ---
    float TickPhysics( double dt, bool capturePresentationPinned,
                       const SceneFrameProceedPolicy& proceedPolicy );                           // Returns the live fixed-tick interpolation fraction.
    bool TickScreenshots( const SceneFrameProceedPolicy& proceedPolicy );                        // Screenshot triggers; true restarts frame.
    void TickAutoCycle( const SceneFrameProceedPolicy& proceedPolicy );                          // Auto-cycle capture; may post WM_QUIT.
    bool TickSceneAdvance( const SceneFrameProceedPolicy& proceedPolicy );                       // Completion/load policy; true restarts frame.
#if defined( SKULLBONEZ_DEVELOPMENT_TOOLS )
    void ApplyDevelopmentUiMode();                                                               // Reapplies the process-lifetime surface selection.
    void SelectDevelopmentUiSurface( DevelopmentUiMode surface );                                // Atomically hides the source before showing the target surface.
#endif

  public:
    Run( SkullbonezCore::Core::SbDiagnosticStore& resultDiagnostics, Window& window, std::vector<std::string> sceneQueue,
         SkullbonezCore::Core::EngineConfig& config, Threading::WorkerPool& workerPool,
         SkullbonezCore::Core::Profiler* profiler, Rendering::Dx12BackbufferCapture& backbufferCapture,
         SkullbonezCore::Core::DevelopmentTools::TracyClientOwner* tracyClientOwner = nullptr ); // sceneQueue empty string selects generated demo mode.
    SkullbonezCore::Core::SbResult
    BindRenderBackend( Rendering::Dx12RenderDevice& renderDevice, Rendering::Dx12FrameOwner& renderFrame,
                       Rendering::Dx12GraphTransientPool& renderGraph, Rendering::Dx12ResourceBuilder& renderResources,
                       Rendering::Dx12TextureOwner& renderTextures, Rendering::Dx12GeometryOwner& renderGeometry,
                       Rendering::Dx12Diagnostics& renderDiagnostics, Rendering::Dx12RaytracingOwner& raytracing,
                       bool raytracingAvailable,
                       std::optional<std::reference_wrapper<Rendering::Dx12ShaderDevelopment>> shaderDevelopment
#if defined( SKULLBONEZ_DEVELOPMENT_TOOLS )
                       ,
                       Rendering::Dx12ImGuiRendererOwner& developmentUiRenderer
#endif
    );
    ~Run();
    void Initialise();                                                                           // Initialises shared resources and loads first scene
    const SkullbonezCore::Core::SbResult& LastSceneLoadResult() const;                           // Initialise scene-load result for CLI startup checks.
    SkullbonezCore::Core::SbResult
    ApplyStartupOverrides( const RunStartupOverrides& overrides );                               // Apply parsed CLI/startup policy before Initialise().
    SkullbonezCore::Core::SbResult
    RunSceneLoadOnly( const char* snapshotOutPath = nullptr );                                   // Scene-load smoke path; skips the frame loop.
    SkullbonezCore::Core::SbResult Execute();                                                    // Main message loop; returns recoverable runtime failures.
};
} // namespace Runtime
} // namespace SkullbonezCore
