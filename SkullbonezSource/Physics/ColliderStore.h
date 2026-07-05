/*
File: SkullbonezSource/Physics/ColliderStore.h
Purpose:
  Owns deterministic collider records and stable collider-handle identity.

Mental model:
  ColliderStore owns the dense live collider rows. Runtime compatibility code
  may replace a row at explicit create/edit boundaries, while topology repair
  only refreshes body identity from PhysicsBodyStore. Handles are allocator
  identity; record order is only an iteration/detail surface.

Glossary:
  Collider: Shape metadata used to choose sphere, box, or convex-hull tests.
  Physics material: Runtime policy for collider friction and sphere drag.
  Narrowphase: Precise collision pass that computes actual contact points.
  Convex hull: Collision shape made from a closed convex set of authored points.
  Replay body id: Stable per-scene id paired with a body for replay diagnostics.

Invariants:
  - Compatibility refresh keeps store row order aligned to scene physics order.
  - Standalone creation keeps rows dense for cache-friendly scans; handles map
    back to the current row after deletions move the last record down.
  - Collider handles are allocator-owned; model-order arrays use explicit maps
    instead of encoding model index inside the handle.
  - Body-binding refresh must not recapture shape/material authoring data.

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
namespace Physics
{
class PhysicsBodyStore;
struct PhysicsBodyRecord;
struct PhysicsMaterial;

enum class ColliderShapeKind : uint8_t
{
    Sphere,
    Box,
    ConvexHull
};

struct ColliderRecord
{
    PhysicsColliderHandle handle;                            // Stable collider handle resolved through store maps.
    PhysicsBodyHandle body;                                  // Body handle resolved by PhysicsBodyStore for the same model slot.
    PhysicsSceneObjectId sceneObjectId;                      // Scene-local id currently mirrored from replay body id.
    uint32_t replayBodyId = 0;                               // Stable replay-facing body id paired with this collider.
    Math::CollisionDetection::CollisionShape shape;          // Exact shape variant used by narrowphase.
    ColliderShapeKind shapeKind = ColliderShapeKind::Sphere; // Cheap typed discriminator for tools and migration checks.
    float boundingRadius = 0.0f;                             // Conservative broadphase radius.
    float restitution = 0.0f;                                // Collision restitution authored on the model.
    float friction = 0.0f;                                   // Tangential contact resistance copied from physics material.
    uint32_t contactMaterialId = 0;                          // Gameplay/audio material hash copied from the model.
    float projectedSurfaceArea = 0.0f;                       // Fluid-drag area mirrored from collision shape.
    float dragCoefficient = 0.0f;                            // Shape drag coefficient used by fluid forces.
};

class ColliderStore
{
  public:
    ColliderStore();

    void Clear();
    void RefreshBodyBindings( const PhysicsBodyStore& bodyStore );
    PhysicsColliderHandle CreateColliderRecord( const ColliderRecord& initialRecord );
    // Authoring edits replace row contents through the stable collider handle,
    // so callers do not need to expose model-order slots at the physics facade
    // boundary.
    bool UpdateRecordForHandle( PhysicsColliderHandle handle, const ColliderRecord& record );
    bool UpdateRecordForModelIndex( int modelIndex, const ColliderRecord& record );
    // Runtime config updates material scalars in-place instead of rebuilding
    // shape records from the GameModel mirror.
    void ApplyPhysicsMaterial( const PhysicsMaterial& material );
    bool DestroyColliderRecord( PhysicsColliderHandle handle );
    bool TrimToCount( int colliderCount );

    const ColliderRecord* Data() const;
    int Count() const;
    bool Empty() const;
    PhysicsColliderHandle HandleForModelIndex( int modelIndex ) const;
    int ModelIndexForHandle( PhysicsColliderHandle handle ) const;
    bool Contains( PhysicsColliderHandle handle ) const;
    const std::vector<ColliderRecord>& Records() const;
    std::vector<ColliderRecord>& MutableRecords();
    ColliderRecord* MutableRecordForHandle( PhysicsColliderHandle handle );
    const ColliderRecord* RecordForHandle( PhysicsColliderHandle handle ) const;

  private:
    PhysicsColliderHandle
    ResolveHandleForModelIndex( int modelIndex, uint32_t replayBodyId, std::vector<uint8_t>& assignedHandleSlots );
    void RetireUnassignedHandles( const std::vector<uint8_t>& assignedHandleSlots );

    std::vector<ColliderRecord> m_colliders;                 // Dense live collider records.
    std::vector<PhysicsColliderHandle>
        m_modelColliderHandles;                              // Compatibility row/model index to store-owned collider handle map.
    std::vector<uint32_t> m_handleGenerations;               // Handle-slot generation counters.
    std::vector<uint8_t> m_handleAlive;                      // Live handle slot flags.
    std::vector<int> m_handleModelIndices;                   // Handle slot to current dense row/model index, or -1.
    std::vector<uint32_t> m_handleReplayBodyIds;             // Replay id paired with each live handle slot.
    std::vector<uint32_t> m_freeHandleSlots;                 // Retired slots available for deterministic reuse.
};
} // namespace Physics
} // namespace SkullbonezCore
