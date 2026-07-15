/*
File: SkullbonezSource/Physics/PhysicsWorld.cpp
Purpose:
  Owns per-scene physics working state shared by broadphase, solver, and diagnostics.

Summary:
  PhysicsWorld.cpp owns per-scene physics working state shared by broadphase,
  solver, and diagnostics. As an implementation unit, keep edits anchored on
  deterministic physics, diagnostics, or world-state flow and on the
  glossary/invariants below.

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
  X-macro field list: Preprocessor list invoked by several tiny visitors so
    replay capture and restore use the same ordered state inventory.
  PhysicsScene: Step owner that supplies stores and handles model-order
    writeback after compact physics work finishes.
  Lane F: Fatal invariant lane for should-never-happen engine state.
  Mutual-gravity pair scratch: Triangular array with one force value for every
    `(i,j)` body pair, populated in parallel and replayed in serial model order.

Invariants:
  - Physics-visible behavior must remain deterministic; byte-exact baselines
    are the validation contract.
  - Fixed-capacity physics scratch buffers must not grow during gameplay; an
    exhausted reserve is a Lane F failure because continuing would either
    allocate on a hot path or silently drop deterministic side effects.
  - Mutual-gravity chunk scheduling may vary, but pair slots and the final
    triangular replay order never depend on worker count.

Related:
  - SkullbonezSource/Physics/PhysicsWorld.h
  - Agentic/Reference/physics-overview.md
  - Agentic/Reference/comment-style-guide.md
*/
#include "PhysicsWorld.h"
#include "../Assets/AssetKeys.h"
#include "../Runtime/Replay/ReplayRetainedMemory.h"

#include "../Core/Config.h"
#include "../Core/FatalError.h"
#include "BuoyancySystem.h"
#include "DisjointSet.h"
#include "PhysicsApi.h"
#include "PhysicsBodyStore.h"
#include "SolverBroadphaseStage.h"
#include "PhysicsWorldForces.h"
#include "ColliderStore.h"
#include "ObjectContactManifold.h"
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

using namespace SkullbonezCore::Physics;
using SkullbonezCore::Math::Vector::Vector3;
using SkullbonezCore::Math::Vector::ZERO_VECTOR;
using SkullbonezCore::Runtime::REPLAY_SOLVER_SNAPSHOT_RESERVE_HARD_BYTES;
using SkullbonezCore::Runtime::REPLAY_SOLVER_SNAPSHOT_RESERVE_OWNER;
using SkullbonezCore::Runtime::ReplaySolverContactCacheSample;
using SkullbonezCore::Runtime::ReplaySolverPersistentContactSample;
using SkullbonezCore::Runtime::ReplaySolverStatsSample;
using SkullbonezCore::Runtime::ReplaySolverWorldSnapshot;
namespace Math = SkullbonezCore::Math;
namespace Physics = SkullbonezCore::Physics;
namespace Vector = SkullbonezCore::Math::Vector;
namespace RuntimeAllocation = SkullbonezCore::Runtime::Allocation;

namespace
{
constexpr size_t MAX_PIPELINE_TRACE_RECORDS = 4096;
constexpr float EXPLICIT_WAKE_NEIGHBOR_SLOP = 0.50f;
constexpr float EXPLICIT_WAKE_VERTICAL_SLOP = 0.25f;
// Why: worker fan-out is more expensive than the work for the validation-sized
// 300-body scenes. Keep all-body jobs inline until there is enough work per
// chunk for the persistent worker pool to pay for itself.
constexpr int PHYSICS_PARALLEL_MIN_BODIES = 512;
constexpr float POINT_JOINT_SLEEP_MIN_ERROR_TOLERANCE = 0.15f;
constexpr float POINT_JOINT_SLEEP_SLACK_TOLERANCE_SCALE = 0.75f;
constexpr float POINT_JOINT_SLEEP_LINEAR_SPEED_SCALE = 6.0f;
constexpr float POINT_JOINT_SLEEP_ANGULAR_SPEED_SCALE = 6.0f;
constexpr uint8_t DEFAULT_PHYSICS_SLEEP_FRAMES = 30;
constexpr int PHYSICS_CANDIDATE_PAIR_RESERVE = SkullbonezCore::Scene::Capacity::MAX_GAME_MODELS * 4;
constexpr int PHYSICS_COLLISION_VISUAL_BODY_RESERVE = PHYSICS_CANDIDATE_PAIR_RESERVE * 2;
constexpr std::size_t REPLAY_SOLVER_SNAPSHOT_VECTOR_INITIAL_CAPACITY = 1024u;
constexpr std::size_t REPLAY_SOLVER_SNAPSHOT_VECTOR_GROWTH_CHUNK = 4096u;
// Runtime allocation policy: replay prediction visualization can discover
// larger solver snapshots interactively. The hard byte cap is the memory bound;
// growth count remains diagnostic instead of being a fatal budget.
constexpr int REPLAY_SOLVER_SNAPSHOT_RESERVE_GROWTH_LIMIT =
    RuntimeAllocation::RUNTIME_RESERVE_REPLAY_GROWTH_LIMIT_UNBOUNDED;
constexpr uint32_t PHYSICS_TORNADO_WORKER_HASH = HashStr( "Frame/Physics/TornadoField/WorkerBodies" );
constexpr uint32_t PHYSICS_INTEGRATE_WORKER_HASH = HashStr( "Frame/Physics/Integrate/WorkerBodies" );

template <typename T> uint64_t VectorCapacityBytes( const std::vector<T>& values )
{
    return static_cast<uint64_t>( values.capacity() ) * static_cast<uint64_t>( sizeof( T ) );
}

bool IsSolverBodyFixed( std::span<const PhysicsBodyRecord> bodyRecords, int bodyIndex )
{
    return bodyRecords[static_cast<size_t>( bodyIndex )].isFixed;
}

const Vector3& SolverBodyPosition( std::span<const PhysicsBodyRecord> bodyRecords, int bodyIndex )
{
    return bodyRecords[static_cast<size_t>( bodyIndex )].position;
}

float SolverBodyRadius( std::span<const ColliderRecord> colliderRecords, int bodyIndex )
{
    return colliderRecords[static_cast<size_t>( bodyIndex )].boundingRadius;
}

bool IsPointJointBodyPair( const PhysicsBodyStore& bodyStore,
                           const std::vector<PointJointConstraint>& pointJointConstraints,
                           int bodyA,
                           int bodyB )
{
    if ( bodyA < 0 || bodyB < 0 || bodyA == bodyB )
    {
        return false;
    }
    if ( bodyA > bodyB )
    {
        std::swap( bodyA, bodyB );
    }
    for ( const PointJointConstraint& constraint : pointJointConstraints )
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




void IntegrateRemainingSolverBody( PhysicsBodyStore& bodyStore,
                                   const ColliderStore& colliderStore,
                                   std::span<const PhysicsBodyRecord> bodyRecords,
                                   const std::vector<uint8_t>& sleepState,
                                   const std::vector<float>& timeRemaining,
                                   int bodyIndex )
{
    if ( IsSolverBodyFixed( bodyRecords, bodyIndex ) )
    {
        return;
    }
    if ( sleepState[bodyIndex] )
    {
        return;
    }

    if ( timeRemaining[bodyIndex] > 0.0f )
    {
        (void)bodyStore.IntegrateBodyPose( colliderStore, bodyIndex, timeRemaining[bodyIndex] );
    }
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
    // Lane F: a partial solver snapshot cannot support deterministic replay
    // restore. Report the shared owner and cap before terminating.
    SB_FATAL( "Runtime/Replay/SolverSnapshot",
              "Replay solver snapshot reserve denied. owner=%s target=%s requested_capacity=%llu hard_bytes=%d",
              REPLAY_SOLVER_SNAPSHOT_RESERVE_OWNER,
              label ? label : "unknown",
              static_cast<unsigned long long>( requestedCapacity ),
              REPLAY_SOLVER_SNAPSHOT_RESERVE_HARD_BYTES );
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

} // namespace


void IntegrateRemainingStageContext::operator()( int bodyIndex ) const
{
    IntegrateRemainingSolverBody( bodyStore, colliderStore, bodyRecords, sleepState, timeRemaining, bodyIndex );
}

// Invariant: replay solver state fields live in these X-macro lists so capture
// clear/reserve/copy and restore copy cannot silently drift apart when solver
// state grows.
#define SB_REPLAY_SOLVER_DIRECT_VECTOR_FIELDS( VISIT )                                                                 \
    VISIT( timeRemaining, m_timeRemaining, "timeRemaining" )                                                           \
    VISIT( sleepSupportedThisFrame, m_sleepSupportedThisFrame, "sleepSupportedThisFrame" )                             \
    VISIT( sleepInhibitedThisFrame, m_sleepInhibitedThisFrame, "sleepInhibitedThisFrame" )                             \
    VISIT( sleepState, m_sleepState, "sleepState" )                                                                    \
    VISIT( sleepCounter, m_sleepCounter, "sleepCounter" )                                                              \
    VISIT( underwaterSleepLocked, m_underwaterSleepLocked, "underwaterSleepLocked" )                                   \
    VISIT( collisionVisualContacts, m_collisionVisualContacts, "collisionVisualContacts" )                             \
    VISIT( sleepIslandVisualId, m_sleepIslandVisualId, "sleepIslandVisualId" )                                         \
    VISIT( sleepIslandAssignedVisualId, m_sleepIslandAssignedVisualId, "sleepIslandAssignedVisualId" )                 \
    VISIT( sleepSupportEdges, m_sleepSupportEdges, "sleepSupportEdges" )                                               \
    VISIT( sleepIslandParent, m_sleepIslandParent, "sleepIslandParent" )                                               \
    VISIT( sleepIslandRank, m_sleepIslandRank, "sleepIslandRank" )                                                     \
    VISIT( sleepIslandHasAwake, m_sleepIslandHasAwake, "sleepIslandHasAwake" )                                         \
    VISIT( sleepIslandHasSupportAnchor, m_sleepIslandHasSupportAnchor, "sleepIslandHasSupportAnchor" )                 \
    VISIT( sleepIslandEligible, m_sleepIslandEligible, "sleepIslandEligible" )                                         \
    VISIT( sleepIslandCanSleep, m_sleepIslandCanSleep, "sleepIslandCanSleep" )                                         \
    VISIT( persistentContactCounts, m_persistentContactCounts, "persistentContactCounts" )                             \
    VISIT( persistentRestingContactCounts, m_persistentRestingContactCounts, "persistentRestingContactCounts" )        \
    VISIT( debugContacts, m_physicsDebugContacts, "debugContacts" )                                                    \
    VISIT( pipelineTrace, m_physicsPipelineTrace, "pipelineTrace" )                                                    \
    VISIT( collisionCellKeys, m_broadphase.CollisionCellKeysForReplay(), "collisionCellKeys" )

#define SB_REPLAY_SOLVER_TORNADO_VECTOR_FIELDS( VISIT )                                                                \
    VISIT( tornadoCaptureSeconds, m_tornadoGameplay.CaptureSeconds(), "tornadoCaptureSeconds" )                        \
    VISIT( tornadoEjectCooldownSeconds, m_tornadoGameplay.EjectCooldownSeconds(), "tornadoEjectCooldownSeconds" )

#define SB_REPLAY_SOLVER_CONVERTED_VECTOR_FIELDS( VISIT )                                                              \
    VISIT( persistentContacts, m_persistentContacts, "persistentContacts" )                                            \
    VISIT( persistentContactCache, m_persistentContactCache, "persistentContactCache" )

#define SB_REPLAY_SOLVER_VECTOR_FIELDS( VISIT )                                                                        \
    SB_REPLAY_SOLVER_DIRECT_VECTOR_FIELDS( VISIT )                                                                     \
    SB_REPLAY_SOLVER_TORNADO_VECTOR_FIELDS( VISIT )                                                                    \
    SB_REPLAY_SOLVER_CONVERTED_VECTOR_FIELDS( VISIT )

#define SB_REPLAY_PERSISTENT_CONTACT_SAMPLE_FIELDS( VISIT )                                                            \
    VISIT( bodyA )                                                                                                     \
    VISIT( bodyB )                                                                                                     \
    VISIT( featureId )                                                                                                 \
    VISIT( key )                                                                                                       \
    VISIT( normal )                                                                                                    \
    VISIT( tangent1 )                                                                                                  \
    VISIT( tangent2 )                                                                                                  \
    VISIT( rA )                                                                                                        \
    VISIT( rB )                                                                                                        \
    VISIT( penetration )                                                                                               \
    VISIT( normalMass )                                                                                                \
    VISIT( tangentMass1 )                                                                                              \
    VISIT( tangentMass2 )                                                                                              \
    VISIT( bias )                                                                                                      \
    VISIT( frictionLimit )                                                                                             \
    VISIT( accN )                                                                                                      \
    VISIT( accT1 )                                                                                                     \
    VISIT( accT2 )                                                                                                     \
    VISIT( warmStarted )                                                                                               \
    VISIT( isTerrain )                                                                                                 \
    VISIT( supportsRestingPolicy )                                                                                     \
    VISIT( allowsTangentFriction )                                                                                     \
    VISIT( normalCoupledFriction )                                                                                     \
    VISIT( inhibitsSleep )                                                                                             \
    VISIT( manifoldPointCount )                                                                                        \
    VISIT( terrainNormal )                                                                                             \
    VISIT( terrainWarmStart )

#define SB_REPLAY_CONTACT_CACHE_SAMPLE_FIELDS( VISIT )                                                                 \
    VISIT( key )                                                                                                       \
    VISIT( accN )                                                                                                      \
    VISIT( accT1 )                                                                                                     \
    VISIT( accT2 )

#define SB_REPLAY_SOLVER_STATS_FIELDS( VISIT )                                                                         \
    VISIT( rowCount )                                                                                                  \
    VISIT( cachePreviousRows )                                                                                         \
    VISIT( cacheHits )                                                                                                 \
    VISIT( cacheMisses )                                                                                               \
    VISIT( warmStartedRows )                                                                                           \
    VISIT( positionCorrectionRows )                                                                                    \
    VISIT( solverIterations )                                                                                          \
    VISIT( positionCorrectionTotal )                                                                                   \
    VISIT( positionCorrectionMax )


PhysicsWorld::PhysicsWorld()
    : m_seedSleepFrameCount( DEFAULT_PHYSICS_SLEEP_FRAMES )
{
    m_timeRemaining.reserve( SkullbonezCore::Scene::Capacity::MAX_GAME_MODELS );
    m_sleepSupportedThisFrame.reserve( SkullbonezCore::Scene::Capacity::MAX_GAME_MODELS );
    m_sleepInhibitedThisFrame.reserve( SkullbonezCore::Scene::Capacity::MAX_GAME_MODELS );
    m_sleepState.reserve( SkullbonezCore::Scene::Capacity::MAX_GAME_MODELS );
    m_sleepCounter.reserve( SkullbonezCore::Scene::Capacity::MAX_GAME_MODELS );
    m_underwaterSleepLocked.reserve( SkullbonezCore::Scene::Capacity::MAX_GAME_MODELS );
    m_collisionVisualContacts.reserve( SkullbonezCore::Scene::Capacity::MAX_GAME_MODELS );
    m_sleepIslandVisualId.reserve( SkullbonezCore::Scene::Capacity::MAX_GAME_MODELS );
    m_sleepIslandAssignedVisualId.reserve( SkullbonezCore::Scene::Capacity::MAX_GAME_MODELS );
    m_sleepSupportEdges.reserve( SkullbonezCore::Scene::Capacity::MAX_GAME_MODELS * 4 );
    m_sleepIslandParent.reserve( SkullbonezCore::Scene::Capacity::MAX_GAME_MODELS );
    m_sleepIslandRank.reserve( SkullbonezCore::Scene::Capacity::MAX_GAME_MODELS );
    m_sleepIslandHasAwake.reserve( SkullbonezCore::Scene::Capacity::MAX_GAME_MODELS );
    m_sleepIslandHasSupportAnchor.reserve( SkullbonezCore::Scene::Capacity::MAX_GAME_MODELS );
    m_sleepIslandEligible.reserve( SkullbonezCore::Scene::Capacity::MAX_GAME_MODELS );
    m_sleepIslandCanSleep.reserve( SkullbonezCore::Scene::Capacity::MAX_GAME_MODELS );
    m_sleepPointJointBody.reserve( SkullbonezCore::Scene::Capacity::MAX_GAME_MODELS );
    m_sleepIslandHasPointJoint.reserve( SkullbonezCore::Scene::Capacity::MAX_GAME_MODELS );
    m_sleepIslandPointJointsRelaxed.reserve( SkullbonezCore::Scene::Capacity::MAX_GAME_MODELS );
    m_sleepVisualIslandIds.reserve( SkullbonezCore::Scene::Capacity::MAX_GAME_MODELS );
    m_sleepVisualIslandBodies.reserve( SkullbonezCore::Scene::Capacity::MAX_GAME_MODELS );
    m_persistentContacts.reserve( SkullbonezCore::Scene::Capacity::MAX_GAME_MODELS * 4 );
    m_persistentContactCache.reserve( SkullbonezCore::Scene::Capacity::MAX_GAME_MODELS * 4 );
    m_persistentContactCounts.reserve( SkullbonezCore::Scene::Capacity::MAX_GAME_MODELS );
    m_persistentRestingContactCounts.reserve( SkullbonezCore::Scene::Capacity::MAX_GAME_MODELS );
    m_solverBodies.reserve( SkullbonezCore::Scene::Capacity::MAX_GAME_MODELS );
    m_physicsDebugContacts.reserve( SkullbonezCore::Scene::Capacity::MAX_GAME_MODELS * 4 );
    m_physicsPipelineTrace.reserve( MAX_PIPELINE_TRACE_RECORDS );
    m_persistentContactSideEffects.pipelineRecords.reserve( MAX_PIPELINE_TRACE_RECORDS );
    m_persistentContactSideEffects.collisionVisualBodies.reserve( PHYSICS_COLLISION_VISUAL_BODY_RESERVE );
    m_persistentContactSideEffects.fixedContactBodies.reserve( SkullbonezCore::Scene::Capacity::MAX_GAME_MODELS );
    m_persistentContactSideEffects.releaseWakeBodies.reserve( 8 );
    m_persistentContactSideEffects.fixedTreeReleases.reserve( 8 );
    m_restingWakeVisitedScratch.reserve( SkullbonezCore::Scene::Capacity::MAX_GAME_MODELS );
    m_restingWakeQueueScratch.reserve( SkullbonezCore::Scene::Capacity::MAX_GAME_MODELS );
    m_pointJointConstraints.reserve( SkullbonezCore::Scene::Capacity::MAX_GAME_MODELS );
}


void PhysicsWorld::ApplyRuntimeConfig( const SkullbonezCore::Core::EngineConfig& config )
{
    m_broadphase.ApplyRuntimeConfig( config );
    m_seedSleepFrameCount = static_cast<uint8_t>( (std::max)( 0, (std::min)( config.physicsSleep.frames, 255 ) ) );
}


void PhysicsWorld::Clear()
{
    m_timeRemaining.clear();
    m_forceStage.Clear();
    m_broadphase.Clear();
    m_sleepSupportedThisFrame.clear();
    m_sleepInhibitedThisFrame.clear();
    m_sleepState.clear();
    m_sleepCounter.clear();
    m_underwaterSleepLocked.clear();
    m_tornadoGameplay.Clear();
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
    m_terrain.Clear();
    m_narrowphase.Clear();
    m_pointJointConstraints.clear();
}


void PhysicsWorld::ReserveBodyScratchCapacity( std::size_t capacity )
{
    m_forceStage.ReserveBodyScratchCapacity( capacity );
}


void PhysicsWorld::CaptureReplaySolverSnapshot( ReplaySolverWorldSnapshot& outSnapshot, int modelCount ) const
{
    // Runtime allocation policy: replay recorder slots pre-reserve these
    // payload vectors outside gameplay. Capture clears the retained slot in
    // place so solver replay does not discard capacity and reallocate per tick.
#define CLEAR_REPLAY_SOLVER_VECTOR_FIELD( snapshotField, worldValues, label ) outSnapshot.snapshotField.clear();
    SB_REPLAY_SOLVER_VECTOR_FIELDS( CLEAR_REPLAY_SOLVER_VECTOR_FIELD )
#undef CLEAR_REPLAY_SOLVER_VECTOR_FIELD
    outSnapshot.solverStats = ReplaySolverStatsSample();

    outSnapshot.version = 2;
    outSnapshot.modelCount = modelCount;
    outSnapshot.nextSleepIslandVisualId = m_nextSleepIslandVisualId;
    outSnapshot.sleepEnabled = m_sleepEnabled;
    outSnapshot.collisionVisualFrameActive = m_collisionVisualFrameActive;
    outSnapshot.tornadoConfig = m_tornadoGameplay.GetFieldConfig();
    outSnapshot.tornadoSystemConfig = m_tornadoGameplay.GetSystemConfig();
    outSnapshot.tornadoSystemElapsedSeconds = m_tornadoGameplay.GetSystemElapsedSeconds();
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
#define INCLUDE_REPLAY_SOLVER_VECTOR_RESERVE( snapshotField, worldValues, label )                                      \
    includeSnapshotReserve( outSnapshot.snapshotField, worldValues.size() );
    SB_REPLAY_SOLVER_VECTOR_FIELDS( INCLUDE_REPLAY_SOLVER_VECTOR_RESERVE )
#undef INCLUDE_REPLAY_SOLVER_VECTOR_RESERVE

    const auto reserveSnapshotVectors = [&]()
    {
#define RESERVE_REPLAY_SOLVER_VECTOR_FIELD( snapshotField, worldValues, label )                                        \
    ReserveReplaySolverSnapshotVector( outSnapshot.snapshotField, worldValues.size(), label );
        SB_REPLAY_SOLVER_VECTOR_FIELDS( RESERVE_REPLAY_SOLVER_VECTOR_FIELD )
#undef RESERVE_REPLAY_SOLVER_VECTOR_FIELD
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
#define CAPTURE_REPLAY_SOLVER_VECTOR_FIELD( snapshotField, worldValues, label ) outSnapshot.snapshotField = worldValues;
    SB_REPLAY_SOLVER_DIRECT_VECTOR_FIELDS( CAPTURE_REPLAY_SOLVER_VECTOR_FIELD )
    SB_REPLAY_SOLVER_TORNADO_VECTOR_FIELDS( CAPTURE_REPLAY_SOLVER_VECTOR_FIELD )
#undef CAPTURE_REPLAY_SOLVER_VECTOR_FIELD

    for ( const PersistentContact& contact : m_persistentContacts )
    {
        ReplaySolverPersistentContactSample sample;
#define CAPTURE_REPLAY_CONTACT_SAMPLE_FIELD( field ) sample.field = contact.field;
        SB_REPLAY_PERSISTENT_CONTACT_SAMPLE_FIELDS( CAPTURE_REPLAY_CONTACT_SAMPLE_FIELD )
#undef CAPTURE_REPLAY_CONTACT_SAMPLE_FIELD
        outSnapshot.persistentContacts.push_back( sample );
    }

    for ( const PersistentContactCacheEntry& cache : m_persistentContactCache )
    {
        ReplaySolverContactCacheSample sample;
#define CAPTURE_REPLAY_CONTACT_CACHE_FIELD( field ) sample.field = cache.field;
        SB_REPLAY_CONTACT_CACHE_SAMPLE_FIELDS( CAPTURE_REPLAY_CONTACT_CACHE_FIELD )
#undef CAPTURE_REPLAY_CONTACT_CACHE_FIELD
        outSnapshot.persistentContactCache.push_back( sample );
    }

#define CAPTURE_REPLAY_SOLVER_STAT_FIELD( field ) outSnapshot.solverStats.field = m_persistentContactSolverStats.field;
    SB_REPLAY_SOLVER_STATS_FIELDS( CAPTURE_REPLAY_SOLVER_STAT_FIELD )
#undef CAPTURE_REPLAY_SOLVER_STAT_FIELD
}


bool PhysicsWorld::RestoreReplaySolverSnapshot( const ReplaySolverWorldSnapshot& snapshot, int modelCount )
{
    if ( snapshot.version < 1 || snapshot.version > 2 || snapshot.modelCount != modelCount )
    {
        return false;
    }

#define RESTORE_REPLAY_SOLVER_VECTOR_FIELD( snapshotField, worldValues, label ) worldValues = snapshot.snapshotField;
    SB_REPLAY_SOLVER_DIRECT_VECTOR_FIELDS( RESTORE_REPLAY_SOLVER_VECTOR_FIELD )
#undef RESTORE_REPLAY_SOLVER_VECTOR_FIELD
    m_nextSleepIslandVisualId = snapshot.nextSleepIslandVisualId;
    m_sleepEnabled = snapshot.sleepEnabled;
    m_collisionVisualFrameActive = snapshot.collisionVisualFrameActive;
    m_tornadoGameplay.SetReplayState( snapshot.tornadoCaptureSeconds,
                                      snapshot.tornadoEjectCooldownSeconds,
                                      snapshot.tornadoConfig,
                                      snapshot.tornadoSystemConfig,
                                      snapshot.tornadoSystemElapsedSeconds );

    m_persistentContacts.clear();
    m_persistentContacts.reserve( snapshot.persistentContacts.size() );
    for ( const ReplaySolverPersistentContactSample& sample : snapshot.persistentContacts )
    {
        PersistentContact contact;
#define RESTORE_REPLAY_CONTACT_SAMPLE_FIELD( field ) contact.field = sample.field;
        SB_REPLAY_PERSISTENT_CONTACT_SAMPLE_FIELDS( RESTORE_REPLAY_CONTACT_SAMPLE_FIELD )
#undef RESTORE_REPLAY_CONTACT_SAMPLE_FIELD
        m_persistentContacts.push_back( contact );
    }

    m_persistentContactCache.clear();
    m_persistentContactCache.reserve( snapshot.persistentContactCache.size() );
    for ( const ReplaySolverContactCacheSample& sample : snapshot.persistentContactCache )
    {
        PersistentContactCacheEntry cache;
#define RESTORE_REPLAY_CONTACT_CACHE_FIELD( field ) cache.field = sample.field;
        SB_REPLAY_CONTACT_CACHE_SAMPLE_FIELDS( RESTORE_REPLAY_CONTACT_CACHE_FIELD )
#undef RESTORE_REPLAY_CONTACT_CACHE_FIELD
        m_persistentContactCache.push_back( cache );
    }

    m_persistentContactSolverStats = PersistentContactSolverStats();
#define RESTORE_REPLAY_SOLVER_STAT_FIELD( field ) m_persistentContactSolverStats.field = snapshot.solverStats.field;
    SB_REPLAY_SOLVER_STATS_FIELDS( RESTORE_REPLAY_SOLVER_STAT_FIELD )
#undef RESTORE_REPLAY_SOLVER_STAT_FIELD

    m_solverBodies.clear();
    m_terrain.Clear();
    m_narrowphase.Clear();
    m_broadphase.ResetTransientAfterReplayRestore();
    return true;
}

#undef SB_REPLAY_SOLVER_DIRECT_VECTOR_FIELDS
#undef SB_REPLAY_SOLVER_TORNADO_VECTOR_FIELDS
#undef SB_REPLAY_SOLVER_CONVERTED_VECTOR_FIELDS
#undef SB_REPLAY_SOLVER_VECTOR_FIELDS
#undef SB_REPLAY_PERSISTENT_CONTACT_SAMPLE_FIELDS
#undef SB_REPLAY_CONTACT_CACHE_SAMPLE_FIELDS
#undef SB_REPLAY_SOLVER_STATS_FIELDS


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

    if ( !BuoyancySystem::RefreshUnderwaterSubmersionForBall( worldForces, bodyStore, colliderStore, index ) )
    {
        return;
    }
    PhysicsBodyRecord* record = bodyStore.MutableRecordForModelIndex( index );
    if ( !record || !BuoyancySystem::IsFullySubmergedBall( *record, colliderStore, index ) )
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


PersistentContactSolverContext
PhysicsWorld::CreatePersistentContactSolverContext( PhysicsBodyStore& bodyStore,
                                                    const ColliderStore& colliderStore,
                                                    const SkullbonezCore::Core::EngineConfig& config,
                                                    const PhysicsWorldForces& worldForces )
{
    const bool elasticCollisions = worldForces.mutualGravity.enabled && worldForces.mutualGravity.elasticCollisions;
    return PersistentContactSolverContext{ m_broadphase.GetCandidatePairs(),
                                           m_sleepState,
                                           m_sleepSupportEdges,
                                           m_persistentContacts,
                                           m_persistentContactCache,
                                           m_persistentContactSolverStats,
                                           m_persistentContactCounts,
                                           m_persistentRestingContactCounts,
                                           m_solverBodies,
                                           m_physicsDebugContacts,
                                           m_terrain.GetContactManifolds(),
                                           m_terrain.GetRestApplied(),
                                           m_sleepSupportedThisFrame,
                                           m_persistentContactSideEffects,
                                           bodyStore.MutableRecords(),
                                           colliderStore.Records(),
                                           bodyStore.Count(),
                                           (std::max)( 0,
                                                       static_cast<int>( MAX_PIPELINE_TRACE_RECORDS ) -
                                                           static_cast<int>( m_physicsPipelineTrace.size() ) ),
                                           elasticCollisions,
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
    assert( effects.collisionVisualBodies.capacity() >= m_broadphase.GetCandidatePairs().size() * 2 );
    assert( effects.fixedContactBodies.capacity() >= static_cast<std::size_t>( modelCount ) );
    assert( effects.releaseWakeBodies.capacity() >= 8 );
    assert( effects.fixedTreeReleases.capacity() >= 8 );
    assert( effects.pipelineRecords.capacity() >= static_cast<std::size_t>( pipelineCapacity ) );
    if ( effects.collisionVisualBodies.capacity() < m_broadphase.GetCandidatePairs().size() * 2 ||
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


void PhysicsWorld::DestroyPointJointsForBody( PhysicsBodyHandle body )
{
    // Invariant: remove every joint that names the retiring handle before the
    // body slot can be reused. Runtime joint rows are dense and are not retained
    // as stable identity outside the physics owner.
    for ( std::size_t index = 0; index < m_pointJointConstraints.size(); )
    {
        const PointJointConstraint& constraint = m_pointJointConstraints[index];
        if ( constraint.bodyA != body && constraint.bodyB != body )
        {
            ++index;
            continue;
        }

        if ( index + 1u != m_pointJointConstraints.size() )
        {
            m_pointJointConstraints[index] = m_pointJointConstraints.back();
        }
        m_pointJointConstraints.pop_back();
    }
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
                               const SkullbonezCore::Core::EngineConfig& config,
                               const PhysicsWorldForces& worldForces,
                               Threading::WorkerPool& workerPool )
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
    const auto bodyRecords = bodyStore.Records();
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
    m_terrain.BeginFrame();
    m_sleepSupportEdges.clear();
    m_diagnostics.BeginCollisionTimeFrame();

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

    RunSolverPhysics( bodyStore, colliderStore, fChangeInTime, config, worldForces, workerPool );
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
                                        int diagnosticNameCount,
                                        const PhysicsDiagnosticsCsvWriter& diagnosticsCsvWriter )
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
            const PhysicsDiagnosticsFrameInput frame{ diagnosticsView,
                                                      bodyStore,
                                                      colliderStore,
                                                      names,
                                                      diagnosticsCsvWriter,
                                                      fChangeInTime };
            if ( regressionLogEnabled )
            {
                m_diagnostics.EmitRegressionLog( frame );
            }
            if ( frameLogEnabled )
            {
                m_diagnostics.EmitFrame( frame );
            }
        }
        m_diagnostics.FlushCollisionTimes( diagnosticNames, diagnosticNameCount, diagnosticsCsvWriter );
        m_diagnostics.IncrementCollisionTimeFrameIfEnabled();
    }
#else
    (void)bodyStore;
    (void)colliderStore;
    (void)fChangeInTime;
    (void)diagnosticNames;
    (void)diagnosticNameCount;
    (void)diagnosticsCsvWriter;
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
                              std::span<const PhysicsBodyRecord> bodyRecords,
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
                refreshedSubmersion = BuoyancySystem::RefreshUnderwaterSubmersionForBall( *worldForces,
                                                                                          *bodyStore,
                                                                                          *colliderStore,
                                                                                          index );
            }
            const PhysicsBodyRecord* record = bodyStore->RecordForModelIndex( index );
            if ( record && colliderStore && ( refreshedSubmersion || record->submergedVolumePercent > 0.0f ) )
            {
                if ( BuoyancySystem::IsFullySubmergedBall( *record, *colliderStore, index ) )
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
void PhysicsWorld::SeedModelAsleep( int bodyCount, std::span<const PhysicsBodyRecord> bodyRecords, int index )
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


void PhysicsWorld::ApplyTornadoGameplay( PhysicsBodyStore& bodyStore,
                                         const ColliderStore& colliderStore,
                                         const PhysicsWorldForces& worldForces,
                                         float dt,
                                         const SkullbonezCore::Core::EngineConfig& runtimeConfig,
                                         Threading::WorkerPool& workerPool )
{
    const TornadoGameplayStepState stepState = m_tornadoGameplay.BeginStep( dt );
    if ( !stepState.active )
    {
        return;
    }

    PROFILE_SCOPED( "Frame/Physics/TornadoField" );
    const std::vector<int>& releaseWakeBodies = m_tornadoGameplay.ReleaseFixedBodies( stepState, bodyStore );
    for ( int releasedIndex : releaseWakeBodies )
    {
        WakeModel( bodyStore, colliderStore, worldForces, releasedIndex );
    }

    TornadoBodyForceContext tornadoBodyForceContext{ bodyStore,
                                                     colliderStore,
                                                     worldForces,
                                                     m_sleepState,
                                                     m_sleepCounter,
                                                     m_sleepIslandVisualId,
                                                     m_timeRemaining,
                                                     m_underwaterSleepLocked,
                                                     dt,
                                                     runtimeConfig,
                                                     workerPool,
                                                     PHYSICS_PARALLEL_MIN_BODIES,
                                                     "Frame/Physics/TornadoField/WorkerBodies",
                                                     PHYSICS_TORNADO_WORKER_HASH };
    m_tornadoGameplay.ApplyBodyForces( stepState, tornadoBodyForceContext );
}


void PhysicsWorld::SetTornadoFieldConfig( const TornadoFieldConfig& config )
{
    m_tornadoGameplay.SetFieldConfig( config );
}


const TornadoFieldConfig& PhysicsWorld::GetTornadoFieldConfig() const
{
    return m_tornadoGameplay.GetFieldConfig();
}


void PhysicsWorld::SetTornadoSystemConfig( const TornadoSystemConfig& config )
{
    m_tornadoGameplay.SetSystemConfig( config );
}


const TornadoSystemConfig& PhysicsWorld::GetTornadoSystemConfig() const
{
    return m_tornadoGameplay.GetSystemConfig();
}


float PhysicsWorld::GetTornadoSystemElapsedSeconds() const
{
    return m_tornadoGameplay.GetSystemElapsedSeconds();
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


void PhysicsWorld::EmitPhysicsCollisionTime( const char* type,
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
    m_diagnostics.QueueCollisionTime( type, bodyA, bodyB, collisionTime, availableTime );
}


void PhysicsWorld::PropagateSleepSupport( std::span<const PhysicsBodyRecord> bodyRecords )
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
                                         std::span<const PhysicsBodyRecord> bodyRecords,
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
                                          std::span<const PhysicsBodyRecord> bodyRecords,
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
                                         std::span<const PhysicsBodyRecord> bodyRecords,
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
                                             std::span<const PhysicsBodyRecord> bodyRecords,
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
    return IsPointJointBodyPair( bodyStore, m_pointJointConstraints, bodyA, bodyB );
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

    const auto bodyRecords = bodyStore.Records();
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



void PhysicsWorld::CommitObjectNarrowphaseEvent( const ObjectNarrowphaseEvent& event )
{
    if ( event.hasPipelineRecord )
    {
        RecordPhysicsPipelineStage( event.pipelineRecord );
    }
    if ( event.emitCollisionTime )
    {
        EmitPhysicsCollisionTime( "object",
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
        m_broadphase.AppendCollisionCellKey( event.collisionCellKey );
    }
}






void PhysicsWorld::RunSleepIslandStage( PhysicsBodyStore& bodyStore,
                                        const ColliderStore& colliderStore,
                                        const PhysicsWorldForces& worldForces,
                                        std::span<PhysicsBodyRecord> bodyRecords,
                                        int modelCount,
                                        float sleepLinearSq,
                                        float sleepAngularSq,
                                        uint8_t sleepFrames )
{
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

    // Concept: sleep island roots identify connected contact/joint groups, so
    // the sleep system can make one deactivation decision for the whole group.
    DisjointSet sleepIslands( m_sleepIslandParent, m_sleepIslandRank, modelCount );

    for ( const PersistentContact& c : m_persistentContacts )
    {
        // Persistent contacts are the solver's current dynamic contact graph, so
        // they are the natural edges for island sleep. Sleeping bodies still act
        // as graph anchors, but only awake bodies below participate in the current
        // eligibility and counter checks.
        if ( c.bodyA >= 0 && c.bodyA < modelCount && c.bodyB >= 0 && c.bodyB < modelCount )
        {
            sleepIslands.Unite( c.bodyA, c.bodyB );
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
        sleepIslands.Unite( a, b );
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
            sleepIslands.Unite( m_sleepVisualIslandBodies[visualSlot], x );
        }
        else
        {
            m_sleepVisualIslandIds.push_back( visualId );
            m_sleepVisualIslandBodies.push_back( x );
        }
    }

    for ( int x = 0; x < modelCount; ++x )
    {
        const int root = sleepIslands.Find( x );

        // A support anchor is evidence that this island is not a free-floating
        // collection of bodies that merely became numerically quiet. Terrain
        // support remains the usual anchor. Fixed objects and sleeping bodies are
        // also valid anchors: fixed objects are immovable world geometry, and a
        // sleeping dynamic body could only have reached sleep after satisfying the
        // same support gate in an earlier frame.
        if ( IsSolverBodyFixed( bodyRecords, x ) ||
             ( x < static_cast<int>( m_sleepState.size() ) && m_sleepState[x] != 0 ) ||
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
            m_sleepIslandPointJointsRelaxed[sleepIslands.Find( a )] = 0;
        }
    }

    for ( int x = 0; x < modelCount; ++x )
    {
        if ( IsSolverBodyFixed( bodyRecords, x ) )
        {
            continue;
        }
        if ( m_sleepState[x] )
        {
            continue;
        }

        const int root = sleepIslands.Find( x );
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
        float quietLinearSq = sleepLinearSq;
        float quietAngularSq = sleepAngularSq;
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
        return;
    }

    ApplySleepIslandTransitions( bodyStore,
                                 colliderStore,
                                 worldForces,
                                 bodyRecords,
                                 sleepIslands,
                                 modelCount,
                                 sleepFrames );
}


void PhysicsWorld::ApplySleepIslandTransitions( PhysicsBodyStore& bodyStore,
                                                const ColliderStore& colliderStore,
                                                const PhysicsWorldForces& worldForces,
                                                std::span<PhysicsBodyRecord> bodyRecords,
                                                DisjointSet& sleepIslands,
                                                int modelCount,
                                                uint8_t sleepFrames )
{
    // Invariant: RunSleepIslandStage has already populated the island eligibility
    // and anchor arrays. This helper only applies counters, visual ids, and the
    // final body sleep transition.

    for ( int x = 0; x < modelCount; ++x )
    {
        if ( IsSolverBodyFixed( bodyRecords, x ) )
        {
            continue;
        }
        if ( m_sleepState[x] )
        {
            continue;
        }

        const int root = sleepIslands.Find( x );
        if ( m_sleepIslandHasAwake[root] && m_sleepIslandEligible[root] )
        {
            if ( m_sleepCounter[x] < sleepFrames )
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
        if ( IsSolverBodyFixed( bodyRecords, x ) )
        {
            continue;
        }
        if ( m_sleepState[x] )
        {
            continue;
        }

        const int root = sleepIslands.Find( x );
        if ( m_sleepCounter[x] < sleepFrames )
        {
            // Every awake body in an eligible island must accumulate the full
            // quiet-frame count before any body in that island is deactivated.
            m_sleepIslandCanSleep[root] = 0;
        }
    }

    m_sleepIslandAssignedVisualId.assign( modelCount, 0 );
    for ( int x = 0; x < modelCount; ++x )
    {
        if ( IsSolverBodyFixed( bodyRecords, x ) )
        {
            continue;
        }
        if ( !m_sleepState[x] || m_sleepIslandVisualId[x] == 0 )
        {
            continue;
        }

        const int root = sleepIslands.Find( x );
        if ( m_sleepIslandAssignedVisualId[root] == 0 )
        {
            m_sleepIslandAssignedVisualId[root] = m_sleepIslandVisualId[x];
        }
    }

    for ( int x = 0; x < modelCount; ++x )
    {
        if ( IsSolverBodyFixed( bodyRecords, x ) )
        {
            continue;
        }
        if ( m_sleepState[x] )
        {
            continue;
        }

        const int root = sleepIslands.Find( x );
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
}


void PhysicsWorld::RunSolverPhysics( PhysicsBodyStore& bodyStore,
                                     const ColliderStore& colliderStore,
                                     float dt,
                                     const SkullbonezCore::Core::EngineConfig& config,
                                     const PhysicsWorldForces& worldForces,
                                     Threading::WorkerPool& workerPool )
{
    const auto bodyRecords = bodyStore.MutableRecords();
    const auto colliderRecords = colliderStore.Records();
    const int modelCount = (std::min)( { bodyStore.Count(),
                                         static_cast<int>( bodyRecords.size() ),
                                         static_cast<int>( colliderRecords.size() ) } );

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
    const float sleepLinear = (std::max)( 0.0f, config.physicsSleep.linearSpeed );
    const float sleepAngular = (std::max)( 0.0f, config.physicsSleep.angularSpeed );
    const float SLEEP_LINEAR_SQ = sleepLinear * sleepLinear;
    const float SLEEP_ANGULAR_SQ = sleepAngular * sleepAngular;
    const uint8_t SLEEP_FRAMES = static_cast<uint8_t>( (std::max)( 1, (std::min)( config.physicsSleep.frames, 255 ) ) );

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
    const Vector3* mutualGravityForces =
        m_forceStage.PrepareMutualGravityForces(
            bodyRecords, m_sleepState, modelCount, worldForces, config.physicsExecution, workerPool );
    ApplyForcesStageContext applyForcesStage{
        bodyStore,
        colliderStore,
        worldForces,
        bodyRecords,
        m_sleepState,
        m_timeRemaining,
        mutualGravityForces,
        dt,
    };
    m_forceStage.ApplyForces( applyForcesStage, modelCount, workerPool, config.physicsExecution );

    ApplyTornadoGameplay( bodyStore, colliderStore, worldForces, dt, config, workerPool );

    // Broadphase: build spatial grid from all object positions (include sleeping for wake detection)
    const float contactSkin = (std::max)( 0.0f, config.bodySimulation.contactEpsilon );
    const PhysicsBroadphaseStageContext broadphaseContext{ bodyStore,
                                                           bodyRecords,
                                                           colliderRecords,
                                                           config,
                                                           m_pointJointConstraints,
                                                           m_sleepState,
                                                           m_physicsPipelineTrace,
                                                           modelCount,
                                                           dt,
                                                           contactSkin };
    const std::span<const std::pair<int, int>> candidatePairs = m_broadphase.Run( broadphaseContext );

    // Object/object CCD front-end: wake sleepers and advance swept hits to a
    // contact candidate, but leave velocity response to the persistent rows.
    PROFILE_BEGIN( "Frame/Physics/Narrowphase" );
    float invCellSize = 1.0f / m_broadphase.GetCellSize();
    const int candidatePairCount = static_cast<int>( candidatePairs.size() );

    ObjectNarrowphasePairStageContext objectNarrowphasePairContext{ bodyStore,
                                                                    colliderStore,
                                                                    worldForces,
                                                                    bodyRecords,
                                                                    colliderRecords,
                                                                    candidatePairs,
                                                                    m_sleepState,
                                                                    m_sleepCounter,
                                                                    m_sleepIslandVisualId,
                                                                    m_timeRemaining,
                                                                    m_underwaterSleepLocked,
                                                                    m_persistentContactCache,
                                                                    modelCount,
                                                                    SLEEP_LINEAR_SQ,
                                                                    SLEEP_ANGULAR_SQ,
                                                                    contactSkin,
                                                                    config.bodySimulation.contactEpsilon,
                                                                    invCellSize,
                                                                    dt };

    const bool ranParallelNarrowphase = m_narrowphase.TryRunParallel( objectNarrowphasePairContext,
                                                                     candidatePairCount,
                                                                     modelCount,
                                                                     config.physicsExecution,
                                                                     workerPool );
    if ( ranParallelNarrowphase )
    {
        PROFILE_SCOPED( "Frame/Physics/Narrowphase/CommitEvents" );
        const std::span<const ObjectNarrowphaseEvent> events = m_narrowphase.GetEvents();
        for ( int pairIndex = 0; pairIndex < candidatePairCount; ++pairIndex )
        {
            CommitObjectNarrowphaseEvent( events[static_cast<size_t>( pairIndex )] );
        }
    }
    else
    {
        // Invariant: serial mode commits each pair's side effects immediately.
        // Deferring this loop would change what the next pair observes.
        PROFILE_SCOPED( "Frame/Physics/Narrowphase/SerialPairs" );
        for ( int pairIndex = 0; pairIndex < candidatePairCount; ++pairIndex )
        {
            ObjectNarrowphaseEvent event;
            m_narrowphase.ProcessObjectNarrowphasePair( objectNarrowphasePairContext, pairIndex, event );
            CommitObjectNarrowphaseEvent( event );
        }
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
    TerrainDetectionStageContext terrainDetectionContext{ bodyRecords,
                                                          colliderRecords,
                                                          config,
                                                          m_sleepState,
                                                          m_timeRemaining };
    TerrainCandidateCommitContext terrainCandidateCommitContext{ bodyStore,
                                                                 colliderStore,
                                                                 bodyRecords,
                                                                 colliderRecords,
                                                                 config,
                                                                 m_sleepSupportedThisFrame,
                                                                 m_sleepInhibitedThisFrame };
    m_terrain.Detect( terrainDetectionContext, modelCount, config.physicsExecution, workerPool );

    const std::span<const TerrainDetectionCandidate> terrainCandidates = m_terrain.GetDetectionCandidates();
    for ( int x = 0; x < modelCount; ++x )
    {
        const TerrainDetectionCandidate& candidate = terrainCandidates[static_cast<size_t>( x )];
        if ( candidate.tested )
        {
            const PreparedTerrainCandidateCommit commit = m_terrain.PrepareCandidateCommit(
                terrainCandidateCommitContext,
                x,
                candidate.availableTime,
                candidate.sweep );
            if ( commit.hit )
            {
                RecordPhysicsPipelineStage( commit.pipelineRecord );
                EmitPhysicsCollisionTime( "terrain", x, -1, commit.collisionTime, commit.availableTime );
                m_terrain.CommitCandidate( terrainCandidateCommitContext, commit );
                MarkCollisionVisualContact( x );
                m_timeRemaining[x] = commit.remainingTime;
            }
        }
    }
    PROFILE_END( "Frame/Physics/Terrain/Detect" );
    PROFILE_END( "Frame/Physics/Terrain" );

    PreparePersistentContactSideEffects( modelCount );
    PersistentContactSolverContext solverContext =
        CreatePersistentContactSolverContext( bodyStore, colliderStore, config, worldForces );
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
    IntegrateRemainingStageContext integrateRemainingStage{ bodyStore,
                                                            colliderStore,
                                                            bodyRecords,
                                                            m_sleepState,
                                                            m_timeRemaining };

    if ( config.physicsExecution.parallel && config.physicsExecution.parallelIntegrate )
    {
        workerPool.ParallelForNoAlloc( 0,
                                       modelCount,
                                       integrateRemainingStage,
                                       PHYSICS_PARALLEL_MIN_BODIES,
                                       "Frame/Physics/Integrate/WorkerBodies",
                                       PHYSICS_INTEGRATE_WORKER_HASH );
    }
    else
    {
        for ( int x = 0; x < modelCount; ++x )
        {
            integrateRemainingStage( x );
        }
    }

    RunSleepIslandStage( bodyStore,
                         colliderStore,
                         worldForces,
                         bodyRecords,
                         modelCount,
                         SLEEP_LINEAR_SQ,
                         SLEEP_ANGULAR_SQ,
                         SLEEP_FRAMES );
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
                                   m_broadphase.GetSpatialGrid(),
                                   m_broadphase.GetCandidatePairs(),
                                   m_broadphase.GetCollisionCellKeys(),
                                   m_sleepSupportEdges,
                                   m_sleepIslandVisualId,
                                   m_physicsPipelineTrace,
                                   m_terrain.GetContactManifolds() };
}

uint64_t PhysicsWorld::CollectMemoryBytes() const
{
    uint64_t bytes = static_cast<uint64_t>( sizeof( *this ) );
    bytes += m_forceStage.CollectDynamicMemoryBytes();
    bytes += m_broadphase.CollectDynamicMemoryBytes();
    bytes += VectorCapacityBytes( m_timeRemaining );
    bytes += VectorCapacityBytes( m_sleepSupportedThisFrame );
    bytes += VectorCapacityBytes( m_sleepInhibitedThisFrame );
    bytes += VectorCapacityBytes( m_sleepState );
    bytes += VectorCapacityBytes( m_sleepCounter );
    bytes += VectorCapacityBytes( m_underwaterSleepLocked );
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
    bytes += m_terrain.CollectDynamicMemoryBytes();
    bytes += m_narrowphase.CollectDynamicMemoryBytes();
    bytes += VectorCapacityBytes( m_restingWakeVisitedScratch );
    bytes += VectorCapacityBytes( m_restingWakeQueueScratch );
    bytes += VectorCapacityBytes( m_pointJointConstraints );
    bytes += m_tornadoGameplay.CollectMemoryBytes();
    return bytes;
}

uint64_t PhysicsWorld::CollectDebugAndBroadphaseMemoryBytes() const
{
    uint64_t bytes = m_broadphase.CollectDebugAndBroadphaseMemoryBytes();
    bytes += VectorCapacityBytes( m_collisionVisualContacts );
    bytes += VectorCapacityBytes( m_sleepIslandVisualId );
    bytes += VectorCapacityBytes( m_physicsDebugContacts );
    bytes += VectorCapacityBytes( m_physicsPipelineTrace );
    bytes += m_tornadoGameplay.CollectDebugMemoryBytes();
    return bytes;
}


const Math::CollisionDetection::SpatialGrid& PhysicsWorld::GetSpatialGrid() const
{
    return m_broadphase.GetSpatialGrid();
}


const std::vector<int64_t>& PhysicsWorld::GetCollisionCellKeys() const
{
    return m_broadphase.GetCollisionCellKeys();
}


const std::vector<uint8_t>& PhysicsWorld::GetCollisionVisualContacts() const
{
    return m_collisionVisualContacts;
}


std::span<const int> PhysicsWorld::GetFixedContactHighlightBodies() const
{
    return m_persistentContactSideEffects.fixedContactBodies;
}


std::span<const PhysicsFixedTreeReleaseEvent> PhysicsWorld::GetFixedTreeReleaseEvents() const
{
    return m_persistentContactSideEffects.fixedTreeReleases;
}


std::span<const uint8_t> PhysicsWorld::GetSleepStates() const
{
    return m_sleepState;
}


std::span<const int> PhysicsWorld::GetSleepIslandVisualIds() const
{
    return m_sleepIslandVisualId;
}


std::span<const uint8_t> PhysicsWorld::GetSleepSupportedStates() const
{
    return m_sleepSupportedThisFrame;
}


std::span<const uint8_t> PhysicsWorld::GetSleepInhibitedStates() const
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
