/*
File: SkullbonezTests/TestColliderStoreFixtures.h
Purpose:
  Supplies explicit cold-row defaults when tests create collider topology.

Summary:
  Production collider creation is one full transaction over hot shape data,
  authored text, and hull identity. Tests that do not care about the cold
  fields still cross that canonical boundary through these fixture helpers.

Glossary:
  Cold row: Scene-authoring metadata retained beside the hot collision shape
    but excluded from per-step physics scans.
  Hull identity: Stable path-and-scale key used to share immutable hull data.

Invariants:
  - Every test collider row is created through ColliderStore's full transaction.
  - Default authoring and hull identity values are explicit test fixtures, not
    production convenience overloads.

Related:
  - SkullbonezSource/Physics/ColliderStore.h
*/
#pragma once

#include "../SkullbonezSource/Physics/ColliderStore.h"

namespace SkullbonezTests::ColliderStoreFixtures
{
inline SkullbonezCore::Physics::PhysicsColliderHandle
CreateColliderRecord( SkullbonezCore::Physics::ColliderStore& store,
                      const SkullbonezCore::Physics::ColliderRecord& record,
                      const SkullbonezCore::Math::CollisionDetection::CollisionShape& shape,
                      const SkullbonezCore::Physics::ColliderAuthoringRecord& authoring = {},
                      const SkullbonezCore::Physics::HullShapeIdentity& hullIdentity = {} )
{
    return store.CreateColliderRecord( record, shape, authoring, hullIdentity );
}


inline bool UpdateRecordForHandle( SkullbonezCore::Physics::ColliderStore& store,
                                   SkullbonezCore::Physics::PhysicsColliderHandle handle,
                                   const SkullbonezCore::Physics::ColliderRecord& record,
                                   const SkullbonezCore::Math::CollisionDetection::CollisionShape& shape,
                                   const SkullbonezCore::Physics::ColliderAuthoringRecord& authoring = {},
                                   const SkullbonezCore::Physics::HullShapeIdentity& hullIdentity = {} )
{
    return store.UpdateRecordForHandle( handle, record, shape, authoring, hullIdentity );
}
} // namespace SkullbonezTests::ColliderStoreFixtures
