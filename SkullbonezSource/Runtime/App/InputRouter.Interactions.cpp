/*
File: SkullbonezSource/Runtime/App/InputRouter.Interactions.cpp
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
  - Agentic/Reference/comment-style-guide.md
*/
#include "Run.h"
#include "../Diagnostics/RuntimeOverlayDiagnostics.h"
#include "InputFrame.h"
#include "../RuntimeFrameViews.h"
#include "../Camera/AttachedCameraController.h"
#include "../Editor/EditorTools.h"
#include "../Input/InputController.Bindings.h"
#include "../Input/InputController.h"
#include "../Replay/ReplayRestoreTransactions.h"
#include "../Direction/DemoDirectorPlayback.h"
#include "../Interaction/RuntimeInteractionCommands.h"
#include "../Scene/SceneRuntimeCreate.h"
#include "../Interaction/OperatorCommandApplier.h"
#include "../Scene/SceneRuntimeGeneratedControls.h"
#include "../Scene/SceneRuntimeLoad.h"
#include "../Scene/SceneRuntimeStyle.h"
#include "../../Core/Log.h"
#include "../../Physics/ColliderStore.h"
#include "../../Physics/PhysicsBodyStore.h"
#include "../../UI/UI.h"
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
using namespace SkullbonezCore::Runtime::RunInternal;
using SkullbonezCore::Math::Vector::Vector3;


void InputRouter::ApplyInteractionTransitionCleanup( const RuntimeInteractionTransition& transition,
                                                     RuntimeFrameInteractionView& interactionOwners,
                                                     SceneController& models,
                                                     ReplayRuntime& replayRuntime,
                                                     RunCameraMode replayRestoreCameraMode )
{
    RuntimeTools& runtimeTools = interactionOwners.runtimeTools;
    RuntimeInteractionController& interaction = interactionOwners.interaction;
    CameraControlState& camera = interactionOwners.camera;
    Environment::CameraCollection& cameras = models.Scene().Cameras();
    Geometry::Terrain* terrain = models.Scene().Terrain().Get();
    const bool attachedCameraFollow = interactionOwners.attachedCamera.State().activeFollow;
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

    const bool replayInteractionCleared = replayRuntime.ApplyInteractionExit(
        ReplayInteractionExitInput { !enteringReplay,
                                     IsReplayWorldOwner( transition.previousOwner ),
                                     replayRestoreCameraMode,
                                     attachedCameraFollow,
                                     directorGrabbed },
        models.Scene().Physics(),
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
        runtimeTools.ClearEditorInteractionForTransition( enteringReplay || enteringTool, models.Scene(), interaction );
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
                                              SceneController& sceneController,
                                              ReplayRuntime& replayRuntime,
                                              RunCameraMode replayRestoreCameraMode )
{
    ApplyInteractionTransitionCleanup( transition,
                                       interactionOwners,
                                       sceneController,
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
                                                                    SceneController& sceneController,
                                                                    ReplayRuntime& replayRuntime,
                                                                    RunCameraMode replayRestoreCameraMode )
{
    // Why: changing the logical owner can eject replay, editor, or camera gestures. InputRouter owns that
    // cleanup because it also reconciles the corresponding capture and cursor state.
    RuntimeInteractionController& interaction = interactionOwners.interaction;
    const RuntimeWorkspace workspace = interaction.WorkspaceForOwner( owner );
    const RuntimeInteractionTransition transition = interaction.SetWorldInteractionOwnerInWorkspace( workspace,
                                                                                                     owner,
                                                                                                     reason );

    ApplyInteractionTransitionCleanup( transition,
                                       interactionOwners,
                                       sceneController,
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
    const CameraControlState& camera = interactionOwners.camera;
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


RuntimePointerRouteResult InputRouter::RouteRuntimePointer( const RuntimePointerEvent& pointer,
                                                            RunCameraMode cameraMode,
                                                            bool replayInspectionActive,
                                                            int activeModelCapacity,
                                                            const Window& window,
                                                            Assets::AssetSystem& assets,
                                                            RuntimeFrameInteractionView& interactionOwners,
                                                            SceneController& models,
                                                            ReplayRuntime& replayRuntime,
                                                            RunCameraMode replayRestoreCameraMode )
{
    RuntimeTools& runtimeTools = interactionOwners.runtimeTools;
    AttachedCameraController& attachedCamera = interactionOwners.attachedCamera;
    RuntimeInteractionController& interaction = interactionOwners.interaction;
    CameraControlState& camera = interactionOwners.camera;
    SceneEntityStore& entities = models.Scene().Entities();
    SceneSessionState& scene = models.State();
    Geometry::Terrain* terrain = models.Scene().Terrain().Get();
    Environment::CameraCollection& cameras = models.Scene().Cameras();
    const bool attachedCameraFollow = attachedCamera.State().activeFollow;
    const bool directorGrabbed = camera.director.grabbed;
    Vector3 rayOrigin = SkullbonezCore::Math::Vector::ZERO_VECTOR;
    Vector3 rayDirection = SkullbonezCore::Math::Vector::ZERO_VECTOR;
    Vector3 clampedRayOrigin = SkullbonezCore::Math::Vector::ZERO_VECTOR;
    Vector3 clampedRayDirection = SkullbonezCore::Math::Vector::ZERO_VECTOR;
    const bool hasWorldRay = TryBuildWorldRay( cameras, window, rayOrigin, rayDirection );
    const bool hasClampedWorldRay = TryBuildWorldRay( cameras, window, clampedRayOrigin, clampedRayDirection, true );
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

    if ( interaction.PointerCapture() == RuntimePointerCaptureOwner::CameraLook )
    {
        return result;
    }

    RuntimePointerArbitration arbitration;
    // Invariant: this order is the world-pointer arbitration contract. Each
    // concrete owner sees the immutable pointer/ray values only when every
    // earlier owner declined the gesture. The phase cursor lane-F fails a
    // reordered, skipped, or repeated stage.
    (void)arbitration.BeginStage( RuntimePointerRouteStage::Editor );
    const EditorPointerRouteResult editorResult = RouteEditorPointer( pointer,
                                                                      hasWorldRay,
                                                                      rayOrigin,
                                                                      rayDirection,
                                                                      cameraMode,
                                                                      replayInspectionActive,
                                                                      activeModelCapacity,
                                                                      assets,
                                                                      runtimeTools,
                                                                      interaction,
                                                                      models );

    result.enteredInteractiveScene = editorResult.enteredInteractiveScene;
    if ( editorResult.hasInteractionTransition )
    {
        ApplyInteractionTransitionCleanup( editorResult.interactionTransition,
                                           interactionOwners,
                                           models,
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

    arbitration.FinishStage( RuntimePointerRouteStage::Editor, editorResult.consumed );

    bool pickupConsumed = false;
    if ( arbitration.BeginStage( RuntimePointerRouteStage::MousePickup ) )
    {
        const bool pickupModeActive = RunCameraModeIsManipulator( cameraMode ) &&
                                      !runtimeTools.Editor().editorModeEnabled && !replayInspectionActive;

        if ( !pickupModeActive )
        {
            runtimeTools.CancelMousePickup( *this, interaction );
        }
        else if ( interaction.Gesture().kind == RuntimeInteractionGestureKind::MousePickupDrag || pointer.leftPressed )
        {
            const MousePickupPointerResult pickupResult = runtimeTools.RouteMousePickupPointer( pointer,
                                                                                                hasWorldRay,
                                                                                                rayOrigin,
                                                                                                rayDirection,
                                                                                                hasClampedWorldRay,
                                                                                                clampedRayOrigin,
                                                                                                clampedRayDirection,
                                                                                                cameraEye,
                                                                                                cameraView,
                                                                                                models.Scene(),
                                                                                                *this,
                                                                                                interaction );

            result.enteredInteractiveScene |= pickupResult.enteredInteractive;
            pickupConsumed = pickupResult.consumed;
        }
    }

    arbitration.FinishStage( RuntimePointerRouteStage::MousePickup, pickupConsumed );

    bool cameraConsumed = false;
    if ( arbitration.BeginStage( RuntimePointerRouteStage::AttachedCamera ) && RunCameraModeIsAttached( cameraMode ) &&
         pointer.leftPressed && !pointer.suppressWorldAction )
    {
        AttachedCameraTargetSelection selection;
        if ( attachedCamera.PickTarget( models.Scene(), hasWorldRay, rayOrigin, rayDirection, selection ) )
        {
            RuntimeInteractionCommand command;
            command.type = RuntimeInteractionCommandType::SetEditorSelection;
            command.body = selection.body;
            command.collider = selection.collider;
            command.selectionScope = RuntimeInteractionSelectionScope::Inspect;
            command.claimSelectionOwner = false;
            runtimeTools.ApplySelectionCommand( command, models.Scene() );
            ApplyPointerPresentation(
                EvaluateRuntimePointerPresentation( *this, runtimeTools.Editor(), replayRuntime.BuildInputView() ) );
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
        replayConsumed = replayRuntime.RouteWorldPointer(
            ReplayWorldPointerInput { pointer.leftPressed,
                                      pointer.suppressWorldAction,
                                      runtimeTools.Editor().editorModeEnabled,
                                      pointer.controlDown,
                                      RunCameraModeUsesLauncher( cameraMode ),
                                      pickInput,
                                      replayRestoreCameraMode,
                                      attachedCameraFollow,
                                      directorGrabbed },
            entities,
            models.Scene().BodyStore(),
            models.Scene().Colliders(),
            models.Scene().RenderPresentationRecords(),
            &cameras,
            terrain,
            camera,
            interaction,
            *this );
    }

    arbitration.FinishStage( RuntimePointerRouteStage::Replay, replayConsumed );

    bool launcherConsumed = false;
    if ( arbitration.BeginStage( RuntimePointerRouteStage::Launcher ) )
    {
        const LauncherPointerResult launcherResult = runtimeTools.RouteLauncherPointer(
            { RunCameraModeUsesLauncher( cameraMode ),
              pointer.leftPressed,
              pointer.suppressWorldAction,
              pointer.uiWantsNativeMouseCursor,
              activeModelCapacity },
            models.Scene(),
            scene );

        if ( launcherResult.recordReplayEvent )
        {
            replayRuntime.SubmitEvent( launcherResult.replayEvent );
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


void InputRouter::ApplyCameraMode( RunCameraMode mode,
                                   RuntimeInputActionSource source,
                                   RuntimeFrameInteractionView& interactionOwners,
                                   SceneController& m_sceneController,
                                   ReplayRuntime& replayRuntime,
                                   RuntimeInputContext& runtimeInput )
{
    // Lifetime: all domain owners are synchronous borrows for one semantic
    // mode request. InputRouter retains only its own edge/presentation state.
    // Invariant: interaction cleanup precedes camera/editor mutation, then
    // pointer and RuntimeInputContext presentation publish the completed mode.
    InputRouter& m_inputRouter = *this;
    CameraControlState& m_camera = interactionOwners.camera;
    RuntimeInteractionController& m_interaction = interactionOwners.interaction;
    RuntimeTools& m_runtimeTools = interactionOwners.runtimeTools;
    ReplayRuntime& m_replayRuntime = replayRuntime;
    AttachedCameraController& m_attachedCamera = interactionOwners.attachedCamera;
    const bool authoredScene = m_sceneController.State().isSceneMode;
    const uint32_t enabledMask = RuntimeCameraModeEnabledMask( m_sceneController.State().isSceneMode,
                                                               m_sceneController.Scene().SceneEntityCount() );

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
        m_attachedCamera.CaptureReturnState( previousMode, m_sceneController.Scene().Cameras() );
    }

    if ( mode == RunCameraMode::Demo )
    {
        const int modelCount = m_sceneController.Scene().SceneEntityCount();
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
        DemoDirectorPlayback::EnterMode( m_camera, m_sceneController.Scene().Cameras() );
    }

    const RuntimeInteractionTransition transition = m_interaction.EnterCameraMode( mode );
    m_inputRouter.ApplyInteractionTransition(
        transition,
        interactionOwners,
        m_sceneController,
        m_replayRuntime,
        NormalizeRuntimeCameraMode( m_replayRuntime.BuildInputView().restoreCameraMode, authoredScene, enabledMask ) );

    const bool wasFlyMode = RunCameraModeUsesFlyControls( m_camera.mode,
                                                          m_attachedCamera.State().activeFollow,
                                                          m_camera.director.grabbed );

    if ( mode != RunCameraMode::Launcher )
    {
        m_camera.modeBeforeLauncher = mode == RunCameraMode::Manipulator ? RunCameraMode::Inspect : mode;
    }

    m_camera.mode = mode;
    if ( leavingAttach )
    {
        m_attachedCamera.RestoreReturnState( m_sceneController.Scene().Cameras() );
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

    const bool isFlyMode = RunCameraModeUsesFlyControls( m_camera.mode,
                                                         m_attachedCamera.State().activeFollow,
                                                         m_camera.director.grabbed );

    if ( wasFlyMode != isFlyMode )
    {
        if ( isFlyMode )
        {
            EnterFlyModeCamera( m_inputRouter,
                                m_camera,
                                m_sceneController.Scene().Cameras(),
                                authoredScene,
                                m_runtimeTools.Editor(),
                                m_replayRuntime.BuildInputView() );
        }
        else
        {
            ExitFlyModeCamera( m_inputRouter,
                               m_camera,
                               m_sceneController.Scene().Cameras(),
                               *m_sceneController.Scene().Terrain().Get(),
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
        const PhysicsBodyStore& bodyStore = m_sceneController.Scene().BodyStore();
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
        const AttachedCameraSeedResult seedResult = m_attachedCamera.SeedTarget( m_sceneController.Scene(),
                                                                                 seedIndex,
                                                                                 selection );

        if ( seedResult == AttachedCameraSeedResult::SelectedSeed )
        {
            RuntimeInteractionCommand command;
            command.type = RuntimeInteractionCommandType::SetEditorSelection;
            command.body = selection.body;
            command.collider = selection.collider;
            command.selectionScope = RuntimeInteractionSelectionScope::Inspect;
            command.claimSelectionOwner = false;
            m_runtimeTools.ApplySelectionCommand( command, m_sceneController.Scene() );
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
                                   SceneController& sceneController,
                                   ReplayRuntime& replayRuntime,
                                   RuntimeInputContext& runtimeInput )
{
    CameraControlState& camera = interactionOwners.camera;
    AttachedCameraController& attachedCamera = interactionOwners.attachedCamera;
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
        const RunCameraMode restoreMode = NormalizeRuntimeCameraMode( attachedCamera.State().returnMode,
                                                                      authoredScene,
                                                                      enabledMask );

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
                             sceneController,
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
                             sceneController,
                             replayRuntime,
                             runtimeInput );

            return;
        }
    }
}


bool InputRouter::HandleUnfocusedFrame( RuntimeFrameInteractionView& interactionOwners,
                                        SceneController& sceneController,
                                        ReplayRuntime& replayRuntime,
                                        RuntimeInputContext& runtimeInput )
{
    RuntimeInteractionController& interaction = interactionOwners.interaction;
    RuntimeTools& runtimeTools = interactionOwners.runtimeTools;
    AttachedCameraController& attachedCamera = interactionOwners.attachedCamera;
    CameraControlState& camera = interactionOwners.camera;
    UI::InGameUI& ui = interactionOwners.operatorUi;
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
                                                                            sceneState.isSceneMode,
                                                                            cameraModeEnabledMask );

    interaction.CancelCameraLookGesture();
    replayRuntime.ApplyInputFocusLoss( &sceneWorld.Cameras(),
                                       sceneWorld.Terrain().Get(),
                                       camera,
                                       normalizedRestoreMode,
                                       attachedCamera.State().activeFollow,
                                       camera.director.grabbed,
                                       interaction,
                                       *this );

    CancelPointerPresentation();
    runtimeTools.CancelMousePickup( *this, interaction );
    RunInternal::ResetEditorUnfocusedInputState( { runtimeTools.Editor(), sceneController.Scene(), interaction } );
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
                                          DiagnosticsRuntime& diagnosticsRuntime,
                                          RuntimeFrameInteractionView& interactionOwners,
                                          SceneController& sceneController,
                                          const GameObjects::PresentationSaveState& presentation,
                                          const ReplayInputView& replayInput )
{
    const CameraControlState& camera = interactionOwners.camera;
    const AttachedCameraController& attachedCamera = interactionOwners.attachedCamera;
    const UI::InGameUI& ui = interactionOwners.operatorUi;
    // Why: capture/reset shortcuts run after UI input so focused controls and
    // panels get first refusal on keyboard ownership.
    const bool flyCamera = RunCameraModeUsesFlyControls( camera.mode,
                                                         attachedCamera.State().activeFollow,
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
            RunInternal::HandleEditorSceneSaveHotkey( sceneController.Scene(),
                                                      sceneController.State(),
                                                      presentation,
                                                      true );

            break;
        case RuntimeInputAction::SaveScreenshot:
            RunInternal::HandleEditorScreenshotHotkey( diagnosticsRuntime.Capture(), true );
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
                                          DiagnosticsRuntime& diagnosticsRuntime,
                                          RuntimeFrameInteractionView& interactionOwners,
                                          SceneController& sceneController,
                                          RuntimeOverlayDiagnostics& overlays,
                                          const ReplayInputView& replayInput )
{
    const bool uiUserInteracted = input.uiUserInteracted;
    const double nowSeconds = input.nowSeconds;
    CameraControlState& camera = interactionOwners.camera;
    AttachedCameraController& attachedCamera = interactionOwners.attachedCamera;
    RuntimeTools& runtimeTools = interactionOwners.runtimeTools;
    RuntimeOverlayPresentationEdit presentationEdit = overlays.EditPresentation();
    OverlayDebugState& debug = presentationEdit.State();
    UI::InGameUI& ui = interactionOwners.operatorUi;
    const bool flyCamera = RunCameraModeUsesFlyControls( camera.mode,
                                                         attachedCamera.State().activeFollow,
                                                         camera.director.grabbed );

    const KeyboardContextFacts contextFacts { !ui.BlocksKeyboard(),
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
        else if ( !input.legacyUiActive )
        {
            // ImGui owns Escape/focus policy in this process. Remember the tap
            // for the existing quick-exit gesture without activating Legacy.
            RecordTap( event.action, nowSeconds );
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
