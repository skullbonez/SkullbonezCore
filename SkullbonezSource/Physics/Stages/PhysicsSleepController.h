/*
File: SkullbonezSource/Physics/Stages/PhysicsSleepController.h
Purpose:
  Owns physics sleep state, wake propagation, support, and island transitions.

Summary:
  PhysicsSleepController is the single owner of model-order sleep rows and all
  algorithms that mutate them. It borrows body, contact, constraint, clock, and
  diagnostics values only for synchronous fixed-step operations.

Glossary:
  Sleep island: Connected contact/joint component deactivated as one unit.
  Support edge: Directed relationship used to propagate grounded support.
  Underwater lock: Policy keeping a fully submerged sleeping ball dormant.
  Visual island id: Persisted debug id shared by bodies that slept together.
  Scratch flags: Transient per-row bits reused by point-joint and explicit-wake
    traversals; they are neither replay state nor cross-stage authority.

Invariants:
  - All model-order rows are construction-reserved to scene capacity.
  - Read-only pipeline stages receive const spans; wake mutations use explicit
    scoped capability values.
  - Packed scratch bits are written only by the serial sleep owner; no worker
    may update a different bit in the same row concurrently.
  - No callback, host pointer, PhysicsWorld reference, or concrete sibling
    owner crosses this boundary.

Related:
  - SkullbonezSource/Physics/Stages/PhysicsSleepController.cpp
  - SkullbonezSource/Physics/SleepIslandSystem.cpp
  - SkullbonezSource/Physics/PhysicsWorld.cpp
*/
#pragma once

#include <cstdint>
#include <span>
#include <utility>
#include <vector>

#include "../PhysicsDebugData.h"
#include "../Ragdoll.h"
#include "../SleepIslandSystem.h"
#include "../PhysicsBodyStore.h"
#include "PhysicsContactSolverStage.h"
#include "../../Runtime/Replay/ReplaySolverSnapshot.h"

namespace SkullbonezCore
{
namespace Core
{
class EngineConfig;
struct PhysicsSleepConfig;
} // namespace Core

namespace Physics
{
class ColliderStore;
class PhysicsBodyStore;
struct PersistentContact;
struct PhysicsBodyRecord;
struct PhysicsWorldForces;
class PhysicsSleepController;

class PhysicsNarrowphaseWakeAccess
{
  private:
    // Lifetime: mutable sleep rows and body stores are borrowed only for one
    // synchronous wake pass. Private fields prevent consumers from acquiring
    // raw mutation authority over the sleep owner's rows.
    PhysicsBodyStore& m_bodyStore;
    const ColliderStore& m_colliderStore;
    const PhysicsWorldForces& m_worldForces;
    std::span<PhysicsBodyRecord> m_bodyRecords;
    PhysicsBodyHotFieldsView m_hotFields;
    std::span<float> m_timeRemaining;
    std::span<uint8_t> m_sleepState;
    std::span<uint8_t> m_sleepCounter;
    std::span<int> m_sleepIslandVisualId;
    std::span<const uint8_t> m_underwaterSleepLocked;
    int m_modelCount = 0;
    float m_dt = 0.0f;

    PhysicsNarrowphaseWakeAccess( PhysicsBodyStore& bodyStore,
                                  const ColliderStore& colliderStore,
                                  const PhysicsWorldForces& worldForces,
                                  std::span<PhysicsBodyRecord> bodyRecords,
                                  const PhysicsBodyHotFieldsView& hotFields,
                                  std::span<float> timeRemaining,
                                  std::span<uint8_t> sleepState,
                                  std::span<uint8_t> sleepCounter,
                                  std::span<int> sleepIslandVisualId,
                                  std::span<const uint8_t> underwaterSleepLocked,
                                  int modelCount,
                                  float dt );
    friend class PhysicsSleepController;

  public:
    void WakeBody( int sleepingIndex ) const;
};

struct PhysicsSleepStepPolicy
{
    float linearSpeedSquared = 0.0f;
    float angularSpeedSquared = 0.0f;
    uint8_t frameCount = 1;
};

struct PhysicsSleepWakeContext
{
    // Lifetime: all rows and the cache capability are consumed synchronously;
    // the sleep owner never retains a sibling owner or borrowed frame state.
    int bodyCount = 0;
    std::span<const PhysicsBodyRecord> bodyRecords;
    PhysicsBodyHotFieldsView hotFields;
    PhysicsBodyStore* bodyStore = nullptr;
    const ColliderStore* colliderStore = nullptr;
    const PhysicsWorldForces* worldForces = nullptr;
    std::span<float> timeRemaining;
    PhysicsContactCacheWakeAccess contactCache;
    std::span<const PersistentContact> persistentContacts;
    const std::vector<PointJointConstraint>& pointJointConstraints;
};

struct PhysicsSleepIslandStageContext
{
    PhysicsBodyStore& bodyStore;
    const ColliderStore& colliderStore;
    const PhysicsWorldForces& worldForces;
    std::span<PhysicsBodyRecord> bodyRecords;
    PhysicsBodyHotFieldsView hotFields;
    std::span<float> timeRemaining;
    std::span<const PersistentContact> persistentContacts;
    std::span<const uint16_t> persistentRestingContactCounts;
    const std::vector<PointJointConstraint>& pointJointConstraints;
    std::vector<PhysicsPipelineRecord>& physicsPipelineTrace;
    int modelCount = 0;
    float sleepLinearSq = 0.0f;
    float sleepAngularSq = 0.0f;
    uint8_t sleepFrames = 0;
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
  private:
    std::vector<uint8_t> m_sleepSupportedThisFrame;
    std::vector<uint8_t> m_sleepInhibitedThisFrame;
    std::vector<uint8_t> m_sleepState;
    std::vector<uint8_t> m_sleepCounter;
    std::vector<uint8_t> m_underwaterSleepLocked;
    std::vector<int> m_sleepIslandVisualId;
    std::vector<int> m_sleepIslandAssignedVisualId;
    int m_nextSleepIslandVisualId = 1;
    bool m_sleepEnabled = true;
    uint8_t m_seedSleepFrameCount = 30;
    std::vector<std::pair<int, int>> m_sleepSupportEdges;
    std::vector<int> m_sleepIslandParent;
    std::vector<uint8_t> m_sleepIslandRank;
    std::vector<uint8_t> m_sleepIslandHasAwake;
    std::vector<uint8_t> m_sleepIslandHasSupportAnchor;
    std::vector<uint8_t> m_sleepIslandEligible;
    std::vector<uint8_t> m_sleepIslandCanSleep;
    std::vector<PhysicsSleepScratchFlags> m_sleepScratchFlags;
    std::vector<int> m_sleepVisualIslandIds;
    std::vector<int> m_sleepVisualIslandBodies;
    std::vector<int> m_restingWakeQueueScratch;
    SleepIslandSystem m_sleepIslandSystem;

    void EnsureUnderwaterSleepLockBuffer( int modelCount );
    void EnsureScratchFlagsSize( int modelCount );
    bool IsUnderwaterSleepLocked( int bodyCount, int index );
    bool WakeDynamicBodyState( const PhysicsSleepWakeContext& context, int index, float dt, bool applyForces );
    void WakeSleepVisualIsland( const PhysicsSleepWakeContext& context, int index, float dt, bool applyForces );
    void WakePointJointIsland( const PhysicsSleepWakeContext& context, int index, float dt, bool applyForces );
    void WakeRestingContactIsland( const PhysicsSleepWakeContext& context, int index, float dt, bool applyForces );
    void ApplyTransitions( const PhysicsSleepIslandStageContext& context, class DisjointSet& sleepIslands );

  public:
    PhysicsSleepController();

    void Clear();
    void ApplyRuntimeConfig( const Core::EngineConfig& config );
    PhysicsSleepStepPolicy ResolveStepPolicy( const Core::PhysicsSleepConfig& config ) const;
    void MirrorFlagsFrom( PhysicsBodyStore& bodyStore, int modelCount );
    void EnsureVisualIdSize( int modelCount );
    void WakeModel( const PhysicsSleepWakeContext& context, int index );
    PhysicsNarrowphaseWakeAccess CreateNarrowphaseWakeAccess( PhysicsBodyStore& bodyStore,
                                                              const ColliderStore& colliderStore,
                                                              const PhysicsWorldForces& worldForces,
                                                              std::span<PhysicsBodyRecord> bodyRecords,
                                                              std::span<float> timeRemaining,
                                                              int modelCount,
                                                              float dt );
    void SeedModelAsleep( const PhysicsBodyStore& bodyStore, int index );
    void SetPhysicsSleepEnabled( bool enabled );
    bool IsPhysicsSleepEnabled() const;
    void LockUnderwaterSleeperIfReady( const PhysicsWorldForces& worldForces,
                                       PhysicsBodyStore& bodyStore,
                                       const ColliderStore& colliderStore,
                                       std::span<float> timeRemaining,
                                       int index );
    void PropagateSupport( const PhysicsBodyStore& bodyStore );
    void AppendPointJointSupportEdges( const PhysicsBodyStore& bodyStore,
                                       const std::vector<PointJointConstraint>& pointJointConstraints,
                                       int modelCount );
    void WakePointJointConnectedBodies( PhysicsBodyStore& bodyStore,
                                        const ColliderStore& colliderStore,
                                        const PhysicsWorldForces& worldForces,
                                        std::span<float> timeRemaining,
                                        PhysicsContactCacheWakeAccess contactCache,
                                        std::span<const PersistentContact> persistentContacts,
                                        const std::vector<PointJointConstraint>& pointJointConstraints,
                                        float dt );
    void RunIslandStage( const PhysicsSleepIslandStageContext& context );
    bool IsPointJointPair( const PhysicsBodyStore& bodyStore,
                           const std::vector<PointJointConstraint>& pointJointConstraints,
                           int bodyA,
                           int bodyB ) const;

    void CaptureReplayState( Runtime::ReplaySolverWorldSnapshot& outSnapshot ) const;
    void RestoreReplayState( const Runtime::ReplaySolverWorldSnapshot& snapshot );

    std::span<const uint8_t> GetSleepStates() const;
    std::span<const uint8_t> GetUnderwaterSleepLocks() const;
    std::span<const int> GetSleepIslandVisualIds() const;
    std::span<const uint8_t> GetSleepSupportedStates() const;
    std::span<const uint8_t> GetSleepInhibitedStates() const;
    std::span<const std::pair<int, int>> GetSleepSupportEdges() const;
    // Lifetime: contact/terrain stages borrow these bounded rows only for one
    // synchronous pass; capacity and semantic ownership remain here.
    std::vector<std::pair<int, int>>& MutableSupportEdgesForContactSolver();
    std::span<uint8_t> MutableSupportedStatesForTerrain();
    std::span<uint8_t> MutableInhibitedStatesForTerrain();
    const std::vector<int>& GetSleepIslandParents() const;
    const std::vector<uint8_t>& GetSleepCounters() const;
    const std::vector<uint8_t>& GetSleepIslandRanks() const;
    const std::vector<uint8_t>& GetSleepIslandHasAwake() const;
    const std::vector<uint8_t>& GetSleepIslandHasSupportAnchor() const;
    const std::vector<uint8_t>& GetSleepIslandEligible() const;
    const std::vector<uint8_t>& GetSleepIslandCanSleep() const;
    const std::vector<uint8_t>& GetUnderwaterSleepLockVector() const;
    const std::vector<int>& GetSleepIslandVisualIdVector() const;
    const std::vector<int>& GetSleepIslandAssignedVisualIds() const;
    const std::vector<uint8_t>& GetSleepStateVector() const;
    const std::vector<uint8_t>& GetSleepSupportedVector() const;
    const std::vector<uint8_t>& GetSleepInhibitedVector() const;
    const std::vector<std::pair<int, int>>& GetSleepSupportEdgeVector() const;
    uint64_t CollectDynamicMemoryBytes() const;
};
} // namespace Physics
} // namespace SkullbonezCore
