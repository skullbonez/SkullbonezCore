#include "SkullbonezRenderDeviceDX12.h"
#include "SkullbonezRenderGraph.h"

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

const TestCase kTests[] = {
    { "Descriptor transient ranges are contiguous", TestDescriptorTransientRangeIsContiguous },
    { "Descriptor transient range failures are atomic", TestDescriptorTransientRangeFailureIsAtomic },
    { "Render graph skips Unknown initial transitions", TestRenderGraphSkipsUnknownInitialTransition },
    { "Render graph emits explicit initial-state transitions", TestRenderGraphExplicitInitialStateTransitions },
    { "Render graph rejects Unknown pass access", TestRenderGraphRejectsUnknownPassAccess },
    { "Render graph rejects bad handles", TestRenderGraphRejectsBadHandles },
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
