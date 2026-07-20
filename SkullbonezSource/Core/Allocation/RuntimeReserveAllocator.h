/*
File: SkullbonezSource/Core/Allocation/RuntimeReserveAllocator.h
Purpose:
  Declares the runtime memory-owner registry and reserve-growth policy gate.

Summary:
  Runtime allocation policy is enforced by named owners, not by one anonymous
  heap. Gameplay owners register fixed capacity so diagnostics can report their
  budget and high-water use. Replay owners are the only owners allowed to ask
  for hard-cap-bounded runtime reserve bumps, and every bump is counted.
  Development builds additionally admit explicitly scoped, hard-byte-capped
  ImGui and Tracy owners without changing the gameplay phase.

Glossary:
  Reserve owner: A runtime subsystem buffer with a named capacity contract.
  Owner scope: A cheap thread-local label used by the global allocation hook to
    attribute generic C++ heap traffic to the owner currently doing work.
  Replay growth: A rare capacity increase requested during replay and bounded
    by its registered hard capacity.
  Development tool permission: Compile-time-only owner metadata that lets one
    calling thread allocate for ImGui or Tracy up to a hard active-byte cap.

Invariants:
  - Registry and counter storage is fixed; reporting and hook attribution must
    not allocate.
  - Handle zero is reserved for unregistered allocations so missing owner scopes
    are visible in validation output.
  - Gameplay owners never receive growth approval from this allocator.
  - Replay byte-budget owners share one active-allocation cap across all of
    their vector/object targets.
  - Development tool permission does not exist in Release or Profile-WPO, and
    cannot exempt allocations on another thread.

Related:
  - SkullbonezSource/Core/Allocation/RuntimeAllocationTracker.h
  - SkullbonezSource/Core/Allocation/DevelopmentToolAllocation.h
  - AGENTS.md (Runtime Static Allocation Policy)
*/
#pragma once

#include "RuntimeAllocationTracker.h"

#include <cstdint>
#include <cstdio>

namespace SkullbonezCore
{
namespace Runtime
{
namespace Allocation
{
using RuntimeReserveOwnerHandle = uint16_t;

constexpr RuntimeReserveOwnerHandle INVALID_RUNTIME_RESERVE_OWNER = 0u;
constexpr int RUNTIME_RESERVE_REPLAY_GROWTH_LIMIT_UNBOUNDED = -1;
constexpr int RUNTIME_RESERVE_GROWTH_EVENT_HISTORY = 256;

// Concept: reserve growth approval uses the same lifecycle phases as the global
// allocation guard. Keeping one phase type prevents the reserve ledger from
// drifting away from the runtime hook that ultimately records the allocation.
using RuntimeReservePhase = RuntimeAllocationPhase;

enum class RuntimeReserveSubsystem
{
    Unknown = 0,
    Physics,
    WorkerPool,
    Renderer,
    DX12Telemetry,
    UI,
    OwnerRequests,
    Replay,
    Diagnostics,
    AllocationTracker,
#if defined( SKULLBONEZ_DEVELOPMENT_TOOLS )
    DevelopmentTools,
#endif
};

struct RuntimeReserveOwnerDesc
{
    const char* ownerName;
    RuntimeReserveSubsystem subsystem;
    RuntimeReservePhase initPhase;
    int initialCapacity;
    int hardCapacity;
    int replayGrowthLimit;   // Negative means hard-cap-only; every growth is still counted.
    bool allowReplayGrowth;
    const char* capacityReason;
#if defined( SKULLBONEZ_DEVELOPMENT_TOOLS )
    // Permanent-development permission. RuntimeAllocationTracker still requires
    // the calling thread to enter this exact registered owner scope.
    bool allowDevelopmentToolAllocations = false;
#endif
};

struct RuntimeReserveGrowthRequest
{
    const char* ownerName;
    const char* targetName;
    RuntimeReservePhase phase;
    int frameNumber;
    int oldCapacity;
    int requestedCapacity;
    int elementSizeBytes;
};

struct RuntimeReserveGrowthResult
{
    bool granted;
    int grantedCapacity;
    int growthCount;
};

struct RuntimeReserveGrowthEventView
{
    const char* ownerName;
    const char* targetName;
    const char* subsystemName;
    const char* phaseName;
    const char* reason;
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

struct RuntimeReserveOwnerStatsView
{
    const char* ownerName;
    RuntimeReserveSubsystem subsystem;
    RuntimeReservePhase initPhase;
    const char* capacityReason;
    uint64_t allocations;
    uint64_t activeBytes;    // Currently live allocation bytes attributed to this owner.
    uint64_t highWaterBytes; // Largest transient active-byte total since counters reset.
    uint64_t replayGrowths;
    uint64_t failedGrowths;
    int currentCapacity;
    int hardCapacity;
    int highWaterCapacity;   // Owner capacity units; byte-budget owners use bytes.
    int lastGrowthFrame;
    bool allowReplayGrowth;
#if defined( SKULLBONEZ_DEVELOPMENT_TOOLS )
    bool allowDevelopmentToolAllocations;
#endif
};

class RuntimeReserveGrowthScope
{
  public:
    RuntimeReserveGrowthScope( RuntimeReserveOwnerHandle owner,
                               RuntimeReservePhase phase,
                               const RuntimeReserveGrowthResult& result ) noexcept;
    ~RuntimeReserveGrowthScope() noexcept;

    RuntimeReserveGrowthScope( const RuntimeReserveGrowthScope& ) = delete;
    RuntimeReserveGrowthScope& operator=( const RuntimeReserveGrowthScope& ) = delete;

  private:
    RuntimeReserveOwnerHandle m_previousOwner;
    RuntimeReservePhase m_previousPhase;
    int m_previousDepth;
    bool m_active;
};

class RuntimeReserveOwnerScope
{
  public:
    explicit RuntimeReserveOwnerScope( RuntimeReserveOwnerHandle owner ) noexcept;
    ~RuntimeReserveOwnerScope() noexcept;

    RuntimeReserveOwnerScope( const RuntimeReserveOwnerScope& ) = delete;
    RuntimeReserveOwnerScope& operator=( const RuntimeReserveOwnerScope& ) = delete;

  private:
    RuntimeReserveOwnerHandle m_previous;
};

class RuntimeReserveAllocator
{
  public:
    static RuntimeReserveOwnerHandle RegisterOwner( const RuntimeReserveOwnerDesc& desc ) noexcept;
    static RuntimeReserveGrowthResult RequestGrowth( RuntimeReserveOwnerHandle owner,
                                                     const RuntimeReserveGrowthRequest& request ) noexcept;

    static RuntimeReserveOwnerHandle CurrentOwner() noexcept;
    static void SetCurrentOwner( RuntimeReserveOwnerHandle owner ) noexcept;
    static bool IsApprovedReplayGrowthAllocation( RuntimeReserveOwnerHandle owner, int phaseIndex ) noexcept;
#if defined( SKULLBONEZ_DEVELOPMENT_TOOLS )
    static bool IsApprovedDevelopmentToolAllocation( RuntimeReserveOwnerHandle owner, int phaseIndex ) noexcept;
    // Reserves one vendor-owned backing range before an allocator maps it.
    // Unlike RecordAllocation(), this can reject the request without first
    // crossing the registered live-byte cap.
    static bool TryRecordDevelopmentToolBackingAllocation( RuntimeReserveOwnerHandle owner,
                                                           int phaseIndex,
                                                           uint64_t bytes ) noexcept;
#endif

    static void RecordAllocation( RuntimeReserveOwnerHandle owner, int phaseIndex, uint64_t bytes ) noexcept;
    static void RecordFree( RuntimeReserveOwnerHandle owner, uint64_t bytes ) noexcept;
    static int CopyRecentGrowthEvents( RuntimeReserveGrowthEventView* outEvents, int maxEvents ) noexcept;
    // Copies one fixed-registry owner row without allocating. Name lookup lets
    // domain diagnostics report owners without retaining allocator handles.
    static bool CopyOwnerStats( RuntimeReserveOwnerHandle owner, RuntimeReserveOwnerStatsView& outStats ) noexcept;
    static bool CopyOwnerStatsByName( const char* ownerName, RuntimeReserveOwnerStatsView& outStats ) noexcept;
    static uint64_t GrowthEventCount() noexcept;
    static uint64_t GrowthEventDroppedCount() noexcept;
    static void ResetCounters() noexcept;
    static void PrintSummary( FILE* out ) noexcept;

    static bool HasPolicyViolations() noexcept;
    static uint64_t PolicyViolationCount() noexcept;
};

const char* RuntimeReservePhaseName( RuntimeReservePhase phase ) noexcept;
const char* RuntimeReserveSubsystemName( RuntimeReserveSubsystem subsystem ) noexcept;
RuntimeReservePhase RuntimeReservePhaseFromAllocationPhaseIndex( int phaseIndex ) noexcept;
} // namespace Allocation
} // namespace Runtime
} // namespace SkullbonezCore
