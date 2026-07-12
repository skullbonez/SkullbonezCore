/*
File: SkullbonezSource/Runtime/RunDemoDirector.h
Purpose:
  Declares presentation-only Demo Director playback/style/pacing helpers for Run split files.

Summary:
  SceneController owns the cameras while Run owns style/playback state, but Director playback
  is a narrow helper module. Callers pass the shelves it needs explicitly so
  this feature does not grow the Run class method surface.

Glossary:
  Director playback: Runtime camera mode that applies authored shot-list poses
    plus optional phase styles and prediction reveal pacing.
  Shot-list phase: One authored camera/style/advance record from `.shot.json`.
  Reveal pacing: Presentation-only replay overlay speed authored per phase.
  Camera owner: The scene-owned CameraCollection mutated by Director playback.

Invariants:
  - Helpers must stay presentation-only and must not mutate physics state.
  - Camera writes go through the borrowed CameraCollection so the scene camera
    owner remains authoritative.
  - Style writes go through SceneRuntimeStyle so object material/cinematic
    changes remain in the existing scene-style owner.
  - Reveal pacing writes stay on replay presentation state and do not rebuild
    prediction physics samples.

Related:
  - SkullbonezSource/Runtime/RunDemoDirector.cpp
  - SkullbonezSource/Runtime/RunCameraState.h
  - SkullbonezSource/Runtime/RunCameraState.h
*/
#pragma once

#include "RunCameraState.h"
#include "CameraCollection.h"

namespace SkullbonezCore
{
namespace Basics
{
struct SceneRuntimeStyleContext;
struct RunReplayPredictionState;

namespace DemoDirectorPlayback
{
bool LoadShotList( RunCameraState& camera, Environment::CameraCollection& cameras, const char* path );
bool AdvancePhase( RunCameraState& camera, Environment::CameraCollection& cameras );
void EnterMode( RunCameraState& camera, Environment::CameraCollection& cameras );
bool BeginGrab( RunCameraState& camera, Environment::CameraCollection& cameras );
bool EndGrab( RunCameraState& camera, Environment::CameraCollection& cameras );
bool SetCurrentPhasePose( RunCameraState& camera, Environment::CameraCollection& cameras );
bool SetCurrentPhaseStyle( RunCameraState& camera, const char* stylePath );
bool SelectNextPhaseForAuthoring( RunCameraState& camera, Environment::CameraCollection& cameras );
bool SaveShotList( const RunCameraState& camera );
void Tick( RunCameraState& camera,
           Environment::CameraCollection& cameras,
           RunReplayPredictionState& prediction,
           SceneRuntimeStyleContext styleContext,
           float cameraDt );
} // namespace DemoDirectorPlayback
} // namespace Basics
} // namespace SkullbonezCore
