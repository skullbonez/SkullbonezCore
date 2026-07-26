/*
File: PhysicsStageCapacity.h
Purpose:
  Defines shared scene-derived ceilings and list types for fixed-step stages.

Summary:
  Broadphase, narrowphase, contact solving, and diagnostics derive their runtime
  reservations from the same admitted body count. Compile-time ceilings retain
  the engine's absolute 8,192-body capability while PhysicsFixedList commits
  only the active scene's required prefix during scene load.

Glossary:
  B: Admitted body rows for the active scene.
  P: Candidate-pair rows, bounded to four per admitted body.
  K: Persistent contact/cache rows, four manifolds per pair plus eight terrain
    rows per body.

Invariants:
  - Every helper is monotonic through the supported body ceiling.
  - Runtime capacities never exceed their matching compile-time ceiling.
  - Candidate-pair and contact formulas are shared by every owning stage.

Related:
  - PhysicsFixedList.h
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
inline constexpr std::size_t PHYSICS_MAX_CONTACT_ROWS = PHYSICS_MAX_CANDIDATE_PAIRS * 4u + PHYSICS_MAX_BODY_ROWS * 8u;
inline constexpr std::size_t PHYSICS_MAX_COLLISION_VISUAL_BODY_ROWS = PHYSICS_MAX_CANDIDATE_PAIRS * 2u;
inline constexpr std::size_t PHYSICS_MAX_PIPELINE_TRACE_RECORDS = 4096u;
inline constexpr std::size_t PHYSICS_MUTUAL_GRAVITY_MAX_BODIES = 512u;
inline constexpr std::size_t PHYSICS_MAX_MUTUAL_GRAVITY_PAIRS = PHYSICS_MUTUAL_GRAVITY_MAX_BODIES *
                                                                ( PHYSICS_MUTUAL_GRAVITY_MAX_BODIES - 1u ) / 2u;

constexpr std::size_t PhysicsCandidatePairCapacity( std::size_t bodyCapacity )
{
    return (std::min)( bodyCapacity * 4u, PHYSICS_MAX_CANDIDATE_PAIRS );
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

using PhysicsCandidatePairList = PhysicsFixedList<std::pair<int, int>, PHYSICS_MAX_CANDIDATE_PAIRS>;
using PhysicsCollisionCellKeyList = PhysicsFixedList<int64_t, PHYSICS_MAX_CANDIDATE_PAIRS>;
} // namespace SkullbonezCore::Physics
