/*
File: SkullbonezSource/Runtime/RunInput.cpp
Purpose:
  Routes raw keyboard, mouse, and UI commands into runtime state changes.

Mental model:
  RunInput.cpp routes raw keyboard, mouse, and UI commands into concrete owners.
  Scene requests are submitted and executed by SceneController; this file only
  wires its cold dependencies at the post-input checkpoint.

Glossary:
  Attach return pose: The visible camera pose captured before Attach takes over
    so the operator can return to the same view later.
  Contact-audio flash command: One-frame UI request that cycles a render-only
    diagnostic selector; it does not change audio classification policy.
  Contact-audio simple command: One-frame UI request that switches audio to the
    body-linear-energy path instead of the solver contact-row classifier.
  Attached-camera physics target: Body/collider handles plus a store-owned pose,
    velocity, and broad radius sampled for camera follow math.
  Lane R result: Recoverable scene-control or capture failure reported without
    treating the command as successfully applied.

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
#include "InputController.Bindings.h"
#include "InputController.h"
#include "Replay/ReplayOverlayLayout.h"
#include "Replay/ReplayRestoreService.h"
#include "Replay/ReplayRuntimeOwnerViews.h"
#include "RunDemoDirector.h"
#include "RuntimeInteractionCommands.h"
#include "RuntimePickService.h"
#include "Scene/SceneRuntimeCreate.h"
#include "RuntimeTuning.h"
#include "Scene/SceneRuntimeGeneratedControls.h"
#include "Scene/SceneRuntimeLoad.h"
#include "Scene/SceneRuntimeStyle.h"
#include "../Core/Log.h"
#include "../Physics/ColliderStore.h"
#include "../Physics/PhysicsBodyStore.h"
#include "../UI/UILayout.h"

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>

using namespace SkullbonezCore::Basics;
using namespace SkullbonezCore::Math::CollisionDetection;
using namespace SkullbonezCore::Math::Orientation;
using namespace SkullbonezCore::Math::Transformation;
using namespace SkullbonezCore::Physics;
using namespace SkullbonezCore::UI::Layout;
using namespace SkullbonezCore::Basics::RunInternal;

namespace
{
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

void ReportRuntimeInputFailure( const SbResult& result )
{
    if ( result.ok )
    {
        return;
    }

    std::fprintf( stderr,
                  "%s: %s\n",
                  result.error.owner[0] != '\0' ? result.error.owner : "Runtime/Input",
                  result.error.message[0] != '\0' ? result.error.message : "recoverable input operation failed" );
}

AttachedCameraPose AttachedCameraPoseFromCameras( SkullbonezCore::Environment::CameraCollection& cameras )
{
    AttachedCameraPose pose;
    pose.eye = cameras.GetCameraTranslation();
    pose.view = cameras.GetCameraView();
    pose.up = cameras.GetCameraUp();
    return pose;
}

bool CaptureAttachedCameraFixedOffsetFromCurrentPose( AttachedCameraState& state,
                                                      SkullbonezCore::Environment::CameraCollection* cameras,
                                                      const AttachedCameraPhysicsTarget& target )
{
    if ( !cameras )
    {
        return false;
    }

    AttachedCameraController::CaptureFixedOffset( state, AttachedCameraPoseFromCameras( *cameras ), target );
    return true;
}

bool CaptureAttachedCameraOrbitFromCurrentPose( AttachedCameraState& state,
                                                SkullbonezCore::Environment::CameraCollection* cameras,
                                                const AttachedCameraPhysicsTarget& target )
{
    if ( !cameras )
    {
        return false;
    }

    AttachedCameraController::CaptureOrbit( state, AttachedCameraPoseFromCameras( *cameras ), target );
    return true;
}


RuntimeInputModeState BuildRuntimeInputModeState( RunCameraMode mode,
                                                  const RunEditorPlacementState& editor,
                                                  bool attachActiveFollow,
                                                  bool directorGrabbed )
{
    RuntimeInputModeState state;
    state.flyCamera = RunCameraModeUsesFlyControls( mode, attachActiveFollow, directorGrabbed );
    state.launcher = RunCameraModeUsesLauncher( mode );
    state.manipulator = RunCameraModeIsManipulator( mode );
    state.editor = editor.editorModeEnabled;
    state.editorPlacement = editor.placementModeEnabled;
    state.editorViewportLook = editor.viewportLookActive;
    state.editorPlacementScale = editor.placementScaleActive;
    state.editorGizmoDrag = editor.gizmoDragActive;
    state.editorGizmoRotation = editor.gizmoDragIsRotation;
    state.editorGizmoScale = editor.gizmoDragIsScale;
    return state;
}


PointerPresentationPolicy EvaluateRuntimePointerPresentation( const InputRouter& inputRouter,
                                                              const RunEditorPlacementState& editor,
                                                              const ReplayRuntime& replayRuntime )
{
    const DeviceInputFrame& deviceFrame = inputRouter.DeviceFrame();
    const UiInputHitSnapshot& uiSnapshot = inputRouter.UiSnapshot();
    PointerPresentationPolicyInput input;
    input.editorModeEnabled = editor.editorModeEnabled;
    input.editorViewportLookActive = editor.viewportLookActive;
    input.editorPlacementModeEnabled = editor.placementModeEnabled;
    input.editorPlacementPreviewVisible = editor.placementPreviewVisible;
    input.replayInspectionActive = replayRuntime.InspectionActive();
    input.replayInspectionLookActive =
        input.replayInspectionActive && replayRuntime.InspectionMouseLookActive( deviceFrame.rightDown,
                                                                                 uiSnapshot.wantsNativeCursor,
                                                                                 uiSnapshot.blocksCameraMouse );
    return inputRouter.EvaluatePointerPresentation( input );
}


bool RuntimeMouseLookOwnsCursor( const InputRouter& inputRouter,
                                 const RunEditorPlacementState& editor,
                                 const ReplayRuntime& replayRuntime )
{
    return EvaluateRuntimePointerPresentation( inputRouter, editor, replayRuntime ).mouseLookOwnsCursor;
}


bool RuntimeShouldHideNativeCursor( const InputRouter& inputRouter,
                                    const RunEditorPlacementState& editor,
                                    const ReplayRuntime& replayRuntime )
{
    return EvaluateRuntimePointerPresentation( inputRouter, editor, replayRuntime ).hideNativeCursor;
}


void ApplyRuntimeCursorOwnership( InputRouter& inputRouter,
                                  const RunEditorPlacementState& editor,
                                  const ReplayRuntime& replayRuntime )
{
    inputRouter.RequestCursorVisible( !RuntimeShouldHideNativeCursor( inputRouter, editor, replayRuntime ) );
}


void ReleaseRuntimeMouseToUI( InputRouter& inputRouter,
                              const RunEditorPlacementState& editor,
                              const ReplayRuntime& replayRuntime,
                              RunCameraState& camera )
{
    if ( !RuntimeMouseLookOwnsCursor( inputRouter, editor, replayRuntime ) )
    {
        inputRouter.ReleaseNativeCapture();
        InputController::ResetMouseLook( camera );
    }
}

// Concept: binding predicates read one immutable pre-UI fact set. A command
// earlier in binding order cannot silently activate a sibling command's mode
// context during the same phase; that new context begins on the next frame.
struct KeyboardContextFacts
{
    bool keyboardUnblocked = false;
    bool scene = false;
    bool flyCamera = false;
    bool launcher = false;
    bool attachedCamera = false;
    bool director = false;
    bool directorAuthoring = false;
    bool replayRestoreNotConsumed = false;
    bool uiNotInteracted = false;
};

RuntimeInputContextMask BuildKeyboardContextMask( const KeyboardContextFacts& facts )
{
    RuntimeInputContextMask mask = 0;
    auto include = [&mask]( RuntimeInputBindingContext context, bool enabled )
    {
        if ( enabled )
        {
            mask |= RuntimeInputContextBit( context );
        }
    };
    include( RuntimeInputBindingContext::KeyboardUnblocked, facts.keyboardUnblocked );
    include( RuntimeInputBindingContext::Scene, facts.scene );
    include( RuntimeInputBindingContext::GeneratedDemo, !facts.scene );
    include( RuntimeInputBindingContext::FlyCamera, facts.flyCamera );
    include( RuntimeInputBindingContext::Launcher, facts.launcher );
    include( RuntimeInputBindingContext::AttachedCamera, facts.attachedCamera );
    include( RuntimeInputBindingContext::Director, facts.director );
    include( RuntimeInputBindingContext::DirectorAuthoring, facts.directorAuthoring );
    include( RuntimeInputBindingContext::ReplayRestoreNotConsumed, facts.replayRestoreNotConsumed );
    include( RuntimeInputBindingContext::UINotInteracted, facts.uiNotInteracted );
#ifdef _DEBUG
    include( RuntimeInputBindingContext::DebugOnly, true );
#endif
    return mask;
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

const char* ReplayOwnerEventName( ReplayOwnerEventCode code )
{
    switch ( code )
    {
    case ReplayOwnerEventCode::SceneLoadBrowserIndex:
        return "SceneLoadBrowserIndex";
    case ReplayOwnerEventCode::SceneLoadDemo:
        return "SceneLoadDemo";
    case ReplayOwnerEventCode::SceneReset:
        return "SceneReset";
    case ReplayOwnerEventCode::SceneCreate:
        return "SceneCreate";
    case ReplayOwnerEventCode::SceneSaveDefaults:
        return "SceneSaveDefaults";
    case ReplayOwnerEventCode::CaptureScreenshot:
        return "CaptureScreenshot";
    case ReplayOwnerEventCode::RenderSaveOrdinaryDefaults:
        return "RenderSaveOrdinaryDefaults";
    case ReplayOwnerEventCode::RenderSaveCinematicDefaults:
        return "RenderSaveCinematicDefaults";
    default:
        return "UnknownOwnerEvent";
    }
}

uint32_t ReplaySceneRequestFlags( const SceneRequest& request )
{
    uint32_t flags = 0;
    flags |= request.preserveUIState ? 1u : 0u;
    flags |= request.suppressExitOnComplete ? 2u : 0u;
    flags |= request.preserveRuntimeState ? 4u : 0u;
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

// Concept: UI command domains return accepted-command facts. These mappers keep
// RuntimeInput transition recording in the original order without forcing each
// domain helper to know about Run's input-mode history.
template <typename RecordAction>
void RecordDiagnosticsPhysicsOverlayUIActions( const DiagnosticsPhysicsOverlayUICommandResult& commands,
                                               RecordAction recordAction )
{
    if ( commands.toggledPhysicsDebugFlags )
    {
        recordAction( RuntimeInputAction::TogglePhysicsDebugFlags );
    }
    if ( commands.steppedPipelinePrevious )
    {
        recordAction( RuntimeInputAction::StepPhysicsPipelinePrevious );
    }
    if ( commands.steppedPipelineNext )
    {
        recordAction( RuntimeInputAction::StepPhysicsPipelineNext );
    }
    if ( commands.toggledPhysicsDebugTransparent )
    {
        recordAction( RuntimeInputAction::TogglePhysicsDebugTransparent );
    }
    if ( commands.toggledBroadphaseOverlay )
    {
        recordAction( RuntimeInputAction::ToggleBroadphaseOverlay );
    }
}

template <typename RecordAction>
void RecordTornadoToggleUIActions( const TornadoUICommandResult& commands, RecordAction recordAction )
{
    if ( commands.toggledTornado )
    {
        recordAction( RuntimeInputAction::ToggleTornado );
    }
    if ( commands.toggledVisualShell )
    {
        recordAction( RuntimeInputAction::ToggleTornadoVisualShell );
    }
    if ( commands.toggledFieldVectors )
    {
        recordAction( RuntimeInputAction::ToggleTornadoFieldVectors );
    }
}

template <typename RecordAction>
void RecordTornadoApplySettingsUIActions( const TornadoUICommandResult& commands, RecordAction recordAction )
{
    for ( int actionIndex = 0; actionIndex < commands.applySettingsActionCount; ++actionIndex )
    {
        recordAction( RuntimeInputAction::ApplyTornadoSettings );
    }
}

template <typename RecordAction>
void RecordRuntimePresentationUIActions( const RuntimePresentationUICommandResult& commands, RecordAction recordAction )
{
    if ( commands.toggledTerrainHidden )
    {
        recordAction( RuntimeInputAction::ToggleTerrainHidden );
    }
    if ( commands.toggledWaterHidden )
    {
        recordAction( RuntimeInputAction::ToggleWaterHidden );
    }
    if ( commands.toggledWaterFreeze )
    {
        recordAction( RuntimeInputAction::ToggleWaterFreeze );
    }
    if ( commands.toggledWaterFlat )
    {
        recordAction( RuntimeInputAction::ToggleWaterFlat );
    }
    if ( commands.toggledSceneShadows )
    {
        recordAction( RuntimeInputAction::ToggleShadows );
    }
    if ( commands.toggledRenderShadows )
    {
        recordAction( RuntimeInputAction::ToggleRenderShadows );
    }
    if ( commands.queuedRenderDefaultsSave )
    {
        recordAction( RuntimeInputAction::SaveRenderDefaults );
    }
    if ( commands.appliedRenderTuning )
    {
        recordAction( RuntimeInputAction::ApplyRenderTuning );
    }
}

template <typename RecordAction>
void RecordRuntimePresentationWaterUIActions( const RuntimePresentationUICommandResult& commands,
                                              RecordAction recordAction )
{
    if ( commands.toggledWaterReflection )
    {
        recordAction( RuntimeInputAction::ToggleWaterReflection );
    }
    if ( commands.setWaterReflectionMode )
    {
        recordAction( RuntimeInputAction::SetWaterReflectionMode );
    }
}

template <typename RecordAction>
void RecordRunSimulationUIActions( const RunSimulationUICommandResult& commands, RecordAction recordAction )
{
    if ( commands.setTimeScale )
    {
        recordAction( RuntimeInputAction::SetTimeScale );
    }
    if ( commands.setRunSeed )
    {
        recordAction( RuntimeInputAction::SetRunSeed );
    }
}

template <typename RecordAction>
void RecordDiagnosticsPhysicsDebugValueUIActions( const DiagnosticsPhysicsDebugValueUICommandResult& commands,
                                                  RecordAction recordAction )
{
    if ( commands.setAlpha )
    {
        recordAction( RuntimeInputAction::SetPhysicsDebugAlpha );
    }
    if ( commands.setContactLinger )
    {
        recordAction( RuntimeInputAction::SetPhysicsDebugContactLinger );
    }
}

template <typename RecordAction>
void RecordPhysicsFrictionUIActions( const PhysicsFrictionUICommandResult& commands, RecordAction recordAction )
{
    for ( int actionIndex = 0; actionIndex < commands.applySettingsActionCount; ++actionIndex )
    {
        recordAction( RuntimeInputAction::ApplyPhysicsFrictionSettings );
    }
}

template <typename RecordAction>
void RecordCinematicTuningUIActions( const CinematicTuningUICommandResult& commands, RecordAction recordAction )
{
    if ( commands.toggledFeature )
    {
        recordAction( RuntimeInputAction::ToggleCinematicFeature );
    }
    if ( commands.appliedParam )
    {
        recordAction( RuntimeInputAction::ApplyCinematicParam );
    }
}

template <typename RecordAction>
void RecordSceneRuntimeUIActions( const SceneRuntimeUICommandResult& commands, RecordAction recordAction )
{
    if ( commands.resetScene )
    {
        recordAction( RuntimeInputAction::ResetScene );
    }
    if ( commands.resetSceneDefaults )
    {
        recordAction( RuntimeInputAction::ResetSceneDefaults );
    }
    if ( commands.loadDemoScene )
    {
        recordAction( RuntimeInputAction::LoadDemoScene );
    }
    if ( commands.saveSceneDefaults )
    {
        recordAction( RuntimeInputAction::SaveSceneDefaults );
    }
    if ( commands.createScene )
    {
        recordAction( RuntimeInputAction::CreateScene );
    }
    if ( commands.selectScene )
    {
        recordAction( RuntimeInputAction::SelectScene );
    }
}

struct PostMappedKeyboardShortcutContext
{
    // Lifetime: borrowed for one post-keyboard dispatch pass. The helper does
    // not store owner references or callbacks past the current frame.
    RuntimeTools& runtimeTools;
    ReplayRuntime& replayRuntime;
    RuntimeInteractionController& interaction;
    RunTimerState& timers;
};

template <typename ApplyEditorPlacementModeToggle,
          typename CancelReplayToolDragState,
          typename EnterInteractiveSceneRun,
          typename EnterReplayInspectionCamera,
          typename ExitReplayInspectionCamera,
          typename SetWorldInteractionOwner>
void ApplyPostMappedKeyboardShortcutState( PostMappedKeyboardShortcutContext context,
                                           const RunInternal::EditorKeyboardShortcutResult& shortcut,
                                           ApplyEditorPlacementModeToggle applyEditorPlacementModeToggle,
                                           CancelReplayToolDragState cancelReplayToolDragState,
                                           EnterInteractiveSceneRun enterInteractiveSceneRun,
                                           EnterReplayInspectionCamera enterReplayInspectionCamera,
                                           ExitReplayInspectionCamera exitReplayInspectionCamera,
                                           SetWorldInteractionOwner setWorldInteractionOwner )
{
    if ( context.runtimeTools.Editor().editorModeEnabled )
    {
        context.replayRuntime.SetVelocityEditAltKeyDown( shortcut.altDown );
        if ( shortcut.togglePlacementMode )
        {
            applyEditorPlacementModeToggle( RuntimeInputActionSource::Keyboard );
        }
        return;
    }

    const bool altDown = shortcut.altDown;
    if ( altDown && !context.replayRuntime.VelocityEdit().keyboardAltWasDown )
    {
        const bool enableVelocityEdit = !context.replayRuntime.VelocityEdit().enabled;
        if ( context.replayRuntime.SetVelocityEditEnabled( enableVelocityEdit ) )
        {
            cancelReplayToolDragState();
            if ( enableVelocityEdit )
            {
                enterInteractiveSceneRun();
                if ( context.replayRuntime.SetLiveAdvanceHeld( true ) )
                {
                    if ( context.replayRuntime.ShouldUseInspectionCamera() )
                    {
                        enterReplayInspectionCamera();
                    }
                    else
                    {
                        exitReplayInspectionCamera();
                    }
                }
                setWorldInteractionOwner( WorldInteractionOwner::ReplayVelocityEdit,
                                          InteractionExitReason::EnterReplay );
            }
            else if ( context.interaction.Owner() == WorldInteractionOwner::ReplayVelocityEdit )
            {
                setWorldInteractionOwner( WorldInteractionOwner::ReplayScrub, InteractionExitReason::EnterReplay );
            }
        }
        context.replayRuntime.Scrubber().visibleUntil =
            context.timers.simulationTimer.GetTotalTime() + ReplayOverlay::REPLAY_SCRUBBER_VISIBLE_SECONDS;
        context.replayRuntime.Scrubber().visible = true;
    }
    context.replayRuntime.VelocityEdit().keyboardAltWasDown = altDown;
}

struct RuntimeUIFrameContext
{
    // Lifetime: borrowed for one UI command frame. The helper applies decoded
    // UI intents immediately and returns only the world-input arbitration facts.
    RuntimeInputContext& runtimeInput;
    InputRouter& inputRouter;
    RunCameraState& camera;
    RuntimeTools& runtimeTools;
    ReplayRuntime& replayRuntime;
    ReplayRuntime::PathPickInput replayPointerRay;
    RunCameraMode replayCurrentCameraMode = RunCameraMode::Demo;
    RunCameraMode replayRestoreCameraMode = RunCameraMode::Demo;
    AttachedCameraController& attachedCamera;
    RuntimeInteractionController& interaction;
    RunTimerState& timers;
    RunDebugState& debug;
    RunLaunchOptions& launchOptions;
    RunRuntimeSettings& runtimeSettings;
    EngineConfig& config;
    RunSceneState& sceneState;
    SceneController& sceneController;
    RunSubsystemState& systems;
    SimulationSystem& simulation;
    SkullbonezCore::Runtime::Audio::ContactAudioService& contactAudio;
    SkullbonezCore::Environment::WorldEnvironment& worldEnvironment;
    SkullbonezCore::GameObjects::GameModelCollection& gameModels;
    RuntimeRenderBackendView& renderBackendView;
    RenderDefaultsStore& renderDefaults;
    CinematicRenderConfig& defaultCinematicRender;
    SkullbonezCore::UI::InGameUI& ui;
    int gameModelCapacity = 0;
};

struct RuntimeUIFrameResult
{
    SbResult status = SbResult::Success();
    ReplayRuntime::ReplayWorkspaceOutput replayWorkspace;
    bool suppressWorldActionThisFrame = false;
    int editorUnhandledWheelDelta = 0;
};

template <typename CameraModeEnabledMask,
          typename EnterInteractiveSceneRun,
          typename DispatchAfterUIKeyboardActions,
          typename UpdateRuntimeInputModeAfterAction,
          typename ApplyCameraMode,
          typename ApplyEditorPlacementModeChange,
          typename ApplyEditorModeToggle,
          typename ApplyEditorPlacementModeToggle,
          typename ResetReplayTimelineForActiveScene,
          typename RunUIStressActions,
          typename TickEditorViewportAndPlacementScaleInput>
RuntimeUIFrameResult
ApplyRuntimeUIFrameCommands( const RuntimeUIFrameContext& context,
                             bool suppressWorldActionThisFrame,
                             bool keyboardToggleEditorMode,
                             CameraModeEnabledMask cameraModeEnabledMask,
                             EnterInteractiveSceneRun enterInteractiveSceneRun,
                             DispatchAfterUIKeyboardActions dispatchAfterUIKeyboardActions,
                             UpdateRuntimeInputModeAfterAction updateRuntimeInputModeAfterAction,
                             ApplyCameraMode applyCameraMode,
                             ApplyEditorPlacementModeChange applyEditorPlacementModeChange,
                             ApplyEditorModeToggle applyEditorModeToggle,
                             ApplyEditorPlacementModeToggle applyEditorPlacementModeToggle,
                             ResetReplayTimelineForActiveScene resetReplayTimelineForActiveScene,
                             RunUIStressActions runUIStressActions,
                             TickEditorViewportAndPlacementScaleInput tickEditorViewportAndPlacementScaleInput )
{
    RuntimeUIFrameResult result;
    result.suppressWorldActionThisFrame = suppressWorldActionThisFrame;
    if ( !context.systems.window )
    {
        return result;
    }

    const int selectedSceneBrowserIndex =
        CurrentSceneBrowserIndex( context.sceneController, context.sceneController.Browser() );
    const HWND windowHandle = context.systems.window->NativeWindowHandle();
    InGameUIInputResult UIResult = context.ui.UpdateInput(
        context.inputRouter.DeviceFrame(),
        context.inputRouter.UiSnapshot().mouse,
        context.systems.window->ClientWidth(),
        context.systems.window->ClientHeight(),
        context.timers.simulationTimer.GetTotalTime(),
        context.runtimeTools.Editor().editorModeEnabled,
        context.runtimeTools.Editor().placementModeEnabled,
        context.runtimeTools.Editor().placeStaticObject,
        context.runtimeTools.Editor().autoTerrainAlign,
        context.runtimeTools.Editor().objectType,
        static_cast<int>( context.camera.mode ),
        cameraModeEnabledMask(),
        context.sceneController.Browser().namePtrs.empty() ? nullptr
                                                           : context.sceneController.Browser().namePtrs.data(),
        static_cast<int>( context.sceneController.Browser().namePtrs.size() ),
        selectedSceneBrowserIndex );
    switch ( UIResult.nativeMouseCapture )
    {
    case InGameUIInputResult::NativeMouseCaptureRequest::Acquire:
        context.inputRouter.RequestNativeCapture();
        break;
    case InGameUIInputResult::NativeMouseCaptureRequest::Release:
        context.inputRouter.ReleaseNativeCapture();
        break;
    case InGameUIInputResult::NativeMouseCaptureRequest::Unchanged:
    default:
        break;
    }
    result.editorUnhandledWheelDelta = UIResult.unhandledWheelDelta;
    const InGameUICommands& uiCommands = UIResult.commands;
    const DeviceInputFrame& deviceFrame = context.inputRouter.DeviceFrame();
    UiInputHitSnapshot uiSnapshot;
    uiSnapshot.mouse = context.inputRouter.UiSnapshot().mouse;
    uiSnapshot.clientX = deviceFrame.clientX;
    uiSnapshot.clientY = deviceFrame.clientY;
    uiSnapshot.hasClientPosition = deviceFrame.hasClientPosition;
    uiSnapshot.unhandledWheelDelta = UIResult.unhandledWheelDelta;
    uiSnapshot.userInteracted = uiCommands.ui.userInteracted;
    uiSnapshot.blocksKeyboard = context.ui.BlocksKeyboard();
    uiSnapshot.blocksCameraMouse = context.ui.BlocksCameraMouse();
    uiSnapshot.wantsNativeCursor = context.ui.WantsNativeMouseCursor();
    context.inputRouter.PublishUiSnapshot( uiSnapshot );
    if ( uiCommands.ui.userInteracted )
    {
        enterInteractiveSceneRun();
    }
    result.suppressWorldActionThisFrame = result.suppressWorldActionThisFrame || uiCommands.ui.userInteracted;
    context.replayRuntime.TickWorkspace(
        ReplayRuntime::ReplayWorkspaceInput{ windowHandle,
                                             context.ui.BlocksCameraMouse(),
                                             result.editorUnhandledWheelDelta,
                                             context.replayPointerRay,
                                             context.inputRouter,
                                             context.interaction,
                                             context.sceneController.Physics(),
                                             context.sceneController.Entities(),
                                             context.gameModels.RenderPresentationRecords(),
                                             &context.sceneController.Cameras(),
                                             context.sceneController.Terrain().Get(),
                                             context.camera,
                                             context.runtimeTools.MousePickup(),
                                             context.replayCurrentCameraMode,
                                             context.replayRestoreCameraMode,
                                             context.attachedCamera.State().activeFollow,
                                             context.camera.director.grabbed,
                                             context.runtimeTools.Editor().editorModeEnabled,
                                             context.sceneState.isScenePhysics,
                                             context.ui.IsVisible(),
                                             context.ui.IsMinimized(),
                                             context.systems.window->ClientWidth(),
                                             context.systems.window->ClientHeight(),
                                             context.timers.simulationTimer.GetTotalTime() },
        result.replayWorkspace );
    if ( result.replayWorkspace.enterInteractive )
    {
        enterInteractiveSceneRun();
    }
    result.suppressWorldActionThisFrame = result.suppressWorldActionThisFrame || result.replayWorkspace.consumesMouse;
    context.runtimeInput.BeginFrame( true,
                                     context.ui.BlocksKeyboard(),
                                     context.ui.BlocksCameraMouse() || result.replayWorkspace.consumesMouse );

    dispatchAfterUIKeyboardActions( uiCommands.ui.userInteracted );

    const auto recordUIAction = [&updateRuntimeInputModeAfterAction]( RuntimeInputAction action )
    { updateRuntimeInputModeAfterAction( action, RuntimeInputActionSource::UI ); };

    if ( ApplyRenderVsyncUICommand(
             RenderDeviceUICommandContext{ context.runtimeSettings, context.renderBackendView.deviceLifecycle },
             uiCommands.renderer ) )
    {
        recordUIAction( RuntimeInputAction::ToggleVsync );
    }
    const RunCameraModeUICommandResult cameraModeCommand = DecodeRunCameraModeUICommand( uiCommands.run );
    if ( cameraModeCommand.accepted )
    {
        applyCameraMode( cameraModeCommand.mode, RuntimeInputActionSource::UI );
    }
    const RunInternal::EditorGizmoContext editorGizmoContext{ context.runtimeTools.Editor(),
                                                              context.gameModels,
                                                              context.sceneController.Physics(),
                                                              context.interaction };
    const RunInternal::EditorPlacementPreModeUICommandResult editorPreModeCommands =
        RunInternal::ApplyEditorPlacementPreModeUICommands( editorGizmoContext, uiCommands.editor );
    if ( editorPreModeCommands.setPlaceStatic )
    {
        enterInteractiveSceneRun();
        recordUIAction( RuntimeInputAction::ToggleEditorStaticPlacement );
    }
    if ( editorPreModeCommands.enterPlacementMode )
    {
        applyEditorPlacementModeChange( RuntimeInputActionSource::UI, true, false );
    }
    if ( editorPreModeCommands.requestedObjectType )
    {
        recordUIAction( RuntimeInputAction::CycleEditorPlacementType );
    }
    if ( editorPreModeCommands.toggleEditorMode || keyboardToggleEditorMode )
    {
        applyEditorModeToggle( keyboardToggleEditorMode ? RuntimeInputActionSource::Keyboard
                                                        : RuntimeInputActionSource::UI );
    }
    if ( editorPreModeCommands.togglePlacementMode )
    {
        applyEditorPlacementModeToggle( RuntimeInputActionSource::UI );
    }
    const RunInternal::EditorPlacementPostModeUICommandResult editorPostModeCommands =
        RunInternal::ApplyEditorPlacementPostModeUICommands( context.runtimeTools.Editor(), uiCommands.editor );
    if ( editorPostModeCommands.toggledPlaceStatic )
    {
        enterInteractiveSceneRun();
        recordUIAction( RuntimeInputAction::ToggleEditorStaticPlacement );
    }
    if ( editorPostModeCommands.toggledTerrainAlign )
    {
        enterInteractiveSceneRun();
        recordUIAction( RuntimeInputAction::ToggleEditorTerrainAlign );
    }
    const DiagnosticsPhysicsOverlayUICommandResult physicsDiagnosticsCommands =
        ApplyDiagnosticsPhysicsOverlayUICommands( context.debug, uiCommands.physics );
    if ( physicsDiagnosticsCommands.toggledCollisionVisualizer )
    {
        recordUIAction( RuntimeInputAction::ToggleCollisionVisualizer );
    }
    if ( ApplyPhysicsSleepPolicyUICommand(
             PhysicsSleepPolicyUICommandContext{ context.runtimeSettings, context.gameModels },
             uiCommands.physics ) )
    {
        recordUIAction( RuntimeInputAction::TogglePhysicsSleepPolicy );
    }
    RecordDiagnosticsPhysicsOverlayUIActions( physicsDiagnosticsCommands, recordUIAction );
    const TornadoUICommandResult tornadoCommands =
        ApplyTornadoUICommands( TornadoUICommandContext{ context.runtimeSettings, context.gameModels },
                                uiCommands.physics );
    RecordTornadoToggleUIActions( tornadoCommands, recordUIAction );
    if ( context.runtimeTools.ApplyRayCastVisualizationUICommand( uiCommands.physics ) )
    {
        recordUIAction( RuntimeInputAction::ToggleRayCastVisualization );
    }
    RecordTornadoApplySettingsUIActions( tornadoCommands, recordUIAction );
    if ( ApplyDiagnosticsTerrainContactProbeUICommand( context.debug, uiCommands.physics ) )
    {
        recordUIAction( RuntimeInputAction::ToggleTerrainContactProbe );
    }
    if ( ApplyRuntimeTextOnlyUICommand( context.debug, uiCommands.sceneOptions ) )
    {
        recordUIAction( RuntimeInputAction::ToggleTextOnly );
    }
    if ( ApplySceneFixedStepUICommand( SceneFixedStepUICommandContext{ context.sceneState, context.simulation },
                                       uiCommands.sceneOptions ) )
    {
        recordUIAction( RuntimeInputAction::ToggleFixedStep );
    }
    const RuntimePresentationUICommandResult presentationCommands = ApplyRuntimePresentationUICommands(
        RuntimePresentationUICommandContext{ context.debug,
                                             context.sceneState,
                                             context.config,
                                             context.launchOptions,
                                             context.renderDefaults,
                                             context.renderBackendView.deviceLifecycle != nullptr,
                                             context.timers.simulationTimer.GetTimeSinceLastStart() },
        uiCommands.sceneOptions,
        uiCommands.renderTuning,
        uiCommands.water );
    RecordRuntimePresentationUIActions( presentationCommands, recordUIAction );
    if ( ApplySoundUICommands( SoundUICommandContext{ context.contactAudio,
                                                      context.runtimeSettings,
                                                      context.launchOptions.noContactAudio },
                               uiCommands.sound ) )
    {
        recordUIAction( RuntimeInputAction::ApplySoundTuning );
    }
    if ( uiCommands.replayMemory.requestPolicy )
    {
        // Invariant: Memory-tab controls only request policy changes. ReplayRuntime
        // owns the reset/reconfigure edge because it knows all recorder windows.
        ReplayMemoryPolicyRequest request;
        request.presetIndex = uiCommands.replayMemory.requestedPresetIndex;
        request.retentionSeconds = uiCommands.replayMemory.requestedRetentionSeconds;
        request.budgetMiB = uiCommands.replayMemory.requestedBudgetMiB;
        if ( context.replayRuntime.ApplyMemoryPolicyRequest( request ) )
        {
            recordUIAction( RuntimeInputAction::SetReplayMemoryPolicy );
        }
    }
    RecordRuntimePresentationWaterUIActions( presentationCommands, recordUIAction );
    const RunSimulationUICommandResult runSimulationCommands =
        ApplyRunSimulationUICommands( RunSimulationUICommandContext{ context.sceneState,
                                                                     context.sceneController.UIOverrides(),
                                                                     context.config,
                                                                     *context.systems.workerPool },
                                      uiCommands.sceneOptions,
                                      uiCommands.run,
                                      uiCommands.profiler );
    RecordRunSimulationUIActions( runSimulationCommands, recordUIAction );
    const DiagnosticsPhysicsDebugValueUICommandResult physicsDebugValueCommands =
        ApplyDiagnosticsPhysicsDebugValueUICommands( context.debug, uiCommands.physics );
    RecordDiagnosticsPhysicsDebugValueUIActions( physicsDebugValueCommands, recordUIAction );
    const RayCastLauncherTuningUICommandResult rayCastLauncherCommands =
        context.runtimeTools.ApplyRayCastLauncherTuningUICommands( uiCommands.physics );
    if ( rayCastLauncherCommands.setImpulseStrength )
    {
        context.replayRuntime.RecordLauncherConfigEvent( rayCastLauncherCommands.impulseConfigChangedFlags,
                                                         rayCastLauncherCommands.impulseConfigImpulseStrength,
                                                         rayCastLauncherCommands.impulseConfigProjectileSpeed );
        recordUIAction( RuntimeInputAction::SetRayCastImpulseStrength );
    }
    if ( rayCastLauncherCommands.setProjectileSpeed )
    {
        context.replayRuntime.RecordLauncherConfigEvent( rayCastLauncherCommands.projectileConfigChangedFlags,
                                                         rayCastLauncherCommands.projectileConfigImpulseStrength,
                                                         rayCastLauncherCommands.projectileConfigProjectileSpeed );
        recordUIAction( RuntimeInputAction::SetLauncherProjectileSpeed );
    }
    EngineConfig& liveConfig = context.config;
    const PhysicsFrictionUICommandResult physicsFrictionCommands =
        ApplyPhysicsFrictionUICommands( PhysicsFrictionUICommandContext{ liveConfig, context.gameModels },
                                        uiCommands.physics );
    RecordPhysicsFrictionUIActions( physicsFrictionCommands, recordUIAction );
    const auto makeSceneGeneratedControlContext = [&context, &liveConfig]() -> SceneRuntimeGeneratedControlContext
    {
        return SceneRuntimeGeneratedControlContext{ context.sceneState,
                                                    context.sceneController.UIOverrides(),
                                                    context.camera,
                                                    context.sceneController,
                                                    liveConfig,
                                                    context.worldEnvironment,
                                                    context.sceneController.Terrain().Get(),
                                                    context.gameModels,
                                                    context.simulation,
                                                    context.runtimeTools,
                                                    context.renderBackendView.deviceLifecycle,
                                                    context.launchOptions.generatedObjectTypeOverride,
                                                    context.gameModelCapacity };
    };
    const auto executeSceneGeneratedControlAction =
        [&resetReplayTimelineForActiveScene]( const SceneRuntimeGeneratedControlAction& action )
    {
        if ( action.resetReplayTimeline )
        {
            resetReplayTimelineForActiveScene();
        }
        if ( action.scheduleProfileReset )
        {
            PROFILE_SCHEDULE_RESET();
        }
    };
    const SceneGeneratedUICommandResult modelCountCommand =
        ApplySceneGeneratedModelCountUICommand( makeSceneGeneratedControlContext(),
                                                uiCommands.sceneOptions.requestedModelCount );
    if ( !modelCountCommand.action.status.ok )
    {
        result.status = modelCountCommand.action.status;
        return result;
    }
    if ( modelCountCommand.accepted )
    {
        executeSceneGeneratedControlAction( modelCountCommand.action );
        recordUIAction( RuntimeInputAction::SetModelCount );
    }
    if ( runSimulationCommands.setWorkerThreads )
    {
        recordUIAction( RuntimeInputAction::SetWorkerThreads );
    }
    const SceneGeneratedUICommandResult solverBallCountCommand =
        ApplySceneGeneratedSolverBallCountUICommand( makeSceneGeneratedControlContext(),
                                                     uiCommands.run.requestedSolverBallCount );
    if ( !solverBallCountCommand.action.status.ok )
    {
        result.status = solverBallCountCommand.action.status;
        return result;
    }
    if ( solverBallCountCommand.accepted )
    {
        executeSceneGeneratedControlAction( solverBallCountCommand.action );
        recordUIAction( RuntimeInputAction::SetSolverCounts );
    }
    const SceneGeneratedUICommandResult solverBoxCountCommand =
        ApplySceneGeneratedSolverBoxCountUICommand( makeSceneGeneratedControlContext(),
                                                    uiCommands.run.requestedSolverBoxCount );
    if ( !solverBoxCountCommand.action.status.ok )
    {
        result.status = solverBoxCountCommand.action.status;
        return result;
    }
    if ( solverBoxCountCommand.accepted )
    {
        executeSceneGeneratedControlAction( solverBoxCountCommand.action );
        recordUIAction( RuntimeInputAction::SetSolverCounts );
    }
    if ( ApplyWorldWaterUICommands( context.worldEnvironment, context.replayRuntime, uiCommands.water ) )
    {
        recordUIAction( RuntimeInputAction::ApplyWorldWaterSettings );
    }
    CinematicRenderConfig& activeCinematic = RuntimeActiveCinematicConfig( context.sceneState, context.config );
    const CinematicUICommandContext cinematicUICommandContext{ context.launchOptions,
                                                               context.sceneState,
                                                               activeCinematic,
                                                               context.renderDefaults };
    if ( ApplyCinematicRenderingToggleUICommand( cinematicUICommandContext, uiCommands.cinematic ) )
    {
        recordUIAction( RuntimeInputAction::ToggleCinematicRendering );
    }
    if ( QueueCinematicSkyDefaultsUICommand( cinematicUICommandContext, uiCommands.cinematic ) )
    {
        recordUIAction( RuntimeInputAction::SaveSkyDefaults );
    }
    if ( HasCinematicModeUICommand( uiCommands.cinematic ) )
    {
        enterInteractiveSceneRun();
        ApplyCinematicModeUICommand( SceneRuntimeStyleContext{ context.launchOptions,
                                                               context.sceneState,
                                                               context.sceneController.Browser(),
                                                               context.gameModels,
                                                               context.sceneController.Entities(),
                                                               context.systems.assets,
                                                               activeCinematic,
                                                               context.defaultCinematicRender },
                                     uiCommands.cinematic );
        recordUIAction( RuntimeInputAction::SelectCinematicScene );
    }
    const CinematicTuningUICommandResult cinematicTuningCommands =
        ApplyCinematicTuningUICommands( cinematicUICommandContext, uiCommands.cinematic );
    RecordCinematicTuningUIActions( cinematicTuningCommands, recordUIAction );
    const SceneRuntimeUICommandResult sceneUICommands =
        SubmitSceneUIRequests( context.sceneController, uiCommands.scene );
    if ( !sceneUICommands.status.ok )
    {
        result.status = sceneUICommands.status;
        return result;
    }
    RecordSceneRuntimeUIActions( sceneUICommands, recordUIAction );

    const SbResult stressResult = runUIStressActions();
    if ( !stressResult.ok )
    {
        result.status = stressResult;
        return result;
    }

    if ( context.attachedCamera.ApplyOrbitInput( context.gameModels,
                                                 context.sceneController.Cameras(),
                                                 RunCameraModeIsAttached( context.replayCurrentCameraMode ),
                                                 result.editorUnhandledWheelDelta,
                                                 context.ui.BlocksCameraMouse() ) )
    {
        enterInteractiveSceneRun();
    }
    tickEditorViewportAndPlacementScaleInput( result.editorUnhandledWheelDelta );
    return result;
}

} // namespace

void Run::UpdateRuntimeInputModeAfterAction( RuntimeInputAction action, RuntimeInputActionSource source )
{
    InputController::ApplyModeAction(
        m_runtimeInput,
        InputController::ResolveMode( BuildRuntimeInputModeState( m_camera.mode,
                                                                  m_runtimeTools.Editor(),
                                                                  m_attachedCamera.State().activeFollow,
                                                                  m_camera.director.grabbed ) ),
        action,
        source );
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
        consumedWorldClick = TickMousePickupInput( mouseEdges, suppressWorldAction );
    }
    if ( !consumedWorldClick )
    {
        consumedWorldClick = TickAttachedCameraWorldClick( mouseEdges, suppressWorldAction );
    }
    if ( !consumedWorldClick && leftPressed && !suppressWorldAction && !m_runtimeTools.Editor().editorModeEnabled &&
         !uiWantsNativeMouseCursor &&
         ( inputSnapshot.pointer.controlDown || !RunCameraModeUsesLauncher( m_camera.mode ) ) )
    {
        const bool additiveReplayPick = inputSnapshot.pointer.shiftDown;
        Vector3 rayOrigin;
        Vector3 rayDirection;
        ReplayRuntime::PathPickInput pickInput;
        pickInput.hasWorldRay = TryBuildMouseWorldRay( rayOrigin, rayDirection );
        pickInput.rayOrigin = rayOrigin;
        pickInput.rayDirection = rayDirection;
        pickInput.additive = additiveReplayPick;
        pickInput.clearOnMiss = !additiveReplayPick;
        const ReplayRuntime::PathPickResult pickResult =
            m_replayRuntime.TryPickPathTarget( pickInput,
                                               m_sceneController.Entities(),
                                               m_sceneController.Models().BodyStore(),
                                               m_sceneController.Models().Colliders(),
                                               m_sceneController.Models().RenderPresentationRecords() );
        if ( pickResult.exitInspectionCamera )
        {
            m_replayRuntime.ExitInspectionCamera(
                &m_sceneController.Cameras(),
                m_sceneController.Terrain().Get(),
                m_camera,
                NormalizeCameraModeForCurrentScene( m_replayRuntime.Camera().restoreCameraMode ),
                m_attachedCamera.State().activeFollow,
                m_camera.director.grabbed,
                m_interaction,
                m_inputRouter );
        }
        consumedWorldClick = true;
    }

    if ( !consumedWorldClick && RunCameraModeUsesLauncher( m_camera.mode ) && leftPressed && !suppressWorldAction &&
         !uiWantsNativeMouseCursor )
    {
        EnterInteractiveSceneRun();
        Vector3 rayOrigin;
        Vector3 rayDirection;
        Vector3 cameraUp;
        if ( m_runtimeTools.TryBuildLauncherCameraRay( &m_sceneController.Cameras(),
                                                       rayOrigin,
                                                       rayDirection,
                                                       cameraUp ) )
        {
            m_replayRuntime.RecordLauncherFireEvent(
                rayOrigin,
                rayDirection,
                cameraUp,
                m_runtimeTools.RayCastTest().fireMode == RunLauncherFireMode::Projectile,
                m_runtimeTools.RayCastTest().impulseStrength,
                m_runtimeTools.RayCastTest().projectileSpeed,
                m_sceneController.Models().SceneEntityCount() );
            // Why: RuntimeTools now fails closed unless Run has completed the
            // cold collection-to-store topology repair at the owner boundary.
            const bool launcherStoresReady = m_sceneController.Models().RepairPhysicsBodyAndColliderTopology();
            if ( launcherStoresReady && m_runtimeTools.FireLauncherRay( m_sceneController.Models(),
                                                                        m_sceneController.Physics(),
                                                                        SceneState(),
                                                                        m_sceneController.Terrain().Get(),
                                                                        m_startup.gameModelCapacity,
                                                                        rayOrigin,
                                                                        rayDirection,
                                                                        cameraUp ) )
            {
                SceneState().modelCount = m_sceneController.Models().SceneEntityCount();
            }
        }
        UpdateRuntimeInputModeAfterAction( RuntimeInputAction::FireLauncher, RuntimeInputActionSource::Mouse );
        consumedWorldClick = true;
    }

    return consumedWorldClick;
}


RuntimeInteractionTransition Run::EnterInteractionForCameraMode( RunCameraMode mode )
{
    return m_interaction.EnterCameraMode( NormalizeCameraModeForCurrentScene( mode ) );
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


void Run::ClearEditorInteractionForRuntimeTransition( bool clearSelection )
{
    RunInternal::ClearEditorManipulationState(
        { m_runtimeTools.Editor(), m_sceneController.Models(), m_sceneController.Physics(), m_interaction } );
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
    ReleaseRuntimeMouseToUI( m_inputRouter, m_runtimeTools.Editor(), m_replayRuntime, m_camera );
    ApplyRuntimeCursorOwnership( m_inputRouter, m_runtimeTools.Editor(), m_replayRuntime );
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

    if ( !enteringReplay &&
         ( m_replayRuntime.HasActiveInteractionState() || IsReplayWorldOwner( transition.previousOwner ) ) )
    {
        if ( m_replayRuntime.ClearInteractionForRuntimeTransition( m_interaction, m_inputRouter ) )
        {
            m_replayRuntime.ExitInspectionCamera(
                &m_sceneController.Cameras(),
                m_sceneController.Terrain().Get(),
                m_camera,
                NormalizeCameraModeForCurrentScene( m_replayRuntime.Camera().restoreCameraMode ),
                m_attachedCamera.State().activeFollow,
                m_camera.director.grabbed,
                m_interaction,
                m_inputRouter );
        }
        ApplyRuntimeCursorOwnership( m_inputRouter, m_runtimeTools.Editor(), m_replayRuntime );
    }

    if ( transition.previousOwner == WorldInteractionOwner::Manipulator &&
         transition.owner != WorldInteractionOwner::Manipulator )
    {
        m_runtimeTools.CancelMousePickup( m_inputRouter, m_interaction );
    }
    if ( enteringTool && transition.owner != WorldInteractionOwner::Manipulator )
    {
        m_runtimeTools.CancelMousePickup( m_inputRouter, m_interaction );
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
        if ( command.modelIndex < -1 || command.modelIndex >= m_sceneController.Models().SceneEntityCount() )
        {
            return false;
        }

        const PhysicsBodyStore& bodyStore = m_sceneController.Models().BodyStore();
        const ColliderStore& colliderStore = m_sceneController.Models().Colliders();
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
        if ( m_attachedCamera.State().submode == AttachedCameraSubmode::VelocityForward )
        {
            submode = "Velocity";
        }
        else if ( m_attachedCamera.State().submode == AttachedCameraSubmode::RagdollEyes )
        {
            submode = "Eyes";
        }
        if ( m_attachedCamera.State().target.modelIndex < 0 )
        {
            sprintf_s( label,
                       sizeof( label ),
                       "Attach: pick target%s",
                       m_attachedCamera.State().activeFollow ? "" : " Pinned" );
        }
        else
        {
            sprintf_s( label,
                       sizeof( label ),
                       "Attach: %s %s%s",
                       submode,
                       m_attachedCamera.State().target.name[0] ? m_attachedCamera.State().target.name : "target",
                       m_attachedCamera.State().activeFollow ? "" : " Pinned" );
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
    return m_sceneController.Models().SceneEntityCount() > 0;
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


void Run::SetAttachedCameraTarget( int modelIndex )
{
    AttachedCameraTargetSelection selection;
    if ( !AttachedCameraController::SelectTarget( m_sceneController.Models(),
                                                  m_attachedCamera.State(),
                                                  modelIndex,
                                                  selection ) )
    {
        return;
    }

    RuntimeInteractionCommand command;
    command.type = RuntimeInteractionCommandType::SetEditorSelection;
    command.modelIndex = modelIndex;
    command.body = m_attachedCamera.State().target.body;
    command.collider = m_attachedCamera.State().target.collider;
    command.selectionScope = RuntimeInteractionSelectionScope::Inspect;
    command.claimSelectionOwner = false;
    ExecuteRuntimeInteractionCommand( command );
    CaptureAttachedCameraFixedOffsetFromCurrentPose( m_attachedCamera.State(),
                                                     &m_sceneController.Cameras(),
                                                     selection.physics );
    ApplyRuntimeCursorOwnership( m_inputRouter, m_runtimeTools.Editor(), m_replayRuntime );
}


void Run::SeedAttachedCameraTargetFromSelection()
{
    AttachedCameraPhysicsTarget currentState;
    if ( AttachedCameraController::TryResolvePhysicsTarget( m_sceneController.Models(),
                                                            m_attachedCamera.State().target,
                                                            currentState ) )
    {
        CaptureAttachedCameraFixedOffsetFromCurrentPose( m_attachedCamera.State(),
                                                         &m_sceneController.Cameras(),
                                                         currentState );
        m_attachedCamera.State().activeFollow = true;
        ApplyRuntimeCursorOwnership( m_inputRouter, m_runtimeTools.Editor(), m_replayRuntime );
        return;
    }

    int seedIndex = -1;
    const RunReplayPathVisualizerState& path = m_replayRuntime.PathVisualizer();
    const PhysicsBodyStore& bodyStore = m_sceneController.Models().BodyStore();
    const int modelCount = bodyStore.Count();
    if ( path.hasTarget && path.targetModelRow.value >= 0 && path.targetModelRow.value < modelCount )
    {
        seedIndex = path.targetModelRow.value;
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
        m_attachedCamera.State().activeFollow = true;
        ApplyRuntimeCursorOwnership( m_inputRouter, m_runtimeTools.Editor(), m_replayRuntime );
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
        request.bodyStore = &m_sceneController.Models().BodyStore();
        request.colliderStore = &m_sceneController.Models().Colliders();
        request.rayOrigin = rayOrigin;
        request.rayDirection = rayDirection;

        if ( RuntimePickService::TryPickModel( request, result ) )
        {
            SetAttachedCameraTarget( result.modelIndex );
        }
        else
        {
            AttachedCameraController::ClearTarget( m_attachedCamera.State() );
        }
    }
    else
    {
        AttachedCameraController::ClearTarget( m_attachedCamera.State() );
    }
    EnterInteractiveSceneRun();
    UpdateRuntimeInputModeAfterAction( RuntimeInputAction::SetCameraMode, RuntimeInputActionSource::Mouse );
    return true;
}


bool Run::TickAttachedCameraWorldClick( const RuntimeMouseEdges& mouseEdges, bool suppressWorldActionThisFrame )
{
    if ( !RunCameraModeIsAttached( m_camera.mode ) || !mouseEdges.leftPressed )
    {
        return false;
    }
    if ( suppressWorldActionThisFrame )
    {
        return false;
    }
    return TryPickAttachedCameraTargetFromMouse();
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
        m_attachedCamera.CaptureReturnState( NormalizeCameraModeForCurrentScene( previousMode ),
                                             m_sceneController.Cameras() );
    }

    if ( mode == RunCameraMode::Demo )
    {
        const int modelCount = m_sceneController.Models().SceneEntityCount();
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
        DemoDirectorPlayback::EnterMode( m_camera, m_sceneController.Cameras() );
    }

    const RuntimeInteractionTransition transition = EnterInteractionForCameraMode( mode );
    ApplyRuntimeInteractionTransitionCleanup( transition );

    const bool wasFlyMode =
        RunCameraModeUsesFlyControls( m_camera.mode, m_attachedCamera.State().activeFollow, m_camera.director.grabbed );
    if ( mode != RunCameraMode::Launcher )
    {
        m_camera.modeBeforeLauncher = mode == RunCameraMode::Manipulator ? RunCameraMode::Inspect : mode;
    }
    SetCameraModeLabelAfterInteractionTransition( mode );
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
        ApplyRuntimeCursorOwnership( m_inputRouter, m_runtimeTools.Editor(), m_replayRuntime );
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
        const RunCameraMode restoreMode = NormalizeCameraModeForCurrentScene( m_attachedCamera.State().returnMode );
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


void Run::EnterFlyModeCamera()
{
    // Entering fly mode: generated demo mode snaps to free camera; scene mode stays
    // on the current camera so fly controls work without requiring CAMERA_FREE
    if ( !SceneState().isSceneMode )
    {
        m_sceneController.Cameras().SelectCamera( CAMERA_FREE, true );
    }
    m_camera.cameraTime = 0.0f;
    XZBounds unbounded;
    unbounded.m_xMin = -99999.9f;
    unbounded.m_xMax = 99999.9f;
    unbounded.m_zMin = -99999.9f;
    unbounded.m_zMax = 99999.9f;
    uint32_t activeCam = SceneState().isSceneMode ? m_sceneController.Cameras().GetSelectedCameraName() : CAMERA_FREE;
    m_sceneController.Cameras().SetCameraXZBounds( activeCam, unbounded );
    if ( RuntimeShouldHideNativeCursor( m_inputRouter, m_runtimeTools.Editor(), m_replayRuntime ) )
    {
        m_inputRouter.RequestCursorVisible( false );
    }
    else
    {
        ReleaseRuntimeMouseToUI( m_inputRouter, m_runtimeTools.Editor(), m_replayRuntime, m_camera );
        m_inputRouter.RequestCursorVisible( true );
    }
    InputController::ResetMouseLook( m_camera );
}


void Run::ExitFlyModeCamera()
{
    // Exiting fly mode restores terrain bounds, the camera-cycle clock, and
    // the stock Windows cursor.
    uint32_t activeCam = SceneState().isSceneMode ? m_sceneController.Cameras().GetSelectedCameraName() : CAMERA_FREE;
    m_sceneController.Cameras().SetCameraXZBounds( activeCam, m_sceneController.Terrain().Get()->GetXZBounds() );
    m_inputRouter.RequestCursorVisible( true );
    m_camera.cameraTime = 0.0f;
    InputController::ResetMouseLook( m_camera );
}


bool Run::HandleUnfocusedInputFrame()
{
    if ( m_inputRouter.AppFocused() )
    {
        return false;
    }

    // Invariant: focus loss releases every active tool capture and refreshes
    // action memory so refocus cannot replay stale drag/key edges.
    m_interaction.CancelCameraLookGesture();
    m_replayRuntime.CancelToolDragState( m_interaction, m_inputRouter );
    m_inputRouter.CancelPointerPresentation();
    if ( m_replayRuntime.ResetScrubberState() )
    {
        m_replayRuntime.ExitInspectionCamera(
            &m_sceneController.Cameras(),
            m_sceneController.Terrain().Get(),
            m_camera,
            NormalizeCameraModeForCurrentScene( m_replayRuntime.Camera().restoreCameraMode ),
            m_attachedCamera.State().activeFollow,
            m_camera.director.grabbed,
            m_interaction,
            m_inputRouter );
    }
    m_replayRuntime.Prediction().ui.checkboxHovered = false;
    m_replayRuntime.Prediction().ui.decreaseHovered = false;
    m_replayRuntime.Prediction().ui.increaseHovered = false;
    m_replayRuntime.Prediction().ui.horizonHovered = false;
    m_replayRuntime.Prediction().ui.horizonDragging = false;
    m_replayRuntime.PathVisualizer().pastPathHovered = false;
    m_replayRuntime.VelocityEdit().toggleHovered = false;
    m_replayRuntime.VelocityEdit().keyboardAltWasDown = false;
    m_replayRuntime.VelocityEdit().dragging = false;
    m_replayRuntime.VelocityEdit().draggingAngular = false;
    m_replayRuntime.VelocityEdit().activeAxis = -1;
    m_replayRuntime.VelocityEdit().hotLinearAxis = -1;
    m_replayRuntime.VelocityEdit().hotAngularAxis = -1;
    if ( m_replayRuntime.VelocityEdit().mouseCaptured )
    {
        m_inputRouter.ReleaseNativeCapture();
        m_replayRuntime.VelocityEdit().mouseCaptured = false;
    }
    m_runtimeTools.CancelMousePickup( m_inputRouter, m_interaction );
    if ( m_replayRuntime.CauseTree().draggingWindow || m_replayRuntime.CauseTree().resizingWindow )
    {
        m_inputRouter.ReleaseNativeCapture();
        m_replayRuntime.CauseTree().draggingWindow = false;
        m_replayRuntime.CauseTree().resizingWindow = false;
    }
    RunInternal::ResetEditorUnfocusedInputState(
        { m_runtimeTools.Editor(), m_sceneController.Models(), m_sceneController.Physics(), m_interaction } );
    InputController::ResetUnfocusedInput( m_camera );
    InputController::BeginFrame( m_runtimeInput,
                                 BuildRuntimeInputModeState( m_camera.mode,
                                                             m_runtimeTools.Editor(),
                                                             m_attachedCamera.State().activeFollow,
                                                             m_camera.director.grabbed ),
                                 false,
                                 true,
                                 true );
    m_UI.CancelInputCapture();
    const SbResult stressResult = RunUIStressActions();
    if ( !stressResult.ok )
    {
        // Lane R: focus loss still routes stress churn through the same guarded
        // rebuild path. End the run before returning to the frame loop.
        ReportRuntimeInputFailure( stressResult );
        std::fflush( stderr );
        m_applicationExit.RequestOwnedFailure( stressResult );
        PostQuitMessage( 1 );
    }
    return true;
}


void Run::DispatchPostUIKeyboardActions()
{
    // Why: capture/reset shortcuts run after UI input so focused controls and
    // panels get first refusal on keyboard ownership.
    const bool flyCamera =
        RunCameraModeUsesFlyControls( m_camera.mode, m_attachedCamera.State().activeFollow, m_camera.director.grabbed );
    const KeyboardContextFacts contextFacts{ !m_UI.BlocksKeyboard(),
                                             SceneState().isSceneMode,
                                             flyCamera,
                                             RunCameraModeUsesLauncher( m_camera.mode ),
                                             RunCameraModeIsAttached( m_camera.mode ),
                                             m_camera.mode == RunCameraMode::Director,
                                             m_camera.mode == RunCameraMode::Director || flyCamera,
                                             !m_replayRuntime.Scrubber().restoreConsumedThisFrame,
                                             false };
    const RuntimeInputKeyBindingView bindings = TakeInputKeyboardBindings();
    m_inputRouter.RoutePhase( bindings,
                              InputActionPhase::Capture,
                              BuildKeyboardContextMask( contextFacts ),
                              m_inputActions );
    if ( m_inputActions.Overflowed() )
    {
        SB_FATAL( "InputRouter", "Fixed input action capacity exhausted while routing capture actions." );
    }

    const RunInternal::EditorSaveHotkeyContext editorSaveHotkeyContext{ m_sceneController.Models(),
                                                                        m_sceneController.Entities(),
                                                                        SceneState(),
                                                                        m_sceneController.World(),
                                                                        m_sceneController.Cameras(),
                                                                        m_diagnosticsRuntime.Capture() };
    // Invariant: side-effect dispatch consumes only accepted semantic events.
    // It must not reopen hardware polling or maintain a second edge latch.
    for ( std::size_t index = 0; index < m_inputActions.Count(); ++index )
    {
        const InputActionEvent& event = m_inputActions[index];
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
            m_sceneController.SubmitResetCurrentScene();
            break;
        case RuntimeInputAction::ResetSceneFromBackspace:
            if ( SceneState().isSceneMode )
            {
                // Backspace is only a scene-mode reset alias; generated demos keep
                // the key free for future non-scene tools.
                m_sceneController.SubmitResetCurrentScene();
            }
            break;
        default:
            break;
        }
    }
}


void Run::DispatchAfterUIKeyboardActions( bool uiUserInteracted )
{
    const bool flyCamera =
        RunCameraModeUsesFlyControls( m_camera.mode, m_attachedCamera.State().activeFollow, m_camera.director.grabbed );
    const KeyboardContextFacts contextFacts{ !m_UI.BlocksKeyboard(),
                                             SceneState().isSceneMode,
                                             flyCamera,
                                             RunCameraModeUsesLauncher( m_camera.mode ),
                                             RunCameraModeIsAttached( m_camera.mode ),
                                             m_camera.mode == RunCameraMode::Director,
                                             m_camera.mode == RunCameraMode::Director || flyCamera,
                                             !m_replayRuntime.Scrubber().restoreConsumedThisFrame,
                                             !uiUserInteracted };
    const RuntimeInputKeyBindingView bindings = TakeInputKeyboardBindings();
    m_inputRouter.RoutePhase( bindings,
                              InputActionPhase::AfterUi,
                              BuildKeyboardContextMask( contextFacts ),
                              m_inputActions );
    if ( m_inputActions.Overflowed() )
    {
        SB_FATAL( "InputRouter", "Fixed input action capacity exhausted while routing after-UI actions." );
    }

    for ( std::size_t index = 0; index < m_inputActions.Count(); ++index )
    {
        const InputActionEvent& event = m_inputActions[index];
        if ( event.phase != InputActionPhase::AfterUi || event.action != RuntimeInputAction::DismissOrExitUI ||
             event.edge != InputActionEdge::Pressed )
        {
            continue;
        }

        // ESC is intentionally after UI processing: focused controls keep
        // local ESC behavior before the diagnostics window reacts.
        constexpr double ESC_QUICK_EXIT_SECONDS = 0.32;
        const double UINow = m_timers.simulationTimer.GetTotalTime();
        if ( m_inputRouter.IsQuickRepeat( event.action, UINow, ESC_QUICK_EXIT_SECONDS ) )
        {
            PostQuitMessage( 0 );
        }
        else
        {
            EnterInteractiveSceneRun();
            m_UI.ToggleVisible( UINow );
            m_debug.overlayMode = OverlayMode::None;
            m_inputRouter.RecordTap( event.action, UINow );
            ApplyRuntimeCursorOwnership( m_inputRouter, m_runtimeTools.Editor(), m_replayRuntime );
            ReleaseRuntimeMouseToUI( m_inputRouter, m_runtimeTools.Editor(), m_replayRuntime, m_camera );
        }
    }
}


void Run::TakeInput()
{
    DeviceInputFrame deviceFrame;
    const SbResult deviceCaptureResult = Input::CaptureDeviceInputFrame( deviceFrame );
    if ( !deviceCaptureResult.ok )
    {
        ReportRuntimeInputFailure( deviceCaptureResult );
        std::fflush( stderr );
        m_applicationExit.RequestOwnedFailure( deviceCaptureResult );
        PostQuitMessage( 1 );
        return;
    }
    const RuntimeInputKeyBindingView keyboardBindings = TakeInputKeyboardBindings();
    m_inputRouter.BeginFrame( deviceFrame, keyboardBindings, m_inputActions );
    UiInputHitSnapshot preUiPointer;
    preUiPointer.mouse = m_inputActions.mouse;
    preUiPointer.clientX = deviceFrame.clientX;
    preUiPointer.clientY = deviceFrame.clientY;
    preUiPointer.hasClientPosition = deviceFrame.hasClientPosition;
    preUiPointer.unhandledWheelDelta = deviceFrame.wheelDelta;
    m_inputRouter.PublishUiSnapshot( preUiPointer );
    auto commitPointerPresentation = [this]()
    {
        PointerPresentationState presentation;
        if ( !m_inputRouter.ConsumePointerPresentationChange( presentation ) )
        {
            return;
        }
        SbResult pointerResult = Input::SetNativeMouseCapture( presentation.nativeCapture );
        if ( pointerResult.ok )
        {
            Input::SetSystemCursorVisible( presentation.cursorVisible );
        }
        if ( !pointerResult.ok )
        {
            ReportRuntimeInputFailure( pointerResult );
            m_applicationExit.RequestOwnedFailure( pointerResult );
            PostQuitMessage( 1 );
        }
    };
    if ( HandleUnfocusedInputFrame() )
    {
        commitPointerPresentation();
        return;
    }
    const bool UIBlocksKeyboardBeforeInput = m_UI.BlocksKeyboard();
    ApplyRuntimeCursorOwnership( m_inputRouter, m_runtimeTools.Editor(), m_replayRuntime );
    InputController::BeginFrame( m_runtimeInput,
                                 BuildRuntimeInputModeState( m_camera.mode,
                                                             m_runtimeTools.Editor(),
                                                             m_attachedCamera.State().activeFollow,
                                                             m_camera.director.grabbed ),
                                 true,
                                 UIBlocksKeyboardBeforeInput,
                                 m_UI.BlocksCameraMouse() );
    bool keyboardToggleEditorMode = false;
    RunInternal::EditorKeyboardShortcutResult keyboardEditorToolShortcut;
    auto completeEditorPlacementModeTransition =
        [this]( RuntimeInputActionSource source, const RunInternal::EditorPlacementModeChangeResult& placementMode )
    {
        SetWorldInteractionOwnerAfterInteractionTransition( placementMode.worldOwner,
                                                            InteractionExitReason::EnterEdit );
        ReleaseRuntimeMouseToUI( m_inputRouter, m_runtimeTools.Editor(), m_replayRuntime, m_camera );
        ApplyRuntimeCursorOwnership( m_inputRouter, m_runtimeTools.Editor(), m_replayRuntime );
        UpdateRuntimeInputModeAfterAction( RuntimeInputAction::ToggleEditorTool, source );
    };
    auto applyEditorPlacementModeChange = [this, &completeEditorPlacementModeTransition](
                                              RuntimeInputActionSource source,
                                              bool enabled,
                                              bool clearManipulation )
    {
        EnterInteractiveSceneRun();
        const RunInternal::EditorPlacementModeChangeResult placementMode = RunInternal::SetEditorPlacementMode(
            { m_runtimeTools.Editor(), m_sceneController.Models(), m_sceneController.Physics(), m_interaction },
            enabled,
            clearManipulation );
        completeEditorPlacementModeTransition( source, placementMode );
    };
    auto applyEditorPlacementModeToggle =
        [this, &completeEditorPlacementModeTransition]( RuntimeInputActionSource source )
    {
        EnterInteractiveSceneRun();
        const RunInternal::EditorPlacementModeChangeResult placementMode = RunInternal::ToggleEditorPlacementMode(
            { m_runtimeTools.Editor(), m_sceneController.Models(), m_sceneController.Physics(), m_interaction } );
        completeEditorPlacementModeTransition( source, placementMode );
    };
    auto applyEditorModeToggle = [this]( RuntimeInputActionSource source )
    {
        EnterInteractiveSceneRun();
        const bool enteringEditor = !m_runtimeTools.Editor().editorModeEnabled;
        if ( enteringEditor )
        {
            ApplyRuntimeInteractionTransitionCleanup( m_interaction.EnterEdit() );
            const bool wasFlyMode = RunCameraModeUsesFlyControls( m_camera.mode,
                                                                  m_attachedCamera.State().activeFollow,
                                                                  m_camera.director.grabbed );
            RunInternal::EnterEditorModeState(
                { m_runtimeTools.Editor(), m_sceneController.Models(), m_sceneController.Physics(), m_interaction },
                NormalizeCameraModeForCurrentScene( m_camera.mode ) );
            m_runtimeTools.CancelMousePickup( m_inputRouter, m_interaction );
            SetCameraModeLabelAfterInteractionTransition( RunCameraMode::Inspect );
            if ( !wasFlyMode )
            {
                EnterFlyModeCamera();
            }
            else
            {
                InputController::ResetMouseLook( m_camera );
            }
        }
        else
        {
            const RunCameraMode restoreMode =
                NormalizeCameraModeForCurrentScene( m_runtimeTools.Editor().restoreCameraModeAfterEditor );
            ApplyRuntimeInteractionTransitionCleanup( EnterInteractionForCameraMode( restoreMode ) );
            const bool wasFlyMode = RunCameraModeUsesFlyControls( m_camera.mode,
                                                                  m_attachedCamera.State().activeFollow,
                                                                  m_camera.director.grabbed );
            RunInternal::ExitEditorModeState(
                { m_runtimeTools.Editor(), m_sceneController.Models(), m_sceneController.Physics(), m_interaction } );
            SetCameraModeLabelAfterInteractionTransition( restoreMode );
            if ( wasFlyMode && !RunCameraModeUsesFlyControls( m_camera.mode,
                                                              m_attachedCamera.State().activeFollow,
                                                              m_camera.director.grabbed ) )
            {
                ExitFlyModeCamera();
            }
            else
            {
                InputController::ResetMouseLook( m_camera );
            }
        }
        ApplyRuntimeCursorOwnership( m_inputRouter, m_runtimeTools.Editor(), m_replayRuntime );
        UpdateRuntimeInputModeAfterAction( RuntimeInputAction::ToggleEditor, source );
    };
    const bool flyCamera =
        RunCameraModeUsesFlyControls( m_camera.mode, m_attachedCamera.State().activeFollow, m_camera.director.grabbed );
    const KeyboardContextFacts keyboardContextFacts{ !UIBlocksKeyboardBeforeInput,
                                                     SceneState().isSceneMode,
                                                     flyCamera,
                                                     RunCameraModeUsesLauncher( m_camera.mode ),
                                                     RunCameraModeIsAttached( m_camera.mode ),
                                                     m_camera.mode == RunCameraMode::Director,
                                                     m_camera.mode == RunCameraMode::Director || flyCamera,
                                                     !m_replayRuntime.Scrubber().restoreConsumedThisFrame,
                                                     false };
    m_inputRouter.RoutePhase( keyboardBindings,
                              InputActionPhase::PreUi,
                              BuildKeyboardContextMask( keyboardContextFacts ),
                              m_inputActions );
    if ( m_inputActions.Overflowed() )
    {
        SB_FATAL( "InputRouter", "Fixed input action capacity exhausted while routing pre-UI actions." );
    }

    const auto executeSceneLoadRequest = [this]( const SceneLoadRequest& request )
    {
        if ( !request.accepted )
        {
            return false;
        }
        return m_sceneController
            .Load( request,
                   m_config,
                   m_launchOptions,
                   m_defaultCinematicRender,
                   m_startup,
                   m_diagnosticsRuntime,
                   m_runtimeSettings,
                   m_timers,
                   m_systems.assets,
                   *m_systems.workerPool,
                   *m_systems.window,
                   m_inputRouter,
                   m_interaction,
                   m_camera,
                   m_attachedCamera.State(),
                   m_simulation,
                   m_replayRuntime,
                   m_contactAudio,
                   m_UI,
                   m_debug,
                   m_graphicsStress,
                   m_runtimeTools,
                   m_physicsDebugVisualizer,
                   m_renderBackendView,
                   m_renderer,
                   sPerfPass )
            .ok;
    };

    // Invariant: pre-UI consumers receive the router's fixed ordered events.
    // Mode checks below may fail closed after an earlier action mutates state,
    // but no consumer may re-sample its physical key.
    for ( std::size_t index = 0; index < m_inputActions.Count(); ++index )
    {
        const InputActionEvent& event = m_inputActions[index];
        if ( event.phase != InputActionPhase::PreUi )
        {
            continue;
        }

        if ( event.action == RuntimeInputAction::ToggleEditorTool )
        {
            keyboardEditorToolShortcut =
                RunInternal::HandleEditorKeyboardShortcut( event.action,
                                                           event.edge != InputActionEdge::Released,
                                                           event.edge == InputActionEdge::Pressed );
            continue;
        }
        if ( event.edge != InputActionEdge::Pressed )
        {
            continue;
        }

        switch ( event.action )
        {
        case RuntimeInputAction::ToggleEditor:
            // Backtick is captured early but applied after UI command processing.
            keyboardToggleEditorMode = true;
            break;
        case RuntimeInputAction::CycleCameraMode:
            CycleCameraMode();
            break;
        case RuntimeInputAction::ToggleFlyCamera:
        {
            const RunCameraMode passiveMode = SceneState().isSceneMode ? RunCameraMode::Scene : RunCameraMode::Demo;
            ApplyCameraMode( m_camera.mode == RunCameraMode::Inspect ? passiveMode : RunCameraMode::Inspect,
                             event.source );
            break;
        }
        case RuntimeInputAction::ToggleLauncher:
            if ( m_camera.mode == RunCameraMode::Launcher )
            {
                ApplyCameraMode( m_camera.modeBeforeLauncher, event.source );
            }
            else
            {
                m_camera.modeBeforeLauncher =
                    m_camera.mode == RunCameraMode::Manipulator ? RunCameraMode::Inspect : m_camera.mode;
                ApplyCameraMode( RunCameraMode::Launcher, event.source );
            }
            break;
        case RuntimeInputAction::CycleLauncherFireMode:
            if ( RunCameraModeUsesLauncher( m_camera.mode ) )
            {
                m_runtimeTools.RayCastTest().fireMode =
                    m_runtimeTools.RayCastTest().fireMode == RunLauncherFireMode::Laser
                        ? RunLauncherFireMode::Projectile
                        : RunLauncherFireMode::Laser;
            }
            break;
        case RuntimeInputAction::CycleAttachedCameraSubmode:
            if ( RunCameraModeIsAttached( m_camera.mode ) &&
                 m_attachedCamera.CycleMode( m_sceneController.Models(), m_sceneController.Cameras() ) )
            {
                UpdateRuntimeInputModeAfterAction( RuntimeInputAction::CycleAttachedCameraSubmode,
                                                   RuntimeInputActionSource::Keyboard );
            }
            break;
        case RuntimeInputAction::ToggleAttachedCameraPin:
            if ( RunCameraModeIsAttached( m_camera.mode ) )
            {
                const bool activeFollow =
                    m_attachedCamera.TogglePin( m_sceneController.Models(), m_sceneController.Cameras() );
                if ( !activeFollow )
                {
                    ReleaseRuntimeMouseToUI( m_inputRouter, m_runtimeTools.Editor(), m_replayRuntime, m_camera );
                }
                ApplyRuntimeCursorOwnership( m_inputRouter, m_runtimeTools.Editor(), m_replayRuntime );
                UpdateRuntimeInputModeAfterAction( RuntimeInputAction::ToggleAttachedCameraPin,
                                                   RuntimeInputActionSource::Keyboard );
            }
            break;
        case RuntimeInputAction::WriteLauncherReproSnapshot:
#ifdef _DEBUG
            if ( RunCameraModeUsesLauncher( m_camera.mode ) && !m_replayRuntime.Scrubber().restoreConsumedThisFrame )
            {
                const double simulationSeconds = m_timers.simulationTimer.GetTimeSinceLastStart();
                m_runtimeTools.WriteLauncherReproSnapshotWithStatusMessage(
                    { m_sceneController.Models(),
                      m_sceneController.Entities(),
                      &m_sceneController.Cameras(),
                      m_sceneController.Terrain().Get(),
                      m_sceneController.World(),
                      SceneState(),
                      m_sceneController.CurrentPath(),
                      m_launchOptions,
                      m_runtimeSettings,
                      m_config.contactEpsilon,
                      m_config.frictionCoeff,
                      m_debug,
                      m_renderBackendView.renderDiagnostics ? m_renderBackendView.renderDiagnostics->GetRendererName()
                                                            : "DirectX 12",
                      simulationSeconds },
                    m_debug );
            }
#endif
            break;
        case RuntimeInputAction::ToggleDirectorGrab:
            if ( m_camera.mode != RunCameraMode::Director )
            {
                break;
            }
            if ( m_camera.director.grabbed )
            {
                if ( DemoDirectorPlayback::EndGrab( m_camera, m_sceneController.Cameras() ) )
                {
                    ExitFlyModeCamera();
                    ApplyRuntimeCursorOwnership( m_inputRouter, m_runtimeTools.Editor(), m_replayRuntime );
                    UpdateRuntimeInputModeAfterAction( event.action, event.source );
                }
            }
            else if ( DemoDirectorPlayback::BeginGrab( m_camera, m_sceneController.Cameras() ) )
            {
                EnterFlyModeCamera();
                ApplyRuntimeCursorOwnership( m_inputRouter, m_runtimeTools.Editor(), m_replayRuntime );
                UpdateRuntimeInputModeAfterAction( event.action, event.source );
            }
            break;
        case RuntimeInputAction::SetDirectorPhasePose:
            if ( ( m_camera.mode == RunCameraMode::Director ||
                   RunCameraModeUsesFlyControls( m_camera.mode,
                                                 m_attachedCamera.State().activeFollow,
                                                 m_camera.director.grabbed ) ) &&
                 DemoDirectorPlayback::SetCurrentPhasePose( m_camera, m_sceneController.Cameras() ) )
            {
                UpdateRuntimeInputModeAfterAction( event.action, event.source );
            }
            break;
        case RuntimeInputAction::StepDirectorPhase:
            if ( ( m_camera.mode == RunCameraMode::Director ||
                   RunCameraModeUsesFlyControls( m_camera.mode,
                                                 m_attachedCamera.State().activeFollow,
                                                 m_camera.director.grabbed ) ) &&
                 DemoDirectorPlayback::SelectNextPhaseForAuthoring( m_camera, m_sceneController.Cameras() ) )
            {
                UpdateRuntimeInputModeAfterAction( event.action, event.source );
            }
            break;
        case RuntimeInputAction::SaveDirectorShotList:
            if ( ( m_camera.mode == RunCameraMode::Director ||
                   RunCameraModeUsesFlyControls( m_camera.mode,
                                                 m_attachedCamera.State().activeFollow,
                                                 m_camera.director.grabbed ) ) &&
                 DemoDirectorPlayback::SaveShotList( m_camera ) )
            {
                UpdateRuntimeInputModeAfterAction( event.action, event.source );
            }
            break;
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
            HandleDiagnosticsKeyboardShortcut(
                DiagnosticsKeyboardShortcutContext{ m_debug,
                                                    m_camera.trackBallIndex,
                                                    m_sceneController.Models(),
                                                    m_renderBackendView.renderDiagnostics,
                                                    SceneState().isSceneMode,
                                                    m_timers.simulationTimer.GetTimeSinceLastStart() },
                event.action,
                true );
            break;
        case RuntimeInputAction::ToggleUIVisibility:
        case RuntimeInputAction::TogglePerformanceHistogram:
        case RuntimeInputAction::ToggleMemoryOverlay:
        {
            const DiagnosticsUIKeyboardShortcutResult shortcutResult = HandleDiagnosticsUIKeyboardShortcut(
                DiagnosticsUIKeyboardShortcutContext{ m_UI,
                                                      m_debug,
                                                      SceneState(),
                                                      m_diagnosticsRuntime.Capture(),
                                                      m_timers.simulationTimer.GetTotalTime() },
                event.action,
                true );
            if ( shortcutResult.triggered )
            {
                if ( shortcutResult.releaseMouseToUI )
                {
                    ApplyRuntimeCursorOwnership( m_inputRouter, m_runtimeTools.Editor(), m_replayRuntime );
                    ReleaseRuntimeMouseToUI( m_inputRouter, m_runtimeTools.Editor(), m_replayRuntime, m_camera );
                }
                UpdateRuntimeInputModeAfterAction( event.action, event.source );
            }
            break;
        }
        case RuntimeInputAction::NavigateScenePrevious:
        case RuntimeInputAction::NavigateSceneNext:
        {
            const int direction = event.action == RuntimeInputAction::NavigateScenePrevious ? -1 : 1;
            EnterInteractiveSceneRun();
            const int currentSceneBrowserIndex =
                CurrentSceneBrowserIndex( m_sceneController, m_sceneController.Browser() );
            const bool isCinematicTabActive = m_UI.GetActiveTab() == InGameUITab::Cinematic;
            const int cinematicIndex = m_sceneController.AdjacentCinematicModeBrowserIndex(
                direction,
                m_sceneController.Browser().selectedCineModeSceneIndex,
                currentSceneBrowserIndex,
                isCinematicTabActive );
            const bool appliedCinematic =
                cinematicIndex >= 0 &&
                ApplyCinematicModeFromBrowserIndex(
                    SceneRuntimeStyleContext{ m_launchOptions,
                                              SceneState(),
                                              m_sceneController.Browser(),
                                              m_sceneController.Models(),
                                              m_sceneController.Entities(),
                                              m_systems.assets,
                                              RuntimeActiveCinematicConfig( SceneState(), m_config ),
                                              m_defaultCinematicRender },
                    cinematicIndex );
            if ( !appliedCinematic )
            {
                executeSceneLoadRequest(
                    m_sceneController.LoadAdjacentSceneFromBrowser( direction, currentSceneBrowserIndex ) );
            }
            break;
        }
        default:
            break;
        }
    }

    ApplyPostMappedKeyboardShortcutState(
        PostMappedKeyboardShortcutContext{ m_runtimeTools, m_replayRuntime, m_interaction, m_timers },
        keyboardEditorToolShortcut,
        applyEditorPlacementModeToggle,
        [this]() { m_replayRuntime.CancelToolDragState( m_interaction, m_inputRouter ); },
        [this]() { EnterInteractiveSceneRun(); },
        [this]()
        {
            m_replayRuntime.EnterInspectionCamera( &m_sceneController.Cameras(),
                                                   m_camera,
                                                   NormalizeCameraModeForCurrentScene( m_camera.mode ),
                                                   m_interaction,
                                                   m_inputRouter,
                                                   m_runtimeTools.MousePickup() );
        },
        [this]()
        {
            m_replayRuntime.ExitInspectionCamera(
                &m_sceneController.Cameras(),
                m_sceneController.Terrain().Get(),
                m_camera,
                NormalizeCameraModeForCurrentScene( m_replayRuntime.Camera().restoreCameraMode ),
                m_attachedCamera.State().activeFollow,
                m_camera.director.grabbed,
                m_interaction,
                m_inputRouter );
        },
        [this]( WorldInteractionOwner owner, InteractionExitReason reason )
        { SetWorldInteractionOwnerAfterInteractionTransition( owner, reason ); } );
    ReplayRuntime::PathPickInput replayPointerRay;
    replayPointerRay.hasWorldRay = TryBuildMouseWorldRay( replayPointerRay.rayOrigin, replayPointerRay.rayDirection );
    const RuntimeUIFrameResult uiFrameResult = ApplyRuntimeUIFrameCommands(
        RuntimeUIFrameContext{ m_runtimeInput,
                               m_inputRouter,
                               m_camera,
                               m_runtimeTools,
                               m_replayRuntime,
                               replayPointerRay,
                               NormalizeCameraModeForCurrentScene( m_camera.mode ),
                               NormalizeCameraModeForCurrentScene( m_replayRuntime.Camera().restoreCameraMode ),
                               m_attachedCamera,
                               m_interaction,
                               m_timers,
                               m_debug,
                               m_launchOptions,
                               m_runtimeSettings,
                               m_config,
                               SceneState(),
                               m_sceneController,
                               m_systems,
                               m_simulation,
                               m_contactAudio,
                               m_sceneController.World(),
                               m_sceneController.Models(),
                               m_renderBackendView,
                               m_renderDefaults,
                               m_defaultCinematicRender,
                               m_UI,
                               m_startup.gameModelCapacity },
        UIBlocksKeyboardBeforeInput,
        keyboardToggleEditorMode,
        [this]() { return CameraModeEnabledMask(); },
        [this]() { EnterInteractiveSceneRun(); },
        [this]( bool uiUserInteracted ) { DispatchAfterUIKeyboardActions( uiUserInteracted ); },
        [this]( RuntimeInputAction action, RuntimeInputActionSource source )
        { UpdateRuntimeInputModeAfterAction( action, source ); },
        [this]( RunCameraMode mode, RuntimeInputActionSource source ) { ApplyCameraMode( mode, source ); },
        applyEditorPlacementModeChange,
        applyEditorModeToggle,
        applyEditorPlacementModeToggle,
        [this]()
        {
            const ReplayRuntime::SceneTimelineResetInput reset = ReplayRuntime::DescribeSceneTimeline(
                m_sceneController,
                SceneState(),
                m_startup.gameModelCapacity,
                static_cast<uint32_t>( m_launchOptions.generatedObjectTypeOverride ) );
            m_replayRuntime.ResetSceneTimeline(
                reset,
                ReplayRuntime::SceneTimelineResetOwners{
                    m_inputRouter,
                    m_interaction,
                    &m_sceneController.Cameras(),
                    m_sceneController.Terrain().Get(),
                    m_camera,
                    NormalizeCameraModeForCurrentScene( m_replayRuntime.Camera().restoreCameraMode ),
                    m_attachedCamera.State().activeFollow,
                    m_camera.director.grabbed } );
        },
        [this]() { return RunUIStressActions(); },
        [this]( int editorWheelDelta ) { TickEditorViewportAndPlacementScaleInput( editorWheelDelta ); } );
    const ReplayLiveRestoreRequest& restoreRequest = uiFrameResult.replayWorkspace.restoreRequest;
    if ( restoreRequest.kind != ReplayLiveRestoreKind::None )
    {
        const ReplayRuntime::SceneTimelineResetInput timelineReset = ReplayRuntime::DescribeSceneTimeline(
            m_sceneController,
            SceneState(),
            m_startup.gameModelCapacity,
            static_cast<uint32_t>( m_launchOptions.generatedObjectTypeOverride ) );
        ReplaySolverSampleRestoreContext sampleOwners{ m_sceneController.Physics(),
                                                       m_sceneController,
                                                       SceneState(),
                                                       m_runtimeSettings,
                                                       m_debug,
                                                       m_runtimeTools };
        ReplayRuntime::SceneTimelineResetOwners timelineOwners{
            m_inputRouter,
            m_interaction,
            &m_sceneController.Cameras(),
            m_sceneController.Terrain().Get(),
            m_camera,
            NormalizeCameraModeForCurrentScene( m_replayRuntime.Camera().restoreCameraMode ),
            m_attachedCamera.State().activeFollow,
            m_camera.director.grabbed };
        const ReplayRuntime::ReplayRestoreTransaction transaction{ sampleOwners,
                                                                   m_diagnosticsRuntime,
                                                                   timelineReset,
                                                                   timelineOwners };
        const ReplayRuntime::ReplayArtifactTopologyOwners topologyOwners{ m_simulation,
                                                                          m_config,
                                                                          m_systems,
                                                                          m_launchOptions.generatedObjectTypeOverride,
                                                                          m_startup.gameModelCapacity };
        const ReplayRuntime::ReplayLiveRestoreOutcome restoreOutcome =
            m_replayRuntime.ApplyLiveRestoreRequest( transaction, topologyOwners, restoreRequest );
        if ( restoreOutcome.enterInteractive )
        {
            EnterInteractiveSceneRun();
        }
    }
    if ( !uiFrameResult.status.ok )
    {
        // Lane R: a generated-resource rebuild could not prove its GPU drain.
        // Stop this frame and end the run before any later world/input mutation.
        ReportRuntimeInputFailure( uiFrameResult.status );
        std::fflush( stderr );
        m_applicationExit.RequestOwnedFailure( uiFrameResult.status );
        PostQuitMessage( 1 );
        commitPointerPresentation();
        return;
    }
    const bool suppressWorldActionThisFrame = uiFrameResult.suppressWorldActionThisFrame;

    // Editor, replay, and launcher actions share world clicks. UI interaction
    // and capture suppress them so panel controls never mutate the scene.
    const RuntimeMouseEdges& mouseEdges = m_inputRouter.UiSnapshot().mouse;
    const DeviceInputFrame& routedDeviceFrame = m_inputRouter.DeviceFrame();
    const UiInputHitSnapshot& routedUiSnapshot = m_inputRouter.UiSnapshot();
    const RuntimeInteractionFrameInput frameInput{
        SceneState().isScenePhysics,
        routedDeviceFrame.keys.IsDown( VK_SPACE ),
        m_replayRuntime.IsScrubPaused(),
        m_replayRuntime.Scrubber().liveAdvanceHeld,
        mouseEdges.rightDown,
        m_runtimeTools.Editor().viewportLookActive,
        m_replayRuntime.InspectionMouseLookActive( routedDeviceFrame.rightDown,
                                                   routedUiSnapshot.wantsNativeCursor,
                                                   routedUiSnapshot.blocksCameraMouse ),
        false,
        SceneState().timeScale };
    const RuntimeInputSnapshot inputSnapshot =
        m_inputRouter.BuildRuntimeSnapshot( frameInput, suppressWorldActionThisFrame );
    RouteRuntimePointerInput( inputSnapshot, mouseEdges );

    if ( m_UI.BlocksKeyboard() )
    {
        m_interaction.CancelCameraLookGesture();
        InputController::ResetMouseLook( m_camera );
        m_camera.input.Set( InputState::Up, false );
        m_camera.input.Set( InputState::Down, false );
        m_camera.input.Set( InputState::Left, false );
        m_camera.input.Set( InputState::Right, false );
        ApplyRuntimeCursorOwnership( m_inputRouter, m_runtimeTools.Editor(), m_replayRuntime );
    }
    else
    {
        DispatchPostUIKeyboardActions();
        const RuntimeInteractionFramePolicy inputPolicy = m_interaction.BuildFramePolicy( inputSnapshot.frameInput );
        const bool mouseOwnsCursor =
            RuntimeMouseLookOwnsCursor( m_inputRouter, m_runtimeTools.Editor(), m_replayRuntime );
        m_interaction.SyncCameraLookGesture( inputSnapshot, inputPolicy, mouseOwnsCursor );
        const bool cameraMouseLookActive =
            inputPolicy.cameraMouseLookActive && mouseOwnsCursor && inputSnapshot.appFocused;
        if ( cameraMouseLookActive )
        {
            m_inputRouter.RequestNativeCapture();
            m_inputRouter.RequestCursorVisible( false );
        }
        const RuntimeCameraInputFrameResult cameraInputResult = InputController::ApplyCameraInputFrame(
            m_camera,
            RuntimeCameraInputFrameContext{ inputSnapshot.appFocused,
                                            cameraMouseLookActive,
                                            mouseOwnsCursor,
                                            inputPolicy.cameraKeyboardControlsActive,
                                            &m_inputRouter.DeviceFrame() } );
        if ( cameraInputResult.applyCursorOwnership )
        {
            ApplyRuntimeCursorOwnership( m_inputRouter, m_runtimeTools.Editor(), m_replayRuntime );
        }

        // Invariant: persistence samples final UI-mutated values before a
        // same-frame scene reset can replace config. Capture keeps its
        // historical pre-render input checkpoint; automation remains post-render.
        const bool processedDefaults = DrainRenderDefaultRequests();
        const bool processedCapture = DrainCaptureRequests();
        const bool processedScene = m_sceneController.ExecutePending( m_config,
                                                                      m_launchOptions,
                                                                      m_defaultCinematicRender,
                                                                      m_startup,
                                                                      m_diagnosticsRuntime,
                                                                      m_runtimeSettings,
                                                                      m_timers,
                                                                      m_systems.assets,
                                                                      *m_systems.workerPool,
                                                                      *m_systems.window,
                                                                      m_inputRouter,
                                                                      m_interaction,
                                                                      m_camera,
                                                                      m_attachedCamera.State(),
                                                                      m_simulation,
                                                                      m_replayRuntime,
                                                                      m_contactAudio,
                                                                      m_UI,
                                                                      m_debug,
                                                                      m_graphicsStress,
                                                                      m_runtimeTools,
                                                                      m_physicsDebugVisualizer,
                                                                      m_renderBackendView,
                                                                      m_renderer,
                                                                      sPerfPass );
        if ( processedCapture || processedDefaults || processedScene )
        {
            RefreshRuntimeViewModel();
        }
    }
    commitPointerPresentation();
}


bool Run::DrainCaptureRequests()
{
    CaptureController& capture = m_diagnosticsRuntime.Capture();
    if ( capture.PendingScreenshotCount() == 0 )
    {
        return false;
    }

    Rendering::IRenderCaptureBackend* captureBackend = m_renderBackendView.captureBackend;
    if ( !captureBackend )
    {
        // Lane F: startup binds this facet before input can submit capture work.
        SB_FATAL( "Runtime/CaptureController", "Queued screenshot requires an active capture backend" );
    }

    const CaptureRequestBatchResult batch = capture.DrainScreenshotRequests( *captureBackend );
    if ( !batch.status.ok )
    {
        std::fprintf( stderr, "%s: %s\n", batch.status.error.owner, batch.status.error.message );
        std::fflush( stderr );
    }

    for ( std::size_t index = 0; index < batch.savedCount; ++index )
    {
        m_replayRuntime.RecordEvent( ReplayEventKind::OwnerAction,
                                     m_replayRuntime.NextEventFrameIndex(),
                                     0,
                                     static_cast<int32_t>( ReplayOwnerEventCode::CaptureScreenshot ),
                                     0,
                                     0,
                                     0,
                                     0,
                                     batch.saved[index].path );
    }
    return true;
}


bool Run::DrainRenderDefaultRequests()
{
    if ( m_renderDefaults.PendingCount() == 0 )
    {
        return false;
    }

    const RenderDefaultsSaveBatchResult batch =
        m_renderDefaults.DrainAtFrameCheckpoint( m_config.ordinaryRender,
                                                 RuntimeActiveCinematicConfig( SceneState(), m_config ) );
    if ( !batch.status.ok )
    {
        std::fprintf( stderr, "%s: %s\n", batch.status.error.owner, batch.status.error.message );
        std::fflush( stderr );
    }

    for ( std::size_t index = 0; index < batch.savedCount; ++index )
    {
        const ReplayOwnerEventCode code = batch.saved[index] == RenderDefaultsRequestType::Ordinary
                                              ? ReplayOwnerEventCode::RenderSaveOrdinaryDefaults
                                              : ReplayOwnerEventCode::RenderSaveCinematicDefaults;
        m_replayRuntime.RecordEvent( ReplayEventKind::OwnerAction,
                                     m_replayRuntime.NextEventFrameIndex(),
                                     0,
                                     static_cast<int32_t>( code ),
                                     0,
                                     0,
                                     0,
                                     0,
                                     ReplayOwnerEventName( code ) );
    }
    return true;
}


bool SceneController::ExecutePending( EngineConfig& m_config,
                                      RunLaunchOptions& m_launchOptions,
                                      const CinematicRenderConfig& m_defaultCinematicRender,
                                      const RunStartupState& m_startup,
                                      DiagnosticsRuntime& m_diagnosticsRuntime,
                                      RunRuntimeSettings& m_runtimeSettings,
                                      RunTimerState& m_timers,
                                      SkullbonezCore::Assets::AssetSystem& assets,
                                      Threading::WorkerPool& workerPool,
                                      Window& window,
                                      InputRouter& m_inputRouter,
                                      RuntimeInteractionController& m_interaction,
                                      RunCameraState& m_camera,
                                      AttachedCameraState& attachedCamera,
                                      SimulationSystem& m_simulation,
                                      ReplayRuntime& m_replayRuntime,
                                      SkullbonezCore::Runtime::Audio::ContactAudioService& m_contactAudio,
                                      SkullbonezCore::UI::InGameUI& m_UI,
                                      RunDebugState& m_debug,
                                      GraphicsStressController& m_graphicsStress,
                                      RuntimeTools& m_runtimeTools,
                                      Physics::PhysicsDebugVisualizer& m_physicsDebugVisualizer,
                                      const RuntimeRenderBackendView& m_renderBackendView,
                                      RuntimeRenderer& m_renderer,
                                      int& sPerfPass )
{
    SceneController& m_sceneController = *this;
    const auto executeSceneLoadRequest = [&]( const SceneLoadRequest& request )
    {
        if ( !request.accepted )
        {
            return false;
        }
        return m_sceneController
            .Load( request,
                   m_config,
                   m_launchOptions,
                   m_defaultCinematicRender,
                   m_startup,
                   m_diagnosticsRuntime,
                   m_runtimeSettings,
                   m_timers,
                   assets,
                   workerPool,
                   window,
                   m_inputRouter,
                   m_interaction,
                   m_camera,
                   attachedCamera,
                   m_simulation,
                   m_replayRuntime,
                   m_contactAudio,
                   m_UI,
                   m_debug,
                   m_graphicsStress,
                   m_runtimeTools,
                   m_physicsDebugVisualizer,
                   m_renderBackendView,
                   m_renderer,
                   sPerfPass )
            .ok;
    };
    const SceneRequestBatch batch = m_sceneController.TakePendingRequests();
    if ( batch.rejectedTransitionCount > 0 )
    {
        std::fprintf( stderr,
                      "Runtime/SceneController: rejected %zu additional same-frame scene transition(s)\n",
                      batch.rejectedTransitionCount );
        std::fflush( stderr );
    }
    for ( std::size_t requestIndex = 0; requestIndex < batch.count; ++requestIndex )
    {
        const SceneRequest& request = batch.requests[requestIndex];
        bool accepted = false;
        ReplayOwnerEventCode eventCode = ReplayOwnerEventCode::SceneLoadBrowserIndex;
        int eventIndex = request.index;
        const char* eventText = nullptr;

        switch ( request.type )
        {
        case SceneRequestType::LoadBrowserIndex:
            eventCode = ReplayOwnerEventCode::SceneLoadBrowserIndex;
            accepted = executeSceneLoadRequest( m_sceneController.LoadSceneFromBrowserIndex( request.index ) );
            break;
        case SceneRequestType::LoadDemoScene:
            eventCode = ReplayOwnerEventCode::SceneLoadDemo;
            accepted = executeSceneLoadRequest( m_sceneController.LoadDemoSceneFromUI() );
            break;
        case SceneRequestType::ResetCurrentScene:
            eventCode = ReplayOwnerEventCode::SceneReset;
            accepted = executeSceneLoadRequest( m_sceneController.ResetCurrentScene( request.preserveUIState,
                                                                                     request.suppressExitOnComplete,
                                                                                     request.preserveRuntimeState ) );
            break;
        case SceneRequestType::CreateScene:
            eventCode = ReplayOwnerEventCode::SceneCreate;
            eventText = request.text;
            accepted = executeSceneLoadRequest(
                CreateSceneFromUI( SceneRuntimeCreateContext{ m_sceneController, m_sceneController.Browser() },
                                   request.text ) );
            break;
        case SceneRequestType::SaveCurrentDefaults:
            eventCode = ReplayOwnerEventCode::SceneSaveDefaults;
            {
                const SbResult saveResult = m_sceneController.SaveCurrentDefaults(
                    SceneDefaultsSaveView{ m_debug, m_runtimeSettings, m_camera } );
                if ( !saveResult.ok )
                {
                    std::fprintf( stderr, "[%s] %s\n", saveResult.error.owner, saveResult.error.message );
                    std::fflush( stderr );
                }
                accepted = saveResult.ok;
                break;
            }
        }

        // Invariant: replay observes completed owner work. Rejected browser
        // indices, failed loads, invalid create names, and failed writes leave
        // no serialized action that a restore could mistake for applied state.
        if ( accepted )
        {
            m_replayRuntime.RecordEvent( ReplayEventKind::OwnerAction,
                                         m_replayRuntime.NextEventFrameIndex(),
                                         ReplaySceneRequestFlags( request ),
                                         static_cast<int32_t>( eventCode ),
                                         eventIndex,
                                         0,
                                         0,
                                         0,
                                         eventText ? eventText : ReplayOwnerEventName( eventCode ) );
        }
    }
    return batch.count > 0;
}


void Run::MoveCamera( float keyMovementQty, float mouseMovementQty )
{
    const bool hasCameraTravelInput = m_camera.input.Get( InputState::Up ) || m_camera.input.Get( InputState::Down ) ||
                                      m_camera.input.Get( InputState::Left ) || m_camera.input.Get( InputState::Right );
    const bool attachedOrbitOwnsCamera = RunCameraModeIsAttached( m_camera.mode ) &&
                                         m_attachedCamera.State().activeFollow &&
                                         m_attachedCamera.State().submode != AttachedCameraSubmode::RagdollEyes;
    if ( !attachedOrbitOwnsCamera &&
         ( RunCameraModeUsesFlyControls( m_camera.mode,
                                         m_attachedCamera.State().activeFollow,
                                         m_camera.director.grabbed ) ||
           RuntimeMouseLookOwnsCursor( m_inputRouter, m_runtimeTools.Editor(), m_replayRuntime ) ||
           m_runtimeTools.Editor().viewportLookActive || hasCameraTravelInput ) )
    {
        // Shift held = 3x speed
        float speedMult = m_inputRouter.DeviceFrame().keys.IsDown( VK_SHIFT ) ? 3.0f : 1.0f;

        // Mouse look
        if ( ( !m_runtimeTools.Editor().editorModeEnabled || m_runtimeTools.Editor().viewportLookActive ) &&
             ( m_camera.input.xMove != 0 || m_camera.input.yMove != 0 ) )
        {
            m_sceneController.Cameras().RotatePrimary( m_camera.input.xMove * mouseMovementQty,
                                                       m_camera.input.yMove * mouseMovementQty );
        }

        // WASD movement
        if ( m_camera.input.Get( InputState::Up ) )
        {
            m_sceneController.Cameras().MovePrimary( Camera::TravelDirection::Forward, keyMovementQty * speedMult );
        }
        if ( m_camera.input.Get( InputState::Left ) )
        {
            m_sceneController.Cameras().MovePrimary( Camera::TravelDirection::Left, keyMovementQty * speedMult );
        }
        if ( m_camera.input.Get( InputState::Down ) )
        {
            m_sceneController.Cameras().MovePrimary( Camera::TravelDirection::Backward, keyMovementQty * speedMult );
        }
        if ( m_camera.input.Get( InputState::Right ) )
        {
            m_sceneController.Cameras().MovePrimary( Camera::TravelDirection::Right, keyMovementQty * speedMult );
        }

        m_sceneController.Cameras().ApplyPrimaryMovementBuffer();
    }

    // Passive generated-demo camera bounds do not own manual or pinned follow views.
    if ( !RunCameraModeUsesManualControls( m_camera.mode,
                                           m_attachedCamera.State().activeFollow,
                                           m_camera.director.grabbed ) &&
         !m_runtimeTools.Editor().viewportLookActive && !SceneState().isSceneMode )
    {
        Vector3 translatedCameraPosition = m_sceneController.Cameras().GetCameraTranslation();
        float minY = m_sceneController.Terrain().Get()->GetTerrainHeightAt( translatedCameraPosition.x,
                                                                            translatedCameraPosition.z,
                                                                            true ) +
                     m_config.minCameraHeight;
        if ( minY > translatedCameraPosition.y )
        {
            m_sceneController.Cameras().AmmendPrimaryY( minY );
        }
        else if ( translatedCameraPosition.y > m_config.maxCameraHeight )
        {
            m_sceneController.Cameras().AmmendPrimaryY( m_config.maxCameraHeight );
        }
    }
}
