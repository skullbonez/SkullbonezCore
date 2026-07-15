/*
File: SkullbonezSource/Physics/Stages/PhysicsStageContexts.h
Purpose:
  Names the borrowed value contexts used at PhysicsWorld fixed-step seams.

Summary:
  These records make stage inputs explicit before scratch storage and algorithms
  move into their concrete owners. They contain references and spans for one
  synchronous fixed-step dispatch only; none is retained after its call returns.

Glossary:
  Stage context: Non-owning bundle of stores, dense rows, and bounded scratch
    required by one fixed-step phase.
  Commit context: Inputs used by the serial terrain pass after worker detection.
  Narrowphase event: Bounded per-pair output committed later in pair order.
  Island dispatch: Parallel work partition whose islands never write the same
    body row.

Invariants:
  - Context field order and construction order preserve the certified P0 call
    sites; changing either can hide accidental argument swaps.
  - Contexts never outlive the synchronous WorkerPool dispatch that borrows them.
  - These seam types own no vectors and perform no allocation.

Related:
  - SkullbonezSource/Physics/PhysicsWorld.h
  - SkullbonezSource/Physics/PhysicsWorld.cpp
  - Agentic/Reports/2026-07-15/physicsworld-ownership-map.md
*/
#pragma once

#include <cstdint>
#include <span>
#include <utility>
#include <vector>

#include "../PersistentContactSolver.h"
#include "../PhysicsDebugData.h"
#include "../TerrainContactManifold.h"
#include "../../Maths/Vector3.h"

namespace SkullbonezCore
{
namespace Core
{
class EngineConfig;
} // namespace Core

namespace Physics
{
class ColliderStore;
class PhysicsBodyStore;
struct ColliderRecord;
struct PhysicsBodyRecord;
struct PhysicsWorldForces;

struct ApplyForcesStageContext
{
    // Lifetime: WorkerPool borrows this callable only during the current force
    // pass; all references and spans originate in the enclosing fixed step.
    PhysicsBodyStore& bodyStore;
    const ColliderStore& colliderStore;
    const PhysicsWorldForces& worldForces;
    std::span<const PhysicsBodyRecord> bodyRecords;
    std::vector<uint8_t>& sleepState;
    std::vector<float>& timeRemaining;
    const Math::Vector::Vector3* mutualGravityForces = nullptr;
    float dt = 0.0f;

    void operator()( int bodyIndex ) const;
};

struct IntegrateRemainingStageContext
{
    // Lifetime: the final integration dispatch borrows current solver records
    // and the cross-stage remaining-time array; it retains neither.
    PhysicsBodyStore& bodyStore;
    const ColliderStore& colliderStore;
    std::span<const PhysicsBodyRecord> bodyRecords;
    const std::vector<uint8_t>& sleepState;
    const std::vector<float>& timeRemaining;

    void operator()( int bodyIndex ) const;
};

struct TerrainDetectionCandidate
{
    float availableTime = 0.0f;
    TerrainContactSweepResult sweep;
    uint8_t tested = 0;
};

struct TerrainDetectionStageContext
{
    // Lifetime: terrain workers read one fixed-step snapshot and write only the
    // candidate row matching their body index.
    std::span<const PhysicsBodyRecord> bodyRecords;
    std::span<const ColliderRecord> colliderRecords;
    const SkullbonezCore::Core::EngineConfig& config;
    const std::vector<uint8_t>& sleepState;
    const std::vector<float>& timeRemaining;
    std::vector<TerrainDetectionCandidate>& candidates;
};

struct TerrainCandidateCommitContext
{
    // Lifetime: serial commits borrow solver rows and side-effect arrays while
    // they still describe the current terrain phase.
    PhysicsBodyStore& bodyStore;
    const ColliderStore& colliderStore;
    std::span<const PhysicsBodyRecord> bodyRecords;
    std::span<const ColliderRecord> colliderRecords;
    const SkullbonezCore::Core::EngineConfig& config;
    std::vector<TerrainContactManifold>& terrainContactManifolds;
    std::vector<uint8_t>& sleepSupportedThisFrame;
    std::vector<uint8_t>& sleepInhibitedThisFrame;
    std::vector<float>& timeRemaining;
};

enum class ObjectNarrowphaseEventKind : uint8_t
{
    None,
    SweptObjectHit,
    SweptObjectMiss,
    WakeDecision
};

struct ObjectNarrowphaseEvent
{
    // Invariant: worker passes fill one event per candidate-pair slot; the
    // facade commits those slots later in original pair order.
    ObjectNarrowphaseEventKind kind = ObjectNarrowphaseEventKind::None;
    PhysicsPipelineRecord pipelineRecord;
    int collisionTimeBodyA = -1;
    int collisionTimeBodyB = -1;
    float collisionTime = 0.0f;
    float availableTime = 0.0f;
    int visualBodyA = -1;
    int visualBodyB = -1;
    int64_t collisionCellKey = 0;
    uint8_t hasPipelineRecord = 0;
    uint8_t emitCollisionTime = 0;
    uint8_t markVisualContact = 0;
    uint8_t hasCollisionCellKey = 0;
};

struct ObjectNarrowphasePairStageContext
{
    // Lifetime: the serial loop or bounded island dispatch borrows these inputs
    // only for the current narrowphase pass.
    PhysicsBodyStore& bodyStore;
    const ColliderStore& colliderStore;
    const PhysicsWorldForces& worldForces;
    std::span<PhysicsBodyRecord> bodyRecords;
    std::span<const ColliderRecord> colliderRecords;
    std::span<const std::pair<int, int>> candidatePairs;
    std::vector<uint8_t>& sleepState;
    std::vector<uint8_t>& sleepCounter;
    std::vector<int>& sleepIslandVisualId;
    std::vector<float>& timeRemaining;
    const std::vector<uint8_t>& underwaterSleepLocked;
    const std::vector<PersistentContactCacheEntry>& persistentContactCache;
    int modelCount = 0;
    float sleepLinearSq = 0.0f;
    float sleepAngularSq = 0.0f;
    float contactSkin = 0.0f;
    float contactEpsilon = 0.0f;
    float invCellSize = 0.0f;
    float dt = 0.0f;
};

struct ObjectNarrowphaseIsland
{
    int minPairIndex = 0;
    size_t firstPairOffset = 0;
    size_t pairCount = 0;
};
} // namespace Physics
} // namespace SkullbonezCore
