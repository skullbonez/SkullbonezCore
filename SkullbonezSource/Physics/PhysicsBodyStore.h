/*
File: SkullbonezSource/Physics/PhysicsBodyStore.h
Purpose:
  Owns a deterministic body-order snapshot of model physics state.

Mental model:
  GameModelCollection is still the compatibility adapter, but physics-facing
  body data now has an explicit store boundary. The store mirrors body index
  order and replay ids so future migrations can move fields without changing
  solver ordering.

Glossary:
  Body: Simulated object state consumed by the physics step.
  Sleep: Optimization that skips stable bodies until contact or user action
    wakes them.
  Inverse mass: Reciprocal mass value; zero means an immovable body.
  Replay body id: Stable per-scene id used by replay and diagnostics.

Invariants:
  - Body records stay in GameModelCollection physics model order.
  - Store refreshes are observational; physics mutation remains elsewhere.

Related:
  - SkullbonezSource/Physics/PhysicsBodyStore.cpp
  - SkullbonezSource/Physics/PhysicsScene.h
  - Agentic/Plans/physics-playground-refactor-and-file-prefix-cleanup-plan.md
*/
#pragma once

#include <cstdint>
#include <vector>

#include "../Maths/Vector3.h"

namespace SkullbonezCore
{
namespace GameObjects
{
class GameModel;
}

namespace Physics
{
struct PhysicsBodyRecord
{
    uint32_t replayBodyId = 0;               // Stable replay-facing body id for this scene.
    Math::Vector::Vector3 position = Math::Vector::ZERO_VECTOR;
    Math::Vector::Vector3 linearVelocity = Math::Vector::ZERO_VECTOR;
    Math::Vector::Vector3 angularVelocity = Math::Vector::ZERO_VECTOR;
    Math::Vector::Vector3 rotationalInertia = Math::Vector::ZERO_VECTOR;
    Math::Vector::Vector3 invRotationalInertia = Math::Vector::ZERO_VECTOR;
    float mass = 0.0f;                       // Authoring mass; fixed bodies still report mass.
    float invMass = 0.0f;                    // Solver inverse mass; fixed bodies use zero.
    bool isFixed = false;                    // True for immovable collision bodies.
    bool isSleeping = false;                 // Mirrors PhysicsWorld sleep state by model index.
};

class PhysicsBodyStore
{
  public:
    PhysicsBodyStore();

    void Clear();
    void Refresh( std::vector<GameObjects::GameModel>& models, const std::vector<uint8_t>& sleepStates );

    const PhysicsBodyRecord* Data() const;
    int Count() const;
    bool Empty() const;
    const std::vector<PhysicsBodyRecord>& Records() const;

  private:
    std::vector<PhysicsBodyRecord> m_bodies; // Body records in GameModelCollection index order.
};
} // namespace Physics
} // namespace SkullbonezCore
