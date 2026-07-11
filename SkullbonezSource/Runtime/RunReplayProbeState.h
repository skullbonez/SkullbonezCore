/*
File: SkullbonezSource/Runtime/RunReplayProbeState.h
Purpose:
  Groups debug-only CLI replay probe state owned by ReplayRuntime.

Mental model:
  These probes are launch-requested diagnostics that drive replay scrub,
  restore, and save coverage after the scene has enough captured samples. They
  share ReplayRuntime's lifecycle so configuration, one-shot completion, and
  bounded failure reporting stay beside the workflows they control.

Glossary:
  Scrub probe: Debug launch path that seeks into captured replay history.
  Restore probe: Debug launch path that restores a historical replay sample
  into the live scene.
  Save probe: Debug launch path that writes a replay artifact after enough
  timeline coverage exists.
  Probe failure: CLI-visible diagnostic result that should make validation
  return nonzero without routing through the fatal-exception path.

Invariants:
  - Probe state exists only in debug builds and is driven by CLI test paths.
  - Completion flags are one-shot guards so a successful probe does not repeat
    every frame after its minimum sample count is reached.
  - Failure text is bounded and stored here so WinMain can return a nonzero
    probe result after the frame loop exits.

Related:
  - SkullbonezSource/Runtime/Replay/ReplayRuntime.h
  - SkullbonezSource/Runtime/Replay/RunReplayProbes.cpp
*/
#pragma once

#include "../Core/SbResult.h"

#include <cstring>

namespace SkullbonezCore
{
namespace Basics
{
#ifdef _DEBUG
struct RunReplayScrubProbeState
{
    bool enabled = false;
    bool completed = false;
    float normalized = 0.25f;
    int minSampleCount = 24;
    float minDistanceSquared = 0.0001f;
};

struct RunReplayRestoreProbeState
{
    bool enabled = false;
    bool completed = false;
    float normalized = 0.25f;
    int minSampleCount = 24;
};

struct RunReplaySaveProbeState
{
    bool enabled = false;
    bool completed = false;
    bool runtimeResetCoverageInjected = false;
    bool eventCoverageInjected = false;
    int minSampleCount = 24;
    char path[260] = {};
};

struct RunReplayProbeFailureState
{
    bool failed = false;
    char owner[64] = {};
    char message[512] = {};
};

struct RunReplayProbeState
{
    void RecordFailure( const SbResult& result )
    {
        if ( result.ok || failure.failed )
        {
            return;
        }

        const char* failureOwner =
            result.error.owner && result.error.owner[0] != '\0' ? result.error.owner : "ReplayProbe";
        const char* failureMessage =
            result.error.message[0] != '\0' ? result.error.message : "replay probe failed without a failure message";
        failure.failed = true;
        strcpy_s( failure.owner, sizeof( failure.owner ), failureOwner );
        strcpy_s( failure.message, sizeof( failure.message ), failureMessage );
    }

    bool Failed() const
    {
        return failure.failed;
    }

    const char* FailureOwner() const
    {
        return failure.owner[0] != '\0' ? failure.owner : "ReplayProbe";
    }

    const char* FailureMessage() const
    {
        return failure.message[0] != '\0' ? failure.message : "replay probe failed";
    }

    RunReplayScrubProbeState scrub;
    RunReplayRestoreProbeState restore;
    RunReplaySaveProbeState save;
    RunReplayProbeFailureState failure;
};
#endif
} // namespace Basics
} // namespace SkullbonezCore
