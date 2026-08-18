/*
File: ContinuousPredictionSampleRing.h
Purpose:
  Owns fixed-capacity, absolute-tick publication for continuous prediction rows.

Summary:
  One prediction worker writes every configured body's position into a physical
  ring slot, then release-publishes the complete row. Readers acquire a logical
  oldest-to-newest interval and validate each atomic slot version while copying,
  so a concurrent wrap yields either one coherent row or an explicit retry.

Glossary:
  Logical row: Chronological index inside one acquired oldest-to-newest window.
  Physical segment: Contiguous storage run that backs part of a logical window;
    a wrapped window has two such runs but no edge from newest back to oldest.
  Slot version: Even value for a committed row and odd value while its physical
    slot is being overwritten.

Invariants:
  - Prepare is the only allocation boundary and is rejected after Start.
  - One producer writes rows in consecutive absolute-tick order.
  - A row is published only after every configured body is written exactly once.
  - Readers never perform non-atomic reads from worker-owned row storage.
  - Cancellation or failure suppresses new snapshots until ResetAfterJoin.
  - Absolute ticks and slot versions fail before unsigned rollover.

Related:
  - SkullbonezTests/TestReplayPredictionScheduling.cpp
  - SkullbonezSource/Runtime/Prediction/ReplayPredictionPublication.h
  - Agentic/Reference/engine-glossary.md
*/
#pragma once

#include "../../Maths/Vector3.h"

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

namespace SkullbonezCore
{
namespace Runtime
{
class ContinuousPredictionSampleRing;

// One contiguous physical run in chronological order. The private constructor
// prevents callers from inventing offsets that disagree with the snapshot.
class ContinuousPredictionSampleRingSegment
{
  public:
    std::size_t LogicalRowOffset() const noexcept
    {
        return m_logicalRowOffset;
    }

    std::size_t PhysicalRowOffset() const noexcept
    {
        return m_physicalRowOffset;
    }

    std::size_t RowCount() const noexcept
    {
        return m_rowCount;
    }

  private:
    friend class ContinuousPredictionSampleRingSnapshot;

    ContinuousPredictionSampleRingSegment( std::size_t logicalRowOffset, std::size_t physicalRowOffset,
                                           std::size_t rowCount ) noexcept
        : m_logicalRowOffset( logicalRowOffset ), m_physicalRowOffset( physicalRowOffset ), m_rowCount( rowCount )
    {
    }

    std::size_t m_logicalRowOffset = 0u;
    std::size_t m_physicalRowOffset = 0u;
    std::size_t m_rowCount = 0u;
};

// Lifetime: this snapshot borrows its ring. ResetAfterJoin and Prepare require
// all readers to have released their snapshots, just as worker retirement does.
class ContinuousPredictionSampleRingSnapshot
{
  public:
    bool Empty() const noexcept
    {
        return m_rowCount == 0u;
    }

    bool Cancelled() const noexcept
    {
        return m_cancelled;
    }

    bool Failed() const noexcept
    {
        return m_failed;
    }

    std::size_t RowCount() const noexcept
    {
        return m_rowCount;
    }

    std::size_t BodyCount() const noexcept
    {
        return m_bodyCount;
    }

    std::uint64_t OldestAbsoluteTick() const noexcept
    {
        return m_oldestAbsoluteTick;
    }

    std::uint64_t NewestAbsoluteTick() const noexcept
    {
        return m_newestAbsoluteTick;
    }

    std::size_t SegmentCount() const noexcept;
    ContinuousPredictionSampleRingSegment SegmentAt( std::size_t segmentIndex ) const noexcept;

    // Copies one logical row into caller-owned storage. False means the row was
    // overwritten or retired while copying; callers must ignore partial output.
    bool TryReadRow( std::size_t logicalRowIndex, std::span<Math::Vector::Vector3> outPositions,
                     std::uint64_t& outAbsoluteTick ) const noexcept;

  private:
    friend class ContinuousPredictionSampleRing;

    const ContinuousPredictionSampleRing* m_owner = nullptr;
    std::uint64_t m_storageGeneration = 0u;
    std::uint64_t m_oldestAbsoluteTick = 0u;
    std::uint64_t m_newestAbsoluteTick = 0u;
    std::size_t m_rowCount = 0u;
    std::size_t m_bodyCount = 0u;
    std::size_t m_rowCapacity = 0u;
    bool m_cancelled = false;
    bool m_failed = false;
};

class ContinuousPredictionSampleRing
{
  public:
    ContinuousPredictionSampleRing() = default;
    ContinuousPredictionSampleRing( const ContinuousPredictionSampleRing& ) = delete;
    ContinuousPredictionSampleRing& operator=( const ContinuousPredictionSampleRing& ) = delete;

    // Allocates the complete row store before worker use. Repeating the same
    // dimensions after ResetAfterJoin reuses storage without increasing the
    // storage generation.
    bool Prepare( std::size_t rowCapacity, std::size_t bodyCount ) noexcept;

    // Starts a fresh consecutive absolute-tick sequence. Production continuous
    // forecast starts at zero; a non-zero seed supports boundary tests.
    bool Start( std::uint64_t firstAbsoluteTick = 0u ) noexcept;

    bool BeginRow( std::uint64_t absoluteTick ) noexcept;
    bool WriteBodyPosition( std::size_t bodyIndex, const Math::Vector::Vector3& position ) noexcept;
    bool PublishRow() noexcept;

    void RequestCancellation() noexcept;

    // Caller contract: the producer is joined and no snapshot remains borrowed.
    // Storage is retained for a same-dimension Start; no allocation occurs here.
    void ResetAfterJoin() noexcept;

    ContinuousPredictionSampleRingSnapshot AcquireSnapshot() const noexcept;

    std::size_t RowCapacity() const noexcept
    {
        return m_rowCapacity;
    }

    std::size_t BodyCount() const noexcept
    {
        return m_bodyCount;
    }

    std::size_t RetainedBytes() const noexcept
    {
        return m_retainedBytes;
    }

    std::uint64_t StorageGeneration() const noexcept
    {
        return m_storageGeneration.load( std::memory_order_acquire );
    }

    bool Failed() const noexcept
    {
        return m_failed.load( std::memory_order_acquire );
    }

    bool CounterOverflowed() const noexcept
    {
        return m_counterOverflowed.load( std::memory_order_acquire );
    }

  private:
    friend class ContinuousPredictionSampleRingSnapshot;

    bool TryReadRow( const ContinuousPredictionSampleRingSnapshot& snapshot, std::size_t logicalRowIndex,
                     std::span<Math::Vector::Vector3> outPositions, std::uint64_t& outAbsoluteTick ) const noexcept;
    void MarkFailure( bool counterOverflow ) noexcept;

    // Runtime allocation policy: Prepare grows these four vectors only through
    // replay_prediction_working_set; Start freezes their capacity until join.
    std::vector<std::uint64_t> m_slotVersions;
    std::vector<std::uint64_t> m_slotTicks;
    std::vector<float> m_positionComponents;
    std::vector<std::uint8_t> m_bodyWritten;

    std::size_t m_rowCapacity = 0u;
    std::size_t m_bodyCount = 0u;
    std::size_t m_positionComponentCount = 0u;
    std::size_t m_retainedBytes = 0u;
    std::size_t m_openPhysicalRow = 0u;
    std::size_t m_writtenBodyCount = 0u;
    std::uint64_t m_nextAbsoluteTick = 0u;
    std::uint64_t m_openCommitVersion = 0u;
    bool m_rowOpen = false;

    std::atomic<std::uint64_t> m_storageGeneration { 0u };
    std::atomic<std::uint64_t> m_firstAbsoluteTick { 0u };
    std::atomic<std::uint64_t> m_publishedNextAbsoluteTick { 0u };
    std::atomic<bool> m_started { false };
    std::atomic<bool> m_cancelRequested { false };
    std::atomic<bool> m_failed { false };
    std::atomic<bool> m_counterOverflowed { false };
};
} // namespace Runtime
} // namespace SkullbonezCore
