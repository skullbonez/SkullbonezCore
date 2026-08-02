/*
File: TestRenderGraph.cpp
Purpose:
  Pins device-free ordinary render-graph transition derivation in the main
  doctest lane.

Summary:
  Hand-authored initial states and pass order produce a structured oracle:
  each expected transition follows read-before-write declaration order rather
  than captured compiler output.

Invariants:
  - Every expected row checks graph identity, copied native identity, access
    states, and all-subresource scope.
  - An Unknown entry state suppresses only the first transition; later
    incompatible uses still emit their derived edge.
  - These tests create no graphics device and exercise only the API-neutral
    graph contract.

Related:
  - SkullbonezSource/Rendering/RenderGraph.h
  - SkullbonezSource/Rendering/RenderGraph.cpp
  - Agentic/Reference/engine-glossary.md
*/
#include "../ThirdPtySource/doctest/doctest.h"

#include "../SkullbonezSource/Rendering/RenderGraph.h"

using namespace SkullbonezCore::Rendering;

namespace
{
void CheckTransition( const RenderGraphTransitionDesc& transition, uint32_t expectedPassIndex,
                      RenderGraphResourceHandle expectedResource, RenderGraphNativeResourceToken expectedNativeResource,
                      RenderGraphResourceAccess expectedBefore, RenderGraphResourceAccess expectedAfter )
{
    CHECK( transition.passIndex == expectedPassIndex );
    CHECK( transition.resource.index == expectedResource.index );
    CHECK( transition.nativeResource.value == expectedNativeResource.value );
    CHECK( transition.before == expectedBefore );
    CHECK( transition.after == expectedAfter );
    CHECK( transition.subresource == RENDER_GRAPH_ALL_SUBRESOURCES );
}
} // namespace

TEST_CASE( "Render graph ordinary transitions follow read-before-write declaration order" )
{
    RenderGraph graph;
    const RenderGraphNativeResourceToken aNative { 0xA001u };
    const RenderGraphNativeResourceToken bNative { 0xB001u };
    const RenderGraphNativeResourceToken cNative { 0xC001u };
    const RenderGraphResourceHandle a =
        graph.AddExternalResource( "A", RenderGraphResourceAccess::Unknown, aNative );
    const RenderGraphResourceHandle b =
        graph.AddExternalResource( "B", RenderGraphResourceAccess::PixelShaderResource, bNative );
    const RenderGraphResourceHandle c =
        graph.AddExternalResource( "C", RenderGraphResourceAccess::Unknown, cNative );

    const uint32_t pass0 = graph.AddPass( "P0" );
    graph.AddRead( pass0, b, RenderGraphResourceAccess::PixelShaderResource );
    graph.AddWrite( pass0, a, RenderGraphResourceAccess::RenderTarget );
    graph.AddWrite( pass0, c, RenderGraphResourceAccess::RenderTarget );

    const uint32_t pass1 = graph.AddPass( "P1" );
    graph.AddRead( pass1, a, RenderGraphResourceAccess::PixelShaderResource );
    graph.AddWrite( pass1, b, RenderGraphResourceAccess::RenderTarget );
    graph.AddWrite( pass1, c, RenderGraphResourceAccess::CopyDest );

    const RenderGraphCompileResult compiled = graph.Compile();
    REQUIRE( compiled.transitions.size() == 3u );
    CheckTransition( compiled.transitions[0], pass1, a, aNative, RenderGraphResourceAccess::RenderTarget,
                     RenderGraphResourceAccess::PixelShaderResource );
    CheckTransition( compiled.transitions[1], pass1, b, bNative, RenderGraphResourceAccess::PixelShaderResource,
                     RenderGraphResourceAccess::RenderTarget );
    CheckTransition( compiled.transitions[2], pass1, c, cNative, RenderGraphResourceAccess::RenderTarget,
                     RenderGraphResourceAccess::CopyDest );
}

TEST_CASE( "Render graph repeated identical read emits only the first concrete transition" )
{
    RenderGraph graph;
    const RenderGraphNativeResourceToken nativeResource { 0xD001u };
    const RenderGraphResourceHandle resource =
        graph.AddExternalResource( "D", RenderGraphResourceAccess::CopyDest, nativeResource );

    const uint32_t pass0 = graph.AddPass( "P0" );
    graph.AddRead( pass0, resource, RenderGraphResourceAccess::PixelShaderResource );
    const uint32_t pass1 = graph.AddPass( "P1" );
    graph.AddRead( pass1, resource, RenderGraphResourceAccess::PixelShaderResource );

    const RenderGraphCompileResult compiled = graph.Compile();
    REQUIRE( compiled.transitions.size() == 1u );
    CheckTransition( compiled.transitions[0], pass0, resource, nativeResource, RenderGraphResourceAccess::CopyDest,
                     RenderGraphResourceAccess::PixelShaderResource );
}

TEST_CASE( "Render graph untouched external resource emits no transition" )
{
    RenderGraph graph;
    const RenderGraphNativeResourceToken unusedNative { 0xE001u };
    const RenderGraphNativeResourceToken depthNative { 0xE002u };
    const RenderGraphResourceHandle unused =
        graph.AddExternalResource( "U", RenderGraphResourceAccess::CopySource, unusedNative );
    const RenderGraphResourceHandle depth =
        graph.AddExternalResource( "V", RenderGraphResourceAccess::DepthWrite, depthNative );
    CHECK( unused.IsValid() );

    const uint32_t pass0 = graph.AddPass( "P0" );
    graph.AddRead( pass0, depth, RenderGraphResourceAccess::DepthRead );

    const RenderGraphCompileResult compiled = graph.Compile();
    REQUIRE( compiled.transitions.size() == 1u );
    CheckTransition( compiled.transitions[0], pass0, depth, depthNative, RenderGraphResourceAccess::DepthWrite,
                     RenderGraphResourceAccess::DepthRead );
}

TEST_CASE( "Render graph backbuffer returns to Present after repeated render-target writes" )
{
    RenderGraph graph;
    const RenderGraphNativeResourceToken nativeResource { 0xF001u };
    const RenderGraphResourceHandle backbuffer =
        graph.AddExternalResource( "Backbuffer", RenderGraphResourceAccess::Present, nativeResource );

    const uint32_t pass0 = graph.AddPass( "P0" );
    graph.AddWrite( pass0, backbuffer, RenderGraphResourceAccess::RenderTarget );
    const uint32_t pass1 = graph.AddPass( "P1" );
    graph.AddWrite( pass1, backbuffer, RenderGraphResourceAccess::RenderTarget );
    const uint32_t pass2 = graph.AddPass( "P2" );
    graph.AddWrite( pass2, backbuffer, RenderGraphResourceAccess::Present );

    const RenderGraphCompileResult compiled = graph.Compile();
    REQUIRE( compiled.transitions.size() == 2u );
    CheckTransition( compiled.transitions[0], pass0, backbuffer, nativeResource, RenderGraphResourceAccess::Present,
                     RenderGraphResourceAccess::RenderTarget );
    CheckTransition( compiled.transitions[1], pass2, backbuffer, nativeResource, RenderGraphResourceAccess::RenderTarget,
                     RenderGraphResourceAccess::Present );
}
