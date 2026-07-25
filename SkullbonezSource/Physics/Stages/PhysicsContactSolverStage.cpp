/*
File: SkullbonezSource/Physics/Stages/PhysicsContactSolverStage.cpp
Purpose:
  Implements the persistent-contact stage owner and replay state transfer.

Summary:
  The stage prepares bounded consequence queues, builds the internal solver
  context from explicit borrows, and executes the existing solver verbatim.
  Replay capture/restore stays with the state owner so PhysicsWorld does not
  regain mutable access to solver internals.

Glossary:
  Pipeline capacity: Remaining bounded diagnostics records for this fixed tick.
  Fixed-tree release: Solver event asking owner-side fixed support to wake.
  Replay transfer: Explicit copy between solver ownership and snapshot values.

Invariants:
  - Consequence queues are cleared but never re-reserved in Solve.
  - Capacity exhaustion is a Lane F fatal invariant violation.
  - Cache erasure preserves the original packed-key body matching expressions.

Related:
  - SkullbonezSource/Physics/Stages/PhysicsContactSolverStage.h
  - SkullbonezSource/Physics/PersistentContactSolver.cpp
  - SkullbonezSource/Physics/PhysicsWorld.cpp
*/
#include "PhysicsContactSolverStage.h"

#include "../../Core/FatalError.h"
#include "../../Core/SceneCapacity.h"
#include "../ColliderStore.h"
#include "../PhysicsBodyStore.h"
#include "../PhysicsWorldForces.h"

#include <algorithm>
#include <cassert>

using namespace SkullbonezCore::Physics;

namespace
{
constexpr int MAX_PIPELINE_TRACE_RECORDS = 4096;
constexpr int PHYSICS_CANDIDATE_PAIR_RESERVE = SkullbonezCore::Scene::Capacity::MAX_SCENE_OBJECTS * 4;
constexpr int PHYSICS_COLLISION_VISUAL_BODY_RESERVE = PHYSICS_CANDIDATE_PAIR_RESERVE * 2;

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

template <typename T> uint64_t VectorCapacityBytes( const std::vector<T>& values )
{
    return static_cast<uint64_t>( values.capacity() ) * static_cast<uint64_t>( sizeof( T ) );
}
} // namespace

PhysicsContactSolverStage::PhysicsContactSolverStage()
{
    m_persistentContacts.reserve( SkullbonezCore::Scene::Capacity::MAX_SCENE_OBJECTS * 4 );
    m_persistentContactCache.reserve( SkullbonezCore::Scene::Capacity::MAX_SCENE_OBJECTS * 4 );
    m_persistentContactCounts.reserve( SkullbonezCore::Scene::Capacity::MAX_SCENE_OBJECTS );
    m_persistentRestingContactCounts.reserve( SkullbonezCore::Scene::Capacity::MAX_SCENE_OBJECTS );
    m_solverBodies.reserve( SkullbonezCore::Scene::Capacity::MAX_SCENE_OBJECTS );
    m_sideEffects.pipelineRecords.reserve( MAX_PIPELINE_TRACE_RECORDS );
    m_sideEffects.collisionVisualBodies.reserve( PHYSICS_COLLISION_VISUAL_BODY_RESERVE );
    m_sideEffects.fixedContactBodies.reserve( SkullbonezCore::Scene::Capacity::MAX_SCENE_OBJECTS );
    m_sideEffects.releaseWakeBodies.reserve( 8 );
    m_sideEffects.fixedTreeReleases.reserve( 8 );
}

void PhysicsContactSolverStage::Clear()
{
    m_persistentContacts.clear();
    m_persistentContactCache.clear();
    m_persistentContactSolverStats = PersistentContactSolverStats();
    m_persistentContactCounts.clear();
    m_persistentRestingContactCounts.clear();
    m_solverBodies.clear();
    m_sideEffects.pipelineRecords.clear();
    m_sideEffects.collisionVisualBodies.clear();
    m_sideEffects.fixedContactBodies.clear();
    m_sideEffects.releaseWakeBodies.clear();
    m_sideEffects.fixedTreeReleases.clear();
}

void PhysicsContactSolverStage::PrepareSideEffects( int modelCount,
                                                    std::size_t candidatePairCount,
                                                    int pipelineRecordCapacity )
{
    m_sideEffects.pipelineRecords.clear();
    m_sideEffects.collisionVisualBodies.clear();
    m_sideEffects.fixedContactBodies.clear();
    m_sideEffects.releaseWakeBodies.clear();
    m_sideEffects.fixedTreeReleases.clear();

    // Invariant: preserving deterministic output requires every list to fit
    // its construction-time reserve; allocating or dropping a command is not
    // an acceptable runtime fallback.
    assert( m_sideEffects.collisionVisualBodies.capacity() >= candidatePairCount * 2 );
    assert( m_sideEffects.fixedContactBodies.capacity() >= static_cast<std::size_t>( modelCount ) );
    assert( m_sideEffects.releaseWakeBodies.capacity() >= 8 );
    assert( m_sideEffects.fixedTreeReleases.capacity() >= 8 );
    assert( m_sideEffects.pipelineRecords.capacity() >= static_cast<std::size_t>( pipelineRecordCapacity ) );
    if ( m_sideEffects.collisionVisualBodies.capacity() < candidatePairCount * 2 ||
         m_sideEffects.fixedContactBodies.capacity() < static_cast<std::size_t>( modelCount ) ||
         m_sideEffects.releaseWakeBodies.capacity() < 8 || m_sideEffects.fixedTreeReleases.capacity() < 8 ||
         m_sideEffects.pipelineRecords.capacity() < static_cast<std::size_t>( pipelineRecordCapacity ) )
    {
        SB_FATAL( "Physics/PhysicsContactSolverStage", "Persistent-contact consequence capacity exhausted." );
    }
}

void PhysicsContactSolverStage::Solve( const PhysicsContactSolverStageContext& context, float dt )
{
    PrepareSideEffects( context.bodyStoreCount, context.candidatePairs.size(), context.pipelineRecordCapacity );
    const bool elasticCollisions = context.worldForces.mutualGravity.enabled &&
                                   context.worldForces.mutualGravity.elasticCollisions;

    PersistentContactSolverContext solverContext { context.candidatePairs,
                                                   context.sleepState,
                                                   context.sleepSupportEdges,
                                                   m_persistentContacts,
                                                   m_persistentContactCache,
                                                   m_persistentContactSolverStats,
                                                   m_persistentContactCounts,
                                                   m_persistentRestingContactCounts,
                                                   m_solverBodies,
                                                   context.physicsDebugContacts,
                                                   context.terrainContactManifolds,
                                                   context.terrainRestApplied,
                                                   context.sleepSupportedThisFrame,
                                                   m_sideEffects,
                                                   context.bodyStore,
                                                   context.bodyRecords,
                                                   context.hotFields,
                                                   context.colliderRecords,
                                                   context.bodyStoreCount,
                                                   context.pipelineRecordCapacity,
                                                   elasticCollisions,
                                                   context.settings,
                                                   context.profiler };

    m_contactSolver.Solve( solverContext, dt );
}

void PhysicsContactCacheWakeAccess::ForgetBody( int bodyIndex ) const
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

    m_cache.erase( std::remove_if( m_cache.begin(),
                                   m_cache.end(),
                                   [bodyIndex, &cacheEntryReferencesBody]( const PersistentContactCacheEntry& entry )
                                   { return cacheEntryReferencesBody( entry, bodyIndex ); } ),
                   m_cache.end() );
}

PhysicsContactCacheWakeAccess PhysicsContactSolverStage::CreateWakeAccess()
{
    return PhysicsContactCacheWakeAccess( m_persistentContactCache );
}

void PhysicsContactSolverStage::CaptureReplayState( PhysicsSolverSnapshot& outSnapshot ) const
{
    outSnapshot.persistentContactCounts = m_persistentContactCounts;
    outSnapshot.persistentRestingContactCounts = m_persistentRestingContactCounts;
    for ( const PersistentContact& contact : m_persistentContacts )
    {
        PhysicsSolverPersistentContactSample sample;
#define CAPTURE_REPLAY_CONTACT_SAMPLE_FIELD( field ) sample.field = contact.field;
        SB_REPLAY_PERSISTENT_CONTACT_SAMPLE_FIELDS( CAPTURE_REPLAY_CONTACT_SAMPLE_FIELD )
#undef CAPTURE_REPLAY_CONTACT_SAMPLE_FIELD
        outSnapshot.persistentContacts.push_back( sample );
    }

    for ( const PersistentContactCacheEntry& cache : m_persistentContactCache )
    {
        PhysicsSolverContactCacheSample sample;
#define CAPTURE_REPLAY_CONTACT_CACHE_FIELD( field ) sample.field = cache.field;
        SB_REPLAY_CONTACT_CACHE_SAMPLE_FIELDS( CAPTURE_REPLAY_CONTACT_CACHE_FIELD )
#undef CAPTURE_REPLAY_CONTACT_CACHE_FIELD
        outSnapshot.persistentContactCache.push_back( sample );
    }

#define CAPTURE_REPLAY_SOLVER_STAT_FIELD( field ) outSnapshot.solverStats.field = m_persistentContactSolverStats.field;
    SB_REPLAY_SOLVER_STATS_FIELDS( CAPTURE_REPLAY_SOLVER_STAT_FIELD )
#undef CAPTURE_REPLAY_SOLVER_STAT_FIELD
}

void PhysicsContactSolverStage::RestoreReplayState( const PhysicsSolverSnapshot& snapshot )
{
    m_persistentContactCounts = snapshot.persistentContactCounts;
    m_persistentRestingContactCounts = snapshot.persistentRestingContactCounts;
    m_persistentContacts.clear();
    m_persistentContacts.reserve( snapshot.persistentContacts.size() );
    for ( const PhysicsSolverPersistentContactSample& sample : snapshot.persistentContacts )
    {
        PersistentContact contact;
#define RESTORE_REPLAY_CONTACT_SAMPLE_FIELD( field ) contact.field = sample.field;
        SB_REPLAY_PERSISTENT_CONTACT_SAMPLE_FIELDS( RESTORE_REPLAY_CONTACT_SAMPLE_FIELD )
#undef RESTORE_REPLAY_CONTACT_SAMPLE_FIELD
        m_persistentContacts.push_back( contact );
    }

    m_persistentContactCache.clear();
    m_persistentContactCache.reserve( snapshot.persistentContactCache.size() );
    for ( const PhysicsSolverContactCacheSample& sample : snapshot.persistentContactCache )
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
}

const std::vector<PersistentContact>& PhysicsContactSolverStage::GetPersistentContacts() const
{
    return m_persistentContacts;
}

const std::vector<PersistentContactCacheEntry>& PhysicsContactSolverStage::GetPersistentContactCache() const
{
    return m_persistentContactCache;
}

const PersistentContactSolverStats& PhysicsContactSolverStage::GetStats() const
{
    return m_persistentContactSolverStats;
}

const std::vector<uint16_t>& PhysicsContactSolverStage::GetPersistentContactCounts() const
{
    return m_persistentContactCounts;
}

const std::vector<uint16_t>& PhysicsContactSolverStage::GetPersistentRestingContactCounts() const
{
    return m_persistentRestingContactCounts;
}

const PersistentContactSolverSideEffects& PhysicsContactSolverStage::GetSideEffects() const
{
    return m_sideEffects;
}

uint64_t PhysicsContactSolverStage::CollectDynamicMemoryBytes() const
{
    uint64_t bytes = VectorCapacityBytes( m_persistentContacts );
    bytes += VectorCapacityBytes( m_persistentContactCache );
    bytes += VectorCapacityBytes( m_persistentContactCounts );
    bytes += VectorCapacityBytes( m_persistentRestingContactCounts );
    bytes += VectorCapacityBytes( m_solverBodies );
    bytes += VectorCapacityBytes( m_sideEffects.pipelineRecords );
    bytes += VectorCapacityBytes( m_sideEffects.collisionVisualBodies );
    bytes += VectorCapacityBytes( m_sideEffects.fixedContactBodies );
    bytes += VectorCapacityBytes( m_sideEffects.releaseWakeBodies );
    bytes += VectorCapacityBytes( m_sideEffects.fixedTreeReleases );
    return bytes;
}

#undef SB_REPLAY_PERSISTENT_CONTACT_SAMPLE_FIELDS
#undef SB_REPLAY_CONTACT_CACHE_SAMPLE_FIELDS
#undef SB_REPLAY_SOLVER_STATS_FIELDS
