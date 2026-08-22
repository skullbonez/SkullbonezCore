/*
File: PhysicsStageCapacity.h
Purpose:
  Defines shared scene-derived ceilings and list types for fixed-step stages.

Summary:
  Broadphase, narrowphase, contact solving, and diagnostics share their
  compile-time ceilings here. Scene-derived reservations use the admitted body
  count, while the spatial grid retains one fixed 8,192-bucket identity table.

Glossary:
  B: Admitted body rows for the active scene.
  P: Candidate-pair rows, bounded by the smaller of the complete body-pair
    topology and the fixed compile-time ceiling.
  K: Persistent contact/cache rows, four manifolds per pair plus eight terrain
    rows per body.
  Grid bucket: Fixed hash-table row that owns one active broadphase cell.

Invariants:
  - Every helper is monotonic through the supported body ceiling.
  - Runtime capacities never exceed their matching compile-time ceiling.
  - Candidate-pair and contact formulas are shared by every owning stage.
  - Solver storage and its detached diagnostic projection use the same grid
    bucket ceiling; diagnostics never define solver capacity.

Related:
  - PhysicsFixedList.h
  - PhysicsBroadphaseDebugView.h
  - SpatialGrid.h
  - Stages/PhysicsBroadphaseStage.h
  - Stages/PhysicsContactSolverStage.h
*/
#pragma once

#include "../Core/SceneCapacity.h"
#include "PhysicsFixedList.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <utility>

namespace SkullbonezCore::Physics
{
inline constexpr std::size_t PHYSICS_MAX_BODY_ROWS = Scene::Capacity::MAX_SCENE_OBJECTS;
inline constexpr std::size_t PHYSICS_MAX_CANDIDATE_PAIRS = PHYSICS_MAX_BODY_ROWS * 4u;
inline constexpr std::size_t PHYSICS_COMPLETE_PAIR_TOPOLOGY_MAX_BODIES = 256u;
inline constexpr std::size_t PHYSICS_MAX_CONTACT_ROWS = PHYSICS_MAX_CANDIDATE_PAIRS * 4u + PHYSICS_MAX_BODY_ROWS * 8u;
inline constexpr std::size_t PHYSICS_MAX_COLLISION_VISUAL_BODY_ROWS = PHYSICS_MAX_CANDIDATE_PAIRS * 2u;
inline constexpr std::size_t PHYSICS_MAX_PIPELINE_TRACE_RECORDS = 4096u;
inline constexpr std::size_t PHYSICS_MUTUAL_GRAVITY_MAX_BODIES = 512u;
inline constexpr std::size_t PHYSICS_SPATIAL_GRID_BUCKET_COUNT = 8192u;
inline constexpr std::size_t PHYSICS_MAX_MUTUAL_GRAVITY_PAIRS = PHYSICS_MUTUAL_GRAVITY_MAX_BODIES *
                                                                ( PHYSICS_MUTUAL_GRAVITY_MAX_BODIES - 1u ) / 2u;
static_assert( PHYSICS_COMPLETE_PAIR_TOPOLOGY_MAX_BODIES * ( PHYSICS_COMPLETE_PAIR_TOPOLOGY_MAX_BODIES - 1u ) / 2u <=
                   PHYSICS_MAX_CANDIDATE_PAIRS,
               "Complete-pair topology bound must fit the candidate ceiling." );
static_assert( ( PHYSICS_COMPLETE_PAIR_TOPOLOGY_MAX_BODIES + 1u ) * PHYSICS_COMPLETE_PAIR_TOPOLOGY_MAX_BODIES / 2u >
                   PHYSICS_MAX_CANDIDATE_PAIRS,
               "Complete-pair topology bound must name the exact largest supported body count." );

constexpr std::size_t PhysicsCandidatePairCapacity( std::size_t bodyCapacity )
{
    const std::size_t completePairCount = bodyCapacity > 1u ? bodyCapacity * ( bodyCapacity - 1u ) / 2u : 0u;
    return (std::min)( completePairCount, PHYSICS_MAX_CANDIDATE_PAIRS );
}

constexpr std::size_t PhysicsContactRowCapacity( std::size_t bodyCapacity )
{
    return PhysicsCandidatePairCapacity( bodyCapacity ) * 4u + bodyCapacity * 8u;
}

constexpr std::size_t PhysicsMutualGravityPairCapacity( std::size_t bodyCapacity )
{
    const std::size_t pairBodies = (std::min)( bodyCapacity, PHYSICS_MUTUAL_GRAVITY_MAX_BODIES );
    return pairBodies > 1u ? pairBodies * ( pairBodies - 1u ) / 2u : 0u;
}

template <typename T> using PhysicsBodyRowList = PhysicsFixedList<T, PHYSICS_MAX_BODY_ROWS>;
template <typename T> using PhysicsContactRowList = PhysicsFixedList<T, PHYSICS_MAX_CONTACT_ROWS>;
template <typename T> using PhysicsPipelineRowList = PhysicsFixedList<T, PHYSICS_MAX_PIPELINE_TRACE_RECORDS>;
using PhysicsCandidatePairList = PhysicsFixedList<std::pair<int, int>, PHYSICS_MAX_CANDIDATE_PAIRS>;
using PhysicsCollisionCellKeyList = PhysicsFixedList<int64_t, PHYSICS_MAX_CANDIDATE_PAIRS>;
} // namespace SkullbonezCore::Physics
