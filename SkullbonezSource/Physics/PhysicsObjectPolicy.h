/*
File: SkullbonezSource/Physics/PhysicsObjectPolicy.h
Purpose:
  Names the small physics policy values copied onto simulated objects.

Summary:
  Physics runtime settings are stamped process input. Physics object policy is
  the stable set of scalar values a body or contact path reads afterward.

Glossary:
  Physics material: Per-object friction and drag coefficients consumed by the
    body integrator, collision shape, and fluid-force cache.
  Body simulation limit: Scalar cap enforced by a body before solver rows see
    velocity state.
  Contact policy: Geometry thresholds that decide when terrain is close enough
    to count as contact and when bounce response may be applied.

Invariants:
  - Defaults must match PhysicsRuntimeSettings defaults until settings
    application replaces them, because models can be constructed before the
    collection receives runtime settings.
  - These structs are value policy, not owners; copying them must not allocate or
    reach back into global configuration.

Related:
  - SkullbonezSource/Physics/PhysicsBodyStore.h
  - SkullbonezSource/Physics/ColliderStore.h
  - Agentic/Reference/comment-style-guide.md
*/
#pragma once

#include "PhysicsRuntimeSettings.h"

namespace SkullbonezCore
{
namespace Physics
{
struct PhysicsMaterial
{
    float frictionCoefficient = 0.1f;
    float sphereDragCoefficient = 0.4f;

    static PhysicsMaterial FromSettings( const PhysicsMaterialSettings& settings );
};

struct BodySimulationLimits
{
    float angularVelocityLimit = 5.0f;

    static BodySimulationLimits FromSettings( const BodySimulationSettings& settings );
};

struct ContactPolicy
{
    float contactEpsilon = 0.05f;
    float terrainContactThreshold = 0.15f;
    float restitutionThreshold = 2.0f;

    static ContactPolicy
    FromSettings( const BodySimulationSettings& bodySettings, const TerrainContactSettings& terrainSettings );
};
} // namespace Physics
} // namespace SkullbonezCore
