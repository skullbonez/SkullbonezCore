/*
File: SkullbonezSource/Physics/PhysicsConstraintSolver.h
Purpose:
  Declares the serial velocity-level constraint solver for ragdoll joints.

Mental model:
  The first production solver intentionally stays serial and deterministic. It
  consumes descriptors in authored order, emits simple scalar rows internally,
  and writes velocities/limited position correction back through GameModel.

Related:
  - SkullbonezSource/Physics/PhysicsConstraintSolver.cpp
  - SkullbonezSource/Physics/PhysicsConstraint.h
*/
#pragma once

#include <vector>

#include "PhysicsConstraint.h"

namespace SkullbonezCore
{
namespace GameObjects
{
class GameModelCollection;
}

namespace Physics
{
struct PhysicsConstraintSolverStats
{
    int descriptorCount = 0;
    int solvedRows = 0;
    int solverIterations = 0;
    float maxAnchorError = 0.0f;
    float maxLimitViolation = 0.0f;
    float maxImpulse = 0.0f;
};

class PhysicsConstraintSolver
{
  public:
    static PhysicsConstraintSolverStats Solve( GameObjects::GameModelCollection& collection,
                                               const std::vector<PhysicsConstraintDescriptor>& constraints,
                                               const std::vector<uint8_t>& sleepState,
                                               float dt );
};
} // namespace Physics
} // namespace SkullbonezCore
