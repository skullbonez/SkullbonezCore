/*
File: SkullbonezSource/Runtime/Replay/ReplayTimeline.h
Purpose:
  Owns replay recorders, retained loading state, and memory policy.

Summary:
  ReplayTimeline is the mutable authority for retained presentation, solver,
  event, and recording-policy state.

Glossary:
  Retention: Seconds retained by a bounded recorder ring.

Invariants:
  - Retention clamps preserve presentation history before solver history.
  - Recorder windows and loaded samples never have parallel storage in ReplayRuntime.

Related:
  - ReplayRuntime.h
  - ReplayRecorder.h
*/
#pragma once

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
    policy.requestedRetentionSeconds = std::clamp( policy.requestedRetentionSeconds,
                                                   REPLAY_MEMORY_POLICY_MIN_SECONDS,
                                                   REPLAY_MEMORY_POLICY_MAX_SECONDS );
    policy.requestedBudgetMiB = std::clamp( policy.requestedBudgetMiB,
                                            REPLAY_MEMORY_POLICY_MIN_BUDGET_MIB,
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

    policy.solverRetentionSeconds =
        std::clamp( policy.solverRetentionSeconds, REPLAY_MEMORY_POLICY_MIN_SECONDS, REPLAY_MEMORY_POLICY_MAX_SECONDS );
    policy.presentationRetentionSeconds = std::clamp( policy.presentationRetentionSeconds,
                                                      REPLAY_MEMORY_POLICY_MIN_SECONDS,
                                                      REPLAY_MEMORY_POLICY_MAX_SECONDS );
    policy.solverWindowReduced = policy.solverRetentionSeconds < policy.requestedRetentionSeconds;
    policy.budgetClamped =
        policy.solverWindowReduced || policy.presentationRetentionSeconds < policy.requestedRetentionSeconds;
    return policy;
}

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

class ReplayTimeline
{
  public:
    ReplayRecorder& Presentation() noexcept
    {
        return m_presentation;
    }
    const ReplayRecorder& Presentation() const noexcept
    {
        return m_presentation;
    }
    ReplaySolverRecorder& Solver() noexcept
    {
        return m_solver;
    }
    const ReplaySolverRecorder& Solver() const noexcept
    {
        return m_solver;
    }
    ReplayEventRecorder& Events() noexcept
    {
        return m_events;
    }
    const ReplayEventRecorder& Events() const noexcept
    {
        return m_events;
    }
    ReplayMemoryPolicy& MemoryPolicy() noexcept
    {
        return m_memoryPolicy;
    }
    const ReplayMemoryPolicy& MemoryPolicy() const noexcept
    {
        return m_memoryPolicy;
    }
    RunLoadedReplayPresentationState& LoadedPresentation() noexcept
    {
        return m_loadedPresentation;
    }
    const RunLoadedReplayPresentationState& LoadedPresentation() const noexcept
    {
        return m_loadedPresentation;
    }
    std::string& RecordingHashLogPath() noexcept
    {
        return m_recordingHashLogPath;
    }
    const std::string& RecordingHashLogPath() const noexcept
    {
        return m_recordingHashLogPath;
    }
    int& PresentationSaveSequence() noexcept
    {
        return m_presentationSaveSequence;
    }
    int& RecordingRuntimeBodyCapacity() noexcept
    {
        return m_recordingRuntimeBodyCapacity;
    }
    uint32_t& CaptureMismatchReports() noexcept
    {
        return m_captureMismatchReports;
    }
    bool& CaptureMismatchSuppressed() noexcept
    {
        return m_captureMismatchSuppressed;
    }
    bool& RecordingConfigured() noexcept
    {
        return m_recordingConfigured;
    }
    bool RecordingConfigured() const noexcept
    {
        return m_recordingConfigured;
    }
    bool& RecordingEnabled() noexcept
    {
        return m_recordingEnabled;
    }
    bool RecordingEnabled() const noexcept
    {
        return m_recordingEnabled;
    }

  private:
    ReplayRecorder m_presentation;
    ReplaySolverRecorder m_solver;
    ReplayEventRecorder m_events;
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
