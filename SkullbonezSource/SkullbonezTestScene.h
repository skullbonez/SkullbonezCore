/*------------------------------------------------------------------------------------------------------------------------------------------------------------------------
                                                                          THE SKULLBONEZ CORE
                                                                                _______
                                                                             .-"       "-.
                                                                            /             \
                                                                           /               \
                                                                           ?   .--. .--.   ?
                                                                           ? )/   ? ?   \( ?
                                                                           ?/ \__/   \__/ \?
                                                                           /      /^\      \
                                                                           \__    '='    __/
                                                                             ?\         /?
                                                                             ?\'"VUUUV"'/?
                                                                             \ `"""""""` /
                                                                              `-._____.-'

                                                                         www.simoneschbach.com
-------------------------------------------------------------------------------------------------------------------------------------------------------------------------*/

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
    Vector3 position;
    Vector3 view;
    Vector3 up;
};

/* -- Scene Ball -------------------------------------------------------------------------------------------------------------------------------------------------*/
struct SceneBall
{
    char name[64];
    float posX, posY, posZ;
    float radius;
    float mass;
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
    bool isPhysicsEnabled;
    bool isTextEnabled;
    bool isGlResetTest;       // test GL context recreation
    int frameCount;           // -1 = unlimited
    char screenshotPath[256]; // output path for screenshot (empty = none)
    int screenshotFrame;      // trigger on frame N (-1 = unused)
    int screenshotMs;         // trigger at N ms elapsed (-1 = unused)
    unsigned int seed;        // RNG seed (0 = use time-based default)
    int legacyBallCount;      // random legacy-style balls (0 = none)
    char perfLogPath[256];    // output path for perf CSV (empty = none)
    std::vector<SceneCamera> cameras;
    std::vector<SceneBall> balls;

  public:
    TestScene( void );
    static TestScene LoadFromFile( const char* path );

    bool IsPhysicsEnabled( void ) const;
    bool IsTextEnabled( void ) const;
    bool IsGlResetTest( void ) const;
    int GetFrameCount( void ) const;
    const char* GetScreenshotPath( void ) const;
    int GetScreenshotFrame( void ) const;
    int GetScreenshotMs( void ) const;
    unsigned int GetSeed( void ) const;
    int GetLegacyBallCount( void ) const;
    const char* GetPerfLogPath( void ) const;
    int GetCameraCount( void ) const;
    int GetBallCount( void ) const;
    const SceneCamera& GetCamera( int index ) const;
    const SceneBall& GetBall( int index ) const;
};
} // namespace Basics
} // namespace SkullbonezCore

#endif /*----------------------------------------------------------------------------------------------------------------------------------------------------------------*/
