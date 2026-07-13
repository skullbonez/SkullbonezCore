/*
File: ReplayPredictionScheduling.h
Purpose:
  Defines allocation-free scheduling decisions for replay prediction builds.

Summary:
  The worker measures prediction throughput while the frame thread chooses
  whether to finish the horizon in one submission or keep using small slices.
  Dirty velocity edits are coalesced into one newest-state restart.

Glossary:
  Instant build: One worker submission that completes the remaining horizon.
  Amortized build: Small worker submissions spread across render frames.
  Latest-wins restart: One pending rebuild bit representing every newer edit.

Invariants:
  - These helpers are pure and allocate no runtime memory.
  - Instant work is never cancelled for a velocity refresh; the newest request
    begins after the current private-engine build completes.

Related:
  - ReplayRuntime.h stores the scheduling state.
  - RunReplayTools.cpp applies these decisions on the frame thread.
*/
#pragma once

#include <cstdint>

namespace SkullbonezCore
{
namespace Runtime
{

enum class ReplayPredictionBuildMode : uint8_t
{
    Undecided,
    Amortized,
    Instant
};

enum class ReplayPredictionCoalescerAction : uint8_t
{
    Nothing,
    Begin,
    Supersede,
    CancelAndBegin
};

inline ReplayPredictionBuildMode
ChooseReplayPredictionBuildMode( double measuredTicksPerMs, int remainingTicks, double instantBudgetMs ) noexcept
{
    if ( measuredTicksPerMs <= 0.0 || remainingTicks < 0 )
    {
        return ReplayPredictionBuildMode::Undecided;
    }
    if ( instantBudgetMs <= 0.0 )
    {
        return ReplayPredictionBuildMode::Amortized;
    }

    const double projectedMilliseconds = static_cast<double>( remainingTicks ) / measuredTicksPerMs;
    return projectedMilliseconds <= instantBudgetMs ? ReplayPredictionBuildMode::Instant
                                                    : ReplayPredictionBuildMode::Amortized;
}

inline ReplayPredictionCoalescerAction ChooseReplayPredictionCoalescerAction( bool dirty,
                                                                              bool building,
                                                                              ReplayPredictionBuildMode mode,
                                                                              bool pendingLatestRestart ) noexcept
{
    const bool restartRequested = dirty || pendingLatestRestart;
    if ( !restartRequested )
    {
        return ReplayPredictionCoalescerAction::Nothing;
    }
    if ( !building )
    {
        return ReplayPredictionCoalescerAction::Begin;
    }
    if ( mode == ReplayPredictionBuildMode::Instant )
    {
        return ReplayPredictionCoalescerAction::Supersede;
    }
    return ReplayPredictionCoalescerAction::CancelAndBegin;
}

} // namespace Runtime
} // namespace SkullbonezCore
