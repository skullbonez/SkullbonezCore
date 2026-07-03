/*
File: SkullbonezSource/Physics/ObjectContactManifold.h
Purpose:
  Builds precise object/object contact manifolds for the persistent solver.

Mental model:
  Physics is deterministic fixed-step state update. Units, contact ownership,
  solver stages, sleep policy, and baseline-sensitive behavior are the key
  reading anchors.

Glossary:
  OBB (Oriented Bounding Box): Box with rotation, used for exact object-space
  collision tests.
  Broadphase: Cheap collision pass that finds object pairs worth testing more
  precisely.
  Narrowphase: Precise collision pass that computes contact points, normals,
  and penetration.
  Manifold: Set of contact points and normals describing one colliding pair.
  Contact body view: Pose-only body input used by narrowphase so the manifold
  builder does not need to borrow whole GameModel storage.
  Feature ID: Deterministic contact key used to match rows across frames for
  warm starting.
  Baumgarte bias: Positional correction term that turns penetration depth into
  a solver velocity target.

Invariants:
  - Physics-visible behavior must remain deterministic; byte-exact baselines
  are the validation contract.

Related:
  - SkullbonezSource/Physics/ObjectContactManifold.cpp
  - Agentic/Reference/physics-overview.md
  - Agentic/Reference/comment-style-guide.md
*/
#pragma once

#include <cstdint>
#include "CollisionShape.h"
#include "../Maths/Quaternion.h"
#include "../Maths/Vector3.h"

namespace SkullbonezCore
{
namespace GameObjects
{
class GameModel;
} // namespace GameObjects

namespace Physics
{
struct ObjectContactBodyView
{
    // Narrowphase contact geometry needs pose plus shape, not the whole
    // GameModel. PhysicsBodyRecord callers fill this view directly; legacy model
    // overloads below adapt at the boundary until the remaining callers migrate.
    Math::Vector::Vector3 position = Math::Vector::ZERO_VECTOR;
    Math::Orientation::Quaternion orientation;
};

// CATTO REF:
//   Catto 2005, Section 4 "Contact Model" represents each contact row with a
//   world-space contact point, a normal, penetration/separation, and the rA/rB
//   arms from each body center to the contact point. PersistentContactSolver
//   consumes exactly that row shape.
// ENGINE-SPECIFIC:
//   Catto does not prescribe this engine's 3D narrowphase. This file builds the
//   Skullbonez shape-pair manifolds that feed Catto-style rows: sphere/sphere,
//   sphere/OBB, and OBB/OBB. Feature IDs are deterministic local encodings so
//   warm-started impulses can be matched across frames.
// LAYMAN VERSION:
//   A manifold is a small contact report. It says "these two bodies are touching
//   here, push along this direction, and this is how deep the overlap is." The
//   solver later turns each point in the report into one rule it can enforce.
struct ObjectContactPoint
{
    Math::Vector::Vector3 point = Math::Vector::ZERO_VECTOR;  // World-space representative contact point.
    Math::Vector::Vector3 rA = Math::Vector::ZERO_VECTOR;     // Contact arm from body A center; Catto Equations 9-11.
    Math::Vector::Vector3 rB = Math::Vector::ZERO_VECTOR;     // Contact arm from body B center; Catto Equations 9-11.
    float penetration = 0.0f;                                 // Positive overlap depth used by Baumgarte bias.
    uint32_t featureId = 0;                                   // Stable local feature key for temporal coherence.
};

struct ObjectContactManifold
{
    int bodyA = -1;                                           // Solver index for body A; -1 means the row is not populated.
    int bodyB = -1;                                           // Solver index for body B; object manifolds require two live bodies.
    Math::Vector::Vector3 normal = Math::Vector::ZERO_VECTOR; // Points from body A toward body B.
    ObjectContactPoint points[4];                             // Up to four face contacts, matching box face clipping.
    uint8_t pointCount = 0;
};

bool BuildObjectContactManifold( const GameObjects::GameModel& a,
                                 const GameObjects::GameModel& b,
                                 int bodyA,
                                 int bodyB,
                                 float contactSkin,
                                 ObjectContactManifold& out );
bool BuildObjectContactManifold( const GameObjects::GameModel& a,
                                 const Math::CollisionDetection::CollisionShape& shapeA,
                                 const GameObjects::GameModel& b,
                                 const Math::CollisionDetection::CollisionShape& shapeB,
                                 int bodyA,
                                 int bodyB,
                                 float contactSkin,
                                 ObjectContactManifold& out );
bool BuildObjectContactManifold( const ObjectContactBodyView& a,
                                 const Math::CollisionDetection::CollisionShape& shapeA,
                                 const ObjectContactBodyView& b,
                                 const Math::CollisionDetection::CollisionShape& shapeB,
                                 int bodyA,
                                 int bodyB,
                                 float contactSkin,
                                 ObjectContactManifold& out );
} // namespace Physics
} // namespace SkullbonezCore
