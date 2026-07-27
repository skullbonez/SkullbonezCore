/*
File: SkullbonezSource/Runtime/Simulation/SimulationSystem.h
Purpose:
  Owns runtime simulation stepping policy and physics accumulators.

Summary:
  SimulationSystem translates scene timing settings and operator pause/step
  state into deterministic physics ticks. Run remains responsible for
  camera/UI logic that consumes the returned simulation/camera deltas.

Glossary:
  Fixed-step: Deterministic mode that advances physics by one fixed delta per
    requested tick instead of wall-clock time.
  Accumulator: Stored fractional tick state that carries time across frames.
  Commit count: Number of fixed physics ticks the runtime owner must execute
    after the scheduler has updated accumulator state.
  Presentation alpha: Bounded leftover accumulator fraction used only to
    display between the previous and current completed physics poses.
  Hitch event: A fixed-step request whose whole-tick demand exceeds the
    per-frame catch-up cap; excess whole ticks are intentionally discarded.

Invariants:
  - SimulationSystem decides tick counts only; it does not borrow model owners,
    physics stores, world forces, worker pools, or presentation callbacks.
  - Result deltas report what was committed this call; accumulator state remains
    private to SimulationSystem.
  - Deterministic and paused paths publish alpha 1 so capture/replay semantics
    stay on exact committed solver state.
  - Catch-up drops whole ticks only; fractional accumulator state survives.

Related:
  - SkullbonezSource/Runtime/Simulation/SimulationSystem.cpp
  - SkullbonezSource/Runtime/App/RunFrame.cpp
  - Agentic/Reference/runtime-reference.md
*/
#pragma once

#include "../Interaction/RuntimeInteractionController.h"
#include "../Scene/SceneLifecycle.h"

#include <cstdint>

namespace SkullbonezCore::Runtime
{
struct SimulationTickInput
{
    double secondsPerFrame = 0.0;
    float timeScale = 1.0f;
    bool isSceneMode = false;
    bool isScenePhysicsEnabled = true;
    bool isFixedStep = false;
    PhysicsAdvanceState physicsAdvance = PhysicsAdvanceState::Running;
    bool isStepRequested = false;

    // True only when the runtime owner has a valid physics target for the
    // returned commit count.
    bool canStepPhysics = false;
};

struct SimulationTickResult
{
    bool shouldUpdateLogic = false;
    float simulationDt = 0.0f;
    float cameraDt = 0.0f;
    int committedPhysicsTicks = 0;
    int droppedPhysicsTicks = 0;    // Whole fixed-step ticks discarded by the catch-up cap this call.
    float presentationAlpha = 1.0f; // Leftover fixed-tick fraction; captures may override this to exact state.
};

class SimulationSystem
{
  public:
    void Reset();

    // Applies the pacing reset once after a scene transaction reaches clear;
    // SimulationSystem never needs to participate in scene population.
    void ObserveSceneLifecycle( const SceneLifecyclePacket& packet );
    SimulationTickResult Tick( const SimulationTickInput& input );

    // Cumulative diagnostics since Reset; callers may sample them without
    // mutating scheduler state or retaining an accumulator view.
    uint64_t DroppedPhysicsTickCount() const noexcept;
    uint64_t PhysicsHitchEventCount() const noexcept;

  private:
    float m_physicsAccumulator = 0.0f;
    float m_fixedStepTickAccumulator = 0.0f;
    uint64_t m_droppedPhysicsTickCount = 0;
    uint64_t m_physicsHitchEventCount = 0;
    SceneLifecycleGenerationObserver m_sceneResetObserver;
};
} // namespace SkullbonezCore::Runtime
