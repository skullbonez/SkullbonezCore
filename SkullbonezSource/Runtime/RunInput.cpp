/*
File: SkullbonezSource/Runtime/RunInput.cpp
Purpose:
  Routes raw keyboard, mouse, and UI commands into runtime state changes.

Mental model:
  RunInput.cpp routes raw keyboard, mouse, and UI commands into runtime state
  changes. As an implementation unit, keep edits anchored on local owner
  boundaries and call direction and on the glossary/invariants below.

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
    RuntimeCommandQueue& runtimeCommands;
    CinematicRenderConfig& defaultCinematicRender;
    SkullbonezCore::UI::InGameUI& ui;
    int gameModelCapacity = 0;
};

struct RuntimeUIFrameResult
{
    SbResult status = SbResult::Success();
    bool suppressWorldActionThisFrame = false;
    int editorUnhandledWheelDelta = 0;
};

template <typename CameraModeEnabledMask,
          typename EnterInteractiveSceneRun,
          typename TickReplayScrubberInput,
          typename TickReplayCauseTreeInput,
          typename TickReplayVelocityEditInput,
          typename DispatchAfterUIKeyboardActions,
          typename UpdateRuntimeInputModeAfterAction,
          typename ApplyCameraMode,
          typename ApplyEditorPlacementModeChange,
          typename ApplyEditorModeToggle,
          typename ApplyEditorPlacementModeToggle,
          typename ResetReplayTimelineForActiveScene,
          typename RunUIStressActions,
          typename TickAttachedCameraOrbitInput,
          typename TickEditorViewportAndPlacementScaleInput>
RuntimeUIFrameResult
ApplyRuntimeUIFrameCommands( const RuntimeUIFrameContext& context,
                             bool suppressWorldActionThisFrame,
                             bool keyboardToggleEditorMode,
                             CameraModeEnabledMask cameraModeEnabledMask,
                             EnterInteractiveSceneRun enterInteractiveSceneRun,
                             TickReplayScrubberInput tickReplayScrubberInput,
                             TickReplayCauseTreeInput tickReplayCauseTreeInput,
                             TickReplayVelocityEditInput tickReplayVelocityEditInput,
                             DispatchAfterUIKeyboardActions dispatchAfterUIKeyboardActions,
                             UpdateRuntimeInputModeAfterAction updateRuntimeInputModeAfterAction,
                             ApplyCameraMode applyCameraMode,
                             ApplyEditorPlacementModeChange applyEditorPlacementModeChange,
                             ApplyEditorModeToggle applyEditorModeToggle,
                             ApplyEditorPlacementModeToggle applyEditorPlacementModeToggle,
                             ResetReplayTimelineForActiveScene resetReplayTimelineForActiveScene,
                             RunUIStressActions runUIStressActions,
                             TickAttachedCameraOrbitInput tickAttachedCameraOrbitInput,
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
    const bool replayScrubberOwnsMouse = tickReplayScrubberInput( windowHandle, context.ui.BlocksCameraMouse() );
    const bool replayCauseTreeOwnsMouse =
        tickReplayCauseTreeInput( context.ui.BlocksCameraMouse() || replayScrubberOwnsMouse,
                                  result.editorUnhandledWheelDelta );
    const bool replayVelocityEditOwnsMouse = tickReplayVelocityEditInput(
        context.ui.BlocksCameraMouse() || replayScrubberOwnsMouse || replayCauseTreeOwnsMouse );
    result.suppressWorldActionThisFrame = result.suppressWorldActionThisFrame || replayScrubberOwnsMouse ||
                                          replayCauseTreeOwnsMouse || replayVelocityEditOwnsMouse;
    context.runtimeInput.BeginFrame( true,
                                     context.ui.BlocksKeyboard(),
                                     context.ui.BlocksCameraMouse() || replayScrubberOwnsMouse ||
                                         replayCauseTreeOwnsMouse || replayVelocityEditOwnsMouse );

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
                                             context.runtimeCommands,
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
                                                    context.systems.terrain.get(),
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
                                                               context.runtimeCommands };
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
        QueueSceneUIRuntimeCommands( context.runtimeCommands, uiCommands.scene );
    RecordSceneRuntimeUIActions( sceneUICommands, recordUIAction );

    const SbResult stressResult = runUIStressActions();
    if ( !stressResult.ok )
    {
        result.status = stressResult;
        return result;
    }

    tickAttachedCameraOrbitInput( result.editorUnhandledWheelDelta );
    tickEditorViewportAndPlacementScaleInput( result.editorUnhandledWheelDelta );
    return result;
}

struct RuntimePointerCameraFrameContext
{
    // Lifetime: borrowed for the final world-input/camera phase of one input
    // frame; the helper does not retain the input snapshot it builds.
    RuntimeInputContext& runtimeInput;
    InputRouter& inputRouter;
    RunCameraState& camera;
    RuntimeInteractionController& interaction;
    SkullbonezCore::UI::InGameUI& ui;
    bool suppressWorldActionThisFrame = false;
};

template <typename BuildRuntimeInputSnapshot,
          typename RouteRuntimePointerInput,
          typename CancelCameraLookGesture,
          typename ApplyCursorOwnership,
          typename DispatchPostUIKeyboardActions,
          typename MouseLookOwnsCursor,
          typename SyncCameraLookGesture,
          typename DrainRuntimeCommands>
void ProcessRuntimePointerCameraFrame( const RuntimePointerCameraFrameContext& context,
                                       BuildRuntimeInputSnapshot buildRuntimeInputSnapshot,
                                       RouteRuntimePointerInput routeRuntimePointerInput,
                                       CancelCameraLookGesture cancelCameraLookGesture,
                                       ApplyCursorOwnership applyCursorOwnership,
                                       DispatchPostUIKeyboardActions dispatchPostUIKeyboardActions,
                                       MouseLookOwnsCursor mouseLookOwnsCursor,
                                       SyncCameraLookGesture syncCameraLookGesture,
                                       DrainRuntimeCommands drainRuntimeCommands )
{
    // Editor, replay, and launcher actions share world clicks. UI interaction
    // and capture suppress them so panel controls never mutate the scene.
    const RuntimeMouseEdges& mouseEdges = context.inputRouter.UiSnapshot().mouse;
    const RuntimeInputSnapshot inputSnapshot =
        buildRuntimeInputSnapshot( mouseEdges, context.suppressWorldActionThisFrame );
    routeRuntimePointerInput( inputSnapshot, mouseEdges );

    if ( context.ui.BlocksKeyboard() )
    {
        cancelCameraLookGesture();
        InputController::ResetMouseLook( context.camera );
        context.camera.input.Set( InputState::Up, false );
        context.camera.input.Set( InputState::Down, false );
        context.camera.input.Set( InputState::Left, false );
        context.camera.input.Set( InputState::Right, false );
        applyCursorOwnership();
        return;
    }

    dispatchPostUIKeyboardActions();

    const RuntimeInteractionFramePolicy inputPolicy = context.interaction.BuildFramePolicy( inputSnapshot.frameInput );
    const bool mouseOwnsCursor = mouseLookOwnsCursor();
    syncCameraLookGesture( inputSnapshot, inputPolicy, mouseOwnsCursor );
    const bool cameraMouseLookActive = inputPolicy.cameraMouseLookActive && mouseOwnsCursor && inputSnapshot.appFocused;
    if ( cameraMouseLookActive )
    {
        context.inputRouter.RequestNativeCapture();
        context.inputRouter.RequestCursorVisible( false );
    }
    const bool cameraKeyboardControlsActive = inputPolicy.cameraKeyboardControlsActive;
    const RuntimeCameraInputFrameResult cameraInputResult =
        InputController::ApplyCameraInputFrame( context.camera,
                                                RuntimeCameraInputFrameContext{ inputSnapshot.appFocused,
                                                                                cameraMouseLookActive,
                                                                                mouseOwnsCursor,
                                                                                cameraKeyboardControlsActive,
                                                                                &context.inputRouter.DeviceFrame() } );
    if ( cameraInputResult.applyCursorOwnership )
    {
        applyCursorOwnership();
    }
    drainRuntimeCommands();
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
    const DeviceInputFrame& deviceFrame = m_inputRouter.DeviceFrame();
    const UiInputHitSnapshot& uiSnapshot = m_inputRouter.UiSnapshot();

    RuntimeInputSnapshot snapshot;
    snapshot.appFocused = deviceFrame.appFocused;
    snapshot.uiBlocksKeyboard = uiSnapshot.blocksKeyboard;
    snapshot.uiBlocksMouse = uiSnapshot.blocksCameraMouse;

    if ( deviceFrame.hasClientPosition )
    {
        snapshot.pointer.clientX = deviceFrame.clientX;
        snapshot.pointer.clientY = deviceFrame.clientY;
    }
    snapshot.pointer.leftDown = mouseEdges.leftDown;
    snapshot.pointer.leftPressed = mouseEdges.leftPressed;
    snapshot.pointer.leftReleased = mouseEdges.leftReleased;
    snapshot.pointer.rightDown = mouseEdges.rightDown;
    snapshot.pointer.rightPressed = mouseEdges.rightPressed;
    snapshot.pointer.rightReleased = mouseEdges.rightReleased;
    snapshot.pointer.controlDown = deviceFrame.keys.IsDown( VK_CONTROL );
    snapshot.pointer.shiftDown = deviceFrame.keys.IsDown( VK_SHIFT );
    snapshot.pointer.uiWantsNativeMouseCursor = uiSnapshot.wantsNativeCursor;
    snapshot.pointer.uiBlocksCameraMouse = uiSnapshot.blocksCameraMouse;
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
                                      deviceFrame.keys.IsDown( VK_SPACE ),
                                      m_replayRuntime.IsScrubPaused(),
                                      m_replayRuntime.Scrubber().liveAdvanceHeld,
                                      mouseEdges.rightDown,
                                      m_runtimeTools.Editor().viewportLookActive,
                                      m_replayRuntime.InspectionMouseLookActive( deviceFrame.rightDown,
                                                                                 uiSnapshot.wantsNativeCursor,
                                                                                 uiSnapshot.blocksCameraMouse ),
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
        TryPickReplayPathTargetFromMouse( additiveReplayPick, !additiveReplayPick );
        consumedWorldClick = true;
    }

    if ( !consumedWorldClick && RunCameraModeUsesLauncher( m_camera.mode ) && leftPressed && !suppressWorldAction &&
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
        m_inputRouter.ReleaseNativeCapture();
    }

    m_replayRuntime.Scrubber().dragging = false;
    m_replayRuntime.Scrubber().mouseCaptured = false;
    m_replayRuntime.Prediction().ui.horizonDragging = false;
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
           m_replayRuntime.Prediction().enabled || m_replayRuntime.Prediction().ui.horizonDragging ||
           m_replayRuntime.Prediction().build.building || m_replayRuntime.VelocityEdit().enabled ||
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
    m_replayRuntime.PathVisualizer().pastPathHovered = false;

    m_replayRuntime.ClearCameraFocusForRestore();
    ExitReplayInspectionCamera();
    m_replayRuntime.ClearPathVisualizerState();
    m_replayRuntime.Prediction().enabled = false;
    m_replayRuntime.Prediction().ui.checkboxHovered = false;
    m_replayRuntime.Prediction().ui.decreaseHovered = false;
    m_replayRuntime.Prediction().ui.increaseHovered = false;
    m_replayRuntime.Prediction().ui.horizonHovered = false;
    m_replayRuntime.Prediction().ui.horizonDragging = false;
    m_replayRuntime.ClearPredictionCache();

    m_replayRuntime.VelocityEdit() = RunReplayVelocityEditState{};
    m_replayRuntime.CauseTree().hoveredRow = -1;
    m_replayRuntime.CauseTree().selectedRow = -1;
    m_replayRuntime.CauseTree().draggingWindow = false;
    m_replayRuntime.CauseTree().resizingWindow = false;
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

        const PhysicsBodyStore& bodyStore = m_cGameModelCollection.BodyStore();
        const ColliderStore& colliderStore = m_cGameModelCollection.Colliders();
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


bool Run::TryResolveAttachedCameraTarget( int& outModelIndex )
{
    if ( AttachedCameraController::TryResolveTargetIdentity( m_cGameModelCollection,
                                                             m_attachedCamera.target,
                                                             outModelIndex ) )
    {
        return true;
    }

    AttachedCameraController::ClearTarget( m_attachedCamera );
    return false;
}


void Run::SetAttachedCameraTarget( int modelIndex )
{
    AttachedCameraTargetSelection selection;
    if ( !AttachedCameraController::SelectTarget( m_cGameModelCollection, m_attachedCamera, modelIndex, selection ) )
    {
        return;
    }

    RuntimeInteractionCommand command;
    command.type = RuntimeInteractionCommandType::SetEditorSelection;
    command.modelIndex = modelIndex;
    command.body = m_attachedCamera.target.body;
    command.collider = m_attachedCamera.target.collider;
    command.selectionScope = RuntimeInteractionSelectionScope::Inspect;
    command.claimSelectionOwner = false;
    ExecuteRuntimeInteractionCommand( command );
    CaptureAttachedCameraFixedOffsetFromCurrentPose( m_attachedCamera, m_systems.cameras, selection.physics );
    ApplyCursorOwnership();
}


void Run::SeedAttachedCameraTargetFromSelection()
{
    AttachedCameraPhysicsTarget currentState;
    if ( AttachedCameraController::TryResolvePhysicsTarget( m_cGameModelCollection,
                                                            m_attachedCamera.target,
                                                            currentState ) )
    {
        CaptureAttachedCameraFixedOffsetFromCurrentPose( m_attachedCamera, m_systems.cameras, currentState );
        m_attachedCamera.activeFollow = true;
        ApplyCursorOwnership();
        return;
    }

    int seedIndex = -1;
    const RunReplayPathVisualizerState& path = m_replayRuntime.PathVisualizer();
    const PhysicsBodyStore& bodyStore = m_cGameModelCollection.BodyStore();
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
        request.bodyStore = &m_cGameModelCollection.BodyStore();
        request.colliderStore = &m_cGameModelCollection.Colliders();
        request.rayOrigin = rayOrigin;
        request.rayDirection = rayDirection;

        if ( RuntimePickService::TryPickModel( request, result ) )
        {
            SetAttachedCameraTarget( result.modelIndex );
        }
        else
        {
            AttachedCameraController::ClearTarget( m_attachedCamera );
        }
    }
    else
    {
        AttachedCameraController::ClearTarget( m_attachedCamera );
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


void Run::CycleAttachedCameraSubmode()
{
    if ( !RunCameraModeIsAttached( m_camera.mode ) )
    {
        return;
    }
    AttachedCameraPhysicsTarget targetState;
    bool shouldCaptureFixedOffset = false;
    if ( !AttachedCameraController::CycleSubmode( m_cGameModelCollection,
                                                  m_attachedCamera,
                                                  targetState,
                                                  shouldCaptureFixedOffset ) )
    {
        return;
    }

    if ( shouldCaptureFixedOffset )
    {
        CaptureAttachedCameraFixedOffsetFromCurrentPose( m_attachedCamera, m_systems.cameras, targetState );
    }
    UpdateRuntimeInputModeAfterAction( RuntimeInputAction::CycleAttachedCameraSubmode,
                                       RuntimeInputActionSource::Keyboard );
}


void Run::ToggleAttachedCameraPin()
{
    if ( !RunCameraModeIsAttached( m_camera.mode ) )
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
            CaptureAttachedCameraFixedOffsetFromCurrentPose( m_attachedCamera, m_systems.cameras, targetState );
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
    if ( !RunCameraModeIsAttached( m_camera.mode ) || !m_attachedCamera.activeFollow ||
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
        CaptureAttachedCameraOrbitFromCurrentPose( m_attachedCamera, m_systems.cameras, targetState );
    }

    if ( AttachedCameraController::ApplyOrbitWheel( m_attachedCamera, targetState, unhandledWheelDelta ) )
    {
        EnterInteractiveSceneRun();
    }
}


void Run::TickAttachedCamera()
{
    if ( !RunCameraModeIsAttached( m_camera.mode ) || !m_attachedCamera.activeFollow || !m_systems.cameras )
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

    const bool wasFlyMode =
        RunCameraModeUsesFlyControls( m_camera.mode, m_attachedCamera.activeFollow, m_camera.director.grabbed );
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

    const bool isFlyMode =
        RunCameraModeUsesFlyControls( m_camera.mode, m_attachedCamera.activeFollow, m_camera.director.grabbed );
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
    const DeviceInputFrame& deviceFrame = m_inputRouter.DeviceFrame();
    if ( !deviceFrame.appFocused )
    {
        return false;
    }

    if ( m_UI.BlocksCameraMouse() )
    {
        return false;
    }

    if ( m_runtimeTools.Editor().editorModeEnabled )
    {
        return m_runtimeTools.Editor().viewportLookActive || deviceFrame.rightDown;
    }

    if ( m_replayRuntime.InspectionActive() )
    {
        return m_replayRuntime.InspectionMouseLookActive( deviceFrame.rightDown,
                                                          m_UI.WantsNativeMouseCursor(),
                                                          m_UI.BlocksCameraMouse() ) ||
               deviceFrame.rightDown;
    }

    return deviceFrame.rightDown;
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
    m_inputRouter.RequestCursorVisible( !ShouldHideNativeCursor() );
}


void Run::ReleaseMouseToUI()
{
    if ( !MouseLookOwnsCursor() )
    {
        m_inputRouter.ReleaseNativeCapture();
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
        m_inputRouter.RequestCursorVisible( false );
    }
    else
    {
        ReleaseMouseToUI();
        m_inputRouter.RequestCursorVisible( true );
    }
    InputController::ResetMouseLook( m_camera );
}


void Run::ExitFlyModeCamera()
{
    // Exiting fly mode restores terrain bounds, the camera-cycle clock, and
    // the stock Windows cursor.
    uint32_t activeCam = SceneState().isSceneMode ? m_systems.cameras->GetSelectedCameraName() : CAMERA_FREE;
    m_systems.cameras->SetCameraXZBounds( activeCam, m_systems.terrain->GetXZBounds() );
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
    CancelCameraLookGesture();
    CancelReplayToolDragState();
    m_inputRouter.CancelPointerPresentation();
    if ( m_replayRuntime.ResetScrubberState() )
    {
        ExitReplayInspectionCamera();
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
    CancelMousePickup();
    if ( m_replayRuntime.CauseTree().draggingWindow || m_replayRuntime.CauseTree().resizingWindow )
    {
        m_inputRouter.ReleaseNativeCapture();
        m_replayRuntime.CauseTree().draggingWindow = false;
        m_replayRuntime.CauseTree().resizingWindow = false;
    }
    RunInternal::ResetEditorUnfocusedInputState( { m_runtimeTools.Editor(), m_cGameModelCollection, m_interaction } );
    InputController::ResetUnfocusedInput( m_camera );
    InputController::BeginFrame( m_runtimeInput,
                                 BuildRuntimeInputModeState( m_camera.mode,
                                                             m_runtimeTools.Editor(),
                                                             m_attachedCamera.activeFollow,
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
        RunCameraModeUsesFlyControls( m_camera.mode, m_attachedCamera.activeFollow, m_camera.director.grabbed );
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

    const RunInternal::EditorSaveHotkeyContext editorSaveHotkeyContext{ m_cGameModelCollection,
                                                                        SceneState(),
                                                                        m_cWorldEnvironment,
                                                                        *m_systems.cameras,
                                                                        m_runtimeCommands };
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
            m_runtimeCommands.Push( RuntimeCommand{ RuntimeCommandType::ResetCurrentScene } );
            break;
        case RuntimeInputAction::ResetSceneFromBackspace:
            if ( SceneState().isSceneMode )
            {
                // Backspace is only a scene-mode reset alias; generated demos keep
                // the key free for future non-scene tools.
                m_runtimeCommands.Push( RuntimeCommand{ RuntimeCommandType::ResetCurrentScene } );
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
        RunCameraModeUsesFlyControls( m_camera.mode, m_attachedCamera.activeFollow, m_camera.director.grabbed );
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
            ApplyCursorOwnership();
            ReleaseMouseToUI();
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
    auto completeEditorPlacementModeTransition =
        [this]( RuntimeInputActionSource source, const RunInternal::EditorPlacementModeChangeResult& placementMode )
    {
        SetWorldInteractionOwnerAfterInteractionTransition( placementMode.worldOwner,
                                                            InteractionExitReason::EnterEdit );
        ReleaseMouseToUI();
        ApplyCursorOwnership();
        UpdateRuntimeInputModeAfterAction( RuntimeInputAction::ToggleEditorTool, source );
    };
    auto applyEditorPlacementModeChange = [this, &completeEditorPlacementModeTransition](
                                              RuntimeInputActionSource source,
                                              bool enabled,
                                              bool clearManipulation )
    {
        EnterInteractiveSceneRun();
        const RunInternal::EditorPlacementModeChangeResult placementMode =
            RunInternal::SetEditorPlacementMode( { m_runtimeTools.Editor(), m_cGameModelCollection, m_interaction },
                                                 enabled,
                                                 clearManipulation );
        completeEditorPlacementModeTransition( source, placementMode );
    };
    auto applyEditorPlacementModeToggle =
        [this, &completeEditorPlacementModeTransition]( RuntimeInputActionSource source )
    {
        EnterInteractiveSceneRun();
        const RunInternal::EditorPlacementModeChangeResult placementMode = RunInternal::ToggleEditorPlacementMode(
            { m_runtimeTools.Editor(), m_cGameModelCollection, m_interaction } );
        completeEditorPlacementModeTransition( source, placementMode );
    };
    auto applyEditorModeToggle = [this]( RuntimeInputActionSource source )
    {
        EnterInteractiveSceneRun();
        const bool enteringEditor = !m_runtimeTools.Editor().editorModeEnabled;
        if ( enteringEditor )
        {
            ApplyRuntimeInteractionTransitionCleanup( m_interaction.EnterEdit() );
            const bool wasFlyMode =
                RunCameraModeUsesFlyControls( m_camera.mode, m_attachedCamera.activeFollow, m_camera.director.grabbed );
            RunInternal::EnterEditorModeState( { m_runtimeTools.Editor(), m_cGameModelCollection, m_interaction },
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
        }
        else
        {
            const RunCameraMode restoreMode =
                NormalizeCameraModeForCurrentScene( m_runtimeTools.Editor().restoreCameraModeAfterEditor );
            ApplyRuntimeInteractionTransitionCleanup( EnterInteractionForCameraMode( restoreMode ) );
            const bool wasFlyMode =
                RunCameraModeUsesFlyControls( m_camera.mode, m_attachedCamera.activeFollow, m_camera.director.grabbed );
            RunInternal::ExitEditorModeState( { m_runtimeTools.Editor(), m_cGameModelCollection, m_interaction } );
            SetCameraModeLabelAfterInteractionTransition( restoreMode );
            if ( wasFlyMode && !RunCameraModeUsesFlyControls( m_camera.mode,
                                                              m_attachedCamera.activeFollow,
                                                              m_camera.director.grabbed ) )
            {
                ExitFlyModeCamera();
            }
            else
            {
                InputController::ResetMouseLook( m_camera );
            }
        }
        ApplyCursorOwnership();
        UpdateRuntimeInputModeAfterAction( RuntimeInputAction::ToggleEditor, source );
    };
    const bool flyCamera =
        RunCameraModeUsesFlyControls( m_camera.mode, m_attachedCamera.activeFollow, m_camera.director.grabbed );
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

    SceneRuntimeControlExecutionContext sceneControlContext{
        this,
        []( void* context ) { static_cast<Run*>( context )->EnterInteractiveSceneRun(); },
        []( void* context, int index, bool preserveUIState, bool suppressExitOnComplete, bool preserveRuntimeState )
            -> bool
        {
            return static_cast<Run*>( context )
                ->LoadScene( index, preserveUIState, suppressExitOnComplete, preserveRuntimeState )
                .ok;
        },
        SceneState(),
        m_diagnosticsRuntime.Capture().Screenshot().isScreenshotAndExit,
        SceneRuntimeStyleContext{ m_launchOptions,
                                  SceneState(),
                                  m_sceneController.Browser(),
                                  m_cGameModelCollection,
                                  m_systems.assets,
                                  RuntimeActiveCinematicConfig( SceneState(), m_config ),
                                  m_defaultCinematicRender },
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
            if ( RunCameraModeIsAttached( m_camera.mode ) )
            {
                CycleAttachedCameraSubmode();
            }
            break;
        case RuntimeInputAction::ToggleAttachedCameraPin:
            if ( RunCameraModeIsAttached( m_camera.mode ) )
            {
                ToggleAttachedCameraPin();
            }
            break;
        case RuntimeInputAction::WriteLauncherReproSnapshot:
#ifdef _DEBUG
            if ( RunCameraModeUsesLauncher( m_camera.mode ) && !m_replayRuntime.Scrubber().restoreConsumedThisFrame )
            {
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
                if ( DemoDirectorPlayback::EndGrab( m_camera, m_systems ) )
                {
                    ExitFlyModeCamera();
                    ApplyCursorOwnership();
                    UpdateRuntimeInputModeAfterAction( event.action, event.source );
                }
            }
            else if ( DemoDirectorPlayback::BeginGrab( m_camera, m_systems ) )
            {
                EnterFlyModeCamera();
                ApplyCursorOwnership();
                UpdateRuntimeInputModeAfterAction( event.action, event.source );
            }
            break;
        case RuntimeInputAction::SetDirectorPhasePose:
            if ( ( m_camera.mode == RunCameraMode::Director ||
                   RunCameraModeUsesFlyControls( m_camera.mode,
                                                 m_attachedCamera.activeFollow,
                                                 m_camera.director.grabbed ) ) &&
                 DemoDirectorPlayback::SetCurrentPhasePose( m_camera, m_systems ) )
            {
                UpdateRuntimeInputModeAfterAction( event.action, event.source );
            }
            break;
        case RuntimeInputAction::StepDirectorPhase:
            if ( ( m_camera.mode == RunCameraMode::Director ||
                   RunCameraModeUsesFlyControls( m_camera.mode,
                                                 m_attachedCamera.activeFollow,
                                                 m_camera.director.grabbed ) ) &&
                 DemoDirectorPlayback::SelectNextPhaseForAuthoring( m_camera, m_systems ) )
            {
                UpdateRuntimeInputModeAfterAction( event.action, event.source );
            }
            break;
        case RuntimeInputAction::SaveDirectorShotList:
            if ( ( m_camera.mode == RunCameraMode::Director ||
                   RunCameraModeUsesFlyControls( m_camera.mode,
                                                 m_attachedCamera.activeFollow,
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
                                                    m_cGameModelCollection,
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
                    ApplyCursorOwnership();
                    ReleaseMouseToUI();
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
            if ( !ExecuteSceneRuntimeControlAction( sceneControlContext,
                                                    m_sceneCoordinator.ApplyAdjacentCinematicMode(
                                                        direction,
                                                        m_sceneController.Browser().paths,
                                                        m_sceneController.Browser().selectedCineModeSceneIndex,
                                                        currentSceneBrowserIndex,
                                                        isCinematicTabActive ) ) )
            {
                ExecuteSceneRuntimeControlAction(
                    sceneControlContext,
                    m_sceneCoordinator.LoadAdjacentSceneFromBrowser( direction,
                                                                     m_sceneController.Browser().paths,
                                                                     currentSceneBrowserIndex ) );
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
        [this]() { CancelReplayToolDragState(); },
        [this]() { EnterInteractiveSceneRun(); },
        [this]() { EnterReplayInspectionCamera(); },
        [this]() { ExitReplayInspectionCamera(); },
        [this]( WorldInteractionOwner owner, InteractionExitReason reason )
        { SetWorldInteractionOwnerAfterInteractionTransition( owner, reason ); } );
    const RuntimeUIFrameResult uiFrameResult = ApplyRuntimeUIFrameCommands(
        RuntimeUIFrameContext{ m_runtimeInput,
                               m_inputRouter,
                               m_camera,
                               m_runtimeTools,
                               m_replayRuntime,
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
                               m_cWorldEnvironment,
                               m_cGameModelCollection,
                               m_renderBackendView,
                               m_runtimeCommands,
                               m_defaultCinematicRender,
                               m_UI,
                               m_startup.gameModelCapacity },
        UIBlocksKeyboardBeforeInput,
        keyboardToggleEditorMode,
        [this]() { return CameraModeEnabledMask(); },
        [this]() { EnterInteractiveSceneRun(); },
        [this]( HWND window, bool blocksCameraMouse ) { return TickReplayScrubberInput( window, blocksCameraMouse ); },
        [this]( bool blocksCameraMouse, int editorWheelDelta )
        { return TickReplayCauseTreeInput( blocksCameraMouse, editorWheelDelta ); },
        [this]( bool blocksCameraMouse ) { return TickReplayVelocityEditInput( blocksCameraMouse ); },
        [this]( bool uiUserInteracted ) { DispatchAfterUIKeyboardActions( uiUserInteracted ); },
        [this]( RuntimeInputAction action, RuntimeInputActionSource source )
        { UpdateRuntimeInputModeAfterAction( action, source ); },
        [this]( RunCameraMode mode, RuntimeInputActionSource source ) { ApplyCameraMode( mode, source ); },
        applyEditorPlacementModeChange,
        applyEditorModeToggle,
        applyEditorPlacementModeToggle,
        [this]() { ResetReplayTimelineForActiveScene(); },
        [this]() { return RunUIStressActions(); },
        [this]( int editorWheelDelta ) { TickAttachedCameraOrbitInput( editorWheelDelta ); },
        [this]( int editorWheelDelta ) { TickEditorViewportAndPlacementScaleInput( editorWheelDelta ); } );
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

    ProcessRuntimePointerCameraFrame(
        RuntimePointerCameraFrameContext{ m_runtimeInput,
                                          m_inputRouter,
                                          m_camera,
                                          m_interaction,
                                          m_UI,
                                          suppressWorldActionThisFrame },
        [this]( const RuntimeMouseEdges& mouseEdges, bool suppressWorldAction )
        { return BuildRuntimeInputSnapshot( mouseEdges, suppressWorldAction ); },
        [this]( const RuntimeInputSnapshot& inputSnapshot, const RuntimeMouseEdges& mouseEdges )
        { RouteRuntimePointerInput( inputSnapshot, mouseEdges ); },
        [this]() { CancelCameraLookGesture(); },
        [this]() { ApplyCursorOwnership(); },
        [this]() { DispatchPostUIKeyboardActions(); },
        [this]() { return MouseLookOwnsCursor(); },
        [this]( const RuntimeInputSnapshot& inputSnapshot,
                const RuntimeInteractionFramePolicy& inputPolicy,
                bool mouseLookOwnsCursor )
        { SyncCameraLookGesture( inputSnapshot, inputPolicy, mouseLookOwnsCursor ); },
        [this]() { DrainRuntimeCommands(); } );
    commitPointerPresentation();
}


bool Run::DrainRuntimeCommands()
{
    SceneRuntimeControlExecutionContext sceneControlContext{
        this,
        []( void* context ) { static_cast<Run*>( context )->EnterInteractiveSceneRun(); },
        []( void* context, int index, bool preserveUIState, bool suppressExitOnComplete, bool preserveRuntimeState )
            -> bool
        {
            return static_cast<Run*>( context )
                ->LoadScene( index, preserveUIState, suppressExitOnComplete, preserveRuntimeState )
                .ok;
        },
        SceneState(),
        m_diagnosticsRuntime.Capture().Screenshot().isScreenshotAndExit,
        SceneRuntimeStyleContext{ m_launchOptions,
                                  SceneState(),
                                  m_sceneController.Browser(),
                                  m_cGameModelCollection,
                                  m_systems.assets,
                                  RuntimeActiveCinematicConfig( SceneState(), m_config ),
                                  m_defaultCinematicRender },
    };
    bool processed = false;
    RuntimeCommand command;
    while ( m_runtimeCommands.TryPop( command ) )
    {
        processed = true;
        switch ( command.type )
        {
        case RuntimeCommandType::LoadSceneIndex:
            ExecuteSceneRuntimeControlAction(
                sceneControlContext,
                m_sceneCoordinator.LoadSceneFromBrowserIndex( command.index, m_sceneController.Browser().paths ) );
            break;
        case RuntimeCommandType::LoadDemoScene:
            ExecuteSceneRuntimeControlAction( sceneControlContext, m_sceneCoordinator.LoadDemoSceneFromUI() );
            break;
        case RuntimeCommandType::ResetCurrentScene:
            EnterInteractiveSceneRun();
            ExecuteSceneRuntimeControlAction( sceneControlContext,
                                              m_sceneCoordinator.ResetCurrentScene( command.preserveUIState,
                                                                                    command.suppressExitOnComplete,
                                                                                    command.preserveRuntimeState ) );
            break;
        case RuntimeCommandType::CreateScene:
            ExecuteSceneRuntimeControlAction(
                sceneControlContext,
                CreateSceneFromUI( SceneRuntimeCreateContext{ m_sceneController, m_sceneController.Browser() },
                                   command.text.c_str() ) );
            break;
        case RuntimeCommandType::SaveScreenshot:
            if ( !command.text.empty() )
            {
                const SbResult captureResult = SaveScreenshot( command.text.c_str() );
                if ( !captureResult.ok )
                {
                    fprintf( stderr, "%s: %s\n", captureResult.error.owner, captureResult.error.message );
                    fflush( stderr );
                }
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
            if ( !ExecuteSceneRuntimeControlAction(
                     sceneControlContext,
                     m_sceneCoordinator.AdvanceScene( m_diagnosticsRuntime.PerfTestActive(),
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
    const bool attachedOrbitOwnsCamera = RunCameraModeIsAttached( m_camera.mode ) && m_attachedCamera.activeFollow &&
                                         m_attachedCamera.submode != AttachedCameraSubmode::RagdollEyes;
    if ( !attachedOrbitOwnsCamera &&
         ( RunCameraModeUsesFlyControls( m_camera.mode, m_attachedCamera.activeFollow, m_camera.director.grabbed ) ||
           MouseLookOwnsCursor() || m_runtimeTools.Editor().viewportLookActive || hasCameraTravelInput ) )
    {
        // Shift held = 3x speed
        float speedMult = m_inputRouter.DeviceFrame().keys.IsDown( VK_SHIFT ) ? 3.0f : 1.0f;

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
    if ( !RunCameraModeUsesManualControls( m_camera.mode, m_attachedCamera.activeFollow, m_camera.director.grabbed ) &&
         !m_runtimeTools.Editor().viewportLookActive && !SceneState().isSceneMode )
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
