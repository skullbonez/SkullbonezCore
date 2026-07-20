/*
File: SkullbonezSource/Runtime/RunFrame.cpp
Purpose:
  Runs one frame of input, simulation, rendering, profiling, and presentation.

Summary:
  RunFrame.cpp runs one frame of input, simulation, rendering, profiling, and
  presentation. As an implementation unit, keep edits anchored on local owner
  boundaries and call direction and on the glossary/invariants below.

Glossary:
  Simulation tick: One runtime decision about whether to advance logic, camera,
    and zero or more fixed physics steps this frame.
  Fixed-step edge: Runtime-owned code that repairs model/body topology before
    PhysicsEngine::Step and applies presentation-only refresh work after it.
  PhysicsBodyStore: Physics-owned body rows for live pose, velocity, fixed
    state, and replay identity.
  ColliderStore: Physics-owned collider rows for exact shape variants, material
    parameters, and broadphase radius.
  Lane R result: Recoverable scene-control or capture failure that prevents a
    failed side effect from being reported as a successful frame transition.
  Presentation pin: Per-frame alpha override to exact current solver state for
    scheduled and auto-cycle capture automation.
  Frame view: Non-copyable stack record of references used to name per-call
    borrows without moving ownership out of the composition root.
  Submitted-frame mark: Development profiler boundary emitted only after DX12
    accepts a successful Present for the game frame.
  Shared editor view: One immutable scene/property/render/replay/tool value
    assembled before the selected operator frontend renders.

Invariants:
  - Frame work updates input, simulation, capture, rendering, and diagnostics
    in a stable order used by validation and replay comparisons.
  - Capture pinning is decided before physics and camera work for that frame.
  - Frame views are created once per frame turn and never retained by helpers.
  - A successful submitted game frame emits exactly one development profiler
    frame mark; failed or capture-only turns emit none.
  - A development surface swap hides the source before the target begins a frame.

Related:
  - RuntimeFrameViews.h defines the frame-helper calling convention.
  - Agentic/Reference/runtime-reference.md
  - Agentic/Reference/comment-style-guide.md
*/
#include "Run.h"
#include "RuntimeOverlayDiagnostics.h"
#include "RuntimeValidationHarness.h"
#include "RuntimeFrameViews.h"
#include "RuntimeViewModel.h"
#include "Window.h"
#include "../Core/WorkerPool.h"
#include "InputFrame.h"
#include "Replay/ReplayRestoreTransactions.h"
#include "Replay/ReplayOverlayRenderer.h"
#include "Replay/ReplayRestoreService.h"
#include "DemoDirectorPlayback.h"
#include "Scene/SceneRuntimeLoad.h"

#include "CaptureSystem.h"
#include "Editor/EditorTools.h"
#include "Replay/ReplayV2Artifact.h"
#include "../Core/Allocation/RuntimeAllocationTracker.h"
#include "../Core/Allocation/RuntimeReserveAllocator.h"
#include "../Core/TracyClientOwner.h"
#if defined( SKULLBONEZ_DEVELOPMENT_TOOLS )
#include "DevelopmentTools/ImGuiEditorOwner.h"
#endif
#include "OperatorCommandApplier.h"
#include "Scene/SceneRuntimeStyle.h"

#include "../Core/FatalError.h"
#include "../Core/Log.h"
#include "../Core/Profiler.h"
#include "../Physics/ColliderStore.h"
#include "../Physics/PhysicsEngine.h"
#include "../Physics/PhysicsApi.h"
#include "../Physics/PhysicsDiagnosticsSink.h"
#include "../Physics/PhysicsTimestep.h"
#include "../Rendering/RenderInstanceStore.h"
#include "../Rendering/IRenderDiagnostics.h"
#include "../Rendering/IRenderDeviceLifecycle.h"
#include "../UI/UI.h"
#include "../UI/UITabEditor.h"

#include <algorithm>
#include <cmath>
#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <limits>
#include <utility>
#include <vector>

using namespace SkullbonezCore::Runtime;
using namespace SkullbonezCore::Runtime::ReplayTimelineOperations;
using namespace SkullbonezCore::Math::CollisionDetection;
using namespace SkullbonezCore::Math::Orientation;
using namespace SkullbonezCore::Math::Transformation;
using namespace SkullbonezCore::Physics;
using namespace SkullbonezCore::Runtime::RunInternal;
using SkullbonezCore::Math::Vector::Vector3;
namespace CoreAllocation = SkullbonezCore::Core::Allocation;

namespace
{

// Why: Profile builds do not emit Debug-only scene-finished telemetry, so
// automation exits need an explicit stdout breadcrumb near the quit request.
void PrintRuntimeExitReason( const char* reason )
{
    printf( "[runtime-exit] %s\n", reason );
    fflush( stdout );
}

float ResolvePresentationAlpha( const SkullbonezCore::Core::EngineConfig& config,
                                bool capturePresentationPinned,
                                float simulationPresentationAlpha )
{
    if ( !config.runtimeRender.presentationInterpolation || capturePresentationPinned )
    {
        return 1.0f;
    }
    return std::clamp( simulationPresentationAlpha, 0.0f, 1.0f );
}

void FillOperatorRenderingParameters( SkullbonezCore::UI::OperatorEditorRenderingView& view,
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

void RenderExecuteUiTextFrame( RuntimeFrameHostView& host,
                               RuntimeFrameInteractionView& interactionOwners,
                               RuntimeFrameSceneView& sceneOwners,
                               RuntimeFramePresentationView& presentationOwners,
                               ReplayRuntime& replayRuntime,
                               const RuntimeUiTextFrameFacts& facts,
                               const ReplayOverlay::ReplayOverlayStateView& replayOverlay,
                               SkullbonezCore::Rendering::IRenderDiagnostics& renderDiagnostics,
                               const SkullbonezCore::UI::UIRenderContext& uiRender,
                               const RuntimeRenderModelFrameView& renderModels )
{
    RuntimeRenderer& renderer = presentationOwners.renderer;
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
    facts.operatorEditorView.scene = { uiScenePath ? uiScenePath->c_str() : "",
                                       uiSceneBrowser.namePtrs.empty() ? nullptr : uiSceneBrowser.namePtrs.data(),
                                       CurrentSceneBrowserIndex( sceneController, uiSceneBrowser ),
                                       static_cast<int>( uiSceneBrowser.namePtrs.size() ),
                                       scene.currentFrame,
                                       sceneController.Scene().SceneEntityCount(),
                                       scene.timeScale,
                                       uiScenePath && !uiScenePath->empty(),
                                       false };
    facts.operatorEditorView.property = { sceneController.Scene().Environment().GetGravity(),
                                          sceneController.Scene().Environment().GetFluidSurfaceHeight(),
                                          sceneController.Scene().Environment().GetFluidDensity() };
    SkullbonezCore::UI::OperatorEditorRenderingView& sharedRendering = facts.operatorEditorView.rendering;
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
    if ( facts.interactionGesture.kind == RuntimeInteractionGestureKind::GizmoDrag )
    {
        switch ( facts.interactionGesture.gizmoKind )
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
    facts.operatorEditorView.viewport = { facts.cameraModeLabel, sharedGizmoMode, facts.presentationPinned };
    facts.operatorEditorView.replay = { sharedReplayHud.memoryPreset,
                                        sharedReplayHud.requestedRetentionSeconds,
                                        sharedReplayHud.requestedBudgetMiB,
                                        sharedReplayHud.presentationRetentionSeconds,
                                        sharedReplayHud.solverRetentionSeconds,
                                        sharedReplayHud.memoryBudgetClamped,
                                        sharedReplayHud.solverWindowReduced };
    facts.operatorEditorView.surfaces = { ui.IsVisible(), facts.operatorEditorView.surfaces.secondaryVisible };
    const RunEditorPlacementState& sharedEditor = runtimeTools.Editor();
    facts.operatorEditorView.scene.dirty = sharedEditor.history.IsDirty();
    facts.operatorEditorView.tools = { sharedEditor.editorModeEnabled,
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
    facts.operatorEditorView.hierarchy.totalRowCount = static_cast<uint32_t>( hierarchyEntities.Count() );
    const uint32_t hierarchyRowCount = (std::min)( facts.operatorEditorView.hierarchy.totalRowCount,
                                                   SkullbonezCore::UI::OPERATOR_EDITOR_HIERARCHY_ROW_CAPACITY );
    facts.operatorEditorView.hierarchy.rowCount = hierarchyRowCount;
    facts.operatorEditorView.hierarchy.truncated = facts.operatorEditorView.hierarchy.totalRowCount > hierarchyRowCount;
    for ( uint32_t index = 0u; index < hierarchyRowCount; ++index )
    {
        const SceneEntityRecord& entity = hierarchyEntities.At( static_cast<int>( index ) );
        SkullbonezCore::UI::OperatorEditorHierarchyRow& row = facts.operatorEditorView.hierarchy.rows[index];
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
            facts.operatorEditorView.hierarchy.selectedSceneObjectId = row.sceneObjectId;
        }
    }
    facts.operatorEditorView.assets = { sharedEditor.objectType,
                                        SkullbonezCore::UI::EditorTab::OBJECT_TYPE_COUNT,
                                        host.assets.FindAssetLibrarySourceAsset( "assetlib.buildings" ) != nullptr };
#if defined( SKULLBONEZ_DEVELOPMENT_TOOLS )
    // Why: the legacy surface does not consume E12 contextual detail. Sampling
    // cold body/collider/material rows only while the secondary editor is
    // visible keeps ordinary Profile and shipping frames on their prior path.
    if ( facts.operatorEditorView.surfaces.secondaryVisible )
    {
        SkullbonezCore::UI::OperatorEditorInspectorView& inspector = facts.operatorEditorView.inspector;
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
            if ( !entity || !body || !collider )
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
                inspector.contactMaterialName = collider->contactMaterialName;
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
        const TornadoFieldConfig& tornado = sceneController.Scene().Physics().GetTornadoFieldConfig();
        facts.operatorEditorView.world = { scene.modelCount,
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
    if ( facts.operatorEditorView.surfaces.secondaryVisible )
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
        renderTargetPreviews = renderer.BuildRenderTargetPreviewSnapshot(
            sharedShadows,
            sharedCinematicRendering,
            sharedCinematicRendering && sharedCinematic.volumetricLightingEnabled );
        SkullbonezCore::UI::OperatorEditorDiagnosticsView& diagnostics = facts.operatorEditorView.diagnostics;
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
        diagnostics.tornadoVisualShell = renderer.TornadoVisualSettingsSnapshot().enabled;
        diagnostics.tornadoFieldVectors =
            sceneController.Scene().Physics().GetTornadoFieldConfig().visualizeVelocityField;
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
                                       facts.operatorEditorView,
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

    if ( renderer.ShouldRenderUiText( uiTextState, ui ) )
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
        renderTargetPreviews =
            renderer.BuildRenderTargetPreviewSnapshot( shadowsAvailable,
                                                       uiCinematicRendering,
                                                       uiCinematicRendering && uiCinematic.volumetricLightingEnabled );
        const ReplayOverlay::ReplayOverlayRenderContext replayOverlayContext{ *uiRender.commands,
                                                                              host.profiler,
                                                                              replayOverlay.scrubber,
                                                                              replayOverlay.prediction,
                                                                              replayOverlay.pathVisualizer,
                                                                              replayOverlay.velocityEdit,
                                                                              replayOverlay.causeTree,
                                                                              replayOverlay.solverStats,
                                                                              replayOverlay.selectedPresentation,
                                                                              replayOverlay.latestPresentation,
                                                                              replayOverlay.selectedSolver,
                                                                              replayOverlay.latestSolver,
                                                                              replayOverlay.selectedPrediction,
                                                                              replayOverlay.currentPresentation,
                                                                              replayOverlay.currentSolver,
                                                                              replayOverlay.solverPresentTrackPosition,
                                                                              replayOverlay.loadedPresentation,
                                                                              replayOverlay.predictionTimelineAvailable,
                                                                              facts.legacyDevelopmentUiActive,
                                                                              replayOverlay.shouldRenderScrubber,
                                                                              runtimeTools.Editor().editorModeEnabled,
                                                                              ui.IsVisible(),
                                                                              ui.IsMinimized(),
                                                                              scene.isScenePhysics,
                                                                              facts.interactionGesture.kind,
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
            renderer.RenderUiText( renderDiagnostics,
                                   uiRender,
                                   uiTextState,
                                   timers,
                                   ui,
                                   renderModels,
                                   diagnosticsRuntime,
                                   replayHud,
                                   replayOverlayContext,
                                   uiCinematic,
                                   uiCinematicRendering,
                                   facts.secondsPerFrame );
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


} // namespace

namespace
{

void CaptureReplayPostStep( RuntimeFrameInteractionView& interactionOwners,
                            RuntimeFrameSceneView& sceneOwners,
                            ReplayRuntime& replayRuntime,
                            SkullbonezCore::Core::Profiler* profiler )
{
    RuntimeTools& runtimeTools = interactionOwners.runtimeTools;
    SkullbonezCore::Runtime::SceneController& models = sceneOwners.sceneController;
    const RunSceneState& scene = models.State();
    RunTimerState& timers = sceneOwners.timers;
    const RunDebugState debug = sceneOwners.overlays.PresentationSnapshot();
    SkullbonezCore::Environment::CameraCollection& cameras = models.Scene().Cameras();
    SkullbonezCore::Environment::WorldEnvironment& world = models.Scene().Environment();
    PhysicsEngine& physics = models.Scene().Physics();
    const SceneEntityStore& entities = models.Scene().Entities();
    CoreAllocation::RuntimeAllocationScope allocationScope( CoreAllocation::RuntimeAllocationPhase::Replay );
    PROFILE_SCOPED( profiler, "Frame/Physics/Step/ReplayCapture" );
    ReplayCaptureInput input;
    input.sceneFrame = scene.currentFrame;
    input.simulationSeconds = timers.simulationTimer.GetTimeSinceLastStart();
    input.physicsDt = PHYSICS_FIXED_DT;
    input.fixedStep = scene.isFixedStep;
    input.scenePhysicsEnabled = scene.isScenePhysics;
    input.sceneTextEnabled = scene.isSceneText;
    input.waterHidden = debug.isWaterHidden;
    input.terrainHidden = debug.isTerrainHidden;
    input.cameras = &cameras;
    input.world = &world;
    input.physics = &physics;
    input.entities = &entities;
    input.bodyStore = &models.Scene().BodyStore();
    input.colliderStore = &models.Scene().Colliders();
    replayRuntime.CaptureFrame( input, runtimeTools );
}

} // namespace

SkullbonezCore::Core::SbResult Run::Execute()
{
    if ( m_skipExecute )
    {
        return SkullbonezCore::Core::SbResult::Success();
    }
    if ( m_applicationExit.ExitRequested() )
    {
        return m_applicationExit.Resolve( 0 );
    }
    MSG msg;
    int messageExitCode = 0;
    constexpr int kMaxMessagesPerFrame = 256;

    for ( ;; )
    {
        bool quitRequested = false;
        int messagesDrained = 0;
        // Hazard: a device or window can flood the thread queue faster than
        // frame work consumes it. The cap keeps rendering responsive by
        // deferring excess messages to the next frame; reaching it is not an
        // error and preserves FIFO order in the Win32 queue.
        while ( messagesDrained < kMaxMessagesPerFrame && PeekMessage( &msg, nullptr, 0, 0, PM_REMOVE ) )
        {
            ++messagesDrained;
            if ( msg.message == WM_QUIT )
            {
                m_validationHarness->PrintGraphicsStressExitSummary( m_sceneController.State().currentFrame );
                // Concept: WM_QUIT is the platform's stop notification, not the
                // process result by itself. Preserve a Run-owned failure when
                // one already exists; otherwise translate the posted integer.
                m_applicationExit.RequestNormalExit();
                messageExitCode = static_cast<int>( msg.wParam );
                quitRequested = true;
                break;
            }
            TranslateMessage( &msg );
            DispatchMessage( &msg );
        }
        if ( quitRequested )
        {
            break;
        }

        {
            CoreAllocation::RuntimeAllocationScope frameAllocationScope(
                CoreAllocation::RuntimeAllocationPhase::SteadyGameplay );
            double secondsPerFrame = m_timers.frameTimer.GetElapsedTime();
            secondsPerFrame = std::clamp( secondsPerFrame, 0.0, 0.05 );

            m_timers.frameTimer.StartTimer();
            PROFILE_FRAME_BEGIN( m_profiler );
            m_timers.workTimer.StartTimer();
            // Lifetime: borrow the startup-owned renderer once for this frame
            // turn. Narrow facets keep reset, GPU-drain, UI accounting, and
            // present from each reaching through the process-global service.
            if ( !m_renderBackendView.deviceLifecycle || !m_renderBackendView.renderDiagnostics ||
                 !m_renderBackendView.renderResources || !m_renderBackendView.renderCommands )
            {
                SB_FATAL( "RunFrame", "Run::Execute requires a render backend." );
            }
            SkullbonezCore::Rendering::IRenderDiagnostics& frameRenderDiagnostics =
                *m_renderBackendView.renderDiagnostics;
            SkullbonezCore::Rendering::IRenderDeviceLifecycle& renderLifecycle = *m_renderBackendView.deviceLifecycle;
            SkullbonezCore::Rendering::IRenderResourceFactory& frameRenderResources =
                *m_renderBackendView.renderResources;
            SkullbonezCore::Rendering::IRenderCommandContext& frameRenderCommands = *m_renderBackendView.renderCommands;
            const SkullbonezCore::UI::UIRenderContext uiRender = { &m_assets,
                                                                   &frameRenderResources,
                                                                   &frameRenderCommands,
                                                                   &frameRenderDiagnostics };
            // Lifetime: the frame views are stack-only borrow maps for this
            // turn. They are never assigned to Run or passed to retained work.
            RuntimeFrameHostView frameHost{ m_applicationExit,
                                            m_diagnosticsRuntime,
                                            m_assets,
                                            m_workerPool,
                                            m_window,
                                            m_profiler };
            RuntimeFrameInteractionView frameInteraction{ m_inputRouter,
                                                          m_interaction,
                                                          m_attachedCamera,
                                                          *m_operatorUi,
                                                          m_runtimeTools,
                                                          m_camera };
            RuntimeFrameSceneView frameScene{ m_config,
                                              m_launchOptions,
                                              m_startup,
                                              m_timers,
                                              *m_overlayDiagnostics,
                                              m_simulation,
                                              m_sceneController };
            RuntimeFramePresentationView framePresentation{ m_renderDefaults,
                                                            *m_validationHarness,
                                                            m_renderBackendView,
                                                            m_renderer };
            frameRenderDiagnostics.ResetFrameDrawCalls();

            PROFILE_BEGIN( m_profiler, "Frame/Input" );
#if defined( SKULLBONEZ_AUTOMATION_DIAGNOSTICS )
            const ReplayAutomationView automationReplayView = m_replayRuntime.BuildAutomationView();
            const ReplayInputView automationReplayInput = automationReplayView.input;
            const InteractionAutomationFrameResult automationBeforeInput =
                TickInteractionAutomationBeforeInput( m_interactionAutomation,
                                                      frameHost,
                                                      frameInteraction,
                                                      frameScene,
                                                      automationReplayView );
#if defined( SKULLBONEZ_DEVELOPMENT_TOOLS )
            for ( std::size_t commandIndex = 0u; commandIndex < automationBeforeInput.developmentUiCommandCount;
                  ++commandIndex )
            {
                const InteractionAutomationDevelopmentUiCommand& command =
                    automationBeforeInput.developmentUiCommands[commandIndex];
                SkullbonezCore::Core::SbResult commandStatus = SkullbonezCore::Core::SbResult::Success();
                switch ( command.type )
                {
                case InteractionAutomationDevelopmentUiCommandType::SelectSurface:
                    SelectDevelopmentUiSurface( std::strcmp( command.target, "imgui" ) == 0
                                                    ? DevelopmentUiMode::ImGui
                                                    : DevelopmentUiMode::Legacy );
                    break;
                case InteractionAutomationDevelopmentUiCommandType::SetPanelVisible:
                case InteractionAutomationDevelopmentUiCommandType::FocusPanel:
                {
                    DevelopmentTools::ImGuiEditorPanelId panel = DevelopmentTools::ImGuiEditorPanelId::Count;
                    if ( !DevelopmentTools::TryParseImGuiEditorPanel( command.target, panel ) )
                    {
                        commandStatus = SkullbonezCore::Core::SbResult::Failure(
                            "DevelopmentTools/ImGuiAutomation",
                            "Interaction script names an unknown ImGui panel: %s",
                            command.target );
                        break;
                    }
                    DevelopmentTools::ImGuiEditorAutomationCommand editorCommand;
                    editorCommand.type = command.type == InteractionAutomationDevelopmentUiCommandType::SetPanelVisible
                                             ? DevelopmentTools::ImGuiEditorAutomationCommandType::SetPanelVisible
                                             : DevelopmentTools::ImGuiEditorAutomationCommandType::FocusPanel;
                    editorCommand.panel = panel;
                    editorCommand.visible = command.boolValue;
                    commandStatus = m_imguiEditor.ApplyAutomationCommand( editorCommand );
                    break;
                }
                case InteractionAutomationDevelopmentUiCommandType::ResetLayout:
                {
                    DevelopmentTools::ImGuiEditorAutomationCommand editorCommand;
                    editorCommand.type = DevelopmentTools::ImGuiEditorAutomationCommandType::ResetLayout;
                    commandStatus = m_imguiEditor.ApplyAutomationCommand( editorCommand );
                    break;
                }
                case InteractionAutomationDevelopmentUiCommandType::SetDpiScale:
                {
                    DevelopmentTools::ImGuiEditorAutomationCommand editorCommand;
                    editorCommand.type = DevelopmentTools::ImGuiEditorAutomationCommandType::SetDpiScale;
                    editorCommand.dpiScale = command.numberValue;
                    commandStatus = m_imguiEditor.ApplyAutomationCommand( editorCommand );
                    break;
                }
                case InteractionAutomationDevelopmentUiCommandType::ResizeWindow:
                {
                    // Why: scripts describe client pixels because those are the
                    // editor's layout coordinates. Win32 resizes the outer frame,
                    // so include the current style and monitor DPI exactly once.
                    RECT outer{ 0, 0, command.width, command.height };
                    const HWND window = m_window.NativeWindowHandle();
                    const DWORD style = static_cast<DWORD>( GetWindowLongPtr( window, GWL_STYLE ) );
                    const DWORD extendedStyle = static_cast<DWORD>( GetWindowLongPtr( window, GWL_EXSTYLE ) );
                    const UINT dpi = GetDpiForWindow( window );
                    if ( !AdjustWindowRectExForDpi( &outer, style, FALSE, extendedStyle, dpi ) ||
                         !SetWindowPos( window,
                                        nullptr,
                                        0,
                                        0,
                                        outer.right - outer.left,
                                        outer.bottom - outer.top,
                                        SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE ) )
                    {
                        commandStatus = SkullbonezCore::Core::SbResult::Failure(
                            "DevelopmentTools/ImGuiAutomation",
                            "Failed to resize the automation client area to %dx%d",
                            command.width,
                            command.height );
                    }
                    break;
                }
                }
                if ( !commandStatus.ok )
                {
                    m_applicationExit.RequestOwnedFailure( commandStatus );
                    break;
                }
            }
#endif
            if ( automationBeforeInput.applyCameraMode )
            {
                m_inputRouter.ApplyCameraMode( automationBeforeInput.cameraMode,
                                               RuntimeInputActionSource::Runtime,
                                               frameInteraction,
                                               frameScene,
                                               m_replayRuntime,
                                               m_inputRouter.RuntimeContext() );
            }
            // Automation publishes replay mutations as a value packet. Apply
            // it once at the frame composition boundary before normal input
            // observes the resulting replay state.
            (void)m_replayRuntime.ApplyFrameIntent( automationBeforeInput.replayIntent );
            if ( automationBeforeInput.setWorldInteractionOwner )
            {
                m_inputRouter.SetWorldInteractionOwner(
                    automationBeforeInput.worldInteractionOwner,
                    automationBeforeInput.worldInteractionReason,
                    frameInteraction,
                    frameScene,
                    m_replayRuntime,
                    NormalizeRuntimeCameraMode(
                        automationReplayInput.restoreCameraMode,
                        m_sceneController.State().isSceneMode,
                        RuntimeCameraModeEnabledMask( m_sceneController.State().isSceneMode,
                                                      m_sceneController.Scene().SceneEntityCount() ) ) );
            }
            if ( !automationBeforeInput.status.ok )
            {
                m_applicationExit.RequestOwnedFailure( automationBeforeInput.status );
            }
            if ( automationBeforeInput.requestQuit )
            {
                PostQuitMessage( 0 );
            }
#endif
            UiInputCaptureIntent developmentUiCapture;
            SkullbonezCore::UI::OperatorEditorCommandQueues developmentEditorCommands;
            bool legacyDevelopmentUiActive = true;
#if defined( SKULLBONEZ_DEVELOPMENT_TOOLS )
            // Concept: native messages were already offered to ImGui while the
            // queue drained. The previous completed editor frame now supplies
            // class-specific capture intent to the single engine input sample.
            const DevelopmentTools::ImGuiEditorInputFrameState imguiInput = m_imguiEditor.ConsumeInputFrameState();
            developmentUiCapture = UiInputCaptureIntent{ imguiInput.capture.mouse,
                                                         imguiInput.capture.keyboard,
                                                         imguiInput.capture.text,
                                                         imguiInput.nativePointerStateTouched };
            developmentUiCapture.gameViewportMappingActive = imguiInput.gameViewport.valid;
            developmentUiCapture.gameViewportMinX = imguiInput.gameViewport.imageMinX;
            developmentUiCapture.gameViewportMinY = imguiInput.gameViewport.imageMinY;
            developmentUiCapture.gameViewportWidth = imguiInput.gameViewport.imageWidth;
            developmentUiCapture.gameViewportHeight = imguiInput.gameViewport.imageHeight;
            developmentUiCapture.gameViewportDpiScale = imguiInput.gameViewport.dpiScale;
            developmentUiCapture.gameViewportSourceWidth = imguiInput.gameViewport.sourceWidth;
            developmentUiCapture.gameViewportSourceHeight = imguiInput.gameViewport.sourceHeight;
            developmentEditorCommands = m_imguiEditor.ConsumeOperatorEditorCommands();
#if defined( SKULLBONEZ_AUTOMATION_DIAGNOSTICS )
            if ( automationBeforeInput.hasOperatorEditorReplayCommand )
            {
                const SkullbonezCore::Core::SbResult submitStatus =
                    UI::SubmitOperatorEditorCommand( developmentEditorCommands.replay,
                                                     automationBeforeInput.operatorEditorReplayCommand );
                if ( !submitStatus.ok )
                {
                    m_applicationExit.RequestOwnedFailure( submitStatus );
                }
            }
#endif
            legacyDevelopmentUiActive = m_imguiEditor.SelectedSurface() == DevelopmentUiMode::Legacy;
#endif
            ProcessInputFrame( frameHost,
                               frameInteraction,
                               frameScene,
                               framePresentation,
                               m_replayRuntime,
                               developmentUiCapture,
                               developmentEditorCommands,
                               legacyDevelopmentUiActive );
#if defined( SKULLBONEZ_DEVELOPMENT_TOOLS )
            if ( m_launchOptions.developmentUiModeExplicit || m_imguiEditor.HasActivatedSurfaceSelection() )
            {
                // Invariant: a scene load may apply scene-authored Legacy window
                // defaults during the input checkpoint. Reassert an explicit
                // process selection before either surface can begin its frame.
                SelectDevelopmentUiSurface( m_imguiEditor.SelectedSurface() );
            }
            bool legacySurfaceSwapRequested = false;
            if ( m_imguiEditor.SelectedSurface() == DevelopmentUiMode::Legacy &&
                 m_inputRouter.DeviceFrame().keys.IsDown( VK_CONTROL ) )
            {
                const InputActions& actions = m_inputRouter.Actions();
                for ( std::size_t actionIndex = 0u; actionIndex < actions.Count(); ++actionIndex )
                {
                    const InputActionEvent& action = actions[actionIndex];
                    legacySurfaceSwapRequested = action.action == RuntimeInputAction::ToggleUIVisibility &&
                                                 action.edge == InputActionEdge::Pressed;
                    if ( legacySurfaceSwapRequested )
                    {
                        break;
                    }
                }
            }
            if ( legacySurfaceSwapRequested )
            {
                // Plain 0 retains the Legacy minimize behavior. Ctrl+0 is the
                // explicit surface chord; selection hides Legacy before ImGui
                // begins a frame, so focus ownership never overlaps.
                SelectDevelopmentUiSurface( DevelopmentUiMode::ImGui );
            }
            // Invariant: ProcessInputFrame may consume Ctrl+0 after the first
            // input snapshot. Resample the selected presentation only after
            // every pre-render swap so this frame cannot draw Legacy replay
            // underneath a newly active ImGui frame.
            legacyDevelopmentUiActive = m_imguiEditor.SelectedSurface() == DevelopmentUiMode::Legacy;
#endif
            m_validationHarness->TickLiveStyle(
                SceneRuntimeStyleContext{ m_launchOptions,
                                          m_sceneController.State(),
                                          m_operatorUi->SceneNavigation().browser,
                                          m_sceneController.Scene(),
                                          m_assets,
                                          ActiveSceneCinematicConfig( m_sceneController.State(), m_config ),
                                          m_renderDefaults.CinematicBaseline() } );
            PROFILE_END( m_profiler, "Frame/Input" );

            m_sceneController.Scene().BeginCollisionVisualFrame();
            const std::string* captureScenePath = m_sceneController.CurrentPath();
            const RuntimeCaptureSceneContext captureContext{ m_sceneController.State().isSceneMode,
                                                             m_sceneController.State().isInteractiveRun,
                                                             m_sceneController.State().currentFrame,
                                                             m_timers.simulationTimer.GetTimeSinceLastStart() * 1000.0,
                                                             captureScenePath ? captureScenePath->c_str() : nullptr };
            // Invariant: decide capture determinism before physics/camera update.
            // The frame rendered for a scheduled screenshot must use exact
            // current solver poses even when live presentation interpolation is on.
            const bool capturePresentationPinned =
                m_diagnosticsRuntime.Capture().RequiresDeterministicPresentation( captureContext ) ||
                ( captureContext.isSceneMode && m_camera.autoCycleInterval > 0.0f ) ||
                m_validationHarness->HasPendingLiveStyleCapture()
#if defined( SKULLBONEZ_AUTOMATION_DIAGNOSTICS )
                || InteractionAutomationWillCaptureAfterRender( m_interactionAutomation,
                                                                m_sceneController.State().currentFrame )
#endif
                ;
            float simulationPresentationAlpha = 1.0f;
            {
                CoreAllocation::RuntimeAllocationScope allocationScope(
                    CoreAllocation::RuntimeAllocationPhase::Physics );
                simulationPresentationAlpha =
                    TickPhysics( secondsPerFrame, frameInteraction, frameScene, capturePresentationPinned );
            }

            {
                // Invariant: prediction scheduling completes before overlay
                // construction. Render consumes only the published future and
                // cannot decide whether the private engine advances.
                CoreAllocation::RuntimeAllocationScope allocationScope(
                    CoreAllocation::RuntimeAllocationPhase::Replay );
                m_replayRuntime.UpdatePrediction( m_sceneController.Scene().Physics(),
                                                  m_sceneController.Scene().Entities(),
                                                  m_config,
                                                  m_sceneController.Scene().Environment().GetPhysicsWorldForces(),
                                                  m_workerPool,
                                                  m_sceneController.State().isScenePhysics,
                                                  m_timers.simulationTimer.GetTimeSinceLastStart(),
                                                  m_timers.simulationTimer.GetTotalTime() );
            }

            m_overlayDiagnostics->UpdatePostPhysics( m_sceneController.Scene(),
                                                     *m_validationHarness,
                                                     m_config.bodySimulation.contactEpsilon,
                                                     secondsPerFrame );

            // Concept: graphics stress is render/runtime churn, not UI command
            // processing. Tick it once per rendered frame so headless and
            // overnight launches keep mutating DX12 state even when the UI
            // command panel is not producing control messages.
            m_validationHarness->ExecuteGraphicsStressFrame( frameHost,
                                                             frameInteraction,
                                                             frameScene,
                                                             framePresentation,
                                                             m_replayRuntime,
                                                             frameRenderDiagnostics,
                                                             legacyDevelopmentUiActive );
            const float presentationAlpha =
                ResolvePresentationAlpha( m_config, capturePresentationPinned, simulationPresentationAlpha );

            if ( m_renderer.PipelineSyncEnabled() )
            {
                PROFILE_BEGIN( m_profiler, "Frame/PipelineSync" );
                SkullbonezCore::Core::SbResult finishResult = SkullbonezCore::Core::SbResult::Success();
                {
                    CoreAllocation::RuntimeAllocationScope allocationScope(
                        CoreAllocation::RuntimeAllocationPhase::Render );
                    finishResult = renderLifecycle.Finish();
                }
                PROFILE_END( m_profiler, "Frame/PipelineSync" );
                if ( !finishResult.ok )
                {
                    m_timers.frameTimer.StopTimer();
                    PROFILE_FRAME_END( m_profiler );
                    m_applicationExit.RequestOwnedFailure( finishResult );
                    return m_applicationExit.Resolve( 0 );
                }
            }

            RuntimeRenderModelFrameView renderModels =
                m_renderer.BuildModelFrameView( m_sceneController.Scene(), m_workerPool, m_config );

            PROFILE_BEGIN( m_profiler, "Frame/Render" );
            {
                CoreAllocation::RuntimeAllocationScope allocationScope(
                    CoreAllocation::RuntimeAllocationPhase::Render );
                DRAW_CALL_TRACE_SCOPE( frameRenderDiagnostics, "Frame/Render" );
                Render( renderModels, presentationAlpha );
            }
            PROFILE_END( m_profiler, "Frame/Render" );

#if defined( SKULLBONEZ_DEVELOPMENT_TOOLS )
            // Invariant: copy the completed world backbuffer before either
            // operator surface draws. The persistent texture follows only the
            // swap-chain extent, so dock drags never recreate GPU resources.
            if ( m_imguiEditor.IsVisible() )
            {
                const SkullbonezCore::Core::SbResult viewportCapture = m_imguiEditor.CaptureGameViewport();
                if ( !viewportCapture.ok )
                {
                    m_timers.frameTimer.StopTimer();
                    PROFILE_FRAME_END( m_profiler );
                    m_applicationExit.RequestOwnedFailure( viewportCapture );
                    return m_applicationExit.Resolve( 0 );
                }
            }
#endif

            SkullbonezCore::UI::OperatorEditorFrameView operatorEditorView;
#if defined( SKULLBONEZ_DEVELOPMENT_TOOLS )
            operatorEditorView.surfaces.secondaryVisible = m_imguiEditor.IsVisible();
#endif
            const RuntimeUiTextFrameFacts uiTextFacts{
                RuntimeCameraModeEnabledMask( m_sceneController.State().isSceneMode,
                                              m_sceneController.Scene().SceneEntityCount() ),
                m_camera.mode == RunCameraMode::Attach ? m_attachedCamera.ModeLabel()
                                                       : RunCameraModeLabel( m_camera.mode ),
                m_runtimeTools.LauncherFireModeLabel(),
                RunCameraModeUsesLauncher( m_camera.mode ),
                m_interaction.Gesture(),
                presentationAlpha,
                capturePresentationPinned,
                secondsPerFrame,
                legacyDevelopmentUiActive,
                operatorEditorView };
            // Lifetime: replay publishes one immutable cause/scrubber view for
            // both the legacy late pass and the development editor. E14 reads
            // its rows directly instead of building a second causality tree.
            const ReplayOverlay::ReplayOverlayStateView replayOverlay =
                m_replayRuntime.BuildOverlayStateView( m_runtimeTools.Editor().editorModeEnabled,
                                                       m_operatorUi->IsVisible(),
                                                       m_operatorUi->IsMinimized(),
                                                       m_interaction.Gesture().kind,
                                                       renderModels.presentationRecords,
                                                       renderModels.bodyStore );
            RenderExecuteUiTextFrame( frameHost,
                                      frameInteraction,
                                      frameScene,
                                      framePresentation,
                                      m_replayRuntime,
                                      uiTextFacts,
                                      replayOverlay,
                                      frameRenderDiagnostics,
                                      uiRender,
                                      renderModels );

#if defined( SKULLBONEZ_DEVELOPMENT_TOOLS )
            // Concept: the context owner builds one typed editor frame, then
            // its narrow E6 renderer binding records draw data before Present.
            // Win32 message routing remains isolated to E7.
            const UINT windowDpi = GetDpiForWindow( m_window.NativeWindowHandle() );
            const float dpiScale = windowDpi > 0u ? static_cast<float>( windowDpi ) / 96.0f : 1.0f;
            const SkullbonezCore::Core::DevelopmentTools::TracyClientStatus tracyStatus =
                SkullbonezCore::Core::DevelopmentTools::TracyClientOwner::CopyStatus();
            const DevelopmentTools::ImGuiEditorFrameInput imguiFrameInput{ m_window.ClientWidth(),
                                                                           m_window.ClientHeight(),
                                                                           dpiScale,
                                                                           static_cast<float>( secondsPerFrame ),
                                                                           tracyStatus.initialized,
                                                                           tracyStatus.viewerConnected,
                                                                           tracyStatus.heavyMode };
            if ( m_imguiEditor.BeginFrame( imguiFrameInput ) )
            {
                m_imguiEditor.BuildEditorShell( operatorEditorView, replayOverlay );
                const DevelopmentTools::ImGuiEditorFrameResult imguiResult = m_imguiEditor.EndFrame();
                if ( !imguiResult.status.ok )
                {
                    m_timers.frameTimer.StopTimer();
                    PROFILE_FRAME_END( m_profiler );
                    m_applicationExit.RequestOwnedFailure( imguiResult.status );
                    return m_applicationExit.Resolve( 0 );
                }
                if ( imguiResult.commands.requestSurfaceSwap )
                {
                    SelectDevelopmentUiSurface( DevelopmentUiMode::Legacy );
                }
            }
#endif

            PROFILE_BEGIN( m_profiler, "Frame/PostDraw/LiveStyleCapture" );
            {
                CoreAllocation::RuntimeAllocationScope allocationScope(
                    CoreAllocation::RuntimeAllocationPhase::Capture );
                m_validationHarness->SavePendingLiveStyleCapture( m_diagnosticsRuntime.Capture(),
                                                                  m_renderBackendView.RequireCaptureBackend() );
            }
            PROFILE_END( m_profiler, "Frame/PostDraw/LiveStyleCapture" );

#if defined( SKULLBONEZ_AUTOMATION_DIAGNOSTICS )
            PROFILE_BEGIN( m_profiler, "Frame/PostDraw/InteractionAutomation" );
            InteractionAutomationDevelopmentUiView automationDevelopmentUiView;
#if defined( SKULLBONEZ_DEVELOPMENT_TOOLS )
            const DevelopmentTools::ImGuiEditorStatus imguiAutomationStatus = m_imguiEditor.CopyStatus();
            automationDevelopmentUiView.available = imguiAutomationStatus.initialized;
            automationDevelopmentUiView.selectedImGui = m_imguiEditor.SelectedSurface() == DevelopmentUiMode::ImGui;
            automationDevelopmentUiView.legacyVisible = m_operatorUi->IsVisible();
            automationDevelopmentUiView.imguiVisible = imguiAutomationStatus.visible;
            automationDevelopmentUiView.legacyReplayPresentationActive = uiTextFacts.legacyDevelopmentUiActive;
            automationDevelopmentUiView.panelVisibilityMask = imguiAutomationStatus.panelVisibilityMask;
            automationDevelopmentUiView.layoutResetCount = imguiAutomationStatus.layoutResetCount;
            automationDevelopmentUiView.automationFocusCount = imguiAutomationStatus.automationFocusCount;
            automationDevelopmentUiView.appliedDpiScale = imguiAutomationStatus.appliedDpiScale;
            automationDevelopmentUiView.rendererDescriptorHighWater = imguiAutomationStatus.rendererDescriptorHighWater;
            automationDevelopmentUiView.gameViewportRecreations = imguiAutomationStatus.gameViewportRecreations;
            automationDevelopmentUiView.preferencesRecovered = imguiAutomationStatus.preferencesRecovered;
#endif
            const InteractionAutomationFrameResult automationAfterRender =
                TickInteractionAutomationAfterRender( m_interactionAutomation,
                                                      frameInteraction,
                                                      frameScene,
                                                      m_replayRuntime.BuildAutomationView(),
                                                      automationDevelopmentUiView,
                                                      m_diagnosticsRuntime.Capture(),
                                                      m_renderBackendView.RequireCaptureBackend() );
            if ( !automationAfterRender.status.ok )
            {
                m_applicationExit.RequestOwnedFailure( automationAfterRender.status );
            }
            if ( automationAfterRender.requestQuit )
            {
                PostQuitMessage( 0 );
            }
            PROFILE_END( m_profiler, "Frame/PostDraw/InteractionAutomation" );
#endif

            {
                CoreAllocation::RuntimeAllocationScope allocationScope(
                    CoreAllocation::RuntimeAllocationPhase::Capture );
                if ( TickScreenshots() )
                {
                    continue;
                }
            }

            PROFILE_BEGIN( m_profiler, "Frame/PostDraw/AutoCycle" );
            TickAutoCycle();
            PROFILE_END( m_profiler, "Frame/PostDraw/AutoCycle" );

            m_timers.workTimer.StopTimer();
            m_timers.cpuFrameWorkMs =
                static_cast<float>( std::clamp( m_timers.workTimer.GetElapsedTime(), 0.0, 0.25 ) * 1000.0 );

            PROFILE_BEGIN( m_profiler, "Frame/VsyncWait" );
            SkullbonezCore::Core::SbResult presentResult = SkullbonezCore::Core::SbResult::Success();
            {
                CoreAllocation::RuntimeAllocationScope allocationScope(
                    CoreAllocation::RuntimeAllocationPhase::Render );
                presentResult = renderLifecycle.Present();
            }
            PROFILE_END( m_profiler, "Frame/VsyncWait" );
            if ( !presentResult.ok )
            {
                m_timers.frameTimer.StopTimer();
                PROFILE_FRAME_END( m_profiler );
                m_applicationExit.RequestOwnedFailure( presentResult );
                return m_applicationExit.Resolve( 0 );
            }

            // Invariant: Tracy counts submitted game frames, not attempted
            // render turns, capture-only continues, or failed Presents.
            SKORE_TRACY_MARK_SUBMITTED_FRAME();

            m_timers.frameTimer.StopTimer();
            PROFILE_FRAME_END( m_profiler );

#if defined( SKULLBONEZ_PROFILE_ENABLED )
            {
                const RuntimeProfilerFrameTimes profilerTimes = m_diagnosticsRuntime.SampleProfilerFrameTimes();
                m_timers.physicsTime = profilerTimes.physicsTimeSeconds;
                m_timers.renderTime = profilerTimes.renderTimeSeconds;
                m_timers.gpuFrameWorkMs = profilerTimes.gpuFrameWorkMs;
            }
#endif

            m_diagnosticsRuntime.TickPerfLog( RuntimePerfTickContext{ m_sceneController.PerfPass() + 1,
                                                                      m_sceneController.State().currentFrame + 1,
                                                                      m_timers.physicsTime,
                                                                      m_timers.renderTime } );

            if ( TickSceneAdvance() )
            {
                continue;
            }
        }
    }
    return m_applicationExit.Resolve( messageExitCode );
}


float Run::TickPhysics( double secondsPerFrame,
                        RuntimeFrameInteractionView& interactionOwners,
                        RuntimeFrameSceneView& sceneOwners,
                        bool capturePresentationPinned )
{
    const ReplayInputView replayInput = m_replayRuntime.BuildInputView();
    if ( replayInput.scrubPaused )
    {
        PROFILE_SCOPED( m_profiler, "Frame/Replay/ScrubCamera" );
        UpdateLogic( 0.0f, static_cast<float>( secondsPerFrame ), 1.0f );
        return 1.0f;
    }

    const bool replayLiveAdvanceHeld = replayInput.liveAdvanceHeld;
    const RuntimeInputSnapshot& inputSnapshot = m_inputRouter.RuntimeSnapshot();
    const bool stepRequested = inputSnapshot.frameInput.stepHeld;
    const bool replayCapture = replayInput.captureEnabled;
#ifdef _DEBUG
    const bool physicsCapture = m_diagnosticsRuntime.PerfLog().physicsRegressionLogOverride[0] != '\0' ||
                                m_diagnosticsRuntime.PerfLog().physicsCollisionTimeLogOverride[0] != '\0' ||
                                m_diagnosticsRuntime.PhysicsDiagnostics().isEnabled;
#else
    constexpr bool physicsCapture = false;
#endif
    RuntimeInteractionFramePolicy policy = m_interaction.BuildFramePolicy(
        RuntimeInteractionFrameInput{ m_sceneController.State().isScenePhysics,
                                      stepRequested,
                                      false,
                                      replayLiveAdvanceHeld,
                                      inputSnapshot.pointer.rightDown,
                                      m_runtimeTools.Editor().viewportLookActive,
                                      inputSnapshot.frameInput.replayInspectionLookActive,
                                      physicsCapture,
                                      m_sceneController.State().timeScale } );
    if ( m_sceneController.CrossScenePauseLocked() )
    {
        // Invariant: the P-key pause lock outranks camera/tool mode. Launcher
        // and passive scene cameras normally keep physics running, but the lock
        // requires Space before any simulation step can proceed.
        policy.physicsAdvance = PhysicsAdvanceState::RunWhileStepHeld;
        if ( !stepRequested )
        {
            policy.physicsTimeScale = 0.0f;
        }
    }
    const bool manipulatorPhysics = policy.manipulatorActive;
    const auto physicsWorldForces = m_sceneController.Scene().Environment().GetPhysicsWorldForces();
    constexpr bool canStepPhysics = true;
    const SimulationTickResult tick = m_simulation.Tick( SimulationTickInput{ secondsPerFrame,
                                                                              policy.physicsTimeScale,
                                                                              m_sceneController.State().isSceneMode,
                                                                              m_sceneController.State().isScenePhysics,
                                                                              m_sceneController.State().isFixedStep,
                                                                              policy.physicsAdvance,
                                                                              stepRequested,
                                                                              canStepPhysics } );
    const float presentationAlpha =
        ResolvePresentationAlpha( m_config, capturePresentationPinned, tick.presentationAlpha );
    if ( tick.committedPhysicsTicks > 0 && canStepPhysics )
    {
        PROFILE_BEGIN( m_profiler, "Frame/Physics" );
        // Why: SimulationSystem now returns only a deterministic tick count.
        // Runtime executes the store-owned physics step directly, then applies
        // the remaining model-owned presentation sync as explicit edge work.
        for ( int tickIndex = 0; tickIndex < tick.committedPhysicsTicks; ++tickIndex )
        {
            PROFILE_SCOPED( m_profiler, "Frame/Physics/Step" );
            {
                PROFILE_SCOPED( m_profiler, "Frame/Physics/Step/PresentationCaptureBegin" );
                m_sceneController.Scene().BeginPhysicsStepPresentationCapture();
            }
            if ( manipulatorPhysics )
            {
                m_runtimeTools.ApplyMousePickupPhysicsStep( m_sceneController.Scene(), m_inputRouter, m_interaction );
            }

            SkullbonezCore::Rendering::RenderInstanceStore& contactPresentation =
                m_sceneController.Scene().MutableRenderInstances();
            contactPresentation.TickContactFeedback( m_sceneController.Scene().SceneEntityCount(), PHYSICS_FIXED_DT );
            const ScenePhysicsPostStepOutput postStep =
                m_sceneController.Scene().StepPhysics( PHYSICS_FIXED_DT, physicsWorldForces, m_workerPool );
            // The physics owner publishes a bounded span; the presentation owner
            // consumes it before the next step can replace those dense-row facts.
            for ( int modelIndex : postStep.fixedContactModelIndices )
            {
                contactPresentation.NotifyFixedContact( modelIndex, 0.5f );
            }
            {
                PROFILE_SCOPED( m_profiler, "Frame/Physics/Step/PresentationCaptureComplete" );
                m_sceneController.Scene().CompletePhysicsStepPresentationCapture();
            }

            if ( manipulatorPhysics || replayCapture )
            {
                AfterPhysicsStep( interactionOwners, sceneOwners );
            }
        }
        PROFILE_END( m_profiler, "Frame/Physics" );
    }
    m_runtimeTools.TickRayCastTestLines( static_cast<float>( secondsPerFrame ) );
    m_runtimeTools.Laser().Update( static_cast<float>( secondsPerFrame ) );
    if ( tick.shouldUpdateLogic )
    {
        UpdateLogic( tick.simulationDt, tick.cameraDt, presentationAlpha );
    }
    else
    {
        // Why: Scene-mode, no-physics harnesses intentionally skip simulation
        // UpdateLogic, but Director is presentation state. It still needs phase
        // style/camera entry work so authored show decks behave in static scenes.
        const ReplayInputView directorReplayInput = m_replayRuntime.BuildInputView();
        DemoDirectorPredictionView directorPrediction;
        directorPrediction.revealAvailable = directorReplayInput.predictionRevealAvailable;
        directorPrediction.revealProgress = directorReplayInput.predictionRevealProgress;
        const DemoDirectorTickResult directorResult = DemoDirectorPlayback::Tick(
            m_camera,
            directorPrediction,
            SceneRuntimeStyleContext{ m_launchOptions,
                                      m_sceneController.State(),
                                      m_operatorUi->SceneNavigation().browser,
                                      m_sceneController.Scene(),
                                      m_assets,
                                      ActiveSceneCinematicConfig( m_sceneController.State(), m_config ),
                                      m_renderDefaults.CinematicBaseline() },
            static_cast<float>( secondsPerFrame ) );
        if ( directorResult.applyRevealRate )
        {
            ReplayFrameIntent intent;
            intent.applyPredictionRevealRate = true;
            intent.predictionRevealRate = directorResult.requestedRevealRate;
            (void)m_replayRuntime.ApplyFrameIntent( intent );
        }
    }
    return tick.presentationAlpha;
}


void Run::AfterPhysicsStep( RuntimeFrameInteractionView& interactionOwners, RuntimeFrameSceneView& sceneOwners )
{
    m_runtimeTools.RestoreMousePickupAngularVelocity( m_sceneController.Scene(), m_inputRouter, m_interaction );
    const bool replayCaptured = m_replayRuntime.BuildInputView().captureEnabled;
    if ( replayCaptured )
    {
        CaptureReplayPostStep( interactionOwners, sceneOwners, m_replayRuntime, m_profiler );
    }
#ifdef _DEBUG
    if ( replayCaptured )
    {
        RuntimeOverlayPresentationEdit presentationEdit = m_overlayDiagnostics->EditPresentation();
        const ReplaySceneTimelineResetInput timelineReset =
            DescribeReplaySceneTimeline( m_sceneController,
                                         m_operatorUi->SceneNavigation().overrides,
                                         m_sceneController.State(),
                                         SkullbonezCore::Core::ActiveSceneObjectCapacity( m_config ),
                                         static_cast<uint32_t>( m_launchOptions.generatedObjectTypeOverride ) );
        ReplaySolverSampleRestoreContext probeSample{ m_sceneController.Scene(),
                                                      m_sceneController.State(),
                                                      m_renderer,
                                                      presentationEdit.State(),
                                                      m_runtimeTools };
        const ReplaySceneTimelineResetOwners timelineOwners{
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
        const ReplayRestoreTransaction probeTransaction{ probeSample,
                                                         m_diagnosticsRuntime,
                                                         timelineReset,
                                                         timelineOwners };
        const ReplayArtifactTopologyOwners probeTopology{ m_simulation,
                                                          m_config,
                                                          m_assets,
                                                          m_workerPool,
                                                          m_operatorUi->SceneNavigation().overrides,
                                                          m_launchOptions.generatedObjectTypeOverride,
                                                          SkullbonezCore::Core::ActiveSceneObjectCapacity( m_config ) };
        // Why: ReplayRuntime owns probe sequencing and bounded failure state;
        // the application exit latch only preserves that first owned failure
        // while WM_QUIT unwinds the frame loop.
        const ReplayProbeTickResult probeResult = m_replayRuntime.TickProbes( probeTransaction, probeTopology );
        if ( !probeResult.status.ok )
        {
            m_applicationExit.RequestOwnedFailure( probeResult.status );
            PostQuitMessage( 0 );
            return;
        }
        if ( probeResult.resetCurrentScene )
        {
            m_sceneController.SubmitResetCurrentScene();
        }
        if ( probeResult.enterInteractive )
        {
            m_sceneController.EnterInteractiveRun();
            m_diagnosticsRuntime.Capture().DisableAutomationExit();
        }
    }
#endif
}


bool Run::TickScreenshots()
{
    PROFILE_BEGIN( m_profiler, "Frame/PostDraw/Screenshots" );
    if ( m_sceneController.CrossScenePauseLocked() && !m_inputRouter.RuntimeSnapshot().frameInput.stepHeld )
    {
        PROFILE_END( m_profiler, "Frame/PostDraw/Screenshots" );
        return false;
    }

    const std::string* scenePath = m_sceneController.CurrentPath();
    const RuntimeCaptureResult result = m_diagnosticsRuntime.Capture().TickScreenshots(
        RuntimeCaptureSceneContext{ m_sceneController.State().isSceneMode,
                                    m_sceneController.State().isInteractiveRun,
                                    m_sceneController.State().currentFrame,
                                    m_timers.simulationTimer.GetTimeSinceLastStart() * 1000.0,
                                    scenePath ? scenePath->c_str() : nullptr },
        m_renderBackendView.RequireCaptureBackend() );

    PROFILE_END( m_profiler, "Frame/PostDraw/Screenshots" );

    if ( !result.captureResult.ok )
    {
        // Lane R: capture readback/file IO failed after rendering, so terminate
        // automation with diagnostics instead of marking the scene complete.
        fprintf( stderr, "%s: %s\n", result.captureResult.error.owner, result.captureResult.error.message );
        fflush( stderr );
        PrintRuntimeExitReason( "Exiting because screenshot capture failed." );
        m_applicationExit.RequestOwnedFailure( result.captureResult );
        PostQuitMessage( 1 );
        return false;
    }

    if ( result.restartFrame )
    {
        PROFILE_FRAME_END( m_profiler );
    }

#ifdef _DEBUG
    if ( result.completion == RuntimeCaptureCompletion::ScreenshotAndExit )
    {
        m_diagnosticsRuntime.LogSceneFinished( m_sceneController,
                                               m_renderBackendView.renderDiagnostics,
                                               "screenshot_and_exit" );
    }
    else if ( result.completion == RuntimeCaptureCompletion::Screenshot )
    {
        m_diagnosticsRuntime.LogSceneFinished( m_sceneController, m_renderBackendView.renderDiagnostics, "screenshot" );
    }
#endif

    switch ( result.automation )
    {
    case RuntimeCaptureAutomation::Quit:
        if ( result.completion == RuntimeCaptureCompletion::ScreenshotAndExit )
        {
            PrintRuntimeExitReason( "Exiting because screenshot-and-exit capture completed." );
        }
        else if ( result.completion == RuntimeCaptureCompletion::AutoCycle )
        {
            PrintRuntimeExitReason( "Exiting because auto-cycle screenshot capture completed." );
        }
        PostQuitMessage( 0 );
        break;
    case RuntimeCaptureAutomation::AdvanceSceneOrQuit:
    {
        const SceneLoadRequest request = m_sceneController.AdvanceScene( m_diagnosticsRuntime.PerfTestActive(),
                                                                         m_sceneController.State().isInteractiveRun );
        bool advanced = false;
        if ( request.HasLoad() )
        {
            SceneLoadConsumerOutputs sceneLoadOutputs;
            advanced = m_sceneController
                           .Load( request,
                                  SceneLoadPolicyInputs{ m_config,
                                                         m_launchOptions,
                                                         m_renderDefaults.CinematicBaseline(),
                                                         m_startup,
                                                         m_assets,
                                                         m_workerPool },
                                  SceneLoadHostParticipants{ m_timers, m_diagnosticsRuntime, m_simulation },
                                  SceneLoadInteractionParticipants{
                                      m_inputRouter,
                                      m_interaction,
                                      m_camera,
                                      m_attachedCamera.State(),
                                      m_runtimeTools,
                                      CaptureSceneLoadNavigationState( m_operatorUi->SceneNavigation() ) },
                                  SceneLoadPresentationParticipants{ m_replayRuntime,
                                                                     *m_overlayDiagnostics,
                                                                     m_renderBackendView,
                                                                     m_renderer },
                                  sceneLoadOutputs )
                           .ok;
            ApplySceneLoadConsumerOutputs( sceneLoadOutputs,
                                           m_window,
                                           *m_operatorUi,
                                           *m_validationHarness,
                                           m_launchOptions );
        }
        if ( !advanced )
        {
            if ( result.completion == RuntimeCaptureCompletion::Screenshot )
            {
                PrintRuntimeExitReason(
                    "Exiting because scene screenshot capture completed and no next scene is queued." );
            }
            PostQuitMessage( 0 );
        }
        break;
    }
    case RuntimeCaptureAutomation::HoldInteractive:
        m_sceneController.MarkInteractiveRunComplete();
        m_diagnosticsRuntime.Capture().DisableAutomationExit();
        m_camera.StopAutoCycle();
        break;
    case RuntimeCaptureAutomation::None:
        break;
    }

    return result.restartFrame;
}


void Run::TickAutoCycle()
{
    if ( m_sceneController.CrossScenePauseLocked() && !m_inputRouter.RuntimeSnapshot().frameInput.stepHeld )
    {
        return;
    }

    const RuntimeCaptureResult result =
        m_diagnosticsRuntime.Capture().TickAutoCycle( m_sceneController.State().isSceneMode,
                                                      m_sceneController.State().isInteractiveRun,
                                                      m_sceneController.Scene().SceneEntityCount(),
                                                      m_camera.autoCycleInterval,
                                                      m_camera.autoCycleAccum,
                                                      m_camera.autoCycleShotsTaken,
                                                      m_camera.trackBallRow.value,
                                                      m_renderBackendView.RequireCaptureBackend() );

    if ( !result.captureResult.ok )
    {
        // Lane R: auto-cycle captures are validation side effects; failed file
        // output exits the run rather than recording a false capture success.
        fprintf( stderr, "%s: %s\n", result.captureResult.error.owner, result.captureResult.error.message );
        fflush( stderr );
        PrintRuntimeExitReason( "Exiting because auto-cycle screenshot capture failed." );
        m_applicationExit.RequestOwnedFailure( result.captureResult );
        PostQuitMessage( 1 );
        return;
    }

    if ( result.completion != RuntimeCaptureCompletion::AutoCycle )
    {
        return;
    }

#ifdef _DEBUG
    m_diagnosticsRuntime.LogSceneFinished( m_sceneController, m_renderBackendView.renderDiagnostics, "auto_cycle" );
#endif

    if ( result.automation == RuntimeCaptureAutomation::Quit )
    {
        PostQuitMessage( 0 );
    }
    else if ( result.automation == RuntimeCaptureAutomation::HoldInteractive )
    {
        m_sceneController.MarkInteractiveRunComplete();
        m_diagnosticsRuntime.Capture().DisableAutomationExit();
        m_camera.StopAutoCycle();
    }
}


bool Run::TickSceneAdvance()
{
    const bool sceneProceedAllowed =
        !m_sceneController.CrossScenePauseLocked() || m_inputRouter.RuntimeSnapshot().frameInput.stepHeld;
    const SceneAutomationGateStatus automationGateStatus = m_validationHarness->SceneGates().Status();
    const SceneFrameAdvanceResult result =
        m_sceneController.AdvanceFrame( automationGateStatus,
                                        sceneProceedAllowed,
                                        m_diagnosticsRuntime.PerfTestActive(),
                                        m_diagnosticsRuntime.Capture().Screenshot().isScreenshotSaved,
                                        RunCameraModeUsesManualControls( m_camera.mode,
                                                                         m_attachedCamera.State().activeFollow,
                                                                         m_camera.director.grabbed ),
                                        m_timers.simulationTimer.GetTimeSinceLastStart() );
    if ( result.reportMissingRequirements )
    {
        m_validationHarness->SceneGates().PrintMissingRequirements();
    }
#ifdef _DEBUG
    if ( result.finishReason )
    {
        m_diagnosticsRuntime.LogSceneFinished( m_sceneController,
                                               m_renderBackendView.renderDiagnostics,
                                               result.finishReason );
    }
#endif
    if ( result.holdInteractive )
    {
        m_diagnosticsRuntime.Capture().DisableAutomationExit();
        m_camera.StopAutoCycle();
    }

    bool loadSucceeded = true;
    if ( result.loadRequest.HasLoad() )
    {
        SceneLoadConsumerOutputs sceneLoadOutputs;
        loadSucceeded = m_sceneController
                            .Load( result.loadRequest,
                                   SceneLoadPolicyInputs{ m_config,
                                                          m_launchOptions,
                                                          m_renderDefaults.CinematicBaseline(),
                                                          m_startup,
                                                          m_assets,
                                                          m_workerPool },
                                   SceneLoadHostParticipants{ m_timers, m_diagnosticsRuntime, m_simulation },
                                   SceneLoadInteractionParticipants{
                                       m_inputRouter,
                                       m_interaction,
                                       m_camera,
                                       m_attachedCamera.State(),
                                       m_runtimeTools,
                                       CaptureSceneLoadNavigationState( m_operatorUi->SceneNavigation() ) },
                                   SceneLoadPresentationParticipants{ m_replayRuntime,
                                                                      *m_overlayDiagnostics,
                                                                      m_renderBackendView,
                                                                      m_renderer },
                                   sceneLoadOutputs )
                            .ok;
        ApplySceneLoadConsumerOutputs( sceneLoadOutputs,
                                       m_window,
                                       *m_operatorUi,
                                       *m_validationHarness,
                                       m_launchOptions );
    }
    if ( loadSucceeded && result.restartSimulationTimerAfterLoad )
    {
        m_timers.simulationTimer.StartTimer();
    }
    if ( result.requestQuit || ( !loadSucceeded && result.quitIfLoadFails ) )
    {
        PostQuitMessage( 0 );
    }
    if ( !loadSucceeded && !result.quitIfLoadFails )
    {
        return false;
    }
    return result.restartFrame;
}


void Run::UpdateLogic( float simulationDt, float cameraDt, float presentationAlpha )
{
    m_camera.AdvanceAutoCycleClock( m_sceneController.State().isSceneMode, simulationDt );
    m_camera.TickControls( m_sceneController.Scene(),
                           m_attachedCamera,
                           m_config,
                           m_runtimeTools.Editor().editorModeEnabled,
                           m_runtimeTools.Editor().viewportLookActive,
                           m_sceneController.State().isSceneMode,
                           cameraDt,
                           presentationAlpha );
    DemoDirectorPredictionView directorPrediction;
    const ReplayInputView replayInput = m_replayRuntime.BuildInputView();
    directorPrediction.revealAvailable = replayInput.predictionRevealAvailable;
    directorPrediction.revealProgress = replayInput.predictionRevealProgress;
    const DemoDirectorTickResult directorResult = DemoDirectorPlayback::Tick(
        m_camera,
        directorPrediction,
        SceneRuntimeStyleContext{ m_launchOptions,
                                  m_sceneController.State(),
                                  m_operatorUi->SceneNavigation().browser,
                                  m_sceneController.Scene(),
                                  m_assets,
                                  ActiveSceneCinematicConfig( m_sceneController.State(), m_config ),
                                  m_renderDefaults.CinematicBaseline() },
        cameraDt );
    if ( directorResult.applyRevealRate )
    {
        ReplayFrameIntent intent;
        intent.applyPredictionRevealRate = true;
        intent.predictionRevealRate = directorResult.requestedRevealRate;
        (void)m_replayRuntime.ApplyFrameIntent( intent );
    }

    m_sceneController.Scene().Environment().ApplyFluidSurfaceAdjustment(
        m_inputRouter.RuntimeSnapshot().fluidSurfaceAdjustment,
        simulationDt );
}
