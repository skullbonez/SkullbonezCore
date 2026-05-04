#pragma once


// --- Includes ---
#include "SkullbonezCommon.h"
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
    char name[64];
    Vector3 m_position;
    Vector3 view;
    Vector3 up;
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
    int m_frameCount;           // -1 = unlimited
    char m_screenshotPath[256]; // output path for screenshot (empty = none)
    int m_screenshotFrame;      // trigger on frame N (-1 = unused)
    int m_screenshotMs;         // trigger at N ms elapsed (-1 = unused)
    unsigned int m_seed;        // RNG m_seed (0 = use time-based default)
    int m_legacyBallCount;      // random legacy-style m_balls (0 = none)
    char m_perfLogPath[256];    // output path for perf CSV (empty = none)
    char m_physicsLogPath[256]; // output path for physics CSV (empty = none)
    int m_screenshotInterval;   // save screenshot every N frames (-1 = disabled)
    char m_screenshotDir[256];  // output directory for interval captures
    float m_timeScale;          // Physics time multiplier (1.0 = realtime)
    bool m_isDebugVectors;      // Draw velocity/omega debug arrows (default false)
    std::vector<SceneCamera> m_cameras;
    std::vector<SceneBall> m_balls;

  public:
    TestScene();
    static TestScene LoadFromFile( const char* path );

    bool IsPhysicsEnabled() const;
    bool IsTextEnabled() const;
    int GetFrameCount() const;
    const char* GetScreenshotPath() const;
    int GetScreenshotFrame() const;
    int GetScreenshotMs() const;
    unsigned int GetSeed() const;
    int GetLegacyBallCount() const;
    const char* GetPerfLogPath() const;
    const char* GetPhysicsLogPath() const;
    int GetScreenshotInterval() const;
    const char* GetScreenshotDir() const;
    float GetTimeScale() const;
    bool IsDebugVectors() const;
    int GetCameraCount() const;
    int GetBallCount() const;
    const SceneCamera& GetCamera( int index ) const;
    const SceneBall& GetBall( int index ) const;
};
} // namespace Basics
} // namespace SkullbonezCore
