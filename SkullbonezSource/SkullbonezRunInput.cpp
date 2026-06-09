// --- Includes ---
#include "SkullbonezRunInternal.h"

// --- Usings ---
using namespace SkullbonezCore::Basics;
using namespace SkullbonezCore::Math::CollisionDetection;
using namespace SkullbonezCore::Math::Orientation;
using namespace SkullbonezCore::Math::Transformation;
using namespace SkullbonezCore::Physics;
using namespace SkullbonezCore::Basics::RunInternal;

void SkullbonezRun::StepPhysicsPipelineStage( int direction )
{
    const int stageCount = static_cast<int>( PhysicsPipelineStage::Count );
    if ( stageCount <= 0 || direction == 0 )
    {
        return;
    }

    m_debug.physicsDebugFlags |= PHYSICS_DEBUG_PIPELINE;
    int nextStage = ( m_debug.physicsDebugPipelineStageCursor + direction ) % stageCount;
    if ( nextStage < 0 )
    {
        nextStage += stageCount;
    }
    m_debug.physicsDebugPipelineStageCursor = nextStage;
}


void SkullbonezRun::TakeInput()
{
    if ( !Input::IsAppFocused() )
    {
        Input::SetSystemCursorVisible( true );
        m_camera.input = {};
        m_leftSceneCycleWasDown = false;
        m_rightSceneCycleWasDown = false;
        Input::ConsumeMouseWheelDelta();
        m_UI.CancelInputCapture();
        RunUIStressActions();
        return;
    }

    const auto UIWantsReleasedMouse = [&]() -> bool
    {
        return m_camera.isFlyMode && m_UI.WantsNativeMouseCursor();
    };
    const auto ApplyCursorOwnership = [&]() -> void
    {
        Input::SetSystemCursorVisible( UIWantsReleasedMouse() );
    };
    const auto ReleaseMouseToUI = [&]() -> void
    {
        if ( UIWantsReleasedMouse() )
        {
            ReleaseCapture();
            m_camera.input.xMove = 0;
            m_camera.input.yMove = 0;
        }
    };

    ApplyCursorOwnership();

    const bool UIBlocksKeyboardBeforeInput = m_UI.BlocksKeyboard();
    if ( !UIBlocksKeyboardBeforeInput )
    {
        // Toggle fly mode with F (edge-detected so snapshot-loaded fly mode survives the next frame)
        bool prevFlyMode = m_camera.isFlyMode;
        bool fNow = Input::IsKeyDown( 'F' );
        if ( fNow && !m_camera.input.Get( InputState::FWasDown ) )
        {
            m_camera.isFlyMode = !m_camera.isFlyMode;
            m_camera.isNudgeMode = false; // F-key fly never implies nudge
        }
        m_camera.input.Set( InputState::FWasDown, fNow );

        // N key: toggle nudge mode — free camera with live simulation (edge-detected).
        // Nudge entering also enters fly mode; nudge exiting also exits fly mode.
        {
            bool nNow = Input::IsKeyDown( 'N' );
            if ( nNow && !m_camera.input.Get( InputState::NWasDown ) )
            {
                m_camera.isNudgeMode = !m_camera.isNudgeMode;
                m_camera.isFlyMode = m_camera.isNudgeMode;
            }
            m_camera.input.Set( InputState::NWasDown, nNow );
        }

#ifdef _DEBUG
        {
            bool enterNow = Input::IsKeyDown( VK_RETURN );
            if ( enterNow && !m_camera.input.Get( InputState::EnterWasDown ) && m_camera.isNudgeMode )
            {
                WriteNudgeReproSnapshot();
            }
            m_camera.input.Set( InputState::EnterWasDown, enterNow );
        }
#endif

        if ( m_camera.isFlyMode != prevFlyMode )
        {
            if ( m_camera.isFlyMode )
            {
                // Entering fly mode: generated demo mode snaps to free camera; scene mode stays
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
                if ( UIWantsReleasedMouse() )
                {
                    ReleaseMouseToUI();
                    Input::SetSystemCursorVisible( true );
                }
                else
                {
                    Input::SetSystemCursorVisible( false );
                    Input::CentreMouseCoordinates();
                }
                m_camera.input.xMove = 0;
                m_camera.input.yMove = 0;
            }
            else
            {
                // Exiting fly mode restores terrain bounds and the camera-cycle clock.  The
                // Windows cursor stays hidden because the diagnostics UI now draws the styled
                // cursor itself; restoring IDC_ARROW here creates a mismatched second cursor.
                uint32_t activeCam = m_scene.isSceneMode ? m_systems.cameras->GetSelectedCameraName() : CAMERA_FREE;
                m_systems.cameras->SetCameraXZBounds( activeCam, m_systems.terrain->GetXZBounds() );
                Input::SetSystemCursorVisible( false );
                m_camera.cameraTime = 0.0f;
                // Exiting fly mode also exits nudge mode
                m_camera.isNudgeMode = false;
            }
        }

        // Water m_shader debug toggles
        bool key1Now = Input::IsKeyDown( '1' );
        if ( key1Now && !m_camera.input.Get( InputState::Key1WasDown ) )
        {
            m_debug.isWaterFreezeDebug = !m_debug.isWaterFreezeDebug;
            if ( m_debug.isWaterFreezeDebug )
            {
                m_debug.frozenWaterTime = static_cast<float>( m_timers.simulationTimer.GetTimeSinceLastStart() );
            }
        }
        m_camera.input.Set( InputState::Key1WasDown, key1Now );
        // Key '2' cycles reflection mode: FBO (default) → DXR ray-traced (if supported) → none → FBO
        {
            static bool s_key2WasDown = false;
            bool s_key2Now = ( Input::IsKeyDown( '2' ) != 0 );
            if ( s_key2Now && !s_key2WasDown )
            {
                if ( !m_debug.isWaterRTReflect && !m_debug.isWaterNoReflect )
                {
                    if ( Gfx().IsDXRSupported() )
                    {
                        m_debug.isWaterRTReflect = true; // FBO → DXR
                    }
                    else
                    {
                        m_debug.isWaterNoReflect = true; // DXR not available, skip to none
                    }
                }
                else if ( m_debug.isWaterRTReflect )
                {
                    m_debug.isWaterRTReflect = false;
                    m_debug.isWaterNoReflect = true; // DXR → none
                }
                else
                {
                    m_debug.isWaterNoReflect = false; // none → FBO
                }
            }
            s_key2WasDown = s_key2Now;
        }
        {
            bool key3Now = Input::IsKeyDown( '3' );
            if ( key3Now && !m_camera.input.Get( InputState::Key3WasDown ) )
            {
                m_debug.isWaterFlatDebug = !m_debug.isWaterFlatDebug;
            }
            m_camera.input.Set( InputState::Key3WasDown, key3Now );
        }
        {
            bool key4Now = Input::IsKeyDown( '4' );
            if ( key4Now && !m_camera.input.Get( InputState::Key4WasDown ) )
            {
                m_debug.isTerrainHidden = !m_debug.isTerrainHidden;
            }
            m_camera.input.Set( InputState::Key4WasDown, key4Now );
        }
        {
            bool key5Now = Input::IsKeyDown( '5' );
            if ( key5Now && !m_camera.input.Get( InputState::Key5WasDown ) )
            {
                m_debug.isWaterHidden = !m_debug.isWaterHidden;
            }
            m_camera.input.Set( InputState::Key5WasDown, key5Now );
        }
        // V key: collision visualizer. Renders balls and boxes as solid debug colours.
        {
            bool vNow = Input::IsKeyDown( 'V' );
            if ( vNow && !m_camera.input.Get( InputState::VWasDown ) )
            {
                m_debug.isCollisionVisualizer = !m_debug.isCollisionVisualizer;
            }
            m_camera.input.Set( InputState::VWasDown, vNow );
        }

        // C key: cycle physics debug overlay - None -> Axes -> Contacts -> Sleep -> All -> None.
        {
            bool cNow = Input::IsKeyDown( 'C' );
            if ( cNow && !m_camera.input.Get( InputState::CKeyWasDown ) )
            {
                switch ( m_debug.physicsDebugFlags )
                {
                case PHYSICS_DEBUG_NONE:
                    m_debug.physicsDebugFlags = PHYSICS_DEBUG_AXES;
                    break;
                case PHYSICS_DEBUG_AXES:
                    m_debug.physicsDebugFlags = PHYSICS_DEBUG_CONTACTS;
                    break;
                case PHYSICS_DEBUG_CONTACTS:
                    m_debug.physicsDebugFlags = PHYSICS_DEBUG_SLEEP;
                    break;
                case PHYSICS_DEBUG_SLEEP:
                    m_debug.physicsDebugFlags = PHYSICS_DEBUG_ALL;
                    break;
                default:
                    m_debug.physicsDebugFlags = PHYSICS_DEBUG_NONE;
                    break;
                }
            }
            m_camera.input.Set( InputState::CKeyWasDown, cNow );
        }

        // F7/F8: step the physics pipeline visualizer through the bounded Catto
        // stage trace from the most recent physics tick. The simulation can be
        // paused with fly mode and advanced separately with Space.
        {
            static bool s_pipelinePrevWasDown = false;
            static bool s_pipelineNextWasDown = false;
            const bool prevNow = Input::IsKeyDown( VK_F7 );
            const bool nextNow = Input::IsKeyDown( VK_F8 );
            if ( prevNow && !s_pipelinePrevWasDown )
            {
                StepPhysicsPipelineStage( -1 );
            }
            if ( nextNow && !s_pipelineNextWasDown )
            {
                StepPhysicsPipelineStage( 1 );
            }
            s_pipelinePrevWasDown = prevNow;
            s_pipelineNextWasDown = nextNow;
        }

        // 6 key: translucent debug collision volumes for inspecting axes/contact rows inside bodies.
        {
            bool key6Now = Input::IsKeyDown( '6' );
            if ( key6Now && !m_camera.input.Get( InputState::Key6WasDown ) )
            {
                m_debug.isPhysicsDebugTransparent = !m_debug.isPhysicsDebugTransparent;
            }
            m_camera.input.Set( InputState::Key6WasDown, key6Now );
        }

        // Q key: cycle render backend at runtime while preserving current simulation state (GL → DX11 → DX12 → GL).
        {
            bool isQNow = Input::IsKeyDown( 'Q' );
            if ( isQNow && !m_camera.input.Get( InputState::QKeyWasDown ) )
            {
                SwitchRenderer( GetNextRendererType( GetCurrentRendererType() ) );
            }
            m_camera.input.Set( InputState::QKeyWasDown, isQNow );
        }

        // G key: toggle broadphase overlay, or cycle tracked ball if overlay is off.
        bool isGNow = Input::IsKeyDown( 'G' );
        if ( isGNow && !m_camera.input.Get( InputState::GKeyWasDown ) )
        {
            if ( m_scene.isSceneMode && m_camera.trackBallIndex >= 0 && !m_debug.isBroadphaseOverlay )
            {
                int count = m_cGameModelCollection.GetModelCount();
                if ( count > 0 )
                {
                    m_camera.trackBallIndex = ( m_camera.trackBallIndex + 1 ) % count;
                }
            }
            else
            {
                m_debug.isBroadphaseOverlay = !m_debug.isBroadphaseOverlay;
            }
        }
        m_camera.input.Set( InputState::GKeyWasDown, isGNow );

        // 0 key: toggle the in-game diagnostics window. Tabs replace the old overlay cycle.
        // Edge-detected in both scene and generated demo modes; one toggle per keypress.
        {
            bool key0Now = Input::IsKeyDown( '0' );
            if ( key0Now && !m_camera.input.Get( InputState::Key0WasDown ) )
            {
                EnterInteractiveSceneRun();
                m_UI.ToggleVisible( m_timers.simulationTimer.GetTotalTime() );
                m_debug.overlayMode = OverlayMode::None;
                ApplyCursorOwnership();
                ReleaseMouseToUI();
            }
            m_camera.input.Set( InputState::Key0WasDown, key0Now );
        }

        const bool leftSceneNow = Input::IsKeyDown( VK_LEFT );
        const bool rightSceneNow = Input::IsKeyDown( VK_RIGHT );
        if ( leftSceneNow && !m_leftSceneCycleWasDown )
        {
            EnterInteractiveSceneRun();
            LoadAdjacentSceneFromBrowser( -1 );
        }
        if ( rightSceneNow && !m_rightSceneCycleWasDown )
        {
            EnterInteractiveSceneRun();
            LoadAdjacentSceneFromBrowser( 1 );
        }
        m_leftSceneCycleWasDown = leftSceneNow;
        m_rightSceneCycleWasDown = rightSceneNow;
    }
    else
    {
        m_leftSceneCycleWasDown = Input::IsKeyDown( VK_LEFT );
        m_rightSceneCycleWasDown = Input::IsKeyDown( VK_RIGHT );
    }

    bool suppressNudgeFireThisFrame = UIBlocksKeyboardBeforeInput;
    if ( m_systems.window )
    {
        const int selectedSceneBrowserIndex = CurrentSceneBrowserIndex();
        InGameUIInputResult UIResult = m_UI.UpdateInput( m_systems.window->m_sWindow,
                                                         static_cast<int>( m_systems.window->m_sWindowDimensions.x ),
                                                         static_cast<int>( m_systems.window->m_sWindowDimensions.y ),
                                                         m_timers.simulationTimer.GetTotalTime(),
                                                         m_sceneBrowserNamePtrs.empty() ? nullptr : m_sceneBrowserNamePtrs.data(),
                                                         static_cast<int>( m_sceneBrowserNamePtrs.size() ),
                                                         selectedSceneBrowserIndex );
        if ( UIResult.userInteracted )
        {
            EnterInteractiveSceneRun();
        }
        suppressNudgeFireThisFrame = suppressNudgeFireThisFrame || UIResult.userInteracted || m_UI.BlocksCameraMouse();

        // ESC flicks the diagnostics window between minimized and expanded, with
        // a very fast double-tap escape hatch for quitting interactive runs.
        // Run it after UI input processing so focused controls keep their local ESC
        // behavior first, such as closing the scene filter combo without also
        // hiding the whole diagnostics surface on the same frame.
        const bool escapeNow = Input::IsKeyDown( VK_ESCAPE );
        if ( escapeNow && !m_camera.input.Get( InputState::EscapeWasDown ) && !UIResult.userInteracted )
        {
            constexpr double ESC_QUICK_EXIT_SECONDS = 0.32;
            const double UINow = m_timers.simulationTimer.GetTotalTime();
            if ( UINow - m_lastEscapeTapTime <= ESC_QUICK_EXIT_SECONDS )
            {
                PostQuitMessage( 0 );
            }
            else
            {
                EnterInteractiveSceneRun();
                m_UI.ToggleVisible( UINow );
                m_debug.overlayMode = OverlayMode::None;
                m_lastEscapeTapTime = UINow;
                ApplyCursorOwnership();
                ReleaseMouseToUI();
            }
        }
        m_camera.input.Set( InputState::EscapeWasDown, escapeNow );

        if ( UIResult.toggleVsync )
        {
            m_runtimeSettings.isVsyncEnabled = !m_runtimeSettings.isVsyncEnabled;
            Gfx().SetVsyncEnabled( m_runtimeSettings.isVsyncEnabled );
        }
        if ( UIResult.toggleCollisionVisualizer )
        {
            m_debug.isCollisionVisualizer = !m_debug.isCollisionVisualizer;
        }
        if ( UIResult.togglePhysicsSleepPolicy )
        {
            m_runtimeSettings.isPhysicsSleepEnabled = !m_runtimeSettings.isPhysicsSleepEnabled;
            m_cGameModelCollection.SetPhysicsSleepEnabled( m_runtimeSettings.isPhysicsSleepEnabled );
        }
        if ( UIResult.togglePhysicsDebugFlags != 0 )
        {
            m_debug.physicsDebugFlags ^= ( UIResult.togglePhysicsDebugFlags & PHYSICS_DEBUG_ALL );
        }
        if ( UIResult.stepPhysicsPipelinePrevious )
        {
            StepPhysicsPipelineStage( -1 );
        }
        if ( UIResult.stepPhysicsPipelineNext )
        {
            StepPhysicsPipelineStage( 1 );
        }
        if ( UIResult.togglePhysicsDebugTransparent )
        {
            m_debug.isPhysicsDebugTransparent = !m_debug.isPhysicsDebugTransparent;
        }
        if ( UIResult.toggleBroadphaseOverlay )
        {
            m_debug.isBroadphaseOverlay = !m_debug.isBroadphaseOverlay;
        }
        if ( UIResult.toggleTextOnly )
        {
            m_debug.isTextOnly = !m_debug.isTextOnly;
        }
        if ( UIResult.toggleFixedStep )
        {
            m_scene.isFixedStep = !m_scene.isFixedStep;
            m_timers.physicsAccumulator = 0.0f;
            m_timers.fixedStepTickAccumulator = 0.0f;
        }
        if ( UIResult.toggleTerrainHidden )
        {
            m_debug.isTerrainHidden = !m_debug.isTerrainHidden;
        }
        if ( UIResult.toggleWaterHidden )
        {
            m_debug.isWaterHidden = !m_debug.isWaterHidden;
        }
        if ( UIResult.toggleWaterFreeze )
        {
            m_debug.isWaterFreezeDebug = !m_debug.isWaterFreezeDebug;
            if ( m_debug.isWaterFreezeDebug )
            {
                m_debug.frozenWaterTime = static_cast<float>( m_timers.simulationTimer.GetTimeSinceLastStart() );
            }
        }
        if ( UIResult.toggleWaterFlat )
        {
            m_debug.isWaterFlatDebug = !m_debug.isWaterFlatDebug;
        }
        if ( UIResult.toggleWaterReflection )
        {
            if ( m_debug.isWaterNoReflect )
            {
                m_debug.isWaterNoReflect = false;
            }
            else
            {
                m_debug.isWaterNoReflect = true;
                m_debug.isWaterRTReflect = false;
            }
        }
        if ( UIResult.requestedWaterReflectionMode >= 0 )
        {
            const int mode = std::clamp( UIResult.requestedWaterReflectionMode, 0, 2 );
            m_debug.isWaterRTReflect = mode == 1;
            m_debug.isWaterNoReflect = mode == 2;
        }
        if ( UIResult.requestedTimeScale > 0.0f )
        {
            m_UITimeScaleOverride = std::clamp( UIResult.requestedTimeScale, 0.10f, 10.00f );
            m_scene.timeScale = m_UITimeScaleOverride;
            m_timers.physicsAccumulator = 0.0f;
            m_timers.fixedStepTickAccumulator = 0.0f;
        }
        if ( UIResult.requestedSeed > 0 )
        {
            m_scene.rngSeed = static_cast<unsigned int>( std::clamp( UIResult.requestedSeed, 1, 999999 ) );
            srand( m_scene.rngSeed );
        }
        if ( UIResult.requestedPhysicsDebugAlpha >= 0.0f )
        {
            m_debug.physicsDebugAlpha = std::clamp( UIResult.requestedPhysicsDebugAlpha, 0.05f, 1.0f );
        }
        if ( UIResult.requestedPhysicsDebugContactLinger >= 0.0f )
        {
            m_debug.physicsDebugContactLinger = std::clamp( UIResult.requestedPhysicsDebugContactLinger, 0.0f, 5.0f );
        }
        if ( UIResult.requestedModelCount >= 0 )
        {
            ApplyUIModelCountOverride( UIResult.requestedModelCount );
        }
        if ( UIResult.requestedSolverBallCount >= 0 )
        {
            const int boxes = m_UISolverBoxCountOverride >= 0 ? m_UISolverBoxCountOverride : m_scene.solverBoxCount;
            ApplyUISolverObjectCounts( std::clamp( UIResult.requestedSolverBallCount, 0, (std::max)( 0, 1000 - boxes ) ), boxes );
        }
        if ( UIResult.requestedSolverBoxCount >= 0 )
        {
            const int balls = m_UISolverBallCountOverride >= 0 ? m_UISolverBallCountOverride : m_scene.solverBallCount;
            ApplyUISolverObjectCounts( balls, std::clamp( UIResult.requestedSolverBoxCount, 0, (std::max)( 0, 1000 - balls ) ) );
        }
        if ( UIResult.requestWorldGravity || UIResult.requestWorldFluidHeight || UIResult.requestWorldFluidDensity )
        {
            const float gravity = UIResult.requestWorldGravity ? UIResult.requestedWorldGravity : m_cWorldEnvironment.GetGravity();
            const float fluidHeight = UIResult.requestWorldFluidHeight ? UIResult.requestedWorldFluidHeight : m_cWorldEnvironment.GetFluidSurfaceHeight();
            const float fluidDensity = UIResult.requestWorldFluidDensity ? UIResult.requestedWorldFluidDensity : m_cWorldEnvironment.GetFluidDensity();
            ApplyUIWorldOverride( std::clamp( gravity, -100.0f, 0.0f ),
                                  std::clamp( fluidHeight, -100.0f, 200.0f ),
                                  std::clamp( fluidDensity, 0.0f, 5.0f ) );
        }
        if ( UIResult.resetScene )
        {
            EnterInteractiveSceneRun();
            ResetCurrentScene( true, true );
        }
        if ( UIResult.resetSceneDefaults )
        {
            EnterInteractiveSceneRun();
            ResetCurrentScene( false, true, false );
        }
        if ( UIResult.requestDemoScene )
        {
            LoadDemoSceneFromUI();
        }
        if ( UIResult.saveSceneDefaults )
        {
            SaveCurrentSceneDefaults();
        }
        if ( UIResult.requestedRendererIndex >= 0 )
        {
            RuntimeRendererType requestedRenderer = RuntimeRendererType::OpenGL;
            if ( UIResult.requestedRendererIndex == 1 )
            {
                requestedRenderer = RuntimeRendererType::DX11;
            }
            else if ( UIResult.requestedRendererIndex == 2 )
            {
                requestedRenderer = RuntimeRendererType::DX12;
            }
            SwitchRenderer( requestedRenderer );
        }
        if ( UIResult.requestedSceneIndex >= 0 )
        {
            LoadSceneFromBrowserIndex( UIResult.requestedSceneIndex );
        }

        RunUIStressActions();
    }

    // Nudge mode owns left click for firing the pooled silver bullets.  Keyboard
    // shortcuts are intentionally avoided so aiming and firing live on the mouse.
    {
        const bool leftMouseNow = Input::IsLeftMouseDown();
        if ( m_camera.isNudgeMode &&
             leftMouseNow &&
             !m_camera.input.Get( InputState::LeftMouseWasDown ) &&
             !suppressNudgeFireThisFrame )
        {
            FireProjectile();
        }
        m_camera.input.Set( InputState::LeftMouseWasDown, leftMouseNow );
    }

    if ( m_UI.BlocksKeyboard() )
    {
        m_camera.input.xMove = 0;
        m_camera.input.yMove = 0;
        m_camera.input.Set( InputState::Up, false );
        m_camera.input.Set( InputState::Down, false );
        m_camera.input.Set( InputState::Left, false );
        m_camera.input.Set( InputState::Right, false );
        ApplyCursorOwnership();
        return;
    }

    // F2: Save scene snapshot to Scenes/
    {
        bool f2Now = Input::IsKeyDown( VK_F2 );
        if ( f2Now && !m_camera.input.Get( InputState::F2WasDown ) )
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
        m_camera.input.Set( InputState::F2WasDown, f2Now );
    }

    // F3: Save screenshot to Screenshots/
    {
        bool f3Now = Input::IsKeyDown( VK_F3 );
        if ( f3Now && !m_camera.input.Get( InputState::F3WasDown ) )
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
        m_camera.input.Set( InputState::F3WasDown, f3Now );
    }

    // R: reset/reload the current scene from scratch. Backspace remains as a scene-mode alias.
    {
        bool rNow = Input::IsKeyDown( 'R' );
        if ( rNow && !m_camera.input.Get( InputState::RKeyWasDown ) )
        {
            EnterInteractiveSceneRun();
            ResetCurrentScene( true, true );
        }
        m_camera.input.Set( InputState::RKeyWasDown, rNow );
    }
    if ( m_scene.isSceneMode )
    {
        bool bsNow = Input::IsKeyDown( VK_BACK );
        if ( bsNow && !m_camera.input.Get( InputState::BackspaceWasDown ) )
        {
            EnterInteractiveSceneRun();
            ResetCurrentScene( true, true );
        }
        m_camera.input.Set( InputState::BackspaceWasDown, bsNow );
    }

    if ( m_camera.isFlyMode )
    {
        // Expanded diagnostics UI owns the native cursor in fly/nudge mode.
        // Otherwise mouse-look keeps using recentered OS coordinates internally.
        if ( UIWantsReleasedMouse() )
        {
            Input::SetSystemCursorVisible( true );
            m_camera.input.xMove = 0;
            m_camera.input.yMove = 0;
        }
        else if ( m_UI.BlocksCameraMouse() )
        {
            Input::SetSystemCursorVisible( false );
            m_camera.input.xMove = 0;
            m_camera.input.yMove = 0;
        }
        else
        {
            Input::SetSystemCursorVisible( false );
            POINT currentCoords = Input::GetMouseCoordinates();
            Input::CentreMouseCoordinates();
            POINT centreCoords = Input::GetMouseCoordinates();
            m_camera.input.xMove = currentCoords.x - centreCoords.x;
            m_camera.input.yMove = currentCoords.y - centreCoords.y;
        }

        // WASD movement
        m_camera.input.Set( InputState::Up, Input::IsKeyDown( 'W' ) );
        m_camera.input.Set( InputState::Left, Input::IsKeyDown( 'A' ) );
        m_camera.input.Set( InputState::Down, Input::IsKeyDown( 'S' ) );
        m_camera.input.Set( InputState::Right, Input::IsKeyDown( 'D' ) );
    }
    else
    {
        m_camera.input.xMove = 0;
        m_camera.input.yMove = 0;
        m_camera.input.Set( InputState::Up, false );
        m_camera.input.Set( InputState::Down, false );
        m_camera.input.Set( InputState::Left, false );
        m_camera.input.Set( InputState::Right, false );
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
        if ( m_camera.input.Get( InputState::Up ) )
        {
            m_systems.cameras->MovePrimary( Camera::TravelDirection::Forward, keyMovementQty * speedMult );
        }
        if ( m_camera.input.Get( InputState::Left ) )
        {
            m_systems.cameras->MovePrimary( Camera::TravelDirection::Left, keyMovementQty * speedMult );
        }
        if ( m_camera.input.Get( InputState::Down ) )
        {
            m_systems.cameras->MovePrimary( Camera::TravelDirection::Backward, keyMovementQty * speedMult );
        }
        if ( m_camera.input.Get( InputState::Right ) )
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


void SkullbonezRun::ResetProjectilePool()
{
    m_fire.bulletIndices.fill( -1 );
    m_fire.bulletNext = 0;
    m_fire.bulletPoolReady = false;
}


bool SkullbonezRun::EnsureProjectilePool()
{
    if ( m_fire.bulletPoolReady )
    {
        return true;
    }

    if ( m_cGameModelCollection.GetModelCount() > MAX_GAME_MODELS - RUNTIME_PROJECTILE_POOL_SIZE )
    {
        return false;
    }

    m_fire.bulletIndices.fill( -1 );
    for ( int i = 0; i < RUNTIME_PROJECTILE_POOL_SIZE; ++i )
    {
        const float parkOffset = static_cast<float>( i ) * ( CAMERA_PROJECTILE_RADIUS * 4.0f );
        GameModel bullet( &m_cWorldEnvironment,
                          Vector3( CAMERA_PROJECTILE_PARK_BASE, CAMERA_PROJECTILE_PARK_BASE - parkOffset, CAMERA_PROJECTILE_PARK_BASE ),
                          Vector3( CAMERA_PROJECTILE_MOMENT, CAMERA_PROJECTILE_MOMENT, CAMERA_PROJECTILE_MOMENT ),
                          CAMERA_PROJECTILE_MASS );
        bullet.SetTerrain( m_systems.terrain.get() );
        bullet.SetCoefficientRestitution( CAMERA_PROJECTILE_RESTITUTION );
        bullet.AddBoundingSphere( CAMERA_PROJECTILE_RADIUS );
        bullet.SetRenderTint( CAMERA_PROJECTILE_SILVER_R, CAMERA_PROJECTILE_SILVER_G, CAMERA_PROJECTILE_SILVER_B, 1.0f );
        bullet.SetFixed( true );

        char name[64];
        sprintf_s( name, sizeof( name ), "silver_bullet_%02d", i );
        bullet.SetName( name );

        const int bulletIndex = m_cGameModelCollection.GetModelCount();
        m_cGameModelCollection.AddGameModel( std::move( bullet ) );
        m_fire.bulletIndices[i] = bulletIndex;
    }

    m_fire.bulletNext = 0;
    m_fire.bulletPoolReady = true;
    return true;
}


void SkullbonezRun::FireProjectile()
{
    if ( !EnsureProjectilePool() )
    {
        return;
    }

    const int slot = m_fire.bulletNext;
    m_fire.bulletNext = ( m_fire.bulletNext + 1 ) % RUNTIME_PROJECTILE_POOL_SIZE;

    const int found = m_fire.bulletIndices[slot];
    if ( found < 0 || found >= m_cGameModelCollection.GetModelCount() )
    {
        ResetProjectilePool();
        return;
    }

    GameModel& model = m_cGameModelCollection.GetModelAtIndex( found );

    const Vector3& camPos = m_systems.cameras->GetCameraTranslation();
    const Vector3& camView = m_systems.cameras->GetCameraView();
    Vector3 forward = camView - camPos;
    const float lenSq = forward * forward;
    if ( lenSq < 1e-8f )
    {
        return;
    }
    forward = forward * ( 1.0f / sqrtf( lenSq ) );

    const Vector3 spawnPos = camPos + forward * CAMERA_PROJECTILE_SPAWN_CLEARANCE;

    const float speedMult = Input::IsKeyDown( VK_SHIFT ) ? CAMERA_PROJECTILE_SHIFT_MULTIPLIER : 1.0f;
    const float fireSpeed = CAMERA_PROJECTILE_SPEED * speedMult;

    model.SetFixed( false );
    m_cGameModelCollection.WakeModel( found );
    model.SetPosition( spawnPos );
    model.SetLinearVelocity( forward * fireSpeed );
    model.SetAngularVelocity( Vector3( 0.0f, 0.0f, 0.0f ) );
    model.SetRenderTint( CAMERA_PROJECTILE_SILVER_R, CAMERA_PROJECTILE_SILVER_G, CAMERA_PROJECTILE_SILVER_B, 1.0f );
}
