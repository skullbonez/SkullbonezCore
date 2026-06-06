#pragma once


// --- Includes ---
#include <memory>
#include <string>
#include <vector>
#include "SkullbonezCommon.h"
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
#include "SkullbonezTestScene.h"
#include "SkullbonezBroadphaseVisualizer.h"
#include "SkullbonezCollisionVisualizer.h"
#include "SkullbonezPhysicsDebugVisualizer.h"
#include "UI/SkullbonezUi.h"


// --- Usings ---
using namespace SkullbonezCore::Environment;
using namespace SkullbonezCore::Hardware;
using namespace SkullbonezCore::Textures;
using namespace SkullbonezCore::Text;
using namespace SkullbonezCore::Geometry;
using namespace SkullbonezCore::Math;
using namespace SkullbonezCore::GameObjects;
using namespace SkullbonezCore::Physics;
using namespace SkullbonezCore::Ui;


namespace SkullbonezCore
{
namespace Basics
{
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
    char physicsRegressionLogOverride[256] = {}; // CLI --physics-regression-log path (empty = disabled)
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
    int runSequence = 0;                       // Incremented on every scene/legacy load
};
#endif


struct RunRuntimeSettings
{
    bool isVsyncEnabled = true;          // Swap-chain sync interval (true = vsync)
    bool isPipelineSyncEnabled = false;  // Force CPU/GPU sync via Finish() before render
    bool defaultRollAlignEnabled = true; // Config default for roll orientation correction
    bool isRollAlignEnabled = true;      // Active roll orientation correction state for current scene
};

struct RunTimerState
{
    Timer frameTimer;
    Timer workTimer;
    Timer updateTimer;
    Timer cameraTimer;
    Timer simulationTimer;

    float physicsTime = 0.0f;        // Last frame physics time (seconds)
    float rollingPhysicsTime = 0.0f; // Smoothed physics time accumulator
    float renderTime = 0.0f;         // Last frame render time (seconds)
    float rollingRenderTime = 0.0f;  // Smoothed render time accumulator
    float rollingFpsTime = 0.0f;     // Smoothed FPS time accumulator
    float rollingSceneEnergy = 0.0f; // Half-second averaged kinetic energy
    float timeSinceLastRender = 0.0f;
    double sceneEnergyAccumulator = 0.0;
    int sceneEnergySampleCount = 0;
    int lastUiDrawCalls = 0;         // Actual UI draw calls measured around Frame/Text/UI last frame
    float physicsAccumulator = 0.0f; // Accumulated time for fixed-step physics
};

struct RunSubsystemState
{
    std::unique_ptr<Terrain> terrain;
    std::unique_ptr<IFramebuffer> reflectionFBO;

    CameraCollection* cameras = nullptr;
    TextureCollection* textures = nullptr;
    SkullbonezWindow* window = nullptr;
    SkyBox* skyBox = nullptr;
};

struct RunCameraState
{
    InputState input = {}; // Current frame input state

    int selectedCamera = 0;          // Keeps track of which camera is selected
    bool isFlyMode = false;          // Free-fly camera mode active (toggle with F)
    bool isNudgeMode = false;        // Nudge mode: free camera + live simulation (toggle with N)
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
    int loadCount = 0;             // Number of scene/legacy loads since startup
    int manualResetCount = 0;      // Number of user-triggered resets since startup
    bool isSceneMode = false;      // Scene file mode (deterministic, data-driven)
    bool isScenePhysics = true;    // Physics enabled in scene mode
    bool isSceneText = true;       // Text overlay enabled in scene mode
    int targetFrameCount = -1;     // Frames to render before holding (-1 = unlimited)
    int currentFrame = 0;          // Current frame counter for the loaded scene/legacy run
    int modelCount = 0;            // Number of models in the active scene
    unsigned int rngSeed = 0;      // Effective RNG seed used to build the current scene
    float timeScale = 1.0f;        // Physics time multiplier (1.0 = realtime)
    bool isFixedStep = false;      // One physics tick per render frame at PHYSICS_FIXED_DT (deterministic)
    bool isExitOnComplete = false; // Exit automatically when targetFrameCount is reached
    bool isTestComplete = false;   // Set when targetFrameCount is reached without --exit; appends "- TEST COMPLETE" to HUD
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
    bool isDebugVectors = false;                              // Draw velocity (green) and angular velocity (red) vectors (toggle with 9)
    uint32_t physicsDebugFlags = Physics::PHYSICS_DEBUG_NONE; // Draw object axes, contact manifolds, and sleep state (cycle with C)
    bool isPhysicsDebugTransparent = false;                   // Draw translucent debug collision volumes behind physics debug lines (toggle with 6)
    float physicsDebugAlpha = 0.28f;                          // Translucent debug volume alpha
    float physicsDebugContactLinger = 0.45f;                  // Seconds to keep contact manifolds visible after their solver row disappears
    bool isCollisionVisualizer = false;                       // Render solid collision/sleep colours for balls and boxes (toggle with V)
    bool isTextOnly = false;                                  // Suppress all 3D rendering; show solid background with large pangram text
    bool isUiTestPattern = false;                             // Bright 2D backdrop behind UI for visual blur tests
    bool isTopTextHidden = false;                             // Hide top-left HUD text while leaving other overlays active
    bool isBroadphaseOverlay = false;                         // Broadphase spatial grid visualizer overlay (toggle with G)
    float frozenWaterTime = 0.0f;                             // Simulation time captured when freeze was toggled on
    float rendererSwitchInterval = -1.0f;                     // Auto-switch renderer every N seconds (-1 = disabled)
    float rendererSwitchAccum = 0.0f;                         // Accumulated time since last auto-switch
#ifdef _DEBUG
    char reproSnapshotMessage[128] = {};    // Short HUD confirmation after nudge-mode repro dump
    double reproSnapshotMessageUntil = 0.0; // Simulation timer value after which the HUD message expires
#endif
};

struct RunFireState
{
    // Cycling indices for projectile recycling.  Each shot steps backwards through the
    // model array so rapid-fire uses different objects instead of relaunching the same one.
    // -1 means "not yet initialised" — the first shot seeds the index from the array tail.
    int ballNext = -1; // Next sphere model index to recycle
    int boxNext = -1;  // Next box model index to recycle
};

enum class RuntimeRendererType
{
    OpenGL,
    DX11,
    DX12
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
    std::vector<std::string> m_sceneQueue; // Ordered list of scene paths ("" = legacy mode)
    float m_cmdTimeScaleOverride = 0.0f;   // CLI --time-scale override applied after each scene load (0 = not set)
    bool m_cmdFixedStep = false;           // CLI --fixed-step override applied after each scene load
    unsigned int m_cmdSeedOverride = 0;    // CLI --seed override applied after each scene load (0 = not set)
    bool m_cmdNoWater = false;             // CLI --no-water starts fluid below terrain
    GeneratedObjectTypeOverride m_generatedObjectTypeOverride = GeneratedObjectTypeOverride::Mixed;
    bool m_cmdHasPhysicsDebugFlagsOverride = false;
    uint32_t m_cmdPhysicsDebugFlagsOverride = PHYSICS_DEBUG_NONE;
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
    RunRuntimeSettings m_runtimeSettings;            // Scene/app runtime toggles (vsync, sync, roll-align)
    RunTimerState m_timers;                          // Frame/simulation timers and rolling timing values
    RunSubsystemState m_systems;                     // Window, camera, texture, terrain, and reflection handles
    RunCameraState m_camera;                         // Camera/input state and ball-tracking settings
    RunSceneState m_scene;                           // Scene-mode execution state
    RunScreenshotState m_screenshot;                 // Screenshot trigger and capture state
    InGameUi m_ui;                                   // Encapsulated in-game diagnostics window
    RunDebugState m_debug;                           // Runtime debug/overlay toggles
    RunFireState m_fire;                             // Projectile recycling state (CTRL = ball, ALT = box)
    BroadphaseVisualizer m_broadphaseVisualizer;     // Spatial grid debug overlay (G key toggle)
    CollisionVisualizer m_collisionVisualizer;       // Solid collision/sleep model visualizer (V key toggle)
    PhysicsDebugVisualizer m_physicsDebugVisualizer; // Line overlay for object axes, contact manifolds, and sleep state
    WorldEnvironment m_cWorldEnvironment;            // SkullbonezCore::Environment::WorldEnvironment class
    GameModelCollection m_cGameModelCollection;      // SkullbonezCore::GameObjects::GameModelCollection class

    inline static int sPerfPass = 0;
    void Render();                                                     // Main render method
    void RelativeUpdateCamera( uint32_t hash );                        // Relative update specified camera
    void UpdateLogic( float fSecondsPerFrame );                        // Camera, autocycle, logs — once per frame
    void TakeInput();                                                  // Take user input
    void SetUpCameras();                                               // Camera init (legacy mode)
    void SetUpCamerasFromScene( const TestScene& scene );              // Camera init from scene file
    void SetUpGameModels( int count );                                 // Game model init (random legacy mode)
    void SetUpSolverObjects( int balls, int boxes );                   // Game model init: exact N solver balls + M solver boxes
    void SetUpGameModelsFromScene( const TestScene& scene );           // Game model init from scene file
    void DrawPrimitives();                                             // Draw OpenGL primitives here
    void SetInitialOpenGlState();                                      // Sets the initial state of the OpenGL evironment
    void SetViewingOrientation();                                      // Renders camera views etc
    void DrawWindowText( const double dSecondsPerFrame );              // Renders text to the window
    void SaveScreenshot( const char* path );                           // Saves framebuffer to BMP file via glReadPixels
    void LogPerfMemory( const char* checkpoint );                      // Log memory usage to perf CSV
    void LoadScene( int index );                                       // Resets scene-specific state and loads a scene by queue index
    void ResetCurrentScene();                                          // User-triggered reset/reload of current scene or legacy mode
    void ApplyNoWaterOverride();                                       // Pushes fluid surface below the active terrain when requested
    bool AdvanceScene();                                               // Advances to the next scene in the queue (returns false if done)
    void MoveCamera( float keyMovementQty, float mouseMovemementQty ); // Moves the camera
    RuntimeRendererType GetCurrentRendererType() const;                // Detect active backend type from Gfx renderer identity
    RuntimeRendererType GetNextRendererType( RuntimeRendererType current ) const;
    void SwitchRenderer( RuntimeRendererType target ); // Rebuild render backend/resources while preserving simulation state

    // --- Per-frame tick helpers (called from Run()) ---
    void TickRendererSwitch( float dt );                  // Advance auto-switch timer; cycle backend when interval elapses
    void TickPhysics( double dt );                        // Physics dispatch: fixed-step, accumulator, legacy vs solver
    bool TickScreenshots();                               // Screenshot triggers; returns true when frame should restart (continue)
    void TickAutoCycle();                                 // Auto-cycle ball capture; posts WM_QUIT when all balls captured
    void TickPerfLog();                                   // Write per-frame perf CSV row and periodic memory checkpoint
    bool TickSceneAdvance();                              // Frame count, exit/hold on completion, restarts; returns true to continue
    void UpdateWaterHeightControls( float dt );           // Slide water surface up/down while held
    void NudgeModelsWithCamera( const Vector3& moveVec ); // Push overlapping balls/boxes in camera movement direction
    void FireProjectile( bool isBox );                    // Recycle and launch a ball (CTRL) or box (ALT) from the camera
#ifdef _DEBUG
    bool PickNudgeReproTarget( int& outIndex, float& outRayT, float& outCrosshairDistance );
    void WriteNudgeReproSnapshot();
    void BeginPhysicsDiagnosticsRun( const char* scenePath );
    void EndPhysicsDiagnosticsRun( const char* status );
#endif

  public:
    SkullbonezRun( std::vector<std::string> sceneQueue, bool legacyPhysics = false ); // Constructor (scene queue; empty string = legacy mode)
    ~SkullbonezRun();                                                                 // Default destructor
    void Initialise();                                                                // Initialises shared resources and loads first scene
    void Run();                                                                       // Runs all scenes in sequence — main message loop
    void SetRendererSwitchInterval( float seconds );
    void SetTimeScaleOverride( float scale );  // Override timeScale for every scene loaded (CLI --time-scale)
    void SetFixedStepOverride();               // Force fixed-step for every scene loaded (CLI --fixed-step)
    void SetSeedOverride( unsigned int seed ); // Override RNG seed for every scene loaded (CLI --seed)
    void SetNoWaterOverride();                 // Start scenes with fluid below terrain (CLI --no-water)
    void SetInitialOverlayMode( OverlayMode mode );
    void SetTopTextHidden( bool hidden );
    void SetBroadphaseVisualizerEnabled( bool enabled );
    void SetGeneratedObjectTypeOverride( GeneratedObjectTypeOverride objectTypeOverride );
    void SetPhysicsDebugFlagsOverride( uint32_t flags );
    void SetPhysicsDebugTransparentOverride( bool transparent );
    void SetPhysicsDebugAlphaOverride( float alpha );
    void SetPhysicsDebugContactLingerOverride( float seconds );

#ifdef _DEBUG
    void SetPhysicsRegressionLogOverride( const char* path );                              // Override regression CSV path for all scenes
    void SetPhysicsDiagnosticsPath( const char* path, bool fixedStepForcedByDiagnostics ); // Enable queryable physics diagnostics (CLI --physics-diag)
#endif
};
} // namespace Basics
} // namespace SkullbonezCore
