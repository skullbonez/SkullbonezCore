#pragma once


// --- Includes ---
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

class SkullbonezConfig
{
  public:
    static SkullbonezConfig& Instance();
    void Load( const char* path );

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

    // Skybox
    float skyboxRenderHeight = 30.0f;
    int skyboxOverflow = 1;
    float skyboxScale = 10.0f;

    // Physics
    float gravity = -30.0f;
    float fluidHeight = 25.0f;
    float fluidDensity = 1.0f;
    float gasDensity = 0.0f;
    float velocityLimit = 5.0f;
    float sphereDragCoeff = 0.4f;
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
