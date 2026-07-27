/*
File: SkullbonezSource/Physics/Stages/PhysicsSleepController.State.cpp
Purpose:
  Implements sleep-owner replay transfer, bounded row views, and memory accounting.

Summary:
  This physical unit keeps the cohesive PhysicsSleepController owner below the
  stage-size review target without creating a second owner. Replay operations
  copy complete value state and invalidate derived awake indices for a cold
  rebuild; view methods expose only synchronous bounded rows.

Glossary:
  Replay transfer: Deterministic copy between owned sleep rows and the solver snapshot.
  Row view: Borrow of an owner-retained model-order buffer for one sequenced pass.
  Capacity bytes: Reserved storage reported for diagnostics, not live row count.

Invariants:
  - Replay capture and restore preserve every sleep-owned row in matching order.
  - Mutable views never transfer capacity or lifetime ownership to consumers.
  - Memory accounting includes every construction-reserved sleep vector.
  - Replay restore never trusts a pre-restore dense awake index mapping.

Related:
  - SkullbonezSource/Physics/Stages/PhysicsSleepController.h
  - SkullbonezSource/Physics/Stages/PhysicsSleepController.cpp
  - SkullbonezSource/Physics/Stages/PhysicsSleepController.Wake.cpp
*/
#include "PhysicsSleepController.h"

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

void PhysicsSleepController::RestoreReplayState( const PhysicsSolverSnapshot& snapshot )
{
    RestoreList( snapshot.sleepSupportedThisFrame, m_sleepSupportedThisFrame );
    RestoreList( snapshot.sleepInhibitedThisFrame, m_sleepInhibitedThisFrame );
    RestoreList( snapshot.sleepState, m_sleepState );
    RestoreList( snapshot.sleepCounter, m_sleepCounter );
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
    m_pendingAwakeCount = 0;
    m_awakeListNeedsRebuild = true;
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
std::span<const uint8_t> PhysicsSleepController::GetSleepCounters() const
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
std::span<const uint8_t> PhysicsSleepController::GetSleepIslandCanSleep() const
{
    return m_sleepIslandCanSleep;
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
    bytes += ListCapacityBytes( m_underwaterSleepLocked );
    bytes += ListCapacityBytes( m_sleepIslandVisualId );
    bytes += ListCapacityBytes( m_sleepIslandAssignedVisualId );
    bytes += ListCapacityBytes( m_sleepSupportEdges );
    bytes += ListCapacityBytes( m_sleepIslandParent );
    bytes += ListCapacityBytes( m_sleepIslandRank );
    bytes += ListCapacityBytes( m_sleepIslandHasAwake );
    bytes += ListCapacityBytes( m_sleepIslandHasSupportAnchor );
    bytes += ListCapacityBytes( m_sleepIslandEligible );
    bytes += ListCapacityBytes( m_sleepIslandCanSleep );
    bytes += ListCapacityBytes( m_sleepScratchFlags );
    bytes += ListCapacityBytes( m_sleepVisualIslandIds );
    bytes += ListCapacityBytes( m_sleepVisualIslandBodies );
    bytes += ListCapacityBytes( m_restingWakeQueueScratch );
    return bytes;
}
