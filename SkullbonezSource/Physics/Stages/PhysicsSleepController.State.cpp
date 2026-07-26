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
template <typename T> uint64_t VectorCapacityBytes( const std::vector<T>& values )
{
    return static_cast<uint64_t>( values.capacity() ) * static_cast<uint64_t>( sizeof( T ) );
}
} // namespace

void PhysicsSleepController::CaptureReplayState( PhysicsSolverSnapshot& snapshot ) const
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

void PhysicsSleepController::RestoreReplayState( const PhysicsSolverSnapshot& snapshot )
{
    m_sleepSupportedThisFrame = snapshot.sleepSupportedThisFrame;
    m_sleepInhibitedThisFrame = snapshot.sleepInhibitedThisFrame;
    m_sleepState = snapshot.sleepState;
    m_sleepCounter = snapshot.sleepCounter;
    m_underwaterSleepLocked = snapshot.underwaterSleepLocked;
    m_sleepIslandVisualId = snapshot.sleepIslandVisualId;
    m_sleepIslandAssignedVisualId = snapshot.sleepIslandAssignedVisualId;
    // Invariant: replay restore is a cold copy, but it still may not enlarge a
    // hot owner beyond the same construction-reserved support-edge ceiling.
    ValidateSleepSupportEdgeCount( snapshot.sleepSupportEdges.size(),
                                   m_sleepSupportEdges.capacity(),
                                   m_sleepSupportEdges.size(),
                                   "replay_restore" );

    m_sleepSupportEdges = snapshot.sleepSupportEdges;
    m_sleepIslandParent = snapshot.sleepIslandParent;
    m_sleepIslandRank = snapshot.sleepIslandRank;
    m_sleepIslandHasAwake = snapshot.sleepIslandHasAwake;
    m_sleepIslandHasSupportAnchor = snapshot.sleepIslandHasSupportAnchor;
    m_sleepIslandEligible = snapshot.sleepIslandEligible;
    m_sleepIslandCanSleep = snapshot.sleepIslandCanSleep;
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
std::vector<std::pair<int, int>>& PhysicsSleepController::MutableSupportEdgesForContactSolver()
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
const std::vector<int>& PhysicsSleepController::GetSleepIslandParents() const
{
    return m_sleepIslandParent;
}
const std::vector<uint8_t>& PhysicsSleepController::GetSleepCounters() const
{
    return m_sleepCounter;
}
const std::vector<uint8_t>& PhysicsSleepController::GetSleepIslandRanks() const
{
    return m_sleepIslandRank;
}
const std::vector<uint8_t>& PhysicsSleepController::GetSleepIslandHasAwake() const
{
    return m_sleepIslandHasAwake;
}
const std::vector<uint8_t>& PhysicsSleepController::GetSleepIslandHasSupportAnchor() const
{
    return m_sleepIslandHasSupportAnchor;
}
const std::vector<uint8_t>& PhysicsSleepController::GetSleepIslandEligible() const
{
    return m_sleepIslandEligible;
}
const std::vector<uint8_t>& PhysicsSleepController::GetSleepIslandCanSleep() const
{
    return m_sleepIslandCanSleep;
}
const std::vector<uint8_t>& PhysicsSleepController::GetUnderwaterSleepLockVector() const
{
    return m_underwaterSleepLocked;
}
const std::vector<int>& PhysicsSleepController::GetSleepIslandVisualIdVector() const
{
    return m_sleepIslandVisualId;
}
const std::vector<int>& PhysicsSleepController::GetSleepIslandAssignedVisualIds() const
{
    return m_sleepIslandAssignedVisualId;
}
const std::vector<uint8_t>& PhysicsSleepController::GetSleepStateVector() const
{
    return m_sleepState;
}
const std::vector<uint8_t>& PhysicsSleepController::GetSleepSupportedVector() const
{
    return m_sleepSupportedThisFrame;
}
const std::vector<uint8_t>& PhysicsSleepController::GetSleepInhibitedVector() const
{
    return m_sleepInhibitedThisFrame;
}
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
    bytes += VectorCapacityBytes( m_sleepScratchFlags );
    bytes += VectorCapacityBytes( m_sleepVisualIslandIds );
    bytes += VectorCapacityBytes( m_sleepVisualIslandBodies );
    bytes += VectorCapacityBytes( m_restingWakeQueueScratch );
    return bytes;
}
