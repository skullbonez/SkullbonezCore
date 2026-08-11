/*
File: SkullbonezSource/Physics/Stages/PhysicsTerrainStage.h
Purpose:
  Owns swept-terrain detection scratch, contact manifolds, and rest-policy rows.

Summary:
  PhysicsTerrainStage detects one candidate per body and converts tested hits
  into terrain manifolds. A typed two-phase commit lets PhysicsWorld preserve
  the exact diagnostics and visual-emission point while the stage owns terrain
  storage and writes borrowed sleep-support outputs. Detection dispatch borrows
  the sleep owner's ascending awake index list.

Glossary:
  Detection candidate: Per-body swept terrain result produced independently.
  Prepared commit: Stack value containing the manifold and diagnostic record
    computed before sequencer-owned side effects are emitted.
  Rest-applied row: Solver scratch preventing duplicate terrain rest response.

Invariants:
  - Detection workers write only the candidate slot matching their body index.
  - Candidate commit remains serial in ascending model order.
  - Detection worker slots map to ascending awake indices and retain per-body
    candidate identity regardless of worker scheduling.
  - Manifold and candidate lists are committed to exact scene capacity.

Related:
  - SkullbonezSource/Physics/Stages/PhysicsTerrainStage.cpp
  - SkullbonezSource/Physics/PhysicsWorld.cpp
  - SkullbonezSource/Physics/PersistentContactSolver.cpp
  - Agentic/Reference/engine-glossary.md
*/
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
                          std::span<const uint8_t> sleepState, std::span<const float> timeRemaining,
                          Core::Profiler* profiler, int bodyIndex );

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
                 std::span<const float> timeRemaining, Core::Profiler* profiler, std::span<const int> awakeBodyIndices,
                 const PhysicsExecutionSettings& execution, Threading::WorkerPool& workerPool );

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
    std::span<uint8_t> GetRestApplied();
    uint64_t CollectDynamicMemoryBytes() const;
};
} // namespace Physics
} // namespace SkullbonezCore
