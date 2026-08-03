/*
File: TestRenderGraph.cpp
Purpose:
  Pins device-free render-graph transition derivation, transient planning, and
  fatal capacity/lifetime boundaries in the main doctest lane.

Summary:
  Hand-authored initial states, numeric subresources, transient descriptions,
  and pass order produce structured oracles from declared lifetimes and access
  order rather than captured compiler output.

Invariants:
  - Every expected row checks graph identity, copied native identity, access
    states, and exact subresource scope.
  - An Unknown entry state suppresses only the first transition; later
    incompatible uses still emit their derived edge.
  - Numeric overrides converge in insertion order and leave no stale state for
    the following all-resource use.
  - The ninth simultaneously active numeric override terminates through the
    shared Lane F child-process harness.
  - Transient pool reuse requires both non-overlapping lifetimes and exact
    kind, format, dimensions, mip count, and descriptor needs.
  - Every planned transient is released at frame end; unused transients
    terminate through the shared Lane F child-process harness.
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

void CheckLifetime( const RenderGraphResourceLifetimeDesc& lifetime, RenderGraphResourceHandle expectedResource,
                    uint32_t expectedFirstPass, uint32_t expectedLastPass, bool expectedUsed )
{
    CHECK( lifetime.resource.index == expectedResource.index );
    CHECK( lifetime.firstPass == expectedFirstPass );
    CHECK( lifetime.lastPass == expectedLastPass );
    CHECK( lifetime.used == expectedUsed );
}

void CheckTransientAllocation( const RenderGraphTransientAllocationDesc& allocation,
                               RenderGraphResourceHandle expectedResource, uint32_t expectedPoolSlot,
                               uint32_t expectedFirstPass, uint32_t expectedLastPass, uint32_t expectedDescriptorCount,
                               bool expectedReused )
{
    CHECK( allocation.resource.index == expectedResource.index );
    CHECK( allocation.poolSlot == expectedPoolSlot );
    CHECK( allocation.firstPass == expectedFirstPass );
    CHECK( allocation.lastPass == expectedLastPass );
    CHECK( allocation.descriptorCount == expectedDescriptorCount );
    CHECK( allocation.reused == expectedReused );
    CHECK( allocation.releasedAtFrameEnd );
}

RenderGraphTransientResourceDesc MakeFullyDescribedTransient()
{
    // Why: all four logical descriptor bits let each compatibility variant
    // flip exactly one field while retaining a nonzero descriptor set. This
    // CPU compiler test never asks the backend to materialize the synthetic
    // combination.
    RenderGraphTransientResourceDesc desc;
    desc.kind = RenderGraphResourceKind::Texture2D;
    desc.format = RenderGraphResourceFormat::RGBA16F;
    desc.width = 128u;
    desc.height = 64u;
    desc.mipLevels = 1u;
    desc.descriptors.renderTarget = true;
    desc.descriptors.depthStencil = true;
    desc.descriptors.shaderResource = true;
    desc.descriptors.unorderedAccess = true;
    return desc;
}
} // namespace

bool RunRenderGraphFatalCase( const char* caseName )
{
    if ( std::strcmp( caseName, "render-graph-subresource-state-capacity" ) == 0 )
    {
        RenderGraph graph;
        const RenderGraphResourceHandle
            texture = graph.AddExternalResource( "CapacityTexture", RenderGraphResourceAccess::PixelShaderResource );
        constexpr std::array<uint32_t, RENDER_GRAPH_MAX_SUBRESOURCE_STATES_PER_RESOURCE> SUBRESOURCE_ORDER = { 7u, 0u, 6u,
                                                                                                               1u, 5u, 2u,
                                                                                                               4u, 3u };

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

    if ( std::strcmp( caseName, "render-graph-unused-transient" ) == 0 )
    {
        RenderGraph graph;
        (void)graph.AddTransientResource( "Unused", MakeFullyDescribedTransient(), RenderGraphResourceAccess::Unknown );
        (void)graph.Compile();
        return true;
    }

    return false;
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

TEST_CASE( "Render graph transient lifetimes require non-overlap before compatible aliasing" )
{
    RenderGraph graph;
    const RenderGraphResourceHandle unusedExternal = graph.AddExternalResource( "UnusedExternal",
                                                                                RenderGraphResourceAccess::CopySource );
    const RenderGraphTransientResourceDesc desc = MakeFullyDescribedTransient();
    const RenderGraphResourceHandle spanning = graph.AddTransientResource( "Spanning", desc,
                                                                           RenderGraphResourceAccess::Unknown );
    const RenderGraphResourceHandle nested = graph.AddTransientResource( "Nested", desc,
                                                                         RenderGraphResourceAccess::Unknown );
    const RenderGraphResourceHandle disjoint = graph.AddTransientResource( "Disjoint", desc,
                                                                           RenderGraphResourceAccess::Unknown );

    const uint32_t spanningBegin = graph.AddPass( "SpanningBegin" );
    graph.AddWrite( spanningBegin, spanning, RenderGraphResourceAccess::CopyDest );
    const uint32_t nestedOnly = graph.AddPass( "NestedOnly" );
    graph.AddRead( nestedOnly, spanning, RenderGraphResourceAccess::CopySource );
    graph.AddWrite( nestedOnly, nested, RenderGraphResourceAccess::CopyDest );
    const uint32_t spanningEnd = graph.AddPass( "SpanningEnd" );
    graph.AddRead( spanningEnd, spanning, RenderGraphResourceAccess::PixelShaderResource );
    const uint32_t disjointOnly = graph.AddPass( "DisjointOnly" );
    graph.AddWrite( disjointOnly, disjoint, RenderGraphResourceAccess::CopyDest );

    const RenderGraphCompileResult compiled = graph.Compile();
    REQUIRE( compiled.resourceLifetimes.size() == 4u );
    CheckLifetime( compiled.resourceLifetimes[0], unusedExternal, 0u, 0u, false );
    CheckLifetime( compiled.resourceLifetimes[1], spanning, spanningBegin, spanningEnd, true );
    CheckLifetime( compiled.resourceLifetimes[2], nested, nestedOnly, nestedOnly, true );
    CheckLifetime( compiled.resourceLifetimes[3], disjoint, disjointOnly, disjointOnly, true );

    REQUIRE( compiled.transientAllocations.size() == 3u );
    CheckTransientAllocation( compiled.transientAllocations[0], spanning, 0u, spanningBegin, spanningEnd, 4u, false );
    CheckTransientAllocation( compiled.transientAllocations[1], nested, 1u, nestedOnly, nestedOnly, 4u, false );
    CheckTransientAllocation( compiled.transientAllocations[2], disjoint, 0u, disjointOnly, disjointOnly, 4u, true );
    CHECK( compiled.transientDiagnostics.allocationCount == 3u );
    CHECK( compiled.transientDiagnostics.reuseCount == 1u );
    CHECK( compiled.transientDiagnostics.releaseCount == 3u );
    CHECK( compiled.transientDiagnostics.highWaterResources == 2u );
    CHECK( compiled.transientDiagnostics.highWaterDescriptors == 8u );
}

TEST_CASE( "Render graph transient alias compatibility compares every compatibility field" )
{
    RenderGraph graph;
    const RenderGraphTransientResourceDesc baseDesc = MakeFullyDescribedTransient();
    const RenderGraphResourceHandle base = graph.AddTransientResource( "Base", baseDesc,
                                                                       RenderGraphResourceAccess::Unknown );
    const RenderGraphResourceHandle compatible = graph.AddTransientResource( "Compatible", baseDesc,
                                                                             RenderGraphResourceAccess::Unknown );

    std::array<RenderGraphTransientResourceDesc, 9> variants;
    variants.fill( baseDesc );
    variants[0].kind = RenderGraphResourceKind::Buffer;
    variants[1].format = RenderGraphResourceFormat::RGBA8;
    variants[2].width = 129u;
    variants[3].height = 65u;
    variants[4].mipLevels = 2u;
    variants[5].descriptors.renderTarget = false;
    variants[6].descriptors.depthStencil = false;
    variants[7].descriptors.shaderResource = false;
    variants[8].descriptors.unorderedAccess = false;
    constexpr std::array<uint32_t, 9> EXPECTED_DESCRIPTOR_COUNTS = { 4u, 4u, 4u, 4u, 4u, 3u, 3u, 3u, 3u };
    std::array<RenderGraphResourceHandle, 9> variantResources;

    for ( size_t index = 0; index < variants.size(); ++index )
    {
        variantResources[index] = graph.AddTransientResource( "IncompatibleVariant", variants[index],
                                                              RenderGraphResourceAccess::Unknown );
    }

    const uint32_t basePass = graph.AddPass( "Base" );
    graph.AddWrite( basePass, base, RenderGraphResourceAccess::CopyDest );
    const uint32_t compatiblePass = graph.AddPass( "Compatible" );
    graph.AddWrite( compatiblePass, compatible, RenderGraphResourceAccess::CopyDest );
    std::array<uint32_t, 9> variantPasses = {};

    for ( size_t index = 0; index < variantResources.size(); ++index )
    {
        variantPasses[index] = graph.AddPass( "IncompatibleVariant" );
        graph.AddWrite( variantPasses[index], variantResources[index], RenderGraphResourceAccess::CopyDest );
    }

    const RenderGraphCompileResult compiled = graph.Compile();
    REQUIRE( compiled.resourceLifetimes.size() == 11u );
    REQUIRE( compiled.transientAllocations.size() == 11u );
    CheckLifetime( compiled.resourceLifetimes[0], base, basePass, basePass, true );
    CheckLifetime( compiled.resourceLifetimes[1], compatible, compatiblePass, compatiblePass, true );
    CheckTransientAllocation( compiled.transientAllocations[0], base, 0u, basePass, basePass, 4u, false );
    CheckTransientAllocation( compiled.transientAllocations[1], compatible, 0u, compatiblePass, compatiblePass, 4u, true );

    for ( size_t index = 0; index < variantResources.size(); ++index )
    {
        const size_t resultIndex = index + 2u;
        CheckLifetime( compiled.resourceLifetimes[resultIndex], variantResources[index], variantPasses[index],
                       variantPasses[index], true );
        CheckTransientAllocation( compiled.transientAllocations[resultIndex], variantResources[index],
                                  static_cast<uint32_t>( index + 1u ), variantPasses[index], variantPasses[index],
                                  EXPECTED_DESCRIPTOR_COUNTS[index], false );
    }

    CHECK( compiled.transientDiagnostics.allocationCount == 11u );
    CHECK( compiled.transientDiagnostics.reuseCount == 1u );
    CHECK( compiled.transientDiagnostics.releaseCount == 11u );
    CHECK( compiled.transientDiagnostics.highWaterResources == 1u );
    CHECK( compiled.transientDiagnostics.highWaterDescriptors == 4u );

    ExpectRuntimeFatalCase( "render-graph-unused-transient",
                            { "FATAL[RenderGraph]",
                              "Transient resource must be read or written by at least one pass. resourceIndex=0" } );
}
