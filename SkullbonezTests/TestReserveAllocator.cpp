//
// File: SkullbonezTests/TestReserveAllocator.cpp
// Purpose:
//   Lock focused RuntimeReserveAllocator policy contracts.
//
// Mental model:
//   The allocator is a fixed-storage policy ledger. Owners register their
//   initial and hard capacity, replay owners may request bounded growth during
//   replay, and every grant or denial becomes a compact diagnostic event.
//
// Glossary:
//   Reserve owner: Named runtime storage owner with an initial capacity and hard
//     capacity budget.
//   Replay growth: Bounded capacity increase allowed only while replay tools are
//     doing replay-phase work.
//   Growth event: Fixed-ring diagnostic row recording one grant or denial.
//   Lifecycle phase: Always-on process label used by allocation and upload
//     policies even when allocation counting is disabled.
//
// Invariants:
//   - Gameplay-phase owners never receive replay growth approval.
//   - Denied growth increments policy violations and still records an event.
//   - ResetCounters() clears counters/events without unregistering owners.
//   - RuntimeAllocationScope publishes/restores phase independently of guard mode.
//
// Related:
//   - SkullbonezSource/Runtime/Allocation/RuntimeReserveAllocator.h
//   - SkullbonezSource/Runtime/Allocation/RuntimeReserveAllocator.cpp
//   - Agentic/Reports/behavioral_test_depth_closure_20260711.md
//

#include "../ThirdPtySource/doctest/doctest.h"

#include "../SkullbonezSource/Runtime/Allocation/RuntimeReserveAllocator.h"
#include "../SkullbonezSource/Runtime/Allocation/RuntimeAllocationTracker.h"

#include <string>

using SkullbonezCore::Runtime::Allocation::INVALID_RUNTIME_RESERVE_OWNER;
using SkullbonezCore::Runtime::Allocation::RUNTIME_RESERVE_REPLAY_GROWTH_LIMIT_UNBOUNDED;
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
using SkullbonezCore::Runtime::Allocation::GetRuntimeAllocationPhase;
using SkullbonezCore::Runtime::Allocation::RuntimeAllocationGuardMode;
using SkullbonezCore::Runtime::Allocation::RuntimeAllocationPhase;
using SkullbonezCore::Runtime::Allocation::RuntimeAllocationScope;
using SkullbonezCore::Runtime::Allocation::SetRuntimeAllocationGuardMode;
using SkullbonezCore::Runtime::Allocation::SetRuntimeAllocationPhase;

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
} // namespace


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
