/*
File: SkullbonezSource/Core/Config.h
Purpose:
  Loads, stores, and exposes engine configuration values from files and command-line overrides.

Summary:
  Loads, stores, and exposes engine configuration
  values from files and command-line overrides.

Glossary:
  Domain config: Narrow value structure whose fields share one concrete runtime
    owner, such as camera navigation or replay prediction scheduling.
  Parallel mutual gravity: Execution policy for building exact pair forces on
    workers before model-order accumulation on the physics owner thread.

Invariants:
  - Command-line and scene-file spellings are user-facing compatibility
  surface.
  - Moving a setting into a domain struct must preserve its key, default,
    accepted range, dump position, and every validation-sensitive consumer.
  - `terrainRaw` selects both render and collision geometry; an asset-path move
    must still receive physics validation at the formal gate.
  - Format version 6 removes the render-only terrain sampling setting. Version-5
    files remain readable while the cold migration tool deletes that row.

Related:
  - SkullbonezSource/Core/Config.cpp
  - Agentic/Reference/runtime-reference.md
  - Agentic/Reference/comment-style-guide.md
  - Agentic/Reference/engine-glossary.md
*/
#pragma once

#include "SceneCapacity.h"
#include "SbResult.h"

#include <algorithm>
#include <cstdio>
#include <string>

namespace SkullbonezCore
{
namespace Core
{

inline constexpr unsigned int ENGINE_CONFIG_FORMAT_VERSION = 6;

/*
    Process configuration loaded once from SkullbonezData/engine.cfg at startup.
    Runtime startup owns the EngineConfig value and threads references or
    snapshots through the composition root. All fields carry defaults matching
    the original hard-coded values; the config file is optional -- if absent,
    defaults apply.
*/

// Window/Init owns startup display creation; runtime code reads this value but
// does not mutate it into a second display-policy store.
struct WindowConfig
{
    int screenX = 1800;
    int screenY = 1000;
    bool fullscreen = false;
    int bitsPerPixel = 32;
    int refreshRate = 75;
};

// RuntimeRenderer owns these live presentation switches and copies startup
// values from EngineConfig before the frame loop.
struct RuntimeRenderFlags
{
    bool vsyncEnabled = true;
    bool forcePipelineSync = false;
    bool renderCollisionVolumes = false;
    bool shadowParallelPrep = false;       // Render-only worker scheduling; physics parallelism has a separate owner.
    bool presentationInterpolation = true; // Live rendering blends fixed-tick poses; captures still pin exact state.
};

// Cold source paths resolved by AssetSystem or terrain construction before
// steady gameplay. The terrain heightfield is also collision geometry, so its
// spelling is physics-validation-sensitive even though this value only owns a path.
struct AssetPathsConfig
{
    std::string skyFront = "sky1.jpg";
    std::string skyLeft = "sky2.jpg";
    std::string skyBack = "sky3.jpg";
    std::string skyRight = "sky4.jpg";
    std::string skyUp = "sky5.jpg";
    std::string skyDown = "sky6.jpg";
    std::string terrainTexture = "ground.jpg";
    std::string sphereTexture = "boundingSphere.jpg";
    std::string terrainRaw = "terrain.raw";
};

// Camera-owned projection and navigation policy. Units are world units except
// mouseSensitivity and cameraCollisionThreshold, which scale angular input.
struct CameraConfig
{
    float frustumNear = 1.0f;
    float frustumFar = 5500.0f;
    float mouseSensitivity = 0.2f;
    float keySpeed = 200.0f;
    float cameraTweenRate = 3.0f;
    float cameraCollisionThreshold = 0.01f;
    float minCameraHeight = 1.5f;
    float maxCameraHeight = 110.0f;
    float minViewMag = 2.0f;
    float maxViewMag = 300.0f;
};

// Skybox presentation values consumed by SkyBox geometry and the render pass.
struct SkyboxConfig
{
    float renderHeight = 30.0f;
    int overflow = 1;
    float scale = 10.0f;
};

// Startup resource bounds. Scene overrides may replace these values, but the
// compiled model maximum and WorkerPool maximum remain the hard caps.
struct RuntimeCapacityConfig
{
    int sceneObjectCapacity = 4000;
    int workerThreads = -1;                // -1 = auto, 0 = disabled, positive = explicit worker count.
};

// Private replay-prediction scheduling policy. These values never alter the
// authoritative solver; they select instant or amortized private-engine work.
struct ReplayPredictionConfig
{
    float instantBudgetMs = 30.0f;
    int probeTicks = 50;
};

// Parser/dump compatibility owner for the retired projected blob-shadow path.
// No runtime renderer currently consumes these values; live shadow maps use
// ShadowQualityConfig inside ordinaryRender and cinematicRender.
struct BlobShadowConfig
{
    float maxHeight = 50.0f;
    float maxAlpha = 0.8f;
    float offset = 0.2f;
    float scale = 1.2f;
};

// Visual ocean-wave parameters copied into WorldEnvironment's bound render
// style. They do not move the physics fluid surface or affect buoyancy.
struct WaterRenderStyleSettings
{
    float oceanWaveHeight = 4.0f;
    float oceanPerturbStrength = 0.002f;
};

// World-level gravity and fluid inputs copied into WorldEnvironment before
// simulation. Every value affects deterministic force integration.
struct WorldForceConfig
{
    float gravity = -30.0f;
    float fluidHeight = 25.0f;
    float fluidDensity = 1.0f;
    float gasDensity = 0.0f;
    float fluidAngularDragMultiplier = 2.0f;
};

// Per-body limits and contact skin/bounce policy stamped at the physics owner
// boundary. These values must remain bit-identical across deterministic runs.
struct BodySimulationPolicyConfig
{
    float velocityLimit = 5.0f;
    float contactRestitutionThreshold = 2.0f;
    float contactEpsilon = 0.05f;
};

// Material response shared by authored body policy, contact solving, UI
// tuning, and diagnostics. Coefficients are dimensionless.
struct PhysicsMaterialConfig
{
    float sphereDragCoeff = 0.4f;
    float frictionCoeff = 0.1f;
    float objectFrictionCoeff = 0.1f;
    float rollingFrictionCoeff = 0.02f;
    float spinFrictionCoeff = 0.3f;
};

// Broadphase grid policy. Cell size is in world units and changes both
// candidate-pair cost and collision coverage, so it also requires perf proof.
struct BroadphaseConfig
{
    float cellSize = 24.0f;
};

// Object/object Projected Gauss-Seidel contact policy. Iteration order and
// correction constants are part of the byte-exact deterministic baseline.
struct PersistentContactSolverConfig
{
    float slop = 0.005f;
    float baumgarteBeta = 0.2f;
    float positionCorrectionPercent = 0.35f;
    int iterations = 12;
};

// Terrain-specific contact generation and correction policy. It stays
// separate because terrain manifolds have different support/rest behavior.
struct TerrainContactConfig
{
    float threshold = 0.15f;
    float slop = 0.005f;
    float baumgarteBeta = 0.3f;
    float maxBaumgarteBias = 2.0f;
};

// Physics sleep thresholds and consecutive-frame requirement. These values
// decide when bodies leave active solver work and therefore affect determinism.
struct PhysicsSleepConfig
{
    float linearSpeed = 0.5f;
    float angularSpeed = 0.3f;
    int frames = 30;
};

// Worker-lane switches owned by physics execution. The master flag must gate
// every child lane so command-line serial mode remains a complete override.
struct PhysicsExecutionConfig
{
    bool parallel = true;
    bool parallelApplyForces = true;
    bool parallelMutualGravity = true;
    bool parallelExternalForceFields = false;
    bool parallelNarrowphase = false;
    bool parallelTerrainDetect = true;
    bool parallelIntegrate = true;
};

// Heightfield scale shared by rendered terrain and collision queries. Both
// values require renderer and physics proof.
struct TerrainGeometryConfig
{
    float scale = 5.0f;
    float heightScale = 0.15f;
};

// Deterministic ranges used only by generated-scene construction. Random draws
// and their order remain unchanged; moving these values must not alter seeds.
struct GeneratedSceneConfig
{
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
};

// RuntimeRenderer owns this scene-light presentation value and publishes it to
// the render passes that shade the active scene.
struct SceneLightConfig
{
    float colorR = 1.0f;
    float colorG = 0.5f;
    float colorB = 0.5f;
    float colorA = 1.0f;
};

// Value owner for one renderer profile's shadow-map policy. Ordinary and
// cinematic rendering choose different strength/softness defaults, while the
// field meanings, limits, and downstream frame contract remain identical.
struct ShadowQualityConfig
{
    bool enabled;
    bool terrainCasts;
    bool objectsCast;
    bool terrainReceives;
    bool objectsReceive;
    int mapSize;
    int pcfRadius;
    float strength;
    float softness;
    float depthBias;
    float slopeBias;
    float maxDistance;
};

inline constexpr ShadowQualityConfig MakeShadowQualityConfig( float strength, float softness )
{
    return { true, true, true, true, true, 2048, 1, strength, softness, 0.00005f, 0.00010f, 1500.0f };
}

// Concept: Replay trajectory appearance is presentation policy, independent of
// the prediction horizon and physics state that produced each sampled point.
struct ReplayTrajectoryAppearanceConfig
{
    float futureWidth = 1.25f;
    float futureAlpha = 1.0f;
    float futureEdgeFeather = 1.0f;
    float causalWidth = 1.25f;
    float causalAlpha = 1.0f;
    float causalEdgeFeather = 1.0f;
    float baselineWidth = 1.0f;
    float baselineAlpha = 1.0f;
    float baselineEdgeFeather = 1.0f;
    float markerWidth = 1.5f;
    float markerAlpha = 1.0f;
    float markerEdgeFeather = 1.0f;
    float selectedEmphasis = 0.45f;
};

// RuntimeRenderer owns the ordinary render profile; its pass/shader owners
// consume these lighting, shadow, water, and material values each frame.
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

    ShadowQualityConfig shadow = MakeShadowQualityConfig( 0.25f, 1.05f );

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

    ReplayTrajectoryAppearanceConfig replayTrajectory;
};

// Style-mode value table shared by config defaults, authored scene/style data,
// UI sliders, and shader uniforms. Names either match tracked style assets or
// describe behavior implemented directly by the owning shader. Sky value 14 is
// currently unassigned; sky values 22..32 remain parser/UI-compatible generic
// fallbacks but have no tracked authored users or distinct shader behavior.
namespace CinematicStyleMode
{
namespace Sky
{
inline constexpr int SunSky = 0;
inline constexpr int Industrial = 1;
inline constexpr int Studio = 2;
inline constexpr int NeonCyberpunk = 3;
inline constexpr int AlienPlanet = 4;
inline constexpr int DesertStorm = 5;
inline constexpr int Painterly = 6;
inline constexpr int RetroFuture = 7;
inline constexpr int AtmosphericFog = 8;
inline constexpr int OceanWorld = 9;
inline constexpr int SciFiTestChamber = 10;
inline constexpr int LowPolyArt = 11;
inline constexpr int MassiveScale = 12;
inline constexpr int StormFront = 13;
inline constexpr int TronGrid = 15;
inline constexpr int Dreamscape = 16;
inline constexpr int NordicWinter = 17;
inline constexpr int AbstractRender = 18;
inline constexpr int PixarInspired = 19;
inline constexpr int OpenHorizon = 20;
inline constexpr int DeepSpace = 21;
} // namespace Sky

namespace Terrain
{
inline constexpr int TexturedWarm = 0;
inline constexpr int WornIndustrial = 1;
inline constexpr int PaleStudio = 2;
inline constexpr int NeonGrid = 3;
inline constexpr int AlienVeins = 4;
inline constexpr int DesertSlope = 5;
inline constexpr int Posterized = 6;
inline constexpr int LowPolyBasin = 7;
inline constexpr int DarkNeutral = 8;
inline constexpr int CoolStone = 9;
inline constexpr int SciFiGrid = 10;
inline constexpr int NordicSnow = 11;
inline constexpr int Photogrammetry = 12;
inline constexpr int ChromaticBands = 13;
inline constexpr int SoftIllustrated = 14;
inline constexpr int SolidStudio = 15;
} // namespace Terrain

namespace Object
{
inline constexpr int BeachBall = 0;
inline constexpr int Matte = 1;
inline constexpr int Metal = 2;
inline constexpr int Emissive = 3;
inline constexpr int Fresnel = 4;
inline constexpr int ToonBands = 5;
inline constexpr int LowPoly = 6;
inline constexpr int DarkRim = 7;
inline constexpr int Foliage = 8;
inline constexpr int Bark = 9;
inline constexpr int Stone = 10;
inline constexpr int Ridge = 11;
inline constexpr int Sand = 12;
inline constexpr int PineNeedles = 13;
} // namespace Object

namespace Water
{
inline constexpr int Off = 0;
inline constexpr int Basin = 1;
inline constexpr int Ocean = 2;
inline constexpr int WetFloor = 3;
inline constexpr int StylizedBasin = 4;
} // namespace Water
} // namespace CinematicStyleMode

// RuntimeRenderer owns the cinematic render profile and distributes these
// values to the sky, volumetric, bloom, fog, shadow, and style passes.
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

    // Normalized world-sky angles. Legacy config/scene spellings
    // `sunScreenX`/`sunScreenY` remain aliases at their parsing boundaries.
    float sunAzimuth = 0.50f;
    float sunElevation = 0.55f;

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

    // Shadows are the default path for generated and scene-authored rendering.
    ShadowQualityConfig shadow = MakeShadowQualityConfig( 0.58f, 1.0f );

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
    // Value tables live in CinematicStyleMode above. Sky uses authored values
    // 0..13 and 15..20; terrain uses 0..15; top-level object styles use 0..7
    // while shader material kinds extend through 13; water uses 0..4. Unknown
    // sky/terrain/object values retain the shaders' generic fallback.
    int skyMode = CinematicStyleMode::Sky::LowPolyArt;
    int terrainMode = CinematicStyleMode::Terrain::LowPolyBasin;
    int objectStyle = CinematicStyleMode::Object::LowPoly;
    int waterMode = CinematicStyleMode::Water::StylizedBasin;

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

    // Lane R: authored configuration is preflighted before any destination is
    // mutated, so an unsupported format cannot leave a partially loaded config.
    SbResult Load( SbDiagnosticStore& diagnostics, const char* path );
    void Dump( FILE* out ) const;

    // Composition invariant: parser rows retain historical order and key
    // spellings while consumers select the narrow domain value they own.
    AssetPathsConfig assetPaths;
    WindowConfig window;
    CameraConfig camera;
    SkyboxConfig skybox;
    RuntimeCapacityConfig runtimeCapacity;
    RuntimeRenderFlags runtimeRender;
    ReplayPredictionConfig replayPrediction;
    BlobShadowConfig blobShadow;
    WaterRenderStyleSettings waterRenderStyle;
    WorldForceConfig worldForces;
    BodySimulationPolicyConfig bodySimulation;
    PhysicsMaterialConfig physicsMaterial;
    BroadphaseConfig broadphase;
    PersistentContactSolverConfig persistentContactSolver;
    TerrainContactConfig terrainContact;
    PhysicsSleepConfig physicsSleep;
    PhysicsExecutionConfig physicsExecution;
    TerrainGeometryConfig terrainGeometry;
    GeneratedSceneConfig generatedScene;
    SceneLightConfig sceneLight;
    OrdinaryRenderConfig ordinaryRender;
    CinematicRenderConfig cinematicRender;
};

inline int ActiveSceneObjectCapacity( const EngineConfig& config )
{
    return std::clamp( config.runtimeCapacity.sceneObjectCapacity, 1, SkullbonezCore::Scene::Capacity::MAX_SCENE_OBJECTS );
}

} // namespace Core
} // namespace SkullbonezCore
