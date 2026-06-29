/*
File: SkullbonezSource/Physics/Ragdoll.h
Purpose:
  Builds simple ragdoll body sets and solves their first point-joint constraints.

Mental model:
  This is deliberately a small bridge toward a future generic constraint system.
  Ragdoll owns prefab construction, while PointJointConstraint is generic solver
  data that already names bodies with physics handles while the current solver
  converts compatibility handles back to scene-order indices.

Glossary:
  Point joint: Constraint that keeps two local anchors near each other.
  Slack: Allowed anchor separation before the solver applies correction.
  Preview lines: Editor-only visualization geometry for placement feedback.

Invariants:
  - Constraint order is deterministic and scene-authored.
  - Constraint bodies refer to PhysicsBodyHandle values; compatibility index
    helpers are temporary solver glue.

Related:
  - SkullbonezSource/Physics/Ragdoll.cpp
  - SkullbonezSource/Physics/PhysicsWorld.cpp
*/
#pragma once

#include <cstdint>
#include <vector>

#include "PhysicsHandles.h"
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
class PhysicsModelAccess;

struct PointJointConstraint
{
    static constexpr uint8_t FLAG_LIMIT_NECK_SWING = 1u << 0;

    PhysicsBodyHandle bodyA;
    PhysicsBodyHandle bodyB;
    Math::Vector::Vector3 localAnchorA = Math::Vector::ZERO_VECTOR;
    Math::Vector::Vector3 localAnchorB = Math::Vector::ZERO_VECTOR;
    float slack = 0.25f;
    float stiffness = 0.22f;
    float damping = 0.35f;
    uint32_t groupId = 0;
    uint8_t flags = 0;

    void SetCompatibilityBodies( int modelIndexA, int modelIndexB )
    {
        bodyA = modelIndexA >= 0 ? MakeCompatibilityPhysicsBodyHandle( static_cast<uint32_t>( modelIndexA ) )
                                 : PhysicsBodyHandle{};
        bodyB = modelIndexB >= 0 ? MakeCompatibilityPhysicsBodyHandle( static_cast<uint32_t>( modelIndexB ) )
                                 : PhysicsBodyHandle{};
    }

    int BodyAIndex() const
    {
        return CompatibilityBodyIndex( bodyA );
    }

    int BodyBIndex() const
    {
        return CompatibilityBodyIndex( bodyB );
    }

    bool HasValidBodies() const
    {
        return bodyA.IsValid() && bodyB.IsValid() && bodyA != bodyB;
    }

  private:
    static int CompatibilityBodyIndex( PhysicsBodyHandle body )
    {
        if ( !body.IsValid() || body.generation != PHYSICS_COMPATIBILITY_HANDLE_GENERATION || body.index > 0x7fffffffu )
        {
            return -1;
        }
        return static_cast<int>( body.index );
    }
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
    static void SolvePointJoints( PhysicsModelAccess& modelAccess,
                                  PhysicsBodyStore& bodyStore,
                                  const std::vector<PointJointConstraint>& constraints,
                                  const std::vector<uint8_t>& sleepState,
                                  float dt );
};
} // namespace Physics
} // namespace SkullbonezCore
