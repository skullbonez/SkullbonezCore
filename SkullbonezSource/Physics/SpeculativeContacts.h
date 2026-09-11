#pragma once
#include "ObjectContactManifold.h"

namespace SkullbonezCore::Physics
{
class PhysicsBodyStore;
class ColliderStore;

// Current geometry and force-resolved velocities determine one uniform-step
// articulation contact. A negative penetration is a gap, with independent
// surface witnesses. Both sleep wake-up and the solver use this same admission
// rule before any contact or joint warm start is applied.
bool BuildArticulatedContactManifold( const PhysicsBodyStore& bodies, const ColliderStore& colliders, float stepDuration, float contactEpsilon, int bodyA, int bodyB, ObjectContactManifold& manifold );
} // namespace SkullbonezCore::Physics
