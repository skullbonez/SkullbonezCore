/*
File: SceneRequestQueue.cpp
Purpose:
  Implements the fixed scene-owner request ring.

Summary:
  Producers submit already-decoded scene intent. A frame checkpoint takes an
  ordered batch, clearing the owner ring before any scene load can re-enter UI.

Glossary:
  Owner budget: Fixed number of requests permitted between frame drains.

Invariants:
  - A taken batch preserves exact FIFO order.
  - Additional same-frame transitions are counted as rejected, not deferred.
  - Invalid create text never consumes a queue slot.

Related:
  - SkullbonezSource/Runtime/Scene/SceneRequestQueue.h
  - Agentic/Reference/engine-glossary.md
*/
#include "SceneRequestQueue.h"
#include "../../Core/SbDiagnosticStore.h"

#include "../../Core/FatalError.h"

#include <cstring>

namespace SkullbonezCore
{
namespace Runtime
{
SkullbonezCore::Core::SbResult SceneRequestQueue::Submit( SkullbonezCore::Core::SbDiagnosticStore& diagnostics,
                                                          const SceneRequest& request )
{
    if ( request.type == SceneRequestType::CreateScene )
    {
        const std::size_t textLength = strnlen_s( request.text, SCENE_REQUEST_TEXT_CAPACITY );

        if ( textLength >= SCENE_REQUEST_TEXT_CAPACITY )
        {
            return diagnostics.Failure( "Runtime/SceneRequestQueue", "Scene name exceeds the fixed %d-byte request payload",
                                        SCENE_REQUEST_TEXT_CAPACITY - 1 );
        }
    }

    if ( m_count >= SCENE_REQUEST_QUEUE_CAPACITY )
    {

        // Lane F: UI/input cannot legally emit more scene intents than the
        // owner budget between drains; growing here would allocate in runtime.
        SB_FATAL( "Runtime/SceneRequestQueue", "Scene request capacity exhausted. capacity=%d high_water=%d phase=input",
                  SCENE_REQUEST_QUEUE_CAPACITY, m_count );
    }

    const int tail = ( m_head + m_count ) % SCENE_REQUEST_QUEUE_CAPACITY;
    m_requests[tail] = request;
    ++m_count;
    return SkullbonezCore::Core::SbResult::Success();
}


SceneRequestBatch SceneRequestQueue::TakePending()
{
    SceneRequestBatch batch;
    bool hasTransition = false;

    while ( m_count > 0 )
    {
        const SceneRequest request = m_requests[m_head];
        m_requests[m_head] = {};

        m_head = ( m_head + 1 ) % SCENE_REQUEST_QUEUE_CAPACITY;
        --m_count;

        if ( SceneRequestIsTransition( request.type ) )
        {
            if ( hasTransition )
            {
                ++batch.rejectedTransitionCount;
                continue;
            }

            hasTransition = true;
        }

        batch.requests[batch.count++] = request;
    }

    m_head = 0;
    return batch;
}

bool SceneRequestQueue::HasTransition() const
{
    for ( int offset = 0; offset < m_count; ++offset )
    {
        const int index = ( m_head + offset ) % SCENE_REQUEST_QUEUE_CAPACITY;

        if ( SceneRequestIsTransition( m_requests[index].type ) )
        {
            return true;
        }
    }

    return false;
}


std::size_t SceneRequestQueue::Size() const
{
    return static_cast<std::size_t>( m_count );
}
} // namespace Runtime
} // namespace SkullbonezCore
