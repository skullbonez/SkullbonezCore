/*
File: SkullbonezSource/Physics/ColliderStore.h
Purpose:
  Owns deterministic collider records and stable collider-handle identity.

Summary:
  ColliderStore owns dense hot collider rows plus a parallel cold authoring
  row for scene round-trip text. Runtime authoring code may replace both rows
  at explicit create/edit boundaries, while topology repair only refreshes body
  identity from PhysicsBodyStore. Handles are allocator identity; record order
  is only an iteration/detail surface.

Glossary:
  Collider: Shape metadata used to choose sphere, box, or convex-hull tests.
  Physics material: Runtime policy for collider friction and sphere drag.
  Authoring row: Cold scene round-trip text paired with one hot collider row.
  Narrowphase: Precise collision pass that computes actual contact points.
  Convex hull: Collision shape made from a closed convex set of authored points.
  Scene object id: Stable per-scene id paired with a body for replay diagnostics.

Invariants:
  - Body-binding refresh keeps store row order aligned to scene physics order.
  - Hot and authoring rows share the same dense index and compact together.
  - Standalone creation keeps rows dense for cache-friendly scans; handles map
    back to the current row after deletions move the last record down.
  - Collider handles are allocator-owned; model-order arrays use explicit maps
    instead of encoding model index inside the handle.
  - Body-binding refresh must not recapture shape/material authoring data.

Related:
  - SkullbonezSource/Physics/ColliderStore.cpp
  - SkullbonezSource/Physics/PhysicsEngine.h
*/
#pragma once

#include <cstdint>
#include <span>
#include <vector>

#include "CollisionShape.h"
#include "PhysicsFixedList.h"
#include "PhysicsHandles.h"
#include "../Core/SceneCapacity.h"
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
    PhysicsColliderHandle handle;                                                                   // Stable collider handle resolved through store maps.
    PhysicsBodyHandle body;                                                                         // Body handle resolved by PhysicsBodyStore for the same model slot.
    // Stable cross-system identity paired with this collider.
    PhysicsSceneObjectId sceneObjectId;
    Math::CollisionDetection::CollisionShape shape;                                                 // Exact shape variant used by narrowphase.
    ColliderShapeKind shapeKind = ColliderShapeKind::Sphere;                                        // Cheap typed discriminator for tools and migration checks.
    float boundingRadius = 0.0f;                                                                    // Broadphase reads this conservative radius every fixed tick.
    float restitution = 0.0f;                                                                       // Contact generation reads this bounce policy every fixed tick.
    float friction = 0.0f;                                                                          // Contact solving reads this tangential resistance every fixed tick.
    uint32_t contactMaterialId = 0;                                                                 // Runtime contact/gameplay classification hash.
    float projectedSurfaceArea = 0.0f;                                                              // Fluid forces read this drag area every fixed tick.
    float dragCoefficient = 0.0f;                                                                   // Fluid forces read this shape coefficient every fixed tick.
};

struct ColliderAuthoringRecord
{
    char contactMaterialName[32] = {};                                                              // Cold scene round-trip token; never read by fixed-step physics.
};

using ColliderRecordList = PhysicsFixedList<ColliderRecord, SkullbonezCore::Scene::Capacity::MAX_SCENE_OBJECTS>;
using ColliderAuthoringRecordList =
    PhysicsFixedList<ColliderAuthoringRecord, SkullbonezCore::Scene::Capacity::MAX_SCENE_OBJECTS>;
using ColliderHandleList = PhysicsFixedList<PhysicsColliderHandle, SkullbonezCore::Scene::Capacity::MAX_SCENE_OBJECTS>;
using ColliderHandleGenerationList = PhysicsFixedList<uint32_t, SkullbonezCore::Scene::Capacity::MAX_SCENE_OBJECTS>;
using ColliderHandleFlagList = PhysicsFixedList<uint8_t, SkullbonezCore::Scene::Capacity::MAX_SCENE_OBJECTS>;
using ColliderHandleModelIndexList = PhysicsFixedList<int, SkullbonezCore::Scene::Capacity::MAX_SCENE_OBJECTS>;
using ColliderHandleSceneObjectIdList =
    PhysicsFixedList<PhysicsSceneObjectId, SkullbonezCore::Scene::Capacity::MAX_SCENE_OBJECTS>;
using ColliderHandleSlotList = PhysicsFixedList<uint32_t, SkullbonezCore::Scene::Capacity::MAX_SCENE_OBJECTS>;
using ColliderHandleAssignmentMask = PhysicsFixedList<uint8_t, SkullbonezCore::Scene::Capacity::MAX_SCENE_OBJECTS>;

class ColliderStore
{
  public:
    ColliderStore();

    void Clear();
    bool RefreshBodyBindings( const PhysicsBodyStore& bodyStore );
    PhysicsColliderHandle CreateColliderRecord( const ColliderRecord& initialRecord );
    // Creates hot and cold rows in one topology transaction. Callers that own
    // authored material text must use this overload so row indices cannot drift.
    PhysicsColliderHandle CreateColliderRecord( const ColliderRecord& initialRecord,
                                                const ColliderAuthoringRecord& initialAuthoringRecord );
    // Authoring edits replace row contents through the stable collider handle,
    // so callers do not need to expose model-order slots at the PhysicsEngine
    // owner boundary.
    bool UpdateRecordForHandle( PhysicsColliderHandle handle, const ColliderRecord& record );
    // Replaces hot and cold authored facts together while retaining handle identity.
    bool UpdateRecordForHandle( PhysicsColliderHandle handle,
                                const ColliderRecord& record,
                                const ColliderAuthoringRecord& authoringRecord );
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
    // Lifetime: these spans borrow the store's live dense prefix and expire on
    // scene mutation, compaction, or store destruction.
    std::span<const ColliderRecord> Records() const;
    std::span<ColliderRecord> MutableRecords();
    std::size_t RecordCapacity() const;
    std::size_t AuthoringRecordCapacity() const;
    ColliderRecord* MutableRecordForHandle( PhysicsColliderHandle handle );
    const ColliderRecord* RecordForHandle( PhysicsColliderHandle handle ) const;
    // Lifetime: returned cold rows expire on store mutation or compaction, just
    // like the hot row returned by RecordForHandle.
    const ColliderAuthoringRecord* AuthoringRecordForHandle( PhysicsColliderHandle handle ) const;
    const ColliderAuthoringRecord* AuthoringRecordForModelIndex( int modelIndex ) const;

  private:
    PhysicsColliderHandle ResolveHandleForModelIndex( int modelIndex,
                                                      PhysicsSceneObjectId sceneObjectId,
                                                      ColliderHandleAssignmentMask& assignedHandleSlots );
    void RetireUnassignedHandles( const ColliderHandleAssignmentMask& assignedHandleSlots );

    ColliderRecordList m_colliders{ "ColliderStore.colliders" };                                    // Dense live collider records.
    // Cold scene text remains index-aligned with m_colliders but outside the
    // rows scanned by broadphase, narrowphase, contact solving, and fluid force.
    ColliderAuthoringRecordList m_authoringRecords{ "ColliderStore.authoringRecords" };
    ColliderHandleList m_modelColliderHandles{
        "ColliderStore.modelColliderHandles" };                                                     // Model slot to collider handle map.
    ColliderHandleGenerationList m_handleGenerations{ "ColliderStore.handleGenerations" };          // Handle-slot generations.
    ColliderHandleFlagList m_handleAlive{ "ColliderStore.handleAlive" };                            // Live handle slot flags.
    ColliderHandleModelIndexList m_handleModelIndices{ "ColliderStore.handleModelIndices" };        // Slot to model index.
    ColliderHandleSceneObjectIdList m_handleSceneObjectIds{ "ColliderStore.handleSceneObjectIds" }; // Slot scene ids.
    ColliderHandleSlotList m_freeHandleSlots{ "ColliderStore.freeHandleSlots" };                    // Retired reusable slots.
    // Runtime allocation policy: refresh reuses this handle-slot mask rather
    // than allocating a heap-backed standard-library container in topology repair.
    ColliderHandleAssignmentMask m_assignedHandleScratch{ "ColliderStore.assignedHandleScratch" };
};
} // namespace Physics
} // namespace SkullbonezCore
