/*
File: SkullbonezSource/Runtime/RunState.h
Purpose:
  Collects Run-owned state aggregates that are shared by split runtime files.

Mental model:
  Run remains the composition root, but its state shelves are named here so the
  public Run facade is no longer a catalogue of every timer, toggle, and render
  resource aggregate. Narrower owners should move state out of this file over
  time; this header is a staging boundary, not a destination for new features.

Related:
  - SkullbonezSource/Runtime/Run.h
  - SkullbonezSource/Runtime/RunInternal.h
  - Agentic/Plans/runtime-run-decomposition-plan.md
*/
#pragma once

#include "../Assets/AssetSystem.h"
#include "../Core/Common.h"
#include "../Core/Config.h"
#include "../Core/Timer.h"
#include "../Physics/Debug/PhysicsDebugVisualizer.h"
#include "../Physics/TornadoField.h"
#include "Input.h"
#include "Render/RuntimeRenderResources.h"
#include "RuntimeCameraMode.h"
#include "Scene/SceneGeneratedSetup.h"

#include <memory>
#include <string>
#include <vector>

namespace SkullbonezCore
{
namespace Environment
{
class CameraCollection;
}
namespace Geometry
{
class SkyBox;
class Terrain;
} // namespace Geometry
namespace Textures
{
class TextureCollection;
}

namespace Basics
{
class Window;

struct TornadoVisualSettings
{
    bool enabled = true;                                       // Render-only sparse funnel shell; physics force state remains separate.
    bool autoEnableWithTornado = true;                         // UI/CLI tornado toggles keep the production visual paired by default.
    float shellAlpha = 0.14f;
    float dustAlpha = 0.20f;
    float ribbonWidth = 5.5f;
    int ribbonCount = 7;
    int ribbonSegments = 48;
    int particleCount = 96;
    float rotationSpeed = 1.25f;
};

struct RunRuntimeSettings
{
    bool isVsyncEnabled = true;                                // Swap-chain sync interval (true = vsync)
    bool isPipelineSyncEnabled = false;                        // Force CPU/GPU sync via Finish() before render
    bool isPhysicsSleepEnabled =
        true;                                                  // Live Catto sleep policy; false keeps bodies awake while leaving collision/solving active
    Physics::TornadoFieldConfig tornadoField;                  // Live vortex force/debug vector field controlled by CLI/UI
    Physics::TornadoSystemConfig tornadoSystem;                // Scene-authored multi-vortex schedule and motion.
    TornadoVisualSettings tornadoVisual;                       // Render-only tornado art tuning outside deterministic physics state.
};

struct RunTimerState
{
    Environment::Timer frameTimer;
    Environment::Timer workTimer;
    Environment::Timer updateTimer;
    Environment::Timer cameraTimer;
    Environment::Timer simulationTimer;

    float physicsTime = 0.0f;                                  // Last frame physics time (seconds)
    float rollingPhysicsTime = 0.0f;                           // Smoothed physics time accumulator
    float renderTime = 0.0f;                                   // Last frame render time (seconds)
    float rollingRenderTime = 0.0f;                            // Smoothed render time accumulator
    float rollingFpsTime = 0.0f;                               // Smoothed FPS time accumulator
    float rollingSceneEnergy = 0.0f;                           // Half-second averaged kinetic energy
    float cpuFrameWorkMs = 0.0f;                               // Last frame CPU work before Present/VSync
    float gpuFrameWorkMs = 0.0f;                               // Last available GPU work before Present/VSync
    float timeSinceLastRender = 0.0f;
    double sceneEnergyAccumulator = 0.0;
    int sceneEnergySampleCount = 0;
    int lastUIDrawCalls = 0;                                   // Actual UI draw calls measured around Frame/UI last frame
};

struct RunSubsystemState
{
    Assets::AssetSystem assets;
    std::unique_ptr<Geometry::Terrain> terrain;
    bool isFlatSlopeTerrain = false;
    // Lifetime: all pass resources are released before backend teardown/rebuild
    // and lazily recreated by the ensure hooks that own their target size and
    // shader contracts.
    RunRenderPassResources renderPasses;

    Environment::CameraCollection* cameras = nullptr;
    Textures::TextureCollection* textures = nullptr;
    Window* window = nullptr;
    Geometry::SkyBox* skyBox = nullptr;
};

struct RunCameraState
{
    Hardware::InputState input = {};                           // Snapshot consumed by camera controls for this frame.

    int selectedCamera = 0;                                    // Keeps track of which camera is selected
    RunCameraMode mode = RunCameraMode::Demo;                  // Explicit operator camera mode shown in the minimized HUD.
    RunCameraMode modeBeforeLauncher = RunCameraMode::Inspect; // N returns to the last non-launcher workspace.
    bool needsMouseLookReset = true;                           // Discard stale absolute mouse deltas after UI/focus/fly transitions
    bool hasMouseLookLastClient = false;
    POINT mouseLookLastClient = {};
    float cameraTime = 0.0f;                                   // Camera helper clock
    int trackBallIndex = -1;                                   // Index of ball to track with camera (-1 = no tracking)
    float trackHeight = 300.0f;                                // Camera height above tracked ball
    float autoCycleInterval = -1.0f;                           // Seconds between per-ball auto screenshots (-1 = disabled)
    float autoCycleAccum = 0.0f;                               // Accumulated real-time seconds since last shot
    int autoCycleShotsTaken = 0;                               // Number of per-ball screenshots taken so far
};

enum class AttachedCameraSubmode
{
    FixedRelative,
    VelocityForward,
    RagdollEyes,
    Count
};

struct AttachedCameraTarget
{
    int modelIndex = -1;                                       // Fast live lookup; revalidated every frame before use.
    uint32_t replayBodyId = 0;                                 // Stable scene-local identity used to recover stale indices.
    char name[64] = {};                                        // Human/debug fallback when replay id cannot recover the target.
};

struct AttachedCameraState
{
    AttachedCameraTarget target;                               // Camera-owned target; replay/editor selections are only seeds.
    AttachedCameraSubmode submode = AttachedCameraSubmode::FixedRelative;
    bool activeFollow = true;                                  // false means pinned in world space with mouse released to UI.
    bool hasFixedOffset = false;
    bool hasLastLookDirection = false;
    Math::Vector::Vector3 localEyeOffset = Math::Vector::ZERO_VECTOR;
    Math::Vector::Vector3 localViewOffset = Math::Vector::Vector3( 0.0f, 0.0f, 1.0f );
    Math::Vector::Vector3 localUp = Math::Vector::Vector3( 0.0f, 1.0f, 0.0f );
    Math::Vector::Vector3 lastLookDirection = Math::Vector::Vector3( 0.0f, 0.0f, 1.0f );
};

struct RunLiveStyleControlState
{
    bool enabled = false;                                      // Polls a small control folder for live style JSON and screenshot requests
    char directory[260] = {};                                  // Folder containing live.style.json, capture.txt, and status.txt
    char stylePath[300] = {};                                  // Style descriptor applied without reloading the scene
    char capturePath[300] = {};                                // Text command file used to request one screenshot
    char statusPath[300] = {};                                 // Latest harness status for scripts/humans
    char pendingScreenshotPath[512] = {};                      // Screenshot path requested by capture.txt
    uint64_t styleStamp = 0;                                   // Last applied live.style.json write stamp
    uint64_t captureStamp = 0;                                 // Last consumed capture.txt write stamp
    int styleApplyCount = 0;                                   // Successful live style applications
    int captureCount = 0;                                      // Successful live screenshots
    bool hasPendingScreenshot = false;                         // Capture should run after render/UI this frame
};

enum class OverlayMode
{
    None,                                                      // Clean screen, nothing shown
    Timers,                                                    // Renderer name, model count, physics solver, profiler overlay
    SceneStats,                                                // Scene telemetry values used by deterministic tests
    BarsNormalized,                                            // Visual profiler bars, segments fill the bar width (relative)
    BarsAbsolute,                                              // Visual profiler bars, white = idle/vsync (absolute frame budget)
    Keys,                                                      // Keyboard reference panel
};

struct RunDebugState
{
    OverlayMode overlayMode =
        OverlayMode::None;                                     // HUD overlay cycle state (0 key advances through timers, scene stats, bars, and keys)
    bool isWaterFreezeDebug = false;                           // Freeze ocean animation at current shape (toggle with 1)
    bool isWaterNoReflect = false;                             // Disable ocean reflection entirely (2 cycles: FBO to DXR to none)
    bool isWaterRTReflect = false;                             // Use DXR ray-traced reflection (DXR only if supported)
    bool isWaterFlatDebug = false;                             // Force ocean mesh fully flat, no displacement (toggle with 3)
    bool isTerrainHidden = false;                              // Hide terrain mesh (toggle with 4)
    bool isWaterHidden = false;                                // Hide water mesh (toggle with 5)
    uint32_t physicsDebugFlags =
        Physics::PHYSICS_DEBUG_NONE;                           // Draw object axes, contact manifolds, and sleep state (cycle with C)
    bool isPhysicsDebugTransparent =
        false;                                                 // Draw translucent debug collision volumes behind physics debug lines (toggle with 6)
    float physicsDebugAlpha = 0.28f;                           // Translucent debug volume alpha
    float physicsDebugContactLinger = 0.45f;                   // Seconds to keep contact manifolds visible after their solver row disappears
    int physicsDebugPipelineStageCursor = 0;                   // F7/F8-selected Catto pipeline stage for PHYSICS_DEBUG_PIPELINE
    bool isCollisionVisualizer = false;                        // Render solid collision/sleep colours for balls and boxes (toggle with V)
    bool isTextOnly = false;                                   // Suppress all 3D rendering; show solid background with large pangram text
    bool isUITestPattern = false;                              // Bright 2D backdrop behind UI for visual blur tests
    bool isTopTextHidden = false;                              // Hide top-left HUD text while leaving other overlays active
    bool isBroadphaseOverlay = false;                          // Broadphase spatial grid visualizer overlay (toggle with G)
    float frozenWaterTime = 0.0f;                              // Simulation time captured when freeze was toggled on
#ifdef _DEBUG
    char reproSnapshotMessage[128] = {};                       // Short HUD confirmation after launcher-mode repro dump
    double reproSnapshotMessageUntil = 0.0;                    // Simulation timer value after which the HUD message expires
#endif
};

struct RunLaunchOptions
{
    float timeScaleOverride = 0.0f;                            // CLI --time-scale override applied after each scene load (0 = not set)
    bool fixedStep = false;                                    // CLI --fixed-step override applied after each scene load
    unsigned int seedOverride = 0;                             // CLI --seed override applied after each scene load (0 = not set)
    bool noWater = false;                                      // CLI --no-water starts fluid below terrain
    bool noSleep = false;                                      // Startup CLI --no-sleep request; live policy can still be toggled from the Physics tab
    bool hasTornadoOverride = false;
    bool tornadoEnabled = false;
    bool tornadoVectors = false;
    bool hasCinematicRenderingOverride = false;
    bool cinematicRendering = false;
    bool hasCinematicShadowsOverride = false;
    bool cinematicShadows = false;
    bool demoHeroStyle = false;                                // CLI --demohero applies the low-poly hero look to generated demo mode
    bool interactiveSceneRun = false;                          // CLI --interactive/--hold keeps scene automation from quitting the app
    int frameCountOverride = -1;                               // CLI --frames override applied after each scene load
    bool uiStress = false;                                     // CLI --ui-stress enables generated/demo stress without a scene file
    unsigned int uiStressSeed = 0;                             // CLI --ui-stress-seed
    int uiStressActions = 5;                                   // CLI --ui-stress-actions
    GeneratedObjectTypeOverride generatedObjectTypeOverride = GeneratedObjectTypeOverride::Mixed;
    bool hasPhysicsDebugFlagsOverride = false;
    uint32_t physicsDebugFlagsOverride = Physics::PHYSICS_DEBUG_NONE;
    bool hasPhysicsDebugTransparentOverride = false;
    bool physicsDebugTransparentOverride = false;
    bool hasPhysicsDebugAlphaOverride = false;
    float physicsDebugAlphaOverride = 0.28f;
    bool hasPhysicsDebugContactLingerOverride = false;
    float physicsDebugContactLingerOverride = 0.45f;
};

struct RunSceneBrowserState
{
    std::vector<std::string> paths;
    std::vector<std::string> names;
    std::vector<const char*> namePtrs;
    int selectedCineModeSceneIndex = -1;                       // -1=Demo/default look, otherwise scene-browser index of live cine/concept look
};

struct RunSceneUIOverrideState
{
    float timeScaleOverride = 0.0f;
    int modelCountOverride = -1;
    int solverBallCountOverride = -1;
    int solverBoxCountOverride = -1;
};

struct RunStartupState
{
    int gameModelCapacity = DEFAULT_GAME_MODEL_CAPACITY;
    int workerThreads = -1;
};

struct RunInputLatchState
{
    bool leftSceneCycleWasDown = false;
    bool rightSceneCycleWasDown = false;
    double lastEscapeTapTime = -1000.0;
};

struct RunReplayMismatchState
{
    uint32_t reports = 0;
    bool suppressed = false;
};
} // namespace Basics
} // namespace SkullbonezCore
