// --- Includes ---
#include "SkullbonezRunInternal.h"

// --- Usings ---
using namespace SkullbonezCore::Basics;
using namespace SkullbonezCore::Math::CollisionDetection;
using namespace SkullbonezCore::Math::Orientation;
using namespace SkullbonezCore::Math::Transformation;
using namespace SkullbonezCore::Physics;
using namespace SkullbonezCore::Basics::RunInternal;

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
            double secondsPerFrame = m_timers.frameTimer.GetElapsedTime();
            secondsPerFrame = std::clamp( secondsPerFrame, 0.0, 0.05 );

            m_timers.frameTimer.StartTimer();
            PROFILE_FRAME_BEGIN();
            m_timers.workTimer.StartTimer();
            Gfx().ResetFrameDrawCallCount();

            PROFILE_BEGIN( "Frame/Input" );
            TakeInput();
            TickLiveStyleControl();
            PROFILE_END( "Frame/Input" );

            TickRendererSwitch( static_cast<float>( secondsPerFrame ) );
            m_cGameModelCollection.BeginCollisionVisualFrame();
            TickPhysics( secondsPerFrame );

            // Update broadphase visualizer state (runs even when overlay is hidden so fades are correct)
            {
                m_broadphaseVisualizer.SetEnabled( m_debug.isBroadphaseOverlay );
                m_broadphaseVisualizer.SetCellSize( m_cGameModelCollection.GetSpatialGrid().GetCellSize() );
                const SpatialGrid& grid = m_cGameModelCollection.GetSpatialGrid();
                SpatialGrid::ActiveCell activeCellBuf[SpatialGrid::MAX_BUCKETS];
                int activeCellCount = grid.GetActiveCellCount();
                grid.GetActiveCells( activeCellBuf, SpatialGrid::MAX_BUCKETS );
                const std::vector<int64_t>& collisionKeys = m_cGameModelCollection.GetCollisionCellKeys();
                m_broadphaseVisualizer.Update( static_cast<float>( secondsPerFrame ), activeCellBuf, activeCellCount, collisionKeys.data(), static_cast<int>( collisionKeys.size() ) );
            }
            m_collisionVisualizer.SetEnabled( m_debug.isCollisionVisualizer );
            m_collisionVisualizer.Update( static_cast<float>( secondsPerFrame ), m_cGameModelCollection );
            m_physicsDebugVisualizer.SetFlags( m_debug.physicsDebugFlags );
            m_physicsDebugVisualizer.SetContactLingerSeconds( m_debug.physicsDebugContactLinger );
            m_physicsDebugVisualizer.SetPipelineStageCursor( m_debug.physicsDebugPipelineStageCursor );
            m_physicsDebugVisualizer.Update( static_cast<float>( secondsPerFrame ), m_cGameModelCollection );
            m_cGameModelCollection.EndCollisionVisualFrame();

            if ( m_runtimeSettings.isPipelineSyncEnabled )
            {
                Gfx().Finish();
            }

            PROFILE_GPU_BEGIN( "Frame/Render" );
            Render();
            PROFILE_GPU_END( "Frame/Render" );

            if ( !m_scene.isSceneMode || m_scene.isSceneText || m_debug.overlayMode != OverlayMode::None || m_UI.IsVisible() )
            {
                PROFILE_GPU_BEGIN( "Frame/UI" );
                DrawWindowText( secondsPerFrame );
                PROFILE_GPU_END( "Frame/UI" );
            }

            TickLiveStyleControlCapture();

            if ( TickScreenshots() )
            {
                continue;
            }

            TickAutoCycle();

            m_timers.workTimer.StopTimer();
            m_timers.cpuFrameWorkMs = static_cast<float>( std::clamp( m_timers.workTimer.GetElapsedTime(), 0.0, 0.25 ) * 1000.0 );

            PROFILE_BEGIN( "Frame/VsyncWait" );
            Gfx().Present();
            PROFILE_END( "Frame/VsyncWait" );

            m_timers.frameTimer.StopTimer();
            PROFILE_FRAME_END();

#if defined( SKULLBONEZ_PROFILE_ENABLED )
            {
                using SkullbonezCore::Basics::Profiler;
                static constexpr uint32_t kPhysicsHash = ::HashStr( "Frame/Physics" );
                static constexpr uint32_t kRenderHash = ::HashStr( "Frame/Render" );
                static constexpr uint32_t kUIHash = ::HashStr( "Frame/UI" );
                m_timers.physicsTime = Profiler::Instance().LastFrameMsByHash( kPhysicsHash ) * 0.001f;
                m_timers.renderTime = Profiler::Instance().LastFrameMsByHash( kRenderHash ) * 0.001f;
                m_timers.gpuFrameWorkMs = Profiler::Instance().LastGpuFrameMsByHash( kRenderHash ) + Profiler::Instance().LastGpuFrameMsByHash( kUIHash );
            }
#endif

            TickPerfLog();

            if ( TickSceneAdvance() )
            {
                continue;
            }
        }
    }
}


void SkullbonezRun::TickRendererSwitch( float dt )
{
    if ( m_debug.rendererSwitchInterval <= 0.0f )
    {
        return;
    }
    m_debug.rendererSwitchAccum += dt;
    if ( m_debug.rendererSwitchAccum >= m_debug.rendererSwitchInterval )
    {
        m_debug.rendererSwitchAccum = 0.0f;
        SwitchRenderer( GetNextRendererType( GetCurrentRendererType() ) );
    }
}


void SkullbonezRun::TickPhysics( double secondsPerFrame )
{
    if ( m_scene.isSceneMode && !m_scene.isScenePhysics )
    {
        return;
    }

    if ( m_scene.isFixedStep )
    {
        // Deterministic lock-step: exact fixed-delta ticks driven by time_scale.
        // Ignores wall-clock time entirely — produces identical results every run.
        //
        // Accumulate fractional fixed ticks so 0.5x, 1.5x, and 10x all remain
        // deterministic while using the exact fixed simulation delta.
        m_timers.fixedStepTickAccumulator += (std::max)( 0.0f, m_scene.timeScale );
        const int ticksThisFrame = (std::min)( static_cast<int>( std::floor( m_timers.fixedStepTickAccumulator ) ), FIXED_STEP_TIME_SCALE_MAX_TICKS_PER_FRAME );
        m_timers.fixedStepTickAccumulator -= static_cast<float>( ticksThisFrame );
        if ( !m_camera.isFlyMode || m_camera.isNudgeMode || Input::IsKeyDown( VK_SPACE ) )
        {
            PROFILE_BEGIN( "Frame/Physics" );
            for ( int tick = 0; tick < ticksThisFrame; ++tick )
            {
                m_cGameModelCollection.RunPhysics( PHYSICS_FIXED_DT );
            }
            PROFILE_END( "Frame/Physics" );
        }
        // Keep deterministic scene time and presentation time separate.
        //
        // Fixed-step scenes intentionally advance simulation by an exact tick count
        // that is independent of wall-clock time. Camera input and camera tweens are
        // not part of that deterministic simulation contract: they should feel the
        // same at 30 Hz, 144 Hz, or when time_scale is cranked for diagnostics.
        UpdateLogic( PHYSICS_FIXED_DT * static_cast<float>( ticksThisFrame ),
                     static_cast<float>( secondsPerFrame ) );
    }
    else
    {
        float scaledDt = static_cast<float>( secondsPerFrame ) * m_scene.timeScale;

        if ( !m_camera.isFlyMode || m_camera.isNudgeMode || Input::IsKeyDown( VK_SPACE ) )
        {
            PROFILE_BEGIN( "Frame/Physics" );
            // The impulse solver uses discrete overlap tests and needs small
            // fixed steps for stability. Only RunPhysics runs in the loop;
            // camera and miscellaneous UI updates use real frame time below.
            m_timers.physicsAccumulator += scaledDt;

            int steps = 0;
            while ( m_timers.physicsAccumulator >= PHYSICS_FIXED_DT && steps < PHYSICS_MAX_STEPS_PER_FRAME )
            {
                m_cGameModelCollection.RunPhysics( PHYSICS_FIXED_DT );
                m_timers.physicsAccumulator -= PHYSICS_FIXED_DT;
                ++steps;
            }

            // Drain excess accumulator if we hit the step cap to avoid carrying
            // a runaway backlog into the next frame after a stall.
            if ( steps == PHYSICS_MAX_STEPS_PER_FRAME )
            {
                m_timers.physicsAccumulator = 0.0f;
            }
            PROFILE_END( "Frame/Physics" );
        }

        // Per-frame logic runs once per render frame. Anything tied to simulation
        // playback gets the scaled dt; camera input and camera tweens get real wall
        // time so time_scale does not make the operator fly around the scene.
        UpdateLogic( scaledDt, static_cast<float>( secondsPerFrame ) );
    }
}


void SkullbonezRun::EnterInteractiveSceneRun()
{
    m_scene.isInteractiveRun = true;
    m_scene.isExitOnComplete = false;
    m_screenshot.isScreenshotAndExit = false;
}


bool SkullbonezRun::CanSceneAutomationQuit() const
{
    return !m_scene.isInteractiveRun;
}


void SkullbonezRun::HoldCompletedInteractiveScene()
{
    m_scene.isTestComplete = true;
    m_scene.isExitOnComplete = false;
    m_screenshot.isScreenshotAndExit = false;
    m_camera.autoCycleInterval = -1.0f;
    m_camera.autoCycleAccum = 0.0f;
}


bool SkullbonezRun::TickScreenshots()
{
    // screenshot_and_exit: on frame 0, save <scenename>.bmp to root then quit
    if ( m_scene.isSceneMode && m_screenshot.isScreenshotAndExit && m_scene.currentFrame == 0 )
    {
        const std::string* scenePath = CurrentSceneQueuePath();
        if ( !scenePath )
        {
            return false;
        }
        char outPath[256];
        const char* base = scenePath->c_str();
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
#ifdef _DEBUG
        LogSceneFinished( "screenshot_and_exit" );
#endif
        if ( CanSceneAutomationQuit() )
        {
            PostQuitMessage( 0 );
        }
        else
        {
            HoldCompletedInteractiveScene();
        }
        return true;
    }

    // Triggered screenshot: capture when target frame or ms threshold is reached
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
            PROFILE_FRAME_END();
#ifdef _DEBUG
            LogSceneFinished( "screenshot" );
#endif
            if ( CanSceneAutomationQuit() )
            {
                if ( !AdvanceScene() )
                {
                    PostQuitMessage( 0 );
                }
            }
            else
            {
                HoldCompletedInteractiveScene();
            }
            return true;
        }
    }

    // Interval capture: save numbered screenshot every N frames
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

    return false;
}


void SkullbonezRun::TickAutoCycle()
{
    if ( !m_scene.isSceneMode || m_camera.autoCycleInterval <= 0.0f || m_camera.autoCycleAccum < m_camera.autoCycleInterval )
    {
        return;
    }

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
#ifdef _DEBUG
        LogSceneFinished( "auto_cycle" );
#endif
        if ( CanSceneAutomationQuit() )
        {
            PostQuitMessage( 0 );
        }
        else
        {
            HoldCompletedInteractiveScene();
        }
    }
    else
    {
        m_camera.trackBallIndex = ( m_camera.trackBallIndex + 1 ) % ballCount;
    }
}


void SkullbonezRun::TickPerfLog()
{
    if ( !m_perfLogState.isPerfTest || !m_perfLogState.perfLogFile )
    {
        return;
    }

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

    if ( ( m_scene.currentFrame + 1 ) % 60 == 0 )
    {
        LogPerfMemory( "periodic" );
    }
}


bool SkullbonezRun::TickSceneAdvance()
{
    ++m_scene.currentFrame;

    // Check if target frame count is reached (skip if screenshot auto-exit is still pending)
    if ( m_scene.targetFrameCount > 0 && !m_screenshot.isScreenshotSaved )
    {
        if ( m_scene.currentFrame >= m_scene.targetFrameCount )
        {
#ifdef _DEBUG
            if ( !m_scene.isTestComplete )
            {
                LogSceneFinished( "frame_count" );
            }
#endif
            if ( m_scene.isExitOnComplete && CanSceneAutomationQuit() )
            {
                if ( !AdvanceScene() )
                {
                    PostQuitMessage( 0 );
                }
                return true;
            }
            else
            {
                if ( CanSceneAutomationQuit() )
                {
                    m_scene.isTestComplete = true;
                }
                else
                {
                    HoldCompletedInteractiveScene();
                }
            }
        }
    }

    // Generated demo mode: restart every 20s to keep the sandbox moving indefinitely.
    if ( !m_scene.isSceneMode && !m_camera.isFlyMode && m_timers.simulationTimer.GetTimeSinceLastStart() > 20.0 )
    {
        LoadScene( m_scene.currentSceneIndex, m_scene.isInteractiveRun, m_scene.isInteractiveRun, m_scene.isInteractiveRun );
        m_timers.simulationTimer.StartTimer();
        return true;
    }

    // Perf-log scenes without an explicit frame count still use a timed pass duration.
    if ( m_perfLogState.isPerfTest &&
         m_scene.targetFrameCount <= 0 &&
         m_timers.simulationTimer.GetTimeSinceLastStart() > PERF_TEST_PASS_SECONDS )
    {
#ifdef _DEBUG
        LogSceneFinished( "perf_duration" );
#endif
        if ( !AdvanceScene() )
        {
            if ( CanSceneAutomationQuit() )
            {
                PostQuitMessage( 0 );
            }
            else
            {
                HoldCompletedInteractiveScene();
            }
        }
        return true;
    }

    return false;
}


void SkullbonezRun::UpdateLogic( float simulationDt, float cameraDt )
{
    // Auto-cycle
    if ( m_scene.isSceneMode && m_camera.autoCycleInterval > 0.0f )
    {
        m_camera.autoCycleAccum += simulationDt;
    }

    // Camera controls are presentation-time behavior, not simulation-time
    // behavior. Keyboard travel is velocity-based, so it consumes unscaled real
    // frame time. Mouse look consumes a per-frame cursor delta, so using live dt
    // would make sensitivity vary with FPS; the fixed reference preserves the
    // existing 60 Hz tuning while making the result frame-rate independent.
    MoveCamera( cameraDt * Cfg().keySpeed,
                CAMERA_MOUSE_REFERENCE_DT * Cfg().mouseSensitivity );

    UpdateWaterHeightControls( simulationDt );

    // Tween speed is also presentation-time behavior. The selected destination
    // camera can still track moving scene objects, but the interpolation rate
    // itself should be stable in real seconds instead of following time_scale.
    m_systems.cameras->SetTweenSpeed( Cfg().cameraTweenRate * cameraDt );
}


void SkullbonezRun::UpdateWaterHeightControls( float dt )
{
    const bool downNow = Input::IsKeyDown( VK_NEXT );
    const bool upNow = Input::IsKeyDown( VK_PRIOR );
    if ( downNow == upNow )
    {
        return;
    }

    const float direction = upNow ? 1.0f : -1.0f;
    const float height = m_cWorldEnvironment.GetFluidSurfaceHeight() + direction * WATER_HEIGHT_CONTROL_SPEED * dt;
    m_cWorldEnvironment.SetFluidSurfaceHeight( height );
}
