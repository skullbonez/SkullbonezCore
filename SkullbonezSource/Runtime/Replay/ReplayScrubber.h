/*
File: SkullbonezSource/Runtime/Replay/ReplayScrubber.h
Purpose:
  Defines scrub cursor and live-restore command values.

Summary:
  ReplayScrubber coordinates past-track inspection and transactional restore; M2 moves definitions only.

Glossary:
  Live edge: The newest retained replay sample.

Invariants:
  - Restore sample pointers are borrowed for the applying frame only.
  - M2 preserves the moved definition bodies verbatim.

Related:
  - ReplayRuntime.h
  - ReplayRecorder.h
*/
#pragma once

#include "ReplayRecorder.h"

#include <cstddef>
#include <cstdint>

namespace SkullbonezCore
{
namespace Runtime
{
enum class RunReplayTrack
{
    Presentation,
    Solver
};

struct RunReplayScrubberState
{
    bool visible = false;
    bool historicalSamplePaused = false;
    bool liveAdvanceHeld = false;
    bool pauseRestoreFlyMode = false;
    bool pauseRestoreLauncherMode = false;
    bool restoreWasDown = false;
    bool restoreConsumedThisFrame = false;
    RunReplayTrack activeTrack = RunReplayTrack::Solver;
    RunReplayTrack saveMessageTrack = RunReplayTrack::Solver;
    float position = 1.0f;                                 // 0 = oldest retained sample, 1 = live edge.
    float presentationPosition = 1.0f;
    float solverPosition = 1.0f;
    int mouseX = 0;
    int mouseY = 0;
    double visibleUntil = 0.0;
    double fadeUpdatedAt = 0.0;                            // Last scrubber opacity update in runtime seconds.
    float visibleAlpha = 0.0f;                             // 0 = hidden, 1 = fully faded in.
    double saveMessageUntil = 0.0;
    char saveMessage[96] = {};
};

struct RunReplayV2TargetRestoreResult
{
    std::size_t checkpointCount = 0;
    std::size_t eventCount = 0;
    std::size_t hashCount = 0;
    std::size_t eventsApplied = 0;
    std::size_t bodyCount = 0;
    std::size_t fileBytes = 0;
    ReplayFrameIndex checkpointFrame = 0;
    ReplayFrameIndex targetFrame = 0;
    uint32_t eventCursor = 0;
    uint32_t branchId = 0;
    uint32_t parentBranchId = 0;
    uint64_t solverHash = 0;
    uint64_t presentationHash = 0;
    bool generatedTopologyRebuilt = false;
    bool madeLiveBranch = false;
    bool enterInteractiveRequested = false;
};

enum class ReplayLiveRestoreKind : uint8_t
{
    None,
    V2ArtifactTarget,
    SolverSample
};

struct ReplayLiveRestoreRequest
{
    ReplayLiveRestoreKind kind = ReplayLiveRestoreKind::None;
    const ReplaySolverFrameSample* solverSample = nullptr; // Borrowed until the workspace command is applied this frame.
    ReplayFrameIndex requestedFrame = 0;
    bool makeLiveBranch = false;
    bool enterInteractive = false;
    RunReplayTrack messageTrack = RunReplayTrack::Solver;
    double now = 0.0;
    char path[260] = {};
};

} // namespace Runtime
} // namespace SkullbonezCore
