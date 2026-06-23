/*
File: SkullbonezSource/Physics/HumanoidRagdollPreset.h
Purpose:
  Declares named humanoid ragdoll joint presets.

Mental model:
  The public product surface is humanoid-first. Presets turn body-pair names
  such as shoulder, elbow, hip, and knee into constraint descriptors without
  requiring an author-facing generic constraint editor.

Related:
  - SkullbonezSource/Physics/HumanoidRagdollPreset.cpp
  - SkullbonezSource/Physics/Ragdoll.cpp
*/
#pragma once

#include "PhysicsConstraint.h"

namespace SkullbonezCore
{
namespace Physics
{
enum class HumanoidJointKind
{
    Neck,
    Shoulder,
    Elbow,
    Hip,
    Knee,
    Ankle,
    Wrist,
    Spine
};

struct HumanoidRagdollPreset
{
    float totalHeight = 70.0f;
    float torsoMass = 8.0f;
    float headMass = 3.0f;
    float upperArmMass = 2.0f;
    float lowerArmMass = 1.6f;
    float upperLegMass = 4.0f;
    float lowerLegMass = 3.0f;
    float jointDamping = 0.35f;
    float angularDamping = 0.28f;
    float limitSoftness = 0.15f;
};

class HumanoidRagdollPresetFactory
{
  public:
    static HumanoidRagdollPreset Default();
    static PhysicsConstraintDescriptor BuildJoint( HumanoidJointKind kind,
                                                   int bodyA,
                                                   int bodyB,
                                                   const Math::Vector::Vector3& localAnchorA,
                                                   const Math::Vector::Vector3& localAnchorB,
                                                   float scale,
                                                   uint32_t groupId,
                                                   const HumanoidRagdollPreset& preset );
};
} // namespace Physics
} // namespace SkullbonezCore
