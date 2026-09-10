/*
File: SkullbonezSource/Physics/Stages/PhysicsMotionEligibilityStage.h
Purpose:
  Owns fixed-capacity per-body motion eligibility and cached motion bounds.

Summary:
  After force application, this stage walks dense body rows once, applies the
  versioned hysteresis policy, and publishes immutable spans to broadphase and
  diagnostics. ColliderStore owns topology-derived geometry facts; this stage
  owns tick-derived motion, joint membership, and previous classification state.

Invariants:
  - Fixed and sleeping rows are not evaluated and retain no promotion state.
    Their derived articulation membership still prevents an awake collision
    partner from advancing one connected limb independently.
  - No worker scheduling participates in classification arithmetic or order.
  - Topology invalidation clears hysteresis before a replacement row is seen.
  - Replay restores only valid state bits and never reconstructs prior state by discovery.
  - Runtime allocation policy: every per-body row is reserved at scene load and
    fixed-step classification cannot grow storage.

Related:
  - SkullbonezSource/Physics/Stages/PhysicsMotionEligibilityStage.cpp
  - SkullbonezSource/Physics/PhysicsWorld.cpp
  - SkullbonezTests/TestPhysicsStageState.cpp
  - Agentic/Reference/engine-glossary.md
*/
#pragma once

#include "../PhysicsStageCapacity.h"
#include "../PhysicsMotionEligibility.h"

#include <cstddef>
#include <cstdint>
#include <span>

namespace SkullbonezCore::Physics
{
class ColliderStore;
class PhysicsBodyStore;
struct PointJointConstraint;

class PhysicsMotionEligibilityStage
{
  public:
    void ReserveBodyCapacity( std::size_t bodyCapacity );
    void Clear();
    void InvalidateBodyTopology();
    void CommitReplayRestoreState( bool hasVersionedState );
    void Run( const PhysicsBodyStore& bodyStore, const ColliderStore& colliderStore, std::span<const uint8_t> sleepState,
              float dt, std::span<const PointJointConstraint> joints = {}, bool speculativeEnabled = true );

    std::span<const uint8_t> State() const;
    std::span<const uint8_t> CollisionPathState() const;
    std::span<const float> LinearTravelSquared() const;
    std::span<const float> LinearDirectionalBoundary() const;
    std::span<const float> AngularTravelSquared() const;
    std::span<const float> AngularBroadphaseExpansion() const;
    const PhysicsMotionEligibilityStats& Stats() const;

    PhysicsBodyRowList<uint8_t>& StateForReplay();
    std::span<const uint8_t> StateForReplay() const;
    std::size_t StateCapacityForReplay() const noexcept;
    uint64_t CollectDynamicMemoryBytes() const;

  private:
    // Tick-derived membership belongs to this stage, not the hot body store or
    // replay hysteresis. Scene-load reservation gives it the same body ceiling.
    PhysicsBodyRowList<uint8_t> m_collisionPathState { "PhysicsMotionEligibilityStage.collisionPathState",
                                                       PhysicsCapacityReason::SceneBodies };
    PhysicsBodyRowList<uint8_t> m_state { "PhysicsMotionEligibilityStage.state", PhysicsCapacityReason::SceneBodies };
    PhysicsBodyRowList<float> m_linearTravelSquared { "PhysicsMotionEligibilityStage.linearTravelSquared",
                                                      PhysicsCapacityReason::SceneBodies };
    PhysicsBodyRowList<float> m_linearDirectionalBoundary { "PhysicsMotionEligibilityStage.linearDirectionalBoundary",
                                                            PhysicsCapacityReason::SceneBodies };
    PhysicsBodyRowList<float> m_angularTravelSquared { "PhysicsMotionEligibilityStage.angularTravelSquared",
                                                       PhysicsCapacityReason::SceneBodies };
    PhysicsBodyRowList<float> m_angularBroadphaseExpansion { "PhysicsMotionEligibilityStage.angularBroadphaseExpansion",
                                                             PhysicsCapacityReason::SceneBodies };
    PhysicsMotionEligibilityStats m_stats;
    bool m_topologyInvalidated = true;
};
} // namespace SkullbonezCore::Physics
