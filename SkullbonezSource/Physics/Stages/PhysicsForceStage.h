/*
File: SkullbonezSource/Physics/Stages/PhysicsForceStage.h
Purpose:
  Owns bounded mutual-gravity scratch, body-force dispatch, and integration.

Summary:
  PhysicsForceStage prepares deterministic model-order gravity forces, retains
  the capped triangular pair table used by worker dispatch, and applies all
  per-body forces and remaining-time integration through explicit synchronous
  borrows. Large scenes retain the exact serial fallback without allocating
  pair scratch beyond 512 bodies.

Glossary:
  Pair table: Triangular array with one force value for each `(i,j)` body pair.
  Model-order reduction: Serial replay of pair forces in the original nested
    loop order so worker scheduling cannot change floating-point additions.
  Large-scene fallback: Exact serial pair accumulation used above the bounded
    parallel pair-table limit.
  Remaining-time integration: Final model-order advance after all CCD lanes
    have consumed their portion of the shared tick clock.
  Awake index list: Ascending sleep-owner rows that select force/integration
    work without rebuilding or scanning the full body store.

Invariants:
  - Worker chunks write disjoint pair-table slots and never reduce forces.
  - Model-order accumulation, serial fallback arithmetic, marker names, worker
    thresholds, and worker hashes match the certified P2 implementation.
  - Borrowed spans and returned force pointers are valid only during the
    enclosing fixed step; the force pointer expires on the next prepare/clear.
  - Integration borrows the PhysicsWorld-owned CCD clock and mutates no retained
    stage state.
  - Awake spans are synchronous borrows and preserve model-order arithmetic.

Related:
  - SkullbonezSource/Physics/Stages/PhysicsForceStage.cpp
  - SkullbonezSource/Physics/PhysicsWorld.cpp
*/
#pragma once

#include <cstddef>
#include <cstdint>
#include <span>

#include "../../Maths/Vector3.h"
#include "../PhysicsBodyStore.h"
#include "../PhysicsRuntimeSettings.h"
#include "../PhysicsStageCapacity.h"

namespace SkullbonezCore
{
namespace Core
{
class Profiler;
} // namespace Core

namespace Threading
{
class WorkerPool;
} // namespace Threading

namespace Physics
{
class ColliderStore;
class PhysicsBodyStore;
struct BuoyancyBodyFacts;
struct PhysicsBodyRecord;
struct PhysicsWorldForces;

class PhysicsForceStage
{
  private:
    PhysicsFixedList<Math::Vector::Vector3, PHYSICS_MAX_BODY_ROWS> m_mutualGravityForces {
        "PhysicsForceStage.m_mutualGravityForces" };
    PhysicsFixedList<Math::Vector::Vector3, PHYSICS_MAX_MUTUAL_GRAVITY_PAIRS> m_mutualGravityPairForces {
        "PhysicsForceStage.m_mutualGravityPairForces" };
    std::size_t m_mutualGravityPairHighWater = 0;

  public:
    PhysicsForceStage();

    void Clear();
    void ReserveBodyScratchCapacity( std::size_t capacity );
    const Math::Vector::Vector3*
    PrepareMutualGravityForces( Core::Profiler* profiler, std::span<const PhysicsBodyRecord> bodyRecords,
                                const PhysicsBodyHotFieldsConstView& hotFields, std::span<const uint8_t> sleepState,
                                int modelCount, const PhysicsWorldForces& worldForces,
                                const PhysicsExecutionSettings& execution, Threading::WorkerPool& workerPool );
    const Math::Vector::Vector3* PrepareMutualGravityForces( std::span<const PhysicsBodyRecord> bodyRecords,
                                                             const PhysicsBodyHotFieldsConstView& hotFields,
                                                             std::span<const uint8_t> sleepState, int modelCount,
                                                             const PhysicsWorldForces& worldForces,
                                                             const PhysicsExecutionSettings& execution,
                                                             Threading::WorkerPool& workerPool )
    {
        return PrepareMutualGravityForces( nullptr, bodyRecords, hotFields, sleepState, modelCount, worldForces, execution,
                                           workerPool );
    }

    // Invariant: these direct operations derive store views once, then map the
    // sleep owner's ascending awake slots without retaining any frame borrow.
    void ApplyForces( PhysicsBodyStore& bodyStore, const ColliderStore& colliderStore, PhysicsTerrainView terrain,
                      const PhysicsWorldForces& worldForces, std::span<const BuoyancyBodyFacts> buoyancyFacts,
                      std::span<const uint8_t> sleepState, std::span<float> timeRemaining,
                      const Math::Vector::Vector3* mutualGravityForces, float dt, std::span<const int> awakeBodyIndices,
                      Threading::WorkerPool& workerPool, const PhysicsExecutionSettings& execution ) const;
    void IntegrateRemaining( PhysicsBodyStore& bodyStore, Core::Profiler* profiler, const ColliderStore& colliderStore,
                             PhysicsTerrainView terrain, std::span<BuoyancyBodyFacts> buoyancyFacts,
                             std::span<const uint8_t> sleepState, std::span<const float> timeRemaining,
                             std::span<const int> awakeBodyIndices, Threading::WorkerPool& workerPool,
                             const PhysicsExecutionSettings& execution ) const;

    uint64_t CollectDynamicMemoryBytes() const;
};
} // namespace Physics
} // namespace SkullbonezCore
