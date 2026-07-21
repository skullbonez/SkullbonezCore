/*
File: Agentic/Tests/Dx12ArchUnitTests/Dx12ArchUnitTests.cpp
Purpose:
  Contains DX12 architecture checks that guard renderer ownership and dependency boundaries.

Summary:
  Exercises CPU-only renderer state machines and ownership contracts without
  creating a D3D12 device or submitting GPU work.

Mental model:
  Dx12ArchUnitTests.cpp contains DX12 architecture checks that guard renderer
  ownership and dependency boundaries. As an implementation unit, keep edits
  anchored on the behavior under test and the regression signal and on the
  glossary/invariants below.

Glossary:
  Recording epoch: Logical open/closed command-list interval committed only
    after the corresponding DX12 operation succeeds.
  Sticky failure: First command-path failure retained until device reset so a
    later success cannot hide the unsafe earlier result.
  GPU drain: Close, submit, fence wait, and command-list reopen sequence that
    must complete before resource mutation is safe.
  Submitted work: Queue work that remains unsafe for reuse/release until a
    successful covering fence is observed complete.
  Dry run: CPU-only render-graph execution mode that records intended barriers
    without calling a real command list.
  Platform profiler GPU stack: Nested marker depth suspended when one command
    list is submitted and restored on its replacement list.
  Profiler value seam: Core-owned fixed spans consumed by concrete Rendering
    timing and overlay owners without an upward renderer pointer.

Invariants:
  Tests stay CPU-only and must not require a real D3D12 device or renderer launch.
  Descriptor, render-graph, and barrier expectations protect ownership and
  synchronization contracts.
  Command-state tests use HRESULT values and fake pointers only; they never
    create or submit GPU work.
  Submission-state tests model Signal/Wait results without a DX12 queue.

Related:
  - AGENTS.md
  - Agentic/Reference/comment-style-guide.md
*/
#include "Rendering/DX12/Dx12RenderGraphExecutor.h"
#include "Rendering/DX12/RenderBackendDX12.CommandRecordingState.h"
#include "Rendering/DX12/RenderBackendDX12.PipelineState.h"
#include "Rendering/DX12/RenderGraphTransientDX12.h"
#include "Rendering/DX12/RenderDeviceDX12.h"
#include "Rendering/DX12/Dx12FrameOwner.h"
#include "Rendering/DX12/Dx12TextureRegistry.h"
#include "Rendering/DX12/RenderBackendDX12.h"
#include "Rendering/ProfilerOverlayPresenter.h"
#include "Rendering/RenderGpuTimingOwner.h"
#include "Rendering/RenderGraph.h"

#include <cstdint>
#include <cstring>
#include <exception>
#include <iostream>
#include <process.h>
#include <sstream>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

using namespace SkullbonezCore::Rendering;
using SkullbonezCore::Core::SbResult;
using SkullbonezCore::Core::Allocation::RuntimeAllocationPhase;

static_assert(
    std::is_same<decltype( std::declval<Dx12FrameOwner&>().FlushGPU() ), SkullbonezCore::Core::SbResult>::value,
    "FlushGPU must return a recoverable result to every resource-mutation caller." );
static_assert( std::is_same<decltype( std::declval<Dx12FrameOwner&>().DrainForResourceRelease() ),
                            SkullbonezCore::Core::SbResult>::value,
               "Terminal resource release must use its own checked drain boundary." );
static_assert( std::is_trivially_copyable<Dx12SubmittedWorkState>::value,
               "Submitted-work tracking must remain an allocation-free value record." );
static_assert( std::is_constructible_v<RenderGpuTimingOwner, SkullbonezCore::Core::Profiler*, Dx12Diagnostics*>,
               "Runtime rendering must own one explicit concrete GPU timing boundary." );
static_assert( !std::is_polymorphic_v<RenderGpuTimingOwner>,
               "GPU timing stays a concrete owner, not a new renderer callback interface." );
static_assert( !std::is_polymorphic_v<RenderBackendDX12>,
               "The DX12 composition root must not regrow a polymorphic renderer facade." );
static_assert( !std::is_polymorphic_v<Dx12FrameOwner> && !std::is_polymorphic_v<Dx12GraphTransientPool> &&
                   !std::is_polymorphic_v<Dx12ResourceBuilder> && !std::is_polymorphic_v<Dx12TextureOwner> &&
                   !std::is_polymorphic_v<Dx12GeometryOwner> && !std::is_polymorphic_v<Dx12Diagnostics>,
               "Concrete DX12 owners must remain non-polymorphic capability boundaries." );
static_assert( std::is_empty_v<ProfilerOverlayPresenter>,
               "Profiler overlay presentation must not retain Core frame or command borrows." );
static_assert( std::is_same_v<decltype( SkullbonezCore::Core::Profiler::ProfilerFrameView::markers ),
                              std::span<const SkullbonezCore::Core::Profiler::Marker>>,
               "Core publishes profiler markers as a fixed read-only span." );

namespace
{

struct TestFailure : public std::runtime_error
{
    explicit TestFailure( const std::string& message ) : std::runtime_error( message )
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
void ExpectEqualImpl( const T& actual,
                      const U& expected,
                      const char* actualExpression,
                      const char* expectedExpression,
                      const char* file,
                      int line )
{
    if ( !( actual == expected ) )
    {
        std::ostringstream out;
        out << "expected " << actualExpression << " == " << expectedExpression << ", actual " << actual << ", expected "
            << expected;
        Fail( file, line, out.str() );
    }
}

const char* g_executablePath = nullptr;

void ExpectFatalCase( const char* caseName, const char* file, int line )
{
    if ( !g_executablePath )
    {
        Fail( file, line, "fatal child executable path is unavailable" );
    }

    // Hazard: fatal engine invariants break/terminate the current process, so
    // each negative contract runs in a child. A normal zero exit means the
    // expected fatal boundary silently returned.
    const intptr_t childExit =
        _spawnl( _P_WAIT, g_executablePath, g_executablePath, "--fatal-case", caseName, static_cast<char*>( nullptr ) );
    if ( childExit == -1 )
    {
        Fail( file, line, std::string( "failed to launch fatal child case: " ) + caseName );
    }
    if ( childExit == 0 )
    {
        Fail( file, line, std::string( "expected fatal child failure from: " ) + caseName );
    }
}

#define EXPECT_TRUE( expression ) ExpectTrue( !!( expression ), #expression, __FILE__, __LINE__ )
#define EXPECT_EQ( actual, expected )                                                                                  \
    ExpectEqualImpl( ( actual ), ( expected ), #actual, #expected, __FILE__, __LINE__ )
#define EXPECT_FATAL_CASE( caseName ) ExpectFatalCase( caseName, __FILE__, __LINE__ )

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

Dx12CommandRecordingState MakeOpenCommandState()
{
    Dx12CommandRecordingState state;
    state.ResetForDevice();
    EXPECT_TRUE( state.CommitAllocatorReset( S_OK ).ok );
    EXPECT_TRUE( state.CommitListReset( S_OK ).ok );
    return state;
}

void TestCommandCloseFailureDoesNotCommitClosedState()
{
    Dx12CommandRecordingState state = MakeOpenCommandState();

    const auto result = state.CommitClose( E_FAIL, "test Close" );

    EXPECT_TRUE( !result.ok );
    EXPECT_TRUE( state.IsOpen() );
    EXPECT_TRUE( !state.CanRecord() );
    EXPECT_TRUE( state.HasFailure() );
}

void TestCommandCloseSuccessCommitsClosedState()
{
    Dx12CommandRecordingState state = MakeOpenCommandState();

    const auto result = state.CommitClose( S_OK, "test Close" );

    EXPECT_TRUE( result.ok );
    EXPECT_TRUE( state.IsClosed() );
    EXPECT_TRUE( !state.HasFailure() );
}

void TestAllocatorResetFailureBlocksListReset()
{
    Dx12CommandRecordingState state;
    state.ResetForDevice();

    const auto allocatorResult = state.CommitAllocatorReset( E_FAIL, "test allocator Reset" );
    const auto listResult = state.CommitListReset( S_OK, "test list Reset" );

    EXPECT_TRUE( !allocatorResult.ok );
    EXPECT_TRUE( !listResult.ok );
    EXPECT_TRUE( state.IsClosed() );
    EXPECT_TRUE( !state.CanRecord() );
    EXPECT_EQ( std::string( listResult.error.message ), std::string( allocatorResult.error.message ) );
}

void TestListResetFailureDoesNotCommitOpenState()
{
    Dx12CommandRecordingState state;
    state.ResetForDevice();
    EXPECT_TRUE( state.CommitAllocatorReset( S_OK ).ok );

    const auto result = state.CommitListReset( E_FAIL, "test list Reset" );

    EXPECT_TRUE( !result.ok );
    EXPECT_TRUE( state.IsClosed() );
    EXPECT_TRUE( !state.CanRecord() );
}

void TestSuccessfulListResetCommitsOpenState()
{
    Dx12CommandRecordingState state;
    state.ResetForDevice();

    EXPECT_TRUE( state.CommitAllocatorReset( S_OK ).ok );
    EXPECT_TRUE( state.CommitListReset( S_OK ).ok );
    EXPECT_TRUE( state.IsOpen() );
    EXPECT_TRUE( state.CanRecord() );
}

void TestWaitFailurePreservesRecordingEpoch()
{
    const auto waitFailure = SkullbonezCore::Core::SbResult::Failure( "TestWait", "fence did not complete" );

    Dx12CommandRecordingState closedState;
    closedState.ResetForDevice();
    const auto closedResult = closedState.CommitWait( waitFailure );

    EXPECT_TRUE( !closedResult.ok );
    EXPECT_TRUE( closedState.IsClosed() );
    EXPECT_TRUE( !closedState.CanRecord() );
    EXPECT_EQ( std::string( closedResult.error.owner ), std::string( "TestWait" ) );

    Dx12CommandRecordingState openState = MakeOpenCommandState();
    const auto openResult = openState.CommitWait( waitFailure );

    EXPECT_TRUE( !openResult.ok );
    EXPECT_TRUE( openState.IsOpen() );
    EXPECT_TRUE( !openState.CanRecord() );
    EXPECT_EQ( std::string( openResult.error.owner ), std::string( "TestWait" ) );
}

void TestFirstCommandFailureRemainsAuthoritative()
{
    Dx12CommandRecordingState state = MakeOpenCommandState();

    const auto first = state.CommitClose( E_FAIL, "first Close" );
    const auto second =
        state.RetainFailure( SkullbonezCore::Core::SbResult::Failure( "SecondOwner", "second failure" ) );

    EXPECT_TRUE( !first.ok );
    EXPECT_TRUE( !second.ok );
    EXPECT_EQ( std::string( second.error.owner ), std::string( first.error.owner ) );
    EXPECT_EQ( std::string( second.error.message ), std::string( first.error.message ) );
}

void TestDeviceResetClearsCommandFailure()
{
    Dx12CommandRecordingState state = MakeOpenCommandState();
    EXPECT_TRUE( !state.CommitClose( E_FAIL ).ok );

    state.ResetForDevice();

    EXPECT_TRUE( state.IsClosed() );
    EXPECT_TRUE( !state.HasFailure() );
    EXPECT_TRUE( state.CurrentResult().ok );
}

void TestMapResultRejectsFailedHresult()
{
    void* unexpectedPointer = reinterpret_cast<void*>( static_cast<uintptr_t>( 0x1234u ) );

    const Dx12MappedPointerResult checked = ValidateDx12MappedPointer( E_FAIL, unexpectedPointer, "test Map" );

    EXPECT_TRUE( !checked.result.ok );
    EXPECT_TRUE( checked.bytes == nullptr );
}

void TestMapResultRejectsNullPointerAfterSuccess()
{
    const Dx12MappedPointerResult checked = ValidateDx12MappedPointer( S_OK, nullptr, "test Map" );

    EXPECT_TRUE( !checked.result.ok );
    EXPECT_TRUE( checked.bytes == nullptr );
}

void TestMapResultAcceptsSuccessfulMappedPointer()
{
    uint8_t* expectedPointer = reinterpret_cast<uint8_t*>( static_cast<uintptr_t>( 0x1234u ) );

    const Dx12MappedPointerResult checked = ValidateDx12MappedPointer( S_OK, expectedPointer, "test Map" );

    EXPECT_TRUE( checked.result.ok );
    EXPECT_TRUE( checked.bytes == expectedPointer );
}

void TestGpuDrainCloseFailureBlocksSubmission()
{
    Dx12CommandRecordingState commandState = MakeOpenCommandState();
    Dx12GpuDrainProgress drainProgress( commandState.IsOpen() );

    const SkullbonezCore::Core::SbResult closeResult = commandState.CommitClose( E_FAIL, "test FlushGPU Close" );
    if ( closeResult.ok )
    {
        drainProgress.CommitClose();
    }

    EXPECT_TRUE( !closeResult.ok );
    EXPECT_TRUE( drainProgress.RequiresClose() );
    EXPECT_TRUE( !drainProgress.CanSubmit() );
    EXPECT_TRUE( !drainProgress.CanWait() );
    EXPECT_TRUE( !drainProgress.IsMutationSafe() );
}

void TestGpuDrainWaitFailureBlocksReopenAndMutation()
{
    Dx12CommandRecordingState commandState = MakeOpenCommandState();
    Dx12GpuDrainProgress drainProgress( commandState.IsOpen() );

    const SkullbonezCore::Core::SbResult closeResult = commandState.CommitClose( S_OK, "test FlushGPU Close" );
    EXPECT_TRUE( closeResult.ok );
    EXPECT_TRUE( drainProgress.CommitClose() );
    EXPECT_TRUE( drainProgress.CommitSubmission() );

    const SkullbonezCore::Core::SbResult waitResult = commandState.CommitWait(
        SkullbonezCore::Core::SbResult::Failure( "TestWait", "submitted work did not drain" ) );
    if ( waitResult.ok )
    {
        drainProgress.CommitWait();
    }

    EXPECT_TRUE( !waitResult.ok );
    EXPECT_TRUE( drainProgress.CanWait() );
    EXPECT_TRUE( !drainProgress.CanReopen() );
    EXPECT_TRUE( !drainProgress.IsMutationSafe() );
}

void TestGpuDrainSuccessAllowsMutationOnlyAfterReopen()
{
    Dx12CommandRecordingState commandState = MakeOpenCommandState();
    Dx12GpuDrainProgress drainProgress( commandState.IsOpen() );

    EXPECT_TRUE( commandState.CommitClose( S_OK, "test FlushGPU Close" ).ok );
    EXPECT_TRUE( drainProgress.CommitClose() );
    EXPECT_TRUE( drainProgress.CommitSubmission() );
    EXPECT_TRUE( commandState.CommitWait( SkullbonezCore::Core::SbResult::Success() ).ok );
    EXPECT_TRUE( drainProgress.CommitWait() );
    EXPECT_TRUE( !drainProgress.IsMutationSafe() );

    EXPECT_TRUE( commandState.CommitAllocatorReset( S_OK, "test FlushGPU allocator Reset" ).ok );
    EXPECT_TRUE( commandState.CommitListReset( S_OK, "test FlushGPU list Reset" ).ok );
    EXPECT_TRUE( drainProgress.CommitReopen() );

    EXPECT_TRUE( commandState.CanRecord() );
    EXPECT_TRUE( drainProgress.IsMutationSafe() );
}

void TestSubmittedWorkSignalFailureBlocksReuseAndRelease()
{
    Dx12SubmittedWorkState submittedWork;
    submittedWork.ResetForDevice();
    submittedWork.MarkSubmitted();

    submittedWork.CommitSignal( SkullbonezCore::Core::SbResult::Failure( "TestSignal", "queue signal failed" ), 0 );

    EXPECT_TRUE( submittedWork.Phase() == Dx12SubmittedWorkPhase::CompletionUncertain );
    EXPECT_TRUE( submittedWork.HasSubmittedWork() );
    EXPECT_TRUE( submittedWork.HasUnfencedOrUncertainWork() );
    EXPECT_TRUE( !submittedWork.HasKnownCompletionFence() );
    EXPECT_TRUE( !submittedWork.CanReleaseWithoutFence() );

    // No arbitrary completed value can cover work whose Signal never committed.
    submittedWork.ObserveCompletedFence( ~static_cast<UINT64>( 0 ) );
    EXPECT_TRUE( submittedWork.HasSubmittedWork() );
}

void TestSubmittedWorkWaitFailurePreservesCompletionFence()
{
    Dx12SubmittedWorkState submittedWork;
    submittedWork.ResetForDevice();
    submittedWork.MarkSubmitted();
    submittedWork.CommitSignal( SkullbonezCore::Core::SbResult::Success(), 42 );

    submittedWork.CommitWait( SkullbonezCore::Core::SbResult::Failure( "TestWait", "fence wait failed" ), 42 );

    EXPECT_TRUE( submittedWork.Phase() == Dx12SubmittedWorkPhase::CompletionUncertain );
    EXPECT_TRUE( submittedWork.HasSubmittedWork() );
    EXPECT_TRUE( submittedWork.HasKnownCompletionFence() );
    EXPECT_EQ( submittedWork.CompletionFence(), static_cast<UINT64>( 42 ) );
    EXPECT_TRUE( !submittedWork.CanReleaseWithoutFence() );

    submittedWork.ObserveCompletedFence( 41 );
    EXPECT_TRUE( submittedWork.HasSubmittedWork() );
    submittedWork.ObserveCompletedFence( 42 );
    EXPECT_TRUE( !submittedWork.HasSubmittedWork() );
    EXPECT_TRUE( submittedWork.CanReleaseWithoutFence() );
}

void TestSubmittedWorkSuccessfulSignalAndWaitAllowsReuse()
{
    Dx12SubmittedWorkState submittedWork;
    submittedWork.ResetForDevice();
    submittedWork.MarkSubmitted();
    submittedWork.CommitSignal( SkullbonezCore::Core::SbResult::Success(), 7 );

    EXPECT_TRUE( submittedWork.Phase() == Dx12SubmittedWorkPhase::SubmittedFenced );
    EXPECT_TRUE( !submittedWork.CanReleaseWithoutFence() );

    submittedWork.CommitWait( SkullbonezCore::Core::SbResult::Success(), 7 );
    EXPECT_TRUE( submittedWork.Phase() == Dx12SubmittedWorkPhase::Idle );
    EXPECT_TRUE( submittedWork.CanReleaseWithoutFence() );
}

void TestDeviceLossBlocksWorkAndRetainsFirstFailure()
{
    Dx12DeviceHealthState health;
    health.ResetForDevice();

    const SkullbonezCore::Core::SbResult first = health.RetainDeviceLoss( "Present", E_FAIL );
    const SkullbonezCore::Core::SbResult second = health.RetainDeviceLoss( "ResizeBuffers", E_OUTOFMEMORY );

    EXPECT_TRUE( !first.ok );
    EXPECT_TRUE( health.IsLost() );
    EXPECT_TRUE( !health.CanIssueDeviceWork() );
    EXPECT_EQ( std::string( second.error.message ), std::string( first.error.message ) );
}

void TestDeviceHealthResetAllowsNewDeviceWork()
{
    Dx12DeviceHealthState health;
    health.ResetForDevice();
    EXPECT_TRUE( !health.RetainDeviceLoss( "Present", E_FAIL ).ok );

    health.ResetForDevice();

    EXPECT_TRUE( health.CanIssueDeviceWork() );
    EXPECT_TRUE( !health.IsLost() );
    EXPECT_TRUE( health.CurrentResult().ok );
}

void TestRemovedDeviceAllowsTerminalSubmittedWorkAbandon()
{
    Dx12DeviceHealthState health;
    health.ResetForDevice();
    Dx12SubmittedWorkState submittedWork;
    submittedWork.ResetForDevice();
    submittedWork.MarkSubmitted();

    EXPECT_TRUE( !health.RetainDeviceLoss( "Present", DXGI_ERROR_DEVICE_REMOVED ).ok );
    EXPECT_TRUE( submittedWork.HasSubmittedWork() );

    submittedWork.AbandonForRemovedDevice();

    EXPECT_TRUE( health.IsLost() );
    EXPECT_TRUE( submittedWork.CanReleaseWithoutFence() );
}

void TestRecreationFailurePreservesPublishedGeneration()
{
    Dx12RecreationTransaction transaction;
    transaction.Begin( 7 );
    EXPECT_TRUE( transaction.CommitCandidateReady() );
    EXPECT_TRUE( transaction.CommitOldReferencesReleased() );

    const SkullbonezCore::Core::SbResult failure =
        transaction.Fail( SkullbonezCore::Core::SbResult::Failure( "TestResize", "ResizeBuffers failed" ) );

    EXPECT_TRUE( !failure.ok );
    EXPECT_TRUE( transaction.HasFailed() );
    EXPECT_TRUE( !transaction.IsPublished() );
    EXPECT_EQ( transaction.PublishedGeneration(), static_cast<uint64_t>( 7 ) );
    EXPECT_TRUE( !transaction.CommitSwapChainResized() );
}

void TestRecreationPublishesOnlyAfterEveryCandidateIsReady()
{
    Dx12RecreationTransaction transaction;
    transaction.Begin( 3 );

    EXPECT_TRUE( !transaction.CommitPublished( 4 ) );
    EXPECT_TRUE( transaction.CommitCandidateReady() );
    EXPECT_TRUE( transaction.CommitOldReferencesReleased() );
    EXPECT_TRUE( transaction.CommitSwapChainResized() );
    EXPECT_TRUE( !transaction.IsPublished() );
    EXPECT_TRUE( transaction.CommitBackBuffersReady() );
    EXPECT_TRUE( transaction.CommitPublished( 4 ) );
    EXPECT_TRUE( transaction.IsPublished() );
    EXPECT_EQ( transaction.PublishedGeneration(), static_cast<uint64_t>( 4 ) );
}

void TestFaultInjectionBlocksFirstAndSubsequentSubmissions()
{
    Dx12FaultInjectionState fault;
    fault.Configure( "before-first-submit" );

    const SkullbonezCore::Core::SbResult first = fault.BeforeSubmission();
    const SkullbonezCore::Core::SbResult second = fault.BeforeSubmission();

    EXPECT_TRUE( !first.ok );
    EXPECT_TRUE( !second.ok );
    EXPECT_TRUE( fault.WasInjected() );
    EXPECT_EQ( fault.SubmissionCount(), 0u );
    EXPECT_EQ( fault.BlockedSubmissionCount(), 1u );
    EXPECT_EQ( std::string( second.error.message ), std::string( first.error.message ) );
}

void TestUnarmedFaultInjectionAllowsSubmissionAccounting()
{
    Dx12FaultInjectionState fault;
    fault.Configure( nullptr );

    EXPECT_TRUE( fault.BeforeSubmission().ok );
    fault.CommitSubmission();

    EXPECT_TRUE( !fault.WasInjected() );
    EXPECT_EQ( fault.SubmissionCount(), 1u );
    EXPECT_EQ( fault.BlockedSubmissionCount(), 0u );
}

struct RenderGraphCallbackTrace
{
    std::vector<std::string> labels;
};

void RecordRenderGraphCallback( const RenderGraphPassContext& context, RenderGraphCallbackTrace& trace )
{
    trace.labels.push_back( context.debugLabel ? context.debugLabel : context.pass->name );
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

void TestDescriptorTransientRangeCapacityProbeIsAtomic()
{
    Dx12DescriptorAllocator allocator = MakeDescriptorAllocator();

    allocator.ResetFrame( 0 );
    EXPECT_EQ( allocator.AllocateTransientRange( 7 ), 4u );

    EXPECT_TRUE( !allocator.CanAllocateTransientRange( 2 ) );

    const Dx12DescriptorAllocatorStats afterFailedRange = allocator.GetStats();
    EXPECT_EQ( afterFailedRange.transientUsedThisFrame, 7u );
    EXPECT_EQ( afterFailedRange.transientPeakThisRun, 7u );

    EXPECT_TRUE( !allocator.CanAllocateTransientRange( 0 ) );

    const Dx12DescriptorAllocatorStats afterZeroRange = allocator.GetStats();
    EXPECT_EQ( afterZeroRange.transientUsedThisFrame, 7u );
    EXPECT_EQ( afterZeroRange.transientPeakThisRun, 7u );
}

void TestStaticDescriptorRowsReuseWithStableHighWater()
{
    Dx12DescriptorAllocator allocator = MakeDescriptorAllocator();

    const UINT first = allocator.AllocateStatic();
    const UINT second = allocator.AllocateStatic();
    allocator.FreeStatic( first );
    const UINT reused = allocator.AllocateStatic();

    EXPECT_EQ( first, 0u );
    EXPECT_EQ( second, 1u );
    EXPECT_EQ( reused, first );
    const Dx12DescriptorAllocatorStats stats = allocator.GetStats();
    EXPECT_EQ( stats.staticUsed, 2u );
    EXPECT_EQ( stats.staticHighWater, 2u );
}

void TestTextureHandleGenerationRejectsReusedSlotAlias()
{
    Dx12TextureRegistry registry;
    registry.Initialize( 2u );
    TextureEntryDX12 entry;
    entry.srvIndex = 11u;
    const uint32_t first = registry.Insert( entry );
    EXPECT_TRUE( registry.Resolve( first ) != nullptr );
    registry.Resolve( first )->srvIndex = UINT_MAX;

    entry.srvIndex = 22u;
    const uint32_t replacement = registry.Insert( entry );
    EXPECT_TRUE( replacement != first );
    EXPECT_TRUE( registry.Resolve( first ) == nullptr );
    EXPECT_TRUE( registry.Resolve( replacement ) != nullptr );
    EXPECT_EQ( registry.Resolve( replacement )->srvIndex, 22u );
}

void TestRenderGraphSkipsUnknownInitialTransition()
{
    RenderGraph graph;
    const RenderGraphResourceHandle legacyTarget =
        graph.AddExternalResource( "LegacyTarget", RenderGraphResourceAccess::Unknown );

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
    const RenderGraphResourceHandle backbuffer =
        graph.AddExternalResource( "Backbuffer", RenderGraphResourceAccess::Present );

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
    const RenderGraphResourceHandle texture =
        graph.AddExternalResource( "MipTexture",
                                   RenderGraphResourceAccess::PixelShaderResource,
                                   RenderGraphNativeResourceToken::From(
                                       reinterpret_cast<ID3D12Resource*>( static_cast<uintptr_t>( 0x6000u ) ) ) );

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
    EXPECT_EQ( result.barrierCount, static_cast<size_t>( 3 ) );
    EXPECT_EQ( result.barriers[0].subresource, 1u );
    EXPECT_EQ( result.barriers[1].subresource, 2u );
    EXPECT_EQ( result.barriers[2].subresource, 1u );
}

void TestRenderGraphAllowsUniformSpecificThenAllSubresourceTransition()
{
    RenderGraph graph;
    const RenderGraphResourceHandle texture =
        graph.AddExternalResource( "UniformTexture", RenderGraphResourceAccess::PixelShaderResource );

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
    const RenderGraphResourceHandle texture =
        graph.AddExternalResource( "ReturnedTexture", RenderGraphResourceAccess::PixelShaderResource );

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
    EXPECT_FATAL_CASE( "mixed-subresource-transition" );
}

void TestRenderGraphRejectsUnknownPassAccess()
{
    EXPECT_FATAL_CASE( "unknown-read-access" );
    EXPECT_FATAL_CASE( "unknown-write-access" );
}

void TestRenderGraphRejectsBadHandles()
{
    EXPECT_FATAL_CASE( "bad-read-resource" );
    EXPECT_FATAL_CASE( "bad-write-pass" );
}

RenderGraphTransientResourceDesc MakeTransientColorDesc( uint32_t width = 128u, uint32_t height = 64u )
{
    RenderGraphTransientResourceDesc desc;
    desc.kind = RenderGraphResourceKind::Texture2D;
    desc.format = RenderGraphResourceFormat::RGBA16F;
    desc.width = width;
    desc.height = height;
    desc.mipLevels = 1u;
    desc.descriptors.renderTarget = true;
    desc.descriptors.shaderResource = true;
    return desc;
}

void TestRenderGraphPlansTransientResourceLifetime()
{
    RenderGraph graph;
    const RenderGraphResourceHandle color =
        graph.AddTransientResource( "HalfResLight", MakeTransientColorDesc(), RenderGraphResourceAccess::Unknown );

    const uint32_t produce = graph.AddPass( "ProduceLight" );
    graph.AddWrite( produce, color, RenderGraphResourceAccess::RenderTarget );

    const uint32_t consume = graph.AddPass( "ConsumeLight" );
    graph.AddRead( consume, color, RenderGraphResourceAccess::PixelShaderResource );

    const RenderGraphCompileResult compiled = graph.Compile();
    EXPECT_EQ( compiled.transientAllocations.size(), static_cast<size_t>( 1 ) );
    EXPECT_EQ( compiled.transientAllocations[0].firstPass, produce );
    EXPECT_EQ( compiled.transientAllocations[0].lastPass, consume );
    EXPECT_EQ( compiled.transientAllocations[0].descriptorCount, 2u );
    EXPECT_TRUE( compiled.transientAllocations[0].releasedAtFrameEnd );
    EXPECT_EQ( compiled.transientDiagnostics.allocationCount, static_cast<size_t>( 1 ) );
    EXPECT_EQ( compiled.transientDiagnostics.releaseCount, static_cast<size_t>( 1 ) );
    EXPECT_EQ( compiled.transientDiagnostics.highWaterResources, static_cast<size_t>( 1 ) );
    EXPECT_EQ( compiled.transientDiagnostics.highWaterDescriptors, static_cast<size_t>( 2 ) );
}

void TestRenderGraphReusesCompatibleNonOverlappingTransientResources()
{
    RenderGraph graph;
    const RenderGraphTransientResourceDesc desc = MakeTransientColorDesc();
    const RenderGraphResourceHandle first =
        graph.AddTransientResource( "FirstTransient", desc, RenderGraphResourceAccess::Unknown );
    const RenderGraphResourceHandle second =
        graph.AddTransientResource( "SecondTransient", desc, RenderGraphResourceAccess::Unknown );

    const uint32_t firstProduce = graph.AddPass( "FirstProduce" );
    graph.AddWrite( firstProduce, first, RenderGraphResourceAccess::RenderTarget );
    const uint32_t firstConsume = graph.AddPass( "FirstConsume" );
    graph.AddRead( firstConsume, first, RenderGraphResourceAccess::PixelShaderResource );
    const uint32_t secondProduce = graph.AddPass( "SecondProduce" );
    graph.AddWrite( secondProduce, second, RenderGraphResourceAccess::RenderTarget );

    const RenderGraphCompileResult compiled = graph.Compile();
    EXPECT_EQ( compiled.transientAllocations.size(), static_cast<size_t>( 2 ) );
    EXPECT_EQ( compiled.transientAllocations[0].poolSlot, compiled.transientAllocations[1].poolSlot );
    EXPECT_TRUE( compiled.transientAllocations[1].reused );
    EXPECT_EQ( compiled.transientDiagnostics.reuseCount, static_cast<size_t>( 1 ) );
    EXPECT_EQ( compiled.transientDiagnostics.highWaterResources, static_cast<size_t>( 1 ) );
}

void TestRenderGraphRejectsUnusedTransientResource()
{
    EXPECT_FATAL_CASE( "unused-transient" );
}

void TestRenderGraphExecutesCallbacksInPassOrder()
{
    RenderGraph graph;
    const RenderGraphResourceHandle backbuffer =
        graph.AddExternalResource( "Backbuffer", RenderGraphResourceAccess::RenderTarget );

    RenderGraphCallbackTrace trace;
    const uint32_t firstPass = graph.AddPass( "FirstCallback" );
    graph.AddWrite( firstPass, backbuffer, RenderGraphResourceAccess::RenderTarget );
    graph.SetPassCallback<RecordRenderGraphCallback>( firstPass, trace, true, "first" );

    const uint32_t declarationOnlyPass = graph.AddPass( "DeclarationOnly" );
    graph.AddWrite( declarationOnlyPass, backbuffer, RenderGraphResourceAccess::RenderTarget );

    const uint32_t secondPass = graph.AddPass( "SecondCallback" );
    graph.AddWrite( secondPass, backbuffer, RenderGraphResourceAccess::RenderTarget );
    graph.SetPassCallback<RecordRenderGraphCallback>( secondPass, trace, true, "second" );

    const RenderGraphCallbackExecutionResult result =
        graph.ExecuteCallbacks( RenderGraphCallbackExecutionMode::Execute );

    EXPECT_EQ( result.callbackPassCount, static_cast<size_t>( 2 ) );
    EXPECT_EQ( result.declarationOnlyPassCount, static_cast<size_t>( 1 ) );
    EXPECT_EQ( result.executedPassCount, static_cast<size_t>( 2 ) );
    EXPECT_EQ( trace.labels.size(), static_cast<size_t>( 2 ) );
    EXPECT_EQ( trace.labels[0], std::string( "first" ) );
    EXPECT_EQ( trace.labels[1], std::string( "second" ) );
}

void TestRenderGraphFrameEdgesKeepOnlyPresentDeclarationOnly()
{
    RenderGraph graph;
    const RenderGraphResourceHandle backbuffer =
        graph.AddExternalResource( "Backbuffer", RenderGraphResourceAccess::Present );

    RenderGraphCallbackTrace trace;
    const uint32_t clearPass = graph.AddPass( "BackbufferClear" );
    graph.AddWrite( clearPass, backbuffer, RenderGraphResourceAccess::RenderTarget );
    graph.SetPassCallback<RecordRenderGraphCallback>( clearPass, trace, true, "clear" );
    const RenderGraphCallbackExecutionResult worldResult =
        graph.ExecuteCallbacks( RenderGraphCallbackExecutionMode::Execute, clearPass, 1u );

    // Invariant: production wrappers rediscover the same named external
    // resource while appending later pass ranges. Identity must remain stable
    // so the compiler retains cross-pass state history.
    const RenderGraphResourceHandle reboundBackbuffer =
        graph.AddExternalResource( "Backbuffer", RenderGraphResourceAccess::RenderTarget );
    EXPECT_EQ( reboundBackbuffer.index, backbuffer.index );
    const uint32_t uiPass = graph.AddPass( "UiTargetAcquire" );
    graph.AddWrite( uiPass, reboundBackbuffer, RenderGraphResourceAccess::RenderTarget );
    graph.SetPassCallback<RecordRenderGraphCallback>( uiPass, trace, true, "ui" );
    const RenderGraphCallbackExecutionResult uiResult =
        graph.ExecuteCallbacks( RenderGraphCallbackExecutionMode::Execute, uiPass, 1u );

    // Invariant: normal frame work is callback-owned. Present alone is a
    // declaration-only frame edge because the swap-chain owner performs it
    // after graph callback execution.
    const uint32_t presentPass = graph.AddPass( "Present" );
    graph.AddWrite( presentPass, backbuffer, RenderGraphResourceAccess::Present );

    const RenderGraphCompileResult compiled = graph.Compile();
    EXPECT_EQ( compiled.transitions.size(), static_cast<size_t>( 2 ) );
    EXPECT_EQ( compiled.transitions[0].passIndex, clearPass );
    EXPECT_TRUE( compiled.transitions[0].before == RenderGraphResourceAccess::Present );
    EXPECT_TRUE( compiled.transitions[0].after == RenderGraphResourceAccess::RenderTarget );
    EXPECT_EQ( compiled.transitions[1].passIndex, presentPass );
    EXPECT_TRUE( compiled.transitions[1].before == RenderGraphResourceAccess::RenderTarget );
    EXPECT_TRUE( compiled.transitions[1].after == RenderGraphResourceAccess::Present );

    EXPECT_TRUE( graph.Passes()[clearPass].executionOwner == RenderGraphPassExecutionOwner::Callback );
    EXPECT_TRUE( graph.Passes()[uiPass].executionOwner == RenderGraphPassExecutionOwner::Callback );
    EXPECT_TRUE( graph.Passes()[presentPass].executionOwner == RenderGraphPassExecutionOwner::DeclarationOnly );

    const RenderGraphExecutionContractResult contract = graph.ValidateFrameExecutionContract( "Present" );
    EXPECT_TRUE( contract.IsValid() );
    EXPECT_EQ( contract.callbackPassCount, static_cast<size_t>( 2 ) );
    EXPECT_EQ( contract.declarationOnlyPassCount, static_cast<size_t>( 1 ) );
    EXPECT_EQ( worldResult.executedPassCount, static_cast<size_t>( 1 ) );
    EXPECT_EQ( uiResult.executedPassCount, static_cast<size_t>( 1 ) );
    EXPECT_EQ( trace.labels.size(), static_cast<size_t>( 2 ) );
    EXPECT_EQ( trace.labels[0], std::string( "clear" ) );
    EXPECT_EQ( trace.labels[1], std::string( "ui" ) );

    // Capture-restart frames execute the same callback-owned UI/world ranges
    // but intentionally leave before swap-chain Present. The production
    // validator must accept exactly zero declaration-only rows for that edge.
    RenderGraph captureGraph;
    const RenderGraphResourceHandle captureBackbuffer =
        captureGraph.AddExternalResource( "Backbuffer", RenderGraphResourceAccess::RenderTarget );
    const uint32_t captureUiPass = captureGraph.AddPass( "UiTargetAcquire" );
    captureGraph.AddWrite( captureUiPass, captureBackbuffer, RenderGraphResourceAccess::RenderTarget );
    captureGraph.SetPassCallback<RecordRenderGraphCallback>( captureUiPass, trace, true, "capture-ui" );
    const RenderGraphCallbackExecutionResult captureResult =
        captureGraph.ExecuteCallbacks( RenderGraphCallbackExecutionMode::Execute, captureUiPass, 1u );
    const RenderGraphExecutionContractResult captureContract = captureGraph.ValidateFrameExecutionContract( nullptr );
    EXPECT_TRUE( captureContract.IsValid() );
    EXPECT_EQ( captureContract.expectedDeclarationOnlyPassCount, static_cast<size_t>( 0 ) );
    EXPECT_EQ( captureContract.declarationOnlyPassCount, static_cast<size_t>( 0 ) );
    EXPECT_EQ( captureResult.executedPassCount, static_cast<size_t>( 1 ) );
    captureGraph.ReleaseCallbackPayloadBorrows();

    const uint32_t accidentalDeclaration = graph.AddPass( "AccidentalDirectPass" );
    graph.AddWrite( accidentalDeclaration, backbuffer, RenderGraphResourceAccess::RenderTarget );
    EXPECT_TRUE( !graph.ValidateFrameExecutionContract( "Present" ).IsValid() );

    RenderGraph disabledGraph;
    const RenderGraphResourceHandle disabledBackbuffer =
        disabledGraph.AddExternalResource( "Backbuffer", RenderGraphResourceAccess::RenderTarget );
    const uint32_t disabledPass = disabledGraph.AddPass( "DisabledFramePass" );
    disabledGraph.AddWrite( disabledPass, disabledBackbuffer, RenderGraphResourceAccess::RenderTarget );
    disabledGraph.SetPassCallback<RecordRenderGraphCallback>( disabledPass, trace, false, "disabled" );
    const uint32_t disabledPresent = disabledGraph.AddPass( "Present" );
    disabledGraph.AddWrite( disabledPresent, disabledBackbuffer, RenderGraphResourceAccess::Present );
    EXPECT_TRUE( !disabledGraph.ValidateFrameExecutionContract( "Present" ).IsValid() );
}

void TestRenderGraphDryRunValidatesCallbacksWithoutExecuting()
{
    RenderGraph graph;
    const RenderGraphResourceHandle target =
        graph.AddExternalResource( "Target", RenderGraphResourceAccess::RenderTarget );

    RenderGraphCallbackTrace trace;
    const uint32_t pass = graph.AddPass( "DryRunCallback" );
    graph.AddWrite( pass, target, RenderGraphResourceAccess::RenderTarget );
    graph.SetPassCallback<RecordRenderGraphCallback>( pass, trace, true, "dry-run" );

    const RenderGraphCallbackExecutionResult result =
        graph.ExecuteCallbacks( RenderGraphCallbackExecutionMode::DryRun );

    EXPECT_EQ( result.callbackPassCount, static_cast<size_t>( 1 ) );
    EXPECT_EQ( result.dryRunValidatedPassCount, static_cast<size_t>( 1 ) );
    EXPECT_EQ( result.executedPassCount, static_cast<size_t>( 0 ) );
    EXPECT_TRUE( trace.labels.empty() );
}

void TestRenderGraphDisabledCallbackDoesNotExecute()
{
    RenderGraph graph;
    const RenderGraphResourceHandle target =
        graph.AddExternalResource( "Target", RenderGraphResourceAccess::RenderTarget );

    RenderGraphCallbackTrace trace;
    const uint32_t pass = graph.AddPass( "DisabledCallback" );
    graph.AddWrite( pass, target, RenderGraphResourceAccess::RenderTarget );
    graph.SetPassCallback<RecordRenderGraphCallback>( pass, trace, false, "disabled" );

    const RenderGraphCallbackExecutionResult result =
        graph.ExecuteCallbacks( RenderGraphCallbackExecutionMode::Execute );

    EXPECT_EQ( result.callbackPassCount, static_cast<size_t>( 1 ) );
    EXPECT_EQ( result.disabledCallbackPassCount, static_cast<size_t>( 1 ) );
    EXPECT_EQ( result.executedPassCount, static_cast<size_t>( 0 ) );
    EXPECT_TRUE( trace.labels.empty() );
}

void TestRenderGraphRejectsCallbackWithoutResourceDeclarations()
{
    EXPECT_FATAL_CASE( "callback-without-resources" );
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

    EXPECT_TRUE( TryDx12RenderGraphAccessToResourceState( RenderGraphResourceAccess::ShaderResource, state ) );
    EXPECT_TRUE( state ==
                 ( D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE | D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE ) );

    EXPECT_TRUE(
        TryDx12RenderGraphAccessToResourceState( RenderGraphResourceAccess::VertexAndNonPixelShaderResource, state ) );
    EXPECT_TRUE( state ==
                 ( D3D12_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER | D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE ) );

    EXPECT_TRUE( !TryDx12RenderGraphAccessToResourceState( RenderGraphResourceAccess::Unknown, state ) );
}

void TestDx12RenderGraphExecutorDryRunBackbufferTransitions()
{
    ID3D12Resource* fakeBackbuffer = reinterpret_cast<ID3D12Resource*>( static_cast<uintptr_t>( 0x1000u ) );

    RenderGraph graph;
    const RenderGraphResourceHandle backbuffer =
        graph.AddExternalResource( "Backbuffer",
                                   RenderGraphResourceAccess::Present,
                                   RenderGraphNativeResourceToken::From( fakeBackbuffer ) );

    const uint32_t drawPass = graph.AddPass( "Draw" );
    graph.AddWrite( drawPass, backbuffer, RenderGraphResourceAccess::RenderTarget );

    const uint32_t presentPass = graph.AddPass( "Present" );
    graph.AddWrite( presentPass, backbuffer, RenderGraphResourceAccess::Present );

    const RenderGraphCompileResult compiled = graph.Compile();
    Dx12RenderGraphExecutionDesc desc;
    desc.mode = Dx12RenderGraphExecutionMode::DryRun;
    desc.sourcePrefix = "GraphDryRun";

    const Dx12RenderGraphExecutionResult result = ExecuteDx12RenderGraphTransitions( graph, compiled, desc );

    EXPECT_EQ( result.barrierCount, static_cast<size_t>( 2 ) );
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
    const RenderGraphResourceHandle legacyTarget =
        graph.AddExternalResource( "LegacyTarget",
                                   RenderGraphResourceAccess::Unknown,
                                   RenderGraphNativeResourceToken::From(
                                       reinterpret_cast<ID3D12Resource*>( static_cast<uintptr_t>( 0x2000u ) ) ) );

    const uint32_t firstWriter = graph.AddPass( "FirstWriter" );
    graph.AddWrite( firstWriter, legacyTarget, RenderGraphResourceAccess::RenderTarget );

    const RenderGraphCompileResult compiled = graph.Compile();
    Dx12RenderGraphExecutionDesc desc;
    const Dx12RenderGraphExecutionResult result = ExecuteDx12RenderGraphTransitions( graph, compiled, desc );

    EXPECT_EQ( compiled.transitions.size(), static_cast<size_t>( 0 ) );
    EXPECT_EQ( result.barrierCount, static_cast<size_t>( 0 ) );
    EXPECT_EQ( result.unknownStateTransitionCount, static_cast<size_t>( 0 ) );
}

void TestDx12RenderGraphExecutorIdentifiesUavAccess()
{
    RenderGraph graph;
    const RenderGraphResourceHandle reflection =
        graph.AddExternalResource( "Reflection",
                                   RenderGraphResourceAccess::PixelShaderResource,
                                   RenderGraphNativeResourceToken::From(
                                       reinterpret_cast<ID3D12Resource*>( static_cast<uintptr_t>( 0x3000u ) ) ) );

    const uint32_t dispatchPass = graph.AddPass( "DispatchReflection", RenderGraphQueueType::Compute );
    graph.AddWrite( dispatchPass, reflection, RenderGraphResourceAccess::UnorderedAccess );

    const RenderGraphCompileResult compiled = graph.Compile();
    Dx12RenderGraphExecutionDesc desc;
    const Dx12RenderGraphExecutionResult result = ExecuteDx12RenderGraphTransitions( graph, compiled, desc );

    EXPECT_EQ( result.barrierCount, static_cast<size_t>( 1 ) );
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

void TestDx12SingleTransitionExecutionProducesRecord()
{
    Dx12RenderGraphSingleTransitionDesc desc;
    desc.commandList = nullptr;
    desc.resource = reinterpret_cast<ID3D12Resource*>( static_cast<uintptr_t>( 0x4100u ) );
    desc.before = RenderGraphResourceAccess::Present;
    desc.after = RenderGraphResourceAccess::RenderTarget;
    desc.subresource = 2u;

    const Dx12RenderGraphBarrierRecord record =
        ExecuteDx12RenderGraphSingleTransition( "Dx12Explicit", "Draw", "Backbuffer", desc );

    EXPECT_EQ( record.source, std::string( "Dx12Explicit:Draw" ) );
    EXPECT_EQ( record.passName, std::string( "Draw" ) );
    EXPECT_EQ( record.resourceName, std::string( "Backbuffer" ) );
    EXPECT_TRUE( record.nativeResource == desc.resource );
    EXPECT_TRUE( record.beforeAccess == RenderGraphResourceAccess::Present );
    EXPECT_TRUE( record.afterAccess == RenderGraphResourceAccess::RenderTarget );
    EXPECT_EQ( record.subresource, 2u );
    EXPECT_TRUE( record.hasNativeResource );
    EXPECT_TRUE( record.hasConcreteStates );
    EXPECT_TRUE( record.missingCommandList );
    EXPECT_TRUE( !record.emitted );
    EXPECT_TRUE( record.beforeState == D3D12_RESOURCE_STATE_PRESENT );
    EXPECT_TRUE( record.afterState == D3D12_RESOURCE_STATE_RENDER_TARGET );
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

void TestDx12UavBarrierExecutionProducesRecord()
{
    Dx12RenderGraphUavBarrierDesc desc;
    desc.commandList = nullptr;
    desc.resource = reinterpret_cast<ID3D12Resource*>( static_cast<uintptr_t>( 0x5100u ) );

    const Dx12RenderGraphUavBarrierRecord record =
        ExecuteDx12RenderGraphUavBarrier( "Dx12Explicit", "DispatchReflection", "Reflection", desc );

    EXPECT_EQ( record.source, std::string( "Dx12Explicit:DispatchReflection" ) );
    EXPECT_EQ( record.resourceName, std::string( "Reflection" ) );
    EXPECT_TRUE( record.nativeResource == desc.resource );
    EXPECT_TRUE( record.hasNativeResource );
    EXPECT_TRUE( record.missingCommandList );
    EXPECT_TRUE( !record.emitted );
}

void TestDx12GraphTransientPoolSlotReuseAllowsSameCompileAlias()
{
    RenderGraphTransientResourceDesc desc;
    desc.kind = RenderGraphResourceKind::Texture2D;
    desc.format = RenderGraphResourceFormat::RGBA16F;
    desc.width = 64;
    desc.height = 32;
    desc.mipLevels = 1;
    desc.descriptors.renderTarget = true;
    desc.descriptors.shaderResource = true;

    GraphTransientResourceDX12 candidate;
    candidate.resource = reinterpret_cast<ID3D12Resource*>( static_cast<uintptr_t>( 0x7000u ) );
    candidate.desc = desc;
    candidate.poolSlot = 3;
    candidate.usedThisCompile = true;

    // Hazard: the graph compiler may assign two non-overlapping resources to
    // one pool slot in the same compile. Backend reuse must follow poolSlot
    // compatibility, not the temporary "used this compile" bookkeeping bit.
    EXPECT_TRUE( GraphTransientPoolSlotCanSatisfyDX12( candidate, 3, desc ) );
    EXPECT_TRUE( !GraphTransientPoolSlotCanSatisfyDX12( candidate, 4, desc ) );

    RenderGraphTransientResourceDesc incompatibleDesc = desc;
    incompatibleDesc.height = 64;
    EXPECT_TRUE( !GraphTransientPoolSlotCanSatisfyDX12( candidate, 3, incompatibleDesc ) );
}

bool RunFatalCase( const char* caseName )
{
    RenderGraph graph;
    if ( std::strcmp( caseName, "mixed-subresource-transition" ) == 0 )
    {
        const RenderGraphResourceHandle texture =
            graph.AddExternalResource( "MixedTexture", RenderGraphResourceAccess::PixelShaderResource );
        const uint32_t writeMipOne = graph.AddPass( "WriteMipOne" );
        graph.AddWrite( writeMipOne, texture, RenderGraphResourceAccess::UnorderedAccess, 1u );
        const uint32_t writeAll = graph.AddPass( "WriteAll" );
        graph.AddWrite( writeAll, texture, RenderGraphResourceAccess::RenderTarget );
        (void)graph.Compile();
        return true;
    }

    const RenderGraphResourceHandle texture =
        graph.AddExternalResource( "Texture", RenderGraphResourceAccess::PixelShaderResource );
    const uint32_t pass = graph.AddPass( "Pass" );
    if ( std::strcmp( caseName, "unknown-read-access" ) == 0 )
    {
        graph.AddRead( pass, texture, RenderGraphResourceAccess::Unknown );
    }
    else if ( std::strcmp( caseName, "unknown-write-access" ) == 0 )
    {
        graph.AddWrite( pass, texture, RenderGraphResourceAccess::Unknown );
    }
    else if ( std::strcmp( caseName, "bad-read-resource" ) == 0 )
    {
        RenderGraphResourceHandle badResource;
        badResource.index = texture.index + 100u;
        graph.AddRead( pass, badResource, RenderGraphResourceAccess::PixelShaderResource );
    }
    else if ( std::strcmp( caseName, "bad-write-pass" ) == 0 )
    {
        graph.AddWrite( pass + 100u, texture, RenderGraphResourceAccess::RenderTarget );
    }
    else if ( std::strcmp( caseName, "unused-transient" ) == 0 )
    {
        RenderGraph unusedGraph;
        unusedGraph.AddTransientResource( "Unused", MakeTransientColorDesc(), RenderGraphResourceAccess::Unknown );
        (void)unusedGraph.Compile();
    }
    else if ( std::strcmp( caseName, "callback-without-resources" ) == 0 )
    {
        RenderGraph callbackGraph;
        RenderGraphCallbackTrace trace;
        const uint32_t callbackPass = callbackGraph.AddPass( "MissingDeclarations" );
        callbackGraph.SetPassCallback<RecordRenderGraphCallback>( callbackPass, trace );
        (void)callbackGraph.ExecuteCallbacks( RenderGraphCallbackExecutionMode::DryRun );
    }
    else
    {
        return false;
    }
    return true;
}

void TestPipelineCommandStateResetRestoresReusableDefaults()
{
    Dx12PipelineCommandState state;
    state.m_activeShader = reinterpret_cast<ShaderDX12*>( 1 );
    state.m_viewport.Width = 800.0f;
    state.m_scissorRect.right = 800;
    state.m_currentRTV.ptr = 11;
    state.m_currentDSV.ptr = 22;
    state.m_currentRTVFormat = DXGI_FORMAT_R16G16B16A16_FLOAT;
    state.m_renderingToFBO = true;
    state.m_lastPSOHash = 123;
    state.m_pipelineBindingDirty = false;
    state.m_targetsDirty = false;

    state.Reset();

    EXPECT_TRUE( state.m_activeShader == nullptr );
    EXPECT_EQ( state.m_viewport.Width, 0.0f );
    EXPECT_EQ( state.m_scissorRect.right, 0L );
    EXPECT_EQ( state.m_currentRTV.ptr, static_cast<SIZE_T>( 0 ) );
    EXPECT_EQ( state.m_currentDSV.ptr, static_cast<SIZE_T>( 0 ) );
    EXPECT_TRUE( state.m_currentRTVFormat == DXGI_FORMAT_R8G8B8A8_UNORM );
    EXPECT_TRUE( !state.m_renderingToFBO );
    EXPECT_EQ( state.m_lastPSOHash, static_cast<size_t>( 0 ) );
    EXPECT_TRUE( state.m_pipelineBindingDirty );
    EXPECT_TRUE( state.m_targetsDirty );
}

void TestPlatformProfilerGpuStackRestoresAcrossSubmission()
{
    Dx12PlatformProfilerGpuStackState stack;
    EXPECT_TRUE( stack.CommitBegin( 4 ) );
    EXPECT_TRUE( stack.CommitBegin( 4 ) );
    EXPECT_EQ( stack.Depth(), 2 );

    const int suspendedDepth = stack.SuspendForSubmit();
    EXPECT_EQ( suspendedDepth, 2 );
    EXPECT_EQ( stack.Depth(), 0 );
    EXPECT_TRUE( stack.RestoreAfterSubmit( suspendedDepth, 4 ) );
    EXPECT_EQ( stack.Depth(), 2 );
    EXPECT_TRUE( stack.CommitEnd() );
    EXPECT_TRUE( stack.CommitEnd() );
    EXPECT_EQ( stack.Depth(), 0 );
}

void TestPlatformProfilerGpuStackRejectsOverflowAndUnderflow()
{
    Dx12PlatformProfilerGpuStackState stack;
    EXPECT_TRUE( stack.CommitBegin( 1 ) );
    EXPECT_TRUE( !stack.CommitBegin( 1 ) );
    EXPECT_EQ( stack.Depth(), 1 );
    EXPECT_TRUE( stack.CommitEnd() );
    EXPECT_TRUE( !stack.CommitEnd() );
    EXPECT_TRUE( !stack.RestoreAfterSubmit( 2, 1 ) );
    EXPECT_EQ( stack.Depth(), 0 );
}

void TestPlatformProfilerGpuStackResetClearsStaleDeviceEpoch()
{
    Dx12PlatformProfilerGpuStackState stack;
    EXPECT_TRUE( stack.CommitBegin( 4 ) );
    EXPECT_TRUE( stack.CommitBegin( 4 ) );
    stack.Reset();
    EXPECT_EQ( stack.Depth(), 0 );
    EXPECT_TRUE( !stack.CommitEnd() );
}

void TestUploadOverflowDropsSteadyCallerWithoutDrain()
{
    EXPECT_TRUE( SelectDx12UploadOverflowAction( false, RuntimeAllocationPhase::Render ) ==
                 Dx12UploadOverflowAction::DropCaller );
    EXPECT_TRUE( SelectDx12UploadOverflowAction( false, RuntimeAllocationPhase::SteadyGameplay ) ==
                 Dx12UploadOverflowAction::DropCaller );
    EXPECT_TRUE( SelectDx12UploadOverflowAction( false, RuntimeAllocationPhase::Replay ) ==
                 Dx12UploadOverflowAction::DropCaller );
    EXPECT_TRUE( SelectDx12UploadOverflowAction( false, RuntimeAllocationPhase::Physics ) ==
                 Dx12UploadOverflowAction::DropCaller );
}

void TestUploadReservationResolverInvokesOnlyColdRetry()
{
    int retryCount = 0;
    const Dx12UploadReservationResolution steady = ResolveDx12UploadReservation( false,
                                                                                 RuntimeAllocationPhase::Render,
                                                                                 [&]()
                                                                                 {
                                                                                     ++retryCount;
                                                                                     return true;
                                                                                 } );
    EXPECT_TRUE( !steady.allowed );
    EXPECT_TRUE( steady.dropped );
    EXPECT_TRUE( !steady.coldRetryAttempted );
    EXPECT_EQ( retryCount, 0 );

    const Dx12UploadReservationResolution cold = ResolveDx12UploadReservation( false,
                                                                               RuntimeAllocationPhase::SceneLoad,
                                                                               [&]()
                                                                               {
                                                                                   ++retryCount;
                                                                                   return true;
                                                                               } );
    EXPECT_TRUE( cold.allowed );
    EXPECT_TRUE( !cold.dropped );
    EXPECT_TRUE( cold.coldRetryAttempted );
    EXPECT_EQ( retryCount, 1 );
}

void TestUploadOverflowKeepsColdFlushRetry()
{
    EXPECT_TRUE( SelectDx12UploadOverflowAction( false, RuntimeAllocationPhase::SceneLoad ) ==
                 Dx12UploadOverflowAction::FlushAndRetry );
    EXPECT_TRUE( SelectDx12UploadOverflowAction( false, RuntimeAllocationPhase::BackendInit ) ==
                 Dx12UploadOverflowAction::FlushAndRetry );
    EXPECT_TRUE( SelectDx12UploadOverflowAction( true, RuntimeAllocationPhase::Render ) ==
                 Dx12UploadOverflowAction::Allocate );
}

void TestUploadRangeProbeRejectsArithmeticOverflow()
{
    const UINT64 maxValue = ( std::numeric_limits<UINT64>::max )();
    EXPECT_TRUE( !CanReserveDx12UploadRange( 32u, 1024u, maxValue, 256u ) );
    EXPECT_TRUE( !CanReserveDx12UploadRange( maxValue - 1u, maxValue, 1u, 256u ) );
    EXPECT_TRUE( CanReserveDx12UploadRange( 768u, 1024u, 256u, 256u ) );
}

const TestCase kTests[] = {
    { "Upload overflow drops steady caller without drain", TestUploadOverflowDropsSteadyCallerWithoutDrain },
    { "Upload overflow keeps cold flush retry", TestUploadOverflowKeepsColdFlushRetry },
    { "Upload reservation resolver invokes only cold retry", TestUploadReservationResolverInvokesOnlyColdRetry },
    { "Upload range probe rejects arithmetic overflow", TestUploadRangeProbeRejectsArithmeticOverflow },
    { "Platform profiler GPU stack restores across submission", TestPlatformProfilerGpuStackRestoresAcrossSubmission },
    { "Platform profiler GPU stack rejects overflow and underflow",
      TestPlatformProfilerGpuStackRejectsOverflowAndUnderflow },
    { "Platform profiler GPU stack reset clears stale device epoch",
      TestPlatformProfilerGpuStackResetClearsStaleDeviceEpoch },
    { "Pipeline command-state reset restores reusable defaults",
      TestPipelineCommandStateResetRestoresReusableDefaults },
    { "Command close failure does not commit closed state", TestCommandCloseFailureDoesNotCommitClosedState },
    { "Command close success commits closed state", TestCommandCloseSuccessCommitsClosedState },
    { "Allocator reset failure blocks list reset", TestAllocatorResetFailureBlocksListReset },
    { "List reset failure does not commit open state", TestListResetFailureDoesNotCommitOpenState },
    { "Successful list reset commits open state", TestSuccessfulListResetCommitsOpenState },
    { "Wait failure preserves recording epoch", TestWaitFailurePreservesRecordingEpoch },
    { "First command failure remains authoritative", TestFirstCommandFailureRemainsAuthoritative },
    { "Device reset clears command failure", TestDeviceResetClearsCommandFailure },
    { "Map result rejects failed HRESULT", TestMapResultRejectsFailedHresult },
    { "Map result rejects null pointer after success", TestMapResultRejectsNullPointerAfterSuccess },
    { "Map result accepts successful mapped pointer", TestMapResultAcceptsSuccessfulMappedPointer },
    { "GPU drain close failure blocks submission", TestGpuDrainCloseFailureBlocksSubmission },
    { "GPU drain wait failure blocks reopen and mutation", TestGpuDrainWaitFailureBlocksReopenAndMutation },
    { "GPU drain success allows mutation only after reopen", TestGpuDrainSuccessAllowsMutationOnlyAfterReopen },
    { "Submitted work signal failure blocks reuse and release", TestSubmittedWorkSignalFailureBlocksReuseAndRelease },
    { "Submitted work wait failure preserves completion fence", TestSubmittedWorkWaitFailurePreservesCompletionFence },
    { "Submitted work successful signal and wait allows reuse", TestSubmittedWorkSuccessfulSignalAndWaitAllowsReuse },
    { "Device loss blocks work and retains first failure", TestDeviceLossBlocksWorkAndRetainsFirstFailure },
    { "Device health reset allows new device work", TestDeviceHealthResetAllowsNewDeviceWork },
    { "Removed device allows terminal submitted-work abandon", TestRemovedDeviceAllowsTerminalSubmittedWorkAbandon },
    { "Recreation failure preserves published generation", TestRecreationFailurePreservesPublishedGeneration },
    { "Recreation publishes only after every candidate is ready",
      TestRecreationPublishesOnlyAfterEveryCandidateIsReady },
    { "Fault injection blocks first and subsequent submissions",
      TestFaultInjectionBlocksFirstAndSubsequentSubmissions },
    { "Unarmed fault injection allows submission accounting", TestUnarmedFaultInjectionAllowsSubmissionAccounting },
    { "Descriptor transient ranges are contiguous", TestDescriptorTransientRangeIsContiguous },
    { "Descriptor transient range capacity probes are atomic", TestDescriptorTransientRangeCapacityProbeIsAtomic },
    { "Static descriptor rows reuse with stable high-water", TestStaticDescriptorRowsReuseWithStableHighWater },
    { "Texture handle generations reject reused-slot aliases", TestTextureHandleGenerationRejectsReusedSlotAlias },
    { "Render graph skips Unknown initial transitions", TestRenderGraphSkipsUnknownInitialTransition },
    { "Render graph emits explicit initial-state transitions", TestRenderGraphExplicitInitialStateTransitions },
    { "Render graph tracks subresource transitions independently",
      TestRenderGraphTracksSubresourceTransitionsIndependently },
    { "Render graph allows uniform specific then all-subresource transition",
      TestRenderGraphAllowsUniformSpecificThenAllSubresourceTransition },
    { "Render graph clears specific state when it returns to all-state",
      TestRenderGraphClearsSpecificStateWhenItReturnsToAllState },
    { "Render graph rejects mixed specific then all-subresource transition",
      TestRenderGraphRejectsMixedSpecificThenAllSubresourceTransition },
    { "Render graph rejects Unknown pass access", TestRenderGraphRejectsUnknownPassAccess },
    { "Render graph rejects bad handles", TestRenderGraphRejectsBadHandles },
    { "Render graph plans transient resource lifetime", TestRenderGraphPlansTransientResourceLifetime },
    { "Render graph reuses compatible non-overlapping transient resources",
      TestRenderGraphReusesCompatibleNonOverlappingTransientResources },
    { "Render graph rejects unused transient resource", TestRenderGraphRejectsUnusedTransientResource },
    { "Render graph executes callbacks in pass order", TestRenderGraphExecutesCallbacksInPassOrder },
    { "Render graph frame edges keep only Present declaration-only",
      TestRenderGraphFrameEdgesKeepOnlyPresentDeclarationOnly },
    { "Render graph dry-run validates callbacks without executing",
      TestRenderGraphDryRunValidatesCallbacksWithoutExecuting },
    { "Render graph disabled callback does not execute", TestRenderGraphDisabledCallbackDoesNotExecute },
    { "Render graph rejects callback without resource declarations",
      TestRenderGraphRejectsCallbackWithoutResourceDeclarations },
    { "DX12 render graph access maps to DX12 states", TestDx12RenderGraphAccessMapsToDx12States },
    { "DX12 render graph executor dry-runs backbuffer transitions",
      TestDx12RenderGraphExecutorDryRunBackbufferTransitions },
    { "DX12 render graph executor skips Unknown initial access", TestDx12RenderGraphExecutorSkipsUnknownInitialAccess },
    { "DX12 render graph executor identifies UAV access", TestDx12RenderGraphExecutorIdentifiesUavAccess },
    { "DX12 single transition requires command list for emit", TestDx12SingleTransitionRequiresCommandListForEmit },
    { "DX12 single transition execution produces record", TestDx12SingleTransitionExecutionProducesRecord },
    { "DX12 UAV barrier requires command list for emit", TestDx12UavBarrierRequiresCommandListForEmit },
    { "DX12 UAV barrier execution produces record", TestDx12UavBarrierExecutionProducesRecord },
    { "DX12 graph transient pool-slot reuse allows same-compile alias",
      TestDx12GraphTransientPoolSlotReuseAllowsSameCompileAlias },
};

} // namespace

int main( int argc, char** argv )
{
    g_executablePath = argc > 0 ? argv[0] : nullptr;
    if ( argc == 3 && std::strcmp( argv[1], "--fatal-case" ) == 0 )
    {
        // A normal return means the engine failed to enforce the fatal
        // invariant. The parent test requires this child to terminate nonzero.
        (void)RunFatalCase( argv[2] );
        return 0;
    }

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
