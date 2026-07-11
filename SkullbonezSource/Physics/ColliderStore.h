/*
File: SkullbonezSource/Physics/ColliderStore.h
Purpose:
  Owns deterministic collider records and stable collider-handle identity.

Mental model:
  ColliderStore owns the dense live collider rows. Runtime authoring code
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
  - Body-binding refresh keeps store row order aligned to scene physics order.
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
#include "PhysicsFixedList.h"
#include "PhysicsHandles.h"
#include "../GameObjects/SceneCapacity.h"
#include "../Core/Common.h"

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
    PhysicsColliderHandle handle;                                                            // Stable collider handle resolved through store maps.
    PhysicsBodyHandle body;                                                                  // Body handle resolved by PhysicsBodyStore for the same model slot.
    PhysicsSceneObjectId sceneObjectId;                                                      // Scene-local id currently mirrored from replay body id.
    uint32_t replayBodyId = 0;                                                               // Stable replay-facing body id paired with this collider.
    Math::CollisionDetection::CollisionShape shape;                                          // Exact shape variant used by narrowphase.
    ColliderShapeKind shapeKind = ColliderShapeKind::Sphere;                                 // Cheap typed discriminator for tools and migration checks.
    float boundingRadius = 0.0f;                                                             // Conservative broadphase radius.
    float restitution = 0.0f;                                                                // Collision restitution authored on the model.
    float friction = 0.0f;                                                                   // Tangential contact resistance copied from physics material.
    uint32_t contactMaterialId = 0;                                                          // Gameplay/audio material hash copied from the collider descriptor.
    char contactMaterialName[32] = {};                                                       // Cold scene round-trip token for authored contact material.
    float projectedSurfaceArea = 0.0f;                                                       // Fluid-drag area mirrored from collision shape.
    float dragCoefficient = 0.0f;                                                            // Shape drag coefficient used by fluid forces.
};

using ColliderRecordList = PhysicsFixedList<ColliderRecord, MAX_GAME_MODELS>;
using ColliderHandleList = PhysicsFixedList<PhysicsColliderHandle, MAX_GAME_MODELS>;
using ColliderHandleGenerationList = PhysicsFixedList<uint32_t, MAX_GAME_MODELS>;
using ColliderHandleFlagList = PhysicsFixedList<uint8_t, MAX_GAME_MODELS>;
using ColliderHandleModelIndexList = PhysicsFixedList<int, MAX_GAME_MODELS>;
using ColliderHandleReplayIdList = PhysicsFixedList<uint32_t, MAX_GAME_MODELS>;
using ColliderHandleSlotList = PhysicsFixedList<uint32_t, MAX_GAME_MODELS>;
using ColliderHandleAssignmentMask = PhysicsFixedList<uint8_t, MAX_GAME_MODELS>;

class ColliderStore
{
  public:
    ColliderStore();

    void Clear();
    bool RefreshBodyBindings( const PhysicsBodyStore& bodyStore );
    PhysicsColliderHandle CreateColliderRecord( const ColliderRecord& initialRecord );
    // Authoring edits replace row contents through the stable collider handle,
    // so callers do not need to expose model-order slots at the physics facade
    // boundary.
    bool UpdateRecordForHandle( PhysicsColliderHandle handle, const ColliderRecord& record );
    bool UpdateRecordForModelIndex( int modelIndex, const ColliderRecord& record );
    // Runtime config updates material scalars in-place instead of rebuilding
    // shape records from scene authoring payloads.
    void ApplyPhysicsMaterial( const PhysicsMaterial& material );
    bool DestroyColliderRecord( PhysicsColliderHandle handle );
    bool TrimToCount( int colliderCount );

    const ColliderRecord* Data() const;
    int Count() const;
    bool Empty() const;
    PhysicsColliderHandle HandleForModelIndex( int modelIndex ) const;
    // Resolves collider identity through physics-owned body identity. The scan is
    // for cold tools, replay overlays, and save paths that already hold a body
    // handle and should not promote the model-index hint back to authority.
    PhysicsColliderHandle HandleForBodyHandle( PhysicsBodyHandle body ) const;
    // Scene/replay restore can know only the stable scene object id. Keep that
    // lookup explicit so callers do not invent a model slot just to reach the
    // collider row.
    PhysicsColliderHandle HandleForSceneObjectId( PhysicsSceneObjectId sceneObjectId ) const;
    int ModelIndexForHandle( PhysicsColliderHandle handle ) const;
    bool Contains( PhysicsColliderHandle handle ) const;
    const ColliderRecordList& Records() const;
    ColliderRecordList& MutableRecords();
    ColliderRecord* MutableRecordForHandle( PhysicsColliderHandle handle );
    const ColliderRecord* RecordForHandle( PhysicsColliderHandle handle ) const;

  private:
    PhysicsColliderHandle ResolveHandleForModelIndex( int modelIndex,
                                                      uint32_t replayBodyId,
                                                      ColliderHandleAssignmentMask& assignedHandleSlots );
    void RetireUnassignedHandles( const ColliderHandleAssignmentMask& assignedHandleSlots );

    ColliderRecordList m_colliders{ "ColliderStore.colliders" };                             // Dense live collider records.
    ColliderHandleList m_modelColliderHandles{
        "ColliderStore.modelColliderHandles" };                                              // Model slot to collider handle map.
    ColliderHandleGenerationList m_handleGenerations{ "ColliderStore.handleGenerations" };   // Handle-slot generations.
    ColliderHandleFlagList m_handleAlive{ "ColliderStore.handleAlive" };                     // Live handle slot flags.
    ColliderHandleModelIndexList m_handleModelIndices{ "ColliderStore.handleModelIndices" }; // Slot to model index.
    ColliderHandleReplayIdList m_handleReplayBodyIds{ "ColliderStore.handleReplayBodyIds" }; // Slot replay ids.
    ColliderHandleSlotList m_freeHandleSlots{ "ColliderStore.freeHandleSlots" };             // Retired reusable slots.
    // Runtime allocation policy: refresh reuses this handle-slot mask rather
    // than allocating a heap-backed standard-library container in topology repair.
    ColliderHandleAssignmentMask m_assignedHandleScratch{ "ColliderStore.assignedHandleScratch" };
};
} // namespace Physics
} // namespace SkullbonezCore
