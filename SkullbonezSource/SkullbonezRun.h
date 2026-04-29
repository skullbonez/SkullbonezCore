#pragma once


// --- Includes ---
#include <memory>
#include <string>
#include <vector>
#include "SkullbonezCommon.h"
#include "SkullbonezCameraCollection.h"
#include "SkullbonezTimer.h"
#include "SkullbonezInput.h"
#include "SkullbonezTextureCollection.h"
#include "SkullbonezWindow.h"
#include "SkullbonezText.h"
#include "SkullbonezTerrain.h"
#include "SkullbonezSkyBox.h"
#include "SkullbonezGeometricMath.h"
#include "SkullbonezGameModelCollection.h"
#include "SkullbonezWorldEnvironment.h"
#include "SkullbonezFramebuffer.h"
#include "SkullbonezTestScene.h"


// --- Usings ---
using namespace SkullbonezCore::Environment;
using namespace SkullbonezCore::Hardware;
using namespace SkullbonezCore::Textures;
using namespace SkullbonezCore::Text;
using namespace SkullbonezCore::Geometry;
using namespace SkullbonezCore::Math;
using namespace SkullbonezCore::GameObjects;


namespace SkullbonezCore
{
namespace Basics
{
/* -- Skullbonez Run ---------------------------------------------------------------------------------------------------------------------------------------------

    Harness for the Skullbonez Core graphics library.
-----------------------------------------------------------------------------------------------------------------------------------------------------------------*/
class SkullbonezRun
{

  private:
    inline static int sPerfPass = 0;
    std::vector<std::string> m_sceneQueue;         // Ordered list of scene paths ("" = legacy mode)
    int m_currentSceneIndex;                       // Index into m_sceneQueue (-1 = not yet loaded)
    bool m_isSceneMode;                            // Scene file mode (deterministic, data-driven)
    bool m_isScenePhysics;                         // Physics enabled in scene mode
    bool m_isSceneText;                            // Text overlay enabled in scene mode
    bool m_isPerfTest;                             // Performance logging mode
    bool m_perfHeaderWritten;                      // CSV header written for current perf run
    bool m_isScreenshotSaved;                      // Screenshot already written this run
    int m_targetFrameCount;                        // Frames to render before holding (-1 = unlimited)
    int m_currentFrame;                            // Current frame counter for scene mode
    int m_screenshotFrame;                         // Save screenshot at this frame (-1 = unused)
    int m_screenshotMs;                            // Save screenshot at this elapsed ms (-1 = unused)
    char m_screenshotPath[256];                    // Output path for screenshot (empty = none)
    char m_perfLogPath[256];                       // Output path for perf CSV (empty = none)
    FILE* m_perfLogFile;                           // Open handle for perf CSV
    int m_selectedCamera;                          // Keeps track of which camera is selected
    int m_modelCount;                              // Number of models in the scene
    float m_physicsTime, m_r_physicsTime;          // Physics time
    float m_renderTime, m_r_renderTime;            // Render time
    float m_r_fpsTime;                             // FPS time
    float m_timeSinceLastRender;                   // Render helper
    float m_cameraTime;                            // Camera helper
    CameraCollection* m_cCameras;                  // SkullbonezCore::Environment::CameraCollection class
    Timer m_cFrameTimer;                           // SkullbonezCore::Environment::Timer class
    Timer m_cWorkTimer;                            // SkullbonezCore::Environment::Timer class
    Timer m_cUpdateTimer;                          // SkullbonezCore::Environment::Timer class
    Timer m_cCameraTimer;                          // SkullbonezCore::Environment::Timer class
    Timer m_cSimulationTimer;                      // SkullbonezCore::Environment::Timer class
    TextureCollection* m_cTextures;                // SkullbonezCore::Textures::TextureCollection class
    SkullbonezWindow* m_cWindow;                   // SkullbonezCore::Basics::SkullbonezWindow class
    std::unique_ptr<Terrain> m_cTerrain;           // SkullbonezCore::Geometry::Terrain class
    SkyBox* m_cSkyBox;                             // SkullbonezCore::Geometry::SkyBox class
    WorldEnvironment m_cWorldEnvironment;          // SkullbonezCore::Environment::WorldEnvironment class
    GameModelCollection m_cGameModelCollection;    // SkullbonezCore::GameObjects::GameModelCollection class
    std::unique_ptr<Framebuffer> m_cReflectionFBO; // Offscreen reflection render target
    InputState m_sInputState;                      // Current frame input state
    bool m_isFlyMode;                              // Free-fly camera mode active (toggle with F)
    bool m_isProfilerOverlay;                      // Profiler overlay visible (toggle with 0; default ON in profile builds)
    bool m_isWaterFreezeDebug;                     // Freeze ocean animation at current shape (toggle with 1)
    bool m_isWaterNoReflect;                       // Disable ocean reflection, output flat tint (toggle with 2)
    bool m_isWaterFlatDebug;                       // Force ocean mesh fully flat, no displacement (toggle with 3)
    float m_frozenWaterTime;                       // Simulation time captured when freeze was toggled on

    void Render();                                                     // Main render method
    void RelativeUpdateCamera( uint32_t hash );                        // Relative update specified camera
    void UpdateLogic( float fSecondsPerFrame );                        // Update world logic
    void TakeInput();                                                  // Take user input
    void SetUpCameras();                                               // Camera init (legacy mode)
    void SetUpCamerasFromScene( const TestScene& scene );              // Camera init from scene file
    void SetUpGameModels( int count );                                 // Game model init (random legacy mode)
    void SetUpGameModelsFromScene( const TestScene& scene );           // Game model init from scene file
    void DrawPrimitives();                                             // Draw OpenGL primitives here
    void SetInitialOpenGlState();                                      // Sets the initial state of the OpenGL evironment
    void SetViewingOrientation();                                      // Renders camera views etc
    void DrawWindowText( const double dSecondsPerFrame );              // Renders text to the window
    void SaveScreenshot( const char* path );                           // Saves framebuffer to BMP file via glReadPixels
    void LogPerfMemory( const char* checkpoint );                      // Log memory usage to perf CSV
    void LoadScene( int index );                                       // Resets scene-specific state and loads a scene by queue index
    bool AdvanceScene();                                               // Advances to the next scene in the queue (returns false if done)
    void MoveCamera( float keyMovementQty, float mouseMovemementQty ); // Moves the camera

  public:
    SkullbonezRun( std::vector<std::string> sceneQueue ); // Constructor (scene queue; empty string = legacy mode)
    ~SkullbonezRun();                                     // Default destructor
    void Initialise();                                    // Initialises shared resources and loads first scene
    void Run();                                           // Runs all scenes in sequence — main message loop
};
} // namespace Basics
} // namespace SkullbonezCore
