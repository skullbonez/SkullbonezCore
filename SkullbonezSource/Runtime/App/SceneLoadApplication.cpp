/*
File: SkullbonezSource/Runtime/App/SceneLoadApplication.cpp
Purpose:
  Applies detached scene-load results at App-owned runtime boundaries.

Summary:
  App consumes Scene's typed result in two ordered passes. The first composes
  runtime sibling reactions and Replay events; the second performs native,
  UI, render-device, and validation publication.

Invariants:
  - Every borrowed reference expires before the application function returns.
  - Replay observes only successfully completed Scene requests.
  - Native and UI presentation cannot run before runtime reactions.

Related:
  - SkullbonezSource/Runtime/App/SceneLoadApplication.h
  - SkullbonezSource/Runtime/Scene/SceneLoadTransaction.h
  - SkullbonezSource/Runtime/App/ReplayRuntime.h
*/
#include "SceneLoadApplication.h"

#include "InputFrame.h"
#include "ReplayRuntime.h"
#include "Run.h"
#include "../Automation/RuntimeValidationHarness.h"
#include "../Capture/GraphicsStressController.h"
#include "../Scene/AttachedCameraController.h"
#include "../Diagnostics/RuntimeOverlayDiagnostics.h"
#include "../Diagnostics/OverlayDebugState.h"
#include "../Editor/EditorTools.h"
#include "../Input/Input.h"
#include "../Input/InputRouter.h"
#include "../Scene/SceneLoadTransaction.h"
#include "../Render/RuntimeRenderer.h"
#include "../Startup/RunLaunchOptions.h"
#include "../Startup/Window.h"
#include "../Tools/RuntimeTools.h"
#include "../../Rendering/DX12/RenderDeviceDX12.h"
#include "../UI/GameUI/UI.h"

#include <cstddef>
#include <cstdio>
#include <utility>

namespace SkullbonezCore
{
namespace Runtime
{
namespace
{
void ApplyScenePresentationValues( RuntimeOverlayDiagnostics& overlays, const ScenePresentationValues& presentation )
{
    OverlayDebugState state = overlays.PresentationSnapshot();
    state.isWaterFreezeDebug = presentation.waterFreeze;
    state.isWaterNoReflect = presentation.waterNoReflect;
    state.isWaterRTReflect = presentation.waterRtReflect;
    state.isWaterFlatDebug = presentation.waterFlat;
    state.isTerrainHidden = presentation.terrainHidden;
    state.isWaterHidden = presentation.waterHidden;
    state.physicsDebugFlags = presentation.physicsDebugFlags;
    state.isPhysicsDebugTransparent = presentation.physicsDebugTransparent;
    state.physicsDebugAlpha = presentation.physicsDebugAlpha;
    state.physicsDebugContactLinger = presentation.physicsDebugContactLinger;
    state.physicsDebugPipelineStageCursor = presentation.physicsDebugPipelineStageCursor;
    state.isCollisionVisualizer = presentation.collisionVisualizer;
    state.isTextOnly = presentation.textOnly;
    state.isUITestPattern = presentation.uiTestPattern;
    state.isBroadphaseOverlay = presentation.broadphaseOverlay;
    state.frozenWaterTime = presentation.frozenWaterTime;
    overlays.ApplyScenePresentation( state );
}


void CaptureSceneDiagnosticsLoad( SceneLoadTransaction& transaction, DiagnosticsRuntime& diagnostics )
{
#ifdef _DEBUG
    const RunPerfLogState& perfLog = diagnostics.PerfLog();
    const RunPhysicsDiagnosticsState& physicsDiagnostics = diagnostics.PhysicsDiagnostics();
    transaction.CaptureDiagnosticsLoad( physicsDiagnostics.isEnabled, physicsDiagnostics.path,
                                        perfLog.physicsRegressionLogOverride, perfLog.physicsCollisionTimeLogOverride );
#else
    (void)diagnostics;
    transaction.CaptureDiagnosticsLoad( false, "", "", "" );
#endif
}


bool ApplySceneDiagnosticsReactions( const SceneDiagnosticsReactionBatch& reactions, DiagnosticsRuntime& diagnostics,
                                     bool applyAuthoredPerfLog, SceneController& sceneController,
                                     const SkullbonezCore::Core::EngineConfig& config )
{
#ifndef _DEBUG
    (void)sceneController;
    (void)config;
#endif

    bool succeeded = true;

    for ( std::size_t index = 0; index < reactions.count; ++index )
    {
        const SceneDiagnosticsReaction& reaction = reactions.reactions[index];

        switch ( reaction.kind )
        {
        case SceneDiagnosticsReactionKind::ResetForSceneLoad:
            succeeded = diagnostics.ResetForSceneLoad( reaction.value ) && succeeded;
            break;
        case SceneDiagnosticsReactionKind::ConfigurePerfLogFlush:
            diagnostics.ConfigurePerfLogFlush( reaction.enabled, reaction.value );
            break;
        case SceneDiagnosticsReactionKind::ApplyScenePerfLog:
            if ( applyAuthoredPerfLog )
            {
                succeeded = diagnostics.ApplyScenePerfLogOptions( reaction.path, reaction.value ) && succeeded;
            }
            break;
        case SceneDiagnosticsReactionKind::SetUiStressEnabled:
            diagnostics.UIStress().SetEnabled( reaction.enabled );
            break;
        case SceneDiagnosticsReactionKind::SetUiStressSeed:
            diagnostics.UIStress().SetRandomState( static_cast<unsigned int>( reaction.value ) );
            break;
        case SceneDiagnosticsReactionKind::SetUiStressActions:
            diagnostics.UIStress().SetActionsPerFrame( reaction.value );
            break;
        case SceneDiagnosticsReactionKind::ConfigureUiStress:
            diagnostics.UIStress().Configure( reaction.enabled, static_cast<unsigned int>( reaction.value ),
                                              reaction.secondaryValue );
            break;
        case SceneDiagnosticsReactionKind::ClosePerfLog:
            succeeded = diagnostics.ClosePerfLog() && succeeded;
            break;
        case SceneDiagnosticsReactionKind::ResetPerfLogForSceneLoad:
            diagnostics.ResetPerfLogForSceneLoad();
            break;
        case SceneDiagnosticsReactionKind::BeginPhysicsDiagnostics:
#ifdef _DEBUG
            diagnostics.BeginPhysicsDiagnosticsRun( sceneController.Scene().Physics(),
                                                    ProjectSceneDiagnosticFacts( sceneController.State() ), config,
                                                    reaction.path, reaction.rendererName,
                                                    reaction.explicitRenderFrameLockstep,
                                                    reaction.effectiveRenderFrameLockstep );
#endif
            break;
        }
    }

    return succeeded;
}

bool ApplyCommandLinePerfLogOverride( const RunLaunchOptions& launchOptions, DiagnosticsRuntime& diagnostics, int perfPass )
{
    if ( launchOptions.perfLogPath[0] == '\0' )
    {
        return true;
    }

    // Why: a command-line capture must work for generated scenes such as
    // demohero, which have no authored logging block. It deliberately replaces
    // a scene path after the scene reaction has completed so one file owns the run.
    return diagnostics.ClosePerfLog() && diagnostics.OpenScenePerfLog( launchOptions.perfLogPath, perfPass );
}


void ApplySceneUiActivation( UI::InGameUI& ui, const SceneUiActivation& activation )
{
    if ( activation.hasAuthoredOptions && !activation.preserveUIState )
    {
        const SceneUIOptions& options = activation.authoredOptions;

        if ( !options.hasVisible )
        {
            if ( activation.automationScene && !options.hasSettings )
            {
                ui.SetVisible( false, activation.nowSeconds );
            }
            else if ( !options.hasSettings )
            {
                if ( !ui.IsVisible() )
                {
                    ui.SetVisible( true, activation.nowSeconds );
                }

                ui.SetMinimized( true, activation.nowSeconds );
            }
            else if ( !ui.IsVisible() )
            {
                ui.SetVisible( true, activation.nowSeconds );
            }
        }

        if ( options.hasWindowRect )
        {
            ui.SetWindowBounds( options.windowX, options.windowY, options.windowW, options.windowH );

            if ( !options.hasMinimized )
            {
                ui.SetMinimized( false, activation.nowSeconds );
            }
        }

        if ( options.hasActiveTab )
        {
            ui.SetActiveTab( static_cast<UI::InGameUITab>( options.activeTab ) );
        }

        if ( options.hasBlur )
        {
            ui.SetBlurEnabled( options.blurEnabled );
        }

        if ( options.hasProfilerExpandAll )
        {
            ui.SetProfilerExpandAll( options.profilerExpandAll );
        }

        if ( options.hasProfilerTimeline )
        {
            ui.SetProfilerTimelineEnabled( options.profilerTimeline );
        }

        if ( options.hasPerformanceHistogram )
        {
            ui.SetPerformanceHistogramEnabled( options.performanceHistogram );
        }

        if ( options.hasHitboxOverlay )
        {
            ui.SetHitboxOverlayEnabled( options.hitboxOverlay );
        }

        if ( options.hasRendererComboOpen )
        {
            ui.SetRendererComboOpen( options.rendererComboOpen );
        }

        if ( options.hasWaterComboOpen )
        {
            ui.SetWaterComboOpen( options.waterComboOpen );
        }

        if ( options.hasSceneComboOpen )
        {
            ui.SetSceneComboOpen( options.sceneComboOpen );
        }

        if ( options.hasSceneFilter )
        {
            ui.SetSceneFilter( options.sceneFilter );
        }

        if ( options.hasScrollY )
        {
            ui.SetScrollY( options.scrollY );
        }

        ui.SetMouseOverride( options.hasMouseOverride, options.mouseX, options.mouseY );

        if ( options.hasVisible )
        {
            ui.SetVisible( options.isVisible, activation.nowSeconds );
        }

        if ( options.hasMinimized )
        {
            ui.SetMinimized( options.isMinimized, 0.0 );
        }
    }

    if ( activation.forceVisible )
    {
        ui.SetVisible( true, activation.nowSeconds );
    }

    if ( activation.forceUnminimized )
    {
        ui.SetMinimized( false, activation.nowSeconds );
    }
}
} // namespace


ScenePresentationValues ProjectScenePresentationValues( const OverlayDebugState& presentation )
{
    ScenePresentationValues values;
    values.waterFreeze = presentation.isWaterFreezeDebug;
    values.waterNoReflect = presentation.isWaterNoReflect;
    values.waterRtReflect = presentation.isWaterRTReflect;
    values.waterFlat = presentation.isWaterFlatDebug;
    values.terrainHidden = presentation.isTerrainHidden;
    values.waterHidden = presentation.isWaterHidden;
    values.physicsDebugFlags = presentation.physicsDebugFlags;
    values.physicsDebugTransparent = presentation.isPhysicsDebugTransparent;
    values.physicsDebugAlpha = presentation.physicsDebugAlpha;
    values.physicsDebugContactLinger = presentation.physicsDebugContactLinger;
    values.physicsDebugPipelineStageCursor = presentation.physicsDebugPipelineStageCursor;
    values.collisionVisualizer = presentation.isCollisionVisualizer;
    values.textOnly = presentation.isTextOnly;
    values.uiTestPattern = presentation.isUITestPattern;
    values.broadphaseOverlay = presentation.isBroadphaseOverlay;
    values.frozenWaterTime = presentation.frozenWaterTime;
    return values;
}


SkullbonezCore::Core::SbResult Run::LoadSceneRequest( SceneLoadTransaction& transaction, const SceneLoadRequest& request )
{
    RuntimeRenderer& renderer = Renderer( "Run::LoadSceneRequest" );
    const SceneLoadBeginResult& preparation = transaction.Prepare( m_sceneController, request, &renderer.RenderFrame(),
                                                                   request.enterInteractiveSceneRun ||
                                                                       m_launchOptions.interactiveSceneRun );

    if ( preparation.status.Ok() && preparation.shouldLoad )
    {
        const std::string* unloadingScenePath = m_sceneController.CurrentPath();
        m_diagnosticsRuntime.BeforeSceneUnload( m_sceneController.State().loadCount, m_sceneController.State().currentFrame,
                                                unloadingScenePath ? unloadingScenePath->c_str() : nullptr );
        transaction.CompleteBeforeUnloadDiagnostics();
    }

    CaptureSceneDiagnosticsLoad( transaction, m_diagnosticsRuntime );
    SkullbonezCore::Core::SbResult result = transaction.Load( m_sceneController, request, m_resultDiagnostics, m_config,
                                                              m_launchOptions, m_renderDefaults.CinematicBaseline(),
                                                              m_startup, m_assets, m_workerPool, &renderer.RenderFrame(),
                                                              &renderer.RenderResources() );

    // Invariant: the command-line artifact is the sole perf-log owner when
    // supplied. A missing authored directory must not fail a valid override.
    const bool applyAuthoredPerfLog = ShouldApplyAuthoredScenePerfLog( m_launchOptions.perfLogPath );
    bool diagnosticsSucceeded = ApplySceneDiagnosticsReactions( transaction.DiagnosticsReactions(), m_diagnosticsRuntime,
                                                                applyAuthoredPerfLog, m_sceneController, m_config );

    diagnosticsSucceeded = ApplyCommandLinePerfLogOverride( m_launchOptions, m_diagnosticsRuntime,
                                                            m_sceneController.PerfPass() ) &&
                           diagnosticsSucceeded;

    result = ApplySceneLoadDiagnosticsStatus( m_resultDiagnostics, m_applicationExit, result, diagnosticsSucceeded );

    const SceneRenderPolicyState& policy = transaction.RenderPolicy();
    renderer.RestorePresentationSettings( RenderPresentationSettings { policy.vsyncEnabled, policy.pipelineSyncEnabled } );

    const bool sceneMutationSucceeded = result.Ok();
    const bool activationPending = transaction.RenderActivationPending();

    if ( sceneMutationSucceeded && activationPending )
    {
        renderer.SetSceneIdentity( m_sceneController.State().currentSceneIndex, m_sceneController.State().loadCount );
        result = renderer.ResourceLifecycle().InitialiseSceneRayTracing( m_sceneController.Scene().Terrain().Get(),
                                                                         transaction.RenderActivationSceneObjectCapacity() );

        if ( SceneRenderActivationCompletesTransition( sceneMutationSucceeded, activationPending, result.Ok() ) )
        {
            // Invariant: activation becomes visible to lifecycle consumers only
            // after the Render owner has accepted the populated Scene capacity.
            transaction.CompleteRenderActivation( m_sceneController );
        }
    }

    return result;
}


bool Run::ExecutePendingSceneRequests( SceneLoadTransaction& transaction )
{
    const SceneRequestBatch batch = m_sceneController.TakePendingRequests();

    if ( batch.rejectedTransitionCount > 0 )
    {
        std::fprintf( stderr, "Runtime/SceneController: rejected %zu additional same-frame scene transition(s)\n",
                      batch.rejectedTransitionCount );
        std::fflush( stderr );
    }

    for ( std::size_t requestIndex = 0; requestIndex < batch.count; ++requestIndex )
    {
        const SceneRequest& request = batch.requests[requestIndex];
        bool accepted = false;

        switch ( request.type )
        {
        case SceneRequestType::LoadBrowserIndex:
            accepted = LoadSceneRequest( transaction, transaction
                                                          .NavigationForFollowingRequest( CaptureSceneLoadNavigationState(
                                                              m_operatorUi->SceneNavigation() ) )
                                                          .LoadSceneFromBrowserIndex( request.index, m_sceneController ) )
                           .Ok();
            break;
        case SceneRequestType::LoadDemoScene:
            accepted = LoadSceneRequest( transaction, transaction
                                                          .NavigationForFollowingRequest( CaptureSceneLoadNavigationState(
                                                              m_operatorUi->SceneNavigation() ) )
                                                          .LoadDemoScene( m_sceneController ) )
                           .Ok();
            break;
        case SceneRequestType::ResetCurrentScene:
            accepted = LoadSceneRequest( transaction, m_sceneController.ResetCurrentScene( request.preserveUIState,
                                                                                           request.suppressExitOnComplete,
                                                                                           request.preserveRuntimeState ) )
                           .Ok();
            break;
        case SceneRequestType::CreateScene:
        {
            const SceneLoadRequest createRequest = m_sceneController.CreateScene( request.text );
            accepted = LoadSceneRequest( transaction, createRequest ).Ok();
            transaction.SetRefreshSceneBrowser( createRequest.accepted );
            break;
        }
        case SceneRequestType::SaveCurrentDefaults:
        {
            const ScenePresentationValues
                presentation = transaction
                                   .PresentationForFollowingRequest( ProjectScenePresentationValues(
                                                                         m_overlayDiagnostics->PresentationSnapshot() ),
                                                                     m_sceneController.LifecyclePacket() );
            const SceneLoadNavigationState& navigation = transaction.NavigationForFollowingRequest(
                CaptureSceneLoadNavigationState( m_operatorUi->SceneNavigation() ) );
            const SceneDefaultsSaveSnapshot saveSnapshot = ProjectSceneDefaultsSaveSnapshot( presentation,
                                                                                             transaction.RenderPolicy(),
                                                                                             transaction.CurrentCamera(),
                                                                                             navigation.overrides );
            const SkullbonezCore::Core::SbResult saveResult = m_sceneController.SaveCurrentDefaults( saveSnapshot );

            if ( !saveResult.Ok() )
            {
                std::fprintf( stderr, "[%s] %s\n", saveResult.ErrorOwner(), saveResult.ErrorMessage() );
                std::fflush( stderr );
            }

            accepted = saveResult.Ok();
            break;
        }
        }

        if ( accepted )
        {
            transaction.RecordCompletedRequest( request );
        }

        if ( !SceneRequestBatchContinuesAfter( request.type, accepted ) )
        {
            break;
        }
    }

    transaction.FinishRequestBatch();
    return batch.count > 0;
}


void Run::ApplySceneLoadRuntimeReactions( SceneLoadTransaction& transaction )
{
    const SceneLoadResult& outputs = transaction.BeginRuntimeReactions();
    const SceneLifecyclePacket& lifecycle = m_sceneController.LifecyclePacket();
    ApplySceneCaptureReactions( m_capture, outputs.captureReactions );

    if ( m_overlaySceneLifecycleObserver.ShouldApply( lifecycle, SceneRuntimeLifecycleEvent::AfterSceneCleared ) )
    {
        ApplyScenePresentationValues( *m_overlayDiagnostics, outputs.presentation );
    }

    for ( std::size_t index = 0; index < outputs.completedWorldChangeCount; ++index )
    {
        const SceneLoadCompletedWorldChange& change = outputs.completedWorldChanges[index];
        m_replayRuntime.SubmitEvent(
            ReplayEventCommandOperations::BuildWorldOverride( change.previousGravity, change.previousFluidHeight,
                                                              change.previousFluidDensity, change.gravity,
                                                              change.fluidHeight, change.fluidDensity ) );
    }

    m_runtimeTools.ObserveSceneLifecycle( lifecycle, m_inputRouter, m_interaction );
    m_editorTools.ObserveSceneLifecycle( lifecycle, m_sceneController.Scene(), m_interaction );

    if ( m_attachedCameraSceneLifecycleObserver.ShouldApply( lifecycle, SceneRuntimeLifecycleEvent::AfterSceneCleared ) )
    {
        m_attachedCamera.ResetForSceneLoad();
    }

    m_replayRuntime.ObserveSceneLifecycleAfterClear( lifecycle, m_interaction, m_inputRouter );

    const bool enterInspectAfterActivation = outputs.camera.mode == RunCameraMode::Inspect;
    m_interaction.ObserveSceneLifecycle( lifecycle.generation,
                                         SceneLifecycleReached( lifecycle.event,
                                                                SceneRuntimeLifecycleEvent::AfterSceneCleared ),
                                         SceneLifecycleReached( lifecycle.event,
                                                                SceneRuntimeLifecycleEvent::AfterSceneActivated ),
                                         enterInspectAfterActivation );

    if ( m_cameraSceneLifecycleObserver.ShouldApply( lifecycle, SceneRuntimeLifecycleEvent::AfterSceneCleared ) )
    {
        m_camera = outputs.camera;
    }

    if ( ApplySceneActivationInputReaction( lifecycle, enterInspectAfterActivation, m_inputSceneLifecycleObserver,
                                            m_inputRouter ) )
    {
        Hardware::Input::ResetMouseLookDeltas();
    }

    RunCameraMode restoreMode = m_replayRuntime.BuildInputView().restoreCameraMode;

    if ( m_sceneController.State().isSceneMode )
    {
        restoreMode = restoreMode == RunCameraMode::Demo ? RunCameraMode::Scene : restoreMode;
    }
    else if ( restoreMode == RunCameraMode::Scene )
    {
        restoreMode = m_sceneController.Scene().SceneEntityCount() > 0 ? RunCameraMode::Demo : RunCameraMode::Inspect;
    }
    else if ( restoreMode == RunCameraMode::Demo && m_sceneController.Scene().SceneEntityCount() <= 0 )
    {
        restoreMode = RunCameraMode::Inspect;
    }

    const ReplaySceneTimelineResetInput timelineReset = ReplayTimelineOperations::
        DescribeReplaySceneTimeline( m_sceneController, outputs.navigation.overrides, m_sceneController.State(),
                                     m_sceneController.Scene().ActiveSceneObjectCapacity(),
                                     static_cast<uint32_t>( m_launchOptions.generatedObjectTypeOverride ) );

    m_replayRuntime.ObserveSceneLifecycleAfterActivation( lifecycle, timelineReset, m_inputRouter, m_interaction,
                                                          &m_sceneController.Scene().Cameras(),
                                                          m_sceneController.Scene().Terrain().Get(), m_camera, restoreMode,
                                                          m_attachedCamera.State().activeFollow, m_camera.director.grabbed );

    if ( m_launchOptions.replayGuideArcsAtStartup && lifecycle.event == SceneRuntimeLifecycleEvent::AfterSceneActivated )
    {
        m_replayRuntime.SetGuideArcsEnabled( true );
    }

    for ( std::size_t index = 0; index < outputs.completedRequests.count; ++index )
    {
        if ( outputs.completedRequests.requests[index].type == SceneRequestType::SaveCurrentDefaults )
        {
            m_editorTools.Editor().history.MarkClean();
            break;
        }
    }

    for ( std::size_t index = 0; index < outputs.completedRequests.count; ++index )
    {
        const SceneRequest& request = outputs.completedRequests.requests[index];
        ReplayOwnerEventCode eventCode = ReplayOwnerEventCode::SceneLoadBrowserIndex;

        switch ( request.type )
        {
        case SceneRequestType::LoadBrowserIndex:
            eventCode = ReplayOwnerEventCode::SceneLoadBrowserIndex;
            break;
        case SceneRequestType::LoadDemoScene:
            eventCode = ReplayOwnerEventCode::SceneLoadDemo;
            break;
        case SceneRequestType::ResetCurrentScene:
            eventCode = ReplayOwnerEventCode::SceneReset;
            break;
        case SceneRequestType::CreateScene:
            eventCode = ReplayOwnerEventCode::SceneCreate;
            break;
        case SceneRequestType::SaveCurrentDefaults:
            eventCode = ReplayOwnerEventCode::SceneSaveDefaults;
            break;
        }

        m_replayRuntime.SubmitEvent(
            ReplayEventCommandOperations::BuildCommand( ReplayEventKind::OwnerAction, 0, true, SceneRequestFlags( request ),
                                                        static_cast<int32_t>( eventCode ), request.index, 0, 0, 0,
                                                        request.type == SceneRequestType::CreateScene
                                                            ? request.text
                                                            : ReplayOwnerEventName( eventCode ) ) );
    }
}


void ApplySceneLoadPresentation( SceneLoadTransaction& transaction, Window& window, UI::InGameUI& operatorUi,
                                 RuntimeValidationHarness& validationHarness, GraphicsStressController& graphicsStress,
                                 SceneLifecycleGenerationObserver& graphicsStressSceneObserver,
                                 const RunLaunchOptions& launchOptions, Rendering::Dx12RenderDevice* renderDevice,
                                 bool rendererVsyncEnabled, SceneController& sceneController )
{
    const SceneLoadResult& outputs = transaction.BeginPresentation();
    const SceneLifecyclePacket& lifecycle = sceneController.LifecyclePacket();
    validationHarness.SceneGates().ObserveSceneLifecycle( lifecycle, transaction.TakeAutomationGates() );

    if ( renderDevice && SceneLifecycleReached( lifecycle.event, SceneRuntimeLifecycleEvent::AfterSceneActivated ) )
    {
        renderDevice->SetVsyncEnabled( rendererVsyncEnabled );
    }

    if ( outputs.windowTitle[0] != '\0' )
    {
        window.SetTitleText( outputs.windowTitle );
    }

    if ( outputs.applyNavigation )
    {
        ApplySceneLoadNavigationState( operatorUi.SceneNavigation(), outputs.navigation );
    }

    if ( outputs.refreshSceneBrowser )
    {
        operatorUi.SceneNavigation().RefreshBrowserList();
    }

    ApplySceneUiActivation( operatorUi, outputs.uiActivation );

    // Invariant: a reload may be sampled more than once, but the Capture-owned
    // random stream and cadence resume exactly once after population commits.
    if ( launchOptions.graphicsStress &&
         graphicsStressSceneObserver.ShouldApply( lifecycle, SceneRuntimeLifecycleEvent::AfterScenePopulate ) )
    {
        graphicsStress.ResumeAfterSceneLoad( launchOptions.graphicsStressSeed, launchOptions.graphicsStressActions,
                                             launchOptions.graphicsStressSceneIntervalFrames );
    }

    transaction.CompletePresentation();
}
} // namespace Runtime
} // namespace SkullbonezCore
