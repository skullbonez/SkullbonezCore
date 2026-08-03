/*
File: SkullbonezSource/Runtime/Replay/ReplayTrajectoryPackets.h
Purpose:
  Defines value records shared by replay packets and prediction-owned trajectory storage.

Summary:
  Replay visual packets publish immutable trajectory rows, while Prediction owns
  the mutable store that builds and versions those rows. Keeping the value
  vocabulary in the lower Replay package preserves the dependency direction:
  Prediction may consume Replay packets, but Replay never reaches into a
  Prediction owner.

Invariants:
  - These records carry values only; mutation policy and capacity authority
    remain with Runtime/Prediction/TrajectoryStore.
  - Scene object ids are durable identity. Branch ordinals distinguish bounded
    path rows within one prediction publication.

Related:
  - SkullbonezSource/Runtime/Prediction/TrajectoryStore.h
  - SkullbonezSource/Runtime/Replay/ReplayVisualPacket.h
  - Agentic/Reference/engine-glossary.md
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

bool operator==( const ReplayTrajectoryRecordKey& lhs, const ReplayTrajectoryRecordKey& rhs ) noexcept;
bool operator!=( const ReplayTrajectoryRecordKey& lhs, const ReplayTrajectoryRecordKey& rhs ) noexcept;
} // namespace SkullbonezCore::Runtime
