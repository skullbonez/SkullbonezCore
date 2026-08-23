/*
File: SkullbonezSource/Runtime/App/OperatorEditorFramePhase.cpp
Purpose:
  Sequences the shared operator-editor presentation for one frame.

Summary:
  App synchronously samples domain owners, combines those facts in one bounded
  OperatorEditorFrameView, and applies typed process commands after UI and GPU
  work complete. Runtime/UI owns the value-only phase cursor and snapshot.
  Run::RenderOperatorUiPhase is the owner-approved top-level phase coordinator.
  It reaches process-owned members for one ordered UI phase, builds one shared
  value projection, submits GameUI and ImGui presentation, and retains no frame
  values after returning to the frame sequencer.

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
#include "OperatorUiProjection.h"
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
#include "../../UI/UI.h"
#include "../../UI/UITabEditor.h"

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
} // namespace

void Run::RenderOperatorUiPhase( const RuntimeRenderModelFrameView& renderModels,
                                 float presentationAlpha, bool capturePresentationPinned, double secondsPerFrame,
                                 bool gameUiActive,
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
            return;
        }
    }
#endif
    SkullbonezCore::UI::OperatorEditorFrameView operatorEditorView;
    operatorEditorView.lookLab = m_operatorUi->LookLabView();
#if defined( SKULLBONEZ_DEVELOPMENT_TOOLS )
    operatorEditorView.surfaces.secondaryVisible = m_imguiEditor.IsVisible();
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

    if ( !operatorUiPhase.Begin( operatorUiSnapshot ) )
    {
        SB_FATAL( "OperatorUI", "Operator UI phase failed to accept its detached frame snapshot." );
    }

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

    // Invariant: projection completes before either UI presenter observes the
    // frame; Render receives only the completed detached values.
    ProjectOperatorEditorScene( operatorEditorView, uiScenePath ? uiScenePath->c_str() : nullptr, uiSceneBrowser,
                                uiSceneBrowser.CurrentIndexForPath( sceneController.CurrentPath() ), scene,
                                sceneController.Scene() );

    ProjectOperatorEditorRendering( operatorEditorView, renderer.PresentationSettings(), config, sharedCinematic, debug,
                                    uiTextFacts, sharedCinematicRendering, sharedShadows );

    operatorEditorView.replay = { sharedReplayHud.memoryPreset,           sharedReplayHud.requestedRetentionSeconds,
                                  sharedReplayHud.requestedBudgetMiB,     sharedReplayHud.presentationRetentionSeconds,
                                  sharedReplayHud.solverRetentionSeconds, sharedReplayHud.memoryBudgetClamped,
                                  sharedReplayHud.solverWindowReduced };

    ProjectOperatorEditorForecast( operatorEditorView, m_continuousForecast.View() );

    operatorEditorView.surfaces = { ui.IsVisible(), operatorEditorView.surfaces.secondaryVisible };

    const RunEditorPlacementState& sharedEditor = editorTools.Editor();
    const int selectedHierarchyRow = ProjectOperatorEditorHierarchy(
        operatorEditorView, sharedEditor, sceneController.Scene(), sceneController.CrossScenePauseLocked(),
        scene.isFixedStep, m_assets.FindAssetLibrarySourceAsset( "assetlib.buildings" ) != nullptr );

#if defined( SKULLBONEZ_DEVELOPMENT_TOOLS )
    // Why: the GameUI surface does not consume secondary-editor contextual detail. Sampling
    // cold body/collider/buoyancy/material rows only while the secondary editor is
    // visible keeps ordinary Profile and shipping frames on their prior path.
    if ( operatorEditorView.surfaces.secondaryVisible )
    {
        ProjectOperatorEditorInspectorAndWorld( operatorEditorView, sharedEditor, sceneController.Scene(),
                                                selectedHierarchyRow, scene, config );
    }
#endif
    RuntimeViewModel runtimeViewModel;
    RuntimeRenderTargetPreviewSnapshot renderTargetPreviews;

    // Lifetime: the diagnostics view borrows this detached buffer until both
    // operator surfaces finish consuming the frame view below.
    RenderDiagnosticsReadout renderDiagnosticsReadout;

    if ( operatorEditorView.surfaces.secondaryVisible )
    {
        // Why: the secondary surface can be visible while GameUI is
        // hidden. Sample its bounded authoring/diagnostic values here instead
        // of making ImGui depend on whether the GameUI text pass happens to run.
        runtimeViewModel = BuildOperatorRuntimeViewModel(
            sceneController.State(), sceneController.Scene(), sceneController.QueueSize(), m_capture.Screenshot(),
            config.runtimeRender.presentationInterpolation, uiTextFacts.presentationPinned,
            uiTextFacts.presentationAlpha );

        renderTargetPreviews = renderer.ResourceLifecycle()
                                   .BuildRenderTargetPreviewSnapshot( sharedShadows, sharedCinematicRendering,
                                                                      sharedCinematicRendering &&
                                                                          sharedCinematic.volumetricLightingEnabled );

        SkullbonezCore::UI::OperatorEditorDiagnosticsView& diagnostics = operatorEditorView.diagnostics;

        // Invariant: the right rail reads fixed snapshots and cached counters;
        // opening Diagnostics must not trigger an allocation scan or grow data.
        const SkullbonezCore::Core::MainMemoryStats& mainMemory = diagnosticsRuntime.MainMemoryStatsSnapshot();
        renderDiagnosticsReadout = renderer.BuildDiagnosticsReadout();
        diagnostics.rendererName = renderDiagnosticsReadout.rendererName.data();
        diagnostics.drawCalls = renderDiagnosticsReadout.drawCalls;
        diagnostics.uiDrawCalls = frameMetrics.uiDrawCalls;
        diagnostics.workerThreadCount = workerPool.GetThreadCount();
        diagnostics.maxWorkerThreadCount = SkullbonezCore::Threading::WorkerPool::MaxThreadCount();
        diagnostics.fps = frameMetrics.rollingFrameSeconds > 0.0f
                              ? 1.0f / frameMetrics.rollingFrameSeconds
                              : ( frameMetrics.secondsPerFrame > 0.0
                                      ? static_cast<float>( 1.0 / frameMetrics.secondsPerFrame )
                                      : 0.0f );
        diagnostics.renderMs = ( frameMetrics.rollingRenderSeconds > 0.0f ? frameMetrics.rollingRenderSeconds
                                                                          : frameMetrics.renderSeconds ) *
                               1000.0f;

        diagnostics.physicsMs = ( frameMetrics.rollingPhysicsSeconds > 0.0f ? frameMetrics.rollingPhysicsSeconds
                                                                            : frameMetrics.physicsSeconds ) *
                                1000.0f;

        diagnostics.cpuFrameMs = frameMetrics.cpuFrameWorkMs;
        diagnostics.gpuFrameMs = frameMetrics.gpuFrameWorkMs;
        diagnostics.physicsDebugFlags = debug.physicsDebugFlags;
        const int stageCount = static_cast<int>( PhysicsPipelineStage::Count );
        int stageIndex = stageCount > 0 ? debug.physicsDebugPipelineStageCursor % stageCount : 0;

        if ( stageIndex < 0 )
        {
            stageIndex += stageCount;
        }

        diagnostics.physicsPipelineStageIndex = stageIndex;
        diagnostics.physicsPipelineStageCount = stageCount;
        diagnostics.physicsPipelineStageName = PhysicsPipelineStageName( static_cast<PhysicsPipelineStage>( stageIndex ) );

        diagnostics.physicsDebugAlpha = debug.physicsDebugAlpha;
        diagnostics.physicsDebugContactLinger = debug.physicsDebugContactLinger;
        diagnostics.rayCastImpulseStrength = runtimeTools.RayCastTest().impulseStrength;
        diagnostics.launcherProjectileSpeed = runtimeTools.RayCastTest().projectileSpeed;
        diagnostics.collisionVisualizer = debug.isCollisionVisualizer;
        diagnostics.physicsDebugTransparent = debug.isPhysicsDebugTransparent;
        diagnostics.broadphaseOverlay = debug.isBroadphaseOverlay;
        diagnostics.tornadoVisualShell = sceneController.Scene().Tornado().VisualSettings().enabled;
        diagnostics.tornadoFieldVectors = sceneController.Scene().Tornado().GetFieldConfig().visualizeVelocityField;
        diagnostics.rayCastVisualization = runtimeTools.RayCastTest().visualizeRays;
        diagnostics.trackedEngineBytes = mainMemory.trackedEngineBytes;
        diagnostics.reconciledTotalBytes = mainMemory.reconciledTotalBytes;
        diagnostics.uploadUsedBytes = renderDiagnosticsReadout.memory.uploadUsedBytes;
        diagnostics.uploadCapacityBytes = renderDiagnosticsReadout.memory.uploadCapacityBytes;
        diagnostics
            .replayReserveGrowthEvents = SkullbonezCore::Core::Allocation::RuntimeReserveAllocator::GrowthEventCount();
        diagnostics.renderTargetCount = (std::min)( renderTargetPreviews.count,
                                                    SkullbonezCore::UI::OPERATOR_EDITOR_RENDER_TARGET_CAPACITY );

        for ( int index = 0; index < diagnostics.renderTargetCount; ++index )
        {
            const RuntimeRenderTargetPreview& source = renderTargetPreviews.targets[static_cast<size_t>( index )];
            diagnostics.renderTargets[index] = { source.label,  source.width,
                                                 source.height, source.available && source.textureHandle != 0u,
                                                 source.depth,  source.hdr };
        }
    }

    const bool replayPathVisualizerHasTarget = replayRuntime.BuildInputView().hasPathTarget;

    if ( !operatorUiPhase.MarkComposed() )
    {
        SB_FATAL( "OperatorUI", "Operator UI phase skipped composition." );
    }

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
        runtimeViewModel = BuildOperatorRuntimeViewModel(
            sceneController.State(), sceneController.Scene(), sceneController.QueueSize(), m_capture.Screenshot(),
            config.runtimeRender.presentationInterpolation, uiTextFacts.presentationPinned,
            uiTextFacts.presentationAlpha );

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

            const bool textOnly = debug.isTextOnly;
            const bool operatorNeeded = ui.NeedsUiTextPass();
            const bool operatorVisible = ui.IsVisible();
            const bool profilerBars = debug.overlayMode == OverlayMode::BarsNormalized ||
                                      debug.overlayMode == OverlayMode::BarsAbsolute;
            const OperatorUiSubmissionPlan submissionPlan = ResolveOperatorUiSubmissionPlan( textOnly, operatorNeeded,
                                                                                             operatorVisible, profilerBars );

            if ( submissionPlan.composeGameUi )
            {
                // UI composition is a CPU value phase. Only prepare/submission
                // touch the GPU, and each renderer callback borrow ends before
                // the next focused operation begins.
                SkullbonezCore::UI::InGameUIFrameData uiData;
                SkullbonezCore::UI::UIRuntimeReserveCapacityRow
                    reserveCapacityRows[SkullbonezCore::UI::UI_RUNTIME_RESERVE_CAPACITY_ROW_MAX] = {};
                renderer.PrepareOperatorUiFrame( uiData, uiViewport, debug.isUITestPattern );
                ProjectOperatorUiDiagnostics( uiData, replayHud, metrics, renderModels, diagnosticsRuntime, ui, &workerPool,
                                              m_profiler, reserveCapacityRows, renderer.RenderDiagnostics() );
                ProjectOperatorUiSettings( uiData, debug, renderer.PresentationSettings(), sceneController.Scene(), config,
                                           uiCinematic, uiCinematicRendering );
                ProjectOperatorUiInteraction( uiData, runtimeTools.RayCastTest(), editorTools.Editor(), runtimeInput, camera,
                                              ui, uiTextFacts.cameraModeEnabledMask, uiTextFacts.cameraModeLabel );
                ProjectOperatorUiPresentation( uiData, scene, runtimeViewModel, uiSceneBrowser, operatorEditorView,
                                               sceneController.HasCurrentEntry(),
                                               uiScenePath ? uiScenePath->c_str() : nullptr,
                                               uiSceneBrowser.CurrentIndexForPath( sceneController.CurrentPath() ),
                                               metrics.sceneEnergy );
                renderer.SubmitOperatorUiFrame( uiData, ui, renderTargetPreviews, m_assets, uiDrawCallStart );
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

    if ( !operatorUiPhase.RecordGpuSubmission( gameUiDrawCalls ) )
    {
        SB_FATAL( "OperatorUI", "Operator UI phase recorded GPU submission out of order." );
    }

    m_timers.RecordUiDrawCalls( operatorUiPhase.GameUiDrawCalls() );

    OperatorUiProcessCommands uiProcessCommands;

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
            return;
        }

        if ( imguiResult.commands.requestSurfaceSwap )
        {
            uiProcessCommands.surface = OperatorUiSurfaceCommand::ShowGameUi;
        }

        if ( imguiResult.commands.requestTracyStandardCapture )
        {
            uiProcessCommands.requestTracyStandardCapture = true;
        }
    }
#endif

    if ( !operatorUiPhase.EmitCommands( uiProcessCommands ) || !operatorUiPhase.Complete() )
    {
        SB_FATAL( "OperatorUI", "Operator UI phase failed to complete after command emission." );
    }

    // App alone applies process and native-surface effects after both UI
    // presenters have finished consuming the immutable phase snapshot.
#if defined( SKULLBONEZ_DEVELOPMENT_TOOLS )
    if ( operatorUiPhase.Commands().surface == OperatorUiSurfaceCommand::ShowGameUi )
    {
        SelectDevelopmentUiSurface( DevelopmentUiMode::GameUI );
    }
#endif

#if defined( SKULLBONEZ_DEVELOPMENT_TOOLS )
    if ( operatorUiPhase.Commands().requestTracyStandardCapture )
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
#endif
}


} // namespace Runtime
} // namespace SkullbonezCore
