/*
File: SkullbonezSource/Runtime/App/ReplayScrubberTools.cpp
Purpose:
  Implements replay scrubber input, inspection-camera, and live-restore policy.

Summary:
  The scrubber maps mouse/UI intent to retained solver or presentation samples.
  ReplayScrubber owns cursor transitions; ReplayRuntime receives a frame-scoped
  workspace view and coordinates typed transport, restore, and application
  commands across existing replay owners.

Glossary:
  Live restore: Applying a retained replay sample back into the current scene.
  Branch restore: Applying a historical replay sample as the new live timeline

    while preserving parent/source branch provenance.
  Inspection camera: Temporary replay-focused camera state for selected samples.

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
  - SkullbonezSource/Runtime/Prediction/ReplayPrediction.cpp
  - SkullbonezSource/Runtime/App/ReplayRuntime.h
  - Agentic/Reference/engine-glossary.md
*/
#include "../Replay/ReplayScrubber.h"
#include "ReplayRuntime.h"
#include "../../Assets/AssetKeys.h"
#include "../Camera/CameraCollection.h"
#include "../Input/InputRouter.h"
#include "../Interaction/RuntimeInteractionCommands.h"
#include "../Camera/CameraControlState.h"
#include "../Tools/RuntimeTools.h"
#include "../../Core/Profiler.h"
#include "../../Core/FatalError.h"
#include "../../Physics/ColliderStore.h"
#include "../../Physics/PhysicsBodyStore.h"
#include "../../Physics/PhysicsEngine.h"
#include "../Input/InputController.h"
#include "../Replay/ReplayOverlayLayout.h"
#include "../Replay/ReplayRestoreTransactions.h"
#include "../Scene/SceneController.h"
#include "../Scene/SceneWorld.h"
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

bool ReplayScrubber::BuildRestoreRequest( const ReplayScrubberRestoreSources& sources, double now,
                                          ReplayLiveRestoreRequest& outRequest, char* outReason, std::size_t reasonSize )
{
    outRequest = ReplayLiveRestoreRequest {};
    outRequest.now = now;

    if ( sources.hasLoadedPresentation && m_state.historicalSamplePaused &&
         m_state.activeTrack == RunReplayTrack::Presentation && sources.presentationSample )
    {
        outRequest.kind = ReplayLiveRestoreKind::V2ArtifactTarget;
        outRequest.requestedFrame = sources.presentationSample->frameIndex;
        outRequest.makeLiveBranch = true;
        outRequest.enterInteractive = true;
        outRequest.messageTrack = RunReplayTrack::Presentation;
        strncpy_s( outRequest.path, sizeof( outRequest.path ),
                   sources.loadedPresentationPath ? sources.loadedPresentationPath : "", _TRUNCATE );

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


void ReplayScrubber::CompleteRestore( const ReplayLiveRestoreRequest& request, bool restored,
                                      const RunReplayV2TargetRestoreResult& v2Result, const char* reason,
                                      RunReplayV2TargetRestoreResult* outV2Result, char* outReason, std::size_t reasonSize )
{
    const char* safeReason = reason ? reason : "";

    if ( request.kind == ReplayLiveRestoreKind::V2ArtifactTarget )
    {

        if ( outV2Result )
        {
            *outV2Result = v2Result;
        }

        fprintf( stderr, "[replay] V2 file restore %s target_frame=%llu branch_id=%u%s%s\n", restored ? "applied" : "failed",
                 static_cast<unsigned long long>( request.requestedFrame ), restored ? v2Result.branchId : 0,
                 safeReason[0] != '\0' ? ": " : "", safeReason );
    }
    else
    {
        fprintf( stderr, "[replay] Solver restore %s%s%s\n", restored ? "applied" : "failed",
                 safeReason[0] != '\0' ? ": " : "", safeReason );
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
    sprintf_s( m_state.saveMessage, sizeof( m_state.saveMessage ),
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

bool SelectReplayPresentationArtifact( ReplayScrubber& scrubber, HWND window, double now, char ( &outPath )[MAX_PATH] );
void PublishReplayLoadResult( ReplayScrubber& scrubber, const char* path, bool loaded, double now );

void PositionReplayCauseTreeCamera( SkullbonezCore::Environment::CameraCollection* cameras, const Vector3& targetPosition,
                                    float targetRadius )
{

    if ( !cameras )
    {
        return;
    }

    // Invariant: EnterInspectionCamera has already captured the operator's
    // restore pose before this function moves the free camera to the cause row.
    const Vector3 eye = cameras->GetRenderCameraTranslation();
    Vector3 direction = eye - targetPosition;
    const float directionLengthSquared = direction.x * direction.x + direction.y * direction.y + direction.z * direction.z;

    if ( directionLengthSquared > TOLERANCE * TOLERANCE )
    {
        direction *= 1.0f / sqrtf( directionLengthSquared );
    }
    else
    {
        direction = Vector3( 0.45f, 0.28f, 0.85f );
        direction *= 1.0f / sqrtf( direction.x * direction.x + direction.y * direction.y + direction.z * direction.z );
    }

    const float distance = (std::max)( 12.0f, targetRadius * 5.5f );
    const Vector3 newEye = targetPosition + direction * distance + Vector3( 0.0f, targetRadius * 0.35f, 0.0f );
    cameras->TweenPrimaryToPose( newEye, targetPosition, cameras->GetRenderCameraUp() );
    cameras->ResetRelativity();
}
} // namespace

void SkullbonezCore::Runtime::ReplayInteractionOperations::CancelToolGesture( RuntimeInteractionController& interaction )
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

void SkullbonezCore::Runtime::ReplayInteractionOperations::CancelToolDragState( RuntimeInteractionController& interaction,
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


void SkullbonezCore::Runtime::ReplayPresentationOperations::EnterInspectionCamera( ReplayPresentation& presentation, Environment::CameraCollection* cameras, CameraControlState& camera,
                                                                                   RunCameraMode normalizedCurrentMode, RuntimeInteractionController& interaction, InputRouter& inputRouter,
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

    mousePickup = RunMousePickupState {};

    if ( !IsReplayScrubberToolOwner( interaction.Owner() ) )
    {
        interaction.SetWorldInteractionOwnerInWorkspace( RuntimeWorkspace::Replay, WorldInteractionOwner::ReplayScrub,
                                                         InteractionExitReason::EnterReplay );
    }

    camera.mode = RunCameraMode::Inspect;

    if ( enteringInspectionCamera )
    {
        inputRouter.RequestCursorVisible( true );
        InputController::ResetMouseLook( camera );
    }
}


void SkullbonezCore::Runtime::ReplayPresentationOperations::ExitInspectionCamera( ReplayPresentation& presentation, const ReplayAuthoring& authoring, Environment::CameraCollection* cameras,
                                                                                  Geometry::Terrain* terrain, CameraControlState& camera, RunCameraMode normalizedRestoreMode, bool attachedFollow,
                                                                                  bool directorGrabbed, RuntimeInteractionController& interaction, InputRouter& inputRouter )
{
    const RunReplayCameraState replayCamera = presentation.CameraView();

    if ( !replayCamera.active )
    {
        return;
    }

    camera.mode = normalizedRestoreMode;

    if ( authoring.VelocityEdit().enabled )
    {
        interaction.SetWorldInteractionOwnerInWorkspace( RuntimeWorkspace::Replay, WorldInteractionOwner::ReplayVelocityEdit,
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
                cameras->TweenPrimaryToPose( replayCamera.restoreEye, replayCamera.restoreView, replayCamera.restoreUp );
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

bool SkullbonezCore::Runtime::ReplayPresentationOperations::BeginLoadedPresentationActivation( bool hasLoadedPresentation, ReplayScrubber& scrubber, ReplayPresentation& presentation, ReplayAuthoring& authoring,
                                                                                               RuntimeInteractionController& interaction, InputRouter& inputRouter )
{

    if ( !hasLoadedPresentation )
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

    const bool ownsReplayCapture = replayGesture && interaction.PointerCapture() == RuntimePointerCaptureOwner::ToolGesture;

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
    return true;
}

void SkullbonezCore::Runtime::ReplayPresentationOperations::ArmLoadedPresentation( float normalized, double now, ReplayScrubber& scrubber, ReplayPresentation& presentation, ReplayAuthoring& authoring,
                                                                                   ReplayPrediction& prediction, RuntimeInteractionController& interaction )
{

    // Invariant: the host camera has exited the previous inspection before the
    // new loaded-track state becomes visible.
    presentation.ClearPathState();
    authoring.ResetCauseTreeRows();
    prediction.ClearCache();
    prediction.MarkDirty();
    prediction.DisableForLiveAdvance();
    authoring.ResetVelocityEdit();
    scrubber.ArmLoadedPresentation( normalized, now, ReplayOverlay::REPLAY_SCRUBBER_VISIBLE_SECONDS );
    interaction.SetWorldInteractionOwnerInWorkspace( RuntimeWorkspace::Replay, WorldInteractionOwner::ReplayScrub,
                                                     InteractionExitReason::EnterReplay );
}

void ReplayRuntime::EnterInspectionCamera( Environment::CameraCollection* cameras, CameraControlState& camera,
                                           RunCameraMode normalizedCurrentMode, RuntimeInteractionController& interaction,
                                           InputRouter& inputRouter, RunMousePickupState& mousePickup )
{
    ReplayPresentationOperations::EnterInspectionCamera( m_visualPresentation, cameras, camera, normalizedCurrentMode,
                                                         interaction, inputRouter, mousePickup );
}

void ReplayRuntime::ExitInspectionCamera( Environment::CameraCollection* cameras, Geometry::Terrain* terrain,
                                          CameraControlState& camera, RunCameraMode normalizedRestoreMode,
                                          bool attachedFollow, bool directorGrabbed,
                                          RuntimeInteractionController& interaction, InputRouter& inputRouter )
{
    ReplayPresentationOperations::ExitInspectionCamera( m_visualPresentation, m_authoring, cameras, terrain, camera,
                                                        normalizedRestoreMode, attachedFollow, directorGrabbed, interaction,
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

bool ReplayRuntime::BeginLoadedPresentationActivationScrubber( bool hasLoadedPresentation, InputRouter& inputRouter,
                                                               RuntimeInteractionController& interaction )
{
    return ReplayPresentationOperations::BeginLoadedPresentationActivation( hasLoadedPresentation, m_scrubberOwner,
                                                                            m_visualPresentation, m_authoring, interaction,
                                                                            inputRouter );
}

void ReplayRuntime::ArmLoadedPresentationScrubber( float normalized, double now, RuntimeInteractionController& interaction )
{
    ReplayPresentationOperations::ArmLoadedPresentation( normalized, now, m_scrubberOwner, m_visualPresentation, m_authoring,
                                                         m_predictionOwner, interaction );
}


void ReplayRuntime::TickWorkspace( const ReplayWorkspaceFrameInput& input, InputRouter& inputRouter,
                                   RuntimeInteractionController& interaction, Physics::PhysicsEngine& physics,
                                   const SceneEntityStore& entities,
                                   std::span<const Rendering::RenderInstancePresentationRecord> presentation,
                                   Environment::CameraCollection* cameras, Geometry::Terrain* terrain,
                                   CameraControlState& camera, RunMousePickupState& mousePickup,
                                   ReplayWorkspaceOutput& output )
{
    output = ReplayWorkspaceOutput {};

    if ( !input.legacyPointerSurfaceActive )
    {

        // Why: semantic commands from ImGui have already reached ReplayRuntime.
        // The inactive Legacy pointer surface must neither compete for capture
        // nor interpret its hidden window as a reason to reset durable replay state.
        ReplayInteractionOperations::CancelToolDragState( interaction, inputRouter );
        return;
    }

    const bool planningOwnsMouse = m_planningOwner.TickPointerSurface( input.uiBlocksMouse, input.screenWidth, inputRouter );

    const ReplayInspectionCameraAction scrubberHostAction = TickScrubberInput( input.uiBlocksMouse || planningOwnsMouse,
                                                                               input.editorModeEnabled,
                                                                               input.scenePhysicsEnabled, input.uiVisible,
                                                                               input.uiMinimized, input.screenWidth,
                                                                               input.screenHeight, input.now, inputRouter,
                                                                               interaction, camera, output );

    output.consumesMouse = output.consumesMouse || planningOwnsMouse;
    const bool scrubberOwnsMouse = output.consumesMouse;
    bool loadedPresentationActivated = false;

    if ( output.loadPresentationRequested )
    {
        char path[MAX_PATH] = {};

        if ( SelectReplayPresentationArtifact( m_scrubberOwner, input.window, input.now, path ) )
        {
            const bool loaded = m_timeline.LoadPresentationArtifact( path );

            if ( loaded && BeginLoadedPresentationActivationScrubber( HasLoadedPresentation(), inputRouter, interaction ) )
            {
                ExitInspectionCamera( cameras, terrain, camera, input.normalizedRestoreMode, input.attachedFollow,
                                      input.directorGrabbed, interaction, inputRouter );

                ArmLoadedPresentationScrubber( 0.25f, input.now, interaction );
                EnterInspectionCamera( cameras, camera, input.normalizedCurrentMode, interaction, inputRouter, mousePickup );

                loadedPresentationActivated = true;
            }

            PublishReplayLoadResult( m_scrubberOwner, path, loaded, input.now );
        }
    }

    if ( !loadedPresentationActivated )
    {

        switch ( scrubberHostAction )
        {
        case ReplayInspectionCameraAction::Enter:
            EnterInspectionCamera( cameras, camera, input.normalizedCurrentMode, interaction, inputRouter, mousePickup );

            break;
        case ReplayInspectionCameraAction::Exit:
            ExitInspectionCamera( cameras, terrain, camera, input.normalizedRestoreMode, input.attachedFollow,
                                  input.directorGrabbed, interaction, inputRouter );

            break;
        case ReplayInspectionCameraAction::None:
            break;
        }
    }

    const Physics::PhysicsBodyStore& bodyStore = Physics::PhysicsEngine::ReadBodies( physics );
    const Physics::ColliderStore& colliderStore = Physics::PhysicsEngine::ReadColliders( physics );
    int focusedCameraRow = -1;
    bool causeTreeRowsReady = false;

    if ( !input.editorModeEnabled && input.screenWidth > 0 && input.screenHeight > 0 )
    {
        causeTreeRowsReady = m_predictionOwner.BuildCauseTreeRows( m_authoring, m_visualPresentation.PathVisualizer(),
                                                                   CurrentSolverScrubSample(), presentation, bodyStore,
                                                                   m_visualPresentation.CameraView(), focusedCameraRow );
    }

    if ( focusedCameraRow >= 0 )
    {
        m_visualPresentation.SetCameraFocusedRow( focusedCameraRow );
    }

    int requestedCauseTreeFocusRow = -1;
    bool exitCauseTreeInspection = false;
    const bool causeTreeOwnsMouse = m_authoring.TickCauseTreeInput( m_visualPresentation, m_scrubberOwner, inputRouter,
                                                                    interaction, causeTreeRowsReady,
                                                                    input.uiBlocksMouse || scrubberOwnsMouse,
                                                                    input.wheelDelta, input.editorModeEnabled,
                                                                    input.screenWidth, input.screenHeight,
                                                                    requestedCauseTreeFocusRow, exitCauseTreeInspection );

    Vector3 causeTreeTargetPosition = Vector3( 0.0f, 0.0f, 0.0f );
    float causeTreeTargetRadius = 0.0f;

    if ( requestedCauseTreeFocusRow >= 0 &&
         m_predictionOwner.ActivateCauseTreeRow( m_authoring, requestedCauseTreeFocusRow, m_visualPresentation,
                                                 m_scrubberOwner, CurrentSolverScrubSample(), bodyStore, colliderStore,
                                                 interaction, causeTreeTargetPosition, causeTreeTargetRadius ) )
    {
        output.enterInteractive = true;
        EnterInspectionCamera( cameras, camera, input.normalizedCurrentMode, interaction, inputRouter, mousePickup );
        PositionReplayCauseTreeCamera( cameras, causeTreeTargetPosition, causeTreeTargetRadius );
        InputController::ResetMouseLook( camera );
        inputRouter.RequestCursorVisible( true );
    }
    else if ( exitCauseTreeInspection )
    {
        ExitInspectionCamera( cameras, terrain, camera, input.normalizedRestoreMode, input.attachedFollow,
                              input.directorGrabbed, interaction, inputRouter );
    }

    ApplyAuthoringPredictionRequest();

    bool velocityEditOwnsMouse = false;
    ReplayInspectionCameraAction velocityInspectionCameraAction = ReplayInspectionCameraAction::None;

    if ( m_authoring.PrepareVelocityEditInput( input.editorModeEnabled, input.scenePhysicsEnabled, input.screenWidth,
                                               input.screenHeight, inputRouter, interaction ) )
    {
        bool velocityPathPickRequested = false;
        velocityEditOwnsMouse = m_authoring.TickVelocityEditInput( m_visualPresentation, m_scrubberOwner, input.pointerRay,
                                                                   input.uiBlocksMouse || scrubberOwnsMouse ||
                                                                       causeTreeOwnsMouse,
                                                                   input.now, inputRouter, interaction, physics,
                                                                   entities.Count(), output.enterInteractive,
                                                                   velocityPathPickRequested,
                                                                   velocityInspectionCameraAction );

        if ( velocityPathPickRequested )
        {
            (void)m_authoring.TryPickVelocityEditTarget( m_visualPresentation, m_scrubberOwner, CurrentSolverScrubSample(),
                                                         entities, presentation, physics, input.pointerRay, interaction,
                                                         input.now, output.enterInteractive,
                                                         velocityInspectionCameraAction );
        }
    }

    if ( velocityInspectionCameraAction == ReplayInspectionCameraAction::Enter )
    {
        EnterInspectionCamera( cameras, camera, input.normalizedCurrentMode, interaction, inputRouter, mousePickup );
    }
    else if ( velocityInspectionCameraAction == ReplayInspectionCameraAction::Exit )
    {
        ExitInspectionCamera( cameras, terrain, camera, input.normalizedRestoreMode, input.attachedFollow,
                              input.directorGrabbed, interaction, inputRouter );
    }

    ApplyAuthoringPredictionRequest();

    output.consumesMouse = output.consumesMouse || causeTreeOwnsMouse || velocityEditOwnsMouse;
    output.enterInteractive = output.enterInteractive || output.restoreRequest.enterInteractive;
}


void ReplayRuntime::ResetSceneTimeline( const ReplaySceneTimelineResetInput& input, InputRouter& inputRouter,
                                        RuntimeInteractionController& interaction, Environment::CameraCollection* cameras,
                                        Geometry::Terrain* terrain, CameraControlState& camera,
                                        RunCameraMode normalizedRestoreMode, bool attachedFollow, bool directorGrabbed )
{
    ReplayInteractionOperations::CancelToolDragState( interaction, inputRouter );
    const ReplaySceneTimelineResetResult begin = BeginSceneTimelineReset( input );

    if ( begin.exitInspectionCamera )
    {
        ReplayPresentationOperations::ExitInspectionCamera( m_visualPresentation, m_authoring, cameras, terrain, camera,
                                                            normalizedRestoreMode, attachedFollow, directorGrabbed,
                                                            interaction, inputRouter );
    }

    const ReplaySceneTimelineResetResult finish = FinishSceneTimelineReset( input );

    if ( finish.exitInspectionCamera )
    {
        ReplayPresentationOperations::ExitInspectionCamera( m_visualPresentation, m_authoring, cameras, terrain, camera,
                                                            normalizedRestoreMode, attachedFollow, directorGrabbed,
                                                            interaction, inputRouter );
    }
}


ReplayLiveRestoreOutcome ReplayLiveRestoreOperations::BuildOutcome( const ReplayRestoreTransaction& transaction,
                                                                    ReplayLiveRestoreKind kind, bool restored )
{
    ReplayLiveRestoreOutcome outcome;
    outcome.requested = kind != ReplayLiveRestoreKind::None;
    outcome.restored = restored;
    outcome.v2Result = transaction.Result();
    strncpy_s( outcome.reason, sizeof( outcome.reason ), transaction.FailureReason(), _TRUNCATE );
    outcome.enterInteractive = transaction.EnterInteractiveRequested();
    return outcome;
}

void ReplayRuntime::ApplyRestoredBranchTimeline( ReplayRestoreTransaction& transaction,
                                                 const ReplayLiveRestoreOutcome& outcome, SceneController& sceneController,
                                                 InputRouter& inputRouter, RuntimeInteractionController& interaction,
                                                 CameraControlState& camera, RunCameraMode normalizedRestoreMode,
                                                 bool attachedFollow, bool directorGrabbed )
{

    if ( outcome.restored && transaction.TimelineResetRequired() )
    {
        ReplaySceneTimelineResetInput reset = transaction.TimelineReset();
        reset.preserveBranchMetadata = true;
        SceneWorld& world = sceneController.Scene();
        ResetSceneTimeline( reset, inputRouter, interaction, &world.Cameras(), world.Terrain().Get(), camera,
                            normalizedRestoreMode, attachedFollow, directorGrabbed );

        SubmitEvent( ReplayEventCommandOperations::BuildCommand( ReplayEventKind::BranchRestore, 0, false, 0,
                                                                 static_cast<int32_t>( transaction.ParentBranchId() ),
                                                                 transaction.BranchSceneFrame(), 0, 0,
                                                                 transaction.BranchSolverHash(),
                                                                 "hash-verified replay restore" ) );

        transaction.MarkTimelineResetApplied();
        transaction.Complete();
    }
}

void ReplayRuntime::CompleteLiveRestoreScrubber( const ReplayRestoreTransaction& transaction,
                                                 const ReplayLiveRestoreRequest& request, ReplayLiveRestoreOutcome& outcome )
{

    // Invariant: scrubber publication is the last restore phase. A caller
    // cannot publish success before branch provenance is committed, or publish
    // failure before rollback reaches a terminal cursor.
    transaction.RequireScrubberPublicationTerminal( outcome.restored );

    const char* reason = outcome.restored ? "restored hash match" : transaction.FailureReason();
    strncpy_s( outcome.reason, sizeof( outcome.reason ), reason, _TRUNCATE );
    m_scrubberOwner.CompleteRestore( request, outcome.restored, transaction.Result(), reason, &outcome.v2Result,
                                     outcome.reason, sizeof( outcome.reason ) );
}

namespace
{
void EndReplayScrubberGesture( InputRouter& inputRouter, RuntimeInteractionController& interaction,
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


void ApplyReplayLiveAdvanceAction( ReplayPrediction& predictionOwner, ReplayPresentation& presentation,
                                   ReplayScrubber& scrubber, bool held, float previousPredictionPresentT,
                                   bool velocityEditEnabled, bool hasCameraFocus, InputRouter& inputRouter,
                                   RuntimeInteractionController& interaction, CameraControlState& camera,
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
            EndReplayScrubberGesture( inputRouter, interaction, RuntimeInteractionGestureKind::ReplayPredictionHorizonDrag );
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
            interaction.SetWorldInteractionOwnerInWorkspace( RuntimeWorkspace::Replay, WorldInteractionOwner::ReplayScrub,
                                                             InteractionExitReason::EnterReplay );
        }
    }
    else if ( velocityEditEnabled )
    {
        interaction.SetWorldInteractionOwnerInWorkspace( RuntimeWorkspace::Replay, WorldInteractionOwner::ReplayVelocityEdit,
                                                         InteractionExitReason::EnterReplay );
    }
    else if ( !scrubber.View().historicalSamplePaused && !hasCameraFocus )
    {
        interaction.EnterCameraMode( camera.mode );
    }
}


void HandleReplayPausePressed( ReplayPrediction& predictionOwner, ReplayPresentation& presentation, ReplayScrubber& scrubber,
                               float solverPresentTrackPosition, bool velocityEditEnabled, bool hasCameraFocus,
                               InputRouter& inputRouter, RuntimeInteractionController& interaction,
                               CameraControlState& camera, double now, bool& outEnterInteractive )
{
    ApplyReplayLiveAdvanceAction( predictionOwner, presentation, scrubber, !scrubber.View().liveAdvanceHeld,
                                  solverPresentTrackPosition, velocityEditEnabled, hasCameraFocus, inputRouter, interaction,
                                  camera, outEnterInteractive );

    KeepReplayScrubberVisible( scrubber, now );
}


void HandleReplayVelocityEditPressed( ReplayAuthoring& authoring, ReplayPrediction& predictionOwner,
                                      ReplayPresentation& presentation, ReplayScrubber& scrubber,
                                      float solverPresentTrackPosition, bool hasCameraFocus, InputRouter& inputRouter,
                                      RuntimeInteractionController& interaction, CameraControlState& camera, double now,
                                      bool& outEnterInteractive )
{
    PROFILE_SCOPED( authoring.ProfilerBorrow(), "Frame/Replay/VelocityEdit/Toggle" );
    const bool enableVelocityEdit = !authoring.VelocityEdit().enabled;

    if ( authoring.SetVelocityEditEnabled( enableVelocityEdit ) )
    {

        // Why: authoring emits a value command so prediction is refreshed in
        // this composition turn without storing an owner pointer or callback.
        const ReplayAuthoringPredictionRequest request = authoring.TakePredictionRequest();
        predictionOwner.ApplyAuthoringRequest( request, REPLAY_PREDICTION_MIN_SECONDS, REPLAY_PREDICTION_MAX_SECONDS );

        CancelReplayToolDragState( interaction, inputRouter );

        if ( enableVelocityEdit )
        {
            outEnterInteractive = true;
            ApplyReplayLiveAdvanceAction( predictionOwner, presentation, scrubber, true, solverPresentTrackPosition, true,
                                          hasCameraFocus, inputRouter, interaction, camera, outEnterInteractive );

            interaction.SetWorldInteractionOwnerInWorkspace( RuntimeWorkspace::Replay,
                                                             WorldInteractionOwner::ReplayVelocityEdit,
                                                             InteractionExitReason::EnterReplay );
        }
        else if ( interaction.Owner() == WorldInteractionOwner::ReplayVelocityEdit )
        {
            interaction.SetWorldInteractionOwnerInWorkspace( RuntimeWorkspace::Replay, WorldInteractionOwner::ReplayScrub,
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


void SetReplayPredictionHorizonFromPointer( ReplayPrediction& predictionOwner, ReplayScrubber& scrubber,
                                            RuntimeInteractionController& interaction,
                                            const SkullbonezCore::UI::UIRect& horizon, int mouseX, double now,
                                            bool ensurePredictionOwner, bool& outEnterInteractive )
{
    outEnterInteractive = true;

    if ( ensurePredictionOwner )
    {
        interaction.SetWorldInteractionOwnerInWorkspace( RuntimeWorkspace::Replay, WorldInteractionOwner::ReplayPrediction,
                                                         InteractionExitReason::EnterReplay );
    }

    const float nextSeconds = ReplayPredictionHorizonFromMouse( mouseX, horizon );
    (void)predictionOwner.SetHorizonSeconds( nextSeconds );
    KeepReplayScrubberVisible( scrubber, now );
}


void HandleReplayPredictionPressed( ReplayPrediction& predictionOwner, ReplayScrubber& scrubber,
                                    float previousPredictionPresentT, RuntimeInteractionController& interaction, double now,
                                    bool& outEnterInteractive )
{
    outEnterInteractive = true;
    const bool predictionEnabled = predictionOwner.ToggleEnabled();
    interaction.SetWorldInteractionOwnerInWorkspace( RuntimeWorkspace::Replay,
                                                     predictionEnabled ? WorldInteractionOwner::ReplayPrediction
                                                                       : WorldInteractionOwner::ReplayScrub,
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


void HandleReplayBranchPressed( ReplayScrubber& scrubber, RuntimeInteractionController& interaction,
                                const ReplayScrubberRestoreSources& sources, double now,
                                ReplayLiveRestoreRequest& outRestoreRequest )
{
    interaction.SetWorldInteractionOwnerInWorkspace( RuntimeWorkspace::Replay, WorldInteractionOwner::ReplayBranchTarget,
                                                     InteractionExitReason::EnterReplay );

    (void)scrubber.BuildRestoreRequest( sources, now, outRestoreRequest );
}


bool BeginReplayScrubberGesture( InputRouter& inputRouter, RuntimeInteractionController& interaction,
                                 RuntimeInteractionGestureKind kind, WorldInteractionOwner owner, int mouseX, int mouseY )
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


void EndReplayScrubberGesture( InputRouter& inputRouter, RuntimeInteractionController& interaction,
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


bool HandleReplayPredictionHorizonPressed( ReplayPrediction& predictionOwner, ReplayScrubber& scrubber,
                                           InputRouter& inputRouter, RuntimeInteractionController& interaction,
                                           const SkullbonezCore::UI::UIRect& horizon, int mouseX, int mouseY, double now,
                                           bool& outEnterInteractive )
{

    if ( !BeginReplayScrubberGesture( inputRouter, interaction, RuntimeInteractionGestureKind::ReplayPredictionHorizonDrag,
                                      WorldInteractionOwner::ReplayPrediction, mouseX, mouseY ) )
    {
        return false;
    }

    SetReplayPredictionHorizonFromPointer( predictionOwner, scrubber, interaction, horizon, mouseX, now, false,
                                           outEnterInteractive );

    return true;
}


bool HandleReplayScrubPressed( ReplayScrubber& scrubber, InputRouter& inputRouter, RuntimeInteractionController& interaction,
                               RunReplayTrack track, int mouseX, int mouseY, bool& outEnterInteractive )
{
    outEnterInteractive = true;

    if ( !BeginReplayScrubberGesture( inputRouter, interaction, RuntimeInteractionGestureKind::ReplayScrubDrag,
                                      WorldInteractionOwner::ReplayScrub, mouseX, mouseY ) )
    {
        return false;
    }

    scrubber.SelectTrack( track );
    return true;
}


bool TickReplayScrubDrag( ReplayScrubber& scrubber, InputRouter& inputRouter, RuntimeInteractionController& interaction,
                          float solverPresentTrackPosition, bool loadedPresentation, int mouseX, int screenWidth,
                          int screenHeight, bool leftReleased )
{

    if ( interaction.Gesture().kind != RuntimeInteractionGestureKind::ReplayScrubDrag )
    {
        return false;
    }

    const RunReplayTrack activeTrack = scrubber.View().activeTrack;
    scrubber.SetTrackPosition( activeTrack,
                               ReplayScrubberPositionFromMouse( mouseX, screenWidth, screenHeight, activeTrack ) );

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

bool TickReplayPredictionHorizonDrag( ReplayPrediction& predictionOwner, ReplayScrubber& scrubber, InputRouter& inputRouter,
                                      RuntimeInteractionController& interaction, bool& outEnterInteractive,
                                      const SkullbonezCore::UI::UIRect& predictionHorizon, int mouseX, bool leftReleased,
                                      double now )
{

    if ( interaction.Gesture().kind != RuntimeInteractionGestureKind::ReplayPredictionHorizonDrag )
    {
        return false;
    }

    SetReplayPredictionHorizonFromPointer( predictionOwner, scrubber, interaction, predictionHorizon, mouseX, now, false,
                                           outEnterInteractive );

    if ( leftReleased )
    {
        EndReplayScrubberGesture( inputRouter, interaction, RuntimeInteractionGestureKind::ReplayPredictionHorizonDrag );
    }

    return true;
}
} // namespace

ReplayScrubberPointerDecision ReplayScrubber::ResolvePointerAction( const ReplayScrubberPointerFrame& frame )
{
    ReplayScrubberPointerDecision decision;
    const ReplayScrubberInputFrame inputFrame = BeginInputFrame( frame.leftPressed, frame.leftReleased, frame.restoreDown );

    decision.leftReleased = inputFrame.leftReleased;

    const bool scrubberAllowed = !frame.editorModeEnabled && frame.uiVisible && frame.uiMinimized;
    const bool replaySurfaceAvailable = frame.loadedPresentation || frame.solverStats.enabled;

    if ( !scrubberAllowed || !replaySurfaceAvailable || frame.screenWidth <= 0 || frame.screenHeight <= 0 )
    {
        decision.cancelToolDrag = true;
        const ReplayScrubberUnavailableResult unavailable = ResetUnavailableSurface( frame.loadedPresentation,
                                                                                     frame.inspectionCameraActive );

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

    ReplayScrubberSurfaceInput surfaceInput = DescribeReplayScrubberAvailability( View(), frame.solverStats,
                                                                                  frame.loadedPresentation,
                                                                                  frame.pathTargetAvailable,
                                                                                  frame.predictionTimelineAvailable,
                                                                                  frame.currentPresentationAvailable,
                                                                                  frame.currentSolverAvailable,
                                                                                  frame.scenePhysicsEnabled );

    surfaceInput.hotZoneEnabled = !frame.uiBlocksMouse;
    surfaceInput.screenW = frame.screenWidth;
    surfaceInput.screenH = frame.screenHeight;
    surfaceInput.gesture = frame.gesture;
    decision.track = surfaceInput.track;
    ReplayScrubberSurface surface;
    BuildReplayScrubberSurface( surfaceInput, surface );
    surface.ResolvePointer( frame.mouseX, frame.mouseY );

    const auto isHotControl = [&]( ReplayScrubberControl control )
    { return surface.hasHotControl && surface.hotControl == ReplayScrubberControlId( control ); };

    const RuntimeUiControl* pointerControl = surface.hasPointerControl ? surface.Find( surface.pointerControl ) : nullptr;

    const RuntimeUiControl* horizonControl = surface.Find( ReplayScrubberControlId( ReplayScrubberControl::PredictionHorizon ) );

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
    const bool replayStateKeepsScrubberVisible = replayDragInProgress || scrubber.historicalSamplePaused ||
                                                 scrubber.liveAdvanceHeld;

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

void ReplayRuntime::ApplyTransportCommand( const ReplayTransportCommand& command, const ReplayTransportHostContext& host,
                                           InputRouter& inputRouter, RuntimeInteractionController& interaction,
                                           Environment::CameraCollection* cameras, Geometry::Terrain* terrain,
                                           CameraControlState& camera, RunMousePickupState& mousePickup,
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
            interaction.SetWorldInteractionOwnerInWorkspace( RuntimeWorkspace::Replay, WorldInteractionOwner::ReplayScrub,
                                                             InteractionExitReason::EnterReplay );
        }
    };

    const auto setCursor = [&]( float normalized )
    {
        const bool loaded = HasLoadedPresentation();

        const RunReplayTrack track = loaded ? RunReplayTrack::Presentation : RunReplayTrack::Solver;
        const std::size_t retainedCount = loaded ? m_timeline.LoadedPresentation().samples.size()
                                                 : m_timeline.Solver().GetStats().sampleCount;

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
        ApplyReplayLiveAdvanceAction( m_predictionOwner, m_visualPresentation, m_scrubberOwner, false, solverPresent,
                                      m_authoring.VelocityEdit().enabled, false, inputRouter, interaction, camera,
                                      enterInteractive );

        m_authoring.ClearCauseTreeFocus();
        ExitInspectionCamera( cameras, terrain, camera, host.normalizedRestoreMode, host.attachedFollow,
                              host.directorGrabbed, interaction, inputRouter );

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
        const std::size_t retainedCount = loaded ? m_timeline.LoadedPresentation().samples.size()
                                                 : m_timeline.Solver().GetStats().sampleCount;

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
        HandleReplayPausePressed( m_predictionOwner, m_visualPresentation, m_scrubberOwner, solverPresent,
                                  m_authoring.VelocityEdit().enabled, hasCameraFocus, inputRouter, interaction, camera,
                                  host.now, output.enterInteractive );

        break;
    case ReplayTransportAction::SetRevealSpeed:
        m_predictionOwner.SetRevealRatePreservingCursor( command.value );
        feedback( "PREDICTION REVEAL SPEED UPDATED" );
        break;
    case ReplayTransportAction::Scrub:
        (void)setCursor( command.value );
        break;
    case ReplayTransportAction::TogglePrediction:
        HandleReplayPredictionPressed( m_predictionOwner, m_scrubberOwner, solverPresent, interaction, host.now,
                                       output.enterInteractive );

        break;
    case ReplayTransportAction::SetPredictionHorizon:
        m_predictionOwner.SetHorizonSeconds( std::clamp( command.value, REPLAY_PREDICTION_MIN_SECONDS, REPLAY_PREDICTION_MAX_SECONDS ) );
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

            if ( loaded && BeginLoadedPresentationActivationScrubber( HasLoadedPresentation(), inputRouter, interaction ) )
            {
                ExitInspectionCamera( cameras, terrain, camera, host.normalizedRestoreMode, host.attachedFollow,
                                      host.directorGrabbed, interaction, inputRouter );

                ArmLoadedPresentationScrubber( 0.25f, host.now, interaction );
                EnterInspectionCamera( cameras, camera, host.normalizedCurrentMode, interaction, inputRouter, mousePickup );
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

ReplayInspectionCameraAction ReplayRuntime::TickScrubberInput( bool uiBlocksMouse, bool editorModeEnabled,
                                                               bool scenePhysicsEnabled, bool uiVisible, bool uiMinimized,
                                                               int screenWidth, int screenHeight, double now,
                                                               InputRouter& inputRouter,
                                                               RuntimeInteractionController& interaction,
                                                               CameraControlState& camera, ReplayWorkspaceOutput& output )
{
    output.restoreRequest = ReplayLiveRestoreRequest {};
    ReplayInspectionCameraAction hostAction = ReplayInspectionCameraAction::None;
    PROFILE_SCOPED( m_profiler, "Frame/Replay/ScrubberInput" );
    const bool loadedPresentation = HasLoadedPresentation();
    const float solverPresentTrackPosition = SolverPresentTrackPosition();
    const bool hasCameraFocus = m_visualPresentation.CameraView().focusKind != RunReplayCameraFocusKind::None;
    const int screenW = screenWidth;
    const int screenH = screenHeight;
    const RuntimeMouseEdges& pointer = inputRouter.UiSnapshot().mouse;
    const RuntimePointerEvent& runtimePointer = inputRouter.RuntimeSnapshot().pointer;
    ReplayScrubberPointerFrame pointerFrame;
    pointerFrame.solverStats = m_timeline.Solver().GetStats();
    pointerFrame.gesture = interaction.Gesture().kind;
    pointerFrame.now = now;
    pointerFrame.mouseX = runtimePointer.clientX;
    pointerFrame.mouseY = runtimePointer.clientY;
    pointerFrame.screenWidth = screenW;
    pointerFrame.screenHeight = screenH;
    pointerFrame.leftPressed = pointer.leftPressed;
    pointerFrame.leftReleased = pointer.leftReleased;
    pointerFrame.restoreDown = inputRouter.RuntimeSnapshot().enterDown;
    pointerFrame.hasClientPosition = runtimePointer.hasClientPosition;
    pointerFrame.uiBlocksMouse = uiBlocksMouse;
    pointerFrame.editorModeEnabled = editorModeEnabled;
    pointerFrame.uiVisible = uiVisible;
    pointerFrame.uiMinimized = uiMinimized;
    pointerFrame.loadedPresentation = loadedPresentation;
    pointerFrame.pathTargetAvailable = m_visualPresentation.PathVisualizer().hasTarget;
    pointerFrame.predictionTimelineAvailable = m_predictionOwner.ActiveFrames().size() >= 2 ||
                                               m_predictionOwner.State().BuildPrefixShouldBePresented();

    pointerFrame.currentPresentationAvailable = CurrentScrubSample() != nullptr;
    pointerFrame.currentSolverAvailable = CurrentSolverScrubSample() != nullptr;
    pointerFrame.scenePhysicsEnabled = scenePhysicsEnabled;
    pointerFrame.inspectionCameraActive = m_visualPresentation.CameraView().active;
    const ReplayScrubberPointerDecision decision = m_scrubberOwner.ResolvePointerAction( pointerFrame );

    if ( decision.cancelToolDrag )
    {
        CancelReplayToolDragState( interaction, inputRouter );
    }

    if ( decision.exitInspectionCamera )
    {
        hostAction = ReplayInspectionCameraAction::Exit;
    }

    if ( !decision.surfaceAvailable )
    {
        return hostAction;
    }

    const POINT mouse { pointerFrame.mouseX, pointerFrame.mouseY };
    const bool leftReleased = decision.leftReleased;
    const auto scrubDragActive = [&]()
    { return interaction.Gesture().kind == RuntimeInteractionGestureKind::ReplayScrubDrag; };

    const auto horizonDragActive = [&]()
    { return interaction.Gesture().kind == RuntimeInteractionGestureKind::ReplayPredictionHorizonDrag; };

    const RunReplayTrack scrubTrack = decision.track;
    const UI::UIRect predictHorizon { decision.horizonX, decision.horizonY, decision.horizonWidth, decision.horizonHeight };

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
        HandleReplayBranchPressed( m_scrubberOwner, interaction, sources, now, output.restoreRequest );
        output.consumesMouse = true;
        return hostAction;
    }
    case ReplayScrubberAction::TogglePause:
        HandleReplayPausePressed( m_predictionOwner, m_visualPresentation, m_scrubberOwner, solverPresentTrackPosition,
                                  m_authoring.VelocityEdit().enabled, hasCameraFocus, inputRouter, interaction, camera, now,
                                  output.enterInteractive );

        consumesMouse = true;
        break;
    case ReplayScrubberAction::ToggleVelocityEdit:
        HandleReplayVelocityEditPressed( m_authoring, m_predictionOwner, m_visualPresentation, m_scrubberOwner,
                                         solverPresentTrackPosition, hasCameraFocus, inputRouter, interaction, camera, now,
                                         output.enterInteractive );

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

        if ( !HandleReplayPredictionHorizonPressed( m_predictionOwner, m_scrubberOwner, inputRouter, interaction,
                                                    predictHorizon, mouse.x, mouse.y, now, output.enterInteractive ) )
        {
            output.consumesMouse = consumesMouse;
            return hostAction;
        }

        consumesMouse = true;
        break;
    case ReplayScrubberAction::TogglePrediction:
        HandleReplayPredictionPressed( m_predictionOwner, m_scrubberOwner, solverPresentTrackPosition, interaction, now,
                                       output.enterInteractive );

        consumesMouse = true;
        break;
    case ReplayScrubberAction::Save:
        output.enterInteractive = true;
        SavePresentationFromScrubber( now );
        consumesMouse = true;
        break;
    case ReplayScrubberAction::Load:
        consumesMouse = true;
        output.loadPresentationRequested = true;
        break;
    case ReplayScrubberAction::Scrub:

        if ( !HandleReplayScrubPressed( m_scrubberOwner, inputRouter, interaction, scrubTrack, mouse.x, mouse.y,
                                        output.enterInteractive ) )
        {
            output.consumesMouse = consumesMouse;
            return hostAction;
        }

        break;
    case ReplayScrubberAction::None:
        break;
    }

    const bool scrubberGestureHandled = TickReplayScrubDrag( m_scrubberOwner, inputRouter, interaction,
                                                             solverPresentTrackPosition, loadedPresentation, mouse.x,
                                                             screenW, screenH, leftReleased ) ||
                                        TickReplayPredictionHorizonDrag( m_predictionOwner, m_scrubberOwner, inputRouter,
                                                                         interaction, output.enterInteractive,
                                                                         predictHorizon, mouse.x, leftReleased, now );

    consumesMouse = consumesMouse || scrubberGestureHandled;
    ReplayScrubberView scrubber = m_scrubberOwner.View();

    if ( !scrubberGestureHandled && !loadedPresentation && !scrubber.historicalSamplePaused )
    {
        m_scrubberOwner.SetAllTrackPositions( solverPresentTrackPosition );
    }

    const bool scrubberTargetVisible = scrubDragActive() || horizonDragActive() || scrubber.historicalSamplePaused ||
                                       scrubber.liveAdvanceHeld || scrubber.visibleUntil >= now;

    m_scrubberOwner.UpdateVisibilityFade( scrubberTargetVisible, now, REPLAY_SCRUBBER_FADE_IN_SECONDS,
                                          REPLAY_SCRUBBER_FADE_OUT_SECONDS, REPLAY_SCRUBBER_FADE_EPSILON );

    scrubber = m_scrubberOwner.View();

    if ( scrubber.historicalSamplePaused || scrubber.liveAdvanceHeld ||
         m_visualPresentation.CameraView().focusKind != RunReplayCameraFocusKind::None )
    {
        hostAction = ReplayInspectionCameraAction::Enter;
    }
    else
    {
        hostAction = ReplayInspectionCameraAction::Exit;
    }

    output.consumesMouse = consumesMouse;
    return hostAction;
}
