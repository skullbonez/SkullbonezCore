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
  Awake index list: Ascending dynamic rows eligible for terrain detection.

Invariants:
  - Detection workers write only the candidate slot matching their body index.
  - Candidate commit remains serial in ascending model order.
  - Detection worker slots map to ascending awake indices and retain per-body
    candidate identity regardless of worker scheduling.
  - Manifold and candidate vectors are construction-reserved to scene capacity.

Related:
  - SkullbonezSource/Physics/Stages/PhysicsTerrainStage.cpp
  - SkullbonezSource/Physics/PhysicsWorld.cpp
  - SkullbonezSource/Physics/PersistentContactSolver.cpp
*/
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

#include "../../Core/SceneCapacity.h"
#include "../PhysicsDebugData.h"
#include "../PhysicsBodyStore.h"
#include "../PhysicsRuntimeSettings.h"
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

struct TerrainDetectionStageContext
{
    // Lifetime: terrain workers borrow this fixed-step snapshot only during
    // synchronous dispatch; each writes one stage-owned candidate row.
    std::span<const PhysicsBodyRecord> bodyRecords;
    PhysicsBodyHotFieldsConstView hotFields;
    std::span<const ColliderRecord> colliderRecords;
    const PhysicsRuntimeSettings& settings;
    std::span<const uint8_t> sleepState;
    std::span<const float> timeRemaining;
    Core::Profiler* profiler = nullptr;
};

struct TerrainCandidateCommitContext
{
    // Lifetime: serial model-order commits borrow solver and sleep rows for the
    // current terrain phase. The stage retains none of these references.
    PhysicsBodyStore& bodyStore;
    const ColliderStore& colliderStore;
    std::span<const PhysicsBodyRecord> bodyRecords;
    PhysicsBodyHotFieldsConstView hotFields;
    std::span<const ColliderRecord> colliderRecords;
    const PhysicsRuntimeSettings& settings;
    std::span<uint8_t> sleepSupportedThisFrame;
    std::span<uint8_t> sleepInhibitedThisFrame;
    Core::Profiler* profiler = nullptr;
};

struct PreparedTerrainCandidateCommit
{
    // Value transaction split around sequencer-owned diagnostics. This keeps
    // Record -> Emit -> manifold/sleep -> visual -> clock ordering unchanged.
    PhysicsPipelineRecord pipelineRecord;
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
    std::vector<TerrainDetectionCandidate> m_detectionCandidates;
    std::vector<TerrainContactManifold> m_contactManifolds;
    std::array<uint8_t, Scene::Capacity::MAX_SCENE_OBJECTS> m_restApplied = {};

    struct TerrainDetectionStage
    {
        PhysicsTerrainStage& stage;
        const TerrainDetectionStageContext& context;
        std::span<const int> bodyIndices;

        void operator()( int bodySlot ) const;
    };

    void DetectTerrainAt( const TerrainDetectionStageContext& context, int bodyIndex );

  public:
    PhysicsTerrainStage();

    void Clear();
    void BeginFrame();
    void Detect(
        const TerrainDetectionStageContext& context,
        int modelCount,
        std::span<const int> awakeBodyIndices,
        const PhysicsExecutionSettings& execution,
        Threading::WorkerPool& workerPool
    );
    PreparedTerrainCandidateCommit PrepareCandidateCommit(
        const TerrainCandidateCommitContext& context,
        int bodyIndex,
        float availableTime,
        const TerrainContactSweepResult& sweep
    );
    void CommitCandidate( const TerrainCandidateCommitContext& context, const PreparedTerrainCandidateCommit& commit );

    std::span<const TerrainDetectionCandidate> GetDetectionCandidates() const;
    std::vector<TerrainContactManifold>& GetContactManifolds();
    const std::vector<TerrainContactManifold>& GetContactManifolds() const;
    std::span<uint8_t> GetRestApplied();
    uint64_t CollectDynamicMemoryBytes() const;
};
} // namespace Physics
} // namespace SkullbonezCore
