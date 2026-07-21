/*
File: ReplayPresentationPackets.h
Purpose:
  Publishes replay's frame-local render and HUD values without exposing the mutable Presentation owner.

Summary:
  Runtime render and UI composition consume these synchronous values after
  ReplayRuntime has selected and prepared one frame. All pointers borrow Replay
  owner storage and are invalidated by the next replay update.

Glossary:
  Render frame view: Read-only pose, prediction, ghost, and focus-mask values for one render turn.
  HUD (Heads-Up Display): Scalar replay diagnostics shown by the late UI pass.

Invariants:
  - Packets contain no mutable replay owner or scheduling operation.
  - Frame-view pointers are consumed synchronously before replay mutation.
  - HUD memory statistics are populated only when explicitly requested.

Related:
  - ReplayRuntime.h publishes these packets.
  - ReplayPresentation.h owns the mutable state that produces them.
*/
#pragma once

#include "../../Core/MainMemoryStats.h"

#include <cstdint>
#include <vector>

namespace SkullbonezCore::Runtime
{
struct ReplayPresentationSample;
struct ReplaySolverFrameSample;
struct ReplayVisualPacket;
struct RunReplayPredictionFrame;

struct ReplayRenderFrameView
{
    const ReplayPresentationSample* presentationSample = nullptr;
    const ReplaySolverFrameSample* solverSample = nullptr;
    const RunReplayPredictionFrame* predictionFrame = nullptr;
    const ReplayVisualPacket* visualPacket = nullptr;
    const std::vector<uint8_t>* focusModelMask = nullptr;
    bool predictionEnabled = false;
    bool liveAdvanceHeld = false;
    bool focusFadeActive = false;
};

struct ReplayHudStatus
{
    SkullbonezCore::Core::MainMemoryReplayStats memoryStats;
    int memoryPreset = 0;
    int requestedRetentionSeconds = 0;
    int requestedBudgetMiB = 0;
    int presentationRetentionSeconds = 0;
    int solverRetentionSeconds = 0;
    float divergenceUnits = 0.0f;
    bool memoryBudgetClamped = false;
    bool solverWindowReduced = false;
    bool divergenceValid = false;
    bool memoryStatsValid = false;
};
} // namespace SkullbonezCore::Runtime
