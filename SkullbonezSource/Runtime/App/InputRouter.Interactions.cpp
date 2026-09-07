/*
File: SkullbonezSource/Runtime/App/InputRouter.Interactions.cpp
Purpose:
  Implements InputRouter transition, pointer, camera-mode, and focus behavior.

Summary:
  InputRouter consumes already-sampled semantic and pointer values, sequences
  cleanup through concrete interaction/tool/replay owners, and publishes the
  resulting input-mode and pointer-presentation state.

Glossary:
  World ray: Camera-projected pointer direction sampled once before editor,
    pickup, camera, Replay, or launcher owners can mutate scene state.

Invariants:
  - Exactly one interaction owner receives a world gesture.
  - World-click precedence is editor, mouse pickup, attached camera, Replay,
    then launcher; a consumed gesture never reaches a later owner.
  - Focus loss cancels active gestures before releasing native capture.
  - InputRouter retains input policy state but never retains borrowed domain
    owners passed to routing operations.

Related:
  - InputRouter.h declares the retained input owner.
  - InputFrameExecution.cpp owns stateless per-frame composition.
  - Agentic/Reference/runtime-reference.md
  - Agentic/Reference/engine-glossary.md
*/
#include "Run.h"
#include "../Diagnostics/RuntimeOverlayDiagnostics.h"
#include "InputFrame.h"
#include "../Scene/AttachedCameraController.h"
#include "../Editor/EditorTools.h"
#include "../Input/InputController.Bindings.h"
#include "../Input/InputController.h"
#include "../Replay/ReplayRestoreTransactions.h"
#include "../Direction/DemoDirectorPlayback.h"
#include "../Interaction/RuntimeInteractionCommands.h"
#include "../Interaction/RuntimePickService.h"
#include "../Scene/SceneGeneratedControlTransaction.h"
#include "../Scene/SceneCinematicPolicy.h"
#include "../../Core/Log.h"
#include "../../Maths/GeometricStructures.h"
#include "../../Physics/ColliderStore.h"
#include "../../Physics/PhysicsBodyStore.h"
#include "../UI/GameUI/UI.h"
#include "../../UI/UILayout.h"

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
using SkullbonezCore::Math::Vector::Vector3;

namespace
{
DemoCameraPose CaptureDemoDirectorPose( const SkullbonezCore::Environment::CameraCollection& cameras )
{
    DemoCameraPose pose;
    pose.eye = cameras.GetCameraTranslation();
    pose.view = cameras.GetCameraView();
    pose.up = cameras.GetCameraUp();
    return pose;
}

RuntimeInputAction ResolveEditorPointerModeAction( EditorPointerModeAction action )
{
    switch ( action )
    {
    case EditorPointerModeAction::EndPlacementScale:
        return RuntimeInputAction::EndEditorPlacementScale;
    case EditorPointerModeAction::EndGizmoDrag:
        return RuntimeInputAction::EndEditorGizmoDrag;
    case EditorPointerModeAction::BeginGizmoScale:
        return RuntimeInputAction::BeginEditorGizmoScale;
    case EditorPointerModeAction::BeginGizmoRotate:
        return RuntimeInputAction::BeginEditorGizmoRotate;
    case EditorPointerModeAction::BeginGizmoTranslate:
        return RuntimeInputAction::BeginEditorGizmoTranslate;
    case EditorPointerModeAction::BeginPlacementScale:
        return RuntimeInputAction::BeginEditorPlacementScale;
    }

    SB_FATAL( "Runtime/InputRouter", "Editor pointer emitted an unknown mode action." );
}
} // namespace


void InputRouter::ApplyInteractionTransitionCleanup( const RuntimeInteractionTransition& transition,
                                                     EditorToolsOwner& editorTools, RuntimeTools& runtimeTools,
                                                     RuntimeInteractionController& interaction,
                                                     AttachedCameraController& attachedCamera, CameraControlState& camera,
                                                     SceneController& models, ReplayRuntime& replayRuntime,
                                                     RunCameraMode replayRestoreCameraMode )
{
    Environment::CameraCollection& cameras = models.Scene().Cameras();
    Geometry::Terrain* terrain = models.Scene().Terrain().Get();
    const bool attachedCameraFollow = attachedCamera.State().activeFollow;
    const bool directorGrabbed = camera.director.grabbed;
    const bool enteringReplay = transition.workspace == RuntimeWorkspace::Replay;
    const bool enteringEdit = transition.workspace == RuntimeWorkspace::Edit;
    const bool enteringTool = transition.owner == WorldInteractionOwner::Launcher ||
                              transition.owner == WorldInteractionOwner::Manipulator;

    const bool editorOwnerSwitchWithinEdit = enteringEdit && IsEditorWorldOwner( transition.previousOwner ) &&
                                             IsEditorWorldOwner( transition.owner );

    const bool inspectGizmoClaimWithinInspect = transition.workspace == RuntimeWorkspace::Inspect &&
                                                transition.owner == WorldInteractionOwner::InspectGizmo &&
                                                ( transition.previousOwner == WorldInteractionOwner::None ||
                                                  transition.previousOwner == WorldInteractionOwner::InspectGizmo );

    const bool
        replayInteractionCleared = replayRuntime
                                       .ApplyInteractionExit( ReplayInteractionExitInput { !enteringReplay,
                                                                                           IsReplayWorldOwner(
                                                                                               transition.previousOwner ),
                                                                                           replayRestoreCameraMode,
                                                                                           attachedCameraFollow,
                                                                                           directorGrabbed },
                                                              models.Scene().Physics(), &cameras, terrain, camera,
                                                              interaction, *this );

    if ( replayInteractionCleared )
    {
        ApplyPointerPresentation(
            EvaluateRuntimePointerPresentation( *this, editorTools.Editor(), replayRuntime.BuildInputView() ) );
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
           editorTools.HasActiveEditorInteractionState( interaction ) ) ||
         ( IsEditorWorldOwner( transition.previousOwner ) && !editorOwnerSwitchWithinEdit &&
           !inspectGizmoClaimWithinInspect ) )
    {
        editorTools.ClearEditorInteractionForTransition( enteringReplay || enteringTool, models.Scene(), interaction );

        if ( ReleasePointerToUi(
                 EvaluateRuntimePointerPresentation( *this, editorTools.Editor(), replayRuntime.BuildInputView() ) ) )
        {
            InputController::ResetMouseLook( camera );
        }

        ApplyPointerPresentation(
            EvaluateRuntimePointerPresentation( *this, editorTools.Editor(), replayRuntime.BuildInputView() ) );

        if ( editorTools.Editor().editorModeEnabled && !enteringEdit )
        {
            editorTools.Editor().editorModeEnabled = false;
        }
    }
}


void InputRouter::ApplyInteractionTransition( const RuntimeInteractionTransition& transition, EditorToolsOwner& editorTools,
                                              RuntimeTools& runtimeTools, RuntimeInteractionController& interaction,
                                              AttachedCameraController& attachedCamera, CameraControlState& camera,
                                              SceneController& sceneController, ReplayRuntime& replayRuntime,
                                              RunCameraMode replayRestoreCameraMode )
{
    ApplyInteractionTransitionCleanup( transition, editorTools, runtimeTools, interaction, attachedCamera, camera,
                                       sceneController, replayRuntime, replayRestoreCameraMode );

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


RuntimeInteractionTransition InputRouter::SetWorldInteractionOwner(
    WorldInteractionOwner owner, InteractionExitReason reason, EditorToolsOwner& editorTools, RuntimeTools& runtimeTools,
    RuntimeInteractionController& interaction, AttachedCameraController& attachedCamera, CameraControlState& camera,
    SceneController& sceneController, ReplayRuntime& replayRuntime, RunCameraMode replayRestoreCameraMode )
{
    // Why: changing the logical owner can eject replay, editor, or camera gestures. InputRouter owns that
    // cleanup because it also reconciles the corresponding capture and cursor state.
    const RuntimeWorkspace workspace = interaction.WorkspaceForOwner( owner );
    const RuntimeInteractionTransition transition = interaction.SetWorldInteractionOwnerInWorkspace( workspace, owner,
                                                                                                     reason );

    ApplyInteractionTransitionCleanup( transition, editorTools, runtimeTools, interaction, attachedCamera, camera,
                                       sceneController, replayRuntime, replayRestoreCameraMode );

    // Invariant: cleanup may temporarily select a neutral owner; the requested owner is the final state.
    interaction.SetWorldInteractionOwnerInWorkspace( workspace, owner, reason );
    return transition;
}

void InputRouter::RecordModeAction( const CameraControlState& camera, const EditorToolsOwner& editorTools,
                                    const RuntimeInteractionController& interaction,
                                    const AttachedCameraController& attachedCamera, RuntimeInputContext& runtimeInput,
                                    RuntimeInputAction action, RuntimeInputActionSource source )
{
    InputController::ApplyModeAction( runtimeInput,
                                      InputController::ResolveMode(
                                          BuildRuntimeInputModeState( camera.mode, editorTools.Editor(),
                                                                      interaction.Gesture(),
                                                                      attachedCamera.State().activeFollow,
                                                                      camera.director.grabbed ) ),
                                      action, source );
}


RuntimePointerRouteResult Run::RouteRuntimePointer( const RuntimePointerEvent& pointer, bool replayInspectionActive,
                                                    int activeModelCapacity, RunCameraMode replayRestoreCameraMode )
{
    InputRouter& inputRouter = m_inputRouter;
    const RunCameraMode cameraMode = m_camera.mode;
    SceneEntityStore& entities = m_sceneController.Scene().Entities();
    SceneSessionState& scene = m_sceneController.State();
    Geometry::Terrain* terrain = m_sceneController.Scene().Terrain().Get();
    Environment::CameraCollection& cameras = m_sceneController.Scene().Cameras();
    const bool attachedCameraFollow = m_attachedCamera.State().activeFollow;
    const bool directorGrabbed = m_camera.director.grabbed;
    Vector3 rayOrigin = SkullbonezCore::Math::Vector::ZERO_VECTOR;
    Vector3 rayDirection = SkullbonezCore::Math::Vector::ZERO_VECTOR;
    Vector3 clampedRayOrigin = SkullbonezCore::Math::Vector::ZERO_VECTOR;
    Vector3 clampedRayDirection = SkullbonezCore::Math::Vector::ZERO_VECTOR;
    const bool hasWorldRay = inputRouter.TryBuildWorldRay( cameras, m_window, rayOrigin, rayDirection );
    const bool hasClampedWorldRay = inputRouter.TryBuildWorldRay( cameras, m_window, clampedRayOrigin, clampedRayDirection,
                                                                  true );
    const Vector3 cameraEye = cameras.GetCameraTranslation();
    const Vector3 cameraView = cameras.GetCameraView();
    RuntimePointerRouteResult result;
    auto appendModeAction = [&result]( RuntimeInputAction action )
    {
        if ( result.modeActionCount >= result.modeActions.size() )
        {
            SB_FATAL( "Runtime/InputRouter", "Runtime pointer mode-action capacity exhausted." );
        }

        result.modeActions[result.modeActionCount++] = action;
    };

    if ( m_interaction.PointerCapture() == RuntimePointerCaptureOwner::CameraLook )
    {
        return result;
    }

    RuntimePointerArbitration arbitration;

    // Invariant: this order is the world-pointer arbitration contract. Each
    // concrete owner sees the immutable pointer/ray values only when every
    // earlier owner declined the gesture. The phase cursor fatal-invariant fails a
    // reordered, skipped, or repeated stage.
    (void)arbitration.BeginStage( RuntimePointerRouteStage::Editor );
    const EditorPointerRouteResult editorResult = inputRouter.RouteEditorPointer( pointer, hasWorldRay, rayOrigin,
                                                                                  rayDirection, cameraMode,
                                                                                  replayInspectionActive,
                                                                                  activeModelCapacity, m_assets,
                                                                                  m_editorTools, m_interaction,
                                                                                  m_sceneController );

    result.enteredInteractiveScene = editorResult.enteredInteractiveScene;

    if ( editorResult.hasInteractionTransition )
    {
        inputRouter.ApplyInteractionTransitionCleanup( editorResult.interactionTransition, m_editorTools, m_runtimeTools,
                                                       m_interaction, m_attachedCamera, m_camera, m_sceneController,
                                                       m_replayRuntime, replayRestoreCameraMode );

        // Cleanup may temporarily select a camera/replay owner; the editor's
        // already-accepted claim is the final state for this pointer route.
        m_interaction.SetWorldInteractionOwnerInWorkspace( editorResult.interactionTransition.workspace,
                                                           editorResult.interactionTransition.owner,
                                                           editorResult.interactionTransition.reason );
    }

    for ( std::size_t replayEventIndex = 0; replayEventIndex < editorResult.replayEvents.count; ++replayEventIndex )
    {
        m_replayRuntime.SubmitEvent( editorResult.replayEvents.commands[replayEventIndex] );
    }

    for ( std::size_t actionIndex = 0; actionIndex < editorResult.modeActionCount; ++actionIndex )
    {
        appendModeAction( ResolveEditorPointerModeAction( editorResult.modeActions[actionIndex] ) );
    }

    arbitration.FinishStage( RuntimePointerRouteStage::Editor, editorResult.consumed );

    bool pickupConsumed = false;

    if ( arbitration.BeginStage( RuntimePointerRouteStage::MousePickup ) )
    {
        const bool pickupModeActive = RunCameraModeIsManipulator( cameraMode ) &&
                                      !m_editorTools.Editor().editorModeEnabled && !replayInspectionActive;

        if ( !pickupModeActive )
        {
            m_runtimeTools.CancelMousePickup( inputRouter, m_interaction );
        }
        else if ( m_interaction.Gesture().kind == RuntimeInteractionGestureKind::MousePickupDrag || pointer.leftPressed )
        {
            const MousePickupPointerResult
                pickupResult = m_runtimeTools.RouteMousePickupPointer( pointer, hasWorldRay,
                                                                       Geometry::Ray( rayOrigin, rayDirection ),
                                                                       hasClampedWorldRay,
                                                                       Geometry::Ray( clampedRayOrigin,
                                                                                      clampedRayDirection ),
                                                                       cameraEye, cameraView, m_sceneController.Scene(),
                                                                       inputRouter, m_interaction );

            result.enteredInteractiveScene |= pickupResult.enteredInteractive;
            pickupConsumed = pickupResult.consumed;
        }
    }

    arbitration.FinishStage( RuntimePointerRouteStage::MousePickup, pickupConsumed );

    bool cameraConsumed = false;

    if ( arbitration.BeginStage( RuntimePointerRouteStage::AttachedCamera ) && RunCameraModeIsAttached( cameraMode ) &&
         !IsReplayWorldOwner( m_interaction.Owner() ) && pointer.leftPressed && !pointer.suppressWorldAction )
    {
        AttachedCameraTargetSelection selection;
        RuntimePickResult pick;
        RuntimePickRequest request;
        request.purpose = RuntimePickPurpose::AttachCameraTarget;
        request.bodyStore = &m_sceneController.Scene().BodyStore();
        request.colliderStore = &m_sceneController.Scene().Colliders();
        request.rayOrigin = rayOrigin;
        request.rayDirection = rayDirection;

        if ( hasWorldRay && RuntimePickService::TryPickModel( request, pick ) &&
             m_attachedCamera.SetTarget( m_sceneController.Scene(), pick.modelRow.value, selection ) )
        {
            RuntimeInteractionCommand command;
            command.type = RuntimeInteractionCommandType::SetEditorSelection;
            command.body = selection.body;
            command.collider = selection.collider;
            command.selectionScope = RuntimeInteractionSelectionScope::Inspect;
            command.claimSelectionOwner = false;
            m_editorTools.ApplySelectionCommand( command, m_sceneController.Scene() );
            inputRouter.ApplyPointerPresentation( EvaluateRuntimePointerPresentation( inputRouter, m_editorTools.Editor(),
                                                                                      m_replayRuntime.BuildInputView() ) );
        }
        else
        {
            AttachedCameraController::ClearTarget( m_attachedCamera.State() );
        }

        result.enteredInteractiveScene = true;
        appendModeAction( RuntimeInputAction::SetCameraMode );
        cameraConsumed = true;
    }

    arbitration.FinishStage( RuntimePointerRouteStage::AttachedCamera, cameraConsumed );

    bool replayConsumed = false;

    if ( arbitration.BeginStage( RuntimePointerRouteStage::Replay ) )
    {
        ReplayPathPickInput pickInput;
        pickInput.hasWorldRay = pointer.leftPressed && hasWorldRay;
        pickInput.rayOrigin = rayOrigin;
        pickInput.rayDirection = rayDirection;
        pickInput.additive = pointer.shiftDown;
        pickInput.clearOnMiss = !pointer.shiftDown;
        replayConsumed = m_replayRuntime
                             .RouteWorldPointer( ReplayWorldPointerInput { pointer.leftPressed, pointer.suppressWorldAction,
                                                                           m_editorTools.Editor().editorModeEnabled,
                                                                           pointer.controlDown,
                                                                           RunCameraModeUsesLauncher( cameraMode ),
                                                                           pickInput, replayRestoreCameraMode,
                                                                           attachedCameraFollow, directorGrabbed },
                                                 entities, m_sceneController.Scene().BodyStore(),
                                                 m_sceneController.Scene().Colliders(),
                                                 m_sceneController.Scene().RenderPresentationRecords(), &cameras, terrain,
                                                 m_camera, m_interaction, inputRouter );
    }

    arbitration.FinishStage( RuntimePointerRouteStage::Replay, replayConsumed );

    bool launcherConsumed = false;

    if ( arbitration.BeginStage( RuntimePointerRouteStage::Launcher ) )
    {
        const LauncherPointerResult
            launcherResult = m_runtimeTools.RouteLauncherPointer( { RunCameraModeUsesLauncher( cameraMode ),
                                                                    pointer.leftPressed, pointer.suppressWorldAction,
                                                                    pointer.uiWantsNativeMouseCursor, activeModelCapacity },
                                                                  m_sceneController.Scene(), scene );

        if ( launcherResult.recordReplayEvent )
        {
            m_replayRuntime.SubmitEvent( launcherResult.replayEvent );
        }

        if ( launcherResult.enteredInteractive )
        {
            result.enteredInteractiveScene = true;
            appendModeAction( RuntimeInputAction::FireLauncher );
        }

        launcherConsumed = launcherResult.consumed;
    }

    arbitration.FinishStage( RuntimePointerRouteStage::Launcher, launcherConsumed );

    result.consumed = arbitration.Consumed();
    return result;
}


void InputRouter::ApplyCameraMode( RunCameraMode mode, RuntimeInputActionSource source, EditorToolsOwner& editorTools,
                                   RuntimeTools& runtimeTools, RuntimeInteractionController& interaction,
                                   AttachedCameraController& attachedCamera, CameraControlState& camera,
                                   SceneController& sceneController, ReplayRuntime& replayRuntime,
                                   RuntimeInputContext& runtimeInput )
{
    // Lifetime: all domain owners are synchronous borrows for one semantic
    // mode request. InputRouter retains only its own edge/presentation state.
    // Invariant: interaction cleanup precedes camera/editor mutation, then
    // pointer and RuntimeInputContext presentation publish the completed mode.
    InputRouter& inputRouter = *this;
    const bool authoredScene = sceneController.State().isSceneMode;
    const uint32_t enabledMask = RuntimeCameraModeEnabledMask( sceneController.State().isSceneMode,
                                                               sceneController.Scene().SceneEntityCount() );

    const int modeIndex = static_cast<int>( mode );

    if ( modeIndex < 0 || modeIndex >= static_cast<int>( RunCameraMode::Count ) )
    {
        return;
    }

    const RunCameraMode previousMode = NormalizeRuntimeCameraMode( camera.mode, authoredScene, enabledMask );
    mode = NormalizeRuntimeCameraMode( mode, authoredScene, enabledMask );
    const bool enteringAttach = mode == RunCameraMode::Attach && previousMode != RunCameraMode::Attach;
    const bool leavingAttach = previousMode == RunCameraMode::Attach && mode != RunCameraMode::Attach;

    if ( enteringAttach )
    {
        attachedCamera.CaptureReturnState( previousMode, sceneController.Scene().Cameras() );
    }

    if ( mode == RunCameraMode::Demo )
    {
        const int modelCount = sceneController.Scene().SceneEntityCount();

        if ( !camera.trackBallRow.IsValid() || camera.trackBallRow.value >= modelCount )
        {
            camera.trackBallRow.value = 0;
        }

        if ( camera.trackHeight <= 0.0f )
        {
            camera.trackHeight = 300.0f;
        }
    }

    if ( mode == RunCameraMode::Director && previousMode != RunCameraMode::Director )
    {
        DemoDirectorPlayback::EnterMode( camera.director, CaptureDemoDirectorPose( sceneController.Scene().Cameras() ) );
    }

    const RuntimeInteractionTransition transition = EnterInteractionForCameraMode( interaction, mode );
    inputRouter.ApplyInteractionTransition( transition, editorTools, runtimeTools, interaction, attachedCamera, camera,
                                            sceneController, replayRuntime,
                                            NormalizeRuntimeCameraMode( replayRuntime.BuildInputView().restoreCameraMode,
                                                                        authoredScene, enabledMask ) );

    const bool wasFlyMode = RunCameraModeUsesFlyControls( camera.mode, attachedCamera.State().activeFollow,
                                                          camera.director.grabbed );

    if ( mode != RunCameraMode::Launcher )
    {
        camera.modeBeforeLauncher = mode == RunCameraMode::Manipulator ? RunCameraMode::Inspect : mode;
    }

    camera.mode = mode;

    if ( leavingAttach )
    {
        attachedCamera.RestoreReturnState( sceneController.Scene().Cameras() );
    }

    if ( mode == RunCameraMode::Attach )
    {
        attachedCamera.State().activeFollow = true;
        attachedCamera.State().needsEntryTween = true;
    }

    if ( editorTools.Editor().editorModeEnabled )
    {
        editorTools.Editor().restoreCameraModeAfterEditor = mode;
    }

    if ( mode != RunCameraMode::Manipulator )
    {
        runtimeTools.CancelMousePickup( inputRouter, interaction );
    }

    const bool isFlyMode = RunCameraModeUsesFlyControls( camera.mode, attachedCamera.State().activeFollow,
                                                         camera.director.grabbed );

    if ( wasFlyMode != isFlyMode )
    {
        if ( isFlyMode )
        {
            EnterFlyModeCamera( inputRouter, camera, sceneController.Scene().Cameras(), authoredScene, editorTools.Editor(),
                                replayRuntime.BuildInputView() );
        }
        else
        {
            ExitFlyModeCamera( inputRouter, camera, sceneController.Scene().Cameras(),
                               *sceneController.Scene().Terrain().Get(), authoredScene );
        }
    }
    else
    {
        InputController::ResetMouseLook( camera );
        inputRouter.ApplyPointerPresentation(
            EvaluateRuntimePointerPresentation( inputRouter, editorTools.Editor(), replayRuntime.BuildInputView() ) );
    }

    if ( mode == RunCameraMode::Attach )
    {
        int seedIndex = -1;
        const PhysicsBodyStore& bodyStore = sceneController.Scene().BodyStore();
        const int modelCount = bodyStore.Count();
        const int replayTargetRow = replayRuntime.BuildInputView().pathTargetModelRow;

        if ( replayTargetRow >= 0 && replayTargetRow < modelCount )
        {
            seedIndex = replayTargetRow;
        }
        else
        {
            const int selectedModelIndex = ResolveSelectedEditorModelIndex( editorTools.Editor(), bodyStore );

            if ( selectedModelIndex >= 0 && selectedModelIndex < modelCount )
            {
                seedIndex = selectedModelIndex;
            }
        }

        AttachedCameraTargetSelection selection;
        const AttachedCameraSeedResult seedResult = attachedCamera.SeedTarget( sceneController.Scene(), seedIndex,
                                                                               selection );

        if ( seedResult == AttachedCameraSeedResult::SelectedSeed )
        {
            RuntimeInteractionCommand command;
            command.type = RuntimeInteractionCommandType::SetEditorSelection;
            command.body = selection.body;
            command.collider = selection.collider;
            command.selectionScope = RuntimeInteractionSelectionScope::Inspect;
            command.claimSelectionOwner = false;
            editorTools.ApplySelectionCommand( command, sceneController.Scene() );
        }

        if ( seedResult != AttachedCameraSeedResult::Failed )
        {
            inputRouter.ApplyPointerPresentation(
                EvaluateRuntimePointerPresentation( inputRouter, editorTools.Editor(), replayRuntime.BuildInputView() ) );
        }
    }

    InputController::ApplyModeAction( runtimeInput,
                                      InputController::ResolveMode(
                                          BuildRuntimeInputModeState( camera.mode, editorTools.Editor(),
                                                                      interaction.Gesture(),
                                                                      attachedCamera.State().activeFollow,
                                                                      camera.director.grabbed ) ),
                                      source == RuntimeInputActionSource::UI ? RuntimeInputAction::SetCameraMode
                                                                             : RuntimeInputAction::CycleCameraMode,
                                      source );
}


void InputRouter::CycleCameraMode( EditorToolsOwner& editorTools, RuntimeTools& runtimeTools,
                                   RuntimeInteractionController& interaction, AttachedCameraController& attachedCamera,
                                   CameraControlState& camera, SceneController& sceneController,
                                   ReplayRuntime& replayRuntime, RuntimeInputContext& runtimeInput )
{
    const bool authoredScene = sceneController.State().isSceneMode;
    const uint32_t enabledMask = RuntimeCameraModeEnabledMask( sceneController.State().isSceneMode,
                                                               sceneController.Scene().SceneEntityCount() );

    int current = static_cast<int>( camera.mode );

    if ( current < 0 || current >= static_cast<int>( RunCameraMode::Count ) )
    {
        current = static_cast<int>( authoredScene ? RunCameraMode::Scene : RunCameraMode::Demo );
    }

    if ( NormalizeRuntimeCameraMode( camera.mode, authoredScene, enabledMask ) == RunCameraMode::Attach )
    {
        const RunCameraMode restoreMode = NormalizeRuntimeCameraMode( attachedCamera.State().returnMode, authoredScene,
                                                                      enabledMask );

        const int restoreIndex = static_cast<int>( restoreMode );

        // Why: Attach is a temporary follow workspace. Keyboard cycling out of
        // it should return to the camera mode that entered Attach, not continue
        // to the next enum value and strand the operator at the follow pose.
        if ( restoreIndex >= 0 && restoreIndex < static_cast<int>( RunCameraMode::Count ) &&
             ( enabledMask & ( 1u << restoreIndex ) ) != 0 )
        {
            ApplyCameraMode( restoreMode, RuntimeInputActionSource::Keyboard, editorTools, runtimeTools, interaction,
                             attachedCamera, camera, sceneController, replayRuntime, runtimeInput );

            return;
        }
    }

    for ( int step = 1; step <= static_cast<int>( RunCameraMode::Count ); ++step )
    {
        const int next = ( current + step ) % static_cast<int>( RunCameraMode::Count );

        if ( ( enabledMask & ( 1u << next ) ) != 0 )
        {
            ApplyCameraMode( static_cast<RunCameraMode>( next ), RuntimeInputActionSource::Keyboard, editorTools,
                             runtimeTools, interaction, attachedCamera, camera, sceneController, replayRuntime,
                             runtimeInput );

            return;
        }
    }
}


bool InputRouter::HandleUnfocusedFrame( EditorToolsOwner& editorTools, RuntimeTools& runtimeTools,
                                        RuntimeInteractionController& interaction, AttachedCameraController& attachedCamera,
                                        CameraControlState& camera, UI::InGameUI& ui, SceneController& sceneController,
                                        ReplayRuntime& replayRuntime, RuntimeInputContext& runtimeInput )
{
    if ( AppFocused() )
    {
        return false;
    }

    // Invariant: focus loss releases every active tool capture and refreshes
    // action memory so refocus cannot replay stale drag/key edges.
    SceneWorld& sceneWorld = sceneController.Scene();
    const SceneSessionState& sceneState = sceneController.State();
    const ReplayInputView replayInput = replayRuntime.BuildInputView();
    const int sceneEntityCount = sceneWorld.SceneEntityCount();
    const uint32_t cameraModeEnabledMask = RuntimeCameraModeEnabledMask( sceneState.isSceneMode, sceneEntityCount );
    const RunCameraMode normalizedRestoreMode = NormalizeRuntimeCameraMode( replayInput.restoreCameraMode,
                                                                            sceneState.isSceneMode, cameraModeEnabledMask );

    interaction.CancelCameraLookGesture();
    replayRuntime.ApplyInputFocusLoss( &sceneWorld.Cameras(), sceneWorld.Terrain().Get(), camera, normalizedRestoreMode,
                                       attachedCamera.State().activeFollow, camera.director.grabbed, interaction, *this );

    CancelPointerPresentation();
    runtimeTools.CancelMousePickup( *this, interaction );
    ResetEditorUnfocusedInputState( editorTools.Editor(), interaction );
    InputController::ResetUnfocusedInput( camera );
    InputController::BeginFrame( runtimeInput,
                                 BuildRuntimeInputModeState( camera.mode, editorTools.Editor(), interaction.Gesture(),
                                                             attachedCamera.State().activeFollow, camera.director.grabbed ),
                                 false, true, true );

    ui.CancelInputCapture();
    return true;
}


InputCaptureActionResult InputRouter::DispatchCaptureActions( InputActions& actions, const CameraControlState& camera,
                                                              const AttachedCameraController& attachedCamera,
                                                              const UI::InGameUI& ui, SceneController& sceneController,
                                                              const GameObjects::PresentationSaveState& presentation,
                                                              const ReplayInputView& replayInput )
{
    InputCaptureActionResult result;

    // Why: capture/reset shortcuts run after UI input so focused controls and
    // panels get first refusal on keyboard ownership.
    const bool flyCamera = RunCameraModeUsesFlyControls( camera.mode, attachedCamera.State().activeFollow,
                                                         camera.director.grabbed );

    const KeyboardContextFacts contextFacts { !ui.BlocksKeyboard(),
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
            HandleEditorSceneSaveHotkey( m_resultDiagnostics, sceneController.Scene(), sceneController.State(), presentation,
                                         true );

            break;
        case RuntimeInputAction::SaveScreenshot:
            result.screenshotRequested = true;
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

    return result;
}


InputAfterUiDismissResult
InputRouter::DispatchAfterUiDismiss( InputActions& actions, bool uiUserInteracted, double nowSeconds, bool gameUiActive,
                                     CameraControlState& camera, AttachedCameraController& attachedCamera,
                                     EditorToolsOwner& editorTools, UI::InGameUI& ui, SceneController& sceneController,
                                     RuntimeOverlayDiagnostics& overlays, const ReplayInputView& replayInput )
{
    InputAfterUiDismissResult result;
    RuntimeOverlayPresentationEdit presentationEdit = overlays.EditPresentation();
    OverlayDebugState& debug = presentationEdit.State();
    const bool flyCamera = RunCameraModeUsesFlyControls( camera.mode, attachedCamera.State().activeFollow,
                                                         camera.director.grabbed );

    const KeyboardContextFacts contextFacts { !ui.BlocksKeyboard(),
                                              sceneController.State().isSceneMode,
                                              flyCamera,
                                              RunCameraModeUsesLauncher( camera.mode ),
                                              RunCameraModeIsAttached( camera.mode ),
                                              camera.mode == RunCameraMode::Director,
                                              camera.mode == RunCameraMode::Director || flyCamera,
                                              editorTools.Editor().editorModeEnabled,
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
            result.quitRequested = true;
            return result;
        }
        else if ( !gameUiActive )
        {
            RecordTap( event.action, nowSeconds );
        }
        else
        {
            sceneController.State().isInteractiveRun = true;
            sceneController.State().isExitOnComplete = false;
            result.disableCaptureAutomationExit = true;
            ui.ToggleVisible( nowSeconds );
            debug.overlayMode = OverlayMode::None;
            RecordTap( event.action, nowSeconds );
            ApplyPointerPresentation( EvaluateRuntimePointerPresentation( *this, editorTools.Editor(), replayInput ) );

            if ( ReleasePointerToUi( EvaluateRuntimePointerPresentation( *this, editorTools.Editor(), replayInput ) ) )
            {
                InputController::ResetMouseLook( camera );
            }
        }
    }

    return result;
}
