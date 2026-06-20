/*
File: SkullbonezSource/SkullbonezRunFrame.cpp
Purpose:
  Runs one frame of input, simulation, rendering, profiling, and presentation.

Mental model:
  Runtime code connects authored scene data, input, simulation, render
  backends, and validation-oriented launch modes. Follow who owns state and
  when that state changes.

Glossary:
  Validation gate: Repository script that proves a class of changes before
  commit or PR.

Related:
  - Agentic/Reference/runtime-reference.md
  - Agentic/Reference/comment-style-guide.md
*/
#include "SkullbonezRunInternal.h"

#include "SkullbonezCaptureSystem.h"

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
            Gfx().ResetFrameDrawCalls();

            PROFILE_BEGIN( "Frame/Input" );
            TakeInput();
            TickLiveStyleControl();
            PROFILE_END( "Frame/Input" );

            m_cGameModelCollection.BeginCollisionVisualFrame();
            TickPhysics( secondsPerFrame );

            PROFILE_BEGIN( "Frame/PostPhysics" );

            PROFILE_BEGIN( "Frame/PostPhysics/BroadphaseVisualizer" );
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
                UpdateRequiredSceneBroadphaseXCells( activeCellBuf, (std::min)( activeCellCount, SpatialGrid::MAX_BUCKETS ) );
            }
            PROFILE_END( "Frame/PostPhysics/BroadphaseVisualizer" );

            PROFILE_BEGIN( "Frame/PostPhysics/CollisionVisualizer" );
            m_collisionVisualizer.SetEnabled( m_debug.isCollisionVisualizer );
            m_collisionVisualizer.Update( static_cast<float>( secondsPerFrame ), m_cGameModelCollection );
            PROFILE_END( "Frame/PostPhysics/CollisionVisualizer" );

            PROFILE_BEGIN( "Frame/PostPhysics/PhysicsDebugVisualizer" );
            m_physicsDebugVisualizer.SetFlags( m_debug.physicsDebugFlags );
            m_physicsDebugVisualizer.SetContactLingerSeconds( m_debug.physicsDebugContactLinger );
            m_physicsDebugVisualizer.SetPipelineStageCursor( m_debug.physicsDebugPipelineStageCursor );
            m_physicsDebugVisualizer.Update( static_cast<float>( secondsPerFrame ), m_cGameModelCollection );
            UpdateRequiredSceneContacts();
            PROFILE_END( "Frame/PostPhysics/PhysicsDebugVisualizer" );

            PROFILE_BEGIN( "Frame/PostPhysics/EndCollisionVisualFrame" );
            m_cGameModelCollection.EndCollisionVisualFrame();
            PROFILE_END( "Frame/PostPhysics/EndCollisionVisualFrame" );

            PROFILE_END( "Frame/PostPhysics" );

            if ( m_runtimeSettings.isPipelineSyncEnabled )
            {
                PROFILE_BEGIN( "Frame/PipelineSync" );
                Gfx().Finish();
                PROFILE_END( "Frame/PipelineSync" );
            }

            PROFILE_BEGIN( "Frame/Render" );
            {
                DRAW_CALL_TRACE_SCOPE( "Frame/Render" );
                Render();
            }
            PROFILE_END( "Frame/Render" );

            if ( m_uiTextPass.ShouldRender() )
            {
                const int uiDrawCallStart = Gfx().GetFrameDrawCallCount();
                PROFILE_BEGIN( "Frame/UI" );
                {
                    DRAW_CALL_TRACE_SCOPE( "Frame/UI" );
                    m_uiTextPass.Render( secondsPerFrame );
                }
                PROFILE_END( "Frame/UI" );
                const int uiDrawCallEnd = Gfx().GetFrameDrawCallCount();
                m_timers.lastUIDrawCalls = (std::max)( 0, uiDrawCallEnd - uiDrawCallStart );
            }
            else
            {
                m_timers.lastUIDrawCalls = 0;
            }

            PROFILE_BEGIN( "Frame/PostDraw/LiveStyleCapture" );
            TickLiveStyleControlCapture();
            PROFILE_END( "Frame/PostDraw/LiveStyleCapture" );

            if ( TickScreenshots() )
            {
                continue;
            }

            PROFILE_BEGIN( "Frame/PostDraw/AutoCycle" );
            TickAutoCycle();
            PROFILE_END( "Frame/PostDraw/AutoCycle" );

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
                m_timers.physicsTime = Profiler::Instance().LastFrameMsByHash( kPhysicsHash ) * 0.001f;
                m_timers.renderTime = Profiler::Instance().LastFrameMsByHash( kRenderHash ) * 0.001f;
                static constexpr uint32_t kRenderGpuHashes[] = {
                    ::HashStr( "Frame/Shadows/ShadowMap" ),
                    ::HashStr( "Frame/Render/Skybox" ),
                    ::HashStr( "Frame/Render/Reflection" ),
                    ::HashStr( "Frame/Render/CinematicSky" ),
                    ::HashStr( "Frame/Render/Balls" ),
                    ::HashStr( "Frame/Render/Terrain" ),
                    ::HashStr( "Frame/Render/Water" ),
                    ::HashStr( "Frame/Render/TransparentBalls" ),
                    ::HashStr( "Frame/UI/Draw" ),
                };
                float gpuMs = 0.0f;
                for ( uint32_t h : kRenderGpuHashes )
                {
                    gpuMs += Profiler::Instance().LastGpuFrameMsByHash( h );
                }
                m_timers.gpuFrameWorkMs = gpuMs;
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


void SkullbonezRun::TickPhysics( double secondsPerFrame )
{
    const SimulationTickResult tick = m_simulation.Tick( SimulationTickInput{
        secondsPerFrame,
        SceneState().timeScale,
        SceneState().isSceneMode,
        SceneState().isScenePhysics,
        SceneState().isFixedStep,
        m_camera.isFlyMode,
        m_camera.isLauncherMode,
        Input::IsKeyDown( VK_SPACE ),
        &m_cGameModelCollection,
        m_replay.IsEnabled() ? &SkullbonezRun::CaptureReplayPhysicsStepThunk : nullptr,
        this } );
    TickRayCastTestLines( static_cast<float>( secondsPerFrame ) );
    m_launcherLaser.Update( static_cast<float>( secondsPerFrame ) );
    if ( tick.shouldUpdateLogic )
    {
        UpdateLogic( tick.simulationDt, tick.cameraDt );
    }
}


void SkullbonezRun::CaptureReplayPhysicsStepThunk( void* userData )
{
    SkullbonezRun* run = static_cast<SkullbonezRun*>( userData );
    if ( run )
    {
        run->CaptureReplayPhysicsStep();
    }
}


void SkullbonezRun::CaptureReplayPhysicsStep()
{
    ReplayCaptureInput input;
    input.sceneFrame = SceneState().currentFrame;
    input.simulationSeconds = m_timers.simulationTimer.GetTimeSinceLastStart();
    input.physicsDt = PHYSICS_FIXED_DT;
    input.fixedStep = SceneState().isFixedStep;
    input.scenePhysicsEnabled = SceneState().isScenePhysics;
    input.sceneTextEnabled = SceneState().isSceneText;
    input.waterHidden = m_debug.isWaterHidden;
    input.terrainHidden = m_debug.isTerrainHidden;
    input.cameras = m_systems.cameras;
    input.world = &m_cWorldEnvironment;
    input.models = &m_cGameModelCollection;
    m_replay.CaptureFrame( input );
}


void SkullbonezRun::EnterInteractiveSceneRun()
{
    SceneState().isInteractiveRun = true;
    SceneState().isExitOnComplete = false;
    m_screenshot.isScreenshotAndExit = false;
}


bool SkullbonezRun::CanSceneAutomationQuit() const
{
    return !SceneState().isInteractiveRun;
}


void SkullbonezRun::HoldCompletedInteractiveScene()
{
    SceneState().isTestComplete = true;
    SceneState().isExitOnComplete = false;
    m_screenshot.isScreenshotAndExit = false;
    m_camera.autoCycleInterval = -1.0f;
    m_camera.autoCycleAccum = 0.0f;
}


bool SkullbonezRun::TickScreenshots()
{
    PROFILE_BEGIN( "Frame/PostDraw/Screenshots" );

    struct ScreenshotSink final : RuntimeCaptureSink
    {
        explicit ScreenshotSink( SkullbonezRun& owner )
            : run( owner )
        {
        }

        void SaveScreenshot( const char* path ) override
        {
            run.SaveScreenshot( path );
        }

        SkullbonezRun& run;
    };

    ScreenshotSink sink( *this );
    const std::string* scenePath = CurrentSceneQueuePath();
    const RuntimeCaptureResult result = CaptureSystem::TickScreenshots( m_screenshot,
                                                                        RuntimeCaptureSceneContext{
                                                                            SceneState().isSceneMode,
                                                                            SceneState().isInteractiveRun,
                                                                            SceneState().currentFrame,
                                                                            m_timers.simulationTimer.GetTimeSinceLastStart() * 1000.0,
                                                                            scenePath ? scenePath->c_str() : nullptr },
                                                                        sink );

    PROFILE_END( "Frame/PostDraw/Screenshots" );

    if ( result.restartFrame )
    {
        PROFILE_FRAME_END();
    }

#ifdef _DEBUG
    if ( result.completion == RuntimeCaptureCompletion::ScreenshotAndExit )
    {
        LogSceneFinished( "screenshot_and_exit" );
    }
    else if ( result.completion == RuntimeCaptureCompletion::Screenshot )
    {
        LogSceneFinished( "screenshot" );
    }
#endif

    switch ( result.automation )
    {
    case RuntimeCaptureAutomation::Quit:
        PostQuitMessage( 0 );
        break;
    case RuntimeCaptureAutomation::AdvanceSceneOrQuit:
        if ( !AdvanceScene() )
        {
            PostQuitMessage( 0 );
        }
        break;
    case RuntimeCaptureAutomation::HoldInteractive:
        HoldCompletedInteractiveScene();
        break;
    case RuntimeCaptureAutomation::None:
        break;
    }

    return result.restartFrame;
}


void SkullbonezRun::TickAutoCycle()
{
    struct ScreenshotSink final : RuntimeCaptureSink
    {
        explicit ScreenshotSink( SkullbonezRun& owner )
            : run( owner )
        {
        }

        void SaveScreenshot( const char* path ) override
        {
            run.SaveScreenshot( path );
        }

        SkullbonezRun& run;
    };

    ScreenshotSink sink( *this );
    const RuntimeCaptureResult result = CaptureSystem::TickAutoCycle( SceneState().isSceneMode,
                                                                      SceneState().isInteractiveRun,
                                                                      m_cGameModelCollection.GetModelCount(),
                                                                      m_camera.autoCycleInterval,
                                                                      m_camera.autoCycleAccum,
                                                                      m_camera.autoCycleShotsTaken,
                                                                      m_camera.trackBallIndex,
                                                                      sink );

    if ( result.completion != RuntimeCaptureCompletion::AutoCycle )
    {
        return;
    }

#ifdef _DEBUG
    LogSceneFinished( "auto_cycle" );
#endif

    if ( result.automation == RuntimeCaptureAutomation::Quit )
    {
        PostQuitMessage( 0 );
    }
    else if ( result.automation == RuntimeCaptureAutomation::HoldInteractive )
    {
        HoldCompletedInteractiveScene();
    }
}


void SkullbonezRun::TickPerfLog()
{
    RuntimeDiagnostics::TickPerfLog( m_perfLogState,
                                     RuntimePerfTickContext{
                                         sPerfPass + 1,
                                         SceneState().currentFrame + 1,
                                         m_timers.physicsTime,
                                         m_timers.renderTime } );

    if ( ( SceneState().currentFrame + 1 ) % 60 == 0 )
    {
        LogPerfMemory( "periodic" );
    }
}


bool SkullbonezRun::TickSceneAdvance()
{
    ++SceneState().currentFrame;

    const bool hasRequiredContactGate = !m_requiredSceneContacts.empty();
    const bool hasRequiredBroadphaseGate = !m_requiredBroadphaseXCells.empty();
    const bool hasRequiredSceneGate = hasRequiredContactGate || hasRequiredBroadphaseGate;
    const bool requiredContactsComplete = RequiredSceneContactsComplete();
    const bool requiredBroadphaseComplete = RequiredSceneBroadphaseXCellsComplete();
    const bool requiredSceneComplete = requiredContactsComplete && requiredBroadphaseComplete;
    if ( hasRequiredSceneGate && requiredSceneComplete && !SceneState().isTestComplete )
    {
#ifdef _DEBUG
        LogSceneFinished( "required_scene_gates" );
#endif
        if ( SceneState().isExitOnComplete && CanSceneAutomationQuit() )
        {
            if ( !AdvanceScene() )
            {
                PostQuitMessage( 0 );
            }
            return true;
        }

        if ( CanSceneAutomationQuit() )
        {
            SceneState().isTestComplete = true;
        }
        else
        {
            HoldCompletedInteractiveScene();
        }
    }

    // Check if target frame count is reached (skip if screenshot auto-exit is still pending)
    if ( SceneState().targetFrameCount > 0 && !m_screenshot.isScreenshotSaved )
    {
        if ( SceneState().currentFrame >= SceneState().targetFrameCount )
        {
            const bool frameCountCompletesScene = !hasRequiredSceneGate || requiredSceneComplete;
#ifdef _DEBUG
            if ( !SceneState().isTestComplete && ( frameCountCompletesScene || SceneState().currentFrame == SceneState().targetFrameCount ) )
            {
                LogSceneFinished( frameCountCompletesScene ? "frame_count" : "required_scene_gates_missing" );
                if ( !frameCountCompletesScene )
                {
                    for ( const RunRequiredContactState& contact : m_requiredSceneContacts )
                    {
                        if ( contact.bodyA < 0 || contact.bodyB < 0 || !contact.touched )
                        {
                            fprintf( stderr,
                                     "[scene] required_contact missing: %s <-> %s\n",
                                     contact.nameA,
                                     contact.nameB );
                        }
                    }
                    for ( const RunRequiredBroadphaseXCellsState& cells : m_requiredBroadphaseXCells )
                    {
                        if ( !cells.activated )
                        {
                            fprintf( stderr,
                                     "[scene] required_broadphase_x_cells missing: x %d..%d y %d z %d first_missing=%d active_cells=%d observed_x=%s%d..%d\n",
                                     cells.minCellX,
                                     cells.maxCellX,
                                     cells.cellY,
                                     cells.cellZ,
                                     cells.lastMissingCellX,
                                     cells.lastActiveCellCount,
                                     cells.hasObservedXRange ? "" : "none ",
                                     cells.lastObservedMinX,
                                     cells.lastObservedMaxX );
                        }
                    }
                }
            }
#endif
            if ( !frameCountCompletesScene )
            {
                return false;
            }

            if ( SceneState().isExitOnComplete && CanSceneAutomationQuit() )
            {
                if ( !AdvanceScene() )
                {
                    PostQuitMessage( 0 );
                }
                return true;
            }
            else
            {
                if ( frameCountCompletesScene && CanSceneAutomationQuit() )
                {
                    SceneState().isTestComplete = true;
                }
                else if ( frameCountCompletesScene )
                {
                    HoldCompletedInteractiveScene();
                }
            }
        }
    }

    // Generated demo mode: restart every 20s to keep the sandbox moving indefinitely.
    if ( !SceneState().isSceneMode && !m_camera.isFlyMode && m_timers.simulationTimer.GetTimeSinceLastStart() > 20.0 )
    {
        LoadScene( SceneState().currentSceneIndex, SceneState().isInteractiveRun, SceneState().isInteractiveRun, SceneState().isInteractiveRun );
        m_timers.simulationTimer.StartTimer();
        return true;
    }

    // Perf-log scenes without an explicit frame count still use a timed pass duration.
    if ( m_perfLogState.isPerfTest &&
         SceneState().targetFrameCount <= 0 &&
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
    if ( SceneState().isSceneMode && m_camera.autoCycleInterval > 0.0f )
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
