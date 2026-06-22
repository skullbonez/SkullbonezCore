/*
File: SkullbonezSource/Physics/ColliderStore.h
Purpose:
  Owns a deterministic collider-order snapshot of model collision metadata.

Mental model:
  The store copies shape and material-adjacent collision fields in model order.
  Narrowphase still reads GameModel through PhysicsWorld today; this boundary
  makes the future authoritative collider store explicit and reviewable.

Related:
  - SkullbonezSource/Physics/ColliderStore.cpp
  - SkullbonezSource/Physics/PhysicsScene.h
*/
#pragma once

#include <cstdint>
#include <vector>

#include "CollisionShape.h"

namespace SkullbonezCore
{
namespace GameObjects
{
class GameModel;
}

namespace Physics
{
enum class ColliderShapeKind : uint8_t
{
    Sphere,
    Box,
    ConvexHull
};

struct ColliderRecord
{
    uint32_t replayBodyId = 0;                               // Stable replay-facing body id paired with this collider.
    Math::CollisionDetection::CollisionShape shape;          // Exact shape variant used by narrowphase.
    ColliderShapeKind shapeKind = ColliderShapeKind::Sphere; // Cheap typed discriminator for tools and migration checks.
    float boundingRadius = 0.0f;                             // Conservative broadphase radius.
    float restitution = 0.0f;                                // Collision restitution authored on the model.
    float projectedSurfaceArea = 0.0f;                       // Fluid-drag area mirrored from collision shape.
    float dragCoefficient = 0.0f;                            // Shape drag coefficient used by fluid forces.
};

class ColliderStore
{
  public:
    ColliderStore();

    void Clear();
    void Refresh( std::vector<GameObjects::GameModel>& models );

    const ColliderRecord* Data() const;
    int Count() const;
    bool Empty() const;
    const std::vector<ColliderRecord>& Records() const;

  private:
    std::vector<ColliderRecord> m_colliders;                 // Collider records in GameModelCollection index order.
};
} // namespace Physics
} // namespace SkullbonezCore
