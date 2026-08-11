/*
File: SkullbonezSource/Runtime/App/Run.cpp
Purpose:
  Coordinates the main game loop and high-level runtime lifecycle.

Summary:
  Run constructs process-lifetime owners, sequences fixed frame and teardown
  phases, and passes typed values between them without absorbing scene,
  rendering, replay, input, or UI business state.

Glossary:
  Process-end capacity table: Final active-scene store rows emitted before
    subsystem teardown.

Invariants:
  - Backend-owned render resources must be released while the renderer backend
    is still alive, after a GPU flush, and in the explicit release order below.
  - Final capacity rows are reported while the active scene stores still exist.

Related:
  - SkullbonezSource/Runtime/App/Run.h
  - SkullbonezSource/Runtime/App/RunRender.cpp
  - Agentic/Reference/runtime-reference.md
  - Agentic/Reference/engine-glossary.md
*/
#include "Run.h"
#include "../Diagnostics/RuntimeOverlayDiagnostics.h"
#include "../Automation/RuntimeValidationHarness.h"
#include "../Camera/RuntimeCameraMode.h"
#include "InputFrame.h"
#include "Window.h"
#include "../../Core/WindowConstants.h"
#include "../Replay/ReplayRestoreTransactions.h"
#include "../Tools/RuntimeFileWriter.h"
#include "../../Core/Allocation/RuntimeAllocationTracker.h"
#include "../../Core/Allocation/RuntimeReserveAllocator.h"
#include "../../Core/SbDiagnosticStore.h"
#include "../Scene/SceneSaveOperations.h"
#include "../Scene/SceneLoadTransaction.h"
#include "../Diagnostics/SceneMemoryDiagnostics.h"
#include "../../Core/FatalError.h"
#include "../../Core/Log.h"
#include "../../Physics/PhysicsTimestep.h"
#include "../../Rendering/DX12/Dx12Diagnostics.h"
#include "../../UI/UI.h"
#include "../../World/Terrain.h"

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
using SkullbonezCore::Geometry::Terrain;
using SkullbonezCore::Geometry::XZBounds;
using SkullbonezCore::UI::InGameUITab;
namespace CoreAllocation = SkullbonezCore::Core::Allocation;

namespace
{
constexpr std::size_t REPLAY_LAUNCHER_LASER_SHOT_CAPACITY = 32;

// Why: the scrubber auto-hides on an idle timer. Startup arming holds it long
// enough for an operator to see the armed predict/target state before the bar
// fades, without pinning it open for the session.
constexpr double REPLAY_STARTUP_PREDICTION_SCRUBBER_HOLD_SECONDS = 5.0;

// Why: the load frame still settles scene stores and physics enablement. Arming
// on a settled frame instead is what keeps the request from being spent while
// prediction would decline it; see the hazard note in
// Run::ApplyStartupPredictionRequest.
constexpr int REPLAY_STARTUP_PREDICTION_ARM_FRAME = 5;

std::unique_ptr<SkullbonezCore::UI::InGameUI> CreateOperatorUiForStartup( SkullbonezCore::Core::Profiler* profiler )
{
    CoreAllocation::RuntimeAllocationScope allocationScope( CoreAllocation::RuntimeAllocationPhase::Startup );

    // Runtime allocation policy: the cohesive UI owner must remain opaque to Run.h so
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

    if ( launch.replayGuideArcsAtStartup )
    {
        target.replayGuideArcsAtStartup = true;
    }

    target.dumpTextureAssets = launch.dumpTextureAssets;

    if ( launch.predictTargetName[0] != '\0' )
    {

        // Why: this policy is copied, not merged. A later scene load reapplies
        // launch policy through the same call, and Run's one-shot latch — not a
        // cleared name — is what stops the request from re-arming.
        strcpy_s( target.predictTargetName, sizeof( target.predictTargetName ), launch.predictTargetName );
        target.predictHorizonSeconds = launch.predictHorizonSeconds;
        target.predictPauseOnStart = launch.predictPauseOnStart;
    }

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


bool ConfigureStartupReplayRecording( const RunStartupOverrides& overrides, ReplayRuntime& replayRuntime,
                                      int sceneObjectCapacity )
{
    if ( !overrides.configureReplayRecording )
    {
        return false;
    }

    // Runtime allocation policy: launcher replay visuals are copied every
    // captured physics tick, so keep their scratch vectors reserved before the
    // replay phase begins.

    const ReplayRecordingActivationResult replayActivation = replayRuntime
                                                                 .ConfigureRecording( overrides.replayRecordingEnabled,
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
                replayConfig.presentationConfig.hashLogPath.empty() ? ""
                                                                    : replayConfig.presentationConfig.hashLogPath.c_str(),
                replayConfig.solverConfig.hashLogPath.empty() ? "" : " solver_hash_log=",
                replayConfig.solverConfig.hashLogPath.empty() ? "" : replayConfig.solverConfig.hashLogPath.c_str() );
    }

    return replayActivation.exitInspectionCamera;
}


#ifdef _DEBUG
void ApplyStartupDiagnosticsPolicy( const RunStartupOverrides& overrides, DiagnosticsRuntime& diagnosticsRuntime,
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
        diagnosticsRuntime.SetPhysicsDiagnosticsPath( physics, overrides.physicsDiagnosticsPath,
                                                      overrides.physicsDiagnosticsFixedStepForced );
    }
}
#endif

} // namespace


void RunStartupState::ApplyStartupConfig( const SkullbonezCore::Core::EngineConfig& config )
{
    sceneObjectCapacity = std::clamp( config.runtimeCapacity.sceneObjectCapacity, 1,
                                      SkullbonezCore::Scene::Capacity::MAX_SCENE_OBJECTS );

    workerThreads = config.runtimeCapacity.workerThreads;
}


Run::Run( SkullbonezCore::Core::SbDiagnosticStore& resultDiagnostics, Window& window, std::vector<std::string> sceneQueue,
          SkullbonezCore::Core::EngineConfig& config, Threading::WorkerPool& workerPool,
          SkullbonezCore::Core::Profiler* profiler, Rendering::Dx12BackbufferCapture& backbufferCapture,
          SkullbonezCore::Core::DevelopmentTools::TracyClientOwner* tracyClientOwner )
    : m_resultDiagnostics( resultDiagnostics ), m_window( window ), m_workerPool( workerPool ), m_config( config ),
      m_profiler( profiler ), m_tracyClientOwner( tracyClientOwner ),
      m_sceneController( resultDiagnostics, std::move( sceneQueue ) ), m_applicationExit( resultDiagnostics ),
      m_renderDefaults( resultDiagnostics ), m_diagnosticsRuntime( resultDiagnostics ), m_inputRouter( resultDiagnostics ),
#if defined( SKULLBONEZ_AUTOMATION_DIAGNOSTICS )
      m_interactionAutomation( resultDiagnostics ),
#endif
      m_replayRuntime( resultDiagnostics, profiler ), m_runtimeTools( resultDiagnostics ),
#if defined( SKULLBONEZ_DEVELOPMENT_TOOLS )
      m_imguiEditor( resultDiagnostics ),
#endif
      m_operatorUi( CreateOperatorUiForStartup( profiler ) ),
      m_overlayDiagnostics( RuntimeOverlayDiagnostics::CreateForStartup( profiler ) ),
      m_validationHarness( RuntimeValidationHarness::CreateForStartup( resultDiagnostics ) ),
      m_backbufferCapture( backbufferCapture )
{
    const SkullbonezCore::Core::EngineConfig& cfg = m_config;
    m_diagnosticsRuntime.BindProfiler( profiler );
    m_sceneController.Scene().Physics().BindProfiler( profiler );
    m_sceneController.Scene().Cameras().ApplyMovementSettings( BuildCameraMovementSettings( cfg ) );
    m_operatorUi->SceneNavigation().RefreshBrowserList();
    m_sceneController.Scene().ApplyRuntimeConfig( cfg );
    m_renderDefaults.CaptureStartupCinematicBaseline( cfg.cinematicRender );
    m_startup.ApplyStartupConfig( cfg );
}

SkullbonezCore::Core::SbResult
Run::BindRenderBackend( Rendering::Dx12RenderDevice& renderDevice, Rendering::Dx12FrameOwner& renderFrame,
                        Rendering::Dx12GraphTransientPool& renderGraph, Rendering::Dx12ResourceBuilder& renderResources,
                        Rendering::Dx12TextureOwner& renderTextures, Rendering::Dx12GeometryOwner& renderGeometry,
                        Rendering::Dx12Diagnostics& renderDiagnostics, Rendering::Dx12RaytracingOwner& raytracing,
                        bool raytracingAvailable,
                        std::optional<std::reference_wrapper<Rendering::Dx12ShaderDevelopment>> shaderDevelopment
#if defined( SKULLBONEZ_DEVELOPMENT_TOOLS )
                        ,
                        Rendering::Dx12ImGuiRendererOwner& developmentUiRenderer
#endif
)
{
    m_renderer = std::make_unique<RuntimeRenderer>( m_resultDiagnostics, renderDevice, renderFrame, renderGraph,
                                                    renderResources, renderTextures, renderGeometry, renderDiagnostics,
                                                    raytracing, raytracingAvailable,
                                                    RenderWorldView { m_assets, m_sceneController.Scene().Cameras(),
                                                                      m_sceneController.Scene().Terrain(), m_window,
                                                                      m_config, m_sceneController.Scene().Environment(),
                                                                      m_overlayDiagnostics->RenderResources(), m_profiler },
                                                    m_sceneController.State() );

    m_shaderDevelopment = shaderDevelopment;
    Renderer().SetVsyncEnabled( m_config.runtimeRender.vsyncEnabled );
    Renderer().SetPipelineSyncEnabled( m_config.runtimeRender.forcePipelineSync );
#if defined( SKULLBONEZ_DEVELOPMENT_TOOLS )
    const SkullbonezCore::Core::SbResult imguiStartResult = m_imguiEditor.Start( m_window.NativeWindowHandle(),
                                                                                 &developmentUiRenderer );

    if ( !imguiStartResult.Ok() )
    {
        m_applicationExit.RequestOwnedFailure( imguiStartResult );
        return imguiStartResult;
    }

    m_window.BindDevelopmentUiInput( m_imguiEditor );
#endif
    return SkullbonezCore::Core::SbResult::Success();
}


Run::~Run()
{
    CoreAllocation::RuntimeAllocationScope allocationScope( CoreAllocation::RuntimeAllocationPhase::Shutdown );
    CancelPendingLookLabSave( "shutdown cancelled screenshot" );
    const std::string* currentScenePath = m_sceneController.CurrentPath();
    m_diagnosticsRuntime.ReportStoreCapacityRows( m_sceneController.State(),
                                                  currentScenePath ? currentScenePath->c_str() : nullptr, "process_end" );

#ifdef _DEBUG
    m_diagnosticsRuntime.EndPhysicsDiagnosticsRun( m_sceneController.State(), "process_end" );
#endif

    if ( m_diagnosticsRuntime.MainMemoryDumpRequested() )
    {
        m_diagnosticsRuntime
            .WriteMainMemoryDump( m_replayRuntime.CollectMemoryStats(),
                                  CollectSceneMemoryStats( SceneMemoryDiagnosticsView { m_sceneController.Scene().Entities(),
                                                                                        m_sceneController.Scene().CollectGameplayMemoryBytes(),
                                                                                        m_sceneController.Scene()
                                                                                            .CollectGameplayDebugMemoryBytes(),
                                                                                        m_sceneController.Scene().Physics(),
                                                                                        m_sceneController.Scene().RenderInstances() } ),
                                  m_sceneController.State(), "shutdown", m_timers.simulationTimer.GetTotalTime() );
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
    const SkullbonezCore::Core::SbResult releaseResult = Renderer().ReleaseBackendOwnedRuntimeResources( RuntimeRenderer::BackendResourceReleaseContext { "shutdown_release", *m_operatorUi, m_runtimeTools } );

    if ( !releaseResult.Ok() )
    {

        // Lane F: a destructor cannot propagate Lane R to a caller, and letting
        // member destruction continue after an uncertain GPU drain is unsafe.
        SB_FATAL( "Runtime/Run", "Backend resource release could not establish GPU safety. owner=%s reason=%s",
                  releaseResult.ErrorOwner()[0] != '\0' ? releaseResult.ErrorOwner() : "Rendering/DX12",
                  releaseResult.ErrorMessage()[0] != '\0' ? releaseResult.ErrorMessage() : "GPU drain failed" );
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

    if ( launch.replayGuideArcsAtStartup )
    {

        // Why: startup overrides are applied after the initial scene transaction;
        // later scene loads reapply the same explicit request after activation.
        m_replayRuntime.SetGuideArcsEnabled( true );
    }

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
        SceneWorld& sceneWorld = m_sceneController.Scene();
        const SceneSessionState& sceneState = m_sceneController.State();
        const ReplayInputView replayInput = m_replayRuntime.BuildInputView();
        const int sceneEntityCount = sceneWorld.SceneEntityCount();
        const uint32_t cameraModeEnabledMask = RuntimeCameraModeEnabledMask( sceneState.isSceneMode, sceneEntityCount );
        const RunCameraMode normalizedRestoreMode = NormalizeRuntimeCameraMode( replayInput.restoreCameraMode,
                                                                                sceneState.isSceneMode,
                                                                                cameraModeEnabledMask );

        m_replayRuntime.ExitInspectionCamera( &sceneWorld.Cameras(), sceneWorld.Terrain().Get(), m_camera,
                                              normalizedRestoreMode, m_attachedCamera.State().activeFollow,
                                              m_camera.director.grabbed, m_interaction, m_inputRouter );
    }

    m_replayRuntime.ConfigureStartupWorkflows( ReplayStartupRequest { overrides.replayLoadPath, overrides.replayLoadProbe,
                                       #ifdef _DEBUG
                                                                      overrides.replayRestoreFileProbePath, overrides.replayRestoreTargetFileProbePath,
                                                                      overrides.replayRestoreBranchFileProbePath, overrides.replayRestoreFailureFileProbePath,
                                                                      overrides.replayScrubProbe, overrides.replayScrubProbeNormalized,
                                                                      overrides.replayRestoreProbe, overrides.replayRestoreProbeNormalized,
                                                                      overrides.replaySaveProbe, overrides.replaySaveProbePath
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

    if ( !result.Ok() )
    {
        const ReplayAutomationView replay = m_replayRuntime.BuildAutomationView();
        (void)m_interactionAutomation.reportWriter.Write( m_interactionAutomation.status, m_interactionAutomation.scriptPath,
                                                          m_sceneController.Scene(), m_sceneController.State(),
                                                          m_sceneController.CurrentPath()
                                                              ? m_sceneController.CurrentPath()->c_str()
                                                              : nullptr,
                                                          m_runtimeTools, replay, m_interaction, m_camera, *m_operatorUi,
                                                          Renderer().FrameGraphSnapshot() );
    }

    return result;
#else

    // Lane R: interaction scripts are external validation input. Ordinary game
    // builds reject them instead of linking the diagnostic controller into the
    // frame loop; tools must use the dedicated Automation configuration.
    return m_resultDiagnostics.Failure( "InteractionAutomation", "--interaction-script requires an Automation|x64 build." );
#endif
}


void Run::ApplyStartupPredictionRequest()
{
    if ( m_startupPredictionApplied || m_launchOptions.predictTargetName[0] == '\0' )
    {
        return;
    }

    const SceneSessionState& sceneState = m_sceneController.State();

    // Hazard: BeginFrameSource consumes the dirty token before it checks scene
    // physics, so arming into a scene that is not simulating yet clears the
    // rebuild request and the horizon never starts. Wait for a simulating scene
    // that has run a frame rather than arming on the load frame.
    if ( !sceneState.isScenePhysics || sceneState.currentFrame < REPLAY_STARTUP_PREDICTION_ARM_FRAME )
    {
        return;
    }

    SceneWorld& sceneWorld = m_sceneController.Scene();
    const int modelIndex = sceneWorld.Entities().FindByDisplayName( m_launchOptions.predictTargetName );
    const PhysicsBodyRecord* body = modelIndex >= 0 ? sceneWorld.BodyStore().RecordForModelIndex( modelIndex ) : nullptr;

    // Lane R: --predict names external launch input. A scene whose bodies are
    // still loading simply retries next frame; only a scene that has finished
    // loading without the name is a reportable operator mistake.
    if ( !body || !body->sceneObjectId.IsValid() )
    {
        if ( sceneWorld.SceneEntityCount() > 0 )
        {
            m_startupPredictionApplied = true;
            fprintf( stdout, "[predict] No scene object named \"%s\"; startup prediction not armed.\n",
                     m_launchOptions.predictTargetName );
        }

        return;
    }

    // Concept: prediction seeds its private engine from the live physics stores,
    // not from the recorded solver track, so arming does not wait for capture.
    // The solver track only supplies the source frame/hash used to decide when a
    // committed future is stale; a null latest sample degrades to source 0.
    ReplayFrameIntent intent;
    intent.setScrubberVisibility = true;
    intent.scrubberVisible = true;
    intent.scrubberNow = m_timers.simulationTimer.GetTotalTime();
    intent.scrubberHoldSeconds = REPLAY_STARTUP_PREDICTION_SCRUBBER_HOLD_SECONDS;
    intent.setPredictionEnabled = true;
    intent.predictionEnabled = true;
    intent.setPredictionHorizon = m_launchOptions.predictHorizonSeconds > 0.0f;
    intent.predictionHorizonSeconds = m_launchOptions.predictHorizonSeconds;
    intent.setPathTarget = true;
    intent.pathTargetId = body->sceneObjectId;
    intent.pathTargetModelRow.value = modelIndex;
    strncpy_s( intent.pathTargetName, sizeof( intent.pathTargetName ), m_launchOptions.predictTargetName, _TRUNCATE );
    (void)m_replayRuntime.ApplyFrameIntent( intent );

    // Invariant: the replay workspace owns world input while prediction is the
    // active tool. Without this the armed target draws but pointer gestures
    // still belong to the previous owner, which is not the state the operator
    // reaches by clicking predict.
    (void)m_interaction.SetWorldInteractionOwnerInWorkspace( RuntimeWorkspace::Replay,
                                                             WorldInteractionOwner::ReplayPrediction,
                                                             InteractionExitReason::EnterReplay );

    // Hazard: cross-scene pause freezes SceneSessionState::currentFrame, which is
    // the counter --frames completes against. Pausing a frame-limited run would
    // hang the process instead of exiting, so a frame limit wins over the pause
    // request and says so rather than failing silently.
    const bool frameLimited = m_launchOptions.frameCountOverride > 0;
    const bool pauseScene = m_launchOptions.predictPauseOnStart && !frameLimited;

    if ( pauseScene && !m_sceneController.CrossScenePauseLocked() )
    {

        // Why: a paused scene stops changing the solver source, so the horizon
        // builds once and holds instead of restarting every frame.
        // --predict-running keeps the scene advancing for sustained worker load.
        m_sceneController.ToggleCrossScenePause();
    }

    m_startupPredictionApplied = true;
    fprintf( stdout, "[predict] Armed prediction on \"%s\" (model row %d, %s).\n", m_launchOptions.predictTargetName,
             modelIndex,
             pauseScene ? "paused"
                        : ( m_launchOptions.predictPauseOnStart ? "running; pause skipped under --frames" : "running" ) );
}


void Run::Initialise()
{

    // Why: timers default to inert storage so Run construction cannot throw
    // before the startup reporter exists. Initialise them at this boundary and
    // return platform counter failures through the normal Lane R process path.
    const SkullbonezCore::Core::SbResult timerStartupResult = m_timers.Initialise( m_resultDiagnostics );

    if ( !timerStartupResult.Ok() )
    {
        m_lastSceneLoadResult = timerStartupResult;
        return;
    }

    assert( m_renderer && "Run requires a renderer before Initialise()" );
    auto& renderResources = Renderer().RenderResources();
    const SkullbonezCore::Rendering::Dx12Diagnostics& renderDiagnostics = Renderer().RenderDiagnostics();

    const char* rendererName = renderDiagnostics.GetRendererName();
    char titleText[256];
    sprintf_s( titleText, "%s [%s] -- LOADING!!!", TITLE_TEXT, rendererName );
    m_window.SetTitleText( titleText );
    const SkullbonezCore::Core::EngineConfig& cfg = m_config;

    // Build renderer-owned resources from source asset records.
    const SkullbonezCore::Core::SbResult rebuildResourcesResult = Renderer().ResourceLifecycle().InitialiseProcessResources( m_launchOptions.dumpTextureAssets );

    if ( !rebuildResourcesResult.Ok() )
    {
        m_lastSceneLoadResult = rebuildResourcesResult;
        return;
    }

    const std::string terrainRawPath = m_assets.RegisterSourceAssetPath( SkullbonezCore::Assets::AssetKind::Terrain,
                                                                         "terrain.raw", cfg.assetPaths.terrainRaw.c_str() );

    std::unique_ptr<Terrain> startupTerrain;
    const SkullbonezCore::Core::SbResult startupTerrainResult = Terrain::TryCreateFromHeightMap( m_resultDiagnostics,
                                                                                                 terrainRawPath.c_str(), 256,
                                                                                                 8, 15, m_config, m_assets,
                                                                                                 renderResources,
                                                                                                 startupTerrain );

    if ( !startupTerrainResult.Ok() )
    {
        m_lastSceneLoadResult = startupTerrainResult;
        return;
    }

    m_sceneController.Scene().ReplaceTerrain( std::move( startupTerrain ), false );

    m_sceneController.Scene().Environment() = WorldEnvironment( cfg.worldForces.fluidHeight, cfg.worldForces.fluidDensity,
                                                                cfg.worldForces.gasDensity, cfg.worldForces.gravity );

    m_sceneController.Scene().Environment().BindRenderContexts( m_config, m_assets, renderResources );
    XZBounds tb = m_sceneController.Scene().Terrain().Get()->GetXZBounds();
    m_sceneController.Scene().Environment().SetTerrainBounds( tb.m_xMin, tb.m_xMax, tb.m_zMin, tb.m_zMax );

    // Why: SDF atlas generation is a startup asset/tooling boundary. Report it
    // as Lane R before scene loading instead of throwing through Run startup.
    const SkullbonezCore::Core::SbResult
        uiTextResourceResult = Renderer().ResourceLifecycle().EnsureUiTextResources( cfg.window.screenX,
                                                                                     cfg.window.screenY );

    if ( !uiTextResourceResult.Ok() )
    {
        m_lastSceneLoadResult = uiTextResourceResult;
        return;
    }

    SceneLoadNavigationState sceneLoadNavigation = CaptureSceneLoadNavigationState( m_operatorUi->SceneNavigation() );

    SceneLoadTransaction sceneLoad;
    sceneLoad.CaptureSubmittedState( m_camera, sceneLoadNavigation, m_overlayDiagnostics->PresentationSnapshot(),
                                     Renderer().RendererName(), m_timers.simulationTimer.GetTotalTime() );

    m_lastSceneLoadResult = sceneLoad.Load( m_sceneController, SceneLoadRequest::Load( 0, false, false, false ), m_config,
                                            m_launchOptions, m_renderDefaults.CinematicBaseline(), m_startup, m_assets,
                                            m_workerPool, m_diagnosticsRuntime, &Renderer().RenderFrame(),
                                            &Renderer().RenderResources(), Renderer() );

    sceneLoad.ApplyRuntimeReactions( m_launchOptions, m_timers, *m_overlayDiagnostics, m_sceneController, m_inputRouter,
                                     m_interaction, m_camera, m_attachedCamera, m_runtimeTools, m_replayRuntime );

    sceneLoad.ApplyPresentationOutputs( m_window, *m_operatorUi, *m_validationHarness, m_launchOptions,
                                        &Renderer().RenderDevice(), Renderer().VsyncEnabled(), m_sceneController );

    if ( !m_lastSceneLoadResult.Ok() )
    {
        return;
    }

    const int sceneObjectCapacity = SkullbonezCore::Core::ActiveSceneObjectCapacity( m_config );
    SceneWorld& sceneWorld = m_sceneController.Scene();
    SceneSessionState& sceneState = m_sceneController.State();
    SkullbonezCore::UI::RunSceneUIOverrideState& sceneOverrides = m_operatorUi->SceneNavigation().overrides;
    GeneratedObjectTypeOverride& generatedObjectTypeOverride = m_launchOptions.generatedObjectTypeOverride;
    const uint32_t generatedObjectTypeOverrideBits = static_cast<uint32_t>( generatedObjectTypeOverride );
    const bool sceneMode = sceneState.isSceneMode;
    const int sceneEntityCount = sceneWorld.SceneEntityCount();
    const uint32_t cameraModeEnabledMask = RuntimeCameraModeEnabledMask( sceneMode, sceneEntityCount );

    const ReplayInputView replayInput = m_replayRuntime.BuildInputView();
    const RunCameraMode normalizedRestoreMode = NormalizeRuntimeCameraMode( replayInput.restoreCameraMode, sceneMode,
                                                                            cameraModeEnabledMask );

    const RunCameraMode normalizedCameraMode = NormalizeRuntimeCameraMode( m_camera.mode, sceneMode, cameraModeEnabledMask );

    const ReplaySceneTimelineResetInput timelineReset = DescribeReplaySceneTimeline( m_sceneController, sceneOverrides,
                                                                                     sceneState, sceneObjectCapacity,
                                                                                     generatedObjectTypeOverrideBits );

    const ReplayStartupLoadInput loadInput { m_timers.simulationTimer.GetTotalTime(),
                                             &sceneWorld.Cameras(),
                                             m_runtimeTools.MousePickup(),
                                             normalizedCameraMode,
                                             m_inputRouter,
                                             m_interaction,
                                             sceneWorld.Terrain().Get(),
                                             m_camera,
                                             normalizedRestoreMode,
                                             m_attachedCamera.State().activeFollow,
                                             m_camera.director.grabbed };

#ifdef _DEBUG
    RuntimeOverlayPresentationEdit presentationEdit = m_overlayDiagnostics->EditPresentation();
    const ReplayStartupResult replayStartup = m_replayRuntime.RunStartupWorkflows( loadInput, m_sceneController,
                                                                                   m_diagnosticsRuntime,
                                                                                   presentationEdit.State(), m_runtimeTools,
                                                                                   m_simulation, m_config, m_assets,
                                                                                   m_workerPool, sceneOverrides,
                                                                                   generatedObjectTypeOverride );

#else
    const ReplayStartupResult replayStartup = m_replayRuntime.RunStartupWorkflows( loadInput );
#endif

    if ( !replayStartup.status.Ok() )
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

    if ( !m_lastSceneLoadResult.Ok() )
    {
        return m_lastSceneLoadResult;
    }

    const bool writeSnapshot = snapshotOutPath && snapshotOutPath[0] != '\0';

    if ( writeSnapshot && sceneCount != 1 )
    {
        return m_resultDiagnostics.Failure( "Runtime/SceneLoadOnly",
                                            "--scene-snapshot-out requires exactly one loaded scene." );
    }

    printf( "[scene-load-only] Loaded 1/%d: %s\n", sceneCount,
            m_sceneController.PathAt( 0 ).empty() ? "generated" : m_sceneController.PathAt( 0 ).c_str() );

    if ( writeSnapshot )
    {
        const OverlayDebugState presentation = m_overlayDiagnostics->PresentationSnapshot();
        const SkullbonezCore::Core::SbResult
            saveResult = SaveSceneLoadOnlySnapshot( m_resultDiagnostics, snapshotOutPath,
                                                    m_sceneController.Scene().GetSaveState(),
                                                    m_sceneController.State().GetSaveState(), presentation.GetSaveState() );

        if ( !saveResult.Ok() )
        {
            return saveResult;
        }

        printf( "[scene-load-only] Snapshot written: %s\n", snapshotOutPath );
    }

    for ( int i = 1; i < sceneCount; ++i )
    {
        SceneLoadTransaction sceneLoad;
        sceneLoad.CaptureSubmittedState( m_camera, CaptureSceneLoadNavigationState( m_operatorUi->SceneNavigation() ),
                                         m_overlayDiagnostics->PresentationSnapshot(), Renderer().RendererName(),
                                         m_timers.simulationTimer.GetTotalTime() );

        const SkullbonezCore::Core::SbResult loadResult = sceneLoad.Load( m_sceneController,
                                                                          SceneLoadRequest::Load( i, false, false, false ),
                                                                          m_config, m_launchOptions,
                                                                          m_renderDefaults.CinematicBaseline(), m_startup,
                                                                          m_assets, m_workerPool, m_diagnosticsRuntime,
                                                                          &Renderer().RenderFrame(),
                                                                          &Renderer().RenderResources(), Renderer() );

        sceneLoad.ApplyRuntimeReactions( m_launchOptions, m_timers, *m_overlayDiagnostics, m_sceneController, m_inputRouter,
                                         m_interaction, m_camera, m_attachedCamera, m_runtimeTools, m_replayRuntime );

        sceneLoad.ApplyPresentationOutputs( m_window, *m_operatorUi, *m_validationHarness, m_launchOptions,
                                            &Renderer().RenderDevice(), Renderer().VsyncEnabled(), m_sceneController );

        if ( !loadResult.Ok() )
        {
            return loadResult;
        }

        printf( "[scene-load-only] Loaded %d/%d: %s\n", i + 1, sceneCount,
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
