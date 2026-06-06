#pragma once


// --- Includes ---
#include "SkullbonezCommon.h"
#include "SkullbonezPhysicsDebugVisualizer.h"
#include "SkullbonezVector3.h"
#include <vector>


// --- Usings ---
using namespace SkullbonezCore::Math::Vector;


namespace SkullbonezCore
{
namespace Basics
{
struct SceneCamera
{
    Vector3 m_position;
    Vector3 view;
    Vector3 up;

    char name[64];
};

struct SceneBall
{
    char name[64];
    float posX, posY, posZ;
    float m_radius;
    float m_mass;
    float moment;
    float restitution;
    float forceX, forceY, forceZ;
    float forcePosX, forcePosY, forcePosZ;
    float eulerX, eulerY, eulerZ; // Initial orientation in degrees (optional, default 0)
    bool hasInitOrient;
};

struct SceneBallState
{
    char name[64];
    float posX, posY, posZ;
    float velX, velY, velZ;
    float angVelX, angVelY, angVelZ;
    float orientX, orientY, orientZ, orientW;
    float radius, mass, restitution;
    float inertiaX, inertiaY, inertiaZ;
};

struct SceneBox
{
    char name[64];
    float posX, posY, posZ;
    float halfX, halfY, halfZ; // Half-extents
    float mass;
    float restitution;
    float eulerX, eulerY, eulerZ; // Initial orientation in degrees (optional, default 0)
    float velX, velY, velZ;       // Initial linear velocity (optional, default 0)
    bool hasInitOrient;
    bool hasInitVelocity;
    bool isFixed;
};

struct SceneOptions
{
    bool isPhysicsEnabled = true;
    bool isTextEnabled = true;
    bool isTextOnly = false;
    int frameCount = -1;                                      // -1 = unlimited
    unsigned int seed = 0;                                    // RNG seed (0 = use time-based default)
    bool hasLegacyBallCount = false;                          // True when legacy_balls was explicitly specified
    int legacyBallCount = 0;                                  // random legacy-style balls (0 = none)
    int physicsMode = 0;                                      // 0=inherit from CLI, 1=legacy, 2=solver (per-scene override)
    int solverBallCount = 0;                                  // exact impulse-solver balls to spawn (0 = not set)
    int solverBoxCount = 0;                                   // exact impulse-solver boxes to spawn (0 = not set)
    float timeScale = 1.0f;                                   // Physics time multiplier (1.0 = realtime)
    bool isFixedStep = false;                                 // If true, each render frame triggers exactly one physics tick at PHYSICS_FIXED_DT
    bool isDebugVectors = false;                              // Draw velocity/omega debug arrows
    uint32_t physicsDebugFlags = Physics::PHYSICS_DEBUG_NONE; // Draw physics debug axes/contacts/sleep markers
    bool physicsDebugTransparent = false;                     // Render translucent debug collision volumes while physics debug is visible
    float physicsDebugAlpha = 0.28f;                          // Alpha for translucent debug collision volumes
    float physicsDebugContactLinger = 0.45f;                  // Seconds to keep contact manifold debug rows visible
    float trackHeight = -1.0f;                                // Height above tracked ball for camera (-1 = no tracking)
    float autoCycleInterval = -1.0f;                          // Seconds between per-ball screenshots (-1 = disabled)
    bool screenshotAndExit = false;                           // Capture first frame as SCENENAME.bmp then exit
    bool exitOnComplete = false;                              // Exit automatically when targetFrameCount is reached
    bool collisionVisualizer = false;                         // Render solid collision/sleep debug colours
    bool broadphaseOverlay = false;                           // Render spatial broadphase debug overlay
    bool waterFreezeDebug = false;                            // Freeze water animation at load time
    bool waterFlatDebug = false;                              // Render water as a flat mesh
    int waterReflectionMode = 0;                              // 0=FBO, 1=DXR, 2=None
    bool waterHidden = false;                                 // Suppress water rendering (for clean texture comparison)
    bool terrainHidden = false;                               // Suppress terrain rendering
};

struct SceneCaptureOptions
{
    char screenshotPath[256] = {}; // output path for screenshot (empty = none)
    int screenshotFrame = -1;      // trigger on frame N (-1 = unused)
    int screenshotMs = -1;         // trigger at N ms elapsed (-1 = unused)
    int screenshotInterval = -1;   // save screenshot every N frames (-1 = disabled)
    char screenshotDir[256] = {};  // output directory for interval captures
};

struct SceneLoggingOptions
{
    char perfLogPath[256] = {};   // output path for perf CSV (empty = none)
    bool isPerfLogFlush = false;  // Force flush after each perf-log write
    int perfLogFlushInterval = 0; // Flush perf log every N writes (0 = only at close)
};

struct SceneRuntimeOverrides
{
    bool hasVsyncOverride = false;        // Scene-level override present for vsync
    bool isVsyncEnabled = true;           // V-Sync policy for scene when override is present
    bool hasPipelineSyncOverride = false; // Scene-level pipeline-sync override present
    bool isPipelineSyncEnabled = false;   // Pipeline-sync policy for scene when override is present
    bool hasRollAlignOverride = false;    // Scene-level roll-align override present
    bool isRollAlignEnabled = true;       // Terrain roll/pole alignment correction policy
};

struct SceneTerrainOverride
{
    bool hasFlatSlope = false; // True when scene overrides terrain with analytic flat slope
    float flatBaseY = 0.0f;    // y = flatBaseY + flatSlopeX*x + flatSlopeZ*z
    float flatSlopeX = 0.0f;
    float flatSlopeZ = 0.0f;
};

struct SceneWorldOverride
{
    bool hasWorldOverride = false;
    float worldGravity = 0.0f;
    float worldFluidHeight = 0.0f;
    float worldFluidDensity = 0.0f;
};

struct SceneUIOptions
{
    bool hasDirective = false;
    bool hasVisible = false;
    bool isVisible = false;
    bool hasMinimized = false;
    bool isMinimized = false;
    bool hasActiveTab = false;
    int activeTab = 0; // 0=Profiler, 1=Scene, 2=Physics, 3=Options, 4=Controls
    bool hasWindowRect = false;
    int windowX = 34;
    int windowY = 56;
    int windowW = 760;
    int windowH = 540;
    bool hasBlur = false;
    bool blurEnabled = true;
    bool hasRendererComboOpen = false;
    bool rendererComboOpen = false;
    bool hasSceneComboOpen = false;
    bool sceneComboOpen = false;
    bool hasSceneFilter = false;
    char sceneFilter[64] = {};
    bool hasProfilerExpandAll = false;
    bool profilerExpandAll = false;
    bool hasProfilerTimeline = false;
    bool profilerTimeline = false;
    bool hasPerformanceHistogram = false;
    bool performanceHistogram = false;
    bool hasScrollY = false;
    float scrollY = 0.0f;
    bool hasTestPattern = false;
    bool testPatternEnabled = false;
    bool hasMouseOverride = false;
    int mouseX = 0;
    int mouseY = 0;
};

/* -- Test Scene -------------------------------------------------------------------------------------------------------------------------------------------------

    Loads and holds a deterministic scene description from a .scene file.
    Used for render regression testing — provides fixed cameras, fixed ball placements,
    and control over physics and frame count.
-----------------------------------------------------------------------------------------------------------------------------------------------------------------*/
class TestScene
{

  private:
    std::vector<SceneCamera> m_cameras;
    std::vector<SceneBall> m_balls;
    std::vector<SceneBallState> m_ballStates;
    std::vector<SceneBox> m_boxes;

    SceneOptions m_sceneOptions;
    SceneCaptureOptions m_captureOptions;
    SceneLoggingOptions m_loggingOptions;
    SceneRuntimeOverrides m_runtimeOverrides;
    SceneTerrainOverride m_terrainOverride;
    SceneWorldOverride m_worldOverride;
    SceneUIOptions m_UIOptions;

  public:
    TestScene();
    static TestScene LoadFromFile( const char* path );

    bool IsPhysicsEnabled() const;
    bool IsTextEnabled() const;
    bool IsTextOnly() const;
    int GetFrameCount() const;
    const char* GetScreenshotPath() const;
    int GetScreenshotFrame() const;
    int GetScreenshotMs() const;
    unsigned int GetSeed() const;
    bool HasLegacyBallCount() const;
    int GetLegacyBallCount() const;
    int GetPhysicsMode() const; // 0=inherit, 1=legacy, 2=solver
    int GetSolverBallCount() const;
    int GetSolverBoxCount() const;
    const char* GetPerfLogPath() const;
    bool IsPerfLogFlushEnabled() const;
    int GetPerfLogFlushInterval() const;
    bool HasVsyncOverride() const;
    bool IsVsyncEnabled() const;
    bool HasPipelineSyncOverride() const;
    bool IsPipelineSyncEnabled() const;
    bool HasRollAlignOverride() const;
    bool IsRollAlignEnabled() const;
    int GetScreenshotInterval() const;
    const char* GetScreenshotDir() const;
    float GetTimeScale() const;
    bool IsFixedStep() const;
    bool IsDebugVectors() const;
    uint32_t GetPhysicsDebugFlags() const;
    bool IsPhysicsDebugTransparent() const;
    float GetPhysicsDebugAlpha() const;
    float GetPhysicsDebugContactLinger() const;
    float GetTrackHeight() const;       // Returns tracking camera height above ball (-1 = disabled)
    float GetAutoCycleInterval() const; // Returns per-ball screenshot interval in seconds (-1 = disabled)
    bool IsScreenshotAndExit() const;   // True if scene should capture first frame then exit
    bool IsExitOnComplete() const;      // True if scene should exit automatically when frame count is reached
    bool IsCollisionVisualizerEnabled() const;
    bool IsBroadphaseOverlayEnabled() const;
    bool IsWaterFreezeDebugEnabled() const;
    bool IsWaterFlatDebugEnabled() const;
    int GetWaterReflectionMode() const;
    bool IsWaterHidden() const;
    bool IsTerrainHidden() const;
    bool HasFlatSlope() const; // True when scene specifies flat analytic slope terrain
    float GetFlatBaseY() const;
    float GetFlatSlopeX() const;
    float GetFlatSlopeZ() const;
    int GetCameraCount() const;
    int GetBallCount() const;
    const SceneCamera& GetCamera( int index ) const;
    const SceneBall& GetBall( int index ) const;
    int GetBallStateCount() const;
    const SceneBallState& GetBallState( int index ) const;
    int GetBoxCount() const;
    const SceneBox& GetBox( int index ) const;
    bool HasWorldOverride() const;
    float GetWorldGravity() const;
    float GetWorldFluidHeight() const;
    float GetWorldFluidDensity() const;
    const SceneUIOptions& GetUIOptions() const;
};
} // namespace Basics
} // namespace SkullbonezCore
