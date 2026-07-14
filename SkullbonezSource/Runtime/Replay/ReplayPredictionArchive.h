/*
File: SkullbonezSource/Runtime/Replay/ReplayPredictionArchive.h
Purpose:
  Declares the value-only prediction-state payload embedded in visual replay artifacts.

Summary:
  The mega replay probe saves the completed prediction once. Non-presenting
  verification restores these values into temporary domain state and requires
  an exact re-serialization before the sole engine process exits. The archive
  never stores a physics engine, worker task, pointer, renderer resource, or
  other process-local owner.

Glossary:
  Prediction archive: Typed future frames and presentation caches required to
    reproduce a completed replay prediction without running prediction physics.
  Offline reconstruction: Restoring saved values without a window, render
    submission, or prediction-generation capability.

Invariants:
  - The payload is little-endian, bounded, and field-serialized.
  - Debug contacts and private prediction-engine scratch are not presentation
    state and are intentionally excluded.
  - Loading always leaves prediction generation disabled and build state clean.

Related:
  - SkullbonezSource/Runtime/Replay/ReplayRuntime.h
  - SkullbonezSource/Runtime/Replay/ReplayV2Artifact.h
  - Agentic/Plans/TODO/replay-visual-fidelity-mega-probe.md
*/
#pragma once

#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

namespace SkullbonezCore::Runtime
{
struct RunReplayPathVisualizerState;
struct RunReplayPredictionState;

namespace ReplayPredictionArchiveOperations
{
bool BuildReplayPredictionArchive( const RunReplayPathVisualizerState& pathVisualizer,
                                   const RunReplayPredictionState& prediction,
                                   std::vector<uint8_t>& outBytes );

bool LoadReplayPredictionArchive( std::span<const uint8_t> bytes,
                                  RunReplayPathVisualizerState& pathVisualizer,
                                  RunReplayPredictionState& prediction,
                                  char* outReason,
                                  std::size_t reasonSize );

bool VerifyReplayPredictionArchiveRoundTrip( std::span<const uint8_t> bytes, char* outReason, std::size_t reasonSize );
} // namespace ReplayPredictionArchiveOperations
} // namespace SkullbonezCore::Runtime
