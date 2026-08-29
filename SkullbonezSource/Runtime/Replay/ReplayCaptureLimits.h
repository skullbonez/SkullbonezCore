/*
File: ReplayCaptureLimits.h
Purpose:
  Publishes stable replay retention and prediction-horizon limits used by startup and UI packets.

Summary:
  Capture owns the values, while startup and Presentation consume them without
  including recorder storage or mutation APIs.

Glossary:
  Default horizon: Future duration used before an operator changes prediction.
  Maximum horizon: Longest bounded future exposed by prediction controls.

Invariants:
  - These limits are configuration vocabulary, not mutable runtime state.
  - The 20-second default preserves ordinary scene and fidelity behavior.
  - The extended maximum remains subject to the prediction owner's unchanged
    byte cap; large scenes fail the reserve request instead of exceeding it.
  - Changing either value requires the normal replay compatibility and fidelity review.

Related:
  - ReplayRecorder.h
  - ReplayOverlaySurface.h
  - Agentic/Reference/engine-glossary.md
*/
#pragma once

#include "../../Physics/PhysicsTimestep.h"

namespace SkullbonezCore::Runtime
{
inline constexpr int REPLAY_PAST_BUFFER_SECONDS = 60;
inline constexpr int REPLAY_CAPTURE_TICKS_PER_SECOND = static_cast<int>( PHYSICS_FIXED_TICKS_PER_SECOND );
inline constexpr int REPLAY_MEMORY_POLICY_MIN_BUDGET_MIB = 32;
inline constexpr int REPLAY_MEMORY_POLICY_MAX_BUDGET_MIB = 512;
inline constexpr float REPLAY_FUTURE_DEFAULT_SECONDS = 20.0f;
inline constexpr float REPLAY_FUTURE_MAX_SECONDS = 120.0f;
} // namespace SkullbonezCore::Runtime
