/*
File: SkullbonezSource/Runtime/Replay/ReplayScrubberTools.cpp
Purpose:
  Implements replay scrubber input, inspection-camera, and live-restore policy.

Summary:
  The scrubber maps mouse/UI intent to retained solver or presentation samples.
  ReplayScrubber owns cursor transitions; ReplayRuntime receives a frame-scoped
  workspace view and coordinates typed transport, restore, and application
  commands across existing replay owners.

Glossary:
  Scrubber: UI control that selects retained replay frames.
  Live restore: Applying a retained replay sample back into the current scene.
  Branch restore: Applying a historical replay sample as the new live timeline
    while preserving parent/source branch provenance.
  Inspection camera: Temporary replay-focused camera state for selected samples.
  Control surface: Disposable front-to-back scrubber table that resolves one
    pointer target from the same named layout rectangles used for drawing.
  Transport command: Presentation-independent request for record, timeline,
    prediction, restore, or cold artifact work.

Invariants:
  - Restoring a sample must set the scrubber status message and consume restore
    input for the current frame.
  - Replay tool pointer ownership must release through the interaction controller.
  - Hit testing must resolve through the scrubber surface; domain handlers may
    not recreate per-button rectangles or fall-through exclusions.
  - Transport dispatch borrows host owners synchronously and retains none.
  - The inactive Legacy pointer surface cannot reset durable replay state after
    a typed command has arrived from the selected ImGui surface.

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
#include "ReplayRestoreTransactions.h"
#include "../../World/Terrain.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdio>
#include <cstring>

#include <commdlg.h>

using namespace SkullbonezCore::Runtime;
using namespace SkullbonezCore::Runtime::ReplayScrubberOperations;
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

void SkullbonezCore::Runtime::ReplayInteractionOperations::CancelToolGesture(
    RuntimeInteractionController& interaction )
{
    switch ( interaction.Gesture().kind )
    {
    case RuntimeInteractionGestureKind::ReplayScrubDrag:
    case RuntimeInteractionGestureKind::ReplayVelocityDrag:
    case RuntimeInteractionGestureKind::ReplayPredictionHorizonDrag:
    case RuntimeInteractionGestureKind::ReplayCauseTreeDrag:
        interaction.EndGestureIfKind( interaction.Gesture().kind );
        break;
    default:
        break;
    }
}

void SkullbonezCore::Runtime::ReplayInteractionOperations::CancelToolDragState(
    RuntimeInteractionController& interaction,
    InputRouter& inputRouter )
{
    const RuntimeInteractionGestureKind gesture = interaction.Gesture().kind;
    const bool ownsReplayCapture = interaction.PointerCapture() == RuntimePointerCaptureOwner::ToolGesture &&
                                   ( gesture == RuntimeInteractionGestureKind::ReplayScrubDrag ||
                                     gesture == RuntimeInteractionGestureKind::ReplayVelocityDrag ||
                                     gesture == RuntimeInteractionGestureKind::ReplayPredictionHorizonDrag ||
                                     gesture == RuntimeInteractionGestureKind::ReplayCauseTreeDrag );
    CancelToolGesture( interaction );
    if ( ownsReplayCapture )
    {
        inputRouter.ReleaseNativeCapture();
    }
}


void SkullbonezCore::Runtime::ReplayPresentationOperations::EnterInspectionCamera(
    ReplayPresentation& presentation,
    Environment::CameraCollection* cameras,
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

    const bool enteringInspectionCamera = !presentation.CameraView().active;
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

        presentation.BeginCameraInspection( normalizedCurrentMode, restoreCameraHash, eye, view, up );
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


void SkullbonezCore::Runtime::ReplayPresentationOperations::ExitInspectionCamera(
    ReplayPresentation& presentation,
    const ReplayAuthoring& authoring,
    Environment::CameraCollection* cameras,
    Geometry::Terrain* terrain,
    RunCameraState& camera,
    RunCameraMode normalizedRestoreMode,
    bool attachedFollow,
    bool directorGrabbed,
    RuntimeInteractionController& interaction,
    InputRouter& inputRouter )
{
    const RunReplayCameraState replayCamera = presentation.CameraView();
    if ( !replayCamera.active )
    {
        return;
    }

    camera.mode = normalizedRestoreMode;
    if ( authoring.VelocityEdit().enabled )
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
    presentation.EndCameraInspection();
    inputRouter.RequestCursorVisible( true );
    InputController::ResetMouseLook( camera );
}

bool SkullbonezCore::Runtime::ReplayPresentationOperations::ActivateLoadedPresentation(
    const ReplayLoadedPresentationActivationRequest& request,
    ReplayScrubber& scrubber,
    ReplayPresentation& presentation,
    ReplayAuthoring& authoring,
    ReplayPrediction& prediction,
    Environment::CameraCollection* cameras,
    Geometry::Terrain* terrain,
    RunCameraState& camera,
    RunMousePickupState& mousePickup,
    RuntimeInteractionController& interaction,
    InputRouter& inputRouter )
{
    if ( !request.hasLoadedPresentation )
    {
        return false;
    }

    // Invariant: timeline decode has committed before this cross-owner reaction.
    // No owner or host reference escapes the operation.
    scrubber.SetLiveAdvanceHeld( false );
    presentation.SetCameraPauseOwnership( false );

    const RuntimeInteractionGestureKind gesture = interaction.Gesture().kind;
    const bool replayGesture = gesture == RuntimeInteractionGestureKind::ReplayScrubDrag ||
                               gesture == RuntimeInteractionGestureKind::ReplayVelocityDrag ||
                               gesture == RuntimeInteractionGestureKind::ReplayPredictionHorizonDrag ||
                               gesture == RuntimeInteractionGestureKind::ReplayCauseTreeDrag;
    const bool ownsReplayCapture =
        replayGesture && interaction.PointerCapture() == RuntimePointerCaptureOwner::ToolGesture;
    if ( replayGesture )
    {
        interaction.EndGestureIfKind( gesture );
    }
    if ( ownsReplayCapture )
    {
        inputRouter.ReleaseNativeCapture();
    }

    (void)presentation.ClearCameraFocus();
    authoring.ClearCauseTreeFocus();
    ExitInspectionCamera( presentation,
                          authoring,
                          cameras,
                          terrain,
                          camera,
                          request.normalizedRestoreMode,
                          request.attachedFollow,
                          request.directorGrabbed,
                          interaction,
                          inputRouter );

    presentation.ClearPathState();
    authoring.ResetCauseTreeRows();
    prediction.ClearCache();
    prediction.MarkDirty();
    prediction.DisableForLiveAdvance();
    authoring.ResetVelocityEdit();
    scrubber.ArmLoadedPresentation( request.normalized, request.now, ReplayOverlay::REPLAY_SCRUBBER_VISIBLE_SECONDS );
    interaction.SetWorldInteractionOwnerInWorkspace( RuntimeWorkspace::Replay,
                                                     WorldInteractionOwner::ReplayScrub,
                                                     InteractionExitReason::EnterReplay );
    EnterInspectionCamera( presentation,
                           cameras,
                           camera,
                           request.normalizedCurrentMode,
                           interaction,
                           inputRouter,
                           mousePickup );
    return true;
}

void ReplayRuntime::EnterInspectionCamera( Environment::CameraCollection* cameras,
                                           RunCameraState& camera,
                                           RunCameraMode normalizedCurrentMode,
                                           RuntimeInteractionController& interaction,
                                           InputRouter& inputRouter,
                                           RunMousePickupState& mousePickup )
{
    ReplayPresentationOperations::EnterInspectionCamera( m_visualPresentation,
                                                         cameras,
                                                         camera,
                                                         normalizedCurrentMode,
                                                         interaction,
                                                         inputRouter,
                                                         mousePickup );
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
    ReplayPresentationOperations::ExitInspectionCamera( m_visualPresentation,
                                                        m_authoring,
                                                        cameras,
                                                        terrain,
                                                        camera,
                                                        normalizedRestoreMode,
                                                        attachedFollow,
                                                        directorGrabbed,
                                                        interaction,
                                                        inputRouter );
}

bool ReplayRuntime::SavePresentationFromScrubber( double now )
{
    // Invariant: the timeline advances the process-local sequence and the
    // scrubber publishes success only after the binary v2 writer completes.
    char path[256] = {};
    bool saved = false;
    if ( m_timeline.NextPresentationSavePath( path, sizeof( path ) ) )
    {
        saved = SavePresentationWithSolverHashes( path );
    }

    char message[96] = {};
    if ( saved )
    {
        const char* fileName = std::strrchr( path, '\\' );
        if ( !fileName )
        {
            fileName = std::strrchr( path, '/' );
        }
        fileName = fileName ? fileName + 1 : path;
        sprintf_s( message, sizeof( message ), "SAVED %s", fileName );
    }
    else
    {
        sprintf_s( message, sizeof( message ), "REPLAY SAVE FAILED" );
    }
    m_scrubberOwner.PublishFeedback( RunReplayTrack::Presentation, message, now, 2.5 );
    m_scrubberOwner.KeepVisible( now, ReplayOverlay::REPLAY_SCRUBBER_VISIBLE_SECONDS );
    return saved;
}

void ReplayRuntime::ActivateLoadedPresentationScrubber( const ReplayLoadedPresentationActivationRequest& request,
                                                        InputRouter& inputRouter,
                                                        RuntimeInteractionController& interaction,
                                                        Environment::CameraCollection* cameras,
                                                        Geometry::Terrain* terrain,
                                                        RunCameraState& camera,
                                                        RunMousePickupState& mousePickup )
{
    (void)ReplayPresentationOperations::ActivateLoadedPresentation( request,
                                                                    m_scrubberOwner,
                                                                    m_visualPresentation,
                                                                    m_authoring,
                                                                    m_predictionOwner,
                                                                    cameras,
                                                                    terrain,
                                                                    camera,
                                                                    mousePickup,
                                                                    interaction,
                                                                    inputRouter );
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

    if ( !input.legacyPointerSurfaceActive )
    {
        // Why: semantic commands from ImGui have already reached ReplayRuntime.
        // The inactive Legacy pointer surface must neither compete for capture
        // nor interpret its hidden window as a reason to reset durable replay state.
        ReplayInteractionOperations::CancelToolDragState( interaction, inputRouter );
        return;
    }

    bool enterInteractive = false;
    const bool scrubberOwnsMouse = TickScrubberInput( input,
                                                      inputRouter,
                                                      interaction,
                                                      cameras,
                                                      terrain,
                                                      camera,
                                                      mousePickup,
                                                      enterInteractive,
                                                      output.restoreRequest );

    const Physics::PhysicsBodyStore& bodyStore = Physics::PhysicsEngine::ReadBodies( physics );
    const Physics::ColliderStore& colliderStore = Physics::PhysicsEngine::ReadColliders( physics );
    const ReplayCauseTreeInputSources causeTreeSources{ .prediction = m_predictionOwner.State(),
                                                        .activePredictionFrames = m_predictionOwner.ActiveFrames(),
                                                        .currentSolverSample = CurrentSolverScrubSample(),
                                                        .bodyStore = bodyStore,
                                                        .colliderStore = colliderStore,
                                                        .presentation = presentation };
    // Why: tools run in priority order. Each value-only copy refines the one
    // frame fact that a higher-priority surface consumed the pointer, while all
    // mutable owners remain explicit call operands.
    ReplayWorkspaceFrameInput causeTreeInput = input;
    causeTreeInput.uiBlocksMouse = input.uiBlocksMouse || scrubberOwnsMouse;
    const bool causeTreeOwnsMouse = m_authoring.TickCauseTreeInput( m_visualPresentation,
                                                                    m_scrubberOwner,
                                                                    causeTreeSources,
                                                                    causeTreeInput,
                                                                    inputRouter,
                                                                    interaction,
                                                                    cameras,
                                                                    terrain,
                                                                    camera,
                                                                    mousePickup,
                                                                    enterInteractive );
    ApplyAuthoringPredictionRequest();

    const ReplayVelocityEditInputSources velocitySources{ .currentSolverSample = CurrentSolverScrubSample(),
                                                          .entities = entities,
                                                          .presentation = presentation };
    ReplayWorkspaceFrameInput velocityInput = input;
    velocityInput.uiBlocksMouse = input.uiBlocksMouse || scrubberOwnsMouse || causeTreeOwnsMouse;
    const bool velocityEditOwnsMouse = m_authoring.TickVelocityEditInput( m_visualPresentation,
                                                                          m_scrubberOwner,
                                                                          velocitySources,
                                                                          velocityInput,
                                                                          inputRouter,
                                                                          interaction,
                                                                          physics,
                                                                          cameras,
                                                                          terrain,
                                                                          camera,
                                                                          mousePickup,
                                                                          enterInteractive );
    ApplyAuthoringPredictionRequest();

    output.consumesMouse = scrubberOwnsMouse || causeTreeOwnsMouse || velocityEditOwnsMouse;
    output.enterInteractive = enterInteractive || output.restoreRequest.enterInteractive;
}


void ReplayRuntime::ResetSceneTimeline( const ReplaySceneTimelineResetInput& input,
                                        const ReplaySceneTimelineResetOwners& owners )
{
    ReplayInteractionOperations::CancelToolDragState( owners.interaction, owners.inputRouter );
    const ReplaySceneTimelineResetResult begin = BeginSceneTimelineReset( input );
    if ( begin.exitInspectionCamera )
    {
        ReplayPresentationOperations::ExitInspectionCamera( m_visualPresentation,
                                                            m_authoring,
                                                            owners.cameras,
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
        ReplayPresentationOperations::ExitInspectionCamera( m_visualPresentation,
                                                            m_authoring,
                                                            owners.cameras,
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
void EndReplayScrubberGesture( InputRouter& inputRouter,
                               RuntimeInteractionController& interaction,
                               RuntimeInteractionGestureKind kind );

void KeepReplayScrubberVisible( ReplayScrubber& scrubber, double now )
{
    scrubber.KeepVisible( now, REPLAY_SCRUBBER_VISIBLE_SECONDS );
}


bool IsReplayToolGesture( RuntimeInteractionGestureKind kind )
{
    return kind == RuntimeInteractionGestureKind::ReplayScrubDrag ||
           kind == RuntimeInteractionGestureKind::ReplayVelocityDrag ||
           kind == RuntimeInteractionGestureKind::ReplayPredictionHorizonDrag ||
           kind == RuntimeInteractionGestureKind::ReplayCauseTreeDrag;
}


void CancelReplayToolDragState( RuntimeInteractionController& interaction, InputRouter& inputRouter )
{
    // Invariant: a stale replay cleanup must not end editor, manipulator, or
    // camera capture that became active later in the same input turn.
    const RuntimeInteractionGestureKind gesture = interaction.Gesture().kind;
    if ( !IsReplayToolGesture( gesture ) )
    {
        return;
    }
    const bool ownsReplayCapture = interaction.PointerCapture() == RuntimePointerCaptureOwner::ToolGesture;
    interaction.EndGestureIfKind( gesture );
    if ( ownsReplayCapture )
    {
        inputRouter.ReleaseNativeCapture();
    }
}


void ApplyReplayLiveAdvanceAction( ReplayPrediction& predictionOwner,
                                   ReplayPresentation& presentation,
                                   ReplayScrubber& scrubber,
                                   bool held,
                                   float previousPredictionPresentT,
                                   bool velocityEditEnabled,
                                   bool hasCameraFocus,
                                   InputRouter& inputRouter,
                                   RuntimeInteractionController& interaction,
                                   RunCameraState& camera,
                                   bool& outEnterInteractive )
{
    // Concept: live advance is scrubber state; prediction and presentation
    // receive explicit reactions here without reopening ReplayRuntime state.
    const bool liveAdvanceChanged = scrubber.SetLiveAdvanceHeld( held );
    if ( !held )
    {
        presentation.SetCameraPauseOwnership( false );
    }
    if ( !liveAdvanceChanged )
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
            EndReplayScrubberGesture( inputRouter,
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
    else if ( velocityEditEnabled )
    {
        interaction.SetWorldInteractionOwnerInWorkspace( RuntimeWorkspace::Replay,
                                                         WorldInteractionOwner::ReplayVelocityEdit,
                                                         InteractionExitReason::EnterReplay );
    }
    else if ( !scrubber.View().historicalSamplePaused && !hasCameraFocus )
    {
        interaction.EnterCameraMode( camera.mode );
    }
}


void HandleReplayPausePressed( ReplayPrediction& predictionOwner,
                               ReplayPresentation& presentation,
                               ReplayScrubber& scrubber,
                               float solverPresentTrackPosition,
                               bool velocityEditEnabled,
                               bool hasCameraFocus,
                               InputRouter& inputRouter,
                               RuntimeInteractionController& interaction,
                               RunCameraState& camera,
                               double now,
                               bool& outEnterInteractive )
{
    ApplyReplayLiveAdvanceAction( predictionOwner,
                                  presentation,
                                  scrubber,
                                  !scrubber.View().liveAdvanceHeld,
                                  solverPresentTrackPosition,
                                  velocityEditEnabled,
                                  hasCameraFocus,
                                  inputRouter,
                                  interaction,
                                  camera,
                                  outEnterInteractive );
    KeepReplayScrubberVisible( scrubber, now );
}


void HandleReplayVelocityEditPressed( ReplayAuthoring& authoring,
                                      ReplayPrediction& predictionOwner,
                                      ReplayPresentation& presentation,
                                      ReplayScrubber& scrubber,
                                      float solverPresentTrackPosition,
                                      bool hasCameraFocus,
                                      InputRouter& inputRouter,
                                      RuntimeInteractionController& interaction,
                                      RunCameraState& camera,
                                      double now,
                                      bool& outEnterInteractive )
{
    PROFILE_SCOPED( authoring.ProfilerBorrow(), "Frame/Replay/VelocityEdit/Toggle" );
    const bool enableVelocityEdit = !authoring.VelocityEdit().enabled;
    if ( authoring.SetVelocityEditEnabled( enableVelocityEdit ) )
    {
        // Why: authoring emits a value command so prediction is refreshed in
        // this composition turn without storing an owner pointer or callback.
        const ReplayAuthoringPredictionRequest request = authoring.TakePredictionRequest();
        predictionOwner.ApplyAuthoringRequest( request.enablePrediction,
                                               request.refreshPrediction,
                                               REPLAY_PREDICTION_MIN_SECONDS,
                                               REPLAY_PREDICTION_MAX_SECONDS );
        CancelReplayToolDragState( interaction, inputRouter );
        if ( enableVelocityEdit )
        {
            outEnterInteractive = true;
            ApplyReplayLiveAdvanceAction( predictionOwner,
                                          presentation,
                                          scrubber,
                                          true,
                                          solverPresentTrackPosition,
                                          true,
                                          hasCameraFocus,
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


void HandleReplayPredictionPressed( ReplayPrediction& predictionOwner,
                                    ReplayScrubber& scrubber,
                                    float previousPredictionPresentT,
                                    RuntimeInteractionController& interaction,
                                    double now,
                                    bool& outEnterInteractive )
{
    outEnterInteractive = true;
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


bool BeginReplayScrubberGesture( InputRouter& inputRouter,
                                 RuntimeInteractionController& interaction,
                                 RuntimeInteractionGestureKind kind,
                                 WorldInteractionOwner owner,
                                 int mouseX,
                                 int mouseY )
{
    RuntimeInteractionGesture gesture;
    gesture.kind = kind;
    gesture.button = RuntimePointerButton::Left;
    gesture.startX = mouseX;
    gesture.startY = mouseY;
    if ( !interaction.BeginOwnedToolGesture( RuntimeWorkspace::Replay, owner, gesture ) )
    {
        return false;
    }
    inputRouter.RequestNativeCapture();
    return true;
}


void EndReplayScrubberGesture( InputRouter& inputRouter,
                               RuntimeInteractionController& interaction,
                               RuntimeInteractionGestureKind kind )
{
    interaction.EndGestureIfKind( kind );
    inputRouter.ReleaseNativeCapture();
}


bool SelectReplayPresentationArtifact( ReplayScrubber& scrubber, HWND window, double now, char ( &outPath )[MAX_PATH] )
{
    // Why: the native picker is cold UI. It runs only after typed Load dispatch
    // and never becomes a stored callback or per-frame service dependency.
    OPENFILENAMEA openFile = {};
    openFile.lStructSize = sizeof( openFile );
    openFile.hwndOwner = window;
    openFile.lpstrFilter = "Skullbonez replay (*.skreplay)\0*.skreplay\0All files (*.*)\0*.*\0";
    openFile.lpstrFile = outPath;
    openFile.nMaxFile = MAX_PATH;
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
        return false;
    }
    return true;
}

void PublishReplayLoadResult( ReplayScrubber& scrubber, const char* path, bool loaded, double now )
{
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


bool HandleReplayPredictionHorizonPressed( ReplayPrediction& predictionOwner,
                                           ReplayScrubber& scrubber,
                                           InputRouter& inputRouter,
                                           RuntimeInteractionController& interaction,
                                           const SkullbonezCore::UI::UIRect& horizon,
                                           int mouseX,
                                           int mouseY,
                                           double now,
                                           bool& outEnterInteractive )
{
    if ( !BeginReplayScrubberGesture( inputRouter,
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


bool HandleReplayScrubPressed( ReplayScrubber& scrubber,
                               InputRouter& inputRouter,
                               RuntimeInteractionController& interaction,
                               RunReplayTrack track,
                               int mouseX,
                               int mouseY,
                               bool& outEnterInteractive )
{
    outEnterInteractive = true;
    if ( !BeginReplayScrubberGesture( inputRouter,
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


bool TickReplayScrubberGesture( ReplayPrediction& predictionOwner,
                                ReplayScrubber& scrubber,
                                float solverPresentTrackPosition,
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
            if ( ReplayAtPresentTrackPosition( scrubber.View().position, solverPresentTrackPosition ) )
            {
                scrubber.SetTrackPosition( activeTrack, solverPresentTrackPosition );
                scrubber.SetHistoricalSamplePaused( false );
            }
            else
            {
                scrubber.SetHistoricalSamplePaused( true );
            }
        }
        if ( leftReleased )
        {
            EndReplayScrubberGesture( inputRouter, interaction, RuntimeInteractionGestureKind::ReplayScrubDrag );
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
            EndReplayScrubberGesture( inputRouter,
                                      interaction,
                                      RuntimeInteractionGestureKind::ReplayPredictionHorizonDrag );
        }
        return true;
    default:
        return false;
    }
}
} // namespace

ReplayScrubberPointerDecision ReplayScrubber::ResolvePointerAction( const ReplayScrubberPointerFrame& frame )
{
    ReplayScrubberPointerDecision decision;
    const ReplayScrubberInputFrame inputFrame =
        BeginInputFrame( frame.leftPressed, frame.leftReleased, frame.restoreDown );
    decision.leftReleased = inputFrame.leftReleased;

    const bool scrubberAllowed = !frame.editorModeEnabled && frame.uiVisible && frame.uiMinimized;
    const bool replaySurfaceAvailable = frame.loadedPresentation || frame.solverStats.enabled;
    if ( !scrubberAllowed || !replaySurfaceAvailable || frame.screenWidth <= 0 || frame.screenHeight <= 0 )
    {
        decision.cancelToolDrag = true;
        const ReplayScrubberUnavailableResult unavailable =
            ResetUnavailableSurface( frame.loadedPresentation, frame.inspectionCameraActive );
        decision.exitInspectionCamera = unavailable.exitInspectionCamera;
        return decision;
    }
    if ( !frame.hasClientPosition )
    {
        return decision;
    }

    decision.surfaceAvailable = true;
    SetPointer( frame.mouseX, frame.mouseY );
    const bool scrubDragActive = frame.gesture == RuntimeInteractionGestureKind::ReplayScrubDrag;
    const bool horizonDragActive = frame.gesture == RuntimeInteractionGestureKind::ReplayPredictionHorizonDrag;

    const ReplayScrubberSurfaceInput surfaceInput = DescribeReplayScrubberSurface(
        ReplayScrubberSurfaceDesc{ .scrubber = View(),
                                   .solverStats = frame.solverStats,
                                   .loadedPresentation = frame.loadedPresentation,
                                   .pathTargetAvailable = frame.pathTargetAvailable,
                                   .predictionTimelineAvailable = frame.predictionTimelineAvailable,
                                   .currentPresentationAvailable = frame.currentPresentationAvailable,
                                   .currentSolverAvailable = frame.currentSolverAvailable,
                                   .scenePhysicsEnabled = frame.scenePhysicsEnabled,
                                   .uiBlocksMouse = frame.uiBlocksMouse,
                                   .screenW = frame.screenWidth,
                                   .screenH = frame.screenHeight,
                                   .gesture = frame.gesture } );
    decision.track = surfaceInput.track;
    ReplayScrubberSurface surface;
    BuildReplayScrubberSurface( surfaceInput, surface );
    surface.ResolvePointer( frame.mouseX, frame.mouseY );

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
    decision.horizonX = horizonControl->drawRect.x;
    decision.horizonY = horizonControl->drawRect.y;
    decision.horizonWidth = horizonControl->drawRect.w;
    decision.horizonHeight = horizonControl->drawRect.h;

    const bool canTakeMouse = frame.uiBlocksMouse == false || scrubDragActive || horizonDragActive;
    const bool pointerRequestsReplayOverlay = pointerControl && pointerControl->requestsReveal;
    const bool replayDragInProgress = scrubDragActive || horizonDragActive;
    ReplayScrubberView scrubber = View();
    const bool replayStateKeepsScrubberVisible =
        replayDragInProgress || scrubber.historicalSamplePaused || scrubber.liveAdvanceHeld;
    // Why: only the track or its broad background/reveal rows may begin a scrub.
    // A disabled front-most control still blocks fall-through to these rows.
    const bool scrubTrackStartTarget = isHotControl( ReplayScrubberControl::ScrubTrack ) ||
                                       isHotControl( ReplayScrubberControl::Panel ) ||
                                       isHotControl( ReplayScrubberControl::HotZone ) ||
                                       ( scrubber.historicalSamplePaused && !surface.hasPointerControl );
    // Why: passive Scene/Demo cameras still reveal the replay bar at its hot
    // zone, while UI-owned mouse regions do not. Active replay state pins the
    // surface open without making empty screen space consume pointer input.
    if ( pointerRequestsReplayOverlay || replayStateKeepsScrubberVisible )
    {
        KeepVisible( frame.now, REPLAY_SCRUBBER_VISIBLE_SECONDS );
        scrubber = View();
    }
    const bool branchControlVisible = scrubber.visibleUntil >= frame.now || scrubDragActive ||
                                      scrubber.historicalSamplePaused || scrubber.liveAdvanceHeld;
    decision.consumesMouse = canTakeMouse && ( replayDragInProgress ||
                                               ( scrubber.visibleUntil >= frame.now && pointerRequestsReplayOverlay ) );

    if ( surfaceInput.branchTargetAvailable && inputFrame.restorePressed )
    {
        decision.action = ReplayScrubberAction::RestoreBranch;
    }
    else if ( inputFrame.leftPressed && canTakeMouse )
    {
        const RuntimeUiControl* hotControl = surface.hasHotControl ? surface.Find( surface.hotControl ) : nullptr;
        if ( hotControl )
        {
            decision.action = static_cast<ReplayScrubberAction>( hotControl->action.value );
            if ( decision.action == ReplayScrubberAction::RestoreBranch && !branchControlVisible )
            {
                decision.action = ReplayScrubberAction::None;
            }
            else if ( decision.action != ReplayScrubberAction::None && decision.action != ReplayScrubberAction::Scrub &&
                      decision.action != ReplayScrubberAction::RestoreBranch && scrubber.visibleUntil < frame.now )
            {
                decision.action = ReplayScrubberAction::None;
            }
            else if ( decision.action == ReplayScrubberAction::None && scrubTrackStartTarget )
            {
                decision.action = ReplayScrubberAction::Scrub;
            }
        }
        else if ( scrubTrackStartTarget )
        {
            decision.action = ReplayScrubberAction::Scrub;
        }
    }
    return decision;
}

void ReplayRuntime::ApplyTransportCommand( const ReplayTransportCommand& command,
                                           const ReplayTransportHostContext& host,
                                           InputRouter& inputRouter,
                                           RuntimeInteractionController& interaction,
                                           Environment::CameraCollection* cameras,
                                           Geometry::Terrain* terrain,
                                           RunCameraState& camera,
                                           RunMousePickupState& mousePickup,
                                           ReplayWorkspaceOutput& output )
{
    const float solverPresent = SolverPresentTrackPosition();
    const bool hasCameraFocus = m_visualPresentation.CameraView().focusKind != RunReplayCameraFocusKind::None;
    const auto feedback = [&]( const char* message )
    {
        m_scrubberOwner.PublishFeedback( m_scrubberOwner.View().activeTrack, message, host.now, 3.0 );
        KeepReplayScrubberVisible( m_scrubberOwner, host.now );
    };
    const auto enterReplayWorkspace = [&]()
    {
        output.enterInteractive = true;
        if ( !IsReplayScrubberToolOwner( interaction.Owner() ) )
        {
            interaction.SetWorldInteractionOwnerInWorkspace( RuntimeWorkspace::Replay,
                                                             WorldInteractionOwner::ReplayScrub,
                                                             InteractionExitReason::EnterReplay );
        }
    };
    const auto setCursor = [&]( float normalized )
    {
        const bool loaded = HasLoadedPresentation();
        const RunReplayTrack track = loaded ? RunReplayTrack::Presentation : RunReplayTrack::Solver;
        const std::size_t retainedCount =
            loaded ? m_timeline.LoadedPresentation().samples.size() : m_timeline.Solver().GetStats().sampleCount;
        const bool predictionAvailable = !loaded && ( m_predictionOwner.ActiveFrames().size() >= 2u ||
                                                      m_predictionOwner.State().BuildPrefixShouldBePresented() );
        if ( retainedCount < 2u && !predictionAvailable )
        {
            feedback( "REPLAY UNAVAILABLE: NO RETAINED SAMPLES" );
            return false;
        }

        const float position = std::clamp( normalized, 0.0f, 1.0f );
        m_scrubberOwner.SelectTrack( track );
        m_scrubberOwner.SetTrackPosition( track, position );
        const float livePosition = loaded ? 1.0f : solverPresent;
        m_scrubberOwner.SetHistoricalSamplePaused( loaded || !ReplayAtPresentTrackPosition( position, livePosition ) );
        enterReplayWorkspace();
        KeepReplayScrubberVisible( m_scrubberOwner, host.now );
        return true;
    };
    const auto returnToLive = [&]()
    {
        if ( HasLoadedPresentation() )
        {
            m_timeline.ClearLoadedPresentation();
        }
        m_scrubberOwner.SelectTrack( RunReplayTrack::Solver );
        m_scrubberOwner.SetAllTrackPositions( 1.0f );
        m_scrubberOwner.SetHistoricalSamplePaused( false );
        bool enterInteractive = false;
        ApplyReplayLiveAdvanceAction( m_predictionOwner,
                                      m_visualPresentation,
                                      m_scrubberOwner,
                                      false,
                                      solverPresent,
                                      m_authoring.VelocityEdit().enabled,
                                      false,
                                      inputRouter,
                                      interaction,
                                      camera,
                                      enterInteractive );
        m_authoring.ClearCauseTreeFocus();
        ExitInspectionCamera( cameras,
                              terrain,
                              camera,
                              host.normalizedRestoreMode,
                              host.attachedFollow,
                              host.directorGrabbed,
                              interaction,
                              inputRouter );
        output.enterInteractive = output.enterInteractive || enterInteractive;
        feedback( "LIVE" );
    };

    switch ( command.action )
    {
    case ReplayTransportAction::SetRecordingEnabled:
        if ( m_timeline.SetRecordingEnabled( command.enabled ) )
        {
            feedback( command.enabled ? "RECORDING" : "RECORDING STOPPED" );
        }
        else if ( m_timeline.RecordingLockedByHashLog() )
        {
            feedback( "RECORDING LOCKED BY HASH LOG" );
        }
        else
        {
            feedback( "RECORDING UNAVAILABLE: ENABLE REPLAY AT LAUNCH" );
        }
        break;
    case ReplayTransportAction::JumpToStart:
        (void)setCursor( 0.0f );
        break;
    case ReplayTransportAction::JumpToEnd:
        // End means the final visible timeline sample, including prediction.
        // ReturnToLive is the distinct command for the solver-present marker.
        (void)setCursor( 1.0f );
        break;
    case ReplayTransportAction::StepBackward:
    case ReplayTransportAction::StepForward:
    {
        const bool loaded = HasLoadedPresentation();
        const std::size_t retainedCount =
            loaded ? m_timeline.LoadedPresentation().samples.size() : m_timeline.Solver().GetStats().sampleCount;
        const std::size_t predictionCount = loaded ? 0u : m_predictionOwner.ActiveFrames().size();
        const std::size_t totalCount = retainedCount + predictionCount;
        if ( totalCount < 2u )
        {
            feedback( "STEP UNAVAILABLE: NO RETAINED NEIGHBOR" );
            break;
        }
        const float direction = command.action == ReplayTransportAction::StepBackward ? -1.0f : 1.0f;
        const float step = direction / static_cast<float>( totalCount - 1u );
        (void)setCursor( m_scrubberOwner.View().position + step );
        break;
    }
    case ReplayTransportAction::TogglePlayPause:
        // Reuse the established play-hold owner for live, predicted, and
        // loaded tracks. Returning to live is a separate explicit command and
        // must not discard a loaded artifact when the operator presses Play.
        HandleReplayPausePressed( m_predictionOwner,
                                  m_visualPresentation,
                                  m_scrubberOwner,
                                  solverPresent,
                                  m_authoring.VelocityEdit().enabled,
                                  hasCameraFocus,
                                  inputRouter,
                                  interaction,
                                  camera,
                                  host.now,
                                  output.enterInteractive );
        break;
    case ReplayTransportAction::SetRevealSpeed:
        m_predictionOwner.SetRevealRatePreservingCursor( command.value );
        feedback( "PREDICTION REVEAL SPEED UPDATED" );
        break;
    case ReplayTransportAction::Scrub:
        (void)setCursor( command.value );
        break;
    case ReplayTransportAction::TogglePrediction:
        HandleReplayPredictionPressed( m_predictionOwner,
                                       m_scrubberOwner,
                                       solverPresent,
                                       interaction,
                                       host.now,
                                       output.enterInteractive );
        break;
    case ReplayTransportAction::SetPredictionHorizon:
        m_predictionOwner.SetHorizonSeconds(
            std::clamp( command.value, REPLAY_PREDICTION_MIN_SECONDS, REPLAY_PREDICTION_MAX_SECONDS ) );
        KeepReplayScrubberVisible( m_scrubberOwner, host.now );
        break;
    case ReplayTransportAction::RestoreBranch:
    {
        ReplayScrubberRestoreSources sources;
        sources.hasLoadedPresentation = HasLoadedPresentation();
        sources.presentationSample = CurrentScrubSample();
        sources.solverSample = CurrentSolverScrubSample();
        sources.loadedPresentationPath = m_timeline.LoadedPresentation().path;
        HandleReplayBranchPressed( m_scrubberOwner, interaction, sources, host.now, output.restoreRequest );
        break;
    }
    case ReplayTransportAction::Save:
        output.enterInteractive = true;
        SavePresentationFromScrubber( host.now );
        break;
    case ReplayTransportAction::Load:
    {
        char path[MAX_PATH] = {};
        if ( SelectReplayPresentationArtifact( m_scrubberOwner, host.window, host.now, path ) )
        {
            const bool loaded = m_timeline.LoadPresentationArtifact( path );
            if ( loaded )
            {
                const ReplayLoadedPresentationActivationRequest activation{
                    .hasLoadedPresentation = HasLoadedPresentation(),
                    .normalized = 0.25f,
                    .now = host.now,
                    .normalizedCurrentMode = host.normalizedCurrentMode,
                    .normalizedRestoreMode = host.normalizedRestoreMode,
                    .attachedFollow = host.attachedFollow,
                    .directorGrabbed = host.directorGrabbed };
                ActivateLoadedPresentationScrubber( activation,
                                                    inputRouter,
                                                    interaction,
                                                    cameras,
                                                    terrain,
                                                    camera,
                                                    mousePickup );
            }
            PublishReplayLoadResult( m_scrubberOwner, path, loaded, host.now );
        }
        break;
    }
    case ReplayTransportAction::ReturnToLive:
        returnToLive();
        break;
    case ReplayTransportAction::SelectCauseRow:
        if ( command.rowIndex >= 0 && command.rowIndex < static_cast<int>( m_authoring.CauseTree().rows.size() ) )
        {
            m_authoring.SetCauseTreeSelectedRow( command.rowIndex );
            enterReplayWorkspace();
        }
        else
        {
            feedback( "CAUSE SELECTION STALE" );
        }
        break;
    }
}

bool ReplayRuntime::TickScrubberInput( const ReplayWorkspaceFrameInput& input,
                                       InputRouter& inputRouter,
                                       RuntimeInteractionController& interaction,
                                       Environment::CameraCollection* cameras,
                                       Geometry::Terrain* terrain,
                                       RunCameraState& camera,
                                       RunMousePickupState& mousePickup,
                                       bool& outEnterInteractive,
                                       ReplayLiveRestoreRequest& outRestoreRequest )
{
    const HWND hwnd = input.window;
    const bool uiBlocksMouse = input.uiBlocksMouse;
    const RunCameraMode normalizedCurrentMode = input.normalizedCurrentMode;
    const RunCameraMode normalizedRestoreMode = input.normalizedRestoreMode;
    const bool attachedFollow = input.attachedFollow;
    const bool directorGrabbed = input.directorGrabbed;
    const bool editorModeEnabled = input.editorModeEnabled;
    const bool scenePhysicsEnabled = input.scenePhysicsEnabled;
    const bool uiVisible = input.uiVisible;
    const bool uiMinimized = input.uiMinimized;
    const int screenWidth = input.screenWidth;
    const int screenHeight = input.screenHeight;
    const double now = input.now;
    InputRouter& m_inputRouter = inputRouter;
    RuntimeInteractionController& m_interaction = interaction;
    outRestoreRequest = ReplayLiveRestoreRequest{};
    const auto enterInspectionCamera = [&]()
    {
        ReplayPresentationOperations::EnterInspectionCamera( m_visualPresentation,
                                                             cameras,
                                                             camera,
                                                             normalizedCurrentMode,
                                                             m_interaction,
                                                             m_inputRouter,
                                                             mousePickup );
    };
    const auto exitInspectionCamera = [&]()
    {
        ReplayPresentationOperations::ExitInspectionCamera( m_visualPresentation,
                                                            m_authoring,
                                                            cameras,
                                                            terrain,
                                                            camera,
                                                            normalizedRestoreMode,
                                                            attachedFollow,
                                                            directorGrabbed,
                                                            m_interaction,
                                                            m_inputRouter );
    };
    PROFILE_SCOPED( m_profiler, "Frame/Replay/ScrubberInput" );
    const bool loadedPresentation = HasLoadedPresentation();
    const float solverPresentTrackPosition = SolverPresentTrackPosition();
    const bool hasCameraFocus = m_visualPresentation.CameraView().focusKind != RunReplayCameraFocusKind::None;
    const int screenW = screenWidth;
    const int screenH = screenHeight;
    const RuntimeMouseEdges& pointer = m_inputRouter.UiSnapshot().mouse;
    const RuntimePointerEvent& runtimePointer = m_inputRouter.RuntimeSnapshot().pointer;
    ReplayScrubberPointerFrame pointerFrame;
    pointerFrame.solverStats = m_timeline.Solver().GetStats();
    pointerFrame.gesture = m_interaction.Gesture().kind;
    pointerFrame.now = now;
    pointerFrame.mouseX = runtimePointer.clientX;
    pointerFrame.mouseY = runtimePointer.clientY;
    pointerFrame.screenWidth = screenW;
    pointerFrame.screenHeight = screenH;
    pointerFrame.leftPressed = pointer.leftPressed;
    pointerFrame.leftReleased = pointer.leftReleased;
    pointerFrame.restoreDown = m_inputRouter.RuntimeSnapshot().enterDown;
    pointerFrame.hasClientPosition = runtimePointer.hasClientPosition;
    pointerFrame.uiBlocksMouse = uiBlocksMouse;
    pointerFrame.editorModeEnabled = editorModeEnabled;
    pointerFrame.uiVisible = uiVisible;
    pointerFrame.uiMinimized = uiMinimized;
    pointerFrame.loadedPresentation = loadedPresentation;
    pointerFrame.pathTargetAvailable = m_visualPresentation.PathVisualizer().hasTarget;
    pointerFrame.predictionTimelineAvailable =
        m_predictionOwner.ActiveFrames().size() >= 2 || m_predictionOwner.State().BuildPrefixShouldBePresented();
    pointerFrame.currentPresentationAvailable = CurrentScrubSample() != nullptr;
    pointerFrame.currentSolverAvailable = CurrentSolverScrubSample() != nullptr;
    pointerFrame.scenePhysicsEnabled = scenePhysicsEnabled;
    pointerFrame.inspectionCameraActive = m_visualPresentation.CameraView().active;
    const ReplayScrubberPointerDecision decision = m_scrubberOwner.ResolvePointerAction( pointerFrame );
    if ( decision.cancelToolDrag )
    {
        CancelReplayToolDragState( m_interaction, m_inputRouter );
    }
    if ( decision.exitInspectionCamera )
    {
        exitInspectionCamera();
    }
    if ( !decision.surfaceAvailable )
    {
        return false;
    }

    const POINT mouse{ pointerFrame.mouseX, pointerFrame.mouseY };
    const bool leftReleased = decision.leftReleased;
    const auto scrubDragActive = [&]()
    { return m_interaction.Gesture().kind == RuntimeInteractionGestureKind::ReplayScrubDrag; };
    const auto horizonDragActive = [&]()
    { return m_interaction.Gesture().kind == RuntimeInteractionGestureKind::ReplayPredictionHorizonDrag; };
    const RunReplayTrack scrubTrack = decision.track;
    const UI::UIRect predictHorizon{ decision.horizonX,
                                     decision.horizonY,
                                     decision.horizonWidth,
                                     decision.horizonHeight };
    bool consumesMouse = decision.consumesMouse;
    const ReplayScrubberAction requestedAction = decision.action;

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
        HandleReplayPausePressed( m_predictionOwner,
                                  m_visualPresentation,
                                  m_scrubberOwner,
                                  solverPresentTrackPosition,
                                  m_authoring.VelocityEdit().enabled,
                                  hasCameraFocus,
                                  m_inputRouter,
                                  m_interaction,
                                  camera,
                                  now,
                                  outEnterInteractive );
        consumesMouse = true;
        break;
    case ReplayScrubberAction::ToggleVelocityEdit:
        HandleReplayVelocityEditPressed( m_authoring,
                                         m_predictionOwner,
                                         m_visualPresentation,
                                         m_scrubberOwner,
                                         solverPresentTrackPosition,
                                         hasCameraFocus,
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
        if ( !HandleReplayPredictionHorizonPressed( m_predictionOwner,
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
        HandleReplayPredictionPressed( m_predictionOwner,
                                       m_scrubberOwner,
                                       solverPresentTrackPosition,
                                       m_interaction,
                                       now,
                                       outEnterInteractive );
        consumesMouse = true;
        break;
    case ReplayScrubberAction::Save:
        outEnterInteractive = true;
        SavePresentationFromScrubber( now );
        consumesMouse = true;
        break;
    case ReplayScrubberAction::Load:
    {
        char path[MAX_PATH] = {};
        if ( SelectReplayPresentationArtifact( m_scrubberOwner, hwnd, now, path ) )
        {
            const bool loaded = m_timeline.LoadPresentationArtifact( path );
            if ( loaded )
            {
                const ReplayLoadedPresentationActivationRequest activation{
                    .hasLoadedPresentation = HasLoadedPresentation(),
                    .normalized = 0.25f,
                    .now = input.now,
                    .normalizedCurrentMode = input.normalizedCurrentMode,
                    .normalizedRestoreMode = input.normalizedRestoreMode,
                    .attachedFollow = input.attachedFollow,
                    .directorGrabbed = input.directorGrabbed };
                ActivateLoadedPresentationScrubber( activation,
                                                    m_inputRouter,
                                                    m_interaction,
                                                    cameras,
                                                    terrain,
                                                    camera,
                                                    mousePickup );
            }
            PublishReplayLoadResult( m_scrubberOwner, path, loaded, now );
        }
        consumesMouse = true;
        break;
    }
    case ReplayScrubberAction::Scrub:
        if ( !HandleReplayScrubPressed( m_scrubberOwner,
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

    const bool scrubberGestureHandled = TickReplayScrubberGesture( m_predictionOwner,
                                                                   m_scrubberOwner,
                                                                   solverPresentTrackPosition,
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
    ReplayScrubberView scrubber = m_scrubberOwner.View();
    if ( !scrubberGestureHandled && !loadedPresentation && !scrubber.historicalSamplePaused )
    {
        m_scrubberOwner.SetAllTrackPositions( solverPresentTrackPosition );
    }

    const bool scrubberTargetVisible = scrubDragActive() || horizonDragActive() || scrubber.historicalSamplePaused ||
                                       scrubber.liveAdvanceHeld || scrubber.visibleUntil >= now;
    m_scrubberOwner.UpdateVisibilityFade( scrubberTargetVisible,
                                          now,
                                          REPLAY_SCRUBBER_FADE_IN_SECONDS,
                                          REPLAY_SCRUBBER_FADE_OUT_SECONDS,
                                          REPLAY_SCRUBBER_FADE_EPSILON );
    scrubber = m_scrubberOwner.View();
    if ( scrubber.historicalSamplePaused || scrubber.liveAdvanceHeld ||
         m_visualPresentation.CameraView().focusKind != RunReplayCameraFocusKind::None )
    {
        enterInspectionCamera();
    }
    else
    {
        exitInspectionCamera();
    }
    return consumesMouse;
}
