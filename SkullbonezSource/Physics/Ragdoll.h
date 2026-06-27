/*
File: SkullbonezSource/Physics/Ragdoll.h
Purpose:
  Builds simple ragdoll body sets and solves their first point-joint constraints.

Mental model:
  This is deliberately a small bridge toward a future generic constraint system.
  Ragdoll owns prefab construction, while PointJointConstraint is generic solver
  data that can later move under a broader constraint module.

Glossary:
  Point joint: Constraint that keeps two local anchors near each other.
  Slack: Allowed anchor separation before the solver applies correction.
  Preview lines: Editor-only visualization geometry for placement feedback.

Invariants:
  - Constraint order is deterministic and scene-authored.
  - Constraint bodies refer to GameModelCollection indices for the active scene.

Related:
  - SkullbonezSource/Physics/Ragdoll.cpp
  - SkullbonezSource/Physics/PhysicsWorld.cpp
*/
#pragma once

#include <cstdint>
#include <vector>

#include "../Maths/Quaternion.h"
#include "../Maths/Vector3.h"

namespace SkullbonezCore
{
namespace Environment
{
class WorldEnvironment;
}

namespace Geometry
{
class Terrain;
}

namespace GameObjects
{
class GameModelCollection;
}

namespace Physics
{
class PhysicsEngine;
class PhysicsBodyStore;

struct PointJointConstraint
{
    static constexpr uint8_t FLAG_LIMIT_NECK_SWING = 1u << 0;

    int bodyA = -1;
    int bodyB = -1;
    Math::Vector::Vector3 localAnchorA = Math::Vector::ZERO_VECTOR;
    Math::Vector::Vector3 localAnchorB = Math::Vector::ZERO_VECTOR;
    float slack = 0.25f;
    float stiffness = 0.22f;
    float damping = 0.35f;
    uint32_t groupId = 0;
    uint8_t flags = 0;
};

struct RagdollBuildOptions
{
    const char* namePrefix = "ragdoll";
    Math::Vector::Vector3 terrainPoint = Math::Vector::ZERO_VECTOR;
    Math::Orientation::Quaternion orientation = Math::Orientation::IDENTITY_QUATERNION;
    float scale = 1.0f;
    bool fixed = false;
    bool startsAsleep = false;
};

class Ragdoll
{
  public:
    static constexpr int SIMPLE_PART_COUNT = 10;

    static float DefaultEditorScale();
    static Math::Vector::Vector3 DefaultPreviewCenter( const Math::Vector::Vector3& terrainPoint,
                                                       float scale,
                                                       const Math::Orientation::Quaternion& orientation );
    static void AddPreviewLines( std::vector<float>& lineData,
                                 const Math::Vector::Vector3& terrainPoint,
                                 float scale,
                                 const Math::Orientation::Quaternion& orientation,
                                 float r,
                                 float g,
                                 float b );
    static void AddSimpleHumanoid( GameObjects::GameModelCollection& collection,
                                   PhysicsEngine& physics,
                                   Environment::WorldEnvironment& worldEnvironment,
                                   Geometry::Terrain* terrain,
                                   const RagdollBuildOptions& options );
    static void SolvePointJoints( GameObjects::GameModelCollection& collection,
                                  PhysicsBodyStore& bodyStore,
                                  const std::vector<PointJointConstraint>& constraints,
                                  const std::vector<uint8_t>& sleepState,
                                  float dt );
};
} // namespace Physics
} // namespace SkullbonezCore
