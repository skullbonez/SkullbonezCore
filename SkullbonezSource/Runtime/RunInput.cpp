/*
File: SkullbonezSource/Runtime/RunInput.cpp
Purpose:
  Implements InputRouter transition, pointer, camera-mode, and focus behavior.

Summary:
  InputRouter consumes already-sampled semantic and pointer values, sequences
  cleanup through concrete interaction/tool/replay owners, and publishes the
  resulting input-mode and pointer-presentation state.

Glossary:
  Interaction owner: The single editor, replay, launcher, or camera workspace
    allowed to interpret the current world gesture.
  Presentation state: Router-owned desired cursor visibility and native mouse
    capture, applied after UI and interaction policy settle.
  Semantic action: Fixed ordered key-edge event routed without polling live
    hardware again later in the frame.
  World ray: Camera-projected pointer direction consumed by editor tools after
    UI has declined the gesture.

Invariants:
  - Exactly one interaction owner receives a world gesture.
  - Focus loss cancels active gestures before releasing native capture.
  - InputRouter retains input policy state but never retains borrowed domain
    owners passed to routing operations.

Related:
  - InputRouter.h declares the retained input owner.
  - InputFrameExecution.cpp owns stateless per-frame composition.
  - Agentic/Reference/runtime-reference.md
  - Agentic/Reference/comment-style-guide.md
*/
#include "Run.h"
#include "RuntimeOverlayDiagnostics.h"
#include "InputFrame.h"
#include "RuntimeFrameViews.h"
#include "AttachedCameraController.h"
#include "Editor/EditorTools.h"
#include "InputController.Bindings.h"
#include "InputController.h"
#include "Replay/ReplayOverlayLayout.h"
#include "Replay/ReplayRestoreService.h"
#include "Replay/ReplayRestoreTransactions.h"
#include "RunDemoDirector.h"
#include "RuntimeInteractionCommands.h"
#include "Scene/SceneRuntimeCreate.h"
#include "RuntimeTuning.h"
#include "Scene/SceneRuntimeGeneratedControls.h"
#include "Scene/SceneRuntimeLoad.h"
#include "Scene/SceneRuntimeStyle.h"
#include "../Core/Log.h"
#include "../Physics/ColliderStore.h"
#include "../Physics/PhysicsBodyStore.h"
#include "../UI/UI.h"
#include "../UI/UILayout.h"

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>

using namespace SkullbonezCore::Runtime;
using namespace SkullbonezCore::Math::CollisionDetection;
using namespace SkullbonezCore::Math::Orientation;
using namespace SkullbonezCore::Math::Transformation;
using namespace SkullbonezCore::Physics;
using namespace SkullbonezCore::UI::Layout;
using namespace SkullbonezCore::Runtime::RunInternal;


void InputRouter::ApplyInteractionTransitionCleanup( const RuntimeInteractionTransition& transition,
                                                     RuntimeFrameInteractionView& interactionOwners,
                                                     RuntimeFrameSceneView& sceneOwners,
                                                     ReplayRuntime& replayRuntime,
                                                     RunCameraMode replayRestoreCameraMode )
{
    RuntimeTools& runtimeTools = interactionOwners.runtimeTools;
    RuntimeInteractionController& interaction = interactionOwners.interaction;
    RunCameraState& camera = interactionOwners.camera;
    SceneController& models = sceneOwners.sceneController;
    Environment::CameraCollection& cameras = models.Cameras();
    Geometry::Terrain* terrain = models.Terrain().Get();
    PhysicsEngine& physics = models.Physics();
    const bool attachedCameraFollow = interactionOwners.attachedCamera.State().activeFollow;
    const bool directorGrabbed = camera.director.grabbed;
    const bool enteringReplay = transition.workspace == RuntimeWorkspace::Replay;
    const bool enteringEdit = transition.workspace == RuntimeWorkspace::Edit;
    const bool enteringTool =
        transition.owner == WorldInteractionOwner::Launcher || transition.owner == WorldInteractionOwner::Manipulator;
    const bool editorOwnerSwitchWithinEdit =
        enteringEdit && IsEditorWorldOwner( transition.previousOwner ) && IsEditorWorldOwner( transition.owner );
    const bool inspectGizmoClaimWithinInspect = transition.workspace == RuntimeWorkspace::Inspect &&
                                                transition.owner == WorldInteractionOwner::InspectGizmo &&
                                                ( transition.previousOwner == WorldInteractionOwner::None ||
                                                  transition.previousOwner == WorldInteractionOwner::InspectGizmo );

    const bool replayInteractionCleared =
        replayRuntime.ApplyInteractionExit( ReplayInteractionExitInput{ !enteringReplay,
                                                                        IsReplayWorldOwner( transition.previousOwner ),
                                                                        replayRestoreCameraMode,
                                                                        attachedCameraFollow,
                                                                        directorGrabbed },
                                            &cameras,
                                            terrain,
                                            camera,
                                            interaction,
                                            *this );
    if ( replayInteractionCleared )
    {
        ApplyPointerPresentation(
            EvaluateRuntimePointerPresentation( *this, runtimeTools.Editor(), replayRuntime.BuildInputView() ) );
    }

    if ( transition.previousOwner == WorldInteractionOwner::Manipulator &&
         transition.owner != WorldInteractionOwner::Manipulator )
    {
        runtimeTools.CancelMousePickup( *this, interaction );
    }
    if ( enteringTool && transition.owner != WorldInteractionOwner::Manipulator )
    {
        runtimeTools.CancelMousePickup( *this, interaction );
    }

    if ( ( !enteringEdit && !inspectGizmoClaimWithinInspect &&
           runtimeTools.HasActiveEditorInteractionState( interaction ) ) ||
         ( IsEditorWorldOwner( transition.previousOwner ) && !editorOwnerSwitchWithinEdit &&
           !inspectGizmoClaimWithinInspect ) )
    {
        runtimeTools.ClearEditorInteractionForTransition( enteringReplay || enteringTool,
                                                          models,
                                                          physics,
                                                          interaction );
        if ( ReleasePointerToUi(
                 EvaluateRuntimePointerPresentation( *this, runtimeTools.Editor(), replayRuntime.BuildInputView() ) ) )
        {
            InputController::ResetMouseLook( camera );
        }
        ApplyPointerPresentation(
            EvaluateRuntimePointerPresentation( *this, runtimeTools.Editor(), replayRuntime.BuildInputView() ) );
        if ( runtimeTools.Editor().editorModeEnabled && !enteringEdit )
        {
            runtimeTools.Editor().editorModeEnabled = false;
        }
    }
}


void InputRouter::ApplyInteractionTransition( const RuntimeInteractionTransition& transition,
                                              RuntimeFrameInteractionView& interactionOwners,
                                              RuntimeFrameSceneView& sceneOwners,
                                              ReplayRuntime& replayRuntime,
                                              RunCameraMode replayRestoreCameraMode )
{
    ApplyInteractionTransitionCleanup( transition,
                                       interactionOwners,
                                       sceneOwners,
                                       replayRuntime,
                                       replayRestoreCameraMode );
    RuntimeInteractionController& interaction = interactionOwners.interaction;
    switch ( transition.owner )
    {
    case WorldInteractionOwner::Launcher:
        interaction.EnterLauncher();
        break;
    case WorldInteractionOwner::Manipulator:
        interaction.EnterManipulator();
        break;
    default:
        if ( transition.workspace == RuntimeWorkspace::Edit )
        {
            interaction.EnterEdit();
        }
        else if ( transition.workspace == RuntimeWorkspace::Replay )
        {
            interaction.EnterReplay();
        }
        else if ( transition.workspace == RuntimeWorkspace::Inspect )
        {
            interaction.EnterInspect();
        }
        else
        {
            interaction.EnterLive();
        }
        break;
    }
}


RuntimeInteractionTransition InputRouter::SetWorldInteractionOwner( WorldInteractionOwner owner,
                                                                    InteractionExitReason reason,
                                                                    RuntimeFrameInteractionView& interactionOwners,
                                                                    RuntimeFrameSceneView& sceneOwners,
                                                                    ReplayRuntime& replayRuntime,
                                                                    RunCameraMode replayRestoreCameraMode )
{
    // Why: changing the logical owner can eject replay, editor, or camera gestures. InputRouter owns that
    // cleanup because it also reconciles the corresponding capture and cursor state.
    RuntimeInteractionController& interaction = interactionOwners.interaction;
    const RuntimeWorkspace workspace = interaction.WorkspaceForOwner( owner );
    const RuntimeInteractionTransition transition =
        interaction.SetWorldInteractionOwnerInWorkspace( workspace, owner, reason );
    ApplyInteractionTransitionCleanup( transition,
                                       interactionOwners,
                                       sceneOwners,
                                       replayRuntime,
                                       replayRestoreCameraMode );
    // Invariant: cleanup may temporarily select a neutral owner; the requested owner is the final state.
    interaction.SetWorldInteractionOwnerInWorkspace( workspace, owner, reason );
    return transition;
}

void InputRouter::RecordModeAction( RuntimeFrameInteractionView& interactionOwners,
                                    RuntimeInputContext& runtimeInput,
                                    RuntimeInputAction action,
                                    RuntimeInputActionSource source )
{
    const RunCameraState& camera = interactionOwners.camera;
    InputController::ApplyModeAction(
        runtimeInput,
        InputController::ResolveMode( BuildRuntimeInputModeState( camera.mode,
                                                                  interactionOwners.runtimeTools.Editor(),
                                                                  interactionOwners.interaction.Gesture(),
                                                                  interactionOwners.attachedCamera.State().activeFollow,
                                                                  camera.director.grabbed ) ),
        action,
        source );
}


RuntimePointerRouteResult InputRouter::RouteRuntimePointer( const RuntimePointerRouteInput& input,
                                                            RuntimeFrameHostView& host,
                                                            RuntimeFrameInteractionView& interactionOwners,
                                                            RuntimeFrameSceneView& sceneOwners,
                                                            ReplayRuntime& replayRuntime,
                                                            RunCameraMode replayRestoreCameraMode )
{
    RuntimeTools& runtimeTools = interactionOwners.runtimeTools;
    AttachedCameraController& attachedCamera = interactionOwners.attachedCamera;
    RuntimeInteractionController& interaction = interactionOwners.interaction;
    RunCameraState& camera = interactionOwners.camera;
    SceneController& models = sceneOwners.sceneController;
    SceneEntityStore& entities = models.Entities();
    PhysicsEngine& physics = models.Physics();
    RunSceneState& scene = models.State();
    Geometry::Terrain* terrain = models.Terrain().Get();
    Environment::CameraCollection& cameras = models.Cameras();
    const bool attachedCameraFollow = attachedCamera.State().activeFollow;
    const bool directorGrabbed = camera.director.grabbed;
    RuntimePointerRouteResult result;
    auto appendModeAction = [&result]( RuntimeInputAction action )
    {
        if ( result.modeActionCount >= result.modeActions.size() )
        {
            SB_FATAL( "Runtime/InputRouter", "Runtime pointer mode-action capacity exhausted." );
        }
        result.modeActions[result.modeActionCount++] = action;
    };
    if ( interaction.PointerCapture() == RuntimePointerCaptureOwner::CameraLook )
    {
        return result;
    }

    const EditorPointerRouteResult editorResult = RouteEditorPointer( { input.leftDown,
                                                                        input.leftPressed,
                                                                        input.leftReleased,
                                                                        input.suppressWorldAction,
                                                                        input.blocksCameraMouse,
                                                                        input.controlDown,
                                                                        input.hasClientPosition,
                                                                        input.hasWorldRay,
                                                                        input.replayInspectionActive,
                                                                        input.clientX,
                                                                        input.clientY,
                                                                        input.activeModelCapacity,
                                                                        input.cameraMode,
                                                                        input.rayOrigin,
                                                                        input.rayDirection },
                                                                      host,
                                                                      interactionOwners,
                                                                      sceneOwners );
    result.enteredInteractiveScene = editorResult.enteredInteractiveScene;
    if ( editorResult.hasInteractionTransition )
    {
        ApplyInteractionTransitionCleanup( editorResult.interactionTransition,
                                           interactionOwners,
                                           sceneOwners,
                                           replayRuntime,
                                           replayRestoreCameraMode );
        // Cleanup may temporarily select a camera/replay owner; the editor's
        // already-accepted claim is the final state for this pointer route.
        interaction.SetWorldInteractionOwnerInWorkspace( editorResult.interactionTransition.workspace,
                                                         editorResult.interactionTransition.owner,
                                                         editorResult.interactionTransition.reason );
    }
    for ( std::size_t replayEventIndex = 0; replayEventIndex < editorResult.replayEvents.count; ++replayEventIndex )
    {
        replayRuntime.SubmitEvent( editorResult.replayEvents.commands[replayEventIndex] );
    }
    for ( std::size_t actionIndex = 0; actionIndex < editorResult.modeActionCount; ++actionIndex )
    {
        appendModeAction( editorResult.modeActions[actionIndex] );
    }
    bool consumed = editorResult.consumed;

    if ( !consumed )
    {
        MousePickupPointerInput pickupInput;
        pickupInput.manipulatorMode = RunCameraModeIsManipulator( input.cameraMode );
        pickupInput.editorMode = runtimeTools.Editor().editorModeEnabled;
        pickupInput.replayInspection = input.replayInspectionActive;
        pickupInput.suppressWorldAction = input.suppressWorldAction;
        pickupInput.uiWantsNativeCursor = input.uiWantsNativeCursor;
        pickupInput.leftPressed = input.leftPressed;
        pickupInput.leftReleased = input.leftReleased;
        pickupInput.leftDown = input.leftDown;
        pickupInput.hasClientPosition = input.hasClientPosition;
        pickupInput.clientX = input.clientX;
        pickupInput.clientY = input.clientY;
        pickupInput.cameraEye = input.cameraEye;
        pickupInput.cameraView = input.cameraView;
        if ( pickupInput.manipulatorMode && !pickupInput.editorMode && !pickupInput.replayInspection &&
             ( interaction.Gesture().kind == RuntimeInteractionGestureKind::MousePickupDrag ||
               pickupInput.leftPressed ) )
        {
            pickupInput.hasWorldRay = input.hasWorldRay;
            pickupInput.hasClampedWorldRay = input.hasClampedWorldRay;
            pickupInput.rayOrigin = input.rayOrigin;
            pickupInput.rayDirection = input.rayDirection;
            pickupInput.clampedRayOrigin = input.clampedRayOrigin;
            pickupInput.clampedRayDirection = input.clampedRayDirection;
        }
        const MousePickupPointerResult pickupResult =
            runtimeTools.RouteMousePickupPointer( pickupInput, models, *this, interaction );
        result.enteredInteractiveScene |= pickupResult.enteredInteractive;
        consumed = pickupResult.consumed;
    }

    if ( !consumed && RunCameraModeIsAttached( input.cameraMode ) && input.leftPressed && !input.suppressWorldAction )
    {
        AttachedCameraTargetSelection selection;
        if ( attachedCamera
                 .PickTarget( models, cameras, input.hasWorldRay, input.rayOrigin, input.rayDirection, selection ) )
        {
            RuntimeInteractionCommand command;
            command.type = RuntimeInteractionCommandType::SetEditorSelection;
            command.body = selection.body;
            command.collider = selection.collider;
            command.selectionScope = RuntimeInteractionSelectionScope::Inspect;
            command.claimSelectionOwner = false;
            runtimeTools.ApplySelectionCommand( command, models );
            ApplyPointerPresentation(
                EvaluateRuntimePointerPresentation( *this, runtimeTools.Editor(), replayRuntime.BuildInputView() ) );
        }
        result.enteredInteractiveScene = true;
        appendModeAction( RuntimeInputAction::SetCameraMode );
        consumed = true;
    }

    if ( !consumed )
    {
        ReplayPathPickInput pickInput;
        pickInput.hasWorldRay = input.leftPressed && input.hasWorldRay;
        pickInput.rayOrigin = input.rayOrigin;
        pickInput.rayDirection = input.rayDirection;
        pickInput.additive = input.shiftDown;
        pickInput.clearOnMiss = !input.shiftDown;
        consumed =
            replayRuntime.RouteWorldPointer( ReplayWorldPointerInput{ input.leftPressed,
                                                                      input.suppressWorldAction,
                                                                      runtimeTools.Editor().editorModeEnabled,
                                                                      input.uiWantsNativeCursor,
                                                                      input.controlDown,
                                                                      RunCameraModeUsesLauncher( input.cameraMode ),
                                                                      pickInput,
                                                                      replayRestoreCameraMode,
                                                                      attachedCameraFollow,
                                                                      directorGrabbed },
                                             entities,
                                             models.BodyStore(),
                                             models.Colliders(),
                                             models.RenderPresentationRecords(),
                                             &cameras,
                                             terrain,
                                             camera,
                                             interaction,
                                             *this );
    }

    if ( !consumed )
    {
        const LauncherPointerResult launcherResult =
            runtimeTools.RouteLauncherPointer( { RunCameraModeUsesLauncher( input.cameraMode ),
                                                 input.leftPressed,
                                                 input.suppressWorldAction,
                                                 input.uiWantsNativeCursor,
                                                 input.activeModelCapacity },
                                               cameras,
                                               models,
                                               physics,
                                               scene,
                                               terrain );
        if ( launcherResult.recordReplayEvent )
        {
            replayRuntime.SubmitEvent( launcherResult.replayEvent );
        }
        if ( launcherResult.enteredInteractive )
        {
            result.enteredInteractiveScene = true;
            appendModeAction( RuntimeInputAction::FireLauncher );
        }
        consumed = launcherResult.consumed;
    }

    result.consumed = consumed;
    return result;
}


void InputRouter::ApplyCameraMode( RunCameraMode mode,
                                   RuntimeInputActionSource source,
                                   RuntimeFrameInteractionView& interactionOwners,
                                   RuntimeFrameSceneView& sceneOwners,
                                   ReplayRuntime& replayRuntime,
                                   RuntimeInputContext& runtimeInput )
{
    // Lifetime: all domain owners are synchronous borrows for one semantic
    // mode request. InputRouter retains only its own edge/presentation state.
    // Invariant: interaction cleanup precedes camera/editor mutation, then
    // pointer and RuntimeInputContext presentation publish the completed mode.
    InputRouter& m_inputRouter = *this;
    RunCameraState& m_camera = interactionOwners.camera;
    RuntimeInteractionController& m_interaction = interactionOwners.interaction;
    RuntimeTools& m_runtimeTools = interactionOwners.runtimeTools;
    ReplayRuntime& m_replayRuntime = replayRuntime;
    AttachedCameraController& m_attachedCamera = interactionOwners.attachedCamera;
    SceneController& m_sceneController = sceneOwners.sceneController;
    const bool authoredScene = m_sceneController.State().isSceneMode;
    const uint32_t enabledMask = RuntimeCameraModeEnabledMask( m_sceneController );
    const int modeIndex = static_cast<int>( mode );
    if ( modeIndex < 0 || modeIndex >= static_cast<int>( RunCameraMode::Count ) )
    {
        return;
    }
    const RunCameraMode previousMode = NormalizeRuntimeCameraMode( m_camera.mode, authoredScene, enabledMask );
    mode = NormalizeRuntimeCameraMode( mode, authoredScene, enabledMask );
    const bool enteringAttach = mode == RunCameraMode::Attach && previousMode != RunCameraMode::Attach;
    const bool leavingAttach = previousMode == RunCameraMode::Attach && mode != RunCameraMode::Attach;
    if ( enteringAttach )
    {
        m_attachedCamera.CaptureReturnState( previousMode, m_sceneController.Cameras() );
    }

    if ( mode == RunCameraMode::Demo )
    {
        const int modelCount = m_sceneController.SceneEntityCount();
        if ( !m_camera.trackBallRow.IsValid() || m_camera.trackBallRow.value >= modelCount )
        {
            m_camera.trackBallRow.value = 0;
        }
        if ( m_camera.trackHeight <= 0.0f )
        {
            m_camera.trackHeight = 300.0f;
        }
    }
    if ( mode == RunCameraMode::Director && previousMode != RunCameraMode::Director )
    {
        DemoDirectorPlayback::EnterMode( m_camera, m_sceneController.Cameras() );
    }

    const RuntimeInteractionTransition transition = m_interaction.EnterCameraMode( mode );
    m_inputRouter.ApplyInteractionTransition(
        transition,
        interactionOwners,
        sceneOwners,
        m_replayRuntime,
        NormalizeRuntimeCameraMode( m_replayRuntime.BuildInputView().restoreCameraMode, authoredScene, enabledMask ) );

    const bool wasFlyMode =
        RunCameraModeUsesFlyControls( m_camera.mode, m_attachedCamera.State().activeFollow, m_camera.director.grabbed );
    if ( mode != RunCameraMode::Launcher )
    {
        m_camera.modeBeforeLauncher = mode == RunCameraMode::Manipulator ? RunCameraMode::Inspect : mode;
    }
    m_camera.mode = mode;
    if ( leavingAttach )
    {
        m_attachedCamera.RestoreReturnState( m_sceneController.Cameras() );
    }
    if ( mode == RunCameraMode::Attach )
    {
        m_attachedCamera.State().activeFollow = true;
        m_attachedCamera.State().needsEntryTween = true;
    }
    if ( m_runtimeTools.Editor().editorModeEnabled )
    {
        m_runtimeTools.Editor().restoreCameraModeAfterEditor = mode;
    }
    if ( mode != RunCameraMode::Manipulator )
    {
        m_runtimeTools.CancelMousePickup( m_inputRouter, m_interaction );
    }

    const bool isFlyMode =
        RunCameraModeUsesFlyControls( m_camera.mode, m_attachedCamera.State().activeFollow, m_camera.director.grabbed );
    if ( wasFlyMode != isFlyMode )
    {
        if ( isFlyMode )
        {
            EnterFlyModeCamera( m_inputRouter,
                                m_camera,
                                m_sceneController.Cameras(),
                                authoredScene,
                                m_runtimeTools.Editor(),
                                m_replayRuntime.BuildInputView() );
        }
        else
        {
            ExitFlyModeCamera( m_inputRouter,
                               m_camera,
                               m_sceneController.Cameras(),
                               *m_sceneController.Terrain().Get(),
                               authoredScene );
        }
    }
    else
    {
        InputController::ResetMouseLook( m_camera );
        m_inputRouter.ApplyPointerPresentation(
            EvaluateRuntimePointerPresentation( m_inputRouter,
                                                m_runtimeTools.Editor(),
                                                m_replayRuntime.BuildInputView() ) );
    }
    if ( mode == RunCameraMode::Attach )
    {
        int seedIndex = -1;
        const PhysicsBodyStore& bodyStore = m_sceneController.BodyStore();
        const int modelCount = bodyStore.Count();
        const int replayTargetRow = m_replayRuntime.BuildInputView().pathTargetModelRow;
        if ( replayTargetRow >= 0 && replayTargetRow < modelCount )
        {
            seedIndex = replayTargetRow;
        }
        else
        {
            const int selectedModelIndex = ResolveSelectedEditorModelIndex( m_runtimeTools.Editor(), bodyStore );
            if ( selectedModelIndex >= 0 && selectedModelIndex < modelCount )
            {
                seedIndex = selectedModelIndex;
            }
        }

        AttachedCameraTargetSelection selection;
        const AttachedCameraSeedResult seedResult =
            m_attachedCamera.SeedTarget( m_sceneController, m_sceneController.Cameras(), seedIndex, selection );
        if ( seedResult == AttachedCameraSeedResult::SelectedSeed )
        {
            RuntimeInteractionCommand command;
            command.type = RuntimeInteractionCommandType::SetEditorSelection;
            command.body = selection.body;
            command.collider = selection.collider;
            command.selectionScope = RuntimeInteractionSelectionScope::Inspect;
            command.claimSelectionOwner = false;
            m_runtimeTools.ApplySelectionCommand( command, m_sceneController );
        }
        if ( seedResult != AttachedCameraSeedResult::Failed )
        {
            m_inputRouter.ApplyPointerPresentation(
                EvaluateRuntimePointerPresentation( m_inputRouter,
                                                    m_runtimeTools.Editor(),
                                                    m_replayRuntime.BuildInputView() ) );
        }
    }
    InputController::ApplyModeAction(
        runtimeInput,
        InputController::ResolveMode( BuildRuntimeInputModeState( m_camera.mode,
                                                                  m_runtimeTools.Editor(),
                                                                  m_interaction.Gesture(),
                                                                  m_attachedCamera.State().activeFollow,
                                                                  m_camera.director.grabbed ) ),
        source == RuntimeInputActionSource::UI ? RuntimeInputAction::SetCameraMode
                                               : RuntimeInputAction::CycleCameraMode,
        source );
}


void InputRouter::CycleCameraMode( RuntimeFrameInteractionView& interactionOwners,
                                   RuntimeFrameSceneView& sceneOwners,
                                   ReplayRuntime& replayRuntime,
                                   RuntimeInputContext& runtimeInput )
{
    RunCameraState& camera = interactionOwners.camera;
    AttachedCameraController& attachedCamera = interactionOwners.attachedCamera;
    SceneController& sceneController = sceneOwners.sceneController;
    const bool authoredScene = sceneController.State().isSceneMode;
    const uint32_t enabledMask = RuntimeCameraModeEnabledMask( sceneController );
    int current = static_cast<int>( camera.mode );
    if ( current < 0 || current >= static_cast<int>( RunCameraMode::Count ) )
    {
        current = static_cast<int>( authoredScene ? RunCameraMode::Scene : RunCameraMode::Demo );
    }

    if ( NormalizeRuntimeCameraMode( camera.mode, authoredScene, enabledMask ) == RunCameraMode::Attach )
    {
        const RunCameraMode restoreMode =
            NormalizeRuntimeCameraMode( attachedCamera.State().returnMode, authoredScene, enabledMask );
        const int restoreIndex = static_cast<int>( restoreMode );
        // Why: Attach is a temporary follow workspace. Keyboard cycling out of
        // it should return to the camera mode that entered Attach, not continue
        // to the next enum value and strand the operator at the follow pose.
        if ( restoreIndex >= 0 && restoreIndex < static_cast<int>( RunCameraMode::Count ) &&
             ( enabledMask & ( 1u << restoreIndex ) ) != 0 )
        {
            ApplyCameraMode( restoreMode,
                             RuntimeInputActionSource::Keyboard,
                             interactionOwners,
                             sceneOwners,
                             replayRuntime,
                             runtimeInput );
            return;
        }
    }

    for ( int step = 1; step <= static_cast<int>( RunCameraMode::Count ); ++step )
    {
        const int next = ( current + step ) % static_cast<int>( RunCameraMode::Count );
        if ( ( enabledMask & ( 1u << next ) ) != 0 )
        {
            ApplyCameraMode( static_cast<RunCameraMode>( next ),
                             RuntimeInputActionSource::Keyboard,
                             interactionOwners,
                             sceneOwners,
                             replayRuntime,
                             runtimeInput );
            return;
        }
    }
}


bool InputRouter::HandleUnfocusedFrame( RuntimeFrameInteractionView& interactionOwners,
                                        RuntimeFrameSceneView& sceneOwners,
                                        ReplayRuntime& replayRuntime,
                                        RuntimeInputContext& runtimeInput )
{
    RuntimeInteractionController& interaction = interactionOwners.interaction;
    RuntimeTools& runtimeTools = interactionOwners.runtimeTools;
    AttachedCameraController& attachedCamera = interactionOwners.attachedCamera;
    RunCameraState& camera = interactionOwners.camera;
    SceneController& sceneController = sceneOwners.sceneController;
    UI::InGameUI& ui = interactionOwners.operatorUi;
    if ( AppFocused() )
    {
        return false;
    }

    // Invariant: focus loss releases every active tool capture and refreshes
    // action memory so refocus cannot replay stale drag/key edges.
    interaction.CancelCameraLookGesture();
    replayRuntime.ApplyInputFocusLoss( &sceneController.Cameras(),
                                       sceneController.Terrain().Get(),
                                       camera,
                                       NormalizeRuntimeCameraMode( replayRuntime.BuildInputView().restoreCameraMode,
                                                                   sceneController.State().isSceneMode,
                                                                   RuntimeCameraModeEnabledMask( sceneController ) ),
                                       attachedCamera.State().activeFollow,
                                       camera.director.grabbed,
                                       interaction,
                                       *this );
    CancelPointerPresentation();
    runtimeTools.CancelMousePickup( *this, interaction );
    RunInternal::ResetEditorUnfocusedInputState(
        { runtimeTools.Editor(), sceneController, sceneController.Physics(), interaction } );
    InputController::ResetUnfocusedInput( camera );
    InputController::BeginFrame( runtimeInput,
                                 BuildRuntimeInputModeState( camera.mode,
                                                             runtimeTools.Editor(),
                                                             interaction.Gesture(),
                                                             attachedCamera.State().activeFollow,
                                                             camera.director.grabbed ),
                                 false,
                                 true,
                                 true );
    ui.CancelInputCapture();
    return true;
}


void InputRouter::DispatchCaptureActions( InputActions& actions,
                                          RuntimeFrameHostView& host,
                                          RuntimeFrameInteractionView& interactionOwners,
                                          RuntimeFrameSceneView& sceneOwners,
                                          const ReplayInputView& replayInput )
{
    const RunCameraState& camera = interactionOwners.camera;
    const AttachedCameraController& attachedCamera = interactionOwners.attachedCamera;
    SceneController& sceneController = sceneOwners.sceneController;
    DiagnosticsRuntime& diagnosticsRuntime = host.diagnosticsRuntime;
    const UI::InGameUI& ui = interactionOwners.operatorUi;
    // Why: capture/reset shortcuts run after UI input so focused controls and
    // panels get first refusal on keyboard ownership.
    const bool flyCamera =
        RunCameraModeUsesFlyControls( camera.mode, attachedCamera.State().activeFollow, camera.director.grabbed );
    const KeyboardContextFacts contextFacts{ !ui.BlocksKeyboard(),
                                             sceneController.State().isSceneMode,
                                             flyCamera,
                                             RunCameraModeUsesLauncher( camera.mode ),
                                             RunCameraModeIsAttached( camera.mode ),
                                             camera.mode == RunCameraMode::Director,
                                             camera.mode == RunCameraMode::Director || flyCamera,
                                             false,
                                             !replayInput.restoreConsumedThisFrame,
                                             false };
    const RuntimeInputKeyBindingView bindings = TakeInputKeyboardBindings();
    RoutePhase( bindings, InputActionPhase::Capture, BuildKeyboardContextMask( contextFacts ), actions );
    if ( actions.Overflowed() )
    {
        SB_FATAL( "InputRouter", "Fixed input action capacity exhausted while routing capture actions." );
    }

    const RunInternal::EditorSaveHotkeyContext editorSaveHotkeyContext{ sceneController,
                                                                        sceneController.Entities(),
                                                                        sceneController.State(),
                                                                        sceneController.World(),
                                                                        sceneController.Cameras(),
                                                                        diagnosticsRuntime.Capture() };
    // Invariant: side-effect dispatch consumes only accepted semantic events.
    // It must not reopen hardware polling or maintain a second edge latch.
    for ( std::size_t index = 0; index < actions.Count(); ++index )
    {
        const InputActionEvent& event = actions[index];
        if ( event.edge != InputActionEdge::Pressed )
        {
            continue;
        }

        switch ( event.action )
        {
        case RuntimeInputAction::SaveSceneSnapshot:
        case RuntimeInputAction::SaveScreenshot:
            RunInternal::HandleEditorSaveHotkey( editorSaveHotkeyContext, event.action, true );
            break;
        case RuntimeInputAction::ResetScene:
            // R reloads after capture actions have had their persistence slot.
            sceneController.SubmitResetCurrentScene();
            break;
        case RuntimeInputAction::ResetSceneFromBackspace:
            if ( sceneController.State().isSceneMode )
            {
                // Backspace is only a scene-mode reset alias; generated demos keep
                // the key free for future non-scene tools.
                sceneController.SubmitResetCurrentScene();
            }
            break;
        default:
            break;
        }
    }
}


bool InputRouter::DispatchAfterUiDismiss( InputActions& actions,
                                          const RuntimeAfterUiDismissInput& input,
                                          RuntimeFrameHostView& host,
                                          RuntimeFrameInteractionView& interactionOwners,
                                          RuntimeFrameSceneView& sceneOwners,
                                          const ReplayInputView& replayInput )
{
    const bool uiUserInteracted = input.uiUserInteracted;
    const double nowSeconds = input.nowSeconds;
    RunCameraState& camera = interactionOwners.camera;
    AttachedCameraController& attachedCamera = interactionOwners.attachedCamera;
    RuntimeTools& runtimeTools = interactionOwners.runtimeTools;
    SceneController& sceneController = sceneOwners.sceneController;
    DiagnosticsRuntime& diagnosticsRuntime = host.diagnosticsRuntime;
    RuntimeOverlayPresentationEdit presentationEdit = sceneOwners.overlays.EditPresentation();
    RunDebugState& debug = presentationEdit.State();
    UI::InGameUI& ui = interactionOwners.operatorUi;
    const bool flyCamera =
        RunCameraModeUsesFlyControls( camera.mode, attachedCamera.State().activeFollow, camera.director.grabbed );
    const KeyboardContextFacts contextFacts{ !ui.BlocksKeyboard(),
                                             sceneController.State().isSceneMode,
                                             flyCamera,
                                             RunCameraModeUsesLauncher( camera.mode ),
                                             RunCameraModeIsAttached( camera.mode ),
                                             camera.mode == RunCameraMode::Director,
                                             camera.mode == RunCameraMode::Director || flyCamera,
                                             runtimeTools.Editor().editorModeEnabled,
                                             !replayInput.restoreConsumedThisFrame,
                                             !uiUserInteracted };
    const RuntimeInputKeyBindingView bindings = TakeInputKeyboardBindings();
    RoutePhase( bindings, InputActionPhase::AfterUi, BuildKeyboardContextMask( contextFacts ), actions );
    if ( actions.Overflowed() )
    {
        SB_FATAL( "InputRouter", "Fixed input action capacity exhausted while routing after-UI actions." );
    }

    for ( std::size_t index = 0; index < actions.Count(); ++index )
    {
        const InputActionEvent& event = actions[index];
        if ( event.phase != InputActionPhase::AfterUi || event.action != RuntimeInputAction::DismissOrExitUI ||
             event.edge != InputActionEdge::Pressed )
        {
            continue;
        }

        // ESC is intentionally after UI processing: focused controls keep
        // local ESC behavior before the diagnostics window reacts.
        constexpr double ESC_QUICK_EXIT_SECONDS = 0.32;
        if ( IsQuickRepeat( event.action, nowSeconds, ESC_QUICK_EXIT_SECONDS ) )
        {
            return true;
        }
        else
        {
            sceneController.State().isInteractiveRun = true;
            sceneController.State().isExitOnComplete = false;
            diagnosticsRuntime.Capture().Screenshot().isScreenshotAndExit = false;
            ui.ToggleVisible( nowSeconds );
            debug.overlayMode = OverlayMode::None;
            RecordTap( event.action, nowSeconds );
            ApplyPointerPresentation( EvaluateRuntimePointerPresentation( *this, runtimeTools.Editor(), replayInput ) );
            if ( ReleasePointerToUi( EvaluateRuntimePointerPresentation( *this, runtimeTools.Editor(), replayInput ) ) )
            {
                InputController::ResetMouseLook( camera );
            }
        }
    }
    return false;
}
