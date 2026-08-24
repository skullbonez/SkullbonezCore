/*
File: SkullbonezSource/Runtime/App/OperatorUiProjection.cpp
Purpose:
  Projects domain-owner facts into one detached GameUI frame.

Summary:
  App performs every Scene, Diagnostics, Input, Camera, and tool read before
  handing the completed value to Render. The renderer receives no route back to
  those owners and performs only GPU submission.

Invariants:
  - Projection is CPU-only and completes before UI submission begins.
  - Reserve-capacity rows borrow caller storage through the synchronous draw.
  - Profile data comes from the startup-bound profiler selected by App.

Related:
  - SkullbonezSource/Runtime/App/OperatorUiProjection.h
  - SkullbonezSource/Runtime/App/OperatorEditorFramePhase.cpp
  - SkullbonezSource/Runtime/Render/UiTextPass.cpp
*/
#include "OperatorUiProjection.h"

#include "../Camera/CameraControlState.h"
#include "../Capture/CaptureSystem.h"
#include "../Diagnostics/DiagnosticsPhysicsUI.h"
#include "../Diagnostics/DiagnosticsRuntime.h"
#include "../Diagnostics/OverlayDebugState.h"
#include "../Editor/EditorTools.h"
#include "../Input/InputController.h"
#include "../Planning/ContinuousOrbitalForecast.h"
#include "../Render/RenderPresentationSettings.h"
#include "../Render/RuntimeRenderFrameValues.h"
#include "../RuntimeFrameViews.h"
#include "../Replay/ReplayRuntimePackets.h"
#include "../Scene/SceneControllerState.h"
#include "../Scene/SceneEntityStore.h"
#include "../Scene/SceneSessionState.h"
#include "../Scene/SceneWorld.h"
#include "../UI/RenderDiagnosticsProjection.h"
#include "../UI/RuntimeViewModel.h"
#include "../Tools/RuntimeTools.h"
#include "../../Core/Allocation/RuntimeReserveAllocator.h"
#include "../../Core/FatalError.h"
#include "../../Core/Profiler.h"
#include "../../Core/TracyClientOwner.h"
#include "../../Core/WorkerPool.h"
#include "../../Physics/ColliderStore.h"
#include "../../Physics/PhysicsApi.h"
#include "../../Physics/PhysicsEngine.h"
#include "../../Physics/PhysicsDebugData.h"
#include "../../Rendering/DX12/Dx12Diagnostics.h"
#include "../UI/GameUI/UI.h"
#include "../UI/GameUI/UIFrameComposition.h"

#include <algorithm>

namespace SkullbonezCore
{
namespace Runtime
{
namespace
{
SkullbonezCore::Core::MainMemoryStats
BuildMainMemoryOverlayStats( const DiagnosticsRuntime& diagnosticsRuntime,
                             const SkullbonezCore::Core::MainMemoryGameObjectStats& gameObjects )
{
    SkullbonezCore::Core::MainMemoryStats stats = diagnosticsRuntime.MainMemoryStatsSnapshot();
    stats.process = SkullbonezCore::Core::MainMemoryProcessStats {};
    stats.gameObjects = gameObjects;
    stats.trackedEngineBytes = stats.replay.totalBytes + stats.gameObjects.totalBytes + stats.otherTrackedBytes;
    stats.unattributedProcessBytes = 0;
    stats.trackedOvershootBytes = 0;
    stats.reconciledTotalBytes = stats.trackedEngineBytes;
    stats.reconciliationDeltaBytes = 0;
    return stats;
}

SkullbonezCore::UI::OperatorEditorForecastCause MapForecastCause( ContinuousOrbitalInstabilityCause cause ) noexcept
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

void FillOperatorRenderingParameters( SkullbonezCore::UI::OperatorEditorRenderingView& view,
                                      const SkullbonezCore::Core::OrdinaryRenderConfig& ordinary,
                                      const SkullbonezCore::Core::CinematicRenderConfig& cinematic )
{
    using SkullbonezCore::UI::UICinematicFeature;
    using SkullbonezCore::UI::UICinematicParam;
    using SkullbonezCore::UI::UIRenderParam;

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
} // namespace

RuntimeViewModel BuildOperatorRuntimeViewModel( const SceneSessionState& scene, const SceneWorld& world, int sceneCount,
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
    view.modelCount = SkullbonezCore::Physics::PhysicsEngine::ReadBodies( world.Physics() ).Count();
    view.timeScale = scene.timeScale;
    view.presentationInterpolation = presentationInterpolation;
    view.presentationPinned = presentationPinned;
    view.presentationAlpha = std::clamp( presentationAlpha, 0.0f, 1.0f );
    return view;
}

void ProjectOperatorEditorScene( UI::OperatorEditorFrameView& view, const char* currentScenePath,
                                 const UI::RunSceneBrowserState& sceneBrowser, int currentSceneBrowserIndex,
                                 const SceneSessionState& scene, const SceneWorld& world )
{
    view.scene = { currentScenePath ? currentScenePath : "",
                   sceneBrowser.namePtrs.empty() ? nullptr : sceneBrowser.namePtrs.data(),
                   currentSceneBrowserIndex,
                   static_cast<int>( sceneBrowser.namePtrs.size() ),
                   scene.currentFrame,
                   world.SceneEntityCount(),
                   scene.timeScale,
                   currentScenePath && currentScenePath[0] != '\0',
                   false };
    view.property = { world.Environment().GetGravity(), world.Environment().GetFluidSurfaceHeight(),
                      world.Environment().GetFluidDensity() };
}

void ProjectOperatorEditorRendering( UI::OperatorEditorFrameView& view, const RenderPresentationSettings& presentation,
                                     const Core::EngineConfig& config, const Core::CinematicRenderConfig& cinematic,
                                     const OverlayDebugState& debug, const RuntimeUiTextFrameFacts& uiTextFacts,
                                     bool cinematicRendering, bool shadowsEnabled )
{
    UI::OperatorEditorRenderingView& rendering = view.rendering;
    rendering.vsyncEnabled = presentation.vsyncEnabled;
    rendering.shadowsEnabled = shadowsEnabled;
    rendering.cinematicRendering = cinematicRendering;
    rendering.presentationInterpolation = config.runtimeRender.presentationInterpolation;
    rendering.presentationAlpha = uiTextFacts.presentationAlpha;
    rendering.terrainHidden = debug.isTerrainHidden;
    rendering.waterHidden = debug.isWaterHidden;
    rendering.waterFrozen = debug.isWaterFreezeDebug;
    rendering.waterFlat = debug.isWaterFlatDebug;
    rendering.waterReflectionMode = debug.isWaterNoReflect ? 2 : ( debug.isWaterRTReflect ? 1 : 0 );
    FillOperatorRenderingParameters( rendering, config.ordinaryRender, cinematic );

    const char* gizmoMode = "translate";

    if ( uiTextFacts.interactionGestureKind == RuntimeInteractionGestureKind::GizmoDrag )
    {
        if ( uiTextFacts.interactionGizmoKind == RuntimeGizmoDragKind::Rotate )
        {
            gizmoMode = "rotate";
        }
        else if ( uiTextFacts.interactionGizmoKind == RuntimeGizmoDragKind::Scale )
        {
            gizmoMode = "scale";
        }
    }

    view.viewport = { uiTextFacts.cameraModeLabel, gizmoMode, uiTextFacts.presentationPinned };
}

void ProjectOperatorEditorForecast( UI::OperatorEditorFrameView& view, const ContinuousOrbitalForecastView& forecast )
{
    const bool blockingFailureFirst = forecast.stability.firstBlockingFailure.latched &&
                                      ( !forecast.stability.firstAuxiliaryFailure.latched ||
                                        forecast.stability.firstBlockingFailure.absoluteTick <=
                                            forecast.stability.firstAuxiliaryFailure.absoluteTick );
    const ContinuousOrbitalFailure& firstFailure = blockingFailureFirst ? forecast.stability.firstBlockingFailure
                                                                        : forecast.stability.firstAuxiliaryFailure;
    UI::OperatorEditorForecastView& target = view.forecast;
    target.simulatedSeconds = forecast.simulatedSeconds;
    target.simulatedSecondsPerRealSecond = forecast.simulatedSecondsPerRealSecond;
    target.rollingWindowAgeSeconds = forecast.rollingWindowAgeSeconds;
    target.energyDrift = forecast.stability.conservation.energyDrift;
    target.angularMomentumDrift = forecast.stability.conservation.angularMomentumDrift;
    target.maximumAbsoluteEnergyDrift = forecast.stability.conservation.maximumAbsoluteEnergyDrift;
    target.maximumAngularMomentumDrift = forecast.stability.conservation.maximumAngularMomentumDrift;
    target.firstFailureSeconds = firstFailure.simulatedSeconds;
    target.newestAbsoluteTick = forecast.newestAbsoluteTick;
    target.retainedBytes = static_cast<uint64_t>( forecast.retainedBytes );
    target.firstFailureSubject = firstFailure.subject.value;
    target.firstFailureOther = firstFailure.other.value;
    target.firstFailureCause = MapForecastCause( firstFailure.cause );
    target.available = forecast.available;
    target.active = forecast.active;
    target.workerInFlight = forecast.workerInFlight;
    target.failed = forecast.failed;
    target.configured = forecast.stability.configured;
    target.numericalHealthy = forecast.stability.numericalHealthy;
    target.systemOrbitalHealthy = forecast.stability.systemOrbitalHealthy;
    target.auxiliaryOrbitalHealthy = forecast.stability.auxiliaryOrbitalHealthy;
    target.energyDriftAvailable = forecast.stability.conservation.energyDriftAvailable;
    target.angularMomentumDriftAvailable = forecast.stability.conservation.angularMomentumDriftAvailable;
}

int ProjectOperatorEditorHierarchy( UI::OperatorEditorFrameView& view, const RunEditorPlacementState& editor,
                                    const SceneWorld& world, bool crossScenePauseLocked, bool fixedStep,
                                    bool buildingAssetsAvailable )
{
    view.scene.dirty = editor.history.IsDirty();
    view.tools = { editor.editorModeEnabled,
                   editor.placementModeEnabled,
                   editor.placeStaticObject,
                   crossScenePauseLocked,
                   fixedStep,
                   editor.autoTerrainAlign,
                   static_cast<int>( editor.history.UndoDepth() ),
                   static_cast<int>( editor.history.RedoDepth() ) };

    const SceneEntityStore& entities = world.Entities();
    const int selectedRow = PeekSelectedEditorModelIndex( editor, world.BodyStore() );
    view.hierarchy.totalRowCount = static_cast<uint32_t>( entities.Count() );
    view.hierarchy.rowCount = (std::min)( view.hierarchy.totalRowCount, UI::OPERATOR_EDITOR_HIERARCHY_ROW_CAPACITY );
    view.hierarchy.truncated = view.hierarchy.totalRowCount > view.hierarchy.rowCount;

    for ( uint32_t index = 0u; index < view.hierarchy.rowCount; ++index )
    {
        const SceneEntityRecord& entity = entities.At( static_cast<int>( index ) );
        UI::OperatorEditorHierarchyRow& row = view.hierarchy.rows[index];
        row.displayName = entity.displayName;
        row.sceneObjectId = entity.sceneObjectId.value;
        row.groupRootObjectId = entity.behaviorGroup.rootObjectId.value;
        row.groupPartIndex = entity.behaviorGroup.partIndex;
        row.assetBacked = entity.asset.isAssetBacked;
        row.visible = entity.editorVisible;
        row.locked = entity.editorLocked;
        row.selected = static_cast<int>( index ) == selectedRow;

        if ( row.selected )
        {
            view.hierarchy.selectedSceneObjectId = row.sceneObjectId;
        }
    }

    view.assets = { editor.objectType, UI::EditorTab::OBJECT_TYPE_COUNT, buildingAssetsAvailable };
    return selectedRow;
}

#if defined( SKULLBONEZ_DEVELOPMENT_TOOLS )
void ProjectOperatorEditorInspectorAndWorld( UI::OperatorEditorFrameView& view, const RunEditorPlacementState& editor,
                                             const SceneWorld& world, int selectedHierarchyRow,
                                             const SceneSessionState& scene, const Core::EngineConfig& config )
{
    UI::OperatorEditorInspectorView& inspector = view.inspector;

    if ( editor.selectedBody.IsValid() && selectedHierarchyRow < 0 )
    {
        inspector.selectionState = UI::OperatorEditorInspectorSelectionState::Stale;
    }
    else if ( selectedHierarchyRow >= 0 )
    {
        const SceneEntityRecord* entity = world.Entities().TryGet( selectedHierarchyRow );
        const Physics::PhysicsBodyStore& bodyStore = world.BodyStore();
        const Physics::ColliderStore& colliderStore = world.Colliders();
        const std::span<const Physics::BuoyancyBodyFacts> buoyancyFacts = Physics::PhysicsEngine::ReadBuoyancyFacts( world.Physics() );
        const Physics::PhysicsBodyRecord* body = entity ? bodyStore.RecordForHandle( entity->body ) : nullptr;
        const Physics::PhysicsColliderHandle colliderHandle = entity ? colliderStore.HandleForBodyHandle( entity->body )
                                                                     : Physics::PhysicsColliderHandle {};
        const Physics::ColliderRecord* collider = colliderStore.RecordForHandle( colliderHandle );
        const Physics::ColliderAuthoringRecord* colliderAuthoring = colliderStore.AuthoringRecordForHandle( colliderHandle );

        if ( !entity || !body || !collider || !colliderAuthoring ||
             selectedHierarchyRow >= static_cast<int>( buoyancyFacts.size() ) )
        {
            inspector.selectionState = UI::OperatorEditorInspectorSelectionState::Stale;
        }
        else
        {
            const Physics::PhysicsBodyHotFieldsConstView hot = bodyStore.HotFields();
            const std::size_t row = static_cast<std::size_t>( selectedHierarchyRow );
            const auto position = Physics::PhysicsBodyPosition( hot, row );
            const auto orientation = Physics::PhysicsBodyOrientation( hot, row );
            const auto linearVelocity = Physics::PhysicsBodyLinearVelocity( hot, row );
            const auto angularVelocity = Physics::PhysicsBodyAngularVelocity( hot, row );
            inspector.displayName = entity->displayName;
            inspector.renderMaterialName = entity->renderMaterial.name[0] != '\0'
                                               ? entity->renderMaterial.name
                                               : Rendering::RenderMaterialKindName( entity->renderMaterial.kind );
            inspector.contactMaterialName = colliderAuthoring->contactMaterialName;
            inspector.assetName = entity->asset.assetName;
            inspector.assetInstanceName = entity->asset.instanceName;
            inspector.assetPartName = entity->asset.partName;
            inspector.selectionState = UI::OperatorEditorInspectorSelectionState::Single;
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

    const Gameplay::TornadoFieldConfig& tornado = world.Tornado().GetFieldConfig();
    view.world = { scene.modelCount,
                   config.runtimeCapacity.sceneObjectCapacity,
                   scene.solverBallCount,
                   scene.solverBoxCount,
                   static_cast<int>( scene.rngSeed ),
                   scene.timeScale,
                   world.Environment().GetGravity(),
                   world.Environment().GetFluidSurfaceHeight(),
                   world.Environment().GetFluidDensity(),
                   config.physicsMaterial.frictionCoeff,
                   config.physicsMaterial.objectFrictionCoeff,
                   config.physicsMaterial.rollingFrictionCoeff,
                   tornado.radius,
                   tornado.height,
                   tornado.inwardAcceleration,
                   tornado.swirlAcceleration,
                   tornado.liftAcceleration,
                   scene.isFixedStep,
                   world.Physics().IsSleepEnabled(),
                   tornado.enabled };
}
#endif

SkullbonezCore::Core::MainMemoryStats
ProjectMemoryTabStats( DiagnosticsRuntime& diagnosticsRuntime, const ReplayHudStatus& replayHud,
                       const SkullbonezCore::Core::MainMemoryGameObjectStats& gameObjects, double nowSeconds )
{
    // Recoverable error: the Memory tab may be opened before Replay has published its
    // first accounting snapshot. The inline policy does not invoke this sampler
    // until the source is valid, so stale diagnostics cannot masquerade as the
    // current scene.
    if ( !replayHud.memoryStatsValid )
    {
        return {};
    }

    return diagnosticsRuntime.RefreshMainMemoryStats( replayHud.memoryStats, gameObjects, nowSeconds, false, false );
}

void ProjectOperatorUiDiagnostics( UI::InGameUIFrameData& UIData, const ReplayHudStatus& replayHud,
                                   const RuntimeFrameMetricsSnapshot& metrics, const RuntimeRenderModelFrameView& models,
                                   DiagnosticsRuntime& diagnosticsRuntime, UI::InGameUI& ui,
                                   Threading::WorkerPool* workerPool, Core::Profiler* profilerOwner,
                                   UI::UIRuntimeReserveCapacityRow* reserveCapacityRows,
                                   Rendering::Dx12Diagnostics& renderDiagnostics )
{
#if defined( SKULLBONEZ_PROFILE_ENABLED )

    if ( !profilerOwner )
    {
        SB_FATAL( "Runtime/App/OperatorUiProjection", "Profile UI projection requires the startup-bound profiler." );
    }

    const SkullbonezCore::Core::Profiler& profiler = *profilerOwner;
#else
    (void)profilerOwner;
#endif
    UIData.UIDrawCalls = metrics.uiDrawCalls;
    UIData.visibility = ProjectRenderVisibilityDiagnostics( renderDiagnostics.GetFrameVisibilityStats() );
    UIData.fps = metrics.rollingFrameSeconds > 0.0f
                     ? 1.0f / metrics.rollingFrameSeconds
                     : ( metrics.secondsPerFrame > 0.0 ? 1.0f / static_cast<float>( metrics.secondsPerFrame ) : 0.0f );

    UIData.renderMs = ( metrics.rollingRenderSeconds > 0.0f ? metrics.rollingRenderSeconds : metrics.renderSeconds ) *
                      1000.0f;

    UIData.physicsMs = ( metrics.rollingPhysicsSeconds > 0.0f ? metrics.rollingPhysicsSeconds : metrics.physicsSeconds ) *
                       1000.0f;

    UIData.cpuFrameMs = metrics.cpuFrameWorkMs;
    UIData.gpuFrameMs = metrics.gpuFrameWorkMs;
    {
        // Concept: render draw attribution is copied through UIData while
        // the render diagnostics capability is already borrowed by Run. The
        // profiler tab never needs the wide renderer facade to explain draw
        // calls.
        const auto drawTrace = renderDiagnostics.GetFrameDrawCallTrace();
        const int sourceNodeCount = (std::max)( 0, drawTrace.nodeCount );
        const int nodeCount = (std::min)( sourceNodeCount, SkullbonezCore::UI::ProfilerTab::MAX_MARKERS );
        SkullbonezCore::UI::ProfilerTab::DrawTraceSnapshot& uiTrace = UIData.profiler.drawTrace;
        uiTrace.nodeCount = nodeCount;
        uiTrace.nodeOverflowCount = drawTrace.nodeOverflowCount + ( sourceNodeCount - nodeCount );
        uiTrace.eventCount = drawTrace.eventCount;
        uiTrace.eventOverflowCount = drawTrace.eventOverflowCount;
        uiTrace.scopeMismatchCount = drawTrace.scopeMismatchCount;

        if ( drawTrace.nodes )
        {
            for ( int nodeIndex = 0; nodeIndex < nodeCount; ++nodeIndex )
            {
                const auto& source = drawTrace.nodes[nodeIndex];
                SkullbonezCore::UI::ProfilerTab::DrawTraceNodeSnapshot& target = uiTrace.nodes[nodeIndex];
                target.name = source.name ? source.name : "";
                target.leafName = source.leafName ? source.leafName : target.name;
                target.hash = source.hash;
                target.parentIndex = source.parentIndex;
                target.depth = source.depth;
                target.drawCallCount = source.drawCallCount;
                target.vertexCount = source.vertexCount;
                target.instanceCount = source.instanceCount;
            }
        }
    }
#if defined( SKULLBONEZ_PROFILE_ENABLED )
    {
        static_assert( SkullbonezCore::UI::ProfilerTab::MAX_MARKERS == SkullbonezCore::Core::Profiler::MAX_MARKERS,
                       "UI profiler snapshot capacity must match SkullbonezCore::Core::Profiler markers" );

        static_assert( SkullbonezCore::UI::ProfilerTab::MAX_WORKER_CORE_SAMPLES ==
                           SkullbonezCore::Core::Profiler::MAX_WORKER_CORES,
                       "UI worker sample snapshot capacity must match SkullbonezCore::Core::Profiler samples" );

        SkullbonezCore::UI::ProfilerTab::FrameSnapshot& profilerFrame = UIData.profiler;
        profilerFrame.markerCount = (std::min)( profiler.MarkerCount(), SkullbonezCore::UI::ProfilerTab::MAX_MARKERS );

        for ( int markerIndex = 0; markerIndex < profilerFrame.markerCount; ++markerIndex )
        {
            const SkullbonezCore::Core::Profiler::Marker& source = profiler.GetMarker( markerIndex );
            const int paletteIndex = source.colorIndex >= 0
                                         ? source.colorIndex % SkullbonezCore::Core::Profiler::BAR_PALETTE_SIZE
                                         : 0;

            const SkullbonezCore::Core::Profiler::BarColor&
                color = SkullbonezCore::Core::Profiler::BAR_PALETTE[paletteIndex];

            SkullbonezCore::UI::ProfilerTab::MarkerSnapshot& target = profilerFrame.markers[markerIndex];
            target.name = source.name ? source.name : "";
            target.leafName = source.leafName ? source.leafName : target.name;
            target.hash = source.hash;
            target.parentIndex = source.parentIndex;
            target.depth = source.depth;
            target.lastFrameMs = source.lastFrameMs;
            target.lastSelfMs = source.lastSelfMs;
            target.avgMs = source.avgMs;
            target.selfAvgMs = source.selfAvgMs;
            target.lastFrameWorkerMs = source.lastFrameWorkerMs;
            target.workerAvgMs = source.workerAvgMs;
            target.p50Ms = source.p50Ms;
            target.p99Ms = source.p99Ms;
            target.colorR = color.r;
            target.colorG = color.g;
            target.colorB = color.b;
        }

        profilerFrame.workerCoreSampleCount = (std::min)( profiler.WorkerCoreSampleCount(),
                                                          SkullbonezCore::UI::ProfilerTab::MAX_WORKER_CORE_SAMPLES );

        for ( int sampleIndex = 0; sampleIndex < profilerFrame.workerCoreSampleCount; ++sampleIndex )
        {
            const SkullbonezCore::Core::Profiler::WorkerCoreSample& source = profiler.GetWorkerCoreSample( sampleIndex );

            SkullbonezCore::UI::ProfilerTab::WorkerCoreSampleSnapshot& target = profilerFrame.workerCoreSamples[sampleIndex];

            target.workerIndex = source.workerIndex;
            target.jobCount = source.jobCount;
            target.coreMs = source.coreMs;
            target.avgCoreMs = source.avgCoreMs;
            target.spanStartMs = source.spanStartMs;
            target.spanEndMs = source.spanEndMs;
            UIData.workerCoreTotalMs += (std::max)( 0.0f, target.coreMs );
        }
    }
#endif
#if defined( TRACY_ENABLE )
    {
        const SkullbonezCore::Core::DevelopmentTools::TracyClientStatus
            tracyStatus = SkullbonezCore::Core::DevelopmentTools::TracyClientOwner::CopyStatus();

        UIData.profiler.tracyBuildEnabled = tracyStatus.buildEnabled;
        UIData.profiler.tracyInitialized = tracyStatus.initialized;
        UIData.profiler.tracyViewerConnected = tracyStatus.viewerConnected;
    }
#endif
    {
        // Concept: marker enumeration stays in the runtime pass that owns
        // profiler access. The UI receives a bounded frame snapshot so
        // drawing and hit testing do not reach into profiler globals.
        auto markerOptionExists = [&]( uint32_t hash, bool isFrameTotal ) -> bool
        {
            for ( int i = 0; i < UIData.profilerMarkerOptionCount; ++i )
            {
                const SkullbonezCore::UI::UIProfilerMarkerOption& option = UIData.profilerMarkerOptions[i];

                if ( option.isFrameTotal == isFrameTotal && ( isFrameTotal || option.hash == hash ) )
                {
                    return true;
                }
            }

            return false;
        };

        // Why: callers label one complete profiler option; this bounded
        // append only normalizes nullable names and non-negative timings.
        auto addMarkerOption = [&]( const SkullbonezCore::UI::UIProfilerMarkerOption& input )
        {
            if ( UIData.profilerMarkerOptionCount >= SkullbonezCore::UI::UI_PROFILER_MARKER_OPTION_MAX ||
                 markerOptionExists( input.hash, input.isFrameTotal ) )
            {
                return;
            }

            SkullbonezCore::UI::UIProfilerMarkerOption&
                option = UIData.profilerMarkerOptions[UIData.profilerMarkerOptionCount++];

            option = input;
            option.name = input.name ? input.name : "";
            option.leafName = input.leafName ? input.leafName : option.name;
            option.cpuMs = (std::max)( 0.0f, input.cpuMs );
            option.cpuAverageMs = (std::max)( 0.0f, input.cpuAverageMs );
            option.workerMs = (std::max)( 0.0f, input.workerMs );
            option.workerAverageMs = (std::max)( 0.0f, input.workerAverageMs );
            option.gpuMs = (std::max)( 0.0f, input.gpuMs );
        };

        float frameAverageMs = UIData.cpuFrameMs;
#if defined( SKULLBONEZ_PROFILE_ENABLED )
        {
            static constexpr uint32_t kFrameHash = ::HashStr( "Frame" );

            for ( int markerIndex = 0; markerIndex < profiler.MarkerCount(); ++markerIndex )
            {
                const SkullbonezCore::Core::Profiler::Marker& marker = profiler.GetMarker( markerIndex );

                if ( marker.hash == kFrameHash )
                {
                    frameAverageMs = marker.avgMs > 0.0f ? marker.avgMs : marker.lastFrameMs;
                    break;
                }
            }
        }
#endif
        const SkullbonezCore::UI::Style::UIColor& mainColor = SkullbonezCore::UI::Style::Palette().accent;
        addMarkerOption( SkullbonezCore::UI::UIProfilerMarkerOption { .name = "Frame Total",
                                                                      .leafName = "Frame Total",
                                                                      .hash = SkullbonezCore::UI::UI_PROFILER_FRAME_TOTAL_HASH,
                                                                      .cpuMs = UIData.cpuFrameMs,
                                                                      .cpuAverageMs = frameAverageMs,
                                                                      .gpuMs = UIData.gpuFrameMs,
                                                                      .colorR = mainColor.r,
                                                                      .colorG = mainColor.g,
                                                                      .colorB = mainColor.b,
                                                                      .hasGpu = true,
                                                                      .sampleValid = true,
                                                                      .isFrameTotal = true } );

#if defined( SKULLBONEZ_PROFILE_ENABLED )
        auto addProfilerMarker = [&]( const SkullbonezCore::Core::Profiler::Marker& marker )
        {
            const SkullbonezCore::Core::Profiler::BarColor&
                color = SkullbonezCore::Core::Profiler::BAR_PALETTE[marker.colorIndex %
                                                                    SkullbonezCore::Core::Profiler::BAR_PALETTE_SIZE];

            addMarkerOption( SkullbonezCore::UI::UIProfilerMarkerOption { .name = marker.name,
                                                                          .leafName = marker.leafName,
                                                                          .hash = marker.hash,
                                                                          .cpuMs = marker.lastFrameMs,
                                                                          .cpuAverageMs = marker.avgMs > 0.0f ? marker.avgMs
                                                                                                              : marker.lastFrameMs,
                                                                          .workerMs = marker.lastFrameWorkerMs,
                                                                          .workerAverageMs = marker.workerAvgMs > 0.0f
                                                                                                 ? marker.workerAvgMs
                                                                                                 : marker.lastFrameWorkerMs,
                                                                          .gpuMs = marker.hasGpu ? marker.gpuLastFrameMs : 0.0f,
                                                                          .colorR = color.r,
                                                                          .colorG = color.g,
                                                                          .colorB = color.b,
                                                                          .hasGpu = marker.hasGpu,
                                                                          .sampleValid = true,
                                                                          .isFrameTotal = false } );
        };

        static constexpr uint32_t kPinnedMarkerHashes[] = { ::HashStr( "Frame/Physics" ), ::HashStr( "Frame/Physics/Step" ),
                                                            ::HashStr( "Frame/Physics/Narrowphase/PersistentContacts/"
                                                                       "SolveRows" ),
                                                            ::HashStr( "Frame/Render" ), ::HashStr( "Frame/UI" ) };

        for ( uint32_t pinnedHash : kPinnedMarkerHashes )
        {
            for ( int markerIndex = 0; markerIndex < profiler.MarkerCount(); ++markerIndex )
            {
                const SkullbonezCore::Core::Profiler::Marker& marker = profiler.GetMarker( markerIndex );

                if ( marker.hash == pinnedHash )
                {
                    addProfilerMarker( marker );
                    break;
                }
            }
        }

        for ( int markerIndex = 0; markerIndex < profiler.MarkerCount(); ++markerIndex )
        {
            addProfilerMarker( profiler.GetMarker( markerIndex ) );
        }
#endif
    }
    UIData.workerThreadCount = workerPool ? workerPool->GetThreadCount() : 0;
    UIData.maxWorkerThreadCount = SkullbonezCore::Threading::WorkerPool::MaxThreadCount();
    UIData.now = metrics.simulationTotalSeconds;
    UIData.replayMemoryPreset = replayHud.memoryPreset;
    UIData.replayMemoryRequestedRetentionSeconds = replayHud.requestedRetentionSeconds;
    UIData.replayMemoryRequestedBudgetMiB = replayHud.requestedBudgetMiB;
    UIData.replayMemoryPresentationRetentionSeconds = replayHud.presentationRetentionSeconds;
    UIData.replayMemorySolverRetentionSeconds = replayHud.solverRetentionSeconds;
    UIData.replayMemoryBudgetClamped = replayHud.memoryBudgetClamped;
    UIData.replayMemorySolverWindowReduced = replayHud.solverWindowReduced;
    UIData.predictionRevealRate = replayHud.predictionRevealRate;
    const bool memoryTabActive = ui.IsVisible() && !ui.IsMinimized() && ui.GetActiveTab() == UI::InGameUITab::Memory;
    const bool memoryOverlayEnabled = ui.IsMemoryOverlayEnabled();
    UIData.reserveCapacityRows = nullptr;
    UIData.reserveCapacityRowCount = 0;

    if ( memoryTabActive )
    {
        // Why: memory sampling belongs to DiagnosticsRuntime; the render host
        // only decides whether the UI pass needs to draw. An unavailable replay
        // snapshot publishes an unavailable memory value without sampling or
        // reusing a stale prior frame.
        UIData.mainMemory = ProjectMemoryTabStats( diagnosticsRuntime, replayHud, models.gameObjectMemory, UIData.now );
    }
    else if ( memoryOverlayEnabled )
    {
        // Why: F6 stays event/counter driven. Merely leaving the overlay up
        // must not start a process-memory or replay-memory sampling heartbeat.
        UIData.mainMemory = BuildMainMemoryOverlayStats( diagnosticsRuntime, models.gameObjectMemory );
    }

    if ( memoryTabActive || memoryOverlayEnabled )
    {
        // The render snapshot is cheap owner-maintained accounting; unlike
        // process memory sampling, it is safe to refresh for the F6 overlay.
        UIData.renderMemory = ProjectRenderMemoryDiagnostics( renderDiagnostics.GetRenderMemoryStats() );
        UIData.reserveGrowthEventTotalCount = SkullbonezCore::Core::Allocation::RuntimeReserveAllocator::GrowthEventCount();

        UIData.reserveGrowthEventDroppedCount = SkullbonezCore::Core::Allocation::RuntimeReserveAllocator::
            GrowthEventDroppedCount();

        UIData.reserveGrowthEventCount = SkullbonezCore::Core::Allocation::RuntimeReserveAllocator::
            CopyRecentGrowthEvents( UIData.reserveGrowthEvents, SkullbonezCore::UI::UI_RUNTIME_RESERVE_GROWTH_EVENT_MAX );
    }

    if ( memoryTabActive )
    {
        const std::span<const SkullbonezCore::Core::Allocation::RuntimeReserveCapacityView>
            capacityRows = SkullbonezCore::Core::Allocation::RuntimeReserveAllocator::CapacityRows();

        UIData.reserveCapacityRowCount = (std::min)( static_cast<int>( capacityRows.size() ),
                                                     SkullbonezCore::UI::UI_RUNTIME_RESERVE_CAPACITY_ROW_MAX );

        for ( int index = 0; index < UIData.reserveCapacityRowCount; ++index )
        {
            const SkullbonezCore::Core::Allocation::RuntimeReserveCapacityView&
                source = capacityRows[static_cast<std::size_t>( index )];
            SkullbonezCore::UI::UIRuntimeReserveCapacityRow& destination = reserveCapacityRows[index];
            strncpy_s( destination.ownerName, sizeof( destination.ownerName ), source.ownerName ? source.ownerName : "",
                       _TRUNCATE );

            strncpy_s( destination.capacityReason, sizeof( destination.capacityReason ),
                       source.capacityReason ? source.capacityReason : "", _TRUNCATE );

            strncpy_s( destination.subsystemName, sizeof( destination.subsystemName ),
                       SkullbonezCore::Core::Allocation::RuntimeReserveSubsystemName( source.subsystem ), _TRUNCATE );

            destination.elementSizeBytes = source.elementSizeBytes;
            destination.currentCapacity = source.currentCapacity;
            destination.liveCount = source.liveCount;
            destination.sessionHighWater = source.sessionHighWater;
            destination.residentBytes = source.residentBytes;
        }

        UIData.reserveCapacityRows = reserveCapacityRows;
    }
}
void ProjectOperatorUiPresentation( UI::InGameUIFrameData& UIData, const SceneSessionState& scene,
                                    const RuntimeViewModel& view,
                                    const SkullbonezCore::UI::RunSceneBrowserState& sceneBrowser,
                                    const UI::OperatorEditorFrameView& operatorEditorView, bool sceneHasCurrentEntry,
                                    const char* currentScenePath, int currentSceneBrowserIndex, float sceneEnergyForDisplay )
{
    const char* sceneName = "";

    if ( view.sceneMode && sceneHasCurrentEntry && currentScenePath )
    {
        sceneName = SceneFileNameFromPath( currentScenePath );
    }

    UIData.sceneName = sceneName;
    UIData.sceneOptions = sceneBrowser.namePtrs.empty() ? nullptr : sceneBrowser.namePtrs.data();
    UIData.sceneOptionCount = static_cast<int>( sceneBrowser.namePtrs.size() );
    UIData.selectedSceneOption = currentSceneBrowserIndex;
    UIData.selectedCineModeSceneOption = sceneBrowser.selectedCineModeSceneIndex;
    UIData.modelCount = view.modelCount;
    UIData.currentFrame = view.frame;
    UIData.targetFrameCount = view.targetFrameCount;
    UIData.rngSeed = scene.rngSeed;
    UIData.solverBallCount = scene.solverBallCount;
    UIData.solverBoxCount = scene.solverBoxCount;
    UIData.currentSceneIndex = view.sceneIndex;
    UIData.sceneCount = view.sceneCount;
    UIData.sceneMode = view.sceneMode;
    UIData.scenePhysicsEnabled = view.scenePhysics;
    UIData.sceneTextEnabled = view.sceneText;
    UIData.fixedStep = view.fixedStep;
    UIData.exitOnComplete = scene.isExitOnComplete;
    UIData.testComplete = scene.isTestComplete;
    UIData.sceneEnergy = sceneEnergyForDisplay;
    UIData.timeScale = view.timeScale;
    UIData.presentationInterpolation = view.presentationInterpolation;
    UIData.presentationPinned = view.presentationPinned;
    UIData.presentationAlpha = view.presentationAlpha;
    UIData.canSaveSceneDefaults = view.sceneMode && sceneHasCurrentEntry && currentScenePath && currentScenePath[0] != '\0';

    // Invariant: representative GameUI controls display the same immutable
    // values supplied to the secondary editor for this frame.
    UIData.operatorEditor = operatorEditorView;
    UIData.sceneName = UIData.operatorEditor.scene.sceneName;
    UIData.modelCount = UIData.operatorEditor.scene.modelCount;
    UIData.currentFrame = UIData.operatorEditor.scene.currentFrame;
    UIData.currentSceneIndex = UIData.operatorEditor.scene.currentSceneIndex;
    UIData.sceneCount = UIData.operatorEditor.scene.sceneCount;
    UIData.timeScale = UIData.operatorEditor.scene.timeScale;
    UIData.worldGravity = UIData.operatorEditor.property.worldGravity;
    UIData.worldFluidHeight = UIData.operatorEditor.property.worldFluidHeight;
    UIData.worldFluidDensity = UIData.operatorEditor.property.worldFluidDensity;
    UIData.vsyncEnabled = UIData.operatorEditor.rendering.vsyncEnabled;
    UIData.presentationInterpolation = UIData.operatorEditor.rendering.presentationInterpolation;
    UIData.presentationAlpha = UIData.operatorEditor.rendering.presentationAlpha;
    UIData.cinematicRendering = UIData.operatorEditor.rendering.cinematicRendering;
    UIData.replayMemoryPreset = UIData.operatorEditor.replay.memoryPreset;
    UIData.replayMemoryRequestedRetentionSeconds = UIData.operatorEditor.replay.requestedRetentionSeconds;
    UIData.replayMemoryRequestedBudgetMiB = UIData.operatorEditor.replay.requestedBudgetMiB;
    UIData.replayMemoryPresentationRetentionSeconds = UIData.operatorEditor.replay.presentationRetentionSeconds;
    UIData.replayMemorySolverRetentionSeconds = UIData.operatorEditor.replay.solverRetentionSeconds;
    UIData.replayMemoryBudgetClamped = UIData.operatorEditor.replay.memoryBudgetClamped;
    UIData.replayMemorySolverWindowReduced = UIData.operatorEditor.replay.solverWindowReduced;
}
void ProjectOperatorUiSettings( UI::InGameUIFrameData& UIData, const OverlayDebugState& debug,
                                const RenderPresentationSettings& renderPresentation, const SceneWorld& world,
                                const SkullbonezCore::Core::EngineConfig& config,
                                const SkullbonezCore::Core::CinematicRenderConfig& cinematic, bool cinematicRendering )
{
    UIData.modelCapacity = SkullbonezCore::Core::ActiveSceneObjectCapacity( config );
    UIData.textOnly = debug.isTextOnly;
    UIData.vsyncEnabled = renderPresentation.vsyncEnabled;
    UIData.pipelineSyncEnabled = renderPresentation.pipelineSyncEnabled;
    UIData.worldGravity = world.Environment().GetGravity();
    UIData.worldFluidHeight = world.Environment().GetFluidSurfaceHeight();
    UIData.worldFluidDensity = world.Environment().GetFluidDensity();
    UIData.physicsDebug = BuildDiagnosticsPhysicsUIStatus( debug );
    UIData.physicsSleepEnabled = world.Physics().IsSleepEnabled();
    const Gameplay::TornadoFieldConfig& tornadoField = world.Tornado().GetFieldConfig();
    UIData.tornadoEnabled = tornadoField.enabled;
    UIData.tornadoVisualShell = world.Tornado().VisualSettings().enabled && tornadoField.enabled;
    UIData.tornadoFieldVectors = tornadoField.visualizeVelocityField;
    UIData.tornadoRadius = tornadoField.radius;
    UIData.tornadoHeight = tornadoField.height;
    UIData.tornadoInwardAcceleration = tornadoField.inwardAcceleration;
    UIData.tornadoSwirlAcceleration = tornadoField.swirlAcceleration;
    UIData.tornadoLiftAcceleration = tornadoField.liftAcceleration;
    UIData.terrainFrictionCoeff = config.physicsMaterial.frictionCoeff;
    UIData.objectFrictionCoeff = config.physicsMaterial.objectFrictionCoeff;
    UIData.rollingFrictionCoeff = config.physicsMaterial.rollingFrictionCoeff;
    UIData.waterFreezeDebug = debug.isWaterFreezeDebug;
    UIData.waterFlatDebug = debug.isWaterFlatDebug;
    UIData.terrainHidden = debug.isTerrainHidden;
    UIData.waterHidden = debug.isWaterHidden;
    UIData.waterNoReflect = debug.isWaterNoReflect;
    UIData.waterRTReflect = debug.isWaterRTReflect;
    UIData.cinematicRendering = cinematicRendering;
    UIData.ordinaryRender = config.ordinaryRender;
    UIData.cinematic = cinematic;
}
void ProjectOperatorUiInteraction( UI::InGameUIFrameData& UIData, const RunRayCastTestState& rayCastTest,
                                   const RunEditorPlacementState& editor, const RuntimeInputContext& runtimeInput,
                                   const CameraControlState& camera, const UI::InGameUI& ui, uint32_t cameraModeEnabledMask,
                                   const char* cameraModeLabel )
{
    UIData.trackHeight = camera.trackBallRow.IsValid() ? camera.trackHeight : 0.0f;
    UIData.autoCycleInterval = camera.autoCycleInterval > 0.0f ? camera.autoCycleInterval : 0.0f;
    UIData.rayCastVisualization = rayCastTest.visualizeRays;
    UIData.rayCastImpulseStrength = rayCastTest.impulseStrength;
    UIData.launcherProjectileSpeed = rayCastTest.projectileSpeed;
    const RuntimeInputMode runtimeInputMode = runtimeInput.CurrentMode();
    UIData.cameraModeIndex = static_cast<int>( camera.mode );
    UIData.cameraModeEnabledMask = cameraModeEnabledMask;
    UIData.runtimeInputModeLabel = cameraModeLabel;
    UIData.cameraMouseActive = ( runtimeInputMode == RuntimeInputMode::FlyCamera ||
                                 runtimeInputMode == RuntimeInputMode::Launcher ||
                                 runtimeInputMode == RuntimeInputMode::EditorViewportLook ) &&
                               !ui.BlocksCameraMouse();

    UIData.nativeCursorVisible = !UIData.cameraMouseActive;
    UIData.editorModeEnabled = editor.editorModeEnabled;
    UIData.editorPlacementMode = editor.placementModeEnabled;
    UIData.editorPlaceStatic = editor.placeStaticObject;
    UIData.editorTerrainAlign = editor.autoTerrainAlign;
    UIData.editorViewportLookActive = editor.viewportLookActive;
    UIData.editorObjectType = editor.objectType;
    UIData.editorUndoDepth = static_cast<int>( editor.history.UndoDepth() );
    UIData.editorRedoDepth = static_cast<int>( editor.history.RedoDepth() );
}

// PROJECTION_FUNCTIONS

} // namespace Runtime
} // namespace SkullbonezCore
