/*
File: SkullbonezSource/Physics/Stages/PhysicsForceStage.h
Purpose:
  Owns bounded mutual-gravity scratch, body-force dispatch, and integration.

Summary:
  PhysicsForceStage prepares deterministic model-order gravity forces, retains
  the capped compact pair list used by worker dispatch, and applies all
  per-body forces and remaining-time integration through explicit synchronous
  borrows. Large scenes retain the exact serial fallback without allocating
  pair scratch beyond 512 bodies.

Glossary:
  Pair list: Chunk-local canonical contributions compacted into one linear
    model-order sequence before reduction.
  Model-order reduction: Serial replay of pair forces in the original nested
    loop order so worker scheduling cannot change floating-point additions.
  Large-scene fallback: Exact serial pair accumulation used above the bounded
    parallel pair-table limit.
  Remaining-time integration: Final model-order advance after all CCD lanes
    have consumed their portion of the shared tick clock.

Invariants:
  - Worker chunks write disjoint pair-list slices and never reduce forces.
  - Model-order accumulation, serial fallback arithmetic, worker thresholds,
    and worker hashes are part of the certified byte-exact contract. The Reduce
    marker is additive observation around that unchanged arithmetic.
  - Borrowed spans and returned force pointers are valid only during the
    enclosing fixed step; the force pointer expires on the next prepare/clear.
  - Integration borrows the PhysicsWorld-owned CCD clock and mutates no retained
    stage state.
  - Awake spans are synchronous borrows and preserve model-order arithmetic.

Related:
  - SkullbonezSource/Physics/Stages/PhysicsForceStage.cpp
  - SkullbonezSource/Physics/PhysicsWorld.cpp
  - Agentic/Reference/engine-glossary.md
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

    // Invariant: one record owns one canonical pair contribution. The bounded
    // 512-body path leaves the high bit of each index free to retain whether
    // that body receives the force, avoiding mutable-state reads in reduction.
    struct MutualGravityPairForce
    {
        Math::Vector::Vector3 force;
        uint16_t bodyAAndReceiver = 0u;
        uint16_t bodyBAndReceiver = 0u;
    };

    PhysicsFixedList<Math::Vector::Vector3, PHYSICS_MAX_BODY_ROWS>
        m_mutualGravityForces { "PhysicsForceStage.m_mutualGravityForces", PhysicsCapacityReason::SceneBodies };
    PhysicsFixedList<MutualGravityPairForce, PHYSICS_MAX_MUTUAL_GRAVITY_PAIRS>
        m_mutualGravityPairForces { "PhysicsForceStage.m_mutualGravityPairForces",
                                    PhysicsCapacityReason::MutualGravityPairs };
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
