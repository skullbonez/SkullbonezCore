/*
File: SkullbonezSource/Runtime/RunFrame.cpp
Purpose:
  Runs one frame of input, simulation, rendering, profiling, and presentation.

Summary:
  RunFrame.cpp runs one frame of input, simulation, rendering, profiling, and
  presentation. As an implementation unit, keep edits anchored on local owner
  boundaries and call direction and on the glossary/invariants below.

Glossary:
  Simulation tick: One runtime decision about whether to advance logic, camera,
    and zero or more fixed physics steps this frame.
  Fixed-step edge: Runtime-owned code that repairs model/body topology before
    PhysicsEngine::Step and applies presentation-only refresh work after it.
  PhysicsBodyStore: Physics-owned body rows for live pose, velocity, fixed
    state, and replay identity.
  ColliderStore: Physics-owned collider rows for exact shape variants, material
    parameters, and broadphase radius.
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
  - RuntimeFrameViews.h defines the frame-helper calling convention.
  - Runtime/UI/OperatorEditorFrameComposer.cpp owns operator UI projection.
  - Agentic/Reference/runtime-reference.md
  - Agentic/Reference/comment-style-guide.md
*/
#include "Run.h"
#include "RuntimeOverlayDiagnostics.h"
#include "RuntimeValidationHarness.h"
#include "RuntimeFrameViews.h"
#include "RuntimeViewModel.h"
#include "UI/OperatorEditorFrameComposer.h"
#include "Window.h"
#include "../Core/WorkerPool.h"
#include "InputFrame.h"
#include "Replay/ReplayRestoreTransactions.h"
#include "Replay/ReplayOverlayPackets.h"
#include "DemoDirectorPlayback.h"
#include "Scene/SceneRuntimeLoad.h"

#include "CaptureSystem.h"
#include "Editor/EditorTools.h"
#include "../Core/Allocation/RuntimeAllocationTracker.h"
#include "../Core/Allocation/RuntimeReserveAllocator.h"
#include "../Core/TracyClientOwner.h"
#if defined( SKULLBONEZ_DEVELOPMENT_TOOLS )
#include "DevelopmentTools/ImGuiEditorOwner.h"
#endif
#include "OperatorCommandApplier.h"
#include "Scene/SceneRuntimeStyle.h"

#include "../Core/FatalError.h"
#include "../Core/Log.h"
#include "../Core/Profiler.h"
#include "../Physics/ColliderStore.h"
#include "../Physics/PhysicsEngine.h"
#include "../Physics/PhysicsApi.h"
#include "../Physics/PhysicsDiagnosticsSink.h"
#include "../Physics/PhysicsTimestep.h"
#include "../Rendering/RenderInstanceStore.h"
#include "../Rendering/DX12/Dx12Diagnostics.h"
#include "../UI/UI.h"
#include "../UI/UITabEditor.h"

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

float ResolvePresentationAlpha( const SkullbonezCore::Core::EngineConfig& config,
                                bool capturePresentationPinned,
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
void CaptureReplayPostStep( RuntimeTools& runtimeTools,
                            SkullbonezCore::Runtime::SceneController& sceneController,
                            RunTimerState& timers,
                            const RuntimeOverlayDiagnostics& overlays,
                            ReplayRuntime& replayRuntime,
                            SkullbonezCore::Core::Profiler* profiler )
{
    const RunSceneState& scene = sceneController.State();
    const RunDebugState debug = overlays.PresentationSnapshot();
    SkullbonezCore::Environment::CameraCollection& cameras = sceneController.Scene().Cameras();
    SkullbonezCore::Environment::WorldEnvironment& world = sceneController.Scene().Environment();
    PhysicsEngine& physics = sceneController.Scene().Physics();
    const SceneEntityStore& entities = sceneController.Scene().Entities();
    CoreAllocation::RuntimeAllocationScope allocationScope( CoreAllocation::RuntimeAllocationPhase::Replay );
    PROFILE_SCOPED( profiler, "Frame/Physics/Step/ReplayCapture" );
    ReplayCaptureInput input;
    input.sceneFrame = scene.currentFrame;
    input.simulationSeconds = timers.simulationTimer.GetTimeSinceLastStart();
    input.physicsDt = PHYSICS_FIXED_DT;
    input.fixedStep = scene.isFixedStep;
    input.scenePhysicsEnabled = scene.isScenePhysics;
    input.sceneTextEnabled = scene.isSceneText;
    input.waterHidden = debug.isWaterHidden;
    input.terrainHidden = debug.isTerrainHidden;
    input.cameras = &cameras;
    input.world = &world;
    input.physics = &physics;
    input.tornadoGameplay = &sceneController.Scene().Tornado();
    input.entities = &entities;
    input.bodyStore = &sceneController.Scene().BodyStore();
    input.colliderStore = &sceneController.Scene().Colliders();
    replayRuntime.CaptureFrame( input, runtimeTools );
}

} // namespace

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
    MSG msg;
    int messageExitCode = 0;
    constexpr int kMaxMessagesPerFrame = 256;

    for ( ;; )
    {
        bool quitRequested = false;
        int messagesDrained = 0;
        // Hazard: a device or window can flood the thread queue faster than
        // frame work consumes it. The cap keeps rendering responsive by
        // deferring excess messages to the next frame; reaching it is not an
        // error and preserves FIFO order in the Win32 queue.
        while ( messagesDrained < kMaxMessagesPerFrame && PeekMessage( &msg, nullptr, 0, 0, PM_REMOVE ) )
        {
            ++messagesDrained;
            if ( msg.message == WM_QUIT )
            {
                m_validationHarness->PrintGraphicsStressExitSummary( m_sceneController.State().currentFrame );
                // Concept: WM_QUIT is the platform's stop notification, not the
                // process result by itself. Preserve a Run-owned failure when
                // one already exists; otherwise translate the posted integer.
                m_applicationExit.RequestNormalExit();
                messageExitCode = static_cast<int>( msg.wParam );
                quitRequested = true;
                break;
            }
            TranslateMessage( &msg );
            DispatchMessage( &msg );
        }
        if ( quitRequested )
        {
            break;
        }

        {
            CoreAllocation::RuntimeAllocationScope frameAllocationScope(
                CoreAllocation::RuntimeAllocationPhase::SteadyGameplay );
            double secondsPerFrame = m_timers.frameTimer.GetElapsedTime();
            secondsPerFrame = std::clamp( secondsPerFrame, 0.0, 0.05 );

            m_timers.frameTimer.StartTimer();
            PROFILE_FRAME_BEGIN( m_profiler );
            m_timers.workTimer.StartTimer();
            // Lifetime: borrow the startup-owned renderer once for this frame
            // turn. Narrow facets keep reset, GPU-drain, UI accounting, and
            // present from each reaching through the process-global service.
            if ( !m_renderBackendView.renderDevice || !m_renderBackendView.renderDiagnostics ||
                 !m_renderBackendView.renderResources || !m_renderBackendView.renderFrame ||
                 !m_renderBackendView.renderGraph || !m_renderBackendView.renderTextures ||
                 !m_renderBackendView.renderGeometry )
            {
                SB_FATAL( "RunFrame", "Run::Execute requires a render backend." );
            }
            SkullbonezCore::Rendering::Dx12Diagnostics& frameRenderDiagnostics = *m_renderBackendView.renderDiagnostics;
            SkullbonezCore::Rendering::Dx12ResourceBuilder& frameRenderResources = *m_renderBackendView.renderResources;
            SkullbonezCore::Rendering::Dx12FrameOwner& frameRenderOwner = *m_renderBackendView.renderFrame;
            SkullbonezCore::Rendering::Dx12TextureOwner& frameRenderTextures = *m_renderBackendView.renderTextures;
            SkullbonezCore::Rendering::Dx12GeometryOwner& frameRenderGeometry = *m_renderBackendView.renderGeometry;
            const SkullbonezCore::UI::UIRenderContext uiRender = { &m_assets,
                                                                   &frameRenderResources,
                                                                   &frameRenderTextures,
                                                                   &frameRenderGeometry,
                                                                   &frameRenderDiagnostics };
            // Lifetime: the frame views are stack-only borrow maps for this
            // turn. They are never assigned to Run or passed to retained work.
            RuntimeFrameHostView frameHost{ m_applicationExit,
                                            m_diagnosticsRuntime,
                                            m_assets,
                                            m_workerPool,
                                            m_window,
                                            m_profiler };
            RuntimeFrameInteractionView frameInteraction{ m_inputRouter,
                                                          m_interaction,
                                                          m_attachedCamera,
                                                          *m_operatorUi,
                                                          m_runtimeTools,
                                                          m_camera };
            RuntimeFrameSceneView frameScene{ m_config,
                                              m_launchOptions,
                                              m_startup,
                                              m_timers,
                                              *m_overlayDiagnostics,
                                              m_simulation,
                                              m_sceneController };
            RuntimeFramePresentationView framePresentation{ m_renderDefaults,
                                                            *m_validationHarness,
                                                            m_renderBackendView,
                                                            m_renderer };
            // Frame boundary: read completed renderer timestamps and publish
            // prior-frame render counters before resetting per-frame diagnostics.
            m_renderer.BeginProfilerFrame();
            frameRenderDiagnostics.ResetFrameDrawCalls();

            PROFILE_BEGIN( m_profiler, "Frame/Input" );
#if defined( SKULLBONEZ_AUTOMATION_DIAGNOSTICS )
            const ReplayAutomationView automationReplayView = m_replayRuntime.BuildAutomationView();
            const ReplayInputView automationReplayInput = automationReplayView.input;
            const InteractionAutomationFrameResult automationBeforeInput =
                TickInteractionAutomationBeforeInput( m_interactionAutomation,
                                                      m_window,
                                                      frameInteraction,
                                                      frameScene,
                                                      automationReplayView );
#if defined( SKULLBONEZ_DEVELOPMENT_TOOLS )
            const InteractionAutomationDevelopmentUiApplyResult developmentUiApply =
                m_interactionAutomation.ApplyDevelopmentUiCommands( automationBeforeInput, m_window, m_imguiEditor );
            if ( developmentUiApply.selectSurface )
            {
                SelectDevelopmentUiSurface( developmentUiApply.surface );
            }
            if ( !developmentUiApply.status.ok )
            {
                m_applicationExit.RequestOwnedFailure( developmentUiApply.status );
            }
#endif
            if ( automationBeforeInput.applyCameraMode )
            {
                m_inputRouter.ApplyCameraMode( automationBeforeInput.cameraMode,
                                               RuntimeInputActionSource::Runtime,
                                               frameInteraction,
                                               m_sceneController,
                                               m_replayRuntime,
                                               m_inputRouter.RuntimeContext() );
            }
            // Automation publishes replay mutations as a value packet. Apply
            // it once at the frame composition boundary before normal input
            // observes the resulting replay state.
            (void)m_replayRuntime.ApplyFrameIntent( automationBeforeInput.replayIntent );
            if ( automationBeforeInput.setWorldInteractionOwner )
            {
                m_inputRouter.SetWorldInteractionOwner(
                    automationBeforeInput.worldInteractionOwner,
                    automationBeforeInput.worldInteractionReason,
                    frameInteraction,
                    m_sceneController,
                    m_replayRuntime,
                    NormalizeRuntimeCameraMode(
                        automationReplayInput.restoreCameraMode,
                        m_sceneController.State().isSceneMode,
                        RuntimeCameraModeEnabledMask( m_sceneController.State().isSceneMode,
                                                      m_sceneController.Scene().SceneEntityCount() ) ) );
            }
            if ( !automationBeforeInput.status.ok )
            {
                m_applicationExit.RequestOwnedFailure( automationBeforeInput.status );
            }
            if ( automationBeforeInput.requestQuit )
            {
                PostQuitMessage( 0 );
            }
#endif
            UiInputCaptureIntent developmentUiCapture;
            SkullbonezCore::UI::OperatorEditorCommandQueues developmentEditorCommands;
            bool legacyDevelopmentUiActive = true;
#if defined( SKULLBONEZ_DEVELOPMENT_TOOLS )
            // Concept: native messages were already offered to ImGui while the
            // queue drained. The previous completed editor frame now supplies
            // class-specific capture intent to the single engine input sample.
            const DevelopmentTools::ImGuiEditorInputFrameState imguiInput = m_imguiEditor.ConsumeInputFrameState();
            developmentUiCapture = UiInputCaptureIntent{ imguiInput.capture.mouse,
                                                         imguiInput.capture.keyboard,
                                                         imguiInput.capture.text,
                                                         imguiInput.nativePointerStateTouched };
            developmentUiCapture.gameViewportMappingActive = imguiInput.gameViewport.valid;
            developmentUiCapture.gameViewportMinX = imguiInput.gameViewport.imageMinX;
            developmentUiCapture.gameViewportMinY = imguiInput.gameViewport.imageMinY;
            developmentUiCapture.gameViewportWidth = imguiInput.gameViewport.imageWidth;
            developmentUiCapture.gameViewportHeight = imguiInput.gameViewport.imageHeight;
            developmentUiCapture.gameViewportDpiScale = imguiInput.gameViewport.dpiScale;
            developmentUiCapture.gameViewportSourceWidth = imguiInput.gameViewport.sourceWidth;
            developmentUiCapture.gameViewportSourceHeight = imguiInput.gameViewport.sourceHeight;
            developmentEditorCommands = m_imguiEditor.ConsumeOperatorEditorCommands();
#if defined( SKULLBONEZ_AUTOMATION_DIAGNOSTICS )
            if ( automationBeforeInput.hasOperatorEditorReplayCommand )
            {
                const SkullbonezCore::Core::SbResult submitStatus =
                    UI::SubmitOperatorEditorCommand( developmentEditorCommands.replay,
                                                     automationBeforeInput.operatorEditorReplayCommand );
                if ( !submitStatus.ok )
                {
                    m_applicationExit.RequestOwnedFailure( submitStatus );
                }
            }
#endif
            legacyDevelopmentUiActive = m_imguiEditor.SelectedSurface() == DevelopmentUiMode::Legacy;
#endif
            [[maybe_unused]] const InputFrameExecutionResult inputFrameResult =
                ProcessInputFrame( frameHost,
                                   frameInteraction,
                                   frameScene,
                                   framePresentation,
                                   m_replayRuntime,
                                   developmentUiCapture,
                                   developmentEditorCommands,
                                   legacyDevelopmentUiActive );
#if defined( SKULLBONEZ_DEVELOPMENT_TOOLS )
            if ( m_launchOptions.developmentUiModeExplicit || m_imguiEditor.HasActivatedSurfaceSelection() )
            {
                // Invariant: a scene load may apply scene-authored Legacy window
                // defaults during the input checkpoint. Reassert an explicit
                // process selection before either surface can begin its frame.
                SelectDevelopmentUiSurface( m_imguiEditor.SelectedSurface() );
            }
            if ( inputFrameResult.requestDevelopmentUiSurfaceSwap )
            {
                // Plain 0 retains the Legacy minimize behavior. Ctrl+0 is the
                // explicit surface chord; selection hides Legacy before ImGui
                // begins a frame, so focus ownership never overlaps.
                SelectDevelopmentUiSurface( DevelopmentUiMode::ImGui );
            }
            // Invariant: ProcessInputFrame may consume Ctrl+0 after the first
            // input snapshot. Resample the selected presentation only after
            // every pre-render swap so this frame cannot draw Legacy replay
            // underneath a newly active ImGui frame.
            legacyDevelopmentUiActive = m_imguiEditor.SelectedSurface() == DevelopmentUiMode::Legacy;
#endif
            // Concept: scene progression owns the cross-scene pause rule. One
            // typed policy is shared by physics, capture, auto-cycle, and scene
            // completion so no late helper samples or reconstructs input policy.
            const SceneFrameProceedPolicy sceneProceedPolicy =
                m_sceneController.BuildFrameProceedPolicy( m_inputRouter.RuntimeSnapshot().frameInput.stepHeld );
            m_validationHarness->TickLiveStyle(
                SceneRuntimeStyleContext{ m_launchOptions,
                                          m_sceneController.State(),
                                          m_operatorUi->SceneNavigation().browser,
                                          m_sceneController.Scene(),
                                          m_assets,
                                          ActiveSceneCinematicConfig( m_sceneController.State(), m_config ),
                                          m_renderDefaults.CinematicBaseline() } );
            PROFILE_END( m_profiler, "Frame/Input" );

            m_sceneController.Scene().BeginCollisionVisualFrame();
            const std::string* captureScenePath = m_sceneController.CurrentPath();
            const RuntimeCaptureSceneContext captureContext{ m_sceneController.State().isSceneMode,
                                                             m_sceneController.State().isInteractiveRun,
                                                             m_sceneController.State().currentFrame,
                                                             m_timers.simulationTimer.GetTimeSinceLastStart() * 1000.0,
                                                             captureScenePath ? captureScenePath->c_str() : nullptr };
            // Invariant: decide capture determinism before physics/camera update.
            // The frame rendered for a scheduled screenshot must use exact
            // current solver poses even when live presentation interpolation is on.
            const bool capturePresentationPinned =
                m_diagnosticsRuntime.Capture().RequiresDeterministicPresentation( captureContext ) ||
                ( captureContext.isSceneMode && m_camera.autoCycleInterval > 0.0f ) ||
                m_validationHarness->HasPendingLiveStyleCapture()
#if defined( SKULLBONEZ_AUTOMATION_DIAGNOSTICS )
                || InteractionAutomationWillCaptureAfterRender( m_interactionAutomation,
                                                                m_sceneController.State().currentFrame )
#endif
                ;
            float simulationPresentationAlpha = 1.0f;
            {
                CoreAllocation::RuntimeAllocationScope allocationScope(
                    CoreAllocation::RuntimeAllocationPhase::Physics );
                simulationPresentationAlpha =
                    TickPhysics( secondsPerFrame, capturePresentationPinned, sceneProceedPolicy );
            }

            {
                // Invariant: prediction scheduling completes before overlay
                // construction. Render consumes only the published future and
                // cannot decide whether the private engine advances.
                CoreAllocation::RuntimeAllocationScope allocationScope(
                    CoreAllocation::RuntimeAllocationPhase::Replay );
                m_replayRuntime.UpdatePrediction( m_sceneController.Scene().Physics(),
                                                  m_sceneController.Scene().Tornado(),
                                                  m_sceneController.Scene().Entities(),
                                                  m_config,
                                                  m_sceneController.Scene().Environment().GetPhysicsWorldForces(),
                                                  m_workerPool,
                                                  m_sceneController.State().isScenePhysics,
                                                  m_timers.simulationTimer.GetTimeSinceLastStart(),
                                                  m_timers.simulationTimer.GetTotalTime() );
            }

            m_overlayDiagnostics->UpdatePostPhysics( m_sceneController.Scene(),
                                                     *m_validationHarness,
                                                     m_config.bodySimulation.contactEpsilon,
                                                     secondsPerFrame );

            // Concept: graphics stress is render/runtime churn, not UI command
            // processing. Tick it once per rendered frame so headless and
            // overnight launches keep mutating DX12 state even when the UI
            // command panel is not producing control messages.
            m_validationHarness->ExecuteGraphicsStressFrame( frameHost,
                                                             frameInteraction,
                                                             frameScene,
                                                             framePresentation,
                                                             m_replayRuntime,
                                                             frameRenderDiagnostics,
                                                             legacyDevelopmentUiActive );
            const float presentationAlpha =
                ResolvePresentationAlpha( m_config, capturePresentationPinned, simulationPresentationAlpha );

            if ( m_renderer.PipelineSyncEnabled() )
            {
                PROFILE_BEGIN( m_profiler, "Frame/PipelineSync" );
                SkullbonezCore::Core::SbResult finishResult = SkullbonezCore::Core::SbResult::Success();
                {
                    CoreAllocation::RuntimeAllocationScope allocationScope(
                        CoreAllocation::RuntimeAllocationPhase::Render );
                    finishResult = frameRenderOwner.FinishAndReopen( frameRenderDiagnostics );
                }
                PROFILE_END( m_profiler, "Frame/PipelineSync" );
                if ( !finishResult.ok )
                {
                    m_timers.frameTimer.StopTimer();
                    PROFILE_FRAME_END( m_profiler );
                    m_applicationExit.RequestOwnedFailure( finishResult );
                    return m_applicationExit.Resolve( 0 );
                }
            }

            RuntimeRenderModelFrameView renderModels =
                m_renderer.BuildModelFrameView( m_sceneController.Scene(), m_workerPool, m_config );

            PROFILE_BEGIN( m_profiler, "Frame/Render" );
            {
                CoreAllocation::RuntimeAllocationScope allocationScope(
                    CoreAllocation::RuntimeAllocationPhase::Render );
                DRAW_CALL_TRACE_SCOPE( frameRenderDiagnostics, "Frame/Render" );
                if ( !m_renderBackendView.renderGraph )
                {
                    SB_FATAL( "RunFrame", "A rendered frame requires the startup-bound render command context." );
                }
                // Invariant: graph ownership begins before Render can choose
                // the text-only early return. Every world/UI/capture path below
                // therefore closes the same current-frame graph exactly once.
                m_renderer.BeginFrameGraph( *m_renderBackendView.renderGraph );
                Render( renderModels, presentationAlpha );
            }
            PROFILE_END( m_profiler, "Frame/Render" );

#if defined( SKULLBONEZ_DEVELOPMENT_TOOLS )
            // Invariant: copy the completed world backbuffer before either
            // operator surface draws. The persistent texture follows only the
            // swap-chain extent, so dock drags never recreate GPU resources.
            if ( m_imguiEditor.IsVisible() )
            {
                const SkullbonezCore::Core::SbResult viewportCapture = m_imguiEditor.CaptureGameViewport();
                if ( !viewportCapture.ok )
                {
                    m_timers.frameTimer.StopTimer();
                    PROFILE_FRAME_END( m_profiler );
                    m_applicationExit.RequestOwnedFailure( viewportCapture );
                    return m_applicationExit.Resolve( 0 );
                }
            }
#endif

            SkullbonezCore::UI::OperatorEditorFrameView operatorEditorView;
#if defined( SKULLBONEZ_DEVELOPMENT_TOOLS )
            operatorEditorView.surfaces.secondaryVisible = m_imguiEditor.IsVisible();
#endif
            const RuntimeUiTextFrameFacts uiTextFacts{
                RuntimeCameraModeEnabledMask( m_sceneController.State().isSceneMode,
                                              m_sceneController.Scene().SceneEntityCount() ),
                m_camera.mode == RunCameraMode::Attach ? m_attachedCamera.ModeLabel()
                                                       : RunCameraModeLabel( m_camera.mode ),
                m_runtimeTools.LauncherFireModeLabel(),
                RunCameraModeUsesLauncher( m_camera.mode ),
                m_interaction.Gesture().kind,
                m_interaction.Gesture().gizmoKind,
                presentationAlpha,
                capturePresentationPinned,
                secondsPerFrame,
                legacyDevelopmentUiActive };
            // Lifetime: replay publishes one immutable cause/scrubber view for
            // both the legacy late pass and the development editor. E14 reads
            // its rows directly instead of building a second causality tree.
            const ReplayOverlay::ReplayOverlayStateView replayOverlay =
                m_replayRuntime.BuildOverlayStateView( m_runtimeTools.Editor().editorModeEnabled,
                                                       m_operatorUi->IsVisible(),
                                                       m_operatorUi->IsMinimized(),
                                                       m_interaction.Gesture().kind,
                                                       renderModels.presentationRecords,
                                                       renderModels.bodyStore );
            OperatorEditorFrameComposer::Render( frameHost,
                                                 frameInteraction,
                                                 frameScene,
                                                 m_renderer,
                                                 m_replayRuntime,
                                                 uiTextFacts,
                                                 operatorEditorView,
                                                 replayOverlay,
                                                 frameRenderDiagnostics,
                                                 uiRender,
                                                 renderModels );

#if defined( SKULLBONEZ_DEVELOPMENT_TOOLS )
            // Concept: the context owner builds one typed editor frame, then
            // RuntimeRenderer records its prepared draw data through the live
            // graph callback before Present. Win32 routing remains isolated to E7.
            const UINT windowDpi = GetDpiForWindow( m_window.NativeWindowHandle() );
            const float dpiScale = windowDpi > 0u ? static_cast<float>( windowDpi ) / 96.0f : 1.0f;
            const SkullbonezCore::Core::DevelopmentTools::TracyClientStatus tracyStatus =
                SkullbonezCore::Core::DevelopmentTools::TracyClientOwner::CopyStatus();
            const DevelopmentTools::ImGuiEditorFrameInput imguiFrameInput{ m_window.ClientWidth(),
                                                                           m_window.ClientHeight(),
                                                                           dpiScale,
                                                                           static_cast<float>( secondsPerFrame ),
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
                    m_timers.frameTimer.StopTimer();
                    PROFILE_FRAME_END( m_profiler );
                    m_applicationExit.RequestOwnedFailure( imguiResult.status );
                    return m_applicationExit.Resolve( 0 );
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
                        // Why: this is an explicit cold diagnostics action, not
                        // steady gameplay. Starting Tracy first and recreating
                        // the idle pool preserves the rule that instrumented
                        // workers enter through their Tracy naming boundary.
                        CoreAllocation::RuntimeAllocationScope tracyStartScope(
                            CoreAllocation::RuntimeAllocationPhase::Diagnostics );
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

            PROFILE_BEGIN( m_profiler, "Frame/PostDraw/LiveStyleCapture" );
            {
                CoreAllocation::RuntimeAllocationScope allocationScope(
                    CoreAllocation::RuntimeAllocationPhase::Capture );
                m_validationHarness->SavePendingLiveStyleCapture( m_diagnosticsRuntime.Capture(),
                                                                  m_renderBackendView.RequireBackbufferCapture() );
            }
            PROFILE_END( m_profiler, "Frame/PostDraw/LiveStyleCapture" );

#if defined( SKULLBONEZ_AUTOMATION_DIAGNOSTICS )
            PROFILE_BEGIN( m_profiler, "Frame/PostDraw/InteractionAutomation" );
            InteractionAutomationDevelopmentUiView automationDevelopmentUiView;
#if defined( SKULLBONEZ_DEVELOPMENT_TOOLS )
            const DevelopmentTools::ImGuiEditorStatus imguiAutomationStatus = m_imguiEditor.CopyStatus();
            automationDevelopmentUiView.available = imguiAutomationStatus.initialized;
            automationDevelopmentUiView.selectedImGui = m_imguiEditor.SelectedSurface() == DevelopmentUiMode::ImGui;
            automationDevelopmentUiView.legacyVisible = m_operatorUi->IsVisible();
            automationDevelopmentUiView.imguiVisible = imguiAutomationStatus.visible;
            automationDevelopmentUiView.legacyReplayPresentationActive = uiTextFacts.legacyDevelopmentUiActive;
            automationDevelopmentUiView.panelVisibilityMask = imguiAutomationStatus.panelVisibilityMask;
            automationDevelopmentUiView.layoutResetCount = imguiAutomationStatus.layoutResetCount;
            automationDevelopmentUiView.automationFocusCount = imguiAutomationStatus.automationFocusCount;
            automationDevelopmentUiView.appliedDpiScale = imguiAutomationStatus.appliedDpiScale;
            automationDevelopmentUiView.rendererDescriptorHighWater = imguiAutomationStatus.rendererDescriptorHighWater;
            automationDevelopmentUiView.gameViewportRecreations = imguiAutomationStatus.gameViewportRecreations;
            automationDevelopmentUiView.preferencesRecovered = imguiAutomationStatus.preferencesRecovered;
#endif
            const InteractionAutomationFrameResult automationAfterRender =
                TickInteractionAutomationAfterRender( m_interactionAutomation,
                                                      frameInteraction,
                                                      m_sceneController,
                                                      m_replayRuntime.BuildAutomationView(),
                                                      automationDevelopmentUiView,
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
#endif

            {
                CoreAllocation::RuntimeAllocationScope allocationScope(
                    CoreAllocation::RuntimeAllocationPhase::Capture );
                if ( TickScreenshots( sceneProceedPolicy ) )
                {
                    continue;
                }
            }

            PROFILE_BEGIN( m_profiler, "Frame/PostDraw/AutoCycle" );
            TickAutoCycle( sceneProceedPolicy );
            PROFILE_END( m_profiler, "Frame/PostDraw/AutoCycle" );

            m_timers.workTimer.StopTimer();
            m_timers.cpuFrameWorkMs =
                static_cast<float>( std::clamp( m_timers.workTimer.GetElapsedTime(), 0.0, 0.25 ) * 1000.0 );

            PROFILE_BEGIN( m_profiler, "Frame/VsyncWait" );
            SkullbonezCore::Core::SbResult presentResult = SkullbonezCore::Core::SbResult::Success();
            {
                CoreAllocation::RuntimeAllocationScope allocationScope(
                    CoreAllocation::RuntimeAllocationPhase::Render );
                // Invariant: close the live production graph with its sole
                // declaration-only Present edge before the swap-chain owner
                // consumes that edge and submits the frame.
                m_renderer.FinalizeFrameGraph();
                presentResult = frameRenderOwner.Present( frameRenderDiagnostics );
            }
            PROFILE_END( m_profiler, "Frame/VsyncWait" );
            if ( !presentResult.ok )
            {
                m_timers.frameTimer.StopTimer();
                PROFILE_FRAME_END( m_profiler );
                m_applicationExit.RequestOwnedFailure( presentResult );
                return m_applicationExit.Resolve( 0 );
            }

            // Invariant: Tracy counts submitted game frames, not attempted
            // render turns, capture-only continues, or failed Presents.
            SKORE_TRACY_MARK_SUBMITTED_FRAME();

            m_timers.frameTimer.StopTimer();
            PROFILE_FRAME_END( m_profiler );

#if defined( SKULLBONEZ_PROFILE_ENABLED )
            {
                const RuntimeProfilerFrameTimes profilerTimes = m_diagnosticsRuntime.SampleProfilerFrameTimes();
                m_timers.physicsTime = profilerTimes.physicsTimeSeconds;
                m_timers.renderTime = profilerTimes.renderTimeSeconds;
                m_timers.gpuFrameWorkMs = profilerTimes.gpuFrameWorkMs;
            }
#endif

            m_diagnosticsRuntime.TickPerfLog( RuntimePerfTickContext{ m_sceneController.PerfPass() + 1,
                                                                      m_sceneController.State().currentFrame + 1,
                                                                      m_timers.physicsTime,
                                                                      m_timers.renderTime } );

            if ( TickSceneAdvance( sceneProceedPolicy ) )
            {
                continue;
            }
        }
    }
    return m_applicationExit.Resolve( messageExitCode );
}


float Run::TickPhysics( double secondsPerFrame,
                        bool capturePresentationPinned,
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
    const SimulationTickResult tick = m_simulation.Tick( SimulationTickInput{ secondsPerFrame,
                                                                              policy.physicsTimeScale,
                                                                              m_sceneController.State().isSceneMode,
                                                                              m_sceneController.State().isScenePhysics,
                                                                              m_sceneController.State().isFixedStep,
                                                                              policy.physicsAdvance,
                                                                              stepRequested,
                                                                              canStepPhysics } );
    const float presentationAlpha =
        ResolvePresentationAlpha( m_config, capturePresentationPinned, tick.presentationAlpha );
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

            SkullbonezCore::Rendering::RenderInstanceStore& contactPresentation =
                m_sceneController.Scene().MutableRenderInstances();
            contactPresentation.TickContactFeedback( m_sceneController.Scene().SceneEntityCount(), PHYSICS_FIXED_DT );
            const ScenePhysicsPostStepOutput postStep =
                m_sceneController.Scene().StepPhysics( PHYSICS_FIXED_DT, physicsWorldForces, m_workerPool );
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
        const DemoDirectorTickResult directorResult = DemoDirectorPlayback::Tick(
            m_camera,
            directorPrediction,
            SceneRuntimeStyleContext{ m_launchOptions,
                                      m_sceneController.State(),
                                      m_operatorUi->SceneNavigation().browser,
                                      m_sceneController.Scene(),
                                      m_assets,
                                      ActiveSceneCinematicConfig( m_sceneController.State(), m_config ),
                                      m_renderDefaults.CinematicBaseline() },
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
        CaptureReplayPostStep( m_runtimeTools,
                               m_sceneController,
                               m_timers,
                               *m_overlayDiagnostics,
                               m_replayRuntime,
                               m_profiler );
    }
#ifdef _DEBUG
    if ( replayCaptured )
    {
        RuntimeOverlayPresentationEdit presentationEdit = m_overlayDiagnostics->EditPresentation();
        const ReplaySceneTimelineResetInput timelineReset =
            DescribeReplaySceneTimeline( m_sceneController,
                                         m_operatorUi->SceneNavigation().overrides,
                                         m_sceneController.State(),
                                         SkullbonezCore::Core::ActiveSceneObjectCapacity( m_config ),
                                         static_cast<uint32_t>( m_launchOptions.generatedObjectTypeOverride ) );
        ReplaySolverSampleRestoreContext probeSample{ m_sceneController.Scene(),
                                                      m_sceneController.State(),
                                                      m_renderer,
                                                      presentationEdit.State(),
                                                      m_runtimeTools };
        const ReplaySceneTimelineResetOwners timelineOwners{
            m_inputRouter,
            m_interaction,
            &m_sceneController.Scene().Cameras(),
            m_sceneController.Scene().Terrain().Get(),
            m_camera,
            NormalizeRuntimeCameraMode( m_replayRuntime.BuildInputView().restoreCameraMode,
                                        m_sceneController.State().isSceneMode,
                                        RuntimeCameraModeEnabledMask( m_sceneController.State().isSceneMode,
                                                                      m_sceneController.Scene().SceneEntityCount() ) ),
            m_attachedCamera.State().activeFollow,
            m_camera.director.grabbed };
        const ReplayRestoreTransaction probeTransaction{ probeSample,
                                                         m_diagnosticsRuntime,
                                                         timelineReset,
                                                         timelineOwners };
        const ReplayArtifactTopologyOwners probeTopology{ m_simulation,
                                                          m_config,
                                                          m_assets,
                                                          m_workerPool,
                                                          m_operatorUi->SceneNavigation().overrides,
                                                          m_launchOptions.generatedObjectTypeOverride,
                                                          SkullbonezCore::Core::ActiveSceneObjectCapacity( m_config ) };
        // Why: ReplayRuntime owns probe sequencing and bounded failure state;
        // the application exit latch only preserves that first owned failure
        // while WM_QUIT unwinds the frame loop.
        const ReplayProbeTickResult probeResult = m_replayRuntime.TickProbes( probeTransaction, probeTopology );
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
    const RuntimeCaptureResult result = m_diagnosticsRuntime.Capture().TickScreenshots(
        RuntimeCaptureSceneContext{ m_sceneController.State().isSceneMode,
                                    m_sceneController.State().isInteractiveRun,
                                    m_sceneController.State().currentFrame,
                                    m_timers.simulationTimer.GetTimeSinceLastStart() * 1000.0,
                                    scenePath ? scenePath->c_str() : nullptr },
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
        m_diagnosticsRuntime.LogSceneFinished( m_sceneController,
                                               m_renderBackendView.renderDiagnostics,
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
            SceneLoadConsumerOutputs sceneLoadOutputs;
            advanced = m_sceneController
                           .Load( request,
                                  SceneLoadPolicyInputs{ m_config,
                                                         m_launchOptions,
                                                         m_renderDefaults.CinematicBaseline(),
                                                         m_startup,
                                                         m_assets,
                                                         m_workerPool,
                                                         m_diagnosticsRuntime,
                                                         m_renderBackendView.RendererName(),
                                                         m_timers.simulationTimer.GetTotalTime() },
                                  SceneLoadInteractionParticipants{
                                      m_camera,
                                      CaptureSceneLoadNavigationState( m_operatorUi->SceneNavigation() ) },
                                  SceneLoadPresentationParticipants{ m_overlayDiagnostics->PresentationSnapshot(),
                                                                     m_renderBackendView.renderFrame,
                                                                     m_renderBackendView.renderResources,
                                                                     m_renderer },
                                  sceneLoadOutputs )
                           .ok;
            ApplySceneLoadConsumerOutputs( sceneLoadOutputs,
                                           m_window,
                                           *m_operatorUi,
                                           *m_validationHarness,
                                           m_launchOptions,
                                           m_renderBackendView.renderDevice,
                                           m_renderer.VsyncEnabled(),
                                           m_timers,
                                           *m_overlayDiagnostics,
                                           m_sceneController,
                                           m_inputRouter,
                                           m_interaction,
                                           m_camera,
                                           m_attachedCamera,
                                           m_runtimeTools,
                                           m_replayRuntime );
        }
        if ( !advanced )
        {
            if ( result.completion == RuntimeCaptureCompletion::Screenshot )
            {
                PrintRuntimeExitReason(
                    "Exiting because scene screenshot capture completed and no next scene is queued." );
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

    const RuntimeCaptureResult result =
        m_diagnosticsRuntime.Capture().TickAutoCycle( m_sceneController.State().isSceneMode,
                                                      m_sceneController.State().isInteractiveRun,
                                                      m_sceneController.Scene().SceneEntityCount(),
                                                      m_camera.autoCycleInterval,
                                                      m_camera.autoCycleAccum,
                                                      m_camera.autoCycleShotsTaken,
                                                      m_camera.trackBallRow.value,
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
    const SceneFrameAdvanceResult result =
        m_sceneController.AdvanceFrame( automationGateStatus,
                                        proceedPolicy.proceedAllowed,
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
        m_diagnosticsRuntime.LogSceneFinished( m_sceneController,
                                               m_renderBackendView.renderDiagnostics,
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
        SceneLoadConsumerOutputs sceneLoadOutputs;
        loadSucceeded = m_sceneController
                            .Load( result.loadRequest,
                                   SceneLoadPolicyInputs{ m_config,
                                                          m_launchOptions,
                                                          m_renderDefaults.CinematicBaseline(),
                                                          m_startup,
                                                          m_assets,
                                                          m_workerPool,
                                                          m_diagnosticsRuntime,
                                                          m_renderBackendView.RendererName(),
                                                          m_timers.simulationTimer.GetTotalTime() },
                                   SceneLoadInteractionParticipants{
                                       m_camera,
                                       CaptureSceneLoadNavigationState( m_operatorUi->SceneNavigation() ) },
                                   SceneLoadPresentationParticipants{ m_overlayDiagnostics->PresentationSnapshot(),
                                                                      m_renderBackendView.renderFrame,
                                                                      m_renderBackendView.renderResources,
                                                                      m_renderer },
                                   sceneLoadOutputs )
                            .ok;
        ApplySceneLoadConsumerOutputs( sceneLoadOutputs,
                                       m_window,
                                       *m_operatorUi,
                                       *m_validationHarness,
                                       m_launchOptions,
                                       m_renderBackendView.renderDevice,
                                       m_renderer.VsyncEnabled(),
                                       m_timers,
                                       *m_overlayDiagnostics,
                                       m_sceneController,
                                       m_inputRouter,
                                       m_interaction,
                                       m_camera,
                                       m_attachedCamera,
                                       m_runtimeTools,
                                       m_replayRuntime );
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
    m_camera.TickControls( m_sceneController.Scene(),
                           m_attachedCamera,
                           m_config,
                           m_runtimeTools.Editor().editorModeEnabled,
                           m_runtimeTools.Editor().viewportLookActive,
                           m_sceneController.State().isSceneMode,
                           cameraDt,
                           presentationAlpha );
    DemoDirectorPredictionView directorPrediction;
    const ReplayInputView replayInput = m_replayRuntime.BuildInputView();
    directorPrediction.revealAvailable = replayInput.predictionRevealAvailable;
    directorPrediction.revealProgress = replayInput.predictionRevealProgress;
    const DemoDirectorTickResult directorResult = DemoDirectorPlayback::Tick(
        m_camera,
        directorPrediction,
        SceneRuntimeStyleContext{ m_launchOptions,
                                  m_sceneController.State(),
                                  m_operatorUi->SceneNavigation().browser,
                                  m_sceneController.Scene(),
                                  m_assets,
                                  ActiveSceneCinematicConfig( m_sceneController.State(), m_config ),
                                  m_renderDefaults.CinematicBaseline() },
        cameraDt );
    if ( directorResult.applyRevealRate )
    {
        ReplayFrameIntent intent;
        intent.applyPredictionRevealRate = true;
        intent.predictionRevealRate = directorResult.requestedRevealRate;
        (void)m_replayRuntime.ApplyFrameIntent( intent );
    }

    m_sceneController.Scene().Environment().ApplyFluidSurfaceAdjustment(
        m_inputRouter.RuntimeSnapshot().fluidSurfaceAdjustment,
        simulationDt );
}
