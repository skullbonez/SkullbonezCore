/*
File: SkullbonezSource/Runtime/Simulation/SimulationSystem.h
Purpose:
  Owns runtime simulation stepping policy and physics accumulators.

Summary:
  SimulationSystem translates an effective pacing policy plus operator
  pause/step state into deterministic physics ticks. Run resolves process/scene
  launch intent and retains the camera/UI coordination that consumes the
  returned simulation/camera deltas.

Invariants:
  - SimulationSystem decides tick counts only; it does not borrow model owners,
    physics stores, world forces, worker pools, or presentation callbacks.
  - Tick/drop counts report work committed or discarded this call. Frame-level
    simulation/camera deltas report sanitized input even when no whole tick
    commits; accumulator state remains private to SimulationSystem.
  - Render-frame lockstep and paused paths publish alpha 1 so capture/replay
    semantics stay on exact committed solver state.
  - Catch-up drops whole ticks only; fractional accumulator state survives.
  - Non-finite timing inputs contribute no time; unrepresentable requested
    whole-tick counts saturate at int max before cap/drop arithmetic.
  - SimulationTickInput is one synchronous scheduler decision; the focused
    TestSimulationSystem suite pins every pacing and pause/step field together.

Related:
  - SkullbonezSource/Runtime/Simulation/SimulationSystem.cpp
  - SkullbonezSource/Runtime/App/RunFrame.cpp
  - SkullbonezTests/TestSimulationSystem.cpp
  - Agentic/Reference/runtime-reference.md
  - Agentic/Reference/engine-glossary.md
*/
#pragma once

#include "../Interaction/RuntimeInteractionController.h"
#include "../Scene/SceneLifecycle.h"

#include <cstdint>

namespace SkullbonezCore::Runtime
{
enum class SimulationPacingPolicy : uint8_t
{
    WallClock = 0,
    RenderFrameLockstep
};

// Invariant: explicit launch policy always wins. A scene-session request is
// intentionally bounded to unattended finite captures so live and unlimited
// scenes cannot change simulation speed with render frequency.
constexpr SimulationPacingPolicy ResolveSimulationPacingPolicy( bool explicitRenderFrameLockstep,
                                                                bool sceneRenderFrameLockstepRequest, int targetFrameCount,
                                                                bool isInteractiveRun ) noexcept
{
    if ( explicitRenderFrameLockstep || ( sceneRenderFrameLockstepRequest && targetFrameCount > 0 && !isInteractiveRun ) )
    {
        return SimulationPacingPolicy::RenderFrameLockstep;
    }

    return SimulationPacingPolicy::WallClock;
}

struct SimulationTickInput
{
    double secondsPerFrame = 0.0;
    float timeScale = 1.0f;
    bool isSceneMode = false;
    bool isScenePhysicsEnabled = true;
    SimulationPacingPolicy pacingPolicy = SimulationPacingPolicy::WallClock;
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
    int droppedPhysicsTicks = 0;    // Whole fixed-timestep drops derived from the saturated requested count.
    float presentationAlpha = 1.0f; // Wall-clock fraction, or exact-state 1.0 for lockstep and paused paths.
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
    double m_physicsAccumulator = 0.0;
    double m_renderFrameLockstepTickAccumulator = 0.0;
    uint64_t m_droppedPhysicsTickCount = 0;
    uint64_t m_physicsHitchEventCount = 0;
    SceneLifecycleGenerationObserver m_sceneResetObserver;
};
} // namespace SkullbonezCore::Runtime
