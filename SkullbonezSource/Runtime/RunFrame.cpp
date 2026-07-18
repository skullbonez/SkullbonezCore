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
  Contact-audio flash mode: Render-only diagnostic selector that decides which
    completed audio decisions paint body flashes after a fixed physics step.
  Contact-audio simple mode: Presentation-only path that emits from body linear
    velocity changes rather than solver contact rows.
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

Invariants:
  - Frame work updates input, simulation, capture, rendering, and diagnostics
    in a stable order used by validation and replay comparisons.
  - Capture pinning is decided before physics and camera work for that frame.
  - Frame views are created once per frame turn and never retained by helpers.
  - A successful submitted game frame emits exactly one development profiler
    frame mark; failed or capture-only turns emit none.

Related:
  - RuntimeFrameViews.h defines the frame-helper calling convention.
  - Agentic/Reference/runtime-reference.md
  - Agentic/Reference/comment-style-guide.md
*/
#include "Run.h"
#include "RuntimeOverlayDiagnostics.h"
#include "RuntimeValidationHarness.h"
#include "RuntimeFrameViews.h"
#include "RuntimeViewModel.h"
#include "Window.h"
#include "../Core/WorkerPool.h"
#include "InputFrame.h"
#include "Replay/ReplayRestoreTransactions.h"
#include "Replay/ReplayOverlayRenderer.h"
#include "Replay/ReplayRestoreService.h"
#include "DemoDirectorPlayback.h"
#include "Scene/SceneRuntimeLoad.h"

#include "CaptureSystem.h"
#include "Editor/EditorTools.h"
#include "Replay/ReplayV2Artifact.h"
#include "Allocation/RuntimeAllocationTracker.h"
#include "DevelopmentTools/TracyClientOwner.h"
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
#include "../Rendering/IRenderDiagnostics.h"
#include "../Rendering/IRenderDeviceLifecycle.h"
#include "../UI/UI.h"

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
namespace RuntimeAllocation = SkullbonezCore::Runtime::Allocation;
using SkullbonezCore::Runtime::Audio::ContactAudioFlashMode;

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

bool ShouldFlashContactAudioDecision( ContactAudioFlashMode mode,
                                      const SkullbonezCore::Runtime::Audio::ContactAudioDecision& decision )
{
    switch ( mode )
    {
    case ContactAudioFlashMode::Off:
        return false;
    case ContactAudioFlashMode::Emitted:
        return decision.submitted && decision.flashEligible;
    case ContactAudioFlashMode::Candidates:
        return true;
    case ContactAudioFlashMode::Rejected:
        return !decision.submitted;
    default:
        return decision.submitted && decision.flashEligible;
    }
}


void RenderExecuteUiTextFrame( RuntimeFrameHostView& host,
                               RuntimeFrameInteractionView& interactionOwners,
                               RuntimeFrameSceneView& sceneOwners,
                               RuntimeFramePresentationView& presentationOwners,
                               ReplayRuntime& replayRuntime,
                               const RuntimeUiTextFrameFacts& facts,
                               SkullbonezCore::Rendering::IRenderDiagnostics& renderDiagnostics,
                               const SkullbonezCore::UI::UIRenderContext& uiRender,
                               const RuntimeRenderModelFrameView& renderModels )
{
    RuntimeRenderer& renderer = presentationOwners.renderer;
    DiagnosticsRuntime& diagnosticsRuntime = host.diagnosticsRuntime;
    RunTimerState& timers = sceneOwners.timers;
    RuntimeOverlayPresentationEdit presentationEdit = sceneOwners.overlays.EditPresentation();
    RunDebugState& debug = presentationEdit.State();
    SceneController& sceneController = sceneOwners.sceneController;
    RunSceneState& scene = sceneController.State();
    SkullbonezCore::Core::EngineConfig& config = sceneOwners.config;
    RuntimeTools& runtimeTools = interactionOwners.runtimeTools;
    SkullbonezCore::UI::InGameUI& ui = interactionOwners.operatorUi;
    RuntimeInputContext& runtimeInput = interactionOwners.inputRouter.RuntimeContext();
    RunCameraState& camera = interactionOwners.camera;
    SkullbonezCore::Runtime::Audio::ContactAudioService& contactAudio = sceneOwners.contactAudio;
    SkullbonezCore::Threading::WorkerPool& workerPool = host.workerPool;
    Window& window = host.window;
    RunLaunchOptions& launchOptions = sceneOwners.launchOptions;
    // Lifetime: the two owner views and value-only facts exist only for this
    // late UI call; no render or UI owner retains them.
    const RunSceneBrowserState& uiSceneBrowser = ui.SceneNavigation().browser;
    const std::string* uiScenePath = sceneController.CurrentPath();
    RuntimeViewModel runtimeViewModel;
    RuntimeRenderTargetPreviewSnapshot renderTargetPreviews;
    const ReplayOverlay::ReplayOverlayStateView replayOverlay =
        replayRuntime.BuildOverlayStateView( runtimeTools.Editor().editorModeEnabled,
                                             ui.IsVisible(),
                                             ui.IsMinimized(),
                                             facts.interactionGesture.kind,
                                             renderModels.presentationRecords,
                                             renderModels.bodyStore );
    const UiTextPassState uiTextState{ debug,
                                       sceneController.CrossScenePauseLocked(),
                                       scene,
                                       renderer.PresentationSettings(),
                                       sceneController.Scene(),
                                       config,
                                       runtimeTools.RayCastTest(),
                                       runtimeTools.Editor(),
                                       runtimeInput,
                                       camera,
                                       runtimeViewModel,
                                       uiSceneBrowser,
                                       renderTargetPreviews,
                                       &workerPool,
                                       window.ClientWidth(),
                                       window.ClientHeight(),
                                       sceneController.QueueSize(),
                                       sceneController.HasCurrentEntry(),
                                       uiScenePath ? uiScenePath->c_str() : nullptr,
                                       CurrentSceneBrowserIndex( sceneController, uiSceneBrowser ),
                                       facts.cameraModeEnabledMask,
                                       facts.cameraModeLabel,
                                       facts.launcherFireModeLabel,
                                       facts.isLauncherCameraMode,
                                       replayOverlay.shouldRenderScrubber,
                                       replayRuntime.BuildInputView().hasPathTarget };

    if ( renderer.ShouldRenderUiText( uiTextState, ui ) )
    {
        runtimeViewModel =
            RuntimeViewModelBuilder::Build( RuntimeViewModelContext{ sceneController.State(),
                                                                     sceneController.Scene(),
                                                                     sceneController.QueueSize(),
                                                                     diagnosticsRuntime.Capture(),
                                                                     config.runtimeRender.presentationInterpolation,
                                                                     facts.presentationPinned,
                                                                     facts.presentationAlpha },
                                            contactAudio );
        const SkullbonezCore::Core::CinematicRenderConfig& uiCinematic = ActiveSceneCinematicConfig( scene, config );
        const bool uiCinematicRendering = IsSceneCinematicRenderingEnabled( scene, config, launchOptions, debug, true );
        const bool shadowsAvailable =
            uiCinematicRendering ? uiCinematic.shadow.enabled : config.ordinaryRender.shadow.enabled;
        renderTargetPreviews =
            renderer.BuildRenderTargetPreviewSnapshot( shadowsAvailable,
                                                       uiCinematicRendering,
                                                       uiCinematicRendering && uiCinematic.volumetricLightingEnabled );
        const ReplayOverlay::ReplayOverlayRenderContext replayOverlayContext{ *uiRender.commands,
                                                                              host.profiler,
                                                                              replayOverlay.scrubber,
                                                                              replayOverlay.prediction,
                                                                              replayOverlay.pathVisualizer,
                                                                              replayOverlay.velocityEdit,
                                                                              replayOverlay.causeTree,
                                                                              replayOverlay.solverStats,
                                                                              replayOverlay.selectedPresentation,
                                                                              replayOverlay.latestPresentation,
                                                                              replayOverlay.selectedSolver,
                                                                              replayOverlay.latestSolver,
                                                                              replayOverlay.selectedPrediction,
                                                                              replayOverlay.currentPresentation,
                                                                              replayOverlay.currentSolver,
                                                                              replayOverlay.solverPresentTrackPosition,
                                                                              replayOverlay.loadedPresentation,
                                                                              replayOverlay.predictionTimelineAvailable,
                                                                              replayOverlay.shouldRenderScrubber,
                                                                              runtimeTools.Editor().editorModeEnabled,
                                                                              ui.IsVisible(),
                                                                              ui.IsMinimized(),
                                                                              scene.isScenePhysics,
                                                                              facts.interactionGesture.kind,
                                                                              window.ClientWidth(),
                                                                              window.ClientHeight(),
                                                                              timers.simulationTimer.GetTotalTime() };
        const int uiDrawCallStart = renderDiagnostics.GetFrameDrawCallCount();
        const bool replayMemoryStatsRequested =
            ui.IsVisible() && !ui.IsMinimized() && ui.GetActiveTab() == SkullbonezCore::UI::InGameUITab::Memory;
        const ReplayHudStatus replayHud = replayRuntime.BuildHudStatus( replayMemoryStatsRequested );
        PROFILE_BEGIN( host.profiler, "Frame/UI" );
        {
            RuntimeAllocation::RuntimeAllocationScope allocationScope(
                RuntimeAllocation::RuntimeAllocationPhase::Render );
            DRAW_CALL_TRACE_SCOPE( renderDiagnostics, "Frame/UI" );
            renderer.RenderUiText( renderDiagnostics,
                                   uiRender,
                                   uiTextState,
                                   timers,
                                   ui,
                                   renderModels,
                                   diagnosticsRuntime,
                                   replayHud,
                                   replayOverlayContext,
                                   uiCinematic,
                                   uiCinematicRendering,
                                   facts.secondsPerFrame );
        }
        PROFILE_END( host.profiler, "Frame/UI" );
        const int uiDrawCallEnd = renderDiagnostics.GetFrameDrawCallCount();
        timers.lastUIDrawCalls = (std::max)( 0, uiDrawCallEnd - uiDrawCallStart );
    }
    else
    {
        timers.lastUIDrawCalls = 0;
    }
}


} // namespace

namespace
{

void ExecuteContactAudioPostStep( SkullbonezCore::Runtime::Audio::ContactAudioService& contactAudio,
                                  RunTimerState& timers,
                                  DiagnosticsRuntime& diagnosticsRuntime,
                                  RunSceneState& scene,
                                  const Vector3& listenerPosition,
                                  SceneWorld& world,
                                  SkullbonezCore::Core::Profiler* profiler )
{
#ifndef _DEBUG
    (void)diagnosticsRuntime;
    (void)scene;
#endif
    PROFILE_SCOPED( profiler, "Frame/Physics/Step/ContactAudio" );

    contactAudio.BeginPhysicsStep( PHYSICS_FIXED_DT, listenerPosition );

    const auto colliderRecords = world.Colliders().Records();
    auto materialForBody = [&]( int bodyIndex ) -> uint32_t
    {
        if ( bodyIndex >= 0 && bodyIndex < static_cast<int>( colliderRecords.size() ) )
        {
            return colliderRecords[static_cast<std::size_t>( bodyIndex )].contactMaterialId;
        }
        return HashStr( "default" );
    };

    if ( contactAudio.SimpleModeEnabled() )
    {
        // Why: Simple Mode answers the practical sound question directly:
        // did a dynamic body experience enough mass-scaled linear velocity
        // change to be heard? Motion comes from PhysicsBodyStore and contact
        // material comes from the paired ColliderStore row.
        const PhysicsBodyStore& bodyStore = world.BodyStore();
        const auto bodyRecords = bodyStore.Records();
        const auto hotFields = bodyStore.HotFields();
        const int simpleBodyCount = static_cast<int>(
            bodyRecords.size() < colliderRecords.size() ? bodyRecords.size() : colliderRecords.size() );
        contactAudio.BeginSimpleLinearStep( simpleBodyCount );
        for ( int bodyIndex = 0; bodyIndex < simpleBodyCount; ++bodyIndex )
        {
            const PhysicsBodyRecord& body = bodyRecords[static_cast<std::size_t>( bodyIndex )];
            const std::size_t hotIndex = static_cast<std::size_t>( bodyIndex );
            if ( hotFields.fixed[hotIndex] != 0u )
            {
                continue;
            }
            contactAudio.SubmitLinearMotion( bodyIndex,
                                             colliderRecords[static_cast<std::size_t>( bodyIndex )].contactMaterialId,
                                             PhysicsBodyPosition( hotFields, hotIndex ),
                                             PhysicsBodyLinearVelocity( hotFields, hotIndex ),
                                             body.mass );
        }
    }
    else
    {
        // Why: PhysicsDebugContact rows are emitted after accumulated normal
        // impulses are known. Audio can consume those facts without entering
        // solver math or changing deterministic physics state.
        const std::vector<PhysicsDebugContact>& contacts = PhysicsEngine::ReadDebugContacts( world.Physics() );
        for ( const PhysicsDebugContact& contact : contacts )
        {
            if ( contact.bodyA < 0 || contact.normalImpulse <= 0.0f )
            {
                continue;
            }

            SkullbonezCore::Runtime::Audio::ContactAudioEvent event;
            event.bodyA = contact.bodyA;
            event.bodyB = contact.bodyB;
            event.featureId = contact.featureId;
            event.materialA = materialForBody( contact.bodyA );
            event.materialB = materialForBody( contact.bodyB );
            event.point = contact.point;
            event.normal = contact.normal;
            event.normalImpulse = contact.normalImpulse;
            // Why: sound uses pre-solve relative motion so stationary wall bricks
            // receiving propagated constraint force do not all become emitters.
            event.normalClosingSpeed = contact.preSolveClosingSpeed;
            event.tangentSlipSpeed = contact.preSolveSlipSpeed;
            event.isTerrain = contact.bodyB < 0;
            event.hasMotionData = true;
            contactAudio.SubmitContact( event );
        }
    }

    contactAudio.EndPhysicsStep();
#ifdef _DEBUG
    if ( diagnosticsRuntime.PhysicsDiagnosticsEnabled() )
    {
        RuntimeDiagnostics::LogContactAudioStepStats( diagnosticsRuntime.PhysicsDiagnostics(),
                                                      scene,
                                                      contactAudio.StepStats() );
        const int decisionCount = contactAudio.DecisionCount();
        for ( int i = 0; i < decisionCount; ++i )
        {
            SkullbonezCore::Runtime::Audio::ContactAudioDecision decision;
            if ( contactAudio.GetDecision( i, decision ) )
            {
                RuntimeDiagnostics::LogContactAudioDecision( diagnosticsRuntime.PhysicsDiagnostics(), scene, decision );
            }
        }
    }
#endif
    if ( contactAudio.FlashMode() != ContactAudioFlashMode::Off )
    {
        // Why: Sound-tab diagnostics can visualize emitted sounds, all
        // candidates, or rejected candidates without touching physics state.
        constexpr float CONTACT_AUDIO_FLASH_SECONDS = 0.1f;
        const int decisionCount = contactAudio.DecisionCount();
        for ( int i = 0; i < decisionCount; ++i )
        {
            SkullbonezCore::Runtime::Audio::ContactAudioDecision decision;
            if ( !contactAudio.GetDecision( i, decision ) ||
                 !ShouldFlashContactAudioDecision( contactAudio.FlashMode(), decision ) )
            {
                continue;
            }

            world.MutableRenderInstances().NotifyAudioContact( decision.event.bodyA, CONTACT_AUDIO_FLASH_SECONDS );
            world.MutableRenderInstances().NotifyAudioContact( decision.event.bodyB, CONTACT_AUDIO_FLASH_SECONDS );
        }
    }
    if ( contactAudio.DebugCountersEnabled() )
    {
        timers.contactAudioStatsLogTime += PHYSICS_FIXED_DT;
        if ( timers.contactAudioStatsLogTime >= 1.0f )
        {
            const SkullbonezCore::Runtime::Audio::ContactAudioStats& stats = contactAudio.Stats();
            printf( "[audio] contact stats facts=%u patches=%u merged=%u threshold=%u cooldown=%u "
                    "submitted=%u rolling=%u/%u budget=%u dropped=%u\n",
                    stats.eventsSeen,
                    stats.patchCandidates,
                    stats.mergedCandidates,
                    stats.rejectedByThreshold,
                    stats.rejectedByCooldown,
                    stats.submittedVoices,
                    stats.rollingSubmittedVoices,
                    stats.rollingCandidates,
                    stats.candidateOverflows + stats.burstWindowSkippedCandidates + stats.budgetRejectedCandidates,
                    stats.droppedVoices );
            contactAudio.ResetFrameStats();
            timers.contactAudioStatsLogTime = 0.0f;
        }
    }
}

void CaptureReplayPostStep( RuntimeFrameInteractionView& interactionOwners,
                            RuntimeFrameSceneView& sceneOwners,
                            ReplayRuntime& replayRuntime,
                            SkullbonezCore::Core::Profiler* profiler )
{
    RuntimeTools& runtimeTools = interactionOwners.runtimeTools;
    SkullbonezCore::Runtime::SceneController& models = sceneOwners.sceneController;
    const RunSceneState& scene = models.State();
    RunTimerState& timers = sceneOwners.timers;
    const RunDebugState debug = sceneOwners.overlays.PresentationSnapshot();
    SkullbonezCore::Environment::CameraCollection& cameras = models.Scene().Cameras();
    SkullbonezCore::Environment::WorldEnvironment& world = models.Scene().Environment();
    PhysicsEngine& physics = models.Scene().Physics();
    const SceneEntityStore& entities = models.Scene().Entities();
    RuntimeAllocation::RuntimeAllocationScope allocationScope( RuntimeAllocation::RuntimeAllocationPhase::Replay );
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
    input.entities = &entities;
    input.bodyStore = &models.Scene().BodyStore();
    input.colliderStore = &models.Scene().Colliders();
    replayRuntime.CaptureFrame( input, runtimeTools );
}

} // namespace

SkullbonezCore::Core::SbResult Run::Execute()
{
    if ( m_skipExecute )
    {
        return SkullbonezCore::Core::SbResult::Success();
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
            RuntimeAllocation::RuntimeAllocationScope frameAllocationScope(
                RuntimeAllocation::RuntimeAllocationPhase::SteadyGameplay );
            double secondsPerFrame = m_timers.frameTimer.GetElapsedTime();
            secondsPerFrame = std::clamp( secondsPerFrame, 0.0, 0.05 );

            m_timers.frameTimer.StartTimer();
            PROFILE_FRAME_BEGIN( m_profiler );
            m_timers.workTimer.StartTimer();
            // Lifetime: borrow the startup-owned renderer once for this frame
            // turn. Narrow facets keep reset, GPU-drain, UI accounting, and
            // present from each reaching through the process-global service.
            if ( !m_renderBackendView.deviceLifecycle || !m_renderBackendView.renderDiagnostics ||
                 !m_renderBackendView.renderResources || !m_renderBackendView.renderCommands )
            {
                SB_FATAL( "RunFrame", "Run::Execute requires a render backend." );
            }
            SkullbonezCore::Rendering::IRenderDiagnostics& frameRenderDiagnostics =
                *m_renderBackendView.renderDiagnostics;
            SkullbonezCore::Rendering::IRenderDeviceLifecycle& renderLifecycle = *m_renderBackendView.deviceLifecycle;
            SkullbonezCore::Rendering::IRenderResourceFactory& frameRenderResources =
                *m_renderBackendView.renderResources;
            SkullbonezCore::Rendering::IRenderCommandContext& frameRenderCommands = *m_renderBackendView.renderCommands;
            const SkullbonezCore::UI::UIRenderContext uiRender = { &m_assets,
                                                                   &frameRenderResources,
                                                                   &frameRenderCommands,
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
                                              m_contactAudio,
                                              m_sceneController };
            RuntimeFramePresentationView framePresentation{ m_renderDefaults,
                                                            *m_validationHarness,
                                                            m_renderBackendView,
                                                            m_renderer };
            frameRenderDiagnostics.ResetFrameDrawCalls();

            PROFILE_BEGIN( m_profiler, "Frame/Input" );
#if defined( SKULLBONEZ_AUTOMATION_DIAGNOSTICS )
            const ReplayAutomationView automationReplayView = m_replayRuntime.BuildAutomationView();
            const ReplayInputView automationReplayInput = automationReplayView.input;
            const InteractionAutomationFrameResult automationBeforeInput =
                TickInteractionAutomationBeforeInput( m_interactionAutomation,
                                                      frameHost,
                                                      frameInteraction,
                                                      frameScene,
                                                      automationReplayView );
            if ( automationBeforeInput.applyCameraMode )
            {
                m_inputRouter.ApplyCameraMode( automationBeforeInput.cameraMode,
                                               RuntimeInputActionSource::Runtime,
                                               frameInteraction,
                                               frameScene,
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
                    frameScene,
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
            ProcessInputFrame( frameHost, frameInteraction, frameScene, framePresentation, m_replayRuntime );
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
                RuntimeAllocation::RuntimeAllocationScope allocationScope(
                    RuntimeAllocation::RuntimeAllocationPhase::Physics );
                simulationPresentationAlpha =
                    TickPhysics( secondsPerFrame, frameInteraction, frameScene, capturePresentationPinned );
            }

            {
                // Invariant: prediction scheduling completes before overlay
                // construction. Render consumes only the published future and
                // cannot decide whether the private engine advances.
                RuntimeAllocation::RuntimeAllocationScope allocationScope(
                    RuntimeAllocation::RuntimeAllocationPhase::Replay );
                m_replayRuntime.UpdatePrediction( m_sceneController.Scene().Physics(),
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
                                                             frameRenderDiagnostics );
            const float presentationAlpha =
                ResolvePresentationAlpha( m_config, capturePresentationPinned, simulationPresentationAlpha );

            if ( m_renderer.PipelineSyncEnabled() )
            {
                PROFILE_BEGIN( m_profiler, "Frame/PipelineSync" );
                SkullbonezCore::Core::SbResult finishResult = SkullbonezCore::Core::SbResult::Success();
                {
                    RuntimeAllocation::RuntimeAllocationScope allocationScope(
                        RuntimeAllocation::RuntimeAllocationPhase::Render );
                    finishResult = renderLifecycle.Finish();
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
                RuntimeAllocation::RuntimeAllocationScope allocationScope(
                    RuntimeAllocation::RuntimeAllocationPhase::Render );
                DRAW_CALL_TRACE_SCOPE( frameRenderDiagnostics, "Frame/Render" );
                Render( renderModels, presentationAlpha );
            }
            PROFILE_END( m_profiler, "Frame/Render" );

            const RuntimeUiTextFrameFacts uiTextFacts{
                RuntimeCameraModeEnabledMask( m_sceneController.State().isSceneMode,
                                              m_sceneController.Scene().SceneEntityCount() ),
                m_camera.mode == RunCameraMode::Attach ? m_attachedCamera.ModeLabel()
                                                       : RunCameraModeLabel( m_camera.mode ),
                m_runtimeTools.LauncherFireModeLabel(),
                RunCameraModeUsesLauncher( m_camera.mode ),
                m_interaction.Gesture(),
                presentationAlpha,
                capturePresentationPinned,
                secondsPerFrame };
            RenderExecuteUiTextFrame( frameHost,
                                      frameInteraction,
                                      frameScene,
                                      framePresentation,
                                      m_replayRuntime,
                                      uiTextFacts,
                                      frameRenderDiagnostics,
                                      uiRender,
                                      renderModels );

#if defined( SKULLBONEZ_DEVELOPMENT_TOOLS )
            // Concept: E5 owns only a CPU-side empty dockspace. DX12 draw-data
            // submission arrives in E6 and Win32 event routing in E7.
            const UINT windowDpi = GetDpiForWindow( m_window.NativeWindowHandle() );
            const float dpiScale = windowDpi > 0u ? static_cast<float>( windowDpi ) / 96.0f : 1.0f;
            const DevelopmentTools::ImGuiEditorFrameInput imguiFrameInput{ m_window.ClientWidth(),
                                                                           m_window.ClientHeight(),
                                                                           dpiScale,
                                                                           static_cast<float>( secondsPerFrame ) };
            if ( m_imguiEditor.BeginFrame( imguiFrameInput ) )
            {
                m_imguiEditor.BuildEmptyDockspace();
                const DevelopmentTools::ImGuiEditorCommands commands = m_imguiEditor.EndFrame();
                if ( commands.requestHide )
                {
                    m_imguiEditor.SetVisible( false );
                }
            }
#endif

            PROFILE_BEGIN( m_profiler, "Frame/PostDraw/LiveStyleCapture" );
            {
                RuntimeAllocation::RuntimeAllocationScope allocationScope(
                    RuntimeAllocation::RuntimeAllocationPhase::Capture );
                m_validationHarness->SavePendingLiveStyleCapture( m_diagnosticsRuntime.Capture(),
                                                                  m_renderBackendView.RequireCaptureBackend() );
            }
            PROFILE_END( m_profiler, "Frame/PostDraw/LiveStyleCapture" );

#if defined( SKULLBONEZ_AUTOMATION_DIAGNOSTICS )
            PROFILE_BEGIN( m_profiler, "Frame/PostDraw/InteractionAutomation" );
            const InteractionAutomationFrameResult automationAfterRender =
                TickInteractionAutomationAfterRender( m_interactionAutomation,
                                                      frameInteraction,
                                                      frameScene,
                                                      m_replayRuntime.BuildAutomationView(),
                                                      m_diagnosticsRuntime.Capture(),
                                                      m_renderBackendView.RequireCaptureBackend() );
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
                RuntimeAllocation::RuntimeAllocationScope allocationScope(
                    RuntimeAllocation::RuntimeAllocationPhase::Capture );
                if ( TickScreenshots() )
                {
                    continue;
                }
            }

            PROFILE_BEGIN( m_profiler, "Frame/PostDraw/AutoCycle" );
            TickAutoCycle();
            PROFILE_END( m_profiler, "Frame/PostDraw/AutoCycle" );

            m_timers.workTimer.StopTimer();
            m_timers.cpuFrameWorkMs =
                static_cast<float>( std::clamp( m_timers.workTimer.GetElapsedTime(), 0.0, 0.25 ) * 1000.0 );

            PROFILE_BEGIN( m_profiler, "Frame/VsyncWait" );
            SkullbonezCore::Core::SbResult presentResult = SkullbonezCore::Core::SbResult::Success();
            {
                RuntimeAllocation::RuntimeAllocationScope allocationScope(
                    RuntimeAllocation::RuntimeAllocationPhase::Render );
                presentResult = renderLifecycle.Present();
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

            if ( TickSceneAdvance() )
            {
                continue;
            }
        }
    }
    return m_applicationExit.Resolve( messageExitCode );
}


float Run::TickPhysics( double secondsPerFrame,
                        RuntimeFrameInteractionView& interactionOwners,
                        RuntimeFrameSceneView& sceneOwners,
                        bool capturePresentationPinned )
{
    const ReplayInputView replayInput = m_replayRuntime.BuildInputView();
    if ( replayInput.scrubPaused )
    {
        PROFILE_SCOPED( m_profiler, "Frame/Replay/ScrubCamera" );
        UpdateLogic( 0.0f, static_cast<float>( secondsPerFrame ), 1.0f );
        return 1.0f;
    }

    const bool replayLiveAdvanceHeld = replayInput.liveAdvanceHeld;
    const RuntimeInputSnapshot& inputSnapshot = m_inputRouter.RuntimeSnapshot();
    const bool stepRequested = inputSnapshot.frameInput.stepHeld;
    const bool replayCapture = replayInput.captureEnabled;
#ifdef _DEBUG
    const bool physicsCapture = m_diagnosticsRuntime.PerfLog().physicsRegressionLogOverride[0] != '\0' ||
                                m_diagnosticsRuntime.PerfLog().physicsCollisionTimeLogOverride[0] != '\0' ||
                                m_diagnosticsRuntime.PhysicsDiagnostics().isEnabled;
#else
    constexpr bool physicsCapture = false;
#endif
    RuntimeInteractionFramePolicy policy = m_interaction.BuildFramePolicy(
        RuntimeInteractionFrameInput{ m_sceneController.State().isScenePhysics,
                                      stepRequested,
                                      false,
                                      replayLiveAdvanceHeld,
                                      inputSnapshot.pointer.rightDown,
                                      m_runtimeTools.Editor().viewportLookActive,
                                      inputSnapshot.frameInput.replayInspectionLookActive,
                                      physicsCapture,
                                      m_sceneController.State().timeScale } );
    if ( m_sceneController.CrossScenePauseLocked() )
    {
        // Invariant: the P-key pause lock outranks camera/tool mode. Launcher
        // and passive scene cameras normally keep physics running, but the lock
        // requires Space before any simulation step can proceed.
        policy.physicsAdvance = PhysicsAdvanceState::RunWhileStepHeld;
        if ( !stepRequested )
        {
            policy.physicsTimeScale = 0.0f;
        }
    }
    const bool manipulatorPhysics = policy.manipulatorActive;
    const bool contactAudioStep = m_contactAudio.IsEnabled();
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
                m_sceneController.Scene().StepPhysics( PHYSICS_FIXED_DT, m_config, physicsWorldForces, m_workerPool );
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

            if ( manipulatorPhysics || replayCapture || contactAudioStep )
            {
                AfterPhysicsStep( interactionOwners, sceneOwners, presentationAlpha );
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


void Run::AfterPhysicsStep( RuntimeFrameInteractionView& interactionOwners,
                            RuntimeFrameSceneView& sceneOwners,
                            float presentationAlpha )
{
    m_runtimeTools.RestoreMousePickupAngularVelocity( m_sceneController.Scene(), m_inputRouter, m_interaction );
    if ( m_contactAudio.IsEnabled() )
    {
        Vector3 listenerPosition = m_sceneController.Scene().Cameras().GetRenderCameraTranslation();
        // Why: audio distance/pan decisions for an attached camera must use the
        // same interpolated target endpoint as the upcoming rendered camera,
        // not the previous frame's cached render pose.
        if ( RunCameraModeIsAttached( m_camera.mode ) )
        {
            (void)m_attachedCamera.TryGetPresentationListenerPosition( m_sceneController.Scene(),
                                                                       presentationAlpha,
                                                                       listenerPosition );
        }
        ExecuteContactAudioPostStep( m_contactAudio,
                                     m_timers,
                                     m_diagnosticsRuntime,
                                     m_sceneController.State(),
                                     listenerPosition,
                                     m_sceneController.Scene(),
                                     m_profiler );
    }
    const bool replayCaptured = m_replayRuntime.BuildInputView().captureEnabled;
    if ( replayCaptured )
    {
        CaptureReplayPostStep( interactionOwners, sceneOwners, m_replayRuntime, m_profiler );
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


bool Run::TickScreenshots()
{
    PROFILE_BEGIN( m_profiler, "Frame/PostDraw/Screenshots" );
    if ( m_sceneController.CrossScenePauseLocked() && !m_inputRouter.RuntimeSnapshot().frameInput.stepHeld )
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
        m_renderBackendView.RequireCaptureBackend() );

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
                                                         m_workerPool },
                                  SceneLoadHostParticipants{ m_timers, m_diagnosticsRuntime, m_simulation },
                                  SceneLoadInteractionParticipants{
                                      m_inputRouter,
                                      m_interaction,
                                      m_camera,
                                      m_attachedCamera.State(),
                                      m_runtimeTools,
                                      CaptureSceneLoadNavigationState( m_operatorUi->SceneNavigation() ) },
                                  SceneLoadPresentationParticipants{ m_replayRuntime,
                                                                     *m_overlayDiagnostics,
                                                                     m_renderBackendView,
                                                                     m_renderer },
                                  sceneLoadOutputs )
                           .ok;
            ApplySceneLoadConsumerOutputs( sceneLoadOutputs,
                                           m_window,
                                           *m_operatorUi,
                                           m_contactAudio,
                                           *m_validationHarness,
                                           m_launchOptions );
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


void Run::TickAutoCycle()
{
    if ( m_sceneController.CrossScenePauseLocked() && !m_inputRouter.RuntimeSnapshot().frameInput.stepHeld )
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
                                                      m_renderBackendView.RequireCaptureBackend() );

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


bool Run::TickSceneAdvance()
{
    const bool sceneProceedAllowed =
        !m_sceneController.CrossScenePauseLocked() || m_inputRouter.RuntimeSnapshot().frameInput.stepHeld;
    const SceneAutomationGateStatus automationGateStatus = m_validationHarness->SceneGates().Status();
    const SceneFrameAdvanceResult result =
        m_sceneController.AdvanceFrame( automationGateStatus,
                                        sceneProceedAllowed,
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
                                                          m_workerPool },
                                   SceneLoadHostParticipants{ m_timers, m_diagnosticsRuntime, m_simulation },
                                   SceneLoadInteractionParticipants{
                                       m_inputRouter,
                                       m_interaction,
                                       m_camera,
                                       m_attachedCamera.State(),
                                       m_runtimeTools,
                                       CaptureSceneLoadNavigationState( m_operatorUi->SceneNavigation() ) },
                                   SceneLoadPresentationParticipants{ m_replayRuntime,
                                                                      *m_overlayDiagnostics,
                                                                      m_renderBackendView,
                                                                      m_renderer },
                                   sceneLoadOutputs )
                            .ok;
        ApplySceneLoadConsumerOutputs( sceneLoadOutputs,
                                       m_window,
                                       *m_operatorUi,
                                       m_contactAudio,
                                       *m_validationHarness,
                                       m_launchOptions );
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
