/*
File: SkullbonezSource/Physics/PersistentContactSolver.h
Purpose:
  Defines persistent-contact step policy, cache, and body-scratch values.

Summary:
  PhysicsContactSolverStage uses these compact values while it solves
  object/object and object/terrain rows. The values carry no owner authority,
  callback, or retained borrow across a solve.

Glossary:
  Broadphase: Cheap collision pass that finds object pairs worth testing more
  precisely.
  Narrowphase: Precise collision pass that computes contact points, normals,
  and penetration.
  Manifold: Set of contact points and normals describing one colliding pair.
  Step policy: Normalized scalar limits borrowed by every contact row in one
    solver invocation.

Invariants:
  - Physics-visible behavior must remain deterministic; byte-exact baselines
    are the validation contract.
  - Raw runtime settings normalize once at the solve boundary; row loops must
    not independently reinterpret authored lower or upper bounds.

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
struct PersistentContactSolverStepPolicy
{
    float objectSlop = 0.0f;
    float objectBaumgarteBeta = 0.0f;
    float objectPositionCorrectionPercent = 0.0f;
    float terrainSlop = 0.0f;
    float terrainBaumgarteBeta = 0.0f;
    float maxBaumgarteBias = 0.0f;
    float contactRestitutionThreshold = 0.0f;    // Effective row threshold; elastic policy may force zero.
    float rawContactRestitutionThreshold = 0.0f; // Authored threshold retained for motion admission.
    float objectFrictionCoefficient = 0.0f;
    float terrainFrictionCoefficient = 0.0f;
    float rollingFrictionCoefficient = 0.0f;
    float sleepLinearSpeed = 0.0f;               // Raw authored values used by legacy relative-motion limits.
    float sleepAngularSpeed = 0.0f;
    float nonNegativeSleepLinearSpeed = 0.0f;    // Normalized values used by the quiet-body gate.
    float nonNegativeSleepAngularSpeed = 0.0f;
    float gravityMagnitude = 0.0f;
    float contactEpsilon = 0.0f;
    int iterations = 1;
    bool elasticCollisions = false;
};

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
    // back to the authoritative hot-field spans after the solve.
    Math::Vector::Vector3 linearVelocity = Math::Vector::ZERO_VECTOR;
    Math::Vector::Vector3 angularVelocity = Math::Vector::ZERO_VECTOR;
    Math::Vector::Vector3 invInertia = Math::Vector::ZERO_VECTOR;
    Math::Transformation::RotationMatrix orientation;
    float invMass = 0.0f;
    bool useWorldInertia = false;
};
} // namespace Physics
} // namespace SkullbonezCore
