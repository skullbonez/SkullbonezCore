/*
File: SkullbonezSource/Runtime/RunDemoDirector.h
Purpose:
  Declares presentation-only Demo Director playback helpers for Run split files.

Mental model:
  Run owns the camera state and subsystem pointers, but Director playback is a
  narrow helper module. Callers pass the shelves it needs explicitly so this
  feature does not grow the Run class method surface.

Glossary:
  Director playback: Runtime camera mode that applies authored shot-list poses.
  Shot-list phase: One authored camera/style/advance record from `.shot.json`.
  Run shelf: A Run-owned aggregate such as RunCameraState or RunSubsystemState.

Invariants:
  - Helpers must stay presentation-only and must not mutate physics or scene
    object state.
  - Camera writes go through RunSubsystemState::cameras so the existing camera
    owner remains authoritative.

Related:
  - SkullbonezSource/Runtime/RunDemoDirector.cpp
  - SkullbonezSource/Runtime/RunState.h
  - fable_plans/08-demo-director-progress.md
*/
#pragma once

#include "RunState.h"

namespace SkullbonezCore
{
namespace Basics
{
namespace DemoDirectorPlayback
{
bool LoadShotList( RunCameraState& camera, const RunSubsystemState& systems, const char* path );
bool AdvancePhase( RunCameraState& camera, const RunSubsystemState& systems );
void EnterMode( RunCameraState& camera, const RunSubsystemState& systems );
void Tick( RunCameraState& camera, const RunSubsystemState& systems, float cameraDt );
} // namespace DemoDirectorPlayback
} // namespace Basics
} // namespace SkullbonezCore
