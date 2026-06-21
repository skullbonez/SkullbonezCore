/*
File: SkullbonezSource/Fence.h
Purpose:
  Provides a reusable CPU fence for worker tasks.

Mental model:
  A fence starts with an expected signal count. Workers call Signal() as chunks
  finish; the caller can Wait() efficiently until the count reaches zero.

Glossary:
  Fence: Synchronization primitive that lets one thread wait until a set of
  worker jobs has signaled completion.
  Signal: Completion notification from one worker job.
  Wait: Blocking call that sleeps until the fence has no remaining signals.

Related:
  - Agentic/Plans/worker-system-plan.md
  - SkullbonezSource/WorkerPool.h
*/

#pragma once

#include <atomic>
#include <condition_variable>
#include <memory>
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

using FenceHandle = std::shared_ptr<Fence>;

} // namespace Threading
} // namespace SkullbonezCore
