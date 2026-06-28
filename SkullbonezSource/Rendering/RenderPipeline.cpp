/*
File: SkullbonezSource/Rendering/RenderPipeline.cpp
Purpose:
  Builds and writes render pipeline diagnostics from scene snapshots.

Mental model:
  Runtime reports what actually executed. This file turns those frame facts into
  a renderer-owned graph declaration without depending on Run internals.

Glossary:
  Back buffer: Swap-chain image that will be presented to the window.
  DXR (DirectX Raytracing): DX12 raytracing path used for water reflections.
  HDR (High Dynamic Range): Floating-point scene target used before tonemap.

Invariants:
  - Pass order in this graph mirrors the live order in RunRender.cpp.
  - Snapshot equality suppresses redundant disk writes; dump equality is a
    second guard against churn when equivalent graph text is produced.

Related:
  - SkullbonezSource/Rendering/RenderPipeline.h
  - SkullbonezSource/Rendering/RenderGraph.h
  - Agentic/Reference/comment-style-guide.md
*/
#include "RenderPipeline.h"

#include "RenderGraph.h"

#include <fstream>
#include <filesystem>
#include <sstream>

namespace SkullbonezCore
{
namespace Rendering
{
namespace
{

bool IsSameSnapshot( const RenderSceneSnapshot& lhs, const RenderSceneSnapshot& rhs )
{
    return lhs.cinematicRender == rhs.cinematicRender && lhs.useCinematicTarget == rhs.useCinematicTarget &&
           lhs.terrainShadowValid == rhs.terrainShadowValid && lhs.objectShadowValid == rhs.objectShadowValid &&
           lhs.reflectionUsedDxr == rhs.reflectionUsedDxr && lhs.objectOpaquePass == rhs.objectOpaquePass &&
           lhs.objectTransparentPass == rhs.objectTransparentPass &&
           lhs.terrainPassRendered == rhs.terrainPassRendered && lhs.waterPassRendered == rhs.waterPassRendered &&
           lhs.waterSamplesReflection == rhs.waterSamplesReflection &&
           lhs.shadowCallbackOwned == rhs.shadowCallbackOwned && lhs.skyboxCallbackOwned == rhs.skyboxCallbackOwned &&
           lhs.reflectionCallbackOwned == rhs.reflectionCallbackOwned &&
           lhs.sceneTargetCallbackOwned == rhs.sceneTargetCallbackOwned &&
           lhs.objectOpaqueCallbackOwned == rhs.objectOpaqueCallbackOwned &&
           lhs.objectTransparentCallbackOwned == rhs.objectTransparentCallbackOwned &&
           lhs.terrainCallbackOwned == rhs.terrainCallbackOwned && lhs.waterCallbackOwned == rhs.waterCallbackOwned &&
           lhs.tornadoVisualCallbackOwned == rhs.tornadoVisualCallbackOwned &&
           lhs.replayGhostCallbackOwned == rhs.replayGhostCallbackOwned &&
           lhs.debugOverlayCallbackOwned == rhs.debugOverlayCallbackOwned &&
           lhs.volumetricCallbackOwned == rhs.volumetricCallbackOwned && lhs.volumetricReady == rhs.volumetricReady &&
           lhs.tonemapCallbackOwned == rhs.tonemapCallbackOwned;
}


void DiagnosticCallbackMarker( const RenderGraphPassContext& /*context*/, void* /*userData*/ )
{
    // Diagnostics build a fresh graph from the immutable frame snapshot after
    // rendering. The no-op callback records ownership in the dump without
    // reaching back into live runtime pass state.
}


bool FrameGraphDumpExists()
{
    std::error_code ec;
    return std::filesystem::exists( "Debug/dx12_frame_graph_actual.txt", ec );
}

} // namespace


std::string RenderPipeline::BuildExecutedFrameGraphText( const RenderSceneSnapshot& snapshot )
{
    RenderGraph graph;
    const RenderGraphResourceHandle backbuffer =
        graph.AddExternalResource( "SwapchainBackbuffer", RenderGraphResourceAccess::Present );
    const RenderGraphResourceHandle mainDepth =
        graph.AddExternalResource( "MainDepthStencil", RenderGraphResourceAccess::DepthWrite );

    RenderGraphResourceHandle terrainShadow;
    RenderGraphResourceHandle objectShadow;
    RenderGraphResourceHandle rasterReflectionColor;
    RenderGraphResourceHandle rasterReflectionDepth;
    RenderGraphResourceHandle dxrReflection;
    RenderGraphResourceHandle sceneColor;
    RenderGraphResourceHandle sceneDepth;
    RenderGraphResourceHandle volumetricLight;

    if ( snapshot.terrainShadowValid )
    {
        terrainShadow =
            graph.AddExternalResource( "TerrainShadowMapDepth", RenderGraphResourceAccess::PixelShaderResource );
    }
    if ( snapshot.objectShadowValid )
    {
        objectShadow =
            graph.AddExternalResource( "ObjectShadowMapDepth", RenderGraphResourceAccess::PixelShaderResource );
    }
    if ( snapshot.reflectionUsedDxr )
    {
        dxrReflection =
            graph.AddExternalResource( "DxrReflectionTexture", RenderGraphResourceAccess::PixelShaderResource );
    }
    else
    {
        rasterReflectionColor =
            graph.AddExternalResource( "RasterReflectionColor", RenderGraphResourceAccess::PixelShaderResource );
        rasterReflectionDepth =
            graph.AddExternalResource( "RasterReflectionDepth", RenderGraphResourceAccess::PixelShaderResource );
    }
    if ( snapshot.useCinematicTarget )
    {
        sceneColor = graph.AddExternalResource( "CinematicSceneColor", RenderGraphResourceAccess::PixelShaderResource );
        // Handoff: the DX12 framebuffer tracks the exact scene-depth state
        // across first frame, resize, and post-chain reads. The diagnostic graph
        // records the concrete pass writes/reads without inventing that source state.
        sceneDepth = graph.AddExternalResource( "CinematicSceneDepth", RenderGraphResourceAccess::Unknown );
        if ( snapshot.volumetricReady )
        {
            volumetricLight =
                graph.AddExternalResource( "VolumetricLight", RenderGraphResourceAccess::PixelShaderResource );
        }
    }

    const auto colorTarget = [&]() -> RenderGraphResourceHandle
    { return snapshot.useCinematicTarget ? sceneColor : backbuffer; };
    const auto depthTarget = [&]() -> RenderGraphResourceHandle
    { return snapshot.useCinematicTarget ? sceneDepth : mainDepth; };

    const auto addTargetWrite = [&]( uint32_t pass )
    {
        graph.AddWrite( pass, colorTarget(), RenderGraphResourceAccess::RenderTarget );
        graph.AddWrite( pass, depthTarget(), RenderGraphResourceAccess::DepthWrite );
    };
    const auto addShadowReads = [&]( uint32_t pass )
    {
        if ( snapshot.terrainShadowValid )
        {
            graph.AddRead( pass, terrainShadow, RenderGraphResourceAccess::PixelShaderResource );
        }
        if ( snapshot.objectShadowValid )
        {
            graph.AddRead( pass, objectShadow, RenderGraphResourceAccess::PixelShaderResource );
        }
    };

    if ( !snapshot.cinematicRender || !snapshot.useCinematicTarget )
    {
        const uint32_t clearPass = graph.AddPass( "BackbufferClear" );
        graph.AddWrite( clearPass, backbuffer, RenderGraphResourceAccess::RenderTarget );
        graph.AddWrite( clearPass, mainDepth, RenderGraphResourceAccess::DepthWrite );
    }

    if ( snapshot.terrainShadowValid || snapshot.objectShadowValid )
    {
        const uint32_t shadowPass = graph.AddPass( "ShadowMapPass" );
        if ( snapshot.terrainShadowValid )
        {
            graph.AddWrite( shadowPass, terrainShadow, RenderGraphResourceAccess::DepthWrite );
        }
        if ( snapshot.objectShadowValid )
        {
            graph.AddWrite( shadowPass, objectShadow, RenderGraphResourceAccess::DepthWrite );
        }
        if ( snapshot.shadowCallbackOwned )
        {
            graph.SetPassCallback( shadowPass, DiagnosticCallbackMarker, nullptr, true, "Frame/Shadows/ShadowMap" );
        }
    }

    if ( !snapshot.cinematicRender )
    {
        const uint32_t skyPass = graph.AddPass( "SkyboxPass" );
        graph.AddWrite( skyPass, backbuffer, RenderGraphResourceAccess::RenderTarget );
        if ( snapshot.skyboxCallbackOwned )
        {
            graph.SetPassCallback( skyPass, DiagnosticCallbackMarker, nullptr, true, "Frame/Render/Skybox" );
        }
    }

    if ( snapshot.reflectionUsedDxr )
    {
        const uint32_t dxrPass = graph.AddPass( "DxrReflectionPass", RenderGraphQueueType::Compute );
        graph.AddWrite( dxrPass, dxrReflection, RenderGraphResourceAccess::UnorderedAccess );
        if ( snapshot.reflectionCallbackOwned )
        {
            graph.SetPassCallback( dxrPass, DiagnosticCallbackMarker, nullptr, true, "Frame/Render/Reflection/DXR" );
        }
    }
    else
    {
        const uint32_t reflectionPass = graph.AddPass( "RasterReflectionPass" );
        if ( snapshot.objectShadowValid )
        {
            graph.AddRead( reflectionPass, objectShadow, RenderGraphResourceAccess::PixelShaderResource );
        }
        graph.AddWrite( reflectionPass, rasterReflectionColor, RenderGraphResourceAccess::RenderTarget );
        graph.AddWrite( reflectionPass, rasterReflectionDepth, RenderGraphResourceAccess::DepthWrite );
        if ( snapshot.reflectionCallbackOwned )
        {
            graph.SetPassCallback( reflectionPass,
                                   DiagnosticCallbackMarker,
                                   nullptr,
                                   true,
                                   "Frame/Render/Reflection/Raster" );
        }
    }

    if ( snapshot.useCinematicTarget )
    {
        const uint32_t sceneBegin = graph.AddPass( "CinematicSceneBegin" );
        graph.AddWrite( sceneBegin, sceneColor, RenderGraphResourceAccess::RenderTarget );
        graph.AddWrite( sceneBegin, sceneDepth, RenderGraphResourceAccess::DepthWrite );
        if ( snapshot.sceneTargetCallbackOwned )
        {
            graph.SetPassCallback( sceneBegin,
                                   DiagnosticCallbackMarker,
                                   nullptr,
                                   true,
                                   "Frame/Render/CinematicSceneBegin" );
        }
    }

    if ( snapshot.objectOpaquePass )
    {
        const uint32_t objectPass = graph.AddPass( "ObjectOpaquePass" );
        if ( snapshot.objectShadowValid )
        {
            graph.AddRead( objectPass, objectShadow, RenderGraphResourceAccess::PixelShaderResource );
        }
        addTargetWrite( objectPass );
        if ( snapshot.objectOpaqueCallbackOwned )
        {
            graph.SetPassCallback( objectPass, DiagnosticCallbackMarker, nullptr, true, "Frame/Render/Objects/Opaque" );
        }
    }

    if ( snapshot.terrainPassRendered )
    {
        const uint32_t terrainPass = graph.AddPass( "TerrainPass" );
        if ( snapshot.terrainShadowValid )
        {
            graph.AddRead( terrainPass, terrainShadow, RenderGraphResourceAccess::PixelShaderResource );
        }
        addTargetWrite( terrainPass );
        if ( snapshot.terrainCallbackOwned )
        {
            graph.SetPassCallback( terrainPass, DiagnosticCallbackMarker, nullptr, true, "Frame/Render/Terrain" );
        }
    }

    if ( snapshot.waterPassRendered )
    {
        const uint32_t waterPass = graph.AddPass( "WaterPass" );
        if ( snapshot.waterSamplesReflection )
        {
            graph.AddRead( waterPass,
                           snapshot.reflectionUsedDxr ? dxrReflection : rasterReflectionColor,
                           RenderGraphResourceAccess::PixelShaderResource );
        }
        addShadowReads( waterPass );
        addTargetWrite( waterPass );
        if ( snapshot.waterCallbackOwned )
        {
            graph.SetPassCallback( waterPass, DiagnosticCallbackMarker, nullptr, true, "Frame/Render/Water" );
        }
    }

    if ( snapshot.tornadoVisualRendered || snapshot.tornadoVisualCallbackOwned )
    {
        const uint32_t tornadoPass = graph.AddPass( "TornadoVisualPass" );
        addTargetWrite( tornadoPass );
        if ( snapshot.tornadoVisualCallbackOwned )
        {
            graph.SetPassCallback( tornadoPass, DiagnosticCallbackMarker, nullptr, true, "Frame/Render/TornadoVisual" );
        }
    }

    if ( snapshot.objectTransparentPass )
    {
        const uint32_t objectPass = graph.AddPass( "ObjectTransparentPass" );
        if ( snapshot.objectShadowValid )
        {
            graph.AddRead( objectPass, objectShadow, RenderGraphResourceAccess::PixelShaderResource );
        }
        addTargetWrite( objectPass );
        if ( snapshot.objectTransparentCallbackOwned )
        {
            graph.SetPassCallback( objectPass,
                                   DiagnosticCallbackMarker,
                                   nullptr,
                                   true,
                                   "Frame/Render/Objects/Transparent" );
        }
    }

    if ( snapshot.replayGhostCallbackOwned )
    {
        const uint32_t replayPass = graph.AddPass( "ReplayPredictionGhostPass" );
        if ( snapshot.objectShadowValid )
        {
            graph.AddRead( replayPass, objectShadow, RenderGraphResourceAccess::PixelShaderResource );
        }
        addTargetWrite( replayPass );
        graph.SetPassCallback( replayPass,
                               DiagnosticCallbackMarker,
                               nullptr,
                               true,
                               "Frame/Render/ReplayPredictionGhosts" );
    }

    const uint32_t debugPass = graph.AddPass( "DebugOverlayPass" );
    addTargetWrite( debugPass );
    if ( snapshot.debugOverlayCallbackOwned )
    {
        graph.SetPassCallback( debugPass, DiagnosticCallbackMarker, nullptr, true, "Frame/Render/DebugOverlay" );
    }

    if ( snapshot.useCinematicTarget && snapshot.volumetricReady )
    {
        const uint32_t volumetricPass = graph.AddPass( "VolumetricLightPass" );
        graph.AddRead( volumetricPass, sceneColor, RenderGraphResourceAccess::PixelShaderResource );
        graph.AddRead( volumetricPass, sceneDepth, RenderGraphResourceAccess::PixelShaderResource );
        graph.AddWrite( volumetricPass, volumetricLight, RenderGraphResourceAccess::RenderTarget );
        if ( snapshot.volumetricCallbackOwned )
        {
            graph.SetPassCallback( volumetricPass,
                                   DiagnosticCallbackMarker,
                                   nullptr,
                                   true,
                                   "Frame/Render/VolumetricLight" );
        }
    }

    if ( snapshot.useCinematicTarget )
    {
        const uint32_t tonemapPass = graph.AddPass( "ToneMapPass" );
        graph.AddRead( tonemapPass, sceneColor, RenderGraphResourceAccess::PixelShaderResource );
        graph.AddRead( tonemapPass, sceneDepth, RenderGraphResourceAccess::PixelShaderResource );
        if ( snapshot.volumetricReady )
        {
            graph.AddRead( tonemapPass, volumetricLight, RenderGraphResourceAccess::PixelShaderResource );
        }
        graph.AddWrite( tonemapPass, backbuffer, RenderGraphResourceAccess::RenderTarget );
        if ( snapshot.tonemapCallbackOwned )
        {
            graph.SetPassCallback( tonemapPass, DiagnosticCallbackMarker, nullptr, true, "Frame/Render/Tonemap" );
        }
    }

    const uint32_t presentPass = graph.AddPass( "Present" );
    graph.AddWrite( presentPass, backbuffer, RenderGraphResourceAccess::Present );

    std::ostringstream out;
    out << "ActualExecutedFrameGraph\n";
    out << "cinematic_render=" << ( snapshot.cinematicRender ? "true" : "false" ) << "\n";
    out << "use_cinematic_target=" << ( snapshot.useCinematicTarget ? "true" : "false" ) << "\n";
    out << "terrain_shadow_valid=" << ( snapshot.terrainShadowValid ? "true" : "false" ) << "\n";
    out << "object_shadow_valid=" << ( snapshot.objectShadowValid ? "true" : "false" ) << "\n";
    out << "reflection_path=" << ( snapshot.reflectionUsedDxr ? "DXR" : "Raster" ) << "\n";
    out << "object_opaque_pass=" << ( snapshot.objectOpaquePass ? "true" : "false" ) << "\n";
    out << "object_transparent_pass=" << ( snapshot.objectTransparentPass ? "true" : "false" ) << "\n";
    out << "terrain_pass_rendered=" << ( snapshot.terrainPassRendered ? "true" : "false" ) << "\n";
    out << "water_pass_rendered=" << ( snapshot.waterPassRendered ? "true" : "false" ) << "\n";
    out << "water_samples_reflection=" << ( snapshot.waterSamplesReflection ? "true" : "false" ) << "\n";
    out << "shadow_callback_owned=" << ( snapshot.shadowCallbackOwned ? "true" : "false" ) << "\n";
    out << "scene_target_callback_owned=" << ( snapshot.sceneTargetCallbackOwned ? "true" : "false" ) << "\n";
    out << "skybox_callback_owned=" << ( snapshot.skyboxCallbackOwned ? "true" : "false" ) << "\n";
    out << "reflection_callback_owned=" << ( snapshot.reflectionCallbackOwned ? "true" : "false" ) << "\n";
    out << "object_opaque_callback_owned=" << ( snapshot.objectOpaqueCallbackOwned ? "true" : "false" ) << "\n";
    out << "object_transparent_callback_owned=" << ( snapshot.objectTransparentCallbackOwned ? "true" : "false" )
        << "\n";
    out << "terrain_callback_owned=" << ( snapshot.terrainCallbackOwned ? "true" : "false" ) << "\n";
    out << "water_callback_owned=" << ( snapshot.waterCallbackOwned ? "true" : "false" ) << "\n";
    out << "tornado_visual_callback_owned=" << ( snapshot.tornadoVisualCallbackOwned ? "true" : "false" ) << "\n";
    out << "replay_ghost_callback_owned=" << ( snapshot.replayGhostCallbackOwned ? "true" : "false" ) << "\n";
    out << "debug_overlay_callback_owned=" << ( snapshot.debugOverlayCallbackOwned ? "true" : "false" ) << "\n";
    out << "volumetric_callback_owned=" << ( snapshot.volumetricCallbackOwned ? "true" : "false" ) << "\n";
    out << "volumetric_ready=" << ( snapshot.volumetricReady ? "true" : "false" ) << "\n";
    out << "tonemap_callback_owned=" << ( snapshot.tonemapCallbackOwned ? "true" : "false" ) << "\n\n";
    out << graph.DumpText();
    return out.str();
}


void RenderPipeline::DumpExecutedFrameGraphIfChanged( const RenderSceneSnapshot& snapshot )
{
    static bool hasLastSnapshot = false;
    static RenderSceneSnapshot lastSnapshot;
    if ( hasLastSnapshot && IsSameSnapshot( snapshot, lastSnapshot ) && FrameGraphDumpExists() )
    {
        return;
    }

    const std::string dumpText = BuildExecutedFrameGraphText( snapshot );
    static std::string lastDumpText;
    if ( dumpText == lastDumpText && FrameGraphDumpExists() )
    {
        return;
    }

    std::ofstream file( "Debug/dx12_frame_graph_actual.txt", std::ios::binary );
    if ( file.is_open() )
    {
        file << dumpText << "\n";
        lastSnapshot = snapshot;
        hasLastSnapshot = true;
        lastDumpText = dumpText;
    }
}

} // namespace Rendering
} // namespace SkullbonezCore
