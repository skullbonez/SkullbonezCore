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
#include "RunInternal.h"
#include "Editor/EditorOverlayTools.h"
#include "Replay/ReplayOverlayLayout.h"
#include "Replay/ReplayRestoreService.h"
#include "Replay/ReplayV2Artifact.h"
#include "RuntimeFileWriter.h"
#include "Allocation/RuntimeAllocationTracker.h"
#include "Allocation/RuntimeReserveAllocator.h"
#include "Scene/SceneRuntimeLoad.h"
#include "../Core/FatalError.h"
#include "../Core/Log.h"
#include "../Physics/PhysicsTimestep.h"

#include <cmath>
#include <cstdio>
#include <cstring>

using namespace SkullbonezCore::Basics;
using namespace SkullbonezCore::Math::CollisionDetection;
using namespace SkullbonezCore::Math::Orientation;
using namespace SkullbonezCore::Math::Transformation;
using namespace SkullbonezCore::Physics;
using namespace SkullbonezCore::Basics::RunInternal;
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
        SyncTornadoRuntimeSettingsToPhysics( models, runtimeSettings );
    }
    if ( launch.tornadoVectors )
    {
        target.tornadoVectors = true;
        runtimeSettings.tornadoField.visualizeVelocityField = true;
        SyncTornadoRuntimeSettingsToPhysics( models, runtimeSettings );
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
                                      ReplayLauncherVisualSample& launcherVisualScratch,
                                      int gameModelCapacity )
{
    if ( !overrides.configureReplayRecording )
    {
        return false;
    }

    // Runtime allocation policy: launcher replay visuals are copied every
    // captured physics tick, so keep their scratch vectors reserved before the
    // replay phase begins.
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


void RunSubsystemState::BindStartupServices( Window& windowOwner,
                                             Threading::WorkerPool& workerPoolOwner,
                                             const EngineConfig& configOwner )
{
    // Lifetime: these are process-start borrows. Runtime/Init owns the native
    // window and worker pool, while Run owns the config reference passed into
    // the constructor.
    window = &windowOwner;
    workerPool = &workerPoolOwner;
    config = &configOwner;
    cameraCollection.ApplyMovementSettings( BuildCameraMovementSettings( configOwner ) );
}


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


RuntimeRendererBindings Run::BuildRuntimeRendererBindings( Profiler* profiler )
{
    // Lifetime: Init resolves the optional profiler once, then Run wires that
    // borrowed diagnostics source into the owners that sample it. Frame code
    // should not reopen the global profiler accessor.
    RuntimeRendererBindings bindings;
    bindings.backend = m_renderBackendView;
    bindings.runtime.systems = &m_systems;
    bindings.runtime.config = &m_config;
    bindings.runtime.launchOptions = &m_launchOptions;
    bindings.runtime.runtimeSettings = &m_runtimeSettings;
    bindings.world.worldEnvironment = &m_cWorldEnvironment;
    bindings.world.collisionVisualizer = &m_collisionVisualizer;
    bindings.world.broadphaseVisualizer = &m_broadphaseVisualizer;
    bindings.world.physicsDebugVisualizer = &m_physicsDebugVisualizer;
    bindings.scene.sceneController = &m_sceneController;
    bindings.scene.sceneBrowser = &m_sceneController.Browser();
    bindings.replayOverlay.replayRuntime = &m_replayRuntime;
    bindings.toolOverlay.tools = &m_runtimeTools;
    bindings.ui.ui = &m_UI;
    bindings.ui.runtimeInput = &m_runtimeInput;
    bindings.ui.camera = &m_camera;
    bindings.ui.runtimeViewModel = &m_runtimeViewModel;
    bindings.diagnostics.debug = &m_debug;
    bindings.diagnostics.timers = &m_timers;
    bindings.diagnostics.profiler = profiler;
    return bindings;
}


// Why: RuntimeRenderer still passes C-style hooks down to a few pass owners.
// Keeping the noncapturing hook lambdas here lets them access Run-owned editor
// overlay behavior without adding another Run.h method or callback-holder type.
Run::Run( Window& window,
          std::vector<std::string> sceneQueue,
          EngineConfig& config,
          Threading::WorkerPool& workerPool,
          Profiler* profiler,
          RuntimeRenderBackendView renderBackendView )
    : m_config( config ), m_sceneController( std::move( sceneQueue ) ), m_sceneCoordinator( m_sceneController ),
      m_renderBackendView( renderBackendView ),
      m_renderer(
          BuildRuntimeRendererBindings( profiler ),
          []( void* user, const char* phase, const char* step )
          {
              if ( Run* run = static_cast<Run*>( user ) )
              {
                  run->LogRenderResourceLifecycleStep( phase, step );
              }
          },
          []( void* user,
              SkullbonezCore::Rendering::IRenderResourceFactory& renderResources,
              SkullbonezCore::Rendering::IRenderCommandContext& renderCommands,
              const Math::Transformation::Matrix4& viewProjection,
              const Math::Vector::Vector3& cameraEye,
              const Math::Vector::Vector3& cameraUp )
          {
              Run* run = static_cast<Run*>( user );
              if ( !run )
              {
                  return;
              }

              RunEditorTracer& tracer = run->m_runtimeTools.EditorTracer();
              tracer.Clear();

              int attachedTargetIndex = -1;
              if ( RunCameraModeIsAttached( run->m_camera.mode ) )
              {
                  int targetIndex = -1;
                  if ( run->TryResolveAttachedCameraTarget( targetIndex ) )
                  {
                      attachedTargetIndex = targetIndex;
                  }
              }

              BuildEditorToolOverlayTrace( { run->m_runtimeTools.Editor(),
                                             run->m_runtimeTools.RayCastTest(),
                                             run->m_runtimeTools.MousePickup(),
                                             run->m_cGameModelCollection,
                                             run->m_cGameModelCollection.BodyStore(),
                                             run->m_cGameModelCollection.Colliders(),
                                             run->m_systems.assets,
                                             tracer },
                                           { run->m_debug.physicsDebugContactLinger,
                                             run->InspectGizmoInteractionActive(),
                                             run->m_inputRouter.DeviceFrame().keys.IsDown( VK_CONTROL ),
                                             attachedTargetIndex,
                                             run->m_attachedCamera.activeFollow } );
              run->RenderReplayPathVisualizer( tracer );
              run->RenderReplayCauseFocusOverlay( tracer );
              run->RenderReplayVelocityEditOverlay( tracer );
              tracer.Render( viewProjection, cameraEye, cameraUp, renderCommands );
              run->m_replayRuntime.RecordReplayTrajectorySubmissionFrame(
                  tracer.ReplaySubmissionStats(),
                  run->SceneState().currentFrame,
                  RuntimeAllocation::RuntimeReserveAllocator::GrowthEventCount() );
              run->m_runtimeTools.Laser().Render( viewProjection,
                                                  cameraEye,
                                                  cameraUp,
                                                  run->m_systems.assets,
                                                  renderResources,
                                                  renderCommands );
          },
          this )
{
    const EngineConfig& cfg = m_config;
    m_diagnosticsRuntime.BindProfiler( profiler );
    m_systems.BindStartupServices( window, workerPool, cfg );
    RefreshRuntimeViewModel();
    RefreshSceneBrowserList( m_sceneController.Browser() );
    m_cGameModelCollection.BindWorkerPool( workerPool );
    m_cGameModelCollection.ApplyRuntimeConfig( cfg );
    m_runtimeSettings.ApplyStartupConfig( cfg );
    m_defaultCinematicRender = cfg.cinematicRender;
    m_startup.ApplyStartupConfig( cfg );
}


RunSceneState& Run::SceneState()
{
    return m_sceneController.State();
}


const RunSceneState& Run::SceneState() const
{
    return m_sceneController.State();
}

void Run::RefreshRuntimeViewModel()
{
    m_runtimeViewModel =
        RuntimeViewModelBuilder::Build( RuntimeViewModelContext{ m_sceneController,
                                                                 m_diagnosticsRuntime.Capture(),
                                                                 m_runtimeSettings,
                                                                 m_cGameModelCollection.GetPhysicsEngine() },
                                        m_contactAudio );
}


Run::~Run()
{
    RuntimeAllocation::RuntimeAllocationScope allocationScope( RuntimeAllocation::RuntimeAllocationPhase::Shutdown );
#ifdef _DEBUG
    EndPhysicsDiagnosticsRun( "process_end" );
#endif

    if ( m_diagnosticsRuntime.MainMemoryDumpRequested() )
    {
        m_diagnosticsRuntime.WriteMainMemoryDump( m_replayRuntime,
                                                  m_cGameModelCollection,
                                                  SceneState(),
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
    const SbResult releaseResult = ReleaseBackendOwnedRenderResources( "shutdown_release" );
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


SbResult Run::ReleaseBackendOwnedRenderResources( const char* phaseName )
{
    SkullbonezCore::Rendering::IRenderDeviceLifecycle* releaseDeviceLifecycle = m_renderBackendView.deviceLifecycle;
    SkullbonezCore::Rendering::IRenderResourceFactory* releaseRenderResources = m_renderBackendView.renderResources;

    return m_renderer.ReleaseBackendOwnedRuntimeResources(
        RuntimeRenderer::BackendResourceReleaseContext{ phaseName,
                                                        releaseDeviceLifecycle,
                                                        releaseRenderResources,
                                                        m_cGameModelCollection,
                                                        m_UI,
                                                        m_runtimeTools } );
}


void Run::DumpTextureAssets( FILE* out ) const
{
    if ( m_systems.textures )
    {
        m_systems.textures->DumpTextureAssets( out );
    }
}


void Run::LogRenderResourceLifecycleStep( const char* phase, const char* step ) const
{
    const SkullbonezCore::Rendering::IRenderDeviceLifecycle* renderLifecycle = m_renderBackendView.deviceLifecycle;
    const bool gfxReady = renderLifecycle != nullptr;
    const int backendWidth = renderLifecycle ? renderLifecycle->GetWidth() : 0;
    const int backendHeight = renderLifecycle ? renderLifecycle->GetHeight() : 0;
    Log().WriteEventf( "render_resource_lifecycle phase=%s step=%s gfx_ready=%d backend_width=%d backend_height=%d "
                       "scene_index=%d load=%d",
                       phase ? phase : "unknown",
                       step ? step : "unknown",
                       gfxReady ? 1 : 0,
                       backendWidth,
                       backendHeight,
                       SceneState().currentSceneIndex,
                       SceneState().loadCount );
}


void Run::ApplyStartupOverrides( const RunStartupOverrides& overrides )
{
    // Why: Runtime/Init owns CLI parsing, but Run owns the live side effects
    // needed to make those startup policies active. Keep the public boundary as
    // one launch packet and preserve the old setter order because several
    // options update both reusable launch policy and already-constructed
    // runtime services.
    const RunLaunchOptions& launch = overrides.launch;

    ApplyRuntimeLaunchPolicy( launch, m_launchOptions, m_runtimeSettings, m_cGameModelCollection, m_contactAudio );
    if ( overrides.liveStyleControlDirectory && overrides.liveStyleControlDirectory[0] != '\0' )
    {
        if ( m_liveStyle.ConfigureDirectory( overrides.liveStyleControlDirectory ) )
        {
            m_launchOptions.interactiveSceneRun = true;
            EnterInteractiveSceneRun();
            m_liveStyle.MarkReady();
        }
    }
    ApplyStressLaunchPolicy( launch, m_launchOptions, m_graphicsStress );
    if ( overrides.mainMemoryDumpPath && overrides.mainMemoryDumpPath[0] != '\0' )
    {
        m_diagnosticsRuntime.SetMainMemoryDumpPath( overrides.mainMemoryDumpPath );
    }
    if ( ConfigureStartupReplayRecording( overrides,
                                          m_replayRuntime,
                                          m_replayLauncherVisualScratch,
                                          m_startup.gameModelCapacity ) )
    {
        ExitReplayInspectionCamera();
    }
#ifdef _DEBUG
    ApplyReplayProbeStartup( overrides, m_replayProbes );
#endif
    ApplyStartupPresentationPolicy( overrides, m_launchOptions, m_debug, m_UI );
#ifdef _DEBUG
    ApplyStartupDiagnosticsPolicy( overrides, m_diagnosticsRuntime, m_cGameModelCollection );
#endif
}


SbResult Run::SetInteractionAutomation( const char* scriptPath, const char* reportPath )
{
    m_interactionAutomation = RunInteractionAutomationState{};
    if ( reportPath && reportPath[0] != '\0' )
    {
        strcpy_s( m_interactionAutomation.reportPath, sizeof( m_interactionAutomation.reportPath ), reportPath );
    }
    else
    {
        strcpy_s( m_interactionAutomation.reportPath,
                  sizeof( m_interactionAutomation.reportPath ),
                  "TestOutput\\interaction\\interaction_report.json" );
    }

    if ( !scriptPath || scriptPath[0] == '\0' )
    {
        m_interactionAutomation.failed = true;
        m_interactionAutomation.finished = true;
        strcpy_s( m_interactionAutomation.failure,
                  sizeof( m_interactionAutomation.failure ),
                  "interaction automation requires a script path" );
        WriteInteractionAutomationReport();
        return SbResult::Failure( "InteractionAutomation", m_interactionAutomation.failure );
    }

    strcpy_s( m_interactionAutomation.scriptPath, sizeof( m_interactionAutomation.scriptPath ), scriptPath );
    m_interactionAutomation.enabled = true;
    printf( "[interaction] Script: %s\n", m_interactionAutomation.scriptPath );
    printf( "[interaction] Report: %s\n", m_interactionAutomation.reportPath );
    return SbResult::Success();
}


SbResult Run::InteractionAutomationResult() const
{
    if ( !m_interactionAutomation.failed )
    {
        return SbResult::Success();
    }
    const char* message =
        m_interactionAutomation.failure[0] != '\0' ? m_interactionAutomation.failure : "interaction automation failed";
    return SbResult::Failure( "InteractionAutomation", message );
}


#ifdef _DEBUG
const RunReplayProbeState& Run::ReplayProbes() const
{
    return m_replayProbes;
}
#endif


bool Run::LoadReplayPresentationArtifact( const char* path, bool activateScrubber )
{
    if ( !path || path[0] == '\0' )
    {
        return false;
    }

    std::vector<ReplayPresentationSample> samples;
    ReplayV2LoadResult result;
    if ( !ReplayV2Artifact::LoadPresentation( path, samples, &result ) || samples.size() < 2 )
    {
        return false;
    }

    m_replayRuntime.LoadedPresentation() = RunLoadedReplayPresentationState{};
    m_replayRuntime.LoadedPresentation().samples.swap( samples );
    m_replayRuntime.LoadedPresentation().enabled = true;
    m_replayRuntime.LoadedPresentation().bodyDictionaryCount = result.bodyDictionaryCount;
    m_replayRuntime.LoadedPresentation().fileBytes = result.fileBytes;
    m_replayRuntime.LoadedPresentation().firstFrame = result.firstFrame;
    m_replayRuntime.LoadedPresentation().lastFrame = result.lastFrame;
    strncpy_s( m_replayRuntime.LoadedPresentation().path,
               sizeof( m_replayRuntime.LoadedPresentation().path ),
               path,
               _TRUNCATE );

    if ( activateScrubber )
    {
        if ( m_replayRuntime.Scrubber().liveAdvanceHeld )
        {
            m_replayRuntime.SetLiveAdvanceHeld( false );
        }
        CancelReplayToolDragState();

        m_replayRuntime.ClearCameraFocusForRestore();
        ExitReplayInspectionCamera();
        m_replayRuntime.ArmLoadedPresentationScrubber( 0.25f, m_timers.simulationTimer.GetTotalTime() );
        SetWorldInteractionOwnerAfterInteractionTransition( WorldInteractionOwner::ReplayScrub,
                                                            InteractionExitReason::EnterReplay );
        if ( m_replayRuntime.ShouldUseInspectionCamera() )
        {
            EnterReplayInspectionCamera();
        }
        else
        {
            ExitReplayInspectionCamera();
        }
    }

    printf( "[replay] Loaded v2 presentation artifact: path=%s samples=%llu bodies=%llu first_frame=%llu "
            "last_frame=%llu bytes=%llu\n",
            m_replayRuntime.LoadedPresentation().path,
            static_cast<unsigned long long>( m_replayRuntime.LoadedPresentation().samples.size() ),
            static_cast<unsigned long long>( m_replayRuntime.LoadedPresentation().bodyDictionaryCount ),
            static_cast<unsigned long long>( m_replayRuntime.LoadedPresentation().firstFrame ),
            static_cast<unsigned long long>( m_replayRuntime.LoadedPresentation().lastFrame ),
            static_cast<unsigned long long>( m_replayRuntime.LoadedPresentation().fileBytes ) );
    return true;
}


void Run::ResetReplayTimelineForActiveScene( bool preserveBranchMetadata )
{
    CancelReplayToolDragState();

    const std::string* scenePath = m_sceneController.CurrentPath();
    const char* sceneLabel = scenePath && !scenePath->empty() ? scenePath->c_str() : "generated";
    ReplayRuntime::SceneTimelineResetInput replayReset;
    replayReset.sceneLabel = sceneLabel;
    replayReset.preserveBranchMetadata = preserveBranchMetadata;
    replayReset.isSceneMode = SceneState().isSceneMode;
    replayReset.modelCount = SceneState().modelCount;
    replayReset.solverBallCount = SceneState().solverBallCount;
    replayReset.solverBoxCount = SceneState().solverBoxCount;
    replayReset.rngSeed = SceneState().rngSeed;
    replayReset.gameModelCapacity = m_startup.gameModelCapacity;
    replayReset.generatedObjectTypeOverride = static_cast<uint32_t>( m_launchOptions.generatedObjectTypeOverride );
    replayReset.hasUiModelCountOverride = m_sceneController.UIOverrides().modelCountOverride >= 0;
    replayReset.hasUiSolverCountOverride = m_sceneController.UIOverrides().solverBallCountOverride >= 0 ||
                                           m_sceneController.UIOverrides().solverBoxCountOverride >= 0;

    const ReplayRuntime::SceneTimelineResetResult replayResetBegin =
        m_replayRuntime.BeginSceneTimelineReset( replayReset );
    if ( replayResetBegin.exitInspectionCamera )
    {
        ExitReplayInspectionCamera();
    }

    const ReplayRuntime::SceneTimelineResetResult replayResetFinish =
        m_replayRuntime.FinishSceneTimelineReset( replayReset );
    if ( replayResetFinish.exitInspectionCamera )
    {
        ExitReplayInspectionCamera();
    }
}


bool Run::ApplyReplaySolverSampleState( const ReplaySolverFrameSample& sample, char* outReason, std::size_t reasonSize )
{
    return ReplayRestoreService::ApplySolverSampleState( ReplaySolverSampleRestoreContext{ m_cGameModelCollection,
                                                                                           m_cWorldEnvironment,
                                                                                           SceneState(),
                                                                                           m_runtimeSettings,
                                                                                           m_debug,
                                                                                           m_systems.cameras,
                                                                                           m_runtimeTools },
                                                         sample,
                                                         outReason,
                                                         reasonSize );
}

bool Run::CaptureCurrentReplaySolverHash( const ReplaySolverFrameSample& reference,
                                          uint64_t& outSolverHash,
                                          uint64_t& outPresentationHash,
                                          std::size_t& outBodyCount )
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
    m_runtimeTools.BuildReplayLauncherVisualSample( launcherVisual );

    ReplayCaptureInput input;
    input.branch = reference.branch;
    input.eventCursor = reference.eventCursor;
    input.sceneFrame = reference.sceneFrame;
    input.simulationSeconds = reference.simulationSeconds;
    input.physicsDt = reference.physicsDt > 0.0f ? reference.physicsDt : PHYSICS_FIXED_DT;
    input.fixedStep = SceneState().isFixedStep;
    input.scenePhysicsEnabled = SceneState().isScenePhysics;
    input.sceneTextEnabled = SceneState().isSceneText;
    input.waterHidden = m_debug.isWaterHidden;
    input.terrainHidden = m_debug.isTerrainHidden;
    input.cameras = m_systems.cameras;
    input.world = &m_cWorldEnvironment;
    input.models = &m_cGameModelCollection;
    input.bodyStore = &m_cGameModelCollection.BodyStore();
    input.colliderStore = &m_cGameModelCollection.Colliders();
    input.launcherVisual = &launcherVisual;
    verifier.CaptureFrame( input );

    const ReplaySolverFrameSample* verified = verifier.LatestSample();
    if ( !verified )
    {
        return false;
    }

    outSolverHash = verified->solverHash;
    outPresentationHash = verified->presentationHash;
    outBodyCount = verified->bodies.size();
    return true;
}

bool Run::RestoreReplaySolverSampleAsLive( const ReplaySolverFrameSample& sample,
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
    bool hasLiveBackup = false;
    if ( const ReplaySolverFrameSample* latest = m_replayRuntime.Solver().LatestSample() )
    {
        if ( latest->frameIndex != sample.frameIndex || latest->solverHash != sample.solverHash )
        {
            liveBackup = *latest;
            hasLiveBackup = true;
        }
    }

    char applyReason[128] = {};
    if ( !ApplyReplaySolverSampleState( sample, applyReason, sizeof( applyReason ) ) )
    {
        writeReason( applyReason[0] != '\0' ? applyReason : "restore apply failed" );
        return false;
    }

    uint64_t restoredSolverHash = 0;
    uint64_t restoredPresentationHash = 0;
    std::size_t restoredBodyCount = 0;
    const bool hashCaptured =
        CaptureCurrentReplaySolverHash( sample, restoredSolverHash, restoredPresentationHash, restoredBodyCount );
    const bool hashMatched = hashCaptured && restoredSolverHash == sample.solverHash;
    bool fallbackRestored = false;

    if ( !hashMatched && hasLiveBackup )
    {
        char fallbackReason[128] = {};
        fallbackRestored = ApplyReplaySolverSampleState( liveBackup, fallbackReason, sizeof( fallbackReason ) );
    }

#ifdef _DEBUG
    m_diagnosticsRuntime.LogReplayRestoreProbe( SceneState(),
                                                sample,
                                                restoredSolverHash,
                                                restoredPresentationHash,
                                                restoredBodyCount,
                                                hashCaptured,
                                                hashMatched,
                                                !hashMatched && hasLiveBackup,
                                                fallbackRestored );
#endif

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
        sample.branch.branchId != 0
            ? sample.branch.branchId
            : ( m_replayRuntime.Branch().branchId != 0 ? m_replayRuntime.Branch().branchId : 1u );
    ReplayBranchInfo restoredBranch;
    restoredBranch.branchId = (std::max)( m_replayRuntime.Branch().branchId, parentBranchId ) + 1u;
    restoredBranch.parentBranchId = parentBranchId;
    restoredBranch.startFrame = 0;
    restoredBranch.sourceFrame = sample.frameIndex;
    restoredBranch.sourceSolverHash = sample.solverHash;
    m_replayRuntime.Branch() = restoredBranch;
    ResetReplayTimelineForActiveScene( true );
    m_replayRuntime.RecordEvent( ReplayEventKind::BranchRestore,
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
    assert( m_systems.window );

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
    m_systems.window->SetTitleText( titleText );
    const EngineConfig& cfg = m_config;

    m_systems.textures = &m_systems.textureCollection;
    m_systems.textures->BindAssetSystem( &m_systems.assets );
    m_systems.textures->BindRenderContexts( &renderResources, &renderCommands );
    m_systems.assets.RegisterBuiltInSourceAssets( m_config );

    // Build renderer-owned resources from source asset records.
    const SbResult rebuildResourcesResult = RebuildRegisteredRenderResources();
    if ( !rebuildResourcesResult.ok )
    {
        m_lastSceneLoadResult = rebuildResourcesResult;
        return;
    }

    const std::string terrainRawPath =
        m_systems.assets.RegisterSourceAssetPath( SkullbonezCore::Assets::AssetKind::Terrain,
                                                  "terrain.raw",
                                                  cfg.terrainRaw.c_str() );
    std::unique_ptr<Terrain> startupTerrain;
    const SbResult startupTerrainResult = Terrain::TryCreateFromHeightMap( terrainRawPath.c_str(),
                                                                           256,
                                                                           8,
                                                                           15,
                                                                           m_config,
                                                                           m_systems.assets,
                                                                           renderResources,
                                                                           startupTerrain );
    if ( !startupTerrainResult.ok )
    {
        m_lastSceneLoadResult = startupTerrainResult;
        return;
    }
    m_systems.terrain = std::move( startupTerrain );
    m_systems.isFlatSlopeTerrain = false;

    // Init SkyBox (m_xMin, m_xMax, yMin, yMax, m_zMin, m_zMax)
    m_systems.skyBoxOwner = std::make_unique<SkyBox>( -250, 300, -300, 300, -250, 300 );
    m_systems.skyBox = m_systems.skyBoxOwner.get();
    m_systems.skyBox->BindTextures( *m_systems.textures );
    m_systems.skyBox->BindRenderContexts( m_config, m_systems.assets, renderResources );
    const SbResult skyBoxResourceResult = m_systems.skyBox->ResetRenderResources();
    if ( !skyBoxResourceResult.ok )
    {
        m_lastSceneLoadResult = skyBoxResourceResult;
        return;
    }

    m_cWorldEnvironment = WorldEnvironment( cfg.fluidHeight, cfg.fluidDensity, cfg.gasDensity, cfg.gravity );
    m_cWorldEnvironment.BindRenderContexts( m_config, m_systems.assets, renderResources );
    XZBounds tb = m_systems.terrain->GetXZBounds();
    m_cWorldEnvironment.SetTerrainBounds( tb.m_xMin, tb.m_xMax, tb.m_zMin, tb.m_zMax );

    // Why: SDF atlas generation is a startup asset/tooling boundary. Report it
    // as Lane R before scene loading instead of throwing through Run startup.
    const SbResult uiTextResourceResult =
        m_renderer.EnsureUiTextResources( renderResources, m_systems.assets, cfg.window.screenX, cfg.window.screenY );
    if ( !uiTextResourceResult.ok )
    {
        m_lastSceneLoadResult = uiTextResourceResult;
        return;
    }

    // Init cameras (shared across scenes, Reset() between loads)
    m_systems.cameras = &m_systems.cameraCollection;

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

    m_lastSceneLoadResult = LoadScene( 0 );
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
        const bool saved = m_cGameModelCollection.SaveSceneSnapshot( snapshotOutPath,
                                                                     SceneState().isScenePhysics,
                                                                     SceneState().isSceneText,
                                                                     m_cWorldEnvironment,
                                                                     m_systems.cameras->GetCameraTranslation(),
                                                                     m_systems.cameras->GetCameraView(),
                                                                     m_systems.cameras->GetCameraUp(),
                                                                     SceneState().isEditableScene,
                                                                     SceneState().isFixedStep,
                                                                     m_debug.isWaterHidden,
                                                                     m_debug.isTerrainHidden,
                                                                     SceneState().hasFlatSlope,
                                                                     SceneState().flatBaseY,
                                                                     SceneState().flatSlopeX,
                                                                     SceneState().flatSlopeZ );
        if ( !saved )
        {
            return SbResult::Failure( "Runtime/SceneLoadOnly", "Failed to write scene snapshot." );
        }
        printf( "[scene-load-only] Snapshot written: %s\n", snapshotOutPath );
    }
    for ( int i = 1; i < sceneCount; ++i )
    {
        const SbResult loadResult = LoadScene( i );
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


#ifdef _DEBUG
void Run::LogSceneFinished( const char* reason )
{
    const char* scenePath = "generated";
    const std::string* currentScenePath = m_sceneController.CurrentPath();
    if ( currentScenePath && !currentScenePath->empty() )
    {
        scenePath = currentScenePath->c_str();
    }

    const char* rendererName =
        m_renderBackendView.renderDiagnostics ? m_renderBackendView.renderDiagnostics->GetRendererName() : "unknown";
    m_diagnosticsRuntime.LogSceneFinished( SceneState(), scenePath, rendererName, reason );
}


void Run::BeginPhysicsDiagnosticsRun( const char* scenePath )
{
    m_diagnosticsRuntime.BeginPhysicsDiagnosticsRun(
        m_cGameModelCollection,
        SceneState(),
        m_config,
        scenePath,
        m_renderBackendView.renderDiagnostics ? m_renderBackendView.renderDiagnostics->GetRendererName() : "unknown" );
}


void Run::EndPhysicsDiagnosticsRun( const char* status )
{
    m_diagnosticsRuntime.EndPhysicsDiagnosticsRun( SceneState(), status );
}
#endif
