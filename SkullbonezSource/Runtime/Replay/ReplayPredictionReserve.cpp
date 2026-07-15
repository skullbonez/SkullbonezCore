/*
File: SkullbonezSource/Runtime/Replay/ReplayPredictionReserve.cpp
Purpose:
  Registers and requests growth from the replay prediction working-set owner.

Summary:
  RuntimeReserveAllocator is the policy gate; this file is the replay-specific
  adapter that supplies the owner metadata and rejects impossible byte counts
  before callers enter allocation scopes.

Glossary:
  Growth request: RuntimeReserveAllocator check that approves one capacity
    increase before the code performs the allocation under matching scopes.
  Hard cap: Maximum byte capacity the replay prediction working set may hold.

Invariants:
  - `RequestGrowth` is called only here for replay prediction working-set
    storage; caller files reserve vectors through this wrapper.
  - A denied request is non-fatal to the process. Callers decide whether to keep
    partial UI data, retry later, or cancel the current prediction build.

Related:
  - SkullbonezSource/Runtime/Allocation/RuntimeReserveAllocator.h
  - SkullbonezSource/Runtime/Replay/ReplayPredictionReserve.h
*/
#include "ReplayPredictionReserve.h"

namespace SkullbonezCore::Runtime
{
namespace
{
// Runtime allocation policy: prediction scratch can grow as the user explores
// larger retained paths. The registered hard cap is a real byte ceiling, not a
// theoretical element-count product; growth count is telemetry so interactive
// replay does not trip a per-run count fuse.
constexpr int REPLAY_PREDICTION_RESERVE_GROWTH_LIMIT =
    Runtime::Allocation::RUNTIME_RESERVE_REPLAY_GROWTH_LIMIT_UNBOUNDED;
} // namespace

namespace ReplayPredictionReserveOperations
{
Runtime::Allocation::RuntimeReserveOwnerHandle ReplayPredictionReserveOwner() noexcept
{
    static const Runtime::Allocation::RuntimeReserveOwnerHandle owner =
        Runtime::Allocation::RuntimeReserveAllocator::RegisterOwner(
            { REPLAY_PREDICTION_RESERVE_OWNER,
              Runtime::Allocation::RuntimeReserveSubsystem::Replay,
              Runtime::Allocation::RuntimeReservePhase::Replay,
              0,
              REPLAY_PREDICTION_RESERVE_HARD_BYTES,
              REPLAY_PREDICTION_RESERVE_GROWTH_LIMIT,
              true,
              "replay prediction supports large retained path visualization under a hard byte budget" } );
    return owner;
}

bool RequestReplayPredictionReserveGrowth( const char* targetName,
                                           int frameNumber,
                                           int oldCapacityBytes,
                                           int requestedCapacityBytes,
                                           int elementSizeBytes,
                                           Runtime::Allocation::RuntimeReserveGrowthResult& outResult ) noexcept
{
    outResult = {};
    if ( !targetName || oldCapacityBytes < 0 || requestedCapacityBytes <= oldCapacityBytes ||
         requestedCapacityBytes > REPLAY_PREDICTION_RESERVE_HARD_BYTES || elementSizeBytes <= 0 )
    {
        return false;
    }

    const Runtime::Allocation::RuntimeReserveOwnerHandle owner = ReplayPredictionReserveOwner();
    const Runtime::Allocation::RuntimeReserveGrowthRequest request = { REPLAY_PREDICTION_RESERVE_OWNER,
                                                                       targetName,
                                                                       Runtime::Allocation::RuntimeReservePhase::Replay,
                                                                       frameNumber,
                                                                       oldCapacityBytes,
                                                                       requestedCapacityBytes,
                                                                       elementSizeBytes };
    outResult = Runtime::Allocation::RuntimeReserveAllocator::RequestGrowth( owner, request );
    return outResult.granted;
}
} // namespace ReplayPredictionReserveOperations
} // namespace SkullbonezCore::Runtime
