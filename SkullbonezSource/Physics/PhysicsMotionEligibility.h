/*
File: SkullbonezSource/Physics/PhysicsMotionEligibility.h
Purpose:
  Defines the deterministic motion-path classification value contract.

Summary:
  The shipping policy compares travel with the first collider-local separating-
  axis boundary reached by the tick displacement while retaining stage-owned bits.
  Diagnostics identify the active policy and publish bounded work counts.

Invariants:
  - Linear promotion uses sphere radius, box half-extents, or cached hull
    centered-difference SAT axes.
  - Travel below every reached local-axis boundary demotes; stationary bodies demote.
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
#include <span>

namespace SkullbonezCore::Physics
{
inline constexpr uint32_t PHYSICS_MOTION_ELIGIBILITY_POLICY_VERSION = 4u;
inline constexpr float PHYSICS_ANGULAR_EXPANSION_THRESHOLD_THICKNESS_FACTOR = 0.5f;
static_assert( PHYSICS_ANGULAR_EXPANSION_THRESHOLD_THICKNESS_FACTOR > 0.0f );

enum PhysicsMotionEligibilityBit : uint8_t
{
    PhysicsMotionEligibilityNone = 0u,
    PhysicsMotionEligibilityLinearPromoted = 1u << 0,
    PhysicsMotionEligibilityAngularExpanded = 1u << 1,
    // Transient collision-path bit, derived from valid point-joint membership
    // each tick. It is deliberately excluded from the replay hysteresis mask.
    PhysicsMotionEligibilityArticulated = 1u << 2,
    PhysicsMotionEligibilitySpeculativeDisabled = 1u << 3, // Validation-only A/B; uniform joint stepping remains active.
};

inline constexpr uint8_t PHYSICS_MOTION_ELIGIBILITY_VALID_BITS = PhysicsMotionEligibilityLinearPromoted |
                                                                 PhysicsMotionEligibilityAngularExpanded;

inline bool UsesArticulatedContacts( std::span<const uint8_t> paths, int body )
{
    return body >= 0 && body < static_cast<int>( paths.size() ) &&
           ( paths[body] & PhysicsMotionEligibilityArticulated ) != 0u;
}

inline bool UsesSpeculativeContacts( std::span<const uint8_t> paths, int body )
{
    return UsesArticulatedContacts( paths, body ) && ( paths[body] & PhysicsMotionEligibilitySpeculativeDisabled ) == 0u;
}

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
    uint64_t articulationDurationNanoseconds = 0u;
    int articulatedBodies = 0;
    int speculativeBodies = 0;
    bool speculativeEnabled = true;
};
} // namespace SkullbonezCore::Physics
