/*
File: SkullbonezSource/SkullbonezConfig.h
Purpose:
  Loads, stores, and exposes engine configuration values from files and command-line overrides.

Mental model:
  Runtime code connects authored scene data, input, simulation, render
  backends, and validation-oriented launch modes. Follow who owns state and
  when that state changes.

Glossary:
  PGS (Projected Gauss-Seidel): Iterative constraint-solver method used for
  bounded contact impulses.
  SkullScope: Queryable physics diagnostics workflow backed by bounded trace
  output and local queries.
  Validation gate: Repository script that proves a class of changes before
  commit or PR.

Invariants:
  - Command-line and scene-file spellings are user-facing compatibility
  surface.

Related:
  - SkullbonezSource/SkullbonezConfig.cpp
  - Agentic/Reference/runtime-reference.md
  - Agentic/Reference/comment-style-guide.md
*/
#pragma once


#include <cstdio>
#include <string>

namespace SkullbonezCore
{
namespace Basics
{

/*
    Singleton configuration loaded once from SkullbonezData/engine.cfg at startup.
    Access via SkullbonezConfig::Instance().fieldName (or grouped sub-structs) anywhere
    SkullbonezCommon.h is included.
    All fields carry defaults matching the original hard-coded values; the config
    file is optional -- if absent, defaults apply.
*/
struct WindowConfig
{
    int screenX = 1800;
    int screenY = 1000;
    bool fullscreen = false;
    int bitsPerPixel = 32;
    int refreshRate = 75;
};

struct RuntimeRenderFlags
{
    bool vsyncEnabled = true;
    bool forcePipelineSync = false;
    bool renderCollisionVolumes = false;
};

struct SceneLightConfig
{
    float colorR = 1.0f;
    float colorG = 0.5f;
    float colorB = 0.5f;
    float colorA = 1.0f;
};

struct OrdinaryRenderConfig
{
    float sunIntensity = 2.20f;
    float sunColorR = 1.0f;
    float sunColorG = 0.50f;
    float sunColorB = 0.50f;
    float ambientStrength = 1.00f;
    float skyAmbientR = 0.20f;
    float skyAmbientG = 0.10f;
    float skyAmbientB = 0.10f;
    float groundAmbientR = 0.20f;
    float groundAmbientG = 0.10f;
    float groundAmbientB = 0.10f;

    bool shadowsEnabled = true;
    bool shadowTerrainCasts = true;
    bool shadowObjectsCast = true;
    bool shadowTerrainReceives = true;
    bool shadowObjectsReceive = true;
    int shadowMapSize = 2048;
    int shadowPcfRadius = 1;
    float shadowStrength = 0.25f;
    float shadowSoftness = 1.05f;
    float shadowDepthBias = 0.00005f;
    float shadowSlopeBias = 0.00010f;
    float shadowMaxDistance = 1500.0f;

    float waterTintR = 0.035f;
    float waterTintG = 0.135f;
    float waterTintB = 0.265f;
    float waterAlpha = 0.70f;
    float waterReflectionStrength = 0.68f;
    float waterFresnelF0 = 0.025f;

    float ballRoughnessScale = 0.82f;
    float ballSpecularScale = 1.25f;
    float boxRoughnessScale = 1.08f;
    float boxSpecularScale = 0.82f;
};

struct CinematicRenderConfig
{
    // Master switch. The UI can toggle this at runtime; command-line overrides
    // can also force it on/off for quick visual checks.
    bool enabled = false;

    // Individual pass toggles. These let the Cine tab turn pieces of the look on
    // and off without rebuilding the renderer: sky, clouds, shafts, bloom, fog,
    // and the visual-only terrain morph.
    bool skyAtmosphereEnabled = true;
    bool cloudsEnabled = true;
    bool godRaysEnabled = true;
    bool volumetricLightingEnabled = true;
    bool bloomEnabled = true;
    bool fogEnabled = true;
    bool terrainReliefEnabled = true;

    // Final image controls. Exposure is overall brightness before tonemapping;
    // gamma adjusts how the final color is mapped to the monitor.
    float exposure = 0.68f;
    float gamma = 2.05f;

    // Sun position is in screen coordinates, not world coordinates. 0,0 is the
    // bottom-left of the image and 1,1 is the top-right. That makes the reference
    // composition easy to tune.
    float sunScreenX = 0.28f;
    float sunScreenY = 0.76f;

    // Sun/sky colors are deliberately allowed above normal 0..1 color in the
    // config parser. HDR values make bloom and tonemapping feel like hot sunlight.
    float sunColorR = 1.0f;
    float sunColorG = 0.68f;
    float sunColorB = 0.32f;
    float sunIntensity = 22.0f;
    float skyHorizonR = 0.88f;
    float skyHorizonG = 0.34f;
    float skyHorizonB = 0.08f;
    float skyZenithR = 0.26f;
    float skyZenithG = 0.13f;
    float skyZenithB = 0.12f;
    float skyGlowStrength = 2.85f;

    // Procedural cloud controls. Coverage decides how much cloud exists, softness
    // controls edge width, scale changes noise size, and intensity is blend amount.
    float cloudCoverage = 0.66f;
    float cloudSoftness = 0.19f;
    float cloudScale = 5.4f;
    float cloudIntensity = 0.62f;

    // God-ray and volumetric controls. Strength is visible brightness, density is
    // how far each ray marches toward the sun, and decay is how quickly light
    // fades along that march.
    float sunShaftStrength = 2.28f;
    float sunShaftFalloff = 1.92f;
    float volumetricStrength = 1.28f;
    float volumetricDensity = 1.25f;
    float volumetricDecay = 0.955f;

    // Bloom controls. Threshold chooses what is bright enough to glow, knee makes
    // that cutoff soft, strength is glow amount, radius is blur spread.
    float bloomThreshold = 1.05f;
    float bloomKnee = 0.55f;
    float bloomStrength = 0.62f;
    float bloomRadius = 4.2f;

    // Visual-only terrain controls. terrainRelief defaults to 0, so the basin
    // exaggeration is off even though the pass is ready for the slider. These do
    // not change physics or collision data.
    float terrainRelief = 0.0f;
    float basinDepth = 48.0f;
    float basinRimLift = 32.0f;

    // Real shadow-map controls. Shadows are the default path for generated and
    // scene-authored rendering.
    bool shadowsEnabled = true;
    bool shadowTerrainCasts = true;
    bool shadowObjectsCast = true;
    bool shadowTerrainReceives = true;
    bool shadowObjectsReceive = true;
    int shadowMapSize = 2048;
    int shadowPcfRadius = 1;
    float shadowStrength = 0.58f;
    float shadowSoftness = 1.0f;
    float shadowDepthBias = 0.00005f;
    float shadowSlopeBias = 0.00010f;
    float shadowMaxDistance = 1500.0f;

    // Fog/haze controls. Fog is applied from depth in post-processing, so it can
    // make distant terrain and balls disappear into warm sunset air.
    float fogColorR = 0.86f;
    float fogColorG = 0.34f;
    float fogColorB = 0.12f;
    float fogStart = 70.0f;
    float fogEnd = 1550.0f;
    float fogDensity = 0.00145f;
    float fogMaxOpacity = 0.54f;

    // Art-direction presets used by the concept scene pack. These extend the
    // original golden-hour cinematic controls without turning the renderer into a
    // full material graph. Modes are consumed by reusable shaders.
    int skyMode = 0;     // 0=sun sky, 1=industrial, 2=studio, 3=neon, 4=alien, ...
    int terrainMode = 0; // 0=warm terrain, 1=industrial, 2=studio, 3=grid, ...
    int objectStyle = 0; // 0=beach ball, 1=matte, 2=metal, 3=emissive, ...
    int waterMode = 1;   // 0=off/none, 1=basin pool, 2=ocean, 3=wet floor

    float styleSaturation = 1.08f;
    float styleContrast = 1.08f;
    float styleVignette = 0.76f;

    float terrainTintR = 0.78f;
    float terrainTintG = 0.60f;
    float terrainTintB = 0.38f;
    float terrainAccentR = 0.20f;
    float terrainAccentG = 0.09f;
    float terrainAccentB = 0.02f;
    float terrainGridScale = 46.0f;
    float terrainGridStrength = 0.0f;

    float waterTintR = 0.24f;
    float waterTintG = 0.13f;
    float waterTintB = 0.055f;
    float waterAlpha = 0.94f;
    float waterReflectionStrength = 0.22f;
    float waterGlintStrength = 0.28f;

    float basinCenterX = 620.0f;
    float basinCenterZ = 615.0f;
    float basinRadiusX = 205.0f;
    float basinRadiusZ = 145.0f;
    float basinFeather = 0.18f;
};

class SkullbonezConfig
{
  public:
    static SkullbonezConfig& Instance();
    void Load( const char* path );
    void Dump( FILE* out ) const;

    // Asset paths
    std::string skyFront = "sky1.jpg";
    std::string skyLeft = "sky2.jpg";
    std::string skyBack = "sky3.jpg";
    std::string skyRight = "sky4.jpg";
    std::string skyUp = "sky5.jpg";
    std::string skyDown = "sky6.jpg";
    std::string terrainTexture = "ground.jpg";
    std::string sphereTexture = "boundingSphere.jpg";
    std::string terrainRaw = "terrain.raw";

    // Window and rendering flags
    WindowConfig window;
    RuntimeRenderFlags runtimeRender;
    SceneLightConfig sceneLight;
    OrdinaryRenderConfig ordinaryRender;
    CinematicRenderConfig cinematicRender;

    // Frustum
    float frustumNear = 1.0f;
    float frustumFar = 5500.0f;

    // Camera controls
    float mouseSensitivity = 0.2f;
    float keySpeed = 200.0f;
    float cameraTweenRate = 3.0f;
    float cameraCollisionThreshold = 0.01f;
    float minCameraHeight = 1.5f;
    float maxCameraHeight = 110.0f;
    float minViewMag = 2.0f;
    float maxViewMag = 300.0f;

    // Terrain
    float terrainScale = 5.0f;
    float terrainHeightScale = 0.15f;
    int terrainRenderStepSize = 2;

    // Skybox
    float skyboxRenderHeight = 30.0f;
    int skyboxOverflow = 1;
    float skyboxScale = 10.0f;

    // Threading
    int gameModelCapacity = 1024;
    int workerThreads = -1;
    bool physicsParallel = true;
    bool physicsParallelApplyForces = true;
    bool physicsParallelTornadoField = false;
    bool physicsParallelNarrowphase = false;
    bool physicsParallelTerrainDetect = true;
    bool physicsParallelIntegrate = true;
    bool shadowParallelPrep = false;

    // Physics
    float gravity = -30.0f;
    float fluidHeight = 25.0f;
    float fluidDensity = 1.0f;
    float gasDensity = 0.0f;
    float velocityLimit = 5.0f;
    float sphereDragCoeff = 0.4f;
    float fluidAngularDragMultiplier = 2.0f;
    float frictionCoeff = 0.1f;
    float rollingFrictionCoeff = 0.02f;
    float spinFrictionCoeff = 0.3f;
    float contactRestitutionThreshold = 2.0f;
    float contactEpsilon = 0.05f;
    float broadphaseCell = 24.0f;

    // Persistent object/object contact solver tuning. These defaults match the
    // current Catto-style PGS behavior; exposing them in config makes intentional
    // solver experiments reproducible and ensures SkullScope can report the
    // exact policy used for a deterministic diagnostics run.
    float persistentContactSlop = 0.005f;
    float persistentContactBaumgarteBeta = 0.2f;
    float persistentContactPositionCorrectionPercent = 0.35f;
    int persistentContactSolverIterations = 12;

    // Terrain contact tuning. The terrain path uses the shared Catto row math
    // for tangent basis and effective mass, but still has terrain-specific
    // manifold generation and rest-support policy. These fields keep those
    // policy choices visible while the paths continue to converge.
    float terrainContactThreshold = 0.15f;
    float terrainContactSlop = 0.005f;
    float terrainContactBaumgarteBeta = 0.3f;
    float terrainMaxBaumgarteBias = 2.0f;

    // Sleep policy tuning. Sleeping is an engine optimization layered on top of
    // Catto solving, so these thresholds are separate from contact row math.
    // Raising them keeps more bodies awake for diagnostics; lowering them can
    // reduce broadphase/narrowphase work once stacks have settled.
    float physicsSleepLinearSpeed = 0.5f;
    float physicsSleepAngularSpeed = 0.3f;
    int physicsSleepFrames = 30;

    // Shadows
    float shadowMaxHeight = 50.0f;
    float shadowMaxAlpha = 0.8f;
    float shadowOffset = 0.2f;
    float shadowScale = 1.2f;

    // Generated-object spawn ranges
    float spawnXBase = 400.0f;
    int spawnXRange = 400;
    float spawnYBase = 100.0f;
    int spawnYRange = 250;
    float spawnZBase = 400.0f;
    int spawnZRange = 400;
    float ballMassMin = 50.0f;
    int ballMassRange = 50;
    float ballMomentMin = 5.0f;
    int ballMomentRange = 15;
    float ballRestitutionMin = 0.5f;
    int ballRestitutionRange = 5;
    int ballRadiusRange = 10;
    int ballForceRange = 1000;

    // Water
    float oceanWaveHeight = 4.0f;
    float oceanPerturbStrength = 0.002f;

  private:
    SkullbonezConfig() = default;
};

} // namespace Basics
} // namespace SkullbonezCore
