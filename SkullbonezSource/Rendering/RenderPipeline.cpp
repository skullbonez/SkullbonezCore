/*
File: SkullbonezSource/Rendering/RenderPipeline.cpp
Purpose:
  Writes diagnostics from the live production frame graph.

Summary:
  RuntimeRenderer supplies the exact graph that scheduled world and UI work.
  This file adds value-only frame outcomes and writes that graph without
  reconstructing pass order or resource intent.

Glossary:
  Live graph: The production RenderGraph whose callbacks recorded this frame.
  Snapshot: Value-only outcomes captured after production callbacks executed.

Invariants:
  - Diagnostics never build a second graph or install marker callbacks.
  - Snapshot equality suppresses redundant disk writes; graph text equality is
    the final guard when the same outcomes use an equivalent schedule.

Related:
  - SkullbonezSource/Rendering/RenderPipeline.h
  - SkullbonezSource/Rendering/RenderGraph.h
  - Agentic/Reference/comment-style-guide.md
*/
#include "RenderPipeline.h"

#include "RenderGraph.h"

#include <filesystem>
#include <fstream>
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
           lhs.shadowPassExecuted == rhs.shadowPassExecuted && lhs.reflectionPassExecuted == rhs.reflectionPassExecuted &&
           lhs.reflectionUsedDxr == rhs.reflectionUsedDxr && lhs.objectOpaquePass == rhs.objectOpaquePass &&
           lhs.objectTransparentPass == rhs.objectTransparentPass && lhs.terrainPassRendered == rhs.terrainPassRendered &&
           lhs.waterPassRendered == rhs.waterPassRendered && lhs.waterSamplesReflection == rhs.waterSamplesReflection &&
           lhs.worldExtensionRendered == rhs.worldExtensionRendered &&
           lhs.volumetricPassExecuted == rhs.volumetricPassExecuted && lhs.volumetricReady == rhs.volumetricReady &&
           lhs.volumetricTextureHandle == rhs.volumetricTextureHandle && lhs.volumetricWidth == rhs.volumetricWidth &&
           lhs.volumetricHeight == rhs.volumetricHeight;
}


uint64_t FrameGraphShapeFingerprint( const RenderGraph& graph )
{

    // Why: DumpText allocates. Hash only stable schedule/resource vocabulary so
    // unchanged frames return before constructing diagnostic strings; native
    // addresses are deliberately excluded because swap-chain rotation is not a
    // graph-shape change.
    uint64_t hash = 1469598103934665603ull;
    const auto appendByte = [&]( uint8_t value )
    {
        hash ^= value;

        hash *= 1099511628211ull;
    };

    const auto appendName = [&]( const char* name )
    {

        for ( const unsigned char* cursor = reinterpret_cast<const unsigned char*>( name ); cursor && *cursor;

              ++cursor )
        {
            appendByte( *cursor );
        }

        appendByte( 0xffu );
    };

    const auto appendU32 = [&]( uint32_t value )
    {
        appendByte( static_cast<uint8_t>( value ) );

        appendByte( static_cast<uint8_t>( value >> 8u ) );
        appendByte( static_cast<uint8_t>( value >> 16u ) );
        appendByte( static_cast<uint8_t>( value >> 24u ) );
    };

    for ( const RenderGraphResourceDesc& resource : graph.Resources() )
    {
        appendName( resource.name );
        appendByte( static_cast<uint8_t>( resource.initialAccess ) );
        appendByte( resource.external ? 1u : 0u );
        appendByte( static_cast<uint8_t>( resource.transient.kind ) );
        appendByte( static_cast<uint8_t>( resource.transient.format ) );
        appendU32( resource.transient.width );
        appendU32( resource.transient.height );
        appendU32( resource.transient.mipLevels );
        appendByte( resource.transient.descriptors.renderTarget ? 1u : 0u );
        appendByte( resource.transient.descriptors.depthStencil ? 1u : 0u );
        appendByte( resource.transient.descriptors.shaderResource ? 1u : 0u );
        appendByte( resource.transient.descriptors.unorderedAccess ? 1u : 0u );
    }

    for ( const RenderGraphPassDesc& pass : graph.Passes() )
    {
        appendName( pass.name );
        appendName( pass.debugLabel );
        appendByte( static_cast<uint8_t>( pass.executionOwner ) );
        appendByte( pass.callbackEnabled ? 1u : 0u );
        appendByte( static_cast<uint8_t>( pass.queue ) );

        for ( const RenderGraphResourceUse& read : pass.reads )
        {
            appendByte( static_cast<uint8_t>( read.resource.index ) );
            appendByte( static_cast<uint8_t>( read.access ) );
            appendU32( read.subresource );
        }

        appendByte( 0xfeu );

        for ( const RenderGraphResourceUse& write : pass.writes )
        {
            appendByte( static_cast<uint8_t>( write.resource.index ) );
            appendByte( static_cast<uint8_t>( write.access ) );
            appendU32( write.subresource );
        }

        appendByte( 0xfdu );
    }

    return hash;
}

} // namespace


std::string RenderPipeline::BuildExecutedFrameGraphText( const RenderGraph& graph, const RenderSceneSnapshot& snapshot )
{
    std::ostringstream out;
    out << "ActualExecutedFrameGraph\n";
    out << "cinematic_render=" << ( snapshot.cinematicRender ? "true" : "false" ) << "\n";
    out << "use_cinematic_target=" << ( snapshot.useCinematicTarget ? "true" : "false" ) << "\n";
    out << "terrain_shadow_valid=" << ( snapshot.terrainShadowValid ? "true" : "false" ) << "\n";
    out << "object_shadow_valid=" << ( snapshot.objectShadowValid ? "true" : "false" ) << "\n";
    out << "shadow_pass_executed=" << ( snapshot.shadowPassExecuted ? "true" : "false" ) << "\n";
    out << "reflection_pass_executed=" << ( snapshot.reflectionPassExecuted ? "true" : "false" ) << "\n";
    out << "reflection_path=" << ( snapshot.reflectionUsedDxr ? "DXR" : "Raster" ) << "\n";
    out << "object_opaque_pass=" << ( snapshot.objectOpaquePass ? "true" : "false" ) << "\n";
    out << "object_transparent_pass=" << ( snapshot.objectTransparentPass ? "true" : "false" ) << "\n";
    out << "terrain_pass_rendered=" << ( snapshot.terrainPassRendered ? "true" : "false" ) << "\n";
    out << "water_pass_rendered=" << ( snapshot.waterPassRendered ? "true" : "false" ) << "\n";
    out << "water_samples_reflection=" << ( snapshot.waterSamplesReflection ? "true" : "false" ) << "\n";
    out << "world_extension_rendered=" << ( snapshot.worldExtensionRendered ? "true" : "false" ) << "\n";
    out << "volumetric_pass_executed=" << ( snapshot.volumetricPassExecuted ? "true" : "false" ) << "\n";
    out << "volumetric_ready=" << ( snapshot.volumetricReady ? "true" : "false" ) << "\n";
    out << "volumetric_texture_handle=" << snapshot.volumetricTextureHandle << "\n";
    out << "volumetric_size=" << snapshot.volumetricWidth << "x" << snapshot.volumetricHeight << "\n\n";
    out << graph.DumpText();
    return out.str();
}


void RenderPipeline::DumpExecutedFrameGraphIfChanged( const RenderGraph& graph, const RenderSceneSnapshot& snapshot )
{
    static bool hasLastSnapshot = false;
    static RenderSceneSnapshot lastSnapshot;
    static uint64_t lastGraphFingerprint = 0;

    const uint64_t graphFingerprint = FrameGraphShapeFingerprint( graph );

    if ( hasLastSnapshot && IsSameSnapshot( snapshot, lastSnapshot ) && graphFingerprint == lastGraphFingerprint )
    {
        return;
    }

    const std::string dumpText = BuildExecutedFrameGraphText( graph, snapshot );
    static std::string lastDumpText;
    lastSnapshot = snapshot;
    lastGraphFingerprint = graphFingerprint;
    hasLastSnapshot = true;

    if ( dumpText == lastDumpText )
    {
        return;
    }

    std::error_code ec;
    std::filesystem::create_directories( "Debug", ec );
    std::ofstream file( "Debug/dx12_frame_graph_actual.txt", std::ios::binary );

    if ( file.is_open() )
    {
        file << dumpText << "\n";
        lastDumpText = dumpText;
    }
}

} // namespace Rendering
} // namespace SkullbonezCore
