/*
File: SkullbonezSource/Physics/PhysicsObjectPolicy.cpp
Purpose:
  Converts runtime config into small per-object physics policy values.

Summary:
  Runtime config is broad process input. Physics object policy is the narrow
  value vocabulary copied into body and collider descriptors before simulation
  stores consume it.

Glossary:
  Physics material: Friction and drag coefficients used by body/collider rows.
  Body simulation limit: Scalar caps applied before solver rows see velocity.
  Contact policy: Terrain/contact thresholds shared by body-store force logic.

Invariants:
  - These helpers allocate nothing and read only the supplied config snapshot.
  - The returned structs are value policy, not ownership handles.

Related:
  - SkullbonezSource/Physics/PhysicsObjectPolicy.h
  - SkullbonezSource/Physics/PhysicsBodyStore.h
  - SkullbonezSource/Physics/ColliderStore.h
  - Agentic/Reference/comment-style-guide.md
*/
#include "PhysicsObjectPolicy.h"
#include "../Core/Config.h"


namespace SkullbonezCore
{
namespace Physics
{
PhysicsMaterial PhysicsMaterial::FromConfig( const SkullbonezCore::Core::EngineConfig& config )
{
    PhysicsMaterial material;
    material.frictionCoefficient = config.physicsMaterial.frictionCoeff;
    material.sphereDragCoefficient = config.physicsMaterial.sphereDragCoeff;
    return material;
}

BodySimulationLimits BodySimulationLimits::FromConfig( const SkullbonezCore::Core::EngineConfig& config )
{
    BodySimulationLimits limits;
    limits.angularVelocityLimit = config.bodySimulation.velocityLimit;
    return limits;
}

ContactPolicy ContactPolicy::FromConfig( const SkullbonezCore::Core::EngineConfig& config )
{
    ContactPolicy policy;
    policy.contactEpsilon = config.bodySimulation.contactEpsilon;
    policy.terrainContactThreshold = config.terrainContact.threshold;
    policy.restitutionThreshold = config.bodySimulation.contactRestitutionThreshold;
    return policy;
}
} // namespace Physics
} // namespace SkullbonezCore
