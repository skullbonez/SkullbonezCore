/*
File: SkullbonezSource/Runtime/Allocation/RuntimeAllocationTracker.cpp
Purpose:
  Implements fixed-storage allocation tracking and the process allocation hook.

Mental model:
  Every C++ heap allocation is wrapped with a tiny header so deletes can update
  active-byte counters. The hot path only touches atomics and CRT malloc/free;
  reporting is a bounded stdout table emitted after the runtime shuts down.

Glossary:
  Violation: An allocation recorded while the process phase is steady gameplay,
    physics, or render under the gameplay guard mode.
  Reentrancy guard: Thread-local flag that prevents tracker internals from
    recursively recording their own emergency work.
  Active bytes: Tracked bytes allocated but not freed at the time of reporting.

Invariants:
  - Allocation/deallocation hooks must not allocate, throw during delete, or use
    engine services.
  - Header layout preserves the caller-requested alignment before returning the
    user pointer.
  - Direct malloc/free in engine code is controlled by the static checker; this
    hook measures C++ allocation paths such as STL growth.

Related:
  - SkullbonezSource/Runtime/Allocation/RuntimeAllocationTracker.h
  - tools/check_allocation_policy.py
*/
#include "RuntimeAllocationTracker.h"

#include <atomic>
#include <cstddef>
#include <cstdlib>
#include <new>

namespace
{
using SkullbonezCore::Runtime::Allocation::RuntimeAllocationGuardMode;
using SkullbonezCore::Runtime::Allocation::RuntimeAllocationPhase;

struct PhaseCounters
{
    std::atomic<uint64_t> allocations;
    std::atomic<uint64_t> frees;
    std::atomic<uint64_t> allocatedBytes;
    std::atomic<uint64_t> freedBytes;
    std::atomic<uint64_t> activeBytes;
    std::atomic<uint64_t> highWaterBytes;
};

struct AllocationHeader
{
    void* raw;
    uint64_t size;
    uint32_t phase;
    uint32_t flags;
    uint32_t magic;
};

constexpr uint32_t ALLOCATION_HEADER_MAGIC = 0xA110CA7Eu;
constexpr uint32_t ALLOCATION_HEADER_RECORDED = 0x1u;
constexpr std::size_t DEFAULT_ALIGNMENT = alignof( std::max_align_t );

std::atomic<int> s_guardMode{ static_cast<int>( RuntimeAllocationGuardMode::Off ) };
std::atomic<int> s_currentPhase{ static_cast<int>( RuntimeAllocationPhase::Startup ) };
std::atomic<uint64_t> s_gameplayViolations{ 0 };
std::atomic<uint64_t> s_totalAllocations{ 0 };
std::atomic<uint64_t> s_totalBytes{ 0 };
PhaseCounters s_phaseCounters[static_cast<int>( RuntimeAllocationPhase::Count )] = {};
thread_local bool s_insideAllocationHook = false;

std::size_t NormalizeAlignment( std::size_t alignment ) noexcept
{
    if ( alignment < DEFAULT_ALIGNMENT )
    {
        alignment = DEFAULT_ALIGNMENT;
    }
    if ( ( alignment & ( alignment - 1u ) ) != 0u )
    {
        std::size_t rounded = DEFAULT_ALIGNMENT;
        while ( rounded < alignment )
        {
            rounded <<= 1u;
        }
        alignment = rounded;
    }
    return alignment;
}

void UpdateHighWater( std::atomic<uint64_t>& highWater, uint64_t value ) noexcept
{
    uint64_t observed = highWater.load( std::memory_order_relaxed );
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
        if ( activeBytes.compare_exchange_weak( observed,
                                                desired,
                                                std::memory_order_relaxed,
                                                std::memory_order_relaxed ) )
        {
            return;
        }
    }
}

bool IsGameplayViolationPhase( RuntimeAllocationPhase phase ) noexcept
{
    return phase == RuntimeAllocationPhase::SteadyGameplay || phase == RuntimeAllocationPhase::Physics ||
           phase == RuntimeAllocationPhase::Render;
}

RuntimeAllocationPhase CurrentPhase() noexcept
{
    const int phase = s_currentPhase.load( std::memory_order_relaxed );
    if ( phase < 0 || phase >= static_cast<int>( RuntimeAllocationPhase::Count ) )
    {
        return RuntimeAllocationPhase::Startup;
    }
    return static_cast<RuntimeAllocationPhase>( phase );
}

RuntimeAllocationGuardMode CurrentMode() noexcept
{
    const int mode = s_guardMode.load( std::memory_order_relaxed );
    if ( mode < static_cast<int>( RuntimeAllocationGuardMode::Off ) ||
         mode > static_cast<int>( RuntimeAllocationGuardMode::Gameplay ) )
    {
        return RuntimeAllocationGuardMode::Off;
    }
    return static_cast<RuntimeAllocationGuardMode>( mode );
}

bool RecordAllocation( RuntimeAllocationPhase phase, uint64_t size ) noexcept
{
    if ( CurrentMode() == RuntimeAllocationGuardMode::Off )
    {
        return false;
    }

    const int phaseIndex = static_cast<int>( phase );
    PhaseCounters& counters = s_phaseCounters[phaseIndex];
    counters.allocations.fetch_add( 1u, std::memory_order_relaxed );
    counters.allocatedBytes.fetch_add( size, std::memory_order_relaxed );
    const uint64_t activeAfter = counters.activeBytes.fetch_add( size, std::memory_order_relaxed ) + size;
    UpdateHighWater( counters.highWaterBytes, activeAfter );
    s_totalAllocations.fetch_add( 1u, std::memory_order_relaxed );
    s_totalBytes.fetch_add( size, std::memory_order_relaxed );

    if ( CurrentMode() == RuntimeAllocationGuardMode::Gameplay && IsGameplayViolationPhase( phase ) )
    {
        s_gameplayViolations.fetch_add( 1u, std::memory_order_relaxed );
    }

    return true;
}

void RecordFree( const AllocationHeader& header ) noexcept
{
    // Invariant: a delete only subtracts bytes that were counted while the
    // guard was enabled. Startup allocations freed during gameplay shutdown
    // still carry tracker headers, but they must not underflow phase counters.
    if ( ( header.flags & ALLOCATION_HEADER_RECORDED ) == 0u )
    {
        return;
    }
    if ( CurrentMode() == RuntimeAllocationGuardMode::Off )
    {
        return;
    }

    const int phaseIndex = header.phase < static_cast<uint32_t>( RuntimeAllocationPhase::Count )
                               ? static_cast<int>( header.phase )
                               : static_cast<int>( RuntimeAllocationPhase::Startup );
    PhaseCounters& counters = s_phaseCounters[phaseIndex];
    counters.frees.fetch_add( 1u, std::memory_order_relaxed );
    counters.freedBytes.fetch_add( header.size, std::memory_order_relaxed );
    SubtractActiveBytes( counters.activeBytes, header.size );
}

void* AllocateTrackedMemory( std::size_t requestedSize, std::size_t requestedAlignment ) noexcept
{
    const std::size_t size = requestedSize == 0u ? 1u : requestedSize;
    const std::size_t alignment = NormalizeAlignment( requestedAlignment );
    const std::size_t totalSize = size + alignment - 1u + sizeof( AllocationHeader );
    void* raw = std::malloc( totalSize );
    if ( !raw )
    {
        return nullptr;
    }

    // Concept: the allocation hook returns the aligned user pointer, but keeps a
    // fixed-size header immediately before it. The stored raw pointer is the
    // only address that may be handed back to CRT free().
    const uintptr_t headerStart = reinterpret_cast<uintptr_t>( raw ) + sizeof( AllocationHeader );
    const uintptr_t userAddress = ( headerStart + alignment - 1u ) & ~( static_cast<uintptr_t>( alignment ) - 1u );
    auto* header = reinterpret_cast<AllocationHeader*>( userAddress - sizeof( AllocationHeader ) );
    header->raw = raw;
    header->size = static_cast<uint64_t>( size );
    header->phase = static_cast<uint32_t>( CurrentPhase() );
    header->flags = 0u;
    header->magic = ALLOCATION_HEADER_MAGIC;

    // Hazard: recording must never allocate through this same hook. The
    // thread-local guard keeps emergency CRT/STL paths from recursively counting
    // tracker internals as gameplay work.
    if ( !s_insideAllocationHook )
    {
        s_insideAllocationHook = true;
        if ( RecordAllocation( static_cast<RuntimeAllocationPhase>( header->phase ), header->size ) )
        {
            header->flags |= ALLOCATION_HEADER_RECORDED;
        }
        s_insideAllocationHook = false;
    }

    return reinterpret_cast<void*>( userAddress );
}

void FreeTrackedMemory( void* pointer ) noexcept
{
    if ( !pointer )
    {
        return;
    }

    auto* header =
        reinterpret_cast<AllocationHeader*>( reinterpret_cast<unsigned char*>( pointer ) - sizeof( AllocationHeader ) );
    if ( header->magic != ALLOCATION_HEADER_MAGIC )
    {
        // Hazard: shutdown or third-party code can call these global delete
        // overloads for storage not produced by our hook. A bad magic value
        // means the safest ownership-preserving behavior is to free the pointer
        // exactly as received and skip counters.
        std::free( pointer );
        return;
    }

    // Lifetime: the header remains valid until raw is freed below. Copy counter
    // data before clearing the magic so a double-delete fails closed into the
    // foreign-pointer path instead of subtracting active bytes twice.
    if ( !s_insideAllocationHook )
    {
        s_insideAllocationHook = true;
        RecordFree( *header );
        s_insideAllocationHook = false;
    }

    void* raw = header->raw;
    header->magic = 0u;
    std::free( raw );
}

void* AllocateOrThrow( std::size_t size, std::size_t alignment )
{
    if ( void* pointer = AllocateTrackedMemory( size, alignment ) )
    {
        return pointer;
    }
    throw std::bad_alloc();
}
} // namespace

namespace SkullbonezCore
{
namespace Runtime
{
namespace Allocation
{
RuntimeAllocationScope::RuntimeAllocationScope( RuntimeAllocationPhase phase ) noexcept
    : m_previous( GetRuntimeAllocationPhase() ), m_active( RuntimeAllocationGuardEnabled() )
{
    if ( m_active )
    {
        SetRuntimeAllocationPhase( phase );
    }
}

RuntimeAllocationScope::~RuntimeAllocationScope() noexcept
{
    if ( m_active )
    {
        SetRuntimeAllocationPhase( m_previous );
    }
}

void SetRuntimeAllocationGuardMode( RuntimeAllocationGuardMode mode ) noexcept
{
    s_guardMode.store( static_cast<int>( mode ), std::memory_order_relaxed );
    ResetRuntimeAllocationCounters();
    SetRuntimeAllocationPhase( RuntimeAllocationPhase::Startup );
}

RuntimeAllocationGuardMode GetRuntimeAllocationGuardMode() noexcept
{
    return CurrentMode();
}

const char* RuntimeAllocationGuardModeName( RuntimeAllocationGuardMode mode ) noexcept
{
    switch ( mode )
    {
    case RuntimeAllocationGuardMode::Off:
        return "off";
    case RuntimeAllocationGuardMode::Measure:
        return "measure";
    case RuntimeAllocationGuardMode::Gameplay:
        return "gameplay";
    default:
        return "unknown";
    }
}

const char* RuntimeAllocationPhaseName( RuntimeAllocationPhase phase ) noexcept
{
    switch ( phase )
    {
    case RuntimeAllocationPhase::Startup:
        return "startup";
    case RuntimeAllocationPhase::SceneLoad:
        return "scene_load";
    case RuntimeAllocationPhase::BackendInit:
        return "backend_init";
    case RuntimeAllocationPhase::SteadyGameplay:
        return "steady_gameplay";
    case RuntimeAllocationPhase::Physics:
        return "physics";
    case RuntimeAllocationPhase::Render:
        return "render";
    case RuntimeAllocationPhase::Replay:
        return "replay";
    case RuntimeAllocationPhase::Capture:
        return "capture";
    case RuntimeAllocationPhase::Shutdown:
        return "shutdown";
    default:
        return "unknown";
    }
}

void SetRuntimeAllocationPhase( RuntimeAllocationPhase phase ) noexcept
{
    s_currentPhase.store( static_cast<int>( phase ), std::memory_order_relaxed );
}

RuntimeAllocationPhase GetRuntimeAllocationPhase() noexcept
{
    return CurrentPhase();
}

bool RuntimeAllocationGuardEnabled() noexcept
{
    return CurrentMode() != RuntimeAllocationGuardMode::Off;
}

bool RuntimeAllocationGuardHasGameplayViolations() noexcept
{
    return RuntimeAllocationGuardViolationCount() > 0u;
}

uint64_t RuntimeAllocationGuardViolationCount() noexcept
{
    return s_gameplayViolations.load( std::memory_order_relaxed );
}

void ResetRuntimeAllocationCounters() noexcept
{
    s_gameplayViolations.store( 0u, std::memory_order_relaxed );
    s_totalAllocations.store( 0u, std::memory_order_relaxed );
    s_totalBytes.store( 0u, std::memory_order_relaxed );
    for ( PhaseCounters& counters : s_phaseCounters )
    {
        counters.allocations.store( 0u, std::memory_order_relaxed );
        counters.frees.store( 0u, std::memory_order_relaxed );
        counters.allocatedBytes.store( 0u, std::memory_order_relaxed );
        counters.freedBytes.store( 0u, std::memory_order_relaxed );
        counters.activeBytes.store( 0u, std::memory_order_relaxed );
        counters.highWaterBytes.store( 0u, std::memory_order_relaxed );
    }
}

void PrintRuntimeAllocationSummary( FILE* out ) noexcept
{
    if ( !out || !RuntimeAllocationGuardEnabled() )
    {
        return;
    }

    const RuntimeAllocationGuardMode mode = GetRuntimeAllocationGuardMode();
    fprintf( out,
             "[allocation-guard] mode=%s total_allocations=%llu total_bytes=%llu gameplay_violations=%llu\n",
             RuntimeAllocationGuardModeName( mode ),
             static_cast<unsigned long long>( s_totalAllocations.load( std::memory_order_relaxed ) ),
             static_cast<unsigned long long>( s_totalBytes.load( std::memory_order_relaxed ) ),
             static_cast<unsigned long long>( RuntimeAllocationGuardViolationCount() ) );
    for ( int phaseIndex = 0; phaseIndex < static_cast<int>( RuntimeAllocationPhase::Count ); ++phaseIndex )
    {
        const PhaseCounters& counters = s_phaseCounters[phaseIndex];
        const uint64_t allocations = counters.allocations.load( std::memory_order_relaxed );
        const uint64_t frees = counters.frees.load( std::memory_order_relaxed );
        const uint64_t bytes = counters.allocatedBytes.load( std::memory_order_relaxed );
        const uint64_t activeBytes = counters.activeBytes.load( std::memory_order_relaxed );
        const uint64_t highWaterBytes = counters.highWaterBytes.load( std::memory_order_relaxed );
        if ( allocations == 0u && frees == 0u && bytes == 0u && activeBytes == 0u && highWaterBytes == 0u )
        {
            continue;
        }

        fprintf( out,
                 "[allocation-guard] phase=%s allocations=%llu frees=%llu bytes=%llu active_bytes=%llu "
                 "high_water_bytes=%llu\n",
                 RuntimeAllocationPhaseName( static_cast<RuntimeAllocationPhase>( phaseIndex ) ),
                 static_cast<unsigned long long>( allocations ),
                 static_cast<unsigned long long>( frees ),
                 static_cast<unsigned long long>( bytes ),
                 static_cast<unsigned long long>( activeBytes ),
                 static_cast<unsigned long long>( highWaterBytes ) );
    }
    if ( RuntimeAllocationGuardHasGameplayViolations() )
    {
        fprintf( out,
                 "[allocation-guard] WARNING: steady gameplay allocation evidence is warning-bearing; "
                 "owner conversion is still required before strict enforcement.\n" );
    }
    else
    {
        fprintf( out, "[allocation-guard] PASS: no steady gameplay allocations recorded by the guard.\n" );
    }
    fflush( out );
}
} // namespace Allocation
} // namespace Runtime
} // namespace SkullbonezCore

// Concept: every global C++ allocation/deallocation overload funnels through
// AllocateTrackedMemory/FreeTrackedMemory so sized, aligned, array, and nothrow
// forms produce one phase-accounting path instead of partial blind spots.
void* operator new( std::size_t size )
{
    return AllocateOrThrow( size, DEFAULT_ALIGNMENT );
}

void* operator new[]( std::size_t size )
{
    return AllocateOrThrow( size, DEFAULT_ALIGNMENT );
}

void* operator new( std::size_t size, const std::nothrow_t& ) noexcept
{
    return AllocateTrackedMemory( size, DEFAULT_ALIGNMENT );
}

void* operator new[]( std::size_t size, const std::nothrow_t& ) noexcept
{
    return AllocateTrackedMemory( size, DEFAULT_ALIGNMENT );
}

void operator delete( void* pointer ) noexcept
{
    FreeTrackedMemory( pointer );
}

void operator delete[]( void* pointer ) noexcept
{
    FreeTrackedMemory( pointer );
}

void operator delete( void* pointer, std::size_t ) noexcept
{
    FreeTrackedMemory( pointer );
}

void operator delete[]( void* pointer, std::size_t ) noexcept
{
    FreeTrackedMemory( pointer );
}

void operator delete( void* pointer, const std::nothrow_t& ) noexcept
{
    FreeTrackedMemory( pointer );
}

void operator delete[]( void* pointer, const std::nothrow_t& ) noexcept
{
    FreeTrackedMemory( pointer );
}

void* operator new( std::size_t size, std::align_val_t alignment )
{
    return AllocateOrThrow( size, static_cast<std::size_t>( alignment ) );
}

void* operator new[]( std::size_t size, std::align_val_t alignment )
{
    return AllocateOrThrow( size, static_cast<std::size_t>( alignment ) );
}

void* operator new( std::size_t size, std::align_val_t alignment, const std::nothrow_t& ) noexcept
{
    return AllocateTrackedMemory( size, static_cast<std::size_t>( alignment ) );
}

void* operator new[]( std::size_t size, std::align_val_t alignment, const std::nothrow_t& ) noexcept
{
    return AllocateTrackedMemory( size, static_cast<std::size_t>( alignment ) );
}

void operator delete( void* pointer, std::align_val_t ) noexcept
{
    FreeTrackedMemory( pointer );
}

void operator delete[]( void* pointer, std::align_val_t ) noexcept
{
    FreeTrackedMemory( pointer );
}

void operator delete( void* pointer, std::size_t, std::align_val_t ) noexcept
{
    FreeTrackedMemory( pointer );
}

void operator delete[]( void* pointer, std::size_t, std::align_val_t ) noexcept
{
    FreeTrackedMemory( pointer );
}

void operator delete( void* pointer, std::align_val_t, const std::nothrow_t& ) noexcept
{
    FreeTrackedMemory( pointer );
}

void operator delete[]( void* pointer, std::align_val_t, const std::nothrow_t& ) noexcept
{
    FreeTrackedMemory( pointer );
}
