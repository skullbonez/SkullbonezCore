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
struct OperatorUiProjectionFacts
{
    RuntimeViewModel runtime;
    RuntimeUiTextFrameFacts uiText;
    OperatorUiSceneFacts scene;
    ReplayHudStatus replayHud;
    Core::CinematicRenderConfig cinematic;
    bool cinematicRendering = false;
    bool shadowsEnabled = false;
};

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


OperatorUiProjectionFacts Run::SampleOperatorUiProjectionFacts( const RuntimeUiTextFrameFacts& uiTextFacts,
                                                                const RuntimeFrameMetricsSnapshot& frameMetrics,
                                                                const OverlayDebugState& debug )
{
    const UI::RunSceneBrowserState& browser = m_operatorUi->SceneNavigation().browser;
    const std::string* scenePath = m_sceneController.CurrentPath();
    const SceneSessionState& scene = m_sceneController.State();
    RuntimeViewModel runtime = SampleRuntimeViewModel( scene, m_sceneController.Scene(), m_sceneController.QueueSize(),
                                                       m_capture.Screenshot(),
                                                       m_config.runtimeRender.presentationInterpolation,
                                                       uiTextFacts.presentationPinned, uiTextFacts.presentationAlpha );
    OperatorUiSceneFacts sceneFacts { runtime,
                                      scenePath ? scenePath->c_str() : nullptr,
                                      scenePath ? SceneFileNameFromPath( scenePath->c_str() ) : "",
                                      browser.namePtrs.empty() ? nullptr : browser.namePtrs.data(),
                                      browser.CurrentIndexForPath( m_sceneController.CurrentPath() ),
                                      static_cast<int>( browser.namePtrs.size() ),
                                      browser.selectedCineModeSceneIndex,
                                      m_sceneController.Scene().SceneEntityCount(),
                                      static_cast<int>( scene.rngSeed ),
                                      scene.solverBallCount,
                                      scene.solverBoxCount,
                                      frameMetrics.sceneEnergy,
                                      m_sceneController.Scene().Environment().GetGravity(),
                                      m_sceneController.Scene().Environment().GetFluidSurfaceHeight(),
                                      m_sceneController.Scene().Environment().GetFluidDensity(),
                                      m_sceneController.HasCurrentEntry(),
                                      scene.isExitOnComplete,
                                      scene.isTestComplete };
    const ReplayHudStatus replayHud = m_replayRuntime.BuildHudStatus( false );
    const Core::CinematicRenderConfig& cinematic = ActiveSceneCinematicConfig( scene, m_config );
    const bool cinematicRendering = IsSceneCinematicRenderingEnabled( scene, m_config, m_launchOptions, debug.isTextOnly,
                                                                      true );
    const bool shadowsEnabled = cinematicRendering ? cinematic.shadow.enabled : m_config.ordinaryRender.shadow.enabled;
    return { runtime, uiTextFacts, sceneFacts, replayHud, cinematic, cinematicRendering, shadowsEnabled };
}

void Run::ProjectOperatorEditorPrimaryView( UI::OperatorEditorFrameView& view, const OperatorUiProjectionFacts& facts,
                                            const RuntimeUiTextFrameFacts& uiTextFacts, bool secondarySurfaceVisible,
                                            const OverlayDebugState& debug )
{
    const RenderPresentationSettings& renderPresentation = Renderer().PresentationSettings();
    const OperatorUiGizmoMode gizmoMode = SampleOperatorUiGizmoMode( m_interaction.Gesture().kind,
                                                                     m_interaction.Gesture().gizmoKind );
    const OperatorUiRenderingFacts renderingFacts { m_config.ordinaryRender,
                                                    facts.cinematic,
                                                    uiTextFacts,
                                                    gizmoMode,
                                                    renderPresentation.vsyncEnabled,
                                                    m_config.runtimeRender.presentationInterpolation,
                                                    facts.shadowsEnabled,
                                                    facts.cinematicRendering,
                                                    debug.isTerrainHidden,
                                                    debug.isWaterHidden,
                                                    debug.isWaterFreezeDebug,
                                                    debug.isWaterFlatDebug,
                                                    debug.isWaterNoReflect,
                                                    debug.isWaterRTReflect };

    // Invariant: projection completes before either UI presenter observes the
    // frame; both surfaces receive only completed detached values.
    ProjectOperatorEditorScene( view, facts.scene );
    ProjectOperatorEditorRendering( view, renderingFacts );
    ProjectOperatorEditorLookLab( view, m_operatorUi->LookLabView() );
    ProjectOperatorEditorReplay( view, facts.replayHud.memoryPreset, facts.replayHud.requestedRetentionSeconds,
                                 facts.replayHud.requestedBudgetMiB, facts.replayHud.presentationRetentionSeconds,
                                 facts.replayHud.solverRetentionSeconds, facts.replayHud.memoryBudgetClamped,
                                 facts.replayHud.solverWindowReduced );
    ProjectOperatorEditorSurfaces( view, m_operatorUi->IsVisible(), secondarySurfaceVisible );
    ProjectOperatorEditorForecast( view, SampleOperatorUiForecastFacts( m_continuousForecast.View() ) );
}

void Run::ProjectOperatorEditorHierarchyView( UI::OperatorEditorFrameView& view )
{
    const RunEditorPlacementState& editor = m_editorTools.Editor();
    const SceneWorld& sceneWorld = m_sceneController.Scene();
    const SceneEntityStore& entities = sceneWorld.Entities();
    const int selectedRow = PeekSelectedEditorModelIndex( editor, sceneWorld.BodyStore() );
    const SceneSessionState& scene = m_sceneController.State();
    const OperatorUiHierarchyFacts facts { static_cast<uint32_t>( entities.Count() ),
                                           selectedRow,
                                           editor.objectType,
                                           UI::EditorTab::OBJECT_TYPE_COUNT,
                                           static_cast<int>( editor.history.UndoDepth() ),
                                           static_cast<int>( editor.history.RedoDepth() ),
                                           editor.history.IsDirty(),
                                           editor.editorModeEnabled,
                                           editor.placementModeEnabled,
                                           editor.placeStaticObject,
                                           m_sceneController.CrossScenePauseLocked(),
                                           scene.isFixedStep,
                                           editor.autoTerrainAlign,
                                           m_assets.FindAssetLibrarySourceAsset( "assetlib.buildings" ) != nullptr };
    BeginOperatorEditorHierarchy( view, facts );

    const uint32_t rowCount = (std::min)( facts.totalRowCount, UI::OPERATOR_EDITOR_HIERARCHY_ROW_CAPACITY );

    for ( uint32_t index = 0u; index < rowCount; ++index )
    {
        const SceneEntityRecord& entity = entities.At( static_cast<int>( index ) );
        const OperatorUiHierarchyEntityFacts entityFacts { entity.displayName,
                                                           entity.sceneObjectId.value,
                                                           entity.behaviorGroup.rootObjectId.value,
                                                           entity.behaviorGroup.partIndex,
                                                           entity.asset.isAssetBacked,
                                                           entity.editorVisible,
                                                           entity.editorLocked };
        AppendOperatorEditorHierarchyRow( view, facts, entityFacts, index );
    }
}

void Run::ProjectOperatorEditorInspectorView( UI::OperatorEditorFrameView& view )
{
    (void)view;
}

void Run::SampleOperatorUiDiagnosticsFacts( OperatorUiDiagnosticsFacts& facts, const RuntimeRenderFrameViews& renderFrame,
                                            const ReplayHudStatus& replayHud, const RuntimeFrameMetricsSnapshot& metrics )
{
    RuntimeRenderer& renderer = Renderer();
    facts.metrics = metrics;
    facts.visibility = renderer.RenderDiagnostics().GetFrameVisibilityStats();
    facts.renderMemory = renderer.RenderDiagnostics().GetRenderMemoryStats();
    facts.drawTrace = renderer.RenderDiagnostics().GetFrameDrawCallTrace();
    facts.workerThreadCount = m_workerPool.GetThreadCount();
    facts.maxWorkerThreadCount = Threading::WorkerPool::MaxThreadCount();
    facts.replayMemoryPreset = replayHud.memoryPreset;
    facts.replayRequestedRetentionSeconds = replayHud.requestedRetentionSeconds;
    facts.replayRequestedBudgetMiB = replayHud.requestedBudgetMiB;
    facts.replayPresentationRetentionSeconds = replayHud.presentationRetentionSeconds;
    facts.replaySolverRetentionSeconds = replayHud.solverRetentionSeconds;
    facts.replayMemoryBudgetClamped = replayHud.memoryBudgetClamped;
    facts.replayMemorySolverWindowReduced = replayHud.solverWindowReduced;
    facts.predictionRevealRate = replayHud.predictionRevealRate;
    facts.now = metrics.simulationTotalSeconds;
#if defined( SKULLBONEZ_PROFILE_ENABLED )
    if ( !m_profiler )
    {
        SB_FATAL( "Runtime/App/OperatorEditorFramePhase", "Profile UI sampling requires the startup-bound profiler." );
    }

    facts.markerCount = (std::min)( m_profiler->MarkerCount(), UI::ProfilerTab::MAX_MARKERS );

    for ( int markerIndex = 0; markerIndex < facts.markerCount; ++markerIndex )
    {
        const Core::Profiler::Marker& source = m_profiler->GetMarker( markerIndex );
        OperatorUiProfilerMarkerFacts& target = facts.markers[static_cast<std::size_t>( markerIndex )];
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

    facts.workerSampleCount = (std::min)( m_profiler->WorkerCoreSampleCount(), UI::ProfilerTab::MAX_WORKER_CORE_SAMPLES );

    for ( int sampleIndex = 0; sampleIndex < facts.workerSampleCount; ++sampleIndex )
    {
        const Core::Profiler::WorkerCoreSample& source = m_profiler->GetWorkerCoreSample( sampleIndex );
        OperatorUiWorkerCoreFacts& target = facts.workerSamples[static_cast<std::size_t>( sampleIndex )];
        target.workerIndex = source.workerIndex;
        target.jobCount = source.jobCount;
        target.coreMs = source.coreMs;
        target.avgCoreMs = source.avgCoreMs;
        target.spanStartMs = source.spanStartMs;
        target.spanEndMs = source.spanEndMs;
    }
#endif

    UI::InGameUI& ui = *m_operatorUi;
    const bool memoryTabActive = ui.IsVisible() && !ui.IsMinimized() && ui.GetActiveTab() == UI::InGameUITab::Memory;
    const bool memoryOverlayEnabled = ui.IsMemoryOverlayEnabled();

    if ( memoryTabActive && replayHud.memoryStatsValid )
    {
        facts.mainMemory = m_diagnosticsRuntime.RefreshMainMemoryStats( replayHud.memoryStats,
                                                                        renderFrame.diagnostics.gameObjectMemory, facts.now,
                                                                        false, false );
    }
    else if ( memoryOverlayEnabled )
    {
        facts.mainMemory = SampleMainMemoryOverlayStats( m_diagnosticsRuntime, renderFrame.diagnostics.gameObjectMemory );
    }

    if ( memoryTabActive || memoryOverlayEnabled )
    {
        facts.renderMemoryAvailable = true;
        facts.reserveGrowthEventTotalCount = CoreAllocation::RuntimeReserveAllocator::GrowthEventCount();
        facts.reserveGrowthEventDroppedCount = CoreAllocation::RuntimeReserveAllocator::GrowthEventDroppedCount();
        facts.reserveGrowthEventCount = CoreAllocation::RuntimeReserveAllocator::
            CopyRecentGrowthEvents( facts.reserveGrowthEvents.data(), static_cast<int>( facts.reserveGrowthEvents.size() ) );
    }

    if ( memoryTabActive )
    {
        facts.reserveCapacityAvailable = true;
        const std::span<const CoreAllocation::RuntimeReserveCapacityView>
            capacityRows = CoreAllocation::RuntimeReserveAllocator::CapacityRows();
        facts.reserveCapacityRowCount = (std::min)( static_cast<int>( capacityRows.size() ),
                                                    static_cast<int>( facts.reserveCapacityRows.size() ) );

        for ( int index = 0; index < facts.reserveCapacityRowCount; ++index )
        {
            facts.reserveCapacityRows[static_cast<std::size_t>( index )] = capacityRows[static_cast<std::size_t>( index )];
        }
    }
}

void Run::BuildOperatorGameUiData( UI::InGameUIFrameData& uiData, const OperatorUiProjectionFacts& projection,
                                   const RuntimeRenderFrameViews& renderFrame,
                                   const UI::OperatorEditorFrameView& operatorEditorView,
                                   const RuntimeFrameMetricsSnapshot& metrics, const UiTextViewport& uiViewport,
                                   int uiDrawCallStart, const OverlayDebugState& debug,
                                   RuntimeRenderTargetPreviewSnapshot& renderTargetPreviews )
{
    UI::UIRuntimeReserveCapacityRow reserveCapacityRows[UI::UI_RUNTIME_RESERVE_CAPACITY_ROW_MAX] = {};
    OperatorUiDiagnosticsFacts diagnostics;
    SampleOperatorUiDiagnosticsFacts( diagnostics, renderFrame, projection.replayHud, metrics );
    ProjectOperatorUiDiagnostics( uiData, diagnostics, reserveCapacityRows );

    const SceneWorld& sceneWorld = m_sceneController.Scene();
    const Gameplay::TornadoFieldConfig& tornado = sceneWorld.Tornado().GetFieldConfig();
    const RenderPresentationSettings& presentation = Renderer().PresentationSettings();
    const OperatorUiSettingsFacts settings { m_config.ordinaryRender,
                                             projection.cinematic,
                                             BuildDiagnosticsPhysicsUIStatus( debug ),
                                             Core::ActiveSceneObjectCapacity( m_config ),
                                             sceneWorld.Environment().GetGravity(),
                                             sceneWorld.Environment().GetFluidSurfaceHeight(),
                                             sceneWorld.Environment().GetFluidDensity(),
                                             tornado.radius,
                                             tornado.height,
                                             tornado.inwardAcceleration,
                                             tornado.swirlAcceleration,
                                             tornado.liftAcceleration,
                                             m_config.physicsMaterial.frictionCoeff,
                                             m_config.physicsMaterial.objectFrictionCoeff,
                                             m_config.physicsMaterial.rollingFrictionCoeff,
                                             debug.isTextOnly,
                                             presentation.vsyncEnabled,
                                             presentation.pipelineSyncEnabled,
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
                                             projection.cinematicRendering };
    ProjectOperatorUiSettings( uiData, settings );

    const RunRayCastTestState& rayCast = m_runtimeTools.RayCastTest();
    const RunEditorPlacementState& editor = m_editorTools.Editor();
    const RuntimeInputMode inputMode = m_inputRouter.RuntimeContext().CurrentMode();
    const RuntimeUiTextFrameFacts& uiText = projection.uiText;
    const OperatorUiInteractionFacts interaction { uiText.cameraModeLabel,
                                                   m_camera.trackBallRow.IsValid() ? m_camera.trackHeight : 0.0f,
                                                   m_camera.autoCycleInterval > 0.0f ? m_camera.autoCycleInterval : 0.0f,
                                                   rayCast.impulseStrength,
                                                   rayCast.projectileSpeed,
                                                   uiText.cameraModeEnabledMask,
                                                   static_cast<int>( m_camera.mode ),
                                                   editor.objectType,
                                                   static_cast<int>( editor.history.UndoDepth() ),
                                                   static_cast<int>( editor.history.RedoDepth() ),
                                                   rayCast.visualizeRays,
                                                   ( inputMode == RuntimeInputMode::FlyCamera ||
                                                     inputMode == RuntimeInputMode::Launcher ||
                                                     inputMode == RuntimeInputMode::EditorViewportLook ) &&
                                                       !m_operatorUi->BlocksCameraMouse(),
                                                   editor.editorModeEnabled,
                                                   editor.placementModeEnabled,
                                                   editor.placeStaticObject,
                                                   editor.autoTerrainAlign,
                                                   editor.viewportLookActive };
    ProjectOperatorUiInteraction( uiData, interaction );
    ProjectOperatorUiPresentation( uiData, projection.scene, operatorEditorView );
    ProjectOperatorUiViewport( uiData, uiViewport.screenW, uiViewport.screenH );

    const RenderDiagnosticsReadout readout = Renderer().BuildDiagnosticsReadout();
    ProjectOperatorUiRenderIdentity( uiData, readout.rendererName.data(), uiDrawCallStart );
    const UI::InteractionRecordingBrowserState& recordings = m_operatorUi->SceneNavigation().recordings;
    ProjectOperatorUiRecordingBrowser( uiData, recordings.namePtrs.empty() ? nullptr : recordings.namePtrs.data(),
                                       static_cast<int>( recordings.namePtrs.size() ),
                                       recordings.paths.empty() ? -1 : recordings.selectedIndex );

    Renderer().AppendDxrReflectionPreview( renderTargetPreviews, uiViewport,
                                           settings.waterRtReflect && !settings.waterNoReflect );
    OperatorUiRenderTargetListFacts renderTargets;
    const int targetCount = (std::min)( renderTargetPreviews.count, UI::UI_RENDER_TARGET_PREVIEW_MAX );

    for ( int index = 0; index < targetCount; ++index )
    {
        const RuntimeRenderTargetPreview& source = renderTargetPreviews.targets[static_cast<std::size_t>( index )];
        static_cast<void>( renderTargets.Append( source.label, source.width, source.height,
                                                 source.available && source.width > 0 && source.height > 0, source.depth,
                                                 source.hdr ) );
    }

    ProjectOperatorUiRenderTargets( uiData, renderTargets );
}

int Run::RenderOperatorUiTextPass( OperatorUiPhaseOwner& operatorUiPhase, const OperatorUiProjectionFacts& projection,
                                   const RuntimeRenderFrameViews& renderFrame,
                                   const UI::OperatorEditorFrameView& operatorEditorView,
                                   const ReplayOverlay::ReplayOverlayStateView& replayOverlay,
                                   RuntimeRenderTargetPreviewSnapshot& renderTargetPreviews, const OverlayDebugState& debug )
{
    RuntimeRenderer& renderer = Renderer();
    UI::InGameUI& ui = *m_operatorUi;
    const SceneSessionState& scene = m_sceneController.State();
    const bool replayPathVisualizerHasTarget = m_replayRuntime.BuildInputView().hasPathTarget;
    const UiTextVisibility visibility { debug.isTextOnly,
                                        scene.isSceneMode,
                                        scene.isSceneText,
                                        debug.overlayMode != OverlayMode::None,
                                        ui.NeedsUiTextPass(),
                                        m_sceneController.CrossScenePauseLocked(),
                                        debug.isTopTextHidden,
                                        scene.isTestComplete,
                                        replayOverlay.timeline.shouldRenderScrubber,
                                        replayPathVisualizerHasTarget,
                                        ProjectUiCameraBadgeMode( m_camera.mode ) != UiCameraBadgeMode::Quiet };

    renderer.PrepareUiFrameTarget();

    if ( !renderer.ResourceLifecycle().ShouldRenderUiText( visibility ) )
    {
        return 0;
    }

    renderTargetPreviews = renderer.ResourceLifecycle()
                               .BuildRenderTargetPreviewSnapshot( projection.shadowsEnabled, projection.cinematicRendering,
                                                                  projection.cinematicRendering &&
                                                                      projection.cinematic.volumetricLightingEnabled );
    const bool memoryStatsRequested = ui.IsVisible() && !ui.IsMinimized() && ui.GetActiveTab() == UI::InGameUITab::Memory;
    const ReplayHudStatus replayHud = m_replayRuntime.BuildHudStatus( memoryStatsRequested );
    const UiTextViewport viewport { operatorUiPhase.Snapshot().viewportWidth, operatorUiPhase.Snapshot().viewportHeight };
    const RuntimeFrameMetricsSnapshot& metrics = operatorUiPhase.Snapshot().metrics;
    const OperatorUiSubmissionPlan& submission = operatorUiPhase.SubmissionPlan();

    PROFILE_BEGIN( "Frame/UI" );
    CoreAllocation::RuntimeAllocationScope allocationScope( CoreAllocation::RuntimeAllocationPhase::Render );
    const int drawCallStart = renderer.BeginUiTextFrame( viewport );
    UiChromeStatusValues chromeStatus;
    chromeStatus.textOnly = debug.isTextOnly;
    chromeStatus.topTextHidden = debug.isTopTextHidden;
    chromeStatus.sceneMode = scene.isSceneMode;
    chromeStatus.sceneTestComplete = scene.isTestComplete;
    chromeStatus.crossScenePauseLocked = m_sceneController.CrossScenePauseLocked();
    chromeStatus.currentFrame = scene.currentFrame;
    chromeStatus.targetFrameCount = scene.targetFrameCount;
    chromeStatus.currentSceneIndex = scene.currentSceneIndex;
    chromeStatus.sceneQueueSize = m_sceneController.QueueSize();
    chromeStatus.cameraMode = ProjectUiCameraBadgeMode( m_camera.mode );
    chromeStatus.cameraModeLabel = projection.uiText.cameraModeLabel;
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
    chromeTail.launcherCameraMode = projection.uiText.isLauncherCameraMode;
    chromeTail.launcherFireModeLabel = projection.uiText.launcherFireModeLabel;
#if defined( _DEBUG )
    chromeTail.reproSnapshotMessage = debug.reproSnapshotMessage;
    chromeTail.reproMessageAgeSeconds = metrics.sceneElapsedSeconds;
    chromeTail.reproSnapshotMessageUntil = debug.reproSnapshotMessageUntil;
#endif
    renderer.SubmitUiChrome( viewport, chromeStatus, chromeTail );

    if ( submission.composeGameUi )
    {
        // UI composition produces a backend-neutral draw list. Render consumes
        // it synchronously with the parallel preview catalog.
        UI::InGameUIFrameData uiData;
        renderer.PrepareOperatorUiSubmission( viewport, debug.isUITestPattern );
        OperatorUiProjectionFacts gameUiProjection = projection;
        gameUiProjection.replayHud = replayHud;
        BuildOperatorGameUiData( uiData, gameUiProjection, renderFrame, operatorEditorView, metrics, viewport, drawCallStart,
                                 debug, renderTargetPreviews );
        const UI::UIDrawList& drawList = ui.Draw( uiData );
        renderer.SubmitOperatorUiDrawList( drawList, renderTargetPreviews, m_assets, viewport );
    }

    if ( submission.submitOverlay )
    {
        const float rollingFps = metrics.rollingFrameSeconds > 0.0f ? 1.0f / metrics.rollingFrameSeconds : 0.0f;
        renderer.SubmitUiOverlay( viewport, ProjectUiOverlayMode( debug.overlayMode ), scene.modelCount, rollingFps,
                                  metrics.sceneEnergy );
    }

    if ( submission.submitReplay )
    {
        const UI::UIDrawList&
            drawList = m_replayRuntime.ComposeOverlayDrawList( replayOverlay, projection.uiText.gameUiActive,
                                                               scene.isScenePhysics,
                                                               projection.uiText.interactionGestureKind,
                                                               { viewport.screenW, viewport.screenH,
                                                                 m_window.GetProjectionMatrix() *
                                                                     m_sceneController.Scene().Cameras().GetViewMatrix() },
                                                               metrics.simulationTotalSeconds );
        renderer.SubmitUiDrawList( drawList, viewport );
    }

    if ( submission.finalizeOverlay )
    {
        renderer.FinalizeUiOverlay( ProjectUiOverlayMode( debug.overlayMode ) );
    }

    const int drawCalls = renderer.EndUiTextFrame( drawCallStart );
    PROFILE_END( "Frame/UI" );
    return drawCalls;
}


void Run::RenderOperatorUiPhase( const RuntimeRenderFrameViews& renderFrame, float presentationAlpha,
                                 bool capturePresentationPinned, double secondsPerFrame, bool gameUiActive,
                                 const RuntimeFrameMetricsSnapshot& frameMetrics )
{
    SkullbonezCore::UI::OperatorEditorFrameView operatorEditorView;
    bool secondarySurfaceVisible = false;
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

    OperatorUiPhaseOwner operatorUiPhase;
    operatorUiPhase.Begin( operatorUiSnapshot );

    const RuntimeUiTextFrameFacts& uiTextFacts = operatorUiPhase.Snapshot().uiText;

    const ReplayOverlay::ReplayOverlayStateView
        replayOverlay = m_replayRuntime.BuildOverlayStateView( m_editorTools.Editor().editorModeEnabled,
                                                               m_operatorUi->IsVisible(), m_operatorUi->IsMinimized(),
                                                               m_interaction.Gesture().kind,
                                                               renderFrame.modelPresentation.presentationRecords,
                                                               renderFrame.debug.physics.bodyStore );

    RuntimeOverlayPresentationEdit presentationEdit = m_overlayDiagnostics->EditPresentation();
    OverlayDebugState& debug = presentationEdit.State();

    // Lifetime: the projection is a detached value snapshot shared by both
    // presenters for this call only.
    const OperatorUiProjectionFacts projection = SampleOperatorUiProjectionFacts( uiTextFacts, frameMetrics, debug );
    ProjectOperatorEditorPrimaryView( operatorEditorView, projection, uiTextFacts, secondarySurfaceVisible, debug );
    ProjectOperatorEditorHierarchyView( operatorEditorView );
    ProjectOperatorEditorInspectorView( operatorEditorView );

    RuntimeRenderTargetPreviewSnapshot renderTargetPreviews;

    const bool profilerBars = debug.overlayMode == OverlayMode::BarsNormalized ||
                              debug.overlayMode == OverlayMode::BarsAbsolute;
    operatorUiPhase.Compose( debug.isTextOnly, m_operatorUi->NeedsUiTextPass(), m_operatorUi->IsVisible(), profilerBars );

    const int gameUiDrawCalls = RenderOperatorUiTextPass( operatorUiPhase, projection, renderFrame, operatorEditorView,
                                                          replayOverlay, renderTargetPreviews, debug );
    operatorUiPhase.RecordGpuSubmission( gameUiDrawCalls );
    m_timers.RecordUiDrawCalls( operatorUiPhase.GameUiDrawCalls() );

    operatorUiPhase.Complete();
}


} // namespace Runtime
} // namespace SkullbonezCore
