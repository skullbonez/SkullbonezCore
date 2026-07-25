/*
File: UIFrameComposition.cpp
Purpose:
  Implements stateless UI frame signatures, resource-preview helpers, and
  minimized-window geometry shared by the UI owner and palette units.

Summary:
  UI.cpp owns retained widget state. These functions derive bounded hashes,
  rectangles, labels, and draw submissions from explicit frame values without
  retaining a host or frame borrow.

Glossary:
  Content signature: Hash of UI-visible values used to invalidate cached draws.
  Interaction signature: Hash of pointer/focus state used to invalidate hit data.
  Preview catalog: Bounded render-target list offered to the UI for inspection.
  Profiler connection snapshot: Fixed build, lifetime, and viewer-state flags
    copied into a profiler frame without retaining the live tool owner.

Invariants:
  - Hash field order is part of draw-cache invalidation behavior.
  - Preview helpers clamp every authored/runtime count to fixed UI capacities.
  - Functions retain no UI owner pointer or mutable frame state.
  - Tracy status participates in the profiler signature so a viewer connection
    transition invalidates the cached draw without per-frame text allocation.
  - Render-target previews use a declared depth-disabled opaque bucket and do
    not query or restore the surrounding UI pass's raster state.

Related:
  - UIFrameComposition.h declares value contracts and constants.
  - UI.cpp owns the surrounding UI frame.
*/
#include "UIFrameComposition.h"
#include "UIFontMetrics.h"
#include "../Rendering/DX12/RenderBackendDX12.h"
#include "../Assets/AssetKeys.h"
#include "../Rendering/RenderGpuTimingOwner.h"

namespace SkullbonezCore::UI::FrameComposition
{
namespace
{
constexpr Rendering::PassRasterStateBucket PREVIEW_RASTER_STATE =
    Rendering::MakePassRasterStateBucket( 0, { false, false, false } );
}

uint32_t HashCombine( uint32_t seed, uint32_t value )
{
    seed ^= value;
    seed *= 16777619u;
    return seed;
}


uint32_t HashTextValue( uint32_t seed, const char* value )
{
    if ( !value )
    {
        return HashCombine( seed, 0u );
    }

    while ( *value != '\0' )
    {
        seed = HashCombine( seed, static_cast<uint8_t>( *value ) );
        ++value;
    }
    return HashCombine( seed, 0u );
}


uint32_t HashBool( uint32_t seed, bool value )
{
    return HashCombine( seed, value ? 1u : 0u );
}


uint32_t HashInt( uint32_t seed, int value )
{
    return HashCombine( seed, static_cast<uint32_t>( value ) );
}


uint32_t HashFloat( uint32_t seed, float value, float scale )
{
    return HashInt( seed, static_cast<int>( std::round( value * scale ) ) );
}


UIRect MinimizedCameraModeComboBounds( const UIRect& minimized )
{
    return { minimized.x + minimized.w - MINIMIZED_RESTORE_W - MINIMIZED_CAMERA_MODE_COMBO_W,
             minimized.y + 6.0f,
             MINIMIZED_CAMERA_MODE_COMBO_W,
             24.0f };
}


float MinimizedWidthWithCameraModeCombo( const char* title, int screenW )
{
    constexpr float margin = 14.0f;
    constexpr float textSize = 12.5f;
    constexpr float titleLeft = 32.0f;
    const float maxW = (std::max)( 154.0f, static_cast<float>( screenW ) - margin * 2.0f );
    const float titleW = UIFontMetrics::MeasureText( textSize, title ? title : "" );
    const float desiredW =
        titleLeft + titleW + MINIMIZED_CAMERA_MODE_GAP + MINIMIZED_CAMERA_MODE_COMBO_W + MINIMIZED_RESTORE_W;
    return std::clamp( desiredW, 154.0f, maxW );
}


void StripMinimizedRuntimeModeSuffix( const InGameUIFrameData& data, char* title, size_t titleSize )
{
    if ( !title || titleSize == 0 )
    {
        return;
    }

    const char* runtimeMode = data.runtimeInputModeLabel ? data.runtimeInputModeLabel : "";
    if ( runtimeMode[0] == '\0' || std::strcmp( runtimeMode, "Scene" ) == 0 )
    {
        return;
    }

    char suffix[80] = {};
    snprintf( suffix, sizeof( suffix ), "  [%s]", runtimeMode );
    const size_t titleLen = strlen( title );
    const size_t suffixLen = strlen( suffix );
    if ( titleLen >= suffixLen && std::strcmp( title + titleLen - suffixLen, suffix ) == 0 )
    {
        title[titleLen - suffixLen] = '\0';
    }
}


uint32_t HashRenderTargetPreviewCatalog( uint32_t hash, const InGameUIFrameData& data )
{
    const int count = std::clamp( data.renderTargetPreviewCount, 0, UI_RENDER_TARGET_PREVIEW_MAX );
    hash = HashInt( hash, count );
    for ( int i = 0; i < count; ++i )
    {
        const UIRenderTargetPreviewResource& resource = data.renderTargetPreviews[i];
        hash = HashTextValue( hash, resource.label );
        hash = HashInt( hash, static_cast<int>( resource.textureHandle ) );
        hash = HashInt( hash, resource.width );
        hash = HashInt( hash, resource.height );
        hash = HashBool( hash, resource.available );
        hash = HashBool( hash, resource.depth );
        hash = HashBool( hash, resource.hdr );
    }
    return hash;
}


uint32_t HashProfilerFrameSnapshot( uint32_t hash, const ProfilerTab::FrameSnapshot& frame )
{
    // Invariant: profiler tab draw caching depends on bounded snapshot values,
    // not live singleton reads. Hash only the fixed arrays copied into UIData.
#if defined( TRACY_ENABLE )
    hash = HashBool( hash, frame.tracyBuildEnabled );
    hash = HashBool( hash, frame.tracyInitialized );
    hash = HashBool( hash, frame.tracyViewerConnected );
#endif
    const int markerCount = std::clamp( frame.markerCount, 0, ProfilerTab::MAX_MARKERS );
    hash = HashInt( hash, markerCount );
    for ( int markerIndex = 0; markerIndex < markerCount; ++markerIndex )
    {
        const ProfilerTab::MarkerSnapshot& marker = frame.markers[markerIndex];
        hash = HashTextValue( hash, marker.leafName );
        hash = HashInt( hash, static_cast<int>( marker.hash ) );
        hash = HashInt( hash, marker.parentIndex );
        hash = HashInt( hash, marker.depth );
        hash = HashFloat( hash, marker.lastFrameMs, 1000.0f );
        hash = HashFloat( hash, marker.lastSelfMs, 1000.0f );
        hash = HashFloat( hash, marker.avgMs, 1000.0f );
        hash = HashFloat( hash, marker.selfAvgMs, 1000.0f );
        hash = HashFloat( hash, marker.p50Ms, 1000.0f );
        hash = HashFloat( hash, marker.p99Ms, 1000.0f );
    }

    const int workerSampleCount = std::clamp( frame.workerCoreSampleCount, 0, ProfilerTab::MAX_WORKER_CORE_SAMPLES );
    hash = HashInt( hash, workerSampleCount );
    for ( int sampleIndex = 0; sampleIndex < workerSampleCount; ++sampleIndex )
    {
        const ProfilerTab::WorkerCoreSampleSnapshot& sample = frame.workerCoreSamples[sampleIndex];
        hash = HashInt( hash, sample.workerIndex );
        hash = HashInt( hash, sample.jobCount );
        hash = HashFloat( hash, sample.coreMs, 1000.0f );
        hash = HashFloat( hash, sample.avgCoreMs, 1000.0f );
    }

    const ProfilerTab::DrawTraceSnapshot& drawTrace = frame.drawTrace;
    const int nodeCount = std::clamp( drawTrace.nodeCount, 0, ProfilerTab::MAX_MARKERS );
    hash = HashInt( hash, nodeCount );
    hash = HashInt( hash, drawTrace.nodeOverflowCount );
    hash = HashInt( hash, drawTrace.eventCount );
    hash = HashInt( hash, drawTrace.eventOverflowCount );
    hash = HashInt( hash, drawTrace.scopeMismatchCount );
    for ( int nodeIndex = 0; nodeIndex < nodeCount; ++nodeIndex )
    {
        const ProfilerTab::DrawTraceNodeSnapshot& node = drawTrace.nodes[nodeIndex];
        hash = HashTextValue( hash, node.leafName );
        hash = HashInt( hash, static_cast<int>( node.hash ) );
        hash = HashInt( hash, node.parentIndex );
        hash = HashInt( hash, node.depth );
        hash = HashInt( hash, node.drawCallCount );
        hash = HashInt( hash, node.instanceCount );
        hash = HashInt( hash, node.vertexCount );
    }
    return hash;
}


uint32_t BuildUIContentSignature( const InGameUIFrameData& data )
{
    // Invariant: The content signature is the cache invalidation contract.
    // Include every frame-data value that can change visible UI text, controls,
    // preview resources, or hit-test-derived drawing.
    uint32_t hash = 2166136261u;
    hash = HashTextValue( hash, data.rendererName );
    hash = HashTextValue( hash, data.sceneName );
    hash = HashInt( hash, data.sceneOptionCount );
    hash = HashInt( hash, data.selectedSceneOption );
    hash = HashInt( hash, data.selectedCineModeSceneOption );
    for ( int i = 0; i < data.sceneOptionCount && data.sceneOptions; ++i )
    {
        hash = HashTextValue( hash, data.sceneOptions[i] );
    }
    hash = HashInt( hash, data.drawCallsBeforeUI );
    hash = HashInt( hash, data.UIDrawCalls );
    // Invariant: visibility rows are live diagnostics. Hash every field so a
    // retained UI draw cannot display the preceding frame's culling result.
    for ( int viewIndex = 0; viewIndex < static_cast<int>( Rendering::RenderVisibilityView::Count ); ++viewIndex )
    {
        const Rendering::RenderVisibilityViewStats& visibility = data.visibility.views[viewIndex];
        hash = HashInt( hash, visibility.candidates );
        hash = HashInt( hash, visibility.submitted );
        hash = HashInt( hash, visibility.culled );
        hash = HashInt( hash, visibility.draws );
    }
    hash = HashInt( hash, static_cast<int>( data.reserveGrowthEventTotalCount ) );
    hash = HashInt( hash, data.reserveGrowthEventCount );
    for ( int eventIndex = 0;
          eventIndex < data.reserveGrowthEventCount && eventIndex < UI_RUNTIME_RESERVE_GROWTH_EVENT_MAX;
          ++eventIndex )
    {
        const SkullbonezCore::Core::Allocation::RuntimeReserveGrowthEventView& event =
            data.reserveGrowthEvents[eventIndex];
        hash = HashTextValue( hash, event.targetName );
        hash = HashInt( hash, event.frameNumber );
        hash = HashInt( hash, event.grantedCapacity );
        hash = HashInt( hash, static_cast<int>( event.bytes ) );
        hash = HashBool( hash, event.granted );
    }
    hash = HashFloat( hash, data.fps );
    hash = HashFloat( hash, data.renderMs, 1000.0f );
    hash = HashFloat( hash, data.physicsMs, 1000.0f );
    hash = HashFloat( hash, data.cpuFrameMs, 1000.0f );
    hash = HashFloat( hash, data.gpuFrameMs, 1000.0f );
    hash = HashFloat( hash, data.workerCoreTotalMs, 1000.0f );
    hash = HashProfilerFrameSnapshot( hash, data.profiler );
    hash = HashInt( hash, data.modelCount );
    hash = HashInt( hash, data.modelCapacity );
    hash = HashInt( hash, data.workerThreadCount );
    hash = HashInt( hash, data.maxWorkerThreadCount );
    hash = HashInt( hash, data.currentFrame );
    hash = HashInt( hash, data.targetFrameCount );
    hash = HashInt( hash, static_cast<int>( data.rngSeed ) );
    hash = HashInt( hash, data.solverBallCount );
    hash = HashInt( hash, data.solverBoxCount );
    hash = HashInt( hash, data.currentSceneIndex );
    hash = HashInt( hash, data.sceneCount );
    hash = HashInt( hash, static_cast<int>( std::round( data.now * 1000.0 ) ) );
    hash = HashBool( hash, data.sceneMode );
    hash = HashBool( hash, data.scenePhysicsEnabled );
    hash = HashBool( hash, data.sceneTextEnabled );
    hash = HashBool( hash, data.textOnly );
    hash = HashBool( hash, data.fixedStep );
    hash = HashBool( hash, data.exitOnComplete );
    hash = HashBool( hash, data.testComplete );
    hash = HashBool( hash, data.vsyncEnabled );
    hash = HashBool( hash, data.pipelineSyncEnabled );
    hash = HashFloat( hash, data.sceneEnergy, 1000.0f );
    hash = HashFloat( hash, data.timeScale, 1000.0f );
    hash = HashBool( hash, data.presentationInterpolation );
    hash = HashBool( hash, data.presentationPinned );
    hash = HashFloat( hash, data.presentationAlpha, 1000.0f );
    hash = HashFloat( hash, data.trackHeight, 1000.0f );
    hash = HashFloat( hash, data.autoCycleInterval, 1000.0f );
    hash = HashFloat( hash, data.worldGravity, 1000.0f );
    hash = HashFloat( hash, data.worldFluidHeight, 1000.0f );
    hash = HashFloat( hash, data.worldFluidDensity, 1000.0f );
    hash = HashInt( hash, static_cast<int>( data.physicsDebug.activeFlags ) );
    hash = HashTextValue( hash, data.physicsDebug.pipelineStageName );
    hash = HashInt( hash, data.physicsDebug.pipelineStageIndex );
    hash = HashInt( hash, data.physicsDebug.pipelineStageCount );
    hash = HashFloat( hash, data.physicsDebug.alpha, 1000.0f );
    hash = HashFloat( hash, data.physicsDebug.contactLinger, 1000.0f );
    hash = HashBool( hash, data.physicsSleepEnabled );
    hash = HashBool( hash, data.physicsDebug.collisionVisualizer );
    hash = HashBool( hash, data.physicsDebug.transparent );
    hash = HashBool( hash, data.physicsDebug.broadphase );
    hash = HashBool( hash, data.tornadoEnabled );
    hash = HashBool( hash, data.tornadoVisualShell );
    hash = HashBool( hash, data.tornadoFieldVectors );
    hash = HashBool( hash, data.rayCastVisualization );
    hash = HashFloat( hash, data.tornadoRadius, 100.0f );
    hash = HashFloat( hash, data.tornadoHeight, 100.0f );
    hash = HashFloat( hash, data.tornadoInwardAcceleration, 100.0f );
    hash = HashFloat( hash, data.tornadoSwirlAcceleration, 100.0f );
    hash = HashFloat( hash, data.tornadoLiftAcceleration, 100.0f );
    hash = HashFloat( hash, data.rayCastImpulseStrength, 100.0f );
    hash = HashFloat( hash, data.launcherProjectileSpeed, 100.0f );
    hash = HashFloat( hash, data.terrainFrictionCoeff, 1000.0f );
    hash = HashFloat( hash, data.objectFrictionCoeff, 1000.0f );
    hash = HashFloat( hash, data.rollingFrictionCoeff, 1000.0f );
    hash = HashBool( hash, data.waterFreezeDebug );
    hash = HashBool( hash, data.waterFlatDebug );
    hash = HashBool( hash, data.terrainHidden );
    hash = HashBool( hash, data.waterHidden );
    hash = HashBool( hash, data.waterNoReflect );
    hash = HashBool( hash, data.waterRTReflect );
    hash = HashBool( hash, data.cameraMouseActive );
    hash = HashBool( hash, data.nativeCursorVisible );
    hash = HashTextValue( hash, data.runtimeInputModeLabel );
    hash = HashInt( hash, data.cameraModeIndex );

    hash = HashInt( hash, static_cast<int>( data.cameraModeEnabledMask ) );
    hash = HashBool( hash, data.editorModeEnabled );
    hash = HashBool( hash, data.editorPlacementMode );
    hash = HashBool( hash, data.editorPlaceStatic );
    hash = HashBool( hash, data.editorTerrainAlign );
    hash = HashBool( hash, data.editorViewportLookActive );
    hash = HashInt( hash, data.editorObjectType );
    hash = HashInt( hash, data.editorUndoDepth );
    hash = HashInt( hash, data.editorRedoDepth );
    hash = HashBool( hash, data.canSaveSceneDefaults );
    hash = HashBool( hash, data.cinematicRendering );
    hash = HashBool( hash, data.ordinaryRender.shadow.enabled );
    hash = HashFloat( hash, data.ordinaryRender.sunIntensity, 1000.0f );
    hash = HashFloat( hash, data.ordinaryRender.sunColorR, 1000.0f );
    hash = HashFloat( hash, data.ordinaryRender.sunColorG, 1000.0f );
    hash = HashFloat( hash, data.ordinaryRender.sunColorB, 1000.0f );
    hash = HashFloat( hash, data.ordinaryRender.ambientStrength, 1000.0f );
    hash = HashFloat( hash, data.ordinaryRender.skyAmbientR, 1000.0f );
    hash = HashFloat( hash, data.ordinaryRender.skyAmbientG, 1000.0f );
    hash = HashFloat( hash, data.ordinaryRender.skyAmbientB, 1000.0f );
    hash = HashFloat( hash, data.ordinaryRender.groundAmbientR, 1000.0f );
    hash = HashFloat( hash, data.ordinaryRender.groundAmbientG, 1000.0f );
    hash = HashFloat( hash, data.ordinaryRender.groundAmbientB, 1000.0f );
    hash = HashFloat( hash, data.ordinaryRender.shadow.strength, 1000.0f );
    hash = HashFloat( hash, data.ordinaryRender.shadow.softness, 1000.0f );
    hash = HashFloat( hash, data.ordinaryRender.shadow.depthBias, 100000.0f );
    hash = HashFloat( hash, data.ordinaryRender.shadow.slopeBias, 100000.0f );
    hash = HashFloat( hash, data.ordinaryRender.waterTintR, 1000.0f );
    hash = HashFloat( hash, data.ordinaryRender.waterTintG, 1000.0f );
    hash = HashFloat( hash, data.ordinaryRender.waterTintB, 1000.0f );
    hash = HashFloat( hash, data.ordinaryRender.waterAlpha, 1000.0f );
    hash = HashFloat( hash, data.ordinaryRender.waterReflectionStrength, 1000.0f );
    hash = HashFloat( hash, data.ordinaryRender.waterFresnelF0, 10000.0f );
    hash = HashFloat( hash, data.ordinaryRender.ballRoughnessScale, 1000.0f );
    hash = HashFloat( hash, data.ordinaryRender.ballSpecularScale, 1000.0f );
    hash = HashFloat( hash, data.ordinaryRender.boxRoughnessScale, 1000.0f );
    hash = HashFloat( hash, data.ordinaryRender.boxSpecularScale, 1000.0f );
    hash = HashFloat( hash, data.ordinaryRender.replayTrajectory.futureWidth, 1000.0f );
    hash = HashFloat( hash, data.ordinaryRender.replayTrajectory.futureAlpha, 1000.0f );
    hash = HashFloat( hash, data.ordinaryRender.replayTrajectory.futureEdgeFeather, 1000.0f );
    hash = HashFloat( hash, data.ordinaryRender.replayTrajectory.causalWidth, 1000.0f );
    hash = HashFloat( hash, data.ordinaryRender.replayTrajectory.causalAlpha, 1000.0f );
    hash = HashFloat( hash, data.ordinaryRender.replayTrajectory.causalEdgeFeather, 1000.0f );
    hash = HashFloat( hash, data.ordinaryRender.replayTrajectory.baselineWidth, 1000.0f );
    hash = HashFloat( hash, data.ordinaryRender.replayTrajectory.baselineAlpha, 1000.0f );
    hash = HashFloat( hash, data.ordinaryRender.replayTrajectory.baselineEdgeFeather, 1000.0f );
    hash = HashFloat( hash, data.ordinaryRender.replayTrajectory.markerWidth, 1000.0f );
    hash = HashFloat( hash, data.ordinaryRender.replayTrajectory.markerAlpha, 1000.0f );
    hash = HashFloat( hash, data.ordinaryRender.replayTrajectory.markerEdgeFeather, 1000.0f );
    hash = HashFloat( hash, data.ordinaryRender.replayTrajectory.selectedEmphasis, 1000.0f );
    hash = HashBool( hash, data.cinematic.enabled );
    hash = HashBool( hash, data.cinematic.skyAtmosphereEnabled );
    hash = HashBool( hash, data.cinematic.cloudsEnabled );
    hash = HashBool( hash, data.cinematic.godRaysEnabled );
    hash = HashBool( hash, data.cinematic.volumetricLightingEnabled );
    hash = HashBool( hash, data.cinematic.bloomEnabled );
    hash = HashBool( hash, data.cinematic.fogEnabled );
    hash = HashBool( hash, data.cinematic.terrainReliefEnabled );
    hash = HashBool( hash, data.cinematic.shadow.enabled );
    hash = HashFloat( hash, data.cinematic.exposure, 1000.0f );
    hash = HashFloat( hash, data.cinematic.gamma, 1000.0f );
    hash = HashFloat( hash, data.cinematic.sunAzimuth, 1000.0f );
    hash = HashFloat( hash, data.cinematic.sunElevation, 1000.0f );
    hash = HashFloat( hash, data.cinematic.sunColorR, 1000.0f );
    hash = HashFloat( hash, data.cinematic.sunColorG, 1000.0f );
    hash = HashFloat( hash, data.cinematic.sunColorB, 1000.0f );
    hash = HashFloat( hash, data.cinematic.sunIntensity, 100.0f );
    hash = HashFloat( hash, data.cinematic.skyGlowStrength, 1000.0f );
    hash = HashFloat( hash, data.cinematic.cloudCoverage, 1000.0f );
    hash = HashFloat( hash, data.cinematic.cloudSoftness, 1000.0f );
    hash = HashFloat( hash, data.cinematic.cloudScale, 1000.0f );
    hash = HashFloat( hash, data.cinematic.cloudIntensity, 1000.0f );
    hash = HashFloat( hash, data.cinematic.sunShaftStrength, 1000.0f );
    hash = HashFloat( hash, data.cinematic.sunShaftFalloff, 1000.0f );
    hash = HashFloat( hash, data.cinematic.volumetricStrength, 1000.0f );
    hash = HashFloat( hash, data.cinematic.volumetricDensity, 1000.0f );
    hash = HashFloat( hash, data.cinematic.volumetricDecay, 1000.0f );
    hash = HashFloat( hash, data.cinematic.bloomThreshold, 1000.0f );
    hash = HashFloat( hash, data.cinematic.bloomKnee, 1000.0f );
    hash = HashFloat( hash, data.cinematic.bloomStrength, 1000.0f );
    hash = HashFloat( hash, data.cinematic.bloomRadius, 1000.0f );
    hash = HashFloat( hash, data.cinematic.terrainRelief, 1000.0f );
    hash = HashFloat( hash, data.cinematic.basinDepth, 100.0f );
    hash = HashFloat( hash, data.cinematic.basinRimLift, 100.0f );
    hash = HashFloat( hash, data.cinematic.fogColorR, 1000.0f );
    hash = HashFloat( hash, data.cinematic.fogColorG, 1000.0f );
    hash = HashFloat( hash, data.cinematic.fogColorB, 1000.0f );
    hash = HashFloat( hash, data.cinematic.fogStart, 10.0f );
    hash = HashFloat( hash, data.cinematic.fogEnd, 10.0f );
    hash = HashFloat( hash, data.cinematic.fogDensity, 100000.0f );
    hash = HashFloat( hash, data.cinematic.fogMaxOpacity, 1000.0f );
    hash = HashInt( hash, data.cinematic.skyMode );
    hash = HashInt( hash, data.cinematic.terrainMode );
    hash = HashInt( hash, data.cinematic.objectStyle );
    hash = HashInt( hash, data.cinematic.waterMode );
    hash = HashFloat( hash, data.cinematic.styleSaturation, 1000.0f );
    hash = HashFloat( hash, data.cinematic.styleContrast, 1000.0f );
    hash = HashFloat( hash, data.cinematic.styleVignette, 1000.0f );
    hash = HashFloat( hash, data.cinematic.terrainTintR, 1000.0f );
    hash = HashFloat( hash, data.cinematic.terrainTintG, 1000.0f );
    hash = HashFloat( hash, data.cinematic.terrainTintB, 1000.0f );
    hash = HashFloat( hash, data.cinematic.terrainAccentR, 1000.0f );
    hash = HashFloat( hash, data.cinematic.terrainAccentG, 1000.0f );
    hash = HashFloat( hash, data.cinematic.terrainAccentB, 1000.0f );
    hash = HashFloat( hash, data.cinematic.terrainGridScale, 100.0f );
    hash = HashFloat( hash, data.cinematic.terrainGridStrength, 1000.0f );
    hash = HashFloat( hash, data.cinematic.waterTintR, 1000.0f );
    hash = HashFloat( hash, data.cinematic.waterTintG, 1000.0f );
    hash = HashFloat( hash, data.cinematic.waterTintB, 1000.0f );
    hash = HashFloat( hash, data.cinematic.waterAlpha, 1000.0f );
    hash = HashFloat( hash, data.cinematic.waterReflectionStrength, 1000.0f );
    hash = HashFloat( hash, data.cinematic.waterGlintStrength, 1000.0f );
    hash = HashFloat( hash, data.cinematic.basinCenterX, 10.0f );
    hash = HashFloat( hash, data.cinematic.basinCenterZ, 10.0f );
    hash = HashFloat( hash, data.cinematic.basinRadiusX, 10.0f );
    hash = HashFloat( hash, data.cinematic.basinRadiusZ, 10.0f );
    hash = HashFloat( hash, data.cinematic.basinFeather, 1000.0f );
    hash = HashRenderTargetPreviewCatalog( hash, data );
    return hash;
}


uint32_t BuildUIInteractionSignature(
    int mouseX,
    int mouseY,
    bool rendererOpen,
    bool reflectionOpen,
    bool sceneOpen,
    bool cineSceneOpen,
    bool editorObjectOpen,
    bool renderTargetOpen,
    bool cameraModeOpen,
    int selectedRenderTarget,
    int activeSlider
)
{
    uint32_t hash = 2166136261u;
    hash = HashInt( hash, mouseX );
    hash = HashInt( hash, mouseY );
    hash = HashBool( hash, rendererOpen );
    hash = HashBool( hash, reflectionOpen );
    hash = HashBool( hash, sceneOpen );
    hash = HashBool( hash, cineSceneOpen );
    hash = HashBool( hash, editorObjectOpen );
    hash = HashBool( hash, renderTargetOpen );
    hash = HashBool( hash, cameraModeOpen );
    hash = HashInt( hash, selectedRenderTarget );
    hash = HashInt( hash, activeSlider );
    return hash;
}


void FlushUIDrawList(
    const UIDrawList& drawList,
    Text::TextBatch& textBatch,
    Rendering::RenderGpuTimingOwner* gpuTiming,
    Rendering::Dx12TextureOwner& renderTextures,
    Rendering::Dx12GeometryOwner& renderCommands,
    Rendering::Dx12Diagnostics& renderDiagnostics,
    int screenW,
    int screenH,
    float offsetX,
    float offsetY
)
{
    PROFILE_GPU_BEGIN( gpuTiming, "Frame/UI/Draw" );
    const UIDrawContext immediateDraw( screenW, screenH, nullptr, &renderTextures, &renderCommands, &textBatch );
    for ( const UIDrawList::Command& command : drawList.Commands() )
    {
        switch ( command.type )
        {
        case UIDrawList::CommandType::Rect:
            immediateDraw.Rect(
                command.x0 + offsetX,
                command.y0 + offsetY,
                command.w,
                command.h,
                command.r,
                command.g,
                command.b,
                command.a
            );
            break;
        case UIDrawList::CommandType::RoundedRect:
            immediateDraw.RoundedRect(
                command.x0 + offsetX,
                command.y0 + offsetY,
                command.w,
                command.h,
                command.radius,
                command.r,
                command.g,
                command.b,
                command.a
            );
            break;
        case UIDrawList::CommandType::Triangle:
            immediateDraw.Triangle(
                command.x0 + offsetX,
                command.y0 + offsetY,
                command.x1 + offsetX,
                command.y1 + offsetY,
                command.x2 + offsetX,
                command.y2 + offsetY,
                command.r,
                command.g,
                command.b,
                command.a
            );
            break;
        case UIDrawList::CommandType::Text:
            immediateDraw.Text(
                command.x0 + offsetX,
                command.y0 + offsetY,
                command.pxSize,
                command.r,
                command.g,
                command.b,
                drawList.TextAt( command.textOffset )
            );
            break;
        case UIDrawList::CommandType::PushClip:
        case UIDrawList::CommandType::PopClip:
        case UIDrawList::CommandType::PreviewImage:
            // UR1 defines these backend-neutral values before UR2 converts
            // their live producers and UR3 gives Runtime/Render submission.
            break;
        }
    }
    {
        DRAW_CALL_TRACE_SCOPE( renderDiagnostics, "Widgets" );
        Text::Text2d::FlushQuads( textBatch, renderCommands );
    }
    {
        DRAW_CALL_TRACE_SCOPE( renderDiagnostics, "Text" );
        Text::Text2d::FlushText( textBatch, renderTextures, renderCommands );
    }
    PROFILE_GPU_END( gpuTiming, "Frame/UI/Draw" );
}


int RenderTargetPreviewCount( const InGameUIFrameData& data )
{
    return std::clamp( data.renderTargetPreviewCount, 0, UI_RENDER_TARGET_PREVIEW_MAX );
}


uint32_t RenderTargetPreviewDisabledMask( const InGameUIFrameData& data )
{
    uint32_t mask = 0;
    const int count = RenderTargetPreviewCount( data );
    for ( int i = 0; i < count; ++i )
    {
        const UIRenderTargetPreviewResource& resource = data.renderTargetPreviews[i];
        if ( !resource.available || resource.textureHandle == 0 || resource.width <= 0 || resource.height <= 0 )
        {
            mask |= 1u << i;
        }
    }
    return mask;
}


int FirstAvailableRenderTargetPreview( const InGameUIFrameData& data )
{
    const int count = RenderTargetPreviewCount( data );
    for ( int i = 0; i < count; ++i )
    {
        const UIRenderTargetPreviewResource& resource = data.renderTargetPreviews[i];
        if ( resource.available && resource.textureHandle != 0 && resource.width > 0 && resource.height > 0 )
        {
            return i;
        }
    }
    return count > 0 ? 0 : -1;
}


int ResolveRenderTargetPreviewSelection( const InGameUIFrameData& data, int selectedIndex )
{
    const int count = RenderTargetPreviewCount( data );
    if ( count <= 0 )
    {
        return -1;
    }

    if ( selectedIndex >= 0 && selectedIndex < count )
    {
        const UIRenderTargetPreviewResource& resource = data.renderTargetPreviews[selectedIndex];
        if ( resource.available && resource.textureHandle != 0 && resource.width > 0 && resource.height > 0 )
        {

            return selectedIndex;
        }
    }
    return FirstAvailableRenderTargetPreview( data );
}


const char* RenderTargetPreviewTypeText( const UIRenderTargetPreviewResource& resource )
{
    if ( resource.depth )
    {
        return "Depth SRV";
    }
    return resource.hdr ? "RGBA16F SRV" : "RGBA8 SRV";
}


UIRect IntersectRect( const UIRect& a, const UIRect& b )
{
    const float left = (std::max)( a.x, b.x );
    const float top = (std::max)( a.y, b.y );
    const float right = (std::min)( a.x + a.w, b.x + b.w );
    const float bottom = (std::min)( a.y + a.h, b.y + b.h );
    if ( right <= left || bottom <= top )
    {
        return {};
    }
    return { left, top, right - left, bottom - top };
}


UIRect FitRectToAspect( const UIRect& bounds, int width, int height )
{
    if ( bounds.w <= 1.0f || bounds.h <= 1.0f || width <= 0 || height <= 0 )
    {
        return bounds;
    }

    const float sourceAspect = static_cast<float>( width ) / static_cast<float>( height );
    float drawW = bounds.w;
    float drawH = bounds.w / sourceAspect;
    if ( drawH > bounds.h )
    {
        drawH = bounds.h;
        drawW = bounds.h * sourceAspect;
    }
    return { bounds.x + ( bounds.w - drawW ) * 0.5f, bounds.y + ( bounds.h - drawH ) * 0.5f, drawW, drawH };
}


void BuildEditorObjectCounterText( const InGameUIFrameData& data, char* out, size_t outSize )
{
    if ( !out || outSize == 0 )
    {
        return;
    }

    const int modelCount = (std::max)( 0, data.modelCount );
    const int modelCapacity = (std::max)( 1, data.modelCapacity );
    snprintf( out, outSize, "Game objects %d / %d", modelCount, modelCapacity );
    out[outSize - 1] = '\0';
}


UIRect TitleButtonGroupBounds( const Chrome::TitleButtonRects& titleButtons )
{
    const float left =
        (std::min)( titleButtons.minimize.x, (std::min)( titleButtons.maximize.x, titleButtons.close.x ) );
    const float top =
        (std::min)( titleButtons.minimize.y, (std::min)( titleButtons.maximize.y, titleButtons.close.y ) );
    const float right = (std::max)( titleButtons.minimize.x + titleButtons.minimize.w,
                                    (std::max)( titleButtons.maximize.x + titleButtons.maximize.w,
                                                titleButtons.close.x + titleButtons.close.w ) );
    const float bottom = (std::max)( titleButtons.minimize.y + titleButtons.minimize.h,
                                     (std::max)( titleButtons.maximize.y + titleButtons.maximize.h,
                                                 titleButtons.close.y + titleButtons.close.h ) );
    return { left, top, right - left, bottom - top };
}


void DrawEditorObjectCounter(
    const UIDrawContext& draw,
    const InGameUIFrameData& data,
    int screenW,
    int screenH,
    const UIRect* avoidBounds
)
{
    if ( !data.editorModeEnabled )
    {
        return;
    }

    char counterText[64] = {};
    BuildEditorObjectCounterText( data, counterText, sizeof( counterText ) );

    constexpr float margin = 14.0f;
    constexpr float fontSize = 12.0f;
    constexpr float padX = 12.0f;
    constexpr float height = 30.0f;
    const float availableW = (std::max)( 1.0f, static_cast<float>( screenW ) - margin * 2.0f );
    const float minW = (std::min)( 140.0f, availableW );
    const float width =
        std::clamp( UIFontMetrics::MeasureText( fontSize, counterText ) + padX * 2.0f, minW, availableW );
    UIRect bounds = { static_cast<float>( screenW ) - margin - width, margin, width, height };

    if ( avoidBounds && IntersectRect( bounds, *avoidBounds ).w > 0.0f )
    {
        bounds.x = (std::max)( margin, avoidBounds->x - 10.0f - width );
        if ( IntersectRect( bounds, *avoidBounds ).w > 0.0f )
        {
            bounds.x = static_cast<float>( screenW ) - margin - width;
            const float maxY = (std::max)( margin, static_cast<float>( screenH ) - margin - height );
            bounds.y = std::clamp( avoidBounds->y + avoidBounds->h + 8.0f, margin, maxY );
        }
    }

    const Style::UIPalette& palette = Style::Palette();
    Style::UIColor fill = palette.windowRaised;
    fill.a = 0.90f;
    draw.RoundedRect(
        bounds.x + 3.0f,
        bounds.y + 4.0f,
        bounds.w,
        bounds.h,
        Style::Radii().control,
        palette.shadow.r,
        palette.shadow.g,
        palette.shadow.b,
        0.24f
    );

    draw.RoundedPanel( bounds, Style::Radii().control, fill, palette.border );
    draw.Text(
        bounds.x + padX,
        bounds.y + 8.0f,
        fontSize,
        palette.textPrimary.r,
        palette.textPrimary.g,
        palette.textPrimary.b,
        counterText
    );
}


void EnsureRenderTargetPreviewResources(
    std::unique_ptr<Rendering::ShaderDX12>& shader,
    uint32_t& dynamicVB,
    const UIRenderContext& render
)
{
    if ( !render.IsReady() )
    {
        return;
    }

    if ( !shader )
    {
        shader = render.assets->CreateShader( *render.resources, "shader.ui_render_target_preview" );
        if ( !shader )
        {
            return;
        }
        shader->Use();
        shader->SetInt( "uTexture", 0 );
    }

    if ( dynamicVB == 0 )
    {
        const int attribs[] = { 2, 2 };
        dynamicVB = render.geometry->CreateDynamicVB( attribs, 2, 6 );
    }
}


void ResetRenderTargetPreviewResources(
    std::unique_ptr<Rendering::ShaderDX12>& shader,
    uint32_t& dynamicVB,
    Rendering::Dx12GeometryOwner* geometry
)
{
    shader.reset();
    if ( dynamicVB != 0 )
    {
        if ( geometry )
        {
            geometry->DestroyDynamicVB( dynamicVB );
        }
        dynamicVB = 0;
    }
}


void DrawRenderTargetPreviewTexture(
    std::unique_ptr<Rendering::ShaderDX12>& shader,
    uint32_t& dynamicVB,
    const UIDrawContext& draw,
    const UIRenderTargetPreviewResource& resource,
    const UIRect& bounds,
    const UIRect& clipBounds,
    const UIRenderContext& render
)
{
    if ( !resource.available || resource.textureHandle == 0 || bounds.w <= 1.0f || bounds.h <= 1.0f ||
         !render.IsReady() )
    {
        return;
    }

    EnsureRenderTargetPreviewResources( shader, dynamicVB, render );
    if ( !shader || dynamicVB == 0 )
    {
        return;
    }


    const UIRect visible = IntersectRect( bounds, clipBounds );
    if ( visible.w <= 1.0f || visible.h <= 1.0f )
    {
        return;
    }

    const float uvLeft = std::clamp( ( visible.x - bounds.x ) / bounds.w, 0.0f, 1.0f );
    const float uvRight = std::clamp( ( visible.x + visible.w - bounds.x ) / bounds.w, 0.0f, 1.0f );
    const float uvTop = std::clamp( ( visible.y - bounds.y ) / bounds.h, 0.0f, 1.0f );
    const float uvBottom = std::clamp( ( visible.y + visible.h - bounds.y ) / bounds.h, 0.0f, 1.0f );
    const float left = draw.TextX( visible.x );
    const float right = draw.TextX( visible.x + visible.w );
    const float top = draw.TextY( visible.y );
    const float bottom = draw.TextY( visible.y + visible.h );
    const float verts[] = {
        left, bottom, uvLeft, uvBottom, right, bottom, uvRight, uvBottom, right, top, uvRight, uvTop,
        left, bottom, uvLeft, uvBottom, right, top,    uvRight, uvTop,    left,  top, uvLeft,  uvTop,
    };

    const Math::Transformation::Matrix4 proj =
        Math::Transformation::Matrix4::Ortho( -draw.HalfW(), draw.HalfW(), -draw.HalfH(), draw.HalfH(), -1.0f, 1.0f );
    Rendering::Dx12TextureOwner& textures = *render.textures;
    Rendering::Dx12GeometryOwner& geometry = *render.geometry;
    const int mode = resource.depth ? 2 : ( resource.hdr ? 1 : 0 );
    shader->Use();
    shader->SetMat4( "uProjection", proj );
    shader->SetInt( "uTexture", 0 );
    shader->SetVec4( "uPreviewParams", static_cast<float>( mode ), 1.0f, 2.2f, 0.0f );
    textures.BindTexture( resource.textureHandle, 0 );
    {
        DRAW_CALL_TRACE_SCOPE( *render.diagnostics, "RenderTargetPreview" );
        geometry.UploadAndDrawDynamicVB( dynamicVB, verts, PREVIEW_RASTER_STATE );
    }
    textures.BindTexture( 0, 0 );
}


int WaterReflectionModeFromData( const InGameUIFrameData& data )
{
    if ( data.waterNoReflect )
    {
        return 2;
    }
    return data.waterRTReflect ? 1 : 0;
}
} // namespace SkullbonezCore::UI::FrameComposition
