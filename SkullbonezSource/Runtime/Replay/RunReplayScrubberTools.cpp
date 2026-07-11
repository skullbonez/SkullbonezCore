/*
File: SkullbonezSource/Runtime/Replay/RunReplayScrubberTools.cpp
Purpose:
  Contains replay scrubber input, inspection-camera, and live-restore glue.

Mental model:
  The scrubber maps mouse/UI intent to retained solver or presentation samples.
  ReplayRuntime receives a frame-scoped workspace view, owns all replay state
  transitions, and returns restore/application commands to the shell.

Glossary:
  Scrubber: UI control that selects retained replay frames.
  Live restore: Applying a retained replay sample back into the current scene.
  Branch restore: Applying a historical replay sample as the new live timeline
    while preserving parent/source branch provenance.
  Inspection camera: Temporary replay-focused camera state for selected samples.

Invariants:
  - Restoring a sample must set the scrubber status message and consume restore
    input for the current frame.
  - Replay tool pointer ownership must release through the interaction controller.

Related:
  - SkullbonezSource/Runtime/Replay/RunReplayTools.cpp
  - SkullbonezSource/Runtime/Replay/ReplayRuntime.h
*/
#include "ReplayRuntime.h"
#include "../../Assets/AssetKeys.h"
#include "../CameraCollection.h"
#include "../InputRouter.h"
#include "../RunCameraState.h"
#include "../Tools/RuntimeTools.h"
#include "../../Core/Profiler.h"
#include "../../Physics/ColliderStore.h"
#include "../../Physics/PhysicsBodyStore.h"
#include "../../Physics/PhysicsEngineStoreQueries.h"
#include "../InputController.h"
#include "ReplayInteractionController.h"
#include "ReplayOverlayLayout.h"
#include "ReplayRuntimeOwnerViews.h"
#include "../../World/Terrain.h"

#include <algorithm>
#include <cstddef>
#include <cstdio>
#include <cstring>

#include <commdlg.h>

using namespace SkullbonezCore::Basics;
using namespace SkullbonezCore::Math::Vector;
using namespace SkullbonezCore::Basics::ReplayOverlay;
using SkullbonezCore::Geometry::XZBounds;

namespace
{
// Why: entering scrubber inspection from sibling replay tools should preserve
// replay-owned pointer/camera state. Non-replay owners still release through the
// interaction controller before scrubber mode takes over.
bool IsReplayScrubberToolOwner( WorldInteractionOwner owner )
{
    return owner == WorldInteractionOwner::ReplayScrub || owner == WorldInteractionOwner::ReplayVelocityEdit ||
           owner == WorldInteractionOwner::ReplayPrediction || owner == WorldInteractionOwner::ReplayBranchTarget ||
           owner == WorldInteractionOwner::ReplayCauseTree;
}
} // namespace


void ReplayRuntime::EnterInspectionCamera( Environment::CameraCollection* cameras,
                                           RunCameraState& camera,
                                           RunCameraMode normalizedCurrentMode,
                                           RuntimeInteractionController& interaction,
                                           InputRouter& inputRouter,
                                           RunMousePickupState& mousePickup )
{
    // Lifetime: Replay camera activation captures the current camera/mode so
    // exiting scrub/velocity/cause inspection can restore the operator's view.
    if ( !cameras )
    {
        return;
    }

    const bool enteringInspectionCamera = !Camera().active;
    if ( !Camera().active )
    {
        Camera().restoreCameraMode = normalizedCurrentMode;
        Camera().restoreCameraHash = cameras->GetSelectedCameraName();

        auto magnitudeSquared = []( const Vector3& value ) -> float
        { return value.x * value.x + value.y * value.y + value.z * value.z; };

        Vector3 eye = cameras->GetRenderCameraTranslation();
        Vector3 view = cameras->GetRenderCameraView();
        Vector3 up = cameras->GetRenderCameraUp();
        if ( magnitudeSquared( view - eye ) < 0.000001f )
        {
            eye = cameras->GetCameraTranslation();
            view = cameras->GetCameraView();
            up = cameras->GetCameraUp();
        }
        if ( magnitudeSquared( view - eye ) < 0.000001f )
        {
            view = eye + Vector3( 0.0f, 0.0f, 1.0f );
        }
        if ( magnitudeSquared( up ) < 0.000001f )
        {
            up = Vector3( 0.0f, 1.0f, 0.0f );
        }

        Camera().restoreEye = eye;
        Camera().restoreView = view;
        Camera().restoreUp = up;
        Camera().hasRestorePose = true;
        cameras->SelectCamera( CAMERA_FREE, false );
        cameras->TweenPrimaryToPose( eye, view, up );
        Camera().active = true;
    }

    XZBounds unbounded;
    unbounded.m_xMin = -99999.9f;
    unbounded.m_xMax = 99999.9f;
    unbounded.m_zMin = -99999.9f;
    unbounded.m_zMax = 99999.9f;
    cameras->SetCameraXZBounds( CAMERA_FREE, unbounded );
    camera.cameraTime = 0.0f;
    if ( mousePickup.mouseCaptured )
    {
        inputRouter.ReleaseNativeCapture();
    }
    if ( interaction.Gesture().kind == RuntimeInteractionGestureKind::MousePickupDrag )
    {
        interaction.EndGesture( InteractionExitReason::EndGesture );
    }
    mousePickup = RunMousePickupState{};
    if ( !IsReplayScrubberToolOwner( interaction.Owner() ) )
    {
        interaction.SetWorldInteractionOwnerInWorkspace( RuntimeWorkspace::Replay,
                                                         WorldInteractionOwner::ReplayScrub,
                                                         InteractionExitReason::EnterReplay );
    }
    camera.mode = RunCameraMode::Inspect;
    if ( enteringInspectionCamera )
    {
        inputRouter.RequestCursorVisible( true );
        InputController::ResetMouseLook( camera );
    }
}


void ReplayRuntime::ExitInspectionCamera( Environment::CameraCollection* cameras,
                                          Geometry::Terrain* terrain,
                                          RunCameraState& camera,
                                          RunCameraMode normalizedRestoreMode,
                                          bool attachedFollow,
                                          bool directorGrabbed,
                                          RuntimeInteractionController& interaction,
                                          InputRouter& inputRouter )
{
    if ( !Camera().active )
    {
        return;
    }

    Camera().active = false;
    camera.mode = normalizedRestoreMode;
    if ( VelocityEdit().enabled )
    {
        interaction.SetWorldInteractionOwnerInWorkspace( RuntimeWorkspace::Replay,
                                                         WorldInteractionOwner::ReplayVelocityEdit,
                                                         InteractionExitReason::EnterReplay );
    }
    else
    {
        interaction.EnterCameraMode( camera.mode );
    }
    if ( cameras )
    {
        // Hazard: scene-load cleanup can run after CameraCollection::Reset()
        // and before authored/generated cameras are registered. A replay
        // restore hash from the old scene must not be looked up until a matching
        // camera exists.
        uint32_t restoreCameraHash = Camera().restoreCameraHash;
        bool restoreCameraAvailable = cameras->HasCamera( restoreCameraHash );
        if ( !restoreCameraAvailable && cameras->HasCamera( CAMERA_FREE ) )
        {
            restoreCameraHash = CAMERA_FREE;
            restoreCameraAvailable = true;
        }
        if ( restoreCameraAvailable )
        {
            cameras->SelectCamera( restoreCameraHash, false );
            if ( Camera().hasRestorePose )
            {
                cameras->TweenPrimaryToPose( Camera().restoreEye, Camera().restoreView, Camera().restoreUp );
            }
            if ( terrain )
            {
                const uint32_t activeCam = cameras->GetSelectedCameraName();
                if ( RunCameraModeUsesFlyControls( camera.mode, attachedFollow, directorGrabbed ) )
                {
                    XZBounds unbounded;
                    unbounded.m_xMin = -99999.9f;
                    unbounded.m_xMax = 99999.9f;
                    unbounded.m_zMin = -99999.9f;
                    unbounded.m_zMax = 99999.9f;
                    cameras->SetCameraXZBounds( activeCam, unbounded );
                }
                else
                {
                    cameras->SetCameraXZBounds( activeCam, terrain->GetXZBounds() );
                }
            }
        }
    }
    Camera().focusKind = RunReplayCameraFocusKind::None;
    Camera().focusedRow = -1;
    Camera().hasRestorePose = false;
    Camera().ownsSimulationPause = false;
    Camera().restoreCameraMode = RunCameraMode::Demo;
    inputRouter.RequestCursorVisible( true );
    InputController::ResetMouseLook( camera );
}


void ReplayRuntime::TickWorkspace( const ReplayWorkspaceInput& input, ReplayWorkspaceOutput& output )
{
    output = ReplayWorkspaceOutput{};

    bool enterInteractive = false;
    const bool scrubberOwnsMouse = TickScrubberInput( input.window,
                                                      input.uiBlocksMouse,
                                                      input.inputRouter,
                                                      input.interaction,
                                                      input.cameras,
                                                      input.terrain,
                                                      input.camera,
                                                      input.mousePickup,
                                                      input.normalizedCurrentMode,
                                                      input.normalizedRestoreMode,
                                                      input.attachedFollow,
                                                      input.directorGrabbed,
                                                      input.editorModeEnabled,
                                                      input.scenePhysicsEnabled,
                                                      input.uiVisible,
                                                      input.uiMinimized,
                                                      input.screenWidth,
                                                      input.screenHeight,
                                                      input.now,
                                                      enterInteractive,
                                                      output.restoreRequest );

    const Physics::PhysicsBodyStore& bodyStore = Physics::PhysicsEngineStoreQueries::BodyStore( input.physics );
    const Physics::ColliderStore& colliderStore = Physics::PhysicsEngineStoreQueries::Colliders( input.physics );
    const bool causeTreeOwnsMouse = TickCauseTreeInput( input.uiBlocksMouse || scrubberOwnsMouse,
                                                        input.wheelDelta,
                                                        input.inputRouter,
                                                        input.interaction,
                                                        bodyStore,
                                                        colliderStore,
                                                        input.presentation,
                                                        input.cameras,
                                                        input.terrain,
                                                        input.camera,
                                                        input.mousePickup,
                                                        input.normalizedCurrentMode,
                                                        input.normalizedRestoreMode,
                                                        input.attachedFollow,
                                                        input.directorGrabbed,
                                                        input.editorModeEnabled,
                                                        input.screenWidth,
                                                        input.screenHeight,
                                                        enterInteractive );

    const bool velocityEditOwnsMouse =
        TickVelocityEditInput( input.uiBlocksMouse || scrubberOwnsMouse || causeTreeOwnsMouse,
                               input.pointerRay,
                               input.inputRouter,
                               input.interaction,
                               input.physics,
                               input.entities,
                               input.presentation,
                               input.cameras,
                               input.terrain,
                               input.camera,
                               input.mousePickup,
                               input.normalizedCurrentMode,
                               input.normalizedRestoreMode,
                               input.attachedFollow,
                               input.directorGrabbed,
                               input.editorModeEnabled,
                               input.scenePhysicsEnabled,
                               input.screenWidth,
                               input.screenHeight,
                               input.now,
                               enterInteractive );

    output.consumesMouse = scrubberOwnsMouse || causeTreeOwnsMouse || velocityEditOwnsMouse;
    output.enterInteractive = enterInteractive || output.restoreRequest.enterInteractive;
}


void ReplayRuntime::ResetSceneTimeline( const SceneTimelineResetInput& input, const SceneTimelineResetOwners& owners )
{
    CancelToolDragState( owners.interaction, owners.inputRouter );
    const SceneTimelineResetResult begin = BeginSceneTimelineReset( input );
    if ( begin.exitInspectionCamera )
    {
        ExitInspectionCamera( owners.cameras,
                              owners.terrain,
                              owners.camera,
                              owners.normalizedRestoreMode,
                              owners.attachedFollow,
                              owners.directorGrabbed,
                              owners.interaction,
                              owners.inputRouter );
    }

    const SceneTimelineResetResult finish = FinishSceneTimelineReset( input );
    if ( finish.exitInspectionCamera )
    {
        ExitInspectionCamera( owners.cameras,
                              owners.terrain,
                              owners.camera,
                              owners.normalizedRestoreMode,
                              owners.attachedFollow,
                              owners.directorGrabbed,
                              owners.interaction,
                              owners.inputRouter );
    }
}


ReplayRuntime::ReplayLiveRestoreOutcome
ReplayRuntime::ApplyLiveRestoreRequest( const ReplayRestoreTransaction& transaction,
                                        const ReplayArtifactTopologyOwners& topologyOwners,
                                        const ReplayLiveRestoreRequest& request )
{
    ReplayLiveRestoreOutcome outcome;
    outcome.requested = request.kind != ReplayLiveRestoreKind::None;
    if ( !outcome.requested )
    {
        return outcome;
    }

    char reason[160] = {};
    RunReplayV2TargetRestoreResult v2Result;
    if ( request.kind == ReplayLiveRestoreKind::V2ArtifactTarget )
    {
        outcome.restored = RestoreV2ArtifactTargetState( transaction,
                                                         topologyOwners,
                                                         request.path,
                                                         request.requestedFrame,
                                                         request.makeLiveBranch,
                                                         v2Result,
                                                         reason,
                                                         sizeof( reason ) );
    }
    else if ( request.kind == ReplayLiveRestoreKind::SolverSample && request.solverSample )
    {
        outcome.restored = RestoreSolverSampleAsLive( transaction, *request.solverSample, reason, sizeof( reason ) );
    }

    ReplayInteractionController replayInteraction;
    replayInteraction.CompleteScrubberRestore( *this, request, outcome.restored, v2Result, reason );
    outcome.enterInteractive = v2Result.enterInteractiveRequested;
    return outcome;
}

bool ReplayRuntime::TickScrubberInput( HWND hwnd,
                                       bool uiBlocksMouse,
                                       InputRouter& inputRouter,
                                       RuntimeInteractionController& interaction,
                                       Environment::CameraCollection* cameras,
                                       Geometry::Terrain* terrain,
                                       RunCameraState& camera,
                                       RunMousePickupState& mousePickup,
                                       RunCameraMode normalizedCurrentMode,
                                       RunCameraMode normalizedRestoreMode,
                                       bool attachedFollow,
                                       bool directorGrabbed,
                                       bool editorModeEnabled,
                                       bool scenePhysicsEnabled,
                                       bool uiVisible,
                                       bool uiMinimized,
                                       int screenWidth,
                                       int screenHeight,
                                       double now,
                                       bool& outEnterInteractive,
                                       ReplayLiveRestoreRequest& outRestoreRequest )
{
    ReplayRuntime& m_replayRuntime = *this;
    InputRouter& m_inputRouter = inputRouter;
    RuntimeInteractionController& m_interaction = interaction;
    outRestoreRequest = ReplayLiveRestoreRequest{};
    const auto enterInspectionCamera = [&]()
    { EnterInspectionCamera( cameras, camera, normalizedCurrentMode, m_interaction, m_inputRouter, mousePickup ); };
    const auto exitInspectionCamera = [&]()
    {
        ExitInspectionCamera( cameras,
                              terrain,
                              camera,
                              normalizedRestoreMode,
                              attachedFollow,
                              directorGrabbed,
                              m_interaction,
                              m_inputRouter );
    };
    PROFILE_SCOPED( "Frame/Replay/ScrubberInput" );
    const RuntimeMouseEdges& pointer = m_inputRouter.UiSnapshot().mouse;
    const bool restoreDown = m_inputRouter.RuntimeSnapshot().enterDown;
    const ReplayRuntime::ScrubberInputFrame inputFrame =
        m_replayRuntime.BeginScrubberInputFrame( pointer.leftPressed, pointer.leftReleased, restoreDown );
    const bool leftPressed = inputFrame.leftPressed;
    const bool leftReleased = inputFrame.leftReleased;
    const bool restorePressed = inputFrame.restorePressed;

    const bool scrubberAllowed = !editorModeEnabled && uiVisible && uiMinimized;
    const bool loadedPresentation = m_replayRuntime.HasLoadedPresentation();
    const ReplayRecorderStats solverReplayStats = m_replayRuntime.Solver().GetStats();
    const bool solverReplayEnabled = solverReplayStats.enabled;
    const bool solverReplayAvailable = solverReplayEnabled && solverReplayStats.sampleCount >= 2;
    const bool replaySurfaceAvailable = loadedPresentation || solverReplayEnabled;
    const int screenW = screenWidth;
    const int screenH = screenHeight;
    if ( !scrubberAllowed || !replaySurfaceAvailable || screenW <= 0 || screenH <= 0 )
    {
        m_replayRuntime.CancelToolDragState( m_interaction, m_inputRouter );
        const ReplayRuntime::ScrubberUnavailableResult unavailable =
            m_replayRuntime.ResetUnavailableScrubberSurface( loadedPresentation );
        if ( unavailable.exitInspectionCamera )
        {
            exitInspectionCamera();
        }
        return false;
    }

    const RuntimePointerEvent& runtimePointer = m_inputRouter.RuntimeSnapshot().pointer;
    if ( !runtimePointer.hasClientPosition )
    {
        return false;
    }
    const POINT mouse{ runtimePointer.clientX, runtimePointer.clientY };
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
    const UI::UIRect pastPathToggle = ReplayScrubberPastPathToggleRect( screenW, screenH );
    const RunReplayTrack scrubTrack = loadedPresentation ? RunReplayTrack::Presentation : RunReplayTrack::Solver;
    const UI::UIRect scrubTrackRect = ReplayScrubberTrackRect( screenW, screenH, scrubTrack );
    const UI::UIRect replayLoadButton = ReplayScrubberLoadButtonRect( screenW, screenH, scrubTrack );
    // Why: paused scenes may not have accumulated two solver frames yet. Scrub,
    // save, and branch tools need retained history; prediction can start from
    // the current live solver state as long as scene physics is available.
    const bool solverToolsEnabled = !loadedPresentation && solverReplayAvailable;
    const bool predictionToolsEnabled = !loadedPresentation && solverReplayEnabled && scenePhysicsEnabled;
    const bool pastPathToolsEnabled = solverToolsEnabled && m_replayRuntime.PathVisualizer().hasTarget;
    // Why: forward prediction scrubbing needs a coherent prediction timeline,
    // not two retained solver samples. The visible build prefix is enough to
    // inspect while a fresh paused scene still lacks normal solver history.
    const bool predictionTimelineAvailable =
        predictionToolsEnabled && ( m_replayRuntime.ActivePredictionFrames().size() >= 2 ||
                                    m_replayRuntime.Prediction().BuildPrefixShouldBePresented() );
    const bool scrubTrackDragEnabled = loadedPresentation || solverToolsEnabled || predictionTimelineAvailable;
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
    const bool overPredictControl = predictionToolsEnabled && predictControl.Contains( mouse.x, mouse.y );
    const bool overPredictToggle = predictionToolsEnabled && predictToggle.Contains( mouse.x, mouse.y );
    const bool overRagdollVisualToggle = predictionToolsEnabled && ragdollVisualToggle.Contains( mouse.x, mouse.y );
    const bool overPastPathToggle = pastPathToolsEnabled && pastPathToggle.Contains( mouse.x, mouse.y );
    const bool overPredictHorizon =
        predictionToolsEnabled && ( predictHorizon.Contains( mouse.x, mouse.y ) ||
                                    ( predictControl.Contains( mouse.x, mouse.y ) && mouse.x >= predictHorizon.x &&
                                      mouse.x <= predictHorizon.x + predictHorizon.w ) );
    const bool overPredictUi = overPredictControl || overPredictToggle || overRagdollVisualToggle || overPredictHorizon;
    // Why: the visible track should claim mouse input directly. If only the
    // bottom hot-zone or paused state opens the gesture, clicking an exposed
    // forward-prediction segment can feel dead until Space changes the mode.
    const bool overScrubTrack = scrubTrackDragEnabled && scrubTrackRect.Contains( mouse.x, mouse.y );
    const RunReplayTrack hoveredTrack = scrubTrack;
    const bool canTakeMouse =
        !uiBlocksMouse || m_replayRuntime.Scrubber().dragging || m_replayRuntime.Prediction().ui.horizonDragging;
    const bool hotZoneCanReveal = inHotZone && !uiBlocksMouse;
    const bool pointerOverScrubberSurface = hotZoneCanReveal || overPanel || overScrubTrack;
    const bool pointerOverScrubberCommand = overSaveButton || overLoadButton || overBranchButton || overPauseButton;
    const bool pointerOverReplayTool = overVelocityEditToggle || overPredictUi || overPastPathToggle;
    const bool pointerRequestsReplayOverlay =
        pointerOverScrubberSurface || pointerOverScrubberCommand || pointerOverReplayTool;
    const bool replayDragInProgress =
        m_replayRuntime.Scrubber().dragging || m_replayRuntime.Prediction().ui.horizonDragging;
    const bool replayStateKeepsScrubberVisible = replayDragInProgress ||
                                                 m_replayRuntime.Scrubber().historicalSamplePaused ||
                                                 m_replayRuntime.Scrubber().liveAdvanceHeld;
    const bool scrubTrackStartTarget =
        inHotZone || overPanel || overScrubTrack || m_replayRuntime.Scrubber().historicalSamplePaused;
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
                const double messageNow = now;
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

        const bool loaded = LoadPresentationArtifact( path,
                                                      true,
                                                      now,
                                                      inputRouter,
                                                      interaction,
                                                      cameras,
                                                      terrain,
                                                      camera,
                                                      mousePickup,
                                                      normalizedCurrentMode,
                                                      normalizedRestoreMode,
                                                      attachedFollow,
                                                      directorGrabbed );
        const double messageNow = now;
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

    // Why: the replay reveal zone is mode-agnostic. Passive Scene/Demo cameras
    // do not own mouse tools, but moving to the bottom edge should still expose
    // retained replay controls. UI-owned mouse areas, such as the minimized
    // options window, should not wake the replay bar. Paused/held replay states
    // pin the bar open without making empty screen space consume mouse input.
    if ( pointerRequestsReplayOverlay || replayStateKeepsScrubberVisible )
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
    const bool solverControlVisible =
        solverToolsEnabled &&
        ( m_replayRuntime.Scrubber().visibleUntil >= now || m_replayRuntime.Scrubber().dragging ||
          m_replayRuntime.Prediction().ui.horizonDragging || m_replayRuntime.Scrubber().historicalSamplePaused ||
          m_replayRuntime.Scrubber().liveAdvanceHeld );
    const bool predictionControlVisible =
        predictionToolsEnabled &&
        ( m_replayRuntime.Scrubber().visibleUntil >= now || m_replayRuntime.Scrubber().dragging ||
          m_replayRuntime.Prediction().ui.horizonDragging || m_replayRuntime.Scrubber().historicalSamplePaused ||
          m_replayRuntime.Scrubber().liveAdvanceHeld );
    m_replayRuntime.Scrubber().pauseHovered = solverToolsEnabled && overPauseButton && solverControlVisible;
    m_replayRuntime.VelocityEdit().toggleHovered = solverToolsEnabled && overVelocityEditToggle && solverControlVisible;
    m_replayRuntime.PathVisualizer().pastPathHovered =
        pastPathToolsEnabled && overPastPathToggle && solverControlVisible;
    m_replayRuntime.Prediction().ui.checkboxHovered =
        predictionToolsEnabled && overPredictToggle && predictionControlVisible;
    m_replayRuntime.Prediction().ui.ragdollVisualsHovered =
        predictionToolsEnabled && overRagdollVisualToggle && predictionControlVisible;
    m_replayRuntime.Prediction().ui.decreaseHovered = false;
    m_replayRuntime.Prediction().ui.increaseHovered = false;
    m_replayRuntime.Prediction().ui.horizonHovered =
        predictionToolsEnabled && overPredictHorizon && predictionControlVisible;

    bool consumesMouse =
        canTakeMouse &&
        ( replayDragInProgress || ( m_replayRuntime.Scrubber().visibleUntil >= now && pointerRequestsReplayOverlay ) );

    if ( branchTargetAvailable &&
         ( restorePressed || ( leftPressed && canTakeMouse && overBranchButton && branchControlVisible ) ) )
    {
        interaction.SetWorldInteractionOwnerInWorkspace( RuntimeWorkspace::Replay,
                                                         WorldInteractionOwner::ReplayBranchTarget,
                                                         InteractionExitReason::EnterReplay );
        ReplayInteractionController replayInteraction;
        (void)replayInteraction.BuildScrubberRestoreRequest( *this, now, outRestoreRequest );
        consumesMouse = true;
        return true;
    }

    auto updateReplayInspectionCamera = [&]()
    {
        if ( m_replayRuntime.ShouldUseInspectionCamera() )
        {
            enterInspectionCamera();
        }
        else
        {
            exitInspectionCamera();
        }
    };

    auto setReplayLiveAdvanceHeld = [&]( bool held )
    {
        const float previousPredictionPresentT = m_replayRuntime.SolverPresentTrackPosition();
        if ( !m_replayRuntime.SetLiveAdvanceHeld( held ) )
        {
            return;
        }

        if ( !held )
        {
            // Why: live play should freeze the last committed path preview.
            // Prediction can still be rebuilt explicitly, but the play button
            // must not let automatic refresh chase the moving live frame.
            RunReplayPredictionState& prediction = m_replayRuntime.Prediction();
            bool promotedBuildPrefix = false;
            if ( prediction.BuildPrefixShouldBePresented() )
            {
                // Why: the overlay may be drawing a build prefix before the
                // full horizon finishes. Promote that visible prefix so Play
                // freezes the lines the user saw instead of clearing them.
                // Hazard: promotion swaps vector ownership and republishes the
                // committed root trajectory, so it must own the worker wait and
                // cleanup as one replay-runtime transition.
                promotedBuildPrefix = m_replayRuntime.PromotePredictionBuildPrefixToCommitted();
            }
            m_replayRuntime.Prediction().enabled = false;
            m_replayRuntime.Prediction().ui.horizonDragging = false;
            if ( !promotedBuildPrefix )
            {
                m_replayRuntime.CancelPredictionJob( false );
            }
            const float currentPosition = m_replayRuntime.TrackPosition( RunReplayTrack::Solver );
            if ( ReplayRuntime::TrackPositionIsFuture( currentPosition, previousPredictionPresentT ) )
            {
                m_replayRuntime.SetTrackPosition( RunReplayTrack::Solver, 1.0f );
                m_replayRuntime.Scrubber().historicalSamplePaused = false;
            }
        }

        if ( held )
        {
            outEnterInteractive = true;
            if ( !IsReplayScrubberToolOwner( m_interaction.Owner() ) )
            {
                interaction.SetWorldInteractionOwnerInWorkspace( RuntimeWorkspace::Replay,
                                                                 WorldInteractionOwner::ReplayScrub,
                                                                 InteractionExitReason::EnterReplay );
            }
        }
        else if ( m_replayRuntime.VelocityEdit().enabled )
        {
            interaction.SetWorldInteractionOwnerInWorkspace( RuntimeWorkspace::Replay,
                                                             WorldInteractionOwner::ReplayVelocityEdit,
                                                             InteractionExitReason::EnterReplay );
        }
        else if ( !m_replayRuntime.Scrubber().historicalSamplePaused &&
                  m_replayRuntime.Camera().focusKind == RunReplayCameraFocusKind::None )
        {
            interaction.EnterCameraMode( camera.mode );
        }
        updateReplayInspectionCamera();
    };

    auto setPredictionHorizonFromMouse = [&]( bool ensureReplayPredictionOwner )
    {
        outEnterInteractive = true;
        if ( ensureReplayPredictionOwner )
        {
            interaction.SetWorldInteractionOwnerInWorkspace( RuntimeWorkspace::Replay,
                                                             WorldInteractionOwner::ReplayPrediction,
                                                             InteractionExitReason::EnterReplay );
        }
        const float nextSeconds = ReplayPredictionHorizonFromMouse( mouse.x, predictHorizon );
        if ( nextSeconds != m_replayRuntime.Prediction().simulation.horizonSeconds )
        {
            m_replayRuntime.Prediction().simulation.horizonSeconds = nextSeconds;
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
            m_replayRuntime.CancelToolDragState( m_interaction, m_inputRouter );
            if ( enableVelocityEdit )
            {
                outEnterInteractive = true;
                setReplayLiveAdvanceHeld( true );
                interaction.SetWorldInteractionOwnerInWorkspace( RuntimeWorkspace::Replay,
                                                                 WorldInteractionOwner::ReplayVelocityEdit,
                                                                 InteractionExitReason::EnterReplay );
            }
            else if ( m_interaction.Owner() == WorldInteractionOwner::ReplayVelocityEdit )
            {
                interaction.SetWorldInteractionOwnerInWorkspace( RuntimeWorkspace::Replay,
                                                                 WorldInteractionOwner::ReplayScrub,
                                                                 InteractionExitReason::EnterReplay );
            }
        }
        m_replayRuntime.Scrubber().visibleUntil = now + REPLAY_SCRUBBER_VISIBLE_SECONDS;
        m_replayRuntime.Scrubber().visible = true;
        consumesMouse = true;
    }
    else if ( pastPathToolsEnabled && leftPressed && canTakeMouse && overPastPathToggle &&
              m_replayRuntime.Scrubber().visibleUntil >= now )
    {
        RunReplayPathVisualizerState& pathVisualizer = m_replayRuntime.PathVisualizer();
        pathVisualizer.pastPathVisible = !pathVisualizer.pastPathVisible;
        if ( !pathVisualizer.pastPathVisible )
        {
            // Why: the retained solver lane is now intentionally hidden, so the
            // automation-facing node cache must not report stale past-path rows.
            pathVisualizer.futureNodes.clear();
        }
        m_replayRuntime.Scrubber().visibleUntil = now + REPLAY_SCRUBBER_VISIBLE_SECONDS;
        m_replayRuntime.Scrubber().visible = true;
        consumesMouse = true;
    }
    else if ( predictionToolsEnabled && leftPressed && canTakeMouse && overRagdollVisualToggle &&
              m_replayRuntime.Scrubber().visibleUntil >= now )
    {
        m_replayRuntime.Prediction().ragdollVisualsEnabled = !m_replayRuntime.Prediction().ragdollVisualsEnabled;
        m_replayRuntime.ClearPredictionFutureNodeCache();
        m_replayRuntime.Scrubber().visibleUntil = now + REPLAY_SCRUBBER_VISIBLE_SECONDS;
        m_replayRuntime.Scrubber().visible = true;
        consumesMouse = true;
    }
    else if ( predictionToolsEnabled && leftPressed && canTakeMouse && overPredictHorizon &&
              m_replayRuntime.Scrubber().visibleUntil >= now )
    {
        m_replayRuntime.Prediction().ui.horizonDragging = true;
        m_replayRuntime.BeginToolGesture( m_interaction,
                                          RuntimeInteractionGestureKind::ReplayPredictionHorizonDrag,
                                          WorldInteractionOwner::ReplayPrediction,
                                          RuntimePointerButton::Left,
                                          mouse.x,
                                          mouse.y );
        setPredictionHorizonFromMouse( false );
        if ( !m_replayRuntime.Scrubber().mouseCaptured )
        {
            m_inputRouter.RequestNativeCapture();
            m_replayRuntime.Scrubber().mouseCaptured = true;
        }
    }
    else if ( predictionToolsEnabled && leftPressed && canTakeMouse && overPredictToggle &&
              m_replayRuntime.Scrubber().visibleUntil >= now )
    {
        outEnterInteractive = true;
        const float previousPredictionPresentT = m_replayRuntime.SolverPresentTrackPosition();
        m_replayRuntime.Prediction().enabled = !m_replayRuntime.Prediction().enabled;
        interaction.SetWorldInteractionOwnerInWorkspace( RuntimeWorkspace::Replay,
                                                         m_replayRuntime.Prediction().enabled
                                                             ? WorldInteractionOwner::ReplayPrediction
                                                             : WorldInteractionOwner::ReplayScrub,
                                                         InteractionExitReason::EnterReplay );
        m_replayRuntime.Prediction().simulation.horizonSeconds =
            std::clamp( m_replayRuntime.Prediction().simulation.horizonSeconds,
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
        outEnterInteractive = true;
        m_replayRuntime.SavePresentationFromScrubber( now );
        consumesMouse = true;
    }
    else if ( leftPressed && canTakeMouse && overLoadButton && m_replayRuntime.Scrubber().visibleUntil >= now )
    {
        promptLoadReplayPresentationArtifact();
        consumesMouse = true;
    }
    else if ( scrubTrackDragEnabled && leftPressed && canTakeMouse && !overBranchButton && !overPauseButton &&
              !overPredictUi && !overPastPathToggle && !overLoadButton && scrubTrackStartTarget )
    {
        outEnterInteractive = true;
        m_replayRuntime.BeginToolGesture( m_interaction,
                                          RuntimeInteractionGestureKind::ReplayScrubDrag,
                                          WorldInteractionOwner::ReplayScrub,
                                          RuntimePointerButton::Left,
                                          mouse.x,
                                          mouse.y );
        m_replayRuntime.Scrubber().activeTrack = scrubTrack;
        m_replayRuntime.SyncActiveTrackPosition();
        m_replayRuntime.Scrubber().dragging = true;
        if ( !m_replayRuntime.Scrubber().mouseCaptured )
        {
            m_inputRouter.RequestNativeCapture();
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
            m_replayRuntime.EndToolGesture( m_interaction, RuntimeInteractionGestureKind::ReplayScrubDrag );
            if ( m_replayRuntime.Scrubber().mouseCaptured )
            {
                m_inputRouter.ReleaseNativeCapture();
                m_replayRuntime.Scrubber().mouseCaptured = false;
            }
        }
    }
    else if ( m_replayRuntime.Prediction().ui.horizonDragging )
    {
        setPredictionHorizonFromMouse( false );
        if ( leftReleased )
        {
            m_replayRuntime.Prediction().ui.horizonDragging = false;
            m_replayRuntime.EndToolGesture( m_interaction, RuntimeInteractionGestureKind::ReplayPredictionHorizonDrag );
            if ( m_replayRuntime.Scrubber().mouseCaptured )
            {
                m_inputRouter.ReleaseNativeCapture();
                m_replayRuntime.Scrubber().mouseCaptured = false;
            }
        }
    }
    else if ( !loadedPresentation && !m_replayRuntime.Scrubber().historicalSamplePaused )
    {
        m_replayRuntime.SetAllTrackPositions( m_replayRuntime.SolverPresentTrackPosition() );
    }

    const bool scrubberTargetVisible =
        m_replayRuntime.Scrubber().dragging || m_replayRuntime.Prediction().ui.horizonDragging ||
        m_replayRuntime.Scrubber().historicalSamplePaused || m_replayRuntime.Scrubber().liveAdvanceHeld ||
        m_replayRuntime.Scrubber().visibleUntil >= now;
    {
        // Concept: visibility is a stateful opacity, not a boolean draw cut.
        // This lets the bottom bar ease in from hover while staying interactive
        // for the whole fade-out tail.
        double& fadeUpdatedAt = m_replayRuntime.Scrubber().fadeUpdatedAt;
        float& visibleAlpha = m_replayRuntime.Scrubber().visibleAlpha;
        if ( fadeUpdatedAt <= 0.0 || now < fadeUpdatedAt )
        {
            fadeUpdatedAt = now;
        }
        const double deltaSeconds = std::clamp( now - fadeUpdatedAt, 0.0, 0.25 );
        fadeUpdatedAt = now;
        const double fadeSeconds =
            scrubberTargetVisible ? REPLAY_SCRUBBER_FADE_IN_SECONDS : REPLAY_SCRUBBER_FADE_OUT_SECONDS;
        const float alphaStep = fadeSeconds > 0.0 ? static_cast<float>( deltaSeconds / fadeSeconds ) : 1.0f;
        visibleAlpha = std::clamp( visibleAlpha + ( scrubberTargetVisible ? alphaStep : -alphaStep ), 0.0f, 1.0f );
        m_replayRuntime.Scrubber().visible = scrubberTargetVisible || visibleAlpha > REPLAY_SCRUBBER_FADE_EPSILON;
    }
    if ( m_replayRuntime.ShouldUseInspectionCamera() )
    {
        enterInspectionCamera();
    }
    else
    {
        exitInspectionCamera();
    }
    return consumesMouse;
}
