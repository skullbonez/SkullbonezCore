/*
File: SkullbonezSource/Core/AmortizedTask.h
Purpose:
  Declares a worker-backed multi-frame chunked task helper.

Summary:
  AmortizedTask is for latency-tolerant work. Each SubmitTick queues one bounded
  chunk on the worker pool and returns immediately; the caller polls progress.
  A time-bounded callback may return the completed prefix so unfinished items
  remain at the cursor for the next submission.

Glossary:
  Amortized work: Large job split across multiple ticks so one frame does not
  pay the full cost.
  In-flight chunk: Submitted worker slice that has not yet marked completion.

Invariants:
  - WorkFunction receives half-open ranges [begin, end). A void result commits
    the whole range; an int result commits that many leading items.
  - Reset restarts the cursor only while idle, reports refusal while a worker
    owns the task, and preserves the configured callback and per-tick budget.
  - Destruction while a chunk is in flight is Lane F because the worker ring
    stores a raw pointer to this object.

Related:
  - SkullbonezSource/Core/WorkerPool.h
  - Agentic/Reference/engine-glossary.md
*/

#pragma once

#include "WorkerPool.h"
#include "FatalError.h"

#include <algorithm>
#include <atomic>
#include <type_traits>
#include <utility>

namespace SkullbonezCore
{
namespace Threading
{

template <typename WorkFunctionT> class AmortizedTask
{
    using WorkResult = std::invoke_result_t<WorkFunctionT&, int, int>;
    static_assert( std::is_void_v<WorkResult> || std::is_same_v<WorkResult, int>,
                   "AmortizedTask work must return void or a completed-prefix int." );

  public:
    AmortizedTask( int totalItems, int itemsPerTick, WorkFunctionT work )
        : m_totalItems( (std::max)( 0, totalItems ) ), m_itemsPerTick( (std::max)( 1, itemsPerTick ) ), m_cursor( 0 ),
          m_complete( totalItems <= 0 ), m_inFlight( false ), m_work( std::move( work ) )
    {
    }

    ~AmortizedTask()
    {

        if ( IsInFlight() )
        {

            // Hazard: SubmitNoAlloc stores this object's address in the fixed
            // worker ring. Returning from destruction would turn the queued
            // callback into a use-after-free.
            SB_FATAL( "Core/AmortizedTask", "Destroying AmortizedTask while worker chunk is in flight." );
        }
    }

    void SubmitTick( WorkerPool& pool )
    {

        if ( IsComplete() )
        {
            return;
        }

        bool expected = false;

        if ( !m_inFlight.compare_exchange_strong( expected, true, std::memory_order_acq_rel ) )
        {

            // Invariant: callers may tick this every frame, but the worker owns
            // the current range until it clears m_inFlight.
            return;
        }

        pool.SubmitNoAlloc( *this );
    }

    bool IsComplete() const
    {
        return m_complete.load( std::memory_order_acquire );
    }

    bool IsInFlight() const
    {
        return m_inFlight.load( std::memory_order_acquire );
    }

    bool Reset()
    {

        if ( IsInFlight() )
        {
            return false;
        }

        m_cursor.store( 0, std::memory_order_release );
        m_complete.store( m_totalItems <= 0, std::memory_order_release );
        return true;
    }

    float GetProgress() const
    {

        if ( m_totalItems <= 0 )
        {
            return 1.0f;
        }

        const int cursor = (std::min)( m_totalItems, m_cursor.load( std::memory_order_acquire ) );
        return static_cast<float>( cursor ) / static_cast<float>( m_totalItems );
    }

    void SetBudget( int itemsPerTick )
    {
        m_itemsPerTick.store( (std::max)( 1, itemsPerTick ), std::memory_order_release );
    }

  private:
    friend class WorkerPool;

    void ExecuteWorkerTask()
    {
        const int budget = (std::max)( 1, m_itemsPerTick.load( std::memory_order_acquire ) );
        const int begin = m_cursor.load( std::memory_order_acquire );

        if ( begin >= m_totalItems )
        {
            m_complete.store( true, std::memory_order_release );
            m_inFlight.store( false, std::memory_order_release );
            return;
        }

        const int end = (std::min)( m_totalItems, begin + budget );
        int committedEnd = end;

        if constexpr ( std::is_void_v<WorkResult> )
        {
            m_work( begin, end );
        }
        else
        {

            // Invariant: only the contiguous completed prefix advances. This
            // lets a timer stop work between indivisible items without losing
            // the unprocessed tail of the claimed range.
            const int completedItems = std::clamp( m_work( begin, end ), 0, end - begin );
            committedEnd = begin + completedItems;
        }

        m_cursor.store( committedEnd, std::memory_order_release );

        if ( committedEnd >= m_totalItems )
        {
            m_complete.store( true, std::memory_order_release );
        }

        m_inFlight.store( false, std::memory_order_release );
    }

    int m_totalItems;
    std::atomic<int> m_itemsPerTick;
    std::atomic<int> m_cursor;
    std::atomic<bool> m_complete;
    std::atomic<bool> m_inFlight;
    WorkFunctionT m_work;
};

} // namespace Threading
} // namespace SkullbonezCore
