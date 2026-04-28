// --- Includes ---
#include "SkullbonezProfiler.h"

#if defined( SKULLBONEZ_PROFILE_ENABLED )

#include <algorithm>
#include <cfloat>
#include <cstring>
#include <cstdlib>
#include "SkullbonezText.h"


// --- Usings ---
using namespace SkullbonezCore::Basics;


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
    : m_markerCount( 0 ), m_stackTop( 0 ), m_qpcFrequency( 0 ), m_lastAvgTicks( 0 ), m_inFrame( false )
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
    m.openCount = 0;
    m.openStartTicks = 0;
    m.accumSecondsThisFrame = 0.0;
    m.ringFilled = 0;
    m.ringHead = 0;
    m.lastFrameMs = 0.0f;
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
    top.openCount = 0;
    --m_stackTop;
}


void Profiler::GpuBegin( const char* fullPath, uint32_t hash )
{
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

            // Commit to GPU ring buffer
            m.gpuRingMs[m.gpuRingHead] = gpuMs;
            m.gpuRingHead = ( m.gpuRingHead + 1 ) % RING_SIZE;
            if ( m.gpuRingFilled < RING_SIZE )
            {
                ++m.gpuRingFilled;
            }

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
}


void Profiler::FrameBegin()
{
    if ( m_inFrame )
    {
        AbortMismatch( "FrameBegin called twice without FrameEnd", nullptr );
    }
    if ( m_stackTop != 0 )
    {
        AbortMismatch( "FrameBegin with non-empty stack", nullptr );
    }
    m_inFrame = true;

    // Read any pending GPU results from previous frames (non-blocking)
    ReadPendingGpuResults();

    // Reset per-frame accumulators
    for ( int i = 0; i < m_markerCount; ++i )
    {
        m_markers[i].accumSecondsThisFrame = 0.0;
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
    static float scratch[RING_SIZE]; // single-threaded — safe to be static
    for ( int i = 0; i < m_markerCount; ++i )
    {
        Marker& m = m_markers[i];
        float ms = static_cast<float>( m.accumSecondsThisFrame * 1000.0 );
        m.lastFrameMs = ms;
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

    // Moving average refreshed every 500 ms (CPU and GPU)
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


void Profiler::WritePerfCSVHeader( FILE* f ) const
{
    fprintf( f, "pass,frame" );
    for ( int i = 0; i < m_markerCount; ++i )
    {
        fprintf( f, ",%s", m_markers[i].name );
        if ( m_markers[i].hasGpu )
        {
            fprintf( f, ",%s_gpu", m_markers[i].name );
        }
    }
    fprintf( f, "\n" );
}


void Profiler::WritePerfCSVRow( FILE* f, int pass, int frame ) const
{
    fprintf( f, "%d,%d", pass, frame );
    for ( int i = 0; i < m_markerCount; ++i )
    {
        fprintf( f, ",%.4f", m_markers[i].lastFrameMs );
        if ( m_markers[i].hasGpu )
        {
            fprintf( f, ",%.4f", m_markers[i].gpuLastFrameMs );
        }
    }
    fprintf( f, "\n" );
}


void Profiler::RenderOverlay( float xLeft, float yAnchor, float lineHeight, float fSize, float fps ) const
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
    const float panelW = anyGpu ? fSize * 53.0f : fSize * 46.0f;
    const float rowsHeight = static_cast<float>( m_markerCount + 2 ) * lineHeight; // +2 for header + column labels

    const float yBottom = yAnchor + padY;
    const float yTop = yBottom + rowsHeight;

    // Background quad
    Text2d::Render2dQuad( xLeft - padX, yBottom, xLeft - padX + panelW, yTop + padY, 0.12f, 0.12f, 0.12f, 0.5f );

    // Color palette
    const float hdrR = 1.0f, hdrG = 0.85f, hdrB = 0.2f; // gold header
    const float colR = 0.6f, colG = 0.6f, colB = 0.6f;  // grey column headers
    const float gpuR = 0.4f, gpuG = 0.8f, gpuB = 1.0f;  // cyan for GPU values

    // Column x-offsets
    const float colName = 0.0f;
    const float colAvg = fSize * 11.0f;
    const float colGpu = anyGpu ? fSize * 18.0f : -1.0f;
    const float colP50 = anyGpu ? fSize * 25.0f : fSize * 18.0f;
    const float colP99 = anyGpu ? fSize * 32.0f : fSize * 25.0f;
    const float colMin = anyGpu ? fSize * 39.0f : fSize * 32.0f;
    const float colMax = anyGpu ? fSize * 46.0f : fSize * 39.0f;

    // Look up Frame, VsyncWait, and PipelineSync for CPU time
    static constexpr uint32_t kFrameHash = ::HashStr( "Frame" );
    static constexpr uint32_t kVsyncHash = ::HashStr( "Frame/VsyncWait" );
    static constexpr uint32_t kPipelineSyncHash = ::HashStr( "Frame/PipelineSync" );
    float frameAvgMs = 0.0f;
    float vsyncAvgMs = 0.0f;
    float pipelineSyncAvgMs = 0.0f;
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
        else if ( m_markers[i].hash == kPipelineSyncHash )
        {
            pipelineSyncAvgMs = m_markers[i].avgMs;
        }
    }
    const float cpuMs = frameAvgMs - vsyncAvgMs - pipelineSyncAvgMs;

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

    // Marker rows
    for ( int i = 0; i < m_markerCount; ++i )
    {
        const Marker& m = m_markers[i];

        // Build indented name
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

        // Traffic light color based on proportion of CPU budget
        float mr, mg, mb;
        if ( m.hash == kVsyncHash || m.hash == kPipelineSyncHash )
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

        Text2d::Render2dTextColor( xLeft + colName, y, fSize, mr, mg, mb, "%-14s", nameBuf );
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
        Text2d::Render2dTextColor( xLeft + colMin, y, fSize, mr, mg, mb, "%6.2f", m.minMs );
        Text2d::Render2dTextColor( xLeft + colMax, y, fSize, mr, mg, mb, "%6.2f", m.maxMs );
        y -= lineHeight;
    }
}


#endif // SKULLBONEZ_PROFILE_ENABLED
