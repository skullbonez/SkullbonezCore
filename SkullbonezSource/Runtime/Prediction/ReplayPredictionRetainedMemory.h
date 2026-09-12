/*
File: SkullbonezSource/Runtime/Prediction/ReplayPredictionRetainedMemory.h
Purpose:
  Owns the registered runtime-growth policy for Prediction's working set.

Summary:
  Prediction registers one Replay-phase growth owner for retained frame,
  physics, trajectory, and exact solver-evidence storage. Its measured
  high-water fact includes the representative dense 120-second evidence matrix
  with simultaneous build/committed banks.

Glossary:
  Working set: Prediction buffers retained while a future build is generated.
  Prefix: Contiguous prediction frames safe to publish to readers.

Invariants:
  - The owner remains replay_prediction_working_set.
  - The hard cap is 960 MiB and the phase remains Replay.
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

// A representative dense 120-second run measured a 317,157,376-byte evidence
// bank. Two such banks plus the prior 18,701,760-byte working-set high-water
// total 653,016,512 bytes. The 960 MiB cap preserves 1.542x headroom over that
// coexistence peak. Velocity divergence retains a second complete Prediction
// owner under this same cap; both owners' frames, engines, trajectories, and
// evidence are counted together. Creating an additional owner reclaims only
// the original's spare evidence bank. Each additional-owner generation releases
// its superseded diagnostics and reserves both path banks before optional
// evidence capture. Original committed evidence stays intact; neither the cap
// nor the Replay-only growth privilege expands. App's optional Original path
// snapshot shares this registration and cap; its at-most 27,000 compact records
// are allocated on the first changed vector and released on accept/cancel.
inline constexpr int REPLAY_PREDICTION_RESERVE_HARD_BYTES = 960 * 1024 * 1024;

inline constexpr ReplayGrowthOwnerPolicy REPLAY_PREDICTION_GROWTH_OWNER_POLICY { REPLAY_PREDICTION_RESERVE_OWNER,
                                                                                 SkullbonezCore::Core::Allocation::RuntimeReservePhase::Replay,
                                                                                 REPLAY_PREDICTION_RESERVE_HARD_BYTES,
                                                                                 653016512u,
                                                                                 ReplayGrowthExhaustionRule::CancelPredictionBuild };
namespace ReplayPredictionReserveOperations
{
SkullbonezCore::Core::Allocation::RuntimeReserveOwnerHandle ReplayPredictionReserveOwner() noexcept;
bool RequestReplayPredictionReserveGrowth( const char* targetName,
                                           int frameNumber,
                                           int oldCapacityBytes,
                                           int requestedCapacityBytes,
                                           int elementSizeBytes,
                                           SkullbonezCore::Core::Allocation::RuntimeReserveGrowthResult& outResult,
                                           uint64_t allocationBytes = 0u ) noexcept;

} // namespace ReplayPredictionReserveOperations
} // namespace SkullbonezCore::Runtime
