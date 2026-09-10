#pragma once

#include "../Replay/ReplayRecorder.h"
#include "../../UI/UIDrawList.h"
#include <array>
#include <atomic>
#include <thread>
#include <string>
#include <vector>
#include <chrono>
#include <cstdio>

namespace SkullbonezCore::Runtime
{
enum class ComparisonDisplay : uint8_t
{
    Split,
    Overlay,
    Toggle,
    Heatmap,
    Pixels
};
enum class ComparisonChange : uint8_t
{
    Equal,
    Changed,
    OnlyA,
    OnlyB,
    NotRecorded,
    Ambiguous
};
enum class ComparisonFamily : uint8_t
{
    Motion,
    Contact,
    Terrain,
    Sleep,
    SolverIteration
};

struct ComparisonBodyDifference
{
    uint64_t id = 0;
    float distance = 0.0f;
    float angleDegrees = 0.0f;
    float heightDelta = 0.0f;
    float verticalVelocityDelta = 0.0f;
    float velocityDistance = 0.0f;
    ComparisonChange change = ComparisonChange::Equal;
    bool sleepChanged = false;
};

// Invariant: contact indices refer to the selected immutable recording at tick;
// summary selects the diagnostic stream. Missing endpoints use -1, never row 0.
struct ComparisonEvent
{
    int tick = 0;
    uint64_t bodyA = 0;
    uint64_t bodyB = 0;
    uint32_t feature = 0;
    ComparisonFamily family = ComparisonFamily::Motion;
    ComparisonChange change = ComparisonChange::Equal;
    int contactA = -1;
    int contactB = -1;
    bool summary = false;
};

struct ComparisonSettings
{
    ComparisonDisplay display = ComparisonDisplay::Split;
    bool showA = false;
    bool angularHeatmap = false;
    bool occludedOutline = false;
    bool followA = false;
    bool orbitSelected = false;
    bool stackedViews = false;
    bool selectedOnly = true;
    bool differencesOnly = true;
    float outlineAlpha = 0.65f;
    float heatScale = 0.1f;
    float pixelGain = 4.0f;
    float positionThreshold = 0.001f;
    float angleThreshold = 0.1f;
    float impulseThreshold = 0.001f;
    float speed = 1.0f;
};

// The loader is the only writer; the UI reads progress and requests cancellation.
struct ComparisonLoadProgress
{
    // Invariant: the worker advances a monotonic percentage and publishes only
    // static phase labels. The UI reads atomics; timing and range state stay on
    // the loading worker until its completion handshake.
    uint64_t availableBytes = 512ull * 1024 * 1024;
    std::atomic<int> percent { 0 };
    std::atomic<bool> cancelled { false };
    std::atomic<const char*> phase { "Starting" };
    int rangeStart = 0, rangeLength = 0;
    std::chrono::steady_clock::time_point phaseStarted {};

    void Begin( const char* label, int start, int length )
    {
        const auto now = std::chrono::steady_clock::now();
        if ( phaseStarted != std::chrono::steady_clock::time_point {} )
        {
            std::printf( "[solver-lab] %s: %.3f s\n", phase.load(),
                         std::chrono::duration<double>( now - phaseStarted ).count() );
            std::fflush( stdout );
        }
        phaseStarted = now;
        rangeStart = start;
        rangeLength = length;
        phase.store( label, std::memory_order_relaxed );
        percent.store( start, std::memory_order_relaxed );
    }

    void Update( uint64_t complete, uint64_t total )
    {
        if ( total && complete <= total )
        {
            const auto value = rangeStart + static_cast<int>( complete * rangeLength / total );
            if ( value > percent.load( std::memory_order_relaxed ) )
            {
                percent.store( value, std::memory_order_relaxed );
            }
        }
    }
};

// Invariant: Load publishes complete, identity-sorted frames; playback never
// mutates poses or invokes a solver to fill missing evidence.
struct ComparisonRecording
{
    struct ContactSummary
    {
        uint64_t bodyA = 0, bodyB = 0;
        uint32_t feature = 0;
        Math::Vector::Vector3 normal { 0, 0, 0 };
        float penetration = 0, normalImpulse = 0, tangentImpulse = 0, preNormalSpeed = 0, preSlipSpeed = 0,
              postSlipSpeed = 0;
        bool terrain = false, warmStarted = false;
    };
    struct IterationSummary
    {
        int iteration = 0;
        float stoppingDeltaSquared = 0, normalDeltaSquared = 0, tangentDeltaSquared = 0;
        int dropped = 0;
    };
    struct Observations
    {
        bool recorded = false;
        bool iterationsRecorded = false;
        std::vector<ContactSummary> contacts;
        std::vector<IterationSummary> iterations;
    };
    std::vector<ReplayPresentationSample> frames;
    std::vector<ReplaySolverFrameSample> diagnostics;
    std::string executable;
    std::string executableHash;
    std::vector<Observations> observations;
    int tickOffset = 1;
    const ReplaySolverFrameSample* Evidence( int tick ) const noexcept;
    const ReplayPresentationSample* Frame( int tick ) const noexcept;
    const Observations* Observation( int tick ) const noexcept;
    bool LoadObservations( const char* path, int ticks, uint64_t& residentBytes, ComparisonLoadProgress* progress = nullptr,
                           int side = 0 );
    bool LoadBinaryObservations( const char* path, int ticks, uint64_t& residentBytes,
                                 ComparisonLoadProgress* progress = nullptr, int side = 0 );
};

// Planning retains immutable recordings and a presentation cursor. Neither
// loading nor seeking imports a solver checkpoint into the live Physics owner.
class PhysicsComparison
{
  public:
    static constexpr uint64_t MEMORY_BUDGET = 512ull * 1024 * 1024;
    bool Load( const char* bundlePath, ComparisonLoadProgress* progress = nullptr );
    void Close() noexcept;
    bool Active() const noexcept
    {
        return !m_recordings[0].frames.empty() && !m_recordings[1].frames.empty();
    }
    const std::string& Error() const noexcept
    {
        return m_error;
    }
    const std::string& ScenePath() const noexcept
    {
        return m_scene;
    }
    const std::string& BundlePath() const noexcept
    {
        return m_bundle;
    }
    const ComparisonRecording& Recording( int side ) const noexcept
    {
        return m_recordings[side == 0 ? 0 : 1];
    }
    uint64_t MemoryCharge() const noexcept
    {
        return m_memoryCharge;
    }
    int Tick() const noexcept
    {
        return m_tick;
    }
    int LastTick() const noexcept
    {
        return m_lastTick;
    }
    int Direction() const noexcept
    {
        return m_direction;
    }
    uint64_t Selected() const noexcept
    {
        return m_selected;
    }
    uint64_t EventSelectionRevision() const noexcept
    {
        return m_eventSelectionRevision;
    }
    int SelectedEvent() const noexcept
    {
        return m_selectedEvent;
    }
    int LoopStart() const noexcept
    {
        return m_loopStart;
    }
    int LoopEnd() const noexcept
    {
        return m_loopEnd;
    }
    bool LoopEnabled() const noexcept
    {
        return m_loop;
    }
    ComparisonSettings& Settings() noexcept
    {
        return m_settings;
    }
    const ComparisonSettings& Settings() const noexcept
    {
        return m_settings;
    }
    void Seek( int tick ) noexcept;
    void Step( int direction ) noexcept;
    void Play( int direction ) noexcept;
    void Advance( double seconds ) noexcept;
    void SetLoop( int first, int last, bool enabled ) noexcept;
    void Select( uint64_t id ) noexcept;
    bool SelectEvent( std::size_t index ) noexcept;
    bool NextDifference() noexcept;
    bool SetSetting( const char* name, double value ) noexcept;
    bool SetDisplay( const char* name ) noexcept;
    bool VisibleEvent( const ComparisonEvent& event ) const noexcept;
    const std::vector<ComparisonEvent>& Events() const noexcept
    {
        return m_events;
    }
    ComparisonBodyDifference Difference( uint64_t id, int tick ) const noexcept;
    const ReplayBodyPresentationSample* Body( int side, uint64_t id, int tick ) const noexcept;
    bool SaveFinding( const char* path, const ReplayCameraSample& camera, const char* note );
    bool LoadFinding( const char* path, ReplayCameraSample& camera, ComparisonLoadProgress* progress = nullptr );
    const std::string& FindingNote() const noexcept
    {
        return m_note;
    }
    static ComparisonBodyDifference Compare( const ReplayBodyPresentationSample* a,
                                             const ReplayBodyPresentationSample* b ) noexcept;

  private:
    friend struct PhysicsComparisonTestAccess;
    void BuildEvents( ComparisonLoadProgress* progress = nullptr );
    void BuildContactEvents( int tick );
    bool BuildObservedContactEvents( int tick );
    std::array<ComparisonRecording, 2> m_recordings;
    std::vector<ComparisonEvent> m_events;
    ComparisonSettings m_settings;
    std::string m_error, m_scene, m_bundle, m_note;
    int m_tick = 0, m_lastTick = 0, m_direction = 0;
    int m_loopStart = 0, m_loopEnd = 0, m_selectedEvent = -1;
    uint64_t m_selected = 0;
    uint64_t m_memoryCharge = 0;
    uint64_t m_eventSelectionRevision = 0;
    double m_fraction = 0.0;
    bool m_loop = false;
};
// Lifetime: the worker owns its candidate exclusively until the release/acquire
// completion handshake. App publishes it and performs scene/GPU setup on the UI thread.
class PhysicsComparisonLoadJob
{
  public:
    ~PhysicsComparisonLoadJob();
    bool Start( const char* path, bool finding, uint64_t retainedBytes = 0 );
    bool Pending() const noexcept
    {
        return m_pending;
    }
    bool Ready() const noexcept
    {
        return m_ready.load( std::memory_order_acquire );
    }
    int Percent() const noexcept
    {
        return m_progress.percent.load( std::memory_order_relaxed );
    }
    const char* Phase() const noexcept
    {
        return m_progress.phase.load( std::memory_order_relaxed );
    }
    void Cancel() noexcept
    {
        m_progress.cancelled.store( true, std::memory_order_relaxed );
    }
    bool Cancelled() const noexcept
    {
        return m_progress.cancelled.load( std::memory_order_relaxed );
    }
    bool Take( PhysicsComparison& destination, ReplayCameraSample& camera );
    bool Finding() const noexcept
    {
        return m_finding;
    }
    const std::string& Error() const noexcept
    {
        return m_error;
    }
    void DismissError() noexcept
    {
        m_error.clear();
    }

  private:
    PhysicsComparison m_candidate;
    ReplayCameraSample m_camera;
    ComparisonLoadProgress m_progress;
    std::atomic<bool> m_ready { false };
    std::thread m_worker;
    std::string m_error;
    bool m_pending = false, m_success = false, m_finding = false;
};
} // namespace SkullbonezCore::Runtime
