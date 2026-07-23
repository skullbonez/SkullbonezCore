/*
File: SkullbonezSource/Runtime/UI/OperatorEditorFrameComposer.cpp
Purpose:
  Builds and renders the shared operator-editor presentation for one frame.

Summary:
  The composer synchronously samples scene, physics, replay, rendering, tools,
  diagnostics, and input owners into one bounded OperatorEditorFrameView before
  either development UI surface consumes it.

Mental model:
  This is a projection owner, not a service locator: it performs the complete
  UI-specific traversal and draw preparation during one call, then releases
  every borrowed runtime owner before control returns to the frame sequencer.

Glossary:
  Shared editor view: One UI-facing value projection used by Legacy and ImGui.
  Cold detail: Inspector and diagnostics data sampled only while ImGui is shown.
  Late UI pass: Presentation work recorded after the 3D game view.

Invariants:
  - Owner references are borrowed for this call only and never retained.
  - Dense physics rows are used only after typed-handle validation.
  - Diagnostics and inspector snapshots do not grow runtime storage.
  - Both surfaces observe identical scene, replay, and rendering values.

Related:
  - OperatorEditorFrameComposer.h
  - RuntimeFrameViews.h
  - Agentic/Reference/comment-style-guide.md
*/
#include "OperatorEditorFrameComposer.h"

#include "../Run.h"
#include "../RuntimeOverlayDiagnostics.h"
#include "../RuntimeValidationHarness.h"
#include "../RuntimeFrameViews.h"
#include "../RuntimeViewModel.h"
#include "../Window.h"
#include "../../Core/WorkerPool.h"
#include "../Replay/ReplayOverlayPackets.h"
#include "../Scene/SceneRuntimeLoad.h"
#include "../CaptureSystem.h"
#include "../Editor/EditorTools.h"
#include "../../Core/Allocation/RuntimeAllocationTracker.h"
#include "../../Core/Allocation/RuntimeReserveAllocator.h"
#include "../../Core/FatalError.h"
#include "../../Core/Profiler.h"
#include "../../Physics/ColliderStore.h"
#include "../../Physics/PhysicsApi.h"
#include "../../Rendering/DX12/Dx12Diagnostics.h"
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
using namespace SkullbonezCore::Runtime::RunInternal;
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
    view.cinematicFeatures[static_cast<int>( UICinematicFeature::VolumetricLight )] =
        cinematic.volumetricLightingEnabled;
    view.cinematicFeatures[static_cast<int>( UICinematicFeature::Bloom )] = cinematic.bloomEnabled;
    view.cinematicFeatures[static_cast<int>( UICinematicFeature::Fog )] = cinematic.fogEnabled;
    view.cinematicFeatures[static_cast<int>( UICinematicFeature::TerrainRelief )] = cinematic.terrainReliefEnabled;
    view.cinematicFeatures[static_cast<int>( UICinematicFeature::Shadows )] = cinematic.shadow.enabled;
}

void Render( RuntimeFrameHostView& host,
             RuntimeFrameInteractionView& interactionOwners,
             RuntimeFrameSceneView& sceneOwners,
             RuntimeRenderer& renderer,
             ReplayRuntime& replayRuntime,
             const RuntimeUiTextFrameFacts& facts,
             SkullbonezCore::UI::OperatorEditorFrameView& operatorEditorView,
             const ReplayOverlay::ReplayOverlayStateView& replayOverlay,
             SkullbonezCore::Rendering::Dx12Diagnostics& renderDiagnostics,
             const SkullbonezCore::UI::UIRenderContext& uiRender,
             const RuntimeRenderModelFrameView& renderModels )
{
    DiagnosticsRuntime& diagnosticsRuntime = host.diagnosticsRuntime;
    RunTimerState& timers = sceneOwners.timers;
    RuntimeOverlayPresentationEdit presentationEdit = sceneOwners.overlays.EditPresentation();
    RunDebugState& debug = presentationEdit.State();
    SceneController& sceneController = sceneOwners.sceneController;
    RunSceneState& scene = sceneController.State();
    SkullbonezCore::Core::EngineConfig& config = sceneOwners.config;
    RuntimeTools& runtimeTools = interactionOwners.runtimeTools;
    SkullbonezCore::UI::InGameUI& ui = interactionOwners.operatorUi;
    RuntimeInputContext& runtimeInput = interactionOwners.inputRouter.RuntimeContext();
    RunCameraState& camera = interactionOwners.camera;
    SkullbonezCore::Threading::WorkerPool& workerPool = host.workerPool;
    Window& window = host.window;
    RunLaunchOptions& launchOptions = sceneOwners.launchOptions;
    // Lifetime: the two owner views and value-only facts exist only for this
    // late UI call; no render or UI owner retains them.
    const RunSceneBrowserState& uiSceneBrowser = ui.SceneNavigation().browser;
    const std::string* uiScenePath = sceneController.CurrentPath();
    const ReplayHudStatus sharedReplayHud = replayRuntime.BuildHudStatus( false );
    const SkullbonezCore::Core::CinematicRenderConfig& sharedCinematic = ActiveSceneCinematicConfig( scene, config );
    const bool sharedCinematicRendering = IsSceneCinematicRenderingEnabled( scene, config, launchOptions, debug, true );
    const bool sharedShadows =
        sharedCinematicRendering ? sharedCinematic.shadow.enabled : config.ordinaryRender.shadow.enabled;
    // Invariant: build this common value once. The legacy draw pass and the
    // secondary editor receive this exact object, not independently sampled owners.
    operatorEditorView.scene = { uiScenePath ? uiScenePath->c_str() : "",
                                 uiSceneBrowser.namePtrs.empty() ? nullptr : uiSceneBrowser.namePtrs.data(),
                                 CurrentSceneBrowserIndex( sceneController, uiSceneBrowser ),
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
    sharedRendering.presentationAlpha = facts.presentationAlpha;
    sharedRendering.terrainHidden = debug.isTerrainHidden;
    sharedRendering.waterHidden = debug.isWaterHidden;
    sharedRendering.waterFrozen = debug.isWaterFreezeDebug;
    sharedRendering.waterFlat = debug.isWaterFlatDebug;
    sharedRendering.waterReflectionMode = debug.isWaterNoReflect ? 2 : ( debug.isWaterRTReflect ? 1 : 0 );
    FillOperatorRenderingParameters( sharedRendering, config.ordinaryRender, sharedCinematic );
    const char* sharedGizmoMode = "translate";
    if ( facts.interactionGestureKind == RuntimeInteractionGestureKind::GizmoDrag )
    {
        switch ( facts.interactionGizmoKind )
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
    operatorEditorView.viewport = { facts.cameraModeLabel, sharedGizmoMode, facts.presentationPinned };
    operatorEditorView.replay = { sharedReplayHud.memoryPreset,
                                  sharedReplayHud.requestedRetentionSeconds,
                                  sharedReplayHud.requestedBudgetMiB,
                                  sharedReplayHud.presentationRetentionSeconds,
                                  sharedReplayHud.solverRetentionSeconds,
                                  sharedReplayHud.memoryBudgetClamped,
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
    const int selectedHierarchyRow =
        RunInternal::PeekSelectedEditorModelIndex( sharedEditor, sceneController.Scene().BodyStore() );
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
    operatorEditorView.assets = { sharedEditor.objectType,
                                  SkullbonezCore::UI::EditorTab::OBJECT_TYPE_COUNT,
                                  host.assets.FindAssetLibrarySourceAsset( "assetlib.buildings" ) != nullptr };
#if defined( SKULLBONEZ_DEVELOPMENT_TOOLS )
    // Why: the legacy surface does not consume E12 contextual detail. Sampling
    // cold body/collider/material rows only while the secondary editor is
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
            const PhysicsBodyRecord* body = entity ? bodyStore.RecordForHandle( entity->body ) : nullptr;
            const PhysicsColliderHandle colliderHandle =
                entity ? colliderStore.HandleForBodyHandle( entity->body ) : PhysicsColliderHandle{};
            const ColliderRecord* collider = colliderStore.RecordForHandle( colliderHandle );
            const ColliderAuthoringRecord* colliderAuthoring = colliderStore.AuthoringRecordForHandle( colliderHandle );
            if ( !entity || !body || !collider || !colliderAuthoring )
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
                inspector.renderMaterialName =
                    entity->renderMaterial.name[0] != '\0'
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
                orientation.GetComponents( inspector.orientation[0],
                                           inspector.orientation[1],
                                           inspector.orientation[2],
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
                inspector.volume = body->volume;
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
    if ( operatorEditorView.surfaces.secondaryVisible )
    {
        // Why: the secondary surface can be visible while the legacy UI is
        // hidden. Sample its bounded authoring/diagnostic values here instead
        // of making ImGui depend on whether the legacy text pass happens to run.
        runtimeViewModel =
            RuntimeViewModelBuilder::Build( RuntimeViewModelContext{ sceneController.State(),
                                                                     sceneController.Scene(),
                                                                     sceneController.QueueSize(),
                                                                     diagnosticsRuntime.Capture(),
                                                                     config.runtimeRender.presentationInterpolation,
                                                                     facts.presentationPinned,
                                                                     facts.presentationAlpha } );
        renderTargetPreviews = renderer.ResourceLifecycle().BuildRenderTargetPreviewSnapshot(
            sharedShadows,
            sharedCinematicRendering,
            sharedCinematicRendering && sharedCinematic.volumetricLightingEnabled );
        SkullbonezCore::UI::OperatorEditorDiagnosticsView& diagnostics = operatorEditorView.diagnostics;
        // Invariant: the right rail reads fixed snapshots and cached counters;
        // opening Diagnostics must not trigger an allocation scan or grow data.
        const SkullbonezCore::Core::MainMemoryStats& mainMemory = diagnosticsRuntime.MainMemoryStatsSnapshot();
        const SkullbonezCore::Rendering::RenderMemoryStats renderMemory = renderDiagnostics.GetRenderMemoryStats();
        diagnostics.rendererName = renderDiagnostics.GetRendererName();
        diagnostics.drawCalls = renderDiagnostics.GetFrameDrawCallCount();
        diagnostics.uiDrawCalls = timers.lastUIDrawCalls;
        diagnostics.workerThreadCount = workerPool.GetThreadCount();
        diagnostics.maxWorkerThreadCount = SkullbonezCore::Threading::WorkerPool::MaxThreadCount();
        diagnostics.fps = facts.secondsPerFrame > 0.0 ? static_cast<float>( 1.0 / facts.secondsPerFrame ) : 0.0f;
        diagnostics.renderMs =
            ( timers.rollingRenderTime > 0.0f ? timers.rollingRenderTime : timers.renderTime ) * 1000.0f;
        diagnostics.physicsMs =
            ( timers.rollingPhysicsTime > 0.0f ? timers.rollingPhysicsTime : timers.physicsTime ) * 1000.0f;
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
        diagnostics.physicsPipelineStageName =
            PhysicsPipelineStageName( static_cast<PhysicsPipelineStage>( stageIndex ) );
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
        diagnostics.uploadUsedBytes = renderMemory.uploadUsedBytes;
        diagnostics.uploadCapacityBytes = renderMemory.uploadCapacityBytes;
        diagnostics.replayReserveGrowthEvents =
            SkullbonezCore::Core::Allocation::RuntimeReserveAllocator::GrowthEventCount();
        diagnostics.renderTargetCount =
            (std::min)( renderTargetPreviews.count, SkullbonezCore::UI::OPERATOR_EDITOR_RENDER_TARGET_CAPACITY );
        for ( int index = 0; index < diagnostics.renderTargetCount; ++index )
        {
            const RuntimeRenderTargetPreview& source = renderTargetPreviews.targets[static_cast<size_t>( index )];
            diagnostics.renderTargets[index] = { source.label,
                                                 source.width,
                                                 source.height,
                                                 source.available && source.textureHandle != 0u,
                                                 source.depth,
                                                 source.hdr };
        }
    }
    const UiTextPassState uiTextState{ debug,
                                       sceneController.CrossScenePauseLocked(),
                                       scene,
                                       renderer.PresentationSettings(),
                                       sceneController.Scene(),
                                       config,
                                       runtimeTools.RayCastTest(),
                                       runtimeTools.Editor(),
                                       runtimeInput,
                                       camera,
                                       runtimeViewModel,
                                       uiSceneBrowser,
                                       renderTargetPreviews,
                                       operatorEditorView,
                                       &workerPool,
                                       window.ClientWidth(),
                                       window.ClientHeight(),
                                       sceneController.QueueSize(),
                                       sceneController.HasCurrentEntry(),
                                       uiScenePath ? uiScenePath->c_str() : nullptr,
                                       CurrentSceneBrowserIndex( sceneController, uiSceneBrowser ),
                                       facts.cameraModeEnabledMask,
                                       facts.cameraModeLabel,
                                       facts.launcherFireModeLabel,
                                       facts.isLauncherCameraMode,
                                       replayOverlay.shouldRenderScrubber,
                                       replayRuntime.BuildInputView().hasPathTarget };

    if ( !uiRender.textures || !uiRender.geometry )
    {
        SB_FATAL( "Runtime/UI", "Operator editor frame has no UI render resources." );
    }
    renderer.PrepareUiFrameTarget();

    if ( renderer.ResourceLifecycle().ShouldRenderUiText( uiTextState, ui ) )
    {
        runtimeViewModel =
            RuntimeViewModelBuilder::Build( RuntimeViewModelContext{ sceneController.State(),
                                                                     sceneController.Scene(),
                                                                     sceneController.QueueSize(),
                                                                     diagnosticsRuntime.Capture(),
                                                                     config.runtimeRender.presentationInterpolation,
                                                                     facts.presentationPinned,
                                                                     facts.presentationAlpha } );
        const SkullbonezCore::Core::CinematicRenderConfig& uiCinematic = ActiveSceneCinematicConfig( scene, config );
        const bool uiCinematicRendering = IsSceneCinematicRenderingEnabled( scene, config, launchOptions, debug, true );
        const bool shadowsAvailable =
            uiCinematicRendering ? uiCinematic.shadow.enabled : config.ordinaryRender.shadow.enabled;
        renderTargetPreviews = renderer.ResourceLifecycle().BuildRenderTargetPreviewSnapshot(
            shadowsAvailable,
            uiCinematicRendering,
            uiCinematicRendering && uiCinematic.volumetricLightingEnabled );
        const ReplayOverlay::ReplayOverlayRenderContext replayOverlayContext{ *uiRender.textures,
                                                                              *uiRender.geometry,
                                                                              host.profiler,
                                                                              replayOverlay,
                                                                              facts.legacyDevelopmentUiActive,
                                                                              runtimeTools.Editor().editorModeEnabled,
                                                                              ui.IsVisible(),
                                                                              ui.IsMinimized(),
                                                                              scene.isScenePhysics,
                                                                              facts.interactionGestureKind,
                                                                              window.ClientWidth(),
                                                                              window.ClientHeight(),
                                                                              timers.simulationTimer.GetTotalTime() };
        const int uiDrawCallStart = renderDiagnostics.GetFrameDrawCallCount();
        const bool replayMemoryStatsRequested =
            ui.IsVisible() && !ui.IsMinimized() && ui.GetActiveTab() == SkullbonezCore::UI::InGameUITab::Memory;
        const ReplayHudStatus replayHud = replayRuntime.BuildHudStatus( replayMemoryStatsRequested );
        PROFILE_BEGIN( host.profiler, "Frame/UI" );
        {
            CoreAllocation::RuntimeAllocationScope allocationScope( CoreAllocation::RuntimeAllocationPhase::Render );
            DRAW_CALL_TRACE_SCOPE( renderDiagnostics, "Frame/UI" );
            // Lifetime: every reference in this stack record remains valid until
            // the renderer executes the synchronous UI-text graph callback below.
            const UiTextPassInputs uiTextInputs{ uiTextState,
                                                 timers,
                                                 ui,
                                                 renderDiagnostics,
                                                 uiRender,
                                                 renderModels,
                                                 diagnosticsRuntime,
                                                 replayHud,
                                                 replayOverlayContext,
                                                 uiCinematic,
                                                 uiCinematicRendering,
                                                 facts.secondsPerFrame };
            renderer.RenderUiText( uiTextInputs );
        }
        PROFILE_END( host.profiler, "Frame/UI" );
        const int uiDrawCallEnd = renderDiagnostics.GetFrameDrawCallCount();
        timers.lastUIDrawCalls = (std::max)( 0, uiDrawCallEnd - uiDrawCallStart );
    }
    else
    {
        timers.lastUIDrawCalls = 0;
    }
}


} // namespace OperatorEditorFrameComposer
} // namespace Runtime
} // namespace SkullbonezCore
