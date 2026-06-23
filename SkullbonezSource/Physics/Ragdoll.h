/*
File: SkullbonezSource/Physics/Ragdoll.h
Purpose:
  Builds simple ragdoll body sets and named humanoid constraints.

Mental model:
  Ragdoll owns prefab construction. Constraint descriptors live in the physics
  constraint module so legacy point joints and named humanoid joints share the
  same solver path.

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
#include "PhysicsConstraint.h"

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
                                   Environment::WorldEnvironment& worldEnvironment,
                                   Geometry::Terrain* terrain,
                                   const RagdollBuildOptions& options );
};
} // namespace Physics
} // namespace SkullbonezCore
