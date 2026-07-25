/*
File: SkullbonezSource/Runtime/Replay/TrajectoryStore.h
Purpose:
  Defines the replay trajectory store: versioned path records with explicit
  published point prefixes for past and future overlay lanes.

Summary:
  Builders own mutation. Renderers read only records whose `publishedPointCount`
  says a complete prefix is available. Replacing a record increments its
  version; appending points under the published prefix does not.

Glossary:
  Trajectory lane: Named path category such as past root, future root, child
    incoming/outgoing, retained trail, or baseline root.
  Published prefix: Count of points a reader may consume from a record.
  Record version: Monotonic identity for a replaced record; readers can detect
    replacement without comparing point arrays.

Invariants:
  - Points append only while capacity is already reserved; append failure must
    be handled by the builder without growing from the draw path.
  - Replacing a record resets its published prefix to zero and assigns a new
    version before any new points become visible.

Related:
  - SkullbonezSource/Runtime/Replay/ReplayRuntime.h
  - SkullbonezSource/Runtime/Replay/ReplayPrediction.cpp
*/
#pragma once

#include "ReplayRecorder.h"

#include <cstddef>
#include <cstdint>
#include <vector>

namespace SkullbonezCore::Runtime
{
enum class ReplayTrajectoryLane : uint8_t
{
    PastRoot,
    FutureRoot,
    FutureChildIncoming,
    FutureChildOutgoing,
    RetainedTrail,
    BaselineRoot
};

struct ReplayTrajectoryRecordKey
{
    Physics::PhysicsSceneObjectId bodyId;
    ReplayTrajectoryLane lane = ReplayTrajectoryLane::PastRoot;
    uint16_t branchOrdinal = 0;
};

struct ReplayTrajectoryPoint
{
    ReplayFrameIndex frameIndex = 0;
    Math::Vector::Vector3 position = Math::Vector::ZERO_VECTOR;
};

struct ReplayTrajectoryRecord
{
    ReplayTrajectoryRecordKey key;
    uint32_t version = 0;
    std::size_t publishedPointCount = 0;
    uint16_t styleId = 0;
    Physics::PhysicsSceneObjectId parentId;
    int depth = 0;
    ReplayFrameIndex firstFrame = 0;
    bool contactDerived = false;
    std::vector<ReplayTrajectoryPoint> points;
};

struct ReplayTrajectoryStore
{
    std::vector<ReplayTrajectoryRecord> records;
    uint32_t nextVersion = 1;
    // Invariant: this token changes only when a reader-visible record identity
    // or published prefix changes. Retained draw lists use it as the O(1)
    // invalidation check that keeps stable prediction frames off the CPU.
    uint64_t publicationVersion = 1;

    void Clear() noexcept;
    ReplayTrajectoryRecord* FindRecord( const ReplayTrajectoryRecordKey& key ) noexcept;
    const ReplayTrajectoryRecord* FindRecord( const ReplayTrajectoryRecordKey& key ) const noexcept;
    ReplayTrajectoryRecord* BeginReplaceRecord( const ReplayTrajectoryRecordKey& key,
                                                uint16_t styleId,
                                                Physics::PhysicsSceneObjectId parentId,
                                                int depth,
                                                ReplayFrameIndex firstFrame,
                                                bool contactDerived );
    bool TryAppendPoint( ReplayTrajectoryRecord& record, const ReplayTrajectoryPoint& point );
    void PublishPrefix( ReplayTrajectoryRecord& record, std::size_t pointCount ) noexcept;
    // Removes expired published points without replacing the record/version, so
    // the renderer always sees one continuous retained-path publication.
    std::size_t TrimPublishedPointsBeforeFrame( ReplayTrajectoryRecord& record,
                                                ReplayFrameIndex firstRetainedFrame ) noexcept;
    bool ReserveRecords( std::size_t requestedCapacity, int frameNumber );
    bool ReserveRecordPoints( ReplayTrajectoryRecord& record, std::size_t requestedCapacity, int frameNumber );
    std::size_t RecordCount() const noexcept;
    std::size_t PointCount() const noexcept;
    uint64_t CapacityBytes() const noexcept;

  private:
    uint32_t AllocateVersion() noexcept;
};

bool operator==( const ReplayTrajectoryRecordKey& lhs, const ReplayTrajectoryRecordKey& rhs ) noexcept;
bool operator!=( const ReplayTrajectoryRecordKey& lhs, const ReplayTrajectoryRecordKey& rhs ) noexcept;
} // namespace SkullbonezCore::Runtime
