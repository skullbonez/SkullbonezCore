/*
File: SkullbonezSource/Runtime/Run.cpp
Purpose:
  Coordinates the main game loop and high-level runtime lifecycle.

Summary:
  Run.cpp coordinates the main game loop and high-level runtime lifecycle. As
  an implementation unit, keep edits anchored on local owner boundaries and
  call direction and on the glossary/invariants below.

Glossary:
  FBO (Framebuffer Object): Engine shorthand for an off-screen render target
  exposed through the renderer abstraction.
  Lane R result: Recoverable scene/load or renderer-drain failure reported as
    an SkullbonezCore::Core::SbResult so the owning boundary can stop before unsafe mutation.
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
#include "RuntimeOverlayDiagnostics.h"
#include "RuntimeValidationHarness.h"
#include "RuntimeCameraMode.h"
#include "InputFrame.h"
#include "Window.h"
#include "../Core/WindowConstants.h"
#include "Replay/ReplayOverlayLayout.h"
#include "Replay/ReplayRestoreService.h"
#include "Replay/ReplayRestoreTransactions.h"
#include "Replay/ReplayV2Artifact.h"
#include "RuntimeFileWriter.h"
#include "../Core/Allocation/RuntimeAllocationTracker.h"
#include "../Core/Allocation/RuntimeReserveAllocator.h"
#include "Scene/SceneRuntimeLoad.h"
#include "Diagnostics/SceneMemoryDiagnostics.h"
#include "../Scene/SceneSnapshotWriter.h"
#include "../Core/FatalError.h"
#include "../Core/Log.h"
#include "../Physics/PhysicsTimestep.h"
#include "../Rendering/DX12/Dx12Diagnostics.h"
#include "../UI/UI.h"
#include "../World/Terrain.h"

#include <cmath>
#include <cstdio>
#include <cstring>

using namespace SkullbonezCore::Runtime;
using namespace SkullbonezCore::Runtime::ReplayTimelineOperations;
using namespace SkullbonezCore::Math::CollisionDetection;
using namespace SkullbonezCore::Math::Orientation;
using namespace SkullbonezCore::Math::Transformation;
using namespace SkullbonezCore::Physics;
using SkullbonezCore::Environment::WorldEnvironment;
using SkullbonezCore::GameObjects::SceneSaveRequest;
using SkullbonezCore::GameObjects::SceneSaveView;
using SkullbonezCore::GameObjects::SceneSnapshotWriter;
using SkullbonezCore::Geometry::Terrain;
using SkullbonezCore::Geometry::XZBounds;
using SkullbonezCore::UI::InGameUITab;
using namespace SkullbonezCore::Runtime::ReplayOverlay;
namespace CoreAllocation = SkullbonezCore::Core::Allocation;

namespace
{
constexpr std::size_t REPLAY_LAUNCHER_LASER_SHOT_CAPACITY = 32;

std::unique_ptr<SkullbonezCore::UI::InGameUI> CreateOperatorUiForStartup( SkullbonezCore::Core::Profiler* profiler )
{
    CoreAllocation::RuntimeAllocationScope allocationScope( CoreAllocation::RuntimeAllocationPhase::Startup );
    // Allocation policy: the cohesive UI owner must remain opaque to Run.h so
    // the public composition-root header does not republish the UI graph.
    return std::make_unique<SkullbonezCore::UI::InGameUI>( profiler );
}

SkullbonezCore::Environment::CameraMovementSettings
BuildCameraMovementSettings( const SkullbonezCore::Core::EngineConfig& cfg )
{
    SkullbonezCore::Environment::CameraMovementSettings settings;
    settings.minViewMag = cfg.camera.minViewMag;
    settings.maxViewMag = cfg.camera.maxViewMag;
    settings.minCameraHeight = cfg.camera.minCameraHeight;
    settings.cameraCollisionThreshold = cfg.camera.cameraCollisionThreshold;
    return settings;
}


// Concept: these source-local helpers keep startup policy grouping visible
// without turning launch-only details back into `Run` public or private methods.
// They mutate the owner references that `Run` passes to them synchronously from
// `ApplyStartupOverrides()`, preserving the original command-line apply order.
void ApplyRuntimeLaunchPolicy( const RunLaunchOptions& launch, RunLaunchOptions& target, SceneWorld& sceneWorld )
{
    PhysicsEngine& physics = sceneWorld.Physics();
    SkullbonezCore::Gameplay::TornadoGameplay& tornadoGameplay = sceneWorld.Tornado();
    target.allocationGuardMode = launch.allocationGuardMode;
    if ( CoreAllocation::GetRuntimeAllocationGuardMode() != launch.allocationGuardMode )
    {
        CoreAllocation::SetRuntimeAllocationGuardMode( launch.allocationGuardMode );
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
        physics.SetSleepEnabled( false );
    }
    if ( launch.hasTornadoOverride )
    {
        target.hasTornadoOverride = true;
        target.tornadoEnabled = launch.tornadoEnabled;
        SkullbonezCore::Gameplay::TornadoFieldConfig tornadoField = tornadoGameplay.GetFieldConfig();
        tornadoField.enabled = launch.tornadoEnabled;
        tornadoGameplay.SetFieldConfig( tornadoField );
        if ( tornadoGameplay.VisualAutoEnableWithTornado() )
        {
            tornadoGameplay.SetVisualEnabled( launch.tornadoEnabled );
        }
    }
    if ( launch.tornadoVectors )
    {
        target.tornadoVectors = true;
        SkullbonezCore::Gameplay::TornadoFieldConfig tornadoField = tornadoGameplay.GetFieldConfig();
        tornadoField.visualizeVelocityField = true;
        tornadoGameplay.SetFieldConfig( tornadoField );
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
#if defined( SKULLBONEZ_DEVELOPMENT_TOOLS )
    target.developmentUiMode = launch.developmentUiMode;
    target.developmentUiModeExplicit = launch.developmentUiModeExplicit;
#endif
}


bool ConfigureStartupReplayRecording( const RunStartupOverrides& overrides,
                                      ReplayRuntime& replayRuntime,
                                      int sceneObjectCapacity )
{
    if ( !overrides.configureReplayRecording )
    {
        return false;
    }

    // Runtime allocation policy: launcher replay visuals are copied every
    // captured physics tick, so keep their scratch vectors reserved before the
    // replay phase begins.

    const ReplayRecordingActivationResult replayActivation =
        replayRuntime.ConfigureRecording( overrides.replayRecordingEnabled,
                                          overrides.replayRetentionSeconds,
                                          overrides.replayHashLogPath,
                                          sceneObjectCapacity );
    const ReplayRecordingConfigResult& replayConfig = replayActivation.configuration;
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
    return replayActivation.exitInspectionCamera;
}


#ifdef _DEBUG
void ApplyStartupDiagnosticsPolicy( const RunStartupOverrides& overrides,
                                    DiagnosticsRuntime& diagnosticsRuntime,
                                    PhysicsEngine& physics )
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
        diagnosticsRuntime.SetPhysicsDiagnosticsPath( physics,
                                                      overrides.physicsDiagnosticsPath,
                                                      overrides.physicsDiagnosticsFixedStepForced );
    }
}
#endif

} // namespace


void RunStartupState::ApplyStartupConfig( const SkullbonezCore::Core::EngineConfig& config )
{
    sceneObjectCapacity =
        std::clamp( config.runtimeCapacity.sceneObjectCapacity, 1, SkullbonezCore::Scene::Capacity::MAX_SCENE_OBJECTS );
    workerThreads = config.runtimeCapacity.workerThreads;
}


Run::Run( Window& window,
          std::vector<std::string> sceneQueue,
          SkullbonezCore::Core::EngineConfig& config,
          Threading::WorkerPool& workerPool,
          SkullbonezCore::Core::Profiler* profiler,
          RuntimeRenderBackendView renderBackendView,
          SkullbonezCore::Core::DevelopmentTools::TracyClientOwner* tracyClientOwner )
    : m_window( window ), m_workerPool( workerPool ), m_config( config ), m_profiler( profiler ),
      m_tracyClientOwner( tracyClientOwner ), m_sceneController( std::move( sceneQueue ) ), m_replayRuntime( profiler ),
      m_operatorUi( CreateOperatorUiForStartup( profiler ) ),
      m_overlayDiagnostics( RuntimeOverlayDiagnostics::CreateForStartup( profiler ) ),
      m_validationHarness( RuntimeValidationHarness::CreateForStartup() ), m_renderBackendView( renderBackendView ),
      m_renderer( m_renderBackendView,
                  RenderWorldView{ m_assets,
                                   m_sceneController.Scene().Cameras(),
                                   m_sceneController.Scene().Terrain(),
                                   window,
                                   m_config,
                                   m_sceneController.Scene().Environment(),
                                   m_overlayDiagnostics->RenderResources(),
                                   profiler },
                  m_sceneController.State() )
{
    const SkullbonezCore::Core::EngineConfig& cfg = m_config;
#if defined( SKULLBONEZ_DEVELOPMENT_TOOLS )
    const SkullbonezCore::Core::SbResult imguiStartResult =
        m_imguiEditor.Start( m_window.NativeWindowHandle(), m_renderBackendView.developmentUiRenderer );
    if ( !imguiStartResult.ok )
    {
        m_applicationExit.RequestOwnedFailure( imguiStartResult );
    }
    else
    {
        // Lifetime: Window forwards only native messages and retains no ImGui
        // context/resource authority. Run removes this direct borrow before
        // destroying the editor owner below.
        m_window.BindDevelopmentUiInput( m_imguiEditor );
    }
#endif
    m_diagnosticsRuntime.BindProfiler( profiler );
    m_sceneController.Scene().Physics().BindProfiler( profiler );
    m_sceneController.Scene().Cameras().ApplyMovementSettings( BuildCameraMovementSettings( cfg ) );
    RefreshSceneBrowserList( m_operatorUi->SceneNavigation().browser );
    m_sceneController.Scene().ApplyRuntimeConfig( cfg );
    m_renderer.SetVsyncEnabled( cfg.runtimeRender.vsyncEnabled );
    m_renderer.SetPipelineSyncEnabled( cfg.runtimeRender.forcePipelineSync );
    m_renderDefaults.CaptureStartupCinematicBaseline( cfg.cinematicRender );
    m_startup.ApplyStartupConfig( cfg );
}


Run::~Run()
{
    CoreAllocation::RuntimeAllocationScope allocationScope( CoreAllocation::RuntimeAllocationPhase::Shutdown );
#ifdef _DEBUG
    m_diagnosticsRuntime.EndPhysicsDiagnosticsRun( m_sceneController.State(), "process_end" );
#endif

    if ( m_diagnosticsRuntime.MainMemoryDumpRequested() )
    {
        m_diagnosticsRuntime.WriteMainMemoryDump( m_replayRuntime.CollectMemoryStats(),
                                                  CollectSceneMemoryStats( SceneMemoryDiagnosticsView{
                                                      m_sceneController.Scene().Entities(),
                                                      m_sceneController.Scene().CollectGameplayMemoryBytes(),
                                                      m_sceneController.Scene().CollectGameplayDebugMemoryBytes(),
                                                      m_sceneController.Scene().Physics(),
                                                      m_sceneController.Scene().RenderInstances() } ),
                                                  m_sceneController.State(),
                                                  "shutdown",
                                                  m_timers.simulationTimer.GetTotalTime() );
    }
    m_diagnosticsRuntime.ClosePerfLog();
    const ReplayShutdownReport replayShutdown = m_replayRuntime.FinishShutdown();
    if ( replayShutdown.presentation.enabled )
    {
        const ReplayRecorderStats& replayStats = replayShutdown.presentation;
        printf( "[replay] Captured %llu physics samples, retained %llu/%llu, checkpoints %llu/%llu, "
                "latest_hash=0x%016llX\n",
                static_cast<unsigned long long>( replayStats.totalFramesCaptured ),
                static_cast<unsigned long long>( replayStats.sampleCount ),
                static_cast<unsigned long long>( replayStats.sampleCapacity ),
                static_cast<unsigned long long>( replayStats.checkpointCount ),
                static_cast<unsigned long long>( replayStats.checkpointCapacity ),
                static_cast<unsigned long long>( replayStats.latestStateHash ) );
    }
    if ( replayShutdown.solver.enabled )
    {
        const ReplayRecorderStats& replayStats = replayShutdown.solver;
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
    const SkullbonezCore::Core::SbResult releaseResult = m_renderer.ReleaseBackendOwnedRuntimeResources(
        RuntimeRenderer::BackendResourceReleaseContext{ "shutdown_release",
                                                        m_renderBackendView.renderFrame,
                                                        m_renderBackendView.renderResources,
                                                        m_renderBackendView.renderTextures,
                                                        m_renderBackendView.renderGeometry,
                                                        *m_operatorUi,
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
    // Lifetime: the renderer has now drained submitted GPU work. Gameplay can
    // release its extension-owned transient storage without a renderer-side
    // content branch or a retained backend pointer.
    m_sceneController.Scene().Tornado().ReleaseVisualResources();
#if defined( SKULLBONEZ_DEVELOPMENT_TOOLS )
    // Lifetime: the preceding release path proved all submitted frames complete.
    // Destroy vendor GPU objects and return its descriptor rows while both the
    // ImGui context and concrete DX12 owners still exist.
    m_window.UnbindDevelopmentUiInput( m_imguiEditor );
    m_imguiEditor.Shutdown();
#endif
}


SkullbonezCore::Core::SbResult Run::ApplyStartupOverrides( const RunStartupOverrides& overrides )
{
    // Why: Runtime/Init owns CLI parsing, but Run owns the live side effects
    // needed to make those startup policies active. Keep the public boundary as
    // one launch packet and preserve the old setter order because several
    // options update both reusable launch policy and already-constructed
    // runtime services.
    const RunLaunchOptions& launch = overrides.launch;

    ApplyRuntimeLaunchPolicy( launch, m_launchOptions, m_sceneController.Scene() );
#if defined( SKULLBONEZ_DEVELOPMENT_TOOLS )
    ApplyDevelopmentUiMode();
#endif
    if ( m_validationHarness->ConfigureStartup( overrides, m_launchOptions ) )
    {
        m_launchOptions.interactiveSceneRun = true;
        m_sceneController.EnterInteractiveRun();
        m_diagnosticsRuntime.Capture().DisableAutomationExit();
        m_validationHarness->MarkLiveStyleReady();
    }
    if ( overrides.mainMemoryDumpPath && overrides.mainMemoryDumpPath[0] != '\0' )
    {
        m_diagnosticsRuntime.SetMainMemoryDumpPath( overrides.mainMemoryDumpPath );
    }
    if ( ConfigureStartupReplayRecording( overrides, m_replayRuntime, m_startup.sceneObjectCapacity ) )
    {
        m_replayRuntime.ExitInspectionCamera(
            &m_sceneController.Scene().Cameras(),
            m_sceneController.Scene().Terrain().Get(),
            m_camera,
            NormalizeRuntimeCameraMode( m_replayRuntime.BuildInputView().restoreCameraMode,
                                        m_sceneController.State().isSceneMode,
                                        RuntimeCameraModeEnabledMask( m_sceneController.State().isSceneMode,
                                                                      m_sceneController.Scene().SceneEntityCount() ) ),
            m_attachedCamera.State().activeFollow,
            m_camera.director.grabbed,
            m_interaction,
            m_inputRouter );
    }
    m_replayRuntime.ConfigureStartupWorkflows( ReplayStartupRequest{ overrides.replayLoadPath,
                                                                     overrides.replayLoadProbe,
#ifdef _DEBUG
                                                                     overrides.replayRestoreFileProbePath,
                                                                     overrides.replayRestoreTargetFileProbePath,
                                                                     overrides.replayRestoreBranchFileProbePath,
                                                                     overrides.replayRestoreFailureFileProbePath,
                                                                     overrides.replayScrubProbe,
                                                                     overrides.replayScrubProbeNormalized,
                                                                     overrides.replayRestoreProbe,
                                                                     overrides.replayRestoreProbeNormalized,
                                                                     overrides.replaySaveProbe,
                                                                     overrides.replaySaveProbePath
#endif
    } );
    m_overlayDiagnostics->ApplyStartupPolicy( overrides, m_launchOptions, *m_operatorUi );
#ifdef _DEBUG
    ApplyStartupDiagnosticsPolicy( overrides, m_diagnosticsRuntime, m_sceneController.Scene().Physics() );
#endif
    if ( !overrides.interactionScriptPath )
    {
        return SkullbonezCore::Core::SbResult::Success();
    }
#if defined( SKULLBONEZ_AUTOMATION_DIAGNOSTICS )
    const SkullbonezCore::Core::SbResult result = ConfigureInteractionAutomation( m_interactionAutomation,
                                                                                  overrides.interactionScriptPath,
                                                                                  overrides.interactionReportPath );
    if ( !result.ok )
    {
        const ReplayAutomationView replay = m_replayRuntime.BuildAutomationView();
        (void)m_interactionAutomation.reportWriter.Write( InteractionAutomationReportInputs{
            m_interactionAutomation.status,
            m_interactionAutomation.scriptPath,
            m_sceneController.Scene(),
            m_sceneController.State(),
            m_sceneController.CurrentPath() ? m_sceneController.CurrentPath()->c_str() : nullptr,
            m_runtimeTools,
            replay,
            m_interaction,
            m_camera,
            *m_operatorUi } );
    }
    return result;
#else
    // Lane R: interaction scripts are external validation input. Ordinary game
    // builds reject them instead of linking the diagnostic controller into the
    // frame loop; tools must use the dedicated Automation configuration.
    return SkullbonezCore::Core::SbResult::Failure( "InteractionAutomation",
                                                    "--interaction-script requires an Automation|x64 build." );
#endif
}


void Run::Initialise()
{
    // Why: timers default to inert storage so Run construction cannot throw
    // before the startup reporter exists. Initialise them at this boundary and
    // return platform counter failures through the normal Lane R process path.
    const SkullbonezCore::Core::SbResult timerStartupResult = m_timers.Initialise();
    if ( !timerStartupResult.ok )
    {
        m_lastSceneLoadResult = timerStartupResult;
        return;
    }

    assert( m_renderBackendView.renderResources && "Run requires render resources before Initialise()" );
    assert( m_renderBackendView.renderTextures && "Run requires render textures before Initialise()" );
    assert( m_renderBackendView.renderGeometry && "Run requires render geometry before Initialise()" );
    assert( m_renderBackendView.renderFrame && m_renderBackendView.renderGraph &&
            "Run requires frame and graph owners before Initialise()" );
    assert( m_renderBackendView.renderDiagnostics && "Run requires render diagnostics before Initialise()" );
    auto& renderResources = *m_renderBackendView.renderResources;
    auto& renderTextures = *m_renderBackendView.renderTextures;
    auto& renderGeometry = *m_renderBackendView.renderGeometry;
    const SkullbonezCore::Rendering::Dx12Diagnostics& renderDiagnostics = *m_renderBackendView.renderDiagnostics;

    const char* rendererName = renderDiagnostics.GetRendererName();
    char titleText[256];
    sprintf_s( titleText, "%s [%s] -- LOADING!!!", TITLE_TEXT, rendererName );
    m_window.SetTitleText( titleText );
    const SkullbonezCore::Core::EngineConfig& cfg = m_config;

    // Build renderer-owned resources from source asset records.
    const SkullbonezCore::Core::SbResult rebuildResourcesResult =
        m_renderer.InitialiseProcessResources( renderResources,
                                               renderTextures,
                                               renderGeometry,
                                               m_config,
                                               m_launchOptions.dumpTextureAssets );
    if ( !rebuildResourcesResult.ok )
    {
        m_lastSceneLoadResult = rebuildResourcesResult;
        return;
    }
    const std::string terrainRawPath = m_assets.RegisterSourceAssetPath( SkullbonezCore::Assets::AssetKind::Terrain,
                                                                         "terrain.raw",
                                                                         cfg.assetPaths.terrainRaw.c_str() );
    std::unique_ptr<Terrain> startupTerrain;
    const SkullbonezCore::Core::SbResult startupTerrainResult = Terrain::TryCreateFromHeightMap( terrainRawPath.c_str(),
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
    m_sceneController.Scene().Terrain().Replace( std::move( startupTerrain ), false );

    m_sceneController.Scene().Environment() = WorldEnvironment( cfg.worldForces.fluidHeight,
                                                                cfg.worldForces.fluidDensity,
                                                                cfg.worldForces.gasDensity,
                                                                cfg.worldForces.gravity );
    m_sceneController.Scene().Environment().BindRenderContexts( m_config, m_assets, renderResources );
    XZBounds tb = m_sceneController.Scene().Terrain().Get()->GetXZBounds();
    m_sceneController.Scene().Environment().SetTerrainBounds( tb.m_xMin, tb.m_xMax, tb.m_zMin, tb.m_zMax );

    // Why: SDF atlas generation is a startup asset/tooling boundary. Report it
    // as Lane R before scene loading instead of throwing through Run startup.
    const SkullbonezCore::Core::SbResult uiTextResourceResult = m_renderer.EnsureUiTextResources( renderResources,
                                                                                                  renderTextures,
                                                                                                  renderGeometry,
                                                                                                  m_assets,
                                                                                                  cfg.window.screenX,
                                                                                                  cfg.window.screenY );
    if ( !uiTextResourceResult.ok )
    {
        m_lastSceneLoadResult = uiTextResourceResult;
        return;
    }

    SceneLoadConsumerOutputs sceneLoadOutputs;
    m_lastSceneLoadResult = m_sceneController.Load(
        SceneLoadRequest::Load( 0, false, false, false ),
        SceneLoadPolicyInputs{ m_config,
                               m_launchOptions,
                               m_renderDefaults.CinematicBaseline(),
                               m_startup,
                               m_assets,
                               m_workerPool },
        SceneLoadHostParticipants{ m_timers, m_diagnosticsRuntime, m_simulation },
        SceneLoadInteractionParticipants{ m_inputRouter,
                                          m_interaction,
                                          m_camera,
                                          m_attachedCamera.State(),
                                          m_runtimeTools,
                                          CaptureSceneLoadNavigationState( m_operatorUi->SceneNavigation() ) },
        SceneLoadPresentationParticipants{ m_replayRuntime, *m_overlayDiagnostics, m_renderBackendView, m_renderer },
        sceneLoadOutputs );
    ApplySceneLoadConsumerOutputs( sceneLoadOutputs, m_window, *m_operatorUi, *m_validationHarness, m_launchOptions );
    if ( !m_lastSceneLoadResult.ok )
    {
        return;
    }

    const ReplaySceneTimelineResetInput timelineReset =
        DescribeReplaySceneTimeline( m_sceneController,
                                     m_operatorUi->SceneNavigation().overrides,
                                     m_sceneController.State(),
                                     SkullbonezCore::Core::ActiveSceneObjectCapacity( m_config ),
                                     static_cast<uint32_t>( m_launchOptions.generatedObjectTypeOverride ) );
    ReplaySceneTimelineResetOwners timelineOwners{
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
    const ReplayStartupLoadInput loadInput{
        m_timers.simulationTimer.GetTotalTime(),
        &m_sceneController.Scene().Cameras(),
        m_runtimeTools.MousePickup(),
        NormalizeRuntimeCameraMode( m_camera.mode,
                                    m_sceneController.State().isSceneMode,
                                    RuntimeCameraModeEnabledMask( m_sceneController.State().isSceneMode,
                                                                  m_sceneController.Scene().SceneEntityCount() ) ),
        timelineOwners };
#ifdef _DEBUG
    RuntimeOverlayPresentationEdit presentationEdit = m_overlayDiagnostics->EditPresentation();
    ReplaySolverSampleRestoreContext probeSample{ m_sceneController.Scene(),
                                                  m_sceneController.State(),
                                                  m_renderer,
                                                  presentationEdit.State(),
                                                  m_runtimeTools };
    const ReplayRestoreTransaction probeTransaction{ probeSample, m_diagnosticsRuntime, timelineReset, timelineOwners };
    const ReplayArtifactTopologyOwners probeTopology{ m_simulation,
                                                      m_config,
                                                      m_assets,
                                                      m_workerPool,
                                                      m_operatorUi->SceneNavigation().overrides,
                                                      m_launchOptions.generatedObjectTypeOverride,
                                                      SkullbonezCore::Core::ActiveSceneObjectCapacity( m_config ) };
    const ReplayStartupResult replayStartup = m_replayRuntime.RunStartupWorkflows(
        loadInput,
        probeTransaction,
        probeTopology,
        m_runtimeTools.MousePickup(),
        NormalizeRuntimeCameraMode( m_camera.mode,
                                    m_sceneController.State().isSceneMode,
                                    RuntimeCameraModeEnabledMask( m_sceneController.State().isSceneMode,
                                                                  m_sceneController.Scene().SceneEntityCount() ) ),
        m_timers.simulationTimer.GetTotalTime() );
#else
    const ReplayStartupResult replayStartup = m_replayRuntime.RunStartupWorkflows( loadInput );
#endif
    if ( !replayStartup.status.ok )
    {
        m_lastSceneLoadResult = replayStartup.status;
        return;
    }
    m_skipExecute = replayStartup.skipExecute;
#if defined( SKULLBONEZ_DEVELOPMENT_TOOLS )
    // Scene-authored legacy window defaults run during load. Reapply the
    // selected surface so an inactive implementation cannot become a second input owner.
    ApplyDevelopmentUiMode();
#endif
}


#if defined( SKULLBONEZ_DEVELOPMENT_TOOLS )
void Run::ApplyDevelopmentUiMode()
{
    // Initialize once from CLI, then preserve an explicit hot swap across
    // scene/replay loads rather than treating scene defaults as UI authority.
    m_imguiEditor.InitializeSurfaceSelection( m_launchOptions.developmentUiMode );
    if ( !m_launchOptions.developmentUiModeExplicit && !m_imguiEditor.HasActivatedSurfaceSelection() )
    {
        // Invariant: an omitted selector chooses the Legacy implementation but
        // preserves the scene-authored Legacy visibility default. This keeps
        // ordinary launches and capture baselines stable while ImGui stays dormant.
        m_imguiEditor.SetVisible( false );
        return;
    }
    SelectDevelopmentUiSurface( m_imguiEditor.SelectedSurface() );
}

void Run::SelectDevelopmentUiSurface( DevelopmentUiMode surface )
{
    // Invariant: deactivate the source before activating the target. The two
    // implementations coexist in the build but never own focus in one instant.
    if ( DevelopmentUiModeShowsLegacy( surface ) )
    {
        m_imguiEditor.SelectSurface( DevelopmentUiMode::Legacy );
        if ( !m_operatorUi->IsVisible() )
        {
            m_operatorUi->SetVisible( true, 0.0 );
        }
        return;
    }

    if ( m_operatorUi->IsVisible() )
    {
        m_operatorUi->SetVisible( false, 0.0 );
    }
    m_imguiEditor.SelectSurface( DevelopmentUiMode::ImGui );
}
#endif


const SkullbonezCore::Core::SbResult& Run::LastSceneLoadResult() const
{
    return m_lastSceneLoadResult;
}


SkullbonezCore::Core::SbResult Run::RunSceneLoadOnly( const char* snapshotOutPath )
{
    const int sceneCount = m_sceneController.QueueSize();
    if ( sceneCount <= 0 )
    {
        printf( "[scene-load-only] Exiting because --scene-load-only was requested, but no scenes were queued.\n" );
        fflush( stdout );
        return SkullbonezCore::Core::SbResult::Success();
    }
    if ( !m_lastSceneLoadResult.ok )
    {
        return m_lastSceneLoadResult;
    }

    const bool writeSnapshot = snapshotOutPath && snapshotOutPath[0] != '\0';
    if ( writeSnapshot && sceneCount != 1 )
    {
        return SkullbonezCore::Core::SbResult::Failure( "Runtime/SceneLoadOnly",
                                                        "--scene-snapshot-out requires exactly one loaded scene." );
    }

    printf( "[scene-load-only] Loaded 1/%d: %s\n",
            sceneCount,
            m_sceneController.PathAt( 0 ).empty() ? "generated" : m_sceneController.PathAt( 0 ).c_str() );
    if ( writeSnapshot )
    {
        // Lifetime: scene-load-only borrows owner arrays only until the
        // synchronous snapshot write completes.
        const auto& joints = Physics::PhysicsEngine::ReadPointJointConstraints( m_sceneController.Scene().Physics() );
        const SceneSaveView saveView{ m_sceneController.Scene().Entities(),
                                      m_sceneController.Scene().BodyStore(),
                                      m_sceneController.Scene().Colliders(),
                                      joints.data(),
                                      static_cast<int>( joints.size() ),
                                      m_sceneController.Scene().Environment().GetGravity(),
                                      m_sceneController.Scene().Environment().GetFluidSurfaceHeight(),
                                      m_sceneController.Scene().Environment().GetFluidDensity(),
                                      m_sceneController.Scene().Environment().GetMutualGravitySettings() };
        const RunDebugState presentation = m_overlayDiagnostics->PresentationSnapshot();
        const SceneSaveRequest saveRequest{ snapshotOutPath,
                                            m_sceneController.Scene().Cameras().GetCameraTranslation(),
                                            m_sceneController.Scene().Cameras().GetCameraView(),
                                            m_sceneController.Scene().Cameras().GetCameraUp(),
                                            m_sceneController.State().isScenePhysics,
                                            m_sceneController.State().isSceneText,
                                            m_sceneController.State().isEditableScene,
                                            m_sceneController.State().isFixedStep,
                                            presentation.isWaterHidden,
                                            presentation.isTerrainHidden,
                                            m_sceneController.State().hasFlatSlope,
                                            m_sceneController.State().flatBaseY,
                                            m_sceneController.State().flatSlopeX,
                                            m_sceneController.State().flatSlopeZ };
        const SkullbonezCore::Core::SbResult saveResult = SceneSnapshotWriter::Save( saveView, saveRequest );
        if ( !saveResult.ok )
        {
            return saveResult;
        }
        printf( "[scene-load-only] Snapshot written: %s\n", snapshotOutPath );
    }
    for ( int i = 1; i < sceneCount; ++i )
    {
        SceneLoadConsumerOutputs sceneLoadOutputs;
        const SkullbonezCore::Core::SbResult loadResult = m_sceneController.Load(
            SceneLoadRequest::Load( i, false, false, false ),
            SceneLoadPolicyInputs{ m_config,
                                   m_launchOptions,
                                   m_renderDefaults.CinematicBaseline(),
                                   m_startup,
                                   m_assets,
                                   m_workerPool },
            SceneLoadHostParticipants{ m_timers, m_diagnosticsRuntime, m_simulation },
            SceneLoadInteractionParticipants{ m_inputRouter,
                                              m_interaction,
                                              m_camera,
                                              m_attachedCamera.State(),
                                              m_runtimeTools,
                                              CaptureSceneLoadNavigationState( m_operatorUi->SceneNavigation() ) },
            SceneLoadPresentationParticipants{ m_replayRuntime,
                                               *m_overlayDiagnostics,
                                               m_renderBackendView,
                                               m_renderer },
            sceneLoadOutputs );
        ApplySceneLoadConsumerOutputs( sceneLoadOutputs,
                                       m_window,
                                       *m_operatorUi,
                                       *m_validationHarness,
                                       m_launchOptions );
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
    return SkullbonezCore::Core::SbResult::Success();
}
