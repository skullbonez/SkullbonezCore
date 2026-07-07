/*
File: SkullbonezSource/Runtime/RunReplayProbeState.h
Purpose:
  Groups debug-only CLI replay probe state used by Run self-tests.

Mental model:
  These probes are launch-requested diagnostics that drive replay scrub,
  restore, and save coverage after the scene has enough captured samples. They
  are not part of the normal replay owner state in ReplayRuntime.

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
  - SkullbonezSource/Runtime/Run.h
  - SkullbonezSource/Runtime/Run.cpp
  - SkullbonezSource/Runtime/Replay/ReplayRuntime.h
*/
#pragma once

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
    RunReplayScrubProbeState scrub;
    RunReplayRestoreProbeState restore;
    RunReplaySaveProbeState save;
    RunReplayProbeFailureState failure;
};
#endif
} // namespace Basics
} // namespace SkullbonezCore
