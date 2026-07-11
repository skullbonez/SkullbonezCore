/*
File: SkullbonezSource/Runtime/Run.cpp
Purpose:
  Coordinates the main game loop and high-level runtime lifecycle.

Mental model:
  Run.cpp coordinates the main game loop and high-level runtime lifecycle. As
  an implementation unit, keep edits anchored on local owner boundaries and
  call direction and on the glossary/invariants below.

Glossary:
  FBO (Framebuffer Object): Engine shorthand for an off-screen render target
  exposed through the renderer abstraction.
  Lane R result: Recoverable scene/load or renderer-drain failure reported as
    an SbResult so the owning boundary can stop before unsafe mutation.
  Probe failure: CLI validation failure reported as bounded result/report data
    so automation exits nonzero without throwing through the frame loop.

Invariants:
  - Backend-owned render resources must be released while the renderer backend
    is still alive, after a GPU flush, and in the explicit release order below.

Related:
  - SkullbonezSource/Runtime/Run.h
  - SkullbonezSource/Runtime/RunRender.cpp
  - Agentic/Reference/runtime-reference.md
  - Agentic/Reference/comment-style-guide.md
*/
#include "Run.h"
#include "RuntimeCameraMode.h"
#include "InputFrame.h"
#include "WindowConstants.h"
#include "Replay/ReplayOverlayLayout.h"
#include "Replay/ReplayRestoreService.h"
#include "Replay/ReplayRuntimeOwnerViews.h"
#include "Replay/ReplayV2Artifact.h"
#include "RuntimeFileWriter.h"
#include "Allocation/RuntimeAllocationTracker.h"
#include "Allocation/RuntimeReserveAllocator.h"
#include "Scene/SceneRuntimeLoad.h"
#include "../Scene/SceneSnapshotWriter.h"
#include "../Core/FatalError.h"
#include "../Core/Log.h"
#include "../Physics/PhysicsTimestep.h"
#include "../Rendering/IRenderDiagnostics.h"
#include "../World/Terrain.h"

#include <cmath>
#include <cstdio>
#include <cstring>

using namespace SkullbonezCore::Basics;
using namespace SkullbonezCore::Math::CollisionDetection;
using namespace SkullbonezCore::Math::Orientation;
using namespace SkullbonezCore::Math::Transformation;
using namespace SkullbonezCore::Physics;
using SkullbonezCore::Environment::WorldEnvironment;
using SkullbonezCore::GameObjects::SceneSaveRequest;
using SkullbonezCore::GameObjects::SceneSaveView;
using SkullbonezCore::GameObjects::SceneSnapshotWriter;
using SkullbonezCore::Geometry::SkyBox;
using SkullbonezCore::Geometry::Terrain;
using SkullbonezCore::Geometry::XZBounds;
using SkullbonezCore::UI::InGameUITab;
using namespace SkullbonezCore::Basics::ReplayOverlay;
namespace RuntimeAllocation = SkullbonezCore::Runtime::Allocation;

namespace
{
constexpr std::size_t REPLAY_LAUNCHER_LASER_SHOT_CAPACITY = 32;

SkullbonezCore::Environment::CameraMovementSettings BuildCameraMovementSettings( const EngineConfig& cfg )
{
    SkullbonezCore::Environment::CameraMovementSettings settings;
    settings.minViewMag = cfg.minViewMag;
    settings.maxViewMag = cfg.maxViewMag;
    settings.minCameraHeight = cfg.minCameraHeight;
    settings.cameraCollisionThreshold = cfg.cameraCollisionThreshold;
    return settings;
}


// Concept: these source-local helpers keep startup policy grouping visible
// without turning launch-only details back into `Run` public or private methods.
// They mutate the owner references that `Run` passes to them synchronously from
// `ApplyStartupOverrides()`, preserving the original command-line apply order.
void ApplyRuntimeLaunchPolicy( const RunLaunchOptions& launch,
                               RunLaunchOptions& target,
                               RunRuntimeSettings& runtimeSettings,
                               SkullbonezCore::GameObjects::GameModelCollection& models,
                               SkullbonezCore::Runtime::Audio::ContactAudioService& contactAudio )
{
    target.allocationGuardMode = launch.allocationGuardMode;
    if ( RuntimeAllocation::GetRuntimeAllocationGuardMode() != launch.allocationGuardMode )
    {
        RuntimeAllocation::SetRuntimeAllocationGuardMode( launch.allocationGuardMode );
    }
    if ( launch.timeScaleOverride > 0.0f )
    {
        target.timeScaleOverride = launch.timeScaleOverride;
    }
    if ( launch.fixedStep )
    {
        target.fixedStep = true;
    }
    if ( launch.seedOverride > 0 )
    {
        target.seedOverride = launch.seedOverride;
    }
    if ( launch.noWater )
    {
        target.noWater = true;
    }
    if ( launch.noSleep )
    {
        target.noSleep = true;
        runtimeSettings.isPhysicsSleepEnabled = false;
        models.SetPhysicsSleepEnabled( false );
    }
    if ( launch.noContactAudio )
    {
        target.noContactAudio = true;
        contactAudio.SetEnabled( false );
    }
    if ( launch.hasTornadoOverride )
    {
        target.hasTornadoOverride = true;
        target.tornadoEnabled = launch.tornadoEnabled;
        runtimeSettings.tornadoField.enabled = launch.tornadoEnabled;
        if ( runtimeSettings.tornadoVisual.autoEnableWithTornado )
        {
            runtimeSettings.tornadoVisual.enabled = launch.tornadoEnabled;
        }
        runtimeSettings.ApplyTornadoPhysics( models );
    }
    if ( launch.tornadoVectors )
    {
        target.tornadoVectors = true;
        runtimeSettings.tornadoField.visualizeVelocityField = true;
        runtimeSettings.ApplyTornadoPhysics( models );
    }
    if ( launch.hasCinematicRenderingOverride )
    {
        target.hasCinematicRenderingOverride = true;
        target.cinematicRendering = launch.cinematicRendering;
    }
    if ( launch.hasCinematicShadowsOverride )
    {
        target.hasCinematicShadowsOverride = true;
        target.cinematicShadows = launch.cinematicShadows;
    }
    if ( launch.demoHeroStyle )
    {
        target.demoHeroStyle = true;
    }
    target.dumpTextureAssets = launch.dumpTextureAssets;
    if ( launch.interactiveSceneRun )
    {
        target.interactiveSceneRun = true;
    }
    if ( launch.frameCountOverride > 0 )
    {
        target.frameCountOverride = (std::max)( 1, launch.frameCountOverride );
    }
}


void ApplyStressLaunchPolicy( const RunLaunchOptions& launch,
                              RunLaunchOptions& target,
                              GraphicsStressController& graphicsStress )
{
    if ( launch.uiStress )
    {
        target.uiStress = true;
        target.uiStressSeed = launch.uiStressSeed > 0 ? launch.uiStressSeed : 0x7F4A7C15u;
        target.uiStressActions = std::clamp( launch.uiStressActions, 1, 32 );
    }
    if ( !launch.graphicsStress )
    {
        return;
    }

    const unsigned int resolvedSeed = launch.graphicsStressSeed > 0 ? launch.graphicsStressSeed : 0xC11E2026u;
    target.graphicsStress = true;
    target.graphicsStressSeed = resolvedSeed;
    target.graphicsStressActions = std::clamp( launch.graphicsStressActions, 1, 64 );
    target.graphicsStressSceneIntervalFrames = std::clamp( launch.graphicsStressSceneIntervalFrames, 1, 600 );
    target.graphicsStressMemoryIntervalFrames = std::clamp( launch.graphicsStressMemoryIntervalFrames, 0, 36000 );
    target.interactiveSceneRun = true;

    graphicsStress.Configure( resolvedSeed,
                              target.graphicsStressActions,
                              target.graphicsStressSceneIntervalFrames,
                              target.graphicsStressMemoryIntervalFrames );
}


bool ConfigureStartupReplayRecording( const RunStartupOverrides& overrides,
                                      ReplayRuntime& replayRuntime,
                                      int gameModelCapacity )
{
    if ( !overrides.configureReplayRecording )
    {
        return false;
    }

    // Runtime allocation policy: launcher replay visuals are copied every
    // captured physics tick, so keep their scratch vectors reserved before the
    // replay phase begins.
    ReplayLauncherVisualSample& launcherVisualScratch = replayRuntime.LauncherVisualCaptureScratch();
    launcherVisualScratch.rayLines.reserve( RunRayCastTestState::MAX_LINES );
    launcherVisualScratch.laserShots.reserve( REPLAY_LAUNCHER_LASER_SHOT_CAPACITY );

    const ReplayRuntime::RecordingConfigResult replayConfig =
        replayRuntime.ConfigureRecording( overrides.replayRecordingEnabled,
                                          overrides.replayRetentionSeconds,
                                          overrides.replayHashLogPath,
                                          gameModelCapacity );
    const bool resetScrubberState = replayRuntime.ResetScrubberState();
    if ( replayConfig.presentationStats.enabled )
    {
        printf( "[replay] Capture enabled: retention_seconds=%d retention_frames=%llu checkpoint_interval_frames=%d "
                "solver_retention_frames=%llu solver_checkpoint_interval_frames=%d event_capacity=%llu%s%s%s%s\n",
                replayConfig.presentationConfig.retentionSeconds,
                static_cast<unsigned long long>( replayConfig.presentationStats.sampleCapacity ),
                replayConfig.presentationConfig.checkpointIntervalFrames,
                static_cast<unsigned long long>( replayConfig.solverStats.sampleCapacity ),
                replayConfig.solverConfig.checkpointIntervalFrames,
                static_cast<unsigned long long>( replayConfig.eventStats.eventCapacity ),
                replayConfig.presentationConfig.hashLogPath.empty() ? "" : " hash_log=",
                replayConfig.presentationConfig.hashLogPath.empty()
                    ? ""
                    : replayConfig.presentationConfig.hashLogPath.c_str(),
                replayConfig.solverConfig.hashLogPath.empty() ? "" : " solver_hash_log=",
                replayConfig.solverConfig.hashLogPath.empty() ? "" : replayConfig.solverConfig.hashLogPath.c_str() );
    }
    return resetScrubberState;
}


#ifdef _DEBUG
void ApplyReplayProbeStartup( const RunStartupOverrides& overrides, RunReplayProbeState& probes )
{
    if ( overrides.replayScrubProbe )
    {
        probes.scrub.enabled = true;
        probes.scrub.completed = false;
        probes.scrub.normalized = std::clamp( overrides.replayScrubProbeNormalized, 0.0f, 0.99f );
        printf( "[replay] Scrub probe enabled: normalized=%.3f\n", probes.scrub.normalized );
    }
    if ( overrides.replayRestoreProbe )
    {
        probes.restore.enabled = true;
        probes.restore.completed = false;
        probes.restore.normalized = std::clamp( overrides.replayRestoreProbeNormalized, 0.0f, 0.99f );
        printf( "[replay] Restore probe enabled: normalized=%.3f\n", probes.restore.normalized );
    }
    if ( overrides.replaySaveProbe )
    {
        if ( !overrides.replaySaveProbePath || overrides.replaySaveProbePath[0] == '\0' )
        {
            probes.RecordFailure( SbResult::Failure( "ReplayProbe", "replay save probe requires an output path" ) );
            return;
        }

        probes.save.enabled = true;
        probes.save.completed = false;
        strcpy_s( probes.save.path, sizeof( probes.save.path ), overrides.replaySaveProbePath );
        printf( "[replay] Save probe enabled: path=%s\n", probes.save.path );
    }
}
#endif


void ApplyStartupPresentationPolicy( const RunStartupOverrides& overrides,
                                     RunLaunchOptions& launchOptions,
                                     RunDebugState& debug,
                                     SkullbonezCore::UI::InGameUI& ui )
{
    const RunLaunchOptions& launch = overrides.launch;
    if ( overrides.hasInitialOverlayMode )
    {
        debug.overlayMode = overrides.initialOverlayMode;
        if ( overrides.initialOverlayMode != OverlayMode::None )
        {
            ui.SetVisible( true );
        }
        switch ( overrides.initialOverlayMode )
        {
        case OverlayMode::SceneStats:
            ui.SetActiveTab( InGameUITab::Scene );
            break;
        case OverlayMode::Keys:
            ui.SetActiveTab( InGameUITab::Keys );
            break;
        case OverlayMode::BarsNormalized:
        case OverlayMode::BarsAbsolute:
        case OverlayMode::Timers:
            ui.SetActiveTab( InGameUITab::Profiler );
            break;
        default:
            break;
        }
    }
    if ( overrides.hideTopText )
    {
        debug.isTopTextHidden = true;
    }
    if ( overrides.showBroadphaseVisualizer )
    {
        debug.isBroadphaseOverlay = true;
    }
    if ( launch.generatedObjectTypeOverride != GeneratedObjectTypeOverride::Mixed )
    {
        launchOptions.generatedObjectTypeOverride = launch.generatedObjectTypeOverride;
    }
    if ( launch.hasPhysicsDebugFlagsOverride )
    {
        launchOptions.hasPhysicsDebugFlagsOverride = true;
        launchOptions.physicsDebugFlagsOverride = launch.physicsDebugFlagsOverride & PHYSICS_DEBUG_ALL;
    }
    if ( launch.hasPhysicsDebugTransparentOverride )
    {
        launchOptions.hasPhysicsDebugTransparentOverride = true;
        launchOptions.physicsDebugTransparentOverride = launch.physicsDebugTransparentOverride;
    }
    if ( launch.hasPhysicsDebugAlphaOverride )
    {
        launchOptions.hasPhysicsDebugAlphaOverride = true;
        launchOptions.physicsDebugAlphaOverride =
            (std::max)( 0.05f, (std::min)( launch.physicsDebugAlphaOverride, 1.0f ) );
    }
    if ( launch.hasPhysicsDebugContactLingerOverride )
    {
        launchOptions.hasPhysicsDebugContactLingerOverride = true;
        launchOptions.physicsDebugContactLingerOverride =
            (std::max)( 0.0f, (std::min)( launch.physicsDebugContactLingerOverride, 5.0f ) );
    }
}


#ifdef _DEBUG
void ApplyStartupDiagnosticsPolicy( const RunStartupOverrides& overrides,
                                    DiagnosticsRuntime& diagnosticsRuntime,
                                    SkullbonezCore::GameObjects::GameModelCollection& models )
{
    if ( overrides.physicsRegressionLogPath && overrides.physicsRegressionLogPath[0] != '\0' )
    {
        diagnosticsRuntime.SetPhysicsRegressionLogOverride( overrides.physicsRegressionLogPath );
    }
    if ( overrides.physicsCollisionTimeLogPath && overrides.physicsCollisionTimeLogPath[0] != '\0' )
    {
        diagnosticsRuntime.SetPhysicsCollisionTimeLogOverride( overrides.physicsCollisionTimeLogPath );
    }
    if ( overrides.physicsDiagnosticsPath && overrides.physicsDiagnosticsPath[0] != '\0' )
    {
        diagnosticsRuntime.SetPhysicsDiagnosticsPath( models,
                                                      overrides.physicsDiagnosticsPath,
                                                      overrides.physicsDiagnosticsFixedStepForced );
    }
}
#endif

} // namespace


void RunRuntimeSettings::ApplyStartupConfig( const EngineConfig& config )
{
    isVsyncEnabled = config.runtimeRender.vsyncEnabled;
    isPipelineSyncEnabled = config.runtimeRender.forcePipelineSync;
    contactAudioDebugCounters = config.contactAudio.debugCounters;
}


void RunStartupState::ApplyStartupConfig( const EngineConfig& config )
{
    gameModelCapacity = std::clamp( config.gameModelCapacity, 1, MAX_GAME_MODELS );
    workerThreads = config.workerThreads;
}


Run::Run( Window& window,
          std::vector<std::string> sceneQueue,
          EngineConfig& config,
          Threading::WorkerPool& workerPool,
          Profiler* profiler,
          RuntimeRenderBackendView renderBackendView )
    : m_window( window ), m_workerPool( workerPool ), m_config( config ), m_sceneController( std::move( sceneQueue ) ),
      m_renderBackendView( renderBackendView ),
      m_renderer( m_renderBackendView,
                  RenderWorldView{ m_assets,
                                   m_sceneController.Cameras(),
                                   m_sceneController.Terrain(),
                                   window,
                                   m_config,
                                   m_runtimeSettings,
                                   m_sceneController.World(),
                                   m_collisionVisualizer,
                                   m_broadphaseVisualizer,
                                   m_physicsDebugVisualizer,
                                   m_debug,
                                   m_timers,
                                   profiler },
                  RenderSceneView{ m_sceneController, m_sceneController.Browser() },
                  RenderReplayOverlayView{ m_replayRuntime, m_sceneController.Entities() },
                  RenderToolOverlayView{ m_runtimeTools },
                  RenderUiView{ m_UI, m_inputRouter.RuntimeContext(), m_camera } )
{
    const EngineConfig& cfg = m_config;
    m_diagnosticsRuntime.BindProfiler( profiler );
    m_sceneController.Cameras().ApplyMovementSettings( BuildCameraMovementSettings( cfg ) );
    RefreshSceneBrowserList( m_sceneController.Browser() );
    m_sceneController.Models().BindWorkerPool( workerPool );
    m_sceneController.Models().BindSceneEntityStore( m_sceneController.Entities() );
    m_sceneController.Models().ApplyRuntimeConfig( cfg );
    m_runtimeSettings.ApplyStartupConfig( cfg );
    m_renderDefaults.CaptureStartupCinematicBaseline( cfg.cinematicRender );
    m_startup.ApplyStartupConfig( cfg );
}


Run::~Run()
{
    RuntimeAllocation::RuntimeAllocationScope allocationScope( RuntimeAllocation::RuntimeAllocationPhase::Shutdown );
#ifdef _DEBUG
    m_diagnosticsRuntime.EndPhysicsDiagnosticsRun( m_sceneController.State(), "process_end" );
#endif

    if ( m_diagnosticsRuntime.MainMemoryDumpRequested() )
    {
        m_diagnosticsRuntime.WriteMainMemoryDump( m_replayRuntime,
                                                  m_sceneController.Models(),
                                                  m_sceneController.State(),
                                                  "shutdown",
                                                  m_timers.simulationTimer.GetTotalTime() );
    }
    m_diagnosticsRuntime.ClosePerfLog();
    m_replayRuntime.FlushHashLogs();
    if ( m_replayRuntime.IsPresentationEnabled() )
    {
        const ReplayRecorderStats replayStats = m_replayRuntime.PresentationStats();
        printf( "[replay] Captured %llu physics samples, retained %llu/%llu, checkpoints %llu/%llu, "
                "latest_hash=0x%016llX\n",
                static_cast<unsigned long long>( replayStats.totalFramesCaptured ),
                static_cast<unsigned long long>( replayStats.sampleCount ),
                static_cast<unsigned long long>( replayStats.sampleCapacity ),
                static_cast<unsigned long long>( replayStats.checkpointCount ),
                static_cast<unsigned long long>( replayStats.checkpointCapacity ),
                static_cast<unsigned long long>( replayStats.latestStateHash ) );
    }
    if ( m_replayRuntime.SolverStats().enabled )
    {
        const ReplayRecorderStats replayStats = m_replayRuntime.SolverStats();
        printf( "[replay] Solver track captured %llu physics samples, retained %llu/%llu, checkpoints %llu/%llu, "
                "latest_solver_hash=0x%016llX\n",
                static_cast<unsigned long long>( replayStats.totalFramesCaptured ),
                static_cast<unsigned long long>( replayStats.sampleCount ),
                static_cast<unsigned long long>( replayStats.sampleCapacity ),
                static_cast<unsigned long long>( replayStats.checkpointCount ),
                static_cast<unsigned long long>( replayStats.checkpointCapacity ),
                static_cast<unsigned long long>( replayStats.latestStateHash ) );
    }

    // Lifetime: clean up backend-owned render resources while the current
    // backend is still alive. RuntimeRenderer performs the checked drain before
    // its first release so no owner can destroy resources after a failed wait.
    const SbResult releaseResult = m_renderer.ReleaseBackendOwnedRuntimeResources(
        RuntimeRenderer::BackendResourceReleaseContext{ "shutdown_release",
                                                        m_renderBackendView.deviceLifecycle,
                                                        m_renderBackendView.renderResources,
                                                        m_sceneController.Models(),
                                                        m_UI,
                                                        m_runtimeTools } );
    if ( !releaseResult.ok )
    {
        // Lane F: a destructor cannot propagate Lane R to a caller, and letting
        // member destruction continue after an uncertain GPU drain is unsafe.
        SB_FATAL( "Runtime/Run",
                  "Backend resource release could not establish GPU safety. owner=%s reason=%s",
                  releaseResult.error.owner[0] != '\0' ? releaseResult.error.owner : "Rendering/DX12",
                  releaseResult.error.message[0] != '\0' ? releaseResult.error.message : "GPU drain failed" );
    }
}


void Run::ApplyStartupOverrides( const RunStartupOverrides& overrides )
{
    // Why: Runtime/Init owns CLI parsing, but Run owns the live side effects
    // needed to make those startup policies active. Keep the public boundary as
    // one launch packet and preserve the old setter order because several
    // options update both reusable launch policy and already-constructed
    // runtime services.
    const RunLaunchOptions& launch = overrides.launch;

    ApplyRuntimeLaunchPolicy( launch, m_launchOptions, m_runtimeSettings, m_sceneController.Models(), m_contactAudio );
    if ( overrides.liveStyleControlDirectory && overrides.liveStyleControlDirectory[0] != '\0' )
    {
        if ( m_liveStyle.ConfigureDirectory( overrides.liveStyleControlDirectory ) )
        {
            m_launchOptions.interactiveSceneRun = true;
            m_sceneController.EnterInteractiveRun();
            m_diagnosticsRuntime.Capture().DisableAutomationExit();
            m_liveStyle.MarkReady();
        }
    }
    ApplyStressLaunchPolicy( launch, m_launchOptions, m_graphicsStress );
    if ( overrides.mainMemoryDumpPath && overrides.mainMemoryDumpPath[0] != '\0' )
    {
        m_diagnosticsRuntime.SetMainMemoryDumpPath( overrides.mainMemoryDumpPath );
    }
    if ( ConfigureStartupReplayRecording( overrides, m_replayRuntime, m_startup.gameModelCapacity ) )
    {
        m_replayRuntime.ExitInspectionCamera(
            &m_sceneController.Cameras(),
            m_sceneController.Terrain().Get(),
            m_camera,
            NormalizeRuntimeCameraMode( m_replayRuntime.Camera().restoreCameraMode,
                                        m_sceneController.State().isSceneMode,
                                        RuntimeCameraModeEnabledMask( m_sceneController ) ),
            m_attachedCamera.State().activeFollow,
            m_camera.director.grabbed,
            m_interaction,
            m_inputRouter );
    }
    m_replayRuntime.ConfigureStartupWorkflows( ReplayRuntime::ReplayStartupRequest{
        overrides.replayLoadPath,
        overrides.replayLoadProbe,
#ifdef _DEBUG
        overrides.replayRestoreFileProbePath,
        overrides.replayRestoreTargetFileProbePath,
        overrides.replayRestoreBranchFileProbePath,
        overrides.replayRestoreFailureFileProbePath
#endif
    } );
#ifdef _DEBUG
    ApplyReplayProbeStartup( overrides, m_replayRuntime.Probes() );
#endif
    ApplyStartupPresentationPolicy( overrides, m_launchOptions, m_debug, m_UI );
#ifdef _DEBUG
    ApplyStartupDiagnosticsPolicy( overrides, m_diagnosticsRuntime, m_sceneController.Models() );
#endif
}


SbResult Run::SetInteractionAutomation( const char* scriptPath, const char* reportPath )
{
    const SbResult result = ConfigureInteractionAutomation( m_interactionAutomation, scriptPath, reportPath );
    if ( !result.ok )
    {
        (void)WriteInteractionAutomationReport( m_interactionAutomation,
                                                m_sceneController,
                                                m_runtimeTools,
                                                m_replayRuntime,
                                                m_interaction,
                                                m_camera,
                                                m_UI );
    }
    return result;
}


SbResult Run::InteractionAutomationResult() const
{
    return SkullbonezCore::Basics::InteractionAutomationResult( m_interactionAutomation );
}


#ifdef _DEBUG
RunReplayProbeState& ReplayRuntime::Probes()
{
    return m_probes;
}


const RunReplayProbeState& ReplayRuntime::Probes() const
{
    return m_probes;
}
#endif


ReplayRuntime::SceneTimelineResetInput ReplayRuntime::DescribeSceneTimeline( const SceneController& sceneController,
                                                                             const RunSceneState& scene,
                                                                             int gameModelCapacity,
                                                                             uint32_t generatedObjectTypeOverride )
{
    const std::string* scenePath = sceneController.CurrentPath();
    const char* sceneLabel = scenePath && !scenePath->empty() ? scenePath->c_str() : "generated";
    SceneTimelineResetInput replayReset;
    replayReset.sceneLabel = sceneLabel;
    replayReset.isSceneMode = scene.isSceneMode;
    replayReset.modelCount = scene.modelCount;
    replayReset.solverBallCount = scene.solverBallCount;
    replayReset.solverBoxCount = scene.solverBoxCount;
    replayReset.rngSeed = scene.rngSeed;
    replayReset.gameModelCapacity = gameModelCapacity;
    replayReset.generatedObjectTypeOverride = generatedObjectTypeOverride;
    replayReset.hasUiModelCountOverride = sceneController.UIOverrides().modelCountOverride >= 0;
    replayReset.hasUiSolverCountOverride = sceneController.UIOverrides().solverBallCountOverride >= 0 ||
                                           sceneController.UIOverrides().solverBoxCountOverride >= 0;
    return replayReset;
}


bool ReplayRuntime::ApplySolverSampleState( const ReplaySolverSampleRestoreContext& owners,
                                            const ReplaySolverFrameSample& sample,
                                            char* outReason,
                                            std::size_t reasonSize )
{
    return ReplayRestoreService::ApplySolverSampleState( owners, sample, outReason, reasonSize );
}

bool ReplayRuntime::CaptureCurrentSolverSample( const ReplaySolverSampleRestoreContext& owners,
                                                const ReplaySolverFrameSample& reference,
                                                ReplaySolverFrameSample& outSample )
{
    ReplayRecorderConfig config;
    config.enabled = true;
    config.retentionSeconds = 1;
    config.checkpointIntervalFrames = 1;

    ReplaySolverRecorder verifier;
    if ( !verifier.Configure( config ) )
    {
        return false;
    }

    ReplayLauncherVisualSample launcherVisual;
    owners.runtimeTools.BuildReplayLauncherVisualSample( launcherVisual );

    ReplayCaptureInput input;
    input.branch = reference.branch;
    input.eventCursor = reference.eventCursor;
    input.sceneFrame = reference.sceneFrame;
    input.simulationSeconds = reference.simulationSeconds;
    input.physicsDt = reference.physicsDt > 0.0f ? reference.physicsDt : PHYSICS_FIXED_DT;
    input.fixedStep = owners.scene.isFixedStep;
    input.scenePhysicsEnabled = owners.scene.isScenePhysics;
    input.sceneTextEnabled = owners.scene.isSceneText;
    input.waterHidden = owners.debug.isWaterHidden;
    input.terrainHidden = owners.debug.isTerrainHidden;
    input.cameras = &owners.sceneController.Cameras();
    input.world = &owners.sceneController.World();
    input.physics = &owners.physics;
    input.entities = &owners.sceneController.Entities();
    input.bodyStore = &Physics::PhysicsEngineStoreQueries::BodyStore( owners.physics );
    input.colliderStore = &Physics::PhysicsEngineStoreQueries::Colliders( owners.physics );
    input.launcherVisual = &launcherVisual;
    verifier.CaptureFrame( input );

    const ReplaySolverFrameSample* verified = verifier.LatestSample();
    if ( !verified )
    {
        return false;
    }

    outSample = *verified;
    return true;
}


bool ReplayRuntime::CaptureCurrentSolverHash( const ReplaySolverSampleRestoreContext& owners,
                                              const ReplaySolverFrameSample& reference,
                                              uint64_t& outSolverHash,
                                              uint64_t& outPresentationHash,
                                              std::size_t& outBodyCount )
{
    ReplaySolverFrameSample verified;
    if ( !CaptureCurrentSolverSample( owners, reference, verified ) )
    {
        return false;
    }
    outSolverHash = verified.solverHash;
    outPresentationHash = verified.presentationHash;
    outBodyCount = verified.bodies.size();
    return true;
}


bool ReplayRuntime::RestoreSolverSampleAsLive( const ReplayRestoreTransaction& transaction,
                                               const ReplaySolverFrameSample& sample,
                                               char* outReason,
                                               std::size_t reasonSize )
{
    auto writeReason = [outReason, reasonSize]( const char* message )
    {
        if ( outReason && reasonSize > 0 )
        {
            strncpy_s( outReason, reasonSize, message ? message : "restore failed", _TRUNCATE );
        }
    };

    ReplaySolverFrameSample liveBackup;
    if ( !CaptureCurrentSolverSample( transaction.sampleOwners, sample, liveBackup ) )
    {
        writeReason( "failed to capture live replay backup" );
        return false;
    }
    const bool hasLiveBackup = true;

    char applyReason[128] = {};
    if ( !ApplySolverSampleState( transaction.sampleOwners, sample, applyReason, sizeof( applyReason ) ) )
    {
        writeReason( applyReason[0] != '\0' ? applyReason : "restore apply failed" );
        return false;
    }

    uint64_t restoredSolverHash = 0;
    uint64_t restoredPresentationHash = 0;
    std::size_t restoredBodyCount = 0;
    const bool hashCaptured = CaptureCurrentSolverHash( transaction.sampleOwners,
                                                        sample,
                                                        restoredSolverHash,
                                                        restoredPresentationHash,
                                                        restoredBodyCount );
    const bool hashMatched = hashCaptured && restoredSolverHash == sample.solverHash;
    bool fallbackRestored = false;

    if ( !hashMatched && hasLiveBackup )
    {
        char fallbackReason[128] = {};
        fallbackRestored =
            ApplySolverSampleState( transaction.sampleOwners, liveBackup, fallbackReason, sizeof( fallbackReason ) );
    }

#ifdef _DEBUG
    transaction.diagnostics.LogReplayRestoreProbe( transaction.sampleOwners.scene,
                                                   sample,
                                                   restoredSolverHash,
                                                   restoredPresentationHash,
                                                   restoredBodyCount,
                                                   hashCaptured,
                                                   hashMatched,
                                                   !hashMatched && hasLiveBackup,
                                                   fallbackRestored );
#endif

    // Hazard: a recoverable restore failure may return only after the live
    // backup was reapplied. Continuing from a half-restored solver would make
    // later physics output nondeterministic, so rollback failure is Lane F.
    if ( !hashMatched && !fallbackRestored )
    {
        SB_FATAL( "Runtime/ReplayRestore",
                  "Replay restore verification failed and the live backup could not be restored" );
    }

    if ( !hashCaptured )
    {
        writeReason( "restore hash capture failed" );
        return false;
    }
    if ( !hashMatched )
    {
        writeReason( fallbackRestored ? "restore hash mismatch; live state restored"
                                      : "restore hash mismatch; fallback unavailable" );
        return false;
    }

    const uint32_t parentBranchId =
        sample.branch.branchId != 0 ? sample.branch.branchId : ( Branch().branchId != 0 ? Branch().branchId : 1u );
    ReplayBranchInfo restoredBranch;
    restoredBranch.branchId = (std::max)( Branch().branchId, parentBranchId ) + 1u;
    restoredBranch.parentBranchId = parentBranchId;
    restoredBranch.startFrame = 0;
    restoredBranch.sourceFrame = sample.frameIndex;
    restoredBranch.sourceSolverHash = sample.solverHash;
    Branch() = restoredBranch;
    SceneTimelineResetInput reset = transaction.timelineReset;
    reset.preserveBranchMetadata = true;
    ResetSceneTimeline( reset, transaction.timelineOwners );
    RecordEvent( ReplayEventKind::BranchRestore,
                 0,
                 0,
                 static_cast<int32_t>( parentBranchId ),
                 sample.sceneFrame,
                 0,
                 0,
                 sample.solverHash,
                 "hash-verified solver restore" );
    writeReason( "restored hash match" );
    return true;
}


void Run::Initialise()
{
    // Why: timers default to inert storage so Run construction cannot throw
    // before the startup reporter exists. Initialise them at this boundary and
    // return platform counter failures through the normal Lane R process path.
    const SbResult timerStartupResult = m_timers.Initialise();
    if ( !timerStartupResult.ok )
    {
        m_lastSceneLoadResult = timerStartupResult;
        return;
    }

    assert( m_renderBackendView.renderResources && "Run requires render resources before Initialise()" );
    assert( m_renderBackendView.renderCommands && "Run requires render commands before Initialise()" );
    assert( m_renderBackendView.renderDiagnostics && "Run requires render diagnostics before Initialise()" );
    auto& renderResources = *m_renderBackendView.renderResources;
    auto& renderCommands = *m_renderBackendView.renderCommands;
    const SkullbonezCore::Rendering::IRenderDiagnostics& renderDiagnostics = *m_renderBackendView.renderDiagnostics;

    const char* rendererName = renderDiagnostics.GetRendererName();
    char titleText[256];
    sprintf_s( titleText, "%s [%s] -- LOADING!!!", TITLE_TEXT, rendererName );
    m_window.SetTitleText( titleText );
    const EngineConfig& cfg = m_config;

    // Build renderer-owned resources from source asset records.
    const SbResult rebuildResourcesResult = m_renderer.InitialiseProcessResources( renderResources,
                                                                                   renderCommands,
                                                                                   m_config,
                                                                                   m_launchOptions.dumpTextureAssets );
    if ( !rebuildResourcesResult.ok )
    {
        m_lastSceneLoadResult = rebuildResourcesResult;
        return;
    }
    const std::string terrainRawPath = m_assets.RegisterSourceAssetPath( SkullbonezCore::Assets::AssetKind::Terrain,
                                                                         "terrain.raw",
                                                                         cfg.terrainRaw.c_str() );
    std::unique_ptr<Terrain> startupTerrain;
    const SbResult startupTerrainResult = Terrain::TryCreateFromHeightMap( terrainRawPath.c_str(),
                                                                           256,
                                                                           8,
                                                                           15,
                                                                           m_config,
                                                                           m_assets,
                                                                           renderResources,
                                                                           startupTerrain );
    if ( !startupTerrainResult.ok )
    {
        m_lastSceneLoadResult = startupTerrainResult;
        return;
    }
    m_sceneController.Terrain().Replace( std::move( startupTerrain ), false );

    m_sceneController.World() = WorldEnvironment( cfg.fluidHeight, cfg.fluidDensity, cfg.gasDensity, cfg.gravity );
    m_sceneController.World().BindRenderContexts( m_config, m_assets, renderResources );
    XZBounds tb = m_sceneController.Terrain().Get()->GetXZBounds();
    m_sceneController.World().SetTerrainBounds( tb.m_xMin, tb.m_xMax, tb.m_zMin, tb.m_zMax );

    // Why: SDF atlas generation is a startup asset/tooling boundary. Report it
    // as Lane R before scene loading instead of throwing through Run startup.
    const SbResult uiTextResourceResult =
        m_renderer.EnsureUiTextResources( renderResources, m_assets, cfg.window.screenX, cfg.window.screenY );
    if ( !uiTextResourceResult.ok )
    {
        m_lastSceneLoadResult = uiTextResourceResult;
        return;
    }

    m_contactAudio.SetMasterGain( cfg.contactAudio.masterGain );
    m_contactAudio.SetMaxDistanceScale( cfg.contactAudio.maxDistanceScale );
    m_contactAudio.SetRollingLevelDb( cfg.contactAudio.rollingLevelDb );
    m_contactAudio.SetRollingMaxDistance( cfg.contactAudio.rollingMaxDistance );
    m_contactAudio.SetRollingMinSlipSpeed( cfg.contactAudio.rollingMinSlipSpeed );
    m_contactAudio.SetRollingVoicesPerWindow( static_cast<uint32_t>( cfg.contactAudio.rollingVoicesPerWindow ) );
    if ( !m_launchOptions.noContactAudio && cfg.contactAudio.enabled )
    {
        const bool audioReady =
            m_contactAudio.Initialize() &&
            m_contactAudio.LoadContactAudioMap( "SkullbonezData/audio/contact_audio.materials.json" );
        m_contactAudio.SetEnabled( audioReady );
    }
    else
    {
        m_contactAudio.SetEnabled( false );
    }

    m_lastSceneLoadResult = m_sceneController.Load( SceneLoadRequest::Load( 0, false, false, false ),
                                                    m_config,
                                                    m_launchOptions,
                                                    m_renderDefaults.CinematicBaseline(),
                                                    m_startup,
                                                    m_diagnosticsRuntime,
                                                    m_runtimeSettings,
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
                                                    m_renderer );
    if ( !m_lastSceneLoadResult.ok )
    {
        return;
    }

    const ReplayRuntime::SceneTimelineResetInput timelineReset =
        ReplayRuntime::DescribeSceneTimeline( m_sceneController,
                                              m_sceneController.State(),
                                              m_startup.gameModelCapacity,
                                              static_cast<uint32_t>( m_launchOptions.generatedObjectTypeOverride ) );
    ReplayRuntime::SceneTimelineResetOwners timelineOwners{
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
    const ReplayRuntime::ReplayStartupLoadInput loadInput{
        m_timers.simulationTimer.GetTotalTime(),
        &m_sceneController.Cameras(),
        m_runtimeTools.MousePickup(),
        NormalizeRuntimeCameraMode( m_camera.mode,
                                    m_sceneController.State().isSceneMode,
                                    RuntimeCameraModeEnabledMask( m_sceneController ) ),
        timelineOwners };
#ifdef _DEBUG
    const ReplayProbeWorld probeWorld{ m_sceneController.State(),
                                       m_runtimeSettings,
                                       m_debug,
                                       m_runtimeTools,
                                       m_sceneController,
                                       m_simulation,
                                       m_config,
                                       m_assets,
                                       m_workerPool,
                                       m_launchOptions.generatedObjectTypeOverride,
                                       m_startup.gameModelCapacity,
                                       m_diagnosticsRuntime,
                                       m_runtimeTools.MousePickup(),
                                       NormalizeRuntimeCameraMode( m_camera.mode,
                                                                   m_sceneController.State().isSceneMode,
                                                                   RuntimeCameraModeEnabledMask( m_sceneController ) ),
                                       m_timers.simulationTimer.GetTotalTime(),
                                       timelineReset,
                                       timelineOwners };
    const ReplayRuntime::ReplayStartupResult replayStartup =
        m_replayRuntime.RunStartupWorkflows( loadInput, probeWorld );
#else
    const ReplayRuntime::ReplayStartupResult replayStartup = m_replayRuntime.RunStartupWorkflows( loadInput );
#endif
    if ( !replayStartup.status.ok )
    {
        m_lastSceneLoadResult = replayStartup.status;
        return;
    }
    m_skipExecute = replayStartup.skipExecute;
}


const SbResult& Run::LastSceneLoadResult() const
{
    return m_lastSceneLoadResult;
}


SbResult Run::RunSceneLoadOnly( const char* snapshotOutPath )
{
    const int sceneCount = m_sceneController.QueueSize();
    if ( sceneCount <= 0 )
    {
        printf( "[scene-load-only] Exiting because --scene-load-only was requested, but no scenes were queued.\n" );
        fflush( stdout );
        return SbResult::Success();
    }
    if ( !m_lastSceneLoadResult.ok )
    {
        return m_lastSceneLoadResult;
    }

    const bool writeSnapshot = snapshotOutPath && snapshotOutPath[0] != '\0';
    if ( writeSnapshot && sceneCount != 1 )
    {
        return SbResult::Failure( "Runtime/SceneLoadOnly", "--scene-snapshot-out requires exactly one loaded scene." );
    }

    printf( "[scene-load-only] Loaded 1/%d: %s\n",
            sceneCount,
            m_sceneController.PathAt( 0 ).empty() ? "generated" : m_sceneController.PathAt( 0 ).c_str() );
    if ( writeSnapshot )
    {
        // Lifetime: scene-load-only borrows owner arrays only until the
        // synchronous snapshot write completes.
        const auto& joints = m_sceneController.Models().GetPointJointConstraints();
        const SceneSaveView saveView{ m_sceneController.Entities(),
                                      m_sceneController.Models().BodyStore(),
                                      m_sceneController.Models().Colliders(),
                                      joints.data(),
                                      static_cast<int>( joints.size() ),
                                      m_sceneController.World().GetGravity(),
                                      m_sceneController.World().GetFluidSurfaceHeight(),
                                      m_sceneController.World().GetFluidDensity(),
                                      m_sceneController.World().GetMutualGravitySettings() };
        const SceneSaveRequest saveRequest{ snapshotOutPath,
                                            m_sceneController.Cameras().GetCameraTranslation(),
                                            m_sceneController.Cameras().GetCameraView(),
                                            m_sceneController.Cameras().GetCameraUp(),
                                            m_sceneController.State().isScenePhysics,
                                            m_sceneController.State().isSceneText,
                                            m_sceneController.State().isEditableScene,
                                            m_sceneController.State().isFixedStep,
                                            m_debug.isWaterHidden,
                                            m_debug.isTerrainHidden,
                                            m_sceneController.State().hasFlatSlope,
                                            m_sceneController.State().flatBaseY,
                                            m_sceneController.State().flatSlopeX,
                                            m_sceneController.State().flatSlopeZ };
        const SbResult saveResult = SceneSnapshotWriter::Save( saveView, saveRequest );
        if ( !saveResult.ok )
        {
            return saveResult;
        }
        printf( "[scene-load-only] Snapshot written: %s\n", snapshotOutPath );
    }
    for ( int i = 1; i < sceneCount; ++i )
    {
        const SbResult loadResult = m_sceneController.Load( SceneLoadRequest::Load( i, false, false, false ),
                                                            m_config,
                                                            m_launchOptions,
                                                            m_renderDefaults.CinematicBaseline(),
                                                            m_startup,
                                                            m_diagnosticsRuntime,
                                                            m_runtimeSettings,
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
                                                            m_renderer );
        if ( !loadResult.ok )
        {
            return loadResult;
        }
        printf( "[scene-load-only] Loaded %d/%d: %s\n",
                i + 1,
                sceneCount,
                m_sceneController.PathAt( i ).empty() ? "generated" : m_sceneController.PathAt( i ).c_str() );
    }
    // Why: --scene-load-only is expected to close immediately after loading;
    // make that intentional automation end visible in Profile and Debug logs.
    printf( "[scene-load-only] Exiting because --scene-load-only finished loading %d queued scene(s) without running "
            "frames.\n",
            sceneCount );
    fflush( stdout );
    return SbResult::Success();
}
