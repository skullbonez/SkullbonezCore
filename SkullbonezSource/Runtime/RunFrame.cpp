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

Invariants:
  - Frame work updates input, simulation, capture, rendering, and diagnostics
    in a stable order used by validation and replay comparisons.

Related:
  - Agentic/Reference/runtime-reference.md
  - Agentic/Reference/comment-style-guide.md
*/
#include "Run.h"
#include "InputFrame.h"
#include "Replay/ReplayRuntimeOwnerViews.h"
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
#include "../Physics/PhysicsEngineStoreQueries.h"
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

namespace
{
constexpr double PERF_TEST_PASS_SECONDS = 2.0;
constexpr float WATER_HEIGHT_CONTROL_SPEED = 20.0f; // World meters per second.
constexpr float CAMERA_MOUSE_REFERENCE_DT = 1.0f / 60.0f;

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


struct ExecuteUiTextFrameContext
{
    RuntimeRenderer& renderer;
    SkullbonezCore::Rendering::IRenderDiagnostics& renderDiagnostics;
    const SkullbonezCore::UI::UIRenderContext& uiRender;
    const RuntimeRenderModelFrameView& renderModels;
    DiagnosticsRuntime& diagnosticsRuntime;
    ReplayRuntime& replayRuntime;
    RunTimerState& timers;
    RunDebugState& debug;
    RunSceneState& scene;
    RunRuntimeSettings& runtimeSettings;
    EngineConfig& config;
    SkullbonezCore::Environment::WorldEnvironment& worldEnvironment;
    RuntimeTools& runtimeTools;
    SkullbonezCore::UI::InGameUI& ui;
    RuntimeInputContext& runtimeInput;
    RunCameraState& camera;
    RuntimeViewModel& runtimeViewModel;
    SceneController& sceneController;
    RunSubsystemState& systems;
    RunLaunchOptions& launchOptions;
    uint32_t cameraModeEnabledMask = 0;
    const char* cameraModeLabel = nullptr;
    const char* launcherFireModeLabel = nullptr;
    bool isLauncherCameraMode = false;
    double secondsPerFrame = 0.0;
};


template <typename RefreshRuntimeViewModel>
void RenderExecuteUiTextFrame( ExecuteUiTextFrameContext& context, RefreshRuntimeViewModel refreshRuntimeViewModel )
{
    // Concept: frame-loop UI text is a late render pass. Keep the state package
    // rebuilt here and refresh the scalar view only after the renderer says the
    // pass will execute, matching the previous Run::Execute ordering.
    const RunSceneBrowserState& uiSceneBrowser = context.sceneController.Browser();
    const std::string* uiScenePath = context.sceneController.CurrentPath();
    const UiTextPassState uiTextState{
        context.debug,
        context.timers,
        context.scene,
        context.runtimeSettings,
        context.config,
        context.worldEnvironment,
        context.runtimeTools.RayCastTest(),
        context.runtimeTools.Editor(),
        context.ui,
        context.runtimeInput,
        context.camera,
        context.runtimeViewModel,
        uiSceneBrowser,
        context.systems.renderPasses,
        context.systems.workerPool,
        context.systems.window ? context.systems.window->ClientWidth() : context.config.window.screenX,
        context.systems.window ? context.systems.window->ClientHeight() : context.config.window.screenY,
        context.sceneController.QueueSize(),
        context.sceneController.HasCurrentEntry(),
        uiScenePath ? uiScenePath->c_str() : nullptr,
        CurrentSceneBrowserIndex( context.sceneController, uiSceneBrowser ),
        context.cameraModeEnabledMask,
        context.cameraModeLabel,
        context.launcherFireModeLabel,
        context.isLauncherCameraMode,
        context.replayRuntime.ShouldRenderScrubber( context.runtimeTools.Editor().editorModeEnabled,
                                                    context.ui.IsVisible(),
                                                    context.ui.IsMinimized() ),
        context.replayRuntime.HasPathVisualizerTarget() };

    if ( context.renderer.ShouldRenderUiText( uiTextState ) )
    {
        refreshRuntimeViewModel();
        const CinematicRenderConfig& uiCinematic = ActiveSceneCinematicConfig( context.scene, context.config );
        const bool uiCinematicRendering = IsSceneCinematicRenderingEnabled( context.scene,
                                                                            context.config,
                                                                            context.launchOptions,
                                                                            context.debug,
                                                                            true );
        const ReplayOverlayFrameState replayOverlay{
            context.runtimeTools.Editor().editorModeEnabled,
            context.ui.IsVisible(),
            context.ui.IsMinimized(),
            context.scene.isScenePhysics,
            context.systems.window ? context.systems.window->ClientWidth() : context.config.window.screenX,
            context.systems.window ? context.systems.window->ClientHeight() : context.config.window.screenY,
            context.timers.simulationTimer.GetTotalTime(),
        };
        const int uiDrawCallStart = context.renderDiagnostics.GetFrameDrawCallCount();
        PROFILE_BEGIN( "Frame/UI" );
        {
            RuntimeAllocation::RuntimeAllocationScope allocationScope(
                RuntimeAllocation::RuntimeAllocationPhase::Render );
            DRAW_CALL_TRACE_SCOPE( context.renderDiagnostics, "Frame/UI" );
            context.renderer.RenderUiText( context.renderDiagnostics,
                                           context.uiRender,
                                           uiTextState,
                                           context.renderModels,
                                           context.diagnosticsRuntime,
                                           context.replayRuntime,
                                           replayOverlay,
                                           uiCinematic,
                                           uiCinematicRendering,
                                           context.secondsPerFrame );
        }
        PROFILE_END( "Frame/UI" );
        const int uiDrawCallEnd = context.renderDiagnostics.GetFrameDrawCallCount();
        context.timers.lastUIDrawCalls = (std::max)( 0, uiDrawCallEnd - uiDrawCallStart );
    }
    else
    {
        context.timers.lastUIDrawCalls = 0;
    }
}


struct ExecutePostPhysicsVisualizationContext
{
    RunDebugState& debug;
    SkullbonezCore::GameObjects::GameModelCollection& models;
    BroadphaseVisualizer& broadphaseVisualizer;
    CollisionVisualizer& collisionVisualizer;
    PhysicsDebugVisualizer& physicsDebugVisualizer;
};


template <typename UpdateRequiredBroadphaseXCells, typename UpdateRequiredContacts>
void TickExecutePostPhysicsVisualizers( ExecutePostPhysicsVisualizationContext& context,
                                        double secondsPerFrame,
                                        UpdateRequiredBroadphaseXCells updateRequiredBroadphaseXCells,
                                        UpdateRequiredContacts updateRequiredContacts )
{
    PROFILE_BEGIN( "Frame/PostPhysics" );

    PROFILE_BEGIN( "Frame/PostPhysics/BroadphaseVisualizer" );
    // Why: broadphase visualizer state runs even when the overlay is hidden so
    // cell fades and scene-gate checks stay coherent across toggles.
    {
        context.broadphaseVisualizer.SetEnabled( context.debug.isBroadphaseOverlay );
        context.broadphaseVisualizer.SetCellSize( context.models.GetSpatialGrid().GetCellSize() );
        const SpatialGrid& grid = context.models.GetSpatialGrid();
        SpatialGrid::ActiveCell activeCellBuf[SpatialGrid::MAX_BUCKETS];
        int activeCellCount = grid.GetActiveCellCount();
        grid.GetActiveCells( activeCellBuf, SpatialGrid::MAX_BUCKETS );
        const std::vector<int64_t>& collisionKeys = context.models.GetCollisionCellKeys();
        context.broadphaseVisualizer.Update( static_cast<float>( secondsPerFrame ),
                                             activeCellBuf,
                                             activeCellCount,
                                             collisionKeys.data(),
                                             static_cast<int>( collisionKeys.size() ) );
        updateRequiredBroadphaseXCells( activeCellBuf, (std::min)( activeCellCount, SpatialGrid::MAX_BUCKETS ) );
    }
    PROFILE_END( "Frame/PostPhysics/BroadphaseVisualizer" );

    PROFILE_BEGIN( "Frame/PostPhysics/CollisionVisualizer" );
    context.collisionVisualizer.SetEnabled( context.debug.isCollisionVisualizer );
    context.models.UpdateCollisionVisualizer( context.collisionVisualizer, static_cast<float>( secondsPerFrame ) );
    PROFILE_END( "Frame/PostPhysics/CollisionVisualizer" );

    PROFILE_BEGIN( "Frame/PostPhysics/PhysicsDebugVisualizer" );
    context.physicsDebugVisualizer.SetFlags( context.debug.physicsDebugFlags );
    context.physicsDebugVisualizer.SetContactLingerSeconds( context.debug.physicsDebugContactLinger );
    context.physicsDebugVisualizer.SetPipelineStageCursor( context.debug.physicsDebugPipelineStageCursor );
    context.models.UpdatePhysicsDebugVisualizer( context.physicsDebugVisualizer,
                                                 static_cast<float>( secondsPerFrame ) );
    updateRequiredContacts();
    PROFILE_END( "Frame/PostPhysics/PhysicsDebugVisualizer" );

    PROFILE_BEGIN( "Frame/PostPhysics/EndCollisionVisualFrame" );
    context.models.EndCollisionVisualFrame();
    PROFILE_END( "Frame/PostPhysics/EndCollisionVisualFrame" );

    PROFILE_END( "Frame/PostPhysics" );
}


} // namespace

namespace
{

struct SimulationPostStepPipelineContext
{
    SkullbonezCore::Runtime::Audio::ContactAudioService& contactAudio;
    RunRuntimeSettings& runtimeSettings;
    RunTimerState& timers;
    DiagnosticsRuntime& diagnosticsRuntime;
    RunSceneState& scene;
    RunDebugState& debug;
    SkullbonezCore::Environment::CameraCollection& cameras;
    RuntimeTools& runtimeTools;
    ReplayRuntime& replayRuntime;
    ReplayLauncherVisualSample& replayLauncherVisualScratch;
    SkullbonezCore::Environment::WorldEnvironment& world;
    SkullbonezCore::GameObjects::GameModelCollection& models;
    PhysicsEngine& physics;
    const SceneEntityStore& entities;
};

struct SimulationPostStepPipelineResult
{
    bool replayCaptured = false;
};

class SimulationPostStepPipeline
{
  public:
    static SimulationPostStepPipelineResult Run( SimulationPostStepPipelineContext& context )
    {
        SimulationPostStepPipelineResult result;
        if ( context.contactAudio.IsEnabled() )
        {
            RunContactAudio( context );
        }
        if ( context.replayRuntime.IsCaptureEnabled() )
        {
            CaptureReplayFrame( context );
            result.replayCaptured = true;
        }
        return result;
    }

  private:
    static void RunContactAudio( SimulationPostStepPipelineContext& context )
    {
        PROFILE_SCOPED( "Frame/Physics/Step/ContactAudio" );

        const Vector3 listenerPosition = context.cameras.GetRenderCameraTranslation();
        context.contactAudio.BeginPhysicsStep( PHYSICS_FIXED_DT, listenerPosition );

        const auto& colliderRecords = context.models.Colliders().Records();
        auto materialForBody = [&]( int bodyIndex ) -> uint32_t
        {
            if ( bodyIndex >= 0 && bodyIndex < static_cast<int>( colliderRecords.size() ) )
            {
                return colliderRecords[static_cast<std::size_t>( bodyIndex )].contactMaterialId;
            }
            return HashStr( "default" );
        };

        if ( context.contactAudio.SimpleModeEnabled() )
        {
            // Why: Simple Mode answers the practical sound question directly:
            // did a dynamic body experience enough mass-scaled linear velocity
            // change to be heard? Motion comes from PhysicsBodyStore and contact
            // material comes from the paired ColliderStore row.
            const auto& bodyRecords = context.models.BodyStore().Records();
            const int simpleBodyCount = static_cast<int>(
                bodyRecords.size() < colliderRecords.size() ? bodyRecords.size() : colliderRecords.size() );
            context.contactAudio.BeginSimpleLinearStep( simpleBodyCount );
            for ( int bodyIndex = 0; bodyIndex < simpleBodyCount; ++bodyIndex )
            {
                const PhysicsBodyRecord& body = bodyRecords[static_cast<std::size_t>( bodyIndex )];
                if ( body.isFixed )
                {
                    continue;
                }
                context.contactAudio.SubmitLinearMotion(
                    bodyIndex,
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
            const std::vector<PhysicsDebugContact>& contacts = context.models.GetPhysicsDebugContacts();
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
                context.contactAudio.SubmitContact( event );
            }
        }

        context.contactAudio.EndPhysicsStep();
#ifdef _DEBUG
        if ( context.diagnosticsRuntime.PhysicsDiagnosticsEnabled() )
        {
            RuntimeDiagnostics::LogContactAudioStepStats( context.diagnosticsRuntime.PhysicsDiagnostics(),
                                                          context.scene,
                                                          context.contactAudio.StepStats() );
            const int decisionCount = context.contactAudio.DecisionCount();
            for ( int i = 0; i < decisionCount; ++i )
            {
                SkullbonezCore::Runtime::Audio::ContactAudioDecision decision;
                if ( context.contactAudio.GetDecision( i, decision ) )
                {
                    RuntimeDiagnostics::LogContactAudioDecision( context.diagnosticsRuntime.PhysicsDiagnostics(),
                                                                 context.scene,
                                                                 decision );
                }
            }
        }
#endif
        if ( context.runtimeSettings.contactAudioFlashMode != ContactAudioFlashMode::Off )
        {
            // Why: Sound-tab diagnostics can visualize emitted sounds, all
            // candidates, or rejected candidates without touching physics state.
            constexpr float CONTACT_AUDIO_FLASH_SECONDS = 0.1f;
            const int decisionCount = context.contactAudio.DecisionCount();
            for ( int i = 0; i < decisionCount; ++i )
            {
                SkullbonezCore::Runtime::Audio::ContactAudioDecision decision;
                if ( !context.contactAudio.GetDecision( i, decision ) ||
                     !ShouldFlashContactAudioDecision( context.runtimeSettings.contactAudioFlashMode, decision ) )
                {
                    continue;
                }

                context.models.NotifyAudioContact( decision.event.bodyA, CONTACT_AUDIO_FLASH_SECONDS );
                context.models.NotifyAudioContact( decision.event.bodyB, CONTACT_AUDIO_FLASH_SECONDS );
            }
        }
        if ( context.runtimeSettings.contactAudioDebugCounters )
        {
            context.timers.contactAudioStatsLogTime += PHYSICS_FIXED_DT;
            if ( context.timers.contactAudioStatsLogTime >= 1.0f )
            {
                const SkullbonezCore::Runtime::Audio::ContactAudioStats& stats = context.contactAudio.Stats();
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
                context.contactAudio.ResetFrameStats();
                context.timers.contactAudioStatsLogTime = 0.0f;
            }
        }
    }

    static void CaptureReplayFrame( SimulationPostStepPipelineContext& context )
    {
        RuntimeAllocation::RuntimeAllocationScope allocationScope( RuntimeAllocation::RuntimeAllocationPhase::Replay );
        PROFILE_SCOPED( "Frame/Physics/Step/ReplayCapture" );
        context.runtimeTools.BuildReplayLauncherVisualSample( context.replayLauncherVisualScratch );

        ReplayCaptureInput input;
        input.sceneFrame = context.scene.currentFrame;
        input.simulationSeconds = context.timers.simulationTimer.GetTimeSinceLastStart();
        input.physicsDt = PHYSICS_FIXED_DT;
        input.fixedStep = context.scene.isFixedStep;
        input.scenePhysicsEnabled = context.scene.isScenePhysics;
        input.sceneTextEnabled = context.scene.isSceneText;
        input.waterHidden = context.debug.isWaterHidden;
        input.terrainHidden = context.debug.isTerrainHidden;
        input.cameras = &context.cameras;
        input.world = &context.world;
        input.physics = &context.physics;
        input.entities = &context.entities;
        input.bodyStore = &context.models.BodyStore();
        input.colliderStore = &context.models.Colliders();
        input.launcherVisual = &context.replayLauncherVisualScratch;
        context.replayRuntime.CaptureFrame( input );
    }
};

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
            const SkullbonezCore::UI::UIRenderContext uiRender = { &m_systems.assets,
                                                                   &frameRenderResources,
                                                                   &frameRenderCommands,
                                                                   &frameRenderDiagnostics };
            frameRenderDiagnostics.ResetFrameDrawCalls();

            PROFILE_BEGIN( "Frame/Input" );
            TickInteractionAutomationBeforeInput();
            ProcessInputFrame( m_inputRouter,
                               m_config,
                               m_launchOptions,
                               m_applicationExit,
                               m_defaultCinematicRender,
                               m_renderDefaults,
                               m_startup,
                               m_diagnosticsRuntime,
                               m_runtimeSettings,
                               m_timers,
                               m_systems,
                               m_interaction,
                               m_camera,
                               m_attachedCamera,
                               m_simulation,
                               m_replayRuntime,
                               m_contactAudio,
                               m_UI,
                               m_debug,
                               m_graphicsStress,
                               m_runtimeTools,
                               m_physicsDebugVisualizer,
                               m_runtimeViewModel,
                               m_renderBackendView,
                               m_renderer,
                               m_sceneController,
                               sPerfPass );
            m_liveStyle.Tick(
                SceneRuntimeStyleContext{ m_launchOptions,
                                          m_sceneController.State(),
                                          m_sceneController.Browser(),
                                          m_sceneController.Models(),
                                          m_sceneController.Entities(),
                                          m_systems.assets,
                                          ActiveSceneCinematicConfig( m_sceneController.State(), m_config ),
                                          m_defaultCinematicRender } );
            PROFILE_END( "Frame/Input" );

            m_sceneController.Models().BeginCollisionVisualFrame();
            {
                RuntimeAllocation::RuntimeAllocationScope allocationScope(
                    RuntimeAllocation::RuntimeAllocationPhase::Physics );
                TickPhysics( secondsPerFrame );
            }

            ExecutePostPhysicsVisualizationContext postPhysicsVisualizationContext{ m_debug,
                                                                                    m_sceneController.Models(),
                                                                                    m_broadphaseVisualizer,
                                                                                    m_collisionVisualizer,
                                                                                    m_physicsDebugVisualizer };
            TickExecutePostPhysicsVisualizers(
                postPhysicsVisualizationContext,
                secondsPerFrame,
                [this]( const SpatialGrid::ActiveCell* activeCells, int activeCellCount )
                { m_sceneController.UpdateRequiredBroadphaseXCells( activeCells, activeCellCount ); },
                [this]() { m_sceneController.UpdateRequiredContacts( m_config.contactEpsilon ); } );

            // Concept: graphics stress is render/runtime churn, not UI command
            // processing. Tick it once per rendered frame so headless and
            // overnight launches keep mutating DX12 state even when the UI
            // command panel is not producing control messages.
            RunGraphicsStressActions( frameRenderDiagnostics );

            if ( m_runtimeSettings.isPipelineSyncEnabled )
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

            RuntimeRenderModelFrameView renderModels =
                m_renderer.BuildModelFrameView( m_sceneController.Models(), m_sceneController.Physics() );

            PROFILE_BEGIN( "Frame/Render" );
            {
                RuntimeAllocation::RuntimeAllocationScope allocationScope(
                    RuntimeAllocation::RuntimeAllocationPhase::Render );
                DRAW_CALL_TRACE_SCOPE( frameRenderDiagnostics, "Frame/Render" );
                Render( renderModels );
            }
            PROFILE_END( "Frame/Render" );

            ExecuteUiTextFrameContext uiTextFrameContext{ m_renderer,
                                                          frameRenderDiagnostics,
                                                          uiRender,
                                                          renderModels,
                                                          m_diagnosticsRuntime,
                                                          m_replayRuntime,
                                                          m_timers,
                                                          m_debug,
                                                          m_sceneController.State(),
                                                          m_runtimeSettings,
                                                          m_config,
                                                          m_sceneController.World(),
                                                          m_runtimeTools,
                                                          m_UI,
                                                          m_inputRouter.RuntimeContext(),
                                                          m_camera,
                                                          m_runtimeViewModel,
                                                          m_sceneController,
                                                          m_systems,
                                                          m_launchOptions,
                                                          RuntimeCameraModeEnabledMask( m_sceneController ),
                                                          m_camera.mode == RunCameraMode::Attach
                                                              ? m_attachedCamera.ModeLabel()
                                                              : RunCameraModeLabel( m_camera.mode ),
                                                          m_runtimeTools.LauncherFireModeLabel(),
                                                          RunCameraModeUsesLauncher( m_camera.mode ),
                                                          secondsPerFrame };
            RenderExecuteUiTextFrame( uiTextFrameContext, [this]() { RefreshRuntimeViewModel(); } );

            PROFILE_BEGIN( "Frame/PostDraw/LiveStyleCapture" );
            {
                RuntimeAllocation::RuntimeAllocationScope allocationScope(
                    RuntimeAllocation::RuntimeAllocationPhase::Capture );
                TickLiveStyleControlCapture();
            }
            PROFILE_END( "Frame/PostDraw/LiveStyleCapture" );

            PROFILE_BEGIN( "Frame/PostDraw/InteractionAutomation" );
            TickInteractionAutomationAfterRender();
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

            m_diagnosticsRuntime.TickPerfLog( RuntimePerfTickContext{ sPerfPass + 1,
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


void Run::TickPhysics( double secondsPerFrame )
{
    if ( m_replayRuntime.IsScrubPaused() )
    {
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
    if ( m_debug.isCrossScenePauseLocked )
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
    const bool canStepPhysics = m_systems.config != nullptr && m_systems.workerPool != nullptr;
    const SimulationTickResult tick = m_simulation.Tick( SimulationTickInput{ secondsPerFrame,
                                                                              policy.physicsTimeScale,
                                                                              m_sceneController.State().isSceneMode,
                                                                              m_sceneController.State().isScenePhysics,
                                                                              m_sceneController.State().isFixedStep,
                                                                              policy.physicsAdvance,
                                                                              stepRequested,
                                                                              canStepPhysics } );
    if ( tick.committedPhysicsTicks > 0 && canStepPhysics )
    {
        PROFILE_BEGIN( "Frame/Physics" );
        // Why: SimulationSystem now returns only a deterministic tick count.
        // Runtime executes the store-owned physics step directly, then applies
        // the remaining model-owned presentation sync as explicit edge work.
        for ( int tickIndex = 0; tickIndex < tick.committedPhysicsTicks; ++tickIndex )
        {
            PROFILE_SCOPED( "Frame/Physics/Step" );
            if ( manipulatorPhysics )
            {
                ApplyMousePickupPhysicsStep();
            }

            m_sceneController.StepPhysics( PHYSICS_FIXED_DT,
                                           *m_systems.config,
                                           physicsWorldForces,
                                           *m_systems.workerPool );

            if ( manipulatorPhysics || replayCapture || contactAudioStep )
            {
                AfterPhysicsStep();
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
                                      m_sceneController.Models(),
                                      m_sceneController.Entities(),
                                      m_systems.assets,
                                      ActiveSceneCinematicConfig( m_sceneController.State(), m_config ),
                                      m_defaultCinematicRender },
            static_cast<float>( secondsPerFrame ) );
    }
}


void Run::AfterPhysicsStep()
{
    RestoreMousePickupAngularVelocity();
    SimulationPostStepPipelineContext context{ m_contactAudio,
                                               m_runtimeSettings,
                                               m_timers,
                                               m_diagnosticsRuntime,
                                               m_sceneController.State(),
                                               m_debug,
                                               m_sceneController.Cameras(),
                                               m_runtimeTools,
                                               m_replayRuntime,
                                               m_replayLauncherVisualScratch,
                                               m_sceneController.World(),
                                               m_sceneController.Models(),
                                               m_sceneController.Physics(),
                                               m_sceneController.Entities() };
    const SimulationPostStepPipelineResult result = SimulationPostStepPipeline::Run( context );
#ifdef _DEBUG
    if ( result.replayCaptured )
    {
        const ReplayRuntime::SceneTimelineResetInput timelineReset = ReplayRuntime::DescribeSceneTimeline(
            m_sceneController,
            m_sceneController.State(),
            m_startup.gameModelCapacity,
            static_cast<uint32_t>( m_launchOptions.generatedObjectTypeOverride ) );
        const ReplayProbeWorld replayWorld{
            m_sceneController.State(),
            m_runtimeSettings,
            m_debug,
            m_runtimeTools,
            m_sceneController,
            m_simulation,
            m_config,
            m_systems,
            m_launchOptions.generatedObjectTypeOverride,
            m_startup.gameModelCapacity,
            m_diagnosticsRuntime,
            m_runtimeTools.MousePickup(),
            NormalizeRuntimeCameraMode( m_camera.mode,
                                        m_sceneController.State().isSceneMode,
                                        RuntimeCameraModeEnabledMask( m_sceneController ) ),
            m_timers.simulationTimer.GetTotalTime(),
            timelineReset,
            ReplayRuntime::SceneTimelineResetOwners{
                m_inputRouter,
                m_interaction,
                &m_sceneController.Cameras(),
                m_sceneController.Terrain().Get(),
                m_camera,
                NormalizeRuntimeCameraMode( m_replayRuntime.Camera().restoreCameraMode,
                                            m_sceneController.State().isSceneMode,
                                            RuntimeCameraModeEnabledMask( m_sceneController ) ),
                m_attachedCamera.State().activeFollow,
                m_camera.director.grabbed } };
        // Why: ReplayRuntime owns probe sequencing and bounded failure state;
        // the application exit latch only preserves that first owned failure
        // while WM_QUIT unwinds the frame loop.
        const ReplayRuntime::ReplayProbeTickResult probeResult = m_replayRuntime.TickProbes( replayWorld );
        if ( !probeResult.status.ok )
        {
            m_applicationExit.RequestOwnedFailure( probeResult.status );
            PostQuitMessage( 0 );
            return;
        }
        if ( probeResult.enterInteractive )
        {
            EnterInteractiveSceneRun();
        }
    }
#endif
}


void Run::EnterInteractiveSceneRun()
{
    m_sceneController.State().isInteractiveRun = true;
    m_sceneController.State().isExitOnComplete = false;
    m_diagnosticsRuntime.Capture().Screenshot().isScreenshotAndExit = false;
}


bool Run::CanSceneAutomationQuit() const
{
    return !m_sceneController.State().isInteractiveRun;
}


void Run::HoldCompletedInteractiveScene()
{
    m_sceneController.State().isTestComplete = true;
    m_sceneController.State().isExitOnComplete = false;
    m_diagnosticsRuntime.Capture().Screenshot().isScreenshotAndExit = false;
    m_camera.autoCycleInterval = -1.0f;
    m_camera.autoCycleAccum = 0.0f;
}


bool Run::TickScreenshots()
{
    PROFILE_BEGIN( "Frame/PostDraw/Screenshots" );
    if ( m_debug.isCrossScenePauseLocked && !m_inputRouter.RuntimeSnapshot().frameInput.stepHeld )
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
        LogSceneFinished( "screenshot_and_exit" );
    }
    else if ( result.completion == RuntimeCaptureCompletion::Screenshot )
    {
        LogSceneFinished( "screenshot" );
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
                                                                         sPerfPass,
                                                                         m_sceneController.State().isInteractiveRun );
        const bool advanced = request.HasLoad() && m_sceneController
                                                       .Load( request,
                                                              m_config,
                                                              m_launchOptions,
                                                              m_defaultCinematicRender,
                                                              m_startup,
                                                              m_diagnosticsRuntime,
                                                              m_runtimeSettings,
                                                              m_timers,
                                                              m_systems.assets,
                                                              *m_systems.workerPool,
                                                              *m_systems.window,
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
                                                              m_renderer,
                                                              sPerfPass )
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
        HoldCompletedInteractiveScene();
        break;
    case RuntimeCaptureAutomation::None:
        break;
    }

    return result.restartFrame;
}


void Run::TickAutoCycle()
{
    if ( m_debug.isCrossScenePauseLocked && !m_inputRouter.RuntimeSnapshot().frameInput.stepHeld )
    {
        return;
    }

    const RuntimeCaptureResult result =
        m_diagnosticsRuntime.Capture().TickAutoCycle( m_sceneController.State().isSceneMode,
                                                      m_sceneController.State().isInteractiveRun,
                                                      m_sceneController.Models().SceneEntityCount(),
                                                      m_camera.autoCycleInterval,
                                                      m_camera.autoCycleAccum,
                                                      m_camera.autoCycleShotsTaken,
                                                      m_camera.trackBallIndex,
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
    LogSceneFinished( "auto_cycle" );
#endif

    if ( result.automation == RuntimeCaptureAutomation::Quit )
    {
        PostQuitMessage( 0 );
    }
    else if ( result.automation == RuntimeCaptureAutomation::HoldInteractive )
    {
        HoldCompletedInteractiveScene();
    }
}


bool Run::TickSceneAdvance()
{
    const auto executeSceneLoadRequest = [this]( const SceneLoadRequest& request )
    {
        return request.HasLoad() && m_sceneController
                                        .Load( request,
                                               m_config,
                                               m_launchOptions,
                                               m_defaultCinematicRender,
                                               m_startup,
                                               m_diagnosticsRuntime,
                                               m_runtimeSettings,
                                               m_timers,
                                               m_systems.assets,
                                               *m_systems.workerPool,
                                               *m_systems.window,
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
                                               m_renderer,
                                               sPerfPass )
                                        .ok;
    };
    const bool sceneProceedAllowed =
        !m_debug.isCrossScenePauseLocked || m_inputRouter.RuntimeSnapshot().frameInput.stepHeld;
    if ( !sceneProceedAllowed )
    {
        return false;
    }

    ++m_sceneController.State().currentFrame;

    const std::vector<RunRequiredContactState>& requiredContacts = m_sceneController.RequiredContacts();
    const std::vector<RunRequiredBroadphaseXCellsState>& requiredBroadphaseXCells =
        m_sceneController.RequiredBroadphaseXCells();
    const bool hasRequiredContactGate = !requiredContacts.empty();
    const bool hasRequiredBroadphaseGate = !requiredBroadphaseXCells.empty();
    const bool hasRequiredSceneGate = hasRequiredContactGate || hasRequiredBroadphaseGate;
    const bool requiredContactsComplete = m_sceneController.RequiredContactsComplete();
    const bool requiredBroadphaseComplete = m_sceneController.RequiredBroadphaseXCellsComplete();
    const bool requiredSceneComplete = requiredContactsComplete && requiredBroadphaseComplete;
    if ( hasRequiredSceneGate && requiredSceneComplete && !m_sceneController.State().isTestComplete )
    {
#ifdef _DEBUG
        LogSceneFinished( "required_scene_gates" );
#endif
        if ( m_sceneController.State().isExitOnComplete && CanSceneAutomationQuit() )
        {
            if ( !executeSceneLoadRequest(
                     m_sceneController.AdvanceScene( m_diagnosticsRuntime.PerfTestActive(),
                                                     sPerfPass,
                                                     m_sceneController.State().isInteractiveRun ) ) )
            {
                PostQuitMessage( 0 );
            }
            return true;
        }

        if ( CanSceneAutomationQuit() )
        {
            m_sceneController.State().isTestComplete = true;
        }
        else
        {
            HoldCompletedInteractiveScene();
        }
    }

    // Check if target frame count is reached (skip if screenshot auto-exit is still pending)
    if ( m_sceneController.State().targetFrameCount > 0 &&
         !m_diagnosticsRuntime.Capture().Screenshot().isScreenshotSaved )
    {
        if ( m_sceneController.State().currentFrame >= m_sceneController.State().targetFrameCount )
        {
            const bool frameCountCompletesScene = !hasRequiredSceneGate || requiredSceneComplete;
#ifdef _DEBUG
            if ( !m_sceneController.State().isTestComplete &&
                 ( frameCountCompletesScene ||
                   m_sceneController.State().currentFrame == m_sceneController.State().targetFrameCount ) )
            {
                LogSceneFinished( frameCountCompletesScene ? "frame_count" : "required_scene_gates_missing" );
                if ( !frameCountCompletesScene )
                {
                    for ( const RunRequiredContactState& contact : requiredContacts )
                    {
                        if ( contact.bodyA < 0 || contact.bodyB < 0 || !contact.touched )
                        {
                            fprintf( stderr,
                                     "[scene] required_contact missing: %s <-> %s\n",
                                     contact.nameA,
                                     contact.nameB );
                        }
                    }
                    for ( const RunRequiredBroadphaseXCellsState& cells : requiredBroadphaseXCells )
                    {
                        if ( !cells.activated )
                        {
                            fprintf( stderr,
                                     "[scene] required_broadphase_x_cells missing: x %d..%d y %d z %d first_missing=%d "
                                     "active_cells=%d observed_x=%s%d..%d\n",
                                     cells.minCellX,
                                     cells.maxCellX,
                                     cells.cellY,
                                     cells.cellZ,
                                     cells.lastMissingCellX,
                                     cells.lastActiveCellCount,
                                     cells.hasObservedXRange ? "" : "none ",
                                     cells.lastObservedMinX,
                                     cells.lastObservedMaxX );
                        }
                    }
                }
            }
#endif
            if ( !frameCountCompletesScene )
            {
                return false;
            }

            if ( m_sceneController.State().isExitOnComplete && CanSceneAutomationQuit() )
            {
                if ( !executeSceneLoadRequest(
                         m_sceneController.AdvanceScene( m_diagnosticsRuntime.PerfTestActive(),
                                                         sPerfPass,
                                                         m_sceneController.State().isInteractiveRun ) ) )
                {
                    PostQuitMessage( 0 );
                }
                return true;
            }
            else
            {
                if ( frameCountCompletesScene && CanSceneAutomationQuit() )
                {
                    m_sceneController.State().isTestComplete = true;
                }
                else if ( frameCountCompletesScene )
                {
                    HoldCompletedInteractiveScene();
                }
            }
        }
    }

    // Generated demo mode: restart every 20s to keep the sandbox moving indefinitely.
    if ( !m_sceneController.State().isSceneMode &&
         !RunCameraModeUsesManualControls( m_camera.mode,
                                           m_attachedCamera.State().activeFollow,
                                           m_camera.director.grabbed ) &&
         m_timers.simulationTimer.GetTimeSinceLastStart() > 20.0 )
    {
        const SbResult loadResult =
            m_sceneController.Load( SceneLoadRequest::Load( m_sceneController.State().currentSceneIndex,
                                                            m_sceneController.State().isInteractiveRun,
                                                            m_sceneController.State().isInteractiveRun,
                                                            m_sceneController.State().isInteractiveRun ),
                                    m_config,
                                    m_launchOptions,
                                    m_defaultCinematicRender,
                                    m_startup,
                                    m_diagnosticsRuntime,
                                    m_runtimeSettings,
                                    m_timers,
                                    m_systems.assets,
                                    *m_systems.workerPool,
                                    *m_systems.window,
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
                                    m_renderer,
                                    sPerfPass );
        if ( !loadResult.ok )
        {
            return false;
        }
        m_timers.simulationTimer.StartTimer();
        return true;
    }

    // Perf-log scenes without an explicit frame count still use a timed pass duration.
    if ( m_diagnosticsRuntime.PerfTestActive() && m_sceneController.State().targetFrameCount <= 0 &&
         m_timers.simulationTimer.GetTimeSinceLastStart() > PERF_TEST_PASS_SECONDS )
    {
#ifdef _DEBUG
        LogSceneFinished( "perf_duration" );
#endif
        if ( !executeSceneLoadRequest( m_sceneController.AdvanceScene( m_diagnosticsRuntime.PerfTestActive(),
                                                                       sPerfPass,
                                                                       m_sceneController.State().isInteractiveRun ) ) )
        {
            if ( CanSceneAutomationQuit() )
            {
                PostQuitMessage( 0 );
            }
            else
            {
                HoldCompletedInteractiveScene();
            }
        }
        return true;
    }

    return false;
}


void Run::UpdateLogic( float simulationDt, float cameraDt )
{
    // Auto-cycle
    if ( m_sceneController.State().isSceneMode && m_camera.autoCycleInterval > 0.0f )
    {
        m_camera.autoCycleAccum += simulationDt;
    }

    // Camera controls are presentation-time behavior, not simulation-time
    // behavior. Keyboard travel is velocity-based, so it consumes unscaled real
    // frame time. Mouse look consumes a per-frame cursor delta, so using live dt
    // would make sensitivity vary with FPS; the fixed reference preserves the
    // existing 60 Hz tuning while making the result frame-rate independent.
    const EngineConfig& cfg = m_config;
    const bool attachedOrbitOwnsCamera = RunCameraModeIsAttached( m_camera.mode ) &&
                                         m_attachedCamera.State().activeFollow &&
                                         m_attachedCamera.State().submode != AttachedCameraSubmode::RagdollEyes;
    InputController::ApplyCameraMovement(
        m_camera,
        m_sceneController.Cameras(),
        *m_sceneController.Terrain().Get(),
        RuntimeCameraMovementInput{ cameraDt * cfg.keySpeed,
                                    CAMERA_MOUSE_REFERENCE_DT * cfg.mouseSensitivity,
                                    cfg.minCameraHeight,
                                    cfg.maxCameraHeight,
                                    attachedOrbitOwnsCamera,
                                    RunCameraModeUsesFlyControls( m_camera.mode,
                                                                  m_attachedCamera.State().activeFollow,
                                                                  m_camera.director.grabbed ),
                                    m_runtimeTools.Editor().editorModeEnabled,
                                    m_runtimeTools.Editor().viewportLookActive,
                                    RunCameraModeUsesManualControls( m_camera.mode,
                                                                     m_attachedCamera.State().activeFollow,
                                                                     m_camera.director.grabbed ),
                                    m_sceneController.State().isSceneMode } );
    if ( RunCameraModeIsAttached( m_camera.mode ) )
    {
        const float orbitYawDelta =
            static_cast<float>( m_camera.input.xMove ) * CAMERA_MOUSE_REFERENCE_DT * m_config.mouseSensitivity;
        const float orbitPitchDelta =
            static_cast<float>( m_camera.input.yMove ) * CAMERA_MOUSE_REFERENCE_DT * m_config.mouseSensitivity;
        (void)m_attachedCamera.TickFollow( m_sceneController.Models(),
                                           m_sceneController.Cameras(),
                                           orbitYawDelta,
                                           orbitPitchDelta );
    }
    DemoDirectorPlayback::Tick(
        m_camera,
        m_sceneController.Cameras(),
        m_replayRuntime.Prediction(),
        SceneRuntimeStyleContext{ m_launchOptions,
                                  m_sceneController.State(),
                                  m_sceneController.Browser(),
                                  m_sceneController.Models(),
                                  m_sceneController.Entities(),
                                  m_systems.assets,
                                  ActiveSceneCinematicConfig( m_sceneController.State(), m_config ),
                                  m_defaultCinematicRender },
        cameraDt );

    UpdateWaterHeightControls( simulationDt );

    // Tween speed is also presentation-time behavior. The selected destination
    // camera can still track moving scene objects, but the interpolation rate
    // itself should be stable in real seconds instead of following time_scale.
    m_sceneController.Cameras().SetTweenSpeed( cfg.cameraTweenRate * cameraDt );
}


void Run::UpdateWaterHeightControls( float dt )
{
    const bool downNow = m_inputRouter.RuntimeSnapshot().pageDown;
    const bool upNow = m_inputRouter.RuntimeSnapshot().pageUp;
    if ( downNow == upNow )
    {
        return;
    }

    const float direction = upNow ? 1.0f : -1.0f;
    const float height =
        m_sceneController.World().GetFluidSurfaceHeight() + direction * WATER_HEIGHT_CONTROL_SPEED * dt;
    m_sceneController.World().SetFluidSurfaceHeight( height );
}
