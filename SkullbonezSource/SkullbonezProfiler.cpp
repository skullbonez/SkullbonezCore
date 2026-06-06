// --- Includes ---
#include "SkullbonezProfiler.h"
#include "SkullbonezIRenderBackend.h"

#if defined( SKULLBONEZ_PROFILE_ENABLED )

#include <algorithm>
#include <cfloat>
#include <cstring>
#include <cstdlib>
#include "SkullbonezText.h"


// --- Usings ---
using namespace SkullbonezCore::Basics;
using namespace SkullbonezCore::Rendering;


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

Profiler& Profiler::Instance()
{
    static Profiler instance;
    return instance;
}


Profiler::Profiler()
    : m_markerCount( 0 ), m_stackTop( 0 ), m_qpcFrequency( 0 ), m_frameStartTicks( 0 ), m_lastAvgTicks( 0 ), m_inFrame( false ),
      m_warmupFrames( WARMUP_FRAMES + 1 ), m_resetPending( false ), m_nextColorIndex( 0 )
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
    std::memset( m_stackIndices, 0, sizeof( m_stackIndices ) );
}


void Profiler::AbortMismatch( const char* msg, const char* details ) const
{
    char buf[512];
    if ( details )
    {
        _snprintf_s( buf, sizeof( buf ), _TRUNCATE, "PROFILER: %s [%s]\n", msg, details );
    }
    else
    {
        _snprintf_s( buf, sizeof( buf ), _TRUNCATE, "PROFILER: %s\n", msg );
    }
    OutputDebugStringA( buf );
    if ( IsDebuggerPresent() )
    {
        __debugbreak();
    }
    std::abort();
}


int Profiler::FindOrRegister( const char* fullPath, uint32_t hash )
{
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
    m.lastFrameStartMs = 0.0f;
    m.lastFrameEndMs = 0.0f;
    m.avgMs = 0.0f;
    m.p50Ms = 0.0f;
    m.p99Ms = 0.0f;
    m.p99_9Ms = 0.0f;
    m.minMs = FLT_MAX;
    m.maxMs = 0.0f;

    // GPU state initialised to inactive
    m.hasGpu = false;
    m.gpuAllocated = false;
    m.gpuWrittenThisFrame = false;
    m.gpuWriteCursor = 0;
    m.gpuReadCursor = 0;
    m.gpuLastFrameMs = 0.0f;
    m.gpuAvgMs = 0.0f;
    m.gpuRingFilled = 0;
    m.gpuRingHead = 0;
    std::memset( m.gpuQueries, 0, sizeof( m.gpuQueries ) );
    std::memset( m.gpuRingMs, 0, sizeof( m.gpuRingMs ) );

    // Resolve parentIndex by stripping last '/' segment and looking up that prefix
    if ( m.depth == 0 )
    {
        m.parentIndex = -1;
    }
    else
    {
        // Build a temporary parent path on the stack
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

        // Compute hash of parent path and resolve
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
    const double startSeconds = static_cast<double>( t.QuadPart - m_frameStartTicks ) / static_cast<double>( m_qpcFrequency );
    if ( !m.spanWrittenThisFrame )
    {
        m.firstStartSecondsThisFrame = startSeconds;
        m.spanWrittenThisFrame = true;
    }
    m.openCount = 1;
    m_stackIndices[m_stackTop++] = idx;
}


void Profiler::End( const char* fullPath, uint32_t hash )
{
    if ( m_stackTop == 0 )
    {
        AbortMismatch( "PROFILE_END with empty stack", fullPath );
    }

    int topIdx = m_stackIndices[m_stackTop - 1];
    Marker& top = m_markers[topIdx];

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
    top.lastEndSecondsThisFrame = static_cast<double>( t.QuadPart - m_frameStartTicks ) / static_cast<double>( m_qpcFrequency );
    top.openCount = 0;
    --m_stackTop;
}


void Profiler::GpuBegin( const char* fullPath, uint32_t hash )
{
    // Prefer the render-backend GPU timer path (DX11/DX12) when the active backend
    // supports it. This must take priority over the GL path because on a runtime renderer
    // switch (GL → DX via G-key) the GLAD function pointers remain non-null even though
    // no GL context is active — causing silent failures if the GL path were chosen.
    if ( IsGfxReady() && Gfx().SupportsGpuTimers() )
    {
        int idx = FindOrRegister( fullPath, hash );
        Marker& m = m_markers[idx];
        m.hasGpu = true;
        m.gpuWrittenThisFrame = true;
        Gfx().GpuTimerBegin( idx );
        return;
    }

    // GL timestamp query path (used when the GL backend is active)
    if ( !glGenQueries )
    {
        return;
    }

    int idx = FindOrRegister( fullPath, hash );
    Marker& m = m_markers[idx];
    m.hasGpu = true;

    // Lazy-allocate GPU query objects
    if ( !m.gpuAllocated )
    {
        glGenQueries( GPU_QUERY_DEPTH * 2, &m.gpuQueries[0][0] );
        m.gpuAllocated = true;
        m.gpuWriteCursor = 0;
        m.gpuReadCursor = 0;
    }

    // Check if ring is full (all slots pending) — skip this frame's GPU timing
    int pending = ( m.gpuWriteCursor - m.gpuReadCursor + GPU_QUERY_DEPTH ) % GPU_QUERY_DEPTH;
    if ( pending >= GPU_QUERY_DEPTH - 1 )
    {
        return; // buffer full, skip GPU timing this frame
    }

    glQueryCounter( m.gpuQueries[m.gpuWriteCursor][0], GL_TIMESTAMP );
    m.gpuWrittenThisFrame = true;
}


void Profiler::GpuEnd( const char* fullPath, uint32_t hash )
{
    // Same priority rule as GpuBegin: prefer backend timer over GL path.
    if ( IsGfxReady() && Gfx().SupportsGpuTimers() )
    {
        int idx = FindOrRegister( fullPath, hash );
        Marker& m = m_markers[idx];
        if ( m.gpuWrittenThisFrame )
        {
            Gfx().GpuTimerEnd( idx );
        }
        return;
    }

    if ( !glQueryCounter )
    {
        return;
    }

    int idx = FindOrRegister( fullPath, hash );
    Marker& m = m_markers[idx];

    if ( !m.gpuWrittenThisFrame )
    {
        return; // GpuBegin was skipped (ring full), skip end too
    }

    glQueryCounter( m.gpuQueries[m.gpuWriteCursor][1], GL_TIMESTAMP );
}


void Profiler::ReadPendingGpuResults()
{
    // Prefer backend GPU timer path (DX11/DX12) — same priority rule as GpuBegin/GpuEnd.
    if ( IsGfxReady() && Gfx().SupportsGpuTimers() )
    {
        int readCount = 0;
        for ( int i = 0; i < m_markerCount; ++i )
        {
            Marker& m = m_markers[i];
            if ( !m.hasGpu )
            {
                continue;
            }
            float ms = 0.0f;
            if ( Gfx().GpuTimerRead( i, ms ) )
            {
                ++readCount;
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
        return;
    }

    // GL timestamp query readback path
    if ( !glGetQueryObjectuiv )
    {
        return;
    }

    for ( int i = 0; i < m_markerCount; ++i )
    {
        Marker& m = m_markers[i];
        if ( !m.hasGpu || !m.gpuAllocated )
        {
            continue;
        }

        // Try to read the oldest pending slot (non-blocking)
        while ( m.gpuReadCursor != m.gpuWriteCursor )
        {
            GLuint endQuery = m.gpuQueries[m.gpuReadCursor][1];
            GLuint available = 0;
            glGetQueryObjectuiv( endQuery, GL_QUERY_RESULT_AVAILABLE, &available );

            if ( !available )
            {
                break; // not ready yet — don't block
            }

            // Both timestamps are ready (end was issued after begin, so begin is certainly done)
            GLuint64 beginTs = 0, endTs = 0;
            glGetQueryObjectui64v( m.gpuQueries[m.gpuReadCursor][0], GL_QUERY_RESULT, &beginTs );
            glGetQueryObjectui64v( endQuery, GL_QUERY_RESULT, &endTs );

            float gpuMs = 0.0f;
            if ( endTs > beginTs )
            {
                gpuMs = static_cast<float>( static_cast<double>( endTs - beginTs ) / 1000000.0 ); // ns → ms
            }

            m.gpuLastFrameMs = gpuMs;

            // Commit to GPU ring buffer and recompute average immediately
            m.gpuRingMs[m.gpuRingHead] = gpuMs;
            m.gpuRingHead = ( m.gpuRingHead + 1 ) % RING_SIZE;
            if ( m.gpuRingFilled < RING_SIZE )
            {
                ++m.gpuRingFilled;
            }
            double gsum = 0.0;
            for ( int k = 0; k < m.gpuRingFilled; ++k )
            {
                gsum += m.gpuRingMs[k];
            }
            m.gpuAvgMs = static_cast<float>( gsum / m.gpuRingFilled );

            m.gpuReadCursor = ( m.gpuReadCursor + 1 ) % GPU_QUERY_DEPTH;
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
            m.gpuWriteCursor = ( m.gpuWriteCursor + 1 ) % GPU_QUERY_DEPTH;
            m.gpuWrittenThisFrame = false;
        }
    }
}


void Profiler::InvalidateGpuQueries()
{
    for ( int i = 0; i < m_markerCount; ++i )
    {
        Marker& m = m_markers[i];
        // Don't call glDeleteQueries — context is already gone
        m.hasGpu = false;
        m.gpuAllocated = false;
        m.gpuWrittenThisFrame = false;
        m.gpuWriteCursor = 0;
        m.gpuReadCursor = 0;
        m.gpuLastFrameMs = 0.0f;
        m.gpuAvgMs = 0.0f;
        m.gpuRingFilled = 0;
        m.gpuRingHead = 0;
        std::memset( m.gpuQueries, 0, sizeof( m.gpuQueries ) );
        std::memset( m.gpuRingMs, 0, sizeof( m.gpuRingMs ) );
    }
    // +1 because FrameBegin decrements before the frame runs
    m_warmupFrames = WARMUP_FRAMES + 1;

    if ( IsGfxReady() )
    {
        Gfx().GpuTimerInvalidate();
    }
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
        // InvalidateGpuQueries also calls Gfx().GpuTimerInvalidate() and resets warmup.
        InvalidateGpuQueries();
        m_markerCount = 0;
        m_lastAvgTicks = 0;
        m_nextColorIndex = 0;
        m_resetPending = false;
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

    // Reset per-frame accumulators
    for ( int i = 0; i < m_markerCount; ++i )
    {
        m_markers[i].accumSecondsThisFrame = 0.0;
        m_markers[i].firstStartSecondsThisFrame = 0.0;
        m_markers[i].lastEndSecondsThisFrame = 0.0;
        m_markers[i].spanWrittenThisFrame = false;
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

    if ( m_stackTop != 0 )
    {
        AbortMismatch( "FrameEnd with open markers (missing PROFILE_END)", m_markers[m_stackIndices[m_stackTop - 1]].name );
    }

    // Advance GPU write cursors for markers that recorded timestamps this frame
    AdvanceGpuWriteCursors();

    // Commit per-frame totals into ring buffer; compute p50 / p99
    // During warmup (m_warmupFrames > 0) we still update lastFrameMs for the live overlay
    // but skip ring buffer / min / max / percentile updates to exclude startup noise.
    static float scratch[RING_SIZE]; // single-threaded — safe to be static
    for ( int i = 0; i < m_markerCount; ++i )
    {
        Marker& m = m_markers[i];
        float ms = static_cast<float>( m.accumSecondsThisFrame * 1000.0 );
        m.lastFrameMs = ms;
        if ( m.spanWrittenThisFrame )
        {
            m.lastFrameStartMs = static_cast<float>( m.firstStartSecondsThisFrame * 1000.0 );
            m.lastFrameEndMs = static_cast<float>( m.lastEndSecondsThisFrame * 1000.0 );
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

    // Moving average refreshed every 500 ms (CPU and GPU) — skip during warmup
    if ( m_warmupFrames == 0 )
    {
        LARGE_INTEGER t;
        QueryPerformanceCounter( &t );
        int64_t elapsedMs = ( t.QuadPart - m_lastAvgTicks ) * 1000 / m_qpcFrequency;
        if ( m_lastAvgTicks == 0 || elapsedMs >= TICKS_PER_AVG_REFRESH_MS )
        {
            m_lastAvgTicks = t.QuadPart;
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
    fprintf( f, "\n" );
}


void Profiler::WritePerfCSVRow( FILE* f, int pass, int frame ) const
{
    if ( m_warmupFrames > 0 )
    {
        return;
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
    fprintf( f, "\n" );
}


void Profiler::RenderOverlay( float xLeft, float yAnchor, float lineHeight, float fSize, float fps, bool rightAnchored ) const
{
    using SkullbonezCore::Text::Text2d;

    // Check if any marker has GPU timing
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
    const float colGpu = anyGpu ? colAvg + valueColStep : -1.0f;
    const float colP50 = anyGpu ? colGpu + valueColStep : colAvg + valueColStep;
    const float colP99 = colP50 + valueColStep;
    const float colMin = colP99 + valueColStep;
    const float colMax = colMin + valueColStep;

    // Dynamically size the panel: right edge of last column plus measured value width + right padding.
    // Using MeasureText ensures the background quad is always wide enough for the actual font metrics.
    const float colValW = Text2d::MeasureText( fSize, "9999.99" );
    const float panelW = colMax + colValW + padX;

    // Clamp rows to available screen height so the panel never overflows the top of the screen.
    const float screenH = Text2d::HalfH() * 2.0f;
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
    Text2d::Render2dQuad( xLeft - padX, yBottom, xLeft - padX + panelW, yTop + padY, 0.12f, 0.12f, 0.12f, 0.5f );

    // Color palette
    const float hdrR = 1.0f, hdrG = 0.85f, hdrB = 0.2f; // gold header
    const float colR = 0.6f, colG = 0.6f, colB = 0.6f;  // grey column headers
    const float gpuR = 0.4f, gpuG = 0.8f, gpuB = 1.0f;  // cyan for GPU values

    // Look up Frame and VsyncWait for active CPU time.
    static constexpr uint32_t kFrameHash = ::HashStr( "Frame" );
    static constexpr uint32_t kVsyncHash = ::HashStr( "Frame/VsyncWait" );
    float frameAvgMs = 0.0f;
    float vsyncAvgMs = 0.0f;
    for ( int i = 0; i < m_markerCount; ++i )
    {
        if ( m_markers[i].hash == kFrameHash )
        {
            frameAvgMs = m_markers[i].avgMs;
        }
        else if ( m_markers[i].hash == kVsyncHash )
        {
            vsyncAvgMs = m_markers[i].avgMs;
        }
    }
    const float cpuMs = frameAvgMs - vsyncAvgMs;

    // Header line
    float y = yTop;
    Text2d::Render2dTextColor( xLeft, y, fSize, hdrR, hdrG, hdrB, "CPU: %.2f ms  FPS: %.1f", cpuMs, fps );
    y -= lineHeight;

    // Column labels
    Text2d::Render2dTextColor( xLeft + colName, y, fSize, colR, colG, colB, "MARKER" );
    Text2d::Render2dTextColor( xLeft + colAvg, y, fSize, colR, colG, colB, "CPU" );
    if ( anyGpu )
    {
        Text2d::Render2dTextColor( xLeft + colGpu, y, fSize, gpuR, gpuG, gpuB, "GPU" );
    }
    Text2d::Render2dTextColor( xLeft + colP50, y, fSize, colR, colG, colB, "P50" );
    Text2d::Render2dTextColor( xLeft + colP99, y, fSize, colR, colG, colB, "P99" );
    Text2d::Render2dTextColor( xLeft + colMin, y, fSize, colR, colG, colB, "MIN" );
    Text2d::Render2dTextColor( xLeft + colMax, y, fSize, colR, colG, colB, "MAX" );
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

        Text2d::Render2dTextColor( xLeft + colName, y, fSize, mr, mg, mb, "%s", nameBuf );
        Text2d::Render2dTextColor( xLeft + colAvg, y, fSize, mr, mg, mb, "%6.2f", m.avgMs );
        if ( anyGpu )
        {
            if ( m.hasGpu && m.gpuRingFilled > 0 )
            {
                Text2d::Render2dTextColor( xLeft + colGpu, y, fSize, gpuR, gpuG, gpuB, "%6.2f", m.gpuAvgMs );
            }
            else
            {
                Text2d::Render2dTextColor( xLeft + colGpu, y, fSize, colR, colG, colB, "    - " );
            }
        }
        Text2d::Render2dTextColor( xLeft + colP50, y, fSize, mr, mg, mb, "%6.2f", m.p50Ms );
        Text2d::Render2dTextColor( xLeft + colP99, y, fSize, mr, mg, mb, "%6.2f", m.p99Ms );
        float displayMin = ( m.ringFilled > 0 ) ? m.minMs : 0.0f;
        float displayMax = ( m.ringFilled > 0 ) ? m.maxMs : 0.0f;
        Text2d::Render2dTextColor( xLeft + colMin, y, fSize, mr, mg, mb, "%6.2f", displayMin );
        Text2d::Render2dTextColor( xLeft + colMax, y, fSize, mr, mg, mb, "%6.2f", displayMax );
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
        if ( m_markers[i].parentIndex == -1 &&
             m_markers[i].hash != kVsyncHash )
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


/* -- RenderBarOverlay -------------------------------------------------------------------------------------------------------------------------------------------

    Renders a visual profiler panel with horizontal stacked bars:
      - One bar for CPU timing (all leaf markers that have CPU timing)
      - One bar for GPU timing (only leaf markers that have GPU timing)

    Each bar is subdivided into coloured segments proportional to the leaf marker's avgMs.
    A colour legend is rendered below the bars.

    absolute=false: "Normalized" — segments fill the entire bar width (relative proportions).
    absolute=true:  "Absolute"  — bar width = full frame time; white segment = idle/vsync remainder.

    The panel is designed with vertical headroom for future multi-core stacking (CPU bar per thread).
-----------------------------------------------------------------------------------------------------------------------------------------------------------------*/
void Profiler::RenderBarOverlay( float xLeft, float yBottom, float panelWidth, float panelHeight, bool absolute ) const
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
    Text2d::BatchQuad( xLeft, yBottom, xLeft + panelWidth, yBottom + panelHeight, 0.06f, 0.06f, 0.10f, 0.90f );

    // Title
    float ty = yBottom + panelHeight - pad - titleH;
    const char* title = absolute ? "PROFILER BARS (ABSOLUTE)" : "PROFILER BARS (NORMALIZED)";
    Text2d::Render2dTextColor( barX0, ty + titleH * 0.35f, fSz * 1.05f, 1.0f, 0.85f, 0.35f, "%s", title );

    // Totals (right-aligned on title row)
    char totalsBuf[128] = { 0 };
    if ( absolute )
    {
        // In absolute mode show CPU sum, GPU sum, and overall frame time
        sprintf_s( totalsBuf, sizeof( totalsBuf ), "CPU: %.2f ms  GPU: %.2f ms  Frame: %.2f ms", cpuTotalMs, gpuTotalMs, frameMs );
    }
    else
    {
        sprintf_s( totalsBuf, sizeof( totalsBuf ), "CPU: %.2f ms  GPU: %.2f ms", cpuTotalMs, gpuTotalMs );
    }
    float totalsW = Text2d::MeasureText( fSz * 0.9f, totalsBuf );
    Text2d::Render2dTextColor( barX1 - totalsW, ty + titleH * 0.35f, fSz * 0.9f, 0.85f, 0.85f, 0.85f, "%s", totalsBuf );

    // --- CPU bar ---
    float cpuBarY = ty - barGap - barHeight * 0.4f; // nudge down so title doesn't overlap
    Text2d::Render2dTextColor( barX0, cpuBarY + barHeight * 0.3f, fSz, 0.85f, 0.85f, 0.85f, "CPU" );
    float cpuLabelW = Text2d::MeasureText( fSz, "CPU " ) + pad * 0.5f;
    float cpuBarX0 = barX0 + cpuLabelW;
    float cpuBarWidth = barX1 - cpuBarX0;

    // Draw background (dark grey = empty / absolute idle)
    Text2d::BatchQuad( cpuBarX0, cpuBarY, barX1, cpuBarY + barHeight, 0.15f, 0.15f, 0.15f, 1.0f );

    // Compute scale factor
    float cpuScale = 1.0f;
    if ( absolute )
    {
        cpuScale = ( frameMs > 0.001f ) ? ( cpuBarWidth / frameMs ) : 0.0f;
    }
    else
    {
        cpuScale = ( cpuTotalMs > 0.001f ) ? ( cpuBarWidth / cpuTotalMs ) : 0.0f;
    }

    // Draw segments
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
            segW = barX1 - cx; // clamp to bar
        }
        const BarColor& c = BAR_PALETTE[m.colorIndex % BAR_PALETTE_SIZE];
        Text2d::BatchQuad( cx, cpuBarY, cx + segW, cpuBarY + barHeight, c.r, c.g, c.b, 1.0f );
        cx += segW;
    }

    // Absolute mode: remaining space = white (idle)
    if ( absolute && cx < barX1 )
    {
        Text2d::BatchQuad( cx, cpuBarY, barX1, cpuBarY + barHeight, 0.85f, 0.85f, 0.85f, 0.7f );
    }

    // --- GPU bar ---
    float gpuBarY = cpuBarY - barGap - barHeight;
    if ( gpuLeafCount > 0 )
    {
        Text2d::Render2dTextColor( barX0, gpuBarY + barHeight * 0.3f, fSz, 0.4f, 0.8f, 1.0f, "GPU" );
        float gpuLabelW = cpuLabelW; // align with CPU bar
        float gpuBarX0 = barX0 + gpuLabelW;

        Text2d::BatchQuad( gpuBarX0, gpuBarY, barX1, gpuBarY + barHeight, 0.15f, 0.15f, 0.15f, 1.0f );

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
            Text2d::BatchQuad( gx, gpuBarY, gx + segW, gpuBarY + barHeight, c.r, c.g, c.b, 1.0f );
            gx += segW;
        }

        if ( absolute && gx < barX1 )
        {
            Text2d::BatchQuad( gx, gpuBarY, barX1, gpuBarY + barHeight, 0.85f, 0.85f, 0.85f, 0.7f );
        }
    }

    // --- Colour legend (below bars) ---
    // Place legend a little further down to avoid overlapping the bars and title.
    float legendY = ( gpuLeafCount > 0 ) ? ( gpuBarY - barGap - legendHeight * 0.5f ) : ( cpuBarY - barGap - legendHeight * 0.5f );
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
        Text2d::BatchQuad( lx, ly, lx + swatchW, ly + swatchH, c.r, c.g, c.b, 1.0f );
        // Label
        Text2d::Render2dTextColor( lx + swatchW + legendSpacing, ly, legendFSz, 0.85f, 0.85f, 0.85f, "%s", m.leafName );
        lx += entryW;
    }

    // Flush all batched quads in one draw call before the text labels are flushed by the caller.
    // This gives the full bar overlay exactly 2 draw calls: one for all quads, one for all text.
    Text2d::FlushQuads();
}


#endif // SKULLBONEZ_PROFILE_ENABLED
