#pragma once


// --- Includes ---
#include <string>

namespace SkullbonezCore
{
namespace Basics
{

/*
    Singleton configuration loaded once from SkullbonezData/engine.cfg at startup.
    Access via SkullbonezConfig::Instance().fieldName anywhere SkullbonezCommon.h is included.
    All fields carry defaults matching the original hard-coded values; the config
    file is optional -- if absent, defaults apply.
*/
class SkullbonezConfig
{
  public:
    static SkullbonezConfig& Instance();
    void Load( const char* path );

    // Window
    int screenX = 1200;
    int screenY = 900;
    bool fullscreen = false;
    int bitsPerPixel = 32;
    int refreshRate = 75;

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
    float rollAlignRate = 5.0f;
    float contactRestitutionThreshold = 2.0f;
    float contactEpsilon = 0.05f;
    float broadphaseCell = 11.0f;

    // Shadows
    float shadowMaxHeight = 50.0f;
    float shadowMaxAlpha = 0.5f;
    float shadowOffset = 0.2f;
    int shadowSegments = 16;
    float shadowScale = 1.2f;

    // Ball spawn ranges (legacy random mode)
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

    // Asset paths
    std::string skyFront = "SkullbonezData/sky1.jpg";
    std::string skyLeft = "SkullbonezData/sky2.jpg";
    std::string skyBack = "SkullbonezData/sky3.jpg";
    std::string skyRight = "SkullbonezData/sky4.jpg";
    std::string skyUp = "SkullbonezData/sky5.jpg";
    std::string skyDown = "SkullbonezData/sky6.jpg";
    std::string terrainTexture = "SkullbonezData/ground.jpg";
    std::string sphereTexture = "SkullbonezData/boundingSphere.jpg";
    std::string terrainRaw = "SkullbonezData/terrain.raw";

    // Water
    float oceanWaveHeight = 4.0f;
    float oceanPerturbStrength = 0.002f;

    // Debug / rendering flags
    bool renderCollisionVolumes = false;

  private:
    SkullbonezConfig() = default;
};

} // namespace Basics
} // namespace SkullbonezCore
