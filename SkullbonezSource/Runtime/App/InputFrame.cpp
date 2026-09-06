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
#include <type_traits>

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

    m_operatorUi->SceneNavigation().browser.selectedSceneIndex = m_operatorUi->SceneNavigation().browser.CurrentIndexForPath(
        m_sceneController.CurrentPath() );
    const HWND windowHandle = m_window.NativeWindowHandle();
    const SkullbonezCore::UI::InputControl::UIInputSnapshot uiInput = BuildUIInputSnapshot( m_inputRouter.DeviceFrame(),
                                                                                            m_inputRouter.UiSnapshot().mouse,
                                                                                            m_operatorUi->InputOverride() );

    const RunEditorPlacementState& editor = m_editorTools.Editor();
    InGameUIInputResult UIResult = m_operatorUi->UpdateInput( uiInput, m_window.ClientWidth(), m_window.ClientHeight(),
                                                              m_timers.SimulationTotalSeconds(), editor.editorModeEnabled,
                                                              editor.placementModeEnabled, editor.placeStaticObject,
                                                              editor.autoTerrainAlign, facts.cameraModeEnabledMask );

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

SkullbonezCore::UI::OperatorEditorArbitrationResult Run::PrepareOperatorInputCommands( RuntimeUIFrameResult& result,
                                                                                       const RuntimeInputFrameFacts& facts )
{
    SkullbonezCore::UI::OperatorEditorArbitrationResult
        editorCommands = SkullbonezCore::UI::ArbitrateOperatorEditorCommands( m_resultDiagnostics,
                                                                              result.commands.operatorEditor,
                                                                              facts.externalEditorCommands );
    if ( !editorCommands.status.Ok() )
    {
        result.status = editorCommands.status;
        return editorCommands;
    }
    result.commands.operatorEditor = editorCommands.commands;
    result.status = SkullbonezCore::UI::ProjectOperatorEditorCommands( m_resultDiagnostics, editorCommands.commands,
                                                                       result.commands );
    if ( !result.status.Ok() )
    {
        return editorCommands;
    }

    const int requestedRecording = result.commands.scene.requestedInteractionRecordingIndex;
    if ( requestedRecording >= 0 )
    {
        SkullbonezCore::UI::InteractionRecordingBrowserState& recordings = m_operatorUi->SceneNavigation().recordings;
        result.status = LaunchInteractionRecording( m_resultDiagnostics, recordings, requestedRecording );
        if ( !result.status.Ok() )
        {
            return editorCommands;
        }
        recordings.selectedIndex = requestedRecording;
        result.commands.scene.requestedInteractionRecordingIndex = -1;
    }
    OperatorCommandBoundaryPolicy::NormalizeOperatorCommands( result.commands );
    return editorCommands;
}

void Run::ApplyReplayTransportCommand( RuntimeUIFrameResult& result, const RuntimeInputFrameFacts& facts,
                                       const ReplayTransportCommand& command )
{
    ReplayWorkspaceOutput transportOutput;
    const double now = m_timers.SimulationTotalSeconds();
    std::visit(
        [&]( const auto& typedCommand )
        {
            using Command = std::remove_cvref_t<decltype( typedCommand )>;

            if constexpr ( std::is_same_v<Command, ReplaySetRecordingEnabledCommand> ||
                           std::is_same_v<Command, ReplaySetRevealSpeedCommand> ||
                           std::is_same_v<Command, ReplaySetPredictionHorizonCommand> ||
                           std::is_same_v<Command, ReplaySetRagdollVisualsEnabledCommand> ||
                           std::is_same_v<Command, ReplaySetPastPathVisibleCommand> ||
                           std::is_same_v<Command, ReplaySetCauseInspectorOpenCommand> )
            {
                m_replayRuntime.ApplyTransportCommand( typedCommand, now );
            }
            else if constexpr ( std::is_same_v<Command, ReplayJumpToStartCommand> ||
                                std::is_same_v<Command, ReplayJumpToEndCommand> ||
                                std::is_same_v<Command, ReplayStepBackwardCommand> ||
                                std::is_same_v<Command, ReplayStepForwardCommand> ||
                                std::is_same_v<Command, ReplayScrubCommand> ||
                                std::is_same_v<Command, ReplayTogglePredictionCommand> ||
                                std::is_same_v<Command, ReplayRestoreBranchCommand> ||
                                std::is_same_v<Command, ReplaySelectCauseRowCommand> )
            {
                m_replayRuntime.ApplyTransportCommand( typedCommand, m_interaction, now, transportOutput );
            }
            else if constexpr ( std::is_same_v<Command, ReplayTogglePlayPauseCommand> ||
                                std::is_same_v<Command, ReplaySetVelocityEditEnabledCommand> )
            {
                m_replayRuntime.ApplyTransportCommand( typedCommand, m_inputRouter, m_interaction, m_camera, now,
                                                       transportOutput );
            }
            else if constexpr ( std::is_same_v<Command, ReplaySetPredictionDetailModeCommand> )
            {
                m_replayRuntime.ApplyTransportCommand( typedCommand, &m_sceneController.Scene().Cameras(),
                                                       m_sceneController.Scene().Terrain().Get(), m_camera,
                                                       facts.replayRestoreCameraMode, m_attachedCamera.State().activeFollow,
                                                       m_camera.director.grabbed, m_interaction, m_inputRouter, now );
            }
            else if constexpr ( std::is_same_v<Command, ReplaySaveCommand> )
            {
                m_replayRuntime.ApplyTransportCommand( typedCommand, now, transportOutput );
            }
            else if constexpr ( std::is_same_v<Command, ReplayLoadCommand> )
            {
                const ReplayTransportLoadResult load = m_replayRuntime.BeginTransportLoad( typedCommand,
                                                                                           m_window.NativeWindowHandle(),
                                                                                           now );

                if ( load.activateLoadedPresentation )
                {
                    m_replayRuntime.ActivateLoadedTransport( &m_sceneController.Scene().Cameras(),
                                                             m_sceneController.Scene().Terrain().Get(), m_camera,
                                                             facts.replayCurrentCameraMode, facts.replayRestoreCameraMode,
                                                             m_attachedCamera.State().activeFollow,
                                                             m_camera.director.grabbed, m_interaction, m_inputRouter,
                                                             m_runtimeTools.MousePickup(), now );
                }
            }
            else if constexpr ( std::is_same_v<Command, ReplayReturnToLiveCommand> )
            {
                m_replayRuntime.ApplyTransportCommand( typedCommand, &m_sceneController.Scene().Cameras(),
                                                       m_sceneController.Scene().Terrain().Get(), m_camera,
                                                       facts.replayRestoreCameraMode, m_attachedCamera.State().activeFollow,
                                                       m_camera.director.grabbed, m_interaction, m_inputRouter, now,
                                                       transportOutput );
            }
            else
            {
                static_assert( std::is_same_v<Command, void>,
                               "Every ReplayTransportCommand alternative requires an explicit App composition path." );
            }
        },
        command );
    result.enterInteractiveScene = result.enterInteractiveScene || transportOutput.enterInteractive;
    if ( result.replayWorkspace.restoreRequest.kind == ReplayLiveRestoreKind::None &&
         transportOutput.restoreRequest.kind != ReplayLiveRestoreKind::None )
    {
        result.replayWorkspace.restoreRequest = transportOutput.restoreRequest;
        result.replayWorkspace.planningTransitionToken = transportOutput.planningTransitionToken;
    }
}

void Run::ApplyReplayOperatorCommands( RuntimeUIFrameResult& result, const RuntimeInputFrameFacts& facts,
                                       const SkullbonezCore::UI::OperatorEditorCommandQueues& commands )
{
    // Concept: Replay transport values pass the shared editor-command
    // validation and arbitration boundary, then translate once into Replay
    // vocabulary. Their reveal and horizon semantics remain owner-specific;
    // they do not participate in App shared-policy normalization.
    // ReplayRuntime coordinates concrete owners and publishes recoverable
    // feedback; this input boundary retains no timeline or restore authority.
    for ( uint32_t index = 0u; index < commands.replay.count; ++index )
    {
        const SkullbonezCore::UI::OperatorEditorReplayCommand& source = commands.replay.commands[index];

        if ( source.type == SkullbonezCore::UI::OperatorEditorReplayCommandType::SetMemoryPolicy )
        {
            continue;
        }

        switch ( source.type )
        {
        case SkullbonezCore::UI::OperatorEditorReplayCommandType::SetRecordingEnabled:
            ApplyReplayTransportCommand( result, facts, ReplaySetRecordingEnabledCommand { source.enabled } );
            break;
        case SkullbonezCore::UI::OperatorEditorReplayCommandType::JumpToStart:
            ApplyReplayTransportCommand( result, facts, ReplayJumpToStartCommand {} );
            break;
        case SkullbonezCore::UI::OperatorEditorReplayCommandType::JumpToEnd:
            ApplyReplayTransportCommand( result, facts, ReplayJumpToEndCommand {} );
            break;
        case SkullbonezCore::UI::OperatorEditorReplayCommandType::TogglePlayPause:
            ApplyReplayTransportCommand( result, facts, ReplayTogglePlayPauseCommand {} );
            break;
        case SkullbonezCore::UI::OperatorEditorReplayCommandType::StepBackward:
            ApplyReplayTransportCommand( result, facts, ReplayStepBackwardCommand {} );
            break;
        case SkullbonezCore::UI::OperatorEditorReplayCommandType::StepForward:
            ApplyReplayTransportCommand( result, facts, ReplayStepForwardCommand {} );
            break;
        case SkullbonezCore::UI::OperatorEditorReplayCommandType::SetRevealSpeed:
            ApplyReplayTransportCommand( result, facts, ReplaySetRevealSpeedCommand { source.value } );
            break;
        case SkullbonezCore::UI::OperatorEditorReplayCommandType::Scrub:
            ApplyReplayTransportCommand( result, facts, ReplayScrubCommand { source.value } );
            break;
        case SkullbonezCore::UI::OperatorEditorReplayCommandType::TogglePrediction:
            ApplyReplayTransportCommand( result, facts, ReplayTogglePredictionCommand {} );

            if ( !m_replayRuntime.BuildInputView().predictionEnabled )
            {
                m_continuousForecast.Stop();
            }

            break;
        case SkullbonezCore::UI::OperatorEditorReplayCommandType::SetPredictionHorizon:
            ApplyReplayTransportCommand( result, facts, ReplaySetPredictionHorizonCommand { source.value } );
            break;
        case SkullbonezCore::UI::OperatorEditorReplayCommandType::RestoreBranch:
            ApplyReplayTransportCommand( result, facts, ReplayRestoreBranchCommand {} );
            break;
        case SkullbonezCore::UI::OperatorEditorReplayCommandType::Save:
            ApplyReplayTransportCommand( result, facts, ReplaySaveCommand {} );
            break;
        case SkullbonezCore::UI::OperatorEditorReplayCommandType::Load:
            ApplyReplayTransportCommand( result, facts, ReplayLoadCommand {} );
            break;
        case SkullbonezCore::UI::OperatorEditorReplayCommandType::ReturnToLive:
            ApplyReplayTransportCommand( result, facts, ReplayReturnToLiveCommand {} );
            break;
        case SkullbonezCore::UI::OperatorEditorReplayCommandType::SelectCauseRow:
            ApplyReplayTransportCommand( result, facts, ReplaySelectCauseRowCommand { source.rowIndex } );
            break;
        case SkullbonezCore::UI::OperatorEditorReplayCommandType::SetMemoryPolicy:
        default:
            continue;
        }
    }
}

#if defined( SKULLBONEZ_SKARNESS )
namespace
{
bool ResolveSkarnessSceneObject( SceneWorld& world, const SkarnessCommand& command, int& modelIndex, const char*& reason )
{
    modelIndex = -1;

    if ( command.unsignedInteger != 0u )
    {
        if ( command.unsignedInteger > UINT32_MAX )
        {
            reason = "sceneObjectId exceeds the runtime identity range";
            return false;
        }

        modelIndex = world.Entities().FindBySceneObjectId(
            Physics::PhysicsSceneObjectId { static_cast<uint32_t>( command.unsignedInteger ) } );
    }
    else
    {
        for ( int entityIndex = 0; entityIndex < world.SceneEntityCount(); ++entityIndex )
        {
            if ( std::strcmp( world.Entities().At( entityIndex ).displayName, command.text.c_str() ) != 0 )
            {
                continue;
            }

            if ( modelIndex >= 0 )
            {
                reason = "display name is ambiguous; supply sceneObjectId";
                return false;
            }

            modelIndex = entityIndex;
        }
    }

    const Physics::PhysicsBodyRecord* body = modelIndex >= 0 ? world.BodyStore().RecordForModelIndex( modelIndex ) : nullptr;

    if ( !body || !body->sceneObjectId.IsValid() )
    {
        reason = "scene object did not resolve to a physics body";
        return false;
    }

    return true;
}

SkarnessSceneObjectResult BuildSkarnessSceneObjectResult( const SceneWorld& world, int modelIndex )
{
    SkarnessSceneObjectResult result;
    const Physics::PhysicsBodyRecord* body = world.BodyStore().RecordForModelIndex( modelIndex );

    if ( body )
    {
        result.sceneObjectId = body->sceneObjectId.value;
        result.modelRow = modelIndex;
        result.name = world.Entities().At( modelIndex ).displayName;
    }

    return result;
}

void SetSkarnessNumberResult( SkarnessCommandApplication& application, const char* name, double value )
{
    application.result.valueName = name;
    application.result.numberValue = value;
    application.result.hasNumberValue = true;
}

void SetSkarnessIntegerResult( SkarnessCommandApplication& application, const char* name, int value )
{
    application.result.valueName = name;
    application.result.integerValue = value;
    application.result.hasIntegerValue = true;
}

void SetSkarnessUnsignedResult( SkarnessCommandApplication& application, const char* name, uint64_t value )
{
    application.result.valueName = name;
    application.result.unsignedValue = value;
    application.result.hasUnsignedValue = true;
}
} // namespace

bool Run::ApplySkarnessCameraCommand( const SkarnessCommand& command, const char*& reason )
{
    const ReplayCauseInspectionMode mode = m_replayRuntime.CauseInspectionView().Transport().mode;

    if ( !ReplayCauseInspectionAcceptsOrbit( mode ) )
    {
        reason = "causal inspection camera is not ready for orbit input";
        return false;
    }

    const bool applied = m_attachedCamera.TickFocusedInspection( m_sceneController.Scene(),
                                                                 static_cast<float>( command.number ),
                                                                 static_cast<float>( command.secondNumber ),
                                                                 command.integer );
    reason = applied ? "" : "causal inspection orbit could not resolve its selected pivot";
    return applied;
}

bool Run::ApplySkarnessPredictionTargetCommand( const SkarnessCommand& command, const char*& reason )
{
    SceneWorld& world = m_sceneController.Scene();
    int modelIndex = -1;

    if ( !ResolveSkarnessSceneObject( world, command, modelIndex, reason ) )
    {
        return false;
    }

    const Physics::PhysicsBodyRecord* body = world.BodyStore().RecordForModelIndex( modelIndex );

    ReplayFrameIntent intent;
    intent.setPathTarget = true;
    intent.pathTargetId = body->sceneObjectId;
    intent.pathTargetModelRow.value = modelIndex;
    strncpy_s( intent.pathTargetName, sizeof( intent.pathTargetName ), world.Entities().At( modelIndex ).displayName,
               _TRUNCATE );
    (void)m_replayRuntime.ApplyFrameIntent( intent );
    (void)m_interaction.SetWorldInteractionOwnerInWorkspace( RuntimeWorkspace::Replay,
                                                             WorldInteractionOwner::ReplayPrediction,
                                                             InteractionExitReason::EnterReplay );
    return true;
}

void Run::ApplySkarnessReplayCommand( const SkarnessCommand& command, RuntimeUIFrameResult& frameResult,
                                      const RuntimeInputFrameFacts& facts, SkarnessCommandApplication& application )
{
    application.handled = true;

    switch ( command.type )
    {
    case SkarnessCommandType::ReplaySetRecordingEnabled:
        ApplyReplayTransportCommand( frameResult, facts, ReplaySetRecordingEnabledCommand { command.enabled } );
        application.result.valueName = "enabled";
        application.result.boolValue = m_replayRuntime.BuildInputView().captureEnabled;
        application.result.hasBoolValue = true;
        return;
    case SkarnessCommandType::ReplaySetRetentionSeconds:
    case SkarnessCommandType::ReplaySetMemoryBudgetMiB:
    {
        ReplayMemoryPolicyRequest request;
        request.retentionSeconds = command.type == SkarnessCommandType::ReplaySetRetentionSeconds ? command.integer : -1;
        request.budgetMiB = command.type == SkarnessCommandType::ReplaySetMemoryBudgetMiB ? command.integer : -1;
        (void)m_replayRuntime.ApplyMemoryPolicyRequest( request );
        const ReplayHudStatus status = m_replayRuntime.BuildHudStatus( false );
        SetSkarnessIntegerResult( application,
                                  command.type == SkarnessCommandType::ReplaySetRetentionSeconds ? "seconds" : "mib",
                                  command.type == SkarnessCommandType::ReplaySetRetentionSeconds
                                      ? status.requestedRetentionSeconds
                                      : status.requestedBudgetMiB );
        return;
    }
    case SkarnessCommandType::ReplayJumpToStart:
        ApplyReplayTransportCommand( frameResult, facts, ReplayJumpToStartCommand {} );
        return;
    case SkarnessCommandType::ReplayJumpToEnd:
        ApplyReplayTransportCommand( frameResult, facts, ReplayJumpToEndCommand {} );
        return;
    case SkarnessCommandType::ReplaySetPlaybackPaused:
        if ( m_replayRuntime.BuildInputView().liveAdvanceHeld != command.enabled )
        {
            ApplyReplayTransportCommand( frameResult, facts, ReplayTogglePlayPauseCommand {} );
        }
        return;
    case SkarnessCommandType::ReplayStepBackward:
        ApplyReplayTransportCommand( frameResult, facts, ReplayStepBackwardCommand {} );
        return;
    case SkarnessCommandType::ReplayStepForward:
        ApplyReplayTransportCommand( frameResult, facts, ReplayStepForwardCommand {} );
        return;
    case SkarnessCommandType::ReplaySetRevealSpeed:
        ApplyReplayTransportCommand( frameResult, facts,
                                     ReplaySetRevealSpeedCommand { static_cast<float>( command.number ) } );
        SetSkarnessNumberResult( application, "rate", m_replayRuntime.BuildHudStatus( false ).predictionRevealRate );
        return;
    case SkarnessCommandType::ReplayScrub:
        ApplyReplayTransportCommand( frameResult, facts, ReplayScrubCommand { static_cast<float>( command.number ) } );
        SetSkarnessNumberResult( application, "normalized",
                                 m_replayRuntime.BuildInputView().activeTrack == RunReplayTrack::Solver
                                     ? m_replayRuntime.BuildInputView().solverTrackPosition
                                     : m_replayRuntime.BuildInputView().presentationTrackPosition );
        return;
    case SkarnessCommandType::ReplaySeekFrame:
    {
        ReplayWorkspaceOutput output;
        ReplayFrameIndex appliedFrame = 0u;
        application.applied = m_replayRuntime.SeekReplayFrame( command.unsignedInteger, m_interaction,
                                                               m_timers.SimulationTotalSeconds(), output, appliedFrame );
        application.reason = application.applied ? nullptr : "active replay track has no retained frames";
        frameResult.enterInteractiveScene = frameResult.enterInteractiveScene || output.enterInteractive;
        SetSkarnessUnsignedResult( application, "frame", appliedFrame );
        return;
    }
    case SkarnessCommandType::ReplaySetPredictionEnabled:
        if ( m_replayRuntime.BuildInputView().predictionEnabled != command.enabled )
        {
            ApplyReplayTransportCommand( frameResult, facts, ReplayTogglePredictionCommand {} );
        }
        return;
    case SkarnessCommandType::ReplaySetPredictionDetailMode:
        if ( m_replayRuntime.BuildSkarnessState().predictionHighDetail != command.enabled )
        {
            ApplyReplayTransportCommand( frameResult, facts, ReplaySetPredictionDetailModeCommand { command.enabled } );
        }
        return;
    case SkarnessCommandType::ReplaySetPredictionHorizon:
        ApplyReplayTransportCommand( frameResult, facts,
                                     ReplaySetPredictionHorizonCommand { static_cast<float>( command.number ) } );
        SetSkarnessNumberResult( application, "seconds", m_replayRuntime.BuildSkarnessState().predictionHorizonSeconds );
        return;
    case SkarnessCommandType::ReplaySetVelocityEditEnabled:
        ApplyReplayTransportCommand( frameResult, facts, ReplaySetVelocityEditEnabledCommand { command.enabled } );
        return;
    case SkarnessCommandType::ReplaySetRagdollVisualsEnabled:
        ApplyReplayTransportCommand( frameResult, facts, ReplaySetRagdollVisualsEnabledCommand { command.enabled } );
        return;
    case SkarnessCommandType::ReplaySetPastPathVisible:
        ApplyReplayTransportCommand( frameResult, facts, ReplaySetPastPathVisibleCommand { command.enabled } );
        return;
    case SkarnessCommandType::ReplaySetGuideArcsEnabled:
        m_replayRuntime.SetGuideArcsEnabled( command.enabled );
        return;
    case SkarnessCommandType::ReplaySetPathColorMode:
        m_replayRuntime.SetPathColorMode( command.text == "velocity" ? ReplayPathColorMode::VelocityHeat
                                          : command.text == "time"   ? ReplayPathColorMode::TimeGradient
                                          : command.text == "object" ? ReplayPathColorMode::PerObjectHue
                                          : command.text == "causal" ? ReplayPathColorMode::CausalDepth
                                                                     : ReplayPathColorMode::LaneFlat );
        return;
    case SkarnessCommandType::ReplaySetInterceptTarget:
    {
        SceneWorld& world = m_sceneController.Scene();
        int modelIndex = -1;
        application.applied = ResolveSkarnessSceneObject( world, command, modelIndex, application.reason );

        if ( application.applied )
        {
            const Physics::PhysicsBodyRecord* body = world.BodyStore().RecordForModelIndex( modelIndex );
            ReplayFrameIntent intent;
            intent.setInterceptTarget = true;
            intent.interceptTargetId = body->sceneObjectId;
            intent.interceptTargetModelRow.value = modelIndex;
            (void)m_replayRuntime.ApplyFrameIntent( intent );
            application.result.objects.push_back( BuildSkarnessSceneObjectResult( world, modelIndex ) );
        }
        return;
    }
    case SkarnessCommandType::ReplayVelocityPreview:
        application.applied = m_replayRuntime
                                  .PreviewVelocity( m_sceneController.Scene().Physics(),
                                                    Math::Vector::Vector3( static_cast<float>( command.number ),
                                                                           static_cast<float>( command.secondNumber ),
                                                                           static_cast<float>( command.thirdNumber ) ),
                                                    Math::Vector::Vector3( static_cast<float>( command.fourthNumber ),
                                                                           static_cast<float>( command.fifthNumber ),
                                                                           static_cast<float>( command.sixthNumber ) ) );
        application.reason = application.applied ? nullptr : "velocity preview requires a live selected physics body";
        return;
    case SkarnessCommandType::ReplayVelocityCommit:
        application.applied = m_replayRuntime.CommitVelocityPreview();
        application.reason = application.applied ? nullptr : "no changed velocity preview is active";
        return;
    case SkarnessCommandType::ReplayVelocityCancel:
        application.applied = m_replayRuntime.CancelVelocityPreview( m_sceneController.Scene().Physics() );
        application.reason = application.applied ? nullptr : "no velocity preview is active";
        return;
    case SkarnessCommandType::PredictionRevealReset:
        SetSkarnessUnsignedResult( application, "frame", m_replayRuntime.ResetDeterministicReveal() );
        return;
    case SkarnessCommandType::PredictionRevealAdvance:
        SetSkarnessUnsignedResult( application, "frame",
                                   m_replayRuntime.AdvanceDeterministicReveal(
                                       static_cast<ReplayFrameIndex>( command.integer ) ) );
        return;
    case SkarnessCommandType::ReplayRestoreBranch:
        ApplyReplayTransportCommand( frameResult, facts, ReplayRestoreBranchCommand {} );
        return;
    case SkarnessCommandType::ReplaySave:
    {
        ReplaySaveCommand save;
        strncpy_s( save.path, sizeof( save.path ), command.text.c_str(), _TRUNCATE );
        ApplyReplayTransportCommand( frameResult, facts, save );
        return;
    }
    case SkarnessCommandType::ReplayLoad:
    {
        ReplayLoadCommand load;
        strncpy_s( load.path, sizeof( load.path ), command.text.c_str(), _TRUNCATE );
        ApplyReplayTransportCommand( frameResult, facts, load );
        return;
    }
    case SkarnessCommandType::ReplayReturnToLive:
        ApplyReplayTransportCommand( frameResult, facts, ReplayReturnToLiveCommand {} );
        return;
    case SkarnessCommandType::ReplayReturnFromCause:
        m_replayRuntime.RequestCauseReturn();
        return;
    case SkarnessCommandType::ReplaySelectCauseRow:
        ApplyReplayTransportCommand( frameResult, facts, ReplaySelectCauseRowCommand { command.integer } );
        return;
    case SkarnessCommandType::ReplaySelectCause:
    {
        const RunReplayCauseTreeState& cause = m_replayRuntime.CauseTree();
        const RunReplayCauseTreeRow* row = command.integer >= 0 && command.integer < static_cast<int>( cause.rows.size() )
                                               ? &cause.rows[command.integer]
                                               : nullptr;
        application.applied = row && row->id.value == command.unsignedInteger &&
                              row->firstFrame == command.secondUnsignedInteger &&
                              row->sourceGeneration == command.thirdUnsignedInteger &&
                              row->sourceBankEpoch == command.fourthUnsignedInteger &&
                              row->sourceTopologyVersion == command.fifthUnsignedInteger &&
                              row->sourcePublicationVersion == command.sixthUnsignedInteger;
        application.reason = application.applied ? nullptr : "cause row identity is stale";

        if ( application.applied )
        {
            ApplyReplayTransportCommand( frameResult, facts, ReplaySelectCauseRowCommand { command.integer } );
        }
        return;
    }
    case SkarnessCommandType::ReplaySetCauseInspectorOpen:
        ApplyReplayTransportCommand( frameResult, facts, ReplaySetCauseInspectorOpenCommand { command.enabled } );
        return;
    case SkarnessCommandType::ReplaySetCauseFilterText:
        m_replayRuntime.SetCauseTreeFilterText( command.text.c_str() );
        return;
    case SkarnessCommandType::ReplaySetCauseFilter:
        m_replayRuntime.SetCauseTreeFilter( command.text == "prediction" ? RunReplayCauseTreeFilter::Prediction
                                            : command.text == "contacts" ? RunReplayCauseTreeFilter::Contacts
                                                                         : RunReplayCauseTreeFilter::All );
        return;
    case SkarnessCommandType::ReplaySetCauseInspectorTab:
        m_replayRuntime.SetCauseInspectorTab( command.text == "raw"          ? ReplayCauseInspectorTab::RawRecord
                                              : command.text == "iterations" ? ReplayCauseInspectorTab::Iterations
                                                                             : ReplayCauseInspectorTab::Summary );
        return;
    case SkarnessCommandType::ReplayCopyCauseRecord:
    {
        char copy[REPLAY_CAUSE_INSPECTOR_COPY_TEXT_CAPACITY] = {};
        application.applied = m_replayRuntime.CopySelectedCauseRecord( copy, sizeof( copy ) );
        application.reason = application.applied ? nullptr : "selected cause has no copyable solver record";
        application.result.valueName = "record";
        application.result.textValue = copy;
        application.result.hasTextValue = application.applied;
        return;
    }
    case SkarnessCommandType::ReplaySetPorkchopVisible:
        m_replayRuntime.SetPorkchopPanelVisible( command.enabled );
        return;
    case SkarnessCommandType::ReplaySelectPorkchopCell:
        application.applied = m_replayRuntime.SelectPorkchopCell( static_cast<std::size_t>( command.integer ) );
        application.reason = application.applied ? nullptr : "porkchop cell is not currently available";
        SetSkarnessIntegerResult( application, "cell", command.integer );
        return;
    case SkarnessCommandType::ReplaySetTripTimeOfFlight:
        application.applied = m_replayRuntime.QueueTripPlannerCommand(
            { ReplayTripPlannerCommandKind::SetTimeOfFlight, static_cast<float>( command.number ) } );
        application.reason = application.applied ? nullptr : "trip planner command queue is full";
        SetSkarnessNumberResult( application, "seconds", command.number );
        return;
    case SkarnessCommandType::ReplayTripPlan:
    case SkarnessCommandType::ReplayTripCommit:
    case SkarnessCommandType::ReplayTripCancel:
        application.applied = m_replayRuntime.QueueTripPlannerCommand(
            { command.type == SkarnessCommandType::ReplayTripPlan     ? ReplayTripPlannerCommandKind::Plan
              : command.type == SkarnessCommandType::ReplayTripCommit ? ReplayTripPlannerCommandKind::Commit
                                                                      : ReplayTripPlannerCommandKind::Cancel,
              0.0f } );
        application.reason = application.applied ? nullptr : "trip planner command queue is full";
        return;
    case SkarnessCommandType::PredictionForecastStart:
    case SkarnessCommandType::PredictionForecastReset:
    case SkarnessCommandType::PredictionForecastStop:
    {
        SkullbonezCore::UI::OperatorEditorCommandQueues commands;
        commands.forecast.count = 1u;
        commands.forecast.commands[0].type = command.type == SkarnessCommandType::PredictionForecastReset
                                                 ? SkullbonezCore::UI::OperatorEditorForecastCommandType::Reset
                                             : command.type == SkarnessCommandType::PredictionForecastStop
                                                 ? SkullbonezCore::UI::OperatorEditorForecastCommandType::Exit
                                                 : SkullbonezCore::UI::OperatorEditorForecastCommandType::ToggleContinuous;
        ApplyForecastOperatorCommands( frameResult, facts, commands );
        return;
    }
    default:
        application.handled = false;
        return;
    }
}

bool Run::ApplySkarnessSceneLoadCommand( const SkarnessCommand& command, bool& deferred, const char*& reason )
{
    const SkullbonezCore::UI::RunSceneBrowserState& browser = m_operatorUi->SceneNavigation().browser;
    const int pathMatchIndex = browser.CurrentIndexForPath( &command.text );
    int browserIndex = -1;

    for ( int index = 0; index < static_cast<int>( browser.paths.size() ); ++index )
    {
        const bool nameMatches = index < static_cast<int>( browser.names.size() ) && browser.names[index] == command.text;
        const bool pathMatches = index == pathMatchIndex;

        if ( !nameMatches && !pathMatches )
        {
            continue;
        }

        if ( browserIndex >= 0 )
        {
            reason = "scene name or path is ambiguous";
            return false;
        }

        browserIndex = index;
    }

    if ( browserIndex < 0 )
    {
        reason = "scene name or path was not found in the scene browser";
        return false;
    }

    const std::string* currentPath = m_sceneController.CurrentPath();

    if ( currentPath && browser.CurrentIndexForPath( currentPath ) == browserIndex )
    {
        return true;
    }

    const SceneLifecyclePacket& lifecycle = m_sceneController.LifecyclePacket();

    if ( !m_skarness.BeginSceneTransition( command.requestId, lifecycle.generation, browser.paths[browserIndex].c_str(),
                                           false ) )
    {
        reason = "another scene transition is pending";
        return false;
    }

    m_sceneController.SubmitLoadBrowserIndex( browserIndex );
    deferred = true;
    return true;
}

void Run::ApplySkarnessSceneLifecycleCommand( const SkarnessCommand& command, SkarnessCommandApplication& application )
{
    application.handled = true;

    switch ( command.type )
    {
    case SkarnessCommandType::CaptureScreenshot:
    {
        const uint64_t token = m_skarness.BeginCapture( command.requestId );
        const Core::SbResult queued = m_capture.QueuePostRenderPng( command.text.c_str(),
                                                                    PostRenderCaptureOwner::ExternalAutomation, token );

        if ( !queued.Ok() )
        {
            m_skarness.CompleteCapture( token, false, queued.ErrorMessage() );
        }

        application.deferred = true;
        return;
    }
    case SkarnessCommandType::SceneLoad:
        application.applied = ApplySkarnessSceneLoadCommand( command, application.deferred, application.reason );
        return;
    case SkarnessCommandType::SceneReset:
    {
        const SceneLifecyclePacket& lifecycle = m_sceneController.LifecyclePacket();
        const std::string* currentPath = m_sceneController.CurrentPath();
        const bool expectDemo = !currentPath || currentPath->empty();

        if ( !m_skarness.BeginSceneTransition( command.requestId, lifecycle.generation,
                                               currentPath ? currentPath->c_str() : "", expectDemo ) )
        {
            application.applied = false;
            application.reason = "another scene transition is pending";
            return;
        }

        m_sceneController.SubmitResetCurrentScene();
        application.deferred = true;
        return;
    }
    case SkarnessCommandType::SceneLoadDemo:
    {
        const SceneLifecyclePacket& lifecycle = m_sceneController.LifecyclePacket();

        if ( !m_skarness.BeginSceneTransition( command.requestId, lifecycle.generation, "", true ) )
        {
            application.applied = false;
            application.reason = "another scene transition is pending";
            return;
        }

        m_sceneController.SubmitLoadDemoScene();
        application.deferred = true;
        return;
    }
    default:
        application.handled = false;
        return;
    }
}

void Run::ApplySkarnessObjectLookupCommand( const SkarnessCommand& command, SkarnessCommandApplication& application )
{
    switch ( command.type )
    {
    case SkarnessCommandType::SceneObjectList:
    {
        const SceneWorld& world = m_sceneController.Scene();
        application.result.objects.reserve( static_cast<std::size_t>( world.SceneEntityCount() ) );

        for ( int modelIndex = 0; modelIndex < world.SceneEntityCount(); ++modelIndex )
        {
            const SkarnessSceneObjectResult object = BuildSkarnessSceneObjectResult( world, modelIndex );

            if ( object.sceneObjectId != 0u )
            {
                application.result.objects.push_back( object );
            }
        }
        return;
    }
    case SkarnessCommandType::SceneObjectResolve:
    {
        SceneWorld& world = m_sceneController.Scene();
        int modelIndex = -1;
        application.applied = ResolveSkarnessSceneObject( world, command, modelIndex, application.reason );

        if ( application.applied )
        {
            application.result.objects.push_back( BuildSkarnessSceneObjectResult( world, modelIndex ) );
        }
        return;
    }
    default:
        application.handled = false;
        return;
    }
}

void Run::ApplySkarnessSelectObjectCommand( const SkarnessCommand& command, SkarnessCommandApplication& application )
{
    if ( command.secondText == "inspect" )
    {
        application.applied = ApplySkarnessPredictionTargetCommand( command, application.reason );
        if ( application.applied )
        {
            SceneWorld& world = m_sceneController.Scene();
            int modelIndex = -1;
            const char* ignoredReason = nullptr;
            if ( ResolveSkarnessSceneObject( world, command, modelIndex, ignoredReason ) )
            {
                application.result.objects.push_back( BuildSkarnessSceneObjectResult( world, modelIndex ) );
            }
        }
        return;
    }

    if ( !m_editorTools.Editor().editorModeEnabled )
    {
        application.applied = false;
        application.reason = "editor selection requires editor mode";
        return;
    }

    SceneWorld& world = m_sceneController.Scene();
    int modelIndex = -1;
    application.applied = ResolveSkarnessSceneObject( world, command, modelIndex, application.reason );

    if ( application.applied )
    {
        const SceneEntityRecord& entity = world.Entities().At( modelIndex );
        RuntimeInteractionCommand selection;
        selection.type = RuntimeInteractionCommandType::SetEditorSelection;
        selection.body = entity.body;
        selection.collider = world.Colliders().HandleForSceneObjectId( entity.sceneObjectId );
        selection.claimSelectionOwner = false;
        application.applied = m_editorTools.ApplySelectionCommand( selection, world );
        application.reason = application.applied ? nullptr : "editor selection owner rejected the scene object";
        if ( application.applied )
        {
            application.result.objects.push_back( BuildSkarnessSceneObjectResult( world, modelIndex ) );
        }
    }
}

void Run::ApplySkarnessClearSelectionCommand( const SkarnessCommand& command, SkarnessCommandApplication& application )
{
    if ( command.secondText == "inspect" )
    {
        m_replayRuntime.ClearPathSelection();
    }
    else if ( m_editorTools.Editor().editorModeEnabled )
    {
        RuntimeInteractionCommand selection;
        selection.type = RuntimeInteractionCommandType::SetEditorSelection;
        selection.claimSelectionOwner = false;
        application.applied = m_editorTools.ApplySelectionCommand( selection, m_sceneController.Scene() );
        application.reason = application.applied ? nullptr : "editor selection owner rejected clear";
    }
    else
    {
        application.applied = false;
        application.reason = "editor selection requires editor mode";
    }
}

void Run::ApplySkarnessSelectionCommand( const SkarnessCommand& command, SkarnessCommandApplication& application )
{
    switch ( command.type )
    {
    case SkarnessCommandType::SceneObjectSelect:
        ApplySkarnessSelectObjectCommand( command, application );
        return;
    case SkarnessCommandType::SceneObjectClearSelection:
        ApplySkarnessClearSelectionCommand( command, application );
        return;
    case SkarnessCommandType::PredictionSelectTarget:
        application.applied = ApplySkarnessPredictionTargetCommand( command, application.reason );
        return;
    case SkarnessCommandType::CameraOrbitInspection:
        application.applied = ApplySkarnessCameraCommand( command, application.reason );
        return;
    default:
        application.handled = false;
        return;
    }
}

void Run::ApplySkarnessObjectCommand( const SkarnessCommand& command, SkarnessCommandApplication& application )
{
    application.handled = true;
    ApplySkarnessObjectLookupCommand( command, application );

    if ( !application.handled )
    {
        application.handled = true;
        ApplySkarnessSelectionCommand( command, application );
    }
}

void Run::ApplySkarnessCommands( RuntimeUIFrameResult& result, const RuntimeInputFrameFacts& facts )
{
    if ( !m_skarness.Enabled() )
    {
        return;
    }

    SkarnessCommand command;

    while ( m_skarness.PopCommand( command ) )
    {
        SkarnessCommandApplication application;

        ApplySkarnessReplayCommand( command, result, facts, application );

        if ( !application.handled )
        {
            ApplySkarnessSceneLifecycleCommand( command, application );
        }

        if ( !application.handled )
        {
            ApplySkarnessObjectCommand( command, application );
        }

        if ( !application.deferred )
        {
            m_skarness.CompleteCommand( command.requestId, application.applied, application.result,
                                        application.handled ? application.reason : "command has no App route" );
        }
    }
}
#endif

void Run::ApplyForecastOperatorCommands( RuntimeUIFrameResult& result, const RuntimeInputFrameFacts& facts,
                                         const SkullbonezCore::UI::OperatorEditorCommandQueues& commands )
{
    // Invariant: bounded PREDICT and the continuous private forecast never own
    // speculative workers at the same time. App resolves this cross-owner mode
    // transition before either producer is advanced later in the frame.
    for ( uint32_t index = 0u; index < commands.forecast.count; ++index )
    {
        const SkullbonezCore::UI::OperatorEditorForecastCommand& command = commands.forecast.commands[index];

        if ( command.type == SkullbonezCore::UI::OperatorEditorForecastCommandType::Exit )
        {
            m_continuousForecast.Stop();
            continue;
        }

        const ContinuousOrbitalForecastView current = m_continuousForecast.View();

        if ( command.type == SkullbonezCore::UI::OperatorEditorForecastCommandType::ToggleContinuous &&
             ( current.active || current.workerInFlight ) )
        {
            m_continuousForecast.Stop();
            continue;
        }

        if ( m_replayRuntime.BuildInputView().predictionEnabled )
        {
            ApplyReplayTransportCommand( result, facts, ReplayTogglePredictionCommand {} );
        }

        if ( !m_replayRuntime.BuildInputView().predictionEnabled )
        {
            const SceneWorld& scene = m_sceneController.Scene();
            const Scene::OrbitalStabilityContract& contract = scene.OrbitalStabilityContract();
            std::array<ContinuousOrbitalPresentationMember, Scene::ORBITAL_STABILITY_MEMBER_CAPACITY> presentationMembers =
                {};
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
                        ? m_continuousForecast.Reset( scene.Physics(), scene.Tornado(), m_config,
                                                      scene.Environment().GetPhysicsWorldForces(), m_workerPool, contract,
                                                      presentationView )
                        : m_continuousForecast.Start( scene.Physics(), scene.Tornado(), m_config,
                                                      scene.Environment().GetPhysicsWorldForces(), m_workerPool, contract,
                                                      presentationView ) );
        }
    }
}

void Run::RecordInputModeAction( RuntimeInputAction action, RuntimeInputActionSource source )
{
    RuntimeInputContext& runtimeInput = m_inputRouter.RuntimeContext();
    InputController::ApplyModeAction( runtimeInput,
                                      InputController::ResolveMode(
                                          BuildRuntimeInputModeState( m_camera.mode, m_editorTools.Editor(),
                                                                      m_interaction.Gesture(),
                                                                      m_attachedCamera.State().activeFollow,
                                                                      m_camera.director.grabbed ) ),
                                      action, source );
}

void Run::ApplyEditorPlacementModeCommand( RuntimeUIFrameResult& result, const RuntimeInputFrameFacts& facts, bool toggle )
{
    result.enterInteractiveScene = true;
    const EditorPlacementModeChangeResult placementMode = toggle ? ToggleEditorPlacementMode( m_editorTools.Editor(),
                                                                                              m_interaction )
                                                                 : SetEditorPlacementMode( m_editorTools.Editor(),
                                                                                           m_interaction, true, false );
    m_inputRouter.SetWorldInteractionOwner( placementMode.worldOwner, InteractionExitReason::EnterEdit, m_editorTools,
                                            m_runtimeTools, m_interaction, m_attachedCamera, m_camera, m_sceneController,
                                            m_replayRuntime, facts.replayRestoreCameraMode );
    if ( m_inputRouter.ReleasePointerToUi( EvaluateRuntimePointerPresentation( m_inputRouter, m_editorTools.Editor(),
                                                                               m_replayRuntime.BuildInputView() ) ) )
    {
        InputController::ResetMouseLook( m_camera );
    }
    m_inputRouter.ApplyPointerPresentation(
        EvaluateRuntimePointerPresentation( m_inputRouter, m_editorTools.Editor(), m_replayRuntime.BuildInputView() ) );
    RecordInputModeAction( RuntimeInputAction::ToggleEditorTool, RuntimeInputActionSource::UI );
}

void Run::ApplyEditorModeToggleCommand( RuntimeUIFrameResult& result, const RuntimeInputFrameFacts& facts,
                                        RuntimeInputActionSource source )
{
    result.enterInteractiveScene = true;
    const bool enteringEditor = !m_editorTools.Editor().editorModeEnabled;
    if ( enteringEditor )
    {
        const RuntimeInteractionTransition transition = m_interaction.EnterEdit();
        m_inputRouter.ApplyInteractionTransition( transition, m_editorTools, m_runtimeTools, m_interaction, m_attachedCamera,
                                                  m_camera, m_sceneController, m_replayRuntime,
                                                  facts.replayRestoreCameraMode );
        const bool wasFlyMode = RunCameraModeUsesFlyControls( m_camera.mode, m_attachedCamera.State().activeFollow,
                                                              m_camera.director.grabbed );
        EnterEditorModeState( m_editorTools.Editor(), m_interaction,
                              NormalizeRuntimeCameraMode( m_camera.mode, m_sceneController.State().isSceneMode,
                                                          facts.cameraModeEnabledMask ) );
        m_runtimeTools.CancelMousePickup( m_inputRouter, m_interaction );
        m_camera.mode = RunCameraMode::Inspect;
        if ( !wasFlyMode )
        {
            EnterFlyModeCamera( m_inputRouter, m_camera, m_sceneController.Scene().Cameras(),
                                m_sceneController.State().isSceneMode, m_editorTools.Editor(),
                                m_replayRuntime.BuildInputView() );
        }
        else
        {
            InputController::ResetMouseLook( m_camera );
        }
    }
    else
    {
        const RunCameraMode restoreMode = NormalizeRuntimeCameraMode( m_editorTools.Editor().restoreCameraModeAfterEditor,
                                                                      m_sceneController.State().isSceneMode,
                                                                      facts.cameraModeEnabledMask );
        const RuntimeInteractionTransition transition = EnterInteractionForCameraMode( m_interaction, restoreMode );
        m_inputRouter.ApplyInteractionTransition( transition, m_editorTools, m_runtimeTools, m_interaction, m_attachedCamera,
                                                  m_camera, m_sceneController, m_replayRuntime,
                                                  facts.replayRestoreCameraMode );
        const bool wasFlyMode = RunCameraModeUsesFlyControls( m_camera.mode, m_attachedCamera.State().activeFollow,
                                                              m_camera.director.grabbed );
        ExitEditorModeState( m_editorTools.Editor(), m_interaction );
        m_camera.mode = restoreMode;
        if ( wasFlyMode && !RunCameraModeUsesFlyControls( m_camera.mode, m_attachedCamera.State().activeFollow,
                                                          m_camera.director.grabbed ) )
        {
            ExitFlyModeCamera( m_inputRouter, m_camera, m_sceneController.Scene().Cameras(),
                               *m_sceneController.Scene().Terrain().Get(), m_sceneController.State().isSceneMode );
        }
        else
        {
            InputController::ResetMouseLook( m_camera );
        }
    }
    m_inputRouter.ApplyPointerPresentation(
        EvaluateRuntimePointerPresentation( m_inputRouter, m_editorTools.Editor(), m_replayRuntime.BuildInputView() ) );
    RecordInputModeAction( RuntimeInputAction::ToggleEditor, source );
}

void Run::ApplyEditorModeCommands( RuntimeUIFrameResult& result, bool keyboardToggleEditorMode,
                                   const RuntimeInputFrameFacts& facts,
                                   const SkullbonezCore::UI::InGameUICommands& commands )
{
    const EditorPlacementPreModeUICommandResult preMode = ApplyEditorPlacementPreModeUICommands( m_editorTools.Editor(),
                                                                                                 m_interaction,
                                                                                                 commands.editor );
    if ( preMode.setPlaceStatic )
    {
        result.enterInteractiveScene = true;
        RecordInputModeAction( RuntimeInputAction::ToggleEditorStaticPlacement, RuntimeInputActionSource::UI );
    }
    if ( preMode.enterPlacementMode )
    {
        ApplyEditorPlacementModeCommand( result, facts, false );
    }
    if ( preMode.requestedObjectType )
    {
        RecordInputModeAction( RuntimeInputAction::CycleEditorPlacementType, RuntimeInputActionSource::UI );
    }
    if ( preMode.toggleEditorMode || keyboardToggleEditorMode )
    {
        ApplyEditorModeToggleCommand( result, facts,
                                      keyboardToggleEditorMode ? RuntimeInputActionSource::Keyboard
                                                               : RuntimeInputActionSource::UI );
    }
    if ( preMode.togglePlacementMode )
    {
        ApplyEditorPlacementModeCommand( result, facts, true );
    }

    const EditorPlacementPostModeUICommandResult postMode = ApplyEditorPlacementPostModeUICommands( m_editorTools.Editor(),
                                                                                                    m_interaction,
                                                                                                    commands.editor );
    if ( postMode.toggledPlaceStatic )
    {
        result.enterInteractiveScene = true;
        RecordInputModeAction( RuntimeInputAction::ToggleEditorStaticPlacement, RuntimeInputActionSource::UI );
    }
    if ( postMode.toggledTerrainAlign )
    {
        result.enterInteractiveScene = true;
        RecordInputModeAction( RuntimeInputAction::ToggleEditorTerrainAlign, RuntimeInputActionSource::UI );
    }
}

void Run::ApplyEditorSceneCommands( RuntimeUIFrameResult& result, const SkullbonezCore::UI::InGameUICommands& commands )
{
    SceneWorld& world = m_sceneController.Scene();
    if ( commands.editor.requestSelectSceneObject && m_editorTools.Editor().editorModeEnabled )
    {
        PhysicsSceneObjectId sceneObjectId { commands.editor.requestedSceneObjectId };
        const SceneEntityRecord* entity = world.Entities().TryGet( world.Entities().FindBySceneObjectId( sceneObjectId ) );
        if ( entity )
        {
            RuntimeInteractionCommand command;
            command.type = RuntimeInteractionCommandType::SetEditorSelection;
            command.body = entity->body;
            command.collider = world.Colliders().HandleForSceneObjectId( sceneObjectId );
            command.claimSelectionOwner = false;
            result.enterInteractiveScene = m_editorTools.ApplySelectionCommand( command, world ) ||
                                           result.enterInteractiveScene;
        }
    }
    if ( commands.editor.requestSetEntityVisible && m_editorTools.Editor().editorModeEnabled )
    {
        result.enterInteractiveScene = world.SetEditorEntityVisible( PhysicsSceneObjectId { commands.editor
                                                                                                .visibilitySceneObjectId },
                                                                     commands.editor.requestedEntityVisible ) ||
                                       result.enterInteractiveScene;
    }
    if ( commands.editor.requestSetEntityLocked && m_editorTools.Editor().editorModeEnabled )
    {
        result
            .enterInteractiveScene = world.SetEditorEntityLocked( PhysicsSceneObjectId { commands.editor.lockSceneObjectId },
                                                                  commands.editor.requestedEntityLocked ) ||
                                     result.enterInteractiveScene;
    }
    if ( commands.editor.requestDuplicateSelection && m_editorTools.Editor().editorModeEnabled &&
         m_editorTools.DuplicateEditorSelection( world, m_sceneController.State() ) )
    {
        result.enterInteractiveScene = true;
    }
    if ( commands.editor.requestDeleteSelection && m_editorTools.Editor().editorModeEnabled &&
         m_editorTools.DeleteEditorSelection( world, m_sceneController.State() ) )
    {
        result.enterInteractiveScene = true;
        RecordInputModeAction( RuntimeInputAction::DeleteEditorSelection, RuntimeInputActionSource::UI );
    }
    if ( commands.editor.requestUndo && m_editorTools.Editor().editorModeEnabled &&
         m_editorTools.UndoEditorCommand( world, m_sceneController.State() ) )
    {
        result.enterInteractiveScene = true;
        RecordInputModeAction( RuntimeInputAction::UndoEditor, RuntimeInputActionSource::UI );
    }
    if ( commands.editor.requestRedo && m_editorTools.Editor().editorModeEnabled &&
         m_editorTools.RedoEditorCommand( world, m_sceneController.State() ) )
    {
        result.enterInteractiveScene = true;
        RecordInputModeAction( RuntimeInputAction::RedoEditor, RuntimeInputActionSource::UI );
    }
}

void Run::ApplyRuntimePresentationCommands( RuntimeUIFrameResult& result, OperatorCommandTransaction& transaction,
                                            const OperatorCommandAcceptanceLedger& acceptance )
{
    const SkullbonezCore::UI::InGameUICommands& commands = result.commands;
    RuntimeOverlayPresentationEdit presentationEdit = m_overlayDiagnostics->EditPresentation();
    OverlayDebugState& debug = presentationEdit.State();

    if ( commands.scene.toggleCrossScenePause )
    {
        m_sceneController.ToggleCrossScenePause();
        RecordInputModeAction( RuntimeInputAction::ToggleCrossScenePause, RuntimeInputActionSource::UI );
    }

    // Invariant: the one-frame step request joins the routed Space level later
    // in RunInputPhase. It is meaningful only while the scene-flow owner is
    // paused and is never retained as Run business state.
    result.requestSceneStep = commands.scene.requestSingleStep && m_sceneController.CrossScenePauseLocked();
    const DiagnosticsPhysicsOverlayUICommandResult
        physicsDiagnostics = ApplyDiagnosticsPhysicsOverlayUICommands( debug, commands.physics );

    if ( physicsDiagnostics.toggledCollisionVisualizer )
    {
        RecordInputModeAction( RuntimeInputAction::ToggleCollisionVisualizer, RuntimeInputActionSource::UI );
    }

    transaction.ApplyPhysicsControl( m_sceneController.Scene() );
    if ( acceptance.toggledPhysicsSleepPolicy )
    {
        RecordInputModeAction( RuntimeInputAction::TogglePhysicsSleepPolicy, RuntimeInputActionSource::UI );
    }

    const auto recordUiAction = [this]( RuntimeInputAction action )
    { RecordInputModeAction( action, RuntimeInputActionSource::UI ); };
    RecordDiagnosticsPhysicsOverlayUIActions( physicsDiagnostics, recordUiAction );
    RecordTornadoToggleUIActions( acceptance, recordUiAction );

    if ( m_runtimeTools.ApplyRayCastVisualizationUICommand( commands.physics ) )
    {
        RecordInputModeAction( RuntimeInputAction::ToggleRayCastVisualization, RuntimeInputActionSource::UI );
    }

    RecordTornadoApplySettingsUIActions( acceptance, recordUiAction );
    if ( ApplyDiagnosticsTerrainContactProbeUICommand( debug, commands.physics ) )
    {
        RecordInputModeAction( RuntimeInputAction::ToggleTerrainContactProbe, RuntimeInputActionSource::UI );
    }

    const SimulationPacingPolicy
        previousPacing = ResolveSimulationPacingPolicy( m_launchOptions.fixedStep, m_sceneController.State().isFixedStep,
                                                        m_sceneController.State().targetFrameCount,
                                                        m_sceneController.State().isInteractiveRun );
    transaction.ApplyRuntimePresentation( debug, m_sceneController.State(), m_config, m_launchOptions, m_renderDefaults,
                                          true, m_timers.SceneElapsedSeconds() );

    if ( acceptance.toggledFixedStep &&
         previousPacing != ResolveSimulationPacingPolicy( m_launchOptions.fixedStep, m_sceneController.State().isFixedStep,
                                                          m_sceneController.State().targetFrameCount,
                                                          m_sceneController.State().isInteractiveRun ) )
    {
        // Why: live and interactive scenes ignore the fixed-step capture request.
        // Preserve fractional wall-clock time when effective pacing did not change.
        m_simulation.Reset();
    }

    if ( acceptance.toggledTextOnly )
    {
        RecordInputModeAction( RuntimeInputAction::ToggleTextOnly, RuntimeInputActionSource::UI );
    }
    if ( acceptance.toggledFixedStep )
    {
        RecordInputModeAction( RuntimeInputAction::ToggleFixedStep, RuntimeInputActionSource::UI );
    }
    RecordRuntimePresentationUIActions( acceptance, recordUiAction );
}

void Run::ApplyReplayAndPhysicsTuningCommands( const SkullbonezCore::UI::InGameUICommands& commands,
                                               OperatorCommandTransaction& transaction,
                                               const OperatorCommandAcceptanceLedger& acceptance )
{
    const auto recordUiAction = [this]( RuntimeInputAction action )
    { RecordInputModeAction( action, RuntimeInputActionSource::UI ); };

    if ( commands.replayMemory.requestPolicy )
    {
        // Invariant: Memory-tab controls only request policy changes. ReplayRuntime
        // owns the reset/reconfigure edge because it knows all recorder windows.
        ReplayMemoryPolicyRequest request;
        request.presetIndex = commands.replayMemory.requestedPresetIndex;
        request.retentionSeconds = commands.replayMemory.requestedRetentionSeconds;
        request.budgetMiB = commands.replayMemory.requestedBudgetMiB;
        if ( m_replayRuntime.ApplyMemoryPolicyRequest( request ) )
        {
            recordUiAction( RuntimeInputAction::SetReplayMemoryPolicy );
        }
    }

    if ( commands.physics.requestPredictionRevealRate )
    {
        // Why: the Physics tab authors this value, but ReplayPrediction owns it.
        // App applies the request instead of widening the Interaction dependency.
        m_replayRuntime.ApplyPredictionRevealRate( commands.physics.requestedPredictionRevealRate );
    }

    RecordRuntimePresentationWaterUIActions( acceptance, recordUiAction );
    transaction.ApplySimulationPolicy( m_sceneController.State(), m_operatorUi->SceneNavigation().overrides, m_config,
                                       m_workerPool );
    RecordRunSimulationUIActions( acceptance, recordUiAction );

    RuntimeOverlayPresentationEdit presentationEdit = m_overlayDiagnostics->EditPresentation();
    const DiagnosticsPhysicsDebugValueUICommandResult
        physicsDebugValues = ApplyDiagnosticsPhysicsDebugValueUICommands( presentationEdit.State(), commands.physics );
    RecordDiagnosticsPhysicsDebugValueUIActions( physicsDebugValues, recordUiAction );

    const RayCastLauncherTuningUICommandResult launcher = m_runtimeTools.ApplyRayCastLauncherTuningUICommands(
        commands.physics );
    if ( launcher.setImpulseStrength )
    {
        m_replayRuntime.SubmitEvent(
            ReplayEventCommandOperations::BuildLauncherConfig( launcher.impulseConfigChangedFlags,
                                                               launcher.impulseConfigImpulseStrength,
                                                               launcher.impulseConfigProjectileSpeed ) );
        recordUiAction( RuntimeInputAction::SetRayCastImpulseStrength );
    }
    if ( launcher.setProjectileSpeed )
    {
        m_replayRuntime.SubmitEvent(
            ReplayEventCommandOperations::BuildLauncherConfig( launcher.projectileConfigChangedFlags,
                                                               launcher.projectileConfigImpulseStrength,
                                                               launcher.projectileConfigProjectileSpeed ) );
        recordUiAction( RuntimeInputAction::SetLauncherProjectileSpeed );
    }

    transaction.ApplyPhysicsMaterial( m_config, m_sceneController.Scene() );
    RecordPhysicsFrictionUIActions( acceptance, recordUiAction );
}

bool Run::ApplyGeneratedSceneCommands( RuntimeUIFrameResult& result, const RuntimeInputFrameFacts& facts,
                                       const OperatorCommandAcceptanceLedger& acceptance )
{
    const SkullbonezCore::UI::InGameUICommands& commands = result.commands;
    RuntimeRenderer& renderer = Renderer();
    const auto executeAction = [&]( const SceneGeneratedControlAction& action )
    {
        if ( action.clearToolRayHistory )
        {
            m_runtimeTools.ClearRayCastTestLines();
        }
        if ( action.resetReplayTimeline )
        {
            const ReplaySceneTimelineResetInput
                reset = DescribeReplaySceneTimeline( m_sceneController, m_operatorUi->SceneNavigation().overrides,
                                                     m_sceneController.State(), facts.sceneObjectCapacity,
                                                     static_cast<uint32_t>( m_launchOptions.generatedObjectTypeOverride ) );
            m_replayRuntime.ResetSceneTimeline( reset, m_inputRouter, m_interaction, &m_sceneController.Scene().Cameras(),
                                                m_sceneController.Scene().Terrain().Get(), m_camera,
                                                facts.replayRestoreCameraMode, m_attachedCamera.State().activeFollow,
                                                m_camera.director.grabbed );
        }
        if ( action.scheduleProfileReset )
        {
            PROFILE_SCHEDULE_RESET( m_profiler );
        }
    };
    const auto executeTransaction = [&]( SceneGeneratedControlTransaction& transaction, RuntimeInputAction action )
    {
        const SceneGeneratedUICommandResult command = transaction.Execute( m_config, m_sceneController,
                                                                           m_operatorUi->SceneNavigation().overrides,
                                                                           m_camera, m_simulation, &renderer.RenderFrame() );
        if ( !command.action.status.Ok() )
        {
            result.status = command.action.status;
            return false;
        }
        if ( command.accepted )
        {
            executeAction( command.action );
            RecordInputModeAction( action, RuntimeInputActionSource::UI );
        }
        return true;
    };

    SceneGeneratedControlTransaction
        modelCount = SceneGeneratedControlTransaction::ModelCount( commands.sceneOptions.requestedModelCount,
                                                                   m_launchOptions.generatedObjectTypeOverride,
                                                                   facts.sceneObjectCapacity );
    if ( !executeTransaction( modelCount, RuntimeInputAction::SetModelCount ) )
    {
        return false;
    }
    if ( acceptance.setWorkerThreads )
    {
        RecordInputModeAction( RuntimeInputAction::SetWorkerThreads, RuntimeInputActionSource::UI );
    }

    SceneGeneratedControlTransaction
        solverBalls = SceneGeneratedControlTransaction::SolverBallCount( commands.run.requestedSolverBallCount,
                                                                         m_launchOptions.generatedObjectTypeOverride,
                                                                         facts.sceneObjectCapacity );
    if ( !executeTransaction( solverBalls, RuntimeInputAction::SetSolverCounts ) )
    {
        return false;
    }
    SceneGeneratedControlTransaction
        solverBoxes = SceneGeneratedControlTransaction::SolverBoxCount( commands.run.requestedSolverBoxCount,
                                                                        m_launchOptions.generatedObjectTypeOverride,
                                                                        facts.sceneObjectCapacity );
    return executeTransaction( solverBoxes, RuntimeInputAction::SetSolverCounts );
}

void Run::ApplyWorldAndCinematicCommands( RuntimeUIFrameResult& result, const SkullbonezCore::UI::InGameUICommands& commands,
                                          OperatorCommandTransaction& transaction,
                                          const OperatorCommandAcceptanceLedger& acceptance )
{
    const auto recordUiAction = [this]( RuntimeInputAction action )
    { RecordInputModeAction( action, RuntimeInputActionSource::UI ); };
    transaction.ApplyWorldPolicy( m_sceneController.Scene().Environment() );
    if ( acceptance.worldOverrideAccepted )
    {
        const Environment::WorldOverrideChange& worldOverride = acceptance.worldOverride;
        m_replayRuntime.SubmitEvent(
            ReplayEventCommandOperations::BuildWorldOverride( worldOverride.previousGravity,
                                                              worldOverride.previousFluidHeight,
                                                              worldOverride.previousFluidDensity, worldOverride.gravity,
                                                              worldOverride.fluidHeight, worldOverride.fluidDensity ) );
        recordUiAction( RuntimeInputAction::ApplyWorldWaterSettings );
    }

    SkullbonezCore::Core::CinematicRenderConfig& activeCinematic = ActiveSceneCinematicConfig( m_sceneController.State(),
                                                                                               m_config );
    transaction.ApplyCinematicPolicy( m_launchOptions, m_sceneController, m_operatorUi->SceneNavigation().browser, m_assets,
                                      activeCinematic, m_renderDefaults.CinematicBaseline(), m_renderDefaults );
    if ( acceptance.toggledCinematicRendering )
    {
        recordUiAction( RuntimeInputAction::ToggleCinematicRendering );
    }
    if ( acceptance.queuedCinematicSkyDefaultsSave )
    {
        recordUiAction( RuntimeInputAction::SaveSkyDefaults );
    }
    if ( acceptance.selectedCinematicMode )
    {
        result.enterInteractiveScene = true;
        recordUiAction( RuntimeInputAction::SelectCinematicScene );
    }
    RecordCinematicTuningUIActions( acceptance, recordUiAction );
    transaction.Complete();

    const SceneUICommandSubmissionResult sceneCommands = m_sceneController.SubmitUIRequests( commands.scene );
    if ( !sceneCommands.status.Ok() )
    {
        result.status = sceneCommands.status;
        return;
    }
    RecordSceneUIActions( sceneCommands, recordUiAction );
}

// Lifetime: command application borrows composed owners synchronously through
// the Run coordinator; no owner is retained by a delegated operation.
RuntimeUIFrameResult Run::ApplyInputCommandsPhase( RuntimeUIFrameResult result, bool keyboardToggleEditorMode,
                                                   const RuntimeInputFrameFacts& facts )
{
    if ( !result.frameActive || !result.status.Ok() )
    {
        return result;
    }

    // Invariant: both operator surfaces converge at this boundary exactly once.
    // Every later owner receives only the normalized command values it consumes.
    const SkullbonezCore::UI::OperatorEditorArbitrationResult editorCommands = PrepareOperatorInputCommands( result, facts );
    if ( !result.status.Ok() )
    {
        return result;
    }

    const SkullbonezCore::UI::InGameUICommands& commands = result.commands;
    OperatorCommandTransaction transaction( commands );
    const OperatorCommandAcceptanceLedger& acceptance = transaction.Acceptance();

    ApplyReplayOperatorCommands( result, facts, editorCommands.commands );
    ApplyForecastOperatorCommands( result, facts, editorCommands.commands );

    RuntimeRenderer& renderer = Renderer();
    transaction.ApplyDeviceAndMode( renderer, renderer.RenderDevice() );
    if ( acceptance.toggledVsync )
    {
        RecordInputModeAction( RuntimeInputAction::ToggleVsync, RuntimeInputActionSource::UI );
    }
    if ( acceptance.cameraModeAccepted )
    {
        m_inputRouter.ApplyCameraMode( static_cast<RunCameraMode>( acceptance.cameraModeIndex ),
                                       RuntimeInputActionSource::UI, m_editorTools, m_runtimeTools, m_interaction,
                                       m_attachedCamera, m_camera, m_sceneController, m_replayRuntime,
                                       m_inputRouter.RuntimeContext() );
    }

    ApplyEditorModeCommands( result, keyboardToggleEditorMode, facts, commands );
    ApplyEditorSceneCommands( result, commands );
    ApplyRuntimePresentationCommands( result, transaction, acceptance );
    ApplyReplayAndPhysicsTuningCommands( commands, transaction, acceptance );
    if ( !ApplyGeneratedSceneCommands( result, facts, acceptance ) )
    {
        return result;
    }

    ApplyWorldAndCinematicCommands( result, commands, transaction, acceptance );
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
                                          InputController::ResolveMode(
                                              BuildRuntimeInputModeState( camera.mode, editorTools.Editor(),
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
    const EditorViewportPlacementResult editorPointerResult = editorTools.RouteEditorViewportPlacement(
        { result.editorUnhandledWheelDelta, editorDevice.rightDown, editorDevice.leftDown,
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
