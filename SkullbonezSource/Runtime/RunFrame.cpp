/*
File: SkullbonezSource/Runtime/RunFrame.cpp
Purpose:
  Runs one frame of input, simulation, rendering, profiling, and presentation.

Mental model:
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

Invariants:
  - Frame work updates input, simulation, capture, rendering, and diagnostics
    in a stable order used by validation and replay comparisons.
  - Capture pinning is decided before physics and camera work for that frame.
  - Frame views are created once per frame turn and never retained by helpers.

Related:
  - RuntimeFrameViews.h defines the frame-helper calling convention.
  - Agentic/Reference/runtime-reference.md
  - Agentic/Reference/comment-style-guide.md
*/
#include "Run.h"
#include "../Core/WorkerPool.h"
#include "RuntimeStressController.h"
#include "InputFrame.h"
#include "Replay/ReplayRuntimeOwnerViews.h"
#include "Replay/ReplayRestoreService.h"
#include "RunDemoDirector.h"
#include "Scene/SceneRuntimeLoad.h"

#include "CaptureSystem.h"
#include "Editor/EditorTools.h"
#include "Replay/ReplayV2Artifact.h"
#include "Allocation/RuntimeAllocationTracker.h"
#include "RuntimeTuning.h"
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

#include <cmath>
#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <limits>
#include <utility>
#include <vector>

using namespace SkullbonezCore::Basics;
using namespace SkullbonezCore::Math::CollisionDetection;
using namespace SkullbonezCore::Math::Orientation;
using namespace SkullbonezCore::Math::Transformation;
using namespace SkullbonezCore::Physics;
using namespace SkullbonezCore::Basics::RunInternal;
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
                               const RuntimeUiTextFrameFacts& facts,
                               SkullbonezCore::Rendering::IRenderDiagnostics& renderDiagnostics,
                               const SkullbonezCore::UI::UIRenderContext& uiRender,
                               const RuntimeRenderModelFrameView& renderModels )
{
    RuntimeRenderer& renderer = presentationOwners.renderer;
    DiagnosticsRuntime& diagnosticsRuntime = host.diagnosticsRuntime;
    ReplayRuntime& replayRuntime = interactionOwners.replayRuntime;
    RunTimerState& timers = sceneOwners.timers;
    RunDebugState& debug = sceneOwners.debug;
    SceneController& sceneController = sceneOwners.sceneController;
    RunSceneState& scene = sceneController.State();
    EngineConfig& config = sceneOwners.config;
    SkullbonezCore::Environment::WorldEnvironment& worldEnvironment = sceneController.World();
    RuntimeTools& runtimeTools = interactionOwners.runtimeTools;
    SkullbonezCore::UI::InGameUI& ui = interactionOwners.ui;
    RuntimeInputContext& runtimeInput = interactionOwners.inputRouter.RuntimeContext();
    RunCameraState& camera = interactionOwners.camera;
    SkullbonezCore::Runtime::Audio::ContactAudioService& contactAudio = sceneOwners.contactAudio;
    SkullbonezCore::Threading::WorkerPool& workerPool = host.workerPool;
    Window& window = host.window;
    RunLaunchOptions& launchOptions = sceneOwners.launchOptions;
    // Lifetime: the two owner views and value-only facts exist only for this
    // late UI call; no render or UI owner retains them.
    const RunSceneBrowserState& uiSceneBrowser = sceneController.Browser();
    const std::string* uiScenePath = sceneController.CurrentPath();
    RuntimeViewModel runtimeViewModel;
    RuntimeRenderTargetPreviewSnapshot renderTargetPreviews;
    const UiTextPassState uiTextState{ debug,
                                       sceneController.CrossScenePauseLocked(),
                                       scene,
                                       renderer.PresentationSettings(),
                                       sceneController,
                                       config,
                                       worldEnvironment,
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
                                       replayRuntime.ShouldRenderScrubber( runtimeTools.Editor().editorModeEnabled,
                                                                           ui.IsVisible(),
                                                                           ui.IsMinimized(),
                                                                           facts.interactionGesture.kind ),
                                       replayRuntime.HasPathVisualizerTarget() };

    if ( renderer.ShouldRenderUiText( uiTextState, ui ) )
    {
        runtimeViewModel =
            RuntimeViewModelBuilder::Build( RuntimeViewModelContext{ sceneController,
                                                                     diagnosticsRuntime.Capture(),
                                                                     sceneController.Physics(),
                                                                     config.runtimeRender.presentationInterpolation,
                                                                     facts.presentationPinned,
                                                                     facts.presentationAlpha },
                                            contactAudio );
        const CinematicRenderConfig& uiCinematic = ActiveSceneCinematicConfig( scene, config );
        const bool uiCinematicRendering = IsSceneCinematicRenderingEnabled( scene, config, launchOptions, debug, true );
        const bool shadowsAvailable =
            uiCinematicRendering ? uiCinematic.shadow.enabled : config.ordinaryRender.shadow.enabled;
        renderTargetPreviews =
            renderer.BuildRenderTargetPreviewSnapshot( shadowsAvailable,
                                                       uiCinematicRendering,
                                                       uiCinematicRendering && uiCinematic.volumetricLightingEnabled );
        const ReplayOverlayFrameState replayOverlay{ runtimeTools.Editor().editorModeEnabled,
                                                     ui.IsVisible(),
                                                     ui.IsMinimized(),
                                                     scene.isScenePhysics,
                                                     facts.interactionGesture.kind,
                                                     window.ClientWidth(),
                                                     window.ClientHeight(),
                                                     timers.simulationTimer.GetTotalTime() };
        const int uiDrawCallStart = renderDiagnostics.GetFrameDrawCallCount();
        PROFILE_BEGIN( "Frame/UI" );
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
                                   replayRuntime,
                                   replayOverlay,
                                   uiCinematic,
                                   uiCinematicRendering,
                                   facts.secondsPerFrame );
        }
        PROFILE_END( "Frame/UI" );
        const int uiDrawCallEnd = renderDiagnostics.GetFrameDrawCallCount();
        timers.lastUIDrawCalls = (std::max)( 0, uiDrawCallEnd - uiDrawCallStart );
    }
    else
    {
        timers.lastUIDrawCalls = 0;
    }
}


template <typename UpdateRequiredBroadphaseXCells, typename UpdateRequiredContacts>
void TickExecutePostPhysicsVisualizers( RunDebugState& debug,
                                        SkullbonezCore::Basics::SceneController& models,
                                        BroadphaseVisualizer& broadphaseVisualizer,
                                        CollisionVisualizer& collisionVisualizer,
                                        PhysicsDebugVisualizer& physicsDebugVisualizer,
                                        double secondsPerFrame,
                                        UpdateRequiredBroadphaseXCells updateRequiredBroadphaseXCells,
                                        UpdateRequiredContacts updateRequiredContacts )
{
    PROFILE_BEGIN( "Frame/PostPhysics" );

    PROFILE_BEGIN( "Frame/PostPhysics/BroadphaseVisualizer" );
    // Why: broadphase visualizer state runs even when the overlay is hidden so
    // cell fades and scene-gate checks stay coherent across toggles.
    {
        broadphaseVisualizer.SetEnabled( debug.isBroadphaseOverlay );
        PhysicsEngine& physics = models.Physics();
        const SpatialGrid& grid = PhysicsEngine::ReadSpatialGrid( physics );
        broadphaseVisualizer.SetCellSize( grid.GetCellSize() );
        SpatialGrid::ActiveCell activeCellBuf[SpatialGrid::MAX_BUCKETS];
        int activeCellCount = grid.GetActiveCellCount();
        grid.GetActiveCells( activeCellBuf, SpatialGrid::MAX_BUCKETS );
        const std::vector<int64_t>& collisionKeys = PhysicsEngine::ReadCollisionCellKeys( physics );
        broadphaseVisualizer.Update( static_cast<float>( secondsPerFrame ),
                                     activeCellBuf,
                                     activeCellCount,
                                     collisionKeys.data(),
                                     static_cast<int>( collisionKeys.size() ) );
        updateRequiredBroadphaseXCells( activeCellBuf, (std::min)( activeCellCount, SpatialGrid::MAX_BUCKETS ) );
    }
    PROFILE_END( "Frame/PostPhysics/BroadphaseVisualizer" );

    PROFILE_BEGIN( "Frame/PostPhysics/CollisionVisualizer" );
    collisionVisualizer.SetEnabled( debug.isCollisionVisualizer );
    const CollisionVisualizerFrameView collisionView{
        models.BodyStore(),
        models.Colliders(),
        models.RenderInstances(),
        PhysicsEngine::ReadCollisionVisualContacts( models.Physics() ),
        PhysicsEngine::ReadSleepStates( models.Physics() ),
        PhysicsEngine::ReadSleepIslandVisualIds( models.Physics() ),
        models.BodyStore().Count(),
    };
    collisionVisualizer.Update( static_cast<float>( secondsPerFrame ), collisionView );
    PROFILE_END( "Frame/PostPhysics/CollisionVisualizer" );

    PROFILE_BEGIN( "Frame/PostPhysics/PhysicsDebugVisualizer" );
    physicsDebugVisualizer.SetFlags( debug.physicsDebugFlags );
    physicsDebugVisualizer.SetContactLingerSeconds( debug.physicsDebugContactLinger );
    physicsDebugVisualizer.SetPipelineStageCursor( debug.physicsDebugPipelineStageCursor );
    const PhysicsDebugFrameView physicsDebugView{
        models.BodyStore(),
        models.Colliders(),
        PhysicsEngine::ReadSleepStates( models.Physics() ),
        PhysicsEngine::ReadSleepSupportedStates( models.Physics() ),
        PhysicsEngine::ReadSleepInhibitedStates( models.Physics() ),
        PhysicsEngine::ReadDebugContacts( models.Physics() ),
        PhysicsEngine::ReadPipelineTrace( models.Physics() ),
        models.BodyStore().Count(),
    };
    physicsDebugVisualizer.Update( static_cast<float>( secondsPerFrame ), physicsDebugView );
    updateRequiredContacts();
    PROFILE_END( "Frame/PostPhysics/PhysicsDebugVisualizer" );

    PROFILE_BEGIN( "Frame/PostPhysics/EndCollisionVisualFrame" );
    models.Physics().EndCollisionVisualFrame();
    PROFILE_END( "Frame/PostPhysics/EndCollisionVisualFrame" );

    PROFILE_END( "Frame/PostPhysics" );
}


} // namespace

namespace
{

void ExecuteContactAudioPostStep( SkullbonezCore::Runtime::Audio::ContactAudioService& contactAudio,
                                  RunTimerState& timers,
                                  DiagnosticsRuntime& diagnosticsRuntime,
                                  RunSceneState& scene,
                                  const Vector3& listenerPosition,
                                  SkullbonezCore::Basics::SceneController& models )
{
#ifndef _DEBUG
    (void)diagnosticsRuntime;
    (void)scene;
#endif
    PROFILE_SCOPED( "Frame/Physics/Step/ContactAudio" );

    contactAudio.BeginPhysicsStep( PHYSICS_FIXED_DT, listenerPosition );

    const auto& colliderRecords = models.Colliders().Records();
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
        const auto& bodyRecords = models.BodyStore().Records();
        const int simpleBodyCount = static_cast<int>(
            bodyRecords.size() < colliderRecords.size() ? bodyRecords.size() : colliderRecords.size() );
        contactAudio.BeginSimpleLinearStep( simpleBodyCount );
        for ( int bodyIndex = 0; bodyIndex < simpleBodyCount; ++bodyIndex )
        {
            const PhysicsBodyRecord& body = bodyRecords[static_cast<std::size_t>( bodyIndex )];
            if ( body.isFixed )
            {
                continue;
            }
            contactAudio.SubmitLinearMotion( bodyIndex,
                                             colliderRecords[static_cast<std::size_t>( bodyIndex )].contactMaterialId,
                                             body.position,
                                             body.linearVelocity,
                                             body.mass );
        }
    }
    else
    {
        // Why: PhysicsDebugContact rows are emitted after accumulated normal
        // impulses are known. Audio can consume those facts without entering
        // solver math or changing deterministic physics state.
        const std::vector<PhysicsDebugContact>& contacts = PhysicsEngine::ReadDebugContacts( models.Physics() );
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

            models.NotifyAudioContact( decision.event.bodyA, CONTACT_AUDIO_FLASH_SECONDS );
            models.NotifyAudioContact( decision.event.bodyB, CONTACT_AUDIO_FLASH_SECONDS );
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

void CaptureReplayPostStep( RuntimeFrameInteractionView& interactionOwners, RuntimeFrameSceneView& sceneOwners )
{
    ReplayRuntime& replayRuntime = interactionOwners.replayRuntime;
    RuntimeTools& runtimeTools = interactionOwners.runtimeTools;
    SkullbonezCore::Basics::SceneController& models = sceneOwners.sceneController;
    const RunSceneState& scene = models.State();
    RunTimerState& timers = sceneOwners.timers;
    const RunDebugState& debug = sceneOwners.debug;
    SkullbonezCore::Environment::CameraCollection& cameras = models.Cameras();
    SkullbonezCore::Environment::WorldEnvironment& world = models.World();
    PhysicsEngine& physics = models.Physics();
    const SceneEntityStore& entities = models.Entities();
    RuntimeAllocation::RuntimeAllocationScope allocationScope( RuntimeAllocation::RuntimeAllocationPhase::Replay );
    PROFILE_SCOPED( "Frame/Physics/Step/ReplayCapture" );
    ReplayLauncherVisualSample& launcherVisual = replayRuntime.LauncherVisualCaptureScratch();
    runtimeTools.BuildReplayLauncherVisualSample( launcherVisual );

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
    input.bodyStore = &models.BodyStore();
    input.colliderStore = &models.Colliders();
    input.launcherVisual = &launcherVisual;
    replayRuntime.CaptureFrame( input );
}

} // namespace

SbResult Run::Execute()
{
    if ( m_skipExecute )
    {
        return SbResult::Success();
    }
    MSG msg;
    int messageExitCode = 0;

    for ( ;; )
    {
        if ( PeekMessage( &msg, nullptr, 0, 0, PM_REMOVE ) )
        {
            if ( msg.message == WM_QUIT )
            {
                if ( m_graphicsStress.IsEnabled() )
                {
                    printf( "[graphics-stress] WM_QUIT received at frame=%d scene_frame=%d scene_loads=%d\n",
                            m_graphicsStress.FramesRun(),
                            m_sceneController.State().currentFrame,
                            m_graphicsStress.SceneLoadsRequested() );
                    fflush( stdout );
                }
                // Concept: WM_QUIT is the platform's stop notification, not the
                // process result by itself. Preserve a Run-owned failure when
                // one already exists; otherwise translate the posted integer.
                m_applicationExit.RequestNormalExit();
                messageExitCode = static_cast<int>( msg.wParam );
                break;
            }
            TranslateMessage( &msg );
            DispatchMessage( &msg );
        }
        else
        {
            RuntimeAllocation::RuntimeAllocationScope frameAllocationScope(
                RuntimeAllocation::RuntimeAllocationPhase::SteadyGameplay );
            double secondsPerFrame = m_timers.frameTimer.GetElapsedTime();
            secondsPerFrame = std::clamp( secondsPerFrame, 0.0, 0.05 );

            m_timers.frameTimer.StartTimer();
            PROFILE_FRAME_BEGIN();
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
            RuntimeFrameHostView frameHost{ m_applicationExit, m_diagnosticsRuntime, m_assets, m_workerPool, m_window };
            RuntimeFrameInteractionView frameInteraction{ m_inputRouter,
                                                          m_interaction,
                                                          m_attachedCamera,
                                                          m_replayRuntime,
                                                          m_UI,
                                                          m_runtimeTools,
                                                          m_camera };
            RuntimeFrameSceneView frameScene{ m_config,
                                              m_launchOptions,
                                              m_startup,
                                              m_timers,
                                              m_debug,
                                              m_simulation,
                                              m_contactAudio,
                                              m_sceneController };
            RuntimeFramePresentationView framePresentation{ m_renderDefaults,
                                                            m_graphicsStress,
                                                            m_physicsDebugVisualizer,
                                                            m_renderBackendView,
                                                            m_renderer };
            frameRenderDiagnostics.ResetFrameDrawCalls();

            PROFILE_BEGIN( "Frame/Input" );
            const InteractionAutomationFrameResult automationBeforeInput =
                TickInteractionAutomationBeforeInput( m_interactionAutomation,
                                                      frameHost,
                                                      frameInteraction,
                                                      frameScene );
            if ( !automationBeforeInput.status.ok )
            {
                m_applicationExit.RequestOwnedFailure( automationBeforeInput.status );
            }
            if ( automationBeforeInput.requestQuit )
            {
                PostQuitMessage( 0 );
            }
            ProcessInputFrame( frameHost, frameInteraction, frameScene, framePresentation );
            m_liveStyle.Tick(
                SceneRuntimeStyleContext{ m_launchOptions,
                                          m_sceneController.State(),
                                          m_sceneController.Browser(),
                                          m_sceneController,
                                          m_sceneController.Entities(),
                                          m_assets,
                                          ActiveSceneCinematicConfig( m_sceneController.State(), m_config ),
                                          m_renderDefaults.CinematicBaseline() } );
            PROFILE_END( "Frame/Input" );

            m_sceneController.BeginCollisionVisualFrame();
            const std::string* captureScenePath = m_sceneController.CurrentPath();
            const RuntimeCaptureSceneContext captureContext{ m_sceneController.State().isSceneMode,
                                                             m_sceneController.State().isInteractiveRun,
                                                             m_sceneController.State().currentFrame,
                                                             m_timers.simulationTimer.GetTimeSinceLastStart() * 1000.0,
                                                             captureScenePath ? captureScenePath->c_str() : nullptr };
            // Invariant: decide capture determinism before physics/camera update.
            // The frame rendered for a scheduled screenshot must use exact
            // current solver poses even when live presentation interpolation is on.
            m_capturePresentationPinned =
                m_diagnosticsRuntime.Capture().RequiresDeterministicPresentation( captureContext ) ||
                ( captureContext.isSceneMode && m_camera.autoCycleInterval > 0.0f ) ||
                m_liveStyle.HasPendingCapture() ||
                InteractionAutomationWillCaptureAfterRender( m_interactionAutomation,
                                                             m_sceneController.State().currentFrame );
            {
                RuntimeAllocation::RuntimeAllocationScope allocationScope(
                    RuntimeAllocation::RuntimeAllocationPhase::Physics );
                TickPhysics( secondsPerFrame, frameInteraction, frameScene );
            }

            TickExecutePostPhysicsVisualizers(
                m_debug,
                m_sceneController,
                m_broadphaseVisualizer,
                m_collisionVisualizer,
                m_physicsDebugVisualizer,
                secondsPerFrame,
                [this]( const SpatialGrid::ActiveCell* activeCells, int activeCellCount )
                { m_sceneController.UpdateRequiredBroadphaseXCells( activeCells, activeCellCount ); },
                [this]() { m_sceneController.UpdateRequiredContacts( m_config.bodySimulation.contactEpsilon ); } );

            // Concept: graphics stress is render/runtime churn, not UI command
            // processing. Tick it once per rendered frame so headless and
            // overnight launches keep mutating DX12 state even when the UI
            // command panel is not producing control messages.
            ExecuteGraphicsStressFrame( frameHost,
                                        frameInteraction,
                                        frameScene,
                                        framePresentation,
                                        frameRenderDiagnostics );

            if ( m_renderer.PipelineSyncEnabled() )
            {
                PROFILE_BEGIN( "Frame/PipelineSync" );
                SbResult finishResult = SbResult::Success();
                {
                    RuntimeAllocation::RuntimeAllocationScope allocationScope(
                        RuntimeAllocation::RuntimeAllocationPhase::Render );
                    finishResult = renderLifecycle.Finish();
                }
                PROFILE_END( "Frame/PipelineSync" );
                if ( !finishResult.ok )
                {
                    m_timers.frameTimer.StopTimer();
                    PROFILE_FRAME_END();
                    m_applicationExit.RequestOwnedFailure( finishResult );
                    return m_applicationExit.Resolve( 0 );
                }
            }

            RuntimeRenderModelFrameView renderModels = m_renderer.BuildModelFrameView( m_sceneController,
                                                                                       m_sceneController.Physics(),
                                                                                       m_workerPool,
                                                                                       m_config );

            PROFILE_BEGIN( "Frame/Render" );
            {
                RuntimeAllocation::RuntimeAllocationScope allocationScope(
                    RuntimeAllocation::RuntimeAllocationPhase::Render );
                DRAW_CALL_TRACE_SCOPE( frameRenderDiagnostics, "Frame/Render" );
                Render( renderModels, PresentationAlphaForFrame() );
            }
            PROFILE_END( "Frame/Render" );

            const RuntimeUiTextFrameFacts uiTextFacts{ RuntimeCameraModeEnabledMask( m_sceneController ),
                                                       m_camera.mode == RunCameraMode::Attach
                                                           ? m_attachedCamera.ModeLabel()
                                                           : RunCameraModeLabel( m_camera.mode ),
                                                       m_runtimeTools.LauncherFireModeLabel(),
                                                       RunCameraModeUsesLauncher( m_camera.mode ),
                                                       m_interaction.Gesture(),
                                                       PresentationAlphaForFrame(),
                                                       m_capturePresentationPinned,
                                                       secondsPerFrame };
            RenderExecuteUiTextFrame( frameHost,
                                      frameInteraction,
                                      frameScene,
                                      framePresentation,
                                      uiTextFacts,
                                      frameRenderDiagnostics,
                                      uiRender,
                                      renderModels );

            PROFILE_BEGIN( "Frame/PostDraw/LiveStyleCapture" );
            {
                RuntimeAllocation::RuntimeAllocationScope allocationScope(
                    RuntimeAllocation::RuntimeAllocationPhase::Capture );
                m_liveStyle.SavePendingCapture( m_diagnosticsRuntime.Capture(),
                                                m_renderBackendView.RequireCaptureBackend() );
            }
            PROFILE_END( "Frame/PostDraw/LiveStyleCapture" );

            PROFILE_BEGIN( "Frame/PostDraw/InteractionAutomation" );
            const InteractionAutomationFrameResult automationAfterRender =
                TickInteractionAutomationAfterRender( m_interactionAutomation,
                                                      frameInteraction,
                                                      frameScene,
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
            PROFILE_END( "Frame/PostDraw/InteractionAutomation" );

            {
                RuntimeAllocation::RuntimeAllocationScope allocationScope(
                    RuntimeAllocation::RuntimeAllocationPhase::Capture );
                if ( TickScreenshots() )
                {
                    continue;
                }
            }

            PROFILE_BEGIN( "Frame/PostDraw/AutoCycle" );
            TickAutoCycle();
            PROFILE_END( "Frame/PostDraw/AutoCycle" );

            m_timers.workTimer.StopTimer();
            m_timers.cpuFrameWorkMs =
                static_cast<float>( std::clamp( m_timers.workTimer.GetElapsedTime(), 0.0, 0.25 ) * 1000.0 );

            PROFILE_BEGIN( "Frame/VsyncWait" );
            SbResult presentResult = SbResult::Success();
            {
                RuntimeAllocation::RuntimeAllocationScope allocationScope(
                    RuntimeAllocation::RuntimeAllocationPhase::Render );
                presentResult = renderLifecycle.Present();
            }
            PROFILE_END( "Frame/VsyncWait" );
            if ( !presentResult.ok )
            {
                m_timers.frameTimer.StopTimer();
                PROFILE_FRAME_END();
                m_applicationExit.RequestOwnedFailure( presentResult );
                return m_applicationExit.Resolve( 0 );
            }

            m_timers.frameTimer.StopTimer();
            PROFILE_FRAME_END();

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


float Run::PresentationAlphaForFrame() const
{
    if ( !m_config.runtimeRender.presentationInterpolation || m_capturePresentationPinned )
    {
        return 1.0f;
    }
    return std::clamp( m_presentationAlpha, 0.0f, 1.0f );
}


void Run::TickPhysics( double secondsPerFrame,
                       RuntimeFrameInteractionView& interactionOwners,
                       RuntimeFrameSceneView& sceneOwners )
{
    if ( m_replayRuntime.IsScrubPaused() )
    {
        m_presentationAlpha = 1.0f;
        PROFILE_SCOPED( "Frame/Replay/ScrubCamera" );
        UpdateLogic( 0.0f, static_cast<float>( secondsPerFrame ) );
        return;
    }

    const bool replayLiveAdvanceHeld = m_replayRuntime.Scrubber().liveAdvanceHeld;
    const RuntimeInputSnapshot& inputSnapshot = m_inputRouter.RuntimeSnapshot();
    const bool stepRequested = inputSnapshot.frameInput.stepHeld;
    const bool replayCapture = m_replayRuntime.IsCaptureEnabled();
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
    const auto physicsWorldForces = m_sceneController.World().GetPhysicsWorldForces();
    constexpr bool canStepPhysics = true;
    const SimulationTickResult tick = m_simulation.Tick( SimulationTickInput{ secondsPerFrame,
                                                                              policy.physicsTimeScale,
                                                                              m_sceneController.State().isSceneMode,
                                                                              m_sceneController.State().isScenePhysics,
                                                                              m_sceneController.State().isFixedStep,
                                                                              policy.physicsAdvance,
                                                                              stepRequested,
                                                                              canStepPhysics } );
    m_presentationAlpha = tick.presentationAlpha;
    if ( tick.committedPhysicsTicks > 0 && canStepPhysics )
    {
        PROFILE_BEGIN( "Frame/Physics" );
        // Why: SimulationSystem now returns only a deterministic tick count.
        // Runtime executes the store-owned physics step directly, then applies
        // the remaining model-owned presentation sync as explicit edge work.
        for ( int tickIndex = 0; tickIndex < tick.committedPhysicsTicks; ++tickIndex )
        {
            PROFILE_SCOPED( "Frame/Physics/Step" );
            {
                PROFILE_SCOPED( "Frame/Physics/Step/PresentationCaptureBegin" );
                m_sceneController.BeginPhysicsStepPresentationCapture();
            }
            if ( manipulatorPhysics )
            {
                m_runtimeTools.ApplyMousePickupPhysicsStep( m_sceneController,
                                                            m_sceneController.Physics(),
                                                            m_inputRouter,
                                                            m_interaction );
            }

            m_sceneController.StepPhysics( PHYSICS_FIXED_DT, m_config, physicsWorldForces, m_workerPool );
            {
                PROFILE_SCOPED( "Frame/Physics/Step/PresentationCaptureComplete" );
                m_sceneController.CompletePhysicsStepPresentationCapture();
            }

            if ( manipulatorPhysics || replayCapture || contactAudioStep )
            {
                AfterPhysicsStep( interactionOwners, sceneOwners );
            }
        }
        PROFILE_END( "Frame/Physics" );
    }
    m_runtimeTools.TickRayCastTestLines( static_cast<float>( secondsPerFrame ) );
    m_runtimeTools.Laser().Update( static_cast<float>( secondsPerFrame ) );
    if ( tick.shouldUpdateLogic )
    {
        UpdateLogic( tick.simulationDt, tick.cameraDt );
    }
    else
    {
        // Why: Scene-mode, no-physics harnesses intentionally skip simulation
        // UpdateLogic, but Director is presentation state. It still needs phase
        // style/camera entry work so authored show decks behave in static scenes.
        DemoDirectorPlayback::Tick(
            m_camera,
            m_sceneController.Cameras(),
            m_replayRuntime.Prediction(),
            SceneRuntimeStyleContext{ m_launchOptions,
                                      m_sceneController.State(),
                                      m_sceneController.Browser(),
                                      m_sceneController,
                                      m_sceneController.Entities(),
                                      m_assets,
                                      ActiveSceneCinematicConfig( m_sceneController.State(), m_config ),
                                      m_renderDefaults.CinematicBaseline() },
            static_cast<float>( secondsPerFrame ) );
    }
}


void Run::AfterPhysicsStep( RuntimeFrameInteractionView& interactionOwners, RuntimeFrameSceneView& sceneOwners )
{
    m_runtimeTools.RestoreMousePickupAngularVelocity( m_sceneController,
                                                      m_sceneController.Physics(),
                                                      m_inputRouter,
                                                      m_interaction );
    if ( m_contactAudio.IsEnabled() )
    {
        Vector3 listenerPosition = m_sceneController.Cameras().GetRenderCameraTranslation();
        // Why: audio distance/pan decisions for an attached camera must use the
        // same interpolated target endpoint as the upcoming rendered camera,
        // not the previous frame's cached render pose.
        if ( RunCameraModeIsAttached( m_camera.mode ) )
        {
            (void)m_attachedCamera.TryGetPresentationListenerPosition( m_sceneController,
                                                                       m_sceneController.Cameras(),
                                                                       PresentationAlphaForFrame(),
                                                                       listenerPosition );
        }
        ExecuteContactAudioPostStep( m_contactAudio,
                                     m_timers,
                                     m_diagnosticsRuntime,
                                     m_sceneController.State(),
                                     listenerPosition,
                                     m_sceneController );
    }
    const bool replayCaptured = m_replayRuntime.IsCaptureEnabled();
    if ( replayCaptured )
    {
        CaptureReplayPostStep( interactionOwners, sceneOwners );
    }
#ifdef _DEBUG
    if ( replayCaptured )
    {
        const ReplayRuntime::SceneTimelineResetInput timelineReset = ReplayRuntime::DescribeSceneTimeline(
            m_sceneController,
            m_sceneController.State(),
            m_startup.gameModelCapacity,
            static_cast<uint32_t>( m_launchOptions.generatedObjectTypeOverride ) );
        ReplaySolverSampleRestoreContext probeSample{ m_sceneController.Physics(),
                                                      m_sceneController,
                                                      m_sceneController.State(),
                                                      m_renderer,
                                                      m_debug,
                                                      m_runtimeTools };
        const ReplayRuntime::SceneTimelineResetOwners timelineOwners{
            m_inputRouter,
            m_interaction,
            &m_sceneController.Cameras(),
            m_sceneController.Terrain().Get(),
            m_camera,
            NormalizeRuntimeCameraMode( m_replayRuntime.Camera().restoreCameraMode,
                                        m_sceneController.State().isSceneMode,
                                        RuntimeCameraModeEnabledMask( m_sceneController ) ),
            m_attachedCamera.State().activeFollow,
            m_camera.director.grabbed };
        const ReplayRuntime::ReplayRestoreTransaction probeTransaction{ probeSample,
                                                                        m_diagnosticsRuntime,
                                                                        timelineReset,
                                                                        timelineOwners };
        const ReplayRuntime::ReplayArtifactTopologyOwners probeTopology{ m_simulation,
                                                                         m_config,
                                                                         m_assets,
                                                                         m_workerPool,
                                                                         m_launchOptions.generatedObjectTypeOverride,
                                                                         m_startup.gameModelCapacity };
        // Why: ReplayRuntime owns probe sequencing and bounded failure state;
        // the application exit latch only preserves that first owned failure
        // while WM_QUIT unwinds the frame loop.
        const ReplayRuntime::ReplayProbeTickResult probeResult =
            m_replayRuntime.TickProbes( probeTransaction, probeTopology );
        if ( !probeResult.status.ok )
        {
            m_applicationExit.RequestOwnedFailure( probeResult.status );
            PostQuitMessage( 0 );
            return;
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
    PROFILE_BEGIN( "Frame/PostDraw/Screenshots" );
    if ( m_sceneController.CrossScenePauseLocked() && !m_inputRouter.RuntimeSnapshot().frameInput.stepHeld )
    {
        PROFILE_END( "Frame/PostDraw/Screenshots" );
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

    PROFILE_END( "Frame/PostDraw/Screenshots" );

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
        PROFILE_FRAME_END();
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
        const bool advanced = request.HasLoad() && m_sceneController
                                                       .Load( request,
                                                              m_config,
                                                              m_launchOptions,
                                                              m_renderDefaults.CinematicBaseline(),
                                                              m_startup,
                                                              m_diagnosticsRuntime,
                                                              m_timers,
                                                              m_assets,
                                                              m_workerPool,
                                                              m_window,
                                                              m_inputRouter,
                                                              m_interaction,
                                                              m_camera,
                                                              m_attachedCamera.State(),
                                                              m_simulation,
                                                              m_replayRuntime,
                                                              m_contactAudio,
                                                              m_UI,
                                                              m_debug,
                                                              m_graphicsStress,
                                                              m_runtimeTools,
                                                              m_physicsDebugVisualizer,
                                                              m_renderBackendView,
                                                              m_renderer )
                                                       .ok;
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
                                                      m_sceneController.SceneEntityCount(),
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
    const SceneFrameAdvanceResult result =
        m_sceneController.AdvanceFrame( sceneProceedAllowed,
                                        m_diagnosticsRuntime.PerfTestActive(),
                                        m_diagnosticsRuntime.Capture().Screenshot().isScreenshotSaved,
                                        RunCameraModeUsesManualControls( m_camera.mode,
                                                                         m_attachedCamera.State().activeFollow,
                                                                         m_camera.director.grabbed ),
                                        m_timers.simulationTimer.GetTimeSinceLastStart() );
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
        loadSucceeded = m_sceneController
                            .Load( result.loadRequest,
                                   m_config,
                                   m_launchOptions,
                                   m_renderDefaults.CinematicBaseline(),
                                   m_startup,
                                   m_diagnosticsRuntime,
                                   m_timers,
                                   m_assets,
                                   m_workerPool,
                                   m_window,
                                   m_inputRouter,
                                   m_interaction,
                                   m_camera,
                                   m_attachedCamera.State(),
                                   m_simulation,
                                   m_replayRuntime,
                                   m_contactAudio,
                                   m_UI,
                                   m_debug,
                                   m_graphicsStress,
                                   m_runtimeTools,
                                   m_physicsDebugVisualizer,
                                   m_renderBackendView,
                                   m_renderer )
                            .ok;
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


void Run::UpdateLogic( float simulationDt, float cameraDt )
{
    m_camera.AdvanceAutoCycleClock( m_sceneController.State().isSceneMode, simulationDt );
    m_camera.TickControls( m_sceneController.Cameras(),
                           *m_sceneController.Terrain().Get(),
                           m_sceneController,
                           m_attachedCamera,
                           m_config,
                           m_runtimeTools.Editor().editorModeEnabled,
                           m_runtimeTools.Editor().viewportLookActive,
                           m_sceneController.State().isSceneMode,
                           cameraDt,
                           PresentationAlphaForFrame() );
    DemoDirectorPlayback::Tick(
        m_camera,
        m_sceneController.Cameras(),
        m_replayRuntime.Prediction(),
        SceneRuntimeStyleContext{ m_launchOptions,
                                  m_sceneController.State(),
                                  m_sceneController.Browser(),
                                  m_sceneController,
                                  m_sceneController.Entities(),
                                  m_assets,
                                  ActiveSceneCinematicConfig( m_sceneController.State(), m_config ),
                                  m_renderDefaults.CinematicBaseline() },
        cameraDt );

    m_sceneController.ApplyWaterHeightControl( m_inputRouter.RuntimeSnapshot().pageDown,
                                               m_inputRouter.RuntimeSnapshot().pageUp,
                                               simulationDt );
}
