// Owns scene-reserved force scratch. Workers write disjoint pair slices;
// serial reduction preserves the original (i,j) addition order across batches.
// Input borrows end at return. Returned forces expire at the next prepare/clear.

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
  public:
    // Invariant: Prepare replaces these counters for the current call; Clear
    // zeros them. Contributions count only admitted pairs, batches count
    // dispatched pair buffers, and scratchBytes reports reserved storage.
    struct WorkStats
    {
        uint64_t pairContributions = 0;
        uint64_t pairBatches = 0;
        uint64_t scratchBytes = 0;
    };

  private:
    // Invariant: one record owns one canonical pair contribution. The bounded
    // body ceiling leaves the high bit of each index free to retain whether
    // that body receives the force, avoiding mutable-state reads in reduction.
    struct MutualGravityPairForce
    {
        Math::Vector::Vector3 force;
        uint16_t bodyAAndReceiver = 0u;
        uint16_t bodyBAndReceiver = 0u;
    };

    PhysicsFixedList<Math::Vector::Vector3, PHYSICS_MAX_BODY_ROWS> m_mutualGravityForces { "PhysicsForceStage.m_mutualGravityForces", PhysicsCapacityReason::SceneBodies };
    PhysicsFixedList<MutualGravityPairForce, PHYSICS_MAX_MUTUAL_GRAVITY_PAIRS> m_mutualGravityPairForces { "PhysicsForceStage.m_mutualGravityPairForces", PhysicsCapacityReason::MutualGravityPairs };
    std::size_t m_mutualGravityPairHighWater = 0;
    WorkStats m_workStats;

    // The caller supplies exactly one chunk's disjoint triangular reservation.
    // Returns the initialized prefix length; skipped receiver pairs leave no
    // readable entry, and generation never mutates accumulated body forces.
    static std::size_t BuildMutualGravityPairChunk( std::span<const PhysicsBodyRecord> bodyRecords,
                                                    const PhysicsBodyHotFieldsConstView& hotFields,
                                                    std::span<const uint8_t> sleepState,
                                                    std::span<MutualGravityPairForce> output,
                                                    float softenedDistanceSq,
                                                    float gravitationalConstant,
                                                    int modelCount,
                                                    int rowBegin,
                                                    int rowEnd );

  public:
    PhysicsForceStage();
    const WorkStats& Stats() const noexcept
    {
        return m_workStats;
    }

    void Clear();
    void ReserveBodyScratchCapacity( std::size_t capacity );
    const Math::Vector::Vector3* PrepareMutualGravityForces( Core::Profiler* profiler,
                                                             std::span<const PhysicsBodyRecord> bodyRecords,
                                                             const PhysicsBodyHotFieldsConstView& hotFields,
                                                             std::span<const uint8_t> sleepState,
                                                             int modelCount,
                                                             const PhysicsWorldForces& worldForces,
                                                             const PhysicsExecutionSettings& execution,
                                                             Threading::WorkerPool& workerPool );
    const Math::Vector::Vector3* PrepareMutualGravityForces( std::span<const PhysicsBodyRecord> bodyRecords,
                                                             const PhysicsBodyHotFieldsConstView& hotFields,
                                                             std::span<const uint8_t> sleepState,
                                                             int modelCount,
                                                             const PhysicsWorldForces& worldForces,
                                                             const PhysicsExecutionSettings& execution,
                                                             Threading::WorkerPool& workerPool )
    {
        return PrepareMutualGravityForces( nullptr, bodyRecords, hotFields, sleepState, modelCount, worldForces, execution, workerPool );
    }

    // Invariant: these direct operations derive store views once, then map the
    // sleep owner's ascending awake slots without retaining any frame borrow.
    void ApplyForces( PhysicsBodyStore& bodyStore,
                      const ColliderStore& colliderStore,
                      PhysicsTerrainView terrain,
                      const PhysicsWorldForces& worldForces,
                      std::span<const BuoyancyBodyFacts> buoyancyFacts,
                      std::span<const uint8_t> sleepState,
                      std::span<float> timeRemaining,
                      float dt,
                      std::span<const int> awakeBodyIndices,
                      Threading::WorkerPool& workerPool,
                      const PhysicsExecutionSettings& execution ) const;
    void IntegrateRemaining( PhysicsBodyStore& bodyStore,
                             Core::Profiler* profiler,
                             const ColliderStore& colliderStore,
                             PhysicsTerrainView terrain,
                             std::span<BuoyancyBodyFacts> buoyancyFacts,
                             std::span<const uint8_t> sleepState,
                             std::span<const float> timeRemaining,
                             std::span<const int> awakeBodyIndices,
                             Threading::WorkerPool& workerPool,
                             const PhysicsExecutionSettings& execution ) const;

    uint64_t CollectDynamicMemoryBytes() const;
};
} // namespace Physics
} // namespace SkullbonezCore
