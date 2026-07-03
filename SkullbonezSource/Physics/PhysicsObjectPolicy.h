/*
File: SkullbonezSource/Physics/PhysicsObjectPolicy.h
Purpose:
  Names the small physics policy values copied onto simulated objects.

Mental model:
  Runtime config is process input. Physics object policy is the stable set of
  scalar values a body or contact path reads after configuration has been
  applied.

Glossary:
  Physics material: Per-object friction and drag coefficients consumed by the
    body integrator, collision shape, and fluid-force cache.
  Body simulation limit: Scalar cap enforced by a body before solver rows see
    velocity state.
  Contact policy: Geometry thresholds that decide when terrain is close enough
    to count as contact and when bounce response may be applied.

Invariants:
  - Defaults must match EngineConfig defaults until config application replaces
    them, because models can be constructed before the collection receives
    runtime config.
  - These structs are value policy, not owners; copying them must not allocate or
    reach back into global configuration.

Related:
  - SkullbonezSource/GameObjects/GameModel.h
  - SkullbonezSource/GameObjects/GameModelCollection.h
  - Agentic/Reference/comment-style-guide.md
*/
#pragma once


namespace SkullbonezCore
{
namespace Basics
{
class EngineConfig;
} // namespace Basics

namespace Physics
{
struct PhysicsMaterial
{
    float frictionCoefficient = 0.1f;
    float sphereDragCoefficient = 0.4f;

    static PhysicsMaterial FromConfig( const Basics::EngineConfig& config );
};

struct BodySimulationLimits
{
    float angularVelocityLimit = 5.0f;

    static BodySimulationLimits FromConfig( const Basics::EngineConfig& config );
};

struct ContactPolicy
{
    float contactEpsilon = 0.05f;
    float terrainContactThreshold = 0.15f;
    float restitutionThreshold = 2.0f;

    static ContactPolicy FromConfig( const Basics::EngineConfig& config );
};
} // namespace Physics
} // namespace SkullbonezCore
