/*
Purpose:
  Declares the runtime memory-owner registry and reserve-growth policy gate.

Invariants:
  - Registry and counter storage is fixed; reporting and hook attribution must
    not allocate.
  - Capacity-row spans borrow allocator storage and are not retained across
    store mutation.
  - A publisher token gives each row one live authority; noncanonical clones
    cannot overwrite or clear its capacity, live count, or session peak.
  - Handle zero is reserved for unregistered allocations so missing owner scopes
    are visible in validation output.
  - Gameplay owners receive growth approval only during their declared init phase.
  - Replay byte-budget owners share one active-allocation cap across all of
    their vector/object targets.
  - Each replay result carries one private owner/phase token and one exact byte
    budget; opening its scope consumes the token and allocations consume bytes.
  - Development tool permission does not exist in Release or Profile-WPO, and
    cannot exempt allocations on another thread.
*/
#pragma once

#include "RuntimeAllocationTracker.h"

#include <cstdint>
#include <cstdio>
#include <span>

namespace SkullbonezCore
{
namespace Core
{
namespace Allocation
{
using RuntimeReserveOwnerHandle = uint16_t;
using RuntimeReserveCapacityPublisherToken = uint32_t;

constexpr RuntimeReserveOwnerHandle INVALID_RUNTIME_RESERVE_OWNER = 0u;
constexpr RuntimeReserveCapacityPublisherToken INVALID_RUNTIME_RESERVE_CAPACITY_PUBLISHER = 0u;
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
    int replayGrowthLimit; // Negative means hard-cap-only; every growth is still counted.
    bool allowReplayGrowth;
    const char* capacityReason;

    // Keep this field unconditional so the descriptor has one ABI across engine
    // libraries compiled with different feature flags. Release/Profile-WPO
    // ignore the permission; development builds still require the exact owner
    // scope before admitting third-party allocations.
    bool allowDevelopmentToolAllocations = false;
    // Nonzero owners express capacities in elements and appear in the fixed
    // capacity-row readout. Zero means the capacity is already measured in bytes.
    int elementSizeBytes = 0;
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
    uint64_t allocationBytes = 0u; // Zero derives one new backing allocation from requested capacity.
};

struct RuntimeReserveGrowthResult
{
    bool granted = false;
    int grantedCapacity = 0;
    int growthCount = 0;

    RuntimeReserveGrowthResult() noexcept = default;
    ~RuntimeReserveGrowthResult() noexcept;
    RuntimeReserveGrowthResult( RuntimeReserveGrowthResult&& other ) noexcept;
    RuntimeReserveGrowthResult& operator=( RuntimeReserveGrowthResult&& other ) noexcept;
    RuntimeReserveGrowthResult( const RuntimeReserveGrowthResult& ) = delete;
    RuntimeReserveGrowthResult& operator=( const RuntimeReserveGrowthResult& ) = delete;

  private:
    friend class RuntimeReserveAllocator;
    friend class RuntimeReserveGrowthScope;

    RuntimeReserveOwnerHandle m_grantOwner = INVALID_RUNTIME_RESERVE_OWNER;
    RuntimeReservePhase m_grantPhase = RuntimeReservePhase::SteadyGameplay;
    uint64_t m_grantId = 0u;
    uint64_t m_allocationBytes = 0u;
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
    uint64_t activeBytes;             // Currently live allocation bytes attributed to this owner.
    uint64_t highWaterBytes;          // Largest transient active-byte total since counters reset.
    uint64_t pendingReplayGrantBytes; // Issued replay bytes not yet allocated or released.
    uint64_t replayGrowths;
    uint64_t failedGrowths;
    int currentCapacity;
    int hardCapacity;
    int highWaterCapacity; // Owner capacity units; byte-budget owners use bytes.
    int lastGrowthFrame;
    bool allowReplayGrowth;
#if defined( SKULLBONEZ_DEVELOPMENT_TOOLS )
    bool allowDevelopmentToolAllocations;
#endif
};

struct RuntimeReserveCapacityView
{
    const char* ownerName;
    RuntimeReserveSubsystem subsystem;
    const char* capacityReason;
    int elementSizeBytes;
    int currentCapacity;
    int liveCount;
    int sessionHighWater;
    uint64_t residentBytes;
};

class RuntimeReserveGrowthScope
{
  public:
    RuntimeReserveGrowthScope( RuntimeReserveOwnerHandle owner, RuntimeReservePhase phase,
                               RuntimeReserveGrowthResult& result ) noexcept;
    ~RuntimeReserveGrowthScope() noexcept;

    RuntimeReserveGrowthScope( const RuntimeReserveGrowthScope& ) = delete;
    RuntimeReserveGrowthScope& operator=( const RuntimeReserveGrowthScope& ) = delete;

  private:
    RuntimeReserveOwnerHandle m_previousOwner;
    RuntimeReservePhase m_previousPhase;
    int m_previousDepth;
    uint64_t m_previousGrantId;
    uint64_t m_previousRemainingBytes;
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

// Invariant: one lexical transaction publishes a coherent allocation phase,
// reserve owner, and one-use growth grant. Member order is activation order so
// reverse destruction closes the grant before restoring the owner and phase.
class RuntimeReserveAllocationScope
{
  public:
    RuntimeReserveAllocationScope( RuntimeReserveOwnerHandle owner, RuntimeReservePhase phase,
                                   RuntimeReserveGrowthResult& result ) noexcept;
    ~RuntimeReserveAllocationScope() noexcept = default;

    RuntimeReserveAllocationScope( const RuntimeReserveAllocationScope& ) = delete;
    RuntimeReserveAllocationScope& operator=( const RuntimeReserveAllocationScope& ) = delete;
    RuntimeReserveAllocationScope( RuntimeReserveAllocationScope&& ) = delete;
    RuntimeReserveAllocationScope& operator=( RuntimeReserveAllocationScope&& ) = delete;

  private:
    RuntimeAllocationScope m_allocationScope;
    RuntimeReserveOwnerScope m_ownerScope;
    RuntimeReserveGrowthScope m_growthScope;
};

class RuntimeReserveAllocator
{
  public:
    static RuntimeReserveOwnerHandle RegisterOwner( const RuntimeReserveOwnerDesc& desc ) noexcept;
    static RuntimeReserveGrowthResult RequestGrowth( RuntimeReserveOwnerHandle owner,
                                                     const RuntimeReserveGrowthRequest& request ) noexcept;

    static RuntimeReserveOwnerHandle CurrentOwner() noexcept;
    static void SetCurrentOwner( RuntimeReserveOwnerHandle owner ) noexcept;

    // IsApproved is a non-consuming preflight for fixed containers. The global
    // allocation hook must use TryConsume with its exact requested byte count.
    static bool IsApprovedReplayGrowthAllocation( RuntimeReserveOwnerHandle owner, int phaseIndex ) noexcept;
    static bool TryConsumeApprovedReplayGrowthAllocation( RuntimeReserveOwnerHandle owner, int phaseIndex, uint64_t bytes,
                                                          uint64_t* outAccountingGeneration = nullptr ) noexcept;
#if defined( SKULLBONEZ_DEVELOPMENT_TOOLS )
    static bool IsApprovedDevelopmentToolAllocation( RuntimeReserveOwnerHandle owner, int phaseIndex ) noexcept;

    // Reserves one vendor-owned backing range before an allocator maps it.
    // Unlike RecordAllocation(), this can reject the request without first
    // crossing the registered live-byte cap.
    static bool TryRecordDevelopmentToolBackingAllocation( RuntimeReserveOwnerHandle owner, int phaseIndex, uint64_t bytes,
                                                           uint64_t* outAccountingGeneration = nullptr ) noexcept;
#endif

    static uint64_t RecordAllocation( RuntimeReserveOwnerHandle owner, int phaseIndex, uint64_t bytes ) noexcept;
    static void RecordFree( RuntimeReserveOwnerHandle owner, uint64_t bytes, uint64_t accountingGeneration = 0u ) noexcept;
    static int CopyRecentGrowthEvents( RuntimeReserveGrowthEventView* outEvents, int maxEvents ) noexcept;

    // Copies one fixed-registry owner row without allocating. Name lookup lets
    // domain diagnostics report owners without retaining allocator handles.
    static bool CopyOwnerStats( RuntimeReserveOwnerHandle owner, RuntimeReserveOwnerStatsView& outStats ) noexcept;
    static bool CopyOwnerStatsByName( const char* ownerName, RuntimeReserveOwnerStatsView& outStats ) noexcept;
    static RuntimeReserveCapacityPublisherToken ClaimCapacityPublisher( RuntimeReserveOwnerHandle owner ) noexcept;
    static void ReleaseCapacityPublisher( RuntimeReserveOwnerHandle owner, RuntimeReserveCapacityPublisherToken publisher,
                                          int sessionHighWater ) noexcept;
    static void PublishCapacityUsage( RuntimeReserveOwnerHandle owner, RuntimeReserveCapacityPublisherToken publisher,
                                      int currentCapacity, int liveCount, int sessionHighWater ) noexcept;
    static std::span<const RuntimeReserveCapacityView> CapacityRows() noexcept;
    static uint64_t CapacitySessionGeneration() noexcept;
    static void BeginCapacitySession() noexcept;
    static void PrintCapacityRows( FILE* out, const char* sceneName, const char* status ) noexcept;
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
} // namespace Core
} // namespace SkullbonezCore
