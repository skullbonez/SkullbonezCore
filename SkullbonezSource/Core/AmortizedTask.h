/*
File: SkullbonezSource/Core/AmortizedTask.h
Purpose:
  Declares a worker-backed multi-frame chunked task helper.

Mental model:
  AmortizedTask is for latency-tolerant work. Each SubmitTick queues one bounded
  chunk on the worker pool and returns immediately; the caller polls progress.

Glossary:
  Amortized work: Large job split across multiple ticks so one frame does not
  pay the full cost.
  Worker pool: Persistent thread group that runs bounded jobs outside the main
  thread.
  In-flight chunk: Submitted worker slice that has not yet marked completion.

Invariants:
  - WorkFunction receives half-open ranges [begin, end) and must tolerate
    smaller final chunks.
  - Reset restarts the cursor but preserves the configured work callback and
    current per-tick budget.

Related:
  - SkullbonezSource/Core/WorkerPool.h
*/

#pragma once

#include <atomic>
#include <functional>

namespace SkullbonezCore
{
namespace Threading
{

class WorkerPool;

class AmortizedTask
{
  public:
    using WorkFunction = std::function<void( int begin, int end )>;

    AmortizedTask( int totalItems, int itemsPerTick, WorkFunction work );

    void SubmitTick( WorkerPool& pool );
    bool IsComplete() const;
    bool IsInFlight() const;
    void Reset();
    float GetProgress() const;
    void SetBudget( int itemsPerTick );

  private:
    int m_totalItems;
    std::atomic<int> m_itemsPerTick;
    std::atomic<int> m_cursor;
    std::atomic<bool> m_complete;
    std::atomic<bool> m_inFlight;
    WorkFunction m_work;
};

} // namespace Threading
} // namespace SkullbonezCore
