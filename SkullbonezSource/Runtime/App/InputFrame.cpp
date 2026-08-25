/*
File: InputFrame.cpp
Purpose:
  Implements shared input value policy and UI-command application helpers.

Summary:
  InputFrameExecution owns the frame sequence. This file normalizes one sampled
  input/UI turn, walks the operator-command transaction around existing owner
  barriers, and supplies synchronous helpers for the remaining typed calls.

Glossary:
  Attach return pose: The visible camera pose captured before Attach takes over
    so the operator can return to the same view later.
  UI frame result: Bounded facts produced while applying one UI command batch;
    later input phases use them without reaching back into UI widget state.

Invariants:
  - Every owner reference is a synchronous borrow and is never retained.
  - Helpers return accepted-command facts; rejected owner work is not reported
    as applied to replay or later frame phases.
  - Operator-command completion precedes scene-request submission.

Related:
  - SkullbonezSource/Runtime/App/InputFrameExecution.cpp owns the fixed frame sequence.
  - SkullbonezSource/Runtime/Interaction/OperatorCommandTransaction.h
  - SkullbonezSource/Runtime/App/SceneLoadApplication.cpp owns scene-request execution.
  - Agentic/Reference/runtime-reference.md
  - Agentic/Reference/engine-glossary.md
*/
#include "InputFrame.h"
#include "Run.h"
#include "OperatorCommandBoundaryPolicy.h"
#include "../Diagnostics/RuntimeOverlayDiagnostics.h"
#include "../Automation/InteractionRecordingBrowser.h"
#include "../Scene/AttachedCameraController.h"
#include "ApplicationExitState.h"
#include "../Diagnostics/DiagnosticsRuntime.h"
#include "../Diagnostics/DiagnosticsPhysicsUI.h"
#include "../Editor/EditorTools.h"
#include "../Input/InputController.Bindings.h"
#include "../Input/InputController.h"
#include "../Replay/ReplayRestoreTransactions.h"
#include "../Direction/DemoDirectorPlayback.h"
#include "../Diagnostics/OverlayDebugState.h"
#include "../Startup/RunLaunchOptions.h"
#include "../Startup/RunStartupState.h"
#include "../Diagnostics/RuntimeFrameMetricsOwner.h"
#include "../Startup/Window.h"
#include "../Render/RuntimeRenderHost.h"
#include "../Render/RuntimeRenderer.h"
#include "../Render/RenderDefaultsStore.h"
#include "../../Core/Profiler.h"
#include "../Interaction/RuntimeInteractionCommands.h"
#include "../Interaction/OperatorCommandTransaction.h"
#include "../Scene/SceneGeneratedControlTransaction.h"
#include "../Scene/SceneCinematicPolicy.h"
#include "../Scene/SceneController.h"
#include "../../Core/Log.h"
#include "../../Physics/ColliderStore.h"
#include "../../Physics/PhysicsBodyStore.h"
#include "../../UI/UILayout.h"
#include "../UI/GameUI/UI.h"
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
using SkullbonezCore::Geometry::XZBounds;
using SkullbonezCore::UI::InGameUICommands;
using SkullbonezCore::UI::InGameUIInputResult;

namespace SkullbonezCore
{
namespace Runtime
{
void ReportRuntimeInputFailure( const SkullbonezCore::Core::SbResult& result )
{
    if ( result.Ok() )
    {
        return;
    }

    std::fprintf( stderr, "%s: %s\n", result.ErrorOwner()[0] != '\0' ? result.ErrorOwner() : "Runtime/Input",
                  result.ErrorMessage()[0] != '\0' ? result.ErrorMessage() : "recoverable input operation failed" );
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
void RecordTornadoToggleUIActions( const OperatorCommandAcceptanceLedger& commands, RecordAction recordAction )
{
    if ( commands.toggledTornado )
    {
        recordAction( RuntimeInputAction::ToggleTornado );
    }

    if ( commands.toggledTornadoVisualShell )
    {
        recordAction( RuntimeInputAction::ToggleTornadoVisualShell );
    }

    if ( commands.toggledTornadoFieldVectors )
    {
        recordAction( RuntimeInputAction::ToggleTornadoFieldVectors );
    }
}

template <typename RecordAction>
void RecordTornadoApplySettingsUIActions( const OperatorCommandAcceptanceLedger& commands, RecordAction recordAction )
{
    for ( int actionIndex = 0; actionIndex < commands.tornadoApplySettingsActionCount; ++actionIndex )
    {
        recordAction( RuntimeInputAction::ApplyTornadoSettings );
    }
}

template <typename RecordAction>
void RecordRuntimePresentationUIActions( const OperatorCommandAcceptanceLedger& commands, RecordAction recordAction )
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
void RecordRuntimePresentationWaterUIActions( const OperatorCommandAcceptanceLedger& commands, RecordAction recordAction )
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
void RecordRunSimulationUIActions( const OperatorCommandAcceptanceLedger& commands, RecordAction recordAction )
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
void RecordPhysicsFrictionUIActions( const OperatorCommandAcceptanceLedger& commands, RecordAction recordAction )
{
    for ( int actionIndex = 0; actionIndex < commands.frictionApplySettingsActionCount; ++actionIndex )
    {
        recordAction( RuntimeInputAction::ApplyPhysicsFrictionSettings );
    }
}

template <typename RecordAction>
void RecordCinematicTuningUIActions( const OperatorCommandAcceptanceLedger& commands, RecordAction recordAction )
{
    if ( commands.toggledCinematicFeature )
    {
        recordAction( RuntimeInputAction::ToggleCinematicFeature );
    }

    if ( commands.appliedCinematicParam )
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
RuntimeUIFrameResult Run::BeginRuntimeUIFrame( const ReplayPathPickInput& replayPointerRay,
                                               const RuntimeInputFrameFacts& facts )
{
    RuntimeInputContext& runtimeInput = m_inputRouter.RuntimeContext();
    RuntimeUIFrameResult result;
    result.suppressWorldActionThisFrame = facts.suppressWorldActionThisFrame || facts.externalUiCapture.mouse;
    result.frameActive = true;

    m_operatorUi->SceneNavigation().browser.selectedSceneIndex = m_operatorUi->SceneNavigation().browser.CurrentIndexForPath( m_sceneController.CurrentPath() );
    const HWND windowHandle = m_window.NativeWindowHandle();
    const SkullbonezCore::UI::InputControl::UIInputSnapshot uiInput = BuildUIInputSnapshot( m_inputRouter.DeviceFrame(),
                                                                                            m_inputRouter.UiSnapshot().mouse,
                                                                                            m_operatorUi->InputOverride() );

    InGameUIInputResult UIResult = m_operatorUi->UpdateInput( uiInput, m_window.ClientWidth(), m_window.ClientHeight(),
                                                              m_timers.SimulationTotalSeconds(),
                                                              m_editorTools.Editor().editorModeEnabled,
                                                              m_editorTools.Editor().placementModeEnabled,
                                                              m_editorTools.Editor().placeStaticObject,
                                                              m_editorTools.Editor().autoTerrainAlign,
                                                              static_cast<int>( m_camera.mode ),
                                                              facts.cameraModeEnabledMask );

    switch ( UIResult.nativeMouseCapture )
    {
    case InGameUIInputResult::NativeMouseCaptureRequest::Acquire:
        m_inputRouter.RequestNativeCapture();
        break;
    case InGameUIInputResult::NativeMouseCaptureRequest::Release:
        m_inputRouter.ReleaseNativeCapture();
        break;
    case InGameUIInputResult::NativeMouseCaptureRequest::Unchanged:
    default:
        break;
    }

    result.editorUnhandledWheelDelta = UIResult.unhandledWheelDelta;
    result.commands = UIResult.commands;
    result.status = NormalizeGameUiOperatorEditorCommands( m_resultDiagnostics, result.commands );

    if ( !result.status.Ok() )
    {
        return result;
    }

    const DeviceInputFrame& deviceFrame = m_inputRouter.DeviceFrame();
    UiInputHitSnapshot uiSnapshot;
    uiSnapshot.mouse = m_inputRouter.UiSnapshot().mouse;
    uiSnapshot.clientX = deviceFrame.clientX;
    uiSnapshot.clientY = deviceFrame.clientY;
    uiSnapshot.hasClientPosition = deviceFrame.hasClientPosition;
    uiSnapshot.unhandledWheelDelta = UIResult.unhandledWheelDelta;
    uiSnapshot.userInteracted = result.commands.ui.userInteracted;
    uiSnapshot.blocksKeyboard = m_operatorUi->BlocksKeyboard() || facts.externalUiCapture.keyboard ||
                                facts.externalUiCapture.text;
    uiSnapshot.blocksCameraMouse = m_operatorUi->BlocksCameraMouse() || facts.externalUiCapture.mouse;
    uiSnapshot.wantsNativeCursor = m_operatorUi->WantsNativeMouseCursor();
    m_inputRouter.PublishUiSnapshot( uiSnapshot );
    result.enterInteractiveScene = result.commands.ui.userInteracted;
    result.suppressWorldActionThisFrame = result.suppressWorldActionThisFrame || result.commands.ui.userInteracted;

    // Invariant: replay workspace tools execute during this input turn, before
    // the completed interaction policy exists. Publish current post-UI pointer
    // and key facts now; RunInputPhase republishes the final policy facts below.
    m_inputRouter.PublishRuntimeSnapshot( RuntimeInteractionFrameInput {}, result.suppressWorldActionThisFrame );
    m_replayRuntime
        .TickWorkspace( ReplayWorkspaceFrameInput { windowHandle,
                                                    m_operatorUi->BlocksCameraMouse() || facts.externalUiCapture.mouse,
                                                    facts.gameUiActive, result.editorUnhandledWheelDelta, replayPointerRay,
                                                    facts.replayCurrentCameraMode, facts.replayRestoreCameraMode,
                                                    m_attachedCamera.State().activeFollow, m_camera.director.grabbed,
                                                    m_editorTools.Editor().editorModeEnabled,
                                                    m_sceneController.State().isScenePhysics, m_operatorUi->IsVisible(),
                                                    m_operatorUi->IsMinimized(),
                                                    m_inputRouter.DeviceFrame().keys.IsDown( VK_SPACE ),
                                                    m_window.ClientWidth(), m_window.ClientHeight(),
                                                    m_camera.mouseRadiansPerPixel, m_timers.SimulationTotalSeconds(),
                                                    facts.requestedReplayCauseRow },
                        m_inputRouter, m_interaction, m_sceneController.Scene(), m_camera, m_attachedCamera,
                        m_runtimeTools.MousePickup(), result.replayWorkspace );

    result.enterInteractiveScene = result.enterInteractiveScene || result.replayWorkspace.enterInteractive;
    result.suppressWorldActionThisFrame = result.suppressWorldActionThisFrame || result.replayWorkspace.consumesMouse;
    runtimeInput.BeginFrame( true,
                             m_operatorUi->BlocksKeyboard() || facts.externalUiCapture.keyboard ||
                                 facts.externalUiCapture.text || result.replayWorkspace.consumesKeyboard,
                             m_operatorUi->BlocksCameraMouse() || facts.externalUiCapture.mouse ||
                                 result.replayWorkspace.consumesMouse );

    return result;
}

// Lifetime: command application borrows composed owners synchronously through
// the Run coordinator; no owner is retained by a delegated operation.
RuntimeUIFrameResult Run::ApplyInputCommandsPhase( RuntimeUIFrameResult result, bool keyboardToggleEditorMode,
                                                   const RuntimeInputFrameFacts& facts )
{
    InputRouter& inputRouter = m_inputRouter;
    RuntimeInputContext& runtimeInput = inputRouter.RuntimeContext();
    CameraControlState& camera = m_camera;
    EditorToolsOwner& editorTools = m_editorTools;
    RuntimeTools& runtimeTools = m_runtimeTools;
    AttachedCameraController& attachedCamera = m_attachedCamera;
    RuntimeInteractionController& interaction = m_interaction;
    SkullbonezCore::UI::InGameUI& ui = *m_operatorUi;
    RuntimeFrameMetricsOwner& timers = m_timers;
    RuntimeOverlayPresentationEdit presentationEdit = m_overlayDiagnostics->EditPresentation();
    OverlayDebugState& debug = presentationEdit.State();
    RunLaunchOptions& launchOptions = m_launchOptions;
    SkullbonezCore::Core::EngineConfig& config = m_config;
    SceneController& sceneController = m_sceneController;
    Assets::AssetSystem& assets = m_assets;
    Threading::WorkerPool& workerPool = m_workerPool;
    SimulationSystem& simulation = m_simulation;
    RenderDefaultsStore& renderDefaults = m_renderDefaults;
    RuntimeRenderer& renderer = Renderer();
    ReplayRuntime& replayRuntime = m_replayRuntime;
    ContinuousOrbitalForecast& continuousForecast = m_continuousForecast;

    if ( !result.frameActive || !result.status.Ok() )
    {
        return result;
    }

    // Invariant: the active surface and optional automation/probe intent
    // converge here exactly once. Runtime selection keeps the human surfaces
    // exclusive; arbitration still coalesces exact duplicate injected intent
    // and rejects conflicting payloads through recoverable result.
    const SkullbonezCore::UI::OperatorEditorArbitrationResult
        editorCommands = SkullbonezCore::UI::ArbitrateOperatorEditorCommands( m_resultDiagnostics,
                                                                              result.commands.operatorEditor,
                                                                              facts.externalEditorCommands );

    if ( !editorCommands.status.Ok() )
    {
        result.status = editorCommands.status;
        return result;
    }

    result.commands.operatorEditor = editorCommands.commands;
    result.status = SkullbonezCore::UI::ProjectOperatorEditorCommands( m_resultDiagnostics, editorCommands.commands,
                                                                       result.commands );

    if ( !result.status.Ok() )
    {
        return result;
    }

    const int requestedRecording = result.commands.scene.requestedInteractionRecordingIndex;

    if ( requestedRecording >= 0 )
    {
        SkullbonezCore::UI::InteractionRecordingBrowserState& recordings = ui.SceneNavigation().recordings;
        result.status = LaunchInteractionRecording( m_resultDiagnostics, recordings, requestedRecording );

        if ( !result.status.Ok() )
        {
            return result;
        }

        recordings.selectedIndex = requestedRecording;
        result.commands.scene.requestedInteractionRecordingIndex = -1;
    }

    // Invariant: either operator surface may submit off-grid shared-policy or
    // render-catalog floats. App snaps those rows before their owner dispatch;
    // separately owned Replay reveal/horizon commands are intentionally absent.
    OperatorCommandBoundaryPolicy::NormalizeOperatorCommands( result.commands );
    const InGameUICommands& uiCommands = result.commands;
    OperatorCommandTransaction operatorCommands( uiCommands );
    const OperatorCommandAcceptanceLedger& operatorAcceptance = operatorCommands.Acceptance();

    const auto applyReplayTransport = [&]( const ReplayTransportCommand& command )
    {
        ReplayWorkspaceOutput transportOutput;
        replayRuntime.ApplyTransportCommand( command,
                                             ReplayTransportHostContext { m_window.NativeWindowHandle(),
                                                                          facts.replayCurrentCameraMode,
                                                                          facts.replayRestoreCameraMode,
                                                                          attachedCamera.State().activeFollow,
                                                                          camera.director.grabbed,
                                                                          timers.SimulationTotalSeconds() },
                                             inputRouter, interaction, &sceneController.Scene().Cameras(),
                                             sceneController.Scene().Terrain().Get(), camera, runtimeTools.MousePickup(),
                                             transportOutput );

        result.enterInteractiveScene = result.enterInteractiveScene || transportOutput.enterInteractive;

        if ( result.replayWorkspace.restoreRequest.kind == ReplayLiveRestoreKind::None &&
             transportOutput.restoreRequest.kind != ReplayLiveRestoreKind::None )
        {
            result.replayWorkspace.restoreRequest = transportOutput.restoreRequest;
            result.replayWorkspace.planningTransitionToken = transportOutput.planningTransitionToken;
        }
    };

    // Concept: Replay transport values pass the shared editor-command
    // validation and arbitration boundary, then translate once into Replay
    // vocabulary. Their reveal and horizon semantics remain owner-specific;
    // they do not participate in App shared-policy normalization.
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

            if ( !replayRuntime.BuildInputView().predictionEnabled )
            {
                continuousForecast.Stop();
            }

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

        applyReplayTransport( command );
    }

    // Invariant: bounded PREDICT and the continuous private forecast never own
    // speculative workers at the same time. App resolves this cross-owner mode
    // transition before either producer is advanced later in the frame.
    for ( uint32_t index = 0u; index < editorCommands.commands.forecast.count; ++index )
    {
        const SkullbonezCore::UI::OperatorEditorForecastCommand& command = editorCommands.commands.forecast.commands[index];

        if ( command.type == SkullbonezCore::UI::OperatorEditorForecastCommandType::Exit )
        {
            continuousForecast.Stop();
            continue;
        }

        const ContinuousOrbitalForecastView current = continuousForecast.View();

        if ( command.type == SkullbonezCore::UI::OperatorEditorForecastCommandType::ToggleContinuous &&
             ( current.active || current.workerInFlight ) )
        {
            continuousForecast.Stop();
            continue;
        }

        if ( replayRuntime.BuildInputView().predictionEnabled )
        {
            ReplayTransportCommand disablePrediction;
            disablePrediction.action = ReplayTransportAction::TogglePrediction;
            applyReplayTransport( disablePrediction );
        }

        if ( !replayRuntime.BuildInputView().predictionEnabled )
        {
            const SceneWorld& scene = sceneController.Scene();
            const Scene::OrbitalStabilityContract& contract = scene.OrbitalStabilityContract();
            std::array<ContinuousOrbitalPresentationMember, Scene::ORBITAL_STABILITY_MEMBER_CAPACITY> presentationMembers = {};
            std::size_t presentationMemberCount = 0u;
            const Physics::PhysicsBodyStore& bodies = Physics::PhysicsEngine::ReadBodies( scene.Physics() );

            for ( std::size_t memberIndex = 0u; memberIndex < contract.memberCount; ++memberIndex )
            {
                const Physics::PhysicsSceneObjectId id = contract.members[memberIndex].sceneObjectId;
                const int bodyRow = bodies.ModelIndexForHandle( bodies.HandleForSceneObjectId( id ) );
                const int entityRow = scene.Entities().FindBySceneObjectId( id );

                if ( bodyRow < 0 || entityRow < 0 )
                {
                    presentationMemberCount = 0u;
                    break;
                }

                const Rendering::RenderMaterial& material = scene.Entities().At( entityRow ).renderMaterial;
                presentationMembers[presentationMemberCount++] = { static_cast<std::size_t>( bodyRow ), id.value,
                                                                   material.baseColor[0], material.baseColor[1],
                                                                   material.baseColor[2] };
            }

            const std::span<const ContinuousOrbitalPresentationMember> presentationView( presentationMembers.data(),
                                                                                         presentationMemberCount );
            (void)( command.type == SkullbonezCore::UI::OperatorEditorForecastCommandType::Reset
                        ? continuousForecast.Reset( scene.Physics(), scene.Tornado(), config,
                                                    scene.Environment().GetPhysicsWorldForces(), workerPool, contract,
                                                    presentationView )
                        : continuousForecast.Start( scene.Physics(), scene.Tornado(), config,
                                                    scene.Environment().GetPhysicsWorldForces(), workerPool, contract,
                                                    presentationView ) );
        }
    }

    const auto updateInputMode = [&]( RuntimeInputAction action, RuntimeInputActionSource source )
    {
        InputController::ApplyModeAction( runtimeInput,
                                          InputController::ResolveMode( BuildRuntimeInputModeState( camera.mode, editorTools.Editor(),
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

        const EditorPlacementModeChangeResult placementMode = toggle ? ToggleEditorPlacementMode( editorTools.Editor(),
                                                                                                  interaction )
                                                                     : SetEditorPlacementMode( editorTools.Editor(),
                                                                                               interaction, true, false );

        inputRouter.SetWorldInteractionOwner( placementMode.worldOwner, InteractionExitReason::EnterEdit, editorTools,
                                              runtimeTools, interaction, attachedCamera, camera, sceneController,
                                              replayRuntime, facts.replayRestoreCameraMode );

        if ( inputRouter.ReleasePointerToUi( EvaluateRuntimePointerPresentation( inputRouter, editorTools.Editor(), replayRuntime.BuildInputView() ) ) )
        {
            InputController::ResetMouseLook( camera );
        }

        inputRouter.ApplyPointerPresentation( EvaluateRuntimePointerPresentation( inputRouter, editorTools.Editor(), replayRuntime.BuildInputView() ) );

        updateInputMode( RuntimeInputAction::ToggleEditorTool, RuntimeInputActionSource::UI );
    };

    const auto applyEditorModeToggle = [&]( RuntimeInputActionSource source )
    {
        result.enterInteractiveScene = true;

        const bool enteringEditor = !editorTools.Editor().editorModeEnabled;

        if ( enteringEditor )
        {
            const RuntimeInteractionTransition editorTransition = interaction.EnterEdit();
            inputRouter.ApplyInteractionTransition( editorTransition, editorTools, runtimeTools, interaction, attachedCamera,
                                                    camera, sceneController, replayRuntime, facts.replayRestoreCameraMode );

            const bool wasFlyMode = RunCameraModeUsesFlyControls( camera.mode, attachedCamera.State().activeFollow,
                                                                  camera.director.grabbed );

            EnterEditorModeState( editorTools.Editor(), interaction,
                                  NormalizeRuntimeCameraMode( camera.mode, sceneController.State().isSceneMode,
                                                              facts.cameraModeEnabledMask ) );

            runtimeTools.CancelMousePickup( inputRouter, interaction );
            camera.mode = RunCameraMode::Inspect;

            if ( !wasFlyMode )
            {
                EnterFlyModeCamera( inputRouter, camera, sceneController.Scene().Cameras(),
                                    sceneController.State().isSceneMode, editorTools.Editor(),
                                    replayRuntime.BuildInputView() );
            }
            else
            {
                InputController::ResetMouseLook( camera );
            }
        }
        else
        {
            const RunCameraMode restoreMode = NormalizeRuntimeCameraMode( editorTools.Editor().restoreCameraModeAfterEditor,
                                                                          sceneController.State().isSceneMode,
                                                                          facts.cameraModeEnabledMask );

            const RuntimeInteractionTransition restoreTransition = EnterInteractionForCameraMode( interaction, restoreMode );
            inputRouter.ApplyInteractionTransition( restoreTransition, editorTools, runtimeTools, interaction,
                                                    attachedCamera, camera, sceneController, replayRuntime,
                                                    facts.replayRestoreCameraMode );

            const bool wasFlyMode = RunCameraModeUsesFlyControls( camera.mode, attachedCamera.State().activeFollow,
                                                                  camera.director.grabbed );

            ExitEditorModeState( editorTools.Editor(), interaction );
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

        inputRouter.ApplyPointerPresentation( EvaluateRuntimePointerPresentation( inputRouter, editorTools.Editor(), replayRuntime.BuildInputView() ) );

        updateInputMode( RuntimeInputAction::ToggleEditor, source );
    };

    operatorCommands.ApplyDeviceAndMode( renderer, renderer.RenderDevice() );

    if ( operatorAcceptance.toggledVsync )
    {
        recordUIAction( RuntimeInputAction::ToggleVsync );
    }

    if ( operatorAcceptance.cameraModeAccepted )
    {
        inputRouter.ApplyCameraMode( static_cast<RunCameraMode>( operatorAcceptance.cameraModeIndex ),
                                     RuntimeInputActionSource::UI, editorTools, runtimeTools, interaction, attachedCamera,
                                     camera, sceneController, replayRuntime, runtimeInput );
    }

    const EditorPlacementPreModeUICommandResult
        editorPreModeCommands = ApplyEditorPlacementPreModeUICommands( editorTools.Editor(), interaction,
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

    const EditorPlacementPostModeUICommandResult
        editorPostModeCommands = ApplyEditorPlacementPostModeUICommands( editorTools.Editor(), interaction,
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

    if ( uiCommands.editor.requestSelectSceneObject && editorTools.Editor().editorModeEnabled )
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

            if ( editorTools.ApplySelectionCommand( command, editorWorld ) )
            {
                result.enterInteractiveScene = true;
            }
        }
    }

    // Invariant: hierarchy metadata is a live editor concern. Typed secondary
    // commands cannot mutate scene presentation or edit locks while play mode
    // owns the frame, even if a stale packet crosses the mode transition.
    if ( uiCommands.editor.requestSetEntityVisible && editorTools.Editor().editorModeEnabled )
    {
        PhysicsSceneObjectId sceneObjectId;
        sceneObjectId.value = uiCommands.editor.visibilitySceneObjectId;
        result.enterInteractiveScene = editorWorld.SetEditorEntityVisible( sceneObjectId,
                                                                           uiCommands.editor.requestedEntityVisible ) ||
                                       result.enterInteractiveScene;
    }

    if ( uiCommands.editor.requestSetEntityLocked && editorTools.Editor().editorModeEnabled )
    {
        PhysicsSceneObjectId sceneObjectId;
        sceneObjectId.value = uiCommands.editor.lockSceneObjectId;
        result.enterInteractiveScene = editorWorld.SetEditorEntityLocked( sceneObjectId,
                                                                          uiCommands.editor.requestedEntityLocked ) ||
                                       result.enterInteractiveScene;
    }

    if ( uiCommands.editor.requestDuplicateSelection && editorTools.Editor().editorModeEnabled &&
         editorTools.DuplicateEditorSelection( editorWorld, sceneController.State() ) )
    {
        result.enterInteractiveScene = true;
    }

    if ( uiCommands.editor.requestDeleteSelection && editorTools.Editor().editorModeEnabled &&
         editorTools.DeleteEditorSelection( editorWorld, sceneController.State() ) )
    {
        result.enterInteractiveScene = true;
        recordUIAction( RuntimeInputAction::DeleteEditorSelection );
    }

    if ( uiCommands.editor.requestUndo && editorTools.Editor().editorModeEnabled &&
         editorTools.UndoEditorCommand( sceneController.Scene(), sceneController.State() ) )
    {
        result.enterInteractiveScene = true;
        recordUIAction( RuntimeInputAction::UndoEditor );
    }

    if ( uiCommands.editor.requestRedo && editorTools.Editor().editorModeEnabled &&
         editorTools.RedoEditorCommand( sceneController.Scene(), sceneController.State() ) )
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
    // in RunInputPhase. It is meaningful only while the scene-flow owner is
    // paused and is never retained as Run business state.
    result.requestSceneStep = uiCommands.scene.requestSingleStep && sceneController.CrossScenePauseLocked();
    const DiagnosticsPhysicsOverlayUICommandResult
        physicsDiagnosticsCommands = ApplyDiagnosticsPhysicsOverlayUICommands( debug, uiCommands.physics );

    if ( physicsDiagnosticsCommands.toggledCollisionVisualizer )
    {
        recordUIAction( RuntimeInputAction::ToggleCollisionVisualizer );
    }

    operatorCommands.ApplyPhysicsControl( sceneController.Scene() );

    if ( operatorAcceptance.toggledPhysicsSleepPolicy )
    {
        recordUIAction( RuntimeInputAction::TogglePhysicsSleepPolicy );
    }

    RecordDiagnosticsPhysicsOverlayUIActions( physicsDiagnosticsCommands, recordUIAction );
    RecordTornadoToggleUIActions( operatorAcceptance, recordUIAction );

    if ( runtimeTools.ApplyRayCastVisualizationUICommand( uiCommands.physics ) )
    {
        recordUIAction( RuntimeInputAction::ToggleRayCastVisualization );
    }

    RecordTornadoApplySettingsUIActions( operatorAcceptance, recordUIAction );

    if ( ApplyDiagnosticsTerrainContactProbeUICommand( debug, uiCommands.physics ) )
    {
        recordUIAction( RuntimeInputAction::ToggleTerrainContactProbe );
    }

    const SimulationPacingPolicy previousPacing = ResolveSimulationPacingPolicy( launchOptions.fixedStep,
                                                                                 sceneController.State().isFixedStep,
                                                                                 sceneController.State().targetFrameCount,
                                                                                 sceneController.State().isInteractiveRun );
    operatorCommands.ApplyRuntimePresentation( debug, sceneController.State(), config, launchOptions, renderDefaults, true,
                                               timers.SceneElapsedSeconds() );

    if ( operatorAcceptance.toggledFixedStep &&
         previousPacing != ResolveSimulationPacingPolicy( launchOptions.fixedStep, sceneController.State().isFixedStep,
                                                          sceneController.State().targetFrameCount,
                                                          sceneController.State().isInteractiveRun ) )
    {
        // Why: live and interactive scenes ignore the fixed-step capture request.
        // Preserve fractional wall-clock time when effective pacing did not change.
        simulation.Reset();
    }

    if ( operatorAcceptance.toggledTextOnly )
    {
        recordUIAction( RuntimeInputAction::ToggleTextOnly );
    }

    if ( operatorAcceptance.toggledFixedStep )
    {
        recordUIAction( RuntimeInputAction::ToggleFixedStep );
    }

    RecordRuntimePresentationUIActions( operatorAcceptance, recordUIAction );

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

    if ( uiCommands.physics.requestPredictionRevealRate )
    {
        // Why: the reveal rate is authored on the Physics tab but owned by
        // ReplayPrediction, which Runtime/Interaction may not include. The
        // composition root applies it here instead of widening that edge.
        replayRuntime.ApplyPredictionRevealRate( uiCommands.physics.requestedPredictionRevealRate );
    }

    RecordRuntimePresentationWaterUIActions( operatorAcceptance, recordUIAction );
    operatorCommands.ApplySimulationPolicy( sceneController.State(), ui.SceneNavigation().overrides, config, workerPool );
    RecordRunSimulationUIActions( operatorAcceptance, recordUIAction );
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

    operatorCommands.ApplyPhysicsMaterial( config, sceneController.Scene() );
    RecordPhysicsFrictionUIActions( operatorAcceptance, recordUIAction );
    const auto executeSceneGeneratedControlAction = [&]( const SceneGeneratedControlAction& action )
    {
        if ( action.clearToolRayHistory )
        {
            runtimeTools.ClearRayCastTestLines();
        }

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
            PROFILE_SCHEDULE_RESET( m_profiler );
        }
    };

    SceneGeneratedControlTransaction
        modelCountTransaction = SceneGeneratedControlTransaction::ModelCount( uiCommands.sceneOptions.requestedModelCount,
                                                                              launchOptions.generatedObjectTypeOverride,
                                                                              facts.sceneObjectCapacity );

    const SceneGeneratedUICommandResult modelCountCommand = modelCountTransaction.Execute( config, sceneController,
                                                                                           ui.SceneNavigation().overrides,
                                                                                           camera, simulation,
                                                                                           &renderer.RenderFrame() );

    if ( !modelCountCommand.action.status.Ok() )
    {
        result.status = modelCountCommand.action.status;
        return result;
    }

    if ( modelCountCommand.accepted )
    {
        executeSceneGeneratedControlAction( modelCountCommand.action );
        recordUIAction( RuntimeInputAction::SetModelCount );
    }

    if ( operatorAcceptance.setWorkerThreads )
    {
        recordUIAction( RuntimeInputAction::SetWorkerThreads );
    }

    SceneGeneratedControlTransaction solverBallCountTransaction = SceneGeneratedControlTransaction::
        SolverBallCount( uiCommands.run.requestedSolverBallCount, launchOptions.generatedObjectTypeOverride,
                         facts.sceneObjectCapacity );

    const SceneGeneratedUICommandResult solverBallCountCommand = solverBallCountTransaction
                                                                     .Execute( config, sceneController,
                                                                               ui.SceneNavigation().overrides, camera,
                                                                               simulation, &renderer.RenderFrame() );

    if ( !solverBallCountCommand.action.status.Ok() )
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
                                                                              simulation, &renderer.RenderFrame() );

    if ( !solverBoxCountCommand.action.status.Ok() )
    {
        result.status = solverBoxCountCommand.action.status;
        return result;
    }

    if ( solverBoxCountCommand.accepted )
    {
        executeSceneGeneratedControlAction( solverBoxCountCommand.action );
        recordUIAction( RuntimeInputAction::SetSolverCounts );
    }

    operatorCommands.ApplyWorldPolicy( sceneController.Scene().Environment() );

    if ( operatorAcceptance.worldOverrideAccepted )
    {
        const Environment::WorldOverrideChange& worldOverride = operatorAcceptance.worldOverride;
        replayRuntime.SubmitEvent( ReplayEventCommandOperations::BuildWorldOverride( worldOverride.previousGravity,
                                                                                     worldOverride.previousFluidHeight,
                                                                                     worldOverride.previousFluidDensity, worldOverride.gravity,
                                                                                     worldOverride.fluidHeight, worldOverride.fluidDensity ) );

        recordUIAction( RuntimeInputAction::ApplyWorldWaterSettings );
    }

    SkullbonezCore::Core::CinematicRenderConfig& activeCinematic = ActiveSceneCinematicConfig( sceneController.State(),
                                                                                               config );

    operatorCommands.ApplyCinematicPolicy( launchOptions, sceneController, ui.SceneNavigation().browser, assets,
                                           activeCinematic, renderDefaults.CinematicBaseline(), renderDefaults );

    if ( operatorAcceptance.toggledCinematicRendering )
    {
        recordUIAction( RuntimeInputAction::ToggleCinematicRendering );
    }

    if ( operatorAcceptance.queuedCinematicSkyDefaultsSave )
    {
        recordUIAction( RuntimeInputAction::SaveSkyDefaults );
    }

    if ( operatorAcceptance.selectedCinematicMode )
    {
        result.enterInteractiveScene = true;
        recordUIAction( RuntimeInputAction::SelectCinematicScene );
    }

    RecordCinematicTuningUIActions( operatorAcceptance, recordUIAction );
    operatorCommands.Complete();
    const SceneUICommandSubmissionResult sceneUICommands = sceneController.SubmitUIRequests( uiCommands.scene );

    if ( !sceneUICommands.status.Ok() )
    {
        result.status = sceneUICommands.status;
        return result;
    }

    RecordSceneUIActions( sceneUICommands, recordUIAction );

    return result;
}

RuntimeUIFrameResult FinishRuntimeUIFramePointer( RuntimeUIFrameResult result, InputRouter& inputRouter,
                                                  CameraControlState& camera, EditorToolsOwner& editorTools,
                                                  RuntimeInteractionController& interaction,
                                                  AttachedCameraController& attachedCamera, SkullbonezCore::UI::InGameUI& ui,
                                                  SceneController& sceneController, ReplayRuntime& replayRuntime,
                                                  RunCameraMode replayCurrentCameraMode )
{
    RuntimeInputContext& runtimeInput = inputRouter.RuntimeContext();

    // Invariant: pointer ownership is finalized only after UI mutations and
    // stress actions succeed; failure leaves later world routing untouched.
    if ( !result.frameActive || !result.status.Ok() )
    {
        return result;
    }

    const auto updateInputMode = [&]( RuntimeInputAction action, RuntimeInputActionSource source )
    {
        InputController::ApplyModeAction( runtimeInput,
                                          InputController::ResolveMode( BuildRuntimeInputModeState( camera.mode, editorTools.Editor(),
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
    const EditorViewportPlacementResult editorPointerResult = editorTools.RouteEditorViewportPlacement( { result.editorUnhandledWheelDelta, editorDevice.rightDown, editorDevice.leftDown,
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
    presentationInput.editorModeEnabled = editorTools.Editor().editorModeEnabled;
    presentationInput.editorViewportLookActive = editorTools.Editor().viewportLookActive;
    presentationInput.editorPlacementModeEnabled = editorTools.Editor().placementModeEnabled;
    presentationInput.editorPlacementPreviewVisible = editorTools.Editor().placementPreviewVisible;
    const ReplayInputView replayInput = replayRuntime.BuildInputView();
    presentationInput.replayInspectionActive = replayInput.inspectionActive;
    presentationInput.replayInspectionLookActive = presentationInput.replayInspectionActive && editorDevice.rightDown &&
                                                   !presentationUi.wantsNativeCursor && !presentationUi.blocksCameraMouse;

    inputRouter.RequestCursorVisible( !inputRouter.EvaluatePointerPresentation( presentationInput ).hideNativeCursor );
    return result;
}

} // namespace Runtime
} // namespace SkullbonezCore
