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
  Fast-sweep augmentation: Conservative segment check that protects tiny,
    high-speed bodies from depending only on grid-cell overlap.
  Collision-cell key: Deterministic diagnostic hash of a contact midpoint cell.
  Same-state oracle: Debug-only comparison that runs legacy and canonical pair
    construction from one broadphase input state before either can evolve it.
  Grid maintenance: Adds or removes only cells whose integer body range changed;
    settled bodies retain their entries without per-step reinsertion.

Invariants:
  - Solver-visible candidates use the P1 canonical `(minIndex, maxIndex)`
    order; rare fast-sweep additions are re-canonicalized before pruning.
  - Pruning predicates and pipeline-trace side effects keep their established
    per-pair order after that explicit canonical transition.
  - Returned spans remain valid only until the next Run or Clear call.
  - Candidate and collision-key lists commit scene-derived capacities before
    play and fail rather than grow during a fixed step.
  - Swept occupancy expires every step and never changes persistent membership.
  - Debug full-cell traversal preserves bounded SleepPrunedPair diagnostics;
    Profile/Release never generate sleep-only candidate work.

Related:
  - SkullbonezSource/Physics/Stages/PhysicsBroadphaseStage.cpp
  - SkullbonezSource/Physics/PhysicsWorld.cpp
  - Agentic/Reports/2026-07-15/physicsworld-ownership-map.md
  - Agentic/Reference/engine-glossary.md
*/
#pragma once

#include <cstdint>
#include <span>
#include <utility>
#include <vector>

#include "../PhysicsDebugData.h"
#include "../PhysicsBodyStore.h"
#include "../PhysicsRuntimeSettings.h"
#include "../PhysicsStageCapacity.h"
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
    PhysicsCandidatePairList m_candidatePairs { "PhysicsBroadphaseStage.candidatePairs",
                                                PhysicsCapacityReason::CandidatePairs };
    PhysicsCollisionCellKeyList m_collisionCellKeys { "PhysicsBroadphaseStage.collisionCellKeys",
                                                      PhysicsCapacityReason::CandidatePairs };
    bool m_gridMembershipSeeded = false;
    int m_gridMembershipBodyCount = 0;
    float m_largestBroadphaseRadius = 0.0f;
    bool m_largestBroadphaseRadiusValid = false;
#if defined( _DEBUG )

    // Debug-only bounded evidence for pairs now suppressed at grid emission.
    PhysicsCandidatePairList m_sleepPrunedPairs { "PhysicsBroadphaseStage.sleepPrunedPairs",
                                                  PhysicsCapacityReason::CandidatePairs };

    // P1 same-state transition oracle. These buffers are scene-load reserved,
    // included in Debug memory accounting, and absent from Release's canonical
    // production path.
    PhysicsCandidatePairList m_pairOracleShadowPairs { "PhysicsBroadphaseStage.pairOracleShadowPairs",
                                                       PhysicsCapacityReason::CandidatePairs };
    PhysicsCandidatePairList m_pairOracleNormalizedDriverPairs { "PhysicsBroadphaseStage.pairOracleNormalizedDriverPairs",
                                                                 PhysicsCapacityReason::CandidatePairs };
    bool m_pairOracleEnabled = false;
    bool m_pairOracleLegacyDrives = false;
    uint64_t m_pairOracleTickCount = 0;
#endif

  public:
    PhysicsBroadphaseStage();

    void ReserveSceneCapacity( std::size_t bodyCapacity );
    void ApplyRuntimeSettings( const BroadphaseSettings& settings );
    void Clear();
    void InvalidateBodyTopology();
    void ResetTransientAfterReplayRestore();

    // Lifetime: every argument is borrowed for this synchronous fixed-step
    // call; only the stage-owned grid and bounded result buffers are retained.
    std::span<const std::pair<int, int>> Run( const PhysicsBodyStore& bodyStore, const ColliderStore& colliderStore,
                                              const BroadphaseSettings& broadphaseSettings,
                                              std::span<const PointJointConstraint> pointJointConstraints,
                                              std::span<const uint8_t> sleepState, std::span<const int> awakeBodyIndices,
                                              PhysicsStepDiagnostics& stepDiagnostics, float dt, float contactSkin,
                                              float contactEpsilon, Core::Profiler* profiler );

    const Math::CollisionDetection::SpatialGrid& GetSpatialGrid() const;
    float GetCellSize() const;
    std::span<const std::pair<int, int>> GetCandidatePairs() const;
    std::span<const int64_t> GetCollisionCellKeys() const;
    std::span<const int64_t> CollisionCellKeysForReplay() const;

    // Lifetime: replay restore mutates this capacity-governed buffer only
    // during the synchronous owner restore sequence; the reference is not retained.
    PhysicsCollisionCellKeyList& CollisionCellKeysForReplay();
    void AppendCollisionCellKey( int64_t collisionCellKey );

    uint64_t CollectDynamicMemoryBytes() const;
    uint64_t CollectDebugAndBroadphaseMemoryBytes() const;
};
} // namespace Physics
} // namespace SkullbonezCore
