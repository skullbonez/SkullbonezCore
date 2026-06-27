/*
File: SkullbonezSource/Runtime/RunInput.cpp
Purpose:
  Routes raw keyboard, mouse, and UI commands into runtime state changes.

Mental model:
  Input arbitration stays here.
  Editor, launcher, and replay behavior live in dedicated runtime files.

Glossary:
  DXR (DirectX Raytracing): DX12 API used for hardware ray traversal and
  reflection dispatch.
  Validation gate: Repository script that proves a class of changes before
  commit or PR.

Related:
  - Agentic/Reference/runtime-reference.md
  - Agentic/Reference/comment-style-guide.md
*/
#include "RunInternal.h"
#include "Editor/EditorTools.h"
#include "InputController.h"
#include "Replay/ReplayOverlayLayout.h"
#include "RuntimePickService.h"
#include "Scene/SceneRuntimeCreate.h"
#include "RuntimeTuning.h"
#include "Scene/SceneRuntimeDefaults.h"
#include "Scene/SceneRuntimeGeneratedControls.h"
#include "Scene/SceneRuntimeLoad.h"
#include "Scene/SceneRuntimeStyle.h"
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
constexpr int ATTACHED_CAMERA_WHEEL_DELTA = 120;
constexpr float ATTACHED_CAMERA_ORBIT_DEFAULT_PITCH = 0.30f;
constexpr float ATTACHED_CAMERA_ORBIT_MOUSE_PITCH_MIN = -1.35f;
constexpr float ATTACHED_CAMERA_ORBIT_MOUSE_PITCH_MAX = 1.35f;
constexpr float ATTACHED_CAMERA_ORBIT_MIN_DISTANCE_RADIUS = 1.25f;
constexpr float ATTACHED_CAMERA_ORBIT_MAX_DISTANCE_RADIUS = 40.0f;
constexpr float ATTACHED_CAMERA_ORBIT_WHEEL_FACTOR = 0.88f;

bool CameraModeUsesFlyControls( RunCameraMode mode, bool attachActiveFollow )
{
    return mode == RunCameraMode::Inspect || mode == RunCameraMode::Launcher || mode == RunCameraMode::Manipulator ||
           ( mode == RunCameraMode::Attach && attachActiveFollow );
}


bool CameraModeUsesLauncher( RunCameraMode mode )
{
    return mode == RunCameraMode::Launcher;
}

bool IsFiniteVector( const Vector3& v )
{
    return std::isfinite( v.x ) && std::isfinite( v.y ) && std::isfinite( v.z );
}

bool TryNormalizeVector( Vector3& v )
{
    if ( !IsFiniteVector( v ) )
    {
        return false;
    }
    const float lengthSq = VectorMagSquared( v );
    if ( lengthSq <= TOLERANCE * TOLERANCE )
    {
        return false;
    }
    v *= 1.0f / sqrtf( lengthSq );
    return true;
}

Vector3 NormalizedOr( Vector3 v, const Vector3& fallback )
{
    if ( TryNormalizeVector( v ) )
    {
        return v;
    }
    Vector3 safeFallback = fallback;
    if ( TryNormalizeVector( safeFallback ) )
    {
        return safeFallback;
    }
    return Vector3( 0.0f, 1.0f, 0.0f );
}

RotationMatrix ModelRotation( const GameModel& model )
{
    Quaternion orientation = model.GetOrientation();
    return orientation.GetOrientationMatrix();
}

Vector3 ModelToWorldVector( const GameModel& model, const Vector3& localVector )
{
    return ModelRotation( model ) * localVector;
}

Vector3 WorldToModelVector( const GameModel& model, const Vector3& worldVector )
{
    return ModelRotation( model ).TransposeMultiply( worldVector );
}

float AttachedCameraModelRadius( const GameModel& model )
{
    return (std::max)( GetShapeBoundingRadius( model.GetCollisionShape() ), 1.0f );
}

bool IsSimpleRagdollPart( const GameModel& model )
{
    return model.GetRuntimeCollectionKind() == SkullbonezCore::GameObjects::GameModelCollectionKind::SimpleRagdoll;
}

bool EndsWith( const char* value, const char* suffix )
{
    if ( !value || !suffix )
    {
        return false;
    }
    const size_t valueLength = strlen( value );
    const size_t suffixLength = strlen( suffix );
    return valueLength >= suffixLength && strcmp( value + valueLength - suffixLength, suffix ) == 0;
}


RuntimeInputModeState
BuildRuntimeInputModeState( RunCameraMode mode, const RunEditorPlacementState& editor, bool attachActiveFollow )
{
    RuntimeInputModeState state;
    state.flyCamera = CameraModeUsesFlyControls( mode, attachActiveFollow );
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

float WrapAttachedCameraOrbitYaw( float yaw )
{
    while ( yaw > _PI )
    {
        yaw -= _2PI;
    }
    while ( yaw < -_PI )
    {
        yaw += _2PI;
    }
    return yaw;
}

float AttachedCameraOrbitMinDistance( const GameModel& model )
{
    return (std::max)( 1.0f, AttachedCameraModelRadius( model ) * ATTACHED_CAMERA_ORBIT_MIN_DISTANCE_RADIUS );
}

float AttachedCameraOrbitMaxDistance( const GameModel& model )
{
    const float minDistance = AttachedCameraOrbitMinDistance( model );
    return (std::max)( minDistance + 1.0f,
                       AttachedCameraModelRadius( model ) * ATTACHED_CAMERA_ORBIT_MAX_DISTANCE_RADIUS );
}

float ClampAttachedCameraOrbitDistance( const GameModel& model, float distance )
{
    if ( !std::isfinite( distance ) )
    {
        distance = AttachedCameraModelRadius( model ) * 8.0f;
    }
    return std::clamp( distance, AttachedCameraOrbitMinDistance( model ), AttachedCameraOrbitMaxDistance( model ) );
}

float ClampAttachedCameraOrbitPitch( float pitch )
{
    if ( !std::isfinite( pitch ) )
    {
        return ATTACHED_CAMERA_ORBIT_DEFAULT_PITCH;
    }
    return std::clamp( pitch, ATTACHED_CAMERA_ORBIT_MOUSE_PITCH_MIN, ATTACHED_CAMERA_ORBIT_MOUSE_PITCH_MAX );
}

Vector3 AttachedCameraOrbitOffset( float yaw, float pitch, float distance )
{
    const float cosPitch = cosf( pitch );
    return Vector3( sinf( yaw ) * cosPitch * distance, sinf( pitch ) * distance, cosf( yaw ) * cosPitch * distance );
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

struct RuntimeInputKeyBinding
{
    RuntimeInputAction action;
    int virtualKey;
};

void AdvanceTakeInputKeyboardActionMemories( RuntimeInputContext& input )
{
    static const RuntimeInputKeyBinding kBindings[] = { { RuntimeInputAction::ToggleFlyCamera, 'F' },
                                                        { RuntimeInputAction::ToggleLauncher, 'N' },
                                                        { RuntimeInputAction::CycleCameraMode, VK_TAB },
                                                        { RuntimeInputAction::CycleAttachedCameraSubmode, VK_F1 },
                                                        { RuntimeInputAction::ToggleAttachedCameraPin, VK_RETURN },
                                                        { RuntimeInputAction::ToggleEditor, VK_OEM_3 },
                                                        { RuntimeInputAction::ToggleEditorTool, VK_MENU },
                                                        { RuntimeInputAction::CycleLauncherFireMode, 'M' },
                                                        { RuntimeInputAction::WriteLauncherReproSnapshot, VK_RETURN },
                                                        { RuntimeInputAction::ToggleWaterFreeze, '1' },
                                                        { RuntimeInputAction::CycleWaterReflection, '2' },
                                                        { RuntimeInputAction::ToggleWaterFlat, '3' },
                                                        { RuntimeInputAction::ToggleTerrainHidden, '4' },
                                                        { RuntimeInputAction::ToggleWaterHidden, '5' },
                                                        { RuntimeInputAction::ToggleCollisionVisualizer, 'V' },
                                                        { RuntimeInputAction::CyclePhysicsDebugOverlay, 'C' },
                                                        { RuntimeInputAction::ToggleTerrainContactProbe, 'O' },
                                                        { RuntimeInputAction::StepPhysicsPipelinePrevious, VK_F7 },
                                                        { RuntimeInputAction::StepPhysicsPipelineNext, VK_F8 },
                                                        { RuntimeInputAction::TogglePhysicsDebugTransparent, '6' },
                                                        { RuntimeInputAction::ReportRendererRuntimeRetired, 'Q' },
                                                        { RuntimeInputAction::ToggleBroadphaseOverlay, 'G' },
                                                        { RuntimeInputAction::ToggleUIVisibility, '0' },
                                                        { RuntimeInputAction::NavigateScenePrevious, VK_LEFT },
                                                        { RuntimeInputAction::NavigateSceneNext, VK_RIGHT },
                                                        { RuntimeInputAction::DismissOrExitUI, VK_ESCAPE },
                                                        { RuntimeInputAction::SaveSceneSnapshot, VK_F2 },
                                                        { RuntimeInputAction::SaveScreenshot, VK_F3 },
                                                        { RuntimeInputAction::ResetScene, 'R' },
                                                        { RuntimeInputAction::ResetSceneFromBackspace, VK_BACK } };

    for ( std::size_t i = 0; i < sizeof( kBindings ) / sizeof( kBindings[0] ); ++i )
    {
        input.SetActionDown( kBindings[i].action, Input::IsKeyDown( kBindings[i].virtualKey ) );
    }
}

} // namespace

void Run::StepPhysicsPipelineStage( int direction )
{
    const int stageCount = static_cast<int>( PhysicsPipelineStage::Count );
    if ( stageCount <= 0 || direction == 0 )
    {
        return;
    }

    m_debug.physicsDebugFlags |= PHYSICS_DEBUG_PIPELINE;
    int nextStage = ( m_debug.physicsDebugPipelineStageCursor + direction ) % stageCount;
    if ( nextStage < 0 )
    {
        nextStage += stageCount;
    }
    m_debug.physicsDebugPipelineStageCursor = nextStage;
}


void Run::UpdateRuntimeInputModeAfterAction( RuntimeInputAction action, RuntimeInputActionSource source )
{
    InputController::ApplyModeAction(
        m_runtimeInput,
        InputController::ResolveMode(
            BuildRuntimeInputModeState( m_camera.mode, m_runtimeTools.Editor(), m_attachedCamera.activeFollow ) ),
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
                m_cGameModelCollection.GetModelCount() );
            if ( m_runtimeTools.FireLauncherRay( m_cGameModelCollection,
                                                 m_cWorldEnvironment,
                                                 m_systems.terrain.get(),
                                                 ActiveGameModelCapacity(),
                                                 rayOrigin,
                                                 rayDirection,
                                                 cameraUp ) )
            {
                SceneState().modelCount = m_cGameModelCollection.GetModelCount();
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
    mode = NormalizeCameraModeForCurrentScene( mode );
    switch ( mode )
    {
    case RunCameraMode::Demo:
    case RunCameraMode::Scene:
        return m_interaction.EnterLive();
    case RunCameraMode::Inspect:
    case RunCameraMode::Attach:
        return m_interaction.EnterInspect();
    case RunCameraMode::Launcher:
        return m_interaction.EnterLauncher();
    case RunCameraMode::Manipulator:
        return m_interaction.EnterManipulator();
    default:
        return m_interaction.EnterLive();
    }
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
        if ( command.modelIndex < -1 || command.modelIndex >= m_cGameModelCollection.GetModelCount() )
        {
            return false;
        }

        const int previousModelIndex = m_runtimeTools.Editor().selectedModelIndex;
        const bool selectionHit = command.modelIndex >= 0;
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
        m_runtimeTools.Editor().selectedModelIndex = command.modelIndex;
        if ( previousModelIndex != command.modelIndex )
        {
            RuntimeInteractionEvent event;
            event.type = RuntimeInteractionEventType::SelectionChanged;
            event.previousModelIndex = previousModelIndex;
            event.modelIndex = command.modelIndex;
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
    return m_cGameModelCollection.GetModelCount() > 0;
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
    return CameraModeUsesFlyControls( m_camera.mode, m_attachedCamera.activeFollow );
}


bool Run::IsManualCameraMode() const
{
    return IsFlyCameraMode() || IsAttachedCameraMode();
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

    m_attachedCamera.returnEye = m_systems.cameras->GetCameraTranslation();
    m_attachedCamera.returnView = m_systems.cameras->GetCameraView();
    m_attachedCamera.returnUp = m_systems.cameras->GetCameraUp();
    m_attachedCamera.hasReturnCameraPose = true;
}


void Run::RestoreAttachedCameraReturnState()
{
    if ( !m_attachedCamera.hasReturnCameraPose || !m_systems.cameras )
    {
        return;
    }

    m_systems.cameras->CancelTween();
    m_systems.cameras->SetPrimaryPosition( m_attachedCamera.returnEye );
    m_systems.cameras->SetViewCoordinates( m_attachedCamera.returnView );
    m_systems.cameras->SetPrimaryUp( m_attachedCamera.returnUp );
    m_systems.cameras->SetCamera();
    m_attachedCamera.hasReturnCameraPose = false;
}


void Run::ClearAttachedCameraTarget()
{
    m_attachedCamera.target = AttachedCameraTarget{};
    m_attachedCamera.hasFixedOffset = false;
    m_attachedCamera.hasOrbit = false;
    m_attachedCamera.hasLastLookDirection = false;
}


bool Run::TryResolveAttachedCameraTarget( int& outModelIndex )
{
    outModelIndex = -1;
    const std::vector<GameModel>& models = m_cGameModelCollection.Models();
    const int cachedIndex = m_attachedCamera.target.modelIndex;
    if ( cachedIndex >= 0 && cachedIndex < static_cast<int>( models.size() ) )
    {
        const GameModel& model = models[static_cast<std::size_t>( cachedIndex )];
        const bool hasReplayId = m_attachedCamera.target.replayBodyId != 0;
        const bool hasName = m_attachedCamera.target.name[0] != '\0';
        bool cachedIndexMatches = true;
        if ( hasReplayId )
        {
            cachedIndexMatches = model.GetReplayBodyId() == m_attachedCamera.target.replayBodyId;
        }
        if ( cachedIndexMatches && hasName )
        {
            cachedIndexMatches = strcmp( model.GetName(), m_attachedCamera.target.name ) == 0;
        }
        if ( cachedIndexMatches )
        {
            outModelIndex = cachedIndex;
            return true;
        }
    }

    if ( m_attachedCamera.target.replayBodyId != 0 )
    {
        int match = -1;
        for ( int i = 0; i < static_cast<int>( models.size() ); ++i )
        {
            if ( models[static_cast<std::size_t>( i )].GetReplayBodyId() == m_attachedCamera.target.replayBodyId )
            {
                if ( match >= 0 )
                {
                    ClearAttachedCameraTarget();
                    return false;
                }
                match = i;
            }
        }
        if ( match >= 0 )
        {
            m_attachedCamera.target.modelIndex = match;
            outModelIndex = match;
            return true;
        }
    }

    if ( m_attachedCamera.target.name[0] != '\0' )
    {
        int match = -1;
        for ( int i = 0; i < static_cast<int>( models.size() ); ++i )
        {
            if ( strcmp( models[static_cast<std::size_t>( i )].GetName(), m_attachedCamera.target.name ) == 0 )
            {
                if ( match >= 0 )
                {
                    ClearAttachedCameraTarget();
                    return false;
                }
                match = i;
            }
        }
        if ( match >= 0 )
        {
            m_attachedCamera.target.modelIndex = match;
            m_attachedCamera.target.replayBodyId = models[static_cast<std::size_t>( match )].GetReplayBodyId();
            outModelIndex = match;
            return true;
        }
    }

    ClearAttachedCameraTarget();
    return false;
}


void Run::CaptureAttachedCameraFixedOffset( const GameModel& model )
{
    if ( !m_systems.cameras )
    {
        return;
    }

    const Vector3 targetPosition = model.GetPosition();
    const Vector3 eye = m_systems.cameras->GetCameraTranslation();
    const Vector3 view = m_systems.cameras->GetCameraView();
    const Vector3 up = m_systems.cameras->GetCameraUp();
    m_attachedCamera.localEyeOffset = WorldToModelVector( model, eye - targetPosition );
    m_attachedCamera.localViewOffset = WorldToModelVector( model, view - targetPosition );
    m_attachedCamera.localUp = NormalizedOr( WorldToModelVector( model, up ), Vector3( 0.0f, 1.0f, 0.0f ) );
    Vector3 look = view - eye;
    if ( TryNormalizeVector( look ) )
    {
        m_attachedCamera.lastLookDirection = look;
        m_attachedCamera.hasLastLookDirection = true;
    }
    m_attachedCamera.hasFixedOffset = true;
    CaptureAttachedCameraOrbit( model );
}


void Run::CaptureAttachedCameraOrbit( const GameModel& model )
{
    if ( !m_systems.cameras )
    {
        return;
    }

    const Vector3 targetPosition = model.GetPosition();
    Vector3 offset = m_systems.cameras->GetCameraTranslation() - targetPosition;
    float distance = sqrtf( VectorMagSquared( offset ) );
    if ( !std::isfinite( distance ) || distance < AttachedCameraOrbitMinDistance( model ) )
    {
        Vector3 look = m_systems.cameras->GetCameraView() - m_systems.cameras->GetCameraTranslation();
        if ( !TryNormalizeVector( look ) )
        {
            look = Vector3( 0.0f, 0.0f, 1.0f );
        }
        distance = AttachedCameraModelRadius( model ) * 8.0f;
        offset = -look * distance;
    }

    const float pitchDistance = (std::max)( distance, 0.001f );
    const float normalizedY = std::clamp( offset.y / pitchDistance, -1.0f, 1.0f );
    m_attachedCamera.orbitDistance = ClampAttachedCameraOrbitDistance( model, distance );
    m_attachedCamera.orbitPitchRadians = ClampAttachedCameraOrbitPitch( asinf( normalizedY ) );
    m_attachedCamera.orbitYawRadians = WrapAttachedCameraOrbitYaw( atan2f( offset.x, offset.z ) );
    m_attachedCamera.hasOrbit = true;
}


void Run::SetAttachedCameraTarget( int modelIndex )
{
    const std::vector<GameModel>& models = m_cGameModelCollection.Models();
    if ( modelIndex < 0 || modelIndex >= static_cast<int>( models.size() ) )
    {
        ClearAttachedCameraTarget();
        return;
    }

    const GameModel& model = models[static_cast<std::size_t>( modelIndex )];
    m_attachedCamera.target.modelIndex = modelIndex;
    m_attachedCamera.target.replayBodyId = model.GetReplayBodyId();
    strncpy_s( m_attachedCamera.target.name, sizeof( m_attachedCamera.target.name ), model.GetName(), _TRUNCATE );
    RuntimeInteractionCommand command;
    command.type = RuntimeInteractionCommandType::SetEditorSelection;
    command.modelIndex = modelIndex;
    command.selectionScope = RuntimeInteractionSelectionScope::Inspect;
    command.claimSelectionOwner = false;
    ExecuteRuntimeInteractionCommand( command );
    m_attachedCamera.activeFollow = true;
    if ( m_attachedCamera.submode == AttachedCameraSubmode::RagdollEyes )
    {
        int headIndex = -1;
        if ( !TryResolveAttachedCameraRagdollHead( modelIndex, headIndex ) )
        {
            m_attachedCamera.submode = AttachedCameraSubmode::FixedRelative;
        }
    }
    CaptureAttachedCameraFixedOffset( model );
    ApplyCursorOwnership();
}


void Run::SeedAttachedCameraTargetFromSelection()
{
    int currentIndex = -1;
    if ( TryResolveAttachedCameraTarget( currentIndex ) )
    {
        CaptureAttachedCameraFixedOffset( m_cGameModelCollection.Models()[static_cast<std::size_t>( currentIndex )] );
        m_attachedCamera.activeFollow = true;
        ApplyCursorOwnership();
        return;
    }

    int seedIndex = -1;
    const RunReplayPathVisualizerState& path = m_replayRuntime.PathVisualizer();
    const std::vector<GameModel>& models = m_cGameModelCollection.Models();
    if ( path.hasTarget && path.targetModelIndex >= 0 && path.targetModelIndex < static_cast<int>( models.size() ) )
    {
        seedIndex = path.targetModelIndex;
    }
    else if ( m_runtimeTools.Editor().selectedModelIndex >= 0 &&
              m_runtimeTools.Editor().selectedModelIndex < static_cast<int>( models.size() ) )
    {
        seedIndex = m_runtimeTools.Editor().selectedModelIndex;
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
        request.models = &m_cGameModelCollection.Models();
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
    outHeadModelIndex = -1;
    const std::vector<GameModel>& models = m_cGameModelCollection.Models();
    if ( selectedModelIndex < 0 || selectedModelIndex >= static_cast<int>( models.size() ) )
    {
        return false;
    }

    const GameModel& selected = models[static_cast<std::size_t>( selectedModelIndex )];
    if ( !IsSimpleRagdollPart( selected ) )
    {
        return false;
    }

    const int rootModelIndex = selected.GetRuntimeCollectionRootModelIndex();
    for ( int i = 0; i < static_cast<int>( models.size() ); ++i )
    {
        const GameModel& candidate = models[static_cast<std::size_t>( i )];
        if ( candidate.GetRuntimeCollectionKind() ==
                 SkullbonezCore::GameObjects::GameModelCollectionKind::SimpleRagdoll &&
             candidate.GetRuntimeCollectionRootModelIndex() == rootModelIndex &&
             candidate.GetRuntimeCollectionPartIndex() == 1 )
        {
            outHeadModelIndex = i;
            return true;
        }
    }

    for ( int i = 0; i < static_cast<int>( models.size() ); ++i )
    {
        const GameModel& candidate = models[static_cast<std::size_t>( i )];
        if ( candidate.GetRuntimeCollectionKind() ==
                 SkullbonezCore::GameObjects::GameModelCollectionKind::SimpleRagdoll &&
             candidate.GetRuntimeCollectionRootModelIndex() == rootModelIndex &&
             EndsWith( candidate.GetName(), "_head" ) )
        {
            outHeadModelIndex = i;
            return true;
        }
    }
    return false;
}


void Run::CycleAttachedCameraSubmode()
{
    if ( !IsAttachedCameraMode() )
    {
        return;
    }
    int modelIndex = -1;
    if ( !TryResolveAttachedCameraTarget( modelIndex ) )
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
    if ( next != AttachedCameraSubmode::RagdollEyes || !m_attachedCamera.hasFixedOffset )
    {
        CaptureAttachedCameraFixedOffset( m_cGameModelCollection.Models()[static_cast<std::size_t>( modelIndex )] );
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
        int modelIndex = -1;
        if ( TryResolveAttachedCameraTarget( modelIndex ) )
        {
            CaptureAttachedCameraFixedOffset( m_cGameModelCollection.Models()[static_cast<std::size_t>( modelIndex )] );
        }
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

    const int wheelSteps = unhandledWheelDelta / ATTACHED_CAMERA_WHEEL_DELTA;
    if ( wheelSteps == 0 )
    {
        return;
    }

    int modelIndex = -1;
    if ( !TryResolveAttachedCameraTarget( modelIndex ) )
    {
        return;
    }

    const GameModel& target = m_cGameModelCollection.Models()[static_cast<std::size_t>( modelIndex )];
    if ( !m_attachedCamera.hasOrbit )
    {
        CaptureAttachedCameraOrbit( target );
    }

    const float nextDistance =
        m_attachedCamera.orbitDistance * powf( ATTACHED_CAMERA_ORBIT_WHEEL_FACTOR, static_cast<float>( wheelSteps ) );
    m_attachedCamera.orbitDistance = ClampAttachedCameraOrbitDistance( target, nextDistance );
    m_attachedCamera.hasOrbit = true;
    EnterInteractiveSceneRun();
}


void Run::TickAttachedCamera()
{
    if ( !IsAttachedCameraMode() || !m_attachedCamera.activeFollow || !m_systems.cameras )
    {
        return;
    }

    int modelIndex = -1;
    if ( !TryResolveAttachedCameraTarget( modelIndex ) )
    {
        return;
    }

    const std::vector<GameModel>& models = m_cGameModelCollection.Models();
    const GameModel& target = models[static_cast<std::size_t>( modelIndex )];
    if ( m_attachedCamera.submode == AttachedCameraSubmode::RagdollEyes )
    {
        int headIndex = -1;
        if ( TryResolveAttachedCameraRagdollHead( modelIndex, headIndex ) )
        {
            const GameModel& head = models[static_cast<std::size_t>( headIndex )];
            const float radius = (std::max)( 0.5f, AttachedCameraModelRadius( head ) );
            const Vector3 eye =
                head.GetPosition() + ModelToWorldVector( head, Vector3( 0.0f, 0.20f * radius, 0.85f * radius ) );
            const Vector3 forward =
                NormalizedOr( ModelToWorldVector( head, Vector3( 0.0f, 0.0f, 1.0f ) ), Vector3( 0.0f, 0.0f, 1.0f ) );
            const Vector3 up =
                NormalizedOr( ModelToWorldVector( head, Vector3( 0.0f, 1.0f, 0.0f ) ), Vector3( 0.0f, 1.0f, 0.0f ) );
            m_systems.cameras->CancelTween();
            m_systems.cameras->SetPrimaryPosition( eye );
            m_systems.cameras->SetViewCoordinates( eye + forward );
            m_systems.cameras->SetPrimaryUp( up );
            m_attachedCamera.lastLookDirection = forward;
            m_attachedCamera.hasLastLookDirection = true;
            return;
        }

        m_attachedCamera.submode = AttachedCameraSubmode::FixedRelative;
    }

    if ( !m_attachedCamera.hasOrbit )
    {
        CaptureAttachedCameraOrbit( target );
    }

    if ( m_camera.input.xMove != 0 || m_camera.input.yMove != 0 )
    {
        m_attachedCamera.orbitYawRadians =
            WrapAttachedCameraOrbitYaw( m_attachedCamera.orbitYawRadians +
                                        m_camera.input.xMove * CAMERA_MOUSE_REFERENCE_DT * Cfg().mouseSensitivity );
        m_attachedCamera.orbitPitchRadians =
            ClampAttachedCameraOrbitPitch( m_attachedCamera.orbitPitchRadians +
                                           m_camera.input.yMove * CAMERA_MOUSE_REFERENCE_DT * Cfg().mouseSensitivity );
    }

    m_attachedCamera.orbitDistance = ClampAttachedCameraOrbitDistance( target, m_attachedCamera.orbitDistance );

    const Vector3 targetPosition = target.GetPosition();
    const Vector3 eye = targetPosition + AttachedCameraOrbitOffset( m_attachedCamera.orbitYawRadians,
                                                                    m_attachedCamera.orbitPitchRadians,
                                                                    m_attachedCamera.orbitDistance );
    Vector3 view = targetPosition;
    Vector3 up = Vector3( 0.0f, 1.0f, 0.0f );
    if ( m_attachedCamera.submode == AttachedCameraSubmode::VelocityForward )
    {
        Vector3 direction = target.GetVelocity();
        if ( !TryNormalizeVector( direction ) )
        {
            direction = m_attachedCamera.hasLastLookDirection
                            ? m_attachedCamera.lastLookDirection
                            : m_systems.cameras->GetCameraView() - m_systems.cameras->GetCameraTranslation();
            if ( !TryNormalizeVector( direction ) )
            {
                direction = NormalizedOr( view - eye, Vector3( 0.0f, 0.0f, 1.0f ) );
            }
        }
        view = targetPosition +
               direction * (std::max)( AttachedCameraModelRadius( target ), m_attachedCamera.orbitDistance * 0.25f );
        m_attachedCamera.lastLookDirection = direction;
        m_attachedCamera.hasLastLookDirection = true;
    }

    if ( !IsFiniteVector( eye ) || !IsFiniteVector( view ) || !IsFiniteVector( up ) ||
         VectorMagSquared( view - eye ) <= TOLERANCE * TOLERANCE )
    {
        return;
    }

    m_systems.cameras->CancelTween();
    m_systems.cameras->SetPrimaryPosition( eye );
    m_systems.cameras->SetViewCoordinates( view );
    m_systems.cameras->SetPrimaryUp( up );
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
    const bool restoringFromAttach = previousMode == RunCameraMode::Attach &&
                                     mode == NormalizeCameraModeForCurrentScene( m_camera.modeBeforeAttach );
    if ( enteringAttach )
    {
        CaptureAttachedCameraReturnState( previousMode );
    }

    if ( mode == RunCameraMode::Demo )
    {
        const int modelCount = m_cGameModelCollection.GetModelCount();
        if ( m_camera.trackBallIndex < 0 || m_camera.trackBallIndex >= modelCount )
        {
            m_camera.trackBallIndex = 0;
        }
        if ( m_camera.trackHeight <= 0.0f )
        {
            m_camera.trackHeight = 300.0f;
        }
    }

    const RuntimeInteractionTransition transition = EnterInteractionForCameraMode( mode );
    ApplyRuntimeInteractionTransitionCleanup( transition );

    const bool wasFlyMode = IsFlyCameraMode();
    if ( mode != RunCameraMode::Launcher )
    {
        m_camera.modeBeforeLauncher = mode == RunCameraMode::Manipulator ? RunCameraMode::Inspect : mode;
    }
    SetCameraModeLabelAfterInteractionTransition( mode );
    if ( restoringFromAttach )
    {
        RestoreAttachedCameraReturnState();
    }
    if ( mode == RunCameraMode::Attach )
    {
        m_attachedCamera.activeFollow = true;
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
    if ( m_camera.mode == RunCameraMode::Attach )
    {
        const RunCameraMode restoreMode = NormalizeCameraModeForCurrentScene( m_camera.modeBeforeAttach );
        const int restoreIndex = static_cast<int>( restoreMode );
        if ( restoreMode != RunCameraMode::Attach && restoreIndex >= 0 &&
             restoreIndex < static_cast<int>( RunCameraMode::Count ) && ( enabledMask & ( 1u << restoreIndex ) ) != 0 )
        {
            ApplyCameraMode( restoreMode, RuntimeInputActionSource::Keyboard );
            return;
        }
    }

    int current = static_cast<int>( m_camera.mode );
    if ( current < 0 || current >= static_cast<int>( RunCameraMode::Count ) )
    {
        current = static_cast<int>( SceneState().isSceneMode ? RunCameraMode::Scene : RunCameraMode::Demo );
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
        m_systems.cameras->SelectCamera( CAMERA_FREE, false );
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


void Run::TakeInput()
{
    if ( !Input::IsAppFocused() )
    {
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
        RunInternal::ResetEditorUnfocusedInputState(
            { m_runtimeTools.Editor(), m_cGameModelCollection, m_interaction } );
        InputController::ResetUnfocusedInput( m_camera,
                                              m_inputLatches.leftSceneCycleWasDown,
                                              m_inputLatches.rightSceneCycleWasDown );
        m_runtimeInput.ResetEdges();
        InputController::BeginFrame(
            m_runtimeInput,
            BuildRuntimeInputModeState( m_camera.mode, m_runtimeTools.Editor(), m_attachedCamera.activeFollow ),
            false,
            true,
            true );
        m_UI.CancelInputCapture();
        RunUIStressActions();
        return;
    }

    ApplyCursorOwnership();

    const bool UIBlocksKeyboardBeforeInput = m_UI.BlocksKeyboard();
    InputController::BeginFrame(
        m_runtimeInput,
        BuildRuntimeInputModeState( m_camera.mode, m_runtimeTools.Editor(), m_attachedCamera.activeFollow ),
        true,
        UIBlocksKeyboardBeforeInput,
        m_UI.BlocksCameraMouse() );
    bool keyboardToggleEditorMode = false;
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
        keyboardToggleEditorMode =
            InputController::CaptureKeyboardActionPress( m_runtimeInput, RuntimeInputAction::ToggleEditor, VK_OEM_3 );

        if ( InputController::CaptureKeyboardActionPress( m_runtimeInput,
                                                          RuntimeInputAction::CycleCameraMode,
                                                          VK_TAB ) )
        {
            CycleCameraMode();
        }

        // F enters Inspect, or returns to the passive camera mode when already inspecting.
        if ( InputController::CaptureKeyboardActionPress( m_runtimeInput, RuntimeInputAction::ToggleFlyCamera, 'F' ) )
        {
            const RunCameraMode passiveMode = SceneState().isSceneMode ? RunCameraMode::Scene : RunCameraMode::Demo;
            ApplyCameraMode( m_camera.mode == RunCameraMode::Inspect ? passiveMode : RunCameraMode::Inspect,
                             RuntimeInputActionSource::Keyboard );
        }

        // N toggles launcher view with live simulation and returns to the previous non-launcher mode.
        {
            if ( InputController::CaptureKeyboardActionPress( m_runtimeInput,
                                                              RuntimeInputAction::ToggleLauncher,
                                                              'N' ) )
            {
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
        }

        {
            if ( InputController::CaptureKeyboardActionPress( m_runtimeInput,
                                                              RuntimeInputAction::CycleLauncherFireMode,
                                                              'M' ) &&
                 IsLauncherCameraMode() )
            {
                m_runtimeTools.RayCastTest().fireMode =
                    m_runtimeTools.RayCastTest().fireMode == RunLauncherFireMode::Laser
                        ? RunLauncherFireMode::Projectile
                        : RunLauncherFireMode::Laser;
            }
        }

        if ( InputController::CaptureKeyboardActionPress( m_runtimeInput,
                                                          RuntimeInputAction::CycleAttachedCameraSubmode,
                                                          VK_F1 ) &&
             IsAttachedCameraMode() )
        {
            CycleAttachedCameraSubmode();
        }

        if ( InputController::CaptureKeyboardActionPress( m_runtimeInput,
                                                          RuntimeInputAction::ToggleAttachedCameraPin,
                                                          VK_RETURN ) &&
             IsAttachedCameraMode() )
        {
            ToggleAttachedCameraPin();
        }

#ifdef _DEBUG
        {
            if ( InputController::CaptureKeyboardActionPress( m_runtimeInput,
                                                              RuntimeInputAction::WriteLauncherReproSnapshot,
                                                              VK_RETURN ) &&
                 IsLauncherCameraMode() && !m_replayRuntime.Scrubber().restoreConsumedThisFrame )
            {
                const double simulationSeconds = m_timers.simulationTimer.GetTimeSinceLastStart();
                const LauncherReproSnapshotStatus snapshotStatus =
                    m_runtimeTools.WriteLauncherReproSnapshot( { m_cGameModelCollection,
                                                                 m_systems.cameras,
                                                                 m_systems.terrain.get(),
                                                                 m_cWorldEnvironment,
                                                                 SceneState(),
                                                                 m_sceneController.CurrentPath(),
                                                                 m_launchOptions,
                                                                 m_runtimeSettings,
                                                                 m_debug,
                                                                 IsGfxReady() ? Gfx().GetRendererName() : "DirectX 12",
                                                                 simulationSeconds } );
                const char* snapshotMessage = "Failed to write repro snapshot";
                if ( snapshotStatus == LauncherReproSnapshotStatus::Wrote )
                {
                    sprintf_s( m_debug.reproSnapshotMessage,
                               sizeof( m_debug.reproSnapshotMessage ),
                               "Repro snapshot: %s",
                               LAUNCHER_REPRO_SNAPSHOT_PATH );
                }
                else if ( snapshotStatus == LauncherReproSnapshotStatus::NoTarget )
                {
                    snapshotMessage = "No repro target under crosshair";
                }
                if ( snapshotStatus != LauncherReproSnapshotStatus::Wrote )
                {
                    sprintf_s( m_debug.reproSnapshotMessage,
                               sizeof( m_debug.reproSnapshotMessage ),
                               "%s",
                               snapshotMessage );
                }
                m_debug.reproSnapshotMessageUntil = simulationSeconds + LAUNCHER_REPRO_MESSAGE_SECONDS;
            }
        }
#endif

        if ( m_runtimeTools.Editor().editorModeEnabled )
        {
            const RunInternal::EditorKeyboardShortcutResult editorShortcuts =
                RunInternal::HandleEditorKeyboardShortcuts( { m_runtimeInput } );
            m_replayRuntime.SetVelocityEditAltKeyDown( editorShortcuts.altDown );
            if ( editorShortcuts.togglePlacementMode )
            {
                applyEditorPlacementModeToggle( RuntimeInputActionSource::Keyboard );
            }
        }
        else
        {
            const bool altDown = Input::IsKeyDown( VK_MENU );
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

        // Water m_shader debug toggles
        if ( InputController::CaptureKeyboardActionPress( m_runtimeInput, RuntimeInputAction::ToggleWaterFreeze, '1' ) )
        {
            m_debug.isWaterFreezeDebug = !m_debug.isWaterFreezeDebug;
            if ( m_debug.isWaterFreezeDebug )
            {
                m_debug.frozenWaterTime = static_cast<float>( m_timers.simulationTimer.GetTimeSinceLastStart() );
            }
        }
        // Key '2' cycles water reflection modes in a predictable loop:
        // FBO mirror rendering, then DXR raytraced reflection when supported,
        // then no reflection, then back to FBO. Machines without DXR skip the
        // unsupported mode instead of leaving the toggle in a dead state.
        {
            if ( InputController::CaptureKeyboardActionPress( m_runtimeInput,
                                                              RuntimeInputAction::CycleWaterReflection,
                                                              '2' ) )
            {
                if ( !m_debug.isWaterRTReflect && !m_debug.isWaterNoReflect )
                {
                    if ( Gfx().GetCapabilities().supportsDxrReflection )
                    {
                        m_debug.isWaterRTReflect = true;
                    }
                    else
                    {
                        m_debug.isWaterNoReflect = true;
                    }
                }
                else if ( m_debug.isWaterRTReflect )
                {
                    m_debug.isWaterRTReflect = false;
                    m_debug.isWaterNoReflect = true;
                }
                else
                {
                    m_debug.isWaterNoReflect = false;
                }
            }
        }
        {
            if ( InputController::CaptureKeyboardActionPress( m_runtimeInput,
                                                              RuntimeInputAction::ToggleWaterFlat,
                                                              '3' ) )
            {
                m_debug.isWaterFlatDebug = !m_debug.isWaterFlatDebug;
            }
        }
        {
            if ( InputController::CaptureKeyboardActionPress( m_runtimeInput,
                                                              RuntimeInputAction::ToggleTerrainHidden,
                                                              '4' ) )
            {
                m_debug.isTerrainHidden = !m_debug.isTerrainHidden;
            }
        }
        {
            if ( InputController::CaptureKeyboardActionPress( m_runtimeInput,
                                                              RuntimeInputAction::ToggleWaterHidden,
                                                              '5' ) )
            {
                m_debug.isWaterHidden = !m_debug.isWaterHidden;
            }
        }
        // V key: collision visualizer for balls and boxes as solid debug colours.
        {
            if ( InputController::CaptureKeyboardActionPress( m_runtimeInput,
                                                              RuntimeInputAction::ToggleCollisionVisualizer,
                                                              'V' ) )
            {
                m_debug.isCollisionVisualizer = !m_debug.isCollisionVisualizer;
            }
        }

        // C key: cycle physics debug overlay - None -> Axes -> Contacts -> Sleep -> All -> None.
        {
            if ( InputController::CaptureKeyboardActionPress( m_runtimeInput,
                                                              RuntimeInputAction::CyclePhysicsDebugOverlay,
                                                              'C' ) )
            {
                switch ( m_debug.physicsDebugFlags )
                {
                case PHYSICS_DEBUG_NONE:
                    m_debug.physicsDebugFlags = PHYSICS_DEBUG_AXES;
                    break;
                case PHYSICS_DEBUG_AXES:
                    m_debug.physicsDebugFlags = PHYSICS_DEBUG_CONTACTS;
                    break;
                case PHYSICS_DEBUG_CONTACTS:
                    m_debug.physicsDebugFlags = PHYSICS_DEBUG_SLEEP;
                    break;
                case PHYSICS_DEBUG_SLEEP:
                    m_debug.physicsDebugFlags = PHYSICS_DEBUG_ALL;
                    break;
                default:
                    m_debug.physicsDebugFlags = PHYSICS_DEBUG_NONE;
                    break;
                }
            }
        }

        // O key: toggle the terrain polygon/contact probe. It is independent of
        // the C-key debug cycle so it can be layered over any other physics view.
        {
            if ( InputController::CaptureKeyboardActionPress( m_runtimeInput,
                                                              RuntimeInputAction::ToggleTerrainContactProbe,
                                                              'O' ) )
            {
                m_debug.physicsDebugFlags ^= PHYSICS_DEBUG_TERRAIN_CONTACT;
            }
        }

        // F7/F8: step the physics pipeline visualizer through the bounded Catto
        // stage trace from the most recent physics tick. The simulation can be
        // paused with fly mode and advanced separately with Space.
        {
            if ( InputController::CaptureKeyboardActionPress( m_runtimeInput,
                                                              RuntimeInputAction::StepPhysicsPipelinePrevious,
                                                              VK_F7 ) )
            {
                StepPhysicsPipelineStage( -1 );
            }
            if ( InputController::CaptureKeyboardActionPress( m_runtimeInput,
                                                              RuntimeInputAction::StepPhysicsPipelineNext,
                                                              VK_F8 ) )
            {
                StepPhysicsPipelineStage( 1 );
            }
        }

        // 6 key: translucent debug collision volumes for inspecting axes/contact rows inside bodies.
        {
            if ( InputController::CaptureKeyboardActionPress( m_runtimeInput,
                                                              RuntimeInputAction::TogglePhysicsDebugTransparent,
                                                              '6' ) )
            {
                m_debug.isPhysicsDebugTransparent = !m_debug.isPhysicsDebugTransparent;
            }
        }

        // Q key used to cycle legacy renderers; it now reports that DX12 is the only runtime renderer.
        {
            if ( InputController::CaptureKeyboardActionPress( m_runtimeInput,
                                                              RuntimeInputAction::ReportRendererRuntimeRetired,
                                                              'Q' ) )
            {
                fprintf( stderr, "Renderer switch ignored: DX12 is the only runtime renderer.\n" );
            }
        }

        // G key: toggle broadphase overlay, or cycle tracked ball if overlay is off.
        if ( InputController::CaptureKeyboardActionPress( m_runtimeInput,
                                                          RuntimeInputAction::ToggleBroadphaseOverlay,
                                                          'G' ) )
        {
            if ( SceneState().isSceneMode && m_camera.trackBallIndex >= 0 && !m_debug.isBroadphaseOverlay )
            {
                int count = m_cGameModelCollection.GetModelCount();
                if ( count > 0 )
                {
                    m_camera.trackBallIndex = ( m_camera.trackBallIndex + 1 ) % count;
                }
            }
            else
            {
                m_debug.isBroadphaseOverlay = !m_debug.isBroadphaseOverlay;
            }
        }

        // 0 key: toggle the in-game diagnostics window. Tabs replace the old overlay cycle.
        // Edge-detected in both scene and generated demo modes; one toggle per keypress.
        {
            if ( InputController::CaptureKeyboardActionPress( m_runtimeInput,
                                                              RuntimeInputAction::ToggleUIVisibility,
                                                              '0' ) )
            {
                EnterInteractiveSceneRun();
                m_UI.ToggleVisible( m_timers.simulationTimer.GetTotalTime() );
                m_debug.overlayMode = OverlayMode::None;
                ApplyCursorOwnership();
                ReleaseMouseToUI();
                UpdateRuntimeInputModeAfterAction( RuntimeInputAction::ToggleUIVisibility,
                                                   RuntimeInputActionSource::Keyboard );
            }
        }

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
                LoadScene( action.index,
                           action.preserveUIState,
                           action.suppressExitOnComplete,
                           action.preserveRuntimeState );
                return true;
            case SceneRuntimeControlActionType::ApplyCinematicModeFromBrowserIndex:
                EnterInteractiveSceneRun();
                return ApplyCinematicModeFromBrowserIndex( SceneRuntimeStyleContext{ m_launchOptions,
                                                                                     SceneState(),
                                                                                     m_sceneBrowser,
                                                                                     m_cGameModelCollection,
                                                                                     ActiveCinematicConfig(),
                                                                                     m_defaultCinematicRender },
                                                           action.index );
            case SceneRuntimeControlActionType::None:
                return false;
            }
            return false;
        };

        if ( InputController::CaptureKeyboardActionPress( m_runtimeInput,
                                                          RuntimeInputAction::NavigateScenePrevious,
                                                          VK_LEFT ) )
        {
            EnterInteractiveSceneRun();
            const int currentSceneBrowserIndex = CurrentSceneBrowserIndex( m_sceneController, m_sceneBrowser );
            const bool isCinematicTabActive = m_UI.GetActiveTab() == InGameUITab::Cinematic;
            if ( !executeSceneControlAction(
                     m_sceneCoordinator.ApplyAdjacentCinematicMode( -1,
                                                                    m_sceneBrowser.paths,
                                                                    m_sceneBrowser.selectedCineModeSceneIndex,
                                                                    currentSceneBrowserIndex,
                                                                    isCinematicTabActive ) ) )
            {
                executeSceneControlAction(
                    m_sceneCoordinator.LoadAdjacentSceneFromBrowser( -1,
                                                                     m_sceneBrowser.paths,
                                                                     currentSceneBrowserIndex ) );
            }
        }
        if ( InputController::CaptureKeyboardActionPress( m_runtimeInput,
                                                          RuntimeInputAction::NavigateSceneNext,
                                                          VK_RIGHT ) )
        {
            EnterInteractiveSceneRun();
            const int currentSceneBrowserIndex = CurrentSceneBrowserIndex( m_sceneController, m_sceneBrowser );
            const bool isCinematicTabActive = m_UI.GetActiveTab() == InGameUITab::Cinematic;
            if ( !executeSceneControlAction(
                     m_sceneCoordinator.ApplyAdjacentCinematicMode( 1,
                                                                    m_sceneBrowser.paths,
                                                                    m_sceneBrowser.selectedCineModeSceneIndex,
                                                                    currentSceneBrowserIndex,
                                                                    isCinematicTabActive ) ) )
            {
                executeSceneControlAction(
                    m_sceneCoordinator.LoadAdjacentSceneFromBrowser( 1,
                                                                     m_sceneBrowser.paths,
                                                                     currentSceneBrowserIndex ) );
            }
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
        const int selectedSceneBrowserIndex = CurrentSceneBrowserIndex( m_sceneController, m_sceneBrowser );
        InGameUIInputResult UIResult =
            m_UI.UpdateInput( m_systems.window->m_sWindow,
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
                              m_sceneBrowser.namePtrs.empty() ? nullptr : m_sceneBrowser.namePtrs.data(),
                              static_cast<int>( m_sceneBrowser.namePtrs.size() ),
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

        // ESC flicks the diagnostics window between minimized and expanded, with
        // a very fast double-tap escape hatch for quitting interactive runs.
        // Run it after UI input processing so focused controls keep their local ESC
        // behavior first, such as closing the scene filter combo without also
        // hiding the whole diagnostics surface on the same frame.
        if ( InputController::CaptureKeyboardActionPress( m_runtimeInput,
                                                          RuntimeInputAction::DismissOrExitUI,
                                                          VK_ESCAPE ) &&
             !uiCommands.ui.userInteracted )
        {
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

        if ( uiCommands.renderer.toggleVsync )
        {
            m_runtimeSettings.isVsyncEnabled = !m_runtimeSettings.isVsyncEnabled;
            Gfx().SetVsyncEnabled( m_runtimeSettings.isVsyncEnabled );
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
            StepPhysicsPipelineStage( -1 );
            UpdateRuntimeInputModeAfterAction( RuntimeInputAction::StepPhysicsPipelinePrevious,
                                               RuntimeInputActionSource::UI );
        }
        if ( uiCommands.physics.stepPhysicsPipelineNext )
        {
            StepPhysicsPipelineStage( 1 );
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
        bool tornadoFieldChanged = false;
        const bool hasTornadoSystem = !m_runtimeSettings.tornadoSystem.vortices.empty();
        const auto applyTornadoFieldValue = [&]( float TornadoFieldConfig::* field, float value )
        {
            if ( hasTornadoSystem )
            {
                for ( TornadoVortexConfig& vortex : m_runtimeSettings.tornadoSystem.vortices )
                {
                    vortex.field.*field = value;
                }
            }
            else
            {
                m_runtimeSettings.tornadoField.*field = value;
            }
        };
        if ( uiCommands.physics.toggleTornado )
        {
            bool tornadoEnabled = false;
            if ( hasTornadoSystem )
            {
                m_runtimeSettings.tornadoSystem.enabled = !m_runtimeSettings.tornadoSystem.enabled;
                tornadoEnabled = m_runtimeSettings.tornadoSystem.enabled;
            }
            else
            {
                m_runtimeSettings.tornadoField.enabled = !m_runtimeSettings.tornadoField.enabled;
                tornadoEnabled = m_runtimeSettings.tornadoField.enabled;
            }
            if ( m_runtimeSettings.tornadoVisual.autoEnableWithTornado )
            {
                m_runtimeSettings.tornadoVisual.enabled = tornadoEnabled;
            }
            tornadoFieldChanged = true;
            UpdateRuntimeInputModeAfterAction( RuntimeInputAction::ToggleTornado, RuntimeInputActionSource::UI );
        }
        if ( uiCommands.physics.toggleTornadoVisualShell )
        {
            m_runtimeSettings.tornadoVisual.enabled = !m_runtimeSettings.tornadoVisual.enabled;
            UpdateRuntimeInputModeAfterAction( RuntimeInputAction::ToggleTornadoVisualShell,
                                               RuntimeInputActionSource::UI );
        }
        if ( uiCommands.physics.toggleTornadoFieldVectors )
        {
            if ( hasTornadoSystem )
            {
                m_runtimeSettings.tornadoSystem.visualizeVelocityField =
                    !m_runtimeSettings.tornadoSystem.visualizeVelocityField;
            }
            else
            {
                m_runtimeSettings.tornadoField.visualizeVelocityField =
                    !m_runtimeSettings.tornadoField.visualizeVelocityField;
            }
            tornadoFieldChanged = true;
            UpdateRuntimeInputModeAfterAction( RuntimeInputAction::ToggleTornadoFieldVectors,
                                               RuntimeInputActionSource::UI );
        }
        if ( uiCommands.physics.toggleRayCastVisualization )
        {
            m_runtimeTools.RayCastTest().visualizeRays = !m_runtimeTools.RayCastTest().visualizeRays;
            UpdateRuntimeInputModeAfterAction( RuntimeInputAction::ToggleRayCastVisualization,
                                               RuntimeInputActionSource::UI );
        }
        if ( uiCommands.physics.requestTornadoRadius )
        {
            applyTornadoFieldValue(
                &TornadoFieldConfig::radius,
                std::clamp( uiCommands.physics.requestedTornadoRadius, UI_TORNADO_RADIUS_MIN, UI_TORNADO_RADIUS_MAX ) );
            tornadoFieldChanged = true;
            UpdateRuntimeInputModeAfterAction( RuntimeInputAction::ApplyTornadoSettings, RuntimeInputActionSource::UI );
        }
        if ( uiCommands.physics.requestTornadoHeight )
        {
            applyTornadoFieldValue(
                &TornadoFieldConfig::height,
                std::clamp( uiCommands.physics.requestedTornadoHeight, UI_TORNADO_HEIGHT_MIN, UI_TORNADO_HEIGHT_MAX ) );
            tornadoFieldChanged = true;
            UpdateRuntimeInputModeAfterAction( RuntimeInputAction::ApplyTornadoSettings, RuntimeInputActionSource::UI );
        }
        if ( uiCommands.physics.requestTornadoInward )
        {
            applyTornadoFieldValue(
                &TornadoFieldConfig::inwardAcceleration,
                std::clamp( uiCommands.physics.requestedTornadoInward, UI_TORNADO_INWARD_MIN, UI_TORNADO_INWARD_MAX ) );
            tornadoFieldChanged = true;
            UpdateRuntimeInputModeAfterAction( RuntimeInputAction::ApplyTornadoSettings, RuntimeInputActionSource::UI );
        }
        if ( uiCommands.physics.requestTornadoSwirl )
        {
            applyTornadoFieldValue(
                &TornadoFieldConfig::swirlAcceleration,
                std::clamp( uiCommands.physics.requestedTornadoSwirl, UI_TORNADO_SWIRL_MIN, UI_TORNADO_SWIRL_MAX ) );
            tornadoFieldChanged = true;
            UpdateRuntimeInputModeAfterAction( RuntimeInputAction::ApplyTornadoSettings, RuntimeInputActionSource::UI );
        }
        if ( uiCommands.physics.requestTornadoLift )
        {
            applyTornadoFieldValue(
                &TornadoFieldConfig::liftAcceleration,
                std::clamp( uiCommands.physics.requestedTornadoLift, UI_TORNADO_LIFT_MIN, UI_TORNADO_LIFT_MAX ) );
            tornadoFieldChanged = true;
            UpdateRuntimeInputModeAfterAction( RuntimeInputAction::ApplyTornadoSettings, RuntimeInputActionSource::UI );
        }
        if ( tornadoFieldChanged )
        {
            SyncTornadoRuntimeSettingsToPhysics( m_cGameModelCollection, m_runtimeSettings );
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
            if ( IsCinematicRenderingEnabled() )
            {
                const bool shadowsActive = ActiveCinematicConfig().shadowsEnabled;
                m_launchOptions.hasCinematicShadowsOverride = false;
                SetCinematicShadowsEnabledFromUI( ActiveCinematicConfig(), SceneState(), !shadowsActive );
            }
            else
            {
                Cfg().ordinaryRender.shadowsEnabled = !Cfg().ordinaryRender.shadowsEnabled;
            }
            UpdateRuntimeInputModeAfterAction( RuntimeInputAction::ToggleShadows, RuntimeInputActionSource::UI );
        }
        if ( uiCommands.renderTuning.toggleShadows )
        {
            Cfg().ordinaryRender.shadowsEnabled = !Cfg().ordinaryRender.shadowsEnabled;
            UpdateRuntimeInputModeAfterAction( RuntimeInputAction::ToggleRenderShadows, RuntimeInputActionSource::UI );
        }
        if ( uiCommands.renderTuning.saveDefaults )
        {
            m_runtimeCommands.Push( RuntimeCommand{ RuntimeCommandType::SaveRenderDefaults } );
            UpdateRuntimeInputModeAfterAction( RuntimeInputAction::SaveRenderDefaults, RuntimeInputActionSource::UI );
        }
        if ( uiCommands.renderTuning.requestedParam != UIRenderParam::None )
        {
            ApplyOrdinaryRenderUIParam( Cfg().ordinaryRender,
                                        uiCommands.renderTuning.requestedParam,
                                        uiCommands.renderTuning.requestedValue );
            UpdateRuntimeInputModeAfterAction( RuntimeInputAction::ApplyRenderTuning, RuntimeInputActionSource::UI );
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
            m_sceneUIOverrides.timeScaleOverride =
                std::clamp( uiCommands.sceneOptions.requestedTimeScale, 0.10f, 10.00f );
            SceneState().timeScale = m_sceneUIOverrides.timeScaleOverride;
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
        const auto makeSceneGeneratedControlContext = [this]() -> SceneRuntimeGeneratedControlContext
        {
            return SceneRuntimeGeneratedControlContext{ SceneState(),
                                                        m_sceneUIOverrides,
                                                        m_camera,
                                                        m_sceneController,
                                                        Cfg(),
                                                        m_cWorldEnvironment,
                                                        m_systems.terrain.get(),
                                                        m_cGameModelCollection,
                                                        m_simulation,
                                                        m_runtimeTools,
                                                        IsGfxReady() ? &Gfx() : nullptr,
                                                        m_launchOptions.generatedObjectTypeOverride,
                                                        ActiveGameModelCapacity() };
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
            ApplyWorkerThreadCountOverride( uiCommands.profiler.requestedWorkerThreads );
            UpdateRuntimeInputModeAfterAction( RuntimeInputAction::SetWorkerThreads, RuntimeInputActionSource::UI );
        }
        if ( uiCommands.run.requestedSolverBallCount >= 0 )
        {
            const int modelCapacity = ActiveGameModelCapacity();
            const int boxes = m_sceneUIOverrides.solverBoxCountOverride >= 0 ? m_sceneUIOverrides.solverBoxCountOverride
                                                                             : SceneState().solverBoxCount;
            executeSceneGeneratedControlAction( ApplyUISolverObjectCounts(
                makeSceneGeneratedControlContext(),
                std::clamp( uiCommands.run.requestedSolverBallCount, 0, (std::max)( 0, modelCapacity - boxes ) ),
                boxes ) );
            UpdateRuntimeInputModeAfterAction( RuntimeInputAction::SetSolverCounts, RuntimeInputActionSource::UI );
        }
        if ( uiCommands.run.requestedSolverBoxCount >= 0 )
        {
            const int modelCapacity = ActiveGameModelCapacity();
            const int balls = m_sceneUIOverrides.solverBallCountOverride >= 0
                                  ? m_sceneUIOverrides.solverBallCountOverride
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
            CinematicRenderConfig& cinematic = ActiveCinematicConfig();
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
            ApplyCinematicModeFromBrowserIndex( SceneRuntimeStyleContext{ m_launchOptions,
                                                                          SceneState(),
                                                                          m_sceneBrowser,
                                                                          m_cGameModelCollection,
                                                                          ActiveCinematicConfig(),
                                                                          m_defaultCinematicRender },
                                                uiCommands.cinematic.requestedModeSceneIndex );
            UpdateRuntimeInputModeAfterAction( RuntimeInputAction::SelectCinematicScene, RuntimeInputActionSource::UI );
        }
        if ( uiCommands.cinematic.requestedFeature != UICinematicFeature::None )
        {
            CinematicRenderConfig& cinematic = ActiveCinematicConfig();
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
            CinematicRenderConfig& cinematic = ActiveCinematicConfig();
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

    HandleEditorSaveHotkeys( { m_runtimeInput,
                               m_cGameModelCollection,
                               SceneState(),
                               m_cWorldEnvironment,
                               *m_systems.cameras,
                               m_runtimeCommands } );

    // R: reset/reload the current scene from scratch. Backspace remains as a scene-mode alias.
    {
        if ( InputController::CaptureKeyboardActionPress( m_runtimeInput, RuntimeInputAction::ResetScene, 'R' ) )
        {
            m_runtimeCommands.Push( RuntimeCommand{ RuntimeCommandType::ResetCurrentScene } );
        }
    }
    if ( SceneState().isSceneMode )
    {
        if ( InputController::CaptureKeyboardActionPress( m_runtimeInput,
                                                          RuntimeInputAction::ResetSceneFromBackspace,
                                                          VK_BACK ) )
        {
            m_runtimeCommands.Push( RuntimeCommand{ RuntimeCommandType::ResetCurrentScene } );
        }
    }

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
            LoadScene( action.index,
                       action.preserveUIState,
                       action.suppressExitOnComplete,
                       action.preserveRuntimeState );
            return true;
        case SceneRuntimeControlActionType::ApplyCinematicModeFromBrowserIndex:
            EnterInteractiveSceneRun();
            return ApplyCinematicModeFromBrowserIndex( SceneRuntimeStyleContext{ m_launchOptions,
                                                                                 SceneState(),
                                                                                 m_sceneBrowser,
                                                                                 m_cGameModelCollection,
                                                                                 ActiveCinematicConfig(),
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
                m_sceneCoordinator.LoadSceneFromBrowserIndex( command.index, m_sceneBrowser.paths ) );
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
                CreateSceneFromUI( SceneRuntimeCreateContext{ m_sceneController, m_sceneBrowser },
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
            SaveRenderDefaults( Cfg().ordinaryRender );
            break;
        case RuntimeCommandType::SaveSkyDefaults:
            SaveSkyDefaults( ActiveCinematicConfig() );
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
            Cfg().minCameraHeight;
        if ( minY > translatedCameraPosition.y )
        {
            m_systems.cameras->AmmendPrimaryY( minY );
        }
        else if ( translatedCameraPosition.y > Cfg().maxCameraHeight )
        {
            m_systems.cameras->AmmendPrimaryY( Cfg().maxCameraHeight );
        }
    }
}
