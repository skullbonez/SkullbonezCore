/*
File: SkullbonezSource/Physics/SleepIslandSystem.h
Purpose:
  Groups supported bodies into sleep islands and decides when islands may sleep.

Mental model:
  Physics is deterministic fixed-step state update. Units, contact ownership,
  solver stages, sleep policy, and baseline-sensitive behavior are the key
  reading anchors.

Glossary:
  Broadphase: Cheap collision pass that finds object pairs worth testing more
  precisely.
  Narrowphase: Precise collision pass that computes contact points, normals,
  and penetration.
  Manifold: Set of contact points and normals describing one colliding pair.

Invariants:
  - Physics-visible behavior must remain deterministic; byte-exact baselines
  are the validation contract.

Related:
  - SkullbonezSource/Physics/SleepIslandSystem.cpp
  - Agentic/Reference/physics-overview.md
  - Agentic/Reference/comment-style-guide.md
*/
#pragma once

namespace SkullbonezCore
{
namespace GameObjects
{
class GameModelCollection;
}

namespace Physics
{
class PhysicsWorld;

class SleepIslandSystem
{
  public:
    void PropagateSupport( PhysicsWorld& world, GameObjects::GameModelCollection& collection );
};
} // namespace Physics
} // namespace SkullbonezCore
