/*
File: SkullbonezSource/Physics/PhysicsWorld.cpp
Purpose:
  Sequences the fixed physics step and lifecycle of concrete stage owners.

Summary:
  PhysicsWorld.cpp is the composition and sequencing surface for the extracted
  force, broadphase, narrowphase, terrain, contact, sleep, and diagnostics
  owners. It retains only cross-stage clocks and top-level sibling lanes.

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
  Contact row: Persistent solver constraint row that applies one contact's
    normal and friction impulses.
  Point joint: Constraint that keeps two local anchor points close together
    without yet modelling a full hinge, cone, or motor.
  Sleep island: Connected body group that may deactivate only as a unit.
  Underwater sleep lock: Sleep policy that keeps fully submerged balls dormant
    so buoyancy jitter does not repeatedly wake them.
  X-macro field list: Preprocessor list invoked by several tiny visitors so
    replay capture and restore use the same ordered state inventory.
  PhysicsEngine: Step owner that supplies stores and handles model-order
    writeback after compact physics work finishes.
  Lane F: Fatal invariant lane for should-never-happen engine state.
  Mutual-gravity pair scratch: Triangular array with one force value for every
    `(i,j)` body pair, populated in parallel and replayed in serial model order.
  Awake index list: Ascending dense body rows owned by the sleep controller and
    borrowed by work-producing stages for one sequenced fixed-step interval.

Invariants:
  - Physics-visible behavior must remain deterministic; byte-exact baselines
    are the validation contract.
  - Fixed-capacity physics scratch buffers must not grow during gameplay; an
    exhausted reserve is a Lane F failure because continuing would either
    allocate on a hot path or silently drop deterministic side effects.
  - Mutual-gravity chunk scheduling may vary, but pair slots and the final
    triangular replay order never depend on worker count.
  - Parallel wake producers are flushed before the next awake-list consumer;
    worker scheduling never changes the ascending stage iteration order.

Related:
  - SkullbonezSource/Physics/PhysicsWorld.h
  - Agentic/Reference/physics-overview.md
  - Agentic/Reference/comment-style-guide.md
*/
#include "PhysicsWorld.h"
#include "../Core/Common.h"

#include "../Core/FatalError.h"
#include "BuoyancySystem.h"
#include "DisjointSet.h"
#include "PhysicsApi.h"
#include "PhysicsBodyStore.h"
#include "PhysicsSceneVectorReserve.h"
#include "SolverBroadphaseStage.h"
#include "PhysicsWorldForces.h"
#include "ColliderStore.h"
#include "ObjectContactManifold.h"
#include "../Core/Profiler.h"
#include "../Core/WorkerPool.h"
#include "../Core/Allocation/RuntimeAllocationTracker.h"
#include "../Core/Allocation/RuntimeReserveAllocator.h"

#include <algorithm>
#include <cassert>
#include <climits>
#include <cmath>
#include <cstdlib>
#include <cstdio>
#include <cstring>
#include <limits>

using namespace SkullbonezCore::Physics;
using SkullbonezCore::Math::Vector::Vector3;
using SkullbonezCore::Math::Vector::ZERO_VECTOR;
using SkullbonezCore::Physics::PHYSICS_SOLVER_SNAPSHOT_RESERVE_HARD_BYTES;
using SkullbonezCore::Physics::PHYSICS_SOLVER_SNAPSHOT_RESERVE_OWNER;
namespace Math = SkullbonezCore::Math;
namespace Physics = SkullbonezCore::Physics;
namespace Vector = SkullbonezCore::Math::Vector;
namespace CoreAllocation = SkullbonezCore::Core::Allocation;

namespace
{

// Why: worker fan-out is more expensive than the work for the validation-sized
// 300-body scenes. Keep all-body jobs inline until there is enough work per
// chunk for the persistent worker pool to pay for itself.
constexpr int PHYSICS_CANDIDATE_PAIR_RESERVE = SkullbonezCore::Scene::Capacity::MAX_SCENE_OBJECTS * 4;
constexpr int PHYSICS_COLLISION_VISUAL_BODY_RESERVE = PHYSICS_CANDIDATE_PAIR_RESERVE * 2;
constexpr std::size_t REPLAY_SOLVER_SNAPSHOT_VECTOR_INITIAL_CAPACITY = 1024u;
constexpr std::size_t REPLAY_SOLVER_SNAPSHOT_VECTOR_GROWTH_CHUNK = 4096u;

// Runtime allocation policy: replay prediction visualization can discover
// larger solver snapshots interactively. The hard byte cap is the memory bound;
// growth count remains diagnostic instead of being a fatal budget.
constexpr int REPLAY_SOLVER_SNAPSHOT_RESERVE_GROWTH_LIMIT = CoreAllocation::RUNTIME_RESERVE_REPLAY_GROWTH_LIMIT_UNBOUNDED;

#ifdef SKULLBONEZ_PROFILE_ENABLED
constexpr uint64_t LogicalStreamBytes( std::size_t elementBytes, uint64_t elementOperations )
{
    return static_cast<uint64_t>( elementBytes ) * elementOperations;
}

// Concept: this is a logical dense-stream census, not a hardware bandwidth
// counter. P4 moved sixteen bytes of guard/bookkeeping work from every scene
// row to the ascending awake set: steady sleep mirroring, CCD-clock reset,
// underwater census, and sleep transition guards. The remaining all-row term
// counts unavoidable island construction/support streams. Each operation is
// one array-element read or write in one pass.
constexpr uint64_t PHYSICS_ALL_BODY_LOGICAL_BYTES_PER_STEP = LogicalStreamBytes( sizeof( uint8_t ), 22u ) +
                                                             LogicalStreamBytes( sizeof( float ), 3u ) +
                                                             LogicalStreamBytes( sizeof( int ), 2u );

constexpr uint64_t PHYSICS_AWAKE_BOOKKEEPING_LOGICAL_BYTES = 16u;

// ApplyForces loads fourteen hot floats plus fixed, then stores six velocity
// floats: (14 + 6) * 4 + 1 = 81 logical bytes for each dynamic awake row.
constexpr uint64_t PHYSICS_FORCE_AWAKE_LOGICAL_BYTES = LogicalStreamBytes( sizeof( float ), 20u ) +
                                                       LogicalStreamBytes( sizeof( uint8_t ), 1u );

// IntegrateBodyPose loads and stores thirteen hot floats and reads fixed/awake:
// (13 + 13) * 4 + 2 = 106 logical bytes for each dynamic awake row.
constexpr uint64_t PHYSICS_INTEGRATE_AWAKE_LOGICAL_BYTES = LogicalStreamBytes( sizeof( float ), 26u ) +
                                                           LogicalStreamBytes( sizeof( uint8_t ), 2u );
static_assert( PHYSICS_ALL_BODY_LOGICAL_BYTES_PER_STEP == 42u );
static_assert( PHYSICS_AWAKE_BOOKKEEPING_LOGICAL_BYTES == 16u );
static_assert( PHYSICS_FORCE_AWAKE_LOGICAL_BYTES == 81u );
static_assert( PHYSICS_INTEGRATE_AWAKE_LOGICAL_BYTES == 106u );

// Cold records, colliders, grid entries, contacts, candidate pairs, cache-line
// amplification, and instruction fetch are excluded deliberately. Separate
// force/integration counts keep synchronous wake and end-step sleep transitions
// from attributing work to the wrong frame.
double EstimatePhysicsHotBytesPerBodyStep( int totalBodies, int forceAwakeBodies, int integrateAwakeBodies )
{

    if ( totalBodies <= 0 )
    {
        return 0.0;
    }

    const uint64_t totalIterations = static_cast<uint64_t>( totalBodies );
    const uint64_t forceAwakeIterations = static_cast<uint64_t>( (std::clamp)( forceAwakeBodies, 0, totalBodies ) );
    const uint64_t integrateAwakeIterations = static_cast<uint64_t>( (std::clamp)( integrateAwakeBodies, 0, totalBodies ) );

    const uint64_t logicalBytes = totalIterations * PHYSICS_ALL_BODY_LOGICAL_BYTES_PER_STEP +
                                  integrateAwakeIterations * PHYSICS_AWAKE_BOOKKEEPING_LOGICAL_BYTES +
                                  forceAwakeIterations * PHYSICS_FORCE_AWAKE_LOGICAL_BYTES +
                                  integrateAwakeIterations * PHYSICS_INTEGRATE_AWAKE_LOGICAL_BYTES;

    return static_cast<double>( logicalBytes ) / static_cast<double>( totalIterations );
}

#endif

template <typename T> uint64_t VectorCapacityBytes( const std::vector<T>& values )
{
    return static_cast<uint64_t>( values.capacity() ) * static_cast<uint64_t>( sizeof( T ) );
}

float SolverBodyRadius( std::span<const ColliderRecord> colliderRecords, int bodyIndex )
{
    return colliderRecords[static_cast<size_t>( bodyIndex )].boundingRadius;
}

bool IsPointJointBodyPair( const PhysicsBodyStore& bodyStore, const std::vector<PointJointConstraint>& pointJointConstraints,
                           int bodyA, int bodyB )
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


CoreAllocation::RuntimeReserveOwnerHandle ReplaySolverSnapshotReserveOwner()
{
    static const CoreAllocation::RuntimeReserveOwnerHandle owner = CoreAllocation::RuntimeReserveAllocator::RegisterOwner( { PHYSICS_SOLVER_SNAPSHOT_RESERVE_OWNER, CoreAllocation::RuntimeReserveSubsystem::Replay,
                                                                                                                             CoreAllocation::RuntimeReservePhase::Replay, 0, PHYSICS_SOLVER_SNAPSHOT_RESERVE_HARD_BYTES,
                                                                                                                             REPLAY_SOLVER_SNAPSHOT_RESERVE_GROWTH_LIMIT, true,
                                                                                                                             "solver replay snapshots reserve vector payload bytes through replay-only growth approval" } );

    return owner;
}

void ReportReplaySolverSnapshotReserveFailure( const char* label, std::size_t requestedCapacity )
{

    // Lane F: a partial solver snapshot cannot support deterministic replay
    // restore. Report the shared owner and cap before terminating.
    SB_FATAL( "Physics/SolverSnapshot",
              "Replay solver snapshot reserve denied. owner=%s target=%s requested_capacity=%llu hard_bytes=%d",
              PHYSICS_SOLVER_SNAPSHOT_RESERVE_OWNER, label ? label : "unknown",
              static_cast<unsigned long long>( requestedCapacity ), PHYSICS_SOLVER_SNAPSHOT_RESERVE_HARD_BYTES );
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

    const std::size_t doubled = values.capacity() > 0 ? values.capacity() * 2u
                                                      : REPLAY_SOLVER_SNAPSHOT_VECTOR_INITIAL_CAPACITY;

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


// Invariant: replay solver state fields live in these X-macro lists so capture
// clear/reserve/copy and restore copy cannot silently drift apart when solver
// state grows.
#define SB_REPLAY_SOLVER_DIRECT_VECTOR_FIELDS( VISIT )                                                                      \
    VISIT( timeRemaining, m_timeRemaining, "timeRemaining" )                                                                \
    VISIT( collisionCellKeys, m_broadphase.CollisionCellKeysForReplay(), "collisionCellKeys" )

#define SB_REPLAY_SOLVER_DIAGNOSTIC_VECTOR_FIELDS( VISIT )                                                                  \
    VISIT( collisionVisualContacts, m_stepDiagnostics.GetCollisionVisualContacts(), "collisionVisualContacts" )             \
    VISIT( debugContacts, m_stepDiagnostics.GetDebugContacts(), "debugContacts" )                                           \
    VISIT( pipelineTrace, m_stepDiagnostics.GetPipelineTrace(), "pipelineTrace" )

#define SB_REPLAY_SOLVER_SLEEP_VECTOR_FIELDS( VISIT )                                                                       \
    VISIT( sleepSupportedThisFrame, m_sleepController.GetSleepSupportedVector(), "sleepSupportedThisFrame" )                \
    VISIT( sleepInhibitedThisFrame, m_sleepController.GetSleepInhibitedVector(), "sleepInhibitedThisFrame" )                \
    VISIT( sleepState, m_sleepController.GetSleepStateVector(), "sleepState" )                                              \
    VISIT( sleepCounter, m_sleepController.GetSleepCounters(), "sleepCounter" )                                             \
    VISIT( underwaterSleepLocked, m_sleepController.GetUnderwaterSleepLockVector(), "underwaterSleepLocked" )               \
    VISIT( sleepIslandVisualId, m_sleepController.GetSleepIslandVisualIdVector(), "sleepIslandVisualId" )                   \
    VISIT( sleepIslandAssignedVisualId, m_sleepController.GetSleepIslandAssignedVisualIds(),                                \
           "sleepIslandAssignedVisualId" )                                                                                  \
    VISIT( sleepSupportEdges, m_sleepController.GetSleepSupportEdgeVector(), "sleepSupportEdges" )                          \
    VISIT( sleepIslandParent, m_sleepController.GetSleepIslandParents(), "sleepIslandParent" )                              \
    VISIT( sleepIslandRank, m_sleepController.GetSleepIslandRanks(), "sleepIslandRank" )                                    \
    VISIT( sleepIslandHasAwake, m_sleepController.GetSleepIslandHasAwake(), "sleepIslandHasAwake" )                         \
    VISIT( sleepIslandHasSupportAnchor, m_sleepController.GetSleepIslandHasSupportAnchor(), "sleepIslandHasSupportAnchor" ) \
    VISIT( sleepIslandEligible, m_sleepController.GetSleepIslandEligible(), "sleepIslandEligible" )                         \
    VISIT( sleepIslandCanSleep, m_sleepController.GetSleepIslandCanSleep(), "sleepIslandCanSleep" )

#define SB_REPLAY_SOLVER_CONTACT_STAGE_VECTOR_FIELDS( VISIT )                                                               \
    VISIT( persistentContactCounts, m_contactSolverStage.GetPersistentContactCounts(), "persistentContactCounts" )          \
    VISIT( persistentRestingContactCounts, m_contactSolverStage.GetPersistentRestingContactCounts(),                        \
           "persistentRestingContactCounts" )                                                                               \
    VISIT( persistentContacts, m_contactSolverStage.GetPersistentContacts(), "persistentContacts" )                         \
    VISIT( persistentContactCache, m_contactSolverStage.GetPersistentContactCache(), "persistentContactCache" )

#define SB_REPLAY_SOLVER_VECTOR_FIELDS( VISIT )                                                                             \
    SB_REPLAY_SOLVER_DIRECT_VECTOR_FIELDS( VISIT )                                                                          \
    SB_REPLAY_SOLVER_SLEEP_VECTOR_FIELDS( VISIT )                                                                           \
    SB_REPLAY_SOLVER_DIAGNOSTIC_VECTOR_FIELDS( VISIT )                                                                      \
    SB_REPLAY_SOLVER_CONTACT_STAGE_VECTOR_FIELDS( VISIT )


PhysicsWorld::PhysicsWorld() = default;

void PhysicsWorld::BindProfiler( SkullbonezCore::Core::Profiler* profiler ) noexcept
{
    m_profiler = profiler;
}

void PhysicsWorld::SetTerrainView( PhysicsTerrainView terrain ) noexcept
{
    m_terrainView = terrain;
}

void PhysicsWorld::ClearTerrainView() noexcept
{
    m_terrainView = {};
}


void PhysicsWorld::ApplyRuntimeSettings( const PhysicsRuntimeSettings& settings )
{
    m_broadphase.ApplyRuntimeSettings( settings.broadphase );
    m_sleepController.ApplyRuntimeSettings( settings.sleep );
}


void PhysicsWorld::Clear()
{
    m_timeRemaining.clear();
    m_lastTimeRemainingStep = 0.0f;
    m_lastTimeRemainingStepValid = false;
    m_underwaterSleepProbeNeeded = true;
    m_lastUnderwaterProbeFluidSurfaceHeight = 0.0f;
    m_lastUnderwaterProbeFluidSurfaceHeightValid = false;
    m_forceStage.Clear();
    m_externalForceStage.Clear();
    m_broadphase.Clear();
    m_sleepController.Clear();
    m_stepDiagnostics.Clear();
    m_contactSolverStage.Clear();
    m_terrain.Clear();
    m_narrowphase.Clear();
    m_pointJointConstraints.clear();
    AdvancePointJointHandleGeneration();
}


void PhysicsWorld::InvalidateBodyTopology()
{

    // Cold boundary: a same-count destroy/create sequence can replace dense
    // rows without exposing a count delta to the next fixed step. Rebuild both
    // derived awake indices and persistent grid membership from owner stores.
    m_sleepController.InvalidateBodyTopology();
    m_broadphase.InvalidateBodyTopology();
}


void PhysicsWorld::ReserveBodyScratchCapacity( std::size_t bodyCapacity, std::size_t pointJointCapacity )
{
    constexpr std::size_t bodyCeiling = SkullbonezCore::Scene::Capacity::MAX_SCENE_OBJECTS;
    ReservePhysicsSceneVector( m_timeRemaining, bodyCapacity, bodyCeiling, "PhysicsWorld.timeRemaining",
                               "Exact scene body rows for fixed-step time scratch" );

    ReservePhysicsSceneVector( m_pointJointConstraints, pointJointCapacity, bodyCeiling,
                               "PhysicsWorld.pointJointConstraints", "Exact current-scene point-joint backing allowance" );

    m_forceStage.ReserveBodyScratchCapacity( bodyCapacity );
    m_sleepController.ReserveBodyCapacity( bodyCapacity, pointJointCapacity );
    m_pointJointCapacity = pointJointCapacity;
}


std::size_t PhysicsWorld::PointJointCapacity() const noexcept
{
    return m_pointJointCapacity;
}


void PhysicsWorld::CaptureReplaySolverSnapshot( PhysicsSolverSnapshot& outSnapshot, int modelCount ) const
{

    // Runtime allocation policy: replay recorder slots pre-reserve these
    // payload vectors outside gameplay. Capture clears the retained slot in
    // place so solver replay does not discard capacity and reallocate per tick.
#define CLEAR_REPLAY_SOLVER_VECTOR_FIELD( snapshotField, worldValues, label ) outSnapshot.snapshotField.clear();
    SB_REPLAY_SOLVER_VECTOR_FIELDS( CLEAR_REPLAY_SOLVER_VECTOR_FIELD )
#undef CLEAR_REPLAY_SOLVER_VECTOR_FIELD
    outSnapshot.solverStats = PhysicsSolverStatsSample();

    outSnapshot.version = 2;
    outSnapshot.modelCount = modelCount;

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

#define INCLUDE_REPLAY_SOLVER_VECTOR_RESERVE( snapshotField, worldValues, label )                                           \
    includeSnapshotReserve( outSnapshot.snapshotField, worldValues.size() );
    SB_REPLAY_SOLVER_VECTOR_FIELDS( INCLUDE_REPLAY_SOLVER_VECTOR_RESERVE )
#undef INCLUDE_REPLAY_SOLVER_VECTOR_RESERVE

    const auto reserveSnapshotVectors = [&]()
    {
#define RESERVE_REPLAY_SOLVER_VECTOR_FIELD( snapshotField, worldValues, label )                                             \
    ReserveReplaySolverSnapshotVector( outSnapshot.snapshotField, worldValues.size(), label );
        SB_REPLAY_SOLVER_VECTOR_FIELDS( RESERVE_REPLAY_SOLVER_VECTOR_FIELD )
#undef RESERVE_REPLAY_SOLVER_VECTOR_FIELD
    };

    if ( snapshotNeedsGrowth )
    {

        if ( requestedSnapshotBytes > static_cast<uint64_t>( PHYSICS_SOLVER_SNAPSHOT_RESERVE_HARD_BYTES ) )
        {
            ReportReplaySolverSnapshotReserveFailure( "solverSnapshotBytes",
                                                      static_cast<std::size_t>( requestedSnapshotBytes ) );
        }

        const CoreAllocation::RuntimeReserveOwnerHandle owner = ReplaySolverSnapshotReserveOwner();
        const CoreAllocation::RuntimeReserveGrowthRequest request = { PHYSICS_SOLVER_SNAPSHOT_RESERVE_OWNER,
                                                                      "PhysicsSolverSnapshot",
                                                                      CoreAllocation::RuntimeReservePhase::Replay,
                                                                      modelCount,
                                                                      static_cast<int>( oldSnapshotBytes ),
                                                                      static_cast<int>( requestedSnapshotBytes ),
                                                                      1 };

        const CoreAllocation::RuntimeReserveGrowthResult
            result = CoreAllocation::RuntimeReserveAllocator::RequestGrowth( owner, request );

        if ( !result.granted )
        {
            ReportReplaySolverSnapshotReserveFailure( "solverSnapshotBytes",
                                                      static_cast<std::size_t>( requestedSnapshotBytes ) );
        }

        CoreAllocation::RuntimeAllocationScope replayAllocationScope( CoreAllocation::RuntimeAllocationPhase::Replay );
        CoreAllocation::RuntimeReserveOwnerScope ownerScope( owner );
        CoreAllocation::RuntimeReserveGrowthScope growthScope( owner, CoreAllocation::RuntimeReservePhase::Replay, result );

        reserveSnapshotVectors();
    }
    else
    {
        reserveSnapshotVectors();
    }

#define CAPTURE_REPLAY_SOLVER_VECTOR_FIELD( snapshotField, worldValues, label ) outSnapshot.snapshotField = worldValues;
    SB_REPLAY_SOLVER_DIRECT_VECTOR_FIELDS( CAPTURE_REPLAY_SOLVER_VECTOR_FIELD )
#undef CAPTURE_REPLAY_SOLVER_VECTOR_FIELD

    m_sleepController.CaptureReplayState( outSnapshot );
    m_stepDiagnostics.CaptureReplayState( outSnapshot );
    m_contactSolverStage.CaptureReplayState( outSnapshot );
}


bool PhysicsWorld::RestoreReplaySolverSnapshot( const PhysicsSolverSnapshot& snapshot, int modelCount )
{

    if ( snapshot.version < 1 || snapshot.version > 2 || snapshot.modelCount != modelCount )
    {
        return false;
    }

#define RESTORE_REPLAY_SOLVER_VECTOR_FIELD( snapshotField, worldValues, label ) worldValues = snapshot.snapshotField;
    SB_REPLAY_SOLVER_DIRECT_VECTOR_FIELDS( RESTORE_REPLAY_SOLVER_VECTOR_FIELD )
#undef RESTORE_REPLAY_SOLVER_VECTOR_FIELD

    m_sleepController.RestoreReplayState( snapshot );
    m_stepDiagnostics.RestoreReplayState( snapshot );
    m_contactSolverStage.RestoreReplayState( snapshot );
    m_terrain.Clear();
    m_narrowphase.Clear();
    m_broadphase.ResetTransientAfterReplayRestore();
    return true;
}

#undef SB_REPLAY_SOLVER_DIRECT_VECTOR_FIELDS
#undef SB_REPLAY_SOLVER_SLEEP_VECTOR_FIELDS
#undef SB_REPLAY_SOLVER_DIAGNOSTIC_VECTOR_FIELDS
#undef SB_REPLAY_SOLVER_CONTACT_STAGE_VECTOR_FIELDS
#undef SB_REPLAY_SOLVER_VECTOR_FIELDS


void PhysicsWorld::CommitContactSolverConsequences( PhysicsBodyStore& bodyStore, const ColliderStore& colliderStore,
                                                    std::span<BuoyancyBodyFacts> buoyancyFacts,
                                                    const PhysicsWorldForces& worldForces )
{
    const PersistentContactSolverSideEffects& effects = m_contactSolverStage.GetSideEffects();

    for ( const PhysicsPipelineRecord& record : effects.pipelineRecords )
    {
        m_stepDiagnostics.RecordPipelineStage( record );
    }

    for ( int index : effects.collisionVisualBodies )
    {
        m_stepDiagnostics.MarkCollisionVisualContact( index );
    }

    for ( int index : effects.releaseWakeBodies )
    {
        WakeModel( bodyStore, colliderStore, buoyancyFacts, worldForces, index );
    }
}


void PhysicsWorld::RunPhysics( PhysicsBodyStore& bodyStore, const ColliderStore& colliderStore,
                               std::span<BuoyancyBodyFacts> buoyancyFacts, float fChangeInTime,
                               const PhysicsRuntimeSettings& settings, const PhysicsWorldForces& worldForces,
                               const ExternalForceFrameInput& externalForces, Threading::WorkerPool& workerPool )
{

    // Concept: one fixed physics tick has a predictable data flow.
    //
    // 1. Resize/clear per-frame arrays so every model index has a slot.
    // 2. Reset debug, sleep-support, pipeline, and terrain-manifold output.
    // 3. Run broadphase, swept movement, terrain manifold generation, and the
    //    persistent Catto-style contact solver.
    // 4. Emit bounded Debug diagnostics before PhysicsEngine copies solved state
    //    into PhysicsBodyStore and invalidates cached model-order data at the
    //    scene owner boundary.
    //
    // Determinism note: changing this ordering can change byte-exact physics
    // baselines even when the final scene "looks" similar.
    const int modelCount = bodyStore.Count();

    if ( colliderStore.Count() != modelCount || static_cast<int>( buoyancyFacts.size() ) != modelCount )
    {
        SB_FATAL( "Physics/PhysicsWorld", "Aligned store mismatch before fixed step: bodies=%d colliders=%d buoyancy=%zu.",
                  modelCount, colliderStore.Count(), buoyancyFacts.size() );
    }

    const auto bodyRecords = bodyStore.Records();
    const bool timeStepChanged = !m_lastTimeRemainingStepValid || fChangeInTime != m_lastTimeRemainingStep;

    if ( static_cast<int>( m_timeRemaining.size() ) != modelCount || timeStepChanged )
    {

        // Cold topology/timestep boundary: preserve the old all-row value
        // contract for replay/diagnostics. Capacity is reserved before play;
        // ordinary same-dt steps below write only bodies that can consume it.
        m_timeRemaining.assign( static_cast<std::size_t>( modelCount ), fChangeInTime );
    }

    m_lastTimeRemainingStep = fChangeInTime;
    m_lastTimeRemainingStepValid = true;
    m_stepDiagnostics.BeginStep( modelCount );
    m_terrain.BeginFrame();
    const bool rebuiltAwakeList = m_sleepController.MirrorFlagsFrom( bodyStore, modelCount );
    const std::span<const int> awakeBodyIndices = m_sleepController.GetAwakeBodyIndices();

    for ( int bodyIndex : awakeBodyIndices )
    {
        m_timeRemaining[static_cast<std::size_t>( bodyIndex )] = fChangeInTime;
    }

    const bool fluidSurfaceHeightChanged = !m_lastUnderwaterProbeFluidSurfaceHeightValid ||
                                           worldForces.fluidSurfaceHeight != m_lastUnderwaterProbeFluidSurfaceHeight;

    m_lastUnderwaterProbeFluidSurfaceHeight = worldForces.fluidSurfaceHeight;
    m_lastUnderwaterProbeFluidSurfaceHeightValid = true;
    const bool probeDormantUnderwaterLocks = rebuiltAwakeList || m_underwaterSleepProbeNeeded || fluidSurfaceHeightChanged;

    m_underwaterSleepProbeNeeded = false;
    RunSolverPhysics( bodyStore, colliderStore, buoyancyFacts, fChangeInTime, settings, worldForces, externalForces,
                      workerPool, probeDormantUnderwaterLocks );
}


void PhysicsWorld::WakeModel( PhysicsBodyStore& bodyStore, int index )
{
    m_sleepController.WakeModel( bodyStore, m_contactSolverStage.CreateWakeAccess(),
                                 m_contactSolverStage.GetPersistentContacts(), m_pointJointConstraints, index );
}


void PhysicsWorld::WakeModel( PhysicsBodyStore& bodyStore, const ColliderStore& colliderStore,
                              std::span<BuoyancyBodyFacts> buoyancyFacts, const PhysicsWorldForces& worldForces, int index )
{
    m_sleepController.WakeModel( bodyStore, colliderStore, worldForces, buoyancyFacts, m_timeRemaining,
                                 m_contactSolverStage.CreateWakeAccess(), m_contactSolverStage.GetPersistentContacts(),
                                 m_pointJointConstraints, index );
}


void PhysicsWorld::SeedModelAsleep( const PhysicsBodyStore& bodyStore, int index )
{
    m_sleepController.SeedModelAsleep( bodyStore, index );

    // Seed commands do not borrow world forces/colliders. The next fixed step
    // performs the one cold underwater-lock probe that transition-driven sleep
    // performs immediately in ApplyTransitions.
    m_underwaterSleepProbeNeeded = true;
}


void PhysicsWorld::SetPhysicsSleepEnabled( bool enabled )
{
    m_sleepController.SetPhysicsSleepEnabled( enabled );
}


bool PhysicsWorld::IsPhysicsSleepEnabled() const
{
    return m_sleepController.IsPhysicsSleepEnabled();
}


void PhysicsWorld::ApplyExternalForces( PhysicsBodyStore& bodyStore, const ColliderStore& colliderStore,
                                        std::span<BuoyancyBodyFacts> buoyancyFacts, const PhysicsWorldForces& worldForces,
                                        const ExternalForceFrameInput& input, const PhysicsExecutionSettings& execution,
                                        Threading::WorkerPool& workerPool )
{

    if ( !input.Active() )
    {
        return;
    }

    PROFILE_SCOPED( m_profiler, "Frame/Physics/ExternalForceField" );
    const std::span<const int> releaseWakeBodies = m_externalForceStage.ReleaseFixedBodies( input, bodyStore );

    for ( int releasedIndex : releaseWakeBodies )
    {
        WakeModel( bodyStore, colliderStore, buoyancyFacts, worldForces, releasedIndex );
    }

    m_externalForceStage.ApplyBodyForces( input, bodyStore, colliderStore,
                                          m_sleepController.CreateNarrowphaseWakeAccess( bodyStore, colliderStore,
                                                                                         m_terrainView, worldForces,
                                                                                         buoyancyFacts,
                                                                                         bodyStore.MutableRecords(),
                                                                                         m_timeRemaining, bodyStore.Count(),
                                                                                         input.stepSeconds ),
                                          execution, workerPool );
}


void PhysicsWorld::CommitObjectNarrowphaseEvent( const ObjectNarrowphaseEvent& event )
{

    if ( event.hasPipelineRecord )
    {
        m_stepDiagnostics.RecordPipelineStage( event.pipelineRecord );
    }

    if ( event.emitCollisionTime )
    {
#ifdef _DEBUG
        const bool diagnosticsSuppressed = m_diagnosticsSuppressed;
#else
        constexpr bool diagnosticsSuppressed = false;
#endif

        m_stepDiagnostics.EmitCollisionTime( diagnosticsSuppressed, "object", event.collisionTimeBodyA,
                                             event.collisionTimeBodyB, event.collisionTime, event.availableTime );
    }

    if ( event.markVisualContact )
    {
        m_stepDiagnostics.MarkCollisionVisualContact( event.visualBodyA );
        m_stepDiagnostics.MarkCollisionVisualContact( event.visualBodyB );
    }

    if ( event.hasCollisionCellKey )
    {
        m_broadphase.AppendCollisionCellKey( event.collisionCellKey );
    }
}


void PhysicsWorld::RunSolverPhysics( PhysicsBodyStore& bodyStore, const ColliderStore& colliderStore,
                                     std::span<BuoyancyBodyFacts> buoyancyFacts, float dt,
                                     const PhysicsRuntimeSettings& settings, const PhysicsWorldForces& worldForces,
                                     const ExternalForceFrameInput& externalForces, Threading::WorkerPool& workerPool,
                                     bool probeDormantUnderwaterLocks )
{
    const auto bodyRecords = bodyStore.MutableRecords();
    const PhysicsBodyHotFieldsConstView hotFields = bodyStore.HotFields();
    const auto colliderRecords = colliderStore.Records();
    const int modelCount = (std::min)( { bodyStore.Count(), static_cast<int>( bodyRecords.size() ),
                                         static_cast<int>( colliderRecords.size() ),
                                         static_cast<int>( buoyancyFacts.size() ) } );

    const std::span<const uint8_t> sleepStates = m_sleepController.GetSleepStates();
    std::span<const int> awakeBodyIndices = m_sleepController.GetAwakeBodyIndices();

    // Sleep owns threshold interpretation and returns the exact squared-speed
    // and counter values consumed by narrowphase and island transitions.
    // PhysicsWorld only sequences that typed policy across the two consumers.
    const PhysicsSleepStepPolicy sleepPolicy = m_sleepController.ResolveStepPolicy( settings.sleep );

    if ( probeDormantUnderwaterLocks )
    {

        // Cold/explicit-seed boundary only. Ordinary island transitions probe
        // the exact body as it sleeps, so dormant rows have zero steady cost.

        for ( int x = 0; x < modelCount; ++x )
        {

            if ( sleepStates[x] )
            {
                m_sleepController.LockUnderwaterSleeperIfReady( worldForces, bodyStore, colliderStore, buoyancyFacts,
                                                                m_timeRemaining, x );
            }
        }
    }

    // Sleeping bodies keep cached state until a contact or scene change wakes
    // them, so force integration only runs for awake rows.
    const Vector3* mutualGravityForces = m_forceStage.PrepareMutualGravityForces( m_profiler, bodyRecords, hotFields,
                                                                                  sleepStates, modelCount, worldForces,
                                                                                  settings.execution, workerPool );

#ifdef SKULLBONEZ_PROFILE_ENABLED
    const int forceAwakeBodyCount = static_cast<int>( awakeBodyIndices.size() );
#endif
    PROFILE_BEGIN( m_profiler, "Frame/Physics/ApplyForces" );
    m_forceStage.ApplyForces( bodyStore, colliderStore, m_terrainView, worldForces, buoyancyFacts, sleepStates,
                              m_timeRemaining, mutualGravityForces, dt, awakeBodyIndices, workerPool, settings.execution );

    PROFILE_END( m_profiler, "Frame/Physics/ApplyForces" );

    ApplyExternalForces( bodyStore, colliderStore, buoyancyFacts, worldForces, externalForces, settings.execution,
                         workerPool );

    m_sleepController.FlushPendingAwakeBodyIndices();
    awakeBodyIndices = m_sleepController.GetAwakeBodyIndices();

    // Broadphase: sleeping membership remains resident, while awake rows update
    // their ranges and source awake-to-sleep wake-detection pairs.
    const float contactSkin = (std::max)( 0.0f, settings.body.contactEpsilon );
    const std::span<const std::pair<int, int>> candidatePairs = m_broadphase.Run( bodyStore, colliderStore,
                                                                                  settings.broadphase,
                                                                                  m_pointJointConstraints, sleepStates,
                                                                                  awakeBodyIndices, m_stepDiagnostics, dt,
                                                                                  contactSkin, settings.body.contactEpsilon,
                                                                                  m_profiler );

    // Object/object CCD front-end: wake sleepers and advance swept hits to a
    // contact candidate, but leave velocity response to the persistent rows.
    PROFILE_BEGIN( m_profiler, "Frame/Physics/Narrowphase" );
    float invCellSize = 1.0f / m_broadphase.GetCellSize();
    const int candidatePairCount = static_cast<int>( candidatePairs.size() );

    const PhysicsNarrowphaseWakeAccess narrowphaseWake = m_sleepController.CreateNarrowphaseWakeAccess( bodyStore,
                                                                                                        colliderStore,
                                                                                                        m_terrainView,
                                                                                                        worldForces,
                                                                                                        buoyancyFacts,
                                                                                                        bodyRecords,
                                                                                                        m_timeRemaining,
                                                                                                        modelCount, dt );

    const ObjectNarrowphaseStepPolicy narrowphasePolicy { sleepPolicy.linearSpeedSquared,
                                                          sleepPolicy.angularSpeedSquared,
                                                          contactSkin,
                                                          settings.body.contactEpsilon,
                                                          invCellSize,
                                                          dt,
                                                          settings.execution.parallel,
                                                          settings.execution.parallelNarrowphase };

    const bool ranParallelNarrowphase = m_narrowphase.TryRunParallel( bodyStore, colliderStore, m_terrainView, buoyancyFacts,
                                                                      candidatePairs, narrowphaseWake, m_timeRemaining,
                                                                      m_contactSolverStage.GetPersistentContactCache(),
                                                                      narrowphasePolicy, m_profiler, workerPool );

    if ( ranParallelNarrowphase )
    {
        PROFILE_SCOPED( m_profiler, "Frame/Physics/Narrowphase/CommitEvents" );
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
        PROFILE_SCOPED( m_profiler, "Frame/Physics/Narrowphase/SerialPairs" );

        for ( int pairIndex = 0; pairIndex < candidatePairCount; ++pairIndex )
        {
            ObjectNarrowphaseEvent event;
            m_narrowphase.ProcessObjectNarrowphasePair( bodyStore, colliderStore, m_terrainView, buoyancyFacts,
                                                        candidatePairs, narrowphaseWake, m_timeRemaining,
                                                        m_contactSolverStage.GetPersistentContactCache(), narrowphasePolicy,
                                                        m_profiler, pairIndex, event );

            CommitObjectNarrowphaseEvent( event );
        }
    }

    PROFILE_END( m_profiler, "Frame/Physics/Narrowphase" );
    m_sleepController.FlushPendingAwakeBodyIndices();
    awakeBodyIndices = m_sleepController.GetAwakeBodyIndices();

    // Terrain phase ownership:
    //   1. Keep swept terrain detection here so fast bodies still stop at the
    //      correct time of impact.
    //   2. Convert the hit into a terrain manifold only. Do not apply impulses
    //      or terrain-only velocity response in this phase.
    //   3. Leave remaining-time integration and all normal/friction response to
    //      the shared persistent contact rows below.
    PROFILE_BEGIN( m_profiler, "Frame/Physics/Terrain" );
    PROFILE_BEGIN( m_profiler, "Frame/Physics/Terrain/Detect" );
    m_terrain.Detect( bodyStore, colliderStore, buoyancyFacts, m_terrainView, settings, sleepStates, m_timeRemaining,
                      m_profiler, awakeBodyIndices, settings.execution, workerPool );

    const std::span<const TerrainDetectionCandidate> terrainCandidates = m_terrain.GetDetectionCandidates();

    for ( int x : awakeBodyIndices )
    {
        const TerrainDetectionCandidate& candidate = terrainCandidates[static_cast<size_t>( x )];

        if ( candidate.tested )
        {
            const PreparedTerrainCandidateCommit commit = m_terrain.PrepareCandidateCommit( bodyStore, colliderStore,
                                                                                            m_terrainView, buoyancyFacts,
                                                                                            settings, m_profiler, x,
                                                                                            candidate.availableTime,
                                                                                            candidate.sweep );

            if ( commit.hit )
            {
                m_stepDiagnostics.RecordPipelineStage( commit.pipelineRecord );
#ifdef _DEBUG
                const bool diagnosticsSuppressed = m_diagnosticsSuppressed;
#else
                constexpr bool diagnosticsSuppressed = false;
#endif

                m_stepDiagnostics.EmitCollisionTime( diagnosticsSuppressed, "terrain", x, -1, commit.collisionTime,
                                                     commit.availableTime );

                m_terrain.CommitCandidate( commit, m_sleepController.MutableSupportedStatesForTerrain(),
                                           m_sleepController.MutableInhibitedStatesForTerrain() );

                m_stepDiagnostics.MarkCollisionVisualContact( x );
                m_timeRemaining[x] = commit.remainingTime;
            }
        }
    }

    PROFILE_END( m_profiler, "Frame/Physics/Terrain/Detect" );
    PROFILE_END( m_profiler, "Frame/Physics/Terrain" );

    const PersistentContactSolverStepPolicy contactPolicy = PhysicsContactSolverStage::ResolveStepPolicy( settings,
                                                                                                          worldForces );

    m_contactSolverStage.Solve( bodyStore, colliderStore, contactPolicy, candidatePairs, sleepStates,
                                m_sleepController.MutableSupportEdgesForContactSolver(), m_terrain.GetContactManifolds(),
                                m_terrain.GetRestApplied(), m_sleepController.MutableSupportedStatesForTerrain(),
                                m_stepDiagnostics, dt, m_profiler );

    CommitContactSolverConsequences( bodyStore, colliderStore, buoyancyFacts, worldForces );
    m_sleepController.WakePointJointConnectedBodies( bodyStore, colliderStore, m_terrainView, worldForces, buoyancyFacts,
                                                     m_timeRemaining, m_contactSolverStage.CreateWakeAccess(),
                                                     m_pointJointConstraints, dt );

    (void)Ragdoll::SolvePointJoints( bodyStore, m_pointJointConstraints, m_sleepController.GetSleepStates(), dt );
    m_sleepController.AppendPointJointSupportEdges( bodyStore, m_pointJointConstraints, modelCount );

    // Object contacts are converted into stack support only after terrain
    // response has had a chance to seed true support for this frame.
    m_sleepController.PropagateSupport( bodyStore );
    awakeBodyIndices = m_sleepController.GetAwakeBodyIndices();

    // Integrate remaining time for awake models.
    PROFILE_BEGIN( m_profiler, "Frame/Physics/Integrate" );
#ifdef SKULLBONEZ_PROFILE_ENABLED
    const int integrateAwakeBodyCount = static_cast<int>( awakeBodyIndices.size() );
#endif
    m_forceStage.IntegrateRemaining( bodyStore, m_profiler, colliderStore, m_terrainView, buoyancyFacts,
                                     m_sleepController.GetSleepStates(), m_timeRemaining, awakeBodyIndices, workerPool,
                                     settings.execution );

    m_sleepController.RunIslandStage( bodyStore, colliderStore, worldForces, buoyancyFacts, m_timeRemaining,
                                      m_contactSolverStage.GetPersistentContacts(),
                                      m_contactSolverStage.GetPersistentRestingContactCounts(), m_pointJointConstraints,
                                      m_stepDiagnostics.MutablePipelineTrace(), sleepPolicy );

    PROFILE_END( m_profiler, "Frame/Physics/Integrate" );

#ifdef SKULLBONEZ_PROFILE_ENABLED

    // Invariant: scale counters sample the completed fixed step. Perf scenes
    // run exactly one fixed step per render frame, so the profiler's last-value
    // counter semantics map one-to-one onto the measurement ledger.
    const int awakeBodyCount = m_sleepController.GetAwakeBodyCount();
    PROFILE_COUNTER( m_profiler, "Counter/Physics/TotalBodies", modelCount );
    PROFILE_COUNTER( m_profiler, "Counter/Physics/AwakeBodies", awakeBodyCount );

    // Concept: persistent solver rows are the compact contact work unit. This
    // count is more actionable in an external capture than object-pair guesses
    // and is sampled after the solver finishes owning the row set.
    PROFILE_COUNTER( m_profiler, "Counter/Physics/PersistentContactRows", m_contactSolverStage.GetStats().rowCount );

    // P2 contract: a body counts only when its integer cell range changes.
    // First insertion and swept-overlay cells have separate meanings and do not
    // inflate this steady-step maintenance witness.
    PROFILE_COUNTER( m_profiler, "Counter/Physics/BodiesReinserted",
                     m_broadphase.GetSpatialGrid().GetMaintenanceStats().movedBodies );

    PROFILE_COUNTER( m_profiler, "Counter/Physics/EstimatedHotBytesPerBodyStep",
                     EstimatePhysicsHotBytesPerBodyStep( modelCount, forceAwakeBodyCount, integrateAwakeBodyCount ) );

#endif
}

void PhysicsWorld::BeginCollisionVisualFrame( int modelCount )
{
    m_stepDiagnostics.BeginCollisionVisualFrame( modelCount );
    m_sleepController.EnsureVisualIdSize( modelCount );
}


void PhysicsWorld::EndCollisionVisualFrame()
{
    m_stepDiagnostics.EndCollisionVisualFrame();
}


void PhysicsWorld::ClearPointJointConstraints()
{
    m_pointJointConstraints.clear();
    AdvancePointJointHandleGeneration();
}


void PhysicsWorld::AdvancePointJointHandleGeneration()
{
    ++m_pointJointHandleGeneration;

    if ( m_pointJointHandleGeneration == 0u )
    {
        m_pointJointHandleGeneration = PHYSICS_HANDLE_INITIAL_GENERATION;
    }

    m_nextPointJointHandleIndex = 0u;
}


void PhysicsWorld::DestroyPointJointsForBody( PhysicsBodyHandle body )
{

    // Invariant: remove every joint that names the retiring handle before the
    // body slot can be reused. Runtime joint rows are dense, but moving a row
    // retains its stable handle so unrelated callers are never retargeted.

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
        return PhysicsConstraintHandle {};
    }

    if ( m_pointJointConstraints.size() >= m_pointJointCapacity )
    {
        SB_FATAL( "Physics/PointJoint",
                  "Point-joint scene capacity exhausted: owner=Physics/PhysicsWorld requested=%zu capacity=%zu "
                  "retained_capacity=%zu phase=scene_topology.",
                  m_pointJointConstraints.size() + 1u, m_pointJointCapacity, m_pointJointConstraints.capacity() );
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

    // Lane F: exhausting the monotonic handle space would let a stale command
    // retarget a new joint. A scene cannot approach this limit legitimately.

    if ( m_nextPointJointHandleIndex == ( std::numeric_limits<uint32_t>::max )() )
    {
        SB_FATAL( "Physics/PointJoint", "Constraint handle index exhausted before a lifecycle clear" );
    }

    PhysicsConstraintHandle handle;
    handle.index = m_nextPointJointHandleIndex++;
    handle.generation = m_pointJointHandleGeneration;
    constraint.handle = handle;
    m_pointJointConstraints.push_back( constraint );
    return handle;
}


bool PhysicsWorld::UpdatePointJoint( const PhysicsPointJointUpdateDesc& desc )
{
    const auto found = std::find_if( m_pointJointConstraints.begin(), m_pointJointConstraints.end(),
                                     [&]( const PointJointConstraint& constraint )
                                     { return constraint.handle == desc.constraint; } );

    if ( found == m_pointJointConstraints.end() )
    {
        return false;
    }

    PointJointConstraint& joint = *found;

    if ( desc.updateMask & PHYSICS_POINT_JOINT_UPDATE_BODIES )
    {
        joint.SetBodies( desc.bodyA, desc.bodyB );
    }

    if ( desc.updateMask & PHYSICS_POINT_JOINT_UPDATE_ANCHORS )
    {
        joint.localAnchorA = desc.localAnchorA;
        joint.localAnchorB = desc.localAnchorB;
    }

    if ( desc.updateMask & PHYSICS_POINT_JOINT_UPDATE_SOLVER )
    {
        joint.slack = desc.slack;
        joint.stiffness = desc.stiffness;
        joint.damping = desc.damping;
    }

    if ( desc.updateMask & PHYSICS_POINT_JOINT_UPDATE_GROUP )
    {
        joint.groupId = desc.groupId;
        joint.flags = desc.flags;
    }

    return true;
}


bool PhysicsWorld::DestroyConstraint( PhysicsConstraintHandle constraint )
{
    const auto found = std::find_if( m_pointJointConstraints.begin(), m_pointJointConstraints.end(),
                                     [&]( const PointJointConstraint& joint ) { return joint.handle == constraint; } );

    if ( found == m_pointJointConstraints.end() )
    {
        return false;
    }

    // Invariant: compaction moves the complete row, including its stable
    // handle, so a surviving constraint keeps its identity.

    if ( found + 1 != m_pointJointConstraints.end() )
    {
        *found = m_pointJointConstraints.back();
    }

    m_pointJointConstraints.pop_back();
    return true;
}


const std::vector<PointJointConstraint>& PhysicsWorld::GetPointJointConstraints() const
{
    return m_pointJointConstraints;
}


bool PhysicsWorld::ShouldEmitStepDiagnostics() const
{
#ifdef _DEBUG
    return m_stepDiagnostics.ShouldEmitStepDiagnostics( m_diagnosticsSuppressed );
#else
    return false;
#endif
}


bool PhysicsWorld::ShouldEmitCollisionTimeDiagnostics() const
{
#ifdef _DEBUG
    return m_stepDiagnostics.ShouldEmitCollisionTimeDiagnostics( m_diagnosticsSuppressed );
#else
    return false;
#endif
}


void PhysicsWorld::SetDiagnosticNames( std::span<const char* const> diagnosticNames )
{
    m_stepDiagnostics.SetDiagnosticNames( diagnosticNames );
}


void PhysicsWorld::EmitStepDiagnostics( const PhysicsBodyStore& bodyStore, const ColliderStore& colliderStore,
                                        float fChangeInTime, const PhysicsDiagnosticsCsvWriter& diagnosticsCsvWriter )
{
#ifdef _DEBUG
    const PhysicsDiagnosticsView diagnosticsView = GetDiagnosticsView();
    m_stepDiagnostics.EmitStepDiagnostics( m_diagnosticsSuppressed, diagnosticsView, bodyStore, colliderStore, fChangeInTime,
                                           diagnosticsCsvWriter );

#else
    (void)bodyStore;
    (void)colliderStore;
    (void)fChangeInTime;
    (void)diagnosticsCsvWriter;
#endif
}


#ifdef _DEBUG
void PhysicsWorld::SetPhysicsRegressionLogPath( const char* path )
{
    m_stepDiagnostics.SetPhysicsRegressionLogPath( path );
}


void PhysicsWorld::SetPhysicsCollisionTimeLogPath( const char* path )
{
    m_stepDiagnostics.SetPhysicsCollisionTimeLogPath( path );
}


void PhysicsWorld::SetPhysicsDiagnosticsPath( const char* path )
{
    m_stepDiagnostics.SetPhysicsDiagnosticsPath( path );
}


void PhysicsWorld::SetPhysicsDiagnosticsRunId( const char* runId )
{
    m_stepDiagnostics.SetPhysicsDiagnosticsRunId( runId );
}


bool PhysicsWorld::SetDiagnosticsSuppressed( bool suppressed )
{
    const bool previous = m_diagnosticsSuppressed;
    m_diagnosticsSuppressed = suppressed;
    return previous;
}


#endif


PhysicsDiagnosticsView PhysicsWorld::GetDiagnosticsView() const
{
    return PhysicsDiagnosticsView { m_contactSolverStage.GetPersistentContacts(),
                                    m_contactSolverStage.GetStats(),
                                    m_sleepController.GetSleepIslandParents(),
                                    m_sleepController.GetSleepSupportedVector(),
                                    m_sleepController.GetSleepInhibitedVector(),
                                    m_sleepController.GetSleepStateVector(),
                                    m_sleepController.GetSleepCounters(),
                                    m_sleepController.GetSleepIslandEligible(),
                                    m_sleepController.GetSleepIslandCanSleep(),
                                    m_pointJointConstraints,
                                    m_broadphase.GetSpatialGrid(),
                                    m_broadphase.GetCandidatePairs(),
                                    m_broadphase.GetCollisionCellKeys(),
                                    m_sleepController.GetSleepSupportEdgeVector(),
                                    m_sleepController.GetSleepIslandVisualIdVector(),
                                    m_stepDiagnostics.GetPipelineTrace(),
                                    m_terrain.GetContactManifolds() };
}

uint64_t PhysicsWorld::CollectMemoryBytes() const
{
    uint64_t bytes = static_cast<uint64_t>( sizeof( *this ) );
    bytes += m_forceStage.CollectDynamicMemoryBytes();
    bytes += m_broadphase.CollectDynamicMemoryBytes();
    bytes += VectorCapacityBytes( m_timeRemaining );
    bytes += m_sleepController.CollectDynamicMemoryBytes();
    bytes += m_stepDiagnostics.CollectDynamicMemoryBytes();
    bytes += m_contactSolverStage.CollectDynamicMemoryBytes();
    bytes += m_terrain.CollectDynamicMemoryBytes();
    bytes += m_narrowphase.CollectDynamicMemoryBytes();
    bytes += VectorCapacityBytes( m_pointJointConstraints );
    bytes += m_externalForceStage.CollectMemoryBytes();
    return bytes;
}

uint64_t PhysicsWorld::CollectDebugAndBroadphaseMemoryBytes() const
{
    uint64_t bytes = m_broadphase.CollectDebugAndBroadphaseMemoryBytes();
    bytes += m_stepDiagnostics.CollectDebugMemoryBytes();
    bytes += VectorCapacityBytes( m_sleepController.GetSleepIslandVisualIdVector() );
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
    return m_stepDiagnostics.GetCollisionVisualContacts();
}


std::span<const int> PhysicsWorld::GetFixedContactHighlightBodies() const
{
    return m_contactSolverStage.GetSideEffects().fixedContactBodies;
}


std::span<const PhysicsFixedTreeReleaseEvent> PhysicsWorld::GetFixedTreeReleaseEvents() const
{
    return m_contactSolverStage.GetSideEffects().fixedTreeReleases;
}


std::span<const uint8_t> PhysicsWorld::GetSleepStates() const
{
    return m_sleepController.GetSleepStates();
}


std::span<const int> PhysicsWorld::GetSleepIslandVisualIds() const
{
    return m_sleepController.GetSleepIslandVisualIds();
}


std::span<const uint8_t> PhysicsWorld::GetSleepSupportedStates() const
{
    return m_sleepController.GetSleepSupportedStates();
}


std::span<const uint8_t> PhysicsWorld::GetSleepInhibitedStates() const
{
    return m_sleepController.GetSleepInhibitedStates();
}


const std::vector<PhysicsDebugContact>& PhysicsWorld::GetPhysicsDebugContacts() const
{
    return m_stepDiagnostics.GetDebugContacts();
}


const std::vector<PhysicsPipelineRecord>& PhysicsWorld::GetPhysicsPipelineTrace() const
{
    return m_stepDiagnostics.GetPipelineTrace();
}
