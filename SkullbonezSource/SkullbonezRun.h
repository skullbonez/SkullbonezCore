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


// --- Usings ---
using namespace SkullbonezCore::Environment;
using namespace SkullbonezCore::Hardware;
using namespace SkullbonezCore::Textures;
using namespace SkullbonezCore::Text;
using namespace SkullbonezCore::Geometry;
using namespace SkullbonezCore::Math;
using namespace SkullbonezCore::GameObjects;


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
    FILE* rollLogFile = nullptr;        // Open handle for roll orientation log (empty = none)
};

struct RunVectorLogState
{
    bool isVectorLogEnabled = false;                  // Per-frame velocity/omega CSV diagnostic
    int vectorLogInterval = 6;                        // Vector log cadence (N frames)
    bool isVectorLogFlushEnabled = false;             // Flush each vector log write batch
    char vectorLogPath[256] = "Debug/vector_log.csv"; // Output path for vector CSV
    FILE* vectorLogFile = nullptr;                    // Open handle for vector CSV
};

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
    float timeSinceLastRender = 0.0f;
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
    float cameraTime = 0.0f;         // Camera helper clock
    int trackBallIndex = -1;         // Index of ball to track with camera (-1 = no tracking)
    float trackHeight = 300.0f;      // Camera height above tracked ball
    float autoCycleInterval = -1.0f; // Seconds between per-ball auto screenshots (-1 = disabled)
    float autoCycleAccum = 0.0f;     // Accumulated real-time seconds since last shot
    int autoCycleShotsTaken = 0;     // Number of per-ball screenshots taken so far
};

struct RunSceneState
{
    int currentSceneIndex = -1; // Index into scene queue (-1 = not yet loaded)
    bool isSceneMode = false;   // Scene file mode (deterministic, data-driven)
    bool isScenePhysics = true; // Physics enabled in scene mode
    bool isSceneText = true;    // Text overlay enabled in scene mode
    int targetFrameCount = -1;  // Frames to render before holding (-1 = unlimited)
    int currentFrame = 0;       // Current frame counter for scene mode
    int modelCount = 0;         // Number of models in the active scene
    float timeScale = 1.0f;     // Physics time multiplier (1.0 = realtime)
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

struct RunDebugState
{
    bool isProfilerOverlay = true;   // Profiler overlay visible (toggle with 0; default ON in profile builds)
    bool isWaterFreezeDebug = false; // Freeze ocean animation at current shape (toggle with 1)
    bool isWaterNoReflect = false;   // Disable ocean reflection, output flat tint (toggle with 2)
    bool isWaterFlatDebug = false;   // Force ocean mesh fully flat, no displacement (toggle with 3)
    bool isTerrainHidden = false;    // Hide terrain mesh (toggle with 4)
    bool isWaterHidden = false;      // Hide water mesh (toggle with 5)
    bool isDebugVectors = false;     // Draw velocity (green) and angular velocity (red) vectors (toggle with V)
    bool isTextOnly = false;         // Suppress all 3D rendering; show solid background with large pangram text
    float frozenWaterTime = 0.0f;    // Simulation time captured when freeze was toggled on
};

/* -- Skullbonez Run ---------------------------------------------------------------------------------------------------------------------------------------------

    Harness for the Skullbonez Core graphics library.
-----------------------------------------------------------------------------------------------------------------------------------------------------------------*/
class SkullbonezRun
{

  private:
    std::vector<std::string> m_sceneQueue; // Ordered list of scene paths ("" = legacy mode)

    RunPerfLogState m_perfLogState;             // Perf/test logging paths, files, and flush policy
    RunVectorLogState m_vectorLogState;         // Vector logging controls and file handle
    RunRuntimeSettings m_runtimeSettings;       // Scene/app runtime toggles (vsync, sync, roll-align)
    RunTimerState m_timers;                     // Frame/simulation timers and rolling timing values
    RunSubsystemState m_systems;                // Window, camera, texture, terrain, and reflection handles
    RunCameraState m_camera;                    // Camera/input state and ball-tracking settings
    RunSceneState m_scene;                      // Scene-mode execution state
    RunScreenshotState m_screenshot;            // Screenshot trigger and capture state
    RunDebugState m_debug;                      // Runtime debug/overlay toggles
    WorldEnvironment m_cWorldEnvironment;       // SkullbonezCore::Environment::WorldEnvironment class
    GameModelCollection m_cGameModelCollection; // SkullbonezCore::GameObjects::GameModelCollection class

    inline static int sPerfPass = 0;

    void Render();                                                     // Main render method
    void RelativeUpdateCamera( uint32_t hash );                        // Relative update specified camera
    void UpdateLogic( float fSecondsPerFrame );                        // Update world logic
    void TakeInput();                                                  // Take user input
    void SetUpCameras();                                               // Camera init (legacy mode)
    void SetUpCamerasFromScene( const TestScene& scene );              // Camera init from scene file
    void SetUpGameModels( int count );                                 // Game model init (random legacy mode)
    void SetUpGameModelsFromScene( const TestScene& scene );           // Game model init from scene file
    void DrawPrimitives();                                             // Draw OpenGL primitives here
    void SetInitialOpenGlState();                                      // Sets the initial state of the OpenGL evironment
    void SetViewingOrientation();                                      // Renders camera views etc
    void DrawWindowText( const double dSecondsPerFrame );              // Renders text to the window
    void SaveScreenshot( const char* path );                           // Saves framebuffer to BMP file via glReadPixels
    void LogPerfMemory( const char* checkpoint );                      // Log memory usage to perf CSV
    void LoadScene( int index );                                       // Resets scene-specific state and loads a scene by queue index
    bool AdvanceScene();                                               // Advances to the next scene in the queue (returns false if done)
    void MoveCamera( float keyMovementQty, float mouseMovemementQty ); // Moves the camera

  public:
    SkullbonezRun( std::vector<std::string> sceneQueue ); // Constructor (scene queue; empty string = legacy mode)
    ~SkullbonezRun();                                     // Default destructor
    void Initialise();                                    // Initialises shared resources and loads first scene
    void Run();                                           // Runs all scenes in sequence — main message loop
};
} // namespace Basics
} // namespace SkullbonezCore
