/*
File: ReplayPredictionScheduling.h
Purpose:
  Defines allocation-free scheduling decisions for replay prediction builds.

Summary:
  The worker schedule owns one in-place task, its in-flight join, and bounded
  submissions. Pure decisions select build mode and coalesce dirty requests.

Glossary:
  Instant build: One worker submission that completes the remaining horizon.
  Amortized build: Small worker submissions spread across render frames.
  Latest-wins restart: One pending rebuild bit representing every newer edit.

Invariants:
  - Scheduling decisions are pure and the schedule allocates no runtime memory.
  - Reset waits for an in-flight slice before releasing the task's stable borrows.
  - Instant work is never cancelled for a velocity refresh; the newest request
    begins after the current private-engine build completes.

Related:
  - ReplayPrediction.h stores the scheduling state.
  - ReplayPredictionScheduling.cpp owns cancellation and prefix promotion.
*/
#pragma once

#include "../../Core/AmortizedTask.h"
#include "ReplayPredictionPackets.h"

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
    CancelAndBegin
};

namespace ReplayPredictionSchedulingOperations
{
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
} // namespace ReplayPredictionSchedulingOperations

} // namespace Runtime
} // namespace SkullbonezCore
