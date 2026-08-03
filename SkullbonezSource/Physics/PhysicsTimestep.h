/*
File: SkullbonezSource/Physics/PhysicsTimestep.h
Purpose:
  Owns the fixed-step timing constants used by physics simulation and replay
  frame-to-seconds conversion.

Summary:
  Physics advances in deterministic 120 Hz ticks. Runtime code may choose how
  many ticks to run for a rendered frame, but each physics tick uses the same
  fixed duration so validation baselines and replay samples stay comparable.

Glossary:
  Fixed timestep: Constant simulation interval applied to each physics step.
  Catch-up cap: Per-render-frame limit that prevents one slow frame from
    executing unbounded physics work.

Invariants:
  - Changing PHYSICS_FIXED_DT changes replay timing and byte-exact physics CSV
    baselines.
  - Changing PHYSICS_MAX_STEPS_PER_FRAME changes runtime catch-up behavior and
    stress-scene determinism.

Related:
  - SkullbonezSource/Runtime/Simulation/SimulationSystem.cpp
  - SkullbonezSource/Core/Common.h includes this during the aliasing period.
*/
#pragma once

constexpr float PHYSICS_FIXED_DT = 1.0f / 120.0f;
constexpr int PHYSICS_MAX_STEPS_PER_FRAME = 8;
