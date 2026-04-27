/*------------------------------------------------------------------------------------------------------------------------------------------------------------------------
                                                                          THE SKULLBONEZ CORE
                                                                          ::: PROFILER :::
-------------------------------------------------------------------------------------------------------------------------------------------------------------------------*/

#ifndef SKULLBONEZ_PROFILER_H
#define SKULLBONEZ_PROFILER_H

#include "SkullbonezCommon.h"

namespace SkullbonezCore
{
namespace Basics
{
/* -- Profiler ---------------------------------------------------------------------------------------------------------------------------------------------------

    Hierarchical CPU sampling profiler. Markers are identified by a string literal whose path encodes
    the tree position, e.g. "Render/Reflection/Skybox". Hash collisions and BEGIN/END mismatches
    abort immediately. All public methods are no-ops when SKULLBONEZ_PROFILE_ENABLED is undefined.

    Use the macros (PROFILE_BEGIN / PROFILE_END / PROFILE_SCOPED) — never call methods directly.
-----------------------------------------------------------------------------------------------------------------------------------------------------------------*/
class Profiler
{
  public:
    static constexpr int MAX_MARKERS = 64;
    static constexpr int MAX_DEPTH = 16;
    static constexpr int RING_SIZE = 600; // ~10 s @ 60 fps

    struct Marker
    {
        const char* name;       // full path literal, e.g. "Render/Skybox"
        const char* leafName;   // pointer into name after last '/'
        uint32_t hash;          // FNV-1a of full path
        int parentIndex;        // -1 if top-level (parent of "Render/Skybox" is "Render")
        int depth;              // count of '/' characters (0 = top)
        int openCount;          // recursion guard (must be 0 at frame end)
        int64_t openStartTicks; // QPC ticks at most recent Begin
        double accumSecondsThisFrame;
        float ringMs[RING_SIZE]; // last RING_SIZE finished-frame totals
        int ringFilled;          // number of valid samples (saturates at RING_SIZE)
        int ringHead;            // next write index
        float lastFrameMs;       // most recent finished-frame total
        float avgMs;             // moving average refreshed every 500 ms
        float p50Ms;             // recomputed every frame
        float p99Ms;             // recomputed every frame
        float p99_9Ms;           // recomputed every frame (for perf CSV)
        float minMs;             // minimum across ring buffer
        float maxMs;             // maximum across ring buffer
    };

    static Profiler& Instance( void );

    void Begin( const char* fullPath, uint32_t hash );
    void End( const char* fullPath, uint32_t hash );

    void FrameBegin( void );
    void FrameEnd( void ); // commits per-frame totals; recomputes p50/p99; refreshes moving avg every 500 ms

    int MarkerCount( void ) const
    {
        return m_markerCount;
    }
    const Marker& GetMarker( int i ) const
    {
        return m_markers[i];
    }

    // Accessor for back-compat perf logging (returns last finished-frame total ms; 0 if marker missing)
    float LastFrameMsByHash( uint32_t hash ) const;

    // Perf CSV helpers: write header (once, pass 1) and one row per frame.
    // Include <cstdio> before calling; FILE* must be open for writing.
    void WritePerfCSVHeader( FILE* f ) const;
    void WritePerfCSVRow( FILE* f, int pass, int frame ) const;

    // Renders the indented overlay using Text2d::Render2dText. Caller decides toggle state.
    // xLeft / yTop in the same frustum-unit space used elsewhere; lineHeight in same space; fSize for Text2d.
    void RenderOverlay( float xLeft, float yAnchor, float lineHeight, float fSize, float fps ) const;

  private:
    Profiler( void );
    Profiler( const Profiler& ) = delete;
    Profiler& operator=( const Profiler& ) = delete;

    int FindOrRegister( const char* fullPath, uint32_t hash );
    void AbortMismatch( const char* msg, const char* details ) const;

    Marker m_markers[MAX_MARKERS];
    int m_markerCount;

    int m_stackIndices[MAX_DEPTH]; // marker indices currently open (top of stack at [m_stackTop-1])
    int m_stackTop;

    int64_t m_qpcFrequency;
    int64_t m_lastAvgTicks;
    bool m_inFrame;
};

/* -- Profiler scope helper for PROFILE_SCOPED ----------------------------------------------------------------------*/
class ProfilerScope
{
  public:
    ProfilerScope( const char* fullPath, uint32_t hash )
        : m_fullPath( fullPath ), m_hash( hash )
    {
        Profiler::Instance().Begin( m_fullPath, m_hash );
    }
    ~ProfilerScope( void )
    {
        Profiler::Instance().End( m_fullPath, m_hash );
    }
    ProfilerScope( const ProfilerScope& ) = delete;
    ProfilerScope& operator=( const ProfilerScope& ) = delete;

  private:
    const char* m_fullPath;
    uint32_t m_hash;
};

} // namespace Basics
} // namespace SkullbonezCore

/* -- PROFILER MACROS -----------------------------------------------------------------------------------------------*/
#if defined( SKULLBONEZ_PROFILE_ENABLED )

#define PROFILE_PASTE_INNER( a, b ) a##b
#define PROFILE_PASTE( a, b ) PROFILE_PASTE_INNER( a, b )

#define PROFILE_BEGIN( name )                                                                             \
    do                                                                                                    \
    {                                                                                                     \
        constexpr uint32_t PROFILE_PASTE( _profH_, __LINE__ ) = ::HashStr( name );                        \
        ::SkullbonezCore::Basics::Profiler::Instance().Begin( name, PROFILE_PASTE( _profH_, __LINE__ ) ); \
    } while ( 0 )

#define PROFILE_END( name )                                                                             \
    do                                                                                                  \
    {                                                                                                   \
        constexpr uint32_t PROFILE_PASTE( _profH_, __LINE__ ) = ::HashStr( name );                      \
        ::SkullbonezCore::Basics::Profiler::Instance().End( name, PROFILE_PASTE( _profH_, __LINE__ ) ); \
    } while ( 0 )

#define PROFILE_SCOPED( name )                                                  \
    constexpr uint32_t PROFILE_PASTE( _profSH_, __LINE__ ) = ::HashStr( name ); \
    ::SkullbonezCore::Basics::ProfilerScope PROFILE_PASTE( _profS_, __LINE__ )( name, PROFILE_PASTE( _profSH_, __LINE__ ) )

#define PROFILE_FRAME_BEGIN() ::SkullbonezCore::Basics::Profiler::Instance().FrameBegin()
#define PROFILE_FRAME_END() ::SkullbonezCore::Basics::Profiler::Instance().FrameEnd()

#else // SKULLBONEZ_PROFILE_ENABLED

#define PROFILE_BEGIN( name ) ( (void)0 )
#define PROFILE_END( name ) ( (void)0 )
#define PROFILE_SCOPED( name ) ( (void)0 )
#define PROFILE_FRAME_BEGIN() ( (void)0 )
#define PROFILE_FRAME_END() ( (void)0 )

#endif // SKULLBONEZ_PROFILE_ENABLED

#endif // SKULLBONEZ_PROFILER_H
