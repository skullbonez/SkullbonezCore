/*
File: SkullbonezSource/Physics/Stages/PhysicsStageContexts.h
Purpose:
  Names the borrowed value contexts used at PhysicsWorld fixed-step seams.

Summary:
  These remaining seam records make force and final-integration inputs explicit.
  They contain references and spans for one synchronous fixed-step dispatch
  only; none is retained after its call returns.

Glossary:
  Stage context: Non-owning bundle of stores, dense rows, and bounded scratch
    required by one fixed-step phase.
  Force context: Borrowed stores, sleep/clock rows, and optional gravity values.
  Integration context: Borrowed rows used to consume each body's remaining time.

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
#include <vector>

#include "../../Maths/Vector3.h"
#include "../PhysicsBodyStore.h"

namespace SkullbonezCore
{
namespace Physics
{
class ColliderStore;
class PhysicsBodyStore;
struct PhysicsWorldForces;

struct ApplyForcesStageContext
{
    // Lifetime: WorkerPool borrows this callable only during the current force
    // pass; all references and spans originate in the enclosing fixed step.
    PhysicsBodyStore& bodyStore;
    const ColliderStore& colliderStore;
    const PhysicsWorldForces& worldForces;
    std::span<const PhysicsBodyRecord> bodyRecords;
    PhysicsBodyHotFieldsConstView hotFields;
    std::span<const uint8_t> sleepState;
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
    PhysicsBodyHotFieldsConstView hotFields;
    std::span<const uint8_t> sleepState;
    std::span<const float> timeRemaining;

    void operator()( int bodyIndex ) const;
};

} // namespace Physics
} // namespace SkullbonezCore
