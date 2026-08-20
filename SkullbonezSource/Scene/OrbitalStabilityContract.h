/*
File: OrbitalStabilityContract.h
Purpose:
  Defines the scene-authored membership and thresholds for orbital stability.

Summary:
  Scene parsing resolves explicit object names to durable Physics identities and
  publishes one fixed-capacity value contract. Upper Runtime/Planning policy can
  consume the values without inferring membership from names, rows, colour, or mass.

Glossary:
  Blocking set: The primary plus every core orbiter; auxiliary members report
    orbital failures without ending the system-wide horizon.
  Configured member: Any primary, core, or auxiliary body named by the scene.

Invariants:
  - A valid contract has exactly one primary and distinct stable object ids.
  - Primary membership carries no radial envelope; every orbiter does.
  - The parser rejects more members than the fixed publication capacity.

Related:
  - SkullbonezSource/Scene/AuthoredScene.h
  - SkullbonezSource/Runtime/Planning/ContinuousOrbitalStability.h
  - SkullbonezData/scenes/solar_system.scene.json
*/
#pragma once

#include "../Physics/PhysicsHandles.h"

#include <array>
#include <cstddef>
#include <cstdint>

namespace SkullbonezCore::Scene
{
constexpr std::size_t ORBITAL_STABILITY_MEMBER_CAPACITY = 16u;

enum class OrbitalStabilityMemberRole : std::uint8_t
{
    Primary = 0,
    CoreOrbiter,
    Auxiliary,
};

struct OrbitalStabilityMemberContract
{
    Physics::PhysicsSceneObjectId sceneObjectId;
    char authoredObjectName[64] = {};
    OrbitalStabilityMemberRole role = OrbitalStabilityMemberRole::Auxiliary;
    double innerRadius = 0.0;
    double outerRadius = 0.0;
    double escapeStartRadius = 0.0;
};

struct OrbitalStabilityContract
{
    std::array<OrbitalStabilityMemberContract, ORBITAL_STABILITY_MEMBER_CAPACITY> members = {};
    std::size_t memberCount = 0u;
    double escapeGraceSeconds = 0.0;
    bool enabled = false;
};
} // namespace SkullbonezCore::Scene
