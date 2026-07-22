/*
File: ReplayPredictionPublication.h
Purpose:
  Owns the release/acquire publication protocol for replay prediction frames.

Summary:
  A worker fills pre-sized frame slots and release-publishes one contiguous
  prefix. Frame-thread readers acquire that cursor before inspecting rows.

Glossary:
  Published prefix: Contiguous build-frame slots safe for readers.

Invariants:
  - A slot is published only after its frame and trajectory writes complete.
  - Reset invalidates the whole prefix and clears the worker-failure signal.

Related:
  - ReplayPrediction.h embeds this owner in the active build state.
*/
#pragma once

#include <atomic>
#include <cstddef>

namespace SkullbonezCore
{
namespace Runtime
{
class ReplayPredictionPublication
{
  public:
    std::size_t PublishedCount( std::size_t frameCapacity ) const noexcept
    {
        const std::size_t count = m_publishedCount.load( std::memory_order_acquire );
        return count < frameCapacity ? count : frameCapacity;
    }

    void Reset() noexcept
    {
        m_publishedCount.store( 0, std::memory_order_release );
        m_workerFailed.store( false, std::memory_order_release );
    }

    void PublishSlot( std::size_t frameSlot, std::size_t frameCapacity ) noexcept
    {
        const std::size_t count = frameSlot < frameCapacity ? frameSlot + 1u : frameCapacity;
        if ( count > m_publishedCount.load( std::memory_order_relaxed ) )
        {
            m_publishedCount.store( count, std::memory_order_release );
        }
    }

    void MarkWorkerFailed() noexcept
    {
        m_workerFailed.store( true, std::memory_order_release );
    }

    bool WorkerFailed() const noexcept
    {
        return m_workerFailed.load( std::memory_order_acquire );
    }

  private:
    std::atomic<std::size_t> m_publishedCount{ 0 };
    std::atomic<bool> m_workerFailed{ false };
};
} // namespace Runtime
} // namespace SkullbonezCore
