/*
File: SkullbonezSource/Runtime/App/RunTimerState.h
Purpose:
  Owns Run's frame, simulation, render, and rolling diagnostics timers.

Summary:
  Run's main loop samples several clocks each frame, then publishes the derived
  values to rendering, HUD text, and automation reports. This shelf keeps those
  timing values together while callers are split across input, frame, render,
  and UI files.

Glossary:
  Rolling timing value: Smoothed frame metric used by HUD and diagnostics so a
  single spike does not dominate display text.
  Scene energy sample: Half-second kinetic-energy bucket used by scene telemetry
  rather than physics authority.
  Timer startup boundary: Explicit high-resolution counter check that can fail
    from platform/environment limits before the frame loop begins.

Invariants:
  - Timer members are process-lifetime values owned by Run; borrowers may sample
    or update them during one frame but must not retain pointers across owners.
  - Run calls Initialise() once before scene loading starts; timer samples before
    that boundary are fatal owner bugs.
  - All durations stored here are seconds unless the field name ends with `Ms`.

Related:
  - SkullbonezSource/Runtime/App/Run.h
  - SkullbonezSource/Runtime/App/RunFrame.cpp
  - Agentic/Reference/runtime-reference.md
  - Agentic/Reference/engine-glossary.md
*/
#pragma once

#include "../../Core/Timer.h"
#include "../Scene/SceneLifecycle.h"

namespace SkullbonezCore
{
namespace Runtime
{
struct RunTimerSceneLifecycleActions
{
    bool resetMeasurements = false;
    bool restartClocks = false;
};

// Concept: the value-only policy decides which timer actions belong to a
// generation. Keeping clock mutation outside this policy makes failed-load and
// duplicate-observation behavior testable without platform timer linkage.
class RunTimerSceneLifecyclePolicy
{
  public:
    RunTimerSceneLifecycleActions Observe( const SceneLifecyclePacket& packet )
    {
        return RunTimerSceneLifecycleActions { m_sceneResetObserver
                                                   .ShouldApply( packet, SceneRuntimeLifecycleEvent::AfterSceneCleared ),
                                               m_sceneActivationObserver
                                                   .ShouldApply( packet, SceneRuntimeLifecycleEvent::AfterSceneActivated ) };
    }

    uint64_t LastResetGeneration() const
    {
        return m_sceneResetObserver.LastAppliedGeneration();
    }
    uint64_t LastActivationGeneration() const
    {
        return m_sceneActivationObserver.LastAppliedGeneration();
    }

  private:
    SceneLifecycleGenerationObserver m_sceneResetObserver;
    SceneLifecycleGenerationObserver m_sceneActivationObserver;
};

struct RunTimerState
{
    SkullbonezCore::Core::SbResult Initialise( SkullbonezCore::Core::SbDiagnosticStore& diagnostics )
    {
        const SkullbonezCore::Core::SbResult frameTimerResult = frameTimer.Initialise( diagnostics );

        if ( !frameTimerResult.Ok() )
        {
            return frameTimerResult;
        }

        const SkullbonezCore::Core::SbResult workTimerResult = workTimer.Initialise( diagnostics );

        if ( !workTimerResult.Ok() )
        {
            return workTimerResult;
        }

        const SkullbonezCore::Core::SbResult updateTimerResult = updateTimer.Initialise( diagnostics );

        if ( !updateTimerResult.Ok() )
        {
            return updateTimerResult;
        }

        const SkullbonezCore::Core::SbResult cameraTimerResult = cameraTimer.Initialise( diagnostics );

        if ( !cameraTimerResult.Ok() )
        {
            return cameraTimerResult;
        }

        return simulationTimer.Initialise( diagnostics );
    }

    void ResetSceneMeasurements()
    {
        timeSinceLastRender = 0.0f;
        renderTime = 0.0f;
        rollingRenderTime = 0.0f;
        physicsTime = 0.0f;
        rollingPhysicsTime = 0.0f;
        rollingFpsTime = 0.0f;
        rollingSceneEnergy = 0.0f;
        cpuFrameWorkMs = 0.0f;
        gpuFrameWorkMs = 0.0f;
        sceneEnergyAccumulator = 0.0;
        sceneEnergySampleCount = 0;
        lastUIDrawCalls = 0;
    }

    void RestartForSceneActivation()
    {
        frameTimer.StartTimer();
        workTimer.StartTimer();
        updateTimer.StartTimer();
        cameraTimer.StartTimer();
        simulationTimer.StartTimer();
    }

    // Applies the two timer-owned lifecycle actions once per generation. A
    // failed attempt that reached clear resets measurements but does not restart
    // clocks; activation performs both actions in their original order.
    void ObserveSceneLifecycle( const SceneLifecyclePacket& packet )
    {
        const RunTimerSceneLifecycleActions actions = m_sceneLifecyclePolicy.Observe( packet );

        if ( actions.resetMeasurements )
        {
            ResetSceneMeasurements();
        }

        if ( actions.restartClocks )
        {
            RestartForSceneActivation();
        }
    }

    uint64_t LastSceneResetGeneration() const
    {
        return m_sceneLifecyclePolicy.LastResetGeneration();
    }
    uint64_t LastSceneActivationGeneration() const
    {
        return m_sceneLifecyclePolicy.LastActivationGeneration();
    }

    Environment::Timer frameTimer;
    Environment::Timer workTimer;
    Environment::Timer updateTimer;
    Environment::Timer cameraTimer;
    Environment::Timer simulationTimer;

    float physicsTime = 0.0f;        // Last frame physics time (seconds)
    float rollingPhysicsTime = 0.0f; // Smoothed physics time accumulator
    float renderTime = 0.0f;         // Last frame render time (seconds)
    float rollingRenderTime = 0.0f;  // Smoothed render time accumulator
    float rollingFpsTime = 0.0f;     // Smoothed FPS time accumulator
    float rollingSceneEnergy = 0.0f; // Half-second averaged kinetic energy
    float cpuFrameWorkMs = 0.0f;     // Last frame CPU work before Present/VSync
    float gpuFrameWorkMs = 0.0f;     // Last available GPU work before Present/VSync
    float timeSinceLastRender = 0.0f;
    double sceneEnergyAccumulator = 0.0;
    int sceneEnergySampleCount = 0;
    int lastUIDrawCalls = 0;         // Actual UI draw calls measured around Frame/UI last frame

  private:
    RunTimerSceneLifecyclePolicy m_sceneLifecyclePolicy;
};

} // namespace Runtime
} // namespace SkullbonezCore
