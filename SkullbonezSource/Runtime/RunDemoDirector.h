/*
File: SkullbonezSource/Runtime/RunDemoDirector.h
Purpose:
  Declares presentation-only Demo Director playback/style/pacing helpers for Run split files.

Mental model:
  Run owns the camera/style state and subsystem pointers, but Director playback
  is a narrow helper module. Callers pass the shelves it needs explicitly so
  this feature does not grow the Run class method surface.

Glossary:
  Director playback: Runtime camera mode that applies authored shot-list poses
    plus optional phase styles and prediction reveal pacing.
  Shot-list phase: One authored camera/style/advance record from `.shot.json`.
  Reveal pacing: Presentation-only replay overlay speed authored per phase.
  Run shelf: A Run-owned aggregate such as RunCameraState or RunSubsystemState.

Invariants:
  - Helpers must stay presentation-only and must not mutate physics state.
  - Camera writes go through RunSubsystemState::cameras so the existing camera
    owner remains authoritative.
  - Style writes go through SceneRuntimeStyle so object material/cinematic
    changes remain in the existing scene-style owner.
  - Reveal pacing writes stay on replay presentation state and do not rebuild
    prediction physics samples.

Related:
  - SkullbonezSource/Runtime/RunDemoDirector.cpp
  - SkullbonezSource/Runtime/RunCameraState.h
  - SkullbonezSource/Runtime/RunSubsystemState.h
*/
#pragma once

#include "RunCameraState.h"
#include "RunSubsystemState.h"

namespace SkullbonezCore
{
namespace Basics
{
struct SceneRuntimeStyleContext;
struct RunReplayPredictionState;

namespace DemoDirectorPlayback
{
bool LoadShotList( RunCameraState& camera, const RunSubsystemState& systems, const char* path );
bool AdvancePhase( RunCameraState& camera, const RunSubsystemState& systems );
void EnterMode( RunCameraState& camera, const RunSubsystemState& systems );
bool BeginGrab( RunCameraState& camera, const RunSubsystemState& systems );
bool EndGrab( RunCameraState& camera, const RunSubsystemState& systems );
bool SetCurrentPhasePose( RunCameraState& camera, const RunSubsystemState& systems );
bool SetCurrentPhaseStyle( RunCameraState& camera, const char* stylePath );
bool SelectNextPhaseForAuthoring( RunCameraState& camera, const RunSubsystemState& systems );
bool SaveShotList( const RunCameraState& camera );
void Tick( RunCameraState& camera,
           const RunSubsystemState& systems,
           RunReplayPredictionState& prediction,
           SceneRuntimeStyleContext styleContext,
           float cameraDt );
} // namespace DemoDirectorPlayback
} // namespace Basics
} // namespace SkullbonezCore
