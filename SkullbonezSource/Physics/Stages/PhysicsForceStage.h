/*
File: SkullbonezSource/Physics/Stages/PhysicsForceStage.h
Purpose:
  Owns bounded mutual-gravity scratch, body-force dispatch, and integration.

Summary:
  PhysicsForceStage prepares deterministic model-order gravity forces, retains
  the capped triangular pair table used by worker dispatch, and applies all
  per-body forces and remaining-time integration through explicit borrowed
  contexts. Large scenes retain the exact serial fallback without allocating
  pair scratch beyond 512 bodies.

Glossary:
  Pair table: Triangular array with one force value for each `(i,j)` body pair.
  Model-order reduction: Serial replay of pair forces in the original nested
    loop order so worker scheduling cannot change floating-point additions.
  Large-scene fallback: Exact serial pair accumulation used above the bounded
    parallel pair-table limit.
  Remaining-time integration: Final model-order advance after all CCD lanes
    have consumed their portion of the shared tick clock.

Invariants:
  - Worker chunks write disjoint pair-table slots and never reduce forces.
  - Model-order accumulation, serial fallback arithmetic, marker names, worker
    thresholds, and worker hashes match the certified P2 implementation.
  - Borrowed contexts and returned force pointers are valid only during the
    enclosing fixed step; the force pointer expires on the next prepare/clear.
  - Integration borrows the facade-owned CCD clock and mutates no retained
    stage state.

Related:
  - SkullbonezSource/Physics/Stages/PhysicsForceStage.cpp
  - SkullbonezSource/Physics/Stages/PhysicsStageContexts.h
  - SkullbonezSource/Physics/PhysicsWorld.cpp
*/
#pragma once

#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

#include "../../Maths/Vector3.h"
#include "../PhysicsBodyStore.h"

namespace SkullbonezCore
{
namespace Core
{
struct PhysicsExecutionConfig;
} // namespace Core

namespace Threading
{
class WorkerPool;
} // namespace Threading

namespace Physics
{
struct ApplyForcesStageContext;
struct IntegrateRemainingStageContext;
struct PhysicsBodyRecord;
struct PhysicsWorldForces;

class PhysicsForceStage
{
  private:
    std::vector<Math::Vector::Vector3> m_mutualGravityForces;
    std::vector<Math::Vector::Vector3> m_mutualGravityPairForces;
    std::size_t m_mutualGravityPairHighWater = 0;

  public:
    PhysicsForceStage();

    void Clear();
    void ReserveBodyScratchCapacity( std::size_t capacity );
    const Math::Vector::Vector3* PrepareMutualGravityForces( std::span<const PhysicsBodyRecord> bodyRecords,
                                                             PhysicsBodyHotFieldsConstView hotFields,
                                                             std::span<const uint8_t> sleepState,
                                                             int modelCount,
                                                             const PhysicsWorldForces& worldForces,
                                                             const Core::PhysicsExecutionConfig& execution,
                                                             Threading::WorkerPool& workerPool );
    void ApplyForces( const ApplyForcesStageContext& context,
                      int modelCount,
                      Threading::WorkerPool& workerPool,
                      const Core::PhysicsExecutionConfig& execution ) const;
    void IntegrateRemaining( const IntegrateRemainingStageContext& context,
                             int modelCount,
                             Threading::WorkerPool& workerPool,
                             const Core::PhysicsExecutionConfig& execution ) const;

    uint64_t CollectDynamicMemoryBytes() const;
};
} // namespace Physics
} // namespace SkullbonezCore
