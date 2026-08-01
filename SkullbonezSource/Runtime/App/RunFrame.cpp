/*
File: SkullbonezSource/Runtime/App/RunFrame.cpp
Purpose:
  Runs one frame of input, simulation, rendering, profiling, and presentation.

Summary:
  Run sequences frame-scoped owner borrows in a fixed order, carries only small
  phase results between them, and performs scene-transition cleanup before any
  load can replace the active world.

Mental model:
  Execute is the visible phase schedule. Each private `Run` coordinator reaches
  composed members directly, delegates concrete operands, performs one
  contiguous span, and returns without retaining frame state.

Glossary:
  Simulation tick: One runtime decision about whether to advance logic, camera,
    and zero or more fixed physics steps this frame.
  Fixed-step edge: Runtime-owned code that repairs model/body topology before
    PhysicsEngine::Step and applies presentation-only refresh work after it.
  PhysicsBodyStore: Physics-owned body rows for live pose, velocity, fixed
    state, and replay identity.
  ColliderStore: Physics-owned hot collider rows plus per-kind shape payloads,
    material parameters, and broadphase radius.
  Presentation pin: Per-frame alpha override to exact current solver state for
    scheduled and auto-cycle capture automation.
  UI text facts: Value-only late-presentation snapshot shared by the operator
    surfaces without exposing mutable owners.
  Development UI apply result: One automation-owned batch outcome containing
    only a recoverable status and an optional Run-owned surface selection.
  FIFO (First In, First Out): Platform-message order retained when the bounded
    drain defers excess messages to the next frame.

Invariants:
  - Frame work updates input, simulation, capture, rendering, and diagnostics
    in a stable order used by validation and replay comparisons.
  - Capture pinning is decided before physics and camera work for that frame.
  - Delegated operations receive concrete operands and retain none.
  - A successful submitted game frame emits exactly one development profiler
    frame mark; failed or capture-only turns emit none.
  - A development surface swap hides the source before the target begins a frame.
  - Run sequences development UI automation but retains only process-wide
    surface selection and application-failure policy.
  - Physics and input owners publish complete policy/results; Run applies them
    without reconstructing or overriding their domain decisions.

Related:
  - SkullbonezSource/Runtime/App/Run.h defines the frame-coordinator calling convention.
  - SkullbonezSource/Runtime/UI/OperatorEditorFrameComposer.cpp owns operator UI projection.
  - Agentic/Reference/runtime-reference.md
  - Agentic/Reference/comment-style-guide.md
  - Agentic/Reference/engine-glossary.md
*/
#include "Run.h"
#include "../Diagnostics/RuntimeOverlayDiagnostics.h"
#include "../Automation/RuntimeValidationHarness.h"
#include "../RuntimeFrameViews.h"
#include "../UI/RuntimeViewModel.h"
#include "../Render/RenderModelFramePublisher.h"
#include "Window.h"
#include "../../Core/WorkerPool.h"
#include "InputFrame.h"
#include "../Replay/ReplayRestoreTransactions.h"
#include "../Planning/ReplayOverlayPackets.h"
#include "../Direction/DemoDirectorPlayback.h"
#include "../Scene/SceneLoadTransaction.h"

#include "../Capture/CaptureSystem.h"
#include "../Capture/RuntimeStressController.h"
#include "../Editor/EditorTools.h"
#include "../../Core/Allocation/RuntimeAllocationTracker.h"
#include "../../Core/Allocation/RuntimeReserveAllocator.h"
#include "../../Core/TracyClientOwner.h"
#if defined( SKULLBONEZ_DEVELOPMENT_TOOLS )
#include "../DevelopmentTools/ImGuiEditorOwner.h"
#endif
#include "../Scene/SceneCinematicPolicy.h"

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

struct Run::FrameSimulationPhaseResult
{
    float interpolationAlpha = 1.0f;
    bool capturePresentationPinned = false;
};

struct Run::FrameRenderPhaseResult
{
    float presentationAlpha = 1.0f;
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

    if ( !m_renderer )
    {
        SB_FATAL( "RunFrame", "Run::Execute requires a render backend." );
    }

    return secondsPerFrame;
}

void Run::BeginFrameDiagnosticsPhase()
{

    // Frame boundary: publish prior-frame GPU counters before resetting the
    // diagnostics storage that records this turn.
    Renderer().BeginProfilerFrame();
    Renderer().RenderDiagnostics().ResetFrameDrawCalls();
}

#if defined( SKULLBONEZ_AUTOMATION_DIAGNOSTICS )
Run::FrameInputPhaseResult Run::RunAutomationAndInputPhase()
{
    const ReplayAutomationView automationReplayView = m_replayRuntime.BuildAutomationView();
    const ReplayInputView automationReplayInput = automationReplayView.input;
    const InteractionAutomationFrameResult result = TickInteractionAutomationBeforeInput( m_interactionAutomation, m_window,
                                                                                          m_config, m_sceneController,
                                                                                          m_timers, m_camera, m_inputRouter,
                                                                                          m_interaction, m_runtimeTools,
                                                                                          *m_operatorUi,
                                                                                          automationReplayView,
                                                                                          Renderer().FrameGraphSnapshot() );

#if defined( SKULLBONEZ_DEVELOPMENT_TOOLS )
    const InteractionAutomationDevelopmentUiApplyResult
        developmentUiApply = m_interactionAutomation.ApplyDevelopmentUiCommands( result, m_window, m_imguiEditor );

    if ( developmentUiApply.selectSurface )
    {
        SelectDevelopmentUiSurface( developmentUiApply.surface );
    }

    if ( !developmentUiApply.status.Ok() )
    {
        m_applicationExit.RequestPhaseFailure( developmentUiApply.status );
    }
#endif

    if ( result.applyCameraMode )
    {
        m_inputRouter.ApplyCameraMode( result.cameraMode, RuntimeInputActionSource::Runtime, m_runtimeTools, m_interaction,
                                       m_attachedCamera, m_camera, m_sceneController, m_replayRuntime,
                                       m_inputRouter.RuntimeContext() );
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

        m_inputRouter.SetWorldInteractionOwner( result.worldInteractionOwner, result.worldInteractionReason, m_runtimeTools,
                                                m_interaction, m_attachedCamera, m_camera, m_sceneController,
                                                m_replayRuntime, normalizedRestoreMode );
    }

    if ( !result.status.Ok() )
    {
        m_applicationExit.RequestPhaseFailure( result.status );
    }

    if ( result.requestQuit )
    {
        PostQuitMessage( 0 );
    }

    return RunInputPhase( &result );
}
#endif

Run::FrameSimulationPhaseResult Run::RunSimulationPhase( double secondsPerFrame,
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
        m_replayRuntime.UpdatePrediction( m_sceneController.Scene().Physics(), m_sceneController.Scene().Tornado(),
                                          m_sceneController.Scene().Entities(), m_config,
                                          m_sceneController.Scene().Environment().GetPhysicsWorldForces(), m_workerPool,
                                          m_sceneController.State().isScenePhysics,
                                          m_timers.simulationTimer.GetTimeSinceLastStart(),
                                          m_timers.simulationTimer.GetTotalTime() );
    }
    m_overlayDiagnostics->UpdatePostPhysics( m_sceneController.Scene(), *m_validationHarness,
                                             m_config.bodySimulation.contactEpsilon, secondsPerFrame );

    return FrameSimulationPhaseResult { interpolationAlpha, capturePresentationPinned };
}

Run::FrameRenderPhaseResult Run::PrepareRenderPhase( bool legacyDevelopmentUiActive,
                                                     const FrameSimulationPhaseResult& simulation )
{

    // Concept: graphics stress is render/runtime churn, not UI command work.
    // This top-level phase coordinates its concrete planning, load, action, and
    // diagnostics operations without delegating the composition root.
    GraphicsStressController& graphicsStress = m_validationHarness->GraphicsStress();

    if ( PrepareGraphicsStressChurn( graphicsStress, m_window, Renderer(), Renderer().RenderDiagnostics() ) )
    {
        const GraphicsStressSceneLoadPlan stressLoad = PlanGraphicsStressSceneLoad( graphicsStress, m_sceneController,
                                                                                    *m_operatorUi );

        if ( stressLoad.request.accepted )
        {
            PrepareLookLabForSceneTransition();
            SceneLoadTransaction sceneLoad;
            sceneLoad.CaptureSubmittedState( m_camera, CaptureSceneLoadNavigationState( m_operatorUi->SceneNavigation() ),
                                             m_overlayDiagnostics->PresentationSnapshot(), Renderer().RendererName(),
                                             m_timers.simulationTimer.GetTotalTime() );

            const bool loaded = sceneLoad
                                    .Load( m_sceneController, stressLoad.request, m_config, m_launchOptions,
                                           m_renderDefaults.CinematicBaseline(), m_startup, m_assets, m_workerPool,
                                           m_diagnosticsRuntime, &Renderer().RenderFrame(), &Renderer().RenderResources(),
                                           Renderer() )
                                    .Ok();

            if ( !legacyDevelopmentUiActive )
            {
                sceneLoad.PreserveInactiveDevelopmentUi();
            }

            sceneLoad.ApplyRuntimeReactions( m_launchOptions, m_timers, *m_overlayDiagnostics, m_sceneController,
                                             m_inputRouter, m_interaction, m_camera, m_attachedCamera, m_runtimeTools,
                                             m_replayRuntime );

            sceneLoad.ApplyPresentationOutputs( m_window, *m_operatorUi, *m_validationHarness, m_launchOptions,
                                                &Renderer().RenderDevice(), Renderer().VsyncEnabled(), m_sceneController );

            if ( loaded )
            {
                graphicsStress.RecordSceneLoad();
                printf( "[graphics-stress] scene_load=%d frame=%d source=%s selected_index=%d action_index=%d\n",
                        graphicsStress.SceneLoadsRequested(), graphicsStress.FramesRun(), stressLoad.selectedSceneSource,
                        stressLoad.selectedSceneIndex, stressLoad.request.index );
            }
            else
            {
                printf( "[graphics-stress] scene_load_skipped frame=%d source=%s selected_index=%d\n",
                        graphicsStress.FramesRun(), stressLoad.selectedSceneSource, stressLoad.selectedSceneIndex );
            }

            fflush( stdout );
        }
        else if ( stressLoad.scheduled )
        {
            printf( "[graphics-stress] scene_load_skipped frame=%d source=%s selected_index=%d\n",
                    graphicsStress.FramesRun(), stressLoad.selectedSceneSource, stressLoad.selectedSceneIndex );

            fflush( stdout );
        }

        if ( legacyDevelopmentUiActive )
        {
            m_operatorUi->SetVisible( true, m_timers.simulationTimer.GetTotalTime() );
            m_operatorUi->SetMinimized( false, m_timers.simulationTimer.GetTotalTime() );
        }

        m_sceneController.EnterInteractiveRun();
        const int actionCount = graphicsStress.InDescriptorChurnQuietWindow() ? 0 : graphicsStress.ActionCount();

        for ( int index = 0; index < actionCount; ++index )
        {
            const int action = graphicsStress.NextAction();

            if ( action <= 14 )
            {
                ApplyGraphicsStressPresentationAction( action, graphicsStress, m_assets, m_launchOptions, m_config,
                                                       *m_overlayDiagnostics, m_sceneController, m_timers, *m_operatorUi,
                                                       m_renderDefaults.CinematicBaseline(), Renderer() );
            }
            else
            {
                ApplyGraphicsStressRuntimeAction( action, graphicsStress, m_launchOptions, *m_overlayDiagnostics,
                                                  m_sceneController, m_camera, *m_operatorUi, m_simulation, m_runtimeTools,
                                                  m_replayRuntime );
            }
        }

        FinishGraphicsStressFrame( graphicsStress, m_diagnosticsRuntime, m_timers, m_sceneController, m_replayRuntime,
                                   Renderer().RenderDiagnostics() );
    }

    const float presentationAlpha = ResolvePresentationAlpha( m_config, simulation.capturePresentationPinned,
                                                              simulation.interpolationAlpha );

    if ( Renderer().PipelineSyncEnabled() )
    {
        PROFILE_BEGIN( m_profiler, "Frame/PipelineSync" );
        SkullbonezCore::Core::SbResult finishResult = SkullbonezCore::Core::SbResult::Success();
        {
            CoreAllocation::RuntimeAllocationScope allocationScope( CoreAllocation::RuntimeAllocationPhase::Render );
            finishResult = Renderer().RenderFrame().FinishAndReopen( Renderer().RenderDiagnostics() );
        }
        PROFILE_END( m_profiler, "Frame/PipelineSync" );

        if ( !finishResult.Ok() )
        {
            m_timers.frameTimer.StopTimer();
            PROFILE_FRAME_END( m_profiler );
            m_applicationExit.RequestPhaseFailure( finishResult );
            return FrameRenderPhaseResult { presentationAlpha };
        }
    }

    return FrameRenderPhaseResult { presentationAlpha };
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
        DRAW_CALL_TRACE_SCOPE( Renderer().RenderDiagnostics(), "Frame/Render" );

        // Invariant: graph ownership begins before Render can take its text-only
        // path. World, UI, capture, and Present close the same graph exactly once.
        Renderer().BeginFrameGraph();
        Render( renderModels, presentationAlpha );
    }
    PROFILE_END( m_profiler, "Frame/Render" );
}

void Run::RunPostDrawDiagnosticsPhase( bool legacyDevelopmentUiActive )
{

    // Invariant: the F11 request was formed during input after its candidate was
    // applied. Draining after world/UI draw and before Present captures that
    // exact frame without lending renderer authority to LookLabController.
    CompleteLookLabPostRenderCaptures();

    PROFILE_BEGIN( m_profiler, "Frame/PostDraw/LiveStyleCapture" );
    {
        CoreAllocation::RuntimeAllocationScope allocationScope( CoreAllocation::RuntimeAllocationPhase::Capture );
        m_validationHarness->SavePendingLiveStyleCapture( m_diagnosticsRuntime.Capture(), BackbufferCapture() );
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
        automationAfterRender = TickInteractionAutomationAfterRender( m_interactionAutomation, m_runtimeTools, m_interaction,
                                                                      m_inputRouter, m_camera, *m_operatorUi,
                                                                      m_sceneController,
                                                                      m_replayRuntime.BuildAutomationView(),
                                                                      automationDevelopmentUiView,
                                                                      Renderer().FrameGraphSnapshot(),
                                                                      m_diagnosticsRuntime.Capture(), BackbufferCapture() );

    if ( !automationAfterRender.status.Ok() )
    {
        m_applicationExit.RequestPhaseFailure( automationAfterRender.status );
    }

    if ( automationAfterRender.requestQuit )
    {
        PostQuitMessage( 0 );
    }

    PROFILE_END( m_profiler, "Frame/PostDraw/InteractionAutomation" );
#else
    (void)legacyDevelopmentUiActive;
#endif
}

void Run::CompleteLookLabPostRenderCaptures()
{
    CaptureController& capture = m_diagnosticsRuntime.Capture();

    if ( capture.PendingPostRenderCount() == 0 )
    {
        return;
    }

    PROFILE_BEGIN( m_profiler, "Frame/PostDraw/LookLabCapture" );
    {
        CoreAllocation::RuntimeAllocationScope allocationScope( CoreAllocation::RuntimeAllocationPhase::Capture );
        PostRenderCaptureBatchResult batch = capture.DrainPostRenderRequests( BackbufferCapture() );

        for ( std::size_t index = 0; index < batch.count; ++index )
        {
            const PostRenderCaptureResult& captured = batch.results[index];

            if ( captured.request.owner != PostRenderCaptureOwner::LookLab )
            {
                continue;
            }

            const Core::SbResult completion = m_lookLab.CompleteSaveCapture( m_resultDiagnostics, captured.request.token,
                                                                             captured.status );

            if ( !completion.Ok() )
            {
                std::fprintf( stderr, "%s: %s\n", completion.ErrorOwner(), completion.ErrorMessage() );
            }
        }
    }
    PROFILE_END( m_profiler, "Frame/PostDraw/LookLabCapture" );
}

void Run::FinishFrameWorkPhase( const SceneFrameProceedPolicy& proceedPolicy )
{
    PROFILE_BEGIN( m_profiler, "Frame/PostDraw/AutoCycle" );
    TickAutoCycle( proceedPolicy );
    PROFILE_END( m_profiler, "Frame/PostDraw/AutoCycle" );
    m_timers.workTimer.StopTimer();
    m_timers.cpuFrameWorkMs = static_cast<float>( std::clamp( m_timers.workTimer.GetElapsedTime(), 0.0, 0.25 ) * 1000.0 );
}

void Run::PresentFramePhase()
{
    PROFILE_BEGIN( m_profiler, "Frame/VsyncWait" );
    SkullbonezCore::Core::SbResult presentResult = SkullbonezCore::Core::SbResult::Success();
    {
        CoreAllocation::RuntimeAllocationScope allocationScope( CoreAllocation::RuntimeAllocationPhase::Render );

        // Invariant: the production graph has one declaration-only Present edge;
        // finalize it before the swap-chain owner submits this frame.
        Renderer().FinalizeFrameGraph();
        presentResult = Renderer().RenderFrame().Present( Renderer().RenderDiagnostics() );
    }
    PROFILE_END( m_profiler, "Frame/VsyncWait" );

    if ( !presentResult.Ok() )
    {
        m_timers.frameTimer.StopTimer();
        PROFILE_FRAME_END( m_profiler );
        m_applicationExit.RequestPhaseFailure( presentResult );
        return;
    }

    // Invariant: Tracy counts submitted game frames, not attempted render turns,
    // capture-only continues, or failed Presents.
    SKORE_TRACY_MARK_SUBMITTED_FRAME();
    m_timers.frameTimer.StopTimer();
    PROFILE_FRAME_END( m_profiler );
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

        BeginFrameDiagnosticsPhase();
        PROFILE_BEGIN( m_profiler, "Frame/Input" );
#if defined( SKULLBONEZ_AUTOMATION_DIAGNOSTICS )
        const FrameInputPhaseResult input = RunAutomationAndInputPhase();
#else
        const FrameInputPhaseResult input = RunInputPhase( nullptr );
#endif
        PROFILE_END( m_profiler, "Frame/Input" );

        if ( m_applicationExit.ExitRequested() )
        {
            return m_applicationExit.Resolve( 0 );
        }

        const auto simulation = RunSimulationPhase( secondsPerFrame, input.proceedPolicy );
        const auto render = PrepareRenderPhase( input.legacyDevelopmentUiActive, simulation );

        // Invariant: every frame phase below has a status-free return. Failure
        // is observable only through the ApplicationExitState latch.

        if ( m_applicationExit.ExitRequested() )
        {
            return m_applicationExit.Resolve( 0 );
        }

        RuntimeRenderModelFrameView models = PublishRenderModelsPhase();
        RenderWorldPhase( models, render.presentationAlpha );
        const auto facts = FramePresentationFacts { render.presentationAlpha, simulation.capturePresentationPinned,
                                                    secondsPerFrame, input.legacyDevelopmentUiActive };

        RenderOperatorUiPhase( models, facts );

        if ( m_applicationExit.ExitRequested() )
        {
            return m_applicationExit.Resolve( 0 );
        }

        RunPostDrawDiagnosticsPhase( input.legacyDevelopmentUiActive );

        if ( m_applicationExit.ExitRequested() )
        {
            return m_applicationExit.Resolve( 0 );
        }

        {
            CoreAllocation::RuntimeAllocationScope allocationScope( CoreAllocation::RuntimeAllocationPhase::Capture );

            if ( TickScreenshots( input.proceedPolicy ) )
            {
                continue;
            }
        }
        FinishFrameWorkPhase( input.proceedPolicy );
        PresentFramePhase();

        if ( m_applicationExit.ExitRequested() )
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
    const OverlayDebugState overlayPresentation = m_overlayDiagnostics->PresentationSnapshot();

    // Why: the saturated Replay count remains live every step, but payload rows
    // are observational work needed only by capture or pipeline presentation.
    m_sceneController.Scene().Physics().SetPipelineTraceFullRecordConsumerActive( replayCapture || ( overlayPresentation.physicsDebugFlags & PHYSICS_DEBUG_PIPELINE ) != 0u );
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
            directorResult = DemoDirectorPlayback::Tick( m_resultDiagnostics, m_camera, directorPrediction, m_launchOptions,
                                                         m_sceneController, m_operatorUi->SceneNavigation().browser,
                                                         m_assets,
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

        if ( !probeResult.status.Ok() )
        {
            m_applicationExit.RequestPhaseFailure( probeResult.status );
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
                                                              BackbufferCapture() );

    if ( result.restartFrame )
    {

        // Capture automation can synchronously replace scene-owned render
        // resources below. Close and clear graph borrows before that mutation;
        // this restart path deliberately records no Present declaration.
        Renderer().FinalizeCaptureOnlyFrameGraph();
    }

    PROFILE_END( m_profiler, "Frame/PostDraw/Screenshots" );

    if ( !result.captureResult.Ok() )
    {

        // Lane R: capture readback/file IO failed after rendering, so terminate
        // automation with diagnostics instead of marking the scene complete.
        fprintf( stderr, "%s: %s\n", result.captureResult.ErrorOwner(), result.captureResult.ErrorMessage() );
        fflush( stderr );
        PrintRuntimeExitReason( "Exiting because screenshot capture failed." );
        m_applicationExit.RequestPhaseFailure( result.captureResult );
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
        m_diagnosticsRuntime.LogSceneFinished( m_sceneController, &Renderer().RenderDiagnostics(), "screenshot_and_exit" );
    }
    else if ( result.completion == RuntimeCaptureCompletion::Screenshot )
    {
        m_diagnosticsRuntime.LogSceneFinished( m_sceneController, &Renderer().RenderDiagnostics(), "screenshot" );
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
            PrepareLookLabForSceneTransition();
            SceneLoadTransaction sceneLoad;
            sceneLoad.CaptureSubmittedState( m_camera, CaptureSceneLoadNavigationState( m_operatorUi->SceneNavigation() ),
                                             m_overlayDiagnostics->PresentationSnapshot(), Renderer().RendererName(),
                                             m_timers.simulationTimer.GetTotalTime() );

            advanced = sceneLoad
                           .Load( m_sceneController, request, m_config, m_launchOptions,
                                  m_renderDefaults.CinematicBaseline(), m_startup, m_assets, m_workerPool,
                                  m_diagnosticsRuntime, &Renderer().RenderFrame(), &Renderer().RenderResources(),
                                  Renderer() )
                           .Ok();

            sceneLoad.ApplyRuntimeReactions( m_launchOptions, m_timers, *m_overlayDiagnostics, m_sceneController,
                                             m_inputRouter, m_interaction, m_camera, m_attachedCamera, m_runtimeTools,
                                             m_replayRuntime );

            sceneLoad.ApplyPresentationOutputs( m_window, *m_operatorUi, *m_validationHarness, m_launchOptions,
                                                &Renderer().RenderDevice(), Renderer().VsyncEnabled(), m_sceneController );
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
                                                            BackbufferCapture() );

    if ( !result.captureResult.Ok() )
    {

        // Lane R: auto-cycle captures are validation side effects; failed file
        // output exits the run rather than recording a false capture success.
        fprintf( stderr, "%s: %s\n", result.captureResult.ErrorOwner(), result.captureResult.ErrorMessage() );
        fflush( stderr );
        PrintRuntimeExitReason( "Exiting because auto-cycle screenshot capture failed." );
        m_applicationExit.RequestPhaseFailure( result.captureResult );
        PostQuitMessage( 1 );
        return;
    }

    if ( result.completion != RuntimeCaptureCompletion::AutoCycle )
    {
        return;
    }

#ifdef _DEBUG
    m_diagnosticsRuntime.LogSceneFinished( m_sceneController, &Renderer().RenderDiagnostics(), "auto_cycle" );
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
        m_diagnosticsRuntime.LogSceneFinished( m_sceneController, &Renderer().RenderDiagnostics(), result.finishReason );
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
        PrepareLookLabForSceneTransition();
        SceneLoadTransaction sceneLoad;
        sceneLoad.CaptureSubmittedState( m_camera, CaptureSceneLoadNavigationState( m_operatorUi->SceneNavigation() ),
                                         m_overlayDiagnostics->PresentationSnapshot(), Renderer().RendererName(),
                                         m_timers.simulationTimer.GetTotalTime() );

        loadSucceeded = sceneLoad
                            .Load( m_sceneController, result.loadRequest, m_config, m_launchOptions,
                                   m_renderDefaults.CinematicBaseline(), m_startup, m_assets, m_workerPool,
                                   m_diagnosticsRuntime, &Renderer().RenderFrame(), &Renderer().RenderResources(),
                                   Renderer() )
                            .Ok();

        sceneLoad.ApplyRuntimeReactions( m_launchOptions, m_timers, *m_overlayDiagnostics, m_sceneController, m_inputRouter,
                                         m_interaction, m_camera, m_attachedCamera, m_runtimeTools, m_replayRuntime );

        sceneLoad.ApplyPresentationOutputs( m_window, *m_operatorUi, *m_validationHarness, m_launchOptions,
                                            &Renderer().RenderDevice(), Renderer().VsyncEnabled(), m_sceneController );
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
        directorResult = DemoDirectorPlayback::Tick( m_resultDiagnostics, m_camera, directorPrediction, m_launchOptions,
                                                     m_sceneController, m_operatorUi->SceneNavigation().browser, m_assets,
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
