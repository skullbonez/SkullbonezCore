/*
File: SkullbonezSource/Physics/PhysicsWorld.cpp
Purpose:
  Sequences the fixed physics step and lifecycle of concrete stage owners.

Summary:
  Composes and sequences the extracted force, broadphase, narrowphase,
  terrain, contact, sleep, and diagnostics owners. It retains cross-stage
  clocks, top-level sibling lanes, and handle-keyed point-joint rows whose
  descriptor and warm-start state participate in byte-exact next-step replay.

Glossary:
  SoA (Structure of Arrays): Data layout that stores each field in a separate
  contiguous array for cache-friendly iteration.
  X-macro field list: Preprocessor list invoked by several tiny visitors so
    replay capture and restore use the same ordered state inventory.
  PhysicsEngine: Step owner that supplies stores and handles model-order
    writeback after compact physics work finishes.

Invariants:
  - Physics-visible behavior must remain deterministic; byte-exact baselines
    are the validation contract.
  - Fixed-capacity physics scratch buffers must not grow during gameplay; an
    exhausted reserve is a fatal invariant failure because continuing would either
    allocate on a hot path or silently drop deterministic side effects.
  - Mutual-gravity chunk scheduling may vary, but pair slots and the final
    triangular replay order never depend on worker count.
  - Parallel wake producers are flushed before the next awake-list consumer;
    worker scheduling never changes the ascending stage iteration order.
  - Pipeline trace mode is fixed at BeginStep; stage commit seams preserve one
    saturated event count whether or not payload records are retained.
  - Replay CanRestore validates dense rows, cross-owner references, committed
    capacities, and point-joint topology for every owner before Restore mutates
    any owner; rejection leaves the complete live world unchanged.

Related:
  - SkullbonezSource/Physics/PhysicsWorld.h
  - SkullbonezTests/TestPhysicsHandles.cpp
  - Agentic/Reference/physics-overview.md
  - Agentic/Reference/engine-glossary.md
*/
#include "PhysicsWorld.h"
#include "../Core/Common.h"

#include <cstddef>

#include "../Core/FatalError.h"
#include "BuoyancySystem.h"
#include "DisjointSet.h"
#include "PhysicsApi.h"
#include "PhysicsBodyStore.h"
#include "PhysicsEngine.ReplayPredictionCloneScope.h"
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
// counter. Sixteen bytes of guard/bookkeeping work live on the ascending awake
// set: steady sleep mirroring, CCD-clock reset,
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

template <typename T> uint64_t ListCapacityBytes( const T& values )
{
    return static_cast<uint64_t>( values.capacity() ) * static_cast<uint64_t>( sizeof( typename T::value_type ) );
}

float SolverBodyRadius( std::span<const ColliderRecord> colliderRecords, int bodyIndex )
{
    return colliderRecords[static_cast<size_t>( bodyIndex )].boundingRadius;
}

bool IsPointJointBodyPair( const PhysicsBodyStore& bodyStore, std::span<const PointJointConstraint> pointJointConstraints,
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
    // Fatal invariant: a partial solver snapshot cannot support deterministic replay
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
#define SB_REPLAY_SOLVER_FIXED_LIST_FIELDS( VISIT )                                                                         \
    VISIT( timeRemaining, m_timeRemaining, "timeRemaining" )                                                                \
    VISIT( motionEligibilityState, m_motionEligibility.StateForReplay(), "motionEligibilityState" )                         \
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

#define SB_REPLAY_SOLVER_POINT_JOINT_VECTOR_FIELDS( VISIT ) VISIT( pointJoints, m_pointJointConstraints, "pointJoints" )

#define SB_REPLAY_SOLVER_VECTOR_FIELDS( VISIT )                                                                             \
    SB_REPLAY_SOLVER_FIXED_LIST_FIELDS( VISIT )                                                                             \
    SB_REPLAY_SOLVER_SLEEP_VECTOR_FIELDS( VISIT )                                                                           \
    SB_REPLAY_SOLVER_DIAGNOSTIC_VECTOR_FIELDS( VISIT )                                                                      \
    SB_REPLAY_SOLVER_CONTACT_STAGE_VECTOR_FIELDS( VISIT )                                                                   \
    SB_REPLAY_SOLVER_POINT_JOINT_VECTOR_FIELDS( VISIT )


PhysicsWorld::PhysicsWorld() = default;


void PhysicsWorld::CloneReplayPredictionTopologyFrom( const PhysicsWorld& source )
{
    Detail::RequireReplayPredictionCloneScope( "PhysicsWorld topology clone" );

    // PhysicsSolverSnapshot restores time, broadphase, sleep, diagnostics, and
    // persistent-contact rows after this operation. Copy only state outside
    // that contract so transient stage scratch cannot become clone authority.
    m_terrainView = source.m_terrainView;
    m_lastTimeRemainingStep = source.m_lastTimeRemainingStep;
    m_lastTimeRemainingStepValid = source.m_lastTimeRemainingStepValid;
    m_underwaterSleepProbeNeeded = source.m_underwaterSleepProbeNeeded;
    m_lastUnderwaterProbeFluidSurfaceHeight = source.m_lastUnderwaterProbeFluidSurfaceHeight;
    m_lastUnderwaterProbeFluidSurfaceHeightValid = source.m_lastUnderwaterProbeFluidSurfaceHeightValid;

    m_pointJointConstraints.Reserve( source.m_pointJointConstraints.size() );
    m_pointJointConstraints.clear();

    for ( const PointJointConstraint& constraint : source.m_pointJointConstraints )
    {
        m_pointJointConstraints.push_back( constraint );
    }

    m_pointJointCapacity = source.m_pointJointCapacity;
    m_nextPointJointHandleIndex = source.m_nextPointJointHandleIndex;
    m_pointJointHandleGeneration = source.m_pointJointHandleGeneration;
#ifdef _DEBUG
    m_diagnosticsSuppressed = source.m_diagnosticsSuppressed;
#endif
}


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
    m_motionEligibility.Clear();
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
    m_motionEligibility.InvalidateBodyTopology();
}


void PhysicsWorld::ReserveBodyScratchCapacity( std::size_t bodyCapacity, std::size_t pointJointCapacity )
{
    m_timeRemaining.Reserve( bodyCapacity );
    m_pointJointConstraints.Reserve( pointJointCapacity );

    m_forceStage.ReserveBodyScratchCapacity( bodyCapacity );
    m_externalForceStage.ReserveBodyCapacity( bodyCapacity );
    m_broadphase.ReserveSceneCapacity( bodyCapacity );
    m_motionEligibility.ReserveBodyCapacity( bodyCapacity );
    m_narrowphase.ReserveSceneCapacity( bodyCapacity );
    m_contactSolverStage.ReserveSceneCapacity( bodyCapacity );
    m_stepDiagnostics.ReserveSceneCapacity( bodyCapacity );
    m_sleepController.ReserveBodyCapacity( bodyCapacity, pointJointCapacity );
    m_terrain.ReserveSceneCapacity( bodyCapacity );
    m_pointJointCapacity = pointJointCapacity;
}


std::size_t PhysicsWorld::PointJointCapacity() const noexcept
{
    return m_pointJointCapacity;
}


void PhysicsWorld::CaptureReplaySolverSnapshot( PhysicsSolverSnapshot& outSnapshot, int modelCount,
                                                const PhysicsBodyStore& bodyStore ) const
{
    // Runtime allocation policy: replay recorder slots pre-reserve these
    // payload vectors outside gameplay. Capture clears the retained slot in
    // place so solver replay does not discard capacity and reallocate per tick.
#define CLEAR_REPLAY_SOLVER_VECTOR_FIELD( snapshotField, worldValues, label ) outSnapshot.snapshotField.clear();
    SB_REPLAY_SOLVER_VECTOR_FIELDS( CLEAR_REPLAY_SOLVER_VECTOR_FIELD )
#undef CLEAR_REPLAY_SOLVER_VECTOR_FIELD
    outSnapshot.solverStats = PhysicsSolverStatsSample();

    outSnapshot.version = PHYSICS_SOLVER_SNAPSHOT_VERSION;
    outSnapshot.modelCount = modelCount;

    // Runtime allocation policy: a solver snapshot owns many typed vectors.
    // Batch their byte budget into one replay approval, then reserve individual
    // vectors inside that owner scope so replay diagnostics stay readable.
    uint64_t oldSnapshotBytes = 0;
    uint64_t requestedSnapshotBytes = 0;
    bool snapshotNeedsGrowth = false;
    const auto includeSnapshotReserve = [&]( const auto& values, std::size_t requestedCapacity )
    {
        oldSnapshotBytes += ListCapacityBytes( values );

        requestedSnapshotBytes += ReplaySolverSnapshotRequestedBytes( values, requestedCapacity );
        snapshotNeedsGrowth = snapshotNeedsGrowth || requestedCapacity > values.capacity();
    };

#define INCLUDE_REPLAY_SOLVER_VECTOR_RESERVE( snapshotField, worldValues, label )                                           \
    includeSnapshotReserve( outSnapshot.snapshotField,                                                                      \
                            (std::max)( worldValues.size(), static_cast<std::size_t>( (std::max)( 0, modelCount ) ) ) );
    SB_REPLAY_SOLVER_VECTOR_FIELDS( INCLUDE_REPLAY_SOLVER_VECTOR_RESERVE )
#undef INCLUDE_REPLAY_SOLVER_VECTOR_RESERVE

    const auto reserveSnapshotVectors = [&]()
    {
#define RESERVE_REPLAY_SOLVER_VECTOR_FIELD( snapshotField, worldValues, label )                                             \
    ReserveReplaySolverSnapshotVector( outSnapshot.snapshotField,                                                           \
                                       (std::max)( worldValues.size(),                                                      \
                                                   static_cast<std::size_t>( (std::max)( 0, modelCount ) ) ),               \
                                       label );
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

#define CAPTURE_REPLAY_SOLVER_FIXED_LIST_FIELD( snapshotField, worldValues, label )                                         \
    for ( const auto& value : worldValues )                                                                                 \
    {                                                                                                                       \
        outSnapshot.snapshotField.push_back( value );                                                                       \
    }

    SB_REPLAY_SOLVER_FIXED_LIST_FIELDS( CAPTURE_REPLAY_SOLVER_FIXED_LIST_FIELD )
#undef CAPTURE_REPLAY_SOLVER_FIXED_LIST_FIELD

    m_sleepController.CaptureReplayState( outSnapshot );
    m_stepDiagnostics.CaptureReplayState( outSnapshot );
    m_contactSolverStage.CaptureReplayState( outSnapshot );

    // Invariant: the requested replay prefix is a self-contained transaction,
    // not a label attached to full live-world vectors. Normalize every body row
    // and remove references to trimmed bodies before restore preflight sees it.
    const std::size_t bodyRows = static_cast<std::size_t>( (std::max)( 0, modelCount ) );
    const auto normalizeBodyRows = [bodyRows]( auto& values, const auto& defaultValue )
    {
        if ( values.size() > bodyRows )
        {
            values.erase( values.begin() + static_cast<std::ptrdiff_t>( bodyRows ), values.end() );
        }

        // Invariant: ReserveReplaySolverSnapshotStorage established at least
        // bodyRows capacity before capture, so cold-state padding cannot grow.
        while ( values.size() < bodyRows )
        {
            values.push_back( defaultValue );
        }
    };

    normalizeBodyRows( outSnapshot.timeRemaining, 0.0f );
    normalizeBodyRows( outSnapshot.motionEligibilityState, uint8_t { 0u } );
    normalizeBodyRows( outSnapshot.sleepSupportedThisFrame, uint8_t { 0u } );
    normalizeBodyRows( outSnapshot.sleepInhibitedThisFrame, uint8_t { 0u } );
    normalizeBodyRows( outSnapshot.sleepState, uint8_t { 0u } );
    normalizeBodyRows( outSnapshot.sleepCounter, uint8_t { 0u } );
    normalizeBodyRows( outSnapshot.underwaterSleepLocked, uint8_t { 0u } );
    normalizeBodyRows( outSnapshot.collisionVisualContacts, uint8_t { 0u } );
    normalizeBodyRows( outSnapshot.sleepIslandVisualId, 0 );
    normalizeBodyRows( outSnapshot.sleepIslandAssignedVisualId, 0 );
    normalizeBodyRows( outSnapshot.sleepIslandParent, 0 );
    normalizeBodyRows( outSnapshot.sleepIslandRank, uint8_t { 0u } );
    normalizeBodyRows( outSnapshot.sleepIslandHasAwake, uint8_t { 0u } );
    normalizeBodyRows( outSnapshot.sleepIslandHasSupportAnchor, uint8_t { 0u } );
    normalizeBodyRows( outSnapshot.sleepIslandEligible, uint8_t { 0u } );
    normalizeBodyRows( outSnapshot.sleepIslandCanSleep, uint8_t { 0u } );
    outSnapshot.persistentContactCounts.assign( bodyRows, 0u );
    outSnapshot.persistentRestingContactCounts.assign( bodyRows, 0u );

    const auto bodyPairFitsPrefix = [modelCount]( int bodyA, int bodyB )
    { return bodyA >= 0 && bodyA < modelCount && ( bodyB == -1 || ( bodyB >= 0 && bodyB < modelCount ) ); };

    std::erase_if( outSnapshot.sleepSupportEdges, [modelCount]( const auto& edge )
                   { return edge.first < 0 || edge.first >= modelCount || edge.second < 0 || edge.second >= modelCount; } );
    std::erase_if( outSnapshot.persistentContacts,
                   [&]( const auto& contact ) { return !bodyPairFitsPrefix( contact.bodyA, contact.bodyB ); } );
    std::erase_if( outSnapshot.debugContacts,
                   [&]( const auto& contact ) { return !bodyPairFitsPrefix( contact.bodyA, contact.bodyB ); } );
    std::erase_if( outSnapshot.pipelineTrace,
                   [&]( const auto& record ) { return record.bodyA >= modelCount || record.bodyB >= modelCount; } );
    std::erase_if( outSnapshot.persistentContactCache, [modelCount]( const auto& cache )
                   { return !PersistentContactCacheKeyBodiesFit( cache.key, modelCount ); } );

    // Invariant: live cache lookup uses lower_bound and therefore observes only
    // the first row for an equal key. Filtering preserves the owner's sorted
    // order; collapsing adjacent duplicates retains that exact observable row
    // while producing the strictly sorted-and-unique replay contract required
    // for deterministic restore.
    outSnapshot.persistentContactCache.erase( std::unique( outSnapshot.persistentContactCache.begin(),
                                                           outSnapshot.persistentContactCache.end(),
                                                           []( const auto& lhs, const auto& rhs )
                                                           { return lhs.key == rhs.key; } ),
                                              outSnapshot.persistentContactCache.end() );

    for ( const PhysicsSolverPersistentContactSample& contact : outSnapshot.persistentContacts )
    {
        if ( contact.bodyB < 0 )
        {
            continue;
        }

        ++outSnapshot.persistentContactCounts[static_cast<std::size_t>( contact.bodyA )];
        ++outSnapshot.persistentContactCounts[static_cast<std::size_t>( contact.bodyB )];

        if ( contact.supportsRestingPolicy )
        {
            ++outSnapshot.persistentRestingContactCounts[static_cast<std::size_t>( contact.bodyA )];
            ++outSnapshot.persistentRestingContactCounts[static_cast<std::size_t>( contact.bodyB )];
        }
    }

    for ( std::size_t bodyIndex = 0; bodyIndex < bodyRows; ++bodyIndex )
    {
        const int parent = outSnapshot.sleepIslandParent[bodyIndex];

        if ( parent < 0 || parent >= modelCount )
        {
            outSnapshot.sleepIslandParent[bodyIndex] = static_cast<int>( bodyIndex );
        }
    }

    const std::span<const PhysicsBodyRecord> bodyRecords = bodyStore.Records();
    uint32_t topologyOrdinal = 0u;

    for ( const PointJointConstraint& constraint : m_pointJointConstraints )
    {
        const int bodyA = constraint.BodyAIndex( bodyStore );
        const int bodyB = constraint.BodyBIndex( bodyStore );

        if ( bodyA < 0 || bodyA >= modelCount || bodyB < 0 || bodyB >= modelCount )
        {
            continue;
        }

        PhysicsSolverPointJointSample sample;
        sample.topologyOrdinal = topologyOrdinal++;
        sample.bodyASceneObjectId = bodyRecords[static_cast<std::size_t>( bodyA )].sceneObjectId;
        sample.bodyBSceneObjectId = bodyRecords[static_cast<std::size_t>( bodyB )].sceneObjectId;
        sample.localAnchorA = constraint.localAnchorA;
        sample.localAnchorB = constraint.localAnchorB;
        sample.slack = constraint.slack;
        sample.stiffness = constraint.stiffness;
        sample.damping = constraint.damping;
        sample.accumulatedImpulse = constraint.accumulatedImpulse;
        sample.groupId = constraint.groupId;
        sample.flags = constraint.flags;
        outSnapshot.pointJoints.push_back( sample );
    }
}


bool PhysicsWorld::CanRestoreReplaySolverSnapshot( const PhysicsSolverSnapshot& snapshot, int modelCount,
                                                   const PhysicsBodyStore& bodyStore ) const
{
    if ( snapshot.version < 1 || snapshot.version > PHYSICS_SOLVER_SNAPSHOT_VERSION || snapshot.modelCount != modelCount )
    {
        return false;
    }

    if ( modelCount < 0 || bodyStore.Records().size() < static_cast<std::size_t>( modelCount ) ||
         snapshot.timeRemaining.size() != static_cast<std::size_t>( modelCount ) ||
         snapshot.timeRemaining.size() > m_timeRemaining.capacity() ||
         ( snapshot.version >= 4u &&
           ( snapshot.motionEligibilityState.size() != static_cast<std::size_t>( modelCount ) ||
             snapshot.motionEligibilityState.size() > m_motionEligibility.StateCapacityForReplay() ) ) ||
         ( snapshot.version < 4u && !snapshot.motionEligibilityState.empty() ) ||
         snapshot.collisionCellKeys.size() > m_broadphase.CollisionCellKeyCapacityForReplay() ||
         !m_sleepController.CanRestoreReplayState( snapshot, modelCount ) ||
         !m_stepDiagnostics.CanRestoreReplayState( snapshot, modelCount ) ||
         !m_contactSolverStage.CanRestoreReplayState( snapshot, modelCount ) )
    {
        return false;
    }

    for ( uint8_t state : snapshot.motionEligibilityState )
    {
        if ( ( state & ~PHYSICS_MOTION_ELIGIBILITY_VALID_BITS ) != 0u )
        {
            return false;
        }
    }

    uint64_t payloadBytes = 0u;
#define REQUIRE_REPLAY_PAYLOAD_BYTES( snapshotField, worldValues, label )                                                   \
    if ( snapshot.snapshotField.size() > static_cast<std::size_t>( PHYSICS_SOLVER_SNAPSHOT_RESERVE_HARD_BYTES ) /           \
                                             sizeof( typename decltype( snapshot.snapshotField )::value_type ) )            \
    {                                                                                                                       \
        return false;                                                                                                       \
    }                                                                                                                       \
    payloadBytes += static_cast<uint64_t>( snapshot.snapshotField.size() ) *                                                \
                    sizeof( typename decltype( snapshot.snapshotField )::value_type );
    SB_REPLAY_SOLVER_VECTOR_FIELDS( REQUIRE_REPLAY_PAYLOAD_BYTES )
#undef REQUIRE_REPLAY_PAYLOAD_BYTES

    if ( payloadBytes > static_cast<uint64_t>( PHYSICS_SOLVER_SNAPSHOT_RESERVE_HARD_BYTES ) )
    {
        return false;
    }

    if ( snapshot.version < 3u && !snapshot.pointJoints.empty() )
    {
        return false;
    }

    if ( snapshot.version >= 3u )
    {
        const std::span<const PhysicsBodyRecord> bodyRecords = bodyStore.Records();
        std::size_t survivingConstraintIndex = 0u;

        for ( const PointJointConstraint& constraint : m_pointJointConstraints )
        {
            const int bodyA = constraint.BodyAIndex( bodyStore );
            const int bodyB = constraint.BodyBIndex( bodyStore );

            if ( bodyA >= 0 && bodyA < modelCount && bodyB >= 0 && bodyB < modelCount )
            {
                if ( survivingConstraintIndex >= snapshot.pointJoints.size() )
                {
                    return false;
                }

                const PhysicsSolverPointJointSample& sample = snapshot.pointJoints[survivingConstraintIndex];

                // Invariant: the filtered row ordinal and durable scene ids are
                // the persisted topology identity. Public constraint/body handle
                // generations are intentionally absent because Clear advances them.
                if ( sample.topologyOrdinal != survivingConstraintIndex ||
                     bodyRecords[static_cast<std::size_t>( bodyA )].sceneObjectId != sample.bodyASceneObjectId ||
                     bodyRecords[static_cast<std::size_t>( bodyB )].sceneObjectId != sample.bodyBSceneObjectId ||
                     std::memcmp( &constraint.localAnchorA, &sample.localAnchorA, sizeof( sample.localAnchorA ) ) != 0 ||
                     std::memcmp( &constraint.localAnchorB, &sample.localAnchorB, sizeof( sample.localAnchorB ) ) != 0 ||
                     std::memcmp( &constraint.slack, &sample.slack, sizeof( sample.slack ) ) != 0 ||
                     std::memcmp( &constraint.stiffness, &sample.stiffness, sizeof( sample.stiffness ) ) != 0 ||
                     std::memcmp( &constraint.damping, &sample.damping, sizeof( sample.damping ) ) != 0 ||
                     constraint.groupId != sample.groupId || constraint.flags != sample.flags )
                {
                    return false;
                }

                ++survivingConstraintIndex;
            }
        }

        if ( survivingConstraintIndex != snapshot.pointJoints.size() )
        {
            return false;
        }
    }

    return true;
}


bool PhysicsWorld::RestoreReplaySolverSnapshot( const PhysicsSolverSnapshot& snapshot, int modelCount,
                                                const PhysicsBodyStore& bodyStore )
{
    if ( !CanRestoreReplaySolverSnapshot( snapshot, modelCount, bodyStore ) )
    {
        return false;
    }

#define RESTORE_REPLAY_SOLVER_FIXED_LIST_FIELD( snapshotField, worldValues, label )                                         \
    worldValues.Reserve( snapshot.snapshotField.size() );                                                                   \
    worldValues.clear();                                                                                                    \
    for ( const auto& value : snapshot.snapshotField )                                                                      \
    {                                                                                                                       \
        worldValues.push_back( value );                                                                                     \
    }

    SB_REPLAY_SOLVER_FIXED_LIST_FIELDS( RESTORE_REPLAY_SOLVER_FIXED_LIST_FIELD )
#undef RESTORE_REPLAY_SOLVER_FIXED_LIST_FIELD

    m_motionEligibility.CommitReplayRestoreState( snapshot.version >= 4u );

    m_sleepController.RestoreReplayState( snapshot );
    m_stepDiagnostics.RestoreReplayState( snapshot );
    m_contactSolverStage.RestoreReplayState( snapshot );

    if ( snapshot.version >= 3u )
    {
        std::size_t sampleIndex = 0u;

        for ( PointJointConstraint& constraint : m_pointJointConstraints )
        {
            const int bodyA = constraint.BodyAIndex( bodyStore );
            const int bodyB = constraint.BodyBIndex( bodyStore );

            if ( bodyA >= 0 && bodyA < modelCount && bodyB >= 0 && bodyB < modelCount )
            {
                constraint.accumulatedImpulse = snapshot.pointJoints[sampleIndex++].accumulatedImpulse;
            }
        }
    }
    else
    {
        // Compatibility: v1/v2 artifacts predate point-joint warm starting.
        // Restoring them must reproduce their cold-cache behavior instead of
        // retaining an unrelated live impulse.
        for ( PointJointConstraint& constraint : m_pointJointConstraints )
        {
            constraint.accumulatedImpulse = 0.0f;
        }
    }

    m_terrain.Clear();
    m_narrowphase.Clear();
    m_broadphase.ResetTransientAfterReplayRestore();
    return true;
}

#undef SB_REPLAY_SOLVER_FIXED_LIST_FIELDS
#undef SB_REPLAY_SOLVER_SLEEP_VECTOR_FIELDS
#undef SB_REPLAY_SOLVER_DIAGNOSTIC_VECTOR_FIELDS
#undef SB_REPLAY_SOLVER_CONTACT_STAGE_VECTOR_FIELDS
#undef SB_REPLAY_SOLVER_POINT_JOINT_VECTOR_FIELDS
#undef SB_REPLAY_SOLVER_VECTOR_FIELDS


void PhysicsWorld::CommitContactSolverConsequences( PhysicsBodyStore& bodyStore, const ColliderStore& colliderStore,
                                                    std::span<BuoyancyBodyFacts> buoyancyFacts,
                                                    const PhysicsWorldForces& worldForces )
{
    const PersistentContactSolverSideEffects& effects = m_contactSolverStage.GetSideEffects();

    // Invariant: Solve selected this same step-owned mode before producing
    // effects, so exactly one representation is committed here.
    if ( m_stepDiagnostics.RetainsFullPipelineRecords() )
    {
        for ( const PhysicsPipelineRecord& record : effects.pipelineRecords )
        {
            m_stepDiagnostics.RecordPipelineStage( record );
        }
    }
    else
    {
        m_stepDiagnostics.RecordPipelineEvents( effects.pipelineEventCount );
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
                               std::span<BuoyancyBodyFacts> buoyancyFacts, float deltaSeconds,
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
    const bool timeStepChanged = !m_lastTimeRemainingStepValid || deltaSeconds != m_lastTimeRemainingStep;

    if ( static_cast<int>( m_timeRemaining.size() ) != modelCount || timeStepChanged )
    {
        // Cold topology/timestep boundary: preserve the old all-row value
        // contract for replay/diagnostics. Capacity is reserved before play;
        // ordinary same-dt steps below write only bodies that can consume it.
        m_timeRemaining.assign( static_cast<std::size_t>( modelCount ), deltaSeconds );
    }

    m_lastTimeRemainingStep = deltaSeconds;
    m_lastTimeRemainingStepValid = true;
    m_stepDiagnostics.BeginStep( modelCount );
    m_terrain.BeginFrame();
    const bool rebuiltAwakeList = m_sleepController.MirrorFlagsFrom( bodyStore, modelCount );
    const std::span<const int> awakeBodyIndices = m_sleepController.GetAwakeBodyIndices();

    for ( int bodyIndex : awakeBodyIndices )
    {
        m_timeRemaining[static_cast<std::size_t>( bodyIndex )] = deltaSeconds;
    }

    const bool fluidSurfaceHeightChanged = !m_lastUnderwaterProbeFluidSurfaceHeightValid ||
                                           worldForces.fluidSurfaceHeight != m_lastUnderwaterProbeFluidSurfaceHeight;

    m_lastUnderwaterProbeFluidSurfaceHeight = worldForces.fluidSurfaceHeight;
    m_lastUnderwaterProbeFluidSurfaceHeightValid = true;
    const bool probeDormantUnderwaterLocks = rebuiltAwakeList || m_underwaterSleepProbeNeeded || fluidSurfaceHeightChanged;

    m_underwaterSleepProbeNeeded = false;
    RunSolverPhysics( bodyStore, colliderStore, buoyancyFacts, deltaSeconds, settings, worldForces, externalForces,
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

    PROFILE_SCOPED( "Frame/Physics/ExternalForceField" );
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
    PROFILE_BEGIN( "Frame/Physics/ApplyForces" );
    m_forceStage.ApplyForces( bodyStore, colliderStore, m_terrainView, worldForces, buoyancyFacts, sleepStates,
                              m_timeRemaining, mutualGravityForces, dt, awakeBodyIndices, workerPool, settings.execution );

    PROFILE_END( "Frame/Physics/ApplyForces" );

    ApplyExternalForces( bodyStore, colliderStore, buoyancyFacts, worldForces, externalForces, settings.execution,
                         workerPool );

    m_sleepController.FlushPendingAwakeBodyIndices();
    awakeBodyIndices = m_sleepController.GetAwakeBodyIndices();

    // One deterministic dense pass consumes the final force-resolved velocities.
    // Narrowphase consumes linear path bits, while broadphase consumes angular
    // reach; rotational time-of-impact and response remain outside this phase.
    m_motionEligibility.Run( bodyStore, colliderStore, sleepStates, dt );

    // Broadphase: sleeping membership remains resident, while awake rows update
    // their ranges and source awake-to-sleep wake-detection pairs.
    const float contactSkin = (std::max)( 0.0f, settings.body.contactEpsilon );
    const std::span<const std::pair<int, int>>
        candidatePairs = m_broadphase.Run( bodyStore, colliderStore, settings.broadphase, m_pointJointConstraints,
                                           sleepStates, awakeBodyIndices, m_motionEligibility.AngularBroadphaseExpansion(),
                                           m_stepDiagnostics, dt, contactSkin, settings.body.contactEpsilon );

    // Object/object CCD front-end: wake sleepers and advance swept hits to a
    // contact candidate, but leave velocity response to the persistent rows.
    PROFILE_BEGIN( "Frame/Physics/Narrowphase" );
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
                                                          settings.body.contactEpsilon,
                                                          invCellSize,
                                                          dt,
                                                          m_stepDiagnostics.RetainsFullPipelineRecords(),
                                                          settings.execution.parallel,
                                                          settings.execution.parallelNarrowphase };

    const bool ranParallelNarrowphase = m_narrowphase.TryRunParallel( bodyStore, colliderStore, m_terrainView, buoyancyFacts,
                                                                      candidatePairs, narrowphaseWake, m_timeRemaining,
                                                                      m_motionEligibility.State(), narrowphasePolicy,
                                                                      m_profiler, workerPool );

    if ( ranParallelNarrowphase )
    {
        PROFILE_SCOPED( "Frame/Physics/Narrowphase/CommitEvents" );
        const std::span<const ObjectNarrowphaseEvent> events = m_narrowphase.GetEvents();

        if ( narrowphasePolicy.retainPipelineRecords )
        {
            for ( int pairIndex = 0; pairIndex < candidatePairCount; ++pairIndex )
            {
                const ObjectNarrowphaseEvent& event = events[static_cast<size_t>( pairIndex )];

                if ( event.pipelineRecord )
                {
                    m_stepDiagnostics.RecordPipelineStage( *event.pipelineRecord );
                }

                CommitObjectNarrowphaseEvent( event );
            }
        }
        else
        {
            std::size_t pipelineEventCount = 0;

            for ( int pairIndex = 0; pairIndex < candidatePairCount; ++pairIndex )
            {
                const ObjectNarrowphaseEvent& event = events[static_cast<size_t>( pairIndex )];
                pipelineEventCount += event.hasPipelineEvent;
                CommitObjectNarrowphaseEvent( event );
            }

            if ( pipelineEventCount != 0 )
            {
                m_stepDiagnostics.RecordPipelineEvents( pipelineEventCount );
            }
        }
    }
    else
    {
        // Invariant: serial mode commits each pair's side effects immediately.
        // Deferring this loop would change what the next pair observes.
        PROFILE_SCOPED( "Frame/Physics/Narrowphase/SerialPairs" );

        if ( narrowphasePolicy.retainPipelineRecords )
        {
            for ( int pairIndex = 0; pairIndex < candidatePairCount; ++pairIndex )
            {
                ObjectNarrowphaseEvent event;
                m_narrowphase.ProcessObjectNarrowphasePair<true>( bodyStore, colliderStore, m_terrainView, buoyancyFacts,
                                                                  candidatePairs, narrowphaseWake, m_timeRemaining,
                                                                  m_motionEligibility.State(), narrowphasePolicy, m_profiler,
                                                                  pairIndex, event );

                if ( event.pipelineRecord )
                {
                    m_stepDiagnostics.RecordPipelineStage( *event.pipelineRecord );
                }

                CommitObjectNarrowphaseEvent( event );
            }
        }
        else
        {
            std::size_t pipelineEventCount = 0;

            for ( int pairIndex = 0; pairIndex < candidatePairCount; ++pairIndex )
            {
                ObjectNarrowphaseEvent event;
                m_narrowphase.ProcessObjectNarrowphasePair<false>( bodyStore, colliderStore, m_terrainView, buoyancyFacts,
                                                                   candidatePairs, narrowphaseWake, m_timeRemaining,
                                                                   m_motionEligibility.State(), narrowphasePolicy,
                                                                   m_profiler, pairIndex, event );

                pipelineEventCount += event.hasPipelineEvent;
                CommitObjectNarrowphaseEvent( event );
            }

            if ( pipelineEventCount != 0 )
            {
                m_stepDiagnostics.RecordPipelineEvents( pipelineEventCount );
            }
        }
    }

    PROFILE_END( "Frame/Physics/Narrowphase" );
    m_sleepController.FlushPendingAwakeBodyIndices();
    awakeBodyIndices = m_sleepController.GetAwakeBodyIndices();

    // Terrain phase ownership:
    //   1. Detect only the current boundary for Discrete bodies; linearly
    //      promoted bodies inspect their full remaining path and stop at TOI.
    //   2. Convert the hit into a terrain manifold only. Do not apply impulses
    //      or terrain-only velocity response in this phase.
    //   3. Leave remaining-time integration and all normal/friction response to
    //      the shared persistent contact rows below.
    PROFILE_BEGIN( "Frame/Physics/Terrain" );
    PROFILE_BEGIN( "Frame/Physics/Terrain/Detect" );
    m_terrain.Detect( bodyStore, colliderStore, buoyancyFacts, m_terrainView, settings, sleepStates,
                      m_motionEligibility.State(), m_timeRemaining, m_profiler, awakeBodyIndices, settings.execution,
                      workerPool );

    const std::span<const TerrainDetectionCandidate> terrainCandidates = m_terrain.GetDetectionCandidates();

    // Why: duplicate the short commit lane so the diagnostic-mode decision is
    // hoisted outside the body loop and count-only code cannot construct rows.
    if ( m_stepDiagnostics.RetainsFullPipelineRecords() )
    {
        for ( int x : awakeBodyIndices )
        {
            const TerrainDetectionCandidate& candidate = terrainCandidates[static_cast<size_t>( x )];

            if ( candidate.tested )
            {
                const PreparedTerrainCandidateCommit
                    commit = m_terrain.PrepareCandidateCommit<true>( bodyStore, colliderStore, m_terrainView, buoyancyFacts,
                                                                     settings, m_profiler, x, candidate.availableTime,
                                                                     candidate.sweep );

                if ( commit.hit )
                {
                    m_stepDiagnostics.RecordPipelineStage( *commit.pipelineRecord );
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
    }
    else
    {
        std::size_t pipelineEventCount = 0;

        for ( int x : awakeBodyIndices )
        {
            const TerrainDetectionCandidate& candidate = terrainCandidates[static_cast<size_t>( x )];

            if ( candidate.tested )
            {
                const PreparedTerrainCandidateCommit
                    commit = m_terrain.PrepareCandidateCommit<false>( bodyStore, colliderStore, m_terrainView, buoyancyFacts,
                                                                      settings, m_profiler, x, candidate.availableTime,
                                                                      candidate.sweep );

                if ( commit.hit )
                {
                    ++pipelineEventCount;
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

        if ( pipelineEventCount != 0 )
        {
            m_stepDiagnostics.RecordPipelineEvents( pipelineEventCount );
        }
    }

    PROFILE_END( "Frame/Physics/Terrain/Detect" );
    PROFILE_END( "Frame/Physics/Terrain" );

    PersistentContactSolverStepPolicy contactPolicy = PhysicsContactSolverStage::ResolveStepPolicy( settings, worldForces );

    // Invariant: only a world with an active, unsuppressed diagnostics sink
    // pays for row attribution. Replay prediction's private PhysicsEngine has
    // no sink, so its fixed amortization budget remains simulation-only.
    contactPolicy.collectConvergenceDiagnostics = ShouldEmitStepDiagnostics();

    // Invariant: narrowphase and terrain detection have already consumed any
    // time-of-impact advancement from m_timeRemaining. The contact solver must
    // borrow these exact rows so its time-scaled terms describe the remaining
    // integration interval without changing the partial-CCD sequence.
    m_contactSolverStage.Solve( bodyStore, colliderStore, contactPolicy, candidatePairs, sleepStates, m_timeRemaining,
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
    PROFILE_BEGIN( "Frame/Physics/Integrate" );
#ifdef SKULLBONEZ_PROFILE_ENABLED
    const int integrateAwakeBodyCount = static_cast<int>( awakeBodyIndices.size() );
#endif
    m_forceStage.IntegrateRemaining( bodyStore, m_profiler, colliderStore, m_terrainView, buoyancyFacts,
                                     m_sleepController.GetSleepStates(), m_timeRemaining, awakeBodyIndices, workerPool,
                                     settings.execution );

    m_sleepController.RunIslandStage( bodyStore, colliderStore, worldForces, buoyancyFacts, m_timeRemaining,
                                      m_contactSolverStage.GetPersistentContacts(),
                                      m_contactSolverStage.GetPersistentRestingContactCounts(), m_pointJointConstraints,
                                      m_stepDiagnostics.MutablePipelineTraceRecorder(), sleepPolicy );

    PROFILE_END( "Frame/Physics/Integrate" );

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

    // Invariant: a body counts only when its integer cell range changes.
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

void PhysicsWorld::SetPipelineTraceFullRecordConsumerActive( bool active )
{
    m_stepDiagnostics.SetPipelineTraceFullRecordConsumerActive( active );
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
    // body slot can be reused. Stable compaction preserves survivor relative
    // order; subsequent snapshot capture assigns filtered ordinals in that
    // order.
    std::size_t writeIndex = 0u;

    for ( std::size_t readIndex = 0; readIndex < m_pointJointConstraints.size(); ++readIndex )
    {
        const PointJointConstraint& constraint = m_pointJointConstraints[readIndex];

        if ( constraint.bodyA == body || constraint.bodyB == body )
        {
            continue;
        }

        if ( writeIndex != readIndex )
        {
            m_pointJointConstraints[writeIndex] = constraint;
        }

        ++writeIndex;
    }

    while ( m_pointJointConstraints.size() > writeIndex )
    {
        m_pointJointConstraints.pop_back();
    }
}


void PhysicsWorld::TrimPointJointsToBodyCount( const PhysicsBodyStore& bodyStore, int bodyCount )
{
    // Invariant: replay body trim retires every joint that references a removed
    // model row before body handles are invalidated. Stable compaction preserves
    // the filtered ordinal order accepted by the pre-mutation restore check.
    std::size_t writeIndex = 0u;

    for ( std::size_t readIndex = 0; readIndex < m_pointJointConstraints.size(); ++readIndex )
    {
        const PointJointConstraint& constraint = m_pointJointConstraints[readIndex];
        const int bodyA = constraint.BodyAIndex( bodyStore );
        const int bodyB = constraint.BodyBIndex( bodyStore );

        if ( bodyA < 0 || bodyA >= bodyCount || bodyB < 0 || bodyB >= bodyCount )
        {
            continue;
        }

        if ( writeIndex != readIndex )
        {
            m_pointJointConstraints[writeIndex] = constraint;
        }

        ++writeIndex;
    }

    while ( m_pointJointConstraints.size() > writeIndex )
    {
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

    // Fatal invariant: exhausting the monotonic handle space would let a stale command
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
    bool invalidateWarmStart = false;

    if ( desc.updateMask & PHYSICS_POINT_JOINT_UPDATE_BODIES )
    {
        joint.SetBodies( desc.bodyA, desc.bodyB );
    }

    if ( desc.updateMask & PHYSICS_POINT_JOINT_UPDATE_ANCHORS )
    {
        joint.localAnchorA = desc.localAnchorA;
        joint.localAnchorB = desc.localAnchorB;
        invalidateWarmStart = true;
    }

    if ( desc.updateMask & PHYSICS_POINT_JOINT_UPDATE_SOLVER )
    {
        joint.slack = desc.slack;
        joint.stiffness = desc.stiffness;
        joint.damping = desc.damping;
        invalidateWarmStart = true;
    }

    if ( desc.updateMask & PHYSICS_POINT_JOINT_UPDATE_GROUP )
    {
        joint.groupId = desc.groupId;
        joint.flags = desc.flags;
    }

    if ( invalidateWarmStart )
    {
        // Invariant: a scalar impulse is meaningful only for the bodies,
        // anchors, and solver policy that produced it. Handle identity survives
        // authoring updates, but stale solver state must not.
        joint.accumulatedImpulse = 0.0f;
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


const PhysicsBodyRowList<PointJointConstraint>& PhysicsWorld::GetPointJointConstraints() const
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


void PhysicsWorld::SetDiagnosticNames( std::span<const char* const> diagnosticNames )
{
    m_stepDiagnostics.SetDiagnosticNames( diagnosticNames );
}


void PhysicsWorld::EmitStepDiagnostics( const PhysicsBodyStore& bodyStore, const ColliderStore& colliderStore,
                                        float deltaSeconds, const PhysicsDiagnosticsCsvWriter& diagnosticsCsvWriter )
{
#ifdef _DEBUG
    const PhysicsDiagnosticsView diagnosticsView = GetDiagnosticsView();
    m_stepDiagnostics.EmitStepDiagnostics( m_diagnosticsSuppressed, diagnosticsView, bodyStore, colliderStore, deltaSeconds,
                                           diagnosticsCsvWriter );

#else
    (void)bodyStore;
    (void)colliderStore;
    (void)deltaSeconds;
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


#endif


PhysicsDiagnosticsView PhysicsWorld::GetDiagnosticsView() const
{
    return PhysicsDiagnosticsView { m_contactSolverStage.GetPersistentContacts(),
                                    m_contactSolverStage.GetStats(),
                                    m_contactSolverStage.GetConvergenceTrace(),
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
                                    m_terrain.GetContactManifolds(),
                                    m_motionEligibility.State(),
                                    m_motionEligibility.LinearTravelSquared(),
                                    m_motionEligibility.AngularTravelSquared(),
                                    m_motionEligibility.Stats() };
}

uint64_t PhysicsWorld::CollectMemoryBytes() const
{
    uint64_t bytes = static_cast<uint64_t>( sizeof( *this ) );
    bytes += m_forceStage.CollectDynamicMemoryBytes();
    bytes += m_broadphase.CollectDynamicMemoryBytes();
    bytes += m_motionEligibility.CollectDynamicMemoryBytes();
    bytes += ListCapacityBytes( m_timeRemaining );
    bytes += m_sleepController.CollectDynamicMemoryBytes();
    bytes += m_stepDiagnostics.CollectDynamicMemoryBytes();
    bytes += m_contactSolverStage.CollectDynamicMemoryBytes();
    bytes += m_terrain.CollectDynamicMemoryBytes();
    bytes += m_narrowphase.CollectDynamicMemoryBytes();
    bytes += ListCapacityBytes( m_pointJointConstraints );
    bytes += m_externalForceStage.CollectMemoryBytes();
    return bytes;
}

uint64_t PhysicsWorld::CollectDebugAndBroadphaseMemoryBytes() const
{
    uint64_t bytes = m_broadphase.CollectDebugAndBroadphaseMemoryBytes();
    bytes += m_stepDiagnostics.CollectDebugMemoryBytes();
    bytes += m_sleepController.GetSleepIslandVisualIdCapacityBytes();
    return bytes;
}


const Math::CollisionDetection::SpatialGrid& PhysicsWorld::GetSpatialGrid() const
{
    return m_broadphase.GetSpatialGrid();
}


std::span<const int64_t> PhysicsWorld::GetCollisionCellKeys() const
{
    return m_broadphase.GetCollisionCellKeys();
}


std::span<const uint8_t> PhysicsWorld::GetCollisionVisualContacts() const
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


std::span<const PhysicsDebugContact> PhysicsWorld::GetPhysicsDebugContacts() const
{
    return m_stepDiagnostics.GetDebugContacts();
}


std::span<const PhysicsPipelineRecord> PhysicsWorld::GetPhysicsPipelineTrace() const
{
    return m_stepDiagnostics.GetPipelineTrace();
}

uint32_t PhysicsWorld::GetPhysicsPipelineRecordCount() const
{
    return m_stepDiagnostics.GetPipelineRecordCount();
}
