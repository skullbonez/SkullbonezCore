/*
File: InputFrameExecution.cpp
Purpose:
  Executes the stateless once-per-frame input turn across concrete owners.

Mental model:
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

Invariants:
  - Device input is captured once; later phases consume router-owned values.
  - UI hit testing completes before pointer ownership is finalized.
  - Every concrete owner is borrowed synchronously for this call only.

Related:
  - InputFrame.cpp implements shared value and UI-command policy.
  - InputRouter.h owns retained input state.
  - Agentic/Reference/runtime-reference.md
  - Agentic/Reference/comment-style-guide.md
*/
#include "InputFrame.h"
#include "RuntimeStressController.h"
#include "AttachedCameraController.h"
#include "ApplicationExitState.h"
#include "Diagnostics/DiagnosticsRuntime.h"
#include "Editor/EditorTools.h"
#include "InputController.Bindings.h"
#include "InputController.h"
#include "Replay/ReplayOverlayLayout.h"
#include "Replay/ReplayRestoreService.h"
#include "Replay/ReplayRuntimeOwnerViews.h"
#include "RunDemoDirector.h"
#include "GraphicsStressController.h"
#include "RenderDefaultsStore.h"
#include "Render/RuntimeRenderer.h"
#include "RunDebugState.h"
#include "RunLaunchOptions.h"
#include "RunStartupState.h"
#include "RunTimerState.h"
#include "RuntimeViewModel.h"
#include "Tools/RuntimeTools.h"
#include "RuntimeInteractionCommands.h"
#include "Scene/SceneRuntimeCreate.h"
#include "RuntimeTuning.h"
#include "Scene/SceneRuntimeGeneratedControls.h"
#include "Scene/SceneRuntimeLoad.h"
#include "Scene/SceneRuntimeStyle.h"
#include "Scene/SceneController.h"
#include "Audio/ContactAudioService.h"
#include "../Core/Log.h"
#include "../Physics/ColliderStore.h"
#include "../Physics/PhysicsBodyStore.h"
#include "../Physics/SimulationSystem.h"
#include "../UI/UI.h"
#include "../Rendering/IRenderDiagnostics.h"
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
using SkullbonezCore::Hardware::Input;
using SkullbonezCore::Hardware::InputState;
using SkullbonezCore::UI::InGameUITab;


// Concept: the input turn is an orchestration boundary, not a new domain owner.
// InputRouter owns sampling, edge memory, semantic order, and pointer policy;
// scene, replay, tools, diagnostics, UI, and rendering retain their own state
// and expose only synchronous operations for accepted input actions.
// Lifetime: every reference below is borrowed for this call and is never stored.
void SkullbonezCore::Basics::ProcessInputFrame( InputRouter& inputRouter,
                                                EngineConfig& m_config,
                                                RunLaunchOptions& m_launchOptions,
                                                ApplicationExitState& m_applicationExit,
                                                RenderDefaultsStore& m_renderDefaults,
                                                const RunStartupState& m_startup,
                                                DiagnosticsRuntime& m_diagnosticsRuntime,
                                                RunTimerState& m_timers,
                                                Assets::AssetSystem& m_assets,
                                                Threading::WorkerPool& m_workerPool,
                                                Window& m_window,
                                                RuntimeInteractionController& m_interaction,
                                                RunCameraState& m_camera,
                                                AttachedCameraController& m_attachedCamera,
                                                SimulationSystem& m_simulation,
                                                ReplayRuntime& m_replayRuntime,
                                                SkullbonezCore::Runtime::Audio::ContactAudioService& m_contactAudio,
                                                SkullbonezCore::UI::InGameUI& m_UI,
                                                RunDebugState& m_debug,
                                                GraphicsStressController& m_graphicsStress,
                                                RuntimeTools& m_runtimeTools,
                                                Physics::PhysicsDebugVisualizer& m_physicsDebugVisualizer,
                                                RuntimeRenderBackendView& m_renderBackendView,
                                                RuntimeRenderer& m_renderer,
                                                SceneController& m_sceneController )
{
    InputRouter& m_inputRouter = inputRouter;
    // Lifetime: these aliases expose InputRouter-owned frame state only for
    // this synchronous routing pass; Run retains neither value as member state.
    RuntimeInputContext& m_runtimeInput = m_inputRouter.RuntimeContext();
    InputActions& m_inputActions = m_inputRouter.Actions();
    const auto SceneState = [&]() -> RunSceneState& { return m_sceneController.State(); };
    const auto NormalizeCameraModeForCurrentScene = [&]( RunCameraMode mode )
    {
        return NormalizeRuntimeCameraMode( mode,
                                           SceneState().isSceneMode,
                                           RuntimeCameraModeEnabledMask( m_sceneController ) );
    };
    const auto CameraModeEnabledMask = [&]() { return RuntimeCameraModeEnabledMask( m_sceneController ); };
    const auto EnterInteractiveSceneRun = [&]()
    {
        m_sceneController.EnterInteractiveRun();
        m_diagnosticsRuntime.Capture().DisableAutomationExit();
    };
    const auto RunUIStressActions = [&]()
    {
        return SkullbonezCore::Basics::RunUIStressActions(
            m_diagnosticsRuntime,
            &m_window,
            m_timers,
            m_UI,
            m_renderer,
            m_renderBackendView,
            m_debug,
            m_sceneController,
            m_camera,
            m_config,
            m_simulation,
            m_runtimeTools,
            m_launchOptions,
            m_startup,
            m_replayRuntime,
            m_inputRouter,
            m_interaction,
            m_attachedCamera,
            NormalizeCameraModeForCurrentScene( m_replayRuntime.Camera().restoreCameraMode ) );
    };
    const auto DrainCaptureRequests = [&]()
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
    };
    const auto DrainRenderDefaultRequests = [&]()
    {
        if ( m_renderDefaults.PendingCount() == 0 )
        {
            return false;
        }
        const RenderDefaultsSaveBatchResult batch =
            m_renderDefaults.DrainAtFrameCheckpoint( m_config.ordinaryRender,
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
    };
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
    auto commitPointerPresentation = [&]()
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
    if ( m_inputRouter.HandleUnfocusedFrame( m_runtimeInput,
                                             m_interaction,
                                             m_runtimeTools,
                                             m_replayRuntime,
                                             m_attachedCamera,
                                             m_camera,
                                             m_sceneController,
                                             m_UI ) )
    {
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
        [&]( RuntimeInputActionSource source, const RunInternal::EditorPlacementModeChangeResult& placementMode )
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
        m_inputRouter.RecordModeAction( m_runtimeInput,
                                        m_camera,
                                        m_runtimeTools,
                                        m_attachedCamera,
                                        RuntimeInputAction::ToggleEditorTool,
                                        source );
    };
    auto applyEditorPlacementModeToggle = [&]( RuntimeInputActionSource source )
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
                   m_renderDefaults.CinematicBaseline(),
                   m_startup,
                   m_diagnosticsRuntime,
                   m_timers,
                   m_assets,
                   m_workerPool,
                   m_window,
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
                   m_renderer )
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
                m_inputRouter.RecordModeAction( m_runtimeInput,
                                                m_camera,
                                                m_runtimeTools,
                                                m_attachedCamera,
                                                RuntimeInputAction::CycleAttachedCameraSubmode,
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
                m_inputRouter.RecordModeAction( m_runtimeInput,
                                                m_camera,
                                                m_runtimeTools,
                                                m_attachedCamera,
                                                RuntimeInputAction::ToggleAttachedCameraPin,
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
                      m_sceneController.Models().IsPhysicsSleepEnabled(),
                      m_renderer.VsyncEnabled(),
                      m_renderer.PipelineSyncEnabled(),
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
                    m_inputRouter.RecordModeAction( m_runtimeInput,
                                                    m_camera,
                                                    m_runtimeTools,
                                                    m_attachedCamera,
                                                    event.action,
                                                    event.source );
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
                m_inputRouter.RecordModeAction( m_runtimeInput,
                                                m_camera,
                                                m_runtimeTools,
                                                m_attachedCamera,
                                                event.action,
                                                event.source );
            }
            break;
        case RuntimeInputAction::SetDirectorPhasePose:
            if ( ( m_camera.mode == RunCameraMode::Director ||
                   RunCameraModeUsesFlyControls( m_camera.mode,
                                                 m_attachedCamera.State().activeFollow,
                                                 m_camera.director.grabbed ) ) &&
                 DemoDirectorPlayback::SetCurrentPhasePose( m_camera, m_sceneController.Cameras() ) )
            {
                m_inputRouter.RecordModeAction( m_runtimeInput,
                                                m_camera,
                                                m_runtimeTools,
                                                m_attachedCamera,
                                                event.action,
                                                event.source );
            }
            break;
        case RuntimeInputAction::StepDirectorPhase:
            if ( ( m_camera.mode == RunCameraMode::Director ||
                   RunCameraModeUsesFlyControls( m_camera.mode,
                                                 m_attachedCamera.State().activeFollow,
                                                 m_camera.director.grabbed ) ) &&
                 DemoDirectorPlayback::SelectNextPhaseForAuthoring( m_camera, m_sceneController.Cameras() ) )
            {
                m_inputRouter.RecordModeAction( m_runtimeInput,
                                                m_camera,
                                                m_runtimeTools,
                                                m_attachedCamera,
                                                event.action,
                                                event.source );
            }
            break;
        case RuntimeInputAction::SaveDirectorShotList:
            if ( ( m_camera.mode == RunCameraMode::Director ||
                   RunCameraModeUsesFlyControls( m_camera.mode,
                                                 m_attachedCamera.State().activeFollow,
                                                 m_camera.director.grabbed ) ) &&
                 DemoDirectorPlayback::SaveShotList( m_camera ) )
            {
                m_inputRouter.RecordModeAction( m_runtimeInput,
                                                m_camera,
                                                m_runtimeTools,
                                                m_attachedCamera,
                                                event.action,
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
            HandleDiagnosticsKeyboardShortcut(
                DiagnosticsKeyboardShortcutContext{ m_debug,
                                                    m_camera.trackBallRow.value,
                                                    m_sceneController.Models(),
                                                    m_renderBackendView.renderDiagnostics,
                                                    SceneState().isSceneMode,
                                                    m_timers.simulationTimer.GetTimeSinceLastStart() },
                event.action,
                true );
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
                m_inputRouter.RecordModeAction( m_runtimeInput,
                                                m_camera,
                                                m_runtimeTools,
                                                m_attachedCamera,
                                                event.action,
                                                event.source );
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
                                              m_assets,
                                              ActiveSceneCinematicConfig( SceneState(), m_config ),
                                              m_renderDefaults.CinematicBaseline() },
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
                                                                   m_window,
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
                             m_window,
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
        if ( m_inputRouter.DispatchAfterUiDismiss( m_inputActions,
                                                   uiFrameResult.commands.ui.userInteracted,
                                                   m_camera,
                                                   m_attachedCamera,
                                                   m_runtimeTools,
                                                   m_replayRuntime,
                                                   m_sceneController,
                                                   m_diagnosticsRuntime,
                                                   m_debug,
                                                   m_UI,
                                                   m_timers.simulationTimer.GetTotalTime() ) )
        {
            PostQuitMessage( 0 );
        }
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
                                     m_config,
                                     m_sceneController,
                                     m_assets,
                                     m_workerPool,
                                     m_simulation,
                                     m_contactAudio,
                                     m_renderBackendView,
                                     m_renderDefaults,
                                     m_renderer,
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
                                                       m_renderer,
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
                                                                          m_assets,
                                                                          m_workerPool,
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
                                                               m_window,
                                                               pointerInput.rayOrigin,
                                                               pointerInput.rayDirection );
    pointerInput.hasClampedWorldRay = m_inputRouter.TryBuildWorldRay( m_sceneController.Cameras(),
                                                                      m_window,
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
        m_assets,
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
        m_inputRouter.RecordModeAction( m_runtimeInput,
                                        m_camera,
                                        m_runtimeTools,
                                        m_attachedCamera,
                                        pointerResult.modeActions[actionIndex],
                                        RuntimeInputActionSource::Mouse );
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
        m_inputRouter.DispatchCaptureActions( m_inputActions,
                                              m_camera,
                                              m_attachedCamera,
                                              m_replayRuntime,
                                              m_sceneController,
                                              m_diagnosticsRuntime,
                                              m_UI );
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
                                                                      m_renderDefaults.CinematicBaseline(),
                                                                      m_startup,
                                                                      m_diagnosticsRuntime,
                                                                      m_timers,
                                                                      m_assets,
                                                                      m_workerPool,
                                                                      m_window,
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
                                                                      m_renderer );
        if ( processedCapture || processedDefaults || processedScene )
        {
        }
    }
    commitPointerPresentation();
}
