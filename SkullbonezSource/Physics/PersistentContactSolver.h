/*
File: SkullbonezSource/Physics/PersistentContactSolver.h
Purpose:
  Solves object/object and object/terrain persistent contact rows.

Summary:
  PersistentContactSolver.h solves object/object and object/terrain persistent
  contact rows. As a public header, keep edits anchored on deterministic
  physics, diagnostics, or world-state flow and on the glossary/invariants
  below.

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
  - SkullbonezSource/Physics/PersistentContactSolver.cpp
  - Agentic/Reference/physics-overview.md
  - Agentic/Reference/comment-style-guide.md
*/
#pragma once

#include <cstdint>

#include "../Maths/RotationMatrix.h"
#include "../Maths/Vector3.h"

namespace SkullbonezCore
{
namespace Physics
{
struct PersistentContactSolverContext;

struct PersistentContactCacheEntry
{
    // Previous-frame impulse cache. The key encodes the bodies plus feature
    // id so a contact can find last tick's converged impulse even if rows
    // are rebuilt from fresh manifolds this tick.
    int64_t key = 0;
    float accN = 0.0f;
    float accT1 = 0.0f;
    float accT2 = 0.0f;
};

struct SolverBodyState
{
    // Solver scratch copy of dynamic body state. Rows iterate over this
    // compact representation first, then the final velocities are written
    // back to PhysicsBodyRecord storage after the solve.
    Math::Vector::Vector3 linearVelocity = Math::Vector::ZERO_VECTOR;
    Math::Vector::Vector3 angularVelocity = Math::Vector::ZERO_VECTOR;
    Math::Vector::Vector3 invInertia = Math::Vector::ZERO_VECTOR;
    Math::Transformation::RotationMatrix orientation;
    float invMass = 0.0f;
    bool useWorldInertia = false;
};

class PersistentContactSolver
{
  public:
    void Solve( PersistentContactSolverContext& context, float dt );
};
} // namespace Physics
} // namespace SkullbonezCore
