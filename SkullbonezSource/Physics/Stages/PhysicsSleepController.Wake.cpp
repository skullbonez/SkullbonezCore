/*
File: SkullbonezSource/Physics/Stages/PhysicsSleepController.Wake.cpp
Purpose:
  Implements synchronous sleep wake, seeding, and underwater-lock policy.

Summary:
  Wake propagation is a cohesive part of PhysicsSleepController but has enough
  bounded graph traversal and policy to merit a focused implementation unit.
  All rows remain owned by the same concrete sleep controller.

Glossary:
  Wake fan-out: Expansion through visual, point-joint, and resting-contact islands.
  Underwater lock: Dormancy guard that prevents buoyancy jitter from waking a ball.
  Seed sleep: Explicitly establish an eligible body as dormant before island analysis.

Invariants:
  - Fixed and underwater-locked bodies reject ordinary wake fan-out.
  - Synchronous narrowphase wake reapplies forces within the same fixed step.
  - Wake scratch uses construction-reserved storage and never grows in steady play.

Related:
  - SkullbonezSource/Physics/Stages/PhysicsSleepController.h
  - SkullbonezSource/Physics/Stages/PhysicsSleepController.cpp
  - Agentic/Reports/2026-07-15/physicsworld-ownership-map.md
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

bool IsSolverBodyFixed( PhysicsBodyHotFieldsConstView hotFields, int bodyIndex )
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
                                                           PhysicsBodyStore& bodyStore,
                                                           const ColliderStore& colliderStore,
                                                           std::span<float> timeRemaining,
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
    if ( !record ||
         !BuoyancySystem::IsFullySubmergedBall( *record,
                                                bodyStore.HotFields().fixed[static_cast<std::size_t>( index )] != 0u,
                                                colliderStore,
                                                index ) )
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

bool PhysicsSleepController::WakeDynamicBodyState( const PhysicsSleepWakeContext& context,
                                                   int index,
                                                   float dt,
                                                   bool applyForces )
{
    // Invariant: every wake clears the model-order sleep rows and the contact
    // cache before the body can participate in later fixed-step stages.
    if ( index < 0 || index >= context.bodyCount || index >= static_cast<int>( context.bodyRecords.size() ) ||
         index >= static_cast<int>( m_sleepState.size() ) ||
         IsSolverBodyFixed( ConstPhysicsBodyHotFields( context.hotFields ), index ) )
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
    context.hotFields.awake[static_cast<std::size_t>( index )] = 1u;
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
    if ( dt > 0.0f && index < static_cast<int>( context.timeRemaining.size() ) )
    {
        context.timeRemaining[index] = dt;
    }
    if ( applyForces && wasSleeping && dt > TOLERANCE && context.bodyStore && context.worldForces &&
         context.colliderStore )
    {
        (void)context.bodyStore->ApplyForces( *context.worldForces, *context.colliderStore, index, dt );
    }
    context.contactCache.ForgetBody( index );
    return wasSleeping || hadCounter || hadSleepVisual || wasUnderwaterLocked;
}

void PhysicsSleepController::WakeSleepVisualIsland( const PhysicsSleepWakeContext& context,
                                                    int index,
                                                    float dt,
                                                    bool applyForces )
{
    if ( index < 0 || index >= static_cast<int>( m_sleepState.size() ) )
    {
        return;
    }
    const int visualId = index < static_cast<int>( m_sleepIslandVisualId.size() ) ? m_sleepIslandVisualId[index] : 0;
    if ( visualId > 0 )
    {
        const int count = (std::min)( { static_cast<int>( m_sleepIslandVisualId.size() ),
                                        context.bodyCount,
                                        static_cast<int>( context.bodyRecords.size() ) } );
        for ( int i = 0; i < count; ++i )
        {
            if ( m_sleepIslandVisualId[i] == visualId )
            {
                WakeDynamicBodyState( context, i, dt, applyForces );
            }
        }
    }
    else
    {
        WakeDynamicBodyState( context, index, dt, applyForces );
    }
}

void PhysicsSleepController::WakePointJointIsland( const PhysicsSleepWakeContext& context,
                                                   int index,
                                                   float dt,
                                                   bool applyForces )
{
    if ( context.bodyStore == nullptr )
    {
        return;
    }
    const int modelCount =
        (std::min)( { context.bodyCount, context.bodyStore->Count(), static_cast<int>( context.bodyRecords.size() ) } );
    if ( context.pointJointConstraints.empty() || index < 0 || index >= modelCount ||
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
    for ( const PointJointConstraint& constraint : context.pointJointConstraints )
    {
        const int a = constraint.BodyAIndex( *context.bodyStore );
        const int b = constraint.BodyBIndex( *context.bodyStore );
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
        if ( m_sleepPointJointBody[i] != 0 && sleepIslands.Find( i ) == root )
        {
            WakeDynamicBodyState( context, i, dt, applyForces );
        }
    }
}

void PhysicsSleepController::WakeRestingContactIsland( const PhysicsSleepWakeContext& context,
                                                       int index,
                                                       float dt,
                                                       bool applyForces )
{
    // Hazard: sleeping contacts are pruned, so explicit wake expands through
    // both retained contact edges and the established bounded proximity test.
    const int modelCount = (std::min)( context.bodyCount, static_cast<int>( context.bodyRecords.size() ) );
    if ( index < 0 || index >= modelCount || index >= static_cast<int>( m_sleepState.size() ) )
    {
        return;
    }
    if ( modelCount > static_cast<int>( m_restingWakeVisitedScratch.capacity() ) ||
         modelCount > static_cast<int>( m_restingWakeQueueScratch.capacity() ) )
    {
        assert( false && "Physics resting-wake scratch capacity exceeded" );
        SB_FATAL( "Physics/PhysicsSleepController", "Physics resting-wake scratch capacity exceeded" );
    }
    m_restingWakeVisitedScratch.assign( static_cast<std::size_t>( modelCount ), 0 );
    m_restingWakeQueueScratch.clear();
    m_restingWakeVisitedScratch[static_cast<std::size_t>( index )] = 1;
    m_restingWakeQueueScratch.push_back( index );
    const auto hasPersistentContactEdge = [&]( int a, int b )
    {
        for ( const PersistentContact& contact : context.persistentContacts )
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
        const PhysicsBodyHotFieldsConstView hotRead = ConstPhysicsBodyHotFields( context.hotFields );
        const std::size_t bodyAIndex = static_cast<std::size_t>( a );
        const std::size_t bodyBIndex = static_cast<std::size_t>( b );
        const float radiusA = (std::max)( 0.01f, context.hotFields.boundingRadius[bodyAIndex] );
        const float radiusB = (std::max)( 0.01f, context.hotFields.boundingRadius[bodyBIndex] );
        const Vector3 positionA = PhysicsBodyPosition( hotRead, bodyAIndex );
        const Vector3 positionB = PhysicsBodyPosition( hotRead, bodyBIndex );
        if ( positionB.y + radiusB + EXPLICIT_WAKE_VERTICAL_SLOP < positionA.y - radiusA )
        {
            return false;
        }
        const float range = radiusA + radiusB + EXPLICIT_WAKE_NEIGHBOR_SLOP;
        const Vector3 delta = positionB - positionA;
        return delta * delta <= range * range;
    };
    for ( std::size_t cursor = 0; cursor < m_restingWakeQueueScratch.size(); ++cursor )
    {
        const int current = m_restingWakeQueueScratch[cursor];
        for ( int candidate = 0; candidate < modelCount; ++candidate )
        {
            if ( m_restingWakeVisitedScratch[static_cast<std::size_t>( candidate )] ||
                 candidate >= static_cast<int>( m_sleepState.size() ) || m_sleepState[candidate] == 0 ||
                 IsSolverBodyFixed( ConstPhysicsBodyHotFields( context.hotFields ), candidate ) ||
                 IsUnderwaterSleepLocked( modelCount, candidate ) ||
                 ( !hasPersistentContactEdge( current, candidate ) && !isLikelyRestingNeighbor( current, candidate ) ) )
            {
                continue;
            }
            m_restingWakeVisitedScratch[static_cast<std::size_t>( candidate )] = 1;
            m_restingWakeQueueScratch.push_back( candidate );
            WakeDynamicBodyState( context, candidate, dt, applyForces );
        }
    }
}

void PhysicsSleepController::WakeModel( const PhysicsSleepWakeContext& context, int index )
{
    const int modelCount = (std::min)( context.bodyCount, static_cast<int>( context.bodyRecords.size() ) );
    if ( index >= 0 && index < modelCount )
    {
        if ( IsSolverBodyFixed( ConstPhysicsBodyHotFields( context.hotFields ), index ) )
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
        if ( context.bodyStore && !m_underwaterSleepLocked[index] && m_sleepState[index] )
        {
            bool refreshedSubmersion = false;
            if ( context.colliderStore && context.worldForces )
            {
                refreshedSubmersion = BuoyancySystem::RefreshUnderwaterSubmersionForBall( *context.worldForces,
                                                                                          *context.bodyStore,
                                                                                          *context.colliderStore,
                                                                                          index );
            }
            const PhysicsBodyRecord* record = context.bodyStore->RecordForModelIndex( index );
            if ( record && context.colliderStore && ( refreshedSubmersion || record->submergedVolumePercent > 0.0f ) &&
                 BuoyancySystem::IsFullySubmergedBall( *record,
                                                       context.hotFields.fixed[static_cast<std::size_t>( index )] != 0u,
                                                       *context.colliderStore,
                                                       index ) )
            {
                m_underwaterSleepLocked[index] = 1;
                if ( index < static_cast<int>( context.timeRemaining.size() ) )
                {
                    context.timeRemaining[index] = 0.0f;
                }
                return;
            }
        }
        if ( IsUnderwaterSleepLocked( modelCount, index ) )
        {
            return;
        }
        WakeSleepVisualIsland( context, index, 0.0f, false );
        WakePointJointIsland( context, index, 0.0f, false );
        WakeRestingContactIsland( context, index, 0.0f, false );
    }
}

PhysicsNarrowphaseWakeAccess::PhysicsNarrowphaseWakeAccess( PhysicsBodyStore& bodyStore,
                                                            const ColliderStore& colliderStore,
                                                            const PhysicsWorldForces& worldForces,
                                                            std::span<PhysicsBodyRecord> bodyRecords,
                                                            PhysicsBodyHotFieldsView hotFields,
                                                            std::span<float> timeRemaining,
                                                            std::span<uint8_t> sleepState,
                                                            std::span<uint8_t> sleepCounter,
                                                            std::span<int> sleepIslandVisualId,
                                                            std::span<const uint8_t> underwaterSleepLocked,
                                                            int modelCount,
                                                            float dt )
    : m_bodyStore( bodyStore ), m_colliderStore( colliderStore ), m_worldForces( worldForces ),
      m_bodyRecords( bodyRecords ), m_hotFields( hotFields ), m_timeRemaining( timeRemaining ),
      m_sleepState( sleepState ), m_sleepCounter( sleepCounter ), m_sleepIslandVisualId( sleepIslandVisualId ),
      m_underwaterSleepLocked( underwaterSleepLocked ), m_modelCount( modelCount ), m_dt( dt )
{
}

void PhysicsNarrowphaseWakeAccess::WakeBody( int sleepingIndex ) const
{
    // Why: narrowphase and tornado wakeups must re-enter the body into this
    // tick synchronously; deferring this mutation changes later pair reads.
    if ( sleepingIndex < 0 || sleepingIndex >= m_modelCount ||
         IsSolverBodyFixed( ConstPhysicsBodyHotFields( m_hotFields ), sleepingIndex ) || !m_sleepState[sleepingIndex] ||
         ( sleepingIndex < static_cast<int>( m_underwaterSleepLocked.size() ) &&
           m_underwaterSleepLocked[sleepingIndex] ) )
    {
        return;
    }
    m_sleepState[sleepingIndex] = 0;
    m_sleepCounter[sleepingIndex] = 0;
    m_sleepIslandVisualId[sleepingIndex] = 0;
    m_timeRemaining[sleepingIndex] = m_dt;
    m_hotFields.awake[static_cast<std::size_t>( sleepingIndex )] = 1u;
    (void)m_bodyStore.ApplyForces( m_worldForces, m_colliderStore, sleepingIndex, m_dt );
}

PhysicsNarrowphaseWakeAccess
PhysicsSleepController::CreateNarrowphaseWakeAccess( PhysicsBodyStore& bodyStore,
                                                     const ColliderStore& colliderStore,
                                                     const PhysicsWorldForces& worldForces,
                                                     std::span<PhysicsBodyRecord> bodyRecords,
                                                     std::span<float> timeRemaining,
                                                     int modelCount,
                                                     float dt )
{
    return PhysicsNarrowphaseWakeAccess( bodyStore,
                                         colliderStore,
                                         worldForces,
                                         bodyRecords,
                                         bodyStore.MutableHotFields(),
                                         timeRemaining,
                                         m_sleepState,
                                         m_sleepCounter,
                                         m_sleepIslandVisualId,
                                         m_underwaterSleepLocked,
                                         modelCount,
                                         dt );
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
}

bool PhysicsSleepController::IsPhysicsSleepEnabled() const
{
    return m_sleepEnabled;
}
