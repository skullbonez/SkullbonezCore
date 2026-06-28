/*
File: SkullbonezSource/Runtime/Replay/RunReplayScrubberTools.inl
Purpose:
  Contains replay scrubber input, inspection-camera, and live-restore glue.

Mental model:
  The scrubber maps mouse/UI intent to retained solver or presentation samples.
  Run still owns the live model collection and camera systems, while replay
  state transitions stay routed through ReplayRuntime.

Glossary:
  Scrubber: UI control that selects retained replay frames.
  Live restore: Applying a retained replay sample back into the current scene.
  Inspection camera: Temporary replay-focused camera state for selected samples.

Invariants:
  - Restoring a sample must set the scrubber status message and consume restore
    input for the current frame.
  - Replay tool pointer ownership must release through the interaction controller.

Related:
  - SkullbonezSource/Runtime/Replay/RunReplayTools.cpp
  - SkullbonezSource/Runtime/Replay/ReplayRuntime.h
*/
void Run::EnterReplayInspectionCamera()
{
    // Lifetime: Replay camera activation captures the current camera/mode so
    // exiting scrub/velocity/cause inspection can restore the operator's view.
    if ( !m_systems.cameras )
    {
        return;
    }

    const bool enteringInspectionCamera = !m_replayRuntime.Camera().active;
    if ( !m_replayRuntime.Camera().active )
    {
        m_replayRuntime.Camera().restoreCameraMode = NormalizeCameraModeForCurrentScene( m_camera.mode );
        m_replayRuntime.Camera().restoreCameraHash = m_systems.cameras->GetSelectedCameraName();

        auto magnitudeSquared = []( const Vector3& value ) -> float
        { return value.x * value.x + value.y * value.y + value.z * value.z; };

        Vector3 eye = m_systems.cameras->GetRenderCameraTranslation();
        Vector3 view = m_systems.cameras->GetRenderCameraView();
        Vector3 up = m_systems.cameras->GetRenderCameraUp();
        if ( magnitudeSquared( view - eye ) < 0.000001f )
        {
            eye = m_systems.cameras->GetCameraTranslation();
            view = m_systems.cameras->GetCameraView();
            up = m_systems.cameras->GetCameraUp();
        }
        if ( magnitudeSquared( view - eye ) < 0.000001f )
        {
            view = eye + Vector3( 0.0f, 0.0f, 1.0f );
        }
        if ( magnitudeSquared( up ) < 0.000001f )
        {
            up = Vector3( 0.0f, 1.0f, 0.0f );
        }

        m_replayRuntime.Camera().restoreEye = eye;
        m_replayRuntime.Camera().restoreView = view;
        m_replayRuntime.Camera().restoreUp = up;
        m_replayRuntime.Camera().hasRestorePose = true;
        m_systems.cameras->CancelTween();
        m_systems.cameras->SelectCamera( CAMERA_FREE, false );
        m_systems.cameras->SetPrimaryPosition( eye );
        m_systems.cameras->SetViewCoordinates( view );
        m_systems.cameras->SetPrimaryUp( up );
        m_replayRuntime.Camera().active = true;
    }

    XZBounds unbounded;
    unbounded.m_xMin = -99999.9f;
    unbounded.m_xMax = 99999.9f;
    unbounded.m_zMin = -99999.9f;
    unbounded.m_zMax = 99999.9f;
    m_systems.cameras->SetCameraXZBounds( CAMERA_FREE, unbounded );
    m_camera.cameraTime = 0.0f;
    CancelMousePickup();
    if ( !IsReplayToolOwner( m_interaction.Owner() ) )
    {
        SetWorldInteractionOwnerAfterInteractionTransition( WorldInteractionOwner::ReplayScrub,
                                                            InteractionExitReason::EnterReplay );
    }
    SetCameraModeLabelAfterInteractionTransition( RunCameraMode::Inspect );
    if ( enteringInspectionCamera )
    {
        Input::SetSystemCursorVisible( true );
        InputController::ResetMouseLook( m_camera );
    }
}


void Run::ExitReplayInspectionCamera()
{
    if ( !m_replayRuntime.Camera().active )
    {
        return;
    }

    m_replayRuntime.Camera().active = false;
    SetCameraModeLabelAfterInteractionTransition(
        NormalizeCameraModeForCurrentScene( m_replayRuntime.Camera().restoreCameraMode ) );
    if ( m_replayRuntime.VelocityEdit().enabled )
    {
        SetWorldInteractionOwnerAfterInteractionTransition( WorldInteractionOwner::ReplayVelocityEdit,
                                                            InteractionExitReason::EnterReplay );
    }
    else
    {
        EnterInteractionForCameraMode( m_camera.mode );
    }
    if ( m_systems.cameras )
    {
        m_systems.cameras->CancelTween();
        m_systems.cameras->SelectCamera( m_replayRuntime.Camera().restoreCameraHash, false );
        if ( m_replayRuntime.Camera().hasRestorePose )
        {
            m_systems.cameras->SetPrimaryPosition( m_replayRuntime.Camera().restoreEye );
            m_systems.cameras->SetViewCoordinates( m_replayRuntime.Camera().restoreView );
            m_systems.cameras->SetPrimaryUp( m_replayRuntime.Camera().restoreUp );
        }
        if ( m_systems.terrain )
        {
            const uint32_t activeCam = m_systems.cameras->GetSelectedCameraName();
            if ( IsFlyCameraMode() )
            {
                XZBounds unbounded;
                unbounded.m_xMin = -99999.9f;
                unbounded.m_xMax = 99999.9f;
                unbounded.m_zMin = -99999.9f;
                unbounded.m_zMax = 99999.9f;
                m_systems.cameras->SetCameraXZBounds( activeCam, unbounded );
            }
            else
            {
                m_systems.cameras->SetCameraXZBounds( activeCam, m_systems.terrain->GetXZBounds() );
            }
        }
    }
    m_replayRuntime.Camera().focusKind = RunReplayCameraFocusKind::None;
    m_replayRuntime.Camera().focusedRow = -1;
    m_replayRuntime.Camera().hasRestorePose = false;
    m_replayRuntime.Camera().ownsSimulationPause = false;
    m_replayRuntime.Camera().restoreCameraMode = RunCameraMode::Demo;
    Input::SetSystemCursorVisible( true );
    InputController::ResetMouseLook( m_camera );
}

bool Run::RestoreReplayScrubberSelectionAsLive( double now,
                                                RunReplayV2TargetRestoreResult* outV2Result,
                                                char* outReason,
                                                std::size_t reasonSize )
{
    if ( outV2Result )
    {
        *outV2Result = RunReplayV2TargetRestoreResult();
    }

    auto writeReason = [outReason, reasonSize]( const char* reason )
    {
        if ( outReason && reasonSize > 0 )
        {
            strncpy_s( outReason, reasonSize, reason ? reason : "restore failed", _TRUNCATE );
        }
    };

    char reason[160] = {};
    bool restored = false;
    RunReplayTrack messageTrack = m_replayRuntime.Scrubber().activeTrack;
    if ( m_replayRuntime.HasLoadedPresentation() && m_replayRuntime.Scrubber().historicalSamplePaused &&
         m_replayRuntime.Scrubber().activeTrack == RunReplayTrack::Presentation )
    {
        EnterInteractiveSceneRun();
        RunReplayV2TargetRestoreResult result;
        const ReplayPresentationSample* selected = m_replayRuntime.CurrentScrubSample();
        const ReplayFrameIndex selectedFrame = selected ? selected->frameIndex : 0;
        restored = selected && RestoreReplayV2ArtifactTargetState( m_replayRuntime.LoadedPresentation().path,
                                                                   selectedFrame,
                                                                   true,
                                                                   result,
                                                                   reason,
                                                                   sizeof( reason ) );
        if ( outV2Result )
        {
            *outV2Result = result;
        }
        messageTrack = RunReplayTrack::Presentation;
        fprintf( stderr,
                 "[replay] V2 file restore %s target_frame=%llu branch_id=%u%s%s\n",
                 restored ? "applied" : "failed",
                 static_cast<unsigned long long>( selectedFrame ),
                 restored ? result.branchId : 0,
                 reason[0] != '\0' ? ": " : "",
                 reason );
    }
    else if ( m_replayRuntime.Scrubber().historicalSamplePaused &&
              m_replayRuntime.Scrubber().activeTrack == RunReplayTrack::Solver )
    {
        EnterInteractiveSceneRun();
        const ReplaySolverFrameSample* sample = m_replayRuntime.CurrentSolverScrubSample();
        restored = sample && RestoreReplaySolverSampleAsLive( *sample, reason, sizeof( reason ) );
        messageTrack = RunReplayTrack::Solver;
        fprintf( stderr,
                 "[replay] Solver restore %s%s%s\n",
                 restored ? "applied" : "failed",
                 reason[0] != '\0' ? ": " : "",
                 reason );
    }
    else
    {
        sprintf_s( reason, sizeof( reason ), "no historical replay branch target selected" );
        fprintf( stderr, "[replay] Branch restore failed: %s\n", reason );
    }

    m_replayRuntime.Scrubber().restoreConsumedThisFrame = true;
    m_replayRuntime.Scrubber().saveMessageTrack = messageTrack;
    sprintf_s( m_replayRuntime.Scrubber().saveMessage,
               sizeof( m_replayRuntime.Scrubber().saveMessage ),
               restored ? ( messageTrack == RunReplayTrack::Presentation ? "V2 FILE BRANCHED" : "SOLVER RESTORED" )
                        : "RESTORE FAILED" );
    m_replayRuntime.Scrubber().saveMessageUntil = now + 2.5;
    m_replayRuntime.Scrubber().visibleUntil = now + REPLAY_SCRUBBER_VISIBLE_SECONDS;
    m_replayRuntime.Scrubber().visible = true;
    writeReason( reason );
    return restored;
}

bool Run::TickReplayScrubberInput( HWND hwnd, bool uiBlocksMouse )
{
    PROFILE_SCOPED( "Frame/Replay/ScrubberInput" );
    m_replayRuntime.Scrubber().restoreConsumedThisFrame = false;
    const bool leftDown = Input::IsLeftMouseDown();
    const bool leftPressed = leftDown && !m_replayRuntime.Scrubber().leftWasDown;
    const bool leftReleased = !leftDown && m_replayRuntime.Scrubber().leftWasDown;
    m_replayRuntime.Scrubber().leftWasDown = leftDown;
    const bool restoreDown = Input::IsKeyDown( VK_RETURN );
    const bool restorePressed = restoreDown && !m_replayRuntime.Scrubber().restoreWasDown;
    m_replayRuntime.Scrubber().restoreWasDown = restoreDown;

    const bool scrubberAllowed = !m_runtimeTools.Editor().editorModeEnabled && m_UI.IsVisible() && m_UI.IsMinimized();
    const bool loadedPresentation = m_replayRuntime.HasLoadedPresentation();
    const ReplayRecorderStats solverReplayStats = m_replayRuntime.Solver().GetStats();
    const bool solverReplayAvailable = solverReplayStats.enabled && solverReplayStats.sampleCount >= 2;
    const int screenW = RuntimeWindowScreenWidth( m_systems, Cfg() );
    const int screenH = RuntimeWindowScreenHeight( m_systems, Cfg() );
    if ( !scrubberAllowed || ( !loadedPresentation && !solverReplayAvailable ) || screenW <= 0 || screenH <= 0 )
    {
        CancelReplayToolDragState();
        if ( !loadedPresentation )
        {
            if ( m_replayRuntime.ResetScrubberState() )
            {
                ExitReplayInspectionCamera();
            }
        }
        m_replayRuntime.Prediction().checkboxHovered = false;
        m_replayRuntime.Prediction().ragdollVisualsHovered = false;
        m_replayRuntime.Prediction().decreaseHovered = false;
        m_replayRuntime.Prediction().increaseHovered = false;
        m_replayRuntime.Prediction().horizonHovered = false;
        m_replayRuntime.Prediction().horizonDragging = false;
        m_replayRuntime.VelocityEdit().toggleHovered = false;
        m_replayRuntime.Scrubber().branchHovered = false;
        m_replayRuntime.Scrubber().loadHovered = false;
        m_replayRuntime.Scrubber().leftWasDown = leftDown;
        return false;
    }

    const POINT mouse = Input::GetClientMouseCoordinates();
    m_replayRuntime.Scrubber().mouseX = mouse.x;
    m_replayRuntime.Scrubber().mouseY = mouse.y;

    const UI::UIRect hotZone = ReplayScrubberHotZoneRect( screenW, screenH );
    const UI::UIRect panel = ReplayScrubberPanelRect( screenW, screenH );
    const UI::UIRect solverSaveButton = ReplayScrubberSaveButtonRect( screenW, screenH, RunReplayTrack::Solver );
    const UI::UIRect branchButton = ReplayScrubberBranchButtonRect( screenW, screenH );
    const UI::UIRect pauseButton = ReplayScrubberPauseButtonRect( screenW, screenH );
    const UI::UIRect velocityEditToggle = ReplayScrubberVelocityEditToggleRect( screenW, screenH );
    const UI::UIRect predictControl = ReplayScrubberPredictControlRect( screenW, screenH );
    const UI::UIRect predictToggle = ReplayScrubberPredictToggleRect( screenW, screenH );
    const UI::UIRect predictHorizon = ReplayScrubberPredictHorizonRect( screenW, screenH );
    const UI::UIRect ragdollVisualToggle = ReplayScrubberRagdollVisualToggleRect( screenW, screenH );
    const RunReplayTrack scrubTrack = loadedPresentation ? RunReplayTrack::Presentation : RunReplayTrack::Solver;
    const UI::UIRect replayLoadButton = ReplayScrubberLoadButtonRect( screenW, screenH, scrubTrack );
    const bool solverToolsEnabled = !loadedPresentation && solverReplayAvailable;
    const bool inHotZone = hotZone.Contains( mouse.x, mouse.y );
    const bool overPanel = panel.Contains( mouse.x, mouse.y );
    const bool overSaveButton = solverToolsEnabled && solverSaveButton.Contains( mouse.x, mouse.y );
    const bool overLoadButton = replayLoadButton.Contains( mouse.x, mouse.y );
    const bool branchTargetAvailable =
        m_replayRuntime.Scrubber().historicalSamplePaused &&
        ( ( loadedPresentation && m_replayRuntime.Scrubber().activeTrack == RunReplayTrack::Presentation &&
            m_replayRuntime.CurrentScrubSample() != nullptr ) ||
          ( solverToolsEnabled && m_replayRuntime.Scrubber().activeTrack == RunReplayTrack::Solver &&
            m_replayRuntime.CurrentSolverScrubSample() != nullptr ) );
    const bool overBranchButton = branchButton.Contains( mouse.x, mouse.y );
    const bool overPauseButton = solverToolsEnabled && pauseButton.Contains( mouse.x, mouse.y );
    const bool overVelocityEditToggle = solverToolsEnabled && velocityEditToggle.Contains( mouse.x, mouse.y );
    const bool overPredictControl = solverToolsEnabled && predictControl.Contains( mouse.x, mouse.y );
    const bool overPredictToggle = solverToolsEnabled && predictToggle.Contains( mouse.x, mouse.y );
    const bool overRagdollVisualToggle = solverToolsEnabled && ragdollVisualToggle.Contains( mouse.x, mouse.y );
    const bool overPredictUi = overPredictControl || overPredictToggle || overRagdollVisualToggle;
    const bool overPredictHorizon =
        solverToolsEnabled && ( predictHorizon.Contains( mouse.x, mouse.y ) ||
                                ( predictControl.Contains( mouse.x, mouse.y ) && mouse.x >= predictHorizon.x &&
                                  mouse.x <= predictHorizon.x + predictHorizon.w ) );
    const RunReplayTrack hoveredTrack = scrubTrack;
    const bool canTakeMouse =
        !uiBlocksMouse || m_replayRuntime.Scrubber().dragging || m_replayRuntime.Prediction().horizonDragging;
    const double now = m_timers.simulationTimer.GetTotalTime();
    auto promptLoadReplayPresentationArtifact = [&]() -> bool
    {
        char path[MAX_PATH] = {};
        OPENFILENAMEA openFile = {};
        openFile.lStructSize = sizeof( openFile );
        openFile.hwndOwner = hwnd;
        openFile.lpstrFilter = "Skullbonez replay (*.skreplay)\0*.skreplay\0All files (*.*)\0*.*\0";
        openFile.lpstrFile = path;
        openFile.nMaxFile = sizeof( path );
        openFile.lpstrInitialDir = "replays";
        openFile.lpstrTitle = "Load Skullbonez replay v2 artifact";
        openFile.lpstrDefExt = "skreplay";
        openFile.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR;

        if ( !GetOpenFileNameA( &openFile ) )
        {
            if ( CommDlgExtendedError() != 0 )
            {
                const double messageNow = m_timers.simulationTimer.GetTotalTime();
                sprintf_s( m_replayRuntime.Scrubber().saveMessage,
                           sizeof( m_replayRuntime.Scrubber().saveMessage ),
                           "REPLAY PICKER FAILED" );
                m_replayRuntime.Scrubber().saveMessageTrack = RunReplayTrack::Presentation;
                m_replayRuntime.Scrubber().saveMessageUntil = messageNow + 2.5;
                m_replayRuntime.Scrubber().visibleUntil = messageNow + REPLAY_SCRUBBER_VISIBLE_SECONDS;
                m_replayRuntime.Scrubber().visible = true;
            }
            return false;
        }

        const bool loaded = LoadReplayPresentationArtifact( path, true );
        const double messageNow = m_timers.simulationTimer.GetTotalTime();
        const char* fileName = strrchr( path, '\\' );
        if ( !fileName )
        {
            fileName = strrchr( path, '/' );
        }
        fileName = fileName ? fileName + 1 : path;

        m_replayRuntime.Scrubber().saveMessageTrack = RunReplayTrack::Presentation;
        if ( loaded )
        {
            constexpr int loadedPrefixLength = 7;
            constexpr int loadedFileNameLimit =
                static_cast<int>( sizeof( m_replayRuntime.Scrubber().saveMessage ) ) - loadedPrefixLength - 1;
            sprintf_s( m_replayRuntime.Scrubber().saveMessage,
                       sizeof( m_replayRuntime.Scrubber().saveMessage ),
                       "LOADED %.*s",
                       loadedFileNameLimit,
                       fileName );
        }
        else
        {
            sprintf_s( m_replayRuntime.Scrubber().saveMessage,
                       sizeof( m_replayRuntime.Scrubber().saveMessage ),
                       "REPLAY LOAD FAILED" );
        }
        m_replayRuntime.Scrubber().saveMessageUntil = messageNow + 2.5;
        m_replayRuntime.Scrubber().visibleUntil = messageNow + REPLAY_SCRUBBER_VISIBLE_SECONDS;
        m_replayRuntime.Scrubber().visible = true;
        return loaded;
    };

    if ( inHotZone || overPanel || overSaveButton || overLoadButton || overBranchButton || overPauseButton ||
         overVelocityEditToggle || overPredictUi || m_replayRuntime.Scrubber().dragging ||
         m_replayRuntime.Prediction().horizonDragging || m_replayRuntime.Scrubber().historicalSamplePaused ||
         m_replayRuntime.Scrubber().liveAdvanceHeld )
    {
        m_replayRuntime.Scrubber().visibleUntil = now + REPLAY_SCRUBBER_VISIBLE_SECONDS;
    }
    m_replayRuntime.Scrubber().saveHovered =
        overSaveButton && ( m_replayRuntime.Scrubber().visibleUntil >= now || m_replayRuntime.Scrubber().dragging ||
                            m_replayRuntime.Scrubber().historicalSamplePaused );
    m_replayRuntime.Scrubber().loadHovered =
        overLoadButton && ( m_replayRuntime.Scrubber().visibleUntil >= now || m_replayRuntime.Scrubber().dragging ||
                            m_replayRuntime.Scrubber().historicalSamplePaused );
    m_replayRuntime.Scrubber().saveHoveredTrack = hoveredTrack;
    const bool branchControlVisible =
        m_replayRuntime.Scrubber().visibleUntil >= now || m_replayRuntime.Scrubber().dragging ||
        m_replayRuntime.Scrubber().historicalSamplePaused || m_replayRuntime.Scrubber().liveAdvanceHeld;
    m_replayRuntime.Scrubber().branchHovered = branchTargetAvailable && overBranchButton && branchControlVisible;
    const bool predictionControlVisible =
        solverToolsEnabled &&
        ( m_replayRuntime.Scrubber().visibleUntil >= now || m_replayRuntime.Scrubber().dragging ||
          m_replayRuntime.Prediction().horizonDragging || m_replayRuntime.Scrubber().historicalSamplePaused ||
          m_replayRuntime.Scrubber().liveAdvanceHeld );
    m_replayRuntime.Scrubber().pauseHovered = solverToolsEnabled && overPauseButton && predictionControlVisible;
    m_replayRuntime.VelocityEdit().toggleHovered =
        solverToolsEnabled && overVelocityEditToggle && predictionControlVisible;
    m_replayRuntime.Prediction().checkboxHovered = solverToolsEnabled && overPredictToggle && predictionControlVisible;
    m_replayRuntime.Prediction().ragdollVisualsHovered =
        solverToolsEnabled && overRagdollVisualToggle && predictionControlVisible;
    m_replayRuntime.Prediction().decreaseHovered = false;
    m_replayRuntime.Prediction().increaseHovered = false;
    m_replayRuntime.Prediction().horizonHovered = solverToolsEnabled && overPredictHorizon && predictionControlVisible;

    bool consumesMouse =
        canTakeMouse && ( m_replayRuntime.Scrubber().dragging || m_replayRuntime.Prediction().horizonDragging ||
                          ( m_replayRuntime.Scrubber().visibleUntil >= now &&
                            ( inHotZone || overPanel || overSaveButton || overBranchButton || overLoadButton ||
                              overPauseButton || overVelocityEditToggle || overPredictUi ) ) );

    if ( branchTargetAvailable &&
         ( restorePressed || ( leftPressed && canTakeMouse && overBranchButton && branchControlVisible ) ) )
    {
        SetWorldInteractionOwnerAfterInteractionTransition( WorldInteractionOwner::ReplayBranchTarget,
                                                            InteractionExitReason::EnterReplay );
        RestoreReplayScrubberSelectionAsLive( now );
        consumesMouse = true;
        return true;
    }

    auto updateReplayInspectionCamera = [&]()
    {
        if ( m_replayRuntime.ShouldUseInspectionCamera() )
        {
            EnterReplayInspectionCamera();
        }
        else
        {
            ExitReplayInspectionCamera();
        }
    };

    auto setReplayLiveAdvanceHeld = [&]( bool held )
    {
        if ( !m_replayRuntime.SetLiveAdvanceHeld( held ) )
        {
            return;
        }

        if ( held )
        {
            EnterInteractiveSceneRun();
            if ( !IsReplayToolOwner( m_interaction.Owner() ) )
            {
                SetWorldInteractionOwnerAfterInteractionTransition( WorldInteractionOwner::ReplayScrub,
                                                                    InteractionExitReason::EnterReplay );
            }
        }
        else if ( m_replayRuntime.VelocityEdit().enabled )
        {
            SetWorldInteractionOwnerAfterInteractionTransition( WorldInteractionOwner::ReplayVelocityEdit,
                                                                InteractionExitReason::EnterReplay );
        }
        else if ( !m_replayRuntime.Scrubber().historicalSamplePaused &&
                  m_replayRuntime.Camera().focusKind == RunReplayCameraFocusKind::None )
        {
            EnterInteractionForCameraMode( m_camera.mode );
        }
        updateReplayInspectionCamera();
    };

    auto setPredictionHorizonFromMouse = [&]( bool ensureReplayPredictionOwner )
    {
        EnterInteractiveSceneRun();
        if ( ensureReplayPredictionOwner )
        {
            SetWorldInteractionOwnerAfterInteractionTransition( WorldInteractionOwner::ReplayPrediction,
                                                                InteractionExitReason::EnterReplay );
        }
        const float nextSeconds = ReplayPredictionHorizonFromMouse( mouse.x, predictHorizon );
        if ( nextSeconds != m_replayRuntime.Prediction().horizonSeconds )
        {
            m_replayRuntime.Prediction().horizonSeconds = nextSeconds;
            m_replayRuntime.MarkPredictionDirty();
        }
        m_replayRuntime.Scrubber().visibleUntil = now + REPLAY_SCRUBBER_VISIBLE_SECONDS;
        m_replayRuntime.Scrubber().visible = true;
        consumesMouse = true;
    };

    if ( solverToolsEnabled && leftPressed && canTakeMouse && overPauseButton &&
         m_replayRuntime.Scrubber().visibleUntil >= now )
    {
        setReplayLiveAdvanceHeld( !m_replayRuntime.Scrubber().liveAdvanceHeld );
        m_replayRuntime.Scrubber().visibleUntil = now + REPLAY_SCRUBBER_VISIBLE_SECONDS;
        m_replayRuntime.Scrubber().visible = true;
        consumesMouse = true;
    }
    else if ( solverToolsEnabled && leftPressed && canTakeMouse && overVelocityEditToggle &&
              m_replayRuntime.Scrubber().visibleUntil >= now )
    {
        const bool enableVelocityEdit = !m_replayRuntime.VelocityEdit().enabled;
        if ( m_replayRuntime.SetVelocityEditEnabled( enableVelocityEdit ) )
        {
            CancelReplayToolDragState();
            if ( enableVelocityEdit )
            {
                EnterInteractiveSceneRun();
                setReplayLiveAdvanceHeld( true );
                SetWorldInteractionOwnerAfterInteractionTransition( WorldInteractionOwner::ReplayVelocityEdit,
                                                                    InteractionExitReason::EnterReplay );
            }
            else if ( m_interaction.Owner() == WorldInteractionOwner::ReplayVelocityEdit )
            {
                SetWorldInteractionOwnerAfterInteractionTransition( WorldInteractionOwner::ReplayScrub,
                                                                    InteractionExitReason::EnterReplay );
            }
        }
        m_replayRuntime.Scrubber().visibleUntil = now + REPLAY_SCRUBBER_VISIBLE_SECONDS;
        m_replayRuntime.Scrubber().visible = true;
        consumesMouse = true;
    }
    else if ( solverToolsEnabled && leftPressed && canTakeMouse && overRagdollVisualToggle &&
              m_replayRuntime.Scrubber().visibleUntil >= now )
    {
        m_replayRuntime.Prediction().ragdollVisualsEnabled = !m_replayRuntime.Prediction().ragdollVisualsEnabled;
        m_replayRuntime.ClearPredictionFutureNodeCache();
        m_replayRuntime.Scrubber().visibleUntil = now + REPLAY_SCRUBBER_VISIBLE_SECONDS;
        m_replayRuntime.Scrubber().visible = true;
        consumesMouse = true;
    }
    else if ( solverToolsEnabled && leftPressed && canTakeMouse && overPredictHorizon &&
              m_replayRuntime.Scrubber().visibleUntil >= now )
    {
        m_replayRuntime.Prediction().horizonDragging = true;
        BeginReplayToolGesture( RuntimeInteractionGestureKind::ReplayPredictionHorizonDrag,
                                WorldInteractionOwner::ReplayPrediction,
                                RuntimePointerButton::Left,
                                mouse.x,
                                mouse.y );
        setPredictionHorizonFromMouse( false );
        if ( !m_replayRuntime.Scrubber().mouseCaptured )
        {
            UI::InputControl::BeginMouseCapture( hwnd );
            m_replayRuntime.Scrubber().mouseCaptured = true;
        }
    }
    else if ( solverToolsEnabled && leftPressed && canTakeMouse && overPredictToggle &&
              m_replayRuntime.Scrubber().visibleUntil >= now )
    {
        EnterInteractiveSceneRun();
        const float previousPredictionPresentT = m_replayRuntime.SolverPresentTrackPosition();
        m_replayRuntime.Prediction().enabled = !m_replayRuntime.Prediction().enabled;
        SetWorldInteractionOwnerAfterInteractionTransition( m_replayRuntime.Prediction().enabled
                                                                ? WorldInteractionOwner::ReplayPrediction
                                                                : WorldInteractionOwner::ReplayScrub,
                                                            InteractionExitReason::EnterReplay );
        m_replayRuntime.Prediction().horizonSeconds = std::clamp( m_replayRuntime.Prediction().horizonSeconds,
                                                                  REPLAY_PREDICTION_MIN_SECONDS,
                                                                  REPLAY_PREDICTION_MAX_SECONDS );
        if ( !m_replayRuntime.Prediction().enabled )
        {
            const float currentPosition = m_replayRuntime.TrackPosition( RunReplayTrack::Solver );
            if ( ReplayRuntime::TrackPositionIsFuture( currentPosition, previousPredictionPresentT ) )
            {
                m_replayRuntime.SetTrackPosition( RunReplayTrack::Solver, 1.0f );
                m_replayRuntime.Scrubber().historicalSamplePaused = false;
            }
            m_replayRuntime.ClearPredictionCache();
        }
        m_replayRuntime.MarkPredictionDirty();
        m_replayRuntime.Scrubber().visibleUntil = now + REPLAY_SCRUBBER_VISIBLE_SECONDS;
        m_replayRuntime.Scrubber().visible = true;
        consumesMouse = true;
    }
    else if ( solverToolsEnabled && leftPressed && canTakeMouse && overSaveButton &&
              m_replayRuntime.Scrubber().visibleUntil >= now )
    {
        EnterInteractiveSceneRun();
        SaveReplayBufferFromScrubber( m_replayRuntime,
                                      RunReplayTrack::Presentation,
                                      m_timers.simulationTimer.GetTotalTime() );
        consumesMouse = true;
    }
    else if ( leftPressed && canTakeMouse && overLoadButton && m_replayRuntime.Scrubber().visibleUntil >= now )
    {
        promptLoadReplayPresentationArtifact();
        consumesMouse = true;
    }
    else if ( leftPressed && canTakeMouse && !overBranchButton && !overPauseButton && !overPredictUi &&
              !overLoadButton && ( inHotZone || overPanel || m_replayRuntime.Scrubber().historicalSamplePaused ) )
    {
        EnterInteractiveSceneRun();
        BeginReplayToolGesture( RuntimeInteractionGestureKind::ReplayScrubDrag,
                                WorldInteractionOwner::ReplayScrub,
                                RuntimePointerButton::Left,
                                mouse.x,
                                mouse.y );
        m_replayRuntime.Scrubber().activeTrack = scrubTrack;
        m_replayRuntime.SyncActiveTrackPosition();
        m_replayRuntime.Scrubber().dragging = true;
        if ( !m_replayRuntime.Scrubber().mouseCaptured )
        {
            UI::InputControl::BeginMouseCapture( hwnd );
            m_replayRuntime.Scrubber().mouseCaptured = true;
        }
    }

    if ( m_replayRuntime.Scrubber().dragging )
    {
        m_replayRuntime.SetTrackPosition(
            m_replayRuntime.Scrubber().activeTrack,
            ReplayScrubberPositionFromMouse( mouse.x, screenW, screenH, m_replayRuntime.Scrubber().activeTrack ) );
        if ( loadedPresentation )
        {
            m_replayRuntime.Scrubber().historicalSamplePaused = true;
        }
        else
        {
            const float presentT = m_replayRuntime.SolverPresentTrackPosition();
            if ( ReplayRuntime::AtPresentTrackPosition( m_replayRuntime.Scrubber().position, presentT ) )
            {
                m_replayRuntime.SetTrackPosition( m_replayRuntime.Scrubber().activeTrack, presentT );
                m_replayRuntime.Scrubber().historicalSamplePaused = false;
            }
            else
            {
                m_replayRuntime.Scrubber().historicalSamplePaused = true;
            }
        }

        if ( leftReleased )
        {
            m_replayRuntime.Scrubber().dragging = false;
            EndReplayToolGesture( RuntimeInteractionGestureKind::ReplayScrubDrag );
            if ( m_replayRuntime.Scrubber().mouseCaptured )
            {
                UI::InputControl::EndMouseCapture();
                m_replayRuntime.Scrubber().mouseCaptured = false;
            }
        }
    }
    else if ( m_replayRuntime.Prediction().horizonDragging )
    {
        setPredictionHorizonFromMouse( false );
        if ( leftReleased )
        {
            m_replayRuntime.Prediction().horizonDragging = false;
            EndReplayToolGesture( RuntimeInteractionGestureKind::ReplayPredictionHorizonDrag );
            if ( m_replayRuntime.Scrubber().mouseCaptured )
            {
                UI::InputControl::EndMouseCapture();
                m_replayRuntime.Scrubber().mouseCaptured = false;
            }
        }
    }
    else if ( !loadedPresentation && !m_replayRuntime.Scrubber().historicalSamplePaused )
    {
        m_replayRuntime.SetAllTrackPositions( m_replayRuntime.SolverPresentTrackPosition() );
    }

    m_replayRuntime.Scrubber().visible =
        m_replayRuntime.Scrubber().dragging || m_replayRuntime.Prediction().horizonDragging ||
        m_replayRuntime.Scrubber().historicalSamplePaused || m_replayRuntime.Scrubber().liveAdvanceHeld ||
        m_replayRuntime.Scrubber().visibleUntil >= now;
    if ( m_replayRuntime.ShouldUseInspectionCamera() )
    {
        EnterReplayInspectionCamera();
    }
    else
    {
        ExitReplayInspectionCamera();
    }
    return consumesMouse;
}
