/*
File: SkullbonezSource/Physics/Stages/PhysicsSleepController.Wake.cpp
Purpose:
  Implements synchronous sleep wake, seeding, and underwater-lock policy.

Summary:
  Wake propagation is a cohesive part of PhysicsSleepController but has enough
  bounded graph traversal and policy to merit a focused implementation unit.
  All rows remain owned by the same concrete sleep controller; parallel wake
  producers publish bounded indices for sequencer-side ordered insertion.

Glossary:
  Seed sleep: Explicitly establish an eligible body as dormant before island analysis.

Invariants:
  - Fixed and underwater-locked bodies reject ordinary wake fan-out.
  - Synchronous narrowphase wake reapplies forces within the same fixed step.
  - Wake scratch uses construction-reserved storage and never grows in steady play.
  - Only the successful sleeping-to-awake compare/exchange owns wake side
    effects, so parallel pairs cannot enqueue or apply forces twice.

Related:
  - SkullbonezSource/Physics/Stages/PhysicsSleepController.h
  - SkullbonezSource/Physics/Stages/PhysicsSleepController.cpp
  - Agentic/Reference/engine-glossary.md
*/
#include "PhysicsSleepController.h"

#include "PhysicsContactSolverStage.h"
#include "../../Core/FatalError.h"
#include "../BuoyancySystem.h"
#include "../ColliderStore.h"
#include "../DisjointSet.h"
#include "../PhysicsBodyStore.h"
#include "../PhysicsWorldForces.h"

#include <algorithm>
#include <cassert>

using namespace SkullbonezCore::Physics;
using SkullbonezCore::Math::Vector::Vector3;
namespace Vector = SkullbonezCore::Math::Vector;

namespace
{
constexpr float EXPLICIT_WAKE_NEIGHBOR_SLOP = 0.50f;
constexpr float EXPLICIT_WAKE_VERTICAL_SLOP = 0.25f;

bool IsSolverBodyFixed( const PhysicsBodyHotFieldsConstView& hotFields, int bodyIndex )
{
    return hotFields.fixed[static_cast<std::size_t>( bodyIndex )] != 0u;
}
} // namespace

bool PhysicsSleepController::IsUnderwaterSleepLocked( int bodyCount, int index )
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

void PhysicsSleepController::LockUnderwaterSleeperIfReady( const PhysicsWorldForces& worldForces,
                                                           PhysicsBodyStore& bodyStore, const ColliderStore& colliderStore,
                                                           std::span<BuoyancyBodyFacts> buoyancyFacts,
                                                           std::span<float> timeRemaining, int index )
{
    const int bodyCount = bodyStore.Count();
    EnsureUnderwaterSleepLockBuffer( bodyCount );

    if ( index < 0 || index >= bodyCount || index >= static_cast<int>( m_sleepState.size() ) || !m_sleepState[index] ||
         index >= static_cast<int>( buoyancyFacts.size() ) || m_underwaterSleepLocked[index] )
    {
        return;
    }

    BuoyancyBodyFacts& facts = buoyancyFacts[static_cast<std::size_t>( index )];

    if ( !BuoyancySystem::RefreshUnderwaterSubmersionForBall( worldForces, bodyStore, colliderStore, facts, index ) )
    {
        return;
    }

    if ( !BuoyancySystem::IsFullySubmergedBall( facts, bodyStore.HotFields().fixed[static_cast<std::size_t>( index )] != 0u,
                                                colliderStore, index ) )
    {
        return;
    }

    m_underwaterSleepLocked[index] = 1;

    if ( index < static_cast<int>( timeRemaining.size() ) )
    {
        timeRemaining[index] = 0.0f;
    }

    PhysicsBodyHotFieldsView hotFields = bodyStore.MutableHotFields();
    const std::size_t bodyIndex = static_cast<std::size_t>( index );
    hotFields.linearVelocityX[bodyIndex] = 0.0f;
    hotFields.linearVelocityY[bodyIndex] = 0.0f;
    hotFields.linearVelocityZ[bodyIndex] = 0.0f;
    hotFields.angularVelocityX[bodyIndex] = 0.0f;
    hotFields.angularVelocityY[bodyIndex] = 0.0f;
    hotFields.angularVelocityZ[bodyIndex] = 0.0f;
    hotFields.awake[bodyIndex] = 0u;
}

bool PhysicsSleepController::WakeDynamicBodyState( PhysicsBodyStore& bodyStore, PhysicsContactCacheWakeAccess contactCache,
                                                   int index )
{
    // Concept: explicit zero-dt wake clears owned sleep/cache rows but leaves
    // the fixed-step CCD clock and force accumulation untouched.
    const int bodyCount = bodyStore.Count();
    const std::span<const PhysicsBodyRecord> bodyRecords = bodyStore.Records();
    const PhysicsBodyHotFieldsView hotFields = bodyStore.MutableHotFields();

    if ( index < 0 || index >= bodyCount || index >= static_cast<int>( bodyRecords.size() ) ||
         index >= static_cast<int>( m_sleepState.size() ) ||
         IsSolverBodyFixed( ConstPhysicsBodyHotFields( hotFields ), index ) )
    {
        return false;
    }

    const bool wasSleeping = m_sleepState[index] != 0;
    const bool hadCounter = index < static_cast<int>( m_sleepCounter.size() ) && m_sleepCounter[index] != 0;
    const bool hadSleepVisual = index < static_cast<int>( m_sleepIslandVisualId.size() ) &&
                                m_sleepIslandVisualId[index] != 0;

    const bool wasUnderwaterLocked = index < static_cast<int>( m_underwaterSleepLocked.size() ) &&
                                     m_underwaterSleepLocked[index] != 0;

    m_sleepState[index] = 0;
    hotFields.awake[static_cast<std::size_t>( index )] = 1u;

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

    contactCache.ForgetBody( index );

    if ( wasSleeping )
    {
        AddAwakeBodyIndex( index );
    }

    return wasSleeping || hadCounter || hadSleepVisual || wasUnderwaterLocked;
}

bool PhysicsSleepController::WakeDynamicBodyStateWithForces( PhysicsBodyStore& bodyStore, const ColliderStore& colliderStore, PhysicsTerrainView terrain,
                                                             const PhysicsWorldForces& worldForces, std::span<BuoyancyBodyFacts> buoyancyFacts, std::span<float> timeRemaining,
                                                             PhysicsContactCacheWakeAccess contactCache, int index, float dt )
{
    // Invariant: same-step wake preserves state -> clock -> force -> cache ->
    // sorted-awake publication order. Later collision stages observe every
    // mutation in that sequence.
    const int bodyCount = bodyStore.Count();
    const std::span<const PhysicsBodyRecord> bodyRecords = bodyStore.Records();
    const PhysicsBodyHotFieldsView hotFields = bodyStore.MutableHotFields();

    if ( index < 0 || index >= bodyCount || index >= static_cast<int>( bodyRecords.size() ) ||
         index >= static_cast<int>( m_sleepState.size() ) ||
         IsSolverBodyFixed( ConstPhysicsBodyHotFields( hotFields ), index ) )
    {
        return false;
    }

    const bool wasSleeping = m_sleepState[index] != 0;
    const bool hadCounter = index < static_cast<int>( m_sleepCounter.size() ) && m_sleepCounter[index] != 0;
    const bool hadSleepVisual = index < static_cast<int>( m_sleepIslandVisualId.size() ) &&
                                m_sleepIslandVisualId[index] != 0;

    const bool wasUnderwaterLocked = index < static_cast<int>( m_underwaterSleepLocked.size() ) &&
                                     m_underwaterSleepLocked[index] != 0;

    m_sleepState[index] = 0;
    hotFields.awake[static_cast<std::size_t>( index )] = 1u;

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

    if ( dt > 0.0f && index < static_cast<int>( timeRemaining.size() ) )
    {
        timeRemaining[index] = dt;
    }

    if ( wasSleeping && dt > TOLERANCE && index < static_cast<int>( buoyancyFacts.size() ) )
    {
        (void)bodyStore.ApplyForces( worldForces, colliderStore, terrain, buoyancyFacts[static_cast<std::size_t>( index )],
                                     index, dt );
    }

    contactCache.ForgetBody( index );

    if ( wasSleeping )
    {
        AddAwakeBodyIndex( index );
    }

    return wasSleeping || hadCounter || hadSleepVisual || wasUnderwaterLocked;
}

void PhysicsSleepController::WakeSleepVisualIsland( PhysicsBodyStore& bodyStore, PhysicsContactCacheWakeAccess contactCache,
                                                    int index )
{
    if ( index < 0 || index >= static_cast<int>( m_sleepState.size() ) )
    {
        return;
    }

    const int visualId = index < static_cast<int>( m_sleepIslandVisualId.size() ) ? m_sleepIslandVisualId[index] : 0;

    if ( visualId > 0 )
    {
        const int count = (std::min)( { static_cast<int>( m_sleepIslandVisualId.size() ), bodyStore.Count(),
                                        static_cast<int>( bodyStore.Records().size() ) } );

        for ( int i = 0; i < count; ++i )
        {
            if ( m_sleepIslandVisualId[i] == visualId )
            {
                WakeDynamicBodyState( bodyStore, contactCache, i );
            }
        }
    }
    else
    {
        WakeDynamicBodyState( bodyStore, contactCache, index );
    }
}

void PhysicsSleepController::WakePointJointIsland( PhysicsBodyStore& bodyStore, PhysicsContactCacheWakeAccess contactCache,
                                                   std::span<const PointJointConstraint> pointJointConstraints, int index )
{
    const int modelCount = (std::min)( bodyStore.Count(), static_cast<int>( bodyStore.Records().size() ) );

    if ( pointJointConstraints.empty() || index < 0 || index >= modelCount ||
         index >= static_cast<int>( m_sleepState.size() ) )
    {
        return;
    }

    m_sleepIslandParent.assign( modelCount, 0 );
    m_sleepIslandRank.assign( modelCount, 0 );
    EnsureScratchFlagsSize( modelCount );

    for ( PhysicsSleepScratchFlags& flags : m_sleepScratchFlags )
    {
        flags.pointJointBody = 0u;
    }

    for ( int i = 0; i < modelCount; ++i )
    {
        m_sleepIslandParent[i] = i;
    }

    DisjointSet sleepIslands( m_sleepIslandParent, m_sleepIslandRank, modelCount );

    for ( const PointJointConstraint& constraint : pointJointConstraints )
    {
        const int a = constraint.BodyAIndex( bodyStore );
        const int b = constraint.BodyBIndex( bodyStore );

        if ( a < 0 || b < 0 || a == b || a >= modelCount || b >= modelCount )
        {
            continue;
        }

        m_sleepScratchFlags[a].pointJointBody = 1u;
        m_sleepScratchFlags[b].pointJointBody = 1u;
        sleepIslands.Unite( a, b );
    }

    if ( m_sleepScratchFlags[index].pointJointBody == 0u )
    {
        return;
    }

    const int root = sleepIslands.Find( index );

    for ( int i = 0; i < modelCount; ++i )
    {
        if ( m_sleepScratchFlags[i].pointJointBody != 0u && sleepIslands.Find( i ) == root )
        {
            WakeDynamicBodyState( bodyStore, contactCache, i );
        }
    }
}

void PhysicsSleepController::WakeRestingContactIsland( PhysicsBodyStore& bodyStore,
                                                       PhysicsContactCacheWakeAccess contactCache,
                                                       std::span<const PersistentContact> persistentContacts, int index )
{
    // Hazard: sleeping contacts are pruned, so explicit wake expands through
    // both retained contact edges and the established bounded proximity test.
    const int modelCount = (std::min)( bodyStore.Count(), static_cast<int>( bodyStore.Records().size() ) );
    const PhysicsBodyHotFieldsView hotFields = bodyStore.MutableHotFields();

    if ( index < 0 || index >= modelCount || index >= static_cast<int>( m_sleepState.size() ) )
    {
        return;
    }

    if ( modelCount > static_cast<int>( m_sleepScratchFlags.capacity() ) ||
         modelCount > static_cast<int>( m_restingWakeQueueScratch.capacity() ) )
    {
        assert( false && "Physics resting-wake scratch capacity exceeded" );
        SB_FATAL( "Physics/PhysicsSleepController", "Physics resting-wake scratch capacity exceeded" );
    }

    EnsureScratchFlagsSize( modelCount );

    for ( PhysicsSleepScratchFlags& flags : m_sleepScratchFlags )
    {
        flags.restingWakeVisited = 0u;
    }

    m_restingWakeQueueScratch.clear();
    m_sleepScratchFlags[static_cast<std::size_t>( index )].restingWakeVisited = 1u;
    m_restingWakeQueueScratch.push_back( index );
    const auto hasPersistentContactEdge = [&]( int a, int b )
    {
        for ( const PersistentContact& contact : persistentContacts )
        {
            if ( ( contact.bodyA == a && contact.bodyB == b ) || ( contact.bodyA == b && contact.bodyB == a ) )
            {
                return true;
            }
        }

        return false;
    };
    const auto isLikelyRestingNeighbor = [&]( int a, int b )
    {
        const PhysicsBodyHotFieldsConstView hotRead = ConstPhysicsBodyHotFields( hotFields );

        const std::size_t bodyAIndex = static_cast<std::size_t>( a );
        const std::size_t bodyBIndex = static_cast<std::size_t>( b );
        const float radiusA = (std::max)( 0.01f, hotFields.boundingRadius[bodyAIndex] );
        const float radiusB = (std::max)( 0.01f, hotFields.boundingRadius[bodyBIndex] );
        const Vector3 positionA = PhysicsBodyPosition( hotRead, bodyAIndex );
        const Vector3 positionB = PhysicsBodyPosition( hotRead, bodyBIndex );

        if ( positionB.y + radiusB + EXPLICIT_WAKE_VERTICAL_SLOP < positionA.y - radiusA )
        {
            return false;
        }

        const float range = radiusA + radiusB + EXPLICIT_WAKE_NEIGHBOR_SLOP;
        const Vector3 delta = positionB - positionA;
        return Dot( delta, delta ) <= range * range;
    };

    for ( std::size_t cursor = 0; cursor < m_restingWakeQueueScratch.size(); ++cursor )
    {
        const int current = m_restingWakeQueueScratch[cursor];

        for ( int candidate = 0; candidate < modelCount; ++candidate )
        {
            if ( m_sleepScratchFlags[static_cast<std::size_t>( candidate )].restingWakeVisited != 0u ||
                 candidate >= static_cast<int>( m_sleepState.size() ) || m_sleepState[candidate] == 0 ||
                 IsSolverBodyFixed( ConstPhysicsBodyHotFields( hotFields ), candidate ) ||
                 IsUnderwaterSleepLocked( modelCount, candidate ) ||
                 ( !hasPersistentContactEdge( current, candidate ) && !isLikelyRestingNeighbor( current, candidate ) ) )
            {
                continue;
            }

            m_sleepScratchFlags[static_cast<std::size_t>( candidate )].restingWakeVisited = 1u;
            m_restingWakeQueueScratch.push_back( candidate );
            WakeDynamicBodyState( bodyStore, contactCache, candidate );
        }
    }
}

bool PhysicsSleepController::PrepareExplicitWake( PhysicsBodyStore& bodyStore, int index )
{
    // Why: both explicit-wake entrypoints share the one cold row-resize path;
    // ordinary fixed steps arrive with owner storage already sized.
    const int modelCount = (std::min)( bodyStore.Count(), static_cast<int>( bodyStore.Records().size() ) );

    if ( index >= 0 && index < modelCount )
    {
        if ( IsSolverBodyFixed( bodyStore.HotFields(), index ) )
        {
            return false;
        }
    }
    else if ( index >= 0 )
    {
        return false;
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
    return index >= 0 && index < static_cast<int>( m_sleepState.size() );
}

void PhysicsSleepController::WakeModel( PhysicsBodyStore& bodyStore, PhysicsContactCacheWakeAccess contactCache,
                                        std::span<const PersistentContact> persistentContacts,
                                        std::span<const PointJointConstraint> pointJointConstraints, int index )
{
    if ( PrepareExplicitWake( bodyStore, index ) )
    {
        if ( IsUnderwaterSleepLocked( static_cast<int>( m_sleepState.size() ), index ) )
        {
            return;
        }

        WakeSleepVisualIsland( bodyStore, contactCache, index );
        WakePointJointIsland( bodyStore, contactCache, pointJointConstraints, index );
        WakeRestingContactIsland( bodyStore, contactCache, persistentContacts, index );
    }
}

void PhysicsSleepController::WakeModel( PhysicsBodyStore& bodyStore, const ColliderStore& colliderStore,
                                        const PhysicsWorldForces& worldForces, std::span<BuoyancyBodyFacts> buoyancyFacts,
                                        std::span<float> timeRemaining, PhysicsContactCacheWakeAccess contactCache,
                                        std::span<const PersistentContact> persistentContacts,
                                        std::span<const PointJointConstraint> pointJointConstraints, int index )
{
    if ( PrepareExplicitWake( bodyStore, index ) )
    {
        if ( !m_underwaterSleepLocked[index] && m_sleepState[index] && index < static_cast<int>( buoyancyFacts.size() ) )
        {
            BuoyancyBodyFacts& facts = buoyancyFacts[static_cast<std::size_t>( index )];
            const bool refreshedSubmersion = BuoyancySystem::RefreshUnderwaterSubmersionForBall( worldForces, bodyStore,
                                                                                                 colliderStore, facts,
                                                                                                 index );

            if ( ( refreshedSubmersion || facts.submergedVolumePercent > 0.0f ) &&
                 BuoyancySystem::IsFullySubmergedBall( facts,
                                                       bodyStore.HotFields().fixed[static_cast<std::size_t>( index )] != 0u,
                                                       colliderStore, index ) )
            {
                m_underwaterSleepLocked[index] = 1;

                if ( index < static_cast<int>( timeRemaining.size() ) )
                {
                    timeRemaining[index] = 0.0f;
                }

                return;
            }
        }

        if ( IsUnderwaterSleepLocked( static_cast<int>( m_sleepState.size() ), index ) )
        {
            return;
        }

        WakeSleepVisualIsland( bodyStore, contactCache, index );
        WakePointJointIsland( bodyStore, contactCache, pointJointConstraints, index );
        WakeRestingContactIsland( bodyStore, contactCache, persistentContacts, index );
    }
}

PhysicsNarrowphaseWakeAccess::PhysicsNarrowphaseWakeAccess( PhysicsSleepController& sleepController, PhysicsBodyStore& bodyStore, const ColliderStore& colliderStore,
                                                            PhysicsTerrainView terrain, const PhysicsWorldForces& worldForces, std::span<BuoyancyBodyFacts> buoyancyFacts,
                                                            std::span<PhysicsBodyRecord> bodyRecords, const PhysicsBodyHotFieldsView& hotFields, std::span<float> timeRemaining,
                                                            int modelCount, float dt )
    : m_sleepController( sleepController ), m_bodyStore( bodyStore ), m_colliderStore( colliderStore ), m_terrain( terrain ),
      m_worldForces( worldForces ), m_buoyancyFacts( buoyancyFacts ), m_bodyRecords( bodyRecords ), m_hotFields( hotFields ),
      m_timeRemaining( timeRemaining ), m_modelCount( modelCount ), m_dt( dt )
{
}

int PhysicsNarrowphaseWakeAccess::SleepRowCount() const
{
    return (std::min)( m_modelCount, static_cast<int>( m_sleepController.GetSleepStates().size() ) );
}

bool PhysicsNarrowphaseWakeAccess::IsSleeping( int bodyIndex ) const
{
    const std::span<const uint8_t> sleepState = m_sleepController.GetSleepStates();
    return bodyIndex >= 0 && bodyIndex < m_modelCount && bodyIndex < static_cast<int>( sleepState.size() ) &&
           sleepState[static_cast<std::size_t>( bodyIndex )] != 0u;
}

bool PhysicsNarrowphaseWakeAccess::IsUnderwaterSleepLocked( int bodyIndex ) const
{
    const std::span<const uint8_t> underwaterLocks = m_sleepController.GetUnderwaterSleepLocks();
    return bodyIndex >= 0 && bodyIndex < m_modelCount && bodyIndex < static_cast<int>( underwaterLocks.size() ) &&
           underwaterLocks[static_cast<std::size_t>( bodyIndex )] != 0u;
}

void PhysicsNarrowphaseWakeAccess::WakeBody( int sleepingIndex ) const
{
    // Why: narrowphase and external-force wakeups must re-enter the body into this
    // tick synchronously; deferring this mutation changes later pair reads.
    if ( sleepingIndex < 0 || sleepingIndex >= m_modelCount ||
         IsSolverBodyFixed( ConstPhysicsBodyHotFields( m_hotFields ), sleepingIndex ) ||
         ( sleepingIndex < static_cast<int>( m_sleepController.m_underwaterSleepLocked.size() ) &&
           m_sleepController.m_underwaterSleepLocked[sleepingIndex] ) )
    {
        return;
    }

    // Parallel pairs can target the same sleeper. One atomic state transition
    // owns the wake side effects and one bounded queue row; the sequencer later
    // folds queued indices into deterministic ascending order.
    std::atomic_ref<uint8_t> sleepState( m_sleepController.m_sleepState[static_cast<std::size_t>( sleepingIndex )] );
    uint8_t expectedSleeping = 1u;

    if ( !sleepState.compare_exchange_strong( expectedSleeping, 0u, std::memory_order_acq_rel ) )
    {
        return;
    }

    std::atomic_ref<int> pendingAwakeCount( m_sleepController.m_pendingAwakeCount );
    const int pendingIndex = pendingAwakeCount.fetch_add( 1, std::memory_order_acq_rel );

    if ( pendingIndex < 0 || pendingIndex >= Scene::Capacity::MAX_SCENE_OBJECTS )
    {
        SB_FATAL( "Physics/PhysicsSleepController",
                  "Pending awake queue capacity exceeded: slot=%d capacity=%zu phase=steady_gameplay.", pendingIndex,
                  Scene::Capacity::MAX_SCENE_OBJECTS );
    }

    m_sleepController.m_pendingAwakeIndices[static_cast<std::size_t>( pendingIndex )] = sleepingIndex;
    m_sleepController.m_sleepCounter[sleepingIndex] = 0;
    m_sleepController.m_sleepIslandVisualId[sleepingIndex] = 0;
    m_timeRemaining[sleepingIndex] = m_dt;
    m_hotFields.awake[static_cast<std::size_t>( sleepingIndex )] = 1u;
    (void)m_bodyStore.ApplyForces( m_worldForces, m_colliderStore, m_terrain,
                                   m_buoyancyFacts[static_cast<std::size_t>( sleepingIndex )], sleepingIndex, m_dt );
}

PhysicsNarrowphaseWakeAccess PhysicsSleepController::CreateNarrowphaseWakeAccess( PhysicsBodyStore& bodyStore, const ColliderStore& colliderStore, PhysicsTerrainView terrain,
                                                                                  const PhysicsWorldForces& worldForces, std::span<BuoyancyBodyFacts> buoyancyFacts,
                                                                                  std::span<PhysicsBodyRecord> bodyRecords, std::span<float> timeRemaining, int modelCount, float dt )
{
    return PhysicsNarrowphaseWakeAccess( *this, bodyStore, colliderStore, terrain, worldForces, buoyancyFacts, bodyRecords,
                                         bodyStore.MutableHotFields(), timeRemaining, modelCount, dt );
}

void PhysicsSleepController::SeedModelAsleep( const PhysicsBodyStore& bodyStore, int index )
{
    if ( !m_sleepEnabled )
    {
        return;
    }

    const PhysicsBodyHotFieldsConstView hotFields = bodyStore.HotFields();
    const int modelCount = (std::min)( bodyStore.Count(), static_cast<int>( hotFields.fixed.size() ) );

    if ( index < 0 || index >= modelCount || IsSolverBodyFixed( hotFields, index ) )
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

    EnsureVisualIdSize( modelCount );
    EnsureUnderwaterSleepLockBuffer( modelCount );
    m_sleepState[index] = 1;
    RemoveAwakeBodyIndex( index );
    m_sleepCounter[index] = m_seedSleepFrameCount;
    m_underwaterSleepLocked[index] = 0;
    m_sleepIslandVisualId[index] = m_nextSleepIslandVisualId++;

    if ( m_nextSleepIslandVisualId <= 0 )
    {
        m_nextSleepIslandVisualId = 1;
    }
}

void PhysicsSleepController::SetPhysicsSleepEnabled( bool enabled )
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
    m_awakeListNeedsRebuild = true;
}

bool PhysicsSleepController::IsPhysicsSleepEnabled() const
{
    return m_sleepEnabled;
}
