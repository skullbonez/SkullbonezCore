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

  Visual island id: Persisted debug id shared by bodies that slept together.
  Scratch flags: Transient per-row bits reused by point-joint analysis; they are
    neither replay state nor cross-stage authority.

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
  - Parallel narrowphase workers publish atomic request bits only; the serial
    sequencer wakes complete islands and mutates the sorted awake list.

Related:
  - SkullbonezSource/Physics/Stages/PhysicsSleepController.cpp
  - SkullbonezSource/Physics/SleepIslandSystem.cpp
  - SkullbonezSource/Physics/PhysicsWorld.cpp
  - Agentic/Reference/engine-glossary.md
*/
#pragma once

#include <array>
#include <atomic>
#include <cstdint>
#include <limits>
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
    // Lifetime: the sleep controller and fixed rows are borrowed only for one
    // synchronous producer pass. Consumers receive behavior, never raw rows.
    PhysicsSleepController& m_sleepController;
    PhysicsBodyHotFieldsConstView m_hotFields;
    int m_modelCount = 0;

    PhysicsNarrowphaseWakeAccess( PhysicsSleepController& sleepController, PhysicsBodyHotFieldsConstView hotFields,
                                  int modelCount );
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
    uint32_t frameCount = 1;
    float objectPenetrationLimit = ( std::numeric_limits<float>::max )();
    float terrainPenetrationLimit = ( std::numeric_limits<float>::max )();
    float correctionSpeedSquared = ( std::numeric_limits<float>::max )();
    float poseDriftLimit = ( std::numeric_limits<float>::max )();

    bool IsQuiet( float bodyLinearSpeedSquared, float bodyAngularSpeedSquared, float linearThresholdScale = 1.0f,
                  float angularThresholdScale = 1.0f ) const
    {
        // Invariant: scales apply to speed thresholds, so squared comparisons
        // receive the squared scale as well.
        return bodyLinearSpeedSquared < linearSpeedSquared * linearThresholdScale * linearThresholdScale &&
               bodyAngularSpeedSquared < angularSpeedSquared * angularThresholdScale * angularThresholdScale;
    }

    bool NeedsMoreQuietFrames( uint32_t quietFrameCount ) const
    {
        return quietFrameCount < frameCount;
    }
};

enum class PhysicsSleepResetReason : uint8_t
{
    None = 0,
    Motion = 1,
    TerrainInhibition = 2,
    ContactTopology = 3,
    ContactStability = 4,
    PointJointError = 5,
    PoseDrift = 6,
    SteepSphereSlope = 7,
    UnsupportedBoxSupport = 8
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
    uint8_t boxBody : 1;
    uint8_t boxHasFaceSupport : 1;
    uint8_t boxHasNarrowSupport : 1;
    uint8_t boxHasSecondContact : 1;
    uint8_t steepSphereTerrain : 1;
};
static_assert( sizeof( PhysicsSleepScratchFlags ) == 1u, "Sleep scratch flags must occupy one byte per body." );

struct PhysicsSleepPoseAnchor
{
    Math::Vector::Vector3 position;
    std::array<float, 4> orientation { 0.0f, 0.0f, 0.0f, 1.0f };
    uint8_t flags = 0u;
};

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
    PhysicsBodyRowList<uint32_t> m_sleepCounter { "PhysicsSleepController.m_sleepCounter",
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
    PhysicsBodyHandle m_pendingConstraintWakeBodies[Scene::Capacity::MAX_SCENE_OBJECTS] = {};
    int m_pendingConstraintWakeBodyCount = 0;
    bool m_awakeListNeedsRebuild = true;
    bool m_resetDenseSleepHistoryForBodyTopologyChange = false;
    bool m_sleepEnabled = true;
    uint32_t m_seedSleepFrameCount = 30;
    PhysicsCandidatePairList m_sleepSupportEdges { "PhysicsSleepController.m_sleepSupportEdges",
                                                   PhysicsCapacityReason::CandidatePairs };
    SimulationIslandSystem m_simulationIslands;
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
    PhysicsBodyRowList<uint8_t> m_sleepIslandTopologyStable { "PhysicsSleepController.m_sleepIslandTopologyStable",
                                                              PhysicsCapacityReason::SceneBodies };
    PhysicsBodyRowList<uint8_t> m_sleepIslandCanSleep { "PhysicsSleepController.m_sleepIslandCanSleep",
                                                        PhysicsCapacityReason::SceneBodies };
    PhysicsBodyRowList<uint8_t> m_sleepBodyEligible { "PhysicsSleepController.m_sleepBodyEligible",
                                                      PhysicsCapacityReason::SceneBodies };
    PhysicsBodyRowList<uint8_t> m_sleepResetReason { "PhysicsSleepController.m_sleepResetReason",
                                                     PhysicsCapacityReason::SceneBodies };
    PhysicsBodyRowList<PhysicsSleepPoseAnchor> m_sleepPoseAnchors { "PhysicsSleepController.m_sleepPoseAnchors",
                                                                    PhysicsCapacityReason::SceneBodies };
    // Invariant: one fixed owner keeps the pose and its lifecycle bits aligned.
    // Workers atomically publish only the request bit; the serial sleep owner
    // alone mutates anchor validity after worker completion.
    static constexpr uint8_t SLEEP_POSE_ANCHOR_VALID_BIT = 1u << 0u;
    static constexpr uint8_t PENDING_NARROWPHASE_WAKE_BIT = 1u << 1u;
    PhysicsBodyRowList<PhysicsSleepScratchFlags> m_sleepScratchFlags { "PhysicsSleepController.m_sleepScratchFlags",
                                                                       PhysicsCapacityReason::SceneBodies };
    PhysicsBodyRowList<int> m_sleepFirstBoxContactPartner { "PhysicsSleepController.m_sleepFirstBoxContactPartner",
                                                            PhysicsCapacityReason::SceneBodies };
    PhysicsBodyRowList<int> m_restingWakeQueueScratch { "PhysicsSleepController.m_restingWakeQueueScratch",
                                                        PhysicsCapacityReason::SceneBodies };
    SleepSupportPropagationSystem m_sleepSupportPropagation;

    void EnsureUnderwaterSleepLockBuffer( int modelCount );
    void EnsureScratchFlagsSize( int modelCount );
    void RegisterBoxContactPartner( int bodyIndex, int partnerIndex );
    void RegisterBoxSupportContact( int bodyIndex, int partnerIndex, bool facePatch );
    void ApplyPendingConstraintTopologyWakes( PhysicsBodyStore& bodyStore, int modelCount );
    bool IsUnderwaterSleepLocked( int bodyCount, int index );
    bool PrepareExplicitWake( PhysicsBodyStore& bodyStore, int index );
    bool WakeDynamicBodyState( PhysicsBodyStore& bodyStore, PhysicsContactCacheWakeAccess contactCache, int index );
    bool WakeDynamicBodyStateWithForces( PhysicsBodyStore& bodyStore, const ColliderStore& colliderStore,
                                         PhysicsTerrainView terrain, const PhysicsWorldForces& worldForces,
                                         std::span<BuoyancyBodyFacts> buoyancyFacts, std::span<float> timeRemaining,
                                         PhysicsContactCacheWakeAccess contactCache, int index, float dt );
    void WakeRetainedSimulationIsland( PhysicsBodyStore& bodyStore, PhysicsContactCacheWakeAccess contactCache, int index );
    void PrepareIslandScratch( const ColliderStore& colliderStore, int modelCount );
    void BuildSimulationIslandTopology( const PhysicsBodyStore& bodyStore,
                                        std::span<const PersistentContact> persistentContacts,
                                        std::span<const PointJointConstraint> pointJointConstraints,
                                        const PhysicsBodyHotFieldsConstView& hotFields, class DisjointSet& sleepIslands );
    void ClassifyContactStability( const ColliderStore& colliderStore, const PhysicsWorldForces& worldForces,
                                   std::span<const PersistentContact> persistentContacts,
                                   const PhysicsSleepStepPolicy& sleepPolicy, int modelCount );
    void ClassifyPointJointStability( const PhysicsBodyStore& bodyStore,
                                      std::span<const PointJointConstraint> pointJointConstraints,
                                      const PhysicsBodyHotFieldsConstView& hotFields, class DisjointSet& sleepIslands,
                                      int modelCount );
    template <bool RetainPipelineRecords>
    void EvaluateAwakeBodyEligibility( const PhysicsBodyStore& bodyStore, const ColliderStore& colliderStore,
                                       const PhysicsWorldForces& worldForces,
                                       std::span<const uint16_t> persistentRestingContactCounts,
                                       PhysicsPipelineTraceRecorder& physicsPipelineTrace,
                                       const PhysicsSleepStepPolicy& sleepPolicy, class DisjointSet& sleepIslands );
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
    PhysicsSleepStepPolicy ResolveStepPolicy( const PhysicsRuntimeSettings& settings ) const;

    // Returns true when a cold topology/replay/config boundary rebuilt the
    // derived awake index. Ordinary fixed steps preserve the controller-owned
    // sleep rows without rescanning the body-store flag array.
    bool MirrorFlagsFrom( PhysicsBodyStore& bodyStore, int modelCount );
    void InvalidateBodyTopology();
    void QueueConstraintTopologyWake( PhysicsBodyHandle bodyA, PhysicsBodyHandle bodyB );
    void CommitPendingNarrowphaseWakes( PhysicsBodyStore& bodyStore, const ColliderStore& colliderStore,
                                        PhysicsTerrainView terrain, const PhysicsWorldForces& worldForces,
                                        std::span<BuoyancyBodyFacts> buoyancyFacts, std::span<float> timeRemaining,
                                        PhysicsContactCacheWakeAccess contactCache, float dt );
    void EnsureVisualIdSize( int modelCount );
    void WakeModel( PhysicsBodyStore& bodyStore, PhysicsContactCacheWakeAccess contactCache, int index );
    void WakeModel( PhysicsBodyStore& bodyStore, const ColliderStore& colliderStore, const PhysicsWorldForces& worldForces,
                    std::span<BuoyancyBodyFacts> buoyancyFacts, std::span<float> timeRemaining,
                    PhysicsContactCacheWakeAccess contactCache, int index );
    PhysicsNarrowphaseWakeAccess CreateNarrowphaseWakeAccess( const PhysicsBodyStore& bodyStore );
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

    // Invariant: restore performs no validation after its first mutation; this
    // preflight must prove all dense-row, edge, and committed-capacity facts.
    bool CanRestoreReplayState( const PhysicsSolverSnapshot& snapshot, int modelCount ) const noexcept;
    void RestoreReplayState( const PhysicsSolverSnapshot& snapshot );
    void RestoreSimulationIslandTopology( const PhysicsBodyStore& bodyStore,
                                          std::span<const PersistentContact> persistentContacts,
                                          std::span<const PointJointConstraint> pointJointConstraints );

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
    std::span<const uint32_t> GetSleepCounters() const;
    std::span<const uint8_t> GetSleepIslandRanks() const;
    std::span<const uint8_t> GetSleepIslandHasAwake() const;
    std::span<const uint8_t> GetSleepIslandHasSupportAnchor() const;
    std::span<const uint8_t> GetSleepIslandEligible() const;
    std::span<const uint8_t> GetSleepIslandTopologyStable() const;
    std::span<const uint8_t> GetSleepIslandCanSleep() const;
    std::span<const uint8_t> GetSleepBodyEligible() const;
    std::span<const uint8_t> GetSleepResetReasons() const;
    std::span<const uint8_t> GetUnderwaterSleepLockVector() const;
    std::span<const int> GetSleepIslandVisualIdVector() const;
    uint64_t GetSleepIslandVisualIdCapacityBytes() const;
    std::span<const int> GetSleepIslandAssignedVisualIds() const;
    std::span<const uint8_t> GetSleepStateVector() const;
    std::span<const PhysicsSleepPoseAnchor> GetSleepPoseAnchors() const;
    std::span<const uint8_t> GetSleepSupportedVector() const;
    std::span<const uint8_t> GetSleepInhibitedVector() const;
    std::span<const std::pair<int, int>> GetSleepSupportEdgeVector() const;
    uint64_t CollectDynamicMemoryBytes() const;
};
} // namespace Physics
} // namespace SkullbonezCore
