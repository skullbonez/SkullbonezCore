/*
File: SkullbonezSource/Runtime/Prediction/ReplayPredictionReserve.h
Purpose:
  Names the replay prediction working-set reserve owner shared by prediction
  frames, future-node caches, the private prediction engine, and trajectory
  and solver-evidence storage, and centralizes its capacity/accounting
  operations.

Summary:
  Replay prediction is allowed to grow during replay exploration, but only
  through one registered RuntimeReserveAllocator owner. Helpers in this file
  keep that owner name, byte accounting, growth rounding, and hard byte cap
  consistent across vector, private-engine, trajectory, and segmented-evidence
  allocations.

Glossary:
  Runtime reserve owner: Registered allocation-policy row that can approve
    bounded replay-phase growth after steady gameplay has started.
  Replay prediction working set: The approved Prediction owner for
    future-frame samples, prediction scratch, and generated trajectory records;
    its growth remains gated to the Replay allocation phase.
  Batched frame payload: One reserve approval covering the same vector member
    across every pre-sized prediction frame.

Invariants:
  - Every runtime growth request for prediction or trajectory storage must use
    `REPLAY_PREDICTION_RESERVE_OWNER`.
  - The hard cap is byte-based and shared by all prediction working-set users.
  - Vector and frame-payload helpers preserve request bytes, phase, scopes, and
    growth-counter order for every caller.
  - Private Physics engine construction and storage seeding enter Replay
    allocation, the canonical prediction owner, and its growth scope only
    through this adapter.

Related:
  - SkullbonezSource/Runtime/Prediction/ReplayPrediction.cpp
  - SkullbonezSource/Runtime/Prediction/TrajectoryStore.h
  - tools/allocation_policy_allowlist.json
*/
#pragma once

#include "ReplayPredictionRetainedMemory.h"

#include <cstddef>
#include <cstdint>
#include <limits>
#include <memory>
#include <vector>

namespace SkullbonezCore::Core
{
struct MainMemoryReplayCategoryBytes;
}
namespace SkullbonezCore::Physics
{
class PhysicsEngine;
}
namespace SkullbonezCore::Runtime
{
struct ReplaySolverWorldSnapshot;
struct RunReplayPredictionFrame;

namespace ReplayPredictionReserveOperations
{
SkullbonezCore::Core::Allocation::RuntimeReserveOwnerHandle ReplayPredictionReserveOwner() noexcept;
bool RequestReplayPredictionReserveGrowth( const char* targetName, int frameNumber, int oldCapacityBytes, int requestedCapacityBytes, int elementSizeBytes,
                                           SkullbonezCore::Core::Allocation::RuntimeReserveGrowthResult& outResult ) noexcept;

template <typename T> bool ReplayPredictionCapacityBytes( std::size_t capacity, uint64_t& outBytes )
{
    constexpr uint64_t elementBytes = static_cast<uint64_t>( sizeof( T ) );
    const uint64_t maxCapacity = ( std::numeric_limits<uint64_t>::max )() / elementBytes;

    if ( capacity > maxCapacity )
    {
        return false;
    }

    outBytes = static_cast<uint64_t>( capacity ) * elementBytes;
    return true;
}

template <typename T> uint64_t ReplayPredictionVectorCapacityBytes( const std::vector<T>& values )
{
    uint64_t bytes = 0;
    return ReplayPredictionCapacityBytes<T>( values.capacity(), bytes ) ? bytes : 0;
}

uint64_t ReplayPredictionWorldSnapshotMemoryBytes( const ReplaySolverWorldSnapshot& snapshot );
void AddReplayPredictionFrameCategoryBytes( SkullbonezCore::Core::MainMemoryReplayCategoryBytes& categories,
                                            const RunReplayPredictionFrame& frame );

template <typename T>
bool ReplayPredictionFramePayloadBytes( std::size_t frameCount, std::size_t capacityPerFrame, uint64_t& outBytes )
{
    uint64_t bytesPerFrame = 0;

    if ( !ReplayPredictionCapacityBytes<T>( capacityPerFrame, bytesPerFrame ) )
    {
        return false;
    }

    const uint64_t maxValue = ( std::numeric_limits<uint64_t>::max )();
    const uint64_t maxFrameCount = bytesPerFrame > 0 ? maxValue / bytesPerFrame : maxValue;

    if ( frameCount > maxFrameCount )
    {
        return false;
    }

    outBytes = static_cast<uint64_t>( frameCount ) * bytesPerFrame;
    return true;
}

std::size_t RoundUpReplayPredictionCapacity( std::size_t requestedCapacity, std::size_t chunk );
std::size_t ReplayPredictionInitialDebugContactCapacity( int modelCount );
std::size_t ReplayPredictionNextDebugContactCapacity( std::size_t currentCapacity, std::size_t requiredCapacity );
uint64_t ReplayPredictionEngineMemoryBytes( const Physics::PhysicsEngine& engine );
int ReplayPredictionEngineReserveBytes( const Physics::PhysicsEngine& engine );

// Constructs and seeds one retained private Physics engine through the exact
// production reserve-owner adapter. The destination remains partial until its
// Runtime caller restores captured body and solver values.
bool SeedReplayPredictionEngineStorage( std::unique_ptr<Physics::PhysicsEngine>& destination,
                                        const Physics::PhysicsEngine& source, int currentReservedBytes,
                                        int& outReservedBytes );

// Invariant: the working-set owner is approved before allocation, then the
// allocation phase, owner, and granted-growth scopes are entered in that order.
// The request uses byte units (`elementSizeBytes == 1`) because the one 960 MiB
// cap is shared across vectors with different element types.
template <typename T>
bool ReserveReplayPredictionVector( std::vector<T>& values, std::size_t requestedCapacity, int frameNumber,
                                    const char* targetName )
{
    if ( requestedCapacity <= values.capacity() )
    {
        return true;
    }

    uint64_t oldBytes = 0;
    uint64_t requestedBytes = 0;

    if ( !ReplayPredictionCapacityBytes<T>( values.capacity(), oldBytes ) ||
         !ReplayPredictionCapacityBytes<T>( requestedCapacity, requestedBytes ) ||
         requestedBytes > static_cast<uint64_t>( REPLAY_PREDICTION_RESERVE_HARD_BYTES ) )
    {
        return false;
    }

    SkullbonezCore::Core::Allocation::RuntimeReserveGrowthResult result = {};

    if ( !RequestReplayPredictionReserveGrowth( targetName, frameNumber, static_cast<int>( oldBytes ),
                                                static_cast<int>( requestedBytes ), 1, result ) )
    {
        return false;
    }

    const SkullbonezCore::Core::Allocation::RuntimeReserveOwnerHandle owner = ReplayPredictionReserveOwner();
    SkullbonezCore::Core::Allocation::RuntimeAllocationScope replayAllocationScope( SkullbonezCore::Core::Allocation::RuntimeAllocationPhase::Replay );
    SkullbonezCore::Core::Allocation::RuntimeReserveOwnerScope ownerScope( owner );
    SkullbonezCore::Core::Allocation::RuntimeReserveGrowthScope
        growthScope( owner, SkullbonezCore::Core::Allocation::RuntimeReservePhase::Replay, result );
    values.reserve( requestedCapacity );
    return requestedCapacity <= values.capacity();
}

template <typename Frame, typename T>
bool ReserveReplayPredictionFramePayloadVectors( std::vector<Frame>& frames, std::size_t requestedFrameCount,
                                                 std::size_t requestedCapacityPerFrame, int frameNumber,
                                                 const char* targetName, std::vector<T> Frame::* member )
{
    // Runtime allocation policy: prediction captures many future frames. Batch
    // the per-frame payload reserves under one replay approval so validation
    // sees one setup event instead of one growth request per future frame.
    if ( requestedCapacityPerFrame == 0 )
    {
        return true;
    }

    uint64_t oldBytes = 0;

    for ( std::size_t i = 0; i < requestedFrameCount; ++i )
    {
        uint64_t frameBytes = 0;

        if ( !ReplayPredictionCapacityBytes<T>( ( frames[i].*member ).capacity(), frameBytes ) ||
             oldBytes > ( std::numeric_limits<uint64_t>::max )() - frameBytes )
        {
            return false;
        }

        oldBytes += frameBytes;
    }

    uint64_t requestedBytes = 0;

    if ( !ReplayPredictionFramePayloadBytes<T>( requestedFrameCount, requestedCapacityPerFrame, requestedBytes ) )
    {
        return false;
    }

    if ( requestedBytes <= oldBytes )
    {
        return true;
    }

    if ( requestedBytes > static_cast<uint64_t>( REPLAY_PREDICTION_RESERVE_HARD_BYTES ) )
    {
        return false;
    }

    SkullbonezCore::Core::Allocation::RuntimeReserveGrowthResult result = {};

    if ( !RequestReplayPredictionReserveGrowth( targetName, frameNumber, static_cast<int>( oldBytes ),
                                                static_cast<int>( requestedBytes ), 1, result ) )
    {
        return false;
    }

    const SkullbonezCore::Core::Allocation::RuntimeReserveOwnerHandle owner = ReplayPredictionReserveOwner();
    SkullbonezCore::Core::Allocation::RuntimeAllocationScope replayAllocationScope( SkullbonezCore::Core::Allocation::RuntimeAllocationPhase::Replay );
    SkullbonezCore::Core::Allocation::RuntimeReserveOwnerScope ownerScope( owner );
    SkullbonezCore::Core::Allocation::RuntimeReserveGrowthScope
        growthScope( owner, SkullbonezCore::Core::Allocation::RuntimeReservePhase::Replay, result );

    for ( std::size_t i = 0; i < requestedFrameCount; ++i )
    {
        ( frames[i].*member ).reserve( requestedCapacityPerFrame );
    }

    return true;
}
} // namespace ReplayPredictionReserveOperations
} // namespace SkullbonezCore::Runtime
