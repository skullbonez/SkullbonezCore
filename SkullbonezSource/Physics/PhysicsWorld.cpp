/*
File: SkullbonezSource/Physics/PhysicsWorld.cpp
Purpose:
  Owns per-scene physics working state shared by broadphase, solver, and diagnostics.

Mental model:
  Physics is deterministic fixed-step state update. Units, contact ownership,
  solver stages, sleep policy, and baseline-sensitive behavior are the key
  reading anchors.

Glossary:
  SoA (Structure of Arrays): Data layout that stores each field in a separate
  contiguous array for cache-friendly iteration.
  CCD (Continuous Collision Detection): Swept collision test that asks whether
  objects hit during a tick, not only where they end the tick.
  Broadphase: Cheap collision pass that finds object pairs worth testing more
  precisely.
  Narrowphase: Precise collision pass that computes contact points, normals,
  and penetration.
  Manifold: Set of contact points and normals describing one colliding pair.
  Point joint: Constraint that keeps two local anchor points close together
    without yet modelling a full hinge, cone, or motor.
  Sleep island: Connected body group that may deactivate only as a unit.
  Underwater sleep lock: Sleep policy that keeps fully submerged balls dormant
    so buoyancy jitter does not repeatedly wake them.
  PhysicsScene: Step owner that supplies stores and handles model-order
    writeback after compact physics work finishes.
  Lane F: Fatal invariant lane for should-never-happen engine state.

Invariants:
  - Physics-visible behavior must remain deterministic; byte-exact baselines
    are the validation contract.
  - Fixed-capacity physics scratch buffers must not grow during gameplay; an
    exhausted reserve is a Lane F failure because continuing would either
    allocate on a hot path or silently drop deterministic side effects.

Related:
  - SkullbonezSource/Physics/PhysicsWorld.h
  - Agentic/Reference/physics-overview.md
  - Agentic/Reference/comment-style-guide.md
*/
#include "PhysicsWorld.h"

#include "../Core/Config.h"
#include "../Core/FatalError.h"
#include "DisjointSet.h"
#include "PhysicsApi.h"
#include "PhysicsBodyStore.h"
#include "PhysicsWorldForces.h"
#include "ColliderStore.h"
#include "ObjectContactManifold.h"
#include "TerrainContactManifold.h"
#include "../Core/Profiler.h"
#include "../Core/WorkerPool.h"
#include "../Runtime/Allocation/RuntimeAllocationTracker.h"
#include "../Runtime/Allocation/RuntimeReserveAllocator.h"

#include <algorithm>
#include <cassert>
#include <climits>
#include <cmath>
#include <cstdlib>
#include <cstdio>
#include <cstring>
#include <variant>

using namespace SkullbonezCore::Physics;
using SkullbonezCore::Basics::ReplaySolverContactCacheSample;
using SkullbonezCore::Basics::ReplaySolverPersistentContactSample;
using SkullbonezCore::Basics::ReplaySolverStatsSample;
using SkullbonezCore::Basics::ReplaySolverWorldSnapshot;
using SkullbonezCore::Math::Vector::Vector3;
using SkullbonezCore::Math::Vector::ZERO_VECTOR;
namespace Math = SkullbonezCore::Math;
namespace Physics = SkullbonezCore::Physics;
namespace Vector = SkullbonezCore::Math::Vector;
namespace RuntimeAllocation = SkullbonezCore::Runtime::Allocation;

namespace
{
constexpr size_t MAX_PIPELINE_TRACE_RECORDS = 4096;
constexpr int TERRAIN_BODY_INDEX = -1;
constexpr float TORNADO_EJECTION_PHASE_HZ = 10.0f;
constexpr float UNDERWATER_SLEEP_LOCK_SUBMERGED_PERCENT = 0.999f;
constexpr float EXPLICIT_WAKE_NEIGHBOR_SLOP = 0.50f;
constexpr float EXPLICIT_WAKE_VERTICAL_SLOP = 0.25f;
// Why: worker fan-out is more expensive than the work for the validation-sized
// 300-body scenes. Keep all-body jobs inline until there is enough work per
// chunk for the persistent worker pool to pay for itself.
constexpr int PHYSICS_PARALLEL_MIN_BODIES = 512;
constexpr int PHYSICS_NARROWPHASE_PARALLEL_MIN_PAIRS = 256;
constexpr int PHYSICS_NARROWPHASE_PARALLEL_MIN_ISLANDS = 16;
constexpr int PHYSICS_NARROWPHASE_PARALLEL_MAX_AVG_PAIRS_PER_ISLAND = 4;
constexpr int PHYSICS_NARROWPHASE_PARALLEL_MAX_PAIRS_PER_BODY = 2;
constexpr bool PHYSICS_NARROWPHASE_ISLAND_WORKER_ENABLED = true;
constexpr float PHYSICS_OBJECT_CCD_RADIUS_FRACTION = 0.25f;
constexpr float PHYSICS_OBJECT_CCD_SKIN_SCALE = 4.0f;
constexpr float PHYSICS_FAST_SWEEP_MAX_RADIUS = 1.0f;
constexpr float PHYSICS_FAST_SWEEP_MIN_DISTANCE = 1.0f;
constexpr float PHYSICS_FAST_SWEEP_PAIR_SLOP = 1.0f;
constexpr float POINT_JOINT_SLEEP_MIN_ERROR_TOLERANCE = 0.15f;
constexpr float POINT_JOINT_SLEEP_SLACK_TOLERANCE_SCALE = 0.75f;
constexpr float POINT_JOINT_SLEEP_LINEAR_SPEED_SCALE = 6.0f;
constexpr float POINT_JOINT_SLEEP_ANGULAR_SPEED_SCALE = 6.0f;
constexpr float BROADPHASE_MIN_CELL_SIZE = 0.5f;
constexpr float DEFAULT_BROADPHASE_CELL = 24.0f;
constexpr uint8_t DEFAULT_PHYSICS_SLEEP_FRAMES = 30;
constexpr int PHYSICS_CANDIDATE_PAIR_RESERVE = MAX_GAME_MODELS * 4;
constexpr int PHYSICS_COLLISION_VISUAL_BODY_RESERVE = PHYSICS_CANDIDATE_PAIR_RESERVE * 2;
constexpr const char* REPLAY_SOLVER_SNAPSHOT_RESERVE_OWNER = "replay_solver_snapshot";
constexpr int REPLAY_SOLVER_SNAPSHOT_RESERVE_HARD_BYTES = 64 * 1024 * 1024;
constexpr std::size_t REPLAY_SOLVER_SNAPSHOT_VECTOR_INITIAL_CAPACITY = 1024u;
constexpr std::size_t REPLAY_SOLVER_SNAPSHOT_VECTOR_GROWTH_CHUNK = 4096u;
// Runtime allocation policy: replay prediction visualization can discover
// larger solver snapshots interactively. The hard byte cap is the memory bound;
// growth count remains diagnostic instead of being a fatal budget.
constexpr int REPLAY_SOLVER_SNAPSHOT_RESERVE_GROWTH_LIMIT =
    RuntimeAllocation::RUNTIME_RESERVE_REPLAY_GROWTH_LIMIT_UNBOUNDED;
constexpr uint32_t PHYSICS_TORNADO_WORKER_HASH = HashStr( "Frame/Physics/TornadoField/WorkerBodies" );
constexpr uint32_t PHYSICS_APPLY_FORCES_WORKER_HASH = HashStr( "Frame/Physics/ApplyForces/WorkerBodies" );
constexpr uint32_t PHYSICS_NARROWPHASE_ISLAND_WORKER_HASH =
    HashStr( "Frame/Physics/Narrowphase/IslandWorkerDispatch/WorkerIslands" );
constexpr uint32_t PHYSICS_TERRAIN_DETECT_WORKER_HASH = HashStr( "Frame/Physics/Terrain/Detect/WorkerBodies" );
constexpr uint32_t PHYSICS_INTEGRATE_WORKER_HASH = HashStr( "Frame/Physics/Integrate/WorkerBodies" );

template <typename T> uint64_t VectorCapacityBytes( const std::vector<T>& values )
{
    return static_cast<uint64_t>( values.capacity() ) * static_cast<uint64_t>( sizeof( T ) );
}

RuntimeAllocation::RuntimeReserveOwnerHandle ReplaySolverSnapshotReserveOwner()
{
    static const RuntimeAllocation::RuntimeReserveOwnerHandle owner =
        RuntimeAllocation::RuntimeReserveAllocator::RegisterOwner(
            { REPLAY_SOLVER_SNAPSHOT_RESERVE_OWNER,
              RuntimeAllocation::RuntimeReserveSubsystem::Replay,
              RuntimeAllocation::RuntimeReservePhase::Replay,
              0,
              REPLAY_SOLVER_SNAPSHOT_RESERVE_HARD_BYTES,
              REPLAY_SOLVER_SNAPSHOT_RESERVE_GROWTH_LIMIT,
              true,
              "solver replay snapshots reserve vector payload bytes through replay-only growth approval" } );
    return owner;
}

void ReportReplaySolverSnapshotReserveFailure( const char* label, std::size_t requestedCapacity )
{
    std::fprintf( stderr,
                  "FATAL: Replay solver snapshot reserve denied for %s (requested_capacity=%zu).\n",
                  label ? label : "unknown",
                  requestedCapacity );
    std::fprintf( stdout,
                  "FATAL: Replay solver snapshot reserve denied for %s (requested_capacity=%zu).\n",
                  label ? label : "unknown",
                  requestedCapacity );
    std::fflush( stderr );
    std::fflush( stdout );
    assert( false && "Replay solver snapshot reserve denied." );
    std::abort();
}

template <typename T>
std::size_t ReplaySolverSnapshotReserveCapacity( const std::vector<T>& values, std::size_t requestedCapacity )
{
    // Why: replay snapshots are diagnostics payloads, not steady gameplay
    // storage. Chunking capacity here keeps prediction exploration from logging
    // a chain of tiny reserve events as contact caches discover denser frames.
    if ( requestedCapacity <= values.capacity() )
    {
        return values.capacity();
    }
    if ( requestedCapacity > static_cast<std::size_t>( PHYSICS_COLLISION_VISUAL_BODY_RESERVE ) )
    {
        return requestedCapacity;
    }

    const std::size_t doubled =
        values.capacity() > 0 ? values.capacity() * 2u : REPLAY_SOLVER_SNAPSHOT_VECTOR_INITIAL_CAPACITY;
    const std::size_t remainder = requestedCapacity % REPLAY_SOLVER_SNAPSHOT_VECTOR_GROWTH_CHUNK;
    const std::size_t chunked = remainder == 0
                                    ? requestedCapacity
                                    : requestedCapacity + ( REPLAY_SOLVER_SNAPSHOT_VECTOR_GROWTH_CHUNK - remainder );
    const std::size_t reserveCapacity = (std::max)( doubled, chunked );
    return (std::min)( reserveCapacity, static_cast<std::size_t>( PHYSICS_COLLISION_VISUAL_BODY_RESERVE ) );
}

template <typename T>
uint64_t ReplaySolverSnapshotRequestedBytes( const std::vector<T>& values, std::size_t requestedCapacity )
{
    const std::size_t capacity = ReplaySolverSnapshotReserveCapacity( values, requestedCapacity );
    return static_cast<uint64_t>( capacity ) * static_cast<uint64_t>( sizeof( T ) );
}

template <typename T>
void ReserveReplaySolverSnapshotVector( std::vector<T>& values, std::size_t requestedCapacity, const char* label )
{
    if ( requestedCapacity <= values.capacity() )
    {
        return;
    }
    if ( requestedCapacity > static_cast<std::size_t>( PHYSICS_COLLISION_VISUAL_BODY_RESERVE ) )
    {
        ReportReplaySolverSnapshotReserveFailure( label, requestedCapacity );
    }
    const std::size_t reserveCapacity = ReplaySolverSnapshotReserveCapacity( values, requestedCapacity );
    values.reserve( reserveCapacity );
    if ( requestedCapacity > values.capacity() )
    {
        ReportReplaySolverSnapshotReserveFailure( label, requestedCapacity );
    }
}

Vector3 ClampVectorMagnitude( const Vector3& value, float maxMagnitude )
{
    if ( maxMagnitude <= TOLERANCE )
    {
        return ZERO_VECTOR;
    }

    const float magSq = value * value;
    const float maxSq = maxMagnitude * maxMagnitude;
    if ( magSq <= maxSq || magSq <= TOLERANCE * TOLERANCE )
    {
        return value;
    }

    return value * ( maxMagnitude / sqrtf( magSq ) );
}
} // namespace


PhysicsWorld::PhysicsWorld()
    : m_spatialGrid( DEFAULT_BROADPHASE_CELL ), m_seedSleepFrameCount( DEFAULT_PHYSICS_SLEEP_FRAMES )
{
    m_timeRemaining.reserve( MAX_GAME_MODELS );
    // Runtime allocation policy: broadphase candidate-pair storage is sized at
    // setup and SpatialGrid asserts on cap exhaustion instead of growing during
    // the fixed-step physics pass.
    m_candidatePairs.reserve( PHYSICS_CANDIDATE_PAIR_RESERVE );
    m_sleepSupportedThisFrame.reserve( MAX_GAME_MODELS );
    m_sleepInhibitedThisFrame.reserve( MAX_GAME_MODELS );
    m_sleepState.reserve( MAX_GAME_MODELS );
    m_sleepCounter.reserve( MAX_GAME_MODELS );
    m_underwaterSleepLocked.reserve( MAX_GAME_MODELS );
    m_tornadoCaptureSeconds.reserve( MAX_GAME_MODELS );
    m_tornadoEjectCooldownSeconds.reserve( MAX_GAME_MODELS );
    m_collisionVisualContacts.reserve( MAX_GAME_MODELS );
    m_sleepIslandVisualId.reserve( MAX_GAME_MODELS );
    m_sleepIslandAssignedVisualId.reserve( MAX_GAME_MODELS );
    m_sleepSupportEdges.reserve( MAX_GAME_MODELS * 4 );
    m_sleepIslandParent.reserve( MAX_GAME_MODELS );
    m_sleepIslandRank.reserve( MAX_GAME_MODELS );
    m_sleepIslandHasAwake.reserve( MAX_GAME_MODELS );
    m_sleepIslandHasSupportAnchor.reserve( MAX_GAME_MODELS );
    m_sleepIslandEligible.reserve( MAX_GAME_MODELS );
    m_sleepIslandCanSleep.reserve( MAX_GAME_MODELS );
    m_sleepPointJointBody.reserve( MAX_GAME_MODELS );
    m_sleepIslandHasPointJoint.reserve( MAX_GAME_MODELS );
    m_sleepIslandPointJointsRelaxed.reserve( MAX_GAME_MODELS );
    m_sleepVisualIslandIds.reserve( MAX_GAME_MODELS );
    m_sleepVisualIslandBodies.reserve( MAX_GAME_MODELS );
    m_tornadoFixedTreeReleaseWakeBodies.reserve( MAX_GAME_MODELS );
    m_persistentContacts.reserve( MAX_GAME_MODELS * 4 );
    m_persistentContactCache.reserve( MAX_GAME_MODELS * 4 );
    m_persistentContactCounts.reserve( MAX_GAME_MODELS );
    m_persistentRestingContactCounts.reserve( MAX_GAME_MODELS );
    m_solverBodies.reserve( MAX_GAME_MODELS );
    m_physicsDebugContacts.reserve( MAX_GAME_MODELS * 4 );
    m_physicsPipelineTrace.reserve( MAX_PIPELINE_TRACE_RECORDS );
    m_persistentContactSideEffects.pipelineRecords.reserve( MAX_PIPELINE_TRACE_RECORDS );
    m_persistentContactSideEffects.collisionVisualBodies.reserve( PHYSICS_COLLISION_VISUAL_BODY_RESERVE );
    m_persistentContactSideEffects.fixedContactBodies.reserve( MAX_GAME_MODELS );
    m_persistentContactSideEffects.releaseWakeBodies.reserve( 8 );
    m_persistentContactSideEffects.fixedTreeReleases.reserve( 8 );
    m_terrainContactManifolds.reserve( MAX_GAME_MODELS );
    m_terrainDetectionCandidates.reserve( MAX_GAME_MODELS );
    m_objectNarrowphaseEvents.reserve( MAX_GAME_MODELS * 4 );
    m_objectNarrowphaseIslands.reserve( MAX_GAME_MODELS );
    m_objectNarrowphaseIslandPairIndices.reserve( PHYSICS_CANDIDATE_PAIR_RESERVE );
    m_objectNarrowphaseIslandWriteOffsets.reserve( MAX_GAME_MODELS );
    m_objectNarrowphaseParent.reserve( MAX_GAME_MODELS );
    m_objectNarrowphaseRank.reserve( MAX_GAME_MODELS );
    m_objectNarrowphaseRootToIsland.reserve( MAX_GAME_MODELS );
    m_restingWakeVisitedScratch.reserve( MAX_GAME_MODELS );
    m_restingWakeQueueScratch.reserve( MAX_GAME_MODELS );
    m_pointJointConstraints.reserve( MAX_GAME_MODELS );
    m_collisionCellKeys.reserve( PHYSICS_CANDIDATE_PAIR_RESERVE );
}


void PhysicsWorld::ApplyRuntimeConfig( const Basics::EngineConfig& config )
{
    const float configuredCell = (std::max)( BROADPHASE_MIN_CELL_SIZE, config.broadphaseCell );
    m_spatialGrid.SetCellSize( configuredCell );
    m_seedSleepFrameCount = static_cast<uint8_t>( (std::max)( 0, (std::min)( config.physicsSleepFrames, 255 ) ) );
}


void PhysicsWorld::Clear()
{
    m_timeRemaining.clear();
    m_candidatePairs.clear();
    m_sleepSupportedThisFrame.clear();
    m_sleepInhibitedThisFrame.clear();
    m_sleepState.clear();
    m_sleepCounter.clear();
    m_underwaterSleepLocked.clear();
    m_tornadoCaptureSeconds.clear();
    m_tornadoEjectCooldownSeconds.clear();
    m_tornadoFixedTreeReleaseWakeBodies.clear();
    m_tornadoSystem.SetConfig( TornadoSystemConfig() );
    m_tornadoSystem.ResetElapsedSeconds();
    m_collisionVisualContacts.clear();
    m_sleepIslandVisualId.clear();
    m_sleepIslandAssignedVisualId.clear();
    m_nextSleepIslandVisualId = 1;
    m_collisionVisualFrameActive = false;
    m_sleepSupportEdges.clear();
    m_sleepIslandParent.clear();
    m_sleepIslandRank.clear();
    m_sleepIslandHasAwake.clear();
    m_sleepIslandHasSupportAnchor.clear();
    m_sleepIslandEligible.clear();
    m_sleepIslandCanSleep.clear();
    m_sleepPointJointBody.clear();
    m_sleepIslandHasPointJoint.clear();
    m_sleepIslandPointJointsRelaxed.clear();
    m_sleepVisualIslandIds.clear();
    m_sleepVisualIslandBodies.clear();
    m_persistentContacts.clear();
    m_persistentContactCache.clear();
    m_persistentContactSolverStats = PersistentContactSolverStats();
    m_persistentContactCounts.clear();
    m_persistentRestingContactCounts.clear();
    m_solverBodies.clear();
    m_physicsDebugContacts.clear();
    m_physicsPipelineTrace.clear();
    m_terrainContactManifolds.clear();
    m_terrainDetectionCandidates.clear();
    m_objectNarrowphaseEvents.clear();
    m_objectNarrowphaseIslands.clear();
    m_objectNarrowphaseIslandPairIndices.clear();
    m_objectNarrowphaseIslandWriteOffsets.clear();
    m_objectNarrowphaseParent.clear();
    m_objectNarrowphaseRank.clear();
    m_objectNarrowphaseRootToIsland.clear();
    m_pointJointConstraints.clear();
    m_collisionCellKeys.clear();
}


void PhysicsWorld::CaptureReplaySolverSnapshot( ReplaySolverWorldSnapshot& outSnapshot, int modelCount ) const
{
    // Runtime allocation policy: replay recorder slots pre-reserve these
    // payload vectors outside gameplay. Capture clears the retained slot in
    // place so solver replay does not discard capacity and reallocate per tick.
    outSnapshot.timeRemaining.clear();
    outSnapshot.sleepSupportedThisFrame.clear();
    outSnapshot.sleepInhibitedThisFrame.clear();
    outSnapshot.sleepState.clear();
    outSnapshot.sleepCounter.clear();
    outSnapshot.underwaterSleepLocked.clear();
    outSnapshot.tornadoCaptureSeconds.clear();
    outSnapshot.tornadoEjectCooldownSeconds.clear();
    outSnapshot.collisionVisualContacts.clear();
    outSnapshot.sleepIslandVisualId.clear();
    outSnapshot.sleepIslandAssignedVisualId.clear();
    outSnapshot.sleepSupportEdges.clear();
    outSnapshot.sleepIslandParent.clear();
    outSnapshot.sleepIslandRank.clear();
    outSnapshot.sleepIslandHasAwake.clear();
    outSnapshot.sleepIslandHasSupportAnchor.clear();
    outSnapshot.sleepIslandEligible.clear();
    outSnapshot.sleepIslandCanSleep.clear();
    outSnapshot.persistentContacts.clear();
    outSnapshot.persistentContactCache.clear();
    outSnapshot.persistentContactCounts.clear();
    outSnapshot.persistentRestingContactCounts.clear();
    outSnapshot.debugContacts.clear();
    outSnapshot.pipelineTrace.clear();
    outSnapshot.collisionCellKeys.clear();
    outSnapshot.solverStats = ReplaySolverStatsSample();

    outSnapshot.version = 2;
    outSnapshot.modelCount = modelCount;
    outSnapshot.nextSleepIslandVisualId = m_nextSleepIslandVisualId;
    outSnapshot.sleepEnabled = m_sleepEnabled;
    outSnapshot.collisionVisualFrameActive = m_collisionVisualFrameActive;
    outSnapshot.tornadoConfig = m_tornadoField.GetConfig();
    outSnapshot.tornadoSystemConfig = m_tornadoSystem.GetConfig();
    outSnapshot.tornadoSystemElapsedSeconds = m_tornadoSystem.GetElapsedSeconds();
    // Runtime allocation policy: a solver snapshot owns many typed vectors.
    // Batch their byte budget into one replay approval, then reserve individual
    // vectors inside that owner scope so replay diagnostics stay readable.
    uint64_t oldSnapshotBytes = 0;
    uint64_t requestedSnapshotBytes = 0;
    bool snapshotNeedsGrowth = false;
    const auto includeSnapshotReserve = [&]( const auto& values, std::size_t requestedCapacity )
    {
        oldSnapshotBytes += VectorCapacityBytes( values );
        requestedSnapshotBytes += ReplaySolverSnapshotRequestedBytes( values, requestedCapacity );
        snapshotNeedsGrowth = snapshotNeedsGrowth || requestedCapacity > values.capacity();
    };
    includeSnapshotReserve( outSnapshot.timeRemaining, m_timeRemaining.size() );
    includeSnapshotReserve( outSnapshot.sleepSupportedThisFrame, m_sleepSupportedThisFrame.size() );
    includeSnapshotReserve( outSnapshot.sleepInhibitedThisFrame, m_sleepInhibitedThisFrame.size() );
    includeSnapshotReserve( outSnapshot.sleepState, m_sleepState.size() );
    includeSnapshotReserve( outSnapshot.sleepCounter, m_sleepCounter.size() );
    includeSnapshotReserve( outSnapshot.underwaterSleepLocked, m_underwaterSleepLocked.size() );
    includeSnapshotReserve( outSnapshot.tornadoCaptureSeconds, m_tornadoCaptureSeconds.size() );
    includeSnapshotReserve( outSnapshot.tornadoEjectCooldownSeconds, m_tornadoEjectCooldownSeconds.size() );
    includeSnapshotReserve( outSnapshot.collisionVisualContacts, m_collisionVisualContacts.size() );
    includeSnapshotReserve( outSnapshot.sleepIslandVisualId, m_sleepIslandVisualId.size() );
    includeSnapshotReserve( outSnapshot.sleepIslandAssignedVisualId, m_sleepIslandAssignedVisualId.size() );
    includeSnapshotReserve( outSnapshot.sleepSupportEdges, m_sleepSupportEdges.size() );
    includeSnapshotReserve( outSnapshot.sleepIslandParent, m_sleepIslandParent.size() );
    includeSnapshotReserve( outSnapshot.sleepIslandRank, m_sleepIslandRank.size() );
    includeSnapshotReserve( outSnapshot.sleepIslandHasAwake, m_sleepIslandHasAwake.size() );
    includeSnapshotReserve( outSnapshot.sleepIslandHasSupportAnchor, m_sleepIslandHasSupportAnchor.size() );
    includeSnapshotReserve( outSnapshot.sleepIslandEligible, m_sleepIslandEligible.size() );
    includeSnapshotReserve( outSnapshot.sleepIslandCanSleep, m_sleepIslandCanSleep.size() );
    includeSnapshotReserve( outSnapshot.persistentContactCounts, m_persistentContactCounts.size() );
    includeSnapshotReserve( outSnapshot.persistentRestingContactCounts, m_persistentRestingContactCounts.size() );
    includeSnapshotReserve( outSnapshot.debugContacts, m_physicsDebugContacts.size() );
    includeSnapshotReserve( outSnapshot.pipelineTrace, m_physicsPipelineTrace.size() );
    includeSnapshotReserve( outSnapshot.collisionCellKeys, m_collisionCellKeys.size() );
    includeSnapshotReserve( outSnapshot.persistentContacts, m_persistentContacts.size() );
    includeSnapshotReserve( outSnapshot.persistentContactCache, m_persistentContactCache.size() );

    const auto reserveSnapshotVectors = [&]()
    {
        ReserveReplaySolverSnapshotVector( outSnapshot.timeRemaining, m_timeRemaining.size(), "timeRemaining" );
        ReserveReplaySolverSnapshotVector( outSnapshot.sleepSupportedThisFrame,
                                           m_sleepSupportedThisFrame.size(),
                                           "sleepSupportedThisFrame" );
        ReserveReplaySolverSnapshotVector( outSnapshot.sleepInhibitedThisFrame,
                                           m_sleepInhibitedThisFrame.size(),
                                           "sleepInhibitedThisFrame" );
        ReserveReplaySolverSnapshotVector( outSnapshot.sleepState, m_sleepState.size(), "sleepState" );
        ReserveReplaySolverSnapshotVector( outSnapshot.sleepCounter, m_sleepCounter.size(), "sleepCounter" );
        ReserveReplaySolverSnapshotVector( outSnapshot.underwaterSleepLocked,
                                           m_underwaterSleepLocked.size(),
                                           "underwaterSleepLocked" );
        ReserveReplaySolverSnapshotVector( outSnapshot.tornadoCaptureSeconds,
                                           m_tornadoCaptureSeconds.size(),
                                           "tornadoCaptureSeconds" );
        ReserveReplaySolverSnapshotVector( outSnapshot.tornadoEjectCooldownSeconds,
                                           m_tornadoEjectCooldownSeconds.size(),
                                           "tornadoEjectCooldownSeconds" );
        ReserveReplaySolverSnapshotVector( outSnapshot.collisionVisualContacts,
                                           m_collisionVisualContacts.size(),
                                           "collisionVisualContacts" );
        ReserveReplaySolverSnapshotVector( outSnapshot.sleepIslandVisualId,
                                           m_sleepIslandVisualId.size(),
                                           "sleepIslandVisualId" );
        ReserveReplaySolverSnapshotVector( outSnapshot.sleepIslandAssignedVisualId,
                                           m_sleepIslandAssignedVisualId.size(),
                                           "sleepIslandAssignedVisualId" );
        ReserveReplaySolverSnapshotVector( outSnapshot.sleepSupportEdges,
                                           m_sleepSupportEdges.size(),
                                           "sleepSupportEdges" );
        ReserveReplaySolverSnapshotVector( outSnapshot.sleepIslandParent,
                                           m_sleepIslandParent.size(),
                                           "sleepIslandParent" );
        ReserveReplaySolverSnapshotVector( outSnapshot.sleepIslandRank, m_sleepIslandRank.size(), "sleepIslandRank" );
        ReserveReplaySolverSnapshotVector( outSnapshot.sleepIslandHasAwake,
                                           m_sleepIslandHasAwake.size(),
                                           "sleepIslandHasAwake" );
        ReserveReplaySolverSnapshotVector( outSnapshot.sleepIslandHasSupportAnchor,
                                           m_sleepIslandHasSupportAnchor.size(),
                                           "sleepIslandHasSupportAnchor" );
        ReserveReplaySolverSnapshotVector( outSnapshot.sleepIslandEligible,
                                           m_sleepIslandEligible.size(),
                                           "sleepIslandEligible" );
        ReserveReplaySolverSnapshotVector( outSnapshot.sleepIslandCanSleep,
                                           m_sleepIslandCanSleep.size(),
                                           "sleepIslandCanSleep" );
        ReserveReplaySolverSnapshotVector( outSnapshot.persistentContactCounts,
                                           m_persistentContactCounts.size(),
                                           "persistentContactCounts" );
        ReserveReplaySolverSnapshotVector( outSnapshot.persistentRestingContactCounts,
                                           m_persistentRestingContactCounts.size(),
                                           "persistentRestingContactCounts" );
        ReserveReplaySolverSnapshotVector( outSnapshot.debugContacts, m_physicsDebugContacts.size(), "debugContacts" );
        ReserveReplaySolverSnapshotVector( outSnapshot.pipelineTrace, m_physicsPipelineTrace.size(), "pipelineTrace" );
        ReserveReplaySolverSnapshotVector( outSnapshot.collisionCellKeys,
                                           m_collisionCellKeys.size(),
                                           "collisionCellKeys" );
        ReserveReplaySolverSnapshotVector( outSnapshot.persistentContacts,
                                           m_persistentContacts.size(),
                                           "persistentContacts" );
        ReserveReplaySolverSnapshotVector( outSnapshot.persistentContactCache,
                                           m_persistentContactCache.size(),
                                           "persistentContactCache" );
    };
    if ( snapshotNeedsGrowth )
    {
        if ( requestedSnapshotBytes > static_cast<uint64_t>( REPLAY_SOLVER_SNAPSHOT_RESERVE_HARD_BYTES ) )
        {
            ReportReplaySolverSnapshotReserveFailure( "solverSnapshotBytes",
                                                      static_cast<std::size_t>( requestedSnapshotBytes ) );
        }
        const RuntimeAllocation::RuntimeReserveOwnerHandle owner = ReplaySolverSnapshotReserveOwner();
        const RuntimeAllocation::RuntimeReserveGrowthRequest request = { REPLAY_SOLVER_SNAPSHOT_RESERVE_OWNER,
                                                                         "ReplaySolverWorldSnapshot",
                                                                         RuntimeAllocation::RuntimeReservePhase::Replay,
                                                                         modelCount,
                                                                         static_cast<int>( oldSnapshotBytes ),
                                                                         static_cast<int>( requestedSnapshotBytes ),
                                                                         1 };
        const RuntimeAllocation::RuntimeReserveGrowthResult result =
            RuntimeAllocation::RuntimeReserveAllocator::RequestGrowth( owner, request );
        if ( !result.granted )
        {
            ReportReplaySolverSnapshotReserveFailure( "solverSnapshotBytes",
                                                      static_cast<std::size_t>( requestedSnapshotBytes ) );
        }
        RuntimeAllocation::RuntimeAllocationScope replayAllocationScope(
            RuntimeAllocation::RuntimeAllocationPhase::Replay );
        RuntimeAllocation::RuntimeReserveOwnerScope ownerScope( owner );
        RuntimeAllocation::RuntimeReserveGrowthScope growthScope( owner,
                                                                  RuntimeAllocation::RuntimeReservePhase::Replay,
                                                                  result );
        reserveSnapshotVectors();
    }
    else
    {
        reserveSnapshotVectors();
    }
    outSnapshot.timeRemaining = m_timeRemaining;
    outSnapshot.sleepSupportedThisFrame = m_sleepSupportedThisFrame;
    outSnapshot.sleepInhibitedThisFrame = m_sleepInhibitedThisFrame;
    outSnapshot.sleepState = m_sleepState;
    outSnapshot.sleepCounter = m_sleepCounter;
    outSnapshot.underwaterSleepLocked = m_underwaterSleepLocked;
    outSnapshot.tornadoCaptureSeconds = m_tornadoCaptureSeconds;
    outSnapshot.tornadoEjectCooldownSeconds = m_tornadoEjectCooldownSeconds;
    outSnapshot.collisionVisualContacts = m_collisionVisualContacts;
    outSnapshot.sleepIslandVisualId = m_sleepIslandVisualId;
    outSnapshot.sleepIslandAssignedVisualId = m_sleepIslandAssignedVisualId;
    outSnapshot.sleepSupportEdges = m_sleepSupportEdges;
    outSnapshot.sleepIslandParent = m_sleepIslandParent;
    outSnapshot.sleepIslandRank = m_sleepIslandRank;
    outSnapshot.sleepIslandHasAwake = m_sleepIslandHasAwake;
    outSnapshot.sleepIslandHasSupportAnchor = m_sleepIslandHasSupportAnchor;
    outSnapshot.sleepIslandEligible = m_sleepIslandEligible;
    outSnapshot.sleepIslandCanSleep = m_sleepIslandCanSleep;
    outSnapshot.persistentContactCounts = m_persistentContactCounts;
    outSnapshot.persistentRestingContactCounts = m_persistentRestingContactCounts;
    outSnapshot.debugContacts = m_physicsDebugContacts;
    outSnapshot.pipelineTrace = m_physicsPipelineTrace;
    outSnapshot.collisionCellKeys = m_collisionCellKeys;

    for ( const PersistentContact& contact : m_persistentContacts )
    {
        ReplaySolverPersistentContactSample sample;
        sample.bodyA = contact.bodyA;
        sample.bodyB = contact.bodyB;
        sample.featureId = contact.featureId;
        sample.key = contact.key;
        sample.normal = contact.normal;
        sample.tangent1 = contact.tangent1;
        sample.tangent2 = contact.tangent2;
        sample.rA = contact.rA;
        sample.rB = contact.rB;
        sample.penetration = contact.penetration;
        sample.normalMass = contact.normalMass;
        sample.tangentMass1 = contact.tangentMass1;
        sample.tangentMass2 = contact.tangentMass2;
        sample.bias = contact.bias;
        sample.frictionLimit = contact.frictionLimit;
        sample.accN = contact.accN;
        sample.accT1 = contact.accT1;
        sample.accT2 = contact.accT2;
        sample.warmStarted = contact.warmStarted;
        sample.isTerrain = contact.isTerrain;
        sample.supportsRestingPolicy = contact.supportsRestingPolicy;
        sample.allowsTangentFriction = contact.allowsTangentFriction;
        sample.normalCoupledFriction = contact.normalCoupledFriction;
        sample.inhibitsSleep = contact.inhibitsSleep;
        sample.manifoldPointCount = contact.manifoldPointCount;
        sample.terrainNormal = contact.terrainNormal;
        sample.terrainWarmStart = contact.terrainWarmStart;
        outSnapshot.persistentContacts.push_back( sample );
    }

    for ( const PersistentContactCacheEntry& cache : m_persistentContactCache )
    {
        ReplaySolverContactCacheSample sample;
        sample.key = cache.key;
        sample.accN = cache.accN;
        sample.accT1 = cache.accT1;
        sample.accT2 = cache.accT2;
        outSnapshot.persistentContactCache.push_back( sample );
    }

    outSnapshot.solverStats.rowCount = m_persistentContactSolverStats.rowCount;
    outSnapshot.solverStats.cachePreviousRows = m_persistentContactSolverStats.cachePreviousRows;
    outSnapshot.solverStats.cacheHits = m_persistentContactSolverStats.cacheHits;
    outSnapshot.solverStats.cacheMisses = m_persistentContactSolverStats.cacheMisses;
    outSnapshot.solverStats.warmStartedRows = m_persistentContactSolverStats.warmStartedRows;
    outSnapshot.solverStats.positionCorrectionRows = m_persistentContactSolverStats.positionCorrectionRows;
    outSnapshot.solverStats.solverIterations = m_persistentContactSolverStats.solverIterations;
    outSnapshot.solverStats.positionCorrectionTotal = m_persistentContactSolverStats.positionCorrectionTotal;
    outSnapshot.solverStats.positionCorrectionMax = m_persistentContactSolverStats.positionCorrectionMax;
}


bool PhysicsWorld::RestoreReplaySolverSnapshot( const ReplaySolverWorldSnapshot& snapshot, int modelCount )
{
    if ( snapshot.version < 1 || snapshot.version > 2 || snapshot.modelCount != modelCount )
    {
        return false;
    }

    m_timeRemaining = snapshot.timeRemaining;
    m_sleepSupportedThisFrame = snapshot.sleepSupportedThisFrame;
    m_sleepInhibitedThisFrame = snapshot.sleepInhibitedThisFrame;
    m_sleepState = snapshot.sleepState;
    m_sleepCounter = snapshot.sleepCounter;
    m_underwaterSleepLocked = snapshot.underwaterSleepLocked;
    m_tornadoCaptureSeconds = snapshot.tornadoCaptureSeconds;
    m_tornadoEjectCooldownSeconds = snapshot.tornadoEjectCooldownSeconds;
    m_collisionVisualContacts = snapshot.collisionVisualContacts;
    m_sleepIslandVisualId = snapshot.sleepIslandVisualId;
    m_sleepIslandAssignedVisualId = snapshot.sleepIslandAssignedVisualId;
    m_nextSleepIslandVisualId = snapshot.nextSleepIslandVisualId;
    m_sleepEnabled = snapshot.sleepEnabled;
    m_collisionVisualFrameActive = snapshot.collisionVisualFrameActive;
    m_sleepSupportEdges = snapshot.sleepSupportEdges;
    m_sleepIslandParent = snapshot.sleepIslandParent;
    m_sleepIslandRank = snapshot.sleepIslandRank;
    m_sleepIslandHasAwake = snapshot.sleepIslandHasAwake;
    m_sleepIslandHasSupportAnchor = snapshot.sleepIslandHasSupportAnchor;
    m_sleepIslandEligible = snapshot.sleepIslandEligible;
    m_sleepIslandCanSleep = snapshot.sleepIslandCanSleep;
    m_persistentContactCounts = snapshot.persistentContactCounts;
    m_persistentRestingContactCounts = snapshot.persistentRestingContactCounts;
    m_physicsDebugContacts = snapshot.debugContacts;
    m_physicsPipelineTrace = snapshot.pipelineTrace;
    m_collisionCellKeys = snapshot.collisionCellKeys;
    m_tornadoField.SetConfig( snapshot.tornadoConfig );
    m_tornadoSystem.SetConfig( snapshot.tornadoSystemConfig );
    m_tornadoSystem.SetElapsedSeconds( snapshot.tornadoSystemElapsedSeconds );

    m_persistentContacts.clear();
    m_persistentContacts.reserve( snapshot.persistentContacts.size() );
    for ( const ReplaySolverPersistentContactSample& sample : snapshot.persistentContacts )
    {
        PersistentContact contact;
        contact.bodyA = sample.bodyA;
        contact.bodyB = sample.bodyB;
        contact.featureId = sample.featureId;
        contact.key = sample.key;
        contact.normal = sample.normal;
        contact.tangent1 = sample.tangent1;
        contact.tangent2 = sample.tangent2;
        contact.rA = sample.rA;
        contact.rB = sample.rB;
        contact.penetration = sample.penetration;
        contact.normalMass = sample.normalMass;
        contact.tangentMass1 = sample.tangentMass1;
        contact.tangentMass2 = sample.tangentMass2;
        contact.bias = sample.bias;
        contact.frictionLimit = sample.frictionLimit;
        contact.accN = sample.accN;
        contact.accT1 = sample.accT1;
        contact.accT2 = sample.accT2;
        contact.warmStarted = sample.warmStarted;
        contact.isTerrain = sample.isTerrain;
        contact.supportsRestingPolicy = sample.supportsRestingPolicy;
        contact.allowsTangentFriction = sample.allowsTangentFriction;
        contact.normalCoupledFriction = sample.normalCoupledFriction;
        contact.inhibitsSleep = sample.inhibitsSleep;
        contact.manifoldPointCount = sample.manifoldPointCount;
        contact.terrainNormal = sample.terrainNormal;
        contact.terrainWarmStart = sample.terrainWarmStart;
        m_persistentContacts.push_back( contact );
    }

    m_persistentContactCache.clear();
    m_persistentContactCache.reserve( snapshot.persistentContactCache.size() );
    for ( const ReplaySolverContactCacheSample& sample : snapshot.persistentContactCache )
    {
        PersistentContactCacheEntry cache;
        cache.key = sample.key;
        cache.accN = sample.accN;
        cache.accT1 = sample.accT1;
        cache.accT2 = sample.accT2;
        m_persistentContactCache.push_back( cache );
    }

    m_persistentContactSolverStats = PersistentContactSolverStats();
    m_persistentContactSolverStats.rowCount = snapshot.solverStats.rowCount;
    m_persistentContactSolverStats.cachePreviousRows = snapshot.solverStats.cachePreviousRows;
    m_persistentContactSolverStats.cacheHits = snapshot.solverStats.cacheHits;
    m_persistentContactSolverStats.cacheMisses = snapshot.solverStats.cacheMisses;
    m_persistentContactSolverStats.warmStartedRows = snapshot.solverStats.warmStartedRows;
    m_persistentContactSolverStats.positionCorrectionRows = snapshot.solverStats.positionCorrectionRows;
    m_persistentContactSolverStats.solverIterations = snapshot.solverStats.solverIterations;
    m_persistentContactSolverStats.positionCorrectionTotal = snapshot.solverStats.positionCorrectionTotal;
    m_persistentContactSolverStats.positionCorrectionMax = snapshot.solverStats.positionCorrectionMax;

    m_candidatePairs.clear();
    m_solverBodies.clear();
    m_terrainContactManifolds.clear();
    m_terrainDetectionCandidates.clear();
    m_objectNarrowphaseEvents.clear();
    m_objectNarrowphaseIslands.clear();
    m_objectNarrowphaseIslandPairIndices.clear();
    m_objectNarrowphaseIslandWriteOffsets.clear();
    m_objectNarrowphaseParent.clear();
    m_objectNarrowphaseRank.clear();
    m_objectNarrowphaseRootToIsland.clear();
    m_spatialGrid.Clear();
    return true;
}


void PhysicsWorld::EnsureCollisionVisualBuffers( int modelCount )
{
    if ( static_cast<int>( m_collisionVisualContacts.size() ) != modelCount )
    {
        m_collisionVisualContacts.assign( modelCount, 0 );
    }
    if ( static_cast<int>( m_sleepIslandVisualId.size() ) != modelCount )
    {
        m_sleepIslandVisualId.assign( modelCount, 0 );
    }
}


void PhysicsWorld::EnsureTornadoStateBuffers( int modelCount )
{
    if ( static_cast<int>( m_tornadoCaptureSeconds.size() ) != modelCount )
    {
        m_tornadoCaptureSeconds.assign( modelCount, 0.0f );
    }
    if ( static_cast<int>( m_tornadoEjectCooldownSeconds.size() ) != modelCount )
    {
        m_tornadoEjectCooldownSeconds.assign( modelCount, 0.0f );
    }
}


void PhysicsWorld::EnsureUnderwaterSleepLockBuffer( int modelCount )
{
    if ( modelCount < 0 )
    {
        return;
    }
    if ( static_cast<int>( m_underwaterSleepLocked.size() ) != modelCount )
    {
        m_underwaterSleepLocked.resize( static_cast<size_t>( modelCount ), 0 );
    }
}


bool PhysicsWorld::IsFullySubmergedBall( const PhysicsBodyRecord& bodyRecord,
                                         const ColliderStore& colliderStore,
                                         int index )
{
    const auto& colliders = colliderStore.Records();
    if ( index < 0 || index >= static_cast<int>( colliders.size() ) || bodyRecord.isFixed ||
         colliders[static_cast<size_t>( index )].shapeKind != ColliderShapeKind::Sphere )
    {
        return false;
    }

    return bodyRecord.submergedVolumePercent >= UNDERWATER_SLEEP_LOCK_SUBMERGED_PERCENT;
}


bool PhysicsWorld::RefreshUnderwaterSubmersionForBall( const PhysicsWorldForces& worldForces,
                                                       PhysicsBodyStore& bodyStore,
                                                       const ColliderStore& colliderStore,
                                                       int index )
{
    PhysicsBodyRecord* bodyRecord = bodyStore.MutableRecordForModelIndex( index );
    if ( !bodyRecord )
    {
        return false;
    }

    bodyRecord->submergedVolumePercent = 0.0f;
    const auto& colliders = colliderStore.Records();
    if ( index < 0 || index >= static_cast<int>( colliders.size() ) )
    {
        return false;
    }

    const ColliderRecord& collider = colliders[static_cast<std::size_t>( index )];
    if ( collider.shapeKind != ColliderShapeKind::Sphere )
    {
        return false;
    }

    const auto* sphere = std::get_if<Math::CollisionDetection::BoundingSphere>( &collider.shape );
    if ( !sphere )
    {
        return false;
    }

    Math::Orientation::Quaternion orientation = bodyRecord->orientation;
    const Math::Transformation::RotationMatrix rotation = orientation.GetOrientationMatrix();
    const Vector3 center = bodyRecord->position + ( rotation * sphere->GetPosition() );
    const float radius = sphere->GetRadius();
    if ( radius <= TOLERANCE )
    {
        return false;
    }

    const float fluidHeightRelativeToCenter = worldForces.fluidSurfaceHeight - center.y;
    if ( fluidHeightRelativeToCenter <= -radius )
    {
        return true;
    }
    if ( fluidHeightRelativeToCenter >= radius )
    {
        bodyRecord->submergedVolumePercent = 1.0f;
        return true;
    }

    // Concept: use the analytic sphere-cap fraction, deriving the world-space
    // sphere center from physics-owned body pose and collider shape.
    const float yValue = fluidHeightRelativeToCenter + radius;
    bodyRecord->submergedVolumePercent =
        std::clamp( ( ONE_OVER_THREE * _PI * ( ( 3.0f * radius ) - yValue ) * yValue * yValue ) / sphere->GetVolume(),
                    0.0f,
                    1.0f );
    return true;
}


void PhysicsWorld::LockUnderwaterSleeperIfReady( const PhysicsWorldForces& worldForces,
                                                 PhysicsBodyStore& bodyStore,
                                                 const ColliderStore& colliderStore,
                                                 int index )
{
    const int bodyCount = bodyStore.Count();
    EnsureUnderwaterSleepLockBuffer( bodyCount );
    if ( index < 0 || index >= bodyCount || index >= static_cast<int>( m_sleepState.size() ) || !m_sleepState[index] ||
         m_underwaterSleepLocked[index] )
    {
        return;
    }

    if ( !RefreshUnderwaterSubmersionForBall( worldForces, bodyStore, colliderStore, index ) )
    {
        return;
    }
    PhysicsBodyRecord* record = bodyStore.MutableRecordForModelIndex( index );
    if ( !record || !IsFullySubmergedBall( *record, colliderStore, index ) )
    {
        return;
    }

    m_underwaterSleepLocked[index] = 1;
    if ( index < static_cast<int>( m_timeRemaining.size() ) )
    {
        m_timeRemaining[index] = 0.0f;
    }
    record->linearVelocity = ZERO_VECTOR;
    record->angularVelocity = ZERO_VECTOR;
    record->isSleeping = true;
}


bool PhysicsWorld::IsUnderwaterSleepLocked( int bodyCount, int index )
{
    EnsureUnderwaterSleepLockBuffer( bodyCount );
    if ( index < 0 || index >= bodyCount )
    {
        return false;
    }
    if ( m_underwaterSleepLocked[index] )
    {
        return true;
    }

    return m_underwaterSleepLocked[index] != 0;
}


void PhysicsWorld::MarkCollisionVisualContact( int index )
{
    if ( index < 0 || index >= static_cast<int>( m_collisionVisualContacts.size() ) )
    {
        return;
    }
    m_collisionVisualContacts[index] = 1;
}


void PhysicsWorld::RecordPhysicsPipelineStage( const PhysicsPipelineRecord& record )
{
    if ( m_physicsPipelineTrace.size() < MAX_PIPELINE_TRACE_RECORDS )
    {
        m_physicsPipelineTrace.push_back( record );
    }
}


bool PhysicsWorld::CanRecordPhysicsPipelineStage() const
{
    return m_physicsPipelineTrace.size() < MAX_PIPELINE_TRACE_RECORDS;
}


PersistentContactSolverContext PhysicsWorld::CreatePersistentContactSolverContext( PhysicsBodyStore& bodyStore,
                                                                                   const ColliderStore& colliderStore,
                                                                                   const Basics::EngineConfig& config )
{
    return PersistentContactSolverContext{ m_candidatePairs,
                                           m_sleepState,
                                           m_sleepSupportEdges,
                                           m_persistentContacts,
                                           m_persistentContactCache,
                                           m_persistentContactSolverStats,
                                           m_persistentContactCounts,
                                           m_persistentRestingContactCounts,
                                           m_solverBodies,
                                           m_physicsDebugContacts,
                                           m_terrainContactManifolds,
                                           m_terrainRestApplied,
                                           m_sleepSupportedThisFrame,
                                           m_persistentContactSideEffects,
                                           bodyStore.MutableRecords(),
                                           colliderStore.Records(),
                                           bodyStore.Count(),
                                           (std::max)( 0,
                                                       static_cast<int>( MAX_PIPELINE_TRACE_RECORDS ) -
                                                           static_cast<int>( m_physicsPipelineTrace.size() ) ),
                                           config };
}


void PhysicsWorld::PreparePersistentContactSideEffects( int modelCount )
{
    PersistentContactSolverSideEffects& effects = m_persistentContactSideEffects;
    effects.pipelineRecords.clear();
    effects.collisionVisualBodies.clear();
    effects.fixedContactBodies.clear();
    effects.releaseWakeBodies.clear();
    effects.fixedTreeReleases.clear();

    const int pipelineCapacity = (std::max)( 0,
                                             static_cast<int>( MAX_PIPELINE_TRACE_RECORDS ) -
                                                 static_cast<int>( m_physicsPipelineTrace.size() ) );
    // Invariant: these side-effect lists are pre-reserved before steady
    // physics. If any reserve is short, preserving determinism is no longer
    // possible because the solver would need to allocate or skip a queued
    // post-pass action.
    assert( effects.collisionVisualBodies.capacity() >= m_candidatePairs.size() * 2 );
    assert( effects.fixedContactBodies.capacity() >= static_cast<std::size_t>( modelCount ) );
    assert( effects.releaseWakeBodies.capacity() >= 8 );
    assert( effects.fixedTreeReleases.capacity() >= 8 );
    assert( effects.pipelineRecords.capacity() >= static_cast<std::size_t>( pipelineCapacity ) );
    if ( effects.collisionVisualBodies.capacity() < m_candidatePairs.size() * 2 ||
         effects.fixedContactBodies.capacity() < static_cast<std::size_t>( modelCount ) ||
         effects.releaseWakeBodies.capacity() < 8 || effects.fixedTreeReleases.capacity() < 8 ||
         effects.pipelineRecords.capacity() < static_cast<std::size_t>( pipelineCapacity ) )
    {
        SB_FATAL( "Physics/PhysicsWorld", "Physics persistent-contact side-effect capacity exhausted." );
    }
}


void PhysicsWorld::ApplyPersistentContactSideEffects( PhysicsBodyStore& bodyStore,
                                                      const ColliderStore& colliderStore,
                                                      const PhysicsWorldForces& worldForces )
{
    const PersistentContactSolverSideEffects& effects = m_persistentContactSideEffects;
    for ( const PhysicsPipelineRecord& record : effects.pipelineRecords )
    {
        RecordPhysicsPipelineStage( record );
    }
    for ( int index : effects.collisionVisualBodies )
    {
        MarkCollisionVisualContact( index );
    }

    for ( int index : effects.releaseWakeBodies )
    {
        WakeModel( bodyStore, colliderStore, worldForces, index );
    }
}


SleepSupportPropagationContext PhysicsWorld::CreateSleepSupportPropagationContext()
{
    return SleepSupportPropagationContext{ m_sleepState, m_sleepSupportEdges, m_sleepSupportedThisFrame };
}


void PhysicsWorld::BeginCollisionVisualFrame( int modelCount )
{
    m_collisionVisualContacts.assign( modelCount, 0 );
    if ( static_cast<int>( m_sleepIslandVisualId.size() ) != modelCount )
    {
        m_sleepIslandVisualId.assign( modelCount, 0 );
    }
    m_collisionVisualFrameActive = true;
}


void PhysicsWorld::EndCollisionVisualFrame()
{
    m_collisionVisualFrameActive = false;
}


void PhysicsWorld::ClearPointJointConstraints()
{
    m_pointJointConstraints.clear();
}


PhysicsConstraintHandle PhysicsWorld::CreatePointJoint( const PhysicsPointJointCreateDesc& desc )
{
    if ( !desc.bodyA.IsValid() || !desc.bodyB.IsValid() || desc.bodyA == desc.bodyB )
    {
        return PhysicsConstraintHandle{};
    }

    // Why: callers create constraints with handle-keyed descriptors, while the
    // solver still iterates dense PointJointConstraint rows without indirection.
    PointJointConstraint constraint;
    constraint.SetBodies( desc.bodyA, desc.bodyB );
    constraint.localAnchorA = desc.localAnchorA;
    constraint.localAnchorB = desc.localAnchorB;
    constraint.slack = desc.slack;
    constraint.stiffness = desc.stiffness;
    constraint.damping = desc.damping;
    constraint.groupId = desc.groupId;
    constraint.flags = desc.flags;

    PhysicsConstraintHandle handle;
    handle.index = static_cast<uint32_t>( m_pointJointConstraints.size() );
    handle.generation = PHYSICS_HANDLE_INITIAL_GENERATION;
    m_pointJointConstraints.push_back( constraint );
    return handle;
}


const std::vector<PointJointConstraint>& PhysicsWorld::GetPointJointConstraints() const
{
    return m_pointJointConstraints;
}


void PhysicsWorld::RunPhysics( PhysicsBodyStore& bodyStore,
                               const ColliderStore& colliderStore,
                               float fChangeInTime,
                               const Basics::EngineConfig& config,
                               const PhysicsWorldForces& worldForces,
                               Threading::WorkerPool& workerPool,
                               const char* const* diagnosticNames,
                               int diagnosticNameCount )
{
    // Concept: one fixed physics tick has a predictable data flow.
    //
    // 1. Resize/clear per-frame arrays so every model index has a slot.
    // 2. Reset debug, sleep-support, pipeline, and terrain-manifold output.
    // 3. Run broadphase, swept movement, terrain manifold generation, and the
    //    persistent Catto-style contact solver.
    // 4. Emit bounded Debug diagnostics before PhysicsScene copies solved state
    //    into PhysicsBodyStore and invalidates cached model-order data at the
    //    scene owner boundary.
    //
    // Determinism note: changing this ordering can change byte-exact physics
    // baselines even when the final scene "looks" similar.
    const int modelCount = bodyStore.Count();
    const auto& bodyRecords = bodyStore.Records();
    EnsureCollisionVisualBuffers( modelCount );
    if ( !m_collisionVisualFrameActive )
    {
        m_collisionVisualContacts.assign( modelCount, 0 );
    }
    m_timeRemaining.assign( modelCount, fChangeInTime );
    m_sleepSupportedThisFrame.assign( modelCount, 0 );
    m_sleepInhibitedThisFrame.assign( modelCount, 0 );
    m_physicsDebugContacts.clear();
    m_physicsPipelineTrace.clear();
    m_terrainContactManifolds.clear();
    m_sleepSupportEdges.clear();

    if ( static_cast<int>( m_sleepState.size() ) != modelCount )
    {
        m_sleepState.assign( modelCount, 0 );
        m_sleepCounter.assign( modelCount, 0 );
    }
    bodyStore.CopySleepStatesTo( m_sleepState );
    EnsureUnderwaterSleepLockBuffer( modelCount );
    if ( !m_sleepEnabled )
    {
        std::fill( m_sleepState.begin(), m_sleepState.end(), static_cast<uint8_t>( 0 ) );
        std::fill( m_sleepCounter.begin(), m_sleepCounter.end(), static_cast<uint8_t>( 0 ) );
        std::fill( m_underwaterSleepLocked.begin(), m_underwaterSleepLocked.end(), static_cast<uint8_t>( 0 ) );
        std::fill( m_sleepIslandVisualId.begin(), m_sleepIslandVisualId.end(), 0 );
    }
    for ( int i = 0; i < modelCount; ++i )
    {
        if ( i < static_cast<int>( bodyRecords.size() ) && bodyRecords[static_cast<size_t>( i )].isFixed )
        {
            m_sleepState[i] = 0;
            m_sleepCounter[i] = 0;
            m_underwaterSleepLocked[i] = 0;
            m_sleepSupportedThisFrame[i] = 1;
            m_sleepIslandVisualId[i] = 0;
            continue;
        }
        if ( !m_sleepState[i] )
        {
            m_underwaterSleepLocked[i] = 0;
            m_sleepIslandVisualId[i] = 0;
        }
    }

    RunSolverPhysics( bodyStore,
                      colliderStore,
                      fChangeInTime,
                      config,
                      worldForces,
                      workerPool,
                      diagnosticNames,
                      diagnosticNameCount );
    bodyStore.CopySleepStatesFrom( m_sleepState );
}


bool PhysicsWorld::ShouldEmitStepDiagnostics() const
{
#ifdef _DEBUG
    return !m_diagnosticsSuppressed && ( m_diagnostics.IsRegressionLogEnabled() || m_diagnostics.IsFrameLogEnabled() );
#else
    return false;
#endif
}


bool PhysicsWorld::ShouldEmitCollisionTimeDiagnostics() const
{
#ifdef _DEBUG
    return !m_diagnosticsSuppressed && m_diagnostics.IsCollisionTimeLogEnabled();
#else
    return false;
#endif
}


void PhysicsWorld::EmitStepDiagnostics( const PhysicsBodyStore& bodyStore,
                                        const ColliderStore& colliderStore,
                                        float fChangeInTime,
                                        const char* const* diagnosticNames,
                                        int diagnosticNameCount )
{
#ifdef _DEBUG
    if ( !m_diagnosticsSuppressed )
    {
        const bool regressionLogEnabled = m_diagnostics.IsRegressionLogEnabled();
        const bool frameLogEnabled = m_diagnostics.IsFrameLogEnabled();
        if ( regressionLogEnabled || frameLogEnabled )
        {
            const PhysicsDiagnosticsNameView names{ diagnosticNames, diagnosticNameCount };
            const PhysicsDiagnosticsView diagnosticsView = GetDiagnosticsView();
            const PhysicsDiagnosticsFrameInput frame{ diagnosticsView, bodyStore, colliderStore, names, fChangeInTime };
            if ( regressionLogEnabled )
            {
                m_diagnostics.EmitRegressionLog( frame );
            }
            if ( frameLogEnabled )
            {
                m_diagnostics.EmitFrame( frame );
            }
        }
        m_diagnostics.IncrementCollisionTimeFrameIfEnabled();
    }
#else
    (void)bodyStore;
    (void)colliderStore;
    (void)fChangeInTime;
    (void)diagnosticNames;
    (void)diagnosticNameCount;
#endif
}


void PhysicsWorld::WakeModel( PhysicsBodyStore& bodyStore, int index )
{
    WakeModel( bodyStore.Count(), bodyStore.Records(), &bodyStore, nullptr, nullptr, index );
}


void PhysicsWorld::WakeModel( PhysicsBodyStore& bodyStore,
                              const ColliderStore& colliderStore,
                              const PhysicsWorldForces& worldForces,
                              int index )
{
    WakeModel( bodyStore.Count(), bodyStore.Records(), &bodyStore, &colliderStore, &worldForces, index );
}


// Why: callers that already hold PhysicsBodyStore should not refresh the
// model owner just to wake a body. Store-owned wake commands stay on dense body
// records; PhysicsScene owns any owner-side cache invalidation.
void PhysicsWorld::WakeModel( int bodyCount,
                              const PhysicsBodyRecordList& bodyRecords,
                              PhysicsBodyStore* bodyStore,
                              const ColliderStore* colliderStore,
                              const PhysicsWorldForces* worldForces,
                              int index )
{
    const int modelCount = (std::min)( bodyCount, static_cast<int>( bodyRecords.size() ) );
    if ( index >= 0 && index < modelCount )
    {
        if ( bodyRecords[static_cast<size_t>( index )].isFixed )
        {
            return;
        }
    }
    else if ( index >= 0 )
    {
        return;
    }

    if ( static_cast<int>( m_sleepState.size() ) < modelCount )
    {
        m_sleepState.resize( modelCount, 0 );
        m_sleepCounter.resize( modelCount, 0 );
    }
    else if ( static_cast<int>( m_sleepState.size() ) > modelCount )
    {
        m_sleepState.assign( modelCount, 0 );
        m_sleepCounter.assign( modelCount, 0 );
    }
    EnsureUnderwaterSleepLockBuffer( modelCount );
    if ( index >= 0 && index < static_cast<int>( m_sleepState.size() ) )
    {
        if ( bodyStore && !m_underwaterSleepLocked[index] && m_sleepState[index] )
        {
            bool refreshedSubmersion = false;
            if ( colliderStore && worldForces )
            {
                refreshedSubmersion =
                    RefreshUnderwaterSubmersionForBall( *worldForces, *bodyStore, *colliderStore, index );
            }
            const PhysicsBodyRecord* record = bodyStore->RecordForModelIndex( index );
            if ( record && colliderStore && ( refreshedSubmersion || record->submergedVolumePercent > 0.0f ) )
            {
                if ( IsFullySubmergedBall( *record, *colliderStore, index ) )
                {
                    m_underwaterSleepLocked[index] = 1;
                    if ( index < static_cast<int>( m_timeRemaining.size() ) )
                    {
                        m_timeRemaining[index] = 0.0f;
                    }
                    return;
                }
            }
        }
        if ( IsUnderwaterSleepLocked( modelCount, index ) )
        {
            return;
        }
    }
    if ( index >= 0 && index < static_cast<int>( m_sleepState.size() ) )
    {
        WakeSleepVisualIsland( modelCount, bodyRecords, bodyStore, index, 0.0f, false );
        WakePointJointIsland( modelCount, bodyRecords, bodyStore, index, 0.0f, false );
        WakeRestingContactIsland( modelCount, bodyRecords, bodyStore, index, 0.0f, false );
    }
}


void PhysicsWorld::SeedModelAsleep( const PhysicsBodyStore& bodyStore, int index )
{
    SeedModelAsleep( bodyStore.Count(), bodyStore.Records(), index );
}


// Why: store-owned seed commands already have dense body records. Avoid
// rebuilding presentation streams from inside PhysicsWorld; owner-side
// projection belongs to PhysicsScene.
void PhysicsWorld::SeedModelAsleep( int bodyCount, const PhysicsBodyRecordList& bodyRecords, int index )
{
    if ( !m_sleepEnabled )
    {
        return;
    }

    const int modelCount = (std::min)( bodyCount, static_cast<int>( bodyRecords.size() ) );
    if ( index < 0 || index >= modelCount || bodyRecords[static_cast<size_t>( index )].isFixed )
    {
        return;
    }

    if ( static_cast<int>( m_sleepState.size() ) < modelCount )
    {
        m_sleepState.resize( modelCount, 0 );
        m_sleepCounter.resize( modelCount, 0 );
    }
    else if ( static_cast<int>( m_sleepState.size() ) > modelCount )
    {
        m_sleepState.assign( modelCount, 0 );
        m_sleepCounter.assign( modelCount, 0 );
    }
    if ( static_cast<int>( m_sleepIslandVisualId.size() ) < modelCount )
    {
        m_sleepIslandVisualId.resize( modelCount, 0 );
    }
    else if ( static_cast<int>( m_sleepIslandVisualId.size() ) > modelCount )
    {
        m_sleepIslandVisualId.assign( modelCount, 0 );
    }
    EnsureUnderwaterSleepLockBuffer( modelCount );

    m_sleepState[index] = 1;
    m_sleepCounter[index] = m_seedSleepFrameCount;
    m_underwaterSleepLocked[index] = 0;
    if ( index < static_cast<int>( m_sleepIslandVisualId.size() ) )
    {
        m_sleepIslandVisualId[index] = m_nextSleepIslandVisualId++;
        if ( m_nextSleepIslandVisualId <= 0 )
        {
            m_nextSleepIslandVisualId = 1;
        }
    }
}


void PhysicsWorld::SetPhysicsSleepEnabled( bool enabled )
{
    m_sleepEnabled = enabled;
    if ( enabled )
    {
        return;
    }

    std::fill( m_sleepState.begin(), m_sleepState.end(), static_cast<uint8_t>( 0 ) );
    std::fill( m_sleepCounter.begin(), m_sleepCounter.end(), static_cast<uint8_t>( 0 ) );
    std::fill( m_underwaterSleepLocked.begin(), m_underwaterSleepLocked.end(), static_cast<uint8_t>( 0 ) );
    std::fill( m_sleepIslandVisualId.begin(), m_sleepIslandVisualId.end(), 0 );
    std::fill( m_sleepIslandAssignedVisualId.begin(), m_sleepIslandAssignedVisualId.end(), 0 );
}


bool PhysicsWorld::IsPhysicsSleepEnabled() const
{
    return m_sleepEnabled;
}


void PhysicsWorld::ApplyTornadoField( PhysicsBodyStore& bodyStore,
                                      const ColliderStore& colliderStore,
                                      const PhysicsWorldForces& worldForces,
                                      float dt,
                                      const Basics::EngineConfig& runtimeConfig,
                                      Threading::WorkerPool& workerPool )
{
    const TornadoFieldConfig& config = m_tornadoField.GetConfig();
    const float step = (std::max)( 0.0f, dt );
    const bool useSystem = m_tornadoSystem.IsEnabled();
    if ( useSystem )
    {
        m_tornadoSystem.Tick( step );
    }
    const std::vector<TornadoActiveVortex>& activeVortices = m_tornadoSystem.ActiveVortices();
    if ( ( !useSystem && !config.enabled ) || ( useSystem && activeVortices.empty() ) )
    {
        return;
    }

    PROFILE_SCOPED( "Frame/Physics/TornadoField" );
    auto& bodyRecords = bodyStore.MutableRecords();
    auto sampleAcceleration =
        [&]( const Vector3& position, TornadoFieldConfig& outBestConfig, float& outBestAccelerationSq ) -> Vector3
    {
        Vector3 acceleration = ZERO_VECTOR;
        outBestConfig = config;
        outBestAccelerationSq = 0.0f;
        if ( useSystem )
        {
            for ( const TornadoActiveVortex& vortex : activeVortices )
            {
                const Vector3 sample = TornadoField::SampleAccelerationForConfig( vortex.field, position );
                const float sampleSq = sample * sample;
                acceleration += sample;
                if ( sampleSq > outBestAccelerationSq )
                {
                    outBestAccelerationSq = sampleSq;
                    outBestConfig = vortex.field;
                }
            }
        }
        else
        {
            acceleration = m_tornadoField.SampleAcceleration( position );
            outBestAccelerationSq = acceleration * acceleration;
        }
        return acceleration;
    };

    if ( useSystem )
    {
        for ( int i = 0; i < bodyStore.Count(); ++i )
        {
            PhysicsBodyRecord& record = bodyRecords[static_cast<size_t>( i )];
            if ( !record.isFixed || !record.releasesFromFixedOnContact )
            {
                continue;
            }

            TornadoFieldConfig bestConfig;
            float bestAccelerationSq = 0.0f;
            const Vector3 acceleration =
                sampleAcceleration( bodyRecords[static_cast<size_t>( i )].position, bestConfig, bestAccelerationSq );
            const float releaseAcceleration = (std::max)( 16.0f, record.contactReleaseImpulseThreshold * 32.0f );
            if ( bestAccelerationSq < releaseAcceleration * releaseAcceleration )
            {
                continue;
            }

            const Vector3 seedLinearVelocity =
                ClampVectorMagnitude( acceleration * 0.08f, (std::max)( 10.0f, bestConfig.maxDeltaVelocity * 1.5f ) );
            const Vector3 seedAngularVelocity( seedLinearVelocity.z * 0.08f, 0.0f, -seedLinearVelocity.x * 0.08f );
            // Why: tornado release runs before broadphase and the parallel
            // tornado pass. Mutate the body store directly so later fixed
            // checks see dynamic bodies from the authoritative row.
            PhysicsBodyStore::ReleaseFixedRecord( record, seedLinearVelocity, seedAngularVelocity );
            WakeModel( bodyStore, colliderStore, worldForces, i );
            bodyStore.ReleaseAttachedFixedTreeParts(
                PhysicsFixedTreeReleaseEvent{ i, seedLinearVelocity, seedAngularVelocity },
                m_tornadoFixedTreeReleaseWakeBodies );
            for ( int releasedIndex : m_tornadoFixedTreeReleaseWakeBodies )
            {
                WakeModel( bodyStore, colliderStore, worldForces, releasedIndex );
            }
        }
    }

    const int modelCount =
        (std::min)( { bodyStore.Count(), static_cast<int>( bodyRecords.size() ), colliderStore.Count() } );
    EnsureTornadoStateBuffers( modelCount );

    auto applyTornadoAt = [&]( int i )
    {
        if ( bodyRecords[static_cast<size_t>( i )].isFixed || IsUnderwaterSleepLocked( modelCount, i ) )
        {
            m_tornadoCaptureSeconds[i] = 0.0f;
            m_tornadoEjectCooldownSeconds[i] = 0.0f;
            return;
        }

        const Vector3 position = bodyRecords[static_cast<size_t>( i )].position;
        TornadoFieldConfig bestConfig;
        float bestAccelerationSq = 0.0f;
        Vector3 acceleration = sampleAcceleration( position, bestConfig, bestAccelerationSq );
        const float dx = position.x - bestConfig.center.x;
        const float dz = position.z - bestConfig.center.z;
        const float horizontalSq = dx * dx + dz * dz;
        const float horizontal = sqrtf( horizontalSq );
        const float height = (std::max)( bestConfig.height, 1.0f );
        const float height01 = ( position.y - bestConfig.center.y ) / height;
        if ( bestAccelerationSq <= TOLERANCE * TOLERANCE )
        {
            m_tornadoCaptureSeconds[i] = 0.0f;
            m_tornadoEjectCooldownSeconds[i] = (std::max)( 0.0f, m_tornadoEjectCooldownSeconds[i] - step );
            return;
        }

        if ( m_sleepState[i] )
        {
            m_sleepState[i] = 0;
            m_sleepCounter[i] = 0;
            m_sleepIslandVisualId[i] = 0;
            m_timeRemaining[i] = dt;
            bodyRecords[static_cast<size_t>( i )].isSleeping = false;
            (void)bodyStore.ApplyForces( worldForces, colliderStore, i, dt );
        }

        Vector3 velocity = bodyRecords[static_cast<size_t>( i )].linearVelocity;
        m_tornadoCaptureSeconds[i] += step;
        m_tornadoEjectCooldownSeconds[i] = (std::max)( 0.0f, m_tornadoEjectCooldownSeconds[i] - step );

        const float ejectBand = std::clamp( bestConfig.ejectBand, 0.0f, 1.0f );
        const float minCaptureSeconds = (std::max)( 0.0f, bestConfig.minCaptureSeconds );
        const float cooldownSeconds = (std::max)( 0.0f, bestConfig.ejectCooldownSeconds );
        const float maxDeltaVelocity = (std::max)( 1.0f, bestConfig.maxDeltaVelocity );
        const float minTangentialSpeed = (std::max)( 18.0f, bestConfig.swirlAcceleration * 0.12f );
        Vector3 outward;
        if ( horizontal > TOLERANCE )
        {
            outward = Vector3( dx / horizontal, 0.0f, dz / horizontal );
        }
        else
        {
            switch ( i & 3 )
            {
            case 0:
                outward = Vector3( 1.0f, 0.0f, 0.0f );
                break;
            case 1:
                outward = Vector3( 0.0f, 0.0f, 1.0f );
                break;
            case 2:
                outward = Vector3( -1.0f, 0.0f, 0.0f );
                break;
            default:
                outward = Vector3( 0.0f, 0.0f, -1.0f );
                break;
            }
        }

        const Vector3 tangent( -outward.z, 0.0f, outward.x );
        const float tangentialSpeed = fabsf( velocity * tangent );
        const int captureBucket = static_cast<int>( m_tornadoCaptureSeconds[i] * TORNADO_EJECTION_PHASE_HZ );
        const bool deterministicSlot = ( ( i + captureBucket ) % 3 ) == 0;
        if ( height01 >= ejectBand && m_tornadoCaptureSeconds[i] >= minCaptureSeconds &&
             m_tornadoEjectCooldownSeconds[i] <= 0.0f && tangentialSpeed >= minTangentialSpeed && deterministicSlot )
        {
            acceleration +=
                outward * bestConfig.ejectAcceleration + Vector3( 0.0f, bestConfig.ejectUpAcceleration, 0.0f );
            m_tornadoCaptureSeconds[i] = 0.0f;
            m_tornadoEjectCooldownSeconds[i] = cooldownSeconds;
        }

        velocity += ClampVectorMagnitude( acceleration * step, maxDeltaVelocity );
        bodyRecords[static_cast<size_t>( i )].linearVelocity = velocity;
    };

    if ( runtimeConfig.physicsParallel && runtimeConfig.physicsParallelTornadoField )
    {
        workerPool.ParallelForNoAlloc( 0,
                                       modelCount,
                                       applyTornadoAt,
                                       PHYSICS_PARALLEL_MIN_BODIES,
                                       "Frame/Physics/TornadoField/WorkerBodies",
                                       PHYSICS_TORNADO_WORKER_HASH );
    }
    else
    {
        for ( int i = 0; i < modelCount; ++i )
        {
            applyTornadoAt( i );
        }
    }
}


void PhysicsWorld::SetTornadoFieldConfig( const TornadoFieldConfig& config )
{
    m_tornadoField.SetConfig( config );
    if ( !m_tornadoField.GetConfig().enabled )
    {
        m_tornadoCaptureSeconds.clear();
        m_tornadoEjectCooldownSeconds.clear();
    }
}


const TornadoFieldConfig& PhysicsWorld::GetTornadoFieldConfig() const
{
    return m_tornadoField.GetConfig();
}


void PhysicsWorld::SetTornadoSystemConfig( const TornadoSystemConfig& config )
{
    m_tornadoSystem.SetConfig( config );
    if ( !m_tornadoSystem.IsEnabled() && !m_tornadoField.GetConfig().enabled )
    {
        m_tornadoCaptureSeconds.clear();
        m_tornadoEjectCooldownSeconds.clear();
    }
}


const TornadoSystemConfig& PhysicsWorld::GetTornadoSystemConfig() const
{
    return m_tornadoSystem.GetConfig();
}


float PhysicsWorld::GetTornadoSystemElapsedSeconds() const
{
    return m_tornadoSystem.GetElapsedSeconds();
}


void PhysicsWorld::RenderTornadoFieldVectors( const Math::Transformation::Matrix4& viewProj,
                                              Rendering::IRenderCommandContext& renderCommands,
                                              bool supportsDebugLines )
{
    if ( m_tornadoSystem.IsEnabled() )
    {
        m_tornadoSystem.RenderVectors( viewProj, renderCommands, supportsDebugLines );
        return;
    }
    m_tornadoField.RenderVectors( viewProj, renderCommands, supportsDebugLines );
}


#ifdef _DEBUG
void PhysicsWorld::SetPhysicsRegressionLogPath( const char* path )
{
    m_diagnostics.SetPhysicsRegressionLogPath( path );
}


void PhysicsWorld::SetPhysicsCollisionTimeLogPath( const char* path )
{
    m_diagnostics.SetPhysicsCollisionTimeLogPath( path );
}


void PhysicsWorld::SetPhysicsDiagnosticsPath( const char* path )
{
    m_diagnostics.SetPhysicsDiagnosticsPath( path );
}


void PhysicsWorld::SetPhysicsDiagnosticsRunId( const char* runId )
{
    m_diagnostics.SetPhysicsDiagnosticsRunId( runId );
}


bool PhysicsWorld::SetDiagnosticsSuppressed( bool suppressed )
{
    const bool previous = m_diagnosticsSuppressed;
    m_diagnosticsSuppressed = suppressed;
    return previous;
}


#endif


void PhysicsWorld::EmitPhysicsCollisionTime( const char* const* diagnosticNames,
                                             int diagnosticNameCount,
                                             const char* type,
                                             int bodyA,
                                             int bodyB,
                                             float collisionTime,
                                             float availableTime )
{
#ifdef _DEBUG
    if ( m_diagnosticsSuppressed )
    {
        return;
    }
#endif
    m_diagnostics
        .EmitCollisionTime( diagnosticNames, diagnosticNameCount, type, bodyA, bodyB, collisionTime, availableTime );
}


void PhysicsWorld::PropagateSleepSupport( const PhysicsBodyRecordList& bodyRecords )
{
    SleepSupportPropagationContext context = CreateSleepSupportPropagationContext();
    m_sleepIslandSystem.PropagateSupport( context, bodyRecords );
}


void PhysicsWorld::AppendPointJointSupportEdges( const PhysicsBodyStore& bodyStore, int modelCount )
{
    // Concept: a point joint is not a contact, but it is still a physical
    // support path for sleep. Adding bidirectional edges lets a quiet ragdoll
    // limb inherit support from any grounded body in the same constrained
    // component without changing the contact solver rows.
    if ( m_pointJointConstraints.empty() )
    {
        return;
    }

    for ( const PointJointConstraint& constraint : m_pointJointConstraints )
    {
        const int a = constraint.BodyAIndex( bodyStore );
        const int b = constraint.BodyBIndex( bodyStore );
        if ( a < 0 || b < 0 || a == b || a >= modelCount || b >= modelCount )
        {
            continue;
        }

        m_sleepSupportEdges.emplace_back( a, b );
        m_sleepSupportEdges.emplace_back( b, a );
    }
}


void PhysicsWorld::ForgetPersistentContactCacheForBody( int bodyIndex )
{
    const auto cacheEntryReferencesBody = []( const PersistentContactCacheEntry& entry, int index ) -> bool
    {
        const uint64_t key = static_cast<uint64_t>( entry.key );
        const uint32_t highBody = static_cast<uint32_t>( ( key >> 48 ) & 0xffffu );
        if ( highBody == 0xffffu )
        {
            const uint32_t terrainBody = static_cast<uint32_t>( ( key >> 16 ) & 0xffffffffu );
            return terrainBody == static_cast<uint32_t>( index );
        }

        const uint32_t lowBody = static_cast<uint32_t>( ( key >> 40 ) & 0xffffffu );
        const uint32_t objectHighBody = static_cast<uint32_t>( ( key >> 16 ) & 0xffffffu );
        return lowBody == static_cast<uint32_t>( index ) || objectHighBody == static_cast<uint32_t>( index );
    };

    m_persistentContactCache.erase(
        std::remove_if( m_persistentContactCache.begin(),
                        m_persistentContactCache.end(),
                        [bodyIndex, &cacheEntryReferencesBody]( const PersistentContactCacheEntry& entry )
                        { return cacheEntryReferencesBody( entry, bodyIndex ); } ),
        m_persistentContactCache.end() );
}


// Why: store-owned wake propagation uses the same sleep-state mutation as the
// deleted legacy stream path, but fixed-state authority comes from
// PhysicsBodyRecord. This keeps solver-triggered wakeups on physics-owned rows.
bool PhysicsWorld::WakeDynamicBodyState( int bodyCount,
                                         const PhysicsBodyRecordList& bodyRecords,
                                         PhysicsBodyStore* bodyStore,
                                         int index,
                                         float dt,
                                         bool applyForces,
                                         const PhysicsWorldForces* worldForces,
                                         const ColliderStore* colliderStore )
{
    if ( index < 0 || index >= bodyCount || index >= static_cast<int>( bodyRecords.size() ) ||
         index >= static_cast<int>( m_sleepState.size() ) )
    {
        return false;
    }
    if ( bodyRecords[static_cast<size_t>( index )].isFixed )
    {
        return false;
    }

    const bool wasSleeping = m_sleepState[index] != 0;
    const bool hadCounter = index < static_cast<int>( m_sleepCounter.size() ) && m_sleepCounter[index] != 0;
    const bool hadSleepVisual =
        index < static_cast<int>( m_sleepIslandVisualId.size() ) && m_sleepIslandVisualId[index] != 0;
    const bool wasUnderwaterLocked =
        index < static_cast<int>( m_underwaterSleepLocked.size() ) && m_underwaterSleepLocked[index] != 0;

    m_sleepState[index] = 0;
    if ( bodyStore )
    {
        // Why: wake propagation already walks dense solver rows. Mutating the
        // row directly avoids converting the row index through handle maps on
        // an island-wake path.
        PhysicsBodyRecord* record = bodyStore->MutableRecordForModelIndex( index );
        if ( record && !record->isFixed )
        {
            record->isSleeping = false;
        }
    }
    if ( index < static_cast<int>( m_sleepCounter.size() ) )
    {
        m_sleepCounter[index] = 0;
    }
    if ( index < static_cast<int>( m_underwaterSleepLocked.size() ) )
    {
        m_underwaterSleepLocked[index] = 0;
    }
    if ( index < static_cast<int>( m_sleepIslandVisualId.size() ) )
    {
        m_sleepIslandVisualId[index] = 0;
    }
    if ( dt > 0.0f && index < static_cast<int>( m_timeRemaining.size() ) )
    {
        m_timeRemaining[index] = dt;
    }
    if ( applyForces && wasSleeping && dt > TOLERANCE && bodyStore && worldForces && colliderStore )
    {
        (void)bodyStore->ApplyForces( *worldForces, *colliderStore, index, dt );
    }
    ForgetPersistentContactCacheForBody( index );

    return wasSleeping || hadCounter || hadSleepVisual || wasUnderwaterLocked;
}


// Why: sleep visual islands are persisted as model-order indices, but the fixed
// and sleep-state facts needed to wake them are already in PhysicsBodyStore.
void PhysicsWorld::WakeSleepVisualIsland( int bodyCount,
                                          const PhysicsBodyRecordList& bodyRecords,
                                          PhysicsBodyStore* bodyStore,
                                          int index,
                                          float dt,
                                          bool applyForces,
                                          const PhysicsWorldForces* worldForces,
                                          const ColliderStore* colliderStore )
{
    if ( index < 0 || index >= static_cast<int>( m_sleepState.size() ) )
    {
        return;
    }

    const int visualId = index < static_cast<int>( m_sleepIslandVisualId.size() ) ? m_sleepIslandVisualId[index] : 0;
    if ( visualId > 0 )
    {
        const int count = (std::min)( { static_cast<int>( m_sleepIslandVisualId.size() ),
                                        bodyCount,
                                        static_cast<int>( bodyRecords.size() ) } );
        for ( int i = 0; i < count; ++i )
        {
            if ( m_sleepIslandVisualId[i] == visualId )
            {
                WakeDynamicBodyState( bodyCount,
                                      bodyRecords,
                                      bodyStore,
                                      i,
                                      dt,
                                      applyForces,
                                      worldForces,
                                      colliderStore );
            }
        }
    }
    else
    {
        WakeDynamicBodyState( bodyCount, bodyRecords, bodyStore, index, dt, applyForces, worldForces, colliderStore );
    }
}


// Why: point-joint wake propagation is part of simulation correctness, so the
// store path preserves the existing island walk while avoiding a model-stream
// refresh inside the fixed step.
void PhysicsWorld::WakePointJointIsland( int bodyCount,
                                         const PhysicsBodyRecordList& bodyRecords,
                                         PhysicsBodyStore* bodyStore,
                                         int index,
                                         float dt,
                                         bool applyForces,
                                         const PhysicsWorldForces* worldForces,
                                         const ColliderStore* colliderStore )
{
    if ( bodyStore == nullptr )
    {
        return;
    }

    const int modelCount = (std::min)( { bodyCount, bodyStore->Count(), static_cast<int>( bodyRecords.size() ) } );
    if ( m_pointJointConstraints.empty() || index < 0 || index >= modelCount ||
         index >= static_cast<int>( m_sleepState.size() ) )
    {
        return;
    }

    m_sleepIslandParent.assign( modelCount, 0 );
    m_sleepIslandRank.assign( modelCount, 0 );
    m_sleepPointJointBody.assign( modelCount, 0 );
    for ( int i = 0; i < modelCount; ++i )
    {
        m_sleepIslandParent[i] = i;
    }

    DisjointSet sleepIslands( m_sleepIslandParent, m_sleepIslandRank, modelCount );

    for ( const PointJointConstraint& constraint : m_pointJointConstraints )
    {
        const int a = constraint.BodyAIndex( *bodyStore );
        const int b = constraint.BodyBIndex( *bodyStore );
        if ( a < 0 || b < 0 || a == b || a >= modelCount || b >= modelCount )
        {
            continue;
        }

        m_sleepPointJointBody[a] = 1;
        m_sleepPointJointBody[b] = 1;
        sleepIslands.Unite( a, b );
    }

    if ( m_sleepPointJointBody[index] == 0 )
    {
        return;
    }

    const int root = sleepIslands.Find( index );
    for ( int i = 0; i < modelCount; ++i )
    {
        if ( m_sleepPointJointBody[i] == 0 || sleepIslands.Find( i ) != root )
        {
            continue;
        }
        WakeDynamicBodyState( modelCount, bodyRecords, bodyStore, i, dt, applyForces, worldForces, colliderStore );
    }
}


// Why: explicit wake still needs to fan out through resting contacts. The store
// path uses body-record position/radius snapshots so solver side effects do not
// rebuild the model SoA cache.
void PhysicsWorld::WakeRestingContactIsland( int bodyCount,
                                             const PhysicsBodyRecordList& bodyRecords,
                                             PhysicsBodyStore* bodyStore,
                                             int index,
                                             float dt,
                                             bool applyForces,
                                             const PhysicsWorldForces* worldForces,
                                             const ColliderStore* colliderStore )
{
    const int modelCount = (std::min)( bodyCount, static_cast<int>( bodyRecords.size() ) );
    if ( index < 0 || index >= modelCount || index >= static_cast<int>( m_sleepState.size() ) )
    {
        return;
    }

    if ( modelCount > static_cast<int>( m_restingWakeVisitedScratch.capacity() ) ||
         modelCount > static_cast<int>( m_restingWakeQueueScratch.capacity() ) )
    {
        assert( false && "Physics resting-wake scratch capacity exceeded" );
        // Invariant: wake propagation is a bounded scratch walk over the body
        // rows. A larger model count means the world's pre-step reserve budget
        // no longer matches the scene being simulated.
        SB_FATAL( "Physics/PhysicsWorld", "Physics resting-wake scratch capacity exceeded" );
    }
    m_restingWakeVisitedScratch.assign( static_cast<size_t>( modelCount ), 0 );
    m_restingWakeQueueScratch.clear();
    m_restingWakeVisitedScratch[static_cast<size_t>( index )] = 1;
    m_restingWakeQueueScratch.push_back( index );

    auto hasPersistentContactEdge = [&]( int a, int b ) -> bool
    {
        for ( const PersistentContact& contact : m_persistentContacts )
        {
            if ( ( contact.bodyA == a && contact.bodyB == b ) || ( contact.bodyA == b && contact.bodyB == a ) )
            {
                return true;
            }
        }
        return false;
    };

    auto isLikelyRestingNeighbor = [&]( int a, int b ) -> bool
    {
        const PhysicsBodyRecord& recordA = bodyRecords[static_cast<size_t>( a )];
        const PhysicsBodyRecord& recordB = bodyRecords[static_cast<size_t>( b )];
        const float radiusA = (std::max)( 0.01f, recordA.boundingRadius );
        const float radiusB = (std::max)( 0.01f, recordB.boundingRadius );
        if ( recordB.position.y + radiusB + EXPLICIT_WAKE_VERTICAL_SLOP < recordA.position.y - radiusA )
        {
            return false;
        }

        const float range = radiusA + radiusB + EXPLICIT_WAKE_NEIGHBOR_SLOP;
        const Vector3 delta = recordB.position - recordA.position;
        return delta * delta <= range * range;
    };

    for ( size_t cursor = 0; cursor < m_restingWakeQueueScratch.size(); ++cursor )
    {
        const int current = m_restingWakeQueueScratch[cursor];
        for ( int candidate = 0; candidate < modelCount; ++candidate )
        {
            if ( m_restingWakeVisitedScratch[static_cast<size_t>( candidate )] ||
                 candidate >= static_cast<int>( m_sleepState.size() ) || m_sleepState[candidate] == 0 )
            {
                continue;
            }
            if ( bodyRecords[static_cast<size_t>( candidate )].isFixed )
            {
                continue;
            }
            if ( IsUnderwaterSleepLocked( modelCount, candidate ) )
            {
                continue;
            }
            if ( !hasPersistentContactEdge( current, candidate ) && !isLikelyRestingNeighbor( current, candidate ) )
            {
                continue;
            }

            m_restingWakeVisitedScratch[static_cast<size_t>( candidate )] = 1;
            m_restingWakeQueueScratch.push_back( candidate );
            WakeDynamicBodyState( modelCount,
                                  bodyRecords,
                                  bodyStore,
                                  candidate,
                                  dt,
                                  applyForces,
                                  worldForces,
                                  colliderStore );
        }
    }
}


bool PhysicsWorld::IsPointJointPair( const PhysicsBodyStore& bodyStore, int bodyA, int bodyB ) const
{
    if ( bodyA < 0 || bodyB < 0 || bodyA == bodyB )
    {
        return false;
    }
    if ( bodyA > bodyB )
    {
        std::swap( bodyA, bodyB );
    }
    for ( const PointJointConstraint& constraint : m_pointJointConstraints )
    {
        int jointA = constraint.BodyAIndex( bodyStore );
        int jointB = constraint.BodyBIndex( bodyStore );
        if ( jointA < 0 || jointB < 0 )
        {
            continue;
        }
        if ( jointA > jointB )
        {
            std::swap( jointA, jointB );
        }
        if ( jointA == bodyA && jointB == bodyB )
        {
            return true;
        }
    }
    return false;
}


void PhysicsWorld::WakePointJointConnectedBodies( PhysicsBodyStore& bodyStore,
                                                  const ColliderStore& colliderStore,
                                                  const PhysicsWorldForces& worldForces,
                                                  float dt )
{
    if ( m_pointJointConstraints.empty() || static_cast<int>( m_sleepState.size() ) <= 0 )
    {
        return;
    }

    const auto& bodyRecords = bodyStore.Records();
    const int modelCount = (std::min)( bodyStore.Count(), static_cast<int>( bodyRecords.size() ) );
    m_sleepIslandParent.assign( modelCount, 0 );
    m_sleepIslandRank.assign( modelCount, 0 );
    m_sleepPointJointBody.assign( modelCount, 0 );
    m_sleepIslandHasAwake.assign( modelCount, 0 );
    m_sleepIslandCanSleep.assign( modelCount, 0 );
    for ( int i = 0; i < modelCount; ++i )
    {
        m_sleepIslandParent[i] = i;
    }

    DisjointSet sleepIslands( m_sleepIslandParent, m_sleepIslandRank, modelCount );

    for ( const PointJointConstraint& constraint : m_pointJointConstraints )
    {
        // Concept: point-joint edges define the constrained component. If one
        // piece is awake, any sleeping neighbors must wake before the solver
        // applies joint impulses against them as static anchors.
        const int a = constraint.BodyAIndex( bodyStore );
        const int b = constraint.BodyBIndex( bodyStore );
        if ( a < 0 || b < 0 || a == b || a >= modelCount || b >= modelCount ||
             a >= static_cast<int>( m_sleepState.size() ) || b >= static_cast<int>( m_sleepState.size() ) )
        {
            continue;
        }

        m_sleepPointJointBody[a] = 1;
        m_sleepPointJointBody[b] = 1;
        sleepIslands.Unite( a, b );
    }

    for ( int i = 0; i < modelCount; ++i )
    {
        if ( m_sleepPointJointBody[i] == 0 || bodyRecords[static_cast<size_t>( i )].isFixed )
        {
            continue;
        }

        const int root = sleepIslands.Find( i );
        if ( i < static_cast<int>( m_sleepState.size() ) && m_sleepState[i] != 0 )
        {
            m_sleepIslandCanSleep[root] = 1;
        }
        else
        {
            m_sleepIslandHasAwake[root] = 1;
        }
    }

    for ( int i = 0; i < modelCount; ++i )
    {
        if ( m_sleepPointJointBody[i] == 0 || bodyRecords[static_cast<size_t>( i )].isFixed ||
             i >= static_cast<int>( m_sleepState.size() ) || m_sleepState[i] == 0 )
        {
            continue;
        }

        const int root = sleepIslands.Find( i );
        if ( m_sleepIslandHasAwake[root] != 0 && m_sleepIslandCanSleep[root] != 0 )
        {
            WakeDynamicBodyState( modelCount, bodyRecords, &bodyStore, i, dt, true, &worldForces, &colliderStore );
        }
    }
}


void PhysicsWorld::RunSolverPhysics( PhysicsBodyStore& bodyStore,
                                     const ColliderStore& colliderStore,
                                     float dt,
                                     const Basics::EngineConfig& config,
                                     const PhysicsWorldForces& worldForces,
                                     Threading::WorkerPool& workerPool,
                                     const char* const* diagnosticNames,
                                     int diagnosticNameCount )
{
    auto& bodyRecords = bodyStore.MutableRecords();
    const auto& colliderRecords = colliderStore.Records();
    const int modelCount = (std::min)( { bodyStore.Count(),
                                         static_cast<int>( bodyRecords.size() ),
                                         static_cast<int>( colliderRecords.size() ) } );
    auto bodyIsFixed = [&]( int index ) -> bool { return bodyRecords[static_cast<size_t>( index )].isFixed; };
    auto bodyPosition = [&]( int index ) -> const Vector3&
    { return bodyRecords[static_cast<size_t>( index )].position; };
    auto bodyRadius = [&]( int index ) -> float
    { return colliderRecords[static_cast<size_t>( index )].boundingRadius; };

    // Sleep thresholds are config-backed because they directly trade CPU cost
    // against visible settling behavior. Higher thresholds keep bodies awake
    // longer, which is useful while validating the solver but expensive in
    // sleeping-heavy scenes. Lower thresholds save broadphase/narrowphase work
    // sooner, but if set too aggressively they can freeze objects before the
    // persistent contact solver has converged to a stable support impulse.
    //
    // The counter storage is still uint8_t, so physics_sleep_frames is clamped
    // to 1..255 here. Widening that storage is a separate data-layout change and
    // should be measured before doing it in a hot per-body array.
    const float sleepLinear = (std::max)( 0.0f, config.physicsSleepLinearSpeed );
    const float sleepAngular = (std::max)( 0.0f, config.physicsSleepAngularSpeed );
    const float SLEEP_LINEAR_SQ = sleepLinear * sleepLinear;
    const float SLEEP_ANGULAR_SQ = sleepAngular * sleepAngular;
    const uint8_t SLEEP_FRAMES = static_cast<uint8_t>( (std::max)( 1, (std::min)( config.physicsSleepFrames, 255 ) ) );

    EnsureUnderwaterSleepLockBuffer( modelCount );
    for ( int x = 0; x < modelCount; ++x )
    {
        if ( m_sleepState[x] )
        {
            LockUnderwaterSleeperIfReady( worldForces, bodyStore, colliderStore, x );
        }
    }

    // Sleeping bodies keep cached state until a contact or scene change wakes
    // them, so force integration only runs for awake rows.
    PROFILE_BEGIN( "Frame/Physics/ApplyForces" );
    auto applyForcesAt = [&]( int x )
    {
        if ( bodyIsFixed( x ) )
        {
            return;
        }
        if ( m_sleepState[x] )
        {
            m_timeRemaining[x] = 0.0f;
            return;
        }
        (void)bodyStore.ApplyForces( worldForces, colliderStore, x, dt );
    };

    if ( config.physicsParallel && config.physicsParallelApplyForces )
    {
        workerPool.ParallelForNoAlloc( 0,
                                       modelCount,
                                       applyForcesAt,
                                       PHYSICS_PARALLEL_MIN_BODIES,
                                       "Frame/Physics/ApplyForces/WorkerBodies",
                                       PHYSICS_APPLY_FORCES_WORKER_HASH );
    }
    else
    {
        for ( int x = 0; x < modelCount; ++x )
        {
            applyForcesAt( x );
        }
    }
    PROFILE_END( "Frame/Physics/ApplyForces" );

    ApplyTornadoField( bodyStore, colliderStore, worldForces, dt, config, workerPool );

    // Broadphase: build spatial grid from all object positions (include sleeping for wake detection)
    PROFILE_BEGIN( "Frame/Physics/Broadphase" );
    std::vector<std::pair<int, int>>& candidatePairs = m_candidatePairs;
    const float contactSkin = (std::max)( 0.0f, config.contactEpsilon );
    // Why: sharing one spatial-grid cell is only a locality hint. Dense wall
    // scenes can put many small boxes in one cell, so reject pairs whose swept
    // bounding spheres never approach before appending them to the hot vector.
    //
    // Invariant: this is still a broadphase test. It may keep false positives,
    // but it must not reject a pair whose exact shapes could touch during this
    // fixed tick; the relative-motion segment covers CCD and wakeup cases.
    struct BroadphaseCandidateFilterContext
    {
        const PhysicsBodyRecordList& bodyRecords;
        const ColliderRecordList& colliderRecords;
        int modelCount;
        float dt;
        float contactSkin;
    };
    BroadphaseCandidateFilterContext broadphaseCandidateFilterContext{
        bodyRecords,
        colliderRecords,
        modelCount,
        dt,
        contactSkin,
    };
    const auto broadphaseCandidateCanTouch = []( const void* userData, int a, int b ) -> bool
    {
        if ( userData == nullptr )
        {
            return true;
        }

        const BroadphaseCandidateFilterContext& context =
            *static_cast<const BroadphaseCandidateFilterContext*>( userData );
        if ( a < 0 || b < 0 || a >= context.modelCount || b >= context.modelCount )
        {
            return false;
        }

        const float radiusA = context.colliderRecords[static_cast<size_t>( a )].boundingRadius;
        const float radiusB = context.colliderRecords[static_cast<size_t>( b )].boundingRadius;
        if ( !std::isfinite( radiusA ) || !std::isfinite( radiusB ) || radiusA < 0.0f || radiusB < 0.0f )
        {
            return true;
        }

        const Vector3 relativeStart = context.bodyRecords[static_cast<size_t>( a )].position -
                                      context.bodyRecords[static_cast<size_t>( b )].position;
        const Vector3 relativeDisplacement = ( context.bodyRecords[static_cast<size_t>( a )].linearVelocity -
                                               context.bodyRecords[static_cast<size_t>( b )].linearVelocity ) *
                                             context.dt;
        const float contactRadius = radiusA + radiusB + context.contactSkin;
        const float contactRadiusSq = contactRadius * contactRadius;
        const float relativeLengthSq = Vector::VectorMagSquared( relativeDisplacement );
        if ( relativeLengthSq <= TOLERANCE * TOLERANCE )
        {
            return Vector::VectorMagSquared( relativeStart ) <= contactRadiusSq;
        }

        float t = -( relativeStart * relativeDisplacement ) / relativeLengthSq;
        t = (std::max)( 0.0f, (std::min)( 1.0f, t ) );
        const Vector3 closestRelative = relativeStart + relativeDisplacement * t;
        return Vector::VectorMagSquared( closestRelative ) <= contactRadiusSq;
    };
    {
        PROFILE_SCOPED( "Frame/Physics/Broadphase/GridBuild" );
        float largestBroadphaseRadius = 0.0f;
        for ( int i = 0; i < modelCount; ++i )
        {
            const float radius = bodyRadius( i );
            if ( std::isfinite( radius ) && radius > largestBroadphaseRadius )
            {
                largestBroadphaseRadius = radius;
            }
        }

        // Why: a fixed 24m cell made the 200-brick wall share huge buckets,
        // producing thousands of false candidate pairs. Cell size follows the
        // largest active broadphase primitive so ordinary bodies span only a few
        // cells while the config value remains an upper bound for legacy scenes.
        // Invariant: the choice uses only deterministic store/collider/config data,
        // so byte-exact physics baselines do not depend on allocator or hash state.
        const float configuredCell = (std::max)( BROADPHASE_MIN_CELL_SIZE, config.broadphaseCell );
        const float sceneCell =
            (std::max)( BROADPHASE_MIN_CELL_SIZE, ( largestBroadphaseRadius + contactSkin ) * 2.0f );
        m_spatialGrid.SetCellSize( (std::min)( configuredCell, sceneCell ) );
        m_spatialGrid.Clear();
        m_collisionCellKeys.clear();
        for ( int i = 0; i < modelCount; ++i )
        {
            const float radius = bodyRadius( i ) + contactSkin;
            const Vector3 displacement = bodyRecords[static_cast<size_t>( i )].linearVelocity * dt;
            const float displacementSq = Vector::VectorMagSquared( displacement );
            if ( !bodyIsFixed( i ) && displacementSq > radius * radius )
            {
                m_spatialGrid.InsertSwept( i, bodyPosition( i ), displacement, radius );
            }
            else
            {
                m_spatialGrid.Insert( i, bodyPosition( i ), radius );
            }
        }
        m_spatialGrid.GetCandidatePairs( candidatePairs,
                                         broadphaseCandidateCanTouch,
                                         &broadphaseCandidateFilterContext );
    }

    auto appendCandidatePairIfMissing = [&]( int a, int b )
    {
        if ( a == b || a < 0 || b < 0 || a >= modelCount || b >= modelCount )
        {
            return;
        }

        if ( a > b )
        {
            std::swap( a, b );
        }

        if ( !broadphaseCandidateCanTouch( &broadphaseCandidateFilterContext, a, b ) )
        {
            return;
        }

        for ( const std::pair<int, int>& pair : candidatePairs )
        {
            if ( pair.first == a && pair.second == b )
            {
                return;
            }
        }

        candidatePairs.emplace_back( a, b );
    };

    auto isFastSmallSweepBody = [&]( int index ) -> bool
    {
        if ( bodyIsFixed( index ) )
        {
            return false;
        }

        const float radius = bodyRadius( index );
        if ( radius > PHYSICS_FAST_SWEEP_MAX_RADIUS )
        {
            return false;
        }

        const Vector3 displacement = bodyRecords[static_cast<size_t>( index )].linearVelocity * dt;
        const float displacementSq = Vector::VectorMagSquared( displacement );
        const float minSweepDistance = (std::max)( radius * 2.0f, PHYSICS_FAST_SWEEP_MIN_DISTANCE );
        return displacementSq > minSweepDistance * minSweepDistance;
    };

    auto sweptSegmentTouchesExpandedBody = [&]( int movingIndex, int targetIndex ) -> bool
    {
        const Vector3 relativeStart = bodyPosition( movingIndex ) - bodyPosition( targetIndex );
        const Vector3 relativeDisplacement = ( bodyRecords[static_cast<size_t>( movingIndex )].linearVelocity -
                                               bodyRecords[static_cast<size_t>( targetIndex )].linearVelocity ) *
                                             dt;
        const float relativeLengthSq = Vector::VectorMagSquared( relativeDisplacement );
        if ( relativeLengthSq <= TOLERANCE * TOLERANCE )
        {
            return false;
        }

        float t = -( relativeStart * relativeDisplacement ) / relativeLengthSq;
        t = (std::max)( 0.0f, (std::min)( 1.0f, t ) );
        const Vector3 closestRelative = relativeStart + relativeDisplacement * t;
        const float expandedRadius = bodyRadius( movingIndex ) + bodyRadius( targetIndex ) + config.contactEpsilon +
                                     PHYSICS_FAST_SWEEP_PAIR_SLOP;
        return Vector::VectorMagSquared( closestRelative ) <= expandedRadius * expandedRadius;
    };

    // Tiny high-speed projectiles should not depend solely on cell overlap.
    // If the hash grid samples or capacity ever miss their path, this conservative
    // segment test still feeds the exact pair to narrowphase CCD.
    {
        PROFILE_SCOPED( "Frame/Physics/Broadphase/FastSmallSweepAugment" );
        for ( int movingIndex = 0; movingIndex < modelCount; ++movingIndex )
        {
            if ( !isFastSmallSweepBody( movingIndex ) )
            {
                continue;
            }

            for ( int targetIndex = 0; targetIndex < modelCount; ++targetIndex )
            {
                if ( movingIndex == targetIndex )
                {
                    continue;
                }
                if ( sweptSegmentTouchesExpandedBody( movingIndex, targetIndex ) )
                {
                    appendCandidatePairIfMissing( movingIndex, targetIndex );
                }
            }
        }
    }

    {
        PROFILE_SCOPED( "Frame/Physics/Broadphase/PruneFixedPairs" );
        candidatePairs.erase( std::remove_if( candidatePairs.begin(),
                                              candidatePairs.end(),
                                              [&]( const std::pair<int, int>& pair )
                                              {
                                                  const int a = pair.first;
                                                  const int b = pair.second;
                                                  return a >= 0 && b >= 0 && a < modelCount && b < modelCount &&
                                                         bodyIsFixed( a ) && bodyIsFixed( b );
                                              } ),
                              candidatePairs.end() );
    }

    if ( !m_pointJointConstraints.empty() )
    {
        PROFILE_SCOPED( "Frame/Physics/Broadphase/PruneJointPairs" );
        candidatePairs.erase( std::remove_if( candidatePairs.begin(),
                                              candidatePairs.end(),
                                              [&]( const std::pair<int, int>& pair )
                                              { return IsPointJointPair( bodyStore, pair.first, pair.second ); } ),
                              candidatePairs.end() );
    }

    {
        PROFILE_SCOPED( "Frame/Physics/Broadphase/RecordCandidates" );
        for ( const auto& pair : candidatePairs )
        {
            // Why: this pass only mirrors pairs into the capped diagnostics trace.
            // Once the trace is full, later iterations cannot affect simulation
            // state or recorded diagnostics.
            if ( !CanRecordPhysicsPipelineStage() )
            {
                break;
            }

            if ( pair.first < 0 || pair.second < 0 || pair.first >= modelCount || pair.second >= modelCount )
            {
                continue;
            }

            Physics::PhysicsPipelineRecord record;
            record.stage = Physics::PhysicsPipelineStage::BroadphaseCandidate;
            record.bodyA = pair.first;
            record.bodyB = pair.second;
            record.point = ( bodyRecords[static_cast<size_t>( pair.first )].position +
                             bodyRecords[static_cast<size_t>( pair.second )].position ) *
                           0.5f;
            Vector3 delta = bodyRecords[static_cast<size_t>( pair.second )].position -
                            bodyRecords[static_cast<size_t>( pair.first )].position;
            float deltaMag = Vector::VectorMag( delta );
            record.normal = deltaMag > TOLERANCE ? delta / deltaMag : Vector3( 0.0f, 1.0f, 0.0f );
            record.scalarA = static_cast<float>( candidatePairs.size() );
            RecordPhysicsPipelineStage( record );
        }
    }
    {
        PROFILE_SCOPED( "Frame/Physics/Broadphase/PruneSleepPairs" );
        // The spatial grid is still populated with sleeping bodies because an
        // awake body must be able to find and wake a sleeping neighbor. What we
        // do not need is sleep/sleep work: two sleeping dynamic bodies cannot
        // generate a new wake event because neither has wake energy, and their
        // previous support relationship is already represented by sleep state
        // and island visual ids. Pruning these pairs immediately keeps both the
        // swept narrowphase and the persistent contact manifold builder from
        // re-checking pairs that would only be skipped later.
        //
        // This is deliberately narrower than a separate awake/sleeping grid.
        // The full partition is still a valid future optimization, but this
        // single pass removes the common dead work without changing pair
        // generation order for any pair that can affect simulation behavior.
        candidatePairs.erase(
            std::remove_if( candidatePairs.begin(),
                            candidatePairs.end(),
                            [&]( const std::pair<int, int>& pair )
                            {
                                const int a = pair.first;
                                const int b = pair.second;
                                const bool prune = a >= 0 && b >= 0 && a < static_cast<int>( m_sleepState.size() ) &&
                                                   b < static_cast<int>( m_sleepState.size() ) &&
                                                   m_sleepState[a] != 0 && m_sleepState[b] != 0;
                                if ( prune && CanRecordPhysicsPipelineStage() )
                                {
                                    Physics::PhysicsPipelineRecord record;
                                    record.stage = Physics::PhysicsPipelineStage::SleepPrunedPair;
                                    record.bodyA = a;
                                    record.bodyB = b;
                                    record.point = ( bodyRecords[static_cast<size_t>( a )].position +
                                                     bodyRecords[static_cast<size_t>( b )].position ) *
                                                   0.5f;
                                    record.scalarA = 1.0f;
                                    RecordPhysicsPipelineStage( record );
                                }
                                return prune;
                            } ),
            candidatePairs.end() );
    }
    PROFILE_END( "Frame/Physics/Broadphase" );

    auto hasWakeEnergy = [&]( int awakeIndex ) -> bool
    {
        const Vector3& vel = bodyRecords[static_cast<size_t>( awakeIndex )].linearVelocity;
        const Vector3& omega = bodyRecords[static_cast<size_t>( awakeIndex )].angularVelocity;
        float speedSq = vel.x * vel.x + vel.y * vel.y + vel.z * vel.z;
        float omegaSq = omega.x * omega.x + omega.y * omega.y + omega.z * omega.z;
        return speedSq >= SLEEP_LINEAR_SQ || omegaSq >= SLEEP_ANGULAR_SQ;
    };

    auto wakeSleepingModel = [&]( int sleepingIndex )
    {
        // Waking re-enters the body into this frame rather than waiting for the
        // next tick. Applying forces immediately keeps gravity and other forces
        // consistent with an awake body that was never asleep.
        if ( sleepingIndex < 0 || sleepingIndex >= modelCount || bodyIsFixed( sleepingIndex ) ||
             !m_sleepState[sleepingIndex] || IsUnderwaterSleepLocked( modelCount, sleepingIndex ) )
        {
            return;
        }

        m_sleepState[sleepingIndex] = 0;
        m_sleepCounter[sleepingIndex] = 0;
        m_sleepIslandVisualId[sleepingIndex] = 0;
        m_timeRemaining[sleepingIndex] = dt;
        bodyRecords[static_cast<size_t>( sleepingIndex )].isSleeping = false;
        (void)bodyStore.ApplyForces( worldForces, colliderStore, sleepingIndex, dt );
    };

    auto contactBodyViewAtTime = [&]( int index, float time ) -> ObjectContactBodyView
    {
        const PhysicsBodyRecord& record = bodyRecords[static_cast<size_t>( index )];
        ObjectContactBodyView body;
        body.position = record.position + record.linearVelocity * time;
        body.orientation = record.orientation;
        return body;
    };

    auto terrainContactBodyViewForIndex = [&]( int index ) -> TerrainContactBodyView
    {
        const PhysicsBodyRecord& record = bodyRecords[static_cast<size_t>( index )];
        TerrainContactBodyView body;
        body.position = record.position;
        body.orientation = record.orientation;
        body.linearVelocity = record.linearVelocity;
        body.terrain = record.terrain;
        body.boundingRadius = record.boundingRadius;
        body.contactEpsilon = record.contactEpsilon;
        body.terrainContactThreshold = config.terrainContactThreshold;
        body.restitutionThreshold = config.contactRestitutionThreshold;
        body.isFixed = record.isFixed;
        return body;
    };

    auto hasPersistentWakeContact = [&]( int awakeIndex, int sleepingIndex ) -> bool
    {
        PROFILE_SCOPED( "Frame/Physics/Narrowphase/WakePersistentContact" );

        // A swept test can miss a sleeper that is already overlapping after an
        // awake body's correction step. This fresh manifold test catches that
        // persistent contact so the sleeper cannot remain frozen inside the
        // awake body until a later frame happens to generate a swept hit.
        if ( awakeIndex < 0 || sleepingIndex < 0 || awakeIndex >= static_cast<int>( colliderRecords.size() ) ||
             sleepingIndex >= static_cast<int>( colliderRecords.size() ) )
        {
            return false;
        }

        ObjectContactManifold manifold;
        return BuildObjectContactManifold( contactBodyViewAtTime( awakeIndex, 0.0f ),
                                           colliderRecords[static_cast<size_t>( awakeIndex )].shape,
                                           contactBodyViewAtTime( sleepingIndex, 0.0f ),
                                           colliderRecords[static_cast<size_t>( sleepingIndex )].shape,
                                           awakeIndex,
                                           sleepingIndex,
                                           config.contactEpsilon,
                                           manifold );
    };

    auto hasObjectContactAtTime = [&]( int a, int b, float time ) -> bool
    {
        PROFILE_SCOPED( "Frame/Physics/Narrowphase/ExactContactAtTime" );

        if ( a < 0 || b < 0 || a >= static_cast<int>( colliderRecords.size() ) ||
             b >= static_cast<int>( colliderRecords.size() ) )
        {
            return false;
        }

        // Query at a candidate time without mutating PhysicsBodyStore or the
        // owner-side presentation rows. CCD refinement only needs temporary pose
        // views plus the collider shape snapshots.
        ObjectContactManifold manifold;
        return BuildObjectContactManifold( contactBodyViewAtTime( a, time ),
                                           colliderRecords[static_cast<size_t>( a )].shape,
                                           contactBodyViewAtTime( b, time ),
                                           colliderRecords[static_cast<size_t>( b )].shape,
                                           a,
                                           b,
                                           config.contactEpsilon,
                                           manifold );
    };

    auto refineObjectSweepContactTime = [&]( int a, int b, float coarseTime, float availableTime ) -> float
    {
        PROFILE_SCOPED( "Frame/Physics/Narrowphase/RefineContactTime" );

        // The broad sweep can give a conservative first time. Refinement walks
        // forward until exact manifold contact appears, then binary-searches the
        // edge of that contact window. This keeps fast objects from advancing
        // too far into each other before persistent rows solve the response.
        if ( coarseTime <= 0.0f || coarseTime >= availableTime )
        {
            return coarseTime;
        }

        if ( hasObjectContactAtTime( a, b, coarseTime ) )
        {
            return coarseTime;
        }

        float lo = coarseTime;
        float hi = coarseTime;
        bool foundContactWindow = false;
        for ( int step = 1; step <= 48; ++step )
        {
            const float t = coarseTime + ( availableTime - coarseTime ) * ( static_cast<float>( step ) / 48.0f );
            if ( hasObjectContactAtTime( a, b, t ) )
            {
                hi = t;
                foundContactWindow = true;
                break;
            }
            lo = t;
        }

        if ( !foundContactWindow )
        {
            return coarseTime;
        }

        for ( int iter = 0; iter < 12; ++iter )
        {
            const float mid = ( lo + hi ) * 0.5f;
            if ( hasObjectContactAtTime( a, b, mid ) )
            {
                hi = mid;
            }
            else
            {
                lo = mid;
            }
        }
        return hi;
    };

    auto sweepObjectPair = [&]( int a, int b, float availableTime ) -> ObjectContactSweepResult
    {
        PROFILE_SCOPED( "Frame/Physics/Narrowphase/SweepPairs" );
        ObjectContactSweepResult result;
        result.collisionTime = availableTime;
        if ( a < 0 || b < 0 || a >= static_cast<int>( colliderRecords.size() ) ||
             b >= static_cast<int>( colliderRecords.size() ) )
        {
            return result;
        }

        const PhysicsBodyRecord& recordA = bodyRecords[static_cast<size_t>( a )];
        const PhysicsBodyRecord& recordB = bodyRecords[static_cast<size_t>( b )];
        return SweepObjectContact( contactBodyViewAtTime( a, 0.0f ),
                                   colliderRecords[static_cast<size_t>( a )].shape,
                                   recordA.linearVelocity,
                                   contactBodyViewAtTime( b, 0.0f ),
                                   colliderRecords[static_cast<size_t>( b )].shape,
                                   recordB.linearVelocity,
                                   availableTime );
    };

    auto objectPairHasPersistentContactCache = [&]( int a, int b ) -> bool
    {
        constexpr uint64_t BODY_MASK = 0x7fffull;
        const int lo = ( a < b ) ? a : b;
        const int hi = ( a < b ) ? b : a;
        // Invariant: this mirrors the object/object prefix of the persistent
        // solver cache key. Feature ids occupy the low 32 bits, so masking those
        // away answers whether any cached contact row existed for this pair.
        const uint64_t pairPrefix = ( ( static_cast<uint64_t>( static_cast<uint32_t>( lo ) ) & BODY_MASK ) << 47 ) |
                                    ( ( static_cast<uint64_t>( static_cast<uint32_t>( hi ) ) & BODY_MASK ) << 32 );
        const int64_t firstKey = static_cast<int64_t>( pairPrefix );
        auto cachedIt = std::lower_bound( m_persistentContactCache.begin(),
                                          m_persistentContactCache.end(),
                                          firstKey,
                                          []( const PersistentContactCacheEntry& entry, int64_t lookupKey )
                                          { return entry.key < lookupKey; } );
        return cachedIt != m_persistentContactCache.end() &&
               ( static_cast<uint64_t>( cachedIt->key ) & 0xffffffff00000000ull ) == pairPrefix;
    };

    auto objectPairNeedsSweptCcd = [&]( int a, int b, float availableTime ) -> bool
    {
        if ( availableTime <= TOLERANCE )
        {
            return false;
        }

        if ( !objectPairHasPersistentContactCache( a, b ) )
        {
            return true;
        }

        const float radiusA = bodyRadius( a );
        const float radiusB = bodyRadius( b );
        if ( !std::isfinite( radiusA ) || !std::isfinite( radiusB ) || radiusA <= TOLERANCE || radiusB <= TOLERANCE )
        {
            return true;
        }

        const PhysicsBodyRecord& bodyA = bodyRecords[static_cast<size_t>( a )];
        const PhysicsBodyRecord& bodyB = bodyRecords[static_cast<size_t>( b )];
        const Vector3 relativeLinearDisplacement = ( bodyA.linearVelocity - bodyB.linearVelocity ) * availableTime;
        const float linearTravel = Vector::VectorMag( relativeLinearDisplacement );
        const float angularTravel = ( Vector::VectorMag( bodyA.angularVelocity ) * radiusA +
                                      Vector::VectorMag( bodyB.angularVelocity ) * radiusB ) *
                                    availableTime;
        const float sweptTravel = linearTravel + angularTravel;
        const float smallerRadius = (std::min)( radiusA, radiusB );
        const float ccdThreshold = (std::max)( contactSkin * PHYSICS_OBJECT_CCD_SKIN_SCALE,
                                               smallerRadius * PHYSICS_OBJECT_CCD_RADIUS_FRACTION );

        // Why: only already-persistent pairs may bypass the swept front-end. New
        // contacts keep their old time-of-impact path; settled contacts rely on
        // persistent manifolds unless motion is large enough to tunnel.
        return sweptTravel > ccdThreshold;
    };

    // Object/object CCD front-end: wake sleepers and advance swept hits to a
    // contact candidate, but leave velocity response to the persistent rows.
    PROFILE_BEGIN( "Frame/Physics/Narrowphase" );
    float invCellSize = 1.0f / m_spatialGrid.GetCellSize();
    const int candidatePairCount = static_cast<int>( candidatePairs.size() );

    auto recordObjectNarrowphaseEvent = []( ObjectNarrowphaseEvent& event,
                                            ObjectNarrowphaseEventKind kind,
                                            const Physics::PhysicsPipelineRecord& record )
    {
        event.kind = kind;
        event.pipelineRecord = record;
        event.hasPipelineRecord = 1;
    };

    auto emitObjectCollisionTimeEvent =
        []( ObjectNarrowphaseEvent& event, int bodyA, int bodyB, float collisionTime, float availableTime )
    {
        event.emitCollisionTime = 1;
        event.collisionTimeBodyA = bodyA;
        event.collisionTimeBodyB = bodyB;
        event.collisionTime = collisionTime;
        event.availableTime = availableTime;
    };

    auto markObjectVisualEvent = []( ObjectNarrowphaseEvent& event, int bodyA, int bodyB )
    {
        event.markVisualContact = 1;
        event.visualBodyA = bodyA;
        event.visualBodyB = bodyB;
    };

    auto writeObjectCollisionCellEvent = [&]( ObjectNarrowphaseEvent& event, int bodyA, int bodyB )
    {
        const Vector3 midpoint = ( bodyRecords[static_cast<size_t>( bodyA )].position +
                                   bodyRecords[static_cast<size_t>( bodyB )].position ) *
                                 0.5f;
        const int16_t cx = static_cast<int16_t>( floorf( midpoint.x * invCellSize ) );
        const int16_t cy = static_cast<int16_t>( floorf( midpoint.y * invCellSize ) );
        const int16_t cz = static_cast<int16_t>( floorf( midpoint.z * invCellSize ) );
        event.collisionCellKey =
            ( int64_t( cx ) * 73856093 ) ^ ( int64_t( cy ) * 19349663 ) ^ ( int64_t( cz ) * 83492791 );
        event.hasCollisionCellKey = 1;
    };

    auto commitObjectNarrowphaseEvent = [&]( const ObjectNarrowphaseEvent& event )
    {
        if ( event.hasPipelineRecord )
        {
            RecordPhysicsPipelineStage( event.pipelineRecord );
        }
        if ( event.emitCollisionTime )
        {
            EmitPhysicsCollisionTime( diagnosticNames,
                                      diagnosticNameCount,
                                      "object",
                                      event.collisionTimeBodyA,
                                      event.collisionTimeBodyB,
                                      event.collisionTime,
                                      event.availableTime );
        }
        if ( event.markVisualContact )
        {
            MarkCollisionVisualContact( event.visualBodyA );
            MarkCollisionVisualContact( event.visualBodyB );
        }
        if ( event.hasCollisionCellKey )
        {
            if ( m_collisionCellKeys.size() >= m_collisionCellKeys.capacity() )
            {
                assert( false && "Physics collision-cell key capacity exceeded" );
                // Invariant: collision-cell diagnostics share the fixed
                // narrowphase event budget. Overflow means the pass can no
                // longer record the same deterministic evidence each run.
                SB_FATAL( "Physics/PhysicsWorld", "Physics collision-cell key capacity exceeded" );
            }
            m_collisionCellKeys.push_back( event.collisionCellKey );
        }
    };

    auto processObjectNarrowphasePair = [&]( int pairIndex, ObjectNarrowphaseEvent& event )
    {
        const auto& cp = candidatePairs[static_cast<size_t>( pairIndex )];
        const int x = cp.first;
        const int y = cp.second;

        // Wake a sleeping object only after an energetic awake neighbor proves
        // an actual swept hit or persistent overlap. Underwater-locked sleepers
        // still receive the swept hit timing, but remain static solver anchors.
        if ( m_sleepState[x] || m_sleepState[y] )
        {
            // Quiet awake bodies cannot wake sleepers just by sharing a broadphase cell.
            if ( m_sleepState[x] && !m_sleepState[y] )
            {
                const bool sleepingLocked = IsUnderwaterSleepLocked( modelCount, x );
                if ( !hasWakeEnergy( y ) )
                {
                    return;
                }
                // Swept impact wakes immediately when time remains; persistent
                // overlap wakes too so sleepers cannot stay frozen after a hit.
                bool wokeBySweptImpact = false;
                if ( m_timeRemaining[y] > 0.0f && objectPairNeedsSweptCcd( y, x, m_timeRemaining[y] ) )
                {
                    ObjectContactSweepResult sweep = sweepObjectPair( y, x, m_timeRemaining[y] );
                    if ( sweep.hit )
                    {
                        const float availableTime = m_timeRemaining[y];
                        float colTime = refineObjectSweepContactTime( y, x, sweep.collisionTime, availableTime );
                        Physics::PhysicsPipelineRecord record;
                        record.stage = Physics::PhysicsPipelineStage::SweptObjectHit;
                        record.bodyA = y;
                        record.bodyB = x;
                        record.point = ( bodyRecords[static_cast<size_t>( y )].position +
                                         bodyRecords[static_cast<size_t>( x )].position ) *
                                       0.5f;
                        record.scalarA = colTime;
                        record.scalarB = availableTime;
                        recordObjectNarrowphaseEvent( event, ObjectNarrowphaseEventKind::SweptObjectHit, record );
                        emitObjectCollisionTimeEvent( event, y, x, colTime, availableTime );

                        (void)bodyStore.IntegrateBodyPose( colliderStore, y, colTime );
                        m_timeRemaining[y] = (std::max)( 0.0f, m_timeRemaining[y] - colTime );
                        if ( !sleepingLocked )
                        {
                            wakeSleepingModel( x );
                        }
                        wokeBySweptImpact = true;
                        markObjectVisualEvent( event, x, y );
                        writeObjectCollisionCellEvent( event, x, y );
                    }
                }
                if ( !wokeBySweptImpact && hasPersistentWakeContact( y, x ) )
                {
                    Physics::PhysicsPipelineRecord record;
                    record.stage = Physics::PhysicsPipelineStage::WakeDecision;
                    record.bodyA = y;
                    record.bodyB = x;
                    record.point = ( bodyRecords[static_cast<size_t>( y )].position +
                                     bodyRecords[static_cast<size_t>( x )].position ) *
                                   0.5f;
                    record.scalarA = sleepingLocked ? 0.0f : 1.0f;
                    recordObjectNarrowphaseEvent( event, ObjectNarrowphaseEventKind::WakeDecision, record );

                    if ( !sleepingLocked )
                    {
                        wakeSleepingModel( x );
                    }
                    markObjectVisualEvent( event, x, y );
                    writeObjectCollisionCellEvent( event, x, y );
                }
                return;
            }
            else if ( m_sleepState[y] && !m_sleepState[x] )
            {
                const bool sleepingLocked = IsUnderwaterSleepLocked( modelCount, y );
                if ( !hasWakeEnergy( x ) )
                {
                    return;
                }
                bool wokeBySweptImpact = false;
                if ( m_timeRemaining[x] > 0.0f && objectPairNeedsSweptCcd( x, y, m_timeRemaining[x] ) )
                {
                    ObjectContactSweepResult sweep = sweepObjectPair( x, y, m_timeRemaining[x] );
                    if ( sweep.hit )
                    {
                        const float availableTime = m_timeRemaining[x];
                        float colTime = refineObjectSweepContactTime( x, y, sweep.collisionTime, availableTime );
                        Physics::PhysicsPipelineRecord record;
                        record.stage = Physics::PhysicsPipelineStage::SweptObjectHit;
                        record.bodyA = x;
                        record.bodyB = y;
                        record.point = ( bodyRecords[static_cast<size_t>( x )].position +
                                         bodyRecords[static_cast<size_t>( y )].position ) *
                                       0.5f;
                        record.scalarA = colTime;
                        record.scalarB = availableTime;
                        recordObjectNarrowphaseEvent( event, ObjectNarrowphaseEventKind::SweptObjectHit, record );
                        emitObjectCollisionTimeEvent( event, x, y, colTime, availableTime );

                        (void)bodyStore.IntegrateBodyPose( colliderStore, x, colTime );
                        m_timeRemaining[x] = (std::max)( 0.0f, m_timeRemaining[x] - colTime );
                        if ( !sleepingLocked )
                        {
                            wakeSleepingModel( y );
                        }
                        wokeBySweptImpact = true;
                        markObjectVisualEvent( event, x, y );
                        writeObjectCollisionCellEvent( event, x, y );
                    }
                }
                if ( !wokeBySweptImpact && hasPersistentWakeContact( x, y ) )
                {
                    Physics::PhysicsPipelineRecord record;
                    record.stage = Physics::PhysicsPipelineStage::WakeDecision;
                    record.bodyA = x;
                    record.bodyB = y;
                    record.point = ( bodyRecords[static_cast<size_t>( x )].position +
                                     bodyRecords[static_cast<size_t>( y )].position ) *
                                   0.5f;
                    record.scalarA = sleepingLocked ? 0.0f : 1.0f;
                    recordObjectNarrowphaseEvent( event, ObjectNarrowphaseEventKind::WakeDecision, record );

                    if ( !sleepingLocked )
                    {
                        wakeSleepingModel( y );
                    }
                    markObjectVisualEvent( event, x, y );
                    writeObjectCollisionCellEvent( event, x, y );
                }
                return;
            }
            else
            {
                // Both bodies are sleeping; there is no awake energy to produce a wake event.
                return;
            }
        }

        if ( m_timeRemaining[x] <= 0.0f || m_timeRemaining[y] <= 0.0f )
        {
            return;
        }

        float availableTime = (std::min)( m_timeRemaining[x], m_timeRemaining[y] );
        if ( !objectPairNeedsSweptCcd( x, y, availableTime ) )
        {
            Physics::PhysicsPipelineRecord record;
            record.stage = Physics::PhysicsPipelineStage::SweptObjectMiss;
            record.bodyA = x;
            record.bodyB = y;
            record.point =
                ( bodyRecords[static_cast<size_t>( x )].position + bodyRecords[static_cast<size_t>( y )].position ) *
                0.5f;
            record.scalarA = availableTime;
            recordObjectNarrowphaseEvent( event, ObjectNarrowphaseEventKind::SweptObjectMiss, record );
            return;
        }

        ObjectContactSweepResult sweep = sweepObjectPair( x, y, availableTime );

        if ( sweep.hit )
        {
            float colTime = refineObjectSweepContactTime( x, y, sweep.collisionTime, availableTime );
            Physics::PhysicsPipelineRecord record;
            record.stage = Physics::PhysicsPipelineStage::SweptObjectHit;
            record.bodyA = x;
            record.bodyB = y;
            record.point =
                ( bodyRecords[static_cast<size_t>( x )].position + bodyRecords[static_cast<size_t>( y )].position ) *
                0.5f;
            record.scalarA = colTime;
            record.scalarB = availableTime;
            recordObjectNarrowphaseEvent( event, ObjectNarrowphaseEventKind::SweptObjectHit, record );
            emitObjectCollisionTimeEvent( event, x, y, colTime, availableTime );

            (void)bodyStore.IntegrateBodyPose( colliderStore, x, colTime );
            (void)bodyStore.IntegrateBodyPose( colliderStore, y, colTime );
            m_timeRemaining[x] = (std::max)( 0.0f, m_timeRemaining[x] - colTime );
            m_timeRemaining[y] = (std::max)( 0.0f, m_timeRemaining[y] - colTime );

            // Object/object CCD only advances to the contact candidate. The
            // persistent Catto rows below own velocity response and cache storage.
            markObjectVisualEvent( event, x, y );
            writeObjectCollisionCellEvent( event, x, y );
        }
        else
        {
            Physics::PhysicsPipelineRecord record;
            record.stage = Physics::PhysicsPipelineStage::SweptObjectMiss;
            record.bodyA = x;
            record.bodyB = y;
            record.point =
                ( bodyRecords[static_cast<size_t>( x )].position + bodyRecords[static_cast<size_t>( y )].position ) *
                0.5f;
            record.scalarA = availableTime;
            recordObjectNarrowphaseEvent( event, ObjectNarrowphaseEventKind::SweptObjectMiss, record );
        }
    };

    auto processObjectNarrowphaseIsland = [&]( int islandIndex )
    {
        const ObjectNarrowphaseIsland& island = m_objectNarrowphaseIslands[static_cast<size_t>( islandIndex )];
        const size_t pairEnd = island.firstPairOffset + island.pairCount;
        for ( size_t pairCursor = island.firstPairOffset; pairCursor < pairEnd; ++pairCursor )
        {
            const int pairIndex = m_objectNarrowphaseIslandPairIndices[pairCursor];
            processObjectNarrowphasePair( pairIndex, m_objectNarrowphaseEvents[static_cast<size_t>( pairIndex )] );
        }
    };

    auto processObjectNarrowphasePairsSerial = [&]()
    {
        PROFILE_SCOPED( "Frame/Physics/Narrowphase/SerialPairs" );
        for ( int pairIndex = 0; pairIndex < candidatePairCount; ++pairIndex )
        {
            ObjectNarrowphaseEvent event;
            processObjectNarrowphasePair( pairIndex, event );
            commitObjectNarrowphaseEvent( event );
        }
    };

    auto buildObjectNarrowphaseIslands = [&]()
    {
        PROFILE_SCOPED( "Frame/Physics/Narrowphase/BuildIslands" );
        m_objectNarrowphaseParent.resize( static_cast<size_t>( modelCount ) );
        m_objectNarrowphaseRank.assign( static_cast<size_t>( modelCount ), 0 );
        for ( int i = 0; i < modelCount; ++i )
        {
            m_objectNarrowphaseParent[static_cast<size_t>( i )] = i;
        }

        DisjointSet objectNarrowphaseSets( m_objectNarrowphaseParent,
                                           m_objectNarrowphaseRank,
                                           modelCount );

        for ( int pairIndex = 0; pairIndex < candidatePairCount; ++pairIndex )
        {
            const int x = candidatePairs[static_cast<size_t>( pairIndex )].first;
            const int y = candidatePairs[static_cast<size_t>( pairIndex )].second;
            if ( x < 0 || y < 0 || x >= modelCount || y >= modelCount )
            {
                continue;
            }
            objectNarrowphaseSets.Unite( x, y );
        }

        m_objectNarrowphaseIslands.clear();
        m_objectNarrowphaseIslandPairIndices.clear();
        m_objectNarrowphaseIslandWriteOffsets.clear();
        m_objectNarrowphaseRootToIsland.assign( static_cast<size_t>( modelCount ), -1 );
        for ( int pairIndex = 0; pairIndex < candidatePairCount; ++pairIndex )
        {
            const int x = candidatePairs[static_cast<size_t>( pairIndex )].first;
            const int y = candidatePairs[static_cast<size_t>( pairIndex )].second;
            if ( x < 0 || y < 0 || x >= modelCount || y >= modelCount )
            {
                continue;
            }

            const int root = objectNarrowphaseSets.Find( x );
            int islandIndex = m_objectNarrowphaseRootToIsland[static_cast<size_t>( root )];
            if ( islandIndex < 0 )
            {
                islandIndex = static_cast<int>( m_objectNarrowphaseIslands.size() );
                m_objectNarrowphaseRootToIsland[static_cast<size_t>( root )] = islandIndex;
                if ( m_objectNarrowphaseIslands.size() >= m_objectNarrowphaseIslands.capacity() )
                {
                    assert( false && "Physics object narrowphase island capacity exceeded" );
                    // Invariant: object narrowphase island storage is bounded
                    // by the precomputed pair/model limits for this frame.
                    // Overflow would reorder or drop pair work.
                    SB_FATAL( "Physics/PhysicsWorld", "Physics object narrowphase island capacity exceeded" );
                }
                m_objectNarrowphaseIslands.push_back( ObjectNarrowphaseIsland() );
                m_objectNarrowphaseIslands.back().minPairIndex = INT_MAX;
            }

            ObjectNarrowphaseIsland& island = m_objectNarrowphaseIslands[static_cast<size_t>( islandIndex )];
            island.minPairIndex = (std::min)( island.minPairIndex, pairIndex );
            ++island.pairCount;
        }
        if ( m_objectNarrowphaseIslandWriteOffsets.capacity() < m_objectNarrowphaseIslands.size() )
        {
            assert( false && "Physics object narrowphase island write-offset capacity exceeded" );
            // Invariant: write offsets are one row per island. A short reserve
            // would make worker writes overlap or depend on allocation order.
            SB_FATAL( "Physics/PhysicsWorld", "Physics object narrowphase island write-offset capacity exceeded" );
        }
        m_objectNarrowphaseIslandWriteOffsets.assign( m_objectNarrowphaseIslands.size(), 0 );
        size_t pairOffset = 0;
        for ( size_t islandIndex = 0; islandIndex < m_objectNarrowphaseIslands.size(); ++islandIndex )
        {
            ObjectNarrowphaseIsland& island = m_objectNarrowphaseIslands[islandIndex];
            island.firstPairOffset = pairOffset;
            m_objectNarrowphaseIslandWriteOffsets[islandIndex] = pairOffset;
            pairOffset += island.pairCount;
        }
        if ( pairOffset > m_objectNarrowphaseIslandPairIndices.capacity() )
        {
            assert( false && "Physics object narrowphase island pair capacity exceeded" );
            // Invariant: pair-index staging owns the exact compacted pair set
            // for the worker pass. Overflow would drop pairs from narrowphase.
            SB_FATAL( "Physics/PhysicsWorld", "Physics object narrowphase island pair capacity exceeded" );
        }
        m_objectNarrowphaseIslandPairIndices.resize( pairOffset, 0 );
        for ( int pairIndex = 0; pairIndex < candidatePairCount; ++pairIndex )
        {
            const int x = candidatePairs[static_cast<size_t>( pairIndex )].first;
            const int y = candidatePairs[static_cast<size_t>( pairIndex )].second;
            if ( x < 0 || y < 0 || x >= modelCount || y >= modelCount )
            {
                continue;
            }

            const int root = objectNarrowphaseSets.Find( x );
            const int islandIndex = m_objectNarrowphaseRootToIsland[static_cast<size_t>( root )];
            if ( islandIndex < 0 )
            {
                continue;
            }
            size_t& writeOffset = m_objectNarrowphaseIslandWriteOffsets[static_cast<size_t>( islandIndex )];
            m_objectNarrowphaseIslandPairIndices[writeOffset++] = pairIndex;
        }
        std::sort( m_objectNarrowphaseIslands.begin(),
                   m_objectNarrowphaseIslands.end(),
                   []( const ObjectNarrowphaseIsland& a, const ObjectNarrowphaseIsland& b )
                   { return a.minPairIndex < b.minPairIndex; } );
    };

    m_objectNarrowphaseIslands.clear();
    m_objectNarrowphaseIslandPairIndices.clear();
    m_objectNarrowphaseIslandWriteOffsets.clear();
    bool ranParallelNarrowphase = false;
    const bool mayBenefitFromIslandDispatch =
        PHYSICS_NARROWPHASE_ISLAND_WORKER_ENABLED && config.physicsParallel && config.physicsParallelNarrowphase &&
        candidatePairCount >= PHYSICS_NARROWPHASE_PARALLEL_MIN_PAIRS &&
        candidatePairCount <= modelCount * PHYSICS_NARROWPHASE_PARALLEL_MAX_PAIRS_PER_BODY &&
        workerPool.GetThreadCount() > 0;
    if ( mayBenefitFromIslandDispatch )
    {
        buildObjectNarrowphaseIslands();

        const int islandCount = static_cast<int>( m_objectNarrowphaseIslands.size() );
        const bool hasSpreadOutNarrowphaseIslands =
            islandCount > 0 &&
            candidatePairCount <= islandCount * PHYSICS_NARROWPHASE_PARALLEL_MAX_AVG_PAIRS_PER_ISLAND;
        if ( islandCount >= PHYSICS_NARROWPHASE_PARALLEL_MIN_ISLANDS && hasSpreadOutNarrowphaseIslands )
        {
            m_objectNarrowphaseEvents.assign( candidatePairs.size(), ObjectNarrowphaseEvent() );
            {
                PROFILE_SCOPED( "Frame/Physics/Narrowphase/IslandWorkerDispatch" );
                workerPool.ParallelForNoAlloc( 0,
                                               islandCount,
                                               processObjectNarrowphaseIsland,
                                               PHYSICS_NARROWPHASE_PARALLEL_MIN_ISLANDS,
                                               "Frame/Physics/Narrowphase/IslandWorkerDispatch/WorkerIslands",
                                               PHYSICS_NARROWPHASE_ISLAND_WORKER_HASH );
            }
            {
                PROFILE_SCOPED( "Frame/Physics/Narrowphase/CommitEvents" );
                for ( int pairIndex = 0; pairIndex < candidatePairCount; ++pairIndex )
                {
                    commitObjectNarrowphaseEvent( m_objectNarrowphaseEvents[static_cast<size_t>( pairIndex )] );
                }
            }
            ranParallelNarrowphase = true;
        }
    }

    if ( !ranParallelNarrowphase )
    {
        processObjectNarrowphasePairsSerial();
    }
    PROFILE_END( "Frame/Physics/Narrowphase" );

    // Terrain phase ownership:
    //   1. Keep swept terrain detection here so fast bodies still stop at the
    //      correct time of impact.
    //   2. Convert the hit into a terrain manifold only. Do not apply impulses
    //      or terrain-only velocity response in this phase.
    //   3. Leave remaining-time integration and all normal/friction response to
    //      the shared persistent contact rows below.
    PROFILE_BEGIN( "Frame/Physics/Terrain" );
    PROFILE_BEGIN( "Frame/Physics/Terrain/Detect" );
    auto detectTerrainAt = [&]( int x )
    {
        TerrainDetectionCandidate& candidate = m_terrainDetectionCandidates[static_cast<size_t>( x )];
        if ( bodyIsFixed( x ) )
        {
            return;
        }
        if ( m_sleepState[x] || m_timeRemaining[x] <= 0.0f )
        {
            return;
        }
        if ( x >= static_cast<int>( bodyRecords.size() ) || x >= static_cast<int>( colliderRecords.size() ) )
        {
            return;
        }

        candidate.availableTime = m_timeRemaining[x];
        candidate.sweep = SweepTerrainContact( terrainContactBodyViewForIndex( x ),
                                               colliderRecords[static_cast<size_t>( x )].shape,
                                               candidate.availableTime );
        candidate.tested = 1;
    };

    auto commitTerrainCandidate = [&]( int x, float availableTime, const TerrainContactSweepResult& sweep )
    {
        if ( sweep.hit )
        {
            const float colTime = sweep.collisionTime;
            (void)bodyStore.IntegrateBodyPose( colliderStore, x, colTime );
            const float remainingTime = (std::max)( 0.0f, availableTime - colTime );
            Physics::TerrainContactManifold manifold;
            const bool hasManifold =
                Physics::BuildTerrainContactManifold( terrainContactBodyViewForIndex( x ),
                                                      colliderRecords[static_cast<size_t>( x )].shape,
                                                      x,
                                                      sweep,
                                                      availableTime,
                                                      manifold );

            Physics::PhysicsPipelineRecord record;
            record.stage = Physics::PhysicsPipelineStage::TerrainHit;
            record.bodyA = x;
            record.bodyB = TERRAIN_BODY_INDEX;
            record.point = hasManifold ? manifold.points[0].point : bodyRecords[static_cast<size_t>( x )].position;
            record.normal = hasManifold ? manifold.normal : ZERO_VECTOR;
            record.scalarA = colTime;
            record.scalarB = hasManifold && manifold.supportsRestingPolicy ? 1.0f : 0.0f;
            record.scalarC = hasManifold ? static_cast<float>( manifold.pointCount ) : 0.0f;
            RecordPhysicsPipelineStage( record );
            EmitPhysicsCollisionTime( diagnosticNames, diagnosticNameCount, "terrain", x, -1, colTime, availableTime );

            if ( hasManifold )
            {
                m_terrainContactManifolds.push_back( manifold );
                if ( manifold.supportsRestingPolicy )
                {
                    m_sleepSupportedThisFrame[x] = 1;
                }
                else
                {
                    m_sleepInhibitedThisFrame[x] = 1;
                }
            }
            else
            {
                m_sleepInhibitedThisFrame[x] = 1;
            }
            MarkCollisionVisualContact( x );
            m_timeRemaining[x] = remainingTime;
        }
    };

    m_terrainDetectionCandidates.assign( static_cast<size_t>( modelCount ), TerrainDetectionCandidate() );
    if ( config.physicsParallel && config.physicsParallelTerrainDetect )
    {
        workerPool.ParallelForNoAlloc( 0,
                                       modelCount,
                                       detectTerrainAt,
                                       PHYSICS_PARALLEL_MIN_BODIES,
                                       "Frame/Physics/Terrain/Detect/WorkerBodies",
                                       PHYSICS_TERRAIN_DETECT_WORKER_HASH );
    }
    else
    {
        for ( int x = 0; x < modelCount; ++x )
        {
            detectTerrainAt( x );
        }
    }

    for ( int x = 0; x < modelCount; ++x )
    {
        const TerrainDetectionCandidate& candidate = m_terrainDetectionCandidates[static_cast<size_t>( x )];
        if ( candidate.tested )
        {
            commitTerrainCandidate( x, candidate.availableTime, candidate.sweep );
        }
    }
    PROFILE_END( "Frame/Physics/Terrain/Detect" );
    PROFILE_END( "Frame/Physics/Terrain" );

    PreparePersistentContactSideEffects( modelCount );
    PersistentContactSolverContext solverContext =
        CreatePersistentContactSolverContext( bodyStore, colliderStore, config );
    m_contactSolver.Solve( solverContext, dt );
    ApplyPersistentContactSideEffects( bodyStore, colliderStore, worldForces );
    WakePointJointConnectedBodies( bodyStore, colliderStore, worldForces, dt );
    (void)Ragdoll::SolvePointJoints( bodyStore, m_pointJointConstraints, m_sleepState, dt );
    AppendPointJointSupportEdges( bodyStore, modelCount );
    // Object contacts are converted into stack support only after terrain
    // response has had a chance to seed true support for this frame.
    PropagateSleepSupport( bodyStore.Records() );

    // Integrate remaining time for awake models
    PROFILE_BEGIN( "Frame/Physics/Integrate" );
    auto integrateRemainingAt = [&]( int x )
    {
        if ( bodyIsFixed( x ) )
        {
            return;
        }
        if ( m_sleepState[x] )
        {
            return;
        }

        if ( m_timeRemaining[x] > 0.0f )
        {
            (void)bodyStore.IntegrateBodyPose( colliderStore, x, m_timeRemaining[x] );
        }
    };

    if ( config.physicsParallel && config.physicsParallelIntegrate )
    {
        workerPool.ParallelForNoAlloc( 0,
                                       modelCount,
                                       integrateRemainingAt,
                                       PHYSICS_PARALLEL_MIN_BODIES,
                                       "Frame/Physics/Integrate/WorkerBodies",
                                       PHYSICS_INTEGRATE_WORKER_HASH );
    }
    else
    {
        for ( int x = 0; x < modelCount; ++x )
        {
            integrateRemainingAt( x );
        }
    }

    // Build sleep islands from the persistent contact graph. Sleep counters are
    // tracked per body, but the final transition is island-level: connected awake
    // bodies deactivate together only if the whole island is quiet and rooted in
    // credible support.
    //
    // Important nuance:
    //   "Supported" is an island property, not a demand that every body directly
    //   touch terrain. A box can be quiet and physically constrained by the side
    //   of a grounded pile. Requiring that specific box to also pass terrain
    //   support classification creates the bad varied-scene wedge: terrain says
    //   "not a stable footprint", object contacts keep the box from falling, and
    //   the sleep gate has no way out. The anchor pass below keeps the original
    //   safety rule for floating/mid-air islands: at least one member must still
    //   be terrain-supported, fixed, or already sleeping from a previous proven
    //   support state.
    m_sleepIslandParent.assign( modelCount, 0 );
    m_sleepIslandRank.assign( modelCount, 0 );
    m_sleepIslandHasAwake.assign( modelCount, 0 );
    m_sleepIslandHasSupportAnchor.assign( modelCount, 0 );
    m_sleepIslandEligible.assign( modelCount, 1 );
    m_sleepIslandCanSleep.assign( modelCount, 1 );
    m_sleepPointJointBody.assign( modelCount, 0 );
    m_sleepIslandHasPointJoint.assign( modelCount, 0 );
    m_sleepIslandPointJointsRelaxed.assign( modelCount, 1 );
    for ( int i = 0; i < modelCount; ++i )
    {
        m_sleepIslandParent[i] = i;
    }

    auto findIsland = [&]( int index ) -> int
    {
        // Union-find lookup with path compression. In plain terms: every body in
        // a connected contact group points to the same representative root, so
        // the sleep system can make one decision for the whole group.
        int root = index;
        while ( m_sleepIslandParent[root] != root )
        {
            root = m_sleepIslandParent[root];
        }
        while ( m_sleepIslandParent[index] != index )
        {
            int parent = m_sleepIslandParent[index];
            m_sleepIslandParent[index] = root;
            index = parent;
        }
        return root;
    };

    auto unionIslands = [&]( int a, int b )
    {
        // Merge two contact groups. Rank keeps the tree shallow so repeated
        // findIsland calls stay cheap during large stacks.
        int rootA = findIsland( a );
        int rootB = findIsland( b );
        if ( rootA == rootB )
        {
            return;
        }

        if ( m_sleepIslandRank[rootA] < m_sleepIslandRank[rootB] )
        {
            std::swap( rootA, rootB );
        }
        m_sleepIslandParent[rootB] = rootA;
        if ( m_sleepIslandRank[rootA] == m_sleepIslandRank[rootB] )
        {
            ++m_sleepIslandRank[rootA];
        }
    };

    for ( const PersistentContact& c : m_persistentContacts )
    {
        // Persistent contacts are the solver's current dynamic contact graph, so
        // they are the natural edges for island sleep. Sleeping bodies still act
        // as graph anchors, but only awake bodies below participate in the current
        // eligibility and counter checks.
        if ( c.bodyA >= 0 && c.bodyA < modelCount && c.bodyB >= 0 && c.bodyB < modelCount )
        {
            unionIslands( c.bodyA, c.bodyB );
        }
    }

    for ( const PointJointConstraint& constraint : m_pointJointConstraints )
    {
        // Hazard: low velocity is not enough to prove a constrained component is
        // ready to sleep. A stretched joint can be numerically quiet for a frame,
        // so block sleep until point anchors are back within a small tolerance.
        const int a = constraint.BodyAIndex( bodyStore );
        const int b = constraint.BodyBIndex( bodyStore );
        if ( a < 0 || b < 0 || a == b || a >= modelCount || b >= modelCount )
        {
            continue;
        }

        m_sleepPointJointBody[a] = 1;
        m_sleepPointJointBody[b] = 1;
        unionIslands( a, b );
    }

    m_sleepVisualIslandIds.clear();
    m_sleepVisualIslandBodies.clear();
    for ( int x = 0; x < modelCount; ++x )
    {
        const int visualId = x < static_cast<int>( m_sleepIslandVisualId.size() ) ? m_sleepIslandVisualId[x] : 0;
        if ( visualId <= 0 )
        {
            continue;
        }

        int visualSlot = -1;
        for ( int i = 0; i < static_cast<int>( m_sleepVisualIslandIds.size() ); ++i )
        {
            if ( m_sleepVisualIslandIds[i] == visualId )
            {
                visualSlot = i;
                break;
            }
        }

        if ( visualSlot >= 0 )
        {
            unionIslands( m_sleepVisualIslandBodies[visualSlot], x );
        }
        else
        {
            m_sleepVisualIslandIds.push_back( visualId );
            m_sleepVisualIslandBodies.push_back( x );
        }
    }

    for ( int x = 0; x < modelCount; ++x )
    {
        const int root = findIsland( x );

        // A support anchor is evidence that this island is not a free-floating
        // collection of bodies that merely became numerically quiet. Terrain
        // support remains the usual anchor. Fixed objects and sleeping bodies are
        // also valid anchors: fixed objects are immovable world geometry, and a
        // sleeping dynamic body could only have reached sleep after satisfying the
        // same support gate in an earlier frame.
        if ( bodyIsFixed( x ) || ( x < static_cast<int>( m_sleepState.size() ) && m_sleepState[x] != 0 ) ||
             ( x < static_cast<int>( m_sleepSupportedThisFrame.size() ) && m_sleepSupportedThisFrame[x] != 0 ) )
        {
            m_sleepIslandHasSupportAnchor[root] = 1;
        }
        if ( m_sleepPointJointBody[x] != 0 )
        {
            m_sleepIslandHasPointJoint[root] = 1;
        }
    }

    for ( const PointJointConstraint& constraint : m_pointJointConstraints )
    {
        const int a = constraint.BodyAIndex( bodyStore );
        const int b = constraint.BodyBIndex( bodyStore );
        if ( a < 0 || b < 0 || a == b || a >= modelCount || b >= modelCount )
        {
            continue;
        }

        auto orientationA = bodyRecords[static_cast<size_t>( a )].orientation;
        auto orientationB = bodyRecords[static_cast<size_t>( b )].orientation;
        const auto rotA = orientationA.GetOrientationMatrix();
        const auto rotB = orientationB.GetOrientationMatrix();
        const Vector3 anchorA = bodyRecords[static_cast<size_t>( a )].position + rotA * constraint.localAnchorA;
        const Vector3 anchorB = bodyRecords[static_cast<size_t>( b )].position + rotB * constraint.localAnchorB;
        const float distance = Vector::VectorMag( anchorB - anchorA );
        const float allowedDistance =
            constraint.slack + (std::max)( POINT_JOINT_SLEEP_MIN_ERROR_TOLERANCE,
                                           constraint.slack * POINT_JOINT_SLEEP_SLACK_TOLERANCE_SCALE );
        if ( distance > allowedDistance )
        {
            m_sleepIslandPointJointsRelaxed[findIsland( a )] = 0;
        }
    }

    for ( int x = 0; x < modelCount; ++x )
    {
        if ( bodyIsFixed( x ) )
        {
            continue;
        }
        if ( m_sleepState[x] )
        {
            continue;
        }

        const int root = findIsland( x );
        m_sleepIslandHasAwake[root] = 1;

        const Vector3& vel = bodyRecords[static_cast<size_t>( x )].linearVelocity;
        const Vector3& omega = bodyRecords[static_cast<size_t>( x )].angularVelocity;
        float speedSq = vel.x * vel.x + vel.y * vel.y + vel.z * vel.z;
        float omegaSq = omega.x * omega.x + omega.y * omega.y + omega.z * omega.z;
        bool supported = x < static_cast<int>( m_sleepSupportedThisFrame.size() ) && m_sleepSupportedThisFrame[x] != 0;
        bool hasRestingObjectContact =
            x < static_cast<int>( m_persistentRestingContactCounts.size() ) && m_persistentRestingContactCounts[x] > 0;
        bool islandHasSupportAnchor = m_sleepIslandHasSupportAnchor[root] != 0;
        bool pointJointMember = x < static_cast<int>( m_sleepPointJointBody.size() ) && m_sleepPointJointBody[x] != 0;
        bool pointJointIsland = m_sleepIslandHasPointJoint[root] != 0;
        float quietLinearSq = SLEEP_LINEAR_SQ;
        float quietAngularSq = SLEEP_ANGULAR_SQ;
        if ( pointJointMember && pointJointIsland && islandHasSupportAnchor )
        {
            // Why: anchored ragdolls can keep feeding tiny contact and joint
            // corrections into each other after they have visually settled.
            // The relaxed gate is still island-wide and support/joint-error
            // guarded, so unsupported or stretched ragdolls stay awake.
            quietLinearSq *= POINT_JOINT_SLEEP_LINEAR_SPEED_SCALE * POINT_JOINT_SLEEP_LINEAR_SPEED_SCALE;
            quietAngularSq *= POINT_JOINT_SLEEP_ANGULAR_SPEED_SCALE * POINT_JOINT_SLEEP_ANGULAR_SPEED_SCALE;
        }
        bool quiet = speedSq < quietLinearSq && omegaSq < quietAngularSq;
        bool pointJointAnchoredSupport = quiet && pointJointMember && pointJointIsland && islandHasSupportAnchor;

        // A quiet body in a grounded object-contact island is supported even if
        // the body itself is side-wedged or touching terrain on an edge/point.
        // This is deliberately narrower than "any contact means support":
        //
        //   * quiet keeps active impacts and real toppling awake;
        //   * hasRestingObjectContact requires a real object-contact footprint;
        //   * islandHasSupportAnchor keeps floating piles from becoming sleepers.
        //
        // Marking the body supported here also keeps SkullScope diagnostics honest:
        // the body is not terrain-supported, but it is supported for deactivation
        // by a contact island rooted in credible support.
        if ( !supported && quiet && hasRestingObjectContact && islandHasSupportAnchor )
        {
            m_sleepSupportedThisFrame[x] = 1;
            supported = true;
        }
        if ( !supported && pointJointAnchoredSupport )
        {
            m_sleepSupportedThisFrame[x] = 1;
            supported = true;
        }

        // Terrain can still inhibit sleep for edge/point contacts when that
        // contact is the only apparent support. In a quiet anchored island,
        // though, the same terrain rejection must not be an infinite veto: the
        // object solver may have wedged the body against neighbors so it cannot
        // fall into a more stable footprint. The island anchor and object-contact
        // checks above are the escape hatch for that exact low-energy state.
        bool terrainInhibitBlocksSleep = m_sleepInhibitedThisFrame[x] != 0 &&
                                         !( quiet && hasRestingObjectContact && islandHasSupportAnchor ) &&
                                         !pointJointAnchoredSupport;
        bool pointJointErrorBlocksSleep = pointJointMember &&
                                          root < static_cast<int>( m_sleepIslandPointJointsRelaxed.size() ) &&
                                          m_sleepIslandPointJointsRelaxed[root] == 0;

        // Modern sleep is still velocity based, but Skullbonez also requires
        // credible island support so unsupported gravity bodies cannot become
        // numerically quiet for a few frames while visibly floating.
        if ( !quiet || !supported || terrainInhibitBlocksSleep || pointJointErrorBlocksSleep )
        {
            m_sleepIslandEligible[root] = 0;
        }

        Physics::PhysicsPipelineRecord record;
        record.stage = Physics::PhysicsPipelineStage::SleepIslandDecision;
        record.bodyA = x;
        record.bodyB = root;
        record.point = bodyRecords[static_cast<size_t>( x )].position;
        record.scalarA = quiet ? 1.0f : 0.0f;
        record.scalarB = supported ? 1.0f : 0.0f;
        record.scalarC = terrainInhibitBlocksSleep ? 1.0f : ( pointJointErrorBlocksSleep ? 2.0f : 0.0f );
        RecordPhysicsPipelineStage( record );
    }

    if ( !m_sleepEnabled )
    {
        std::fill( m_sleepCounter.begin(), m_sleepCounter.end(), static_cast<uint8_t>( 0 ) );
        m_sleepIslandCanSleep.assign( modelCount, 0 );
        m_sleepIslandAssignedVisualId.assign( modelCount, 0 );
        PROFILE_END( "Frame/Physics/Integrate" );
        return;
    }

    for ( int x = 0; x < modelCount; ++x )
    {
        if ( bodyIsFixed( x ) )
        {
            continue;
        }
        if ( m_sleepState[x] )
        {
            continue;
        }

        const int root = findIsland( x );
        if ( m_sleepIslandHasAwake[root] && m_sleepIslandEligible[root] )
        {
            if ( m_sleepCounter[x] < SLEEP_FRAMES )
            {
                ++m_sleepCounter[x];
            }
        }
        else
        {
            m_sleepCounter[x] = 0;
        }
    }

    for ( int x = 0; x < modelCount; ++x )
    {
        if ( bodyIsFixed( x ) )
        {
            continue;
        }
        if ( m_sleepState[x] )
        {
            continue;
        }

        const int root = findIsland( x );
        if ( m_sleepCounter[x] < SLEEP_FRAMES )
        {
            // Every awake body in an eligible island must accumulate the full
            // quiet-frame count before any body in that island is deactivated.
            m_sleepIslandCanSleep[root] = 0;
        }
    }

    m_sleepIslandAssignedVisualId.assign( modelCount, 0 );
    for ( int x = 0; x < modelCount; ++x )
    {
        if ( bodyIsFixed( x ) )
        {
            continue;
        }
        if ( !m_sleepState[x] || m_sleepIslandVisualId[x] == 0 )
        {
            continue;
        }

        const int root = findIsland( x );
        if ( m_sleepIslandAssignedVisualId[root] == 0 )
        {
            m_sleepIslandAssignedVisualId[root] = m_sleepIslandVisualId[x];
        }
    }

    for ( int x = 0; x < modelCount; ++x )
    {
        if ( bodyIsFixed( x ) )
        {
            continue;
        }
        if ( m_sleepState[x] )
        {
            continue;
        }

        const int root = findIsland( x );
        if ( m_sleepIslandHasAwake[root] && m_sleepIslandEligible[root] && m_sleepIslandCanSleep[root] )
        {
            if ( m_sleepIslandAssignedVisualId[root] == 0 )
            {
                m_sleepIslandAssignedVisualId[root] = m_nextSleepIslandVisualId++;
                if ( m_nextSleepIslandVisualId <= 0 )
                {
                    m_nextSleepIslandVisualId = 1;
                }
            }
            m_sleepState[x] = 1;
            m_sleepIslandVisualId[x] = m_sleepIslandAssignedVisualId[root];
            Physics::PhysicsPipelineRecord record;
            record.stage = Physics::PhysicsPipelineStage::SleepIslandDecision;
            record.bodyA = x;
            record.bodyB = root;
            record.point = bodyRecords[static_cast<size_t>( x )].position;
            record.scalarA = 1.0f;
            record.scalarB = static_cast<float>( m_sleepIslandAssignedVisualId[root] );
            record.scalarC = static_cast<float>( m_sleepCounter[x] );
            RecordPhysicsPipelineStage( record );
            // Zeroing velocities at the island sleep transition prevents tiny
            // residual solver drift from reappearing when the body later wakes.
            bodyRecords[static_cast<size_t>( x )].linearVelocity = Math::Vector::ZERO_VECTOR;
            bodyRecords[static_cast<size_t>( x )].angularVelocity = Math::Vector::ZERO_VECTOR;
            bodyRecords[static_cast<size_t>( x )].isSleeping = true;
            LockUnderwaterSleeperIfReady( worldForces, bodyStore, colliderStore, x );
        }
    }
    PROFILE_END( "Frame/Physics/Integrate" );
}


PhysicsDiagnosticsView PhysicsWorld::GetDiagnosticsView() const
{
    return PhysicsDiagnosticsView{ m_persistentContacts,
                                   m_persistentContactSolverStats,
                                   m_sleepIslandParent,
                                   m_sleepSupportedThisFrame,
                                   m_sleepInhibitedThisFrame,
                                   m_sleepState,
                                   m_sleepCounter,
                                   m_sleepIslandEligible,
                                   m_sleepIslandCanSleep,
                                   m_pointJointConstraints,
                                   m_spatialGrid,
                                   m_candidatePairs,
                                   m_collisionCellKeys,
                                   m_sleepSupportEdges,
                                   m_sleepIslandVisualId,
                                   m_physicsPipelineTrace,
                                   m_terrainContactManifolds };
}

uint64_t PhysicsWorld::CollectMemoryBytes() const
{
    uint64_t bytes = static_cast<uint64_t>( sizeof( *this ) );
    bytes += VectorCapacityBytes( m_candidatePairs );
    bytes += VectorCapacityBytes( m_timeRemaining );
    bytes += VectorCapacityBytes( m_sleepSupportedThisFrame );
    bytes += VectorCapacityBytes( m_sleepInhibitedThisFrame );
    bytes += VectorCapacityBytes( m_sleepState );
    bytes += VectorCapacityBytes( m_sleepCounter );
    bytes += VectorCapacityBytes( m_underwaterSleepLocked );
    bytes += VectorCapacityBytes( m_tornadoCaptureSeconds );
    bytes += VectorCapacityBytes( m_tornadoEjectCooldownSeconds );
    bytes += VectorCapacityBytes( m_tornadoFixedTreeReleaseWakeBodies );
    bytes += VectorCapacityBytes( m_collisionVisualContacts );
    bytes += VectorCapacityBytes( m_sleepIslandVisualId );
    bytes += VectorCapacityBytes( m_sleepIslandAssignedVisualId );
    bytes += VectorCapacityBytes( m_sleepSupportEdges );
    bytes += VectorCapacityBytes( m_sleepIslandParent );
    bytes += VectorCapacityBytes( m_sleepIslandRank );
    bytes += VectorCapacityBytes( m_sleepIslandHasAwake );
    bytes += VectorCapacityBytes( m_sleepIslandHasSupportAnchor );
    bytes += VectorCapacityBytes( m_sleepIslandEligible );
    bytes += VectorCapacityBytes( m_sleepIslandCanSleep );
    bytes += VectorCapacityBytes( m_sleepPointJointBody );
    bytes += VectorCapacityBytes( m_sleepIslandHasPointJoint );
    bytes += VectorCapacityBytes( m_sleepIslandPointJointsRelaxed );
    bytes += VectorCapacityBytes( m_sleepVisualIslandIds );
    bytes += VectorCapacityBytes( m_sleepVisualIslandBodies );
    bytes += VectorCapacityBytes( m_persistentContacts );
    bytes += VectorCapacityBytes( m_persistentContactCache );
    bytes += VectorCapacityBytes( m_persistentContactCounts );
    bytes += VectorCapacityBytes( m_persistentRestingContactCounts );
    bytes += VectorCapacityBytes( m_solverBodies );
    bytes += VectorCapacityBytes( m_physicsDebugContacts );
    bytes += VectorCapacityBytes( m_physicsPipelineTrace );
    bytes += VectorCapacityBytes( m_terrainContactManifolds );
    bytes += VectorCapacityBytes( m_terrainDetectionCandidates );
    bytes += VectorCapacityBytes( m_objectNarrowphaseEvents );
    bytes += VectorCapacityBytes( m_objectNarrowphaseIslands );
    bytes += VectorCapacityBytes( m_objectNarrowphaseIslandPairIndices );
    bytes += VectorCapacityBytes( m_objectNarrowphaseIslandWriteOffsets );
    bytes += VectorCapacityBytes( m_objectNarrowphaseParent );
    bytes += VectorCapacityBytes( m_objectNarrowphaseRank );
    bytes += VectorCapacityBytes( m_objectNarrowphaseRootToIsland );
    bytes += VectorCapacityBytes( m_restingWakeVisitedScratch );
    bytes += VectorCapacityBytes( m_restingWakeQueueScratch );
    bytes += VectorCapacityBytes( m_pointJointConstraints );
    bytes += VectorCapacityBytes( m_collisionCellKeys );
    bytes += static_cast<uint64_t>( m_tornadoField.DynamicMemoryBytes() );
    bytes += static_cast<uint64_t>( m_tornadoSystem.DynamicMemoryBytes() );
    return bytes;
}

uint64_t PhysicsWorld::CollectDebugAndBroadphaseMemoryBytes() const
{
    uint64_t bytes = static_cast<uint64_t>( sizeof( m_spatialGrid ) );
    bytes += VectorCapacityBytes( m_candidatePairs );
    bytes += VectorCapacityBytes( m_collisionCellKeys );
    bytes += VectorCapacityBytes( m_collisionVisualContacts );
    bytes += VectorCapacityBytes( m_sleepIslandVisualId );
    bytes += VectorCapacityBytes( m_physicsDebugContacts );
    bytes += VectorCapacityBytes( m_physicsPipelineTrace );
    bytes += static_cast<uint64_t>( m_tornadoField.DynamicMemoryBytes() );
    return bytes;
}


const Math::CollisionDetection::SpatialGrid& PhysicsWorld::GetSpatialGrid() const
{
    return m_spatialGrid;
}


const std::vector<int64_t>& PhysicsWorld::GetCollisionCellKeys() const
{
    return m_collisionCellKeys;
}


const std::vector<uint8_t>& PhysicsWorld::GetCollisionVisualContacts() const
{
    return m_collisionVisualContacts;
}


const std::vector<int>& PhysicsWorld::GetFixedContactHighlightBodies() const
{
    return m_persistentContactSideEffects.fixedContactBodies;
}


const std::vector<PhysicsFixedTreeReleaseEvent>& PhysicsWorld::GetFixedTreeReleaseEvents() const
{
    return m_persistentContactSideEffects.fixedTreeReleases;
}


const std::vector<uint8_t>& PhysicsWorld::GetSleepStates() const
{
    return m_sleepState;
}


const std::vector<int>& PhysicsWorld::GetSleepIslandVisualIds() const
{
    return m_sleepIslandVisualId;
}


const std::vector<uint8_t>& PhysicsWorld::GetSleepSupportedStates() const
{
    return m_sleepSupportedThisFrame;
}


const std::vector<uint8_t>& PhysicsWorld::GetSleepInhibitedStates() const
{
    return m_sleepInhibitedThisFrame;
}


const std::vector<PhysicsDebugContact>& PhysicsWorld::GetPhysicsDebugContacts() const
{
    return m_physicsDebugContacts;
}


const std::vector<PhysicsPipelineRecord>& PhysicsWorld::GetPhysicsPipelineTrace() const
{
    return m_physicsPipelineTrace;
}
