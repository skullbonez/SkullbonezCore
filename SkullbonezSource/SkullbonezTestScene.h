#pragma once


// --- Includes ---
#include "SkullbonezCommon.h"
#include "SkullbonezConfig.h"
#include "SkullbonezPhysicsDebugVisualizer.h"
#include "SkullbonezVector3.h"
#include <vector>

namespace SkullbonezCore
{
namespace Basics
{
class TestScene;
class TestSceneParser;
TestScene LoadTestSceneFromFileImpl( const char* path );

struct SceneCamera
{
    Math::Vector::Vector3 m_position;
    Math::Vector::Vector3 view;
    Math::Vector::Vector3 up;

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
    bool isFixed;
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

enum SceneCinematicOverrideBits : uint64_t
{
    // Scene files may specify any subset of cinematic_* directives. Each bit says
    // "this exact field was authored in the scene." That lets the loader merge
    // scene-specific values over engine.cfg without wiping unspecified defaults.
    SCENE_CINE_RENDERING = 1ull << 0,
    SCENE_CINE_SKY_ATMOSPHERE = 1ull << 1,
    SCENE_CINE_CLOUDS = 1ull << 2,
    SCENE_CINE_GOD_RAYS = 1ull << 3,
    SCENE_CINE_VOLUMETRIC_LIGHTING = 1ull << 4,
    SCENE_CINE_BLOOM = 1ull << 5,
    SCENE_CINE_FOG = 1ull << 6,
    SCENE_CINE_TERRAIN_RELIEF_ENABLED = 1ull << 7,
    SCENE_CINE_EXPOSURE = 1ull << 8,
    SCENE_CINE_GAMMA = 1ull << 9,
    SCENE_CINE_SUN_SCREEN_X = 1ull << 10,
    SCENE_CINE_SUN_SCREEN_Y = 1ull << 11,
    SCENE_CINE_SUN_COLOR_R = 1ull << 12,
    SCENE_CINE_SUN_COLOR_G = 1ull << 13,
    SCENE_CINE_SUN_COLOR_B = 1ull << 14,
    SCENE_CINE_SUN_INTENSITY = 1ull << 15,
    SCENE_CINE_SKY_HORIZON_R = 1ull << 16,
    SCENE_CINE_SKY_HORIZON_G = 1ull << 17,
    SCENE_CINE_SKY_HORIZON_B = 1ull << 18,
    SCENE_CINE_SKY_ZENITH_R = 1ull << 19,
    SCENE_CINE_SKY_ZENITH_G = 1ull << 20,
    SCENE_CINE_SKY_ZENITH_B = 1ull << 21,
    SCENE_CINE_SKY_GLOW_STRENGTH = 1ull << 22,
    SCENE_CINE_CLOUD_COVERAGE = 1ull << 23,
    SCENE_CINE_CLOUD_SOFTNESS = 1ull << 24,
    SCENE_CINE_CLOUD_SCALE = 1ull << 25,
    SCENE_CINE_CLOUD_INTENSITY = 1ull << 26,
    SCENE_CINE_SUN_SHAFT_STRENGTH = 1ull << 27,
    SCENE_CINE_SUN_SHAFT_FALLOFF = 1ull << 28,
    SCENE_CINE_VOLUMETRIC_STRENGTH = 1ull << 29,
    SCENE_CINE_VOLUMETRIC_DENSITY = 1ull << 30,
    SCENE_CINE_VOLUMETRIC_DECAY = 1ull << 31,
    SCENE_CINE_BLOOM_THRESHOLD = 1ull << 32,
    SCENE_CINE_BLOOM_KNEE = 1ull << 33,
    SCENE_CINE_BLOOM_STRENGTH = 1ull << 34,
    SCENE_CINE_BLOOM_RADIUS = 1ull << 35,
    SCENE_CINE_TERRAIN_RELIEF = 1ull << 36,
    SCENE_CINE_BASIN_DEPTH = 1ull << 37,
    SCENE_CINE_BASIN_RIM_LIFT = 1ull << 38,
    SCENE_CINE_FOG_COLOR_R = 1ull << 39,
    SCENE_CINE_FOG_COLOR_G = 1ull << 40,
    SCENE_CINE_FOG_COLOR_B = 1ull << 41,
    SCENE_CINE_FOG_START = 1ull << 42,
    SCENE_CINE_FOG_END = 1ull << 43,
    SCENE_CINE_FOG_DENSITY = 1ull << 44,
    SCENE_CINE_FOG_MAX_OPACITY = 1ull << 45,
    SCENE_CINE_STYLE_MODES = 1ull << 46,
    SCENE_CINE_STYLE_GRADE = 1ull << 47,
    SCENE_CINE_TERRAIN_TINT = 1ull << 48,
    SCENE_CINE_TERRAIN_ACCENT = 1ull << 49,
    SCENE_CINE_TERRAIN_GRID = 1ull << 50,
    SCENE_CINE_WATER_TINT = 1ull << 51,
    SCENE_CINE_WATER_PROFILE = 1ull << 52,
    SCENE_CINE_BASIN_MASK = 1ull << 53,
};

struct SceneObjectMaterialOverride
{
    char target[64];
    float tintR = 1.0f;
    float tintG = 1.0f;
    float tintB = 1.0f;
    float materialMode = 1.0f;
};

struct SceneOptions
{
    bool isPhysicsEnabled = true;
    bool isTextEnabled = true;
    bool isTextOnly = false;
    int frameCount = -1;                                      // -1 = unlimited
    unsigned int seed = 0;                                    // RNG seed (0 = use time-based default)
    int solverBallCount = 0;                                  // exact impulse-solver balls to spawn (0 = not set)
    int solverBoxCount = 0;                                   // exact impulse-solver boxes to spawn (0 = not set)
    float timeScale = 1.0f;                                   // Physics time multiplier (1.0 = realtime)
    bool isFixedStep = false;                                 // If true, each render frame triggers exactly one physics tick at PHYSICS_FIXED_DT
    uint32_t physicsDebugFlags = Physics::PHYSICS_DEBUG_NONE; // Draw physics debug axes/contacts/sleep/pipeline markers
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
    bool hasCinematicRenderingOverride = false;               // Scene explicitly toggles cinematic HDR/post rendering
    bool cinematicRendering = false;                          // Cinematic HDR/post rendering scene override
    bool hasCinematicExposure = false;                        // Scene explicitly sets tonemap exposure
    float cinematicExposure = 1.0f;                           // Scene tonemap exposure
    bool hasCinematicGamma = false;                           // Scene explicitly sets output gamma
    float cinematicGamma = 2.2f;                              // Scene output gamma
    uint64_t cinematicOverrideMask = 0;                       // Per-field overrides from cinematic_* directives
    CinematicRenderConfig cinematicRender;                    // Scene-authored cinematic values for overridden fields
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
    bool hasWaterComboOpen = false;
    bool waterComboOpen = false;
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
    bool hasStress = false;
    bool stressEnabled = false;
    bool hasStressSeed = false;
    unsigned int stressSeed = 0;
    bool hasStressActions = false;
    int stressActionsPerFrame = 4;
};

/* -- Test Scene -------------------------------------------------------------------------------------------------------------------------------------------------

    Loads and holds a deterministic scene description from a .scene file.
    Used for render regression testing — provides fixed cameras, fixed ball placements,
    and control over physics and frame count.
-----------------------------------------------------------------------------------------------------------------------------------------------------------------*/
class TestScene
{

  private:
    friend TestScene LoadTestSceneFromFileImpl( const char* path );
    friend class TestSceneParser;

    std::vector<SceneCamera> m_cameras;
    std::vector<SceneBall> m_balls;
    std::vector<SceneBallState> m_ballStates;
    std::vector<SceneBox> m_boxes;
    std::vector<SceneObjectMaterialOverride> m_objectMaterials;

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
    int GetSolverBallCount() const;
    int GetSolverBoxCount() const;
    const char* GetPerfLogPath() const;
    bool IsPerfLogFlushEnabled() const;
    int GetPerfLogFlushInterval() const;
    bool HasVsyncOverride() const;
    bool IsVsyncEnabled() const;
    bool HasPipelineSyncOverride() const;
    bool IsPipelineSyncEnabled() const;
    int GetScreenshotInterval() const;
    const char* GetScreenshotDir() const;
    float GetTimeScale() const;
    bool IsFixedStep() const;
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
    bool HasCinematicRenderingOverride() const;
    bool IsCinematicRenderingEnabled() const;
    bool HasCinematicExposure() const;
    float GetCinematicExposure() const;
    bool HasCinematicGamma() const;
    float GetCinematicGamma() const;
    uint64_t GetCinematicOverrideMask() const;
    const CinematicRenderConfig& GetCinematicRenderConfig() const;
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
    int GetObjectMaterialOverrideCount() const;
    const SceneObjectMaterialOverride& GetObjectMaterialOverride( int index ) const;
    bool HasWorldOverride() const;
    float GetWorldGravity() const;
    float GetWorldFluidHeight() const;
    float GetWorldFluidDensity() const;
    const SceneUIOptions& GetUIOptions() const;
};
} // namespace Basics
} // namespace SkullbonezCore
