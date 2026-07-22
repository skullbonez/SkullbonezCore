/*
File: ReplayPredictionPackets.h
Purpose:
  Publishes small replay prediction policy values without exposing worker scheduling ownership.

Summary:
  Prediction scheduling selects a build mode; automation and reports may name
  that resulting value without including the worker task, join, or cancellation API.

Glossary:
  Instant build: One worker submission completes the remaining prediction horizon.
  Amortized build: Bounded worker slices spread prediction work across frames.

Invariants:
  - Packet enums carry no scheduling authority.
  - Explicit enum values are interpreted only inside the current process/report schema.

Related:
  - ReplayPredictionScheduling.h
  - ReplayPredictionView.h
*/
#pragma once

#include <cstdint>

namespace SkullbonezCore::Runtime
{
enum class ReplayPredictionBuildMode : uint8_t
{
    Undecided,
    Amortized,
    Instant
};
} // namespace SkullbonezCore::Runtime
