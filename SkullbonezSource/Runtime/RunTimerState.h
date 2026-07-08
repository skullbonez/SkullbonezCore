/*
File: SkullbonezSource/Runtime/RunTimerState.h
Purpose:
  Owns Run's frame, simulation, render, and rolling diagnostics timers.

Mental model:
  Run's main loop samples several clocks each frame, then publishes the derived
  values to rendering, HUD text, automation reports, and contact-audio
  diagnostics. This shelf keeps those timing values together while callers are
  split across input, frame, render, and UI files.

Glossary:
  Rolling timing value: Smoothed frame metric used by HUD and diagnostics so a
  single spike does not dominate display text.
  Scene energy sample: Half-second kinetic-energy bucket used by scene telemetry
  rather than physics authority.

Invariants:
  - Timer members are process-lifetime values owned by Run; borrowers may sample
    or update them during one frame but must not retain pointers across owners.
  - All durations stored here are seconds unless the field name ends with `Ms`.

Related:
  - SkullbonezSource/Runtime/Run.h
  - SkullbonezSource/Runtime/RunSubsystemState.h
  - Agentic/Reference/runtime-reference.md
  - Agentic/Reference/comment-style-guide.md
*/
#pragma once

#include "../Core/Timer.h"

namespace SkullbonezCore
{
namespace Basics
{
struct RunTimerState
{
    Environment::Timer frameTimer;
    Environment::Timer workTimer;
    Environment::Timer updateTimer;
    Environment::Timer cameraTimer;
    Environment::Timer simulationTimer;

    float physicsTime = 0.0f;              // Last frame physics time (seconds)
    float rollingPhysicsTime = 0.0f;       // Smoothed physics time accumulator
    float renderTime = 0.0f;               // Last frame render time (seconds)
    float rollingRenderTime = 0.0f;        // Smoothed render time accumulator
    float rollingFpsTime = 0.0f;           // Smoothed FPS time accumulator
    float rollingSceneEnergy = 0.0f;       // Half-second averaged kinetic energy
    float cpuFrameWorkMs = 0.0f;           // Last frame CPU work before Present/VSync
    float gpuFrameWorkMs = 0.0f;           // Last available GPU work before Present/VSync
    float contactAudioStatsLogTime = 0.0f; // Seconds since the last optional contact-audio counter print.
    float timeSinceLastRender = 0.0f;
    double sceneEnergyAccumulator = 0.0;
    int sceneEnergySampleCount = 0;
    int lastUIDrawCalls = 0;               // Actual UI draw calls measured around Frame/UI last frame
};

} // namespace Basics
} // namespace SkullbonezCore
