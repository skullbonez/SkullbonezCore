/*
File: SkullbonezSource/Core/Profiler.h
Purpose:
  Records hierarchical CPU/GPU timing markers for runtime diagnostics.

Invariants:
  - Public macros are the supported entry points; direct calls risk mismatched
    marker hashes and begin/end pairs.
  - Marker path segments encode the overlay tree; callers keep one stable path
    literal for each logical interval.
  - Marker arrays are fixed-capacity runtime storage, so adding broad marker
    families must account for MAX_MARKERS and MAX_DEPTH.
  - Worker spans enter a frame-tokened staging store; only FrameEnd merges them
    into the main-thread-owned marker registry and finalized history.
  - Core stores profiler values only. Rendering owns timestamp queries, GPU
    events, and overlay presentation and submits completed samples here.

Related:
  - SkullbonezSource/Core/Profiler.cpp
  - SkullbonezSource/Runtime/Render/UIProfilerOverlayPresenter.cpp
  - Agentic/Reference/runtime-reference.md
  - Agentic/Reference/engine-glossary.md
*/

#pragma once


#include "Common.h"
#include "StringHash.h"

#include <cstdint>
#include <span>

#if defined( SKULLBONEZ_PROFILE_ENABLED )
#include <mutex>
#endif

namespace SkullbonezCore
{
namespace Core
{

class Profiler
{
  public:
    // Lifetime: startup owns one active profiler for the synchronous RunApp
    // lifetime. CPU marker macros resolve it ambiently so instrumentation never
    // widens domain APIs or adds retained service borrows.
    Profiler();
    ~Profiler();
    static Profiler* Active() noexcept
    {
        return s_active;
    }
    static constexpr int MAX_MARKERS = 192;      // Registry capacity; overflow is a profiling contract bug.
    static constexpr int MAX_COUNTERS = 16;      // Scalar diagnostic columns carried beside marker timings.
    static constexpr int MAX_WORKER_CORES = 128; // Worker overlay capacity, not a thread-spawn request.
    static constexpr int MAX_DEPTH = 16;         // Nested marker stack depth before begin/end mismatch becomes unsafe.
    static constexpr int RING_SIZE = 600;        // ~10 s @ 60 fps
    static constexpr int GPU_QUERY_DEPTH = 4;    // pending query ring depth (non-blocking readback)
    static constexpr int WARMUP_FRAMES = 30;     // frames excluded from ring-buffer stats at session/pass start

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
        const char* name;             // full path literal, e.g. "Render/Skybox"
        const char* leafName;         // pointer into name after last '/'
        uint32_t hash;                // FNV-1a of full path
        int parentIndex;              // -1 if top-level (parent of "Render/Skybox" is "Render")
        int depth;                    // count of '/' characters (0 = top)
        int colorIndex;               // index into BAR_PALETTE (assigned at registration for leaf markers, -1 otherwise)
        int openCount;                // recursion guard (must be 0 at frame end)
        int64_t openStartTicks;       // QPC ticks at most recent Begin
        double accumSecondsThisFrame; // Marker-thread span time only; worker spans accumulate below.

        // Concept: one marker path is reached from both the frame thread and
        // worker threads, because worker jobs reuse the same instrumented code.
        // Summing both into accumSecondsThisFrame made a row like
        // Frame/Physics/Integrate report replay-prediction work as if it were
        // live physics, so worker spans accumulate separately and the overlay
        // reports them in their own column.
        // Invariant: these three are written only under m_workerSampleMutex or
        // at a frame boundary, never from a marker Begin/End on the frame thread.
        double workerAccumSecondsThisFrame;
        float lastFrameWorkerMs; // Most recent finished-frame worker total for this marker.
        float workerAvgMs;       // Worker moving average, refreshed on the CPU/GPU cadence.
        double workerAvgAccumMs; // Running worker sum inside the current average window.
        int workerAvgFrameCount; // Frames accumulated into workerAvgAccumMs.

        // Latches on the first worker sample and drives this marker's `_worker`
        // perf CSV column, mirroring how hasGpu drives `_gpu`. A changed column
        // count re-emits the header, which analyze_perf treats as authoritative
        // for the rows after it.
        bool hasWorker;
        double firstStartSecondsThisFrame;
        double lastEndSecondsThisFrame;
        bool spanWrittenThisFrame;
        float ringMs[RING_SIZE]; // last RING_SIZE finished-frame totals
        int ringFilled;          // number of valid samples (saturates at RING_SIZE)
        int ringHead;            // next write index
        float lastFrameMs;       // most recent finished-frame total
        float lastSelfMs;        // most recent finished-frame direct time after direct child totals
        float lastFrameStartMs;  // first Begin point within the most recent frame
        float lastFrameEndMs;    // final End point within the most recent frame
        float avgMs;             // moving average refreshed every 500 ms
        float selfAvgMs;         // moving average of direct time after direct child totals
        float p50Ms;             // recomputed every frame
        float p99Ms;             // recomputed every frame
        float p99_9Ms;           // recomputed every frame (for perf CSV)
        float minMs;             // session-wide minimum
        float maxMs;             // session-wide maximum
        float selfRingMs[RING_SIZE];
        int selfRingFilled;
        int selfRingHead;

        // GPU timestamp query state
        bool hasGpu;                // true if this marker uses GPU timing
        bool gpuWrittenThisFrame;   // Set when Rendering opens a timestamp query; cleared at FrameEnd.
        float gpuLastFrameMs;       // most recent GPU sample
        float gpuAvgMs;             // GPU moving average
        float gpuRingMs[RING_SIZE]; // GPU ring buffer
        int gpuRingFilled;
        int gpuRingHead;
    };

    struct WorkerCoreSample
    {
        int workerIndex;
        int jobCount;
        float coreMs;
        float avgCoreMs;
        float spanStartMs;
        float spanEndMs;
    };

    struct Counter
    {
        const char* name;      // Stable CSV column name, e.g. "Counter/Physics/AwakeBodies".
        uint32_t hash;         // FNV-1a of the full counter name.
        double valueThisFrame; // Most recent value recorded in the current render frame.
        double lastFrameValue; // Value exported after FrameEnd; zero when not recorded.
        bool writtenThisFrame; // Distinguishes a real zero from a counter omitted this frame.
    };

    struct GpuTimingSample
    {
        uint32_t markerHash = 0;   // Core marker identity; never a backend query-slot identity.
        float milliseconds = 0.0f; // Completed non-blocking renderer measurement.
    };

    struct ProfilerFrameView
    {
        std::span<const Marker> markers;                     // Fixed read-only marker/history rows for presenters.
        std::span<const Counter> counters;                   // Scalar rows committed at the preceding frame boundary.
        std::span<const WorkerCoreSample> workerCoreSamples; // Fixed worker summary rows.
    };

    void Begin( const char* fullPath, uint32_t hash );
    void End( const char* fullPath, uint32_t hash );

    // outermostOnThread distinguishes the two quantities a worker span feeds.
    // Every span adds to its own marker's worker total, but only the outermost
    // one adds to the per-core wall-clock accumulator: nested spans overlap in
    // time, so counting them all would report several times the core's real
    // occupancy.
    void RecordWorkerSample( const char* fullPath, uint32_t hash, int workerIndex, int64_t startTicks, int64_t endTicks,
                             bool outermostOnThread, uint64_t frameToken );
    void RecordCounter( const char* fullPath, uint32_t hash, double value );

    // Rendering calls these around command recording. Core owns the nested CPU
    // record and marker identity; the concrete render timing owner owns every
    // backend query and GPU event.
    int BeginRenderRecord( const char* fullPath, uint32_t hash );
    void EndRenderRecord( const char* fullPath, uint32_t hash );
    void MarkGpuMarkerWritten( int markerIndex );
    void ApplyGpuTimingSamples( std::span<const GpuTimingSample> samples );
    void InvalidateGpuSamples();

    void FrameBegin();
    void FrameEnd(); // commits per-frame totals; recomputes p50/p99; refreshes moving avg every 500 ms

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
    int WorkerCoreSampleCount() const
    {
        return m_workerCoreSampleCount;
    }
    const WorkerCoreSample& GetWorkerCoreSample( int i ) const
    {
        return m_workerCoreSamples[i];
    }
    ProfilerFrameView FrameView() const;
    uint32_t MarkerEpoch() const
    {
        return m_markerEpoch;
    }

    // Accessor for back-compat perf logging (returns last finished-frame total ms; 0 if marker missing)
    float LastFrameMsByHash( uint32_t hash ) const;
    float LastGpuFrameMsByHash( uint32_t hash ) const;

    // Perf CSV helpers: write header (once, pass 1) and one row per frame.
    // Include <cstdio> before calling; FILE* must be open for writing.
    void WritePerfCSVHeader( FILE* f ) const;
    void WritePerfCSVRow( FILE* f, int pass, int frame ) const;

  private:
    friend class WorkerProfilerScope;

    // Non-owning process marker target; the sole startup owner clears it on destruction.
    static Profiler* s_active;
    Profiler( const Profiler& ) = delete;
    Profiler& operator=( const Profiler& ) = delete;

    void BeginInternal( const char* fullPath, uint32_t hash, bool emitCpuPlatformProfiler );
    void EndInternal( const char* fullPath, uint32_t hash, bool emitCpuPlatformProfiler );
    int FindOrRegister( const char* fullPath, uint32_t hash );
    int FindOrRegisterCounter( const char* fullPath, uint32_t hash );
    int PerfCSVColumnCount() const;
    void AbortMismatch( const char* msg, const char* details ) const;
    void AdvanceGpuWriteCursors();
    void RestartWarmup();
#if defined( SKULLBONEZ_PROFILE_ENABLED )
    uint64_t CaptureWorkerFrameToken() const;
    void CloseWorkerFrameAndMergeSamples();
    void RefreshMarkerAverages();
#endif

    struct WorkerCoreAccumulator
    {
        int jobCount;
        double accumSecondsThisFrame;
        double firstStartSecondsThisFrame;
        double lastEndSecondsThisFrame;
        bool spanWrittenThisFrame;
    };

    struct WorkerCoreAverageWindow
    {
        double accumulatedCoreMs;
        int frameCount;
        float avgCoreMs;
    };

    struct WorkerMarkerAccumulator
    {
        // Workers publish only into this frame-local staging record. FrameEnd
        // drains it into the main-thread-owned marker registry after admission
        // closes, avoiding concurrent registry and history mutation.
        const char* name;
        uint32_t hash;
        double accumSeconds;
        double firstStartSeconds;
        double lastEndSeconds;
        bool spanWritten;
    };

    Marker m_markers[MAX_MARKERS];
    int m_markerCount;
    Counter m_counters[MAX_COUNTERS];
    int m_counterCount;
    mutable int m_lastPerfCSVColumnCount;
    WorkerCoreAccumulator m_workerCoreAccumulators[MAX_WORKER_CORES];
    WorkerCoreAverageWindow m_workerCoreAverageWindows[MAX_WORKER_CORES];
    WorkerCoreSample m_workerCoreSamples[MAX_WORKER_CORES];
    int m_workerCoreSampleCount;

    int m_stackIndices[MAX_DEPTH]; // marker indices currently open (top of stack at [m_stackTop-1])
    bool m_platformProfilerCpuOpen[MAX_DEPTH];
    bool m_platformProfilerRenderRecordOpen[MAX_DEPTH];
    int m_stackTop;

    int64_t m_qpcFrequency;
    int64_t m_frameStartTicks;
    int64_t m_lastAvgTicks;
    bool m_inFrame;
    int m_warmupFrames;     // frames remaining in warmup window; ring-buffer stats not recorded when > 0
    bool m_resetPending;    // set by ScheduleReset(); applied at the next FrameBegin()
    int m_nextColorIndex;   // round-robin colour assignment for leaf markers
    uint32_t m_markerEpoch; // Advances when marker identities are cleared at a frame boundary.
#if defined( SKULLBONEZ_PROFILE_ENABLED )
    mutable std::mutex m_workerSampleMutex;
    WorkerMarkerAccumulator m_workerMarkerAccumulators[MAX_MARKERS];
    int m_workerMarkerAccumulatorCount;
    uint64_t m_workerFrameToken;
#endif
};

class ProfilerScope
{
  public:
    ProfilerScope( Profiler* profiler, const char* fullPath, uint32_t hash )
        : m_profiler( profiler ), m_fullPath( fullPath ), m_hash( hash )
    {
        if ( m_profiler )
        {
            m_profiler->Begin( m_fullPath, m_hash );
        }
    }
    ~ProfilerScope()
    {
        if ( m_profiler )
        {
            m_profiler->End( m_fullPath, m_hash );
        }
    }
    ProfilerScope( const ProfilerScope& ) = delete;
    ProfilerScope& operator=( const ProfilerScope& ) = delete;

  private:
    // Lifetime: captured only for this lexical scope; the startup owner outlives it.
    Profiler* m_profiler;
    const char* m_fullPath;
    uint32_t m_hash;
};

class WorkerProfilerScope
{
  public:
    WorkerProfilerScope( Profiler* profiler, const char* fullPath, uint32_t hash );

    // Concept: an inert scope that Open arms later. Ambient PROFILE_BEGIN on a
    // worker needs the same timing behavior without constructing an object at
    // the Begin call site, because the per-thread marker stack is fixed storage
    // and must not use optional emplacement to arm one of its rows.
    WorkerProfilerScope() noexcept;
    ~WorkerProfilerScope();
    WorkerProfilerScope( const WorkerProfilerScope& ) = delete;
    WorkerProfilerScope& operator=( const WorkerProfilerScope& ) = delete;

    // Invariant: Open arms an inert scope and Close is idempotent, so the
    // destructor after an explicit Close is a no-op. Both are for the ambient
    // begin/end path; RAII users construct and destruct normally.
    void Open( Profiler* profiler, const char* fullPath, uint32_t hash );
    void Close();

  private:
    // Lifetime: captured only for this worker scope; the startup owner outlives it.
    Profiler* m_profiler;
    const char* m_fullPath;
    uint32_t m_hash;
    int m_workerIndex;
    int64_t m_startTicks;
    uint64_t m_frameToken;

    // Set when this scope opened at worker nesting depth zero; see
    // Profiler::RecordWorkerSample for why only that level feeds core occupancy.
    bool m_outermostOnThread;
    bool m_platformProfilerOpen;
};

} // namespace Core
} // namespace SkullbonezCore

#if defined( SKULLBONEZ_PROFILE_ENABLED )

#define PROFILE_PASTE_INNER( a, b ) a##b
#define PROFILE_PASTE( a, b ) PROFILE_PASTE_INNER( a, b )

#define PROFILE_BEGIN( name )                                                                                               \
    do                                                                                                                      \
    {                                                                                                                       \
        constexpr uint32_t PROFILE_PASTE( _profH_, __LINE__ ) = ::HashStr( name );                                          \
        auto* PROFILE_PASTE( _profP_, __LINE__ ) = ::SkullbonezCore::Core::Profiler::Active();                              \
        if ( PROFILE_PASTE( _profP_, __LINE__ ) )                                                                           \
            PROFILE_PASTE( _profP_, __LINE__ )->Begin( name, PROFILE_PASTE( _profH_, __LINE__ ) );                          \
    } while ( 0 )

#define PROFILE_END( name )                                                                                                 \
    do                                                                                                                      \
    {                                                                                                                       \
        constexpr uint32_t PROFILE_PASTE( _profH_, __LINE__ ) = ::HashStr( name );                                          \
        auto* PROFILE_PASTE( _profP_, __LINE__ ) = ::SkullbonezCore::Core::Profiler::Active();                              \
        if ( PROFILE_PASTE( _profP_, __LINE__ ) )                                                                           \
            PROFILE_PASTE( _profP_, __LINE__ )->End( name, PROFILE_PASTE( _profH_, __LINE__ ) );                            \
    } while ( 0 )

#define PROFILE_SCOPED( name )                                                                                              \
    constexpr uint32_t PROFILE_PASTE( _profSH_, __LINE__ ) = ::HashStr( name );                                             \
    ::SkullbonezCore::Core::ProfilerScope PROFILE_PASTE( _profS_, __LINE__ )( ::SkullbonezCore::Core::Profiler::Active(),   \
                                                                              name, PROFILE_PASTE( _profSH_, __LINE__ ) )

#define PROFILE_WORKER_SCOPED( profiler, name )                                                                             \
    constexpr uint32_t PROFILE_PASTE( _profWH_, __LINE__ ) = ::HashStr( name );                                             \
    ::SkullbonezCore::Core::WorkerProfilerScope PROFILE_PASTE( _profW_, __LINE__ )( profiler, name,                         \
                                                                                    PROFILE_PASTE( _profWH_, __LINE__ ) )

#define PROFILE_COUNTER( profiler, name, value )                                                                            \
    do                                                                                                                      \
    {                                                                                                                       \
        constexpr uint32_t PROFILE_PASTE( _profCH_, __LINE__ ) = ::HashStr( name );                                         \
        auto* PROFILE_PASTE( _profCP_, __LINE__ ) = ( profiler );                                                           \
        if ( PROFILE_PASTE( _profCP_, __LINE__ ) )                                                                          \
            PROFILE_PASTE( _profCP_, __LINE__ )                                                                             \
                ->RecordCounter( name, PROFILE_PASTE( _profCH_, __LINE__ ), static_cast<double>( value ) );                 \
    } while ( 0 )

#define PROFILE_FRAME_BEGIN( profiler )                                                                                     \
    do                                                                                                                      \
    {                                                                                                                       \
        if ( profiler )                                                                                                     \
            ( profiler )->FrameBegin();                                                                                     \
    } while ( 0 )
#define PROFILE_FRAME_END( profiler )                                                                                       \
    do                                                                                                                      \
    {                                                                                                                       \
        if ( profiler )                                                                                                     \
            ( profiler )->FrameEnd();                                                                                       \
    } while ( 0 )
#define PROFILE_SCHEDULE_RESET( profiler )                                                                                  \
    do                                                                                                                      \
    {                                                                                                                       \
        if ( profiler )                                                                                                     \
            ( profiler )->ScheduleReset();                                                                                  \
    } while ( 0 )

#else // SKULLBONEZ_PROFILE_ENABLED

#define PROFILE_BEGIN( name ) ( (void)sizeof( name ) )
#define PROFILE_END( name ) ( (void)sizeof( name ) )
#define PROFILE_SCOPED( name ) ( (void)sizeof( name ) )
#define PROFILE_WORKER_SCOPED( profiler, name ) ( (void)( profiler ) )
#define PROFILE_COUNTER( profiler, name, value ) ( (void)( profiler ) )
#define PROFILE_FRAME_BEGIN( profiler ) ( (void)( profiler ) )
#define PROFILE_FRAME_END( profiler ) ( (void)( profiler ) )
#define PROFILE_SCHEDULE_RESET( profiler ) ( (void)( profiler ) )

#endif // SKULLBONEZ_PROFILE_ENABLED
