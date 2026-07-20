/*
File: SkullbonezSource/Physics/PhysicsObjectPolicy.cpp
Purpose:
  Converts stamped runtime settings into small per-object physics policy values.

Summary:
  Physics runtime settings are the owner-local snapshot. Physics object policy
  is the narrow value vocabulary copied into body and collider descriptors
  before simulation stores consume it.

Glossary:
  Physics material: Friction and drag coefficients used by body/collider rows.
  Body simulation limit: Scalar caps applied before solver rows see velocity.
  Contact policy: Terrain/contact thresholds shared by body-store force logic.

Invariants:
  - These helpers allocate nothing and read only supplied Physics settings.
  - The returned structs are value policy, not ownership handles.

Related:
  - SkullbonezSource/Physics/PhysicsObjectPolicy.h
  - SkullbonezSource/Physics/PhysicsBodyStore.h
  - SkullbonezSource/Physics/ColliderStore.h
  - Agentic/Reference/comment-style-guide.md
*/
#include "PhysicsObjectPolicy.h"


namespace SkullbonezCore
{
namespace Physics
{
PhysicsMaterial PhysicsMaterial::FromSettings( const PhysicsMaterialSettings& settings )
{
    PhysicsMaterial material;
    material.frictionCoefficient = settings.terrainFrictionCoefficient;
    material.sphereDragCoefficient = settings.sphereDragCoefficient;
    return material;
}

BodySimulationLimits BodySimulationLimits::FromSettings( const BodySimulationSettings& settings )
{
    BodySimulationLimits limits;
    limits.angularVelocityLimit = settings.angularVelocityLimit;
    return limits;
}

ContactPolicy ContactPolicy::FromSettings( const BodySimulationSettings& bodySettings,
                                           const TerrainContactSettings& terrainSettings )
{
    ContactPolicy policy;
    policy.contactEpsilon = bodySettings.contactEpsilon;
    policy.terrainContactThreshold = terrainSettings.threshold;
    policy.restitutionThreshold = bodySettings.contactRestitutionThreshold;
    return policy;
}
} // namespace Physics
} // namespace SkullbonezCore
