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

Invariants:
  - Physics-visible behavior must remain deterministic; byte-exact baselines
  are the validation contract.

Related:
  - SkullbonezSource/Physics/PhysicsWorld.h
  - Agentic/Reference/physics-overview.md
  - Agentic/Reference/comment-style-guide.md
*/
#include "PhysicsWorld.h"

#include "../Core/Config.h"
#include "PhysicsModelView.h"
#include "PhysicsBodyStore.h"
#include "ObjectContactManifold.h"
#include "../Core/Profiler.h"
#include "../Core/WorkerPool.h"

#include <algorithm>
#include <climits>
#include <cmath>
#include <cstdio>
#include <cstring>

using namespace SkullbonezCore::GameObjects;
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

namespace
{
constexpr size_t MAX_PIPELINE_TRACE_RECORDS = 4096;
constexpr int TERRAIN_BODY_INDEX = -1;
constexpr float TORNADO_EJECTION_PHASE_HZ = 10.0f;
constexpr float UNDERWATER_SLEEP_LOCK_SUBMERGED_PERCENT = 0.999f;
constexpr float EXPLICIT_WAKE_NEIGHBOR_SLOP = 0.50f;
constexpr float EXPLICIT_WAKE_VERTICAL_SLOP = 0.25f;
constexpr int PHYSICS_PARALLEL_MIN_BODIES = 256;
constexpr int PHYSICS_NARROWPHASE_PARALLEL_MIN_PAIRS = 256;
constexpr int PHYSICS_NARROWPHASE_PARALLEL_MIN_ISLANDS = 16;
constexpr int PHYSICS_NARROWPHASE_PARALLEL_MAX_AVG_PAIRS_PER_ISLAND = 4;
constexpr int PHYSICS_NARROWPHASE_PARALLEL_MAX_PAIRS_PER_BODY = 2;
constexpr bool PHYSICS_NARROWPHASE_ISLAND_WORKER_ENABLED = true;
constexpr float PHYSICS_FAST_SWEEP_MAX_RADIUS = 1.0f;
constexpr float PHYSICS_FAST_SWEEP_MIN_DISTANCE = 1.0f;
constexpr float PHYSICS_FAST_SWEEP_PAIR_SLOP = 1.0f;
constexpr float POINT_JOINT_SLEEP_MIN_ERROR_TOLERANCE = 0.15f;
constexpr float POINT_JOINT_SLEEP_SLACK_TOLERANCE_SCALE = 0.75f;
constexpr float POINT_JOINT_SLEEP_LINEAR_SPEED_SCALE = 6.0f;
constexpr float POINT_JOINT_SLEEP_ANGULAR_SPEED_SCALE = 6.0f;
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


PhysicsWorld::PhysicsWorld() : m_spatialGrid( Cfg().broadphaseCell )
{
    m_timeRemaining.reserve( MAX_GAME_MODELS );
    m_sleepSupportedThisFrame.reserve( MAX_GAME_MODELS );
    m_sleepInhibitedThisFrame.reserve( MAX_GAME_MODELS );
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
    m_persistentContacts.reserve( MAX_GAME_MODELS * 4 );
    m_persistentContactCache.reserve( MAX_GAME_MODELS * 4 );
    m_persistentContactCounts.reserve( MAX_GAME_MODELS );
    m_persistentRestingContactCounts.reserve( MAX_GAME_MODELS );
    m_solverBodies.reserve( MAX_GAME_MODELS );
    m_physicsDebugContacts.reserve( MAX_GAME_MODELS * 4 );
    m_physicsPipelineTrace.reserve( MAX_PIPELINE_TRACE_RECORDS );
    m_terrainContactManifolds.reserve( MAX_GAME_MODELS );
    m_terrainDetectionCandidates.reserve( MAX_GAME_MODELS );
    m_objectNarrowphaseEvents.reserve( MAX_GAME_MODELS * 4 );
    m_objectNarrowphaseIslands.reserve( MAX_GAME_MODELS );
    m_objectNarrowphaseParent.reserve( MAX_GAME_MODELS );
    m_objectNarrowphaseRank.reserve( MAX_GAME_MODELS );
    m_objectNarrowphaseRootToIsland.reserve( MAX_GAME_MODELS );
    m_pointJointConstraints.reserve( MAX_GAME_MODELS );
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
    m_objectNarrowphaseParent.clear();
    m_objectNarrowphaseRank.clear();
    m_objectNarrowphaseRootToIsland.clear();
    m_pointJointConstraints.clear();
    m_collisionCellKeys.clear();
}


void PhysicsWorld::CaptureReplaySolverSnapshot( ReplaySolverWorldSnapshot& outSnapshot, int modelCount ) const
{
    outSnapshot = ReplaySolverWorldSnapshot();
    outSnapshot.version = 2;
    outSnapshot.modelCount = modelCount;
    outSnapshot.nextSleepIslandVisualId = m_nextSleepIslandVisualId;
    outSnapshot.sleepEnabled = m_sleepEnabled;
    outSnapshot.collisionVisualFrameActive = m_collisionVisualFrameActive;
    outSnapshot.tornadoConfig = m_tornadoField.GetConfig();
    outSnapshot.tornadoSystemConfig = m_tornadoSystem.GetConfig();
    outSnapshot.tornadoSystemElapsedSeconds = m_tornadoSystem.GetElapsedSeconds();
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

    outSnapshot.persistentContacts.reserve( m_persistentContacts.size() );
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

    outSnapshot.persistentContactCache.reserve( m_persistentContactCache.size() );
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


bool PhysicsWorld::IsFullySubmergedBall( PhysicsModelView& modelView, const GameModelBodyStream& bodyStream, int index )
{
    auto& m_gameModels = modelView.Models();
    if ( index < 0 || index >= bodyStream.count || index >= static_cast<int>( m_gameModels.size() ) ||
         bodyStream.isFixed[index] || bodyStream.isBox[index] )
    {
        return false;
    }

    return m_gameModels[index].GetSubmergedVolumePercent() >= UNDERWATER_SLEEP_LOCK_SUBMERGED_PERCENT;
}


void PhysicsWorld::LockUnderwaterSleeperIfReady( PhysicsModelView& modelView,
                                                 PhysicsBodyStore& bodyStore,
                                                 const GameModelBodyStream& bodyStream,
                                                 int index )
{
    auto& m_gameModels = modelView.Models();
    EnsureUnderwaterSleepLockBuffer( bodyStream.count );
    if ( index < 0 || index >= bodyStream.count || index >= static_cast<int>( m_sleepState.size() ) ||
         !m_sleepState[index] || m_underwaterSleepLocked[index] ||
         !IsFullySubmergedBall( modelView, bodyStream, index ) )
    {
        return;
    }

    m_underwaterSleepLocked[index] = 1;
    if ( index < static_cast<int>( m_timeRemaining.size() ) )
    {
        m_timeRemaining[index] = 0.0f;
    }
    PhysicsBodyRecord* record = bodyStore.MutableRecordForModelIndex( index );
    if ( record )
    {
        record->linearVelocity = ZERO_VECTOR;
        record->angularVelocity = ZERO_VECTOR;
        record->isSleeping = true;
        bodyStore.WriteBackToModelAt( m_gameModels, index );
    }
}


bool PhysicsWorld::IsUnderwaterSleepLocked( PhysicsModelView& modelView,
                                            const GameModelBodyStream& bodyStream,
                                            int index )
{
    (void)modelView;
    EnsureUnderwaterSleepLockBuffer( bodyStream.count );
    if ( index < 0 || index >= bodyStream.count )
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


void PhysicsWorld::MarkFixedContact( PhysicsModelView& modelView, int index )
{
    auto& m_gameModels = modelView.Models();
    if ( index < 0 || index >= static_cast<int>( m_gameModels.size() ) )
    {
        return;
    }
    if ( m_gameModels[index].IsFixed() )
    {
        m_gameModels[index].NotifyFixedContact( 0.5f );
    }
}


void PhysicsWorld::RecordPhysicsPipelineStage( const PhysicsPipelineRecord& record )
{
    if ( m_physicsPipelineTrace.size() < MAX_PIPELINE_TRACE_RECORDS )
    {
        m_physicsPipelineTrace.push_back( record );
    }
}


PersistentContactSolverContext PhysicsWorld::CreatePersistentContactSolverContext( PhysicsBodyStore& bodyStore )
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
                                           bodyStore.MutableRecords(),
                                           bodyStore,
                                           *this };
}


SleepSupportPropagationContext PhysicsWorld::CreateSleepSupportPropagationContext()
{
    return SleepSupportPropagationContext{ m_sleepState, m_sleepSupportEdges, m_sleepSupportedThisFrame };
}


void PersistentContactSolverContext::RecordPhysicsPipelineStage( const PhysicsPipelineRecord& record ) const
{
    world.RecordSolverPhysicsPipelineStage( record );
}


void PersistentContactSolverContext::MarkCollisionVisualContact( int index ) const
{
    world.MarkSolverCollisionVisualContact( index );
}


void PersistentContactSolverContext::MarkFixedContact( PhysicsModelView& modelView, int index ) const
{
    world.MarkSolverFixedContact( modelView, index );
}


void PersistentContactSolverContext::WakeModel( PhysicsModelView& modelView, int index ) const
{
    world.WakeModel( modelView, index );
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


void PhysicsWorld::AddPointJointConstraint( const PointJointConstraint& constraint )
{
    if ( constraint.bodyA < 0 || constraint.bodyB < 0 || constraint.bodyA == constraint.bodyB )
    {
        return;
    }
    m_pointJointConstraints.push_back( constraint );
}


const std::vector<PointJointConstraint>& PhysicsWorld::GetPointJointConstraints() const
{
    return m_pointJointConstraints;
}


void PhysicsWorld::RunPhysics( PhysicsModelView& modelView, PhysicsBodyStore& bodyStore, float fChangeInTime )
{
    // Concept: one fixed physics tick has a predictable data flow.
    //
    // 1. Resize/clear per-frame arrays so every model index has a slot.
    // 2. Reset debug, sleep-support, pipeline, and terrain-manifold output.
    // 3. Refresh the SoA cache from GameModel/RigidBody state for hot loops.
    // 4. Run broadphase, swept movement, terrain manifold generation, and the
    //    persistent Catto-style contact solver.
    // 5. Emit bounded Debug diagnostics, then invalidate cached render/physics
    //    SoA data because solver writeback may have changed body state.
    //
    // Determinism note: changing this ordering can change byte-exact physics
    // baselines even when the final scene "looks" similar.
    auto& m_gameModels = modelView.Models();
    const int modelCount = static_cast<int>( m_gameModels.size() );
    bodyStore.WriteBackToModels( m_gameModels );
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

    for ( int i = 0; i < modelCount; ++i )
    {
        m_gameModels[i].TickFixedContactHighlight( fChangeInTime );
    }

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
        if ( m_gameModels[i].IsFixed() )
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

    (void)modelView.GetBodyStream();
    RunSolverPhysics( modelView, bodyStore, fChangeInTime );
    bodyStore.CopySleepStatesFrom( m_sleepState );

#ifdef _DEBUG
    if ( !m_diagnosticsSuppressed )
    {
        m_diagnostics.EmitRegressionLog( *this, modelView );
        m_diagnostics.IncrementCollisionTimeFrameIfEnabled();
        m_diagnostics.EmitFrame( modelView, fChangeInTime );
    }
#endif

    modelView.InvalidatePhysicsStreams();
}


void PhysicsWorld::WakeModel( PhysicsModelView& modelView, int index )
{
    auto& m_gameModels = modelView.Models();
    if ( index >= 0 && index < static_cast<int>( m_gameModels.size() ) && m_gameModels[index].IsFixed() )
    {
        return;
    }

    const int modelCount = static_cast<int>( m_gameModels.size() );
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
        GameModelBodyStream bodyStream = modelView.GetBodyStream();
        if ( !m_underwaterSleepLocked[index] && m_sleepState[index] &&
             IsFullySubmergedBall( modelView, bodyStream, index ) )
        {
            m_underwaterSleepLocked[index] = 1;
            if ( index < static_cast<int>( m_timeRemaining.size() ) )
            {
                m_timeRemaining[index] = 0.0f;
            }
            return;
        }
        if ( IsUnderwaterSleepLocked( modelView, bodyStream, index ) )
        {
            return;
        }
    }
    if ( index >= 0 && index < static_cast<int>( m_sleepState.size() ) )
    {
        modelView.InvalidatePhysicsStreams();
        WakeSleepVisualIsland( modelView, nullptr, index, 0.0f, false );
        WakePointJointIsland( modelView, nullptr, index, 0.0f, false );
        WakeRestingContactIsland( modelView, nullptr, index, 0.0f, false );
    }
}


void PhysicsWorld::SeedModelAsleep( PhysicsModelView& modelView, int index )
{
    if ( !m_sleepEnabled )
    {
        return;
    }

    auto& m_gameModels = modelView.Models();
    if ( index < 0 || index >= static_cast<int>( m_gameModels.size() ) || m_gameModels[index].IsFixed() )
    {
        return;
    }

    const int modelCount = static_cast<int>( m_gameModels.size() );
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

    modelView.InvalidatePhysicsStreams();
    m_sleepState[index] = 1;
    m_sleepCounter[index] = static_cast<uint8_t>( (std::min)( Cfg().physicsSleepFrames, 255 ) );
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


void PhysicsWorld::ApplyTornadoField( PhysicsModelView& modelView, PhysicsBodyStore& bodyStore, float dt )
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
    auto& m_gameModels = modelView.Models();
    std::vector<PhysicsBodyRecord>& bodyRecords = bodyStore.MutableRecords();
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

    bool releasedFixedParts = false;
    if ( useSystem )
    {
        for ( int i = 0; i < static_cast<int>( m_gameModels.size() ); ++i )
        {
            GameModel& model = m_gameModels[static_cast<size_t>( i )];
            if ( !model.IsFixed() || !model.ReleasesFromFixedOnContact() )
            {
                continue;
            }

            TornadoFieldConfig bestConfig;
            float bestAccelerationSq = 0.0f;
            const Vector3 acceleration =
                sampleAcceleration( bodyRecords[static_cast<size_t>( i )].position, bestConfig, bestAccelerationSq );
            const float releaseAcceleration = (std::max)( 16.0f, model.GetContactReleaseImpulseThreshold() * 32.0f );
            if ( bestAccelerationSq < releaseAcceleration * releaseAcceleration )
            {
                continue;
            }

            const Vector3 seedLinearVelocity =
                ClampVectorMagnitude( acceleration * 0.08f, (std::max)( 10.0f, bestConfig.maxDeltaVelocity * 1.5f ) );
            model.SetFixed( false );
            bodyStore.CaptureMutableStateFromModelAt( m_gameModels, i );
            PhysicsBodyRecord& record = bodyRecords[static_cast<size_t>( i )];
            record.linearVelocity = seedLinearVelocity;
            record.angularVelocity = Vector3( seedLinearVelocity.z * 0.08f, 0.0f, -seedLinearVelocity.x * 0.08f );
            bodyStore.WriteBackToModelAt( m_gameModels, i );
            WakeModel( modelView, i );
            modelView.ReleaseAttachedFixedTreeParts(
                i,
                seedLinearVelocity,
                Vector3( seedLinearVelocity.z * 0.08f, 0.0f, -seedLinearVelocity.x * 0.08f ) );
            releasedFixedParts = true;
        }
    }
    if ( releasedFixedParts )
    {
        bodyStore.LoadFromModels( m_gameModels, m_sleepState );
        modelView.InvalidatePhysicsStreams();
    }

    const GameModelBodyStream bodyStream = modelView.GetBodyStream();
    const int modelCount = bodyStream.count;
    EnsureTornadoStateBuffers( modelCount );

    auto applyTornadoAt = [&]( int i )
    {
        if ( bodyStream.isFixed[i] || IsUnderwaterSleepLocked( modelView, bodyStream, i ) )
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
            bodyStore.ApplyCompatibilityForces( m_gameModels, i, dt );
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
        bodyStore.WriteBackToModelAt( m_gameModels, i );
    };

    if ( Cfg().physicsParallel && Cfg().physicsParallelTornadoField )
    {
        SkullbonezCore::Threading::WorkerPool::Instance().ParallelFor( 0,
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


void PhysicsWorld::RenderTornadoFieldVectors( const Math::Transformation::Matrix4& viewProj )
{
    if ( m_tornadoSystem.IsEnabled() )
    {
        m_tornadoSystem.RenderVectors( viewProj );
        return;
    }
    m_tornadoField.RenderVectors( viewProj );
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


void PhysicsWorld::EmitPhysicsDiagnosticsFrame( PhysicsModelView& modelView, float dt )
{
    if ( m_diagnosticsSuppressed )
    {
        return;
    }
    m_diagnostics.EmitFrame( modelView, dt );
}
#endif


void PhysicsWorld::EmitPhysicsCollisionTime( PhysicsModelView& modelView,
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
    m_diagnostics.EmitCollisionTime( modelView, type, bodyA, bodyB, collisionTime, availableTime );
}


void PhysicsWorld::PropagateSleepSupport( PhysicsModelView& modelView )
{
    SleepSupportPropagationContext context = CreateSleepSupportPropagationContext();
    m_sleepIslandSystem.PropagateSupport( context, modelView );
}


void PhysicsWorld::AppendPointJointSupportEdges( int modelCount )
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
        const int a = constraint.bodyA;
        const int b = constraint.bodyB;
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


bool PhysicsWorld::WakeDynamicBodyState( PhysicsModelView& modelView,
                                         PhysicsBodyStore* bodyStore,
                                         int index,
                                         float dt,
                                         bool applyForces )
{
    auto& models = modelView.Models();
    if ( index < 0 || index >= static_cast<int>( models.size() ) || models[index].IsFixed() ||
         index >= static_cast<int>( m_sleepState.size() ) )
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
        bodyStore->WakeBody( index );
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
    if ( applyForces && wasSleeping && dt > TOLERANCE )
    {
        if ( bodyStore )
        {
            bodyStore->ApplyCompatibilityForces( models, index, dt );
        }
        else
        {
            models[index].ApplyForces( dt );
        }
    }
    // Hazard: waking a body must also forget any cached contact impulses that
    // involve that body. Warm-start impulses are great for resting contact, but
    // stale impulses after a manual wake or external force can push the body as
    // if an old support contact still existed.
    ForgetPersistentContactCacheForBody( index );

    return wasSleeping || hadCounter || hadSleepVisual || wasUnderwaterLocked;
}


void PhysicsWorld::WakeSleepVisualIsland( PhysicsModelView& modelView,
                                          PhysicsBodyStore* bodyStore,
                                          int index,
                                          float dt,
                                          bool applyForces )
{
    // Concept: m_sleepIslandVisualId is the persisted identity of a group that
    // deactivated together. Contacts may be pruned while the group sleeps, so
    // this id is the cheap way to wake the whole resting pile again.
    if ( index < 0 || index >= static_cast<int>( m_sleepState.size() ) )
    {
        return;
    }

    const int visualId = index < static_cast<int>( m_sleepIslandVisualId.size() ) ? m_sleepIslandVisualId[index] : 0;
    bool changed = false;
    if ( visualId > 0 )
    {
        const int count = static_cast<int>( m_sleepIslandVisualId.size() );
        for ( int i = 0; i < count; ++i )
        {
            if ( m_sleepIslandVisualId[i] == visualId )
            {
                changed = WakeDynamicBodyState( modelView, bodyStore, i, dt, applyForces ) || changed;
            }
        }
    }
    else
    {
        changed = WakeDynamicBodyState( modelView, bodyStore, index, dt, applyForces );
    }

    if ( changed )
    {
        modelView.InvalidatePhysicsStreams();
    }
}


void PhysicsWorld::WakePointJointIsland( PhysicsModelView& modelView,
                                         PhysicsBodyStore* bodyStore,
                                         int index,
                                         float dt,
                                         bool applyForces )
{
    // Hazard: solving a ragdoll with one awake piece and several sleeping pieces
    // treats the sleepers as temporary static anchors. Wake the whole constraint
    // component so later point-joint impulses are applied to the same live island.
    auto& models = modelView.Models();
    const int modelCount = static_cast<int>( models.size() );
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

    auto findIsland = [&]( int bodyIndex ) -> int
    {
        int root = bodyIndex;
        while ( m_sleepIslandParent[root] != root )
        {
            root = m_sleepIslandParent[root];
        }
        while ( m_sleepIslandParent[bodyIndex] != bodyIndex )
        {
            int parent = m_sleepIslandParent[bodyIndex];
            m_sleepIslandParent[bodyIndex] = root;
            bodyIndex = parent;
        }
        return root;
    };

    auto unionIslands = [&]( int a, int b )
    {
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

    for ( const PointJointConstraint& constraint : m_pointJointConstraints )
    {
        // Point-joint constraints are sleep-island edges. This keeps the current
        // ragdoll behavior aligned with the future generic constraint system:
        // constraints decide connectivity, contacts decide physical impulses.
        const int a = constraint.bodyA;
        const int b = constraint.bodyB;
        if ( a < 0 || b < 0 || a == b || a >= modelCount || b >= modelCount )
        {
            continue;
        }

        m_sleepPointJointBody[a] = 1;
        m_sleepPointJointBody[b] = 1;
        unionIslands( a, b );
    }

    if ( m_sleepPointJointBody[index] == 0 )
    {
        return;
    }

    const int root = findIsland( index );
    bool changed = false;
    for ( int i = 0; i < modelCount; ++i )
    {
        if ( m_sleepPointJointBody[i] == 0 || findIsland( i ) != root )
        {
            continue;
        }
        changed = WakeDynamicBodyState( modelView, bodyStore, i, dt, applyForces ) || changed;
    }

    if ( changed )
    {
        modelView.InvalidatePhysicsStreams();
    }
}


void PhysicsWorld::WakeRestingContactIsland( PhysicsModelView& modelView,
                                             PhysicsBodyStore* bodyStore,
                                             int index,
                                             float dt,
                                             bool applyForces )
{
    auto& models = modelView.Models();
    const int modelCount = static_cast<int>( models.size() );
    if ( index < 0 || index >= modelCount || index >= static_cast<int>( m_sleepState.size() ) )
    {
        return;
    }

    GameModelBodyStream bodyStream = modelView.GetBodyStream();
    const std::vector<PhysicsBodyRecord>* bodyRecords = bodyStore ? &bodyStore->Records() : nullptr;
    std::vector<uint8_t> visited( static_cast<size_t>( modelCount ), 0 );
    std::vector<int> wakeQueue;
    wakeQueue.reserve( static_cast<size_t>( modelCount ) );
    visited[static_cast<size_t>( index )] = 1;
    wakeQueue.push_back( index );

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
        const Vector3 posA = bodyRecords ? ( *bodyRecords )[static_cast<size_t>( a )].position
                                         : models[static_cast<size_t>( a )].GetPosition();
        const Vector3 posB = bodyRecords ? ( *bodyRecords )[static_cast<size_t>( b )].position
                                         : models[static_cast<size_t>( b )].GetPosition();
        const float radiusA = (std::max)( 0.01f, models[static_cast<size_t>( a )].GetBoundingRadius() );
        const float radiusB = (std::max)( 0.01f, models[static_cast<size_t>( b )].GetBoundingRadius() );
        if ( posB.y + radiusB + EXPLICIT_WAKE_VERTICAL_SLOP < posA.y - radiusA )
        {
            return false;
        }

        const float range = radiusA + radiusB + EXPLICIT_WAKE_NEIGHBOR_SLOP;
        const Vector3 delta = posB - posA;
        return delta * delta <= range * range;
    };

    bool changed = false;
    for ( size_t cursor = 0; cursor < wakeQueue.size(); ++cursor )
    {
        const int current = wakeQueue[cursor];
        for ( int candidate = 0; candidate < modelCount; ++candidate )
        {
            if ( visited[static_cast<size_t>( candidate )] || candidate >= static_cast<int>( m_sleepState.size() ) ||
                 m_sleepState[candidate] == 0 || bodyStream.isFixed[candidate] )
            {
                continue;
            }
            if ( IsUnderwaterSleepLocked( modelView, bodyStream, candidate ) )
            {
                continue;
            }
            if ( !hasPersistentContactEdge( current, candidate ) && !isLikelyRestingNeighbor( current, candidate ) )
            {
                continue;
            }

            visited[static_cast<size_t>( candidate )] = 1;
            wakeQueue.push_back( candidate );
            changed = WakeDynamicBodyState( modelView, bodyStore, candidate, dt, applyForces ) || changed;
        }
    }

    if ( changed )
    {
        modelView.InvalidatePhysicsStreams();
    }
}


bool PhysicsWorld::IsPointJointPair( int bodyA, int bodyB ) const
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
        int jointA = constraint.bodyA;
        int jointB = constraint.bodyB;
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


void PhysicsWorld::WakePointJointConnectedBodies( PhysicsModelView& modelView, PhysicsBodyStore& bodyStore, float dt )
{
    if ( m_pointJointConstraints.empty() || static_cast<int>( m_sleepState.size() ) <= 0 )
    {
        return;
    }

    auto& models = modelView.Models();
    const std::vector<PhysicsBodyRecord>& bodyRecords = bodyStore.Records();
    const int modelCount = static_cast<int>( models.size() );
    m_sleepIslandParent.assign( modelCount, 0 );
    m_sleepIslandRank.assign( modelCount, 0 );
    m_sleepPointJointBody.assign( modelCount, 0 );
    m_sleepIslandHasAwake.assign( modelCount, 0 );
    m_sleepIslandCanSleep.assign( modelCount, 0 );
    for ( int i = 0; i < modelCount; ++i )
    {
        m_sleepIslandParent[i] = i;
    }

    auto findIsland = [&]( int index ) -> int
    {
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

    for ( const PointJointConstraint& constraint : m_pointJointConstraints )
    {
        // Concept: point-joint edges define the constrained component. If one
        // piece is awake, any sleeping neighbors must wake before the solver
        // applies joint impulses against them as static anchors.
        const int a = constraint.bodyA;
        const int b = constraint.bodyB;
        if ( a < 0 || b < 0 || a == b || a >= modelCount || b >= modelCount ||
             a >= static_cast<int>( m_sleepState.size() ) || b >= static_cast<int>( m_sleepState.size() ) )
        {
            continue;
        }

        m_sleepPointJointBody[a] = 1;
        m_sleepPointJointBody[b] = 1;
        unionIslands( a, b );
    }

    for ( int i = 0; i < modelCount; ++i )
    {
        if ( m_sleepPointJointBody[i] == 0 || bodyRecords[static_cast<size_t>( i )].isFixed )
        {
            continue;
        }

        const int root = findIsland( i );
        if ( i < static_cast<int>( m_sleepState.size() ) && m_sleepState[i] != 0 )
        {
            m_sleepIslandCanSleep[root] = 1;
        }
        else
        {
            m_sleepIslandHasAwake[root] = 1;
        }
    }

    bool changed = false;
    for ( int i = 0; i < modelCount; ++i )
    {
        if ( m_sleepPointJointBody[i] == 0 || bodyRecords[static_cast<size_t>( i )].isFixed ||
             i >= static_cast<int>( m_sleepState.size() ) || m_sleepState[i] == 0 )
        {
            continue;
        }

        const int root = findIsland( i );
        if ( m_sleepIslandHasAwake[root] != 0 && m_sleepIslandCanSleep[root] != 0 )
        {
            changed = WakeDynamicBodyState( modelView, &bodyStore, i, dt, true ) || changed;
        }
    }

    if ( changed )
    {
        modelView.InvalidatePhysicsStreams();
    }
}


void PhysicsWorld::RunSolverPhysics( PhysicsModelView& modelView, PhysicsBodyStore& bodyStore, float dt )
{
    auto& m_gameModels = modelView.Models();
    const GameModelBodyStream bodyStream = modelView.GetBodyStream();
    const int modelCount = bodyStream.count;
    std::vector<PhysicsBodyRecord>& bodyRecords = bodyStore.MutableRecords();

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
    const float sleepLinear = (std::max)( 0.0f, Cfg().physicsSleepLinearSpeed );
    const float sleepAngular = (std::max)( 0.0f, Cfg().physicsSleepAngularSpeed );
    const float SLEEP_LINEAR_SQ = sleepLinear * sleepLinear;
    const float SLEEP_ANGULAR_SQ = sleepAngular * sleepAngular;
    const uint8_t SLEEP_FRAMES = static_cast<uint8_t>( (std::max)( 1, (std::min)( Cfg().physicsSleepFrames, 255 ) ) );

    EnsureUnderwaterSleepLockBuffer( modelCount );
    for ( int x = 0; x < modelCount; ++x )
    {
        if ( m_sleepState[x] )
        {
            LockUnderwaterSleeperIfReady( modelView, bodyStore, bodyStream, x );
        }
    }

    // Sleeping bodies keep cached state until a contact or scene change wakes
    // them, so force integration only runs for awake rows.
    PROFILE_BEGIN( "Frame/Physics/ApplyForces" );
    auto applyForcesAt = [&]( int x )
    {
        if ( bodyStream.isFixed[x] )
        {
            return;
        }
        if ( m_sleepState[x] )
        {
            m_timeRemaining[x] = 0.0f;
            return;
        }
        bodyStore.ApplyCompatibilityForces( m_gameModels, x, dt );
    };

    if ( Cfg().physicsParallel && Cfg().physicsParallelApplyForces )
    {
        SkullbonezCore::Threading::WorkerPool::Instance().ParallelFor( 0,
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

    ApplyTornadoField( modelView, bodyStore, dt );

    // Broadphase: build spatial grid from all object positions (include sleeping for wake detection)
    PROFILE_BEGIN( "Frame/Physics/Broadphase" );
    std::vector<std::pair<int, int>>& candidatePairs = m_candidatePairs;
    {
        PROFILE_SCOPED( "Frame/Physics/Broadphase/GridBuild" );
        m_spatialGrid.Clear();
        m_collisionCellKeys.clear();
        for ( int i = 0; i < modelCount; ++i )
        {
            const float radius = bodyStream.boundingRadii[i];
            const Vector3 displacement = bodyRecords[static_cast<size_t>( i )].linearVelocity * dt;
            const float displacementSq = Vector::VectorMagSquared( displacement );
            if ( !bodyStream.isFixed[i] && displacementSq > radius * radius )
            {
                m_spatialGrid.InsertSwept( i, bodyStream.positions[i], displacement, radius );
            }
            else
            {
                m_spatialGrid.Insert( i, bodyStream.positions[i], radius );
            }
        }
        m_spatialGrid.GetCandidatePairs( candidatePairs );
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
        if ( bodyStream.isFixed[index] )
        {
            return false;
        }

        const float radius = bodyStream.boundingRadii[index];
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
        const Vector3 relativeStart = bodyStream.positions[movingIndex] - bodyStream.positions[targetIndex];
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
        const float expandedRadius = bodyStream.boundingRadii[movingIndex] + bodyStream.boundingRadii[targetIndex] +
                                     Cfg().contactEpsilon + PHYSICS_FAST_SWEEP_PAIR_SLOP;
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
                                                         bodyStream.isFixed[a] && bodyStream.isFixed[b];
                                              } ),
                              candidatePairs.end() );
    }

    if ( !m_pointJointConstraints.empty() )
    {
        PROFILE_SCOPED( "Frame/Physics/Broadphase/PruneJointPairs" );
        candidatePairs.erase( std::remove_if( candidatePairs.begin(),
                                              candidatePairs.end(),
                                              [&]( const std::pair<int, int>& pair )
                                              { return IsPointJointPair( pair.first, pair.second ); } ),
                              candidatePairs.end() );
    }

    {
        PROFILE_SCOPED( "Frame/Physics/Broadphase/RecordCandidates" );
        for ( const auto& pair : candidatePairs )
        {
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
                                if ( prune )
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
        if ( sleepingIndex < 0 || sleepingIndex >= modelCount || bodyStream.isFixed[sleepingIndex] ||
             !m_sleepState[sleepingIndex] || IsUnderwaterSleepLocked( modelView, bodyStream, sleepingIndex ) )
        {
            return;
        }

        m_sleepState[sleepingIndex] = 0;
        m_sleepCounter[sleepingIndex] = 0;
        m_sleepIslandVisualId[sleepingIndex] = 0;
        m_timeRemaining[sleepingIndex] = dt;
        bodyRecords[static_cast<size_t>( sleepingIndex )].isSleeping = false;
        bodyStore.ApplyCompatibilityForces( m_gameModels, sleepingIndex, dt );
    };

    auto hasPersistentWakeContact = [&]( int awakeIndex, int sleepingIndex ) -> bool
    {
        PROFILE_SCOPED( "Frame/Physics/Narrowphase/WakePersistentContact" );

        // A swept test can miss a sleeper that is already overlapping after an
        // awake body's correction step. This fresh manifold test catches that
        // persistent contact so the sleeper cannot remain frozen inside the
        // awake body until a later frame happens to generate a swept hit.
        ObjectContactManifold manifold;
        return BuildObjectContactManifold( m_gameModels[awakeIndex],
                                           m_gameModels[sleepingIndex],
                                           awakeIndex,
                                           sleepingIndex,
                                           Cfg().contactEpsilon,
                                           manifold );
    };

    auto hasObjectContactAtTime = [&]( int a, int b, float time ) -> bool
    {
        PROFILE_SCOPED( "Frame/Physics/Narrowphase/ExactContactAtTime" );

        // Temporarily place both bodies at a candidate time, ask the exact
        // narrowphase whether they touch there, then restore positions. This is
        // a query only; it must leave the world exactly as it found it.
        const Vector3 startA = bodyRecords[static_cast<size_t>( a )].position;
        const Vector3 startB = bodyRecords[static_cast<size_t>( b )].position;
        bodyRecords[static_cast<size_t>( a )].position =
            startA + bodyRecords[static_cast<size_t>( a )].linearVelocity * time;
        bodyRecords[static_cast<size_t>( b )].position =
            startB + bodyRecords[static_cast<size_t>( b )].linearVelocity * time;
        bodyStore.WriteBackToModelAt( m_gameModels, a );
        bodyStore.WriteBackToModelAt( m_gameModels, b );

        ObjectContactManifold manifold;
        const bool hit =
            BuildObjectContactManifold( m_gameModels[a], m_gameModels[b], a, b, Cfg().contactEpsilon, manifold );

        bodyRecords[static_cast<size_t>( a )].position = startA;
        bodyRecords[static_cast<size_t>( b )].position = startB;
        bodyStore.WriteBackToModelAt( m_gameModels, a );
        bodyStore.WriteBackToModelAt( m_gameModels, b );
        return hit;
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

    auto sweepObjectPair = [&]( int a, int b, float availableTime ) -> GameModel::ObjectSweepResult
    {
        PROFILE_SCOPED( "Frame/Physics/Narrowphase/SweepPairs" );
        return m_gameModels[a].SweepGameModel( m_gameModels[b], availableTime );
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
            EmitPhysicsCollisionTime( modelView,
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
                const bool sleepingLocked = IsUnderwaterSleepLocked( modelView, bodyStream, x );
                if ( !hasWakeEnergy( y ) )
                {
                    return;
                }
                // Swept impact wakes immediately when time remains; persistent
                // overlap wakes too so sleepers cannot stay frozen after a hit.
                bool wokeBySweptImpact = false;
                if ( m_timeRemaining[y] > 0.0f )
                {
                    GameModel::ObjectSweepResult sweep = sweepObjectPair( y, x, m_timeRemaining[y] );
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

                        bodyStore.IntegrateBodyPose( m_gameModels, y, colTime );
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
                const bool sleepingLocked = IsUnderwaterSleepLocked( modelView, bodyStream, y );
                if ( !hasWakeEnergy( x ) )
                {
                    return;
                }
                bool wokeBySweptImpact = false;
                if ( m_timeRemaining[x] > 0.0f )
                {
                    GameModel::ObjectSweepResult sweep = sweepObjectPair( x, y, m_timeRemaining[x] );
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

                        bodyStore.IntegrateBodyPose( m_gameModels, x, colTime );
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
        GameModel::ObjectSweepResult sweep = sweepObjectPair( x, y, availableTime );

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

            bodyStore.IntegrateBodyPose( m_gameModels, x, colTime );
            bodyStore.IntegrateBodyPose( m_gameModels, y, colTime );
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
        for ( int pairIndex : island.pairIndices )
        {
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

        auto findObjectNarrowphaseRoot = [&]( int index ) -> int
        {
            int root = index;
            while ( m_objectNarrowphaseParent[static_cast<size_t>( root )] != root )
            {
                root = m_objectNarrowphaseParent[static_cast<size_t>( root )];
            }
            while ( m_objectNarrowphaseParent[static_cast<size_t>( index )] != index )
            {
                const int next = m_objectNarrowphaseParent[static_cast<size_t>( index )];
                m_objectNarrowphaseParent[static_cast<size_t>( index )] = root;
                index = next;
            }
            return root;
        };

        auto unionObjectNarrowphaseRoots = [&]( int a, int b )
        {
            int rootA = findObjectNarrowphaseRoot( a );
            int rootB = findObjectNarrowphaseRoot( b );
            if ( rootA == rootB )
            {
                return;
            }

            if ( m_objectNarrowphaseRank[static_cast<size_t>( rootA )] <
                 m_objectNarrowphaseRank[static_cast<size_t>( rootB )] )
            {
                std::swap( rootA, rootB );
            }
            m_objectNarrowphaseParent[static_cast<size_t>( rootB )] = rootA;
            if ( m_objectNarrowphaseRank[static_cast<size_t>( rootA )] ==
                 m_objectNarrowphaseRank[static_cast<size_t>( rootB )] )
            {
                ++m_objectNarrowphaseRank[static_cast<size_t>( rootA )];
            }
        };

        for ( int pairIndex = 0; pairIndex < candidatePairCount; ++pairIndex )
        {
            const int x = candidatePairs[static_cast<size_t>( pairIndex )].first;
            const int y = candidatePairs[static_cast<size_t>( pairIndex )].second;
            if ( x < 0 || y < 0 || x >= modelCount || y >= modelCount )
            {
                continue;
            }
            unionObjectNarrowphaseRoots( x, y );
        }

        m_objectNarrowphaseIslands.clear();
        m_objectNarrowphaseRootToIsland.assign( static_cast<size_t>( modelCount ), -1 );
        for ( int pairIndex = 0; pairIndex < candidatePairCount; ++pairIndex )
        {
            const int x = candidatePairs[static_cast<size_t>( pairIndex )].first;
            const int y = candidatePairs[static_cast<size_t>( pairIndex )].second;
            if ( x < 0 || y < 0 || x >= modelCount || y >= modelCount )
            {
                continue;
            }

            const int root = findObjectNarrowphaseRoot( x );
            int islandIndex = m_objectNarrowphaseRootToIsland[static_cast<size_t>( root )];
            if ( islandIndex < 0 )
            {
                islandIndex = static_cast<int>( m_objectNarrowphaseIslands.size() );
                m_objectNarrowphaseRootToIsland[static_cast<size_t>( root )] = islandIndex;
                m_objectNarrowphaseIslands.push_back( ObjectNarrowphaseIsland() );
                m_objectNarrowphaseIslands.back().minPairIndex = INT_MAX;
            }

            ObjectNarrowphaseIsland& island = m_objectNarrowphaseIslands[static_cast<size_t>( islandIndex )];
            island.minPairIndex = (std::min)( island.minPairIndex, pairIndex );
            island.pairIndices.push_back( pairIndex );
        }
        std::sort( m_objectNarrowphaseIslands.begin(),
                   m_objectNarrowphaseIslands.end(),
                   []( const ObjectNarrowphaseIsland& a, const ObjectNarrowphaseIsland& b )
                   { return a.minPairIndex < b.minPairIndex; } );
    };

    m_objectNarrowphaseIslands.clear();
    bool ranParallelNarrowphase = false;
    const bool mayBenefitFromIslandDispatch =
        PHYSICS_NARROWPHASE_ISLAND_WORKER_ENABLED && Cfg().physicsParallel && Cfg().physicsParallelNarrowphase &&
        candidatePairCount >= PHYSICS_NARROWPHASE_PARALLEL_MIN_PAIRS &&
        candidatePairCount <= modelCount * PHYSICS_NARROWPHASE_PARALLEL_MAX_PAIRS_PER_BODY &&
        SkullbonezCore::Threading::WorkerPool::Instance().GetThreadCount() > 0;
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
                SkullbonezCore::Threading::WorkerPool::Instance().ParallelFor(
                    0,
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
        if ( bodyStream.isFixed[x] )
        {
            return;
        }
        if ( m_sleepState[x] || m_timeRemaining[x] <= 0.0f )
        {
            return;
        }

        candidate.availableTime = m_timeRemaining[x];
        candidate.collisionTime = m_gameModels[x].CollisionDetectTerrain( candidate.availableTime );
        candidate.tested = 1;
    };

    auto commitTerrainCandidate = [&]( int x, float availableTime, float colTime )
    {
        if ( m_gameModels[x].IsResponseRequired() )
        {
            bodyStore.IntegrateBodyPose( m_gameModels, x, colTime );
            const float remainingTime = (std::max)( 0.0f, availableTime - colTime );
            // BuildTerrainContactManifold is the handoff from terrain-specific
            // collision data to solver-neutral contact geometry. The old
            // response-required flag is now just a detection latch; clear it
            // once the manifold is captured so no later path can replay terrain
            // response work.
            Physics::TerrainContactManifold manifold;
            const bool hasManifold = m_gameModels[x].BuildTerrainContactManifold( x, colTime, availableTime, manifold );
            m_gameModels[x].ClearResponseRequired();

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
            EmitPhysicsCollisionTime( modelView, "terrain", x, -1, colTime, availableTime );

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
    if ( Cfg().physicsParallel && Cfg().physicsParallelTerrainDetect )
    {
        SkullbonezCore::Threading::WorkerPool::Instance().ParallelFor( 0,
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
            commitTerrainCandidate( x, candidate.availableTime, candidate.collisionTime );
        }
    }
    PROFILE_END( "Frame/Physics/Terrain/Detect" );
    PROFILE_END( "Frame/Physics/Terrain" );

    PersistentContactSolverContext solverContext = CreatePersistentContactSolverContext( bodyStore );
    m_contactSolver.Solve( solverContext, modelView, dt );
    WakePointJointConnectedBodies( modelView, bodyStore, dt );
    Ragdoll::SolvePointJoints( modelView, bodyStore, m_pointJointConstraints, m_sleepState, dt );
    AppendPointJointSupportEdges( modelCount );
    // Object contacts are converted into stack support only after terrain
    // response has had a chance to seed true support for this frame.
    PropagateSleepSupport( modelView );

    // Integrate remaining time for awake models
    PROFILE_BEGIN( "Frame/Physics/Integrate" );
    auto integrateRemainingAt = [&]( int x )
    {
        if ( bodyStream.isFixed[x] )
        {
            return;
        }
        if ( m_sleepState[x] )
        {
            return;
        }

        if ( m_timeRemaining[x] > 0.0f )
        {
            bodyStore.IntegrateBodyPose( m_gameModels, x, m_timeRemaining[x] );
        }
    };

    if ( Cfg().physicsParallel && Cfg().physicsParallelIntegrate )
    {
        SkullbonezCore::Threading::WorkerPool::Instance().ParallelFor( 0,
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
        const int a = constraint.bodyA;
        const int b = constraint.bodyB;
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
        if ( bodyStream.isFixed[x] || ( x < static_cast<int>( m_sleepState.size() ) && m_sleepState[x] != 0 ) ||
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
        const int a = constraint.bodyA;
        const int b = constraint.bodyB;
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
        if ( bodyStream.isFixed[x] )
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
        if ( bodyStream.isFixed[x] )
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
        if ( bodyStream.isFixed[x] )
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
        if ( bodyStream.isFixed[x] )
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
        if ( bodyStream.isFixed[x] )
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
            bodyStore.WriteBackToModelAt( m_gameModels, x );
            LockUnderwaterSleeperIfReady( modelView, bodyStore, bodyStream, x );
        }
    }
    PROFILE_END( "Frame/Physics/Integrate" );
}


PhysicsWorld::DiagnosticsView PhysicsWorld::GetDiagnosticsView() const
{
    return DiagnosticsView{ m_persistentContacts,
                            m_persistentContactSolverStats,
                            m_sleepIslandParent,
                            m_sleepSupportedThisFrame,
                            m_sleepInhibitedThisFrame,
                            m_sleepState,
                            m_sleepCounter,
                            m_sleepIslandEligible,
                            m_sleepIslandCanSleep,
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
    for ( const ObjectNarrowphaseIsland& island : m_objectNarrowphaseIslands )
    {
        bytes += VectorCapacityBytes( island.pairIndices );
    }
    bytes += VectorCapacityBytes( m_objectNarrowphaseParent );
    bytes += VectorCapacityBytes( m_objectNarrowphaseRank );
    bytes += VectorCapacityBytes( m_objectNarrowphaseRootToIsland );
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
