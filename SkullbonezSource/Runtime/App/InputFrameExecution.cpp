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
  Input turn: Ordered frame interval from immutable device sampling through UI
    routing and the final owner-specific request checkpoint.
  Pre-UI facts: Focus, key, and pointer values sampled before widgets can claim
    the gesture.
  Post-UI snapshot: Immutable hit/capture result published after widget layout
    so world tools do not reinterpret UI-owned input.
  Semantic action: Fixed ordered input event derived from sampled key edges,
    independent of the platform's live hardware state.
  Selected development surface: The one Legacy or ImGui implementation allowed
    to own development-tool input and visibility for the current frame.
  Input turn result: Value-only request returned to Run when an interpreted
    semantic action requires process-wide development-surface selection.

Invariants:
  - Device input is captured once; later phases consume router-owned values.
  - UI hit testing completes before pointer ownership is finalized.
  - Every concrete owner is borrowed synchronously for this call only.
  - An inactive Legacy surface cannot reactivate itself through stress actions.
  - Process policy consumes typed results; it never rescans InputRouter actions.

Related:
  - SkullbonezSource/Runtime/App/InputFrame.cpp implements shared value and UI-command policy.
  - SkullbonezSource/Runtime/Input/InputRouter.h owns retained input state.
  - Agentic/Reference/runtime-reference.md
  - Agentic/Reference/comment-style-guide.md
*/
#include "InputFrame.h"
#include "Run.h"
#if defined( SKULLBONEZ_DEVELOPMENT_TOOLS )
#include "../DevelopmentTools/ImGuiEditorLayoutPolicy.h"
#endif
#include "../Diagnostics/RuntimeOverlayDiagnostics.h"
#include "../Automation/RuntimeValidationHarness.h"
#include "../Camera/AttachedCameraController.h"
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
#include "RunLaunchOptions.h"
#include "RunStartupState.h"
#include "RunTimerState.h"
#include "../UI/RuntimeViewModel.h"
#include "../Tools/RuntimeTools.h"
#include "../Interaction/RuntimeInteractionCommands.h"
#include "../Scene/SceneGeneratedControlTransaction.h"
#include "../Scene/SceneLoadTransaction.h"
#include "../Scene/SceneCinematicPolicy.h"
#include "../Scene/SceneController.h"
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

using namespace SkullbonezCore::Runtime;
using namespace SkullbonezCore::Runtime::ReplayTimelineOperations;
using namespace SkullbonezCore::Math::CollisionDetection;
using namespace SkullbonezCore::Math::Orientation;
using namespace SkullbonezCore::Math::Transformation;
using namespace SkullbonezCore::Physics;
using namespace SkullbonezCore::UI::Layout;
using SkullbonezCore::Hardware::Input;
using SkullbonezCore::Hardware::InputState;
using SkullbonezCore::UI::InGameUITab;


// Concept: the input turn is an orchestration boundary, not a new domain owner.
// InputRouter owns sampling, edge memory, semantic order, and pointer policy;
// scene, replay, tools, diagnostics, UI, and rendering retain their own state
// and expose only synchronous operations for accepted input actions.
// Lifetime: the Run coordinator reaches composed owners only for this ordered
// input turn; delegated operations receive concrete operands and retain none.
Run::FrameInputPhaseResult Run::RunInputPhase( const InteractionAutomationFrameResult* automationBeforeInput )
{
    UiInputCaptureIntent externalUiCapture;
    SkullbonezCore::UI::OperatorEditorCommandQueues externalEditorCommands;
    bool legacyDevelopmentUiActive = true;
#if defined( SKULLBONEZ_DEVELOPMENT_TOOLS )
    externalUiCapture = m_imguiEditor.ConsumeInputCaptureIntent();
    externalEditorCommands = m_imguiEditor.ConsumeOperatorEditorCommands();
#if defined( SKULLBONEZ_AUTOMATION_DIAGNOSTICS )

    if ( automationBeforeInput )
    {
        const SkullbonezCore::Core::SbResult submitStatus = m_interactionAutomation
                                                                .SubmitOperatorEditorReplayCommand( *automationBeforeInput,
                                                                                                    externalEditorCommands );

        if ( !submitStatus.ok )
        {
            m_applicationExit.RequestOwnedFailure( submitStatus );
        }
    }
#else
    (void)automationBeforeInput;
#endif
    legacyDevelopmentUiActive = m_imguiEditor.SelectedSurface() == DevelopmentUiMode::Legacy;
#else
    (void)automationBeforeInput;
#endif
    bool requestDevelopmentUiSurfaceSwap = false;
    InputRouter& inputRouter = m_inputRouter;
    SkullbonezCore::Core::EngineConfig& config = m_config;
    RunLaunchOptions& launchOptions = m_launchOptions;
    const RunStartupState& startup = m_startup;
    RunTimerState& timers = m_timers;
    CameraControlState& camera = m_camera;
    RuntimeOverlayPresentationEdit presentationEdit = m_overlayDiagnostics->EditPresentation();
    OverlayDebugState& debug = presentationEdit.State();
    ApplicationExitState& applicationExit = m_applicationExit;
    RenderDefaultsStore& renderDefaults = m_renderDefaults;
    DiagnosticsRuntime& diagnosticsRuntime = m_diagnosticsRuntime;
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

            // Invariant: scene load may apply a Legacy default during input. An
            // explicit process selection wins before either UI begins its frame.
            SelectDevelopmentUiSurface( m_imguiEditor.SelectedSurface() );
        }

        if ( requestDevelopmentUiSurfaceSwap )
        {
            SelectDevelopmentUiSurface( DevelopmentUiMode::ImGui );
        }

        // RunInputPhase may consume Ctrl+0 after its snapshot; resample only
        // after every pre-render swap so both surfaces cannot draw concurrently.
        legacyDevelopmentUiActive = m_imguiEditor.SelectedSurface() == DevelopmentUiMode::Legacy;
#endif
        const SceneFrameProceedPolicy proceedPolicy = sceneController.BuildFrameProceedPolicy( inputRouter.RuntimeSnapshot().frameInput.stepHeld );

        validationHarness.TickLiveStyle( launchOptions, sceneController, ui.SceneNavigation().browser, assets,
                                         ActiveSceneCinematicConfig( sceneController.State(), config ),
                                         renderDefaults.CinematicBaseline() );

        return FrameInputPhaseResult { proceedPolicy, legacyDevelopmentUiActive };
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

        diagnosticsRuntime.Capture().DisableAutomationExit();
    };

    const auto runUIStressBatch = [&]()
    {

        // Invariant: the scene-authored stress harness mutates Legacy UI state.
        // Once ImGui owns the development surface, allowing that harness to
        // re-show Legacy would violate the exclusive focus/visibility contract.

        if ( !legacyDevelopmentUiActive )
        {
            return SkullbonezCore::Core::SbResult::Success();
        }

        presentationEdit.Commit();
        const SkullbonezCore::Core::SbResult result = this->RunUIStressActions( NormalizeCameraModeForCurrentScene( replayRuntime.BuildInputView().restoreCameraMode ) );

        presentationEdit.Refresh();
        return result;
    };

    const auto DrainCaptureRequests = [&]()
    {
        CaptureController& capture = diagnosticsRuntime.Capture();

        if ( capture.PendingScreenshotCount() == 0 )
        {
            return false;
        }

        const CaptureRequestBatchResult batch = capture.DrainScreenshotRequests( BackbufferCapture() );

        if ( !batch.status.ok )
        {
            std::fprintf( stderr, "%s: %s\n", batch.status.error.owner, batch.status.error.message );
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

            replayRuntime.SubmitEvent( ReplayEventCommandOperations::BuildCommand( ReplayEventKind::OwnerAction, 0, true, 0,
                                                                                   static_cast<int32_t>( code ), 0, 0, 0, 0,
                                                                                   ReplayOwnerEventName( code ) ) );
        }

        return true;
    };

    DeviceInputFrame deviceFrame;
    const SkullbonezCore::Core::SbResult deviceCaptureResult = Input::CaptureDeviceInputFrame( deviceFrame );

    if ( !deviceCaptureResult.ok )
    {
        ReportRuntimeInputFailure( deviceCaptureResult );
        std::fflush( stderr );
        applicationExit.RequestOwnedFailure( deviceCaptureResult );
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

        SkullbonezCore::Core::SbResult pointerResult = Input::SetNativeMouseCapture( presentation.nativeCapture );

        if ( pointerResult.ok )
        {
            Input::SetSystemCursorVisible( presentation.cursorVisible );
        }

        if ( !pointerResult.ok )
        {
            ReportRuntimeInputFailure( pointerResult );
            applicationExit.RequestOwnedFailure( pointerResult );
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

        if ( !stressResult.ok )
        {

            // Lane R: focus loss still routes stress churn through the same guarded
            // rebuild path. End the run before returning to the frame loop.
            ReportRuntimeInputFailure( stressResult );
            std::fflush( stderr );
            applicationExit.RequestOwnedFailure( stressResult );
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

        presentationEdit.Commit();
        SceneLoadTransaction sceneLoad;
        sceneLoad.CaptureSubmittedState( camera, CaptureSceneLoadNavigationState( ui.SceneNavigation() ), debug,
                                         renderer.RendererName(), timers.simulationTimer.GetTotalTime() );

        const bool loaded = sceneLoad
                                .Load( sceneController, request, config, launchOptions, renderDefaults.CinematicBaseline(),
                                       startup, assets, workerPool, diagnosticsRuntime, &renderer.RenderFrame(),
                                       &renderer.RenderResources(), renderer )
                                .ok;

        sceneLoad.ApplyRuntimeReactions( launchOptions, timers, *m_overlayDiagnostics, sceneController, inputRouter,
                                         interaction, camera, attachedCamera, runtimeTools, replayRuntime );

        sceneLoad.ApplyPresentationOutputs( window, ui, validationHarness, launchOptions, &renderer.RenderDevice(),
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
                const double simulationSeconds = timers.simulationTimer.GetTimeSinceLastStart();
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

                if ( DemoDirectorPlayback::EndGrab( camera, sceneController.Scene().Cameras() ) )
                {
                    ExitFlyModeCamera( inputRouter, camera, sceneController.Scene().Cameras(),
                                       *sceneController.Scene().Terrain().Get(), SceneState().isSceneMode );

                    inputRouter.ApplyPointerPresentation( EvaluateRuntimePointerPresentation( inputRouter, runtimeTools.Editor(),
                                                                                              replayRuntime.BuildInputView() ) );

                    inputRouter.RecordModeAction( camera, runtimeTools, interaction, attachedCamera, runtimeInput,
                                                  event.action, event.source );
                }
            }
            else if ( DemoDirectorPlayback::BeginGrab( camera, sceneController.Scene().Cameras() ) )
            {
                EnterFlyModeCamera( inputRouter, camera, sceneController.Scene().Cameras(), SceneState().isSceneMode,
                                    runtimeTools.Editor(), replayRuntime.BuildInputView() );

                inputRouter.ApplyPointerPresentation( EvaluateRuntimePointerPresentation( inputRouter, runtimeTools.Editor(),
                                                                                          replayRuntime.BuildInputView() ) );

                inputRouter.RecordModeAction( camera, runtimeTools, interaction, attachedCamera, runtimeInput, event.action,
                                              event.source );
            }

            break;
        case RuntimeInputAction::SetDirectorPhasePose:

            if ( ( camera.mode == RunCameraMode::Director ||
                   RunCameraModeUsesFlyControls( camera.mode, attachedCamera.State().activeFollow,
                                                 camera.director.grabbed ) ) &&
                 DemoDirectorPlayback::SetCurrentPhasePose( camera, sceneController.Scene().Cameras() ) )
            {
                inputRouter.RecordModeAction( camera, runtimeTools, interaction, attachedCamera, runtimeInput, event.action,
                                              event.source );
            }

            break;
        case RuntimeInputAction::StepDirectorPhase:

            if ( ( camera.mode == RunCameraMode::Director ||
                   RunCameraModeUsesFlyControls( camera.mode, attachedCamera.State().activeFollow,
                                                 camera.director.grabbed ) ) &&
                 DemoDirectorPlayback::SelectNextPhaseForAuthoring( camera, sceneController.Scene().Cameras() ) )
            {
                inputRouter.RecordModeAction( camera, runtimeTools, interaction, attachedCamera, runtimeInput, event.action,
                                              event.source );
            }

            break;
        case RuntimeInputAction::SaveDirectorShotList:

            if ( ( camera.mode == RunCameraMode::Director ||
                   RunCameraModeUsesFlyControls( camera.mode, attachedCamera.State().activeFollow,
                                                 camera.director.grabbed ) ) &&
                 DemoDirectorPlayback::SaveShotList( camera ) )
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
                                               &renderer.RenderDiagnostics(), SceneState().isSceneMode,
                                               timers.simulationTimer.GetTimeSinceLastStart(), event.action, true );

            break;
        case RuntimeInputAction::ReloadShadersFromSource:
        {

            if ( !m_shaderDevelopment )
            {
                fprintf( stderr, "Shader hot reload unavailable: active backend has no development capability.\n" );
                break;
            }

            // Allocation policy: F9 is an explicit cold developer utility. The
            // bake, manifest parse, reflection maps, and process launch belong
            // to BackendInit rather than steady input/render accounting.
            SkullbonezCore::Core::Allocation::RuntimeAllocationScope allocationScope( SkullbonezCore::Core::Allocation::RuntimeAllocationPhase::BackendInit );
            const SkullbonezCore::Core::SbResult reloadResult = m_shaderDevelopment->get().ReloadShadersFromSource();

            if ( !reloadResult.ok )
            {
                fprintf( stderr, "Shader hot reload failed: owner=%s reason=%s\n", reloadResult.error.owner,
                         reloadResult.error.message );

                SkullbonezCore::Core::Log().WriteEventf( "shader_hot_reload_failed owner=%s reason=%s",
                                                         reloadResult.error.owner, reloadResult.error.message );
            }

            break;
        }
        case RuntimeInputAction::CycleReplayPathColorMode:

            // Concept: comma changes a presentation value only. Existing
            // trajectory samples remain immutable and are recolored next draw.
            replayRuntime.CyclePathColorMode();
            break;
        case RuntimeInputAction::ToggleReplayGuideArcs:

            // Why: guide rings remain a Legacy-only teaching aid while ImGui
            // owns its separate development-tool presentation contract.

            if ( legacyDevelopmentUiActive )
            {
                replayRuntime.ToggleGuideArcs();
            }

            break;
        case RuntimeInputAction::ToggleReplayTripPlanner:

            if ( legacyDevelopmentUiActive )
            {
                (void)replayRuntime.QueueTripPlannerCommand( { ReplayTripPlannerCommandKind::TogglePanel } );
            }

            break;
        case RuntimeInputAction::ToggleReplayPorkchopPanel:

            if ( legacyDevelopmentUiActive )
            {
                replayRuntime.TogglePorkchopPanel();
            }

            break;
        case RuntimeInputAction::ToggleCrossScenePause:

            // P locks scene automation without turning the run interactive;
            // SceneController preserves the policy across load transactions.
            sceneController.ToggleCrossScenePause();
            break;
        case RuntimeInputAction::ToggleUIVisibility:
        case RuntimeInputAction::TogglePerformanceHistogram:
        case RuntimeInputAction::ToggleMemoryOverlay:
        {

            if ( event.action == RuntimeInputAction::ToggleUIVisibility && !legacyDevelopmentUiActive )
            {

                // Invariant: the legacy visibility shortcut is inert while the
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
                shortcutResult = HandleDiagnosticsUIKeyboardShortcut( ui, debug, SceneState(), diagnosticsRuntime.Capture(),
                                                                      timers.simulationTimer.GetTotalTime(), event.action,
                                                                      true );

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
        (void)replayRuntime.ApplyKeyboardVelocityEdit( { keyboardEditorToolShortcut.altDown, false, interaction.Owner(), timers.simulationTimer.GetTotalTime() } );

        if ( keyboardEditorToolShortcut.togglePlacementMode )
        {
            applyEditorPlacementModeToggle( RuntimeInputActionSource::Keyboard );
        }
    }
    else
    {
        const ReplayKeyboardVelocityEditResult velocityEditResult = replayRuntime.ApplyKeyboardVelocityEdit( { keyboardEditorToolShortcut.altDown, true, interaction.Owner(), timers.simulationTimer.GetTotalTime() } );

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
                                                   legacyDevelopmentUiActive };

    RuntimeUIFrameResult uiFrameResult = BeginRuntimeUIFrame( window, inputRouter, camera, runtimeTools, attachedCamera,
                                                              interaction, ui, timers, sceneController, replayRuntime,
                                                              replayPointerRay, uiSamplingFacts );

    if ( uiFrameResult.frameActive )
    {

        if ( uiFrameResult.enterInteractiveScene )
        {
            EnterInteractiveSceneRun();
            uiFrameResult.enterInteractiveScene = false;
        }

        presentationEdit.Commit();
        const bool quitRequested = inputRouter.DispatchAfterUiDismiss( inputActions,
                                                                       uiFrameResult.commands.ui.userInteracted,
                                                                       timers.simulationTimer.GetTotalTime(),
                                                                       legacyDevelopmentUiActive, diagnosticsRuntime, camera,
                                                                       attachedCamera, runtimeTools, ui, sceneController,
                                                                       *m_overlayDiagnostics,
                                                                       replayRuntime.BuildInputView() );

        presentationEdit.Refresh();

        if ( quitRequested )
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
                                                legacyDevelopmentUiActive };

    presentationEdit.Commit();
    uiFrameResult = ApplyInputCommandsPhase( uiFrameResult, keyboardToggleEditorMode, commandFacts );

    presentationEdit.Refresh();

    if ( uiFrameResult.status.ok && uiFrameResult.frameActive )
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
        const ReplaySceneTimelineResetInput
            timelineReset = DescribeReplaySceneTimeline( sceneController, ui.SceneNavigation().overrides, SceneState(),
                                                         SkullbonezCore::Core::ActiveSceneObjectCapacity( config ),
                                                         static_cast<uint32_t>( launchOptions.generatedObjectTypeOverride ) );

        ReplayRestoreTransaction transaction { timelineReset };
        ReplayLiveRestoreOutcome restoreOutcome = replayRuntime
                                                      .ApplyLiveRestoreRequest( transaction, restoreRequest,
                                                                                sceneController.Scene(), SceneState(), debug,
                                                                                runtimeTools, simulation, config, assets,
                                                                                workerPool, ui.SceneNavigation().overrides,
                                                                                launchOptions.generatedObjectTypeOverride );

        replayRuntime.CompleteLiveRestoreRequest( transaction, restoreRequest, restoreOutcome, sceneController.Scene(),
                                                  SceneState(), diagnosticsRuntime, inputRouter, interaction, camera,
                                                  NormalizeCameraModeForCurrentScene( replayRuntime.BuildInputView().restoreCameraMode ),
                                                  attachedCamera.State().activeFollow, camera.director.grabbed );

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
        applicationExit.RequestOwnedFailure( uiFrameResult.status );
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

    if ( ui.BlocksKeyboard() || externalUiCapture.keyboard || externalUiCapture.text )
    {
        interaction.CancelCameraLookGesture();
        InputController::ResetMouseLook( camera );
        camera.input.Set( InputState::Up, false );
        camera.input.Set( InputState::Down, false );
        camera.input.Set( InputState::Left, false );
        camera.input.Set( InputState::Right, false );
        inputRouter.ApplyPointerPresentation( EvaluateRuntimePointerPresentation( inputRouter, runtimeTools.Editor(), replayRuntime.BuildInputView() ) );
    }
    else
    {
        inputRouter.DispatchCaptureActions( inputActions, diagnosticsRuntime, camera, attachedCamera, ui, sceneController,
                                            m_overlayDiagnostics->PresentationSnapshot().GetSaveState(),
                                            replayRuntime.BuildInputView() );

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

    SceneLoadTransaction sceneLoad;
    sceneLoad.CaptureSubmittedState( camera, sceneLoadNavigation, debug, renderer.RendererName(),
                                     timers.simulationTimer.GetTotalTime() );

    const bool processedScene = sceneController.ExecutePending( sceneLoad, config, launchOptions,
                                                                renderDefaults.CinematicBaseline(), startup, assets,
                                                                workerPool, diagnosticsRuntime, &renderer.RenderFrame(),
                                                                &renderer.RenderResources(), renderer );

    sceneLoad.ApplyRuntimeReactions( launchOptions, timers, *m_overlayDiagnostics, sceneController, inputRouter, interaction,
                                     camera, attachedCamera, runtimeTools, replayRuntime );

    sceneLoad.ApplyPresentationOutputs( window, ui, validationHarness, launchOptions, &renderer.RenderDevice(),
                                        renderer.VsyncEnabled(), sceneController );

    presentationEdit.Refresh();

    if ( processedCapture || processedDefaults || processedScene )
    {
    }

    commitPointerPresentation();
    return CompleteInputPhase();
}
