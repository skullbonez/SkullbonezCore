/*
File: SkullbonezSource/Physics/HumanoidRagdollPreset.cpp
Purpose:
  Builds default humanoid ragdoll constraint descriptors.

Mental model:
  The preset intentionally favors stable, funny physical behavior over exact
  anatomy. Shoulders and hips get broad cone-twist joints, elbows and knees get
  asymmetric hinges, and low-priority wrists/ankles stay loose ball sockets.

Related:
  - SkullbonezSource/Physics/HumanoidRagdollPreset.h
  - Agentic/Plans/humanoid-ragdoll-constraints-plan.md
*/
#include "HumanoidRagdollPreset.h"

#include <cstdio>

using SkullbonezCore::Math::Vector::Vector3;
using SkullbonezCore::Physics::ConstraintAxisMode;
using SkullbonezCore::Physics::HumanoidJointKind;
using SkullbonezCore::Physics::HumanoidRagdollPreset;
using SkullbonezCore::Physics::HumanoidRagdollPresetFactory;
using SkullbonezCore::Physics::PhysicsConstraintDescriptor;
using SkullbonezCore::Physics::PhysicsConstraintLimit;
using SkullbonezCore::Physics::PhysicsConstraintType;

namespace
{
constexpr float DEG_TO_RAD = 0.01745329251994329577f;

PhysicsConstraintLimit LimitDegrees( float minDegrees, float maxDegrees, float softness, float damping )
{
    PhysicsConstraintLimit limit;
    limit.minValue = minDegrees * DEG_TO_RAD;
    limit.maxValue = maxDegrees * DEG_TO_RAD;
    limit.softness = softness;
    limit.damping = damping;
    return limit;
}

const char* JointName( HumanoidJointKind kind )
{
    switch ( kind )
    {
    case HumanoidJointKind::Neck:
        return "neck";
    case HumanoidJointKind::Shoulder:
        return "shoulder";
    case HumanoidJointKind::Elbow:
        return "elbow";
    case HumanoidJointKind::Hip:
        return "hip";
    case HumanoidJointKind::Knee:
        return "knee";
    case HumanoidJointKind::Ankle:
        return "ankle";
    case HumanoidJointKind::Wrist:
        return "wrist";
    case HumanoidJointKind::Spine:
        return "spine";
    default:
        return "joint";
    }
}
} // namespace

HumanoidRagdollPreset HumanoidRagdollPresetFactory::Default()
{
    return HumanoidRagdollPreset();
}


PhysicsConstraintDescriptor HumanoidRagdollPresetFactory::BuildJoint( HumanoidJointKind kind,
                                                                      int bodyA,
                                                                      int bodyB,
                                                                      const Vector3& localAnchorA,
                                                                      const Vector3& localAnchorB,
                                                                      float scale,
                                                                      uint32_t groupId,
                                                                      const HumanoidRagdollPreset& preset )
{
    PhysicsConstraintDescriptor descriptor;
    descriptor.bodyA = bodyA;
    descriptor.bodyB = bodyB;
    descriptor.localAnchorA = localAnchorA;
    descriptor.localAnchorB = localAnchorB;
    descriptor.slack = 0.04f * scale;
    descriptor.stiffness = 0.24f;
    descriptor.damping = preset.jointDamping;
    descriptor.angularDamping = preset.angularDamping;
    descriptor.groupId = groupId;
    descriptor.stableId = ( groupId * 31u ) + static_cast<uint32_t>( kind );
    descriptor.breakEnabled = false;
    descriptor.motorEnabled = false;
    descriptor.breakImpulseThreshold = 0.0f;
    sprintf_s( descriptor.debugName, sizeof( descriptor.debugName ), "humanoid_%s_%u", JointName( kind ), groupId );

    switch ( kind )
    {
    case HumanoidJointKind::Elbow:
        descriptor.type = PhysicsConstraintType::Hinge;
        descriptor.localAxisA = Vector3( 0.0f, 0.0f, 1.0f );
        descriptor.localAxisB = Vector3( 0.0f, 0.0f, 1.0f );
        descriptor.localSecondaryAxisA = Vector3( 0.0f, 1.0f, 0.0f );
        descriptor.localSecondaryAxisB = Vector3( 0.0f, 1.0f, 0.0f );
        descriptor.angularLimits[0] = LimitDegrees( -8.0f, 132.0f, preset.limitSoftness, preset.angularDamping );
        break;
    case HumanoidJointKind::Knee:
        descriptor.type = PhysicsConstraintType::Hinge;
        descriptor.localAxisA = Vector3( 0.0f, 0.0f, 1.0f );
        descriptor.localAxisB = Vector3( 0.0f, 0.0f, 1.0f );
        descriptor.localSecondaryAxisA = Vector3( 0.0f, 1.0f, 0.0f );
        descriptor.localSecondaryAxisB = Vector3( 0.0f, 1.0f, 0.0f );
        descriptor.angularLimits[0] = LimitDegrees( -4.0f, 118.0f, preset.limitSoftness, preset.angularDamping );
        break;
    case HumanoidJointKind::Shoulder:
        descriptor.type = PhysicsConstraintType::ConeTwist;
        descriptor.localAxisA = Vector3( 0.0f, 1.0f, 0.0f );
        descriptor.localAxisB = Vector3( 0.0f, 1.0f, 0.0f );
        descriptor.angularLimits[0] = LimitDegrees( 0.0f, 76.0f, preset.limitSoftness, preset.angularDamping );
        descriptor.angularLimits[1] = LimitDegrees( -42.0f, 42.0f, preset.limitSoftness, preset.angularDamping );
        break;
    case HumanoidJointKind::Hip:
        descriptor.type = PhysicsConstraintType::ConeTwist;
        descriptor.localAxisA = Vector3( 0.0f, 1.0f, 0.0f );
        descriptor.localAxisB = Vector3( 0.0f, 1.0f, 0.0f );
        descriptor.angularLimits[0] = LimitDegrees( 0.0f, 54.0f, preset.limitSoftness, preset.angularDamping );
        descriptor.angularLimits[1] = LimitDegrees( -28.0f, 28.0f, preset.limitSoftness, preset.angularDamping );
        break;
    case HumanoidJointKind::Neck:
        descriptor.type = PhysicsConstraintType::ConeTwist;
        descriptor.localAxisA = Vector3( 0.0f, 1.0f, 0.0f );
        descriptor.localAxisB = Vector3( 0.0f, 1.0f, 0.0f );
        descriptor.angularLimits[0] = LimitDegrees( 0.0f, 34.0f, preset.limitSoftness, preset.angularDamping );
        descriptor.angularLimits[1] = LimitDegrees( -24.0f, 24.0f, preset.limitSoftness, preset.angularDamping );
        break;
    case HumanoidJointKind::Ankle:
    case HumanoidJointKind::Wrist:
    case HumanoidJointKind::Spine:
    default:
        descriptor.type = PhysicsConstraintType::BallSocket;
        descriptor.angularDamping = preset.angularDamping * 0.75f;
        break;
    }

    descriptor.linearAxisModes = { ConstraintAxisMode::Locked, ConstraintAxisMode::Locked, ConstraintAxisMode::Locked };
    return descriptor;
}
