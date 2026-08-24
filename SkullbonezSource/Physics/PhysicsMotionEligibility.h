/*
File: SkullbonezSource/Physics/PhysicsMotionEligibility.h
Purpose:
  Defines the deterministic motion-path classification value contract.

Summary:
  One versioned absolute-travel policy classifies awake body motion before
  broadphase. The stage retains hysteresis bits; diagnostics publish bounded
  counts and the independently measured pass duration without granting mutation
  authority.

Invariants:
  - Promotion equality promotes; demotion equality demotes.
  - Per-tick demotion travel is strictly below per-tick promotion travel.
  - Thresholds are absolute metres travelled during one Physics tick and do not
    depend on collider thickness.
  - Linear promotion and angular broadphase expansion are independent bits.

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
inline constexpr float PHYSICS_MOTION_PROMOTE_TRAVEL_PER_TICK = 0.1f;
inline constexpr float PHYSICS_MOTION_DEMOTE_TRAVEL_PER_TICK = 0.075f;
static_assert( PHYSICS_MOTION_DEMOTE_TRAVEL_PER_TICK > 0.0f );
static_assert( PHYSICS_MOTION_DEMOTE_TRAVEL_PER_TICK < PHYSICS_MOTION_PROMOTE_TRAVEL_PER_TICK );

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
    uint64_t passDurationNanoseconds = 0u;
};
} // namespace SkullbonezCore::Physics
