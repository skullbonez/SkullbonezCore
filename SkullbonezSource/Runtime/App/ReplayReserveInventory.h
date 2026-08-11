/*
File: SkullbonezSource/Runtime/App/ReplayReserveInventory.h
Purpose:
  Aggregates the registered growth-owner policies from Replay, Physics, and Prediction.

Summary:
  Runtime/App is the only boundary allowed to assemble the cross-package
  diagnostics inventory. Package-local policy rows stay with their owners.

Glossary:
  Growth owner: Registered subsystem allowed bounded post-gameplay reserve growth.
  Policy row: Owner name, phase gate, byte cap, counter, and denial behavior.

Invariants:
  - Stable inventory order is recorder, solver snapshot, prediction working set.
  - The inventory contains exactly the three pre-partition owners and caps.
  - Lookup returns pointers into the stable constexpr inventory.

Related:
  - SkullbonezSource/Runtime/Replay/ReplayRetainedMemory.h
  - SkullbonezSource/Runtime/Prediction/ReplayPredictionRetainedMemory.h
*/
#pragma once

#include "../Replay/ReplayRetainedMemory.h"
#include "../Prediction/ReplayPredictionRetainedMemory.h"

namespace SkullbonezCore::Runtime
{
inline constexpr std::array<ReplayGrowthOwnerPolicy, 3> REPLAY_GROWTH_OWNER_POLICIES = { REPLAY_CORE_GROWTH_OWNER_POLICIES[0], REPLAY_CORE_GROWTH_OWNER_POLICIES[1], REPLAY_PREDICTION_GROWTH_OWNER_POLICY };

inline const ReplayGrowthOwnerPolicy* FindReplayGrowthOwnerPolicy( const char* ownerName ) noexcept
{
    if ( !ownerName )
    {
        return nullptr;
    }

    for ( const ReplayGrowthOwnerPolicy& policy : REPLAY_GROWTH_OWNER_POLICIES )
    {
        const char* lhs = policy.ownerName;
        const char* rhs = ownerName;

        while ( *lhs != '\0' && *lhs == *rhs )
        {
            ++lhs;
            ++rhs;
        }

        if ( *lhs == '\0' && *rhs == '\0' )
        {
            return &policy;
        }
    }

    return nullptr;
}
} // namespace SkullbonezCore::Runtime
