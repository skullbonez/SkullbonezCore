/*
File: SkullbonezSource/Physics/PhysicsConstraint.h
Purpose:
  Declares authored physics constraints and runtime solver state.

Mental model:
  Constraint descriptors are authoring data. They name two bodies, local anchors,
  local joint frames, limits, and damping. The solver rebuilds world-space rows
  from those local values every fixed tick so rotated or editor-placed ragdolls
  do not drift away from their authored joints.

Invariants:
  - Descriptor order is solver order; keep it deterministic.
  - Body indices are GameModelCollection indices for the active scene.

Related:
  - SkullbonezSource/Physics/PhysicsConstraint.cpp
  - SkullbonezSource/Physics/PhysicsConstraintSolver.cpp
  - Agentic/Plans/humanoid-ragdoll-constraints-plan.md
*/
#pragma once

#include <array>
#include <cstdint>

#include "../Maths/Vector3.h"

namespace SkullbonezCore
{
namespace Physics
{
enum class PhysicsConstraintType
{
    BallSocket,
    Hinge,
    ConeTwist,
    Slider,
    SixDof
};

enum class ConstraintAxisMode
{
    Free,
    Locked,
    Limited
};

struct PhysicsConstraintLimit
{
    float minValue = 0.0f;
    float maxValue = 0.0f;
    float softness = 0.0f;
    float damping = 0.0f;
    float restitution = 0.0f;
};

struct PointJointConstraint
{
    int bodyA = -1;
    int bodyB = -1;
    Math::Vector::Vector3 localAnchorA = Math::Vector::ZERO_VECTOR;
    Math::Vector::Vector3 localAnchorB = Math::Vector::ZERO_VECTOR;
    float slack = 0.25f;
    float stiffness = 0.22f;
    float damping = 0.35f;
    uint32_t groupId = 0;
    bool solverEnabled = true;
};

struct PhysicsConstraintDescriptor
{
    PhysicsConstraintType type = PhysicsConstraintType::BallSocket;
    int bodyA = -1;
    int bodyB = -1;
    Math::Vector::Vector3 localAnchorA = Math::Vector::ZERO_VECTOR;
    Math::Vector::Vector3 localAnchorB = Math::Vector::ZERO_VECTOR;
    Math::Vector::Vector3 localAxisA = Math::Vector::Vector3( 0.0f, 1.0f, 0.0f );
    Math::Vector::Vector3 localAxisB = Math::Vector::Vector3( 0.0f, 1.0f, 0.0f );
    Math::Vector::Vector3 localSecondaryAxisA = Math::Vector::Vector3( 1.0f, 0.0f, 0.0f );
    Math::Vector::Vector3 localSecondaryAxisB = Math::Vector::Vector3( 1.0f, 0.0f, 0.0f );
    std::array<ConstraintAxisMode, 3> linearAxisModes = { ConstraintAxisMode::Locked,
                                                          ConstraintAxisMode::Locked,
                                                          ConstraintAxisMode::Locked };
    std::array<ConstraintAxisMode, 3> angularAxisModes = { ConstraintAxisMode::Limited,
                                                           ConstraintAxisMode::Limited,
                                                           ConstraintAxisMode::Limited };
    std::array<PhysicsConstraintLimit, 3> linearLimits = {};
    std::array<PhysicsConstraintLimit, 3> angularLimits = {};
    float slack = 0.0f;
    float stiffness = 0.22f;
    float damping = 0.35f;
    float angularDamping = 0.25f;
    uint32_t groupId = 0;
    uint32_t stableId = 0;
    bool motorEnabled = false;
    bool breakEnabled = false;
    float breakImpulseThreshold = 0.0f;
    char debugName[48] = {};
};

struct PhysicsConstraintRuntimeState
{
    float accumulatedLinearImpulse[3] = {};
    float accumulatedAngularImpulse[3] = {};
    float maxError = 0.0f;
    float maxLimitViolation = 0.0f;
    float maxAccumulatedImpulse = 0.0f;
};

const char* PhysicsConstraintTypeName( PhysicsConstraintType type );
PhysicsConstraintDescriptor MakeBallSocketConstraint( const PointJointConstraint& joint );
} // namespace Physics
} // namespace SkullbonezCore
