/*
File: SkullbonezSource/Runtime/Camera/CameraControlState.h
Purpose:
  Owns Run's operator camera mode, input memory, and camera automation state.

Summary:
  Run still stores the camera shelf, but camera/input/director helpers consume
  this named aggregate instead of reaching through the shared RunState staging
  header. The shelf records operator intent and presentation timers; camera pose
  authority remains inside CameraCollection. A cached config-derived mouse
  scale lets the earlier replay turn interpret the same raw deltas without
  reopening configuration ownership.

Glossary:
  Operator camera mode: Current user-facing workspace such as Demo, Inspect,
    Attach, Launcher, Manipulator, or Director.
  Mouse-look memory: Per-frame raw-input state used to discard stale deltas when
    focus, UI ownership, or camera mode changes.
  Auto-cycle screenshot: Validation/authoring helper that advances tracked
    models and captures one screenshot per target at a fixed interval.
  Detached scene camera: Transaction-owned copy populated during scene load and
    committed to this owner once the corresponding clear phase is observed.

Invariants:
  - This shelf stores camera intent and helper timers, not authoritative camera
    pose; pose writes still go through CameraCollection.
  - `input` contains only camera movement facts captured by InputController;
    Camera never retains the broad device snapshot.
  - App applies detached scene-camera state once per lifecycle generation.

Related:
  - SkullbonezSource/Runtime/App/Run.h
  - SkullbonezSource/Runtime/Input/InputController.h
  - SkullbonezSource/Runtime/Direction/DemoDirectorPlayback.h
  - Agentic/Reference/engine-glossary.md
*/
#pragma once

#include "../../Core/PlatformWin32.h"
#include "../../Core/Timer.h"
#include "../../Core/Common.h"
#include "DemoDirector.h"
#include "RuntimeCameraMode.h"
#include "../../Physics/PhysicsHandles.h"
#include "../../Assets/AssetKeys.h"

#include <array>

namespace SkullbonezCore
{
namespace Core
{
class EngineConfig;
class Profiler;
} // namespace Core
namespace Environment
{
class CameraCollection;
}
namespace Runtime
{
class SceneWorld;
}
namespace Geometry
{
class Terrain;
}
namespace Runtime
{
// Invariant: unattended cycling is a closed list of operator-visible slots.
// The causal detail slot is selected only by causal inspection.
inline constexpr std::array<uint32_t, 3> DEMO_CAMERA_CYCLE_SLOTS = { CAMERA_SCENE_OBJECT_1, CAMERA_SCENE_OBJECT_2,
                                                                     CAMERA_FREE };

class AttachedCameraController;

struct CameraControlState
{
    long inputXMove = 0;                                      // Mouse-look delta sampled for this camera frame.
    long inputYMove = 0;
    bool inputMoveForward = false;                             // Movement levels sampled for this camera frame.
    bool inputMoveBackward = false;
    bool inputMoveLeft = false;
    bool inputMoveRight = false;

    int selectedCamera = 0;                                    // Keeps track of which camera is selected
    RunCameraMode mode = RunCameraMode::Demo;                  // Explicit operator camera mode shown in the minimized HUD.
    RunCameraMode modeBeforeLauncher = RunCameraMode::Inspect; // N returns to the last non-launcher workspace.
    DemoDirectorPlaybackState director;                        // Fixed shot-list playback state for Director camera mode.
    bool needsMouseLookReset = true;                           // Discard stale absolute mouse deltas after UI/focus/fly transitions
    bool hasMouseLookLastClient = false;
    POINT mouseLookLastClient = {};
    bool mouseLookOwnsCursor = false;                          // Resolved post-UI pointer policy captured with this frame's camera input.
    float travelSpeedMultiplier = 1.0f;                        // Captured Shift modifier; late camera update never reopens device state.
    float mouseRadiansPerPixel = ( 1.0f / 60.0f ) * 0.2f;      // Last applied config sample reused by the earlier replay input turn.
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
        inputXMove = 0;
        inputYMove = 0;
        inputMoveForward = false;
        inputMoveBackward = false;
        inputMoveLeft = false;
        inputMoveRight = false;
        selectedCamera = 0;
        cameraTime = 0.0f;
    }

    // Lifetime: each camera tick borrows SceneWorld once and derives Cameras and
    // Terrain locally, keeping subowner identity inside this cohesive boundary.
    Core::SbResult InitialiseTiming( Core::SbDiagnosticStore& diagnostics )
    {
        return m_cameraTimer.Initialise( diagnostics );
    }
    void UpdateViewingOrientation( Runtime::SceneWorld& world, bool replayCameraActive, bool sceneMode,
                                   bool attachedActiveFollow, bool cameraLookCaptured, float presentationAlpha,
                                   Core::Profiler* profiler );
    void AdvanceAutoCycleClock( bool sceneMode, float simulationDt );
    void TickControls( Runtime::SceneWorld& world, AttachedCameraController& attachedCamera,
                       const SkullbonezCore::Core::EngineConfig& config, bool editorModeEnabled, bool viewportLookActive,
                       bool sceneMode, float cameraDt, float presentationAlpha );

  private:
    Environment::Timer m_cameraTimer;
};

} // namespace Runtime
} // namespace SkullbonezCore
