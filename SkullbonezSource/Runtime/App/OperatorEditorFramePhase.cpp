/*
File: SkullbonezSource/Runtime/App/OperatorEditorFramePhase.cpp
Purpose:
  Sequences the shared operator-editor presentation for one frame.

Summary:
  App synchronously samples domain owners, combines those facts in one bounded
  OperatorEditorFrameView, and returns typed process commands after UI and GPU
  work complete. Runtime/UI owns the snapshot, submission-policy decision,
  presenter-command arbitration, and ordered phase walk.
  Run::RenderOperatorUiPhase is the owner-approved top-level phase coordinator.
  It reaches process-owned members for one ordered UI phase, builds one shared
  value projection, applies the phase owner's submission plan, and retains no
  frame values after returning commands to the frame sequencer.

Glossary:
  Cold detail: Inspector and diagnostics data sampled only while ImGui is shown.
  Late UI pass: Presentation work recorded after the 3D game view.

Invariants:
  - Owner references are borrowed for this call only and never retained.
  - Dense physics rows are used only after typed-handle validation.
  - Diagnostics and inspector snapshots do not grow runtime storage.
  - Both surfaces observe identical scene, replay, and rendering values.

Related:
  - Runtime/App/Run.h owns the private frame-coordinator declaration.
  - Runtime/UI/OperatorUiPhase.h owns the value-only phase walk.
  - Runtime/RuntimeFrameViews.h retains the value-only late-UI facts.
  - Agentic/Reference/engine-glossary.md
*/
#include "Run.h"
#include "../UI/OperatorUiProjection.h"
#include "../Diagnostics/RuntimeOverlayDiagnostics.h"
#include "../Automation/RuntimeValidationHarness.h"
#include "../RuntimeFrameViews.h"
#include "../UI/OperatorUiPhase.h"
#include "../UI/RuntimeViewModel.h"
#include "../Startup/Window.h"
#include "../../Core/WorkerPool.h"
#include "../Planning/ReplayOverlayPackets.h"
#include "../Capture/CaptureSystem.h"
#include "../Scene/SceneCinematicPolicy.h"
#include "../Editor/EditorTools.h"
#include "../../Core/Allocation/RuntimeAllocationTracker.h"
#include "../../Core/Allocation/RuntimeReserveAllocator.h"
#include "../../Core/FatalError.h"
#include "../../Core/Profiler.h"
#include "../../Core/TracyClientOwner.h"
#include "../../Physics/ColliderStore.h"
#include "../../Physics/PhysicsApi.h"
#include "../../Physics/PhysicsEngine.h"
#include "../../Rendering/RenderInstanceStore.h"
#include "../UI/GameUI/UI.h"
#include "../UI/GameUI/UITabEditor.h"

#include <algorithm>
#include <cstdint>
#include <cstring>

using namespace SkullbonezCore::Runtime;
using namespace SkullbonezCore::Math::CollisionDetection;
using namespace SkullbonezCore::Math::Orientation;
using namespace SkullbonezCore::Math::Transformation;
using namespace SkullbonezCore::Physics;
using SkullbonezCore::Math::Vector::Vector3;
namespace CoreAllocation = SkullbonezCore::Core::Allocation;

namespace SkullbonezCore
{
namespace Runtime
{
namespace
{
UiCameraBadgeMode ProjectUiCameraBadgeMode( RunCameraMode mode )
{
    switch ( mode )
    {
    case RunCameraMode::Demo:
    case RunCameraMode::Scene:
    case RunCameraMode::Director:
        return UiCameraBadgeMode::Quiet;
    case RunCameraMode::Attach:
        return UiCameraBadgeMode::Attach;
    case RunCameraMode::Manipulator:
        return UiCameraBadgeMode::Manipulator;
    case RunCameraMode::Launcher:
        return UiCameraBadgeMode::Launcher;
    case RunCameraMode::Inspect:
        return UiCameraBadgeMode::Inspect;
    default:
        return UiCameraBadgeMode::Other;
    }
}

UiOverlayMode ProjectUiOverlayMode( OverlayMode mode )
{
    switch ( mode )
    {
    case OverlayMode::SceneStats:
        return UiOverlayMode::SceneStats;
    case OverlayMode::BarsNormalized:
        return UiOverlayMode::BarsNormalized;
    case OverlayMode::BarsAbsolute:
        return UiOverlayMode::BarsAbsolute;
    case OverlayMode::Keys:
        return UiOverlayMode::Keys;
    case OverlayMode::Timers:
        return UiOverlayMode::Timers;
    case OverlayMode::None:
    default:
        return UiOverlayMode::None;
    }
}

OperatorUiGizmoMode SampleOperatorUiGizmoMode( RuntimeInteractionGestureKind gestureKind,
                                               RuntimeGizmoDragKind gizmoKind ) noexcept
{
    if ( gestureKind != RuntimeInteractionGestureKind::GizmoDrag )
    {
        return OperatorUiGizmoMode::Translate;
    }

    switch ( gizmoKind )
    {
    case RuntimeGizmoDragKind::Rotate:
        return OperatorUiGizmoMode::Rotate;
    case RuntimeGizmoDragKind::Scale:
        return OperatorUiGizmoMode::Scale;
    default:
        return OperatorUiGizmoMode::Translate;
    }
}

RuntimeViewModel SampleRuntimeViewModel( const SceneSessionState& scene, const SceneWorld& world, int sceneCount,
                                         const RunScreenshotState& screenshot, bool presentationInterpolation,
                                         bool presentationPinned, float presentationAlpha )
{
    RuntimeViewModel view;
    const bool screenshotConfigured = screenshot.isScreenshotAndExit || screenshot.screenshotFrame >= 0 ||
                                      screenshot.screenshotMs >= 0 || screenshot.screenshotPath[0] != '\0' ||
                                      screenshot.screenshotInterval > 0;
    view.sceneMode = scene.isSceneMode;
    view.scenePhysics = scene.isScenePhysics;
    view.sceneText = scene.isSceneText;
    view.fixedStep = scene.isFixedStep;
    view.screenshotPending = screenshotConfigured && !screenshot.isScreenshotSaved;
    view.sceneIndex = scene.currentSceneIndex;
    view.sceneCount = sceneCount;
    view.frame = scene.currentFrame;
    view.targetFrameCount = scene.targetFrameCount;
    view.modelCount = PhysicsEngine::ReadBodies( world.Physics() ).Count();
    view.timeScale = scene.timeScale;
    view.presentationInterpolation = presentationInterpolation;
    view.presentationPinned = presentationPinned;
    view.presentationAlpha = std::clamp( presentationAlpha, 0.0f, 1.0f );
    return view;
}

OperatorUiForecastCause SampleOperatorUiForecastCause( ContinuousOrbitalInstabilityCause cause )
{
    using Cause = ContinuousOrbitalInstabilityCause;

    switch ( cause )
    {
    case Cause::InvalidContract:
        return OperatorUiForecastCause::InvalidContract;
    case Cause::NonFiniteState:
        return OperatorUiForecastCause::NonFiniteState;
    case Cause::PrivateStepFailure:
        return OperatorUiForecastCause::PrivateStepFailure;
    case Cause::InvalidPublication:
        return OperatorUiForecastCause::InvalidPublication;
    case Cause::InnerEnvelope:
        return OperatorUiForecastCause::InnerEnvelope;
    case Cause::OuterEnvelope:
        return OperatorUiForecastCause::OuterEnvelope;
    case Cause::SustainedEscape:
        return OperatorUiForecastCause::SustainedEscape;
    case Cause::Collision:
        return OperatorUiForecastCause::Collision;
    case Cause::None:
    default:
        return OperatorUiForecastCause::None;
    }
}

OperatorUiForecastFacts SampleOperatorUiForecastFacts( const ContinuousOrbitalForecastView& forecast )
{
    const bool blockingFailureFirst = forecast.stability.firstBlockingFailure.latched &&
                                      ( !forecast.stability.firstAuxiliaryFailure.latched ||
                                        forecast.stability.firstBlockingFailure.absoluteTick <=
                                            forecast.stability.firstAuxiliaryFailure.absoluteTick );
    const ContinuousOrbitalFailure& firstFailure = blockingFailureFirst ? forecast.stability.firstBlockingFailure
                                                                        : forecast.stability.firstAuxiliaryFailure;
    OperatorUiForecastFacts facts;
    facts.simulatedSeconds = forecast.simulatedSeconds;
    facts.simulatedSecondsPerRealSecond = forecast.simulatedSecondsPerRealSecond;
    facts.rollingWindowAgeSeconds = forecast.rollingWindowAgeSeconds;
    facts.energyDrift = forecast.stability.conservation.energyDrift;
    facts.angularMomentumDrift = forecast.stability.conservation.angularMomentumDrift;
    facts.maximumAbsoluteEnergyDrift = forecast.stability.conservation.maximumAbsoluteEnergyDrift;
    facts.maximumAngularMomentumDrift = forecast.stability.conservation.maximumAngularMomentumDrift;
    facts.firstFailureSeconds = firstFailure.simulatedSeconds;
    facts.newestAbsoluteTick = forecast.newestAbsoluteTick;
    facts.retainedBytes = static_cast<uint64_t>( forecast.retainedBytes );
    facts.firstFailureSubject = firstFailure.subject.value;
    facts.firstFailureOther = firstFailure.other.value;
    facts.firstFailureCause = SampleOperatorUiForecastCause( firstFailure.cause );
    facts.available = forecast.available;
    facts.active = forecast.active;
    facts.workerInFlight = forecast.workerInFlight;
    facts.failed = forecast.failed;
    facts.configured = forecast.stability.configured;
    facts.numericalHealthy = forecast.stability.numericalHealthy;
    facts.systemOrbitalHealthy = forecast.stability.systemOrbitalHealthy;
    facts.auxiliaryOrbitalHealthy = forecast.stability.auxiliaryOrbitalHealthy;
    facts.energyDriftAvailable = forecast.stability.conservation.energyDriftAvailable;
    facts.angularMomentumDriftAvailable = forecast.stability.conservation.angularMomentumDriftAvailable;
    return facts;
}

Core::MainMemoryStats SampleMainMemoryOverlayStats( const DiagnosticsRuntime& diagnosticsRuntime,
                                                    const Core::MainMemoryGameObjectStats& gameObjects )
{
    Core::MainMemoryStats stats = diagnosticsRuntime.MainMemoryStatsSnapshot();
    stats.process = Core::MainMemoryProcessStats {};
    stats.gameObjects = gameObjects;
    stats.trackedEngineBytes = stats.replay.totalBytes + stats.gameObjects.totalBytes + stats.otherTrackedBytes;
    stats.unattributedProcessBytes = 0;
    stats.trackedOvershootBytes = 0;
    stats.reconciledTotalBytes = stats.trackedEngineBytes;
    stats.reconciliationDeltaBytes = 0;
    return stats;
}
} // namespace

void Run::SampleSecondaryOperatorDiagnostics( const RuntimeFrameMetricsSnapshot& frameMetrics,
                                              const RuntimeUiTextFrameFacts& uiTextFacts, const OverlayDebugState& debug,
                                              bool shadowsEnabled, bool cinematicRendering,
                                              const Core::CinematicRenderConfig& cinematic,
                                              RuntimeViewModel& runtimeViewModel,
                                              RuntimeRenderTargetPreviewSnapshot& renderTargetPreviews,
                                              RenderDiagnosticsReadout& renderDiagnosticsReadout,
                                              OperatorUiSecondaryDiagnosticsFacts& facts )
{
    // Why: the secondary surface can be visible while GameUI is hidden. This
    // cold projection samples its bounded diagnostics independently of the GPU
    // submission decision made by RenderOperatorUiPhase.
    runtimeViewModel = SampleRuntimeViewModel( m_sceneController.State(), m_sceneController.Scene(),
                                               m_sceneController.QueueSize(), m_capture.Screenshot(),
                                               m_config.runtimeRender.presentationInterpolation,
                                               uiTextFacts.presentationPinned, uiTextFacts.presentationAlpha );

    renderTargetPreviews = Renderer()
                               .ResourceLifecycle()
                               .BuildRenderTargetPreviewSnapshot( shadowsEnabled, cinematicRendering,
                                                                  cinematicRendering &&
                                                                      cinematic.volumetricLightingEnabled );

    const Core::MainMemoryStats& mainMemory = m_diagnosticsRuntime.MainMemoryStatsSnapshot();
    renderDiagnosticsReadout = Renderer().BuildDiagnosticsReadout();
    facts.rendererName = renderDiagnosticsReadout.rendererName.data();
    facts.drawCalls = renderDiagnosticsReadout.drawCalls;
    facts.uiDrawCalls = frameMetrics.uiDrawCalls;
    facts.workerThreadCount = m_workerPool.GetThreadCount();
    facts.maxWorkerThreadCount = Threading::WorkerPool::MaxThreadCount();
    facts.fps = frameMetrics.rollingFrameSeconds > 0.0f
                    ? 1.0f / frameMetrics.rollingFrameSeconds
                    : ( frameMetrics.secondsPerFrame > 0.0 ? static_cast<float>( 1.0 / frameMetrics.secondsPerFrame )
                                                           : 0.0f );
    facts.renderMs = ( frameMetrics.rollingRenderSeconds > 0.0f ? frameMetrics.rollingRenderSeconds
                                                                : frameMetrics.renderSeconds ) *
                     1000.0f;
    facts.physicsMs = ( frameMetrics.rollingPhysicsSeconds > 0.0f ? frameMetrics.rollingPhysicsSeconds
                                                                  : frameMetrics.physicsSeconds ) *
                      1000.0f;
    facts.cpuFrameMs = frameMetrics.cpuFrameWorkMs;
    facts.gpuFrameMs = frameMetrics.gpuFrameWorkMs;

    facts.physicsDebugFlags = debug.physicsDebugFlags;
    const int stageCount = static_cast<int>( PhysicsPipelineStage::Count );
    int stageIndex = stageCount > 0 ? debug.physicsDebugPipelineStageCursor % stageCount : 0;

    if ( stageIndex < 0 )
    {
        stageIndex += stageCount;
    }

    facts.physicsPipelineStageIndex = stageIndex;
    facts.physicsPipelineStageCount = stageCount;
    facts.physicsPipelineStageName = PhysicsPipelineStageName( static_cast<PhysicsPipelineStage>( stageIndex ) );
    facts.physicsDebugAlpha = debug.physicsDebugAlpha;
    facts.physicsDebugContactLinger = debug.physicsDebugContactLinger;
    facts.rayCastImpulseStrength = m_runtimeTools.RayCastTest().impulseStrength;
    facts.launcherProjectileSpeed = m_runtimeTools.RayCastTest().projectileSpeed;
    facts.collisionVisualizer = debug.isCollisionVisualizer;
    facts.physicsDebugTransparent = debug.isPhysicsDebugTransparent;
    facts.broadphaseOverlay = debug.isBroadphaseOverlay;
    facts.tornadoVisualShell = m_sceneController.Scene().Tornado().VisualSettings().enabled;
    facts.tornadoFieldVectors = m_sceneController.Scene().Tornado().GetFieldConfig().visualizeVelocityField;
    facts.rayCastVisualization = m_runtimeTools.RayCastTest().visualizeRays;
    facts.trackedEngineBytes = mainMemory.trackedEngineBytes;
    facts.reconciledTotalBytes = mainMemory.reconciledTotalBytes;
    facts.uploadUsedBytes = renderDiagnosticsReadout.memory.uploadUsedBytes;
    facts.uploadCapacityBytes = renderDiagnosticsReadout.memory.uploadCapacityBytes;
    facts.replayReserveGrowthEvents = CoreAllocation::RuntimeReserveAllocator::GrowthEventCount();
    facts.renderTargetCount = (std::min)( renderTargetPreviews.count, UI::OPERATOR_EDITOR_RENDER_TARGET_CAPACITY );

    for ( int index = 0; index < facts.renderTargetCount; ++index )
    {
        const RuntimeRenderTargetPreview& source = renderTargetPreviews.targets[static_cast<size_t>( index )];
        facts.renderTargets[static_cast<std::size_t>( index )] = { source.label,
                                                                   source.width,
                                                                   source.height,
                                                                   source.available && source.textureHandle != 0u,
                                                                   source.depth,
                                                                   source.hdr };
    }
}

void Run::ApplyOperatorUiProcessCommands( const OperatorUiProcessCommands& commands )
{
    // App applies native-surface and process effects only after both presenters
    // have released their borrows of the immutable operator snapshot.
#if defined( SKULLBONEZ_DEVELOPMENT_TOOLS )

    if ( commands.surface == OperatorUiSurfaceCommand::ShowGameUi )
    {
        SelectDevelopmentUiSurface( DevelopmentUiMode::GameUI );
    }

    if ( commands.requestTracyStandardCapture )
    {
        bool tracyStarted = false;
#if defined( TRACY_ENABLE )

        if ( m_tracyClientOwner )
        {
            CoreAllocation::RuntimeAllocationScope tracyStartScope( CoreAllocation::RuntimeAllocationPhase::Diagnostics );
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
#else
    (void)commands;
#endif
}

OperatorUiProcessCommands Run::RenderOperatorUiPhase( const RuntimeRenderModelFrameView& renderModels,
                                                      float presentationAlpha, bool capturePresentationPinned,
                                                      double secondsPerFrame, bool gameUiActive,
                                                      const RuntimeFrameMetricsSnapshot& frameMetrics )
{
#if defined( SKULLBONEZ_DEVELOPMENT_TOOLS )
    // Invariant: copy the completed world backbuffer before either operator
    // surface draws, preserving one presentation owner at a time.
    if ( m_imguiEditor.IsVisible() )
    {
        const SkullbonezCore::Core::SbResult viewportCapture = m_imguiEditor.CaptureGameViewport();

        if ( !viewportCapture.Ok() )
        {
            m_timers.FinishPresentedFrame();
            PROFILE_FRAME_END( m_profiler );
            m_applicationExit.RequestPhaseFailure( viewportCapture );
            return {};
        }
    }
#endif
    SkullbonezCore::UI::OperatorEditorFrameView operatorEditorView;
    bool secondarySurfaceVisible = false;
#if defined( SKULLBONEZ_DEVELOPMENT_TOOLS )
    secondarySurfaceVisible = m_imguiEditor.IsVisible();
#endif
    OperatorUiFrameSnapshot operatorUiSnapshot;
    operatorUiSnapshot.uiText = { RuntimeCameraModeEnabledMask( m_sceneController.State().isSceneMode,
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
                                  gameUiActive };

    operatorUiSnapshot.metrics = frameMetrics;
    operatorUiSnapshot.viewportWidth = m_window.ClientWidth();
    operatorUiSnapshot.viewportHeight = m_window.ClientHeight();
#if defined( SKULLBONEZ_DEVELOPMENT_TOOLS )
    operatorUiSnapshot.secondarySurfaceVisible = m_imguiEditor.IsVisible();
#endif

    OperatorUiPhaseOwner operatorUiPhase;
    operatorUiPhase.Begin( operatorUiSnapshot );

    const RuntimeUiTextFrameFacts& uiTextFacts = operatorUiPhase.Snapshot().uiText;

    const ReplayOverlay::ReplayOverlayStateView
        replayOverlay = m_replayRuntime.BuildOverlayStateView( m_editorTools.Editor().editorModeEnabled,
                                                               m_operatorUi->IsVisible(), m_operatorUi->IsMinimized(),
                                                               m_interaction.Gesture().kind,
                                                               renderModels.presentationRecords, renderModels.bodyStore );

    DiagnosticsRuntime& diagnosticsRuntime = m_diagnosticsRuntime;
    RuntimeOverlayPresentationEdit presentationEdit = m_overlayDiagnostics->EditPresentation();
    OverlayDebugState& debug = presentationEdit.State();
    SceneController& sceneController = m_sceneController;
    SceneSessionState& scene = sceneController.State();
    SkullbonezCore::Core::EngineConfig& config = m_config;
    RuntimeTools& runtimeTools = m_runtimeTools;
    EditorToolsOwner& editorTools = m_editorTools;
    SkullbonezCore::UI::InGameUI& ui = *m_operatorUi;
    RuntimeInputContext& runtimeInput = m_inputRouter.RuntimeContext();
    CameraControlState& camera = m_camera;
    SkullbonezCore::Threading::WorkerPool& workerPool = m_workerPool;
    RunLaunchOptions& launchOptions = m_launchOptions;
    RuntimeRenderer& renderer = Renderer();
    ReplayRuntime& replayRuntime = m_replayRuntime;

    // Lifetime: value-only facts exist only for this late UI call; no render or
    // UI owner retains a coordinator borrow.
    const SkullbonezCore::UI::RunSceneBrowserState& uiSceneBrowser = ui.SceneNavigation().browser;
    const std::string* uiScenePath = sceneController.CurrentPath();
    const ReplayHudStatus sharedReplayHud = replayRuntime.BuildHudStatus( false );
    const SkullbonezCore::Core::CinematicRenderConfig& sharedCinematic = ActiveSceneCinematicConfig( scene, config );
    const bool sharedCinematicRendering = IsSceneCinematicRenderingEnabled( scene, config, launchOptions, debug.isTextOnly,
                                                                            true );
    const bool sharedShadows = sharedCinematicRendering ? sharedCinematic.shadow.enabled
                                                        : config.ordinaryRender.shadow.enabled;
    RuntimeViewModel runtimeViewModel = SampleRuntimeViewModel( sceneController.State(), sceneController.Scene(),
                                                                sceneController.QueueSize(), m_capture.Screenshot(),
                                                                config.runtimeRender.presentationInterpolation,
                                                                uiTextFacts.presentationPinned,
                                                                uiTextFacts.presentationAlpha );

    const OperatorUiSceneFacts sceneFacts { runtimeViewModel,
                                            uiScenePath ? uiScenePath->c_str() : nullptr,
                                            uiScenePath ? SceneFileNameFromPath( uiScenePath->c_str() ) : "",
                                            uiSceneBrowser.namePtrs.empty() ? nullptr : uiSceneBrowser.namePtrs.data(),
                                            uiSceneBrowser.CurrentIndexForPath( sceneController.CurrentPath() ),
                                            static_cast<int>( uiSceneBrowser.namePtrs.size() ),
                                            uiSceneBrowser.selectedCineModeSceneIndex,
                                            sceneController.Scene().SceneEntityCount(),
                                            static_cast<int>( scene.rngSeed ),
                                            scene.solverBallCount,
                                            scene.solverBoxCount,
                                            frameMetrics.sceneEnergy,
                                            sceneController.Scene().Environment().GetGravity(),
                                            sceneController.Scene().Environment().GetFluidSurfaceHeight(),
                                            sceneController.Scene().Environment().GetFluidDensity(),
                                            sceneController.HasCurrentEntry(),
                                            scene.isExitOnComplete,
                                            scene.isTestComplete };

    const RenderPresentationSettings& renderPresentation = renderer.PresentationSettings();
    const OperatorUiGizmoMode gizmoMode = SampleOperatorUiGizmoMode( m_interaction.Gesture().kind,
                                                                     m_interaction.Gesture().gizmoKind );
    const OperatorUiRenderingFacts renderingFacts { config.ordinaryRender,
                                                    sharedCinematic,
                                                    uiTextFacts,
                                                    gizmoMode,
                                                    renderPresentation.vsyncEnabled,
                                                    config.runtimeRender.presentationInterpolation,
                                                    sharedShadows,
                                                    sharedCinematicRendering,
                                                    debug.isTerrainHidden,
                                                    debug.isWaterHidden,
                                                    debug.isWaterFreezeDebug,
                                                    debug.isWaterFlatDebug,
                                                    debug.isWaterNoReflect,
                                                    debug.isWaterRTReflect };

    // Invariant: projection completes before either UI presenter observes the
    // frame; Render receives only the completed detached values.
    ProjectOperatorEditorScene( operatorEditorView, sceneFacts );
    ProjectOperatorEditorRendering( operatorEditorView, renderingFacts );

    ProjectOperatorEditorLookLab( operatorEditorView, m_operatorUi->LookLabView() );
    ProjectOperatorEditorReplay( operatorEditorView, sharedReplayHud.memoryPreset, sharedReplayHud.requestedRetentionSeconds,
                                 sharedReplayHud.requestedBudgetMiB, sharedReplayHud.presentationRetentionSeconds,
                                 sharedReplayHud.solverRetentionSeconds, sharedReplayHud.memoryBudgetClamped,
                                 sharedReplayHud.solverWindowReduced );
    ProjectOperatorEditorSurfaces( operatorEditorView, ui.IsVisible(), secondarySurfaceVisible );

    ProjectOperatorEditorForecast( operatorEditorView, SampleOperatorUiForecastFacts( m_continuousForecast.View() ) );

    const RunEditorPlacementState& sharedEditor = editorTools.Editor();
    const SceneWorld& sceneWorld = sceneController.Scene();
    const SceneEntityStore& sceneEntities = sceneWorld.Entities();
    const int selectedHierarchyRow = PeekSelectedEditorModelIndex( sharedEditor, sceneWorld.BodyStore() );
    const OperatorUiHierarchyFacts hierarchyFacts { static_cast<uint32_t>( sceneEntities.Count() ),
                                                    selectedHierarchyRow,
                                                    sharedEditor.objectType,
                                                    UI::EditorTab::OBJECT_TYPE_COUNT,
                                                    static_cast<int>( sharedEditor.history.UndoDepth() ),
                                                    static_cast<int>( sharedEditor.history.RedoDepth() ),
                                                    sharedEditor.history.IsDirty(),
                                                    sharedEditor.editorModeEnabled,
                                                    sharedEditor.placementModeEnabled,
                                                    sharedEditor.placeStaticObject,
                                                    sceneController.CrossScenePauseLocked(),
                                                    scene.isFixedStep,
                                                    sharedEditor.autoTerrainAlign,
                                                    m_assets.FindAssetLibrarySourceAsset( "assetlib.buildings" ) !=
                                                        nullptr };
    BeginOperatorEditorHierarchy( operatorEditorView, hierarchyFacts );

    const uint32_t hierarchyRowCount = (std::min)( hierarchyFacts.totalRowCount,
                                                   UI::OPERATOR_EDITOR_HIERARCHY_ROW_CAPACITY );

    for ( uint32_t index = 0u; index < hierarchyRowCount; ++index )
    {
        const SceneEntityRecord& entity = sceneEntities.At( static_cast<int>( index ) );
        const OperatorUiHierarchyEntityFacts entityFacts { entity.displayName,
                                                           entity.sceneObjectId.value,
                                                           entity.behaviorGroup.rootObjectId.value,
                                                           entity.behaviorGroup.partIndex,
                                                           entity.asset.isAssetBacked,
                                                           entity.editorVisible,
                                                           entity.editorLocked };
        AppendOperatorEditorHierarchyRow( operatorEditorView, hierarchyFacts, entityFacts, index );
    }

#if defined( SKULLBONEZ_DEVELOPMENT_TOOLS )
    // Why: the GameUI surface does not consume secondary-editor contextual detail. Sampling
    // cold body/collider/buoyancy/material rows only while the secondary editor is
    // visible keeps ordinary Profile and shipping frames on their prior path.
    if ( operatorEditorView.surfaces.secondaryVisible )
    {
        OperatorUiInspectorFacts inspectorFacts;

        if ( sharedEditor.selectedBody.IsValid() && selectedHierarchyRow < 0 )
        {
            inspectorFacts.selectionState = UI::OperatorEditorInspectorSelectionState::Stale;
        }
        else if ( selectedHierarchyRow >= 0 )
        {
            const SceneEntityRecord* entity = sceneEntities.TryGet( selectedHierarchyRow );
            const PhysicsBodyStore& bodyStore = sceneWorld.BodyStore();
            const ColliderStore& colliderStore = sceneWorld.Colliders();
            const std::span<const BuoyancyBodyFacts> buoyancyFacts = PhysicsEngine::ReadBuoyancyFacts( sceneWorld.Physics() );
            const PhysicsBodyRecord* body = entity ? bodyStore.RecordForHandle( entity->body ) : nullptr;
            const PhysicsColliderHandle colliderHandle = entity ? colliderStore.HandleForBodyHandle( entity->body )
                                                                : PhysicsColliderHandle {};
            const ColliderRecord* collider = colliderStore.RecordForHandle( colliderHandle );
            const ColliderAuthoringRecord* colliderAuthoring = colliderStore.AuthoringRecordForHandle( colliderHandle );

            if ( !entity || !body || !collider || !colliderAuthoring ||
                 selectedHierarchyRow >= static_cast<int>( buoyancyFacts.size() ) )
            {
                inspectorFacts.selectionState = UI::OperatorEditorInspectorSelectionState::Stale;
            }
            else
            {
                const PhysicsBodyHotFieldsConstView hot = bodyStore.HotFields();
                const std::size_t row = static_cast<std::size_t>( selectedHierarchyRow );
                const auto position = PhysicsBodyPosition( hot, row );
                const auto orientation = PhysicsBodyOrientation( hot, row );
                const auto linearVelocity = PhysicsBodyLinearVelocity( hot, row );
                const auto angularVelocity = PhysicsBodyAngularVelocity( hot, row );
                inspectorFacts.displayName = entity->displayName;
                inspectorFacts.renderMaterialName = entity->renderMaterial.name[0] != '\0'
                                                        ? entity->renderMaterial.name
                                                        : Rendering::RenderMaterialKindName( entity->renderMaterial.kind );
                inspectorFacts.contactMaterialName = colliderAuthoring->contactMaterialName;
                inspectorFacts.assetName = entity->asset.assetName;
                inspectorFacts.assetInstanceName = entity->asset.instanceName;
                inspectorFacts.assetPartName = entity->asset.partName;
                inspectorFacts.selectionState = UI::OperatorEditorInspectorSelectionState::Single;
                inspectorFacts.sceneObjectId = entity->sceneObjectId.value;
                inspectorFacts.selectionCount = 1u;
                inspectorFacts.renderMaterialKind = static_cast<int>( entity->renderMaterial.kind );
                inspectorFacts.colliderShapeKind = static_cast<int>( collider->shapeKind );
                inspectorFacts.behaviorGroupKind = static_cast<int>( entity->behaviorGroup.kind );
                inspectorFacts.behaviorPartIndex = entity->behaviorGroup.partIndex;
                inspectorFacts.position[0] = position.x;
                inspectorFacts.position[1] = position.y;
                inspectorFacts.position[2] = position.z;
                orientation.GetComponents( inspectorFacts.orientation[0], inspectorFacts.orientation[1],
                                           inspectorFacts.orientation[2], inspectorFacts.orientation[3] );
                inspectorFacts.linearVelocity[0] = linearVelocity.x;
                inspectorFacts.linearVelocity[1] = linearVelocity.y;
                inspectorFacts.linearVelocity[2] = linearVelocity.z;
                inspectorFacts.angularVelocity[0] = angularVelocity.x;
                inspectorFacts.angularVelocity[1] = angularVelocity.y;
                inspectorFacts.angularVelocity[2] = angularVelocity.z;

                for ( int channel = 0; channel < 4; ++channel )
                {
                    inspectorFacts.baseColor[channel] = entity->renderMaterial.baseColor[channel];
                }

                inspectorFacts.mass = body->mass;
                inspectorFacts.volume = buoyancyFacts[row].volume;
                inspectorFacts.boundingRadius = collider->boundingRadius;
                inspectorFacts.dragCoefficient = collider->dragCoefficient;
                inspectorFacts.friction = collider->friction;
                inspectorFacts.restitution = collider->restitution;
                inspectorFacts.roughness = entity->renderMaterial.roughness;
                inspectorFacts.metallic = entity->renderMaterial.metallic;
                inspectorFacts.specular = entity->renderMaterial.specular;
                inspectorFacts.visible = entity->editorVisible;
                inspectorFacts.locked = entity->editorLocked;
                inspectorFacts.fixed = hot.fixed[row] != 0u;
                inspectorFacts.sleeping = hot.awake[row] == 0u;
                inspectorFacts.assetBacked = entity->asset.isAssetBacked;
            }
        }

        const Gameplay::TornadoFieldConfig& tornado = sceneWorld.Tornado().GetFieldConfig();
        const OperatorUiWorldFacts worldFacts { scene.modelCount,
                                                config.runtimeCapacity.sceneObjectCapacity,
                                                scene.solverBallCount,
                                                scene.solverBoxCount,
                                                static_cast<int>( scene.rngSeed ),
                                                scene.timeScale,
                                                sceneWorld.Environment().GetGravity(),
                                                sceneWorld.Environment().GetFluidSurfaceHeight(),
                                                sceneWorld.Environment().GetFluidDensity(),
                                                config.physicsMaterial.frictionCoeff,
                                                config.physicsMaterial.objectFrictionCoeff,
                                                config.physicsMaterial.rollingFrictionCoeff,
                                                tornado.radius,
                                                tornado.height,
                                                tornado.inwardAcceleration,
                                                tornado.swirlAcceleration,
                                                tornado.liftAcceleration,
                                                scene.isFixedStep,
                                                sceneWorld.Physics().IsSleepEnabled(),
                                                tornado.enabled };
        ProjectOperatorEditorInspectorAndWorld( operatorEditorView, inspectorFacts, worldFacts );
    }
#endif
    RuntimeRenderTargetPreviewSnapshot renderTargetPreviews;

    // Lifetime: the diagnostics view borrows this detached buffer until both
    // operator surfaces finish consuming the frame view below.
    RenderDiagnosticsReadout renderDiagnosticsReadout;
    OperatorUiSecondaryDiagnosticsFacts secondaryDiagnosticsFacts;

    if ( operatorEditorView.surfaces.secondaryVisible )
    {
        SampleSecondaryOperatorDiagnostics( frameMetrics, uiTextFacts, debug, sharedShadows, sharedCinematicRendering,
                                            sharedCinematic, runtimeViewModel, renderTargetPreviews,
                                            renderDiagnosticsReadout, secondaryDiagnosticsFacts );
        ProjectOperatorEditorDiagnostics( operatorEditorView, secondaryDiagnosticsFacts );
    }

    const bool replayPathVisualizerHasTarget = replayRuntime.BuildInputView().hasPathTarget;

    const bool profilerBars = debug.overlayMode == OverlayMode::BarsNormalized ||
                              debug.overlayMode == OverlayMode::BarsAbsolute;

    operatorUiPhase.Compose( debug.isTextOnly, ui.NeedsUiTextPass(), ui.IsVisible(), profilerBars );

    const OperatorUiSubmissionPlan& submissionPlan = operatorUiPhase.SubmissionPlan();

    renderer.PrepareUiFrameTarget();
    int gameUiDrawCalls = 0;

    const UiTextVisibility uiTextVisibility { debug.isTextOnly,
                                              scene.isSceneMode,
                                              scene.isSceneText,
                                              debug.overlayMode != OverlayMode::None,
                                              ui.NeedsUiTextPass(),
                                              sceneController.CrossScenePauseLocked(),
                                              debug.isTopTextHidden,
                                              scene.isTestComplete,
                                              replayOverlay.shouldRenderScrubber,
                                              replayPathVisualizerHasTarget,
                                              ProjectUiCameraBadgeMode( camera.mode ) != UiCameraBadgeMode::Quiet };

    if ( renderer.ResourceLifecycle().ShouldRenderUiText( uiTextVisibility ) )
    {
        const SkullbonezCore::Core::CinematicRenderConfig& uiCinematic = ActiveSceneCinematicConfig( scene, config );
        const bool uiCinematicRendering = IsSceneCinematicRenderingEnabled( scene, config, launchOptions, debug.isTextOnly,
                                                                            true );
        const bool shadowsAvailable = uiCinematicRendering ? uiCinematic.shadow.enabled
                                                           : config.ordinaryRender.shadow.enabled;

        renderTargetPreviews = renderer.ResourceLifecycle()
                                   .BuildRenderTargetPreviewSnapshot( shadowsAvailable, uiCinematicRendering,
                                                                      uiCinematicRendering &&
                                                                          uiCinematic.volumetricLightingEnabled );

        const bool replayMemoryStatsRequested = ui.IsVisible() && !ui.IsMinimized() &&
                                                ui.GetActiveTab() == SkullbonezCore::UI::InGameUITab::Memory;

        const ReplayHudStatus replayHud = replayRuntime.BuildHudStatus( replayMemoryStatsRequested );
        const UiTextViewport uiViewport { operatorUiPhase.Snapshot().viewportWidth,
                                          operatorUiPhase.Snapshot().viewportHeight };

        PROFILE_BEGIN( "Frame/UI" );
        {
            CoreAllocation::RuntimeAllocationScope allocationScope( CoreAllocation::RuntimeAllocationPhase::Render );
            const RuntimeFrameMetricsSnapshot& metrics = operatorUiPhase.Snapshot().metrics;
            const int uiDrawCallStart = renderer.BeginUiTextFrame( uiViewport );
            UiChromeStatusValues chromeStatus;
            chromeStatus.textOnly = debug.isTextOnly;
            chromeStatus.topTextHidden = debug.isTopTextHidden;
            chromeStatus.sceneMode = scene.isSceneMode;
            chromeStatus.sceneTestComplete = scene.isTestComplete;
            chromeStatus.crossScenePauseLocked = sceneController.CrossScenePauseLocked();
            chromeStatus.currentFrame = scene.currentFrame;
            chromeStatus.targetFrameCount = scene.targetFrameCount;
            chromeStatus.currentSceneIndex = scene.currentSceneIndex;
            chromeStatus.sceneQueueSize = sceneController.QueueSize();
            chromeStatus.cameraMode = ProjectUiCameraBadgeMode( camera.mode );
            chromeStatus.cameraModeLabel = uiTextFacts.cameraModeLabel;
            chromeStatus.interactionRecording = debug.isInteractionRecording;
            chromeStatus.interactionPlayback = debug.isInteractionPlayback;
            chromeStatus.interactionFailure = debug.interactionRecordingFailure;
            chromeStatus.interactionPlaybackTurn = debug.interactionPlaybackTurn;
            chromeStatus.interactionPlaybackTurnCount = debug.interactionPlaybackTurnCount;
            chromeStatus.interactionRecordingElapsedSeconds = debug.interactionRecordingElapsedSeconds;
            chromeStatus.interactionRecordingMaximumMinutes = debug.interactionRecordingMaximumMinutes;
            chromeStatus.interactionRecordingFrameCount = debug.interactionRecordingFrameCount;
            chromeStatus.interactionRecordingFrameCapacity = debug.interactionRecordingFrameCapacity;

            UiChromeTailValues chromeTail;
            chromeTail.topTextHidden = debug.isTopTextHidden;
            chromeTail.divergenceValid = replayHud.divergenceValid;
            chromeTail.divergenceUnits = replayHud.divergenceUnits;
            chromeTail.launcherCameraMode = uiTextFacts.isLauncherCameraMode;
            chromeTail.launcherFireModeLabel = uiTextFacts.launcherFireModeLabel;
#if defined( _DEBUG )
            chromeTail.reproSnapshotMessage = debug.reproSnapshotMessage;
            chromeTail.reproMessageAgeSeconds = metrics.sceneElapsedSeconds;
            chromeTail.reproSnapshotMessageUntil = debug.reproSnapshotMessageUntil;
#endif
            renderer.SubmitUiChrome( uiViewport, chromeStatus, chromeTail );

            if ( submissionPlan.composeGameUi )
            {
                // UI composition is a CPU value phase. Render receives the
                // completed backend-neutral draw list and the parallel
                // renderer-owned preview catalog only for synchronous replay.
                SkullbonezCore::UI::InGameUIFrameData uiData;
                SkullbonezCore::UI::UIRuntimeReserveCapacityRow
                    reserveCapacityRows[SkullbonezCore::UI::UI_RUNTIME_RESERVE_CAPACITY_ROW_MAX] = {};
                renderer.PrepareOperatorUiSubmission( uiViewport, debug.isUITestPattern );
                OperatorUiDiagnosticsFacts diagnosticsFacts;
                diagnosticsFacts.metrics = metrics;
                diagnosticsFacts.visibility = renderer.RenderDiagnostics().GetFrameVisibilityStats();
                diagnosticsFacts.renderMemory = renderer.RenderDiagnostics().GetRenderMemoryStats();
                diagnosticsFacts.drawTrace = renderer.RenderDiagnostics().GetFrameDrawCallTrace();
                diagnosticsFacts.workerThreadCount = workerPool.GetThreadCount();
                diagnosticsFacts.maxWorkerThreadCount = Threading::WorkerPool::MaxThreadCount();
                diagnosticsFacts.replayMemoryPreset = replayHud.memoryPreset;
                diagnosticsFacts.replayRequestedRetentionSeconds = replayHud.requestedRetentionSeconds;
                diagnosticsFacts.replayRequestedBudgetMiB = replayHud.requestedBudgetMiB;
                diagnosticsFacts.replayPresentationRetentionSeconds = replayHud.presentationRetentionSeconds;
                diagnosticsFacts.replaySolverRetentionSeconds = replayHud.solverRetentionSeconds;
                diagnosticsFacts.replayMemoryBudgetClamped = replayHud.memoryBudgetClamped;
                diagnosticsFacts.replayMemorySolverWindowReduced = replayHud.solverWindowReduced;
                diagnosticsFacts.predictionRevealRate = replayHud.predictionRevealRate;
                diagnosticsFacts.now = metrics.simulationTotalSeconds;
#if defined( SKULLBONEZ_PROFILE_ENABLED )

                if ( !m_profiler )
                {
                    SB_FATAL( "Runtime/App/OperatorEditorFramePhase",
                              "Profile UI sampling requires the startup-bound profiler." );
                }

                diagnosticsFacts.markerCount = (std::min)( m_profiler->MarkerCount(), UI::ProfilerTab::MAX_MARKERS );

                for ( int markerIndex = 0; markerIndex < diagnosticsFacts.markerCount; ++markerIndex )
                {
                    const Core::Profiler::Marker& source = m_profiler->GetMarker( markerIndex );
                    OperatorUiProfilerMarkerFacts& target = diagnosticsFacts
                                                                .markers[static_cast<std::size_t>( markerIndex )];
                    target.name = source.name;
                    target.leafName = source.leafName;
                    target.hash = source.hash;
                    target.parentIndex = source.parentIndex;
                    target.depth = source.depth;
                    target.colorIndex = source.colorIndex;
                    target.lastFrameMs = source.lastFrameMs;
                    target.lastSelfMs = source.lastSelfMs;
                    target.avgMs = source.avgMs;
                    target.selfAvgMs = source.selfAvgMs;
                    target.lastFrameWorkerMs = source.lastFrameWorkerMs;
                    target.workerAvgMs = source.workerAvgMs;
                    target.p50Ms = source.p50Ms;
                    target.p99Ms = source.p99Ms;
                    target.gpuLastFrameMs = source.gpuLastFrameMs;
                    target.hasGpu = source.hasGpu;
                }

                diagnosticsFacts.workerSampleCount = (std::min)( m_profiler->WorkerCoreSampleCount(),
                                                                 UI::ProfilerTab::MAX_WORKER_CORE_SAMPLES );

                for ( int sampleIndex = 0; sampleIndex < diagnosticsFacts.workerSampleCount; ++sampleIndex )
                {
                    const Core::Profiler::WorkerCoreSample& source = m_profiler->GetWorkerCoreSample( sampleIndex );
                    OperatorUiWorkerCoreFacts& target = diagnosticsFacts
                                                            .workerSamples[static_cast<std::size_t>( sampleIndex )];
                    target.workerIndex = source.workerIndex;
                    target.jobCount = source.jobCount;
                    target.coreMs = source.coreMs;
                    target.avgCoreMs = source.avgCoreMs;
                    target.spanStartMs = source.spanStartMs;
                    target.spanEndMs = source.spanEndMs;
                }
#endif
#if defined( TRACY_ENABLE )
                {
                    const Core::DevelopmentTools::TracyClientStatus
                        tracyStatus = Core::DevelopmentTools::TracyClientOwner::CopyStatus();
                    diagnosticsFacts.tracyBuildEnabled = tracyStatus.buildEnabled;
                    diagnosticsFacts.tracyInitialized = tracyStatus.initialized;
                    diagnosticsFacts.tracyViewerConnected = tracyStatus.viewerConnected;
                }
#endif
                const bool memoryTabActive = ui.IsVisible() && !ui.IsMinimized() &&
                                             ui.GetActiveTab() == UI::InGameUITab::Memory;
                const bool memoryOverlayEnabled = ui.IsMemoryOverlayEnabled();

                if ( memoryTabActive && replayHud.memoryStatsValid )
                {
                    diagnosticsFacts.mainMemory = diagnosticsRuntime.RefreshMainMemoryStats( replayHud.memoryStats,
                                                                                             renderModels.gameObjectMemory,
                                                                                             diagnosticsFacts.now, false,
                                                                                             false );
                }
                else if ( memoryOverlayEnabled )
                {
                    diagnosticsFacts.mainMemory = SampleMainMemoryOverlayStats( diagnosticsRuntime,
                                                                                renderModels.gameObjectMemory );
                }

                if ( memoryTabActive || memoryOverlayEnabled )
                {
                    diagnosticsFacts.renderMemoryAvailable = true;
                    diagnosticsFacts
                        .reserveGrowthEventTotalCount = CoreAllocation::RuntimeReserveAllocator::GrowthEventCount();
                    diagnosticsFacts
                        .reserveGrowthEventDroppedCount = CoreAllocation::RuntimeReserveAllocator::GrowthEventDroppedCount();
                    diagnosticsFacts.reserveGrowthEventCount = CoreAllocation::RuntimeReserveAllocator::
                        CopyRecentGrowthEvents( diagnosticsFacts.reserveGrowthEvents.data(),
                                                static_cast<int>( diagnosticsFacts.reserveGrowthEvents.size() ) );
                }

                if ( memoryTabActive )
                {
                    diagnosticsFacts.reserveCapacityAvailable = true;
                    const std::span<const CoreAllocation::RuntimeReserveCapacityView>
                        capacityRows = CoreAllocation::RuntimeReserveAllocator::CapacityRows();
                    diagnosticsFacts
                        .reserveCapacityRowCount = (std::min)( static_cast<int>( capacityRows.size() ),
                                                               static_cast<int>( diagnosticsFacts.reserveCapacityRows.size() ) );

                    for ( int index = 0; index < diagnosticsFacts.reserveCapacityRowCount; ++index )
                    {
                        diagnosticsFacts.reserveCapacityRows[static_cast<std::size_t>( index )] = capacityRows[static_cast<std::size_t>( index )];
                    }
                }

                ProjectOperatorUiDiagnostics( uiData, diagnosticsFacts, reserveCapacityRows );
                const Gameplay::TornadoFieldConfig& tornado = sceneWorld.Tornado().GetFieldConfig();
                const OperatorUiSettingsFacts settingsFacts { config.ordinaryRender,
                                                              uiCinematic,
                                                              BuildDiagnosticsPhysicsUIStatus( debug ),
                                                              SkullbonezCore::Core::ActiveSceneObjectCapacity( config ),
                                                              sceneWorld.Environment().GetGravity(),
                                                              sceneWorld.Environment().GetFluidSurfaceHeight(),
                                                              sceneWorld.Environment().GetFluidDensity(),
                                                              tornado.radius,
                                                              tornado.height,
                                                              tornado.inwardAcceleration,
                                                              tornado.swirlAcceleration,
                                                              tornado.liftAcceleration,
                                                              config.physicsMaterial.frictionCoeff,
                                                              config.physicsMaterial.objectFrictionCoeff,
                                                              config.physicsMaterial.rollingFrictionCoeff,
                                                              debug.isTextOnly,
                                                              renderPresentation.vsyncEnabled,
                                                              renderPresentation.pipelineSyncEnabled,
                                                              sceneWorld.Physics().IsSleepEnabled(),
                                                              tornado.enabled,
                                                              sceneWorld.Tornado().VisualSettings().enabled,
                                                              tornado.visualizeVelocityField,
                                                              debug.isWaterFreezeDebug,
                                                              debug.isWaterFlatDebug,
                                                              debug.isTerrainHidden,
                                                              debug.isWaterHidden,
                                                              debug.isWaterNoReflect,
                                                              debug.isWaterRTReflect,
                                                              uiCinematicRendering };
                ProjectOperatorUiSettings( uiData, settingsFacts );

                const RuntimeInputMode runtimeInputMode = runtimeInput.CurrentMode();
                const RunRayCastTestState& rayCast = runtimeTools.RayCastTest();
                const RunEditorPlacementState& editor = editorTools.Editor();
                const OperatorUiInteractionFacts
                    interactionFacts { uiTextFacts.cameraModeLabel,
                                       camera.trackBallRow.IsValid() ? camera.trackHeight : 0.0f,
                                       camera.autoCycleInterval > 0.0f ? camera.autoCycleInterval : 0.0f,
                                       rayCast.impulseStrength,
                                       rayCast.projectileSpeed,
                                       uiTextFacts.cameraModeEnabledMask,
                                       static_cast<int>( camera.mode ),
                                       editor.objectType,
                                       static_cast<int>( editor.history.UndoDepth() ),
                                       static_cast<int>( editor.history.RedoDepth() ),
                                       rayCast.visualizeRays,
                                       ( runtimeInputMode == RuntimeInputMode::FlyCamera ||
                                         runtimeInputMode == RuntimeInputMode::Launcher ||
                                         runtimeInputMode == RuntimeInputMode::EditorViewportLook ) &&
                                           !ui.BlocksCameraMouse(),
                                       editor.editorModeEnabled,
                                       editor.placementModeEnabled,
                                       editor.placeStaticObject,
                                       editor.autoTerrainAlign,
                                       editor.viewportLookActive };
                ProjectOperatorUiInteraction( uiData, interactionFacts );
                OperatorUiSceneFacts gameUiSceneFacts = sceneFacts;
                gameUiSceneFacts.energyForDisplay = metrics.sceneEnergy;
                ProjectOperatorUiPresentation( uiData, gameUiSceneFacts, operatorEditorView );

                ProjectOperatorUiViewport( uiData, uiViewport.screenW, uiViewport.screenH );
                const RenderDiagnosticsReadout operatorDiagnostics = renderer.BuildDiagnosticsReadout();
                ProjectOperatorUiRenderIdentity( uiData, operatorDiagnostics.rendererName.data(), uiDrawCallStart );

                const SkullbonezCore::UI::InteractionRecordingBrowserState& recordings = ui.SceneNavigation().recordings;
                ProjectOperatorUiRecordingBrowser( uiData,
                                                   recordings.namePtrs.empty() ? nullptr : recordings.namePtrs.data(),
                                                   static_cast<int>( recordings.namePtrs.size() ),
                                                   recordings.paths.empty() ? -1 : recordings.selectedIndex );

                renderer.AppendDxrReflectionPreview( renderTargetPreviews, uiViewport,
                                                     settingsFacts.waterRtReflect && !settingsFacts.waterNoReflect );
                OperatorUiRenderTargetListFacts renderTargetFacts;
                const int renderTargetCount = (std::min)( renderTargetPreviews.count,
                                                          SkullbonezCore::UI::UI_RENDER_TARGET_PREVIEW_MAX );

                for ( int index = 0; index < renderTargetCount; ++index )
                {
                    const RuntimeRenderTargetPreview& source = renderTargetPreviews
                                                                   .targets[static_cast<std::size_t>( index )];
                    static_cast<void>( renderTargetFacts.Append( source.label, source.width, source.height,
                                                                 source.available && source.width > 0 && source.height > 0,
                                                                 source.depth, source.hdr ) );
                }

                ProjectOperatorUiRenderTargets( uiData, renderTargetFacts );

                const SkullbonezCore::UI::UIDrawList& drawList = ui.Draw( uiData );
                renderer.SubmitOperatorUiDrawList( drawList, renderTargetPreviews, m_assets, uiViewport );
            }

            if ( submissionPlan.submitOverlay )
            {
                const float rollingFps = metrics.rollingFrameSeconds > 0.0f ? 1.0f / metrics.rollingFrameSeconds : 0.0f;
                renderer.SubmitUiOverlay( uiViewport, ProjectUiOverlayMode( debug.overlayMode ), scene.modelCount,
                                          rollingFps, metrics.sceneEnergy );
            }

            if ( submissionPlan.submitReplay )
            {
                const UI::UIDrawList&
                    replayDrawList = replayRuntime.ComposeOverlayDrawList( replayOverlay, uiTextFacts.gameUiActive,
                                                                           scene.isScenePhysics,
                                                                           uiTextFacts.interactionGestureKind,
                                                                           { uiViewport.screenW, uiViewport.screenH },
                                                                           metrics.simulationTotalSeconds );
                renderer.SubmitUiDrawList( replayDrawList, uiViewport );
            }

            if ( submissionPlan.finalizeOverlay )
            {
                renderer.FinalizeUiOverlay( ProjectUiOverlayMode( debug.overlayMode ) );
            }

            gameUiDrawCalls = renderer.EndUiTextFrame( uiDrawCallStart );
        }
        PROFILE_END( "Frame/UI" );
    }
    else
    {
        gameUiDrawCalls = 0;
    }

    operatorUiPhase.RecordGpuSubmission( gameUiDrawCalls );

    m_timers.RecordUiDrawCalls( operatorUiPhase.GameUiDrawCalls() );

    bool requestSurfaceSwap = false;
    bool requestTracyStandardCapture = false;

#if defined( SKULLBONEZ_DEVELOPMENT_TOOLS )
    const UINT windowDpi = GetDpiForWindow( m_window.NativeWindowHandle() );
    const float dpiScale = windowDpi > 0u ? static_cast<float>( windowDpi ) / 96.0f : 1.0f;
    const SkullbonezCore::Core::DevelopmentTools::TracyClientStatus
        tracyStatus = SkullbonezCore::Core::DevelopmentTools::TracyClientOwner::CopyStatus();

    const DevelopmentTools::ImGuiEditorFrameInput imguiFrameInput { m_window.ClientWidth(),
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

        if ( imguiResult.status.Ok() )
        {
            const DevelopmentTools::ImGuiPreparedDrawDataView drawData = m_imguiEditor.PreparedDrawData();
            imguiResult.status = Renderer().RenderDevelopmentUi( drawData.context, drawData.drawData );
        }

        if ( !imguiResult.status.Ok() )
        {
            m_timers.FinishPresentedFrame();
            PROFILE_FRAME_END( m_profiler );
            m_applicationExit.RequestPhaseFailure( imguiResult.status );
            return {};
        }

        if ( imguiResult.commands.requestSurfaceSwap )
        {
            requestSurfaceSwap = true;
        }

        if ( imguiResult.commands.requestTracyStandardCapture )
        {
            requestTracyStandardCapture = true;
        }
    }
#endif

    operatorUiPhase.EmitCommands( requestSurfaceSwap, requestTracyStandardCapture );
    operatorUiPhase.Complete();

    return operatorUiPhase.Commands();
}


} // namespace Runtime
} // namespace SkullbonezCore
