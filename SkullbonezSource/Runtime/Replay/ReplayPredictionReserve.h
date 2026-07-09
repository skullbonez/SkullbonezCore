/*
File: SkullbonezSource/Runtime/Replay/ReplayPredictionReserve.h
Purpose:
  Names the replay prediction working-set reserve owner shared by prediction
  frames, future-node caches, the private prediction engine, and trajectory
  store storage.

Mental model:
  Replay prediction is allowed to grow during replay exploration, but only
  through one registered RuntimeReserveAllocator owner. Helpers in this file
  keep that owner name and hard byte cap from splintering across replay files.

Glossary:
  Runtime reserve owner: Registered allocation-policy row that can approve
    bounded replay-phase growth after steady gameplay has started.
  Replay prediction working set: The approved replay owner for future-frame
    samples, prediction scratch, and generated trajectory records.

Invariants:
  - Every runtime growth request for prediction or trajectory storage must use
    `REPLAY_PREDICTION_RESERVE_OWNER`.
  - The hard cap is byte-based and shared by all prediction working-set users.

Related:
  - SkullbonezSource/Runtime/Replay/RunReplayTools.cpp
  - SkullbonezSource/Runtime/Replay/TrajectoryStore.h
  - tools/allocation_policy_allowlist.json
*/
#pragma once

#include "../Allocation/RuntimeReserveAllocator.h"

namespace SkullbonezCore::Basics
{
inline constexpr const char* REPLAY_PREDICTION_RESERVE_OWNER = "replay_prediction_working_set";
inline constexpr int REPLAY_PREDICTION_RESERVE_HARD_BYTES = 256 * 1024 * 1024;

Runtime::Allocation::RuntimeReserveOwnerHandle ReplayPredictionReserveOwner() noexcept;
bool RequestReplayPredictionReserveGrowth( const char* targetName,
                                           int frameNumber,
                                           int oldCapacityBytes,
                                           int requestedCapacityBytes,
                                           int elementSizeBytes,
                                           Runtime::Allocation::RuntimeReserveGrowthResult& outResult ) noexcept;
} // namespace SkullbonezCore::Basics
