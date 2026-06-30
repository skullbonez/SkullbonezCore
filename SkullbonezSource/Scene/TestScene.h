/*
File: SkullbonezSource/Scene/TestScene.h
Purpose:
  Stores parsed test-scene JSON and applies it to runtime scene state.

Mental model:
  Runtime code connects authored scene data, input, simulation, render
  backends, and validation-oriented launch modes. Follow who owns state and
  when that state changes.

Glossary:
  DXR (DirectX Raytracing): DX12 API used for hardware ray traversal and
  reflection dispatch.
  FBO (Framebuffer Object): Engine shorthand for an off-screen render target
  exposed through the renderer abstraction.
  CSV (Comma-Separated Values): Text table format used for byte-exact physics
  regression output.
  Validation gate: Repository script that proves a class of changes before
  commit or PR.
  Override mask: Bitfield that records which optional JSON fields were authored
  so unspecified values keep engine.cfg defaults.
  Asset system: Runtime-owned registry that resolves logical asset-library names
    without requiring the parser to query process-global state.

Invariants:
  - Command-line and scene JSON fields are user-facing compatibility
  surface.

Related:
  - SkullbonezSource/Scene/TestScene.cpp
  - Agentic/Reference/runtime-reference.md
  - Agentic/Reference/comment-style-guide.md
*/
#pragma once


#include "../Assets/AssetSystem.h"
#include "../Core/Common.h"
#include "../Core/Config.h"
#include "../Physics/Debug/PhysicsDebugVisualizer.h"
#include "../Physics/TornadoField.h"
#include "../Rendering/RenderMaterial.h"
#include "../Maths/Vector3.h"
#include <vector>

namespace SkullbonezCore
{
namespace Basics
{
class TestScene;
class TestSceneParser;
TestScene LoadTestSceneFromFileImpl( const char* path, Assets::AssetContext assets );
TestScene LoadStyleSceneFromFileImpl( const char* path, Assets::AssetContext assets );

struct SceneCamera
{
    Math::Vector::Vector3 m_position;
    Math::Vector::Vector3 view;
    Math::Vector::Vector3 up;

    char name[64];                                            // Stable authoring name used by diagnostics and camera selection.
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
    float eulerX, eulerY, eulerZ;                             // Initial orientation in degrees (optional, default 0)
    bool hasInitOrient;                                       // False means use default identity orientation.
    bool isFixed;                                             // Fixed bodies participate in contacts but do not integrate.
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
    bool isFixed;
    bool isSleeping;
};

struct SceneBoxState
{
    char name[64];
    float posX, posY, posZ;
    float velX, velY, velZ;
    float angVelX, angVelY, angVelZ;
    float orientX, orientY, orientZ, orientW;
    float halfX, halfY, halfZ;
    float mass, restitution;
    float inertiaX, inertiaY, inertiaZ;
    bool isFixed;
    bool isSleeping;
};

struct SceneConvexHullState
{
    char name[64];
    char hullPath[260];
    float posX, posY, posZ;
    float velX, velY, velZ;
    float angVelX, angVelY, angVelZ;
    float orientX, orientY, orientZ, orientW;
    float mass, restitution;
    float inertiaX, inertiaY, inertiaZ;
    float contactReleaseImpulseThreshold;
    bool isFixed;
    bool isSleeping;
    bool contactReleaseOnImpact;
};

struct SceneRagdoll
{
    char name[64];
    float posX, posY, posZ;
    float scale;
    float eulerX, eulerY, eulerZ;
    bool hasInitOrient;
    bool isFixed;
    bool startsAsleep;
};

struct ScenePointJointConstraint
{
    char bodyA[64];
    char bodyB[64];
    Math::Vector::Vector3 localAnchorA = Math::Vector::ZERO_VECTOR;
    Math::Vector::Vector3 localAnchorB = Math::Vector::ZERO_VECTOR;
    float slack = 0.25f;
    float stiffness = 0.22f;
    float damping = 0.35f;
    uint32_t groupId = 0;
    uint8_t flags = 0;
};

struct SceneBox
{
    char name[64];
    float posX, posY, posZ;
    float halfX, halfY, halfZ;                                // Half-extents
    float mass;
    float restitution;
    float eulerX, eulerY, eulerZ;                             // Initial orientation in degrees (optional, default 0)
    float velX, velY, velZ;                                   // Initial linear velocity (optional, default 0)
    bool hasInitOrient;
    bool hasInitVelocity;
    bool isFixed;
};

struct SceneConvexHull
{
    char name[64];
    char hullPath[260];
    float posX, posY, posZ;
    float mass;
    float restitution;
    float eulerX, eulerY, eulerZ;
    float velX, velY, velZ;
    float angVelX, angVelY, angVelZ;
    float contactReleaseImpulseThreshold;
    bool hasInitOrient;
    bool hasInitVelocity;
    bool hasInitAngularVelocity;
    bool isFixed;
    bool isSleeping;
    bool contactReleaseOnImpact;
};

enum SceneCinematicOverrideBits : uint64_t
{
    // Scene files may specify any subset of cinematic JSON fields. Each bit says
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
    SCENE_CINE_SHADOWS = 1ull << 54,
    SCENE_CINE_RESERVED_55 = 1ull << 55,
    SCENE_CINE_SHADOW_MAP_SIZE = 1ull << 56,
    SCENE_CINE_SHADOW_PCF_RADIUS = 1ull << 57,
    SCENE_CINE_SHADOW_STRENGTH = 1ull << 58,
    SCENE_CINE_SHADOW_SOFTNESS = 1ull << 59,
    SCENE_CINE_SHADOW_DEPTH_BIAS = 1ull << 60,
    SCENE_CINE_SHADOW_SLOPE_BIAS = 1ull << 61,
    SCENE_CINE_SHADOW_MAX_DISTANCE = 1ull << 62,
};

struct SceneObjectMaterialOverride
{
    char target[64];
    float tintR = 1.0f;
    float tintG = 1.0f;
    float tintB = 1.0f;
    float materialMode = 1.0f;
    Rendering::RenderMaterial material;
};

struct SceneRequiredContact
{
    char nameA[64] = {};
    char nameB[64] = {};
};

struct SceneRequiredBroadphaseXCells
{
    int minCellX = 0;
    int maxCellX = 0;
    int cellY = 0;
    int cellZ = 0;
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
    int modelCapacity = -1;                                   // active game-model capacity (-1 = use startup/config capacity)
    int workerThreads = -2;                                   // -2 = use startup/config worker count, -1 = auto, 0 = disabled, >0 = explicit workers
    float timeScale = 1.0f;                                   // Physics time multiplier (1.0 = realtime)
    bool isFixedStep = false;                                 // If true, each render frame triggers exactly one physics tick at PHYSICS_FIXED_DT
    bool pauseSnapshotState = true;                           // Start authored body-state scenes paused for inspection
    uint32_t physicsDebugFlags = Physics::PHYSICS_DEBUG_NONE; // Physics debug overlay mask.
    bool physicsDebugTransparent = false;                     // Translucent collision volumes while physics debug is visible.
    float physicsDebugAlpha = 0.28f;                          // Alpha for translucent debug collision volumes
    float physicsDebugContactLinger = 0.45f;                  // Seconds to keep contact manifold debug rows visible
    float trackHeight = -1.0f;                                // Height above tracked ball for camera (-1 = no tracking)
    float autoCycleInterval = -1.0f;                          // Seconds between per-ball screenshots (-1 = disabled)
    bool screenshotAndExit = false;                           // Capture first frame as SCENENAME.bmp then exit
    bool exitOnComplete = false;                              // Exit automatically when targetFrameCount is reached
    bool collisionVisualizer = false;                         // Solid collision/sleep debug colours.
    bool broadphaseOverlay = false;                           // Spatial broadphase debug overlay.
    bool waterFreezeDebug = false;                            // Freeze water animation at load time
    bool waterFlatDebug = false;                              // Flat water mesh for debug captures.
    int waterReflectionMode = 0;                              // 0=FBO, 1=DXR, 2=None
    bool waterHidden = false;                                 // Suppress water rendering (for clean texture comparison)
    bool terrainHidden = false;                               // Suppress terrain rendering
    bool editableScene = false;                               // Scene-tab starter scene; Save Defaults persists live object state
    bool hasCinematicRenderingOverride = false;               // Scene explicitly toggles cinematic HDR/post rendering
    bool cinematicRendering = false;                          // Cinematic HDR/post rendering scene override
    bool hasCinematicExposure = false;                        // Scene explicitly sets tonemap exposure
    float cinematicExposure = 1.0f;                           // Scene tonemap exposure
    bool hasCinematicGamma = false;                           // Scene explicitly sets output gamma
    float cinematicGamma = 2.2f;                              // Scene output gamma
    uint64_t cinematicOverrideMask = 0;                       // Per-field overrides from cinematic JSON fields
    CinematicRenderConfig cinematicRender;                    // Scene-authored cinematic values for overridden fields
};

struct SceneCaptureOptions
{
    char screenshotPath[256] = {};                            // output path for screenshot (empty = none)
    int screenshotFrame = -1;                                 // trigger on frame N (-1 = unused)
    int screenshotMs = -1;                                    // trigger at N ms elapsed (-1 = unused)
    int screenshotInterval = -1;                              // save screenshot every N frames (-1 = disabled)
    char screenshotDir[256] = {};                             // output directory for interval captures
};

struct SceneLoggingOptions
{
    char perfLogPath[256] = {};                               // output path for perf CSV (empty = none)
    bool isPerfLogFlush = false;                              // Force flush after each perf-log write
    int perfLogFlushInterval = 0;                             // Flush perf log every N writes (0 = only at close)
};

struct SceneRuntimeOverrides
{
    bool hasVsyncOverride = false;                            // Scene-level override present for vsync
    bool isVsyncEnabled = true;                               // V-Sync policy for scene when override is present
    bool hasPipelineSyncOverride = false;                     // Scene-level pipeline-sync override present
    bool isPipelineSyncEnabled = false;                       // Pipeline-sync policy for scene when override is present
};

struct SceneTerrainOverride
{
    bool hasFlatSlope = false;                                // True when scene overrides terrain with analytic flat slope
    float flatBaseY = 0.0f;                                   // y = flatBaseY + flatSlopeX*x + flatSlopeZ*z
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

struct SceneTornadoSystem
{
    bool hasTornadoSystem = false;
    Physics::TornadoSystemConfig config;
};

struct SceneUIOptions
{
    bool hasSettings = false;
    bool hasVisible = false;
    bool isVisible = false;
    bool hasMinimized = false;
    bool isMinimized = false;
    bool hasActiveTab = false;
    int activeTab = 0;                                        // InGameUITab ordinal.
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
    bool hasHitboxOverlay = false;
    bool hitboxOverlay = false;
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

/* -- Test Scene
-------------------------------------------------------------------------------------------------------------------------------------------------

    Loads and holds a deterministic scene description from a .scene.json file.
    Used for render regression testing — provides fixed cameras, fixed ball placements,
    and control over physics and frame count.
-----------------------------------------------------------------------------------------------------------------------------------------------------------------*/
class TestScene
{

  private:
    // Parser-local construction helpers populate the immutable scene record in
    // one pass; runtime systems use public read-only access below.
    friend TestScene LoadTestSceneFromFileImpl( const char* path, Assets::AssetContext assets );
    friend TestScene LoadStyleSceneFromFileImpl( const char* path, Assets::AssetContext assets );
    friend class TestSceneParser;

    std::vector<SceneCamera> m_cameras;
    std::vector<SceneBall> m_balls;
    std::vector<SceneBallState> m_ballStates;
    std::vector<SceneBoxState> m_boxStates;
    std::vector<SceneBox> m_boxes;
    std::vector<SceneConvexHull> m_convexHulls;
    std::vector<SceneConvexHullState> m_convexHullStates;
    std::vector<SceneRagdoll> m_ragdolls;
    std::vector<ScenePointJointConstraint> m_pointJointConstraints;
    std::vector<SceneObjectMaterialOverride> m_objectMaterials;
    std::vector<SceneRequiredContact> m_requiredContacts;
    std::vector<SceneRequiredBroadphaseXCells> m_requiredBroadphaseXCells;

    SceneOptions m_sceneOptions;
    SceneCaptureOptions m_captureOptions;
    SceneLoggingOptions m_loggingOptions;
    SceneRuntimeOverrides m_runtimeOverrides;
    SceneTerrainOverride m_terrainOverride;
    SceneWorldOverride m_worldOverride;
    SceneTornadoSystem m_tornadoSystem;
    SceneUIOptions m_UIOptions;

  public:
    TestScene();
    static TestScene LoadFromFile( const char* path );

    // Runtime callers pass the owned asset registry so scene asset-library
    // tokens resolve without touching ActiveAssetSystem().
    static TestScene LoadFromFile( const char* path, const Assets::AssetSystem& assets );
    static TestScene LoadStyleFromFile( const char* path );

    // Style scenes use the same parser and may include asset-library references
    // through shared scene snippets, so they accept the explicit registry too.
    static TestScene LoadStyleFromFile( const char* path, const Assets::AssetSystem& assets );

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
    bool HasModelCapacityOverride() const;
    int GetModelCapacity() const;
    bool HasWorkerThreadOverride() const;
    int GetWorkerThreads() const;
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
    bool ShouldPauseSnapshotState() const;
    uint32_t GetPhysicsDebugFlags() const;
    bool IsPhysicsDebugTransparent() const;
    float GetPhysicsDebugAlpha() const;
    float GetPhysicsDebugContactLinger() const;
    float GetTrackHeight() const;                             // Tracking camera height above ball; -1 disables.
    float GetAutoCycleInterval() const;                       // Per-ball screenshot interval in seconds; -1 disables.
    bool IsScreenshotAndExit() const;                         // True if scene should capture first frame then exit
    bool IsExitOnComplete() const;                            // True if scene should exit automatically when frame count is reached
    bool IsCollisionVisualizerEnabled() const;
    bool IsBroadphaseOverlayEnabled() const;
    bool IsWaterFreezeDebugEnabled() const;
    bool IsWaterFlatDebugEnabled() const;
    int GetWaterReflectionMode() const;
    bool IsWaterHidden() const;
    bool IsTerrainHidden() const;
    bool IsEditableScene() const;
    bool HasCinematicRenderingOverride() const;
    bool IsCinematicRenderingEnabled() const;
    bool HasCinematicExposure() const;
    float GetCinematicExposure() const;
    bool HasCinematicGamma() const;
    float GetCinematicGamma() const;
    uint64_t GetCinematicOverrideMask() const;
    const CinematicRenderConfig& GetCinematicRenderConfig() const;
    bool HasFlatSlope() const;                                // True when scene specifies flat analytic slope terrain
    float GetFlatBaseY() const;
    float GetFlatSlopeX() const;
    float GetFlatSlopeZ() const;
    int GetCameraCount() const;
    int GetBallCount() const;
    const SceneCamera& GetCamera( int index ) const;
    const SceneBall& GetBall( int index ) const;
    int GetBallStateCount() const;
    const SceneBallState& GetBallState( int index ) const;
    int GetBoxStateCount() const;
    const SceneBoxState& GetBoxState( int index ) const;
    int GetBoxCount() const;
    const SceneBox& GetBox( int index ) const;
    int GetConvexHullCount() const;
    const SceneConvexHull& GetConvexHull( int index ) const;
    int GetConvexHullStateCount() const;
    const SceneConvexHullState& GetConvexHullState( int index ) const;
    int GetRagdollCount() const;
    const SceneRagdoll& GetRagdoll( int index ) const;
    int GetPointJointConstraintCount() const;
    const ScenePointJointConstraint& GetPointJointConstraint( int index ) const;
    int GetObjectMaterialOverrideCount() const;
    const SceneObjectMaterialOverride& GetObjectMaterialOverride( int index ) const;
    int GetRequiredContactCount() const;
    const SceneRequiredContact& GetRequiredContact( int index ) const;
    int GetRequiredBroadphaseXCellCount() const;
    const SceneRequiredBroadphaseXCells& GetRequiredBroadphaseXCell( int index ) const;
    bool HasWorldOverride() const;
    float GetWorldGravity() const;
    float GetWorldFluidHeight() const;
    float GetWorldFluidDensity() const;
    bool HasTornadoSystem() const;
    const Physics::TornadoSystemConfig& GetTornadoSystemConfig() const;
    const SceneUIOptions& GetUIOptions() const;
};
} // namespace Basics
} // namespace SkullbonezCore
