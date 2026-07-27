/*
File: SkullbonezSource/Runtime/Prediction/ReplayPredictionRetainedMemory.h
Purpose:
  Owns the registered runtime-growth policy for Prediction's working set.

Summary:
  The owner name, Replay phase gate, measured high-water mark, hard cap, and
  recoverable cancellation rule are unchanged by the package partition.

Glossary:
  Working set: Prediction buffers retained while a future build is generated.
  Prefix: Contiguous prediction frames safe to publish to readers.

Invariants:
  - The owner remains replay_prediction_working_set.
  - The hard cap remains 256 MiB and the phase remains Replay.
  - Denial cancels or truncates a build before publishing an incoherent prefix.

Related:
  - SkullbonezSource/Runtime/Prediction/ReplayPredictionReserve.h
  - SkullbonezSource/Runtime/App/ReplayReserveInventory.h
*/
#pragma once

#include "../Replay/ReplayRetainedMemory.h"

namespace SkullbonezCore::Runtime
{
inline constexpr const char* REPLAY_PREDICTION_RESERVE_OWNER = "replay_prediction_working_set";

// The strict two-generation probe measured 110,979,828 prediction bytes. The
// 256 MiB cap preserves 2.42x headroom for larger path/cause-tree generations.
inline constexpr int REPLAY_PREDICTION_RESERVE_HARD_BYTES = 256 * 1024 * 1024;

inline constexpr ReplayGrowthOwnerPolicy
    REPLAY_PREDICTION_GROWTH_OWNER_POLICY { REPLAY_PREDICTION_RESERVE_OWNER,
                                            SkullbonezCore::Core::Allocation::RuntimeReservePhase::Replay,
                                            REPLAY_PREDICTION_RESERVE_HARD_BYTES, 110979828u,
                                            ReplayGrowthExhaustionRule::CancelPredictionBuild };
} // namespace SkullbonezCore::Runtime
