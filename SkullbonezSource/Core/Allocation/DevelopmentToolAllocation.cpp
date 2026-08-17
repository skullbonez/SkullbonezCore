/*
File: SkullbonezSource/Core/Allocation/DevelopmentToolAllocation.cpp
Purpose:
  Registers and reports the two bounded development-tool allocation owners.

Summary:
  ImGui and Tracy receive distinct fixed-registry owner rows. Their scopes only
  change thread-local attribution, so concurrent gameplay allocations remain
  visible to the process-wide phase guard. Vendor page allocators also reserve
  their real backing ranges in the same ledger before mapping them. Heavy Tracy
  heap events are emitted here so the Core allocation hook has no upward
  dependency on Runtime's Tracy lifecycle owner.

Glossary:
  Permanent-development exception: An allocation permission that remains valid
  only while the shared development capability is compiled and never ships.

Invariants:
  - Owner names and reasons have static lifetime because the registry borrows
    their pointers for the process lifetime.
  - ImGui is capped at 64 MiB active bytes and Tracy at 512 MiB active bytes.
  - Neither owner is eligible for replay reserve growth.
  - Allocation/free events pair within one Tracy viewer connection generation.

Related:
  - SkullbonezSource/Core/Allocation/DevelopmentToolAllocation.h
  - SkullbonezSource/Core/Allocation/RuntimeAllocationTracker.cpp
  - tools/allocation_policy_allowlist.json
*/
#include "DevelopmentToolAllocation.h"

#if defined( TRACY_ENABLE )
#include <tracy/Tracy.hpp>
#include <tracy/TracyC.h>
#endif

#include <atomic>
#include <new>

namespace
{
using SkullbonezCore::Core::Allocation::DevelopmentToolAllocationOwner;
using SkullbonezCore::Core::Allocation::RuntimeReserveAllocator;
using SkullbonezCore::Core::Allocation::RuntimeReserveOwnerHandle;
using SkullbonezCore::Core::Allocation::RuntimeReservePhase;
using SkullbonezCore::Core::Allocation::RuntimeReserveSubsystem;

constexpr int MEBIBYTE_BYTES = 1024 * 1024;
constexpr int IMGUI_ACTIVE_BYTE_CAP = 64 * MEBIBYTE_BYTES;
#if defined( TRACY_ENABLE )
constexpr int TRACY_HEAP_CALLSTACK_DEPTH = 16;
constexpr const char* TRACY_RUNTIME_HEAP_NAME = "Skore Runtime C++ Heap";
#endif

// Why: standard validation captures peak near 400 MiB of Tracy-owned process
// backing. A 512 MiB ceiling leaves bounded headroom; the smaller nominal
// estimate was never an enforceable production cap.
constexpr int TRACY_ACTIVE_BYTE_CAP = 512 * MEBIBYTE_BYTES;
std::atomic<bool> g_tracyAllocationTracingEnabled { false };

RuntimeReserveOwnerHandle ToolOwnerHandle( DevelopmentToolAllocationOwner owner ) noexcept
{
    if ( owner == DevelopmentToolAllocationOwner::DearImGui )
    {
        static const RuntimeReserveOwnerHandle imguiOwner = RuntimeReserveAllocator::RegisterOwner( { "DevelopmentTools/DearImGui", RuntimeReserveSubsystem::DevelopmentTools, RuntimeReservePhase::BackendInit, 0,
                                                                                                      IMGUI_ACTIVE_BYTE_CAP, 0, false,
                                                                                                      "Dear ImGui process storage is a permanent development-only exception capped at 64 MiB active bytes", true } );

        return imguiOwner;
    }

    static const RuntimeReserveOwnerHandle tracyOwner = RuntimeReserveAllocator::RegisterOwner( { "DevelopmentTools/Tracy", RuntimeReserveSubsystem::DevelopmentTools, RuntimeReservePhase::BackendInit, 0,
                                                                                                  TRACY_ACTIVE_BYTE_CAP, 0, false,
                                                                                                  "Tracy client buffers are a permanent development-only exception capped at 512 MiB active bytes", true } );

    return tracyOwner;
}
} // namespace

namespace SkullbonezCore::Core::Allocation
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

bool TryAccountDevelopmentToolBackingMemory( DevelopmentToolAllocationOwner owner, std::size_t size ) noexcept
{
    return RuntimeReserveAllocator::TryRecordDevelopmentToolBackingAllocation( ToolOwnerHandle( owner ),
                                                                               static_cast<int>( GetRuntimeAllocationPhase() ),
                                                                               static_cast<uint64_t>( size ) );
}

void ReleaseDevelopmentToolBackingMemory( DevelopmentToolAllocationOwner owner, std::size_t size ) noexcept
{
    RuntimeReserveAllocator::RecordFree( ToolOwnerHandle( owner ), static_cast<uint64_t>( size ) );
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

void SetTracyAllocationTracingEnabled( bool enabled ) noexcept
{
    g_tracyAllocationTracingEnabled.store( enabled, std::memory_order_release );
}

uint64_t RecordTracyAllocation( const void* pointer, std::size_t size ) noexcept
{
#if defined( TRACY_ENABLE )

    if ( !pointer || !g_tracyAllocationTracingEnabled.load( std::memory_order_acquire ) || !TracyIsConnected )
    {
        return 0u;
    }

    const uint64_t connectionId = tracy::GetProfiler().ConnectionId();
    DevelopmentToolAllocationScope allocationScope( DevelopmentToolAllocationOwner::Tracy );
    ___tracy_emit_memory_alloc_callstack_named( pointer, size, TRACY_HEAP_CALLSTACK_DEPTH, 0, TRACY_RUNTIME_HEAP_NAME );
    return connectionId;
#else
    (void)pointer;
    (void)size;
    return 0u;
#endif
}

void RecordTracyFree( const void* pointer, uint64_t connectionId ) noexcept
{
#if defined( TRACY_ENABLE )

    if ( !pointer || connectionId == 0u || !g_tracyAllocationTracingEnabled.load( std::memory_order_acquire ) ||
         !TracyIsConnected || tracy::GetProfiler().ConnectionId() != connectionId )
    {
        return;
    }

    DevelopmentToolAllocationScope allocationScope( DevelopmentToolAllocationOwner::Tracy );
    ___tracy_emit_memory_free_callstack_named( pointer, TRACY_HEAP_CALLSTACK_DEPTH, 0, TRACY_RUNTIME_HEAP_NAME );
#else
    (void)pointer;
    (void)connectionId;
#endif
}
} // namespace SkullbonezCore::Core::Allocation
