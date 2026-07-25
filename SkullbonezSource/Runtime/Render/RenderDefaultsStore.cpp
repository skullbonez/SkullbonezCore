/*
File: RenderDefaultsStore.cpp
Purpose:
  Applies deferred render-default saves at the final input-frame checkpoint.

Summary:
  The queue records intent, not values. Draining reads the final ordinary and
  cinematic config references supplied by the composition root, then returns
  only successful owner events for replay serialization.

Glossary:
  Accepted event: Persistence request whose complete file rewrite succeeded.
  Lane R: Recoverable filesystem/config failure returned with owner diagnostics.

Invariants:
  - Request order is preserved even when ordinary and cinematic saves interleave.
  - The first failure is retained while later queued requests still get a chance to save.

Related:
  - SkullbonezSource/Runtime/Render/RenderDefaultsStore.h
  - SkullbonezSource/Runtime/Scene/SceneRuntimeDefaults.cpp
*/
#include "RenderDefaultsStore.h"

#include "../../Core/FatalError.h"
#include "../Scene/SceneRuntimeDefaults.h"

namespace SkullbonezCore
{
namespace Runtime
{
void RenderDefaultsStore::CaptureStartupCinematicBaseline(
    const SkullbonezCore::Core::CinematicRenderConfig& cinematic
)
{
    m_cinematicBaseline = cinematic;
}


const SkullbonezCore::Core::CinematicRenderConfig& RenderDefaultsStore::CinematicBaseline() const
{
    return m_cinematicBaseline;
}


void RenderDefaultsStore::SubmitOrdinarySave()
{
    Submit( RenderDefaultsRequestType::Ordinary );
}


void RenderDefaultsStore::SubmitCinematicSave()
{
    Submit( RenderDefaultsRequestType::Cinematic );
}


void RenderDefaultsStore::Submit( RenderDefaultsRequestType type )
{
    if ( m_count >= RENDER_DEFAULTS_REQUEST_CAPACITY )
    {
        // Lane F: a UI frame cannot legally exceed the fixed persistence owner
        // budget. A growth fallback would violate steady-runtime allocation policy.
        SB_FATAL(
            "Runtime/RenderDefaultsStore",
            "Render-default request capacity exhausted. capacity=%d high_water=%d phase=input",
            RENDER_DEFAULTS_REQUEST_CAPACITY,
            m_count
        );
    }

    const int tail = ( m_head + m_count ) % RENDER_DEFAULTS_REQUEST_CAPACITY;
    m_requests[tail] = type;
    ++m_count;
}


RenderDefaultsSaveBatchResult RenderDefaultsStore::DrainAtFrameCheckpoint(
    const SkullbonezCore::Core::OrdinaryRenderConfig& ordinary,
    const SkullbonezCore::Core::CinematicRenderConfig& cinematic
)
{
    RenderDefaultsSaveBatchResult result;
    while ( m_count > 0 )
    {
        const RenderDefaultsRequestType request = m_requests[m_head];
        m_head = ( m_head + 1 ) % RENDER_DEFAULTS_REQUEST_CAPACITY;
        --m_count;

        const SkullbonezCore::Core::SbResult saveResult = request == RenderDefaultsRequestType::Ordinary
                                                              ? SaveRenderDefaults( ordinary )
                                                              : SaveSkyDefaults( cinematic );
        if ( saveResult.ok )
        {
            result.saved[result.savedCount++] = request;
        }
        else
        {
            if ( result.status.ok )
            {
                result.status = saveResult;
            }
            ++result.failedCount;
        }
    }
    m_head = 0;
    return result;
}


std::size_t RenderDefaultsStore::PendingCount() const
{
    return static_cast<std::size_t>( m_count );
}


RenderDefaultsRequestType RenderDefaultsStore::PendingTypeAt( std::size_t index ) const
{
    if ( index >= static_cast<std::size_t>( m_count ) )
    {
        // Lane F: this accessor is diagnostics/test evidence over occupied slots.
        SB_FATAL(
            "Runtime/RenderDefaultsStore",
            "Pending request index out of range. index=%zu count=%d",
            index,
            m_count
        );
    }
    return m_requests[( m_head + static_cast<int>( index ) ) % RENDER_DEFAULTS_REQUEST_CAPACITY];
}
} // namespace Runtime
} // namespace SkullbonezCore
