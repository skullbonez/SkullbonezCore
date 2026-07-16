/*
File: SkullbonezSource/Physics/Stages/PhysicsTerrainStage.h
Purpose:
  Owns swept-terrain detection scratch, contact manifolds, and rest-policy rows.

Summary:
  PhysicsTerrainStage detects one candidate per body and converts tested hits
  into terrain manifolds. A typed two-phase commit lets PhysicsWorld preserve
  the exact diagnostics and visual-emission point while the stage owns terrain
  storage and writes borrowed sleep-support outputs.

Glossary:
  Detection candidate: Per-body swept terrain result produced independently.
  Prepared commit: Stack value containing the manifold and diagnostic record
    computed before sequencer-owned side effects are emitted.
  Rest-applied row: Solver scratch preventing duplicate terrain rest response.

Invariants:
  - Detection workers write only the candidate slot matching their body index.
  - Candidate commit remains serial in ascending model order.
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

#include "../../Runtime/Scene/SceneCapacity.h"
#include "../PhysicsDebugData.h"
#include "../PhysicsBodyStore.h"
#include "../TerrainContactManifold.h"

namespace SkullbonezCore
{
namespace Core
{
class EngineConfig;
struct PhysicsExecutionConfig;
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
    const Core::EngineConfig& config;
    std::span<const uint8_t> sleepState;
    std::span<const float> timeRemaining;
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
    const Core::EngineConfig& config;
    std::span<uint8_t> sleepSupportedThisFrame;
    std::span<uint8_t> sleepInhibitedThisFrame;
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
    std::array<uint8_t, Scene::Capacity::MAX_GAME_MODELS> m_restApplied = {};

    struct TerrainDetectionStage
    {
        PhysicsTerrainStage& stage;
        const TerrainDetectionStageContext& context;

        void operator()( int bodyIndex ) const;
    };

    void DetectTerrainAt( const TerrainDetectionStageContext& context, int bodyIndex );

  public:
    PhysicsTerrainStage();

    void Clear();
    void BeginFrame();
    void Detect( const TerrainDetectionStageContext& context,
                 int modelCount,
                 const Core::PhysicsExecutionConfig& execution,
                 Threading::WorkerPool& workerPool );
    PreparedTerrainCandidateCommit PrepareCandidateCommit( const TerrainCandidateCommitContext& context,
                                                           int bodyIndex,
                                                           float availableTime,
                                                           const TerrainContactSweepResult& sweep );
    void CommitCandidate( const TerrainCandidateCommitContext& context, const PreparedTerrainCandidateCommit& commit );

    std::span<const TerrainDetectionCandidate> GetDetectionCandidates() const;
    std::vector<TerrainContactManifold>& GetContactManifolds();
    const std::vector<TerrainContactManifold>& GetContactManifolds() const;
    std::span<uint8_t> GetRestApplied();
    uint64_t CollectDynamicMemoryBytes() const;
};
} // namespace Physics
} // namespace SkullbonezCore
