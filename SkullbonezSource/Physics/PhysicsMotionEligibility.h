/*
File: SkullbonezSource/Physics/PhysicsMotionEligibility.h
Purpose:
  Defines the deterministic motion-path classification value contract.

Summary:
  The shipping default uses one versioned absolute-travel policy. FP4 can select
  a same-executable radius-scaled trial that compares travel with the collider's
  exact radius along the tick-travel direction while retaining the same
  stage-owned state bits.
  Diagnostics identify the active policy and publish bounded work counts.

Invariants:
  - Absolute-policy promotion equality promotes; demotion equality demotes.
  - Radius-scaled linear promotion uses sphere radius, oriented-box support, or
    bounded convex-hull min/max support in the tick-travel direction.
  - Travel below that directional radius demotes; stationary bodies demote.
  - Exact radius equality preserves the previous radius-scaled classification.
  - Angular expansion remains conservative at half the minimum thickness and
    is independent from linear promotion.

Related:
  - SkullbonezSource/Physics/Stages/PhysicsMotionEligibilityStage.cpp
  - SkullbonezTests/TestPhysicsStageState.cpp
  - Agentic/Reference/engine-glossary.md
*/
#pragma once

#include <cstdint>

namespace SkullbonezCore::Physics
{
inline constexpr uint32_t PHYSICS_MOTION_ELIGIBILITY_POLICY_VERSION = 2u;
inline constexpr uint32_t PHYSICS_RADIUS_SCALED_MOTION_ELIGIBILITY_POLICY_VERSION = 3u;
inline constexpr float PHYSICS_MOTION_PROMOTE_TRAVEL_PER_TICK = 0.1f;
inline constexpr float PHYSICS_MOTION_DEMOTE_TRAVEL_PER_TICK = 0.075f;
inline constexpr float PHYSICS_RADIUS_SCALED_THRESHOLD_THICKNESS_FACTOR = 0.5f;
static_assert( PHYSICS_MOTION_DEMOTE_TRAVEL_PER_TICK > 0.0f );
static_assert( PHYSICS_MOTION_DEMOTE_TRAVEL_PER_TICK < PHYSICS_MOTION_PROMOTE_TRAVEL_PER_TICK );
static_assert( PHYSICS_RADIUS_SCALED_THRESHOLD_THICKNESS_FACTOR > 0.0f );

enum PhysicsMotionEligibilityBit : uint8_t
{
    PhysicsMotionEligibilityNone = 0u,
    PhysicsMotionEligibilityLinearPromoted = 1u << 0,
    PhysicsMotionEligibilityAngularExpanded = 1u << 1,
};

inline constexpr uint8_t PHYSICS_MOTION_ELIGIBILITY_VALID_BITS = PhysicsMotionEligibilityLinearPromoted |
                                                                 PhysicsMotionEligibilityAngularExpanded;

struct PhysicsMotionEligibilityStats
{
    uint32_t policyVersion = PHYSICS_MOTION_ELIGIBILITY_POLICY_VERSION;
    int evaluatedBodies = 0;
    int discreteBodies = 0;
    int promotedBodies = 0;
    int angularExpandedBodies = 0;
    int promotionsThisStep = 0;
    int demotionsThisStep = 0;
    uint64_t passDurationNanoseconds = 0u;
};
} // namespace SkullbonezCore::Physics
