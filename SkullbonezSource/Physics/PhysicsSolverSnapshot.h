/*
File: SkullbonezSource/Physics/PhysicsSolverSnapshot.h
Purpose:
  Defines physics-owned solver-state snapshots consumed by replay rollback.

Summary:
  Solver snapshots are not render poses. A restorable replay tick also needs
  the persistent contact cache, sleep state, and diagnostics
  that determine the next fixed physics step. Physics owns this value contract;
  Runtime replay may retain and serialize it without defining solver state.

Glossary:
  Contact cache: Persistent contact rows and accumulated impulses reused by the
    solver for stability.
  Sleep state: Per-body flag that lets stable bodies skip simulation until woken.

Invariants:
  - Snapshot field order stays stable for replay artifact compatibility.
  - Restored snapshots contain enough state for the next fixed step to match.
  - Replay-only vector growth is registered under one fixed owner and cannot
    exceed the measured 8 MiB hard cap.

Related:
  - SkullbonezSource/Physics/PhysicsWorld.cpp
  - SkullbonezSource/Runtime/Replay/ReplayRecorder.h
  - SkullbonezSource/Runtime/Prediction/ReplayPrediction.cpp
  - Agentic/Reference/engine-glossary.md
*/
#pragma once

#include "PhysicsDebugData.h"
#include "../Maths/Vector3.h"

#include <cstdint>
#include <utility>
#include <vector>

namespace SkullbonezCore::Physics
{
inline constexpr const char* PHYSICS_SOLVER_SNAPSHOT_RESERVE_OWNER = "replay_solver_snapshot";

// The strict two-generation prediction probe measured 2,877,186 bytes.
// Eight MiB preserves 2.92x measured headroom.
inline constexpr int PHYSICS_SOLVER_SNAPSHOT_RESERVE_HARD_BYTES = 8 * 1024 * 1024;

struct PhysicsSolverContactCacheSample
{
    int64_t key = 0;
    float accN = 0.0f;
    float accT1 = 0.0f;
    float accT2 = 0.0f;
};

struct PhysicsSolverPersistentContactSample
{

    // Persistent contacts are solver rows, not just debug visuals. The cached
    // impulses below are warm-start inputs for deterministic next-frame replay.
    int bodyA = -1;
    int bodyB = -1;
    uint32_t featureId = 0;
    int64_t key = 0;
    Math::Vector::Vector3 normal = Math::Vector::ZERO_VECTOR;
    Math::Vector::Vector3 tangent1 = Math::Vector::ZERO_VECTOR;
    Math::Vector::Vector3 tangent2 = Math::Vector::ZERO_VECTOR;
    Math::Vector::Vector3 rA = Math::Vector::ZERO_VECTOR;
    Math::Vector::Vector3 rB = Math::Vector::ZERO_VECTOR;
    float penetration = 0.0f;
    float normalMass = 0.0f;
    float tangentMass1 = 0.0f;
    float tangentMass2 = 0.0f;
    float bias = 0.0f;
    float frictionLimit = 0.0f;
    float accN = 0.0f;
    float accT1 = 0.0f;
    float accT2 = 0.0f;
    bool warmStarted = false;
    bool isTerrain = false;
    bool supportsRestingPolicy = true;
    bool allowsTangentFriction = true;
    bool normalCoupledFriction = false;
    bool inhibitsSleep = false;
    uint8_t manifoldPointCount = 1;
    Math::Vector::Vector3 terrainNormal = Math::Vector::ZERO_VECTOR;
    float terrainWarmStart = 0.0f; // Terrain support seed; name/order preserve Replay v2 conversion.
};

struct PhysicsSolverStatsSample
{
    int rowCount = 0;
    int cachePreviousRows = 0;
    int cacheHits = 0;
    int cacheMisses = 0;
    int warmStartedRows = 0;
    int positionCorrectionRows = 0;
    int solverIterations = 0;
    float positionCorrectionTotal = 0.0f;
    float positionCorrectionMax = 0.0f;
};

struct PhysicsSolverSnapshot
{

    // Snapshot payload for hidden physics state. Body poses live in
    // ReplaySolverBodySample; this struct stores the caches that make the next
    // fixed physics step match after restore.
    uint32_t version = 2;
    int modelCount = 0;
    int nextSleepIslandVisualId = 1;
    bool sleepEnabled = true;
    bool collisionVisualFrameActive = false;
    std::vector<float> timeRemaining;
    std::vector<uint8_t> sleepSupportedThisFrame;
    std::vector<uint8_t> sleepInhibitedThisFrame;
    std::vector<uint8_t> sleepState;
    std::vector<uint8_t> sleepCounter;
    std::vector<uint8_t> underwaterSleepLocked;
    std::vector<uint8_t> collisionVisualContacts;
    std::vector<int> sleepIslandVisualId;
    std::vector<int> sleepIslandAssignedVisualId;
    std::vector<std::pair<int, int>> sleepSupportEdges;
    std::vector<int> sleepIslandParent;
    std::vector<uint8_t> sleepIslandRank;
    std::vector<uint8_t> sleepIslandHasAwake;
    std::vector<uint8_t> sleepIslandHasSupportAnchor;
    std::vector<uint8_t> sleepIslandEligible;
    std::vector<uint8_t> sleepIslandCanSleep;
    std::vector<PhysicsSolverPersistentContactSample> persistentContacts;
    std::vector<PhysicsSolverContactCacheSample> persistentContactCache;
    PhysicsSolverStatsSample solverStats;
    std::vector<uint16_t> persistentContactCounts;
    std::vector<uint16_t> persistentRestingContactCounts;
    std::vector<PhysicsDebugContact> debugContacts;
    std::vector<PhysicsPipelineRecord> pipelineTrace;
    std::vector<int64_t> collisionCellKeys;

    void ClearPreservingCapacity() noexcept
    {

        // Lifetime: replay prediction cancels and restarts in steady runtime.
        // Clear logical state without replacing vectors so the reserve-phase
        // storage remains registered and reusable by the next prediction.
        version = 2;
        modelCount = 0;
        nextSleepIslandVisualId = 1;
        sleepEnabled = true;
        collisionVisualFrameActive = false;
        timeRemaining.clear();
        sleepSupportedThisFrame.clear();
        sleepInhibitedThisFrame.clear();
        sleepState.clear();
        sleepCounter.clear();
        underwaterSleepLocked.clear();
        collisionVisualContacts.clear();
        sleepIslandVisualId.clear();
        sleepIslandAssignedVisualId.clear();
        sleepSupportEdges.clear();
        sleepIslandParent.clear();
        sleepIslandRank.clear();
        sleepIslandHasAwake.clear();
        sleepIslandHasSupportAnchor.clear();
        sleepIslandEligible.clear();
        sleepIslandCanSleep.clear();
        persistentContacts.clear();
        persistentContactCache.clear();
        solverStats = {};
        persistentContactCounts.clear();
        persistentRestingContactCounts.clear();
        debugContacts.clear();
        pipelineTrace.clear();
        collisionCellKeys.clear();
    }
};
} // namespace SkullbonezCore::Physics
