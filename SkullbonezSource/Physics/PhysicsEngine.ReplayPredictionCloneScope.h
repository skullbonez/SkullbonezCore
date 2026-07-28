/*
File: SkullbonezSource/Physics/PhysicsEngine.ReplayPredictionCloneScope.h
Purpose:
  Protects explicit Physics replay-prediction clones with exact owner identity.

Summary:
  Physics clone entry points are intentionally private or narrowly named, but
  their fixed-list backing can still allocate during Replay. This internal
  guard admits only the allocator registry row owned by the isolated prediction
  working set, not every Replay subsystem owner with growth permission.

Glossary:
  Canonical owner identity: Stable allocator ownerName registered by the one
    Runtime prediction working-set owner.
  Clone scope: Calling-thread phase and reserve-owner scopes surrounding one
    synchronous prediction seed.

Invariants:
  - Replay subsystem membership is insufficient authority: solver snapshots,
    recorder samples, and other Replay owners cannot clone Physics storage.
  - The literal below is a canonical registry policy identity, not an
    unforgeable token. It rejects differently named Replay owners without
    adding a Physics include or type dependency on Runtime/Prediction.

Related:
  - SkullbonezSource/Physics/PhysicsEngine.cpp
  - SkullbonezSource/Runtime/Prediction/ReplayPredictionReserve.cpp
*/
#pragma once

#include "../Core/FatalError.h"
#include "../Core/Allocation/RuntimeAllocationTracker.h"
#include "../Core/Allocation/RuntimeReserveAllocator.h"

#include <cstring>

namespace SkullbonezCore
{
namespace Physics
{
namespace Detail
{
inline constexpr const char* REPLAY_PREDICTION_CLONE_OWNER_NAME = "replay_prediction_working_set";


inline void RequireReplayPredictionCloneScope( const char* target )
{
    namespace CoreAllocation = SkullbonezCore::Core::Allocation;
    const CoreAllocation::RuntimeReservePhase phase = CoreAllocation::GetRuntimeAllocationPhase();
    const CoreAllocation::RuntimeReserveOwnerHandle owner = CoreAllocation::RuntimeReserveAllocator::CurrentOwner();
    CoreAllocation::RuntimeReserveOwnerStatsView ownerStats = {};
    const bool canonicalOwner = owner != CoreAllocation::INVALID_RUNTIME_RESERVE_OWNER &&
                                CoreAllocation::RuntimeReserveAllocator::CopyOwnerStats( owner, ownerStats ) &&
                                ownerStats.subsystem == CoreAllocation::RuntimeReserveSubsystem::Replay &&
                                ownerStats.allowReplayGrowth && ownerStats.ownerName &&
                                std::strcmp( ownerStats.ownerName, REPLAY_PREDICTION_CLONE_OWNER_NAME ) == 0;

    if ( phase != CoreAllocation::RuntimeReservePhase::Replay || !canonicalOwner )
    {
        SB_FATAL( "Physics/ReplayPredictionClone",
                  "%s requires the canonical ReplayPrediction owner scope. phase=%s owner=%u owner_name=%s "
                  "required_owner=%s.",
                  target ? target : "Physics replay prediction clone", CoreAllocation::RuntimeReservePhaseName( phase ),
                  static_cast<unsigned int>( owner ), ownerStats.ownerName ? ownerStats.ownerName : "<unregistered>",
                  REPLAY_PREDICTION_CLONE_OWNER_NAME );
    }
}
} // namespace Detail
} // namespace Physics
} // namespace SkullbonezCore
