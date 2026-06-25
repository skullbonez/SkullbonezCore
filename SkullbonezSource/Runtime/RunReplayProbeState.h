/*
File: SkullbonezSource/Runtime/RunReplayProbeState.h
Purpose:
  Groups debug-only CLI replay probe state used by Run self-tests.

Mental model:
  These probes are launch-requested diagnostics that drive replay scrub,
  restore, and save coverage after the scene has enough captured samples. They
  are not part of the normal replay owner state in ReplayRuntime.

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
#endif
} // namespace Basics
} // namespace SkullbonezCore
