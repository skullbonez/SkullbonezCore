/*
File: SkullbonezSource/Physics/ColliderStore.h
Purpose:
  Owns a deterministic collider-order snapshot of model collision metadata.

Mental model:
  The store copies shape and material-adjacent collision fields in model order.
  Narrowphase still reads GameModel through PhysicsWorld today; this boundary
  makes the future authoritative collider store explicit and reviewable.

Glossary:
  Collider: Shape metadata used to choose sphere, box, or convex-hull tests.
  Narrowphase: Precise collision pass that computes actual contact points.
  Convex hull: Collision shape made from a closed convex set of authored points.
  Replay body id: Stable per-scene id paired with a body for replay diagnostics.

Invariants:
  - Store index order must match GameModelCollection physics model order.
  - Collider handles are allocator-owned; model-order arrays use explicit maps
    instead of encoding model index inside the handle.
  - Refresh must not mutate GameModel state; it is a snapshot boundary only.

Related:
  - SkullbonezSource/Physics/ColliderStore.cpp
  - SkullbonezSource/Physics/PhysicsScene.h
*/
#pragma once

#include <cstdint>
#include <vector>

#include "CollisionShape.h"
#include "PhysicsHandles.h"

namespace SkullbonezCore
{
namespace GameObjects
{
class GameModel;
}

namespace Physics
{
class PhysicsBodyStore;

enum class ColliderShapeKind : uint8_t
{
    Sphere,
    Box,
    ConvexHull
};

struct ColliderRecord
{
    PhysicsColliderHandle handle;                              // Stable collider handle resolved through store maps.
    PhysicsBodyHandle body;                                    // Body handle resolved by PhysicsBodyStore for the same model slot.
    PhysicsSceneObjectId sceneObjectId;                        // Scene-local id currently mirrored from replay body id.
    uint32_t replayBodyId = 0;                                 // Stable replay-facing body id paired with this collider.
    Math::CollisionDetection::CollisionShape shape;            // Exact shape variant used by narrowphase.
    ColliderShapeKind shapeKind = ColliderShapeKind::Sphere;   // Cheap typed discriminator for tools and migration checks.
    float boundingRadius = 0.0f;                               // Conservative broadphase radius.
    float restitution = 0.0f;                                  // Collision restitution authored on the model.
    uint32_t contactMaterialId = 0;                            // Gameplay/audio material hash copied from the model.
    float projectedSurfaceArea = 0.0f;                         // Fluid-drag area mirrored from collision shape.
    float dragCoefficient = 0.0f;                              // Shape drag coefficient used by fluid forces.
};

class ColliderStore
{
  public:
    ColliderStore();

    void Clear();
    void Refresh( std::vector<GameObjects::GameModel>& models, const PhysicsBodyStore& bodyStore );
    void Refresh( GameObjects::GameModel* models, int modelCount, const PhysicsBodyStore& bodyStore );

    const ColliderRecord* Data() const;
    int Count() const;
    bool Empty() const;
    PhysicsColliderHandle HandleForModelIndex( int modelIndex ) const;
    int ModelIndexForHandle( PhysicsColliderHandle handle ) const;
    bool Contains( PhysicsColliderHandle handle ) const;
    const std::vector<ColliderRecord>& Records() const;

  private:
    PhysicsColliderHandle
    ResolveHandleForModelIndex( int modelIndex, uint32_t replayBodyId, std::vector<uint8_t>& assignedHandleSlots );
    void RetireUnassignedHandles( const std::vector<uint8_t>& assignedHandleSlots );

    std::vector<ColliderRecord> m_colliders;                   // Collider records in GameModelCollection index order.
    std::vector<PhysicsColliderHandle> m_modelColliderHandles; // Model index to store-owned collider handle map.
    std::vector<uint32_t> m_handleGenerations;                 // Handle-slot generation counters.
    std::vector<uint8_t> m_handleAlive;                        // Live handle slot flags.
    std::vector<int> m_handleModelIndices;                     // Handle slot to current model index, or -1.
    std::vector<uint32_t> m_handleReplayBodyIds;               // Replay id paired with each live handle slot.
    std::vector<uint32_t> m_freeHandleSlots;                   // Retired slots available for deterministic reuse.
};
} // namespace Physics
} // namespace SkullbonezCore
