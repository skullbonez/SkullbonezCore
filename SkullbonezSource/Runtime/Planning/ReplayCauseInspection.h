/*
File: ReplayCauseInspection.h
Purpose:
  Defines the Planning-owned exact-frame eligibility result for causal-row transport.

Summary:
  Cause rows address either the retained solver ring or the active prediction
  bank. This value contract keeps the chosen frame, source track, and refusal
  state together so later transport cannot silently clamp to a nearby frame.

Glossary:
  Seek source: Timeline bank that must contain the row's exact frame before
    transport is enabled.

Invariants:
  - Available results identify one exact frame in the selected source bank.
  - Missing frames refuse transport with `Replay frame expired`.
  - Solver-detail availability is independent of frame transport eligibility.

Related:
  - SkullbonezSource/Runtime/Replay/ReplayAuthoringPackets.h
  - SkullbonezSource/Runtime/Replay/ReplayCapturePackets.h
*/
#pragma once

#include "../Replay/ReplayAuthoringPackets.h"
#include "../Replay/ReplayCapturePackets.h"

#include <span>

namespace SkullbonezCore::Runtime
{
struct RunReplayPredictionFrame;

enum class ReplayCauseSeekSource
{
    SolverHistory,
    Prediction
};

enum class ReplayCauseSeekAvailability
{
    Available,
    ReplayFrameExpired
};

struct ReplayCauseSeekResult
{
    // Invariant: frame and source always describe the requested row even when
    // availability refuses transport, so diagnostics never report a clamped substitute.
    ReplayFrameIndex frame = 0;
    ReplayCauseSeekSource source = ReplayCauseSeekSource::SolverHistory;
    ReplayCauseSeekAvailability availability = ReplayCauseSeekAvailability::ReplayFrameExpired;

    bool CanTransport() const noexcept;
    const char* Feedback() const noexcept;
};

ReplayCauseSeekResult EvaluateReplayCauseSeek( const RunReplayCauseTreeRow& row, const ReplayRecorderStats& solverStats,
                                               std::span<const RunReplayPredictionFrame> predictionFrames ) noexcept;
} // namespace SkullbonezCore::Runtime
