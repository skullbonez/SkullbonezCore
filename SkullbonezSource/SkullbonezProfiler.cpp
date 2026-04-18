/*-----------------------------------------------------------------------------------
    SkullbonezProfiler implementation
-----------------------------------------------------------------------------------*/

#include "SkullbonezProfiler.h"

namespace SkullbonezCore
{
namespace Basics
{


/* -- SINGLETON ---------------------------------------------------------------*/

SkullbonezProfiler& SkullbonezProfiler::Instance()
{
    static SkullbonezProfiler instance;
    return instance;
}


/* -- CONSTRUCTOR / DESTRUCTOR ------------------------------------------------*/

SkullbonezProfiler::SkullbonezProfiler()
    : m_scopeCount( 0 )
    , m_stackDepth( 0 )
    , m_accumFrameCount( 0 )
    , m_lastSnapshotTick( 0 )
    , m_isHudVisible( false )
    , m_fps( 0.0f )
    , m_modelCount( 0 )
    , m_csvFile( nullptr )
    , m_csvHeaderWritten( false )
    , m_csvPass( 1 )
    , m_csvFrameCount( 0 )
    , m_ticksToMs( 0.0 )
{
#if SKULLBONEZ_PROFILING_ENABLED
    m_ticksToMs = 1000.0 / GetTickFrequency();
    memset( m_frameScopeMs, 0, sizeof( m_frameScopeMs ) );
    memset( m_accumMs, 0, sizeof( m_accumMs ) );
    memset( m_displayMs, 0, sizeof( m_displayMs ) );
    m_hudText[0] = '\0';
#endif
}

SkullbonezProfiler::~SkullbonezProfiler()
{
    if ( m_csvFile )
    {
        fclose( m_csvFile );
        m_csvFile = nullptr;
    }
}


/* -- FRAME LIFECYCLE ---------------------------------------------------------*/

void SkullbonezProfiler::FrameStart()
{
#if SKULLBONEZ_PROFILING_ENABLED
    memset( m_frameScopeMs, 0, sizeof( m_frameScopeMs ) );
    m_stackDepth = 0;

    if ( m_lastSnapshotTick == 0 )
    {
        m_lastSnapshotTick = GetTick();
    }
#endif
}

void SkullbonezProfiler::FrameEnd()
{
#if SKULLBONEZ_PROFILING_ENABLED
    // Assert no unclosed scopes
    if ( m_stackDepth != 0 )
    {
        char msg[256];
        sprintf_s( msg, sizeof( msg ), "PROFILER: %d unclosed scope(s) at FrameEnd. Stack:", m_stackDepth );
        OutputDebugStringA( msg );
        for ( int i = m_stackDepth - 1; i >= 0; --i )
        {
            OutputDebugStringA( "  " );
            OutputDebugStringA( m_scopes[m_scopeStack[i]].name );
        }
        ProfilerFail( msg );
    }

    // Accumulate for rolling average
    for ( int i = 0; i < m_scopeCount; ++i )
    {
        m_accumMs[i] += m_frameScopeMs[i];
    }
    ++m_accumFrameCount;

    // Snapshot every 500ms
    unsigned long long now = GetTick();
    double elapsedMs = (double)( now - m_lastSnapshotTick ) * m_ticksToMs;
    if ( elapsedMs >= 500.0 )
    {
        if ( m_accumFrameCount > 0 )
        {
            for ( int i = 0; i < m_scopeCount; ++i )
            {
                m_displayMs[i] = m_accumMs[i] / m_accumFrameCount;
            }
        }
        memset( m_accumMs, 0, sizeof( m_accumMs ) );
        m_accumFrameCount = 0;
        m_lastSnapshotTick = now;
        UpdateHUDText();
    }

    // Write CSV row
    if ( m_csvFile )
    {
        WriteCSVRow();
    }
#endif
}


/* -- SCOPE TIMING ------------------------------------------------------------*/

int SkullbonezProfiler::BeginScope( const char* name )
{
#if SKULLBONEZ_PROFILING_ENABLED
    int idx = FindOrAddScope( name );

    if ( m_stackDepth >= PROFILER_MAX_STACK )
    {
        ProfilerFail( "PROFILER: scope stack overflow" );
    }

    m_scopeStack[m_stackDepth++] = idx;
    m_scopes[idx].startTick = GetTick();
    return idx;
#else
    (void)name;
    return -1;
#endif
}

void SkullbonezProfiler::EndScope( const char* name )
{
#if SKULLBONEZ_PROFILING_ENABLED
    unsigned long long endTick = GetTick();

    if ( m_stackDepth <= 0 )
    {
        char msg[256];
        sprintf_s( msg, sizeof( msg ), "PROFILER: EndScope(\"%s\") with empty stack", name );
        ProfilerFail( msg );
    }

    int topIdx = m_scopeStack[m_stackDepth - 1];
    if ( strcmp( m_scopes[topIdx].name, name ) != 0 )
    {
        char msg[256];
        sprintf_s( msg, sizeof( msg ), "PROFILER: EndScope(\"%s\") but top of stack is \"%s\"", name, m_scopes[topIdx].name );
        ProfilerFail( msg );
    }

    --m_stackDepth;
    double ms = (double)( endTick - m_scopes[topIdx].startTick ) * m_ticksToMs;
    m_frameScopeMs[topIdx] += ms;
#else
    (void)name;
#endif
}

int SkullbonezProfiler::FindOrAddScope( const char* name )
{
#if SKULLBONEZ_PROFILING_ENABLED
    for ( int i = 0; i < m_scopeCount; ++i )
    {
        if ( strcmp( m_scopes[i].name, name ) == 0 )
        {
            return i;
        }
    }

    if ( m_scopeCount >= PROFILER_MAX_SCOPES )
    {
        ProfilerFail( "PROFILER: too many scopes" );
    }

    int idx = m_scopeCount++;
    strcpy_s( m_scopes[idx].name, sizeof( m_scopes[idx].name ), name );
    m_scopes[idx].startTick = 0;
    m_frameScopeMs[idx] = 0.0;
    m_accumMs[idx] = 0.0;
    m_displayMs[idx] = 0.0;
    return idx;
#else
    (void)name;
    return -1;
#endif
}


/* -- CSV OUTPUT --------------------------------------------------------------*/

void SkullbonezProfiler::OpenCSV( const char* path, int pass )
{
#if SKULLBONEZ_PROFILING_ENABLED
    if ( m_csvFile )
    {
        fclose( m_csvFile );
    }

    const char* mode = ( pass == 0 ) ? "w" : "a";
    fopen_s( &m_csvFile, path, mode );
    m_csvPass = pass + 1;
    m_csvFrameCount = 0;

    if ( pass == 0 )
    {
        m_csvHeaderWritten = false;
    }
#else
    (void)path;
    (void)pass;
#endif
}

void SkullbonezProfiler::CloseCSV()
{
#if SKULLBONEZ_PROFILING_ENABLED
    if ( m_csvFile )
    {
        fclose( m_csvFile );
        m_csvFile = nullptr;
    }
#endif
}

void SkullbonezProfiler::WriteCSVRow()
{
#if SKULLBONEZ_PROFILING_ENABLED
    if ( !m_csvFile || m_scopeCount == 0 )
    {
        return;
    }

    // Write header on first row
    if ( !m_csvHeaderWritten )
    {
        fprintf( m_csvFile, "pass,frame" );
        for ( int i = 0; i < m_scopeCount; ++i )
        {
            fprintf( m_csvFile, ",%s", m_scopes[i].name );
        }
        fprintf( m_csvFile, "\n" );
        m_csvHeaderWritten = true;
    }

    ++m_csvFrameCount;
    fprintf( m_csvFile, "%d,%d", m_csvPass, m_csvFrameCount );
    for ( int i = 0; i < m_scopeCount; ++i )
    {
        fprintf( m_csvFile, ",%.4f", m_frameScopeMs[i] );
    }
    fprintf( m_csvFile, "\n" );
    fflush( m_csvFile );
#endif
}


/* -- HUD TEXT ----------------------------------------------------------------*/

void SkullbonezProfiler::UpdateHUDText()
{
#if SKULLBONEZ_PROFILING_ENABLED
    char* p = m_hudText;
    int remaining = PROFILER_HUD_BUF;

    auto append = [&]( const char* fmt, ... )
    {
        va_list args;
        va_start( args, fmt );
        int written = vsprintf_s( p, remaining, fmt, args );
        va_end( args );
        if ( written > 0 )
        {
            p += written;
            remaining -= written;
        }
    };

    auto getMs = [this]( const char* name ) -> double
    {
        for ( int i = 0; i < m_scopeCount; ++i )
        {
            if ( strcmp( m_scopes[i].name, name ) == 0 )
                return m_displayMs[i];
        }
        return 0.0;
    };

    append( "FPS: %.1f   Models: %d\n", m_fps, m_modelCount );
    append( "-----------------------------\n" );

    append( "Physics       %6.2f ms\n", getMs( "PhysicsTotal" ) );
    append( "  Gravity     %6.2f ms\n", getMs( "Gravity" ) );
    append( "  Broadphase  %6.2f ms\n", getMs( "Broadphase" ) );
    append( "  NarrowPhase %6.2f ms\n", getMs( "NarrowPhase" ) );
    append( "  TerrainCol  %6.2f ms\n", getMs( "TerrainCollision" ) );
    append( "\n" );

    append( "Render        %6.2f ms\n", getMs( "RenderTotal" ) );
    append( "  Skybox      %6.2f ms\n", getMs( "RenderSkybox" ) );
    append( "  Reflection  %6.2f ms\n", getMs( "RenderReflection" ) );
    append( "  Models      %6.2f ms\n", getMs( "RenderGameModels" ) );
    append( "  Terrain     %6.2f ms\n", getMs( "RenderTerrain" ) );
    append( "  Shadows     %6.2f ms\n", getMs( "RenderShadows" ) );
    append( "  Water       %6.2f ms\n", getMs( "RenderWater" ) );
    append( "Text          %6.2f ms\n", getMs( "TextRender" ) );
    append( "\n" );

    append( "Camera        %6.2f ms\n", getMs( "CameraUpdate" ) );
    append( "Swap          %6.2f ms\n", getMs( "SwapBuffers" ) );
#endif
}


/* -- PERFORMANCE COUNTER (WINDOWS) -------------------------------------------*/

unsigned long long SkullbonezProfiler::GetTick()
{
    LARGE_INTEGER counter;
    QueryPerformanceCounter( &counter );
    return counter.QuadPart;
}

double SkullbonezProfiler::GetTickFrequency()
{
    LARGE_INTEGER freq;
    QueryPerformanceFrequency( &freq );
    return static_cast<double>( freq.QuadPart );
}


/* -- FAILURE HANDLER (works even with NDEBUG defined) ------------------------*/

void SkullbonezProfiler::ProfilerFail( const char* msg )
{
    OutputDebugStringA( msg );
    OutputDebugStringA( "\n" );
    __debugbreak();
}


/* -- CLEANUP -----------------------------------------------------------------*/

void SkullbonezProfiler::Destroy()
{
#if SKULLBONEZ_PROFILING_ENABLED
    CloseCSV();
    m_hudText[0] = '\0';
#endif
}

} // namespace Basics
} // namespace SkullbonezCore
