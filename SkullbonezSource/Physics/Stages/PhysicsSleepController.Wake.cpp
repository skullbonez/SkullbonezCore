/*
File: SkullbonezSource/Physics/Stages/PhysicsSleepController.Wake.cpp
Purpose:
  Implements synchronous sleep wake, seeding, and underwater-lock policy.

Summary:
  Wake propagation is a cohesive part of PhysicsSleepController but has enough
  bounded graph traversal and policy to merit a focused implementation unit.
  All rows remain owned by the same concrete sleep controller; parallel wake
  producers publish one atomic bit per body for sequencer-side island wake.

Glossary:
  Seed sleep: Explicitly establish an eligible body as dormant before island analysis.

Invariants:
  - Fixed and underwater-locked bodies reject ordinary wake fan-out.
  - Sequencer-side narrowphase wake reapplies forces within the same fixed step.
  - Wake scratch uses construction-reserved storage and never grows in steady play.
  - Worker threads never mutate authoritative sleep, clock, force, or awake rows.

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
#include "../PhysicsBodyStore.h"
#include "../PhysicsWorldForces.h"

#include <algorithm>

using namespace SkullbonezCore::Physics;
using SkullbonezCore::Math::Vector::Vector3;
namespace Vector = SkullbonezCore::Math::Vector;

namespace
{
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

    if ( index < static_cast<int>( m_sleepPoseAnchors.size() ) )
    {
        m_sleepPoseAnchors[index].flags &= static_cast<uint8_t>( ~SLEEP_POSE_ANCHOR_VALID_BIT );
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

bool PhysicsSleepController::WakeDynamicBodyStateWithForces(
    PhysicsBodyStore& bodyStore, const ColliderStore& colliderStore, PhysicsTerrainView terrain,
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

    if ( index < static_cast<int>( m_sleepPoseAnchors.size() ) )
    {
        m_sleepPoseAnchors[index].flags &= static_cast<uint8_t>( ~SLEEP_POSE_ANCHOR_VALID_BIT );
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

void PhysicsSleepController::WakeRetainedSimulationIsland( PhysicsBodyStore& bodyStore,
                                                           PhysicsContactCacheWakeAccess contactCache, int index )
{
    const int modelCount = (std::min)( bodyStore.Count(), static_cast<int>( bodyStore.Records().size() ) );

    if ( index < 0 || index >= modelCount || index >= static_cast<int>( m_sleepState.size() ) )
    {
        return;
    }

    const auto findRetainedRoot = [&]( int bodyIndex )
    {
        int root = bodyIndex;

        for ( int depth = 0; depth < modelCount; ++depth )
        {
            if ( root < 0 || root >= static_cast<int>( m_sleepIslandParent.size() ) )
            {
                return -1;
            }

            const int parent = m_sleepIslandParent[static_cast<std::size_t>( root )];

            if ( parent == root )
            {
                return root;
            }

            root = parent;
        }
        return -1;
    };

    const int retainedRoot = static_cast<int>( m_sleepIslandParent.size() ) == modelCount ? findRetainedRoot( index ) : -1;

    // Invariant: a completed island transition writes one parent component.
    // Explicit activation traverses that retained component in body order;
    // visual ids and geometric proximity cannot widen the wake set.
    for ( int bodyIndex = 0; bodyIndex < modelCount; ++bodyIndex )
    {
        if ( bodyIndex == index || ( retainedRoot >= 0 && findRetainedRoot( bodyIndex ) == retainedRoot ) )
        {
            WakeDynamicBodyState( bodyStore, contactCache, bodyIndex );
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

void PhysicsSleepController::WakeModel( PhysicsBodyStore& bodyStore, PhysicsContactCacheWakeAccess contactCache, int index )
{
    if ( PrepareExplicitWake( bodyStore, index ) )
    {
        if ( IsUnderwaterSleepLocked( static_cast<int>( m_sleepState.size() ), index ) )
        {
            return;
        }

        WakeRetainedSimulationIsland( bodyStore, contactCache, index );
    }
}

void PhysicsSleepController::WakeModel( PhysicsBodyStore& bodyStore, const ColliderStore& colliderStore,
                                        const PhysicsWorldForces& worldForces, std::span<BuoyancyBodyFacts> buoyancyFacts,
                                        std::span<float> timeRemaining, PhysicsContactCacheWakeAccess contactCache,
                                        int index )
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

        WakeRetainedSimulationIsland( bodyStore, contactCache, index );
    }
}

PhysicsNarrowphaseWakeAccess::PhysicsNarrowphaseWakeAccess( PhysicsSleepController& sleepController,
                                                            PhysicsBodyHotFieldsConstView hotFields, int modelCount )
    : m_sleepController( sleepController ), m_hotFields( hotFields ), m_modelCount( modelCount )
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
    if ( sleepingIndex < 0 || sleepingIndex >= m_modelCount ||
         sleepingIndex >= static_cast<int>( m_sleepController.m_sleepPoseAnchors.size() ) ||
         IsSolverBodyFixed( m_hotFields, sleepingIndex ) ||
         ( sleepingIndex < static_cast<int>( m_sleepController.m_underwaterSleepLocked.size() ) &&
           m_sleepController.m_underwaterSleepLocked[sleepingIndex] ) )
    {
        return;
    }

    // Hazard: worker completion is the synchronization boundary. Publishing a
    // request is the only legal mutation here; the serial owner later expands
    // it through the retained island and applies forces in body order.
    std::atomic_ref<uint8_t> sleepFlags(
        m_sleepController.m_sleepPoseAnchors[static_cast<std::size_t>( sleepingIndex )].flags );
    sleepFlags.fetch_or( PhysicsSleepController::PENDING_NARROWPHASE_WAKE_BIT, std::memory_order_release );
}

void PhysicsSleepController::CommitPendingNarrowphaseWakes( PhysicsBodyStore& bodyStore, const ColliderStore& colliderStore,
                                                            PhysicsTerrainView terrain,
                                                            const PhysicsWorldForces& worldForces,
                                                            std::span<BuoyancyBodyFacts> buoyancyFacts,
                                                            std::span<float> timeRemaining,
                                                            PhysicsContactCacheWakeAccess contactCache, float dt )
{
    const int modelCount = (std::min)( bodyStore.Count(), static_cast<int>( m_sleepState.size() ) );
    const PhysicsBodyHotFieldsConstView hotFields = bodyStore.HotFields();
    const bool retainedParentsValid = static_cast<int>( m_sleepIslandParent.size() ) == modelCount;

    const auto findRetainedRoot = [&]( int row )
    {
        if ( !retainedParentsValid )
        {
            return row;
        }

        int root = row;
        for ( int hop = 0; hop < modelCount; ++hop )
        {
            if ( root < 0 || root >= modelCount )
            {
                return row;
            }

            const int parent = m_sleepIslandParent[static_cast<std::size_t>( root )];
            if ( parent == root )
            {
                return root;
            }
            root = parent;
        }
        return row;
    };

    m_restingWakeQueueScratch.clear();
    for ( int bodyIndex = 0; bodyIndex < modelCount; ++bodyIndex )
    {
        std::atomic_ref<uint8_t> sleepFlags( m_sleepPoseAnchors[static_cast<std::size_t>( bodyIndex )].flags );
        const uint8_t priorFlags = sleepFlags.fetch_and( static_cast<uint8_t>( ~PENDING_NARROWPHASE_WAKE_BIT ),
                                                         std::memory_order_acquire );
        if ( ( priorFlags & PENDING_NARROWPHASE_WAKE_BIT ) == 0u || IsSolverBodyFixed( hotFields, bodyIndex ) ||
             ( bodyIndex < static_cast<int>( m_underwaterSleepLocked.size() ) && m_underwaterSleepLocked[bodyIndex] != 0u ) )
        {
            continue;
        }

        const int root = findRetainedRoot( bodyIndex );
        if ( std::find( m_restingWakeQueueScratch.begin(), m_restingWakeQueueScratch.end(), root ) ==
             m_restingWakeQueueScratch.end() )
        {
            m_restingWakeQueueScratch.push_back( root );
        }
    }

    std::sort( m_restingWakeQueueScratch.begin(), m_restingWakeQueueScratch.end() );
    for ( int bodyIndex = 0; bodyIndex < modelCount; ++bodyIndex )
    {
        if ( !std::binary_search( m_restingWakeQueueScratch.begin(), m_restingWakeQueueScratch.end(),
                                  findRetainedRoot( bodyIndex ) ) )
        {
            continue;
        }

        WakeDynamicBodyStateWithForces( bodyStore, colliderStore, terrain, worldForces, buoyancyFacts, timeRemaining,
                                        contactCache, bodyIndex, dt );
    }
}

PhysicsNarrowphaseWakeAccess PhysicsSleepController::CreateNarrowphaseWakeAccess( const PhysicsBodyStore& bodyStore )
{
    return PhysicsNarrowphaseWakeAccess( *this, bodyStore.HotFields(), bodyStore.Count() );
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
    std::fill( m_sleepCounter.begin(), m_sleepCounter.end(), uint32_t { 0u } );
    for ( PhysicsSleepPoseAnchor& anchor : m_sleepPoseAnchors )
    {
        anchor.flags = 0u;
    }
    std::fill( m_underwaterSleepLocked.begin(), m_underwaterSleepLocked.end(), static_cast<uint8_t>( 0 ) );
    std::fill( m_sleepIslandVisualId.begin(), m_sleepIslandVisualId.end(), 0 );
    std::fill( m_sleepIslandAssignedVisualId.begin(), m_sleepIslandAssignedVisualId.end(), 0 );
    m_awakeListNeedsRebuild = true;
}

bool PhysicsSleepController::IsPhysicsSleepEnabled() const
{
    return m_sleepEnabled;
}
