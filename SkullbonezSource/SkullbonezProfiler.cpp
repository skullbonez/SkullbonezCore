/*-----------------------------------------------------------------------------------
    SkullbonezProfiler implementation
-----------------------------------------------------------------------------------*/

#include "SkullbonezProfiler.h"
#include "SkullbonezCommon.h"
#include <cstdio>
#include <algorithm>

namespace SkullbonezCore::Basics
{

/* -- SINGLETON ACCESSOR -----------------------------------------------------------*/

SkullbonezProfiler& SkullbonezProfiler::Instance()
{
    static SkullbonezProfiler instance;
    return instance;
}

/* -- CONSTRUCTOR / DESTRUCTOR ----------------------------------------------------*/

SkullbonezProfiler::SkullbonezProfiler()
    : m_nextScopeId( 0 )
    , m_frameStartTick( 0 )
    , m_csvFile( nullptr, &fclose )
    , m_csvHeaderWritten( false )
    , m_frameCount( 0 )
    , m_ticksToMs( 0.0 )
{
#if SKULLBONEZ_PROFILING_ENABLED
    m_frameScopes.reserve( 32 );
    m_activeScopes.reserve( 16 );
    m_ticksToMs = 1000.0 / GetTickFrequency();
#endif
}

SkullbonezProfiler::~SkullbonezProfiler()
{
    // CSV file auto-closes via unique_ptr deleter
}

/* -- FRAME LIFECYCLE -------------------------------------------------------------*/

void SkullbonezProfiler::FrameStart()
{
#if SKULLBONEZ_PROFILING_ENABLED
    m_frameScopes.clear();
    m_frameStartTick = GetPerformanceCounter();
#endif
}

void SkullbonezProfiler::FrameEnd()
{
#if SKULLBONEZ_PROFILING_ENABLED
    UpdateHUDText();
    WriteCSVRow();
    m_frameCount++;
#endif
}

/* -- SCOPE TIMING ----------------------------------------------------------------*/

uint32_t SkullbonezProfiler::BeginScope( const char* scopeName )
{
#if SKULLBONEZ_PROFILING_ENABLED
    uint32_t scopeId = m_nextScopeId++;
    m_activeScopes.push_back( { scopeId, scopeName, GetPerformanceCounter() } );
    return scopeId;
#else
    return 0;
#endif
}

void SkullbonezProfiler::EndScope( uint32_t scopeId )
{
#if SKULLBONEZ_PROFILING_ENABLED
    unsigned long long endTick = GetPerformanceCounter();

    // Find and remove the matching active scope (stack-like)
    for( auto it = m_activeScopes.rbegin(); it != m_activeScopes.rend(); ++it )
    {
        if( it->id == scopeId )
        {
            double ms = ( static_cast<double>( endTick - it->startTick ) ) * m_ticksToMs;
            m_frameScopes.push_back( { it->name, ms } );
            m_activeScopes.erase( std::next( it ).base() );
            return;
        }
    }
#endif
}

/* -- CSV LOGGING -----------------------------------------------------------------*/

void SkullbonezProfiler::SetCSVLogPath( const char* path )
{
#if SKULLBONEZ_PROFILING_ENABLED
    if( !path || !*path )
        return;

    m_csvPath = path;
    m_csvFile.reset( nullptr );
    m_csvHeaderWritten = false;

    // Open file in append mode
    FILE* f = nullptr;
    if( fopen_s( &f, path, "a" ) == 0 && f )
    {
        m_csvFile.reset( f );

        // Check if file is empty (no header yet)
        fseek( f, 0, SEEK_END );
        if( ftell( f ) == 0 )
        {
            m_csvHeaderWritten = false;
        }
        else
        {
            m_csvHeaderWritten = true; // Assume header exists
        }
    }
#endif
}

void SkullbonezProfiler::WriteCSVHeader()
{
#if SKULLBONEZ_PROFILING_ENABLED
    if( !m_csvFile || m_csvHeaderWritten )
        return;

    fprintf( m_csvFile.get(),
             "frame,physics_total,gravity,collision,response,render_total,reflection,"
             "skybox,models,terrain,shadows,water,text_render,input,camera,swap_ms\n" );
    fflush( m_csvFile.get() );
    m_csvHeaderWritten = true;
#endif
}

void SkullbonezProfiler::WriteCSVRow()
{
#if SKULLBONEZ_PROFILING_ENABLED
    if( !m_csvFile )
    {
        // Auto-open CSV if path was set but file not yet opened
        if( !m_csvPath.empty() )
        {
            SetCSVLogPath( m_csvPath.c_str() );
        }
        if( !m_csvFile )
            return;
    }

    WriteCSVHeader();

    // Build a map of scope names to timings
    std::vector<std::pair<const char*, double>> scopeMap;
    for( const auto& scope : m_frameScopes )
    {
        // Accumulate duplicate scope names (in case a scope appears multiple times per frame)
        auto it = std::find_if( scopeMap.begin(), scopeMap.end(),
                                [scope]( const auto& p ) { return p.first == scope.name; } );
        if( it != scopeMap.end() )
            it->second += scope.ms;
        else
            scopeMap.push_back( { scope.name, scope.ms } );
    }

    // Helper to get scope timing by name
    auto getScope = [&scopeMap]( const char* name ) -> double
    {
        auto it = std::find_if( scopeMap.begin(), scopeMap.end(),
                                [name]( const auto& p ) { return p.first == name; } );
        return ( it != scopeMap.end() ) ? it->second : 0.0;
    };

    // Write CSV row: frame + scope timings
    fprintf( m_csvFile.get(), "%u,%f,%f,%f,%f,%f,%f,%f,%f,%f,%f,%f,%f,%f,%f,%f\n", m_frameCount,
             getScope( "PhysicsTotal" ), getScope( "Gravity" ), getScope( "Collision" ),
             getScope( "Response" ), getScope( "RenderTotal" ), getScope( "RenderReflection" ),
             getScope( "RenderSkybox" ), getScope( "RenderGameModels" ),
             getScope( "RenderTerrain" ), getScope( "RenderShadows" ), getScope( "RenderWater" ),
             getScope( "TextRender" ), getScope( "InputProcessing" ), getScope( "CameraUpdate" ),
             getScope( "SwapBuffers" ) );
    fflush( m_csvFile.get() );
#endif
}

/* -- HUD TEXT GENERATION ---------------------------------------------------------*/

void SkullbonezProfiler::UpdateHUDText()
{
#if SKULLBONEZ_PROFILING_ENABLED
    m_hudText.clear();

    // Sort frame scopes by time (descending) for display
    std::vector<ScopeData> sorted = m_frameScopes;
    std::sort( sorted.begin(), sorted.end(),
               [](const ScopeData& a, const ScopeData& b) { return a.ms > b.ms; } );

    // Build HUD string: top N scopes with highest times
    m_hudText = "PERF (ms)\n";
    for( const auto& scope : sorted )
    {
        char buf[128];
        snprintf( buf, sizeof( buf ), "  %s: %.2f\n", scope.name, scope.ms );
        m_hudText += buf;
    }
#endif
}

/* -- PERFORMANCE COUNTER (WINDOWS) -----------------------------------------------*/

unsigned long long SkullbonezProfiler::GetPerformanceCounter()
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

/* -- CLEANUP -------------------------------------------------------------------*/

void SkullbonezProfiler::Destroy()
{
#if SKULLBONEZ_PROFILING_ENABLED
    m_csvFile.reset( nullptr );
    m_frameScopes.clear();
    m_activeScopes.clear();
    m_hudText.clear();
#endif
}

} // namespace SkullbonezCore::Basics
