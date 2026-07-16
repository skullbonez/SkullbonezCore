/*
File: SkullbonezSource/Physics/Stages/Kernels/NarrowphasePruneKernel.h
Purpose:
  Declares the eight-pair conservative narrowphase-front-end prune kernel.

Summary:
  SpatialGrid batches deduplicated cell-sharing pairs here before appending
  them. The kernel tests each pair's relative swept segment against the sum of
  its conservative collider radii and returns accepted lanes in original order.

Glossary:
  Pair lane: One `(bodyA, bodyB)` candidate in an eight-wide block.
  Conservative prune: A rejection that permits false positives but never drops
    a pair whose bounding spheres can touch during the fixed tick.

Invariants:
  - Invalid radii stay accepted so exact collision code retains authority.
  - Invalid indices are rejected before any hot-field or collider access.
  - Partial blocks use the same masked vector path; there is no scalar tail.

Related:
  - SkullbonezSource/Physics/Stages/Kernels/NarrowphasePruneKernel.cpp
  - SkullbonezSource/Physics/SolverBroadphaseStage.h
  - SkullbonezSource/Physics/SpatialGrid.cpp
*/
#pragma once

#include <cstdint>
#include <span>
#include <utility>

#include "../../ColliderStore.h"
#include "../../PhysicsBodyStore.h"

namespace SkullbonezCore
{
namespace Physics::Kernels
{
inline constexpr int NARROWPHASE_PRUNE_LANE_COUNT = 8;

uint32_t PruneNarrowphasePairsAvx2( const PhysicsBodyHotFieldsConstView& hotFields,
                                    std::span<const ColliderRecord> colliderRecords,
                                    std::span<const std::pair<int, int>> pairs,
                                    int modelCount,
                                    float deltaSeconds,
                                    float contactSkin );
} // namespace Physics::Kernels
} // namespace SkullbonezCore
