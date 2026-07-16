/*
File: SkullbonezSource/Physics/Stages/Kernels/BroadphaseKernel.h
Purpose:
  Declares the eight-lane broadphase swept-bounds preparation kernel.

Summary:
  PhysicsBroadphaseStage supplies contiguous SoA pose/velocity rows and gathered
  collider radii. The kernel emits start/end displacement and conservative AABB
  bounds; SpatialGrid retains variable-size cell insertion and dedup ownership.

Glossary:
  AABB (Axis-Aligned Bounding Box): Min/max world bounds covering a body or its
    swept center path for the current fixed tick.
  Swept lane: Dynamic body whose displacement exceeds its padded radius and
    therefore needs SpatialGrid's swept traversal policy.
  Prepared bounds: Fixed eight-row output consumed immediately by the stage;
    it owns no retained storage.

Invariants:
  - The kernel never inserts cells or emits candidate pairs; SpatialGrid remains
    the single owner of bucket order, capacity checks, and deduplication.
  - Masked tails never read collider or hot-field rows beyond bodyCount.
  - Body order is unchanged when the stage consumes the prepared rows.

Related:
  - SkullbonezSource/Physics/Stages/Kernels/BroadphaseKernel.cpp
  - SkullbonezSource/Physics/Stages/PhysicsBroadphaseStage.cpp
  - SkullbonezSource/Physics/SpatialGrid.h
*/
#pragma once

#include <cstdint>
#include <span>

#include "../../ColliderStore.h"
#include "../../PhysicsBodyStore.h"

namespace SkullbonezCore
{
namespace Physics::Kernels
{
inline constexpr int BROADPHASE_LANE_COUNT = 8;

struct BroadphaseBoundsBlock
{
    alignas( 32 ) float minX[BROADPHASE_LANE_COUNT] = {};
    alignas( 32 ) float minY[BROADPHASE_LANE_COUNT] = {};
    alignas( 32 ) float minZ[BROADPHASE_LANE_COUNT] = {};
    alignas( 32 ) float maxX[BROADPHASE_LANE_COUNT] = {};
    alignas( 32 ) float maxY[BROADPHASE_LANE_COUNT] = {};
    alignas( 32 ) float maxZ[BROADPHASE_LANE_COUNT] = {};
    alignas( 32 ) float displacementX[BROADPHASE_LANE_COUNT] = {};
    alignas( 32 ) float displacementY[BROADPHASE_LANE_COUNT] = {};
    alignas( 32 ) float displacementZ[BROADPHASE_LANE_COUNT] = {};
    alignas( 32 ) float radius[BROADPHASE_LANE_COUNT] = {};
    uint32_t validBits = 0u;
    uint32_t sweptBits = 0u;
};

void BuildBroadphaseBoundsAvx2( const PhysicsBodyHotFieldsConstView& hotFields,
                                std::span<const ColliderRecord> colliderRecords,
                                int bodyBegin,
                                int bodyCount,
                                float deltaSeconds,
                                float contactSkin,
                                BroadphaseBoundsBlock& outBounds );
} // namespace Physics::Kernels
} // namespace SkullbonezCore
