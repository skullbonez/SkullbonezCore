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
  Control surface: Disposable front-to-back scrubber table that resolves one
    pointer target from the same named layout rectangles used for drawing.

Invariants:
  - Restoring a sample must set the scrubber status message and consume restore
    input for the current frame.
  - Replay tool pointer ownership must release through the interaction controller.
  - Hit testing must resolve through the scrubber surface; domain handlers may
    not recreate per-button rectangles or fall-through exclusions.

Related:
  - SkullbonezSource/Runtime/Replay/RunReplayTools.cpp
  - SkullbonezSource/Runtime/Replay/ReplayRuntime.h
*/
#include "ReplayRuntime.h"
#include "../../Assets/AssetKeys.h"
#include "../CameraCollection.h"
#include "../InputRouter.h"
#include "../RuntimeInteractionCommands.h"
#include "../RunCameraState.h"
#include "../Tools/RuntimeTools.h"
#include "../../Core/Profiler.h"
#include "../../Core/FatalError.h"
#include "../../Physics/ColliderStore.h"
#include "../../Physics/PhysicsBodyStore.h"
#include "../../Physics/PhysicsEngine.h"
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
    if ( interaction.Gesture().kind == RuntimeInteractionGestureKind::MousePickupDrag )
    {
        inputRouter.ReleaseNativeCapture();
        RuntimeGestureCommand command;
        command.action = RuntimeGestureCommandAction::End;
        command.gesture.kind = RuntimeInteractionGestureKind::MousePickupDrag;
        command.reason = InteractionExitReason::EndGesture;
        RuntimeGestureEvent event;
        (void)interaction.ApplyGestureCommand( command, event );
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

    const Physics::PhysicsBodyStore& bodyStore = Physics::PhysicsEngine::ReadBodies( input.physics );
    const Physics::ColliderStore& colliderStore = Physics::PhysicsEngine::ReadColliders( input.physics );
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

namespace
{
void EndReplayScrubberGesture( ReplayRuntime& replayRuntime,
                               InputRouter& inputRouter,
                               RuntimeInteractionController& interaction,
                               RuntimeInteractionGestureKind kind );

void KeepReplayScrubberVisible( ReplayRuntime& replayRuntime, double now )
{
    replayRuntime.Scrubber().visibleUntil = now + REPLAY_SCRUBBER_VISIBLE_SECONDS;
    replayRuntime.Scrubber().visible = true;
}


void ApplyReplayLiveAdvanceAction( ReplayRuntime& replayRuntime,
                                   bool held,
                                   InputRouter& inputRouter,
                                   RuntimeInteractionController& interaction,
                                   RunCameraState& camera,
                                   bool& outEnterInteractive )
{
    const float previousPredictionPresentT = replayRuntime.SolverPresentTrackPosition();
    if ( !replayRuntime.SetLiveAdvanceHeld( held ) )
    {
        return;
    }

    if ( !held )
    {
        bool promotedBuildPrefix = false;
        if ( replayRuntime.Prediction().BuildPrefixShouldBePresented() )
        {
            // Why: Play freezes the prediction prefix currently visible to the
            // operator, not an older committed path hidden behind worker state.
            promotedBuildPrefix = replayRuntime.PromotePredictionBuildPrefixToCommitted();
        }
        replayRuntime.Prediction().enabled = false;
        if ( interaction.Gesture().kind == RuntimeInteractionGestureKind::ReplayPredictionHorizonDrag )
        {
            EndReplayScrubberGesture( replayRuntime,
                                      inputRouter,
                                      interaction,
                                      RuntimeInteractionGestureKind::ReplayPredictionHorizonDrag );
        }
        if ( !promotedBuildPrefix )
        {
            replayRuntime.CancelPredictionJob( false );
        }
        const float currentPosition = replayRuntime.TrackPosition( RunReplayTrack::Solver );
        if ( ReplayRuntime::TrackPositionIsFuture( currentPosition, previousPredictionPresentT ) )
        {
            replayRuntime.SetTrackPosition( RunReplayTrack::Solver, 1.0f );
            replayRuntime.Scrubber().historicalSamplePaused = false;
        }
    }

    if ( held )
    {
        outEnterInteractive = true;
        if ( !IsReplayScrubberToolOwner( interaction.Owner() ) )
        {
            interaction.SetWorldInteractionOwnerInWorkspace( RuntimeWorkspace::Replay,
                                                             WorldInteractionOwner::ReplayScrub,
                                                             InteractionExitReason::EnterReplay );
        }
    }
    else if ( replayRuntime.VelocityEdit().enabled )
    {
        interaction.SetWorldInteractionOwnerInWorkspace( RuntimeWorkspace::Replay,
                                                         WorldInteractionOwner::ReplayVelocityEdit,
                                                         InteractionExitReason::EnterReplay );
    }
    else if ( !replayRuntime.Scrubber().historicalSamplePaused &&
              replayRuntime.Camera().focusKind == RunReplayCameraFocusKind::None )
    {
        interaction.EnterCameraMode( camera.mode );
    }
}


void HandleReplayPausePressed( ReplayRuntime& replayRuntime,
                               InputRouter& inputRouter,
                               RuntimeInteractionController& interaction,
                               RunCameraState& camera,
                               double now,
                               bool& outEnterInteractive )
{
    ApplyReplayLiveAdvanceAction( replayRuntime,
                                  !replayRuntime.Scrubber().liveAdvanceHeld,
                                  inputRouter,
                                  interaction,
                                  camera,
                                  outEnterInteractive );
    KeepReplayScrubberVisible( replayRuntime, now );
}


void HandleReplayVelocityEditPressed( ReplayRuntime& replayRuntime,
                                      InputRouter& inputRouter,
                                      RuntimeInteractionController& interaction,
                                      RunCameraState& camera,
                                      double now,
                                      bool& outEnterInteractive )
{
    const bool enableVelocityEdit = !replayRuntime.VelocityEdit().enabled;
    if ( replayRuntime.SetVelocityEditEnabled( enableVelocityEdit ) )
    {
        replayRuntime.CancelToolDragState( interaction, inputRouter );
        if ( enableVelocityEdit )
        {
            outEnterInteractive = true;
            ApplyReplayLiveAdvanceAction( replayRuntime, true, inputRouter, interaction, camera, outEnterInteractive );
            interaction.SetWorldInteractionOwnerInWorkspace( RuntimeWorkspace::Replay,
                                                             WorldInteractionOwner::ReplayVelocityEdit,
                                                             InteractionExitReason::EnterReplay );
        }
        else if ( interaction.Owner() == WorldInteractionOwner::ReplayVelocityEdit )
        {
            interaction.SetWorldInteractionOwnerInWorkspace( RuntimeWorkspace::Replay,
                                                             WorldInteractionOwner::ReplayScrub,
                                                             InteractionExitReason::EnterReplay );
        }
    }
    KeepReplayScrubberVisible( replayRuntime, now );
}


void HandleReplayPastPathPressed( ReplayRuntime& replayRuntime, double now )
{
    RunReplayPathVisualizerState& pathVisualizer = replayRuntime.PathVisualizer();
    pathVisualizer.pastPathVisible = !pathVisualizer.pastPathVisible;
    if ( !pathVisualizer.pastPathVisible )
    {
        pathVisualizer.futureNodes.clear();
    }
    KeepReplayScrubberVisible( replayRuntime, now );
}


void HandleReplayRagdollVisualsPressed( ReplayRuntime& replayRuntime, double now )
{
    replayRuntime.Prediction().ragdollVisualsEnabled = !replayRuntime.Prediction().ragdollVisualsEnabled;
    replayRuntime.ClearPredictionFutureNodeCache();
    KeepReplayScrubberVisible( replayRuntime, now );
}


void SetReplayPredictionHorizonFromPointer( ReplayRuntime& replayRuntime,
                                            RuntimeInteractionController& interaction,
                                            const SkullbonezCore::UI::UIRect& horizon,
                                            int mouseX,
                                            double now,
                                            bool ensurePredictionOwner,
                                            bool& outEnterInteractive )
{
    outEnterInteractive = true;
    if ( ensurePredictionOwner )
    {
        interaction.SetWorldInteractionOwnerInWorkspace( RuntimeWorkspace::Replay,
                                                         WorldInteractionOwner::ReplayPrediction,
                                                         InteractionExitReason::EnterReplay );
    }
    const float nextSeconds = ReplayPredictionHorizonFromMouse( mouseX, horizon );
    if ( nextSeconds != replayRuntime.Prediction().simulation.horizonSeconds )
    {
        replayRuntime.Prediction().simulation.horizonSeconds = nextSeconds;
        replayRuntime.MarkPredictionDirty();
    }
    KeepReplayScrubberVisible( replayRuntime, now );
}


void HandleReplayPredictionPressed( ReplayRuntime& replayRuntime,
                                    RuntimeInteractionController& interaction,
                                    double now,
                                    bool& outEnterInteractive )
{
    outEnterInteractive = true;
    const float previousPredictionPresentT = replayRuntime.SolverPresentTrackPosition();
    replayRuntime.Prediction().enabled = !replayRuntime.Prediction().enabled;
    interaction.SetWorldInteractionOwnerInWorkspace( RuntimeWorkspace::Replay,
                                                     replayRuntime.Prediction().enabled
                                                         ? WorldInteractionOwner::ReplayPrediction
                                                         : WorldInteractionOwner::ReplayScrub,
                                                     InteractionExitReason::EnterReplay );
    replayRuntime.Prediction().simulation.horizonSeconds =
        std::clamp( replayRuntime.Prediction().simulation.horizonSeconds,
                    REPLAY_PREDICTION_MIN_SECONDS,
                    REPLAY_PREDICTION_MAX_SECONDS );
    if ( !replayRuntime.Prediction().enabled )
    {
        const float currentPosition = replayRuntime.TrackPosition( RunReplayTrack::Solver );
        if ( ReplayRuntime::TrackPositionIsFuture( currentPosition, previousPredictionPresentT ) )
        {
            replayRuntime.SetTrackPosition( RunReplayTrack::Solver, 1.0f );
            replayRuntime.Scrubber().historicalSamplePaused = false;
        }
        replayRuntime.ClearPredictionCache();
    }
    replayRuntime.MarkPredictionDirty();
    KeepReplayScrubberVisible( replayRuntime, now );
}


void HandleReplayBranchPressed( ReplayRuntime& replayRuntime,
                                RuntimeInteractionController& interaction,
                                double now,
                                ReplayLiveRestoreRequest& outRestoreRequest )
{
    interaction.SetWorldInteractionOwnerInWorkspace( RuntimeWorkspace::Replay,
                                                     WorldInteractionOwner::ReplayBranchTarget,
                                                     InteractionExitReason::EnterReplay );
    ReplayInteractionController replayInteraction;
    (void)replayInteraction.BuildScrubberRestoreRequest( replayRuntime, now, outRestoreRequest );
}


void HandleReplaySavePressed( ReplayRuntime& replayRuntime, double now, bool& outEnterInteractive )
{
    outEnterInteractive = true;
    replayRuntime.SavePresentationFromScrubber( now );
}


bool BeginReplayScrubberGesture( ReplayRuntime& replayRuntime,
                                 InputRouter& inputRouter,
                                 RuntimeInteractionController& interaction,
                                 RuntimeInteractionGestureKind kind,
                                 WorldInteractionOwner owner,
                                 int mouseX,
                                 int mouseY )
{
    if ( !replayRuntime.BeginToolGesture( interaction, kind, owner, RuntimePointerButton::Left, mouseX, mouseY ) )
    {
        return false;
    }
    inputRouter.RequestNativeCapture();
    return true;
}


void EndReplayScrubberGesture( ReplayRuntime& replayRuntime,
                               InputRouter& inputRouter,
                               RuntimeInteractionController& interaction,
                               RuntimeInteractionGestureKind kind )
{
    replayRuntime.EndToolGesture( interaction, kind );
    inputRouter.ReleaseNativeCapture();
}


void HandleReplayLoadPressed( ReplayRuntime& replayRuntime,
                              HWND window,
                              double now,
                              InputRouter& inputRouter,
                              RuntimeInteractionController& interaction,
                              SkullbonezCore::Environment::CameraCollection* cameras,
                              SkullbonezCore::Geometry::Terrain* terrain,
                              RunCameraState& camera,
                              RunMousePickupState& mousePickup,
                              RunCameraMode normalizedCurrentMode,
                              RunCameraMode normalizedRestoreMode,
                              bool attachedFollow,
                              bool directorGrabbed )
{
    // Why: the native picker is cold UI. It runs only after typed Load dispatch
    // and never becomes a stored callback or per-frame service dependency.
    char path[MAX_PATH] = {};
    OPENFILENAMEA openFile = {};
    openFile.lStructSize = sizeof( openFile );
    openFile.hwndOwner = window;
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
            sprintf_s( replayRuntime.Scrubber().saveMessage,
                       sizeof( replayRuntime.Scrubber().saveMessage ),
                       "REPLAY PICKER FAILED" );
            replayRuntime.Scrubber().saveMessageTrack = RunReplayTrack::Presentation;
            replayRuntime.Scrubber().saveMessageUntil = now + 2.5;
            KeepReplayScrubberVisible( replayRuntime, now );
        }
        return;
    }

    const bool loaded = replayRuntime.LoadPresentationArtifact( path,
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
    const char* fileName = strrchr( path, '\\' );
    if ( !fileName )
    {
        fileName = strrchr( path, '/' );
    }
    fileName = fileName ? fileName + 1 : path;

    replayRuntime.Scrubber().saveMessageTrack = RunReplayTrack::Presentation;
    if ( loaded )
    {
        constexpr int loadedPrefixLength = 7;
        constexpr int loadedFileNameLimit =
            static_cast<int>( sizeof( replayRuntime.Scrubber().saveMessage ) ) - loadedPrefixLength - 1;
        sprintf_s( replayRuntime.Scrubber().saveMessage,
                   sizeof( replayRuntime.Scrubber().saveMessage ),
                   "LOADED %.*s",
                   loadedFileNameLimit,
                   fileName );
    }
    else
    {
        sprintf_s( replayRuntime.Scrubber().saveMessage,
                   sizeof( replayRuntime.Scrubber().saveMessage ),
                   "REPLAY LOAD FAILED" );
    }
    replayRuntime.Scrubber().saveMessageUntil = now + 2.5;
    KeepReplayScrubberVisible( replayRuntime, now );
}


bool HandleReplayPredictionHorizonPressed( ReplayRuntime& replayRuntime,
                                           InputRouter& inputRouter,
                                           RuntimeInteractionController& interaction,
                                           const SkullbonezCore::UI::UIRect& horizon,
                                           int mouseX,
                                           int mouseY,
                                           double now,
                                           bool& outEnterInteractive )
{
    if ( !BeginReplayScrubberGesture( replayRuntime,
                                      inputRouter,
                                      interaction,
                                      RuntimeInteractionGestureKind::ReplayPredictionHorizonDrag,
                                      WorldInteractionOwner::ReplayPrediction,
                                      mouseX,
                                      mouseY ) )
    {
        return false;
    }
    SetReplayPredictionHorizonFromPointer( replayRuntime,
                                           interaction,
                                           horizon,
                                           mouseX,
                                           now,
                                           false,
                                           outEnterInteractive );
    return true;
}


bool HandleReplayScrubPressed( ReplayRuntime& replayRuntime,
                               InputRouter& inputRouter,
                               RuntimeInteractionController& interaction,
                               RunReplayTrack track,
                               int mouseX,
                               int mouseY,
                               bool& outEnterInteractive )
{
    outEnterInteractive = true;
    if ( !BeginReplayScrubberGesture( replayRuntime,
                                      inputRouter,
                                      interaction,
                                      RuntimeInteractionGestureKind::ReplayScrubDrag,
                                      WorldInteractionOwner::ReplayScrub,
                                      mouseX,
                                      mouseY ) )
    {
        return false;
    }
    replayRuntime.Scrubber().activeTrack = track;
    replayRuntime.SyncActiveTrackPosition();
    return true;
}


bool TickReplayScrubberGesture( ReplayRuntime& replayRuntime,
                                InputRouter& inputRouter,
                                RuntimeInteractionController& interaction,
                                bool loadedPresentation,
                                int mouseX,
                                int screenW,
                                int screenH,
                                const SkullbonezCore::UI::UIRect& predictionHorizon,
                                bool leftReleased,
                                double now,
                                bool& outEnterInteractive )
{
    switch ( interaction.Gesture().kind )
    {
    case RuntimeInteractionGestureKind::ReplayScrubDrag:
    {
        replayRuntime.SetTrackPosition(
            replayRuntime.Scrubber().activeTrack,
            ReplayScrubberPositionFromMouse( mouseX, screenW, screenH, replayRuntime.Scrubber().activeTrack ) );
        if ( loadedPresentation )
        {
            replayRuntime.Scrubber().historicalSamplePaused = true;
        }
        else
        {
            const float presentT = replayRuntime.SolverPresentTrackPosition();
            if ( ReplayRuntime::AtPresentTrackPosition( replayRuntime.Scrubber().position, presentT ) )
            {
                replayRuntime.SetTrackPosition( replayRuntime.Scrubber().activeTrack, presentT );
                replayRuntime.Scrubber().historicalSamplePaused = false;
            }
            else
            {
                replayRuntime.Scrubber().historicalSamplePaused = true;
            }
        }
        if ( leftReleased )
        {
            EndReplayScrubberGesture( replayRuntime,
                                      inputRouter,
                                      interaction,
                                      RuntimeInteractionGestureKind::ReplayScrubDrag );
        }
        return true;
    }
    case RuntimeInteractionGestureKind::ReplayPredictionHorizonDrag:
        SetReplayPredictionHorizonFromPointer( replayRuntime,
                                               interaction,
                                               predictionHorizon,
                                               mouseX,
                                               now,
                                               false,
                                               outEnterInteractive );
        if ( leftReleased )
        {
            EndReplayScrubberGesture( replayRuntime,
                                      inputRouter,
                                      interaction,
                                      RuntimeInteractionGestureKind::ReplayPredictionHorizonDrag );
        }
        return true;
    default:
        return false;
    }
}
} // namespace

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

    const auto scrubDragActive = [&]()
    { return m_interaction.Gesture().kind == RuntimeInteractionGestureKind::ReplayScrubDrag; };
    const auto horizonDragActive = [&]()
    { return m_interaction.Gesture().kind == RuntimeInteractionGestureKind::ReplayPredictionHorizonDrag; };

    const ReplayScrubberSurfaceInput surfaceInput = DescribeReplayScrubberSurface( *this,
                                                                                   scenePhysicsEnabled,
                                                                                   uiBlocksMouse,
                                                                                   screenW,
                                                                                   screenH,
                                                                                   m_interaction.Gesture().kind );
    const RunReplayTrack scrubTrack = surfaceInput.track;
    const bool branchTargetAvailable = surfaceInput.branchTargetAvailable;
    ReplayScrubberSurface surface;
    BuildReplayScrubberSurface( surfaceInput, surface );
    surface.ResolvePointer( mouse.x, mouse.y );

    const auto isHotControl = [&]( ReplayScrubberControl control )
    { return surface.hasHotControl && surface.hotControl == ReplayScrubberControlId( control ); };
    const RuntimeUiControl* pointerControl =
        surface.hasPointerControl ? surface.Find( surface.pointerControl ) : nullptr;
    const RuntimeUiControl* horizonControl =
        surface.Find( ReplayScrubberControlId( ReplayScrubberControl::PredictionHorizon ) );
    // Invariant: the fixed scrubber builder always publishes the horizon row;
    // disabled state changes eligibility, not the geometry table.
    if ( !horizonControl )
    {
        SB_FATAL( "ReplayScrubberSurface", "Prediction horizon control is missing from the scrubber surface." );
    }
    const UI::UIRect predictHorizon = horizonControl->drawRect;

    const bool canTakeMouse = !uiBlocksMouse || scrubDragActive() || horizonDragActive();
    const bool pointerRequestsReplayOverlay = pointerControl && pointerControl->requestsReveal;
    const bool replayDragInProgress = scrubDragActive() || horizonDragActive();
    const bool replayStateKeepsScrubberVisible = replayDragInProgress ||
                                                 m_replayRuntime.Scrubber().historicalSamplePaused ||
                                                 m_replayRuntime.Scrubber().liveAdvanceHeld;
    // Why: only the track or its broad background/reveal rows may begin a scrub.
    // A disabled front-most control still blocks fall-through to these rows.
    const bool scrubTrackStartTarget =
        isHotControl( ReplayScrubberControl::ScrubTrack ) || isHotControl( ReplayScrubberControl::Panel ) ||
        isHotControl( ReplayScrubberControl::HotZone ) ||
        ( m_replayRuntime.Scrubber().historicalSamplePaused && !surface.hasPointerControl );
    // Why: the replay reveal zone is mode-agnostic. Passive Scene/Demo cameras
    // do not own mouse tools, but moving to the bottom edge should still expose
    // retained replay controls. UI-owned mouse areas, such as the minimized
    // options window, should not wake the replay bar. Paused/held replay states
    // pin the bar open without making empty screen space consume mouse input.
    if ( pointerRequestsReplayOverlay || replayStateKeepsScrubberVisible )
    {
        m_replayRuntime.Scrubber().visibleUntil = now + REPLAY_SCRUBBER_VISIBLE_SECONDS;
    }
    const bool branchControlVisible = m_replayRuntime.Scrubber().visibleUntil >= now || scrubDragActive() ||
                                      m_replayRuntime.Scrubber().historicalSamplePaused ||
                                      m_replayRuntime.Scrubber().liveAdvanceHeld;

    bool consumesMouse =
        canTakeMouse &&
        ( replayDragInProgress || ( m_replayRuntime.Scrubber().visibleUntil >= now && pointerRequestsReplayOverlay ) );

    ReplayScrubberAction requestedAction = ReplayScrubberAction::None;
    if ( branchTargetAvailable && restorePressed )
    {
        requestedAction = ReplayScrubberAction::RestoreBranch;
    }
    else if ( leftPressed && canTakeMouse )
    {
        const RuntimeUiControl* hotControl = surface.hasHotControl ? surface.Find( surface.hotControl ) : nullptr;
        if ( hotControl )
        {
            requestedAction = static_cast<ReplayScrubberAction>( hotControl->action.value );
            if ( requestedAction == ReplayScrubberAction::RestoreBranch && !branchControlVisible )
            {
                requestedAction = ReplayScrubberAction::None;
            }
            else if ( requestedAction != ReplayScrubberAction::None && requestedAction != ReplayScrubberAction::Scrub &&
                      requestedAction != ReplayScrubberAction::RestoreBranch &&
                      m_replayRuntime.Scrubber().visibleUntil < now )
            {
                requestedAction = ReplayScrubberAction::None;
            }
            else if ( requestedAction == ReplayScrubberAction::None && scrubTrackStartTarget )
            {
                requestedAction = ReplayScrubberAction::Scrub;
            }
        }
        else if ( scrubTrackStartTarget )
        {
            requestedAction = ReplayScrubberAction::Scrub;
        }
    }

    // Concept: pointer rows and keyboard shortcuts select the same semantic
    // action before any owner mutation. The switch is an explicit value dispatch,
    // not a callback table retained on the hot path.
    switch ( requestedAction )
    {
    case ReplayScrubberAction::RestoreBranch:
        HandleReplayBranchPressed( *this, m_interaction, now, outRestoreRequest );
        return true;
    case ReplayScrubberAction::TogglePause:
        HandleReplayPausePressed( *this, m_inputRouter, m_interaction, camera, now, outEnterInteractive );
        consumesMouse = true;
        break;
    case ReplayScrubberAction::ToggleVelocityEdit:
        HandleReplayVelocityEditPressed( *this, m_inputRouter, m_interaction, camera, now, outEnterInteractive );
        consumesMouse = true;
        break;
    case ReplayScrubberAction::TogglePastPath:
        HandleReplayPastPathPressed( *this, now );
        consumesMouse = true;
        break;
    case ReplayScrubberAction::ToggleRagdollVisuals:
        HandleReplayRagdollVisualsPressed( *this, now );
        consumesMouse = true;
        break;
    case ReplayScrubberAction::SetPredictionHorizon:
        if ( !HandleReplayPredictionHorizonPressed( *this,
                                                    m_inputRouter,
                                                    m_interaction,
                                                    predictHorizon,
                                                    mouse.x,
                                                    mouse.y,
                                                    now,
                                                    outEnterInteractive ) )
        {
            return consumesMouse;
        }
        consumesMouse = true;
        break;
    case ReplayScrubberAction::TogglePrediction:
        HandleReplayPredictionPressed( *this, m_interaction, now, outEnterInteractive );
        consumesMouse = true;
        break;
    case ReplayScrubberAction::Save:
        HandleReplaySavePressed( *this, now, outEnterInteractive );
        consumesMouse = true;
        break;
    case ReplayScrubberAction::Load:
        HandleReplayLoadPressed( *this,
                                 hwnd,
                                 now,
                                 m_inputRouter,
                                 m_interaction,
                                 cameras,
                                 terrain,
                                 camera,
                                 mousePickup,
                                 normalizedCurrentMode,
                                 normalizedRestoreMode,
                                 attachedFollow,
                                 directorGrabbed );
        consumesMouse = true;
        break;
    case ReplayScrubberAction::Scrub:
        if ( !HandleReplayScrubPressed( *this,
                                        m_inputRouter,
                                        m_interaction,
                                        scrubTrack,
                                        mouse.x,
                                        mouse.y,
                                        outEnterInteractive ) )
        {
            return consumesMouse;
        }
        break;
    case ReplayScrubberAction::None:
        break;
    }

    const bool scrubberGestureHandled = TickReplayScrubberGesture( *this,
                                                                   m_inputRouter,
                                                                   m_interaction,
                                                                   loadedPresentation,
                                                                   mouse.x,
                                                                   screenW,
                                                                   screenH,
                                                                   predictHorizon,
                                                                   leftReleased,
                                                                   now,
                                                                   outEnterInteractive );
    consumesMouse = consumesMouse || scrubberGestureHandled;
    if ( !scrubberGestureHandled && !loadedPresentation && !m_replayRuntime.Scrubber().historicalSamplePaused )
    {
        m_replayRuntime.SetAllTrackPositions( m_replayRuntime.SolverPresentTrackPosition() );
    }

    const bool scrubberTargetVisible =
        scrubDragActive() || horizonDragActive() || m_replayRuntime.Scrubber().historicalSamplePaused ||
        m_replayRuntime.Scrubber().liveAdvanceHeld || m_replayRuntime.Scrubber().visibleUntil >= now;
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
