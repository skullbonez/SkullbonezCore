/*-----------------------------------------------------------------------------------
    SkullbonezProfiler — Scope-based performance profiler

    Macro API: PROFILE_BEGIN/END/SCOPED compile to nothing in Release.
    Active in Debug and Profile configs.  Uses QueryPerformanceCounter.
    Asserts on marker mismatch with __debugbreak() (works even with NDEBUG).
-----------------------------------------------------------------------------------*/

#ifndef SKULLBONEZ_PROFILER_H
#define SKULLBONEZ_PROFILER_H

#include "SkullbonezCommon.h"
#include <cstring>

/* -- LIMITS ------------------------------------------------------------------*/
static constexpr int PROFILER_MAX_SCOPES     = 32;
static constexpr int PROFILER_MAX_STACK      = 8;
static constexpr int PROFILER_HUD_BUF        = 2048;

namespace SkullbonezCore
{
namespace Basics
{

class SkullbonezProfiler
{
  public:
    static SkullbonezProfiler& Instance();

    // Frame lifecycle
    void FrameStart();
    void FrameEnd();

    // Scope timing
    int  BeginScope( const char* name );
    void EndScope( const char* name );

    // CSV output
    void OpenCSV( const char* path, int pass );
    void CloseCSV();

    // HUD
    void SetHUDVisible( bool visible ) { m_isHudVisible = visible; }
    void ToggleHUD()                   { m_isHudVisible = !m_isHudVisible; }
    bool IsHUDVisible() const          { return m_isHudVisible; }
    const char* GetHUDText() const     { return m_hudText; }
    void SetFPS( float fps )           { m_fps = fps; }
    void SetModelCount( int count )    { m_modelCount = count; }

    // Cleanup
    void Destroy();

  private:
    SkullbonezProfiler();
    ~SkullbonezProfiler();

    struct ScopeEntry
    {
        char name[64];
        unsigned long long startTick;
    };

    ScopeEntry m_scopes[PROFILER_MAX_SCOPES];
    int        m_scopeCount;

    // Per-frame timing (ms) indexed by scope
    double m_frameScopeMs[PROFILER_MAX_SCOPES];

    // Rolling average accumulation
    double             m_accumMs[PROFILER_MAX_SCOPES];
    int                m_accumFrameCount;
    double             m_displayMs[PROFILER_MAX_SCOPES];
    unsigned long long m_lastSnapshotTick;

    // Scope stack for mismatch detection
    int  m_stackDepth;
    int  m_scopeStack[PROFILER_MAX_STACK];

    // HUD
    bool  m_isHudVisible;
    float m_fps;
    int   m_modelCount;
    char  m_hudText[PROFILER_HUD_BUF];

    // CSV
    FILE*  m_csvFile;
    bool   m_csvHeaderWritten;
    int    m_csvPass;
    int    m_csvFrameCount;

    // Timing
    double m_ticksToMs;

    int  FindOrAddScope( const char* name );
    void WriteCSVRow();
    void UpdateHUDText();

    static unsigned long long GetTick();
    static double GetTickFrequency();
    static void ProfilerFail( const char* msg );
};

} // namespace Basics
} // namespace SkullbonezCore


/* -- RAII SCOPE GUARD --------------------------------------------------------*/
#if SKULLBONEZ_PROFILING_ENABLED

struct ProfileScope
{
    const char* m_name;
    ProfileScope( const char* name ) : m_name( name )
    {
        SkullbonezCore::Basics::SkullbonezProfiler::Instance().BeginScope( m_name );
    }
    ~ProfileScope()
    {
        SkullbonezCore::Basics::SkullbonezProfiler::Instance().EndScope( m_name );
    }
};

#define PROFILER_CONCAT2( a, b ) a##b
#define PROFILER_CONCAT( a, b ) PROFILER_CONCAT2( a, b )
#define PROFILER_UNIQUE PROFILER_CONCAT( _profileScope_, __LINE__ )

#define PROFILE_SCOPED( name )       ProfileScope PROFILER_UNIQUE( name )
#define PROFILE_BEGIN( name )        SkullbonezCore::Basics::SkullbonezProfiler::Instance().BeginScope( name )
#define PROFILE_END( name )          SkullbonezCore::Basics::SkullbonezProfiler::Instance().EndScope( name )
#define PROFILE_FRAME_START()        SkullbonezCore::Basics::SkullbonezProfiler::Instance().FrameStart()
#define PROFILE_FRAME_END()          SkullbonezCore::Basics::SkullbonezProfiler::Instance().FrameEnd()
#define PROFILE_OPEN_CSV( p, pass )  SkullbonezCore::Basics::SkullbonezProfiler::Instance().OpenCSV( p, pass )
#define PROFILE_CLOSE_CSV()          SkullbonezCore::Basics::SkullbonezProfiler::Instance().CloseCSV()
#define PROFILE_SET_HUD_VISIBLE( v ) SkullbonezCore::Basics::SkullbonezProfiler::Instance().SetHUDVisible( v )
#define PROFILE_TOGGLE_HUD()         SkullbonezCore::Basics::SkullbonezProfiler::Instance().ToggleHUD()
#define PROFILE_IS_HUD_VISIBLE()     SkullbonezCore::Basics::SkullbonezProfiler::Instance().IsHUDVisible()
#define PROFILE_GET_HUD()            SkullbonezCore::Basics::SkullbonezProfiler::Instance().GetHUDText()
#define PROFILE_SET_FPS( f )         SkullbonezCore::Basics::SkullbonezProfiler::Instance().SetFPS( f )
#define PROFILE_SET_MODELS( n )      SkullbonezCore::Basics::SkullbonezProfiler::Instance().SetModelCount( n )
#define PROFILE_DESTROY()            SkullbonezCore::Basics::SkullbonezProfiler::Instance().Destroy()

#else // Profiling disabled — everything compiles to nothing

#define PROFILE_SCOPED( name )       ((void)0)
#define PROFILE_BEGIN( name )        ((void)0)
#define PROFILE_END( name )          ((void)0)
#define PROFILE_FRAME_START()        ((void)0)
#define PROFILE_FRAME_END()          ((void)0)
#define PROFILE_OPEN_CSV( p, pass )  ((void)0)
#define PROFILE_CLOSE_CSV()          ((void)0)
#define PROFILE_SET_HUD_VISIBLE( v ) ((void)0)
#define PROFILE_TOGGLE_HUD()         ((void)0)
#define PROFILE_IS_HUD_VISIBLE()     false
#define PROFILE_GET_HUD()            ""
#define PROFILE_SET_FPS( f )         ((void)0)
#define PROFILE_SET_MODELS( n )      ((void)0)
#define PROFILE_DESTROY()            ((void)0)

#endif

#endif
