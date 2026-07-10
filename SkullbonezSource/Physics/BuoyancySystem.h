/*
File: SkullbonezSource/Physics/BuoyancySystem.h
Purpose:
  Owns analytic fluid-submersion queries used by physics gameplay policy.

Mental model:
  PhysicsBodyStore applies continuous fluid forces during force integration.
  This owner provides the targeted sphere-water snapshot used by underwater
  sleep locks so PhysicsWorld does not own shape-specific buoyancy math.

Glossary:
  Sphere cap: Portion of a sphere below the fluid surface; its analytic volume
    gives a deterministic submerged fraction without sampling.
  Submersion snapshot: Per-body fraction cached on PhysicsBodyRecord for one
    physics decision, not an authoring value.
  Underwater sleep lock: Sleep policy that keeps fully submerged balls dormant
    so buoyancy jitter does not repeatedly wake them.

Invariants:
  - Only sphere colliders participate in the underwater sleep-lock query.
  - The submerged fraction is deterministic math over body pose, collider
    offset, and the per-tick world-force snapshot.

Related:
  - SkullbonezSource/Physics/BuoyancySystem.cpp
  - SkullbonezSource/Physics/PhysicsWorld.cpp
*/
#pragma once

namespace SkullbonezCore
{
namespace Physics
{
class ColliderStore;
class PhysicsBodyStore;
struct PhysicsBodyRecord;
struct PhysicsWorldForces;

class BuoyancySystem
{
  public:
    static bool RefreshUnderwaterSubmersionForBall( const PhysicsWorldForces& worldForces,
                                                    PhysicsBodyStore& bodyStore,
                                                    const ColliderStore& colliderStore,
                                                    int index );
    static bool
    IsFullySubmergedBall( const PhysicsBodyRecord& bodyRecord, const ColliderStore& colliderStore, int index );
};
} // namespace Physics
} // namespace SkullbonezCore
