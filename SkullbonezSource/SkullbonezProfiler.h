#pragma once


// --- Includes ---
#include "SkullbonezCommon.h"

namespace SkullbonezCore
{
namespace Basics
{
/* -- Profiler ---------------------------------------------------------------------------------------------------------------------------------------------------

    Hierarchical CPU + GPU sampling profiler. Markers are identified by a string literal whose path
    encodes the tree position, e.g. "Render/Reflection/Skybox". Hash collisions and BEGIN/END
    mismatches abort immediately. All public methods are no-ops when SKULLBONEZ_PROFILE_ENABLED is
    undefined.

    CPU timing uses QueryPerformanceCounter (wall-clock).
    GPU timing uses GL_TIMESTAMP queries (double-buffered, non-blocking readback).

    Use the macros:
      PROFILE_BEGIN / PROFILE_END / PROFILE_SCOPED         — CPU-only timing
      PROFILE_GPU_BEGIN / PROFILE_GPU_END / PROFILE_GPU_SCOPED — CPU + GPU timing

    Never call methods directly.
-----------------------------------------------------------------------------------------------------------------------------------------------------------------*/
class Profiler
{
  public:
    static constexpr int MAX_MARKERS = 64;
    static constexpr int MAX_DEPTH = 16;
    static constexpr int RING_SIZE = 600;     // ~10 s @ 60 fps
    static constexpr int GPU_QUERY_DEPTH = 4; // pending query ring depth (non-blocking readback)

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
        float minMs;             // session-wide minimum
        float maxMs;             // session-wide maximum

        // GPU timestamp query state
        bool hasGpu;                           // true if this marker uses GPU timing
        bool gpuAllocated;                     // true if glGenQueries has been called
        bool gpuWrittenThisFrame;              // set by GpuBegin, cleared at FrameEnd
        GLuint gpuQueries[GPU_QUERY_DEPTH][2]; // [slot][0=begin, 1=end] timestamp query IDs
        int gpuWriteCursor;                    // next write slot (mod GPU_QUERY_DEPTH)
        int gpuReadCursor;                     // oldest unread slot (mod GPU_QUERY_DEPTH)
        float gpuLastFrameMs;                  // most recent GPU sample
        float gpuAvgMs;                        // GPU moving average
        float gpuRingMs[RING_SIZE];            // GPU ring buffer
        int gpuRingFilled;
        int gpuRingHead;
    };

    static Profiler& Instance();

    void Begin( const char* fullPath, uint32_t hash );
    void End( const char* fullPath, uint32_t hash );

    void GpuBegin( const char* fullPath, uint32_t hash );
    void GpuEnd( const char* fullPath, uint32_t hash );

    void FrameBegin();
    void FrameEnd(); // commits per-frame totals; recomputes p50/p99; refreshes moving avg every 500 ms

    // Call when GL context is destroyed/recreated to invalidate all GPU query state
    void InvalidateGpuQueries();

    int MarkerCount() const
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
    Profiler();
    Profiler( const Profiler& ) = delete;
    Profiler& operator=( const Profiler& ) = delete;

    int FindOrRegister( const char* fullPath, uint32_t hash );
    void AbortMismatch( const char* msg, const char* details ) const;
    void ReadPendingGpuResults();
    void AdvanceGpuWriteCursors();

    Marker m_markers[MAX_MARKERS];
    int m_markerCount;

    int m_stackIndices[MAX_DEPTH]; // marker indices currently open (top of stack at [m_stackTop-1])
    int m_stackTop;

    int64_t m_qpcFrequency;
    int64_t m_lastAvgTicks;
    bool m_inFrame;
};

class ProfilerScope
{
  public:
    ProfilerScope( const char* fullPath, uint32_t hash )
        : m_fullPath( fullPath ), m_hash( hash )
    {
        Profiler::Instance().Begin( m_fullPath, m_hash );
    }
    ~ProfilerScope()
    {
        Profiler::Instance().End( m_fullPath, m_hash );
    }
    ProfilerScope( const ProfilerScope& ) = delete;
    ProfilerScope& operator=( const ProfilerScope& ) = delete;

  private:
    const char* m_fullPath;
    uint32_t m_hash;
};

class GpuProfilerScope
{
  public:
    GpuProfilerScope( const char* fullPath, uint32_t hash )
        : m_fullPath( fullPath ), m_hash( hash )
    {
        Profiler::Instance().Begin( m_fullPath, m_hash );
        Profiler::Instance().GpuBegin( m_fullPath, m_hash );
    }
    ~GpuProfilerScope()
    {
        Profiler::Instance().GpuEnd( m_fullPath, m_hash );
        Profiler::Instance().End( m_fullPath, m_hash );
    }
    GpuProfilerScope( const GpuProfilerScope& ) = delete;
    GpuProfilerScope& operator=( const GpuProfilerScope& ) = delete;

  private:
    const char* m_fullPath;
    uint32_t m_hash;
};

} // namespace Basics
} // namespace SkullbonezCore

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

#define PROFILE_GPU_BEGIN( name )                                                                            \
    do                                                                                                       \
    {                                                                                                        \
        constexpr uint32_t PROFILE_PASTE( _profH_, __LINE__ ) = ::HashStr( name );                           \
        ::SkullbonezCore::Basics::Profiler::Instance().Begin( name, PROFILE_PASTE( _profH_, __LINE__ ) );    \
        ::SkullbonezCore::Basics::Profiler::Instance().GpuBegin( name, PROFILE_PASTE( _profH_, __LINE__ ) ); \
    } while ( 0 )

#define PROFILE_GPU_END( name )                                                                            \
    do                                                                                                     \
    {                                                                                                      \
        constexpr uint32_t PROFILE_PASTE( _profH_, __LINE__ ) = ::HashStr( name );                         \
        ::SkullbonezCore::Basics::Profiler::Instance().GpuEnd( name, PROFILE_PASTE( _profH_, __LINE__ ) ); \
        ::SkullbonezCore::Basics::Profiler::Instance().End( name, PROFILE_PASTE( _profH_, __LINE__ ) );    \
    } while ( 0 )

#define PROFILE_GPU_SCOPED( name )                                              \
    constexpr uint32_t PROFILE_PASTE( _profSH_, __LINE__ ) = ::HashStr( name ); \
    ::SkullbonezCore::Basics::GpuProfilerScope PROFILE_PASTE( _profS_, __LINE__ )( name, PROFILE_PASTE( _profSH_, __LINE__ ) )

#define PROFILE_FRAME_BEGIN() ::SkullbonezCore::Basics::Profiler::Instance().FrameBegin()
#define PROFILE_FRAME_END() ::SkullbonezCore::Basics::Profiler::Instance().FrameEnd()

#else // SKULLBONEZ_PROFILE_ENABLED

#define PROFILE_BEGIN( name ) ( (void)0 )
#define PROFILE_END( name ) ( (void)0 )
#define PROFILE_SCOPED( name ) ( (void)0 )
#define PROFILE_GPU_BEGIN( name ) ( (void)0 )
#define PROFILE_GPU_END( name ) ( (void)0 )
#define PROFILE_GPU_SCOPED( name ) ( (void)0 )
#define PROFILE_FRAME_BEGIN() ( (void)0 )
#define PROFILE_FRAME_END() ( (void)0 )

#endif // SKULLBONEZ_PROFILE_ENABLED
