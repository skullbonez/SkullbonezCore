/*
File: SkullbonezSource/Physics/Stages/PhysicsSleepController.State.cpp
Purpose:
  Implements sleep-owner replay transfer, bounded row views, and memory accounting.

Summary:
  This physical unit keeps the cohesive PhysicsSleepController owner below the
  stage-size review target without creating a second owner. Replay operations
  copy next-step state, rebuild derived indices, and reseed persistent island
  topology from restored solver rows; view methods expose synchronous borrows.

Glossary:
  Row view: Borrow of an owner-retained model-order buffer for one sequenced pass.

Invariants:
  - Replay capture and restore preserve every next-step sleep input in matching order.
  - Persistent island topology is reseeded from restored contact and joint rows
    before the next fixed step, so restore does not report false edge changes.
  - Mutable views never transfer capacity or lifetime ownership to consumers.
  - Memory accounting includes every construction-reserved sleep vector.
  - Replay restore never trusts a pre-restore dense awake index mapping.

Related:
  - SkullbonezSource/Physics/Stages/PhysicsSleepController.h
  - SkullbonezSource/Physics/Stages/PhysicsSleepController.cpp
  - SkullbonezSource/Physics/Stages/PhysicsSleepController.Wake.cpp
  - Agentic/Reference/engine-glossary.md
*/
#include "PhysicsSleepController.h"
#include "../DisjointSet.h"

using namespace SkullbonezCore::Physics;

namespace
{
template <typename T> uint64_t ListCapacityBytes( const T& values )
{
    return static_cast<uint64_t>( values.capacity() ) * static_cast<uint64_t>( sizeof( typename T::value_type ) );
}

template <typename Source, typename Destination> void CaptureList( const Source& source, Destination& destination )
{
    destination.clear();

    for ( const auto& value : source )
    {
        destination.push_back( value );
    }
}

template <typename Source, typename Destination> void RestoreList( const Source& source, Destination& destination )
{
    destination.Reserve( source.size() );
    destination.clear();

    for ( const auto& value : source )
    {
        destination.push_back( value );
    }
}
} // namespace

void PhysicsSleepController::CaptureReplayState( PhysicsSolverSnapshot& snapshot ) const
{
    CaptureList( m_sleepSupportedThisFrame, snapshot.sleepSupportedThisFrame );
    CaptureList( m_sleepInhibitedThisFrame, snapshot.sleepInhibitedThisFrame );
    CaptureList( m_sleepState, snapshot.sleepState );
    CaptureList( m_sleepCounter, snapshot.sleepCounter );
    snapshot.sleepPoseAnchorPosition.clear();
    snapshot.sleepPoseAnchorOrientation.clear();
    snapshot.sleepPoseAnchorValid.clear();
    for ( const PhysicsSleepPoseAnchor& anchor : m_sleepPoseAnchors )
    {
        snapshot.sleepPoseAnchorPosition.push_back( anchor.position );
        snapshot.sleepPoseAnchorOrientation.push_back( anchor.orientation );
        snapshot.sleepPoseAnchorValid.push_back( anchor.flags & SLEEP_POSE_ANCHOR_VALID_BIT );
    }
    CaptureList( m_underwaterSleepLocked, snapshot.underwaterSleepLocked );
    CaptureList( m_sleepIslandVisualId, snapshot.sleepIslandVisualId );
    CaptureList( m_sleepIslandAssignedVisualId, snapshot.sleepIslandAssignedVisualId );
    CaptureList( m_sleepSupportEdges, snapshot.sleepSupportEdges );
    CaptureList( m_sleepIslandParent, snapshot.sleepIslandParent );
    CaptureList( m_sleepIslandRank, snapshot.sleepIslandRank );
    CaptureList( m_sleepIslandHasAwake, snapshot.sleepIslandHasAwake );
    CaptureList( m_sleepIslandHasSupportAnchor, snapshot.sleepIslandHasSupportAnchor );
    CaptureList( m_sleepIslandEligible, snapshot.sleepIslandEligible );
    CaptureList( m_sleepIslandCanSleep, snapshot.sleepIslandCanSleep );
    snapshot.nextSleepIslandVisualId = m_nextSleepIslandVisualId;
    snapshot.sleepEnabled = m_sleepEnabled;
}

bool PhysicsSleepController::CanRestoreReplayState( const PhysicsSolverSnapshot& snapshot, int modelCount ) const noexcept
{
    if ( modelCount < 0 || snapshot.nextSleepIslandVisualId < 1 )
    {
        return false;
    }

    const std::size_t bodyRows = static_cast<std::size_t>( modelCount );
#define REQUIRE_SLEEP_BODY_ROWS( snapshotField, ownerField )                                                                \
    if ( snapshot.snapshotField.size() != bodyRows || snapshot.snapshotField.size() > ownerField.capacity() )               \
    {                                                                                                                       \
        return false;                                                                                                       \
    }

    REQUIRE_SLEEP_BODY_ROWS( sleepSupportedThisFrame, m_sleepSupportedThisFrame )
    REQUIRE_SLEEP_BODY_ROWS( sleepInhibitedThisFrame, m_sleepInhibitedThisFrame )
    REQUIRE_SLEEP_BODY_ROWS( sleepState, m_sleepState )
    REQUIRE_SLEEP_BODY_ROWS( sleepCounter, m_sleepCounter )
    REQUIRE_SLEEP_BODY_ROWS( sleepPoseAnchorPosition, m_sleepPoseAnchors )
    REQUIRE_SLEEP_BODY_ROWS( sleepPoseAnchorOrientation, m_sleepPoseAnchors )
    REQUIRE_SLEEP_BODY_ROWS( sleepPoseAnchorValid, m_sleepPoseAnchors )
    REQUIRE_SLEEP_BODY_ROWS( underwaterSleepLocked, m_underwaterSleepLocked )
    REQUIRE_SLEEP_BODY_ROWS( sleepIslandVisualId, m_sleepIslandVisualId )
    REQUIRE_SLEEP_BODY_ROWS( sleepIslandAssignedVisualId, m_sleepIslandAssignedVisualId )
    REQUIRE_SLEEP_BODY_ROWS( sleepIslandParent, m_sleepIslandParent )
    REQUIRE_SLEEP_BODY_ROWS( sleepIslandRank, m_sleepIslandRank )
    REQUIRE_SLEEP_BODY_ROWS( sleepIslandHasAwake, m_sleepIslandHasAwake )
    REQUIRE_SLEEP_BODY_ROWS( sleepIslandHasSupportAnchor, m_sleepIslandHasSupportAnchor )
    REQUIRE_SLEEP_BODY_ROWS( sleepIslandEligible, m_sleepIslandEligible )
    REQUIRE_SLEEP_BODY_ROWS( sleepIslandCanSleep, m_sleepIslandCanSleep )
#undef REQUIRE_SLEEP_BODY_ROWS

    if ( snapshot.sleepSupportEdges.size() > m_sleepSupportEdges.capacity() )
    {
        return false;
    }

    for ( const auto& edge : snapshot.sleepSupportEdges )
    {
        if ( edge.first < 0 || edge.first >= modelCount || edge.second < 0 || edge.second >= modelCount ||
             edge.first == edge.second )
        {
            return false;
        }
    }

    for ( int parent : snapshot.sleepIslandParent )
    {
        if ( parent < 0 || parent >= modelCount )
        {
            return false;
        }
    }

    for ( uint8_t flags : snapshot.sleepPoseAnchorValid )
    {
        if ( ( flags & static_cast<uint8_t>( ~SLEEP_POSE_ANCHOR_VALID_BIT ) ) != 0u )
        {
            return false;
        }
    }

    return true;
}

void PhysicsSleepController::RestoreReplayState( const PhysicsSolverSnapshot& snapshot )
{
    RestoreList( snapshot.sleepSupportedThisFrame, m_sleepSupportedThisFrame );
    RestoreList( snapshot.sleepInhibitedThisFrame, m_sleepInhibitedThisFrame );
    RestoreList( snapshot.sleepState, m_sleepState );
    RestoreList( snapshot.sleepCounter, m_sleepCounter );
    m_sleepPoseAnchors.Reserve( snapshot.sleepPoseAnchorPosition.size() );
    m_sleepPoseAnchors.clear();
    for ( std::size_t index = 0; index < snapshot.sleepPoseAnchorPosition.size(); ++index )
    {
        m_sleepPoseAnchors.push_back( PhysicsSleepPoseAnchor { snapshot.sleepPoseAnchorPosition[index],
                                                               snapshot.sleepPoseAnchorOrientation[index],
                                                               snapshot.sleepPoseAnchorValid[index] } );
    }
    RestoreList( snapshot.underwaterSleepLocked, m_underwaterSleepLocked );
    RestoreList( snapshot.sleepIslandVisualId, m_sleepIslandVisualId );
    RestoreList( snapshot.sleepIslandAssignedVisualId, m_sleepIslandAssignedVisualId );

    // Invariant: replay restore is a cold copy, but it still may not enlarge a
    // hot owner beyond the same scene-committed support-edge ceiling.
    ValidateSleepSupportEdgeCount( snapshot.sleepSupportEdges.size(), m_sleepSupportEdges.capacity(),
                                   m_sleepSupportEdges.size(), "replay_restore" );

    RestoreList( snapshot.sleepSupportEdges, m_sleepSupportEdges );
    RestoreList( snapshot.sleepIslandParent, m_sleepIslandParent );
    RestoreList( snapshot.sleepIslandRank, m_sleepIslandRank );
    RestoreList( snapshot.sleepIslandHasAwake, m_sleepIslandHasAwake );
    RestoreList( snapshot.sleepIslandHasSupportAnchor, m_sleepIslandHasSupportAnchor );
    RestoreList( snapshot.sleepIslandEligible, m_sleepIslandEligible );
    RestoreList( snapshot.sleepIslandCanSleep, m_sleepIslandCanSleep );
    m_nextSleepIslandVisualId = snapshot.nextSleepIslandVisualId;
    m_sleepEnabled = snapshot.sleepEnabled;
    m_pendingConstraintWakeBodyCount = 0;
    m_awakeListNeedsRebuild = true;
    m_simulationIslands.Invalidate();
}

void PhysicsSleepController::RestoreSimulationIslandTopology( const PhysicsBodyStore& bodyStore,
                                                              std::span<const PersistentContact> persistentContacts,
                                                              std::span<const PointJointConstraint> pointJointConstraints )
{
    // Invariant: the contact solver and point-joint owner have already restored
    // their authoritative rows. Seed the persistent island owner from those
    // exact rows so the first replayed tick compares like topology with like.
    m_simulationIslands.Rebuild( bodyStore, persistentContacts, pointJointConstraints, m_sleepState );

    const int modelCount = bodyStore.Count();
    m_sleepIslandParent.assign( static_cast<std::size_t>( modelCount ), 0 );
    m_sleepIslandRank.assign( static_cast<std::size_t>( modelCount ), 0u );
    DisjointSet islands( m_sleepIslandParent, m_sleepIslandRank, modelCount );
    islands.Reset();

    for ( const auto& edge : m_simulationIslands.ActiveContactEdges() )
    {
        islands.Unite( edge.first, edge.second );
    }

    for ( const SimulationIslandJointEdge& edge : m_simulationIslands.ActiveJointEdges() )
    {
        islands.Unite( edge.bodyA, edge.bodyB );
    }

    for ( int bodyIndex = 0; bodyIndex < modelCount; ++bodyIndex )
    {
        m_sleepIslandParent[static_cast<std::size_t>( bodyIndex )] = islands.Find( bodyIndex );
    }
}

std::span<const uint8_t> PhysicsSleepController::GetSleepStates() const
{
    return m_sleepState;
}

std::span<const int> PhysicsSleepController::GetAwakeBodyIndices() const
{
    return std::span<const int>( m_awakeBodyIndices.data(), m_awakeBodyIndices.size() );
}

int PhysicsSleepController::GetAwakeBodyCount() const
{
    return m_awakeBodyCount;
}
std::span<const uint8_t> PhysicsSleepController::GetUnderwaterSleepLocks() const
{
    return m_underwaterSleepLocked;
}
std::span<const int> PhysicsSleepController::GetSleepIslandVisualIds() const
{
    return m_sleepIslandVisualId;
}
std::span<const uint8_t> PhysicsSleepController::GetSleepSupportedStates() const
{
    return m_sleepSupportedThisFrame;
}
std::span<const uint8_t> PhysicsSleepController::GetSleepInhibitedStates() const
{
    return m_sleepInhibitedThisFrame;
}
std::span<const std::pair<int, int>> PhysicsSleepController::GetSleepSupportEdges() const
{
    return m_sleepSupportEdges;
}
PhysicsCandidatePairList& PhysicsSleepController::MutableSupportEdgesForContactSolver()
{
    return m_sleepSupportEdges;
}
std::span<uint8_t> PhysicsSleepController::MutableSupportedStatesForTerrain()
{
    return m_sleepSupportedThisFrame;
}
std::span<uint8_t> PhysicsSleepController::MutableInhibitedStatesForTerrain()
{
    return m_sleepInhibitedThisFrame;
}
std::span<const int> PhysicsSleepController::GetSleepIslandParents() const
{
    return m_sleepIslandParent;
}
std::span<const uint32_t> PhysicsSleepController::GetSleepCounters() const
{
    return m_sleepCounter;
}
std::span<const uint8_t> PhysicsSleepController::GetSleepIslandRanks() const
{
    return m_sleepIslandRank;
}
std::span<const uint8_t> PhysicsSleepController::GetSleepIslandHasAwake() const
{
    return m_sleepIslandHasAwake;
}
std::span<const uint8_t> PhysicsSleepController::GetSleepIslandHasSupportAnchor() const
{
    return m_sleepIslandHasSupportAnchor;
}
std::span<const uint8_t> PhysicsSleepController::GetSleepIslandEligible() const
{
    return m_sleepIslandEligible;
}
std::span<const uint8_t> PhysicsSleepController::GetSleepIslandTopologyStable() const
{
    return m_sleepIslandTopologyStable;
}
std::span<const uint8_t> PhysicsSleepController::GetSleepIslandCanSleep() const
{
    return m_sleepIslandCanSleep;
}
std::span<const uint8_t> PhysicsSleepController::GetSleepBodyEligible() const
{
    return m_sleepBodyEligible;
}
std::span<const uint8_t> PhysicsSleepController::GetSleepResetReasons() const
{
    return m_sleepResetReason;
}
std::span<const uint8_t> PhysicsSleepController::GetUnderwaterSleepLockVector() const
{
    return m_underwaterSleepLocked;
}
std::span<const int> PhysicsSleepController::GetSleepIslandVisualIdVector() const
{
    return m_sleepIslandVisualId;
}
uint64_t PhysicsSleepController::GetSleepIslandVisualIdCapacityBytes() const
{
    return m_sleepIslandVisualId.committed_bytes();
}
std::span<const int> PhysicsSleepController::GetSleepIslandAssignedVisualIds() const
{
    return m_sleepIslandAssignedVisualId;
}
std::span<const uint8_t> PhysicsSleepController::GetSleepStateVector() const
{
    return m_sleepState;
}
std::span<const PhysicsSleepPoseAnchor> PhysicsSleepController::GetSleepPoseAnchors() const
{
    return m_sleepPoseAnchors;
}
std::span<const uint8_t> PhysicsSleepController::GetSleepSupportedVector() const
{
    return m_sleepSupportedThisFrame;
}
std::span<const uint8_t> PhysicsSleepController::GetSleepInhibitedVector() const
{
    return m_sleepInhibitedThisFrame;
}
std::span<const std::pair<int, int>> PhysicsSleepController::GetSleepSupportEdgeVector() const
{
    return m_sleepSupportEdges;
}

uint64_t PhysicsSleepController::CollectDynamicMemoryBytes() const
{
    uint64_t bytes = m_awakeBodyIndices.committed_bytes() + m_awakeListPositions.committed_bytes();
    bytes += ListCapacityBytes( m_sleepSupportedThisFrame );
    bytes += ListCapacityBytes( m_sleepInhibitedThisFrame );
    bytes += ListCapacityBytes( m_sleepState );
    bytes += ListCapacityBytes( m_sleepCounter );
    bytes += ListCapacityBytes( m_sleepPoseAnchors );
    bytes += ListCapacityBytes( m_underwaterSleepLocked );
    bytes += ListCapacityBytes( m_sleepIslandVisualId );
    bytes += ListCapacityBytes( m_sleepIslandAssignedVisualId );
    bytes += ListCapacityBytes( m_sleepSupportEdges );
    bytes += m_simulationIslands.CollectDynamicMemoryBytes();
    bytes += ListCapacityBytes( m_sleepIslandParent );
    bytes += ListCapacityBytes( m_sleepIslandRank );
    bytes += ListCapacityBytes( m_sleepIslandHasAwake );
    bytes += ListCapacityBytes( m_sleepIslandHasSupportAnchor );
    bytes += ListCapacityBytes( m_sleepIslandEligible );
    bytes += ListCapacityBytes( m_sleepIslandTopologyStable );
    bytes += ListCapacityBytes( m_sleepIslandCanSleep );
    bytes += ListCapacityBytes( m_sleepBodyEligible );
    bytes += ListCapacityBytes( m_sleepResetReason );
    bytes += ListCapacityBytes( m_sleepScratchFlags );
    bytes += ListCapacityBytes( m_sleepFirstBoxContactPartner );
    bytes += ListCapacityBytes( m_restingWakeQueueScratch );
    return bytes;
}
