/*-----------------------------------------------------------------------------------
    SkullbonezProfiler — Performance profiling system with scope-based timing,
    CSV logging, and real-time HUD overlay.

    Usage (RAII-based):
        {
            ProfileScope ps("PhysicsGravity");
            // code here is timed
        } // timing recorded automatically

    When SKULLBONEZ_PROFILING_ENABLED=0, all profiling code compiles to no-ops.

-----------------------------------------------------------------------------------*/

#ifndef SKULLBONEZ_PROFILER_H
#define SKULLBONEZ_PROFILER_H

#include <cstdint>
#include <string>
#include <vector>
#include <memory>

namespace SkullbonezCore::Basics
{

/* -- PROFILER CORE ---------------------------------------------------------------*/

class SkullbonezProfiler
{
public:
    // Singleton accessor
    static SkullbonezProfiler& Instance();

    // Per-frame lifecycle
    void FrameStart();
    void FrameEnd();

    // Scope timing (opaque handle)
    uint32_t BeginScope( const char* scopeName );
    void     EndScope( uint32_t scopeId );

    // HUD overlay text (updated each FrameEnd)
    const char* GetHUDText() const { return m_hudText.c_str(); }

    // Initialize with CSV file path (optional; called auto if not set)
    void SetCSVLogPath( const char* path );

    // Destroy resources (called before GL context destruction)
    void Destroy();

private:
    SkullbonezProfiler();
    ~SkullbonezProfiler();

    // Delete copy/move
    SkullbonezProfiler( const SkullbonezProfiler& )            = delete;
    SkullbonezProfiler& operator=( const SkullbonezProfiler& ) = delete;
    SkullbonezProfiler( SkullbonezProfiler&& )                 = delete;
    SkullbonezProfiler& operator=( SkullbonezProfiler&& )      = delete;

    struct ScopeData
    {
        const char* name;
        double      ms;
    };

    struct ActiveScope
    {
        uint32_t        id;
        const char*     name;
        unsigned long long startTick;
    };

    // Per-frame data
    std::vector<ScopeData>     m_frameScopes;
    std::vector<ActiveScope>   m_activeScopes;
    uint32_t                   m_nextScopeId;
    unsigned long long         m_frameStartTick;

    // HUD text (reused across frames)
    std::string m_hudText;

    // CSV logging
    std::unique_ptr<FILE, decltype( &fclose )> m_csvFile;
    std::string m_csvPath;
    bool        m_csvHeaderWritten;
    uint32_t    m_frameCount;

    // Frequency scaling for tick->ms conversion
    double m_ticksToMs;

    // Helpers
    void WriteCSVHeader();
    void WriteCSVRow();
    void UpdateHUDText();
    unsigned long long GetPerformanceCounter();
    double             GetTickFrequency();
};

/* -- RAII SCOPE GUARD (no-op when profiling disabled) ---------------------------*/

class ProfileScope
{
public:
#if SKULLBONEZ_PROFILING_ENABLED
    explicit ProfileScope( const char* scopeName )
        : m_scopeId( SkullbonezProfiler::Instance().BeginScope( scopeName ) )
    {
    }

    ~ProfileScope() { SkullbonezProfiler::Instance().EndScope( m_scopeId ); }

private:
    uint32_t m_scopeId;
#else
    explicit ProfileScope( const char* /*scopeName*/ ) {}
    ~ProfileScope() {}
#endif
};

} // namespace SkullbonezCore::Basics

#endif // SKULLBONEZ_PROFILER_H
