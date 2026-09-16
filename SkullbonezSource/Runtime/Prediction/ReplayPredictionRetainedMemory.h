// Prediction owns one Replay-only, demand-allocated budget shared by all buffers.
#pragma once

#include "../Replay/ReplayRetainedMemory.h"

namespace SkullbonezCore::Runtime
{
inline constexpr const char* REPLAY_PREDICTION_RESERVE_OWNER = "replay_prediction_working_set";

// Why: dense long horizons can exceed the old 960 MiB allowance before a
// single future completes. This is permission to grow, never an initial reserve.
// Live allocations and pending grants across both banks and additional prediction
// owners share 8 GiB; optional evidence has a 4 GiB ceiling per bank.
// Individual requests retain int-sized capacity arithmetic.
inline constexpr uint64_t REPLAY_PREDICTION_RESERVE_HARD_BYTES = 8ull * 1024ull * 1024ull * 1024ull;
inline constexpr int REPLAY_PREDICTION_RESERVE_MAX_REQUEST_BYTES = 2147483647;

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
