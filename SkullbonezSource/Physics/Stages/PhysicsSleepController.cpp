/*
File: SkullbonezSource/Physics/Stages/PhysicsSleepController.cpp
Purpose:
  Implements deterministic sleep mirroring, wake propagation, and islands.

Summary:
  This file mechanically re-homes the former PhysicsWorld sleep algorithms.
  All ordering, thresholds, packed contact traversal, and transition expressions
  remain unchanged while state and mutation authority become cohesive.

Glossary:
  Wake fan-out: Expansion through visual, point-joint, and resting-contact islands.
  Credible support: Terrain, fixed, or previously proven sleeping island anchor.
  Quiet-frame counter: Consecutive eligible ticks required before deactivation.

Invariants:
  - Fixed bodies never enter dynamic sleep state.
  - Underwater-locked bodies reject ordinary wake fan-out.
  - Pipeline records retain their former call positions and bounded cap.

Related:
  - SkullbonezSource/Physics/Stages/PhysicsSleepController.h
  - SkullbonezSource/Physics/PhysicsWorld.cpp
  - SkullbonezSource/Physics/SleepIslandSystem.cpp
*/
#include "PhysicsSleepController.h"

#include "PhysicsContactSolverStage.h"
#include "../../Core/Config.h"
#include "../../Core/FatalError.h"
#include "../../Runtime/Scene/SceneCapacity.h"
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
constexpr float POINT_JOINT_SLEEP_MIN_ERROR_TOLERANCE = 0.15f;
constexpr float POINT_JOINT_SLEEP_SLACK_TOLERANCE_SCALE = 0.75f;
constexpr float POINT_JOINT_SLEEP_LINEAR_SPEED_SCALE = 6.0f;
constexpr float POINT_JOINT_SLEEP_ANGULAR_SPEED_SCALE = 6.0f;
constexpr std::size_t MAX_PIPELINE_TRACE_RECORDS = 4096;

template <typename T> uint64_t VectorCapacityBytes( const std::vector<T>& values )
{
    return static_cast<uint64_t>( values.capacity() ) * static_cast<uint64_t>( sizeof( T ) );
}

bool IsSolverBodyFixed( std::span<const PhysicsBodyRecord> bodyRecords, int bodyIndex )
{
    return bodyRecords[static_cast<std::size_t>( bodyIndex )].isFixed;
}

bool IsPointJointBodyPair( const PhysicsBodyStore& bodyStore,
                           const std::vector<PointJointConstraint>& pointJointConstraints,
                           int bodyA,
                           int bodyB )
{
    for ( const PointJointConstraint& constraint : pointJointConstraints )
    {
        const int a = constraint.BodyAIndex( bodyStore );
        const int b = constraint.BodyBIndex( bodyStore );
        if ( ( a == bodyA && b == bodyB ) || ( a == bodyB && b == bodyA ) )
        {
            return true;
        }
    }
    return false;
}

void RecordPipelineStage( std::vector<PhysicsPipelineRecord>& trace, const PhysicsPipelineRecord& record )
{
    if ( trace.size() < MAX_PIPELINE_TRACE_RECORDS )
    {
        trace.push_back( record );
    }
}
} // namespace

PhysicsSleepController::PhysicsSleepController()
{
    const std::size_t bodyCapacity = SkullbonezCore::Scene::Capacity::MAX_GAME_MODELS;
    m_sleepSupportedThisFrame.reserve( bodyCapacity );
    m_sleepInhibitedThisFrame.reserve( bodyCapacity );
    m_sleepState.reserve( bodyCapacity );
    m_sleepCounter.reserve( bodyCapacity );
    m_underwaterSleepLocked.reserve( bodyCapacity );
    m_sleepIslandVisualId.reserve( bodyCapacity );
    m_sleepIslandAssignedVisualId.reserve( bodyCapacity );
    m_sleepSupportEdges.reserve( bodyCapacity * 4 );
    m_sleepIslandParent.reserve( bodyCapacity );
    m_sleepIslandRank.reserve( bodyCapacity );
    m_sleepIslandHasAwake.reserve( bodyCapacity );
    m_sleepIslandHasSupportAnchor.reserve( bodyCapacity );
    m_sleepIslandEligible.reserve( bodyCapacity );
    m_sleepIslandCanSleep.reserve( bodyCapacity );
    m_sleepPointJointBody.reserve( bodyCapacity );
    m_sleepIslandHasPointJoint.reserve( bodyCapacity );
    m_sleepIslandPointJointsRelaxed.reserve( bodyCapacity );
    m_sleepVisualIslandIds.reserve( bodyCapacity );
    m_sleepVisualIslandBodies.reserve( bodyCapacity );
    m_restingWakeVisitedScratch.reserve( bodyCapacity );
    m_restingWakeQueueScratch.reserve( bodyCapacity );
}

void PhysicsSleepController::Clear()
{
    m_sleepSupportedThisFrame.clear();
    m_sleepInhibitedThisFrame.clear();
    m_sleepState.clear();
    m_sleepCounter.clear();
    m_underwaterSleepLocked.clear();
    m_sleepIslandVisualId.clear();
    m_sleepIslandAssignedVisualId.clear();
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
    m_restingWakeVisitedScratch.clear();
    m_restingWakeQueueScratch.clear();
    m_nextSleepIslandVisualId = 1;
}

void PhysicsSleepController::ApplyRuntimeConfig( const Core::EngineConfig& config )
{
    m_seedSleepFrameCount =
        static_cast<uint8_t>( (std::max)( 0, (std::min)( config.physicsSleep.frames, 255 ) ) );
}

void PhysicsSleepController::EnsureUnderwaterSleepLockBuffer( int modelCount )
{
    if ( modelCount >= 0 && static_cast<int>( m_underwaterSleepLocked.size() ) != modelCount )
    {
        m_underwaterSleepLocked.resize( static_cast<std::size_t>( modelCount ), 0 );
    }
}

void PhysicsSleepController::EnsureVisualIdSize( int modelCount )
{
    if ( static_cast<int>( m_sleepIslandVisualId.size() ) != modelCount )
    {
        m_sleepIslandVisualId.assign( modelCount, 0 );
    }
}

void PhysicsSleepController::MirrorFlagsFrom( PhysicsBodyStore& bodyStore,
                                              std::span<const PhysicsBodyRecord> bodyRecords,
                                              int modelCount )
{
    m_sleepSupportedThisFrame.assign( modelCount, 0 );
    m_sleepInhibitedThisFrame.assign( modelCount, 0 );
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
        if ( i < static_cast<int>( bodyRecords.size() ) && bodyRecords[static_cast<std::size_t>( i )].isFixed )
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
}

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
    if ( !record || !BuoyancySystem::IsFullySubmergedBall( *record, colliderStore, index ) )
    {
        return;
    }
    m_underwaterSleepLocked[index] = 1;
    if ( index < static_cast<int>( timeRemaining.size() ) )
    {
        timeRemaining[index] = 0.0f;
    }
    record->linearVelocity = Vector::ZERO_VECTOR;
    record->angularVelocity = Vector::ZERO_VECTOR;
    record->isSleeping = true;
}

bool PhysicsSleepController::WakeDynamicBodyState( const PhysicsSleepWakeContext& context,
                                                   int index,
                                                   float dt,
                                                   bool applyForces )
{
    // Invariant: every wake clears the model-order sleep rows and the contact
    // cache before the body can participate in later fixed-step stages.
    if ( index < 0 || index >= context.bodyCount || index >= static_cast<int>( context.bodyRecords.size() ) ||
         index >= static_cast<int>( m_sleepState.size() ) || IsSolverBodyFixed( context.bodyRecords, index ) )
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
    if ( context.bodyStore )
    {
        PhysicsBodyRecord* record = context.bodyStore->MutableRecordForModelIndex( index );
        if ( record && !record->isFixed )
        {
            record->isSleeping = false;
        }
    }
    if ( index < static_cast<int>( m_sleepCounter.size() ) ) m_sleepCounter[index] = 0;
    if ( index < static_cast<int>( m_underwaterSleepLocked.size() ) ) m_underwaterSleepLocked[index] = 0;
    if ( index < static_cast<int>( m_sleepIslandVisualId.size() ) ) m_sleepIslandVisualId[index] = 0;
    if ( dt > 0.0f && index < static_cast<int>( context.timeRemaining.size() ) ) context.timeRemaining[index] = dt;
    if ( applyForces && wasSleeping && dt > TOLERANCE && context.bodyStore && context.worldForces &&
         context.colliderStore )
    {
        (void)context.bodyStore->ApplyForces( *context.worldForces, *context.colliderStore, index, dt );
    }
    context.contactSolverStage.ForgetPersistentContactCacheForBody( index );
    return wasSleeping || hadCounter || hadSleepVisual || wasUnderwaterLocked;
}

void PhysicsSleepController::WakeSleepVisualIsland( const PhysicsSleepWakeContext& context,
                                                    int index,
                                                    float dt,
                                                    bool applyForces )
{
    if ( index < 0 || index >= static_cast<int>( m_sleepState.size() ) ) return;
    const int visualId = index < static_cast<int>( m_sleepIslandVisualId.size() ) ? m_sleepIslandVisualId[index] : 0;
    if ( visualId > 0 )
    {
        const int count = (std::min)( { static_cast<int>( m_sleepIslandVisualId.size() ),
                                        context.bodyCount,
                                        static_cast<int>( context.bodyRecords.size() ) } );
        for ( int i = 0; i < count; ++i )
        {
            if ( m_sleepIslandVisualId[i] == visualId ) WakeDynamicBodyState( context, i, dt, applyForces );
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
    if ( context.bodyStore == nullptr ) return;
    const int modelCount =
        (std::min)( { context.bodyCount, context.bodyStore->Count(), static_cast<int>( context.bodyRecords.size() ) } );
    if ( context.pointJointConstraints.empty() || index < 0 || index >= modelCount ||
         index >= static_cast<int>( m_sleepState.size() ) ) return;
    m_sleepIslandParent.assign( modelCount, 0 );
    m_sleepIslandRank.assign( modelCount, 0 );
    m_sleepPointJointBody.assign( modelCount, 0 );
    for ( int i = 0; i < modelCount; ++i ) m_sleepIslandParent[i] = i;
    DisjointSet sleepIslands( m_sleepIslandParent, m_sleepIslandRank, modelCount );
    for ( const PointJointConstraint& constraint : context.pointJointConstraints )
    {
        const int a = constraint.BodyAIndex( *context.bodyStore );
        const int b = constraint.BodyBIndex( *context.bodyStore );
        if ( a < 0 || b < 0 || a == b || a >= modelCount || b >= modelCount ) continue;
        m_sleepPointJointBody[a] = 1;
        m_sleepPointJointBody[b] = 1;
        sleepIslands.Unite( a, b );
    }
    if ( m_sleepPointJointBody[index] == 0 ) return;
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
    if ( index < 0 || index >= modelCount || index >= static_cast<int>( m_sleepState.size() ) ) return;
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
                return true;
        }
        return false;
    };
    const auto isLikelyRestingNeighbor = [&]( int a, int b )
    {
        const PhysicsBodyRecord& recordA = context.bodyRecords[static_cast<std::size_t>( a )];
        const PhysicsBodyRecord& recordB = context.bodyRecords[static_cast<std::size_t>( b )];
        const float radiusA = (std::max)( 0.01f, recordA.boundingRadius );
        const float radiusB = (std::max)( 0.01f, recordB.boundingRadius );
        if ( recordB.position.y + radiusB + EXPLICIT_WAKE_VERTICAL_SLOP < recordA.position.y - radiusA ) return false;
        const float range = radiusA + radiusB + EXPLICIT_WAKE_NEIGHBOR_SLOP;
        const Vector3 delta = recordB.position - recordA.position;
        return delta * delta <= range * range;
    };
    for ( std::size_t cursor = 0; cursor < m_restingWakeQueueScratch.size(); ++cursor )
    {
        const int current = m_restingWakeQueueScratch[cursor];
        for ( int candidate = 0; candidate < modelCount; ++candidate )
        {
            if ( m_restingWakeVisitedScratch[static_cast<std::size_t>( candidate )] ||
                 candidate >= static_cast<int>( m_sleepState.size() ) || m_sleepState[candidate] == 0 ||
                 IsSolverBodyFixed( context.bodyRecords, candidate ) || IsUnderwaterSleepLocked( modelCount, candidate ) ||
                 ( !hasPersistentContactEdge( current, candidate ) && !isLikelyRestingNeighbor( current, candidate ) ) )
                continue;
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
        if ( IsSolverBodyFixed( context.bodyRecords, index ) ) return;
    }
    else if ( index >= 0 ) return;
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
                refreshedSubmersion = BuoyancySystem::RefreshUnderwaterSubmersionForBall(
                    *context.worldForces, *context.bodyStore, *context.colliderStore, index );
            const PhysicsBodyRecord* record = context.bodyStore->RecordForModelIndex( index );
            if ( record && context.colliderStore && ( refreshedSubmersion || record->submergedVolumePercent > 0.0f ) &&
                 BuoyancySystem::IsFullySubmergedBall( *record, *context.colliderStore, index ) )
            {
                m_underwaterSleepLocked[index] = 1;
                if ( index < static_cast<int>( context.timeRemaining.size() ) ) context.timeRemaining[index] = 0.0f;
                return;
            }
        }
        if ( IsUnderwaterSleepLocked( modelCount, index ) ) return;
        WakeSleepVisualIsland( context, index, 0.0f, false );
        WakePointJointIsland( context, index, 0.0f, false );
        WakeRestingContactIsland( context, index, 0.0f, false );
    }
}

void PhysicsSleepController::WakeNarrowphaseBody( PhysicsBodyStore& bodyStore,
                                                  const ColliderStore& colliderStore,
                                                  const PhysicsWorldForces& worldForces,
                                                  std::span<PhysicsBodyRecord> bodyRecords,
                                                  std::span<float> timeRemaining,
                                                  int modelCount,
                                                  int sleepingIndex,
                                                  float dt )
{
    // Why: narrowphase and tornado wakeups must re-enter the body into this
    // tick synchronously; deferring this mutation changes later pair reads.
    if ( sleepingIndex < 0 || sleepingIndex >= modelCount || IsSolverBodyFixed( bodyRecords, sleepingIndex ) ||
         !m_sleepState[sleepingIndex] ||
         ( sleepingIndex < static_cast<int>( m_underwaterSleepLocked.size() ) &&
           m_underwaterSleepLocked[sleepingIndex] ) ) return;
    m_sleepState[sleepingIndex] = 0;
    m_sleepCounter[sleepingIndex] = 0;
    m_sleepIslandVisualId[sleepingIndex] = 0;
    timeRemaining[sleepingIndex] = dt;
    bodyRecords[static_cast<std::size_t>( sleepingIndex )].isSleeping = false;
    (void)bodyStore.ApplyForces( worldForces, colliderStore, sleepingIndex, dt );
}

void PhysicsSleepController::SeedModelAsleep( int bodyCount,
                                              std::span<const PhysicsBodyRecord> bodyRecords,
                                              int index )
{
    if ( !m_sleepEnabled ) return;
    const int modelCount = (std::min)( bodyCount, static_cast<int>( bodyRecords.size() ) );
    if ( index < 0 || index >= modelCount || IsSolverBodyFixed( bodyRecords, index ) ) return;
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
    if ( m_nextSleepIslandVisualId <= 0 ) m_nextSleepIslandVisualId = 1;
}

void PhysicsSleepController::SetPhysicsSleepEnabled( bool enabled )
{
    m_sleepEnabled = enabled;
    if ( enabled ) return;
    std::fill( m_sleepState.begin(), m_sleepState.end(), static_cast<uint8_t>( 0 ) );
    std::fill( m_sleepCounter.begin(), m_sleepCounter.end(), static_cast<uint8_t>( 0 ) );
    std::fill( m_underwaterSleepLocked.begin(), m_underwaterSleepLocked.end(), static_cast<uint8_t>( 0 ) );
    std::fill( m_sleepIslandVisualId.begin(), m_sleepIslandVisualId.end(), 0 );
    std::fill( m_sleepIslandAssignedVisualId.begin(), m_sleepIslandAssignedVisualId.end(), 0 );
}

bool PhysicsSleepController::IsPhysicsSleepEnabled() const { return m_sleepEnabled; }

void PhysicsSleepController::PropagateSupport( std::span<const PhysicsBodyRecord> bodyRecords )
{
    SleepSupportPropagationContext context{ m_sleepState, m_sleepSupportEdges, m_sleepSupportedThisFrame };
    m_sleepIslandSystem.PropagateSupport( context, bodyRecords );
}

void PhysicsSleepController::AppendPointJointSupportEdges(
    const PhysicsBodyStore& bodyStore,
    const std::vector<PointJointConstraint>& pointJointConstraints,
    int modelCount )
{
    for ( const PointJointConstraint& constraint : pointJointConstraints )
    {
        const int a = constraint.BodyAIndex( bodyStore );
        const int b = constraint.BodyBIndex( bodyStore );
        if ( a < 0 || b < 0 || a == b || a >= modelCount || b >= modelCount ) continue;
        m_sleepSupportEdges.emplace_back( a, b );
        m_sleepSupportEdges.emplace_back( b, a );
    }
}

void PhysicsSleepController::WakePointJointConnectedBodies(
    PhysicsBodyStore& bodyStore,
    const ColliderStore& colliderStore,
    const PhysicsWorldForces& worldForces,
    std::span<float> timeRemaining,
    PhysicsContactSolverStage& contactSolverStage,
    const std::vector<PointJointConstraint>& pointJointConstraints,
    float dt )
{
    if ( pointJointConstraints.empty() || m_sleepState.empty() ) return;
    const auto bodyRecords = bodyStore.Records();
    const int modelCount = (std::min)( bodyStore.Count(), static_cast<int>( bodyRecords.size() ) );
    m_sleepIslandParent.assign( modelCount, 0 );
    m_sleepIslandRank.assign( modelCount, 0 );
    m_sleepPointJointBody.assign( modelCount, 0 );
    m_sleepIslandHasAwake.assign( modelCount, 0 );
    m_sleepIslandCanSleep.assign( modelCount, 0 );
    for ( int i = 0; i < modelCount; ++i ) m_sleepIslandParent[i] = i;
    DisjointSet sleepIslands( m_sleepIslandParent, m_sleepIslandRank, modelCount );
    for ( const PointJointConstraint& constraint : pointJointConstraints )
    {
        const int a = constraint.BodyAIndex( bodyStore );
        const int b = constraint.BodyBIndex( bodyStore );
        if ( a < 0 || b < 0 || a == b || a >= modelCount || b >= modelCount ||
             a >= static_cast<int>( m_sleepState.size() ) || b >= static_cast<int>( m_sleepState.size() ) ) continue;
        m_sleepPointJointBody[a] = 1;
        m_sleepPointJointBody[b] = 1;
        sleepIslands.Unite( a, b );
    }
    for ( int i = 0; i < modelCount; ++i )
    {
        if ( m_sleepPointJointBody[i] == 0 || IsSolverBodyFixed( bodyRecords, i ) ) continue;
        const int root = sleepIslands.Find( i );
        if ( m_sleepState[i] != 0 ) m_sleepIslandCanSleep[root] = 1;
        else m_sleepIslandHasAwake[root] = 1;
    }
    const PhysicsSleepWakeContext wakeContext{ modelCount,
                                               bodyRecords,
                                               &bodyStore,
                                               &colliderStore,
                                               &worldForces,
                                               timeRemaining,
                                               contactSolverStage,
                                               contactSolverStage.GetPersistentContacts(),
                                               pointJointConstraints };
    for ( int i = 0; i < modelCount; ++i )
    {
        if ( m_sleepPointJointBody[i] == 0 || IsSolverBodyFixed( bodyRecords, i ) || m_sleepState[i] == 0 ) continue;
        const int root = sleepIslands.Find( i );
        if ( m_sleepIslandHasAwake[root] != 0 && m_sleepIslandCanSleep[root] != 0 )
            WakeDynamicBodyState( wakeContext, i, dt, true );
    }
}

void PhysicsSleepController::RunIslandStage( const PhysicsSleepIslandStageContext& context )
{
    // Invariant: contact rows, point joints, and persisted visual ids are
    // united in their original order before any eligibility decision is made.
    const int modelCount = context.modelCount;
    m_sleepIslandParent.assign( modelCount, 0 );
    m_sleepIslandRank.assign( modelCount, 0 );
    m_sleepIslandHasAwake.assign( modelCount, 0 );
    m_sleepIslandHasSupportAnchor.assign( modelCount, 0 );
    m_sleepIslandEligible.assign( modelCount, 1 );
    m_sleepIslandCanSleep.assign( modelCount, 1 );
    m_sleepPointJointBody.assign( modelCount, 0 );
    m_sleepIslandHasPointJoint.assign( modelCount, 0 );
    m_sleepIslandPointJointsRelaxed.assign( modelCount, 1 );
    for ( int i = 0; i < modelCount; ++i ) m_sleepIslandParent[i] = i;

    // Concept: the controller makes one sleep decision for each connected
    // contact/joint component, retaining the established deterministic order.
    DisjointSet sleepIslands( m_sleepIslandParent, m_sleepIslandRank, modelCount );
    for ( const PersistentContact& contact : context.persistentContacts )
    {
        if ( contact.bodyA >= 0 && contact.bodyA < modelCount && contact.bodyB >= 0 && contact.bodyB < modelCount )
            sleepIslands.Unite( contact.bodyA, contact.bodyB );
    }
    for ( const PointJointConstraint& constraint : context.pointJointConstraints )
    {
        const int a = constraint.BodyAIndex( context.bodyStore );
        const int b = constraint.BodyBIndex( context.bodyStore );
        if ( a < 0 || b < 0 || a == b || a >= modelCount || b >= modelCount ) continue;
        m_sleepPointJointBody[a] = 1;
        m_sleepPointJointBody[b] = 1;
        sleepIslands.Unite( a, b );
    }

    m_sleepVisualIslandIds.clear();
    m_sleepVisualIslandBodies.clear();
    for ( int x = 0; x < modelCount; ++x )
    {
        const int visualId = x < static_cast<int>( m_sleepIslandVisualId.size() ) ? m_sleepIslandVisualId[x] : 0;
        if ( visualId <= 0 ) continue;
        int visualSlot = -1;
        for ( int i = 0; i < static_cast<int>( m_sleepVisualIslandIds.size() ); ++i )
        {
            if ( m_sleepVisualIslandIds[i] == visualId )
            {
                visualSlot = i;
                break;
            }
        }
        if ( visualSlot >= 0 ) sleepIslands.Unite( m_sleepVisualIslandBodies[visualSlot], x );
        else
        {
            m_sleepVisualIslandIds.push_back( visualId );
            m_sleepVisualIslandBodies.push_back( x );
        }
    }

    for ( int x = 0; x < modelCount; ++x )
    {
        const int root = sleepIslands.Find( x );
        if ( IsSolverBodyFixed( context.bodyRecords, x ) ||
             ( x < static_cast<int>( m_sleepState.size() ) && m_sleepState[x] != 0 ) ||
             ( x < static_cast<int>( m_sleepSupportedThisFrame.size() ) && m_sleepSupportedThisFrame[x] != 0 ) )
            m_sleepIslandHasSupportAnchor[root] = 1;
        if ( m_sleepPointJointBody[x] != 0 ) m_sleepIslandHasPointJoint[root] = 1;
    }

    for ( const PointJointConstraint& constraint : context.pointJointConstraints )
    {
        const int a = constraint.BodyAIndex( context.bodyStore );
        const int b = constraint.BodyBIndex( context.bodyStore );
        if ( a < 0 || b < 0 || a == b || a >= modelCount || b >= modelCount ) continue;
        auto orientationA = context.bodyRecords[static_cast<size_t>( a )].orientation;
        auto orientationB = context.bodyRecords[static_cast<size_t>( b )].orientation;
        const auto rotA = orientationA.GetOrientationMatrix();
        const auto rotB = orientationB.GetOrientationMatrix();
        const Vector3 anchorA = context.bodyRecords[static_cast<size_t>( a )].position + rotA * constraint.localAnchorA;
        const Vector3 anchorB = context.bodyRecords[static_cast<size_t>( b )].position + rotB * constraint.localAnchorB;
        const float distance = Vector::VectorMag( anchorB - anchorA );
        const float allowedDistance =
            constraint.slack + (std::max)( POINT_JOINT_SLEEP_MIN_ERROR_TOLERANCE,
                                           constraint.slack * POINT_JOINT_SLEEP_SLACK_TOLERANCE_SCALE );
        if ( distance > allowedDistance ) m_sleepIslandPointJointsRelaxed[sleepIslands.Find( a )] = 0;
    }

    for ( int x = 0; x < modelCount; ++x )
    {
        if ( IsSolverBodyFixed( context.bodyRecords, x ) || m_sleepState[x] ) continue;
        const int root = sleepIslands.Find( x );
        m_sleepIslandHasAwake[root] = 1;
        const Vector3& vel = context.bodyRecords[static_cast<size_t>( x )].linearVelocity;
        const Vector3& omega = context.bodyRecords[static_cast<size_t>( x )].angularVelocity;
        const float speedSq = vel.x * vel.x + vel.y * vel.y + vel.z * vel.z;
        const float omegaSq = omega.x * omega.x + omega.y * omega.y + omega.z * omega.z;
        bool supported = x < static_cast<int>( m_sleepSupportedThisFrame.size() ) && m_sleepSupportedThisFrame[x] != 0;
        const bool hasRestingObjectContact = x < static_cast<int>( context.persistentRestingContactCounts.size() ) &&
                                             context.persistentRestingContactCounts[x] > 0;
        const bool islandHasSupportAnchor = m_sleepIslandHasSupportAnchor[root] != 0;
        const bool pointJointMember = x < static_cast<int>( m_sleepPointJointBody.size() ) && m_sleepPointJointBody[x] != 0;
        const bool pointJointIsland = m_sleepIslandHasPointJoint[root] != 0;
        float quietLinearSq = context.sleepLinearSq;
        float quietAngularSq = context.sleepAngularSq;
        if ( pointJointMember && pointJointIsland && islandHasSupportAnchor )
        {
            quietLinearSq *= POINT_JOINT_SLEEP_LINEAR_SPEED_SCALE * POINT_JOINT_SLEEP_LINEAR_SPEED_SCALE;
            quietAngularSq *= POINT_JOINT_SLEEP_ANGULAR_SPEED_SCALE * POINT_JOINT_SLEEP_ANGULAR_SPEED_SCALE;
        }
        const bool quiet = speedSq < quietLinearSq && omegaSq < quietAngularSq;
        const bool pointJointAnchoredSupport = quiet && pointJointMember && pointJointIsland && islandHasSupportAnchor;
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
        const bool terrainInhibitBlocksSleep = m_sleepInhibitedThisFrame[x] != 0 &&
                                               !( quiet && hasRestingObjectContact && islandHasSupportAnchor ) &&
                                               !pointJointAnchoredSupport;
        const bool pointJointErrorBlocksSleep = pointJointMember &&
                                                root < static_cast<int>( m_sleepIslandPointJointsRelaxed.size() ) &&
                                                m_sleepIslandPointJointsRelaxed[root] == 0;
        if ( !quiet || !supported || terrainInhibitBlocksSleep || pointJointErrorBlocksSleep )
            m_sleepIslandEligible[root] = 0;

        PhysicsPipelineRecord record;
        record.stage = PhysicsPipelineStage::SleepIslandDecision;
        record.bodyA = x;
        record.bodyB = root;
        record.point = context.bodyRecords[static_cast<size_t>( x )].position;
        record.scalarA = quiet ? 1.0f : 0.0f;
        record.scalarB = supported ? 1.0f : 0.0f;
        record.scalarC = terrainInhibitBlocksSleep ? 1.0f : ( pointJointErrorBlocksSleep ? 2.0f : 0.0f );
        RecordPipelineStage( context.physicsPipelineTrace, record );
    }

    if ( !m_sleepEnabled )
    {
        std::fill( m_sleepCounter.begin(), m_sleepCounter.end(), static_cast<uint8_t>( 0 ) );
        m_sleepIslandCanSleep.assign( modelCount, 0 );
        m_sleepIslandAssignedVisualId.assign( modelCount, 0 );
        return;
    }
    ApplyTransitions( context, sleepIslands );
}

void PhysicsSleepController::ApplyTransitions( const PhysicsSleepIslandStageContext& context,
                                               DisjointSet& sleepIslands )
{
    // Invariant: RunIslandStage has already populated eligibility and support;
    // this pass only advances counters and applies whole-island transitions.
    const int modelCount = context.modelCount;
    for ( int x = 0; x < modelCount; ++x )
    {
        if ( IsSolverBodyFixed( context.bodyRecords, x ) || m_sleepState[x] ) continue;
        const int root = sleepIslands.Find( x );
        if ( m_sleepIslandHasAwake[root] && m_sleepIslandEligible[root] )
        {
            if ( m_sleepCounter[x] < context.sleepFrames ) ++m_sleepCounter[x];
        }
        else m_sleepCounter[x] = 0;
    }
    for ( int x = 0; x < modelCount; ++x )
    {
        if ( IsSolverBodyFixed( context.bodyRecords, x ) || m_sleepState[x] ) continue;
        const int root = sleepIslands.Find( x );
        if ( m_sleepCounter[x] < context.sleepFrames ) m_sleepIslandCanSleep[root] = 0;
    }

    m_sleepIslandAssignedVisualId.assign( modelCount, 0 );
    for ( int x = 0; x < modelCount; ++x )
    {
        if ( IsSolverBodyFixed( context.bodyRecords, x ) || !m_sleepState[x] || m_sleepIslandVisualId[x] == 0 ) continue;
        const int root = sleepIslands.Find( x );
        if ( m_sleepIslandAssignedVisualId[root] == 0 ) m_sleepIslandAssignedVisualId[root] = m_sleepIslandVisualId[x];
    }
    for ( int x = 0; x < modelCount; ++x )
    {
        if ( IsSolverBodyFixed( context.bodyRecords, x ) || m_sleepState[x] ) continue;
        const int root = sleepIslands.Find( x );
        if ( m_sleepIslandHasAwake[root] && m_sleepIslandEligible[root] && m_sleepIslandCanSleep[root] )
        {
            if ( m_sleepIslandAssignedVisualId[root] == 0 )
            {
                m_sleepIslandAssignedVisualId[root] = m_nextSleepIslandVisualId++;
                if ( m_nextSleepIslandVisualId <= 0 ) m_nextSleepIslandVisualId = 1;
            }
            m_sleepState[x] = 1;
            m_sleepIslandVisualId[x] = m_sleepIslandAssignedVisualId[root];
            PhysicsPipelineRecord record;
            record.stage = PhysicsPipelineStage::SleepIslandDecision;
            record.bodyA = x;
            record.bodyB = root;
            record.point = context.bodyRecords[static_cast<size_t>( x )].position;
            record.scalarA = 1.0f;
            record.scalarB = static_cast<float>( m_sleepIslandAssignedVisualId[root] );
            record.scalarC = static_cast<float>( m_sleepCounter[x] );
            RecordPipelineStage( context.physicsPipelineTrace, record );
            context.bodyRecords[static_cast<size_t>( x )].linearVelocity = Math::Vector::ZERO_VECTOR;
            context.bodyRecords[static_cast<size_t>( x )].angularVelocity = Math::Vector::ZERO_VECTOR;
            context.bodyRecords[static_cast<size_t>( x )].isSleeping = true;
            LockUnderwaterSleeperIfReady(
                context.worldForces, context.bodyStore, context.colliderStore, context.timeRemaining, x );
        }
    }
}

bool PhysicsSleepController::IsPointJointPair( const PhysicsBodyStore& bodyStore,
                                               const std::vector<PointJointConstraint>& pointJointConstraints,
                                               int bodyA,
                                               int bodyB ) const
{
    return IsPointJointBodyPair( bodyStore, pointJointConstraints, bodyA, bodyB );
}

void PhysicsSleepController::CaptureReplayState( Runtime::ReplaySolverWorldSnapshot& snapshot ) const
{
    snapshot.sleepSupportedThisFrame = m_sleepSupportedThisFrame;
    snapshot.sleepInhibitedThisFrame = m_sleepInhibitedThisFrame;
    snapshot.sleepState = m_sleepState;
    snapshot.sleepCounter = m_sleepCounter;
    snapshot.underwaterSleepLocked = m_underwaterSleepLocked;
    snapshot.sleepIslandVisualId = m_sleepIslandVisualId;
    snapshot.sleepIslandAssignedVisualId = m_sleepIslandAssignedVisualId;
    snapshot.sleepSupportEdges = m_sleepSupportEdges;
    snapshot.sleepIslandParent = m_sleepIslandParent;
    snapshot.sleepIslandRank = m_sleepIslandRank;
    snapshot.sleepIslandHasAwake = m_sleepIslandHasAwake;
    snapshot.sleepIslandHasSupportAnchor = m_sleepIslandHasSupportAnchor;
    snapshot.sleepIslandEligible = m_sleepIslandEligible;
    snapshot.sleepIslandCanSleep = m_sleepIslandCanSleep;
    snapshot.nextSleepIslandVisualId = m_nextSleepIslandVisualId;
    snapshot.sleepEnabled = m_sleepEnabled;
}

void PhysicsSleepController::RestoreReplayState( const Runtime::ReplaySolverWorldSnapshot& snapshot )
{
    m_sleepSupportedThisFrame = snapshot.sleepSupportedThisFrame;
    m_sleepInhibitedThisFrame = snapshot.sleepInhibitedThisFrame;
    m_sleepState = snapshot.sleepState;
    m_sleepCounter = snapshot.sleepCounter;
    m_underwaterSleepLocked = snapshot.underwaterSleepLocked;
    m_sleepIslandVisualId = snapshot.sleepIslandVisualId;
    m_sleepIslandAssignedVisualId = snapshot.sleepIslandAssignedVisualId;
    m_sleepSupportEdges = snapshot.sleepSupportEdges;
    m_sleepIslandParent = snapshot.sleepIslandParent;
    m_sleepIslandRank = snapshot.sleepIslandRank;
    m_sleepIslandHasAwake = snapshot.sleepIslandHasAwake;
    m_sleepIslandHasSupportAnchor = snapshot.sleepIslandHasSupportAnchor;
    m_sleepIslandEligible = snapshot.sleepIslandEligible;
    m_sleepIslandCanSleep = snapshot.sleepIslandCanSleep;
    m_nextSleepIslandVisualId = snapshot.nextSleepIslandVisualId;
    m_sleepEnabled = snapshot.sleepEnabled;
}

std::span<const uint8_t> PhysicsSleepController::GetSleepStates() const { return m_sleepState; }
std::span<const uint8_t> PhysicsSleepController::GetUnderwaterSleepLocks() const { return m_underwaterSleepLocked; }
std::span<const int> PhysicsSleepController::GetSleepIslandVisualIds() const { return m_sleepIslandVisualId; }
std::span<const uint8_t> PhysicsSleepController::GetSleepSupportedStates() const { return m_sleepSupportedThisFrame; }
std::span<const uint8_t> PhysicsSleepController::GetSleepInhibitedStates() const { return m_sleepInhibitedThisFrame; }
std::span<const std::pair<int, int>> PhysicsSleepController::GetSleepSupportEdges() const { return m_sleepSupportEdges; }
std::vector<std::pair<int, int>>& PhysicsSleepController::MutableSupportEdgesForContactSolver()
{
    return m_sleepSupportEdges;
}
std::span<uint8_t> PhysicsSleepController::MutableSupportedStatesForTerrain() { return m_sleepSupportedThisFrame; }
std::span<uint8_t> PhysicsSleepController::MutableInhibitedStatesForTerrain() { return m_sleepInhibitedThisFrame; }
const std::vector<int>& PhysicsSleepController::GetSleepIslandParents() const { return m_sleepIslandParent; }
const std::vector<uint8_t>& PhysicsSleepController::GetSleepCounters() const { return m_sleepCounter; }
const std::vector<uint8_t>& PhysicsSleepController::GetSleepIslandRanks() const { return m_sleepIslandRank; }
const std::vector<uint8_t>& PhysicsSleepController::GetSleepIslandHasAwake() const { return m_sleepIslandHasAwake; }
const std::vector<uint8_t>& PhysicsSleepController::GetSleepIslandHasSupportAnchor() const
{
    return m_sleepIslandHasSupportAnchor;
}
const std::vector<uint8_t>& PhysicsSleepController::GetSleepIslandEligible() const { return m_sleepIslandEligible; }
const std::vector<uint8_t>& PhysicsSleepController::GetSleepIslandCanSleep() const { return m_sleepIslandCanSleep; }
const std::vector<uint8_t>& PhysicsSleepController::GetUnderwaterSleepLockVector() const
{
    return m_underwaterSleepLocked;
}
const std::vector<int>& PhysicsSleepController::GetSleepIslandVisualIdVector() const { return m_sleepIslandVisualId; }
const std::vector<int>& PhysicsSleepController::GetSleepIslandAssignedVisualIds() const
{
    return m_sleepIslandAssignedVisualId;
}
const std::vector<uint8_t>& PhysicsSleepController::GetSleepStateVector() const { return m_sleepState; }
const std::vector<uint8_t>& PhysicsSleepController::GetSleepSupportedVector() const { return m_sleepSupportedThisFrame; }
const std::vector<uint8_t>& PhysicsSleepController::GetSleepInhibitedVector() const { return m_sleepInhibitedThisFrame; }
const std::vector<std::pair<int, int>>& PhysicsSleepController::GetSleepSupportEdgeVector() const
{
    return m_sleepSupportEdges;
}

uint64_t PhysicsSleepController::CollectDynamicMemoryBytes() const
{
    uint64_t bytes = 0;
    bytes += VectorCapacityBytes( m_sleepSupportedThisFrame );
    bytes += VectorCapacityBytes( m_sleepInhibitedThisFrame );
    bytes += VectorCapacityBytes( m_sleepState );
    bytes += VectorCapacityBytes( m_sleepCounter );
    bytes += VectorCapacityBytes( m_underwaterSleepLocked );
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
    bytes += VectorCapacityBytes( m_restingWakeVisitedScratch );
    bytes += VectorCapacityBytes( m_restingWakeQueueScratch );
    return bytes;
}
