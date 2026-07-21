/*
File: ReplayCaptureLimits.h
Purpose:
  Publishes stable replay retention and prediction-horizon limits used by startup and UI packets.

Summary:
  Capture owns the values, while startup and Presentation consume them without
  including recorder storage or mutation APIs.

Glossary:
  Retention window: Maximum authored duration requested for retained past samples.
  Prediction horizon: Maximum future duration exposed by replay prediction controls.

Invariants:
  - These limits are configuration vocabulary, not mutable runtime state.
  - Changing either value requires the normal replay compatibility and fidelity review.

Related:
  - ReplayRecorder.h
  - ReplayOverlaySurface.h
*/
#pragma once

namespace SkullbonezCore::Runtime
{
inline constexpr int REPLAY_PAST_BUFFER_SECONDS = 60;
inline constexpr float REPLAY_FUTURE_BUFFER_SECONDS = 20.0f;
} // namespace SkullbonezCore::Runtime
