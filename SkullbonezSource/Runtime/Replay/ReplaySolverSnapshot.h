/*
File: SkullbonezSource/Runtime/Replay/ReplaySolverSnapshot.h
Purpose:
  Defines retained solver-state snapshots used by replay rollback.

Mental model:
  Solver replay samples are not just render poses. A restorable replay tick also
  needs the persistent contact cache and sleep/tornado state that affect the
  next fixed physics step.

Glossary:
  Solver sample: Physics-facing state retained for rollback and diagnostics.
  Contact cache: Persistent contact rows and accumulated impulses reused by the
    solver for stability.
  Sleep state: Per-body flag that lets stable bodies skip simulation until woken.
  Tornado field: Runtime force field whose state affects future physics ticks.

Invariants:
  - Snapshot field order should stay stable for replay artifact compatibility.
  - Restored samples must contain enough state for the next fixed step to match.

Related:
  - SkullbonezSource/Runtime/Replay/ReplayRecorder.h
  - SkullbonezSource/Runtime/Replay/RunReplayTools.cpp
*/
#pragma once

#include <cstdint>
#include <utility>
#include <vector>

#include "../../Physics/Debug/PhysicsDebugVisualizer.h"
#include "../../Physics/TornadoField.h"
#include "../../Maths/Vector3.h"

namespace SkullbonezCore
{
namespace Basics
{
struct ReplaySolverContactCacheSample
{
    int64_t key = 0;
    float accN = 0.0f;
    float accT1 = 0.0f;
    float accT2 = 0.0f;
};

struct ReplaySolverPersistentContactSample
{
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
    float terrainWarmStart = 0.0f;
};

struct ReplaySolverStatsSample
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

struct ReplaySolverWorldSnapshot
{
    uint32_t version = 2;
    int modelCount = 0;
    int nextSleepIslandVisualId = 1;
    bool sleepEnabled = true;
    bool collisionVisualFrameActive = false;
    Physics::TornadoFieldConfig tornadoConfig;
    Physics::TornadoSystemConfig tornadoSystemConfig;
    float tornadoSystemElapsedSeconds = 0.0f;
    std::vector<float> timeRemaining;
    std::vector<uint8_t> sleepSupportedThisFrame;
    std::vector<uint8_t> sleepInhibitedThisFrame;
    std::vector<uint8_t> sleepState;
    std::vector<uint8_t> sleepCounter;
    std::vector<uint8_t> underwaterSleepLocked;
    std::vector<float> tornadoCaptureSeconds;
    std::vector<float> tornadoEjectCooldownSeconds;
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
    std::vector<ReplaySolverPersistentContactSample> persistentContacts;
    std::vector<ReplaySolverContactCacheSample> persistentContactCache;
    ReplaySolverStatsSample solverStats;
    std::vector<uint16_t> persistentContactCounts;
    std::vector<uint16_t> persistentRestingContactCounts;
    std::vector<Physics::PhysicsDebugContact> debugContacts;
    std::vector<Physics::PhysicsPipelineRecord> pipelineTrace;
    std::vector<int64_t> collisionCellKeys;
};
} // namespace Basics
} // namespace SkullbonezCore
