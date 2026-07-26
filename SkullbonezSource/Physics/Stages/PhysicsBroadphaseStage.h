/*
File: SkullbonezSource/Physics/Stages/PhysicsBroadphaseStage.h
Purpose:
  Owns fixed-step spatial broadphase storage and deterministic candidate output.

Summary:
  PhysicsBroadphaseStage incrementally maintains persistent spatial membership

  for one fixed tick, overlays conservative fast sweeps, visits only cells
  reached by awake bodies in production, and exposes its retained candidate span
  to later stages. Collision-cell keys share this owner because they use the
  same coordinates.

Glossary:
  Broadphase: Cheap collision pass that finds object pairs worth precise tests.
  Candidate pair: Normalized body-index pair that may reach narrowphase.
  Fast-sweep augmentation: Conservative segment check that protects tiny,
    high-speed bodies from depending only on grid-cell overlap.
  Collision-cell key: Deterministic diagnostic hash of a contact midpoint cell.
  Same-state oracle: Debug-only comparison that runs legacy and canonical pair
    construction from one broadphase input state before either can evolve it.
  Grid maintenance: Adds or removes only cells whose integer body range changed;
    settled bodies retain their entries without per-step reinsertion.
  Pair-source cell: Current-generation cell reached by an awake body; dormant
    membership remains resident even when the cell is not visited this step.

Invariants:
  - Solver-visible candidates use the P1 canonical `(minIndex, maxIndex)`
    order; rare fast-sweep additions are re-canonicalized before pruning.
  - Pruning predicates and pipeline-trace side effects keep their established
    per-pair order after that explicit canonical transition.
  - Returned spans remain valid only until the next Run or Clear call.
  - Candidate and collision-key vectors are reserved at construction and never
    grow beyond their fixed runtime capacities.
  - Swept occupancy expires every step and never changes persistent membership.
  - Debug full-cell traversal preserves bounded SleepPrunedPair diagnostics;
    Profile/Release never generate sleep-only candidate work.

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
#include "../PhysicsRuntimeSettings.h"
#include "../Ragdoll.h"
#include "../SpatialGrid.h"

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
class PhysicsStepDiagnostics;

class PhysicsBroadphaseStage
{
  private:
    Math::CollisionDetection::SpatialGrid m_spatialGrid;
    std::vector<std::pair<int, int>> m_candidatePairs;
    std::vector<int64_t> m_collisionCellKeys;
    bool m_gridMembershipSeeded = false;
    int m_gridMembershipBodyCount = 0;
    float m_largestBroadphaseRadius = 0.0f;
    bool m_largestBroadphaseRadiusValid = false;
#if defined( _DEBUG )

    // Debug-only bounded evidence for pairs now suppressed at grid emission.
    std::vector<std::pair<int, int>> m_sleepPrunedPairs;

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

    void ApplyRuntimeSettings( const BroadphaseSettings& settings );
    void Clear();
    void InvalidateBodyTopology();
    void ResetTransientAfterReplayRestore();

    // Lifetime: every argument is borrowed for this synchronous fixed-step
    // call; only the stage-owned grid and bounded result buffers are retained.
    std::span<const std::pair<int, int>> Run( const PhysicsBodyStore& bodyStore, const ColliderStore& colliderStore,
                                              const BroadphaseSettings& broadphaseSettings,
                                              const std::vector<PointJointConstraint>& pointJointConstraints,
                                              std::span<const uint8_t> sleepState, std::span<const int> awakeBodyIndices,
                                              PhysicsStepDiagnostics& stepDiagnostics, float dt, float contactSkin,
                                              float contactEpsilon, Core::Profiler* profiler );

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
