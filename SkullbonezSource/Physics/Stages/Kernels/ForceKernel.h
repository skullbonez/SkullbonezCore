/*
File: SkullbonezSource/Physics/Stages/Kernels/ForceKernel.h
Purpose:
  Declares pinned eight-lane gravity and mutual-gravity pair kernels.

Summary:
  PhysicsForceStage supplies borrowed hot SoA rows. The body kernel applies the
  universal gravity velocity delta before store-owned force completion. The
  pair kernel computes eight consecutive triangular-table forces while the
  stage retains the certified serial model-order reduction.

Glossary:
  Pair row: Consecutive `(i,j)` entries for one body `i` in the triangular
    mutual-gravity scratch table.
  Receive mask: Lanes where at least one body is dynamic, awake, and able to
    accept the pair force.
  Serial reduction: Original nested-loop accumulation of pair scratch into the
    per-body force array after parallel pair construction.

Invariants:
  - The >512-body mutual-gravity fallback never calls these kernels.
  - Pair lanes write unique scratch rows; no kernel performs a horizontal sum.
  - Returned body bits identify scalar store completion, including zero-mass
    dynamic rows whose gravity lane intentionally performs no velocity write.

Related:
  - SkullbonezSource/Physics/Stages/Kernels/ForceKernel.cpp
  - SkullbonezSource/Physics/Stages/PhysicsForceStage.cpp
  - SkullbonezSource/Physics/PhysicsBodyStore.h
*/
#pragma once

#include <cstdint>
#include <span>

#include "../../PhysicsBodyStore.h"

namespace SkullbonezCore
{
namespace Physics::Kernels
{
inline constexpr int FORCE_LANE_COUNT = 8;

uint32_t ApplyGravityAvx2( const PhysicsBodyHotFieldsView& hotFields,
                           std::span<const uint8_t> sleepState,
                           int bodyBegin,
                           int bodyCount,
                           float gravity,
                           float deltaSeconds );

uint32_t BuildMutualGravityPairsAvx2( std::span<const PhysicsBodyRecord> bodyRecords,
                                      const PhysicsBodyHotFieldsConstView& hotFields,
                                      std::span<const uint8_t> sleepState,
                                      int bodyAIndex,
                                      int bodyBBegin,
                                      int bodyCount,
                                      float softenedDistanceSq,
                                      float gravitationalConstant,
                                      Math::Vector::Vector3* outPairForces );
} // namespace Physics::Kernels
} // namespace SkullbonezCore
