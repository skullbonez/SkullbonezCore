/*
File: SkullbonezSource/Physics/PhysicsTimestep.h
Purpose:
  Owns the fixed-timestep constants used by physics simulation and replay
  frame-to-seconds conversion.

Summary:
  Physics advances in deterministic 120 Hz ticks. Runtime code may choose how
  many ticks to run for a rendered frame, but each physics tick uses the same
  fixed duration so validation baselines and replay samples stay comparable.

Glossary:
  Catch-up cap: Per-render-frame limit that prevents one slow frame from
    executing unbounded physics work.

Invariants:
  - PHYSICS_FIXED_DT_SECONDS is the scheduler's canonical double-precision
    120 Hz interval, while PHYSICS_FIXED_DT is the float duration passed to each
    solver step.
  - Changing either fixed duration changes replay timing and byte-exact physics
    CSV baselines.
  - Changing PHYSICS_MAX_STEPS_PER_FRAME changes runtime catch-up behavior and
    stress-scene determinism.

Related:
  - SkullbonezSource/Runtime/Simulation/SimulationSystem.cpp
  - SkullbonezSource/Runtime/Scene/SceneSessionState.h
  - Agentic/Reference/runtime-reference.md
  - Agentic/Reference/engine-glossary.md
*/
#pragma once

constexpr double PHYSICS_FIXED_TICKS_PER_SECOND = 120.0;
constexpr double PHYSICS_FIXED_DT_SECONDS = 1.0 / PHYSICS_FIXED_TICKS_PER_SECOND;
constexpr float PHYSICS_FIXED_DT = static_cast<float>( PHYSICS_FIXED_DT_SECONDS );
constexpr int PHYSICS_MAX_STEPS_PER_FRAME = 8;
