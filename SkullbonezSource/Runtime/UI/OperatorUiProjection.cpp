/*
File: SkullbonezSource/Runtime/UI/OperatorUiProjection.cpp
Purpose:
  Projects detached domain facts into one operator UI frame.

Summary:
  App samples concrete owners, then Runtime/UI alone maps those bounded facts
  into editor and GameUI rows. Neither this file nor Render can reopen Scene,
  Diagnostics, Input, Camera, or tool authority while composing the frame.

Invariants:
  - Projection is CPU-only and completes before UI submission begins.
  - Reserve-capacity rows borrow caller storage through the synchronous draw.
  - Profile data comes from the startup-bound profiler selected by App.

Related:
  - SkullbonezSource/Runtime/UI/OperatorUiProjection.h
  - SkullbonezSource/Runtime/App/OperatorEditorFramePhase.cpp
  - SkullbonezSource/Runtime/Render/UiTextPass.cpp
*/
#include "OperatorUiProjection.h"

#include "../RuntimeFrameViews.h"
#include "RenderDiagnosticsProjection.h"
#include "RuntimeViewModel.h"
#include "../../Core/Allocation/RuntimeReserveAllocator.h"
#include "../../Core/Profiler.h"
#include "GameUI/UI.h"
#include "GameUI/UIFrameComposition.h"

#include <algorithm>

namespace SkullbonezCore
{
namespace Runtime
{
namespace
{
SkullbonezCore::UI::OperatorEditorForecastCause MapForecastCause( OperatorUiForecastCause cause ) noexcept
{
    using Cause = OperatorUiForecastCause;
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

void ProjectOperatorEditorScene( UI::OperatorEditorFrameView& view, const OperatorUiSceneFacts& facts )
{
    view.scene = { facts.currentScenePath ? facts.currentScenePath : "",
                   facts.sceneOptions,
                   facts.currentSceneBrowserIndex,
                   facts.sceneOptionCount,
                   facts.runtime.frame,
                   facts.entityCount,
                   facts.runtime.timeScale,
                   facts.currentScenePath && facts.currentScenePath[0] != '\0',
                   false };
    view.property = { facts.worldGravity, facts.worldFluidHeight, facts.worldFluidDensity };
}

void ProjectOperatorEditorRendering( UI::OperatorEditorFrameView& view, const OperatorUiRenderingFacts& facts )
{
    UI::OperatorEditorRenderingView& rendering = view.rendering;
    rendering.vsyncEnabled = facts.vsyncEnabled;
    rendering.shadowsEnabled = facts.shadowsEnabled;
    rendering.cinematicRendering = facts.cinematicRendering;
    rendering.presentationInterpolation = facts.presentationInterpolation;
    rendering.presentationAlpha = facts.uiText.presentationAlpha;
    rendering.terrainHidden = facts.terrainHidden;
    rendering.waterHidden = facts.waterHidden;
    rendering.waterFrozen = facts.waterFrozen;
    rendering.waterFlat = facts.waterFlat;
    rendering.waterReflectionMode = facts.waterNoReflect ? 2 : ( facts.waterRtReflect ? 1 : 0 );
    FillOperatorRenderingParameters( rendering, facts.ordinary, facts.cinematic );

    const char* gizmoModeLabel = "translate";

    switch ( facts.gizmoMode )
    {
    case OperatorUiGizmoMode::Rotate:
        gizmoModeLabel = "rotate";
        break;
    case OperatorUiGizmoMode::Scale:
        gizmoModeLabel = "scale";
        break;
    case OperatorUiGizmoMode::Translate:
    default:
        break;
    }

    view.viewport = { facts.uiText.cameraModeLabel, gizmoModeLabel, facts.uiText.presentationPinned };
}

void ProjectOperatorEditorForecast( UI::OperatorEditorFrameView& view, const OperatorUiForecastFacts& facts )
{
    UI::OperatorEditorForecastView& target = view.forecast;
    target.simulatedSeconds = facts.simulatedSeconds;
    target.simulatedSecondsPerRealSecond = facts.simulatedSecondsPerRealSecond;
    target.rollingWindowAgeSeconds = facts.rollingWindowAgeSeconds;
    target.energyDrift = facts.energyDrift;
    target.angularMomentumDrift = facts.angularMomentumDrift;
    target.maximumAbsoluteEnergyDrift = facts.maximumAbsoluteEnergyDrift;
    target.maximumAngularMomentumDrift = facts.maximumAngularMomentumDrift;
    target.firstFailureSeconds = facts.firstFailureSeconds;
    target.newestAbsoluteTick = facts.newestAbsoluteTick;
    target.retainedBytes = facts.retainedBytes;
    target.firstFailureSubject = facts.firstFailureSubject;
    target.firstFailureOther = facts.firstFailureOther;
    target.firstFailureCause = MapForecastCause( facts.firstFailureCause );
    target.available = facts.available;
    target.active = facts.active;
    target.workerInFlight = facts.workerInFlight;
    target.failed = facts.failed;
    target.configured = facts.configured;
    target.numericalHealthy = facts.numericalHealthy;
    target.systemOrbitalHealthy = facts.systemOrbitalHealthy;
    target.auxiliaryOrbitalHealthy = facts.auxiliaryOrbitalHealthy;
    target.energyDriftAvailable = facts.energyDriftAvailable;
    target.angularMomentumDriftAvailable = facts.angularMomentumDriftAvailable;
}

void ProjectOperatorEditorLookLab( UI::OperatorEditorFrameView& view, const UI::OperatorEditorLookLabView& lookLab )
{
    view.lookLab = lookLab;
}

void ProjectOperatorEditorReplay( UI::OperatorEditorFrameView& view, int memoryPreset, int requestedRetentionSeconds,
                                  int requestedBudgetMiB, int presentationRetentionSeconds, int solverRetentionSeconds,
                                  bool memoryBudgetClamped, bool solverWindowReduced )
{
    view.replay = { memoryPreset,           requestedRetentionSeconds, requestedBudgetMiB, presentationRetentionSeconds,
                    solverRetentionSeconds, memoryBudgetClamped,       solverWindowReduced };
}

void ProjectOperatorEditorSurfaces( UI::OperatorEditorFrameView& view, bool primaryVisible, bool secondaryVisible )
{
    view.surfaces = { primaryVisible, secondaryVisible };
}

#if defined( SKULLBONEZ_DEVELOPMENT_TOOLS )
void ProjectOperatorEditorInspectorAndWorld( UI::OperatorEditorFrameView& view, const OperatorUiInspectorFacts& inspector,
                                             const OperatorUiWorldFacts& world )
{
    UI::OperatorEditorInspectorView& target = view.inspector;
    target.displayName = inspector.displayName;
    target.renderMaterialName = inspector.renderMaterialName;
    target.contactMaterialName = inspector.contactMaterialName;
    target.assetName = inspector.assetName;
    target.assetInstanceName = inspector.assetInstanceName;
    target.assetPartName = inspector.assetPartName;
    target.selectionState = inspector.selectionState;
    target.sceneObjectId = inspector.sceneObjectId;
    target.selectionCount = inspector.selectionCount;
    target.renderMaterialKind = inspector.renderMaterialKind;
    target.colliderShapeKind = inspector.colliderShapeKind;
    target.behaviorGroupKind = inspector.behaviorGroupKind;
    target.behaviorPartIndex = inspector.behaviorPartIndex;

    for ( int channel = 0; channel < 3; ++channel )
    {
        target.position[channel] = inspector.position[channel];
        target.linearVelocity[channel] = inspector.linearVelocity[channel];
        target.angularVelocity[channel] = inspector.angularVelocity[channel];
    }

    for ( int channel = 0; channel < 4; ++channel )
    {
        target.orientation[channel] = inspector.orientation[channel];
        target.baseColor[channel] = inspector.baseColor[channel];
    }

    target.mass = inspector.mass;
    target.volume = inspector.volume;
    target.boundingRadius = inspector.boundingRadius;
    target.dragCoefficient = inspector.dragCoefficient;
    target.friction = inspector.friction;
    target.restitution = inspector.restitution;
    target.roughness = inspector.roughness;
    target.metallic = inspector.metallic;
    target.specular = inspector.specular;
    target.visible = inspector.visible;
    target.locked = inspector.locked;
    target.fixed = inspector.fixed;
    target.sleeping = inspector.sleeping;
    target.assetBacked = inspector.assetBacked;

    view.world = { world.modelCount,
                   world.modelCapacity,
                   world.solverBallCount,
                   world.solverBoxCount,
                   world.rngSeed,
                   world.timeScale,
                   world.gravity,
                   world.fluidHeight,
                   world.fluidDensity,
                   world.terrainFriction,
                   world.objectFriction,
                   world.rollingFriction,
                   world.tornadoRadius,
                   world.tornadoHeight,
                   world.tornadoInward,
                   world.tornadoSwirl,
                   world.tornadoLift,
                   world.fixedStep,
                   world.physicsSleepEnabled,
                   world.tornadoEnabled };
}
#endif

void ProjectOperatorEditorDiagnostics( UI::OperatorEditorFrameView& view, const OperatorUiSecondaryDiagnosticsFacts& facts )
{
    UI::OperatorEditorDiagnosticsView& target = view.diagnostics;
    target.rendererName = facts.rendererName;
    target.physicsPipelineStageName = facts.physicsPipelineStageName;
    target.renderTargetCount = (std::clamp)( facts.renderTargetCount, 0, UI::OPERATOR_EDITOR_RENDER_TARGET_CAPACITY );
    target.drawCalls = facts.drawCalls;
    target.uiDrawCalls = facts.uiDrawCalls;
    target.workerThreadCount = facts.workerThreadCount;
    target.maxWorkerThreadCount = facts.maxWorkerThreadCount;
    target.physicsPipelineStageIndex = facts.physicsPipelineStageIndex;
    target.physicsPipelineStageCount = facts.physicsPipelineStageCount;
    target.fps = facts.fps;
    target.renderMs = facts.renderMs;
    target.physicsMs = facts.physicsMs;
    target.cpuFrameMs = facts.cpuFrameMs;
    target.gpuFrameMs = facts.gpuFrameMs;
    target.physicsDebugAlpha = facts.physicsDebugAlpha;
    target.physicsDebugContactLinger = facts.physicsDebugContactLinger;
    target.rayCastImpulseStrength = facts.rayCastImpulseStrength;
    target.launcherProjectileSpeed = facts.launcherProjectileSpeed;
    target.trackedEngineBytes = facts.trackedEngineBytes;
    target.reconciledTotalBytes = facts.reconciledTotalBytes;
    target.uploadUsedBytes = facts.uploadUsedBytes;
    target.uploadCapacityBytes = facts.uploadCapacityBytes;
    target.replayReserveGrowthEvents = facts.replayReserveGrowthEvents;
    target.physicsDebugFlags = facts.physicsDebugFlags;
    target.collisionVisualizer = facts.collisionVisualizer;
    target.physicsDebugTransparent = facts.physicsDebugTransparent;
    target.broadphaseOverlay = facts.broadphaseOverlay;
    target.tornadoVisualShell = facts.tornadoVisualShell;
    target.tornadoFieldVectors = facts.tornadoFieldVectors;
    target.rayCastVisualization = facts.rayCastVisualization;

    for ( int index = 0; index < target.renderTargetCount; ++index )
    {
        const OperatorUiSecondaryRenderTargetFacts& source = facts.renderTargets[static_cast<std::size_t>( index )];
        target.renderTargets[index] = { source.label,     source.width, source.height,
                                        source.available, source.depth, source.hdr };
    }
}

void ProjectOperatorUiDiagnostics( UI::InGameUIFrameData& UIData, const OperatorUiDiagnosticsFacts& facts,
                                   UI::UIRuntimeReserveCapacityRow* reserveCapacityRows )
{
    const RuntimeFrameMetricsSnapshot& metrics = facts.metrics;
    UIData.UIDrawCalls = metrics.uiDrawCalls;
    UIData.visibility = ProjectRenderVisibilityDiagnostics( facts.visibility );
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
        const Rendering::DrawCallTraceSnapshot& drawTrace = facts.drawTrace;
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
        profilerFrame.markerCount = (std::min)( facts.markerCount, SkullbonezCore::UI::ProfilerTab::MAX_MARKERS );

        for ( int markerIndex = 0; markerIndex < profilerFrame.markerCount; ++markerIndex )
        {
            const OperatorUiProfilerMarkerFacts& source = facts.markers[static_cast<std::size_t>( markerIndex )];
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

        profilerFrame.workerCoreSampleCount = (std::min)( facts.workerSampleCount,
                                                          SkullbonezCore::UI::ProfilerTab::MAX_WORKER_CORE_SAMPLES );

        for ( int sampleIndex = 0; sampleIndex < profilerFrame.workerCoreSampleCount; ++sampleIndex )
        {
            const OperatorUiWorkerCoreFacts& source = facts.workerSamples[static_cast<std::size_t>( sampleIndex )];

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
    UIData.profiler.tracyBuildEnabled = facts.tracyBuildEnabled;
    UIData.profiler.tracyInitialized = facts.tracyInitialized;
    UIData.profiler.tracyViewerConnected = facts.tracyViewerConnected;
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

            for ( int markerIndex = 0; markerIndex < facts.markerCount; ++markerIndex )
            {
                const OperatorUiProfilerMarkerFacts& marker = facts.markers[static_cast<std::size_t>( markerIndex )];

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
        auto addProfilerMarker = [&]( const OperatorUiProfilerMarkerFacts& marker )
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
            for ( int markerIndex = 0; markerIndex < facts.markerCount; ++markerIndex )
            {
                const OperatorUiProfilerMarkerFacts& marker = facts.markers[static_cast<std::size_t>( markerIndex )];

                if ( marker.hash == pinnedHash )
                {
                    addProfilerMarker( marker );
                    break;
                }
            }
        }

        for ( int markerIndex = 0; markerIndex < facts.markerCount; ++markerIndex )
        {
            addProfilerMarker( facts.markers[static_cast<std::size_t>( markerIndex )] );
        }
#endif
    }
    UIData.workerThreadCount = facts.workerThreadCount;
    UIData.maxWorkerThreadCount = facts.maxWorkerThreadCount;
    UIData.now = facts.now;
    UIData.replayMemoryPreset = facts.replayMemoryPreset;
    UIData.replayMemoryRequestedRetentionSeconds = facts.replayRequestedRetentionSeconds;
    UIData.replayMemoryRequestedBudgetMiB = facts.replayRequestedBudgetMiB;
    UIData.replayMemoryPresentationRetentionSeconds = facts.replayPresentationRetentionSeconds;
    UIData.replayMemorySolverRetentionSeconds = facts.replaySolverRetentionSeconds;
    UIData.replayMemoryBudgetClamped = facts.replayMemoryBudgetClamped;
    UIData.replayMemorySolverWindowReduced = facts.replayMemorySolverWindowReduced;
    UIData.predictionRevealRate = facts.predictionRevealRate;
    UIData.reserveCapacityRows = nullptr;
    UIData.reserveCapacityRowCount = 0;

    UIData.mainMemory = facts.mainMemory;

    if ( facts.renderMemoryAvailable )
    {
        UIData.renderMemory = ProjectRenderMemoryDiagnostics( facts.renderMemory );
        UIData.reserveGrowthEventTotalCount = facts.reserveGrowthEventTotalCount;
        UIData.reserveGrowthEventDroppedCount = facts.reserveGrowthEventDroppedCount;
        UIData.reserveGrowthEventCount = (std::min)( facts.reserveGrowthEventCount,
                                                     SkullbonezCore::UI::UI_RUNTIME_RESERVE_GROWTH_EVENT_MAX );

        for ( int index = 0; index < UIData.reserveGrowthEventCount; ++index )
        {
            UIData.reserveGrowthEvents[index] = facts.reserveGrowthEvents[static_cast<std::size_t>( index )];
        }
    }

    if ( facts.reserveCapacityAvailable )
    {
        UIData.reserveCapacityRowCount = (std::min)( facts.reserveCapacityRowCount,
                                                     SkullbonezCore::UI::UI_RUNTIME_RESERVE_CAPACITY_ROW_MAX );

        for ( int index = 0; index < UIData.reserveCapacityRowCount; ++index )
        {
            const SkullbonezCore::Core::Allocation::RuntimeReserveCapacityView&
                source = facts.reserveCapacityRows[static_cast<std::size_t>( index )];
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
void ProjectOperatorUiPresentation( UI::InGameUIFrameData& UIData, const OperatorUiSceneFacts& facts,
                                    const UI::OperatorEditorFrameView& operatorEditorView )
{
    const RuntimeViewModel& view = facts.runtime;
    UIData.sceneName = view.sceneMode && facts.sceneHasCurrentEntry && facts.currentSceneName ? facts.currentSceneName : "";
    UIData.sceneOptions = facts.sceneOptions;
    UIData.sceneOptionCount = facts.sceneOptionCount;
    UIData.selectedSceneOption = facts.currentSceneBrowserIndex;
    UIData.selectedCineModeSceneOption = facts.selectedCineModeSceneIndex;
    UIData.modelCount = view.modelCount;
    UIData.currentFrame = view.frame;
    UIData.targetFrameCount = view.targetFrameCount;
    UIData.rngSeed = facts.rngSeed;
    UIData.solverBallCount = facts.solverBallCount;
    UIData.solverBoxCount = facts.solverBoxCount;
    UIData.currentSceneIndex = view.sceneIndex;
    UIData.sceneCount = view.sceneCount;
    UIData.sceneMode = view.sceneMode;
    UIData.scenePhysicsEnabled = view.scenePhysics;
    UIData.sceneTextEnabled = view.sceneText;
    UIData.fixedStep = view.fixedStep;
    UIData.exitOnComplete = facts.exitOnComplete;
    UIData.testComplete = facts.testComplete;
    UIData.sceneEnergy = facts.energyForDisplay;
    UIData.timeScale = view.timeScale;
    UIData.presentationInterpolation = view.presentationInterpolation;
    UIData.presentationPinned = view.presentationPinned;
    UIData.presentationAlpha = view.presentationAlpha;
    UIData.canSaveSceneDefaults = view.sceneMode && facts.sceneHasCurrentEntry && facts.currentScenePath &&
                                  facts.currentScenePath[0] != '\0';

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
void ProjectOperatorUiSettings( UI::InGameUIFrameData& UIData, const OperatorUiSettingsFacts& facts )
{
    UIData.modelCapacity = facts.modelCapacity;
    UIData.textOnly = facts.textOnly;
    UIData.vsyncEnabled = facts.vsyncEnabled;
    UIData.pipelineSyncEnabled = facts.pipelineSyncEnabled;
    UIData.worldGravity = facts.worldGravity;
    UIData.worldFluidHeight = facts.worldFluidHeight;
    UIData.worldFluidDensity = facts.worldFluidDensity;
    UIData.physicsDebug = facts.physicsDebug;
    UIData.physicsSleepEnabled = facts.physicsSleepEnabled;
    UIData.tornadoEnabled = facts.tornadoEnabled;
    UIData.tornadoVisualShell = facts.tornadoVisualShell && facts.tornadoEnabled;
    UIData.tornadoFieldVectors = facts.tornadoFieldVectors;
    UIData.tornadoRadius = facts.tornadoRadius;
    UIData.tornadoHeight = facts.tornadoHeight;
    UIData.tornadoInwardAcceleration = facts.tornadoInwardAcceleration;
    UIData.tornadoSwirlAcceleration = facts.tornadoSwirlAcceleration;
    UIData.tornadoLiftAcceleration = facts.tornadoLiftAcceleration;
    UIData.terrainFrictionCoeff = facts.terrainFriction;
    UIData.objectFrictionCoeff = facts.objectFriction;
    UIData.rollingFrictionCoeff = facts.rollingFriction;
    UIData.waterFreezeDebug = facts.waterFrozen;
    UIData.waterFlatDebug = facts.waterFlat;
    UIData.terrainHidden = facts.terrainHidden;
    UIData.waterHidden = facts.waterHidden;
    UIData.waterNoReflect = facts.waterNoReflect;
    UIData.waterRTReflect = facts.waterRtReflect;
    UIData.cinematicRendering = facts.cinematicRendering;
    UIData.ordinaryRender = facts.ordinary;
    UIData.cinematic = facts.cinematic;
}
void ProjectOperatorUiInteraction( UI::InGameUIFrameData& UIData, const OperatorUiInteractionFacts& facts )
{
    UIData.trackHeight = facts.trackHeight;
    UIData.autoCycleInterval = facts.autoCycleInterval;
    UIData.rayCastVisualization = facts.rayCastVisualization;
    UIData.rayCastImpulseStrength = facts.rayCastImpulseStrength;
    UIData.launcherProjectileSpeed = facts.launcherProjectileSpeed;
    UIData.cameraModeIndex = facts.cameraModeIndex;
    UIData.cameraModeEnabledMask = facts.cameraModeEnabledMask;
    UIData.runtimeInputModeLabel = facts.cameraModeLabel;
    UIData.cameraMouseActive = facts.cameraMouseActive;
    UIData.nativeCursorVisible = !facts.cameraMouseActive;
    UIData.editorModeEnabled = facts.editorModeEnabled;
    UIData.editorPlacementMode = facts.editorPlacementMode;
    UIData.editorPlaceStatic = facts.editorPlaceStatic;
    UIData.editorTerrainAlign = facts.editorTerrainAlign;
    UIData.editorViewportLookActive = facts.editorViewportLookActive;
    UIData.editorObjectType = facts.editorObjectType;
    UIData.editorUndoDepth = facts.editorUndoDepth;
    UIData.editorRedoDepth = facts.editorRedoDepth;
}

// PROJECTION_FUNCTIONS

} // namespace Runtime
} // namespace SkullbonezCore
