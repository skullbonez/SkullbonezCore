/*
File: SkullbonezSource/Physics/Stages/PhysicsSleepController.cpp
Purpose:
  Implements deterministic sleep mirroring, wake propagation, and islands.

Summary:
  This file implements the serial sleep-island algorithms plus the ascending
  dense awake index list updated at sleep/wake transitions. Thresholds, packed
  contact traversal, and transition expressions remain unchanged.

Glossary:
  Wake fan-out: Expansion through visual, point-joint, and resting-contact islands.
  Credible support: Terrain, fixed, or previously proven sleeping island anchor.
  Quiet-frame counter: Consecutive eligible ticks required before deactivation.
  Awake list position: Reverse map from dense body row to its slot in the
    ascending awake list, or -1 when fixed/dormant.

Invariants:
  - Fixed bodies never enter dynamic sleep state.
  - Underwater-locked bodies reject ordinary wake fan-out.
  - Pipeline records retain their former call positions and bounded cap.
  - Ordinary fixed steps update awake indices only at explicit transitions;
    full rebuilds are limited to topology/replay/config cold boundaries.

Related:
  - SkullbonezSource/Physics/Stages/PhysicsSleepController.h
  - SkullbonezSource/Physics/PhysicsWorld.cpp
  - SkullbonezSource/Physics/SleepIslandSystem.cpp
*/
#include "PhysicsSleepController.h"

#include "PhysicsContactSolverStage.h"
#include "../../Core/FatalError.h"
#include "../../Core/SceneCapacity.h"
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

void SkullbonezCore::Physics::ValidateSleepSupportEdgeCount( std::size_t requested,
                                                             std::size_t reservedCapacity,
                                                             std::size_t highWater,
                                                             const char* phase )
{
    if ( requested <= MAX_SLEEP_SUPPORT_EDGES && requested <= reservedCapacity )
    {
        return;
    }

    SB_FATAL( "Physics/SleepSupportEdges",
              "Sleep support edge capacity exceeded: requested=%zu capacity=%zu reserved_capacity=%zu "
              "high_water=%zu phase=%s.",
              requested,
              MAX_SLEEP_SUPPORT_EDGES,
              reservedCapacity,
              highWater,
              phase );
}

void SkullbonezCore::Physics::AppendSleepSupportEdge( std::vector<std::pair<int, int>>& edges,
                                                      int supporter,
                                                      int supported )
{
    // Hazard: checking the semantic cap alone would still let an incorrectly
    // initialized vector grow below that cap. The actual construction reserve
    // is part of the fail-before-grow contract too.
    ValidateSleepSupportEdgeCount( edges.size() + 1u, edges.capacity(), edges.size(), "steady_gameplay" );
    edges.emplace_back( supporter, supported );
}

PhysicsSleepController::PhysicsSleepController()
{
    const std::size_t bodyCapacity = SkullbonezCore::Scene::Capacity::MAX_SCENE_OBJECTS;
    m_sleepSupportedThisFrame.reserve( bodyCapacity );
    m_sleepInhibitedThisFrame.reserve( bodyCapacity );
    m_sleepState.reserve( bodyCapacity );
    m_sleepCounter.reserve( bodyCapacity );
    m_underwaterSleepLocked.reserve( bodyCapacity );
    m_sleepIslandVisualId.reserve( bodyCapacity );
    m_sleepIslandAssignedVisualId.reserve( bodyCapacity );
    m_sleepSupportEdges.reserve( MAX_SLEEP_SUPPORT_EDGES );
    m_sleepIslandParent.reserve( bodyCapacity );
    m_sleepIslandRank.reserve( bodyCapacity );
    m_sleepIslandHasAwake.reserve( bodyCapacity );
    m_sleepIslandHasSupportAnchor.reserve( bodyCapacity );
    m_sleepIslandEligible.reserve( bodyCapacity );
    m_sleepIslandCanSleep.reserve( bodyCapacity );
    m_sleepScratchFlags.reserve( bodyCapacity );
    m_sleepVisualIslandIds.reserve( bodyCapacity );
    m_sleepVisualIslandBodies.reserve( bodyCapacity );
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
    m_sleepScratchFlags.clear();
    m_sleepVisualIslandIds.clear();
    m_sleepVisualIslandBodies.clear();
    m_restingWakeQueueScratch.clear();
    m_awakeBodyIndices.clear();
    m_awakeListPositions.clear();
    m_pendingAwakeCount = 0;
    m_awakeListNeedsRebuild = true;
    m_nextSleepIslandVisualId = 1;
    m_awakeBodyCount = 0;
}

void PhysicsSleepController::ApplyRuntimeSettings( const SleepSettings& settings )
{
    m_seedSleepFrameCount = static_cast<uint8_t>( (std::max)( 0, (std::min)( settings.frames, 255 ) ) );
}

PhysicsSleepStepPolicy PhysicsSleepController::ResolveStepPolicy( const SleepSettings& settings ) const
{
    // Why: sleep eligibility and wake-energy thresholds are sleep-domain
    // policy. PhysicsWorld sequences the resulting value without re-deciding it.
    const float linearSpeed = (std::max)( 0.0f, settings.linearSpeed );
    const float angularSpeed = (std::max)( 0.0f, settings.angularSpeed );
    return PhysicsSleepStepPolicy { linearSpeed * linearSpeed,
                                    angularSpeed * angularSpeed,
                                    static_cast<uint8_t>( (std::max)( 1, (std::min)( settings.frames, 255 ) ) ) };
}

void PhysicsSleepController::EnsureUnderwaterSleepLockBuffer( int modelCount )
{
    if ( modelCount >= 0 && static_cast<int>( m_underwaterSleepLocked.size() ) != modelCount )
    {
        m_underwaterSleepLocked.resize( static_cast<std::size_t>( modelCount ), 0 );
    }
}

void PhysicsSleepController::EnsureScratchFlagsSize( int modelCount )
{
    if ( modelCount >= 0 && static_cast<int>( m_sleepScratchFlags.size() ) != modelCount )
    {
        m_sleepScratchFlags.assign( static_cast<std::size_t>( modelCount ), PhysicsSleepScratchFlags {} );
    }
}

void PhysicsSleepController::EnsureVisualIdSize( int modelCount )
{
    if ( static_cast<int>( m_sleepIslandVisualId.size() ) != modelCount )
    {
        m_sleepIslandVisualId.assign( modelCount, 0 );
    }
}

void PhysicsSleepController::RebuildAwakeBodyIndices( const PhysicsBodyHotFieldsConstView& hotFields, int modelCount )
{
    // Cold boundary: topology changes and replay restores can reassign dense
    // model indices. Ordinary fixed steps update this list only at wake/sleep
    // transitions and never rebuild it from the full body set.
    m_awakeBodyIndices.clear();
    m_awakeListPositions.assign( static_cast<std::size_t>( modelCount ), -1 );
    for ( int index = 0; index < modelCount; ++index )
    {
        if ( hotFields.fixed[static_cast<std::size_t>( index )] == 0u && m_sleepState[index] == 0u )
        {
            m_awakeListPositions[static_cast<std::size_t>( index )] = static_cast<int>( m_awakeBodyIndices.size() );
            m_awakeBodyIndices.push_back( index );
        }
    }

    m_awakeBodyCount = static_cast<int>( m_awakeBodyIndices.size() );
    m_awakeListNeedsRebuild = false;
}

void PhysicsSleepController::AddAwakeBodyIndex( int index )
{
    if ( index < 0 || index >= static_cast<int>( m_awakeListPositions.size() ) || m_awakeListNeedsRebuild )
    {
        m_awakeListNeedsRebuild = true;
        return;
    }

    if ( m_awakeListPositions[static_cast<std::size_t>( index )] >= 0 )
    {
        return;
    }

    std::size_t insertAt = 0u;
    while ( insertAt < m_awakeBodyIndices.size() && m_awakeBodyIndices[insertAt] < index )
    {
        ++insertAt;
    }

    m_awakeBodyIndices.push_back( index );
    for ( std::size_t position = m_awakeBodyIndices.size() - 1u; position > insertAt; --position )
    {
        const int shifted = m_awakeBodyIndices[position - 1u];
        m_awakeBodyIndices[position] = shifted;
        m_awakeListPositions[static_cast<std::size_t>( shifted )] = static_cast<int>( position );
    }

    m_awakeBodyIndices[insertAt] = index;
    m_awakeListPositions[static_cast<std::size_t>( index )] = static_cast<int>( insertAt );
    m_awakeBodyCount = static_cast<int>( m_awakeBodyIndices.size() );
}

void PhysicsSleepController::RemoveAwakeBodyIndex( int index )
{
    if ( index < 0 || index >= static_cast<int>( m_awakeListPositions.size() ) || m_awakeListNeedsRebuild )
    {
        m_awakeListNeedsRebuild = true;
        return;
    }

    const int removeAt = m_awakeListPositions[static_cast<std::size_t>( index )];
    if ( removeAt < 0 )
    {
        return;
    }

    for ( std::size_t position = static_cast<std::size_t>( removeAt ); position + 1u < m_awakeBodyIndices.size();
          ++position )
    {
        const int shifted = m_awakeBodyIndices[position + 1u];
        m_awakeBodyIndices[position] = shifted;
        m_awakeListPositions[static_cast<std::size_t>( shifted )] = static_cast<int>( position );
    }

    m_awakeBodyIndices.pop_back();
    m_awakeListPositions[static_cast<std::size_t>( index )] = -1;
    m_awakeBodyCount = static_cast<int>( m_awakeBodyIndices.size() );
}

void PhysicsSleepController::InvalidateBodyTopology()
{
    m_awakeListNeedsRebuild = true;
}

void PhysicsSleepController::FlushPendingAwakeBodyIndices()
{
    // Parallel wake workers publish only bounded body indices. The sequencer
    // folds them into the sorted owner list after WorkerPool completion, so
    // worker scheduling cannot affect later force/integration order.
    std::atomic_ref<int> pendingAwakeCount( m_pendingAwakeCount );
    const int pendingCount = pendingAwakeCount.exchange( 0, std::memory_order_acquire );
    if ( pendingCount < 0 || pendingCount > Scene::Capacity::MAX_SCENE_OBJECTS )
    {
        SB_FATAL( "Physics/PhysicsSleepController",
                  "Pending awake queue count invalid: count=%d capacity=%d.",
                  pendingCount,
                  Scene::Capacity::MAX_SCENE_OBJECTS );
    }

    for ( int pendingIndex = 0; pendingIndex < pendingCount; ++pendingIndex )
    {
        AddAwakeBodyIndex( m_pendingAwakeIndices[pendingIndex] );
    }
}

bool PhysicsSleepController::MirrorFlagsFrom( PhysicsBodyStore& bodyStore, int modelCount )
{
    const PhysicsBodyHotFieldsConstView hotFields = bodyStore.HotFields();
    const bool rebuildAwakeList = m_awakeListNeedsRebuild ||
                                  static_cast<int>( m_awakeListPositions.size() ) != modelCount;

    m_sleepSupportedThisFrame.assign( modelCount, 0 );
    m_sleepInhibitedThisFrame.assign( modelCount, 0 );
    m_sleepSupportEdges.clear();
    if ( static_cast<int>( m_sleepState.size() ) != modelCount )
    {
        m_sleepState.assign( modelCount, 0 );
        m_sleepCounter.assign( modelCount, 0 );
    }

    if ( rebuildAwakeList )
    {
        // Cold boundary: authored topology/body commands own the body-store
        // flag. Steady steps retain m_sleepState directly and avoid two full
        // mirror passes over sleepers that cannot have changed state.
        bodyStore.CopySleepStatesTo( m_sleepState );
    }

    EnsureUnderwaterSleepLockBuffer( modelCount );
    // Hazard: the first fixed step can arrive before any island rebuild has
    // sized the diagnostics mirror. Keep it row-aligned before indexing below;
    // this changes no solver decision or byte-exact physics value.
    EnsureVisualIdSize( modelCount );
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

    if ( rebuildAwakeList )
    {
        RebuildAwakeBodyIndices( hotFields, modelCount );
        // Sleep-disable and replay/topology repair can change the controller
        // row while rebuilding. Publish that cold result before hot kernels
        // consult the body-store awake flag.
        bodyStore.CopySleepStatesFrom( m_sleepState );
    }

#if defined( _DEBUG )
    else
    {
        // Invariant: every non-topology wake/sleep path updates the list at the
        // transition. A mismatch here exposes a bypass before it can reorder a
        // worker stage or strand a body outside broadphase maintenance.
        for ( int index = 0; index < modelCount; ++index )
        {
            const bool expectedAwake = hotFields.fixed[static_cast<std::size_t>( index )] == 0u &&
                                       m_sleepState[index] == 0u;

            const bool listedAwake = m_awakeListPositions[static_cast<std::size_t>( index )] >= 0;
            assert( expectedAwake == listedAwake && "awake index list drift" );
        }
    }
#endif
    m_awakeBodyCount = static_cast<int>( m_awakeBodyIndices.size() );
    return rebuildAwakeList;
}


void PhysicsSleepController::PropagateSupport( const PhysicsBodyStore& bodyStore )
{
    SleepSupportPropagationContext context { m_sleepState, m_sleepSupportEdges, m_sleepSupportedThisFrame };
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

        AppendSleepSupportEdge( m_sleepSupportEdges, a, b );
        AppendSleepSupportEdge( m_sleepSupportEdges, b, a );
    }
}

void PhysicsSleepController::WakePointJointConnectedBodies(
    PhysicsBodyStore& bodyStore,
    const ColliderStore& colliderStore,
    PhysicsTerrainView terrain,
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
    EnsureScratchFlagsSize( modelCount );
    for ( PhysicsSleepScratchFlags& flags : m_sleepScratchFlags )
    {
        flags.pointJointBody = 0u;
    }

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

        m_sleepScratchFlags[a].pointJointBody = 1u;
        m_sleepScratchFlags[b].pointJointBody = 1u;
        sleepIslands.Unite( a, b );
    }

    for ( int i = 0; i < modelCount; ++i )
    {
        if ( m_sleepScratchFlags[i].pointJointBody == 0u || IsSolverBodyFixed( hotRead, i ) )
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

    const PhysicsSleepWakeContext wakeContext { modelCount,
                                                bodyRecords,
                                                hotFields,
                                                &bodyStore,
                                                &colliderStore,
                                                terrain,
                                                &worldForces,
                                                timeRemaining,
                                                contactCache,
                                                persistentContacts,
                                                pointJointConstraints };

    for ( int i = 0; i < modelCount; ++i )
    {
        if ( m_sleepScratchFlags[i].pointJointBody == 0u || IsSolverBodyFixed( hotRead, i ) || m_sleepState[i] == 0 )
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
    EnsureScratchFlagsSize( modelCount );
    for ( PhysicsSleepScratchFlags& flags : m_sleepScratchFlags )
    {
        flags.pointJointBody = 0u;
        flags.islandHasPointJoint = 0u;
        flags.islandPointJointsRelaxed = 1u;
    }

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

        m_sleepScratchFlags[a].pointJointBody = 1u;
        m_sleepScratchFlags[b].pointJointBody = 1u;
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

        if ( m_sleepScratchFlags[x].pointJointBody != 0u )
        {
            m_sleepScratchFlags[root].islandHasPointJoint = 1u;
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

        auto orientationA = PhysicsBodyOrientation( ConstPhysicsBodyHotFields( context.hotFields ),
                                                    static_cast<size_t>( a ) );

        auto orientationB = PhysicsBodyOrientation( ConstPhysicsBodyHotFields( context.hotFields ),
                                                    static_cast<size_t>( b ) );

        const auto rotA = orientationA.GetOrientationMatrix();
        const auto rotB = orientationB.GetOrientationMatrix();
        const Vector3 anchorA = PhysicsBodyPosition( ConstPhysicsBodyHotFields( context.hotFields ),
                                                     static_cast<size_t>( a ) ) +
                                rotA * constraint.localAnchorA;

        const Vector3 anchorB = PhysicsBodyPosition( ConstPhysicsBodyHotFields( context.hotFields ),
                                                     static_cast<size_t>( b ) ) +
                                rotB * constraint.localAnchorB;

        const float distance = Vector::VectorMag( anchorB - anchorA );
        const float allowedDistance = constraint.slack +
                                      (std::max)( POINT_JOINT_SLEEP_MIN_ERROR_TOLERANCE,
                                                  constraint.slack * POINT_JOINT_SLEEP_SLACK_TOLERANCE_SCALE );

        if ( distance > allowedDistance )
        {
            m_sleepScratchFlags[sleepIslands.Find( a )].islandPointJointsRelaxed = 0u;
        }
    }

    // P4 invariant: the awake list is ascending dense order, so this walk
    // performs the same per-body arithmetic in the same order while skipping
    // fixed and sleeping guard reads entirely.
    for ( int x : context.awakeBodyIndices )
    {
#if defined( _DEBUG )
        assert( !IsSolverBodyFixed( ConstPhysicsBodyHotFields( context.hotFields ), x ) && !m_sleepState[x] );
#endif
        const int root = sleepIslands.Find( x );
        m_sleepIslandHasAwake[root] = 1;
        const Vector3 vel = PhysicsBodyLinearVelocity( ConstPhysicsBodyHotFields( context.hotFields ),
                                                       static_cast<size_t>( x ) );

        const Vector3 omega = PhysicsBodyAngularVelocity( ConstPhysicsBodyHotFields( context.hotFields ),
                                                          static_cast<size_t>( x ) );

        const float speedSq = vel.x * vel.x + vel.y * vel.y + vel.z * vel.z;
        const float omegaSq = omega.x * omega.x + omega.y * omega.y + omega.z * omega.z;
        bool supported = x < static_cast<int>( m_sleepSupportedThisFrame.size() ) && m_sleepSupportedThisFrame[x] != 0;
        const bool hasRestingObjectContact = x < static_cast<int>( context.persistentRestingContactCounts.size() ) &&
                                             context.persistentRestingContactCounts[x] > 0;

        const bool islandHasSupportAnchor = m_sleepIslandHasSupportAnchor[root] != 0;
        const bool pointJointMember = x < static_cast<int>( m_sleepScratchFlags.size() ) &&
                                      m_sleepScratchFlags[x].pointJointBody != 0u;

        const bool pointJointIsland = m_sleepScratchFlags[root].islandHasPointJoint != 0u;
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
                                                root < static_cast<int>( m_sleepScratchFlags.size() ) &&
                                                m_sleepScratchFlags[root].islandPointJointsRelaxed == 0u;

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
        m_awakeBodyCount = static_cast<int>( m_awakeBodyIndices.size() );
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
    for ( int x : context.awakeBodyIndices )
    {
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

    for ( int x : context.awakeBodyIndices )
    {
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

    // Removing a sleeping row compacts m_awakeBodyIndices. Keep the cursor on
    // that slot after a transition so the next higher dense index is not
    // skipped; non-transition rows advance normally. This retains ascending
    // model order without copying or allocating a second list.
    for ( std::size_t awakeSlot = 0; awakeSlot < m_awakeBodyIndices.size(); )
    {
        const int x = m_awakeBodyIndices[awakeSlot];
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
            RemoveAwakeBodyIndex( x );
            m_sleepIslandVisualId[x] = m_sleepIslandAssignedVisualId[root];
            PhysicsPipelineRecord record;
            record.stage = PhysicsPipelineStage::SleepIslandDecision;
            record.bodyA = x;
            record.bodyB = root;
            record.point = PhysicsBodyPosition( ConstPhysicsBodyHotFields( context.hotFields ),
                                                static_cast<size_t>( x ) );

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

            continue;
        }

        ++awakeSlot;
    }

    m_awakeBodyCount = static_cast<int>( m_awakeBodyIndices.size() );
}

bool PhysicsSleepController::IsPointJointPair( const PhysicsBodyStore& bodyStore,
                                               const std::vector<PointJointConstraint>& pointJointConstraints,
                                               int bodyA,
                                               int bodyB ) const
{
    return IsPointJointBodyPair( bodyStore, pointJointConstraints, bodyA, bodyB );
}
