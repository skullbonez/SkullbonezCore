/*
File: SkullbonezSource/Physics/Stages/PhysicsBroadphaseStage.h
Purpose:
  Owns fixed-step spatial broadphase storage and deterministic candidate output.

Summary:
  PhysicsBroadphaseStage rebuilds the spatial grid for one fixed tick, augments
  conservative fast sweeps, prunes pairs that cannot produce work, and exposes
  its retained candidate span to later stages. Collision-cell keys share this
  owner because they are indexed in the same broadphase coordinate system.

Glossary:
  Broadphase: Cheap collision pass that finds object pairs worth precise tests.
  Candidate pair: Normalized body-index pair that may reach narrowphase.
  Fast-sweep augmentation: Conservative segment check that protects tiny,
    high-speed bodies from depending only on grid-cell overlap.
  Collision-cell key: Deterministic diagnostic hash of a contact midpoint cell.
  Same-state oracle: Debug-only comparison that runs legacy and canonical pair
    construction from one broadphase input state before either can evolve it.

Invariants:
  - Solver-visible candidates use the P1 canonical `(minIndex, maxIndex)`
    order; rare fast-sweep additions are re-canonicalized before pruning.
  - Pruning predicates and pipeline-trace side effects keep their established
    per-pair order after that explicit canonical transition.
  - Returned spans remain valid only until the next Run or Clear call.
  - Candidate and collision-key vectors are reserved at construction and never
    grow beyond their fixed runtime capacities.

Related:
  - SkullbonezSource/Physics/Stages/PhysicsBroadphaseStage.cpp
  - SkullbonezSource/Physics/PhysicsWorld.cpp
  - Agentic/Reports/2026-07-15/physicsworld-ownership-map.md
*/
#pragma once

#include <cstdint>
#include <span>
#include <utility>
#include <vector>

#include "../PhysicsDebugData.h"
#include "../PhysicsBodyStore.h"
#include "../Ragdoll.h"
#include "../SpatialGrid.h"

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

struct PhysicsBroadphaseStageContext
{
    // Lifetime: all borrows cover one synchronous Run call. The stage retains
    // only its owned grid and bounded output buffers.
    const PhysicsBodyStore& bodyStore;
    std::span<const PhysicsBodyRecord> bodyRecords;
    PhysicsBodyHotFieldsConstView hotFields;
    std::span<const ColliderRecord> colliderRecords;
    const SkullbonezCore::Core::EngineConfig& config;
    const std::vector<PointJointConstraint>& pointJointConstraints;
    std::span<const uint8_t> sleepState;
    std::vector<PhysicsPipelineRecord>& physicsPipelineTrace;
    int modelCount = 0;
    float dt = 0.0f;
    float contactSkin = 0.0f;
    Core::Profiler* profiler = nullptr;
};

class PhysicsBroadphaseStage
{
  private:
    Math::CollisionDetection::SpatialGrid m_spatialGrid;
    std::vector<std::pair<int, int>> m_candidatePairs;
    std::vector<int64_t> m_collisionCellKeys;
#if defined( _DEBUG )
    // P1 same-state transition oracle. These buffers are construction-reserved,
    // included in Debug memory accounting, and absent from Release's canonical
    // production path.
    std::vector<std::pair<int, int>> m_pairOracleShadowPairs;
    std::vector<std::pair<int, int>> m_pairOracleNormalizedDriverPairs;
    bool m_pairOracleEnabled = false;
    bool m_pairOracleLegacyDrives = false;
    uint64_t m_pairOracleTickCount = 0;
#endif

  public:
    PhysicsBroadphaseStage();

    void ApplyRuntimeConfig( const SkullbonezCore::Core::EngineConfig& config );
    void Clear();
    void ResetTransientAfterReplayRestore();
    std::span<const std::pair<int, int>> Run( const PhysicsBroadphaseStageContext& context );

    const Math::CollisionDetection::SpatialGrid& GetSpatialGrid() const;
    float GetCellSize() const;
    std::span<const std::pair<int, int>> GetCandidatePairs() const;
    const std::vector<int64_t>& GetCollisionCellKeys() const;
    const std::vector<int64_t>& CollisionCellKeysForReplay() const;
    // Lifetime: replay restore mutates this construction-reserved buffer only
    // during the synchronous owner restore sequence; the reference is not retained.
    std::vector<int64_t>& CollisionCellKeysForReplay();
    void AppendCollisionCellKey( int64_t collisionCellKey );

    uint64_t CollectDynamicMemoryBytes() const;
    uint64_t CollectDebugAndBroadphaseMemoryBytes() const;
};
} // namespace Physics
} // namespace SkullbonezCore
