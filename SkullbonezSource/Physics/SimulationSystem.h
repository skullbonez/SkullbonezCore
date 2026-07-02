/*
File: SkullbonezSource/Physics/SimulationSystem.h
Purpose:
  Owns runtime simulation stepping policy and physics accumulators.

Mental model:
  SimulationSystem translates scene timing settings and operator pause/step
  state into deterministic physics ticks. Run remains responsible for
  camera/UI logic that consumes the returned simulation/camera deltas.

Glossary:
  Fixed-step: Deterministic mode that advances physics by one fixed delta per
  requested tick instead of wall-clock time.
  Accumulator: Stored fractional tick state that carries time across frames.
  PhysicsModelAccess: Temporary compatibility interface that lets the physics
    world step legacy model-backed storage without naming GameModelCollection.

Invariants:
  - SimulationTickInput borrows physics step context only for the duration of Tick.
  - SimulationSystem must not know which runtime object owns the concrete
    physics world; Run supplies that adapter through PhysicsModelAccess.
  - Result deltas report what was committed this call; accumulator state remains
    private to SimulationSystem.

Related:
  - SkullbonezSource/Physics/SimulationSystem.cpp
  - SkullbonezSource/Runtime/RunFrame.cpp
  - Agentic/Reference/runtime-reference.md
*/
#pragma once

#include "../Runtime/RuntimeInteractionController.h"

namespace SkullbonezCore
{
namespace Physics
{
class PhysicsEngine;
class PhysicsModelAccess;
} // namespace Physics

namespace Threading
{
class WorkerPool;
} // namespace Threading

namespace Basics
{
class EngineConfig;

struct SimulationPhysicsStep
{
    Physics::PhysicsEngine* engine = nullptr;
    Physics::PhysicsModelAccess* modelAccess = nullptr;
    const EngineConfig* config = nullptr;
    Threading::WorkerPool* workerPool = nullptr;

    // Returns true when every borrowed physics-step service is present.
    bool IsBound() const;

    // Advances one fixed physics step through the current engine, compatibility model access, and Run-owned services.
    void Run( float deltaSeconds ) const;
};

struct SimulationTickInput
{
    using PhysicsStepCallback = void ( * )( void* userData );

    double secondsPerFrame = 0.0;
    float timeScale = 1.0f;
    bool isSceneMode = false;
    bool isScenePhysicsEnabled = true;
    bool isFixedStep = false;
    PhysicsAdvanceState physicsAdvance = PhysicsAdvanceState::Running;
    bool isStepRequested = false;
    SimulationPhysicsStep physicsStep;
    PhysicsStepCallback beforePhysicsStep = nullptr;
    void* beforePhysicsStepUserData = nullptr;
    PhysicsStepCallback afterPhysicsStep = nullptr;
    void* afterPhysicsStepUserData = nullptr;
};

struct SimulationTickResult
{
    bool shouldUpdateLogic = false;
    float simulationDt = 0.0f;
    float cameraDt = 0.0f;
    int committedPhysicsTicks = 0;
};

class SimulationSystem
{
  public:
    void Reset();
    SimulationTickResult Tick( const SimulationTickInput& input );

  private:
    float m_physicsAccumulator = 0.0f;
    float m_fixedStepTickAccumulator = 0.0f;
};
} // namespace Basics
} // namespace SkullbonezCore
