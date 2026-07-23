/*
File: SkullbonezSource/Runtime/DemoDirectorPlayback.h
Purpose:
  Declares presentation-only Demo Director playback/style/pacing helpers for Run split files.

Summary:
  SceneWorld owns cameras while Run owns style/playback state, but Director
  playback is a narrow helper module. The frame tick receives one scene-style
  context and derives its camera subowner locally so the two cannot disagree.

Glossary:
  Director playback: Runtime camera mode that applies authored shot-list poses
    plus optional phase styles and prediction reveal pacing.
  Shot-list phase: One authored camera/style/advance record from `.shot.json`.
  Reveal pacing: Presentation-only replay overlay speed authored per phase.
  Camera owner: The scene-owned CameraCollection mutated by Director playback.

Invariants:
  - Helpers must stay presentation-only and must not mutate physics state.
  - Per-frame camera writes derive CameraCollection from the same SceneWorld
    used for style writes, so callers cannot pair mismatched scene owners.
  - Style writes go through SceneRuntimeStyle so object material/cinematic
    changes remain in the existing scene-style owner.
  - Reveal pacing writes stay on replay presentation state and do not rebuild
    prediction physics samples.

Related:
  - SkullbonezSource/Runtime/DemoDirectorPlayback.cpp
  - SkullbonezSource/Runtime/CameraControlState.h
  - SkullbonezSource/Runtime/CameraControlState.h
*/
#pragma once

#include "CameraControlState.h"
#include "CameraCollection.h"

namespace SkullbonezCore
{
namespace Runtime
{
struct SceneRuntimeStyleContext;

// Concept: Director consumes a value-only reveal sample and returns a command.
// It never borrows the prediction owner or its mutable reveal clock.
struct DemoDirectorPredictionView
{
    float revealProgress = 0.0f;
    bool revealAvailable = false;
};

struct DemoDirectorTickResult
{
    float requestedRevealRate = 1.0f;
    bool applyRevealRate = false;
};

namespace DemoDirectorPlayback
{
bool LoadShotList( CameraControlState& camera, Environment::CameraCollection& cameras, const char* path );
bool AdvancePhase( CameraControlState& camera, Environment::CameraCollection& cameras );
void EnterMode( CameraControlState& camera, Environment::CameraCollection& cameras );
bool BeginGrab( CameraControlState& camera, Environment::CameraCollection& cameras );
bool EndGrab( CameraControlState& camera, Environment::CameraCollection& cameras );
bool SetCurrentPhasePose( CameraControlState& camera, Environment::CameraCollection& cameras );
bool SetCurrentPhaseStyle( CameraControlState& camera, const char* stylePath );
bool SelectNextPhaseForAuthoring( CameraControlState& camera, Environment::CameraCollection& cameras );
bool SaveShotList( const CameraControlState& camera );
DemoDirectorTickResult Tick( CameraControlState& camera,
                             DemoDirectorPredictionView prediction,
                             SceneRuntimeStyleContext styleContext,
                             float cameraDt );
} // namespace DemoDirectorPlayback
} // namespace Runtime
} // namespace SkullbonezCore
