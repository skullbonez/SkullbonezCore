/*
File: SkullbonezSource/Core/Fence.h
Purpose:
  Provides a reusable CPU fence for worker tasks.

Summary:
  A fence starts with an expected signal count. Workers call Signal() as chunks
  finish; the caller can Wait() efficiently until the count reaches zero.

Glossary:
  Signal: Completion notification from one worker job.
  Wait: Blocking call that sleeps until the fence has no remaining signals.

Invariants:
  - Reset publishes the exact number of required signals before any worker uses
    the fence for a new batch.
  - Signal may be called from workers, but Wait owns the blocking side and
    observes completion through the atomic remaining count.

Related:
  - SkullbonezSource/Core/WorkerPool.h
  - Agentic/Reference/engine-glossary.md
*/

#pragma once

#include <atomic>
#include <condition_variable>
#include <mutex>

namespace SkullbonezCore
{
namespace Threading
{

class Fence
{
  public:
    explicit Fence( int taskCount = 0 ) : m_remaining( taskCount )
    {
    }

    void Reset( int taskCount )
    {
        std::lock_guard<std::mutex> lock( m_mutex );
        m_remaining.store( taskCount, std::memory_order_release );

        if ( taskCount == 0 )
        {
            m_complete.notify_all();
        }
    }

    void Signal()
    {
        // Why: the atomic decrement publishes completion even when the final
        // signal arrives before the waiter reaches condition_variable::wait.
        const int previous = m_remaining.fetch_sub( 1, std::memory_order_acq_rel );

        if ( previous <= 1 )
        {
            std::lock_guard<std::mutex> lock( m_mutex );
            m_complete.notify_all();
        }
    }

    void Wait()
    {
        std::unique_lock<std::mutex> lock( m_mutex );
        m_complete.wait( lock, [&]() { return m_remaining.load( std::memory_order_acquire ) <= 0; } );
    }

    bool IsComplete() const
    {
        return m_remaining.load( std::memory_order_acquire ) <= 0;
    }

  private:
    std::atomic<int> m_remaining;
    mutable std::mutex m_mutex;
    std::condition_variable m_complete;
};

} // namespace Threading
} // namespace SkullbonezCore
