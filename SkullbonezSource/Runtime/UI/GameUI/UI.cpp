/*
File: SkullbonezSource/Runtime/UI/GameUI/UI.cpp
Purpose:
  Composes in-engine UI drawing and preserves the public InGameUI command surface.

Summary:
  UIWindowInteractionOwner records its own widgets into one ordered frame.
  InGameUI keeps the public command surface and detached scene-navigation
  composition without exposing the owner's complete widget storage.

Invariants:
  - Draw geometry and hit testing must be derived from the same layout
    constants.
  - InGameUI never reconstructs or publishes the owner's complete widget surface.
  - Draw builds values only; Runtime/Render performs every flush, preview
    resolution, resource operation, and GPU timing scope.

Related:
  - SkullbonezSource/Runtime/UI/GameUI/UI.h
  - SkullbonezSource/Runtime/UI/GameUI/UIWindowInteractionOwner.h
  - Agentic/Reference/engine-glossary.md
*/
#include "UI.h"
#include <chrono>
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
             scene.testComplete,
             surface.sceneName,
             surface.sceneMode,
             operatorEditor.tools.crossScenePauseLocked };
}

namespace
{
void DrawDockFold( const UIDrawContext& draw, const UIRect& bounds, bool pointsRight, bool hovered )
{
    const auto& palette = Style::Palette();
    if ( hovered )
    {
        draw.RoundedRect( bounds.x + 2.0f, bounds.y + 2.0f, bounds.w - 4.0f, bounds.h - 4.0f, 4.0f, palette.controlHover.r,
                          palette.controlHover.g, palette.controlHover.b, 1.0f );
    }
    const auto& ink = hovered ? palette.accentStrong : palette.textSecondary;
    const float cx = bounds.x + bounds.w * 0.5f;
    const float cy = bounds.y + bounds.h * 0.5f;
    // Preserve winding when mirroring: the UI triangle rasterizer culls back faces.
    const float direction = pointsRight ? 1.0f : -1.0f;
    draw.Triangle( cx + direction * 3.0f, cy, cx - direction * 2.0f, cy - direction * 4.0f, cx - direction * 2.0f,
                   cy + direction * 4.0f, ink.r, ink.g, ink.b, 1.0f );
}

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

void AppendStandaloneOverlays( ProfilerTab::UIProfilerTabState& profiler, MemoryTab::UIMemoryOverlayState& memoryState,
                               const InGameUIFrameData& data, UIDrawList& frame, UIDrawList& histogram, UIDrawList& memory,
                               int screenW, int screenH, bool histogramEnabled, bool memoryEnabled )
{
    if ( histogramEnabled )
    {
        histogram.Clear();
        histogram.SetPanel( UIPanel::DiagnosticPrimary );
        const UIDrawContext draw( screenW, screenH, histogram );
        ProfilerTab::DrawPerformanceHistogram( profiler, draw, data.ProfilerTabFrame() );
        frame.Append( histogram );
    }

    if ( memoryEnabled )
    {
        // Why: allocator events are retained by memory level, not by sample
        // index. Place this overlay under the histogram when both are visible.
        memory.Clear();
        memory.SetPanel( UIPanel::DiagnosticSecondary );
        const UIDrawContext draw( screenW, screenH, memory );
        const float x = histogramEnabled ? profiler.histogramPanelX : 16.0f;
        const float y = histogramEnabled ? profiler.histogramPanelY + profiler.histogramPanelH + 8.0f : 16.0f;
        MemoryTab::DrawOverlay( memoryState, draw, data.MemoryTabFrame(), x, y );
        frame.Append( memory );
    }
}

} // namespace

void UIWindowInteractionOwner::DrawMinimizedContent( const InGameUIFrameData& data, UIDrawList& drawList, int screenW,
                                                     int screenH )
{
    drawList.Clear();
    drawList.SetPanel( UIPanel::Drawer );
    const UIDrawContext draw( screenW, screenH, drawList );

    if ( m_window.animationActive && m_window.animationToMinimized )
    {
        Chrome::DrawWindowAnimationShell( draw, Chrome::CurrentWindowRect( m_window, data.surface.now ) );
        return;
    }

    char titleText[192] = {};
    BuildWindowTitle( data, titleText, sizeof( titleText ) );

    if ( !data.editor.editorModeEnabled )
    {
        StripMinimizedRuntimeModeSuffix( data, titleText, sizeof( titleText ) );
    }

    m_window.minimizedWidth = data.editor.editorModeEnabled
                                  ? EditorMinimizedWidth( data.EditorTabFrame(), screenW )
                                  : (std::min)( MinimizedWidthWithCameraModeCombo( titleText, screenW ),
                                                MINIMIZED_RUN_MAX_W );
    const UIRect minimized = Layout::MinimizedRect( screenW, screenH, m_window.minimizedWidth );

    if ( data.editor.editorModeEnabled )
    {
        const EditorMiniPaletteLayout layout = BuildEditorMiniPaletteLayout( screenW, screenH, minimized,
                                                                             m_editorMiniPalettePressedEntry,
                                                                             m_editorMiniPaletteFlyoutOpen );
        DrawEditorMiniPalette( draw, layout, data.editor.editorObjectType, data.editor.editorPlaceStatic, m_mouseX, m_mouseY,
                               m_editorMiniPalettePressedTreePlacement, m_editorMiniPalettePressedHoldMode,
                               m_editorMiniPalettePressedEntry, screenW, screenH );
        DrawEditorMinimizedWindow( draw, minimized, data.EditorTabFrame(), m_mouseX, m_mouseY );
    }
    else
    {
        const UIRect comboBounds = MinimizedCameraModeComboBounds( minimized );
        m_cameraModeCombo.SetLabelVisible( false );
        m_cameraModeCombo.SetBounds( comboBounds.x, comboBounds.y, comboBounds.w, comboBounds.h );
        m_cameraModeCombo.SetDropUp( true );
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
        m_cameraModeCombo.Draw( draw, "",
                                { std::span<const char* const>( kCameraModeOptions ), cameraModeIndex, disabledMask },
                                { m_mouseX, m_mouseY } );
    }

    DrawEditorObjectCounter( draw, data, screenW, screenH );
}

void UIWindowInteractionOwner::DrawRenderTabContent( const InGameUIFrameData& data, const UIDrawContext& draw,
                                                     const UIRect& content, float scrolledY )
{
    char buffer[128];
    const Style::UIPalette& palette = Style::Palette();
    const float colW = (std::max)( 148.0f, content.w * 0.46f );
    DrawSectionTitle( draw, content.x, content.y, content.h, scrolledY + 16.0f, 16.0f, "Render" );
    DrawContentToggle( draw, content.y, content.h, m_renderShadowToggle, content.x, scrolledY + UI_RENDER_FEATURE_START_Y,
                       colW, "Shadows", data.rendering.ordinaryRender.shadow.enabled );
    m_saveRenderDefaultsButton.SetBounds( content.x + content.w - UI_RENDER_SAVE_BUTTON_W,
                                          scrolledY + UI_RENDER_FEATURE_START_Y, UI_RENDER_SAVE_BUTTON_W, 24.0f );

    if ( IsRowVisible( content.y, content.h, scrolledY + UI_RENDER_FEATURE_START_Y, 24.0f ) )
    {
        m_saveRenderDefaultsButton.Draw( draw, "Save CFG", m_mouseX, m_mouseY );
    }

    static constexpr const char* labels[] = { "Main", "Reflection", "Terrain shadow", "Object shadow" };
    char visibilityText[96];

    for ( int viewIndex = 0; viewIndex < static_cast<int>( UIRenderVisibilityView::Count ); ++viewIndex )
    {
        const UIRenderVisibilityViewStats& visibility = data.surface.visibility.views[viewIndex];
        snprintf( visibilityText, sizeof( visibilityText ), "%d submitted, %d culled, %d draws", visibility.submitted,
                  visibility.culled, visibility.draws );
        DrawLabelValueAt( draw, content.y, content.h, content.x, scrolledY + 76.0f + static_cast<float>( viewIndex ) * 18.0f,
                          labels[viewIndex], visibilityText, palette.accent.r, palette.accent.g, palette.accent.b );
    }

    m_saveTrajectoryStyleButton.SetBounds( 0, 0, 0, 0 );
    const float baseY = scrolledY + UI_RENDER_START_Y;

    for ( int index = 0; index < static_cast<int>( UIRenderParam::Count ); ++index )
    {
        const RenderSliderSpec& spec = kRenderSliderSpecs[index];
        const float sliderY = RenderSliderY( index, baseY );

        if ( RenderSliderStartsSection( index ) &&
             IsRowVisible( content.y, content.h, sliderY - UI_RENDER_SECTION_H + 4.0f, 18.0f ) )
        {
            DrawSectionTitle( draw, content.x, content.y, content.h, sliderY - UI_RENDER_SECTION_H + 4.0f, 12.0f,
                              UIRenderAuthoringSectionName( spec.section ) );

            if ( spec.section == UIRenderAuthoringSection::PredictionPaths )
            {
                m_saveTrajectoryStyleButton.SetBounds( content.x + content.w - UI_TRAJECTORY_SAVE_BUTTON_W,
                                                       sliderY - UI_RENDER_SECTION_H + 1.0f, UI_TRAJECTORY_SAVE_BUTTON_W,
                                                       20.0f );
                m_saveTrajectoryStyleButton.Draw( draw, "Save Paths", m_mouseX, m_mouseY );
            }
        }

        const float value = std::clamp( RenderValueForParam( data.rendering.ordinaryRender, spec.param ), spec.minValue,
                                        spec.maxValue );
        snprintf( buffer, sizeof( buffer ), spec.valueFormat, value );
        m_renderSliders[index].SetBounds( content.x, sliderY, content.w, 34.0f );

        if ( IsRowVisible( content.y, content.h, sliderY, 34.0f ) )
        {
            m_renderSliders[index].Draw( draw, spec.label, buffer, value, spec.minValue, spec.maxValue );
        }
    }
}

void UIWindowInteractionOwner::DrawTargetsTabContent( const InGameUIFrameData& data, const UIDrawContext& draw,
                                                      UIDrawList& drawList, const UIRect& content, float scrolledY )
{
    const int targetCount = RenderTargetPreviewCount( data );
    const int selectedIndex = m_selectedRenderTargetPreview;
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

    DrawSectionTitle( draw, content.x, content.y, content.h, scrolledY + 16.0f, 16.0f, "Targets" );
    char countText[64];
    snprintf( countText, sizeof( countText ), "%d / %d live", liveCount, targetCount );

    if ( IsRowVisible( content.y, content.h, scrolledY + UI_TARGETS_META_Y - 24.0f, 18.0f ) )
    {
        DrawLabelValueAt( draw, content.y, content.h, content.x, scrolledY + UI_TARGETS_META_Y - 24.0f, "Resources",
                          countText, palette.accent.r, palette.accent.g, palette.accent.b );
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

        DrawLabelValueAt( draw, content.y, content.h, content.x, scrolledY + UI_TARGETS_META_Y, "Selected", detailText,
                          available ? palette.textPrimary.r : palette.textMuted.r,
                          available ? palette.textPrimary.g : palette.textMuted.g,
                          available ? palette.textPrimary.b : palette.textMuted.b );
    }

    const UIRect previewPanel = { content.x, scrolledY + UI_TARGETS_PREVIEW_Y, content.w, UI_TARGETS_PREVIEW_H };
    const UIRect previewClip = content;
    UIRect previewImage = previewPanel;

    if ( IsBlockVisible( content.y, content.h, previewPanel.y, previewPanel.h ) )
    {
        draw.RoundedPanel( previewPanel, Style::Radii().control, palette.windowSubtle, palette.innerBorder );
        const UIRect inset = { previewPanel.x + 10.0f, previewPanel.y + 10.0f, (std::max)( 1.0f, previewPanel.w - 20.0f ),
                               (std::max)( 1.0f, previewPanel.h - 20.0f ) };
        previewImage = selected ? FitRectToAspect( inset, selected->width, selected->height ) : inset;
        draw.RoundedRect( previewImage.x - 1.0f, previewImage.y - 1.0f, previewImage.w + 2.0f, previewImage.h + 2.0f,
                          Style::Radii().control, 0.01f, 0.015f, 0.018f, 0.92f );
    }

    if ( available && IsBlockVisible( content.y, content.h, previewImage.y, previewImage.h ) )
    {
        drawList.PushClip( previewClip );
        drawList.AddPreviewImage( { static_cast<uint16_t>( selectedIndex ), true }, previewImage, palette.windowSubtle,
                                  "Preview unavailable" );
        drawList.PopClip();
    }
    else if ( IsRowVisible( content.y, content.h, scrolledY + UI_TARGETS_PREVIEW_Y + 116.0f, 18.0f ) )
    {
        draw.Text( previewPanel.x + 18.0f, previewPanel.y + 116.0f, 12.0f, palette.textMuted.r, palette.textMuted.g,
                   palette.textMuted.b, "Not available this frame" );
    }

    if ( IsBlockVisible( content.y, content.h, previewPanel.y, previewPanel.h ) )
    {
        draw.Outline( previewImage.x, previewImage.y, previewImage.w, previewImage.h, palette.border.r, palette.border.g,
                      palette.border.b, 0.72f );
    }

    const char* selectedText = selected ? selected->label : "No targets";
    m_renderTargetCombo.SetBounds( content.x, scrolledY + UI_TARGETS_COMBO_Y, content.w, 24.0f );

    if ( IsRowVisible( content.y, content.h, scrolledY + UI_TARGETS_COMBO_Y, 24.0f ) )
    {
        m_renderTargetCombo.Draw( draw, "View",
                                  { std::span<const char* const>( options, static_cast<std::size_t>( targetCount ) ),
                                    selectedIndex, m_lastRenderTargetDisabledMask, selectedText },
                                  { m_mouseX, m_mouseY } );
    }
}

namespace
{

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
    const float cellWidth = statsWidth / 5.0f;
    snprintf( status, sizeof( status ), "%.0f", data.surface.fps );
    DrawFooterStatCell( context.draw, statsX + 18.0f, geometry.footerY, "FPS", status, palette.accent.r, palette.accent.g,
                        palette.accent.b );
    DrawFooterStatDivider( context.draw, statsX + cellWidth, geometry.footerY );
    snprintf( status, sizeof( status ), "%.2f ms", frameDisplayMs );
    DrawFooterStatCell( context.draw, statsX + cellWidth + 18.0f, geometry.footerY, "Frame Time", status,
                        palette.textPrimary.r, palette.textPrimary.g, palette.textPrimary.b );
    DrawFooterStatDivider( context.draw, statsX + cellWidth * 2.0f, geometry.footerY );
    snprintf( status, sizeof( status ), "%d%%", cpuPercent );
    DrawFooterStatCell( context.draw, statsX + cellWidth * 2.0f + 18.0f, geometry.footerY, "CPU", status, palette.accent.r,
                        palette.accent.g, palette.accent.b );
    DrawFooterStatDivider( context.draw, statsX + cellWidth * 3.0f, geometry.footerY );
    snprintf( status, sizeof( status ), "%d%%", gpuPercent );
    DrawFooterStatCell( context.draw, statsX + cellWidth * 3.0f + 18.0f, geometry.footerY, "GPU", status, palette.accent.r,
                        palette.accent.g, palette.accent.b );
    DrawFooterStatDivider( context.draw, statsX + cellWidth * 4.0f, geometry.footerY );
    snprintf( status, sizeof( status ), "%d / %d", drawCalls, data.surface.UIDrawCalls );
    DrawFooterStatCell( context.draw, statsX + cellWidth * 4.0f + 18.0f, geometry.footerY, "Draws / UI", status,
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

} // namespace

UIRect UIWindowInteractionOwner::DrawCompactToolsFooter( const InGameUIFrameData& data, const UIDrawContext& draw,
                                                         const ToolsChromeRects& chrome )
{
    PrepareCompactToolsControls( chrome );
    const char* options[] = { "Renderer: DX12",
                              "Water reflection...",
                              m_blurPreviewEnabled ? "Blur: on" : "Blur: off",
                              data.surface.vsyncEnabled ? "VSync: on" : "VSync: off",
                              m_hitboxOverlayEnabled ? "UI hitboxes: on" : "UI hitboxes: off",
                              ProfilerTab::PerformanceHistogramEnabled( m_profilerTab ) ? "Canvas performance: on"
                                                                                        : "Canvas performance: off",
                              ProfilerTab::TimelineEnabled( m_profilerTab ) ? "Profiler timeline: on"
                                                                            : "Profiler timeline: off" };
    if ( m_reflectionCombo.IsOpen() )
    {
        static const char* reflections[] = { "FBO", "DXR", "None" };
        m_reflectionCombo.Draw( draw, "",
                                { std::span<const char* const>( reflections ), WaterReflectionModeFromData( data ),
                                  ReflectionDisabledMask() },
                                { m_mouseX, m_mouseY } );
    }
    else
    {
        m_toolsDisplayCombo.Draw( draw, "", { std::span<const char* const>( options ), -1, 1u, "Display settings" },
                                  { m_mouseX, m_mouseY } );
    }
    return chrome.footer;
}

UIRect UIWindowInteractionOwner::DrawFooterContent( const InGameUIFrameData& data, const UIDrawContext& draw, float x,
                                                    float y, float width, float height, float bottomHeight,
                                                    float titleStatWidth, float titleStatX, const char* titleStat )
{
    const ToolsChromeRects chrome = ComputeToolsChromeRects( { x, y, width, height }, m_presentationEnabled );
    if ( chrome.compact )
    {
        return DrawCompactToolsFooter( data, draw, chrome );
    }
    const Style::UIPalette& palette = Style::Palette();
    const UIFooterDrawContext context { draw, x, y, width, height, bottomHeight, titleStatWidth, titleStatX, titleStat };
    UIFooterGeometry geometry;
    geometry.footerY = context.y + context.height - context.bottomHeight;
    geometry.footerX = context.x + 18.0f;
    geometry.footerWidth = (std::max)( 120.0f, context.width - 36.0f );
    geometry.hasSeparateStats = geometry.footerWidth >= 600.0f;
    geometry.controlsWidth = geometry.hasSeparateStats ? 462.0f : geometry.footerWidth;
    context.draw.Rect( context.x + 16.0f, geometry.footerY, context.width - 32.0f, 1.0f, palette.lineSoft.r,
                       palette.lineSoft.g, palette.lineSoft.b, 0.14f );
    const UIRect controlsBounds = { geometry.footerX, geometry.footerY + 16.0f, geometry.controlsWidth, 56.0f };
    context.draw.RoundedPanel( controlsBounds, Style::Radii().control, palette.windowSubtle, palette.innerBorder );
    const UIRect rendererBounds = FooterRendererComboBounds( context.x, geometry.footerY );
    const UIRect waterBounds = FooterWaterComboBounds( context.x, geometry.footerY );
    const UIRect blurBounds = FooterBlurBounds( context.x, geometry.footerY );
    const UIRect vsyncBounds = FooterVsyncBounds( context.x, geometry.footerY );
    const UIRect hitboxBounds = FooterHitboxBounds( context.x, geometry.footerY );
    const UIRect timelineBounds = FooterTimelineBounds( context.x, geometry.footerY );
    const UIRect performanceBounds = FooterPerfBounds( context.x, geometry.footerY );
    m_rendererCombo.SetBounds( rendererBounds.x, rendererBounds.y, rendererBounds.w, rendererBounds.h );
    m_rendererCombo.SetDropUp( true );
    m_reflectionCombo.SetBounds( waterBounds.x, waterBounds.y, waterBounds.w, waterBounds.h );
    m_reflectionCombo.SetDropUp( true );
    m_blurToggle.SetBounds( blurBounds.x, blurBounds.y, blurBounds.w, blurBounds.h );
    m_vsyncToggle.SetBounds( vsyncBounds.x, vsyncBounds.y, vsyncBounds.w, vsyncBounds.h );
    m_hitboxToggle.SetBounds( hitboxBounds.x, hitboxBounds.y, hitboxBounds.w, hitboxBounds.h );
    m_histogramToggle.SetBounds( performanceBounds.x, performanceBounds.y, performanceBounds.w, performanceBounds.h );
    m_timelineToggle.SetBounds( timelineBounds.x, timelineBounds.y, timelineBounds.w, timelineBounds.h );

    static const char* rendererOptions[] = { "DX12" };
    static const char* reflectionOptions[] = { "FBO", "DXR", "None" };
    m_rendererCombo.Draw( context.draw, "Renderer", { std::span<const char* const>( rendererOptions ), 0 },
                          { m_mouseX, m_mouseY } );
    DrawFooterToggle( context.draw, blurBounds, "Blur", m_blurPreviewEnabled );
    DrawFooterToggle( context.draw, vsyncBounds, "VSync", data.surface.vsyncEnabled );
    DrawFooterToggle( context.draw, hitboxBounds, "Hitboxes", m_hitboxOverlayEnabled );
    DrawFooterToggle( context.draw, performanceBounds, "Perf", ProfilerTab::PerformanceHistogramEnabled( m_profilerTab ) );
    DrawFooterToggle( context.draw, timelineBounds, "Timeline", ProfilerTab::TimelineEnabled( m_profilerTab ) );
    m_reflectionCombo.Draw( context.draw, "Water",
                            { std::span<const char* const>( reflectionOptions ), WaterReflectionModeFromData( data ),
                              ReflectionDisabledMask() },
                            { m_mouseX, m_mouseY } );
    DrawFooterStats( data, context, geometry );
    context.draw.Rect( context.x + context.width - 24.0f, context.y + context.height - 9.0f, 14.0f, 2.0f,
                       palette.textMuted.r, palette.textMuted.g, palette.textMuted.b, 0.58f );
    context.draw.Rect( context.x + context.width - 18.0f, context.y + context.height - 15.0f, 8.0f, 2.0f,
                       palette.textMuted.r, palette.textMuted.g, palette.textMuted.b, 0.46f );
    context.draw.Rect( context.x + context.width - 12.0f, context.y + context.height - 21.0f, 2.0f, 2.0f,
                       palette.textMuted.r, palette.textMuted.g, palette.textMuted.b, 0.38f );
    return controlsBounds;
}


void UIWindowInteractionOwner::DrawActiveTabContent( const InGameUIFrameData& data, const UIDrawContext& draw,
                                                     UIDrawList& drawList, const UIRect& content, float scrolledY )
{
    // Invariant: the selected branch is the only branch allowed to access its
    // retained tab state. Adding a tab must not rebuild a complete reference
    // surface at this dispatch boundary.
    switch ( m_activeTab )
    {
    case InGameUITab::Profiler:
        ProfilerTab::Draw( m_profilerTab, draw, data.ProfilerTabFrame(), content.x, content.y, content.w, content.h,
                           m_scrollY, m_activeSlider );
        break;
    case InGameUITab::Memory:
        MemoryTab::Draw( draw, m_memoryOverlay, data.MemoryTabFrame(), content.x, content.y, content.w, content.h, scrolledY,
                         m_activeSlider, m_mouseX, m_mouseY );
        break;
    case InGameUITab::Scene:
        SceneTab::Draw( m_sceneTab, draw, data.SceneTabFrame(), content.x, content.y, content.w, content.h, scrolledY,
                        m_mouseX, m_mouseY );
        break;
    case InGameUITab::Physics:
        PhysicsTab::Draw( m_physicsTab, draw, data.PhysicsTabFrame(), content.x, content.y, content.w, content.h, scrolledY,
                          m_activeSlider, m_mouseX, m_mouseY );
        break;
    case InGameUITab::Editor:
        EditorTab::Draw( m_editorTab, draw, data.EditorTabFrame(), content.x, content.y, content.w, content.h, scrolledY,
                         m_mouseX, m_mouseY );
        break;
    case InGameUITab::Options:
        OptionsTab::Draw( m_optionsTab, draw, data.OptionsTabFrame(), content.x, content.y, content.w, content.h, scrolledY,
                          m_activeSlider );
        break;
    case InGameUITab::Render:
        DrawRenderTabContent( data, draw, content, scrolledY );
        break;
    case InGameUITab::Targets:
        DrawTargetsTabContent( data, draw, drawList, content, scrolledY );
        break;
    case InGameUITab::Sky:
        SkyTab::Draw( m_skyTab, draw, data.rendering.cinematic, content.x, content.y, content.w, content.h, scrolledY,
                      m_mouseX, m_mouseY );
        break;
    case InGameUITab::Cinematic:
        CinematicTab::Draw( m_cinematicTab, draw, data.CinematicTabFrame(), content.x, content.y, content.w, content.h,
                            scrolledY, m_mouseX, m_mouseY );
        break;
    case InGameUITab::Keys:
        ControlsTab::Draw( m_controlsTab, draw, data.ControlsTabFrame(), content.x, content.y, content.w, content.h,
                           scrolledY );
        break;
    default:
        break;
    }
}


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
    return m_windowInteraction.BlocksCameraMouse() ||
           m_panelTransitions->BlocksPointer(
               { static_cast<float>( m_windowInteraction.m_mouseX ), static_cast<float>( m_windowInteraction.m_mouseY ) } );
}

bool InGameUI::BlocksReplayMouse() const
{
    return m_windowInteraction.BlocksReplayMouse() ||
           m_panelTransitions->BlocksPointer(
               { static_cast<float>( m_windowInteraction.m_mouseX ), static_cast<float>( m_windowInteraction.m_mouseY ) } );
}

bool InGameUI::BlocksCauseMouse() const
{
    return m_windowInteraction.BlocksCauseMouse() ||
           m_panelTransitions->BlocksPointer(
               { static_cast<float>( m_windowInteraction.m_mouseX ), static_cast<float>( m_windowInteraction.m_mouseY ) } );
}

bool InGameUI::SharedPresentationEnabled() const
{
    return m_windowInteraction.m_presentationEnabled;
}

void InGameUI::RevealReplayControls( int width, int height )
{
    if ( !m_windowInteraction.m_presentationEnabled )
    {
        return;
    }
    m_windowInteraction.m_presentation.preferences.layout = LayoutMode::Editor;
    m_windowInteraction.m_presentation.detailsOpen = false;
    m_windowInteraction.m_presentation.detailsCauses = false;
    m_windowInteraction.m_presentation.preferences.replayFolded = false;
    m_windowInteraction.m_presentation.replayScroll = 0.0f;
    m_windowInteraction.m_presentationRects = ComputePresentationRects( m_windowInteraction.m_presentation, width, height );
}
void InGameUI::RevealCauseControls( int width, int height )
{
    if ( !m_windowInteraction.m_presentationEnabled )
    {
        return;
    }
    m_windowInteraction.m_presentation.preferences.layout = LayoutMode::Editor;
    m_windowInteraction.m_presentation.detailsOpen = false;
    m_windowInteraction.m_presentation.detailsCauses = true;
    m_windowInteraction.m_presentation.preferences.rightFolded = false;
    m_windowInteraction.m_presentationRects = ComputePresentationRects( m_windowInteraction.m_presentation, width, height );
}

GameLayout::ComboPopupPresentation InGameUI::EditorPopup() const
{
    const UIComboBox& popup = m_windowInteraction.m_editorTab.objectCombo;
    return { popup.DropdownBounds( EditorTab::OBJECT_TYPE_COUNT ), popup.FirstVisibleOption( EditorTab::OBJECT_TYPE_COUNT ),
             popup.VisibleOptionCount( EditorTab::OBJECT_TYPE_COUNT ), EditorTab::OBJECT_TYPE_COUNT, popup.IsOpen() };
}

GameLayout::ComboPopupPresentation InGameUI::ToolsPopup() const
{
    const auto& owner = m_windowInteraction;
    const bool tabs = owner.m_toolsTabCombo.IsOpen();
    const bool reflection = owner.m_reflectionCombo.IsOpen();
    const UIComboBox& popup = tabs ? owner.m_toolsTabCombo
                                   : ( reflection ? owner.m_reflectionCombo : owner.m_toolsDisplayCombo );
    const int count = tabs ? static_cast<int>( InGameUITab::Count ) : ( reflection ? 3 : 7 );
    return { popup.DropdownBounds( count ), popup.FirstVisibleOption( count ), popup.VisibleOptionCount( count ), count,
             popup.IsOpen() };
}

GameLayout::ComboPopupPresentation InGameUI::TargetPopup() const
{
    const auto& owner = m_windowInteraction;
    const auto& popup = owner.m_renderTargetCombo;
    const int count = owner.m_lastRenderTargetPreviewCount;
    return { popup.DropdownBounds( count ),
             popup.FirstVisibleOption( count ),
             popup.VisibleOptionCount( count ),
             count,
             popup.IsOpen(),
             owner.m_selectedRenderTargetPreview,
             owner.m_lastRenderTargetDisabledMask };
}

GameLayout::ComboPopupPresentation InGameUI::RecordingPopup() const
{
    const auto& owner = m_windowInteraction;
    const auto& popup = owner.m_sceneTab.recordingCombo;
    const int count = static_cast<int>( m_sceneNavigation.recordings.paths.size() );
    const int visible = GameLayout::SceneComboVisibleCount( count );
    return { popup.DropdownBounds( visible ),
             owner.m_sceneTab.recordingComboScroll,
             popup.VisibleOptionCount( visible ),
             count,
             popup.IsOpen(),
             m_sceneNavigation.recordings.selectedIndex };
}

GameLayout::ComboPopupPresentation InGameUI::CameraPopup() const
{
    const UIComboBox& popup = m_windowInteraction.m_cameraModeCombo;
    return { popup.DropdownBounds( CAMERA_MODE_OPTION_COUNT ), 0, CAMERA_MODE_OPTION_COUNT, CAMERA_MODE_OPTION_COUNT,
             popup.IsOpen() };
}

bool InGameUI::HasOpenPopup() const
{
    return m_windowInteraction.HasOpenPopup();
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
    return m_windowInteraction.NeedsUiTextPass() || m_panelTransitions->Active();
}
void InGameUI::SetHitboxOverlayEnabled( bool enabled )
{
    m_windowInteraction.SetHitboxOverlayEnabled( enabled );
}
float InGameUI::ToolsScroll() const noexcept
{
    return m_windowInteraction.m_scrollY;
}

UIRect InGameUI::ToolsContentBounds() const noexcept
{
    return GameLayout::ComputeToolsChromeRects( m_windowInteraction.m_presentationRects.drawer, true ).content;
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
void UIWindowInteractionOwner::DrawHitboxOverlay( const UIDrawContext& draw, const InGameUIFrameData& data,
                                                  const UIRect& windowBounds, const UIRect& contentBounds,
                                                  const UIRect& footerBounds )
{
    if ( !m_hitboxOverlayEnabled )
    {
        return;
    }

    DrawWindowHitboxes( draw, windowBounds, contentBounds );
    DrawActiveTabHitboxes( draw, data );
    DrawFooterHitboxes( draw, footerBounds );
}


void UIWindowInteractionOwner::DrawWindowHitboxes( const UIDrawContext& draw, const UIRect& windowBounds,
                                                   const UIRect& contentBounds )
{
    constexpr float chromeR = 0.16f;
    constexpr float chromeG = 0.86f;
    constexpr float chromeB = 1.00f;
    constexpr float contentR = 0.30f;
    constexpr float contentG = 1.00f;
    constexpr float contentB = 0.42f;
    DrawHitboxRect( draw, windowBounds, chromeR, chromeG, chromeB, 0.018f, 0.44f );

    const Chrome::TitleButtonRects titleButtons = Chrome::GetTitleButtonRects( windowBounds );
    DrawHitboxRect( draw, titleButtons.minimize, chromeR, chromeG, chromeB, 0.050f, 0.86f );
    DrawHitboxRect( draw, titleButtons.maximize, chromeR, chromeG, chromeB, 0.050f, 0.86f );
    DrawHitboxRect( draw, titleButtons.close, chromeR, chromeG, chromeB, 0.050f, 0.86f );

    if ( !m_window.isMaximized )
    {
        DrawHitboxRect( draw,
                        { windowBounds.x + windowBounds.w - 26.0f, windowBounds.y + windowBounds.h - 26.0f, 26.0f, 26.0f },
                        chromeR, chromeG, chromeB, 0.050f, 0.86f );
    }

    DrawTabHitboxes( draw, m_tabBar, static_cast<int>( InGameUITab::Count ) );
    DrawHitboxRect( draw, contentBounds, contentR, contentG, contentB, 0.018f, 0.48f );

    if ( ContentHeight() > static_cast<int>( contentBounds.h ) )
    {
        DrawHitboxRect( draw, m_scrollBar.Bounds(), 0.18f, 0.82f, 0.95f, 0.060f, 0.86f );
    }
}


void UIWindowInteractionOwner::DrawActiveTabHitboxes( const UIDrawContext& draw, const InGameUIFrameData& data )
{
    constexpr float contentR = 0.30f;
    constexpr float contentG = 1.00f;
    constexpr float contentB = 0.42f;
    constexpr float buttonR = 1.00f;
    constexpr float buttonG = 0.62f;
    constexpr float buttonB = 0.18f;

    switch ( m_activeTab )
    {
    case InGameUITab::Scene:
        DrawComboHitboxes( draw, m_sceneTab.combo, SceneDropdownHitboxOptionCount( m_sceneTab, data.SceneTabFrame() ),
                           contentR, contentG, contentB );

        DrawHitboxRect( draw, m_sceneTab.resetSceneButton.Bounds(), buttonR, buttonG, buttonB );
        DrawHitboxRect( draw, m_sceneTab.resetDefaultsButton.Bounds(), buttonR, buttonG, buttonB );
        DrawHitboxRect( draw, m_sceneTab.saveDefaultsButton.Bounds(), buttonR, buttonG, buttonB );
        DrawHitboxRect( draw, m_sceneTab.timeScaleSlider.Bounds(), contentR, contentG, contentB );
        break;
    case InGameUITab::Editor:
        DrawHitboxRect( draw, m_editorTab.editorModeToggle.Bounds(), contentR, contentG, contentB );
        DrawHitboxRect( draw, m_editorTab.placementModeToggle.Bounds(), contentR, contentG, contentB );
        DrawHitboxRect( draw, m_editorTab.staticObjectToggle.Bounds(), contentR, contentG, contentB );
        DrawComboHitboxes( draw, m_editorTab.objectCombo, EditorTab::OBJECT_TYPE_COUNT, contentR, contentG, contentB );

        break;
    case InGameUITab::Physics:

        for ( int i = 0; i < 13; ++i )
        {
            DrawHitboxRect( draw, m_physicsTab.toggles[i].Bounds(), contentR, contentG, contentB );
        }

        DrawHitboxRect( draw, m_physicsTab.pipelinePrevButton, buttonR, buttonG, buttonB );
        DrawHitboxRect( draw, m_physicsTab.pipelineNextButton, buttonR, buttonG, buttonB );
        DrawHitboxRect( draw, m_physicsTab.alphaSlider.Bounds(), contentR, contentG, contentB );
        DrawHitboxRect( draw, m_physicsTab.contactLingerSlider.Bounds(), contentR, contentG, contentB );
        DrawHitboxRect( draw, m_physicsTab.rayImpulseSlider.Bounds(), contentR, contentG, contentB );
        DrawHitboxRect( draw, m_physicsTab.launcherProjectileSpeedSlider.Bounds(), contentR, contentG, contentB );
        DrawHitboxRect( draw, m_physicsTab.worldGravitySlider.Bounds(), contentR, contentG, contentB );
        DrawHitboxRect( draw, m_physicsTab.terrainFrictionSlider.Bounds(), contentR, contentG, contentB );
        DrawHitboxRect( draw, m_physicsTab.objectFrictionSlider.Bounds(), contentR, contentG, contentB );
        DrawHitboxRect( draw, m_physicsTab.rollingFrictionSlider.Bounds(), contentR, contentG, contentB );
        DrawHitboxRect( draw, m_physicsTab.tornadoRadiusSlider.Bounds(), contentR, contentG, contentB );
        DrawHitboxRect( draw, m_physicsTab.tornadoHeightSlider.Bounds(), contentR, contentG, contentB );
        DrawHitboxRect( draw, m_physicsTab.tornadoInwardSlider.Bounds(), contentR, contentG, contentB );
        DrawHitboxRect( draw, m_physicsTab.tornadoSwirlSlider.Bounds(), contentR, contentG, contentB );
        DrawHitboxRect( draw, m_physicsTab.tornadoLiftSlider.Bounds(), contentR, contentG, contentB );
        break;
    case InGameUITab::Options:

        for ( int i = 0; i < 6; ++i )
        {
            DrawHitboxRect( draw, m_optionsTab.toggles[i].Bounds(), contentR, contentG, contentB );
        }

        DrawHitboxRect( draw, m_optionsTab.timeScaleSlider.Bounds(), contentR, contentG, contentB );
        DrawHitboxRect( draw, m_optionsTab.modelCountSlider.Bounds(), contentR, contentG, contentB );
        break;
    case InGameUITab::Render:
        DrawHitboxRect( draw, m_renderShadowToggle.Bounds(), contentR, contentG, contentB );
        DrawHitboxRect( draw, m_saveRenderDefaultsButton.Bounds(), contentR, contentG, contentB );

        for ( int i = 0; i < static_cast<int>( UIRenderParam::Count ); ++i )
        {
            DrawHitboxRect( draw, m_renderSliders[i].Bounds(), contentR, contentG, contentB );
        }

        break;
    case InGameUITab::Targets:
        DrawComboHitboxes( draw, m_renderTargetCombo, m_lastRenderTargetPreviewCount, contentR, contentG, contentB );

        break;
    case InGameUITab::Keys:
        DrawHitboxRect( draw, m_controlsTab.seedSlider.Bounds(), contentR, contentG, contentB );
        DrawHitboxRect( draw, m_controlsTab.solverBallSlider.Bounds(), contentR, contentG, contentB );
        DrawHitboxRect( draw, m_controlsTab.solverBoxSlider.Bounds(), contentR, contentG, contentB );
        DrawHitboxRect( draw, m_controlsTab.worldFluidHeightSlider.Bounds(), contentR, contentG, contentB );
        DrawHitboxRect( draw, m_controlsTab.worldFluidDensitySlider.Bounds(), contentR, contentG, contentB );
        break;
    case InGameUITab::Sky:
        SkyTab::DrawHitboxes( m_skyTab, draw, contentR, contentG, contentB );
        break;
    case InGameUITab::Cinematic:
        CinematicTab::DrawHitboxes( m_cinematicTab, draw, data.CinematicTabFrame(), contentR, contentG, contentB );
        break;
    case InGameUITab::Profiler:
        DrawHitboxRect( draw, m_profilerTab.workerToggle.Bounds(), contentR, contentG, contentB );
        DrawHitboxRect( draw, m_profilerTab.workerThreadSlider.Bounds(), contentR, contentG, contentB );
        break;
    case InGameUITab::Memory:
        break;
    default:
        break;
    }
}

void UIWindowInteractionOwner::DrawFooterHitboxes( const UIDrawContext& draw, const UIRect& footerBounds )
{
    constexpr float footerR = 1.00f;
    constexpr float footerG = 0.22f;
    constexpr float footerB = 0.82f;
    DrawHitboxRect( draw, footerBounds, footerR, footerG, footerB, 0.020f, 0.54f );
    DrawComboHitboxes( draw, m_rendererCombo, 1, footerR, footerG, footerB );
    DrawComboHitboxes( draw, m_reflectionCombo, 3, footerR, footerG, footerB );
    DrawHitboxRect( draw, m_blurToggle.Bounds(), footerR, footerG, footerB );
    DrawHitboxRect( draw, m_vsyncToggle.Bounds(), footerR, footerG, footerB );
    DrawHitboxRect( draw, m_histogramToggle.Bounds(), footerR, footerG, footerB );
    DrawHitboxRect( draw, m_timelineToggle.Bounds(), footerR, footerG, footerB );
    DrawHitboxRect( draw, m_hitboxToggle.Bounds(), footerR, footerG, footerB );
}


InputControl::UIPointerOverride InGameUI::InputOverride() const
{
    return m_windowInteraction.InputOverride();
}


InGameUIInputResult InGameUI::UpdateInput( const InputControl::UIInputSnapshot& input, int screenWidth, int screenHeight,
                                           double now, bool editorModeEnabled, bool placementModeEnabled,
                                           bool placeStaticObject, bool autoTerrainAlign, uint32_t cameraModeEnabledMask )
{
    PROFILE_SCOPED( "Frame/UI/Input" );
    InputControl::UIInputSnapshot pointerInput = input;
    if ( !m_windowInteraction.m_interaction.isResizing &&
         m_panelTransitions->BlocksPointer( { static_cast<float>( input.mouseX ), static_cast<float>( input.mouseY ) } ) )
    {
        pointerInput.leftDown = pointerInput.leftPressed = pointerInput.leftReleased = false;
        pointerInput.rightDown = pointerInput.middleDown = false;
        pointerInput.wheelDelta = 0;
    }
    return m_windowInteraction.UpdateInput( pointerInput, m_sceneNavigation, screenWidth, screenHeight, now,
                                            editorModeEnabled, placementModeEnabled, placeStaticObject, autoTerrainAlign,
                                            cameraModeEnabledMask );
}
const UIDrawList& InGameUI::Draw( const InGameUIFrameData& data )
{
    return m_windowInteraction.Draw( data );
}

const UIDrawList& InGameUI::ForegroundDraw() const
{
    return m_windowInteraction.m_foregroundDrawList;
}

void InGameUI::UpdatePresentationInput( const InputControl::UIInputSnapshot& input, int width, int height, bool enabled )
{
    m_windowInteraction.UpdatePresentationInput( input, width, height, enabled );
    if ( m_windowInteraction.m_interaction.isResizing && m_windowInteraction.m_interaction.resizeRegion == 1 )
    {
        m_panelTransitions->Finish( UIPanel::Drawer, true );
    }
}

PresentationRects InGameUI::PresentationBounds() const
{
    return m_windowInteraction.m_presentationRects;
}

Workspace InGameUI::PresentationWorkspace() const
{
    return m_windowInteraction.m_presentation.workspace;
}

void InGameUI::SetPresentationWorkspace( Workspace workspace )
{
    m_windowInteraction.m_presentation.workspace = workspace;
    m_windowInteraction.m_tooltip.Dismiss();
    m_windowInteraction.m_presentationRects = ComputePresentationRects( m_windowInteraction.m_presentation,
                                                                        m_windowInteraction.m_lastScreenW,
                                                                        m_windowInteraction.m_lastScreenH );
}

LayoutMode InGameUI::PresentationLayout() const
{
    return m_windowInteraction.m_presentation.preferences.layout;
}

bool InGameUI::HasDockedSurface() const
{
    return m_windowInteraction.m_presentationEnabled &&
           ( PresentationLayout() == LayoutMode::Editor || PresentationWorkspace() == Workspace::SolverLab ||
             m_windowInteraction.m_presentation.detailsOpen || ( IsVisible() && !IsMinimized() ) );
}

void InGameUI::ReturnToGame()
{
    m_windowInteraction.ReturnToGame();
}

UITooltipTarget InGameUI::VisibleTooltip() const
{
    const double now = std::chrono::duration<double>( std::chrono::steady_clock::now().time_since_epoch() ).count();
    return m_windowInteraction.m_tooltip.VisibleTarget( now );
}

GameLayout::DiagnosticPresentation InGameUI::DiagnosticPresentation() const
{
    GameLayout::DiagnosticPresentation view;
    view.markerHistoryVisible = m_windowInteraction.IsPerformanceHistogramEnabled();
    view.memoryWaterlineVisible = m_windowInteraction.IsMemoryOverlayEnabled();
    const auto& profiler = m_windowInteraction.m_profilerTab;
    view.markerBounds = view.markerHistoryVisible ? ( profiler.histogramDockedBounds.h > 0
                                                          ? profiler.histogramDockedBounds
                                                          : UIRect { profiler.histogramPanelX, profiler.histogramPanelY,
                                                                     profiler.histogramPanelW, profiler.histogramPanelH } )
                                                  : UIRect {};
    view.workerToggleBounds = profiler.workerToggle.Bounds();
    view.workerSliderBounds = profiler.workerThreadSlider.Bounds();
    view.markerSamples = m_windowInteraction.m_profilerTab.histogramCount;
    view.memorySamples = m_windowInteraction.m_memoryOverlay.sampleCount;
    view.focusedPanel = m_windowInteraction.m_presentation.focusedDiagnostic;
    view.profilerTimeline = m_windowInteraction.m_profilerTab.timelineEnabled;
    view.profilerMarkerCount = m_windowInteraction.m_profilerTab.frame.markerCount;
    view.profilerDrawNodeCount = m_windowInteraction.m_profilerTab.frame.drawTrace.nodeCount;
    view.drawExpanderBounds = ProfilerTab::FirstDrawExpanderBounds( m_windowInteraction.m_profilerTab, ToolsContentBounds(),
                                                                    m_windowInteraction.m_scrollY );
    for ( int index = 0; index < m_windowInteraction.m_profilerTab.expandedHashCount; ++index )
    {
        view.profilerExpansionHash = HashCombine( view.profilerExpansionHash,
                                                  m_windowInteraction.m_profilerTab.expandedHashes[index] );
    }
    for ( int index = 0; index < m_windowInteraction.m_profilerTab.drawExpandedHashCount; ++index )
    {
        view.drawExpansionHash = HashCombine( view.drawExpansionHash,
                                              m_windowInteraction.m_profilerTab.drawExpandedHashes[index] );
    }
    for ( int index = 0; index < m_windowInteraction.m_profilerTab.histogramOptionCount; ++index )
    {
        if ( m_windowInteraction.m_profilerTab.histogramOptionSelected[index] )
        {
            // Main uses hash zero. Include its frame-total identity so selecting
            // it cannot produce the same observation as an empty selection.
            view.markerSelectionHash = HashCombine( view.markerSelectionHash,
                                                    m_windowInteraction.m_profilerTab.histogramOptionFrameTotals[index]
                                                        ? 1u
                                                        : 2u );
            view.markerSelectionHash = HashCombine( view.markerSelectionHash,
                                                    m_windowInteraction.m_profilerTab.histogramOptionHashes[index] );
        }
    }
    return view;
}

void UIWindowInteractionOwner::DrawPresentationDocks( const InGameUIFrameData& data )
{
    const UIPanelScope panelScope( m_frameDrawList, UIPanel::None );
    const auto& palette = Style::Palette();
    if ( !m_presentationEnabled )
    {
        return;
    }
    const UIDrawContext draw( data.surface.screenW, data.surface.screenH, m_frameDrawList );
    for ( const auto& slot : { m_presentationRects.editorPane, m_presentationRects.replayPane } )
    {
        if ( slot.h > 0 && slot.w <= 24 && m_presentationRects.left.w > 24 )
        {
            draw.Rect( slot.x, slot.y, m_presentationRects.left.w, slot.h, palette.window.r, palette.window.g,
                       palette.window.b, 1 );
        }
    }
    const UIRect panes[] = { m_presentationRects.editorPane, m_presentationRects.right, m_presentationRects.replayPane };
    for ( size_t index = 0; index < std::size( panes ); ++index )

    {
        const UIRect& pane = panes[index];
        const UIPanel ids[] = { UIPanel::Left, UIPanel::Right, UIPanel::LowerLeft };
        m_frameDrawList.SetPanel( pane.w > 24.0f ? ids[index] : UIPanel::None );
        if ( pane.w <= 0.0f || pane.h <= 0.0f )
        {
            continue;
        }
        draw.Rect( pane.x, pane.y, pane.w, pane.h, palette.window.r, palette.window.g, palette.window.b, 1.0f );
        draw.Outline( pane.x, pane.y, pane.w, pane.h, palette.border.r, palette.border.g, palette.border.b, 1.0f );
    }
    m_frameDrawList.SetPanel( UIPanel::Transport );
    const float transportAlpha = m_presentation.workspace == Workspace::SolverLab ||
                                         m_presentation.preferences.layout == LayoutMode::Editor
                                     ? 1.0f
                                     : std::clamp( data.surface.transportAlpha, 0.0f, 1.0f );
    if ( transportAlpha > 0.0f )
    {
        if ( m_presentation.preferences.layout == LayoutMode::Editor )
        {
            draw.Rect( 0.0f, m_presentationRects.transport.y, m_presentationRects.window.w, m_presentationRects.transport.h,
                       palette.window.r, palette.window.g, palette.window.b, transportAlpha );
        }
        if ( !m_presentation.toolsOpen )
        {
            const UIRect& details = m_presentationRects.replayDetails;
            const auto& fill = details.Contains( m_mouseX, m_mouseY ) ? palette.controlHover : palette.control;
            draw.RoundedRect( details.x, details.y, details.w, details.h, 4.0f, fill.r, fill.g, fill.b, transportAlpha );
            draw.Text( details.x + 10.0f, details.y + 8.0f, 11.0f, palette.textPrimary.r * transportAlpha,
                       palette.textPrimary.g * transportAlpha, palette.textPrimary.b * transportAlpha, "Tools" );
        }
    }
    m_frameDrawList.SetPanel( UIPanel::Right );
    const UIRect tabs[] = { m_presentationRects.detailsReplayTab, m_presentationRects.detailsCausesTab };
    for ( int index = 0; index < 2; ++index )
    {
        if ( tabs[index].w > 0.0f )
        {
            const bool selected = m_presentation.detailsCauses == ( index == 1 );
            const bool hovered = tabs[index].Contains( m_mouseX, m_mouseY );
            const auto& fill = selected ? palette.selection : ( hovered ? palette.controlHover : palette.control );
            const auto& ink = selected || hovered ? palette.textPrimary : palette.textSecondary;
            draw.RoundedRect( tabs[index].x, tabs[index].y, tabs[index].w, tabs[index].h, 4.0f, fill.r, fill.g, fill.b,
                              1.0f );
            if ( selected )
            {
                draw.Rect( tabs[index].x + 6.0f, tabs[index].y + tabs[index].h - 2.0f, tabs[index].w - 12.0f, 2.0f,
                           palette.accent.r, palette.accent.g, palette.accent.b, 1.0f );
            }
            draw.Text( tabs[index].x + 10.0f, tabs[index].y + 5.0f, 12.0f, ink.r, ink.g, ink.b,
                       m_presentation.workspace == Workspace::SolverLab ? ( index == 0 ? "Controls" : "Differences" )
                                                                        : ( index == 0 ? "Replay" : "Causes" ) );
        }
    }
    draw.BeginLayer();
}

void UIWindowInteractionOwner::DrawEditorDock( const InGameUIFrameData& data )
{
    const UIPanelScope panelScope( m_frameDrawList, UIPanel::None );
    const auto& palette = Style::Palette();
    if ( !m_presentationEnabled || m_presentation.preferences.layout != LayoutMode::Editor )
    {
        return;
    }
    const UIDrawContext draw( data.surface.screenW, data.surface.screenH, m_frameDrawList );
    const UIRect tabs[] = { m_presentationRects.editorTab, m_presentationRects.editorReplayTab };
    const auto verticalTab = [&]( const UIRect& bounds, const char* label, const Style::UIColor& color )
    {
        if ( bounds.w <= 0.0f || bounds.h <= 0.0f )
        {
            return;
        }
        const auto& fill = bounds.Contains( m_mouseX, m_mouseY ) ? palette.controlHover : palette.control;
        draw.RoundedPanel( bounds, 3.0f, fill, color );
        const float advance = (std::max)( 1.0f, UIFontMetrics::MeasureText( 1.0f, label ) );
        const float font = (std::min)( 13.0f, (std::max)( 0.0f, bounds.h - 8.0f ) / advance );
        draw.PushClip( bounds );
        draw.VerticalText( { bounds.x + ( bounds.w - font ) * 0.5f, bounds.y + ( bounds.h - advance * font ) * 0.5f }, font,
                           color, label );
        draw.PopClip();
    };
    const UIRect panes[] = { m_presentationRects.editorPane, m_presentationRects.replayPane };
    const UIRect folds[] = { m_presentationRects.leftFold, m_presentationRects.replayFold };
    const UIPanel ids[] = { UIPanel::Left, UIPanel::LowerLeft };
    for ( int index = 0; index < 2; ++index )
    {
        if ( panes[index].h <= 0 )
        {
            continue;
        }
        const bool folded = panes[index].w <= 24;
        m_frameDrawList.SetPanel( folded ? UIPanel::None : ids[index] );
        const char* label = index == 1 ? "Replay"
                                       : ( m_presentation.workspace == Workspace::SolverLab ? "Controls" : "Editor" );
        if ( folded )
        {
            verticalTab( tabs[index], label, palette.textSecondary );
        }
        else
        {
            const bool hovered = tabs[index].Contains( m_mouseX, m_mouseY );
            const auto& fill = hovered ? palette.controlHover : palette.control;
            draw.RoundedPanel( tabs[index], 3, fill, palette.border );
            draw.Text( tabs[index].x + 8, tabs[index].y + 6, 12, palette.textPrimary.r, palette.textPrimary.g,
                       palette.textPrimary.b, label );
        }
        DrawDockFold( draw, folds[index], folded, folds[index].Contains( m_mouseX, m_mouseY ) );
    }
    m_frameDrawList.SetPanel( m_presentation.preferences.rightFolded ? UIPanel::None : UIPanel::Right );
    verticalTab( m_presentationRects.causeTab, m_presentation.workspace == Workspace::SolverLab ? "Differences" : "Causes",
                 palette.textSecondary );
    DrawDockFold( draw, m_presentationRects.rightFold, !m_presentation.preferences.rightFolded,
                  m_presentationRects.rightFold.Contains( m_mouseX, m_mouseY ) );
    if ( m_presentationRects.right.w > 24 )
    {
        draw.Text( m_presentationRects.right.x + 30, m_presentationRects.right.y + 8, 11, palette.textPrimary.r,
                   palette.textPrimary.g, palette.textPrimary.b,
                   m_presentation.workspace == Workspace::SolverLab ? "Differences" : "Causes" );
    }
    // A visible grip and larger hit area make both dock edges discoverable.
    const UIRect grips[] = { m_presentationRects.leftResize, m_presentationRects.replayResize,
                             m_presentationRects.rightResize };
    const UIPanel gripPanels[] = { UIPanel::Left, UIPanel::LowerLeft, UIPanel::Right };
    for ( int index = 0; index < 3; ++index )
    {
        const auto& grip = grips[index];
        if ( grip.w <= 0 )
        {
            continue;
        }
        m_frameDrawList.SetPanel( gripPanels[index] );
        const bool active = grip.Contains( m_mouseX, m_mouseY ) ||
                            ( m_interaction.isResizing && m_interaction.resizeRegion == ( index == 2 ? 3 : 2 ) );
        const auto& ink = active ? palette.accent : palette.border;
        draw.RoundedRect( grip.x + grip.w * 0.5f - 1, grip.y + grip.h * 0.5f - 20, 2, 40, 1, ink.r, ink.g, ink.b, 1 );
    }
    m_frameDrawList.SetPanel( UIPanel::Left );
    const UIRect& bounds = m_presentationRects.editorControls;
    if ( bounds.w > 0.0f )
    {
        m_frameDrawList.PushClip( bounds );
        EditorTab::Draw( m_editorTab, draw, data.EditorTabFrame(), bounds.x, bounds.y, bounds.w, bounds.h,
                         bounds.y - m_presentationRects.editorScroll, m_mouseX, m_mouseY );
        m_frameDrawList.PopClip();
    }
    else if ( m_presentation.editorInTools && m_presentationRects.editorPane.w > 24.0f )
    {
        draw.Text( m_presentationRects.left.x + 12.0f, m_presentationRects.left.y + 52.0f, 11.0f, palette.textMuted.r,
                   palette.textMuted.g, palette.textMuted.b, "Editor controls are open in Tools." );
    }
}

void UIWindowInteractionOwner::DrawPresentedEditorPalette( const InGameUIFrameData& data )
{
    const UIPanelScope panelScope( m_frameDrawList, m_presentation.editorInTools ? UIPanel::Drawer : UIPanel::Left );
    if ( !m_presentationEnabled || m_presentation.workspace != Workspace::Scene || !data.editor.editorModeEnabled )
    {
        return;
    }
    const EditorMiniPaletteLayout layout = PresentedEditorPalette();
    if ( layout.buttonCount == 0 )
    {
        return;
    }
    const UIDrawContext draw( data.surface.screenW, data.surface.screenH, m_frameDrawList );
    draw.PushClip( layout.clip );
    draw.Text( layout.bounds.x, layout.bounds.y - 24.0f, 12.0f, Style::Palette().textSecondary.r,
               Style::Palette().textSecondary.g, Style::Palette().textSecondary.b, "Quick objects" );
    draw.PopClip();
    DrawEditorMiniPalette( draw, layout, data.editor.editorObjectType, data.editor.editorPlaceStatic, m_mouseX, m_mouseY,
                           m_editorMiniPalettePressedTreePlacement, m_editorMiniPalettePressedHoldMode,
                           m_editorMiniPalettePressedEntry, data.surface.screenW, data.surface.screenH );
}

void UIWindowInteractionOwner::DrawPresentationHeader( const InGameUIFrameData& data )
{
    const UIPanelScope panelScope( m_frameDrawList, UIPanel::Header );
    const auto& palette = Style::Palette();
    // Solver Lab always exposes its exit. Only Scene Canvas uses edge reveal.
    if ( !m_presentationEnabled ||
         ( m_presentation.workspace == Workspace::Scene && m_presentation.preferences.layout == LayoutMode::Canvas &&
           !m_presentationHeaderHovered && !m_cameraModeCombo.IsOpen() ) )
    {
        return;
    }
    const UIDrawContext draw( data.surface.screenW, data.surface.screenH, m_frameDrawList );
    const HeaderRects bounds = ComputeHeaderRects( m_presentationRects.header, m_presentation.workspace );
    draw.BeginLayer();
    // The shared compositor places Header above every workspace panel.
    draw.PushClip( m_presentationRects.header );
    draw.Rect( 0.0f, 0.0f, m_presentationRects.header.w, m_presentationRects.header.h, palette.window.r, palette.window.g,
               palette.window.b, 1.0f );
    draw.Rect( 0.0f, m_presentationRects.header.h - 1.0f, m_presentationRects.header.w, 1.0f, palette.border.r,
               palette.border.g, palette.border.b, 0.65f );
    if ( m_presentation.toolsOpen || bounds.skull.Contains( m_mouseX, m_mouseY ) )
    {
        const auto& fill = m_presentation.toolsOpen ? palette.selection : palette.controlHover;
        draw.RoundedRect( bounds.skull.x - 2.0f, bounds.skull.y - 2.0f, bounds.skull.w + 4.0f, bounds.skull.h + 4.0f, 5.0f,
                          fill.r, fill.g, fill.b, 1.0f );
    }
    DrawSkullLogo( draw, bounds.skull );
    const auto button = [&]( const UIRect& rect, const char* label )
    {
        if ( rect.w <= 0.0f )
        {
            return;
        }
        const auto& fill = rect.Contains( m_mouseX, m_mouseY ) ? palette.controlHover : palette.control;
        draw.RoundedRect( rect.x, rect.y, rect.w, rect.h, 4.0f, fill.r, fill.g, fill.b, 1.0f );
        draw.PushClip( rect );
        draw.Text( rect.x + ( rect.w < 50.0f ? 4.0f : 10.0f ), rect.y + 7.0f, 12.0f, palette.textPrimary.r,
                   palette.textPrimary.g, palette.textPrimary.b, label );
        draw.PopClip();
    };
    button( bounds.layout, m_presentation.preferences.layout == LayoutMode::Canvas
                               ? ( bounds.layout.w < 100.0f ? "Docked" : "Docked Interface" )
                               : ( bounds.layout.w < 100.0f ? "Full" : "Full Screen" ) );
    if ( bounds.close.w > 0.0f )
    {
        DrawTitleButton( draw, bounds.close, TitleButtonIcon::Close, bounds.close.Contains( m_mouseX, m_mouseY ), false );
    }
    button( bounds.workspace, bounds.workspace.w < 80.0f ? "Lab" : "Solver Lab" );
    auto scene = HeaderTitle( data.surface.sceneName );
    if ( bounds.scene.w > 16.0f )
    {
        Chrome::FitTitleText( scene.data(), scene.size(), 14.0f, bounds.scene.w - 16.0f );
        draw.PushClip( bounds.scene );
        draw.Text( bounds.scene.x + 8.0f, bounds.scene.y + 6.0f, 14.0f, palette.textPrimary.r, palette.textPrimary.g,
                   palette.textPrimary.b, scene.data() );
        draw.PopClip();
    }
    draw.PopClip();
    if ( m_presentation.workspace == Workspace::SolverLab )
    {
        draw.PushClip( bounds.camera );
        draw.Text( bounds.camera.x + 4.0f, bounds.camera.y + 8.0f, 11.0f, palette.textSecondary.r, palette.textSecondary.g,
                   palette.textSecondary.b, bounds.camera.w < 80.0f ? "Pair" : "Paired view" );
        draw.PopClip();
        return;
    }
    m_cameraModeCombo.SetLabelVisible( false );
    m_cameraModeCombo.SetBounds( bounds.camera.x, bounds.camera.y, bounds.camera.w, bounds.camera.h );
    m_cameraModeCombo.SetDropUp( false );
    const uint32_t disabled = ( ( 1u << CAMERA_MODE_OPTION_COUNT ) - 1u ) & ~data.surface.cameraModeEnabledMask;
    m_cameraModeCombo.Draw( draw, "",
                            { std::span<const char* const>( kCameraModeOptions ),
                              std::clamp( data.surface.cameraModeIndex, 0, CAMERA_MODE_OPTION_COUNT - 1 ), disabled },
                            { m_mouseX, m_mouseY } );
}

void UIWindowInteractionOwner::DrawToolsDrawerChrome( const UIDrawContext& draw, const UIRect& bounds )
{
    const auto& palette = Style::Palette();
    draw.Rect( bounds.x, bounds.y, bounds.w, bounds.h, palette.window.r, palette.window.g, palette.window.b, 1.0f );
    draw.Rect( bounds.x, bounds.y, bounds.w, 1.0f, palette.border.r, palette.border.g, palette.border.b, 0.5f );
    const ToolsChromeRects chrome = ComputeToolsChromeRects( bounds, true );
    const float logoY = bounds.y + ( chrome.compact ? 4.0f : 11.0f );
    DrawSkullLogo( draw, { bounds.x + 14.0f, logoY, 22.0f, 22.0f } );
    draw.Text( bounds.x + 46.0f, logoY + 4.0f, 13.0f, palette.textPrimary.r, palette.textPrimary.g, palette.textPrimary.b,
               "Tools" );
    const bool gripActive = m_presentationRects.drawerResize.Contains( m_mouseX, m_mouseY ) ||
                            ( m_interaction.isResizing && m_interaction.resizeRegion == 1 );
    const auto& grip = gripActive ? palette.accentStrong : palette.textMuted;
    draw.RoundedRect( bounds.x + bounds.w * 0.5f - 24.0f, bounds.y + 2.0f, 48.0f, 3.0f, 1.5f, grip.r, grip.g, grip.b, 1.0f );
    DrawTitleButton( draw, chrome.close, TitleButtonIcon::Close, chrome.close.Contains( m_mouseX, m_mouseY ), false );
}


void UIWindowInteractionOwner::DrawTooltips( const InGameUIFrameData& data )
{
    const bool sceneAvailable = m_window.isVisible && !m_window.isMinimized && m_activeTab == InGameUITab::Scene &&
                                !m_sceneTab.combo.IsOpen() && !m_sceneTab.recordingCombo.IsOpen() &&
                                !m_sceneTab.solverLabCombo.IsOpen();
    if ( !sceneAvailable && !m_presentationEnabled )
    {
        m_tooltip.Dismiss();
        return;
    }

    const UIRect window = Chrome::CurrentWindowRect( m_window, data.surface.now );
    const UIRect content = ComputeToolsChromeRects( window, m_presentationEnabled ).content;
    const HeaderRects header = ComputeHeaderRects( m_presentationRects.header, m_presentation.workspace );
    const bool solverLab = m_presentation.workspace == Workspace::SolverLab;
    const UITooltipTarget candidates[] =
        { { 1, m_sceneTab.resetSceneButton.Bounds(), { "Rebuild this scene while preserving live runtime controls." } },
          { 2, m_sceneTab.resetDefaultsButton.Bounds(), { "Discard live scene edits and reload authored defaults." } },
          { 3,
            m_sceneTab.saveDefaultsButton.Bounds(),
            { "Save current authored scene settings through scene persistence." } },
          { 4,
            m_sceneTab.combo.Bounds(),
            { "Filter and load a scene, return to Demo, or create a scene using the filter name." } },
          { 5,
            m_sceneTab.recordingCombo.Bounds(),
            { "Play an existing interaction recording from the recording catalog." } },
          { 6, m_sceneTab.solverLabCombo.Bounds(), { "Open a recorded physics comparison in Solver Lab." } },
          { 7,
            m_sceneTab.timeScaleSlider.Bounds(),
            { "Control how quickly the simulation advances.", "Multiplier of real time" } },
          { 8,
            m_sceneTab.predictionRevealSlider.Bounds(),
            { "Reveal an already-computed prediction at this speed.", "Multiplier; maximum reveals instantly" } },
          { 9, m_sceneTab.continuousForecastToggle.Bounds(), { "Start or stop the continuous orbital forecast." } },
          { 10, m_sceneTab.resetForecastButton.Bounds(), { "Restart the orbital forecast from the current scene state." } },
          { 11, header.skull, { "Open or close Tools while retaining the selected tab." } },
          { 12, header.scene, { "Open the existing Scene browser for loading, creation and scene defaults." } },
          { 13, header.scenes, { "Open the existing Scene browser for loading, creation and scene defaults." } },
          { 14, header.layout, { "Toggle full-screen game and the docked interface while retaining panel choices." } },
          { 15, header.tools, { "Open or close Tools while retaining the selected tab." } },
          { 16,
            DiagnosticDetailsBounds( m_presentationRects.markerHistory ),
            { "Open the detailed Profiler: marker hierarchy, timeline, workers and draw calls.", "Milliseconds",
              "F5 focuses marker history" } },
          { 17,
            DiagnosticDetailsBounds( m_presentationRects.memoryWaterline ),
            { "Open detailed Memory and replay retention controls.", "MiB", "F6 focuses memory waterline" } },
          { 18,
            m_presentationRects.replayDetails,
            { "Click to toggle Tools, or drag upward to open and resize the bottom drawer." } },
          { 19, m_presentationRects.editorTab, { "Show the editing controls. This does not enable editor mode." } },
          { 20, m_presentationRects.editorReplayTab, { "Show recording, prediction and replay controls." } },
          { 21, m_presentationRects.leftFold, { "Fold or reopen the left pane while preserving its controls and state." } },
          { 22,
            m_presentationRects.rightFold,
            { solverLab ? "Fold or reopen comparison differences while retaining the selected object."
                        : "Fold or reopen Causes while retaining selected evidence." } },
          { 23, m_presentationRects.leftResize, { "Drag to resize the left dock within the window." } },
          { 24, m_presentationRects.rightResize, { "Drag to resize the right dock within the window." } },
          { 26, header.workspace, { "Switch Scene and Solver Lab. Leaving Solver Lab pauses and retains its comparison." } },
          { 27, header.close, { "Exit Solver Lab and return to the full-screen game.", "", "Esc" } },
          { 25, header.camera, { "Choose a supported camera. Unavailable attached cameras are disabled for this scene." } },
          { 34, m_editorTab.terrainAlignToggle.Bounds(), { "Align newly placed objects to the terrain surface." } },
          { 30, m_editorTab.editorModeToggle.Bounds(), { "Enable or disable functional scene editing." } },
          { 31,
            m_editorTab.placementModeToggle.Bounds(),
            { "Switch between placing objects and selecting existing objects.", "", "", "Enable Editor mode first." },
            false,
            false,
            data.editor.editorModeEnabled },
          { 32, m_editorTab.staticObjectToggle.Bounds(), { "Choose whether newly placed objects are static." } },
          { 33, m_editorTab.objectCombo.Bounds(), { "Choose an object from the existing placement catalog." } } };
    UITooltipTarget target;
    for ( const auto& candidate : candidates )
    {
        const bool editorAvailable = m_presentationRects.editorControls.Contains( m_mouseX, m_mouseY ) ||
                                     ( m_window.isVisible && !m_window.isMinimized && m_activeTab == InGameUITab::Editor &&
                                       content.Contains( m_mouseX, m_mouseY ) );
        const bool available = !HasOpenPopup() && !( solverLab && candidate.id == 25 ) &&
                               ( candidate.id >= 30   ? editorAvailable
                                 : candidate.id >= 11 ? m_presentationEnabled
                                                      : sceneAvailable && content.Contains( m_mouseX, m_mouseY ) );
        if ( available && candidate.bounds.Contains( m_mouseX, m_mouseY ) )
        {
            target = candidate;
            target.hovered = true;
            break;
        }
    }
    if ( target.id == 0 && m_presentationEnabled && !solverLab && data.editor.editorModeEnabled && !HasOpenPopup() )
    {
        const EditorMiniPaletteLayout palette = PresentedEditorPalette();
        const int entry = HitEditorMiniPaletteButton( palette, m_mouseX, m_mouseY );
        if ( entry >= 0 )
        {
            target = { static_cast<uint32_t>( 100 + entry ),
                       palette.buttons[entry],
                       { EditorMiniPaletteEntryLabel( kEditorMiniPaletteEntries[entry] ), "",
                         kEditorMiniPaletteEntries[entry].holdMode == EDITOR_MINI_HOLD_MODE_NONE
                             ? "Click to select and enter placement"
                             : "Hold, move to a variant, then release" } };
            target.hovered = true;
        }
    }
    if ( target.id == 0 && !HasOpenPopup() )
    {
        target = FindToolsTooltip( content );
    }
    if ( target.id == 0 && !HasOpenPopup() )
    {
        for ( const UITooltipTarget& candidate : data.workspaceTooltips )
        {
            if ( candidate.id != 0 && ( candidate.bounds.Contains( m_mouseX, m_mouseY ) || candidate.focused ) )
            {
                target = candidate;
                target.hovered = candidate.bounds.Contains( m_mouseX, m_mouseY );
                break;
            }
        }
    }
    // Why: hover feedback uses elapsed presentation time even while simulation
    // playback is paused. The generic tooltip receives this explicit clock value.
    const double hoverNow = std::chrono::duration<double>( std::chrono::steady_clock::now().time_since_epoch() ).count();
    m_tooltip.Update( target, hoverNow, m_tooltipGestureActive );
    const UIDrawContext draw( data.surface.screenW, data.surface.screenH, m_frameDrawList );
    m_tooltip.Draw( draw,
                    { 0.0f, 0.0f, static_cast<float>( data.surface.screenW ), static_cast<float>( data.surface.screenH ) },
                    hoverNow );
}

void UIWindowInteractionOwner::DrawDiagnosticLinks( const InGameUIFrameData& data )
{
    const auto& palette = Style::Palette();
    if ( !m_presentationEnabled )
    {
        return;
    }
    const UIDrawContext draw( data.surface.screenW, data.surface.screenH, m_frameDrawList );
    const UIRect panels[] = { m_presentationRects.markerHistory, m_presentationRects.memoryWaterline };
    draw.BeginLayer();
    for ( int index = 0; index < 2; ++index )
    {
        const UIRect details = DiagnosticDetailsBounds( panels[index] );
        if ( details.w <= 0.0f )
        {
            continue;
        }
        if ( m_presentation.focusedDiagnostic == index + 1 )
        {
            draw.Outline( panels[index].x, panels[index].y, panels[index].w, panels[index].h, palette.accent.r,
                          palette.accent.g, palette.accent.b, 1.0f );
        }
        const auto& fill = details.Contains( m_mouseX, m_mouseY ) ? palette.controlHover : palette.control;
        draw.RoundedRect( details.x, details.y, details.w, details.h, 3.0f, fill.r, fill.g, fill.b, 1.0f );
        draw.Text( details.x + 10.0f, details.y + 5.0f, 11.0f, palette.textPrimary.r, palette.textPrimary.g,
                   palette.textPrimary.b, "Details" );
    }
}

const UIDrawList& UIWindowInteractionOwner::Draw( const InGameUIFrameData& data )
{
    m_frameDrawList.Clear();
    DrawPresentationDocks( data );
    DrawEditorDock( data );
    m_histogramDrawList.Clear();
    m_memoryOverlayDrawList.Clear();
    const bool histogramEnabled = ProfilerTab::PerformanceHistogramEnabled( m_profilerTab );
    const bool memoryOverlayEnabled = MemoryTab::OverlayEnabled( m_memoryOverlay );
    const auto finishDraw = [&]() -> const UIDrawList&
    {
        DrawPresentedEditorPalette( data );
        DrawDiagnosticLinks( data );
        DrawPresentationHeader( data );
        DrawTooltips( data );
        // Why: every exit path must publish capacity evidence. Hidden,
        // minimized, and cached frames are real retained-stream consumers too.
        PublishDrawStats( m_activeTab, m_frameDrawList, m_histogramDrawList, m_memoryOverlayDrawList );
        // App submits this bounded popup/tooltip stream after the workspace
        // presenters. Extraction copies text; neither stream borrows widgets.
        m_frameDrawList.ExtractForeground( m_foregroundDrawList );
        return m_frameDrawList;
    };

    // Why: input handling runs before the next draw, so the profiler tab keeps a
    // bounded copy of the latest frame snapshot for content height and hit tests.
    ProfilerTab::SetFrameSnapshot( m_profilerTab, data.diagnostics.profiler );

    if ( !m_window.isVisible && !histogramEnabled && !memoryOverlayEnabled )
    {
        return finishDraw();
    }

    const int screenW = (std::max)( 1, data.surface.screenW );
    const int screenH = (std::max)( 1, data.surface.screenH );
    m_lastScreenW = screenW;
    m_lastScreenH = screenH;
    m_lastModelCapacity = std::clamp( data.scene.modelCapacity, 1, SkullbonezCore::Scene::Capacity::MAX_SCENE_OBJECTS );
    m_lastSolverBallCount = std::clamp( data.scene.solverBallCount, UI_SOLVER_COUNT_MIN, m_lastModelCapacity );
    m_lastSolverBoxCount = std::clamp( data.scene.solverBoxCount, UI_SOLVER_COUNT_MIN, m_lastModelCapacity );
    m_lastMaxWorkerThreadCount = (std::max)( 1, data.surface.maxWorkerThreadCount );
    m_lastWorkerThreadCount = std::clamp( data.surface.workerThreadCount, 0, m_lastMaxWorkerThreadCount );
    m_lastRenderTargetPreviewCount = RenderTargetPreviewCount( data );
    m_lastRenderTargetDisabledMask = RenderTargetPreviewDisabledMask( data );
    m_selectedRenderTargetPreview = ResolveRenderTargetPreviewSelection( data, m_selectedRenderTargetPreview );

    if ( histogramEnabled )
    {
        ProfilerTab::PushPerformanceHistogramSample( m_profilerTab, data.ProfilerTabFrame() );
    }

    if ( memoryOverlayEnabled )
    {
        MemoryTab::PushOverlayFrame( m_memoryOverlay, data.MemoryTabFrame() );
    }

    if ( !m_window.isVisible )
    {
        AppendStandaloneOverlays( m_profilerTab, m_memoryOverlay, data, m_frameDrawList, m_histogramDrawList,
                                  m_memoryOverlayDrawList, screenW, screenH, histogramEnabled, memoryOverlayEnabled );
        return finishDraw();
    }

    if ( m_window.isMinimized )
    {
        m_cache.Reset();
        UIDrawList& drawList = m_cache.MutableDrawList();
        if ( !m_presentationEnabled )
        {
            DrawMinimizedContent( data, drawList, screenW, screenH );
        }
        m_frameDrawList.Append( drawList );
        AppendStandaloneOverlays( m_profilerTab, m_memoryOverlay, data, m_frameDrawList, m_histogramDrawList,
                                  m_memoryOverlayDrawList, screenW, screenH, histogramEnabled, memoryOverlayEnabled );
        return finishDraw();
    }

    PrepareForDraw( data.surface.now );
    PROFILE_BEGIN( "Frame/UI/Layout" );
    const UIRect windowBounds = Chrome::CurrentWindowRect( m_window, data.surface.now );
    const float x = windowBounds.x;
    const float y = windowBounds.y;
    const float w = windowBounds.w;
    const float h = windowBounds.h;
    const ToolsChromeRects chrome = ComputeToolsChromeRects( windowBounds, m_presentationEnabled );
    PrepareCompactToolsControls( chrome );
    const float titleH = chrome.title.h;
    const float tabH = chrome.tabs.h;
    const float bottomH = chrome.footer.h;
    const float contentX = chrome.content.x;
    const float contentY = chrome.content.y;
    const float contentW = chrome.content.w;
    const float contentH = chrome.content.h;
    const float scrolledY = contentY - m_scrollY;
    char titleText[192] = {};

    BuildWindowTitle( data, titleText, sizeof( titleText ) );
    const bool useTitleStats = !chrome.compact && w - 36.0f < 560.0f;
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
    ProfilerTab::ApplyDefaultExpansion( m_profilerTab );
    ProfilerTab::ApplyExpandAll( m_profilerTab );

    UICacheFrameKey cacheKey;
    cacheKey.screenW = screenW;
    cacheKey.screenH = screenH;
    cacheKey.windowBounds = windowBounds;
    cacheKey.activeTab = static_cast<int>( m_activeTab );
    cacheKey.scrollY = m_scrollY;
    cacheKey.blurEnabled = m_blurPreviewEnabled;
    cacheKey.contentSignature = BuildUIContentSignature( data );
    cacheKey.styleSignature = HashBool( HashBool( 2166136261u + static_cast<uint32_t>( Style::CurrentTheme() ),
                                                  m_blurPreviewEnabled ),
                                        m_hitboxOverlayEnabled );

    uint32_t openControls = 0u;
    openControls |= m_rendererCombo.IsOpen() ? UI_INTERACTION_RENDERER_OPEN : 0u;
    openControls |= m_reflectionCombo.IsOpen() ? UI_INTERACTION_REFLECTION_OPEN : 0u;
    openControls |= m_sceneTab.combo.IsOpen() ? UI_INTERACTION_SCENE_OPEN : 0u;
    openControls |= CinematicTab::IsComboOpen( m_cinematicTab ) ? UI_INTERACTION_CINEMATIC_SCENE_OPEN : 0u;
    openControls |= m_editorTab.objectCombo.IsOpen() ? UI_INTERACTION_EDITOR_OBJECT_OPEN : 0u;
    openControls |= m_renderTargetCombo.IsOpen() ? UI_INTERACTION_RENDER_TARGET_OPEN : 0u;
    openControls |= m_cameraModeCombo.IsOpen() ? UI_INTERACTION_CAMERA_MODE_OPEN : 0u;
    openControls |= m_toolsTabCombo.IsOpen() ? ( 1u << 16 ) : 0u;
    openControls |= m_toolsDisplayCombo.IsOpen() ? ( 1u << 17 ) : 0u;
    cacheKey.interactionSignature = BuildUIInteractionSignature(
        { { m_mouseX, m_mouseY }, windowBounds, openControls, m_selectedRenderTargetPreview, m_activeSlider } );

    // Why: Most UI frames only move the window/scroll offset. Replaying cached
    // draw commands keeps draw-call churn low while live render-target previews
    // still rebuild every frame.
    m_cache.BeginFrame( cacheKey );
    PROFILE_END( "Frame/UI/Layout" );

    const bool drawsLiveRenderTargetPreview = m_activeTab == InGameUITab::Targets;

    if ( !drawsLiveRenderTargetPreview && m_cache.CanReplayPositionOnly( cacheKey, m_interaction.isDragging ) )
    {
        const float replayOffsetX = m_cache.ReplayOffsetX( cacheKey );
        const float replayOffsetY = m_cache.ReplayOffsetY( cacheKey );
        m_frameDrawList.Append( m_cache.DrawList(), replayOffsetX, replayOffsetY );

        AppendStandaloneOverlays( m_profilerTab, m_memoryOverlay, data, m_frameDrawList, m_histogramDrawList,
                                  m_memoryOverlayDrawList, screenW, screenH, histogramEnabled, memoryOverlayEnabled );
        // Lifetime: the cached commands remain in their original coordinate
        // space. Retain that source key so consecutive drag offsets stay total,
        // then rebuild from current content when capture ends.
        return finishDraw();
    }

    UIDrawList& drawList = m_cache.MutableDrawList();
    drawList.Clear();
    drawList.SetPanel( UIPanel::Drawer );
    const UIDrawContext draw( screenW, screenH, drawList );
    PROFILE_BEGIN( "Frame/UI/DrawBuild" );

    const UIRect blurBounds = { x, y, w, h };
    PROFILE_BEGIN( "Frame/UI/Blur" );
    m_backdropBlur.Draw( draw, blurBounds, screenW, screenH, data.scene.currentFrame, data.surface.now,
                         m_blurPreviewEnabled );
    PROFILE_END( "Frame/UI/Blur" );

    if ( m_presentationEnabled )
    {
        DrawToolsDrawerChrome( draw, windowBounds );
    }
    else
    {
        Chrome::DrawWindowFrame( draw, windowBounds, titleH, tabH, m_blurPreviewEnabled, titleText );
    }
    const Chrome::TitleButtonRects titleButtons = Chrome::GetTitleButtonRects( windowBounds );
    if ( !m_presentationEnabled )
    {
        Chrome::DrawTitleButtons( draw, titleButtons, m_window.isMaximized, m_mouseX, m_mouseY );
    }
    const UIRect objectCounterAvoidBounds = TitleButtonGroupBounds( titleButtons );
    DrawEditorObjectCounter( draw, data, screenW, screenH, &objectCounterAvoidBounds );

    const int tabCount = static_cast<int>( InGameUITab::Count );
    static_assert( std::size( TOOL_NAMES ) == static_cast<size_t>( InGameUITab::Count ) );
    m_tabBar.SetBounds( chrome.tabs.x, chrome.tabs.y, chrome.tabs.w, chrome.tabs.h );
    if ( chrome.compact )
    {
        m_toolsTabCombo.Draw( draw, "", { std::span<const char* const>( TOOL_NAMES ), static_cast<int>( m_activeTab ) },
                              { m_mouseX, m_mouseY } );
    }
    else
    {
        m_tabBar.Draw( draw, TOOL_NAMES, tabCount, static_cast<int>( m_activeTab ) );
    }

    const Style::UIPalette& palette = Style::Palette();
    const float inset = chrome.compact ? 2.0f : 10.0f;
    draw.RoundedPanel( { contentX - inset, contentY - inset, contentW + inset * 2.0f, contentH + inset + 2.0f },
                       Style::Radii().window, palette.windowSubtle, palette.innerBorder );

    // Invariant: scrolling content cannot paint over the drawer footer. Popup
    // commands are extracted into the foreground stream after this clipped pass.
    draw.PushClip( { contentX, contentY, contentW, contentH } );
    DrawActiveTabContent( data, draw, drawList, { contentX, contentY, contentW, contentH }, scrolledY );
    draw.PopClip();

    m_scrollBar.SetBounds( x + w - 14.0f, contentY, 4.0f, contentH );
    m_scrollBar.Draw( draw, static_cast<float>( ContentHeight() ), contentH, m_scrollY, m_scrollbarVisibleUntil,
                      data.surface.now );

    const UIRect footerBounds = DrawFooterContent( data, draw, x, y, w, h, bottomH, titleStatW, titleStatX, titleStat );
    DrawHitboxOverlay( draw, data, windowBounds, { contentX, contentY, contentW, contentH }, footerBounds );

    PROFILE_END( "Frame/UI/DrawBuild" );
    m_frameDrawList.Append( drawList );
    AppendStandaloneOverlays( m_profilerTab, m_memoryOverlay, data, m_frameDrawList, m_histogramDrawList,
                              m_memoryOverlayDrawList, screenW, screenH, histogramEnabled, memoryOverlayEnabled );

    if ( drawsLiveRenderTargetPreview )
    {
        m_cache.Reset();
    }
    else
    {
        m_cache.StoreFrame( cacheKey );
    }

    return finishDraw();
}
