/*
File: SkullbonezSource/Physics/PhysicsSceneVectorReserve.h
Purpose:
  Attributes cold scene-sized std::vector backing to concrete Physics owners.

Summary:
  This adapter applies named reserve policy when a Physics owner chooses raw
  std::vector storage instead of PhysicsRuntimeList. The present production tree
  has no callers; any adopter must supply the owner, phase, and hard-cap evidence
  required by RuntimeReserveAllocator.

Glossary:
  Retained backing: Physical vector allocation kept after a smaller scene
    lowers its logical capacity.
  Replay owner: Registered upper-layer allocation authority that admits the
    isolated prediction engine's cold reconstruction work.

Invariants:
  - Ordinary scene vectors grow only during the SceneLoad allocation phase.
  - Replay reconstruction grows only beneath an already-approved Replay owner;
    this helper does not register or widen Replay allocation authority.
  - SceneLoad growth registers each vector's stable owner name and hard row
    ceiling so attribution remains concrete.
  - Retained vector backing is monotonic; logical scene allowances remain with
    their concrete owners.

Related:
  - SkullbonezSource/Core/Allocation/RuntimeReserveAllocator.h
  - SkullbonezSource/Physics/PhysicsEngine.cpp
  - SkullbonezSource/Runtime/Prediction/ReplayPrediction.cpp
  - Agentic/Reference/engine-glossary.md
*/
#pragma once

#include "../Core/Allocation/RuntimeReserveAllocator.h"
#include "../Core/FatalError.h"

#include <climits>
#include <cstddef>
#include <vector>

namespace SkullbonezCore
{
namespace Physics
{
template <typename T>
void ReservePhysicsSceneVector( std::vector<T>& values, std::size_t requestedCapacity, std::size_t hardCapacity,
                                const char* ownerName, const char* capacityReason )
{
    if ( requestedCapacity <= values.capacity() )
    {
        return;
    }

    if ( requestedCapacity > hardCapacity || requestedCapacity > static_cast<std::size_t>( INT_MAX ) )
    {
        SB_FATAL( "Physics/SceneCapacity",
                  "Scene vector capacity exceeds its hard ceiling: owner=%s requested=%zu ceiling=%zu.",
                  ownerName ? ownerName : "Physics/Unknown", requestedCapacity, hardCapacity );
    }

    using namespace SkullbonezCore::Core::Allocation;
    const RuntimeReservePhase phase = GetRuntimeAllocationPhase();

    if ( phase == RuntimeReservePhase::Replay )
    {
        const RuntimeReserveOwnerHandle replayOwner = RuntimeReserveAllocator::CurrentOwner();

        // ReplayPrediction owns one outer byte-budget approval for its private
        // engine and nested fixed/vector stores. Scene vectors may consume that
        // already-granted backing but cannot manufacture replay authority.
        if ( replayOwner == INVALID_RUNTIME_RESERVE_OWNER ||
             !RuntimeReserveAllocator::IsApprovedReplayGrowthAllocation( replayOwner, static_cast<int>( phase ) ) )
        {
            SB_FATAL( "Physics/SceneCapacity",
                      "Replay scene vector reserve lacks an approved outer owner: target=%s requested=%zu retained=%zu.",
                      ownerName ? ownerName : "Physics/Unknown", requestedCapacity, values.capacity() );
        }

        RuntimeReserveOwnerScope ownerScope( replayOwner );
        values.reserve( requestedCapacity );
        return;
    }

    if ( phase != RuntimeReservePhase::SceneLoad )
    {
        SB_FATAL( "Physics/SceneCapacity",
                  "Scene vector reserve denied outside scene load: owner=%s requested=%zu retained=%zu phase=%s.",
                  ownerName ? ownerName : "Physics/Unknown", requestedCapacity, values.capacity(),
                  RuntimeReservePhaseName( phase ) );
    }

    const RuntimeReserveOwnerHandle owner = RuntimeReserveAllocator::RegisterOwner( { ownerName, RuntimeReserveSubsystem::Physics, RuntimeReservePhase::SceneLoad, 0, static_cast<int>( hardCapacity ),
                                                                                      0, false, capacityReason } );
    RuntimeReserveGrowthResult
        growth = RuntimeReserveAllocator::RequestGrowth( owner, { ownerName, ownerName, phase, -1,
                                                                  static_cast<int>( values.capacity() ),
                                                                  static_cast<int>( requestedCapacity ),
                                                                  static_cast<int>( sizeof( T ) ) } );

    if ( !growth.granted )
    {
        SB_FATAL( "Physics/SceneCapacity",
                  "Scene vector reserve denied: owner=%s requested=%zu retained=%zu ceiling=%zu phase=%s.",
                  ownerName ? ownerName : "Physics/Unknown", requestedCapacity, values.capacity(), hardCapacity,
                  RuntimeReservePhaseName( phase ) );
    }

    RuntimeReserveOwnerScope ownerScope( owner );
    RuntimeReserveGrowthScope growthScope( owner, phase, growth );
    values.reserve( requestedCapacity );
}
} // namespace Physics
} // namespace SkullbonezCore
