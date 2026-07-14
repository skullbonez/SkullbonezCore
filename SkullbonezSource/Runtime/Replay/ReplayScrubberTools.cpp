/*
File: SkullbonezSource/Runtime/Replay/ReplayScrubberTools.cpp
Purpose:
  Implements replay scrubber input, inspection-camera, and live-restore policy.

Summary:
  The scrubber maps mouse/UI intent to retained solver or presentation samples.
  ReplayScrubber owns cursor transitions; ReplayRuntime receives a frame-scoped
  workspace view and coordinates restore/application commands across owners.

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
  - SkullbonezSource/Runtime/Replay/ReplayPrediction.cpp
  - SkullbonezSource/Runtime/Replay/ReplayRuntime.h
*/
#include "ReplayScrubber.h"
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
#include "ReplayOverlayLayout.h"
#include "ReplayRuntimeOwnerViews.h"
#include "../../World/Terrain.h"

#include <algorithm>
#include <cstddef>
#include <cstdio>
#include <cstring>

#include <commdlg.h>

using namespace SkullbonezCore::Runtime;
using namespace SkullbonezCore::Math::Vector;
using namespace SkullbonezCore::Runtime::ReplayOverlay;
using SkullbonezCore::Geometry::XZBounds;

bool ReplayScrubber::BuildRestoreRequest( const ReplayScrubberRestoreSources& sources,
                                          double now,
                                          ReplayLiveRestoreRequest& outRequest,
                                          char* outReason,
                                          std::size_t reasonSize )
{
    outRequest = ReplayLiveRestoreRequest{};
    outRequest.now = now;
    if ( sources.hasLoadedPresentation && m_state.historicalSamplePaused &&
         m_state.activeTrack == RunReplayTrack::Presentation && sources.presentationSample )
    {
        outRequest.kind = ReplayLiveRestoreKind::V2ArtifactTarget;
        outRequest.requestedFrame = sources.presentationSample->frameIndex;
        outRequest.makeLiveBranch = true;
        outRequest.enterInteractive = true;
        outRequest.messageTrack = RunReplayTrack::Presentation;
        strncpy_s( outRequest.path,
                   sizeof( outRequest.path ),
                   sources.loadedPresentationPath ? sources.loadedPresentationPath : "",
                   _TRUNCATE );
        return true;
    }
    if ( m_state.historicalSamplePaused && m_state.activeTrack == RunReplayTrack::Solver && sources.solverSample )
    {
        outRequest.kind = ReplayLiveRestoreKind::SolverSample;
        outRequest.solverSample = sources.solverSample;
        outRequest.enterInteractive = true;
        outRequest.messageTrack = RunReplayTrack::Solver;
        return true;
    }

    const char* reason = "no historical replay branch target selected";
    fprintf( stderr, "[replay] Branch restore failed: %s\n", reason );
    PublishRestoreResult( now, false, m_state.activeTrack );
    WriteRestoreReason( outReason, reasonSize, reason );
    return false;
}


void ReplayScrubber::CompleteRestore( const ReplayLiveRestoreRequest& request,
                                      bool restored,
                                      const RunReplayV2TargetRestoreResult& v2Result,
                                      const char* reason,
                                      RunReplayV2TargetRestoreResult* outV2Result,
                                      char* outReason,
                                      std::size_t reasonSize )
{
    const char* safeReason = reason ? reason : "";
    if ( request.kind == ReplayLiveRestoreKind::V2ArtifactTarget )
    {
        if ( outV2Result )
        {
            *outV2Result = v2Result;
        }
        fprintf( stderr,
                 "[replay] V2 file restore %s target_frame=%llu branch_id=%u%s%s\n",
                 restored ? "applied" : "failed",
                 static_cast<unsigned long long>( request.requestedFrame ),
                 restored ? v2Result.branchId : 0,
                 safeReason[0] != '\0' ? ": " : "",
                 safeReason );
    }
    else
    {
        fprintf( stderr,
                 "[replay] Solver restore %s%s%s\n",
                 restored ? "applied" : "failed",
                 safeReason[0] != '\0' ? ": " : "",
                 safeReason );
    }

    if ( restored )
    {
        // Why: the restored historical frame is now the live timeline edge;
        // retaining the parent cursor would advertise authority that no longer exists.
        m_state.activeTrack = RunReplayTrack::Solver;
        m_state.historicalSamplePaused = false;
        SetAllTrackPositions( 1.0f );
    }
    PublishRestoreResult( request.now, restored, request.messageTrack );
    WriteRestoreReason( outReason, reasonSize, safeReason );
}


void ReplayScrubber::WriteRestoreReason( char* outReason, std::size_t reasonSize, const char* reason )
{
    if ( outReason && reasonSize > 0 )
    {
        strncpy_s( outReason, reasonSize, reason ? reason : "restore failed", _TRUNCATE );
    }
}


void ReplayScrubber::PublishRestoreResult( double now, bool restored, RunReplayTrack messageTrack )
{
    m_state.restoreConsumedThisFrame = true;
    m_state.saveMessageTrack = messageTrack;
    sprintf_s( m_state.saveMessage,
               sizeof( m_state.saveMessage ),
               restored ? ( messageTrack == RunReplayTrack::Presentation ? "V2 FILE BRANCHED" : "SOLVER RESTORED" )
                        : "RESTORE FAILED" );
    m_state.saveMessageUntil = now + 2.5;
    m_state.visibleUntil = now + REPLAY_SCRUBBER_VISIBLE_SECONDS;
    m_state.visible = true;
}

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

    const bool enteringInspectionCamera = !m_visualPresentation.CameraView().active;
    if ( enteringInspectionCamera )
    {
        const uint32_t restoreCameraHash = cameras->GetSelectedCameraName();

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

        m_visualPresentation.BeginCameraInspection( normalizedCurrentMode, restoreCameraHash, eye, view, up );
        cameras->SelectCamera( CAMERA_FREE, false );
        cameras->TweenPrimaryToPose( eye, view, up );
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
    const RunReplayCameraState replayCamera = m_visualPresentation.CameraView();
    if ( !replayCamera.active )
    {
        return;
    }

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
        uint32_t restoreCameraHash = replayCamera.restoreCameraHash;
        bool restoreCameraAvailable = cameras->HasCamera( restoreCameraHash );
        if ( !restoreCameraAvailable && cameras->HasCamera( CAMERA_FREE ) )
        {
            restoreCameraHash = CAMERA_FREE;
            restoreCameraAvailable = true;
        }
        if ( restoreCameraAvailable )
        {
            cameras->SelectCamera( restoreCameraHash, false );
            if ( replayCamera.hasRestorePose )
            {
                cameras->TweenPrimaryToPose( replayCamera.restoreEye,
                                             replayCamera.restoreView,
                                             replayCamera.restoreUp );
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
    m_visualPresentation.EndCameraInspection();
    inputRouter.RequestCursorVisible( true );
    InputController::ResetMouseLook( camera );
}


void ReplayRuntime::TickWorkspace( const ReplayWorkspaceFrameInput& input,
                                   InputRouter& inputRouter,
                                   RuntimeInteractionController& interaction,
                                   Physics::PhysicsEngine& physics,
                                   const SceneEntityStore& entities,
                                   std::span<const Rendering::RenderInstancePresentationRecord> presentation,
                                   Environment::CameraCollection* cameras,
                                   Geometry::Terrain* terrain,
                                   RunCameraState& camera,
                                   RunMousePickupState& mousePickup,
                                   ReplayWorkspaceOutput& output )
{
    output = ReplayWorkspaceOutput{};

    bool enterInteractive = false;
    const bool scrubberOwnsMouse = TickScrubberInput( input.window,
                                                      input.uiBlocksMouse,
                                                      inputRouter,
                                                      interaction,
                                                      cameras,
                                                      terrain,
                                                      camera,
                                                      mousePickup,
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

    const Physics::PhysicsBodyStore& bodyStore = Physics::PhysicsEngine::ReadBodies( physics );
    const Physics::ColliderStore& colliderStore = Physics::PhysicsEngine::ReadColliders( physics );
    const bool causeTreeOwnsMouse = TickCauseTreeInput( input.uiBlocksMouse || scrubberOwnsMouse,
                                                        input.wheelDelta,
                                                        inputRouter,
                                                        interaction,
                                                        bodyStore,
                                                        colliderStore,
                                                        presentation,
                                                        cameras,
                                                        terrain,
                                                        camera,
                                                        mousePickup,
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
                               inputRouter,
                               interaction,
                               physics,
                               entities,
                               presentation,
                               cameras,
                               terrain,
                               camera,
                               mousePickup,
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


void ReplayRuntime::ResetSceneTimeline( const ReplaySceneTimelineResetInput& input,
                                        const ReplaySceneTimelineResetOwners& owners )
{
    CancelToolDragState( owners.interaction, owners.inputRouter );
    const ReplaySceneTimelineResetResult begin = BeginSceneTimelineReset( input );
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

    const ReplaySceneTimelineResetResult finish = FinishSceneTimelineReset( input );
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


ReplayLiveRestoreOutcome ReplayRuntime::ApplyLiveRestoreRequest( const ReplayRestoreTransaction& transaction,
                                                                 const ReplayArtifactTopologyOwners& topologyOwners,
                                                                 const ReplayLiveRestoreRequest& request )
{
    ReplayLiveRestoreOutcome outcome;
    outcome.requested = request.kind != ReplayLiveRestoreKind::None;
    if ( !outcome.requested )
    {
        return outcome;
    }

    // Invariant: restore is an owner-to-owner transaction. Prediction must be
    // idle before either restore path mutates live physics authority, even when
    // a caller constructs the request without using the scrubber request builder.
    m_predictionOwner.CancelJob( false );

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

    outcome.v2Result = v2Result;
    strncpy_s( outcome.reason, sizeof( outcome.reason ), reason, _TRUNCATE );
    m_scrubberOwner.CompleteRestore( request, outcome.restored, v2Result, reason );
    outcome.enterInteractive = v2Result.enterInteractiveRequested;
    return outcome;
}

namespace
{
void EndReplayScrubberGesture( ReplayRuntime& replayRuntime,
                               InputRouter& inputRouter,
                               RuntimeInteractionController& interaction,
                               RuntimeInteractionGestureKind kind );

void KeepReplayScrubberVisible( ReplayScrubber& scrubber, double now )
{
    scrubber.KeepVisible( now, REPLAY_SCRUBBER_VISIBLE_SECONDS );
}


void ApplyReplayLiveAdvanceAction( ReplayRuntime& replayRuntime,
                                   ReplayPrediction& predictionOwner,
                                   ReplayScrubber& scrubber,
                                   bool held,
                                   InputRouter& inputRouter,
                                   RuntimeInteractionController& interaction,
                                   RunCameraState& camera,
                                   bool& outEnterInteractive )
{
    const float previousPredictionPresentT = replayRuntime.BuildInputView().solverPresentTrackPosition;
    if ( !replayRuntime.SetLiveAdvanceHeld( held ) )
    {
        return;
    }

    if ( !held )
    {
        bool promotedBuildPrefix = false;
        if ( predictionOwner.BuildPrefixShouldBePresented() )
        {
            // Why: Play freezes the prediction prefix currently visible to the
            // operator, not an older committed path hidden behind worker state.
            promotedBuildPrefix = predictionOwner.PromoteBuildPrefixToCommitted();
        }
        predictionOwner.DisableForLiveAdvance();
        if ( interaction.Gesture().kind == RuntimeInteractionGestureKind::ReplayPredictionHorizonDrag )
        {
            EndReplayScrubberGesture( replayRuntime,
                                      inputRouter,
                                      interaction,
                                      RuntimeInteractionGestureKind::ReplayPredictionHorizonDrag );
        }
        if ( !promotedBuildPrefix )
        {
            predictionOwner.CancelJob( false );
        }
        const float currentPosition = scrubber.TrackPosition( RunReplayTrack::Solver );
        if ( ReplayTrackPositionIsFuture( currentPosition, previousPredictionPresentT ) )
        {
            scrubber.SetTrackPosition( RunReplayTrack::Solver, 1.0f );
            scrubber.SetHistoricalSamplePaused( false );
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
    else if ( replayRuntime.BuildInputView().velocityEditEnabled )
    {
        interaction.SetWorldInteractionOwnerInWorkspace( RuntimeWorkspace::Replay,
                                                         WorldInteractionOwner::ReplayVelocityEdit,
                                                         InteractionExitReason::EnterReplay );
    }
    else if ( !scrubber.View().historicalSamplePaused && !replayRuntime.BuildInputView().hasCameraFocus )
    {
        interaction.EnterCameraMode( camera.mode );
    }
}


void HandleReplayPausePressed( ReplayRuntime& replayRuntime,
                               ReplayPrediction& predictionOwner,
                               ReplayScrubber& scrubber,
                               InputRouter& inputRouter,
                               RuntimeInteractionController& interaction,
                               RunCameraState& camera,
                               double now,
                               bool& outEnterInteractive )
{
    ApplyReplayLiveAdvanceAction( replayRuntime,
                                  predictionOwner,
                                  scrubber,
                                  !scrubber.View().liveAdvanceHeld,
                                  inputRouter,
                                  interaction,
                                  camera,
                                  outEnterInteractive );
    KeepReplayScrubberVisible( scrubber, now );
}


void HandleReplayVelocityEditPressed( ReplayRuntime& replayRuntime,
                                      ReplayPrediction& predictionOwner,
                                      ReplayScrubber& scrubber,
                                      InputRouter& inputRouter,
                                      RuntimeInteractionController& interaction,
                                      RunCameraState& camera,
                                      double now,
                                      bool& outEnterInteractive )
{
    const bool enableVelocityEdit = !replayRuntime.BuildInputView().velocityEditEnabled;
    if ( replayRuntime.SetVelocityEditEnabled( enableVelocityEdit ) )
    {
        replayRuntime.CancelToolDragState( interaction, inputRouter );
        if ( enableVelocityEdit )
        {
            outEnterInteractive = true;
            ApplyReplayLiveAdvanceAction( replayRuntime,
                                          predictionOwner,
                                          scrubber,
                                          true,
                                          inputRouter,
                                          interaction,
                                          camera,
                                          outEnterInteractive );
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
    KeepReplayScrubberVisible( scrubber, now );
}


void HandleReplayPastPathPressed( ReplayPresentation& presentation, ReplayScrubber& scrubber, double now )
{
    presentation.TogglePastPathVisible();
    KeepReplayScrubberVisible( scrubber, now );
}


void HandleReplayRagdollVisualsPressed( ReplayPrediction& predictionOwner, ReplayScrubber& scrubber, double now )
{
    (void)predictionOwner.ToggleRagdollVisualsEnabled();
    predictionOwner.ClearFutureNodeCache();
    KeepReplayScrubberVisible( scrubber, now );
}


void SetReplayPredictionHorizonFromPointer( ReplayPrediction& predictionOwner,
                                            ReplayScrubber& scrubber,
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
    (void)predictionOwner.SetHorizonSeconds( nextSeconds );
    KeepReplayScrubberVisible( scrubber, now );
}


void HandleReplayPredictionPressed( ReplayRuntime& replayRuntime,
                                    ReplayPrediction& predictionOwner,
                                    ReplayScrubber& scrubber,
                                    RuntimeInteractionController& interaction,
                                    double now,
                                    bool& outEnterInteractive )
{
    outEnterInteractive = true;
    const float previousPredictionPresentT = replayRuntime.BuildInputView().solverPresentTrackPosition;
    const bool predictionEnabled = predictionOwner.ToggleEnabled();
    interaction.SetWorldInteractionOwnerInWorkspace(
        RuntimeWorkspace::Replay,
        predictionEnabled ? WorldInteractionOwner::ReplayPrediction : WorldInteractionOwner::ReplayScrub,
        InteractionExitReason::EnterReplay );
    predictionOwner.ClampHorizonSeconds( REPLAY_PREDICTION_MIN_SECONDS, REPLAY_PREDICTION_MAX_SECONDS );
    if ( !predictionEnabled )
    {
        const float currentPosition = scrubber.TrackPosition( RunReplayTrack::Solver );
        if ( ReplayTrackPositionIsFuture( currentPosition, previousPredictionPresentT ) )
        {
            scrubber.SetTrackPosition( RunReplayTrack::Solver, 1.0f );
            scrubber.SetHistoricalSamplePaused( false );
        }
        predictionOwner.ClearCache();
    }
    predictionOwner.MarkDirty();
    KeepReplayScrubberVisible( scrubber, now );
}


void HandleReplayBranchPressed( ReplayScrubber& scrubber,
                                RuntimeInteractionController& interaction,
                                const ReplayScrubberRestoreSources& sources,
                                double now,
                                ReplayLiveRestoreRequest& outRestoreRequest )
{
    interaction.SetWorldInteractionOwnerInWorkspace( RuntimeWorkspace::Replay,
                                                     WorldInteractionOwner::ReplayBranchTarget,
                                                     InteractionExitReason::EnterReplay );
    (void)scrubber.BuildRestoreRequest( sources, now, outRestoreRequest );
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
                              ReplayScrubber& scrubber,
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
            scrubber.PublishFeedback( RunReplayTrack::Presentation, "REPLAY PICKER FAILED", now, 2.5 );
            KeepReplayScrubberVisible( scrubber, now );
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

    char message[96] = {};
    if ( loaded )
    {
        constexpr int loadedPrefixLength = 7;
        constexpr int loadedFileNameLimit = static_cast<int>( sizeof( message ) ) - loadedPrefixLength - 1;
        sprintf_s( message, sizeof( message ), "LOADED %.*s", loadedFileNameLimit, fileName );
    }
    else
    {
        sprintf_s( message, sizeof( message ), "REPLAY LOAD FAILED" );
    }
    scrubber.PublishFeedback( RunReplayTrack::Presentation, message, now, 2.5 );
    KeepReplayScrubberVisible( scrubber, now );
}


bool HandleReplayPredictionHorizonPressed( ReplayRuntime& replayRuntime,
                                           ReplayPrediction& predictionOwner,
                                           ReplayScrubber& scrubber,
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
    SetReplayPredictionHorizonFromPointer( predictionOwner,
                                           scrubber,
                                           interaction,
                                           horizon,
                                           mouseX,
                                           now,
                                           false,
                                           outEnterInteractive );
    return true;
}


bool HandleReplayScrubPressed( ReplayRuntime& replayRuntime,
                               ReplayScrubber& scrubber,
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
    scrubber.SelectTrack( track );
    return true;
}


bool TickReplayScrubberGesture( ReplayRuntime& replayRuntime,
                                ReplayPrediction& predictionOwner,
                                ReplayScrubber& scrubber,
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
        const RunReplayTrack activeTrack = scrubber.View().activeTrack;
        scrubber.SetTrackPosition( activeTrack,
                                   ReplayScrubberPositionFromMouse( mouseX, screenW, screenH, activeTrack ) );
        if ( loadedPresentation )
        {
            scrubber.SetHistoricalSamplePaused( true );
        }
        else
        {
            const float presentT = replayRuntime.BuildInputView().solverPresentTrackPosition;
            if ( ReplayAtPresentTrackPosition( scrubber.View().position, presentT ) )
            {
                scrubber.SetTrackPosition( activeTrack, presentT );
                scrubber.SetHistoricalSamplePaused( false );
            }
            else
            {
                scrubber.SetHistoricalSamplePaused( true );
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
        SetReplayPredictionHorizonFromPointer( predictionOwner,
                                               scrubber,
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
    const ReplayScrubberInputFrame inputFrame =
        m_replayRuntime.BeginReplayScrubberInputFrame( pointer.leftPressed, pointer.leftReleased, restoreDown );
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
        const ReplayScrubberUnavailableResult unavailable =
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
    m_scrubberOwner.SetPointer( mouse.x, mouse.y );

    const auto scrubDragActive = [&]()
    { return m_interaction.Gesture().kind == RuntimeInteractionGestureKind::ReplayScrubDrag; };
    const auto horizonDragActive = [&]()
    { return m_interaction.Gesture().kind == RuntimeInteractionGestureKind::ReplayPredictionHorizonDrag; };

    const ReplayScrubberSurfaceInput surfaceInput = DescribeReplayScrubberSurface(
        m_scrubberOwner.View(),
        m_timeline.Solver().GetStats(),
        HasLoadedPresentation(),
        m_visualPresentation.PathVisualizer().hasTarget,
        ActivePredictionFrames().size() >= 2 || m_predictionOwner.State().BuildPrefixShouldBePresented(),
        CurrentScrubSample() != nullptr,
        CurrentSolverScrubSample() != nullptr,
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
    ReplayScrubberView scrubber = m_scrubberOwner.View();
    const bool replayStateKeepsScrubberVisible =
        replayDragInProgress || scrubber.historicalSamplePaused || scrubber.liveAdvanceHeld;
    // Why: only the track or its broad background/reveal rows may begin a scrub.
    // A disabled front-most control still blocks fall-through to these rows.
    const bool scrubTrackStartTarget = isHotControl( ReplayScrubberControl::ScrubTrack ) ||
                                       isHotControl( ReplayScrubberControl::Panel ) ||
                                       isHotControl( ReplayScrubberControl::HotZone ) ||
                                       ( scrubber.historicalSamplePaused && !surface.hasPointerControl );
    // Why: the replay reveal zone is mode-agnostic. Passive Scene/Demo cameras
    // do not own mouse tools, but moving to the bottom edge should still expose
    // retained replay controls. UI-owned mouse areas, such as the minimized
    // options window, should not wake the replay bar. Paused/held replay states
    // pin the bar open without making empty screen space consume mouse input.
    if ( pointerRequestsReplayOverlay || replayStateKeepsScrubberVisible )
    {
        m_scrubberOwner.KeepVisible( now, REPLAY_SCRUBBER_VISIBLE_SECONDS );
        scrubber = m_scrubberOwner.View();
    }
    const bool branchControlVisible = scrubber.visibleUntil >= now || scrubDragActive() ||
                                      scrubber.historicalSamplePaused || scrubber.liveAdvanceHeld;

    bool consumesMouse =
        canTakeMouse && ( replayDragInProgress || ( scrubber.visibleUntil >= now && pointerRequestsReplayOverlay ) );

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
                      requestedAction != ReplayScrubberAction::RestoreBranch && scrubber.visibleUntil < now )
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
    {
        ReplayScrubberRestoreSources sources;
        sources.hasLoadedPresentation = HasLoadedPresentation();
        sources.presentationSample = CurrentScrubSample();
        sources.solverSample = CurrentSolverScrubSample();
        sources.loadedPresentationPath = m_timeline.LoadedPresentation().path;
        HandleReplayBranchPressed( m_scrubberOwner, m_interaction, sources, now, outRestoreRequest );
        return true;
    }
    case ReplayScrubberAction::TogglePause:
        HandleReplayPausePressed( *this,
                                  m_predictionOwner,
                                  m_scrubberOwner,
                                  m_inputRouter,
                                  m_interaction,
                                  camera,
                                  now,
                                  outEnterInteractive );
        consumesMouse = true;
        break;
    case ReplayScrubberAction::ToggleVelocityEdit:
        HandleReplayVelocityEditPressed( *this,
                                         m_predictionOwner,
                                         m_scrubberOwner,
                                         m_inputRouter,
                                         m_interaction,
                                         camera,
                                         now,
                                         outEnterInteractive );
        consumesMouse = true;
        break;
    case ReplayScrubberAction::TogglePastPath:
        HandleReplayPastPathPressed( m_visualPresentation, m_scrubberOwner, now );
        consumesMouse = true;
        break;
    case ReplayScrubberAction::ToggleRagdollVisuals:
        HandleReplayRagdollVisualsPressed( m_predictionOwner, m_scrubberOwner, now );
        consumesMouse = true;
        break;
    case ReplayScrubberAction::SetPredictionHorizon:
        if ( !HandleReplayPredictionHorizonPressed( *this,
                                                    m_predictionOwner,
                                                    m_scrubberOwner,
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
        HandleReplayPredictionPressed( *this,
                                       m_predictionOwner,
                                       m_scrubberOwner,
                                       m_interaction,
                                       now,
                                       outEnterInteractive );
        consumesMouse = true;
        break;
    case ReplayScrubberAction::Save:
        HandleReplaySavePressed( *this, now, outEnterInteractive );
        consumesMouse = true;
        break;
    case ReplayScrubberAction::Load:
        HandleReplayLoadPressed( *this,
                                 m_scrubberOwner,
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
                                        m_scrubberOwner,
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
                                                                   m_predictionOwner,
                                                                   m_scrubberOwner,
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
    scrubber = m_scrubberOwner.View();
    if ( !scrubberGestureHandled && !loadedPresentation && !scrubber.historicalSamplePaused )
    {
        m_replayRuntime.SetAllTrackPositions( m_replayRuntime.SolverPresentTrackPosition() );
    }

    const bool scrubberTargetVisible = scrubDragActive() || horizonDragActive() || scrubber.historicalSamplePaused ||
                                       scrubber.liveAdvanceHeld || scrubber.visibleUntil >= now;
    m_scrubberOwner.UpdateVisibilityFade( scrubberTargetVisible,
                                          now,
                                          REPLAY_SCRUBBER_FADE_IN_SECONDS,
                                          REPLAY_SCRUBBER_FADE_OUT_SECONDS,
                                          REPLAY_SCRUBBER_FADE_EPSILON );
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
