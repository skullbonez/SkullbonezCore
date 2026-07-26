/*
File: SkullbonezSource/Runtime/Scene/SceneRuntimeUiOptions.cpp
Purpose:
  Applies authored scene UI options through a scene-runtime boundary.

Summary:
  Scene load chooses whether automation scenes hide UI, whether authored window
  state wins, and which deterministic UI stress values should seed diagnostics.
  Keep those decisions here so Run only supplies owners and timing.

Glossary:
  UI options: Optional `ui` block parsed from a `.scene.json` file.
  Automation scene: Screenshot/perf/exit scene where the UI should not cover the
    validation capture unless the scene explicitly says otherwise.
  UI stress: Deterministic diagnostics input churn for exercising UI behavior.

Invariants:
  - Authored visible/minimized/window settings apply in the same order as the
    historic RunScene code.
  - UI stress fields are clamped and applied independently of visible UI
    preservation.

Related:
  - SkullbonezSource/Runtime/Scene/SceneRuntimeUiOptions.h
  - SkullbonezSource/Runtime/Diagnostics/DiagnosticsRuntime.h
  - SkullbonezSource/UI/UI.h
*/
#include "SceneRuntimeUiOptions.h"
#include "../Diagnostics/DiagnosticsRuntime.h"
#include "../Diagnostics/OverlayDebugState.h"
#include "../../Scene/AuthoredScene.h"
#include "../../UI/UI.h"

#include <algorithm>

namespace SkullbonezCore
{
namespace Runtime
{
void PrepareSceneUiOptions( SceneRuntimeUiOptionsContext context, const SceneUIOptions& options, double nowSeconds,
                            bool preserveUIState, bool automationScene )
{
    context.activation.authoredOptions = options;
    context.activation.nowSeconds = nowSeconds;
    context.activation.hasAuthoredOptions = true;
    context.activation.preserveUIState = preserveUIState;
    context.activation.automationScene = automationScene;

    // Why: diagnostics and debug values already have genuine load-phase owners;
    // only window presentation crosses the returned activation value.

    if ( !preserveUIState && options.hasTestPattern )
    {
        context.debug.isUITestPattern = options.testPatternEnabled;
    }

    if ( options.hasStress )
    {
        context.diagnostics.UIStress().enabled = options.stressEnabled;
    }

    if ( options.hasStressSeed )
    {
        context.diagnostics.UIStress().randomState = options.stressSeed;
    }

    if ( options.hasStressActions )
    {
        context.diagnostics.UIStress().actionsPerFrame = std::clamp( options.stressActionsPerFrame, 1, 32 );
    }
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

} // namespace Runtime
} // namespace SkullbonezCore
