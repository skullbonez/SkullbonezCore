/*
File: SkullbonezSource/Physics/Stages/Kernels/SolverRowKernel.h
Purpose:
  Declares the eight-row persistent-contact preparation kernel.

Summary:
  PersistentContactSolver supplies consecutive contact rows and compact solver
  bodies. The kernel prepares tangent axes, effective masses, anchor-relative
  normal speed, and penetration bias while the dependency-chained PGS core stays
  scalar and model ordered.

Glossary:
  Anchor velocity: Linear velocity at the contact point after adding angular
    velocity crossed with the point's offset from the body center.
  Effective mass: Reciprocal resistance of both bodies along one row axis.
  PGS (Projected Gauss-Seidel): Ordered iterative solve that revisits contact
    rows and clamps accumulated impulses.

Invariants:
  - Partial blocks use the same vector path; no scalar tail solves a row.
  - The kernel never reads or mutates the warm-start cache or body velocities.
  - Outputs are consumed before the next block and do not own runtime storage.

Related:
  - SkullbonezSource/Physics/Stages/Kernels/SolverRowKernel.cpp
  - SkullbonezSource/Physics/PersistentContactSolver.cpp
  - Agentic/Plans/TODO/physics-soa-simd-1000-bodies.md
*/
#pragma once

#include <cstdint>
#include <span>

#include "../../PersistentContactSolver.h"
#include "../PhysicsContactSolverStage.h"

namespace SkullbonezCore
{
namespace Physics::Kernels
{
inline constexpr int SOLVER_ROW_LANE_COUNT = 8;

struct SolverRowPrepBlock
{
    alignas( 32 ) float normalSpeed[SOLVER_ROW_LANE_COUNT] = {};
    alignas( 32 ) float penetrationBias[SOLVER_ROW_LANE_COUNT] = {};
    uint32_t validBits = 0u;
};

void PrepareSolverRowsAvx2( std::span<PersistentContact> contacts,
                            std::span<const SolverBodyState> bodies,
                            int rowBegin,
                            float inverseDeltaSeconds,
                            float contactSlop,
                            float baumgarteBeta,
                            float maxBaumgarteBias,
                            SolverRowPrepBlock& outBlock );
} // namespace Physics::Kernels
} // namespace SkullbonezCore
