// Owns terrain detection, committed contact manifolds, and support classification.
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>

#include "../../Core/SceneCapacity.h"
#include "../BuoyancySystem.h"
#include "../PhysicsDebugData.h"
#include "../PhysicsBodyStore.h"
#include "../PhysicsRuntimeSettings.h"
#include "../PhysicsStageCapacity.h"
#include "../TerrainContactManifold.h"

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
struct ColliderRecord;
struct PhysicsBodyRecord;

struct TerrainDetectionCandidate
{
    float availableTime = 0.0f;
    TerrainContactSweepResult sweep;
    uint8_t tested = 0;
};

struct PreparedTerrainCandidateCommit
{
    // Value transaction split around sequencer-owned diagnostics. This keeps
    // Record -> Emit -> manifold/sleep -> visual -> clock ordering unchanged.
    // The count-only specialization leaves the optional disengaged and never
    // constructs a diagnostic payload.
    std::optional<PhysicsPipelineRecord> pipelineRecord;
    TerrainContactManifold manifold;
    float collisionTime = 0.0f;
    float availableTime = 0.0f;
    float remainingTime = 0.0f;
    int bodyIndex = -1;
    uint8_t hit = 0;
    uint8_t hasManifold = 0;
};

class PhysicsTerrainStage
{
  private:
    PhysicsBodyRowList<TerrainDetectionCandidate> m_detectionCandidates { "PhysicsTerrainStage.detectionCandidates",
                                                                          PhysicsCapacityReason::SceneBodies };
    PhysicsBodyRowList<TerrainContactManifold> m_contactManifolds { "PhysicsTerrainStage.contactManifolds",
                                                                    PhysicsCapacityReason::SceneBodies };
    std::array<uint8_t, Scene::Capacity::MAX_SCENE_OBJECTS> m_restApplied = {};

    void DetectTerrainAt( std::span<const PhysicsBodyRecord> bodyRecords, std::span<const BuoyancyBodyFacts> buoyancyFacts,
                          const PhysicsBodyHotFieldsConstView& hotFields, std::span<const ColliderRecord> colliderRecords,
                          PhysicsTerrainView terrain, const PhysicsRuntimeSettings& settings,
                          std::span<const uint8_t> sleepState, std::span<const uint8_t> motionEligibilityState,
                          std::span<const float> timeRemaining, int bodyIndex );

  public:
    PhysicsTerrainStage();
    void ReserveSceneCapacity( std::size_t bodyCapacity );

    void Clear();
    void BeginFrame();

    // Lifetime: detection workers borrow these concrete rows only until the
    // no-allocation dispatch joins; candidate storage remains stage-owned.
    void Detect( const PhysicsBodyStore& bodyStore, const ColliderStore& colliderStore,
                 std::span<const BuoyancyBodyFacts> buoyancyFacts, PhysicsTerrainView terrain,
                 const PhysicsRuntimeSettings& settings, std::span<const uint8_t> sleepState,
                 std::span<const uint8_t> motionEligibilityState, std::span<const float> timeRemaining,
                 std::span<const int> awakeBodyIndices, const PhysicsExecutionSettings& execution,
                 Threading::WorkerPool& workerPool );

    // Invariant: prepare performs body integration and manifold construction;
    // commit performs only the serial manifold/sleep publication after the
    // PhysicsWorld diagnostic gap. The template lane makes the trace payload
    // absent from count-only code while preserving the same hit event.
    template <bool RetainPipelineRecords>
    PreparedTerrainCandidateCommit
    PrepareCandidateCommit( PhysicsBodyStore& bodyStore, const ColliderStore& colliderStore, PhysicsTerrainView terrain,
                            std::span<BuoyancyBodyFacts> buoyancyFacts, const PhysicsRuntimeSettings& settings,
                            Core::Profiler* profiler, int bodyIndex, float availableTime,
                            const TerrainContactSweepResult& sweep );
    void CommitCandidate( const PreparedTerrainCandidateCommit& commit, std::span<uint8_t> sleepSupportedThisFrame,
                          std::span<uint8_t> sleepInhibitedThisFrame );

    std::span<const TerrainDetectionCandidate> GetDetectionCandidates() const;
    PhysicsBodyRowList<TerrainContactManifold>& GetContactManifolds();
    std::span<const TerrainContactManifold> GetContactManifolds() const;
    // Call after articulation wake-up and before contact-driven fixed-body release.
    void AppendReactivatedContacts( const PhysicsBodyStore& bodies, const ColliderStore& colliders,
                                    std::span<const BuoyancyBodyFacts> buoyancy, PhysicsTerrainView terrain,
                                    const PhysicsRuntimeSettings& settings, std::span<const int> reactivated,
                                    std::span<uint8_t> supported, std::span<uint8_t> inhibited, float stepSeconds );
    void PublishRestSupport( const PhysicsBodyStore& bodyStore, std::span<const uint8_t> sleepState );
    std::span<uint8_t> GetRestApplied();
    uint64_t CollectDynamicMemoryBytes() const;
};
} // namespace Physics
} // namespace SkullbonezCore
