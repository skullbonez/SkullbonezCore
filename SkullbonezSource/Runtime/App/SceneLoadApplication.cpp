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
#include "../Automation/RuntimeValidationHarness.h"
#include "../Camera/AttachedCameraController.h"
#include "../Diagnostics/RuntimeOverlayDiagnostics.h"
#include "../Input/Input.h"
#include "../Input/InputRouter.h"
#include "../Scene/SceneLoadTransaction.h"
#include "../Startup/RunLaunchOptions.h"
#include "../Startup/Window.h"
#include "../Tools/RuntimeTools.h"
#include "../../Rendering/DX12/RenderDeviceDX12.h"
#include "../../UI/UI.h"

#include <cstddef>
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


void ApplySceneLoadRuntimeReactions( SceneLoadTransaction& transaction, const RunLaunchOptions& launchOptions,
                                     RuntimeOverlayDiagnostics& overlays, SceneController& sceneController,
                                     InputRouter& inputRouter, RuntimeInteractionController& interaction,
                                     CameraControlState& camera, AttachedCameraController& attachedCamera,
                                     RuntimeTools& runtimeTools, ReplayRuntime& replayRuntime )
{
    const SceneLoadResult& outputs = transaction.BeginRuntimeReactions();
    const SceneLifecyclePacket& lifecycle = sceneController.LifecyclePacket();

    overlays.ObserveSceneLifecycle( lifecycle, outputs.presentation );

    for ( std::size_t index = 0; index < outputs.completedWorldChangeCount; ++index )
    {
        const SceneLoadCompletedWorldChange& change = outputs.completedWorldChanges[index];
        replayRuntime.SubmitEvent( ReplayEventCommandOperations::BuildWorldOverride(
            change.previousGravity, change.previousFluidHeight, change.previousFluidDensity, change.gravity,
            change.fluidHeight, change.fluidDensity ) );
    }

    runtimeTools.ObserveSceneLifecycle( lifecycle, sceneController.Scene(), inputRouter, interaction );
    attachedCamera.ObserveSceneLifecycle( lifecycle );
    replayRuntime.ObserveSceneLifecycleAfterClear( lifecycle, interaction, inputRouter );

    const bool enterInspectAfterActivation = outputs.camera.mode == RunCameraMode::Inspect;
    interaction.ObserveSceneLifecycle( lifecycle, enterInspectAfterActivation );
    camera.ObserveSceneLifecycle( lifecycle, outputs.camera );

    if ( inputRouter.ObserveSceneLifecycle( lifecycle, enterInspectAfterActivation ) )
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
