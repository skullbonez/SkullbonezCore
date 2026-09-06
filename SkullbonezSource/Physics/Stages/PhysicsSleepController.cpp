/*
File: SkullbonezSource/Physics/Stages/PhysicsSleepController.cpp
Purpose:
  Implements deterministic sleep mirroring, wake propagation, and islands.

Summary:
  This file implements body-local deactivation clocks, whole-island sleep
  decisions, constraint-topology wake publication, and the ascending dense
  awake index list updated at transitions.

Glossary:
  Support diagnostic: Terrain, fixed, or previously sleeping anchor evidence;
    it does not define simulation-island topology.
  Quiet-frame counter: Consecutive eligible ticks required before deactivation.
  Awake list position: Reverse map from dense body row to its slot in the
    ascending awake list, or -1 when fixed/dormant.

Invariants:
  - Fixed bodies never enter dynamic sleep state.
  - Underwater-locked bodies reject ordinary wake fan-out.
  - Pipeline events retain their former call positions and bounded cap; payload
    records exist only when the step has a full-record consumer.
  - Ordinary fixed steps update awake indices only at explicit transitions;
    full rebuilds are limited to topology/replay/config cold boundaries.
  - Static and terrain contacts anchor constraints without connecting dynamic
    bodies through the static world.

Related:
  - SkullbonezSource/Physics/Stages/PhysicsSleepController.h
  - SkullbonezSource/Physics/PhysicsWorld.cpp
  - SkullbonezSource/Physics/SleepIslandSystem.cpp
  - Agentic/Reference/engine-glossary.md
*/
#include "PhysicsSleepController.h"

#include "PhysicsContactSolverStage.h"
#include "../../Core/FatalError.h"
#include "../../Core/SceneCapacity.h"
#include "../ColliderStore.h"
#include "../DisjointSet.h"
#include "../PhysicsBodyStore.h"
#include "../TerrainSupportClassifier.h"
#include "../PhysicsWorldForces.h"
#include "PhysicsStepDiagnostics.h"

#include <algorithm>
#include <cmath>

using namespace SkullbonezCore::Physics;
using SkullbonezCore::Math::Vector::Vector3;
namespace Vector = SkullbonezCore::Math::Vector;

namespace
{
constexpr float POINT_JOINT_SLEEP_MIN_ERROR_TOLERANCE = 0.15f;
constexpr float POINT_JOINT_SLEEP_SLACK_TOLERANCE_SCALE = 0.75f;
bool IsSolverBodyFixed( const PhysicsBodyHotFieldsConstView& hotFields, int bodyIndex )
{
    return hotFields.fixed[static_cast<std::size_t>( bodyIndex )] != 0u;
}

} // namespace

void SkullbonezCore::Physics::ValidateSleepSupportEdgeCount( std::size_t requested, std::size_t reservedCapacity,
                                                             std::size_t highWater, const char* phase )
{
    if ( requested <= MAX_SLEEP_SUPPORT_EDGES && requested <= reservedCapacity )
    {
        return;
    }

    SB_FATAL( "Physics/SleepSupportEdges",
              "Sleep support edge capacity exceeded: requested=%zu capacity=%zu reserved_capacity=%zu "
              "high_water=%zu phase=%s.",
              requested, MAX_SLEEP_SUPPORT_EDGES, reservedCapacity, highWater, phase );
}

void SkullbonezCore::Physics::AppendSleepSupportEdge( PhysicsCandidatePairList& edges, int supporter, int supported )
{
    // Hazard: checking the semantic cap alone would still let an incorrectly
    // initialized list overrun below that cap. The actual scene-load commit
    // is part of the fail-before-grow contract too.
    ValidateSleepSupportEdgeCount( edges.size() + 1u, edges.capacity(), edges.size(), "steady_gameplay" );
    edges.emplace_back( supporter, supported );
}

PhysicsSleepController::PhysicsSleepController() = default;


void PhysicsSleepController::ReserveBodyCapacity( std::size_t bodyCapacity, std::size_t pointJointCapacity )
{
    const auto reserveBodyRows = [&]( auto& values ) { values.Reserve( bodyCapacity ); };

    reserveBodyRows( m_sleepSupportedThisFrame );
    reserveBodyRows( m_sleepInhibitedThisFrame );
    reserveBodyRows( m_sleepState );
    reserveBodyRows( m_sleepCounter );
    reserveBodyRows( m_underwaterSleepLocked );
    reserveBodyRows( m_sleepIslandVisualId );
    reserveBodyRows( m_sleepIslandAssignedVisualId );
    reserveBodyRows( m_jointWakeParent );
    reserveBodyRows( m_jointWakeRank );
    reserveBodyRows( m_sleepIslandParent );
    reserveBodyRows( m_sleepIslandRank );
    reserveBodyRows( m_sleepIslandHasAwake );
    reserveBodyRows( m_sleepIslandHasSupportAnchor );
    reserveBodyRows( m_sleepIslandEligible );
    reserveBodyRows( m_sleepIslandTopologyStable );
    reserveBodyRows( m_sleepIslandCanSleep );
    reserveBodyRows( m_sleepBodyEligible );
    reserveBodyRows( m_sleepResetReason );
    reserveBodyRows( m_sleepPoseAnchors );
    reserveBodyRows( m_sleepScratchFlags );
    reserveBodyRows( m_sleepFirstBoxContactPartner );
    reserveBodyRows( m_restingWakeQueueScratch );

    const std::size_t pairCapacity = (std::min)( bodyCapacity * ( bodyCapacity > 0u ? bodyCapacity - 1u : 0u ) / 2u,
                                                 MAX_SLEEP_SUPPORT_EDGES );

    const std::size_t supportCapacity = (std::min)( pairCapacity +
                                                        (std::min)( pointJointCapacity, MAX_SLEEP_SUPPORT_EDGES / 2u ) * 2u,
                                                    MAX_SLEEP_SUPPORT_EDGES );

    m_sleepSupportEdges.Reserve( supportCapacity );
    m_simulationIslands.Reserve( bodyCapacity, supportCapacity, pointJointCapacity );

    m_awakeBodyIndices.Reserve( bodyCapacity );
    m_awakeListPositions.Reserve( bodyCapacity );
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
    m_simulationIslands.Clear();
    m_jointWakeParent.clear();
    m_jointWakeRank.clear();
    m_sleepIslandParent.clear();
    m_sleepIslandRank.clear();
    m_sleepIslandHasAwake.clear();
    m_sleepIslandHasSupportAnchor.clear();
    m_sleepIslandEligible.clear();
    m_sleepIslandTopologyStable.clear();
    m_sleepIslandCanSleep.clear();
    m_sleepBodyEligible.clear();
    m_sleepResetReason.clear();
    m_sleepPoseAnchors.clear();
    m_sleepScratchFlags.clear();
    m_sleepFirstBoxContactPartner.clear();
    m_restingWakeQueueScratch.clear();
    m_awakeBodyIndices.clear();
    m_awakeListPositions.clear();
    m_pendingConstraintWakeBodyCount = 0;
    m_awakeListNeedsRebuild = true;
    m_resetDenseSleepHistoryForBodyTopologyChange = false;
    m_nextSleepIslandVisualId = 1;
    m_awakeBodyCount = 0;
}

void PhysicsSleepController::ApplyRuntimeSettings( const SleepSettings& settings )
{
    m_seedSleepFrameCount = static_cast<uint32_t>( (std::max)( 0, settings.frames ) );
}

PhysicsSleepStepPolicy PhysicsSleepController::ResolveStepPolicy( const SleepSettings& settings ) const
{
    // Why: sleep eligibility and wake-energy thresholds are sleep-domain
    // policy. PhysicsWorld sequences the resulting value without re-deciding it.
    const float linearSpeed = (std::max)( 0.0f, settings.linearSpeed );
    const float angularSpeed = (std::max)( 0.0f, settings.angularSpeed );
    return PhysicsSleepStepPolicy { linearSpeed * linearSpeed, angularSpeed * angularSpeed,
                                    static_cast<uint32_t>( (std::max)( 1, settings.frames ) ) };
}

PhysicsSleepStepPolicy PhysicsSleepController::ResolveStepPolicy( const PhysicsRuntimeSettings& settings ) const
{
    PhysicsSleepStepPolicy policy = ResolveStepPolicy( settings.sleep );
    const float contactSkin = (std::max)( 0.0f, settings.body.contactEpsilon );
    policy.objectPenetrationLimit = contactSkin + (std::max)( 0.0f, settings.solver.slop );
    policy.terrainPenetrationLimit = contactSkin + (std::max)( 0.0f, settings.terrain.slop );
    policy.correctionSpeedSquared = policy.linearSpeedSquared;
    policy.poseDriftLimit = (std::max)( policy.objectPenetrationLimit, policy.terrainPenetrationLimit );
    return policy;
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

void PhysicsSleepController::RegisterBoxContactPartner( int bodyIndex, int partnerIndex )
{
    PhysicsSleepScratchFlags& flags = m_sleepScratchFlags[static_cast<std::size_t>( bodyIndex )];
    int& firstPartner = m_sleepFirstBoxContactPartner[static_cast<std::size_t>( bodyIndex )];

    if ( firstPartner == ( std::numeric_limits<int>::min )() )
    {
        firstPartner = partnerIndex;
    }
    else if ( firstPartner != partnerIndex )
    {
        flags.boxHasSecondContact = 1u;
    }
}

void PhysicsSleepController::RegisterBoxSupportContact( int bodyIndex, int partnerIndex, bool facePatch )
{
    PhysicsSleepScratchFlags& flags = m_sleepScratchFlags[static_cast<std::size_t>( bodyIndex )];
    RegisterBoxContactPartner( bodyIndex, partnerIndex );

    if ( facePatch )
    {
        flags.boxHasFaceSupport = 1u;
        return;
    }

    flags.boxHasNarrowSupport = 1u;
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
    m_simulationIslands.Invalidate();
    m_awakeListNeedsRebuild = true;
    m_resetDenseSleepHistoryForBodyTopologyChange = true;
}

void PhysicsSleepController::QueueConstraintTopologyWake( PhysicsBodyHandle bodyA, PhysicsBodyHandle bodyB )
{
    const auto appendUnique = [&]( PhysicsBodyHandle body )
    {
        if ( !body.IsValid() )
        {
            return;
        }

        for ( int queuedIndex = 0; queuedIndex < m_pendingConstraintWakeBodyCount; ++queuedIndex )
        {
            if ( m_pendingConstraintWakeBodies[queuedIndex] == body )
            {
                return;
            }
        }

        if ( m_pendingConstraintWakeBodyCount >= Scene::Capacity::MAX_SCENE_OBJECTS )
        {
            SB_FATAL( "Physics/PhysicsSleepController",
                      "Constraint topology wake body capacity exceeded: requested=%d capacity=%d.",
                      m_pendingConstraintWakeBodyCount + 1, Scene::Capacity::MAX_SCENE_OBJECTS );
        }

        m_pendingConstraintWakeBodies[m_pendingConstraintWakeBodyCount++] = body;
    };

    // Hazard: constraint authoring may occur while both endpoint islands are
    // dormant. Retain stable handles until the next body-store mirror so only
    // the old and new endpoint islands wake before solver pruning.
    appendUnique( bodyA );
    appendUnique( bodyB );
    m_awakeListNeedsRebuild = true;
}

void PhysicsSleepController::ApplyPendingConstraintTopologyWakes( PhysicsBodyStore& bodyStore, int modelCount )
{
    if ( m_pendingConstraintWakeBodyCount == 0 )
    {
        return;
    }

    const auto findRetainedRoot = [&]( int bodyIndex )
    {
        int root = bodyIndex;

        for ( int hop = 0; hop < modelCount; ++hop )
        {
            if ( root < 0 || root >= static_cast<int>( m_sleepIslandParent.size() ) )
            {
                return bodyIndex;
            }

            const int parent = m_sleepIslandParent[static_cast<std::size_t>( root )];
            if ( parent == root )
            {
                return root;
            }
            root = parent;
        }
        return bodyIndex;
    };

    m_restingWakeQueueScratch.clear();
    for ( int queuedIndex = 0; queuedIndex < m_pendingConstraintWakeBodyCount; ++queuedIndex )
    {
        const int bodyIndex = bodyStore.ModelIndexForHandle( m_pendingConstraintWakeBodies[queuedIndex] );
        if ( bodyIndex < 0 || bodyIndex >= modelCount )
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
        const int root = findRetainedRoot( bodyIndex );
        if ( !std::binary_search( m_restingWakeQueueScratch.begin(), m_restingWakeQueueScratch.end(), root ) )
        {
            continue;
        }

        m_sleepState[bodyIndex] = 0u;
        m_sleepCounter[bodyIndex] = 0u;
        if ( bodyIndex < static_cast<int>( m_sleepPoseAnchors.size() ) )
        {
            m_sleepPoseAnchors[bodyIndex].flags &= static_cast<uint8_t>( ~SLEEP_POSE_ANCHOR_VALID_BIT );
        }
        m_underwaterSleepLocked[bodyIndex] = 0u;
        m_sleepIslandVisualId[bodyIndex] = 0;
    }

    m_pendingConstraintWakeBodyCount = 0;
}

bool PhysicsSleepController::MirrorFlagsFrom( PhysicsBodyStore& bodyStore, int modelCount )
{
    const PhysicsBodyHotFieldsConstView hotFields = bodyStore.HotFields();
    const bool rebuildAwakeList = m_awakeListNeedsRebuild || static_cast<int>( m_awakeListPositions.size() ) != modelCount;

    m_sleepSupportedThisFrame.assign( modelCount, 0 );
    m_sleepInhibitedThisFrame.assign( modelCount, 0 );
    m_sleepSupportEdges.clear();

    if ( static_cast<int>( m_sleepState.size() ) != modelCount )
    {
        m_sleepState.assign( modelCount, 0 );
        m_sleepCounter.assign( modelCount, 0 );
    }

    if ( static_cast<int>( m_sleepPoseAnchors.size() ) != modelCount )
    {
        m_sleepPoseAnchors.assign( modelCount, PhysicsSleepPoseAnchor {} );
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

    if ( m_resetDenseSleepHistoryForBodyTopologyChange )
    {
        // Invariant: dense-row compaction can move an unrelated survivor into
        // a removed row. The body store has already preserved sleep by stable
        // handle, while index-keyed timers, parents, and diagnostic ids must reset.
        std::fill( m_sleepCounter.begin(), m_sleepCounter.end(), uint32_t { 0u } );
        for ( PhysicsSleepPoseAnchor& anchor : m_sleepPoseAnchors )
        {
            anchor.flags = 0u;
        }
        std::fill( m_sleepIslandVisualId.begin(), m_sleepIslandVisualId.end(), 0 );
        m_jointWakeParent.clear();
        m_jointWakeRank.clear();
        m_sleepIslandParent.clear();
        m_sleepIslandRank.clear();
        m_resetDenseSleepHistoryForBodyTopologyChange = false;
    }

    ApplyPendingConstraintTopologyWakes( bodyStore, modelCount );

    if ( !m_sleepEnabled )
    {
        std::fill( m_sleepState.begin(), m_sleepState.end(), static_cast<uint8_t>( 0 ) );
        std::fill( m_sleepCounter.begin(), m_sleepCounter.end(), uint32_t { 0u } );
        for ( PhysicsSleepPoseAnchor& anchor : m_sleepPoseAnchors )
        {
            anchor.flags = 0u;
        }
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
            const bool expectedAwake = hotFields.fixed[static_cast<std::size_t>( index )] == 0u && m_sleepState[index] == 0u;

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
    m_sleepSupportPropagation.PropagateSupport( context, bodyStore.HotFields() );
}

void PhysicsSleepController::AppendPointJointSupportEdges( const PhysicsBodyStore& bodyStore,
                                                           std::span<const PointJointConstraint> pointJointConstraints,
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
    PhysicsBodyStore& bodyStore, const ColliderStore& colliderStore, PhysicsTerrainView terrain,
    const PhysicsWorldForces& worldForces, std::span<BuoyancyBodyFacts> buoyancyFacts, std::span<float> timeRemaining,
    PhysicsContactCacheWakeAccess contactCache, std::span<const PointJointConstraint> pointJointConstraints, float dt )
{
    if ( pointJointConstraints.empty() || m_sleepState.empty() )
    {
        return;
    }

    const auto bodyRecords = bodyStore.Records();
    const PhysicsBodyHotFieldsView hotFields = bodyStore.MutableHotFields();
    const PhysicsBodyHotFieldsConstView hotRead = ConstPhysicsBodyHotFields( hotFields );
    const int modelCount = (std::min)( bodyStore.Count(), static_cast<int>( bodyRecords.size() ) );
    m_jointWakeParent.assign( modelCount, 0 );
    m_jointWakeRank.assign( modelCount, 0 );
    EnsureScratchFlagsSize( modelCount );

    for ( PhysicsSleepScratchFlags& flags : m_sleepScratchFlags )
    {
        flags.pointJointBody = 0u;
    }

    m_sleepIslandHasAwake.assign( modelCount, 0 );
    m_sleepIslandCanSleep.assign( modelCount, 0 );

    for ( int i = 0; i < modelCount; ++i )
    {
        m_jointWakeParent[i] = i;
    }

    DisjointSet sleepIslands( m_jointWakeParent, m_jointWakeRank, modelCount );

    for ( const PointJointConstraint& constraint : pointJointConstraints )
    {
        const int a = constraint.BodyAIndex( bodyStore );
        const int b = constraint.BodyBIndex( bodyStore );

        if ( a < 0 || b < 0 || a == b || a >= modelCount || b >= modelCount ||
             a >= static_cast<int>( m_sleepState.size() ) || b >= static_cast<int>( m_sleepState.size() ) )
        {
            continue;
        }

        const bool dynamicA = !IsSolverBodyFixed( hotRead, a );
        const bool dynamicB = !IsSolverBodyFixed( hotRead, b );

        if ( dynamicA )
        {
            m_sleepScratchFlags[a].pointJointBody = 1u;
        }
        if ( dynamicB )
        {
            m_sleepScratchFlags[b].pointJointBody = 1u;
        }

        // Invariant: fixed endpoints anchor their own constraint but cannot
        // bridge two dynamic wake components through the static world.
        if ( dynamicA && dynamicB )
        {
            sleepIslands.Unite( a, b );
        }
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

    for ( int i = 0; i < modelCount; ++i )
    {
        if ( m_sleepScratchFlags[i].pointJointBody == 0u || IsSolverBodyFixed( hotRead, i ) || m_sleepState[i] == 0 )
        {
            continue;
        }

        const int root = sleepIslands.Find( i );

        if ( m_sleepIslandHasAwake[root] != 0 && m_sleepIslandCanSleep[root] != 0 )
        {
            WakeDynamicBodyStateWithForces( bodyStore, colliderStore, terrain, worldForces, buoyancyFacts, timeRemaining,
                                            contactCache, i, dt );
        }
    }
}

void PhysicsSleepController::PrepareIslandScratch( const ColliderStore& colliderStore, int modelCount )
{
    const std::span<const ColliderRecord> colliderRecords = colliderStore.Records();
    m_sleepIslandParent.assign( modelCount, 0 );
    m_sleepIslandRank.assign( modelCount, 0 );
    m_sleepIslandHasAwake.assign( modelCount, 0 );
    m_sleepIslandHasSupportAnchor.assign( modelCount, 0 );
    m_sleepIslandEligible.assign( modelCount, 1 );
    m_sleepIslandTopologyStable.assign( modelCount, 1 );
    m_sleepIslandCanSleep.assign( modelCount, 1 );
    m_sleepBodyEligible.assign( modelCount, 1 );
    m_sleepResetReason.assign( modelCount, static_cast<uint8_t>( PhysicsSleepResetReason::None ) );
    EnsureScratchFlagsSize( modelCount );
    m_sleepFirstBoxContactPartner.assign( modelCount, ( std::numeric_limits<int>::min )() );

    if ( static_cast<int>( m_sleepPoseAnchors.size() ) != modelCount )
    {
        m_sleepPoseAnchors.assign( modelCount, PhysicsSleepPoseAnchor {} );
    }

    for ( PhysicsSleepScratchFlags& flags : m_sleepScratchFlags )
    {
        flags = PhysicsSleepScratchFlags {};
        flags.islandPointJointsRelaxed = 1u;
    }
    for ( int bodyIndex = 0; bodyIndex < modelCount && bodyIndex < static_cast<int>( colliderRecords.size() ); ++bodyIndex )
    {
        m_sleepScratchFlags[static_cast<std::size_t>( bodyIndex )]
            .boxBody = colliderRecords[static_cast<std::size_t>( bodyIndex )].shapeKind == ColliderShapeKind::Box ? 1u : 0u;
    }
    for ( int bodyIndex = 0; bodyIndex < modelCount; ++bodyIndex )
    {
        m_sleepIslandParent[bodyIndex] = bodyIndex;
    }
}

void PhysicsSleepController::BuildSimulationIslandTopology( const PhysicsBodyStore& bodyStore,
                                                            std::span<const PersistentContact> persistentContacts,
                                                            std::span<const PointJointConstraint> pointJointConstraints,
                                                            const PhysicsBodyHotFieldsConstView& hotFields,
                                                            DisjointSet& sleepIslands )
{
    m_simulationIslands.Rebuild( bodyStore, persistentContacts, pointJointConstraints, m_sleepState );
    for ( const auto& edge : m_simulationIslands.ActiveContactEdges() )
    {
        if ( edge.second >= 0 && !IsSolverBodyFixed( hotFields, edge.first ) &&
             !IsSolverBodyFixed( hotFields, edge.second ) )
        {
            sleepIslands.Unite( edge.first, edge.second );
        }
    }
    for ( const SimulationIslandJointEdge& edge : m_simulationIslands.ActiveJointEdges() )
    {
        if ( !IsSolverBodyFixed( hotFields, edge.bodyA ) && !IsSolverBodyFixed( hotFields, edge.bodyB ) )
        {
            sleepIslands.Unite( edge.bodyA, edge.bodyB );
        }
    }
}

void PhysicsSleepController::ClassifyContactStability( const ColliderStore& colliderStore,
                                                       const PhysicsWorldForces& worldForces,
                                                       std::span<const PersistentContact> persistentContacts,
                                                       const PhysicsSleepStepPolicy& sleepPolicy, int modelCount )
{
    const std::span<const ColliderRecord> colliderRecords = colliderStore.Records();
    for ( const PersistentContact& contact : persistentContacts )
    {
        const float penetrationLimit = contact.isTerrain ? sleepPolicy.terrainPenetrationLimit
                                                         : sleepPolicy.objectPenetrationLimit;
        const float correctionSpeedSquared = contact.separationBias * contact.separationBias;
        const bool finite = std::isfinite( contact.penetration ) && std::isfinite( contact.separationBias ) &&
                            std::isfinite( contact.preSolveClosingSpeed ) && std::isfinite( contact.preSolveSlipSpeed ) &&
                            std::isfinite( contact.accN ) && std::isfinite( contact.accT1 ) &&
                            std::isfinite( contact.accT2 );
        const bool stable = finite && contact.penetration <= penetrationLimit &&
                            correctionSpeedSquared < sleepPolicy.correctionSpeedSquared;

        if ( contact.isTerrain && contact.bodyA >= 0 && contact.bodyA < modelCount &&
             contact.bodyA < static_cast<int>( colliderRecords.size() ) && contact.inhibitsSleep &&
             contact.supportsRestingPolicy &&
             colliderRecords[static_cast<std::size_t>( contact.bodyA )].shapeKind == ColliderShapeKind::Sphere )
        {
            m_sleepScratchFlags[static_cast<std::size_t>( contact.bodyA )].steepSphereTerrain = 1u;
        }

        if ( stable )
        {
            if ( contact.bodyA >= 0 && contact.bodyA < modelCount &&
                 m_sleepScratchFlags[static_cast<std::size_t>( contact.bodyA )].boxBody != 0u )
            {
                RegisterBoxContactPartner( contact.bodyA, contact.bodyB );
            }
            if ( !contact.isTerrain && contact.bodyB >= 0 && contact.bodyB < modelCount &&
                 m_sleepScratchFlags[static_cast<std::size_t>( contact.bodyB )].boxBody != 0u )
            {
                RegisterBoxContactPartner( contact.bodyB, contact.bodyA );
            }

            int supportedBody = -1;
            int supportPartner = -1;
            if ( contact.isTerrain )
            {
                supportedBody = contact.bodyA;
            }
            else
            {
                const float gravityUpY = worldForces.gravity > 0.0f ? -1.0f : 1.0f;
                const float supportDirection = contact.normal.y * gravityUpY;
                if ( supportDirection > 0.25f )
                {
                    supportedBody = contact.bodyB;
                    supportPartner = contact.bodyA;
                }
                else if ( supportDirection < -0.25f )
                {
                    supportedBody = contact.bodyA;
                    supportPartner = contact.bodyB;
                }
            }
            if ( supportedBody >= 0 && supportedBody < modelCount &&
                 m_sleepScratchFlags[static_cast<std::size_t>( supportedBody )].boxBody != 0u )
            {
                RegisterBoxSupportContact( supportedBody, supportPartner, !contact.inhibitsSleep );
            }
        }
        else
        {
            if ( contact.bodyA >= 0 && contact.bodyA < modelCount )
            {
                m_sleepBodyEligible[contact.bodyA] = 0u;
                m_sleepResetReason[contact.bodyA] = static_cast<uint8_t>( PhysicsSleepResetReason::ContactStability );
            }
            if ( contact.bodyB >= 0 && contact.bodyB < modelCount )
            {
                m_sleepBodyEligible[contact.bodyB] = 0u;
                m_sleepResetReason[contact.bodyB] = static_cast<uint8_t>( PhysicsSleepResetReason::ContactStability );
            }
        }
    }
}

void PhysicsSleepController::ClassifyPointJointStability( const PhysicsBodyStore& bodyStore,
                                                          std::span<const PointJointConstraint> pointJointConstraints,
                                                          const PhysicsBodyHotFieldsConstView& hotFields,
                                                          DisjointSet& sleepIslands, int modelCount )
{
    for ( const PointJointConstraint& constraint : pointJointConstraints )
    {
        const int a = constraint.BodyAIndex( bodyStore );
        const int b = constraint.BodyBIndex( bodyStore );
        if ( a < 0 || b < 0 || a == b || a >= modelCount || b >= modelCount )
        {
            continue;
        }

        const bool fixedA = IsSolverBodyFixed( hotFields, a );
        const bool fixedB = IsSolverBodyFixed( hotFields, b );
        if ( !fixedA )
        {
            m_sleepScratchFlags[a].pointJointBody = 1u;
            if ( fixedB )
            {
                m_sleepIslandHasSupportAnchor[sleepIslands.Find( a )] = 1u;
            }
        }
        if ( !fixedB )
        {
            m_sleepScratchFlags[b].pointJointBody = 1u;
            if ( fixedA )
            {
                m_sleepIslandHasSupportAnchor[sleepIslands.Find( b )] = 1u;
            }
        }
    }

    const std::span<const uint8_t> topologyChangedBodies = m_simulationIslands.TopologyChangedBodies();
    for ( int bodyIndex = 0; bodyIndex < modelCount; ++bodyIndex )
    {
        const int root = sleepIslands.Find( bodyIndex );
        if ( topologyChangedBodies[bodyIndex] != 0u )
        {
            m_sleepIslandTopologyStable[root] = 0u;
        }
        if ( IsSolverBodyFixed( hotFields, bodyIndex ) ||
             ( bodyIndex < static_cast<int>( m_sleepState.size() ) && m_sleepState[bodyIndex] != 0 ) ||
             ( bodyIndex < static_cast<int>( m_sleepSupportedThisFrame.size() ) &&
               m_sleepSupportedThisFrame[bodyIndex] != 0 ) )
        {
            m_sleepIslandHasSupportAnchor[root] = 1u;
        }
        if ( m_sleepScratchFlags[bodyIndex].pointJointBody != 0u )
        {
            m_sleepScratchFlags[root].islandHasPointJoint = 1u;
        }
    }

    for ( const PointJointConstraint& constraint : pointJointConstraints )
    {
        const int a = constraint.BodyAIndex( bodyStore );
        const int b = constraint.BodyBIndex( bodyStore );
        if ( a < 0 || b < 0 || a == b || a >= modelCount || b >= modelCount )
        {
            continue;
        }

        const auto rotA = PhysicsBodyOrientation( hotFields, static_cast<std::size_t>( a ) ).GetOrientationMatrix();
        const auto rotB = PhysicsBodyOrientation( hotFields, static_cast<std::size_t>( b ) ).GetOrientationMatrix();
        const Vector3 anchorA = PhysicsBodyPosition( hotFields, static_cast<std::size_t>( a ) ) +
                                rotA * constraint.localAnchorA;
        const Vector3 anchorB = PhysicsBodyPosition( hotFields, static_cast<std::size_t>( b ) ) +
                                rotB * constraint.localAnchorB;
        const float distance = Vector::VectorMag( anchorB - anchorA );
        const float allowedDistance = constraint.slack +
                                      (std::max)( POINT_JOINT_SLEEP_MIN_ERROR_TOLERANCE,
                                                  constraint.slack * POINT_JOINT_SLEEP_SLACK_TOLERANCE_SCALE );
        if ( distance <= allowedDistance )
        {
            continue;
        }

        if ( !IsSolverBodyFixed( hotFields, a ) )
        {
            m_sleepScratchFlags[sleepIslands.Find( a )].islandPointJointsRelaxed = 0u;
        }
        if ( !IsSolverBodyFixed( hotFields, b ) )
        {
            m_sleepScratchFlags[sleepIslands.Find( b )].islandPointJointsRelaxed = 0u;
        }
    }
}

template <bool RetainPipelineRecords>
void PhysicsSleepController::EvaluateAwakeBodyEligibility(
    const PhysicsBodyStore& bodyStore, const ColliderStore& colliderStore, const PhysicsWorldForces& worldForces,
    std::span<const uint16_t> persistentRestingContactCounts, PhysicsPipelineTraceRecorder& physicsPipelineTrace,
    const PhysicsSleepStepPolicy& sleepPolicy, DisjointSet& sleepIslands )
{
    const PhysicsBodyHotFieldsConstView hotFields = bodyStore.HotFields();
    const std::span<const ColliderRecord> colliderRecords = colliderStore.Records();
    const std::span<const int> awakeBodyIndices = GetAwakeBodyIndices();

    if constexpr ( !RetainPipelineRecords )
    {
        physicsPipelineTrace.RecordEvents( awakeBodyIndices.size() );
    }

    for ( int bodyIndex : awakeBodyIndices )
    {
#if defined( _DEBUG )
        assert( IsAwakeListEntryConsistent( IsSolverBodyFixed( hotFields, bodyIndex ), m_sleepState[bodyIndex] != 0u ) );
#endif
        const int root = sleepIslands.Find( bodyIndex );
        m_sleepIslandHasAwake[root] = 1u;
        const Vector3 velocity = PhysicsBodyLinearVelocity( hotFields, static_cast<std::size_t>( bodyIndex ) );
        const Vector3 angularVelocity = PhysicsBodyAngularVelocity( hotFields, static_cast<std::size_t>( bodyIndex ) );
        const float speedSquared = velocity.x * velocity.x + velocity.y * velocity.y + velocity.z * velocity.z;
        const float angularSpeedSquared = angularVelocity.x * angularVelocity.x + angularVelocity.y * angularVelocity.y +
                                          angularVelocity.z * angularVelocity.z;
        bool supported = bodyIndex < static_cast<int>( m_sleepSupportedThisFrame.size() ) &&
                         m_sleepSupportedThisFrame[bodyIndex] != 0u;
        const bool hasRestingObjectContact = bodyIndex < static_cast<int>( persistentRestingContactCounts.size() ) &&
                                             persistentRestingContactCounts[bodyIndex] > 0u;
        const bool islandHasSupportAnchor = m_sleepIslandHasSupportAnchor[root] != 0u;
        const bool pointJointMember = bodyIndex < static_cast<int>( m_sleepScratchFlags.size() ) &&
                                      m_sleepScratchFlags[bodyIndex].pointJointBody != 0u;
        const bool pointJointIsland = m_sleepScratchFlags[root].islandHasPointJoint != 0u;
        const bool quiet = sleepPolicy.IsQuiet( speedSquared, angularSpeedSquared );
        const bool pointJointAnchoredSupport = quiet && pointJointMember && pointJointIsland && islandHasSupportAnchor;

        if ( !supported && quiet && hasRestingObjectContact && islandHasSupportAnchor )
        {
            m_sleepSupportedThisFrame[bodyIndex] = 1u;
            supported = true;
        }
        if ( !supported && pointJointAnchoredSupport )
        {
            m_sleepSupportedThisFrame[bodyIndex] = 1u;
            supported = true;
        }

        const PhysicsSleepScratchFlags& bodyFlags = m_sleepScratchFlags[static_cast<std::size_t>( bodyIndex )];
        const bool boxSupportEligible = bodyFlags.boxBody == 0u || bodyFlags.boxHasFaceSupport != 0u ||
                                        ( bodyFlags.boxHasNarrowSupport != 0u && bodyFlags.boxHasSecondContact != 0u ) ||
                                        pointJointAnchoredSupport;
        const bool unsupportedBoxSupport = bodyFlags.boxBody != 0u && !boxSupportEligible;
        const bool steepSphereSlope = bodyFlags.steepSphereTerrain != 0u;
        const bool terrainInhibitBlocksSleep = steepSphereSlope ||
                                               ( m_sleepInhibitedThisFrame[bodyIndex] != 0u && !pointJointAnchoredSupport &&
                                                 !( bodyFlags.boxBody != 0u && boxSupportEligible ) );
        const bool unsupportedInGravity = fabsf( worldForces.gravity ) > TOLERANCE && !supported &&
                                          !pointJointAnchoredSupport;
        const bool pointJointErrorBlocksSleep = pointJointMember && root < static_cast<int>( m_sleepScratchFlags.size() ) &&
                                                m_sleepScratchFlags[root].islandPointJointsRelaxed == 0u;

        const std::size_t bodyRow = static_cast<std::size_t>( bodyIndex );
        const Vector3 position = PhysicsBodyPosition( hotFields, bodyRow );
        const auto orientation = PhysicsBodyOrientation( hotFields, bodyRow );
        std::array<float, 4> orientationComponents = {};
        orientation.GetComponents( orientationComponents[0], orientationComponents[1], orientationComponents[2],
                                   orientationComponents[3] );

        bool poseStable = true;
        if ( ( m_sleepPoseAnchors[bodyRow].flags & SLEEP_POSE_ANCHOR_VALID_BIT ) == 0u || m_sleepCounter[bodyRow] == 0u )
        {
            m_sleepPoseAnchors[bodyRow].position = position;
            m_sleepPoseAnchors[bodyRow].orientation = orientationComponents;
            m_sleepPoseAnchors[bodyRow].flags |= SLEEP_POSE_ANCHOR_VALID_BIT;
        }
        else
        {
            const Vector3 translation = position - m_sleepPoseAnchors[bodyRow].position;
            const std::array<float, 4>& anchorOrientation = m_sleepPoseAnchors[bodyRow].orientation;
            const float orientationDot = std::clamp( fabsf( anchorOrientation[0] * orientationComponents[0] +
                                                            anchorOrientation[1] * orientationComponents[1] +
                                                            anchorOrientation[2] * orientationComponents[2] +
                                                            anchorOrientation[3] * orientationComponents[3] ),
                                                     0.0f, 1.0f );
            const float maximumRadius = bodyRow < colliderRecords.size() ? colliderRecords[bodyRow].maximumCenterOfMassRadius
                                                                         : hotFields.boundingRadius[bodyRow];
            const float rotationalDrift = 2.0f * (std::max)( maximumRadius, 0.0f ) *
                                          sqrtf( (std::max)( 0.0f, 1.0f - orientationDot * orientationDot ) );
            const float poseDrift = Vector::VectorMag( translation ) + rotationalDrift;
            poseStable = std::isfinite( poseDrift ) && poseDrift <= sleepPolicy.poseDriftLimit;
        }

        const bool bodyEligible = quiet && !terrainInhibitBlocksSleep && !unsupportedBoxSupport && !unsupportedInGravity &&
                                  !pointJointErrorBlocksSleep && poseStable && m_sleepBodyEligible[bodyIndex] != 0u;
        const bool holdBoxDeactivation = quiet && unsupportedBoxSupport && !terrainInhibitBlocksSleep &&
                                         !unsupportedInGravity && !pointJointErrorBlocksSleep && poseStable &&
                                         m_sleepBodyEligible[bodyIndex] != 0u;
        m_sleepBodyEligible[bodyIndex] = bodyEligible ? 1u : 0u;

        if ( !quiet )
        {
            m_sleepResetReason[bodyIndex] = static_cast<uint8_t>( PhysicsSleepResetReason::Motion );
        }
        else if ( m_sleepBodyEligible[bodyIndex] == 0u &&
                  m_sleepResetReason[bodyIndex] == static_cast<uint8_t>( PhysicsSleepResetReason::ContactStability ) )
        {
            // Preserve the contact-row reason established by the contact phase.
        }
        else if ( steepSphereSlope )
        {
            m_sleepResetReason[bodyIndex] = static_cast<uint8_t>( PhysicsSleepResetReason::SteepSphereSlope );
        }
        else if ( unsupportedBoxSupport )
        {
            m_sleepResetReason[bodyIndex] = static_cast<uint8_t>( PhysicsSleepResetReason::UnsupportedBoxSupport );
        }
        else if ( terrainInhibitBlocksSleep || unsupportedInGravity )
        {
            m_sleepResetReason[bodyIndex] = static_cast<uint8_t>( PhysicsSleepResetReason::TerrainInhibition );
        }
        else if ( pointJointErrorBlocksSleep )
        {
            m_sleepResetReason[bodyIndex] = static_cast<uint8_t>( PhysicsSleepResetReason::PointJointError );
        }
        else if ( !poseStable )
        {
            m_sleepResetReason[bodyIndex] = static_cast<uint8_t>( PhysicsSleepResetReason::PoseDrift );
        }

        if ( !bodyEligible )
        {
            m_sleepIslandEligible[root] = 0u;
            if ( !holdBoxDeactivation )
            {
                m_sleepPoseAnchors[bodyRow].flags &= static_cast<uint8_t>( ~SLEEP_POSE_ANCHOR_VALID_BIT );
            }
        }

        if constexpr ( RetainPipelineRecords )
        {
            PhysicsPipelineRecord record;
            record.stage = PhysicsPipelineStage::SleepIslandDecision;
            record.bodyA = bodyIndex;
            record.bodyB = root;
            record.point = position;
            record.scalarA = bodyEligible ? 1.0f : 0.0f;
            record.scalarB = supported ? 1.0f : 0.0f;
            record.scalarC = terrainInhibitBlocksSleep ? 1.0f : ( pointJointErrorBlocksSleep ? 3.0f : 0.0f );
            physicsPipelineTrace.Record( record );
        }
    }
}

template <bool RetainPipelineRecords>
void PhysicsSleepController::RunIslandStageMode( PhysicsBodyStore& bodyStore, const ColliderStore& colliderStore,
                                                 const PhysicsWorldForces& worldForces,
                                                 std::span<BuoyancyBodyFacts> buoyancyFacts, std::span<float> timeRemaining,
                                                 std::span<const PersistentContact> persistentContacts,
                                                 std::span<const uint16_t> persistentRestingContactCounts,
                                                 std::span<const PointJointConstraint> pointJointConstraints,
                                                 PhysicsPipelineTraceRecorder& physicsPipelineTrace,
                                                 const PhysicsSleepStepPolicy& sleepPolicy )
{
    // Invariant: canonical dynamic contacts and joints define membership.
    // Visual ids remain diagnostic output and never become topology authority.
    const int modelCount = bodyStore.Count();
    PrepareIslandScratch( colliderStore, modelCount );

    DisjointSet sleepIslands( m_sleepIslandParent, m_sleepIslandRank, modelCount );
    const PhysicsBodyHotFieldsConstView hotFields = bodyStore.HotFields();
    BuildSimulationIslandTopology( bodyStore, persistentContacts, pointJointConstraints, hotFields, sleepIslands );
    ClassifyContactStability( colliderStore, worldForces, persistentContacts, sleepPolicy, modelCount );
    ClassifyPointJointStability( bodyStore, pointJointConstraints, hotFields, sleepIslands, modelCount );
    EvaluateAwakeBodyEligibility<RetainPipelineRecords>( bodyStore, colliderStore, worldForces,
                                                         persistentRestingContactCounts, physicsPipelineTrace, sleepPolicy,
                                                         sleepIslands );

    if ( !m_sleepEnabled )
    {
        std::fill( m_sleepCounter.begin(), m_sleepCounter.end(), uint32_t { 0u } );
        m_sleepIslandCanSleep.assign( modelCount, 0 );
        m_sleepIslandAssignedVisualId.assign( modelCount, 0 );
        m_awakeBodyCount = static_cast<int>( m_awakeBodyIndices.size() );
        return;
    }

    ApplyTransitionsMode<RetainPipelineRecords>( bodyStore, colliderStore, worldForces, buoyancyFacts, timeRemaining,
                                                 physicsPipelineTrace, sleepPolicy, sleepIslands );
}

template <bool RetainPipelineRecords>
void PhysicsSleepController::ApplyTransitionsMode( PhysicsBodyStore& bodyStore, const ColliderStore& colliderStore,
                                                   const PhysicsWorldForces& worldForces,
                                                   std::span<BuoyancyBodyFacts> buoyancyFacts,
                                                   std::span<float> timeRemaining,
                                                   PhysicsPipelineTraceRecorder& physicsPipelineTrace,
                                                   const PhysicsSleepStepPolicy& sleepPolicy, DisjointSet& sleepIslands )
{
    // Invariant: each body advances its own deactivation clock, but transition
    // authority remains island-wide. A noisy member cannot erase a stable
    // neighbor's history, and the neighbor still cannot sleep alone while the
    // active constraint graph connects them.
    const int modelCount = bodyStore.Count();

    const PhysicsBodyHotFieldsView hotFields = bodyStore.MutableHotFields();
    const std::span<const int> awakeBodyIndices = GetAwakeBodyIndices();

    for ( int x : awakeBodyIndices )
    {
        const int root = sleepIslands.Find( x );

        if ( m_sleepIslandHasAwake[root] && m_sleepBodyEligible[x] != 0u )
        {
            if ( sleepPolicy.NeedsMoreQuietFrames( m_sleepCounter[x] ) )
            {
                ++m_sleepCounter[x];
            }
        }
        else if ( m_sleepResetReason[x] != static_cast<uint8_t>( PhysicsSleepResetReason::UnsupportedBoxSupport ) )
        {
            m_sleepCounter[x] = 0;
        }
    }

    for ( int x : awakeBodyIndices )
    {
        const int root = sleepIslands.Find( x );

        if ( m_sleepBodyEligible[x] == 0u || sleepPolicy.NeedsMoreQuietFrames( m_sleepCounter[x] ) )
        {
            m_sleepIslandCanSleep[root] = 0;
        }
    }

    m_sleepIslandAssignedVisualId.assign( modelCount, 0 );

    for ( int x = 0; x < modelCount; ++x )
    {
        if ( IsSolverBodyFixed( ConstPhysicsBodyHotFields( hotFields ), x ) || !m_sleepState[x] ||
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

    std::size_t countOnlyTransitionEvents = 0;

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

            if constexpr ( RetainPipelineRecords )
            {
                PhysicsPipelineRecord record;
                record.stage = PhysicsPipelineStage::SleepIslandDecision;
                record.bodyA = x;
                record.bodyB = root;
                record.point = PhysicsBodyPosition( ConstPhysicsBodyHotFields( hotFields ), static_cast<size_t>( x ) );

                record.scalarA = 1.0f;
                record.scalarB = static_cast<float>( m_sleepIslandAssignedVisualId[root] );
                record.scalarC = static_cast<float>( m_sleepCounter[x] );
                physicsPipelineTrace.Record( record );
            }
            else
            {
                ++countOnlyTransitionEvents;
            }

            const size_t bodyIndex = static_cast<size_t>( x );
            hotFields.linearVelocityX[bodyIndex] = 0.0f;
            hotFields.linearVelocityY[bodyIndex] = 0.0f;
            hotFields.linearVelocityZ[bodyIndex] = 0.0f;
            hotFields.angularVelocityX[bodyIndex] = 0.0f;
            hotFields.angularVelocityY[bodyIndex] = 0.0f;
            hotFields.angularVelocityZ[bodyIndex] = 0.0f;
            hotFields.awake[bodyIndex] = 0u;
            LockUnderwaterSleeperIfReady( worldForces, bodyStore, colliderStore, buoyancyFacts, timeRemaining, x );

            continue;
        }

        ++awakeSlot;
    }

    if constexpr ( !RetainPipelineRecords )
    {
        physicsPipelineTrace.RecordEvents( countOnlyTransitionEvents );
    }

    m_awakeBodyCount = static_cast<int>( m_awakeBodyIndices.size() );
}

void PhysicsSleepController::RunIslandStage( PhysicsBodyStore& bodyStore, const ColliderStore& colliderStore,
                                             const PhysicsWorldForces& worldForces,
                                             std::span<BuoyancyBodyFacts> buoyancyFacts, std::span<float> timeRemaining,
                                             std::span<const PersistentContact> persistentContacts,
                                             std::span<const uint16_t> persistentRestingContactCounts,
                                             std::span<const PointJointConstraint> pointJointConstraints,
                                             PhysicsPipelineTraceRecorder& physicsPipelineTrace,
                                             const PhysicsSleepStepPolicy& sleepPolicy )
{
    // Why: select once per step so count-only execution has no per-body
    // diagnostic branch or payload construction.
    if ( physicsPipelineTrace.RetainsFullRecords() )
    {
        RunIslandStageMode<true>( bodyStore, colliderStore, worldForces, buoyancyFacts, timeRemaining, persistentContacts,
                                  persistentRestingContactCounts, pointJointConstraints, physicsPipelineTrace, sleepPolicy );
    }
    else
    {
        RunIslandStageMode<false>( bodyStore, colliderStore, worldForces, buoyancyFacts, timeRemaining, persistentContacts,
                                   persistentRestingContactCounts, pointJointConstraints, physicsPipelineTrace,
                                   sleepPolicy );
    }
}
