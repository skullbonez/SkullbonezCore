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
namespace OperatorEditorFrameComposer
{

static UiCameraBadgeMode ProjectUiCameraBadgeMode( RunCameraMode mode )
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

static UiOverlayMode ProjectUiOverlayMode( OverlayMode mode )
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

static RuntimeViewModel BuildRuntimeViewModel( const SceneSessionState& scene, const SceneWorld& world, int sceneCount,
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

    // Why: Physics body rows remain the runtime count authority; App copies
    // the scalar before UI receives this detached presentation value.
    view.modelCount = SkullbonezCore::Physics::PhysicsEngine::ReadBodies( world.Physics() ).Count();
    view.timeScale = scene.timeScale;
    view.presentationInterpolation = presentationInterpolation;
    view.presentationPinned = presentationPinned;
    view.presentationAlpha = std::clamp( presentationAlpha, 0.0f, 1.0f );
    return view;
}

static SkullbonezCore::UI::OperatorEditorForecastCause MapForecastCause( ContinuousOrbitalInstabilityCause cause ) noexcept
{
    using Cause = ContinuousOrbitalInstabilityCause;
    using ViewCause = SkullbonezCore::UI::OperatorEditorForecastCause;

    switch ( cause )
    {
    case Cause::InvalidContract:
        return ViewCause::InvalidContract;
    case Cause::NonFiniteState:
        return ViewCause::NonFiniteState;
    case Cause::PrivateStepFailure:
        return ViewCause::PrivateStepFailure;
    case Cause::InvalidPublication:
        return ViewCause::InvalidPublication;
    case Cause::InnerEnvelope:
        return ViewCause::InnerEnvelope;
    case Cause::OuterEnvelope:
        return ViewCause::OuterEnvelope;
    case Cause::SustainedEscape:
        return ViewCause::SustainedEscape;
    case Cause::Collision:
        return ViewCause::Collision;
    case Cause::None:
    default:
        return ViewCause::None;
    }
}

static void FillOperatorRenderingParameters( SkullbonezCore::UI::OperatorEditorRenderingView& view,
                                             const SkullbonezCore::Core::OrdinaryRenderConfig& ordinary,
                                             const SkullbonezCore::Core::CinematicRenderConfig& cinematic )
{
    using SkullbonezCore::UI::UICinematicFeature;
    using SkullbonezCore::UI::UICinematicParam;
    using SkullbonezCore::UI::UIRenderParam;

    // Invariant: every enum slot crosses the runtime/presentation boundary
    // explicitly. The count assertions make a newly authored parameter fail
    // the build until this bounded projection is updated.
    static_assert( static_cast<int>( UIRenderParam::Count ) ==
                   SkullbonezCore::UI::OperatorEditorRenderingView::ordinaryParameterCount );

    static_assert( static_cast<int>( UICinematicParam::Count ) ==
                   SkullbonezCore::UI::OperatorEditorRenderingView::cinematicParameterCount );

    static_assert( static_cast<int>( UICinematicFeature::Count ) ==
                   SkullbonezCore::UI::OperatorEditorRenderingView::cinematicFeatureCount );

    const auto ordinaryValue = [&]( UIRenderParam parameter, float value )
    { view.ordinaryParameters[static_cast<int>( parameter )] = value; };

    ordinaryValue( UIRenderParam::SunIntensity, ordinary.sunIntensity );
    ordinaryValue( UIRenderParam::SunRed, ordinary.sunColorR );
    ordinaryValue( UIRenderParam::SunGreen, ordinary.sunColorG );
    ordinaryValue( UIRenderParam::SunBlue, ordinary.sunColorB );
    ordinaryValue( UIRenderParam::AmbientStrength, ordinary.ambientStrength );
    ordinaryValue( UIRenderParam::SkyRed, ordinary.skyAmbientR );
    ordinaryValue( UIRenderParam::SkyGreen, ordinary.skyAmbientG );
    ordinaryValue( UIRenderParam::SkyBlue, ordinary.skyAmbientB );
    ordinaryValue( UIRenderParam::GroundRed, ordinary.groundAmbientR );
    ordinaryValue( UIRenderParam::GroundGreen, ordinary.groundAmbientG );
    ordinaryValue( UIRenderParam::GroundBlue, ordinary.groundAmbientB );
    ordinaryValue( UIRenderParam::ShadowStrength, ordinary.shadow.strength );
    ordinaryValue( UIRenderParam::ShadowSoftness, ordinary.shadow.softness );
    ordinaryValue( UIRenderParam::ShadowDepthBias, ordinary.shadow.depthBias );
    ordinaryValue( UIRenderParam::ShadowSlopeBias, ordinary.shadow.slopeBias );
    ordinaryValue( UIRenderParam::WaterRed, ordinary.waterTintR );
    ordinaryValue( UIRenderParam::WaterGreen, ordinary.waterTintG );
    ordinaryValue( UIRenderParam::WaterBlue, ordinary.waterTintB );
    ordinaryValue( UIRenderParam::WaterAlpha, ordinary.waterAlpha );
    ordinaryValue( UIRenderParam::WaterReflection, ordinary.waterReflectionStrength );
    ordinaryValue( UIRenderParam::WaterFresnel, ordinary.waterFresnelF0 );
    ordinaryValue( UIRenderParam::BallRoughness, ordinary.ballRoughnessScale );
    ordinaryValue( UIRenderParam::BallSpecular, ordinary.ballSpecularScale );
    ordinaryValue( UIRenderParam::BoxRoughness, ordinary.boxRoughnessScale );
    ordinaryValue( UIRenderParam::BoxSpecular, ordinary.boxSpecularScale );
    ordinaryValue( UIRenderParam::TrajectoryFutureWidth, ordinary.replayTrajectory.futureWidth );
    ordinaryValue( UIRenderParam::TrajectoryFutureAlpha, ordinary.replayTrajectory.futureAlpha );
    ordinaryValue( UIRenderParam::TrajectoryFutureEdgeFeather, ordinary.replayTrajectory.futureEdgeFeather );
    ordinaryValue( UIRenderParam::TrajectoryCausalWidth, ordinary.replayTrajectory.causalWidth );
    ordinaryValue( UIRenderParam::TrajectoryCausalAlpha, ordinary.replayTrajectory.causalAlpha );
    ordinaryValue( UIRenderParam::TrajectoryCausalEdgeFeather, ordinary.replayTrajectory.causalEdgeFeather );
    ordinaryValue( UIRenderParam::TrajectoryBaselineWidth, ordinary.replayTrajectory.baselineWidth );
    ordinaryValue( UIRenderParam::TrajectoryBaselineAlpha, ordinary.replayTrajectory.baselineAlpha );
    ordinaryValue( UIRenderParam::TrajectoryBaselineEdgeFeather, ordinary.replayTrajectory.baselineEdgeFeather );
    ordinaryValue( UIRenderParam::TrajectoryMarkerWidth, ordinary.replayTrajectory.markerWidth );
    ordinaryValue( UIRenderParam::TrajectoryMarkerAlpha, ordinary.replayTrajectory.markerAlpha );
    ordinaryValue( UIRenderParam::TrajectoryMarkerEdgeFeather, ordinary.replayTrajectory.markerEdgeFeather );
    ordinaryValue( UIRenderParam::TrajectorySelectedEmphasis, ordinary.replayTrajectory.selectedEmphasis );

    const auto cinematicValue = [&]( UICinematicParam parameter, float value )
    { view.cinematicParameters[static_cast<int>( parameter )] = value; };

    cinematicValue( UICinematicParam::Exposure, cinematic.exposure );
    cinematicValue( UICinematicParam::Gamma, cinematic.gamma );
    cinematicValue( UICinematicParam::SkyMode, static_cast<float>( cinematic.skyMode ) );
    cinematicValue( UICinematicParam::TerrainMode, static_cast<float>( cinematic.terrainMode ) );
    cinematicValue( UICinematicParam::ObjectStyle, static_cast<float>( cinematic.objectStyle ) );
    cinematicValue( UICinematicParam::WaterMode, static_cast<float>( cinematic.waterMode ) );
    cinematicValue( UICinematicParam::StyleSaturation, cinematic.styleSaturation );
    cinematicValue( UICinematicParam::StyleContrast, cinematic.styleContrast );
    cinematicValue( UICinematicParam::StyleVignette, cinematic.styleVignette );
    cinematicValue( UICinematicParam::SunAzimuth, cinematic.sunAzimuth );
    cinematicValue( UICinematicParam::SunElevation, cinematic.sunElevation );
    cinematicValue( UICinematicParam::SunBrightness, cinematic.sunIntensity );
    cinematicValue( UICinematicParam::SunRed, cinematic.sunColorR );
    cinematicValue( UICinematicParam::SunGreen, cinematic.sunColorG );
    cinematicValue( UICinematicParam::SunBlue, cinematic.sunColorB );
    cinematicValue( UICinematicParam::SkyGlow, cinematic.skyGlowStrength );
    cinematicValue( UICinematicParam::HorizonRed, cinematic.skyHorizonR );
    cinematicValue( UICinematicParam::HorizonGreen, cinematic.skyHorizonG );
    cinematicValue( UICinematicParam::HorizonBlue, cinematic.skyHorizonB );
    cinematicValue( UICinematicParam::ZenithRed, cinematic.skyZenithR );
    cinematicValue( UICinematicParam::ZenithGreen, cinematic.skyZenithG );
    cinematicValue( UICinematicParam::ZenithBlue, cinematic.skyZenithB );
    cinematicValue( UICinematicParam::CloudCoverage, cinematic.cloudCoverage );
    cinematicValue( UICinematicParam::CloudSoftness, cinematic.cloudSoftness );
    cinematicValue( UICinematicParam::CloudScale, cinematic.cloudScale );
    cinematicValue( UICinematicParam::CloudIntensity, cinematic.cloudIntensity );
    cinematicValue( UICinematicParam::ShaftStrength, cinematic.sunShaftStrength );
    cinematicValue( UICinematicParam::ShaftFalloff, cinematic.sunShaftFalloff );
    cinematicValue( UICinematicParam::VolumetricStrength, cinematic.volumetricStrength );
    cinematicValue( UICinematicParam::VolumetricDensity, cinematic.volumetricDensity );
    cinematicValue( UICinematicParam::VolumetricDecay, cinematic.volumetricDecay );
    cinematicValue( UICinematicParam::BloomThreshold, cinematic.bloomThreshold );
    cinematicValue( UICinematicParam::BloomKnee, cinematic.bloomKnee );
    cinematicValue( UICinematicParam::BloomStrength, cinematic.bloomStrength );
    cinematicValue( UICinematicParam::BloomRadius, cinematic.bloomRadius );
    cinematicValue( UICinematicParam::TerrainRelief, cinematic.terrainRelief );
    cinematicValue( UICinematicParam::TerrainTintRed, cinematic.terrainTintR );
    cinematicValue( UICinematicParam::TerrainTintGreen, cinematic.terrainTintG );
    cinematicValue( UICinematicParam::TerrainTintBlue, cinematic.terrainTintB );
    cinematicValue( UICinematicParam::TerrainAccentRed, cinematic.terrainAccentR );
    cinematicValue( UICinematicParam::TerrainAccentGreen, cinematic.terrainAccentG );
    cinematicValue( UICinematicParam::TerrainAccentBlue, cinematic.terrainAccentB );
    cinematicValue( UICinematicParam::TerrainGridScale, cinematic.terrainGridScale );
    cinematicValue( UICinematicParam::TerrainGridStrength, cinematic.terrainGridStrength );
    cinematicValue( UICinematicParam::WaterTintRed, cinematic.waterTintR );
    cinematicValue( UICinematicParam::WaterTintGreen, cinematic.waterTintG );
    cinematicValue( UICinematicParam::WaterTintBlue, cinematic.waterTintB );
    cinematicValue( UICinematicParam::WaterAlpha, cinematic.waterAlpha );
    cinematicValue( UICinematicParam::WaterReflection, cinematic.waterReflectionStrength );
    cinematicValue( UICinematicParam::WaterGlint, cinematic.waterGlintStrength );
    cinematicValue( UICinematicParam::BasinCenterX, cinematic.basinCenterX );
    cinematicValue( UICinematicParam::BasinCenterZ, cinematic.basinCenterZ );
    cinematicValue( UICinematicParam::BasinRadiusX, cinematic.basinRadiusX );
    cinematicValue( UICinematicParam::BasinRadiusZ, cinematic.basinRadiusZ );
    cinematicValue( UICinematicParam::BasinFeather, cinematic.basinFeather );
    cinematicValue( UICinematicParam::BasinDepth, cinematic.basinDepth );
    cinematicValue( UICinematicParam::BasinRimLift, cinematic.basinRimLift );
    cinematicValue( UICinematicParam::FogDensity, cinematic.fogDensity );
    cinematicValue( UICinematicParam::FogOpacity, cinematic.fogMaxOpacity );
    cinematicValue( UICinematicParam::FogStart, cinematic.fogStart );
    cinematicValue( UICinematicParam::FogEnd, cinematic.fogEnd );
    cinematicValue( UICinematicParam::FogRed, cinematic.fogColorR );
    cinematicValue( UICinematicParam::FogGreen, cinematic.fogColorG );
    cinematicValue( UICinematicParam::FogBlue, cinematic.fogColorB );

    view.cinematicFeatures[static_cast<int>( UICinematicFeature::Sky )] = cinematic.skyAtmosphereEnabled;
    view.cinematicFeatures[static_cast<int>( UICinematicFeature::Clouds )] = cinematic.cloudsEnabled;
    view.cinematicFeatures[static_cast<int>( UICinematicFeature::GodRays )] = cinematic.godRaysEnabled;
    view.cinematicFeatures[static_cast<int>( UICinematicFeature::VolumetricLight )] = cinematic.volumetricLightingEnabled;

    view.cinematicFeatures[static_cast<int>( UICinematicFeature::Bloom )] = cinematic.bloomEnabled;
    view.cinematicFeatures[static_cast<int>( UICinematicFeature::Fog )] = cinematic.fogEnabled;
    view.cinematicFeatures[static_cast<int>( UICinematicFeature::TerrainRelief )] = cinematic.terrainReliefEnabled;
    view.cinematicFeatures[static_cast<int>( UICinematicFeature::Shadows )] = cinematic.shadow.enabled;
}

} // namespace OperatorEditorFrameComposer

using namespace OperatorEditorFrameComposer;

void Run::RenderOperatorUiPhase( const RuntimeRenderModelFrameView& renderModels,
                                 const FramePresentationFacts& presentationFacts,
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
                                  presentationFacts.presentationAlpha,
                                  presentationFacts.capturePresentationPinned,
                                  presentationFacts.secondsPerFrame,
                                  presentationFacts.gameUiActive };

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

    // Invariant: build this common value once. The GameUI draw pass and the
    // secondary editor receive this exact object, not independently sampled owners.
    operatorEditorView.scene = { uiScenePath ? uiScenePath->c_str() : "",
                                 uiSceneBrowser.namePtrs.empty() ? nullptr : uiSceneBrowser.namePtrs.data(),
                                 uiSceneBrowser.CurrentIndexForPath( sceneController.CurrentPath() ),
                                 static_cast<int>( uiSceneBrowser.namePtrs.size() ),
                                 scene.currentFrame,
                                 sceneController.Scene().SceneEntityCount(),
                                 scene.timeScale,
                                 uiScenePath && !uiScenePath->empty(),
                                 false };

    operatorEditorView.property = { sceneController.Scene().Environment().GetGravity(),
                                    sceneController.Scene().Environment().GetFluidSurfaceHeight(),
                                    sceneController.Scene().Environment().GetFluidDensity() };

    SkullbonezCore::UI::OperatorEditorRenderingView& sharedRendering = operatorEditorView.rendering;
    sharedRendering.vsyncEnabled = renderer.PresentationSettings().vsyncEnabled;
    sharedRendering.shadowsEnabled = sharedShadows;
    sharedRendering.cinematicRendering = sharedCinematicRendering;
    sharedRendering.presentationInterpolation = config.runtimeRender.presentationInterpolation;
    sharedRendering.presentationAlpha = uiTextFacts.presentationAlpha;
    sharedRendering.terrainHidden = debug.isTerrainHidden;
    sharedRendering.waterHidden = debug.isWaterHidden;
    sharedRendering.waterFrozen = debug.isWaterFreezeDebug;
    sharedRendering.waterFlat = debug.isWaterFlatDebug;
    sharedRendering.waterReflectionMode = debug.isWaterNoReflect ? 2 : ( debug.isWaterRTReflect ? 1 : 0 );
    FillOperatorRenderingParameters( sharedRendering, config.ordinaryRender, sharedCinematic );
    const char* sharedGizmoMode = "translate";

    if ( uiTextFacts.interactionGestureKind == RuntimeInteractionGestureKind::GizmoDrag )
    {
        switch ( uiTextFacts.interactionGizmoKind )
        {
        case RuntimeGizmoDragKind::Rotate:
            sharedGizmoMode = "rotate";
            break;
        case RuntimeGizmoDragKind::Scale:
            sharedGizmoMode = "scale";
            break;
        case RuntimeGizmoDragKind::Translate:
        case RuntimeGizmoDragKind::None:
        default:
            break;
        }
    }

    operatorEditorView.viewport = { uiTextFacts.cameraModeLabel, sharedGizmoMode, uiTextFacts.presentationPinned };

    operatorEditorView.replay = { sharedReplayHud.memoryPreset,           sharedReplayHud.requestedRetentionSeconds,
                                  sharedReplayHud.requestedBudgetMiB,     sharedReplayHud.presentationRetentionSeconds,
                                  sharedReplayHud.solverRetentionSeconds, sharedReplayHud.memoryBudgetClamped,
                                  sharedReplayHud.solverWindowReduced };

    const ContinuousOrbitalForecastView forecast = m_continuousForecast.View();
    const bool blockingFailureFirst = forecast.stability.firstBlockingFailure.latched &&
                                      ( !forecast.stability.firstAuxiliaryFailure.latched ||
                                        forecast.stability.firstBlockingFailure.absoluteTick <=
                                            forecast.stability.firstAuxiliaryFailure.absoluteTick );
    const ContinuousOrbitalFailure& firstFailure = blockingFailureFirst ? forecast.stability.firstBlockingFailure
                                                                        : forecast.stability.firstAuxiliaryFailure;
    SkullbonezCore::UI::OperatorEditorForecastView& sharedForecast = operatorEditorView.forecast;
    sharedForecast.simulatedSeconds = forecast.simulatedSeconds;
    sharedForecast.simulatedSecondsPerRealSecond = forecast.simulatedSecondsPerRealSecond;
    sharedForecast.rollingWindowAgeSeconds = forecast.rollingWindowAgeSeconds;
    sharedForecast.energyDrift = forecast.stability.conservation.energyDrift;
    sharedForecast.angularMomentumDrift = forecast.stability.conservation.angularMomentumDrift;
    sharedForecast.maximumAbsoluteEnergyDrift = forecast.stability.conservation.maximumAbsoluteEnergyDrift;
    sharedForecast.maximumAngularMomentumDrift = forecast.stability.conservation.maximumAngularMomentumDrift;
    sharedForecast.firstFailureSeconds = firstFailure.simulatedSeconds;
    sharedForecast.newestAbsoluteTick = forecast.newestAbsoluteTick;
    sharedForecast.retainedBytes = static_cast<uint64_t>( forecast.retainedBytes );
    sharedForecast.firstFailureSubject = firstFailure.subject.value;
    sharedForecast.firstFailureOther = firstFailure.other.value;
    sharedForecast.firstFailureCause = MapForecastCause( firstFailure.cause );
    sharedForecast.available = forecast.available;
    sharedForecast.active = forecast.active;
    sharedForecast.workerInFlight = forecast.workerInFlight;
    sharedForecast.failed = forecast.failed;
    sharedForecast.configured = forecast.stability.configured;
    sharedForecast.numericalHealthy = forecast.stability.numericalHealthy;
    sharedForecast.systemOrbitalHealthy = forecast.stability.systemOrbitalHealthy;
    sharedForecast.auxiliaryOrbitalHealthy = forecast.stability.auxiliaryOrbitalHealthy;
    sharedForecast.energyDriftAvailable = forecast.stability.conservation.energyDriftAvailable;
    sharedForecast.angularMomentumDriftAvailable = forecast.stability.conservation.angularMomentumDriftAvailable;

    operatorEditorView.surfaces = { ui.IsVisible(), operatorEditorView.surfaces.secondaryVisible };

    const RunEditorPlacementState& sharedEditor = editorTools.Editor();
    operatorEditorView.scene.dirty = sharedEditor.history.IsDirty();
    operatorEditorView.tools = { sharedEditor.editorModeEnabled,
                                 sharedEditor.placementModeEnabled,
                                 sharedEditor.placeStaticObject,
                                 sceneController.CrossScenePauseLocked(),
                                 scene.isFixedStep,
                                 sharedEditor.autoTerrainAlign,
                                 static_cast<int>( sharedEditor.history.UndoDepth() ),
                                 static_cast<int>( sharedEditor.history.RedoDepth() ) };

    const SceneEntityStore& hierarchyEntities = sceneController.Scene().Entities();
    const int selectedHierarchyRow = PeekSelectedEditorModelIndex( sharedEditor, sceneController.Scene().BodyStore() );

    operatorEditorView.hierarchy.totalRowCount = static_cast<uint32_t>( hierarchyEntities.Count() );
    const uint32_t hierarchyRowCount = (std::min)( operatorEditorView.hierarchy.totalRowCount,
                                                   SkullbonezCore::UI::OPERATOR_EDITOR_HIERARCHY_ROW_CAPACITY );

    operatorEditorView.hierarchy.rowCount = hierarchyRowCount;
    operatorEditorView.hierarchy.truncated = operatorEditorView.hierarchy.totalRowCount > hierarchyRowCount;

    for ( uint32_t index = 0u; index < hierarchyRowCount; ++index )
    {
        const SceneEntityRecord& entity = hierarchyEntities.At( static_cast<int>( index ) );
        SkullbonezCore::UI::OperatorEditorHierarchyRow& row = operatorEditorView.hierarchy.rows[index];
        row.displayName = entity.displayName;
        row.sceneObjectId = entity.sceneObjectId.value;
        row.groupRootObjectId = entity.behaviorGroup.rootObjectId.value;
        row.groupPartIndex = entity.behaviorGroup.partIndex;
        row.assetBacked = entity.asset.isAssetBacked;
        row.visible = entity.editorVisible;
        row.locked = entity.editorLocked;
        row.selected = static_cast<int>( index ) == selectedHierarchyRow;

        if ( row.selected )
        {
            operatorEditorView.hierarchy.selectedSceneObjectId = row.sceneObjectId;
        }
    }

    operatorEditorView.assets = { sharedEditor.objectType, SkullbonezCore::UI::EditorTab::OBJECT_TYPE_COUNT,
                                  m_assets.FindAssetLibrarySourceAsset( "assetlib.buildings" ) != nullptr };

#if defined( SKULLBONEZ_DEVELOPMENT_TOOLS )
    // Why: the GameUI surface does not consume secondary-editor contextual detail. Sampling
    // cold body/collider/buoyancy/material rows only while the secondary editor is
    // visible keeps ordinary Profile and shipping frames on their prior path.
    if ( operatorEditorView.surfaces.secondaryVisible )
    {
        SkullbonezCore::UI::OperatorEditorInspectorView& inspector = operatorEditorView.inspector;

        if ( sharedEditor.selectedBody.IsValid() && selectedHierarchyRow < 0 )
        {
            // Hazard: a scene transition can invalidate the body handle before the
            // presentation frame observes the cleared editor selection. Report the
            // stale state; never repair identity from a dense-row guess in the UI.
            inspector.selectionState = SkullbonezCore::UI::OperatorEditorInspectorSelectionState::Stale;
        }
        else if ( selectedHierarchyRow >= 0 )
        {
            const SceneEntityRecord* entity = hierarchyEntities.TryGet( selectedHierarchyRow );
            const PhysicsBodyStore& bodyStore = sceneController.Scene().BodyStore();
            const ColliderStore& colliderStore = sceneController.Scene().Colliders();
            const std::span<const BuoyancyBodyFacts> buoyancyFacts = PhysicsEngine::ReadBuoyancyFacts(
                sceneController.Scene().Physics() );

            const PhysicsBodyRecord* body = entity ? bodyStore.RecordForHandle( entity->body ) : nullptr;
            const PhysicsColliderHandle colliderHandle = entity ? colliderStore.HandleForBodyHandle( entity->body )
                                                                : PhysicsColliderHandle {};

            const ColliderRecord* collider = colliderStore.RecordForHandle( colliderHandle );
            const ColliderAuthoringRecord* colliderAuthoring = colliderStore.AuthoringRecordForHandle( colliderHandle );

            if ( !entity || !body || !collider || !colliderAuthoring ||
                 selectedHierarchyRow >= static_cast<int>( buoyancyFacts.size() ) )
            {
                inspector.selectionState = SkullbonezCore::UI::OperatorEditorInspectorSelectionState::Stale;
            }
            else
            {
                const PhysicsBodyHotFieldsConstView hot = bodyStore.HotFields();
                const std::size_t row = static_cast<std::size_t>( selectedHierarchyRow );
                const Vector3 position = PhysicsBodyPosition( hot, row );
                const Quaternion orientation = PhysicsBodyOrientation( hot, row );
                const Vector3 linearVelocity = PhysicsBodyLinearVelocity( hot, row );
                const Vector3 angularVelocity = PhysicsBodyAngularVelocity( hot, row );
                inspector.displayName = entity->displayName;
                inspector.renderMaterialName = entity->renderMaterial.name[0] != '\0'
                                                   ? entity->renderMaterial.name
                                                   : SkullbonezCore::Rendering::RenderMaterialKindName(
                                                         entity->renderMaterial.kind );

                inspector.contactMaterialName = colliderAuthoring->contactMaterialName;
                inspector.assetName = entity->asset.assetName;
                inspector.assetInstanceName = entity->asset.instanceName;
                inspector.assetPartName = entity->asset.partName;
                inspector.selectionState = SkullbonezCore::UI::OperatorEditorInspectorSelectionState::Single;
                inspector.sceneObjectId = entity->sceneObjectId.value;
                inspector.selectionCount = 1u;
                inspector.renderMaterialKind = static_cast<int>( entity->renderMaterial.kind );
                inspector.colliderShapeKind = static_cast<int>( collider->shapeKind );
                inspector.behaviorGroupKind = static_cast<int>( entity->behaviorGroup.kind );
                inspector.behaviorPartIndex = entity->behaviorGroup.partIndex;
                inspector.position[0] = position.x;
                inspector.position[1] = position.y;
                inspector.position[2] = position.z;
                orientation.GetComponents( inspector.orientation[0], inspector.orientation[1], inspector.orientation[2],
                                           inspector.orientation[3] );

                inspector.linearVelocity[0] = linearVelocity.x;
                inspector.linearVelocity[1] = linearVelocity.y;
                inspector.linearVelocity[2] = linearVelocity.z;
                inspector.angularVelocity[0] = angularVelocity.x;
                inspector.angularVelocity[1] = angularVelocity.y;
                inspector.angularVelocity[2] = angularVelocity.z;

                for ( int channel = 0; channel < 4; ++channel )
                {
                    inspector.baseColor[channel] = entity->renderMaterial.baseColor[channel];
                }

                inspector.mass = body->mass;
                inspector.volume = buoyancyFacts[row].volume;
                inspector.boundingRadius = collider->boundingRadius;
                inspector.dragCoefficient = collider->dragCoefficient;
                inspector.friction = collider->friction;
                inspector.restitution = collider->restitution;
                inspector.roughness = entity->renderMaterial.roughness;
                inspector.metallic = entity->renderMaterial.metallic;
                inspector.specular = entity->renderMaterial.specular;
                inspector.visible = entity->editorVisible;
                inspector.locked = entity->editorLocked;
                inspector.fixed = hot.fixed[row] != 0u;
                inspector.sleeping = hot.awake[row] == 0u;
                inspector.assetBacked = entity->asset.isAssetBacked;
            }
        }

        const Gameplay::TornadoFieldConfig& tornado = sceneController.Scene().Tornado().GetFieldConfig();
        operatorEditorView.world = { scene.modelCount,
                                     config.runtimeCapacity.sceneObjectCapacity,
                                     scene.solverBallCount,
                                     scene.solverBoxCount,
                                     static_cast<int>( scene.rngSeed ),
                                     scene.timeScale,
                                     sceneController.Scene().Environment().GetGravity(),
                                     sceneController.Scene().Environment().GetFluidSurfaceHeight(),
                                     sceneController.Scene().Environment().GetFluidDensity(),
                                     config.physicsMaterial.frictionCoeff,
                                     config.physicsMaterial.objectFrictionCoeff,
                                     config.physicsMaterial.rollingFrictionCoeff,
                                     tornado.radius,
                                     tornado.height,
                                     tornado.inwardAcceleration,
                                     tornado.swirlAcceleration,
                                     tornado.liftAcceleration,
                                     scene.isFixedStep,
                                     sceneController.Scene().Physics().IsSleepEnabled(),
                                     tornado.enabled };
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
        runtimeViewModel = BuildRuntimeViewModel( sceneController.State(), sceneController.Scene(),
                                                  sceneController.QueueSize(), m_capture.Screenshot(),
                                                  config.runtimeRender.presentationInterpolation,
                                                  uiTextFacts.presentationPinned, uiTextFacts.presentationAlpha );

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
        runtimeViewModel = BuildRuntimeViewModel( sceneController.State(), sceneController.Scene(),
                                                  sceneController.QueueSize(), m_capture.Screenshot(),
                                                  config.runtimeRender.presentationInterpolation,
                                                  uiTextFacts.presentationPinned, uiTextFacts.presentationAlpha );

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
                                                                    static_cast<float>( presentationFacts.secondsPerFrame ),
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
    if ( operatorUiPhase.Commands().surface == OperatorUiSurfaceCommand::ShowGameUi )
    {
        SelectDevelopmentUiSurface( DevelopmentUiMode::GameUI );
    }

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
