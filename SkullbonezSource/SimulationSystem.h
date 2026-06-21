/*
File: SkullbonezSource/SimulationSystem.h
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

Related:
  - SkullbonezSource/SimulationSystem.cpp
  - SkullbonezSource/RunFrame.cpp
  - Agentic/Reference/runtime-reference.md
*/
#pragma once

namespace SkullbonezCore
{
namespace GameObjects
{
class GameModelCollection;
}

namespace Basics
{
struct SimulationTickInput
{
    using PhysicsStepCallback = void ( * )( void* userData );

    double secondsPerFrame = 0.0;
    float timeScale = 1.0f;
    bool isSceneMode = false;
    bool isScenePhysicsEnabled = true;
    bool isFixedStep = false;
    bool isFlyMode = false;
    bool isLauncherMode = false;
    bool isStepRequested = false;
    GameObjects::GameModelCollection* models = nullptr;
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
