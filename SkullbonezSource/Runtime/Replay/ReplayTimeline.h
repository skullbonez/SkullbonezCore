/*
File: SkullbonezSource/Runtime/Replay/ReplayTimeline.h
Purpose:
  Owns replay recorders, retained loading state, and memory policy.

Summary:
  ReplayTimeline is the mutable authority for retained presentation, solver,
  event, and recording-policy state.

Glossary:
  Retention: Seconds retained by a bounded recorder ring.
  Recording gate: Live append permission over already configured recorder rings;
    stopping the gate does not clear or resize retained samples.
  Hash-log lock: Startup validation capture whose append policy cannot be
    changed interactively.

Invariants:
  - Retention clamps preserve presentation history before solver history.
  - Recorder windows and loaded samples never have parallel storage in ReplayRuntime.
  - Hash-log recording cannot be stopped by an editor command.

Related:
  - SkullbonezSource/Runtime/App/ReplayRuntime.h
  - SkullbonezSource/Runtime/Replay/ReplayRecorder.h
*/
#pragma once

#include "ReplayArtifactHashLog.h"
#include "ReplayRecorder.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace SkullbonezCore
{
namespace Runtime
{
enum class ReplayMemoryPreset : int
{
    LosslessLook = 0,
    Balanced = 1,
    Compact = 2,
    Count
};

struct ReplayMemoryPolicy
{
    // Concept: presets and sliders resolve to concrete recorder windows here so
    // UI code never needs to know how presentation, solver, and event rings are
    // sized or degraded.
    ReplayMemoryPreset preset = ReplayMemoryPreset::LosslessLook;
    int requestedRetentionSeconds = REPLAY_PAST_BUFFER_SECONDS;
    int requestedBudgetMiB = 256;
    int presentationRetentionSeconds = REPLAY_PAST_BUFFER_SECONDS;
    int solverRetentionSeconds = REPLAY_PAST_BUFFER_SECONDS;
    bool budgetClamped = false;
    bool solverWindowReduced = false;
};

struct ReplayMemoryPolicyRequest
{
    // Sentinel -1 means "leave the current policy value unchanged"; UI controls
    // can therefore emit one focused command without mirroring every slider.
    int presetIndex = -1;
    int retentionSeconds = -1;
    int budgetMiB = -1;
};

inline constexpr int REPLAY_MEMORY_POLICY_MIN_SECONDS = 1;
inline constexpr int REPLAY_MEMORY_POLICY_MAX_SECONDS = 600;
inline constexpr int REPLAY_MEMORY_POLICY_MIN_BUDGET_MIB = 32;
inline constexpr int REPLAY_MEMORY_POLICY_MAX_BUDGET_MIB = 512;

namespace ReplayTimelineOperations
{
inline ReplayMemoryPreset ReplayMemoryPresetFromIndex( int presetIndex )
{
    switch ( presetIndex )
    {
    case static_cast<int>( ReplayMemoryPreset::Balanced ):
        return ReplayMemoryPreset::Balanced;
    case static_cast<int>( ReplayMemoryPreset::Compact ):
        return ReplayMemoryPreset::Compact;
    case static_cast<int>( ReplayMemoryPreset::LosslessLook ):
    default:
        return ReplayMemoryPreset::LosslessLook;
    }
}

inline ReplayMemoryPolicy ReplayMemoryPresetPolicy( ReplayMemoryPreset preset )
{
    ReplayMemoryPolicy policy;
    policy.preset = preset;

    switch ( preset )
    {
    case ReplayMemoryPreset::Balanced:
        policy.requestedRetentionSeconds = 45;
        policy.requestedBudgetMiB = 128;
        break;
    case ReplayMemoryPreset::Compact:
        policy.requestedRetentionSeconds = 20;
        policy.requestedBudgetMiB = 64;
        break;
    case ReplayMemoryPreset::LosslessLook:
    default:
        policy.requestedRetentionSeconds = REPLAY_PAST_BUFFER_SECONDS;
        policy.requestedBudgetMiB = 256;
        break;
    }

    return policy;
}

inline ReplayMemoryPolicy ResolveReplayMemoryPolicy( ReplayMemoryPolicy policy )
{
    policy.requestedRetentionSeconds = std::clamp( policy.requestedRetentionSeconds, REPLAY_MEMORY_POLICY_MIN_SECONDS,
                                                   REPLAY_MEMORY_POLICY_MAX_SECONDS );
    policy.requestedBudgetMiB = std::clamp( policy.requestedBudgetMiB, REPLAY_MEMORY_POLICY_MIN_BUDGET_MIB,
                                            REPLAY_MEMORY_POLICY_MAX_BUDGET_MIB );
    policy.presentationRetentionSeconds = policy.requestedRetentionSeconds;
    policy.solverRetentionSeconds = policy.requestedRetentionSeconds;

    if ( policy.preset == ReplayMemoryPreset::Balanced )
    {
        policy.solverRetentionSeconds = (std::min)( policy.solverRetentionSeconds, 30 );
    }
    else if ( policy.preset == ReplayMemoryPreset::Compact )
    {
        policy.solverRetentionSeconds = (std::min)( policy.solverRetentionSeconds, 10 );
    }

    // Why: lower memory-budget requests keep the visual/presentation look as
    // long as possible and shorten solver/debug inspection history first.
    if ( policy.requestedBudgetMiB < 192 )
    {
        policy.solverRetentionSeconds = (std::min)( policy.solverRetentionSeconds, 30 );
    }

    if ( policy.requestedBudgetMiB < 128 )
    {
        policy.solverRetentionSeconds = (std::min)( policy.solverRetentionSeconds, 15 );
    }

    if ( policy.requestedBudgetMiB < 64 )
    {
        policy.solverRetentionSeconds = (std::min)( policy.solverRetentionSeconds, 5 );
        policy.presentationRetentionSeconds = (std::min)( policy.presentationRetentionSeconds, 30 );
    }

    policy.solverRetentionSeconds = std::clamp( policy.solverRetentionSeconds, REPLAY_MEMORY_POLICY_MIN_SECONDS,
                                                REPLAY_MEMORY_POLICY_MAX_SECONDS );
    policy.presentationRetentionSeconds = std::clamp( policy.presentationRetentionSeconds, REPLAY_MEMORY_POLICY_MIN_SECONDS,
                                                      REPLAY_MEMORY_POLICY_MAX_SECONDS );
    policy.solverWindowReduced = policy.solverRetentionSeconds < policy.requestedRetentionSeconds;
    policy.budgetClamped = policy.solverWindowReduced ||
                           policy.presentationRetentionSeconds < policy.requestedRetentionSeconds;
    return policy;
}
} // namespace ReplayTimelineOperations

struct RunLoadedReplayPresentationState
{
    bool enabled = false;
    std::vector<ReplayPresentationSample> samples;
    std::size_t bodyDictionaryCount = 0;
    std::size_t fileBytes = 0;
    ReplayFrameIndex firstFrame = 0;
    ReplayFrameIndex lastFrame = 0;
    char path[260] = {};
};

struct ReplayRecordingConfigResult
{
    ReplayRecorderConfig presentationConfig;
    ReplayRecorderConfig solverConfig;
    ReplayRecorderStats presentationStats;
    ReplayRecorderStats solverStats;
    ReplayEventRecorderStats eventStats;
};

struct ReplayMemoryPolicyApplyResult
{
    bool changed = false;
    bool recordersReset = false;
};

struct ReplayTimelineMemoryStats
{
    SkullbonezCore::Core::MainMemoryReplayCategoryBytes categoryBytes;
    ReplayMemoryPolicy policy;
    std::size_t presentationSamples = 0;
    std::size_t solverSamples = 0;
    std::size_t eventSamples = 0;
    std::size_t loadedSamples = 0;
};

class ReplayTimeline
{
  public:
    const ReplayRecorder& Presentation() const noexcept
    {
        return m_presentation;
    }
    const ReplaySolverRecorder& Solver() const noexcept
    {
        return m_solver;
    }
    const ReplayEventRecorder& Events() const noexcept
    {
        return m_events;
    }
    const ReplayMemoryPolicy& MemoryPolicy() const noexcept
    {
        return m_memoryPolicy;
    }
    const RunLoadedReplayPresentationState& LoadedPresentation() const noexcept
    {
        return m_loadedPresentation;
    }
    bool RecordingConfigured() const noexcept
    {
        return m_recordingConfigured;
    }
    bool RecordingEnabled() const noexcept
    {
        return m_recordingEnabled;
    }
    bool RecordingLockedByHashLog() const noexcept
    {
        return !m_recordingHashLogPath.empty();
    }

    ReplayRecordingConfigResult ConfigureRecording( bool enabled, int retentionSeconds, const char* hashLogPath,
                                                    int runtimeBodyCapacity );
    bool SetRecordingEnabled( bool enabled ) noexcept;
    ReplayMemoryPolicyApplyResult ApplyMemoryPolicyRequest( const ReplayMemoryPolicyRequest& request );
    void FlushHashLogs();
    void Reset( const char* sceneLabel );
    void ClearLoadedPresentation();

    // Cold-I/O command: decode and install one retained presentation without
    // exposing temporary sample storage to the composition root.
    bool LoadPresentationArtifact( const char* path );
    bool NextPresentationSavePath( char* outPath, std::size_t outPathSize );
    const ReplaySolverFrameSample*
    CaptureFrame( int sceneFrame, float physicsDt, const ReplayWorldPresentationSample& world,
                  const ReplayCameraSample& camera, const ReplayLauncherVisualSample& launcherVisual,
                  Physics::PhysicsEngine& physics, const Gameplay::TornadoGameplay& tornadoGameplay,
                  const SceneEntityStore& entities, const Physics::PhysicsBodyStore& bodyStore,
                  const Physics::ColliderStore& colliderStore, const ReplayBranchInfo& branch );
    void RecordEvent( const ReplayEventInput& input );

    // Concept: event sequencing belongs to the timeline owner. The caller
    // supplies branch provenance as a value so recording never reaches into
    // authoring state or the replay composition root.
    void SubmitEvent( const ReplayEventCommand& command, const ReplayBranchInfo& branch );
    void CollectMemoryCategoryBytes( SkullbonezCore::Core::MainMemoryReplayCategoryBytes& categories ) const;
    ReplayTimelineMemoryStats CollectMemoryStats() const;
    void ResetCaptureMismatchDiagnostics() noexcept;

  private:
    void InstallLoadedPresentation( const char* path, std::vector<ReplayPresentationSample>& samples,
                                    std::size_t bodyDictionaryCount, std::size_t fileBytes, ReplayFrameIndex firstFrame,
                                    ReplayFrameIndex lastFrame );
    void ReportLatestCaptureMismatch();
    ReplayRecorder m_presentation;
    ReplaySolverRecorder m_solver;
    ReplayEventRecorder m_events;

    // ArtifactIO owns both file handles and stable CSV formatting. Timeline
    // sequences it only after Capture publishes committed sample values.
    ReplayArtifactHashLog m_artifactHashLog;
    ReplayMemoryPolicy m_memoryPolicy;
    RunLoadedReplayPresentationState m_loadedPresentation;
    std::string m_recordingHashLogPath;
    int m_presentationSaveSequence = 0;
    int m_recordingRuntimeBodyCapacity = 0;
    uint32_t m_captureMismatchReports = 0;
    bool m_captureMismatchSuppressed = false;
    bool m_recordingConfigured = false;
    bool m_recordingEnabled = false;
};

} // namespace Runtime
} // namespace SkullbonezCore
