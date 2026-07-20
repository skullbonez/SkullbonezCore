/*
File: SkullbonezSource/Physics/Stages/PhysicsContactSolverStage.h
Purpose:
  Owns persistent-contact rows, cache, solver scratch, statistics, and outputs.

Summary:
  PhysicsContactSolverStage wraps PersistentContactSolver with the complete
  feeding state for one fixed-step contact solve. The stage borrows dense body,
  collider, sleep, terrain, and diagnostics rows synchronously and publishes a
  typed consequence batch for the PhysicsWorld sequencer to commit.

Glossary:
  Persistent contact: Solver row retained long enough to warm-start a matching
    contact feature on the next fixed tick.
  Consequence batch: Bounded post-solve records and body indices whose foreign
    owner-side effects are committed after the hot solver pass.
  Wake access: Narrow synchronous capability that can invalidate cache rows
    without exposing the contact-solver owner to the sleep subsystem.
  Warm start: Reuse of last tick's accumulated contact impulses.

Invariants:
  - Owned vectors are construction-reserved and never grow past their bounds
    during steady gameplay.
  - Solve prepares a fresh consequence batch before invoking the row solver.
  - The stage retains no pointer or reference to PhysicsWorld or borrowed rows.
  - Wake propagation receives only a cache-invalidation capability, never the
    concrete contact-solver owner.

Related:
  - SkullbonezSource/Physics/Stages/PhysicsContactSolverStage.cpp
  - SkullbonezSource/Physics/PersistentContactSolver.cpp
  - SkullbonezSource/Physics/PhysicsWorld.cpp
*/
#pragma once

#include <cstdint>
#include <span>
#include <utility>
#include <vector>

#include "../PersistentContactSolver.h"
#include "../PhysicsBodyStore.h"
#include "../PhysicsDebugData.h"
#include "../TerrainContactManifold.h"
#include "../PhysicsSolverSnapshot.h"

namespace SkullbonezCore
{
namespace Core
{
class EngineConfig;
class Profiler;
} // namespace Core

namespace Physics
{
class ColliderStore;
class PhysicsBodyStore;
struct ColliderRecord;
struct PhysicsBodyRecord;
struct PhysicsWorldForces;

struct PersistentContact
{
    // One solver row for one contact point. bodyB == -1 means static terrain.
    // Accumulated impulses are cache-sensitive and validation-sensitive.
    int bodyA = -1;
    int bodyB = -1;
    uint32_t featureId = 0;
    int64_t key = 0;
    Math::Vector::Vector3 normal = Math::Vector::ZERO_VECTOR;
    Math::Vector::Vector3 tangent1 = Math::Vector::ZERO_VECTOR;
    Math::Vector::Vector3 tangent2 = Math::Vector::ZERO_VECTOR;
    Math::Vector::Vector3 rA = Math::Vector::ZERO_VECTOR;
    Math::Vector::Vector3 rB = Math::Vector::ZERO_VECTOR;
    float penetration = 0.0f;
    float normalMass = 0.0f;
    float tangentMass1 = 0.0f;
    float tangentMass2 = 0.0f;
    float bias = 0.0f;
    float frictionLimit = 0.0f;
    float accN = 0.0f;
    float accT1 = 0.0f;
    float accT2 = 0.0f;
    bool warmStarted = false;
    bool isTerrain = false;
    bool supportsRestingPolicy = true;
    bool allowsTangentFriction = true;
    bool normalCoupledFriction = false;
    bool inhibitsSleep = false;
    uint8_t manifoldPointCount = 1;
    Math::Vector::Vector3 terrainNormal = Math::Vector::ZERO_VECTOR;
    float terrainWarmStart = 0.0f;
    // Captured before impulses so diagnostics can reject force-transfer
    // rows that had no actual relative impact motion.
    float preSolveNormalSpeed = 0.0f;
    float preSolveClosingSpeed = 0.0f;
    float preSolveSlipSpeed = 0.0f;
};

struct PersistentContactSolverStats
{
    int rowCount = 0;
    int cachePreviousRows = 0;
    int cacheHits = 0;
    int cacheMisses = 0;
    int warmStartedRows = 0;
    int positionCorrectionRows = 0;
    int solverIterations = 0;
    float positionCorrectionTotal = 0.0f;
    float positionCorrectionMax = 0.0f;
};

struct PersistentContactSolverSideEffects
{
    // These are values, not callbacks: the sequencer applies them in the same
    // deterministic order after Solve returns.
    std::vector<PhysicsPipelineRecord> pipelineRecords;
    std::vector<int> collisionVisualBodies;
    std::vector<int> fixedContactBodies;
    std::vector<int> releaseWakeBodies;
    std::vector<PhysicsFixedTreeReleaseEvent> fixedTreeReleases;
};

struct PersistentContactSolverContext
{
    // Lifetime: every span/reference is borrowed for one synchronous call to
    // PersistentContactSolver::Solve and is never retained.
    std::span<const std::pair<int, int>> candidatePairs;
    std::span<const uint8_t> sleepState;
    std::vector<std::pair<int, int>>& sleepSupportEdges;
    std::vector<PersistentContact>& persistentContacts;
    std::vector<PersistentContactCacheEntry>& persistentContactCache;
    PersistentContactSolverStats& persistentContactSolverStats;
    std::vector<uint16_t>& persistentContactCounts;
    std::vector<uint16_t>& persistentRestingContactCounts;
    std::vector<SolverBodyState>& solverBodies;
    std::vector<PhysicsDebugContact>& physicsDebugContacts;
    std::vector<TerrainContactManifold>& terrainContactManifolds;
    std::span<uint8_t> terrainRestApplied;
    std::span<uint8_t> sleepSupportedThisFrame;
    PersistentContactSolverSideEffects& sideEffects;
    PhysicsBodyStore& bodyStore;
    std::span<PhysicsBodyRecord> bodyRecords;
    PhysicsBodyHotFieldsView hotFields;
    std::span<const ColliderRecord> colliderRecords;
    int bodyStoreCount = 0;
    int pipelineRecordCapacity = 0;
    bool elasticCollisions = false;
    const Core::EngineConfig& config;
    Core::Profiler* profiler = nullptr;
};

struct PhysicsContactSolverStageContext
{
    // Lifetime: the stage consumes this aggregate synchronously and retains no
    // borrowed owner state after Solve returns.
    PhysicsBodyStore& bodyStore;
    const ColliderStore& colliderStore;
    const Core::EngineConfig& config;
    const PhysicsWorldForces& worldForces;
    std::span<const std::pair<int, int>> candidatePairs;
    std::span<const uint8_t> sleepState;
    std::vector<std::pair<int, int>>& sleepSupportEdges;
    std::vector<PhysicsDebugContact>& physicsDebugContacts;
    std::vector<TerrainContactManifold>& terrainContactManifolds;
    std::span<uint8_t> terrainRestApplied;
    std::span<uint8_t> sleepSupportedThisFrame;
    std::span<PhysicsBodyRecord> bodyRecords;
    PhysicsBodyHotFieldsView hotFields;
    std::span<const ColliderRecord> colliderRecords;
    int bodyStoreCount = 0;
    int pipelineRecordCapacity = 0;
    Core::Profiler* profiler = nullptr;
};

class PhysicsContactCacheWakeAccess
{
  private:
    std::vector<PersistentContactCacheEntry>& m_cache;

  public:
    // Lifetime: this narrow capability borrows the contact owner's cache only
    // for the synchronous wake operation that requested it.
    explicit PhysicsContactCacheWakeAccess( std::vector<PersistentContactCacheEntry>& cache ) : m_cache( cache )
    {
    }
    void ForgetBody( int bodyIndex ) const;
};

class PhysicsContactSolverStage
{
  private:
    std::vector<PersistentContact> m_persistentContacts;
    std::vector<PersistentContactCacheEntry> m_persistentContactCache;
    PersistentContactSolverStats m_persistentContactSolverStats;
    std::vector<uint16_t> m_persistentContactCounts;
    std::vector<uint16_t> m_persistentRestingContactCounts;
    std::vector<SolverBodyState> m_solverBodies;
    PersistentContactSolverSideEffects m_sideEffects;
    PersistentContactSolver m_contactSolver;

    void PrepareSideEffects( int modelCount, std::size_t candidatePairCount, int pipelineRecordCapacity );

  public:
    PhysicsContactSolverStage();

    void Clear();
    void Solve( const PhysicsContactSolverStageContext& context, float dt );
    PhysicsContactCacheWakeAccess CreateWakeAccess();

    void CaptureReplayState( PhysicsSolverSnapshot& outSnapshot ) const;
    void RestoreReplayState( const PhysicsSolverSnapshot& snapshot );

    const std::vector<PersistentContact>& GetPersistentContacts() const;
    const std::vector<PersistentContactCacheEntry>& GetPersistentContactCache() const;
    const PersistentContactSolverStats& GetStats() const;
    const std::vector<uint16_t>& GetPersistentContactCounts() const;
    const std::vector<uint16_t>& GetPersistentRestingContactCounts() const;
    const PersistentContactSolverSideEffects& GetSideEffects() const;
    uint64_t CollectDynamicMemoryBytes() const;
};
} // namespace Physics
} // namespace SkullbonezCore
