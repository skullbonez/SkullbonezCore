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
//
// Invariants:
//   - Gameplay-phase owners never receive replay growth approval.
//   - Denied growth increments policy violations and still records an event.
//   - ResetCounters() clears counters/events without unregistering owners.
//
// Related:
//   - SkullbonezSource/Runtime/Allocation/RuntimeReserveAllocator.h
//   - SkullbonezSource/Runtime/Allocation/RuntimeReserveAllocator.cpp
//   - Agentic/Plans/TODO/behavioral-test-depth.md
//

#include "../ThirdPtySource/doctest/doctest.h"

#include "../SkullbonezSource/Runtime/Allocation/RuntimeReserveAllocator.h"

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
using SkullbonezCore::Runtime::Allocation::RuntimeReservePhase;
using SkullbonezCore::Runtime::Allocation::RuntimeReserveSubsystem;

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
    const RuntimeReserveOwnerHandle owner =
        RuntimeReserveAllocator::RegisterOwner( MakeReplayOwnerDesc( ownerName ) );
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
    const RuntimeReserveOwnerHandle owner =
        RuntimeReserveAllocator::RegisterOwner( MakeReplayOwnerDesc( ownerName ) );
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
