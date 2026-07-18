/*
File: SkullbonezSource/Runtime/Allocation/DevelopmentToolAllocation.cpp
Purpose:
  Registers and reports the two bounded development-tool allocation owners.

Summary:
  ImGui and Tracy receive distinct fixed-registry owner rows. Their scopes only
  change thread-local attribution, so concurrent gameplay allocations remain
  visible to the process-wide phase guard. The caps are diagnostic policy, not
  permission to grow gameplay storage.

Glossary:
  Permanent-development exception: An allocation permission that remains valid
  only while the shared development capability is compiled and never ships.

Invariants:
  - Owner names and reasons have static lifetime because the registry borrows
    their pointers for the process lifetime.
  - ImGui is capped at 64 MiB active bytes and Tracy at 256 MiB active bytes.
  - Neither owner is eligible for replay reserve growth.

Related:
  - SkullbonezSource/Runtime/Allocation/DevelopmentToolAllocation.h
  - SkullbonezSource/Runtime/Allocation/RuntimeAllocationTracker.cpp
  - tools/allocation_policy_allowlist.json
*/
#include "DevelopmentToolAllocation.h"

#include <new>

namespace
{
using SkullbonezCore::Runtime::Allocation::DevelopmentToolAllocationOwner;
using SkullbonezCore::Runtime::Allocation::RuntimeReserveAllocator;
using SkullbonezCore::Runtime::Allocation::RuntimeReserveOwnerHandle;
using SkullbonezCore::Runtime::Allocation::RuntimeReservePhase;
using SkullbonezCore::Runtime::Allocation::RuntimeReserveSubsystem;

constexpr int MEBIBYTE_BYTES = 1024 * 1024;
constexpr int IMGUI_ACTIVE_BYTE_CAP = 64 * MEBIBYTE_BYTES;
constexpr int TRACY_ACTIVE_BYTE_CAP = 256 * MEBIBYTE_BYTES;

RuntimeReserveOwnerHandle ToolOwnerHandle( DevelopmentToolAllocationOwner owner ) noexcept
{
    if ( owner == DevelopmentToolAllocationOwner::DearImGui )
    {
        static const RuntimeReserveOwnerHandle imguiOwner = RuntimeReserveAllocator::RegisterOwner(
            { "DevelopmentTools/DearImGui",
              RuntimeReserveSubsystem::DevelopmentTools,
              RuntimeReservePhase::BackendInit,
              0,
              IMGUI_ACTIVE_BYTE_CAP,
              0,
              false,
              "Dear ImGui process storage is a permanent development-only exception capped at 64 MiB active bytes",
              true } );
        return imguiOwner;
    }

    static const RuntimeReserveOwnerHandle tracyOwner = RuntimeReserveAllocator::RegisterOwner(
        { "DevelopmentTools/Tracy",
          RuntimeReserveSubsystem::DevelopmentTools,
          RuntimeReservePhase::BackendInit,
          0,
          TRACY_ACTIVE_BYTE_CAP,
          0,
          false,
          "Tracy client buffers are a permanent development-only exception capped at 256 MiB active bytes",
          true } );
    return tracyOwner;
}
} // namespace

namespace SkullbonezCore::Runtime::Allocation
{
DevelopmentToolAllocationScope::DevelopmentToolAllocationScope( DevelopmentToolAllocationOwner owner ) noexcept
    : m_ownerScope( ToolOwnerHandle( owner ) )
{
    // Invariant: RuntimeReserveOwnerScope changes only this thread's owner. It
    // deliberately leaves the global phase untouched so other engine threads
    // continue to fail the gameplay allocation guard.
}

bool CopyDevelopmentToolAllocationStats( DevelopmentToolAllocationOwner owner,
                                         DevelopmentToolAllocationStats& outStats ) noexcept
{
    outStats = {};
    RuntimeReserveOwnerStatsView ownerStats;
    if ( !RuntimeReserveAllocator::CopyOwnerStats( ToolOwnerHandle( owner ), ownerStats ) )
    {
        return false;
    }

    outStats.ownerName = ownerStats.ownerName;
    outStats.allocations = ownerStats.allocations;
    outStats.activeBytes = ownerStats.activeBytes;
    outStats.highWaterBytes = ownerStats.highWaterBytes;
    outStats.hardCapBytes = ownerStats.hardCapacity;
    return true;
}

void* AllocateDevelopmentToolMemory( DevelopmentToolAllocationOwner owner, std::size_t size ) noexcept
{
    DevelopmentToolAllocationScope allocationScope( owner );
    return ::operator new( size, std::nothrow );
}

void FreeDevelopmentToolMemory( DevelopmentToolAllocationOwner owner, void* pointer ) noexcept
{
    if ( !pointer )
    {
        return;
    }
    DevelopmentToolAllocationScope allocationScope( owner );
    ::operator delete( pointer );
}
} // namespace SkullbonezCore::Runtime::Allocation
