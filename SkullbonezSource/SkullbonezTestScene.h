/* -- INCLUDE GUARDS ----------------------------------------------------------------------------------------------------------------------------------------------------*/
#ifndef SKULLBONEZ_TEST_SCENE_H
#define SKULLBONEZ_TEST_SCENE_H

/* -- INCLUDES ----------------------------------------------------------------------------------------------------------------------------------------------------------*/
#include "SkullbonezCommon.h"
#include "SkullbonezVector3.h"
#include <vector>

/* -- USING CLAUSES -----------------------------------------------------------------------------------------------------------------------------------------------------*/
using namespace SkullbonezCore::Math::Vector;

namespace SkullbonezCore
{
namespace Basics
{
/* -- Scene Camera -----------------------------------------------------------------------------------------------------------------------------------------------*/
struct SceneCamera
{
    char name[64];
    Vector3 m_position;
    Vector3 view;
    Vector3 up;
};

/* -- Scene Ball -------------------------------------------------------------------------------------------------------------------------------------------------*/
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
};

/* -- Test Scene -------------------------------------------------------------------------------------------------------------------------------------------------

    Loads and holds a deterministic scene description from a .scene file.
    Used for render regression testing — provides fixed cameras, fixed ball placements,
    and control over physics and frame count.
-----------------------------------------------------------------------------------------------------------------------------------------------------------------*/
class TestScene
{

  private:
    bool m_isPhysicsEnabled;
    bool m_isTextEnabled;
    bool m_isGlResetTest;       // test GL context recreation
    int m_frameCount;           // -1 = unlimited
    char m_screenshotPath[256]; // output path for screenshot (empty = none)
    int m_screenshotFrame;      // trigger on frame N (-1 = unused)
    int m_screenshotMs;         // trigger at N ms elapsed (-1 = unused)
    unsigned int m_seed;        // RNG m_seed (0 = use time-based default)
    int m_legacyBallCount;      // random legacy-style m_balls (0 = none)
    char m_perfLogPath[256];    // output path for perf CSV (empty = none)
    std::vector<SceneCamera> m_cameras;
    std::vector<SceneBall> m_balls;

  public:
    TestScene();
    static TestScene LoadFromFile( const char* path );

    bool IsPhysicsEnabled() const;
    bool IsTextEnabled() const;
    bool IsGlResetTest() const;
    int GetFrameCount() const;
    const char* GetScreenshotPath() const;
    int GetScreenshotFrame() const;
    int GetScreenshotMs() const;
    unsigned int GetSeed() const;
    int GetLegacyBallCount() const;
    const char* GetPerfLogPath() const;
    int GetCameraCount() const;
    int GetBallCount() const;
    const SceneCamera& GetCamera( int index ) const;
    const SceneBall& GetBall( int index ) const;
};
} // namespace Basics
} // namespace SkullbonezCore

#endif /*----------------------------------------------------------------------------------------------------------------------------------------------------------------*/
