/*
File: TestRenderGraph.cpp
Purpose:
  Pins device-free render-graph transition derivation and its fatal capacity
  boundary in the main doctest lane.

Summary:
  Hand-authored initial states, numeric subresources, and pass order produce a
  structured oracle: each expected transition follows declaration and stored
  override order rather than captured compiler output.

Invariants:
  - Every expected row checks graph identity, copied native identity, access
    states, and exact subresource scope.
  - An Unknown entry state suppresses only the first transition; later
    incompatible uses still emit their derived edge.
  - Numeric overrides converge in insertion order and leave no stale state for
    the following all-resource use.
  - The ninth simultaneously active numeric override terminates through the
    shared Lane F child-process harness.
  - These tests create no graphics device and exercise only the API-neutral
    graph contract.

Related:
  - SkullbonezSource/Rendering/RenderGraph.h
  - SkullbonezSource/Rendering/RenderGraph.cpp
  - Agentic/Reference/engine-glossary.md
*/
#include "../ThirdPtySource/doctest/doctest.h"

#include "../SkullbonezSource/Rendering/RenderGraph.h"
#include "TestFatalCases.h"

#include <array>
#include <cstring>

using namespace SkullbonezCore::Rendering;

namespace
{
void CheckTransition( const RenderGraphTransitionDesc& transition, uint32_t expectedPassIndex,
                      RenderGraphResourceHandle expectedResource, RenderGraphNativeResourceToken expectedNativeResource,
                      RenderGraphResourceAccess expectedBefore, RenderGraphResourceAccess expectedAfter,
                      uint32_t expectedSubresource = RENDER_GRAPH_ALL_SUBRESOURCES )
{
    CHECK( transition.passIndex == expectedPassIndex );
    CHECK( transition.resource.index == expectedResource.index );
    CHECK( transition.nativeResource.value == expectedNativeResource.value );
    CHECK( transition.before == expectedBefore );
    CHECK( transition.after == expectedAfter );
    CHECK( transition.subresource == expectedSubresource );
}
} // namespace

bool RunRenderGraphFatalCase( const char* caseName )
{
    if ( std::strcmp( caseName, "render-graph-subresource-state-capacity" ) != 0 )
    {
        return false;
    }

    RenderGraph graph;
    const RenderGraphResourceHandle texture = graph.AddExternalResource( "CapacityTexture",
                                                                         RenderGraphResourceAccess::PixelShaderResource );
    constexpr std::array<uint32_t, RENDER_GRAPH_MAX_SUBRESOURCE_STATES_PER_RESOURCE> SUBRESOURCE_ORDER = { 7u, 0u, 6u, 1u,
                                                                                                           5u, 2u, 4u, 3u };

    for ( const uint32_t subresource : SUBRESOURCE_ORDER )
    {
        const uint32_t pass = graph.AddPass( "CreateOverride" );
        graph.AddWrite( pass, texture, RenderGraphResourceAccess::RenderTarget, subresource );
    }

    const uint32_t overflowPass = graph.AddPass( "OverflowOverride" );
    graph.AddWrite( overflowPass, texture, RenderGraphResourceAccess::RenderTarget, 8u );
    (void)graph.Compile();
    return true;
}

TEST_CASE( "Render graph ordinary transitions follow read-before-write declaration order" )
{
    RenderGraph graph;
    const RenderGraphNativeResourceToken aNative { 0xA001u };
    const RenderGraphNativeResourceToken bNative { 0xB001u };
    const RenderGraphNativeResourceToken cNative { 0xC001u };
    const RenderGraphResourceHandle a = graph.AddExternalResource( "A", RenderGraphResourceAccess::Unknown, aNative );
    const RenderGraphResourceHandle b = graph.AddExternalResource( "B", RenderGraphResourceAccess::PixelShaderResource,
                                                                   bNative );
    const RenderGraphResourceHandle c = graph.AddExternalResource( "C", RenderGraphResourceAccess::Unknown, cNative );

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
    const RenderGraphResourceHandle resource = graph.AddExternalResource( "D", RenderGraphResourceAccess::CopyDest,
                                                                          nativeResource );

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
    const RenderGraphResourceHandle unused = graph.AddExternalResource( "U", RenderGraphResourceAccess::CopySource,
                                                                        unusedNative );
    const RenderGraphResourceHandle depth = graph.AddExternalResource( "V", RenderGraphResourceAccess::DepthWrite,
                                                                       depthNative );
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
    const RenderGraphResourceHandle backbuffer = graph.AddExternalResource( "Backbuffer", RenderGraphResourceAccess::Present,
                                                                            nativeResource );

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

TEST_CASE( "Render graph divergent numeric states converge in deterministic stored order" )
{
    RenderGraph graph;
    const RenderGraphNativeResourceToken nativeResource { 0xF101u };
    const RenderGraphResourceHandle texture = graph.AddExternalResource( "DivergentTexture",
                                                                         RenderGraphResourceAccess::PixelShaderResource,
                                                                         nativeResource );

    const uint32_t writeMipFive = graph.AddPass( "WriteMipFive" );
    graph.AddWrite( writeMipFive, texture, RenderGraphResourceAccess::RenderTarget, 5u );
    const uint32_t writeMipTwo = graph.AddPass( "WriteMipTwo" );
    graph.AddWrite( writeMipTwo, texture, RenderGraphResourceAccess::CopyDest, 2u );
    const uint32_t convergeAll = graph.AddPass( "ConvergeAll" );
    graph.AddRead( convergeAll, texture, RenderGraphResourceAccess::PixelShaderResource );
    const uint32_t copyAll = graph.AddPass( "CopyAll" );
    graph.AddRead( copyAll, texture, RenderGraphResourceAccess::CopySource );

    const RenderGraphCompileResult compiled = graph.Compile();
    REQUIRE( compiled.transitions.size() == 5u );
    CheckTransition( compiled.transitions[0], writeMipFive, texture, nativeResource,
                     RenderGraphResourceAccess::PixelShaderResource, RenderGraphResourceAccess::RenderTarget, 5u );
    CheckTransition( compiled.transitions[1], writeMipTwo, texture, nativeResource,
                     RenderGraphResourceAccess::PixelShaderResource, RenderGraphResourceAccess::CopyDest, 2u );
    CheckTransition( compiled.transitions[2], convergeAll, texture, nativeResource, RenderGraphResourceAccess::RenderTarget,
                     RenderGraphResourceAccess::PixelShaderResource, 5u );
    CheckTransition( compiled.transitions[3], convergeAll, texture, nativeResource, RenderGraphResourceAccess::CopyDest,
                     RenderGraphResourceAccess::PixelShaderResource, 2u );
    CheckTransition( compiled.transitions[4], copyAll, texture, nativeResource,
                     RenderGraphResourceAccess::PixelShaderResource, RenderGraphResourceAccess::CopySource );
}

TEST_CASE( "Render graph supports eight active numeric states and rejects the ninth" )
{
    RenderGraph graph;
    const RenderGraphNativeResourceToken nativeResource { 0xF201u };
    const RenderGraphResourceHandle texture = graph.AddExternalResource( "CapacityTexture",
                                                                         RenderGraphResourceAccess::PixelShaderResource,
                                                                         nativeResource );
    constexpr std::array<uint32_t, RENDER_GRAPH_MAX_SUBRESOURCE_STATES_PER_RESOURCE> SUBRESOURCE_ORDER = { 7u, 0u, 6u, 1u,
                                                                                                           5u, 2u, 4u, 3u };

    for ( const uint32_t subresource : SUBRESOURCE_ORDER )
    {
        const uint32_t pass = graph.AddPass( "CreateOverride" );
        graph.AddWrite( pass, texture, RenderGraphResourceAccess::RenderTarget, subresource );
    }

    const uint32_t convergeAll = graph.AddPass( "ConvergeAll" );
    graph.AddRead( convergeAll, texture, RenderGraphResourceAccess::PixelShaderResource );
    const RenderGraphCompileResult compiled = graph.Compile();
    REQUIRE( compiled.transitions.size() == SUBRESOURCE_ORDER.size() * 2u );

    for ( size_t index = 0; index < SUBRESOURCE_ORDER.size(); ++index )
    {
        CheckTransition( compiled.transitions[index], static_cast<uint32_t>( index ), texture, nativeResource,
                         RenderGraphResourceAccess::PixelShaderResource, RenderGraphResourceAccess::RenderTarget,
                         SUBRESOURCE_ORDER[index] );
        CheckTransition( compiled.transitions[index + SUBRESOURCE_ORDER.size()], convergeAll, texture, nativeResource,
                         RenderGraphResourceAccess::RenderTarget, RenderGraphResourceAccess::PixelShaderResource,
                         SUBRESOURCE_ORDER[index] );
    }

    ExpectRuntimeFatalCase( "render-graph-subresource-state-capacity",
                            { "FATAL[RenderGraph]", "Subresource state capacity exceeded. count=8 capacity=8" } );
}
