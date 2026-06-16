/*
File: SkullbonezSource/SkullbonezSimulationSystem.h
Purpose:
  Owns runtime simulation stepping policy and physics accumulators.

Mental model:
  SimulationSystem translates scene timing settings and operator pause/step
  state into deterministic physics ticks. SkullbonezRun remains responsible for
  camera/UI logic that consumes the returned simulation/camera deltas.

Related:
  - SkullbonezSource/SkullbonezSimulationSystem.cpp
  - SkullbonezSource/SkullbonezRunFrame.cpp
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
    double secondsPerFrame = 0.0;
    float timeScale = 1.0f;
    bool isSceneMode = false;
    bool isScenePhysicsEnabled = true;
    bool isFixedStep = false;
    bool isFlyMode = false;
    bool isNudgeMode = false;
    bool isStepRequested = false;
    GameObjects::GameModelCollection* models = nullptr;
};

struct SimulationTickResult
{
    bool shouldUpdateLogic = false;
    float simulationDt = 0.0f;
    float cameraDt = 0.0f;
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
