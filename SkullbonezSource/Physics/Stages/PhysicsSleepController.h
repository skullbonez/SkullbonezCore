/*
File: SkullbonezSource/Physics/Stages/PhysicsSleepController.h
Purpose:
  Owns physics sleep state, wake propagation, support, and island transitions.

Summary:
  PhysicsSleepController is the single owner of model-order sleep rows and all
  algorithms that mutate them, including the ascending dense awake index list.
  It borrows body, contact, constraint, clock, and diagnostics values only for
  synchronous fixed-step operations.

Glossary:
  Support edge: Directed relationship used to propagate grounded support.
  Visual island id: Persisted debug id shared by bodies that slept together.
  Scratch flags: Transient per-row bits reused by point-joint and explicit-wake
    traversals; they are neither replay state nor cross-stage authority.

Invariants:
  - Fixed-list model rows are reserved to the active scene capacity before play.
  - Read-only pipeline stages receive const spans; wake mutations use explicit
    scoped capability values.
  - Packed scratch bits are written only by the serial sleep owner; no worker
    may update a different bit in the same row concurrently.
  - Full/count pipeline mode dispatch occurs once before sleep row loops; the
    count lane never constructs a diagnostic payload.
  - No callback, host pointer, PhysicsWorld reference, or concrete sibling
    owner crosses this boundary.
  - Parallel wake producers claim a body once through atomic sleep-state
    transition; only the sequencer mutates the sorted awake list.

Related:
  - SkullbonezSource/Physics/Stages/PhysicsSleepController.cpp
  - SkullbonezSource/Physics/SleepIslandSystem.cpp
  - SkullbonezSource/Physics/PhysicsWorld.cpp
  - Agentic/Reference/engine-glossary.md
*/
#pragma once

#include <atomic>
#include <cstdint>
#include <span>
#include <utility>

#include "../PhysicsDebugData.h"
#include "../BuoyancySystem.h"
#include "../PhysicsRuntimeSettings.h"
#include "../PhysicsStageCapacity.h"
#include "../Ragdoll.h"
#include "../SleepIslandSystem.h"
#include "../PhysicsBodyStore.h"
#include "PhysicsContactSolverStage.h"
#include "../PhysicsSolverSnapshot.h"

namespace SkullbonezCore
{
namespace Core
{
} // namespace Core

namespace Physics
{
class ColliderStore;
class PhysicsBodyStore;
class PhysicsPipelineTraceRecorder;
struct PersistentContact;
struct PhysicsBodyRecord;
struct PhysicsWorldForces;
class PhysicsSleepController;

class PhysicsNarrowphaseWakeAccess
{
  private:

    // Lifetime: the sleep controller and body stores are borrowed only for one
    // synchronous wake pass. Consumers receive behavior, never raw sleep rows.
    PhysicsSleepController& m_sleepController;
    PhysicsBodyStore& m_bodyStore;
    const ColliderStore& m_colliderStore;
    PhysicsTerrainView m_terrain;
    const PhysicsWorldForces& m_worldForces;
    std::span<BuoyancyBodyFacts> m_buoyancyFacts;
    std::span<PhysicsBodyRecord> m_bodyRecords;
    PhysicsBodyHotFieldsView m_hotFields;
    std::span<float> m_timeRemaining;
    int m_modelCount = 0;
    float m_dt = 0.0f;

    PhysicsNarrowphaseWakeAccess( PhysicsSleepController& sleepController, PhysicsBodyStore& bodyStore,
                                  const ColliderStore& colliderStore, PhysicsTerrainView terrain,
                                  const PhysicsWorldForces& worldForces, std::span<BuoyancyBodyFacts> buoyancyFacts,
                                  std::span<PhysicsBodyRecord> bodyRecords, const PhysicsBodyHotFieldsView& hotFields,
                                  std::span<float> timeRemaining, int modelCount, float dt );
    friend class PhysicsSleepController;

  public:

    // Read-only pair queries preserve the sleep controller as sole row owner;
    // this capability borrow remains scoped to one synchronous narrowphase pass.
    int SleepRowCount() const;
    bool IsSleeping( int bodyIndex ) const;
    bool IsUnderwaterSleepLocked( int bodyIndex ) const;
    void WakeBody( int sleepingIndex ) const;
};

struct PhysicsSleepStepPolicy
{
    float linearSpeedSquared = 0.0f;
    float angularSpeedSquared = 0.0f;
    uint8_t frameCount = 1;
};

// Concept: related one-bit scratch decisions share one byte per model row.
//
// These values formerly occupied four independent byte arrays. They are safe
// to co-locate because the sleep controller alone sequences every mutation,
// and replay never serializes them. Named fields keep call sites bool-like
// while the size assertion makes the cache-working-set experiment explicit.
struct PhysicsSleepScratchFlags
{
    uint8_t pointJointBody : 1;
    uint8_t islandHasPointJoint : 1;
    uint8_t islandPointJointsRelaxed : 1;
    uint8_t restingWakeVisited : 1;
    uint8_t reserved : 4;
};
static_assert( sizeof( PhysicsSleepScratchFlags ) == 1u, "Sleep scratch flags must occupy one byte per body." );

class PhysicsSleepController
{
    friend class PhysicsNarrowphaseWakeAccess;
    friend struct PhysicsSleepControllerTestAccess;

  private:

    // Why: Debug validates the derived awake-list membership with this pure
    // classifier; Release retains the zero-cost trusted traversal.
    static constexpr bool IsAwakeListEntryConsistent( bool fixed, bool sleeping ) noexcept
    {
        return !fixed && !sleeping;
    }

    PhysicsBodyRowList<uint8_t> m_sleepSupportedThisFrame { "PhysicsSleepController.m_sleepSupportedThisFrame",
                                                            PhysicsCapacityReason::SceneBodies };
    PhysicsBodyRowList<uint8_t> m_sleepInhibitedThisFrame { "PhysicsSleepController.m_sleepInhibitedThisFrame",
                                                            PhysicsCapacityReason::SceneBodies };
    PhysicsBodyRowList<uint8_t> m_sleepState { "PhysicsSleepController.m_sleepState", PhysicsCapacityReason::SceneBodies };
    PhysicsBodyRowList<uint8_t> m_sleepCounter { "PhysicsSleepController.m_sleepCounter",
                                                 PhysicsCapacityReason::SceneBodies };
    PhysicsBodyRowList<uint8_t> m_underwaterSleepLocked { "PhysicsSleepController.m_underwaterSleepLocked",
                                                          PhysicsCapacityReason::SceneBodies };
    PhysicsBodyRowList<int> m_sleepIslandVisualId { "PhysicsSleepController.m_sleepIslandVisualId",
                                                    PhysicsCapacityReason::SceneBodies };
    PhysicsBodyRowList<int> m_sleepIslandAssignedVisualId { "PhysicsSleepController.m_sleepIslandAssignedVisualId",
                                                            PhysicsCapacityReason::SceneBodies };
    int m_nextSleepIslandVisualId = 1;
    int m_awakeBodyCount = 0; // Dynamic awake rows at the last mirror or completed sleep-island transition.
    PhysicsBodyRowList<int> m_awakeBodyIndices { "PhysicsSleepController.awakeBodyIndices",
                                                 PhysicsCapacityReason::SceneBodies };
    PhysicsBodyRowList<int> m_awakeListPositions { "PhysicsSleepController.awakeListPositions",
                                                   PhysicsCapacityReason::SceneBodies };
    int m_pendingAwakeIndices[Scene::Capacity::MAX_SCENE_OBJECTS] = {};

    // Parallel producers access this aligned scalar only through atomic_ref.
    // Restore resets the pending count before rebuilding the authoritative
    // awake rows, so no producer-owned wake request crosses a restore boundary.
    int m_pendingAwakeCount = 0;
    bool m_awakeListNeedsRebuild = true;
    bool m_sleepEnabled = true;
    uint8_t m_seedSleepFrameCount = 30;
    PhysicsCandidatePairList m_sleepSupportEdges { "PhysicsSleepController.m_sleepSupportEdges",
                                                   PhysicsCapacityReason::CandidatePairs };
    PhysicsBodyRowList<int> m_sleepIslandParent { "PhysicsSleepController.m_sleepIslandParent",
                                                  PhysicsCapacityReason::SceneBodies };
    PhysicsBodyRowList<uint8_t> m_sleepIslandRank { "PhysicsSleepController.m_sleepIslandRank",
                                                    PhysicsCapacityReason::SceneBodies };
    PhysicsBodyRowList<uint8_t> m_sleepIslandHasAwake { "PhysicsSleepController.m_sleepIslandHasAwake",
                                                        PhysicsCapacityReason::SceneBodies };
    PhysicsBodyRowList<uint8_t> m_sleepIslandHasSupportAnchor { "PhysicsSleepController.m_sleepIslandHasSupportAnchor",
                                                                PhysicsCapacityReason::SceneBodies };
    PhysicsBodyRowList<uint8_t> m_sleepIslandEligible { "PhysicsSleepController.m_sleepIslandEligible",
                                                        PhysicsCapacityReason::SceneBodies };
    PhysicsBodyRowList<uint8_t> m_sleepIslandCanSleep { "PhysicsSleepController.m_sleepIslandCanSleep",
                                                        PhysicsCapacityReason::SceneBodies };
    PhysicsBodyRowList<PhysicsSleepScratchFlags> m_sleepScratchFlags { "PhysicsSleepController.m_sleepScratchFlags",
                                                                       PhysicsCapacityReason::SceneBodies };
    PhysicsBodyRowList<int> m_sleepVisualIslandIds { "PhysicsSleepController.m_sleepVisualIslandIds",
                                                     PhysicsCapacityReason::SceneBodies };
    PhysicsBodyRowList<int> m_sleepVisualIslandBodies { "PhysicsSleepController.m_sleepVisualIslandBodies",
                                                        PhysicsCapacityReason::SceneBodies };
    PhysicsBodyRowList<int> m_restingWakeQueueScratch { "PhysicsSleepController.m_restingWakeQueueScratch",
                                                        PhysicsCapacityReason::SceneBodies };
    SleepIslandSystem m_sleepIslandSystem;

    void EnsureUnderwaterSleepLockBuffer( int modelCount );
    void EnsureScratchFlagsSize( int modelCount );
    bool IsUnderwaterSleepLocked( int bodyCount, int index );
    bool PrepareExplicitWake( PhysicsBodyStore& bodyStore, int index );
    bool WakeDynamicBodyState( PhysicsBodyStore& bodyStore, PhysicsContactCacheWakeAccess contactCache, int index );
    bool WakeDynamicBodyStateWithForces( PhysicsBodyStore& bodyStore, const ColliderStore& colliderStore,
                                         PhysicsTerrainView terrain, const PhysicsWorldForces& worldForces,
                                         std::span<BuoyancyBodyFacts> buoyancyFacts, std::span<float> timeRemaining,
                                         PhysicsContactCacheWakeAccess contactCache, int index, float dt );
    void WakeSleepVisualIsland( PhysicsBodyStore& bodyStore, PhysicsContactCacheWakeAccess contactCache, int index );
    void WakePointJointIsland( PhysicsBodyStore& bodyStore, PhysicsContactCacheWakeAccess contactCache,
                               std::span<const PointJointConstraint> pointJointConstraints, int index );
    void WakeRestingContactIsland( PhysicsBodyStore& bodyStore, PhysicsContactCacheWakeAccess contactCache,
                                   std::span<const PersistentContact> persistentContacts, int index );
    template <bool RetainPipelineRecords>
    void RunIslandStageMode( PhysicsBodyStore& bodyStore, const ColliderStore& colliderStore,
                             const PhysicsWorldForces& worldForces, std::span<BuoyancyBodyFacts> buoyancyFacts,
                             std::span<float> timeRemaining, std::span<const PersistentContact> persistentContacts,
                             std::span<const uint16_t> persistentRestingContactCounts,
                             std::span<const PointJointConstraint> pointJointConstraints,
                             PhysicsPipelineTraceRecorder& physicsPipelineTrace, const PhysicsSleepStepPolicy& sleepPolicy );
    template <bool RetainPipelineRecords>
    void ApplyTransitionsMode( PhysicsBodyStore& bodyStore, const ColliderStore& colliderStore,
                               const PhysicsWorldForces& worldForces, std::span<BuoyancyBodyFacts> buoyancyFacts,
                               std::span<float> timeRemaining, PhysicsPipelineTraceRecorder& physicsPipelineTrace,
                               const PhysicsSleepStepPolicy& sleepPolicy, class DisjointSet& sleepIslands );
    void RebuildAwakeBodyIndices( const PhysicsBodyHotFieldsConstView& hotFields, int modelCount );
    void AddAwakeBodyIndex( int index );
    void RemoveAwakeBodyIndex( int index );

  public:
    PhysicsSleepController();
    void ReserveBodyCapacity( std::size_t bodyCapacity, std::size_t pointJointCapacity = 0u );

    void Clear();
    void ApplyRuntimeSettings( const SleepSettings& settings );
    PhysicsSleepStepPolicy ResolveStepPolicy( const SleepSettings& settings ) const;

    // Returns true when a cold topology/replay/config boundary rebuilt the
    // derived awake index. Ordinary fixed steps preserve the controller-owned
    // sleep rows without rescanning the body-store flag array.
    bool MirrorFlagsFrom( PhysicsBodyStore& bodyStore, int modelCount );
    void InvalidateBodyTopology();
    void FlushPendingAwakeBodyIndices();
    void EnsureVisualIdSize( int modelCount );
    void WakeModel( PhysicsBodyStore& bodyStore, PhysicsContactCacheWakeAccess contactCache,
                    std::span<const PersistentContact> persistentContacts,
                    std::span<const PointJointConstraint> pointJointConstraints, int index );
    void WakeModel( PhysicsBodyStore& bodyStore, const ColliderStore& colliderStore, const PhysicsWorldForces& worldForces,
                    std::span<BuoyancyBodyFacts> buoyancyFacts, std::span<float> timeRemaining,
                    PhysicsContactCacheWakeAccess contactCache, std::span<const PersistentContact> persistentContacts,
                    std::span<const PointJointConstraint> pointJointConstraints, int index );
    PhysicsNarrowphaseWakeAccess CreateNarrowphaseWakeAccess( PhysicsBodyStore& bodyStore,
                                                              const ColliderStore& colliderStore, PhysicsTerrainView terrain,
                                                              const PhysicsWorldForces& worldForces,
                                                              std::span<BuoyancyBodyFacts> buoyancyFacts,
                                                              std::span<PhysicsBodyRecord> bodyRecords,
                                                              std::span<float> timeRemaining, int modelCount, float dt );
    void SeedModelAsleep( const PhysicsBodyStore& bodyStore, int index );
    void SetPhysicsSleepEnabled( bool enabled );
    bool IsPhysicsSleepEnabled() const;
    void LockUnderwaterSleeperIfReady( const PhysicsWorldForces& worldForces, PhysicsBodyStore& bodyStore,
                                       const ColliderStore& colliderStore, std::span<BuoyancyBodyFacts> buoyancyFacts,
                                       std::span<float> timeRemaining, int index );
    void PropagateSupport( const PhysicsBodyStore& bodyStore );
    void AppendPointJointSupportEdges( const PhysicsBodyStore& bodyStore,
                                       std::span<const PointJointConstraint> pointJointConstraints, int modelCount );
    void WakePointJointConnectedBodies( PhysicsBodyStore& bodyStore, const ColliderStore& colliderStore,
                                        PhysicsTerrainView terrain, const PhysicsWorldForces& worldForces,
                                        std::span<BuoyancyBodyFacts> buoyancyFacts, std::span<float> timeRemaining,
                                        PhysicsContactCacheWakeAccess contactCache,
                                        std::span<const PointJointConstraint> pointJointConstraints, float dt );
    void RunIslandStage( PhysicsBodyStore& bodyStore, const ColliderStore& colliderStore,
                         const PhysicsWorldForces& worldForces, std::span<BuoyancyBodyFacts> buoyancyFacts,
                         std::span<float> timeRemaining, std::span<const PersistentContact> persistentContacts,
                         std::span<const uint16_t> persistentRestingContactCounts,
                         std::span<const PointJointConstraint> pointJointConstraints,
                         PhysicsPipelineTraceRecorder& physicsPipelineTrace, const PhysicsSleepStepPolicy& sleepPolicy );

    void CaptureReplayState( PhysicsSolverSnapshot& outSnapshot ) const;
    void RestoreReplayState( const PhysicsSolverSnapshot& snapshot );

    std::span<const uint8_t> GetSleepStates() const;
    std::span<const int> GetAwakeBodyIndices() const;
    int GetAwakeBodyCount() const;
    std::span<const uint8_t> GetUnderwaterSleepLocks() const;
    std::span<const int> GetSleepIslandVisualIds() const;
    std::span<const uint8_t> GetSleepSupportedStates() const;
    std::span<const uint8_t> GetSleepInhibitedStates() const;
    std::span<const std::pair<int, int>> GetSleepSupportEdges() const;

    // Lifetime: contact/terrain stages borrow these bounded rows only for one
    // synchronous pass; capacity and semantic ownership remain here.
    PhysicsCandidatePairList& MutableSupportEdgesForContactSolver();
    std::span<uint8_t> MutableSupportedStatesForTerrain();
    std::span<uint8_t> MutableInhibitedStatesForTerrain();
    std::span<const int> GetSleepIslandParents() const;
    std::span<const uint8_t> GetSleepCounters() const;
    std::span<const uint8_t> GetSleepIslandRanks() const;
    std::span<const uint8_t> GetSleepIslandHasAwake() const;
    std::span<const uint8_t> GetSleepIslandHasSupportAnchor() const;
    std::span<const uint8_t> GetSleepIslandEligible() const;
    std::span<const uint8_t> GetSleepIslandCanSleep() const;
    std::span<const uint8_t> GetUnderwaterSleepLockVector() const;
    std::span<const int> GetSleepIslandVisualIdVector() const;
    uint64_t GetSleepIslandVisualIdCapacityBytes() const;
    std::span<const int> GetSleepIslandAssignedVisualIds() const;
    std::span<const uint8_t> GetSleepStateVector() const;
    std::span<const uint8_t> GetSleepSupportedVector() const;
    std::span<const uint8_t> GetSleepInhibitedVector() const;
    std::span<const std::pair<int, int>> GetSleepSupportEdgeVector() const;
    uint64_t CollectDynamicMemoryBytes() const;
};
} // namespace Physics
} // namespace SkullbonezCore
