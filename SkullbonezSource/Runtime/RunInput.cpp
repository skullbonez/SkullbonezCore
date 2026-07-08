/*
File: SkullbonezSource/Runtime/RunInput.cpp
Purpose:
  Routes raw keyboard, mouse, and UI commands into runtime state changes.

Mental model:
  Input arbitration stays here.
  Editor, launcher, and replay behavior live in dedicated runtime files.

Glossary:
  Attach return pose: The visible camera pose captured before Attach takes over
    so the operator can return to the same view later.
  Contact-audio flash command: One-frame UI request that cycles a render-only
    diagnostic selector; it does not change audio classification policy.
  Contact-audio simple command: One-frame UI request that switches audio to the
    body-linear-energy path instead of the solver contact-row classifier.
  Attached-camera physics target: Body/collider handles plus a store-owned pose,
    velocity, and broad radius sampled for camera follow math.
  Lane R result: Recoverable scene-control failure reported by a load action
    without treating the command as successfully applied.
  Validation gate: Repository script that proves a class of changes before
    commit or PR.

Invariants:
  - This file arbitrates ownership before mutating world state; UI, editor,
    replay, launcher, and camera modes must not all consume the same gesture.
  - Camera vectors are normalized defensively because scene and replay targets
    may be absent, stale, or degenerate for a frame.

Related:
  - Agentic/Reference/runtime-reference.md
  - Agentic/Reference/comment-style-guide.md
*/
#include "RunInternal.h"
#include "AttachedCameraController.h"
#include "Editor/EditorTools.h"
#include "InputController.h"
#include "Replay/ReplayOverlayLayout.h"
#include "RunDemoDirector.h"
#include "RuntimeInteractionCommands.h"
#include "RuntimePickService.h"
#include "Scene/SceneRuntimeCreate.h"
#include "RuntimeTuning.h"
#include "Scene/SceneRuntimeDefaults.h"
#include "Scene/SceneRuntimeGeneratedControls.h"
#include "Scene/SceneRuntimeLoad.h"
#include "Scene/SceneRuntimeStyle.h"
#include "../Core/Log.h"
#include "../Physics/ColliderStore.h"
#include "../Physics/PhysicsBodyStore.h"
#include "../UI/UIInput.h"
#include "../UI/UILayout.h"

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <utility>

using namespace SkullbonezCore::Basics;
using namespace SkullbonezCore::Math::CollisionDetection;
using namespace SkullbonezCore::Math::Orientation;
using namespace SkullbonezCore::Math::Transformation;
using namespace SkullbonezCore::Physics;
using namespace SkullbonezCore::UI::Layout;
using namespace SkullbonezCore::Basics::RunInternal;

namespace
{
bool CameraModeUsesFlyControls( RunCameraMode mode, bool attachActiveFollow, bool directorGrabbed )
{
    return mode == RunCameraMode::Inspect || mode == RunCameraMode::Launcher || mode == RunCameraMode::Manipulator ||
           ( mode == RunCameraMode::Attach && attachActiveFollow ) ||
           ( mode == RunCameraMode::Director && directorGrabbed );
}


bool CameraModeUsesLauncher( RunCameraMode mode )
{
    return mode == RunCameraMode::Launcher;
}

const char* PresentationNameForModelIndex( const SkullbonezCore::GameObjects::GameModelCollection& collection,
                                           int modelIndex )
{
    const auto& presentationRecords = collection.RenderPresentationRecords();
    if ( modelIndex < 0 || modelIndex >= static_cast<int>( presentationRecords.size() ) )
    {
        return "";
    }
    return presentationRecords[static_cast<std::size_t>( modelIndex )].displayName;
}

AttachedCameraPose AttachedCameraPoseFromCameras( SkullbonezCore::Environment::CameraCollection& cameras )
{
    AttachedCameraPose pose;
    pose.eye = cameras.GetCameraTranslation();
    pose.view = cameras.GetCameraView();
    pose.up = cameras.GetCameraUp();
    return pose;
}


RuntimeInputModeState BuildRuntimeInputModeState( RunCameraMode mode,
                                                  const RunEditorPlacementState& editor,
                                                  bool attachActiveFollow,
                                                  bool directorGrabbed )
{
    RuntimeInputModeState state;
    state.flyCamera = CameraModeUsesFlyControls( mode, attachActiveFollow, directorGrabbed );
    state.launcher = CameraModeUsesLauncher( mode );
    state.manipulator = mode == RunCameraMode::Manipulator;
    state.editor = editor.editorModeEnabled;
    state.editorPlacement = editor.placementModeEnabled;
    state.editorViewportLook = editor.viewportLookActive;
    state.editorPlacementScale = editor.placementScaleActive;
    state.editorGizmoDrag = editor.gizmoDragActive;
    state.editorGizmoRotation = editor.gizmoDragIsRotation;
    state.editorGizmoScale = editor.gizmoDragIsScale;
    return state;
}

bool IsReplayWorldOwner( WorldInteractionOwner owner )
{
    return owner == WorldInteractionOwner::ReplayScrub || owner == WorldInteractionOwner::ReplayVelocityEdit ||
           owner == WorldInteractionOwner::ReplayPrediction || owner == WorldInteractionOwner::ReplayBranchTarget ||
           owner == WorldInteractionOwner::ReplayCauseTree;
}

bool IsEditorWorldOwner( WorldInteractionOwner owner )
{
    return owner == WorldInteractionOwner::EditorPlacement || owner == WorldInteractionOwner::EditorGizmo ||
           owner == WorldInteractionOwner::InspectGizmo;
}

RuntimeWorkspace WorkspaceForWorldInteractionOwner( RuntimeWorkspace fallback, WorldInteractionOwner owner )
{
    if ( IsReplayWorldOwner( owner ) )
    {
        return RuntimeWorkspace::Replay;
    }
    if ( owner == WorldInteractionOwner::InspectGizmo )
    {
        return RuntimeWorkspace::Inspect;
    }
    if ( owner == WorldInteractionOwner::EditorPlacement || owner == WorldInteractionOwner::EditorGizmo )
    {
        return RuntimeWorkspace::Edit;
    }
    if ( owner == WorldInteractionOwner::Launcher || owner == WorldInteractionOwner::Manipulator )
    {
        return RuntimeWorkspace::Live;
    }
    return fallback;
}

const char* ReplayRuntimeCommandName( RuntimeCommandType type )
{
    switch ( type )
    {
    case RuntimeCommandType::LoadSceneIndex:
        return "LoadSceneIndex";
    case RuntimeCommandType::LoadDemoScene:
        return "LoadDemoScene";
    case RuntimeCommandType::ResetCurrentScene:
        return "ResetCurrentScene";
    case RuntimeCommandType::CreateScene:
        return "CreateScene";
    case RuntimeCommandType::SaveScreenshot:
        return "SaveScreenshot";
    case RuntimeCommandType::SaveSceneDefaults:
        return "SaveSceneDefaults";
    case RuntimeCommandType::SaveRenderDefaults:
        return "SaveRenderDefaults";
    case RuntimeCommandType::SaveSkyDefaults:
        return "SaveSkyDefaults";
    case RuntimeCommandType::AdvanceScene:
        return "AdvanceScene";
    case RuntimeCommandType::Quit:
        return "Quit";
    case RuntimeCommandType::None:
    default:
        return "None";
    }
}

uint32_t ReplayRuntimeCommandFlags( const RuntimeCommand& command )
{
    uint32_t flags = 0;
    flags |= command.preserveUIState ? 1u : 0u;
    flags |= command.suppressExitOnComplete ? 2u : 0u;
    flags |= command.preserveRuntimeState ? 4u : 0u;
    return flags;
}

const char* RuntimeInteractionSelectionScopeName( RuntimeInteractionSelectionScope scope )
{
    switch ( scope )
    {
    case RuntimeInteractionSelectionScope::Inspect:
        return "inspect";
    case RuntimeInteractionSelectionScope::Editor:
    default:
        return "editor";
    }
}

const char* RuntimeInteractionEventName( RuntimeInteractionEventType type )
{
    switch ( type )
    {
    case RuntimeInteractionEventType::SelectionChanged:
        return "selection_changed";
    case RuntimeInteractionEventType::None:
    default:
        return "none";
    }
}

using InputBindingContext = RuntimeInputBindingContext;
constexpr RuntimeInputContextMask kKeyboardUnblockedContext =
    RuntimeInputContextBit( InputBindingContext::KeyboardUnblocked );
constexpr RuntimeInputContextMask kAfterUIUpdateContext = RuntimeInputContextBit( InputBindingContext::AfterUIUpdate );
constexpr RuntimeInputContextMask kCaptureContext = RuntimeInputContextBit( InputBindingContext::Capture );

// Invariant: This table mirrors the keyboard edges that TakeInput recognizes
// today. Step 1.2 makes the mapping auditable data only; dispatch still follows
// the existing hand-written branches until later action-group slices replace it.
const RuntimeInputKeyBinding kTakeInputKeyboardBindings[] = {
    { VK_OEM_3, RuntimeInputAction::ToggleEditor, kKeyboardUnblockedContext },
    { VK_TAB, RuntimeInputAction::CycleCameraMode, kKeyboardUnblockedContext },
    { 'F', RuntimeInputAction::ToggleFlyCamera, kKeyboardUnblockedContext },
    { 'N', RuntimeInputAction::ToggleLauncher, kKeyboardUnblockedContext },
    { 'M', RuntimeInputAction::CycleLauncherFireMode, kKeyboardUnblockedContext | InputBindingContext::Launcher },
    { VK_F1,
      RuntimeInputAction::CycleAttachedCameraSubmode,
      kKeyboardUnblockedContext | InputBindingContext::AttachedCamera },
    { VK_RETURN,
      RuntimeInputAction::ToggleAttachedCameraPin,
      kKeyboardUnblockedContext | InputBindingContext::AttachedCamera },
    { 'B', RuntimeInputAction::ToggleDirectorGrab, kKeyboardUnblockedContext | InputBindingContext::Director },
    { 'J',
      RuntimeInputAction::SetDirectorPhasePose,
      kKeyboardUnblockedContext | InputBindingContext::DirectorAuthoring },
    { 'K', RuntimeInputAction::StepDirectorPhase, kKeyboardUnblockedContext | InputBindingContext::DirectorAuthoring },
    { 'L',
      RuntimeInputAction::SaveDirectorShotList,
      kKeyboardUnblockedContext | InputBindingContext::DirectorAuthoring },
    { VK_RETURN,
      RuntimeInputAction::WriteLauncherReproSnapshot,
      kKeyboardUnblockedContext | InputBindingContext::Launcher | InputBindingContext::ReplayRestoreNotConsumed |
          InputBindingContext::DebugOnly },
    { VK_MENU, RuntimeInputAction::ToggleEditorTool, kKeyboardUnblockedContext },
    { '1', RuntimeInputAction::ToggleWaterFreeze, kKeyboardUnblockedContext },
    { '2', RuntimeInputAction::CycleWaterReflection, kKeyboardUnblockedContext },
    { '3', RuntimeInputAction::ToggleWaterFlat, kKeyboardUnblockedContext },
    { '4', RuntimeInputAction::ToggleTerrainHidden, kKeyboardUnblockedContext },
    { '5', RuntimeInputAction::ToggleWaterHidden, kKeyboardUnblockedContext },
    { 'V', RuntimeInputAction::ToggleCollisionVisualizer, kKeyboardUnblockedContext },
    { 'C', RuntimeInputAction::CyclePhysicsDebugOverlay, kKeyboardUnblockedContext },
    { 'O', RuntimeInputAction::ToggleTerrainContactProbe, kKeyboardUnblockedContext },
    { VK_F7, RuntimeInputAction::StepPhysicsPipelinePrevious, kKeyboardUnblockedContext },
    { VK_F8, RuntimeInputAction::StepPhysicsPipelineNext, kKeyboardUnblockedContext },
    { '6', RuntimeInputAction::TogglePhysicsDebugTransparent, kKeyboardUnblockedContext },
    { 'Q', RuntimeInputAction::ReportRendererRuntimeRetired, kKeyboardUnblockedContext },
    { 'P', RuntimeInputAction::ToggleCrossScenePause, kKeyboardUnblockedContext },
    { 'G', RuntimeInputAction::ToggleBroadphaseOverlay, kKeyboardUnblockedContext },
    { '0', RuntimeInputAction::ToggleUIVisibility, kKeyboardUnblockedContext },
    { VK_F5, RuntimeInputAction::TogglePerformanceHistogram, kKeyboardUnblockedContext },
    { VK_F6, RuntimeInputAction::ToggleMemoryOverlay, kKeyboardUnblockedContext },
    { VK_LEFT, RuntimeInputAction::NavigateScenePrevious, kKeyboardUnblockedContext },
    { VK_RIGHT, RuntimeInputAction::NavigateSceneNext, kKeyboardUnblockedContext },
    { VK_ESCAPE, RuntimeInputAction::DismissOrExitUI, kAfterUIUpdateContext | InputBindingContext::UINotInteracted },
    { VK_F2, RuntimeInputAction::SaveSceneSnapshot, kCaptureContext },
    { VK_F3, RuntimeInputAction::SaveScreenshot, kCaptureContext },
    { 'R', RuntimeInputAction::ResetScene, kAfterUIUpdateContext },
    { VK_BACK, RuntimeInputAction::ResetSceneFromBackspace, kAfterUIUpdateContext | InputBindingContext::Scene } };
constexpr std::size_t kTakeInputKeyboardBindingCount =
    sizeof( kTakeInputKeyboardBindings ) / sizeof( kTakeInputKeyboardBindings[0] );

void AdvanceTakeInputKeyboardActionMemories( RuntimeInputContext& input )
{
    for ( std::size_t i = 0; i < kTakeInputKeyboardBindingCount; ++i )
    {
        input.SetActionDown( kTakeInputKeyboardBindings[i].action,
                             Input::IsKeyDown( kTakeInputKeyboardBindings[i].virtualKey ) );
    }
}

} // namespace

void Run::UpdateRuntimeInputModeAfterAction( RuntimeInputAction action, RuntimeInputActionSource source )
{
    InputController::ApplyModeAction(
        m_runtimeInput,
        InputController::ResolveMode( BuildRuntimeInputModeState( m_camera.mode,
                                                                  m_runtimeTools.Editor(),
                                                                  m_attachedCamera.activeFollow,
                                                                  m_camera.director.grabbed ) ),
        action,
        source );
}


RuntimeInputSnapshot Run::BuildRuntimeInputSnapshot( const RuntimeMouseEdges& mouseEdges,
                                                     bool suppressWorldActionThisFrame ) const
{
    const POINT mouse = Input::GetClientMouseCoordinates();

    RuntimeInputSnapshot snapshot;
    snapshot.appFocused = Input::IsAppFocused();
    snapshot.uiBlocksKeyboard = m_UI.BlocksKeyboard();
    snapshot.uiBlocksMouse = m_UI.BlocksCameraMouse();

    snapshot.pointer.clientX = mouse.x;
    snapshot.pointer.clientY = mouse.y;
    snapshot.pointer.leftDown = mouseEdges.leftDown;
    snapshot.pointer.leftPressed = mouseEdges.leftPressed;
    snapshot.pointer.leftReleased = mouseEdges.leftReleased;
    snapshot.pointer.rightDown = mouseEdges.rightDown;
    snapshot.pointer.rightPressed = mouseEdges.rightPressed;
    snapshot.pointer.rightReleased = mouseEdges.rightReleased;
    snapshot.pointer.controlDown = Input::IsKeyDown( VK_CONTROL );
    snapshot.pointer.shiftDown = Input::IsKeyDown( VK_SHIFT );
    snapshot.pointer.uiWantsNativeMouseCursor = m_UI.WantsNativeMouseCursor();
    snapshot.pointer.uiBlocksCameraMouse = m_UI.BlocksCameraMouse();
    snapshot.pointer.suppressWorldAction = suppressWorldActionThisFrame;

    if ( mouseEdges.leftPressed || mouseEdges.leftReleased || mouseEdges.leftDown )
    {
        snapshot.pointer.button = RuntimePointerButton::Left;
    }
    else if ( mouseEdges.rightPressed || mouseEdges.rightReleased || mouseEdges.rightDown )
    {
        snapshot.pointer.button = RuntimePointerButton::Right;
    }

    snapshot.frameInput =
        RuntimeInteractionFrameInput{ SceneState().isScenePhysics,
                                      Input::IsKeyDown( VK_SPACE ),
                                      m_replayRuntime.IsScrubPaused(),
                                      m_replayRuntime.Scrubber().liveAdvanceHeld,
                                      mouseEdges.rightDown,
                                      m_runtimeTools.Editor().viewportLookActive,
                                      m_replayRuntime.InspectionMouseLookActive( Input::IsRightMouseDown(),
                                                                                 m_UI.WantsNativeMouseCursor(),
                                                                                 m_UI.BlocksCameraMouse() ),
                                      false,
                                      SceneState().timeScale };
    return snapshot;
}


bool Run::RouteRuntimePointerInput( const RuntimeInputSnapshot& inputSnapshot, const RuntimeMouseEdges& mouseEdges )
{
    const bool leftPressed = inputSnapshot.pointer.leftPressed;
    const bool suppressWorldAction = inputSnapshot.pointer.suppressWorldAction;
    const bool uiWantsNativeMouseCursor = inputSnapshot.pointer.uiWantsNativeMouseCursor;

    if ( m_interaction.PointerCapture() == RuntimePointerCaptureOwner::CameraLook )
    {
        return false;
    }

    bool consumedWorldClick = TickEditorWorldClick( mouseEdges, suppressWorldAction );
    if ( !consumedWorldClick )
    {
        consumedWorldClick = TickMousePickupInput( m_systems.window ? m_systems.window->m_sWindow : nullptr,
                                                   mouseEdges,
                                                   suppressWorldAction );
    }
    if ( !consumedWorldClick )
    {
        consumedWorldClick = TickAttachedCameraWorldClick( mouseEdges, suppressWorldAction );
    }
    if ( !consumedWorldClick && leftPressed && !suppressWorldAction && !m_runtimeTools.Editor().editorModeEnabled &&
         !uiWantsNativeMouseCursor && ( inputSnapshot.pointer.controlDown || !IsLauncherCameraMode() ) )
    {
        const bool additiveReplayPick = inputSnapshot.pointer.shiftDown;
        TryPickReplayPathTargetFromMouse( additiveReplayPick, !additiveReplayPick );
        consumedWorldClick = true;
    }

    if ( !consumedWorldClick && IsLauncherCameraMode() && leftPressed && !suppressWorldAction &&
         !uiWantsNativeMouseCursor )
    {
        EnterInteractiveSceneRun();
        Vector3 rayOrigin;
        Vector3 rayDirection;
        Vector3 cameraUp;
        if ( m_runtimeTools.TryBuildLauncherCameraRay( m_systems.cameras, rayOrigin, rayDirection, cameraUp ) )
        {
            m_replayRuntime.RecordLauncherFireEvent(
                rayOrigin,
                rayDirection,
                cameraUp,
                m_runtimeTools.RayCastTest().fireMode == RunLauncherFireMode::Projectile,
                m_runtimeTools.RayCastTest().impulseStrength,
                m_runtimeTools.RayCastTest().projectileSpeed,
                m_cGameModelCollection.SceneEntityCount() );
            // Why: RuntimeTools now fails closed unless Run has completed the
            // cold collection-to-store topology repair at the owner boundary.
            const bool launcherStoresReady = m_cGameModelCollection.RepairPhysicsBodyAndColliderTopology();
            if ( launcherStoresReady && m_runtimeTools.FireLauncherRay( m_cGameModelCollection,
                                                                        SceneState(),
                                                                        m_systems.terrain.get(),
                                                                        m_startup.gameModelCapacity,
                                                                        rayOrigin,
                                                                        rayDirection,
                                                                        cameraUp ) )
            {
                SceneState().modelCount = m_cGameModelCollection.SceneEntityCount();
            }
        }
        UpdateRuntimeInputModeAfterAction( RuntimeInputAction::FireLauncher, RuntimeInputActionSource::Mouse );
        consumedWorldClick = true;
    }

    return consumedWorldClick;
}


void Run::CancelCameraLookGesture()
{
    if ( m_interaction.PointerCapture() == RuntimePointerCaptureOwner::CameraLook )
    {
        m_interaction.EndGesture( InteractionExitReason::EndGesture );
    }
}


void Run::SyncCameraLookGesture( const RuntimeInputSnapshot& inputSnapshot,
                                 const RuntimeInteractionFramePolicy& inputPolicy,
                                 bool mouseLookOwnsCursor )
{
    const bool cameraLookCaptured = m_interaction.PointerCapture() == RuntimePointerCaptureOwner::CameraLook;
    const bool wantsCameraLook = inputSnapshot.appFocused && mouseLookOwnsCursor && inputPolicy.cameraMouseLookActive;

    if ( !wantsCameraLook )
    {
        CancelCameraLookGesture();
        return;
    }

    if ( cameraLookCaptured || m_interaction.PointerCapture() != RuntimePointerCaptureOwner::None ||
         m_interaction.Gesture().kind != RuntimeInteractionGestureKind::None )
    {
        return;
    }

    RuntimeInteractionGesture gesture;
    gesture.kind = RuntimeInteractionGestureKind::CameraLook;
    gesture.button = inputSnapshot.pointer.rightDown ? RuntimePointerButton::Right : RuntimePointerButton::None;
    gesture.startX = inputSnapshot.pointer.clientX;
    gesture.startY = inputSnapshot.pointer.clientY;
    m_interaction.BeginGesture( gesture, RuntimePointerCaptureOwner::CameraLook, InteractionExitReason::BeginGesture );
}


void Run::BeginReplayToolGesture( RuntimeInteractionGestureKind kind,
                                  WorldInteractionOwner owner,
                                  RuntimePointerButton button,
                                  int startX,
                                  int startY,
                                  int modelIndex,
                                  int axis,
                                  bool angular )
{
    SetWorldInteractionOwnerAfterInteractionTransition( owner, InteractionExitReason::BeginGesture );

    RuntimeInteractionGesture gesture;
    gesture.kind = kind;
    gesture.button = button;
    gesture.startX = startX;
    gesture.startY = startY;
    gesture.modelIndex = modelIndex;
    gesture.axis = axis;
    gesture.angular = angular;
    m_interaction.BeginGesture( gesture, RuntimePointerCaptureOwner::ToolGesture, InteractionExitReason::BeginGesture );
}


void Run::EndReplayToolGesture( RuntimeInteractionGestureKind kind )
{
    if ( m_interaction.Gesture().kind == kind )
    {
        m_interaction.EndGesture( InteractionExitReason::EndGesture );
    }
}


void Run::CancelReplayToolGesture()
{
    switch ( m_interaction.Gesture().kind )
    {
    case RuntimeInteractionGestureKind::ReplayScrubDrag:
    case RuntimeInteractionGestureKind::ReplayVelocityDrag:
    case RuntimeInteractionGestureKind::ReplayPredictionHorizonDrag:
    case RuntimeInteractionGestureKind::ReplayCauseTreeDrag:
        m_interaction.EndGesture( InteractionExitReason::EndGesture );
        break;
    default:
        break;
    }
}


void Run::CancelReplayToolDragState()
{
    CancelReplayToolGesture();
    if ( m_replayRuntime.Scrubber().mouseCaptured || m_replayRuntime.VelocityEdit().mouseCaptured ||
         m_replayRuntime.CauseTree().draggingWindow || m_replayRuntime.CauseTree().resizingWindow )
    {
        UI::InputControl::EndMouseCapture();
    }

    m_replayRuntime.Scrubber().dragging = false;
    m_replayRuntime.Scrubber().mouseCaptured = false;
    m_replayRuntime.Prediction().horizonDragging = false;
    m_replayRuntime.VelocityEdit().dragging = false;
    m_replayRuntime.VelocityEdit().draggingAngular = false;
    m_replayRuntime.VelocityEdit().activeAxis = -1;
    m_replayRuntime.VelocityEdit().mouseCaptured = false;
    m_replayRuntime.CauseTree().draggingWindow = false;
    m_replayRuntime.CauseTree().resizingWindow = false;
}


RuntimeInteractionTransition Run::EnterInteractionForCameraMode( RunCameraMode mode )
{
    return m_interaction.EnterCameraMode( NormalizeCameraModeForCurrentScene( mode ) );
}


bool Run::HasActiveReplayInteractionState() const
{
    return m_replayRuntime.Camera().active || m_replayRuntime.Camera().focusKind != RunReplayCameraFocusKind::None ||
           m_replayRuntime.Scrubber().dragging || m_replayRuntime.Scrubber().historicalSamplePaused ||
           m_replayRuntime.Scrubber().liveAdvanceHeld || m_replayRuntime.Scrubber().mouseCaptured ||
           m_replayRuntime.PathVisualizer().hasTarget || !m_replayRuntime.PathVisualizer().targets.empty() ||
           m_replayRuntime.Prediction().enabled || m_replayRuntime.Prediction().horizonDragging ||
           m_replayRuntime.Prediction().building || m_replayRuntime.VelocityEdit().enabled ||
           m_replayRuntime.VelocityEdit().dragging || m_replayRuntime.VelocityEdit().mouseCaptured ||
           m_replayRuntime.CauseTree().draggingWindow || m_replayRuntime.CauseTree().resizingWindow ||
           m_replayRuntime.CauseTree().selectedRow >= 0 || !m_replayRuntime.CauseTree().rows.empty();
}


bool Run::HasActiveEditorInteractionState() const
{
    return m_runtimeTools.Editor().editorModeEnabled || m_runtimeTools.Editor().placementModeEnabled ||
           m_runtimeTools.Editor().viewportLookActive || m_runtimeTools.Editor().placementPreviewVisible ||
           m_runtimeTools.Editor().placementScaleActive || m_runtimeTools.Editor().gizmoDragActive ||
           m_runtimeTools.Editor().hotGizmoAxis >= 0 || m_runtimeTools.Editor().hotRotationAxis >= 0 ||
           m_runtimeTools.Editor().activeGizmoAxis >= 0;
}


bool Run::InspectGizmoInteractionActive() const
{
    return !m_runtimeTools.Editor().editorModeEnabled && m_camera.mode == RunCameraMode::Inspect &&
           !m_replayRuntime.InspectionActive();
}


void Run::ClearReplayInteractionForRuntimeTransition()
{
    CancelReplayToolDragState();

    m_replayRuntime.SetLiveAdvanceHeld( false );
    if ( m_replayRuntime.ResetScrubberState() )
    {
        ExitReplayInspectionCamera();
    }
    m_replayRuntime.SetAllTrackPositions( 1.0f );
    m_replayRuntime.Scrubber().visible = false;
    m_replayRuntime.Scrubber().visibleAlpha = 0.0f;
    m_replayRuntime.Scrubber().fadeUpdatedAt = 0.0;
    m_replayRuntime.Scrubber().dragging = false;
    m_replayRuntime.Scrubber().mouseCaptured = false;
    m_replayRuntime.Scrubber().branchHovered = false;
    m_replayRuntime.Scrubber().pauseHovered = false;
    m_replayRuntime.Scrubber().saveHovered = false;
    m_replayRuntime.Scrubber().loadHovered = false;

    m_replayRuntime.ClearCameraFocusForRestore();
    ExitReplayInspectionCamera();
    m_replayRuntime.ClearPathVisualizerState();
    m_replayRuntime.Prediction().enabled = false;
    m_replayRuntime.Prediction().checkboxHovered = false;
    m_replayRuntime.Prediction().decreaseHovered = false;
    m_replayRuntime.Prediction().increaseHovered = false;
    m_replayRuntime.Prediction().horizonHovered = false;
    m_replayRuntime.Prediction().horizonDragging = false;
    m_replayRuntime.ClearPredictionCache();

    m_replayRuntime.VelocityEdit() = RunReplayVelocityEditState{};
    m_replayRuntime.CauseTree().hoveredRow = -1;
    m_replayRuntime.CauseTree().selectedRow = -1;
    m_replayRuntime.CauseTree().draggingWindow = false;
    m_replayRuntime.CauseTree().resizingWindow = false;
    m_replayRuntime.CauseTree().leftWasDown = false;
    m_replayRuntime.CauseTree().scrollY = 0.0f;
    m_replayRuntime.CauseTree().rows.clear();

    ExitReplayInspectionCamera();
    ApplyCursorOwnership();
}


void Run::ClearEditorInteractionForRuntimeTransition( bool clearSelection )
{
    RunInternal::ClearEditorManipulationState( { m_runtimeTools.Editor(), m_cGameModelCollection, m_interaction } );
    m_runtimeTools.Editor().viewportLookActive = false;
    m_runtimeTools.Editor().placementModeEnabled = false;
    m_runtimeTools.Editor().hotGizmoAxis = -1;
    m_runtimeTools.Editor().hotRotationAxis = -1;
    m_runtimeTools.Editor().activeGizmoAxis = -1;
    if ( clearSelection )
    {
        RuntimeInteractionCommand command;
        command.type = RuntimeInteractionCommandType::SetEditorSelection;
        command.modelIndex = -1;
        command.claimSelectionOwner = false;
        ExecuteRuntimeInteractionCommand( command );
    }
    ReleaseMouseToUI();
    ApplyCursorOwnership();
}


void Run::ClearRuntimeInteractionStateForTransition( const RuntimeInteractionTransition& transition )
{
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

    if ( !enteringReplay && ( HasActiveReplayInteractionState() || IsReplayWorldOwner( transition.previousOwner ) ) )
    {
        ClearReplayInteractionForRuntimeTransition();
    }

    if ( transition.previousOwner == WorldInteractionOwner::Manipulator &&
         transition.owner != WorldInteractionOwner::Manipulator )
    {
        CancelMousePickup();
    }
    if ( enteringTool && transition.owner != WorldInteractionOwner::Manipulator )
    {
        CancelMousePickup();
    }

    if ( ( !enteringEdit && !inspectGizmoClaimWithinInspect && HasActiveEditorInteractionState() ) ||
         ( IsEditorWorldOwner( transition.previousOwner ) && !editorOwnerSwitchWithinEdit &&
           !inspectGizmoClaimWithinInspect ) )
    {
        ClearEditorInteractionForRuntimeTransition( enteringReplay || enteringTool );
        if ( m_runtimeTools.Editor().editorModeEnabled && !enteringEdit )
        {
            m_runtimeTools.Editor().editorModeEnabled = false;
        }
    }
}


void Run::ApplyRuntimeInteractionTransitionCleanup( const RuntimeInteractionTransition& transition )
{
    ClearRuntimeInteractionStateForTransition( transition );
    switch ( transition.owner )
    {
    case WorldInteractionOwner::Launcher:
        m_interaction.EnterLauncher();
        break;
    case WorldInteractionOwner::Manipulator:
        m_interaction.EnterManipulator();
        break;
    default:
        if ( transition.workspace == RuntimeWorkspace::Edit )
        {
            m_interaction.EnterEdit();
        }
        else if ( transition.workspace == RuntimeWorkspace::Replay )
        {
            m_interaction.EnterReplay();
        }
        else if ( transition.workspace == RuntimeWorkspace::Inspect )
        {
            m_interaction.EnterInspect();
        }
        else
        {
            m_interaction.EnterLive();
        }
        break;
    }
}


RuntimeInteractionTransition Run::SetWorldInteractionOwnerAfterInteractionTransition( WorldInteractionOwner owner,
                                                                                      InteractionExitReason reason )
{
    const RuntimeWorkspace workspace = WorkspaceForWorldInteractionOwner( m_interaction.Workspace(), owner );
    const RuntimeInteractionTransition transition =
        m_interaction.SetWorldInteractionOwnerInWorkspace( workspace, owner, reason );
    ClearRuntimeInteractionStateForTransition( transition );
    m_interaction.SetWorldInteractionOwnerInWorkspace( workspace, owner, reason );
    return transition;
}


void Run::PublishRuntimeInteractionEvent( const RuntimeInteractionEvent& event )
{
    switch ( event.type )
    {
    case RuntimeInteractionEventType::SelectionChanged:
        Log().WriteEventf( "runtime_interaction_command_event type=%s scope=%s previous_model=%d model=%d",
                           RuntimeInteractionEventName( event.type ),
                           RuntimeInteractionSelectionScopeName( event.selectionScope ),
                           event.previousModelIndex,
                           event.modelIndex );
        break;
    case RuntimeInteractionEventType::None:
        break;
    }
}


bool Run::ExecuteRuntimeInteractionCommand( const RuntimeInteractionCommand& command )
{
    switch ( command.type )
    {
    case RuntimeInteractionCommandType::SetEditorSelection:
    {
        if ( command.modelIndex < -1 || command.modelIndex >= m_cGameModelCollection.SceneEntityCount() )
        {
            return false;
        }

        const PhysicsBodyStore& bodyStore = m_cGameModelCollection.GetPhysicsEngine().BodyStore();
        const ColliderStore& colliderStore = m_cGameModelCollection.GetPhysicsEngine().Colliders();
        const int previousModelIndex = ResolveSelectedEditorModelIndex( m_runtimeTools.Editor(), bodyStore );
        const bool selectionHit = command.modelIndex >= 0;
        PhysicsBodyHandle selectedBody;
        PhysicsColliderHandle selectedCollider;
        if ( selectionHit )
        {
            // Invariant: positive selection commands prove identity with
            // handles. The model index only checks the paired UI row.
            selectedBody = command.body;
            selectedCollider = command.collider;
            const PhysicsBodyRecord* body = bodyStore.RecordForHandle( selectedBody );
            const ColliderRecord* collider = colliderStore.RecordForHandle( selectedCollider );
            if ( !body || !collider || bodyStore.ModelIndexForHandle( selectedBody ) != command.modelIndex ||
                 colliderStore.ModelIndexForHandle( selectedCollider ) != command.modelIndex ||
                 collider->body != selectedBody )
            {
                return false;
            }
        }

        const PhysicsBodyHandle previousBody = m_runtimeTools.Editor().selectedBody;
        const PhysicsColliderHandle previousCollider = m_runtimeTools.Editor().selectedCollider;
        const bool inspectSelection = command.selectionScope == RuntimeInteractionSelectionScope::Inspect;
        if ( command.claimSelectionOwner )
        {
            const WorldInteractionOwner owner = selectionHit ? ( inspectSelection ? WorldInteractionOwner::InspectGizmo
                                                                                  : WorldInteractionOwner::EditorGizmo )
                                                             : WorldInteractionOwner::None;
            const InteractionExitReason reason =
                inspectSelection ? InteractionExitReason::EnterInspect : InteractionExitReason::EnterEdit;
            SetWorldInteractionOwnerAfterInteractionTransition( owner, reason );
        }
        m_runtimeTools.Editor().selectedModelRow.value = command.modelIndex;
        m_runtimeTools.Editor().selectedBody = selectedBody;
        m_runtimeTools.Editor().selectedCollider = selectedCollider;
        if ( previousModelIndex != command.modelIndex || previousBody != selectedBody ||
             previousCollider != selectedCollider )
        {
            RuntimeInteractionEvent event;
            event.type = RuntimeInteractionEventType::SelectionChanged;
            event.previousModelIndex = previousModelIndex;
            event.modelIndex = command.modelIndex;
            event.previousBody = previousBody;
            event.body = selectedBody;
            event.previousCollider = previousCollider;
            event.collider = selectedCollider;
            event.selectionScope = command.selectionScope;
            PublishRuntimeInteractionEvent( event );
        }
        return true;
    }
    case RuntimeInteractionCommandType::None:
        break;
    }

    return false;
}


const char* Run::CameraModeLabel( RunCameraMode mode ) const
{
    switch ( mode )
    {
    case RunCameraMode::Demo:
        return "Demo";
    case RunCameraMode::Scene:
        return "Scene";
    case RunCameraMode::Inspect:
        return "Inspect";
    case RunCameraMode::Attach:
    {
        static thread_local char label[96];
        const char* submode = "Fixed";
        if ( m_attachedCamera.submode == AttachedCameraSubmode::VelocityForward )
        {
            submode = "Velocity";
        }
        else if ( m_attachedCamera.submode == AttachedCameraSubmode::RagdollEyes )
        {
            submode = "Eyes";
        }
        if ( m_attachedCamera.target.modelIndex < 0 )
        {
            sprintf_s( label,
                       sizeof( label ),
                       "Attach: pick target%s",
                       m_attachedCamera.activeFollow ? "" : " Pinned" );
        }
        else
        {
            sprintf_s( label,
                       sizeof( label ),
                       "Attach: %s %s%s",
                       submode,
                       m_attachedCamera.target.name[0] ? m_attachedCamera.target.name : "target",
                       m_attachedCamera.activeFollow ? "" : " Pinned" );
        }
        return label;
    }
    case RunCameraMode::Launcher:
        return "Launcher";
    case RunCameraMode::Manipulator:
        return "Manipulator";
    case RunCameraMode::Director:
        return "Director";
    default:
        return "Unknown";
    }
}


bool Run::IsDemoCameraModeAvailable() const
{
    if ( SceneState().isSceneMode )
    {
        return false;
    }
    return m_cGameModelCollection.SceneEntityCount() > 0;
}


RunCameraMode Run::NormalizeCameraModeForCurrentScene( RunCameraMode mode ) const
{
    if ( SceneState().isSceneMode )
    {
        return mode == RunCameraMode::Demo ? RunCameraMode::Scene : mode;
    }
    if ( mode == RunCameraMode::Scene )
    {
        return IsDemoCameraModeAvailable() ? RunCameraMode::Demo : RunCameraMode::Inspect;
    }
    if ( mode == RunCameraMode::Demo && !IsDemoCameraModeAvailable() )
    {
        return RunCameraMode::Inspect;
    }
    return mode;
}


void Run::SetCameraModeLabelAfterInteractionTransition( RunCameraMode mode )
{
    const int modeIndex = static_cast<int>( mode );
    if ( modeIndex < 0 || modeIndex >= static_cast<int>( RunCameraMode::Count ) )
    {
        return;
    }

    m_camera.mode = mode;
}


bool Run::IsFlyCameraMode() const
{
    return CameraModeUsesFlyControls( m_camera.mode, m_attachedCamera.activeFollow, m_camera.director.grabbed );
}


bool Run::IsManualCameraMode() const
{
    return IsFlyCameraMode() || IsAttachedCameraMode() || m_camera.mode == RunCameraMode::Director;
}


bool Run::IsLauncherCameraMode() const
{
    return CameraModeUsesLauncher( m_camera.mode );
}


bool Run::IsManipulatorCameraMode() const
{
    return m_camera.mode == RunCameraMode::Manipulator;
}


bool Run::IsAttachedCameraMode() const
{
    return m_camera.mode == RunCameraMode::Attach;
}


void Run::ResetAttachedCamera()
{
    m_attachedCamera = AttachedCameraState{};
}


void Run::CaptureAttachedCameraReturnState( RunCameraMode previousMode )
{
    previousMode = NormalizeCameraModeForCurrentScene( previousMode );
    if ( previousMode == RunCameraMode::Attach )
    {
        return;
    }

    m_camera.modeBeforeAttach = previousMode;
    m_attachedCamera.hasReturnCameraPose = false;
    if ( !m_systems.cameras )
    {
        return;
    }

    // Why: capture the render pose, not only the selected camera slot. The
    // player may enter Attach while another transition is still visible.
    m_attachedCamera.returnCameraHash = m_systems.cameras->GetSelectedCameraName();
    m_attachedCamera.returnEye = m_systems.cameras->GetRenderCameraTranslation();
    m_attachedCamera.returnView = m_systems.cameras->GetRenderCameraView();
    m_attachedCamera.returnUp = m_systems.cameras->GetRenderCameraUp();
    if ( VectorMagSquared( m_attachedCamera.returnView - m_attachedCamera.returnEye ) <= TOLERANCE * TOLERANCE )
    {
        m_attachedCamera.returnEye = m_systems.cameras->GetCameraTranslation();
        m_attachedCamera.returnView = m_systems.cameras->GetCameraView();
        m_attachedCamera.returnUp = m_systems.cameras->GetCameraUp();
    }
    m_attachedCamera.hasReturnCameraPose = true;
}


void Run::RestoreAttachedCameraReturnState()
{
    if ( !m_attachedCamera.hasReturnCameraPose || !m_systems.cameras )
    {
        return;
    }

    // Why: switching the logical slot without tweening keeps the previous render
    // pose alive as the source for TweenPrimaryToPose below.
    if ( m_systems.cameras->HasCamera( m_attachedCamera.returnCameraHash ) &&
         !m_systems.cameras->IsCameraSelected( m_attachedCamera.returnCameraHash ) )
    {
        m_systems.cameras->SelectCamera( m_attachedCamera.returnCameraHash, false );
    }
    m_systems.cameras->TweenPrimaryToPose( m_attachedCamera.returnEye,
                                           m_attachedCamera.returnView,
                                           m_attachedCamera.returnUp );
    m_attachedCamera.hasReturnCameraPose = false;
}


void Run::ClearAttachedCameraTarget()
{
    AttachedCameraController::ClearTarget( m_attachedCamera );
}


bool Run::TryResolveAttachedCameraTarget( int& outModelIndex )
{
    if ( AttachedCameraController::TryResolveTargetIdentity( m_cGameModelCollection,
                                                             m_attachedCamera.target,
                                                             outModelIndex ) )
    {
        return true;
    }

    ClearAttachedCameraTarget();
    return false;
}


void Run::CaptureAttachedCameraFixedOffset( const Vector3& targetPosition,
                                            const RotationMatrix& targetRotation,
                                            float targetRadius )
{
    if ( !m_systems.cameras )
    {
        return;
    }

    AttachedCameraPhysicsTarget target;
    target.position = targetPosition;
    target.rotation = targetRotation;
    target.radius = targetRadius;
    AttachedCameraController::CaptureFixedOffset( m_attachedCamera,
                                                  AttachedCameraPoseFromCameras( *m_systems.cameras ),
                                                  target );
}


void Run::CaptureAttachedCameraOrbit( const Vector3& targetPosition, float targetRadius )
{
    if ( !m_systems.cameras )
    {
        return;
    }

    AttachedCameraPhysicsTarget target;
    target.position = targetPosition;
    target.radius = targetRadius;
    AttachedCameraController::CaptureOrbit( m_attachedCamera,
                                            AttachedCameraPoseFromCameras( *m_systems.cameras ),
                                            target );
}


void Run::SetAttachedCameraTarget( int modelIndex )
{
    const int modelCount = m_cGameModelCollection.GetPhysicsEngine().BodyStore().Count();
    if ( modelIndex < 0 || modelIndex >= modelCount )
    {
        ClearAttachedCameraTarget();
        return;
    }

    AttachedCameraPhysicsTarget targetState;
    if ( !AttachedCameraController::TryAttachTargetHandlesFromModelIndex( m_cGameModelCollection,
                                                                          modelIndex,
                                                                          m_attachedCamera.target ) ||
         !AttachedCameraController::TryResolvePhysicsTarget( m_cGameModelCollection,
                                                             m_attachedCamera.target,
                                                             targetState ) )
    {
        ClearAttachedCameraTarget();
        return;
    }

    strncpy_s( m_attachedCamera.target.name,
               sizeof( m_attachedCamera.target.name ),
               PresentationNameForModelIndex( m_cGameModelCollection, modelIndex ),
               _TRUNCATE );
    RuntimeInteractionCommand command;
    command.type = RuntimeInteractionCommandType::SetEditorSelection;
    command.modelIndex = modelIndex;
    command.body = m_attachedCamera.target.body;
    command.collider = m_attachedCamera.target.collider;
    command.selectionScope = RuntimeInteractionSelectionScope::Inspect;
    command.claimSelectionOwner = false;
    ExecuteRuntimeInteractionCommand( command );
    m_attachedCamera.activeFollow = true;
    m_attachedCamera.needsEntryTween = true;
    if ( m_attachedCamera.submode == AttachedCameraSubmode::RagdollEyes )
    {
        int headIndex = -1;
        if ( !AttachedCameraController::TryResolveRagdollHead( m_cGameModelCollection, modelIndex, headIndex ) )
        {
            m_attachedCamera.submode = AttachedCameraSubmode::FixedRelative;
        }
    }
    CaptureAttachedCameraFixedOffset( targetState.position, targetState.rotation, targetState.radius );
    ApplyCursorOwnership();
}


void Run::SeedAttachedCameraTargetFromSelection()
{
    AttachedCameraPhysicsTarget currentState;
    if ( AttachedCameraController::TryResolvePhysicsTarget( m_cGameModelCollection,
                                                            m_attachedCamera.target,
                                                            currentState ) )
    {
        CaptureAttachedCameraFixedOffset( currentState.position, currentState.rotation, currentState.radius );
        m_attachedCamera.activeFollow = true;
        ApplyCursorOwnership();
        return;
    }

    int seedIndex = -1;
    const RunReplayPathVisualizerState& path = m_replayRuntime.PathVisualizer();
    const PhysicsBodyStore& bodyStore = m_cGameModelCollection.GetPhysicsEngine().BodyStore();
    const int modelCount = bodyStore.Count();
    if ( path.hasTarget && path.targetModelIndex >= 0 && path.targetModelIndex < modelCount )
    {
        seedIndex = path.targetModelIndex;
    }
    else
    {
        const int selectedModelIndex = ResolveSelectedEditorModelIndex( m_runtimeTools.Editor(), bodyStore );
        if ( selectedModelIndex >= 0 && selectedModelIndex < modelCount )
        {
            seedIndex = selectedModelIndex;
        }
    }

    if ( seedIndex >= 0 )
    {
        SetAttachedCameraTarget( seedIndex );
    }
    else
    {
        m_attachedCamera.activeFollow = true;
        ApplyCursorOwnership();
    }
}


bool Run::TryPickAttachedCameraTargetFromMouse()
{
    Vector3 rayOrigin;
    Vector3 rayDirection;
    RuntimePickResult result;
    if ( TryBuildMouseWorldRay( rayOrigin, rayDirection ) )
    {
        RuntimePickRequest request;
        request.purpose = RuntimePickPurpose::AttachCameraTarget;
        request.bodyStore = &m_cGameModelCollection.GetPhysicsEngine().BodyStore();
        request.colliderStore = &m_cGameModelCollection.GetPhysicsEngine().Colliders();
        request.rayOrigin = rayOrigin;
        request.rayDirection = rayDirection;

        if ( RuntimePickService::TryPickModel( request, result ) )
        {
            SetAttachedCameraTarget( result.modelIndex );
        }
        else
        {
            ClearAttachedCameraTarget();
        }
    }
    else
    {
        ClearAttachedCameraTarget();
    }
    EnterInteractiveSceneRun();
    UpdateRuntimeInputModeAfterAction( RuntimeInputAction::SetCameraMode, RuntimeInputActionSource::Mouse );
    return true;
}


bool Run::TickAttachedCameraWorldClick( const RuntimeMouseEdges& mouseEdges, bool suppressWorldActionThisFrame )
{
    if ( !IsAttachedCameraMode() || !mouseEdges.leftPressed )
    {
        return false;
    }
    if ( suppressWorldActionThisFrame )
    {
        return false;
    }
    return TryPickAttachedCameraTargetFromMouse();
}


bool Run::TryResolveAttachedCameraRagdollHead( int selectedModelIndex, int& outHeadModelIndex ) const
{
    return AttachedCameraController::TryResolveRagdollHead( m_cGameModelCollection,
                                                            selectedModelIndex,
                                                            outHeadModelIndex );
}


void Run::CycleAttachedCameraSubmode()
{
    if ( !IsAttachedCameraMode() )
    {
        return;
    }
    int modelIndex = -1;
    AttachedCameraPhysicsTarget targetState;
    if ( !AttachedCameraController::TryResolvePhysicsTarget( m_cGameModelCollection,
                                                             m_attachedCamera.target,
                                                             targetState,
                                                             &modelIndex ) )
    {
        return;
    }

    int headIndex = -1;
    const bool hasEyes = TryResolveAttachedCameraRagdollHead( modelIndex, headIndex );
    AttachedCameraSubmode next = AttachedCameraSubmode::FixedRelative;
    if ( m_attachedCamera.submode == AttachedCameraSubmode::FixedRelative )
    {
        next = AttachedCameraSubmode::VelocityForward;
    }
    else if ( m_attachedCamera.submode == AttachedCameraSubmode::VelocityForward && hasEyes )
    {
        next = AttachedCameraSubmode::RagdollEyes;
    }

    m_attachedCamera.submode = next;
    m_attachedCamera.needsEntryTween = true;
    if ( next != AttachedCameraSubmode::RagdollEyes || !m_attachedCamera.hasFixedOffset )
    {
        CaptureAttachedCameraFixedOffset( targetState.position, targetState.rotation, targetState.radius );
    }
    UpdateRuntimeInputModeAfterAction( RuntimeInputAction::CycleAttachedCameraSubmode,
                                       RuntimeInputActionSource::Keyboard );
}


void Run::ToggleAttachedCameraPin()
{
    if ( !IsAttachedCameraMode() )
    {
        return;
    }

    m_attachedCamera.activeFollow = !m_attachedCamera.activeFollow;
    if ( m_attachedCamera.activeFollow )
    {
        AttachedCameraPhysicsTarget targetState;
        if ( AttachedCameraController::TryResolvePhysicsTarget( m_cGameModelCollection,
                                                                m_attachedCamera.target,
                                                                targetState ) )
        {
            CaptureAttachedCameraFixedOffset( targetState.position, targetState.rotation, targetState.radius );
        }
        m_attachedCamera.needsEntryTween = true;
    }
    else
    {
        ReleaseMouseToUI();
    }
    ApplyCursorOwnership();
    UpdateRuntimeInputModeAfterAction( RuntimeInputAction::ToggleAttachedCameraPin,
                                       RuntimeInputActionSource::Keyboard );
}


void Run::TickAttachedCameraOrbitInput( int unhandledWheelDelta )
{
    if ( !IsAttachedCameraMode() || !m_attachedCamera.activeFollow ||
         m_attachedCamera.submode == AttachedCameraSubmode::RagdollEyes || m_UI.BlocksCameraMouse() )
    {
        return;
    }

    AttachedCameraPhysicsTarget targetState;
    if ( !AttachedCameraController::TryResolvePhysicsTarget( m_cGameModelCollection,
                                                             m_attachedCamera.target,
                                                             targetState ) )
    {
        return;
    }
    if ( !m_attachedCamera.hasOrbit )
    {
        CaptureAttachedCameraOrbit( targetState.position, targetState.radius );
    }

    if ( AttachedCameraController::ApplyOrbitWheel( m_attachedCamera, targetState, unhandledWheelDelta ) )
    {
        EnterInteractiveSceneRun();
    }
}


void Run::TickAttachedCamera()
{
    if ( !IsAttachedCameraMode() || !m_attachedCamera.activeFollow || !m_systems.cameras )
    {
        return;
    }

    int modelIndex = -1;
    AttachedCameraPhysicsTarget targetState;
    if ( !AttachedCameraController::TryResolvePhysicsTarget( m_cGameModelCollection,
                                                             m_attachedCamera.target,
                                                             targetState,
                                                             &modelIndex ) )
    {
        return;
    }
    AttachedCameraPoseCommand poseCommand;
    const float orbitYawDelta =
        static_cast<float>( m_camera.input.xMove ) * CAMERA_MOUSE_REFERENCE_DT * m_config.mouseSensitivity;
    const float orbitPitchDelta =
        static_cast<float>( m_camera.input.yMove ) * CAMERA_MOUSE_REFERENCE_DT * m_config.mouseSensitivity;
    if ( !AttachedCameraController::BuildFollowPose( m_cGameModelCollection,
                                                     m_attachedCamera,
                                                     targetState,
                                                     modelIndex,
                                                     AttachedCameraPoseFromCameras( *m_systems.cameras ),
                                                     orbitYawDelta,
                                                     orbitPitchDelta,
                                                     poseCommand ) )
    {
        return;
    }

    // Why: follow cameras update their destination every frame. Only the first
    // valid solve after an Attach transition starts a tween; later solves
    // retarget that live destination without cutting the render pose.
    if ( poseCommand.startEntryTween )
    {
        m_systems.cameras->TweenPrimaryToPose( poseCommand.pose.eye, poseCommand.pose.view, poseCommand.pose.up );
    }
    else
    {
        m_systems.cameras->SetPrimaryPose( poseCommand.pose.eye, poseCommand.pose.view, poseCommand.pose.up );
    }
}


uint32_t Run::CameraModeEnabledMask() const
{
    uint32_t mask = 0;
    if ( IsDemoCameraModeAvailable() )
    {
        mask |= 1u << static_cast<int>( RunCameraMode::Demo );
    }
    if ( SceneState().isSceneMode )
    {
        mask |= 1u << static_cast<int>( RunCameraMode::Scene );
    }
    mask |= 1u << static_cast<int>( RunCameraMode::Inspect );
    mask |= 1u << static_cast<int>( RunCameraMode::Attach );
    mask |= 1u << static_cast<int>( RunCameraMode::Launcher );
    mask |= 1u << static_cast<int>( RunCameraMode::Manipulator );
    mask |= 1u << static_cast<int>( RunCameraMode::Director );
    return mask;
}


void Run::ApplyCameraMode( RunCameraMode mode, RuntimeInputActionSource source )
{
    const int modeIndex = static_cast<int>( mode );
    if ( modeIndex < 0 || modeIndex >= static_cast<int>( RunCameraMode::Count ) )
    {
        return;
    }
    const RunCameraMode previousMode = NormalizeCameraModeForCurrentScene( m_camera.mode );
    mode = NormalizeCameraModeForCurrentScene( mode );
    const bool enteringAttach = mode == RunCameraMode::Attach && previousMode != RunCameraMode::Attach;
    const bool leavingAttach = previousMode == RunCameraMode::Attach && mode != RunCameraMode::Attach;
    if ( enteringAttach )
    {
        CaptureAttachedCameraReturnState( previousMode );
    }

    if ( mode == RunCameraMode::Demo )
    {
        const int modelCount = m_cGameModelCollection.SceneEntityCount();
        if ( m_camera.trackBallIndex < 0 || m_camera.trackBallIndex >= modelCount )
        {
            m_camera.trackBallIndex = 0;
        }
        if ( m_camera.trackHeight <= 0.0f )
        {
            m_camera.trackHeight = 300.0f;
        }
    }
    if ( mode == RunCameraMode::Director && previousMode != RunCameraMode::Director )
    {
        DemoDirectorPlayback::EnterMode( m_camera, m_systems );
    }

    const RuntimeInteractionTransition transition = EnterInteractionForCameraMode( mode );
    ApplyRuntimeInteractionTransitionCleanup( transition );

    const bool wasFlyMode = IsFlyCameraMode();
    if ( mode != RunCameraMode::Launcher )
    {
        m_camera.modeBeforeLauncher = mode == RunCameraMode::Manipulator ? RunCameraMode::Inspect : mode;
    }
    SetCameraModeLabelAfterInteractionTransition( mode );
    if ( leavingAttach )
    {
        RestoreAttachedCameraReturnState();
    }
    if ( mode == RunCameraMode::Attach )
    {
        m_attachedCamera.activeFollow = true;
        m_attachedCamera.needsEntryTween = true;
    }
    if ( m_runtimeTools.Editor().editorModeEnabled )
    {
        m_runtimeTools.Editor().restoreCameraModeAfterEditor = mode;
    }
    if ( mode != RunCameraMode::Manipulator )
    {
        CancelMousePickup();
    }

    const bool isFlyMode = IsFlyCameraMode();
    if ( wasFlyMode != isFlyMode )
    {
        if ( isFlyMode )
        {
            EnterFlyModeCamera();
        }
        else
        {
            ExitFlyModeCamera();
        }
    }
    else
    {
        InputController::ResetMouseLook( m_camera );
        ApplyCursorOwnership();
    }
    if ( mode == RunCameraMode::Attach )
    {
        SeedAttachedCameraTargetFromSelection();
    }
    UpdateRuntimeInputModeAfterAction( source == RuntimeInputActionSource::UI ? RuntimeInputAction::SetCameraMode
                                                                              : RuntimeInputAction::CycleCameraMode,
                                       source );
}


void Run::CycleCameraMode()
{
    const uint32_t enabledMask = CameraModeEnabledMask();
    int current = static_cast<int>( m_camera.mode );
    if ( current < 0 || current >= static_cast<int>( RunCameraMode::Count ) )
    {
        current = static_cast<int>( SceneState().isSceneMode ? RunCameraMode::Scene : RunCameraMode::Demo );
    }

    if ( NormalizeCameraModeForCurrentScene( m_camera.mode ) == RunCameraMode::Attach )
    {
        const RunCameraMode restoreMode = NormalizeCameraModeForCurrentScene( m_camera.modeBeforeAttach );
        const int restoreIndex = static_cast<int>( restoreMode );
        // Why: Attach is a temporary follow workspace. Keyboard cycling out of
        // it should return to the camera mode that entered Attach, not continue
        // to the next enum value and strand the operator at the follow pose.
        if ( restoreIndex >= 0 && restoreIndex < static_cast<int>( RunCameraMode::Count ) &&
             ( enabledMask & ( 1u << restoreIndex ) ) != 0 )
        {
            ApplyCameraMode( restoreMode, RuntimeInputActionSource::Keyboard );
            return;
        }
    }

    for ( int step = 1; step <= static_cast<int>( RunCameraMode::Count ); ++step )
    {
        const int next = ( current + step ) % static_cast<int>( RunCameraMode::Count );
        if ( ( enabledMask & ( 1u << next ) ) != 0 )
        {
            ApplyCameraMode( static_cast<RunCameraMode>( next ), RuntimeInputActionSource::Keyboard );
            return;
        }
    }
}


bool Run::MouseLookOwnsCursor() const
{
    if ( !Input::IsAppFocused() )
    {
        return false;
    }

    if ( m_UI.BlocksCameraMouse() )
    {
        return false;
    }

    if ( m_runtimeTools.Editor().editorModeEnabled )
    {
        return m_runtimeTools.Editor().viewportLookActive || Input::IsRightMouseDown();
    }

    if ( m_replayRuntime.InspectionActive() )
    {
        return m_replayRuntime.InspectionMouseLookActive( Input::IsRightMouseDown(),
                                                          m_UI.WantsNativeMouseCursor(),
                                                          m_UI.BlocksCameraMouse() ) ||
               Input::IsRightMouseDown();
    }

    return Input::IsRightMouseDown();
}


bool Run::ShouldHideNativeCursor() const
{
    if ( MouseLookOwnsCursor() )
    {
        return true;
    }

    return m_runtimeTools.Editor().editorModeEnabled && m_runtimeTools.Editor().placementModeEnabled &&
           m_runtimeTools.Editor().placementPreviewVisible && !m_UI.WantsNativeMouseCursor() &&
           !m_UI.BlocksCameraMouse();
}


void Run::ApplyCursorOwnership()
{
    Input::SetSystemCursorVisible( !ShouldHideNativeCursor() );
}


void Run::ReleaseMouseToUI()
{
    if ( !MouseLookOwnsCursor() )
    {
        ReleaseCapture();
        InputController::ResetMouseLook( m_camera );
    }
}


void Run::EnterFlyModeCamera()
{
    // Entering fly mode: generated demo mode snaps to free camera; scene mode stays
    // on the current camera so fly controls work without requiring CAMERA_FREE
    if ( !SceneState().isSceneMode )
    {
        m_systems.cameras->SelectCamera( CAMERA_FREE, true );
    }
    m_camera.cameraTime = 0.0f;
    XZBounds unbounded;
    unbounded.m_xMin = -99999.9f;
    unbounded.m_xMax = 99999.9f;
    unbounded.m_zMin = -99999.9f;
    unbounded.m_zMax = 99999.9f;
    uint32_t activeCam = SceneState().isSceneMode ? m_systems.cameras->GetSelectedCameraName() : CAMERA_FREE;
    m_systems.cameras->SetCameraXZBounds( activeCam, unbounded );
    if ( ShouldHideNativeCursor() )
    {
        Input::SetSystemCursorVisible( false );
    }
    else
    {
        ReleaseMouseToUI();
        Input::SetSystemCursorVisible( true );
    }
    InputController::ResetMouseLook( m_camera );
}


void Run::ExitFlyModeCamera()
{
    // Exiting fly mode restores terrain bounds, the camera-cycle clock, and
    // the stock Windows cursor.
    uint32_t activeCam = SceneState().isSceneMode ? m_systems.cameras->GetSelectedCameraName() : CAMERA_FREE;
    m_systems.cameras->SetCameraXZBounds( activeCam, m_systems.terrain->GetXZBounds() );
    Input::SetSystemCursorVisible( true );
    m_camera.cameraTime = 0.0f;
    InputController::ResetMouseLook( m_camera );
}


bool Run::HandleUnfocusedInputFrame()
{
    if ( Input::IsAppFocused() )
    {
        return false;
    }

    // Invariant: focus loss releases every active tool capture and refreshes
    // action memory so refocus cannot replay stale drag/key edges.
    CancelCameraLookGesture();
    CancelReplayToolDragState();
    Input::SetSystemCursorVisible( true );
    if ( m_replayRuntime.ResetScrubberState() )
    {
        ExitReplayInspectionCamera();
    }
    m_replayRuntime.Prediction().checkboxHovered = false;
    m_replayRuntime.Prediction().decreaseHovered = false;
    m_replayRuntime.Prediction().increaseHovered = false;
    m_replayRuntime.Prediction().horizonHovered = false;
    m_replayRuntime.Prediction().horizonDragging = false;
    m_replayRuntime.VelocityEdit().toggleHovered = false;
    m_replayRuntime.VelocityEdit().keyboardAltWasDown = false;
    m_replayRuntime.VelocityEdit().dragging = false;
    m_replayRuntime.VelocityEdit().draggingAngular = false;
    m_replayRuntime.VelocityEdit().activeAxis = -1;
    m_replayRuntime.VelocityEdit().hotLinearAxis = -1;
    m_replayRuntime.VelocityEdit().hotAngularAxis = -1;
    if ( m_replayRuntime.VelocityEdit().mouseCaptured )
    {
        UI::InputControl::EndMouseCapture();
        m_replayRuntime.VelocityEdit().mouseCaptured = false;
    }
    CancelMousePickup();
    if ( m_replayRuntime.CauseTree().draggingWindow || m_replayRuntime.CauseTree().resizingWindow )
    {
        UI::InputControl::EndMouseCapture();
        m_replayRuntime.CauseTree().draggingWindow = false;
        m_replayRuntime.CauseTree().resizingWindow = false;
    }
    RunInternal::ResetEditorUnfocusedInputState( { m_runtimeTools.Editor(), m_cGameModelCollection, m_interaction } );
    InputController::ResetUnfocusedInput( m_camera,
                                          m_inputLatches.leftSceneCycleWasDown,
                                          m_inputLatches.rightSceneCycleWasDown );
    m_runtimeInput.ResetEdges();
    InputController::BeginFrame( m_runtimeInput,
                                 BuildRuntimeInputModeState( m_camera.mode,
                                                             m_runtimeTools.Editor(),
                                                             m_attachedCamera.activeFollow,
                                                             m_camera.director.grabbed ),
                                 false,
                                 true,
                                 true );
    m_UI.CancelInputCapture();
    RunUIStressActions();
    return true;
}


void Run::DispatchPostUIKeyboardActions()
{
    // Why: capture/reset shortcuts run after UI input so focused controls and
    // panels get first refusal on keyboard ownership.
    const RunInternal::EditorSaveHotkeyContext editorSaveHotkeyContext{ m_runtimeInput,
                                                                        m_cGameModelCollection,
                                                                        SceneState(),
                                                                        m_cWorldEnvironment,
                                                                        *m_systems.cameras,
                                                                        m_runtimeCommands };
    auto dispatchCaptureKeyboardAction = [&editorSaveHotkeyContext]( const RuntimeInputKeyBinding& binding ) -> bool
    {
        switch ( binding.action )
        {
        case RuntimeInputAction::SaveSceneSnapshot:
        case RuntimeInputAction::SaveScreenshot:
            RunInternal::HandleEditorSaveHotkey( editorSaveHotkeyContext, binding.action, binding.virtualKey );
            return true;
        default:
            return false;
        }
    };
    for ( std::size_t i = 0; i < kTakeInputKeyboardBindingCount; ++i )
    {
        if ( ( kTakeInputKeyboardBindings[i].contexts & kCaptureContext ) != 0 )
        {
            dispatchCaptureKeyboardAction( kTakeInputKeyboardBindings[i] );
        }
    }

    auto dispatchLateKeyboardAction = [this]( const RuntimeInputKeyBinding& binding ) -> bool
    {
        switch ( binding.action )
        {
        case RuntimeInputAction::ResetScene:
            if ( InputController::CaptureKeyboardActionPress( m_runtimeInput, binding.action, binding.virtualKey ) )
            {
                // R reloads the current scene after editor save hotkeys have had
                // their chance to consume Ctrl-based persistence shortcuts.
                m_runtimeCommands.Push( RuntimeCommand{ RuntimeCommandType::ResetCurrentScene } );
            }
            return true;
        case RuntimeInputAction::ResetSceneFromBackspace:
            if ( SceneState().isSceneMode &&
                 InputController::CaptureKeyboardActionPress( m_runtimeInput, binding.action, binding.virtualKey ) )
            {
                // Backspace is only a scene-mode reset alias; generated demos keep
                // the key free for future non-scene tools.
                m_runtimeCommands.Push( RuntimeCommand{ RuntimeCommandType::ResetCurrentScene } );
            }
            return true;
        default:
            return false;
        }
    };
    for ( std::size_t i = 0; i < kTakeInputKeyboardBindingCount; ++i )
    {
        if ( ( kTakeInputKeyboardBindings[i].contexts & kAfterUIUpdateContext ) != 0 )
        {
            dispatchLateKeyboardAction( kTakeInputKeyboardBindings[i] );
        }
    }
}


void Run::TakeInput()
{
    if ( HandleUnfocusedInputFrame() )
    {
        return;
    }

    const bool UIBlocksKeyboardBeforeInput = m_UI.BlocksKeyboard();
    ApplyCursorOwnership();
    InputController::BeginFrame( m_runtimeInput,
                                 BuildRuntimeInputModeState( m_camera.mode,
                                                             m_runtimeTools.Editor(),
                                                             m_attachedCamera.activeFollow,
                                                             m_camera.director.grabbed ),
                                 true,
                                 UIBlocksKeyboardBeforeInput,
                                 m_UI.BlocksCameraMouse() );
    bool keyboardToggleEditorMode = false;
    RunInternal::EditorKeyboardShortcutResult keyboardEditorToolShortcut;
    auto editorGizmoContext = [this]()
    { return RunInternal::EditorGizmoContext{ m_runtimeTools.Editor(), m_cGameModelCollection, m_interaction }; };
    auto applyEditorPlacementModeChange =
        [this, &editorGizmoContext]( RuntimeInputActionSource source, bool enabled, bool clearManipulation )
    {
        EnterInteractiveSceneRun();
        const RunInternal::EditorPlacementModeChangeResult placementMode =
            RunInternal::SetEditorPlacementMode( editorGizmoContext(), enabled, clearManipulation );
        SetWorldInteractionOwnerAfterInteractionTransition( placementMode.worldOwner,
                                                            InteractionExitReason::EnterEdit );
        ReleaseMouseToUI();
        ApplyCursorOwnership();
        UpdateRuntimeInputModeAfterAction( RuntimeInputAction::ToggleEditorTool, source );
    };
    auto applyEditorPlacementModeToggle = [this, &editorGizmoContext]( RuntimeInputActionSource source )
    {
        EnterInteractiveSceneRun();
        const RunInternal::EditorPlacementModeChangeResult placementMode =
            RunInternal::ToggleEditorPlacementMode( editorGizmoContext() );
        SetWorldInteractionOwnerAfterInteractionTransition( placementMode.worldOwner,
                                                            InteractionExitReason::EnterEdit );
        ReleaseMouseToUI();
        ApplyCursorOwnership();
        UpdateRuntimeInputModeAfterAction( RuntimeInputAction::ToggleEditorTool, source );
    };
    auto applyEditorModeToggle = [this, &editorGizmoContext]( RuntimeInputActionSource source )
    {
        EnterInteractiveSceneRun();
        const bool enteringEditor = !m_runtimeTools.Editor().editorModeEnabled;
        if ( enteringEditor )
        {
            const RuntimeInteractionTransition transition = m_interaction.EnterEdit();
            ApplyRuntimeInteractionTransitionCleanup( transition );
            const bool wasFlyMode = IsFlyCameraMode();
            RunInternal::EnterEditorModeState( editorGizmoContext(),
                                               NormalizeCameraModeForCurrentScene( m_camera.mode ) );
            CancelMousePickup();
            SetCameraModeLabelAfterInteractionTransition( RunCameraMode::Inspect );
            if ( !wasFlyMode )
            {
                EnterFlyModeCamera();
            }
            else
            {
                InputController::ResetMouseLook( m_camera );
            }
            ApplyCursorOwnership();
        }
        else
        {
            const RunCameraMode restoreMode =
                NormalizeCameraModeForCurrentScene( m_runtimeTools.Editor().restoreCameraModeAfterEditor );
            const RuntimeInteractionTransition transition = EnterInteractionForCameraMode( restoreMode );
            ApplyRuntimeInteractionTransitionCleanup( transition );
            const bool wasFlyMode = IsFlyCameraMode();
            RunInternal::ExitEditorModeState( editorGizmoContext() );
            SetCameraModeLabelAfterInteractionTransition( restoreMode );
            if ( wasFlyMode && !IsFlyCameraMode() )
            {
                ExitFlyModeCamera();
            }
            else
            {
                InputController::ResetMouseLook( m_camera );
            }
            ApplyCursorOwnership();
        }
        UpdateRuntimeInputModeAfterAction( RuntimeInputAction::ToggleEditor, source );
    };
    if ( !UIBlocksKeyboardBeforeInput )
    {
        auto executeSceneControlAction = [&]( const SceneRuntimeControlAction& action ) -> bool
        {
            if ( action.enterInteractiveSceneRun )
            {
                EnterInteractiveSceneRun();
            }

            switch ( action.type )
            {
            case SceneRuntimeControlActionType::ClearCurrentSceneAutomation:
                SceneState().isExitOnComplete = false;
                m_diagnosticsRuntime.Capture().Screenshot().isScreenshotAndExit = false;
                return true;
            case SceneRuntimeControlActionType::LoadScene:
                return LoadScene( action.index,
                                  action.preserveUIState,
                                  action.suppressExitOnComplete,
                                  action.preserveRuntimeState )
                    .ok;
            case SceneRuntimeControlActionType::ApplyCinematicModeFromBrowserIndex:
                EnterInteractiveSceneRun();
                return ApplyCinematicModeFromBrowserIndex(
                    SceneRuntimeStyleContext{ m_launchOptions,
                                              SceneState(),
                                              m_sceneController.Browser(),
                                              m_cGameModelCollection,
                                              m_systems.assets,
                                              RuntimeActiveCinematicConfig( SceneState(), m_config ),
                                              m_defaultCinematicRender },
                    action.index );
            case SceneRuntimeControlActionType::None:
                return false;
            }
            return false;
        };

        auto dispatchMappedKeyboardAction =
            [this, &executeSceneControlAction, &keyboardToggleEditorMode, &keyboardEditorToolShortcut](
                const RuntimeInputKeyBinding& binding ) -> bool
        {
            switch ( binding.action )
            {
            case RuntimeInputAction::ToggleEditor:
                // Backtick is captured early but applied after UI command processing
                // so keyboard and UI editor toggles share the same transition path.
                keyboardToggleEditorMode =
                    InputController::CaptureKeyboardActionPress( m_runtimeInput, binding.action, binding.virtualKey );
                return true;
            case RuntimeInputAction::ToggleEditorTool:
                keyboardEditorToolShortcut = RunInternal::HandleEditorKeyboardShortcut(
                    binding.action,
                    Input::IsKeyDown( binding.virtualKey ),
                    InputController::CaptureKeyboardActionPress( m_runtimeInput, binding.action, binding.virtualKey ) );
                return true;
            case RuntimeInputAction::CycleCameraMode:
                if ( InputController::CaptureKeyboardActionPress( m_runtimeInput, binding.action, binding.virtualKey ) )
                {
                    CycleCameraMode();
                }
                return true;
            case RuntimeInputAction::ToggleFlyCamera:
                if ( InputController::CaptureKeyboardActionPress( m_runtimeInput, binding.action, binding.virtualKey ) )
                {
                    // F enters Inspect, or returns to the passive camera mode when already inspecting.
                    const RunCameraMode passiveMode =
                        SceneState().isSceneMode ? RunCameraMode::Scene : RunCameraMode::Demo;
                    ApplyCameraMode( m_camera.mode == RunCameraMode::Inspect ? passiveMode : RunCameraMode::Inspect,
                                     RuntimeInputActionSource::Keyboard );
                }
                return true;
            case RuntimeInputAction::ToggleLauncher:
                if ( InputController::CaptureKeyboardActionPress( m_runtimeInput, binding.action, binding.virtualKey ) )
                {
                    // N toggles launcher view with live simulation and returns to the previous non-launcher mode.
                    if ( m_camera.mode == RunCameraMode::Launcher )
                    {
                        ApplyCameraMode( m_camera.modeBeforeLauncher, RuntimeInputActionSource::Keyboard );
                    }
                    else
                    {
                        m_camera.modeBeforeLauncher =
                            m_camera.mode == RunCameraMode::Manipulator ? RunCameraMode::Inspect : m_camera.mode;
                        ApplyCameraMode( RunCameraMode::Launcher, RuntimeInputActionSource::Keyboard );
                    }
                }
                return true;
            case RuntimeInputAction::CycleLauncherFireMode:
                if ( InputController::CaptureKeyboardActionPress( m_runtimeInput,
                                                                  binding.action,
                                                                  binding.virtualKey ) &&
                     IsLauncherCameraMode() )
                {
                    m_runtimeTools.RayCastTest().fireMode =
                        m_runtimeTools.RayCastTest().fireMode == RunLauncherFireMode::Laser
                            ? RunLauncherFireMode::Projectile
                            : RunLauncherFireMode::Laser;
                }
                return true;
            case RuntimeInputAction::CycleAttachedCameraSubmode:
                if ( InputController::CaptureKeyboardActionPress( m_runtimeInput,
                                                                  binding.action,
                                                                  binding.virtualKey ) &&
                     IsAttachedCameraMode() )
                {
                    CycleAttachedCameraSubmode();
                }
                return true;
            case RuntimeInputAction::ToggleAttachedCameraPin:
                if ( InputController::CaptureKeyboardActionPress( m_runtimeInput,
                                                                  binding.action,
                                                                  binding.virtualKey ) &&
                     IsAttachedCameraMode() )
                {
                    ToggleAttachedCameraPin();
                }
                return true;
            case RuntimeInputAction::WriteLauncherReproSnapshot:
#ifdef _DEBUG
                if ( InputController::CaptureKeyboardActionPress( m_runtimeInput,
                                                                  binding.action,
                                                                  binding.virtualKey ) &&
                     IsLauncherCameraMode() && !m_replayRuntime.Scrubber().restoreConsumedThisFrame )
                {
                    // Debug-only Enter writes a launcher repro snapshot unless a replay
                    // restore consumed Enter this frame; Profile keeps this table row inert.
                    const double simulationSeconds = m_timers.simulationTimer.GetTimeSinceLastStart();
                    m_runtimeTools.WriteLauncherReproSnapshotWithStatusMessage(
                        { m_cGameModelCollection,
                          m_systems.cameras,
                          m_systems.terrain.get(),
                          m_cWorldEnvironment,
                          SceneState(),
                          m_sceneController.CurrentPath(),
                          m_launchOptions,
                          m_runtimeSettings,
                          m_config.contactEpsilon,
                          m_config.frictionCoeff,
                          m_debug,
                          m_renderBackendView.renderDiagnostics
                              ? m_renderBackendView.renderDiagnostics->GetRendererName()
                              : "DirectX 12",
                          simulationSeconds },
                        m_debug );
                }
#endif
                return true;
            case RuntimeInputAction::ToggleDirectorGrab:
                if ( InputController::CaptureKeyboardActionPress( m_runtimeInput,
                                                                  binding.action,
                                                                  binding.virtualKey ) &&
                     m_camera.mode == RunCameraMode::Director )
                {
                    // B key: Director grab/release keeps the visible mode as Director while
                    // temporarily letting the operator fly the selected camera.
                    if ( m_camera.director.grabbed )
                    {
                        if ( DemoDirectorPlayback::EndGrab( m_camera, m_systems ) )
                        {
                            ExitFlyModeCamera();
                            ApplyCursorOwnership();
                            UpdateRuntimeInputModeAfterAction( RuntimeInputAction::ToggleDirectorGrab,
                                                               RuntimeInputActionSource::Keyboard );
                        }
                    }
                    else if ( DemoDirectorPlayback::BeginGrab( m_camera, m_systems ) )
                    {
                        EnterFlyModeCamera();
                        ApplyCursorOwnership();
                        UpdateRuntimeInputModeAfterAction( RuntimeInputAction::ToggleDirectorGrab,
                                                           RuntimeInputActionSource::Keyboard );
                    }
                }
                return true;
            case RuntimeInputAction::SetDirectorPhasePose:
            {
                const bool directorAuthoringAvailable = m_camera.mode == RunCameraMode::Director || IsFlyCameraMode();
                if ( InputController::CaptureKeyboardActionPress( m_runtimeInput,
                                                                  binding.action,
                                                                  binding.virtualKey ) &&
                     directorAuthoringAvailable && DemoDirectorPlayback::SetCurrentPhasePose( m_camera, m_systems ) )
                {
                    UpdateRuntimeInputModeAfterAction( RuntimeInputAction::SetDirectorPhasePose,
                                                       RuntimeInputActionSource::Keyboard );
                }
                return true;
            }
            case RuntimeInputAction::StepDirectorPhase:
            {
                const bool directorAuthoringAvailable = m_camera.mode == RunCameraMode::Director || IsFlyCameraMode();
                if ( InputController::CaptureKeyboardActionPress( m_runtimeInput,
                                                                  binding.action,
                                                                  binding.virtualKey ) &&
                     directorAuthoringAvailable &&
                     DemoDirectorPlayback::SelectNextPhaseForAuthoring( m_camera, m_systems ) )
                {
                    UpdateRuntimeInputModeAfterAction( RuntimeInputAction::StepDirectorPhase,
                                                       RuntimeInputActionSource::Keyboard );
                }
                return true;
            }
            case RuntimeInputAction::SaveDirectorShotList:
            {
                const bool directorAuthoringAvailable = m_camera.mode == RunCameraMode::Director || IsFlyCameraMode();
                if ( InputController::CaptureKeyboardActionPress( m_runtimeInput,
                                                                  binding.action,
                                                                  binding.virtualKey ) &&
                     directorAuthoringAvailable && DemoDirectorPlayback::SaveShotList( m_camera ) )
                {
                    UpdateRuntimeInputModeAfterAction( RuntimeInputAction::SaveDirectorShotList,
                                                       RuntimeInputActionSource::Keyboard );
                }
                return true;
            }
            case RuntimeInputAction::ToggleWaterFreeze:
            case RuntimeInputAction::CycleWaterReflection:
            case RuntimeInputAction::ToggleWaterFlat:
            case RuntimeInputAction::ToggleTerrainHidden:
            case RuntimeInputAction::ToggleWaterHidden:
            case RuntimeInputAction::ToggleCollisionVisualizer:
            case RuntimeInputAction::CyclePhysicsDebugOverlay:
            case RuntimeInputAction::ToggleTerrainContactProbe:
            case RuntimeInputAction::StepPhysicsPipelinePrevious:
            case RuntimeInputAction::StepPhysicsPipelineNext:
            case RuntimeInputAction::TogglePhysicsDebugTransparent:
            case RuntimeInputAction::ReportRendererRuntimeRetired:
            case RuntimeInputAction::ToggleCrossScenePause:
            case RuntimeInputAction::ToggleBroadphaseOverlay:
            {
                return HandleDiagnosticsKeyboardShortcut(
                    DiagnosticsKeyboardShortcutContext{ m_runtimeInput,
                                                        m_debug,
                                                        m_camera.trackBallIndex,
                                                        m_cGameModelCollection,
                                                        m_renderBackendView.renderDiagnostics,
                                                        SceneState().isSceneMode,
                                                        m_timers.simulationTimer.GetTimeSinceLastStart() },
                    binding.action,
                    binding.virtualKey );
            }
            case RuntimeInputAction::ToggleUIVisibility:
            case RuntimeInputAction::TogglePerformanceHistogram:
            case RuntimeInputAction::ToggleMemoryOverlay:
            {
                const DiagnosticsUIKeyboardShortcutResult shortcutResult = HandleDiagnosticsUIKeyboardShortcut(
                    DiagnosticsUIKeyboardShortcutContext{ m_runtimeInput,
                                                          m_UI,
                                                          m_debug,
                                                          SceneState(),
                                                          m_diagnosticsRuntime.Capture(),
                                                          m_timers.simulationTimer.GetTotalTime() },
                    binding.action,
                    binding.virtualKey );
                if ( shortcutResult.triggered )
                {
                    if ( shortcutResult.releaseMouseToUI )
                    {
                        ApplyCursorOwnership();
                        ReleaseMouseToUI();
                    }
                    UpdateRuntimeInputModeAfterAction( binding.action, RuntimeInputActionSource::Keyboard );
                }
                return shortcutResult.handled;
            }
            case RuntimeInputAction::NavigateScenePrevious:
            case RuntimeInputAction::NavigateSceneNext:
                if ( InputController::CaptureKeyboardActionPress( m_runtimeInput, binding.action, binding.virtualKey ) )
                {
                    const int direction = binding.action == RuntimeInputAction::NavigateScenePrevious ? -1 : 1;
                    // Left/right first move through cinematic variants when that tab
                    // owns context; otherwise they load the adjacent browser scene.
                    EnterInteractiveSceneRun();
                    const int currentSceneBrowserIndex =
                        CurrentSceneBrowserIndex( m_sceneController, m_sceneController.Browser() );
                    const bool isCinematicTabActive = m_UI.GetActiveTab() == InGameUITab::Cinematic;
                    if ( !executeSceneControlAction( m_sceneCoordinator.ApplyAdjacentCinematicMode(
                             direction,
                             m_sceneController.Browser().paths,
                             m_sceneController.Browser().selectedCineModeSceneIndex,
                             currentSceneBrowserIndex,
                             isCinematicTabActive ) ) )
                    {
                        executeSceneControlAction(
                            m_sceneCoordinator.LoadAdjacentSceneFromBrowser( direction,
                                                                             m_sceneController.Browser().paths,
                                                                             currentSceneBrowserIndex ) );
                    }
                }
                return true;
            default:
                return false;
            }
        };
        for ( std::size_t i = 0; i < kTakeInputKeyboardBindingCount; ++i )
        {
            dispatchMappedKeyboardAction( kTakeInputKeyboardBindings[i] );
        }

        if ( m_runtimeTools.Editor().editorModeEnabled )
        {
            const RunInternal::EditorKeyboardShortcutResult editorShortcuts = keyboardEditorToolShortcut;
            m_replayRuntime.SetVelocityEditAltKeyDown( editorShortcuts.altDown );
            if ( editorShortcuts.togglePlacementMode )
            {
                applyEditorPlacementModeToggle( RuntimeInputActionSource::Keyboard );
            }
        }
        else
        {
            const bool altDown = keyboardEditorToolShortcut.altDown;
            if ( altDown && !m_replayRuntime.VelocityEdit().keyboardAltWasDown )
            {
                const bool enableVelocityEdit = !m_replayRuntime.VelocityEdit().enabled;
                if ( m_replayRuntime.SetVelocityEditEnabled( enableVelocityEdit ) )
                {
                    CancelReplayToolDragState();
                    if ( enableVelocityEdit )
                    {
                        EnterInteractiveSceneRun();
                        if ( m_replayRuntime.SetLiveAdvanceHeld( true ) )
                        {
                            if ( m_replayRuntime.ShouldUseInspectionCamera() )
                            {
                                EnterReplayInspectionCamera();
                            }
                            else
                            {
                                ExitReplayInspectionCamera();
                            }
                        }
                        SetWorldInteractionOwnerAfterInteractionTransition( WorldInteractionOwner::ReplayVelocityEdit,
                                                                            InteractionExitReason::EnterReplay );
                    }
                    else if ( m_interaction.Owner() == WorldInteractionOwner::ReplayVelocityEdit )
                    {
                        SetWorldInteractionOwnerAfterInteractionTransition( WorldInteractionOwner::ReplayScrub,
                                                                            InteractionExitReason::EnterReplay );
                    }
                }
                m_replayRuntime.Scrubber().visibleUntil =
                    m_timers.simulationTimer.GetTotalTime() + ReplayOverlay::REPLAY_SCRUBBER_VISIBLE_SECONDS;
                m_replayRuntime.Scrubber().visible = true;
            }
            m_replayRuntime.VelocityEdit().keyboardAltWasDown = altDown;
            m_runtimeInput.SetActionDown( RuntimeInputAction::ToggleEditorTool, altDown );
            m_runtimeInput.SetActionDown( RuntimeInputAction::CycleCameraMode, Input::IsKeyDown( VK_TAB ) );
            m_runtimeTools.Editor().altShortcutWasDown = altDown;
            m_runtimeTools.Editor().tabShortcutWasDown = Input::IsKeyDown( VK_TAB );
        }
    }
    else
    {
        AdvanceTakeInputKeyboardActionMemories( m_runtimeInput );
        m_inputLatches.leftSceneCycleWasDown = Input::IsKeyDown( VK_LEFT );
        m_inputLatches.rightSceneCycleWasDown = Input::IsKeyDown( VK_RIGHT );
        m_replayRuntime.VelocityEdit().keyboardAltWasDown = Input::IsKeyDown( VK_MENU );
        m_runtimeTools.Editor().altShortcutWasDown = Input::IsKeyDown( VK_MENU );
        m_runtimeTools.Editor().tabShortcutWasDown = Input::IsKeyDown( VK_TAB );
        m_runtimeTools.Editor().tildeShortcutWasDown = Input::IsKeyDown( VK_OEM_3 );
    }

    bool suppressWorldActionThisFrame = UIBlocksKeyboardBeforeInput;
    int editorUnhandledWheelDelta = 0;
    if ( m_systems.window )
    {
        const int selectedSceneBrowserIndex =
            CurrentSceneBrowserIndex( m_sceneController, m_sceneController.Browser() );
        InGameUIInputResult UIResult = m_UI.UpdateInput(
            m_systems.window->m_sWindow,
            static_cast<int>( m_systems.window->m_sWindowDimensions.x ),
            static_cast<int>( m_systems.window->m_sWindowDimensions.y ),
            m_timers.simulationTimer.GetTotalTime(),
            m_runtimeTools.Editor().editorModeEnabled,
            m_runtimeTools.Editor().placementModeEnabled,
            m_runtimeTools.Editor().placeStaticObject,
            m_runtimeTools.Editor().autoTerrainAlign,
            m_runtimeTools.Editor().objectType,
            static_cast<int>( m_camera.mode ),
            CameraModeEnabledMask(),
            m_sceneController.Browser().namePtrs.empty() ? nullptr : m_sceneController.Browser().namePtrs.data(),
            static_cast<int>( m_sceneController.Browser().namePtrs.size() ),
            selectedSceneBrowserIndex );
        editorUnhandledWheelDelta = UIResult.unhandledWheelDelta;
        const InGameUICommands& uiCommands = UIResult.commands;
        if ( uiCommands.ui.userInteracted )
        {
            EnterInteractiveSceneRun();
        }
        suppressWorldActionThisFrame = suppressWorldActionThisFrame || uiCommands.ui.userInteracted;
        const bool replayScrubberOwnsMouse =
            TickReplayScrubberInput( m_systems.window->m_sWindow, m_UI.BlocksCameraMouse() );
        const bool replayCauseTreeOwnsMouse =
            TickReplayCauseTreeInput( m_systems.window->m_sWindow,
                                      m_UI.BlocksCameraMouse() || replayScrubberOwnsMouse,
                                      editorUnhandledWheelDelta );
        const bool replayVelocityEditOwnsMouse = TickReplayVelocityEditInput(
            m_systems.window->m_sWindow,
            m_UI.BlocksCameraMouse() || replayScrubberOwnsMouse || replayCauseTreeOwnsMouse );
        suppressWorldActionThisFrame = suppressWorldActionThisFrame || replayScrubberOwnsMouse ||
                                       replayCauseTreeOwnsMouse || replayVelocityEditOwnsMouse;
        m_runtimeInput.BeginFrame( true,
                                   m_UI.BlocksKeyboard(),
                                   m_UI.BlocksCameraMouse() || replayScrubberOwnsMouse || replayCauseTreeOwnsMouse ||
                                       replayVelocityEditOwnsMouse );

        auto dispatchAfterUIKeyboardAction = [this, &uiCommands]( const RuntimeInputKeyBinding& binding ) -> bool
        {
            switch ( binding.action )
            {
            case RuntimeInputAction::DismissOrExitUI:
                if ( InputController::CaptureKeyboardActionPress( m_runtimeInput,
                                                                  binding.action,
                                                                  binding.virtualKey ) &&
                     !uiCommands.ui.userInteracted )
                {
                    // ESC is intentionally after UI processing: focused controls
                    // keep local ESC behavior before the diagnostics window reacts.
                    constexpr double ESC_QUICK_EXIT_SECONDS = 0.32;
                    const double UINow = m_timers.simulationTimer.GetTotalTime();
                    if ( UINow - m_inputLatches.lastEscapeTapTime <= ESC_QUICK_EXIT_SECONDS )
                    {
                        PostQuitMessage( 0 );
                    }
                    else
                    {
                        EnterInteractiveSceneRun();
                        m_UI.ToggleVisible( UINow );
                        m_debug.overlayMode = OverlayMode::None;
                        m_inputLatches.lastEscapeTapTime = UINow;
                        ApplyCursorOwnership();
                        ReleaseMouseToUI();
                    }
                }
                return true;
            default:
                return false;
            }
        };
        for ( std::size_t i = 0; i < kTakeInputKeyboardBindingCount; ++i )
        {
            if ( ( kTakeInputKeyboardBindings[i].contexts & kAfterUIUpdateContext ) != 0 )
            {
                dispatchAfterUIKeyboardAction( kTakeInputKeyboardBindings[i] );
            }
        }

        if ( uiCommands.renderer.toggleVsync )
        {
            m_runtimeSettings.isVsyncEnabled = !m_runtimeSettings.isVsyncEnabled;
            if ( m_renderBackendView.deviceLifecycle )
            {
                m_renderBackendView.deviceLifecycle->SetVsyncEnabled( m_runtimeSettings.isVsyncEnabled );
            }
            UpdateRuntimeInputModeAfterAction( RuntimeInputAction::ToggleVsync, RuntimeInputActionSource::UI );
        }
        if ( uiCommands.run.requestedCameraMode >= 0 &&
             uiCommands.run.requestedCameraMode < static_cast<int>( RunCameraMode::Count ) )
        {
            ApplyCameraMode( static_cast<RunCameraMode>( uiCommands.run.requestedCameraMode ),
                             RuntimeInputActionSource::UI );
        }
        if ( uiCommands.editor.requestPlaceStatic &&
             RunInternal::SetEditorPlaceStaticObject( m_runtimeTools.Editor(),
                                                      uiCommands.editor.requestedPlaceStatic ) )
        {
            EnterInteractiveSceneRun();
            UpdateRuntimeInputModeAfterAction( RuntimeInputAction::ToggleEditorStaticPlacement,
                                               RuntimeInputActionSource::UI );
        }
        if ( uiCommands.editor.requestedObjectType >= 0 )
        {
            const RunInternal::EditorObjectTypeRequestResult objectTypeRequest =
                RunInternal::SelectEditorObjectType( editorGizmoContext(),
                                                     uiCommands.editor.requestedObjectType,
                                                     uiCommands.editor.enterPlacementMode );
            if ( objectTypeRequest.enterPlacementMode )
            {
                applyEditorPlacementModeChange( RuntimeInputActionSource::UI, true, false );
            }
            UpdateRuntimeInputModeAfterAction( RuntimeInputAction::CycleEditorPlacementType,
                                               RuntimeInputActionSource::UI );
        }
        if ( uiCommands.editor.toggleEditorMode || keyboardToggleEditorMode )
        {
            applyEditorModeToggle( keyboardToggleEditorMode ? RuntimeInputActionSource::Keyboard
                                                            : RuntimeInputActionSource::UI );
        }
        if ( uiCommands.editor.togglePlacementMode )
        {
            applyEditorPlacementModeToggle( RuntimeInputActionSource::UI );
        }
        if ( uiCommands.editor.togglePlaceStatic )
        {
            EnterInteractiveSceneRun();
            RunInternal::ToggleEditorPlaceStaticObject( m_runtimeTools.Editor() );
            UpdateRuntimeInputModeAfterAction( RuntimeInputAction::ToggleEditorStaticPlacement,
                                               RuntimeInputActionSource::UI );
        }
        if ( uiCommands.editor.toggleTerrainAlign )
        {
            EnterInteractiveSceneRun();
            RunInternal::ToggleEditorTerrainAlign( m_runtimeTools.Editor() );
            UpdateRuntimeInputModeAfterAction( RuntimeInputAction::ToggleEditorTerrainAlign,
                                               RuntimeInputActionSource::UI );
        }
        if ( uiCommands.physics.toggleCollisionVisualizer )
        {
            m_debug.isCollisionVisualizer = !m_debug.isCollisionVisualizer;
            UpdateRuntimeInputModeAfterAction( RuntimeInputAction::ToggleCollisionVisualizer,
                                               RuntimeInputActionSource::UI );
        }
        if ( uiCommands.physics.togglePhysicsSleepPolicy )
        {
            m_runtimeSettings.isPhysicsSleepEnabled = !m_runtimeSettings.isPhysicsSleepEnabled;
            m_cGameModelCollection.SetPhysicsSleepEnabled( m_runtimeSettings.isPhysicsSleepEnabled );
            UpdateRuntimeInputModeAfterAction( RuntimeInputAction::TogglePhysicsSleepPolicy,
                                               RuntimeInputActionSource::UI );
        }
        if ( uiCommands.physics.togglePhysicsDebugFlags != 0 )
        {
            m_debug.physicsDebugFlags ^= ( uiCommands.physics.togglePhysicsDebugFlags & PHYSICS_DEBUG_ALL );
            UpdateRuntimeInputModeAfterAction( RuntimeInputAction::TogglePhysicsDebugFlags,
                                               RuntimeInputActionSource::UI );
        }
        if ( uiCommands.physics.stepPhysicsPipelinePrevious )
        {
            StepDiagnosticsPhysicsPipelineStage( m_debug, -1 );
            UpdateRuntimeInputModeAfterAction( RuntimeInputAction::StepPhysicsPipelinePrevious,
                                               RuntimeInputActionSource::UI );
        }
        if ( uiCommands.physics.stepPhysicsPipelineNext )
        {
            StepDiagnosticsPhysicsPipelineStage( m_debug, 1 );
            UpdateRuntimeInputModeAfterAction( RuntimeInputAction::StepPhysicsPipelineNext,
                                               RuntimeInputActionSource::UI );
        }
        if ( uiCommands.physics.togglePhysicsDebugTransparent )
        {
            m_debug.isPhysicsDebugTransparent = !m_debug.isPhysicsDebugTransparent;
            UpdateRuntimeInputModeAfterAction( RuntimeInputAction::TogglePhysicsDebugTransparent,
                                               RuntimeInputActionSource::UI );
        }
        if ( uiCommands.physics.toggleBroadphaseOverlay )
        {
            m_debug.isBroadphaseOverlay = !m_debug.isBroadphaseOverlay;
            UpdateRuntimeInputModeAfterAction( RuntimeInputAction::ToggleBroadphaseOverlay,
                                               RuntimeInputActionSource::UI );
        }
        const TornadoUICommandResult tornadoCommands =
            ApplyTornadoUICommands( TornadoUICommandContext{ m_runtimeSettings, m_cGameModelCollection },
                                    uiCommands.physics );
        if ( tornadoCommands.toggledTornado )
        {
            UpdateRuntimeInputModeAfterAction( RuntimeInputAction::ToggleTornado, RuntimeInputActionSource::UI );
        }
        if ( tornadoCommands.toggledVisualShell )
        {
            UpdateRuntimeInputModeAfterAction( RuntimeInputAction::ToggleTornadoVisualShell,
                                               RuntimeInputActionSource::UI );
        }
        if ( tornadoCommands.toggledFieldVectors )
        {
            UpdateRuntimeInputModeAfterAction( RuntimeInputAction::ToggleTornadoFieldVectors,
                                               RuntimeInputActionSource::UI );
        }
        if ( uiCommands.physics.toggleRayCastVisualization )
        {
            m_runtimeTools.RayCastTest().visualizeRays = !m_runtimeTools.RayCastTest().visualizeRays;
            UpdateRuntimeInputModeAfterAction( RuntimeInputAction::ToggleRayCastVisualization,
                                               RuntimeInputActionSource::UI );
        }
        for ( int tornadoApplyAction = 0; tornadoApplyAction < tornadoCommands.applySettingsActionCount;
              ++tornadoApplyAction )
        {
            UpdateRuntimeInputModeAfterAction( RuntimeInputAction::ApplyTornadoSettings, RuntimeInputActionSource::UI );
        }
        if ( uiCommands.physics.toggleTerrainContactProbe )
        {
            m_debug.physicsDebugFlags ^= PHYSICS_DEBUG_TERRAIN_CONTACT;
            UpdateRuntimeInputModeAfterAction( RuntimeInputAction::ToggleTerrainContactProbe,
                                               RuntimeInputActionSource::UI );
        }
        if ( uiCommands.sceneOptions.toggleTextOnly )
        {
            m_debug.isTextOnly = !m_debug.isTextOnly;
            UpdateRuntimeInputModeAfterAction( RuntimeInputAction::ToggleTextOnly, RuntimeInputActionSource::UI );
        }
        if ( uiCommands.sceneOptions.toggleFixedStep )
        {
            SceneState().isFixedStep = !SceneState().isFixedStep;
            m_simulation.Reset();
            UpdateRuntimeInputModeAfterAction( RuntimeInputAction::ToggleFixedStep, RuntimeInputActionSource::UI );
        }
        if ( uiCommands.sceneOptions.toggleTerrainHidden )
        {
            m_debug.isTerrainHidden = !m_debug.isTerrainHidden;
            UpdateRuntimeInputModeAfterAction( RuntimeInputAction::ToggleTerrainHidden, RuntimeInputActionSource::UI );
        }
        if ( uiCommands.sceneOptions.toggleWaterHidden )
        {
            m_debug.isWaterHidden = !m_debug.isWaterHidden;
            UpdateRuntimeInputModeAfterAction( RuntimeInputAction::ToggleWaterHidden, RuntimeInputActionSource::UI );
        }
        if ( uiCommands.sceneOptions.toggleWaterFreeze )
        {
            m_debug.isWaterFreezeDebug = !m_debug.isWaterFreezeDebug;
            if ( m_debug.isWaterFreezeDebug )
            {
                m_debug.frozenWaterTime = static_cast<float>( m_timers.simulationTimer.GetTimeSinceLastStart() );
            }
            UpdateRuntimeInputModeAfterAction( RuntimeInputAction::ToggleWaterFreeze, RuntimeInputActionSource::UI );
        }
        if ( uiCommands.sceneOptions.toggleWaterFlat )
        {
            m_debug.isWaterFlatDebug = !m_debug.isWaterFlatDebug;
            UpdateRuntimeInputModeAfterAction( RuntimeInputAction::ToggleWaterFlat, RuntimeInputActionSource::UI );
        }
        if ( uiCommands.sceneOptions.toggleShadows )
        {
            if ( RuntimeCinematicRenderingEnabled( SceneState(),
                                                   m_config,
                                                   m_launchOptions,
                                                   m_debug,
                                                   m_renderBackendView.deviceLifecycle != nullptr ) )
            {
                const bool shadowsActive = RuntimeActiveCinematicConfig( SceneState(), m_config ).shadowsEnabled;
                m_launchOptions.hasCinematicShadowsOverride = false;
                SetCinematicShadowsEnabledFromUI( RuntimeActiveCinematicConfig( SceneState(), m_config ),
                                                  SceneState(),
                                                  !shadowsActive );
            }
            else
            {
                m_config.ordinaryRender.shadowsEnabled = !m_config.ordinaryRender.shadowsEnabled;
            }
            UpdateRuntimeInputModeAfterAction( RuntimeInputAction::ToggleShadows, RuntimeInputActionSource::UI );
        }
        if ( uiCommands.renderTuning.toggleShadows )
        {
            m_config.ordinaryRender.shadowsEnabled = !m_config.ordinaryRender.shadowsEnabled;
            UpdateRuntimeInputModeAfterAction( RuntimeInputAction::ToggleRenderShadows, RuntimeInputActionSource::UI );
        }
        if ( uiCommands.renderTuning.saveDefaults )
        {
            m_runtimeCommands.Push( RuntimeCommand{ RuntimeCommandType::SaveRenderDefaults } );
            UpdateRuntimeInputModeAfterAction( RuntimeInputAction::SaveRenderDefaults, RuntimeInputActionSource::UI );
        }
        if ( uiCommands.renderTuning.requestedParam != UIRenderParam::None )
        {
            ApplyOrdinaryRenderUIParam( m_config.ordinaryRender,
                                        uiCommands.renderTuning.requestedParam,
                                        uiCommands.renderTuning.requestedValue );
            UpdateRuntimeInputModeAfterAction( RuntimeInputAction::ApplyRenderTuning, RuntimeInputActionSource::UI );
        }
        if ( ApplySoundUICommands(
                 SoundUICommandContext{ m_contactAudio, m_runtimeSettings, m_launchOptions.noContactAudio },
                 uiCommands.sound ) )
        {
            UpdateRuntimeInputModeAfterAction( RuntimeInputAction::ApplySoundTuning, RuntimeInputActionSource::UI );
        }
        if ( uiCommands.water.toggleWaterReflection )
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
            UpdateRuntimeInputModeAfterAction( RuntimeInputAction::ToggleWaterReflection,
                                               RuntimeInputActionSource::UI );
        }
        if ( uiCommands.water.requestedWaterReflectionMode >= 0 )
        {
            const int mode = std::clamp( uiCommands.water.requestedWaterReflectionMode, 0, 2 );
            m_debug.isWaterRTReflect = mode == 1;
            m_debug.isWaterNoReflect = mode == 2;
            UpdateRuntimeInputModeAfterAction( RuntimeInputAction::SetWaterReflectionMode,
                                               RuntimeInputActionSource::UI );
        }
        if ( uiCommands.sceneOptions.requestedTimeScale > 0.0f )
        {
            m_sceneController.UIOverrides().timeScaleOverride =
                std::clamp( uiCommands.sceneOptions.requestedTimeScale, 0.10f, 10.00f );
            SceneState().timeScale = m_sceneController.UIOverrides().timeScaleOverride;
            UpdateRuntimeInputModeAfterAction( RuntimeInputAction::SetTimeScale, RuntimeInputActionSource::UI );
        }
        if ( uiCommands.run.requestedSeed > 0 )
        {
            SceneState().rngSeed = static_cast<unsigned int>( std::clamp( uiCommands.run.requestedSeed, 1, 999999 ) );
            SceneState().rngState = SceneState().rngSeed;
            UpdateRuntimeInputModeAfterAction( RuntimeInputAction::SetRunSeed, RuntimeInputActionSource::UI );
        }
        if ( uiCommands.physics.requestedPhysicsDebugAlpha >= 0.0f )
        {
            m_debug.physicsDebugAlpha = std::clamp( uiCommands.physics.requestedPhysicsDebugAlpha, 0.05f, 1.0f );
            UpdateRuntimeInputModeAfterAction( RuntimeInputAction::SetPhysicsDebugAlpha, RuntimeInputActionSource::UI );
        }
        if ( uiCommands.physics.requestedPhysicsDebugContactLinger >= 0.0f )
        {
            m_debug.physicsDebugContactLinger =
                std::clamp( uiCommands.physics.requestedPhysicsDebugContactLinger, 0.0f, 5.0f );
            UpdateRuntimeInputModeAfterAction( RuntimeInputAction::SetPhysicsDebugContactLinger,
                                               RuntimeInputActionSource::UI );
        }
        if ( uiCommands.physics.requestRayCastImpulseStrength )
        {
            const float previousImpulse = m_runtimeTools.RayCastTest().impulseStrength;
            m_runtimeTools.RayCastTest().impulseStrength =
                std::clamp( uiCommands.physics.requestedRayCastImpulseStrength,
                            UI_RAY_IMPULSE_MIN,
                            UI_RAY_IMPULSE_MAX );
            m_replayRuntime.RecordLauncherConfigEvent(
                previousImpulse != m_runtimeTools.RayCastTest().impulseStrength ? 1u : 0u,
                m_runtimeTools.RayCastTest().impulseStrength,
                m_runtimeTools.RayCastTest().projectileSpeed );
            UpdateRuntimeInputModeAfterAction( RuntimeInputAction::SetRayCastImpulseStrength,
                                               RuntimeInputActionSource::UI );
        }
        if ( uiCommands.physics.requestLauncherProjectileSpeed )
        {
            const float previousProjectileSpeed = m_runtimeTools.RayCastTest().projectileSpeed;
            m_runtimeTools.RayCastTest().projectileSpeed =
                std::clamp( uiCommands.physics.requestedLauncherProjectileSpeed,
                            UI_LAUNCHER_PROJECTILE_SPEED_MIN,
                            UI_LAUNCHER_PROJECTILE_SPEED_MAX );
            m_replayRuntime.RecordLauncherConfigEvent(
                previousProjectileSpeed != m_runtimeTools.RayCastTest().projectileSpeed ? 2u : 0u,
                m_runtimeTools.RayCastTest().impulseStrength,
                m_runtimeTools.RayCastTest().projectileSpeed );
            UpdateRuntimeInputModeAfterAction( RuntimeInputAction::SetLauncherProjectileSpeed,
                                               RuntimeInputActionSource::UI );
        }
        EngineConfig& liveConfig = m_config;
        bool runtimePhysicsConfigChanged = false;
        if ( uiCommands.physics.requestTerrainFrictionCoeff )
        {
            liveConfig.frictionCoeff = std::clamp( uiCommands.physics.requestedTerrainFrictionCoeff,
                                                   UI_FRICTION_COEFF_MIN,
                                                   UI_FRICTION_COEFF_MAX );
            runtimePhysicsConfigChanged = true;
            UpdateRuntimeInputModeAfterAction( RuntimeInputAction::ApplyPhysicsFrictionSettings,
                                               RuntimeInputActionSource::UI );
        }
        if ( uiCommands.physics.requestObjectFrictionCoeff )
        {
            liveConfig.objectFrictionCoeff = std::clamp( uiCommands.physics.requestedObjectFrictionCoeff,
                                                         UI_FRICTION_COEFF_MIN,
                                                         UI_FRICTION_COEFF_MAX );
            runtimePhysicsConfigChanged = true;
            UpdateRuntimeInputModeAfterAction( RuntimeInputAction::ApplyPhysicsFrictionSettings,
                                               RuntimeInputActionSource::UI );
        }
        if ( uiCommands.physics.requestRollingFrictionCoeff )
        {
            liveConfig.rollingFrictionCoeff = std::clamp( uiCommands.physics.requestedRollingFrictionCoeff,
                                                          UI_ROLLING_FRICTION_COEFF_MIN,
                                                          UI_ROLLING_FRICTION_COEFF_MAX );
            runtimePhysicsConfigChanged = true;
            UpdateRuntimeInputModeAfterAction( RuntimeInputAction::ApplyPhysicsFrictionSettings,
                                               RuntimeInputActionSource::UI );
        }
        if ( runtimePhysicsConfigChanged )
        {
            // Invariant: GameModelCollection caches per-model runtime tuning so
            // existing bodies and newly added bodies must observe the same live
            // physics settings immediately after UI config edits.
            m_cGameModelCollection.ApplyRuntimeConfig( liveConfig );
        }
        const auto makeSceneGeneratedControlContext = [this, &liveConfig]() -> SceneRuntimeGeneratedControlContext
        {
            return SceneRuntimeGeneratedControlContext{ SceneState(),
                                                        m_sceneController.UIOverrides(),
                                                        m_camera,
                                                        m_sceneController,
                                                        liveConfig,
                                                        m_cWorldEnvironment,
                                                        m_systems.terrain.get(),
                                                        m_cGameModelCollection,
                                                        m_simulation,
                                                        m_runtimeTools,
                                                        m_renderBackendView.deviceLifecycle,
                                                        m_launchOptions.generatedObjectTypeOverride,
                                                        m_startup.gameModelCapacity };
        };
        const auto executeSceneGeneratedControlAction = [this]( const SceneRuntimeGeneratedControlAction& action )
        {
            if ( action.resetReplayTimeline )
            {
                ResetReplayTimelineForActiveScene();
            }
            if ( action.scheduleProfileReset )
            {
                PROFILE_SCHEDULE_RESET();
            }
        };
        if ( uiCommands.sceneOptions.requestedModelCount >= 0 )
        {
            executeSceneGeneratedControlAction(
                ApplyUIModelCountOverride( makeSceneGeneratedControlContext(),
                                           uiCommands.sceneOptions.requestedModelCount ) );
            UpdateRuntimeInputModeAfterAction( RuntimeInputAction::SetModelCount, RuntimeInputActionSource::UI );
        }
        if ( uiCommands.profiler.requestedWorkerThreads >= -1 )
        {
            ApplyWorkerThreadCountOverride( m_config,
                                            *m_systems.workerPool,
                                            uiCommands.profiler.requestedWorkerThreads );
            UpdateRuntimeInputModeAfterAction( RuntimeInputAction::SetWorkerThreads, RuntimeInputActionSource::UI );
        }
        if ( uiCommands.run.requestedSolverBallCount >= 0 )
        {
            const int modelCapacity = m_startup.gameModelCapacity;
            const int boxes = m_sceneController.UIOverrides().solverBoxCountOverride >= 0
                                  ? m_sceneController.UIOverrides().solverBoxCountOverride
                                  : SceneState().solverBoxCount;
            executeSceneGeneratedControlAction( ApplyUISolverObjectCounts(
                makeSceneGeneratedControlContext(),
                std::clamp( uiCommands.run.requestedSolverBallCount, 0, (std::max)( 0, modelCapacity - boxes ) ),
                boxes ) );
            UpdateRuntimeInputModeAfterAction( RuntimeInputAction::SetSolverCounts, RuntimeInputActionSource::UI );
        }
        if ( uiCommands.run.requestedSolverBoxCount >= 0 )
        {
            const int modelCapacity = m_startup.gameModelCapacity;
            const int balls = m_sceneController.UIOverrides().solverBallCountOverride >= 0
                                  ? m_sceneController.UIOverrides().solverBallCountOverride
                                  : SceneState().solverBallCount;
            executeSceneGeneratedControlAction( ApplyUISolverObjectCounts(
                makeSceneGeneratedControlContext(),
                balls,
                std::clamp( uiCommands.run.requestedSolverBoxCount, 0, (std::max)( 0, modelCapacity - balls ) ) ) );
            UpdateRuntimeInputModeAfterAction( RuntimeInputAction::SetSolverCounts, RuntimeInputActionSource::UI );
        }
        if ( uiCommands.water.requestWorldGravity || uiCommands.water.requestWorldFluidHeight ||
             uiCommands.water.requestWorldFluidDensity )
        {
            const float gravity = uiCommands.water.requestWorldGravity ? uiCommands.water.requestedWorldGravity
                                                                       : m_cWorldEnvironment.GetGravity();
            const float fluidHeight = uiCommands.water.requestWorldFluidHeight
                                          ? uiCommands.water.requestedWorldFluidHeight
                                          : m_cWorldEnvironment.GetFluidSurfaceHeight();
            const float fluidDensity = uiCommands.water.requestWorldFluidDensity
                                           ? uiCommands.water.requestedWorldFluidDensity
                                           : m_cWorldEnvironment.GetFluidDensity();
            ApplyUIWorldOverride( m_cWorldEnvironment,
                                  m_replayRuntime,
                                  std::clamp( gravity, -100.0f, 0.0f ),
                                  std::clamp( fluidHeight, -100.0f, 200.0f ),
                                  std::clamp( fluidDensity, 0.0f, 5.0f ) );
            UpdateRuntimeInputModeAfterAction( RuntimeInputAction::ApplyWorldWaterSettings,
                                               RuntimeInputActionSource::UI );
        }
        if ( uiCommands.cinematic.toggleRendering )
        {
            // Master Cine switch. Clearing m_launchOptions.hasCinematicRenderingOverride lets
            // the runtime toggle become the new source of truth after launch.
            CinematicRenderConfig& cinematic = RuntimeActiveCinematicConfig( SceneState(), m_config );
            const bool currentlyEnabled =
                m_launchOptions.hasCinematicRenderingOverride ? m_launchOptions.cinematicRendering : cinematic.enabled;
            cinematic.enabled = !currentlyEnabled;
            m_launchOptions.hasCinematicRenderingOverride = false;
            if ( SceneState().isSceneMode )
            {
                SceneState().hasCinematicRenderingOverride = true;
                SceneState().isCinematicRenderingEnabled = cinematic.enabled;
                SceneState().cinematicOverrideMask |= SCENE_CINE_RENDERING;
                SceneState().uiCinematicOverrideMask |= SCENE_CINE_RENDERING;
            }
            UpdateRuntimeInputModeAfterAction( RuntimeInputAction::ToggleCinematicRendering,
                                               RuntimeInputActionSource::UI );
        }
        if ( uiCommands.cinematic.saveSkyDefaults )
        {
            m_runtimeCommands.Push( RuntimeCommand{ RuntimeCommandType::SaveSkyDefaults } );
            UpdateRuntimeInputModeAfterAction( RuntimeInputAction::SaveSkyDefaults, RuntimeInputActionSource::UI );
        }
        if ( uiCommands.cinematic.requestedModeSceneIndex >= -1 )
        {
            EnterInteractiveSceneRun();
            ApplyCinematicModeFromBrowserIndex(
                SceneRuntimeStyleContext{ m_launchOptions,
                                          SceneState(),
                                          m_sceneController.Browser(),
                                          m_cGameModelCollection,
                                          m_systems.assets,
                                          RuntimeActiveCinematicConfig( SceneState(), m_config ),
                                          m_defaultCinematicRender },
                uiCommands.cinematic.requestedModeSceneIndex );
            UpdateRuntimeInputModeAfterAction( RuntimeInputAction::SelectCinematicScene, RuntimeInputActionSource::UI );
        }
        if ( uiCommands.cinematic.requestedFeature != UICinematicFeature::None )
        {
            CinematicRenderConfig& cinematic = RuntimeActiveCinematicConfig( SceneState(), m_config );
            if ( uiCommands.cinematic.requestedFeature == UICinematicFeature::Shadows )
            {
                m_launchOptions.hasCinematicShadowsOverride = false;
            }
            ToggleCinematicUIFeature( cinematic, SceneState(), uiCommands.cinematic.requestedFeature );
            UpdateRuntimeInputModeAfterAction( RuntimeInputAction::ToggleCinematicFeature,
                                               RuntimeInputActionSource::UI );
        }
        if ( uiCommands.cinematic.requestedParam != UICinematicParam::None )
        {
            CinematicRenderConfig& cinematic = RuntimeActiveCinematicConfig( SceneState(), m_config );
            ApplyCinematicUIParam( cinematic,
                                   SceneState(),
                                   uiCommands.cinematic.requestedParam,
                                   uiCommands.cinematic.requestedValue );
            UpdateRuntimeInputModeAfterAction( RuntimeInputAction::ApplyCinematicParam, RuntimeInputActionSource::UI );
        }
        if ( uiCommands.scene.resetScene )
        {
            m_runtimeCommands.Push( RuntimeCommand{ RuntimeCommandType::ResetCurrentScene } );
            UpdateRuntimeInputModeAfterAction( RuntimeInputAction::ResetScene, RuntimeInputActionSource::UI );
        }
        if ( uiCommands.scene.resetSceneDefaults )
        {
            RuntimeCommand command{ RuntimeCommandType::ResetCurrentScene };
            command.preserveUIState = false;
            command.preserveRuntimeState = false;
            m_runtimeCommands.Push( std::move( command ) );
            UpdateRuntimeInputModeAfterAction( RuntimeInputAction::ResetSceneDefaults, RuntimeInputActionSource::UI );
        }
        if ( uiCommands.scene.requestDemoScene )
        {
            m_runtimeCommands.Push( RuntimeCommand{ RuntimeCommandType::LoadDemoScene } );
            UpdateRuntimeInputModeAfterAction( RuntimeInputAction::LoadDemoScene, RuntimeInputActionSource::UI );
        }
        if ( uiCommands.scene.saveSceneDefaults )
        {
            m_runtimeCommands.Push( RuntimeCommand{ RuntimeCommandType::SaveSceneDefaults } );
            UpdateRuntimeInputModeAfterAction( RuntimeInputAction::SaveSceneDefaults, RuntimeInputActionSource::UI );
        }
        if ( uiCommands.scene.createScene )
        {
            RuntimeCommand command{ RuntimeCommandType::CreateScene };
            command.text = uiCommands.scene.requestedSceneName;
            m_runtimeCommands.Push( std::move( command ) );
            UpdateRuntimeInputModeAfterAction( RuntimeInputAction::CreateScene, RuntimeInputActionSource::UI );
        }
        if ( uiCommands.scene.requestedSceneIndex >= 0 )
        {
            RuntimeCommand command{ RuntimeCommandType::LoadSceneIndex };
            command.index = uiCommands.scene.requestedSceneIndex;
            m_runtimeCommands.Push( command );
            UpdateRuntimeInputModeAfterAction( RuntimeInputAction::SelectScene, RuntimeInputActionSource::UI );
        }

        RunUIStressActions();

        TickAttachedCameraOrbitInput( editorUnhandledWheelDelta );
        TickEditorViewportAndPlacementScaleInput( editorUnhandledWheelDelta );
    }

    // Editor, replay, and launcher actions share world clicks. UI interaction
    // and capture suppress them so panel controls never mutate the scene.
    const RuntimeMouseEdges mouseEdges =
        m_runtimeInput.CaptureMouseButtons( Input::IsLeftMouseDown(), Input::IsRightMouseDown() );
    const RuntimeInputSnapshot inputSnapshot = BuildRuntimeInputSnapshot( mouseEdges, suppressWorldActionThisFrame );
    RouteRuntimePointerInput( inputSnapshot, mouseEdges );

    if ( m_UI.BlocksKeyboard() )
    {
        AdvanceTakeInputKeyboardActionMemories( m_runtimeInput );
        CancelCameraLookGesture();
        InputController::ResetMouseLook( m_camera );
        m_camera.input.Set( InputState::Up, false );
        m_camera.input.Set( InputState::Down, false );
        m_camera.input.Set( InputState::Left, false );
        m_camera.input.Set( InputState::Right, false );
        ApplyCursorOwnership();
        return;
    }

    DispatchPostUIKeyboardActions();

    const RuntimeInteractionFramePolicy inputPolicy = m_interaction.BuildFramePolicy( inputSnapshot.frameInput );
    const bool mouseLookOwnsCursor = MouseLookOwnsCursor();
    SyncCameraLookGesture( inputSnapshot, inputPolicy, mouseLookOwnsCursor );
    const bool cameraMouseLookActive =
        inputPolicy.cameraMouseLookActive && mouseLookOwnsCursor && inputSnapshot.appFocused;
    const bool cameraKeyboardControlsActive = inputPolicy.cameraKeyboardControlsActive;
    if ( cameraMouseLookActive )
    {
        // Diagnostics UI owns the native cursor; mouse-look hides it while
        // consuming raw Win32 deltas, with cursor-position deltas as a
        // remote-desktop friendly fallback when raw input is unavailable.
        if ( !Input::IsAppFocused() )
        {
            InputController::ResetMouseLook( m_camera );
        }
        else if ( !MouseLookOwnsCursor() )
        {
            ApplyCursorOwnership();
            InputController::ResetMouseLook( m_camera );
        }
        else
        {
            Input::SetSystemCursorVisible( false );
            long rawX = 0;
            long rawY = 0;
            const bool hasRawDelta = Input::ConsumeRawMouseDelta( rawX, rawY );
            POINT currentClient = Input::GetClientMouseCoordinates();

            if ( m_camera.needsMouseLookReset )
            {
                m_camera.input.xMove = 0;
                m_camera.input.yMove = 0;
                m_camera.mouseLookLastClient = currentClient;
                m_camera.hasMouseLookLastClient = true;
                m_camera.needsMouseLookReset = false;
            }
            else if ( hasRawDelta )
            {
                InputController::SetMouseLookDelta( m_camera, rawX, rawY );
                m_camera.mouseLookLastClient = currentClient;
                m_camera.hasMouseLookLastClient = true;
            }
            else if ( !m_camera.hasMouseLookLastClient )
            {
                m_camera.input.xMove = 0;
                m_camera.input.yMove = 0;
                m_camera.mouseLookLastClient = currentClient;
                m_camera.hasMouseLookLastClient = true;
            }
            else
            {
                InputController::SetMouseLookDelta( m_camera,
                                                    currentClient.x - m_camera.mouseLookLastClient.x,
                                                    currentClient.y - m_camera.mouseLookLastClient.y );
                m_camera.mouseLookLastClient = currentClient;
            }
        }
    }
    else
    {
        InputController::ResetMouseLook( m_camera );
        ApplyCursorOwnership();
    }

    if ( cameraKeyboardControlsActive )
    {
        // WASD movement
        m_camera.input.Set( InputState::Up, Input::IsKeyDown( 'W' ) );
        m_camera.input.Set( InputState::Left, Input::IsKeyDown( 'A' ) );
        m_camera.input.Set( InputState::Down, Input::IsKeyDown( 'S' ) );
        m_camera.input.Set( InputState::Right, Input::IsKeyDown( 'D' ) );
    }
    else
    {
        InputController::ResetMouseLook( m_camera );
        m_camera.input.Set( InputState::Up, false );
        m_camera.input.Set( InputState::Down, false );
        m_camera.input.Set( InputState::Left, false );
        m_camera.input.Set( InputState::Right, false );
    }

    DrainRuntimeCommands();
}


bool Run::DrainRuntimeCommands()
{
    auto executeSceneControlAction = [&]( const SceneRuntimeControlAction& action ) -> bool
    {
        if ( action.enterInteractiveSceneRun )
        {
            EnterInteractiveSceneRun();
        }

        switch ( action.type )
        {
        case SceneRuntimeControlActionType::ClearCurrentSceneAutomation:
            SceneState().isExitOnComplete = false;
            m_diagnosticsRuntime.Capture().Screenshot().isScreenshotAndExit = false;
            return true;
        case SceneRuntimeControlActionType::LoadScene:
            return LoadScene( action.index,
                              action.preserveUIState,
                              action.suppressExitOnComplete,
                              action.preserveRuntimeState )
                .ok;
        case SceneRuntimeControlActionType::ApplyCinematicModeFromBrowserIndex:
            EnterInteractiveSceneRun();
            return ApplyCinematicModeFromBrowserIndex(
                SceneRuntimeStyleContext{ m_launchOptions,
                                          SceneState(),
                                          m_sceneController.Browser(),
                                          m_cGameModelCollection,
                                          m_systems.assets,
                                          RuntimeActiveCinematicConfig( SceneState(), m_config ),
                                          m_defaultCinematicRender },
                action.index );
        case SceneRuntimeControlActionType::None:
            return false;
        }
        return false;
    };

    bool processed = false;
    RuntimeCommand command;
    while ( m_runtimeCommands.TryPop( command ) )
    {
        processed = true;
        switch ( command.type )
        {
        case RuntimeCommandType::LoadSceneIndex:
            executeSceneControlAction(
                m_sceneCoordinator.LoadSceneFromBrowserIndex( command.index, m_sceneController.Browser().paths ) );
            break;
        case RuntimeCommandType::LoadDemoScene:
            executeSceneControlAction( m_sceneCoordinator.LoadDemoSceneFromUI() );
            break;
        case RuntimeCommandType::ResetCurrentScene:
            EnterInteractiveSceneRun();
            executeSceneControlAction( m_sceneCoordinator.ResetCurrentScene( command.preserveUIState,
                                                                             command.suppressExitOnComplete,
                                                                             command.preserveRuntimeState ) );
            break;
        case RuntimeCommandType::CreateScene:
            executeSceneControlAction(
                CreateSceneFromUI( SceneRuntimeCreateContext{ m_sceneController, m_sceneController.Browser() },
                                   command.text.c_str() ) );
            break;
        case RuntimeCommandType::SaveScreenshot:
            if ( !command.text.empty() )
            {
                SaveScreenshot( command.text.c_str() );
            }
            break;
        case RuntimeCommandType::SaveSceneDefaults:
            SaveCurrentSceneDefaults();
            break;
        case RuntimeCommandType::SaveRenderDefaults:
            SaveRenderDefaults( m_config.ordinaryRender );
            break;
        case RuntimeCommandType::SaveSkyDefaults:
            SaveSkyDefaults( RuntimeActiveCinematicConfig( SceneState(), m_config ) );
            break;
        case RuntimeCommandType::AdvanceScene:
            if ( !executeSceneControlAction( m_sceneCoordinator.AdvanceScene( m_diagnosticsRuntime.PerfTestActive(),
                                                                              sPerfPass,
                                                                              SceneState().isInteractiveRun ) ) )
            {
                PostQuitMessage( 0 );
            }
            break;
        case RuntimeCommandType::Quit:
            PostQuitMessage( 0 );
            break;
        case RuntimeCommandType::None:
            break;
        }
        if ( command.type != RuntimeCommandType::None )
        {
            m_replayRuntime.RecordEvent(
                ReplayEventKind::RuntimeCommand,
                m_replayRuntime.NextEventFrameIndex(),
                ReplayRuntimeCommandFlags( command ),
                static_cast<int32_t>( command.type ),
                command.index,
                0,
                0,
                0,
                command.text.empty() ? ReplayRuntimeCommandName( command.type ) : command.text.c_str() );
        }
    }

    if ( processed )
    {
        RefreshRuntimeViewModel();
    }
    return processed;
}


void Run::MoveCamera( float keyMovementQty, float mouseMovementQty )
{
    const bool hasCameraTravelInput = m_camera.input.Get( InputState::Up ) || m_camera.input.Get( InputState::Down ) ||
                                      m_camera.input.Get( InputState::Left ) || m_camera.input.Get( InputState::Right );
    const bool attachedOrbitOwnsCamera = IsAttachedCameraMode() && m_attachedCamera.activeFollow &&
                                         m_attachedCamera.submode != AttachedCameraSubmode::RagdollEyes;
    if ( !attachedOrbitOwnsCamera && ( IsFlyCameraMode() || MouseLookOwnsCursor() ||
                                       m_runtimeTools.Editor().viewportLookActive || hasCameraTravelInput ) )
    {
        // Shift held = 3x speed
        float speedMult = Input::IsKeyDown( VK_SHIFT ) ? 3.0f : 1.0f;

        // Mouse look
        if ( ( !m_runtimeTools.Editor().editorModeEnabled || m_runtimeTools.Editor().viewportLookActive ) &&
             ( m_camera.input.xMove != 0 || m_camera.input.yMove != 0 ) )
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

    // Passive generated-demo camera bounds do not own manual or pinned follow views.
    if ( !IsManualCameraMode() && !m_runtimeTools.Editor().viewportLookActive && !SceneState().isSceneMode )
    {
        Vector3 translatedCameraPosition = m_systems.cameras->GetCameraTranslation();
        float minY =
            m_systems.terrain->GetTerrainHeightAt( translatedCameraPosition.x, translatedCameraPosition.z, true ) +
            m_config.minCameraHeight;
        if ( minY > translatedCameraPosition.y )
        {
            m_systems.cameras->AmmendPrimaryY( minY );
        }
        else if ( translatedCameraPosition.y > m_config.maxCameraHeight )
        {
            m_systems.cameras->AmmendPrimaryY( m_config.maxCameraHeight );
        }
    }
}
