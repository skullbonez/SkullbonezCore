/*
File: SkullbonezSource/Physics/PhysicsConstraint.cpp
Purpose:
  Implements small helpers for authored physics constraint descriptors.

Mental model:
  Point joints are the old scene-authoring surface. They convert to ball-socket
  descriptors so legacy scenes and saved snapshots share the production
  constraint solver instead of a separate bespoke solve path.

Related:
  - SkullbonezSource/Physics/PhysicsConstraint.h
  - SkullbonezSource/Physics/PhysicsConstraintSolver.cpp
*/
#include "PhysicsConstraint.h"

#include <cstdio>

namespace SkullbonezCore
{
namespace Physics
{
const char* PhysicsConstraintTypeName( PhysicsConstraintType type )
{
    switch ( type )
    {
    case PhysicsConstraintType::BallSocket:
        return "ballSocket";
    case PhysicsConstraintType::Hinge:
        return "hinge";
    case PhysicsConstraintType::ConeTwist:
        return "coneTwist";
    case PhysicsConstraintType::Slider:
        return "slider";
    case PhysicsConstraintType::SixDof:
        return "sixDof";
    default:
        return "unknown";
    }
}


PhysicsConstraintDescriptor MakeBallSocketConstraint( const PointJointConstraint& joint )
{
    PhysicsConstraintDescriptor descriptor;
    descriptor.type = PhysicsConstraintType::BallSocket;
    descriptor.bodyA = joint.bodyA;
    descriptor.bodyB = joint.bodyB;
    descriptor.localAnchorA = joint.localAnchorA;
    descriptor.localAnchorB = joint.localAnchorB;
    descriptor.slack = joint.slack;
    descriptor.stiffness = joint.stiffness;
    descriptor.damping = joint.damping;
    descriptor.angularDamping = joint.damping * 0.5f;
    descriptor.groupId = joint.groupId;
    descriptor.stableId = joint.groupId;
    sprintf_s( descriptor.debugName, sizeof( descriptor.debugName ), "point_joint_%u", joint.groupId );
    return descriptor;
}
} // namespace Physics
} // namespace SkullbonezCore
