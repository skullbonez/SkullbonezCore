/*
File: UIFrameComposition.cpp
Purpose:
  Implements stateless UI frame signatures, preview-catalog policy, and
  minimized-window geometry shared by the UI owner and palette units.

Summary:
  UI.cpp owns retained widget state. These functions derive bounded hashes,
  rectangles, labels, and preview selections from explicit frame values without
  retaining a host or frame borrow.

Invariants:
  - Hash field order is part of draw-cache invalidation behavior.
  - Preview helpers clamp every authored/runtime count to fixed UI capacities.
  - Functions retain no UI owner pointer or mutable frame state.
  - Preview helpers expose identities and layout only; Runtime/Render resolves
    current GPU resources and submits them.
  - Capacity hashes cover every owned label and numeric field.

Related:
  - UIFrameComposition.h declares value contracts and constants.
  - UI.cpp owns the surrounding UI frame.
  - Agentic/Reference/engine-glossary.md
*/

#include "UIFrameComposition.h"
#include "../../../UI/UIFontMetrics.h"

#include <cmath>

namespace SkullbonezCore::UI::FrameComposition
{
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
    constexpr float titleLeft = 32.0f;
    const float availableW = (std::max)( 0.0f, minimized.w - titleLeft - MINIMIZED_CAMERA_MODE_GAP - MINIMIZED_RESTORE_W );
    const float comboW = (std::min)( MINIMIZED_CAMERA_MODE_COMBO_W, availableW );
    const float comboRight = (std::max)( minimized.x, minimized.x + minimized.w - MINIMIZED_RESTORE_W );
    return { (std::max)( minimized.x, comboRight - comboW ), minimized.y + 6.0f, comboW, 24.0f };
}


float MinimizedWidthWithCameraModeCombo( const char* title, int screenW )
{
    constexpr float margin = 14.0f;
    constexpr float textSize = 12.5f;
    constexpr float titleLeft = 32.0f;
    const float safeScreenW = static_cast<float>( (std::max)( 1, screenW ) );
    const float marginX = (std::min)( margin, ( safeScreenW - 1.0f ) * 0.5f );
    const float maxW = (std::max)( 1.0f, safeScreenW - marginX * 2.0f );
    const float minW = (std::min)( 154.0f, maxW );
    const float titleW = UIFontMetrics::MeasureText( textSize, title ? title : "" );
    const float desiredW = titleLeft + titleW + MINIMIZED_CAMERA_MODE_GAP + MINIMIZED_CAMERA_MODE_COMBO_W +
                           MINIMIZED_RESTORE_W;

    return std::clamp( desiredW, minW, maxW );
}

// Invariant: the product title clamps completed captures to their requested
// frame and appends only non-default input modes to the bounded output.
void BuildWindowTitle( const InGameUIFrameData& data, char* out, size_t outSize )
{
    if ( outSize == 0 )
    {
        return;
    }

    if ( data.surface.sceneMode && data.surface.sceneName && data.surface.sceneName[0] != '\0' )
    {
        const int displayedFrame = ( data.scene.testComplete && data.scene.targetFrameCount > 0 &&
                                     data.scene.currentFrame > data.scene.targetFrameCount )
                                       ? data.scene.targetFrameCount
                                       : data.scene.currentFrame;

        if ( data.scene.testComplete )
        {
            if ( data.scene.targetFrameCount > 0 )
            {
                snprintf( out, outSize, "%s  %d/%d complete", data.surface.sceneName, displayedFrame,
                          data.scene.targetFrameCount );
            }
            else
            {
                snprintf( out, outSize, "%s  complete", data.surface.sceneName );
            }
        }
        else if ( data.scene.targetFrameCount > 0 )
        {
            snprintf( out, outSize, "%s  %d/%d", data.surface.sceneName, displayedFrame, data.scene.targetFrameCount );
        }
        else
        {
            snprintf( out, outSize, "%s", data.surface.sceneName );
        }
    }
    else
    {
        snprintf( out, outSize, "Skullbonez Core" );
    }

    out[outSize - 1] = '\0';
    const char* runtimeMode = data.surface.runtimeInputModeLabel ? data.surface.runtimeInputModeLabel : "";

    if ( runtimeMode[0] != '\0' && std::strcmp( runtimeMode, "Scene" ) != 0 )
    {
        char base[192] = {};
        strcpy_s( base, sizeof( base ), out );
        snprintf( out, outSize, "%s  [%s]", base, runtimeMode );
        out[outSize - 1] = '\0';
    }
}


void StripMinimizedRuntimeModeSuffix( const InGameUIFrameData& data, char* title, size_t titleSize )
{
    if ( !title || titleSize == 0 )
    {
        return;
    }

    const char* runtimeMode = data.surface.runtimeInputModeLabel ? data.surface.runtimeInputModeLabel : "";

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
    const int count = std::clamp( data.renderTargets.count, 0, UI_RENDER_TARGET_PREVIEW_MAX );
    hash = HashInt( hash, count );

    for ( int i = 0; i < count; ++i )
    {
        const UIRenderTargetPreviewResource& resource = data.renderTargets.previews[i];
        hash = HashTextValue( hash, resource.label );
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
    hash = HashTextValue( hash, data.surface.rendererName );
    hash = HashTextValue( hash, data.surface.sceneName );
    hash = HashInt( hash, data.scene.sceneOptionCount );
    hash = HashInt( hash, data.scene.selectedSceneOption );
    hash = HashInt( hash, data.scene.selectedCineModeSceneOption );

    for ( int i = 0; i < data.scene.sceneOptionCount && data.scene.sceneOptions; ++i )
    {
        hash = HashTextValue( hash, data.scene.sceneOptions[i] );
    }

    hash = HashInt( hash, data.scene.selectedInteractionRecordingOption );
    hash = HashInt( hash, data.scene.interactionRecordingOptionCount );

    for ( int i = 0; i < data.scene.interactionRecordingOptionCount && data.scene.interactionRecordingOptions; ++i )
    {
        hash = HashTextValue( hash, data.scene.interactionRecordingOptions[i] );
    }

    hash = HashInt( hash, data.surface.drawCallsBeforeUI );
    hash = HashInt( hash, data.surface.UIDrawCalls );

    // Invariant: visibility rows are live diagnostics. Hash every field so a
    // retained UI draw cannot display the preceding frame's culling result.
    for ( int viewIndex = 0; viewIndex < static_cast<int>( UIRenderVisibilityView::Count ); ++viewIndex )
    {
        const UIRenderVisibilityViewStats& visibility = data.surface.visibility.views[viewIndex];
        hash = HashInt( hash, visibility.candidates );
        hash = HashInt( hash, visibility.submitted );
        hash = HashInt( hash, visibility.culled );
        hash = HashInt( hash, visibility.draws );
    }

    hash = HashInt( hash, data.diagnostics.reserveCapacityRowCount );

    for ( int rowIndex = 0; data.diagnostics.reserveCapacityRows && rowIndex < data.diagnostics.reserveCapacityRowCount &&
                            rowIndex < UI_RUNTIME_RESERVE_CAPACITY_ROW_MAX;
          ++rowIndex )
    {
        const UIRuntimeReserveCapacityRow& row = data.diagnostics.reserveCapacityRows[rowIndex];
        hash = HashTextValue( hash, row.ownerName );
        hash = HashTextValue( hash, row.capacityReason );
        hash = HashTextValue( hash, row.subsystemName );
        hash = HashInt( hash, row.elementSizeBytes );
        hash = HashInt( hash, row.currentCapacity );
        hash = HashInt( hash, row.liveCount );
        hash = HashInt( hash, row.sessionHighWater );
        hash = HashInt( hash, static_cast<int>( row.residentBytes & 0xFFFFFFFFu ) );
        hash = HashInt( hash, static_cast<int>( row.residentBytes >> 32u ) );
    }

    hash = HashInt( hash, static_cast<int>( data.diagnostics.reserveGrowthEventTotalCount ) );
    hash = HashInt( hash, data.diagnostics.reserveGrowthEventCount );

    for ( int eventIndex = 0;
          eventIndex < data.diagnostics.reserveGrowthEventCount && eventIndex < UI_RUNTIME_RESERVE_GROWTH_EVENT_MAX;
          ++eventIndex )
    {
        const SkullbonezCore::Core::Allocation::RuntimeReserveGrowthEventView& event = data.diagnostics
                                                                                           .reserveGrowthEvents[eventIndex];

        hash = HashTextValue( hash, event.targetName );
        hash = HashInt( hash, event.frameNumber );
        hash = HashInt( hash, event.grantedCapacity );
        hash = HashInt( hash, static_cast<int>( event.bytes ) );
        hash = HashBool( hash, event.granted );
    }

    hash = HashFloat( hash, data.surface.fps );
    hash = HashFloat( hash, data.surface.renderMs, 1000.0f );
    hash = HashFloat( hash, data.surface.physicsMs, 1000.0f );
    hash = HashFloat( hash, data.surface.cpuFrameMs, 1000.0f );
    hash = HashFloat( hash, data.surface.gpuFrameMs, 1000.0f );
    hash = HashFloat( hash, data.surface.workerCoreTotalMs, 1000.0f );
    hash = HashProfilerFrameSnapshot( hash, data.diagnostics.profiler );
    hash = HashInt( hash, data.scene.modelCount );
    hash = HashInt( hash, data.scene.modelCapacity );
    hash = HashInt( hash, data.surface.workerThreadCount );
    hash = HashInt( hash, data.surface.maxWorkerThreadCount );
    hash = HashInt( hash, data.scene.currentFrame );
    hash = HashInt( hash, data.scene.targetFrameCount );
    hash = HashInt( hash, static_cast<int>( data.scene.rngSeed ) );
    hash = HashInt( hash, data.scene.solverBallCount );
    hash = HashInt( hash, data.scene.solverBoxCount );
    hash = HashInt( hash, data.scene.currentSceneIndex );
    hash = HashInt( hash, data.scene.sceneCount );
    hash = HashInt( hash, static_cast<int>( std::round( data.surface.now * 1000.0 ) ) );
    hash = HashBool( hash, data.surface.sceneMode );
    hash = HashBool( hash, data.surface.scenePhysicsEnabled );
    hash = HashBool( hash, data.surface.sceneTextEnabled );
    hash = HashBool( hash, data.surface.textOnly );
    hash = HashBool( hash, data.scene.fixedStep );
    hash = HashBool( hash, data.scene.exitOnComplete );
    hash = HashBool( hash, data.scene.testComplete );
    hash = HashBool( hash, data.surface.vsyncEnabled );
    hash = HashBool( hash, data.surface.pipelineSyncEnabled );
    hash = HashFloat( hash, data.scene.sceneEnergy, 1000.0f );
    hash = HashFloat( hash, data.scene.timeScale, 1000.0f );
    hash = HashBool( hash, data.scene.presentationInterpolation );
    hash = HashBool( hash, data.scene.presentationPinned );
    hash = HashFloat( hash, data.scene.presentationAlpha, 1000.0f );
    hash = HashFloat( hash, data.surface.trackHeight, 1000.0f );
    hash = HashFloat( hash, data.surface.autoCycleInterval, 1000.0f );
    hash = HashFloat( hash, data.world.worldGravity, 1000.0f );
    hash = HashFloat( hash, data.world.worldFluidHeight, 1000.0f );
    hash = HashFloat( hash, data.world.worldFluidDensity, 1000.0f );
    hash = HashInt( hash, static_cast<int>( data.world.physicsDebug.activeFlags ) );
    hash = HashTextValue( hash, data.world.physicsDebug.pipelineStageName );
    hash = HashInt( hash, data.world.physicsDebug.pipelineStageIndex );
    hash = HashInt( hash, data.world.physicsDebug.pipelineStageCount );
    hash = HashFloat( hash, data.world.physicsDebug.alpha, 1000.0f );
    hash = HashFloat( hash, data.world.physicsDebug.contactLinger, 1000.0f );
    hash = HashBool( hash, data.world.physicsSleepEnabled );
    hash = HashBool( hash, data.world.physicsDebug.collisionVisualizer );
    hash = HashBool( hash, data.world.physicsDebug.transparent );
    hash = HashBool( hash, data.world.physicsDebug.broadphase );
    hash = HashBool( hash, data.world.tornadoEnabled );
    hash = HashBool( hash, data.world.tornadoVisualShell );
    hash = HashBool( hash, data.world.tornadoFieldVectors );
    hash = HashBool( hash, data.world.rayCastVisualization );
    hash = HashFloat( hash, data.world.tornadoRadius, 100.0f );
    hash = HashFloat( hash, data.world.tornadoHeight, 100.0f );
    hash = HashFloat( hash, data.world.tornadoInwardAcceleration, 100.0f );
    hash = HashFloat( hash, data.world.tornadoSwirlAcceleration, 100.0f );
    hash = HashFloat( hash, data.world.tornadoLiftAcceleration, 100.0f );
    hash = HashFloat( hash, data.world.rayCastImpulseStrength, 100.0f );
    hash = HashFloat( hash, data.world.launcherProjectileSpeed, 100.0f );
    hash = HashFloat( hash, data.world.terrainFrictionCoeff, 1000.0f );
    hash = HashFloat( hash, data.world.objectFrictionCoeff, 1000.0f );
    hash = HashFloat( hash, data.world.rollingFrictionCoeff, 1000.0f );
    hash = HashBool( hash, data.world.waterFreezeDebug );
    hash = HashBool( hash, data.world.waterFlatDebug );
    hash = HashBool( hash, data.world.terrainHidden );
    hash = HashBool( hash, data.world.waterHidden );
    hash = HashBool( hash, data.world.waterNoReflect );
    hash = HashBool( hash, data.world.waterRTReflect );
    hash = HashBool( hash, data.surface.cameraMouseActive );
    hash = HashBool( hash, data.surface.nativeCursorVisible );
    hash = HashTextValue( hash, data.surface.runtimeInputModeLabel );
    hash = HashInt( hash, data.surface.cameraModeIndex );

    hash = HashInt( hash, static_cast<int>( data.surface.cameraModeEnabledMask ) );
    hash = HashBool( hash, data.editor.editorModeEnabled );
    hash = HashBool( hash, data.editor.editorPlacementMode );
    hash = HashBool( hash, data.editor.editorPlaceStatic );
    hash = HashBool( hash, data.editor.editorTerrainAlign );
    hash = HashBool( hash, data.editor.editorViewportLookActive );
    hash = HashInt( hash, data.editor.editorObjectType );
    hash = HashInt( hash, data.editor.editorUndoDepth );
    hash = HashInt( hash, data.editor.editorRedoDepth );
    hash = HashBool( hash, data.scene.canSaveSceneDefaults );
    hash = HashBool( hash, data.rendering.cinematicRendering );
    hash = HashBool( hash, data.rendering.ordinaryRender.shadow.enabled );
    hash = HashFloat( hash, data.rendering.ordinaryRender.sunIntensity, 1000.0f );
    hash = HashFloat( hash, data.rendering.ordinaryRender.sunColorR, 1000.0f );
    hash = HashFloat( hash, data.rendering.ordinaryRender.sunColorG, 1000.0f );
    hash = HashFloat( hash, data.rendering.ordinaryRender.sunColorB, 1000.0f );
    hash = HashFloat( hash, data.rendering.ordinaryRender.ambientStrength, 1000.0f );
    hash = HashFloat( hash, data.rendering.ordinaryRender.skyAmbientR, 1000.0f );
    hash = HashFloat( hash, data.rendering.ordinaryRender.skyAmbientG, 1000.0f );
    hash = HashFloat( hash, data.rendering.ordinaryRender.skyAmbientB, 1000.0f );
    hash = HashFloat( hash, data.rendering.ordinaryRender.groundAmbientR, 1000.0f );
    hash = HashFloat( hash, data.rendering.ordinaryRender.groundAmbientG, 1000.0f );
    hash = HashFloat( hash, data.rendering.ordinaryRender.groundAmbientB, 1000.0f );
    hash = HashFloat( hash, data.rendering.ordinaryRender.shadow.strength, 1000.0f );
    hash = HashFloat( hash, data.rendering.ordinaryRender.shadow.softness, 1000.0f );
    hash = HashFloat( hash, data.rendering.ordinaryRender.shadow.depthBias, 100000.0f );
    hash = HashFloat( hash, data.rendering.ordinaryRender.shadow.slopeBias, 100000.0f );
    hash = HashFloat( hash, data.rendering.ordinaryRender.waterTintR, 1000.0f );
    hash = HashFloat( hash, data.rendering.ordinaryRender.waterTintG, 1000.0f );
    hash = HashFloat( hash, data.rendering.ordinaryRender.waterTintB, 1000.0f );
    hash = HashFloat( hash, data.rendering.ordinaryRender.waterAlpha, 1000.0f );
    hash = HashFloat( hash, data.rendering.ordinaryRender.waterReflectionStrength, 1000.0f );
    hash = HashFloat( hash, data.rendering.ordinaryRender.waterFresnelF0, 10000.0f );
    hash = HashFloat( hash, data.rendering.ordinaryRender.ballRoughnessScale, 1000.0f );
    hash = HashFloat( hash, data.rendering.ordinaryRender.ballSpecularScale, 1000.0f );
    hash = HashFloat( hash, data.rendering.ordinaryRender.boxRoughnessScale, 1000.0f );
    hash = HashFloat( hash, data.rendering.ordinaryRender.boxSpecularScale, 1000.0f );
    hash = HashFloat( hash, data.rendering.ordinaryRender.replayTrajectory.futureWidth, 1000.0f );
    hash = HashFloat( hash, data.rendering.ordinaryRender.replayTrajectory.futureAlpha, 1000.0f );
    hash = HashFloat( hash, data.rendering.ordinaryRender.replayTrajectory.futureEdgeFeather, 1000.0f );
    hash = HashFloat( hash, data.rendering.ordinaryRender.replayTrajectory.causalWidth, 1000.0f );
    hash = HashFloat( hash, data.rendering.ordinaryRender.replayTrajectory.causalAlpha, 1000.0f );
    hash = HashFloat( hash, data.rendering.ordinaryRender.replayTrajectory.causalEdgeFeather, 1000.0f );
    hash = HashFloat( hash, data.rendering.ordinaryRender.replayTrajectory.baselineWidth, 1000.0f );
    hash = HashFloat( hash, data.rendering.ordinaryRender.replayTrajectory.baselineAlpha, 1000.0f );
    hash = HashFloat( hash, data.rendering.ordinaryRender.replayTrajectory.baselineEdgeFeather, 1000.0f );
    hash = HashFloat( hash, data.rendering.ordinaryRender.replayTrajectory.markerWidth, 1000.0f );
    hash = HashFloat( hash, data.rendering.ordinaryRender.replayTrajectory.markerAlpha, 1000.0f );
    hash = HashFloat( hash, data.rendering.ordinaryRender.replayTrajectory.markerEdgeFeather, 1000.0f );
    hash = HashFloat( hash, data.rendering.ordinaryRender.replayTrajectory.selectedEmphasis, 1000.0f );
    hash = HashBool( hash, data.rendering.cinematic.enabled );
    hash = HashBool( hash, data.rendering.cinematic.skyAtmosphereEnabled );
    hash = HashBool( hash, data.rendering.cinematic.cloudsEnabled );
    hash = HashBool( hash, data.rendering.cinematic.godRaysEnabled );
    hash = HashBool( hash, data.rendering.cinematic.volumetricLightingEnabled );
    hash = HashBool( hash, data.rendering.cinematic.bloomEnabled );
    hash = HashBool( hash, data.rendering.cinematic.fogEnabled );
    hash = HashBool( hash, data.rendering.cinematic.terrainReliefEnabled );
    hash = HashBool( hash, data.rendering.cinematic.shadow.enabled );
    hash = HashFloat( hash, data.rendering.cinematic.exposure, 1000.0f );
    hash = HashFloat( hash, data.rendering.cinematic.gamma, 1000.0f );
    hash = HashFloat( hash, data.rendering.cinematic.sunAzimuth, 1000.0f );
    hash = HashFloat( hash, data.rendering.cinematic.sunElevation, 1000.0f );
    hash = HashFloat( hash, data.rendering.cinematic.sunColorR, 1000.0f );
    hash = HashFloat( hash, data.rendering.cinematic.sunColorG, 1000.0f );
    hash = HashFloat( hash, data.rendering.cinematic.sunColorB, 1000.0f );
    hash = HashFloat( hash, data.rendering.cinematic.sunIntensity, 100.0f );
    hash = HashFloat( hash, data.rendering.cinematic.skyGlowStrength, 1000.0f );
    hash = HashFloat( hash, data.rendering.cinematic.cloudCoverage, 1000.0f );
    hash = HashFloat( hash, data.rendering.cinematic.cloudSoftness, 1000.0f );
    hash = HashFloat( hash, data.rendering.cinematic.cloudScale, 1000.0f );
    hash = HashFloat( hash, data.rendering.cinematic.cloudIntensity, 1000.0f );
    hash = HashFloat( hash, data.rendering.cinematic.sunShaftStrength, 1000.0f );
    hash = HashFloat( hash, data.rendering.cinematic.sunShaftFalloff, 1000.0f );
    hash = HashFloat( hash, data.rendering.cinematic.volumetricStrength, 1000.0f );
    hash = HashFloat( hash, data.rendering.cinematic.volumetricDensity, 1000.0f );
    hash = HashFloat( hash, data.rendering.cinematic.volumetricDecay, 1000.0f );
    hash = HashFloat( hash, data.rendering.cinematic.bloomThreshold, 1000.0f );
    hash = HashFloat( hash, data.rendering.cinematic.bloomKnee, 1000.0f );
    hash = HashFloat( hash, data.rendering.cinematic.bloomStrength, 1000.0f );
    hash = HashFloat( hash, data.rendering.cinematic.bloomRadius, 1000.0f );
    hash = HashFloat( hash, data.rendering.cinematic.terrainRelief, 1000.0f );
    hash = HashFloat( hash, data.rendering.cinematic.basinDepth, 100.0f );
    hash = HashFloat( hash, data.rendering.cinematic.basinRimLift, 100.0f );
    hash = HashFloat( hash, data.rendering.cinematic.fogColorR, 1000.0f );
    hash = HashFloat( hash, data.rendering.cinematic.fogColorG, 1000.0f );
    hash = HashFloat( hash, data.rendering.cinematic.fogColorB, 1000.0f );
    hash = HashFloat( hash, data.rendering.cinematic.fogStart, 10.0f );
    hash = HashFloat( hash, data.rendering.cinematic.fogEnd, 10.0f );
    hash = HashFloat( hash, data.rendering.cinematic.fogDensity, 100000.0f );
    hash = HashFloat( hash, data.rendering.cinematic.fogMaxOpacity, 1000.0f );
    hash = HashInt( hash, data.rendering.cinematic.skyMode );
    hash = HashInt( hash, data.rendering.cinematic.terrainMode );
    hash = HashInt( hash, data.rendering.cinematic.objectStyle );
    hash = HashInt( hash, data.rendering.cinematic.waterMode );
    hash = HashFloat( hash, data.rendering.cinematic.styleSaturation, 1000.0f );
    hash = HashFloat( hash, data.rendering.cinematic.styleContrast, 1000.0f );
    hash = HashFloat( hash, data.rendering.cinematic.styleVignette, 1000.0f );
    hash = HashFloat( hash, data.rendering.cinematic.terrainTintR, 1000.0f );
    hash = HashFloat( hash, data.rendering.cinematic.terrainTintG, 1000.0f );
    hash = HashFloat( hash, data.rendering.cinematic.terrainTintB, 1000.0f );
    hash = HashFloat( hash, data.rendering.cinematic.terrainAccentR, 1000.0f );
    hash = HashFloat( hash, data.rendering.cinematic.terrainAccentG, 1000.0f );
    hash = HashFloat( hash, data.rendering.cinematic.terrainAccentB, 1000.0f );
    hash = HashFloat( hash, data.rendering.cinematic.terrainGridScale, 100.0f );
    hash = HashFloat( hash, data.rendering.cinematic.terrainGridStrength, 1000.0f );
    hash = HashFloat( hash, data.rendering.cinematic.waterTintR, 1000.0f );
    hash = HashFloat( hash, data.rendering.cinematic.waterTintG, 1000.0f );
    hash = HashFloat( hash, data.rendering.cinematic.waterTintB, 1000.0f );
    hash = HashFloat( hash, data.rendering.cinematic.waterAlpha, 1000.0f );
    hash = HashFloat( hash, data.rendering.cinematic.waterReflectionStrength, 1000.0f );
    hash = HashFloat( hash, data.rendering.cinematic.waterGlintStrength, 1000.0f );
    hash = HashFloat( hash, data.rendering.cinematic.basinCenterX, 10.0f );
    hash = HashFloat( hash, data.rendering.cinematic.basinCenterZ, 10.0f );
    hash = HashFloat( hash, data.rendering.cinematic.basinRadiusX, 10.0f );
    hash = HashFloat( hash, data.rendering.cinematic.basinRadiusZ, 10.0f );
    hash = HashFloat( hash, data.rendering.cinematic.basinFeather, 1000.0f );
    hash = HashRenderTargetPreviewCatalog( hash, data );
    return hash;
}


int UIInteractionSignatureInput::LocalPointerX() const noexcept
{
    return static_cast<int>( std::lround( static_cast<float>( pointer.x ) - windowBounds.x ) );
}


int UIInteractionSignatureInput::LocalPointerY() const noexcept
{
    return static_cast<int>( std::lround( static_cast<float>( pointer.y ) - windowBounds.y ) );
}


uint32_t BuildUIInteractionSignature( const UIInteractionSignatureInput& input )
{
    uint32_t hash = 2166136261u;
    // Invariant: pointer interaction is window-local. Moving a captured window
    // and pointer together changes only position, so retained commands replay.
    hash = HashInt( hash, input.LocalPointerX() );
    hash = HashInt( hash, input.LocalPointerY() );
    for ( uint32_t control = UI_INTERACTION_RENDERER_OPEN; control <= UI_INTERACTION_CAMERA_MODE_OPEN; control <<= 1u )
    {
        hash = HashBool( hash, ( input.openControls & control ) != 0u );
    }
    hash = HashInt( hash, input.selectedRenderTarget );
    hash = HashInt( hash, input.activeSlider );
    return hash;
}


int RenderTargetPreviewCount( const InGameUIFrameData& data )
{
    return std::clamp( data.renderTargets.count, 0, UI_RENDER_TARGET_PREVIEW_MAX );
}


uint32_t RenderTargetPreviewDisabledMask( const InGameUIFrameData& data )
{
    uint32_t mask = 0;
    const int count = RenderTargetPreviewCount( data );

    for ( int i = 0; i < count; ++i )
    {
        const UIRenderTargetPreviewResource& resource = data.renderTargets.previews[i];

        if ( !resource.available || resource.width <= 0 || resource.height <= 0 )
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
        const UIRenderTargetPreviewResource& resource = data.renderTargets.previews[i];

        if ( resource.available && resource.width > 0 && resource.height > 0 )
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
        const UIRenderTargetPreviewResource& resource = data.renderTargets.previews[selectedIndex];

        if ( resource.available && resource.width > 0 && resource.height > 0 )
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

    const int modelCount = (std::max)( 0, data.scene.modelCount );
    const int modelCapacity = (std::max)( 1, data.scene.modelCapacity );
    snprintf( out, outSize, "Game objects %d / %d", modelCount, modelCapacity );
    out[outSize - 1] = '\0';
}


UIRect TitleButtonGroupBounds( const Chrome::TitleButtonRects& titleButtons )
{
    const float left = (std::min)( titleButtons.minimize.x, (std::min)( titleButtons.maximize.x, titleButtons.close.x ) );

    const float top = (std::min)( titleButtons.minimize.y, (std::min)( titleButtons.maximize.y, titleButtons.close.y ) );

    const float right = (std::max)( titleButtons.minimize.x + titleButtons.minimize.w,
                                    (std::max)( titleButtons.maximize.x + titleButtons.maximize.w,
                                                titleButtons.close.x + titleButtons.close.w ) );

    const float bottom = (std::max)( titleButtons.minimize.y + titleButtons.minimize.h,
                                     (std::max)( titleButtons.maximize.y + titleButtons.maximize.h,
                                                 titleButtons.close.y + titleButtons.close.h ) );

    return { left, top, right - left, bottom - top };
}


void DrawEditorObjectCounter( const UIDrawContext& draw, const InGameUIFrameData& data, int screenW, int screenH,
                              const UIRect* avoidBounds )
{
    if ( !data.editor.editorModeEnabled )
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
    const float width = std::clamp( UIFontMetrics::MeasureText( fontSize, counterText ) + padX * 2.0f, minW, availableW );

    UIRect bounds = { static_cast<float>( screenW ) - margin - width, margin, width, height };

    if ( avoidBounds && UI::IntersectRect( bounds, *avoidBounds ).w > 0.0f )
    {
        bounds.x = (std::max)( margin, avoidBounds->x - 10.0f - width );

        if ( UI::IntersectRect( bounds, *avoidBounds ).w > 0.0f )
        {
            bounds.x = static_cast<float>( screenW ) - margin - width;
            const float maxY = (std::max)( margin, static_cast<float>( screenH ) - margin - height );
            bounds.y = std::clamp( avoidBounds->y + avoidBounds->h + 8.0f, margin, maxY );
        }
    }

    const Style::UIPalette& palette = Style::Palette();
    Style::UIColor fill = palette.windowRaised;
    fill.a = 0.90f;
    draw.RoundedRect( bounds.x + 3.0f, bounds.y + 4.0f, bounds.w, bounds.h, Style::Radii().control, palette.shadow.r,
                      palette.shadow.g, palette.shadow.b, 0.24f );

    draw.RoundedPanel( bounds, Style::Radii().control, fill, palette.border );
    draw.Text( bounds.x + padX, bounds.y + 8.0f, fontSize, palette.textPrimary.r, palette.textPrimary.g,
               palette.textPrimary.b, counterText );
}


int WaterReflectionModeFromData( const InGameUIFrameData& data )
{
    if ( data.world.waterNoReflect )
    {
        return 2;
    }

    return data.world.waterRTReflect ? 1 : 0;
}
} // namespace SkullbonezCore::UI::FrameComposition
