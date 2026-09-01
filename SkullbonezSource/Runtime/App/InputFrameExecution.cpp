/*
File: InputFrameExecution.cpp
Purpose:
  Executes the stateless once-per-frame input turn across concrete owners.

Summary:
  This file composes device capture, semantic routing, UI application, pointer
  ownership, and the final owner-specific request checkpoint in one fixed order.
  Scene requests are submitted and executed by SceneController; this file only
  wires its cold dependencies at the post-input checkpoint.

Glossary:
  Pre-UI facts: Focus, key, and pointer values sampled before widgets can claim
    the gesture.
  Post-UI snapshot: Immutable hit/capture result published after widget layout
    so world tools do not reinterpret UI-owned input.
  Selected development surface: The one built-in GameUI or optional ImGui
    development implementation allowed
    to own development-tool input and visibility for the current frame.

Invariants:
  - Device input is captured once; later phases consume router-owned values.
  - UI hit testing completes before pointer ownership is finalized.
  - Every concrete owner is borrowed synchronously for this call only.
  - An inactive GameUI surface cannot reactivate itself through stress actions.
  - Process policy consumes typed results; it never rescans InputRouter actions.

Related:
  - SkullbonezSource/Runtime/App/InputFrame.cpp implements shared value and UI-command policy.
  - SkullbonezSource/Runtime/Input/InputRouter.h owns retained input state.
  - Agentic/Reference/runtime-reference.md
  - Agentic/Reference/engine-glossary.md
*/
#include "InputFrame.h"
#include "Run.h"
#include "SceneLoadApplication.h"
#include "../Startup/Window.h"
#if defined( SKULLBONEZ_DEVELOPMENT_TOOLS )
#include "../DevelopmentTools/ImGuiEditorLayoutPolicy.h"
#endif
#include "../Diagnostics/RuntimeOverlayDiagnostics.h"
#include "../Automation/RuntimeValidationHarness.h"
#include "../../Scene/StandaloneStyleWriter.h"
#include "../Scene/AttachedCameraController.h"
#include "ApplicationExitState.h"
#include "../Diagnostics/DiagnosticsRuntime.h"
#include "../Editor/EditorTools.h"
#include "../Input/InputController.Bindings.h"
#include "../Input/InputController.h"
#include "../Replay/ReplayRestoreTransactions.h"
#include "../Direction/DemoDirectorPlayback.h"
#include "../Render/RenderDefaultsStore.h"
#include "../Render/RuntimeRenderer.h"
#include "../Diagnostics/OverlayDebugState.h"
#include "../Startup/RunLaunchOptions.h"
#include "../Startup/RunStartupState.h"
#include "../Diagnostics/RuntimeFrameMetricsOwner.h"
#include "../UI/RuntimeViewModel.h"
#include "../Tools/RuntimeTools.h"
#include "../Interaction/RuntimeInteractionCommands.h"
#include "../Scene/SceneGeneratedControlTransaction.h"
#include "../Scene/SceneLoadTransaction.h"
#include "../Scene/SceneCinematicPolicy.h"
#include "../Scene/SceneController.h"
#include "../../Scene/AuthoredScene.h"
#include "../../Core/Log.h"
#include "../../Physics/ColliderStore.h"
#include "../../Physics/PhysicsBodyStore.h"
#include "../Simulation/SimulationSystem.h"
#include "../UI/GameUI/UI.h"
#include "../../Rendering/DX12/Dx12Diagnostics.h"
#include "../../Rendering/DX12/Dx12BackbufferCapture.h"
#include "../../Rendering/DX12/Dx12ShaderDevelopment.h"
#include "../../Core/Allocation/RuntimeAllocationTracker.h"
#include "../../UI/UILayout.h"

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <ctime>

using namespace SkullbonezCore::Runtime;
using namespace SkullbonezCore::Runtime::ReplayTimelineOperations;
using namespace SkullbonezCore::Math::CollisionDetection;
using namespace SkullbonezCore::Math::Orientation;
using namespace SkullbonezCore::Math::Transformation;
using namespace SkullbonezCore::Physics;
using namespace SkullbonezCore::UI::Layout;
using SkullbonezCore::Hardware::Input;
using SkullbonezCore::UI::InGameUITab;

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

DiagnosticsKeyboardCommand ProjectDiagnosticsKeyboardCommand( RuntimeInputAction action )
{
    switch ( action )
    {
    case RuntimeInputAction::ToggleWaterFreeze:
        return DiagnosticsKeyboardCommand::ToggleWaterFreeze;
    case RuntimeInputAction::CycleWaterReflection:
        return DiagnosticsKeyboardCommand::CycleWaterReflection;
    case RuntimeInputAction::ToggleWaterFlat:
        return DiagnosticsKeyboardCommand::ToggleWaterFlat;
    case RuntimeInputAction::ToggleTerrainHidden:
        return DiagnosticsKeyboardCommand::ToggleTerrainHidden;
    case RuntimeInputAction::ToggleWaterHidden:
        return DiagnosticsKeyboardCommand::ToggleWaterHidden;
    case RuntimeInputAction::ToggleCollisionVisualizer:
        return DiagnosticsKeyboardCommand::ToggleCollisionVisualizer;
    case RuntimeInputAction::CyclePhysicsDebugOverlay:
        return DiagnosticsKeyboardCommand::CyclePhysicsDebugOverlay;
    case RuntimeInputAction::ToggleTerrainContactProbe:
        return DiagnosticsKeyboardCommand::ToggleTerrainContactProbe;
    case RuntimeInputAction::StepPhysicsPipelinePrevious:
        return DiagnosticsKeyboardCommand::StepPhysicsPipelinePrevious;
    case RuntimeInputAction::StepPhysicsPipelineNext:
        return DiagnosticsKeyboardCommand::StepPhysicsPipelineNext;
    case RuntimeInputAction::TogglePhysicsDebugTransparent:
        return DiagnosticsKeyboardCommand::TogglePhysicsDebugTransparent;
    case RuntimeInputAction::ReportRendererRuntimeRetired:
        return DiagnosticsKeyboardCommand::ReportRendererRuntimeRetired;
    case RuntimeInputAction::ToggleBroadphaseOverlay:
        return DiagnosticsKeyboardCommand::ToggleBroadphaseOverlay;
    default:
        SB_FATAL( "Runtime/InputFrameExecution", "Input action %u is not a diagnostics command.",
                  static_cast<unsigned int>( action ) );
    }
}

DiagnosticsUiKeyboardCommand ProjectDiagnosticsUiKeyboardCommand( RuntimeInputAction action )
{
    switch ( action )
    {
    case RuntimeInputAction::ToggleUIVisibility:
        return DiagnosticsUiKeyboardCommand::ToggleVisibility;
    case RuntimeInputAction::TogglePerformanceHistogram:
        return DiagnosticsUiKeyboardCommand::TogglePerformanceHistogram;
    case RuntimeInputAction::ToggleMemoryOverlay:
        return DiagnosticsUiKeyboardCommand::ToggleMemoryOverlay;
    default:
        SB_FATAL( "Runtime/InputFrameExecution", "Input action %u is not a diagnostics UI command.",
                  static_cast<unsigned int>( action ) );
    }
}
} // namespace


// Concept: the input turn is an orchestration boundary, not a new domain owner.
// InputRouter owns sampling, edge memory, semantic order, and pointer policy;
// scene, replay, tools, diagnostics, UI, and rendering retain their own state
// and expose only synchronous operations for accepted input actions.
// Lifetime: the Run coordinator reaches composed owners only for this ordered
// input turn; delegated operations receive concrete operands and retain none.
void Run::PublishLookLabStatusView()
{
    const LookLabStatusView status = m_lookLab.Status();
    SkullbonezCore::UI::OperatorEditorLookLabView view;
    view.seed = status.seed;
    view.hasCandidate = status.hasCandidate;
    view.savePending = status.savePending;
    view.detail = status.detail;
    view.bundleDirectory = status.bundleDirectory;
    m_operatorUi->SetLookLabView( view );
}

bool Run::ApplyLookLabSeed( uint64_t seed )
{
    SkullbonezCore::Core::CinematicRenderConfig& active = ActiveSceneCinematicConfig( m_sceneController.State(), m_config );

    if ( !m_lookLab.ResolveSeed( seed, active ) )
    {
        PublishLookLabStatusView();
        return false;
    }

    const SkullbonezCore::Scene::StandaloneStyleSnapshot snapshot = m_lookLab.BuildCurrentSnapshot();
    m_sceneController.ApplyStandaloneStyle( m_launchOptions, m_operatorUi->SceneNavigation().browser, active, snapshot );
    m_lookLab.MarkApplied();
    PublishLookLabStatusView();
    return true;
}

void Run::BeginLookLabSave()
{
    std::time_t now = std::time( nullptr );
    std::tm localTime {};
    std::tm utcTime {};

    if ( now == static_cast<std::time_t>( -1 ) || localtime_s( &localTime, &now ) != 0 || gmtime_s( &utcTime, &now ) != 0 )
    {
        std::fprintf( stderr, "Runtime/Direction/LookLabController: local time unavailable; bundle not created\n" );
        return;
    }

    char timestamp[20] = {};

    if ( std::strftime( timestamp, sizeof( timestamp ), "%Y-%m-%d_%H-%M-%S", &localTime ) == 0 )
    {
        std::fprintf( stderr,
                      "Runtime/Direction/LookLabController: local timestamp formatting failed; bundle not created\n" );

        return;
    }

    const std::time_t localAsUtc = _mkgmtime( &localTime );
    const std::time_t utcAsUtc = _mkgmtime( &utcTime );
    const int utcOffsetMinutes = localAsUtc == static_cast<std::time_t>( -1 ) || utcAsUtc == static_cast<std::time_t>( -1 )
                                     ? 0
                                     : static_cast<int>( std::difftime( localAsUtc, utcAsUtc ) / 60.0 );

    const std::string* scenePath = m_sceneController.CurrentPath();
    const UI::RunSceneBrowserState& browser = m_operatorUi->SceneNavigation().browser;
    const int browserIndex = browser.CurrentIndexForPath( scenePath );
    const char* displayName = "Generated Demo";

    if ( browserIndex >= 0 && static_cast<std::size_t>( browserIndex ) < browser.names.size() )
    {
        displayName = browser.names[static_cast<std::size_t>( browserIndex )].c_str();
    }
    else if ( scenePath )
    {
        displayName = scenePath->c_str();
    }

    const LookLabSaveRequest request { "LookLab", timestamp, utcOffsetMinutes, scenePath ? scenePath->c_str() : "",
                                       displayName };

    LookLabSaveStartResult start = m_lookLab.BeginSave( m_resultDiagnostics, request );
    PublishLookLabStatusView();

    if ( !start.status.Ok() )
    {
        std::fprintf( stderr, "%s: %s\n", start.status.ErrorOwner(), start.status.ErrorMessage() );
        return;
    }

    if ( !start.captureRequested )
    {
        return;
    }

    CaptureController& capture = m_capture;
    Core::SbResult queueResult = capture.QueuePostRenderPng( start.screenshotPath.data(), PostRenderCaptureOwner::LookLab,
                                                             start.captureToken );

    if ( !queueResult.Ok() )
    {
        const Core::SbResult completion = m_lookLab.CompleteSaveCapture( m_resultDiagnostics, start.captureToken,
                                                                         queueResult );

        PublishLookLabStatusView();

        std::fprintf( stderr, "%s: %s\n", completion.ErrorOwner(), completion.ErrorMessage() );
    }
}

void Run::CancelPendingLookLabSave( const char* reason )
{
    if ( !m_lookLab.HasPendingSave() )
    {
        return;
    }

    const uint64_t token = m_lookLab.PendingSaveToken();
    (void)m_capture.CancelPostRenderRequest( PostRenderCaptureOwner::LookLab, token );
    const Core::SbResult result = m_lookLab.CancelPendingSave( m_resultDiagnostics, reason );
    PublishLookLabStatusView();

    if ( !result.Ok() )
    {
        std::fprintf( stderr, "%s: %s\n", result.ErrorOwner(), result.ErrorMessage() );
    }
}

void Run::PrepareSceneScopedOwnersForTransition()
{
    // Lifetime: a scene load cannot invalidate the forecast's live seed until
    // Stop has requested cancellation and joined its private worker.
    m_continuousForecast.Stop();
    CancelPendingLookLabSave( "scene transition cancelled screenshot" );

    if ( !m_lookLab.HasCandidate() )
    {
        return;
    }

    // Invariant: generated scenes render from the process config. Restore its
    // startup presentation before loading so a candidate cannot leak into the
    // next scene; that load may then apply its own authored or hero style.
    m_config.cinematicRender = m_renderDefaults.CinematicBaseline();
    m_lookLab.ClearForSceneTransition();
    PublishLookLabStatusView();
}

SceneFrameProceedPolicy Run::CompleteRuntimeInputPhase( bool& gameUiActive, bool requestDevelopmentUiSurfaceSwap )
{
#if defined( SKULLBONEZ_DEVELOPMENT_TOOLS )
    if ( m_launchOptions.developmentUiModeExplicit || m_imguiEditor.HasActivatedSurfaceSelection() )
    {
        // Invariant: an explicit process selection wins after any scene default.
        SelectDevelopmentUiSurface( m_imguiEditor.SelectedSurface() );
    }
    if ( requestDevelopmentUiSurfaceSwap )
    {
        SelectDevelopmentUiSurface( DevelopmentUiMode::ImGui );
    }
    gameUiActive = m_imguiEditor.SelectedSurface() == DevelopmentUiMode::GameUI;
#else
    (void)gameUiActive;
    (void)requestDevelopmentUiSurfaceSwap;
#endif

    const SceneFrameProceedPolicy proceedPolicy = m_sceneController.BuildFrameProceedPolicy(
        m_inputRouter.RuntimeSnapshot().frameInput.stepHeld );
    AuthoredScene liveStyle;
    if ( m_liveStyle.Poll( m_resultDiagnostics, m_assets, liveStyle ) )
    {
        m_sceneController.ApplyLiveStyle( m_launchOptions, m_operatorUi->SceneNavigation().browser,
                                          ActiveSceneCinematicConfig( m_sceneController.State(), m_config ),
                                          m_renderDefaults.CinematicBaseline(), liveStyle );
        m_liveStyle.MarkStyleApplied();
    }
    return proceedPolicy;
}

RunCameraMode Run::NormalizeInputCameraMode( RunCameraMode mode ) const
{
    const SceneSessionState& scene = m_sceneController.State();
    return NormalizeRuntimeCameraMode( mode, scene.isSceneMode,
                                       RuntimeCameraModeEnabledMask( scene.isSceneMode,
                                                                     m_sceneController.Scene().SceneEntityCount() ) );
}

uint32_t Run::CurrentCameraModeEnabledMask() const
{
    return RuntimeCameraModeEnabledMask( m_sceneController.State().isSceneMode,
                                         m_sceneController.Scene().SceneEntityCount() );
}

void Run::EnterInteractiveInputScene()
{
    m_sceneController.EnterInteractiveRun();
    m_capture.DisableAutomationExit();
}

SkullbonezCore::Core::SbResult Run::RunInputUiStressBatch( bool gameUiActive,
                                                           RuntimeOverlayPresentationEdit& presentationEdit )
{
    // The authored stress harness mutates GameUI and is inert while ImGui owns
    // the development surface.
    if ( !gameUiActive )
    {
        return SkullbonezCore::Core::SbResult::Success();
    }
    presentationEdit.Commit();
    const SkullbonezCore::Core::SbResult result = RunUIStressActions();
    presentationEdit.Refresh();
    return result;
}

bool Run::DrainInputCaptureRequests()
{
    if ( m_capture.PendingScreenshotCount() == 0 )
    {
        return false;
    }
    const CaptureRequestBatchResult batch = m_capture.DrainScreenshotRequests( BackbufferCapture() );
    if ( !batch.status.Ok() )
    {
        std::fprintf( stderr, "%s: %s\n", batch.status.ErrorOwner(), batch.status.ErrorMessage() );
        std::fflush( stderr );
    }
    for ( std::size_t index = 0; index < batch.savedCount; ++index )
    {
        m_replayRuntime.SubmitEvent(
            ReplayEventCommandOperations::BuildCommand( ReplayEventKind::OwnerAction, 0, true, 0,
                                                        static_cast<int32_t>( ReplayOwnerEventCode::CaptureScreenshot ), 0,
                                                        0, 0, 0, batch.saved[index].path ) );
    }
    return true;
}

bool Run::DrainInputRenderDefaultRequests()
{
    if ( m_renderDefaults.PendingCount() == 0 )
    {
        return false;
    }
    const RenderDefaultsSaveBatchResult
        batch = m_renderDefaults.DrainAtFrameCheckpoint( m_config.ordinaryRender,
                                                         ActiveSceneCinematicConfig( m_sceneController.State(), m_config ) );
    if ( !batch.status.Ok() )
    {
        std::fprintf( stderr, "%s: %s\n", batch.status.ErrorOwner(), batch.status.ErrorMessage() );
        std::fflush( stderr );
    }
    for ( std::size_t index = 0; index < batch.savedCount; ++index )
    {
        const ReplayOwnerEventCode code = batch.saved[index] == RenderDefaultsRequestType::Ordinary
                                              ? ReplayOwnerEventCode::RenderSaveOrdinaryDefaults
                                              : ReplayOwnerEventCode::RenderSaveCinematicDefaults;
        m_replayRuntime.SubmitEvent( ReplayEventCommandOperations::BuildCommand( ReplayEventKind::OwnerAction, 0, true, 0,
                                                                                 static_cast<int32_t>( code ), 0, 0, 0, 0,
                                                                                 ReplayOwnerEventName( code ) ) );
    }
    return true;
}

void Run::CommitInputPointerPresentation( const UI::InputCaptureIntent& externalUiCapture )
{
    if ( externalUiCapture.mouse )
    {
        // The vendor backend owns HWND capture during a tool drag. The router
        // will reapply its complete policy when engine mouse intent returns.
        return;
    }
    PointerPresentationState presentation;
    if ( !m_inputRouter.ConsumePointerPresentationChange( presentation ) )
    {
        return;
    }
    const SkullbonezCore::Core::SbResult status = Input::SetNativeMouseCapture( m_resultDiagnostics,
                                                                                presentation.nativeCapture );
    if ( status.Ok() )
    {
        Input::SetSystemCursorVisible( presentation.cursorVisible );
        return;
    }
    ReportRuntimeInputFailure( status );
    m_applicationExit.RequestPhaseFailure( status );
    PostQuitMessage( 1 );
}

bool Run::ExecuteInputSceneLoadRequest( const SceneLoadRequest& request, RuntimeOverlayPresentationEdit& presentationEdit )
{
    if ( !request.accepted )
    {
        return false;
    }
    PrepareSceneScopedOwnersForTransition();
    presentationEdit.Commit();
    SceneLoadTransaction sceneLoad;
    sceneLoad.CaptureSubmittedState( m_camera, CaptureSceneLoadNavigationState( m_operatorUi->SceneNavigation() ),
                                     ProjectScenePresentationValues( presentationEdit.State() ),
                                     { Renderer().VsyncEnabled(), Renderer().PipelineSyncEnabled() },
                                     Renderer().RendererName(), m_timers.SimulationTotalSeconds() );
    const bool loaded = LoadSceneRequest( sceneLoad, request ).Ok();
    ApplyRuntimeFrameMetricsLifecycle( m_metricsSceneLifecyclePolicy, m_sceneController.LifecyclePacket(), m_timers );
    ApplySceneLoadRuntimeReactions( sceneLoad );
    {
        const SceneLoadResult& presentation = BeginSceneLoadPresentation( sceneLoad, *m_validationHarness,
                                                                          m_sceneController );
        const SceneLifecyclePacket& lifecycle = m_sceneController.LifecyclePacket();
        ApplySceneLoadRenderPresentation( lifecycle, &Renderer().RenderDevice(), Renderer().VsyncEnabled() );
        ApplySceneLoadWindowUiPresentation( presentation, m_window, *m_operatorUi );
        ApplySceneLoadGraphicsStressPresentation( lifecycle, m_graphicsStress, m_graphicsStressSceneObserver,
                                                  m_launchOptions );
        sceneLoad.CompletePresentation();
    }
    presentationEdit.Refresh();
    return loaded;
}

bool Run::ToggleInputInteractionRecording( const DeviceInputFrame& deviceFrame,
                                           const UI::InputCaptureIntent& externalUiCapture )
{
    if ( m_interactionRecorder.IsRecording() )
    {
        const Core::SbResult status = m_interactionRecorder.StopAndSave( m_resultDiagnostics, "operator", false );
        if ( !status.Ok() )
        {
            m_applicationExit.RequestPhaseFailure( status );
        }
        else
        {
            m_operatorUi->SceneNavigation().RefreshInteractionRecordings();
        }
        return true;
    }

    bool idle = !deviceFrame.leftDown && !deviceFrame.rightDown && !deviceFrame.middleDown && deviceFrame.wheelDelta == 0 &&
                deviceFrame.rawMouseX == 0 && deviceFrame.rawMouseY == 0 &&
                m_interaction.Gesture().kind == RuntimeInteractionGestureKind::None && !externalUiCapture.text;
    const std::span<const uint64_t> keyWords = deviceFrame.keys.Words();
    for ( std::size_t word = 0u; idle && word < keyWords.size(); ++word )
    {
        uint64_t allowed = 0u;
        if ( word == static_cast<std::size_t>( VK_F8 ) / 64u )
        {
            allowed = uint64_t { 1 } << ( static_cast<unsigned int>( VK_F8 ) & 63u );
        }
        idle = ( keyWords[word] & ~allowed ) == 0u;
    }
    if ( !idle )
    {
        std::fprintf( stderr,
                      "[recorder] Start rejected: release other keys/buttons and finish the active gesture first.\n" );
        return true;
    }
    const Core::SbResult status = m_interactionRecorder.Arm( m_resultDiagnostics, nullptr,
                                                             m_launchOptions.interactionRecordMaxMinutes );
    if ( !status.Ok() )
    {
        m_applicationExit.RequestPhaseFailure( status );
    }
    return true;
}

bool Run::HandlePreUiAuthoringAction( const InputActionEvent& event, const DeviceInputFrame& deviceFrame,
                                      const UI::InputCaptureIntent& externalUiCapture, bool& keyboardToggleEditorMode )
{
    switch ( event.action )
    {
    case RuntimeInputAction::RerollLookLab:
        if ( !ApplyLookLabSeed( m_lookLab.NextAuthoringSeed() ) )
        {
            const LookLabStatusView status = m_lookLab.Status();
            std::fprintf( stderr, "Runtime/Direction/LookLabController: %s\n", status.detail.data() );
        }
        return true;
    case RuntimeInputAction::SaveLookLabBundle:
        BeginLookLabSave();
        return true;
    case RuntimeInputAction::ToggleInteractionRecording:
        return ToggleInputInteractionRecording( deviceFrame, externalUiCapture );
    case RuntimeInputAction::ToggleEditor:
        keyboardToggleEditorMode = true;
        return true;
    default:
        return false;
    }
}

bool Run::HandlePreUiEditorAction( const InputActionEvent& event, const DeviceInputFrame& deviceFrame )
{
    switch ( event.action )
    {
    case RuntimeInputAction::UndoEditor:
        if ( m_editorTools.Editor().editorModeEnabled && deviceFrame.keys.IsDown( VK_CONTROL ) )
        {
            if ( deviceFrame.keys.IsDown( VK_SHIFT ) )
            {
                (void)m_editorTools.RedoEditorCommand( m_sceneController.Scene(), m_sceneController.State() );
            }
            else
            {
                (void)m_editorTools.UndoEditorCommand( m_sceneController.Scene(), m_sceneController.State() );
            }
        }
        return true;
    case RuntimeInputAction::RedoEditor:
        if ( m_editorTools.Editor().editorModeEnabled && deviceFrame.keys.IsDown( VK_CONTROL ) )
        {
            (void)m_editorTools.RedoEditorCommand( m_sceneController.Scene(), m_sceneController.State() );
        }
        return true;
    case RuntimeInputAction::DeleteEditorSelection:
        if ( m_editorTools.Editor().editorModeEnabled )
        {
            (void)m_editorTools.DeleteEditorSelection( m_sceneController.Scene(), m_sceneController.State() );
        }
        return true;
    default:
        return false;
    }
}

bool Run::HandlePreUiCameraAction( const InputActionEvent& event )
{
    RuntimeInputContext& runtimeInput = m_inputRouter.RuntimeContext();
    switch ( event.action )
    {
    case RuntimeInputAction::CycleCameraMode:
        m_inputRouter.CycleCameraMode( m_editorTools, m_runtimeTools, m_interaction, m_attachedCamera, m_camera,
                                       m_sceneController, m_replayRuntime, runtimeInput );
        return true;
    case RuntimeInputAction::ToggleFlyCamera:
    {
        const RunCameraMode passive = m_sceneController.State().isSceneMode ? RunCameraMode::Scene : RunCameraMode::Demo;
        m_inputRouter.ApplyCameraMode( m_camera.mode == RunCameraMode::Inspect ? passive : RunCameraMode::Inspect,
                                       event.source, m_editorTools, m_runtimeTools, m_interaction, m_attachedCamera,
                                       m_camera, m_sceneController, m_replayRuntime, runtimeInput );
        return true;
    }
    case RuntimeInputAction::ToggleLauncher:
    {
        const RunCameraMode target = m_camera.mode == RunCameraMode::Launcher ? m_camera.modeBeforeLauncher
                                                                              : RunCameraMode::Launcher;
        if ( target == RunCameraMode::Launcher )
        {
            m_camera.modeBeforeLauncher = m_camera.mode == RunCameraMode::Manipulator ? RunCameraMode::Inspect
                                                                                      : m_camera.mode;
        }
        m_inputRouter.ApplyCameraMode( target, event.source, m_editorTools, m_runtimeTools, m_interaction, m_attachedCamera,
                                       m_camera, m_sceneController, m_replayRuntime, runtimeInput );
        return true;
    }
    case RuntimeInputAction::CycleLauncherFireMode:
        if ( RunCameraModeUsesLauncher( m_camera.mode ) )
        {
            m_runtimeTools.RayCastTest().fireMode = m_runtimeTools.RayCastTest().fireMode == RunLauncherFireMode::Laser
                                                        ? RunLauncherFireMode::Projectile
                                                        : RunLauncherFireMode::Laser;
        }
        return true;
    case RuntimeInputAction::CycleAttachedCameraSubmode:
        if ( RunCameraModeIsAttached( m_camera.mode ) && m_attachedCamera.CycleMode( m_sceneController.Scene() ) )
        {
            m_inputRouter.RecordModeAction( m_camera, m_editorTools, m_interaction, m_attachedCamera, runtimeInput,
                                            event.action, RuntimeInputActionSource::Keyboard );
        }
        return true;
    case RuntimeInputAction::ToggleAttachedCameraPin:
        if ( RunCameraModeIsAttached( m_camera.mode ) )
        {
            if ( !m_attachedCamera.TogglePin( m_sceneController.Scene() ) &&
                 m_inputRouter.ReleasePointerToUi( EvaluateRuntimePointerPresentation( m_inputRouter, m_editorTools.Editor(),
                                                                                       m_replayRuntime.BuildInputView() ) ) )
            {
                InputController::ResetMouseLook( m_camera );
            }
            m_inputRouter.ApplyPointerPresentation( EvaluateRuntimePointerPresentation( m_inputRouter,
                                                                                        m_editorTools.Editor(),
                                                                                        m_replayRuntime.BuildInputView() ) );
            m_inputRouter.RecordModeAction( m_camera, m_editorTools, m_interaction, m_attachedCamera, runtimeInput,
                                            event.action, RuntimeInputActionSource::Keyboard );
        }
        return true;
    default:
        return false;
    }
}

bool Run::HandlePreUiDirectorAction( const InputActionEvent& event, OverlayDebugState& debug )
{
    static_cast<void>( debug );
    RuntimeInputContext& runtimeInput = m_inputRouter.RuntimeContext();
    switch ( event.action )
    {
    case RuntimeInputAction::WriteLauncherReproSnapshot:
#ifdef _DEBUG
        if ( RunCameraModeUsesLauncher( m_camera.mode ) && !m_replayRuntime.BuildInputView().restoreConsumedThisFrame )
        {
            const LauncherReproSnapshotResult result = m_runtimeTools.WriteLauncherReproSnapshotWithStatusMessage(
                { { m_sceneController.Scene(), m_sceneController.State(), m_sceneController.CurrentPath() },
                  { m_launchOptions, m_sceneController.Scene().Physics().IsSleepEnabled(),
                    m_config.bodySimulation.contactEpsilon, m_config.physicsMaterial.frictionCoeff },
                  { Renderer().VsyncEnabled(), Renderer().PipelineSyncEnabled(), debug.isWaterHidden, debug.isTerrainHidden,
                    debug.isCollisionVisualizer, Renderer().RendererName(), m_timers.SceneElapsedSeconds() } } );
            sprintf_s( debug.reproSnapshotMessage, sizeof( debug.reproSnapshotMessage ), "%s", result.message.data() );
            debug.reproSnapshotMessageUntil = result.messageUntil;
        }
#endif
        return true;
    case RuntimeInputAction::ToggleDirectorGrab:
    {
        if ( m_camera.mode != RunCameraMode::Director )
        {
            return true;
        }
        const DemoCameraPose pose = CaptureDemoDirectorPose( m_sceneController.Scene().Cameras() );
        bool applied = false;
        if ( m_camera.director.grabbed )
        {
            applied = DemoDirectorPlayback::EndGrab( m_camera.director, true, pose );
            if ( applied )
            {
                ExitFlyModeCamera( m_inputRouter, m_camera, m_sceneController.Scene().Cameras(),
                                   *m_sceneController.Scene().Terrain().Get(), m_sceneController.State().isSceneMode );
            }
        }
        else
        {
            DemoDirectorCameraCommand command;
            applied = DemoDirectorPlayback::BeginGrab( m_camera.director, true, pose, command );
            if ( applied && command.applyPose )
            {
                m_sceneController.Scene().Cameras().SetPrimaryPose( command.pose.eye, command.pose.view, command.pose.up );
            }
            if ( applied )
            {
                EnterFlyModeCamera( m_inputRouter, m_camera, m_sceneController.Scene().Cameras(),
                                    m_sceneController.State().isSceneMode, m_editorTools.Editor(),
                                    m_replayRuntime.BuildInputView() );
            }
        }
        if ( applied )
        {
            m_inputRouter.ApplyPointerPresentation( EvaluateRuntimePointerPresentation( m_inputRouter,
                                                                                        m_editorTools.Editor(),
                                                                                        m_replayRuntime.BuildInputView() ) );
            m_inputRouter.RecordModeAction( m_camera, m_editorTools, m_interaction, m_attachedCamera, runtimeInput,
                                            event.action, event.source );
        }
        return true;
    }
    case RuntimeInputAction::SetDirectorPhasePose:
    case RuntimeInputAction::StepDirectorPhase:
    case RuntimeInputAction::SaveDirectorShotList:
    {
        const bool available = m_camera.mode == RunCameraMode::Director ||
                               RunCameraModeUsesFlyControls( m_camera.mode, m_attachedCamera.State().activeFollow,
                                                             m_camera.director.grabbed );
        const DemoCameraPose pose = CaptureDemoDirectorPose( m_sceneController.Scene().Cameras() );
        const bool applied = available &&
                             ( event.action == RuntimeInputAction::SetDirectorPhasePose
                                   ? DemoDirectorPlayback::SetCurrentPhasePose( m_camera.director, pose )
                               : event.action == RuntimeInputAction::StepDirectorPhase
                                   ? DemoDirectorPlayback::SelectNextPhaseForAuthoring( m_camera.director, pose )
                                   : DemoDirectorPlayback::SaveShotList( m_camera.director ) );
        if ( applied )
        {
            m_inputRouter.RecordModeAction( m_camera, m_editorTools, m_interaction, m_attachedCamera, runtimeInput,
                                            event.action, event.source );
        }
        return true;
    }
    default:
        return false;
    }
}

bool Run::HandlePreUiDiagnosticsAction( const InputActionEvent& event, OverlayDebugState& debug )
{
    switch ( event.action )
    {
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
    case RuntimeInputAction::ToggleBroadphaseOverlay:
        HandleDiagnosticsKeyboardShortcut( debug, m_camera.trackBallRow.value, m_sceneController.Scene().SceneEntityCount(),
                                           Renderer().RenderDiagnostics().GetCapabilities().supportsDxrReflection,
                                           m_sceneController.State().isSceneMode, m_timers.SceneElapsedSeconds(),
                                           ProjectDiagnosticsKeyboardCommand( event.action ), true );
        return true;
    case RuntimeInputAction::ReloadShadersFromSource:
    {
        if ( !m_shaderDevelopment )
        {
            fprintf( stderr, "Shader hot reload unavailable: active backend has no development capability.\n" );
            return true;
        }
        // F9 is an explicit cold developer utility outside steady frame allocation policy.
        SkullbonezCore::Core::Allocation::RuntimeAllocationScope allocationScope(
            SkullbonezCore::Core::Allocation::RuntimeAllocationPhase::BackendInit );
        const SkullbonezCore::Core::SbResult status = m_shaderDevelopment->get().ReloadShadersFromSource();
        if ( !status.Ok() )
        {
            fprintf( stderr, "Shader hot reload failed: owner=%s reason=%s\n", status.ErrorOwner(), status.ErrorMessage() );
            SkullbonezCore::Core::Log().WriteEventf( "shader_hot_reload_failed owner=%s reason=%s", status.ErrorOwner(),
                                                     status.ErrorMessage() );
        }
        return true;
    }
    default:
        return false;
    }
}

bool Run::HandlePreUiReplayAction( const InputActionEvent& event, bool gameUiActive )
{
    switch ( event.action )
    {
    case RuntimeInputAction::CycleReplayPathColorMode:
        m_replayRuntime.CyclePathColorMode();
        return true;
    case RuntimeInputAction::ToggleReplayGuideArcs:
        if ( gameUiActive )
        {
            m_replayRuntime.ToggleGuideArcs();
        }
        return true;
    case RuntimeInputAction::ToggleReplayTripPlanner:
        if ( gameUiActive )
        {
            (void)m_replayRuntime.QueueTripPlannerCommand( { ReplayTripPlannerCommandKind::TogglePanel } );
        }
        return true;
    case RuntimeInputAction::ToggleReplayPorkchopPanel:
        if ( gameUiActive )
        {
            m_replayRuntime.TogglePorkchopPanel();
        }
        return true;
    case RuntimeInputAction::ToggleCrossScenePause:
        m_sceneController.ToggleCrossScenePause();
        return true;
    case RuntimeInputAction::ToggleReplayPlayPause:
    {
        ReplayWorkspaceOutput output;
        m_replayRuntime.ApplyTransportCommand( ReplayTogglePlayPauseCommand {}, m_inputRouter, m_interaction, m_camera,
                                               m_timers.SimulationTotalSeconds(), output );
        if ( output.enterInteractive )
        {
            EnterInteractiveInputScene();
        }
        return true;
    }
    default:
        return false;
    }
}

bool Run::HandlePreUiSurfaceAction( const InputActionEvent& event, const DeviceInputFrame& deviceFrame, bool gameUiActive,
                                    OverlayDebugState& debug, bool& requestDevelopmentUiSurfaceSwap )
{
    if ( event.action != RuntimeInputAction::ToggleUIVisibility &&
         event.action != RuntimeInputAction::TogglePerformanceHistogram &&
         event.action != RuntimeInputAction::ToggleMemoryOverlay )
    {
        return false;
    }
    if ( event.action == RuntimeInputAction::ToggleUIVisibility && !gameUiActive )
    {
        return true;
    }
    if ( event.action == RuntimeInputAction::ToggleUIVisibility && deviceFrame.keys.IsDown( VK_CONTROL ) )
    {
        requestDevelopmentUiSurfaceSwap = true;
    }

    const DiagnosticsUiKeyboardCommand command = ProjectDiagnosticsUiKeyboardCommand( event.action );
    const DiagnosticsUIKeyboardShortcutResult result = HandleDiagnosticsUIKeyboardShortcut( debug, command, true );
    switch ( command )
    {
    case DiagnosticsUiKeyboardCommand::ToggleVisibility:
        m_operatorUi->ToggleVisible( m_timers.SimulationTotalSeconds() );
        break;
    case DiagnosticsUiKeyboardCommand::TogglePerformanceHistogram:
        m_operatorUi->TogglePerformanceHistogramEnabled();
        break;
    case DiagnosticsUiKeyboardCommand::ToggleMemoryOverlay:
        m_operatorUi->ToggleMemoryOverlayEnabled();
        break;
    }
    if ( result.markInteractiveRun )
    {
        m_sceneController.State().isInteractiveRun = true;
    }
    if ( result.disableExitOnComplete )
    {
        m_sceneController.State().isExitOnComplete = false;
    }
    if ( result.disableCaptureAutomationExit )
    {
        m_capture.DisableAutomationExit();
    }
    if ( result.triggered )
    {
        if ( result.releaseMouseToUI )
        {
            const PointerPresentationPolicy pointer = EvaluateRuntimePointerPresentation( m_inputRouter,
                                                                                          m_editorTools.Editor(),
                                                                                          m_replayRuntime.BuildInputView() );
            m_inputRouter.ApplyPointerPresentation( pointer );
            if ( m_inputRouter.ReleasePointerToUi( pointer ) )
            {
                InputController::ResetMouseLook( m_camera );
            }
        }
        m_inputRouter.RecordModeAction( m_camera, m_editorTools, m_interaction, m_attachedCamera,
                                        m_inputRouter.RuntimeContext(), event.action, event.source );
    }
    return true;
}

bool Run::HandlePreUiSceneNavigationAction( const InputActionEvent& event, RuntimeOverlayPresentationEdit& presentationEdit )
{
    if ( event.action != RuntimeInputAction::NavigateScenePrevious && event.action != RuntimeInputAction::NavigateSceneNext )
    {
        return false;
    }
    const int direction = event.action == RuntimeInputAction::NavigateScenePrevious ? -1 : 1;
    EnterInteractiveInputScene();
    UI::SceneNavigationModel& navigation = m_operatorUi->SceneNavigation();
    const int current = navigation.browser.CurrentIndexForPath( m_sceneController.CurrentPath() );
    const int cinematic = AdjacentCinematicModeBrowserIndex( navigation, direction, current,
                                                             m_operatorUi->GetActiveTab() == InGameUITab::Cinematic );
    const bool applied = cinematic >= 0 &&
                         m_sceneController.ApplyCinematicBrowserStyle( m_launchOptions, navigation.browser, m_assets,
                                                                       ActiveSceneCinematicConfig( m_sceneController.State(),
                                                                                                   m_config ),
                                                                       m_renderDefaults.CinematicBaseline(), cinematic );
    if ( !applied )
    {
        ExecuteInputSceneLoadRequest( LoadAdjacentScene( navigation, direction, current, m_sceneController ),
                                      presentationEdit );
    }
    return true;
}

void Run::ApplyKeyboardEditorReplayInput( const EditorKeyboardShortcutResult& shortcut )
{
    if ( m_editorTools.Editor().editorModeEnabled )
    {
        (void)m_replayRuntime.ApplyKeyboardVelocityEdit(
            { shortcut.altDown, false, m_interaction.Owner() == WorldInteractionOwner::ReplayVelocityEdit,
              m_timers.SimulationTotalSeconds() } );
        if ( shortcut.togglePlacementMode )
        {
            EnterInteractiveInputScene();
            const EditorPlacementModeChangeResult placement = ToggleEditorPlacementMode( m_editorTools.Editor(),
                                                                                         m_interaction );
            m_inputRouter.SetWorldInteractionOwner( placement.worldOwner, InteractionExitReason::EnterEdit, m_editorTools,
                                                    m_runtimeTools, m_interaction, m_attachedCamera, m_camera,
                                                    m_sceneController, m_replayRuntime,
                                                    NormalizeInputCameraMode(
                                                        m_replayRuntime.BuildInputView().restoreCameraMode ) );
            if ( m_inputRouter.ReleasePointerToUi( EvaluateRuntimePointerPresentation( m_inputRouter, m_editorTools.Editor(),
                                                                                       m_replayRuntime.BuildInputView() ) ) )
            {
                InputController::ResetMouseLook( m_camera );
            }
            m_inputRouter.ApplyPointerPresentation( EvaluateRuntimePointerPresentation( m_inputRouter,
                                                                                        m_editorTools.Editor(),
                                                                                        m_replayRuntime.BuildInputView() ) );
            m_inputRouter.RecordModeAction( m_camera, m_editorTools, m_interaction, m_attachedCamera,
                                            m_inputRouter.RuntimeContext(), RuntimeInputAction::ToggleEditorTool,
                                            RuntimeInputActionSource::Keyboard );
        }
        return;
    }

    const ReplayKeyboardVelocityEditResult result = m_replayRuntime.ApplyKeyboardVelocityEdit(
        { shortcut.altDown, true, m_interaction.Owner() == WorldInteractionOwner::ReplayVelocityEdit,
          m_timers.SimulationTotalSeconds() } );
    if ( result.cancelToolDrag )
    {
        ReplayInteractionOperations::CancelToolDragState( m_interaction, m_inputRouter );
    }
    if ( result.enterInteractive )
    {
        EnterInteractiveInputScene();
    }
    if ( result.cameraAction == ReplayKeyboardVelocityEditCameraAction::EnterInspection )
    {
        m_replayRuntime.EnterInspectionCamera( &m_sceneController.Scene().Cameras(), m_camera,
                                               NormalizeInputCameraMode( m_camera.mode ), m_interaction, m_inputRouter,
                                               m_runtimeTools.MousePickup() );
    }
    else if ( result.cameraAction == ReplayKeyboardVelocityEditCameraAction::ExitInspection )
    {
        m_replayRuntime.ExitInspectionCamera( &m_sceneController.Scene().Cameras(),
                                              m_sceneController.Scene().Terrain().Get(), m_camera,
                                              NormalizeInputCameraMode( m_replayRuntime.BuildInputView().restoreCameraMode ),
                                              m_attachedCamera.State().activeFollow, m_camera.director.grabbed,
                                              m_interaction, m_inputRouter );
    }
    if ( result.setWorldOwner )
    {
        const WorldInteractionOwner owner = result.worldOwner == ReplayWorldOwnerRequest::VelocityEdit
                                                ? WorldInteractionOwner::ReplayVelocityEdit
                                                : WorldInteractionOwner::ReplayScrub;
        m_inputRouter.SetWorldInteractionOwner( owner, InteractionExitReason::EnterReplay, m_editorTools, m_runtimeTools,
                                                m_interaction, m_attachedCamera, m_camera, m_sceneController,
                                                m_replayRuntime,
                                                NormalizeInputCameraMode(
                                                    m_replayRuntime.BuildInputView().restoreCameraMode ) );
    }
}

RuntimeUIFrameResult Run::RunOperatorInputFrame( const UI::InputCaptureIntent& externalUiCapture,
                                                 const UI::OperatorEditorCommandQueues& externalEditorCommands,
                                                 int requestedReplayCauseRow, bool gameUiActive,
                                                 bool keyboardToggleEditorMode,
                                                 RuntimeOverlayPresentationEdit& presentationEdit )
{
    ReplayPathPickInput pointerRay;
    pointerRay.hasWorldRay = m_inputRouter.TryBuildWorldRay( m_sceneController.Scene().Cameras(), m_window,
                                                             pointerRay.rayOrigin, pointerRay.rayDirection );
    const bool blocksKeyboard = m_operatorUi->BlocksKeyboard() || externalUiCapture.keyboard || externalUiCapture.text;
    const RuntimeInputFrameFacts samplingFacts { NormalizeInputCameraMode( m_camera.mode ),
                                                 NormalizeInputCameraMode(
                                                     m_replayRuntime.BuildInputView().restoreCameraMode ),
                                                 CurrentCameraModeEnabledMask(),
                                                 blocksKeyboard,
                                                 SkullbonezCore::Core::ActiveSceneObjectCapacity( m_config ),
                                                 externalUiCapture,
                                                 externalEditorCommands,
                                                 gameUiActive,
                                                 requestedReplayCauseRow };
    RuntimeUIFrameResult result = BeginRuntimeUIFrame( pointerRay, samplingFacts );
    if ( result.frameActive )
    {
        if ( result.enterInteractiveScene )
        {
            EnterInteractiveInputScene();
            result.enterInteractiveScene = false;
        }
        presentationEdit.Commit();
        const InputAfterUiDismissResult dismiss = m_inputRouter.DispatchAfterUiDismiss( m_inputRouter.Actions(),
                                                                                        result.commands.ui.userInteracted,
                                                                                        m_timers.SimulationTotalSeconds(),
                                                                                        gameUiActive, m_camera,
                                                                                        m_attachedCamera, m_editorTools,
                                                                                        *m_operatorUi, m_sceneController,
                                                                                        *m_overlayDiagnostics,
                                                                                        m_replayRuntime.BuildInputView() );
        if ( dismiss.disableCaptureAutomationExit )
        {
            m_capture.DisableAutomationExit();
        }
        presentationEdit.Refresh();
        if ( dismiss.quitRequested )
        {
            PostQuitMessage( 0 );
        }
    }

    const RuntimeInputFrameFacts commandFacts { NormalizeInputCameraMode( m_camera.mode ),
                                                NormalizeInputCameraMode(
                                                    m_replayRuntime.BuildInputView().restoreCameraMode ),
                                                CurrentCameraModeEnabledMask(),
                                                result.suppressWorldActionThisFrame,
                                                SkullbonezCore::Core::ActiveSceneObjectCapacity( m_config ),
                                                externalUiCapture,
                                                externalEditorCommands,
                                                gameUiActive,
                                                requestedReplayCauseRow };
    presentationEdit.Commit();
    result = ApplyInputCommandsPhase( result, keyboardToggleEditorMode, commandFacts );
    presentationEdit.Refresh();
    if ( result.status.Ok() && result.frameActive )
    {
        result.status = RunInputUiStressBatch( gameUiActive, presentationEdit );
    }
    result = FinishRuntimeUIFramePointer( result, m_inputRouter, m_camera, m_editorTools, m_interaction, m_attachedCamera,
                                          *m_operatorUi, m_sceneController, m_replayRuntime,
                                          NormalizeInputCameraMode( m_camera.mode ) );
    if ( result.enterInteractiveScene )
    {
        EnterInteractiveInputScene();
    }
    return result;
}

void Run::ApplyInputReplayRestore( RuntimeUIFrameResult& result, OverlayDebugState& debug )
{
    const ReplayLiveRestoreRequest& request = result.replayWorkspace.restoreRequest;
    if ( request.kind == ReplayLiveRestoreKind::None )
    {
        return;
    }
    ReplaySceneTimelineResetInput
        timelineReset = DescribeReplaySceneTimeline( m_sceneController, m_operatorUi->SceneNavigation().overrides,
                                                     m_sceneController.State(),
                                                     SkullbonezCore::Core::ActiveSceneObjectCapacity( m_config ),
                                                     static_cast<uint32_t>( m_launchOptions.generatedObjectTypeOverride ) );
    timelineReset.preserveReplayInspection = result.replayWorkspace.planningTransitionToken != 0;
    ReplayRestoreTransaction transaction { timelineReset };
    bool restored = false;
    if ( request.kind == ReplayLiveRestoreKind::V2ArtifactTarget )
    {
        transaction.SetArtifactRequest( request );
        restored = m_replayRuntime.RestoreV2ArtifactTargetState( transaction, m_sceneController, debug, m_editorTools,
                                                                 m_runtimeTools, m_simulation, m_config, m_assets,
                                                                 m_workerPool, m_operatorUi->SceneNavigation().overrides,
                                                                 m_launchOptions.generatedObjectTypeOverride );
    }
    else if ( request.kind == ReplayLiveRestoreKind::SolverSample && request.solverSample )
    {
        restored = m_replayRuntime.RestoreSolverSampleAsLive( transaction, m_sceneController.Scene(),
                                                              m_sceneController.State(), debug, m_runtimeTools,
                                                              *request.solverSample );
    }
    else
    {
        transaction.FailBeforeMutation( "live solver restore request has no selected sample" );
    }
    ReplayLiveRestoreOutcome outcome = ReplayLiveRestoreOperations::BuildOutcome( transaction, request.kind, restored );
#ifdef _DEBUG
    m_replayRuntime.PublishRestoreDiagnostic( transaction, m_diagnosticsRuntime, m_sceneController.State() );
#endif
    m_replayRuntime.ApplyRestoredBranchTimeline( transaction, outcome, m_sceneController, m_inputRouter, m_interaction,
                                                 m_camera,
                                                 NormalizeInputCameraMode(
                                                     m_replayRuntime.BuildInputView().restoreCameraMode ),
                                                 m_attachedCamera.State().activeFollow, m_camera.director.grabbed );
    m_replayRuntime.CompleteLiveRestoreScrubber( transaction, request, outcome );
    m_replayRuntime.CompletePlanningTransition( result.replayWorkspace.planningTransitionToken, outcome.restored );
    if ( outcome.enterInteractive )
    {
        EnterInteractiveInputScene();
    }
}

void Run::PublishInputRecordingDiagnostics( OverlayDebugState& debug )
{
    const InteractionRecordingStatusView status = m_interactionRecorder.Status();
    debug.isInteractionRecording = m_interactionRecorder.IsActive();
    debug.interactionRecordingElapsedSeconds = status.elapsedSeconds;
    debug.interactionRecordingMaximumMinutes = status.maximumMinutes;
    debug.interactionRecordingFrameCount = status.frameCount;
    debug.interactionRecordingFrameCapacity = status.frameCapacity;
    strncpy_s( debug.interactionRecordingFailure, sizeof( debug.interactionRecordingFailure ), status.failure, _TRUNCATE );
#if defined( SKULLBONEZ_AUTOMATION_DIAGNOSTICS )
    debug.isInteractionPlayback = m_interactionAutomation.enabled && m_interactionAutomation.recordedManifest &&
                                  !m_interactionAutomation.finished;
    debug.interactionPlaybackTurn = static_cast<std::size_t>( m_interactionAutomation.recordedTurn );
    debug.interactionPlaybackTurnCount = m_interactionAutomation.recordedFrames.size();
#else
    debug.isInteractionPlayback = false;
    debug.interactionPlaybackTurn = 0u;
    debug.interactionPlaybackTurnCount = 0u;
#endif
}

void Run::ApplyInputCameraControls( const UI::InputCaptureIntent& externalUiCapture, InputActions& inputActions,
                                    const RuntimeInputSnapshot& inputSnapshot )
{
    if ( m_operatorUi->BlocksKeyboard() || externalUiCapture.keyboard || externalUiCapture.text )
    {
        m_interaction.CancelCameraLookGesture();
        InputController::ResetMouseLook( m_camera );
        m_camera.inputMoveForward = false;
        m_camera.inputMoveBackward = false;
        m_camera.inputMoveLeft = false;
        m_camera.inputMoveRight = false;
        m_inputRouter.ApplyPointerPresentation(
            EvaluateRuntimePointerPresentation( m_inputRouter, m_editorTools.Editor(), m_replayRuntime.BuildInputView() ) );
        return;
    }

    const InputCaptureActionResult
        capture = m_inputRouter.DispatchCaptureActions( inputActions, m_camera, m_attachedCamera, *m_operatorUi,
                                                        m_sceneController,
                                                        m_overlayDiagnostics->PresentationSnapshot().GetSaveState(),
                                                        m_replayRuntime.BuildInputView() );
    if ( capture.screenshotRequested )
    {
        const std::string path = BuildEditorScreenshotPath();
        if ( !path.empty() )
        {
            const SkullbonezCore::Core::SbResult status = m_capture.QueueScreenshot( path.c_str() );
            if ( !status.Ok() )
            {
                std::fprintf( stderr, "%s: %s\n", status.ErrorOwner(), status.ErrorMessage() );
                std::fflush( stderr );
            }
        }
    }

    const RuntimeInteractionFramePolicy policy = m_interaction.BuildFramePolicy( inputSnapshot.frameInput );
    const bool mouseOwnsCursor = EvaluateRuntimePointerPresentation( m_inputRouter, m_editorTools.Editor(),
                                                                     m_replayRuntime.BuildInputView() )
                                     .mouseLookOwnsCursor;
    m_interaction.SyncCameraLookGesture( inputSnapshot, policy, mouseOwnsCursor );
    const bool mouseLook = policy.cameraMouseLookActive && mouseOwnsCursor && inputSnapshot.appFocused;
    if ( mouseLook )
    {
        m_inputRouter.RequestNativeCapture();
        m_inputRouter.RequestCursorVisible( false );
    }
    const RuntimeCameraInputFrameResult result = InputController::ApplyCameraInputFrame( m_camera, inputSnapshot.appFocused,
                                                                                         mouseLook, mouseOwnsCursor,
                                                                                         policy.cameraKeyboardControlsActive,
                                                                                         m_inputRouter.DeviceFrame() );
    if ( result.applyCursorOwnership )
    {
        m_inputRouter.ApplyPointerPresentation(
            EvaluateRuntimePointerPresentation( m_inputRouter, m_editorTools.Editor(), m_replayRuntime.BuildInputView() ) );
    }
}

void Run::ApplyDeferredInputOwnerRequests( RuntimeOverlayPresentationEdit& presentationEdit )
{
    (void)DrainInputRenderDefaultRequests();
    (void)DrainInputCaptureRequests();
    presentationEdit.Commit();
    const SceneLoadNavigationState navigation = CaptureSceneLoadNavigationState( m_operatorUi->SceneNavigation() );
    if ( m_sceneController.HasPendingTransition() )
    {
        PrepareSceneScopedOwnersForTransition();
    }
    SceneLoadTransaction sceneLoad;
    sceneLoad.CaptureSubmittedState( m_camera, navigation, ProjectScenePresentationValues( presentationEdit.State() ),
                                     { Renderer().VsyncEnabled(), Renderer().PipelineSyncEnabled() },
                                     Renderer().RendererName(), m_timers.SimulationTotalSeconds() );
    (void)ExecutePendingSceneRequests( sceneLoad );
    ApplyRuntimeFrameMetricsLifecycle( m_metricsSceneLifecyclePolicy, m_sceneController.LifecyclePacket(), m_timers );
    ApplySceneLoadRuntimeReactions( sceneLoad );
    {
        const SceneLoadResult& presentation = BeginSceneLoadPresentation( sceneLoad, *m_validationHarness,
                                                                          m_sceneController );
        const SceneLifecyclePacket& lifecycle = m_sceneController.LifecyclePacket();
        ApplySceneLoadRenderPresentation( lifecycle, &Renderer().RenderDevice(), Renderer().VsyncEnabled() );
        ApplySceneLoadWindowUiPresentation( presentation, m_window, *m_operatorUi );
        ApplySceneLoadGraphicsStressPresentation( lifecycle, m_graphicsStress, m_graphicsStressSceneObserver,
                                                  m_launchOptions );
        sceneLoad.CompletePresentation();
    }
    presentationEdit.Refresh();
}


SceneFrameProceedPolicy Run::RunInputPhase( const InteractionAutomationFrameResult* automationBeforeInput,
                                            bool& gameUiActive )
{
    UI::InputCaptureIntent externalUiCapture;
    SkullbonezCore::UI::OperatorEditorCommandQueues externalEditorCommands;
    gameUiActive = true;
#if defined( SKULLBONEZ_DEVELOPMENT_TOOLS )
    externalUiCapture = m_imguiEditor.ConsumeInputCaptureIntent();
    externalEditorCommands = m_imguiEditor.ConsumeOperatorEditorCommands();
#if defined( SKULLBONEZ_AUTOMATION_DIAGNOSTICS )

    if ( automationBeforeInput )
    {
        const SkullbonezCore::Core::SbResult submitStatus = m_interactionAutomation
                                                                .SubmitOperatorEditorReplayCommand( *automationBeforeInput,
                                                                                                    externalEditorCommands );

        if ( !submitStatus.Ok() )
        {
            m_applicationExit.RequestPhaseFailure( submitStatus );
        }

        const SkullbonezCore::Core::SbResult
            forecastSubmitStatus = m_interactionAutomation.SubmitOperatorEditorForecastCommand( *automationBeforeInput,
                                                                                                externalEditorCommands );

        if ( !forecastSubmitStatus.Ok() )
        {
            m_applicationExit.RequestPhaseFailure( forecastSubmitStatus );
        }
    }
#else
    (void)automationBeforeInput;
#endif
    gameUiActive = m_imguiEditor.SelectedSurface() == DevelopmentUiMode::GameUI;
#else
    (void)automationBeforeInput;
#endif
    int requestedReplayCauseRow = -1;
#if defined( SKULLBONEZ_AUTOMATION_DIAGNOSTICS )

    if ( automationBeforeInput )
    {
        requestedReplayCauseRow = automationBeforeInput->requestedReplayCauseRow;
    }
#endif
    bool requestDevelopmentUiSurfaceSwap = false;
    InputRouter& inputRouter = m_inputRouter;
    SkullbonezCore::Core::EngineConfig& config = m_config;
    CameraControlState& camera = m_camera;
    RuntimeOverlayPresentationEdit presentationEdit = m_overlayDiagnostics->EditPresentation();
    OverlayDebugState& debug = presentationEdit.State();
    ApplicationExitState& applicationExit = m_applicationExit;
    RuntimeInteractionController& interaction = m_interaction;
    AttachedCameraController& attachedCamera = m_attachedCamera;
    SkullbonezCore::UI::InGameUI& ui = *m_operatorUi;
    RuntimeTools& runtimeTools = m_runtimeTools;
    EditorToolsOwner& editorTools = m_editorTools;
    SceneController& sceneController = m_sceneController;
    ReplayRuntime& replayRuntime = m_replayRuntime;

    // Lifetime: these aliases expose InputRouter-owned frame state only for
    // this synchronous routing pass; Run retains neither value as member state.
    RuntimeInputContext& runtimeInput = inputRouter.RuntimeContext();
    InputActions& inputActions = inputRouter.Actions();
    const auto SceneState = [&]() -> SceneSessionState& { return sceneController.State(); };


    DeviceInputFrame deviceFrame;
    const SkullbonezCore::Core::SbResult deviceCaptureResult = Input::CaptureDeviceInputFrame( m_resultDiagnostics,
                                                                                               deviceFrame );

    if ( !deviceCaptureResult.Ok() )
    {
        ReportRuntimeInputFailure( deviceCaptureResult );
        std::fflush( stderr );
        applicationExit.RequestPhaseFailure( deviceCaptureResult );
        PostQuitMessage( 1 );
        return CompleteRuntimeInputPhase( gameUiActive, requestDevelopmentUiSurfaceSwap );
    }

#if defined( SKULLBONEZ_DEVELOPMENT_TOOLS )
    // Concept: the last completed UI frame publishes the fitted image value.
    // Mapping immediately after the sole device sample gives every existing
    // world interaction owner one coherent source-space point this frame.
    if ( deviceFrame.hasClientPosition && externalUiCapture.gameViewportMappingActive )
    {
        DevelopmentTools::ImGuiGameViewportRect viewport;
        viewport.imageMinX = externalUiCapture.gameViewportMinX;
        viewport.imageMinY = externalUiCapture.gameViewportMinY;
        viewport.imageWidth = externalUiCapture.gameViewportWidth;
        viewport.imageHeight = externalUiCapture.gameViewportHeight;
        viewport.dpiScale = externalUiCapture.gameViewportDpiScale;
        viewport.sourceWidth = externalUiCapture.gameViewportSourceWidth;
        viewport.sourceHeight = externalUiCapture.gameViewportSourceHeight;
        viewport.valid = true;
        int mappedX = 0;
        int mappedY = 0;

        if ( DevelopmentTools::MapImGuiGameViewportPoint( viewport, static_cast<float>( deviceFrame.clientX ),
                                                          static_cast<float>( deviceFrame.clientY ), mappedX, mappedY ) )
        {
            // Invariant: every downstream pointer consumer sees the same mapped
            // source pixel; no tool resamples Win32 coordinates independently.
            deviceFrame.clientX = mappedX;
            deviceFrame.clientY = mappedY;
        }
    }
#endif
    const RuntimeInputKeyBindingView keyboardBindings = TakeInputKeyboardBindings();
    inputRouter.BeginFrame( deviceFrame, keyboardBindings, inputActions, externalUiCapture );
    UiInputHitSnapshot preUiPointer;
    preUiPointer.mouse = inputActions.mouse;
    preUiPointer.clientX = deviceFrame.clientX;
    preUiPointer.clientY = deviceFrame.clientY;
    preUiPointer.hasClientPosition = deviceFrame.hasClientPosition;
    preUiPointer.unhandledWheelDelta = deviceFrame.wheelDelta;
    inputRouter.PublishUiSnapshot( preUiPointer );

    if ( externalUiCapture.nativePointerStateTouched )
    {
        // The vendor backend may have changed shared HWND capture/cursor state
        // while translating a mouse message. Reassert the input owner's full
        // native policy even when its desired value is otherwise unchanged.
        inputRouter.DeferPointerPresentationCommit();
    }

    if ( externalUiCapture.mouse )
    {
        inputRouter.ReleaseNativeCapture();
        inputRouter.RequestCursorVisible( true );
        inputRouter.DeferPointerPresentationCommit();
    }

    if ( inputRouter.HandleUnfocusedFrame( editorTools, runtimeTools, interaction, attachedCamera, camera, ui,
                                           sceneController, replayRuntime, runtimeInput ) )
    {
        const SkullbonezCore::Core::SbResult stressResult = RunInputUiStressBatch( gameUiActive, presentationEdit );

        if ( !stressResult.Ok() )
        {
            // Recoverable error: focus loss still routes stress churn through the same guarded
            // rebuild path. End the run before returning to the frame loop.
            ReportRuntimeInputFailure( stressResult );
            std::fflush( stderr );
            applicationExit.RequestPhaseFailure( stressResult );
            PostQuitMessage( 1 );
        }

        CommitInputPointerPresentation( externalUiCapture );
        return CompleteRuntimeInputPhase( gameUiActive, requestDevelopmentUiSurfaceSwap );
    }

    const bool UIBlocksKeyboardBeforeInput = ui.BlocksKeyboard() || externalUiCapture.keyboard || externalUiCapture.text;

    inputRouter.ApplyPointerPresentation(
        EvaluateRuntimePointerPresentation( inputRouter, editorTools.Editor(), replayRuntime.BuildInputView() ) );

    InputController::BeginFrame( runtimeInput,
                                 BuildRuntimeInputModeState( camera.mode, editorTools.Editor(), interaction.Gesture(),
                                                             attachedCamera.State().activeFollow, camera.director.grabbed ),
                                 true, UIBlocksKeyboardBeforeInput, ui.BlocksCameraMouse() || externalUiCapture.mouse );

    bool keyboardToggleEditorMode = false;
    EditorKeyboardShortcutResult keyboardEditorToolShortcut;

    const bool flyCamera = RunCameraModeUsesFlyControls( camera.mode, attachedCamera.State().activeFollow,
                                                         camera.director.grabbed );

    const KeyboardContextFacts keyboardContextFacts { !UIBlocksKeyboardBeforeInput,
                                                      SceneState().isSceneMode,
                                                      flyCamera,
                                                      RunCameraModeUsesLauncher( camera.mode ),
                                                      RunCameraModeIsAttached( camera.mode ),
                                                      camera.mode == RunCameraMode::Director,
                                                      camera.mode == RunCameraMode::Director || flyCamera,
                                                      editorTools.Editor().editorModeEnabled,
                                                      !replayRuntime.BuildInputView().restoreConsumedThisFrame,
                                                      false };

    inputRouter.RoutePhase( keyboardBindings, InputActionPhase::PreUi, BuildKeyboardContextMask( keyboardContextFacts ),
                            inputActions );

    if ( inputActions.Overflowed() )
    {
        SB_FATAL( "InputRouter", "Fixed input action capacity exhausted while routing pre-UI actions." );
    }


    // Invariant: pre-UI consumers receive the router's fixed ordered events.
    // Mode checks below may fail closed after an earlier action mutates state,
    // but no consumer may re-sample its physical key.
    for ( std::size_t index = 0; index < inputActions.Count(); ++index )
    {
        const InputActionEvent& event = inputActions[index];

        if ( event.phase != InputActionPhase::PreUi )
        {
            continue;
        }

        if ( event.action == RuntimeInputAction::ToggleEditorTool )
        {
            keyboardEditorToolShortcut = HandleEditorKeyboardShortcut( event.action, event.edge != InputActionEdge::Released,
                                                                       event.edge == InputActionEdge::Pressed );

            continue;
        }

        if ( event.edge != InputActionEdge::Pressed )
        {
            continue;
        }

        (void)( HandlePreUiAuthoringAction( event, deviceFrame, externalUiCapture, keyboardToggleEditorMode ) ||
                HandlePreUiEditorAction( event, deviceFrame ) || HandlePreUiCameraAction( event ) ||
                HandlePreUiDirectorAction( event, debug ) || HandlePreUiDiagnosticsAction( event, debug ) ||
                HandlePreUiReplayAction( event, gameUiActive ) ||
                HandlePreUiSurfaceAction( event, deviceFrame, gameUiActive, debug, requestDevelopmentUiSurfaceSwap ) ||
                HandlePreUiSceneNavigationAction( event, presentationEdit ) );
    }

    ApplyKeyboardEditorReplayInput( keyboardEditorToolShortcut );

    RuntimeUIFrameResult uiFrameResult = RunOperatorInputFrame( externalUiCapture, externalEditorCommands,
                                                                requestedReplayCauseRow, gameUiActive,
                                                                keyboardToggleEditorMode, presentationEdit );

    ApplyInputReplayRestore( uiFrameResult, debug );

    if ( !uiFrameResult.status.Ok() )
    {
        // Recoverable error: a generated-resource rebuild could not prove its GPU drain.
        // Stop this frame and end the run before any later world/input mutation.
        ReportRuntimeInputFailure( uiFrameResult.status );
        std::fflush( stderr );
        applicationExit.RequestPhaseFailure( uiFrameResult.status );
        PostQuitMessage( 1 );
        CommitInputPointerPresentation( externalUiCapture );
        return CompleteRuntimeInputPhase( gameUiActive, requestDevelopmentUiSurfaceSwap );
    }

    const bool suppressWorldActionThisFrame = uiFrameResult.suppressWorldActionThisFrame;

    // Editor, replay, and launcher actions share world clicks. UI interaction
    // and capture suppress them so panel controls never mutate the scene.
    const RuntimeMouseEdges& mouseEdges = inputRouter.UiSnapshot().mouse;
    const DeviceInputFrame& routedDeviceFrame = inputRouter.DeviceFrame();
    const UiInputHitSnapshot& routedUiSnapshot = inputRouter.UiSnapshot();
    const ReplayInputView replayInput = replayRuntime.BuildInputView();
    RuntimeInteractionFrameInput frameInput;
    frameInput.scenePhysicsEnabled = SceneState().isScenePhysics;
    frameInput.stepHeld = routedDeviceFrame.keys.IsDown( VK_SPACE ) || uiFrameResult.requestSceneStep;
    frameInput.replayScrubbedHistoricalSample = replayInput.scrubPaused;
    frameInput.replayLiveHeldAtCurrentFrame = replayInput.liveAdvanceHeld;
    frameInput.crossScenePauseLocked = sceneController.CrossScenePauseLocked();
    frameInput.rightMouseLookHeld = mouseEdges.rightDown;
    frameInput.editorViewportLookActive = editorTools.Editor().viewportLookActive;
    frameInput.replayInspectionLookActive = replayInput.inspectionActive && routedDeviceFrame.rightDown &&
                                            !routedUiSnapshot.wantsNativeCursor && !routedUiSnapshot.blocksCameraMouse;

    frameInput.forcePhysicsRunning = false;
    frameInput.sceneTimeScale = SceneState().timeScale;
    const RuntimeInputSnapshot& inputSnapshot = inputRouter.PublishRuntimeSnapshot( frameInput,
                                                                                    suppressWorldActionThisFrame );

    // Invariant: InputRouter samples both world rays before the first domain
    // owner can mutate selection, camera, or scene state. Consumers receive the
    // existing semantic pointer value plus only their focused leaf operands.
    const RuntimePointerRouteResult pointerResult = RouteRuntimePointer( inputSnapshot.pointer, replayInput.inspectionActive,
                                                                         SkullbonezCore::Core::ActiveSceneObjectCapacity(
                                                                             config ),
                                                                         NormalizeInputCameraMode(
                                                                             replayInput.restoreCameraMode ) );

    if ( pointerResult.enteredInteractiveScene )
    {
        EnterInteractiveInputScene();
    }

    for ( std::size_t actionIndex = 0; actionIndex < pointerResult.modeActionCount; ++actionIndex )
    {
        inputRouter.RecordModeAction( camera, editorTools, interaction, attachedCamera, runtimeInput,
                                      pointerResult.modeActions[actionIndex], RuntimeInputActionSource::Mouse );
    }

    PublishInputRecordingDiagnostics( debug );

    ApplyInputCameraControls( externalUiCapture, inputActions, inputSnapshot );

    // Persistence and scene transitions observe the final UI-mutated values.
    ApplyDeferredInputOwnerRequests( presentationEdit );

    CommitInputPointerPresentation( externalUiCapture );
    return CompleteRuntimeInputPhase( gameUiActive, requestDevelopmentUiSurfaceSwap );
}
