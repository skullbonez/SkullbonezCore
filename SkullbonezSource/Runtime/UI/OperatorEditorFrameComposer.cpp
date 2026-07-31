/*
File: SkullbonezSource/Runtime/UI/OperatorEditorFrameComposer.cpp
Purpose:
  Builds and renders the shared operator-editor presentation for one frame.

Summary:
  The composer synchronously samples scene, physics, replay, rendering, tools,
  diagnostics, and input owners into one bounded OperatorEditorFrameView before
  either development UI surface consumes it.

Mental model:
  Run::RenderOperatorUiPhase is the owner-approved top-level phase coordinator.
  It reaches process-owned members for one ordered UI phase, builds one shared
  value projection, submits Legacy and ImGui presentation, and retains no frame
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
  - RuntimeFrameViews.h retains the value-only late-UI facts.
  - Agentic/Reference/comment-style-guide.md
  - Agentic/Reference/engine-glossary.md
*/
#include "../App/Run.h"
#include "../Diagnostics/RuntimeOverlayDiagnostics.h"
#include "../Automation/RuntimeValidationHarness.h"
#include "../RuntimeFrameViews.h"
#include "RuntimeViewModel.h"
#include "../App/Window.h"
#include "../../Core/WorkerPool.h"
#include "../Planning/ReplayOverlayPackets.h"
#include "../Capture/CaptureSystem.h"
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
                                 const FramePresentationFacts& presentationFacts )
{
#if defined( SKULLBONEZ_DEVELOPMENT_TOOLS )

    // Invariant: copy the completed world backbuffer before either operator
    // surface draws, preserving one presentation owner at a time.

    if ( m_imguiEditor.IsVisible() )
    {
        const SkullbonezCore::Core::SbResult viewportCapture = m_imguiEditor.CaptureGameViewport();

        if ( !viewportCapture.Ok() )
        {
            m_timers.frameTimer.StopTimer();
            PROFILE_FRAME_END( m_profiler );
            m_applicationExit.RequestPhaseFailure( viewportCapture );
            return;
        }
    }
#endif
    SkullbonezCore::UI::OperatorEditorFrameView operatorEditorView;
#if defined( SKULLBONEZ_DEVELOPMENT_TOOLS )
    operatorEditorView.surfaces.secondaryVisible = m_imguiEditor.IsVisible();
#endif
    const RuntimeUiTextFrameFacts uiTextFacts { RuntimeCameraModeEnabledMask( m_sceneController.State().isSceneMode,
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
                                                presentationFacts.legacyDevelopmentUiActive };

    const ReplayOverlay::ReplayOverlayStateView
        replayOverlay = m_replayRuntime.BuildOverlayStateView( m_runtimeTools.Editor().editorModeEnabled,
                                                               m_operatorUi->IsVisible(), m_operatorUi->IsMinimized(),
                                                               m_interaction.Gesture().kind,
                                                               renderModels.presentationRecords, renderModels.bodyStore );

    DiagnosticsRuntime& diagnosticsRuntime = m_diagnosticsRuntime;
    RunTimerState& timers = m_timers;
    RuntimeOverlayPresentationEdit presentationEdit = m_overlayDiagnostics->EditPresentation();
    OverlayDebugState& debug = presentationEdit.State();
    SceneController& sceneController = m_sceneController;
    SceneSessionState& scene = sceneController.State();
    SkullbonezCore::Core::EngineConfig& config = m_config;
    RuntimeTools& runtimeTools = m_runtimeTools;
    SkullbonezCore::UI::InGameUI& ui = *m_operatorUi;
    RuntimeInputContext& runtimeInput = m_inputRouter.RuntimeContext();
    CameraControlState& camera = m_camera;
    SkullbonezCore::Threading::WorkerPool& workerPool = m_workerPool;
    Window& window = m_window;
    RunLaunchOptions& launchOptions = m_launchOptions;
    RuntimeRenderer& renderer = Renderer();
    ReplayRuntime& replayRuntime = m_replayRuntime;

    // Lifetime: value-only facts exist only for this late UI call; no render or
    // UI owner retains a coordinator borrow.
    const SkullbonezCore::UI::RunSceneBrowserState& uiSceneBrowser = ui.SceneNavigation().browser;
    const std::string* uiScenePath = sceneController.CurrentPath();
    const ReplayHudStatus sharedReplayHud = replayRuntime.BuildHudStatus( false );
    const SkullbonezCore::Core::CinematicRenderConfig& sharedCinematic = ActiveSceneCinematicConfig( scene, config );
    const bool sharedCinematicRendering = IsSceneCinematicRenderingEnabled( scene, config, launchOptions, debug, true );
    const bool sharedShadows = sharedCinematicRendering ? sharedCinematic.shadow.enabled
                                                        : config.ordinaryRender.shadow.enabled;

    // Invariant: build this common value once. The legacy draw pass and the
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

    operatorEditorView.surfaces = { ui.IsVisible(), operatorEditorView.surfaces.secondaryVisible };

    const RunEditorPlacementState& sharedEditor = runtimeTools.Editor();
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

    // Why: the legacy surface does not consume E12 contextual detail. Sampling
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
            const std::span<const BuoyancyBodyFacts> buoyancyFacts = PhysicsEngine::ReadBuoyancyFacts( sceneController.Scene().Physics() );

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
                                                   : SkullbonezCore::Rendering::RenderMaterialKindName( entity->renderMaterial.kind );

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

        // Why: the secondary surface can be visible while the legacy UI is
        // hidden. Sample its bounded authoring/diagnostic values here instead
        // of making ImGui depend on whether the legacy text pass happens to run.
        runtimeViewModel = RuntimeViewModelBuilder::Build( sceneController.State(), sceneController.Scene(),
                                                           sceneController.QueueSize(), diagnosticsRuntime.Capture(),
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
        diagnostics.uiDrawCalls = timers.lastUIDrawCalls;
        diagnostics.workerThreadCount = workerPool.GetThreadCount();
        diagnostics.maxWorkerThreadCount = SkullbonezCore::Threading::WorkerPool::MaxThreadCount();
        diagnostics.fps = uiTextFacts.secondsPerFrame > 0.0 ? static_cast<float>( 1.0 / uiTextFacts.secondsPerFrame ) : 0.0f;
        diagnostics.renderMs = ( timers.rollingRenderTime > 0.0f ? timers.rollingRenderTime : timers.renderTime ) * 1000.0f;

        diagnostics.physicsMs = ( timers.rollingPhysicsTime > 0.0f ? timers.rollingPhysicsTime : timers.physicsTime ) *
                                1000.0f;

        diagnostics.cpuFrameMs = timers.cpuFrameWorkMs;
        diagnostics.gpuFrameMs = timers.gpuFrameWorkMs;
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

    renderer.PrepareUiFrameTarget();

    if ( renderer.ResourceLifecycle().ShouldRenderUiText( debug, scene, sceneController.CrossScenePauseLocked(), camera, ui,
                                                          replayOverlay.shouldRenderScrubber,
                                                          replayPathVisualizerHasTarget ) )
    {
        runtimeViewModel = RuntimeViewModelBuilder::Build( sceneController.State(), sceneController.Scene(),
                                                           sceneController.QueueSize(), diagnosticsRuntime.Capture(),
                                                           config.runtimeRender.presentationInterpolation,
                                                           uiTextFacts.presentationPinned, uiTextFacts.presentationAlpha );

        const SkullbonezCore::Core::CinematicRenderConfig& uiCinematic = ActiveSceneCinematicConfig( scene, config );
        const bool uiCinematicRendering = IsSceneCinematicRenderingEnabled( scene, config, launchOptions, debug, true );
        const bool shadowsAvailable = uiCinematicRendering ? uiCinematic.shadow.enabled
                                                           : config.ordinaryRender.shadow.enabled;

        renderTargetPreviews = renderer.ResourceLifecycle()
                                   .BuildRenderTargetPreviewSnapshot( shadowsAvailable, uiCinematicRendering,
                                                                      uiCinematicRendering &&
                                                                          uiCinematic.volumetricLightingEnabled );

        const bool replayMemoryStatsRequested = ui.IsVisible() && !ui.IsMinimized() &&
                                                ui.GetActiveTab() == SkullbonezCore::UI::InGameUITab::Memory;

        const ReplayHudStatus replayHud = replayRuntime.BuildHudStatus( replayMemoryStatsRequested );
        UiChromeGraphInvocation uiChrome;
        uiChrome.debug = &debug;
        uiChrome.crossScenePauseLocked = sceneController.CrossScenePauseLocked();
        uiChrome.scene = &scene;
        uiChrome.camera = &camera;
        uiChrome.sceneQueueSize = sceneController.QueueSize();
        uiChrome.cameraModeLabel = uiTextFacts.cameraModeLabel;
        uiChrome.launcherFireModeLabel = uiTextFacts.launcherFireModeLabel;
        uiChrome.launcherCameraMode = uiTextFacts.isLauncherCameraMode;
        uiChrome.replayHud = &replayHud;
        uiChrome.viewport = { window.ClientWidth(), window.ClientHeight() };

        UiOperatorDiagnosticsGraphInvocation uiOperatorDiagnostics;
        uiOperatorDiagnostics.replayHud = &replayHud;
        uiOperatorDiagnostics.diagnosticsRuntime = &diagnosticsRuntime;
        uiOperatorDiagnostics.ui = &ui;
        uiOperatorDiagnostics.workerPool = &workerPool;

        UiOperatorSettingsGraphInvocation uiOperatorSettings;
        uiOperatorSettings.debug = &debug;
        uiOperatorSettings.renderPresentation = &renderer.PresentationSettings();
        uiOperatorSettings.world = &sceneController.Scene();
        uiOperatorSettings.config = &config;
        uiOperatorSettings.cinematic = &uiCinematic;
        uiOperatorSettings.cinematicRendering = uiCinematicRendering;

        UiOperatorInteractionGraphInvocation uiOperatorInteraction;
        uiOperatorInteraction.rayCastTest = &runtimeTools.RayCastTest();
        uiOperatorInteraction.editor = &runtimeTools.Editor();
        uiOperatorInteraction.runtimeInput = &runtimeInput;
        uiOperatorInteraction.camera = &camera;
        uiOperatorInteraction.ui = &ui;
        uiOperatorInteraction.cameraModeEnabledMask = uiTextFacts.cameraModeEnabledMask;
        uiOperatorInteraction.cameraModeLabel = uiTextFacts.cameraModeLabel;

        UiOperatorPresentationGraphInvocation uiOperatorPresentation;
        uiOperatorPresentation.scene = &scene;
        uiOperatorPresentation.runtimeViewModel = &runtimeViewModel;
        uiOperatorPresentation.sceneBrowser = &uiSceneBrowser;
        uiOperatorPresentation.operatorEditorView = &operatorEditorView;
        uiOperatorPresentation.sceneHasCurrentEntry = sceneController.HasCurrentEntry();
        uiOperatorPresentation.currentScenePath = uiScenePath ? uiScenePath->c_str() : nullptr;
        uiOperatorPresentation.currentSceneBrowserIndex = uiSceneBrowser.CurrentIndexForPath( sceneController.CurrentPath() );

        UiOperatorSubmissionGraphInvocation uiOperatorSubmission;
        uiOperatorSubmission.ui = &ui;
        uiOperatorSubmission.renderTargetPreviews = &renderTargetPreviews;
        uiOperatorSubmission.assets = &m_assets;

        UiReplayGraphInvocation uiReplay;
        uiReplay.overlay = &replayOverlay;
        uiReplay.profiler = m_profiler;
        uiReplay.legacySurfaceActive = uiTextFacts.legacyDevelopmentUiActive;
        uiReplay.scenePhysicsEnabled = scene.isScenePhysics;
        uiReplay.gesture = uiTextFacts.interactionGestureKind;
        uiReplay.viewport = { window.ClientWidth(), window.ClientHeight() };
        uiReplay.nowSeconds = timers.simulationTimer.GetTotalTime();

        PROFILE_BEGIN( m_profiler, "Frame/UI" );
        {
            CoreAllocation::RuntimeAllocationScope allocationScope( CoreAllocation::RuntimeAllocationPhase::Render );

            // Lifetime: caller-owned ABI records and every direct borrow remain
            // valid until the synchronous UI-text graph completes below.
            timers.lastUIDrawCalls = renderer.RenderUiText( timers, renderModels, uiTextFacts.secondsPerFrame, uiChrome,
                                                            uiOperatorDiagnostics, uiOperatorSettings, uiOperatorInteraction,
                                                            uiOperatorPresentation, uiOperatorSubmission, uiReplay );
        }
        PROFILE_END( m_profiler, "Frame/UI" );
    }
    else
    {
        timers.lastUIDrawCalls = 0;
    }

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
            imguiResult.status = Renderer().RenderDevelopmentUi( m_imguiEditor );
        }

        if ( !imguiResult.status.Ok() )
        {
            m_timers.frameTimer.StopTimer();
            PROFILE_FRAME_END( m_profiler );
            m_applicationExit.RequestPhaseFailure( imguiResult.status );
            return;
        }

        if ( imguiResult.commands.requestSurfaceSwap )
        {
            SelectDevelopmentUiSurface( DevelopmentUiMode::Legacy );
        }

        if ( imguiResult.commands.requestTracyStandardCapture )
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
    }
#endif
}


} // namespace Runtime
} // namespace SkullbonezCore
