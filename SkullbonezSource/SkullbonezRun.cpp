// --- Includes ---
#include "SkullbonezRun.h"
#include "SkullbonezHelper.h"
#include "SkullbonezBoundingSphere.h"
#include "SkullbonezGameModel.h"
#include "SkullbonezProfiler.h"
#include "SkullbonezIRenderBackend.h"
#include "SkullbonezRenderBackendGL.h"
#include "SkullbonezRenderBackendDX11.h"
#include "SkullbonezRenderBackendDX12.h"
#include "SkullbonezCollisionResponse.h"
#include "SkullbonezImpulseSolver.h"
#include <time.h>
#include <cstring>
#include <psapi.h>
#include <cmath>
#include <dwmapi.h>
#pragma comment( lib, "dwmapi.lib" )

// --- Usings ---
using namespace SkullbonezCore::Basics;
using namespace SkullbonezCore::Math::CollisionDetection;
using namespace SkullbonezCore::Physics;


SkullbonezRun::SkullbonezRun( std::vector<std::string> sceneQueue, bool legacyPhysics )
    : m_sceneQueue( std::move( sceneQueue ) )
{
    m_cGameModelCollection.SetLegacyMode( legacyPhysics );
    // Config-driven defaults are resolved at construction; scene-specific defaults remain in-member.
    m_runtimeSettings.isVsyncEnabled = Cfg().runtimeRender.vsyncEnabled;
    m_runtimeSettings.isPipelineSyncEnabled = Cfg().runtimeRender.forcePipelineSync;
    m_runtimeSettings.defaultRollAlignEnabled = Cfg().rollAlignEnabled;
    m_runtimeSettings.isRollAlignEnabled = m_runtimeSettings.defaultRollAlignEnabled;
}


SkullbonezRun::~SkullbonezRun()
{
    if ( m_perfLogState.perfLogFile )
    {
        fclose( m_perfLogState.perfLogFile );
        m_perfLogState.perfLogFile = nullptr;
    }

    // Flush GPU before destroying resources to avoid use-after-free
    if ( IsGfxReady() )
    {
        Gfx().FlushGPU();
    }

    // Clean up GL resources while context is still alive
    SkullbonezHelper::ResetGLResources();
    m_cWorldEnvironment.ResetGLResources();
    m_cGameModelCollection.ResetGLResources();
    if ( m_systems.reflectionFBO )
    {
        m_systems.reflectionFBO->ResetResources();
    }
#if defined( SKULLBONEZ_PROFILE_ENABLED )
    Profiler::Instance().InvalidateGpuQueries();
#endif
    Text2d::DeleteFont();

    m_systems.textures->Destroy();
    m_systems.cameras->Destroy();
    m_systems.skyBox->Destroy();
}


void SkullbonezRun::SetRendererSwitchInterval( float seconds )
{
    m_debug.rendererSwitchInterval = seconds;
}


#ifdef _DEBUG
void SkullbonezRun::SetPhysicsLogOverride( const char* path )
{
    strcpy_s( m_perfLogState.physicsLogOverride, sizeof( m_perfLogState.physicsLogOverride ), path );
}
#endif


void SkullbonezRun::Initialise()
{
    // Init window
    m_systems.window = SkullbonezWindow::Instance();

    // Set loading text
    const char* rendererName = Gfx().GetRendererName();
    char titleText[256];
    sprintf_s( titleText, "%s [%s] -- LOADING!!!", TITLE_TEXT, rendererName );
    m_systems.window->SetTitleText( titleText );

    // Init m_textures
    m_systems.textures = TextureCollection::Instance();

    // Init OpenGL
    SetInitialOpenGlState();

    // Init m_terrain
    // path to m_height map | map size pixels | step size | times to wrap texture
    m_systems.terrain = std::make_unique<Terrain>( ( std::string( DATA_ROOT ) + Cfg().terrainRaw ).c_str(), 256, 8, 15 );

    // Init SkyBox (m_xMin, m_xMax, yMin, yMax, m_zMin, m_zMax)
    m_systems.skyBox = SkyBox::Instance( -250, 300, -300, 300, -250, 300 );
    m_systems.skyBox->ResetGLResources();

    // Init world environment
    {
        const SkullbonezConfig& cfg = Cfg();
        m_cWorldEnvironment = WorldEnvironment( cfg.fluidHeight, cfg.fluidDensity, cfg.gasDensity, cfg.gravity );
        XZBounds tb = m_systems.terrain->GetXZBounds();
        m_cWorldEnvironment.SetTerrainBounds( tb.m_xMin, tb.m_xMax, tb.m_zMin, tb.m_zMax );
    }

    // Init reflection FBO at the current viewport size
    int fboW = Gfx().GetWidth() * 2;
    int fboH = Gfx().GetHeight() * 2;
    m_systems.reflectionFBO = Gfx().CreateFramebuffer( fboW, fboH );

    // Init font (HDC, font)
    Text2d::BuildFont( "Verdana" );

    // Init cameras singleton (shared across scenes, Reset() between loads)
    m_systems.cameras = CameraCollection::Instance();

    // Load the first scene
    LoadScene( 0 );
}


RuntimeRendererType SkullbonezRun::GetCurrentRendererType() const
{
    const char* rendererName = Gfx().GetRendererName();
    if ( strstr( rendererName, "12" ) )
    {
        return RuntimeRendererType::DX12;
    }
    if ( strstr( rendererName, "11" ) )
    {
        return RuntimeRendererType::DX11;
    }
    return RuntimeRendererType::OpenGL;
}


RuntimeRendererType SkullbonezRun::GetNextRendererType( RuntimeRendererType current ) const
{
    switch ( current )
    {
    case RuntimeRendererType::OpenGL:
        return RuntimeRendererType::DX11;
    case RuntimeRendererType::DX11:
        return RuntimeRendererType::DX12;
    case RuntimeRendererType::DX12:
    default:
        return RuntimeRendererType::OpenGL;
    }
}


void SkullbonezRun::SwitchRenderer( RuntimeRendererType target )
{
    if ( !m_systems.window || !IsGfxReady() )
    {
        return;
    }

    RuntimeRendererType current = GetCurrentRendererType();
    if ( current == target )
    {
        return;
    }

    // --- Phase 1: Tear down the current backend ---
    // All GPU-visible resources must be released while the backend that owns them is still alive.
    // The backend's FlushGPU() ensures all in-flight GPU work completes before resource destruction.
    Gfx().FlushGPU();
    if ( m_systems.reflectionFBO )
    {
        m_systems.reflectionFBO->ResetResources();
        m_systems.reflectionFBO.reset();
    }
    Text2d::DeleteFont();
    m_cGameModelCollection.ResetGLResources();
    SkullbonezHelper::ResetGLResources();
    if ( m_systems.textures )
    {
        m_systems.textures->DeleteAllTextures();
    }
#if defined( SKULLBONEZ_PROFILE_ENABLED )
    Profiler::Instance().InvalidateGpuQueries();
#endif

    // Determine if the outgoing backend used a DXGI flip-model swap chain.
    // DXGI swap chains take exclusive ownership of the HWND's DWM composition surface;
    // GDI rendering (used by OpenGL's SwapBuffers) is disabled while DXGI owns the window.
    bool outgoingWasDXGI = ( current == RuntimeRendererType::DX11 || current == RuntimeRendererType::DX12 );

    // Destroy the backend — its destructor calls Shutdown() which releases all API objects
    // including the swap chain.
    DestroyGfxBackend();

    // --- Phase 2: Reset the window surface between DXGI and GDI ownership ---
    // DXGI flip-model swap chains (DXGI_SWAP_EFFECT_FLIP_DISCARD) permanently taint a window's
    // GDI presentation surface. After a DXGI swap chain has been on an HWND, SwapBuffers
    // (used by OpenGL) can no longer present to the screen — it succeeds but DWM continues
    // compositing the stale DXGI surface. No amount of DwmFlush/SetWindowPos/DC re-acquire
    // fixes this; the taint is permanent for the lifetime of that HWND.
    //
    // The only reliable fix is to destroy the HWND and create a fresh one. The new window gets
    // a clean GDI surface that SwapBuffers can present to normally.
    if ( outgoingWasDXGI && target == RuntimeRendererType::OpenGL )
    {
        // Delete the old WGL context — it was bound to the old window's DC.
        if ( m_systems.window->m_sRenderContext )
        {
            wglMakeCurrent( nullptr, nullptr );
            wglDeleteContext( m_systems.window->m_sRenderContext );
            m_systems.window->m_sRenderContext = nullptr;
        }

        // Recreate the window entirely (new HWND, new DC, clean GDI surface)
        m_systems.window->RecreateWindow();
    }
    else if ( outgoingWasDXGI )
    {
        // Switching between DXGI backends (DX11↔DX12): DwmFlush is sufficient since both
        // use DXGI swap chains and don't rely on GDI surfaces.
        DwmFlush();
    }

    // --- Phase 3: Create and initialise the new backend ---
    auto makeBackend = [&]( RuntimeRendererType type ) -> std::unique_ptr<IRenderBackend>
    {
        if ( type == RuntimeRendererType::OpenGL )
        {
            // After Phase 2 recreated the window, we need a fresh WGL context on the new HWND.
            // If we're just cycling GL→GL (shouldn't happen) or DX→DX→GL, the context was
            // already destroyed in Phase 2.
            if ( !m_systems.window->m_sRenderContext )
            {
                m_systems.window->InitialiseOpenGL();
            }
            else
            {
                wglMakeCurrent( m_systems.window->m_sDevice, m_systems.window->m_sRenderContext );
            }
            return std::make_unique<RenderBackendGL>();
        }

        // DX backends: deactivate GL context so DXGI can take exclusive window ownership.
        if ( m_systems.window->m_sRenderContext )
        {
            wglMakeCurrent( nullptr, nullptr );
        }

        if ( type == RuntimeRendererType::DX12 )
        {
            return std::make_unique<RenderBackendDX12>();
        }

        return std::make_unique<RenderBackendDX11>();
    };

    auto initialiseBackend = [&]( RuntimeRendererType type ) -> std::unique_ptr<IRenderBackend>
    {
        std::unique_ptr<IRenderBackend> backend = makeBackend( type );
        if ( !backend->Init( m_systems.window->m_sWindow,
                             m_systems.window->m_sDevice,
                             m_systems.window->m_sWindowDimensions.x,
                             m_systems.window->m_sWindowDimensions.y ) )
        {
            throw std::runtime_error( "Renderer backend initialisation failed." );
        }
        return backend;
    };

    std::string switchFailureReason;
    try
    {
        std::unique_ptr<IRenderBackend> backend = initialiseBackend( target );
        SetGfxBackend( std::move( backend ) );
    }
    catch ( const std::exception& e )
    {
        switchFailureReason = e.what();
        try
        {
            std::unique_ptr<IRenderBackend> rollbackBackend = initialiseBackend( current );
            SetGfxBackend( std::move( rollbackBackend ) );
        }
        catch ( const std::exception& rollbackError )
        {
            char msg[512];
            sprintf_s( msg,
                       sizeof( msg ),
                       "Renderer switch failed (target: %s, rollback: %s).",
                       switchFailureReason.c_str(),
                       rollbackError.what() );
            throw std::runtime_error( msg );
        }
    }

    // --- Phase 4: Rebuild render resources ---
    m_systems.window->HandleScreenResize();
    Gfx().SetVsyncEnabled( m_runtimeSettings.isVsyncEnabled );

    SetInitialOpenGlState();
    if ( m_systems.terrain )
    {
        m_systems.terrain->ResetRenderResources();
    }
    if ( m_systems.skyBox )
    {
        m_systems.skyBox->ResetGLResources();
    }
    m_cWorldEnvironment.ResetGLResources();

    int fboW = Gfx().GetWidth() * 2;
    int fboH = Gfx().GetHeight() * 2;
    m_systems.reflectionFBO = Gfx().CreateFramebuffer( fboW, fboH );

    Text2d::BuildFont( "Verdana" );

    // --- Phase 5: Warm-up frame ---
    // Present one fully-cleared frame to the new backend. This serves two purposes:
    //  1. For DXGI backends (DX11/DX12): the first Present() registers the new swap chain with
    //     DWM and initialises all backbuffers to a known state. Without this, the compositor
    //     may briefly display garbage or stale content from the previous backend.
    //  2. For OpenGL: the first SwapBuffers() claims the GDI pixel buffer from DWM. If DXGI
    //     previously owned the window, this forces DWM to switch back to GDI compositing.
    // FlushGPU after presenting ensures the warm-up frame is fully complete before the main
    // loop begins rendering real content.
    // For GL specifically, we do TWO swap cycles to prime both front and back buffers.
    // The double-buffer swap chain means the first SwapBuffers fills the front buffer while
    // the back buffer may still hold stale DXGI content from the previous backend.
    Gfx().Clear( true, true );
    Gfx().Present();
    if ( target == RuntimeRendererType::OpenGL )
    {
        Gfx().Clear( true, true );
        Gfx().Present();
    }
    Gfx().FlushGPU();

    // --- Phase 6: Update window title ---
    char titleText[256];
    if ( switchFailureReason.empty() )
    {
        sprintf_s( titleText, "%s [%s]", TITLE_TEXT, Gfx().GetRendererName() );
    }
    else
    {
        sprintf_s( titleText, "%s [%s] -- SWITCH FAILED", TITLE_TEXT, Gfx().GetRendererName() );
        fprintf( stderr, "Renderer switch failed, restored previous renderer: %s\n", switchFailureReason.c_str() );
    }
    m_systems.window->SetTitleText( titleText );
}


void SkullbonezRun::SetUpGameModels( int count )
{
    m_scene.modelCount = count;

    const SkullbonezConfig& cfg = Cfg();

    auto randFloat = [&]( float base, int range )
    { return base + static_cast<float>( rand() % range ); };
    auto randSigned = [&]( int range ) -> float
    {
        float mag = 1.0f + static_cast<float>( rand() % range );
        return ( rand() % 2 == 0 ) ? mag : -mag;
    };
    auto randSign = []() -> float
    { return ( rand() % 2 == 0 ) ? 1.0f : -1.0f; };

    for ( int x = 0; x < m_scene.modelCount; ++x )
    {
        float posX = randFloat( cfg.spawnXBase, cfg.spawnXRange );
        float posY = randFloat( cfg.spawnYBase, cfg.spawnYRange );
        float posZ = randFloat( cfg.spawnZBase, cfg.spawnZRange );
        float mass = randFloat( cfg.ballMassMin, cfg.ballMassRange );
        float restitution = cfg.ballRestitutionMin + static_cast<float>( rand() % cfg.ballRestitutionRange ) / 10.0f;
        Vector3 force( randSigned( cfg.ballForceRange ), randSigned( cfg.ballForceRange ), randSigned( cfg.ballForceRange ) );
        Vector3 forcePos( randSign(), randSign(), randSign() );

        // ~30% of objects are boxes when using new physics; legacy mode is spheres only
        bool makeBox = !m_cGameModelCollection.GetLegacyMode() && ( rand() % 10 ) < 3;

        if ( makeBox )
        {
            float halfExtent = ( 1.0f + static_cast<float>( rand() % 3 ) ) * 0.6f;
            float hx = halfExtent * ( 0.7f + static_cast<float>( rand() % 4 ) * 0.2f );
            float hy = halfExtent;
            float hz = halfExtent * ( 0.7f + static_cast<float>( rand() % 4 ) * 0.2f );

            // Box inertia: I = m/3 * (hy² + hz²) etc.
            float hx2 = hx * hx;
            float hy2 = hy * hy;
            float hz2 = hz * hz;
            float m3 = mass / 3.0f;
            Vector3 inertia( m3 * ( hy2 + hz2 ), m3 * ( hx2 + hz2 ), m3 * ( hx2 + hy2 ) );

            GameModel gameModel( &m_cWorldEnvironment, Vector3( posX, posY, posZ ), inertia, mass );
            gameModel.SetCoefficientRestitution( restitution );
            gameModel.SetTerrain( m_systems.terrain.get() );
            gameModel.AddBoundingBox( Vector3( hx, hy, hz ) );
            gameModel.SetImpulseForce( force, forcePos );

            m_cGameModelCollection.AddGameModel( std::move( gameModel ) );
        }
        else
        {
            float moment = randFloat( cfg.ballMomentMin, cfg.ballMomentRange );
            float radius = ( 1.0f + static_cast<float>( rand() % cfg.ballRadiusRange ) ) * 0.5f;

            GameModel gameModel( &m_cWorldEnvironment, Vector3( posX, posY, posZ ), Vector3( moment, moment, moment ), mass );
            gameModel.SetCoefficientRestitution( restitution );
            gameModel.SetTerrain( m_systems.terrain.get() );
            gameModel.AddBoundingSphere( radius );
            gameModel.SetImpulseForce( force, forcePos );

            m_cGameModelCollection.AddGameModel( std::move( gameModel ) );
        }
    }
}


// Spawns exactly 'balls' sphere objects followed by exactly 'boxes' OBB objects using the
// same random parameter ranges as SetUpGameModels().  Unlike SetUpGameModels() — which uses
// a probabilistic 30% box split — this function gives the caller precise control over the
// object-type mix, which is important for reproducible benchmarks (e.g. "200 balls + 100 boxes").
//
// Spheres are spawned first so the RNG sequence is deterministic given a fixed seed,
// regardless of the balls/boxes ratio.
void SkullbonezRun::SetUpSolverObjects( int balls, int boxes )
{
    const SkullbonezConfig& cfg = Cfg();

    auto randFloat = [&]( float base, int range )
    { return base + static_cast<float>( rand() % range ); };
    auto randSigned = [&]( int range ) -> float
    {
        float mag = 1.0f + static_cast<float>( rand() % range );
        return ( rand() % 2 == 0 ) ? mag : -mag;
    };
    auto randSign = []() -> float
    { return ( rand() % 2 == 0 ) ? 1.0f : -1.0f; };

    // --- Sphere pass ---
    for ( int i = 0; i < balls; ++i )
    {
        float posX = randFloat( cfg.spawnXBase, cfg.spawnXRange );
        float posY = randFloat( cfg.spawnYBase, cfg.spawnYRange );
        float posZ = randFloat( cfg.spawnZBase, cfg.spawnZRange );
        float mass = randFloat( cfg.ballMassMin, cfg.ballMassRange );
        float restitution = cfg.ballRestitutionMin + static_cast<float>( rand() % cfg.ballRestitutionRange ) / 10.0f;
        float moment = randFloat( cfg.ballMomentMin, cfg.ballMomentRange );
        float radius = ( 1.0f + static_cast<float>( rand() % cfg.ballRadiusRange ) ) * 0.5f;
        Vector3 force( randSigned( cfg.ballForceRange ), randSigned( cfg.ballForceRange ), randSigned( cfg.ballForceRange ) );
        Vector3 forcePos( randSign(), randSign(), randSign() );

        GameModel gameModel( &m_cWorldEnvironment, Vector3( posX, posY, posZ ), Vector3( moment, moment, moment ), mass );
        gameModel.SetCoefficientRestitution( restitution );
        gameModel.SetTerrain( m_systems.terrain.get() );
        gameModel.AddBoundingSphere( radius );
        gameModel.SetImpulseForce( force, forcePos );
        m_cGameModelCollection.AddGameModel( std::move( gameModel ) );
    }

    // --- Box pass ---
    // Box inertia tensor (solid cuboid about centre of mass):
    //   Ix = m/12 * (hy² + hz²),  Iy = m/12 * (hx² + hz²),  Iz = m/12 * (hx² + hy²)
    // where hx, hy, hz are the full extents (2 × half-extents).
    // The spawn code uses half-extents internally, so the factor is m/3 (= m/12 * 4).
    for ( int i = 0; i < boxes; ++i )
    {
        float posX = randFloat( cfg.spawnXBase, cfg.spawnXRange );
        float posY = randFloat( cfg.spawnYBase, cfg.spawnYRange );
        float posZ = randFloat( cfg.spawnZBase, cfg.spawnZRange );
        float mass = randFloat( cfg.ballMassMin, cfg.ballMassRange );
        float restitution = cfg.ballRestitutionMin + static_cast<float>( rand() % cfg.ballRestitutionRange ) / 10.0f;
        Vector3 force( randSigned( cfg.ballForceRange ), randSigned( cfg.ballForceRange ), randSigned( cfg.ballForceRange ) );
        Vector3 forcePos( randSign(), randSign(), randSign() );

        float halfExtent = ( 1.0f + static_cast<float>( rand() % 3 ) ) * 0.6f;
        float hx = halfExtent * ( 0.7f + static_cast<float>( rand() % 4 ) * 0.2f );
        float hy = halfExtent;
        float hz = halfExtent * ( 0.7f + static_cast<float>( rand() % 4 ) * 0.2f );

        float hx2 = hx * hx, hy2 = hy * hy, hz2 = hz * hz;
        float m3 = mass / 3.0f;
        Vector3 inertia( m3 * ( hy2 + hz2 ), m3 * ( hx2 + hz2 ), m3 * ( hx2 + hy2 ) );

        GameModel gameModel( &m_cWorldEnvironment, Vector3( posX, posY, posZ ), inertia, mass );
        gameModel.SetCoefficientRestitution( restitution );
        gameModel.SetTerrain( m_systems.terrain.get() );
        gameModel.AddBoundingBox( Vector3( hx, hy, hz ) );
        gameModel.SetImpulseForce( force, forcePos );
        m_cGameModelCollection.AddGameModel( std::move( gameModel ) );
    }

    m_scene.modelCount = balls + boxes;
}


void SkullbonezRun::Run()
{
    MSG msg;

    for ( ;; )
    {
        if ( PeekMessage( &msg, nullptr, 0, 0, PM_REMOVE ) )
        {
            if ( msg.message == WM_QUIT )
            {
                break;
            }
            TranslateMessage( &msg );
            DispatchMessage( &msg );
        }
        else
        {
            // find out how many seconds passed during last frame
            double secondsPerFrame = m_timers.frameTimer.GetElapsedTime();

            // Clamp to [0, 0.05] to avoid numerical instability.
            // The lower bound catches the first frame after a scene load where
            // StopTimer() has not yet been called (m_endTime is stale/zero).
            if ( secondsPerFrame < 0.0 )
            {
                secondsPerFrame = 0.0;
            }
            if ( secondsPerFrame > 0.05 )
            {
                secondsPerFrame = 0.05;
            }

            m_timers.frameTimer.StartTimer();
            PROFILE_FRAME_BEGIN();

            // Input
            PROFILE_BEGIN( "Frame/Input" );
            TakeInput();
            PROFILE_END( "Frame/Input" );

            // Automated renderer switch timer (--switch-interval N)
            if ( m_debug.rendererSwitchInterval > 0.0f )
            {
                m_debug.rendererSwitchAccum += static_cast<float>( secondsPerFrame );
                if ( m_debug.rendererSwitchAccum >= m_debug.rendererSwitchInterval )
                {
                    m_debug.rendererSwitchAccum = 0.0f;
                    SwitchRenderer( GetNextRendererType( GetCurrentRendererType() ) );
                }
            }

            // Physics (fixed-step or variable dt) then per-frame camera/misc — once each.
            if ( !m_scene.isSceneMode || m_scene.isScenePhysics )
            {
                if ( m_scene.isFixedStep )
                {
                    // Deterministic lock-step: exactly one physics tick per render frame.
                    // Ignores wall-clock time entirely — produces identical results every run.
                    if ( !m_camera.isFlyMode || Input::IsKeyDown( VK_SPACE ) )
                    {
                        PROFILE_BEGIN( "Frame/Physics" );
                        m_cGameModelCollection.RunPhysics( PHYSICS_FIXED_DT );
                        PROFILE_END( "Frame/Physics" );
                    }
                    UpdateLogic( PHYSICS_FIXED_DT );
                }
                else
                {
                    float scaledDt = static_cast<float>( secondsPerFrame ) * m_scene.timeScale;

                    if ( !m_camera.isFlyMode || Input::IsKeyDown( VK_SPACE ) )
                    {
                        PROFILE_BEGIN( "Frame/Physics" );
                        if ( m_cGameModelCollection.GetLegacyMode() )
                        {
                            // Legacy swept physics is frame-rate independent: CollisionDetect* computes the
                            // exact time-of-impact and steps to it internally, so external sub-division adds
                            // no benefit and was never used before the accumulator was introduced for the solver.
                            m_timers.physicsAccumulator = 0.0f;
                            m_cGameModelCollection.RunPhysics( scaledDt );
                        }
                        else
                        {
                            // Impulse solver uses discrete overlap tests and needs small fixed steps for stability.
                            // Only RunPhysics runs in the loop — camera and misc update once per frame below.
                            m_timers.physicsAccumulator += scaledDt;

                            int steps = 0;
                            while ( m_timers.physicsAccumulator >= PHYSICS_FIXED_DT && steps < PHYSICS_MAX_STEPS_PER_FRAME )
                            {
                                m_cGameModelCollection.RunPhysics( PHYSICS_FIXED_DT );
                                m_timers.physicsAccumulator -= PHYSICS_FIXED_DT;
                                ++steps;
                            }

                            // Drain excess accumulator if we hit the step cap (avoids spiral of death)
                            if ( steps == PHYSICS_MAX_STEPS_PER_FRAME )
                            {
                                m_timers.physicsAccumulator = 0.0f;
                            }
                        }
                        PROFILE_END( "Frame/Physics" );
                    }

                    // Camera movement, tween, auto-cycle, logs — once per frame with real scaled dt
                    UpdateLogic( scaledDt );
                }
            }

            // Drain GPU pipeline before render
            PROFILE_BEGIN( "Frame/PipelineSync" );
            if ( m_runtimeSettings.isPipelineSyncEnabled )
            {
                Gfx().Finish();
            }
            PROFILE_END( "Frame/PipelineSync" );

            // Render
            PROFILE_GPU_BEGIN( "Frame/Render" );
            Render();
            PROFILE_GPU_END( "Frame/Render" );

            // Render overlay text
            if ( !m_scene.isSceneMode || m_scene.isSceneText )
            {
                PROFILE_GPU_BEGIN( "Frame/Text" );
                DrawWindowText( secondsPerFrame );
                PROFILE_GPU_END( "Frame/Text" );
            }

            // Scene mode: check screenshot triggers (read back buffer before swap)
            // screenshot_and_exit: on frame 1, save SCENENAME.bmp to root then quit
            if ( m_scene.isSceneMode && m_screenshot.isScreenshotAndExit && m_scene.currentFrame == 0 )
            {
                const std::string& scenePath = m_sceneQueue[m_scene.currentSceneIndex];
                char outPath[256];
                const char* base = scenePath.c_str();
                const char* slash = strrchr( base, '/' );
                const char* backslash = strrchr( base, '\\' );
                const char* name = slash ? slash + 1 : ( backslash ? backslash + 1 : base );
                char stem[256];
                strcpy_s( stem, sizeof( stem ), name );
                char* dot = strrchr( stem, '.' );
                if ( dot )
                {
                    *dot = '\0';
                }
                sprintf_s( outPath, sizeof( outPath ), "%s.bmp", stem );
                SaveScreenshot( outPath );

                PROFILE_FRAME_END();
                PostQuitMessage( 0 );
                continue;
            }

            if ( m_scene.isSceneMode && m_screenshot.screenshotPath[0] != '\0' && !m_screenshot.isScreenshotSaved )
            {
                bool shouldCapture = false;


                if ( m_screenshot.screenshotFrame > 0 && ( m_scene.currentFrame + 1 ) >= m_screenshot.screenshotFrame )
                {
                    shouldCapture = true;
                }

                if ( m_screenshot.screenshotMs > 0 && m_timers.simulationTimer.GetTimeSinceLastStart() * 1000.0 >= m_screenshot.screenshotMs )
                {
                    shouldCapture = true;
                }

                if ( shouldCapture )
                {
                    SaveScreenshot( m_screenshot.screenshotPath );
                    m_screenshot.isScreenshotSaved = true;

                    // Close profiler frame before scene transition
                    PROFILE_FRAME_END();

                    // Advance to next scene (or exit if done)
                    if ( !AdvanceScene() )
                    {
                        PostQuitMessage( 0 );
                    }
                    continue;
                }
            }

            // Interval capture: save numbered screenshots at regular frame intervals
            if ( m_scene.isSceneMode && m_screenshot.screenshotInterval > 0 && m_screenshot.screenshotDir[0] != '\0' )
            {
                if ( ( m_scene.currentFrame + 1 ) % m_screenshot.screenshotInterval == 0 )
                {
                    ++m_screenshot.intervalCaptureCount;
                    char intervalPath[512];
                    sprintf_s( intervalPath, sizeof( intervalPath ), "%s/capture_%04d.bmp", m_screenshot.screenshotDir, m_screenshot.intervalCaptureCount );
                    SaveScreenshot( intervalPath );
                }
            }

            // Auto-cycle screenshots (scene directive: auto_cycle_interval N).
            // Every N real seconds: screenshot current tracked ball, cycle to next, exit when all done.
            if ( m_scene.isSceneMode && m_camera.autoCycleInterval > 0.0f && m_camera.autoCycleAccum >= m_camera.autoCycleInterval )
            {
                int ballCount = m_cGameModelCollection.GetModelCount();
                char shotPath[256];
                sprintf_s( shotPath, sizeof( shotPath ), "Profile/cardinal_ball%d.bmp", m_camera.autoCycleShotsTaken );
                SaveScreenshot( shotPath );
                fprintf( stdout, "Auto-shot %d: ball index %d -> %s\n", m_camera.autoCycleShotsTaken, m_camera.trackBallIndex, shotPath );
                fflush( stdout );

                ++m_camera.autoCycleShotsTaken;
                m_camera.autoCycleAccum = 0.0f;

                if ( m_camera.autoCycleShotsTaken >= ballCount )
                {
                    // All balls captured ? done
                    PostQuitMessage( 0 );
                }
                else
                {
                    m_camera.trackBallIndex = ( m_camera.trackBallIndex + 1 ) % ballCount;
                }
            }

            // Swap back buffer
            PROFILE_BEGIN( "Frame/VsyncWait" );
            Gfx().Present();
            PROFILE_END( "Frame/VsyncWait" );

            m_timers.frameTimer.StopTimer();

            // Close profiler frame and refresh timing fields
            PROFILE_FRAME_END();
#if defined( SKULLBONEZ_PROFILE_ENABLED )
            {
                using SkullbonezCore::Basics::Profiler;
                static constexpr uint32_t kPhysicsHash = ::HashStr( "Frame/Physics" );
                static constexpr uint32_t kRenderHash = ::HashStr( "Frame/Render" );
                m_timers.physicsTime = Profiler::Instance().LastFrameMsByHash( kPhysicsHash ) * 0.001f;
                m_timers.renderTime = Profiler::Instance().LastFrameMsByHash( kRenderHash ) * 0.001f;
            }
#endif

            // Perf test: log per-frame timing + periodic memory
            if ( m_perfLogState.isPerfTest && m_perfLogState.perfLogFile )
            {
#if defined( SKULLBONEZ_PROFILE_ENABLED )
                if ( !m_perfLogState.perfHeaderWritten )
                {
                    Profiler::Instance().WritePerfCSVHeader( m_perfLogState.perfLogFile );
                    m_perfLogState.perfHeaderWritten = true;
                }
                Profiler::Instance().WritePerfCSVRow( m_perfLogState.perfLogFile, sPerfPass + 1, m_scene.currentFrame + 1 );
#else
                fprintf( m_perfLogState.perfLogFile, "%d,%d,%.4f,%.4f\n", sPerfPass + 1, m_scene.currentFrame + 1, m_timers.physicsTime * 1000.0f, m_timers.renderTime * 1000.0f );
#endif
                ++m_perfLogState.perfLogWritesSinceFlush;
                if ( m_perfLogState.isPerfLogFlushEnabled ||
                     ( m_perfLogState.perfLogFlushInterval > 0 && m_perfLogState.perfLogWritesSinceFlush >= m_perfLogState.perfLogFlushInterval ) )
                {
                    fflush( m_perfLogState.perfLogFile );
                    m_perfLogState.perfLogWritesSinceFlush = 0;
                }

                // Log memory every 60 frames (~1 second)
                if ( ( m_scene.currentFrame + 1 ) % 60 == 0 )
                {
                    LogPerfMemory( "periodic" );
                }
            }

            // Scene mode: count frames
            if ( m_scene.isSceneMode )
            {
                ++m_scene.currentFrame;
            }

            // Scene mode: exit or hold after target frame count reached (skip if screenshot auto-exit pending)
            if ( m_scene.isSceneMode && m_scene.targetFrameCount > 0 && !m_screenshot.isScreenshotSaved )
            {
                if ( m_scene.currentFrame >= m_scene.targetFrameCount )
                {
                    if ( m_scene.isExitOnComplete )
                    {
                        // Auto-exit: advance to next scene or quit if none remain
                        if ( !AdvanceScene() )
                        {
                            PostQuitMessage( 0 );
                        }
                        continue;
                    }

                    for ( ;; )
                    {
                        MSG holdMsg;
                        if ( PeekMessage( &holdMsg, nullptr, 0, 0, PM_REMOVE ) )
                        {
                            if ( holdMsg.message == WM_QUIT )
                            {
                                return;
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

            // Legacy mode: restart scene after 20s (keeps app running indefinitely)
            if ( !m_scene.isSceneMode && !m_camera.isFlyMode && m_timers.simulationTimer.GetTimeSinceLastStart() > 20.0 )
            {
                // Reload the same scene to restart
                LoadScene( m_scene.currentSceneIndex );
                m_timers.simulationTimer.StartTimer();
                continue;
            }

            // Perf test: advance at 5s (pass 1 restarts same scene, pass 2 advances)
            if ( m_perfLogState.isPerfTest && m_timers.simulationTimer.GetTimeSinceLastStart() > 5.0 )
            {
                if ( !AdvanceScene() )
                {
                    PostQuitMessage( 0 );
                }
                continue;
            }
        }
    }
}


void SkullbonezRun::TakeInput()
{
    // Toggle fly mode with F (edge-detected so snapshot-loaded fly mode survives the next frame)
    bool prevFlyMode = m_camera.isFlyMode;
    bool fNow = Input::IsKeyDown( 'F' );
    if ( fNow && !m_camera.input.fFWasDown )
    {
        m_camera.isFlyMode = !m_camera.isFlyMode;
    }
    m_camera.input.fFWasDown = fNow;

    if ( m_camera.isFlyMode != prevFlyMode )
    {
        if ( m_camera.isFlyMode )
        {
            // Entering fly mode: in legacy mode snap to free camera; in scene mode stay
            // on the current camera so fly controls work without requiring CAMERA_FREE
            if ( !m_scene.isSceneMode )
            {
                m_systems.cameras->SelectCamera( CAMERA_FREE, false );
            }
            m_camera.cameraTime = 0.0f;
            XZBounds unbounded;
            unbounded.m_xMin = -99999.9f;
            unbounded.m_xMax = 99999.9f;
            unbounded.m_zMin = -99999.9f;
            unbounded.m_zMax = 99999.9f;
            uint32_t activeCam = m_scene.isSceneMode ? m_systems.cameras->GetSelectedCameraName() : CAMERA_FREE;
            m_systems.cameras->SetCameraXZBounds( activeCam, unbounded );
            SetCursor( nullptr );
            Input::CentreMouseCoordinates();
            m_camera.input.xMove = 0;
            m_camera.input.yMove = 0;
        }
        else
        {
            // Exiting fly mode: restore m_terrain XZ bounds, cursor, camera cycle clock
            uint32_t activeCam = m_scene.isSceneMode ? m_systems.cameras->GetSelectedCameraName() : CAMERA_FREE;
            m_systems.cameras->SetCameraXZBounds( activeCam, m_systems.terrain->GetXZBounds() );
            SetCursor( LoadCursor( nullptr, IDC_ARROW ) );
            m_camera.cameraTime = 0.0f;
        }
    }

    // Water m_shader debug toggles
    bool prevFreeze = m_debug.isWaterFreezeDebug;
    m_debug.isWaterFreezeDebug = ( Input::IsKeyToggled( '1' ) != 0 ); // Water perturbation ON
    if ( m_debug.isWaterFreezeDebug && !prevFreeze )
    {
        m_debug.frozenWaterTime = static_cast<float>( m_timers.simulationTimer.GetTimeSinceLastStart() );
    }
    m_debug.isWaterNoReflect = ( Input::IsKeyToggled( '2' ) != 0 ); // Reflection default ON
    m_debug.isWaterFlatDebug = ( Input::IsKeyToggled( '3' ) != 0 ); // Ocean wave displacement ON
    m_debug.isTerrainHidden = ( Input::IsKeyToggled( '4' ) != 0 );  // Terrain visibility ON
    m_debug.isWaterHidden = ( Input::IsKeyToggled( '5' ) != 0 );    // Water visibility ON
    // Debug vectors: in scene mode, start from the scene-loaded value and edge-detect '9' toggles.
    // In legacy mode, mirror the Windows key-toggle state.
    if ( m_scene.isSceneMode )
    {
        if ( Input::IsKeyDown( '9' ) && !m_camera.input.f9WasDown )
        {
            m_debug.isDebugVectors = !m_debug.isDebugVectors;
        }
        m_camera.input.f9WasDown = Input::IsKeyDown( '9' );
    }
    else
    {
        m_debug.isDebugVectors = ( Input::IsKeyToggled( '9' ) != 0 );
    }

    // R key: cycle render backend at runtime while preserving current simulation state (GL → DX11 → DX12 → GL).
    {
        bool isRNow = Input::IsKeyDown( 'R' );
        if ( isRNow && !m_camera.input.fRKeyWasDown )
        {
            SwitchRenderer( GetNextRendererType( GetCurrentRendererType() ) );
        }
        m_camera.input.fRKeyWasDown = isRNow;
    }

    // G key: cycle tracked ball index in scene mode (when ball tracking is active).
    bool isGNow = Input::IsKeyDown( 'G' );
    if ( isGNow && !m_camera.input.fGKeyWasDown )
    {
        if ( m_scene.isSceneMode && m_camera.trackBallIndex >= 0 )
        {
            int count = m_cGameModelCollection.GetModelCount();
            if ( count > 0 )
            {
                m_camera.trackBallIndex = ( m_camera.trackBallIndex + 1 ) % count;
            }
        }
    }
    m_camera.input.fGKeyWasDown = isGNow;

    // Profiler overlay: same edge-detection pattern as debug vectors ? starts from scene-loaded
    // default in scene mode, mirrors Windows key-toggle state in legacy mode.
    if ( m_scene.isSceneMode )
    {
        if ( Input::IsKeyDown( '0' ) && !m_camera.input.f0WasDown )
        {
            m_debug.isProfilerOverlay = !m_debug.isProfilerOverlay;
        }
        m_camera.input.f0WasDown = Input::IsKeyDown( '0' );
    }
    else
    {
        // Profiler overlay default ON; pressing '0' toggles the OS-level toggle bit, hiding the overlay.
        m_debug.isProfilerOverlay = ( Input::IsKeyToggled( '0' ) == 0 );
    }

    // F2: Save scene snapshot to Scenes/
    {
        bool f2Now = Input::IsKeyDown( VK_F2 );
        if ( f2Now && !m_camera.input.fF2WasDown )
        {
            CreateDirectoryA( "Scenes", nullptr );
            static int sSnapshotSeq = 0;
            bool saved = false;
            for ( int tries = 0; tries < 100 && !saved; ++tries )
            {
                char path[256];
                sprintf_s( path, sizeof( path ), "Scenes\\snapshot_%04d.scene", sSnapshotSeq++ );
                saved = m_cGameModelCollection.SaveSceneSnapshot(
                    path,
                    m_scene.isScenePhysics,
                    m_scene.isSceneText,
                    m_cWorldEnvironment,
                    m_systems.cameras->GetCameraTranslation(),
                    m_systems.cameras->GetCameraView(),
                    m_systems.cameras->GetCameraUp() );
            }
        }
        m_camera.input.fF2WasDown = f2Now;
    }

    // F3: Save screenshot to Screenshots/
    {
        bool f3Now = Input::IsKeyDown( VK_F3 );
        if ( f3Now && !m_camera.input.fF3WasDown )
        {
            CreateDirectoryA( "Screenshots", nullptr );
            static int sScreenshotSeq = 0;
            bool saved = false;
            for ( int tries = 0; tries < 100 && !saved; ++tries )
            {
                char path[256];
                sprintf_s( path, sizeof( path ), "Screenshots\\screenshot_%04d.bmp", sScreenshotSeq++ );
                if ( GetFileAttributesA( path ) == INVALID_FILE_ATTRIBUTES )
                {
                    SaveScreenshot( path );
                    saved = true;
                }
            }
        }
        m_camera.input.fF3WasDown = f3Now;
    }

    // P: toggle between legacy (sphere-only, ad-hoc) and new (sequential impulse) solver at runtime.
    // In legacy mode boxes freeze in place and disappear; they reappear when toggled back.
    {
        bool pNow = Input::IsKeyDown( 'P' );
        if ( pNow && !m_camera.input.fPWasDown )
        {
            m_cGameModelCollection.SetLegacyMode( !m_cGameModelCollection.GetLegacyMode() );
            PROFILE_SCHEDULE_RESET();
        }
        m_camera.input.fPWasDown = pNow;
    }

    if ( m_camera.isFlyMode )
    {
        // Keep cursor hidden every frame ? Windows restores it on WM_SETCURSOR
        SetCursor( nullptr );

        // Mouse look: delta from screen centre
        POINT currentCoords = Input::GetMouseCoordinates();
        Input::CentreMouseCoordinates();
        POINT centreCoords = Input::GetMouseCoordinates();
        m_camera.input.xMove = currentCoords.x - centreCoords.x;
        m_camera.input.yMove = currentCoords.y - centreCoords.y;

        // WASD movement
        m_camera.input.fUp = Input::IsKeyDown( 'W' );
        m_camera.input.fLeft = Input::IsKeyDown( 'A' );
        m_camera.input.fDown = Input::IsKeyDown( 'S' );
        m_camera.input.fRight = Input::IsKeyDown( 'D' );
    }
    else
    {
        m_camera.input.xMove = 0;
        m_camera.input.yMove = 0;
        m_camera.input.fUp = false;
        m_camera.input.fDown = false;
        m_camera.input.fLeft = false;
        m_camera.input.fRight = false;
    }
}


void SkullbonezRun::UpdateLogic( float fSecondsPerFrame )
{
    // Auto-cycle
    if ( m_scene.isSceneMode && m_camera.autoCycleInterval > 0.0f )
    {
        m_camera.autoCycleAccum += fSecondsPerFrame;
    }

    // move the camera based on input
    // (arguments are calculating time based movement quantities)
    MoveCamera( fSecondsPerFrame * Cfg().keySpeed,
                fSecondsPerFrame * Cfg().mouseSensitivity );

    // update camera tweening speed
    m_systems.cameras->SetTweenSpeed( Cfg().cameraTweenRate * fSecondsPerFrame );
}


void SkullbonezRun::Render()
{
    // Clear screen pixel and depth into buffers
    Gfx().Clear( true, true );

    // In text_only mode all 3D rendering is skipped ? DrawWindowText handles the display
    if ( m_debug.isTextOnly )
    {
        return;
    }

    // renders camera views etc
    SetViewingOrientation();

    // set the camera into its m_position
    m_systems.cameras->SetCamera();

    // now camera rotation has been done, draw OpenGL primitives
    DrawPrimitives();
}


void SkullbonezRun::DrawPrimitives()
{
    float lightPosition[] = { 200.0f, 400.0f, 1200.0f, 1.0f };

    // Get view and projection matrices from camera/window
    Matrix4 baseView = m_systems.cameras->GetViewMatrix();
    Matrix4 proj = m_systems.window->GetProjectionMatrix();
    Matrix4 reflVP;

    // Camera m_position for skybox placement.  During camera transitions the
    // selected camera is already the destination, but SetCamera() renders from
    // the interpolated tween camera.  Reflection math must use the same render
    // camera as baseView; otherwise the mirror pass is generated from the
    // destination camera while the water surface samples it from the in-between
    // camera, which stretches reflected balls during transitions.
    Vector3 eye = m_systems.cameras->GetRenderCameraTranslation();

    // render skybox ------------------------------
    PROFILE_GPU_BEGIN( "Frame/Render/Skybox" );
    Matrix4 skyView = baseView * Matrix4::Translate( eye.x, Cfg().skyboxRenderHeight, eye.z ) * Matrix4::Scale( Cfg().skyboxScale );
    m_systems.skyBox->Render( skyView, proj );
    PROFILE_GPU_END( "Frame/Render/Skybox" );

    // reflection pre-pass: render above-water scene from mirrored camera into FBO (or DXR dispatch)
    PROFILE_GPU_BEGIN( "Frame/Render/Reflection" );
    float waterY = m_cWorldEnvironment.GetFluidSurfaceHeight();
    Vector3 center = m_systems.cameras->GetRenderCameraView();

    // Mirror eye and look-at target about the water plane; flip up vector
    Vector3 reflEye( eye.x, 2.0f * waterY - eye.y, eye.z );
    Vector3 reflCenter( center.x, 2.0f * waterY - center.y, center.z );
    Vector3 up = m_systems.cameras->GetRenderCameraUp();
    Vector3 reflUp( up.x, -up.y, up.z );
    Matrix4 reflView = Matrix4::LookAt( reflEye, reflCenter, reflUp );
    reflVP = proj * reflView;

    if ( Gfx().IsDXRSupported() && !m_debug.isWaterNoReflect )
    {
        // DXR path: rebuild TLAS with current ball positions, then dispatch rays
        int ballCount = m_cGameModelCollection.GetModelCount();
        std::vector<float> transforms( (size_t)ballCount * 16 );
        for ( int i = 0; i < ballCount; ++i )
        {
            Matrix4 mdlMat = m_cGameModelCollection.GetModelAtIndex( i ).GetModelMatrix();
            memcpy( transforms.data() + i * 16, mdlMat.Data(), 16 * sizeof( float ) );
        }

        Gfx().BuildTLAS( transforms.data(), ballCount, 0, 0 ); // BLAS VAs retrieved internally

        // Compute inverse VP matrix for ray reconstruction
        Matrix4 vp = proj * baseView;
        Matrix4 invVP = vp.Inverse();
        float cameraPos[3] = { eye.x, eye.y, eye.z };
        float simTime = static_cast<float>( m_timers.simulationTimer.GetTotalTime() );

        uint32_t sphereHandle = m_systems.textures->GetTextureHandle( TEXTURE_BOUNDING_SPHERE );
        uint32_t terrainHandle = m_systems.textures->GetTextureHandle( TEXTURE_GROUND );
        uint32_t skyUpHandle = m_systems.textures->GetTextureHandle( TEXTURE_SKY_UP );
        uint32_t skyDownHandle = m_systems.textures->GetTextureHandle( TEXTURE_SKY_DOWN );
        uint32_t skyRightHandle = m_systems.textures->GetTextureHandle( TEXTURE_SKY_RIGHT );
        uint32_t skyLeftHandle = m_systems.textures->GetTextureHandle( TEXTURE_SKY_LEFT );
        uint32_t skyFrontHandle = m_systems.textures->GetTextureHandle( TEXTURE_SKY_FRONT );
        uint32_t skyBackHandle = m_systems.textures->GetTextureHandle( TEXTURE_SKY_BACK );
        Gfx().DispatchReflectionRays( invVP.Data(), cameraPos, waterY, simTime, lightPosition, m_systems.window->m_sWindowDimensions.x * 2, m_systems.window->m_sWindowDimensions.y * 2, sphereHandle, terrainHandle, skyUpHandle, skyDownHandle, skyRightHandle, skyLeftHandle, skyFrontHandle, skyBackHandle );
    }
    else
    {
        // FBO mirror-camera path (GL, DX11, or DXR fallback)
        m_systems.reflectionFBO->Bind();
        Gfx().SetViewport( 0, 0, m_systems.reflectionFBO->GetWidth(), m_systems.reflectionFBO->GetHeight() );
        Gfx().Clear( true, true );

        // Skybox reflected (XZ follows eye; Y anchored at Cfg().skyboxRenderHeight)
        PROFILE_GPU_BEGIN( "Frame/Render/Reflection/Skybox" );
        Matrix4 skyReflView = reflView * Matrix4::Translate( eye.x, Cfg().skyboxRenderHeight, eye.z ) * Matrix4::Scale( Cfg().skyboxScale );
        m_systems.skyBox->Render( skyReflView, proj );
        PROFILE_GPU_END( "Frame/Render/Reflection/Skybox" );

        // Game models reflected ? clip at water surface (above-water portion only)
        PROFILE_GPU_BEGIN( "Frame/Render/Reflection/Balls" );
        Gfx().SetClipPlane( 0, true );
        SkullbonezHelper::SetClipPlane( 0.0f, 1.0f, 0.0f, -waterY );
        m_systems.textures->SelectTexture( TEXTURE_BOUNDING_SPHERE );
        m_cGameModelCollection.RenderModels( reflView, proj, lightPosition );
        Gfx().SetClipPlane( 0, false );
        SkullbonezHelper::SetClipPlane( 0.0f, 1.0f, 0.0f, 1.0e9f );
        PROFILE_GPU_END( "Frame/Render/Reflection/Balls" );

        m_systems.reflectionFBO->Unbind();
        Gfx().SetViewport( 0, 0, m_systems.window->m_sWindowDimensions.x, m_systems.window->m_sWindowDimensions.y );
    }
    PROFILE_GPU_END( "Frame/Render/Reflection" );

    // render game models -----------------------------
    PROFILE_GPU_BEGIN( "Frame/Render/Balls" );
    m_systems.textures->SelectTexture( TEXTURE_BOUNDING_SPHERE );
    m_cGameModelCollection.RenderModels( baseView, proj, lightPosition );
    PROFILE_GPU_END( "Frame/Render/Balls" );

    // render m_terrain ------------------------------
    if ( !m_debug.isTerrainHidden )
    {
        PROFILE_GPU_BEGIN( "Frame/Render/Terrain" );
        m_systems.textures->SelectTexture( TEXTURE_GROUND );
        m_systems.terrain->Render( baseView, proj, lightPosition );
        PROFILE_GPU_END( "Frame/Render/Terrain" );
    }

    // render ground shadows on top of m_terrain
    if ( !m_debug.isTerrainHidden )
    {
        PROFILE_GPU_BEGIN( "Frame/Render/Shadows" );
        m_cGameModelCollection.RenderShadows( m_systems.terrain.get(), baseView, proj, waterY );
        PROFILE_GPU_END( "Frame/Render/Shadows" );
    }

    // render the fluid ---------------------------
    if ( !m_debug.isWaterHidden )
    {
        PROFILE_GPU_BEGIN( "Frame/Render/Water" );
        float waterTime = m_debug.isWaterFreezeDebug
                              ? m_debug.frozenWaterTime
                              : static_cast<float>( m_timers.simulationTimer.GetTimeSinceLastStart() );
        uint32_t reflTex = ( Gfx().IsDXRSupported() && !m_debug.isWaterNoReflect )
                               ? Gfx().GetReflectionUAVTexture()
                               : m_systems.reflectionFBO->GetColorTextureHandle();
        // DXR reflection texture is in main-camera screen space, so sample it
        // using the main VP ? not the mirror VP used by the FBO path.
        Matrix4 waterSampleVP = ( Gfx().IsDXRSupported() && !m_debug.isWaterNoReflect )
                                    ? proj * baseView
                                    : reflVP;
        m_cWorldEnvironment.RenderFluid( baseView, proj, waterSampleVP, waterTime, reflTex, m_debug.isWaterFlatDebug, m_debug.isWaterNoReflect );
        PROFILE_GPU_END( "Frame/Render/Water" );
    }

    // debug vector overlay ? GL only, toggled with V (or debug_vectors in scene)
    //   green  = Travel Vector (velocity, scaled)
    //   red    = Roll Axis Vector (angular velocity, scaled)
    //   white  = Pole Vector outside tolerance
    //   blue   = Pole Vector within tolerance (perpendicular to red within 5?)
    if ( m_debug.isDebugVectors )
    {
        Matrix4 viewProj = proj * baseView;
        std::vector<std::pair<Vector3, Vector3>> velLines;
        std::vector<std::pair<Vector3, Vector3>> omegaLines;
        std::vector<std::pair<Vector3, Vector3>> upAlignedLines;
        std::vector<std::pair<Vector3, Vector3>> upErrorLines;
        const float axisToleranceRad = 5.0f * _PI / 180.0f;
        int modelCount = m_cGameModelCollection.GetModelCount();
        for ( int i = 0; i < modelCount; ++i )
        {
            GameModel& mdl = m_cGameModelCollection.GetModelAtIndex( i );
            Vector3 pos = mdl.GetPosition();
            Vector3 vel = mdl.GetVelocity();
            Vector3 omega = mdl.GetAngularVelocity();
            const float velScale = 0.5f;
            const float omegaScale = 2.0f;
            velLines.push_back( { pos, pos + vel * velScale } );
            omegaLines.push_back( { pos, pos + omega * omegaScale } );

            // White spike: starts at sphere centre, points along the visual "up" axis.
            // Length = 2.5? radius so it clearly protrudes above the ball surface.
            Vector3 orientUp = mdl.GetOrientationUp();
            float radius = mdl.GetBoundingRadius();
            bool isWithinPlaneTolerance = false;
            float omegaMag = Vector::VectorMag( omega );
            float orientUpMag = Vector::VectorMag( orientUp );
            if ( omegaMag > TOLERANCE && orientUpMag > TOLERANCE )
            {
                float dotRed = ( orientUp * omega ) / ( orientUpMag * omegaMag );
                if ( dotRed > 1.0f )
                {
                    dotRed = 1.0f;
                }
                else if ( dotRed < -1.0f )
                {
                    dotRed = -1.0f;
                }
                float redPerpDeviationRad = asinf( fabsf( dotRed ) );
                isWithinPlaneTolerance = ( redPerpDeviationRad <= axisToleranceRad );
            }

            Vector3 upLineEnd = pos + orientUp * ( radius * 2.5f );
            if ( isWithinPlaneTolerance )
            {
                upAlignedLines.push_back( { pos, upLineEnd } );
            }
            else
            {
                upErrorLines.push_back( { pos, upLineEnd } );
            }
        }
        SkullbonezHelper::DrawDebugVectors( viewProj, velLines, 0.0f, 1.0f, 0.0f );
        SkullbonezHelper::DrawDebugVectors( viewProj, omegaLines, 1.0f, 0.0f, 0.0f );
        SkullbonezHelper::DrawDebugVectors( viewProj, upAlignedLines, 0.0f, 0.0f, 1.0f );
        SkullbonezHelper::DrawDebugVectors( viewProj, upErrorLines, 1.0f, 1.0f, 1.0f );
    }
}


void SkullbonezRun::SetUpCameras()
{
    m_systems.cameras = CameraCollection::Instance();

    m_systems.cameras->AddCamera( Vector3( 321.0f, 110.0f, 557.0f ), // Position
                                  Vector3( 581.0f, 40.0f, 633.0f ),  // View
                                  Vector3( 0.0f, 1.0f, 0.0f ),       // Up
                                  CAMERA_GAME_MODEL_1 );

    m_systems.cameras->AddCamera( Vector3( 730.0f, 100.0f, 380.0f ), // Position
                                  Vector3( 709.0f, 92.0f, 482.0f ),  // View
                                  Vector3( 0.0f, 1.0f, 0.0f ),       // Up
                                  CAMERA_GAME_MODEL_2 );

    m_systems.cameras->AddCamera( Vector3( 900.0f, 110.0f, 900.0f ), // Position
                                  Vector3( 313.0f, 31.0f, 282.0f ),  // View
                                  Vector3( 0.0f, 1.0f, 0.0f ),       // Up
                                  CAMERA_FREE );

    // set the camera m_boundaries
    m_systems.cameras->SetCameraXZBounds( m_systems.terrain->GetXZBounds() );

    // set the m_terrain
    m_systems.cameras->SetTerrain( m_systems.terrain.get() );

    // lock the m_cameras
    m_systems.cameras->SetLockedMode( true );
}


void SkullbonezRun::SetInitialOpenGlState()
{
    SkullbonezHelper::ResetGLResources();

    // load m_textures
    const SkullbonezConfig& cfg = Cfg();
    m_systems.textures->CreateJpegTexture( ( std::string( DATA_ROOT ) + cfg.terrainTexture ).c_str(), TEXTURE_GROUND );
    m_systems.textures->CreateJpegTexture( ( std::string( DATA_ROOT ) + cfg.sphereTexture ).c_str(), TEXTURE_BOUNDING_SPHERE );
}


void SkullbonezRun::DrawWindowText( const double dSecondsPerFrame )
{
    // update timers
    m_timers.updateTimer.StopTimer();
    m_timers.timeSinceLastRender += static_cast<float>( m_timers.updateTimer.GetElapsedTime() );
    m_timers.updateTimer.StartTimer();

    // if half a second has passed
    if ( m_timers.timeSinceLastRender > 0.5f )
    {
        if ( dSecondsPerFrame )
        {
            // update the display information
            m_timers.rollingFpsTime = 1.0f / static_cast<float>( dSecondsPerFrame );
            m_timers.rollingPhysicsTime = m_timers.physicsTime;
            m_timers.rollingRenderTime = m_timers.renderTime;
        }

        // reset time since last render
        m_timers.timeSinceLastRender = 0.0f;
    }

    // TOP - show renderer type
    const char* rendererName = Gfx().GetRendererName();

    // text_only mode: solid background + full-screen pangram, no HUD/profiler
    if ( m_debug.isTextOnly )
    {
        // Dark background covering the full viewport
        Text2d::Render2dQuad( -0.55f, -0.45f, 0.55f, 0.45f, 0.08f, 0.08f, 0.12f, 1.0f );

        // Three rows of the pangram ? each line uses a slightly different colour
        // so hue/brightness fringing artefacts are visible on all channel combinations
        const float sz = 0.09f;
        Text2d::Render2dTextColor( -0.46f, 0.22f, sz, 1.00f, 1.00f, 1.00f, "The quick brown fox" );
        Text2d::Render2dTextColor( -0.46f, 0.07f, sz, 1.00f, 0.90f, 0.20f, "jumps over the" );
        Text2d::Render2dTextColor( -0.46f, -0.08f, sz, 0.40f, 0.90f, 1.00f, "lazy dog" );

        // Renderer name in small text at bottom so we know which backend we're looking at
        Text2d::Render2dTextColor( -0.46f, -0.38f, 0.015f, 0.60f, 0.60f, 0.60f, "renderer: %s", rendererName );

        Text2d::FlushText();
        return;
    }

    const float hw = Text2d::HalfW();
    const float hh = Text2d::HalfH();
    const float mX = 0.022f; // horizontal inset from left/right edge
    const float mY = 0.015f; // vertical inset from top/bottom edge
    const float fSz = 0.015f;

    // Top-left
    Text2d::Render2dText( -( hw - mX ), hh - mY - fSz, fSz, "SKULLBONEZ CORE [%s]", rendererName );

    // Top-right: measure the formatted string so it ends just inside the right edge
    char mcBuf[64];
    snprintf( mcBuf, sizeof( mcBuf ), "Model Count: %i", m_scene.modelCount );
    Text2d::Render2dText( hw - mX - Text2d::MeasureText( fSz, mcBuf ), hh - mY - fSz, fSz, "%s", mcBuf );

    // Second row top-right: show active physics solver (toggle with P)
    {
        const char* solverTag = m_cGameModelCollection.GetLegacyMode() ? "PHYSICS: LEGACY [P]" : "PHYSICS: IMPULSE [P]";
        Text2d::Render2dText( hw - mX - Text2d::MeasureText( fSz, solverTag ), hh - mY - fSz * 3.0f, fSz, "%s", solverTag );
    }

    // Profiler overlay ? bottom-right anchored.
    // Compiled out in Release; toggleable with '0' in Debug/Profile.
#if defined( SKULLBONEZ_PROFILE_ENABLED )
    if ( m_debug.isProfilerOverlay )
    {
        const float lineH = 0.018f;
        const float profFSz = 0.012f;
        const float padY = lineH * 1.2f;
        Profiler::Instance().RenderOverlay( -( hw - mX ), -( hh - mY ) - padY, lineH, profFSz, m_timers.rollingFpsTime );
    }
#endif

    // Flush all accumulated text quads in a single GPU upload+draw call.
    Text2d::FlushText();
}


void SkullbonezRun::SetViewingOrientation()
{
    // In scene mode, use the first camera without cycling.
    // If ball-tracking is active, keep the camera locked onto the selected ball.
    if ( m_scene.isSceneMode )
    {
        if ( m_camera.trackBallIndex >= 0 && m_camera.trackBallIndex < m_cGameModelCollection.GetModelCount() )
        {
            Vector3 ballPos = m_cGameModelCollection.GetModelPosition( m_camera.trackBallIndex );
            m_systems.cameras->SetPrimaryPosition( Vector3( ballPos.x, ballPos.y + m_camera.trackHeight, ballPos.z ) );
            m_systems.cameras->SetViewCoordinates( ballPos );
        }
        return;
    }

    // In fly mode, freeze the cycle clock and keep the free camera
    if ( m_camera.isFlyMode )
    {
        m_camera.cameraTime = 0.0f;
        m_timers.cameraTimer.StopTimer();
        m_timers.cameraTimer.StartTimer();
        return;
    }

    // set viewing m_orientation
    /*
        if(Input::IsKeyDown('1')) m_camera.selectedCamera = 0;
        if(Input::IsKeyDown('2')) m_camera.selectedCamera = 1;
        if(Input::IsKeyDown('3')) m_camera.selectedCamera = 2;
    */

    // maintain the camera timer
    m_timers.cameraTimer.StopTimer();
    m_camera.cameraTime += static_cast<float>( m_timers.cameraTimer.GetElapsedTime() );
    m_timers.cameraTimer.StartTimer();

    // change the viewing camera automatically
    if ( m_camera.cameraTime > 5.0f )
    {
        ++m_camera.selectedCamera;
        if ( m_camera.selectedCamera == 3 )
        {
            m_camera.selectedCamera = 0;
        }
        m_camera.cameraTime = 0.0f;
    }

    // select camera based on input
    switch ( m_camera.selectedCamera )
    {
    case 0:
        m_systems.cameras->SelectCamera( CAMERA_GAME_MODEL_1, true );
        break;
    case 1:
        m_systems.cameras->SelectCamera( CAMERA_GAME_MODEL_2, true );
        break;
    case 2:
        m_systems.cameras->SelectCamera( CAMERA_FREE, true );
        break;
    }

    // set the view m_position of the selected camera based on the game model m_position
    if ( m_systems.cameras->IsCameraSelected( CAMERA_GAME_MODEL_1 ) )
    {
        m_systems.cameras->SetViewCoordinates( m_cGameModelCollection.GetModelPosition( 0 ) );
    }
    if ( m_systems.cameras->IsCameraSelected( CAMERA_GAME_MODEL_2 ) )
    {
        m_systems.cameras->SetViewCoordinates( m_cGameModelCollection.GetModelPosition( 1 ) );
    }

    /*
        // reset relativity when a new request for synchronisation comes in
        if(m_camera.input.fAux1) m_systems.cameras->ResetRelativity();

        // sync m_cameras if in sync mode
        if(m_camera.input.fAux2)
        {
            // perform the relative update
            RelativeUpdateCamera(CAMERA_GAME_MODEL_1);
            RelativeUpdateCamera(CAMERA_GAME_MODEL_2);
            RelativeUpdateCamera(CAMERA_FREE);

            // reset the relative variable as we have already performed the action on desired m_cameras
            m_systems.cameras->ResetRelativity();
        }
    */
}


void SkullbonezRun::RelativeUpdateCamera( uint32_t hash )
{
    if ( !m_systems.cameras->IsCameraSelected( hash ) )
    {
        Vector3 translatedCameraPosition = m_systems.cameras->GetCameraTranslation( hash );
        float minY = m_systems.terrain->GetTerrainHeightAt( translatedCameraPosition.x, translatedCameraPosition.z, true ) + Cfg().minCameraHeight;
        m_systems.cameras->RelativeUpdate( hash, minY, Cfg().maxCameraHeight );
    }
}


void SkullbonezRun::MoveCamera( float keyMovementQty, float mouseMovementQty )
{
    if ( m_camera.isFlyMode )
    {
        // Shift held = 3x speed
        float speedMult = Input::IsKeyDown( VK_SHIFT ) ? 3.0f : 1.0f;

        // Mouse look
        if ( m_camera.input.xMove != 0 || m_camera.input.yMove != 0 )
        {
            m_systems.cameras->RotatePrimary( m_camera.input.xMove * mouseMovementQty,
                                              m_camera.input.yMove * mouseMovementQty );
        }

        // WASD movement
        if ( m_camera.input.fUp )
        {
            m_systems.cameras->MovePrimary( Camera::TravelDirection::Forward, keyMovementQty * speedMult );
        }
        if ( m_camera.input.fLeft )
        {
            m_systems.cameras->MovePrimary( Camera::TravelDirection::Left, keyMovementQty * speedMult );
        }
        if ( m_camera.input.fDown )
        {
            m_systems.cameras->MovePrimary( Camera::TravelDirection::Backward, keyMovementQty * speedMult );
        }
        if ( m_camera.input.fRight )
        {
            m_systems.cameras->MovePrimary( Camera::TravelDirection::Right, keyMovementQty * speedMult );
        }

        m_systems.cameras->ApplyPrimaryMovementBuffer();
    }

    // Clamp camera Y between m_terrain surface and Cfg().maxCameraHeight (not in fly mode, not in scene mode)
    if ( !m_camera.isFlyMode && !m_scene.isSceneMode )
    {
        Vector3 translatedCameraPosition = m_systems.cameras->GetCameraTranslation();
        float minY = m_systems.terrain->GetTerrainHeightAt( translatedCameraPosition.x, translatedCameraPosition.z, true ) + Cfg().minCameraHeight;
        if ( minY > translatedCameraPosition.y )
        {
            m_systems.cameras->AmmendPrimaryY( minY );
        }
        else if ( translatedCameraPosition.y > Cfg().maxCameraHeight )
        {
            m_systems.cameras->AmmendPrimaryY( Cfg().maxCameraHeight );
        }
    }
}


void SkullbonezRun::SetUpCamerasFromScene( const TestScene& scene )
{
    m_systems.cameras = CameraCollection::Instance();

    for ( int i = 0; i < scene.GetCameraCount(); ++i )
    {
        const SceneCamera& cam = scene.GetCamera( i );
        uint32_t hash = HashStr( cam.name );
        m_systems.cameras->AddCamera( cam.m_position, cam.view, cam.up, hash );
    }

    // set the camera m_boundaries
    m_systems.cameras->SetCameraXZBounds( m_systems.terrain->GetXZBounds() );

    // set the m_terrain
    m_systems.cameras->SetTerrain( m_systems.terrain.get() );

    // lock the m_cameras
    m_systems.cameras->SetLockedMode( false );
}


void SkullbonezRun::SetUpGameModelsFromScene( const TestScene& scene )
{
    m_scene.modelCount = scene.GetBallCount() + scene.GetBallStateCount() + scene.GetBoxCount();

    for ( int i = 0; i < scene.GetBallCount(); ++i )
    {
        const SceneBall& ball = scene.GetBall( i );

        GameModel gameModel( &m_cWorldEnvironment,
                             Vector3( ball.posX, ball.posY, ball.posZ ),
                             Vector3( ball.moment, ball.moment, ball.moment ),
                             ball.m_mass );

        gameModel.SetCoefficientRestitution( ball.restitution );
        gameModel.SetTerrain( m_systems.terrain.get() );
        gameModel.SetName( ball.name );
        gameModel.AddBoundingSphere( ball.m_radius );

        // apply initial orientation if specified (euler angles in degrees, XYZ order)
        if ( ball.hasInitOrient )
        {
            gameModel.SetInitialOrientation( ball.eulerX, ball.eulerY, ball.eulerZ );
        }

        // apply force if any is specified
        if ( ball.forceX != 0.0f || ball.forceY != 0.0f || ball.forceZ != 0.0f )
        {
            gameModel.SetImpulseForce(
                Vector3( ball.forceX, ball.forceY, ball.forceZ ),
                Vector3( ball.forcePosX, ball.forcePosY, ball.forcePosZ ) );
        }

        m_cGameModelCollection.AddGameModel( std::move( gameModel ) );
    }

    // ball_state entries: full dynamic state from a snapshot
    for ( int i = 0; i < scene.GetBallStateCount(); ++i )
    {
        const SceneBallState& bs = scene.GetBallState( i );

        GameModel gameModel( &m_cWorldEnvironment,
                             Vector3( bs.posX, bs.posY, bs.posZ ),
                             Vector3( bs.inertiaX, bs.inertiaY, bs.inertiaZ ),
                             bs.mass );

        gameModel.SetCoefficientRestitution( bs.restitution );
        gameModel.SetTerrain( m_systems.terrain.get() );
        gameModel.SetName( bs.name );
        gameModel.AddBoundingSphere( bs.radius );
        gameModel.SetLinearVelocity( Vector3( bs.velX, bs.velY, bs.velZ ) );
        gameModel.SetAngularVelocity( Vector3( bs.angVelX, bs.angVelY, bs.angVelZ ) );
        gameModel.SetOrientation( Quaternion( bs.orientX, bs.orientY, bs.orientZ, bs.orientW ) );

        m_cGameModelCollection.AddGameModel( std::move( gameModel ) );
    }

    // box entries: rigid box entities
    for ( int i = 0; i < scene.GetBoxCount(); ++i )
    {
        const SceneBox& box = scene.GetBox( i );

        // Box inertia: I = m/3 * (hy² + hz²) etc. for half-extents
        float hx2 = box.halfX * box.halfX;
        float hy2 = box.halfY * box.halfY;
        float hz2 = box.halfZ * box.halfZ;
        float m3 = box.mass / 3.0f;
        Vector3 inertia( m3 * ( hy2 + hz2 ), m3 * ( hx2 + hz2 ), m3 * ( hx2 + hy2 ) );

        GameModel gameModel( &m_cWorldEnvironment,
                             Vector3( box.posX, box.posY, box.posZ ),
                             inertia,
                             box.mass );

        gameModel.SetCoefficientRestitution( box.restitution );
        gameModel.SetTerrain( m_systems.terrain.get() );
        gameModel.SetName( box.name );
        gameModel.AddBoundingBox( Vector3( box.halfX, box.halfY, box.halfZ ) );

        if ( box.hasInitOrient )
        {
            gameModel.SetInitialOrientation( box.eulerX, box.eulerY, box.eulerZ );
        }

        m_cGameModelCollection.AddGameModel( std::move( gameModel ) );
    }
}


void SkullbonezRun::LoadScene( int index )
{
    // Flush GPU before destroying scene resources to avoid use-after-free
    if ( IsGfxReady() )
    {
        Gfx().FlushGPU();
    }

    m_scene.currentSceneIndex = index;
    const std::string& scenePath = m_sceneQueue[index];

    // Close previous perf log if open
    if ( m_perfLogState.perfLogFile )
    {
        LogPerfMemory( "end" );
        if ( m_perfLogState.perfLogWritesSinceFlush > 0 )
        {
            fflush( m_perfLogState.perfLogFile );
            m_perfLogState.perfLogWritesSinceFlush = 0;
        }
        fclose( m_perfLogState.perfLogFile );
        m_perfLogState.perfLogFile = nullptr;
    }

    // Reset scene config to defaults
    m_scene.isScenePhysics = true;
    m_scene.isSceneText = true;
    m_perfLogState.isPerfTest = false;
    m_perfLogState.perfHeaderWritten = false;
    m_screenshot.isScreenshotSaved = false;
    m_screenshot.isScreenshotAndExit = false;
    m_scene.targetFrameCount = -1;
    m_scene.currentFrame = 0;
    m_timers.physicsAccumulator = 0.0f;
    m_screenshot.screenshotFrame = -1;
    m_screenshot.screenshotMs = -1;
    m_screenshot.screenshotPath[0] = '\0';
    m_screenshot.screenshotInterval = -1;
    m_screenshot.intervalCaptureCount = 0;
    m_screenshot.screenshotDir[0] = '\0';
    m_perfLogState.perfLogPath[0] = '\0';
    m_perfLogState.isPerfLogFlushEnabled = false;
    m_perfLogState.perfLogFlushInterval = 0;
    m_perfLogState.perfLogWritesSinceFlush = 0;
    m_runtimeSettings.isVsyncEnabled = Cfg().runtimeRender.vsyncEnabled;
    m_runtimeSettings.isPipelineSyncEnabled = Cfg().runtimeRender.forcePipelineSync;
    m_runtimeSettings.isRollAlignEnabled = m_runtimeSettings.defaultRollAlignEnabled;
    Cfg().rollAlignEnabled = m_runtimeSettings.isRollAlignEnabled;

    // Reset cameras and game models
    m_systems.cameras->Reset();
    m_cGameModelCollection.Clear();

    // Reset input and debug state
    m_camera.isFlyMode = false;
    m_debug.isWaterFreezeDebug = false;
    m_debug.isWaterNoReflect = false;
    m_debug.isWaterFlatDebug = false;
    m_debug.isTerrainHidden = false;
    m_debug.isWaterHidden = false;
    m_debug.isDebugVectors = false;
    m_debug.isTextOnly = false;
    m_scene.timeScale = 1.0f;
    m_scene.isFixedStep = false;
    m_scene.isExitOnComplete = false;
    m_debug.frozenWaterTime = 0.0f;
    m_camera.trackBallIndex = -1;
    m_camera.trackHeight = 300.0f;
    m_camera.autoCycleInterval = -1.0f;
    m_camera.autoCycleAccum = 0.0f;
    m_camera.autoCycleShotsTaken = 0;
    m_camera.input = {};
    m_debug.isProfilerOverlay = true;
    m_camera.selectedCamera = 0;

    // Reset timing
    m_timers.timeSinceLastRender = 0.0f;
    m_timers.renderTime = 0.0f;
    m_camera.cameraTime = 0.0f;
    m_timers.rollingRenderTime = 0.0f;
    m_timers.physicsTime = 0.0f;
    m_timers.rollingPhysicsTime = 0.0f;
    m_timers.rollingFpsTime = 0.0f;

    // Reseed RNG
    srand( static_cast<unsigned>( time( nullptr ) ) );

    // Branch on scene mode vs legacy mode
    if ( scenePath.empty() )
    {
        m_scene.isSceneMode = false;
        SetUpCameras();
        SetUpGameModels( DEFAULT_GAME_MODELS );
        const char* rendererName = Gfx().GetRendererName();
        char titleText[256];
        sprintf_s( titleText, "%s [%s]", TITLE_TEXT, rendererName );
        m_systems.window->SetTitleText( titleText );
    }
    else
    {
        m_scene.isSceneMode = true;
        TestScene scene = TestScene::LoadFromFile( scenePath.c_str() );
        m_scene.isScenePhysics = scene.IsPhysicsEnabled();
        m_scene.isSceneText = scene.IsTextEnabled();
        m_debug.isDebugVectors = scene.IsDebugVectors();
        m_perfLogState.isPerfLogFlushEnabled = scene.IsPerfLogFlushEnabled();
        m_perfLogState.perfLogFlushInterval = scene.GetPerfLogFlushInterval();
        if ( scene.HasVsyncOverride() )
        {
            m_runtimeSettings.isVsyncEnabled = scene.IsVsyncEnabled();
        }
        if ( scene.HasPipelineSyncOverride() )
        {
            m_runtimeSettings.isPipelineSyncEnabled = scene.IsPipelineSyncEnabled();
        }
        if ( scene.HasRollAlignOverride() )
        {
            m_runtimeSettings.isRollAlignEnabled = scene.IsRollAlignEnabled();
        }
        Cfg().rollAlignEnabled = m_runtimeSettings.isRollAlignEnabled;
        m_debug.isTextOnly = scene.IsTextOnly();
        m_debug.isWaterHidden = scene.IsWaterHidden();
        m_debug.isTerrainHidden = scene.IsTerrainHidden();
        m_scene.timeScale = scene.GetTimeScale();
        m_scene.isFixedStep = scene.IsFixedStep();

        // Apply per-scene physics mode override — supersedes the --legacy CLI flag for this scene.
        // physicsMode 0 = inherit (no change), 1 = legacy, 2 = impulse solver.
        if ( scene.GetPhysicsMode() != 0 )
        {
            m_cGameModelCollection.SetLegacyMode( scene.GetPhysicsMode() == 1 );
        }

        m_scene.targetFrameCount = scene.GetFrameCount();
        m_scene.isExitOnComplete = scene.IsExitOnComplete();
        m_screenshot.screenshotFrame = scene.GetScreenshotFrame();
        m_screenshot.screenshotMs = scene.GetScreenshotMs();
        m_screenshot.isScreenshotAndExit = scene.IsScreenshotAndExit();

        if ( scene.GetScreenshotPath()[0] != '\0' )
        {
            strcpy_s( m_screenshot.screenshotPath, sizeof( m_screenshot.screenshotPath ), scene.GetScreenshotPath() );
        }
        // Interval capture: create output directory
        m_screenshot.screenshotInterval = scene.GetScreenshotInterval();
        if ( scene.GetScreenshotDir()[0] != '\0' )
        {
            strcpy_s( m_screenshot.screenshotDir, sizeof( m_screenshot.screenshotDir ), scene.GetScreenshotDir() );
            CreateDirectoryA( m_screenshot.screenshotDir, nullptr );
        }

        // Perf test: open CSV log file
        const char* pPerfPath = scene.GetPerfLogPath();
        if ( pPerfPath[0] != '\0' )
        {
            m_perfLogState.isPerfTest = true;
            strcpy_s( m_perfLogState.perfLogPath, sizeof( m_perfLogState.perfLogPath ), pPerfPath );
            const char* mode = ( sPerfPass == 0 ) ? "w" : "a";
            fopen_s( &m_perfLogState.perfLogFile, m_perfLogState.perfLogPath, mode );
            if ( m_perfLogState.perfLogFile )
            {
                m_perfLogState.perfLogWritesSinceFlush = 0;
                LogPerfMemory( "start" );
            }
        }

        // Physics log: per-frame ball state CSV. CLI --physics-log override takes priority over scene directive.
#ifdef _DEBUG
        const char* physLogPath = ( m_perfLogState.physicsLogOverride[0] != '\0' )
                                      ? m_perfLogState.physicsLogOverride
                                      : scene.GetPhysicsLogPath();
        m_cGameModelCollection.SetPhysicsLogPath( physLogPath );
#endif

        // Override RNG seed for deterministic scenes
        if ( scene.GetSeed() > 0 )
        {
            srand( scene.GetSeed() );
        }

        // Replace terrain with analytic flat slope when the scene requests it
        if ( scene.HasFlatSlope() )
        {
            Gfx().FlushGPU();
            m_systems.terrain = std::make_unique<Terrain>( scene.GetFlatBaseY(), scene.GetFlatSlopeX(), scene.GetFlatSlopeZ() );
        }

        // Override world environment if scene specifies world values
        if ( scene.HasWorldOverride() )
        {
            m_cWorldEnvironment = WorldEnvironment( scene.GetWorldFluidHeight(), scene.GetWorldFluidDensity(), Cfg().gasDensity, scene.GetWorldGravity() );
            XZBounds tb = m_systems.terrain->GetXZBounds();
            m_cWorldEnvironment.SetTerrainBounds( tb.m_xMin, tb.m_xMax, tb.m_zMin, tb.m_zMax );
        }

        SetUpCamerasFromScene( scene );

        if ( scene.GetSolverBallCount() > 0 || scene.GetSolverBoxCount() > 0 )
        {
            // Exact-count solver spawn — explicit ball/box split for benchmarks.
            SetUpSolverObjects( scene.GetSolverBallCount(), scene.GetSolverBoxCount() );
        }
        else if ( scene.GetLegacyBallCount() > 0 )
        {
            SetUpGameModels( scene.GetLegacyBallCount() );
        }
        else
        {
            SetUpGameModelsFromScene( scene );
        }

        // Ball-tracking camera: enabled when scene specifies a positive track_height
        if ( scene.GetTrackHeight() > 0.0f )
        {
            m_camera.trackHeight = scene.GetTrackHeight();
            m_camera.trackBallIndex = 0;
            m_camera.autoCycleInterval = scene.GetAutoCycleInterval(); // -1 if not specified = disabled
        }

        const char* rendererName = Gfx().GetRendererName();
        char titleText[256];
        sprintf_s( titleText, "%s [SCENE MODE] [%s]", TITLE_TEXT, rendererName );
        m_systems.window->SetTitleText( titleText );

        // Snapshot scenes (ball_state) start paused in free camera mode ?
        // user presses F to resume simulation and attach to scene camera
        if ( scene.GetBallStateCount() > 0 )
        {
            m_camera.isFlyMode = true;
            m_camera.cameraTime = 0.0f;
            XZBounds unbounded;
            unbounded.m_xMin = -99999.9f;
            unbounded.m_xMax = 99999.9f;
            unbounded.m_zMin = -99999.9f;
            unbounded.m_zMax = 99999.9f;
            uint32_t activeCam = m_systems.cameras->GetSelectedCameraName();
            m_systems.cameras->SetCameraXZBounds( activeCam, unbounded );
            SetCursor( nullptr );
            Input::CentreMouseCoordinates();
            m_camera.input.xMove = 0;
            m_camera.input.yMove = 0;
        }
    }

    // Apply runtime swap policy after config/scene overrides are resolved.
    Gfx().SetVsyncEnabled( m_runtimeSettings.isVsyncEnabled );

    // Restart timers
    m_timers.frameTimer.StartTimer();
    m_timers.workTimer.StartTimer();
    m_timers.updateTimer.StartTimer();
    m_timers.cameraTimer.StartTimer();
    m_timers.simulationTimer.StartTimer();

    // Initialize DXR raytracing on first scene load (requires terrain + sphere meshes to exist)
    // Force sphere mesh creation (normally lazy-init on first render)
    if ( Gfx().IsDXRSupported() && SkullbonezHelper::GetSphereInstMeshHandle() == 0 )
    {
        SkullbonezHelper::EnsureSphereMesh();
    }
    {
    }
    if ( Gfx().IsDXRSupported() && m_systems.terrain && m_systems.terrain->GetMesh() )
    {
        IMesh* terrainMesh = m_systems.terrain->GetMesh();
        uint64_t terrainVBVA = terrainMesh->GetVertexBufferGPUVA();
        int terrainVertCount = terrainMesh->GetVertexCount();
        int terrainStride = terrainMesh->GetStride();

        uint32_t sphereHandle = SkullbonezHelper::GetSphereInstMeshHandle();
        uint64_t sphereVBVA = Gfx().GetInstancedMeshStaticVBVA( sphereHandle );
        int sphereVertCount = SkullbonezHelper::GetSphereVertexCount();
        int sphereStride = Gfx().GetInstancedMeshStaticStride( sphereHandle );

        {
        }

        if ( terrainVBVA != 0 && sphereVBVA != 0 )
        {
            Gfx().InitDXR( terrainVBVA, terrainVertCount, terrainStride, sphereVBVA, sphereVertCount, sphereStride, MAX_GAME_MODELS );
        }
    }
}


bool SkullbonezRun::AdvanceScene()
{
    // For perf tests with 2 passes, the second pass re-runs the same scene
    if ( m_perfLogState.isPerfTest && sPerfPass == 0 )
    {
        sPerfPass = 1;
        LoadScene( m_scene.currentSceneIndex );
        return true;
    }

    // Reset perf pass counter for next scene
    sPerfPass = 0;

    int nextIndex = m_scene.currentSceneIndex + 1;
    if ( nextIndex >= static_cast<int>( m_sceneQueue.size() ) )
    {
        return false;
    }

    LoadScene( nextIndex );
    return true;
}


void SkullbonezRun::SaveScreenshot( const char* path )
{
    // Capture backbuffer via render backend (returns BGR, bottom-up, 4-byte aligned rows)
    int m_width = 0;
    int m_height = 0;
    std::vector<uint8_t> pixels = Gfx().CaptureBackbuffer( m_width, m_height );

    // Row stride padded to 4-byte boundary (BMP requirement)
    int rowStride = ( m_width * 3 + 3 ) & ~3;
    int imageSize = rowStride * m_height;

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


void SkullbonezRun::LogPerfMemory( const char* checkpoint )
{
    if ( !m_perfLogState.perfLogFile )
    {
        return;
    }

    PROCESS_MEMORY_COUNTERS pmc;
    pmc.cb = sizeof( pmc );
    if ( GetProcessMemoryInfo( GetCurrentProcess(), &pmc, sizeof( pmc ) ) )
    {
        double mb = static_cast<double>( pmc.WorkingSetSize ) / ( 1024.0 * 1024.0 );
        fprintf( m_perfLogState.perfLogFile, "# MEM %s pass=%d working_set_mb=%.2f\n", checkpoint, sPerfPass + 1, mb );
        ++m_perfLogState.perfLogWritesSinceFlush;
        if ( m_perfLogState.isPerfLogFlushEnabled ||
             ( m_perfLogState.perfLogFlushInterval > 0 && m_perfLogState.perfLogWritesSinceFlush >= m_perfLogState.perfLogFlushInterval ) )
        {
            fflush( m_perfLogState.perfLogFile );
            m_perfLogState.perfLogWritesSinceFlush = 0;
        }
    }
}
