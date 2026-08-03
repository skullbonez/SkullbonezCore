/*
File: SkullbonezSource/Physics/Stages/PhysicsContactSolverStage.cpp
Purpose:
  Implements the persistent-contact stage owner and replay state transfer.

Summary:
  The stage prepares bounded consequence queues and owns the persistent-row
  solve reached through explicit store, settings-policy, and step-value borrows.
  Replay capture/restore stays with the state owner so PhysicsWorld does not
  regain mutable access to solver internals.

Glossary:
  Pipeline capacity: Remaining bounded diagnostics events for this fixed tick;
    payload rows are optional.

Invariants:
  - Consequence queues are cleared but never re-reserved in Solve.
  - Pipeline consequences use one representation per step: ordered records or
    an equivalent count-only total.
  - Capacity exhaustion is a Lane F fatal invariant violation.
  - Cache erasure preserves the original packed-key body matching expressions.
  - Replay restore clears convergence diagnostics rather than presenting stale
    live-solve attribution as restored solver state.

Related:
  - SkullbonezSource/Physics/Stages/PhysicsContactSolverStage.h
  - SkullbonezSource/Physics/PersistentContactSolver.cpp
  - SkullbonezSource/Physics/PhysicsWorld.cpp
  - Agentic/Reference/engine-glossary.md
*/
#include "PhysicsContactSolverStage.h"

#include "../../Core/FatalError.h"
#include "../ColliderStore.h"
#include "../PhysicsBodyStore.h"
#include "../PhysicsWorldForces.h"

#include <algorithm>
#include <cassert>

using namespace SkullbonezCore::Physics;

namespace
{
#define SB_REPLAY_PERSISTENT_CONTACT_SAMPLE_FIELDS( VISIT )                                                                 \
    VISIT( bodyA )                                                                                                          \
    VISIT( bodyB )                                                                                                          \
    VISIT( featureId )                                                                                                      \
    VISIT( key )                                                                                                            \
    VISIT( normal )                                                                                                         \
    VISIT( tangent1 )                                                                                                       \
    VISIT( tangent2 )                                                                                                       \
    VISIT( rA )                                                                                                             \
    VISIT( rB )                                                                                                             \
    VISIT( penetration )                                                                                                    \
    VISIT( normalMass )                                                                                                     \
    VISIT( tangentMass1 )                                                                                                   \
    VISIT( tangentMass2 )                                                                                                   \
    VISIT( bias )                                                                                                           \
    VISIT( frictionLimit )                                                                                                  \
    VISIT( accN )                                                                                                           \
    VISIT( accT1 )                                                                                                          \
    VISIT( accT2 )                                                                                                          \
    VISIT( warmStarted )                                                                                                    \
    VISIT( isTerrain )                                                                                                      \
    VISIT( supportsRestingPolicy )                                                                                          \
    VISIT( allowsTangentFriction )                                                                                          \
    VISIT( normalCoupledFriction )                                                                                          \
    VISIT( inhibitsSleep )                                                                                                  \
    VISIT( manifoldPointCount )                                                                                             \
    VISIT( terrainNormal )                                                                                                  \
    VISIT( terrainWarmStart )

#define SB_REPLAY_CONTACT_CACHE_SAMPLE_FIELDS( VISIT )                                                                      \
    VISIT( key )                                                                                                            \
    VISIT( accN )                                                                                                           \
    VISIT( accT1 )                                                                                                          \
    VISIT( accT2 )

#define SB_REPLAY_SOLVER_STATS_FIELDS( VISIT )                                                                              \
    VISIT( rowCount )                                                                                                       \
    VISIT( cachePreviousRows )                                                                                              \
    VISIT( cacheHits )                                                                                                      \
    VISIT( cacheMisses )                                                                                                    \
    VISIT( warmStartedRows )                                                                                                \
    VISIT( positionCorrectionRows )                                                                                         \
    VISIT( solverIterations )                                                                                               \
    VISIT( positionCorrectionTotal )                                                                                        \
    VISIT( positionCorrectionMax )

template <typename T> uint64_t ListCapacityBytes( const T& values )
{
    return static_cast<uint64_t>( values.capacity() ) * static_cast<uint64_t>( sizeof( typename T::value_type ) );
}
} // namespace

PhysicsContactSolverStage::PhysicsContactSolverStage() = default;

void PersistentContactSolveTransaction::ReserveSceneCapacity( std::size_t bodyCapacity )
{
    m_bodies.Reserve( bodyCapacity );
}

void PersistentContactSolveTransaction::Clear()
{
    m_bodies.clear();
    m_phase = PersistentContactSolvePhaseCursor();
}

uint64_t PersistentContactSolveTransaction::CollectDynamicMemoryBytes() const
{
    return ListCapacityBytes( m_bodies );
}

void PersistentContactSolveTransaction::ResetBodies( std::size_t bodyCount )
{
    m_bodies.ResetDefault( bodyCount );
}

std::size_t PersistentContactSolveTransaction::BodyCount() const
{
    return m_bodies.size();
}

SolverBodyState& PersistentContactSolveTransaction::Body( std::size_t index )
{
    return m_bodies[index];
}

const SolverBodyState& PersistentContactSolveTransaction::Body( std::size_t index ) const
{
    return m_bodies[index];
}

void PhysicsContactSolverStage::ReserveSceneCapacity( std::size_t bodyCapacity )
{
    const std::size_t pairCapacity = PhysicsCandidatePairCapacity( bodyCapacity );
    const std::size_t contactCapacity = PhysicsContactRowCapacity( bodyCapacity );
    m_persistentContacts.Reserve( contactCapacity );
    m_persistentContactCache.Reserve( contactCapacity );
    m_persistentContactCounts.Reserve( bodyCapacity );
    m_persistentRestingContactCounts.Reserve( bodyCapacity );
    m_solveTransaction.ReserveSceneCapacity( bodyCapacity );
    m_sideEffects.pipelineRecords.Reserve( PHYSICS_MAX_PIPELINE_TRACE_RECORDS );
    m_sideEffects.collisionVisualBodies.Reserve( pairCapacity * 2u );
    m_sideEffects.fixedContactBodies.Reserve( contactCapacity );
    m_sideEffects.releaseWakeBodies.Reserve( bodyCapacity );
    m_sideEffects.fixedTreeReleases.Reserve( bodyCapacity );
}

void PhysicsContactSolverStage::Clear()
{
    m_persistentContacts.clear();
    m_persistentContactCache.clear();
    m_persistentContactSolverStats = PersistentContactSolverStats();
    m_persistentContactConvergenceTrace.Clear();
    m_persistentContactCounts.clear();
    m_persistentRestingContactCounts.clear();
    m_solveTransaction.Clear();
    m_sideEffects.pipelineRecords.clear();
    m_sideEffects.pipelineEventCount = 0;
    m_sideEffects.collisionVisualBodies.clear();
    m_sideEffects.fixedContactBodies.clear();
    m_sideEffects.releaseWakeBodies.clear();
    m_sideEffects.fixedTreeReleases.clear();
}

void PhysicsContactSolverStage::PrepareSideEffects( int modelCount, std::size_t candidatePairCount,
                                                    int pipelineRecordCapacity )
{
    m_sideEffects.pipelineRecords.clear();
    m_sideEffects.pipelineEventCount = 0;
    m_sideEffects.collisionVisualBodies.clear();
    m_sideEffects.fixedContactBodies.clear();
    m_sideEffects.releaseWakeBodies.clear();
    m_sideEffects.fixedTreeReleases.clear();

    // Invariant: preserving deterministic output requires every list to fit
    // its scene-load reservation; allocating or dropping a command is not
    // an acceptable runtime fallback.
    assert( m_sideEffects.collisionVisualBodies.capacity() >= candidatePairCount * 2 );
    assert( m_sideEffects.fixedContactBodies.capacity() >= static_cast<std::size_t>( modelCount ) );
    assert( m_sideEffects.releaseWakeBodies.capacity() >= static_cast<std::size_t>( modelCount ) );
    assert( m_sideEffects.fixedTreeReleases.capacity() >= static_cast<std::size_t>( modelCount ) );
    assert( m_sideEffects.pipelineRecords.capacity() >= static_cast<std::size_t>( pipelineRecordCapacity ) );

    if ( m_sideEffects.collisionVisualBodies.capacity() < candidatePairCount * 2 ||
         m_sideEffects.fixedContactBodies.capacity() < static_cast<std::size_t>( modelCount ) ||
         m_sideEffects.releaseWakeBodies.capacity() < static_cast<std::size_t>( modelCount ) ||
         m_sideEffects.fixedTreeReleases.capacity() < static_cast<std::size_t>( modelCount ) ||
         m_sideEffects.pipelineRecords.capacity() < static_cast<std::size_t>( pipelineRecordCapacity ) )
    {
        SB_FATAL( "Physics/PhysicsContactSolverStage", "Persistent-contact consequence capacity exhausted." );
    }
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

    m_cache.erase( std::remove_if( m_cache.begin(), m_cache.end(),
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
    outSnapshot.persistentContactCounts.clear();

    for ( uint16_t count : m_persistentContactCounts )
    {
        outSnapshot.persistentContactCounts.push_back( count );
    }

    outSnapshot.persistentRestingContactCounts.clear();

    for ( uint16_t count : m_persistentRestingContactCounts )
    {
        outSnapshot.persistentRestingContactCounts.push_back( count );
    }

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
    m_persistentContactCounts.Reserve( snapshot.persistentContactCounts.size() );
    m_persistentContactCounts.clear();

    for ( uint16_t count : snapshot.persistentContactCounts )
    {
        m_persistentContactCounts.push_back( count );
    }

    m_persistentRestingContactCounts.Reserve( snapshot.persistentRestingContactCounts.size() );
    m_persistentRestingContactCounts.clear();

    for ( uint16_t count : snapshot.persistentRestingContactCounts )
    {
        m_persistentRestingContactCounts.push_back( count );
    }

    m_persistentContacts.clear();
    m_persistentContacts.Reserve( snapshot.persistentContacts.size() );

    for ( const PhysicsSolverPersistentContactSample& sample : snapshot.persistentContacts )
    {
        PersistentContact contact;
#define RESTORE_REPLAY_CONTACT_SAMPLE_FIELD( field ) contact.field = sample.field;
        SB_REPLAY_PERSISTENT_CONTACT_SAMPLE_FIELDS( RESTORE_REPLAY_CONTACT_SAMPLE_FIELD )
#undef RESTORE_REPLAY_CONTACT_SAMPLE_FIELD
        m_persistentContacts.push_back( contact );
    }

    m_persistentContactCache.clear();
    m_persistentContactCache.Reserve( snapshot.persistentContactCache.size() );

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
    m_persistentContactConvergenceTrace.Clear();
    m_solveTransaction.Clear();
}

std::span<const PersistentContact> PhysicsContactSolverStage::GetPersistentContacts() const
{
    return m_persistentContacts;
}

std::span<const PersistentContactCacheEntry> PhysicsContactSolverStage::GetPersistentContactCache() const
{
    return m_persistentContactCache;
}

const PersistentContactSolverStats& PhysicsContactSolverStage::GetStats() const
{
    return m_persistentContactSolverStats;
}

const PersistentContactConvergenceTrace& PhysicsContactSolverStage::GetConvergenceTrace() const
{
    return m_persistentContactConvergenceTrace;
}

std::span<const uint16_t> PhysicsContactSolverStage::GetPersistentContactCounts() const
{
    return m_persistentContactCounts;
}

std::span<const uint16_t> PhysicsContactSolverStage::GetPersistentRestingContactCounts() const
{
    return m_persistentRestingContactCounts;
}

const PersistentContactSolverSideEffects& PhysicsContactSolverStage::GetSideEffects() const
{
    return m_sideEffects;
}

uint64_t PhysicsContactSolverStage::CollectDynamicMemoryBytes() const
{
    uint64_t bytes = ListCapacityBytes( m_persistentContacts );
    bytes += ListCapacityBytes( m_persistentContactCache );
    bytes += ListCapacityBytes( m_persistentContactCounts );
    bytes += ListCapacityBytes( m_persistentRestingContactCounts );
    bytes += m_solveTransaction.CollectDynamicMemoryBytes();
    bytes += ListCapacityBytes( m_sideEffects.pipelineRecords );
    bytes += ListCapacityBytes( m_sideEffects.collisionVisualBodies );
    bytes += ListCapacityBytes( m_sideEffects.fixedContactBodies );
    bytes += ListCapacityBytes( m_sideEffects.releaseWakeBodies );
    bytes += ListCapacityBytes( m_sideEffects.fixedTreeReleases );
    return bytes;
}

#undef SB_REPLAY_PERSISTENT_CONTACT_SAMPLE_FIELDS
#undef SB_REPLAY_CONTACT_CACHE_SAMPLE_FIELDS
#undef SB_REPLAY_SOLVER_STATS_FIELDS
