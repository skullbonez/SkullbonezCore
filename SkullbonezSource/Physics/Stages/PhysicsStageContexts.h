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

} // namespace Physics
} // namespace SkullbonezCore
