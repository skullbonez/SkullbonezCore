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

Invariants:
  - All model-order rows are construction-reserved to scene capacity.
  - Read-only pipeline stages receive const spans; wake mutations use explicit
    controller APIs.
  - No callback, host pointer, or PhysicsWorld reference crosses this boundary.

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
#include "../../Runtime/Replay/ReplaySolverSnapshot.h"

namespace SkullbonezCore
{
namespace Core
{
class EngineConfig;
} // namespace Core

namespace Physics
{
class ColliderStore;
class PhysicsBodyStore;
class PhysicsContactSolverStage;
struct PersistentContact;
struct PhysicsBodyRecord;
struct PhysicsWorldForces;

struct PhysicsSleepWakeContext
{
    int bodyCount = 0;
    std::span<const PhysicsBodyRecord> bodyRecords;
    PhysicsBodyStore* bodyStore = nullptr;
    const ColliderStore* colliderStore = nullptr;
    const PhysicsWorldForces* worldForces = nullptr;
    std::span<float> timeRemaining;
    PhysicsContactSolverStage& contactSolverStage;
    std::span<const PersistentContact> persistentContacts;
    const std::vector<PointJointConstraint>& pointJointConstraints;
};

struct PhysicsSleepIslandStageContext
{
    PhysicsBodyStore& bodyStore;
    const ColliderStore& colliderStore;
    const PhysicsWorldForces& worldForces;
    std::span<PhysicsBodyRecord> bodyRecords;
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
    std::vector<uint8_t> m_sleepPointJointBody;
    std::vector<uint8_t> m_sleepIslandHasPointJoint;
    std::vector<uint8_t> m_sleepIslandPointJointsRelaxed;
    std::vector<int> m_sleepVisualIslandIds;
    std::vector<int> m_sleepVisualIslandBodies;
    std::vector<uint8_t> m_restingWakeVisitedScratch;
    std::vector<int> m_restingWakeQueueScratch;
    SleepIslandSystem m_sleepIslandSystem;

    void EnsureUnderwaterSleepLockBuffer( int modelCount );
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
    void MirrorFlagsFrom( PhysicsBodyStore& bodyStore, std::span<const PhysicsBodyRecord> bodyRecords, int modelCount );
    void EnsureVisualIdSize( int modelCount );
    void WakeModel( const PhysicsSleepWakeContext& context, int index );
    void WakeNarrowphaseBody( PhysicsBodyStore& bodyStore,
                              const ColliderStore& colliderStore,
                              const PhysicsWorldForces& worldForces,
                              std::span<PhysicsBodyRecord> bodyRecords,
                              std::span<float> timeRemaining,
                              int modelCount,
                              int sleepingIndex,
                              float dt );
    void SeedModelAsleep( int bodyCount, std::span<const PhysicsBodyRecord> bodyRecords, int index );
    void SetPhysicsSleepEnabled( bool enabled );
    bool IsPhysicsSleepEnabled() const;
    void LockUnderwaterSleeperIfReady( const PhysicsWorldForces& worldForces,
                                       PhysicsBodyStore& bodyStore,
                                       const ColliderStore& colliderStore,
                                       std::span<float> timeRemaining,
                                       int index );
    void PropagateSupport( std::span<const PhysicsBodyRecord> bodyRecords );
    void AppendPointJointSupportEdges( const PhysicsBodyStore& bodyStore,
                                       const std::vector<PointJointConstraint>& pointJointConstraints,
                                       int modelCount );
    void WakePointJointConnectedBodies( PhysicsBodyStore& bodyStore,
                                        const ColliderStore& colliderStore,
                                        const PhysicsWorldForces& worldForces,
                                        std::span<float> timeRemaining,
                                        PhysicsContactSolverStage& contactSolverStage,
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
