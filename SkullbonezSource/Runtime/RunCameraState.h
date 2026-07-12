/*
File: SkullbonezSource/Runtime/RunCameraState.h
Purpose:
  Owns Run's operator camera mode, input memory, and camera automation state.

Summary:
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
  - Agentic/Reports/2026-07-11/runtime-shell-final-ownership-review.md
*/
#pragma once

#include "../Core/Common.h"
#include "DemoDirector.h"
#include "Input.h"
#include "RuntimeCameraMode.h"
#include "../Physics/PhysicsHandles.h"

namespace SkullbonezCore
{
namespace Environment
{
class CameraCollection;
}
namespace Basics
{
class SceneController;
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
    Physics::ModelRowHint trackBallRow;                        // Cache for camera tracking; never object identity.
    float trackHeight = 300.0f;                                // Camera height above tracked ball
    float autoCycleInterval = -1.0f;                           // Seconds between per-ball auto screenshots (-1 = disabled)
    float autoCycleAccum = 0.0f;                               // Accumulated real-time seconds since last shot
    int autoCycleShotsTaken = 0;                               // Number of per-ball screenshots taken so far

    void StopAutoCycle()
    {
        autoCycleInterval = -1.0f;
        autoCycleAccum = 0.0f;
    }

    void ResetForSceneLoad( bool authoredScene )
    {
        // Scene activation chooses only the initial workspace. Camera-local
        // tracking, automation, and frame input memory are reset here.
        mode = authoredScene ? RunCameraMode::Scene : RunCameraMode::Demo;
        trackBallRow.value = -1;
        trackHeight = 300.0f;
        autoCycleInterval = -1.0f;
        autoCycleAccum = 0.0f;
        autoCycleShotsTaken = 0;
        input = {};
        selectedCamera = 0;
        cameraTime = 0.0f;
    }

    void UpdateViewingOrientation( RunTimerState& timers,
                                   Environment::CameraCollection& cameras,
                                   const Basics::SceneController& models,
                                   bool replayCameraActive,
                                   bool sceneMode,
                                   bool attachedActiveFollow,
                                   bool cameraLookCaptured,
                                   float presentationAlpha );
    void AdvanceAutoCycleClock( bool sceneMode, float simulationDt );
    void TickControls( Environment::CameraCollection& cameras,
                       Geometry::Terrain& terrain,
                       Basics::SceneController& models,
                       AttachedCameraController& attachedCamera,
                       const EngineConfig& config,
                       bool editorModeEnabled,
                       bool viewportLookActive,
                       bool sceneMode,
                       float cameraDt,
                       float presentationAlpha );
};

} // namespace Basics
} // namespace SkullbonezCore
