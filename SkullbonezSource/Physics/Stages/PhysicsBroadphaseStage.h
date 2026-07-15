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

Invariants:
  - Candidate order, pruning order, profiler markers, and trace emission order
    match the certified P0 PhysicsWorld implementation.
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
#include "../Ragdoll.h"
#include "../SpatialGrid.h"

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
struct ColliderRecord;
struct PhysicsBodyRecord;

struct PhysicsBroadphaseStageContext
{
    // Lifetime: all borrows cover one synchronous Run call. The stage retains
    // only its owned grid and bounded output buffers.
    const PhysicsBodyStore& bodyStore;
    std::span<const PhysicsBodyRecord> bodyRecords;
    std::span<const ColliderRecord> colliderRecords;
    const SkullbonezCore::Core::EngineConfig& config;
    const std::vector<PointJointConstraint>& pointJointConstraints;
    const std::vector<uint8_t>& sleepState;
    std::vector<PhysicsPipelineRecord>& physicsPipelineTrace;
    int modelCount = 0;
    float dt = 0.0f;
    float contactSkin = 0.0f;
};

class PhysicsBroadphaseStage
{
  private:
    Math::CollisionDetection::SpatialGrid m_spatialGrid;
    std::vector<std::pair<int, int>> m_candidatePairs;
    std::vector<int64_t> m_collisionCellKeys;

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
    std::vector<int64_t>& CollisionCellKeysForReplay();
    void AppendCollisionCellKey( int64_t collisionCellKey );

    uint64_t CollectDynamicMemoryBytes() const;
    uint64_t CollectDebugAndBroadphaseMemoryBytes() const;
};
} // namespace Physics
} // namespace SkullbonezCore
