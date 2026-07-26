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
  - InputFrame.cpp implements shared value and UI-command policy.
  - InputRouter.h owns retained input state.
  - Agentic/Reference/runtime-reference.md
  - Agentic/Reference/comment-style-guide.md
*/
#include "InputFrame.h"
#if defined( SKULLBONEZ_DEVELOPMENT_TOOLS )
#include "../DevelopmentTools/ImGuiEditorLayoutPolicy.h"
#endif
#include "../Diagnostics/RuntimeOverlayDiagnostics.h"
#include "../Automation/RuntimeValidationHarness.h"
#include "../Capture/RuntimeStressController.h"
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
#include "../Scene/SceneRuntimeCreate.h"
#include "../Interaction/OperatorCommandApplier.h"
#include "../Scene/SceneRuntimeGeneratedControls.h"
#include "../Scene/SceneRuntimeLoad.h"
#include "../Scene/SceneLoadTransaction.h"
#include "../Scene/SceneRuntimeStyle.h"
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
using namespace SkullbonezCore::Runtime::RunInternal;
using SkullbonezCore::Hardware::Input;
using SkullbonezCore::Hardware::InputState;
using SkullbonezCore::UI::InGameUITab;


// Concept: the input turn is an orchestration boundary, not a new domain owner.
// InputRouter owns sampling, edge memory, semantic order, and pointer policy;
// scene, replay, tools, diagnostics, UI, and rendering retain their own state
// and expose only synchronous operations for accepted input actions.
// Lifetime: both views are borrowed for this call and are never stored.
InputFrameExecutionResult
SkullbonezCore::Runtime::ProcessInputFrame( RuntimeFrameHostView& host,
                                            RuntimeFrameInteractionView& interactionOwners,
                                            RuntimeFrameSceneView& sceneOwners,
                                            RuntimeFramePresentationView& presentationOwners,
                                            ReplayRuntime& replayRuntime,
                                            UiInputCaptureIntent externalUiCapture,
                                            UI::OperatorEditorCommandQueues externalEditorCommands,
                                            bool legacyDevelopmentUiActive )
{
    InputFrameExecutionResult result;
    InputRouter& m_inputRouter = interactionOwners.inputRouter;
    SkullbonezCore::Core::EngineConfig& m_config = sceneOwners.config;
    RunLaunchOptions& m_launchOptions = sceneOwners.launchOptions;
    const RunStartupState& m_startup = sceneOwners.startup;
    RunTimerState& m_timers = sceneOwners.timers;
    CameraControlState& m_camera = interactionOwners.camera;
    RuntimeOverlayPresentationEdit presentationEdit = sceneOwners.overlays.EditPresentation();
    OverlayDebugState& m_debug = presentationEdit.State();
    ApplicationExitState& m_applicationExit = host.applicationExit;
    RenderDefaultsStore& m_renderDefaults = presentationOwners.renderDefaults;
    DiagnosticsRuntime& m_diagnosticsRuntime = host.diagnosticsRuntime;
    Assets::AssetSystem& m_assets = host.assets;
    Threading::WorkerPool& m_workerPool = host.workerPool;
    Window& m_window = host.window;
    RuntimeInteractionController& m_interaction = interactionOwners.interaction;
    AttachedCameraController& m_attachedCamera = interactionOwners.attachedCamera;
    SimulationSystem& m_simulation = sceneOwners.simulation;
    ReplayRuntime& m_replayRuntime = replayRuntime;
    SkullbonezCore::UI::InGameUI& m_UI = interactionOwners.operatorUi;
    RuntimeValidationHarness& m_validationHarness = presentationOwners.validationHarness;
    RuntimeTools& m_runtimeTools = interactionOwners.runtimeTools;
    RuntimeRenderBackendView& m_renderBackendView = presentationOwners.renderBackendView;
    RuntimeRenderer& m_renderer = presentationOwners.renderer;
    SceneController& m_sceneController = sceneOwners.sceneController;
    // Lifetime: these aliases expose InputRouter-owned frame state only for
    // this synchronous routing pass; Run retains neither value as member state.
    RuntimeInputContext& m_runtimeInput = m_inputRouter.RuntimeContext();
    InputActions& m_inputActions = m_inputRouter.Actions();
    const auto SceneState = [&]() -> SceneSessionState& { return m_sceneController.State(); };

    const auto NormalizeCameraModeForCurrentScene = [&]( RunCameraMode mode )
    {
        const SceneSessionState& sceneState = SceneState();

        const int sceneEntityCount = m_sceneController.Scene().SceneEntityCount();
        const uint32_t cameraModeEnabledMask = RuntimeCameraModeEnabledMask( sceneState.isSceneMode, sceneEntityCount );
        return NormalizeRuntimeCameraMode( mode, sceneState.isSceneMode, cameraModeEnabledMask );
    };

    const auto CameraModeEnabledMask = [&]()
    { return RuntimeCameraModeEnabledMask( SceneState().isSceneMode, m_sceneController.Scene().SceneEntityCount() ); };

    const auto EnterInteractiveSceneRun = [&]()
    {
        m_sceneController.EnterInteractiveRun();

        m_diagnosticsRuntime.Capture().DisableAutomationExit();
    };

    const auto RunUIStressActions = [&]()
    {
        // Invariant: the scene-authored stress harness mutates Legacy UI state.
        // Once ImGui owns the development surface, allowing that harness to
        // re-show Legacy would violate the exclusive focus/visibility contract.
        if ( !legacyDevelopmentUiActive )
        {
            return SkullbonezCore::Core::SbResult::Success();
        }

        presentationEdit.Commit();
        const SkullbonezCore::Core::SbResult result = SkullbonezCore::Runtime::RunUIStressActions(
            host,
            interactionOwners,
            sceneOwners,
            m_renderBackendView,
            m_renderer,
            m_replayRuntime,
            NormalizeCameraModeForCurrentScene( m_replayRuntime.BuildInputView().restoreCameraMode ) );

        presentationEdit.Refresh();
        return result;
    };

    const auto DrainCaptureRequests = [&]()
    {
        CaptureController& capture = m_diagnosticsRuntime.Capture();

        if ( capture.PendingScreenshotCount() == 0 )
        {
            return false;
        }

        Rendering::Dx12BackbufferCapture* backbufferCapture = m_renderBackendView.backbufferCapture;
        if ( !backbufferCapture )
        {
            // Lane F: startup binds this owner before input can submit capture work.
            SB_FATAL( "Runtime/CaptureController", "Queued screenshot requires an active DX12 capture owner" );
        }

        const CaptureRequestBatchResult batch = capture.DrainScreenshotRequests( *backbufferCapture );
        if ( !batch.status.ok )
        {
            std::fprintf( stderr, "%s: %s\n", batch.status.error.owner, batch.status.error.message );
            std::fflush( stderr );
        }

        for ( std::size_t index = 0; index < batch.savedCount; ++index )
        {
            m_replayRuntime.SubmitEvent( ReplayEventCommandOperations::BuildCommand(
                ReplayEventKind::OwnerAction,
                0,
                true,
                0,
                static_cast<int32_t>( ReplayOwnerEventCode::CaptureScreenshot ),
                0,
                0,
                0,
                0,
                batch.saved[index].path ) );
        }

        return true;
    };

    const auto DrainRenderDefaultRequests = [&]()
    {
        if ( m_renderDefaults.PendingCount() == 0 )
        {
            return false;
        }

        const RenderDefaultsSaveBatchResult batch = m_renderDefaults.DrainAtFrameCheckpoint(
            m_config.ordinaryRender,
            ActiveSceneCinematicConfig( SceneState(), m_config ) );

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

            m_replayRuntime.SubmitEvent( ReplayEventCommandOperations::BuildCommand( ReplayEventKind::OwnerAction,
                                                                                     0,
                                                                                     true,
                                                                                     0,
                                                                                     static_cast<int32_t>( code ),
                                                                                     0,
                                                                                     0,
                                                                                     0,
                                                                                     0,
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
        m_applicationExit.RequestOwnedFailure( deviceCaptureResult );
        PostQuitMessage( 1 );
        return result;
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
        if ( DevelopmentTools::MapImGuiGameViewportPoint( viewport,
                                                          static_cast<float>( deviceFrame.clientX ),
                                                          static_cast<float>( deviceFrame.clientY ),
                                                          mappedX,
                                                          mappedY ) )
        {
            // Invariant: every downstream pointer consumer sees the same mapped
            // source pixel; no tool resamples Win32 coordinates independently.
            deviceFrame.clientX = mappedX;
            deviceFrame.clientY = mappedY;
        }
    }
#endif
    const RuntimeInputKeyBindingView keyboardBindings = TakeInputKeyboardBindings();
    m_inputRouter.BeginFrame( deviceFrame, keyboardBindings, m_inputActions, externalUiCapture );
    UiInputHitSnapshot preUiPointer;
    preUiPointer.mouse = m_inputActions.mouse;
    preUiPointer.clientX = deviceFrame.clientX;
    preUiPointer.clientY = deviceFrame.clientY;
    preUiPointer.hasClientPosition = deviceFrame.hasClientPosition;
    preUiPointer.unhandledWheelDelta = deviceFrame.wheelDelta;
    m_inputRouter.PublishUiSnapshot( preUiPointer );
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
        if ( !m_inputRouter.ConsumePointerPresentationChange( presentation ) )
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
            m_applicationExit.RequestOwnedFailure( pointerResult );
            PostQuitMessage( 1 );
        }
    };

    if ( externalUiCapture.nativePointerStateTouched )
    {
        // The vendor backend may have changed shared HWND capture/cursor state
        // while translating a mouse message. Reassert the input owner's full
        // native policy even when its desired value is otherwise unchanged.
        m_inputRouter.DeferPointerPresentationCommit();
    }

    if ( externalUiCapture.mouse )
    {
        m_inputRouter.ReleaseNativeCapture();
        m_inputRouter.RequestCursorVisible( true );
        m_inputRouter.DeferPointerPresentationCommit();
    }

    if ( m_inputRouter.HandleUnfocusedFrame( interactionOwners, m_sceneController, m_replayRuntime, m_runtimeInput ) )
    {
        const SkullbonezCore::Core::SbResult stressResult = RunUIStressActions();
        if ( !stressResult.ok )
        {
            // Lane R: focus loss still routes stress churn through the same guarded
            // rebuild path. End the run before returning to the frame loop.
            ReportRuntimeInputFailure( stressResult );
            std::fflush( stderr );
            m_applicationExit.RequestOwnedFailure( stressResult );
            PostQuitMessage( 1 );
        }

        commitPointerPresentation();
        return result;
    }

    const bool UIBlocksKeyboardBeforeInput = m_UI.BlocksKeyboard() || externalUiCapture.keyboard ||
                                             externalUiCapture.text;

    m_inputRouter.ApplyPointerPresentation( EvaluateRuntimePointerPresentation( m_inputRouter,
                                                                                m_runtimeTools.Editor(),
                                                                                m_replayRuntime.BuildInputView() ) );

    InputController::BeginFrame( m_runtimeInput,
                                 BuildRuntimeInputModeState( m_camera.mode,
                                                             m_runtimeTools.Editor(),
                                                             m_interaction.Gesture(),
                                                             m_attachedCamera.State().activeFollow,
                                                             m_camera.director.grabbed ),
                                 true,
                                 UIBlocksKeyboardBeforeInput,
                                 m_UI.BlocksCameraMouse() || externalUiCapture.mouse );

    bool keyboardToggleEditorMode = false;
    RunInternal::EditorKeyboardShortcutResult keyboardEditorToolShortcut;
    auto completeEditorPlacementModeTransition = [&]( RuntimeInputActionSource source,
                                                     const RunInternal::EditorPlacementModeChangeResult& placementMode )
    {
        m_inputRouter.SetWorldInteractionOwner(
            placementMode.worldOwner,
            InteractionExitReason::EnterEdit,
            interactionOwners,
            m_sceneController,
            m_replayRuntime,
            NormalizeCameraModeForCurrentScene( m_replayRuntime.BuildInputView().restoreCameraMode ) );

        if ( m_inputRouter.ReleasePointerToUi(
                 EvaluateRuntimePointerPresentation( m_inputRouter,
                                                     m_runtimeTools.Editor(),
                                                     m_replayRuntime.BuildInputView() ) ) )
        {
            InputController::ResetMouseLook( m_camera );
        }

        m_inputRouter.ApplyPointerPresentation(
            EvaluateRuntimePointerPresentation( m_inputRouter,
                                                m_runtimeTools.Editor(),
                                                m_replayRuntime.BuildInputView() ) );

        m_inputRouter.RecordModeAction( interactionOwners,
                                        m_runtimeInput,
                                        RuntimeInputAction::ToggleEditorTool,
                                        source );
    };

    auto applyEditorPlacementModeToggle = [&]( RuntimeInputActionSource source )
    {
        EnterInteractiveSceneRun();

        const RunInternal::EditorPlacementModeChangeResult placementMode = RunInternal::ToggleEditorPlacementMode(
            { m_runtimeTools.Editor(), m_sceneController.Scene(), m_interaction } );

        completeEditorPlacementModeTransition( source, placementMode );
    };

    const bool flyCamera = RunCameraModeUsesFlyControls( m_camera.mode,
                                                         m_attachedCamera.State().activeFollow,
                                                         m_camera.director.grabbed );

    const KeyboardContextFacts keyboardContextFacts { !UIBlocksKeyboardBeforeInput,
                                                      SceneState().isSceneMode,
                                                      flyCamera,
                                                      RunCameraModeUsesLauncher( m_camera.mode ),
                                                      RunCameraModeIsAttached( m_camera.mode ),
                                                      m_camera.mode == RunCameraMode::Director,
                                                      m_camera.mode == RunCameraMode::Director || flyCamera,
                                                      m_runtimeTools.Editor().editorModeEnabled,
                                                      !m_replayRuntime.BuildInputView().restoreConsumedThisFrame,
                                                      false };

    m_inputRouter.RoutePhase( keyboardBindings,
                              InputActionPhase::PreUi,
                              BuildKeyboardContextMask( keyboardContextFacts ),
                              m_inputActions );

    if ( m_inputActions.Overflowed() )
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
        const bool loaded = sceneLoad
                                .Load(
                                    m_sceneController,
                                    request,
                                    SceneLoadPolicyInputs { m_config,
                                                            m_launchOptions,
                                                            m_renderDefaults.CinematicBaseline(),
                                                            m_startup,
                                                            m_assets,
                                                            m_workerPool,
                                                            m_diagnosticsRuntime,
                                                            m_renderBackendView.RendererName(),
                                                            m_timers.simulationTimer.GetTotalTime() },
                                    m_camera,
                                    CaptureSceneLoadNavigationState( interactionOwners.operatorUi.SceneNavigation() ),
                                    m_debug,
                                    m_renderBackendView.renderFrame,
                                    m_renderBackendView.renderResources,
                                    m_renderer )
                                .ok;

        sceneLoad.ApplyRuntimeReactions( m_launchOptions,
                                         m_timers,
                                         sceneOwners.overlays,
                                         m_sceneController,
                                         m_inputRouter,
                                         m_interaction,
                                         m_camera,
                                         m_attachedCamera,
                                         m_runtimeTools,
                                         m_replayRuntime );

        sceneLoad.ApplyPresentationOutputs( m_window,
                                            interactionOwners.operatorUi,
                                            m_validationHarness,
                                            m_launchOptions,
                                            m_renderBackendView.renderDevice,
                                            m_renderer.VsyncEnabled(),
                                            m_sceneController );

        presentationEdit.Refresh();
        return loaded;
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
            keyboardEditorToolShortcut = RunInternal::HandleEditorKeyboardShortcut(
                event.action,
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
        case RuntimeInputAction::UndoEditor:
            if ( m_runtimeTools.Editor().editorModeEnabled && deviceFrame.keys.IsDown( VK_CONTROL ) )
            {
                if ( deviceFrame.keys.IsDown( VK_SHIFT ) )
                {
                    (void)m_runtimeTools.RedoEditorCommand( m_sceneController.Scene(), m_sceneController.State() );
                }
                else
                {
                    (void)m_runtimeTools.UndoEditorCommand( m_sceneController.Scene(), m_sceneController.State() );
                }
            }

            break;
        case RuntimeInputAction::RedoEditor:
            if ( m_runtimeTools.Editor().editorModeEnabled && deviceFrame.keys.IsDown( VK_CONTROL ) )
            {
                (void)m_runtimeTools.RedoEditorCommand( m_sceneController.Scene(), m_sceneController.State() );
            }

            break;
        case RuntimeInputAction::DeleteEditorSelection:
            if ( m_runtimeTools.Editor().editorModeEnabled )
            {
                (void)m_runtimeTools.DeleteEditorSelection( m_sceneController.Scene(), m_sceneController.State() );
            }

            break;
        case RuntimeInputAction::CycleCameraMode:
            m_inputRouter.CycleCameraMode( interactionOwners, m_sceneController, m_replayRuntime, m_runtimeInput );
            break;
        case RuntimeInputAction::ToggleFlyCamera:
        {
            const RunCameraMode passiveMode = SceneState().isSceneMode ? RunCameraMode::Scene : RunCameraMode::Demo;
            m_inputRouter.ApplyCameraMode(
                m_camera.mode == RunCameraMode::Inspect ? passiveMode : RunCameraMode::Inspect,
                event.source,
                interactionOwners,
                m_sceneController,
                m_replayRuntime,
                m_runtimeInput );

            break;
        }
        case RuntimeInputAction::ToggleLauncher:
            if ( m_camera.mode == RunCameraMode::Launcher )
            {
                m_inputRouter.ApplyCameraMode( m_camera.modeBeforeLauncher,
                                               event.source,
                                               interactionOwners,
                                               m_sceneController,
                                               m_replayRuntime,
                                               m_runtimeInput );
            }
            else
            {
                m_camera.modeBeforeLauncher = m_camera.mode == RunCameraMode::Manipulator ? RunCameraMode::Inspect
                                                                                          : m_camera.mode;

                m_inputRouter.ApplyCameraMode( RunCameraMode::Launcher,
                                               event.source,
                                               interactionOwners,
                                               m_sceneController,
                                               m_replayRuntime,
                                               m_runtimeInput );
            }

            break;
        case RuntimeInputAction::CycleLauncherFireMode:
            if ( RunCameraModeUsesLauncher( m_camera.mode ) )
            {
                m_runtimeTools.RayCastTest().fireMode = m_runtimeTools.RayCastTest().fireMode ==
                                                                RunLauncherFireMode::Laser
                                                            ? RunLauncherFireMode::Projectile
                                                            : RunLauncherFireMode::Laser;
            }

            break;
        case RuntimeInputAction::CycleAttachedCameraSubmode:
            if ( RunCameraModeIsAttached( m_camera.mode ) && m_attachedCamera.CycleMode( m_sceneController.Scene() ) )
            {
                m_inputRouter.RecordModeAction( interactionOwners,
                                                m_runtimeInput,
                                                RuntimeInputAction::CycleAttachedCameraSubmode,
                                                RuntimeInputActionSource::Keyboard );
            }

            break;
        case RuntimeInputAction::ToggleAttachedCameraPin:
            if ( RunCameraModeIsAttached( m_camera.mode ) )
            {
                const bool activeFollow = m_attachedCamera.TogglePin( m_sceneController.Scene() );
                if ( !activeFollow )
                {
                    if ( m_inputRouter.ReleasePointerToUi(
                             EvaluateRuntimePointerPresentation( m_inputRouter,
                                                                 m_runtimeTools.Editor(),
                                                                 m_replayRuntime.BuildInputView() ) ) )
                    {
                        InputController::ResetMouseLook( m_camera );
                    }
                }

                m_inputRouter.ApplyPointerPresentation(
                    EvaluateRuntimePointerPresentation( m_inputRouter,
                                                        m_runtimeTools.Editor(),
                                                        m_replayRuntime.BuildInputView() ) );

                m_inputRouter.RecordModeAction( interactionOwners,
                                                m_runtimeInput,
                                                RuntimeInputAction::ToggleAttachedCameraPin,
                                                RuntimeInputActionSource::Keyboard );
            }

            break;
        case RuntimeInputAction::WriteLauncherReproSnapshot:
#ifdef _DEBUG
            if ( RunCameraModeUsesLauncher( m_camera.mode ) &&
                 !m_replayRuntime.BuildInputView().restoreConsumedThisFrame )
            {
                const double simulationSeconds = m_timers.simulationTimer.GetTimeSinceLastStart();
                m_runtimeTools.WriteLauncherReproSnapshotWithStatusMessage(
                    { m_sceneController.Scene(),
                      SceneState(),
                      m_sceneController.CurrentPath(),
                      m_launchOptions,
                      m_sceneController.Scene().Physics().IsSleepEnabled(),
                      m_renderer.VsyncEnabled(),
                      m_renderer.PipelineSyncEnabled(),
                      m_config.bodySimulation.contactEpsilon,
                      m_config.physicsMaterial.frictionCoeff,
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
                if ( DemoDirectorPlayback::EndGrab( m_camera, m_sceneController.Scene().Cameras() ) )
                {
                    ExitFlyModeCamera( m_inputRouter,
                                       m_camera,
                                       m_sceneController.Scene().Cameras(),
                                       *m_sceneController.Scene().Terrain().Get(),
                                       SceneState().isSceneMode );

                    m_inputRouter.ApplyPointerPresentation(
                        EvaluateRuntimePointerPresentation( m_inputRouter,
                                                            m_runtimeTools.Editor(),
                                                            m_replayRuntime.BuildInputView() ) );

                    m_inputRouter.RecordModeAction( interactionOwners, m_runtimeInput, event.action, event.source );
                }
            }
            else if ( DemoDirectorPlayback::BeginGrab( m_camera, m_sceneController.Scene().Cameras() ) )
            {
                EnterFlyModeCamera( m_inputRouter,
                                    m_camera,
                                    m_sceneController.Scene().Cameras(),
                                    SceneState().isSceneMode,
                                    m_runtimeTools.Editor(),
                                    m_replayRuntime.BuildInputView() );

                m_inputRouter.ApplyPointerPresentation(
                    EvaluateRuntimePointerPresentation( m_inputRouter,
                                                        m_runtimeTools.Editor(),
                                                        m_replayRuntime.BuildInputView() ) );

                m_inputRouter.RecordModeAction( interactionOwners, m_runtimeInput, event.action, event.source );
            }

            break;
        case RuntimeInputAction::SetDirectorPhasePose:
            if ( ( m_camera.mode == RunCameraMode::Director ||
                   RunCameraModeUsesFlyControls( m_camera.mode,
                                                 m_attachedCamera.State().activeFollow,
                                                 m_camera.director.grabbed ) ) &&
                 DemoDirectorPlayback::SetCurrentPhasePose( m_camera, m_sceneController.Scene().Cameras() ) )
            {
                m_inputRouter.RecordModeAction( interactionOwners, m_runtimeInput, event.action, event.source );
            }

            break;
        case RuntimeInputAction::StepDirectorPhase:
            if ( ( m_camera.mode == RunCameraMode::Director ||
                   RunCameraModeUsesFlyControls( m_camera.mode,
                                                 m_attachedCamera.State().activeFollow,
                                                 m_camera.director.grabbed ) ) &&
                 DemoDirectorPlayback::SelectNextPhaseForAuthoring( m_camera, m_sceneController.Scene().Cameras() ) )
            {
                m_inputRouter.RecordModeAction( interactionOwners, m_runtimeInput, event.action, event.source );
            }

            break;
        case RuntimeInputAction::SaveDirectorShotList:
            if ( ( m_camera.mode == RunCameraMode::Director ||
                   RunCameraModeUsesFlyControls( m_camera.mode,
                                                 m_attachedCamera.State().activeFollow,
                                                 m_camera.director.grabbed ) ) &&
                 DemoDirectorPlayback::SaveShotList( m_camera ) )
            {
                m_inputRouter.RecordModeAction( interactionOwners, m_runtimeInput, event.action, event.source );
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
            HandleDiagnosticsKeyboardShortcut(
                DiagnosticsKeyboardShortcutContext { m_debug,
                                                     m_camera.trackBallRow.value,
                                                     m_sceneController.Scene().SceneEntityCount(),
                                                     m_renderBackendView.renderDiagnostics,
                                                     SceneState().isSceneMode,
                                                     m_timers.simulationTimer.GetTimeSinceLastStart() },
                event.action,
                true );

            break;
        case RuntimeInputAction::ReloadShadersFromSource:
        {
            if ( !m_renderBackendView.shaderDevelopment )
            {
                fprintf( stderr, "Shader hot reload unavailable: active backend has no development capability.\n" );
                break;
            }

            // Allocation policy: F9 is an explicit cold developer utility. The
            // bake, manifest parse, reflection maps, and process launch belong
            // to BackendInit rather than steady input/render accounting.
            SkullbonezCore::Core::Allocation::RuntimeAllocationScope allocationScope( SkullbonezCore::Core::Allocation::RuntimeAllocationPhase::BackendInit );
            const SkullbonezCore::Core::SbResult reloadResult = m_renderBackendView.shaderDevelopment
                                                                    ->ReloadShadersFromSource();

            if ( !reloadResult.ok )
            {
                fprintf( stderr,
                         "Shader hot reload failed: owner=%s reason=%s\n",
                         reloadResult.error.owner,
                         reloadResult.error.message );

                SkullbonezCore::Core::Log().WriteEventf( "shader_hot_reload_failed owner=%s reason=%s",
                                                         reloadResult.error.owner,
                                                         reloadResult.error.message );
            }

            break;
        }
        case RuntimeInputAction::CycleReplayPathColorMode:
            // Concept: comma changes a presentation value only. Existing
            // trajectory samples remain immutable and are recolored next draw.
            m_replayRuntime.CyclePathColorMode();
            break;
        case RuntimeInputAction::ToggleReplayGuideArcs:
            // Why: guide rings remain a Legacy-only teaching aid while ImGui
            // owns its separate development-tool presentation contract.
            if ( legacyDevelopmentUiActive )
            {
                m_replayRuntime.ToggleGuideArcs();
            }

            break;
        case RuntimeInputAction::ToggleReplayTripPlanner:
            if ( legacyDevelopmentUiActive )
            {
                (void)m_replayRuntime.QueueTripPlannerCommand( { ReplayTripPlannerCommandKind::TogglePanel } );
            }

            break;
        case RuntimeInputAction::ToggleReplayPorkchopPanel:
            if ( legacyDevelopmentUiActive )
            {
                m_replayRuntime.TogglePorkchopPanel();
            }

            break;
        case RuntimeInputAction::ToggleCrossScenePause:
            // P locks scene automation without turning the run interactive;
            // SceneController preserves the policy across load transactions.
            m_sceneController.ToggleCrossScenePause();
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
                result.requestDevelopmentUiSurfaceSwap = true;
            }

            const DiagnosticsUIKeyboardShortcutResult shortcutResult = HandleDiagnosticsUIKeyboardShortcut(
                DiagnosticsUIKeyboardShortcutContext { m_UI,
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
                        EvaluateRuntimePointerPresentation( m_inputRouter,
                                                            m_runtimeTools.Editor(),
                                                            m_replayRuntime.BuildInputView() ) );

                    if ( m_inputRouter.ReleasePointerToUi(
                             EvaluateRuntimePointerPresentation( m_inputRouter,
                                                                 m_runtimeTools.Editor(),
                                                                 m_replayRuntime.BuildInputView() ) ) )
                    {
                        InputController::ResetMouseLook( m_camera );
                    }
                }

                m_inputRouter.RecordModeAction( interactionOwners, m_runtimeInput, event.action, event.source );
            }

            break;
        }
        case RuntimeInputAction::NavigateScenePrevious:
        case RuntimeInputAction::NavigateSceneNext:
        {
            const int direction = event.action == RuntimeInputAction::NavigateScenePrevious ? -1 : 1;
            EnterInteractiveSceneRun();
            const int currentSceneBrowserIndex = CurrentSceneBrowserIndex( m_sceneController,
                                                                           m_UI.SceneNavigation().browser );

            const bool isCinematicTabActive = m_UI.GetActiveTab() == InGameUITab::Cinematic;
            UI::SceneNavigationModel& sceneNavigation = m_UI.SceneNavigation();
            const int cinematicIndex = AdjacentCinematicModeBrowserIndex( sceneNavigation,
                                                                          direction,
                                                                          currentSceneBrowserIndex,
                                                                          isCinematicTabActive );

            const bool appliedCinematic = cinematicIndex >= 0 &&
                                          ApplyCinematicModeFromBrowserIndex(
                                              SceneRuntimeStyleContext {
                                                  m_launchOptions,
                                                  SceneState(),
                                                  m_UI.SceneNavigation().browser,
                                                  m_sceneController.Scene(),
                                                  m_assets,
                                                  ActiveSceneCinematicConfig( SceneState(), m_config ),
                                                  m_renderDefaults.CinematicBaseline() },
                                              cinematicIndex );

            if ( !appliedCinematic )
            {
                executeSceneLoadRequest( LoadAdjacentScene( sceneNavigation,
                                                            direction,
                                                            currentSceneBrowserIndex,
                                                            m_sceneController.Runtime() ) );
            }

            break;
        }
        default:
            break;
        }
    }

    if ( m_runtimeTools.Editor().editorModeEnabled )
    {
        (void)m_replayRuntime.ApplyKeyboardVelocityEdit( { keyboardEditorToolShortcut.altDown,
                                                           false,
                                                           m_interaction.Owner(),
                                                           m_timers.simulationTimer.GetTotalTime() } );

        if ( keyboardEditorToolShortcut.togglePlacementMode )
        {
            applyEditorPlacementModeToggle( RuntimeInputActionSource::Keyboard );
        }
    }
    else
    {
        const ReplayKeyboardVelocityEditResult velocityEditResult = m_replayRuntime.ApplyKeyboardVelocityEdit(
            { keyboardEditorToolShortcut.altDown,
              true,
              m_interaction.Owner(),
              m_timers.simulationTimer.GetTotalTime() } );

        if ( velocityEditResult.cancelToolDrag )
        {
            ReplayInteractionOperations::CancelToolDragState( m_interaction, m_inputRouter );
        }

        if ( velocityEditResult.enterInteractive )
        {
            EnterInteractiveSceneRun();
        }

        if ( velocityEditResult.cameraAction == ReplayKeyboardVelocityEditCameraAction::EnterInspection )
        {
            m_replayRuntime.EnterInspectionCamera( &m_sceneController.Scene().Cameras(),
                                                   m_camera,
                                                   NormalizeCameraModeForCurrentScene( m_camera.mode ),
                                                   m_interaction,
                                                   m_inputRouter,
                                                   m_runtimeTools.MousePickup() );
        }
        else if ( velocityEditResult.cameraAction == ReplayKeyboardVelocityEditCameraAction::ExitInspection )
        {
            m_replayRuntime.ExitInspectionCamera(
                &m_sceneController.Scene().Cameras(),
                m_sceneController.Scene().Terrain().Get(),
                m_camera,
                NormalizeCameraModeForCurrentScene( m_replayRuntime.BuildInputView().restoreCameraMode ),
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
                interactionOwners,
                m_sceneController,
                m_replayRuntime,
                NormalizeCameraModeForCurrentScene( m_replayRuntime.BuildInputView().restoreCameraMode ) );
        }
    }

    ReplayPathPickInput replayPointerRay;
    replayPointerRay.hasWorldRay = m_inputRouter.TryBuildWorldRay( m_sceneController.Scene().Cameras(),
                                                                   m_window,
                                                                   replayPointerRay.rayOrigin,
                                                                   replayPointerRay.rayDirection );

    const RuntimeInputFrameFacts uiSamplingFacts {
        NormalizeCameraModeForCurrentScene( m_camera.mode ),
        NormalizeCameraModeForCurrentScene( m_replayRuntime.BuildInputView().restoreCameraMode ),
        CameraModeEnabledMask(),
        UIBlocksKeyboardBeforeInput,
        SkullbonezCore::Core::ActiveSceneObjectCapacity( m_config ),
        externalUiCapture,
        externalEditorCommands,
        legacyDevelopmentUiActive };

    RuntimeUIFrameResult uiFrameResult = BeginRuntimeUIFrame( m_window,
                                                              interactionOwners,
                                                              m_timers,
                                                              m_sceneController,
                                                              m_replayRuntime,
                                                              replayPointerRay,
                                                              uiSamplingFacts );

    if ( uiFrameResult.frameActive )
    {
        if ( uiFrameResult.enterInteractiveScene )
        {
            EnterInteractiveSceneRun();
            uiFrameResult.enterInteractiveScene = false;
        }

        presentationEdit.Commit();
        const bool quitRequested = m_inputRouter.DispatchAfterUiDismiss( m_inputActions,
                                                                         { uiFrameResult.commands.ui.userInteracted,
                                                                           m_timers.simulationTimer.GetTotalTime(),
                                                                           legacyDevelopmentUiActive },
                                                                         m_diagnosticsRuntime,
                                                                         interactionOwners,
                                                                         m_sceneController,
                                                                         sceneOwners.overlays,
                                                                         m_replayRuntime.BuildInputView() );

        presentationEdit.Refresh();
        if ( quitRequested )
        {
            PostQuitMessage( 0 );
        }
    }

    const RuntimeInputFrameFacts commandFacts {
        NormalizeCameraModeForCurrentScene( m_camera.mode ),
        NormalizeCameraModeForCurrentScene( m_replayRuntime.BuildInputView().restoreCameraMode ),
        CameraModeEnabledMask(),
        uiFrameResult.suppressWorldActionThisFrame,
        SkullbonezCore::Core::ActiveSceneObjectCapacity( m_config ),
        externalUiCapture,
        externalEditorCommands,
        legacyDevelopmentUiActive };

    presentationEdit.Commit();
    uiFrameResult = ApplyRuntimeUIFrameCommands( uiFrameResult,
                                                 keyboardToggleEditorMode,
                                                 host,
                                                 interactionOwners,
                                                 sceneOwners,
                                                 presentationOwners,
                                                 m_replayRuntime,
                                                 commandFacts );

    presentationEdit.Refresh();
    if ( uiFrameResult.status.ok && uiFrameResult.frameActive )
    {
        uiFrameResult.status = RunUIStressActions();
    }

    uiFrameResult = FinishRuntimeUIFramePointer( uiFrameResult,
                                                 interactionOwners,
                                                 m_sceneController,
                                                 m_replayRuntime,
                                                 NormalizeCameraModeForCurrentScene( m_camera.mode ) );

    if ( uiFrameResult.enterInteractiveScene )
    {
        EnterInteractiveSceneRun();
    }

    const ReplayLiveRestoreRequest& restoreRequest = uiFrameResult.replayWorkspace.restoreRequest;
    if ( restoreRequest.kind != ReplayLiveRestoreKind::None )
    {
        const ReplaySceneTimelineResetInput timelineReset = DescribeReplaySceneTimeline(
            m_sceneController,
            m_UI.SceneNavigation().overrides,
            SceneState(),
            SkullbonezCore::Core::ActiveSceneObjectCapacity( m_config ),
            static_cast<uint32_t>( m_launchOptions.generatedObjectTypeOverride ) );

        ReplaySolverSampleRestoreContext sampleOwners { m_sceneController.Scene(),
                                                        SceneState(),
                                                        m_renderer,
                                                        m_debug,
                                                        m_runtimeTools };

        ReplaySceneTimelineResetOwners timelineOwners {
            m_inputRouter,
            m_interaction,
            &m_sceneController.Scene().Cameras(),
            m_sceneController.Scene().Terrain().Get(),
            m_camera,
            NormalizeCameraModeForCurrentScene( m_replayRuntime.BuildInputView().restoreCameraMode ),
            m_attachedCamera.State().activeFollow,
            m_camera.director.grabbed };

        const ReplayRestoreTransaction transaction { sampleOwners,
                                                     m_diagnosticsRuntime,
                                                     timelineReset,
                                                     timelineOwners };

        const ReplayArtifactTopologyOwners topologyOwners {
            m_simulation,
            m_config,
            m_assets,
            m_workerPool,
            m_UI.SceneNavigation().overrides,
            m_launchOptions.generatedObjectTypeOverride,
            SkullbonezCore::Core::ActiveSceneObjectCapacity( m_config ) };

        const ReplayLiveRestoreOutcome restoreOutcome = m_replayRuntime.ApplyLiveRestoreRequest( transaction,
                                                                                                 topologyOwners,
                                                                                                 restoreRequest );

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
        return result;
    }

    const bool suppressWorldActionThisFrame = uiFrameResult.suppressWorldActionThisFrame;

    // Editor, replay, and launcher actions share world clicks. UI interaction
    // and capture suppress them so panel controls never mutate the scene.
    const RuntimeMouseEdges& mouseEdges = m_inputRouter.UiSnapshot().mouse;
    const DeviceInputFrame& routedDeviceFrame = m_inputRouter.DeviceFrame();
    const UiInputHitSnapshot& routedUiSnapshot = m_inputRouter.UiSnapshot();
    const ReplayInputView replayInput = m_replayRuntime.BuildInputView();
    RuntimeInteractionFrameInput frameInput;
    frameInput.scenePhysicsEnabled = SceneState().isScenePhysics;
    frameInput.stepHeld = routedDeviceFrame.keys.IsDown( VK_SPACE ) || uiFrameResult.requestSceneStep;
    frameInput.replayScrubbedHistoricalSample = replayInput.scrubPaused;
    frameInput.replayLiveHeldAtCurrentFrame = replayInput.liveAdvanceHeld;
    frameInput.crossScenePauseLocked = m_sceneController.CrossScenePauseLocked();
    frameInput.rightMouseLookHeld = mouseEdges.rightDown;
    frameInput.editorViewportLookActive = m_runtimeTools.Editor().viewportLookActive;
    frameInput.replayInspectionLookActive = replayInput.inspectionActive && routedDeviceFrame.rightDown &&
                                            !routedUiSnapshot.wantsNativeCursor && !routedUiSnapshot.blocksCameraMouse;

    frameInput.forcePhysicsRunning = false;
    frameInput.sceneTimeScale = SceneState().timeScale;
    const RuntimeInputSnapshot& inputSnapshot = m_inputRouter.PublishRuntimeSnapshot( frameInput,
                                                                                      suppressWorldActionThisFrame );

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
    pointerInput.replayInspectionActive = replayInput.inspectionActive;
    pointerInput.clientX = pointerDevice.clientX;
    pointerInput.clientY = pointerDevice.clientY;
    pointerInput.activeModelCapacity = SkullbonezCore::Core::ActiveSceneObjectCapacity( m_config );
    pointerInput.cameraMode = m_camera.mode;
    pointerInput.hasWorldRay = m_inputRouter.TryBuildWorldRay( m_sceneController.Scene().Cameras(),
                                                               m_window,
                                                               pointerInput.rayOrigin,
                                                               pointerInput.rayDirection );

    pointerInput.hasClampedWorldRay = m_inputRouter.TryBuildWorldRay( m_sceneController.Scene().Cameras(),
                                                                      m_window,
                                                                      pointerInput.clampedRayOrigin,
                                                                      pointerInput.clampedRayDirection,
                                                                      true );

    pointerInput.cameraEye = m_sceneController.Scene().Cameras().GetCameraTranslation();
    pointerInput.cameraView = m_sceneController.Scene().Cameras().GetCameraView();
    const RuntimePointerRouteResult pointerResult = m_inputRouter.RouteRuntimePointer(
        pointerInput,
        m_assets,
        interactionOwners,
        m_sceneController,
        m_replayRuntime,
        NormalizeCameraModeForCurrentScene( replayInput.restoreCameraMode ) );

    if ( pointerResult.enteredInteractiveScene )
    {
        EnterInteractiveSceneRun();
    }

    for ( std::size_t actionIndex = 0; actionIndex < pointerResult.modeActionCount; ++actionIndex )
    {
        m_inputRouter.RecordModeAction( interactionOwners,
                                        m_runtimeInput,
                                        pointerResult.modeActions[actionIndex],
                                        RuntimeInputActionSource::Mouse );
    }

    if ( m_UI.BlocksKeyboard() || externalUiCapture.keyboard || externalUiCapture.text )
    {
        m_interaction.CancelCameraLookGesture();
        InputController::ResetMouseLook( m_camera );
        m_camera.input.Set( InputState::Up, false );
        m_camera.input.Set( InputState::Down, false );
        m_camera.input.Set( InputState::Left, false );
        m_camera.input.Set( InputState::Right, false );
        m_inputRouter.ApplyPointerPresentation(
            EvaluateRuntimePointerPresentation( m_inputRouter,
                                                m_runtimeTools.Editor(),
                                                m_replayRuntime.BuildInputView() ) );
    }
    else
    {
        m_inputRouter.DispatchCaptureActions( m_inputActions,
                                              m_diagnosticsRuntime,
                                              interactionOwners,
                                              m_sceneController,
                                              m_replayRuntime.BuildInputView() );

        const RuntimeInteractionFramePolicy inputPolicy = m_interaction.BuildFramePolicy( inputSnapshot.frameInput );
        const bool mouseOwnsCursor = EvaluateRuntimePointerPresentation( m_inputRouter,
                                                                         m_runtimeTools.Editor(),
                                                                         m_replayRuntime.BuildInputView() )
                                         .mouseLookOwnsCursor;

        m_interaction.SyncCameraLookGesture( inputSnapshot, inputPolicy, mouseOwnsCursor );
        const bool cameraMouseLookActive = inputPolicy.cameraMouseLookActive && mouseOwnsCursor &&
                                           inputSnapshot.appFocused;

        if ( cameraMouseLookActive )
        {
            m_inputRouter.RequestNativeCapture();
            m_inputRouter.RequestCursorVisible( false );
        }

        const RuntimeCameraInputFrameResult cameraInputResult = InputController::ApplyCameraInputFrame(
            m_camera,
            RuntimeCameraInputFrameContext { inputSnapshot.appFocused,
                                             cameraMouseLookActive,
                                             mouseOwnsCursor,
                                             inputPolicy.cameraKeyboardControlsActive,
                                             &m_inputRouter.DeviceFrame() } );

        if ( cameraInputResult.applyCursorOwnership )
        {
            m_inputRouter.ApplyPointerPresentation(
                EvaluateRuntimePointerPresentation( m_inputRouter,
                                                    m_runtimeTools.Editor(),
                                                    m_replayRuntime.BuildInputView() ) );
        }
    }

    // Invariant: UI capture gates world/camera controls, not deferred owner
    // commands. Persistence samples final UI-mutated values before a same-frame
    // scene reset can replace config, even while an ImGui text field owns keys.
    const bool processedDefaults = DrainRenderDefaultRequests();
    const bool processedCapture = DrainCaptureRequests();
    presentationEdit.Commit();
    const SceneLoadPolicyInputs sceneLoadPolicy { m_config,
                                                  m_launchOptions,
                                                  m_renderDefaults.CinematicBaseline(),
                                                  m_startup,
                                                  m_assets,
                                                  m_workerPool,
                                                  m_diagnosticsRuntime,
                                                  m_renderBackendView.RendererName(),
                                                  m_timers.simulationTimer.GetTotalTime() };

    const SceneLoadNavigationState sceneLoadNavigation = CaptureSceneLoadNavigationState(
        interactionOwners.operatorUi.SceneNavigation() );

    SceneLoadTransaction sceneLoad;
    const bool processedScene = m_sceneController.ExecutePending( sceneLoad,
                                                                  sceneLoadPolicy,
                                                                  m_camera,
                                                                  sceneLoadNavigation,
                                                                  m_debug,
                                                                  m_renderBackendView.renderFrame,
                                                                  m_renderBackendView.renderResources,
                                                                  m_renderer );

    sceneLoad.ApplyRuntimeReactions( m_launchOptions,
                                     m_timers,
                                     sceneOwners.overlays,
                                     m_sceneController,
                                     m_inputRouter,
                                     m_interaction,
                                     m_camera,
                                     m_attachedCamera,
                                     m_runtimeTools,
                                     m_replayRuntime );

    sceneLoad.ApplyPresentationOutputs( m_window,
                                        interactionOwners.operatorUi,
                                        m_validationHarness,
                                        m_launchOptions,
                                        m_renderBackendView.renderDevice,
                                        m_renderer.VsyncEnabled(),
                                        m_sceneController );

    presentationEdit.Refresh();
    if ( processedCapture || processedDefaults || processedScene )
    {
    }

    commitPointerPresentation();
    return result;
}
