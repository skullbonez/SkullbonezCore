/*
File: Agentic/Tests/Dx12ArchUnitTests/Dx12ArchUnitTests.cpp
Purpose:
  Contains DX12 architecture checks that guard renderer ownership and dependency boundaries.

Mental model:
  This module is one piece of the engine contract. Read the glossary and
  invariants first, then follow ownership and call direction through the
  related files.

Glossary:
  Validation gate: Repository script that proves a class of changes before
  commit or PR.

Related:
  - AGENTS.md
  - Agentic/Reference/comment-style-guide.md
*/
#include "RenderDeviceDX12.h"
#include "RenderGraph.h"
#include "Dx12RenderGraphExecutor.h"

#include <cstdint>
#include <exception>
#include <functional>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

using namespace SkullbonezCore::Rendering;

namespace
{

struct TestFailure : public std::runtime_error
{
    explicit TestFailure( const std::string& message )
        : std::runtime_error( message )
    {
    }
};

void Fail( const char* file, int line, const std::string& message )
{
    std::ostringstream out;
    out << file << "(" << line << "): " << message;
    throw TestFailure( out.str() );
}

void ExpectTrue( bool value, const char* expression, const char* file, int line )
{
    if ( !value )
    {
        Fail( file, line, std::string( "expected true: " ) + expression );
    }
}

template <typename T, typename U>
void ExpectEqualImpl( const T& actual, const U& expected, const char* actualExpression, const char* expectedExpression, const char* file, int line )
{
    if ( !( actual == expected ) )
    {
        std::ostringstream out;
        out << "expected " << actualExpression << " == " << expectedExpression << ", actual " << actual << ", expected " << expected;
        Fail( file, line, out.str() );
    }
}

void ExpectThrows( const std::function<void()>& callback, const char* expression, const char* file, int line )
{
    try
    {
        callback();
    }
    catch ( const std::exception& )
    {
        return;
    }

    Fail( file, line, std::string( "expected exception from: " ) + expression );
}

#define EXPECT_TRUE( expression ) ExpectTrue( !!( expression ), #expression, __FILE__, __LINE__ )
#define EXPECT_EQ( actual, expected ) ExpectEqualImpl( ( actual ), ( expected ), #actual, #expected, __FILE__, __LINE__ )
#define EXPECT_THROWS( expression ) ExpectThrows( [&]() { expression; }, #expression, __FILE__, __LINE__ )

struct TestCase
{
    const char* name = "";
    void ( *run )() = nullptr;
};

Dx12DescriptorAllocator MakeDescriptorAllocator()
{
    Dx12DescriptorAllocator allocator;
    allocator.Init( reinterpret_cast<ID3D12DescriptorHeap*>( 1 ),
                    reinterpret_cast<ID3D12DescriptorHeap*>( 2 ),
                    32,
                    4,
                    8,
                    2 );
    return allocator;
}

void TestDescriptorTransientRangeIsContiguous()
{
    Dx12DescriptorAllocator allocator = MakeDescriptorAllocator();

    allocator.ResetFrame( 0 );
    EXPECT_EQ( allocator.AllocateTransientRange( 4 ), 4u );
    EXPECT_EQ( allocator.AllocateTransient(), 8u );

    const Dx12DescriptorAllocatorStats frameZeroStats = allocator.GetStats();
    EXPECT_EQ( frameZeroStats.staticCapacity, 4u );
    EXPECT_EQ( frameZeroStats.staticUsed, 0u );
    EXPECT_EQ( frameZeroStats.transientCapacityPerFrame, 8u );
    EXPECT_EQ( frameZeroStats.transientUsedThisFrame, 5u );
    EXPECT_EQ( frameZeroStats.transientPeakThisRun, 5u );
    EXPECT_EQ( frameZeroStats.currentFrame, 0u );

    allocator.ResetFrame( 1 );
    EXPECT_EQ( allocator.AllocateTransientRange( 2 ), 12u );

    const Dx12DescriptorAllocatorStats frameOneStats = allocator.GetStats();
    EXPECT_EQ( frameOneStats.transientUsedThisFrame, 2u );
    EXPECT_EQ( frameOneStats.transientPeakThisRun, 5u );
    EXPECT_EQ( frameOneStats.currentFrame, 1u );
}

void TestDescriptorTransientRangeFailureIsAtomic()
{
    Dx12DescriptorAllocator allocator = MakeDescriptorAllocator();

    allocator.ResetFrame( 0 );
    EXPECT_EQ( allocator.AllocateTransientRange( 7 ), 4u );

    EXPECT_THROWS( allocator.AllocateTransientRange( 2 ) );

    const Dx12DescriptorAllocatorStats afterFailedRange = allocator.GetStats();
    EXPECT_EQ( afterFailedRange.transientUsedThisFrame, 7u );
    EXPECT_EQ( afterFailedRange.transientPeakThisRun, 7u );

    EXPECT_THROWS( allocator.AllocateTransientRange( 0 ) );

    const Dx12DescriptorAllocatorStats afterZeroRange = allocator.GetStats();
    EXPECT_EQ( afterZeroRange.transientUsedThisFrame, 7u );
    EXPECT_EQ( afterZeroRange.transientPeakThisRun, 7u );
}

void TestRenderGraphSkipsUnknownInitialTransition()
{
    RenderGraph graph;
    const RenderGraphResourceHandle legacyTarget = graph.AddExternalResource( "LegacyTarget", RenderGraphResourceAccess::Unknown );

    const uint32_t firstWriter = graph.AddPass( "FirstWriter" );
    graph.AddWrite( firstWriter, legacyTarget, RenderGraphResourceAccess::RenderTarget );

    RenderGraphCompileResult compiled = graph.Compile();
    EXPECT_EQ( compiled.transitions.size(), static_cast<size_t>( 0 ) );

    const uint32_t laterReader = graph.AddPass( "LaterReader" );
    graph.AddRead( laterReader, legacyTarget, RenderGraphResourceAccess::PixelShaderResource );

    compiled = graph.Compile();
    EXPECT_EQ( compiled.transitions.size(), static_cast<size_t>( 1 ) );
    EXPECT_EQ( compiled.transitions[0].passIndex, laterReader );
    EXPECT_EQ( compiled.transitions[0].resource.index, legacyTarget.index );
    EXPECT_TRUE( compiled.transitions[0].before == RenderGraphResourceAccess::RenderTarget );
    EXPECT_TRUE( compiled.transitions[0].after == RenderGraphResourceAccess::PixelShaderResource );
}

void TestRenderGraphExplicitInitialStateTransitions()
{
    RenderGraph graph;
    const RenderGraphResourceHandle backbuffer = graph.AddExternalResource( "Backbuffer", RenderGraphResourceAccess::Present );

    const uint32_t drawPass = graph.AddPass( "Draw" );
    graph.AddWrite( drawPass, backbuffer, RenderGraphResourceAccess::RenderTarget );

    const uint32_t presentPass = graph.AddPass( "Present" );
    graph.AddWrite( presentPass, backbuffer, RenderGraphResourceAccess::Present );

    const RenderGraphCompileResult compiled = graph.Compile();
    EXPECT_EQ( compiled.transitions.size(), static_cast<size_t>( 2 ) );

    EXPECT_EQ( compiled.transitions[0].passIndex, drawPass );
    EXPECT_TRUE( compiled.transitions[0].before == RenderGraphResourceAccess::Present );
    EXPECT_TRUE( compiled.transitions[0].after == RenderGraphResourceAccess::RenderTarget );

    EXPECT_EQ( compiled.transitions[1].passIndex, presentPass );
    EXPECT_TRUE( compiled.transitions[1].before == RenderGraphResourceAccess::RenderTarget );
    EXPECT_TRUE( compiled.transitions[1].after == RenderGraphResourceAccess::Present );
}

void TestRenderGraphTracksSubresourceTransitionsIndependently()
{
    RenderGraph graph;
    const RenderGraphResourceHandle texture = graph.AddExternalResource( "MipTexture", RenderGraphResourceAccess::PixelShaderResource, reinterpret_cast<const void*>( static_cast<uintptr_t>( 0x6000u ) ) );

    const uint32_t writeMipOne = graph.AddPass( "WriteMipOne" );
    graph.AddWrite( writeMipOne, texture, RenderGraphResourceAccess::UnorderedAccess, 1u );

    const uint32_t writeMipTwo = graph.AddPass( "WriteMipTwo" );
    graph.AddWrite( writeMipTwo, texture, RenderGraphResourceAccess::CopyDest, 2u );

    const uint32_t readMipOne = graph.AddPass( "ReadMipOne" );
    graph.AddRead( readMipOne, texture, RenderGraphResourceAccess::PixelShaderResource, 1u );

    const RenderGraphCompileResult compiled = graph.Compile();
    EXPECT_EQ( compiled.transitions.size(), static_cast<size_t>( 3 ) );

    EXPECT_EQ( compiled.transitions[0].passIndex, writeMipOne );
    EXPECT_EQ( compiled.transitions[0].subresource, 1u );
    EXPECT_TRUE( compiled.transitions[0].before == RenderGraphResourceAccess::PixelShaderResource );
    EXPECT_TRUE( compiled.transitions[0].after == RenderGraphResourceAccess::UnorderedAccess );

    EXPECT_EQ( compiled.transitions[1].passIndex, writeMipTwo );
    EXPECT_EQ( compiled.transitions[1].subresource, 2u );
    EXPECT_TRUE( compiled.transitions[1].before == RenderGraphResourceAccess::PixelShaderResource );
    EXPECT_TRUE( compiled.transitions[1].after == RenderGraphResourceAccess::CopyDest );

    EXPECT_EQ( compiled.transitions[2].passIndex, readMipOne );
    EXPECT_EQ( compiled.transitions[2].subresource, 1u );
    EXPECT_TRUE( compiled.transitions[2].before == RenderGraphResourceAccess::UnorderedAccess );
    EXPECT_TRUE( compiled.transitions[2].after == RenderGraphResourceAccess::PixelShaderResource );

    Dx12RenderGraphExecutionDesc desc;
    const Dx12RenderGraphExecutionResult result = ExecuteDx12RenderGraphTransitions( graph, compiled, desc );
    EXPECT_EQ( result.barriers.size(), static_cast<size_t>( 3 ) );
    EXPECT_EQ( result.barriers[0].subresource, 1u );
    EXPECT_EQ( result.barriers[1].subresource, 2u );
    EXPECT_EQ( result.barriers[2].subresource, 1u );
}

void TestRenderGraphAllowsUniformSpecificThenAllSubresourceTransition()
{
    RenderGraph graph;
    const RenderGraphResourceHandle texture = graph.AddExternalResource( "UniformTexture", RenderGraphResourceAccess::PixelShaderResource );

    const uint32_t readMipOne = graph.AddPass( "ReadMipOne" );
    graph.AddRead( readMipOne, texture, RenderGraphResourceAccess::PixelShaderResource, 1u );

    const uint32_t writeAll = graph.AddPass( "WriteAll" );
    graph.AddWrite( writeAll, texture, RenderGraphResourceAccess::RenderTarget );

    const RenderGraphCompileResult compiled = graph.Compile();
    EXPECT_EQ( compiled.transitions.size(), static_cast<size_t>( 1 ) );
    EXPECT_EQ( compiled.transitions[0].passIndex, writeAll );
    EXPECT_EQ( compiled.transitions[0].subresource, RENDER_GRAPH_ALL_SUBRESOURCES );
    EXPECT_TRUE( compiled.transitions[0].before == RenderGraphResourceAccess::PixelShaderResource );
    EXPECT_TRUE( compiled.transitions[0].after == RenderGraphResourceAccess::RenderTarget );
}

void TestRenderGraphClearsSpecificStateWhenItReturnsToAllState()
{
    RenderGraph graph;
    const RenderGraphResourceHandle texture = graph.AddExternalResource( "ReturnedTexture", RenderGraphResourceAccess::PixelShaderResource );

    const uint32_t writeMipOne = graph.AddPass( "WriteMipOne" );
    graph.AddWrite( writeMipOne, texture, RenderGraphResourceAccess::UnorderedAccess, 1u );

    const uint32_t readMipOne = graph.AddPass( "ReadMipOne" );
    graph.AddRead( readMipOne, texture, RenderGraphResourceAccess::PixelShaderResource, 1u );

    const uint32_t writeAll = graph.AddPass( "WriteAll" );
    graph.AddWrite( writeAll, texture, RenderGraphResourceAccess::RenderTarget );

    const RenderGraphCompileResult compiled = graph.Compile();
    EXPECT_EQ( compiled.transitions.size(), static_cast<size_t>( 3 ) );

    EXPECT_EQ( compiled.transitions[0].passIndex, writeMipOne );
    EXPECT_EQ( compiled.transitions[0].subresource, 1u );
    EXPECT_TRUE( compiled.transitions[0].before == RenderGraphResourceAccess::PixelShaderResource );
    EXPECT_TRUE( compiled.transitions[0].after == RenderGraphResourceAccess::UnorderedAccess );

    EXPECT_EQ( compiled.transitions[1].passIndex, readMipOne );
    EXPECT_EQ( compiled.transitions[1].subresource, 1u );
    EXPECT_TRUE( compiled.transitions[1].before == RenderGraphResourceAccess::UnorderedAccess );
    EXPECT_TRUE( compiled.transitions[1].after == RenderGraphResourceAccess::PixelShaderResource );

    EXPECT_EQ( compiled.transitions[2].passIndex, writeAll );
    EXPECT_EQ( compiled.transitions[2].subresource, RENDER_GRAPH_ALL_SUBRESOURCES );
    EXPECT_TRUE( compiled.transitions[2].before == RenderGraphResourceAccess::PixelShaderResource );
    EXPECT_TRUE( compiled.transitions[2].after == RenderGraphResourceAccess::RenderTarget );
}

void TestRenderGraphRejectsMixedSpecificThenAllSubresourceTransition()
{
    RenderGraph graph;
    const RenderGraphResourceHandle texture = graph.AddExternalResource( "MixedTexture", RenderGraphResourceAccess::PixelShaderResource );

    const uint32_t writeMipOne = graph.AddPass( "WriteMipOne" );
    graph.AddWrite( writeMipOne, texture, RenderGraphResourceAccess::UnorderedAccess, 1u );

    const uint32_t writeAll = graph.AddPass( "WriteAll" );
    graph.AddWrite( writeAll, texture, RenderGraphResourceAccess::RenderTarget );

    EXPECT_THROWS( graph.Compile() );
}

void TestRenderGraphRejectsUnknownPassAccess()
{
    RenderGraph graph;
    const RenderGraphResourceHandle texture = graph.AddExternalResource( "Texture", RenderGraphResourceAccess::PixelShaderResource );
    const uint32_t pass = graph.AddPass( "BadPass" );

    EXPECT_THROWS( graph.AddRead( pass, texture, RenderGraphResourceAccess::Unknown ) );
    EXPECT_THROWS( graph.AddWrite( pass, texture, RenderGraphResourceAccess::Unknown ) );
}

void TestRenderGraphRejectsBadHandles()
{
    RenderGraph graph;
    const RenderGraphResourceHandle texture = graph.AddExternalResource( "Texture", RenderGraphResourceAccess::PixelShaderResource );
    const uint32_t pass = graph.AddPass( "Pass" );
    RenderGraphResourceHandle badResource;
    badResource.index = texture.index + 100u;

    EXPECT_THROWS( graph.AddRead( pass, badResource, RenderGraphResourceAccess::PixelShaderResource ) );
    EXPECT_THROWS( graph.AddWrite( pass + 100u, texture, RenderGraphResourceAccess::RenderTarget ) );
}

void TestDx12RenderGraphAccessMapsToDx12States()
{
    D3D12_RESOURCE_STATES state = D3D12_RESOURCE_STATE_COMMON;

    EXPECT_TRUE( TryDx12RenderGraphAccessToResourceState( RenderGraphResourceAccess::Present, state ) );
    EXPECT_TRUE( state == D3D12_RESOURCE_STATE_PRESENT );

    EXPECT_TRUE( TryDx12RenderGraphAccessToResourceState( RenderGraphResourceAccess::RenderTarget, state ) );
    EXPECT_TRUE( state == D3D12_RESOURCE_STATE_RENDER_TARGET );

    EXPECT_TRUE( TryDx12RenderGraphAccessToResourceState( RenderGraphResourceAccess::PixelShaderResource, state ) );
    EXPECT_TRUE( state == D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE );

    EXPECT_TRUE( TryDx12RenderGraphAccessToResourceState( RenderGraphResourceAccess::VertexAndNonPixelShaderResource, state ) );
    EXPECT_TRUE( state == ( D3D12_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER | D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE ) );

    EXPECT_TRUE( !TryDx12RenderGraphAccessToResourceState( RenderGraphResourceAccess::Unknown, state ) );
}

void TestDx12RenderGraphExecutorDryRunBackbufferTransitions()
{
    const void* fakeBackbuffer = reinterpret_cast<const void*>( static_cast<uintptr_t>( 0x1000u ) );

    RenderGraph graph;
    const RenderGraphResourceHandle backbuffer = graph.AddExternalResource( "Backbuffer", RenderGraphResourceAccess::Present, fakeBackbuffer );

    const uint32_t drawPass = graph.AddPass( "Draw" );
    graph.AddWrite( drawPass, backbuffer, RenderGraphResourceAccess::RenderTarget );

    const uint32_t presentPass = graph.AddPass( "Present" );
    graph.AddWrite( presentPass, backbuffer, RenderGraphResourceAccess::Present );

    const RenderGraphCompileResult compiled = graph.Compile();
    Dx12RenderGraphExecutionDesc desc;
    desc.mode = Dx12RenderGraphExecutionMode::DryRun;
    desc.sourcePrefix = "GraphDryRun";

    const Dx12RenderGraphExecutionResult result = ExecuteDx12RenderGraphTransitions( graph, compiled, desc );

    EXPECT_EQ( result.barriers.size(), static_cast<size_t>( 2 ) );
    EXPECT_EQ( result.transitionBarrierCount, static_cast<size_t>( 2 ) );
    EXPECT_EQ( result.emittedTransitionBarrierCount, static_cast<size_t>( 0 ) );
    EXPECT_EQ( result.missingNativeResourceTransitionCount, static_cast<size_t>( 0 ) );

    EXPECT_TRUE( result.barriers[0].beforeState == D3D12_RESOURCE_STATE_PRESENT );
    EXPECT_TRUE( result.barriers[0].afterState == D3D12_RESOURCE_STATE_RENDER_TARGET );
    EXPECT_TRUE( !result.barriers[0].emitted );
    EXPECT_EQ( result.barriers[0].source, std::string( "GraphDryRun:Draw" ) );

    EXPECT_TRUE( result.barriers[1].beforeState == D3D12_RESOURCE_STATE_RENDER_TARGET );
    EXPECT_TRUE( result.barriers[1].afterState == D3D12_RESOURCE_STATE_PRESENT );
    EXPECT_EQ( result.barriers[1].source, std::string( "GraphDryRun:Present" ) );
}

void TestDx12RenderGraphExecutorSkipsUnknownInitialAccess()
{
    RenderGraph graph;
    const RenderGraphResourceHandle legacyTarget = graph.AddExternalResource( "LegacyTarget", RenderGraphResourceAccess::Unknown, reinterpret_cast<const void*>( static_cast<uintptr_t>( 0x2000u ) ) );

    const uint32_t firstWriter = graph.AddPass( "FirstWriter" );
    graph.AddWrite( firstWriter, legacyTarget, RenderGraphResourceAccess::RenderTarget );

    const RenderGraphCompileResult compiled = graph.Compile();
    Dx12RenderGraphExecutionDesc desc;
    const Dx12RenderGraphExecutionResult result = ExecuteDx12RenderGraphTransitions( graph, compiled, desc );

    EXPECT_EQ( compiled.transitions.size(), static_cast<size_t>( 0 ) );
    EXPECT_EQ( result.barriers.size(), static_cast<size_t>( 0 ) );
    EXPECT_EQ( result.unknownStateTransitionCount, static_cast<size_t>( 0 ) );
}

void TestDx12RenderGraphExecutorIdentifiesUavAccess()
{
    RenderGraph graph;
    const RenderGraphResourceHandle reflection = graph.AddExternalResource( "Reflection", RenderGraphResourceAccess::PixelShaderResource, reinterpret_cast<const void*>( static_cast<uintptr_t>( 0x3000u ) ) );

    const uint32_t dispatchPass = graph.AddPass( "DispatchReflection", RenderGraphQueueType::Compute );
    graph.AddWrite( dispatchPass, reflection, RenderGraphResourceAccess::UnorderedAccess );

    const RenderGraphCompileResult compiled = graph.Compile();
    Dx12RenderGraphExecutionDesc desc;
    const Dx12RenderGraphExecutionResult result = ExecuteDx12RenderGraphTransitions( graph, compiled, desc );

    EXPECT_EQ( result.barriers.size(), static_cast<size_t>( 1 ) );
    EXPECT_EQ( result.uavAccessTransitionCount, static_cast<size_t>( 1 ) );
    EXPECT_TRUE( result.barriers[0].requiresUavOrderingReview );
    EXPECT_TRUE( result.barriers[0].beforeState == D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE );
    EXPECT_TRUE( result.barriers[0].afterState == D3D12_RESOURCE_STATE_UNORDERED_ACCESS );
}

void TestDx12SingleTransitionRequiresCommandListForEmit()
{
    Dx12RenderGraphSingleTransitionDesc desc;
    desc.commandList = nullptr;
    desc.resource = reinterpret_cast<ID3D12Resource*>( static_cast<uintptr_t>( 0x4000u ) );
    desc.before = RenderGraphResourceAccess::Present;
    desc.after = RenderGraphResourceAccess::RenderTarget;

    const Dx12RenderGraphSingleTransitionResult result = EmitDx12RenderGraphTransitionBarrier( desc );

    EXPECT_TRUE( result.hasNativeResource );
    EXPECT_TRUE( result.hasConcreteStates );
    EXPECT_TRUE( result.missingCommandList );
    EXPECT_TRUE( !result.skippedSameState );
    EXPECT_TRUE( !result.emitted );
    EXPECT_TRUE( result.beforeState == D3D12_RESOURCE_STATE_PRESENT );
    EXPECT_TRUE( result.afterState == D3D12_RESOURCE_STATE_RENDER_TARGET );
}

void TestDx12UavBarrierRequiresCommandListForEmit()
{
    Dx12RenderGraphUavBarrierDesc desc;
    desc.commandList = nullptr;
    desc.resource = reinterpret_cast<ID3D12Resource*>( static_cast<uintptr_t>( 0x5000u ) );

    const Dx12RenderGraphUavBarrierResult result = EmitDx12RenderGraphUavBarrier( desc );

    EXPECT_TRUE( result.hasNativeResource );
    EXPECT_TRUE( result.missingCommandList );
    EXPECT_TRUE( !result.emitted );
}

const TestCase kTests[] = {
    { "Descriptor transient ranges are contiguous", TestDescriptorTransientRangeIsContiguous },
    { "Descriptor transient range failures are atomic", TestDescriptorTransientRangeFailureIsAtomic },
    { "Render graph skips Unknown initial transitions", TestRenderGraphSkipsUnknownInitialTransition },
    { "Render graph emits explicit initial-state transitions", TestRenderGraphExplicitInitialStateTransitions },
    { "Render graph tracks subresource transitions independently", TestRenderGraphTracksSubresourceTransitionsIndependently },
    { "Render graph allows uniform specific then all-subresource transition", TestRenderGraphAllowsUniformSpecificThenAllSubresourceTransition },
    { "Render graph clears specific state when it returns to all-state", TestRenderGraphClearsSpecificStateWhenItReturnsToAllState },
    { "Render graph rejects mixed specific then all-subresource transition", TestRenderGraphRejectsMixedSpecificThenAllSubresourceTransition },
    { "Render graph rejects Unknown pass access", TestRenderGraphRejectsUnknownPassAccess },
    { "Render graph rejects bad handles", TestRenderGraphRejectsBadHandles },
    { "DX12 render graph access maps to DX12 states", TestDx12RenderGraphAccessMapsToDx12States },
    { "DX12 render graph executor dry-runs backbuffer transitions", TestDx12RenderGraphExecutorDryRunBackbufferTransitions },
    { "DX12 render graph executor skips Unknown initial access", TestDx12RenderGraphExecutorSkipsUnknownInitialAccess },
    { "DX12 render graph executor identifies UAV access", TestDx12RenderGraphExecutorIdentifiesUavAccess },
    { "DX12 single transition requires command list for emit", TestDx12SingleTransitionRequiresCommandListForEmit },
    { "DX12 UAV barrier requires command list for emit", TestDx12UavBarrierRequiresCommandListForEmit },
};

} // namespace

int main()
{
    int failures = 0;

    for ( const TestCase& test : kTests )
    {
        try
        {
            test.run();
            std::cout << "PASS: " << test.name << "\n";
        }
        catch ( const std::exception& ex )
        {
            ++failures;
            std::cerr << "FAIL: " << test.name << "\n";
            std::cerr << "      " << ex.what() << "\n";
        }
    }

    if ( failures != 0 )
    {
        std::cerr << failures << " DX12 architecture unit test(s) failed.\n";
        return 1;
    }

    std::cout << "PASS: all DX12 architecture unit tests passed.\n";
    return 0;
}
