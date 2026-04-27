/*------------------------------------------------------------------------------------------------------------------------------------------------------------------------
                                                                          THE SKULLBONEZ CORE
                                                                          ::: PROFILER :::
-------------------------------------------------------------------------------------------------------------------------------------------------------------------------*/

#include "SkullbonezProfiler.h"

#if defined( SKULLBONEZ_PROFILE_ENABLED )

#include <algorithm>
#include <cstring>
#include <cstdlib>
#include "SkullbonezText.h"

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

Profiler& Profiler::Instance( void )
{
    static Profiler instance;
    return instance;
}

Profiler::Profiler( void )
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

void Profiler::FrameBegin( void )
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

    // Reset per-frame accumulators
    for ( int i = 0; i < m_markerCount; ++i )
    {
        m_markers[i].accumSecondsThisFrame = 0.0;
    }

    // Implicit top-level "Frame" marker captures the entire frame total.
    static constexpr uint32_t kFrameHash = HashStr( "Frame" );
    Begin( "Frame", kFrameHash );
}

void Profiler::FrameEnd( void )
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

    // Commit per-frame totals into ring buffer; compute p50 / p99
    static float scratch[RING_SIZE]; // single-threaded — safe to be static
    for ( int i = 0; i < m_markerCount; ++i )
    {
        Marker& m = m_markers[i];
        float ms = static_cast<float>( m.accumSecondsThisFrame * 1000.0 );
        m.lastFrameMs = ms;
        m.ringMs[m.ringHead] = ms;
        m.ringHead = ( m.ringHead + 1 ) % RING_SIZE;
        if ( m.ringFilled < RING_SIZE )
        {
            ++m.ringFilled;
        }

        // p50 / p99 from ring buffer (frame-accurate)
        int n = m.ringFilled;
        if ( n > 0 )
        {
            std::memcpy( scratch, m.ringMs, sizeof( float ) * static_cast<size_t>( n ) );
            int p50i = n / 2;
            int p99i = ( n * 99 ) / 100;
            if ( p99i >= n )
            {
                p99i = n - 1;
            }
            std::nth_element( scratch, scratch + p50i, scratch + n );
            m.p50Ms = scratch[p50i];
            std::nth_element( scratch, scratch + p99i, scratch + n );
            m.p99Ms = scratch[p99i];
        }
    }

    // Moving average refreshed every 500 ms
    LARGE_INTEGER t;
    QueryPerformanceCounter( &t );
    int64_t elapsedMs = ( t.QuadPart - m_lastAvgTicks ) * 1000 / m_qpcFrequency;
    if ( m_lastAvgTicks == 0 || elapsedMs >= TICKS_PER_AVG_REFRESH_MS )
    {
        m_lastAvgTicks = t.QuadPart;
        for ( int i = 0; i < m_markerCount; ++i )
        {
            Marker& m = m_markers[i];
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

void Profiler::RenderOverlay( float xLeft, float yAnchor, float lineHeight, float fSize, float fps ) const
{
    using SkullbonezCore::Text::Text2d;

    // Anchor from the bottom up so the panel sits flush in the bottom-left corner.
    // padY is the equal margin applied above the topmost line and below the bottom line.
    const float padX = fSize * 0.6f;
    const float padY = lineHeight * 1.2f;
    const float panelW = fSize * 28.0f;
    const float rowsHeight = (float)( m_markerCount + 1 ) * lineHeight;

    // yBottom is the baseline of the lowest text row; yTop is derived from it.
    const float yBottom = yAnchor + padY;
    const float yTop = yBottom + rowsHeight;

    Text2d::Render2dQuad( xLeft - padX,
                          yBottom,
                          xLeft - padX + panelW,
                          yTop + padY,
                          0.12f,
                          0.12f,
                          0.12f,
                          0.5f );

    // Look up Frame and VsyncWait averages to derive CPU processing time.
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

    // Draw header at top, then each marker row stepping downward.
    float y = yTop;
    Text2d::Render2dText( xLeft, y, fSize, "CPU: %.2f ms  FPS: %.1f", cpuMs, fps );
    y -= lineHeight;

    // Walk markers in registration order. Because FrameBegin always registers "Frame"
    // first and parents are registered before their children (PROFILE_BEGIN must occur
    // textually inside the parent's Begin/End range), this order yields a sane top-down
    // tree dump indented by depth.
    for ( int i = 0; i < m_markerCount; ++i )
    {
        const Marker& m = m_markers[i];
        char indent[64] = { 0 };
        int spaces = m.depth * 2;
        if ( spaces > 31 )
        {
            spaces = 31;
        }
        for ( int k = 0; k < spaces; ++k )
        {
            indent[k] = ' ';
        }
        indent[spaces] = '\0';

        // Use leaf name in display so deep paths don't push other columns off-screen
        Text2d::Render2dText( xLeft, y, fSize, "%s%-12s %6.2f ms  [P50:%.2f P99:%.2f]", indent, m.leafName, m.avgMs, m.p50Ms, m.p99Ms );
        y -= lineHeight;
    }
}

#endif // SKULLBONEZ_PROFILE_ENABLED
