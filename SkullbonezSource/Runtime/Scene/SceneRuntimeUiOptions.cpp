/*
File: SkullbonezSource/Runtime/Scene/SceneRuntimeUiOptions.cpp
Purpose:
  Applies authored scene UI options through a scene-runtime boundary.

Mental model:
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
#include "../RunState.h"
#include "../../Scene/TestScene.h"
#include "../../UI/UI.h"

#include <algorithm>

namespace SkullbonezCore
{
namespace Basics
{
void ApplySceneRuntimeUiOptions( SceneRuntimeUiOptionsContext context, const SceneUIOptions& options )
{
    // Concept: Scene-authored UI state is a load policy, not a frame policy.
    // Preserve live operator UI for resets, but still consume stress directives
    // because they are diagnostics inputs authored by the scene.
    if ( !context.preserveUIState )
    {
        if ( !options.hasVisible )
        {
            if ( context.automationScene && !options.hasSettings )
            {
                context.ui.SetVisible( false, context.nowSeconds );
            }
            else if ( !options.hasSettings )
            {
                if ( !context.ui.IsVisible() )
                {
                    context.ui.SetVisible( true, context.nowSeconds );
                }
                context.ui.SetMinimized( true, context.nowSeconds );
            }
            else if ( !context.ui.IsVisible() )
            {
                context.ui.SetVisible( true, context.nowSeconds );
            }
        }
        if ( options.hasWindowRect )
        {
            context.ui.SetWindowBounds( options.windowX, options.windowY, options.windowW, options.windowH );
            if ( !options.hasMinimized )
            {
                context.ui.SetMinimized( false, context.nowSeconds );
            }
        }
        if ( options.hasActiveTab )
        {
            context.ui.SetActiveTab( static_cast<UI::InGameUITab>( options.activeTab ) );
        }
        if ( options.hasBlur )
        {
            context.ui.SetBlurEnabled( options.blurEnabled );
        }
        if ( options.hasProfilerExpandAll )
        {
            context.ui.SetProfilerExpandAll( options.profilerExpandAll );
        }
        if ( options.hasProfilerTimeline )
        {
            context.ui.SetProfilerTimelineEnabled( options.profilerTimeline );
        }
        if ( options.hasPerformanceHistogram )
        {
            context.ui.SetPerformanceHistogramEnabled( options.performanceHistogram );
        }
        if ( options.hasHitboxOverlay )
        {
            context.ui.SetHitboxOverlayEnabled( options.hitboxOverlay );
        }
        if ( options.hasRendererComboOpen )
        {
            context.ui.SetRendererComboOpen( options.rendererComboOpen );
        }
        if ( options.hasWaterComboOpen )
        {
            context.ui.SetWaterComboOpen( options.waterComboOpen );
        }
        if ( options.hasSceneComboOpen )
        {
            context.ui.SetSceneComboOpen( options.sceneComboOpen );
        }
        if ( options.hasSceneFilter )
        {
            context.ui.SetSceneFilter( options.sceneFilter );
        }
        if ( options.hasScrollY )
        {
            context.ui.SetScrollY( options.scrollY );
        }
        context.ui.SetMouseOverride( options.hasMouseOverride, options.mouseX, options.mouseY );
        if ( options.hasVisible )
        {
            context.ui.SetVisible( options.isVisible, context.nowSeconds );
        }
        if ( options.hasMinimized )
        {
            context.ui.SetMinimized( options.isMinimized, 0.0 );
        }
        if ( options.hasTestPattern )
        {
            context.debug.isUITestPattern = options.testPatternEnabled;
        }
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

} // namespace Basics
} // namespace SkullbonezCore
