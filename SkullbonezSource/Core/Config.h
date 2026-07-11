/*
File: SkullbonezSource/Core/Config.h
Purpose:
  Loads, stores, and exposes engine configuration values from files and command-line overrides.

Mental model:
  Config.h loads, stores, and exposes engine configuration values from files
  and command-line overrides. As a public header, keep edits anchored on
  process-wide contracts, diagnostics, and validation-sensitive state and on
  the glossary/invariants below.

Glossary:
  PGS (Projected Gauss-Seidel): Iterative constraint-solver method used for
  bounded contact impulses.
  SkullScope: Queryable physics diagnostics workflow backed by bounded trace
  output and local queries.

Invariants:
  - Command-line and scene-file spellings are user-facing compatibility
  surface.

Related:
  - SkullbonezSource/Core/Config.cpp
  - Agentic/Reference/runtime-reference.md
  - Agentic/Reference/comment-style-guide.md
*/
#pragma once

#include "../GameObjects/SceneCapacity.h"

#include <algorithm>
#include <cstdio>
#include <string>

namespace SkullbonezCore
{
namespace Basics
{

/*
    Process configuration loaded once from SkullbonezData/engine.cfg at startup.
    Runtime startup owns the EngineConfig value and threads references or
    snapshots through the composition root. All fields carry defaults matching
    the original hard-coded values; the config file is optional -- if absent,
    defaults apply.
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

struct ContactAudioConfig
{
    bool enabled = true;               // Master startup switch; CLI mute can still force the service off.
    float masterGain = 1.0f;           // Multiplier applied after material/band gain, clamped by the audio service.
    float maxDistanceScale = 1.0f;     // Multiplier for each sound set's authored maxDistance.
    float rollingLevelDb = -24.0f;     // dB; separate quiet roll/slide level.
    float rollingMaxDistance = 24.0f;  // World units; independent of impact distance.
    float rollingMinSlipSpeed = 0.65f; // Pre-solve tangential speed threshold.
    int rollingVoicesPerWindow = 4;    // Per 100 ms; zero disables rolling.
    bool debugCounters = false;        // Prints copied presentation counters once per simulated second.
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

    // Individual pass toggles. God rays controls shaft energy inside the sole
    // half-resolution sun march; volumetric lighting controls whether that pass
    // runs and whether tonemap composites its result. The other toggles control
    // sky, clouds, bloom, fog, and the visual-only terrain morph.
    bool skyAtmosphereEnabled = true;
    bool cloudsEnabled = true;
    bool godRaysEnabled = true;
    bool volumetricLightingEnabled = true;
    bool bloomEnabled = true;
    bool fogEnabled = true;
    bool terrainReliefEnabled = true;

    // Final image controls. Exposure is overall brightness before tonemapping;
    // gamma adjusts how the final color is mapped to the monitor.
    float exposure = 1.02f;
    float gamma = 1.50f;

    // Legacy key names keep scene/config compatibility, but these are now
    // normalized world-sky controls: X is azimuth, Y is elevation.
    float sunScreenX = 0.50f;
    float sunScreenY = 0.55f;

    // Sun/sky colors are deliberately allowed above normal 0..1 color in the
    // config parser. HDR values make bloom and tonemapping feel like hot sunlight.
    float sunColorR = 1.56f;
    float sunColorG = 0.85f;
    float sunColorB = 0.42f;
    float sunIntensity = 11.2f;
    float skyHorizonR = 1.18f;
    float skyHorizonG = 0.74f;
    float skyHorizonB = 0.68f;
    float skyZenithR = 0.16f;
    float skyZenithG = 0.78f;
    float skyZenithB = 1.58f;
    float skyGlowStrength = 0.52f;

    // Procedural cloud controls. Coverage decides how much cloud exists, softness
    // controls edge width, scale changes noise size, and intensity is blend amount.
    float cloudCoverage = 0.56f;
    float cloudSoftness = 0.16f;
    float cloudScale = 5.2f;
    float cloudIntensity = 1.08f;

    // God-ray and volumetric controls all feed the half-resolution volumetric
    // pass. Shaft strength/falloff shape its brightness and radial reach;
    // volumetric strength scales the completed shaft texture, density controls
    // march distance, and decay controls how quickly samples fade along it.
    float sunShaftStrength = 0.34f;
    float sunShaftFalloff = 2.40f;
    float volumetricStrength = 0.12f;
    float volumetricDensity = 0.65f;
    float volumetricDecay = 0.955f;

    // Bloom controls. Threshold chooses what is bright enough to glow, knee makes
    // that cutoff soft, strength is glow amount, radius is blur spread.
    float bloomThreshold = 1.08f;
    float bloomKnee = 0.55f;
    float bloomStrength = 0.24f;
    float bloomRadius = 3.0f;

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
    float fogColorR = 0.82f;
    float fogColorG = 0.66f;
    float fogColorB = 0.58f;
    float fogStart = 360.0f;
    float fogEnd = 1760.0f;
    float fogDensity = 0.00038f;
    float fogMaxOpacity = 0.14f;

    // Art-direction presets used by the concept scene pack. These extend the
    // original golden-hour cinematic controls without turning the renderer into a
    // full material graph. Modes are consumed by reusable shaders.
    int skyMode = 11;                  // 0=sun sky, 1=industrial, 2=studio, 3=neon, 4=alien, ...
    int terrainMode = 7;               // 0=warm terrain, 1=industrial, 2=studio, 3=grid, ...
    int objectStyle = 6;               // 0=beach ball, 1=matte, 2=metal, 3=emissive, ...
    int waterMode = 4;                 // 0=off/none, 1=basin pool, 2=ocean, 3=wet floor

    float styleSaturation = 1.44f;
    float styleContrast = 1.34f;
    float styleVignette = 0.34f;

    float terrainTintR = 0.34f;
    float terrainTintG = 0.54f;
    float terrainTintB = 0.16f;
    float terrainAccentR = 0.06f;
    float terrainAccentG = 0.18f;
    float terrainAccentB = 0.045f;
    float terrainGridScale = 46.0f;
    float terrainGridStrength = 0.0f;

    float waterTintR = 0.018f;
    float waterTintG = 0.13f;
    float waterTintB = 0.18f;
    float waterAlpha = 0.92f;
    float waterReflectionStrength = 0.28f;
    float waterGlintStrength = 1.18f;

    float basinCenterX = 620.0f;
    float basinCenterZ = 650.0f;
    float basinRadiusX = 250.0f;
    float basinRadiusZ = 170.0f;
    float basinFeather = 0.16f;
};

class EngineConfig
{
  public:
    EngineConfig() = default;
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
    ContactAudioConfig contactAudio;
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
    int gameModelCapacity = 4000;
    int workerThreads = -1;
    bool physicsParallel = true;
    bool physicsParallelApplyForces = true;
    bool physicsParallelTornadoField = false;
    bool physicsParallelNarrowphase = false;
    bool physicsParallelTerrainDetect = true;
    bool physicsParallelIntegrate = true;
    bool shadowParallelPrep = false;

    // Replay prediction measures private-engine throughput before choosing a
    // scheduling mode. A zero budget keeps the legacy amortized path.
    float replayPredictionInstantBudgetMs = 30.0f;
    int replayPredictionProbeTicks = 50;

    // Physics
    float gravity = -30.0f;
    float fluidHeight = 25.0f;
    float fluidDensity = 1.0f;
    float gasDensity = 0.0f;
    float velocityLimit = 5.0f;
    float sphereDragCoeff = 0.4f;
    float fluidAngularDragMultiplier = 2.0f;
    float frictionCoeff = 0.1f;
    float objectFrictionCoeff = 0.1f;
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
};

inline int ActiveGameModelCapacity( const EngineConfig& config )
{
    return std::clamp( config.gameModelCapacity, 1, MAX_GAME_MODELS );
}

} // namespace Basics
} // namespace SkullbonezCore
