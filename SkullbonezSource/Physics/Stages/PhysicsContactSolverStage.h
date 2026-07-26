/*
File: SkullbonezSource/Physics/Stages/PhysicsContactSolverStage.h
Purpose:
  Owns persistent-contact rows, cache, solver scratch, statistics, and outputs.

Summary:
  PhysicsContactSolverStage owns and executes the complete persistent-row solve

  for one fixed step. It borrows dense body, collider, sleep, terrain, and
  diagnostics rows synchronously and publishes a typed consequence batch for
  the PhysicsWorld sequencer to commit.

Glossary:
  Persistent contact: Solver row retained long enough to warm-start a matching
    contact feature on the next fixed tick.
  Consequence batch: Bounded post-solve records and body indices whose foreign
    owner-side effects are committed after the hot solver pass.
  Wake access: Narrow synchronous capability that can invalidate cache rows
    without exposing the contact-solver owner to the sleep subsystem.
  Warm start: Reuse of last tick's accumulated contact impulses.

Invariants:
  - Owned lists commit scene-derived runtime capacities before play and fail
    loudly rather than grow during steady gameplay.
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
#include "../PhysicsRuntimeSettings.h"
#include "../PhysicsDebugData.h"
#include "../PhysicsStageCapacity.h"
#include "../TerrainContactManifold.h"
#include "../PhysicsSolverSnapshot.h"

namespace SkullbonezCore
{
namespace Core
{
class Profiler;
} // namespace Core

namespace Physics
{
class ColliderStore;
class PhysicsBodyStore;
struct ColliderRecord;
struct PhysicsBodyRecord;
struct PhysicsWorldForces;
class PhysicsStepDiagnostics;

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

using PersistentContactList = PhysicsFixedList<PersistentContact, PHYSICS_MAX_CONTACT_ROWS>;
using PersistentContactCacheList = PhysicsFixedList<PersistentContactCacheEntry, PHYSICS_MAX_CONTACT_ROWS>;
using PersistentContactCountList = PhysicsFixedList<uint16_t, PHYSICS_MAX_BODY_ROWS>;
using SolverBodyStateList = PhysicsFixedList<SolverBodyState, PHYSICS_MAX_BODY_ROWS>;
using PhysicsPipelineRecordList = PhysicsFixedList<PhysicsPipelineRecord, PHYSICS_MAX_PIPELINE_TRACE_RECORDS>;
using PhysicsCollisionVisualBodyList = PhysicsFixedList<int, PHYSICS_MAX_COLLISION_VISUAL_BODY_ROWS>;
using PhysicsContactBodyList = PhysicsFixedList<int, PHYSICS_MAX_CONTACT_ROWS>;
using PhysicsReleaseWakeBodyList = PhysicsFixedList<int, PHYSICS_MAX_BODY_ROWS>;
using PhysicsFixedTreeReleaseList = PhysicsFixedList<PhysicsFixedTreeReleaseEvent, PHYSICS_MAX_BODY_ROWS>;

struct PersistentContactSolverSideEffects
{

    // These are values, not callbacks: the sequencer applies them in the same
    // deterministic order after Solve returns.
    PhysicsPipelineRecordList pipelineRecords { "PhysicsContactSolverStage.pipelineRecords" };
    PhysicsCollisionVisualBodyList collisionVisualBodies { "PhysicsContactSolverStage.collisionVisualBodies" };
    PhysicsContactBodyList fixedContactBodies { "PhysicsContactSolverStage.fixedContactBodies" };
    PhysicsReleaseWakeBodyList releaseWakeBodies { "PhysicsContactSolverStage.releaseWakeBodies" };
    PhysicsFixedTreeReleaseList fixedTreeReleases { "PhysicsContactSolverStage.fixedTreeReleases" };
};

class PhysicsContactCacheWakeAccess
{
  private:
    PersistentContactCacheList& m_cache;

  public:

    // Lifetime: this narrow capability borrows the contact owner's cache only
    // for the synchronous wake operation that requested it.
    explicit PhysicsContactCacheWakeAccess( PersistentContactCacheList& cache ) : m_cache( cache )
    {
    }
    void ForgetBody( int bodyIndex ) const;
};

class PhysicsContactSolverStage
{
  private:
    PersistentContactList m_persistentContacts { "PhysicsContactSolverStage.persistentContacts" };
    PersistentContactCacheList m_persistentContactCache { "PhysicsContactSolverStage.persistentContactCache" };
    PersistentContactSolverStats m_persistentContactSolverStats;
    PersistentContactCountList m_persistentContactCounts { "PhysicsContactSolverStage.persistentContactCounts" };
    PersistentContactCountList m_persistentRestingContactCounts {
        "PhysicsContactSolverStage.persistentRestingContactCounts" };
    SolverBodyStateList m_solverBodies { "PhysicsContactSolverStage.solverBodies" };
    PersistentContactSolverSideEffects m_sideEffects;

    void PrepareSideEffects( int modelCount, std::size_t candidatePairCount, int pipelineRecordCapacity );

  public:
    PhysicsContactSolverStage();

    void Clear();
    void ReserveSceneCapacity( std::size_t bodyCapacity );

    // Returns the single per-solve normalization of raw stamped settings and
    // live world-force policy. Tests use this seam to pin bounds without
    // recreating solver math.
    static PersistentContactSolverStepPolicy ResolveStepPolicy( const PhysicsRuntimeSettings& settings,
                                                                const PhysicsWorldForces& worldForces ) noexcept;
    void Solve( PhysicsBodyStore& bodyStore, const ColliderStore& colliderStore,
                const PersistentContactSolverStepPolicy& stepPolicy, std::span<const std::pair<int, int>> candidatePairs,
                std::span<const uint8_t> sleepState, std::vector<std::pair<int, int>>& sleepSupportEdges,
                std::vector<TerrainContactManifold>& terrainContactManifolds, std::span<uint8_t> terrainRestApplied,
                std::span<uint8_t> sleepSupportedThisFrame, PhysicsStepDiagnostics& stepDiagnostics, float dt,
                Core::Profiler* profiler );
    PhysicsContactCacheWakeAccess CreateWakeAccess();

    void CaptureReplayState( PhysicsSolverSnapshot& outSnapshot ) const;
    void RestoreReplayState( const PhysicsSolverSnapshot& snapshot );

    std::span<const PersistentContact> GetPersistentContacts() const;
    std::span<const PersistentContactCacheEntry> GetPersistentContactCache() const;
    const PersistentContactSolverStats& GetStats() const;
    std::span<const uint16_t> GetPersistentContactCounts() const;
    std::span<const uint16_t> GetPersistentRestingContactCounts() const;
    const PersistentContactSolverSideEffects& GetSideEffects() const;
    uint64_t CollectDynamicMemoryBytes() const;
};
} // namespace Physics
} // namespace SkullbonezCore
