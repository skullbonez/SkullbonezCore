/*
File: SkullbonezSource/Physics/ColliderStore.h
Purpose:
  Owns deterministic collider records and stable collider-handle identity.

Summary:
  ColliderStore owns dense hot collider rows plus a parallel cold authoring
  row for scene round-trip text. Exact shape payloads live in separate
  sphere-, box-, and convex-hull stores, so ordinary collider rows contain only
  typed references and scenes without hulls allocate no hull payload storage.
  Each collider row also caches immutable shape thickness and farthest-point
  geometry so fixed-step motion classification never revisits shape topology.
  Runtime authoring code may replace these rows at explicit cold create/edit
  boundaries, while topology repair only refreshes body identity from
  PhysicsBodyStore. Handles are allocator identity; record order is only an
  iteration/detail surface.

Invariants:
  - Body-binding refresh keeps store row order aligned to scene physics order.
  - Hot and authoring rows share the same dense index and compact together.
  - Sphere and box stores compact by kind; hull variants retain stable indices
    until scene clear so shared references never need refcounts.
  - Rebind and removal validate per-kind indices and hull/identity parity
    before dereferencing or compacting shape storage.
  - Hull capacity is driven by distinct shareable plus explicitly unique
    variants, not total hull collider count.
  - Standalone creation keeps rows dense for cache-friendly scans; handles map
    back to the current row after deletions move the last record down.
  - Collider handles are allocator-owned; model-order arrays use explicit maps
    instead of encoding model index inside the handle.
  - Body-binding refresh must not recapture shape/material authoring data.
  - Collider create/update refreshes both cached motion-geometry scalars from
    the same newly committed shape; body-binding refresh leaves them unchanged.

Related:
  - SkullbonezSource/Physics/ColliderStore.cpp
  - SkullbonezSource/Physics/PhysicsEngine.h
  - Agentic/Reference/engine-glossary.md
*/
#pragma once

#include <cstdint>
#include <span>
#include <vector>

#include "CollisionShape.h"
#include "PhysicsApi.h"
#include "PhysicsFixedList.h"
#include "PhysicsHandles.h"
#include "../Core/SceneCapacity.h"
#include "../Core/Common.h"

namespace SkullbonezCore
{
namespace Physics
{
class PhysicsBodyStore;
class PhysicsEngine;
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
    PhysicsColliderHandle handle;                                                                          // Stable collider handle resolved through store maps.
    PhysicsBodyHandle body;                                                                                // Body handle resolved by PhysicsBodyStore for the same model slot.

    // Stable cross-system identity paired with this collider.
    PhysicsSceneObjectId sceneObjectId;
    Math::CollisionDetection::CollisionShapeReference shape;                                               // Typed borrow into the store's per-kind shape storage.
    ColliderShapeKind shapeKind = ColliderShapeKind::Sphere;                                               // Cheap typed discriminator for tools and migration checks.
    float boundingRadius = 0.0f;                                                                           // Broadphase reads this conservative radius every fixed tick.
    float minimumCollisionThickness = 0.0f;                                                                // Cold shape fact consumed by motion eligibility.
    float maximumCenterOfMassRadius = 0.0f;                                                                // Cold farthest-point radius consumed by angular eligibility.
    float restitution = 0.0f;                                                                              // Contact generation reads this bounce policy every fixed tick.
    float friction = 0.0f;                                                                                 // Contact solving reads this tangential resistance every fixed tick.
    uint32_t contactMaterialId = 0;                                                                        // Runtime contact/gameplay classification hash.
    float projectedSurfaceArea = 0.0f;                                                                     // Shape/editor query copy; BuoyancySystem owns the fixed-step fluid value.
    float dragCoefficient = 0.0f;                                                                          // Shape/editor query copy; BuoyancySystem owns the fixed-step fluid value.
};

struct ColliderAuthoringRecord
{
    char contactMaterialName[32] = {};                                                                     // Cold scene round-trip token; never read by fixed-step physics.
};

using ColliderRecordList = PhysicsFixedList<ColliderRecord, SkullbonezCore::Scene::Capacity::MAX_SCENE_OBJECTS>;
using ColliderAuthoringRecordList = PhysicsFixedList<ColliderAuthoringRecord,
                                                     SkullbonezCore::Scene::Capacity::MAX_SCENE_OBJECTS>;
using ColliderHandleList = PhysicsFixedList<PhysicsColliderHandle, SkullbonezCore::Scene::Capacity::MAX_SCENE_OBJECTS>;
using ColliderHandleGenerationList = PhysicsFixedList<uint32_t, SkullbonezCore::Scene::Capacity::MAX_SCENE_OBJECTS>;
using ColliderHandleFlagList = PhysicsFixedList<uint8_t, SkullbonezCore::Scene::Capacity::MAX_SCENE_OBJECTS>;
using ColliderHandleModelIndexList = PhysicsFixedList<int, SkullbonezCore::Scene::Capacity::MAX_SCENE_OBJECTS>;
using ColliderHandleSceneObjectIdList = PhysicsFixedList<PhysicsSceneObjectId,
                                                         SkullbonezCore::Scene::Capacity::MAX_SCENE_OBJECTS>;
using ColliderHandleSlotList = PhysicsFixedList<uint32_t, SkullbonezCore::Scene::Capacity::MAX_SCENE_OBJECTS>;
using ColliderHandleAssignmentMask = PhysicsFixedList<uint8_t, SkullbonezCore::Scene::Capacity::MAX_SCENE_OBJECTS>;
using ColliderSphereShapeList = PhysicsFixedList<Math::CollisionDetection::BoundingSphere,
                                                 SkullbonezCore::Scene::Capacity::MAX_SCENE_OBJECTS>;
using ColliderBoxShapeList = PhysicsFixedList<Math::CollisionDetection::BoundingBox,
                                              SkullbonezCore::Scene::Capacity::MAX_SCENE_OBJECTS>;
using ColliderHullShapeList = PhysicsFixedList<Math::CollisionDetection::ConvexHullShape,
                                               SkullbonezCore::Scene::Capacity::MAX_SCENE_OBJECTS>;
using ColliderHullIdentityList = PhysicsFixedList<HullShapeIdentity, SkullbonezCore::Scene::Capacity::MAX_SCENE_OBJECTS>;

class ColliderStore
{
  public:
    ColliderStore();
    ColliderStore( const ColliderStore& ) = delete;
    ColliderStore& operator=( const ColliderStore& ) = delete;
    ColliderStore( ColliderStore&& ) = delete;
    ColliderStore& operator=( ColliderStore&& ) = delete;
    void ReserveCapacity( std::size_t capacity );
    void ReserveShapeCapacity( std::size_t sphereCapacity, std::size_t boxCapacity, std::size_t hullCapacity );

    void Clear();
    bool RefreshBodyBindings( const PhysicsBodyStore& bodyStore );

    // Creates hot and cold rows in one topology transaction. Callers that own
    // authored material text must use this overload so row indices cannot drift.
    PhysicsColliderHandle CreateColliderRecord( const ColliderRecord& initialRecord,
                                                const Math::CollisionDetection::CollisionShape& shape,
                                                const ColliderAuthoringRecord& initialAuthoringRecord,
                                                const HullShapeIdentity& hullIdentity );

    // Authoring edits replace row contents through the stable collider handle,
    // so callers do not need to expose model-order slots at the PhysicsEngine
    // owner boundary.

    // Replaces hot and cold authored facts together while retaining handle identity.
    bool UpdateRecordForHandle( PhysicsColliderHandle handle, const ColliderRecord& record,
                                const Math::CollisionDetection::CollisionShape& shape,
                                const ColliderAuthoringRecord& authoringRecord, const HullShapeIdentity& hullIdentity );

    // Runtime config updates material scalars in-place instead of rebuilding
    // shape records from scene authoring payloads.
    void ApplyPhysicsMaterial( const PhysicsMaterial& material );
    bool DestroyColliderRecord( PhysicsColliderHandle handle );
    bool TrimToCount( int colliderCount );

    int Count() const;
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
    std::size_t RecordCapacity() const;
    std::size_t AuthoringRecordCapacity() const;
    std::size_t SphereShapeCount() const;
    std::size_t SphereShapeCapacity() const;
    std::size_t BoxShapeCount() const;
    std::size_t BoxShapeCapacity() const;
    std::size_t HullShapeCount() const;
    std::size_t HullShapeCapacity() const;
    uint64_t CollectRuntimeCapacityMemoryBytes() const;
    const ColliderRecord* RecordForHandle( PhysicsColliderHandle handle ) const;

    // Lifetime: returned cold rows expire on store mutation or compaction, just
    // like the hot row returned by RecordForHandle.
    const ColliderAuthoringRecord* AuthoringRecordForHandle( PhysicsColliderHandle handle ) const;
    const ColliderAuthoringRecord* AuthoringRecordForModelIndex( int modelIndex ) const;
    const HullShapeIdentity* HullIdentityForHandle( PhysicsColliderHandle handle ) const;

  private:
    friend class PhysicsEngine;
    friend struct ColliderStoreTestAccess;

    // Invariant: replay prediction storage cloning remains a private
    // PhysicsEngine-coordinated operation and always rebinds copied shape
    // references into this destination's per-kind stores.
    void CloneReplayPredictionStorageFrom( const ColliderStore& source );

    PhysicsColliderHandle ResolveHandleForModelIndex( int modelIndex, PhysicsSceneObjectId sceneObjectId,
                                                      ColliderHandleAssignmentMask& assignedHandleSlots );
    void RetireUnassignedHandles( const ColliderHandleAssignmentMask& assignedHandleSlots );
    bool HasShareableHullIdentity( const HullShapeIdentity& identity ) const;
    Math::CollisionDetection::CollisionShapeReference AppendShape( const Math::CollisionDetection::CollisionShape& shape,
                                                                   const HullShapeIdentity& hullIdentity );
    void ReplaceShape( int recordIndex, const Math::CollisionDetection::CollisionShape& shape,
                       const HullShapeIdentity& hullIdentity );
    void RemoveShape( const ColliderRecord& record, int ignoredRecordIndex );
    void RebindShapeReferences();
    void RebindShapeReferences( ColliderShapeKind shapeKind );
    ColliderRecord* RequireMovedShapeOwner( ColliderShapeKind shapeKind, std::size_t removedIndex, std::size_t movedIndex,
                                            int ignoredRecordIndex );
    static void RequireShapeStorage( ColliderShapeKind shapeKind, std::size_t index, std::size_t shapeCount,
                                     std::size_t identityCount, const char* operation );

    ColliderRecordList m_colliders { "ColliderStore.colliders",
                                     PhysicsCapacityReason::SceneColliders };                              // Dense live collider records.

    // Cold scene text remains index-aligned with m_colliders but outside the
    // rows scanned by broadphase, narrowphase, contact solving, and fluid force.
    ColliderAuthoringRecordList m_authoringRecords { "ColliderStore.authoringRecords",
                                                     PhysicsCapacityReason::SceneColliders };
    ColliderHandleList
        m_modelColliderHandles { "ColliderStore.modelColliderHandles",
                                 PhysicsCapacityReason::SceneColliders };                                  // Model slot to collider handle map.
    ColliderHandleGenerationList
        m_handleGenerations { "ColliderStore.handleGenerations",
                              PhysicsCapacityReason::ColliderHandleSlots };                                // Handle-slot generations.
    ColliderHandleFlagList m_handleAlive { "ColliderStore.handleAlive",
                                           PhysicsCapacityReason::ColliderHandleSlots };                   // Live handle slot flags.
    ColliderHandleModelIndexList m_handleModelIndices { "ColliderStore.handleModelIndices",
                                                        PhysicsCapacityReason::ColliderHandleSlots };      // Slot to model index.
    ColliderHandleSceneObjectIdList m_handleSceneObjectIds { "ColliderStore.handleSceneObjectIds",
                                                             PhysicsCapacityReason::ColliderHandleSlots }; // Slot scene ids.
    ColliderHandleSlotList m_freeHandleSlots { "ColliderStore.freeHandleSlots",
                                               PhysicsCapacityReason::ColliderHandleSlots };               // Retired reusable slots.

    // Runtime allocation policy: refresh reuses this handle-slot mask rather
    // than allocating a heap-backed standard-library container in topology repair.
    ColliderHandleAssignmentMask m_assignedHandleScratch { "ColliderStore.assignedHandleScratch",
                                                           PhysicsCapacityReason::ColliderHandleSlots };

    // Shape payloads are dense by concrete type. Hull identity is cold,
    // index-aligned storage used only while appending/replacing authored rows.
    ColliderSphereShapeList m_sphereShapes { "ColliderStore.sphereShapes", PhysicsCapacityReason::SphereColliders };
    ColliderBoxShapeList m_boxShapes { "ColliderStore.boxShapes", PhysicsCapacityReason::BoxColliders };
    ColliderHullShapeList m_hullShapes { "ColliderStore.hullShapes", PhysicsCapacityReason::HullColliders };
    ColliderHullIdentityList m_hullIdentities { "ColliderStore.hullIdentities", PhysicsCapacityReason::HullColliders };
};
} // namespace Physics
} // namespace SkullbonezCore
