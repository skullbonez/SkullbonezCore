/*-----------------------------------------------------------------------------------
                                  THE SKULLBONEZ CORE
                                        _______
                                     .-"       "-.
                                    /             \
                                   /               \
                                   |   .--. .--.   |
                                   | )/   | |   \( |
                                   |/ \__/   \__/ \|
                                   /      /^\      \
                                   \__    '='    __/
                                     |\         /|
                                     |\'"VUUUV"'/|
                                     \ `"""""""` /
                                      `-._____.-'

                                 www.simoneschbach.com
-----------------------------------------------------------------------------------*/

/* -- INCLUDES --------------------------------------------------------------------*/
#include "SkullbonezRun.h"
#include "SkullbonezHelper.h"
#include "SkullbonezBoundingSphere.h"
#include "SkullbonezGameModel.h"
#include "SkullbonezProfiler.h"
#include <time.h>
#include <cstring>
#include <psapi.h>

/* -- USING CLAUSES ---------------------------------------------------------------*/
using namespace SkullbonezCore::Basics;
using namespace SkullbonezCore::Math::CollisionDetection;

/* -- STATIC MEMBER INITIALISATION ------------------------------------------------*/
int SkullbonezRun::sGlResetPass = 0;
int SkullbonezRun::sPerfPass = 0;

/* -- DEFAULT CONSTRUCTOR ---------------------------------------------------------*/
SkullbonezRun::SkullbonezRun( const char* pScenePath )
{
    // init scene mode
    m_isSceneMode = ( pScenePath != nullptr );
    m_isScenePhysics = true;
    m_isSceneText = true;
    m_isGlResetTest = false;
    m_isPerfTest = false;
    m_perfHeaderWritten = false;
    m_isScreenshotSaved = false;
    m_targetFrameCount = -1;
    m_currentFrame = 0;
    m_screenshotFrame = -1;
    m_screenshotMs = -1;
    m_screenshotPath[0] = '\0';
    m_perfLogPath[0] = '\0';
    m_perfLogFile = nullptr;

    if ( pScenePath )
    {
        strcpy_s( m_scenePath, sizeof( m_scenePath ), pScenePath );
    }
    else
    {
        m_scenePath[0] = '\0';
    }

    // init members
    m_cCameras = 0;
    m_cTextures = 0;
    m_cSkyBox = 0;
    m_selectedCamera = 0;
    m_timeSinceLastRender = 0.0f;
    m_renderTime = 0.0f;
    m_cameraTime = 0.0f;
    m_r_renderTime = 0.0f;
    m_physicsTime = 0.0f;
    m_r_physicsTime = 0.0f;
    m_r_fpsTime = 0.0f;
    m_isFlyMode = false;
    m_isProfilerOverlay = true;
    m_isWaterFreezeDebug = false;
    m_isWaterNoReflect = false;
    m_isWaterFlatDebug = false;
    m_frozenWaterTime = 0.0f;
    m_sInputState = {};

    // m_seed the random number generator
    srand( (unsigned)time( NULL ) );
}

/* -- DEFAULT DESTRUCTOR ----------------------------------------------------------*/
SkullbonezRun::~SkullbonezRun( void )
{
    if ( m_perfLogFile )
    {
        fclose( m_perfLogFile );
        m_perfLogFile = nullptr;
    }

    // Clean up GL resources while context is still alive
    SkullbonezHelper::ResetGLResources();
    m_cWorldEnvironment.ResetGLResources();
    m_cGameModelCollection.ResetGLResources();
    if ( m_cReflectionFBO )
    {
        m_cReflectionFBO->ResetGLResources();
    }
    Text2d::DeleteFont();

    m_cTextures->Destroy();
    m_cCameras->Destroy();
    m_cSkyBox->Destroy();
}

/* -- INITIALISE ------------------------------------------------------------------*/
void SkullbonezRun::Initialise( void )
{
    // Init window
    m_cWindow = SkullbonezWindow::Instance();

    // Set loading text
    m_cWindow->SetTitleText( "::SKULLBONEZ CORE:: -- LOADING!!!" );

    // Init m_textures
    m_cTextures = TextureCollection::Instance();

    // Init OpenGL
    this->SetInitialOpenGlState();

    // Init m_terrain
    // path to m_height map | map size pixels | step size | times to wrap texture
    m_cTerrain = std::make_unique<Terrain>( Cfg().terrainRaw.c_str(), 256, 8, 15 );

    // Init SkyBox (m_xMin, m_xMax, yMin, yMax, m_zMin, m_zMax)
    m_cSkyBox = SkyBox::Instance( -250, 300, -300, 300, -250, 300 );
    m_cSkyBox->ResetGLResources();

    // Init world environment
    {
        const SkullbonezConfig& cfg = Cfg();
        m_cWorldEnvironment = WorldEnvironment( cfg.fluidHeight, cfg.fluidDensity, cfg.gasDensity, cfg.gravity );
        XZBounds tb = m_cTerrain->GetXZBounds();
        m_cWorldEnvironment.SetTerrainBounds( tb.m_xMin, tb.m_xMax, tb.m_zMin, tb.m_zMax );
    }

    // Init reflection FBO at the current viewport size so it matches the main render
    // regardless of windowed vs fullscreen resolution
    GLint vp[4];
    glGetIntegerv( GL_VIEWPORT, vp );
    m_cReflectionFBO = std::make_unique<Framebuffer>( vp[2] * 2, vp[3] * 2 );

    // Branch on scene mode vs legacy mode
    if ( m_isSceneMode )
    {
        TestScene scene = TestScene::LoadFromFile( m_scenePath );
        m_isScenePhysics = scene.IsPhysicsEnabled();
        m_isSceneText = scene.IsTextEnabled();
        m_targetFrameCount = scene.GetFrameCount();
        m_screenshotFrame = scene.GetScreenshotFrame();
        m_screenshotMs = scene.GetScreenshotMs();

        if ( scene.GetScreenshotPath()[0] != '\0' )
        {
            strcpy_s( m_screenshotPath, sizeof( m_screenshotPath ), scene.GetScreenshotPath() );

            // On pass 2 of GL reset test, modify screenshot path to add _reset suffix
            if ( scene.IsGlResetTest() && sGlResetPass > 0 )
            {
                char* dot = strrchr( m_screenshotPath, '.' );
                if ( dot )
                {
                    char ext[32];
                    strcpy_s( ext, sizeof( ext ), dot );
                    strcpy_s( dot, sizeof( m_screenshotPath ) - ( dot - m_screenshotPath ), "_reset" );
                    strcat_s( m_screenshotPath, sizeof( m_screenshotPath ), ext );
                }
            }
        }

        m_isGlResetTest = scene.IsGlResetTest();

        // Perf test: open CSV log file
        const char* pPerfPath = scene.GetPerfLogPath();
        if ( pPerfPath[0] != '\0' )
        {
            m_isPerfTest = true;
            strcpy_s( m_perfLogPath, sizeof( m_perfLogPath ), pPerfPath );
            const char* mode = ( sPerfPass == 0 ) ? "w" : "a";
            fopen_s( &m_perfLogFile, m_perfLogPath, mode );
            if ( m_perfLogFile )
            {
                this->LogPerfMemory( "start" );
            }
        }

        // Override RNG m_seed for deterministic scenes
        if ( scene.GetSeed() > 0 )
        {
            srand( scene.GetSeed() );
        }

        this->SetUpCamerasFromScene( scene );

        // Use legacy random ball generation or explicit ball list
        if ( scene.GetLegacyBallCount() > 0 )
        {
            this->SetUpGameModels( scene.GetLegacyBallCount() );
        }
        else
        {
            this->SetUpGameModelsFromScene( scene );
        }
    }
    else
    {
        this->SetUpCameras();
        this->SetUpGameModels();
    }

    // Init font (HDC, font)
    Text2d::BuildFont( m_cWindow->m_sDevice, "Verdana" );

    // Restore initial window text
    if ( m_isSceneMode )
    {
        m_cWindow->SetTitleText( "::SKULLBONEZ CORE:: [SCENE MODE]" );
    }
    else
    {
        m_cWindow->SetTitleText( "::SKULLBONEZ CORE::" );
    }

    // begin timing
    m_cUpdateTimer.StartTimer();
    m_cCameraTimer.StartTimer();
    m_cSimulationTimer.StartTimer();
}

/* -- SET UP GAME MODELS ----------------------------------------------------------*/
void SkullbonezRun::SetUpGameModels( int count )
{
    m_modelCount = count;

    const SkullbonezConfig& cfg = Cfg();

    auto randFloat = [&]( float base, int range )
    { return base + (float)( rand() % range ); };
    auto randSigned = [&]( int range ) -> float
    {
        float mag = 1.0f + (float)( rand() % range );
        return ( rand() % 2 == 0 ) ? mag : -mag;
    };
    auto randSign = []() -> float
    { return ( rand() % 2 == 0 ) ? 1.0f : -1.0f; };

    for ( int x = 0; x < m_modelCount; ++x )
    {
        float posX = randFloat( cfg.spawnXBase, cfg.spawnXRange );
        float posY = randFloat( cfg.spawnYBase, cfg.spawnYRange );
        float posZ = randFloat( cfg.spawnZBase, cfg.spawnZRange );
        float mass = randFloat( cfg.ballMassMin, cfg.ballMassRange );
        float moment = randFloat( cfg.ballMomentMin, cfg.ballMomentRange );
        float restitution = cfg.ballRestitutionMin + (float)( rand() % cfg.ballRestitutionRange ) / 10.0f;
        float radius = ( 1.0f + (float)( rand() % cfg.ballRadiusRange ) ) * 0.5f;
        Vector3 force( randSigned( cfg.ballForceRange ), randSigned( cfg.ballForceRange ), randSigned( cfg.ballForceRange ) );
        Vector3 forcePos( randSign(), randSign(), randSign() );

        GameModel gameModel( &m_cWorldEnvironment, Vector3( posX, posY, posZ ), Vector3( moment, moment, moment ), mass );
        gameModel.SetCoefficientRestitution( restitution );
        gameModel.SetTerrain( m_cTerrain.get() );
        gameModel.AddBoundingSphere( radius );
        gameModel.SetImpulseForce( force, forcePos );

        m_cGameModelCollection.AddGameModel( std::move( gameModel ) );
    }
}

/* -- RUN -------------------------------------------------------------------------*/
bool SkullbonezRun::Run( void )
{
    /*
        Runs the application after initialisation - main message loop.

        Note:   To make it so the application only invalidates on events
                (mouse movements etc) place WaitMessage() after input,
                logic and render.
    */

    // Variable to hold messages from message queue
    MSG msg;

    for ( ;; ) // Do until application is closed
    {
        if ( PeekMessage( &msg, NULL, 0, 0, PM_REMOVE ) ) // Check for msg
        {
            if ( msg.message == WM_QUIT )
            {
                break; // Quit if requested
            }
            TranslateMessage( &msg ); // Interpret msg
            DispatchMessage( &msg );  // Execute msg
        }
        else
        {
            // find out how many seconds passed during last frame
            double secondsPerFrame = m_cFrameTimer.GetElapsedTime();

            // if the last frame took an exceptionally long time,
            // cap the time step to avoid making the simulation inaccurate
            // and to avoid numerical instability
            if ( secondsPerFrame > 0.05 )
            {
                secondsPerFrame = 0.05;
            }

            // Begin timer
            m_cFrameTimer.StartTimer();

            // Begin profiler frame (implicit "Frame" marker brackets everything below)
            PROFILE_FRAME_BEGIN();

            // Input
            PROFILE_BEGIN( "Frame/Input" );
            this->TakeInput();
            PROFILE_END( "Frame/Input" );

            // Logic (skip physics in scene mode when disabled)
            if ( !m_isSceneMode || m_isScenePhysics )
            {
                this->UpdateLogic( (float)secondsPerFrame );
            }

            // Render
            PROFILE_BEGIN( "Frame/Render" );
            this->Render();
            PROFILE_END( "Frame/Render" );

            // Render overlay text
            if ( !m_isSceneMode || m_isSceneText )
            {
                PROFILE_BEGIN( "Frame/Text" );
                this->DrawWindowText( secondsPerFrame );
                PROFILE_END( "Frame/Text" );
            }

            // Scene mode: check screenshot triggers (read back buffer before swap)
            if ( m_isSceneMode && m_screenshotPath[0] != '\0' && !m_isScreenshotSaved )
            {
                bool shouldCapture = false;

                if ( m_screenshotFrame > 0 && ( m_currentFrame + 1 ) >= m_screenshotFrame )
                {
                    shouldCapture = true;
                }

                if ( m_screenshotMs > 0 && m_cSimulationTimer.GetTimeSinceLastStart() * 1000.0 >= m_screenshotMs )
                {
                    shouldCapture = true;
                }

                if ( shouldCapture )
                {
                    this->SaveScreenshot( m_screenshotPath );
                    m_isScreenshotSaved = true;

                    // GL reset test pass 1: force context recreation instead of exiting
                    if ( m_isGlResetTest && sGlResetPass == 0 )
                    {
                        sGlResetPass++;
                        PROFILE_FRAME_END();
                        return true; // triggers GL context destroy/recreate in WinMain loop
                    }

                    // Normal exit (or pass 2 of GL reset test)
                    sGlResetPass = 0; // reset for next test
                    PostQuitMessage( 0 );
                }
            }

            // Swap back buffer
            PROFILE_BEGIN( "Frame/VsyncWait" );
            SwapBuffers( m_cWindow->m_sDevice );
            PROFILE_END( "Frame/VsyncWait" );

            // Stop frame timer
            m_cFrameTimer.StopTimer();

            // Close profiler frame and refresh back-compat timing fields from markers
            PROFILE_FRAME_END();
#if defined( SKULLBONEZ_PROFILE_ENABLED )
            {
                using SkullbonezCore::Basics::Profiler;
                static constexpr uint32_t kPhysicsHash = ::HashStr( "Frame/Physics" );
                static constexpr uint32_t kRenderHash = ::HashStr( "Frame/Render" );
                m_physicsTime = Profiler::Instance().LastFrameMsByHash( kPhysicsHash ) * 0.001f;
                m_renderTime = Profiler::Instance().LastFrameMsByHash( kRenderHash ) * 0.001f;
            }
#endif

            // Perf test: log per-frame timing + periodic memory
            if ( m_isPerfTest && m_perfLogFile )
            {
#if defined( SKULLBONEZ_PROFILE_ENABLED )
                if ( !m_perfHeaderWritten )
                {
                    Profiler::Instance().WritePerfCSVHeader( m_perfLogFile );
                    m_perfHeaderWritten = true;
                }
                Profiler::Instance().WritePerfCSVRow( m_perfLogFile, sPerfPass + 1, m_currentFrame + 1 );
#else
                fprintf( m_perfLogFile, "%d,%d,%.4f,%.4f\n", sPerfPass + 1, m_currentFrame + 1, m_physicsTime * 1000.0f, m_renderTime * 1000.0f );
#endif

                // Log memory every 60 frames (~1 second)
                if ( ( m_currentFrame + 1 ) % 60 == 0 )
                {
                    this->LogPerfMemory( "periodic" );
                }
            }

            // Scene mode: count frames
            if ( m_isSceneMode )
            {
                ++m_currentFrame;
            }

            // Scene mode: hold after target reached (skip if screenshot auto-exit pending)
            if ( m_isSceneMode && m_targetFrameCount > 0 && !m_isScreenshotSaved )
            {
                if ( m_currentFrame >= m_targetFrameCount )
                {
                    // hold — keep rendering but don't advance logic
                    for ( ;; )
                    {
                        MSG holdMsg;
                        if ( PeekMessage( &holdMsg, NULL, 0, 0, PM_REMOVE ) )
                        {
                            if ( holdMsg.message == WM_QUIT )
                            {
                                return false;
                            }
                            TranslateMessage( &holdMsg );
                            DispatchMessage( &holdMsg );
                        }
                        else
                        {
                            Sleep( 16 );
                        }
                    }
                }
            }

            // End the simulation when required (legacy mode only, not while user has camera control)
            if ( !m_isSceneMode && !m_isFlyMode && m_cSimulationTimer.GetTimeSinceLastStart() > 20.0 )
            {
                return true;
            }

            // Perf test: restart at 5s (pass 1), exit at 5s (pass 2)
            if ( m_isPerfTest && m_cSimulationTimer.GetTimeSinceLastStart() > 5.0 )
            {
                this->LogPerfMemory( "end" );
                if ( sPerfPass == 0 )
                {
                    sPerfPass++;
                    if ( m_perfLogFile )
                    {
                        fclose( m_perfLogFile );
                        m_perfLogFile = nullptr;
                    }
                    return true; // force GL context restart
                }
                else
                {
                    sPerfPass = 0;
                    if ( m_perfLogFile )
                    {
                        fclose( m_perfLogFile );
                        m_perfLogFile = nullptr;
                    }
                    PostQuitMessage( 0 );
                }
            }
        }
    }

    return false;
}

/* -- TAKE INPUT ------------------------------------------------------------------*/
void SkullbonezRun::TakeInput( void )
{
    // Toggle fly mode with F
    bool prevFlyMode = m_isFlyMode;
    m_isFlyMode = ( Input::IsKeyToggled( 'F' ) != 0 );

    if ( m_isFlyMode != prevFlyMode )
    {
        if ( m_isFlyMode )
        {
            // Entering fly mode: snap to free camera (no tween), remove XZ bounds, hide and centre mouse
            m_cCameras->SelectCamera( CAMERA_FREE, false );
            m_cameraTime = 0.0f;
            XZBounds unbounded;
            unbounded.m_xMin = -99999.9f;
            unbounded.m_xMax = 99999.9f;
            unbounded.m_zMin = -99999.9f;
            unbounded.m_zMax = 99999.9f;
            m_cCameras->SetCameraXZBounds( CAMERA_FREE, unbounded );
            SetCursor( nullptr );
            Input::CentreMouseCoordinates();
            m_sInputState.xMove = 0;
            m_sInputState.yMove = 0;
        }
        else
        {
            // Exiting fly mode: restore m_terrain XZ bounds, cursor, camera cycle clock
            m_cCameras->SetCameraXZBounds( CAMERA_FREE, m_cTerrain->GetXZBounds() );
            SetCursor( LoadCursor( nullptr, IDC_ARROW ) );
            m_cameraTime = 0.0f;
        }
    }

    // Water m_shader debug toggles
    bool prevFreeze = m_isWaterFreezeDebug;
    m_isWaterFreezeDebug = ( Input::IsKeyToggled( '1' ) != 0 ); // Water perturbation ON
    if ( m_isWaterFreezeDebug && !prevFreeze )
    {
        m_frozenWaterTime = static_cast<float>( m_cSimulationTimer.GetTimeSinceLastStart() );
    }
    m_isWaterNoReflect = ( Input::IsKeyToggled( '2' ) != 0 ); // Reflection default ON
    m_isWaterFlatDebug = ( Input::IsKeyToggled( '3' ) != 0 ); // Ocean wave displacement ON

    // Profiler overlay default ON; pressing '0' toggles the OS-level toggle bit, hiding the overlay.
    m_isProfilerOverlay = ( Input::IsKeyToggled( '0' ) == 0 );

    if ( m_isFlyMode )
    {
        // Keep cursor hidden every frame — Windows restores it on WM_SETCURSOR
        SetCursor( nullptr );

        // Mouse look: delta from screen centre
        POINT currentCoords = Input::GetMouseCoordinates();
        Input::CentreMouseCoordinates();
        POINT centreCoords = Input::GetMouseCoordinates();
        m_sInputState.xMove = currentCoords.x - centreCoords.x;
        m_sInputState.yMove = currentCoords.y - centreCoords.y;

        // WASD movement
        m_sInputState.fUp = Input::IsKeyDown( 'W' );
        m_sInputState.fLeft = Input::IsKeyDown( 'A' );
        m_sInputState.fDown = Input::IsKeyDown( 'S' );
        m_sInputState.fRight = Input::IsKeyDown( 'D' );
    }
    else
    {
        m_sInputState.xMove = 0;
        m_sInputState.yMove = 0;
        m_sInputState.fUp = false;
        m_sInputState.fDown = false;
        m_sInputState.fLeft = false;
        m_sInputState.fRight = false;
    }
}

/* -- UPDATE LOGIC ----------------------------------------------------------------*/
void SkullbonezRun::UpdateLogic( float fSecondsPerFrame )
{
    if ( !m_isFlyMode || Input::IsKeyDown( VK_SPACE ) )
    {
        // update the game models (sub-markers added inside RunPhysics)
        PROFILE_BEGIN( "Frame/Physics" );
        m_cGameModelCollection.RunPhysics( fSecondsPerFrame );
        PROFILE_END( "Frame/Physics" );
    }

    // move the camera based on input
    // (arguments are calculating time based movement quantities)
    this->MoveCamera( fSecondsPerFrame * Cfg().keySpeed,
                      fSecondsPerFrame * Cfg().mouseSensitivity );

    // update camera tweening speed
    m_cCameras->SetTweenSpeed( Cfg().cameraTweenRate * fSecondsPerFrame );
}

/* -- RENDER ----------------------------------------------------------------------*/
void SkullbonezRun::Render( void )
{
    // Clear screen pixel and depth into buffers
    glClear( GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT );

    // renders camera views etc
    this->SetViewingOrientation();

    // set the camera into its m_position
    m_cCameras->SetCamera();

    // now camera rotation has been done, draw OpenGL primitives
    this->DrawPrimitives();
}

/* -- DRAW PRIMITIVES ---------------------------------------------------------------------*/
void SkullbonezRun::DrawPrimitives( void )
{
    float lightPosition[] = { 200.0f, 400.0f, 1200.0f, 1.0f };

    // Get view and projection matrices from camera/window
    Matrix4 baseView = m_cCameras->GetViewMatrix();
    Matrix4 proj = m_cWindow->GetProjectionMatrix();
    Matrix4 reflVP;

    // Current viewport — saved here so the reflection pre-pass can restore it
    GLint vp[4];
    glGetIntegerv( GL_VIEWPORT, vp );

    // Camera m_position for skybox placement
    Vector3 eye = m_cCameras->GetCameraTranslation();

    // render skybox ------------------------------
    {
        PROFILE_SCOPED( "Frame/Render/Skybox" );
        Matrix4 skyView = baseView * Matrix4::Translate( eye.x, Cfg().skyboxRenderHeight, eye.z ) * Matrix4::Scale( Cfg().skyboxScale );
        m_cSkyBox->Render( skyView, proj );
    }

    // reflection pre-pass: render above-water scene from mirrored camera into FBO
    // TODO: this needs to run when camera m_isTweening!!
    {
        PROFILE_SCOPED( "Frame/Render/Reflection" );
        float waterY = m_cWorldEnvironment.GetFluidSurfaceHeight();
        Vector3 center = m_cCameras->GetCameraView();

        // Mirror eye and look-at target about the water plane; flip up vector
        Vector3 reflEye( eye.x, 2.0f * waterY - eye.y, eye.z );
        Vector3 reflCenter( center.x, 2.0f * waterY - center.y, center.z );
        Vector3 reflUp( 0.0f, -1.0f, 0.0f );
        Matrix4 reflView = Matrix4::LookAt( reflEye, reflCenter, reflUp );
        reflVP = proj * reflView;

        m_cReflectionFBO->Bind();
        glViewport( 0, 0, m_cReflectionFBO->GetWidth(), m_cReflectionFBO->GetHeight() );
        glClear( GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT );

        // Skybox reflected (XZ follows eye; Y anchored at Cfg().skyboxRenderHeight)
        {
            PROFILE_SCOPED( "Frame/Render/Reflection/Skybox" );
            Matrix4 skyReflView = reflView * Matrix4::Translate( eye.x, Cfg().skyboxRenderHeight, eye.z ) * Matrix4::Scale( Cfg().skyboxScale );
            m_cSkyBox->Render( skyReflView, proj );
        }

        // Game models reflected — clip at water surface (above-water portion only)
        PROFILE_BEGIN( "Frame/Render/Reflection/Balls" );
        glEnable( GL_CLIP_DISTANCE0 );
        SkullbonezHelper::SetClipPlane( 0.0f, 1.0f, 0.0f, -waterY );
        m_cTextures->SelectTexture( TEXTURE_BOUNDING_SPHERE );
        m_cGameModelCollection.RenderModels( reflView, proj, lightPosition );
        glDisable( GL_CLIP_DISTANCE0 );
        SkullbonezHelper::SetClipPlane( 0.0f, 1.0f, 0.0f, 1.0e9f );
        PROFILE_END( "Frame/Render/Reflection/Balls" );

        m_cReflectionFBO->Unbind();
        glViewport( vp[0], vp[1], vp[2], vp[3] );
    }

    // render game models -----------------------------
    PROFILE_BEGIN( "Frame/Render/Balls" );
    m_cTextures->SelectTexture( TEXTURE_BOUNDING_SPHERE );
    m_cGameModelCollection.RenderModels( baseView, proj, lightPosition );
    PROFILE_END( "Frame/Render/Balls" );

    // render m_terrain ------------------------------
    {
        PROFILE_SCOPED( "Frame/Render/Terrain" );
        m_cTextures->SelectTexture( TEXTURE_GROUND );
        m_cTerrain->Render( baseView, proj, lightPosition );
    }

    // render ground shadows on top of m_terrain
    PROFILE_BEGIN( "Frame/Render/Shadows" );
    m_cGameModelCollection.RenderShadows( m_cTerrain.get(), baseView, proj );
    PROFILE_END( "Frame/Render/Shadows" );

    // render the fluid ---------------------------
    {
        PROFILE_SCOPED( "Frame/Render/Water" );
        float waterTime = m_isWaterFreezeDebug
                              ? m_frozenWaterTime
                              : static_cast<float>( m_cSimulationTimer.GetTimeSinceLastStart() );
        m_cWorldEnvironment.RenderFluid( baseView, proj, reflVP, waterTime, m_cReflectionFBO->GetColorTexture(), m_isWaterFlatDebug, m_isWaterNoReflect );
    }
}

/* -- SET UP CAMERAS ----------------------------------------------------------------------*/
void SkullbonezRun::SetUpCameras( void )
{
    m_cCameras = CameraCollection::Instance();

    m_cCameras->AddCamera( Vector3( 321.0f, 110.0f, 557.0f ), // Position
                           Vector3( 581.0f, 40.0f, 633.0f ),  // View
                           Vector3( 0.0f, 1.0f, 0.0f ),       // Up
                           CAMERA_GAME_MODEL_1 );

    m_cCameras->AddCamera( Vector3( 730.0f, 100.0f, 380.0f ), // Position
                           Vector3( 709.0f, 92.0f, 482.0f ),  // View
                           Vector3( 0.0f, 1.0f, 0.0f ),       // Up
                           CAMERA_GAME_MODEL_2 );

    m_cCameras->AddCamera( Vector3( 900.0f, 110.0f, 900.0f ), // Position
                           Vector3( 313.0f, 31.0f, 282.0f ),  // View
                           Vector3( 0.0f, 1.0f, 0.0f ),       // Up
                           CAMERA_FREE );

    // set the camera m_boundaries
    m_cCameras->SetCameraXZBounds( m_cTerrain->GetXZBounds() );

    // set the m_terrain
    m_cCameras->SetTerrain( m_cTerrain.get() );

    // lock the m_cameras
    m_cCameras->SetLockedMode( true );
}

/* -- SET INITIAL OPEN GL STATE -----------------------------------------------------------*/
void SkullbonezRun::SetInitialOpenGlState( void )
{
    SkullbonezHelper::ResetGLResources();
    SkullbonezHelper::StateSetup();

    // load m_textures
    const SkullbonezConfig& cfg = Cfg();
    m_cTextures->CreateJpegTexture( cfg.terrainTexture.c_str(), TEXTURE_GROUND );
    m_cTextures->CreateJpegTexture( cfg.sphereTexture.c_str(), TEXTURE_BOUNDING_SPHERE );
}

/* -- DRAW WINDOW TEXT --------------------------------------------------------------------------------------------------------------------------------------------------*/
void SkullbonezRun::DrawWindowText( const double dSecondsPerFrame )
{
    // update timers
    m_cUpdateTimer.StopTimer();
    m_timeSinceLastRender += (float)m_cUpdateTimer.GetElapsedTime();
    m_cUpdateTimer.StartTimer();

    // if half a second has passed
    if ( m_timeSinceLastRender > 0.5f )
    {
        if ( dSecondsPerFrame )
        {
            // update the display information
            m_r_fpsTime = 1.0f / (float)dSecondsPerFrame;
            m_r_physicsTime = m_physicsTime;
            m_r_renderTime = m_renderTime;
        }

        // reset time since last render
        m_timeSinceLastRender = 0.0f;
    }

    // TOP
    Text2d::Render2dText( -0.53f, 0.39f, 0.015f, "SKULLBONEZ CORE | Simon Eschbach 2005" );
    Text2d::Render2dText( 0.39f, 0.39f, 0.015f, "Model Count: %i", m_modelCount );

    // Profiler overlay (replaces the legacy bottom FPS/Physics/Render strip).
    // Compiled out in Release; toggleable with '0' in Debug/Profile.
#if defined( SKULLBONEZ_PROFILE_ENABLED )
    if ( m_isProfilerOverlay )
    {
        Profiler::Instance().RenderOverlay( -0.53f, -0.43f, 0.018f, 0.012f, m_r_fpsTime );
    }
#endif
}

/* -- SET VIEWING ORIENTATION -------------------------------------------------------------------------------------------------------------------------------------------*/
void SkullbonezRun::SetViewingOrientation( void )
{
    // In scene mode, use the first camera without cycling
    if ( m_isSceneMode )
    {
        return;
    }

    // In fly mode, freeze the cycle clock and keep the free camera
    if ( m_isFlyMode )
    {
        m_cameraTime = 0.0f;
        m_cCameraTimer.StopTimer();
        m_cCameraTimer.StartTimer();
        return;
    }

    // set viewing m_orientation
    /*
        if(Input::IsKeyDown('1')) m_selectedCamera = 0;
        if(Input::IsKeyDown('2')) m_selectedCamera = 1;
        if(Input::IsKeyDown('3')) m_selectedCamera = 2;
    */

    // maintain the camera timer
    m_cCameraTimer.StopTimer();
    m_cameraTime += (float)m_cCameraTimer.GetElapsedTime();
    m_cCameraTimer.StartTimer();

    // change the viewing camera automatically
    if ( m_cameraTime > 5.0f )
    {
        ++m_selectedCamera;
        if ( m_selectedCamera == 3 )
        {
            m_selectedCamera = 0;
        }
        m_cameraTime = 0.0f;
    }

    // select camera based on input
    switch ( m_selectedCamera )
    {
    case 0:
        m_cCameras->SelectCamera( CAMERA_GAME_MODEL_1, true );
        break;
    case 1:
        m_cCameras->SelectCamera( CAMERA_GAME_MODEL_2, true );
        break;
    case 2:
        m_cCameras->SelectCamera( CAMERA_FREE, true );
        break;
    }

    // set the view m_position of the selected camera based on the game model m_position
    if ( m_cCameras->IsCameraSelected( CAMERA_GAME_MODEL_1 ) )
    {
        m_cCameras->SetViewCoordinates( m_cGameModelCollection.GetModelPosition( 0 ) );
    }
    if ( m_cCameras->IsCameraSelected( CAMERA_GAME_MODEL_2 ) )
    {
        m_cCameras->SetViewCoordinates( m_cGameModelCollection.GetModelPosition( 1 ) );
    }

    /*
        // reset relativity when a new request for synchronisation comes in
        if(m_sInputState.fAux1) m_cCameras->ResetRelativity();

        // sync m_cameras if in sync mode
        if(m_sInputState.fAux2)
        {
            // perform the relative update
            this->RelativeUpdateCamera(CAMERA_GAME_MODEL_1);
            this->RelativeUpdateCamera(CAMERA_GAME_MODEL_2);
            this->RelativeUpdateCamera(CAMERA_FREE);

            // reset the relative variable as we have already performed the action on desired m_cameras
            m_cCameras->ResetRelativity();
        }
    */
}

/* -- RELATIVE UPDATE CAMERA --------------------------------------------------------------------------------------------------------------------------------------------*/
void SkullbonezRun::RelativeUpdateCamera( uint32_t hash )
{
    if ( !m_cCameras->IsCameraSelected( hash ) )
    {
        Vector3 translatedCameraPosition = m_cCameras->GetCameraTranslation( hash );
        float minY = m_cTerrain->GetTerrainHeightAt( translatedCameraPosition.x, translatedCameraPosition.z, true ) + Cfg().minCameraHeight;
        m_cCameras->RelativeUpdate( hash, minY, Cfg().maxCameraHeight );
    }
}

/* -- MOVE CAMERA -------------------------------------------------------------------------------------------------------------------------------------------------------*/
void SkullbonezRun::MoveCamera( float keyMovementQty, float mouseMovementQty )
{
    if ( m_isFlyMode )
    {
        // Shift held = 3x speed
        float speedMult = Input::IsKeyDown( VK_SHIFT ) ? 3.0f : 1.0f;

        // Mouse look
        if ( m_sInputState.xMove != 0 || m_sInputState.yMove != 0 )
        {
            m_cCameras->RotatePrimary( m_sInputState.xMove * mouseMovementQty,
                                       m_sInputState.yMove * mouseMovementQty );
        }

        // WASD movement
        if ( m_sInputState.fUp )
        {
            m_cCameras->MovePrimary( Camera::TravelDirection::Forward, keyMovementQty * speedMult );
        }
        if ( m_sInputState.fLeft )
        {
            m_cCameras->MovePrimary( Camera::TravelDirection::Left, keyMovementQty * speedMult );
        }
        if ( m_sInputState.fDown )
        {
            m_cCameras->MovePrimary( Camera::TravelDirection::Backward, keyMovementQty * speedMult );
        }
        if ( m_sInputState.fRight )
        {
            m_cCameras->MovePrimary( Camera::TravelDirection::Right, keyMovementQty * speedMult );
        }

        m_cCameras->ApplyPrimaryMovementBuffer();
    }

    // Clamp camera Y between m_terrain surface and Cfg().maxCameraHeight (not in fly mode)
    if ( !m_isFlyMode )
    {
        Vector3 translatedCameraPosition = m_cCameras->GetCameraTranslation();
        float minY = m_cTerrain->GetTerrainHeightAt( translatedCameraPosition.x, translatedCameraPosition.z, true ) + Cfg().minCameraHeight;
        if ( minY > translatedCameraPosition.y )
        {
            m_cCameras->AmmendPrimaryY( minY );
        }
        else if ( translatedCameraPosition.y > Cfg().maxCameraHeight )
        {
            m_cCameras->AmmendPrimaryY( Cfg().maxCameraHeight );
        }
    }
}

/* -- SET UP CAMERAS FROM SCENE ---------------------------------------------------*/
void SkullbonezRun::SetUpCamerasFromScene( const TestScene& scene )
{
    m_cCameras = CameraCollection::Instance();

    for ( int i = 0; i < scene.GetCameraCount(); ++i )
    {
        const SceneCamera& cam = scene.GetCamera( i );
        uint32_t hash = HashStr( cam.name );
        m_cCameras->AddCamera( cam.m_position, cam.view, cam.up, hash );
    }

    // set the camera m_boundaries
    m_cCameras->SetCameraXZBounds( m_cTerrain->GetXZBounds() );

    // set the m_terrain
    m_cCameras->SetTerrain( m_cTerrain.get() );

    // lock the m_cameras
    m_cCameras->SetLockedMode( true );
}

/* -- SET UP GAME MODELS FROM SCENE -----------------------------------------------*/
void SkullbonezRun::SetUpGameModelsFromScene( const TestScene& scene )
{
    m_modelCount = scene.GetBallCount();

    for ( int i = 0; i < scene.GetBallCount(); ++i )
    {
        const SceneBall& ball = scene.GetBall( i );

        GameModel gameModel( &m_cWorldEnvironment,
                             Vector3( ball.posX, ball.posY, ball.posZ ),
                             Vector3( ball.moment, ball.moment, ball.moment ),
                             ball.m_mass );

        gameModel.SetCoefficientRestitution( ball.restitution );
        gameModel.SetTerrain( m_cTerrain.get() );
        gameModel.AddBoundingSphere( ball.m_radius );

        // apply force if any is specified
        if ( ball.forceX != 0.0f || ball.forceY != 0.0f || ball.forceZ != 0.0f )
        {
            gameModel.SetImpulseForce(
                Vector3( ball.forceX, ball.forceY, ball.forceZ ),
                Vector3( ball.forcePosX, ball.forcePosY, ball.forcePosZ ) );
        }

        m_cGameModelCollection.AddGameModel( std::move( gameModel ) );
    }
}

/* -- SAVE SCREENSHOT -------------------------------------------------------------*/
void SkullbonezRun::SaveScreenshot( const char* path )
{
    // Read viewport dimensions
    GLint viewport[4];
    glGetIntegerv( GL_VIEWPORT, viewport );
    int m_width = viewport[2];
    int m_height = viewport[3];

    // Row stride padded to 4-byte boundary (BMP requirement)
    int rowStride = ( m_width * 3 + 3 ) & ~3;
    int imageSize = rowStride * m_height;

    // Allocate pixel buffer
    std::vector<unsigned char> pixels( static_cast<size_t>( imageSize ) );

    // Read the back buffer (bottom-up, BGR — native BMP layout)
    glPixelStorei( GL_PACK_ALIGNMENT, 4 );
    glReadBuffer( GL_BACK );
    glReadPixels( 0, 0, m_width, m_height, GL_BGR, GL_UNSIGNED_BYTE, pixels.data() );

    // BMP file header (14 bytes)
    unsigned char fileHeader[14] = {};
    int fileSize = 14 + 40 + imageSize;
    fileHeader[0] = 'B';
    fileHeader[1] = 'M';
    fileHeader[2] = (unsigned char)( fileSize );
    fileHeader[3] = (unsigned char)( fileSize >> 8 );
    fileHeader[4] = (unsigned char)( fileSize >> 16 );
    fileHeader[5] = (unsigned char)( fileSize >> 24 );
    fileHeader[10] = 54; // pixel data offset

    // BMP info header (40 bytes)
    unsigned char infoHeader[40] = {};
    infoHeader[0] = 40; // header size
    infoHeader[4] = (unsigned char)( m_width );
    infoHeader[5] = (unsigned char)( m_width >> 8 );
    infoHeader[6] = (unsigned char)( m_width >> 16 );
    infoHeader[7] = (unsigned char)( m_width >> 24 );
    infoHeader[8] = (unsigned char)( m_height );
    infoHeader[9] = (unsigned char)( m_height >> 8 );
    infoHeader[10] = (unsigned char)( m_height >> 16 );
    infoHeader[11] = (unsigned char)( m_height >> 24 );
    infoHeader[12] = 1;  // color planes
    infoHeader[14] = 24; // bits per pixel
    infoHeader[20] = (unsigned char)( imageSize );
    infoHeader[21] = (unsigned char)( imageSize >> 8 );
    infoHeader[22] = (unsigned char)( imageSize >> 16 );
    infoHeader[23] = (unsigned char)( imageSize >> 24 );

    // Write to file
    FILE* file = nullptr;
    errno_t err = fopen_s( &file, path, "wb" );
    if ( err != 0 || !file )
    {
        char msg[512];
        sprintf_s( msg, sizeof( msg ), "Failed to open screenshot file: %s  (SkullbonezRun::SaveScreenshot)", path );
        throw std::runtime_error( msg );
    }

    fwrite( fileHeader, 1, 14, file );
    fwrite( infoHeader, 1, 40, file );
    fwrite( pixels.data(), 1, static_cast<size_t>( imageSize ), file );
    fclose( file );
}

/* -- LOG PERF MEMORY -------------------------------------------------------------*/
void SkullbonezRun::LogPerfMemory( const char* checkpoint )
{
    if ( !m_perfLogFile )
    {
        return;
    }

    PROCESS_MEMORY_COUNTERS pmc;
    pmc.cb = sizeof( pmc );
    if ( GetProcessMemoryInfo( GetCurrentProcess(), &pmc, sizeof( pmc ) ) )
    {
        double mb = static_cast<double>( pmc.WorkingSetSize ) / ( 1024.0 * 1024.0 );
        fprintf( m_perfLogFile, "# MEM %s pass=%d working_set_mb=%.2f\n", checkpoint, sPerfPass + 1, mb );
        fflush( m_perfLogFile );
    }
}
