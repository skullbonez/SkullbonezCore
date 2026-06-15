/*
File: SkullbonezSource/SkullbonezRun.h
Purpose:
  Coordinates the main game loop and high-level runtime lifecycle.

Mental model:
  Runtime code connects authored scene data, input, simulation, render
  backends, and validation-oriented launch modes. Follow who owns state and
  when that state changes.

Glossary:
  DX12 (DirectX 12): Production renderer API used for explicit GPU resource,
  descriptor, and command-list control.
  DX11 (DirectX 11): Legacy parity renderer used to compare output while the
  engine migrates to DX12.
  OpenGL: Legacy parity renderer used as a reference path for visual output.
  GL (OpenGL): Legacy parity renderer path.
  DXR (DirectX Raytracing): DX12 API used for hardware ray traversal and
  reflection dispatch.
  GPU (Graphics Processing Unit): Processor that executes rendering, compute,
  and raytracing commands asynchronously from the CPU.
  CPU (Central Processing Unit): Host processor running engine code and
  recording GPU commands.
  FBO (Framebuffer Object): OpenGL-style off-screen render target concept used
  by parity and reflection code.
  HUD (Heads-Up Display): On-screen diagnostics and control overlay.
  CLI (Command-Line Interface): Text arguments or scripts used to launch
  validation and tooling paths.

Related:
  - SkullbonezSource/SkullbonezRun.cpp
  - Agentic/Reference/runtime-reference.md
  - Agentic/Reference/comment-style-guide.md
*/
#pragma once


#include <array>
#include <memory>
#include <string>
#include <vector>
#include "SkullbonezCommon.h"
#include "SkullbonezAssetSystem.h"
#include "SkullbonezCameraCollection.h"
#include "SkullbonezTimer.h"
#include "SkullbonezInput.h"
#include "SkullbonezTextureCollection.h"
#include "SkullbonezWindow.h"
#include "SkullbonezText.h"
#include "SkullbonezTerrain.h"
#include "SkullbonezSkyBox.h"
#include "SkullbonezGeometricMath.h"
#include "SkullbonezGameModelCollection.h"
#include "SkullbonezWorldEnvironment.h"
#include "SkullbonezIFramebuffer.h"
#include "SkullbonezIShader.h"
#include "SkullbonezShadow.h"
#include "SkullbonezTestScene.h"
#include "SkullbonezBroadphaseVisualizer.h"
#include "SkullbonezCollisionVisualizer.h"
#include "SkullbonezPhysicsDebugVisualizer.h"
#include "UI/SkullbonezUI.h"


namespace SkullbonezCore
{
namespace Basics
{
namespace RunInternal
{
struct SceneRuntimeResetSnapshot;
}

struct RunPerfLogState
{
    bool isPerfTest = false;            // Performance logging mode
    bool perfHeaderWritten = false;     // CSV header written for current perf run
    char perfLogPath[256] = {};         // Output path for perf CSV (empty = none)
    FILE* perfLogFile = nullptr;        // Open handle for perf CSV
    bool isPerfLogFlushEnabled = false; // Flush perf CSV on each write (diagnostic mode)
    int perfLogFlushInterval = 0;       // Flush perf CSV every N writes (0 = flush on close only)
    int perfLogWritesSinceFlush = 0;    // Buffered perf-log write count since last flush
#ifdef _DEBUG
    char physicsRegressionLogOverride[256] = {};    // CLI --physics-regression-log path (empty = disabled)
    char physicsCollisionTimeLogOverride[256] = {}; // CLI --physics-collision-time-log path (empty = disabled)
#endif
};

#ifdef _DEBUG
struct RunPhysicsDiagnosticsState
{
    char path[256] = {};                       // CLI --physics-diag path (empty = disabled)
    char currentRunId[32] = {};                // Stable per-load id written into NDJSON rows
    bool isEnabled = false;                    // True when a diagnostics path was provided
    bool isRunActive = false;                  // True after a run row and before the matching end row
    bool fixedStepForcedByDiagnostics = false; // True when --physics-diag forced fixed-step mode
    int runSequence = 0;                       // Incremented on every scene/generated load
};
#endif


struct RunRuntimeSettings
{
    bool isVsyncEnabled = true;               // Swap-chain sync interval (true = vsync)
    bool isPipelineSyncEnabled = false;       // Force CPU/GPU sync via Finish() before render
    bool isPhysicsSleepEnabled = true;        // Live Catto sleep policy; false keeps bodies awake while leaving collision/solving active
    Physics::TornadoFieldConfig tornadoField; // Live vortex force/debug vector field controlled by CLI/UI
};

struct RunTimerState
{
    Environment::Timer frameTimer;
    Environment::Timer workTimer;
    Environment::Timer updateTimer;
    Environment::Timer cameraTimer;
    Environment::Timer simulationTimer;

    float physicsTime = 0.0f;        // Last frame physics time (seconds)
    float rollingPhysicsTime = 0.0f; // Smoothed physics time accumulator
    float renderTime = 0.0f;         // Last frame render time (seconds)
    float rollingRenderTime = 0.0f;  // Smoothed render time accumulator
    float rollingFpsTime = 0.0f;     // Smoothed FPS time accumulator
    float rollingSceneEnergy = 0.0f; // Half-second averaged kinetic energy
    float cpuFrameWorkMs = 0.0f;     // Last frame CPU work before Present/VSync
    float gpuFrameWorkMs = 0.0f;     // Last available GPU work before Present/VSync
    float timeSinceLastRender = 0.0f;
    double sceneEnergyAccumulator = 0.0;
    int sceneEnergySampleCount = 0;
    int lastUIDrawCalls = 0;               // Actual UI draw calls measured around Frame/UI last frame
    float physicsAccumulator = 0.0f;       // Accumulated seconds for variable-step solver substeps
    float fixedStepTickAccumulator = 0.0f; // Fractional fixed ticks owed by time_scale in fixed-step mode
};

struct RunSubsystemState
{
    Assets::AssetSystem assets;
    std::unique_ptr<Geometry::Terrain> terrain;
    bool isFlatSlopeTerrain = false;
    std::unique_ptr<Rendering::IFramebuffer> reflectionFBO;

    // Cinematic render targets and post-process shaders. The sceneFBO holds the
    // full HDR world image, volumetricLightFBO holds a softer half-res light
    // texture, and postQuadVB is the screen-covering rectangle used by those
    // shaders.
    std::unique_ptr<Rendering::IFramebuffer> sceneFBO;
    std::unique_ptr<Rendering::IFramebuffer> volumetricLightFBO;
    std::unique_ptr<Rendering::IShader> skyAtmosphereShader;
    std::unique_ptr<Rendering::IShader> volumetricLightShader;
    std::unique_ptr<Rendering::IShader> tonemapShader;
    uint32_t postQuadVB = 0;
    std::unique_ptr<Rendering::IFramebuffer> shadowFBO;
    Rendering::ShadowFrameData shadowFrame;
    std::unique_ptr<Rendering::IFramebuffer> objectShadowFBO;
    Rendering::ShadowFrameData objectShadowFrame;

    Environment::CameraCollection* cameras = nullptr;
    Textures::TextureCollection* textures = nullptr;
    SkullbonezWindow* window = nullptr;
    Geometry::SkyBox* skyBox = nullptr;
};

struct RunCameraState
{
    Hardware::InputState input = {}; // Current frame input state

    int selectedCamera = 0;          // Keeps track of which camera is selected
    bool isFlyMode = false;          // Free-fly camera mode active (toggle with F)
    bool isNudgeMode = false;        // Nudge mode: free camera + live simulation (toggle with N)
    bool needsMouseLookReset = true; // Discard stale absolute mouse deltas after UI/focus/fly transitions
    bool hasMouseLookLastClient = false;
    POINT mouseLookLastClient = {};
    float cameraTime = 0.0f;         // Camera helper clock
    int trackBallIndex = -1;         // Index of ball to track with camera (-1 = no tracking)
    float trackHeight = 300.0f;      // Camera height above tracked ball
    float autoCycleInterval = -1.0f; // Seconds between per-ball auto screenshots (-1 = disabled)
    float autoCycleAccum = 0.0f;     // Accumulated real-time seconds since last shot
    int autoCycleShotsTaken = 0;     // Number of per-ball screenshots taken so far
};

struct RunSceneState
{
    int currentSceneIndex = -1;    // Index into scene queue (-1 = not yet loaded)
    int loadCount = 0;             // Number of scene/generated loads since startup
    int manualResetCount = 0;      // Number of user-triggered resets since startup
    bool isSceneMode = false;      // Scene file mode (deterministic, data-driven)
    bool isScenePhysics = true;    // Physics enabled in scene mode
    bool isSceneText = true;       // Text overlay enabled in scene mode
    int targetFrameCount = -1;     // Frames to render before holding (-1 = unlimited)
    int currentFrame = 0;          // Current frame counter for the loaded scene/generated run
    int modelCount = 0;            // Number of models in the active scene
    int solverBallCount = 0;       // Exact solver ball count when generated through solver_balls
    int solverBoxCount = 0;        // Exact solver box count when generated through solver_boxes
    unsigned int rngSeed = 0;      // Effective RNG seed used to build the current scene
    unsigned int rngState = 1;     // Local deterministic generator state for scene object setup
    float timeScale = 1.0f;        // Physics time multiplier
    bool isFixedStep = false;      // One physics tick per render frame at PHYSICS_FIXED_DT (deterministic)
    bool isExitOnComplete = false; // Exit automatically when targetFrameCount is reached
    bool isTestComplete = false;   // Set when targetFrameCount is reached without --exit; appends "- TEST COMPLETE" to HUD
    bool isFinishLogged = false;   // Debug event log guard for scene completion
    bool isInteractiveRun = false; // User/UI controlled scene flow: completion automation may hold/advance but never quit

    // Live cinematic scene state. Scene files can override only selected fields,
    // while UI sliders mutate this copy at runtime so the user can tune the look
    // without changing engine.cfg.
    bool hasCinematicRenderingOverride = false;
    bool isCinematicRenderingEnabled = false;
    bool hasCinematicExposure = false;
    float cinematicExposure = 1.0f;
    bool hasCinematicGamma = false;
    float cinematicGamma = 2.2f;
    uint64_t cinematicOverrideMask = 0;
    uint64_t uiCinematicOverrideMask = 0; // Cine-tab values edited by sliders/toggles and eligible for Save Defaults
    CinematicRenderConfig cinematicRender;
};

struct RunScreenshotState
{
    bool isScreenshotSaved = false;   // Screenshot already written this run
    bool isScreenshotAndExit = false; // Capture frame 1 as SCENENAME.bmp then exit
    int screenshotFrame = -1;         // Save screenshot at this frame (-1 = unused)
    int screenshotMs = -1;            // Save screenshot at this elapsed ms (-1 = unused)
    char screenshotPath[256] = {};    // Output path for screenshot (empty = none)
    int screenshotInterval = -1;      // Save screenshot every N frames (-1 = disabled)
    int intervalCaptureCount = 0;     // Sequential counter for interval captures
    char screenshotDir[256] = {};     // Output directory for interval captures
};

struct RunLiveStyleControlState
{
    bool enabled = false;                 // Polls a small control folder for live .style and screenshot requests
    char directory[260] = {};             // Folder containing live.style, capture.txt, and status.txt
    char stylePath[300] = {};             // Style descriptor applied without reloading the scene
    char capturePath[300] = {};           // Text command file used to request one screenshot
    char statusPath[300] = {};            // Latest harness status for scripts/humans
    char pendingScreenshotPath[512] = {}; // Screenshot path requested by capture.txt
    uint64_t styleStamp = 0;              // Last applied live.style write stamp
    uint64_t captureStamp = 0;            // Last consumed capture.txt write stamp
    int styleApplyCount = 0;              // Successful live style applications
    int captureCount = 0;                 // Successful live screenshots
    bool hasPendingScreenshot = false;    // Capture should run after render/UI this frame
};

enum class OverlayMode
{
    None,           // Clean screen — nothing shown
    Timers,         // Renderer name, model count, physics solver, profiler overlay
    SceneStats,     // Scene telemetry values used by deterministic tests
    BarsNormalized, // Visual profiler bars — segments fill the bar width (relative)
    BarsAbsolute,   // Visual profiler bars — white = idle/vsync (absolute frame budget)
    Keys,           // Keyboard reference panel
};

struct RunDebugState
{
    OverlayMode overlayMode = OverlayMode::None;              // HUD overlay cycle state (0 key advances through timers, scene stats, bars, and keys)
    bool isWaterFreezeDebug = false;                          // Freeze ocean animation at current shape (toggle with 1)
    bool isWaterNoReflect = false;                            // Disable ocean reflection entirely (2 cycles: FBO→DXR→none)
    bool isWaterRTReflect = false;                            // Use DXR ray-traced reflection (2 cycles: FBO→DXR→none; DXR only if supported)
    bool isWaterFlatDebug = false;                            // Force ocean mesh fully flat, no displacement (toggle with 3)
    bool isTerrainHidden = false;                             // Hide terrain mesh (toggle with 4)
    bool isWaterHidden = false;                               // Hide water mesh (toggle with 5)
    uint32_t physicsDebugFlags = Physics::PHYSICS_DEBUG_NONE; // Draw object axes, contact manifolds, and sleep state (cycle with C)
    bool isPhysicsDebugTransparent = false;                   // Draw translucent debug collision volumes behind physics debug lines (toggle with 6)
    float physicsDebugAlpha = 0.28f;                          // Translucent debug volume alpha
    float physicsDebugContactLinger = 0.45f;                  // Seconds to keep contact manifolds visible after their solver row disappears
    int physicsDebugPipelineStageCursor = 0;                  // F7/F8-selected Catto pipeline stage for PHYSICS_DEBUG_PIPELINE
    bool isCollisionVisualizer = false;                       // Render solid collision/sleep colours for balls and boxes (toggle with V)
    bool isTextOnly = false;                                  // Suppress all 3D rendering; show solid background with large pangram text
    bool isUITestPattern = false;                             // Bright 2D backdrop behind UI for visual blur tests
    bool isTopTextHidden = false;                             // Hide top-left HUD text while leaving other overlays active
    bool isBroadphaseOverlay = false;                         // Broadphase spatial grid visualizer overlay (toggle with G)
    float frozenWaterTime = 0.0f;                             // Simulation time captured when freeze was toggled on
#ifdef _DEBUG
    char reproSnapshotMessage[128] = {};    // Short HUD confirmation after nudge-mode repro dump
    double reproSnapshotMessageUntil = 0.0; // Simulation timer value after which the HUD message expires
#endif
};

static constexpr int RUNTIME_PROJECTILE_POOL_SIZE = 10;

struct RunFireState
{
    // Runtime-created bullet model indices, recycled in a simple ring.
    RunFireState()
    {
        bulletIndices.fill( -1 );
    }

    std::array<int, RUNTIME_PROJECTILE_POOL_SIZE> bulletIndices = {};
    int bulletNext = 0;
    bool bulletPoolReady = false;
};

struct RunUIStressState
{
    bool enabled = false;                   // Deterministic scene-driven UI stress runner
    unsigned int randomState = 0x7F4A7C15u; // LCG state, seeded from scene UI options
    int actionsPerFrame = 4;                // Cheap UI state mutations per rendered frame
    int framesRun = 0;                      // Stress-run frame counter independent of scene resets
};

enum class GeneratedObjectTypeOverride
{
    Mixed,
    AllBalls,
    AllBoxes
};

/* -- Skullbonez Run ---------------------------------------------------------------------------------------------------------------------------------------------

    Harness for the Skullbonez Core graphics library.
-----------------------------------------------------------------------------------------------------------------------------------------------------------------*/
class SkullbonezRun
{

  private:
    std::vector<std::string> m_sceneQueue; // Ordered list of scene paths ("" = generated demo scene)
    std::vector<std::string> m_sceneBrowserPaths;
    std::vector<std::string> m_sceneBrowserNames;
    std::vector<const char*> m_sceneBrowserNamePtrs;
    bool m_leftSceneCycleWasDown = false;
    bool m_rightSceneCycleWasDown = false;
    double m_lastEscapeTapTime = -1000.0;
    float m_cmdTimeScaleOverride = 0.0f; // CLI --time-scale override applied after each scene load (0 = not set)
    bool m_cmdFixedStep = false;         // CLI --fixed-step override applied after each scene load
    unsigned int m_cmdSeedOverride = 0;  // CLI --seed override applied after each scene load (0 = not set)
    bool m_cmdNoWater = false;           // CLI --no-water starts fluid below terrain
    bool m_cmdNoSleep = false;           // Startup CLI --no-sleep request; the live policy can still be toggled from the Physics tab
    bool m_cmdHasTornadoOverride = false;
    bool m_cmdTornadoEnabled = false;
    bool m_cmdTornadoVectors = false;
    bool m_cmdHasCinematicRenderingOverride = false;
    bool m_cmdCinematicRendering = false;
    bool m_cmdHasCinematicShadowsOverride = false;
    bool m_cmdCinematicShadows = false;
    bool m_cmdDemoHeroStyle = false;                // CLI --demohero applies the low-poly hero look to generated demo mode
    bool m_cmdInteractiveSceneRun = false;          // CLI --interactive/--hold keeps scene automation from quitting the app
    int m_cmdFrameCountOverride = -1;               // CLI --frames override applied after each scene load
    bool m_cmdUIStress = false;                     // CLI --ui-stress enables generated/demo stress without a scene file
    unsigned int m_cmdUIStressSeed = 0;             // CLI --ui-stress-seed
    int m_cmdUIStressActions = 5;                   // CLI --ui-stress-actions
    int m_selectedCineModeSceneIndex = -1;          // -1=Demo/default look, otherwise scene-browser index of live cine/concept look
    CinematicRenderConfig m_defaultCinematicRender; // engine.cfg cinematic baseline restored by the Demo Scene cine mode
    GeneratedObjectTypeOverride m_generatedObjectTypeOverride = GeneratedObjectTypeOverride::Mixed;
    float m_UITimeScaleOverride = 0.0f;
    int m_UIModelCountOverride = -1;
    int m_UISolverBallCountOverride = -1;
    int m_UISolverBoxCountOverride = -1;
    bool m_cmdHasPhysicsDebugFlagsOverride = false;
    uint32_t m_cmdPhysicsDebugFlagsOverride = Physics::PHYSICS_DEBUG_NONE;
    bool m_cmdHasPhysicsDebugTransparentOverride = false;
    bool m_cmdPhysicsDebugTransparentOverride = false;
    bool m_cmdHasPhysicsDebugAlphaOverride = false;
    float m_cmdPhysicsDebugAlphaOverride = 0.28f;
    bool m_cmdHasPhysicsDebugContactLingerOverride = false;
    float m_cmdPhysicsDebugContactLingerOverride = 0.45f;

    RunPerfLogState m_perfLogState; // Perf/test logging paths, files, and flush policy
#ifdef _DEBUG
    RunPhysicsDiagnosticsState m_physicsDiagnostics; // Queryable model-facing physics diagnostic trace
#endif
    RunRuntimeSettings m_runtimeSettings;                     // Scene/app runtime swap policy toggles
    RunTimerState m_timers;                                   // Frame/simulation timers and rolling timing values
    RunSubsystemState m_systems;                              // Window, camera, texture, terrain, and reflection handles
    RunCameraState m_camera;                                  // Camera/input state and ball-tracking settings
    RunSceneState m_scene;                                    // Scene-mode execution state
    RunScreenshotState m_screenshot;                          // Screenshot trigger and capture state
    RunLiveStyleControlState m_liveStyle;                     // Live style tweak/capture harness state
    UI::InGameUI m_UI;                                        // Encapsulated in-game diagnostics window
    RunDebugState m_debug;                                    // Runtime debug/overlay toggles
    RunFireState m_fire;                                      // Runtime silver bullet pool state
    RunUIStressState m_uiStress;                              // Deterministic UI stress run state
    Physics::BroadphaseVisualizer m_broadphaseVisualizer;     // Spatial grid debug overlay (G key toggle)
    Physics::CollisionVisualizer m_collisionVisualizer;       // Solid collision/sleep model visualizer (V key toggle)
    Physics::PhysicsDebugVisualizer m_physicsDebugVisualizer; // Line overlay for object axes, contact manifolds, and sleep state
    Environment::WorldEnvironment m_cWorldEnvironment;        // SkullbonezCore::Environment::WorldEnvironment class
    GameObjects::GameModelCollection m_cGameModelCollection;  // SkullbonezCore::GameObjects::GameModelCollection class
    std::array<float, MAX_GAME_MODELS * 16> m_dxrReflectionTransforms = {};

    inline static int sPerfPass = 0;
    void Render();                                                                                                                                     // Main render method
    void RelativeUpdateCamera( uint32_t hash );                                                                                                        // Relative update specified camera
    void UpdateLogic( float simulationDt, float cameraDt );                                                                                            // Per-frame logic; cameraDt is unscaled wall time
    void TakeInput();                                                                                                                                  // Take user input
    void StepPhysicsPipelineStage( int direction );                                                                                                    // Move the debug pipeline visualization cursor left/right
    void SetUpCameras();                                                                                                                               // Camera init for generated demo mode
    void SetUpCamerasFromScene( const TestScene& scene );                                                                                              // Camera init from scene file
    void SetUpGameModels( int count );                                                                                                                 // Game model init for generated mixed-object mode
    void SetUpSolverObjects( int balls, int boxes );                                                                                                   // Game model init: exact N solver balls + M solver boxes
    void SetUpGameModelsFromScene( const TestScene& scene );                                                                                           // Game model init from scene file
    void RegisterBuiltInAssets();                                                                                                                      // Registers built-in texture and shader source records
    std::string ResolveSourceAssetPath( Assets::AssetKind kind, const char* logicalName, const std::string& relativePath );                            // Registers and resolves a source asset under DATA_ROOT
    void DrawPrimitives();                                                                                                                             // Draw OpenGL primitives here
    CinematicRenderConfig& ActiveCinematicConfig();                                                                                                    // Mutable cinematic style config for the active scene/run
    const CinematicRenderConfig& ActiveCinematicConfig() const;                                                                                        // Read-only cinematic style config for the active scene/run
    bool IsCinematicRenderingEnabled() const;                                                                                                          // True when the HDR/post stack should wrap the main scene
    void EnsureCinematicRenderResources();                                                                                                             // Lazily builds/resizes HDR scene target and post resources
    void ResetCinematicRenderResources();                                                                                                              // Releases HDR/post resources before backend teardown
    void EnsureShadowRenderResources( const CinematicRenderConfig& cinematic );                                                                        // Lazily builds/resizes the directional shadow-map target
    void ResetShadowRenderResources();                                                                                                                 // Releases shadow-map resources before backend teardown
    Rendering::ShadowFrameData BuildShadowFrameData( const CinematicRenderConfig& cinematic, const Math::Vector::Vector3& lightDirectionWorld ) const; // Builds a stable light-space frame for shadow mapping
    void RenderCinematicSky( const Math::Transformation::Matrix4& view, const Math::Transformation::Matrix4& projection );                             // Draws procedural HDR sunset sky into the active cinematic target
    bool RenderCinematicVolumetricLight();                                                                                                             // Renders depth-aware low-resolution light shafts into the volumetric buffer
    void ResolveCinematicSceneToBackbuffer( bool sceneAlreadyUnbound, bool volumetricReady );                                                          // Tonemaps HDR scene target to the backbuffer
    void SetInitialOpenGlState();                                                                                                                      // Sets the initial state of the OpenGL evironment
    void SetViewingOrientation();                                                                                                                      // Renders camera views etc
    void DrawWindowText( const double dSecondsPerFrame );                                                                                              // Renders text to the window
    void SaveScreenshot( const char* path );                                                                                                           // Saves current backbuffer to a BMP file
    bool SaveCurrentSceneDefaults();                                                                                                                   // Writes UI-controlled defaults back to the active scene file
    void RefreshSceneBrowserList();                                                                                                                    // Discovers scene files available to the in-game scene dropdown
    int CurrentSceneBrowserIndex() const;                                                                                                              // Returns current scene index within the discovered scene dropdown list
    void LoadSceneFromBrowserIndex( int index );                                                                                                       // Loads a scene selected from the in-game scene dropdown
    void LoadDemoSceneFromUI();                                                                                                                        // Loads the generated demo scene from the in-game Scene tab
    bool ApplyCinematicModeFromBrowserIndex( int index );                                                                                              // Applies a cine/concept look live without rebuilding the scene
    bool ApplyAdjacentCinematicMode( int direction );                                                                                                  // Cycles live cine/concept looks without rebuilding the scene
    void ApplyLiveStyleScene( const TestScene& styleScene );                                                                                           // Applies style-only cinematic/material directives without rebuilding objects
    void ApplyDemoHeroStyleOverride();                                                                                                                 // Applies the low-poly hero style to generated demo mode
    void LoadAdjacentSceneFromBrowser( int direction );                                                                                                // Keyboard scene cycling through the discovered scene dropdown list
    void EnterInteractiveSceneRun();                                                                                                                   // Locks scene automation into non-quitting interactive mode
    bool CanSceneAutomationQuit() const;                                                                                                               // True for CLI suites/tests; false once the user owns scene flow
    void HoldCompletedInteractiveScene();                                                                                                              // Keep the current scene alive after interactive automation completes
    bool HasSceneQueueEntry( int index ) const;                                                                                                        // True when index points at a queued scene/demo entry
    bool HasCurrentSceneQueueEntry() const;                                                                                                            // True when currentSceneIndex points at a queued entry
    const std::string* CurrentSceneQueuePath() const;                                                                                                  // Current queued scene path, or nullptr if no current entry
    RunInternal::SceneRuntimeResetSnapshot CaptureSceneRuntimeResetSnapshot();                                                                         // Captures live runtime controls before a scene reset rebuilds objects
    void RestoreSceneRuntimeResetSnapshot( const RunInternal::SceneRuntimeResetSnapshot& snapshot, bool suppressExitOnComplete );                      // Restores preserved live controls after scene file/defaults rebuild
    void ClearSceneRuntimeUIOverrides();                                                                                                               // Clears UI rebuild overrides when a new scene/defaults should be authoritative
    void LogPerfMemory( const char* checkpoint );                                                                                                      // Log memory usage to perf CSV
    void LoadScene( int index, bool preserveUIState = false, bool suppressExitOnComplete = false, bool preserveRuntimeState = false );                 // Resets scene-specific state and loads a scene by queue index
    void ResetCurrentScene( bool preserveUIState = false, bool suppressExitOnComplete = false, bool preserveRuntimeState = true );                     // User-triggered reset/reload of current scene or generated demo mode
    void ApplyUIModelCountOverride( int count );                                                                                                       // Rebuilds the active generated model pool from the UI slider
    void ApplyUISolverObjectCounts( int balls, int boxes );                                                                                            // Rebuilds generated solver objects from exact UI counts
    void ApplyUIWorldOverride( float gravity, float fluidHeight, float fluidDensity );                                                                 // Applies live world/fluid scalar controls
    void ApplyNoWaterOverride();                                                                                                                       // Pushes fluid surface below the active terrain when requested
    void ApplyTornadoDefaultsForActiveScene();                                                                                                         // Centers the tornado around the active inner-water/basin region
    void SyncTornadoFieldToPhysics();                                                                                                                  // Sends live tornado state to the physics collection
    void UseDefaultTerrain();                                                                                                                          // Restores the normal height-map terrain when leaving analytic test scenes
    void UseFlatSlopeTerrain( float baseY, float slopeX, float slopeZ );                                                                               // Activates analytic flat-slope terrain for focused physics scenes
    void UpdateWorldTerrainBounds();                                                                                                                   // Keeps world/fluid helpers aligned with the active terrain bounds
    bool AdvanceScene();                                                                                                                               // Advances to the next scene in the queue (returns false if done)
    void MoveCamera( float keyMovementQty, float mouseMovemementQty );                                                                                 // Moves the camera
    // Builds a tight light-space frame for nearby object receivers.
    Rendering::ShadowFrameData BuildObjectShadowFrameData( const CinematicRenderConfig& cinematic, const Math::Vector::Vector3& lightDirectionWorld, const Math::Vector::Vector3& focusHint );
    // Renders requested depth casters from the sun view.
    void RenderShadowMap( Rendering::IFramebuffer& target, const Rendering::ShadowFrameData& shadowFrame, const CinematicRenderConfig& cinematic, bool renderTerrain, bool renderObjects );
    void ReleaseBackendOwnedResourcesForSwitch();        // Retired switch helper retained until resource-reset cleanup
    void RebuildBackendOwnedResourcesAfterSwitch();      // Retired switch helper retained until resource-reset cleanup
    void RunRendererSwitchResourceReleaseSteps();        // Ordered backend-resource release registry retained for cleanup
    void RunRendererSwitchResourceRebuildSteps();        // Ordered backend-resource rebuild registry retained for cleanup
    void ReleaseReflectionResourcesForSwitch();          // Releases reflection framebuffer ownership before backend teardown
    void RebuildReflectionResourcesAfterSwitch();        // Recreates reflection framebuffer ownership after backend startup
    void ReleaseTextResourcesForSwitch();                // Releases text renderer GPU resources before backend teardown
    void RebuildTextResourcesAfterSwitch();              // Recreates text renderer GPU resources after backend startup
    void ReleaseModelCollectionResourcesForSwitch();     // Releases game-model collection GPU resources before backend teardown
    void ReleaseHelperResourcesForSwitch();              // Releases helper-owned cached render resources before backend teardown
    void ReleaseCollisionVisualizerResourcesForSwitch(); // Releases collision visualizer GPU resources before backend teardown
    void ReleaseUIResourcesForSwitch();                  // Releases in-game UI GPU resources before backend teardown
    void ReleaseTextureResourcesForSwitch();             // Clears texture collection GPU resources before backend teardown
    void RebuildTerrainResourcesAfterSwitch();           // Recreates active terrain GPU resources after backend startup
    void RebuildSkyBoxResourcesAfterSwitch();            // Recreates active skybox GPU resources after backend startup
    void RebuildWorldResourcesAfterSwitch();             // Recreates world/fluid GPU resources after backend startup
    unsigned int NextUIStressRandom();
    int NextUIStressInt( int maxExclusive );
    float NextUIStressFloat( float minValue, float maxValue );
    void RunUIStressActions();

    // --- Per-frame tick helpers (called from Run()) ---
    void TickPhysics( double dt );              // Physics dispatch: fixed-step and variable-step accumulator
    bool TickScreenshots();                     // Screenshot triggers; returns true when frame should restart (continue)
    void TickLiveStyleControl();                // Poll live.style/capture.txt and apply look changes without scene reload
    void TickLiveStyleControlCapture();         // Save pending harness screenshot after render/UI are drawn
    void TickAutoCycle();                       // Auto-cycle ball capture; posts WM_QUIT when all balls captured
    void TickPerfLog();                         // Write per-frame perf CSV row and periodic memory checkpoint
    bool TickSceneAdvance();                    // Frame count, exit/hold on completion, restarts; returns true to continue
    void UpdateWaterHeightControls( float dt ); // Slide water surface up/down while held
    void ResetProjectilePool();                 // Clears cached projectile indices after scene/model rebuilds
    bool EnsureProjectilePool();                // Lazily creates the ten runtime silver bullets
    void FireProjectile();                      // Recycle and launch a high-speed silver bullet from the camera
#ifdef _DEBUG
    void LogSceneFinished( const char* reason );
    bool PickNudgeReproTarget( int& outIndex, float& outRayT, float& outCrosshairDistance );
    void WriteNudgeReproSnapshot();
    void BeginPhysicsDiagnosticsRun( const char* scenePath );
    void EndPhysicsDiagnosticsRun( const char* status );
#endif

  public:
    SkullbonezRun( std::vector<std::string> sceneQueue );               // Constructor (scene queue; empty string = generated demo scene)
    ~SkullbonezRun();                                                   // Default destructor
    void Initialise();                                                  // Initialises shared resources and loads first scene
    void RunSceneLoadOnly();                                            // Loads every queued scene once, then returns without entering the frame loop
    void Run();                                                         // Runs all scenes in sequence — main message loop
    void SetTimeScaleOverride( float scale );                           // Override timeScale for every scene loaded (CLI --time-scale)
    void SetFixedStepOverride();                                        // Force fixed-step for every scene loaded (CLI --fixed-step)
    void SetSeedOverride( unsigned int seed );                          // Override RNG seed for every scene loaded (CLI --seed)
    void SetNoWaterOverride();                                          // Start scenes with fluid below terrain (CLI --no-water)
    void SetNoSleepOverride();                                          // Disable physics sleeping for every scene loaded (CLI --no-sleep)
    void SetTornadoOverride( bool enabled );                            // Enable/disable tornado mode for loaded scenes (CLI --tornado)
    void SetTornadoVectorFieldOverride( bool enabled );                 // Show/hide tornado velocity vectors at startup
    void SetCinematicRenderingOverride( bool enabled );                 // Force cinematic HDR/post rendering on/off for every scene loaded
    void SetCinematicShadowsOverride( bool enabled );                   // Force shadow maps on/off for every scene loaded
    void SetDemoHeroStyleOverride();                                    // Run generated demo mode with the low-poly hero rendering style
    void SetInteractiveRunOverride();                                   // Keep scene automation from quitting the app (CLI --interactive/--hold)
    void SetLiveStyleControlDirectory( const char* path );              // Enable live .style/capture harness in a control folder
    void SetFrameCountOverride( int frames );                           // Stop scene/demo automation after N frames (CLI --frames)
    void SetUIStressOverride( unsigned int seed, int actionsPerFrame ); // Enable deterministic UI stress from CLI
    void SetInitialOverlayMode( OverlayMode mode );
    void SetTopTextHidden( bool hidden );
    void SetBroadphaseVisualizerEnabled( bool enabled );
    void SetGeneratedObjectTypeOverride( GeneratedObjectTypeOverride objectTypeOverride );
    void SetPhysicsDebugFlagsOverride( uint32_t flags );
    void SetPhysicsDebugTransparentOverride( bool transparent );
    void SetPhysicsDebugAlphaOverride( float alpha );
    void SetPhysicsDebugContactLingerOverride( float seconds );
    void DumpTextureAssets( FILE* out ) const;

#ifdef _DEBUG
    void SetPhysicsRegressionLogOverride( const char* path );                              // Override regression CSV path for all scenes
    void SetPhysicsCollisionTimeLogOverride( const char* path );                           // Override swept collision-time CSV path for all scenes
    void SetPhysicsDiagnosticsPath( const char* path, bool fixedStepForcedByDiagnostics ); // Enable queryable physics diagnostics (CLI --physics-diag)
#endif
};
} // namespace Basics
} // namespace SkullbonezCore
