/*
File: SkullbonezSource/SkullbonezProfiler.h
Purpose:
  Records hierarchical CPU/GPU timing markers for runtime diagnostics.

Mental model:
  Runtime code connects authored scene data, input, simulation, render
  backends, and validation-oriented launch modes. Follow who owns state and
  when that state changes.

Glossary:
  GPU (Graphics Processing Unit): Processor that executes rendering, compute,
  and raytracing commands asynchronously from the CPU.
  CPU (Central Processing Unit): Host processor running engine code and
  recording GPU commands.
  CSV (Comma-Separated Values): Text table format used for byte-exact physics
  regression output.
  Validation gate: Repository script that proves a class of changes before
  commit or PR.

Related:
  - SkullbonezSource/SkullbonezProfiler.cpp
  - Agentic/Reference/runtime-reference.md
  - Agentic/Reference/comment-style-guide.md
*/
#pragma once


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
    GPU timing uses the active render backend's non-blocking timestamp readback.

    Use the macros:
      PROFILE_BEGIN / PROFILE_END / PROFILE_SCOPED         — CPU-only timing
      PROFILE_GPU_BEGIN / PROFILE_GPU_END / PROFILE_GPU_SCOPED — CPU + GPU timing

    Never call methods directly.
-----------------------------------------------------------------------------------------------------------------------------------------------------------------*/
class Profiler
{
  public:
    static constexpr int MAX_MARKERS = 128;
    static constexpr int MAX_DEPTH = 16;
    static constexpr int RING_SIZE = 600;     // ~10 s @ 60 fps
    static constexpr int GPU_QUERY_DEPTH = 4; // pending query ring depth (non-blocking readback)
    static constexpr int WARMUP_FRAMES = 30;  // frames excluded from ring-buffer stats at session/pass start

    // 20-colour palette for visual bar segments. Assigned round-robin to leaf markers.
    static constexpr int BAR_PALETTE_SIZE = 20;
    struct BarColor
    {
        float r, g, b;
    };
    static constexpr BarColor BAR_PALETTE[BAR_PALETTE_SIZE] = {
        { 0.90f, 0.30f, 0.30f }, // red
        { 0.30f, 0.75f, 0.93f }, // sky blue
        { 0.40f, 0.85f, 0.40f }, // green
        { 0.95f, 0.70f, 0.20f }, // amber
        { 0.70f, 0.40f, 0.90f }, // purple
        { 0.20f, 0.90f, 0.80f }, // teal
        { 0.95f, 0.50f, 0.70f }, // pink
        { 0.55f, 0.80f, 0.25f }, // lime
        { 0.30f, 0.50f, 0.95f }, // blue
        { 0.95f, 0.85f, 0.30f }, // yellow
        { 0.85f, 0.45f, 0.20f }, // orange
        { 0.50f, 0.90f, 0.60f }, // mint
        { 0.80f, 0.30f, 0.70f }, // magenta
        { 0.60f, 0.70f, 0.85f }, // steel
        { 0.90f, 0.60f, 0.40f }, // peach
        { 0.35f, 0.65f, 0.55f }, // sage
        { 0.75f, 0.55f, 0.85f }, // lavender
        { 0.65f, 0.85f, 0.75f }, // seafoam
        { 0.85f, 0.75f, 0.55f }, // tan
        { 0.45f, 0.45f, 0.80f }, // indigo
    };

    struct Marker
    {
        const char* name;       // full path literal, e.g. "Render/Skybox"
        const char* leafName;   // pointer into name after last '/'
        uint32_t hash;          // FNV-1a of full path
        int parentIndex;        // -1 if top-level (parent of "Render/Skybox" is "Render")
        int depth;              // count of '/' characters (0 = top)
        int colorIndex;         // index into BAR_PALETTE (assigned at registration for leaf markers, -1 otherwise)
        int openCount;          // recursion guard (must be 0 at frame end)
        int64_t openStartTicks; // QPC ticks at most recent Begin
        double accumSecondsThisFrame;
        double firstStartSecondsThisFrame;
        double lastEndSecondsThisFrame;
        bool spanWrittenThisFrame;
        float ringMs[RING_SIZE]; // last RING_SIZE finished-frame totals
        int ringFilled;          // number of valid samples (saturates at RING_SIZE)
        int ringHead;            // next write index
        float lastFrameMs;       // most recent finished-frame total
        float lastFrameStartMs;  // first Begin point within the most recent frame
        float lastFrameEndMs;    // final End point within the most recent frame
        float avgMs;             // moving average refreshed every 500 ms
        float p50Ms;             // recomputed every frame
        float p99Ms;             // recomputed every frame
        float p99_9Ms;           // recomputed every frame (for perf CSV)
        float minMs;             // session-wide minimum
        float maxMs;             // session-wide maximum

        // GPU timestamp query state
        bool hasGpu;                // true if this marker uses GPU timing
        bool gpuWrittenThisFrame;   // set by GpuBegin, cleared at FrameEnd
        float gpuLastFrameMs;       // most recent GPU sample
        float gpuAvgMs;             // GPU moving average
        float gpuRingMs[RING_SIZE]; // GPU ring buffer
        int gpuRingFilled;
        int gpuRingHead;
    };

    static Profiler& Instance();

    void Begin( const char* fullPath, uint32_t hash );
    void End( const char* fullPath, uint32_t hash );

    // GPU scopes still record CPU elapsed time internally, but emit platform
    // profiler GPU annotations through the active backend instead of duplicating
    // CPU marker ranges.
    void GpuBegin( const char* fullPath, uint32_t hash );
    void GpuEnd( const char* fullPath, uint32_t hash );

    void FrameBegin();
    void FrameEnd(); // commits per-frame totals; recomputes p50/p99; refreshes moving avg every 500 ms

    // Call when GL context is destroyed/recreated to invalidate all GPU query state
    void InvalidateGpuQueries();

    // Wipes the marker registry at the start of the next frame so stale markers disappear.
    // Safe to call mid-frame (deferred until FrameBegin). No-op when profiling is disabled.
    void ScheduleReset();

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
    float LastGpuFrameMsByHash( uint32_t hash ) const;

    // Perf CSV helpers: write header (once, pass 1) and one row per frame.
    // Include <cstdio> before calling; FILE* must be open for writing.
    void WritePerfCSVHeader( FILE* f ) const;
    void WritePerfCSVRow( FILE* f, int pass, int frame ) const;

    // Renders the indented overlay using Text2d::Render2dText. Caller decides toggle state.
    // xLeft / yTop in the same frustum-unit space used elsewhere; lineHeight in same space; fSize for Text2d.
    // When rightAnchored=true, xLeft is treated as the desired right edge of the panel instead.
    void RenderOverlay( float xLeft, float yAnchor, float lineHeight, float fSize, float fps, bool rightAnchored = false ) const;

    // Renders the visual bar overlay — horizontal stacked bars for CPU and GPU timing.
    // absolute=false: normalized (bar fills panelWidth), absolute=true: white = idle/vsync.
    void RenderBarOverlay( float xLeft, float yBottom, float panelWidth, float panelHeight, bool absolute ) const;

  private:
    Profiler();
    Profiler( const Profiler& ) = delete;
    Profiler& operator=( const Profiler& ) = delete;

    void BeginInternal( const char* fullPath, uint32_t hash, bool emitCpuPlatformProfiler );
    void EndInternal( const char* fullPath, uint32_t hash, bool emitCpuPlatformProfiler );
    void BeginGpuTimerInternal( const char* fullPath, uint32_t hash );
    void EndGpuTimerInternal( const char* fullPath, uint32_t hash );
    int FindOrRegister( const char* fullPath, uint32_t hash );
    void AbortMismatch( const char* msg, const char* details ) const;
    void ReadPendingGpuResults();
    void AdvanceGpuWriteCursors();

    Marker m_markers[MAX_MARKERS];
    int m_markerCount;

    int m_stackIndices[MAX_DEPTH]; // marker indices currently open (top of stack at [m_stackTop-1])
    int m_stackTop;

    int64_t m_qpcFrequency;
    int64_t m_frameStartTicks;
    int64_t m_lastAvgTicks;
    bool m_inFrame;
    int m_warmupFrames;   // frames remaining in warmup window; ring-buffer stats not recorded when > 0
    bool m_resetPending;  // set by ScheduleReset(); applied at the next FrameBegin()
    int m_nextColorIndex; // round-robin colour assignment for leaf markers
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
        Profiler::Instance().GpuBegin( m_fullPath, m_hash );
    }
    ~GpuProfilerScope()
    {
        Profiler::Instance().GpuEnd( m_fullPath, m_hash );
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
        ::SkullbonezCore::Basics::Profiler::Instance().GpuBegin( name, PROFILE_PASTE( _profH_, __LINE__ ) ); \
    } while ( 0 )

#define PROFILE_GPU_END( name )                                                                            \
    do                                                                                                     \
    {                                                                                                      \
        constexpr uint32_t PROFILE_PASTE( _profH_, __LINE__ ) = ::HashStr( name );                         \
        ::SkullbonezCore::Basics::Profiler::Instance().GpuEnd( name, PROFILE_PASTE( _profH_, __LINE__ ) ); \
    } while ( 0 )

#define PROFILE_GPU_SCOPED( name )                                              \
    constexpr uint32_t PROFILE_PASTE( _profSH_, __LINE__ ) = ::HashStr( name ); \
    ::SkullbonezCore::Basics::GpuProfilerScope PROFILE_PASTE( _profS_, __LINE__ )( name, PROFILE_PASTE( _profSH_, __LINE__ ) )

#define PROFILE_FRAME_BEGIN() ::SkullbonezCore::Basics::Profiler::Instance().FrameBegin()
#define PROFILE_FRAME_END() ::SkullbonezCore::Basics::Profiler::Instance().FrameEnd()
#define PROFILE_SCHEDULE_RESET() ::SkullbonezCore::Basics::Profiler::Instance().ScheduleReset()

#else // SKULLBONEZ_PROFILE_ENABLED

#define PROFILE_BEGIN( name ) ( (void)0 )
#define PROFILE_END( name ) ( (void)0 )
#define PROFILE_SCOPED( name ) ( (void)0 )
#define PROFILE_GPU_BEGIN( name ) ( (void)0 )
#define PROFILE_GPU_END( name ) ( (void)0 )
#define PROFILE_GPU_SCOPED( name ) ( (void)0 )
#define PROFILE_FRAME_BEGIN() ( (void)0 )
#define PROFILE_FRAME_END() ( (void)0 )
#define PROFILE_SCHEDULE_RESET() ( (void)0 )

#endif // SKULLBONEZ_PROFILE_ENABLED
