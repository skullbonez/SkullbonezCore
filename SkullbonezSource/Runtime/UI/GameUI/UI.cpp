/*
File: SkullbonezSource/Runtime/UI/GameUI/UI.cpp
Purpose:
  Composes in-engine UI drawing and preserves the public InGameUI command surface.

Summary:
  Records the current typed widget view into one complete
  ordered frame and delegates persistent window/input state to
  UIWindowInteractionOwner. Public wrappers preserve existing call
  sites while authority lives in the concrete interaction owner.

Invariants:
  - Draw geometry and hit testing must be derived from the same layout
    constants.
  - InGameUI never reaches into owner storage through friendship or a retained
    pointer; WidgetView is borrowed only inside the current draw call.
  - Draw builds values only; Runtime/Render performs every flush, preview
    resolution, resource operation, and GPU timing scope.

Related:
  - SkullbonezSource/Runtime/UI/GameUI/UI.h
  - SkullbonezSource/Runtime/UI/GameUI/UIWindowInteractionOwner.h
  - Agentic/Reference/engine-glossary.md
*/
#include "UI.h"
#include "UIFrameComposition.h"
#include "../../../UI/UIFontMetrics.h"
#include "../../../Core/Profiler.h"
#include "../../../UI/UIDraw.h"
#include "../../../UI/UIDrawList.h"
#include "../../../UI/UIDrawWidgets.h"
#include "../../../UI/UIInput.h"
#include "../../../UI/UILayout.h"
#include "GameUILayout.h"
#include "UITabControls.h"
#include "UITabCinematic.h"
#include "UITabEditor.h"
#include "UITabMemory.h"
#include "UITabOptions.h"
#include "UITabPhysics.h"
#include "UITabProfiler.h"
#include "UITabScene.h"
#include "UITabSky.h"
#include "../../../UI/UIStyle.h"
#include "../../../UI/UIWindowChrome.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <span>

using namespace SkullbonezCore::UI;
using namespace SkullbonezCore::UI::Widgets;
using namespace SkullbonezCore::UI::GameLayout;
using namespace SkullbonezCore::UI::OperatorControlPolicy;
using namespace SkullbonezCore::UI::FrameComposition;

// Invariant: these projections are the only place a tab-specific presentation
// borrow is assembled. Adding a visible tab fact requires adding it to that
// tab's view rather than widening every moved presenter back to the root frame.
UIControlsTabFrameView InGameUIFrameData::ControlsTabFrame() const
{
    return { scene.modelCapacity,  scene.rngSeed,          scene.solverBallCount,
             scene.solverBoxCount, world.worldFluidHeight, world.worldFluidDensity };
}

UIEditorTabFrameView InGameUIFrameData::EditorTabFrame() const
{
    return { editor.editorModeEnabled,  editor.editorPlacementMode,      editor.editorPlaceStatic,
             editor.editorTerrainAlign, editor.editorViewportLookActive, editor.editorObjectType,
             editor.editorUndoDepth,    editor.editorRedoDepth };
}

UICinematicTabFrameView InGameUIFrameData::CinematicTabFrame() const
{
    return { rendering.cinematic, scene.sceneOptions, scene.sceneOptionCount, scene.selectedCineModeSceneOption };
}

UIOptionsTabFrameView InGameUIFrameData::OptionsTabFrame() const
{
    return { rendering.ordinaryRender.shadow.enabled,
             rendering.cinematic.shadow.enabled,
             scene.timeScale,
             scene.presentationAlpha,
             scene.modelCount,
             scene.modelCapacity,
             scene.fixedStep,
             scene.presentationInterpolation,
             scene.presentationPinned,
             rendering.cinematicRendering,
             world.waterFreezeDebug,
             world.waterFlatDebug,
             world.terrainHidden,
             world.waterHidden };
}

UIPhysicsTabFrameView InGameUIFrameData::PhysicsTabFrame() const
{
    return { world.physicsDebug,
             world.worldGravity,
             world.rayCastImpulseStrength,
             world.launcherProjectileSpeed,
             world.terrainFrictionCoeff,
             world.objectFrictionCoeff,
             world.rollingFrictionCoeff,
             world.tornadoRadius,
             world.tornadoHeight,
             world.tornadoInwardAcceleration,
             world.tornadoSwirlAcceleration,
             world.tornadoLiftAcceleration,
             world.physicsSleepEnabled,
             world.tornadoEnabled,
             world.tornadoVisualShell,
             world.tornadoFieldVectors,
             world.rayCastVisualization };
}

UIProfilerTabFrameView InGameUIFrameData::ProfilerTabFrame() const
{
    return { diagnostics.profilerMarkerOptions,
             diagnostics.profilerMarkerOptionCount,
             surface.workerThreadCount,
             surface.maxWorkerThreadCount,
             surface.screenW,
             surface.screenH,
             surface.workerCoreTotalMs,
             surface.now };
}

UIMemoryTabFrameView InGameUIFrameData::MemoryTabFrame() const
{
    return { diagnostics.mainMemory,
             diagnostics.renderMemory,
             diagnostics.reserveCapacityRows,
             diagnostics.reserveGrowthEvents,
             diagnostics.reserveCapacityRowCount,
             diagnostics.reserveGrowthEventCount,
             surface.screenW,
             surface.screenH,
             diagnostics.replayMemoryPreset,
             diagnostics.replayMemoryRequestedRetentionSeconds,
             diagnostics.replayMemoryRequestedBudgetMiB,
             diagnostics.replayMemoryPresentationRetentionSeconds,
             diagnostics.replayMemorySolverRetentionSeconds,
             diagnostics.reserveGrowthEventTotalCount,
             diagnostics.reserveGrowthEventDroppedCount,
             surface.now,
             diagnostics.replayMemoryBudgetClamped,
             diagnostics.replayMemorySolverWindowReduced };
}

UISceneTabFrameView InGameUIFrameData::SceneTabFrame() const
{
    const OperatorEditorForecastView& source = operatorEditor.forecast;
    const UISceneForecastFrameView forecast = { source.simulatedSeconds,
                                                source.simulatedSecondsPerRealSecond,
                                                source.rollingWindowAgeSeconds,
                                                source.energyDrift,
                                                source.angularMomentumDrift,
                                                source.maximumAbsoluteEnergyDrift,
                                                source.maximumAngularMomentumDrift,
                                                source.firstFailureSeconds,
                                                source.firstFailureSubject,
                                                source.firstFailureOther,
                                                source.firstFailureCause,
                                                source.available,
                                                source.active,
                                                source.workerInFlight,
                                                source.failed,
                                                source.configured,
                                                source.numericalHealthy,
                                                source.systemOrbitalHealthy,
                                                source.auxiliaryOrbitalHealthy,
                                                source.energyDriftAvailable,
                                                source.angularMomentumDriftAvailable };

    return { forecast,
             surface.rendererName,
             scene.sceneOptions,
             scene.interactionRecordingOptions,
             scene.sceneOptionCount,
             scene.selectedSceneOption,
             scene.interactionRecordingOptionCount,
             scene.selectedInteractionRecordingOption,
             scene.currentFrame,
             scene.targetFrameCount,
             scene.modelCount,
             scene.currentSceneIndex,
             scene.sceneCount,
             surface.fps,
             scene.sceneEnergy,
             scene.timeScale,
             scene.predictionRevealRate,
             scene.fixedStep,
             scene.testComplete };
}

namespace
{
using InGameUIWidgets = UIWindowInteractionOwner::WidgetView;
using InGameUIChromeWidgets = UIWindowInteractionOwner::ChromeWidgetView;
using InGameUIFooterWidgets = UIWindowInteractionOwner::FooterWidgetView;
using InGameUIRenderWidgets = UIWindowInteractionOwner::RenderWidgetView;
using InGameUIDrawCatalog = UIWindowInteractionOwner::DrawCatalogView;
using InGameUITabWidgets = UIWindowInteractionOwner::TabWidgetView;
using InGameUIPointerState = UIWindowInteractionOwner::PointerDrawStateView;
using InGameUIMiniPaletteState = UIWindowInteractionOwner::EditorMiniPaletteView;

void PublishDrawStats( InGameUITab activeTab, const UIDrawList& frame, const UIDrawList& histogram,
                       const UIDrawList& memory )
{
    char drawStatsFlag[2] = {};
    size_t drawStatsFlagLength = 0;
    const bool requested = getenv_s( &drawStatsFlagLength, drawStatsFlag, sizeof( drawStatsFlag ), "SKORE_UI_DRAW_STATS" ) ==
                               0 &&
                           drawStatsFlag[0] != '\0';

    if ( !requested )
    {
        return;
    }

    const UIDrawList::Stats frameStats = frame.GetStats();
    const UIDrawList::Stats histogramStats = histogram.GetStats();
    const UIDrawList::Stats memoryStats = memory.GetStats();
    const auto overflowed = []( const UIDrawList::Stats& stats )
    { return stats.commandOverflow || stats.textOverflow || stats.clipOverflow; };
    const bool overflow = overflowed( frameStats ) || overflowed( histogramStats ) || overflowed( memoryStats );

    std::fprintf( stderr, "[ui-draw-stats] tab=%d frame=%d/%d histogram=%d/%d memory=%d/%d clip=%d/%d/%d overflow=%d\n",
                  static_cast<int>( activeTab ), frameStats.commandCount, frameStats.textBytes, histogramStats.commandCount,
                  histogramStats.textBytes, memoryStats.commandCount, memoryStats.textBytes, frameStats.maxClipDepth,
                  histogramStats.maxClipDepth, memoryStats.maxClipDepth, overflow ? 1 : 0 );
}

void AppendStandaloneOverlays( InGameUITabWidgets& tabs, const InGameUIFrameData& data, UIDrawList& frame,
                               UIDrawList& histogram, UIDrawList& memory, int screenW, int screenH, bool histogramEnabled,
                               bool memoryEnabled )
{
    if ( histogramEnabled )
    {
        histogram.Clear();
        const UIDrawContext draw( screenW, screenH, histogram );
        ProfilerTab::DrawPerformanceHistogram( tabs.profilerTab, draw, data.ProfilerTabFrame() );
        frame.Append( histogram );
    }

    if ( memoryEnabled )
    {
        // Why: allocator events are retained by memory level, not by sample
        // index. Place this overlay under the histogram when both are visible.
        memory.Clear();
        const UIDrawContext draw( screenW, screenH, memory );
        const float x = histogramEnabled ? tabs.profilerTab.histogramPanelX : 16.0f;
        const float y = histogramEnabled ? tabs.profilerTab.histogramPanelY + tabs.profilerTab.histogramPanelH + 8.0f
                                         : 16.0f;
        MemoryTab::DrawOverlay( tabs.memoryOverlay, draw, data.MemoryTabFrame(), x, y );
        frame.Append( memory );
    }
}

void DrawMinimizedContent( InGameUIChromeWidgets& chrome, InGameUIFooterWidgets& footer, InGameUIPointerState& pointer,
                           InGameUIMiniPaletteState& miniPalette, const InGameUIFrameData& data, UIDrawList& drawList,
                           int screenW, int screenH )
{
    drawList.Clear();
    const UIDrawContext draw( screenW, screenH, drawList );

    if ( chrome.window.animationActive && chrome.window.animationToMinimized )
    {
        Chrome::DrawWindowAnimationShell( draw, Chrome::CurrentWindowRect( chrome.window, data.surface.now ) );
        return;
    }

    char titleText[192] = {};
    BuildWindowTitle( data, titleText, sizeof( titleText ) );

    if ( !data.editor.editorModeEnabled )
    {
        StripMinimizedRuntimeModeSuffix( data, titleText, sizeof( titleText ) );
    }

    chrome.window.minimizedWidth = data.editor.editorModeEnabled
                                       ? EditorMinimizedWidth( data.EditorTabFrame(), screenW )
                                       : (std::min)( MinimizedWidthWithCameraModeCombo( titleText, screenW ),
                                                     MINIMIZED_RUN_MAX_W );
    const UIRect minimized = Layout::MinimizedRect( screenW, screenH, chrome.window.minimizedWidth );

    if ( data.editor.editorModeEnabled )
    {
        const EditorMiniPaletteLayout layout = BuildEditorMiniPaletteLayout( screenW, screenH, minimized,
                                                                             miniPalette.editorMiniPalettePressedEntry,
                                                                             miniPalette.editorMiniPaletteFlyoutOpen );
        DrawEditorMiniPalette( draw, layout, data.editor.editorObjectType, data.editor.editorPlaceStatic, pointer.mouseX,
                               pointer.mouseY, miniPalette.editorMiniPalettePressedTreePlacement,
                               miniPalette.editorMiniPalettePressedHoldMode, miniPalette.editorMiniPalettePressedEntry,
                               screenW, screenH );
        DrawEditorMinimizedWindow( draw, minimized, data.EditorTabFrame(), pointer.mouseX, pointer.mouseY );
    }
    else
    {
        const UIRect comboBounds = MinimizedCameraModeComboBounds( minimized );
        footer.cameraModeCombo.SetLabelVisible( false );
        footer.cameraModeCombo.SetBounds( comboBounds.x, comboBounds.y, comboBounds.w, comboBounds.h );
        footer.cameraModeCombo.SetDropUp( true );
        const float titleMaxW = (std::max)( 0.0f, comboBounds.x - ( minimized.x + 32.0f ) - MINIMIZED_CAMERA_MODE_GAP );

        if ( titleMaxW < UIFontMetrics::MeasureText( 12.5f, "..." ) )
        {
            titleText[0] = '\0';
        }
        else
        {
            Chrome::FitTitleText( titleText, sizeof( titleText ), 12.5f, titleMaxW );
        }

        Chrome::DrawMinimizedWindow( draw, minimized, titleText );
        const int cameraModeIndex = std::clamp( data.surface.cameraModeIndex, 0, CAMERA_MODE_OPTION_COUNT - 1 );
        const uint32_t disabledMask = ( ( 1u << CAMERA_MODE_OPTION_COUNT ) - 1u ) &
                                      ~( data.surface.cameraModeEnabledMask & ( ( 1u << CAMERA_MODE_OPTION_COUNT ) - 1u ) );
        footer.cameraModeCombo.Draw( draw, "",
                                     { std::span<const char* const>( kCameraModeOptions ), cameraModeIndex, disabledMask },
                                     { pointer.mouseX, pointer.mouseY } );
    }

    DrawEditorObjectCounter( draw, data, screenW, screenH );
}

void DrawRenderTabContent( InGameUIRenderWidgets& render, InGameUIPointerState& pointer, const InGameUIFrameData& data,
                           const UIDrawContext& draw, float contentX, float contentY, float contentW, float contentH,
                           float scrolledY )
{
    char buffer[128];
    const Style::UIPalette& palette = Style::Palette();
    const float colW = (std::max)( 148.0f, contentW * 0.46f );
    DrawSectionTitle( draw, contentX, contentY, contentH, scrolledY + 16.0f, 16.0f, "Render" );
    DrawContentToggle( draw, contentY, contentH, render.renderShadowToggle, contentX, scrolledY + UI_RENDER_FEATURE_START_Y,
                       colW, "Shadows", data.rendering.ordinaryRender.shadow.enabled );
    render.saveRenderDefaultsButton.SetBounds( contentX + contentW - UI_RENDER_SAVE_BUTTON_W,
                                               scrolledY + UI_RENDER_FEATURE_START_Y, UI_RENDER_SAVE_BUTTON_W, 24.0f );

    if ( IsRowVisible( contentY, contentH, scrolledY + UI_RENDER_FEATURE_START_Y, 24.0f ) )
    {
        render.saveRenderDefaultsButton.Draw( draw, "Save CFG", pointer.mouseX, pointer.mouseY );
    }

    static constexpr const char* labels[] = { "Main", "Reflection", "Terrain shadow", "Object shadow" };
    char visibilityText[96];

    for ( int viewIndex = 0; viewIndex < static_cast<int>( UIRenderVisibilityView::Count ); ++viewIndex )
    {
        const UIRenderVisibilityViewStats& visibility = data.surface.visibility.views[viewIndex];
        snprintf( visibilityText, sizeof( visibilityText ), "%d submitted, %d culled, %d draws", visibility.submitted,
                  visibility.culled, visibility.draws );
        DrawLabelValueAt( draw, contentY, contentH, contentX, scrolledY + 76.0f + static_cast<float>( viewIndex ) * 18.0f,
                          labels[viewIndex], visibilityText, palette.accent.r, palette.accent.g, palette.accent.b );
    }

    const float baseY = scrolledY + UI_RENDER_START_Y;

    for ( int index = 0; index < static_cast<int>( UIRenderParam::Count ); ++index )
    {
        const RenderSliderSpec& spec = kRenderSliderSpecs[index];
        const float sliderY = RenderSliderY( index, baseY );

        if ( RenderSliderStartsSection( index ) &&
             IsRowVisible( contentY, contentH, sliderY - UI_RENDER_SECTION_H + 4.0f, 18.0f ) )
        {
            DrawSectionTitle( draw, contentX, contentY, contentH, sliderY - UI_RENDER_SECTION_H + 4.0f, 12.0f,
                              UIRenderAuthoringSectionName( spec.section ) );

            if ( spec.section == UIRenderAuthoringSection::PredictionPaths )
            {
                render.saveTrajectoryStyleButton.SetBounds( contentX + contentW - UI_TRAJECTORY_SAVE_BUTTON_W,
                                                            sliderY - UI_RENDER_SECTION_H + 1.0f,
                                                            UI_TRAJECTORY_SAVE_BUTTON_W, 20.0f );
                render.saveTrajectoryStyleButton.Draw( draw, "Save Paths", pointer.mouseX, pointer.mouseY );
            }
        }

        const float value = std::clamp( RenderValueForParam( data.rendering.ordinaryRender, spec.param ), spec.minValue,
                                        spec.maxValue );
        snprintf( buffer, sizeof( buffer ), spec.valueFormat, value );
        render.renderSliders[index].SetBounds( contentX, sliderY, contentW, 34.0f );

        if ( IsRowVisible( contentY, contentH, sliderY, 34.0f ) )
        {
            render.renderSliders[index].Draw( draw, spec.label, buffer, value, spec.minValue, spec.maxValue );
        }
    }
}

void DrawTargetsTabContent( InGameUIRenderWidgets& render, InGameUIDrawCatalog& catalog, InGameUIPointerState& pointer,
                            const InGameUIFrameData& data, const UIDrawContext& draw, UIDrawList& drawList, float contentX,
                            float contentY, float contentW, float contentH, float scrolledY )
{
    const int targetCount = RenderTargetPreviewCount( data );
    const int selectedIndex = catalog.selectedRenderTargetPreview;
    const bool hasSelection = selectedIndex >= 0 && selectedIndex < targetCount;
    const UIRenderTargetPreviewResource* selected = hasSelection ? &data.renderTargets.previews[selectedIndex] : nullptr;
    const bool available = selected && selected->available && selected->width > 0 && selected->height > 0;
    const Style::UIPalette& palette = Style::Palette();
    const char* options[UI_RENDER_TARGET_PREVIEW_MAX] = {};
    int liveCount = 0;

    for ( int index = 0; index < targetCount; ++index )
    {
        const UIRenderTargetPreviewResource& resource = data.renderTargets.previews[index];
        options[index] = resource.label;
        liveCount += resource.available && resource.width > 0 && resource.height > 0 ? 1 : 0;
    }

    DrawSectionTitle( draw, contentX, contentY, contentH, scrolledY + 16.0f, 16.0f, "Targets" );
    char countText[64];
    snprintf( countText, sizeof( countText ), "%d / %d live", liveCount, targetCount );

    if ( IsRowVisible( contentY, contentH, scrolledY + UI_TARGETS_META_Y - 24.0f, 18.0f ) )
    {
        DrawLabelValueAt( draw, contentY, contentH, contentX, scrolledY + UI_TARGETS_META_Y - 24.0f, "Resources", countText,
                          palette.accent.r, palette.accent.g, palette.accent.b );
    }

    if ( selected )
    {
        char detailText[160];

        if ( available )
        {
            snprintf( detailText, sizeof( detailText ), "%s, %d x %d, #%d", RenderTargetPreviewTypeText( *selected ),
                      selected->width, selected->height, selectedIndex );
        }
        else
        {
            snprintf( detailText, sizeof( detailText ), "%s, n/a", RenderTargetPreviewTypeText( *selected ) );
        }

        DrawLabelValueAt( draw, contentY, contentH, contentX, scrolledY + UI_TARGETS_META_Y, "Selected", detailText,
                          available ? palette.textPrimary.r : palette.textMuted.r,
                          available ? palette.textPrimary.g : palette.textMuted.g,
                          available ? palette.textPrimary.b : palette.textMuted.b );
    }

    const UIRect previewPanel = { contentX, scrolledY + UI_TARGETS_PREVIEW_Y, contentW, UI_TARGETS_PREVIEW_H };
    const UIRect previewClip = { contentX, contentY, contentW, contentH };
    UIRect previewImage = previewPanel;

    if ( IsBlockVisible( contentY, contentH, previewPanel.y, previewPanel.h ) )
    {
        draw.RoundedPanel( previewPanel, Style::Radii().control, palette.windowSubtle, palette.innerBorder );
        const UIRect inset = { previewPanel.x + 10.0f, previewPanel.y + 10.0f, (std::max)( 1.0f, previewPanel.w - 20.0f ),
                               (std::max)( 1.0f, previewPanel.h - 20.0f ) };
        previewImage = selected ? FitRectToAspect( inset, selected->width, selected->height ) : inset;
        draw.RoundedRect( previewImage.x - 1.0f, previewImage.y - 1.0f, previewImage.w + 2.0f, previewImage.h + 2.0f,
                          Style::Radii().control, 0.01f, 0.015f, 0.018f, 0.92f );
    }

    if ( available && IsBlockVisible( contentY, contentH, previewImage.y, previewImage.h ) )
    {
        drawList.PushClip( previewClip );
        drawList.AddPreviewImage( { static_cast<uint16_t>( selectedIndex ), true }, previewImage, palette.windowSubtle,
                                  "Preview unavailable" );
        drawList.PopClip();
    }
    else if ( IsRowVisible( contentY, contentH, scrolledY + UI_TARGETS_PREVIEW_Y + 116.0f, 18.0f ) )
    {
        draw.Text( previewPanel.x + 18.0f, previewPanel.y + 116.0f, 12.0f, palette.textMuted.r, palette.textMuted.g,
                   palette.textMuted.b, "Not available this frame" );
    }

    if ( IsBlockVisible( contentY, contentH, previewPanel.y, previewPanel.h ) )
    {
        draw.Outline( previewImage.x, previewImage.y, previewImage.w, previewImage.h, palette.border.r, palette.border.g,
                      palette.border.b, 0.72f );
    }

    const char* selectedText = selected ? selected->label : "No targets";
    render.renderTargetCombo.SetBounds( contentX, scrolledY + UI_TARGETS_COMBO_Y, contentW, 24.0f );

    if ( IsRowVisible( contentY, contentH, scrolledY + UI_TARGETS_COMBO_Y, 24.0f ) )
    {
        render.renderTargetCombo.Draw( draw, "View",
                                       { std::span<const char* const>( options, static_cast<std::size_t>( targetCount ) ),
                                         selectedIndex, catalog.lastRenderTargetDisabledMask, selectedText },
                                       { pointer.mouseX, pointer.mouseY } );
    }
}

struct UIActiveTabDrawContext
{
    const UIDrawContext& draw;
    UIDrawList& drawList;
    UIRect content;
    float scrolledY = 0.0f;
};

bool DrawDiagnosticTabContent( InGameUITab activeTab, InGameUITabWidgets& tabs, InGameUIPointerState& pointer,
                               const InGameUIFrameData& data, const UIActiveTabDrawContext& context )
{
    const UIRect& content = context.content;
    switch ( activeTab )
    {
    case InGameUITab::Profiler:
        ProfilerTab::Draw( tabs.profilerTab, context.draw, data.ProfilerTabFrame(), content.x, content.y, content.w,
                           content.h, pointer.scrollY, pointer.activeSlider );
        return true;
    case InGameUITab::Memory:
        MemoryTab::Draw( context.draw, tabs.memoryOverlay, data.MemoryTabFrame(), content.x, content.y, content.w, content.h,
                         context.scrolledY, pointer.activeSlider, pointer.mouseX, pointer.mouseY );
        return true;
    case InGameUITab::Scene:
        SceneTab::Draw( tabs.sceneTab, context.draw, data.SceneTabFrame(), content.x, content.y, content.w, content.h,
                        context.scrolledY, pointer.mouseX, pointer.mouseY );
        return true;
    case InGameUITab::Physics:
        PhysicsTab::Draw( tabs.physicsTab, context.draw, data.PhysicsTabFrame(), content.x, content.y, content.w, content.h,
                          context.scrolledY, pointer.activeSlider, pointer.mouseX, pointer.mouseY );
        return true;
    case InGameUITab::Editor:
        EditorTab::Draw( tabs.editorTab, context.draw, data.EditorTabFrame(), content.x, content.y, content.w, content.h,
                         context.scrolledY, pointer.mouseX, pointer.mouseY );
        return true;
    case InGameUITab::Options:
        OptionsTab::Draw( tabs.optionsTab, context.draw, data.OptionsTabFrame(), content.x, content.y, content.w, content.h,
                          context.scrolledY, pointer.activeSlider );
        return true;
    default:
        return false;
    }
}

void DrawAuthoringTabContent( InGameUITab activeTab, InGameUITabWidgets& tabs, InGameUIRenderWidgets& render,
                              InGameUIDrawCatalog& catalog, InGameUIPointerState& pointer, const InGameUIFrameData& data,
                              const UIActiveTabDrawContext& context )
{
    const UIRect& content = context.content;
    switch ( activeTab )
    {
    case InGameUITab::Render:
        DrawRenderTabContent( render, pointer, data, context.draw, content.x, content.y, content.w, content.h,
                              context.scrolledY );
        break;
    case InGameUITab::Targets:
        DrawTargetsTabContent( render, catalog, pointer, data, context.draw, context.drawList, content.x, content.y,
                               content.w, content.h, context.scrolledY );
        break;
    case InGameUITab::Sky:
        SkyTab::Draw( tabs.skyTab, context.draw, data.rendering.cinematic, content.x, content.y, content.w, content.h,
                      context.scrolledY, pointer.mouseX, pointer.mouseY );
        break;
    case InGameUITab::Cinematic:
        CinematicTab::Draw( tabs.cinematicTab, context.draw, data.CinematicTabFrame(), content.x, content.y, content.w,
                            content.h, context.scrolledY, pointer.mouseX, pointer.mouseY );
        break;
    case InGameUITab::Keys:
        ControlsTab::Draw( tabs.controlsTab, context.draw, data.ControlsTabFrame(), content.x, content.y, content.w,
                           content.h, context.scrolledY );
        break;
    default:
        break;
    }
}

void DrawActiveTabContent( InGameUITab activeTab, InGameUITabWidgets& tabs, InGameUIRenderWidgets& render,
                           InGameUIDrawCatalog& catalog, InGameUIPointerState& pointer, const InGameUIFrameData& data,
                           const UIActiveTabDrawContext& context )
{
    if ( !DrawDiagnosticTabContent( activeTab, tabs, pointer, data, context ) )
    {
        DrawAuthoringTabContent( activeTab, tabs, render, catalog, pointer, data, context );
    }
}

struct UIFooterDrawContext
{
    const UIDrawContext& draw;
    float x = 0.0f;
    float y = 0.0f;
    float width = 0.0f;
    float height = 0.0f;
    float bottomHeight = 0.0f;
    float titleStatWidth = 0.0f;
    float titleStatX = 0.0f;
    const char* titleStat = "";
};

struct UIFooterGeometry
{
    float footerY = 0.0f;
    float footerX = 0.0f;
    float footerWidth = 0.0f;
    float controlsWidth = 0.0f;
    bool hasSeparateStats = false;
};

UIRect DrawFooterControls( InGameUIChromeWidgets& chrome, InGameUIFooterWidgets& footer, InGameUITabWidgets& tabs,
                           InGameUIPointerState& pointer, const InGameUIFrameData& data, const UIFooterDrawContext& context,
                           const UIFooterGeometry& geometry )
{
    const UIRect controlsBounds = { geometry.footerX, geometry.footerY + 16.0f, geometry.controlsWidth, 56.0f };
    context.draw.RoundedPanel( controlsBounds, Style::Radii().control, Style::Palette().windowSubtle,
                               Style::Palette().innerBorder );
    const UIRect rendererBounds = FooterRendererComboBounds( context.x, geometry.footerY );
    const UIRect waterBounds = FooterWaterComboBounds( context.x, geometry.footerY );
    const UIRect blurBounds = FooterBlurBounds( context.x, geometry.footerY );
    const UIRect vsyncBounds = FooterVsyncBounds( context.x, geometry.footerY );
    const UIRect hitboxBounds = FooterHitboxBounds( context.x, geometry.footerY );
    const UIRect timelineBounds = FooterTimelineBounds( context.x, geometry.footerY );
    const UIRect performanceBounds = FooterPerfBounds( context.x, geometry.footerY );
    footer.rendererCombo.SetBounds( rendererBounds.x, rendererBounds.y, rendererBounds.w, rendererBounds.h );
    footer.rendererCombo.SetDropUp( true );
    footer.reflectionCombo.SetBounds( waterBounds.x, waterBounds.y, waterBounds.w, waterBounds.h );
    footer.reflectionCombo.SetDropUp( true );
    footer.blurToggle.SetBounds( blurBounds.x, blurBounds.y, blurBounds.w, blurBounds.h );
    footer.vsyncToggle.SetBounds( vsyncBounds.x, vsyncBounds.y, vsyncBounds.w, vsyncBounds.h );
    footer.hitboxToggle.SetBounds( hitboxBounds.x, hitboxBounds.y, hitboxBounds.w, hitboxBounds.h );
    footer.histogramToggle.SetBounds( performanceBounds.x, performanceBounds.y, performanceBounds.w, performanceBounds.h );
    footer.timelineToggle.SetBounds( timelineBounds.x, timelineBounds.y, timelineBounds.w, timelineBounds.h );

    static const char* rendererOptions[] = { "DX12" };
    static const char* reflectionOptions[] = { "FBO", "DXR", "None" };
    footer.rendererCombo.Draw( context.draw, "Renderer", { std::span<const char* const>( rendererOptions ), 0 },
                               { pointer.mouseX, pointer.mouseY } );
    DrawFooterToggle( context.draw, blurBounds, "Blur", chrome.blurPreviewEnabled );
    DrawFooterToggle( context.draw, vsyncBounds, "VSync", data.surface.vsyncEnabled );
    DrawFooterToggle( context.draw, hitboxBounds, "Hitboxes", chrome.hitboxOverlayEnabled );
    DrawFooterToggle( context.draw, performanceBounds, "Perf",
                      ProfilerTab::PerformanceHistogramEnabled( tabs.profilerTab ) );
    DrawFooterToggle( context.draw, timelineBounds, "Timeline", ProfilerTab::TimelineEnabled( tabs.profilerTab ) );
    footer.reflectionCombo.Draw( context.draw, "Water",
                                 { std::span<const char* const>( reflectionOptions ), WaterReflectionModeFromData( data ),
                                   ReflectionDisabledMask() },
                                 { pointer.mouseX, pointer.mouseY } );
    return controlsBounds;
}

void DrawWideFooterStats( const InGameUIFrameData& data, const UIFooterDrawContext& context,
                          const UIFooterGeometry& geometry, float statsX, float statsWidth )
{
    const Style::UIPalette& palette = Style::Palette();
    char status[128];
    const float frameDisplayMs = data.surface.fps > 0.0f ? 1000.0f / data.surface.fps : 0.0f;
    const int cpuPercent = static_cast<int>(
        std::clamp( ( data.surface.renderMs + data.surface.physicsMs ) / 16.67f * 100.0f, 0.0f, 99.0f ) );
    const int gpuPercent = static_cast<int>( std::clamp( data.surface.renderMs / 16.67f * 100.0f, 0.0f, 99.0f ) );
    const int drawCalls = data.surface.drawCallsBeforeUI + data.surface.UIDrawCalls;
    snprintf( status, sizeof( status ), "%.0f", data.surface.fps );
    DrawFooterStatCell( context.draw, statsX + 18.0f, geometry.footerY, "FPS", status, palette.accent.r, palette.accent.g,
                        palette.accent.b );
    DrawFooterStatDivider( context.draw, statsX + 78.0f, geometry.footerY );
    snprintf( status, sizeof( status ), "%.2f ms", frameDisplayMs );
    DrawFooterStatCell( context.draw, statsX + 100.0f, geometry.footerY, "Frame Time", status, palette.textPrimary.r,
                        palette.textPrimary.g, palette.textPrimary.b );
    DrawFooterStatDivider( context.draw, statsX + 190.0f, geometry.footerY );
    snprintf( status, sizeof( status ), "%d%%", cpuPercent );
    DrawFooterStatCell( context.draw, statsX + 212.0f, geometry.footerY, "CPU", status, palette.accent.r, palette.accent.g,
                        palette.accent.b );
    DrawFooterStatDivider( context.draw, statsX + 266.0f, geometry.footerY );
    snprintf( status, sizeof( status ), "%d%%", gpuPercent );
    DrawFooterStatCell( context.draw, statsX + 288.0f, geometry.footerY, "GPU", status, palette.accent.r, palette.accent.g,
                        palette.accent.b );
    DrawFooterStatDivider( context.draw, statsX + 342.0f, geometry.footerY );
    snprintf( status, sizeof( status ), "%d / %d", drawCalls, data.surface.UIDrawCalls );
    DrawFooterStatCell( context.draw, statsX + statsWidth - 112.0f, geometry.footerY, "Draws / UI", status,
                        palette.textPrimary.r, palette.textPrimary.g, palette.textPrimary.b );
}

void DrawFooterStats( const InGameUIFrameData& data, const UIFooterDrawContext& context, const UIFooterGeometry& geometry )
{
    const Style::UIPalette& palette = Style::Palette();
    if ( !geometry.hasSeparateStats )
    {
        if ( context.titleStatWidth > 0.0f &&
             context.titleStatX + context.titleStatWidth < context.x + context.width - 116.0f )
        {
            context.draw.Text( context.titleStatX, context.y + 17.0f, 10.5f, palette.accent.r, palette.accent.g,
                               palette.accent.b, context.titleStat );
        }
        return;
    }

    const float statsX = geometry.footerX + geometry.controlsWidth + 16.0f;
    const float statsWidth = (std::max)( 120.0f, context.x + context.width - 18.0f - statsX );
    context.draw.RoundedPanel( { statsX, geometry.footerY + 16.0f, statsWidth, 56.0f }, Style::Radii().control,
                               palette.windowSubtle, palette.innerBorder );
    if ( statsWidth < 350.0f )
    {
        char fpsText[32];
        char frameText[32];
        char drawText[32];
        snprintf( fpsText, sizeof( fpsText ), "%.0f", data.surface.fps );
        snprintf( frameText, sizeof( frameText ), "%.2f ms", data.surface.fps > 0.0f ? 1000.0f / data.surface.fps : 0.0f );
        snprintf( drawText, sizeof( drawText ), "%d/%d", data.surface.drawCallsBeforeUI + data.surface.UIDrawCalls,
                  data.surface.UIDrawCalls );
        DrawCompactFooterStat( context.draw, statsX, geometry.footerY + 23.0f, "FPS", fpsText, palette.accent.r,
                               palette.accent.g, palette.accent.b );
        DrawCompactFooterStat( context.draw, statsX, geometry.footerY + 41.0f, "Frame", frameText, palette.textPrimary.r,
                               palette.textPrimary.g, palette.textPrimary.b );
        DrawCompactFooterStat( context.draw, statsX, geometry.footerY + 59.0f, "Draw/UI", drawText, palette.textPrimary.r,
                               palette.textPrimary.g, palette.textPrimary.b );
        return;
    }
    DrawWideFooterStats( data, context, geometry, statsX, statsWidth );
}

UIRect DrawFooterContent( InGameUIChromeWidgets& chrome, InGameUIFooterWidgets& footer, InGameUITabWidgets& tabs,
                          InGameUIPointerState& pointer, const InGameUIFrameData& data, const UIFooterDrawContext& context )
{
    const Style::UIPalette& palette = Style::Palette();
    UIFooterGeometry geometry;
    geometry.footerY = context.y + context.height - context.bottomHeight;
    geometry.footerX = context.x + 18.0f;
    geometry.footerWidth = (std::max)( 120.0f, context.width - 36.0f );
    geometry.hasSeparateStats = geometry.footerWidth >= 560.0f;
    geometry.controlsWidth = geometry.hasSeparateStats ? 462.0f : geometry.footerWidth;
    context.draw.Rect( context.x + 16.0f, geometry.footerY, context.width - 32.0f, 1.0f, palette.lineSoft.r,
                       palette.lineSoft.g, palette.lineSoft.b, 0.14f );
    const UIRect controlsBounds = DrawFooterControls( chrome, footer, tabs, pointer, data, context, geometry );
    DrawFooterStats( data, context, geometry );
    context.draw.Rect( context.x + context.width - 24.0f, context.y + context.height - 9.0f, 14.0f, 2.0f,
                       palette.textMuted.r, palette.textMuted.g, palette.textMuted.b, 0.58f );
    context.draw.Rect( context.x + context.width - 18.0f, context.y + context.height - 15.0f, 8.0f, 2.0f,
                       palette.textMuted.r, palette.textMuted.g, palette.textMuted.b, 0.46f );
    context.draw.Rect( context.x + context.width - 12.0f, context.y + context.height - 21.0f, 2.0f, 2.0f,
                       palette.textMuted.r, palette.textMuted.g, palette.textMuted.b, 0.38f );
    return controlsBounds;
}
} // namespace


bool InGameUI::IsVisible() const
{
    return m_windowInteraction.IsVisible();
}
bool InGameUI::IsMinimized() const
{
    return m_windowInteraction.IsMinimized();
}
void InGameUI::SetVisible( bool visible, double now )
{
    m_windowInteraction.SetVisible( visible, now );
}
void InGameUI::ToggleVisible( double now )
{
    m_windowInteraction.ToggleVisible( now );
}
void InGameUI::SetMinimized( bool minimized, double now )
{
    m_windowInteraction.SetMinimized( minimized, now );
}
void InGameUI::SetActiveTab( InGameUITab tab )
{
    m_windowInteraction.SetActiveTab( tab );
}
InGameUITab InGameUI::GetActiveTab() const
{
    return m_windowInteraction.GetActiveTab();
}
void InGameUI::CancelInputCapture()
{
    m_windowInteraction.CancelInputCapture();
}
bool InGameUI::BlocksCameraMouse() const
{
    return m_windowInteraction.BlocksCameraMouse();
}
bool InGameUI::BlocksKeyboard() const
{
    return m_windowInteraction.BlocksKeyboard();
}
bool InGameUI::WantsNativeMouseCursor() const
{
    return m_windowInteraction.WantsNativeMouseCursor();
}
void InGameUI::SetWindowBounds( int x, int y, int width, int height )
{
    m_windowInteraction.SetWindowBounds( x, y, width, height );
}
bool InGameUI::CaptureInteractionAnchor( int clientX, int clientY, char* output, std::size_t outputSize ) const
{
    return m_windowInteraction.CaptureInteractionAnchor( clientX, clientY, output, outputSize );
}
bool InGameUI::ResolveInteractionAnchor( const char* anchor, int& clientX, int& clientY ) const
{
    return m_windowInteraction.ResolveInteractionAnchor( anchor, clientX, clientY );
}
void InGameUI::SetBlurEnabled( bool enabled )
{
    m_windowInteraction.SetBlurEnabled( enabled );
}
void InGameUI::SetRendererComboOpen( bool open )
{
    m_windowInteraction.SetRendererComboOpen( open );
}
void InGameUI::SetWaterComboOpen( bool open )
{
    m_windowInteraction.SetWaterComboOpen( open );
}
void InGameUI::SetSceneComboOpen( bool open )
{
    m_windowInteraction.SetSceneComboOpen( open );
}
void InGameUI::SetSceneFilter( const char* filter )
{
    m_windowInteraction.SetSceneFilter( filter );
}
void InGameUI::SetProfilerExpandAll( bool expandAll )
{
    m_windowInteraction.SetProfilerExpandAll( expandAll );
}
void InGameUI::SetProfilerTimelineEnabled( bool enabled )
{
    m_windowInteraction.SetProfilerTimelineEnabled( enabled );
}
void InGameUI::SetPerformanceHistogramEnabled( bool enabled )
{
    m_windowInteraction.SetPerformanceHistogramEnabled( enabled );
}
void InGameUI::TogglePerformanceHistogramEnabled()
{
    m_windowInteraction.TogglePerformanceHistogramEnabled();
}
bool InGameUI::IsMemoryOverlayEnabled() const
{
    return m_windowInteraction.IsMemoryOverlayEnabled();
}
void InGameUI::ToggleMemoryOverlayEnabled()
{
    m_windowInteraction.ToggleMemoryOverlayEnabled();
}
bool InGameUI::NeedsUiTextPass() const
{
    return m_windowInteraction.NeedsUiTextPass();
}
void InGameUI::SetHitboxOverlayEnabled( bool enabled )
{
    m_windowInteraction.SetHitboxOverlayEnabled( enabled );
}
void InGameUI::SetScrollY( float scrollY )
{
    m_windowInteraction.SetScrollY( scrollY );
}
void InGameUI::SetMouseOverride( bool enabled, int x, int y )
{
    m_windowInteraction.SetMouseOverride( enabled, x, y );
}
void InGameUI::ResetPresentationState()
{
    m_windowInteraction.ResetPresentationResources();
}
void InGameUI::DrawHitboxOverlay( const UIDrawContext& draw, const InGameUIFrameData& data,
                                  UIWindowInteractionOwner::WidgetView& widgets, const UIRect& windowBounds,
                                  const UIRect& contentBounds, const UIRect& footerBounds )
{
    const UICinematicTabFrameView cinematicTabFrame = data.CinematicTabFrame();

    if ( !widgets.chrome.hitboxOverlayEnabled )
    {
        return;
    }

    constexpr float chromeR = 0.16f;
    constexpr float chromeG = 0.86f;
    constexpr float chromeB = 1.00f;
    constexpr float contentR = 0.30f;
    constexpr float contentG = 1.00f;
    constexpr float contentB = 0.42f;
    constexpr float footerR = 1.00f;
    constexpr float footerG = 0.22f;
    constexpr float footerB = 0.82f;
    constexpr float buttonR = 1.00f;
    constexpr float buttonG = 0.62f;
    constexpr float buttonB = 0.18f;

    DrawHitboxRect( draw, windowBounds, chromeR, chromeG, chromeB, 0.018f, 0.44f );

    const Chrome::TitleButtonRects titleButtons = Chrome::GetTitleButtonRects( windowBounds );
    DrawHitboxRect( draw, titleButtons.minimize, chromeR, chromeG, chromeB, 0.050f, 0.86f );
    DrawHitboxRect( draw, titleButtons.maximize, chromeR, chromeG, chromeB, 0.050f, 0.86f );
    DrawHitboxRect( draw, titleButtons.close, chromeR, chromeG, chromeB, 0.050f, 0.86f );

    if ( !widgets.chrome.window.isMaximized )
    {
        DrawHitboxRect( draw,
                        { windowBounds.x + windowBounds.w - 26.0f, windowBounds.y + windowBounds.h - 26.0f, 26.0f, 26.0f },
                        chromeR, chromeG, chromeB, 0.050f, 0.86f );
    }

    DrawTabHitboxes( draw, widgets.chrome.tabBar, static_cast<int>( InGameUITab::Count ) );
    DrawHitboxRect( draw, contentBounds, contentR, contentG, contentB, 0.018f, 0.48f );

    switch ( widgets.chrome.activeTab )
    {
    case InGameUITab::Scene:
        DrawComboHitboxes( draw, widgets.tabs.sceneTab.combo,
                           SceneDropdownHitboxOptionCount( widgets.tabs.sceneTab, data.SceneTabFrame() ), contentR, contentG,
                           contentB );

        DrawHitboxRect( draw, widgets.tabs.sceneTab.resetSceneButton.Bounds(), buttonR, buttonG, buttonB );
        DrawHitboxRect( draw, widgets.tabs.sceneTab.resetDefaultsButton.Bounds(), buttonR, buttonG, buttonB );
        DrawHitboxRect( draw, widgets.tabs.sceneTab.saveDefaultsButton.Bounds(), buttonR, buttonG, buttonB );
        DrawHitboxRect( draw, widgets.tabs.sceneTab.timeScaleSlider.Bounds(), contentR, contentG, contentB );
        break;
    case InGameUITab::Editor:
        DrawHitboxRect( draw, widgets.tabs.editorTab.editorModeToggle.Bounds(), contentR, contentG, contentB );
        DrawHitboxRect( draw, widgets.tabs.editorTab.placementModeToggle.Bounds(), contentR, contentG, contentB );
        DrawHitboxRect( draw, widgets.tabs.editorTab.staticObjectToggle.Bounds(), contentR, contentG, contentB );
        DrawComboHitboxes( draw, widgets.tabs.editorTab.objectCombo, EditorTab::OBJECT_TYPE_COUNT, contentR, contentG,
                           contentB );

        break;
    case InGameUITab::Physics:

        for ( int i = 0; i < 13; ++i )
        {
            DrawHitboxRect( draw, widgets.tabs.physicsTab.toggles[i].Bounds(), contentR, contentG, contentB );
        }

        DrawHitboxRect( draw, widgets.tabs.physicsTab.pipelinePrevButton, buttonR, buttonG, buttonB );
        DrawHitboxRect( draw, widgets.tabs.physicsTab.pipelineNextButton, buttonR, buttonG, buttonB );
        DrawHitboxRect( draw, widgets.tabs.physicsTab.alphaSlider.Bounds(), contentR, contentG, contentB );
        DrawHitboxRect( draw, widgets.tabs.physicsTab.contactLingerSlider.Bounds(), contentR, contentG, contentB );
        DrawHitboxRect( draw, widgets.tabs.physicsTab.rayImpulseSlider.Bounds(), contentR, contentG, contentB );
        DrawHitboxRect( draw, widgets.tabs.physicsTab.launcherProjectileSpeedSlider.Bounds(), contentR, contentG, contentB );
        DrawHitboxRect( draw, widgets.tabs.physicsTab.worldGravitySlider.Bounds(), contentR, contentG, contentB );
        DrawHitboxRect( draw, widgets.tabs.physicsTab.terrainFrictionSlider.Bounds(), contentR, contentG, contentB );
        DrawHitboxRect( draw, widgets.tabs.physicsTab.objectFrictionSlider.Bounds(), contentR, contentG, contentB );
        DrawHitboxRect( draw, widgets.tabs.physicsTab.rollingFrictionSlider.Bounds(), contentR, contentG, contentB );
        DrawHitboxRect( draw, widgets.tabs.physicsTab.tornadoRadiusSlider.Bounds(), contentR, contentG, contentB );
        DrawHitboxRect( draw, widgets.tabs.physicsTab.tornadoHeightSlider.Bounds(), contentR, contentG, contentB );
        DrawHitboxRect( draw, widgets.tabs.physicsTab.tornadoInwardSlider.Bounds(), contentR, contentG, contentB );
        DrawHitboxRect( draw, widgets.tabs.physicsTab.tornadoSwirlSlider.Bounds(), contentR, contentG, contentB );
        DrawHitboxRect( draw, widgets.tabs.physicsTab.tornadoLiftSlider.Bounds(), contentR, contentG, contentB );
        break;
    case InGameUITab::Options:

        for ( int i = 0; i < 6; ++i )
        {
            DrawHitboxRect( draw, widgets.tabs.optionsTab.toggles[i].Bounds(), contentR, contentG, contentB );
        }

        DrawHitboxRect( draw, widgets.tabs.optionsTab.timeScaleSlider.Bounds(), contentR, contentG, contentB );
        DrawHitboxRect( draw, widgets.tabs.optionsTab.modelCountSlider.Bounds(), contentR, contentG, contentB );
        break;
    case InGameUITab::Render:
        DrawHitboxRect( draw, widgets.render.renderShadowToggle.Bounds(), contentR, contentG, contentB );
        DrawHitboxRect( draw, widgets.render.saveRenderDefaultsButton.Bounds(), contentR, contentG, contentB );

        for ( int i = 0; i < static_cast<int>( UIRenderParam::Count ); ++i )
        {
            DrawHitboxRect( draw, widgets.render.renderSliders[i].Bounds(), contentR, contentG, contentB );
        }

        break;
    case InGameUITab::Targets:
        DrawComboHitboxes( draw, widgets.render.renderTargetCombo, widgets.catalog.lastRenderTargetPreviewCount, contentR,
                           contentG, contentB );

        break;
    case InGameUITab::Keys:
        DrawHitboxRect( draw, widgets.tabs.controlsTab.seedSlider.Bounds(), contentR, contentG, contentB );
        DrawHitboxRect( draw, widgets.tabs.controlsTab.solverBallSlider.Bounds(), contentR, contentG, contentB );
        DrawHitboxRect( draw, widgets.tabs.controlsTab.solverBoxSlider.Bounds(), contentR, contentG, contentB );
        DrawHitboxRect( draw, widgets.tabs.controlsTab.worldFluidHeightSlider.Bounds(), contentR, contentG, contentB );
        DrawHitboxRect( draw, widgets.tabs.controlsTab.worldFluidDensitySlider.Bounds(), contentR, contentG, contentB );
        break;
    case InGameUITab::Sky:
        SkyTab::DrawHitboxes( widgets.tabs.skyTab, draw, contentR, contentG, contentB );
        break;
    case InGameUITab::Cinematic:
        CinematicTab::DrawHitboxes( widgets.tabs.cinematicTab, draw, cinematicTabFrame, contentR, contentG, contentB );
        break;
    case InGameUITab::Profiler:
        DrawHitboxRect( draw, widgets.tabs.profilerTab.workerToggle.Bounds(), contentR, contentG, contentB );
        DrawHitboxRect( draw, widgets.tabs.profilerTab.workerThreadSlider.Bounds(), contentR, contentG, contentB );
        break;
    case InGameUITab::Memory:
        break;
    default:
        break;
    }

    if ( m_windowInteraction.ContentHeight() > static_cast<int>( contentBounds.h ) )
    {
        DrawHitboxRect( draw, widgets.chrome.scrollBar.Bounds(), 0.18f, 0.82f, 0.95f, 0.060f, 0.86f );
    }

    DrawHitboxRect( draw, footerBounds, footerR, footerG, footerB, 0.020f, 0.54f );
    DrawComboHitboxes( draw, widgets.footer.rendererCombo, 1, footerR, footerG, footerB );
    DrawComboHitboxes( draw, widgets.footer.reflectionCombo, 3, footerR, footerG, footerB );
    DrawHitboxRect( draw, widgets.footer.blurToggle.Bounds(), footerR, footerG, footerB );
    DrawHitboxRect( draw, widgets.footer.vsyncToggle.Bounds(), footerR, footerG, footerB );
    DrawHitboxRect( draw, widgets.footer.histogramToggle.Bounds(), footerR, footerG, footerB );
    DrawHitboxRect( draw, widgets.footer.timelineToggle.Bounds(), footerR, footerG, footerB );
    DrawHitboxRect( draw, widgets.footer.hitboxToggle.Bounds(), footerR, footerG, footerB );
}


InputControl::UIPointerOverride InGameUI::InputOverride() const
{
    return m_windowInteraction.InputOverride();
}


InGameUIInputResult InGameUI::UpdateInput( const InputControl::UIInputSnapshot& input, const UIInputFrameFacts& frame,
                                           const UIEditorModeFacts& editor, const UICameraModeFacts& camera )
{
    PROFILE_SCOPED( "Frame/UI/Input" );
    return m_windowInteraction.UpdateInput( input, frame, editor, camera, m_sceneNavigation );
}
const UIDrawList& InGameUI::Draw( const InGameUIFrameData& data )
{
    m_frameDrawList.Clear();
    m_histogramDrawList.Clear();
    m_memoryOverlayDrawList.Clear();
    auto widgets = m_windowInteraction.Widgets();
    const bool histogramEnabled = ProfilerTab::PerformanceHistogramEnabled( widgets.tabs.profilerTab );
    const bool memoryOverlayEnabled = MemoryTab::OverlayEnabled( widgets.tabs.memoryOverlay );
    const auto finishDraw = [&]() -> const UIDrawList&
    {
        // Why: every exit path must publish capacity evidence. Hidden,
        // minimized, and cached frames are real retained-stream consumers too.
        PublishDrawStats( widgets.chrome.activeTab, m_frameDrawList, m_histogramDrawList, m_memoryOverlayDrawList );
        return m_frameDrawList;
    };

    // Why: input handling runs before the next draw, so the profiler tab keeps a
    // bounded copy of the latest frame snapshot for content height and hit tests.
    ProfilerTab::SetFrameSnapshot( widgets.tabs.profilerTab, data.diagnostics.profiler );

    if ( !widgets.chrome.window.isVisible && !histogramEnabled && !memoryOverlayEnabled )
    {
        return finishDraw();
    }

    const int screenW = (std::max)( 1, data.surface.screenW );
    const int screenH = (std::max)( 1, data.surface.screenH );
    widgets.catalog.lastScreenW = screenW;
    widgets.catalog.lastScreenH = screenH;
    widgets.catalog.lastModelCapacity = std::clamp( data.scene.modelCapacity, 1,
                                                    SkullbonezCore::Scene::Capacity::MAX_SCENE_OBJECTS );
    widgets.catalog.lastSolverBallCount = std::clamp( data.scene.solverBallCount, UI_SOLVER_COUNT_MIN,
                                                      widgets.catalog.lastModelCapacity );
    widgets.catalog.lastSolverBoxCount = std::clamp( data.scene.solverBoxCount, UI_SOLVER_COUNT_MIN,
                                                     widgets.catalog.lastModelCapacity );
    widgets.catalog.lastMaxWorkerThreadCount = (std::max)( 1, data.surface.maxWorkerThreadCount );
    widgets.catalog.lastWorkerThreadCount = std::clamp( data.surface.workerThreadCount, 0,
                                                        widgets.catalog.lastMaxWorkerThreadCount );
    widgets.catalog.lastRenderTargetPreviewCount = RenderTargetPreviewCount( data );
    widgets.catalog.lastRenderTargetDisabledMask = RenderTargetPreviewDisabledMask( data );
    widgets.catalog
        .selectedRenderTargetPreview = ResolveRenderTargetPreviewSelection( data,
                                                                            widgets.catalog.selectedRenderTargetPreview );

    if ( histogramEnabled )
    {
        ProfilerTab::PushPerformanceHistogramSample( widgets.tabs.profilerTab, data.ProfilerTabFrame() );
    }

    if ( memoryOverlayEnabled )
    {
        MemoryTab::PushOverlayFrame( widgets.tabs.memoryOverlay, data.MemoryTabFrame() );
    }

    if ( !widgets.chrome.window.isVisible )
    {
        AppendStandaloneOverlays( widgets.tabs, data, m_frameDrawList, m_histogramDrawList, m_memoryOverlayDrawList, screenW,
                                  screenH, histogramEnabled, memoryOverlayEnabled );
        return finishDraw();
    }

    if ( widgets.chrome.window.isMinimized )
    {
        widgets.chrome.cache.Reset();
        UIDrawList& drawList = widgets.chrome.cache.MutableDrawList();
        DrawMinimizedContent( widgets.chrome, widgets.footer, widgets.pointer, widgets.miniPalette, data, drawList, screenW,
                              screenH );
        m_frameDrawList.Append( drawList );
        AppendStandaloneOverlays( widgets.tabs, data, m_frameDrawList, m_histogramDrawList, m_memoryOverlayDrawList, screenW,
                                  screenH, histogramEnabled, memoryOverlayEnabled );
        return finishDraw();
    }

    m_windowInteraction.PrepareForDraw( data.surface.now );

    PROFILE_BEGIN( "Frame/UI/Layout" );
    const UIRect windowBounds = Chrome::CurrentWindowRect( widgets.chrome.window, data.surface.now );
    const float x = windowBounds.x;
    const float y = windowBounds.y;
    const float w = windowBounds.w;
    const float h = windowBounds.h;
    const float titleH = 44.0f;
    const float tabH = 44.0f;
    const float bottomH = 78.0f;
    const float pad = 18.0f;
    const float contentX = x + pad;
    const float contentY = y + titleH + tabH + 12.0f;
    const float contentW = w - pad * 2.0f - 8.0f;
    const float contentH = (std::max)( 30.0f, h - titleH - tabH - bottomH - pad );
    const float scrolledY = contentY - widgets.pointer.scrollY;
    char titleText[192] = {};

    BuildWindowTitle( data, titleText, sizeof( titleText ) );
    const bool useTitleStats = w - 36.0f < 560.0f;
    char titleStat[32] = {};

    float titleStatW = 0.0f;
    float titleStatX = 0.0f;
    float titleMaxW = w - 150.0f;

    if ( useTitleStats )
    {
        snprintf( titleStat, sizeof( titleStat ), "%.0f FPS", data.surface.fps );
        titleStatW = UIFontMetrics::MeasureText( 10.5f, titleStat );
        titleStatX = (std::max)( x + 148.0f, x + w - 128.0f - titleStatW );
        titleMaxW = titleStatX - ( x + 20.0f ) - 10.0f;
    }

    Chrome::FitTitleText( titleText, sizeof( titleText ), 15.5f, (std::max)( 40.0f, titleMaxW ) );
    ProfilerTab::ApplyDefaultExpansion( widgets.tabs.profilerTab );
    ProfilerTab::ApplyExpandAll( widgets.tabs.profilerTab );

    UICacheFrameKey cacheKey;
    cacheKey.screenW = screenW;
    cacheKey.screenH = screenH;
    cacheKey.windowBounds = windowBounds;
    cacheKey.activeTab = static_cast<int>( widgets.chrome.activeTab );
    cacheKey.scrollY = widgets.pointer.scrollY;
    cacheKey.blurEnabled = widgets.chrome.blurPreviewEnabled;
    cacheKey.contentSignature = BuildUIContentSignature( data );
    cacheKey.styleSignature = HashBool( HashBool( 2166136261u, widgets.chrome.blurPreviewEnabled ),
                                        widgets.chrome.hitboxOverlayEnabled );

    uint32_t openControls = 0u;
    openControls |= widgets.footer.rendererCombo.IsOpen() ? UI_INTERACTION_RENDERER_OPEN : 0u;
    openControls |= widgets.footer.reflectionCombo.IsOpen() ? UI_INTERACTION_REFLECTION_OPEN : 0u;
    openControls |= widgets.tabs.sceneTab.combo.IsOpen() ? UI_INTERACTION_SCENE_OPEN : 0u;
    openControls |= CinematicTab::IsComboOpen( widgets.tabs.cinematicTab ) ? UI_INTERACTION_CINEMATIC_SCENE_OPEN : 0u;
    openControls |= widgets.tabs.editorTab.objectCombo.IsOpen() ? UI_INTERACTION_EDITOR_OBJECT_OPEN : 0u;
    openControls |= widgets.render.renderTargetCombo.IsOpen() ? UI_INTERACTION_RENDER_TARGET_OPEN : 0u;
    openControls |= widgets.footer.cameraModeCombo.IsOpen() ? UI_INTERACTION_CAMERA_MODE_OPEN : 0u;
    cacheKey.interactionSignature = BuildUIInteractionSignature( { { widgets.pointer.mouseX, widgets.pointer.mouseY },
                                                                   windowBounds,
                                                                   openControls,
                                                                   widgets.catalog.selectedRenderTargetPreview,
                                                                   widgets.pointer.activeSlider } );

    // Why: Most UI frames only move the window/scroll offset. Replaying cached
    // draw commands keeps draw-call churn low while live render-target previews
    // still rebuild every frame.
    widgets.chrome.cache.BeginFrame( cacheKey );
    PROFILE_END( "Frame/UI/Layout" );

    const bool drawsLiveRenderTargetPreview = widgets.chrome.activeTab == InGameUITab::Targets;

    if ( !drawsLiveRenderTargetPreview &&
         widgets.chrome.cache.CanReplayPositionOnly( cacheKey, widgets.chrome.interaction.isDragging ) )
    {
        const float replayOffsetX = widgets.chrome.cache.ReplayOffsetX( cacheKey );
        const float replayOffsetY = widgets.chrome.cache.ReplayOffsetY( cacheKey );
        m_frameDrawList.Append( widgets.chrome.cache.DrawList(), replayOffsetX, replayOffsetY );

        AppendStandaloneOverlays( widgets.tabs, data, m_frameDrawList, m_histogramDrawList, m_memoryOverlayDrawList, screenW,
                                  screenH, histogramEnabled, memoryOverlayEnabled );
        // Lifetime: the cached commands remain in their original coordinate
        // space. Retain that source key so consecutive drag offsets stay total,
        // then rebuild from current content when capture ends.
        return finishDraw();
    }

    UIDrawList& drawList = widgets.chrome.cache.MutableDrawList();
    drawList.Clear();
    const UIDrawContext draw( screenW, screenH, drawList );
    PROFILE_BEGIN( "Frame/UI/DrawBuild" );

    const UIRect blurBounds = { x, y, w, h };
    PROFILE_BEGIN( "Frame/UI/Blur" );
    widgets.chrome.backdropBlur.Draw( draw, blurBounds, screenW, screenH, data.scene.currentFrame, data.surface.now,
                                      widgets.chrome.blurPreviewEnabled );
    PROFILE_END( "Frame/UI/Blur" );

    Chrome::DrawWindowFrame( draw, windowBounds, titleH, tabH, widgets.chrome.blurPreviewEnabled, titleText );
    const Chrome::TitleButtonRects titleButtons = Chrome::GetTitleButtonRects( windowBounds );
    Chrome::DrawTitleButtons( draw, titleButtons, widgets.chrome.window.isMaximized, widgets.pointer.mouseX,
                              widgets.pointer.mouseY );
    const UIRect objectCounterAvoidBounds = TitleButtonGroupBounds( titleButtons );
    DrawEditorObjectCounter( draw, data, screenW, screenH, &objectCounterAvoidBounds );

    static const char* kTabs[] = { "Prof",    "Scene", "Edit", "Phys", "Opt", "Render",
                                   "Targets", "Ctrl",  "Sky",  "Cine", "Mem" };

    const int tabCount = static_cast<int>( InGameUITab::Count );
    const float tabPad = 14.0f;
    widgets.chrome.tabBar.SetBounds( x + tabPad, y + titleH, w - tabPad * 2.0f, tabH );
    widgets.chrome.tabBar.Draw( draw, kTabs, tabCount, static_cast<int>( widgets.chrome.activeTab ) );

    const Style::UIPalette& palette = Style::Palette();
    draw.RoundedPanel( { contentX - 10.0f, contentY - 10.0f, contentW + 20.0f, contentH + 12.0f }, Style::Radii().window,
                       palette.windowSubtle, palette.innerBorder );

    DrawActiveTabContent( widgets.chrome.activeTab, widgets.tabs, widgets.render, widgets.catalog, widgets.pointer, data,
                          { draw, drawList, { contentX, contentY, contentW, contentH }, scrolledY } );

    widgets.chrome.scrollBar.SetBounds( x + w - 14.0f, contentY, 4.0f, contentH );
    widgets.chrome.scrollBar.Draw( draw, static_cast<float>( m_windowInteraction.ContentHeight() ), contentH,
                                   widgets.pointer.scrollY, widgets.pointer.scrollbarVisibleUntil, data.surface.now );

    const UIRect footerBounds = DrawFooterContent( widgets.chrome, widgets.footer, widgets.tabs, widgets.pointer, data,
                                                   { draw, x, y, w, h, bottomH, titleStatW, titleStatX, titleStat } );
    DrawHitboxOverlay( draw, data, widgets, windowBounds, { contentX, contentY, contentW, contentH }, footerBounds );

    PROFILE_END( "Frame/UI/DrawBuild" );
    m_frameDrawList.Append( drawList );
    AppendStandaloneOverlays( widgets.tabs, data, m_frameDrawList, m_histogramDrawList, m_memoryOverlayDrawList, screenW,
                              screenH, histogramEnabled, memoryOverlayEnabled );

    if ( drawsLiveRenderTargetPreview )
    {
        widgets.chrome.cache.Reset();
    }
    else
    {
        widgets.chrome.cache.StoreFrame( cacheKey );
    }

    return finishDraw();
}
