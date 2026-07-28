/*
File: SkullbonezSource/Runtime/Prediction/ReplayPredictionReserve.cpp
Purpose:
  Registers the replay prediction working-set owner and owns its capacity,
  accounting, and growth operations.

Summary:
  RuntimeReserveAllocator is the policy gate; this file is the replay-specific
  adapter that supplies the owner metadata, measures its Physics/frame storage,
  and rejects impossible byte counts before callers enter allocation scopes.

Glossary:
  Growth request: RuntimeReserveAllocator check that approves one capacity
    increase before the code performs the allocation under matching scopes.
  Hard cap: Maximum byte capacity the replay prediction working set may hold.
  Capacity accounting: Overflow-checked conversion of owned vector capacities
    into the byte categories and reserve requests used by diagnostics.

Invariants:
  - `RequestGrowth` is called only here for replay prediction working-set
    storage; caller files reserve vectors through this wrapper.
  - Debug-contact rounding and engine estimates have one implementation shared
    by orchestration, publication, and memory reporting.
  - Private Physics engine construction and storage seeding enter Replay
    allocation, the canonical prediction owner, and its growth scope only
    through this adapter.
  - A denied request is non-fatal to the process. Callers decide whether to keep
    partial UI data, retry later, or cancel the current prediction build.

Related:
  - SkullbonezSource/Core/Allocation/RuntimeReserveAllocator.h
  - SkullbonezSource/Runtime/Prediction/ReplayPredictionReserve.h
*/
#include "ReplayPredictionReserve.h"
#include "ReplayPrediction.h"

#include "../../Physics/ColliderStore.h"
#include "../../Physics/PhysicsBodyStore.h"
#include "../../Physics/PhysicsEngine.h"

#include <algorithm>
#include <limits>

namespace SkullbonezCore::Runtime
{
namespace
{

// Runtime allocation policy: prediction scratch can grow as the user explores
// larger retained paths. The registered hard cap is a real byte ceiling, not a
// theoretical element-count product; growth count is telemetry so interactive
// replay does not trip a per-run count fuse.
constexpr int
    REPLAY_PREDICTION_RESERVE_GROWTH_LIMIT = SkullbonezCore::Core::Allocation::RUNTIME_RESERVE_REPLAY_GROWTH_LIMIT_UNBOUNDED;
constexpr std::size_t REPLAY_PREDICTION_DEBUG_CONTACT_INITIAL_MIN = 512u;
constexpr std::size_t REPLAY_PREDICTION_DEBUG_CONTACT_INITIAL_MAX = 2048u;
constexpr std::size_t REPLAY_PREDICTION_DEBUG_CONTACT_GROWTH_CHUNK = 4096u;
} // namespace

namespace ReplayPredictionReserveOperations
{
SkullbonezCore::Core::Allocation::RuntimeReserveOwnerHandle ReplayPredictionReserveOwner() noexcept
{
    static const SkullbonezCore::Core::Allocation::RuntimeReserveOwnerHandle
        owner = SkullbonezCore::Core::Allocation::RuntimeReserveAllocator::RegisterOwner( { REPLAY_PREDICTION_RESERVE_OWNER, SkullbonezCore::Core::Allocation::RuntimeReserveSubsystem::Replay,
                                                                                            SkullbonezCore::Core::Allocation::RuntimeReservePhase::Replay, 0, REPLAY_PREDICTION_RESERVE_HARD_BYTES,
                                                                                            REPLAY_PREDICTION_RESERVE_GROWTH_LIMIT, true,
                                                                                            "replay prediction supports large retained path visualization under a hard byte budget" } );

    return owner;
}

bool RequestReplayPredictionReserveGrowth( const char* targetName, int frameNumber, int oldCapacityBytes,
                                           int requestedCapacityBytes, int elementSizeBytes,
                                           SkullbonezCore::Core::Allocation::RuntimeReserveGrowthResult& outResult ) noexcept
{
    outResult = {};

    if ( !targetName || oldCapacityBytes < 0 || requestedCapacityBytes <= oldCapacityBytes ||
         requestedCapacityBytes > REPLAY_PREDICTION_RESERVE_HARD_BYTES || elementSizeBytes <= 0 )
    {
        return false;
    }

    const SkullbonezCore::Core::Allocation::RuntimeReserveOwnerHandle owner = ReplayPredictionReserveOwner();
    const SkullbonezCore::Core::Allocation::RuntimeReserveGrowthRequest request = { REPLAY_PREDICTION_RESERVE_OWNER,
          targetName,
          SkullbonezCore::Core::Allocation::RuntimeReservePhase::Replay,
          frameNumber,
          oldCapacityBytes,
          requestedCapacityBytes,
          elementSizeBytes };

    outResult = SkullbonezCore::Core::Allocation::RuntimeReserveAllocator::RequestGrowth( owner, request );
    return outResult.granted;
}

uint64_t ReplayPredictionWorldSnapshotMemoryBytes( const ReplaySolverWorldSnapshot& snapshot )
{
    const SkullbonezCore::Physics::PhysicsSolverSnapshot& physics = snapshot.physics;
    uint64_t bytes = 0;
    bytes += ReplayPredictionVectorCapacityBytes( physics.timeRemaining );
    bytes += ReplayPredictionVectorCapacityBytes( physics.sleepSupportedThisFrame );
    bytes += ReplayPredictionVectorCapacityBytes( physics.sleepInhibitedThisFrame );
    bytes += ReplayPredictionVectorCapacityBytes( physics.sleepState );
    bytes += ReplayPredictionVectorCapacityBytes( physics.sleepCounter );
    bytes += ReplayPredictionVectorCapacityBytes( physics.underwaterSleepLocked );
    bytes += ReplayPredictionVectorCapacityBytes( snapshot.tornadoCaptureSeconds );
    bytes += ReplayPredictionVectorCapacityBytes( snapshot.tornadoEjectCooldownSeconds );
    bytes += ReplayPredictionVectorCapacityBytes( physics.collisionVisualContacts );
    bytes += ReplayPredictionVectorCapacityBytes( physics.sleepIslandVisualId );
    bytes += ReplayPredictionVectorCapacityBytes( physics.sleepIslandAssignedVisualId );
    bytes += ReplayPredictionVectorCapacityBytes( physics.sleepSupportEdges );
    bytes += ReplayPredictionVectorCapacityBytes( physics.sleepIslandParent );
    bytes += ReplayPredictionVectorCapacityBytes( physics.sleepIslandRank );
    bytes += ReplayPredictionVectorCapacityBytes( physics.sleepIslandHasAwake );
    bytes += ReplayPredictionVectorCapacityBytes( physics.sleepIslandHasSupportAnchor );
    bytes += ReplayPredictionVectorCapacityBytes( physics.sleepIslandEligible );
    bytes += ReplayPredictionVectorCapacityBytes( physics.sleepIslandCanSleep );
    bytes += ReplayPredictionVectorCapacityBytes( physics.persistentContacts );
    bytes += ReplayPredictionVectorCapacityBytes( physics.persistentContactCache );
    bytes += ReplayPredictionVectorCapacityBytes( physics.persistentContactCounts );
    bytes += ReplayPredictionVectorCapacityBytes( physics.persistentRestingContactCounts );
    bytes += ReplayPredictionVectorCapacityBytes( physics.debugContacts );
    bytes += ReplayPredictionVectorCapacityBytes( physics.pipelineTrace );
    bytes += ReplayPredictionVectorCapacityBytes( physics.collisionCellKeys );
    return bytes;
}

void AddReplayPredictionFrameCategoryBytes( SkullbonezCore::Core::MainMemoryReplayCategoryBytes& categories,
                                            const RunReplayPredictionFrame& frame )
{
    SkullbonezCore::Core::
        MainMemoryAddReplayCategoryBytes( categories,
                                          SkullbonezCore::Core::MainMemoryReplayByteCategory::PredictionFrameBodies,
                                          ReplayPredictionVectorCapacityBytes( frame.bodies ) );

    SkullbonezCore::Core::
        MainMemoryAddReplayCategoryBytes( categories,
                                          SkullbonezCore::Core::MainMemoryReplayByteCategory::PredictionDebugContacts,
                                          ReplayPredictionVectorCapacityBytes( frame.debugContacts ) );
}

std::size_t RoundUpReplayPredictionCapacity( std::size_t requestedCapacity, std::size_t chunk )
{

    if ( chunk == 0 || requestedCapacity == 0 )
    {
        return requestedCapacity;
    }

    const std::size_t remainder = requestedCapacity % chunk;
    return remainder == 0 ? requestedCapacity : requestedCapacity + ( chunk - remainder );
}

std::size_t ReplayPredictionInitialDebugContactCapacity( int modelCount )
{
    const std::size_t modelScaled = static_cast<std::size_t>( (std::max)( modelCount, 1 ) ) * 8u;
    return std::clamp( modelScaled, REPLAY_PREDICTION_DEBUG_CONTACT_INITIAL_MIN,
                       REPLAY_PREDICTION_DEBUG_CONTACT_INITIAL_MAX );
}

std::size_t ReplayPredictionNextDebugContactCapacity( std::size_t currentCapacity, std::size_t requiredCapacity )
{
    const std::size_t chunked = RoundUpReplayPredictionCapacity( requiredCapacity,
                                                                 REPLAY_PREDICTION_DEBUG_CONTACT_GROWTH_CHUNK );

    const std::size_t doubled = currentCapacity > 0 ? currentCapacity * 2u : REPLAY_PREDICTION_DEBUG_CONTACT_INITIAL_MIN;

    return (std::max)( chunked, doubled );
}

uint64_t ReplayPredictionEngineMemoryBytes( const Physics::PhysicsEngine& engine )
{

    // Why: seeding the private engine copies physics-owned vectors and
    // scene-sized fixed-list backing. Measure both before requesting the replay
    // growth scope so every copy allocation shares one bounded prediction owner.
    uint64_t bytes = static_cast<uint64_t>( sizeof( Physics::PhysicsEngine ) );
    bytes += engine.CollectPhysicsWorldMemoryBytes();
    bytes += engine.CollectDebugAndBroadphaseMemoryBytes();
    bytes += engine.CollectSceneSizedStoreMemoryBytes();
    return bytes;
}

int ReplayPredictionEngineReserveBytes( const Physics::PhysicsEngine& engine )
{
    const uint64_t bytes = ReplayPredictionEngineMemoryBytes( engine );

    if ( bytes == 0 || bytes > static_cast<uint64_t>( REPLAY_PREDICTION_RESERVE_HARD_BYTES ) ||
         bytes > static_cast<uint64_t>( ( std::numeric_limits<int>::max )() ) )
    {
        return 0;
    }

    return static_cast<int>( bytes );
}

bool SeedReplayPredictionEngineStorage( std::unique_ptr<Physics::PhysicsEngine>& destination,
                                        const Physics::PhysicsEngine& source, int currentReservedBytes,
                                        int& outReservedBytes )
{
    outReservedBytes = currentReservedBytes;
    const int requestedBytes = ReplayPredictionEngineReserveBytes( source );

    if ( currentReservedBytes < 0 || requestedBytes <= 0 )
    {
        return false;
    }

    SkullbonezCore::Core::Allocation::RuntimeReserveGrowthResult result = {};

    if ( requestedBytes > currentReservedBytes )
    {

        // Why: the private engine is retained across prediction rebuilds. Only
        // real capacity increases should consume replay growth events; same-size
        // reseeds just reuse the previous bounded reservation.

        if ( !RequestReplayPredictionReserveGrowth( "RunReplayPredictionSimulationState::predictionEngine", 0,
                                                    currentReservedBytes, requestedBytes, 1, result ) )
        {
            return false;
        }
    }

    const SkullbonezCore::Core::Allocation::RuntimeReserveOwnerHandle owner = ReplayPredictionReserveOwner();
    SkullbonezCore::Core::Allocation::RuntimeAllocationScope replayAllocationScope( SkullbonezCore::Core::Allocation::RuntimeAllocationPhase::Replay );
    SkullbonezCore::Core::Allocation::RuntimeReserveOwnerScope ownerScope( owner );
    SkullbonezCore::Core::Allocation::RuntimeReserveGrowthScope
        growthScope( owner, SkullbonezCore::Core::Allocation::RuntimeReservePhase::Replay, result );

    if ( !destination )
    {
        destination = std::make_unique<Physics::PhysicsEngine>();
    }

    destination->SeedReplayPredictionStorageFrom( source );
    outReservedBytes = (std::max)( currentReservedBytes, requestedBytes );
    return true;
}
} // namespace ReplayPredictionReserveOperations
} // namespace SkullbonezCore::Runtime
