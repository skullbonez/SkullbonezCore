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
#include "../../UI/UI.h"
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


Run::FrameInputPhaseResult Run::RunInputPhase( const InteractionAutomationFrameResult* automationBeforeInput )
{
    UI::InputCaptureIntent externalUiCapture;
    SkullbonezCore::UI::OperatorEditorCommandQueues externalEditorCommands;
    bool gameUiActive = true;
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
    RunLaunchOptions& launchOptions = m_launchOptions;
    RuntimeFrameMetricsOwner& timers = m_timers;
    CameraControlState& camera = m_camera;
    RuntimeOverlayPresentationEdit presentationEdit = m_overlayDiagnostics->EditPresentation();
    OverlayDebugState& debug = presentationEdit.State();
    ApplicationExitState& applicationExit = m_applicationExit;
    RenderDefaultsStore& renderDefaults = m_renderDefaults;
    Assets::AssetSystem& assets = m_assets;
    Threading::WorkerPool& workerPool = m_workerPool;
    Window& window = m_window;
    RuntimeInteractionController& interaction = m_interaction;
    AttachedCameraController& attachedCamera = m_attachedCamera;
    SimulationSystem& simulation = m_simulation;
    SkullbonezCore::UI::InGameUI& ui = *m_operatorUi;
    RuntimeValidationHarness& validationHarness = *m_validationHarness;
    RuntimeTools& runtimeTools = m_runtimeTools;
    RuntimeRenderer& renderer = Renderer();
    SceneController& sceneController = m_sceneController;
    ReplayRuntime& replayRuntime = m_replayRuntime;

    // Lifetime: these aliases expose InputRouter-owned frame state only for
    // this synchronous routing pass; Run retains neither value as member state.
    RuntimeInputContext& runtimeInput = inputRouter.RuntimeContext();
    InputActions& inputActions = inputRouter.Actions();
    const auto SceneState = [&]() -> SceneSessionState& { return sceneController.State(); };

    const auto CompleteInputPhase = [&]()
    {

#if defined( SKULLBONEZ_DEVELOPMENT_TOOLS )

        if ( launchOptions.developmentUiModeExplicit || m_imguiEditor.HasActivatedSurfaceSelection() )
        {
            // Invariant: scene load may apply a GameUI default during input. An
            // explicit process selection wins before either UI begins its frame.
            SelectDevelopmentUiSurface( m_imguiEditor.SelectedSurface() );
        }

        if ( requestDevelopmentUiSurfaceSwap )
        {
            SelectDevelopmentUiSurface( DevelopmentUiMode::ImGui );
        }

        // RunInputPhase may consume Ctrl+0 after its snapshot; resample only
        // after every pre-render swap so both surfaces cannot draw concurrently.
        gameUiActive = m_imguiEditor.SelectedSurface() == DevelopmentUiMode::GameUI;
#endif
        const SceneFrameProceedPolicy proceedPolicy = sceneController.BuildFrameProceedPolicy( inputRouter.RuntimeSnapshot().frameInput.stepHeld );

        AuthoredScene liveStyle;

        if ( validationHarness.PollLiveStyle( assets, liveStyle ) )
        {
            sceneController.ApplyLiveStyle( launchOptions, ui.SceneNavigation().browser,
                                            ActiveSceneCinematicConfig( sceneController.State(), config ),
                                            renderDefaults.CinematicBaseline(), liveStyle );
            validationHarness.MarkLiveStyleApplied();
        }

        return FrameInputPhaseResult { proceedPolicy, gameUiActive };
    };

    const auto NormalizeCameraModeForCurrentScene = [&]( RunCameraMode mode )
    {
        const SceneSessionState& sceneState = SceneState();

        const int sceneEntityCount = sceneController.Scene().SceneEntityCount();
        const uint32_t cameraModeEnabledMask = RuntimeCameraModeEnabledMask( sceneState.isSceneMode, sceneEntityCount );
        return NormalizeRuntimeCameraMode( mode, sceneState.isSceneMode, cameraModeEnabledMask );
    };

    const auto CameraModeEnabledMask = [&]()
    { return RuntimeCameraModeEnabledMask( SceneState().isSceneMode, sceneController.Scene().SceneEntityCount() ); };

    const auto EnterInteractiveSceneRun = [&]()
    {
        sceneController.EnterInteractiveRun();

        m_capture.DisableAutomationExit();
    };

    const auto runUIStressBatch = [&]()
    {
        // Invariant: the scene-authored stress harness mutates GameUI state.
        // Once ImGui owns the development surface, allowing that harness to
        // re-show GameUI would violate the exclusive focus/visibility contract.
        if ( !gameUiActive )
        {
            return SkullbonezCore::Core::SbResult::Success();
        }

        presentationEdit.Commit();
        const SkullbonezCore::Core::SbResult result = this->RunUIStressActions();

        presentationEdit.Refresh();
        return result;
    };

    const auto DrainCaptureRequests = [&]()
    {
        CaptureController& capture = m_capture;

        if ( capture.PendingScreenshotCount() == 0 )
        {
            return false;
        }

        const CaptureRequestBatchResult batch = capture.DrainScreenshotRequests( BackbufferCapture() );

        if ( !batch.status.Ok() )
        {
            std::fprintf( stderr, "%s: %s\n", batch.status.ErrorOwner(), batch.status.ErrorMessage() );
            std::fflush( stderr );
        }

        for ( std::size_t index = 0; index < batch.savedCount; ++index )
        {
            replayRuntime.SubmitEvent( ReplayEventCommandOperations::BuildCommand( ReplayEventKind::OwnerAction, 0, true, 0,
                                                                                   static_cast<int32_t>( ReplayOwnerEventCode::CaptureScreenshot ),
                                                                                   0, 0, 0, 0, batch.saved[index].path ) );
        }

        return true;
    };

    const auto DrainRenderDefaultRequests = [&]()
    {
        if ( renderDefaults.PendingCount() == 0 )
        {
            return false;
        }

        const RenderDefaultsSaveBatchResult
            batch = renderDefaults.DrainAtFrameCheckpoint( config.ordinaryRender,
                                                           ActiveSceneCinematicConfig( SceneState(), config ) );

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

            replayRuntime.SubmitEvent( ReplayEventCommandOperations::BuildCommand( ReplayEventKind::OwnerAction, 0, true, 0,
                                                                                   static_cast<int32_t>( code ), 0, 0, 0, 0,
                                                                                   ReplayOwnerEventName( code ) ) );
        }

        return true;
    };

    DeviceInputFrame deviceFrame;
    const SkullbonezCore::Core::SbResult deviceCaptureResult = Input::CaptureDeviceInputFrame( m_resultDiagnostics,
                                                                                               deviceFrame );

    if ( !deviceCaptureResult.Ok() )
    {
        ReportRuntimeInputFailure( deviceCaptureResult );
        std::fflush( stderr );
        applicationExit.RequestPhaseFailure( deviceCaptureResult );
        PostQuitMessage( 1 );
        return CompleteInputPhase();
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
    auto commitPointerPresentation = [&]()
    {
        if ( externalUiCapture.mouse )
        {
            // Hazard: imgui_impl_win32 owns the same HWND capture while a tool
            // drag is active. Do not let the engine release it at a frame edge;

            // DeferPointerPresentationCommit below forces a complete reapply
            // when mouse intent returns to the engine.
            return;
        }

        PointerPresentationState presentation;

        if ( !inputRouter.ConsumePointerPresentationChange( presentation ) )
        {
            return;
        }

        SkullbonezCore::Core::SbResult pointerResult = Input::SetNativeMouseCapture( m_resultDiagnostics,
                                                                                     presentation.nativeCapture );

        if ( pointerResult.Ok() )
        {
            Input::SetSystemCursorVisible( presentation.cursorVisible );
        }

        if ( !pointerResult.Ok() )
        {
            ReportRuntimeInputFailure( pointerResult );
            applicationExit.RequestPhaseFailure( pointerResult );
            PostQuitMessage( 1 );
        }
    };

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

    if ( inputRouter.HandleUnfocusedFrame( runtimeTools, interaction, attachedCamera, camera, ui, sceneController,
                                           replayRuntime, runtimeInput ) )
    {
        const SkullbonezCore::Core::SbResult stressResult = runUIStressBatch();

        if ( !stressResult.Ok() )
        {
            // Recoverable error: focus loss still routes stress churn through the same guarded
            // rebuild path. End the run before returning to the frame loop.
            ReportRuntimeInputFailure( stressResult );
            std::fflush( stderr );
            applicationExit.RequestPhaseFailure( stressResult );
            PostQuitMessage( 1 );
        }

        commitPointerPresentation();
        return CompleteInputPhase();
    }

    const bool UIBlocksKeyboardBeforeInput = ui.BlocksKeyboard() || externalUiCapture.keyboard || externalUiCapture.text;

    inputRouter.ApplyPointerPresentation( EvaluateRuntimePointerPresentation( inputRouter, runtimeTools.Editor(), replayRuntime.BuildInputView() ) );

    InputController::BeginFrame( runtimeInput,
                                 BuildRuntimeInputModeState( camera.mode, runtimeTools.Editor(), interaction.Gesture(),
                                                             attachedCamera.State().activeFollow, camera.director.grabbed ),
                                 true, UIBlocksKeyboardBeforeInput, ui.BlocksCameraMouse() || externalUiCapture.mouse );

    bool keyboardToggleEditorMode = false;
    EditorKeyboardShortcutResult keyboardEditorToolShortcut;
    auto completeEditorPlacementModeTransition = [&]( RuntimeInputActionSource source,
                                                     const EditorPlacementModeChangeResult& placementMode )
    {
        inputRouter.SetWorldInteractionOwner( placementMode.worldOwner, InteractionExitReason::EnterEdit, runtimeTools,
                                              interaction, attachedCamera, camera, sceneController, replayRuntime,
                                              NormalizeCameraModeForCurrentScene( replayRuntime.BuildInputView().restoreCameraMode ) );

        if ( inputRouter.ReleasePointerToUi( EvaluateRuntimePointerPresentation( inputRouter, runtimeTools.Editor(), replayRuntime.BuildInputView() ) ) )
        {
            InputController::ResetMouseLook( camera );
        }

        inputRouter.ApplyPointerPresentation( EvaluateRuntimePointerPresentation( inputRouter, runtimeTools.Editor(), replayRuntime.BuildInputView() ) );

        inputRouter.RecordModeAction( camera, runtimeTools, interaction, attachedCamera, runtimeInput,
                                      RuntimeInputAction::ToggleEditorTool, source );
    };

    auto applyEditorPlacementModeToggle = [&]( RuntimeInputActionSource source )
    {
        EnterInteractiveSceneRun();

        const EditorPlacementModeChangeResult placementMode = ToggleEditorPlacementMode( runtimeTools.Editor(),
                                                                                         interaction );

        completeEditorPlacementModeTransition( source, placementMode );
    };

    const bool flyCamera = RunCameraModeUsesFlyControls( camera.mode, attachedCamera.State().activeFollow,
                                                         camera.director.grabbed );

    const KeyboardContextFacts keyboardContextFacts { !UIBlocksKeyboardBeforeInput,
                                                      SceneState().isSceneMode,
                                                      flyCamera,
                                                      RunCameraModeUsesLauncher( camera.mode ),
                                                      RunCameraModeIsAttached( camera.mode ),
                                                      camera.mode == RunCameraMode::Director,
                                                      camera.mode == RunCameraMode::Director || flyCamera,
                                                      runtimeTools.Editor().editorModeEnabled,
                                                      !replayRuntime.BuildInputView().restoreConsumedThisFrame,
                                                      false };

    inputRouter.RoutePhase( keyboardBindings, InputActionPhase::PreUi, BuildKeyboardContextMask( keyboardContextFacts ),
                            inputActions );

    if ( inputActions.Overflowed() )
    {
        SB_FATAL( "InputRouter", "Fixed input action capacity exhausted while routing pre-UI actions." );
    }

    const auto executeSceneLoadRequest = [&]( const SceneLoadRequest& request )
    {
        if ( !request.accepted )
        {
            return false;
        }

        PrepareSceneScopedOwnersForTransition();
        presentationEdit.Commit();
        SceneLoadTransaction sceneLoad;
        sceneLoad.CaptureSubmittedState( camera, CaptureSceneLoadNavigationState( ui.SceneNavigation() ),
                                         ProjectScenePresentationValues( debug ),
                                         { renderer.VsyncEnabled(), renderer.PipelineSyncEnabled() },
                                         renderer.RendererName(), timers.SimulationTotalSeconds() );

        const bool loaded = LoadSceneRequest( sceneLoad, request ).Ok();

        ApplyRuntimeFrameMetricsLifecycle( m_metricsSceneLifecyclePolicy, sceneController.LifecyclePacket(), timers );
        ApplySceneLoadRuntimeReactions( sceneLoad, launchOptions, *m_overlayDiagnostics, m_capture,
                                        m_overlaySceneLifecycleObserver, sceneController,
                                        m_inputSceneLifecycleObserver, inputRouter, interaction,
                                        m_cameraSceneLifecycleObserver, camera,
                                        m_attachedCameraSceneLifecycleObserver, attachedCamera, runtimeTools,
                                        replayRuntime );

        ApplySceneLoadPresentation( sceneLoad, window, ui, validationHarness, launchOptions, &renderer.RenderDevice(),
                                    renderer.VsyncEnabled(), sceneController );

        presentationEdit.Refresh();
        return loaded;
    };

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

        switch ( event.action )
        {
        case RuntimeInputAction::RerollLookLab:
        {
            const uint64_t seed = m_lookLab.NextAuthoringSeed();

            if ( !ApplyLookLabSeed( seed ) )
            {
                const LookLabStatusView status = m_lookLab.Status();
                std::fprintf( stderr, "Runtime/Direction/LookLabController: %s\n", status.detail.data() );
            }

            break;
        }
        case RuntimeInputAction::SaveLookLabBundle:
            BeginLookLabSave();
            break;
        case RuntimeInputAction::ToggleInteractionRecording:
        {
            if ( m_interactionRecorder.IsRecording() )
            {
                const Core::SbResult save = m_interactionRecorder.StopAndSave( m_resultDiagnostics, "operator", false );

                if ( !save.Ok() )
                {
                    applicationExit.RequestPhaseFailure( save );
                }
                else
                {
                    ui.SceneNavigation().RefreshInteractionRecordings();
                }

                break;
            }

            bool idle = !deviceFrame.leftDown && !deviceFrame.rightDown && !deviceFrame.middleDown &&
                        deviceFrame.wheelDelta == 0 && deviceFrame.rawMouseX == 0 && deviceFrame.rawMouseY == 0 &&
                        interaction.Gesture().kind == RuntimeInteractionGestureKind::None && !externalUiCapture.text;
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
                std::fprintf( stderr, "[recorder] Start rejected: release other keys/buttons and finish the active gesture "
                                      "first.\n" );
                break;
            }

            const Core::SbResult arm = m_interactionRecorder.Arm( m_resultDiagnostics, nullptr,
                                                                  launchOptions.interactionRecordMaxMinutes );

            if ( !arm.Ok() )
            {
                applicationExit.RequestPhaseFailure( arm );
            }

            break;
        }
        case RuntimeInputAction::ToggleEditor:

            // Backtick is captured early but applied after UI command processing.
            keyboardToggleEditorMode = true;
            break;
        case RuntimeInputAction::UndoEditor:

            if ( runtimeTools.Editor().editorModeEnabled && deviceFrame.keys.IsDown( VK_CONTROL ) )
            {
                if ( deviceFrame.keys.IsDown( VK_SHIFT ) )
                {
                    (void)runtimeTools.RedoEditorCommand( sceneController.Scene(), sceneController.State() );
                }
                else
                {
                    (void)runtimeTools.UndoEditorCommand( sceneController.Scene(), sceneController.State() );
                }
            }

            break;
        case RuntimeInputAction::RedoEditor:

            if ( runtimeTools.Editor().editorModeEnabled && deviceFrame.keys.IsDown( VK_CONTROL ) )
            {
                (void)runtimeTools.RedoEditorCommand( sceneController.Scene(), sceneController.State() );
            }

            break;
        case RuntimeInputAction::DeleteEditorSelection:

            if ( runtimeTools.Editor().editorModeEnabled )
            {
                (void)runtimeTools.DeleteEditorSelection( sceneController.Scene(), sceneController.State() );
            }

            break;
        case RuntimeInputAction::CycleCameraMode:
            inputRouter.CycleCameraMode( runtimeTools, interaction, attachedCamera, camera, sceneController, replayRuntime,
                                         runtimeInput );

            break;
        case RuntimeInputAction::ToggleFlyCamera:
        {
            const RunCameraMode passiveMode = SceneState().isSceneMode ? RunCameraMode::Scene : RunCameraMode::Demo;
            inputRouter.ApplyCameraMode( camera.mode == RunCameraMode::Inspect ? passiveMode : RunCameraMode::Inspect,
                                         event.source, runtimeTools, interaction, attachedCamera, camera, sceneController,
                                         replayRuntime, runtimeInput );

            break;
        }
        case RuntimeInputAction::ToggleLauncher:

            if ( camera.mode == RunCameraMode::Launcher )
            {
                inputRouter.ApplyCameraMode( camera.modeBeforeLauncher, event.source, runtimeTools, interaction,
                                             attachedCamera, camera, sceneController, replayRuntime, runtimeInput );
            }
            else
            {
                camera.modeBeforeLauncher = camera.mode == RunCameraMode::Manipulator ? RunCameraMode::Inspect : camera.mode;

                inputRouter.ApplyCameraMode( RunCameraMode::Launcher, event.source, runtimeTools, interaction,
                                             attachedCamera, camera, sceneController, replayRuntime, runtimeInput );
            }

            break;
        case RuntimeInputAction::CycleLauncherFireMode:

            if ( RunCameraModeUsesLauncher( camera.mode ) )
            {
                runtimeTools.RayCastTest().fireMode = runtimeTools.RayCastTest().fireMode == RunLauncherFireMode::Laser
                                                          ? RunLauncherFireMode::Projectile
                                                          : RunLauncherFireMode::Laser;
            }

            break;
        case RuntimeInputAction::CycleAttachedCameraSubmode:

            if ( RunCameraModeIsAttached( camera.mode ) && attachedCamera.CycleMode( sceneController.Scene() ) )
            {
                inputRouter.RecordModeAction( camera, runtimeTools, interaction, attachedCamera, runtimeInput,
                                              RuntimeInputAction::CycleAttachedCameraSubmode,
                                              RuntimeInputActionSource::Keyboard );
            }

            break;
        case RuntimeInputAction::ToggleAttachedCameraPin:

            if ( RunCameraModeIsAttached( camera.mode ) )
            {
                const bool activeFollow = attachedCamera.TogglePin( sceneController.Scene() );

                if ( !activeFollow )
                {
                    if ( inputRouter.ReleasePointerToUi( EvaluateRuntimePointerPresentation( inputRouter, runtimeTools.Editor(),
                                                                                             replayRuntime.BuildInputView() ) ) )
                    {
                        InputController::ResetMouseLook( camera );
                    }
                }

                inputRouter.ApplyPointerPresentation( EvaluateRuntimePointerPresentation( inputRouter, runtimeTools.Editor(),
                                                                                          replayRuntime.BuildInputView() ) );

                inputRouter.RecordModeAction( camera, runtimeTools, interaction, attachedCamera, runtimeInput,
                                              RuntimeInputAction::ToggleAttachedCameraPin,
                                              RuntimeInputActionSource::Keyboard );
            }

            break;
        case RuntimeInputAction::WriteLauncherReproSnapshot:
#ifdef _DEBUG

            if ( RunCameraModeUsesLauncher( camera.mode ) && !replayRuntime.BuildInputView().restoreConsumedThisFrame )
            {
                const double simulationSeconds = timers.SceneElapsedSeconds();
                runtimeTools
                    .WriteLauncherReproSnapshotWithStatusMessage( { sceneController.Scene(), SceneState(),
                                                                    sceneController.CurrentPath(), launchOptions,
                                                                    sceneController.Scene().Physics().IsSleepEnabled(),
                                                                    renderer.VsyncEnabled(), renderer.PipelineSyncEnabled(),
                                                                    config.bodySimulation.contactEpsilon,
                                                                    config.physicsMaterial.frictionCoeff, debug,
                                                                    renderer.RendererName(), simulationSeconds },
                                                                  debug );
            }
#endif
            break;
        case RuntimeInputAction::ToggleDirectorGrab:

            if ( camera.mode != RunCameraMode::Director )
            {
                break;
            }

            if ( camera.director.grabbed )
            {
                const DemoCameraPose currentPose = CaptureDemoDirectorPose( sceneController.Scene().Cameras() );

                if ( DemoDirectorPlayback::EndGrab( camera.director, true, currentPose ) )
                {
                    ExitFlyModeCamera( inputRouter, camera, sceneController.Scene().Cameras(),
                                       *sceneController.Scene().Terrain().Get(), SceneState().isSceneMode );

                    inputRouter.ApplyPointerPresentation( EvaluateRuntimePointerPresentation( inputRouter, runtimeTools.Editor(),
                                                                                              replayRuntime.BuildInputView() ) );

                    inputRouter.RecordModeAction( camera, runtimeTools, interaction, attachedCamera, runtimeInput,
                                                  event.action, event.source );
                }
            }
            else
            {
                const DemoCameraPose currentPose = CaptureDemoDirectorPose( sceneController.Scene().Cameras() );
                DemoDirectorCameraCommand cameraCommand;

                if ( DemoDirectorPlayback::BeginGrab( camera.director, true, currentPose, cameraCommand ) )
                {
                    if ( cameraCommand.applyPose )
                    {
                        sceneController.Scene().Cameras().SetPrimaryPose( cameraCommand.pose.eye, cameraCommand.pose.view,
                                                                          cameraCommand.pose.up );
                    }

                    EnterFlyModeCamera( inputRouter, camera, sceneController.Scene().Cameras(), SceneState().isSceneMode,
                                        runtimeTools.Editor(), replayRuntime.BuildInputView() );

                    inputRouter.ApplyPointerPresentation( EvaluateRuntimePointerPresentation(
                        inputRouter, runtimeTools.Editor(), replayRuntime.BuildInputView() ) );

                    inputRouter.RecordModeAction( camera, runtimeTools, interaction, attachedCamera, runtimeInput,
                                                  event.action, event.source );
                }
            }

            break;
        case RuntimeInputAction::SetDirectorPhasePose:

            if ( ( camera.mode == RunCameraMode::Director ||
                   RunCameraModeUsesFlyControls( camera.mode, attachedCamera.State().activeFollow,
                                                 camera.director.grabbed ) ) &&
                 DemoDirectorPlayback::SetCurrentPhasePose(
                     camera.director, CaptureDemoDirectorPose( sceneController.Scene().Cameras() ) ) )
            {
                inputRouter.RecordModeAction( camera, runtimeTools, interaction, attachedCamera, runtimeInput, event.action,
                                              event.source );
            }

            break;
        case RuntimeInputAction::StepDirectorPhase:

            if ( ( camera.mode == RunCameraMode::Director ||
                   RunCameraModeUsesFlyControls( camera.mode, attachedCamera.State().activeFollow,
                                                 camera.director.grabbed ) ) &&
                 DemoDirectorPlayback::SelectNextPhaseForAuthoring(
                     camera.director, CaptureDemoDirectorPose( sceneController.Scene().Cameras() ) ) )
            {
                inputRouter.RecordModeAction( camera, runtimeTools, interaction, attachedCamera, runtimeInput, event.action,
                                              event.source );
            }

            break;
        case RuntimeInputAction::SaveDirectorShotList:

            if ( ( camera.mode == RunCameraMode::Director ||
                   RunCameraModeUsesFlyControls( camera.mode, attachedCamera.State().activeFollow,
                                                 camera.director.grabbed ) ) &&
                 DemoDirectorPlayback::SaveShotList( camera.director ) )
            {
                inputRouter.RecordModeAction( camera, runtimeTools, interaction, attachedCamera, runtimeInput, event.action,
                                              event.source );
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
        case RuntimeInputAction::ToggleBroadphaseOverlay:
            HandleDiagnosticsKeyboardShortcut( debug, camera.trackBallRow.value, sceneController.Scene().SceneEntityCount(),
                                               renderer.RenderDiagnostics().GetCapabilities().supportsDxrReflection,
                                               SceneState().isSceneMode,
                                               timers.SceneElapsedSeconds(),
                                               ProjectDiagnosticsKeyboardCommand( event.action ), true );

            break;
        case RuntimeInputAction::ReloadShadersFromSource:
        {
            if ( !m_shaderDevelopment )
            {
                fprintf( stderr, "Shader hot reload unavailable: active backend has no development capability.\n" );
                break;
            }

            // Runtime allocation policy: F9 is an explicit cold developer utility. The
            // bake, manifest parse, reflection maps, and process launch belong
            // to BackendInit rather than steady input/render accounting.
            SkullbonezCore::Core::Allocation::RuntimeAllocationScope allocationScope( SkullbonezCore::Core::Allocation::RuntimeAllocationPhase::BackendInit );
            const SkullbonezCore::Core::SbResult reloadResult = m_shaderDevelopment->get().ReloadShadersFromSource();

            if ( !reloadResult.Ok() )
            {
                fprintf( stderr, "Shader hot reload failed: owner=%s reason=%s\n", reloadResult.ErrorOwner(),
                         reloadResult.ErrorMessage() );

                SkullbonezCore::Core::Log().WriteEventf( "shader_hot_reload_failed owner=%s reason=%s",
                                                         reloadResult.ErrorOwner(), reloadResult.ErrorMessage() );
            }

            break;
        }
        case RuntimeInputAction::CycleReplayPathColorMode:

            // Concept: comma changes a presentation value only. Existing
            // trajectory samples remain immutable and are recolored next draw.
            replayRuntime.CyclePathColorMode();
            break;
        case RuntimeInputAction::ToggleReplayGuideArcs:

            // Why: guide rings remain a GameUI-only teaching aid while ImGui
            // owns its separate development-tool presentation contract.
            if ( gameUiActive )
            {
                replayRuntime.ToggleGuideArcs();
            }

            break;
        case RuntimeInputAction::ToggleReplayTripPlanner:

            if ( gameUiActive )
            {
                (void)replayRuntime.QueueTripPlannerCommand( { ReplayTripPlannerCommandKind::TogglePanel } );
            }

            break;
        case RuntimeInputAction::ToggleReplayPorkchopPanel:

            if ( gameUiActive )
            {
                replayRuntime.TogglePorkchopPanel();
            }

            break;
        case RuntimeInputAction::ToggleCrossScenePause:
            sceneController.ToggleCrossScenePause();
            break;
        case RuntimeInputAction::ToggleReplayPlayPause:
        {
            ReplayWorkspaceOutput transportOutput;
            replayRuntime
                .ApplyTransportCommand( ReplayTransportCommand { ReplayTransportAction::TogglePlayPause },
                                        ReplayTransportHostContext { window.NativeWindowHandle(),
                                                                     NormalizeCameraModeForCurrentScene( camera.mode ),
                                                                     NormalizeCameraModeForCurrentScene( replayRuntime.BuildInputView().restoreCameraMode ),
                                                                     attachedCamera.State().activeFollow,
                                                                     camera.director.grabbed,
                                                                     timers.SimulationTotalSeconds() },
                                        inputRouter, interaction, &sceneController.Scene().Cameras(),
                                        sceneController.Scene().Terrain().Get(), camera, runtimeTools.MousePickup(),
                                        transportOutput );

            if ( transportOutput.enterInteractive )
            {
                EnterInteractiveSceneRun();
            }

            break;
        }
        case RuntimeInputAction::ToggleUIVisibility:
        case RuntimeInputAction::TogglePerformanceHistogram:
        case RuntimeInputAction::ToggleMemoryOverlay:
        {
            if ( event.action == RuntimeInputAction::ToggleUIVisibility && !gameUiActive )
            {
                // Invariant: the GameUI visibility shortcut is inert while the
                // active ImGui surface owns focus and input.
                break;
            }

            if ( event.action == RuntimeInputAction::ToggleUIVisibility && deviceFrame.keys.IsDown( VK_CONTROL ) )
            {
                // The input owner interprets the chord once; Run retains only
                // the process-wide decision about which surface becomes active.
                requestDevelopmentUiSurfaceSwap = true;
            }

            const DiagnosticsUIKeyboardShortcutResult
                shortcutResult = HandleDiagnosticsUIKeyboardShortcut(
                    ui, debug, timers.SimulationTotalSeconds(), ProjectDiagnosticsUiKeyboardCommand( event.action ), true );

            if ( shortcutResult.markInteractiveRun )
            {
                SceneState().isInteractiveRun = true;
            }
            if ( shortcutResult.disableExitOnComplete )
            {
                SceneState().isExitOnComplete = false;
            }
            if ( shortcutResult.disableCaptureAutomationExit )
            {
                m_capture.DisableAutomationExit();
            }

            if ( shortcutResult.triggered )
            {
                if ( shortcutResult.releaseMouseToUI )
                {
                    inputRouter.ApplyPointerPresentation( EvaluateRuntimePointerPresentation( inputRouter, runtimeTools.Editor(),
                                                                                              replayRuntime.BuildInputView() ) );

                    if ( inputRouter.ReleasePointerToUi( EvaluateRuntimePointerPresentation( inputRouter, runtimeTools.Editor(),
                                                                                             replayRuntime.BuildInputView() ) ) )
                    {
                        InputController::ResetMouseLook( camera );
                    }
                }

                inputRouter.RecordModeAction( camera, runtimeTools, interaction, attachedCamera, runtimeInput, event.action,
                                              event.source );
            }

            break;
        }
        case RuntimeInputAction::NavigateScenePrevious:
        case RuntimeInputAction::NavigateSceneNext:
        {
            const int direction = event.action == RuntimeInputAction::NavigateScenePrevious ? -1 : 1;
            EnterInteractiveSceneRun();
            const int currentSceneBrowserIndex = ui.SceneNavigation().browser.CurrentIndexForPath( sceneController.CurrentPath() );

            const bool isCinematicTabActive = ui.GetActiveTab() == InGameUITab::Cinematic;
            UI::SceneNavigationModel& sceneNavigation = ui.SceneNavigation();
            const int cinematicIndex = AdjacentCinematicModeBrowserIndex( sceneNavigation, direction,
                                                                          currentSceneBrowserIndex, isCinematicTabActive );

            const bool appliedCinematic = cinematicIndex >= 0 &&
                                          sceneController
                                              .ApplyCinematicBrowserStyle( launchOptions, ui.SceneNavigation().browser,
                                                                           assets,
                                                                           ActiveSceneCinematicConfig( SceneState(),
                                                                                                       config ),
                                                                           renderDefaults.CinematicBaseline(),
                                                                           cinematicIndex );

            if ( !appliedCinematic )
            {
                executeSceneLoadRequest( LoadAdjacentScene( sceneNavigation, direction, currentSceneBrowserIndex, sceneController ) );
            }

            break;
        }
        default:
            break;
        }
    }

    if ( runtimeTools.Editor().editorModeEnabled )
    {
        (void)replayRuntime.ApplyKeyboardVelocityEdit( { keyboardEditorToolShortcut.altDown, false, interaction.Owner(), timers.SimulationTotalSeconds() } );

        if ( keyboardEditorToolShortcut.togglePlacementMode )
        {
            applyEditorPlacementModeToggle( RuntimeInputActionSource::Keyboard );
        }
    }
    else
    {
        const ReplayKeyboardVelocityEditResult velocityEditResult = replayRuntime.ApplyKeyboardVelocityEdit( { keyboardEditorToolShortcut.altDown, true, interaction.Owner(), timers.SimulationTotalSeconds() } );

        if ( velocityEditResult.cancelToolDrag )
        {
            ReplayInteractionOperations::CancelToolDragState( interaction, inputRouter );
        }

        if ( velocityEditResult.enterInteractive )
        {
            EnterInteractiveSceneRun();
        }

        if ( velocityEditResult.cameraAction == ReplayKeyboardVelocityEditCameraAction::EnterInspection )
        {
            replayRuntime.EnterInspectionCamera( &sceneController.Scene().Cameras(), camera,
                                                 NormalizeCameraModeForCurrentScene( camera.mode ), interaction, inputRouter,
                                                 runtimeTools.MousePickup() );
        }
        else if ( velocityEditResult.cameraAction == ReplayKeyboardVelocityEditCameraAction::ExitInspection )
        {
            replayRuntime.ExitInspectionCamera( &sceneController.Scene().Cameras(), sceneController.Scene().Terrain().Get(),
                                                camera,
                                                NormalizeCameraModeForCurrentScene( replayRuntime.BuildInputView().restoreCameraMode ),
                                                attachedCamera.State().activeFollow, camera.director.grabbed, interaction,
                                                inputRouter );
        }

        if ( velocityEditResult.setWorldOwner )
        {
            inputRouter.SetWorldInteractionOwner( velocityEditResult.worldOwner, InteractionExitReason::EnterReplay,
                                                  runtimeTools, interaction, attachedCamera, camera, sceneController,
                                                  replayRuntime,
                                                  NormalizeCameraModeForCurrentScene( replayRuntime.BuildInputView().restoreCameraMode ) );
        }
    }

    ReplayPathPickInput replayPointerRay;
    replayPointerRay.hasWorldRay = inputRouter.TryBuildWorldRay( sceneController.Scene().Cameras(), window,
                                                                 replayPointerRay.rayOrigin, replayPointerRay.rayDirection );

    const RuntimeInputFrameFacts uiSamplingFacts { NormalizeCameraModeForCurrentScene( camera.mode ),
                                                   NormalizeCameraModeForCurrentScene( replayRuntime.BuildInputView().restoreCameraMode ),
                                                   CameraModeEnabledMask(),
                                                   UIBlocksKeyboardBeforeInput,
                                                   SkullbonezCore::Core::ActiveSceneObjectCapacity( config ),
                                                   externalUiCapture,
                                                   externalEditorCommands,
                                                   gameUiActive,
                                                   requestedReplayCauseRow };

    RuntimeUIFrameResult uiFrameResult = BeginRuntimeUIFrame( m_resultDiagnostics, window, inputRouter, camera, runtimeTools,
                                                              attachedCamera, interaction, ui, timers, sceneController,
                                                              replayRuntime, replayPointerRay, uiSamplingFacts );

    if ( uiFrameResult.frameActive )
    {
        if ( uiFrameResult.enterInteractiveScene )
        {
            EnterInteractiveSceneRun();
            uiFrameResult.enterInteractiveScene = false;
        }

        presentationEdit.Commit();
        const InputAfterUiDismissResult dismissResult = inputRouter.DispatchAfterUiDismiss(
            inputActions, uiFrameResult.commands.ui.userInteracted, timers.SimulationTotalSeconds(), gameUiActive,
            camera, attachedCamera, runtimeTools, ui, sceneController, *m_overlayDiagnostics,
            replayRuntime.BuildInputView() );

        if ( dismissResult.disableCaptureAutomationExit )
        {
            m_capture.DisableAutomationExit();
        }

        presentationEdit.Refresh();

        if ( dismissResult.quitRequested )
        {
            PostQuitMessage( 0 );
        }
    }

    const RuntimeInputFrameFacts commandFacts { NormalizeCameraModeForCurrentScene( camera.mode ),
                                                NormalizeCameraModeForCurrentScene( replayRuntime.BuildInputView().restoreCameraMode ),
                                                CameraModeEnabledMask(),
                                                uiFrameResult.suppressWorldActionThisFrame,
                                                SkullbonezCore::Core::ActiveSceneObjectCapacity( config ),
                                                externalUiCapture,
                                                externalEditorCommands,
                                                gameUiActive,
                                                requestedReplayCauseRow };

    presentationEdit.Commit();
    uiFrameResult = ApplyInputCommandsPhase( uiFrameResult, keyboardToggleEditorMode, commandFacts );

    presentationEdit.Refresh();

    if ( uiFrameResult.status.Ok() && uiFrameResult.frameActive )
    {
        uiFrameResult.status = runUIStressBatch();
    }

    uiFrameResult = FinishRuntimeUIFramePointer( uiFrameResult, inputRouter, camera, runtimeTools, interaction,
                                                 attachedCamera, ui, sceneController, replayRuntime,
                                                 NormalizeCameraModeForCurrentScene( camera.mode ) );

    if ( uiFrameResult.enterInteractiveScene )
    {
        EnterInteractiveSceneRun();
    }

    const ReplayLiveRestoreRequest& restoreRequest = uiFrameResult.replayWorkspace.restoreRequest;

    if ( restoreRequest.kind != ReplayLiveRestoreKind::None )
    {
        ReplaySceneTimelineResetInput
            timelineReset = DescribeReplaySceneTimeline( sceneController, ui.SceneNavigation().overrides, SceneState(),
                                                         SkullbonezCore::Core::ActiveSceneObjectCapacity( config ),
                                                         static_cast<uint32_t>( launchOptions.generatedObjectTypeOverride ) );
        timelineReset.preserveReplayInspection = uiFrameResult.replayWorkspace.planningTransitionToken != 0;

        ReplayRestoreTransaction transaction { timelineReset };
        bool restored = false;

        if ( restoreRequest.kind == ReplayLiveRestoreKind::V2ArtifactTarget )
        {
            restored = replayRuntime.RestoreV2ArtifactTargetState( transaction, restoreRequest, sceneController, debug,
                                                                   runtimeTools, simulation, config, assets, workerPool,
                                                                   ui.SceneNavigation().overrides,
                                                                   launchOptions.generatedObjectTypeOverride );
        }
        else if ( restoreRequest.kind == ReplayLiveRestoreKind::SolverSample && restoreRequest.solverSample )
        {
            restored = replayRuntime.RestoreSolverSampleAsLive( transaction, sceneController.Scene(), SceneState(), debug,
                                                                runtimeTools, *restoreRequest.solverSample );
        }
        else
        {
            transaction.FailBeforeMutation( "live solver restore request has no selected sample" );
        }

        ReplayLiveRestoreOutcome restoreOutcome = ReplayLiveRestoreOperations::BuildOutcome( transaction,
                                                                                             restoreRequest.kind, restored );

#ifdef _DEBUG
        replayRuntime.PublishRestoreDiagnostic( transaction, m_diagnosticsRuntime, SceneState() );
#endif
        replayRuntime.ApplyRestoredBranchTimeline( transaction, restoreOutcome, sceneController, inputRouter, interaction,
                                                   camera,
                                                   NormalizeCameraModeForCurrentScene( replayRuntime.BuildInputView().restoreCameraMode ),
                                                   attachedCamera.State().activeFollow, camera.director.grabbed );

        replayRuntime.CompleteLiveRestoreScrubber( transaction, restoreRequest, restoreOutcome );
        replayRuntime.CompletePlanningTransition( uiFrameResult.replayWorkspace.planningTransitionToken,
                                                  restoreOutcome.restored );

        if ( restoreOutcome.enterInteractive )
        {
            EnterInteractiveSceneRun();
        }
    }

    if ( !uiFrameResult.status.Ok() )
    {
        // Recoverable error: a generated-resource rebuild could not prove its GPU drain.
        // Stop this frame and end the run before any later world/input mutation.
        ReportRuntimeInputFailure( uiFrameResult.status );
        std::fflush( stderr );
        applicationExit.RequestPhaseFailure( uiFrameResult.status );
        PostQuitMessage( 1 );
        commitPointerPresentation();
        return CompleteInputPhase();
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
    frameInput.editorViewportLookActive = runtimeTools.Editor().viewportLookActive;
    frameInput.replayInspectionLookActive = replayInput.inspectionActive && routedDeviceFrame.rightDown &&
                                            !routedUiSnapshot.wantsNativeCursor && !routedUiSnapshot.blocksCameraMouse;

    frameInput.forcePhysicsRunning = false;
    frameInput.sceneTimeScale = SceneState().timeScale;
    const RuntimeInputSnapshot& inputSnapshot = inputRouter.PublishRuntimeSnapshot( frameInput,
                                                                                    suppressWorldActionThisFrame );

    // Invariant: InputRouter samples both world rays before the first domain
    // owner can mutate selection, camera, or scene state. Consumers receive the
    // existing semantic pointer value plus only their focused leaf operands.
    const RuntimePointerRouteResult
        pointerResult = inputRouter.RouteRuntimePointer( inputSnapshot.pointer, replayInput.inspectionActive,
                                                         SkullbonezCore::Core::ActiveSceneObjectCapacity( config ), window,
                                                         assets, runtimeTools, attachedCamera, interaction, camera,
                                                         sceneController, replayRuntime,
                                                         NormalizeCameraModeForCurrentScene( replayInput.restoreCameraMode ) );

    if ( pointerResult.enteredInteractiveScene )
    {
        EnterInteractiveSceneRun();
    }

    for ( std::size_t actionIndex = 0; actionIndex < pointerResult.modeActionCount; ++actionIndex )
    {
        inputRouter.RecordModeAction( camera, runtimeTools, interaction, attachedCamera, runtimeInput,
                                      pointerResult.modeActions[actionIndex], RuntimeInputActionSource::Mouse );
    }

    const InteractionRecordingStatusView recordingStatus = m_interactionRecorder.Status();
    debug.isInteractionRecording = m_interactionRecorder.IsActive();
    debug.interactionRecordingElapsedSeconds = recordingStatus.elapsedSeconds;
    debug.interactionRecordingMaximumMinutes = recordingStatus.maximumMinutes;
    debug.interactionRecordingFrameCount = recordingStatus.frameCount;
    debug.interactionRecordingFrameCapacity = recordingStatus.frameCapacity;
    strncpy_s( debug.interactionRecordingFailure, sizeof( debug.interactionRecordingFailure ), recordingStatus.failure,
               _TRUNCATE );
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

    if ( ui.BlocksKeyboard() || externalUiCapture.keyboard || externalUiCapture.text )
    {
        interaction.CancelCameraLookGesture();
        InputController::ResetMouseLook( camera );
        camera.input.moveForward = false;
        camera.input.moveBackward = false;
        camera.input.moveLeft = false;
        camera.input.moveRight = false;
        inputRouter.ApplyPointerPresentation( EvaluateRuntimePointerPresentation( inputRouter, runtimeTools.Editor(), replayRuntime.BuildInputView() ) );
    }
    else
    {
        const InputCaptureActionResult captureActions = inputRouter.DispatchCaptureActions(
            inputActions, camera, attachedCamera, ui, sceneController,
            m_overlayDiagnostics->PresentationSnapshot().GetSaveState(), replayRuntime.BuildInputView() );
        if ( captureActions.screenshotRequested )
        {
            HandleEditorScreenshotHotkey( m_capture, true );
        }

        const RuntimeInteractionFramePolicy inputPolicy = interaction.BuildFramePolicy( inputSnapshot.frameInput );
        const bool mouseOwnsCursor = EvaluateRuntimePointerPresentation( inputRouter, runtimeTools.Editor(),
                                                                         replayRuntime.BuildInputView() )
                                         .mouseLookOwnsCursor;

        interaction.SyncCameraLookGesture( inputSnapshot, inputPolicy, mouseOwnsCursor );
        const bool cameraMouseLookActive = inputPolicy.cameraMouseLookActive && mouseOwnsCursor && inputSnapshot.appFocused;

        if ( cameraMouseLookActive )
        {
            inputRouter.RequestNativeCapture();
            inputRouter.RequestCursorVisible( false );
        }

        const RuntimeCameraInputFrameResult
            cameraInputResult = InputController::ApplyCameraInputFrame( camera, inputSnapshot.appFocused,
                                                                        cameraMouseLookActive, mouseOwnsCursor,
                                                                        inputPolicy.cameraKeyboardControlsActive,
                                                                        inputRouter.DeviceFrame() );

        if ( cameraInputResult.applyCursorOwnership )
        {
            inputRouter.ApplyPointerPresentation( EvaluateRuntimePointerPresentation( inputRouter, runtimeTools.Editor(), replayRuntime.BuildInputView() ) );
        }
    }

    // Invariant: UI capture gates world/camera controls, not deferred owner
    // commands. Persistence samples final UI-mutated values before a same-frame
    // scene reset can replace config, even while an ImGui text field owns keys.
    const bool processedDefaults = DrainRenderDefaultRequests();
    const bool processedCapture = DrainCaptureRequests();
    presentationEdit.Commit();
    const SceneLoadNavigationState sceneLoadNavigation = CaptureSceneLoadNavigationState( ui.SceneNavigation() );

    if ( sceneController.HasPendingTransition() )
    {
        PrepareSceneScopedOwnersForTransition();
    }

    SceneLoadTransaction sceneLoad;
    sceneLoad.CaptureSubmittedState( camera, sceneLoadNavigation, ProjectScenePresentationValues( debug ),
                                     { renderer.VsyncEnabled(), renderer.PipelineSyncEnabled() },
                                     renderer.RendererName(), timers.SimulationTotalSeconds() );

    const bool processedScene = ExecutePendingSceneRequests( sceneLoad );

    ApplyRuntimeFrameMetricsLifecycle( m_metricsSceneLifecyclePolicy, sceneController.LifecyclePacket(), timers );
    ApplySceneLoadRuntimeReactions( sceneLoad, launchOptions, *m_overlayDiagnostics, m_capture,
                                    m_overlaySceneLifecycleObserver, sceneController,
                                    m_inputSceneLifecycleObserver, inputRouter, interaction,
                                    m_cameraSceneLifecycleObserver, camera,
                                    m_attachedCameraSceneLifecycleObserver, attachedCamera, runtimeTools, replayRuntime );

    ApplySceneLoadPresentation( sceneLoad, window, ui, validationHarness, launchOptions, &renderer.RenderDevice(),
                                renderer.VsyncEnabled(), sceneController );

    presentationEdit.Refresh();

    if ( processedCapture || processedDefaults || processedScene )
    {
    }

    commitPointerPresentation();
    return CompleteInputPhase();
}
