/*
File: SkullbonezSource/Runtime/RunCameraState.h
Purpose:
  Owns Run's operator camera mode, input memory, and camera automation state.

Mental model:
  Run still stores the camera shelf, but camera/input/director helpers consume
  this named aggregate instead of reaching through the shared RunState staging
  header. The shelf records operator intent and presentation timers; camera pose
  authority remains inside CameraCollection.

Glossary:
  Operator camera mode: Current user-facing workspace such as Demo, Inspect,
    Attach, Launcher, Manipulator, or Director.
  Mouse-look memory: Per-frame raw-input state used to discard stale deltas when
    focus, UI ownership, or camera mode changes.
  Auto-cycle screenshot: Validation/authoring helper that advances tracked
    models and captures one screenshot per target at a fixed interval.
  Director playback: Presentation-only shot-list state that times authored
    camera/style phases without mutating deterministic physics.

Invariants:
  - This shelf stores camera intent and helper timers, not authoritative camera
    pose; pose writes still go through CameraCollection.
  - `input` is frame-local memory captured by InputController and must not be
    used as a long-lived hardware snapshot.

Related:
  - SkullbonezSource/Runtime/Run.h
  - SkullbonezSource/Runtime/InputController.h
  - SkullbonezSource/Runtime/RunDemoDirector.h
  - Agentic/Plans/TODO/runtime-shell-decomposition.md
*/
#pragma once

#include "../Core/Common.h"
#include "DemoDirector.h"
#include "Input.h"
#include "RuntimeCameraMode.h"

namespace SkullbonezCore
{
namespace Environment
{
class CameraCollection;
}
namespace GameObjects
{
class GameModelCollection;
}
namespace Geometry
{
class Terrain;
}
namespace Basics
{
class AttachedCameraController;
class EngineConfig;
struct RunTimerState;
struct RunCameraState
{
    Hardware::InputState input = {};                           // Snapshot consumed by camera controls for this frame.

    int selectedCamera = 0;                                    // Keeps track of which camera is selected
    RunCameraMode mode = RunCameraMode::Demo;                  // Explicit operator camera mode shown in the minimized HUD.
    RunCameraMode modeBeforeLauncher = RunCameraMode::Inspect; // N returns to the last non-launcher workspace.
    DemoDirectorPlaybackState director;                        // Fixed shot-list playback state for Director camera mode.
    bool needsMouseLookReset = true;                           // Discard stale absolute mouse deltas after UI/focus/fly transitions
    bool hasMouseLookLastClient = false;
    POINT mouseLookLastClient = {};
    bool mouseLookOwnsCursor = false;                          // Resolved post-UI pointer policy captured with this frame's camera input.
    float travelSpeedMultiplier = 1.0f;                        // Captured Shift modifier; late camera update never reopens device state.
    float cameraTime = 0.0f;                                   // Camera helper clock
    int trackBallIndex = -1;                                   // Index of ball to track with camera (-1 = no tracking)
    float trackHeight = 300.0f;                                // Camera height above tracked ball
    float autoCycleInterval = -1.0f;                           // Seconds between per-ball auto screenshots (-1 = disabled)
    float autoCycleAccum = 0.0f;                               // Accumulated real-time seconds since last shot
    int autoCycleShotsTaken = 0;                               // Number of per-ball screenshots taken so far

    void StopAutoCycle()
    {
        autoCycleInterval = -1.0f;
        autoCycleAccum = 0.0f;
    }

    void UpdateViewingOrientation( RunTimerState& timers,
                                   Environment::CameraCollection& cameras,
                                   const GameObjects::GameModelCollection& models,
                                   bool replayCameraActive,
                                   bool sceneMode,
                                   bool attachedActiveFollow,
                                   bool cameraLookCaptured );
    void AdvanceAutoCycleClock( bool sceneMode, float simulationDt );
    void TickControls( Environment::CameraCollection& cameras,
                       Geometry::Terrain& terrain,
                       GameObjects::GameModelCollection& models,
                       AttachedCameraController& attachedCamera,
                       const EngineConfig& config,
                       bool editorModeEnabled,
                       bool viewportLookActive,
                       bool sceneMode,
                       float cameraDt );
};

} // namespace Basics
} // namespace SkullbonezCore
