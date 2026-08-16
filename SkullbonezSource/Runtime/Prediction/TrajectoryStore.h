/*
File: SkullbonezSource/Runtime/Prediction/TrajectoryStore.h
Purpose:
  Defines Prediction's mutable trajectory store over Replay-owned value records.

Summary:
  Builders mutate an active record prefix while dormant slots retain their
  nested point capacity across Predict toggles. Renderers see only published
  points in the active prefix. A coherent committed flip retires obsolete
  prediction-bank records into dormant capacity without disturbing retained order;
  archive restore intentionally replaces storage.

Invariants:
  - Points append only while capacity is already reserved; append failure must
    be handled by the builder without growing from the draw path.
  - Replacing a record resets its published prefix to zero and assigns a new
    version before any new points become visible.
  - Dormant records are capacity only; no reader may inspect their stale keys or points.

Related:
  - SkullbonezSource/Runtime/Replay/ReplayTrajectoryPackets.h
  - SkullbonezSource/Runtime/Prediction/ReplayPrediction.cpp
  - Agentic/Reference/engine-glossary.md
*/
#pragma once

#include "../Replay/ReplayTrajectoryPackets.h"

#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

namespace SkullbonezCore::Runtime
{
// Concept: bank names identify future-trajectory key partitions, independently
// of which frame vector currently owns the captured visible prefix.
enum class ReplayPredictionTrajectoryBank : uint8_t
{
    Committed,
    Build
};

namespace ReplayTrajectoryStoreOperations
{
// Selects same-key capacity first, then the smallest sufficient slot, then the
// largest remaining slot. Best-fit preserves larger dormant capacities for
// later records instead of ratcheting total retained bytes after key churn.
// Returning records.size() means no dormant slot exists.
inline std::size_t SelectDormantRecordIndex( std::span<const ReplayTrajectoryRecord> records, std::size_t activeRecordCount,
                                             const ReplayTrajectoryRecordKey& key,
                                             std::size_t requiredPointCapacity ) noexcept
{
    if ( activeRecordCount >= records.size() )
    {
        return records.size();
    }

    std::size_t bestFitIndex = records.size();
    std::size_t largestIndex = activeRecordCount;

    for ( std::size_t index = activeRecordCount; index < records.size(); ++index )
    {
        const ReplayTrajectoryRecord& candidate = records[index];
        const bool keyMatches = candidate.key.bodyId.value == key.bodyId.value && candidate.key.lane == key.lane &&
                                candidate.key.branchOrdinal == key.branchOrdinal;

        if ( keyMatches && candidate.points.capacity() >= requiredPointCapacity )
        {
            return index;
        }

        if ( candidate.points.capacity() >= requiredPointCapacity &&
             ( bestFitIndex == records.size() || candidate.points.capacity() < records[bestFitIndex].points.capacity() ) )
        {
            bestFitIndex = index;
        }

        if ( candidate.points.capacity() > records[largestIndex].points.capacity() )
        {
            largestIndex = index;
        }
    }

    return bestFitIndex != records.size() ? bestFitIndex : largestIndex;
}
} // namespace ReplayTrajectoryStoreOperations

struct ReplayTrajectoryStore
{
    uint32_t nextVersion = 1;

    // Invariant: this token changes only when a reader-visible record identity
    // or published prefix changes. Retained draw lists use it as the O(1)
    // invalidation check that keeps stable prediction frames off the CPU.
    uint64_t publicationVersion = 1;

    // Logically removes every record without releasing warmed nested vectors.
    void Clear() noexcept;

    // Returns the only record range that presentation and serialization may read.
    std::span<const ReplayTrajectoryRecord> ActiveRecords() const noexcept
    {
        return { records.data(), activeRecordCount };
    }
    ReplayTrajectoryRecord* FindRecord( const ReplayTrajectoryRecordKey& key ) noexcept;
    const ReplayTrajectoryRecord* FindRecord( const ReplayTrajectoryRecordKey& key ) const noexcept;
    ReplayTrajectoryRecord* BeginReplaceRecord( const ReplayTrajectoryRecordKey& key, uint16_t styleId,
                                                Physics::PhysicsSceneObjectId parentId, int depth,
                                                ReplayFrameIndex firstFrame, bool contactDerived,
                                                std::size_t requiredPointCapacity );
    bool TryAppendPoint( ReplayTrajectoryRecord& record, const ReplayTrajectoryPoint& point );
    void PublishPrefix( ReplayTrajectoryRecord& record, std::size_t pointCount ) noexcept;

    // Retires one hidden prediction bank before replacement starts. Kept
    // records preserve relative order; retired records remain dormant capacity.
    std::size_t RetirePredictionBank( ReplayPredictionTrajectoryBank bank, uint16_t futureRootBuildBranch,
                                      uint16_t firstChildBuildBranch ) noexcept;

    // Atomically removes the old visible bank and normalizes a build-bank
    // replacement to committed keys. Non-prediction lanes remain untouched.
    std::size_t CommitPredictionReplacementBank( ReplayPredictionTrajectoryBank replacementBank,
                                                 uint16_t futureRootBuildBranch, uint16_t firstChildBuildBranch ) noexcept;

    // Removes expired published points without replacing the record/version, so
    // the renderer always sees one continuous retained-path publication.
    std::size_t TrimPublishedPointsBeforeFrame( ReplayTrajectoryRecord& record,
                                                ReplayFrameIndex firstRetainedFrame ) noexcept;
    bool ReserveRecords( std::size_t requestedCapacity, int frameNumber );
    bool ReserveRecordPoints( ReplayTrajectoryRecord& record, std::size_t requestedCapacity, int frameNumber );
    std::size_t RecordCount() const noexcept;
    std::size_t PointCount() const noexcept;
    uint64_t CapacityBytes() const noexcept;

    // Cold archive restore replaces both active content and retained capacity.
    void ReplaceRecordsFromArchive( std::vector<ReplayTrajectoryRecord>&& loadedRecords ) noexcept;

  private:

    // Invariant: readers see only this prefix. Clear retires its keys without
    // destroying nested point vectors; BeginReplaceRecord first reactivates a
    // dormant slot with the same key so each trajectory keeps its warmed cap.
    std::vector<ReplayTrajectoryRecord> records;
    std::size_t activeRecordCount = 0;

    std::size_t RetirePredictionBankRecords( ReplayPredictionTrajectoryBank bank, uint16_t futureRootBuildBranch,
                                             uint16_t firstChildBuildBranch ) noexcept;
    uint32_t AllocateVersion() noexcept;
};
} // namespace SkullbonezCore::Runtime
