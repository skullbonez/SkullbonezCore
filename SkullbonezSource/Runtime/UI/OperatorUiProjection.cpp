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
void ProjectOperatorEditorInspectorAndWorld( UI::OperatorEditorFrameView& view,
                                             const UI::OperatorEditorInspectorView& inspector,
                                             const OperatorUiWorldFacts& world )
{
    view.inspector = inspector;

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

namespace
{
void ProjectOperatorUiDrawTrace( UI::InGameUIFrameData& uiData, const Rendering::DrawCallTraceSnapshot& drawTrace )
{
    // Concept: render draw attribution is copied through uiData while
    // the render diagnostics capability is already borrowed by Run. The
    // profiler tab never needs the wide renderer facade to explain draw
    // calls.
    const int sourceNodeCount = (std::max)( 0, drawTrace.nodeCount );
    const int nodeCount = (std::min)( sourceNodeCount, SkullbonezCore::UI::ProfilerTab::MAX_MARKERS );
    SkullbonezCore::UI::ProfilerTab::DrawTraceSnapshot& uiTrace = uiData.diagnostics.profiler.drawTrace;
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
void ProjectOperatorUiProfilerFrame( UI::InGameUIFrameData& uiData, std::span<const OperatorUiProfilerMarkerFacts> markers,
                                     std::span<const OperatorUiWorkerCoreFacts> workerSamples )
{
    static_assert( SkullbonezCore::UI::ProfilerTab::MAX_MARKERS == SkullbonezCore::Core::Profiler::MAX_MARKERS,
                   "UI profiler snapshot capacity must match SkullbonezCore::Core::Profiler markers" );

    static_assert( SkullbonezCore::UI::ProfilerTab::MAX_WORKER_CORE_SAMPLES ==
                       SkullbonezCore::Core::Profiler::MAX_WORKER_CORES,
                   "UI worker sample snapshot capacity must match SkullbonezCore::Core::Profiler samples" );

    SkullbonezCore::UI::ProfilerTab::FrameSnapshot& profilerFrame = uiData.diagnostics.profiler;
    profilerFrame.markerCount = (std::min)( static_cast<int>( markers.size() ),
                                            SkullbonezCore::UI::ProfilerTab::MAX_MARKERS );

    for ( int markerIndex = 0; markerIndex < profilerFrame.markerCount; ++markerIndex )
    {
        const OperatorUiProfilerMarkerFacts& source = markers[static_cast<std::size_t>( markerIndex )];
        const int paletteIndex = source.colorIndex >= 0
                                     ? source.colorIndex % SkullbonezCore::Core::Profiler::BAR_PALETTE_SIZE
                                     : 0;

        const SkullbonezCore::Core::Profiler::BarColor& color = SkullbonezCore::Core::Profiler::BAR_PALETTE[paletteIndex];

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

    profilerFrame.workerCoreSampleCount = (std::min)( static_cast<int>( workerSamples.size() ),
                                                      SkullbonezCore::UI::ProfilerTab::MAX_WORKER_CORE_SAMPLES );

    for ( int sampleIndex = 0; sampleIndex < profilerFrame.workerCoreSampleCount; ++sampleIndex )
    {
        const OperatorUiWorkerCoreFacts& source = workerSamples[static_cast<std::size_t>( sampleIndex )];

        SkullbonezCore::UI::ProfilerTab::WorkerCoreSampleSnapshot& target = profilerFrame.workerCoreSamples[sampleIndex];

        target.workerIndex = source.workerIndex;
        target.jobCount = source.jobCount;
        target.coreMs = source.coreMs;
        target.avgCoreMs = source.avgCoreMs;
        target.spanStartMs = source.spanStartMs;
        target.spanEndMs = source.spanEndMs;
        uiData.surface.workerCoreTotalMs += (std::max)( 0.0f, target.coreMs );
    }
}
#endif
} // namespace

void ProjectOperatorUiDiagnostics( UI::InGameUIFrameData& UIData, const OperatorUiDiagnosticsFacts& facts,
                                   UI::UIRuntimeReserveCapacityRow* reserveCapacityRows )
{
    UIData.surface.UIDrawCalls = facts.metrics.uiDrawCalls;
    UIData.surface.visibility = ProjectRenderVisibilityDiagnostics( facts.visibility );
    UIData.surface.fps = facts.metrics.rollingFrameSeconds > 0.0f
                             ? 1.0f / facts.metrics.rollingFrameSeconds
                             : ( facts.metrics.secondsPerFrame > 0.0
                                     ? 1.0f / static_cast<float>( facts.metrics.secondsPerFrame )
                                     : 0.0f );
    UIData.surface.renderMs = ( facts.metrics.rollingRenderSeconds > 0.0f ? facts.metrics.rollingRenderSeconds
                                                                          : facts.metrics.renderSeconds ) *
                              1000.0f;
    UIData.surface.physicsMs = ( facts.metrics.rollingPhysicsSeconds > 0.0f ? facts.metrics.rollingPhysicsSeconds
                                                                            : facts.metrics.physicsSeconds ) *
                               1000.0f;
    UIData.surface.cpuFrameMs = facts.metrics.cpuFrameWorkMs;
    UIData.surface.gpuFrameMs = facts.metrics.gpuFrameWorkMs;
    ProjectOperatorUiDrawTrace( UIData, facts.drawTrace );
#if defined( SKULLBONEZ_PROFILE_ENABLED )
    const int markerCount = (std::clamp)( facts.markerCount, 0, static_cast<int>( facts.markers.size() ) );
    const int workerSampleCount = (std::clamp)( facts.workerSampleCount, 0, static_cast<int>( facts.workerSamples.size() ) );
    ProjectOperatorUiProfilerFrame( UIData,
                                    std::span<const OperatorUiProfilerMarkerFacts>( facts.markers.data(), markerCount ),
                                    std::span<const OperatorUiWorkerCoreFacts>( facts.workerSamples.data(),
                                                                                workerSampleCount ) );
#endif
    UIData.diagnostics.profiler.tracyBuildEnabled = facts.tracyBuildEnabled;
    UIData.diagnostics.profiler.tracyInitialized = facts.tracyInitialized;
    UIData.diagnostics.profiler.tracyViewerConnected = facts.tracyViewerConnected;
    {
        // Concept: marker enumeration stays in the runtime pass that owns
        // profiler access. The UI receives a bounded frame snapshot so
        // drawing and hit testing do not reach into profiler globals.
        auto markerOptionExists = [&]( uint32_t hash, bool isFrameTotal ) -> bool
        {
            for ( int i = 0; i < UIData.diagnostics.profilerMarkerOptionCount; ++i )
            {
                const SkullbonezCore::UI::UIProfilerMarkerOption& option = UIData.diagnostics.profilerMarkerOptions[i];

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
            if ( UIData.diagnostics.profilerMarkerOptionCount >= SkullbonezCore::UI::UI_PROFILER_MARKER_OPTION_MAX ||
                 markerOptionExists( input.hash, input.isFrameTotal ) )
            {
                return;
            }

            SkullbonezCore::UI::UIProfilerMarkerOption&
                option = UIData.diagnostics.profilerMarkerOptions[UIData.diagnostics.profilerMarkerOptionCount++];

            option = input;
            option.name = input.name ? input.name : "";
            option.leafName = input.leafName ? input.leafName : option.name;
            option.cpuMs = (std::max)( 0.0f, input.cpuMs );
            option.cpuAverageMs = (std::max)( 0.0f, input.cpuAverageMs );
            option.workerMs = (std::max)( 0.0f, input.workerMs );
            option.workerAverageMs = (std::max)( 0.0f, input.workerAverageMs );
            option.gpuMs = (std::max)( 0.0f, input.gpuMs );
        };

        float frameAverageMs = UIData.surface.cpuFrameMs;
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
        addMarkerOption(
            SkullbonezCore::UI::UIProfilerMarkerOption { .name = "Frame Total",
                                                         .leafName = "Frame Total",
                                                         .hash = SkullbonezCore::UI::UI_PROFILER_FRAME_TOTAL_HASH,
                                                         .cpuMs = UIData.surface.cpuFrameMs,
                                                         .cpuAverageMs = frameAverageMs,
                                                         .gpuMs = UIData.surface.gpuFrameMs,
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

            addMarkerOption(
                SkullbonezCore::UI::UIProfilerMarkerOption { .name = marker.name,
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
    UIData.surface.workerThreadCount = facts.workerThreadCount;
    UIData.surface.maxWorkerThreadCount = facts.maxWorkerThreadCount;
    UIData.surface.now = facts.now;
    UIData.diagnostics.replayMemoryPreset = facts.replayMemoryPreset;
    UIData.diagnostics.replayMemoryRequestedRetentionSeconds = facts.replayRequestedRetentionSeconds;
    UIData.diagnostics.replayMemoryRequestedBudgetMiB = facts.replayRequestedBudgetMiB;
    UIData.diagnostics.replayMemoryPresentationRetentionSeconds = facts.replayPresentationRetentionSeconds;
    UIData.diagnostics.replayMemorySolverRetentionSeconds = facts.replaySolverRetentionSeconds;
    UIData.diagnostics.replayMemoryBudgetClamped = facts.replayMemoryBudgetClamped;
    UIData.diagnostics.replayMemorySolverWindowReduced = facts.replayMemorySolverWindowReduced;
    UIData.scene.predictionRevealRate = facts.predictionRevealRate;
    UIData.diagnostics.reserveCapacityRows = nullptr;
    UIData.diagnostics.reserveCapacityRowCount = 0;

    UIData.diagnostics.mainMemory = facts.mainMemory;

    if ( facts.renderMemoryAvailable )
    {
        UIData.diagnostics.renderMemory = ProjectRenderMemoryDiagnostics( facts.renderMemory );
        UIData.diagnostics.reserveGrowthEventTotalCount = facts.reserveGrowthEventTotalCount;
        UIData.diagnostics.reserveGrowthEventDroppedCount = facts.reserveGrowthEventDroppedCount;
        UIData.diagnostics.reserveGrowthEventCount = (std::min)( facts.reserveGrowthEventCount,
                                                                 SkullbonezCore::UI::UI_RUNTIME_RESERVE_GROWTH_EVENT_MAX );

        for ( int index = 0; index < UIData.diagnostics.reserveGrowthEventCount; ++index )
        {
            UIData.diagnostics.reserveGrowthEvents[index] = facts.reserveGrowthEvents[static_cast<std::size_t>( index )];
        }
    }

    if ( facts.reserveCapacityAvailable )
    {
        UIData.diagnostics.reserveCapacityRowCount = (std::min)( facts.reserveCapacityRowCount,
                                                                 SkullbonezCore::UI::UI_RUNTIME_RESERVE_CAPACITY_ROW_MAX );

        for ( int index = 0; index < UIData.diagnostics.reserveCapacityRowCount; ++index )
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

        UIData.diagnostics.reserveCapacityRows = reserveCapacityRows;
    }
}
void ProjectOperatorUiPresentation( UI::InGameUIFrameData& UIData, const OperatorUiSceneFacts& facts,
                                    const UI::OperatorEditorFrameView& operatorEditorView )
{
    const RuntimeViewModel& view = facts.runtime;
    UIData.surface.sceneName = view.sceneMode && facts.sceneHasCurrentEntry && facts.currentSceneName
                                   ? facts.currentSceneName
                                   : "";
    UIData.scene.sceneOptions = facts.sceneOptions;
    UIData.scene.sceneOptionCount = facts.sceneOptionCount;
    UIData.scene.selectedSceneOption = facts.currentSceneBrowserIndex;
    UIData.scene.selectedCineModeSceneOption = facts.selectedCineModeSceneIndex;
    UIData.scene.modelCount = view.modelCount;
    UIData.scene.currentFrame = view.frame;
    UIData.scene.targetFrameCount = view.targetFrameCount;
    UIData.scene.rngSeed = facts.rngSeed;
    UIData.scene.solverBallCount = facts.solverBallCount;
    UIData.scene.solverBoxCount = facts.solverBoxCount;
    UIData.scene.currentSceneIndex = view.sceneIndex;
    UIData.scene.sceneCount = view.sceneCount;
    UIData.surface.sceneMode = view.sceneMode;
    UIData.surface.scenePhysicsEnabled = view.scenePhysics;
    UIData.surface.sceneTextEnabled = view.sceneText;
    UIData.scene.fixedStep = view.fixedStep;
    UIData.scene.exitOnComplete = facts.exitOnComplete;
    UIData.scene.testComplete = facts.testComplete;
    UIData.scene.sceneEnergy = facts.energyForDisplay;
    UIData.scene.timeScale = view.timeScale;
    UIData.scene.presentationInterpolation = view.presentationInterpolation;
    UIData.scene.presentationPinned = view.presentationPinned;
    UIData.scene.presentationAlpha = view.presentationAlpha;
    UIData.scene.canSaveSceneDefaults = view.sceneMode && facts.sceneHasCurrentEntry && facts.currentScenePath &&
                                        facts.currentScenePath[0] != '\0';

    // Invariant: representative GameUI controls display the same immutable
    // values supplied to the secondary editor for this frame.
    UIData.operatorEditor = operatorEditorView;
    UIData.surface.sceneName = UIData.operatorEditor.scene.sceneName;
    UIData.scene.modelCount = UIData.operatorEditor.scene.modelCount;
    UIData.scene.currentFrame = UIData.operatorEditor.scene.currentFrame;
    UIData.scene.currentSceneIndex = UIData.operatorEditor.scene.currentSceneIndex;
    UIData.scene.sceneCount = UIData.operatorEditor.scene.sceneCount;
    UIData.scene.timeScale = UIData.operatorEditor.scene.timeScale;
    UIData.world.worldGravity = UIData.operatorEditor.property.worldGravity;
    UIData.world.worldFluidHeight = UIData.operatorEditor.property.worldFluidHeight;
    UIData.world.worldFluidDensity = UIData.operatorEditor.property.worldFluidDensity;
    UIData.surface.vsyncEnabled = UIData.operatorEditor.rendering.vsyncEnabled;
    UIData.scene.presentationInterpolation = UIData.operatorEditor.rendering.presentationInterpolation;
    UIData.scene.presentationAlpha = UIData.operatorEditor.rendering.presentationAlpha;
    UIData.rendering.cinematicRendering = UIData.operatorEditor.rendering.cinematicRendering;
    UIData.diagnostics.replayMemoryPreset = UIData.operatorEditor.replay.memoryPreset;
    UIData.diagnostics.replayMemoryRequestedRetentionSeconds = UIData.operatorEditor.replay.requestedRetentionSeconds;
    UIData.diagnostics.replayMemoryRequestedBudgetMiB = UIData.operatorEditor.replay.requestedBudgetMiB;
    UIData.diagnostics.replayMemoryPresentationRetentionSeconds = UIData.operatorEditor.replay.presentationRetentionSeconds;
    UIData.diagnostics.replayMemorySolverRetentionSeconds = UIData.operatorEditor.replay.solverRetentionSeconds;
    UIData.diagnostics.replayMemoryBudgetClamped = UIData.operatorEditor.replay.memoryBudgetClamped;
    UIData.diagnostics.replayMemorySolverWindowReduced = UIData.operatorEditor.replay.solverWindowReduced;
}
void ProjectOperatorUiSettings( UI::InGameUIFrameData& UIData, const OperatorUiSettingsFacts& facts )
{
    UIData.scene.modelCapacity = facts.modelCapacity;
    UIData.surface.textOnly = facts.textOnly;
    UIData.surface.vsyncEnabled = facts.vsyncEnabled;
    UIData.surface.pipelineSyncEnabled = facts.pipelineSyncEnabled;
    UIData.world.worldGravity = facts.worldGravity;
    UIData.world.worldFluidHeight = facts.worldFluidHeight;
    UIData.world.worldFluidDensity = facts.worldFluidDensity;
    UIData.world.physicsDebug = facts.physicsDebug;
    UIData.world.physicsSleepEnabled = facts.physicsSleepEnabled;
    UIData.world.tornadoEnabled = facts.tornadoEnabled;
    UIData.world.tornadoVisualShell = facts.tornadoVisualShell && facts.tornadoEnabled;
    UIData.world.tornadoFieldVectors = facts.tornadoFieldVectors;
    UIData.world.tornadoRadius = facts.tornadoRadius;
    UIData.world.tornadoHeight = facts.tornadoHeight;
    UIData.world.tornadoInwardAcceleration = facts.tornadoInwardAcceleration;
    UIData.world.tornadoSwirlAcceleration = facts.tornadoSwirlAcceleration;
    UIData.world.tornadoLiftAcceleration = facts.tornadoLiftAcceleration;
    UIData.world.terrainFrictionCoeff = facts.terrainFriction;
    UIData.world.objectFrictionCoeff = facts.objectFriction;
    UIData.world.rollingFrictionCoeff = facts.rollingFriction;
    UIData.world.waterFreezeDebug = facts.waterFrozen;
    UIData.world.waterFlatDebug = facts.waterFlat;
    UIData.world.terrainHidden = facts.terrainHidden;
    UIData.world.waterHidden = facts.waterHidden;
    UIData.world.waterNoReflect = facts.waterNoReflect;
    UIData.world.waterRTReflect = facts.waterRtReflect;
    UIData.rendering.cinematicRendering = facts.cinematicRendering;
    UIData.rendering.ordinaryRender = facts.ordinary;
    UIData.rendering.cinematic = facts.cinematic;
}
void ProjectOperatorUiInteraction( UI::InGameUIFrameData& UIData, const OperatorUiInteractionFacts& facts )
{
    UIData.surface.trackHeight = facts.trackHeight;
    UIData.surface.autoCycleInterval = facts.autoCycleInterval;
    UIData.world.rayCastVisualization = facts.rayCastVisualization;
    UIData.world.rayCastImpulseStrength = facts.rayCastImpulseStrength;
    UIData.world.launcherProjectileSpeed = facts.launcherProjectileSpeed;
    UIData.surface.cameraModeIndex = facts.cameraModeIndex;
    UIData.surface.cameraModeEnabledMask = facts.cameraModeEnabledMask;
    UIData.surface.runtimeInputModeLabel = facts.cameraModeLabel;
    UIData.surface.cameraMouseActive = facts.cameraMouseActive;
    UIData.surface.nativeCursorVisible = !facts.cameraMouseActive;
    UIData.editor.editorModeEnabled = facts.editorModeEnabled;
    UIData.editor.editorPlacementMode = facts.editorPlacementMode;
    UIData.editor.editorPlaceStatic = facts.editorPlaceStatic;
    UIData.editor.editorTerrainAlign = facts.editorTerrainAlign;
    UIData.editor.editorViewportLookActive = facts.editorViewportLookActive;
    UIData.editor.editorObjectType = facts.editorObjectType;
    UIData.editor.editorUndoDepth = facts.editorUndoDepth;
    UIData.editor.editorRedoDepth = facts.editorRedoDepth;
}

// PROJECTION_FUNCTIONS

} // namespace Runtime
} // namespace SkullbonezCore
