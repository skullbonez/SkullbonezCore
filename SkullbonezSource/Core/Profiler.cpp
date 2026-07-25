/*
File: SkullbonezSource/Core/Profiler.cpp
Purpose:
  Owns hierarchical CPU/GPU timing marker records and fixed profiler history.

Summary:
  Core records stable marker identities, balanced hierarchy spans, worker
  samples, counters, rolling history, and renderer-supplied GPU samples.
  Rendering consumes the resulting frame view without becoming a dependency
  of this infrastructure package.

Glossary:
  Marker epoch: Generation number advanced whenever marker identities reset.
  Render record: Core timing span that brackets renderer-owned GPU work.
  Warmup frame: Completed frame excluded while measurements settle.
  Lane F: Fatal invariant path for corrupt marker identity or nesting.

Invariants:
  - Marker identity is the full path plus hash; collisions are Lane F failures.
  - Begin/end nesting balances before FrameEnd.
  - GPU samples enter as value records; Core never includes Rendering.

Related:
  - SkullbonezSource/Core/Profiler.h
  - SkullbonezSource/Core/PlatformProfiler.h
  - SkullbonezSource/Rendering/RenderProfilerPresentation.cpp
  - Agentic/Reference/comment-style-guide.md
*/
#include "Profiler.h"
#include "FatalError.h"
#include "PlatformProfiler.h"
#include "TracyClientOwner.h"
#include "WorkerPool.h"

#include <algorithm>
#include <cfloat>
#include <cstdio>
#include <cstring>
#include <mutex>


using namespace SkullbonezCore::Core;


#if defined( SKULLBONEZ_PROFILE_ENABLED )


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
      m_warmupFrames( WARMUP_FRAMES + 1 ), m_resetPending( false ), m_nextColorIndex( 0 ), m_markerEpoch( 1 )
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
    std::memset( m_platformProfilerRenderRecordOpen, 0, sizeof( m_platformProfilerRenderRecordOpen ) );
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


void Profiler::RecordWorkerSample(
    const char* fullPath,
    uint32_t hash,
    int workerIndex,
    int64_t startTicks,
    int64_t endTicks
)
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
            m_hash
        );

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
    const SkullbonezCore::Core::DevelopmentTools::TracyZoneToken tracyToken { m_tracyZoneId,
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
    m_platformProfilerRenderRecordOpen[stackSlot] = false;
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
    const SkullbonezCore::Core::DevelopmentTools::TracyZoneToken tracyToken { tracyZoneId,
                                                                              tracyZoneActive,
                                                                              tracyZoneConnectionId };

    SKORE_TRACY_END_OWNER_ZONE( tracyToken );
#endif
    if ( emitCpuPlatformProfiler && cpuPlatformOpen )
    {
        PlatformProfiler::CpuEnd();
    }
}


int Profiler::BeginRenderRecord( const char* fullPath, uint32_t hash )
{
    if ( SkullbonezCore::Threading::WorkerPool::IsCurrentThreadWorker() )
    {
        return -1;
    }
    BeginInternal( fullPath, hash, false );
    const int stackSlot = m_stackTop - 1;
    if ( PlatformProfiler::AreDetailedRangesEnabled() )
    {
        char markerName[PlatformProfiler::MAX_DECORATED_MARKER_NAME_CHARS];
        PlatformProfiler::CpuBegin(
            PlatformProfiler::DecorateMarkerName( fullPath, "_Record", markerName, sizeof( markerName ) ),
            hash
        );

        m_platformProfilerRenderRecordOpen[stackSlot] = true;
    }
    return m_stackIndices[stackSlot];
}


void Profiler::EndRenderRecord( const char* fullPath, uint32_t hash )
{
    if ( SkullbonezCore::Threading::WorkerPool::IsCurrentThreadWorker() )
    {
        return;
    }
    const int stackSlot = m_stackTop > 0 ? m_stackTop - 1 : -1;
    const bool platformRecordOpen = stackSlot >= 0 ? m_platformProfilerRenderRecordOpen[stackSlot] : false;
    if ( stackSlot >= 0 )
    {
        m_platformProfilerRenderRecordOpen[stackSlot] = false;
    }
    if ( platformRecordOpen )
    {
        PlatformProfiler::CpuEnd();
    }
    EndInternal( fullPath, hash, false );
}


void Profiler::MarkGpuMarkerWritten( int markerIndex )
{
    if ( markerIndex < 0 || markerIndex >= m_markerCount )
    {
        SB_FATAL( "Profiler", "GPU marker index %d is outside [0,%d)", markerIndex, m_markerCount );
    }
    Marker& marker = m_markers[markerIndex];
    marker.hasGpu = true;
    marker.gpuWrittenThisFrame = true;
}


void Profiler::ApplyGpuTimingSamples( std::span<const GpuTimingSample> samples )
{
    for ( const GpuTimingSample& sample : samples )
    {
        for ( int markerIndex = 0; markerIndex < m_markerCount; ++markerIndex )
        {
            Marker& marker = m_markers[markerIndex];
            if ( marker.hash != sample.markerHash )
            {
                continue;
            }
            const float milliseconds = (std::max)( 0.0f, sample.milliseconds );
            marker.gpuLastFrameMs = milliseconds;
            marker.gpuRingMs[marker.gpuRingHead] = milliseconds;
            marker.gpuRingHead = ( marker.gpuRingHead + 1 ) % RING_SIZE;
            if ( marker.gpuRingFilled < RING_SIZE )
            {
                ++marker.gpuRingFilled;
            }
            double totalMilliseconds = 0.0;
            for ( int sampleIndex = 0; sampleIndex < marker.gpuRingFilled; ++sampleIndex )
            {
                totalMilliseconds += marker.gpuRingMs[sampleIndex];
            }
            marker.gpuAvgMs = static_cast<float>( totalMilliseconds / marker.gpuRingFilled );
            break;
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


void Profiler::InvalidateGpuSamples()
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
}


Profiler::ProfilerFrameView Profiler::FrameView() const
{
    return ProfilerFrameView {
        std::span<const Marker>( m_markers, static_cast<std::size_t>( m_markerCount ) ),
        std::span<const Counter>( m_counters, static_cast<std::size_t>( m_counterCount ) ),
        std::span<const WorkerCoreSample>( m_workerCoreSamples, static_cast<std::size_t>( m_workerCoreSampleCount ) )
    };
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
        // Clear Core values and advance the identity epoch. The concrete render
        // timing owner observes the new epoch at this same frame boundary and
        // invalidates its backend query slots before reading them.
        InvalidateGpuSamples();
        m_markerCount = 0;
        m_counterCount = 0;
        m_lastPerfCSVColumnCount = -1;
        m_lastAvgTicks = 0;
        m_nextColorIndex = 0;
        ++m_markerEpoch;
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
        AbortMismatch(
            "FrameEnd with open markers (missing PROFILE_END)",
            m_markers[m_stackIndices[m_stackTop - 1]].name
        );
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


#else // SKULLBONEZ_PROFILE_ENABLED

// Why: unprofiled tools/tests retain the same public no-op Core contract.
Profiler::Profiler()
    : m_markerCount( 0 ), m_counterCount( 0 ), m_lastPerfCSVColumnCount( -1 ), m_workerCoreSampleCount( 0 ),
      m_stackTop( 0 ), m_qpcFrequency( 1 ), m_frameStartTicks( 0 ), m_lastAvgTicks( 0 ), m_inFrame( false ),
      m_warmupFrames( WARMUP_FRAMES + 1 ), m_resetPending( false ), m_nextColorIndex( 0 ), m_markerEpoch( 1 )
{
    std::memset( m_markers, 0, sizeof( m_markers ) );
    std::memset( m_counters, 0, sizeof( m_counters ) );
    std::memset( m_workerCoreAccumulators, 0, sizeof( m_workerCoreAccumulators ) );
    std::memset( m_workerCoreAverageWindows, 0, sizeof( m_workerCoreAverageWindows ) );
    std::memset( m_workerCoreSamples, 0, sizeof( m_workerCoreSamples ) );
    std::memset( m_stackIndices, 0, sizeof( m_stackIndices ) );
    std::memset( m_platformProfilerCpuOpen, 0, sizeof( m_platformProfilerCpuOpen ) );
    std::memset( m_platformProfilerRenderRecordOpen, 0, sizeof( m_platformProfilerRenderRecordOpen ) );
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


int Profiler::BeginRenderRecord( const char*, uint32_t )
{
    return -1;
}


void Profiler::EndRenderRecord( const char*, uint32_t )
{
}


void Profiler::MarkGpuMarkerWritten( int )
{
}


void Profiler::ApplyGpuTimingSamples( std::span<const GpuTimingSample> )
{
}


void Profiler::FrameBegin()
{
}


void Profiler::FrameEnd()
{
}


void Profiler::InvalidateGpuSamples()
{
}


Profiler::ProfilerFrameView Profiler::FrameView() const
{
    return {};
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


WorkerProfilerScope::WorkerProfilerScope( Profiler* profiler, const char* fullPath, uint32_t hash )
    : m_profiler( profiler ), m_fullPath( fullPath ), m_hash( hash ), m_workerIndex( -1 ), m_startTicks( 0 ),
      m_platformProfilerOpen( false )
{
}


WorkerProfilerScope::~WorkerProfilerScope()
{
}


#endif // SKULLBONEZ_PROFILE_ENABLED
