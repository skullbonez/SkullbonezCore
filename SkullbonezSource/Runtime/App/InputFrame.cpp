/*
File: InputFrame.cpp
Purpose:
  Implements shared input value policy and UI-command application helpers.

Summary:
  InputFrameExecution owns the frame sequence. This file supplies the pure or
  synchronous helper operations that translate one sampled input/UI turn into
  typed owner calls without retaining the borrowed owners.

Glossary:
  Attach return pose: The visible camera pose captured before Attach takes over
    so the operator can return to the same view later.
  UI frame result: Bounded facts produced while applying one UI command batch;
    later input phases use them without reaching back into UI widget state.
  Lane R result: Recoverable scene-control or capture failure reported without
    treating the command as successfully applied.

Invariants:
  - Every owner reference is a synchronous borrow and is never retained.
  - Helpers return accepted-command facts; rejected owner work is not reported
    as applied to replay or later frame phases.

Related:
  - SkullbonezSource/Runtime/App/InputFrameExecution.cpp owns the fixed frame sequence.
  - SkullbonezSource/Runtime/Scene/SceneRequestExecution.cpp owns scene-request execution.
  - Agentic/Reference/runtime-reference.md
  - Agentic/Reference/comment-style-guide.md
*/
#include "InputFrame.h"
#include "../Diagnostics/RuntimeOverlayDiagnostics.h"
#include "../Camera/AttachedCameraController.h"
#include "ApplicationExitState.h"
#include "../Diagnostics/DiagnosticsRuntime.h"
#include "../Diagnostics/DiagnosticsPhysicsUI.h"
#include "../Editor/EditorTools.h"
#include "../Input/InputController.Bindings.h"
#include "../Input/InputController.h"
#include "../Replay/ReplayRestoreTransactions.h"
#include "../Direction/DemoDirectorPlayback.h"
#include "../Diagnostics/OverlayDebugState.h"
#include "RunLaunchOptions.h"
#include "RunStartupState.h"
#include "RunTimerState.h"
#include "Window.h"
#include "../Render/RuntimeRenderHost.h"
#include "../Render/RuntimeRenderer.h"
#include "../../Core/Profiler.h"
#include "../Interaction/RuntimeInteractionCommands.h"
#include "../Interaction/OperatorCommandApplier.h"
#include "../Scene/SceneGeneratedControlTransaction.h"
#include "../Scene/SceneCinematicPolicy.h"
#include "../Scene/SceneController.h"
#include "../../Core/Log.h"
#include "../../Physics/ColliderStore.h"
#include "../../Physics/PhysicsBodyStore.h"
#include "../../UI/UILayout.h"
#include "../../UI/UI.h"
#include "../../World/Terrain.h"

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <span>

using namespace SkullbonezCore::Runtime;
using namespace SkullbonezCore::Runtime::ReplayTimelineOperations;
using namespace SkullbonezCore::Math::CollisionDetection;
using namespace SkullbonezCore::Math::Orientation;
using namespace SkullbonezCore::Math::Transformation;
using namespace SkullbonezCore::Physics;
using namespace SkullbonezCore::UI::Layout;
using namespace SkullbonezCore::Runtime::RunInternal;
using SkullbonezCore::Geometry::XZBounds;
using SkullbonezCore::UI::InGameUICommands;
using SkullbonezCore::UI::InGameUIInputResult;

namespace SkullbonezCore
{
namespace Runtime
{
void ReportRuntimeInputFailure( const SkullbonezCore::Core::SbResult& result )
{

    if ( result.ok )
    {
        return;
    }

    std::fprintf( stderr, "%s: %s\n", result.error.owner[0] != '\0' ? result.error.owner : "Runtime/Input",
                  result.error.message[0] != '\0' ? result.error.message : "recoverable input operation failed" );
}

RuntimeInputModeState BuildRuntimeInputModeState( RunCameraMode mode, const RunEditorPlacementState& editor,
                                                  const RuntimeInteractionGesture& gesture, bool attachActiveFollow,
                                                  bool directorGrabbed )
{
    RuntimeInputModeState state;
    state.flyCamera = RunCameraModeUsesFlyControls( mode, attachActiveFollow, directorGrabbed );
    state.launcher = RunCameraModeUsesLauncher( mode );
    state.manipulator = RunCameraModeIsManipulator( mode );
    state.editor = editor.editorModeEnabled;
    state.editorPlacement = editor.placementModeEnabled;
    state.editorViewportLook = editor.viewportLookActive;
    state.editorPlacementScale = gesture.kind == RuntimeInteractionGestureKind::EditorPlacementScaleDrag;
    state.editorGizmoDrag = gesture.kind == RuntimeInteractionGestureKind::GizmoDrag;
    state.editorGizmoRotation = state.editorGizmoDrag && gesture.gizmoKind == RuntimeGizmoDragKind::Rotate;
    state.editorGizmoScale = state.editorGizmoDrag && gesture.gizmoKind == RuntimeGizmoDragKind::Scale;
    return state;
}


PointerPresentationPolicy EvaluateRuntimePointerPresentation( const InputRouter& inputRouter,
                                                              const RunEditorPlacementState& editor,
                                                              const ReplayInputView& replayInput )
{
    const RuntimeInputSnapshot& runtimeSnapshot = inputRouter.RuntimeSnapshot();
    const UiInputHitSnapshot& uiSnapshot = inputRouter.UiSnapshot();
    PointerPresentationPolicyInput input;
    input.editorModeEnabled = editor.editorModeEnabled;
    input.editorViewportLookActive = editor.viewportLookActive;
    input.editorPlacementModeEnabled = editor.placementModeEnabled;
    input.editorPlacementPreviewVisible = editor.placementPreviewVisible;
    input.replayInspectionActive = replayInput.inspectionActive;
    input.replayInspectionLookActive = input.replayInspectionActive && runtimeSnapshot.pointer.rightDown &&
                                       !uiSnapshot.wantsNativeCursor && !uiSnapshot.blocksCameraMouse;

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


uint32_t RuntimeCameraModeEnabledMask( bool authoredScene, int sceneEntityCount )
{
    const bool demoAvailable = !authoredScene && sceneEntityCount > 0;
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


void EnterFlyModeCamera( InputRouter& inputRouter, CameraControlState& camera,
                         SkullbonezCore::Environment::CameraCollection& cameras, bool authoredScene,
                         const RunEditorPlacementState& editor, const ReplayInputView& replayInput )
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

    const PointerPresentationPolicy presentation = EvaluateRuntimePointerPresentation( inputRouter, editor, replayInput );

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


void ExitFlyModeCamera( InputRouter& inputRouter, CameraControlState& camera,
                        SkullbonezCore::Environment::CameraCollection& cameras, SkullbonezCore::Geometry::Terrain& terrain,
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
    include( RuntimeInputBindingContext::Editor, facts.editor );
    include( RuntimeInputBindingContext::EditorInactive, !facts.editor );
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
void RecordRuntimePresentationWaterUIActions( const RuntimePresentationUICommandResult& commands, RecordAction recordAction )
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
void RecordSceneUIActions( const SceneUICommandSubmissionResult& commands, RecordAction recordAction )
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

// Concept: UI sampling and replay workspace arbitration publish the post-UI
// frame before mapped keyboard commands run. The returned commands are fixed
// value records; no callback retains access to the application shell.
RuntimeUIFrameResult BeginRuntimeUIFrame( Window& window, RuntimeFrameInteractionView& interactionOwners,
                                          RunTimerState& timers, SceneController& sceneController,
                                          ReplayRuntime& replayRuntime, const ReplayPathPickInput& replayPointerRay,
                                          const RuntimeInputFrameFacts& facts )
{
    InputRouter& inputRouter = interactionOwners.inputRouter;
    RuntimeInputContext& runtimeInput = inputRouter.RuntimeContext();
    CameraControlState& camera = interactionOwners.camera;
    RuntimeTools& runtimeTools = interactionOwners.runtimeTools;
    AttachedCameraController& attachedCamera = interactionOwners.attachedCamera;
    RuntimeInteractionController& interaction = interactionOwners.interaction;
    SkullbonezCore::UI::InGameUI& ui = interactionOwners.operatorUi;
    RuntimeUIFrameResult result;
    result.suppressWorldActionThisFrame = facts.suppressWorldActionThisFrame || facts.externalUiCapture.mouse;
    result.frameActive = true;

    const int selectedSceneBrowserIndex = ui.SceneNavigation().browser.CurrentIndexForPath( sceneController.CurrentPath() );
    const HWND windowHandle = window.NativeWindowHandle();
    const SkullbonezCore::UI::InputControl::UIInputSnapshot uiInput = BuildUIInputSnapshot( inputRouter.DeviceFrame(),
                                                                                            inputRouter.UiSnapshot().mouse,
                                                                                            ui.InputOverride() );

    InGameUIInputResult
        UIResult = ui.UpdateInput( uiInput, window.ClientWidth(), window.ClientHeight(),
                                   timers.simulationTimer.GetTotalTime(), runtimeTools.Editor().editorModeEnabled,
                                   runtimeTools.Editor().placementModeEnabled, runtimeTools.Editor().placeStaticObject,
                                   runtimeTools.Editor().autoTerrainAlign, static_cast<int>( camera.mode ),
                                   facts.cameraModeEnabledMask,
                                   std::span<const char* const>( ui.SceneNavigation().browser.namePtrs.empty()
                                                                     ? nullptr
                                                                     : ui.SceneNavigation().browser.namePtrs.data(),
                                                                 ui.SceneNavigation().browser.namePtrs.size() ),
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
    result.status = NormalizeLegacyOperatorEditorCommands( result.commands );

    if ( !result.status.ok )
    {
        return result;
    }

    const DeviceInputFrame& deviceFrame = inputRouter.DeviceFrame();
    UiInputHitSnapshot uiSnapshot;
    uiSnapshot.mouse = inputRouter.UiSnapshot().mouse;
    uiSnapshot.clientX = deviceFrame.clientX;
    uiSnapshot.clientY = deviceFrame.clientY;
    uiSnapshot.hasClientPosition = deviceFrame.hasClientPosition;
    uiSnapshot.unhandledWheelDelta = UIResult.unhandledWheelDelta;
    uiSnapshot.userInteracted = result.commands.ui.userInteracted;
    uiSnapshot.blocksKeyboard = ui.BlocksKeyboard() || facts.externalUiCapture.keyboard || facts.externalUiCapture.text;
    uiSnapshot.blocksCameraMouse = ui.BlocksCameraMouse() || facts.externalUiCapture.mouse;
    uiSnapshot.wantsNativeCursor = ui.WantsNativeMouseCursor();
    inputRouter.PublishUiSnapshot( uiSnapshot );
    result.enterInteractiveScene = result.commands.ui.userInteracted;
    result.suppressWorldActionThisFrame = result.suppressWorldActionThisFrame || result.commands.ui.userInteracted;

    // Invariant: replay workspace tools execute during this input turn, before
    // the completed interaction policy exists. Publish current post-UI pointer
    // and key facts now; ProcessInputFrame republishes the final policy facts below.
    inputRouter.PublishRuntimeSnapshot( RuntimeInteractionFrameInput {}, result.suppressWorldActionThisFrame );
    replayRuntime.TickWorkspace( ReplayWorkspaceFrameInput { windowHandle,
                                                             ui.BlocksCameraMouse() || facts.externalUiCapture.mouse,
                                                             facts.legacyDevelopmentUiActive,
                                                             result.editorUnhandledWheelDelta, replayPointerRay,
                                                             facts.replayCurrentCameraMode, facts.replayRestoreCameraMode,
                                                             attachedCamera.State().activeFollow, camera.director.grabbed,
                                                             runtimeTools.Editor().editorModeEnabled,
                                                             sceneController.State().isScenePhysics, ui.IsVisible(),
                                                             ui.IsMinimized(), window.ClientWidth(), window.ClientHeight(),
                                                             timers.simulationTimer.GetTotalTime() },
                                 inputRouter, interaction, sceneController.Scene().Physics(),
                                 sceneController.Scene().Entities(), sceneController.Scene().RenderPresentationRecords(),
                                 &sceneController.Scene().Cameras(), sceneController.Scene().Terrain().Get(), camera,
                                 runtimeTools.MousePickup(), result.replayWorkspace );

    result.enterInteractiveScene = result.enterInteractiveScene || result.replayWorkspace.enterInteractive;
    result.suppressWorldActionThisFrame = result.suppressWorldActionThisFrame || result.replayWorkspace.consumesMouse;
    runtimeInput.BeginFrame( true, ui.BlocksKeyboard() || facts.externalUiCapture.keyboard || facts.externalUiCapture.text,
                             ui.BlocksCameraMouse() || facts.externalUiCapture.mouse ||
                                 result.replayWorkspace.consumesMouse );

    return result;
}

// Lifetime: command application borrows the per-call views synchronously. The
// views are local calling-convention records and are never retained here.
RuntimeUIFrameResult ApplyRuntimeUIFrameCommands( RuntimeUIFrameResult result, bool keyboardToggleEditorMode,
                                                  RuntimeFrameHostView& host, RuntimeFrameInteractionView& interactionOwners,
                                                  RuntimeFrameSceneView& sceneOwners,
                                                  RuntimeFramePresentationView& presentationOwners,
                                                  ReplayRuntime& replayRuntime, const RuntimeInputFrameFacts& facts )
{
    InputRouter& inputRouter = interactionOwners.inputRouter;
    RuntimeInputContext& runtimeInput = inputRouter.RuntimeContext();
    CameraControlState& camera = interactionOwners.camera;
    RuntimeTools& runtimeTools = interactionOwners.runtimeTools;
    AttachedCameraController& attachedCamera = interactionOwners.attachedCamera;
    RuntimeInteractionController& interaction = interactionOwners.interaction;
    SkullbonezCore::UI::InGameUI& ui = interactionOwners.operatorUi;
    RunTimerState& timers = sceneOwners.timers;
    RuntimeOverlayPresentationEdit presentationEdit = sceneOwners.overlays.EditPresentation();
    OverlayDebugState& debug = presentationEdit.State();
    RunLaunchOptions& launchOptions = sceneOwners.launchOptions;
    SkullbonezCore::Core::EngineConfig& config = sceneOwners.config;
    SceneController& sceneController = sceneOwners.sceneController;
    Assets::AssetSystem& assets = host.assets;
    Threading::WorkerPool& workerPool = host.workerPool;
    SimulationSystem& simulation = sceneOwners.simulation;
    RenderDefaultsStore& renderDefaults = presentationOwners.renderDefaults;
    RuntimeRenderer& renderer = presentationOwners.renderer;

    if ( !result.frameActive || !result.status.ok )
    {
        return result;
    }

    // Invariant: the active surface and optional automation/probe intent
    // converge here exactly once. Runtime selection keeps the human surfaces
    // exclusive; arbitration still coalesces exact duplicate injected intent
    // and rejects conflicting payloads through Lane R.
    const SkullbonezCore::UI::OperatorEditorArbitrationResult
        editorCommands = SkullbonezCore::UI::ArbitrateOperatorEditorCommands( result.commands.operatorEditor,
                                                                              facts.externalEditorCommands );

    if ( !editorCommands.status.ok )
    {
        result.status = editorCommands.status;
        return result;
    }

    result.commands.operatorEditor = editorCommands.commands;
    result.status = SkullbonezCore::UI::ProjectOperatorEditorCommands( editorCommands.commands, result.commands );

    if ( !result.status.ok )
    {
        return result;
    }

    const InGameUICommands& uiCommands = result.commands;

    // Concept: operator transport values are normalized with every other
    // editor command, then translated once into replay-domain vocabulary.
    // ReplayRuntime coordinates concrete owners and publishes recoverable
    // feedback; this input boundary retains no timeline or restore authority.

    for ( uint32_t index = 0u; index < editorCommands.commands.replay.count; ++index )
    {
        const SkullbonezCore::UI::OperatorEditorReplayCommand& source = editorCommands.commands.replay.commands[index];

        if ( source.type == SkullbonezCore::UI::OperatorEditorReplayCommandType::SetMemoryPolicy )
        {
            continue;
        }

        ReplayTransportCommand command;
        command.value = source.value;
        command.rowIndex = source.rowIndex;
        command.enabled = source.enabled;

        switch ( source.type )
        {
        case SkullbonezCore::UI::OperatorEditorReplayCommandType::SetRecordingEnabled:
            command.action = ReplayTransportAction::SetRecordingEnabled;
            break;
        case SkullbonezCore::UI::OperatorEditorReplayCommandType::JumpToStart:
            command.action = ReplayTransportAction::JumpToStart;
            break;
        case SkullbonezCore::UI::OperatorEditorReplayCommandType::JumpToEnd:
            command.action = ReplayTransportAction::JumpToEnd;
            break;
        case SkullbonezCore::UI::OperatorEditorReplayCommandType::TogglePlayPause:
            command.action = ReplayTransportAction::TogglePlayPause;
            break;
        case SkullbonezCore::UI::OperatorEditorReplayCommandType::StepBackward:
            command.action = ReplayTransportAction::StepBackward;
            break;
        case SkullbonezCore::UI::OperatorEditorReplayCommandType::StepForward:
            command.action = ReplayTransportAction::StepForward;
            break;
        case SkullbonezCore::UI::OperatorEditorReplayCommandType::SetRevealSpeed:
            command.action = ReplayTransportAction::SetRevealSpeed;
            break;
        case SkullbonezCore::UI::OperatorEditorReplayCommandType::Scrub:
            command.action = ReplayTransportAction::Scrub;
            break;
        case SkullbonezCore::UI::OperatorEditorReplayCommandType::TogglePrediction:
            command.action = ReplayTransportAction::TogglePrediction;
            break;
        case SkullbonezCore::UI::OperatorEditorReplayCommandType::SetPredictionHorizon:
            command.action = ReplayTransportAction::SetPredictionHorizon;
            break;
        case SkullbonezCore::UI::OperatorEditorReplayCommandType::RestoreBranch:
            command.action = ReplayTransportAction::RestoreBranch;
            break;
        case SkullbonezCore::UI::OperatorEditorReplayCommandType::Save:
            command.action = ReplayTransportAction::Save;
            break;
        case SkullbonezCore::UI::OperatorEditorReplayCommandType::Load:
            command.action = ReplayTransportAction::Load;
            break;
        case SkullbonezCore::UI::OperatorEditorReplayCommandType::ReturnToLive:
            command.action = ReplayTransportAction::ReturnToLive;
            break;
        case SkullbonezCore::UI::OperatorEditorReplayCommandType::SelectCauseRow:
            command.action = ReplayTransportAction::SelectCauseRow;
            break;
        case SkullbonezCore::UI::OperatorEditorReplayCommandType::SetMemoryPolicy:
        default:
            continue;
        }

        ReplayWorkspaceOutput transportOutput;
        replayRuntime.ApplyTransportCommand( command,
                                             ReplayTransportHostContext { host.window.NativeWindowHandle(),
                                                                          facts.replayCurrentCameraMode,
                                                                          facts.replayRestoreCameraMode,
                                                                          attachedCamera.State().activeFollow,
                                                                          camera.director.grabbed,
                                                                          timers.simulationTimer.GetTotalTime() },
                                             inputRouter, interaction, &sceneController.Scene().Cameras(),
                                             sceneController.Scene().Terrain().Get(), camera, runtimeTools.MousePickup(),
                                             transportOutput );

        result.enterInteractiveScene = result.enterInteractiveScene || transportOutput.enterInteractive;

        if ( result.replayWorkspace.restoreRequest.kind == ReplayLiveRestoreKind::None &&
             transportOutput.restoreRequest.kind != ReplayLiveRestoreKind::None )
        {
            result.replayWorkspace.restoreRequest = transportOutput.restoreRequest;
        }
    }

    const auto updateInputMode = [&]( RuntimeInputAction action, RuntimeInputActionSource source )
    {
        InputController::ApplyModeAction( runtimeInput,
                                          InputController::ResolveMode( BuildRuntimeInputModeState( camera.mode, runtimeTools.Editor(),
                                                                                                    interaction.Gesture(),
                                                                                                    attachedCamera.State().activeFollow,
                                                                                                    camera.director.grabbed ) ),
                                          action, source );
    };

    const auto recordUIAction = [&updateInputMode]( RuntimeInputAction action )
    { updateInputMode( action, RuntimeInputActionSource::UI ); };

    const auto applyEditorPlacementMode = [&]( bool toggle )
    {
        result.enterInteractiveScene = true;

        const RunInternal::EditorPlacementModeChangeResult
            placementMode = toggle ? RunInternal::ToggleEditorPlacementMode( runtimeTools.Editor(), interaction )
                                   : RunInternal::SetEditorPlacementMode( runtimeTools.Editor(), interaction, true, false );

        inputRouter.SetWorldInteractionOwner( placementMode.worldOwner, InteractionExitReason::EnterEdit, interactionOwners,
                                              sceneController, replayRuntime, facts.replayRestoreCameraMode );

        if ( inputRouter.ReleasePointerToUi( EvaluateRuntimePointerPresentation( inputRouter, runtimeTools.Editor(), replayRuntime.BuildInputView() ) ) )
        {
            InputController::ResetMouseLook( camera );
        }

        inputRouter.ApplyPointerPresentation( EvaluateRuntimePointerPresentation( inputRouter, runtimeTools.Editor(), replayRuntime.BuildInputView() ) );

        updateInputMode( RuntimeInputAction::ToggleEditorTool, RuntimeInputActionSource::UI );
    };

    const auto applyEditorModeToggle = [&]( RuntimeInputActionSource source )
    {
        result.enterInteractiveScene = true;

        const bool enteringEditor = !runtimeTools.Editor().editorModeEnabled;

        if ( enteringEditor )
        {
            const RuntimeInteractionTransition editorTransition = interaction.EnterEdit();
            inputRouter.ApplyInteractionTransition( editorTransition, interactionOwners, sceneController, replayRuntime,
                                                    facts.replayRestoreCameraMode );

            const bool wasFlyMode = RunCameraModeUsesFlyControls( camera.mode, attachedCamera.State().activeFollow,
                                                                  camera.director.grabbed );

            RunInternal::EnterEditorModeState( runtimeTools.Editor(), interaction,
                                               NormalizeRuntimeCameraMode( camera.mode, sceneController.State().isSceneMode,
                                                                           facts.cameraModeEnabledMask ) );

            runtimeTools.CancelMousePickup( inputRouter, interaction );
            camera.mode = RunCameraMode::Inspect;

            if ( !wasFlyMode )
            {
                EnterFlyModeCamera( inputRouter, camera, sceneController.Scene().Cameras(),
                                    sceneController.State().isSceneMode, runtimeTools.Editor(),
                                    replayRuntime.BuildInputView() );
            }
            else
            {
                InputController::ResetMouseLook( camera );
            }
        }
        else
        {
            const RunCameraMode restoreMode = NormalizeRuntimeCameraMode( runtimeTools.Editor().restoreCameraModeAfterEditor,
                                                                          sceneController.State().isSceneMode,
                                                                          facts.cameraModeEnabledMask );

            const RuntimeInteractionTransition restoreTransition = interaction.EnterCameraMode( restoreMode );
            inputRouter.ApplyInteractionTransition( restoreTransition, interactionOwners, sceneController, replayRuntime,
                                                    facts.replayRestoreCameraMode );

            const bool wasFlyMode = RunCameraModeUsesFlyControls( camera.mode, attachedCamera.State().activeFollow,
                                                                  camera.director.grabbed );

            RunInternal::ExitEditorModeState( runtimeTools.Editor(), interaction );
            camera.mode = restoreMode;

            if ( wasFlyMode &&
                 !RunCameraModeUsesFlyControls( camera.mode, attachedCamera.State().activeFollow, camera.director.grabbed ) )
            {
                ExitFlyModeCamera( inputRouter, camera, sceneController.Scene().Cameras(),
                                   *sceneController.Scene().Terrain().Get(), sceneController.State().isSceneMode );
            }
            else
            {
                InputController::ResetMouseLook( camera );
            }
        }

        inputRouter.ApplyPointerPresentation( EvaluateRuntimePointerPresentation( inputRouter, runtimeTools.Editor(), replayRuntime.BuildInputView() ) );

        updateInputMode( RuntimeInputAction::ToggleEditor, source );
    };

    if ( ApplyRenderVsyncUICommand( renderer, &renderer.RenderDevice(), uiCommands.renderer ) )
    {
        recordUIAction( RuntimeInputAction::ToggleVsync );
    }

    const RunCameraModeUICommandResult cameraModeCommand = DecodeRunCameraModeUICommand( uiCommands.run );

    if ( cameraModeCommand.accepted )
    {
        inputRouter.ApplyCameraMode( cameraModeCommand.mode, RuntimeInputActionSource::UI, interactionOwners,
                                     sceneController, replayRuntime, runtimeInput );
    }

    const RunInternal::EditorPlacementPreModeUICommandResult
        editorPreModeCommands = RunInternal::ApplyEditorPlacementPreModeUICommands( runtimeTools.Editor(), interaction,
                                                                                    uiCommands.editor );

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

    const RunInternal::EditorPlacementPostModeUICommandResult
        editorPostModeCommands = RunInternal::ApplyEditorPlacementPostModeUICommands( runtimeTools.Editor(), interaction,
                                                                                      uiCommands.editor );

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

    SceneWorld& editorWorld = sceneController.Scene();

    if ( uiCommands.editor.requestSelectSceneObject && runtimeTools.Editor().editorModeEnabled )
    {
        PhysicsSceneObjectId sceneObjectId;
        sceneObjectId.value = uiCommands.editor.requestedSceneObjectId;
        const int modelIndex = editorWorld.Entities().FindBySceneObjectId( sceneObjectId );
        const SceneEntityRecord* entity = editorWorld.Entities().TryGet( modelIndex );

        if ( entity )
        {
            RuntimeInteractionCommand command;
            command.type = RuntimeInteractionCommandType::SetEditorSelection;
            command.body = entity->body;
            command.collider = editorWorld.Colliders().HandleForSceneObjectId( sceneObjectId );
            command.claimSelectionOwner = false;

            if ( runtimeTools.ApplySelectionCommand( command, editorWorld ) )
            {
                result.enterInteractiveScene = true;
            }
        }
    }

    // Invariant: hierarchy metadata is a live editor concern. Typed secondary
    // commands cannot mutate scene presentation or edit locks while play mode
    // owns the frame, even if a stale packet crosses the mode transition.

    if ( uiCommands.editor.requestSetEntityVisible && runtimeTools.Editor().editorModeEnabled )
    {
        PhysicsSceneObjectId sceneObjectId;
        sceneObjectId.value = uiCommands.editor.visibilitySceneObjectId;
        result.enterInteractiveScene = editorWorld.SetEditorEntityVisible( sceneObjectId,
                                                                           uiCommands.editor.requestedEntityVisible ) ||
                                       result.enterInteractiveScene;
    }

    if ( uiCommands.editor.requestSetEntityLocked && runtimeTools.Editor().editorModeEnabled )
    {
        PhysicsSceneObjectId sceneObjectId;
        sceneObjectId.value = uiCommands.editor.lockSceneObjectId;
        result.enterInteractiveScene = editorWorld.SetEditorEntityLocked( sceneObjectId,
                                                                          uiCommands.editor.requestedEntityLocked ) ||
                                       result.enterInteractiveScene;
    }

    if ( uiCommands.editor.requestDuplicateSelection && runtimeTools.Editor().editorModeEnabled &&
         runtimeTools.DuplicateEditorSelection( editorWorld, sceneController.State() ) )
    {
        result.enterInteractiveScene = true;
    }

    if ( uiCommands.editor.requestDeleteSelection && runtimeTools.Editor().editorModeEnabled &&
         runtimeTools.DeleteEditorSelection( editorWorld, sceneController.State() ) )
    {
        result.enterInteractiveScene = true;
        recordUIAction( RuntimeInputAction::DeleteEditorSelection );
    }

    if ( uiCommands.editor.requestUndo && runtimeTools.Editor().editorModeEnabled &&
         runtimeTools.UndoEditorCommand( sceneController.Scene(), sceneController.State() ) )
    {
        result.enterInteractiveScene = true;
        recordUIAction( RuntimeInputAction::UndoEditor );
    }

    if ( uiCommands.editor.requestRedo && runtimeTools.Editor().editorModeEnabled &&
         runtimeTools.RedoEditorCommand( sceneController.Scene(), sceneController.State() ) )
    {
        result.enterInteractiveScene = true;
        recordUIAction( RuntimeInputAction::RedoEditor );
    }

    if ( uiCommands.scene.toggleCrossScenePause )
    {
        sceneController.ToggleCrossScenePause();
        recordUIAction( RuntimeInputAction::ToggleCrossScenePause );
    }

    // Invariant: the one-frame step request joins the routed Space level later
    // in ProcessInputFrame. It is meaningful only while the scene-flow owner is
    // paused and is never retained as Run business state.
    result.requestSceneStep = uiCommands.scene.requestSingleStep && sceneController.CrossScenePauseLocked();
    const DiagnosticsPhysicsOverlayUICommandResult
        physicsDiagnosticsCommands = ApplyDiagnosticsPhysicsOverlayUICommands( debug, uiCommands.physics );

    if ( physicsDiagnosticsCommands.toggledCollisionVisualizer )
    {
        recordUIAction( RuntimeInputAction::ToggleCollisionVisualizer );
    }

    if ( ApplyPhysicsSleepPolicyUICommand( sceneController.Scene(), uiCommands.physics ) )
    {
        recordUIAction( RuntimeInputAction::TogglePhysicsSleepPolicy );
    }

    RecordDiagnosticsPhysicsOverlayUIActions( physicsDiagnosticsCommands, recordUIAction );
    const TornadoUICommandResult tornadoCommands = ApplyTornadoUICommands( sceneController.Scene(), uiCommands.physics );

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

    if ( ApplySceneFixedStepUICommand( sceneController.State(), simulation, uiCommands.sceneOptions ) )
    {
        recordUIAction( RuntimeInputAction::ToggleFixedStep );
    }

    const RuntimePresentationUICommandResult
        presentationCommands = ApplyRuntimePresentationUICommands( debug, sceneController.State(), config, launchOptions,
                                                                   renderDefaults, true,
                                                                   timers.simulationTimer.GetTimeSinceLastStart(),
                                                                   uiCommands.sceneOptions, uiCommands.renderTuning,
                                                                   uiCommands.water );

    RecordRuntimePresentationUIActions( presentationCommands, recordUIAction );

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
    const RunSimulationUICommandResult runSimulationCommands = ApplyRunSimulationUICommands( sceneController.State(),
                                                                                             ui.SceneNavigation().overrides,
                                                                                             config, workerPool,
                                                                                             uiCommands.sceneOptions,
                                                                                             uiCommands.run,
                                                                                             uiCommands.profiler );

    RecordRunSimulationUIActions( runSimulationCommands, recordUIAction );
    const DiagnosticsPhysicsDebugValueUICommandResult
        physicsDebugValueCommands = ApplyDiagnosticsPhysicsDebugValueUICommands( debug, uiCommands.physics );

    RecordDiagnosticsPhysicsDebugValueUIActions( physicsDebugValueCommands, recordUIAction );
    const RayCastLauncherTuningUICommandResult rayCastLauncherCommands = runtimeTools.ApplyRayCastLauncherTuningUICommands( uiCommands.physics );

    if ( rayCastLauncherCommands.setImpulseStrength )
    {
        replayRuntime.SubmitEvent( ReplayEventCommandOperations::BuildLauncherConfig( rayCastLauncherCommands.impulseConfigChangedFlags,
                                                                                      rayCastLauncherCommands.impulseConfigImpulseStrength,
                                                                                      rayCastLauncherCommands.impulseConfigProjectileSpeed ) );

        recordUIAction( RuntimeInputAction::SetRayCastImpulseStrength );
    }

    if ( rayCastLauncherCommands.setProjectileSpeed )
    {
        replayRuntime.SubmitEvent( ReplayEventCommandOperations::BuildLauncherConfig( rayCastLauncherCommands.projectileConfigChangedFlags,
                                                                                      rayCastLauncherCommands.projectileConfigImpulseStrength,
                                                                                      rayCastLauncherCommands.projectileConfigProjectileSpeed ) );

        recordUIAction( RuntimeInputAction::SetLauncherProjectileSpeed );
    }

    const PhysicsFrictionUICommandResult physicsFrictionCommands = ApplyPhysicsFrictionUICommands( config,
                                                                                                   sceneController.Scene(),
                                                                                                   uiCommands.physics );

    RecordPhysicsFrictionUIActions( physicsFrictionCommands, recordUIAction );
    const auto executeSceneGeneratedControlAction = [&]( const SceneGeneratedControlAction& action )
    {

        if ( action.resetReplayTimeline )
        {
            const ReplaySceneTimelineResetInput
                reset = DescribeReplaySceneTimeline( sceneController, ui.SceneNavigation().overrides,
                                                     sceneController.State(), facts.sceneObjectCapacity,
                                                     static_cast<uint32_t>( launchOptions.generatedObjectTypeOverride ) );

            replayRuntime.ResetSceneTimeline( reset, inputRouter, interaction, &sceneController.Scene().Cameras(),
                                              sceneController.Scene().Terrain().Get(), camera, facts.replayRestoreCameraMode,
                                              attachedCamera.State().activeFollow, camera.director.grabbed );
        }

        if ( action.scheduleProfileReset )
        {
            PROFILE_SCHEDULE_RESET( host.profiler );
        }
    };

    SceneGeneratedControlTransaction
        modelCountTransaction = SceneGeneratedControlTransaction::ModelCount( uiCommands.sceneOptions.requestedModelCount,
                                                                              launchOptions.generatedObjectTypeOverride,
                                                                              facts.sceneObjectCapacity );

    const SceneGeneratedUICommandResult modelCountCommand = modelCountTransaction.Execute( config, sceneController,
                                                                                           ui.SceneNavigation().overrides,
                                                                                           camera, simulation, runtimeTools,
                                                                                           &renderer.RenderFrame() );

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

    SceneGeneratedControlTransaction solverBallCountTransaction = SceneGeneratedControlTransaction::
        SolverBallCount( uiCommands.run.requestedSolverBallCount, launchOptions.generatedObjectTypeOverride,
                         facts.sceneObjectCapacity );

    const SceneGeneratedUICommandResult solverBallCountCommand = solverBallCountTransaction
                                                                     .Execute( config, sceneController,
                                                                               ui.SceneNavigation().overrides, camera,
                                                                               simulation, runtimeTools,
                                                                               &renderer.RenderFrame() );

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

    SceneGeneratedControlTransaction
        solverBoxCountTransaction = SceneGeneratedControlTransaction::SolverBoxCount( uiCommands.run.requestedSolverBoxCount,
                                                                                      launchOptions
                                                                                          .generatedObjectTypeOverride,
                                                                                      facts.sceneObjectCapacity );

    const SceneGeneratedUICommandResult solverBoxCountCommand = solverBoxCountTransaction
                                                                    .Execute( config, sceneController,
                                                                              ui.SceneNavigation().overrides, camera,
                                                                              simulation, runtimeTools,
                                                                              &renderer.RenderFrame() );

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

    WorldOverrideChange worldOverride;

    if ( ApplyWorldWaterUICommands( sceneController.Scene().Environment(), uiCommands.water, worldOverride ) )
    {
        replayRuntime.SubmitEvent( ReplayEventCommandOperations::BuildWorldOverride( worldOverride.previousGravity,
                                                                                     worldOverride.previousFluidHeight,
                                                                                     worldOverride.previousFluidDensity, worldOverride.gravity,
                                                                                     worldOverride.fluidHeight, worldOverride.fluidDensity ) );

        recordUIAction( RuntimeInputAction::ApplyWorldWaterSettings );
    }

    SkullbonezCore::Core::CinematicRenderConfig& activeCinematic = ActiveSceneCinematicConfig( sceneController.State(),
                                                                                               config );

    if ( ApplyCinematicRenderingToggleUICommand( launchOptions, sceneController.State(), activeCinematic,
                                                 uiCommands.cinematic ) )
    {
        recordUIAction( RuntimeInputAction::ToggleCinematicRendering );
    }

    if ( QueueCinematicSkyDefaultsUICommand( renderDefaults, uiCommands.cinematic ) )
    {
        recordUIAction( RuntimeInputAction::SaveSkyDefaults );
    }

    if ( HasCinematicModeUICommand( uiCommands.cinematic ) )
    {
        result.enterInteractiveScene = true;
        ApplyCinematicModeUICommand( launchOptions, sceneController, ui.SceneNavigation().browser, assets, activeCinematic,
                                     renderDefaults.CinematicBaseline(), uiCommands.cinematic );

        recordUIAction( RuntimeInputAction::SelectCinematicScene );
    }

    const CinematicTuningUICommandResult cinematicTuningCommands = ApplyCinematicTuningUICommands( launchOptions,
                                                                                                   sceneController.State(),
                                                                                                   activeCinematic,
                                                                                                   uiCommands.cinematic );

    RecordCinematicTuningUIActions( cinematicTuningCommands, recordUIAction );
    const SceneUICommandSubmissionResult sceneUICommands = sceneController.SubmitUIRequests( uiCommands.scene );

    if ( !sceneUICommands.status.ok )
    {
        result.status = sceneUICommands.status;
        return result;
    }

    RecordSceneUIActions( sceneUICommands, recordUIAction );

    return result;
}

RuntimeUIFrameResult FinishRuntimeUIFramePointer( RuntimeUIFrameResult result,
                                                  RuntimeFrameInteractionView& interactionOwners,
                                                  SceneController& sceneController, ReplayRuntime& replayRuntime,
                                                  RunCameraMode replayCurrentCameraMode )
{
    InputRouter& inputRouter = interactionOwners.inputRouter;
    RuntimeInputContext& runtimeInput = inputRouter.RuntimeContext();
    CameraControlState& camera = interactionOwners.camera;
    RuntimeTools& runtimeTools = interactionOwners.runtimeTools;
    RuntimeInteractionController& interaction = interactionOwners.interaction;
    AttachedCameraController& attachedCamera = interactionOwners.attachedCamera;
    SkullbonezCore::UI::InGameUI& ui = interactionOwners.operatorUi;

    // Invariant: pointer ownership is finalized only after UI mutations and
    // stress actions succeed; failure leaves later world routing untouched.

    if ( !result.frameActive || !result.status.ok )
    {
        return result;
    }

    const auto updateInputMode = [&]( RuntimeInputAction action, RuntimeInputActionSource source )
    {
        InputController::ApplyModeAction( runtimeInput,
                                          InputController::ResolveMode( BuildRuntimeInputModeState( camera.mode, runtimeTools.Editor(),
                                                                                                    interaction.Gesture(),
                                                                                                    attachedCamera.State().activeFollow,
                                                                                                    camera.director.grabbed ) ),
                                          action, source );
    };

    if ( attachedCamera.ApplyOrbitInput( sceneController.Scene(), RunCameraModeIsAttached( replayCurrentCameraMode ),
                                         result.editorUnhandledWheelDelta, ui.BlocksCameraMouse() ) )
    {
        result.enterInteractiveScene = true;
    }

    const DeviceInputFrame& editorDevice = inputRouter.DeviceFrame();
    const EditorViewportPlacementResult editorPointerResult = runtimeTools.RouteEditorViewportPlacement( { result.editorUnhandledWheelDelta, editorDevice.rightDown, editorDevice.leftDown,
                                                                                                           editorDevice.keys.IsDown( VK_CONTROL ), ui.BlocksCameraMouse(), editorDevice.hasClientPosition,
                                                                                                           runtimeInput.CurrentMode() == RuntimeInputMode::EditorViewportLook, interaction.Gesture().kind,
                                                                                                           editorDevice.clientX, editorDevice.clientY } );

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
    const ReplayInputView replayInput = replayRuntime.BuildInputView();
    presentationInput.replayInspectionActive = replayInput.inspectionActive;
    presentationInput.replayInspectionLookActive = presentationInput.replayInspectionActive && editorDevice.rightDown &&
                                                   !presentationUi.wantsNativeCursor && !presentationUi.blocksCameraMouse;

    inputRouter.RequestCursorVisible( !inputRouter.EvaluatePointerPresentation( presentationInput ).hideNativeCursor );
    return result;
}

} // namespace Runtime
} // namespace SkullbonezCore
