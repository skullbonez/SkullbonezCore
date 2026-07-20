/*
File: SkullbonezSource/Rendering/ProfilerImplementation.cpp
Purpose:
  Records hierarchical CPU/GPU timing markers for runtime diagnostics.

Summary:
  The public profiler contract is Core infrastructure, while its implementation
  integrates renderer timestamps, platform GPU ranges, text overlays, worker
  samples, and Tracy plots. Locating that implementation in Rendering keeps the
  Core API free of upward implementation includes.

Glossary:
  Render diagnostics capability: Narrow renderer interface used here for GPU
    timers and platform GPU marker events without depending on the wide backend
    facade.
  Warmup frame: Completed frame intentionally excluded from profiler stats and
    perf CSV rows while a scene/pass settles.
  External owner zone: Tracy interval that mirrors an established engine
    profiler path and nesting edge.
  Lane F: Fatal invariant path for should-never-happen engine state.

Invariants:
  - Marker identity is the full path plus hash; hash collisions are Lane F
    failures because merged timings would corrupt diagnostics.
  - Begin/end nesting must balance before frame end for both CPU and GPU marker
    rings.

Related:
  - SkullbonezSource/Core/Profiler.h
  - SkullbonezSource/Rendering/IRenderDiagnostics.h
  - SkullbonezSource/Rendering/Text.h
  - Agentic/Reference/runtime-reference.md
  - Agentic/Reference/comment-style-guide.md
*/
#include "../Core/Profiler.h"
#include "../Core/FatalError.h"
#include "IRenderDiagnostics.h"
#include "../Core/TracyClientOwner.h"
#include "../Core/WorkerPool.h"

#include <cstring>


using namespace SkullbonezCore::Core;
using namespace SkullbonezCore::Rendering;


#if defined( SKULLBONEZ_PROFILE_ENABLED )

#include <algorithm>
#include <cfloat>
#include <cstdio>
#include <mutex>
#include "../Core/PlatformProfiler.h"
#include "Text.h"


namespace
{
constexpr int64_t TICKS_PER_AVG_REFRESH_MS = 500;

int CountSlashes( const char* s )
{
    int n = 0;
    for ( const char* p = s; *p; ++p )
    {
        if ( *p == '/' )
        {
            ++n;
        }
    }
    return n;
}

const char* FindLeafName( const char* fullPath )
{
    const char* leaf = fullPath;
    for ( const char* p = fullPath; *p; ++p )
    {
        if ( *p == '/' )
        {
            leaf = p + 1;
        }
    }
    return leaf;
}
} // namespace

Profiler::Profiler()
    : m_markerCount( 0 ), m_counterCount( 0 ), m_lastPerfCSVColumnCount( -1 ), m_workerCoreSampleCount( 0 ),
      m_stackTop( 0 ), m_qpcFrequency( 0 ), m_frameStartTicks( 0 ), m_lastAvgTicks( 0 ), m_inFrame( false ),
      m_warmupFrames( WARMUP_FRAMES + 1 ), m_resetPending( false ), m_nextColorIndex( 0 ),
      m_renderDiagnostics( nullptr )
{
    LARGE_INTEGER f;
    if ( QueryPerformanceFrequency( &f ) )
    {
        m_qpcFrequency = f.QuadPart;
    }
    else
    {
        m_qpcFrequency = 1; // avoid division by zero; timings will be garbage but won't crash
    }
    std::memset( m_markers, 0, sizeof( m_markers ) );
    std::memset( m_counters, 0, sizeof( m_counters ) );
    std::memset( m_workerCoreAccumulators, 0, sizeof( m_workerCoreAccumulators ) );
    std::memset( m_workerCoreAverageWindows, 0, sizeof( m_workerCoreAverageWindows ) );
    std::memset( m_workerCoreSamples, 0, sizeof( m_workerCoreSamples ) );
    std::memset( m_stackIndices, 0, sizeof( m_stackIndices ) );
    std::memset( m_platformProfilerCpuOpen, 0, sizeof( m_platformProfilerCpuOpen ) );
    std::memset( m_platformProfilerGpuRecordOpen, 0, sizeof( m_platformProfilerGpuRecordOpen ) );
    std::memset( m_platformProfilerGpuEventOpen, 0, sizeof( m_platformProfilerGpuEventOpen ) );
    std::memset( m_tracyZoneIds, 0, sizeof( m_tracyZoneIds ) );
    std::memset( m_tracyZoneActive, 0, sizeof( m_tracyZoneActive ) );
    std::memset( m_tracyZoneConnectionIds, 0, sizeof( m_tracyZoneConnectionIds ) );
}


int Profiler::FindOrRegisterCounter( const char* fullPath, uint32_t hash )
{
    // Hazard: counter columns are durable measurement-ledger identities. A
    // collision must fail instead of silently combining unrelated units.
    for ( int i = 0; i < m_counterCount; ++i )
    {
        if ( m_counters[i].hash != hash )
        {
            continue;
        }
        if ( std::strcmp( m_counters[i].name, fullPath ) != 0 )
        {
            AbortMismatch( "FNV-1a hash collision between profiler counters", fullPath );
        }
        return i;
    }

    if ( m_counterCount >= MAX_COUNTERS )
    {
        AbortMismatch( "MAX_COUNTERS exceeded", fullPath );
    }

    Counter& counter = m_counters[m_counterCount];
    counter.name = fullPath;
    counter.hash = hash;
    counter.valueThisFrame = 0.0;
    counter.lastFrameValue = 0.0;
    counter.writtenThisFrame = false;
    return m_counterCount++;
}


void Profiler::AbortMismatch( const char* msg, const char* details ) const
{
    const char* safeMessage = msg ? msg : "profiler invariant failed";
    const char* safeDetails = details ? details : "";
    char buf[512];
    if ( safeDetails[0] != '\0' )
    {
        _snprintf_s( buf, sizeof( buf ), _TRUNCATE, "PROFILER: %s [%s]\n", safeMessage, safeDetails );
    }
    else
    {
        _snprintf_s( buf, sizeof( buf ), _TRUNCATE, "PROFILER: %s\n", safeMessage );
    }
    OutputDebugStringA( buf );
    // Hazard: marker hash collisions and begin/end mismatches corrupt the
    // profiler's nesting stack. Treat them as Lane F engine invariants so the
    // fatal path owns stderr logging, event-log flushing, and termination.
    if ( safeDetails[0] != '\0' )
    {
        SB_FATAL( "Core/Profiler", "%s [%s]", safeMessage, safeDetails );
    }
    SB_FATAL( "Core/Profiler", "%s", safeMessage );
}


int Profiler::FindOrRegister( const char* fullPath, uint32_t hash )
{
    // Hazard: profiler overlays and CSVs rely on stable marker identity. A
    // collision is surfaced immediately instead of silently merging samples.
    for ( int i = 0; i < m_markerCount; ++i )
    {
        if ( m_markers[i].hash == hash )
        {
            // Hash collision guard: full-path strcmp must match
            if ( std::strcmp( m_markers[i].name, fullPath ) != 0 )
            {
                AbortMismatch( "FNV-1a hash collision between markers", fullPath );
            }
            return i;
        }
    }

    if ( m_markerCount >= MAX_MARKERS )
    {
        AbortMismatch( "MAX_MARKERS exceeded", fullPath );
    }

    Marker& m = m_markers[m_markerCount];
    m.name = fullPath;
    m.leafName = FindLeafName( fullPath );
    m.hash = hash;
    // Concept: the established engine marker is the source of truth. Tracy
    // receives the exact same owner path and therefore remains comparable to
    // platform-profiler and perf-CSV evidence.
    m.tracySourceLocationHandle = SKORE_TRACY_REGISTER_OWNER_ZONE( fullPath, hash );
    m.depth = CountSlashes( fullPath );
    m.colorIndex = m_nextColorIndex;
    m_nextColorIndex = ( m_nextColorIndex + 1 ) % BAR_PALETTE_SIZE;
    m.openCount = 0;
    m.openStartTicks = 0;
    m.accumSecondsThisFrame = 0.0;
    m.firstStartSecondsThisFrame = 0.0;
    m.lastEndSecondsThisFrame = 0.0;
    m.spanWrittenThisFrame = false;
    m.ringFilled = 0;
    m.ringHead = 0;
    m.lastFrameMs = 0.0f;
    m.lastSelfMs = 0.0f;
    m.lastFrameStartMs = 0.0f;
    m.lastFrameEndMs = 0.0f;
    m.avgMs = 0.0f;
    m.selfAvgMs = 0.0f;
    m.p50Ms = 0.0f;
    m.p99Ms = 0.0f;
    m.p99_9Ms = 0.0f;
    m.minMs = FLT_MAX;
    m.maxMs = 0.0f;
    m.selfRingFilled = 0;
    m.selfRingHead = 0;
    std::memset( m.selfRingMs, 0, sizeof( m.selfRingMs ) );

    // GPU state initialised to inactive
    m.hasGpu = false;
    m.gpuWrittenThisFrame = false;
    m.gpuLastFrameMs = 0.0f;
    m.gpuAvgMs = 0.0f;
    m.gpuRingFilled = 0;
    m.gpuRingHead = 0;
    std::memset( m.gpuRingMs, 0, sizeof( m.gpuRingMs ) );

    // Resolve parentIndex by stripping last '/' segment and looking up that prefix
    if ( m.depth == 0 )
    {
        m.parentIndex = -1;
    }
    else
    {
        // Parent names are the marker path before the final slash.
        char parentPath[256];
        const char* lastSlash = nullptr;
        for ( const char* p = fullPath; *p; ++p )
        {
            if ( *p == '/' )
            {
                lastSlash = p;
            }
        }
        size_t plen = static_cast<size_t>( lastSlash - fullPath );
        if ( plen >= sizeof( parentPath ) )
        {
            plen = sizeof( parentPath ) - 1;
        }
        std::memcpy( parentPath, fullPath, plen );
        parentPath[plen] = '\0';

        uint32_t pHash = HashStr( parentPath );
        int pIdx = -1;
        for ( int i = 0; i < m_markerCount; ++i )
        {
            if ( m_markers[i].hash == pHash && std::strcmp( m_markers[i].name, parentPath ) == 0 )
            {
                pIdx = i;
                break;
            }
        }
        // Parent might not be registered yet (e.g. first time we hit "Render/Skybox" before "Render"
        // ever opens). That's fine — overlay falls back to indenting by depth.
        m.parentIndex = pIdx;
    }

    return m_markerCount++;
}


void Profiler::Begin( const char* fullPath, uint32_t hash )
{
    if ( SkullbonezCore::Threading::WorkerPool::IsCurrentThreadWorker() )
    {
        return;
    }
    BeginInternal( fullPath, hash, true );
}


void Profiler::End( const char* fullPath, uint32_t hash )
{
    if ( SkullbonezCore::Threading::WorkerPool::IsCurrentThreadWorker() )
    {
        return;
    }
    EndInternal( fullPath, hash, true );
}


void Profiler::RecordWorkerSample( const char* fullPath,
                                   uint32_t hash,
                                   int workerIndex,
                                   int64_t startTicks,
                                   int64_t endTicks )
{
    if ( !m_inFrame || workerIndex < 0 || workerIndex >= MAX_WORKER_CORES )
    {
        return;
    }
    if ( endTicks < startTicks )
    {
        endTicks = startTicks;
    }

    std::lock_guard<std::mutex> lock( m_workerSampleMutex );
    int idx = FindOrRegister( fullPath, hash );
    Marker& marker = m_markers[idx];
    const double startSeconds =
        static_cast<double>( startTicks - m_frameStartTicks ) / static_cast<double>( m_qpcFrequency );
    const double endSeconds =
        static_cast<double>( endTicks - m_frameStartTicks ) / static_cast<double>( m_qpcFrequency );
    const double durationSeconds = static_cast<double>( endTicks - startTicks ) / static_cast<double>( m_qpcFrequency );
    marker.accumSecondsThisFrame += durationSeconds;
    if ( !marker.spanWrittenThisFrame )
    {
        marker.firstStartSecondsThisFrame = startSeconds;
        marker.lastEndSecondsThisFrame = endSeconds;
        marker.spanWrittenThisFrame = true;
    }
    else
    {
        marker.firstStartSecondsThisFrame = (std::min)( marker.firstStartSecondsThisFrame, startSeconds );
        marker.lastEndSecondsThisFrame = (std::max)( marker.lastEndSecondsThisFrame, endSeconds );
    }

    WorkerCoreAccumulator& worker = m_workerCoreAccumulators[workerIndex];
    ++worker.jobCount;
    worker.accumSecondsThisFrame += durationSeconds;
    if ( !worker.spanWrittenThisFrame )
    {
        worker.firstStartSecondsThisFrame = startSeconds;
        worker.lastEndSecondsThisFrame = endSeconds;
        worker.spanWrittenThisFrame = true;
    }
    else
    {
        worker.firstStartSecondsThisFrame = (std::min)( worker.firstStartSecondsThisFrame, startSeconds );
        worker.lastEndSecondsThisFrame = (std::max)( worker.lastEndSecondsThisFrame, endSeconds );
    }
}


void Profiler::RecordCounter( const char* fullPath, uint32_t hash, double value )
{
    if ( SkullbonezCore::Threading::WorkerPool::IsCurrentThreadWorker() )
    {
        return;
    }
    if ( !m_inFrame )
    {
        AbortMismatch( "PROFILE_COUNTER called outside frame", fullPath );
    }

    Counter& counter = m_counters[FindOrRegisterCounter( fullPath, hash )];
    counter.valueThisFrame = value;
    counter.writtenThisFrame = true;
}


WorkerProfilerScope::WorkerProfilerScope( Profiler* profiler, const char* fullPath, uint32_t hash )
    : m_profiler( profiler ), m_fullPath( fullPath ), m_hash( hash ),
      m_workerIndex( SkullbonezCore::Threading::WorkerPool::CurrentWorkerIndex() ), m_startTicks( 0 ),
      m_platformProfilerOpen( false ), m_tracySourceLocationHandle( 0u ), m_tracyZoneId( 0u ), m_tracyZoneActive( 0 ),
      m_tracyZoneConnectionId( 0u )
{
    if ( m_workerIndex < 0 )
    {
        return;
    }
    LARGE_INTEGER t;
    QueryPerformanceCounter( &t );
    m_startTicks = t.QuadPart;
#if defined( TRACY_ENABLE )
    m_tracySourceLocationHandle = SKORE_TRACY_REGISTER_OWNER_ZONE( fullPath, hash );
    const auto tracyToken = SKORE_TRACY_BEGIN_OWNER_ZONE( m_tracySourceLocationHandle );
    m_tracyZoneId = tracyToken.id;
    m_tracyZoneActive = tracyToken.active;
    m_tracyZoneConnectionId = tracyToken.connectionId;
#endif
    if ( PlatformProfiler::AreDetailedRangesEnabled() )
    {
        char markerName[PlatformProfiler::MAX_DECORATED_MARKER_NAME_CHARS];
        PlatformProfiler::CpuBegin(
            PlatformProfiler::DecorateMarkerName( m_fullPath, "_Worker", markerName, sizeof( markerName ) ),
            m_hash );
        m_platformProfilerOpen = true;
    }
}


WorkerProfilerScope::~WorkerProfilerScope()
{
    if ( m_workerIndex < 0 )
    {
        return;
    }
    LARGE_INTEGER t;
    QueryPerformanceCounter( &t );
#if defined( TRACY_ENABLE )
    const SkullbonezCore::Core::DevelopmentTools::TracyZoneToken tracyToken{ m_tracyZoneId,
                                                                             m_tracyZoneActive,
                                                                             m_tracyZoneConnectionId };
    SKORE_TRACY_END_OWNER_ZONE( tracyToken );
    m_tracyZoneId = 0u;
    m_tracyZoneActive = 0;
    m_tracyZoneConnectionId = 0u;
#endif
    if ( m_platformProfilerOpen )
    {
        PlatformProfiler::CpuEnd();
        m_platformProfilerOpen = false;
    }
    if ( m_profiler )
    {
        m_profiler->RecordWorkerSample( m_fullPath, m_hash, m_workerIndex, m_startTicks, t.QuadPart );
    }
}


void Profiler::BeginInternal( const char* fullPath, uint32_t hash, bool emitCpuPlatformProfiler )
{
    if ( !m_inFrame )
    {
        AbortMismatch( "PROFILE_BEGIN called outside frame", fullPath );
    }
    if ( m_stackTop >= MAX_DEPTH )
    {
        AbortMismatch( "MAX_DEPTH exceeded", fullPath );
    }

    int idx = FindOrRegister( fullPath, hash );
    Marker& m = m_markers[idx];

    if ( m.openCount != 0 )
    {
        AbortMismatch( "PROFILE_BEGIN on already-open marker (no recursion supported)", fullPath );
    }

    LARGE_INTEGER t;
    QueryPerformanceCounter( &t );
    m.openStartTicks = t.QuadPart;
    const double startSeconds =
        static_cast<double>( t.QuadPart - m_frameStartTicks ) / static_cast<double>( m_qpcFrequency );
    if ( !m.spanWrittenThisFrame )
    {
        m.firstStartSecondsThisFrame = startSeconds;
        m.spanWrittenThisFrame = true;
    }
    m.openCount = 1;
    const int stackSlot = m_stackTop++;
    m_stackIndices[stackSlot] = idx;
    m_platformProfilerCpuOpen[stackSlot] = false;
    m_platformProfilerGpuRecordOpen[stackSlot] = false;
    m_platformProfilerGpuEventOpen[stackSlot] = false;
    m_tracyZoneIds[stackSlot] = 0u;
    m_tracyZoneActive[stackSlot] = 0;
    m_tracyZoneConnectionIds[stackSlot] = 0u;
#if defined( TRACY_ENABLE )
    const auto tracyToken = SKORE_TRACY_BEGIN_OWNER_ZONE( m.tracySourceLocationHandle );
    m_tracyZoneIds[stackSlot] = tracyToken.id;
    m_tracyZoneActive[stackSlot] = tracyToken.active;
    m_tracyZoneConnectionIds[stackSlot] = tracyToken.connectionId;
#endif
    if ( emitCpuPlatformProfiler && PlatformProfiler::IsEnabled() )
    {
        PlatformProfiler::CpuBegin( fullPath, hash );
        m_platformProfilerCpuOpen[stackSlot] = true;
    }
}


void Profiler::EndInternal( const char* fullPath, uint32_t hash, bool emitCpuPlatformProfiler )
{
    if ( m_stackTop == 0 )
    {
        AbortMismatch( "PROFILE_END with empty stack", fullPath );
    }

    const int stackSlot = m_stackTop - 1;
    int topIdx = m_stackIndices[stackSlot];
    Marker& top = m_markers[topIdx];
    const bool cpuPlatformOpen = m_platformProfilerCpuOpen[stackSlot];
    m_platformProfilerCpuOpen[stackSlot] = false;
#if defined( TRACY_ENABLE )
    const uint32_t tracyZoneId = m_tracyZoneIds[stackSlot];
    const int32_t tracyZoneActive = m_tracyZoneActive[stackSlot];
    const uint64_t tracyZoneConnectionId = m_tracyZoneConnectionIds[stackSlot];
    m_tracyZoneIds[stackSlot] = 0u;
    m_tracyZoneActive[stackSlot] = 0;
    m_tracyZoneConnectionIds[stackSlot] = 0u;
#endif

    if ( top.hash != hash )
    {
        AbortMismatch( "PROFILE_BEGIN/END mismatch", top.name );
    }

    LARGE_INTEGER t;
    QueryPerformanceCounter( &t );
    int64_t delta = t.QuadPart - top.openStartTicks;
    if ( delta < 0 )
    {
        delta = 0;
    }
    top.accumSecondsThisFrame += static_cast<double>( delta ) / static_cast<double>( m_qpcFrequency );
    top.lastEndSecondsThisFrame =
        static_cast<double>( t.QuadPart - m_frameStartTicks ) / static_cast<double>( m_qpcFrequency );
    top.openCount = 0;
    --m_stackTop;
#if defined( TRACY_ENABLE )
    const SkullbonezCore::Core::DevelopmentTools::TracyZoneToken tracyToken{ tracyZoneId,
                                                                             tracyZoneActive,
                                                                             tracyZoneConnectionId };
    SKORE_TRACY_END_OWNER_ZONE( tracyToken );
#endif
    if ( emitCpuPlatformProfiler && cpuPlatformOpen )
    {
        PlatformProfiler::CpuEnd();
    }
}


void Profiler::GpuBegin( const char* fullPath, uint32_t hash )
{
    // Owner: Profiler with a startup-bound RenderDiagnostics borrow. Reason:
    // GPU markers share Profiler's nesting stack, but Core must not reopen the
    // renderer singleton while recording a scope. Deletion condition: if GPU
    // scopes become owned by render passes directly, this borrow can disappear
    // with the scope wrapper. Checker budget: ProfilerImplementation.cpp has
    // zero renderer-service global accesses.
    if ( SkullbonezCore::Threading::WorkerPool::IsCurrentThreadWorker() )
    {
        return;
    }
    BeginInternal( fullPath, hash, false );
    const int stackSlot = m_stackTop - 1;
    if ( PlatformProfiler::AreDetailedRangesEnabled() )
    {
        char markerName[PlatformProfiler::MAX_DECORATED_MARKER_NAME_CHARS];
        PlatformProfiler::CpuBegin(
            PlatformProfiler::DecorateMarkerName( fullPath, "_Record", markerName, sizeof( markerName ) ),
            hash );
        m_platformProfilerGpuRecordOpen[stackSlot] = true;
    }
    if ( PlatformProfiler::IsEnabled() && m_renderDiagnostics )
    {
        m_renderDiagnostics->PlatformProfilerGpuBegin( fullPath, hash );
        m_platformProfilerGpuEventOpen[stackSlot] = true;
    }
    BeginGpuTimerInternal( fullPath, hash );
}


void Profiler::GpuEnd( const char* fullPath, uint32_t hash )
{
    if ( SkullbonezCore::Threading::WorkerPool::IsCurrentThreadWorker() )
    {
        return;
    }
    const int stackSlot = m_stackTop > 0 ? m_stackTop - 1 : -1;
    const bool platformGpuOpen = stackSlot >= 0 ? m_platformProfilerGpuEventOpen[stackSlot] : false;
    const bool platformRecordOpen = stackSlot >= 0 ? m_platformProfilerGpuRecordOpen[stackSlot] : false;
    if ( stackSlot >= 0 )
    {
        m_platformProfilerGpuEventOpen[stackSlot] = false;
        m_platformProfilerGpuRecordOpen[stackSlot] = false;
    }
    EndGpuTimerInternal( fullPath, hash );
    if ( platformGpuOpen && m_renderDiagnostics )
    {
        m_renderDiagnostics->PlatformProfilerGpuEnd();
    }
    if ( platformRecordOpen )
    {
        PlatformProfiler::CpuEnd();
    }
    EndInternal( fullPath, hash, false );
}


void Profiler::BeginGpuTimerInternal( const char* fullPath, uint32_t hash )
{
    Rendering::IRenderDiagnostics* renderDiagnostics = m_renderDiagnostics;
    if ( renderDiagnostics && renderDiagnostics->GetCapabilities().supportsGpuTimers )
    {
        int idx = FindOrRegister( fullPath, hash );
        Marker& m = m_markers[idx];
        m.hasGpu = true;
        m.gpuWrittenThisFrame = true;
        renderDiagnostics->GpuTimerBegin( idx );
        return;
    }
}


void Profiler::EndGpuTimerInternal( const char* fullPath, uint32_t hash )
{
    Rendering::IRenderDiagnostics* renderDiagnostics = m_renderDiagnostics;
    if ( renderDiagnostics && renderDiagnostics->GetCapabilities().supportsGpuTimers )
    {
        int idx = FindOrRegister( fullPath, hash );
        Marker& m = m_markers[idx];
        if ( m.gpuWrittenThisFrame )
        {
            renderDiagnostics->GpuTimerEnd( idx );
        }
        return;
    }
}


void Profiler::ReadPendingGpuResults()
{
    Rendering::IRenderDiagnostics* renderDiagnostics = m_renderDiagnostics;
    if ( renderDiagnostics && renderDiagnostics->GetCapabilities().supportsGpuTimers )
    {
        for ( int i = 0; i < m_markerCount; ++i )
        {
            Marker& m = m_markers[i];
            if ( !m.hasGpu )
            {
                continue;
            }
            float ms = 0.0f;
            if ( renderDiagnostics->GpuTimerRead( i, ms ) )
            {
                m.gpuLastFrameMs = ms;
                m.gpuRingMs[m.gpuRingHead] = ms;
                m.gpuRingHead = ( m.gpuRingHead + 1 ) % RING_SIZE;
                if ( m.gpuRingFilled < RING_SIZE )
                {
                    ++m.gpuRingFilled;
                }
                // Recompute the running average here so the display column recovers
                // immediately after a ScheduleReset() (e.g. P-key mode switch) without
                // waiting for the 500ms moving-average refresh or the warmup window.
                double gsum = 0.0;
                for ( int k = 0; k < m.gpuRingFilled; ++k )
                {
                    gsum += m.gpuRingMs[k];
                }
                m.gpuAvgMs = static_cast<float>( gsum / m.gpuRingFilled );
            }
        }
    }
}


void Profiler::AdvanceGpuWriteCursors()
{
    for ( int i = 0; i < m_markerCount; ++i )
    {
        Marker& m = m_markers[i];
        if ( m.gpuWrittenThisFrame )
        {
            m.gpuWrittenThisFrame = false;
        }
    }
}


void Profiler::BindRenderDiagnostics( Rendering::IRenderDiagnostics* renderDiagnostics )
{
    if ( m_renderDiagnostics == renderDiagnostics )
    {
        return;
    }

    // Lifetime: invalidate against the old backend borrow before replacing it
    // so stale timestamp queries cannot be read after a renderer reset/teardown.
    InvalidateGpuQueries();
    m_renderDiagnostics = renderDiagnostics;
}


void Profiler::InvalidateGpuQueries()
{
    for ( int i = 0; i < m_markerCount; ++i )
    {
        Marker& m = m_markers[i];
        m.hasGpu = false;
        m.gpuWrittenThisFrame = false;
        m.gpuLastFrameMs = 0.0f;
        m.gpuAvgMs = 0.0f;
        m.gpuRingFilled = 0;
        m.gpuRingHead = 0;
        std::memset( m.gpuRingMs, 0, sizeof( m.gpuRingMs ) );
    }
    RestartWarmup();

    if ( m_renderDiagnostics )
    {
        m_renderDiagnostics->GpuTimerInvalidate();
    }
}


void Profiler::RestartWarmup()
{
    // Invariant: FrameBegin consumes one warmup tick before frame work runs, so
    // +1 keeps exactly WARMUP_FRAMES completed frames out of stats and CSV rows.
    m_warmupFrames = WARMUP_FRAMES + 1;
}


void Profiler::ScheduleReset()
{
    m_resetPending = true;
}


void Profiler::FrameBegin()
{
    if ( m_resetPending )
    {
        // Wipe GPU query state on all current markers, then clear the registry.
        // InvalidateGpuQueries also invalidates the bound renderer timers and resets warmup.
        InvalidateGpuQueries();
        m_markerCount = 0;
        m_counterCount = 0;
        m_lastPerfCSVColumnCount = -1;
        m_lastAvgTicks = 0;
        m_nextColorIndex = 0;
        m_resetPending = false;
        {
            std::lock_guard<std::mutex> lock( m_workerSampleMutex );
            std::memset( m_workerCoreAccumulators, 0, sizeof( m_workerCoreAccumulators ) );
            std::memset( m_workerCoreAverageWindows, 0, sizeof( m_workerCoreAverageWindows ) );
            std::memset( m_workerCoreSamples, 0, sizeof( m_workerCoreSamples ) );
            m_workerCoreSampleCount = 0;
        }
    }

    if ( m_inFrame )
    {
        AbortMismatch( "FrameBegin called twice without FrameEnd", nullptr );
    }
    if ( m_stackTop != 0 )
    {
        AbortMismatch( "FrameBegin with non-empty stack", nullptr );
    }
    m_inFrame = true;

    LARGE_INTEGER frameStart;
    QueryPerformanceCounter( &frameStart );
    m_frameStartTicks = frameStart.QuadPart;

    // Consume warmup budget at frame start so FrameEnd and WritePerfCSVRow see the same value
    if ( m_warmupFrames > 0 )
    {
        --m_warmupFrames;
    }

    // Read any pending GPU results from previous frames (non-blocking)
    ReadPendingGpuResults();

    for ( int i = 0; i < m_markerCount; ++i )
    {
        m_markers[i].accumSecondsThisFrame = 0.0;
        m_markers[i].firstStartSecondsThisFrame = 0.0;
        m_markers[i].lastEndSecondsThisFrame = 0.0;
        m_markers[i].spanWrittenThisFrame = false;
    }
    for ( int i = 0; i < m_counterCount; ++i )
    {
        m_counters[i].valueThisFrame = 0.0;
        m_counters[i].writtenThisFrame = false;
    }
    {
        std::lock_guard<std::mutex> lock( m_workerSampleMutex );
        std::memset( m_workerCoreAccumulators, 0, sizeof( m_workerCoreAccumulators ) );
    }

    // Implicit top-level "Frame" marker captures the entire frame total.
    static constexpr uint32_t kFrameHash = HashStr( "Frame" );
    Begin( "Frame", kFrameHash );
}


void Profiler::FrameEnd()
{
    if ( !m_inFrame )
    {
        AbortMismatch( "FrameEnd called without FrameBegin", nullptr );
    }

    // Close the implicit Frame marker — this also catches missing user PROFILE_END calls
    // because the stack top will not be "Frame" if anything is still open.
    static constexpr uint32_t kFrameHash = HashStr( "Frame" );
    End( "Frame", kFrameHash );

    for ( int i = 0; i < m_counterCount; ++i )
    {
        Counter& counter = m_counters[i];
        counter.lastFrameValue = counter.writtenThisFrame ? counter.valueThisFrame : 0.0;
    }

    if ( m_stackTop != 0 )
    {
        AbortMismatch( "FrameEnd with open markers (missing PROFILE_END)",
                       m_markers[m_stackIndices[m_stackTop - 1]].name );
    }

    // Advance GPU write cursors for markers that recorded timestamps this frame
    AdvanceGpuWriteCursors();

    static constexpr uint32_t kVsyncHash = HashStr( "Frame/VsyncWait" );
    float vsyncMsThisFrame = 0.0f;
    for ( int i = 0; i < m_markerCount; ++i )
    {
        if ( m_markers[i].hash == kVsyncHash )
        {
            vsyncMsThisFrame = static_cast<float>( m_markers[i].accumSecondsThisFrame * 1000.0 );
            break;
        }
    }

    // Commit per-frame totals into ring buffer; compute p50 / p99
    // During warmup (m_warmupFrames > 0) we still update lastFrameMs for the live overlay
    // but skip ring buffer / min / max / percentile updates to exclude startup noise.
    static float scratch[RING_SIZE]; // single-threaded — safe to be static
    for ( int i = 0; i < m_markerCount; ++i )
    {
        Marker& m = m_markers[i];
        const bool isFrameMarker = m.hash == kFrameHash;
        float ms = static_cast<float>( m.accumSecondsThisFrame * 1000.0 );
        if ( isFrameMarker )
        {
            ms = (std::max)( 0.0f, ms - vsyncMsThisFrame );
        }
        m.lastFrameMs = ms;
        if ( m.spanWrittenThisFrame )
        {
            m.lastFrameStartMs = static_cast<float>( m.firstStartSecondsThisFrame * 1000.0 );
            m.lastFrameEndMs = static_cast<float>( m.lastEndSecondsThisFrame * 1000.0 );
            if ( isFrameMarker )
            {
                m.lastFrameEndMs = (std::max)( m.lastFrameStartMs, m.lastFrameEndMs - vsyncMsThisFrame );
            }
        }
        else
        {
            m.lastFrameStartMs = 0.0f;
            m.lastFrameEndMs = 0.0f;
        }
        if ( m_warmupFrames > 0 )
        {
            continue;
        }

        if ( ms < m.minMs )
        {
            m.minMs = ms;
        }
        if ( ms > m.maxMs )
        {
            m.maxMs = ms;
        }
        m.ringMs[m.ringHead] = ms;
        m.ringHead = ( m.ringHead + 1 ) % RING_SIZE;
        if ( m.ringFilled < RING_SIZE )
        {
            ++m.ringFilled;
        }

        // p50 / p99 / p99.9 / min / max from ring buffer (frame-accurate)
        int n = m.ringFilled;
        if ( n > 0 )
        {
            std::memcpy( scratch, m.ringMs, sizeof( float ) * static_cast<size_t>( n ) );
            int p50i = n / 2;
            int p99i = ( n * 99 ) / 100;
            int p999i = ( n * 999 ) / 1000;
            if ( p99i >= n )
            {
                p99i = n - 1;
            }
            if ( p999i >= n )
            {
                p999i = n - 1;
            }
            std::nth_element( scratch, scratch + p50i, scratch + n );
            m.p50Ms = scratch[p50i];
            std::nth_element( scratch, scratch + p99i, scratch + n );
            m.p99Ms = scratch[p99i];
            std::nth_element( scratch, scratch + p999i, scratch + n );
            m.p99_9Ms = scratch[p999i];
        }
    }

    // Direct self time explains parent rows whose visible children do not sum
    // to the parent total. Frame is already VSync-excluded, so skip VsyncWait
    // when accounting for Frame's direct children.
    for ( int i = 0; i < m_markerCount; ++i )
    {
        Marker& marker = m_markers[i];
        float directChildMs = 0.0f;
        for ( int childIndex = 0; childIndex < m_markerCount; ++childIndex )
        {
            const Marker& child = m_markers[childIndex];
            if ( child.parentIndex != i )
            {
                continue;
            }
            if ( marker.hash == kFrameHash && child.hash == kVsyncHash )
            {
                continue;
            }
            directChildMs += child.lastFrameMs;
        }

        marker.lastSelfMs = (std::max)( 0.0f, marker.lastFrameMs - directChildMs );
        if ( m_warmupFrames > 0 )
        {
            continue;
        }
        marker.selfRingMs[marker.selfRingHead] = marker.lastSelfMs;
        marker.selfRingHead = ( marker.selfRingHead + 1 ) % RING_SIZE;
        if ( marker.selfRingFilled < RING_SIZE )
        {
            ++marker.selfRingFilled;
        }
    }

    // Moving average refreshed every 500 ms (CPU, GPU, and worker core work) — skip during warmup
    bool refreshAverages = false;
    if ( m_warmupFrames == 0 )
    {
        LARGE_INTEGER t;
        QueryPerformanceCounter( &t );
        const int64_t elapsedMs = ( t.QuadPart - m_lastAvgTicks ) * 1000 / m_qpcFrequency;
        if ( m_lastAvgTicks == 0 || elapsedMs >= TICKS_PER_AVG_REFRESH_MS )
        {
            m_lastAvgTicks = t.QuadPart;
            refreshAverages = true;
        }
    }

    {
        std::lock_guard<std::mutex> lock( m_workerSampleMutex );
        m_workerCoreSampleCount = 0;
        for ( int workerIndex = 0; workerIndex < MAX_WORKER_CORES; ++workerIndex )
        {
            const WorkerCoreAccumulator& worker = m_workerCoreAccumulators[workerIndex];
            const float frameCoreMs = static_cast<float>( worker.accumSecondsThisFrame * 1000.0 );
            WorkerCoreAverageWindow& average = m_workerCoreAverageWindows[workerIndex];
            average.accumulatedCoreMs += frameCoreMs;
            ++average.frameCount;
            if ( refreshAverages && average.frameCount > 0 )
            {
                average.avgCoreMs =
                    static_cast<float>( average.accumulatedCoreMs / static_cast<double>( average.frameCount ) );
                average.accumulatedCoreMs = 0.0;
                average.frameCount = 0;
            }

            if ( worker.jobCount <= 0 && frameCoreMs <= 0.0f && average.avgCoreMs <= 0.0f )
            {
                continue;
            }

            WorkerCoreSample& sample = m_workerCoreSamples[m_workerCoreSampleCount++];
            sample.workerIndex = workerIndex;
            sample.jobCount = worker.jobCount;
            sample.coreMs = frameCoreMs;
            sample.avgCoreMs = average.avgCoreMs;
            sample.spanStartMs =
                worker.spanWrittenThisFrame ? static_cast<float>( worker.firstStartSecondsThisFrame * 1000.0 ) : 0.0f;
            sample.spanEndMs =
                worker.spanWrittenThisFrame ? static_cast<float>( worker.lastEndSecondsThisFrame * 1000.0 ) : 0.0f;
        }
    }

#if defined( TRACY_ENABLE )
    if ( SkullbonezCore::Core::DevelopmentTools::TracyClientOwner::CopyStatus().viewerConnected )
    {
        // Why: fixed snapshots already computed for the legacy profiler are
        // the cheapest trustworthy capacity facts. No-viewer runs skip these
        // copies, including the renderer memory snapshot.
        double workerCoreMs = 0.0;
        int workerJobs = 0;
        for ( int index = 0; index < m_workerCoreSampleCount; ++index )
        {
            workerCoreMs += (std::max)( 0.0f, m_workerCoreSamples[index].coreMs );
            workerJobs += (std::max)( 0, m_workerCoreSamples[index].jobCount );
        }
        double frameMs = 0.0;
        for ( int index = 0; index < m_markerCount; ++index )
        {
            if ( m_markers[index].hash == kFrameHash )
            {
                frameMs = m_markers[index].lastFrameMs;
                break;
            }
        }
        const double workerUtilization =
            frameMs > 0.0 && m_workerCoreSampleCount > 0
                ? (std::min)( 100.0, workerCoreMs * 100.0 / ( frameMs * m_workerCoreSampleCount ) )
                : 0.0;
        SKORE_TRACY_PLOT_VALUE( "Counter/Workers/ActiveWorkers", m_workerCoreSampleCount );
        SKORE_TRACY_PLOT_VALUE( "Counter/Workers/Jobs", workerJobs );
        SKORE_TRACY_PLOT_VALUE( "Counter/Workers/CoreMilliseconds", workerCoreMs );
        SKORE_TRACY_PLOT_VALUE( "Counter/Workers/UtilizationPercent", workerUtilization );
        for ( int index = 0; index < m_counterCount; ++index )
        {
            SKORE_TRACY_PLOT_VALUE( m_counters[index].name, m_counters[index].lastFrameValue );
        }
        if ( m_renderDiagnostics )
        {
            const RenderMemoryStats memory = m_renderDiagnostics->GetRenderMemoryStats();
            SKORE_TRACY_PLOT_VALUE( "Counter/Render/DrawCalls", m_renderDiagnostics->GetFrameDrawCallCount() );
            SKORE_TRACY_PLOT_VALUE( "Counter/Render/UploadUsedBytes", memory.uploadUsedBytes );
            SKORE_TRACY_PLOT_VALUE( "Counter/Render/UploadPeakBytes", memory.uploadPeakBytes );
            SKORE_TRACY_PLOT_VALUE( "Counter/Render/RTVDescriptorsUsed", memory.rtvDescriptorsUsed );
            SKORE_TRACY_PLOT_VALUE( "Counter/Render/DSVDescriptorsUsed", memory.dsvDescriptorsUsed );
            SKORE_TRACY_PLOT_VALUE( "Counter/Render/StaticSRVDescriptorsUsed", memory.srvStaticDescriptorsUsed );
            SKORE_TRACY_PLOT_VALUE( "Counter/Render/StaticSRVDescriptorsHighWater",
                                    memory.srvStaticDescriptorsHighWater );
            SKORE_TRACY_PLOT_VALUE( "Counter/Render/TransientSRVDescriptorsUsed",
                                    memory.srvTransientDescriptorsUsedThisFrame );
            SKORE_TRACY_PLOT_VALUE( "Counter/Render/TransientSRVDescriptorsPeak",
                                    memory.srvTransientDescriptorsPeakThisRun );
        }
        SkullbonezCore::Core::DevelopmentTools::TracyClientOwner::PublishDevelopmentAllocationPlots();
    }
#endif

    if ( refreshAverages )
    {
        for ( int i = 0; i < m_markerCount; ++i )
        {
            Marker& m = m_markers[i];

            // CPU average
            int n = m.ringFilled;
            if ( n > 0 )
            {
                double sum = 0.0;
                for ( int k = 0; k < n; ++k )
                {
                    sum += m.ringMs[k];
                }
                m.avgMs = static_cast<float>( sum / n );
            }

            int sn = m.selfRingFilled;
            if ( sn > 0 )
            {
                double selfSum = 0.0;
                for ( int k = 0; k < sn; ++k )
                {
                    selfSum += m.selfRingMs[k];
                }
                m.selfAvgMs = static_cast<float>( selfSum / sn );
            }

            // GPU average
            if ( m.hasGpu )
            {
                int gn = m.gpuRingFilled;
                if ( gn > 0 )
                {
                    double gsum = 0.0;
                    for ( int k = 0; k < gn; ++k )
                    {
                        gsum += m.gpuRingMs[k];
                    }
                    m.gpuAvgMs = static_cast<float>( gsum / gn );
                }
            }
        }
    }

    m_inFrame = false;
}


float Profiler::LastFrameMsByHash( uint32_t hash ) const
{
    for ( int i = 0; i < m_markerCount; ++i )
    {
        if ( m_markers[i].hash == hash )
        {
            return m_markers[i].lastFrameMs;
        }
    }
    return 0.0f;
}


float Profiler::LastGpuFrameMsByHash( uint32_t hash ) const
{
    for ( int i = 0; i < m_markerCount; ++i )
    {
        if ( m_markers[i].hash == hash )
        {
            return m_markers[i].gpuLastFrameMs > 0.0f ? m_markers[i].gpuLastFrameMs : m_markers[i].gpuAvgMs;
        }
    }
    return 0.0f;
}


int Profiler::PerfCSVColumnCount() const
{
    int columnCount = 2 + m_counterCount; // pass, frame, then scalar counters.
    for ( int i = 0; i < m_markerCount; ++i )
    {
        columnCount += m_markers[i].hasGpu ? 2 : 1;
    }
    return columnCount;
}


void Profiler::WritePerfCSVHeader( FILE* f ) const
{
    static constexpr uint32_t kVsyncHash = ::HashStr( "Frame/VsyncWait" );

    fprintf( f, "pass,frame" );
    for ( int i = 0; i < m_markerCount; ++i )
    {
        if ( m_markers[i].hash == kVsyncHash )
        {
            continue;
        }
        fprintf( f, ",%s", m_markers[i].name );
        if ( m_markers[i].hasGpu )
        {
            fprintf( f, ",%s_gpu", m_markers[i].name );
        }
    }
    // Keep VsyncWait at the end so it doesn't skew active-frame averages when viewed together.
    for ( int i = 0; i < m_markerCount; ++i )
    {
        if ( m_markers[i].hash == kVsyncHash )
        {
            fprintf( f, ",%s", m_markers[i].name );
            if ( m_markers[i].hasGpu )
            {
                fprintf( f, ",%s_gpu", m_markers[i].name );
            }
            break;
        }
    }
    for ( int i = 0; i < m_counterCount; ++i )
    {
        fprintf( f, ",%s", m_counters[i].name );
    }
    fprintf( f, "\n" );
    m_lastPerfCSVColumnCount = PerfCSVColumnCount();
}


void Profiler::WritePerfCSVRow( FILE* f, int pass, int frame ) const
{
    if ( m_warmupFrames > 0 )
    {
        return;
    }

    // Hazard: some diagnostics register only when a late scene event occurs.
    // Re-emit the dynamic header before the first wider row so columns never
    // shift silently; analyze_perf already treats each header as authoritative
    // for the rows that follow it.
    if ( m_lastPerfCSVColumnCount != PerfCSVColumnCount() )
    {
        WritePerfCSVHeader( f );
    }

    static constexpr uint32_t kVsyncHash = ::HashStr( "Frame/VsyncWait" );

    fprintf( f, "%d,%d", pass, frame );
    for ( int i = 0; i < m_markerCount; ++i )
    {
        if ( m_markers[i].hash == kVsyncHash )
        {
            continue;
        }
        fprintf( f, ",%.4f", m_markers[i].lastFrameMs );
        if ( m_markers[i].hasGpu )
        {
            fprintf( f, ",%.4f", m_markers[i].gpuLastFrameMs );
        }
    }
    for ( int i = 0; i < m_markerCount; ++i )
    {
        if ( m_markers[i].hash == kVsyncHash )
        {
            fprintf( f, ",%.4f", m_markers[i].lastFrameMs );
            if ( m_markers[i].hasGpu )
            {
                fprintf( f, ",%.4f", m_markers[i].gpuLastFrameMs );
            }
            break;
        }
    }
    for ( int i = 0; i < m_counterCount; ++i )
    {
        fprintf( f, ",%.4f", m_counters[i].lastFrameValue );
    }
    fprintf( f, "\n" );
}


void Profiler::RenderOverlay( Text::TextBatch& textBatch,
                              SkullbonezCore::Rendering::IRenderCommandContext& renderCommands,
                              float xLeft,
                              float yAnchor,
                              float lineHeight,
                              float fSize,
                              float fps,
                              bool rightAnchored ) const
{
    using SkullbonezCore::Text::Text2d;

    bool anyGpu = false;
    for ( int i = 0; i < m_markerCount; ++i )
    {
        if ( m_markers[i].hasGpu && m_markers[i].gpuRingFilled > 0 )
        {
            anyGpu = true;
            break;
        }
    }

    // Layout constants
    const float padX = fSize * 0.6f;
    const float padY = lineHeight * 1.2f;

    float markerNameW = Text2d::MeasureText( fSize, "MARKER" );
    for ( int i = 0; i < m_markerCount; ++i )
    {
        char nameBuf[64] = { 0 };
        int spaces = m_markers[i].depth * 2;
        if ( spaces > 20 )
        {
            spaces = 20;
        }
        for ( int k = 0; k < spaces; ++k )
        {
            nameBuf[k] = ' ';
        }
        strcpy_s( nameBuf + spaces, sizeof( nameBuf ) - spaces, m_markers[i].leafName );
        markerNameW = (std::max)( markerNameW, Text2d::MeasureText( fSize, nameBuf ) );
    }

    // Column x-offsets
    const float colName = 0.0f;
    const float valueColStep = fSize * 7.0f;
    const float colAvg = markerNameW + fSize * 1.5f;
    const float colSelf = colAvg + valueColStep;
    const float colGpu = anyGpu ? colSelf + valueColStep : -1.0f;
    const float colP50 = anyGpu ? colGpu + valueColStep : colSelf + valueColStep;
    const float colP99 = colP50 + valueColStep;
    const float colMin = colP99 + valueColStep;
    const float colMax = colMin + valueColStep;

    // Dynamically size the panel: right edge of last column plus measured value width + right padding.
    // Using MeasureText ensures the background quad is always wide enough for the actual font metrics.
    const float colValW = Text2d::MeasureText( fSize, "9999.99" );
    const float panelW = colMax + colValW + padX;

    // Clamp rows to available screen height so the panel never overflows the top of the screen.
    const float screenH = Text2d::HalfH( textBatch ) * 2.0f;
    const int maxRows = static_cast<int>( ( screenH - 4.0f * padY ) / lineHeight );
    const int visRows = ( m_markerCount + 2 < maxRows ) ? m_markerCount + 2 : maxRows;
    const float rowsHeight = static_cast<float>( visRows ) * lineHeight;

    // When right-anchored, xLeft is the desired right edge of the panel; resolve to true xLeft.
    if ( rightAnchored )
    {
        xLeft = xLeft + padX - panelW;
    }

    const float yBottom = yAnchor + padY;
    const float yTop = yBottom + rowsHeight;

    // Background quad
    Text2d::Render2dQuad( textBatch,
                          renderCommands,
                          xLeft - padX,
                          yBottom,
                          xLeft - padX + panelW,
                          yTop + padY,
                          0.12f,
                          0.12f,
                          0.12f,
                          0.5f );

    // Color palette
    const float hdrR = 1.0f, hdrG = 0.85f, hdrB = 0.2f; // gold header
    const float colR = 0.6f, colG = 0.6f, colB = 0.6f;  // grey column headers
    const float gpuR = 0.4f, gpuG = 0.8f, gpuB = 1.0f;  // cyan for GPU values

    // Frame samples are work-only; VsyncWait remains a separate marker row.
    static constexpr uint32_t kFrameHash = ::HashStr( "Frame" );
    static constexpr uint32_t kVsyncHash = ::HashStr( "Frame/VsyncWait" );
    float frameAvgMs = 0.0f;
    for ( int i = 0; i < m_markerCount; ++i )
    {
        if ( m_markers[i].hash == kFrameHash )
        {
            frameAvgMs = m_markers[i].avgMs;
        }
    }
    const float cpuMs = frameAvgMs;

    // Header line
    float y = yTop;
    Text2d::Render2dTextColor( textBatch, xLeft, y, fSize, hdrR, hdrG, hdrB, "CPU: %.2f ms  FPS: %.1f", cpuMs, fps );
    y -= lineHeight;

    // Column labels
    Text2d::Render2dTextColor( textBatch, xLeft + colName, y, fSize, colR, colG, colB, "MARKER" );
    Text2d::Render2dTextColor( textBatch, xLeft + colAvg, y, fSize, colR, colG, colB, "CPU" );
    Text2d::Render2dTextColor( textBatch, xLeft + colSelf, y, fSize, colR, colG, colB, "SELF" );
    if ( anyGpu )
    {
        Text2d::Render2dTextColor( textBatch, xLeft + colGpu, y, fSize, gpuR, gpuG, gpuB, "GPU" );
    }
    Text2d::Render2dTextColor( textBatch, xLeft + colP50, y, fSize, colR, colG, colB, "P50" );
    Text2d::Render2dTextColor( textBatch, xLeft + colP99, y, fSize, colR, colG, colB, "P99" );
    Text2d::Render2dTextColor( textBatch, xLeft + colMin, y, fSize, colR, colG, colB, "MIN" );
    Text2d::Render2dTextColor( textBatch, xLeft + colMax, y, fSize, colR, colG, colB, "MAX" );
    y -= lineHeight;

    // Traffic-light threshold: proportion of CPU budget
    float budgetMs = ( cpuMs > 0.001f ) ? cpuMs : 1.0f;

    // Marker rows -- VsyncWait rendered last (at bottom).
    auto renderMarkerRow = [&]( const Marker& m )
    {
        if ( y < yBottom )
        {
            y -= lineHeight;
            return;
        }
        char nameBuf[64] = { 0 };
        int spaces = m.depth * 2;
        if ( spaces > 20 )
        {
            spaces = 20;
        }
        for ( int k = 0; k < spaces; ++k )
        {
            nameBuf[k] = ' ';
        }
        strcpy_s( nameBuf + spaces, sizeof( nameBuf ) - spaces, m.leafName );

        float mr, mg, mb;
        if ( m.hash == kVsyncHash )
        {
            mr = 0.5f;
            mg = 0.5f;
            mb = 0.5f;
        }
        else
        {
            float ratio = m.avgMs / budgetMs;
            if ( ratio < 0.15f )
            {
                mr = 0.3f;
                mg = 0.9f;
                mb = 0.3f;
            }
            else if ( ratio < 0.5f )
            {
                mr = 1.0f;
                mg = 0.7f;
                mb = 0.2f;
            }
            else
            {
                mr = 1.0f;
                mg = 0.3f;
                mb = 0.3f;
            }
        }

        Text2d::Render2dTextColor( textBatch, xLeft + colName, y, fSize, mr, mg, mb, "%s", nameBuf );
        Text2d::Render2dTextColor( textBatch, xLeft + colAvg, y, fSize, mr, mg, mb, "%6.2f", m.avgMs );
        const float selfMs = m.selfAvgMs > 0.0f ? m.selfAvgMs : m.lastSelfMs;
        Text2d::Render2dTextColor( textBatch, xLeft + colSelf, y, fSize, mr, mg, mb, "%6.2f", selfMs );
        if ( anyGpu )
        {
            if ( m.hasGpu && m.gpuRingFilled > 0 )
            {
                Text2d::Render2dTextColor( textBatch, xLeft + colGpu, y, fSize, gpuR, gpuG, gpuB, "%6.2f", m.gpuAvgMs );
            }
            else
            {
                Text2d::Render2dTextColor( textBatch, xLeft + colGpu, y, fSize, colR, colG, colB, "    - " );
            }
        }
        Text2d::Render2dTextColor( textBatch, xLeft + colP50, y, fSize, mr, mg, mb, "%6.2f", m.p50Ms );
        Text2d::Render2dTextColor( textBatch, xLeft + colP99, y, fSize, mr, mg, mb, "%6.2f", m.p99Ms );
        float displayMin = ( m.ringFilled > 0 ) ? m.minMs : 0.0f;
        float displayMax = ( m.ringFilled > 0 ) ? m.maxMs : 0.0f;
        Text2d::Render2dTextColor( textBatch, xLeft + colMin, y, fSize, mr, mg, mb, "%6.2f", displayMin );
        Text2d::Render2dTextColor( textBatch, xLeft + colMax, y, fSize, mr, mg, mb, "%6.2f", displayMax );
        y -= lineHeight;
    };

    // Build children lists (fixed arrays — no heap allocation).
    // children[i] holds indices of markers whose parentIndex == i, in registration order.
    int childBuf[MAX_MARKERS][MAX_MARKERS]; // [parent][slot]
    int childCount[MAX_MARKERS] = {};
    for ( int i = 0; i < m_markerCount; ++i )
    {
        int p = m_markers[i].parentIndex;
        if ( p >= 0 && p < m_markerCount )
        {
            childBuf[p][childCount[p]++] = i;
        }
    }

    // Depth-first tree walk: roots first in registration order, then their children, etc.
    // Preserves execution order within each sibling group.
    int dfsStack[MAX_MARKERS];
    int dfsTop = 0;
    for ( int i = m_markerCount - 1; i >= 0; --i )
    {
        if ( m_markers[i].parentIndex == -1 && m_markers[i].hash != kVsyncHash )
        {
            dfsStack[dfsTop++] = i;
        }
    }
    while ( dfsTop > 0 )
    {
        int idx = dfsStack[--dfsTop];
        renderMarkerRow( m_markers[idx] );
        for ( int j = childCount[idx] - 1; j >= 0; --j )
        {
            dfsStack[dfsTop++] = childBuf[idx][j];
        }
    }
    for ( int i = 0; i < m_markerCount; ++i )
    {
        if ( m_markers[i].hash == kVsyncHash )
        {
            renderMarkerRow( m_markers[i] );
            break;
        }
    }
}


/* -- RenderBarOverlay
-------------------------------------------------------------------------------------------------------------------------------------------

    Renders a visual profiler panel with horizontal stacked bars:
      - One bar for CPU timing (all leaf markers that have CPU timing)
      - One bar for GPU timing (only leaf markers that have GPU timing)

    Each bar is subdivided into coloured segments proportional to the leaf marker's avgMs.
    A colour legend is rendered below the bars.

    absolute=false: "Normalized" — segments fill the entire bar width (relative proportions).
    absolute=true:  "Absolute"  — bar width = full frame time; white segment = idle/vsync remainder.

    The panel is designed with vertical headroom for future multi-core stacking (CPU bar per thread).
-----------------------------------------------------------------------------------------------------------------------------------------------------------------*/
void Profiler::RenderBarOverlay( Text::TextBatch& textBatch,
                                 SkullbonezCore::Rendering::IRenderCommandContext& renderCommands,
                                 float xLeft,
                                 float yBottom,
                                 float panelWidth,
                                 float panelHeight,
                                 bool absolute ) const
{
    using SkullbonezCore::Text::Text2d;

    // Identify leaf markers: a marker is a leaf if no other marker has it as parentIndex.
    // Also skip "Frame" (top-level container) from appearing as a bar segment.
    bool isLeaf[MAX_MARKERS] = {};
    for ( int i = 0; i < m_markerCount; ++i )
    {
        isLeaf[i] = true;
    }
    for ( int i = 0; i < m_markerCount; ++i )
    {
        if ( m_markers[i].parentIndex >= 0 )
        {
            isLeaf[m_markers[i].parentIndex] = false;
        }
    }

    // Exclude VsyncWait from CPU segments (it becomes idle in absolute mode).
    static constexpr uint32_t kFrameHash = ::HashStr( "Frame" );
    static constexpr uint32_t kVsyncHash = ::HashStr( "Frame/VsyncWait" );

    // Gather leaf indices for CPU and GPU bars
    int cpuLeaves[MAX_MARKERS];
    int cpuLeafCount = 0;
    int gpuLeaves[MAX_MARKERS];
    int gpuLeafCount = 0;
    float cpuTotalMs = 0.0f;
    float gpuTotalMs = 0.0f;

    for ( int i = 0; i < m_markerCount; ++i )
    {
        if ( !isLeaf[i] )
        {
            continue;
        }
        if ( m_markers[i].hash == kFrameHash )
        {
            continue;
        }

        bool isIdle = ( m_markers[i].hash == kVsyncHash );
        if ( !isIdle )
        {
            cpuLeaves[cpuLeafCount++] = i;
            cpuTotalMs += m_markers[i].avgMs;
        }
        if ( m_markers[i].hasGpu && m_markers[i].gpuRingFilled > 0 && !isIdle )
        {
            gpuLeaves[gpuLeafCount++] = i;
            gpuTotalMs += m_markers[i].gpuAvgMs;
        }
    }

    // In absolute mode, the bar width represents the full frame time.
    // The idle portion is drawn as white at the end.
    float frameMs = 0.0f;
    for ( int i = 0; i < m_markerCount; ++i )
    {
        if ( m_markers[i].hash == kFrameHash )
        {
            frameMs = m_markers[i].avgMs;
            break;
        }
    }
    if ( frameMs < 0.001f )
    {
        frameMs = 16.67f; // fallback: assume 60 Hz
    }

    // Layout constants
    const float pad = panelHeight * 0.06f;
    const float barHeight = panelHeight * 0.18f;    // each bar slightly smaller to make room
    const float barGap = panelHeight * 0.09f;       // larger gap between bars
    const float legendHeight = panelHeight * 0.20f; // legend row (increased for wrapping)
    const float titleH = panelHeight * 0.12f;       // title line (bigger)
    const float barX0 = xLeft + pad;
    const float barX1 = xLeft + panelWidth - pad;
    const float fSz = barHeight * 0.45f; // text size proportional to bar

    // Background quad
    Text2d::BatchQuad( textBatch,
                       renderCommands,
                       xLeft,
                       yBottom,
                       xLeft + panelWidth,
                       yBottom + panelHeight,
                       0.06f,
                       0.06f,
                       0.10f,
                       0.90f );

    // Title
    float ty = yBottom + panelHeight - pad - titleH;
    const char* title = absolute ? "PROFILER BARS (ABSOLUTE)" : "PROFILER BARS (NORMALIZED)";
    Text2d::Render2dTextColor( textBatch, barX0, ty + titleH * 0.35f, fSz * 1.05f, 1.0f, 0.85f, 0.35f, "%s", title );

    // Totals (right-aligned on title row)
    char totalsBuf[128] = { 0 };
    if ( absolute )
    {
        // In absolute mode show CPU sum, GPU sum, and overall frame time
        sprintf_s( totalsBuf,
                   sizeof( totalsBuf ),
                   "CPU: %.2f ms  GPU: %.2f ms  Frame: %.2f ms",
                   cpuTotalMs,
                   gpuTotalMs,
                   frameMs );
    }
    else
    {
        sprintf_s( totalsBuf, sizeof( totalsBuf ), "CPU: %.2f ms  GPU: %.2f ms", cpuTotalMs, gpuTotalMs );
    }
    float totalsW = Text2d::MeasureText( fSz * 0.9f, totalsBuf );
    Text2d::Render2dTextColor( textBatch,
                               barX1 - totalsW,
                               ty + titleH * 0.35f,
                               fSz * 0.9f,
                               0.85f,
                               0.85f,
                               0.85f,
                               "%s",
                               totalsBuf );

    // --- CPU bar ---
    float cpuBarY = ty - barGap - barHeight * 0.4f; // shift down so title doesn't overlap
    Text2d::Render2dTextColor( textBatch, barX0, cpuBarY + barHeight * 0.3f, fSz, 0.85f, 0.85f, 0.85f, "CPU" );
    float cpuLabelW = Text2d::MeasureText( fSz, "CPU " ) + pad * 0.5f;
    float cpuBarX0 = barX0 + cpuLabelW;
    float cpuBarWidth = barX1 - cpuBarX0;

    // Draw background (dark grey = empty / absolute idle)
    Text2d::BatchQuad( textBatch,
                       renderCommands,
                       cpuBarX0,
                       cpuBarY,
                       barX1,
                       cpuBarY + barHeight,
                       0.15f,
                       0.15f,
                       0.15f,
                       1.0f );

    // Scale bars either against the absolute frame or the CPU subtotal.
    float cpuScale = 1.0f;
    if ( absolute )
    {
        cpuScale = ( frameMs > 0.001f ) ? ( cpuBarWidth / frameMs ) : 0.0f;
    }
    else
    {
        cpuScale = ( cpuTotalMs > 0.001f ) ? ( cpuBarWidth / cpuTotalMs ) : 0.0f;
    }

    float cx = cpuBarX0;
    for ( int i = 0; i < cpuLeafCount; ++i )
    {
        const Marker& m = m_markers[cpuLeaves[i]];
        float segW = m.avgMs * cpuScale;
        if ( segW < 0.0001f )
        {
            continue;
        }
        if ( cx + segW > barX1 )
        {
            segW = barX1 - cx; // Keep the segment inside the panel.
        }
        const BarColor& c = BAR_PALETTE[m.colorIndex % BAR_PALETTE_SIZE];
        Text2d::BatchQuad( textBatch,
                           renderCommands,
                           cx,
                           cpuBarY,
                           cx + segW,
                           cpuBarY + barHeight,
                           c.r,
                           c.g,
                           c.b,
                           1.0f );
        cx += segW;
    }

    // Absolute mode: remaining space = white (idle)
    if ( absolute && cx < barX1 )
    {
        Text2d::BatchQuad( textBatch,
                           renderCommands,
                           cx,
                           cpuBarY,
                           barX1,
                           cpuBarY + barHeight,
                           0.85f,
                           0.85f,
                           0.85f,
                           0.7f );
    }

    // --- GPU bar ---
    float gpuBarY = cpuBarY - barGap - barHeight;
    if ( gpuLeafCount > 0 )
    {
        Text2d::Render2dTextColor( textBatch, barX0, gpuBarY + barHeight * 0.3f, fSz, 0.4f, 0.8f, 1.0f, "GPU" );
        float gpuLabelW = cpuLabelW; // align with CPU bar
        float gpuBarX0 = barX0 + gpuLabelW;

        Text2d::BatchQuad( textBatch,
                           renderCommands,
                           gpuBarX0,
                           gpuBarY,
                           barX1,
                           gpuBarY + barHeight,
                           0.15f,
                           0.15f,
                           0.15f,
                           1.0f );

        float gpuScale = 1.0f;
        if ( absolute )
        {
            gpuScale = ( frameMs > 0.001f ) ? ( ( barX1 - gpuBarX0 ) / frameMs ) : 0.0f;
        }
        else
        {
            gpuScale = ( gpuTotalMs > 0.001f ) ? ( ( barX1 - gpuBarX0 ) / gpuTotalMs ) : 0.0f;
        }

        float gx = gpuBarX0;
        for ( int i = 0; i < gpuLeafCount; ++i )
        {
            const Marker& m = m_markers[gpuLeaves[i]];
            float segW = m.gpuAvgMs * gpuScale;
            if ( segW < 0.0001f )
            {
                continue;
            }
            if ( gx + segW > barX1 )
            {
                segW = barX1 - gx;
            }
            const BarColor& c = BAR_PALETTE[m.colorIndex % BAR_PALETTE_SIZE];
            Text2d::BatchQuad( textBatch,
                               renderCommands,
                               gx,
                               gpuBarY,
                               gx + segW,
                               gpuBarY + barHeight,
                               c.r,
                               c.g,
                               c.b,
                               1.0f );
            gx += segW;
        }

        if ( absolute && gx < barX1 )
        {
            Text2d::BatchQuad( textBatch,
                               renderCommands,
                               gx,
                               gpuBarY,
                               barX1,
                               gpuBarY + barHeight,
                               0.85f,
                               0.85f,
                               0.85f,
                               0.7f );
        }
    }

    // --- Colour legend (below bars) ---
    // Place legend a little further down to avoid overlapping the bars and title.
    float legendY =
        ( gpuLeafCount > 0 ) ? ( gpuBarY - barGap - legendHeight * 0.5f ) : ( cpuBarY - barGap - legendHeight * 0.5f );
    float legendFSz = fSz * 0.85f;
    float swatchW = legendFSz * 1.5f; // colour swatch width
    float swatchH = legendFSz;
    float legendX = barX0 + cpuLabelW;
    float legendSpacing = pad * 0.4f;

    // Collect unique leaf markers for legend (union of CPU + GPU leaves, no duplicates)
    int legendIndices[MAX_MARKERS];
    int legendCount = 0;
    bool inLegend[MAX_MARKERS] = {};
    for ( int i = 0; i < cpuLeafCount; ++i )
    {
        inLegend[cpuLeaves[i]] = true;
        legendIndices[legendCount++] = cpuLeaves[i];
    }
    for ( int i = 0; i < gpuLeafCount; ++i )
    {
        if ( !inLegend[gpuLeaves[i]] )
        {
            inLegend[gpuLeaves[i]] = true;
            legendIndices[legendCount++] = gpuLeaves[i];
        }
    }

    // Render legend entries — wrap to next row if they overflow the bar width
    float lx = legendX;
    float ly = legendY;
    for ( int i = 0; i < legendCount; ++i )
    {
        const Marker& m = m_markers[legendIndices[i]];
        const BarColor& c = BAR_PALETTE[m.colorIndex % BAR_PALETTE_SIZE];
        float labelW = Text2d::MeasureText( legendFSz, m.leafName );
        float entryW = swatchW + legendSpacing + labelW + pad;

        // Wrap if this entry would overflow
        if ( lx + entryW > barX1 && lx > legendX + 0.001f )
        {
            lx = legendX;
            ly -= legendHeight;
        }

        // Swatch
        Text2d::BatchQuad( textBatch, renderCommands, lx, ly, lx + swatchW, ly + swatchH, c.r, c.g, c.b, 1.0f );
        // Label
        Text2d::Render2dTextColor( textBatch,
                                   lx + swatchW + legendSpacing,
                                   ly,
                                   legendFSz,
                                   0.85f,
                                   0.85f,
                                   0.85f,
                                   "%s",
                                   m.leafName );
        lx += entryW;
    }

    // Flush all batched quads in one draw call before the text labels are flushed by the caller.
    // This gives the full bar overlay exactly 2 draw calls: one for all quads, one for all text.
    Text2d::FlushQuads( textBatch, renderCommands );
}


#else // SKULLBONEZ_PROFILE_ENABLED

// Why: unprofiled tools/tests can link against profiled static libraries after
// project splits. These definitions preserve the public no-op contract without
// dragging render text or platform-profiler code into non-profiling binaries.
Profiler::Profiler()
    : m_markerCount( 0 ), m_counterCount( 0 ), m_lastPerfCSVColumnCount( -1 ), m_workerCoreSampleCount( 0 ),
      m_stackTop( 0 ), m_qpcFrequency( 1 ), m_frameStartTicks( 0 ), m_lastAvgTicks( 0 ), m_inFrame( false ),
      m_warmupFrames( WARMUP_FRAMES + 1 ), m_resetPending( false ), m_nextColorIndex( 0 ),
      m_renderDiagnostics( nullptr )
{
    std::memset( m_markers, 0, sizeof( m_markers ) );
    std::memset( m_counters, 0, sizeof( m_counters ) );
    std::memset( m_workerCoreAccumulators, 0, sizeof( m_workerCoreAccumulators ) );
    std::memset( m_workerCoreAverageWindows, 0, sizeof( m_workerCoreAverageWindows ) );
    std::memset( m_workerCoreSamples, 0, sizeof( m_workerCoreSamples ) );
    std::memset( m_stackIndices, 0, sizeof( m_stackIndices ) );
    std::memset( m_platformProfilerCpuOpen, 0, sizeof( m_platformProfilerCpuOpen ) );
    std::memset( m_platformProfilerGpuRecordOpen, 0, sizeof( m_platformProfilerGpuRecordOpen ) );
    std::memset( m_platformProfilerGpuEventOpen, 0, sizeof( m_platformProfilerGpuEventOpen ) );
}


void Profiler::Begin( const char*, uint32_t )
{
}


void Profiler::End( const char*, uint32_t )
{
}


void Profiler::RecordWorkerSample( const char*, uint32_t, int, int64_t, int64_t )
{
}


void Profiler::RecordCounter( const char*, uint32_t, double )
{
}


void Profiler::GpuBegin( const char*, uint32_t )
{
}


void Profiler::GpuEnd( const char*, uint32_t )
{
}


void Profiler::FrameBegin()
{
}


void Profiler::FrameEnd()
{
}


void Profiler::BindRenderDiagnostics( Rendering::IRenderDiagnostics* )
{
}


void Profiler::InvalidateGpuQueries()
{
}


void Profiler::ScheduleReset()
{
}


float Profiler::LastFrameMsByHash( uint32_t ) const
{
    return 0.0f;
}


float Profiler::LastGpuFrameMsByHash( uint32_t ) const
{
    return 0.0f;
}


void Profiler::WritePerfCSVHeader( FILE* ) const
{
}


void Profiler::WritePerfCSVRow( FILE*, int, int ) const
{
}


void Profiler::RenderOverlay( Text::TextBatch&,
                              Rendering::IRenderCommandContext&,
                              float,
                              float,
                              float,
                              float,
                              float,
                              bool ) const
{
}


void Profiler::RenderBarOverlay( Text::TextBatch&, Rendering::IRenderCommandContext&, float, float, float, float, bool )
    const
{
}


WorkerProfilerScope::WorkerProfilerScope( Profiler* profiler, const char* fullPath, uint32_t hash )
    : m_profiler( profiler ), m_fullPath( fullPath ), m_hash( hash ), m_workerIndex( -1 ), m_startTicks( 0 ),
      m_platformProfilerOpen( false )
{
}


WorkerProfilerScope::~WorkerProfilerScope()
{
}


#endif // SKULLBONEZ_PROFILE_ENABLED
