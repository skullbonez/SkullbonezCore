/*
File: SkullbonezSource/Runtime/Allocation/RuntimeReserveAllocator.h
Purpose:
  Declares the runtime memory-owner registry and reserve-growth policy gate.

Mental model:
  Runtime allocation policy is enforced by named owners, not by one anonymous
  heap. Gameplay owners register fixed capacity so diagnostics can report their
  budget and high-water use. Replay owners are the only owners allowed to ask
  for bounded runtime reserve bumps, and every bump is counted.

Glossary:
  Reserve owner: A runtime subsystem buffer with a named capacity contract.
  Owner scope: A cheap thread-local label used by the global allocation hook to
    attribute generic C++ heap traffic to the owner currently doing work.
  Replay growth: A rare, bounded capacity increase requested during replay.

Invariants:
  - Registry and counter storage is fixed; reporting and hook attribution must
    not allocate.
  - Handle zero is reserved for unregistered allocations so missing owner scopes
    are visible in validation output.
  - Gameplay owners never receive growth approval from this allocator.

Related:
  - SkullbonezSource/Runtime/Allocation/RuntimeAllocationTracker.h
  - Agentic/Plans/Done/runtime-static-allocation-policy-plan.md
*/
#pragma once

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

enum class RuntimeReservePhase
{
    Startup = 0,
    SceneLoad,
    BackendInit,
    SteadyGameplay,
    Physics,
    Render,
    Replay,
    Capture,
    Diagnostics,
    Shutdown
};

enum class RuntimeReserveSubsystem
{
    Unknown = 0,
    Physics,
    WorkerPool,
    Renderer,
    DX12Telemetry,
    UI,
    RuntimeCommands,
    Replay,
    Diagnostics,
    AllocationTracker
};

struct RuntimeReserveOwnerDesc
{
    const char* ownerName;
    RuntimeReserveSubsystem subsystem;
    RuntimeReservePhase initPhase;
    int initialCapacity;
    int hardCapacity;
    int replayGrowthLimit;
    bool allowReplayGrowth;
    const char* capacityReason;
};

struct RuntimeReserveGrowthRequest
{
    const char* ownerName;
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

    static void RecordAllocation( RuntimeReserveOwnerHandle owner, int phaseIndex, uint64_t bytes ) noexcept;
    static void RecordFree( RuntimeReserveOwnerHandle owner, uint64_t bytes ) noexcept;
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
