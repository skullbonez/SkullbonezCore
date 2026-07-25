/*
File: ReplayPredictionScheduling.h
Purpose:
  Defines allocation-free scheduling, budget, and reveal decisions for replay
  prediction builds and presentation.

Summary:
  The worker schedule owns one in-place task, its in-flight join, and bounded
  submissions. Shared operations select build mode, coalesce dirty requests,
  measure pass budgets, and advance the presentation reveal cursor.

Glossary:
  Instant build: One worker submission that completes the remaining horizon.
  Amortized build: Small worker submissions spread across render frames.
  Latest-wins restart: One pending rebuild bit representing every newer edit.
  Reveal cursor: Monotonic presentation frame reached by the prediction clock.
  Budget pass: Named prediction stage whose elapsed-time exhaustion is counted.

Invariants:
  - Build-mode/coalescer decisions are pure and the schedule allocates no runtime memory.
  - Reveal operations mutate only the prediction-owned reveal clock.
  - Reset waits for an in-flight slice before releasing the task's stable borrows.
  - Instant work is never cancelled for a velocity refresh; the newest request
    begins after the current private-engine build completes.
  - Predictions above the small-scene body cap always use amortized work because
    a cheap pre-contact probe cannot predict later contact fan-out.
  - Every caller uses the same steady-clock budget and reveal policy.

Related:
  - ReplayPrediction.h stores the scheduling state.
  - ReplayPredictionScheduling.cpp owns cancellation and prefix promotion.
*/
#pragma once

#include "../../Core/AmortizedTask.h"
#include "../../Core/MainMemoryStats.h"
#include "../../Physics/PhysicsHandles.h"
#include "ReplayIdentity.h"
#include "ReplayPredictionPackets.h"

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <thread>
#include <utility>

namespace SkullbonezCore
{
namespace Core
{
class EngineConfig;
}
namespace Threading
{
class WorkerPool;
}
namespace Runtime
{
class ReplayPrediction;
struct ReplayPredictionUpdateResult;
struct RunReplayPredictionState;

// Concept: one submitted slice borrows only the stable prediction owner,
// immutable config, worker pool, and captured model count. The schedule waits
// for this operation to leave flight before any of those borrows are reset.
struct ReplayPredictionSimulationSlice
{
    ReplayPrediction* prediction = nullptr;
    const SkullbonezCore::Core::EngineConfig* config = nullptr;
    Threading::WorkerPool* workerPool = nullptr;
    int modelCount = 0;

    void operator()( int beginTickIndex, int endTickIndex ) const;
};

using ReplayPredictionAmortizedTask = Threading::AmortizedTask<ReplayPredictionSimulationSlice>;

class ReplayPredictionWorkerSchedule
{
  public:
    bool Active() const noexcept
    {
        return m_task.has_value();
    }

    bool CompleteAndIdle() const noexcept
    {
        return m_task && m_task->IsComplete() && !m_task->IsInFlight();
    }

    void WaitForIdle() const noexcept
    {
        // Hazard: cancellation is a scene/branch mutation edge. Build scratch
        // remains worker-owned until the submitted slice drops its in-flight bit.
        while ( m_task && m_task->IsInFlight() )
        {
            std::this_thread::yield();
        }
    }

    void Reset() noexcept
    {
        WaitForIdle();
        m_task.reset();
    }

    template <typename... Args> void Begin( Args&&... args )
    {
        m_task.emplace( std::forward<Args>( args )... );
    }

    void SetBudget( int tickBudget )
    {
        if ( m_task )
        {
            m_task->SetBudget( tickBudget );
        }
    }

    void SubmitTick( Threading::WorkerPool& workerPool )
    {
        if ( m_task )
        {
            m_task->SubmitTick( workerPool );
        }
    }

  private:
    // Lifetime: the optional owns one in-place task. Reset first waits for its
    // in-flight slice and releases no heap allocation.
    std::optional<ReplayPredictionAmortizedTask> m_task;
};

enum class ReplayPredictionCoalescerAction : uint8_t
{
    Nothing,
    Begin,
    Supersede,
    PromoteAndBegin
};

namespace ReplayPredictionSchedulingOperations
{
double ReplayPredictionElapsedMilliseconds( const std::chrono::steady_clock::time_point& start );
bool ReplayPredictionBudgetExpired( const std::chrono::steady_clock::time_point& start, double budgetMilliseconds );
bool ReplayPredictionBudgetExpiredForPass(
    ReplayPredictionUpdateResult& result,
    SkullbonezCore::Core::MainMemoryReplayBudgetPass pass,
    const std::chrono::steady_clock::time_point& start,
    double budgetMilliseconds
);
double
ReplayPredictionRemainingMilliseconds( const std::chrono::steady_clock::time_point& start, double budgetMilliseconds );
double ReplayPredictionRevealSecondsPerSecond( const RunReplayPredictionState& prediction );
ReplayFrameIndex
ReplayPredictionRevealFrameIndex( RunReplayPredictionState& prediction, ReplayFrameIndex lastAvailableFrame );
std::size_t ReplayPredictionBuildPresentationFrameCountForRefresh(
    RunReplayPredictionState& prediction,
    Physics::PhysicsSceneObjectId requestedTargetId
);

inline ReplayPredictionBuildMode ChooseReplayPredictionBuildMode(
    double measuredTicksPerMs,
    int remainingTicks,
    double instantBudgetMs,
    std::size_t bodyCount
) noexcept
{
    if ( measuredTicksPerMs <= 0.0 || remainingTicks < 0 )
    {
        return ReplayPredictionBuildMode::Undecided;
    }
    constexpr std::size_t REPLAY_PREDICTION_INSTANT_MAX_BODY_COUNT = 64u;
    if ( instantBudgetMs <= 0.0 || bodyCount > REPLAY_PREDICTION_INSTANT_MAX_BODY_COUNT )
    {
        // Why: the probe samples the cheap beginning of a prediction. In a
        // large contact scene such as the 200-box wall, that prefix cannot
        // predict the later solver fan-out; allowing it to select Instant made
        // presentation mode depend on machine timing and revealed bursty paths.
        return ReplayPredictionBuildMode::Amortized;
    }

    const double projectedMilliseconds = static_cast<double>( remainingTicks ) / measuredTicksPerMs;
    return projectedMilliseconds <= instantBudgetMs ? ReplayPredictionBuildMode::Instant
                                                    : ReplayPredictionBuildMode::Amortized;
}

inline ReplayPredictionCoalescerAction ChooseReplayPredictionCoalescerAction(
    bool dirty,
    bool building,
    ReplayPredictionBuildMode mode,
    bool pendingLatestRestart,
    bool replacementPrefixPresented
) noexcept
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
    if ( mode == ReplayPredictionBuildMode::Instant || !replacementPrefixPresented )
    {
        // Invariant: held edits cannot cancel a replacement generation before
        // the frame thread presents one coherent changed prefix. The pending
        // bit keeps only the newest follow-up request.
        return ReplayPredictionCoalescerAction::Supersede;
    }
    return ReplayPredictionCoalescerAction::PromoteAndBegin;
}
} // namespace ReplayPredictionSchedulingOperations

} // namespace Runtime
} // namespace SkullbonezCore
