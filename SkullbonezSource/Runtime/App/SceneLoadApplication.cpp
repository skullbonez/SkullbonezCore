/*
File: SkullbonezSource/Runtime/App/SceneLoadApplication.cpp
Purpose:
  Applies detached scene-load results at App-owned runtime boundaries.

Summary:
  App consumes Scene's typed result in two ordered passes. The first composes
  runtime sibling reactions and Replay events; the second performs native,
  UI, render-device, and validation publication.

Invariants:
  - Every owner borrow expires before the application function returns.
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
#include "../Scene/AttachedCameraController.h"
#include "../Diagnostics/RuntimeOverlayDiagnostics.h"
#include "../Input/Input.h"
#include "../Input/InputRouter.h"
#include "../Scene/SceneLoadTransaction.h"
#include "../Render/RuntimeRenderer.h"
#include "../Startup/RunLaunchOptions.h"
#include "../Startup/Window.h"
#include "../Tools/RuntimeTools.h"
#include "../../Rendering/DX12/RenderDeviceDX12.h"
#include "../../UI/UI.h"

#include <cstddef>
#include <cstdio>
#include <utility>

namespace SkullbonezCore
{
namespace Runtime
{
namespace
{
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


SkullbonezCore::Core::SbResult Run::LoadSceneRequest( SceneLoadTransaction& transaction,
                                                      const SceneLoadRequest& request )
{
    RuntimeRenderer& renderer = Renderer( "Run::LoadSceneRequest" );
    SkullbonezCore::Core::SbResult result = transaction.Load(
        m_sceneController, request, m_config, m_launchOptions, m_renderDefaults.CinematicBaseline(), m_startup, m_assets,
        m_workerPool, m_diagnosticsRuntime, &renderer.RenderFrame(), &renderer.RenderResources() );

    const SceneRenderPolicyState& policy = transaction.RenderPolicy();
    renderer.RestorePresentationSettings( RenderPresentationSettings { policy.vsyncEnabled,
                                                                        policy.pipelineSyncEnabled } );

    const bool sceneMutationSucceeded = result.Ok();
    const bool activationPending = transaction.RenderActivation().pending;
    if ( sceneMutationSucceeded && activationPending )
    {
        result = renderer.ResourceLifecycle().InitialiseSceneRayTracing(
            transaction.RenderActivation().sceneObjectCapacity );
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
            accepted = LoadSceneRequest(
                           transaction,
                           transaction.NavigationForFollowingRequest( CaptureSceneLoadNavigationState(
                               m_operatorUi->SceneNavigation() ) )
                               .LoadSceneFromBrowserIndex( request.index, m_sceneController ) )
                           .Ok();
            break;
        case SceneRequestType::LoadDemoScene:
            accepted = LoadSceneRequest(
                           transaction,
                           transaction.NavigationForFollowingRequest( CaptureSceneLoadNavigationState(
                               m_operatorUi->SceneNavigation() ) )
                               .LoadDemoScene( m_sceneController ) )
                           .Ok();
            break;
        case SceneRequestType::ResetCurrentScene:
            accepted = LoadSceneRequest( transaction,
                                         m_sceneController.ResetCurrentScene( request.preserveUIState,
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
            const OverlayDebugState& presentation = transaction.PresentationForFollowingRequest(
                m_overlayDiagnostics->PresentationSnapshot(), m_sceneController.LifecyclePacket() );
            const SceneLoadNavigationState& navigation = transaction.NavigationForFollowingRequest(
                CaptureSceneLoadNavigationState( m_operatorUi->SceneNavigation() ) );
            const SkullbonezCore::Core::SbResult saveResult = m_sceneController.SaveCurrentDefaults(
                SceneDefaultsSaveView { presentation, transaction.RenderPolicy(), transaction.CurrentCamera(),
                                        navigation.overrides } );
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


void ApplySceneLoadRuntimeReactions( SceneLoadTransaction& transaction, const RunLaunchOptions& launchOptions,
                                     RuntimeOverlayDiagnostics& overlays,
                                     SceneLifecycleGenerationObserver& overlayLifecycle, SceneController& sceneController,
                                     SceneLifecycleGenerationObserver& inputLifecycle, InputRouter& inputRouter,
                                     RuntimeInteractionController& interaction,
                                     SceneLifecycleGenerationObserver& cameraLifecycle, CameraControlState& camera,
                                     SceneLifecycleGenerationObserver& attachedCameraLifecycle,
                                     AttachedCameraController& attachedCamera,
                                     RuntimeTools& runtimeTools, ReplayRuntime& replayRuntime )
{
    const SceneLoadResult& outputs = transaction.BeginRuntimeReactions();
    const SceneLifecyclePacket& lifecycle = sceneController.LifecyclePacket();

    if ( overlayLifecycle.ShouldApply( lifecycle, SceneRuntimeLifecycleEvent::AfterSceneCleared ) )
    {
        overlays.ApplyScenePresentation( outputs.presentation );
    }

    for ( std::size_t index = 0; index < outputs.completedWorldChangeCount; ++index )
    {
        const SceneLoadCompletedWorldChange& change = outputs.completedWorldChanges[index];
        replayRuntime.SubmitEvent( ReplayEventCommandOperations::BuildWorldOverride(
            change.previousGravity, change.previousFluidHeight, change.previousFluidDensity, change.gravity,
            change.fluidHeight, change.fluidDensity ) );
    }

    runtimeTools.ObserveSceneLifecycle( lifecycle, sceneController.Scene(), inputRouter, interaction );
    if ( attachedCameraLifecycle.ShouldApply( lifecycle, SceneRuntimeLifecycleEvent::AfterSceneCleared ) )
    {
        attachedCamera.ResetForSceneLoad();
    }
    replayRuntime.ObserveSceneLifecycleAfterClear( lifecycle, interaction, inputRouter );

    const bool enterInspectAfterActivation = outputs.camera.mode == RunCameraMode::Inspect;
    interaction.ObserveSceneLifecycle(
        { lifecycle.generation,
          SceneLifecycleReached( lifecycle.event, SceneRuntimeLifecycleEvent::AfterSceneCleared ),
          SceneLifecycleReached( lifecycle.event, SceneRuntimeLifecycleEvent::AfterSceneActivated ) },
        enterInspectAfterActivation );
    if ( cameraLifecycle.ShouldApply( lifecycle, SceneRuntimeLifecycleEvent::AfterSceneCleared ) )
    {
        camera = outputs.camera;
    }

    if ( ApplySceneActivationInputReaction( lifecycle, enterInspectAfterActivation, inputLifecycle, inputRouter ) )
    {
        Hardware::Input::ResetMouseLookDeltas();
    }

    RunCameraMode restoreMode = replayRuntime.BuildInputView().restoreCameraMode;
    if ( sceneController.State().isSceneMode )
    {
        restoreMode = restoreMode == RunCameraMode::Demo ? RunCameraMode::Scene : restoreMode;
    }
    else if ( restoreMode == RunCameraMode::Scene )
    {
        restoreMode = sceneController.Scene().SceneEntityCount() > 0 ? RunCameraMode::Demo : RunCameraMode::Inspect;
    }
    else if ( restoreMode == RunCameraMode::Demo && sceneController.Scene().SceneEntityCount() <= 0 )
    {
        restoreMode = RunCameraMode::Inspect;
    }

    const ReplaySceneTimelineResetInput timelineReset = ReplayTimelineOperations::DescribeReplaySceneTimeline(
        sceneController, outputs.navigation.overrides, sceneController.State(),
        sceneController.Scene().ActiveSceneObjectCapacity(),
        static_cast<uint32_t>( launchOptions.generatedObjectTypeOverride ) );

    replayRuntime.ObserveSceneLifecycleAfterActivation(
        lifecycle, timelineReset, inputRouter, interaction, &sceneController.Scene().Cameras(),
        sceneController.Scene().Terrain().Get(), camera, restoreMode, attachedCamera.State().activeFollow,
        camera.director.grabbed );

    if ( launchOptions.replayGuideArcsAtStartup && lifecycle.event == SceneRuntimeLifecycleEvent::AfterSceneActivated )
    {
        replayRuntime.SetGuideArcsEnabled( true );
    }

    for ( std::size_t index = 0; index < outputs.completedRequests.count; ++index )
    {
        if ( outputs.completedRequests.requests[index].type == SceneRequestType::SaveCurrentDefaults )
        {
            runtimeTools.Editor().history.MarkClean();
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

        replayRuntime.SubmitEvent( ReplayEventCommandOperations::BuildCommand(
            ReplayEventKind::OwnerAction, 0, true, SceneRequestFlags( request ),
            static_cast<int32_t>( eventCode ), request.index, 0, 0, 0,
            request.type == SceneRequestType::CreateScene ? request.text : ReplayOwnerEventName( eventCode ) ) );
    }
}


void ApplySceneLoadPresentation( SceneLoadTransaction& transaction, Window& window, UI::InGameUI& operatorUi,
                                 RuntimeValidationHarness& validationHarness, const RunLaunchOptions& launchOptions,
                                 Rendering::Dx12RenderDevice* renderDevice, bool rendererVsyncEnabled,
                                 SceneController& sceneController )
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
    validationHarness.ObserveSceneLifecycle( lifecycle, launchOptions );
    transaction.CompletePresentation();
}
} // namespace Runtime
} // namespace SkullbonezCore
