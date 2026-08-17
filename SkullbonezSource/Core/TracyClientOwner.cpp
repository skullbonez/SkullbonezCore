/*
File: SkullbonezSource/Core/TracyClientOwner.cpp
Purpose:
  Owns Tracy lifecycle, owner zones, plots, capture mode, and status.

Summary:
  The process composition root starts the manually managed on-demand client
  before engine workers exist when profiling was requested at launch, or starts
  Standard capture at a cold editor boundary and recreates those workers. It
  shuts Tracy down after joining them. A fixed source-location registry maps
  the engine profiler's established owner paths into Tracy. Standard mode
  records zones and capacity plots; explicit heavy mode also enables the Core
  allocation owner to emit global C++ allocation events.

Glossary:
  On-demand client: Tracy mode that records only while an external viewer is
    connected instead of retaining an unbounded pre-connection history.
  Connection snapshot: Direct read of Tracy's existing atomic connection flag;
    it does not probe the network, launch a process, or allocate.
  Capture configuration: Tracy starts disabled unless SKORE_TRACY_MODE selects
    `standard` or `heavy`, or the ImGui action requests Standard capture;
    standard is the comparable low-overhead mode.
  Backing map: Page-aligned virtual-memory range from which Tracy's private
    rpmalloc instance serves its transport queues and other client storage.
  LZ4 owner hooks: Compile-time allocator callbacks used by Tracy's embedded
    compression stream instead of its default direct CRT allocation path.

Invariants:
  - Startup and shutdown are idempotent at the engine boundary.
  - Frame marks are ignored until manual startup has completed.
  - The 32-byte worker-name buffer bounds all formatted thread labels.
  - Source locations borrow static engine marker strings and never outlive the
    process-owned client.
  - The Core allocation owner emits heavy frees only to the connection that
    observed the matching allocation; this owner publishes the active mode.
  - Every rpmalloc backing map reserves bytes in the named Tracy owner before
    VirtualAlloc, and every LZ4 stream allocation enters that same owner; there
    is no untracked system- or CRT-allocation fallback.
  - Main/render/replay/prediction/IO share one lane in the current topology;
    the main thread name states that fact rather than inventing fake threads.

Related:
  - SkullbonezSource/Core/TracyClientOwner.h
  - SkullbonezSource/Core/PlatformWin32.h
  - SkullbonezSource/Runtime/App/Init.cpp
  - SkullbonezSource/Runtime/App/RunFrame.cpp
  - SkullbonezSource/Core/WorkerPool.cpp
*/
#include "TracyClientOwner.h"

#include "Allocation/DevelopmentToolAllocation.h"
#include "FatalError.h"
#include "PlatformWin32.h"

#include <client/tracy_rpmalloc.hpp>
#include <tracy/Tracy.hpp>
#include <tracy/TracyC.h>

#include <atomic>
#include <cstdio>
#include <cstring>
#include <limits>
#include <mutex>

namespace CoreAllocation = SkullbonezCore::Core::Allocation;

#if defined( LZ4_USER_MEMORY_FUNCTIONS )
// Why: Tracy compiles its private LZ4 implementation into TracyClient.cpp. The
// vendor's opt-in hooks keep that compression stream inside the same named,
// capped owner as its rpmalloc maps without modifying the pinned vendor source.
void* LZ4_malloc( std::size_t size )
{
    return CoreAllocation::AllocateDevelopmentToolMemory( CoreAllocation::DevelopmentToolAllocationOwner::Tracy, size );
}

void* LZ4_calloc( std::size_t count, std::size_t size )
{
    if ( count != 0u && size > std::numeric_limits<std::size_t>::max() / count )
    {
        return nullptr;
    }

    const std::size_t byteCount = count * size;
    void* memory = LZ4_malloc( byteCount );

    if ( memory )
    {
        std::memset( memory, 0, byteCount );
    }

    return memory;
}

void LZ4_free( void* pointer )
{
    CoreAllocation::FreeDevelopmentToolMemory( CoreAllocation::DevelopmentToolAllocationOwner::Tracy, pointer );
}
#endif

namespace
{
constexpr uint32_t MAX_OWNER_SOURCE_LOCATIONS = 256u;
constexpr int HEAVY_CALLSTACK_DEPTH = 16;
constexpr const char* OWNER_ZONE_SOURCE = "SkullbonezCore owner boundary";
constexpr const char* OWNER_ZONE_FUNCTION = "Engine owner interval";
constexpr const char* CAPTURE_MODE_ENVIRONMENT = "SKORE_TRACY_MODE";
constexpr const char* STANDARD_MODE_VALUE = "standard";
constexpr const char* HEAVY_MODE_VALUE = "heavy";
constexpr std::size_t TRACY_BACKING_ALIGNMENT = 64u * 1024u;

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

std::atomic<bool> g_tracyInitialized { false };
std::atomic<bool> g_tracyHeavyMode { false };
OwnerSourceLocation g_ownerSourceLocations[MAX_OWNER_SOURCE_LOCATIONS] = {};
std::atomic<uint32_t> g_ownerSourceLocationCount { 0u };
std::mutex g_ownerSourceLocationMutex;

RequestedCaptureMode ReadRequestedCaptureMode() noexcept
{
    char value[16] = {};
    const std::size_t copied = SkullbonezCore::Core::Platform::ReadEnvironmentVariable( CAPTURE_MODE_ENVIRONMENT, value,
                                                                                        sizeof( value ) );

    if ( copied == 0u || copied >= sizeof( value ) )
    {
        return RequestedCaptureMode::Off;
    }

    if ( SkullbonezCore::Core::Platform::CompareCaseInsensitive( value, STANDARD_MODE_VALUE ) == 0 )
    {
        return RequestedCaptureMode::Standard;
    }

    if ( SkullbonezCore::Core::Platform::CompareCaseInsensitive( value, HEAVY_MODE_VALUE ) == 0 )
    {
        return RequestedCaptureMode::Heavy;
    }

    return RequestedCaptureMode::Off;
}

bool IsInitialized() noexcept
{
    return g_tracyInitialized.load( std::memory_order_acquire );
}

void* MapTracyBackingMemory( std::size_t size, std::size_t* offset )
{
    if ( offset )
    {
        *offset = 0u;
    }

    if ( !CoreAllocation::TryAccountDevelopmentToolBackingMemory( CoreAllocation::DevelopmentToolAllocationOwner::Tracy,
                                                                  size ) )
    {
        CoreAllocation::DevelopmentToolAllocationStats stats;
        CoreAllocation::CopyDevelopmentToolAllocationStats( CoreAllocation::DevelopmentToolAllocationOwner::Tracy, stats );

        // Lane F: continuing would either exceed the owner-approved cap or
        // tempt the vendor allocator to fall back outside engine accounting.
        SB_FATAL( "DevelopmentTools/Tracy",
                  "rpmalloc backing cap exhausted: request=%llu active=%llu high_water=%llu cap=%d",
                  static_cast<unsigned long long>( size ), static_cast<unsigned long long>( stats.activeBytes ),
                  static_cast<unsigned long long>( stats.highWaterBytes ), stats.hardCapBytes );
    }

    void* address = VirtualAlloc( nullptr, size, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE );

    if ( !address )
    {
        CoreAllocation::ReleaseDevelopmentToolBackingMemory( CoreAllocation::DevelopmentToolAllocationOwner::Tracy, size );

        SB_FATAL( "DevelopmentTools/Tracy", "VirtualAlloc failed for rpmalloc backing: request=%llu error=%lu",
                  static_cast<unsigned long long>( size ), static_cast<unsigned long>( GetLastError() ) );
    }

    if ( ( reinterpret_cast<std::uintptr_t>( address ) & ( TRACY_BACKING_ALIGNMENT - 1u ) ) != 0u )
    {
        VirtualFree( address, 0u, MEM_RELEASE );
        CoreAllocation::ReleaseDevelopmentToolBackingMemory( CoreAllocation::DevelopmentToolAllocationOwner::Tracy, size );

        SB_FATAL( "DevelopmentTools/Tracy", "VirtualAlloc returned a non-64-KiB-aligned rpmalloc range." );
    }

    return address;
}

void UnmapTracyBackingMemory( void* address, std::size_t size, std::size_t offset, std::size_t release )
{
    // Why: rpmalloc uses release==0 as an optional decommit hint. Retaining the
    // committed pages keeps reuse valid and conservatively charges the complete
    // live backing range until its one matching full release.
    if ( release == 0u )
    {
        return;
    }

    if ( !address || offset != 0u || release < size )
    {
        SB_FATAL( "DevelopmentTools/Tracy",
                  "Invalid rpmalloc backing release: address=%p size=%llu offset=%llu release=%llu", address,
                  static_cast<unsigned long long>( size ), static_cast<unsigned long long>( offset ),
                  static_cast<unsigned long long>( release ) );
    }

    if ( !VirtualFree( address, 0u, MEM_RELEASE ) )
    {
        SB_FATAL( "DevelopmentTools/Tracy", "VirtualFree failed for rpmalloc backing: release=%llu error=%lu",
                  static_cast<unsigned long long>( release ), static_cast<unsigned long>( GetLastError() ) );
    }

    CoreAllocation::ReleaseDevelopmentToolBackingMemory( CoreAllocation::DevelopmentToolAllocationOwner::Tracy, release );
}

void ConfigureTracyBackingAllocator()
{
    tracy::rpmalloc_config_t config = {};
    config.memory_map = MapTracyBackingMemory;
    config.memory_unmap = UnmapTracyBackingMemory;
    config.span_size = TRACY_BACKING_ALIGNMENT;

    if ( tracy::rpmalloc_initialize_config( &config ) != 0 )
    {
        SB_FATAL( "DevelopmentTools/Tracy", "Failed to initialize the engine-accounted rpmalloc configuration." );
    }

    const tracy::rpmalloc_config_t* activeConfig = tracy::rpmalloc_config();

    if ( !activeConfig || activeConfig->memory_map != MapTracyBackingMemory ||
         activeConfig->memory_unmap != UnmapTracyBackingMemory )
    {
        // Hazard: this means Tracy touched rpmalloc before the engine owner and
        // installed its direct OS mapper, recreating the untracked allocation path.
        SB_FATAL( "DevelopmentTools/Tracy", "rpmalloc initialized before the engine backing-map owner." );
    }
}
} // namespace

namespace SkullbonezCore::Core::DevelopmentTools
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
        fprintf( stdout, "[tracy] Client disabled. Set SKORE_TRACY_MODE=standard or heavy before launch to capture.\n" );

        fflush( stdout );
        return;
    }

    ConfigureTracyBackingAllocator();
    CoreAllocation::DevelopmentToolAllocationScope allocationScope( CoreAllocation::DevelopmentToolAllocationOwner::Tracy );
    tracy::StartupProfiler();
    m_started = tracy::IsProfilerStarted();
    const bool heavyMode = m_started && captureMode == RequestedCaptureMode::Heavy;
    g_tracyHeavyMode.store( heavyMode, std::memory_order_release );
    g_tracyInitialized.store( m_started, std::memory_order_release );
    CoreAllocation::SetTracyAllocationTracingEnabled( heavyMode );

    if ( !m_started )
    {
        return;
    }

    // Concept: current runtime topology executes render, replay/prediction
    // coordination, and cold IO on the main thread. A composite label makes
    // that ownership visible without pretending those roles have private lanes.
    tracy::SetThreadName( "Skore Main + Render + Replay + IO" );
    TracySetProgramName( "SkullbonezCore" );
    CoreAllocation::DevelopmentToolAllocationStats tracyAllocationStats;
    CoreAllocation::CopyDevelopmentToolAllocationStats( CoreAllocation::DevelopmentToolAllocationOwner::Tracy,
                                                        tracyAllocationStats );

    fprintf( stdout,
             "[tracy] Manual on-demand client started. viewer=waiting capture=%s callstacks=%s allocations=%s "
             "owner_active=%llu owner_high_water=%llu owner_cap=%d\n",
             heavyMode ? "heavy" : "standard", heavyMode ? "depth-16" : "off", heavyMode ? "global-cpp-heap" : "off",
             static_cast<unsigned long long>( tracyAllocationStats.activeBytes ),
             static_cast<unsigned long long>( tracyAllocationStats.highWaterBytes ), tracyAllocationStats.hardCapBytes );

    fflush( stdout );
}

bool TracyClientOwner::StartStandardCapture()
{
    if ( m_started )
    {
        return true;
    }

    // Why: Heavy capture observes global allocation lifetimes and therefore
    // remains a pre-launch choice. Standard capture has no allocation events,
    // so Tracy's manual-lifetime client can start safely at this cold editor
    // boundary without invalidating already-live engine allocations.
    if ( !SetEnvironmentVariableA( CAPTURE_MODE_ENVIRONMENT, STANDARD_MODE_VALUE ) )
    {
        fprintf( stderr, "[tracy] Standard capture request failed to set %s (error=%lu).\n", CAPTURE_MODE_ENVIRONMENT,
                 static_cast<unsigned long>( GetLastError() ) );

        fflush( stderr );
        return false;
    }

    Start();
    return m_started;
}

void TracyClientOwner::Shutdown() noexcept
{
    if ( !m_started )
    {
        return;
    }

    CoreAllocation::DevelopmentToolAllocationScope allocationScope( CoreAllocation::DevelopmentToolAllocationOwner::Tracy );

    // Lifetime: publish the stopped state before freeing Tracy's process data;
    // UI snapshots and marker seams therefore stop reaching the vendor first.
    g_tracyInitialized.store( false, std::memory_order_release );
    g_tracyHeavyMode.store( false, std::memory_order_release );
    CoreAllocation::SetTracyAllocationTracingEnabled( false );
    tracy::ShutdownProfiler();
    m_started = false;
    CoreAllocation::DevelopmentToolAllocationStats tracyAllocationStats;
    CoreAllocation::CopyDevelopmentToolAllocationStats( CoreAllocation::DevelopmentToolAllocationOwner::Tracy,
                                                        tracyAllocationStats );

    fprintf( stdout,
             "[tracy] Client stopped after engine worker shutdown. owner_active=%llu owner_high_water=%llu "
             "owner_cap=%d\n",
             static_cast<unsigned long long>( tracyAllocationStats.activeBytes ),
             static_cast<unsigned long long>( tracyAllocationStats.highWaterBytes ), tracyAllocationStats.hardCapBytes );

    fflush( stdout );
}

TracyClientStatus TracyClientOwner::CopyStatus() noexcept
{
    const bool initialized = g_tracyInitialized.load( std::memory_order_acquire );

    // Why: IsConnected is an atomic state read owned by the client. The editor
    // never scans processes, opens sockets, or constructs a string per frame.
    return { true, initialized, initialized && TracyIsConnected,
             initialized && g_tracyHeavyMode.load( std::memory_order_acquire ) };
}

void TracyClientOwner::MarkSubmittedFrame() noexcept
{
    if ( !g_tracyInitialized.load( std::memory_order_acquire ) )
    {
        return;
    }

    CoreAllocation::DevelopmentToolAllocationScope allocationScope( CoreAllocation::DevelopmentToolAllocationOwner::Tracy );
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
    CoreAllocation::DevelopmentToolAllocationScope allocationScope( CoreAllocation::DevelopmentToolAllocationOwner::Tracy );

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

    CoreAllocation::DevelopmentToolAllocationScope allocationScope( CoreAllocation::DevelopmentToolAllocationOwner::Tracy );
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

    CoreAllocation::DevelopmentToolAllocationScope allocationScope( CoreAllocation::DevelopmentToolAllocationOwner::Tracy );
    ___tracy_emit_zone_end( { token.id, token.active } );
}

void TracyClientOwner::PublishPlot( const char* name, double value ) noexcept
{
    if ( !IsInitialized() || !name || !TracyIsConnected )
    {
        return;
    }

    CoreAllocation::DevelopmentToolAllocationScope allocationScope( CoreAllocation::DevelopmentToolAllocationOwner::Tracy );
    ___tracy_emit_plot( name, value );
}

void TracyClientOwner::PublishDevelopmentAllocationPlots() noexcept
{
    CoreAllocation::DevelopmentToolAllocationStats imguiStats;
    CoreAllocation::DevelopmentToolAllocationStats tracyStats;

    if ( CoreAllocation::CopyDevelopmentToolAllocationStats( CoreAllocation::DevelopmentToolAllocationOwner::DearImGui,
                                                             imguiStats ) )
    {
        PublishPlot( "Counter/DevelopmentTools/ImGuiActiveBytes", static_cast<double>( imguiStats.activeBytes ) );
        PublishPlot( "Counter/DevelopmentTools/ImGuiHighWaterBytes", static_cast<double>( imguiStats.highWaterBytes ) );
    }

    if ( CoreAllocation::CopyDevelopmentToolAllocationStats( CoreAllocation::DevelopmentToolAllocationOwner::Tracy,
                                                             tracyStats ) )
    {
        PublishPlot( "Counter/DevelopmentTools/TracyActiveBytes", static_cast<double>( tracyStats.activeBytes ) );
        PublishPlot( "Counter/DevelopmentTools/TracyHighWaterBytes", static_cast<double>( tracyStats.highWaterBytes ) );
    }
}

} // namespace SkullbonezCore::Core::DevelopmentTools
