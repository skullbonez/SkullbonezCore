/*
File: SkullbonezSource/Runtime/App/RunFrame.cpp
Purpose:
  Runs one frame of input, simulation, rendering, profiling, and presentation.

Summary:
  RunFrame.cpp runs one frame of input, simulation, rendering, profiling, and
  presentation. As an implementation unit, keep edits anchored on local owner
  boundaries and call direction and on the glossary/invariants below.

Mental model:
  Execute is the visible phase schedule. Each private phase borrows only the
  established frame views plus small value results, performs one contiguous
  span of the schedule, and returns control without retaining frame state.

Glossary:
  Simulation tick: One runtime decision about whether to advance logic, camera,
    and zero or more fixed physics steps this frame.
  Fixed-step edge: Runtime-owned code that repairs model/body topology before
    PhysicsEngine::Step and applies presentation-only refresh work after it.
  PhysicsBodyStore: Physics-owned body rows for live pose, velocity, fixed
    state, and replay identity.
  ColliderStore: Physics-owned hot collider rows plus per-kind shape payloads,
    material parameters, and broadphase radius.
  Lane R result: Recoverable scene-control or capture failure that prevents a
    failed side effect from being reported as a successful frame transition.
  Presentation pin: Per-frame alpha override to exact current solver state for
    scheduled and auto-cycle capture automation.
  Frame view: Non-copyable stack record of references used to name per-call
    borrows without moving ownership out of the composition root.
  Submitted-frame mark: Development profiler boundary emitted only after DX12
    accepts a successful Present for the game frame.
  Shared editor view: Frame-owned storage passed to the operator-editor
    composer and then consumed by the selected development frontend.
  Development UI apply result: One automation-owned batch outcome containing
    only a recoverable status and an optional Run-owned surface selection.
  Input turn result: Value-only process request published after the input owner
    interprets semantic actions; Run never reopens the action array.
  FIFO (First In, First Out): Platform-message order retained when the bounded
    drain defers excess messages to the next frame.

Invariants:
  - Frame work updates input, simulation, capture, rendering, and diagnostics
    in a stable order used by validation and replay comparisons.
  - Capture pinning is decided before physics and camera work for that frame.
  - Frame views are created once per frame turn and never retained by helpers.
  - A successful submitted game frame emits exactly one development profiler
    frame mark; failed or capture-only turns emit none.
  - A development surface swap hides the source before the target begins a frame.
  - Run sequences development UI automation but retains only process-wide
    surface selection and application-failure policy.
  - Physics and input owners publish complete policy/results; Run applies them
    without reconstructing or overriding their domain decisions.

Related:
  - SkullbonezSource/Runtime/RuntimeFrameViews.h defines the frame-helper calling convention.
  - SkullbonezSource/Runtime/UI/OperatorEditorFrameComposer.cpp owns operator UI projection.
  - Agentic/Reference/runtime-reference.md
  - Agentic/Reference/comment-style-guide.md
*/
#include "Run.h"
#include "../Diagnostics/RuntimeOverlayDiagnostics.h"
#include "../Automation/RuntimeValidationHarness.h"
#include "../RuntimeFrameViews.h"
#include "../UI/RuntimeViewModel.h"
#include "../Render/RenderModelFramePublisher.h"
#include "../UI/OperatorEditorFrameComposer.h"
#include "Window.h"
#include "../../Core/WorkerPool.h"
#include "InputFrame.h"
#include "../Replay/ReplayRestoreTransactions.h"
#include "../Planning/ReplayOverlayPackets.h"
#include "../Direction/DemoDirectorPlayback.h"
#include "../Scene/SceneRuntimeLoad.h"
#include "../Scene/SceneLoadTransaction.h"

#include "../Capture/CaptureSystem.h"
#include "../Editor/EditorTools.h"
#include "../../Core/Allocation/RuntimeAllocationTracker.h"
#include "../../Core/Allocation/RuntimeReserveAllocator.h"
#include "../../Core/TracyClientOwner.h"
#if defined( SKULLBONEZ_DEVELOPMENT_TOOLS )
#include "../DevelopmentTools/ImGuiEditorOwner.h"
#endif
#include "../Scene/SceneRuntimeStyle.h"

#include "../../Core/FatalError.h"
#include "../../Core/Log.h"
#include "../../Core/Profiler.h"
#include "../../Physics/ColliderStore.h"
#include "../../Physics/PhysicsEngine.h"
#include "../../Physics/PhysicsApi.h"
#include "../../Physics/PhysicsDiagnosticsSink.h"
#include "../../Physics/PhysicsTimestep.h"
#include "../../Rendering/RenderInstanceStore.h"
#include "../../Rendering/DX12/Dx12Diagnostics.h"
#include "../../UI/UI.h"
#include "../../UI/UITabEditor.h"

#include <algorithm>
#include <cmath>
#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <limits>
#include <utility>
#include <vector>

using namespace SkullbonezCore::Runtime;
using namespace SkullbonezCore::Runtime::ReplayTimelineOperations;
using namespace SkullbonezCore::Math::CollisionDetection;
using namespace SkullbonezCore::Math::Orientation;
using namespace SkullbonezCore::Math::Transformation;
using namespace SkullbonezCore::Physics;
using namespace SkullbonezCore::Runtime::RunInternal;
using SkullbonezCore::Math::Vector::Vector3;
namespace CoreAllocation = SkullbonezCore::Core::Allocation;

namespace
{

// Why: Profile builds do not emit Debug-only scene-finished telemetry, so
// automation exits need an explicit stdout breadcrumb near the quit request.
void PrintRuntimeExitReason( const char* reason )
{
    printf( "[runtime-exit] %s\n", reason );
    fflush( stdout );
}

float ResolvePresentationAlpha( const SkullbonezCore::Core::EngineConfig& config, bool capturePresentationPinned,
                                float simulationPresentationAlpha )
{

    if ( !config.runtimeRender.presentationInterpolation || capturePresentationPinned )
    {
        return 1.0f;
    }

    return std::clamp( simulationPresentationAlpha, 0.0f, 1.0f );
}

} // namespace

namespace
{

// Lifetime: this fixed post-step operation receives only its replay-capture
// inputs. It cannot reach unrelated frame owners through the root view slices.
void CaptureReplayPostStep( RuntimeTools& runtimeTools, SkullbonezCore::Runtime::SceneController& sceneController,
                            const RuntimeOverlayDiagnostics& overlays, ReplayRuntime& replayRuntime,
                            SkullbonezCore::Core::Profiler* profiler )
{
    const SceneSessionState& scene = sceneController.State();
    const OverlayDebugState debug = overlays.PresentationSnapshot();
    SkullbonezCore::Environment::CameraCollection& cameras = sceneController.Scene().Cameras();
    SkullbonezCore::Environment::WorldEnvironment& world = sceneController.Scene().Environment();
    PhysicsEngine& physics = sceneController.Scene().Physics();
    const SceneEntityStore& entities = sceneController.Scene().Entities();
    CoreAllocation::RuntimeAllocationScope allocationScope( CoreAllocation::RuntimeAllocationPhase::Replay );
    PROFILE_SCOPED( profiler, "Frame/Physics/Step/ReplayCapture" );
    ReplayWorldPresentationSample worldSample;
    worldSample.gravity = world.GetGravity();
    worldSample.fluidHeight = world.GetFluidSurfaceHeight();
    worldSample.fluidDensity = world.GetFluidDensity();
    worldSample.fixedStep = scene.isFixedStep;
    worldSample.scenePhysicsEnabled = scene.isScenePhysics;
    worldSample.sceneTextEnabled = scene.isSceneText;
    worldSample.waterHidden = debug.isWaterHidden;
    worldSample.terrainHidden = debug.isTerrainHidden;

    ReplayCameraSample cameraSample;
    cameraSample.eye = cameras.GetCameraTranslation();
    cameraSample.view = cameras.GetCameraView();
    cameraSample.up = cameras.GetCameraUp();

    replayRuntime.CaptureFrame( scene.currentFrame, PHYSICS_FIXED_DT, worldSample, cameraSample, physics,
                                sceneController.Scene().Tornado(), entities, sceneController.Scene().BodyStore(),
                                sceneController.Scene().Colliders(), runtimeTools );
}

} // namespace

struct Run::FrameInputPhaseResult
{
    SceneFrameProceedPolicy proceedPolicy;
    bool legacyDevelopmentUiActive = true;
};

struct Run::FrameSimulationPhaseResult
{
    float interpolationAlpha = 1.0f;
    bool capturePresentationPinned = false;
};

struct Run::FrameRenderPhaseResult
{
    SkullbonezCore::Core::SbResult status = SkullbonezCore::Core::SbResult::Success();
    float presentationAlpha = 1.0f;
};

struct Run::FramePresentationFacts
{
    float presentationAlpha = 1.0f;
    bool capturePresentationPinned = false;
    double secondsPerFrame = 0.0;
    bool legacyDevelopmentUiActive = true;
};

bool Run::PumpFrameMessages( int& messageExitCode )
{
    MSG msg;
    constexpr int kMaxMessagesPerFrame = 256;
    int messagesDrained = 0;

    // Hazard: a device or window can flood the thread queue faster than frame
    // work consumes it. The cap defers excess FIFO messages to the next frame.

    while ( messagesDrained < kMaxMessagesPerFrame && PeekMessage( &msg, nullptr, 0, 0, PM_REMOVE ) )
    {
        ++messagesDrained;

        if ( msg.message == WM_QUIT )
        {
            m_validationHarness->PrintGraphicsStressExitSummary( m_sceneController.State().currentFrame );

            // Concept: WM_QUIT is the platform stop notification, not the
            // process result. An earlier Run-owned failure remains authoritative.
            m_applicationExit.RequestNormalExit();
            messageExitCode = static_cast<int>( msg.wParam );
            return true;
        }

        TranslateMessage( &msg );
        DispatchMessage( &msg );
    }

    return false;
}

double Run::BeginFrameTurn()
{
    double secondsPerFrame = m_timers.frameTimer.GetElapsedTime();
    secondsPerFrame = std::clamp( secondsPerFrame, 0.0, 0.05 );
    m_timers.frameTimer.StartTimer();
    PROFILE_FRAME_BEGIN( m_profiler );
    m_timers.workTimer.StartTimer();

    // Lifetime: every facet is a startup-owned borrow for this synchronous
    // frame turn. A missing facet is a composition invariant failure.

    if ( !m_renderBackendView.renderDevice || !m_renderBackendView.renderDiagnostics ||
         !m_renderBackendView.renderResources || !m_renderBackendView.renderFrame || !m_renderBackendView.renderGraph ||
         !m_renderBackendView.renderTextures || !m_renderBackendView.renderGeometry )
    {
        SB_FATAL( "RunFrame", "Run::Execute requires a render backend." );
    }

    return secondsPerFrame;
}

RuntimeFrameHostView Run::BuildFrameHostView()
{
    return RuntimeFrameHostView { m_applicationExit, m_diagnosticsRuntime, m_assets, m_workerPool, m_window, m_profiler };
}

RuntimeFrameInteractionView Run::BuildFrameInteractionView()
{
    return RuntimeFrameInteractionView { m_inputRouter, m_interaction,  m_attachedCamera,
                                         *m_operatorUi, m_runtimeTools, m_camera };
}

RuntimeFrameSceneView Run::BuildFrameSceneView()
{
    return RuntimeFrameSceneView { m_config,     m_launchOptions,  m_startup, m_timers, *m_overlayDiagnostics,
                                   m_simulation, m_sceneController };
}

RuntimeFramePresentationView Run::BuildFramePresentationView()
{
    return RuntimeFramePresentationView { m_renderDefaults, *m_validationHarness, m_renderBackendView, m_renderer };
}

void Run::BeginFrameDiagnosticsPhase()
{

    // Frame boundary: publish prior-frame GPU counters before resetting the
    // diagnostics storage that records this turn.
    m_renderer.BeginProfilerFrame();
    m_renderBackendView.renderDiagnostics->ResetFrameDrawCalls();
}

#if defined( SKULLBONEZ_AUTOMATION_DIAGNOSTICS )
InteractionAutomationFrameResult Run::RunAutomationBeforeInputPhase( RuntimeFrameInteractionView& interaction,
                                                                     RuntimeFrameSceneView& scene )
{
    const ReplayAutomationView automationReplayView = m_replayRuntime.BuildAutomationView();
    const ReplayInputView automationReplayInput = automationReplayView.input;
    const InteractionAutomationFrameResult result = TickInteractionAutomationBeforeInput( m_interactionAutomation, m_window,
                                                                                          interaction, scene,
                                                                                          automationReplayView,
                                                                                          m_renderer.FrameGraphSnapshot() );

#if defined( SKULLBONEZ_DEVELOPMENT_TOOLS )
    const InteractionAutomationDevelopmentUiApplyResult
        developmentUiApply = m_interactionAutomation.ApplyDevelopmentUiCommands( result, m_window, m_imguiEditor );

    if ( developmentUiApply.selectSurface )
    {
        SelectDevelopmentUiSurface( developmentUiApply.surface );
    }

    if ( !developmentUiApply.status.ok )
    {
        m_applicationExit.RequestOwnedFailure( developmentUiApply.status );
    }
#endif

    if ( result.applyCameraMode )
    {
        m_inputRouter.ApplyCameraMode( result.cameraMode, RuntimeInputActionSource::Runtime, interaction, m_sceneController,
                                       m_replayRuntime, m_inputRouter.RuntimeContext() );
    }

    (void)m_replayRuntime.ApplyFrameIntent( result.replayIntent );

    if ( result.setWorldInteractionOwner )
    {
        const SceneSessionState& sceneState = m_sceneController.State();
        const int sceneEntityCount = m_sceneController.Scene().SceneEntityCount();
        const uint32_t cameraModeEnabledMask = RuntimeCameraModeEnabledMask( sceneState.isSceneMode, sceneEntityCount );
        const RunCameraMode normalizedRestoreMode = NormalizeRuntimeCameraMode( automationReplayInput.restoreCameraMode,
                                                                                sceneState.isSceneMode,
                                                                                cameraModeEnabledMask );

        m_inputRouter.SetWorldInteractionOwner( result.worldInteractionOwner, result.worldInteractionReason, interaction,
                                                m_sceneController, m_replayRuntime, normalizedRestoreMode );
    }

    if ( !result.status.ok )
    {
        m_applicationExit.RequestOwnedFailure( result.status );
    }

    if ( result.requestQuit )
    {
        PostQuitMessage( 0 );
    }

    return result;
}
#endif

Run::FrameInputPhaseResult Run::RunInputPhase( RuntimeFrameHostView& host, RuntimeFrameInteractionView& interaction,
                                               RuntimeFrameSceneView& scene, RuntimeFramePresentationView& presentation,
                                               const InteractionAutomationFrameResult* automationBeforeInput )
{
    UiInputCaptureIntent developmentUiCapture;
    SkullbonezCore::UI::OperatorEditorCommandQueues developmentEditorCommands;
    bool legacyDevelopmentUiActive = true;
#if defined( SKULLBONEZ_DEVELOPMENT_TOOLS )
    developmentUiCapture = m_imguiEditor.ConsumeInputCaptureIntent();
    developmentEditorCommands = m_imguiEditor.ConsumeOperatorEditorCommands();
#if defined( SKULLBONEZ_AUTOMATION_DIAGNOSTICS )

    if ( automationBeforeInput )
    {
        const SkullbonezCore::Core::SbResult
            submitStatus = m_interactionAutomation.SubmitOperatorEditorReplayCommand( *automationBeforeInput,
                                                                                      developmentEditorCommands );

        if ( !submitStatus.ok )
        {
            m_applicationExit.RequestOwnedFailure( submitStatus );
        }
    }
#else
    (void)automationBeforeInput;
#endif
    legacyDevelopmentUiActive = m_imguiEditor.SelectedSurface() == DevelopmentUiMode::Legacy;
#else
    (void)automationBeforeInput;
#endif
    [[maybe_unused]] const InputFrameExecutionResult inputFrameResult = ProcessInputFrame( host, interaction, scene,
                                                                                           presentation, m_replayRuntime,
                                                                                           developmentUiCapture,
                                                                                           developmentEditorCommands,
                                                                                           legacyDevelopmentUiActive );
#if defined( SKULLBONEZ_DEVELOPMENT_TOOLS )

    if ( m_launchOptions.developmentUiModeExplicit || m_imguiEditor.HasActivatedSurfaceSelection() )
    {

        // Invariant: scene load may apply a Legacy default during input. An
        // explicit process selection wins before either UI begins its frame.
        SelectDevelopmentUiSurface( m_imguiEditor.SelectedSurface() );
    }

    if ( inputFrameResult.requestDevelopmentUiSurfaceSwap )
    {
        SelectDevelopmentUiSurface( DevelopmentUiMode::ImGui );
    }

    // ProcessInputFrame may consume Ctrl+0 after its snapshot; resample only
    // after every pre-render swap so both surfaces cannot draw concurrently.
    legacyDevelopmentUiActive = m_imguiEditor.SelectedSurface() == DevelopmentUiMode::Legacy;
#endif
    const SceneFrameProceedPolicy proceedPolicy = m_sceneController.BuildFrameProceedPolicy( m_inputRouter.RuntimeSnapshot().frameInput.stepHeld );

    m_validationHarness->TickLiveStyle( m_launchOptions, m_sceneController.State(), m_operatorUi->SceneNavigation().browser,
                                        m_sceneController.Scene(), m_assets,
                                        ActiveSceneCinematicConfig( m_sceneController.State(), m_config ),
                                        m_renderDefaults.CinematicBaseline() );

    return FrameInputPhaseResult { proceedPolicy, legacyDevelopmentUiActive };
}

Run::FrameSimulationPhaseResult Run::RunSimulationPhase( RuntimeFrameSceneView& scene, double secondsPerFrame,
                                                         const SceneFrameProceedPolicy& proceedPolicy )
{
    m_sceneController.Scene().BeginCollisionVisualFrame();

    // Invariant: capture pinning is fixed before physics and camera work. A
    // scheduled screenshot renders exact solver poses for this whole turn.
    const bool capturePresentationPinned = m_diagnosticsRuntime.Capture()
                                               .RequiresDeterministicPresentation( m_sceneController.State().isSceneMode,
                                                                                   m_sceneController.State().currentFrame,
                                                                                   m_timers.simulationTimer
                                                                                           .GetTimeSinceLastStart() *
                                                                                       1000.0 ) ||
                                           ( m_sceneController.State().isSceneMode && m_camera.autoCycleInterval > 0.0f ) ||
                                           m_validationHarness->HasPendingLiveStyleCapture()
#if defined( SKULLBONEZ_AUTOMATION_DIAGNOSTICS )
                                           || InteractionAutomationWillCaptureAfterRender( m_interactionAutomation,
                                                                                           m_sceneController.State()
                                                                                               .currentFrame )
#endif
        ;

    float interpolationAlpha = 1.0f;
    {
        CoreAllocation::RuntimeAllocationScope allocationScope( CoreAllocation::RuntimeAllocationPhase::Physics );
        interpolationAlpha = TickPhysics( secondsPerFrame, capturePresentationPinned, proceedPolicy );
    }
    {

        // Invariant: prediction publication completes before overlay and render
        // construction. Render cannot decide whether the private engine advances.
        CoreAllocation::RuntimeAllocationScope allocationScope( CoreAllocation::RuntimeAllocationPhase::Replay );
        m_replayRuntime.UpdatePrediction( scene.sceneController.Scene().Physics(), scene.sceneController.Scene().Tornado(),
                                          scene.sceneController.Scene().Entities(), scene.config,
                                          scene.sceneController.Scene().Environment().GetPhysicsWorldForces(), m_workerPool,
                                          scene.sceneController.State().isScenePhysics,
                                          scene.timers.simulationTimer.GetTimeSinceLastStart(),
                                          scene.timers.simulationTimer.GetTotalTime() );
    }
    scene.overlays.UpdatePostPhysics( scene.sceneController.Scene(), *m_validationHarness,
                                      scene.config.bodySimulation.contactEpsilon, secondsPerFrame );

    return FrameSimulationPhaseResult { interpolationAlpha, capturePresentationPinned };
}

Run::FrameRenderPhaseResult Run::PrepareRenderPhase( RuntimeFrameHostView& host, RuntimeFrameInteractionView& interaction,
                                                     RuntimeFrameSceneView& scene,
                                                     RuntimeFramePresentationView& presentation,
                                                     bool legacyDevelopmentUiActive,
                                                     const FrameSimulationPhaseResult& simulation )
{

    // Concept: graphics stress is render/runtime churn, not UI command work. It
    // runs once per rendered frame in headless and interactive configurations.
    presentation.validationHarness.ExecuteGraphicsStressFrame( host, interaction, scene, presentation, m_replayRuntime,
                                                               *presentation.renderBackendView.renderDiagnostics,
                                                               legacyDevelopmentUiActive );

    const float presentationAlpha = ResolvePresentationAlpha( scene.config, simulation.capturePresentationPinned,
                                                              simulation.interpolationAlpha );

    if ( presentation.renderer.PipelineSyncEnabled() )
    {
        PROFILE_BEGIN( host.profiler, "Frame/PipelineSync" );
        SkullbonezCore::Core::SbResult finishResult = SkullbonezCore::Core::SbResult::Success();
        {
            CoreAllocation::RuntimeAllocationScope allocationScope( CoreAllocation::RuntimeAllocationPhase::Render );
            finishResult = presentation.renderBackendView.renderFrame->FinishAndReopen( *presentation.renderBackendView.renderDiagnostics );
        }
        PROFILE_END( host.profiler, "Frame/PipelineSync" );

        if ( !finishResult.ok )
        {
            scene.timers.frameTimer.StopTimer();
            PROFILE_FRAME_END( host.profiler );
            host.applicationExit.RequestOwnedFailure( finishResult );
            return FrameRenderPhaseResult { finishResult, presentationAlpha };
        }
    }

    return FrameRenderPhaseResult { SkullbonezCore::Core::SbResult::Success(), presentationAlpha };
}

RuntimeRenderModelFrameView Run::PublishRenderModelsPhase()
{
    return PublishRenderModelFrame( m_sceneController.Scene(), m_workerPool, m_config );
}

void Run::RenderWorldPhase( const RuntimeRenderModelFrameView& renderModels, float presentationAlpha )
{
    PROFILE_BEGIN( m_profiler, "Frame/Render" );
    {
        CoreAllocation::RuntimeAllocationScope allocationScope( CoreAllocation::RuntimeAllocationPhase::Render );
        DRAW_CALL_TRACE_SCOPE( *m_renderBackendView.renderDiagnostics, "Frame/Render" );

        if ( !m_renderBackendView.renderGraph )
        {
            SB_FATAL( "RunFrame", "A rendered frame requires the startup-bound render command context." );
        }

        // Invariant: graph ownership begins before Render can take its text-only
        // path. World, UI, capture, and Present close the same graph exactly once.
        m_renderer.BeginFrameGraph( *m_renderBackendView.renderGraph );
        Render( renderModels, presentationAlpha );
    }
    PROFILE_END( m_profiler, "Frame/Render" );
}

SkullbonezCore::Core::SbResult
Run::RenderOperatorUiPhase( RuntimeFrameHostView& host, RuntimeFrameInteractionView& interaction,
                            RuntimeFrameSceneView& scene, RuntimeFramePresentationView& presentation,
                            const RuntimeRenderModelFrameView& renderModels, const FramePresentationFacts& facts )
{
#if defined( SKULLBONEZ_DEVELOPMENT_TOOLS )

    // Invariant: copy the completed world backbuffer before either operator
    // surface draws, preserving one presentation owner at a time.

    if ( m_imguiEditor.IsVisible() )
    {
        const SkullbonezCore::Core::SbResult viewportCapture = m_imguiEditor.CaptureGameViewport();

        if ( !viewportCapture.ok )
        {
            scene.timers.frameTimer.StopTimer();
            PROFILE_FRAME_END( host.profiler );
            host.applicationExit.RequestOwnedFailure( viewportCapture );
            return viewportCapture;
        }
    }
#endif

    SkullbonezCore::UI::OperatorEditorFrameView operatorEditorView;
#if defined( SKULLBONEZ_DEVELOPMENT_TOOLS )
    operatorEditorView.surfaces.secondaryVisible = m_imguiEditor.IsVisible();
#endif
    const RuntimeUiTextFrameFacts uiTextFacts { RuntimeCameraModeEnabledMask( scene.sceneController.State().isSceneMode,
                                                                              scene.sceneController.Scene()
                                                                                  .SceneEntityCount() ),
                                                m_camera.mode == RunCameraMode::Attach ? m_attachedCamera.ModeLabel()
                                                                                       : RunCameraModeLabel( m_camera.mode ),
                                                m_runtimeTools.LauncherFireModeLabel(),
                                                RunCameraModeUsesLauncher( m_camera.mode ),
                                                m_interaction.Gesture().kind,
                                                m_interaction.Gesture().gizmoKind,
                                                facts.presentationAlpha,
                                                facts.capturePresentationPinned,
                                                facts.secondsPerFrame,
                                                facts.legacyDevelopmentUiActive };

    const ReplayOverlay::ReplayOverlayStateView
        replayOverlay = m_replayRuntime.BuildOverlayStateView( m_runtimeTools.Editor().editorModeEnabled,
                                                               m_operatorUi->IsVisible(), m_operatorUi->IsMinimized(),
                                                               m_interaction.Gesture().kind,
                                                               renderModels.presentationRecords, renderModels.bodyStore );

    OperatorEditorFrameComposer::Render( host, interaction, scene, presentation.renderer, m_replayRuntime, uiTextFacts,
                                         operatorEditorView, replayOverlay, renderModels );

#if defined( SKULLBONEZ_DEVELOPMENT_TOOLS )
    const UINT windowDpi = GetDpiForWindow( m_window.NativeWindowHandle() );
    const float dpiScale = windowDpi > 0u ? static_cast<float>( windowDpi ) / 96.0f : 1.0f;
    const SkullbonezCore::Core::DevelopmentTools::TracyClientStatus
        tracyStatus = SkullbonezCore::Core::DevelopmentTools::TracyClientOwner::CopyStatus();

    const DevelopmentTools::ImGuiEditorFrameInput imguiFrameInput { m_window.ClientWidth(),
                                                                    m_window.ClientHeight(),
                                                                    dpiScale,
                                                                    static_cast<float>( facts.secondsPerFrame ),
                                                                    tracyStatus.initialized,
                                                                    tracyStatus.viewerConnected,
                                                                    tracyStatus.heavyMode };

    if ( m_imguiEditor.BeginFrame( imguiFrameInput ) )
    {
        m_imguiEditor.BuildEditorShell( operatorEditorView, replayOverlay );
        DevelopmentTools::ImGuiEditorFrameResult imguiResult = m_imguiEditor.EndFrame();

        if ( imguiResult.status.ok )
        {
            imguiResult.status = m_renderer.RenderDevelopmentUi( m_imguiEditor );
        }

        if ( !imguiResult.status.ok )
        {
            scene.timers.frameTimer.StopTimer();
            PROFILE_FRAME_END( host.profiler );
            host.applicationExit.RequestOwnedFailure( imguiResult.status );
            return imguiResult.status;
        }

        if ( imguiResult.commands.requestSurfaceSwap )
        {
            SelectDevelopmentUiSurface( DevelopmentUiMode::Legacy );
        }

        if ( imguiResult.commands.requestTracyStandardCapture )
        {
            bool tracyStarted = false;
#if defined( TRACY_ENABLE )

            if ( m_tracyClientOwner )
            {

                // Why: this explicit cold diagnostics action starts Tracy before
                // recreating workers so their instrumentation names are bound.
                CoreAllocation::RuntimeAllocationScope tracyStartScope( CoreAllocation::RuntimeAllocationPhase::Diagnostics );
                tracyStarted = m_tracyClientOwner->StartStandardCapture();

                if ( tracyStarted )
                {
                    m_workerPool.Initialise( m_config.runtimeCapacity.workerThreads );
                    m_workerPool.BindProfiler( m_profiler );
                }
            }
#endif
            m_imguiEditor.ReportTracyClientStartResult( tracyStarted );
        }
    }
#endif
    return SkullbonezCore::Core::SbResult::Success();
}

void Run::RunPostDrawDiagnosticsPhase( RuntimeFrameInteractionView& interaction, bool legacyDevelopmentUiActive )
{
    PROFILE_BEGIN( m_profiler, "Frame/PostDraw/LiveStyleCapture" );
    {
        CoreAllocation::RuntimeAllocationScope allocationScope( CoreAllocation::RuntimeAllocationPhase::Capture );
        m_validationHarness->SavePendingLiveStyleCapture( m_diagnosticsRuntime.Capture(),
                                                          m_renderBackendView.RequireBackbufferCapture() );
    }
    PROFILE_END( m_profiler, "Frame/PostDraw/LiveStyleCapture" );

#if defined( SKULLBONEZ_AUTOMATION_DIAGNOSTICS )
    PROFILE_BEGIN( m_profiler, "Frame/PostDraw/InteractionAutomation" );
    InteractionAutomationDevelopmentUiView automationDevelopmentUiView;
#if defined( SKULLBONEZ_DEVELOPMENT_TOOLS )
    const DevelopmentTools::ImGuiEditorStatus imguiAutomationStatus = m_imguiEditor.CopyStatus();
    automationDevelopmentUiView = m_interactionAutomation.BuildDevelopmentUiView( imguiAutomationStatus,
                                                                                  m_operatorUi->IsVisible(),
                                                                                  legacyDevelopmentUiActive );

#endif
    const InteractionAutomationFrameResult
        automationAfterRender = TickInteractionAutomationAfterRender( m_interactionAutomation, interaction,
                                                                      m_sceneController,
                                                                      m_replayRuntime.BuildAutomationView(),
                                                                      automationDevelopmentUiView,
                                                                      m_renderer.FrameGraphSnapshot(),
                                                                      m_diagnosticsRuntime.Capture(),
                                                                      m_renderBackendView.RequireBackbufferCapture() );

    if ( !automationAfterRender.status.ok )
    {
        m_applicationExit.RequestOwnedFailure( automationAfterRender.status );
    }

    if ( automationAfterRender.requestQuit )
    {
        PostQuitMessage( 0 );
    }

    PROFILE_END( m_profiler, "Frame/PostDraw/InteractionAutomation" );
#else
    (void)interaction;
    (void)legacyDevelopmentUiActive;
#endif
}

void Run::FinishFrameWorkPhase( const SceneFrameProceedPolicy& proceedPolicy )
{
    PROFILE_BEGIN( m_profiler, "Frame/PostDraw/AutoCycle" );
    TickAutoCycle( proceedPolicy );
    PROFILE_END( m_profiler, "Frame/PostDraw/AutoCycle" );
    m_timers.workTimer.StopTimer();
    m_timers.cpuFrameWorkMs = static_cast<float>( std::clamp( m_timers.workTimer.GetElapsedTime(), 0.0, 0.25 ) * 1000.0 );
}

SkullbonezCore::Core::SbResult Run::PresentFramePhase()
{
    PROFILE_BEGIN( m_profiler, "Frame/VsyncWait" );
    SkullbonezCore::Core::SbResult presentResult = SkullbonezCore::Core::SbResult::Success();
    {
        CoreAllocation::RuntimeAllocationScope allocationScope( CoreAllocation::RuntimeAllocationPhase::Render );

        // Invariant: the production graph has one declaration-only Present edge;
        // finalize it before the swap-chain owner submits this frame.
        m_renderer.FinalizeFrameGraph();
        presentResult = m_renderBackendView.renderFrame->Present( *m_renderBackendView.renderDiagnostics );
    }
    PROFILE_END( m_profiler, "Frame/VsyncWait" );

    if ( !presentResult.ok )
    {
        m_timers.frameTimer.StopTimer();
        PROFILE_FRAME_END( m_profiler );
        m_applicationExit.RequestOwnedFailure( presentResult );
        return presentResult;
    }

    // Invariant: Tracy counts submitted game frames, not attempted render turns,
    // capture-only continues, or failed Presents.
    SKORE_TRACY_MARK_SUBMITTED_FRAME();
    m_timers.frameTimer.StopTimer();
    PROFILE_FRAME_END( m_profiler );
    return SkullbonezCore::Core::SbResult::Success();
}

bool Run::CompleteFramePhase( const SceneFrameProceedPolicy& proceedPolicy )
{
#if defined( SKULLBONEZ_PROFILE_ENABLED )
    {
        const RuntimeProfilerFrameTimes profilerTimes = m_diagnosticsRuntime.SampleProfilerFrameTimes();
        m_timers.physicsTime = profilerTimes.physicsTimeSeconds;
        m_timers.renderTime = profilerTimes.renderTimeSeconds;
        m_timers.gpuFrameWorkMs = profilerTimes.gpuFrameWorkMs;
    }
#endif
    m_diagnosticsRuntime.TickPerfLog( m_sceneController.PerfPass() + 1, m_sceneController.State().currentFrame + 1,
                                      m_timers.physicsTime, m_timers.renderTime );

    return TickSceneAdvance( proceedPolicy );
}

SkullbonezCore::Core::SbResult Run::Execute()
{

    if ( m_skipExecute )
    {
        return SkullbonezCore::Core::SbResult::Success();
    }

    if ( m_applicationExit.ExitRequested() )
    {
        return m_applicationExit.Resolve( 0 );
    }

    int messageExitCode = 0;

    for ( ;; )
    {

        if ( PumpFrameMessages( messageExitCode ) )
        {
            break;
        }

        CoreAllocation::RuntimeAllocationScope frameAllocationScope {
            CoreAllocation::RuntimeAllocationPhase::SteadyGameplay };

        const double secondsPerFrame = BeginFrameTurn();

        // Lifetime: each stack view is built once and never retained.
        RuntimeFrameHostView host = BuildFrameHostView();
        RuntimeFrameInteractionView interaction = BuildFrameInteractionView();
        RuntimeFrameSceneView scene = BuildFrameSceneView();
        RuntimeFramePresentationView presentation = BuildFramePresentationView();
        BeginFrameDiagnosticsPhase();
        PROFILE_BEGIN( m_profiler, "Frame/Input" );
#if defined( SKULLBONEZ_AUTOMATION_DIAGNOSTICS )
        const auto automationResult = RunAutomationBeforeInputPhase( interaction, scene );
        const InteractionAutomationFrameResult* automation = &automationResult;
#else
        const InteractionAutomationFrameResult* automation = nullptr;
#endif
        const FrameInputPhaseResult input = RunInputPhase( host, interaction, scene, presentation, automation );
        PROFILE_END( m_profiler, "Frame/Input" );
        const auto simulation = RunSimulationPhase( scene, secondsPerFrame, input.proceedPolicy );
        const auto render = PrepareRenderPhase( host, interaction, scene, presentation, input.legacyDevelopmentUiActive,
                                                simulation );

        if ( !render.status.ok )
        {
            return m_applicationExit.Resolve( 0 );
        }

        RuntimeRenderModelFrameView models = PublishRenderModelsPhase();
        RenderWorldPhase( models, render.presentationAlpha );
        const auto facts = FramePresentationFacts { render.presentationAlpha, simulation.capturePresentationPinned,
                                                    secondsPerFrame, input.legacyDevelopmentUiActive };

        const auto operatorUiResult = RenderOperatorUiPhase( host, interaction, scene, presentation, models, facts );

        if ( !operatorUiResult.ok )
        {
            return m_applicationExit.Resolve( 0 );
        }

        RunPostDrawDiagnosticsPhase( interaction, input.legacyDevelopmentUiActive );
        {
            CoreAllocation::RuntimeAllocationScope allocationScope( CoreAllocation::RuntimeAllocationPhase::Capture );

            if ( TickScreenshots( input.proceedPolicy ) )
            {
                continue;
            }
        }
        FinishFrameWorkPhase( input.proceedPolicy );
        const SkullbonezCore::Core::SbResult presentResult = PresentFramePhase();

        if ( !presentResult.ok )
        {
            return m_applicationExit.Resolve( 0 );
        }

        if ( CompleteFramePhase( input.proceedPolicy ) )
        {
            continue;
        }
    }

    return m_applicationExit.Resolve( messageExitCode );
}


float Run::TickPhysics( double secondsPerFrame, bool capturePresentationPinned,
                        const SceneFrameProceedPolicy& proceedPolicy )
{

    // Why: simulation pacing is a reactive frame concern. Sampling the ledger
    // here keeps SimulationSystem out of every cold scene-load call surface.
    m_simulation.ObserveSceneLifecycle( m_sceneController.LifecyclePacket() );
    const ReplayInputView replayInput = m_replayRuntime.BuildInputView();

    if ( replayInput.scrubPaused )
    {
        PROFILE_SCOPED( m_profiler, "Frame/Replay/ScrubCamera" );
        UpdateLogic( 0.0f, static_cast<float>( secondsPerFrame ), 1.0f );
        return 1.0f;
    }

    const bool replayLiveAdvanceHeld = replayInput.liveAdvanceHeld;
    const RuntimeInputSnapshot& inputSnapshot = m_inputRouter.RuntimeSnapshot();
    const bool stepRequested = proceedPolicy.stepRequested;
    const bool replayCapture = replayInput.captureEnabled;
#ifdef _DEBUG
    const bool physicsCapture = m_diagnosticsRuntime.PerfLog().physicsRegressionLogOverride[0] != '\0' ||
                                m_diagnosticsRuntime.PerfLog().physicsCollisionTimeLogOverride[0] != '\0' ||
                                m_diagnosticsRuntime.PhysicsDiagnostics().isEnabled;
#else
    constexpr bool physicsCapture = false;
#endif
    RuntimeInteractionFrameInput interactionFrameInput;
    interactionFrameInput.scenePhysicsEnabled = m_sceneController.State().isScenePhysics;
    interactionFrameInput.stepHeld = stepRequested;
    interactionFrameInput.replayScrubbedHistoricalSample = false;
    interactionFrameInput.replayLiveHeldAtCurrentFrame = replayLiveAdvanceHeld;
    interactionFrameInput.crossScenePauseLocked = proceedPolicy.crossScenePauseLocked;
    interactionFrameInput.rightMouseLookHeld = inputSnapshot.pointer.rightDown;
    interactionFrameInput.editorViewportLookActive = m_runtimeTools.Editor().viewportLookActive;
    interactionFrameInput.replayInspectionLookActive = inputSnapshot.frameInput.replayInspectionLookActive;
    interactionFrameInput.forcePhysicsRunning = physicsCapture;
    interactionFrameInput.sceneTimeScale = m_sceneController.State().timeScale;
    const RuntimeInteractionFramePolicy policy = m_interaction.BuildFramePolicy( interactionFrameInput );
    const bool manipulatorPhysics = policy.manipulatorActive;
    const auto physicsWorldForces = m_sceneController.Scene().Environment().GetPhysicsWorldForces();
    constexpr bool canStepPhysics = true;
    const SimulationTickResult tick = m_simulation.Tick( SimulationTickInput { secondsPerFrame, policy.physicsTimeScale, m_sceneController.State().isSceneMode,
                                                                               m_sceneController.State().isScenePhysics, m_sceneController.State().isFixedStep,
                                                                               policy.physicsAdvance, stepRequested, canStepPhysics } );

    const float presentationAlpha = ResolvePresentationAlpha( m_config, capturePresentationPinned, tick.presentationAlpha );

    if ( tick.committedPhysicsTicks > 0 && canStepPhysics )
    {
        PROFILE_BEGIN( m_profiler, "Frame/Physics" );

        // Why: SimulationSystem now returns only a deterministic tick count.
        // Runtime executes the store-owned physics step directly, then applies
        // the remaining model-owned presentation sync as explicit edge work.

        for ( int tickIndex = 0; tickIndex < tick.committedPhysicsTicks; ++tickIndex )
        {
            PROFILE_SCOPED( m_profiler, "Frame/Physics/Step" );
            {
                PROFILE_SCOPED( m_profiler, "Frame/Physics/Step/PresentationCaptureBegin" );
                m_sceneController.Scene().BeginPhysicsStepPresentationCapture();
            }

            if ( manipulatorPhysics )
            {
                m_runtimeTools.ApplyMousePickupPhysicsStep( m_sceneController.Scene(), m_inputRouter, m_interaction );
            }

            SkullbonezCore::Rendering::RenderInstanceStore& contactPresentation = m_sceneController.Scene()
                                                                                      .MutableRenderInstances();

            contactPresentation.TickContactFeedback( m_sceneController.Scene().SceneEntityCount(), PHYSICS_FIXED_DT );
            const ScenePhysicsPostStepOutput postStep = m_sceneController.Scene().StepPhysics( PHYSICS_FIXED_DT,
                                                                                               physicsWorldForces,
                                                                                               m_workerPool );

            // The physics owner publishes a bounded span; the presentation owner
            // consumes it before the next step can replace those dense-row facts.

            for ( int modelIndex : postStep.fixedContactModelIndices )
            {
                contactPresentation.NotifyFixedContact( modelIndex, 0.5f );
            }

            {
                PROFILE_SCOPED( m_profiler, "Frame/Physics/Step/PresentationCaptureComplete" );
                m_sceneController.Scene().CompletePhysicsStepPresentationCapture();
            }

            if ( manipulatorPhysics || replayCapture )
            {
                AfterPhysicsStep();
            }
        }

        PROFILE_END( m_profiler, "Frame/Physics" );
    }

    m_runtimeTools.TickRayCastTestLines( static_cast<float>( secondsPerFrame ) );
    m_runtimeTools.Laser().Update( static_cast<float>( secondsPerFrame ) );

    if ( tick.shouldUpdateLogic )
    {
        UpdateLogic( tick.simulationDt, tick.cameraDt, presentationAlpha );
    }
    else
    {

        // Why: Scene-mode, no-physics harnesses intentionally skip simulation
        // UpdateLogic, but Director is presentation state. It still needs phase
        // style/camera entry work so authored show decks behave in static scenes.
        const ReplayInputView directorReplayInput = m_replayRuntime.BuildInputView();
        DemoDirectorPredictionView directorPrediction;
        directorPrediction.revealAvailable = directorReplayInput.predictionRevealAvailable;
        directorPrediction.revealProgress = directorReplayInput.predictionRevealProgress;
        const DemoDirectorTickResult
            directorResult = DemoDirectorPlayback::Tick( m_camera, directorPrediction, m_launchOptions,
                                                         m_sceneController.State(), m_operatorUi->SceneNavigation().browser,
                                                         m_sceneController.Scene(), m_assets,
                                                         ActiveSceneCinematicConfig( m_sceneController.State(), m_config ),
                                                         m_renderDefaults.CinematicBaseline(),
                                                         static_cast<float>( secondsPerFrame ) );

        if ( directorResult.applyRevealRate )
        {
            ReplayFrameIntent intent;
            intent.applyPredictionRevealRate = true;
            intent.predictionRevealRate = directorResult.requestedRevealRate;
            (void)m_replayRuntime.ApplyFrameIntent( intent );
        }
    }

    return tick.presentationAlpha;
}


void Run::AfterPhysicsStep()
{
    m_runtimeTools.RestoreMousePickupAngularVelocity( m_sceneController.Scene(), m_inputRouter, m_interaction );
    const bool replayCaptured = m_replayRuntime.BuildInputView().captureEnabled;

    if ( replayCaptured )
    {
        CaptureReplayPostStep( m_runtimeTools, m_sceneController, *m_overlayDiagnostics, m_replayRuntime, m_profiler );
    }

#ifdef _DEBUG

    if ( replayCaptured )
    {
        RuntimeOverlayPresentationEdit presentationEdit = m_overlayDiagnostics->EditPresentation();
        SceneWorld& sceneWorld = m_sceneController.Scene();
        SceneSessionState& sceneState = m_sceneController.State();
        auto& sceneOverrides = m_operatorUi->SceneNavigation().overrides;
        const bool sceneMode = sceneState.isSceneMode;
        const int sceneEntityCount = sceneWorld.SceneEntityCount();
        const int sceneObjectCapacity = SkullbonezCore::Core::ActiveSceneObjectCapacity( m_config );
        GeneratedObjectTypeOverride& generatedObjectTypeOverride = m_launchOptions.generatedObjectTypeOverride;
        const uint32_t generatedObjectTypeOverrideBits = static_cast<uint32_t>( generatedObjectTypeOverride );

        const uint32_t cameraModeEnabledMask = RuntimeCameraModeEnabledMask( sceneMode, sceneEntityCount );

        const ReplayInputView replayInput = m_replayRuntime.BuildInputView();
        const RunCameraMode normalizedRestoreMode = NormalizeRuntimeCameraMode( replayInput.restoreCameraMode, sceneMode,
                                                                                cameraModeEnabledMask );

        const ReplaySceneTimelineResetInput timelineReset = DescribeReplaySceneTimeline( m_sceneController, sceneOverrides,
                                                                                         sceneState, sceneObjectCapacity,
                                                                                         generatedObjectTypeOverrideBits );

        // Why: ReplayRuntime owns probe sequencing and bounded failure state;
        // the application exit latch only preserves that first owned failure
        // while WM_QUIT unwinds the frame loop.
        const ReplayProbeTickResult probeResult = m_replayRuntime.TickProbes( m_sceneController, presentationEdit.State(),
                                                                              m_runtimeTools, m_config, m_assets,
                                                                              timelineReset, m_diagnosticsRuntime,
                                                                              m_inputRouter, m_interaction, m_camera,
                                                                              normalizedRestoreMode,
                                                                              m_attachedCamera.State().activeFollow );

        if ( !probeResult.status.ok )
        {
            m_applicationExit.RequestOwnedFailure( probeResult.status );
            PostQuitMessage( 0 );
            return;
        }

        if ( probeResult.resetCurrentScene )
        {
            m_sceneController.SubmitResetCurrentScene();
        }

        if ( probeResult.enterInteractive )
        {
            m_sceneController.EnterInteractiveRun();
            m_diagnosticsRuntime.Capture().DisableAutomationExit();
        }
    }
#endif
}


bool Run::TickScreenshots( const SceneFrameProceedPolicy& proceedPolicy )
{
    PROFILE_BEGIN( m_profiler, "Frame/PostDraw/Screenshots" );

    if ( !proceedPolicy.proceedAllowed )
    {
        PROFILE_END( m_profiler, "Frame/PostDraw/Screenshots" );
        return false;
    }

    const std::string* scenePath = m_sceneController.CurrentPath();
    const RuntimeCaptureResult result = m_diagnosticsRuntime.Capture()
                                            .TickScreenshots( m_sceneController.State().isSceneMode,
                                                              m_sceneController.State().isInteractiveRun,
                                                              m_sceneController.State().currentFrame,
                                                              m_timers.simulationTimer.GetTimeSinceLastStart() * 1000.0,
                                                              scenePath ? scenePath->c_str() : nullptr,
                                                              m_renderBackendView.RequireBackbufferCapture() );

    if ( result.restartFrame )
    {

        // Capture automation can synchronously replace scene-owned render
        // resources below. Close and clear graph borrows before that mutation;
        // this restart path deliberately records no Present declaration.
        m_renderer.FinalizeCaptureOnlyFrameGraph();
    }

    PROFILE_END( m_profiler, "Frame/PostDraw/Screenshots" );

    if ( !result.captureResult.ok )
    {

        // Lane R: capture readback/file IO failed after rendering, so terminate
        // automation with diagnostics instead of marking the scene complete.
        fprintf( stderr, "%s: %s\n", result.captureResult.error.owner, result.captureResult.error.message );
        fflush( stderr );
        PrintRuntimeExitReason( "Exiting because screenshot capture failed." );
        m_applicationExit.RequestOwnedFailure( result.captureResult );
        PostQuitMessage( 1 );
        return false;
    }

    if ( result.restartFrame )
    {
        PROFILE_FRAME_END( m_profiler );
    }

#ifdef _DEBUG

    if ( result.completion == RuntimeCaptureCompletion::ScreenshotAndExit )
    {
        m_diagnosticsRuntime.LogSceneFinished( m_sceneController, m_renderBackendView.renderDiagnostics,
                                               "screenshot_and_exit" );
    }
    else if ( result.completion == RuntimeCaptureCompletion::Screenshot )
    {
        m_diagnosticsRuntime.LogSceneFinished( m_sceneController, m_renderBackendView.renderDiagnostics, "screenshot" );
    }
#endif

    switch ( result.automation )
    {
    case RuntimeCaptureAutomation::Quit:

        if ( result.completion == RuntimeCaptureCompletion::ScreenshotAndExit )
        {
            PrintRuntimeExitReason( "Exiting because screenshot-and-exit capture completed." );
        }
        else if ( result.completion == RuntimeCaptureCompletion::AutoCycle )
        {
            PrintRuntimeExitReason( "Exiting because auto-cycle screenshot capture completed." );
        }

        PostQuitMessage( 0 );
        break;
    case RuntimeCaptureAutomation::AdvanceSceneOrQuit:
    {
        const SceneLoadRequest request = m_sceneController.AdvanceScene( m_diagnosticsRuntime.PerfTestActive(),
                                                                         m_sceneController.State().isInteractiveRun );

        bool advanced = false;

        if ( request.HasLoad() )
        {
            SceneLoadTransaction sceneLoad;
            sceneLoad.CaptureSubmittedState( m_camera, CaptureSceneLoadNavigationState( m_operatorUi->SceneNavigation() ),
                                             m_overlayDiagnostics->PresentationSnapshot(),
                                             m_renderBackendView.RendererName(), m_timers.simulationTimer.GetTotalTime() );

            advanced = sceneLoad
                           .Load( m_sceneController, request, m_config, m_launchOptions,
                                  m_renderDefaults.CinematicBaseline(), m_startup, m_assets, m_workerPool,
                                  m_diagnosticsRuntime, m_renderBackendView.renderFrame, m_renderBackendView.renderResources,
                                  m_renderer )
                           .ok;

            sceneLoad.ApplyRuntimeReactions( m_launchOptions, m_timers, *m_overlayDiagnostics, m_sceneController,
                                             m_inputRouter, m_interaction, m_camera, m_attachedCamera, m_runtimeTools,
                                             m_replayRuntime );

            sceneLoad.ApplyPresentationOutputs( m_window, *m_operatorUi, *m_validationHarness, m_launchOptions,
                                                m_renderBackendView.renderDevice, m_renderer.VsyncEnabled(),
                                                m_sceneController );
        }

        if ( !advanced )
        {

            if ( result.completion == RuntimeCaptureCompletion::Screenshot )
            {
                PrintRuntimeExitReason( "Exiting because scene screenshot capture completed and no next scene is queued." );
            }

            PostQuitMessage( 0 );
        }

        break;
    }
    case RuntimeCaptureAutomation::HoldInteractive:
        m_sceneController.MarkInteractiveRunComplete();
        m_diagnosticsRuntime.Capture().DisableAutomationExit();
        m_camera.StopAutoCycle();
        break;
    case RuntimeCaptureAutomation::None:
        break;
    }

    return result.restartFrame;
}


void Run::TickAutoCycle( const SceneFrameProceedPolicy& proceedPolicy )
{

    if ( !proceedPolicy.proceedAllowed )
    {
        return;
    }

    const RuntimeCaptureResult result = m_diagnosticsRuntime.Capture()
                                            .TickAutoCycle( m_sceneController.State().isSceneMode,
                                                            m_sceneController.State().isInteractiveRun,
                                                            m_sceneController.Scene().SceneEntityCount(),
                                                            m_camera.autoCycleInterval, m_camera.autoCycleAccum,
                                                            m_camera.autoCycleShotsTaken, m_camera.trackBallRow.value,
                                                            m_renderBackendView.RequireBackbufferCapture() );

    if ( !result.captureResult.ok )
    {

        // Lane R: auto-cycle captures are validation side effects; failed file
        // output exits the run rather than recording a false capture success.
        fprintf( stderr, "%s: %s\n", result.captureResult.error.owner, result.captureResult.error.message );
        fflush( stderr );
        PrintRuntimeExitReason( "Exiting because auto-cycle screenshot capture failed." );
        m_applicationExit.RequestOwnedFailure( result.captureResult );
        PostQuitMessage( 1 );
        return;
    }

    if ( result.completion != RuntimeCaptureCompletion::AutoCycle )
    {
        return;
    }

#ifdef _DEBUG
    m_diagnosticsRuntime.LogSceneFinished( m_sceneController, m_renderBackendView.renderDiagnostics, "auto_cycle" );
#endif

    if ( result.automation == RuntimeCaptureAutomation::Quit )
    {
        PostQuitMessage( 0 );
    }
    else if ( result.automation == RuntimeCaptureAutomation::HoldInteractive )
    {
        m_sceneController.MarkInteractiveRunComplete();
        m_diagnosticsRuntime.Capture().DisableAutomationExit();
        m_camera.StopAutoCycle();
    }
}


bool Run::TickSceneAdvance( const SceneFrameProceedPolicy& proceedPolicy )
{
    const SceneAutomationGateStatus automationGateStatus = m_validationHarness->SceneGates().Status();
    const SceneFrameAdvanceResult
        result = m_sceneController.AdvanceFrame( automationGateStatus, proceedPolicy.proceedAllowed,
                                                 m_diagnosticsRuntime.PerfTestActive(),
                                                 m_diagnosticsRuntime.Capture().Screenshot().isScreenshotSaved,
                                                 RunCameraModeUsesManualControls( m_camera.mode,
                                                                                  m_attachedCamera.State().activeFollow,
                                                                                  m_camera.director.grabbed ),
                                                 m_timers.simulationTimer.GetTimeSinceLastStart() );

    if ( result.reportMissingRequirements )
    {
        m_validationHarness->SceneGates().PrintMissingRequirements();
    }

#ifdef _DEBUG

    if ( result.finishReason )
    {
        m_diagnosticsRuntime.LogSceneFinished( m_sceneController, m_renderBackendView.renderDiagnostics,
                                               result.finishReason );
    }
#endif

    if ( result.holdInteractive )
    {
        m_diagnosticsRuntime.Capture().DisableAutomationExit();
        m_camera.StopAutoCycle();
    }

    bool loadSucceeded = true;

    if ( result.loadRequest.HasLoad() )
    {
        SceneLoadTransaction sceneLoad;
        sceneLoad.CaptureSubmittedState( m_camera, CaptureSceneLoadNavigationState( m_operatorUi->SceneNavigation() ),
                                         m_overlayDiagnostics->PresentationSnapshot(), m_renderBackendView.RendererName(),
                                         m_timers.simulationTimer.GetTotalTime() );

        loadSucceeded = sceneLoad
                            .Load( m_sceneController, result.loadRequest, m_config, m_launchOptions,
                                   m_renderDefaults.CinematicBaseline(), m_startup, m_assets, m_workerPool,
                                   m_diagnosticsRuntime, m_renderBackendView.renderFrame,
                                   m_renderBackendView.renderResources, m_renderer )
                            .ok;

        sceneLoad.ApplyRuntimeReactions( m_launchOptions, m_timers, *m_overlayDiagnostics, m_sceneController, m_inputRouter,
                                         m_interaction, m_camera, m_attachedCamera, m_runtimeTools, m_replayRuntime );

        sceneLoad.ApplyPresentationOutputs( m_window, *m_operatorUi, *m_validationHarness, m_launchOptions,
                                            m_renderBackendView.renderDevice, m_renderer.VsyncEnabled(), m_sceneController );
    }

    if ( loadSucceeded && result.restartSimulationTimerAfterLoad )
    {
        m_timers.simulationTimer.StartTimer();
    }

    if ( result.requestQuit || ( !loadSucceeded && result.quitIfLoadFails ) )
    {
        PostQuitMessage( 0 );
    }

    if ( !loadSucceeded && !result.quitIfLoadFails )
    {
        return false;
    }

    return result.restartFrame;
}


void Run::UpdateLogic( float simulationDt, float cameraDt, float presentationAlpha )
{
    m_camera.AdvanceAutoCycleClock( m_sceneController.State().isSceneMode, simulationDt );
    m_camera.TickControls( m_sceneController.Scene(), m_attachedCamera, m_config, m_runtimeTools.Editor().editorModeEnabled,
                           m_runtimeTools.Editor().viewportLookActive, m_sceneController.State().isSceneMode, cameraDt,
                           presentationAlpha );

    DemoDirectorPredictionView directorPrediction;
    const ReplayInputView replayInput = m_replayRuntime.BuildInputView();
    directorPrediction.revealAvailable = replayInput.predictionRevealAvailable;
    directorPrediction.revealProgress = replayInput.predictionRevealProgress;
    const DemoDirectorTickResult
        directorResult = DemoDirectorPlayback::Tick( m_camera, directorPrediction, m_launchOptions,
                                                     m_sceneController.State(), m_operatorUi->SceneNavigation().browser,
                                                     m_sceneController.Scene(), m_assets,
                                                     ActiveSceneCinematicConfig( m_sceneController.State(), m_config ),
                                                     m_renderDefaults.CinematicBaseline(), cameraDt );

    if ( directorResult.applyRevealRate )
    {
        ReplayFrameIntent intent;
        intent.applyPredictionRevealRate = true;
        intent.predictionRevealRate = directorResult.requestedRevealRate;
        (void)m_replayRuntime.ApplyFrameIntent( intent );
    }

    m_sceneController.Scene()
        .Environment()
        .ApplyFluidSurfaceAdjustment( m_inputRouter.RuntimeSnapshot().fluidSurfaceAdjustment, simulationDt );
}
