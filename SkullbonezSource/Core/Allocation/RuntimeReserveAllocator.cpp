/*
File: SkullbonezSource/Core/Allocation/RuntimeReserveAllocator.cpp
Purpose:
  Implements fixed-storage runtime reserve-owner registration and diagnostics.

Summary:
  The allocator starts as the policy ledger: owners register their intended
  capacity, the allocation hook attributes heap traffic to the active owner, and
  replay reserve requests are checked against owner caps. Backing arenas can
  move underneath this ledger without changing the validation contract. In
  development builds, the same ledger bounds two thread-local tool owners.
  Reportable stores also update fixed capacity rows borrowed by diagnostics.

Glossary:
  Policy violation: Unregistered gameplay-phase heap traffic, unregistered
    growth, disallowed growth phase, cap overflow, or exhausted owner-specific
    replay growth-count limits.
  High-water capacity: Largest capacity a registered owner reported after
    startup preallocation or replay-approved growth.
  Last phase/frame: Compact breadcrumbs that identify where an owner last
    allocated or grew without needing heap-backed logs.
  Development tool owner: ImGui or Tracy attribution admitted only by the
    shared compile capability and rejected after its active-byte cap.
  Capacity row: Fixed registry storage carrying one store's live sizing
    telemetry without building a heap-backed report.

Invariants:
  - The registry uses fixed arrays and atomics only; no STL containers or heap
    allocation are allowed here.
  - Owner registration is expected before steady gameplay. Duplicate owner names
    reuse the first handle so repeated scene warmups stay stable.
  - RequestGrowth grants capacity only for replay owners during replay phases.
  - Capacity rows mutate on the owning scene thread; borrowed spans do not
    survive a scene mutation.
  - Tool permission is owner- and thread-specific; it never changes the process
    allocation phase observed by gameplay workers.

Related:
  - SkullbonezSource/Core/Allocation/RuntimeReserveAllocator.h
  - SkullbonezSource/Core/Allocation/RuntimeAllocationTracker.cpp
  - SkullbonezSource/Core/Allocation/DevelopmentToolAllocation.cpp
*/
#include "RuntimeReserveAllocator.h"

#include <algorithm>
#include <atomic>
#include <cstring>

namespace
{
using SkullbonezCore::Core::Allocation::RuntimeReserveCapacityView;
using SkullbonezCore::Core::Allocation::RuntimeReserveGrowthEventView;
using SkullbonezCore::Core::Allocation::RuntimeReserveGrowthRequest;
using SkullbonezCore::Core::Allocation::RuntimeReserveGrowthResult;
using SkullbonezCore::Core::Allocation::RuntimeReserveOwnerDesc;
using SkullbonezCore::Core::Allocation::RuntimeReserveOwnerHandle;
using SkullbonezCore::Core::Allocation::RuntimeReserveOwnerStatsView;
using SkullbonezCore::Core::Allocation::RuntimeReservePhase;
using SkullbonezCore::Core::Allocation::RuntimeReserveSubsystem;

constexpr int MAX_RUNTIME_RESERVE_OWNERS = 160;
constexpr int MAX_RUNTIME_RESERVE_GROWTH_EVENTS = SkullbonezCore::Core::Allocation::RUNTIME_RESERVE_GROWTH_EVENT_HISTORY;
constexpr RuntimeReserveOwnerHandle UNREGISTERED_OWNER = 0u;

struct OwnerCounters
{
    std::atomic<uint64_t> allocations;
    std::atomic<uint64_t> frees;
    std::atomic<uint64_t> allocatedBytes;
    std::atomic<uint64_t> activeBytes;
    std::atomic<uint64_t> highWaterBytes;
    std::atomic<uint64_t> replayGrowths;
    std::atomic<uint64_t> failedGrowths;
    std::atomic<int> currentCapacity;
    std::atomic<int> highWaterCapacity;
    std::atomic<int> lastPhaseIndex;
    std::atomic<int> lastGrowthFrame;
};

struct OwnerRecord
{
    std::atomic<uint32_t> active;
    const char* ownerName;
    RuntimeReserveSubsystem subsystem;
    RuntimeReservePhase initPhase;
    int initialCapacity;
    int hardCapacity;
    int replayGrowthLimit;
    bool allowReplayGrowth;
    const char* capacityReason;
    int elementSizeBytes;
    int capacityRowIndex;
#if defined( SKULLBONEZ_DEVELOPMENT_TOOLS )
    bool allowDevelopmentToolAllocations;
#endif
    OwnerCounters counters;
};

struct GrowthEventRecord
{
    const char* ownerName;
    const char* targetName;
    const char* reason;
    RuntimeReserveSubsystem subsystem;
    RuntimeReservePhase phase;
    uint64_t sequence;
    uint64_t bytes;
    int frameNumber;
    int oldCapacity;
    int requestedCapacity;
    int grantedCapacity;
    int elementSizeBytes;
    int growthCount;
    bool granted;
};

std::atomic<int> s_registeredOwnerCount { 1 };
std::atomic<uint64_t> s_policyViolations { 0 };
std::atomic<uint64_t> s_growthEventCount { 0 };
std::atomic_flag s_growthEventLock = ATOMIC_FLAG_INIT;
OwnerRecord s_owners[MAX_RUNTIME_RESERVE_OWNERS] = {};
RuntimeReserveCapacityView s_capacityRows[MAX_RUNTIME_RESERVE_OWNERS] = {};
std::atomic<int> s_capacityRowCount { 0 };
GrowthEventRecord s_growthEvents[MAX_RUNTIME_RESERVE_GROWTH_EVENTS] = {};
thread_local RuntimeReserveOwnerHandle s_currentOwner = UNREGISTERED_OWNER;
thread_local RuntimeReserveOwnerHandle s_approvedReplayGrowthOwner = UNREGISTERED_OWNER;
thread_local RuntimeReservePhase s_approvedReplayGrowthPhase = RuntimeReservePhase::SteadyGameplay;
thread_local int s_approvedReplayGrowthDepth = 0;

class GrowthEventLock
{
  public:
    GrowthEventLock() noexcept
    {

        while ( s_growthEventLock.test_and_set( std::memory_order_acquire ) )
        {
        }
    }

    ~GrowthEventLock() noexcept
    {
        s_growthEventLock.clear( std::memory_order_release );
    }

    GrowthEventLock( const GrowthEventLock& ) = delete;
    GrowthEventLock& operator=( const GrowthEventLock& ) = delete;
};

bool IsGameplayPhaseIndex( int phaseIndex ) noexcept
{

    // Runtime allocation policy: replay is allowed bounded registered growth,
    // but an unregistered replay heap request is still a validation failure.
    return phaseIndex == 3 || phaseIndex == 4 || phaseIndex == 5 || phaseIndex == 6;
}

const char* SafeOwnerName( const OwnerRecord& owner, int index ) noexcept
{

    if ( index == 0 )
    {
        return "unregistered_runtime_allocation";
    }

    return owner.ownerName ? owner.ownerName : "unnamed_runtime_reserve_owner";
}

const char* SafeCapacityReason( const OwnerRecord& owner ) noexcept
{
    return owner.capacityReason ? owner.capacityReason : "unspecified";
}

const char* SafeTargetName( const RuntimeReserveGrowthRequest& request ) noexcept
{
    return request.targetName && request.targetName[0] != '\0' ? request.targetName : "unnamed_runtime_reserve_target";
}

const char* SafeReason( const char* reason ) noexcept
{
    return reason && reason[0] != '\0' ? reason : "unspecified";
}

bool SameOwnerName( const char* lhs, const char* rhs ) noexcept
{

    if ( lhs == rhs )
    {
        return true;
    }

    if ( !lhs || !rhs )
    {
        return false;
    }

    return std::strcmp( lhs, rhs ) == 0;
}

void UpdateHighWaterU64( std::atomic<uint64_t>& highWater, uint64_t value ) noexcept
{
    uint64_t observed = highWater.load( std::memory_order_relaxed );

    while ( observed < value &&
            !highWater.compare_exchange_weak( observed, value, std::memory_order_relaxed, std::memory_order_relaxed ) )
    {
    }
}

void UpdateHighWaterI32( std::atomic<int>& highWater, int value ) noexcept
{
    int observed = highWater.load( std::memory_order_relaxed );

    while ( observed < value &&
            !highWater.compare_exchange_weak( observed, value, std::memory_order_relaxed, std::memory_order_relaxed ) )
    {
    }
}

void SubtractActiveBytes( std::atomic<uint64_t>& activeBytes, uint64_t size ) noexcept
{
    uint64_t observed = activeBytes.load( std::memory_order_relaxed );

    while ( observed > 0u )
    {
        const uint64_t desired = observed > size ? observed - size : 0u;

        if ( activeBytes.compare_exchange_weak( observed, desired, std::memory_order_relaxed, std::memory_order_relaxed ) )
        {
            return;
        }
    }
}

OwnerRecord& OwnerForHandle( RuntimeReserveOwnerHandle owner ) noexcept
{
    const int index = owner < MAX_RUNTIME_RESERVE_OWNERS ? static_cast<int>( owner ) : 0;

    if ( index == 0 || s_owners[index].active.load( std::memory_order_acquire ) != 0u )
    {
        return s_owners[index];
    }

    return s_owners[0];
}

RuntimeReserveOwnerHandle NormalizeOwnerHandle( RuntimeReserveOwnerHandle owner ) noexcept
{

    if ( owner >= MAX_RUNTIME_RESERVE_OWNERS )
    {
        return UNREGISTERED_OWNER;
    }

    if ( owner != UNREGISTERED_OWNER && s_owners[owner].active.load( std::memory_order_acquire ) == 0u )
    {
        return UNREGISTERED_OWNER;
    }

    return owner;
}

void ResetOwnerCounters( OwnerCounters& counters, int initialCapacity ) noexcept
{
    counters.allocations.store( 0u, std::memory_order_relaxed );
    counters.frees.store( 0u, std::memory_order_relaxed );
    counters.allocatedBytes.store( 0u, std::memory_order_relaxed );
    counters.activeBytes.store( 0u, std::memory_order_relaxed );
    counters.highWaterBytes.store( 0u, std::memory_order_relaxed );
    counters.replayGrowths.store( 0u, std::memory_order_relaxed );
    counters.failedGrowths.store( 0u, std::memory_order_relaxed );
    counters.currentCapacity.store( initialCapacity, std::memory_order_relaxed );
    counters.highWaterCapacity.store( initialCapacity, std::memory_order_relaxed );
    counters.lastPhaseIndex.store( -1, std::memory_order_relaxed );
    counters.lastGrowthFrame.store( -1, std::memory_order_relaxed );
}

bool GrowthRequestMatchesOwner( const OwnerRecord& owner, const RuntimeReserveGrowthRequest& request ) noexcept
{
    return !request.ownerName || SameOwnerName( owner.ownerName, request.ownerName );
}

bool ReplayGrowthCountLimitExhausted( const OwnerRecord& owner, uint64_t oldGrowthCount ) noexcept
{

    // Invariant: the hard capacity is the replay memory bound. A negative
    // growth-count limit leaves exploratory replay tools free to discover
    // larger prediction buffers while still counting and reporting each bump.
    return owner.replayGrowthLimit >= 0 && oldGrowthCount >= static_cast<uint64_t>( owner.replayGrowthLimit );
}

uint64_t GrowthDeltaBytes( int oldCapacity, int grantedCapacity, int elementSizeBytes ) noexcept
{
    const int elementBytes = elementSizeBytes > 0 ? elementSizeBytes : 1;
    const int grownElements = grantedCapacity > oldCapacity ? grantedCapacity - oldCapacity : 0;
    return static_cast<uint64_t>( grownElements ) * static_cast<uint64_t>( elementBytes );
}

void RecordGrowthEvent( const OwnerRecord& owner, int ownerIndex, const RuntimeReserveGrowthRequest& request,
                        const RuntimeReserveGrowthResult& result, const char* reason, uint64_t bytes ) noexcept
{
    GrowthEventLock lock;
    const uint64_t sequence = s_growthEventCount.load( std::memory_order_relaxed ) + 1u;
    s_growthEventCount.store( sequence, std::memory_order_relaxed );

    GrowthEventRecord& event = s_growthEvents[( sequence - 1u ) % MAX_RUNTIME_RESERVE_GROWTH_EVENTS];
    event.ownerName = SafeOwnerName( owner, ownerIndex );
    event.targetName = SafeTargetName( request );
    event.reason = SafeReason( reason );
    event.subsystem = ownerIndex == 0 ? RuntimeReserveSubsystem::Unknown : owner.subsystem;
    event.phase = request.phase;
    event.sequence = sequence;
    event.bytes = bytes;
    event.frameNumber = request.frameNumber;
    event.oldCapacity = request.oldCapacity;
    event.requestedCapacity = request.requestedCapacity;
    event.grantedCapacity = result.grantedCapacity;
    event.elementSizeBytes = request.elementSizeBytes > 0 ? request.elementSizeBytes : 1;
    event.growthCount = result.growthCount;
    event.granted = result.granted;
}

RuntimeReserveGrowthResult DenyGrowth( OwnerRecord& owner, int ownerIndex, const RuntimeReserveGrowthRequest& request,
                                       const char* reason ) noexcept
{
    owner.counters.failedGrowths.fetch_add( 1u, std::memory_order_relaxed );
    s_policyViolations.fetch_add( 1u, std::memory_order_relaxed );
    RuntimeReserveGrowthResult result = {};
    result.granted = false;
    result.grantedCapacity = owner.counters.currentCapacity.load( std::memory_order_relaxed );
    result.growthCount = static_cast<int>( owner.counters.replayGrowths.load( std::memory_order_relaxed ) );
    RecordGrowthEvent( owner, ownerIndex, request, result, reason, 0u );
    std::fprintf( stdout,
                  "[runtime-reserve] growth owner=%s target=%s subsystem=%s phase=%s frame=%d old_capacity=%d "
                  "requested_capacity=%d granted_capacity=%d element_bytes=%d bytes=0 growth_count=%d "
                  "hard_capacity=%d status=denied reason=%s\n",
                  SafeOwnerName( owner, ownerIndex ), SafeTargetName( request ),
                  RuntimeReserveSubsystemName( ownerIndex == 0 ? RuntimeReserveSubsystem::Unknown : owner.subsystem ),
                  RuntimeReservePhaseName( request.phase ), request.frameNumber, request.oldCapacity,
                  request.requestedCapacity, result.grantedCapacity,
                  request.elementSizeBytes > 0 ? request.elementSizeBytes : 1, result.growthCount,
                  ownerIndex == 0 ? 0 : owner.hardCapacity, SafeReason( reason ) );

    return result;
}
} // namespace

namespace SkullbonezCore
{
namespace Core
{
namespace Allocation
{
RuntimeReserveGrowthScope::RuntimeReserveGrowthScope( RuntimeReserveOwnerHandle owner, RuntimeReservePhase phase,
                                                      const RuntimeReserveGrowthResult& result ) noexcept
    : m_previousOwner( s_approvedReplayGrowthOwner ), m_previousPhase( s_approvedReplayGrowthPhase ),
      m_previousDepth( s_approvedReplayGrowthDepth ), m_active( false )
{
    const RuntimeReserveOwnerHandle normalizedOwner = NormalizeOwnerHandle( owner );

    if ( result.granted && normalizedOwner != UNREGISTERED_OWNER && phase == RuntimeReservePhase::Replay )
    {
        s_approvedReplayGrowthOwner = normalizedOwner;
        s_approvedReplayGrowthPhase = phase;
        s_approvedReplayGrowthDepth = m_previousDepth + 1;
        m_active = true;
    }
}

RuntimeReserveGrowthScope::~RuntimeReserveGrowthScope() noexcept
{

    if ( m_active )
    {
        s_approvedReplayGrowthOwner = m_previousOwner;
        s_approvedReplayGrowthPhase = m_previousPhase;
        s_approvedReplayGrowthDepth = m_previousDepth;
    }
}

RuntimeReserveOwnerScope::RuntimeReserveOwnerScope( RuntimeReserveOwnerHandle owner ) noexcept
    : m_previous( RuntimeReserveAllocator::CurrentOwner() )
{
    RuntimeReserveAllocator::SetCurrentOwner( owner );
}

RuntimeReserveOwnerScope::~RuntimeReserveOwnerScope() noexcept
{
    RuntimeReserveAllocator::SetCurrentOwner( m_previous );
}

RuntimeReserveOwnerHandle RuntimeReserveAllocator::RegisterOwner( const RuntimeReserveOwnerDesc& desc ) noexcept
{
    const char* ownerName = desc.ownerName && desc.ownerName[0] != '\0' ? desc.ownerName : "unnamed_runtime_reserve_owner";

    for ( int index = 1; index < s_registeredOwnerCount.load( std::memory_order_acquire ); ++index )
    {
        OwnerRecord& existing = s_owners[index];

        if ( existing.active.load( std::memory_order_acquire ) != 0u && SameOwnerName( existing.ownerName, ownerName ) )
        {
            return static_cast<RuntimeReserveOwnerHandle>( index );
        }
    }

    const int index = s_registeredOwnerCount.fetch_add( 1, std::memory_order_acq_rel );

    if ( index <= 0 || index >= MAX_RUNTIME_RESERVE_OWNERS )
    {
        s_policyViolations.fetch_add( 1u, std::memory_order_relaxed );
        return INVALID_RUNTIME_RESERVE_OWNER;
    }

    OwnerRecord& owner = s_owners[index];
    owner.ownerName = ownerName;
    owner.subsystem = desc.subsystem;
    owner.initPhase = desc.initPhase;
    owner.initialCapacity = desc.initialCapacity;
    owner.hardCapacity = desc.hardCapacity >= desc.initialCapacity ? desc.hardCapacity : desc.initialCapacity;
    owner.replayGrowthLimit = desc.replayGrowthLimit;
    owner.allowReplayGrowth = desc.allowReplayGrowth;
    owner.capacityReason = desc.capacityReason && desc.capacityReason[0] != '\0' ? desc.capacityReason : "unspecified";
    owner.elementSizeBytes = desc.elementSizeBytes > 0 ? desc.elementSizeBytes : 0;
    owner.capacityRowIndex = -1;
#if defined( SKULLBONEZ_DEVELOPMENT_TOOLS )
    owner.allowDevelopmentToolAllocations = desc.allowDevelopmentToolAllocations;
#endif
    ResetOwnerCounters( owner.counters, owner.initialCapacity );

    if ( owner.elementSizeBytes > 0 )
    {
        const int capacityRowIndex = s_capacityRowCount.fetch_add( 1, std::memory_order_acq_rel );

        if ( capacityRowIndex >= 0 && capacityRowIndex < MAX_RUNTIME_RESERVE_OWNERS )
        {
            owner.capacityRowIndex = capacityRowIndex;
            s_capacityRows[capacityRowIndex] = {
                owner.ownerName,
                owner.subsystem,
                owner.capacityReason,
                owner.elementSizeBytes,
                owner.initialCapacity,
                0,
                0,
                static_cast<uint64_t>( owner.initialCapacity ) * static_cast<uint64_t>( owner.elementSizeBytes ),
            };
        }
        else
        {
            s_policyViolations.fetch_add( 1u, std::memory_order_relaxed );
        }
    }

    owner.active.store( 1u, std::memory_order_release );
    return static_cast<RuntimeReserveOwnerHandle>( index );
}

RuntimeReserveGrowthResult RuntimeReserveAllocator::RequestGrowth( RuntimeReserveOwnerHandle ownerHandle,
                                                                   const RuntimeReserveGrowthRequest& request ) noexcept
{
    const RuntimeReserveOwnerHandle ownerIndex = NormalizeOwnerHandle( ownerHandle );
    OwnerRecord& owner = OwnerForHandle( ownerIndex );

    if ( ownerIndex == UNREGISTERED_OWNER || !GrowthRequestMatchesOwner( owner, request ) )
    {
        return DenyGrowth( owner, ownerIndex, request, "owner_mismatch" );
    }

    owner.counters.lastGrowthFrame.store( request.frameNumber, std::memory_order_relaxed );

    const bool initPhaseGrowth = request.phase == owner.initPhase;
    const bool replayGrowth = owner.allowReplayGrowth && request.phase == RuntimeReservePhase::Replay;

    if ( !initPhaseGrowth && !replayGrowth )
    {
        return DenyGrowth( owner, ownerIndex, request, "growth_not_allowed" );
    }

    if ( request.requestedCapacity > owner.hardCapacity || request.requestedCapacity <= request.oldCapacity )
    {
        return DenyGrowth( owner, ownerIndex, request, "capacity_out_of_range" );
    }

    const int elementBytes = request.elementSizeBytes > 0 ? request.elementSizeBytes : 1;
    const uint64_t grownBytes = GrowthDeltaBytes( request.oldCapacity, request.requestedCapacity, elementBytes );

    // Invariant: replay byte-budget owners pass byte capacities with
    // elementSizeBytes=1. Enforce their cap across every live allocation owned
    // by the subsystem, not independently for each vector target.

    if ( owner.subsystem == RuntimeReserveSubsystem::Replay && elementBytes == 1 )
    {
        const uint64_t activeBytes = owner.counters.activeBytes.load( std::memory_order_relaxed );
        const uint64_t hardBytes = static_cast<uint64_t>( owner.hardCapacity );

        if ( activeBytes > hardBytes || grownBytes > hardBytes - activeBytes )
        {
            return DenyGrowth( owner, ownerIndex, request, "owner_byte_budget" );
        }
    }

    const uint64_t oldGrowthCount = owner.counters.replayGrowths.load( std::memory_order_relaxed );

    if ( replayGrowth && ReplayGrowthCountLimitExhausted( owner, oldGrowthCount ) )
    {
        return DenyGrowth( owner, ownerIndex, request, "growth_count_limit" );
    }

    const uint64_t newGrowthCount = replayGrowth
                                        ? owner.counters.replayGrowths.fetch_add( 1u, std::memory_order_relaxed ) + 1u
                                        : oldGrowthCount;

    // Invariant: owner names describe conceptual buffers and may be reused by
    // isolated engines or test fixtures. Capacity telemetry is process-monotonic
    // so a smaller second instance cannot make the registered owner appear to
    // shrink while larger backing remains live.
    UpdateHighWaterI32( owner.counters.currentCapacity, request.requestedCapacity );
    UpdateHighWaterI32( owner.counters.highWaterCapacity, request.requestedCapacity );

    if ( owner.capacityRowIndex >= 0 )
    {
        RuntimeReserveCapacityView& capacityRow = s_capacityRows[owner.capacityRowIndex];

        if ( request.requestedCapacity > capacityRow.currentCapacity )
        {
            capacityRow.currentCapacity = request.requestedCapacity;
            capacityRow.residentBytes = static_cast<uint64_t>( request.requestedCapacity ) *
                                        static_cast<uint64_t>( capacityRow.elementSizeBytes );
        }
    }

    RuntimeReserveGrowthResult result = {};
    result.granted = true;
    result.grantedCapacity = request.requestedCapacity;
    result.growthCount = static_cast<int>( newGrowthCount );
    RecordGrowthEvent( owner, ownerIndex, request, result, "granted", grownBytes );
    std::fprintf( stdout,
                  "[runtime-reserve] growth owner=%s target=%s subsystem=%s phase=%s frame=%d old_capacity=%d "
                  "requested_capacity=%d granted_capacity=%d element_bytes=%d bytes=%llu growth_count=%d "
                  "hard_capacity=%d status=granted\n",
                  SafeOwnerName( owner, ownerIndex ), SafeTargetName( request ),
                  RuntimeReserveSubsystemName( owner.subsystem ), RuntimeReservePhaseName( request.phase ),
                  request.frameNumber, request.oldCapacity, request.requestedCapacity, request.requestedCapacity,
                  elementBytes, static_cast<unsigned long long>( grownBytes ), result.growthCount, owner.hardCapacity );

    return result;
}

RuntimeReserveOwnerHandle RuntimeReserveAllocator::CurrentOwner() noexcept
{
    return s_currentOwner;
}

void RuntimeReserveAllocator::SetCurrentOwner( RuntimeReserveOwnerHandle owner ) noexcept
{
    s_currentOwner = NormalizeOwnerHandle( owner );
}

bool RuntimeReserveAllocator::IsApprovedReplayGrowthAllocation( RuntimeReserveOwnerHandle owner, int phaseIndex ) noexcept
{
    const RuntimeReserveOwnerHandle ownerIndex = NormalizeOwnerHandle( owner );
    return phaseIndex == 6 && ownerIndex != UNREGISTERED_OWNER && ownerIndex == s_approvedReplayGrowthOwner &&
           s_approvedReplayGrowthDepth > 0 && s_approvedReplayGrowthPhase == RuntimeReservePhase::Replay;
}

#if defined( SKULLBONEZ_DEVELOPMENT_TOOLS )
bool RuntimeReserveAllocator::IsApprovedDevelopmentToolAllocation( RuntimeReserveOwnerHandle ownerHandle,
                                                                   int phaseIndex ) noexcept
{
    const RuntimeReserveOwnerHandle ownerIndex = NormalizeOwnerHandle( ownerHandle );

    if ( !IsGameplayPhaseIndex( phaseIndex ) || ownerIndex == UNREGISTERED_OWNER )
    {
        return false;
    }

    const OwnerRecord& owner = OwnerForHandle( ownerIndex );
    const uint64_t activeBytes = owner.counters.activeBytes.load( std::memory_order_relaxed );
    return owner.allowDevelopmentToolAllocations && owner.hardCapacity > 0 &&
           activeBytes <= static_cast<uint64_t>( owner.hardCapacity );
}

bool RuntimeReserveAllocator::TryRecordDevelopmentToolBackingAllocation( RuntimeReserveOwnerHandle ownerHandle,
                                                                         int phaseIndex, uint64_t bytes ) noexcept
{
    const RuntimeReserveOwnerHandle ownerIndex = NormalizeOwnerHandle( ownerHandle );

    if ( ownerIndex == UNREGISTERED_OWNER || bytes == 0u )
    {
        return false;
    }

    OwnerRecord& owner = OwnerForHandle( ownerIndex );

    if ( !owner.allowDevelopmentToolAllocations || owner.hardCapacity <= 0 )
    {
        return false;
    }

    const uint64_t hardBytes = static_cast<uint64_t>( owner.hardCapacity );
    uint64_t activeBefore = owner.counters.activeBytes.load( std::memory_order_relaxed );

    for ( ;; )
    {

        if ( activeBefore > hardBytes || bytes > hardBytes - activeBefore )
        {

            // Lane F precursor: the caller reports the named vendor and map
            // request before terminating. Count the rejected request here so
            // allocation-policy summaries cannot present the cap as healthy.
            owner.counters.failedGrowths.fetch_add( 1u, std::memory_order_relaxed );
            s_policyViolations.fetch_add( 1u, std::memory_order_relaxed );
            return false;
        }

        if ( owner.counters.activeBytes.compare_exchange_weak( activeBefore, activeBefore + bytes, std::memory_order_relaxed,
                                                               std::memory_order_relaxed ) )
        {
            break;
        }
    }

    owner.counters.allocations.fetch_add( 1u, std::memory_order_relaxed );
    owner.counters.allocatedBytes.fetch_add( bytes, std::memory_order_relaxed );
    owner.counters.lastPhaseIndex.store( phaseIndex, std::memory_order_relaxed );
    UpdateHighWaterU64( owner.counters.highWaterBytes, activeBefore + bytes );
    return true;
}
#endif

void RuntimeReserveAllocator::RecordAllocation( RuntimeReserveOwnerHandle ownerHandle, int phaseIndex,
                                                uint64_t bytes ) noexcept
{
    const RuntimeReserveOwnerHandle ownerIndex = NormalizeOwnerHandle( ownerHandle );
    OwnerRecord& owner = OwnerForHandle( ownerIndex );
    owner.counters.allocations.fetch_add( 1u, std::memory_order_relaxed );
    owner.counters.allocatedBytes.fetch_add( bytes, std::memory_order_relaxed );
    owner.counters.lastPhaseIndex.store( phaseIndex, std::memory_order_relaxed );
    const uint64_t activeAfter = owner.counters.activeBytes.fetch_add( bytes, std::memory_order_relaxed ) + bytes;
    UpdateHighWaterU64( owner.counters.highWaterBytes, activeAfter );

    if ( ownerIndex == UNREGISTERED_OWNER && IsGameplayPhaseIndex( phaseIndex ) )
    {
        s_policyViolations.fetch_add( 1u, std::memory_order_relaxed );
        std::fprintf( stdout,
                      "[runtime-reserve] policy_violation owner=unregistered_runtime_allocation phase=%s bytes=%llu "
                      "reason=missing_owner_scope\n",
                      RuntimeReservePhaseName( RuntimeReservePhaseFromAllocationPhaseIndex( phaseIndex ) ),
                      static_cast<unsigned long long>( bytes ) );
    }

#if defined( SKULLBONEZ_DEVELOPMENT_TOOLS )

    if ( ownerIndex != UNREGISTERED_OWNER && owner.allowDevelopmentToolAllocations &&
         ( owner.hardCapacity <= 0 || activeAfter > static_cast<uint64_t>( owner.hardCapacity ) ) )
    {

        // Invariant: the tool exception is bounded by live bytes. Crossing the
        // cap remains a policy violation even though the allocation itself has
        // already succeeded inside the third-party library.
        s_policyViolations.fetch_add( 1u, std::memory_order_relaxed );
        std::fprintf( stdout,
                      "[runtime-reserve] policy_violation owner=%s phase=%s bytes=%llu active_bytes=%llu "
                      "hard_capacity=%d reason=development_tool_byte_cap\n",
                      SafeOwnerName( owner, ownerIndex ),
                      RuntimeReservePhaseName( RuntimeReservePhaseFromAllocationPhaseIndex( phaseIndex ) ),
                      static_cast<unsigned long long>( bytes ), static_cast<unsigned long long>( activeAfter ),
                      owner.hardCapacity );
    }
#endif
}

void RuntimeReserveAllocator::RecordFree( RuntimeReserveOwnerHandle ownerHandle, uint64_t bytes ) noexcept
{
    OwnerRecord& owner = OwnerForHandle( NormalizeOwnerHandle( ownerHandle ) );
    owner.counters.frees.fetch_add( 1u, std::memory_order_relaxed );
    SubtractActiveBytes( owner.counters.activeBytes, bytes );
}

int RuntimeReserveAllocator::CopyRecentGrowthEvents( RuntimeReserveGrowthEventView* outEvents, int maxEvents ) noexcept
{

    if ( !outEvents || maxEvents <= 0 )
    {
        return 0;
    }

    GrowthEventLock lock;
    const uint64_t total = s_growthEventCount.load( std::memory_order_relaxed );
    const uint64_t available = (std::min)( total, static_cast<uint64_t>( MAX_RUNTIME_RESERVE_GROWTH_EVENTS ) );
    const int copyCount = (std::min)( maxEvents, static_cast<int>( available ) );

    for ( int i = 0; i < copyCount; ++i )
    {
        const uint64_t sequence = total - static_cast<uint64_t>( i );
        const GrowthEventRecord& event = s_growthEvents[( sequence - 1u ) % MAX_RUNTIME_RESERVE_GROWTH_EVENTS];
        outEvents[i] = { event.ownerName,
                         event.targetName,
                         RuntimeReserveSubsystemName( event.subsystem ),
                         RuntimeReservePhaseName( event.phase ),
                         event.reason,
                         event.sequence,
                         event.bytes,
                         event.frameNumber,
                         event.oldCapacity,
                         event.requestedCapacity,
                         event.grantedCapacity,
                         event.elementSizeBytes,
                         event.growthCount,
                         event.granted };
    }

    return copyCount;
}

bool RuntimeReserveAllocator::CopyOwnerStats( RuntimeReserveOwnerHandle ownerHandle,
                                              RuntimeReserveOwnerStatsView& outStats ) noexcept
{
    outStats = {};
    const RuntimeReserveOwnerHandle ownerIndex = NormalizeOwnerHandle( ownerHandle );

    if ( ownerIndex == UNREGISTERED_OWNER )
    {
        return false;
    }

    const OwnerRecord& owner = OwnerForHandle( ownerIndex );
    outStats.ownerName = owner.ownerName;
    outStats.subsystem = owner.subsystem;
    outStats.initPhase = owner.initPhase;
    outStats.capacityReason = owner.capacityReason;
    outStats.allocations = owner.counters.allocations.load( std::memory_order_relaxed );
    outStats.activeBytes = owner.counters.activeBytes.load( std::memory_order_relaxed );
    outStats.highWaterBytes = owner.counters.highWaterBytes.load( std::memory_order_relaxed );
    outStats.replayGrowths = owner.counters.replayGrowths.load( std::memory_order_relaxed );
    outStats.failedGrowths = owner.counters.failedGrowths.load( std::memory_order_relaxed );
    outStats.currentCapacity = owner.counters.currentCapacity.load( std::memory_order_relaxed );
    outStats.hardCapacity = owner.hardCapacity;
    outStats.highWaterCapacity = owner.counters.highWaterCapacity.load( std::memory_order_relaxed );
    outStats.lastGrowthFrame = owner.counters.lastGrowthFrame.load( std::memory_order_relaxed );
    outStats.allowReplayGrowth = owner.allowReplayGrowth;
#if defined( SKULLBONEZ_DEVELOPMENT_TOOLS )
    outStats.allowDevelopmentToolAllocations = owner.allowDevelopmentToolAllocations;
#endif
    return true;
}

bool RuntimeReserveAllocator::CopyOwnerStatsByName( const char* ownerName, RuntimeReserveOwnerStatsView& outStats ) noexcept
{
    outStats = {};

    if ( !ownerName || ownerName[0] == '\0' )
    {
        return false;
    }

    const int ownerCount = s_registeredOwnerCount.load( std::memory_order_acquire );

    for ( int index = 1; index < ownerCount && index < MAX_RUNTIME_RESERVE_OWNERS; ++index )
    {
        const OwnerRecord& owner = s_owners[index];

        if ( owner.active.load( std::memory_order_acquire ) != 0u && owner.ownerName &&
             std::strcmp( owner.ownerName, ownerName ) == 0 )
        {
            return CopyOwnerStats( static_cast<RuntimeReserveOwnerHandle>( index ), outStats );
        }
    }

    return false;
}

void RuntimeReserveAllocator::PublishCapacityUsage( RuntimeReserveOwnerHandle ownerHandle, int currentCapacity,
                                                    int liveCount, int sessionHighWater ) noexcept
{
    const RuntimeReserveOwnerHandle ownerIndex = NormalizeOwnerHandle( ownerHandle );

    if ( ownerIndex == UNREGISTERED_OWNER )
    {
        s_policyViolations.fetch_add( 1u, std::memory_order_relaxed );
        return;
    }

    OwnerRecord& owner = OwnerForHandle( ownerIndex );

    if ( owner.capacityRowIndex < 0 )
    {
        return;
    }

    RuntimeReserveCapacityView& capacityRow = s_capacityRows[owner.capacityRowIndex];
    capacityRow.currentCapacity = currentCapacity > 0 ? currentCapacity : 0;
    capacityRow.liveCount = liveCount > 0 ? liveCount : 0;
    const int reportedHighWater = sessionHighWater > capacityRow.liveCount ? sessionHighWater : capacityRow.liveCount;

    if ( reportedHighWater > capacityRow.sessionHighWater )
    {
        capacityRow.sessionHighWater = reportedHighWater;
    }

    capacityRow.residentBytes = static_cast<uint64_t>( capacityRow.currentCapacity ) *
                                static_cast<uint64_t>( capacityRow.elementSizeBytes );
}

std::span<const RuntimeReserveCapacityView> RuntimeReserveAllocator::CapacityRows() noexcept
{
    const int rowCount = s_capacityRowCount.load( std::memory_order_acquire );
    const int boundedCount = rowCount < MAX_RUNTIME_RESERVE_OWNERS ? rowCount : MAX_RUNTIME_RESERVE_OWNERS;
    return std::span<const RuntimeReserveCapacityView>( s_capacityRows, static_cast<std::size_t>( boundedCount ) );
}

uint64_t RuntimeReserveAllocator::GrowthEventCount() noexcept
{
    return s_growthEventCount.load( std::memory_order_relaxed );
}

uint64_t RuntimeReserveAllocator::GrowthEventDroppedCount() noexcept
{
    const uint64_t total = GrowthEventCount();
    return total > static_cast<uint64_t>( MAX_RUNTIME_RESERVE_GROWTH_EVENTS )
               ? total - static_cast<uint64_t>( MAX_RUNTIME_RESERVE_GROWTH_EVENTS )
               : 0u;
}

void RuntimeReserveAllocator::ResetCounters() noexcept
{
    s_policyViolations.store( 0u, std::memory_order_relaxed );
    {
        GrowthEventLock lock;
        s_growthEventCount.store( 0u, std::memory_order_relaxed );

        for ( GrowthEventRecord& event : s_growthEvents )
        {
            event = {};
        }
    }
    ResetOwnerCounters( s_owners[0].counters, 0 );

    for ( int index = 1; index < s_registeredOwnerCount.load( std::memory_order_acquire ); ++index )
    {
        OwnerRecord& owner = s_owners[index];

        if ( owner.active.load( std::memory_order_acquire ) != 0u )
        {
            ResetOwnerCounters( owner.counters, owner.initialCapacity );
        }
    }

    for ( int index = 0; index < s_capacityRowCount.load( std::memory_order_acquire ); ++index )
    {
        RuntimeReserveCapacityView& capacityRow = s_capacityRows[index];
        capacityRow.sessionHighWater = capacityRow.liveCount;
    }
}

void RuntimeReserveAllocator::PrintSummary( FILE* out ) noexcept
{

    if ( !out )
    {
        return;
    }

    std::fprintf( out, "[runtime-reserve] policy_violations=%llu registered_owners=%d\n",
                  static_cast<unsigned long long>( PolicyViolationCount() ),
                  s_registeredOwnerCount.load( std::memory_order_relaxed ) - 1 );

    for ( int index = 0; index < s_registeredOwnerCount.load( std::memory_order_acquire ); ++index )
    {
        const OwnerRecord& owner = s_owners[index];

        if ( index != 0 && owner.active.load( std::memory_order_acquire ) == 0u )
        {
            continue;
        }

        const uint64_t allocations = owner.counters.allocations.load( std::memory_order_relaxed );
        const uint64_t frees = owner.counters.frees.load( std::memory_order_relaxed );
        const uint64_t bytes = owner.counters.allocatedBytes.load( std::memory_order_relaxed );
        const uint64_t activeBytes = owner.counters.activeBytes.load( std::memory_order_relaxed );
        const uint64_t highWaterBytes = owner.counters.highWaterBytes.load( std::memory_order_relaxed );
        const uint64_t replayGrowths = owner.counters.replayGrowths.load( std::memory_order_relaxed );
        const uint64_t failedGrowths = owner.counters.failedGrowths.load( std::memory_order_relaxed );
        const int highWaterCapacity = owner.counters.highWaterCapacity.load( std::memory_order_relaxed );

        if ( allocations == 0u && frees == 0u && bytes == 0u && activeBytes == 0u && highWaterBytes == 0u &&
             replayGrowths == 0u && failedGrowths == 0u && highWaterCapacity == 0 )
        {
            continue;
        }

        const int lastPhase = owner.counters.lastPhaseIndex.load( std::memory_order_relaxed );
        const RuntimeReservePhase phase = RuntimeReservePhaseFromAllocationPhaseIndex( lastPhase );
        std::fprintf( out,
                      "[runtime-reserve] owner=%s subsystem=%s init_phase=%s last_phase=%s allocations=%llu "
                      "frees=%llu bytes=%llu active_bytes=%llu high_water_bytes=%llu capacity=%d "
                      "hard_capacity=%d growth_limit=%d high_water_capacity=%d replay_grows=%llu failed_grows=%llu "
                      "last_growth_frame=%d reason=\"%s\"\n",
                      SafeOwnerName( owner, index ),
                      RuntimeReserveSubsystemName( index == 0 ? RuntimeReserveSubsystem::Unknown : owner.subsystem ),
                      RuntimeReservePhaseName( index == 0 ? RuntimeReservePhase::SteadyGameplay : owner.initPhase ),
                      RuntimeReservePhaseName( phase ), static_cast<unsigned long long>( allocations ),
                      static_cast<unsigned long long>( frees ), static_cast<unsigned long long>( bytes ),
                      static_cast<unsigned long long>( activeBytes ), static_cast<unsigned long long>( highWaterBytes ),
                      owner.counters.currentCapacity.load( std::memory_order_relaxed ), index == 0 ? 0 : owner.hardCapacity,
                      index == 0 ? 0 : owner.replayGrowthLimit, highWaterCapacity,
                      static_cast<unsigned long long>( replayGrowths ), static_cast<unsigned long long>( failedGrowths ),
                      owner.counters.lastGrowthFrame.load( std::memory_order_relaxed ),
                      index == 0 ? "missing RuntimeReserveOwnerScope" : SafeCapacityReason( owner ) );
    }

    RuntimeReserveGrowthEventView recentEvents[32] = {};
    const int recentEventCount = CopyRecentGrowthEvents( recentEvents, 32 );

    if ( recentEventCount > 0 )
    {
        std::fprintf( out, "[runtime-reserve] growth_events total=%llu shown=%d dropped=%llu\n",
                      static_cast<unsigned long long>( GrowthEventCount() ), recentEventCount,
                      static_cast<unsigned long long>( GrowthEventDroppedCount() ) );

        for ( int index = recentEventCount - 1; index >= 0; --index )
        {
            const RuntimeReserveGrowthEventView& event = recentEvents[index];
            std::fprintf( out,
                          "[runtime-reserve] growth_event sequence=%llu owner=%s target=%s phase=%s frame=%d "
                          "bytes=%llu old_capacity=%d requested_capacity=%d granted_capacity=%d "
                          "element_bytes=%d growth_count=%d status=%s reason=%s\n",
                          static_cast<unsigned long long>( event.sequence ), event.ownerName ? event.ownerName : "",
                          event.targetName ? event.targetName : "", event.phaseName ? event.phaseName : "",
                          event.frameNumber, static_cast<unsigned long long>( event.bytes ), event.oldCapacity,
                          event.requestedCapacity, event.grantedCapacity, event.elementSizeBytes, event.growthCount,
                          event.granted ? "granted" : "denied", event.reason ? event.reason : "" );
        }
    }
}

bool RuntimeReserveAllocator::HasPolicyViolations() noexcept
{
    return PolicyViolationCount() > 0u;
}

uint64_t RuntimeReserveAllocator::PolicyViolationCount() noexcept
{
    return s_policyViolations.load( std::memory_order_relaxed );
}

const char* RuntimeReservePhaseName( RuntimeReservePhase phase ) noexcept
{
    return RuntimeAllocationPhaseName( phase );
}

const char* RuntimeReserveSubsystemName( RuntimeReserveSubsystem subsystem ) noexcept
{

    switch ( subsystem )
    {
    case RuntimeReserveSubsystem::Unknown:
        return "unknown";
    case RuntimeReserveSubsystem::Physics:
        return "physics";
    case RuntimeReserveSubsystem::WorkerPool:
        return "worker_pool";
    case RuntimeReserveSubsystem::Renderer:
        return "renderer";
    case RuntimeReserveSubsystem::DX12Telemetry:
        return "dx12_telemetry";
    case RuntimeReserveSubsystem::UI:
        return "ui";
    case RuntimeReserveSubsystem::OwnerRequests:
        return "owner_requests";
    case RuntimeReserveSubsystem::Replay:
        return "replay";
    case RuntimeReserveSubsystem::Diagnostics:
        return "diagnostics";
    case RuntimeReserveSubsystem::AllocationTracker:
        return "allocation_tracker";
#if defined( SKULLBONEZ_DEVELOPMENT_TOOLS )
    case RuntimeReserveSubsystem::DevelopmentTools:
        return "development_tools";
#endif
    default:
        return "unknown";
    }
}

RuntimeReservePhase RuntimeReservePhaseFromAllocationPhaseIndex( int phaseIndex ) noexcept
{

    if ( phaseIndex < 0 || phaseIndex >= static_cast<int>( RuntimeAllocationPhase::Count ) )
    {
        return RuntimeReservePhase::SteadyGameplay;
    }

    return static_cast<RuntimeReservePhase>( phaseIndex );
}
} // namespace Allocation
} // namespace Core
} // namespace SkullbonezCore
