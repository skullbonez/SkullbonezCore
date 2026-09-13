/*
File: SkullbonezSource/Physics/Stages/PhysicsBroadphaseStage.h
Purpose:
  Owns fixed-step spatial broadphase storage and deterministic candidate output.

Summary:
  PhysicsBroadphaseStage incrementally maintains persistent spatial membership
  during one fixed tick, overlays policy-selected linear sweeps and angular
  reach, visits only cells reached by awake bodies in production, and exposes
  its retained candidate span to later stages. Collision-cell keys share this
  owner because they use the same coordinates.

Glossary:
  Fast-sweep augmentation: Conservative segment check that protects tiny,
    high-speed bodies from depending only on grid-cell overlap.
  Collision-cell key: Deterministic diagnostic hash of a contact midpoint cell.
  Grid maintenance: Adds or removes only cells whose integer body range changed;
    settled bodies retain their entries without per-step reinsertion.

Invariants:
  - Solver-visible candidates use canonical `(minIndex, maxIndex)`
    order; rare fast-sweep additions are re-canonicalized before pruning.
  - Pruning predicates and pipeline-trace side effects keep their established
    per-pair order after that explicit canonical transition.
  - Returned spans remain valid only until the next Run or Clear call.
  - Candidate and collision-key lists commit scene-derived capacities before
    play and fail rather than grow during a fixed step.
  - Linear-swept and angular-expanded occupancy expires every step and never
    changes persistent membership.
  - Debug full-cell traversal preserves bounded SleepPrunedPair diagnostics;
    Profile/Release never generate sleep-only candidate work.

Related:
  - SkullbonezSource/Physics/Stages/PhysicsBroadphaseStage.cpp
  - SkullbonezSource/Physics/PhysicsWorld.cpp
  - Agentic/Reference/engine-glossary.md
*/
#pragma once

#include <cstdint>
#include <span>
#include <utility>

#include "../PhysicsDebugData.h"
#include "../PhysicsBodyStore.h"
#include "../PhysicsBroadphaseStepValues.h"
#include "../PhysicsRuntimeSettings.h"
#include "../PhysicsStageCapacity.h"
#include "../Ragdoll.h"
#include "../SpatialGrid.h"

namespace SkullbonezCore
{
namespace Physics
{
class ColliderStore;
class PhysicsBodyStore;
struct ColliderRecord;
struct PhysicsBodyRecord;
class PhysicsPipelineTraceRecorder;
class BroadphasePairFilter;

class PhysicsBroadphaseStage
{
  public:
    // Invariant: Run starts one step's work counts; contact refresh adds joint
    // resolutions and replaces its key count. Clear/topology/restore reset all
    // counters. Scratch bytes are capacity, while geometry counts evaluations.
    struct WorkStats
    {
        uint64_t sweepMovers = 0;
        uint64_t sweepTargets = 0;
        uint64_t sweepGeometryBodies = 0;
        uint64_t sweepPairProbes = 0;
        uint64_t sweepQueryNodes = 0;
        uint64_t sweepFullScanMovers = 0;
        uint64_t sweepScratchBytes = 0;
        uint64_t jointEndpointResolutions = 0;
        uint64_t jointExclusionKeys = 0;
    };

  private:
    // Invariant: one row copies the exact current collider center and shape
    // radius used by the original sweep predicate, without body-radius inflation.
    struct SweepGeometry
    {
        Math::Vector::Vector3 center;
        float radius = 0.0f;
    };

    // Invariant: supported leaves enclose the whole swept sphere on each axis;
    // parents enclose both children. Empty leaves use low=+inf, high=-inf.
    struct SweepBounds
    {
        double low[3] = {};
        double high[3] = {};
    };

    // Invariant: scratch is rebuilt from current dense rows whenever a sweep
    // runs. SceneLoad commits bounded backing; replay/compaction never reuse
    // cached body identity. Pair membership is seeded from this step's grid.
    PhysicsBodyRowList<SweepGeometry> m_sweepGeometry { "PhysicsBroadphaseStage.sweepGeometry", PhysicsCapacityReason::SceneBodies };
    PhysicsFixedList<uint64_t, PHYSICS_MAX_CANDIDATE_PAIRS * 2u> m_sweepPairs { "PhysicsBroadphaseStage.sweepPairs", "Power-of-two sweep membership at twice the candidate capacity" };
    WorkStats m_workStats;
    PhysicsBodyRowList<std::pair<int, int>> m_jointPairs { "PhysicsBroadphaseStage.jointPairs", PhysicsCapacityReason::PointJoints };
    void PrepareJointExclusions( const PhysicsBodyStore& bodies, std::span<const PointJointConstraint> joints );
    void PruneJointPairs( PhysicsCandidatePairList& pairs ) const;
    PhysicsBodyRowList<int> m_sweepOrder { "PhysicsBroadphaseStage.sweepOrder", PhysicsCapacityReason::SceneBodies };
    PhysicsFixedList<SweepBounds, PHYSICS_MAX_BODY_ROWS * 2u> m_sweepTree { "PhysicsBroadphaseStage.sweepTree", "Complete binary swept-bounds tree over scene body rows" };
    std::size_t m_sweepLeafBase = 0;
    bool SweepBoundsForBody( const PhysicsBodyHotFieldsConstView& hotFields, BroadphaseSweepContactEnvelope envelope, int body, SweepBounds& bounds ) const;
    bool PrepareSweepQuery( const PhysicsBodyHotFieldsConstView& hotFields, BroadphaseSweepContactEnvelope envelope );
    void AppendSweepTarget( const BroadphasePairFilter& filter, const PhysicsBodyHotFieldsConstView& hotFields, BroadphaseSweepContactEnvelope envelope, int moving, int target );
    void QuerySweepTargets( const BroadphasePairFilter& filter, const PhysicsBodyHotFieldsConstView& hotFields, BroadphaseSweepContactEnvelope envelope, int moving );
    bool MarkSweepPairFirstSeen( int a, int b );
    bool SweepTouches( const PhysicsBodyHotFieldsConstView& hotFields, int movingIndex, int targetIndex, BroadphaseSweepContactEnvelope envelope ) const;
    bool AppendFastSmallSweepPairs( const BroadphasePairFilter& pairFilter,
                                    const PhysicsBodyHotFieldsConstView& hotFields,
                                    std::span<const ColliderRecord> colliders,
                                    BroadphaseBodyActivityView activity,
                                    BroadphaseSweepContactEnvelope envelope );
    Math::CollisionDetection::SpatialGrid m_spatialGrid;
    PhysicsCandidatePairList m_candidatePairs { "PhysicsBroadphaseStage.candidatePairs", PhysicsCapacityReason::CandidatePairs };
    PhysicsCollisionCellKeyList m_collisionCellKeys { "PhysicsBroadphaseStage.collisionCellKeys", PhysicsCapacityReason::CandidatePairs };
    bool m_gridMembershipSeeded = false;
    int m_gridMembershipBodyCount = 0;
    float m_configuredCellSize = 24.0f;
    float m_largestBroadphaseRadius = 0.0f;
    bool m_largestBroadphaseRadiusValid = false;
#if defined( _DEBUG )
    // Debug-only bounded evidence for pairs now suppressed at grid emission.
    PhysicsCandidatePairList m_sleepPrunedPairs { "PhysicsBroadphaseStage.sleepPrunedPairs", PhysicsCapacityReason::CandidatePairs };
#endif

  public:
    PhysicsBroadphaseStage();

    const WorkStats& Stats() const noexcept
    {
        return m_workStats;
    }

    void ReserveSceneCapacity( std::size_t bodyCapacity, std::size_t pointJointCapacity = 0u );
    void ApplyRuntimeSettings( const BroadphaseSettings& settings );
    void Clear();
    void InvalidateBodyTopology();
    void ResetTransientAfterReplayRestore();

    // Lifetime: every argument is borrowed for this synchronous fixed-step
    // call; only the stage-owned grid and bounded result buffers are retained.
    std::span<const std::pair<int, int>> Run( const PhysicsBodyStore& bodyStore,
                                              const ColliderStore& colliderStore,
                                              std::span<const PointJointConstraint> pointJointConstraints,
                                              BroadphaseBodyActivityView activity,
                                              BroadphaseSweepContactEnvelope envelope,
                                              PhysicsPipelineTraceRecorder& physicsPipelineTrace );

    // Re-query resident membership after same-tick release/wake, without another
    // movement or CCD pass. Dormant members retain valid grid occupancy.
    std::span<const std::pair<int, int>>
    RefreshCurrentContacts( const PhysicsBodyStore& bodies, const ColliderStore& colliders, std::span<const PointJointConstraint> joints, BroadphaseBodyActivityView activity, float contactEpsilon );
    const Math::CollisionDetection::SpatialGrid& GetSpatialGrid() const;
    float GetCellSize() const;
    std::span<const std::pair<int, int>> GetCandidatePairs() const;
    std::span<const int64_t> GetCollisionCellKeys() const;
    std::span<const int64_t> CollisionCellKeysForReplay() const;

    // Lifetime: replay restore mutates this capacity-governed buffer only
    // during the synchronous owner restore sequence; the reference is not retained.
    PhysicsCollisionCellKeyList& CollisionCellKeysForReplay();
    std::size_t CollisionCellKeyCapacityForReplay() const noexcept;
    void AppendCollisionCellKey( int64_t collisionCellKey );

    uint64_t CollectDynamicMemoryBytes() const;
    uint64_t CollectDebugAndBroadphaseMemoryBytes() const;
};
} // namespace Physics
} // namespace SkullbonezCore
