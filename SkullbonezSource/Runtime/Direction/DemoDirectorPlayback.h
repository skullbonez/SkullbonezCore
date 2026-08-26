/*
File: SkullbonezSource/Runtime/Direction/DemoDirectorPlayback.h
Purpose:
  Declares presentation-only Demo Director playback/style/pacing helpers for Run split files.

Summary:
  Direction retains shot-list policy and playback state. App supplies the
  current camera pose and applies the bounded pose, style, and reveal commands
  returned by each synchronous operation.

Glossary:
  Shot-list phase: One authored camera/style/advance record from `.shot.json`.
  Reveal pacing: Presentation-only replay overlay speed authored per phase.
  Camera command: Detached pose value App applies to CameraCollection.

Invariants:
  - Helpers must stay presentation-only and must not mutate physics state.
  - Direction stores no Camera, Scene, Capture, or App owner pointer.
  - App applies style commands through SceneController after parsing succeeds.
  - Reveal pacing writes stay on replay presentation state and do not rebuild
    prediction physics samples.

Related:
  - SkullbonezSource/Runtime/Direction/DemoDirectorPlayback.cpp
  - SkullbonezSource/Runtime/Camera/CameraControlState.h
  - Agentic/Reference/engine-glossary.md
*/
#pragma once

#include "DemoDirectorPersistence.h"

namespace SkullbonezCore
{
namespace Runtime
{
// Concept: Director consumes a value-only reveal sample and returns a command.
// It never borrows the prediction owner or its mutable reveal clock.
struct DemoDirectorPredictionView
{
    float revealProgress = 0.0f;
    bool revealAvailable = false;
};

struct DemoDirectorTickResult
{
    DemoCameraPose cameraPose;
    char stylePath[DemoPhase::STYLE_PATH_BYTES] = {};
    float requestedRevealRate = 1.0f;
    bool applyCameraPose = false;
    bool applyStyle = false;
    bool applyRevealRate = false;
};

struct DemoDirectorCameraCommand
{
    DemoCameraPose pose;
    bool applyPose = false;
};

namespace DemoDirectorPlayback
{
// Retains the complete cold save target or leaves the prior path unchanged.
bool TryRetainShotListPath( DemoDirectorPlaybackState& director, const char* path ) noexcept;
bool LoadShotList( DemoDirectorPlaybackState& director, const DemoCameraPose& currentPose, const char* path );
bool AdvancePhase( DemoDirectorPlaybackState& director, const DemoCameraPose& currentPose );
void EnterMode( DemoDirectorPlaybackState& director, const DemoCameraPose& currentPose );
bool BeginGrab( DemoDirectorPlaybackState& director, bool directorModeActive, const DemoCameraPose& currentPose,
                DemoDirectorCameraCommand& outCommand );
bool EndGrab( DemoDirectorPlaybackState& director, bool directorModeActive, const DemoCameraPose& currentPose );
bool SetCurrentPhasePose( DemoDirectorPlaybackState& director, const DemoCameraPose& currentPose );
bool SetCurrentPhaseStyle( DemoDirectorPlaybackState& director, const char* stylePath );
bool SelectNextPhaseForAuthoring( DemoDirectorPlaybackState& director, const DemoCameraPose& currentPose );
bool SaveShotList( const DemoDirectorPlaybackState& director );
DemoDirectorTickResult Tick( DemoDirectorPlaybackState& director, bool directorModeActive,
                             DemoDirectorPredictionView prediction, const DemoCameraPose& currentPose, float cameraDt );
void CompleteStyleApplication( DemoDirectorPlaybackState& director, bool succeeded, const char* errorMessage );
} // namespace DemoDirectorPlayback
} // namespace Runtime
} // namespace SkullbonezCore
