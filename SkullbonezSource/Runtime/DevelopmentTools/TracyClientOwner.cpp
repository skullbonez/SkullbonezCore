/*
File: SkullbonezSource/Runtime/DevelopmentTools/TracyClientOwner.cpp
Purpose:
  Owns Tracy lifecycle, owner zones, plots, heavy allocation tracing, and status.

Summary:
  The process composition root starts the manually managed on-demand client
  before engine workers exist when profiling was explicitly requested, and
  shuts it down after joining them. A fixed source-location registry maps the
  engine profiler's established owner paths into Tracy. Standard mode records
  zones and capacity plots; explicit heavy mode also records call stacks and
  global C++ allocation events.

Glossary:
  On-demand client: Tracy mode that records only while an external viewer is
    connected instead of retaining an unbounded pre-connection history.
  Connection snapshot: Direct read of Tracy's existing atomic connection flag;
    it does not probe the network, launch a process, or allocate.
  Capture configuration: Tracy is off unless SKORE_TRACY_MODE selects
    `standard` or `heavy`; standard is the comparable low-overhead mode.

Invariants:
  - Startup and shutdown are idempotent at the engine boundary.
  - Frame marks are ignored until manual startup has completed.
  - The 32-byte worker-name buffer bounds all formatted thread labels.
  - Source locations borrow static engine marker strings and never outlive the
    process-owned client.
  - Heavy allocation frees are emitted only to the connection that observed
    the matching allocation.
  - Main/render/replay/prediction/IO share one lane in the current topology;
    the main thread name states that fact rather than inventing fake threads.

Related:
  - SkullbonezSource/Runtime/DevelopmentTools/TracyClientOwner.h
  - SkullbonezSource/Runtime/Init.cpp
  - SkullbonezSource/Runtime/RunFrame.cpp
  - SkullbonezSource/Core/WorkerPool.cpp
*/
#include "TracyClientOwner.h"

#include "../Allocation/DevelopmentToolAllocation.h"
#include "../../Core/PlatformWin32.h"

#include <tracy/Tracy.hpp>
#include <tracy/TracyC.h>

#include <atomic>
#include <cstdio>
#include <cstring>
#include <mutex>

namespace RuntimeAllocation = SkullbonezCore::Runtime::Allocation;

namespace
{
constexpr uint32_t MAX_OWNER_SOURCE_LOCATIONS = 256u;
constexpr int HEAVY_CALLSTACK_DEPTH = 16;
constexpr const char* OWNER_ZONE_SOURCE = "SkullbonezCore owner boundary";
constexpr const char* OWNER_ZONE_FUNCTION = "Engine owner interval";
constexpr const char* CAPTURE_MODE_ENVIRONMENT = "SKORE_TRACY_MODE";
constexpr const char* STANDARD_MODE_VALUE = "standard";
constexpr const char* HEAVY_MODE_VALUE = "heavy";
constexpr const char* RUNTIME_HEAP_NAME = "Skore Runtime C++ Heap";

enum class RequestedCaptureMode
{
    Off,
    Standard,
    Heavy
};

struct OwnerSourceLocation
{
    ___tracy_source_location_data tracy = {};
    uint32_t hash = 0u;
};

std::atomic<bool> g_tracyInitialized{ false };
std::atomic<bool> g_tracyHeavyMode{ false };
OwnerSourceLocation g_ownerSourceLocations[MAX_OWNER_SOURCE_LOCATIONS] = {};
std::atomic<uint32_t> g_ownerSourceLocationCount{ 0u };
std::mutex g_ownerSourceLocationMutex;

RequestedCaptureMode ReadRequestedCaptureMode() noexcept
{
    char value[16] = {};
    const DWORD copied =
        GetEnvironmentVariableA( CAPTURE_MODE_ENVIRONMENT, value, static_cast<DWORD>( sizeof( value ) ) );
    if ( copied == 0u || copied >= sizeof( value ) )
    {
        return RequestedCaptureMode::Off;
    }
    if ( _stricmp( value, STANDARD_MODE_VALUE ) == 0 )
    {
        return RequestedCaptureMode::Standard;
    }
    if ( _stricmp( value, HEAVY_MODE_VALUE ) == 0 )
    {
        return RequestedCaptureMode::Heavy;
    }
    return RequestedCaptureMode::Off;
}

bool IsInitialized() noexcept
{
    return g_tracyInitialized.load( std::memory_order_acquire );
}

bool IsHeavyCaptureConnected() noexcept
{
    return IsInitialized() && g_tracyHeavyMode.load( std::memory_order_acquire ) && TracyIsConnected;
}
} // namespace

namespace SkullbonezCore::Runtime::DevelopmentTools
{
TracyClientOwner::~TracyClientOwner()
{
    Shutdown();
}

void TracyClientOwner::Start()
{
    if ( m_started )
    {
        return;
    }

    const RequestedCaptureMode captureMode = ReadRequestedCaptureMode();
    if ( captureMode == RequestedCaptureMode::Off )
    {
        // Why: Tracy's vendor client owns sizeable transport queues even while
        // no viewer is attached. Requiring an explicit capture mode keeps the
        // named default/perf configuration inside the established memory gate.
        fprintf( stdout,
                 "[tracy] Client disabled. Set SKORE_TRACY_MODE=standard or heavy before launch to capture.\n" );
        fflush( stdout );
        return;
    }

    RuntimeAllocation::DevelopmentToolAllocationScope allocationScope(
        RuntimeAllocation::DevelopmentToolAllocationOwner::Tracy );
    tracy::StartupProfiler();
    m_started = tracy::IsProfilerStarted();
    const bool heavyMode = m_started && captureMode == RequestedCaptureMode::Heavy;
    g_tracyHeavyMode.store( heavyMode, std::memory_order_release );
    g_tracyInitialized.store( m_started, std::memory_order_release );
    if ( !m_started )
    {
        return;
    }

    // Concept: current runtime topology executes render, replay/prediction
    // coordination, and cold IO on the main thread. A composite label makes
    // that ownership visible without pretending those roles have private lanes.
    tracy::SetThreadName( "Skore Main + Render + Replay + IO" );
    TracySetProgramName( "SkullbonezCore" );
    fprintf( stdout,
             "[tracy] Manual on-demand client started. viewer=waiting capture=%s callstacks=%s allocations=%s\n",
             heavyMode ? "heavy" : "standard",
             heavyMode ? "depth-16" : "off",
             heavyMode ? "global-cpp-heap" : "off" );
    fflush( stdout );
}

void TracyClientOwner::Shutdown() noexcept
{
    if ( !m_started )
    {
        return;
    }

    RuntimeAllocation::DevelopmentToolAllocationScope allocationScope(
        RuntimeAllocation::DevelopmentToolAllocationOwner::Tracy );
    // Lifetime: publish the stopped state before freeing Tracy's process data;
    // UI snapshots and marker seams therefore stop reaching the vendor first.
    g_tracyInitialized.store( false, std::memory_order_release );
    g_tracyHeavyMode.store( false, std::memory_order_release );
    tracy::ShutdownProfiler();
    m_started = false;
    fprintf( stdout, "[tracy] Client stopped after engine worker shutdown.\n" );
    fflush( stdout );
}

TracyClientStatus TracyClientOwner::CopyStatus() noexcept
{
    const bool initialized = g_tracyInitialized.load( std::memory_order_acquire );
    // Why: IsConnected is an atomic state read owned by the client. The editor
    // never scans processes, opens sockets, or constructs a string per frame.
    return { true,
             initialized,
             initialized && TracyIsConnected,
             initialized && g_tracyHeavyMode.load( std::memory_order_acquire ) };
}

void TracyClientOwner::MarkSubmittedFrame() noexcept
{
    if ( !g_tracyInitialized.load( std::memory_order_acquire ) )
    {
        return;
    }

    RuntimeAllocation::DevelopmentToolAllocationScope allocationScope(
        RuntimeAllocation::DevelopmentToolAllocationOwner::Tracy );
    FrameMark;
}

void TracyClientOwner::NameWorkerThread( int workerIndex ) noexcept
{
    if ( !g_tracyInitialized.load( std::memory_order_acquire ) )
    {
        return;
    }

    char threadName[32] = {};
    _snprintf_s( threadName, sizeof( threadName ), _TRUNCATE, "Skore Worker %02d", workerIndex );
    RuntimeAllocation::DevelopmentToolAllocationScope allocationScope(
        RuntimeAllocation::DevelopmentToolAllocationOwner::Tracy );
    tracy::SetThreadNameWithHint( threadName, 1 );
}

uint32_t TracyClientOwner::RegisterOwnerZone( const char* fullPath, uint32_t hash ) noexcept
{
    if ( !fullPath || !fullPath[0] )
    {
        return 0u;
    }

    struct ThreadZoneCacheEntry
    {
        const char* name = nullptr;
        uint32_t hash = 0u;
        uint32_t handle = 0u;
    };
    // Why: worker scopes repeat a small marker vocabulary at job frequency.
    // This fixed direct-mapped cache keeps the common lookup off the global
    // registration mutex without growing per-thread storage.
    thread_local ThreadZoneCacheEntry threadCache[16] = {};
    ThreadZoneCacheEntry& cached = threadCache[hash % 16u];
    if ( cached.handle != 0u && cached.hash == hash && std::strcmp( cached.name, fullPath ) == 0 )
    {
        return cached.handle;
    }

    std::lock_guard<std::mutex> lock( g_ownerSourceLocationMutex );
    const uint32_t sourceLocationCount = g_ownerSourceLocationCount.load( std::memory_order_acquire );
    for ( uint32_t index = 0u; index < sourceLocationCount; ++index )
    {
        const OwnerSourceLocation& source = g_ownerSourceLocations[index];
        if ( source.hash == hash && std::strcmp( source.tracy.name, fullPath ) == 0 )
        {
            cached = { fullPath, hash, index + 1u };
            return cached.handle;
        }
    }
    if ( sourceLocationCount >= MAX_OWNER_SOURCE_LOCATIONS )
    {
        return 0u;
    }

    // Lifetime: profiler marker names are static literals borrowed for the
    // process lifetime. Tracy reads these fixed records by address, so neither
    // the record nor its strings may be transient or dynamically grown.
    OwnerSourceLocation& source = g_ownerSourceLocations[sourceLocationCount];
    source.tracy = { fullPath, OWNER_ZONE_FUNCTION, OWNER_ZONE_SOURCE, 0u, 0u };
    source.hash = hash;
    g_ownerSourceLocationCount.store( sourceLocationCount + 1u, std::memory_order_release );
    cached = { fullPath, hash, sourceLocationCount + 1u };
    return cached.handle;
}

TracyZoneToken TracyClientOwner::BeginOwnerZone( uint32_t sourceLocationHandle ) noexcept
{
    TracyZoneToken token;
    if ( !IsInitialized() || sourceLocationHandle == 0u ||
         sourceLocationHandle > g_ownerSourceLocationCount.load( std::memory_order_acquire ) )
    {
        return token;
    }

    RuntimeAllocation::DevelopmentToolAllocationScope allocationScope(
        RuntimeAllocation::DevelopmentToolAllocationOwner::Tracy );
    const ___tracy_source_location_data* source = &g_ownerSourceLocations[sourceLocationHandle - 1u].tracy;
    const TracyCZoneCtx context = g_tracyHeavyMode.load( std::memory_order_acquire )
                                      ? ___tracy_emit_zone_begin_callstack( source, HEAVY_CALLSTACK_DEPTH, 1 )
                                      : ___tracy_emit_zone_begin( source, 1 );
    token.active = context.active;
    token.id = token.active ? context.id : 0u;
    token.connectionId = token.active ? tracy::GetProfiler().ConnectionId() : 0u;
    return token;
}

void TracyClientOwner::EndOwnerZone( TracyZoneToken token ) noexcept
{
    if ( !token.active || !IsInitialized() || !TracyIsConnected ||
         tracy::GetProfiler().ConnectionId() != token.connectionId )
    {
        return;
    }

    RuntimeAllocation::DevelopmentToolAllocationScope allocationScope(
        RuntimeAllocation::DevelopmentToolAllocationOwner::Tracy );
    ___tracy_emit_zone_end( { token.id, token.active } );
}

void TracyClientOwner::PublishPlot( const char* name, double value ) noexcept
{
    if ( !IsInitialized() || !name || !TracyIsConnected )
    {
        return;
    }

    RuntimeAllocation::DevelopmentToolAllocationScope allocationScope(
        RuntimeAllocation::DevelopmentToolAllocationOwner::Tracy );
    ___tracy_emit_plot( name, value );
}

void TracyClientOwner::PublishDevelopmentAllocationPlots() noexcept
{
    RuntimeAllocation::DevelopmentToolAllocationStats imguiStats;
    RuntimeAllocation::DevelopmentToolAllocationStats tracyStats;
    if ( RuntimeAllocation::CopyDevelopmentToolAllocationStats(
             RuntimeAllocation::DevelopmentToolAllocationOwner::DearImGui,
             imguiStats ) )
    {
        PublishPlot( "Counter/DevelopmentTools/ImGuiActiveBytes", static_cast<double>( imguiStats.activeBytes ) );
        PublishPlot( "Counter/DevelopmentTools/ImGuiHighWaterBytes", static_cast<double>( imguiStats.highWaterBytes ) );
    }
    if ( RuntimeAllocation::CopyDevelopmentToolAllocationStats(
             RuntimeAllocation::DevelopmentToolAllocationOwner::Tracy,
             tracyStats ) )
    {
        PublishPlot( "Counter/DevelopmentTools/TracyActiveBytes", static_cast<double>( tracyStats.activeBytes ) );
        PublishPlot( "Counter/DevelopmentTools/TracyHighWaterBytes", static_cast<double>( tracyStats.highWaterBytes ) );
    }
}

uint64_t TracyClientOwner::RecordAllocation( const void* pointer, std::size_t size ) noexcept
{
    if ( !pointer || !IsHeavyCaptureConnected() )
    {
        return 0u;
    }

    const uint64_t connectionId = tracy::GetProfiler().ConnectionId();
    RuntimeAllocation::DevelopmentToolAllocationScope allocationScope(
        RuntimeAllocation::DevelopmentToolAllocationOwner::Tracy );
    ___tracy_emit_memory_alloc_callstack_named( pointer, size, HEAVY_CALLSTACK_DEPTH, 0, RUNTIME_HEAP_NAME );
    return connectionId;
}

void TracyClientOwner::RecordFree( const void* pointer, uint64_t connectionId ) noexcept
{
    if ( !pointer || connectionId == 0u || !IsHeavyCaptureConnected() ||
         tracy::GetProfiler().ConnectionId() != connectionId )
    {
        return;
    }

    RuntimeAllocation::DevelopmentToolAllocationScope allocationScope(
        RuntimeAllocation::DevelopmentToolAllocationOwner::Tracy );
    ___tracy_emit_memory_free_callstack_named( pointer, HEAVY_CALLSTACK_DEPTH, 0, RUNTIME_HEAP_NAME );
}
} // namespace SkullbonezCore::Runtime::DevelopmentTools
