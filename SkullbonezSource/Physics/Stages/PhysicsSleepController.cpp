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
#include "../../Runtime/Scene/SceneCapacity.h"
#include "../ColliderStore.h"
#include "../DisjointSet.h"
#include "../PhysicsBodyStore.h"
#include "../PhysicsWorldForces.h"

#include <algorithm>

using namespace SkullbonezCore::Physics;
using SkullbonezCore::Math::Vector::Vector3;
namespace Vector = SkullbonezCore::Math::Vector;

namespace
{
constexpr float POINT_JOINT_SLEEP_MIN_ERROR_TOLERANCE = 0.15f;
constexpr float POINT_JOINT_SLEEP_SLACK_TOLERANCE_SCALE = 0.75f;
constexpr float POINT_JOINT_SLEEP_LINEAR_SPEED_SCALE = 6.0f;
constexpr float POINT_JOINT_SLEEP_ANGULAR_SPEED_SCALE = 6.0f;
constexpr std::size_t MAX_PIPELINE_TRACE_RECORDS = 4096;

bool IsSolverBodyFixed( const PhysicsBodyHotFieldsConstView& hotFields, int bodyIndex )
{
    return hotFields.fixed[static_cast<std::size_t>( bodyIndex )] != 0u;
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
    m_seedSleepFrameCount = static_cast<uint8_t>( (std::max)( 0, (std::min)( config.physicsSleep.frames, 255 ) ) );
}

PhysicsSleepStepPolicy PhysicsSleepController::ResolveStepPolicy( const Core::PhysicsSleepConfig& config ) const
{
    // Why: sleep eligibility and wake-energy thresholds are sleep-domain
    // policy. The facade sequences the resulting value without re-deciding it.
    const float linearSpeed = (std::max)( 0.0f, config.linearSpeed );
    const float angularSpeed = (std::max)( 0.0f, config.angularSpeed );
    return PhysicsSleepStepPolicy{ linearSpeed * linearSpeed,
                                   angularSpeed * angularSpeed,
                                   static_cast<uint8_t>( (std::max)( 1, (std::min)( config.frames, 255 ) ) ) };
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

void PhysicsSleepController::MirrorFlagsFrom( PhysicsBodyStore& bodyStore, int modelCount )
{
    const PhysicsBodyHotFieldsConstView hotFields = bodyStore.HotFields();
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
        if ( i < static_cast<int>( hotFields.fixed.size() ) && hotFields.fixed[static_cast<std::size_t>( i )] != 0u )
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


void PhysicsSleepController::PropagateSupport( const PhysicsBodyStore& bodyStore )
{
    SleepSupportPropagationContext context{ m_sleepState, m_sleepSupportEdges, m_sleepSupportedThisFrame };
    m_sleepIslandSystem.PropagateSupport( context, bodyStore.HotFields() );
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
        if ( a < 0 || b < 0 || a == b || a >= modelCount || b >= modelCount )
        {
            continue;
        }
        m_sleepSupportEdges.emplace_back( a, b );
        m_sleepSupportEdges.emplace_back( b, a );
    }
}

void PhysicsSleepController::WakePointJointConnectedBodies(
    PhysicsBodyStore& bodyStore,
    const ColliderStore& colliderStore,
    const PhysicsWorldForces& worldForces,
    std::span<float> timeRemaining,
    PhysicsContactCacheWakeAccess contactCache,
    std::span<const PersistentContact> persistentContacts,
    const std::vector<PointJointConstraint>& pointJointConstraints,
    float dt )
{
    if ( pointJointConstraints.empty() || m_sleepState.empty() )
    {
        return;
    }
    const auto bodyRecords = bodyStore.Records();
    const PhysicsBodyHotFieldsView hotFields = bodyStore.MutableHotFields();
    const PhysicsBodyHotFieldsConstView hotRead = ConstPhysicsBodyHotFields( hotFields );
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
    for ( const PointJointConstraint& constraint : pointJointConstraints )
    {
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
        if ( m_sleepPointJointBody[i] == 0 || IsSolverBodyFixed( hotRead, i ) )
        {
            continue;
        }
        const int root = sleepIslands.Find( i );
        if ( m_sleepState[i] != 0 )
        {
            m_sleepIslandCanSleep[root] = 1;
        }
        else
        {
            m_sleepIslandHasAwake[root] = 1;
        }
    }
    const PhysicsSleepWakeContext wakeContext{ modelCount,
                                               bodyRecords,
                                               hotFields,
                                               &bodyStore,
                                               &colliderStore,
                                               &worldForces,
                                               timeRemaining,
                                               contactCache,
                                               persistentContacts,
                                               pointJointConstraints };
    for ( int i = 0; i < modelCount; ++i )
    {
        if ( m_sleepPointJointBody[i] == 0 || IsSolverBodyFixed( hotRead, i ) || m_sleepState[i] == 0 )
        {
            continue;
        }
        const int root = sleepIslands.Find( i );
        if ( m_sleepIslandHasAwake[root] != 0 && m_sleepIslandCanSleep[root] != 0 )
        {
            WakeDynamicBodyState( wakeContext, i, dt, true );
        }
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
    for ( int i = 0; i < modelCount; ++i )
    {
        m_sleepIslandParent[i] = i;
    }

    // Concept: the controller makes one sleep decision for each connected
    // contact/joint component, retaining the established deterministic order.
    DisjointSet sleepIslands( m_sleepIslandParent, m_sleepIslandRank, modelCount );
    for ( const PersistentContact& contact : context.persistentContacts )
    {
        if ( contact.bodyA >= 0 && contact.bodyA < modelCount && contact.bodyB >= 0 && contact.bodyB < modelCount )
        {
            sleepIslands.Unite( contact.bodyA, contact.bodyB );
        }
    }
    for ( const PointJointConstraint& constraint : context.pointJointConstraints )
    {
        const int a = constraint.BodyAIndex( context.bodyStore );
        const int b = constraint.BodyBIndex( context.bodyStore );
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
        if ( IsSolverBodyFixed( ConstPhysicsBodyHotFields( context.hotFields ), x ) ||
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

    for ( const PointJointConstraint& constraint : context.pointJointConstraints )
    {
        const int a = constraint.BodyAIndex( context.bodyStore );
        const int b = constraint.BodyBIndex( context.bodyStore );
        if ( a < 0 || b < 0 || a == b || a >= modelCount || b >= modelCount )
        {
            continue;
        }
        auto orientationA =
            PhysicsBodyOrientation( ConstPhysicsBodyHotFields( context.hotFields ), static_cast<size_t>( a ) );
        auto orientationB =
            PhysicsBodyOrientation( ConstPhysicsBodyHotFields( context.hotFields ), static_cast<size_t>( b ) );
        const auto rotA = orientationA.GetOrientationMatrix();
        const auto rotB = orientationB.GetOrientationMatrix();
        const Vector3 anchorA =
            PhysicsBodyPosition( ConstPhysicsBodyHotFields( context.hotFields ), static_cast<size_t>( a ) ) +
            rotA * constraint.localAnchorA;
        const Vector3 anchorB =
            PhysicsBodyPosition( ConstPhysicsBodyHotFields( context.hotFields ), static_cast<size_t>( b ) ) +
            rotB * constraint.localAnchorB;
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
        if ( IsSolverBodyFixed( ConstPhysicsBodyHotFields( context.hotFields ), x ) || m_sleepState[x] )
        {
            continue;
        }
        const int root = sleepIslands.Find( x );
        m_sleepIslandHasAwake[root] = 1;
        const Vector3 vel =
            PhysicsBodyLinearVelocity( ConstPhysicsBodyHotFields( context.hotFields ), static_cast<size_t>( x ) );
        const Vector3 omega =
            PhysicsBodyAngularVelocity( ConstPhysicsBodyHotFields( context.hotFields ), static_cast<size_t>( x ) );
        const float speedSq = vel.x * vel.x + vel.y * vel.y + vel.z * vel.z;
        const float omegaSq = omega.x * omega.x + omega.y * omega.y + omega.z * omega.z;
        bool supported = x < static_cast<int>( m_sleepSupportedThisFrame.size() ) && m_sleepSupportedThisFrame[x] != 0;
        const bool hasRestingObjectContact = x < static_cast<int>( context.persistentRestingContactCounts.size() ) &&
                                             context.persistentRestingContactCounts[x] > 0;
        const bool islandHasSupportAnchor = m_sleepIslandHasSupportAnchor[root] != 0;
        const bool pointJointMember =
            x < static_cast<int>( m_sleepPointJointBody.size() ) && m_sleepPointJointBody[x] != 0;
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
        {
            m_sleepIslandEligible[root] = 0;
        }

        PhysicsPipelineRecord record;
        record.stage = PhysicsPipelineStage::SleepIslandDecision;
        record.bodyA = x;
        record.bodyB = root;
        record.point = PhysicsBodyPosition( ConstPhysicsBodyHotFields( context.hotFields ), static_cast<size_t>( x ) );
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
        if ( IsSolverBodyFixed( ConstPhysicsBodyHotFields( context.hotFields ), x ) || m_sleepState[x] )
        {
            continue;
        }
        const int root = sleepIslands.Find( x );
        if ( m_sleepIslandHasAwake[root] && m_sleepIslandEligible[root] )
        {
            if ( m_sleepCounter[x] < context.sleepFrames )
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
        if ( IsSolverBodyFixed( ConstPhysicsBodyHotFields( context.hotFields ), x ) || m_sleepState[x] )
        {
            continue;
        }
        const int root = sleepIslands.Find( x );
        if ( m_sleepCounter[x] < context.sleepFrames )
        {
            m_sleepIslandCanSleep[root] = 0;
        }
    }

    m_sleepIslandAssignedVisualId.assign( modelCount, 0 );
    for ( int x = 0; x < modelCount; ++x )
    {
        if ( IsSolverBodyFixed( ConstPhysicsBodyHotFields( context.hotFields ), x ) || !m_sleepState[x] ||
             m_sleepIslandVisualId[x] == 0 )
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
        if ( IsSolverBodyFixed( ConstPhysicsBodyHotFields( context.hotFields ), x ) || m_sleepState[x] )
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
            PhysicsPipelineRecord record;
            record.stage = PhysicsPipelineStage::SleepIslandDecision;
            record.bodyA = x;
            record.bodyB = root;
            record.point =
                PhysicsBodyPosition( ConstPhysicsBodyHotFields( context.hotFields ), static_cast<size_t>( x ) );
            record.scalarA = 1.0f;
            record.scalarB = static_cast<float>( m_sleepIslandAssignedVisualId[root] );
            record.scalarC = static_cast<float>( m_sleepCounter[x] );
            RecordPipelineStage( context.physicsPipelineTrace, record );
            const size_t bodyIndex = static_cast<size_t>( x );
            context.hotFields.linearVelocityX[bodyIndex] = 0.0f;
            context.hotFields.linearVelocityY[bodyIndex] = 0.0f;
            context.hotFields.linearVelocityZ[bodyIndex] = 0.0f;
            context.hotFields.angularVelocityX[bodyIndex] = 0.0f;
            context.hotFields.angularVelocityY[bodyIndex] = 0.0f;
            context.hotFields.angularVelocityZ[bodyIndex] = 0.0f;
            context.hotFields.awake[bodyIndex] = 0u;
            LockUnderwaterSleeperIfReady( context.worldForces,
                                          context.bodyStore,
                                          context.colliderStore,
                                          context.timeRemaining,
                                          x );
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
