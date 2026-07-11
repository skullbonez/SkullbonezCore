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
    const RuntimeInputSnapshot& runtimeSnapshot = inputRouter.RuntimeSnapshot();
    const UiInputHitSnapshot& uiSnapshot = inputRouter.UiSnapshot();
    PointerPresentationPolicyInput input;
    input.editorModeEnabled = editor.editorModeEnabled;
    input.editorViewportLookActive = editor.viewportLookActive;
    input.editorPlacementModeEnabled = editor.placementModeEnabled;
    input.editorPlacementPreviewVisible = editor.placementPreviewVisible;
    input.replayInspectionActive = replayRuntime.InspectionActive();
    input.replayInspectionLookActive =
        input.replayInspectionActive && replayRuntime.InspectionMouseLookActive( runtimeSnapshot.pointer.rightDown,
                                                                                 uiSnapshot.wantsNativeCursor,
                                                                                 uiSnapshot.blocksCameraMouse );
    return inputRouter.EvaluatePointerPresentation( input );
}


// Concept: passive camera vocabulary is scene-relative. Authored scenes expose
// Scene while generated demos expose Demo only when a trackable model exists.
RunCameraMode NormalizeRuntimeCameraMode( RunCameraMode mode, bool authoredScene, uint32_t enabledMask )
{
    if ( authoredScene )
    {
        return mode == RunCameraMode::Demo ? RunCameraMode::Scene : mode;
    }
    const bool demoAvailable = ( enabledMask & ( 1u << static_cast<int>( RunCameraMode::Demo ) ) ) != 0;
    if ( mode == RunCameraMode::Scene )
    {
        return demoAvailable ? RunCameraMode::Demo : RunCameraMode::Inspect;
    }
    return mode == RunCameraMode::Demo && !demoAvailable ? RunCameraMode::Inspect : mode;
}


uint32_t RuntimeCameraModeEnabledMask( const SceneController& sceneController )
{
    const bool authoredScene = sceneController.State().isSceneMode;
    const bool demoAvailable = !authoredScene && sceneController.Models().SceneEntityCount() > 0;
    uint32_t mask = 0;
    mask |= demoAvailable ? 1u << static_cast<int>( RunCameraMode::Demo ) : 0u;
    mask |= authoredScene ? 1u << static_cast<int>( RunCameraMode::Scene ) : 0u;
    mask |= 1u << static_cast<int>( RunCameraMode::Inspect );
    mask |= 1u << static_cast<int>( RunCameraMode::Attach );
    mask |= 1u << static_cast<int>( RunCameraMode::Launcher );
    mask |= 1u << static_cast<int>( RunCameraMode::Manipulator );
    mask |= 1u << static_cast<int>( RunCameraMode::Director );
    return mask;
}


void EnterFlyModeCamera( InputRouter& inputRouter,
                         RunCameraState& camera,
                         SkullbonezCore::Environment::CameraCollection& cameras,
                         bool authoredScene,
                         const RunEditorPlacementState& editor,
                         const ReplayRuntime& replayRuntime )
{
    // Why: generated demos snap to CAMERA_FREE; authored scenes keep their
    // selected camera so manual controls continue from the visible pose.
    if ( !authoredScene )
    {
        cameras.SelectCamera( CAMERA_FREE, true );
    }
    camera.cameraTime = 0.0f;
    XZBounds unbounded;
    unbounded.m_xMin = -99999.9f;
    unbounded.m_xMax = 99999.9f;
    unbounded.m_zMin = -99999.9f;
    unbounded.m_zMax = 99999.9f;
    const uint32_t activeCamera = authoredScene ? cameras.GetSelectedCameraName() : CAMERA_FREE;
    cameras.SetCameraXZBounds( activeCamera, unbounded );

    const PointerPresentationPolicy presentation =
        EvaluateRuntimePointerPresentation( inputRouter, editor, replayRuntime );
    if ( presentation.hideNativeCursor )
    {
        inputRouter.RequestCursorVisible( false );
    }
    else
    {
        inputRouter.ReleasePointerToUi( presentation );
        inputRouter.RequestCursorVisible( true );
    }
    InputController::ResetMouseLook( camera );
}


void ExitFlyModeCamera( InputRouter& inputRouter,
                        RunCameraState& camera,
                        SkullbonezCore::Environment::CameraCollection& cameras,
                        SkullbonezCore::Geometry::Terrain& terrain,
                        bool authoredScene )
{
    const uint32_t activeCamera = authoredScene ? cameras.GetSelectedCameraName() : CAMERA_FREE;
    cameras.SetCameraXZBounds( activeCamera, terrain.GetXZBounds() );
    inputRouter.RequestCursorVisible( true );
    camera.cameraTime = 0.0f;
    InputController::ResetMouseLook( camera );
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

struct RuntimeUIFrameResult
{
    SbResult status = SbResult::Success();
    ReplayRuntime::ReplayWorkspaceOutput replayWorkspace;
    InGameUICommands commands;
    bool suppressWorldActionThisFrame = false;
    bool frameActive = false;
    bool enterInteractiveScene = false;
    int editorUnhandledWheelDelta = 0;
};

// Concept: UI sampling and replay workspace arbitration publish the post-UI
// frame before mapped keyboard commands run. The returned commands are fixed
// value records; no callback retains access to the application shell.
RuntimeUIFrameResult BeginRuntimeUIFrame( RuntimeInputContext& runtimeInput,
                                          InputRouter& inputRouter,
                                          RunCameraState& camera,
                                          RuntimeTools& runtimeTools,
                                          ReplayRuntime& replayRuntime,
                                          const ReplayRuntime::PathPickInput& replayPointerRay,
                                          RunCameraMode replayCurrentCameraMode,
                                          RunCameraMode replayRestoreCameraMode,
                                          AttachedCameraController& attachedCamera,
                                          RuntimeInteractionController& interaction,
                                          RunTimerState& timers,
                                          SceneController& sceneController,
                                          RunSubsystemState& systems,
                                          SkullbonezCore::UI::InGameUI& ui,
                                          uint32_t cameraModeEnabledMask,
                                          bool suppressWorldActionThisFrame )
{
    RuntimeUIFrameResult result;
    result.suppressWorldActionThisFrame = suppressWorldActionThisFrame;
    if ( !systems.window )
    {
        return result;
    }
    result.frameActive = true;

    const int selectedSceneBrowserIndex = CurrentSceneBrowserIndex( sceneController, sceneController.Browser() );
    const HWND windowHandle = systems.window->NativeWindowHandle();
    InGameUIInputResult UIResult = ui.UpdateInput(
        inputRouter.DeviceFrame(),
        inputRouter.UiSnapshot().mouse,
        systems.window->ClientWidth(),
        systems.window->ClientHeight(),
        timers.simulationTimer.GetTotalTime(),
        runtimeTools.Editor().editorModeEnabled,
        runtimeTools.Editor().placementModeEnabled,
        runtimeTools.Editor().placeStaticObject,
        runtimeTools.Editor().autoTerrainAlign,
        runtimeTools.Editor().objectType,
        static_cast<int>( camera.mode ),
        cameraModeEnabledMask,
        sceneController.Browser().namePtrs.empty() ? nullptr : sceneController.Browser().namePtrs.data(),
        static_cast<int>( sceneController.Browser().namePtrs.size() ),
        selectedSceneBrowserIndex );
    switch ( UIResult.nativeMouseCapture )
    {
    case InGameUIInputResult::NativeMouseCaptureRequest::Acquire:
        inputRouter.RequestNativeCapture();
        break;
    case InGameUIInputResult::NativeMouseCaptureRequest::Release:
        inputRouter.ReleaseNativeCapture();
        break;
    case InGameUIInputResult::NativeMouseCaptureRequest::Unchanged:
    default:
        break;
    }
    result.editorUnhandledWheelDelta = UIResult.unhandledWheelDelta;
    result.commands = UIResult.commands;
    const DeviceInputFrame& deviceFrame = inputRouter.DeviceFrame();
    UiInputHitSnapshot uiSnapshot;
    uiSnapshot.mouse = inputRouter.UiSnapshot().mouse;
    uiSnapshot.clientX = deviceFrame.clientX;
    uiSnapshot.clientY = deviceFrame.clientY;
    uiSnapshot.hasClientPosition = deviceFrame.hasClientPosition;
    uiSnapshot.unhandledWheelDelta = UIResult.unhandledWheelDelta;
    uiSnapshot.userInteracted = result.commands.ui.userInteracted;
    uiSnapshot.blocksKeyboard = ui.BlocksKeyboard();
    uiSnapshot.blocksCameraMouse = ui.BlocksCameraMouse();
    uiSnapshot.wantsNativeCursor = ui.WantsNativeMouseCursor();
    inputRouter.PublishUiSnapshot( uiSnapshot );
    result.enterInteractiveScene = result.commands.ui.userInteracted;
    result.suppressWorldActionThisFrame = result.suppressWorldActionThisFrame || result.commands.ui.userInteracted;
    // Invariant: replay workspace tools execute during this input turn, before
    // the completed interaction policy exists. Publish current post-UI pointer
    // and key facts now; TakeInput republishes the final policy facts below.
    inputRouter.PublishRuntimeSnapshot( RuntimeInteractionFrameInput{}, result.suppressWorldActionThisFrame );
    replayRuntime.TickWorkspace(
        ReplayRuntime::ReplayWorkspaceInput{ windowHandle,
                                             ui.BlocksCameraMouse(),
                                             result.editorUnhandledWheelDelta,
                                             replayPointerRay,
                                             inputRouter,
                                             interaction,
                                             sceneController.Physics(),
                                             sceneController.Entities(),
                                             sceneController.Models().RenderPresentationRecords(),
                                             &sceneController.Cameras(),
                                             sceneController.Terrain().Get(),
                                             camera,
                                             runtimeTools.MousePickup(),
                                             replayCurrentCameraMode,
                                             replayRestoreCameraMode,
                                             attachedCamera.State().activeFollow,
                                             camera.director.grabbed,
                                             runtimeTools.Editor().editorModeEnabled,
                                             sceneController.State().isScenePhysics,
                                             ui.IsVisible(),
                                             ui.IsMinimized(),
                                             systems.window->ClientWidth(),
                                             systems.window->ClientHeight(),
                                             timers.simulationTimer.GetTotalTime() },
        result.replayWorkspace );
    result.enterInteractiveScene = result.enterInteractiveScene || result.replayWorkspace.enterInteractive;
    result.suppressWorldActionThisFrame = result.suppressWorldActionThisFrame || result.replayWorkspace.consumesMouse;
    runtimeInput.BeginFrame( true,
                             ui.BlocksKeyboard(),
                             ui.BlocksCameraMouse() || result.replayWorkspace.consumesMouse );
    return result;
}

// Lifetime: command application borrows concrete owners synchronously. The
// explicit list prevents a retained multi-domain bag from recreating Run.
RuntimeUIFrameResult ApplyRuntimeUIFrameCommands( RuntimeUIFrameResult result,
                                                  bool keyboardToggleEditorMode,
                                                  RuntimeInputContext& runtimeInput,
                                                  InputRouter& inputRouter,
                                                  RunCameraState& camera,
                                                  RuntimeTools& runtimeTools,
                                                  ReplayRuntime& replayRuntime,
                                                  RunCameraMode replayRestoreCameraMode,
                                                  uint32_t cameraModeEnabledMask,
                                                  AttachedCameraController& attachedCamera,
                                                  RuntimeInteractionController& interaction,
                                                  RunTimerState& timers,
                                                  RunDebugState& debug,
                                                  RunLaunchOptions& launchOptions,
                                                  RunRuntimeSettings& runtimeSettings,
                                                  EngineConfig& config,
                                                  SceneController& sceneController,
                                                  RunSubsystemState& systems,
                                                  SimulationSystem& simulation,
                                                  SkullbonezCore::Runtime::Audio::ContactAudioService& contactAudio,
                                                  RuntimeRenderBackendView& renderBackendView,
                                                  RenderDefaultsStore& renderDefaults,
                                                  CinematicRenderConfig& defaultCinematicRender,
                                                  int gameModelCapacity )
{
    if ( !result.frameActive )
    {
        return result;
    }
    const InGameUICommands& uiCommands = result.commands;

    const auto updateInputMode = [&]( RuntimeInputAction action, RuntimeInputActionSource source )
    {
        InputController::ApplyModeAction(
            runtimeInput,
            InputController::ResolveMode( BuildRuntimeInputModeState( camera.mode,
                                                                      runtimeTools.Editor(),
                                                                      attachedCamera.State().activeFollow,
                                                                      camera.director.grabbed ) ),
            action,
            source );
    };
    const auto recordUIAction = [&updateInputMode]( RuntimeInputAction action )
    { updateInputMode( action, RuntimeInputActionSource::UI ); };
    const auto applyEditorPlacementMode = [&]( bool toggle )
    {
        result.enterInteractiveScene = true;
        const RunInternal::EditorGizmoContext editorContext{ runtimeTools.Editor(),
                                                             sceneController.Models(),
                                                             sceneController.Physics(),
                                                             interaction };
        const RunInternal::EditorPlacementModeChangeResult placementMode =
            toggle ? RunInternal::ToggleEditorPlacementMode( editorContext )
                   : RunInternal::SetEditorPlacementMode( editorContext, true, false );
        inputRouter.SetWorldInteractionOwner( placementMode.worldOwner,
                                              InteractionExitReason::EnterEdit,
                                              replayRuntime,
                                              runtimeTools,
                                              interaction,
                                              sceneController.Cameras(),
                                              sceneController.Terrain().Get(),
                                              sceneController.Models(),
                                              sceneController.Physics(),
                                              camera,
                                              replayRestoreCameraMode,
                                              attachedCamera.State().activeFollow,
                                              camera.director.grabbed );
        if ( inputRouter.ReleasePointerToUi(
                 EvaluateRuntimePointerPresentation( inputRouter, runtimeTools.Editor(), replayRuntime ) ) )
        {
            InputController::ResetMouseLook( camera );
        }
        inputRouter.ApplyPointerPresentation(
            EvaluateRuntimePointerPresentation( inputRouter, runtimeTools.Editor(), replayRuntime ) );
        updateInputMode( RuntimeInputAction::ToggleEditorTool, RuntimeInputActionSource::UI );
    };
    const auto applyEditorModeToggle = [&]( RuntimeInputActionSource source )
    {
        result.enterInteractiveScene = true;
        const bool enteringEditor = !runtimeTools.Editor().editorModeEnabled;
        if ( enteringEditor )
        {
            const RuntimeInteractionTransition editorTransition = interaction.EnterEdit();
            inputRouter.ApplyInteractionTransition( editorTransition,
                                                    replayRuntime,
                                                    runtimeTools,
                                                    interaction,
                                                    sceneController.Cameras(),
                                                    sceneController.Terrain().Get(),
                                                    sceneController.Models(),
                                                    sceneController.Physics(),
                                                    camera,
                                                    replayRestoreCameraMode,
                                                    attachedCamera.State().activeFollow,
                                                    camera.director.grabbed );
            const bool wasFlyMode = RunCameraModeUsesFlyControls( camera.mode,
                                                                  attachedCamera.State().activeFollow,
                                                                  camera.director.grabbed );
            RunInternal::EnterEditorModeState(
                { runtimeTools.Editor(), sceneController.Models(), sceneController.Physics(), interaction },
                NormalizeRuntimeCameraMode( camera.mode, sceneController.State().isSceneMode, cameraModeEnabledMask ) );
            runtimeTools.CancelMousePickup( inputRouter, interaction );
            camera.mode = RunCameraMode::Inspect;
            if ( !wasFlyMode )
            {
                EnterFlyModeCamera( inputRouter,
                                    camera,
                                    sceneController.Cameras(),
                                    sceneController.State().isSceneMode,
                                    runtimeTools.Editor(),
                                    replayRuntime );
            }
            else
            {
                InputController::ResetMouseLook( camera );
            }
        }
        else
        {
            const RunCameraMode restoreMode =
                NormalizeRuntimeCameraMode( runtimeTools.Editor().restoreCameraModeAfterEditor,
                                            sceneController.State().isSceneMode,
                                            cameraModeEnabledMask );
            const RuntimeInteractionTransition restoreTransition = interaction.EnterCameraMode( restoreMode );
            inputRouter.ApplyInteractionTransition( restoreTransition,
                                                    replayRuntime,
                                                    runtimeTools,
                                                    interaction,
                                                    sceneController.Cameras(),
                                                    sceneController.Terrain().Get(),
                                                    sceneController.Models(),
                                                    sceneController.Physics(),
                                                    camera,
                                                    replayRestoreCameraMode,
                                                    attachedCamera.State().activeFollow,
                                                    camera.director.grabbed );
            const bool wasFlyMode = RunCameraModeUsesFlyControls( camera.mode,
                                                                  attachedCamera.State().activeFollow,
                                                                  camera.director.grabbed );
            RunInternal::ExitEditorModeState(
                { runtimeTools.Editor(), sceneController.Models(), sceneController.Physics(), interaction } );
            camera.mode = restoreMode;
            if ( wasFlyMode && !RunCameraModeUsesFlyControls( camera.mode,
                                                              attachedCamera.State().activeFollow,
                                                              camera.director.grabbed ) )
            {
                ExitFlyModeCamera( inputRouter,
                                   camera,
                                   sceneController.Cameras(),
                                   *sceneController.Terrain().Get(),
                                   sceneController.State().isSceneMode );
            }
            else
            {
                InputController::ResetMouseLook( camera );
            }
        }
        inputRouter.ApplyPointerPresentation(
            EvaluateRuntimePointerPresentation( inputRouter, runtimeTools.Editor(), replayRuntime ) );
        updateInputMode( RuntimeInputAction::ToggleEditor, source );
    };

    if ( ApplyRenderVsyncUICommand( RenderDeviceUICommandContext{ runtimeSettings, renderBackendView.deviceLifecycle },
                                    uiCommands.renderer ) )
    {
        recordUIAction( RuntimeInputAction::ToggleVsync );
    }
    const RunCameraModeUICommandResult cameraModeCommand = DecodeRunCameraModeUICommand( uiCommands.run );
    if ( cameraModeCommand.accepted )
    {
        inputRouter.ApplyCameraMode( camera,
                                     cameraModeCommand.mode,
                                     RuntimeInputActionSource::UI,
                                     runtimeInput,
                                     interaction,
                                     runtimeTools,
                                     replayRuntime,
                                     attachedCamera,
                                     sceneController );
    }
    const RunInternal::EditorGizmoContext editorGizmoContext{ runtimeTools.Editor(),
                                                              sceneController.Models(),
                                                              sceneController.Physics(),
                                                              interaction };
    const RunInternal::EditorPlacementPreModeUICommandResult editorPreModeCommands =
        RunInternal::ApplyEditorPlacementPreModeUICommands( editorGizmoContext, uiCommands.editor );
    if ( editorPreModeCommands.setPlaceStatic )
    {
        result.enterInteractiveScene = true;
        recordUIAction( RuntimeInputAction::ToggleEditorStaticPlacement );
    }
    if ( editorPreModeCommands.enterPlacementMode )
    {
        applyEditorPlacementMode( false );
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
        applyEditorPlacementMode( true );
    }
    const RunInternal::EditorPlacementPostModeUICommandResult editorPostModeCommands =
        RunInternal::ApplyEditorPlacementPostModeUICommands( runtimeTools.Editor(), uiCommands.editor );
    if ( editorPostModeCommands.toggledPlaceStatic )
    {
        result.enterInteractiveScene = true;
        recordUIAction( RuntimeInputAction::ToggleEditorStaticPlacement );
    }
    if ( editorPostModeCommands.toggledTerrainAlign )
    {
        result.enterInteractiveScene = true;
        recordUIAction( RuntimeInputAction::ToggleEditorTerrainAlign );
    }
    const DiagnosticsPhysicsOverlayUICommandResult physicsDiagnosticsCommands =
        ApplyDiagnosticsPhysicsOverlayUICommands( debug, uiCommands.physics );
    if ( physicsDiagnosticsCommands.toggledCollisionVisualizer )
    {
        recordUIAction( RuntimeInputAction::ToggleCollisionVisualizer );
    }
    if ( ApplyPhysicsSleepPolicyUICommand(
             PhysicsSleepPolicyUICommandContext{ runtimeSettings, sceneController.Models() },
             uiCommands.physics ) )
    {
        recordUIAction( RuntimeInputAction::TogglePhysicsSleepPolicy );
    }
    RecordDiagnosticsPhysicsOverlayUIActions( physicsDiagnosticsCommands, recordUIAction );
    const TornadoUICommandResult tornadoCommands =
        ApplyTornadoUICommands( TornadoUICommandContext{ runtimeSettings, sceneController.Models() },
                                uiCommands.physics );
    RecordTornadoToggleUIActions( tornadoCommands, recordUIAction );
    if ( runtimeTools.ApplyRayCastVisualizationUICommand( uiCommands.physics ) )
    {
        recordUIAction( RuntimeInputAction::ToggleRayCastVisualization );
    }
    RecordTornadoApplySettingsUIActions( tornadoCommands, recordUIAction );
    if ( ApplyDiagnosticsTerrainContactProbeUICommand( debug, uiCommands.physics ) )
    {
        recordUIAction( RuntimeInputAction::ToggleTerrainContactProbe );
    }
    if ( ApplyRuntimeTextOnlyUICommand( debug, uiCommands.sceneOptions ) )
    {
        recordUIAction( RuntimeInputAction::ToggleTextOnly );
    }
    if ( ApplySceneFixedStepUICommand( SceneFixedStepUICommandContext{ sceneController.State(), simulation },
                                       uiCommands.sceneOptions ) )
    {
        recordUIAction( RuntimeInputAction::ToggleFixedStep );
    }
    const RuntimePresentationUICommandResult presentationCommands = ApplyRuntimePresentationUICommands(
        RuntimePresentationUICommandContext{ debug,
                                             sceneController.State(),
                                             config,
                                             launchOptions,
                                             renderDefaults,
                                             renderBackendView.deviceLifecycle != nullptr,
                                             timers.simulationTimer.GetTimeSinceLastStart() },
        uiCommands.sceneOptions,
        uiCommands.renderTuning,
        uiCommands.water );
    RecordRuntimePresentationUIActions( presentationCommands, recordUIAction );
    if ( ApplySoundUICommands( SoundUICommandContext{ contactAudio, runtimeSettings, launchOptions.noContactAudio },
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
        if ( replayRuntime.ApplyMemoryPolicyRequest( request ) )
        {
            recordUIAction( RuntimeInputAction::SetReplayMemoryPolicy );
        }
    }
    RecordRuntimePresentationWaterUIActions( presentationCommands, recordUIAction );
    const RunSimulationUICommandResult runSimulationCommands =
        ApplyRunSimulationUICommands( RunSimulationUICommandContext{ sceneController.State(),
                                                                     sceneController.UIOverrides(),
                                                                     config,
                                                                     *systems.workerPool },
                                      uiCommands.sceneOptions,
                                      uiCommands.run,
                                      uiCommands.profiler );
    RecordRunSimulationUIActions( runSimulationCommands, recordUIAction );
    const DiagnosticsPhysicsDebugValueUICommandResult physicsDebugValueCommands =
        ApplyDiagnosticsPhysicsDebugValueUICommands( debug, uiCommands.physics );
    RecordDiagnosticsPhysicsDebugValueUIActions( physicsDebugValueCommands, recordUIAction );
    const RayCastLauncherTuningUICommandResult rayCastLauncherCommands =
        runtimeTools.ApplyRayCastLauncherTuningUICommands( uiCommands.physics );
    if ( rayCastLauncherCommands.setImpulseStrength )
    {
        replayRuntime.RecordLauncherConfigEvent( rayCastLauncherCommands.impulseConfigChangedFlags,
                                                 rayCastLauncherCommands.impulseConfigImpulseStrength,
                                                 rayCastLauncherCommands.impulseConfigProjectileSpeed );
        recordUIAction( RuntimeInputAction::SetRayCastImpulseStrength );
    }
    if ( rayCastLauncherCommands.setProjectileSpeed )
    {
        replayRuntime.RecordLauncherConfigEvent( rayCastLauncherCommands.projectileConfigChangedFlags,
                                                 rayCastLauncherCommands.projectileConfigImpulseStrength,
                                                 rayCastLauncherCommands.projectileConfigProjectileSpeed );
        recordUIAction( RuntimeInputAction::SetLauncherProjectileSpeed );
    }
    EngineConfig& liveConfig = config;
    const PhysicsFrictionUICommandResult physicsFrictionCommands =
        ApplyPhysicsFrictionUICommands( PhysicsFrictionUICommandContext{ liveConfig, sceneController.Models() },
                                        uiCommands.physics );
    RecordPhysicsFrictionUIActions( physicsFrictionCommands, recordUIAction );
    const auto makeSceneGeneratedControlContext = [&]() -> SceneRuntimeGeneratedControlContext
    {
        return SceneRuntimeGeneratedControlContext{ sceneController.State(),
                                                    sceneController.UIOverrides(),
                                                    camera,
                                                    sceneController,
                                                    liveConfig,
                                                    sceneController.World(),
                                                    sceneController.Terrain().Get(),
                                                    sceneController.Models(),
                                                    simulation,
                                                    runtimeTools,
                                                    renderBackendView.deviceLifecycle,
                                                    launchOptions.generatedObjectTypeOverride,
                                                    gameModelCapacity };
    };
    const auto executeSceneGeneratedControlAction = [&]( const SceneRuntimeGeneratedControlAction& action )
    {
        if ( action.resetReplayTimeline )
        {
            const ReplayRuntime::SceneTimelineResetInput reset = ReplayRuntime::DescribeSceneTimeline(
                sceneController,
                sceneController.State(),
                gameModelCapacity,
                static_cast<uint32_t>( launchOptions.generatedObjectTypeOverride ) );
            replayRuntime.ResetSceneTimeline(
                reset,
                ReplayRuntime::SceneTimelineResetOwners{ inputRouter,
                                                         interaction,
                                                         &sceneController.Cameras(),
                                                         sceneController.Terrain().Get(),
                                                         camera,
                                                         replayRestoreCameraMode,
                                                         attachedCamera.State().activeFollow,
                                                         camera.director.grabbed } );
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
    if ( ApplyWorldWaterUICommands( sceneController.World(), replayRuntime, uiCommands.water ) )
    {
        recordUIAction( RuntimeInputAction::ApplyWorldWaterSettings );
    }
    CinematicRenderConfig& activeCinematic = RuntimeActiveCinematicConfig( sceneController.State(), config );
    const CinematicUICommandContext cinematicUICommandContext{ launchOptions,
                                                               sceneController.State(),
                                                               activeCinematic,
                                                               renderDefaults };
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
        result.enterInteractiveScene = true;
        ApplyCinematicModeUICommand( SceneRuntimeStyleContext{ launchOptions,
                                                               sceneController.State(),
                                                               sceneController.Browser(),
                                                               sceneController.Models(),
                                                               sceneController.Entities(),
                                                               systems.assets,
                                                               activeCinematic,
                                                               defaultCinematicRender },
                                     uiCommands.cinematic );
        recordUIAction( RuntimeInputAction::SelectCinematicScene );
    }
    const CinematicTuningUICommandResult cinematicTuningCommands =
        ApplyCinematicTuningUICommands( cinematicUICommandContext, uiCommands.cinematic );
    RecordCinematicTuningUIActions( cinematicTuningCommands, recordUIAction );
    const SceneRuntimeUICommandResult sceneUICommands = SubmitSceneUIRequests( sceneController, uiCommands.scene );
    if ( !sceneUICommands.status.ok )
    {
        result.status = sceneUICommands.status;
        return result;
    }
    RecordSceneRuntimeUIActions( sceneUICommands, recordUIAction );

    return result;
}

RuntimeUIFrameResult FinishRuntimeUIFramePointer( RuntimeUIFrameResult result,
                                                  RuntimeInputContext& runtimeInput,
                                                  InputRouter& inputRouter,
                                                  RunCameraState& camera,
                                                  RuntimeTools& runtimeTools,
                                                  ReplayRuntime& replayRuntime,
                                                  RunCameraMode replayCurrentCameraMode,
                                                  AttachedCameraController& attachedCamera,
                                                  SceneController& sceneController,
                                                  SkullbonezCore::UI::InGameUI& ui )
{
    // Invariant: pointer ownership is finalized only after UI mutations and
    // stress actions succeed; failure leaves later world routing untouched.
    if ( !result.frameActive || !result.status.ok )
    {
        return result;
    }

    const auto updateInputMode = [&]( RuntimeInputAction action, RuntimeInputActionSource source )
    {
        InputController::ApplyModeAction(
            runtimeInput,
            InputController::ResolveMode( BuildRuntimeInputModeState( camera.mode,
                                                                      runtimeTools.Editor(),
                                                                      attachedCamera.State().activeFollow,
                                                                      camera.director.grabbed ) ),
            action,
            source );
    };

    if ( attachedCamera.ApplyOrbitInput( sceneController.Models(),
                                         sceneController.Cameras(),
                                         RunCameraModeIsAttached( replayCurrentCameraMode ),
                                         result.editorUnhandledWheelDelta,
                                         ui.BlocksCameraMouse() ) )
    {
        result.enterInteractiveScene = true;
    }
    const DeviceInputFrame& editorDevice = inputRouter.DeviceFrame();
    const EditorViewportPlacementResult editorPointerResult =
        runtimeTools.RouteEditorViewportPlacement( { result.editorUnhandledWheelDelta,
                                                     editorDevice.rightDown,
                                                     editorDevice.leftDown,
                                                     editorDevice.keys.IsDown( VK_CONTROL ),
                                                     ui.BlocksCameraMouse(),
                                                     editorDevice.hasClientPosition,
                                                     runtimeInput.CurrentMode() == RuntimeInputMode::EditorViewportLook,
                                                     editorDevice.clientX,
                                                     editorDevice.clientY } );
    if ( editorPointerResult.resetMouseLook )
    {
        InputController::ResetMouseLook( camera );
    }
    if ( editorPointerResult.modeAction == EditorViewportModeAction::Begin )
    {
        updateInputMode( RuntimeInputAction::BeginEditorViewportLook, RuntimeInputActionSource::Mouse );
    }
    else if ( editorPointerResult.modeAction == EditorViewportModeAction::End )
    {
        updateInputMode( RuntimeInputAction::EndEditorViewportLook, RuntimeInputActionSource::Mouse );
    }
    if ( editorPointerResult.enteredInteractiveScene )
    {
        result.enterInteractiveScene = true;
    }
    const UiInputHitSnapshot& presentationUi = inputRouter.UiSnapshot();
    PointerPresentationPolicyInput presentationInput;
    presentationInput.editorModeEnabled = runtimeTools.Editor().editorModeEnabled;
    presentationInput.editorViewportLookActive = runtimeTools.Editor().viewportLookActive;
    presentationInput.editorPlacementModeEnabled = runtimeTools.Editor().placementModeEnabled;
    presentationInput.editorPlacementPreviewVisible = runtimeTools.Editor().placementPreviewVisible;
    presentationInput.replayInspectionActive = replayRuntime.InspectionActive();
    presentationInput.replayInspectionLookActive =
        presentationInput.replayInspectionActive &&
        replayRuntime.InspectionMouseLookActive( editorDevice.rightDown,
                                                 presentationUi.wantsNativeCursor,
                                                 presentationUi.blocksCameraMouse );
    inputRouter.RequestCursorVisible( !inputRouter.EvaluatePointerPresentation( presentationInput ).hideNativeCursor );
    return result;
}

} // namespace

void InputRouter::ApplyInteractionTransitionCleanup( const RuntimeInteractionTransition& transition,
                                                     ReplayRuntime& replayRuntime,
                                                     RuntimeTools& runtimeTools,
                                                     RuntimeInteractionController& interaction,
                                                     SkullbonezCore::Environment::CameraCollection& cameras,
                                                     SkullbonezCore::Geometry::Terrain* terrain,
                                                     SkullbonezCore::GameObjects::GameModelCollection& models,
                                                     PhysicsEngine& physics,
                                                     RunCameraState& camera,
                                                     RunCameraMode replayRestoreCameraMode,
                                                     bool attachedCameraFollow,
                                                     bool directorGrabbed )
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
         ( replayRuntime.HasActiveInteractionState() || IsReplayWorldOwner( transition.previousOwner ) ) )
    {
        if ( replayRuntime.ClearInteractionForRuntimeTransition( interaction, *this ) )
        {
            replayRuntime.ExitInspectionCamera( &cameras,
                                                terrain,
                                                camera,
                                                replayRestoreCameraMode,
                                                attachedCameraFollow,
                                                directorGrabbed,
                                                interaction,
                                                *this );
        }
        ApplyPointerPresentation( EvaluateRuntimePointerPresentation( *this, runtimeTools.Editor(), replayRuntime ) );
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

    if ( ( !enteringEdit && !inspectGizmoClaimWithinInspect && runtimeTools.HasActiveEditorInteractionState() ) ||
         ( IsEditorWorldOwner( transition.previousOwner ) && !editorOwnerSwitchWithinEdit &&
           !inspectGizmoClaimWithinInspect ) )
    {
        runtimeTools.ClearEditorInteractionForTransition( enteringReplay || enteringTool,
                                                          models,
                                                          physics,
                                                          interaction );
        if ( ReleasePointerToUi( EvaluateRuntimePointerPresentation( *this, runtimeTools.Editor(), replayRuntime ) ) )
        {
            InputController::ResetMouseLook( camera );
        }
        ApplyPointerPresentation( EvaluateRuntimePointerPresentation( *this, runtimeTools.Editor(), replayRuntime ) );
        if ( runtimeTools.Editor().editorModeEnabled && !enteringEdit )
        {
            runtimeTools.Editor().editorModeEnabled = false;
        }
    }
}


void InputRouter::ApplyInteractionTransition( const RuntimeInteractionTransition& transition,
                                              ReplayRuntime& replayRuntime,
                                              RuntimeTools& runtimeTools,
                                              RuntimeInteractionController& interaction,
                                              SkullbonezCore::Environment::CameraCollection& cameras,
                                              SkullbonezCore::Geometry::Terrain* terrain,
                                              SkullbonezCore::GameObjects::GameModelCollection& models,
                                              PhysicsEngine& physics,
                                              RunCameraState& camera,
                                              RunCameraMode replayRestoreCameraMode,
                                              bool attachedCameraFollow,
                                              bool directorGrabbed )
{
    ApplyInteractionTransitionCleanup( transition,
                                       replayRuntime,
                                       runtimeTools,
                                       interaction,
                                       cameras,
                                       terrain,
                                       models,
                                       physics,
                                       camera,
                                       replayRestoreCameraMode,
                                       attachedCameraFollow,
                                       directorGrabbed );
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


RuntimeInteractionTransition
InputRouter::SetWorldInteractionOwner( WorldInteractionOwner owner,
                                       InteractionExitReason reason,
                                       ReplayRuntime& replayRuntime,
                                       RuntimeTools& runtimeTools,
                                       RuntimeInteractionController& interaction,
                                       SkullbonezCore::Environment::CameraCollection& cameras,
                                       SkullbonezCore::Geometry::Terrain* terrain,
                                       SkullbonezCore::GameObjects::GameModelCollection& models,
                                       PhysicsEngine& physics,
                                       RunCameraState& camera,
                                       RunCameraMode replayRestoreCameraMode,
                                       bool attachedCameraFollow,
                                       bool directorGrabbed )
{
    // Why: changing the logical owner can eject replay, editor, or camera gestures. InputRouter owns that
    // cleanup because it also reconciles the corresponding capture and cursor state.
    const RuntimeWorkspace workspace = WorkspaceForWorldInteractionOwner( interaction.Workspace(), owner );
    const RuntimeInteractionTransition transition =
        interaction.SetWorldInteractionOwnerInWorkspace( workspace, owner, reason );
    ApplyInteractionTransitionCleanup( transition,
                                       replayRuntime,
                                       runtimeTools,
                                       interaction,
                                       cameras,
                                       terrain,
                                       models,
                                       physics,
                                       camera,
                                       replayRestoreCameraMode,
                                       attachedCameraFollow,
                                       directorGrabbed );
    // Invariant: cleanup may temporarily select a neutral owner; the requested owner is the final state.
    interaction.SetWorldInteractionOwnerInWorkspace( workspace, owner, reason );
    return transition;
}

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


RuntimePointerRouteResult InputRouter::RouteRuntimePointer( const RuntimePointerRouteInput& input,
                                                            RuntimeTools& runtimeTools,
                                                            ReplayRuntime& replayRuntime,
                                                            AttachedCameraController& attachedCamera,
                                                            RuntimeInteractionController& interaction,
                                                            SceneEntityStore& entities,
                                                            GameObjects::GameModelCollection& models,
                                                            PhysicsEngine& physics,
                                                            RunSceneState& scene,
                                                            Environment::WorldEnvironment& world,
                                                            Geometry::Terrain* terrain,
                                                            Assets::AssetSystem& assets,
                                                            Environment::CameraCollection& cameras,
                                                            RunCameraState& camera,
                                                            RunCameraMode replayRestoreCameraMode,
                                                            bool attachedCameraFollow,
                                                            bool directorGrabbed )
{
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
                                                                      runtimeTools,
                                                                      replayRuntime,
                                                                      interaction,
                                                                      models,
                                                                      physics,
                                                                      scene,
                                                                      world,
                                                                      terrain,
                                                                      assets,
                                                                      cameras,
                                                                      camera,
                                                                      replayRestoreCameraMode,
                                                                      attachedCameraFollow,
                                                                      directorGrabbed );
    result.enteredInteractiveScene = editorResult.enteredInteractiveScene;
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
             ( runtimeTools.MousePickup().active || pickupInput.leftPressed ) )
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
            command.modelIndex = selection.modelIndex;
            command.body = selection.body;
            command.collider = selection.collider;
            command.selectionScope = RuntimeInteractionSelectionScope::Inspect;
            command.claimSelectionOwner = false;
            runtimeTools.ApplySelectionCommand( command, models );
            ApplyPointerPresentation(
                EvaluateRuntimePointerPresentation( *this, runtimeTools.Editor(), replayRuntime ) );
        }
        result.enteredInteractiveScene = true;
        appendModeAction( RuntimeInputAction::SetCameraMode );
        consumed = true;
    }

    if ( !consumed )
    {
        ReplayRuntime::PathPickInput pickInput;
        pickInput.hasWorldRay = input.leftPressed && input.hasWorldRay;
        pickInput.rayOrigin = input.rayOrigin;
        pickInput.rayDirection = input.rayDirection;
        pickInput.additive = input.shiftDown;
        pickInput.clearOnMiss = !input.shiftDown;
        consumed = replayRuntime.RouteWorldPointer(
            ReplayRuntime::WorldPointerInput{ input.leftPressed,
                                              input.suppressWorldAction,
                                              runtimeTools.Editor().editorModeEnabled,
                                              input.uiWantsNativeCursor,
                                              input.controlDown,
                                              RunCameraModeUsesLauncher( input.cameraMode ),
                                              pickInput,
                                              entities,
                                              models.BodyStore(),
                                              models.Colliders(),
                                              models.RenderPresentationRecords(),
                                              &cameras,
                                              terrain,
                                              camera,
                                              replayRestoreCameraMode,
                                              attachedCameraFollow,
                                              directorGrabbed,
                                              interaction,
                                              *this } );
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
                                               replayRuntime,
                                               models,
                                               physics,
                                               scene,
                                               terrain );
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


RuntimeInteractionTransition Run::EnterInteractionForCameraMode( RunCameraMode mode )
{
    return m_interaction.EnterCameraMode( NormalizeCameraModeForCurrentScene( mode ) );
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
    return NormalizeRuntimeCameraMode( mode, SceneState().isSceneMode, CameraModeEnabledMask() );
}


uint32_t Run::CameraModeEnabledMask() const
{
    return RuntimeCameraModeEnabledMask( m_sceneController );
}


void InputRouter::ApplyCameraMode( RunCameraState& camera,
                                   RunCameraMode mode,
                                   RuntimeInputActionSource source,
                                   RuntimeInputContext& runtimeInput,
                                   RuntimeInteractionController& interaction,
                                   RuntimeTools& runtimeTools,
                                   ReplayRuntime& replayRuntime,
                                   AttachedCameraController& attachedCamera,
                                   SceneController& sceneController )
{
    // Lifetime: all domain owners are synchronous borrows for one semantic
    // mode request. InputRouter retains only its own edge/presentation state.
    // Invariant: interaction cleanup precedes camera/editor mutation, then
    // pointer and RuntimeInputContext presentation publish the completed mode.
    InputRouter& m_inputRouter = *this;
    RunCameraState& m_camera = camera;
    RuntimeInteractionController& m_interaction = interaction;
    RuntimeTools& m_runtimeTools = runtimeTools;
    ReplayRuntime& m_replayRuntime = replayRuntime;
    AttachedCameraController& m_attachedCamera = attachedCamera;
    SceneController& m_sceneController = sceneController;
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

    const RuntimeInteractionTransition transition = m_interaction.EnterCameraMode( mode );
    m_inputRouter.ApplyInteractionTransition(
        transition,
        m_replayRuntime,
        m_runtimeTools,
        m_interaction,
        m_sceneController.Cameras(),
        m_sceneController.Terrain().Get(),
        m_sceneController.Models(),
        m_sceneController.Physics(),
        m_camera,
        NormalizeRuntimeCameraMode( m_replayRuntime.Camera().restoreCameraMode, authoredScene, enabledMask ),
        m_attachedCamera.State().activeFollow,
        m_camera.director.grabbed );

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
                                m_replayRuntime );
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
            EvaluateRuntimePointerPresentation( m_inputRouter, m_runtimeTools.Editor(), m_replayRuntime ) );
    }
    if ( mode == RunCameraMode::Attach )
    {
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

        AttachedCameraTargetSelection selection;
        const AttachedCameraSeedResult seedResult = m_attachedCamera.SeedTarget( m_sceneController.Models(),
                                                                                 m_sceneController.Cameras(),
                                                                                 seedIndex,
                                                                                 selection );
        if ( seedResult == AttachedCameraSeedResult::SelectedSeed )
        {
            RuntimeInteractionCommand command;
            command.type = RuntimeInteractionCommandType::SetEditorSelection;
            command.modelIndex = selection.modelIndex;
            command.body = selection.body;
            command.collider = selection.collider;
            command.selectionScope = RuntimeInteractionSelectionScope::Inspect;
            command.claimSelectionOwner = false;
            m_runtimeTools.ApplySelectionCommand( command, m_sceneController.Models() );
        }
        if ( seedResult != AttachedCameraSeedResult::Failed )
        {
            m_inputRouter.ApplyPointerPresentation(
                EvaluateRuntimePointerPresentation( m_inputRouter, m_runtimeTools.Editor(), m_replayRuntime ) );
        }
    }
    InputController::ApplyModeAction(
        runtimeInput,
        InputController::ResolveMode( BuildRuntimeInputModeState( m_camera.mode,
                                                                  m_runtimeTools.Editor(),
                                                                  m_attachedCamera.State().activeFollow,
                                                                  m_camera.director.grabbed ) ),
        source == RuntimeInputActionSource::UI ? RuntimeInputAction::SetCameraMode
                                               : RuntimeInputAction::CycleCameraMode,
        source );
}


void InputRouter::CycleCameraMode( RunCameraState& camera,
                                   RuntimeInputContext& runtimeInput,
                                   RuntimeInteractionController& interaction,
                                   RuntimeTools& runtimeTools,
                                   ReplayRuntime& replayRuntime,
                                   AttachedCameraController& attachedCamera,
                                   SceneController& sceneController )
{
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
            ApplyCameraMode( camera,
                             restoreMode,
                             RuntimeInputActionSource::Keyboard,
                             runtimeInput,
                             interaction,
                             runtimeTools,
                             replayRuntime,
                             attachedCamera,
                             sceneController );
            return;
        }
    }

    for ( int step = 1; step <= static_cast<int>( RunCameraMode::Count ); ++step )
    {
        const int next = ( current + step ) % static_cast<int>( RunCameraMode::Count );
        if ( ( enabledMask & ( 1u << next ) ) != 0 )
        {
            ApplyCameraMode( camera,
                             static_cast<RunCameraMode>( next ),
                             RuntimeInputActionSource::Keyboard,
                             runtimeInput,
                             interaction,
                             runtimeTools,
                             replayRuntime,
                             attachedCamera,
                             sceneController );
            return;
        }
    }
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
            m_inputRouter.ApplyPointerPresentation(
                EvaluateRuntimePointerPresentation( m_inputRouter, m_runtimeTools.Editor(), m_replayRuntime ) );
            if ( m_inputRouter.ReleasePointerToUi(
                     EvaluateRuntimePointerPresentation( m_inputRouter, m_runtimeTools.Editor(), m_replayRuntime ) ) )
            {
                InputController::ResetMouseLook( m_camera );
            }
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
    m_inputRouter.ApplyPointerPresentation(
        EvaluateRuntimePointerPresentation( m_inputRouter, m_runtimeTools.Editor(), m_replayRuntime ) );
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
        m_inputRouter.SetWorldInteractionOwner(
            placementMode.worldOwner,
            InteractionExitReason::EnterEdit,
            m_replayRuntime,
            m_runtimeTools,
            m_interaction,
            m_sceneController.Cameras(),
            m_sceneController.Terrain().Get(),
            m_sceneController.Models(),
            m_sceneController.Physics(),
            m_camera,
            NormalizeCameraModeForCurrentScene( m_replayRuntime.Camera().restoreCameraMode ),
            m_attachedCamera.State().activeFollow,
            m_camera.director.grabbed );
        if ( m_inputRouter.ReleasePointerToUi(
                 EvaluateRuntimePointerPresentation( m_inputRouter, m_runtimeTools.Editor(), m_replayRuntime ) ) )
        {
            InputController::ResetMouseLook( m_camera );
        }
        m_inputRouter.ApplyPointerPresentation(
            EvaluateRuntimePointerPresentation( m_inputRouter, m_runtimeTools.Editor(), m_replayRuntime ) );
        UpdateRuntimeInputModeAfterAction( RuntimeInputAction::ToggleEditorTool, source );
    };
    auto applyEditorPlacementModeToggle =
        [this, &completeEditorPlacementModeTransition]( RuntimeInputActionSource source )
    {
        EnterInteractiveSceneRun();
        const RunInternal::EditorPlacementModeChangeResult placementMode = RunInternal::ToggleEditorPlacementMode(
            { m_runtimeTools.Editor(), m_sceneController.Models(), m_sceneController.Physics(), m_interaction } );
        completeEditorPlacementModeTransition( source, placementMode );
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
            m_inputRouter.CycleCameraMode( m_camera,
                                           m_runtimeInput,
                                           m_interaction,
                                           m_runtimeTools,
                                           m_replayRuntime,
                                           m_attachedCamera,
                                           m_sceneController );
            break;
        case RuntimeInputAction::ToggleFlyCamera:
        {
            const RunCameraMode passiveMode = SceneState().isSceneMode ? RunCameraMode::Scene : RunCameraMode::Demo;
            m_inputRouter.ApplyCameraMode(
                m_camera,
                m_camera.mode == RunCameraMode::Inspect ? passiveMode : RunCameraMode::Inspect,
                event.source,
                m_runtimeInput,
                m_interaction,
                m_runtimeTools,
                m_replayRuntime,
                m_attachedCamera,
                m_sceneController );
            break;
        }
        case RuntimeInputAction::ToggleLauncher:
            if ( m_camera.mode == RunCameraMode::Launcher )
            {
                m_inputRouter.ApplyCameraMode( m_camera,
                                               m_camera.modeBeforeLauncher,
                                               event.source,
                                               m_runtimeInput,
                                               m_interaction,
                                               m_runtimeTools,
                                               m_replayRuntime,
                                               m_attachedCamera,
                                               m_sceneController );
            }
            else
            {
                m_camera.modeBeforeLauncher =
                    m_camera.mode == RunCameraMode::Manipulator ? RunCameraMode::Inspect : m_camera.mode;
                m_inputRouter.ApplyCameraMode( m_camera,
                                               RunCameraMode::Launcher,
                                               event.source,
                                               m_runtimeInput,
                                               m_interaction,
                                               m_runtimeTools,
                                               m_replayRuntime,
                                               m_attachedCamera,
                                               m_sceneController );
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
                    if ( m_inputRouter.ReleasePointerToUi( EvaluateRuntimePointerPresentation( m_inputRouter,
                                                                                               m_runtimeTools.Editor(),
                                                                                               m_replayRuntime ) ) )
                    {
                        InputController::ResetMouseLook( m_camera );
                    }
                }
                m_inputRouter.ApplyPointerPresentation(
                    EvaluateRuntimePointerPresentation( m_inputRouter, m_runtimeTools.Editor(), m_replayRuntime ) );
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
                    ExitFlyModeCamera( m_inputRouter,
                                       m_camera,
                                       m_sceneController.Cameras(),
                                       *m_sceneController.Terrain().Get(),
                                       SceneState().isSceneMode );
                    m_inputRouter.ApplyPointerPresentation(
                        EvaluateRuntimePointerPresentation( m_inputRouter, m_runtimeTools.Editor(), m_replayRuntime ) );
                    UpdateRuntimeInputModeAfterAction( event.action, event.source );
                }
            }
            else if ( DemoDirectorPlayback::BeginGrab( m_camera, m_sceneController.Cameras() ) )
            {
                EnterFlyModeCamera( m_inputRouter,
                                    m_camera,
                                    m_sceneController.Cameras(),
                                    SceneState().isSceneMode,
                                    m_runtimeTools.Editor(),
                                    m_replayRuntime );
                m_inputRouter.ApplyPointerPresentation(
                    EvaluateRuntimePointerPresentation( m_inputRouter, m_runtimeTools.Editor(), m_replayRuntime ) );
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
                    m_inputRouter.ApplyPointerPresentation(
                        EvaluateRuntimePointerPresentation( m_inputRouter, m_runtimeTools.Editor(), m_replayRuntime ) );
                    if ( m_inputRouter.ReleasePointerToUi( EvaluateRuntimePointerPresentation( m_inputRouter,
                                                                                               m_runtimeTools.Editor(),
                                                                                               m_replayRuntime ) ) )
                    {
                        InputController::ResetMouseLook( m_camera );
                    }
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

    if ( m_runtimeTools.Editor().editorModeEnabled )
    {
        m_replayRuntime.SetVelocityEditAltKeyDown( keyboardEditorToolShortcut.altDown );
        if ( keyboardEditorToolShortcut.togglePlacementMode )
        {
            applyEditorPlacementModeToggle( RuntimeInputActionSource::Keyboard );
        }
    }
    else
    {
        const ReplayRuntime::KeyboardVelocityEditResult velocityEditResult = m_replayRuntime.ApplyKeyboardVelocityEdit(
            { keyboardEditorToolShortcut.altDown, m_interaction.Owner(), m_timers.simulationTimer.GetTotalTime() } );
        if ( velocityEditResult.cancelToolDrag )
        {
            m_replayRuntime.CancelToolDragState( m_interaction, m_inputRouter );
        }
        if ( velocityEditResult.enterInteractive )
        {
            EnterInteractiveSceneRun();
        }
        if ( velocityEditResult.cameraAction == ReplayRuntime::KeyboardVelocityEditCameraAction::EnterInspection )
        {
            m_replayRuntime.EnterInspectionCamera( &m_sceneController.Cameras(),
                                                   m_camera,
                                                   NormalizeCameraModeForCurrentScene( m_camera.mode ),
                                                   m_interaction,
                                                   m_inputRouter,
                                                   m_runtimeTools.MousePickup() );
        }
        else if ( velocityEditResult.cameraAction == ReplayRuntime::KeyboardVelocityEditCameraAction::ExitInspection )
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
        if ( velocityEditResult.setWorldOwner )
        {
            m_inputRouter.SetWorldInteractionOwner(
                velocityEditResult.worldOwner,
                InteractionExitReason::EnterReplay,
                m_replayRuntime,
                m_runtimeTools,
                m_interaction,
                m_sceneController.Cameras(),
                m_sceneController.Terrain().Get(),
                m_sceneController.Models(),
                m_sceneController.Physics(),
                m_camera,
                NormalizeCameraModeForCurrentScene( m_replayRuntime.Camera().restoreCameraMode ),
                m_attachedCamera.State().activeFollow,
                m_camera.director.grabbed );
        }
    }
    ReplayRuntime::PathPickInput replayPointerRay;
    replayPointerRay.hasWorldRay = m_inputRouter.TryBuildWorldRay( m_sceneController.Cameras(),
                                                                   *m_systems.window,
                                                                   replayPointerRay.rayOrigin,
                                                                   replayPointerRay.rayDirection );
    RuntimeUIFrameResult uiFrameResult =
        BeginRuntimeUIFrame( m_runtimeInput,
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
                             m_sceneController,
                             m_systems,
                             m_UI,
                             CameraModeEnabledMask(),
                             UIBlocksKeyboardBeforeInput );
    if ( uiFrameResult.frameActive )
    {
        if ( uiFrameResult.enterInteractiveScene )
        {
            EnterInteractiveSceneRun();
            uiFrameResult.enterInteractiveScene = false;
        }
        DispatchAfterUIKeyboardActions( uiFrameResult.commands.ui.userInteracted );
    }
    uiFrameResult =
        ApplyRuntimeUIFrameCommands( uiFrameResult,
                                     keyboardToggleEditorMode,
                                     m_runtimeInput,
                                     m_inputRouter,
                                     m_camera,
                                     m_runtimeTools,
                                     m_replayRuntime,
                                     NormalizeCameraModeForCurrentScene( m_replayRuntime.Camera().restoreCameraMode ),
                                     CameraModeEnabledMask(),
                                     m_attachedCamera,
                                     m_interaction,
                                     m_timers,
                                     m_debug,
                                     m_launchOptions,
                                     m_runtimeSettings,
                                     m_config,
                                     m_sceneController,
                                     m_systems,
                                     m_simulation,
                                     m_contactAudio,
                                     m_renderBackendView,
                                     m_renderDefaults,
                                     m_defaultCinematicRender,
                                     m_startup.gameModelCapacity );
    if ( uiFrameResult.status.ok && uiFrameResult.frameActive )
    {
        uiFrameResult.status = RunUIStressActions();
    }
    uiFrameResult = FinishRuntimeUIFramePointer( uiFrameResult,
                                                 m_runtimeInput,
                                                 m_inputRouter,
                                                 m_camera,
                                                 m_runtimeTools,
                                                 m_replayRuntime,
                                                 NormalizeCameraModeForCurrentScene( m_camera.mode ),
                                                 m_attachedCamera,
                                                 m_sceneController,
                                                 m_UI );
    if ( uiFrameResult.enterInteractiveScene )
    {
        EnterInteractiveSceneRun();
    }
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
    const RuntimeInputSnapshot& inputSnapshot =
        m_inputRouter.PublishRuntimeSnapshot( frameInput, suppressWorldActionThisFrame );
    const DeviceInputFrame& pointerDevice = m_inputRouter.DeviceFrame();
    RuntimePointerRouteInput pointerInput;
    pointerInput.leftDown = mouseEdges.leftDown;
    pointerInput.leftPressed = inputSnapshot.pointer.leftPressed;
    pointerInput.leftReleased = mouseEdges.leftReleased;
    pointerInput.suppressWorldAction = inputSnapshot.pointer.suppressWorldAction;
    pointerInput.uiWantsNativeCursor = inputSnapshot.pointer.uiWantsNativeMouseCursor;
    pointerInput.shiftDown = inputSnapshot.pointer.shiftDown;
    pointerInput.controlDown = inputSnapshot.pointer.controlDown;
    pointerInput.blocksCameraMouse = routedUiSnapshot.blocksCameraMouse;
    pointerInput.hasClientPosition = pointerDevice.hasClientPosition;
    pointerInput.replayInspectionActive = m_replayRuntime.InspectionActive();
    pointerInput.clientX = pointerDevice.clientX;
    pointerInput.clientY = pointerDevice.clientY;
    pointerInput.activeModelCapacity = m_startup.gameModelCapacity;
    pointerInput.cameraMode = m_camera.mode;
    pointerInput.hasWorldRay = m_inputRouter.TryBuildWorldRay( m_sceneController.Cameras(),
                                                               *m_systems.window,
                                                               pointerInput.rayOrigin,
                                                               pointerInput.rayDirection );
    pointerInput.hasClampedWorldRay = m_inputRouter.TryBuildWorldRay( m_sceneController.Cameras(),
                                                                      *m_systems.window,
                                                                      pointerInput.clampedRayOrigin,
                                                                      pointerInput.clampedRayDirection,
                                                                      true );
    pointerInput.cameraEye = m_sceneController.Cameras().GetCameraTranslation();
    pointerInput.cameraView = m_sceneController.Cameras().GetCameraView();
    const RuntimePointerRouteResult pointerResult = m_inputRouter.RouteRuntimePointer(
        pointerInput,
        m_runtimeTools,
        m_replayRuntime,
        m_attachedCamera,
        m_interaction,
        m_sceneController.Entities(),
        m_sceneController.Models(),
        m_sceneController.Physics(),
        SceneState(),
        m_sceneController.World(),
        m_sceneController.Terrain().Get(),
        m_systems.assets,
        m_sceneController.Cameras(),
        m_camera,
        NormalizeCameraModeForCurrentScene( m_replayRuntime.Camera().restoreCameraMode ),
        m_attachedCamera.State().activeFollow,
        m_camera.director.grabbed );
    if ( pointerResult.enteredInteractiveScene )
    {
        EnterInteractiveSceneRun();
    }
    for ( std::size_t actionIndex = 0; actionIndex < pointerResult.modeActionCount; ++actionIndex )
    {
        UpdateRuntimeInputModeAfterAction( pointerResult.modeActions[actionIndex], RuntimeInputActionSource::Mouse );
    }

    if ( m_UI.BlocksKeyboard() )
    {
        m_interaction.CancelCameraLookGesture();
        InputController::ResetMouseLook( m_camera );
        m_camera.input.Set( InputState::Up, false );
        m_camera.input.Set( InputState::Down, false );
        m_camera.input.Set( InputState::Left, false );
        m_camera.input.Set( InputState::Right, false );
        m_inputRouter.ApplyPointerPresentation(
            EvaluateRuntimePointerPresentation( m_inputRouter, m_runtimeTools.Editor(), m_replayRuntime ) );
    }
    else
    {
        DispatchPostUIKeyboardActions();
        const RuntimeInteractionFramePolicy inputPolicy = m_interaction.BuildFramePolicy( inputSnapshot.frameInput );
        const bool mouseOwnsCursor =
            EvaluateRuntimePointerPresentation( m_inputRouter, m_runtimeTools.Editor(), m_replayRuntime )
                .mouseLookOwnsCursor;
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
            m_inputRouter.ApplyPointerPresentation(
                EvaluateRuntimePointerPresentation( m_inputRouter, m_runtimeTools.Editor(), m_replayRuntime ) );
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
