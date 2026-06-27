/*
File: SkullbonezSource/Core/AmortizedTask.cpp
Purpose:
  Implements worker-backed multi-frame chunk processing.

Mental model:
  Only one tick is allowed in flight. The worker advances an atomic cursor by
  the current budget, processes that slice, and marks completion when the cursor
  reaches the total item count.

Glossary:
  Worker pool: Persistent thread group that runs bounded jobs outside the main
  thread.
  Atomic cursor: Thread-safe index used to claim each slice once.
  Budget: Maximum item count processed by one submitted chunk.

Invariants:
  - At most one worker chunk is in flight per task; SubmitTick is a no-op while
    the previous chunk still owns the cursor.
  - The cursor only advances by claimed budgets, and completion is published
    after the last claimed range reaches totalItems.

Related:
  - SkullbonezSource/Core/AmortizedTask.h
  - SkullbonezSource/Core/WorkerPool.h
*/

#include "AmortizedTask.h"

#include "WorkerPool.h"

#include <algorithm>
#include <utility>

namespace SkullbonezCore
{
namespace Threading
{

AmortizedTask::AmortizedTask( int totalItems, int itemsPerTick, WorkFunction work )
    : m_totalItems( (std::max)( 0, totalItems ) ), m_itemsPerTick( (std::max)( 1, itemsPerTick ) ), m_cursor( 0 ),
      m_complete( totalItems <= 0 ), m_inFlight( false ), m_work( std::move( work ) )
{
}


void AmortizedTask::SubmitTick( WorkerPool& pool )
{
    if ( IsComplete() || !m_work )
    {
        return;
    }

    bool expected = false;
    if ( !m_inFlight.compare_exchange_strong( expected, true, std::memory_order_acq_rel ) )
    {
        // Invariant: callers may tick this every frame, but the worker owns the
        // current range until it clears m_inFlight.
        return;
    }

    pool.Submit(
        [this]()
        {
            const int budget = (std::max)( 1, m_itemsPerTick.load( std::memory_order_acquire ) );
            const int begin = m_cursor.fetch_add( budget, std::memory_order_acq_rel );
            if ( begin >= m_totalItems )
            {
                m_complete.store( true, std::memory_order_release );
                m_inFlight.store( false, std::memory_order_release );
                return;
            }

            const int end = (std::min)( m_totalItems, begin + budget );
            m_work( begin, end );

            if ( end >= m_totalItems )
            {
                m_complete.store( true, std::memory_order_release );
            }
            m_inFlight.store( false, std::memory_order_release );
        } );
}


bool AmortizedTask::IsComplete() const
{
    return m_complete.load( std::memory_order_acquire );
}


bool AmortizedTask::IsInFlight() const
{
    return m_inFlight.load( std::memory_order_acquire );
}


void AmortizedTask::Reset()
{
    if ( IsInFlight() )
    {
        return;
    }
    m_cursor.store( 0, std::memory_order_release );
    m_complete.store( m_totalItems <= 0, std::memory_order_release );
}


float AmortizedTask::GetProgress() const
{
    if ( m_totalItems <= 0 )
    {
        return 1.0f;
    }

    const int cursor = (std::min)( m_totalItems, m_cursor.load( std::memory_order_acquire ) );
    return static_cast<float>( cursor ) / static_cast<float>( m_totalItems );
}


void AmortizedTask::SetBudget( int itemsPerTick )
{
    m_itemsPerTick.store( (std::max)( 1, itemsPerTick ), std::memory_order_release );
}

} // namespace Threading
} // namespace SkullbonezCore
