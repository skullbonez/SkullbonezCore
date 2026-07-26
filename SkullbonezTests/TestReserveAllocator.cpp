//
// File: SkullbonezTests/TestReserveAllocator.cpp
// Purpose:
//   Lock allocation-tracker phase accounting and RuntimeReserveAllocator
//   policy contracts.
//
// Summary:
//   The allocator is a fixed-storage policy ledger. Owners register their
//   initial and hard capacity, replay owners may request bounded growth during
//   replay, and every grant or denial becomes a compact diagnostic event.
//   Reportable stores also publish per-scene capacity rows and unload text.
//
// Glossary:
//   Reserve owner: Named runtime storage owner with an initial capacity and hard
//     capacity budget.
//   Replay growth: Bounded capacity increase allowed only while replay tools are
//     doing replay-phase work.
//   Development tool owner: Thread-local ImGui or Tracy attribution that admits
//     bounded vendor storage without changing the process gameplay phase.
//   Growth event: Fixed-ring diagnostic row recording one grant or denial.
//   Capacity session: One scene's live/high-water interval, advanced after the
//     preceding scene is cleared.
//   Lifecycle phase: Always-on calling-thread label used by allocation and
//     upload policies even when allocation counting is disabled.
//   Allocation guard: Process-wide measurement mode that attributes global
//     heap requests to lifecycle phases and flags steady-gameplay violations.
//
// Invariants:
//   - Gameplay-phase owners never receive replay growth approval.
//   - Denied growth increments policy violations and still records an event.
//   - ResetCounters() clears counters/events without unregistering owners.
//   - RuntimeAllocationScope publishes/restores calling-thread phase
//     independently of guard mode and concurrent scopes.
//   - Development tool scopes do not mask an ordinary gameplay allocation.
//   - Tracker cases restore the process-wide guard to Off before returning.
//   - A new capacity session resets the visible and list-local peak lazily.
//
// Related:
//   - SkullbonezSource/Core/Allocation/RuntimeReserveAllocator.h
//   - SkullbonezSource/Core/Allocation/RuntimeReserveAllocator.cpp
//   - Agentic/Reports/behavioral_test_depth_closure_20260711.md
//

#include "../ThirdPtySource/doctest/doctest.h"

#include "../SkullbonezSource/Core/Allocation/RuntimeReserveAllocator.h"
#include "../SkullbonezSource/Core/Allocation/RuntimeAllocationTracker.h"
#include "../SkullbonezSource/Physics/PhysicsFixedList.h"
#if defined( SKULLBONEZ_DEVELOPMENT_TOOLS )
#include "../SkullbonezSource/Core/Allocation/DevelopmentToolAllocation.h"
#endif

#include <algorithm>
#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <new>
#include <ranges>
#include <stdexcept>
#include <string>
#include <thread>

using SkullbonezCore::Core::Allocation::GetRuntimeAllocationGuardMode;
using SkullbonezCore::Core::Allocation::GetRuntimeAllocationPhase;
using SkullbonezCore::Core::Allocation::INVALID_RUNTIME_RESERVE_OWNER;
using SkullbonezCore::Core::Allocation::PrintRuntimeAllocationSummary;
using SkullbonezCore::Core::Allocation::ResetRuntimeAllocationCounters;
using SkullbonezCore::Core::Allocation::RUNTIME_RESERVE_REPLAY_GROWTH_LIMIT_UNBOUNDED;
using SkullbonezCore::Core::Allocation::RuntimeAllocationGuardEnabled;
using SkullbonezCore::Core::Allocation::RuntimeAllocationGuardHasGameplayViolations;
using SkullbonezCore::Core::Allocation::RuntimeAllocationGuardMode;
using SkullbonezCore::Core::Allocation::RuntimeAllocationGuardModeName;
using SkullbonezCore::Core::Allocation::RuntimeAllocationGuardViolationCount;
using SkullbonezCore::Core::Allocation::RuntimeAllocationPhase;
using SkullbonezCore::Core::Allocation::RuntimeAllocationPhaseName;
using SkullbonezCore::Core::Allocation::RuntimeAllocationScope;
using SkullbonezCore::Core::Allocation::RuntimeReserveAllocator;
using SkullbonezCore::Core::Allocation::RuntimeReserveCapacityView;
using SkullbonezCore::Core::Allocation::RuntimeReserveGrowthEventView;
using SkullbonezCore::Core::Allocation::RuntimeReserveGrowthRequest;
using SkullbonezCore::Core::Allocation::RuntimeReserveGrowthResult;
using SkullbonezCore::Core::Allocation::RuntimeReserveGrowthScope;
using SkullbonezCore::Core::Allocation::RuntimeReserveOwnerDesc;
using SkullbonezCore::Core::Allocation::RuntimeReserveOwnerHandle;
using SkullbonezCore::Core::Allocation::RuntimeReserveOwnerScope;
using SkullbonezCore::Core::Allocation::RuntimeReserveOwnerStatsView;
using SkullbonezCore::Core::Allocation::RuntimeReservePhase;
using SkullbonezCore::Core::Allocation::RuntimeReserveSubsystem;
using SkullbonezCore::Core::Allocation::SetRuntimeAllocationGuardMode;
using SkullbonezCore::Core::Allocation::SetRuntimeAllocationPhase;
using SkullbonezCore::Physics::PhysicsCapacityReason::ExplicitTestCapacity;
using SkullbonezCore::Physics::PhysicsFixedList;
#if defined( SKULLBONEZ_DEVELOPMENT_TOOLS )
using SkullbonezCore::Core::Allocation::CopyDevelopmentToolAllocationStats;
using SkullbonezCore::Core::Allocation::DevelopmentToolAllocationOwner;
using SkullbonezCore::Core::Allocation::DevelopmentToolAllocationScope;
using SkullbonezCore::Core::Allocation::DevelopmentToolAllocationStats;
using SkullbonezCore::Core::Allocation::ReleaseDevelopmentToolBackingMemory;
using SkullbonezCore::Core::Allocation::TryAccountDevelopmentToolBackingMemory;
#endif

namespace
{
// Why: owner registration persists for the process lifetime; unique owner names
// let ResetCounters() clear diagnostics between cases without a registry teardown.
RuntimeReserveOwnerDesc MakeReplayOwnerDesc( const char* ownerName,
                                             int initialCapacity = 4,
                                             int hardCapacity = 10,
                                             int growthLimit = RUNTIME_RESERVE_REPLAY_GROWTH_LIMIT_UNBOUNDED )
{
    RuntimeReserveOwnerDesc desc = {};
    desc.ownerName = ownerName;
    desc.subsystem = RuntimeReserveSubsystem::Replay;
    desc.initPhase = RuntimeReservePhase::Startup;
    desc.initialCapacity = initialCapacity;
    desc.hardCapacity = hardCapacity;
    desc.replayGrowthLimit = growthLimit;
    desc.allowReplayGrowth = true;
    desc.capacityReason = "unit test reserve owner";
    return desc;
}

TEST_CASE( "RuntimeAllocationScope: lifecycle phase remains active when allocation counting is off" )
{
    SetRuntimeAllocationGuardMode( RuntimeAllocationGuardMode::Off );
    SetRuntimeAllocationPhase( RuntimeAllocationPhase::Startup );
    {
        RuntimeAllocationScope renderScope( RuntimeAllocationPhase::Render );
        CHECK( GetRuntimeAllocationPhase() == RuntimeAllocationPhase::Render );
    }
    CHECK( GetRuntimeAllocationPhase() == RuntimeAllocationPhase::Startup );
}

TEST_CASE( "RuntimeAllocationScope: concurrent threads retain independent nested phases" )
{
    SetRuntimeAllocationGuardMode( RuntimeAllocationGuardMode::Off );
    SetRuntimeAllocationPhase( RuntimeAllocationPhase::Startup );
    std::atomic<bool> workerEnteredReplay{ false };
    std::atomic<bool> mainObservedRender{ false };
    std::atomic<bool> workerPhasesCorrect{ false };

    {
        RuntimeAllocationScope mainRenderScope( RuntimeAllocationPhase::Render );
        std::thread worker( [&]() {
            SetRuntimeAllocationPhase( RuntimeAllocationPhase::BackendInit );
            bool phasesCorrect = GetRuntimeAllocationPhase() == RuntimeAllocationPhase::BackendInit;
            {
                RuntimeAllocationScope workerReplayScope( RuntimeAllocationPhase::Replay );
                phasesCorrect = phasesCorrect && GetRuntimeAllocationPhase() == RuntimeAllocationPhase::Replay;
                workerEnteredReplay.store( true, std::memory_order_release );
                while ( !mainObservedRender.load( std::memory_order_acquire ) )
                {
                    std::this_thread::yield();
                }
                phasesCorrect = phasesCorrect && GetRuntimeAllocationPhase() == RuntimeAllocationPhase::Replay;
            }
            phasesCorrect = phasesCorrect && GetRuntimeAllocationPhase() == RuntimeAllocationPhase::BackendInit;
            workerPhasesCorrect.store( phasesCorrect, std::memory_order_release );
        } );

        while ( !workerEnteredReplay.load( std::memory_order_acquire ) )
        {
            std::this_thread::yield();
        }
        CHECK( GetRuntimeAllocationPhase() == RuntimeAllocationPhase::Render );
        mainObservedRender.store( true, std::memory_order_release );
        worker.join();
        CHECK( workerPhasesCorrect.load( std::memory_order_acquire ) );
        CHECK( GetRuntimeAllocationPhase() == RuntimeAllocationPhase::Render );
    }
    CHECK( GetRuntimeAllocationPhase() == RuntimeAllocationPhase::Startup );
}

RuntimeReserveGrowthRequest MakeGrowthRequest( const char* ownerName,
                                               int oldCapacity,
                                               int requestedCapacity,
                                               RuntimeReservePhase phase = RuntimeReservePhase::Replay )
{
    RuntimeReserveGrowthRequest request = {};
    request.ownerName = ownerName;
    request.targetName = "unit-test-buffer";
    request.phase = phase;
    request.frameNumber = 42;
    request.oldCapacity = oldCapacity;
    request.requestedCapacity = requestedCapacity;
    request.elementSizeBytes = 16;
    return request;
}

RuntimeReserveGrowthEventView LatestGrowthEvent()
{
    RuntimeReserveGrowthEventView events[2] = {};
    REQUIRE( RuntimeReserveAllocator::CopyRecentGrowthEvents( events, 2 ) >= 1 );
    return events[0];
}

void CheckEventText( const char* actual, const char* expected )
{
    REQUIRE( actual != nullptr );
    CHECK( std::string( actual ) == expected );
}

std::string ReadFileText( FILE* file )
{
    std::string text;
    std::rewind( file );
    char buffer[1024] = {};
    while ( const size_t bytesRead = std::fread( buffer, 1u, sizeof( buffer ), file ) )
    {
        text.append( buffer, bytesRead );
    }
    return text;
}
} // namespace

TEST_CASE( "RuntimeAllocationTracker: public mode and phase names cover every lifecycle label" )
{
    CHECK( std::string( RuntimeAllocationGuardModeName( RuntimeAllocationGuardMode::Off ) ) == "off" );
    CHECK( std::string( RuntimeAllocationGuardModeName( RuntimeAllocationGuardMode::Measure ) ) == "measure" );
    CHECK( std::string( RuntimeAllocationGuardModeName( RuntimeAllocationGuardMode::Gameplay ) ) == "gameplay" );
    CHECK( std::string( RuntimeAllocationGuardModeName( static_cast<RuntimeAllocationGuardMode>( 99 ) ) ) ==
           "unknown" );

    const std::array<const char*, static_cast<size_t>( RuntimeAllocationPhase::Count )> expected = { "startup",
                                                                                                     "scene_load",
                                                                                                     "backend_init",
                                                                                                     "steady_gameplay",
                                                                                                     "physics",
                                                                                                     "render",
                                                                                                     "replay",
                                                                                                     "capture",
                                                                                                     "diagnostics",
                                                                                                     "shutdown" };
    for ( size_t index = 0; index < expected.size(); ++index )
    {
        CHECK( std::string( RuntimeAllocationPhaseName( static_cast<RuntimeAllocationPhase>( index ) ) ) ==
               expected[index] );
    }
    CHECK( std::string( RuntimeAllocationPhaseName( RuntimeAllocationPhase::Count ) ) == "unknown" );
}

TEST_CASE( "RuntimeAllocationTracker: measured allocations are attributed and freed in their source phase" )
{
    SetRuntimeAllocationGuardMode( RuntimeAllocationGuardMode::Measure );
    REQUIRE( GetRuntimeAllocationGuardMode() == RuntimeAllocationGuardMode::Measure );
    REQUIRE( RuntimeAllocationGuardEnabled() );
    SetRuntimeAllocationPhase( RuntimeAllocationPhase::Diagnostics );

    int* scalar = new int( 17 );
    int* array = new int[4]{};
    REQUIRE( scalar != nullptr );
    REQUIRE( array != nullptr );
    delete scalar;
    delete[] array;

    SetRuntimeAllocationGuardMode( RuntimeAllocationGuardMode::Off );
    FILE* output = nullptr;
    REQUIRE( tmpfile_s( &output ) == 0 );
    REQUIRE( output != nullptr );
    SetRuntimeAllocationGuardMode( RuntimeAllocationGuardMode::Measure );
    SetRuntimeAllocationPhase( RuntimeAllocationPhase::Diagnostics );
    int* reported = new int( 23 );
    delete reported;
    PrintRuntimeAllocationSummary( output );
    SetRuntimeAllocationGuardMode( RuntimeAllocationGuardMode::Off );

    const std::string summary = ReadFileText( output );
    std::fclose( output );
    CHECK( summary.find( "mode=measure" ) != std::string::npos );
    CHECK( summary.find( "phase=diagnostics" ) != std::string::npos );
    CHECK( summary.find( "allocations=1" ) != std::string::npos );
    CHECK( summary.find( "frees=1" ) != std::string::npos );
    CHECK( summary.find( "PASS: no steady gameplay allocations" ) != std::string::npos );
}

TEST_CASE( "RuntimeAllocationTracker: gameplay guard reports a physics allocation violation" )
{
    FILE* output = nullptr;
    REQUIRE( tmpfile_s( &output ) == 0 );
    REQUIRE( output != nullptr );
    SetRuntimeAllocationGuardMode( RuntimeAllocationGuardMode::Gameplay );
    SetRuntimeAllocationPhase( RuntimeAllocationPhase::Physics );

    int* value = new int( 31 );
    delete value;
    const uint64_t violations = RuntimeAllocationGuardViolationCount();
    const bool hasViolations = RuntimeAllocationGuardHasGameplayViolations();
    PrintRuntimeAllocationSummary( output );
    SetRuntimeAllocationGuardMode( RuntimeAllocationGuardMode::Off );

    const std::string summary = ReadFileText( output );
    std::fclose( output );
    CHECK( violations >= 1u );
    CHECK( hasViolations );
    CHECK( summary.find( "mode=gameplay" ) != std::string::npos );
    CHECK( summary.find( "phase=physics" ) != std::string::npos );
    CHECK( summary.find( "VIOLATION:" ) != std::string::npos );
}

#if defined( SKULLBONEZ_DEVELOPMENT_TOOLS )
TEST_CASE( "Development tool allocation scopes remain separate without masking gameplay violations" )
{
    SetRuntimeAllocationGuardMode( RuntimeAllocationGuardMode::Gameplay );
    SetRuntimeAllocationPhase( RuntimeAllocationPhase::Render );

    {
        DevelopmentToolAllocationScope imguiScope( DevelopmentToolAllocationOwner::DearImGui );
        void* imguiBlock = ::operator new( 32u );
        ::operator delete( imguiBlock );
    }
    {
        DevelopmentToolAllocationScope tracyScope( DevelopmentToolAllocationOwner::Tracy );
        void* tracyBlock = ::operator new( 48u );
        ::operator delete( tracyBlock );
    }
    const bool tracyBackingReserved =
        TryAccountDevelopmentToolBackingMemory( DevelopmentToolAllocationOwner::Tracy, 64u * 1024u );

    const uint64_t toolScopeViolations = RuntimeAllocationGuardViolationCount();
    DevelopmentToolAllocationStats imguiStats;
    DevelopmentToolAllocationStats tracyStats;
    const bool copiedImGui =
        CopyDevelopmentToolAllocationStats( DevelopmentToolAllocationOwner::DearImGui, imguiStats );
    const bool copiedTracy = CopyDevelopmentToolAllocationStats( DevelopmentToolAllocationOwner::Tracy, tracyStats );
    if ( tracyBackingReserved )
    {
        ReleaseDevelopmentToolBackingMemory( DevelopmentToolAllocationOwner::Tracy, 64u * 1024u );
    }

    // Acceptance probe: this unscoped allocation uses the same Render phase as
    // the tool calls. It must still fail the gameplay guard.
    void* gameplayBlock = ::operator new( 16u );
    ::operator delete( gameplayBlock );
    const uint64_t finalViolations = RuntimeAllocationGuardViolationCount();
    const bool guardFailed = RuntimeAllocationGuardHasGameplayViolations();
    SetRuntimeAllocationGuardMode( RuntimeAllocationGuardMode::Off );

    CHECK( toolScopeViolations == 0u );
    CHECK( copiedImGui );
    CHECK( copiedTracy );
    CHECK( tracyBackingReserved );
    CHECK( imguiStats.allocations == 1u );
    CHECK( tracyStats.allocations == 2u );
    CHECK( imguiStats.highWaterBytes >= 32u );
    CHECK( tracyStats.activeBytes >= 64u * 1024u );
    CHECK( tracyStats.highWaterBytes >= 64u * 1024u );
    CHECK( imguiStats.hardCapBytes == 64 * 1024 * 1024 );
    CHECK( tracyStats.hardCapBytes == 512 * 1024 * 1024 );
    CHECK( finalViolations >= 1u );
    CHECK( guardFailed );
}
#endif

TEST_CASE( "RuntimeAllocationTracker: global allocation overloads preserve alignment and null-delete behavior" )
{
    struct alignas( 64 ) AlignedValue
    {
        unsigned char bytes[64];
    };

    SetRuntimeAllocationGuardMode( RuntimeAllocationGuardMode::Measure );
    void* scalarNothrow = ::operator new( 7u, std::nothrow );
    void* arrayNothrow = ::operator new[]( 9u, std::nothrow );
    void* aligned = ::operator new( sizeof( AlignedValue ), std::align_val_t( alignof( AlignedValue ) ) );
    void* alignedArray =
        ::operator new[]( sizeof( AlignedValue ) * 2u, std::align_val_t( alignof( AlignedValue ) ), std::nothrow );
    REQUIRE( scalarNothrow != nullptr );
    REQUIRE( arrayNothrow != nullptr );
    REQUIRE( aligned != nullptr );
    REQUIRE( alignedArray != nullptr );
    CHECK( reinterpret_cast<uintptr_t>( aligned ) % alignof( AlignedValue ) == 0u );
    CHECK( reinterpret_cast<uintptr_t>( alignedArray ) % alignof( AlignedValue ) == 0u );

    ::operator delete( scalarNothrow, std::nothrow );
    ::operator delete[]( arrayNothrow, std::nothrow );
    ::operator delete( aligned, std::align_val_t( alignof( AlignedValue ) ) );
    ::operator delete[]( alignedArray, std::align_val_t( alignof( AlignedValue ) ), std::nothrow );
    ::operator delete( nullptr );
    ResetRuntimeAllocationCounters();
    CHECK( RuntimeAllocationGuardViolationCount() == 0u );
    SetRuntimeAllocationGuardMode( RuntimeAllocationGuardMode::Off );

    PrintRuntimeAllocationSummary( nullptr );
    CHECK_FALSE( RuntimeAllocationGuardEnabled() );
}


TEST_CASE( "RuntimeReserveAllocator: replay growth under cap grants and records bytes" )
{
    RuntimeReserveAllocator::ResetCounters();
    constexpr const char* ownerName = "unit.reserve.e1.grant";
    const RuntimeReserveOwnerHandle owner = RuntimeReserveAllocator::RegisterOwner( MakeReplayOwnerDesc( ownerName ) );
    REQUIRE( owner != INVALID_RUNTIME_RESERVE_OWNER );

    const RuntimeReserveGrowthResult result =
        RuntimeReserveAllocator::RequestGrowth( owner, MakeGrowthRequest( ownerName, 4, 8 ) );

    CHECK( result.granted );
    CHECK( result.grantedCapacity == 8 );
    CHECK( result.growthCount == 1 );
    CHECK_FALSE( RuntimeReserveAllocator::HasPolicyViolations() );
    CHECK( RuntimeReserveAllocator::GrowthEventCount() == 1u );

    const RuntimeReserveGrowthEventView event = LatestGrowthEvent();
    CHECK( event.granted );
    CheckEventText( event.ownerName, ownerName );
    CheckEventText( event.targetName, "unit-test-buffer" );
    CheckEventText( event.phaseName, "replay" );
    CheckEventText( event.reason, "granted" );
    CHECK( event.bytes == 64u );
    CHECK( event.oldCapacity == 4 );
    CHECK( event.requestedCapacity == 8 );
    CHECK( event.grantedCapacity == 8 );
    CHECK( event.elementSizeBytes == 16 );
    CHECK( event.growthCount == 1 );
}


TEST_CASE( "RuntimeReserveAllocator: replay growth scope approves only the granted replay window" )
{
    RuntimeReserveAllocator::ResetCounters();
    constexpr const char* ownerName = "unit.reserve.e1.scope";
    const RuntimeReserveOwnerHandle owner = RuntimeReserveAllocator::RegisterOwner( MakeReplayOwnerDesc( ownerName ) );
    REQUIRE( owner != INVALID_RUNTIME_RESERVE_OWNER );
    const RuntimeReserveGrowthResult result =
        RuntimeReserveAllocator::RequestGrowth( owner, MakeGrowthRequest( ownerName, 4, 6 ) );
    REQUIRE( result.granted );

    CHECK_FALSE( RuntimeReserveAllocator::IsApprovedReplayGrowthAllocation( owner, 6 ) );
    {
        RuntimeReserveGrowthScope scope( owner, RuntimeReservePhase::Replay, result );
        CHECK( RuntimeReserveAllocator::IsApprovedReplayGrowthAllocation( owner, 6 ) );
        CHECK_FALSE( RuntimeReserveAllocator::IsApprovedReplayGrowthAllocation( owner, 3 ) );
    }
    CHECK_FALSE( RuntimeReserveAllocator::IsApprovedReplayGrowthAllocation( owner, 6 ) );
}


TEST_CASE( "RuntimeReserveAllocator: over-cap growth denies and records a policy violation" )
{
    RuntimeReserveAllocator::ResetCounters();
    constexpr const char* ownerName = "unit.reserve.e1.over-cap";
    const RuntimeReserveOwnerHandle owner =
        RuntimeReserveAllocator::RegisterOwner( MakeReplayOwnerDesc( ownerName, 4, 6 ) );
    REQUIRE( owner != INVALID_RUNTIME_RESERVE_OWNER );

    const RuntimeReserveGrowthResult result =
        RuntimeReserveAllocator::RequestGrowth( owner, MakeGrowthRequest( ownerName, 4, 8 ) );

    CHECK_FALSE( result.granted );
    CHECK( result.grantedCapacity == 4 );
    CHECK( result.growthCount == 0 );
    CHECK( RuntimeReserveAllocator::HasPolicyViolations() );
    CHECK( RuntimeReserveAllocator::PolicyViolationCount() == 1u );

    const RuntimeReserveGrowthEventView event = LatestGrowthEvent();
    CHECK_FALSE( event.granted );
    CheckEventText( event.reason, "capacity_out_of_range" );
    CHECK( event.bytes == 0u );
    CHECK( event.grantedCapacity == 4 );
}


TEST_CASE( "RuntimeReserveAllocator: replay growth count limit denies later bumps" )
{
    RuntimeReserveAllocator::ResetCounters();
    constexpr const char* ownerName = "unit.reserve.e1.limit";
    const RuntimeReserveOwnerHandle owner =
        RuntimeReserveAllocator::RegisterOwner( MakeReplayOwnerDesc( ownerName, 4, 12, 1 ) );
    REQUIRE( owner != INVALID_RUNTIME_RESERVE_OWNER );

    const RuntimeReserveGrowthResult first =
        RuntimeReserveAllocator::RequestGrowth( owner, MakeGrowthRequest( ownerName, 4, 6 ) );
    const RuntimeReserveGrowthResult second =
        RuntimeReserveAllocator::RequestGrowth( owner, MakeGrowthRequest( ownerName, 6, 8 ) );

    CHECK( first.granted );
    CHECK( first.growthCount == 1 );
    CHECK_FALSE( second.granted );
    CHECK( second.grantedCapacity == 6 );
    CHECK( second.growthCount == 1 );
    CHECK( RuntimeReserveAllocator::PolicyViolationCount() == 1u );
    CHECK( RuntimeReserveAllocator::GrowthEventCount() == 2u );
    CheckEventText( LatestGrowthEvent().reason, "growth_count_limit" );
}


TEST_CASE( "RuntimeReserveAllocator: replay byte owners share one active allocation cap" )
{
    RuntimeReserveAllocator::ResetCounters();
    constexpr const char* ownerName = "unit.reserve.e1.aggregate-bytes";
    const RuntimeReserveOwnerHandle owner =
        RuntimeReserveAllocator::RegisterOwner( MakeReplayOwnerDesc( ownerName, 0, 100 ) );
    REQUIRE( owner != INVALID_RUNTIME_RESERVE_OWNER );

    RuntimeReserveAllocator::RecordAllocation( owner, 6, 80u );
    RuntimeReserveGrowthRequest request = MakeGrowthRequest( ownerName, 0, 30 );
    request.elementSizeBytes = 1;
    const RuntimeReserveGrowthResult result = RuntimeReserveAllocator::RequestGrowth( owner, request );

    CHECK_FALSE( result.granted );
    CHECK( RuntimeReserveAllocator::PolicyViolationCount() == 1u );
    CheckEventText( LatestGrowthEvent().reason, "owner_byte_budget" );

    RuntimeReserveOwnerStatsView stats = {};
    REQUIRE( RuntimeReserveAllocator::CopyOwnerStats( owner, stats ) );
    CHECK( stats.activeBytes == 80u );
    CHECK( stats.failedGrowths == 1u );
    RuntimeReserveAllocator::RecordFree( owner, 80u );
}


TEST_CASE( "RuntimeReserveAllocator: ResetCounters clears growth events without unregistering owners" )
{
    RuntimeReserveAllocator::ResetCounters();
    constexpr const char* ownerName = "unit.reserve.e1.reset";
    const RuntimeReserveOwnerHandle owner =
        RuntimeReserveAllocator::RegisterOwner( MakeReplayOwnerDesc( ownerName, 4, 10, 1 ) );
    REQUIRE( owner != INVALID_RUNTIME_RESERVE_OWNER );
    REQUIRE( RuntimeReserveAllocator::RequestGrowth( owner, MakeGrowthRequest( ownerName, 4, 6 ) ).granted );
    REQUIRE( RuntimeReserveAllocator::GrowthEventCount() == 1u );

    RuntimeReserveAllocator::ResetCounters();

    CHECK( RuntimeReserveAllocator::GrowthEventCount() == 0u );
    CHECK_FALSE( RuntimeReserveAllocator::HasPolicyViolations() );
    RuntimeReserveGrowthEventView events[1] = {};
    CHECK( RuntimeReserveAllocator::CopyRecentGrowthEvents( events, 1 ) == 0 );
    const RuntimeReserveGrowthResult afterReset =
        RuntimeReserveAllocator::RequestGrowth( owner, MakeGrowthRequest( ownerName, 4, 6 ) );
    CHECK( afterReset.granted );
    CHECK( afterReset.growthCount == 1 );
}


TEST_CASE( "RuntimeReserveAllocator: owner stats expose fixed-registry growth evidence by name" )
{
    RuntimeReserveAllocator::ResetCounters();
    constexpr const char* ownerName = "unit.reserve.e1.owner-stats";
    const RuntimeReserveOwnerHandle owner =
        RuntimeReserveAllocator::RegisterOwner( MakeReplayOwnerDesc( ownerName, 4, 12 ) );
    REQUIRE( owner != INVALID_RUNTIME_RESERVE_OWNER );
    REQUIRE( RuntimeReserveAllocator::RequestGrowth( owner, MakeGrowthRequest( ownerName, 4, 9 ) ).granted );

    RuntimeReserveOwnerStatsView stats = {};
    REQUIRE( RuntimeReserveAllocator::CopyOwnerStatsByName( ownerName, stats ) );
    CHECK( std::string( stats.ownerName ) == ownerName );
    CHECK( stats.subsystem == RuntimeReserveSubsystem::Replay );
    CHECK( stats.initPhase == RuntimeReservePhase::Startup );
    CHECK( stats.currentCapacity == 9 );
    CHECK( stats.hardCapacity == 12 );
    CHECK( stats.highWaterCapacity == 9 );
    CHECK( stats.replayGrowths == 1u );
    CHECK( stats.failedGrowths == 0u );
    CHECK( stats.lastGrowthFrame == 42 );
    CHECK( stats.allowReplayGrowth );

    RuntimeReserveOwnerStatsView missing = {};
    CHECK_FALSE( RuntimeReserveAllocator::CopyOwnerStatsByName( "unit.reserve.e1.missing", missing ) );
}


TEST_CASE( "PhysicsFixedList: scene-load reserve fills exact runtime capacity through contiguous pointers" )
{
    using List = PhysicsFixedList<int, 8>;
    static_assert( std::ranges::contiguous_range<List> );
    static_assert( std::ranges::contiguous_range<const List> );

    RuntimeAllocationScope sceneLoad( RuntimeAllocationPhase::SceneLoad );
    List values( "unit.physics-fixed-list.reserve-fill", ExplicitTestCapacity );
    CHECK( values.owner_handle() != INVALID_RUNTIME_RESERVE_OWNER );
    CHECK( std::strcmp( values.capacity_reason(), ExplicitTestCapacity ) == 0 );
    CHECK( values.capacity() == 0u );
    CHECK( values.max_capacity() == 8u );
    values.Reserve( 3u );
    CHECK( values.capacity() == 3u );
    CHECK( reinterpret_cast<std::uintptr_t>( values.data() ) % 32u == 0u );

    values.push_back( 11 );
    values.push_back( 22 );
    values.push_back( 33 );
    CHECK( values.size() == values.capacity() );
    CHECK( values.high_water() == 3u );
    CHECK( values.end() - values.begin() == 3 );
    CHECK( values.data()[0] == 11 );
    CHECK( values.data()[2] == 33 );

    const auto findCapacityRow = []( const char* ownerName ) -> const RuntimeReserveCapacityView* {
        const std::span<const RuntimeReserveCapacityView> rows = RuntimeReserveAllocator::CapacityRows();
        const auto row = std::find_if( rows.begin(), rows.end(), [ownerName]( const RuntimeReserveCapacityView& candidate ) {
            return candidate.ownerName && std::strcmp( candidate.ownerName, ownerName ) == 0;
        } );
        return row != rows.end() ? &*row : nullptr;
    };

    ResetRuntimeAllocationCounters();
    SetRuntimeAllocationGuardMode( RuntimeAllocationGuardMode::Gameplay );
    const RuntimeReserveCapacityView* filled = nullptr;
    {
        RuntimeAllocationScope steadyGameplay( RuntimeAllocationPhase::SteadyGameplay );
        filled = findCapacityRow( "unit.physics-fixed-list.reserve-fill" );
    }
    const uint64_t queryAllocationViolations = RuntimeAllocationGuardViolationCount();
    SetRuntimeAllocationGuardMode( RuntimeAllocationGuardMode::Off );

    CHECK( queryAllocationViolations == 0u );
    REQUIRE( filled != nullptr );
    CHECK( filled->subsystem == RuntimeReserveSubsystem::Physics );
    CHECK( filled->elementSizeBytes == static_cast<int>( sizeof( int ) ) );
    CHECK( filled->currentCapacity == 3 );
    CHECK( filled->liveCount == 3 );
    CHECK( filled->sessionHighWater == 3 );
    CHECK( filled->residentBytes == 3u * sizeof( int ) );

    values.clear();
    const RuntimeReserveCapacityView* cleared = findCapacityRow( "unit.physics-fixed-list.reserve-fill" );
    REQUIRE( cleared != nullptr );
    CHECK( cleared->currentCapacity == 3 );
    CHECK( cleared->liveCount == 0 );
    CHECK( cleared->sessionHighWater == 3 );
    CHECK( cleared->residentBytes == 3u * sizeof( int ) );

    RuntimeReserveAllocator::BeginCapacitySession();
    values.push_back( 44 );
    const RuntimeReserveCapacityView* nextScene = findCapacityRow( "unit.physics-fixed-list.reserve-fill" );
    REQUIRE( nextScene != nullptr );
    CHECK( nextScene->currentCapacity == 3 );
    CHECK( nextScene->liveCount == 1 );
    CHECK( nextScene->sessionHighWater == 1 );

    FILE* capacityLog = nullptr;
    REQUIRE( tmpfile_s( &capacityLog ) == 0 );
    REQUIRE( capacityLog != nullptr );
    RuntimeReserveAllocator::PrintCapacityRows( capacityLog, "unit-capacity.scene", "scene_unload" );
    const std::string capacityText = ReadFileText( capacityLog );
    std::fclose( capacityLog );
    CHECK( capacityText.find( "[capacity] scene=\"unit-capacity.scene\" status=scene_unload" ) != std::string::npos );
    CHECK( capacityText.find( "owner=\"unit.physics-fixed-list.reserve-fill\"" ) != std::string::npos );
    CHECK( capacityText.find( "capacity=3 live=1 high_water=1 utilisation=33.33% resident_bytes=12" ) !=
           std::string::npos );
}


TEST_CASE( "PhysicsFixedList: runtime and compile-time ceilings remain distinct" )
{
    RuntimeAllocationScope sceneLoad( RuntimeAllocationPhase::SceneLoad );
    PhysicsFixedList<int, 7> values( "unit.physics-fixed-list.ceilings", ExplicitTestCapacity );
    values.Reserve( 2u );

    CHECK( values.capacity() == 2u );
    CHECK( values.max_capacity() == 7u );
    values.reserve( 2u );
    CHECK( values.capacity() == 2u );
}


namespace
{
struct PhysicsFixedListTrackedValue
{
    explicit PhysicsFixedListTrackedValue( int initialValue = 0 ) : value( initialValue )
    {
    }

    PhysicsFixedListTrackedValue( const PhysicsFixedListTrackedValue& other ) : value( other.value )
    {
        ++copyConstructions;
    }

    PhysicsFixedListTrackedValue( PhysicsFixedListTrackedValue&& other ) noexcept : value( other.value )
    {
        other.value = -1;
        ++moveConstructions;
    }

    PhysicsFixedListTrackedValue& operator=( const PhysicsFixedListTrackedValue& ) = default;
    PhysicsFixedListTrackedValue& operator=( PhysicsFixedListTrackedValue&& ) = default;

    int value = 0;
    static inline int copyConstructions = 0;
    static inline int moveConstructions = 0;
};

struct PhysicsFixedListThrowingValue
{
    explicit PhysicsFixedListThrowingValue( int initialValue = 0 ) : value( initialValue )
    {
        ++liveCount;
    }

    PhysicsFixedListThrowingValue( const PhysicsFixedListThrowingValue& other ) : value( other.value )
    {
        ++copyAttempts;

        if ( throwOnCopyAttempt > 0 && copyAttempts == throwOnCopyAttempt )
        {
            throw std::runtime_error( "PhysicsFixedList copy probe" );
        }

        ++liveCount;
    }

    PhysicsFixedListThrowingValue( PhysicsFixedListThrowingValue&& other ) : value( other.value )
    {
        other.value = -1;
        ++liveCount;
    }

    ~PhysicsFixedListThrowingValue()
    {
        --liveCount;
    }

    int value = 0;
    static inline int liveCount = 0;
    static inline int copyAttempts = 0;
    static inline int throwOnCopyAttempt = 0;
};
} // namespace


TEST_CASE( "PhysicsFixedList: trivial and non-trivial copy move preserve live values" )
{
    RuntimeAllocationScope sceneLoad( RuntimeAllocationPhase::SceneLoad );

    PhysicsFixedList<uint32_t, 8> trivial( "unit.physics-fixed-list.trivial-source", ExplicitTestCapacity );
    trivial.Reserve( 4u );
    trivial.push_back( 4u );
    trivial.push_back( 9u );
    PhysicsFixedList<uint32_t, 8> trivialCopy( trivial );
    PhysicsFixedList<uint32_t, 8> trivialMove( std::move( trivialCopy ) );
    CHECK( trivialMove.size() == 2u );
    CHECK( trivialMove[0] == 4u );
    CHECK( trivialMove[1] == 9u );

    PhysicsFixedListTrackedValue::copyConstructions = 0;
    PhysicsFixedListTrackedValue::moveConstructions = 0;
    PhysicsFixedList<PhysicsFixedListTrackedValue, 8> tracked( "unit.physics-fixed-list.tracked-source",
                                                              ExplicitTestCapacity );
    tracked.Reserve( 3u );
    tracked.push_back( PhysicsFixedListTrackedValue( 17 ) );
    tracked.push_back( PhysicsFixedListTrackedValue( 23 ) );
    PhysicsFixedList<PhysicsFixedListTrackedValue, 8> trackedCopy( tracked );
    PhysicsFixedList<PhysicsFixedListTrackedValue, 8> trackedMove( std::move( trackedCopy ) );

    CHECK( trackedMove.size() == 2u );
    CHECK( trackedMove[0].value == 17 );
    CHECK( trackedMove[1].value == 23 );
    CHECK( PhysicsFixedListTrackedValue::copyConstructions == 2 );
    CHECK( PhysicsFixedListTrackedValue::moveConstructions >= 4 );
}


TEST_CASE( "PhysicsFixedList: failed non-trivial copy and relocation clean every constructed destination" )
{
    RuntimeAllocationScope sceneLoad( RuntimeAllocationPhase::SceneLoad );
    using List = PhysicsFixedList<PhysicsFixedListThrowingValue, 8>;
    PhysicsFixedListThrowingValue::liveCount = 0;
    PhysicsFixedListThrowingValue::copyAttempts = 0;
    PhysicsFixedListThrowingValue::throwOnCopyAttempt = 0;

    {
        List source( "unit.physics-fixed-list.throwing-source", ExplicitTestCapacity );
        source.Reserve( 3u );
        source.push_back( PhysicsFixedListThrowingValue( 17 ) );
        source.push_back( PhysicsFixedListThrowingValue( 23 ) );
        REQUIRE( PhysicsFixedListThrowingValue::liveCount == 2 );

        PhysicsFixedListThrowingValue::copyAttempts = 0;
        PhysicsFixedListThrowingValue::throwOnCopyAttempt = 2;
        bool copyThrew = false;

        try
        {
            List failedCopy( source );
        }
        catch ( const std::runtime_error& )
        {
            copyThrew = true;
        }

        CHECK( copyThrew );
        CHECK( PhysicsFixedListThrowingValue::liveCount == 2 );

        PhysicsFixedListThrowingValue::copyAttempts = 0;
        PhysicsFixedListThrowingValue::throwOnCopyAttempt = 2;
        bool relocationThrew = false;

        try
        {
            source.Reserve( 5u );
        }
        catch ( const std::runtime_error& )
        {
            relocationThrew = true;
        }

        CHECK( relocationThrew );
        CHECK( source.capacity() == 3u );
        CHECK( source.size() == 2u );
        CHECK( source[0].value == 17 );
        CHECK( source[1].value == 23 );
        CHECK( PhysicsFixedListThrowingValue::liveCount == 2 );
        PhysicsFixedListThrowingValue::throwOnCopyAttempt = 0;
    }

    CHECK( PhysicsFixedListThrowingValue::liveCount == 0 );
}


TEST_CASE( "PhysicsFixedList: replay reserve requires an approved outer owner and growth scope" )
{
    constexpr const char* ownerName = "unit.physics-fixed-list.replay-owner";
    const RuntimeReserveOwnerHandle owner =
        RuntimeReserveAllocator::RegisterOwner( MakeReplayOwnerDesc( ownerName, 0, 1024 ) );
    RuntimeReserveGrowthRequest request = MakeGrowthRequest( ownerName, 0, 128 );
    request.elementSizeBytes = 1;
    const RuntimeReserveGrowthResult growth = RuntimeReserveAllocator::RequestGrowth( owner, request );
    REQUIRE( growth.granted );

    PhysicsFixedList<int, 8> values( "unit.physics-fixed-list.replay-target", ExplicitTestCapacity );
    {
        RuntimeAllocationScope replayPhase( RuntimeAllocationPhase::Replay );
        RuntimeReserveOwnerScope ownerScope( owner );
        RuntimeReserveGrowthScope growthScope( owner, RuntimeReservePhase::Replay, growth );
        values.Reserve( 4u );
    }

    CHECK( values.capacity() == 4u );
}


TEST_CASE( "PhysicsFixedList: object size no longer scales with compile-time capacity" )
{
    CHECK( sizeof( PhysicsFixedList<uint8_t, 8192> ) == sizeof( PhysicsFixedList<uint8_t, 8> ) );
    CHECK( sizeof( PhysicsFixedList<uint8_t, 8192> ) <= 64u );
}
