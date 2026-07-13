/*
File: SkullbonezSource/Runtime/Replay/ReplayTimeline.h
Purpose:
  Defines replay recording-retention policy and retained presentation values.

Summary:
  ReplayTimeline owns bounded recorder history and retention policy; M2 moves definitions only.

Glossary:
  Retention: Seconds retained by a bounded recorder ring.

Invariants:
  - Retention clamps preserve presentation history before solver history.
  - M2 preserves the moved definition bodies verbatim.

Related:
  - ReplayRuntime.h
  - ReplayRecorder.h
*/
#pragma once

#include "ReplayRecorder.h"

#include <algorithm>
#include <cstddef>
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

} // namespace Runtime
} // namespace SkullbonezCore
