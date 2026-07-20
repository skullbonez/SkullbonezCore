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
//
// Glossary:
//   Reserve owner: Named runtime storage owner with an initial capacity and hard
//     capacity budget.
//   Replay growth: Bounded capacity increase allowed only while replay tools are
//     doing replay-phase work.
//   Development tool owner: Thread-local ImGui or Tracy attribution that admits
//     bounded vendor storage without changing the process gameplay phase.
//   Growth event: Fixed-ring diagnostic row recording one grant or denial.
//   Lifecycle phase: Always-on process label used by allocation and upload
//     policies even when allocation counting is disabled.
//   Allocation guard: Process-wide measurement mode that attributes global
//     heap requests to lifecycle phases and flags steady-gameplay violations.
//
// Invariants:
//   - Gameplay-phase owners never receive replay growth approval.
//   - Denied growth increments policy violations and still records an event.
//   - ResetCounters() clears counters/events without unregistering owners.
//   - RuntimeAllocationScope publishes/restores phase independently of guard mode.
//   - Development tool scopes do not mask an ordinary gameplay allocation.
//   - Tracker cases restore the process-wide guard to Off before returning.
//
// Related:
//   - SkullbonezSource/Core/Allocation/RuntimeReserveAllocator.h
//   - SkullbonezSource/Core/Allocation/RuntimeReserveAllocator.cpp
//   - Agentic/Reports/behavioral_test_depth_closure_20260711.md
//

#include "../ThirdPtySource/doctest/doctest.h"

#include "../SkullbonezSource/Core/Allocation/RuntimeReserveAllocator.h"
#include "../SkullbonezSource/Core/Allocation/RuntimeAllocationTracker.h"
#if defined( SKULLBONEZ_DEVELOPMENT_TOOLS )
#include "../SkullbonezSource/Core/Allocation/DevelopmentToolAllocation.h"
#endif

#include <array>
#include <cstddef>
#include <cstdio>
#include <new>
#include <string>

using SkullbonezCore::Runtime::Allocation::GetRuntimeAllocationGuardMode;
using SkullbonezCore::Runtime::Allocation::GetRuntimeAllocationPhase;
using SkullbonezCore::Runtime::Allocation::INVALID_RUNTIME_RESERVE_OWNER;
using SkullbonezCore::Runtime::Allocation::PrintRuntimeAllocationSummary;
using SkullbonezCore::Runtime::Allocation::ResetRuntimeAllocationCounters;
using SkullbonezCore::Runtime::Allocation::RUNTIME_RESERVE_REPLAY_GROWTH_LIMIT_UNBOUNDED;
using SkullbonezCore::Runtime::Allocation::RuntimeAllocationGuardEnabled;
using SkullbonezCore::Runtime::Allocation::RuntimeAllocationGuardHasGameplayViolations;
using SkullbonezCore::Runtime::Allocation::RuntimeAllocationGuardMode;
using SkullbonezCore::Runtime::Allocation::RuntimeAllocationGuardModeName;
using SkullbonezCore::Runtime::Allocation::RuntimeAllocationGuardViolationCount;
using SkullbonezCore::Runtime::Allocation::RuntimeAllocationPhase;
using SkullbonezCore::Runtime::Allocation::RuntimeAllocationPhaseName;
using SkullbonezCore::Runtime::Allocation::RuntimeAllocationScope;
using SkullbonezCore::Runtime::Allocation::RuntimeReserveAllocator;
using SkullbonezCore::Runtime::Allocation::RuntimeReserveGrowthEventView;
using SkullbonezCore::Runtime::Allocation::RuntimeReserveGrowthRequest;
using SkullbonezCore::Runtime::Allocation::RuntimeReserveGrowthResult;
using SkullbonezCore::Runtime::Allocation::RuntimeReserveGrowthScope;
using SkullbonezCore::Runtime::Allocation::RuntimeReserveOwnerDesc;
using SkullbonezCore::Runtime::Allocation::RuntimeReserveOwnerHandle;
using SkullbonezCore::Runtime::Allocation::RuntimeReserveOwnerStatsView;
using SkullbonezCore::Runtime::Allocation::RuntimeReservePhase;
using SkullbonezCore::Runtime::Allocation::RuntimeReserveSubsystem;
using SkullbonezCore::Runtime::Allocation::SetRuntimeAllocationGuardMode;
using SkullbonezCore::Runtime::Allocation::SetRuntimeAllocationPhase;
#if defined( SKULLBONEZ_DEVELOPMENT_TOOLS )
using SkullbonezCore::Runtime::Allocation::CopyDevelopmentToolAllocationStats;
using SkullbonezCore::Runtime::Allocation::DevelopmentToolAllocationOwner;
using SkullbonezCore::Runtime::Allocation::DevelopmentToolAllocationScope;
using SkullbonezCore::Runtime::Allocation::DevelopmentToolAllocationStats;
using SkullbonezCore::Runtime::Allocation::ReleaseDevelopmentToolBackingMemory;
using SkullbonezCore::Runtime::Allocation::TryAccountDevelopmentToolBackingMemory;
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
