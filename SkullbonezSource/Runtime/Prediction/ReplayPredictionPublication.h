/*
File: SkullbonezSource/Runtime/Prediction/ReplayPredictionPublication.h
Purpose:
  Owns the release/acquire publication protocol for replay prediction frames.

Summary:
  A worker fills pre-sized frame slots and release-publishes one contiguous
  prefix. The frame thread latches one prepared prefix so every presentation
  consumer sees the same rows until the next preparation pass.

Glossary:
  Published prefix: Contiguous build-frame slots safe for readers.
  Prepared prefix: Published rows whose topology and trajectories were brought
    into coherence by the frame thread for one render pass.

Invariants:
  - A slot is published only after its frame and trajectory writes complete.
  - Worker publication cannot expand the prepared prefix during rendering.
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
    std::atomic<std::size_t> m_publishedCount { 0 };
    std::atomic<bool> m_workerFailed { false };
};

class ReplayPredictionPresentationPublication
{
  public:
    void Prepare( std::size_t publishedCount, std::size_t frameCapacity ) noexcept
    {
        m_preparedCount = publishedCount < frameCapacity ? publishedCount : frameCapacity;
    }

    std::size_t PresentedCount( std::size_t publishedCount, std::size_t frameCapacity ) const noexcept
    {
        const std::size_t boundedPublished = publishedCount < frameCapacity ? publishedCount : frameCapacity;
        return m_preparedCount < boundedPublished ? m_preparedCount : boundedPublished;
    }

    void Reset() noexcept
    {
        m_preparedCount = 0u;
    }

  private:
    std::size_t m_preparedCount = 0u;
};
} // namespace Runtime
} // namespace SkullbonezCore
