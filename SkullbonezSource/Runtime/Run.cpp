/*
File: SkullbonezSource/Runtime/Run.cpp
Purpose:
  Coordinates the main game loop and high-level runtime lifecycle.

Mental model:
  Runtime code connects authored scene data, input, simulation, render
  backends, and validation-oriented launch modes. Follow who owns state and
  when that state changes.

Glossary:
  FBO (Framebuffer Object): Engine shorthand for an off-screen render target
  exposed through the renderer abstraction.
  Validation gate: Repository script that proves a class of changes before
  commit or PR.

Invariants:
  - Backend-owned render resources must be released while the renderer backend
    is still alive, after a GPU flush, and in the explicit release order below.
  - WorldEnvironment reset can record upload commands; flush after that step
    before later resources are released.

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
#include "Scene/SceneRuntimeLoad.h"
#include "../UI/UIInput.h"
#include "../Physics/PhysicsTimestep.h"

#include <cmath>
#include <cstdio>
#include <cstring>
#include <stdexcept>

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
              if ( run->IsAttachedCameraMode() )
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
                                             run->m_cGameModelCollection.GetPhysicsEngine().BodyStore(),
                                             run->m_cGameModelCollection.GetPhysicsEngine().Colliders(),
                                             run->m_systems.assets,
                                             tracer },
                                           { run->m_debug.physicsDebugContactLinger,
                                             run->InspectGizmoInteractionActive(),
                                             Input::IsKeyDown( VK_CONTROL ),
                                             attachedTargetIndex,
                                             run->m_attachedCamera.activeFollow } );
              run->RenderReplayPathVisualizer( tracer );
              run->RenderReplayCauseFocusOverlay( tracer );
              run->RenderReplayVelocityEditOverlay( tracer );
              tracer.Render( viewProjection, cameraEye, cameraUp, renderCommands );
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
    BindEngineContext();
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


void Run::BindEngineContext()
{
    m_engineContext.Bind( EngineContextBindings{ &m_sceneController,
                                                 &m_simulation,
                                                 &m_diagnosticsRuntime.Capture(),
                                                 &m_diagnosticsRuntime.Diagnostics(),
                                                 &m_runtimeCommands,
                                                 &m_systems,
                                                 &m_runtimeSettings,
                                                 &m_runtimeInput,
                                                 &m_camera,
                                                 &m_debug,
                                                 &m_cWorldEnvironment,
                                                 &m_cGameModelCollection.GetPhysicsEngine(),
                                                 &m_cGameModelCollection } );
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

    // Hazard: backend resources can still be referenced by queued GPU work.
    // Flush before releasing the runtime's owning pointers so teardown cannot
    // free memory while the device is still reading it.
    if ( m_renderBackendView.renderBackend )
    {
        m_renderBackendView.renderBackend->FlushGPU();
    }

    // Lifetime: clean up backend-owned render resources while the current
    // backend is still alive. The world step now releases water GPU resources
    // without rebuilding; the flush keeps any already-submitted GPU work out of
    // teardown.
    ReleaseBackendOwnedRenderResources( "shutdown_release" );
}


void Run::ReleaseBackendOwnedRenderResources( const char* phaseName )
{
    SkullbonezCore::Rendering::IRenderDeviceLifecycle* releaseDeviceLifecycle = m_renderBackendView.deviceLifecycle;
    SkullbonezCore::Rendering::IRenderResourceFactory* releaseRenderResources = m_renderBackendView.renderResources;

    m_renderer.ReleaseBackendOwnedRuntimeResources(
        RuntimeRenderer::BackendResourceReleaseContext{ phaseName,
                                                        releaseDeviceLifecycle,
                                                        releaseRenderResources,
                                                        m_cGameModelCollection,
                                                        m_UI,
                                                        m_runtimeTools } );
}


void Run::RegisterBuiltInAssets()
{
    m_systems.assets.RegisterBuiltInSourceAssets( m_config );
}


std::string Run::ResolveSourceAssetPath( SkullbonezCore::Assets::AssetKind kind,
                                         const char* logicalName,
                                         const std::string& relativePath )
{
    const SkullbonezCore::Assets::SourceAssetRecord& record =
        m_systems.assets.RegisterSourceAsset( kind, logicalName, relativePath.c_str() );
    return record.resolvedPath;
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
    const SkullbonezCore::Rendering::IRenderBackend* renderBackend = m_renderBackendView.renderBackend;
    const bool gfxReady = renderBackend != nullptr;
    const int backendWidth = renderBackend ? renderBackend->GetWidth() : 0;
    const int backendHeight = renderBackend ? renderBackend->GetHeight() : 0;
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


void Run::SetTimeScaleOverride( float scale )
{
    m_launchOptions.timeScaleOverride = scale;
}


void Run::SetFixedStepOverride()
{
    m_launchOptions.fixedStep = true;
}


void Run::SetSeedOverride( unsigned int seed )
{
    m_launchOptions.seedOverride = seed;
}


void Run::SetNoWaterOverride()
{
    m_launchOptions.noWater = true;
}


void Run::SetNoSleepOverride()
{
    m_launchOptions.noSleep = true;
    m_runtimeSettings.isPhysicsSleepEnabled = false;
    m_cGameModelCollection.SetPhysicsSleepEnabled( false );
}


void Run::SetNoContactAudioOverride()
{
    m_launchOptions.noContactAudio = true;
    m_contactAudio.SetEnabled( false );
}


void Run::SetTornadoOverride( bool enabled )
{
    m_launchOptions.hasTornadoOverride = true;
    m_launchOptions.tornadoEnabled = enabled;
    m_runtimeSettings.tornadoField.enabled = enabled;
    if ( m_runtimeSettings.tornadoVisual.autoEnableWithTornado )
    {
        m_runtimeSettings.tornadoVisual.enabled = enabled;
    }
    SyncTornadoRuntimeSettingsToPhysics( m_cGameModelCollection, m_runtimeSettings );
}


void Run::SetTornadoVectorFieldOverride( bool enabled )
{
    m_launchOptions.tornadoVectors = enabled;
    m_runtimeSettings.tornadoField.visualizeVelocityField = enabled;
    SyncTornadoRuntimeSettingsToPhysics( m_cGameModelCollection, m_runtimeSettings );
}


void Run::SetCinematicRenderingOverride( bool enabled )
{
    m_launchOptions.hasCinematicRenderingOverride = true;
    m_launchOptions.cinematicRendering = enabled;
}


void Run::SetCinematicShadowsOverride( bool enabled )
{
    m_launchOptions.hasCinematicShadowsOverride = true;
    m_launchOptions.cinematicShadows = enabled;
}


void Run::SetDemoHeroStyleOverride()
{
    m_launchOptions.demoHeroStyle = true;
}


void Run::SetInteractiveRunOverride()
{
    m_launchOptions.interactiveSceneRun = true;
}


void Run::SetFrameCountOverride( int frames )
{
    m_launchOptions.frameCountOverride = (std::max)( 1, frames );
}


void Run::SetAllocationGuardMode( RuntimeAllocation::RuntimeAllocationGuardMode mode )
{
    m_launchOptions.allocationGuardMode = mode;
    if ( RuntimeAllocation::GetRuntimeAllocationGuardMode() != mode )
    {
        RuntimeAllocation::SetRuntimeAllocationGuardMode( mode );
    }
}


void Run::SetUIStressOverride( unsigned int seed, int actionsPerFrame )
{
    m_launchOptions.uiStress = true;
    m_launchOptions.uiStressSeed = seed > 0 ? seed : 0x7F4A7C15u;
    m_launchOptions.uiStressActions = std::clamp( actionsPerFrame, 1, 32 );
}


void Run::SetGraphicsStressOverride( unsigned int seed,
                                     int actionsPerFrame,
                                     int sceneIntervalFrames,
                                     int memoryLogIntervalFrames )
{
    const unsigned int resolvedSeed = seed > 0 ? seed : 0xC11E2026u;
    m_launchOptions.graphicsStress = true;
    m_launchOptions.graphicsStressSeed = resolvedSeed;
    m_launchOptions.graphicsStressActions = std::clamp( actionsPerFrame, 1, 64 );
    m_launchOptions.graphicsStressSceneIntervalFrames = std::clamp( sceneIntervalFrames, 1, 600 );
    m_launchOptions.graphicsStressMemoryIntervalFrames = std::clamp( memoryLogIntervalFrames, 0, 36000 );
    m_launchOptions.interactiveSceneRun = true;

    m_graphicsStress.enabled = true;
    m_graphicsStress.randomState = resolvedSeed;
    m_graphicsStress.actionsPerFrame = m_launchOptions.graphicsStressActions;
    m_graphicsStress.sceneIntervalFrames = m_launchOptions.graphicsStressSceneIntervalFrames;
    m_graphicsStress.memoryLogIntervalFrames = m_launchOptions.graphicsStressMemoryIntervalFrames;
}


void Run::SetReplayRecording( bool enabled, int retentionSeconds, const char* hashLogPath )
{
    // Runtime allocation policy: launcher replay visuals are copied every
    // captured physics tick, so keep their scratch vectors reserved before the
    // replay phase begins.
    m_replayLauncherVisualScratch.rayLines.reserve( RunRayCastTestState::MAX_LINES );
    m_replayLauncherVisualScratch.laserShots.reserve( REPLAY_LAUNCHER_LASER_SHOT_CAPACITY );

    const ReplayRuntime::RecordingConfigResult replayConfig =
        m_replayRuntime.ConfigureRecording( enabled, retentionSeconds, hashLogPath, m_startup.gameModelCapacity );
    if ( m_replayRuntime.ResetScrubberState() )
    {
        ExitReplayInspectionCamera();
    }
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
}


void Run::SetMainMemoryDumpPath( const char* path )
{
    m_diagnosticsRuntime.SetMainMemoryDumpPath( path );
}

void Run::SetInteractionAutomation( const char* scriptPath, const char* reportPath )
{
    if ( !scriptPath || scriptPath[0] == '\0' )
    {
        throw std::runtime_error( "interaction automation requires a script path" );
    }

    m_interactionAutomation = RunInteractionAutomationState{};
    strcpy_s( m_interactionAutomation.scriptPath, sizeof( m_interactionAutomation.scriptPath ), scriptPath );
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
    m_interactionAutomation.enabled = true;
    printf( "[interaction] Script: %s\n", m_interactionAutomation.scriptPath );
    printf( "[interaction] Report: %s\n", m_interactionAutomation.reportPath );
}


#ifdef _DEBUG
void Run::SetReplayScrubProbe( float normalized )
{
    m_replayProbes.scrub.enabled = true;
    m_replayProbes.scrub.completed = false;
    m_replayProbes.scrub.normalized = std::clamp( normalized, 0.0f, 0.99f );
    printf( "[replay] Scrub probe enabled: normalized=%.3f\n", m_replayProbes.scrub.normalized );
}

void Run::SetReplayRestoreProbe( float normalized )
{
    m_replayProbes.restore.enabled = true;
    m_replayProbes.restore.completed = false;
    m_replayProbes.restore.normalized = std::clamp( normalized, 0.0f, 0.99f );
    printf( "[replay] Restore probe enabled: normalized=%.3f\n", m_replayProbes.restore.normalized );
}

void Run::SetReplaySaveProbe( const char* path )
{
    if ( !path || path[0] == '\0' )
    {
        throw std::runtime_error( "replay save probe requires an output path" );
    }

    m_replayProbes.save.enabled = true;
    m_replayProbes.save.completed = false;
    strcpy_s( m_replayProbes.save.path, sizeof( m_replayProbes.save.path ), path );
    printf( "[replay] Save probe enabled: path=%s\n", m_replayProbes.save.path );
}

void Run::RecordReplayProbeFailure( const SbResult& result )
{
    if ( result.ok || m_replayProbes.failure.failed )
    {
        return;
    }

    const char* owner = result.error.owner && result.error.owner[0] != '\0' ? result.error.owner : "ReplayProbe";
    const char* message =
        result.error.message[0] != '\0' ? result.error.message : "replay probe failed without a failure message";
    m_replayProbes.failure.failed = true;
    strcpy_s( m_replayProbes.failure.owner, sizeof( m_replayProbes.failure.owner ), owner );
    strcpy_s( m_replayProbes.failure.message, sizeof( m_replayProbes.failure.message ), message );
}

bool Run::ReplayProbeFailed() const
{
    return m_replayProbes.failure.failed;
}

const char* Run::ReplayProbeFailureOwner() const
{
    return m_replayProbes.failure.owner[0] != '\0' ? m_replayProbes.failure.owner : "ReplayProbe";
}

const char* Run::ReplayProbeFailureMessage() const
{
    return m_replayProbes.failure.message[0] != '\0' ? m_replayProbes.failure.message : "replay probe failed";
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
    if ( !replayResetFinish.timelineStarted )
    {
        return;
    }

    m_solverReplayMismatch.reports = 0;
    m_solverReplayMismatch.suppressed = false;
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
    input.bodyStore = &m_cGameModelCollection.GetPhysicsEngine().BodyStore();
    input.colliderStore = &m_cGameModelCollection.GetPhysicsEngine().Colliders();
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


void Run::SetInitialOverlayMode( OverlayMode mode )
{
    m_debug.overlayMode = mode;
    if ( mode != OverlayMode::None )
    {
        m_UI.SetVisible( true );
    }
    switch ( mode )
    {
    case OverlayMode::SceneStats:
        m_UI.SetActiveTab( InGameUITab::Scene );
        break;
    case OverlayMode::Keys:
        m_UI.SetActiveTab( InGameUITab::Keys );
        break;
    case OverlayMode::BarsNormalized:
    case OverlayMode::BarsAbsolute:
    case OverlayMode::Timers:
        m_UI.SetActiveTab( InGameUITab::Profiler );
        break;
    default:
        break;
    }
}


void Run::SetTopTextHidden( bool hidden )
{
    m_debug.isTopTextHidden = hidden;
}


void Run::SetBroadphaseVisualizerEnabled( bool enabled )
{
    m_debug.isBroadphaseOverlay = enabled;
}


void Run::SetGeneratedObjectTypeOverride( GeneratedObjectTypeOverride objectTypeOverride )
{
    m_launchOptions.generatedObjectTypeOverride = objectTypeOverride;
}


void Run::SetPhysicsDebugFlagsOverride( uint32_t flags )
{
    m_launchOptions.hasPhysicsDebugFlagsOverride = true;
    m_launchOptions.physicsDebugFlagsOverride = flags & PHYSICS_DEBUG_ALL;
}


void Run::SetPhysicsDebugTransparentOverride( bool transparent )
{
    m_launchOptions.hasPhysicsDebugTransparentOverride = true;
    m_launchOptions.physicsDebugTransparentOverride = transparent;
}


void Run::SetPhysicsDebugAlphaOverride( float alpha )
{
    m_launchOptions.hasPhysicsDebugAlphaOverride = true;
    m_launchOptions.physicsDebugAlphaOverride = (std::max)( 0.05f, (std::min)( alpha, 1.0f ) );
}


void Run::SetPhysicsDebugContactLingerOverride( float seconds )
{
    m_launchOptions.hasPhysicsDebugContactLingerOverride = true;
    m_launchOptions.physicsDebugContactLingerOverride = (std::max)( 0.0f, (std::min)( seconds, 5.0f ) );
}


#ifdef _DEBUG
void Run::SetPhysicsRegressionLogOverride( const char* path )
{
    m_diagnosticsRuntime.SetPhysicsRegressionLogOverride( path );
}


void Run::SetPhysicsCollisionTimeLogOverride( const char* path )
{
    m_diagnosticsRuntime.SetPhysicsCollisionTimeLogOverride( path );
}


void Run::SetPhysicsDiagnosticsPath( const char* path, bool fixedStepForcedByDiagnostics )
{
    m_diagnosticsRuntime.SetPhysicsDiagnosticsPath( m_cGameModelCollection, path, fixedStepForcedByDiagnostics );
}
#endif


void Run::Initialise()
{
    assert( m_systems.window );

    assert( m_renderBackendView.renderBackend && "Run requires a render backend before Initialise()" );
    IRenderBackend& renderBackend = *m_renderBackendView.renderBackend;
    auto& renderResources = static_cast<SkullbonezCore::Rendering::IRenderResourceFactory&>( renderBackend );
    auto& renderCommands = static_cast<SkullbonezCore::Rendering::IRenderCommandContext&>( renderBackend );

    const char* rendererName = renderBackend.GetRendererName();
    char titleText[256];
    sprintf_s( titleText, "%s [%s] -- LOADING!!!", TITLE_TEXT, rendererName );
    m_systems.window->SetTitleText( titleText );
    const EngineConfig& cfg = m_config;

    m_systems.textures = &m_systems.textureCollection;
    m_systems.textures->BindAssetSystem( &m_systems.assets );
    m_systems.textures->BindRenderContexts( &renderResources, &renderCommands );
    RegisterBuiltInAssets();

    // Build renderer-owned resources from source asset records.
    RebuildRegisteredRenderResources();

    const std::string terrainRawPath =
        ResolveSourceAssetPath( SkullbonezCore::Assets::AssetKind::Terrain, "terrain.raw", cfg.terrainRaw );
    m_systems.terrain =
        std::make_unique<Terrain>( terrainRawPath.c_str(), 256, 8, 15, m_config, m_systems.assets, renderResources );
    m_systems.isFlatSlopeTerrain = false;

    // Init SkyBox (m_xMin, m_xMax, yMin, yMax, m_zMin, m_zMax)
    m_systems.skyBoxOwner = std::make_unique<SkyBox>( -250, 300, -300, 300, -250, 300 );
    m_systems.skyBox = m_systems.skyBoxOwner.get();
    m_systems.skyBox->BindTextures( *m_systems.textures );
    m_systems.skyBox->BindRenderContexts( m_config, m_systems.assets, renderResources );
    m_systems.skyBox->ResetRenderResources();

    m_cWorldEnvironment = WorldEnvironment( cfg.fluidHeight, cfg.fluidDensity, cfg.gasDensity, cfg.gravity );
    m_cWorldEnvironment.BindRenderContexts( m_config, m_systems.assets, renderResources );
    XZBounds tb = m_systems.terrain->GetXZBounds();
    m_cWorldEnvironment.SetTerrainBounds( tb.m_xMin, tb.m_xMax, tb.m_zMin, tb.m_zMax );

    // Init font (HDC, font)
    m_renderer.EnsureUiTextResources( renderResources, m_systems.assets, cfg.window.screenX, cfg.window.screenY );

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

    LoadScene( 0 );
}


void Run::RunSceneLoadOnly( const char* snapshotOutPath )
{
    const int sceneCount = m_sceneController.QueueSize();
    if ( sceneCount <= 0 )
    {
        printf( "[scene-load-only] Exiting because --scene-load-only was requested, but no scenes were queued.\n" );
        fflush( stdout );
        return;
    }

    const bool writeSnapshot = snapshotOutPath && snapshotOutPath[0] != '\0';
    if ( writeSnapshot && sceneCount != 1 )
    {
        throw std::runtime_error( "--scene-snapshot-out requires exactly one loaded scene." );
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
            throw std::runtime_error( "Failed to write scene snapshot." );
        }
        printf( "[scene-load-only] Snapshot written: %s\n", snapshotOutPath );
    }
    for ( int i = 1; i < sceneCount; ++i )
    {
        LoadScene( i );
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
        m_renderBackendView.renderBackend ? m_renderBackendView.renderBackend->GetRendererName() : "unknown";
    m_diagnosticsRuntime.LogSceneFinished( SceneState(), scenePath, rendererName, reason );
}


void Run::BeginPhysicsDiagnosticsRun( const char* scenePath )
{
    m_diagnosticsRuntime.BeginPhysicsDiagnosticsRun(
        m_cGameModelCollection,
        SceneState(),
        m_config,
        scenePath,
        m_renderBackendView.renderBackend ? m_renderBackendView.renderBackend->GetRendererName() : "unknown" );
}


void Run::EndPhysicsDiagnosticsRun( const char* status )
{
    m_diagnosticsRuntime.EndPhysicsDiagnosticsRun( SceneState(), status );
}
#endif
