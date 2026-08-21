/*
File: SkullbonezSource/Core/Allocation/RuntimeAllocationTracker.cpp
Purpose:
  Implements fixed-storage allocation tracking and the process allocation hook.

Summary:
  Every C++ heap allocation is wrapped with a tiny header so deletes can update
  active-byte counters. The hot path only touches atomics and CRT malloc/free;
  reporting is a bounded stdout table emitted after the runtime shuts down.

Glossary:
  Violation: An allocation recorded while the calling-thread phase is steady
    gameplay, physics, render, or replay under the gameplay guard mode.
  Thread phase: Calling-thread lifecycle label captured in each allocation
    header; scopes on other threads cannot overwrite it.
  Reentrancy guard: Thread-local flag that prevents tracker internals from
    recursively recording their own emergency work.
  Foreign pointer: Address passed to global delete without a readable,
    pointer-bound tracker header produced by this hook.
  Active bytes: Tracked bytes allocated but not freed at the time of reporting.
  Trace connection generation: Monotonic Tracy viewer-session id stored beside
    a heavy-mode allocation so its free cannot leak into a later capture.

Invariants:
  - Allocation/deallocation hooks must not allocate, throw during delete, or use
    engine services.
  - Header layout preserves the caller-requested alignment before returning the
    user pointer.
  - Direct malloc/free in engine code is controlled by the static checker; this
    hook measures C++ allocation paths such as STL growth.
  - Tool permission is checked from the calling thread's owner, so concurrent
    gameplay allocations retain ordinary violation accounting.
  - Allocation phase and reserve owner are both calling-thread state. Keeping
    only one thread-local would create impossible phase/owner pairs.
  - Heavy-mode allocation/free events pair only within one viewer connection.
  - A foreign pointer cannot fault the process while the hook copies its
    candidate header; only a fully readable, pointer-bound provenance cookie
    admits tracker-owned field access.

Related:
  - SkullbonezSource/Core/Allocation/RuntimeAllocationTracker.h
  - tools/check_allocation_policy.py
  - SkullbonezSource/Core/Allocation/DevelopmentToolAllocation.h
  - Agentic/Reference/engine-glossary.md
*/
#include "RuntimeAllocationTracker.h"

#include "../PlatformWin32.h"
#include "RuntimeReserveAllocator.h"
#if defined( TRACY_ENABLE )
#include "DevelopmentToolAllocation.h"
#endif

#include <atomic>
#include <cstddef>
#include <cstdio>
#include <cstdlib>
#include <limits>
#include <new>

namespace
{
using SkullbonezCore::Core::Allocation::RuntimeAllocationGuardMode;
using SkullbonezCore::Core::Allocation::RuntimeAllocationPhase;
using SkullbonezCore::Core::Allocation::RuntimeReserveAllocator;
using SkullbonezCore::Core::Allocation::RuntimeReserveOwnerHandle;

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
    // Why: the CRT owns the original unaligned allocation address; the tracker
    // must preserve that opaque block so FreeTrackedMemory returns it exactly.
    void* raw;
    uint64_t size;
    uint32_t phase;
    uint32_t flags;
    uint16_t owner;
    uint16_t reserved;
    uint32_t magic;
    uint64_t ownershipCookie;
#if defined( TRACY_ENABLE )
    uint64_t tracyConnectionId;
#endif
};

struct CallsiteCounters
{
    std::atomic<uintptr_t> address;
    std::atomic<uintptr_t> parentAddress;
    std::atomic<int> phaseIndex;
    std::atomic<uint32_t> owner;
    std::atomic<uint64_t> allocations;
    std::atomic<uint64_t> violations;
    std::atomic<uint64_t> bytes;
};

constexpr uint32_t ALLOCATION_HEADER_MAGIC = 0xA110CA7Eu;
constexpr uint32_t ALLOCATION_HEADER_RECORDED = 0x1u;
constexpr int MAX_ALLOCATION_CALLSITES = 1024;
constexpr int MAX_PRINTED_CALLSITES = 24;
constexpr std::size_t DEFAULT_ALIGNMENT = alignof( std::max_align_t );

std::atomic<int> s_guardMode { static_cast<int>( RuntimeAllocationGuardMode::Off ) };

// Invariant: phase and reserve-owner attribution must share thread affinity.
// A process-global phase races nested Replay/Render scopes and can pair one
// thread's phase with another thread's owner-zero allocation.
thread_local RuntimeAllocationPhase s_currentPhase = RuntimeAllocationPhase::Startup;
std::atomic<uint64_t> s_gameplayViolations { 0 };
std::atomic<uint64_t> s_foreignFrees { 0 };

// Invariant: the process counter never resets. A newly selected guard mode
// starts a fresh validation interval without erasing lifetime diagnostics.
std::atomic<uint64_t> s_foreignFreeGuardBaseline { 0 };
std::atomic<uint64_t> s_totalAllocations { 0 };
std::atomic<uint64_t> s_totalBytes { 0 };
PhaseCounters s_phaseCounters[static_cast<int>( RuntimeAllocationPhase::Count )] = {};
CallsiteCounters s_callsiteCounters[MAX_ALLOCATION_CALLSITES] = {};
thread_local bool s_insideAllocationHook = false;

uint64_t MixOwnershipCookieValue( uint64_t cookie, uint64_t value ) noexcept
{
    cookie ^= value + 0x9E3779B97F4A7C15ull + ( cookie << 6u ) + ( cookie >> 2u );
    cookie ^= cookie >> 30u;
    cookie *= 0xBF58476D1CE4E5B9ull;
    cookie ^= cookie >> 27u;
    cookie *= 0x94D049BB133111EBull;
    return cookie ^ ( cookie >> 31u );
}

uint64_t AllocationOwnershipCookie( const AllocationHeader& header, const void* userPointer ) noexcept
{
    // Provenance token: ASLR makes the process-local atomic address different
    // each launch, while binding every header field to its exact user pointer
    // prevents a readable copied/shaped header from authorizing another address.
    uint64_t cookie = 0xD1B54A32D192ED03ull ^ static_cast<uint64_t>( reinterpret_cast<uintptr_t>( &s_foreignFrees ) );

    cookie = MixOwnershipCookieValue( cookie, static_cast<uint64_t>( reinterpret_cast<uintptr_t>( userPointer ) ) );
    cookie = MixOwnershipCookieValue( cookie, static_cast<uint64_t>( reinterpret_cast<uintptr_t>( header.raw ) ) );
    cookie = MixOwnershipCookieValue( cookie, header.size );
    cookie = MixOwnershipCookieValue( cookie, static_cast<uint64_t>( header.phase ) << 32u | header.flags );
    cookie = MixOwnershipCookieValue( cookie, static_cast<uint64_t>( header.owner ) << 48u |
                                                  static_cast<uint64_t>( header.reserved ) << 32u | header.magic );

#if defined( TRACY_ENABLE )
    cookie = MixOwnershipCookieValue( cookie, header.tracyConnectionId );
#endif
    return cookie;
}

uintptr_t ProcessImageBase() noexcept
{
    // Why: diagnostics normalize captured callsites when their platform can
    // expose an image base; portable builds retain zero as the honest sentinel.
    return SkullbonezCore::Core::Platform::ProcessImageBase();
}

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
            if ( rounded > std::numeric_limits<std::size_t>::max() / 2u )
            {
                return 0u;
            }

            rounded <<= 1u;
        }

        alignment = rounded;
    }

    return alignment;
}

bool TryComputeAllocationSize( std::size_t size, std::size_t alignment, std::size_t& totalSize ) noexcept
{
    if ( alignment == 0u )
    {
        return false;
    }

    constexpr std::size_t maximum = std::numeric_limits<std::size_t>::max();
    const std::size_t alignmentPadding = alignment - 1u;

    if ( alignmentPadding > maximum - sizeof( AllocationHeader ) )
    {
        return false;
    }

    const std::size_t overhead = alignmentPadding + sizeof( AllocationHeader );

    if ( size > maximum - overhead )
    {
        return false;
    }

    totalSize = size + overhead;
    return true;
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

        if ( activeBytes.compare_exchange_weak( observed, desired, std::memory_order_relaxed, std::memory_order_relaxed ) )
        {
            return;
        }
    }
}

RuntimeAllocationPhase CurrentPhase() noexcept
{
    const int phase = static_cast<int>( s_currentPhase );

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

void RecordCallsite( RuntimeAllocationPhase phase, RuntimeReserveOwnerHandle owner, uintptr_t callsite, uintptr_t parent,
                     bool violation, uint64_t size ) noexcept
{
    if ( callsite == 0u )
    {
        return;
    }

    const int phaseIndex = static_cast<int>( phase );
    const int start = static_cast<int>( ( callsite >> 4u ) % MAX_ALLOCATION_CALLSITES );

    for ( int probe = 0; probe < MAX_ALLOCATION_CALLSITES; ++probe )
    {
        CallsiteCounters& counters = s_callsiteCounters[( start + probe ) % MAX_ALLOCATION_CALLSITES];
        uintptr_t observed = counters.address.load( std::memory_order_acquire );

        if ( observed == callsite && counters.parentAddress.load( std::memory_order_relaxed ) == parent &&
             counters.phaseIndex.load( std::memory_order_relaxed ) == phaseIndex )
        {
            counters.owner.store( owner, std::memory_order_relaxed );
            counters.allocations.fetch_add( 1u, std::memory_order_relaxed );

            if ( violation )
            {
                counters.violations.fetch_add( 1u, std::memory_order_relaxed );
            }

            counters.bytes.fetch_add( size, std::memory_order_relaxed );
            return;
        }

        if ( observed == 0u && counters.address.compare_exchange_strong( observed, callsite, std::memory_order_acq_rel,
                                                                         std::memory_order_acquire ) )
        {
            counters.parentAddress.store( parent, std::memory_order_relaxed );
            counters.phaseIndex.store( phaseIndex, std::memory_order_relaxed );
            counters.owner.store( owner, std::memory_order_relaxed );
            counters.allocations.store( 1u, std::memory_order_relaxed );
            counters.violations.store( violation ? 1u : 0u, std::memory_order_relaxed );
            counters.bytes.store( size, std::memory_order_relaxed );
            return;
        }
    }
}

bool RecordAllocation( RuntimeAllocationPhase phase, uint64_t size, RuntimeReserveOwnerHandle owner,
                       uintptr_t callsite ) noexcept
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
    RuntimeReserveAllocator::RecordAllocation( owner, phaseIndex, size );

    uintptr_t stackFrames[8] = {};
#if defined( _WIN32 )
    // Why: CaptureStackBackTrace reports opaque return addresses through its
    // void-pointer ABI; the tracker stores integer addresses for bounded lookup
    // and never dereferences them.
    void* capturedFrames[8] = {};

    const USHORT capturedCount = CaptureStackBackTrace( 2u, 8u, capturedFrames, nullptr );

    for ( USHORT index = 0; index < capturedCount && index < 8u; ++index )
    {
        stackFrames[index] = reinterpret_cast<uintptr_t>( capturedFrames[index] );
    }
#endif
    const bool approvedReplayGrowth = RuntimeReserveAllocator::IsApprovedReplayGrowthAllocation( owner, phaseIndex );
#if defined( SKULLBONEZ_DEVELOPMENT_TOOLS )
    const bool
        approvedDevelopmentToolAllocation = RuntimeReserveAllocator::IsApprovedDevelopmentToolAllocation( owner,
                                                                                                          phaseIndex );
#else
    constexpr bool approvedDevelopmentToolAllocation = false;
#endif
    const bool gameplayViolation = CurrentMode() == RuntimeAllocationGuardMode::Gameplay &&
                                   SkullbonezCore::Core::Allocation::IsRuntimeAllocationGuardedSteadyPhase( phase ) &&
                                   !approvedReplayGrowth && !approvedDevelopmentToolAllocation;

    // Why: STL allocation helpers often inline several layers below the owner.
    // The fourth captured frame normally clears those helpers while remaining
    // near the engine operation; fall back to the nearest captured frame on a
    // shallower platform stack.
    const uintptr_t parent = stackFrames[3] != 0u ? stackFrames[3] : stackFrames[0];
    RecordCallsite( phase, owner, callsite, parent, gameplayViolation, size );

    if ( gameplayViolation )
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
    SkullbonezCore::Core::Allocation::RuntimeReserveAllocator::RecordFree( header.owner, header.size );
}

void* AllocateTrackedMemory( std::size_t requestedSize, std::size_t requestedAlignment, void* callsite,
                             bool* overflowed = nullptr ) noexcept
{
    const std::size_t size = requestedSize == 0u ? 1u : requestedSize;
    const std::size_t alignment = NormalizeAlignment( requestedAlignment );
    std::size_t totalSize = 0u;

    if ( !TryComputeAllocationSize( size, alignment, totalSize ) )
    {
        if ( overflowed )
        {
            *overflowed = true;
        }

        return nullptr;
    }

    if ( overflowed )
    {
        *overflowed = false;
    }

    void* raw = std::malloc( totalSize );

    if ( !raw )
    {
        return nullptr;
    }

    // Concept: the allocation hook returns the aligned user pointer, but keeps a
    // fixed-size header immediately before it. The stored raw pointer is the
    // only address that may be handed back to CRT free().
    // Why: alignment arithmetic must use uintptr_t, then reconstruct the exact
    // header/user pointer shapes required by the global allocation ABI.
    const uintptr_t headerStart = reinterpret_cast<uintptr_t>( raw ) + sizeof( AllocationHeader );
    const uintptr_t userAddress = ( headerStart + alignment - 1u ) & ~( static_cast<uintptr_t>( alignment ) - 1u );
    auto* header = reinterpret_cast<AllocationHeader*>( userAddress - sizeof( AllocationHeader ) );
    header->raw = raw;
    header->size = static_cast<uint64_t>( size );
    header->phase = static_cast<uint32_t>( CurrentPhase() );
    header->flags = 0u;
    const RuntimeReserveOwnerHandle owner = RuntimeReserveAllocator::CurrentOwner();
    header->owner = static_cast<uint16_t>( owner );
    header->reserved = 0u;
    header->magic = ALLOCATION_HEADER_MAGIC;
    header->ownershipCookie = 0u;
#if defined( TRACY_ENABLE )
    header->tracyConnectionId = 0u;
#endif

    // Hazard: recording must never allocate through this same hook. The
    // thread-local guard keeps emergency CRT/STL paths from recursively counting
    // tracker internals as gameplay work.
    if ( !s_insideAllocationHook )
    {
        s_insideAllocationHook = true;

        if ( RecordAllocation( static_cast<RuntimeAllocationPhase>( header->phase ), header->size, owner,
                               reinterpret_cast<uintptr_t>( callsite ) ) )
        {
            header->flags |= ALLOCATION_HEADER_RECORDED;
        }

#if defined( TRACY_ENABLE )
        // Heavy Tracy capture is independent of allocation-guard mode. The
        // connection id pairs this allocation with a free only inside the same
        // viewer session, avoiding stale frees after disconnect/reconnect.
        header->tracyConnectionId = SkullbonezCore::Core::Allocation::RecordTracyAllocation( reinterpret_cast<void*>( userAddress ),
                                                                                             size );
#endif
        s_insideAllocationHook = false;
    }

    header->ownershipCookie = AllocationOwnershipCookie( *header, reinterpret_cast<void*>( userAddress ) );
    return reinterpret_cast<void*>( userAddress );
}

bool TryCopyAllocationHeader( const AllocationHeader* header, AllocationHeader& copy ) noexcept
{
#if defined( _WIN32 ) && defined( _MSC_VER )
    // Hazard: magic alone is not provenance. A foreign candidate can expose
    // only the magic bytes while raw/size remain inaccessible, or can shape a
    // readable public magic value around a non-CRT raw pointer. Copy the entire
    // candidate under table-based SEH before validating its pointer-bound
    // process cookie; no candidate field is read after a partial copy.
    __try
    {
        copy = *header;
        return true;
    }
    __except ( EXCEPTION_EXECUTE_HANDLER )
    {
        return false;
    }
#else
    // The shipping global hook is Win32/MSVC-owned. Other toolchain builds keep
    // the existing direct copy until they acquire an equivalent signal guard.
    copy = *header;
    return true;
#endif
}

// Why: only debug/profile and the allocation-fatal probe call this helper.
// Compile its definition under the same condition so warning-clean Release and
// portable builds do not retain a dead internal symbol.
#if defined( _DEBUG ) || defined( SKULLBONEZ_PROFILE_ENABLED ) || defined( SKULLBONEZ_TEST_PROFILE_ALLOCATION_FATAL )
[[noreturn]] void FatalForeignFree( void* pointer, RuntimeAllocationPhase phase, RuntimeReserveOwnerHandle owner,
                                    const char* headerState, uint64_t foreignFreeCount ) noexcept
{
    char message[384] = {};
    std::snprintf( message, sizeof( message ),
                   "FATAL[Runtime/Allocation]: unprovable foreign pointer delete. pointer=%p phase=%s owner=%u "
                   "header=%s foreign_free_count=%llu\n",
                   pointer, SkullbonezCore::Core::Allocation::RuntimeAllocationPhaseName( phase ),
                   static_cast<unsigned int>( owner ), headerState, static_cast<unsigned long long>( foreignFreeCount ) );

    SkullbonezCore::Core::Platform::WriteDebugger( message );
    std::fputs( message, stderr );
    std::fflush( stderr );

    SkullbonezCore::Core::Platform::DebugBreak();

    std::abort();
}
#endif

void HandleForeignFree( void* pointer, const char* headerState ) noexcept
{
    const RuntimeAllocationPhase phase = CurrentPhase();
    const RuntimeReserveOwnerHandle owner = RuntimeReserveAllocator::CurrentOwner();
    const uint64_t foreignFreeCount = s_foreignFrees.fetch_add( 1u, std::memory_order_relaxed ) + 1u;

#if defined( _DEBUG ) || defined( SKULLBONEZ_PROFILE_ENABLED ) || defined( SKULLBONEZ_TEST_PROFILE_ALLOCATION_FATAL )
    FatalForeignFree( pointer, phase, owner, headerState, foreignFreeCount );
#else
    char message[384] = {};
    std::snprintf( message, sizeof( message ),
                   "[allocation-guard] FOREIGN_FREE pointer=%p phase=%s owner=%u header=%s foreign_free_count=%llu\n",
                   pointer, SkullbonezCore::Core::Allocation::RuntimeAllocationPhaseName( phase ),
                   static_cast<unsigned int>( owner ), headerState, static_cast<unsigned long long>( foreignFreeCount ) );

    SkullbonezCore::Core::Platform::WriteDebugger( message );
    std::fputs( message, stderr );
    std::fflush( stderr );

    // Invariant: the owner ruled Release foreign frees counted and reported.
    // Hazard: fallback assumes the foreign allocator is this process's CRT;
    // s_foreignFrees is the tripwire for that intentionally shipped risk.
    std::free( pointer );
#endif
}

void FreeTrackedMemory( void* pointer ) noexcept
{
    if ( !pointer )
    {
        return;
    }

    auto* header = reinterpret_cast<AllocationHeader*>( reinterpret_cast<unsigned char*>( pointer ) -
                                                        sizeof( AllocationHeader ) );

    AllocationHeader headerCopy = {};

    if ( !TryCopyAllocationHeader( header, headerCopy ) )
    {
        HandleForeignFree( pointer, "unreadable" );
        return;
    }

    if ( headerCopy.magic != ALLOCATION_HEADER_MAGIC )
    {
        HandleForeignFree( pointer, "bad_magic" );
        return;
    }

    if ( headerCopy.ownershipCookie != AllocationOwnershipCookie( headerCopy, pointer ) )
    {
        HandleForeignFree( pointer, "bad_provenance" );
        return;
    }

    // Lifetime: the guarded, provenance-checked snapshot remains valid after
    // raw is freed below. Clearing the live magic makes a double-delete fail
    // closed into the foreign-pointer path instead of subtracting twice.
    if ( !s_insideAllocationHook )
    {
        s_insideAllocationHook = true;
#if defined( TRACY_ENABLE )
        SkullbonezCore::Core::Allocation::RecordTracyFree( pointer, headerCopy.tracyConnectionId );
#endif
        RecordFree( headerCopy );
        s_insideAllocationHook = false;
    }

    void* raw = headerCopy.raw;
    header->magic = 0u;
    std::free( raw );
}

[[noreturn]] void FatalAllocationFailure( std::size_t size, std::size_t alignment, const char* reason ) noexcept
{
    char message[256] = {};
    std::snprintf( message, sizeof( message ),
                   "FATAL[Runtime/Allocation]: global operator new failed. reason=%s size=%llu alignment=%llu\n", reason,
                   static_cast<unsigned long long>( size ), static_cast<unsigned long long>( alignment ) );

    SkullbonezCore::Core::Platform::WriteDebugger( message );
    std::fputs( message, stderr );
    std::fflush( stderr );

#if defined( _DEBUG ) || defined( SKULLBONEZ_PROFILE_ENABLED )
    SkullbonezCore::Core::Platform::DebugBreak();
#endif

    std::abort();
}

void* AllocateOrFatal( std::size_t size, std::size_t alignment, void* callsite )
{
    const std::size_t normalizedAlignment = NormalizeAlignment( alignment );
    bool overflowed = false;

    if ( void* pointer = AllocateTrackedMemory( size, normalizedAlignment, callsite, &overflowed ) )
    {
        return pointer;
    }

    // Fatal invariant / Hazard: malloc has already failed inside the global allocation
    // hook, so this path must not call SB_FATAL or any SkullbonezCore::Core::EngineLog-backed helper.
    FatalAllocationFailure( size, normalizedAlignment, overflowed ? "size_arithmetic_overflow" : "exhausted_memory" );
}
} // namespace

namespace SkullbonezCore
{
namespace Core
{
namespace Allocation
{
RuntimeAllocationScope::RuntimeAllocationScope( RuntimeAllocationPhase phase ) noexcept
    : m_previous( GetRuntimeAllocationPhase() )
{
    // Invariant: lifecycle phase is runtime policy input even when allocation
    // counting is disabled. Upload overflow, replay reserve, and future phase
    // consumers must not silently observe Startup in ordinary launches.
    SetRuntimeAllocationPhase( phase );
}

RuntimeAllocationScope::~RuntimeAllocationScope() noexcept
{
    SetRuntimeAllocationPhase( m_previous );
}

void SetRuntimeAllocationGuardMode( RuntimeAllocationGuardMode mode ) noexcept
{
    s_guardMode.store( static_cast<int>( mode ), std::memory_order_relaxed );
    ResetRuntimeAllocationCounters();
    s_foreignFreeGuardBaseline.store( RuntimeAllocationForeignFreeCount(), std::memory_order_relaxed );
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
    case RuntimeAllocationPhase::Diagnostics:
        return "diagnostics";
    case RuntimeAllocationPhase::Shutdown:
        return "shutdown";
    default:
        return "unknown";
    }
}

void SetRuntimeAllocationPhase( RuntimeAllocationPhase phase ) noexcept
{
    s_currentPhase = phase;
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
    return RuntimeAllocationGuardViolationCount() > 0u ||
           RuntimeAllocationForeignFreeCount() > s_foreignFreeGuardBaseline.load( std::memory_order_relaxed ) ||
           RuntimeReserveAllocator::HasPolicyViolations();
}

uint64_t RuntimeAllocationGuardViolationCount() noexcept
{
    return s_gameplayViolations.load( std::memory_order_relaxed );
}

uint64_t RuntimeAllocationForeignFreeCount() noexcept
{
    return s_foreignFrees.load( std::memory_order_relaxed );
}

void ResetRuntimeAllocationCounters() noexcept
{
    s_gameplayViolations.store( 0u, std::memory_order_relaxed );
    s_totalAllocations.store( 0u, std::memory_order_relaxed );
    s_totalBytes.store( 0u, std::memory_order_relaxed );
    RuntimeReserveAllocator::ResetCounters();

    for ( PhaseCounters& counters : s_phaseCounters )
    {
        counters.allocations.store( 0u, std::memory_order_relaxed );
        counters.frees.store( 0u, std::memory_order_relaxed );
        counters.allocatedBytes.store( 0u, std::memory_order_relaxed );
        counters.freedBytes.store( 0u, std::memory_order_relaxed );
        counters.activeBytes.store( 0u, std::memory_order_relaxed );
        counters.highWaterBytes.store( 0u, std::memory_order_relaxed );
    }

    for ( CallsiteCounters& counters : s_callsiteCounters )
    {
        counters.address.store( 0u, std::memory_order_relaxed );
        counters.parentAddress.store( 0u, std::memory_order_relaxed );
        counters.phaseIndex.store( -1, std::memory_order_relaxed );
        counters.owner.store( 0u, std::memory_order_relaxed );
        counters.allocations.store( 0u, std::memory_order_relaxed );
        counters.violations.store( 0u, std::memory_order_relaxed );
        counters.bytes.store( 0u, std::memory_order_relaxed );
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
             "[allocation-guard] mode=%s total_allocations=%llu total_bytes=%llu gameplay_violations=%llu "
             "foreign_frees=%llu\n",
             RuntimeAllocationGuardModeName( mode ),
             static_cast<unsigned long long>( s_totalAllocations.load( std::memory_order_relaxed ) ),
             static_cast<unsigned long long>( s_totalBytes.load( std::memory_order_relaxed ) ),
             static_cast<unsigned long long>( RuntimeAllocationGuardViolationCount() ),
             static_cast<unsigned long long>( RuntimeAllocationForeignFreeCount() ) );

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
                 static_cast<unsigned long long>( allocations ), static_cast<unsigned long long>( frees ),
                 static_cast<unsigned long long>( bytes ), static_cast<unsigned long long>( activeBytes ),
                 static_cast<unsigned long long>( highWaterBytes ) );
    }

    RuntimeReserveAllocator::PrintSummary( out );
    const uintptr_t imageBase = ProcessImageBase();
    const CallsiteCounters* topCallsites[MAX_PRINTED_CALLSITES] = {};

    uint64_t topCounts[MAX_PRINTED_CALLSITES] = {};

    const bool rankViolationCallsites = mode == RuntimeAllocationGuardMode::Gameplay;

    for ( const CallsiteCounters& counters : s_callsiteCounters )
    {
        const uintptr_t address = counters.address.load( std::memory_order_acquire );
        const uint64_t allocations = counters.allocations.load( std::memory_order_relaxed );
        const uint64_t violations = counters.violations.load( std::memory_order_relaxed );
        const int phaseIndex = counters.phaseIndex.load( std::memory_order_relaxed );

        if ( address == 0u || allocations == 0u || phaseIndex < 0 ||
             phaseIndex >= static_cast<int>( RuntimeAllocationPhase::Count ) ||
             !SkullbonezCore::Core::Allocation::IsRuntimeAllocationGuardedSteadyPhase( static_cast<RuntimeAllocationPhase>( phaseIndex ) ) )
        {
            continue;
        }

        const uint64_t rankCount = rankViolationCallsites ? violations : allocations;

        if ( rankCount == 0u )
        {
            continue;
        }

        for ( int rank = 0; rank < MAX_PRINTED_CALLSITES; ++rank )
        {
            if ( rankCount <= topCounts[rank] )
            {
                continue;
            }

            for ( int move = MAX_PRINTED_CALLSITES - 1; move > rank; --move )
            {
                topCounts[move] = topCounts[move - 1];
                topCallsites[move] = topCallsites[move - 1];
            }

            topCounts[rank] = rankCount;
            topCallsites[rank] = &counters;
            break;
        }
    }

    for ( int rank = 0; rank < MAX_PRINTED_CALLSITES; ++rank )
    {
        const CallsiteCounters* counters = topCallsites[rank];

        if ( !counters )
        {
            continue;
        }

        const uintptr_t address = counters->address.load( std::memory_order_relaxed );
        const uintptr_t parent = counters->parentAddress.load( std::memory_order_relaxed );
        const uintptr_t rva = imageBase != 0u && address >= imageBase ? address - imageBase : address;
        const uintptr_t parentRva = imageBase != 0u && parent >= imageBase ? parent - imageBase : parent;
        const int phaseIndex = counters->phaseIndex.load( std::memory_order_relaxed );
        fprintf( out,
                 "[allocation-guard] callsite rank=%d phase=%s owner=%u rva=0x%llx parent_rva=0x%llx "
                 "allocations=%llu violations=%llu bytes=%llu\n",
                 rank + 1, RuntimeAllocationPhaseName( static_cast<RuntimeAllocationPhase>( phaseIndex ) ),
                 counters->owner.load( std::memory_order_relaxed ), static_cast<unsigned long long>( rva ),
                 static_cast<unsigned long long>( parentRva ),
                 static_cast<unsigned long long>( counters->allocations.load( std::memory_order_relaxed ) ),
                 static_cast<unsigned long long>( counters->violations.load( std::memory_order_relaxed ) ),
                 static_cast<unsigned long long>( counters->bytes.load( std::memory_order_relaxed ) ) );
    }

    if ( RuntimeAllocationGuardHasGameplayViolations() )
    {
        fprintf( out, "[allocation-guard] VIOLATION: gameplay allocation guard detected heap or reserve policy "
                      "violations; strict mode will fail after the summary.\n" );
    }
    else
    {
        fprintf( out, "[allocation-guard] PASS: no steady gameplay allocations or reserve policy violations recorded by "
                      "the guard.\n" );
    }

    fflush( out );
}
} // namespace Allocation
} // namespace Core
} // namespace SkullbonezCore

#if defined( _MSC_VER )
#define SKULLBONEZ_ALLOCATION_CALLSITE() _ReturnAddress()
#else
#define SKULLBONEZ_ALLOCATION_CALLSITE() nullptr
#endif

// Concept: every global C++ allocation/deallocation overload funnels through
// AllocateTrackedMemory/FreeTrackedMemory so sized, aligned, array, and nothrow
// forms produce one phase-accounting path instead of partial blind spots.
// Why: these pointer signatures are mandated by the global operator new/delete
// ABI. They are the process allocation boundary, not an engine-facing raw API.
void* operator new( std::size_t size )
{
    return AllocateOrFatal( size, DEFAULT_ALIGNMENT, SKULLBONEZ_ALLOCATION_CALLSITE() );
}

void* operator new[]( std::size_t size )
{
    return AllocateOrFatal( size, DEFAULT_ALIGNMENT, SKULLBONEZ_ALLOCATION_CALLSITE() );
}

void* operator new( std::size_t size, const std::nothrow_t& ) noexcept
{
    return AllocateTrackedMemory( size, DEFAULT_ALIGNMENT, SKULLBONEZ_ALLOCATION_CALLSITE() );
}

void* operator new[]( std::size_t size, const std::nothrow_t& ) noexcept
{
    return AllocateTrackedMemory( size, DEFAULT_ALIGNMENT, SKULLBONEZ_ALLOCATION_CALLSITE() );
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
    return AllocateOrFatal( size, static_cast<std::size_t>( alignment ), SKULLBONEZ_ALLOCATION_CALLSITE() );
}

void* operator new[]( std::size_t size, std::align_val_t alignment )
{
    return AllocateOrFatal( size, static_cast<std::size_t>( alignment ), SKULLBONEZ_ALLOCATION_CALLSITE() );
}

void* operator new( std::size_t size, std::align_val_t alignment, const std::nothrow_t& ) noexcept
{
    return AllocateTrackedMemory( size, static_cast<std::size_t>( alignment ), SKULLBONEZ_ALLOCATION_CALLSITE() );
}

void* operator new[]( std::size_t size, std::align_val_t alignment, const std::nothrow_t& ) noexcept
{
    return AllocateTrackedMemory( size, static_cast<std::size_t>( alignment ), SKULLBONEZ_ALLOCATION_CALLSITE() );
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

#undef SKULLBONEZ_ALLOCATION_CALLSITE
