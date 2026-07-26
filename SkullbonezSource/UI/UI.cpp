/*
File: SkullbonezSource/UI/UI.cpp
Purpose:
  Composes in-engine UI drawing and preserves the public InGameUI command surface.

Summary:
  UI.cpp records the current typed widget view into one complete ordered frame
  and delegates persistent window/input state to UIWindowInteractionOwner.
  Public wrappers preserve existing call sites while authority lives in the
  concrete interaction owner.

Glossary:
  Draw command: Lightweight record describing a UI shape or text batch to
  render later in the frame.
  Retained draw stream: Fixed-capacity command/text storage reused across
    frames by one UI owner.
  Hit box: Screen-space rectangle used to decide whether mouse input targets a
    widget.
  Widget view: Synchronous typed references borrowed from the interaction owner
    so drawing uses the exact controls whose bounds were hit-tested.

Invariants:
  - Draw geometry and hit testing must be derived from the same layout
    constants.
  - InGameUI never reaches into owner storage through friendship or a retained
    pointer; WidgetView is borrowed only inside the current draw call.
  - Draw builds values only; Runtime/Render performs every flush, preview
    resolution, resource operation, and GPU timing scope.

Related:
  - SkullbonezSource/UI/UI.h
  - SkullbonezSource/UI/UIWindowInteractionOwner.h
  - Agentic/Reference/comment-style-guide.md
*/
#include "UI.h"
#include "UIFrameComposition.h"
#include "UIFontMetrics.h"
#include "../Core/Profiler.h"
#include "UIDraw.h"
#include "UIDrawList.h"
#include "UIDrawWidgets.h"
#include "UIInput.h"
#include "UILayout.h"
#include "UITabControls.h"
#include "UITabEditor.h"
#include "UITabMemory.h"
#include "UITabOptions.h"
#include "UITabPhysics.h"
#include "UITabProfiler.h"
#include "UITabScene.h"
#include "UIStyle.h"
#include "UIWindowChrome.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>

using namespace SkullbonezCore::UI;
using namespace SkullbonezCore::UI::Widgets;
using namespace SkullbonezCore::UI::Layout;
using namespace SkullbonezCore::UI::FrameComposition;


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
void InGameUI::ToggleMaximizeMinimize( int screenW, int screenH, double now )
{
    m_windowInteraction.ToggleMaximizeMinimize( screenW, screenH, now );
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
bool InGameUI::IsPerformanceHistogramEnabled() const
{
    return m_windowInteraction.IsPerformanceHistogramEnabled();
}
void InGameUI::TogglePerformanceHistogramEnabled()
{
    m_windowInteraction.TogglePerformanceHistogramEnabled();
}
void InGameUI::SetMemoryOverlayEnabled( bool enabled )
{
    m_windowInteraction.SetMemoryOverlayEnabled( enabled );
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
void InGameUI::DrawHitboxOverlay( const UIDrawContext& draw, const InGameUIFrameData& data, const UIRect& windowBounds,
                                  const UIRect& contentBounds, const UIRect& footerBounds )
{
    auto widgets = m_windowInteraction.Widgets();

    if ( !widgets.hitboxOverlayEnabled )
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

    if ( !widgets.window.isMaximized )
    {
        DrawHitboxRect( draw,
                        { windowBounds.x + windowBounds.w - 26.0f, windowBounds.y + windowBounds.h - 26.0f, 26.0f, 26.0f },
                        chromeR, chromeG, chromeB, 0.050f, 0.86f );
    }

    DrawTabHitboxes( draw, widgets.tabBar, static_cast<int>( InGameUITab::Count ) );
    DrawHitboxRect( draw, contentBounds, contentR, contentG, contentB, 0.018f, 0.48f );

    switch ( widgets.activeTab )
    {
    case InGameUITab::Scene:
        DrawComboHitboxes( draw, widgets.sceneTab.combo, SceneDropdownHitboxOptionCount( widgets.sceneTab, data ), contentR,
                           contentG, contentB );

        DrawHitboxRect( draw, widgets.sceneTab.resetSceneButton.Bounds(), buttonR, buttonG, buttonB );
        DrawHitboxRect( draw, widgets.sceneTab.resetDefaultsButton.Bounds(), buttonR, buttonG, buttonB );
        DrawHitboxRect( draw, widgets.sceneTab.saveDefaultsButton.Bounds(), buttonR, buttonG, buttonB );
        DrawHitboxRect( draw, widgets.sceneTab.timeScaleSlider.Bounds(), contentR, contentG, contentB );
        break;
    case InGameUITab::Editor:
        DrawHitboxRect( draw, widgets.editorTab.editorModeToggle.Bounds(), contentR, contentG, contentB );
        DrawHitboxRect( draw, widgets.editorTab.placementModeToggle.Bounds(), contentR, contentG, contentB );
        DrawHitboxRect( draw, widgets.editorTab.staticObjectToggle.Bounds(), contentR, contentG, contentB );
        DrawComboHitboxes( draw, widgets.editorTab.objectCombo, EditorTab::OBJECT_TYPE_COUNT, contentR, contentG, contentB );

        break;
    case InGameUITab::Physics:

        for ( int i = 0; i < 13; ++i )
        {
            DrawHitboxRect( draw, widgets.physicsTab.toggles[i].Bounds(), contentR, contentG, contentB );
        }

        DrawHitboxRect( draw, widgets.physicsTab.pipelinePrevButton, buttonR, buttonG, buttonB );
        DrawHitboxRect( draw, widgets.physicsTab.pipelineNextButton, buttonR, buttonG, buttonB );
        DrawHitboxRect( draw, widgets.physicsTab.alphaSlider.Bounds(), contentR, contentG, contentB );
        DrawHitboxRect( draw, widgets.physicsTab.contactLingerSlider.Bounds(), contentR, contentG, contentB );
        DrawHitboxRect( draw, widgets.physicsTab.rayImpulseSlider.Bounds(), contentR, contentG, contentB );
        DrawHitboxRect( draw, widgets.physicsTab.launcherProjectileSpeedSlider.Bounds(), contentR, contentG, contentB );
        DrawHitboxRect( draw, widgets.physicsTab.worldGravitySlider.Bounds(), contentR, contentG, contentB );
        DrawHitboxRect( draw, widgets.physicsTab.terrainFrictionSlider.Bounds(), contentR, contentG, contentB );
        DrawHitboxRect( draw, widgets.physicsTab.objectFrictionSlider.Bounds(), contentR, contentG, contentB );
        DrawHitboxRect( draw, widgets.physicsTab.rollingFrictionSlider.Bounds(), contentR, contentG, contentB );
        DrawHitboxRect( draw, widgets.physicsTab.tornadoRadiusSlider.Bounds(), contentR, contentG, contentB );
        DrawHitboxRect( draw, widgets.physicsTab.tornadoHeightSlider.Bounds(), contentR, contentG, contentB );
        DrawHitboxRect( draw, widgets.physicsTab.tornadoInwardSlider.Bounds(), contentR, contentG, contentB );
        DrawHitboxRect( draw, widgets.physicsTab.tornadoSwirlSlider.Bounds(), contentR, contentG, contentB );
        DrawHitboxRect( draw, widgets.physicsTab.tornadoLiftSlider.Bounds(), contentR, contentG, contentB );
        break;
    case InGameUITab::Options:

        for ( int i = 0; i < 6; ++i )
        {
            DrawHitboxRect( draw, widgets.optionsTab.toggles[i].Bounds(), contentR, contentG, contentB );
        }

        DrawHitboxRect( draw, widgets.optionsTab.timeScaleSlider.Bounds(), contentR, contentG, contentB );
        DrawHitboxRect( draw, widgets.optionsTab.modelCountSlider.Bounds(), contentR, contentG, contentB );
        break;
    case InGameUITab::Render:
        DrawHitboxRect( draw, widgets.renderShadowToggle.Bounds(), contentR, contentG, contentB );
        DrawHitboxRect( draw, widgets.saveRenderDefaultsButton.Bounds(), contentR, contentG, contentB );

        for ( int i = 0; i < static_cast<int>( UIRenderParam::Count ); ++i )
        {
            DrawHitboxRect( draw, widgets.renderSliders[i].Bounds(), contentR, contentG, contentB );
        }

        break;
    case InGameUITab::Targets:
        DrawComboHitboxes( draw, widgets.renderTargetCombo, widgets.lastRenderTargetPreviewCount, contentR, contentG,
                           contentB );

        break;
    case InGameUITab::Keys:
        DrawHitboxRect( draw, widgets.controlsTab.seedSlider.Bounds(), contentR, contentG, contentB );
        DrawHitboxRect( draw, widgets.controlsTab.solverBallSlider.Bounds(), contentR, contentG, contentB );
        DrawHitboxRect( draw, widgets.controlsTab.solverBoxSlider.Bounds(), contentR, contentG, contentB );
        DrawHitboxRect( draw, widgets.controlsTab.worldFluidHeightSlider.Bounds(), contentR, contentG, contentB );
        DrawHitboxRect( draw, widgets.controlsTab.worldFluidDensitySlider.Bounds(), contentR, contentG, contentB );
        break;
    case InGameUITab::Sky:
        SkyTab::DrawHitboxes( widgets.skyTab, draw, contentR, contentG, contentB );
        break;
    case InGameUITab::Cinematic:
        CinematicTab::DrawHitboxes( widgets.cinematicTab, draw, data, contentR, contentG, contentB );
        break;
    case InGameUITab::Profiler:
        DrawHitboxRect( draw, widgets.profilerTab.workerToggle.Bounds(), contentR, contentG, contentB );
        DrawHitboxRect( draw, widgets.profilerTab.workerThreadSlider.Bounds(), contentR, contentG, contentB );
        break;
    case InGameUITab::Memory:
        break;
    default:
        break;
    }

    if ( m_windowInteraction.ContentHeight() > static_cast<int>( contentBounds.h ) )
    {
        DrawHitboxRect( draw, widgets.scrollBar.Bounds(), 0.18f, 0.82f, 0.95f, 0.060f, 0.86f );
    }

    DrawHitboxRect( draw, footerBounds, footerR, footerG, footerB, 0.020f, 0.54f );
    DrawComboHitboxes( draw, widgets.rendererCombo, 1, footerR, footerG, footerB );
    DrawComboHitboxes( draw, widgets.reflectionCombo, 3, footerR, footerG, footerB );
    DrawHitboxRect( draw, widgets.blurToggle.Bounds(), footerR, footerG, footerB );
    DrawHitboxRect( draw, widgets.vsyncToggle.Bounds(), footerR, footerG, footerB );
    DrawHitboxRect( draw, widgets.histogramToggle.Bounds(), footerR, footerG, footerB );
    DrawHitboxRect( draw, widgets.timelineToggle.Bounds(), footerR, footerG, footerB );
    DrawHitboxRect( draw, widgets.hitboxToggle.Bounds(), footerR, footerG, footerB );
}


InputControl::UIPointerOverride InGameUI::InputOverride() const
{
    return m_windowInteraction.InputOverride();
}


InGameUIInputResult InGameUI::UpdateInput( const InputControl::UIInputSnapshot& input, int screenWidth, int screenHeight,
                                           double now, bool editorModeEnabled, bool editorPlacementMode,
                                           bool editorPlaceStatic, bool editorTerrainAlign, int cameraModeIndex,
                                           uint32_t cameraModeEnabledMask, std::span<const char* const> sceneOptions,
                                           int selectedSceneOption )
{
    PROFILE_SCOPED( m_profiler, "Frame/UI/Input" );
    return m_windowInteraction.UpdateInput( input, screenWidth, screenHeight, now, editorModeEnabled, editorPlacementMode,
                                            editorPlaceStatic, editorTerrainAlign, cameraModeIndex, cameraModeEnabledMask,
                                            sceneOptions, selectedSceneOption );
}
const UIDrawList& InGameUI::Draw( const InGameUIFrameData& data )
{
    m_frameDrawList.Clear();
    m_histogramDrawList.Clear();
    m_memoryOverlayDrawList.Clear();
    auto widgets = m_windowInteraction.Widgets();
    const bool histogramEnabled = ProfilerTab::PerformanceHistogramEnabled( widgets.profilerTab );
    const bool memoryOverlayEnabled = MemoryTab::OverlayEnabled( widgets.memoryOverlay );
    const auto finishDraw = [&]() -> const UIDrawList&
    {

        // Why: every exit path must publish capacity evidence. Hidden,
        // minimized, and cached frames are real retained-stream consumers too.
        char drawStatsFlag[2] = {};

        size_t drawStatsFlagLength = 0;
        const bool drawStatsRequested = getenv_s( &drawStatsFlagLength, drawStatsFlag, sizeof( drawStatsFlag ),
                                                  "SKORE_UI_DRAW_STATS" ) == 0 &&
                                        drawStatsFlag[0] != '\0';

        if ( drawStatsRequested )
        {
            const UIDrawList::Stats frameStats = m_frameDrawList.GetStats();
            const UIDrawList::Stats histogramStats = m_histogramDrawList.GetStats();
            const UIDrawList::Stats memoryStats = m_memoryOverlayDrawList.GetStats();
            const auto overflowed = []( const UIDrawList::Stats& stats )
            { return stats.commandOverflow || stats.textOverflow || stats.clipOverflow; };

            const bool overflow = overflowed( frameStats ) || overflowed( histogramStats ) || overflowed( memoryStats );

            std::fprintf( stderr,
                          "[ui-draw-stats] tab=%d frame=%d/%d histogram=%d/%d memory=%d/%d clip=%d/%d/%d overflow=%d\n",
                          static_cast<int>( widgets.activeTab ), frameStats.commandCount, frameStats.textBytes,
                          histogramStats.commandCount, histogramStats.textBytes, memoryStats.commandCount,
                          memoryStats.textBytes, frameStats.maxClipDepth, histogramStats.maxClipDepth,
                          memoryStats.maxClipDepth, overflow ? 1 : 0 );
        }

        return m_frameDrawList;
    };

    // Why: input handling runs before the next draw, so the profiler tab keeps a
    // bounded copy of the latest frame snapshot for content height and hit tests.
    ProfilerTab::SetFrameSnapshot( widgets.profilerTab, data.profiler );

    if ( !widgets.window.isVisible && !histogramEnabled && !memoryOverlayEnabled )
    {
        return finishDraw();
    }

    const int screenW = (std::max)( 1, data.screenW );
    const int screenH = (std::max)( 1, data.screenH );
    widgets.lastScreenW = screenW;
    widgets.lastScreenH = screenH;
    widgets.lastModelCapacity = std::clamp( data.modelCapacity, 1, SkullbonezCore::Scene::Capacity::MAX_SCENE_OBJECTS );
    widgets.lastSolverBallCount = std::clamp( data.solverBallCount, UI_SOLVER_COUNT_MIN, widgets.lastModelCapacity );
    widgets.lastSolverBoxCount = std::clamp( data.solverBoxCount, UI_SOLVER_COUNT_MIN, widgets.lastModelCapacity );
    widgets.lastMaxWorkerThreadCount = (std::max)( 1, data.maxWorkerThreadCount );
    widgets.lastWorkerThreadCount = std::clamp( data.workerThreadCount, 0, widgets.lastMaxWorkerThreadCount );
    widgets.lastRenderTargetPreviewCount = RenderTargetPreviewCount( data );
    widgets.lastRenderTargetDisabledMask = RenderTargetPreviewDisabledMask( data );
    widgets.selectedRenderTargetPreview = ResolveRenderTargetPreviewSelection( data, widgets.selectedRenderTargetPreview );

    auto drawHistogramOverlay = [&]()
    {

        if ( !histogramEnabled )
        {
            return;
        }

        // Why: the main diagnostics window can replay cached draw commands, but
        // marker samples, selector state, and drag/resize feedback must rebuild
        // every frame.
        m_histogramDrawList.Clear();
        const UIDrawContext histogramDraw( screenW, screenH, m_histogramDrawList );
        ProfilerTab::DrawPerformanceHistogram( widgets.profilerTab, histogramDraw, data );
        m_frameDrawList.Append( m_histogramDrawList );
    };

    auto drawMemoryOverlay = [&]()
    {

        if ( !memoryOverlayEnabled )
        {
            return;
        }

        // Why: allocator events are retained by memory level, not by sample
        // index. Draw it after the F5 panel so the F6 panel can anchor under
        // the CPU histogram's current position when both are visible.
        m_memoryOverlayDrawList.Clear();
        const UIDrawContext memoryDraw( screenW, screenH, m_memoryOverlayDrawList );
        const float memoryX = histogramEnabled ? widgets.profilerTab.histogramPanelX : 16.0f;
        const float memoryY = histogramEnabled
                                  ? widgets.profilerTab.histogramPanelY + widgets.profilerTab.histogramPanelH + 8.0f
                                  : 16.0f;

        MemoryTab::DrawOverlay( widgets.memoryOverlay, memoryDraw, data, memoryX, memoryY );
        m_frameDrawList.Append( m_memoryOverlayDrawList );
    };

    auto drawStandaloneOverlays = [&]()
    {
        drawHistogramOverlay();

        drawMemoryOverlay();
    };

    if ( histogramEnabled )
    {
        ProfilerTab::PushPerformanceHistogramSample( widgets.profilerTab, data );
    }

    if ( memoryOverlayEnabled )
    {
        MemoryTab::PushOverlayFrame( widgets.memoryOverlay, data );
    }

    if ( !widgets.window.isVisible )
    {
        drawStandaloneOverlays();
        return finishDraw();
    }

    if ( widgets.window.isMinimized )
    {
        widgets.cache.Reset();
        UIDrawList& drawList = widgets.cache.MutableDrawList();
        drawList.Clear();
        const UIDrawContext draw( screenW, screenH, drawList );

        if ( widgets.window.animationActive && widgets.window.animationToMinimized )
        {
            const UIRect animBounds = Chrome::CurrentWindowRect( widgets.window, data.now );

            if ( widgets.window.animationActive )
            {
                Chrome::DrawWindowAnimationShell( draw, animBounds );
                m_frameDrawList.Append( drawList );
                drawStandaloneOverlays();
                return finishDraw();
            }
        }

        char titleText[192] = {};
        Chrome::BuildWindowTitle( data, titleText, sizeof( titleText ) );

        if ( !data.editorModeEnabled )
        {
            StripMinimizedRuntimeModeSuffix( data, titleText, sizeof( titleText ) );
        }

        widgets.window.minimizedWidth = data.editorModeEnabled
                                            ? EditorMinimizedWidth( data, screenW )
                                            : (std::min)( MinimizedWidthWithCameraModeCombo( titleText, screenW ),
                                                          MINIMIZED_RUN_MAX_W );

        const UIRect minimized = MinimizedRect( screenW, screenH, widgets.window.minimizedWidth );

        if ( data.editorModeEnabled )
        {
            const EditorMiniPaletteLayout
                editorMiniPalette = BuildEditorMiniPaletteLayout( screenW, screenH, minimized,
                                                                  widgets.editorMiniPalettePressedEntry,
                                                                  widgets.editorMiniPaletteFlyoutOpen );

            DrawEditorMiniPalette( draw, editorMiniPalette, data.editorObjectType, data.editorPlaceStatic, widgets.mouseX,
                                   widgets.mouseY, widgets.editorMiniPalettePressedTreePlacement,
                                   widgets.editorMiniPalettePressedHoldMode, widgets.editorMiniPalettePressedEntry, screenW,
                                   screenH );

            DrawEditorMinimizedWindow( draw, minimized, data, widgets.mouseX, widgets.mouseY );
        }
        else
        {
            const UIRect cameraModeComboBounds = MinimizedCameraModeComboBounds( minimized );
            widgets.cameraModeCombo.SetLabelVisible( false );
            widgets.cameraModeCombo.SetBounds( cameraModeComboBounds.x, cameraModeComboBounds.y, cameraModeComboBounds.w,
                                               cameraModeComboBounds.h );

            widgets.cameraModeCombo.SetDropUp( true );
            const float titleMaxW = (std::max)( 40.0f, cameraModeComboBounds.x - ( minimized.x + 32.0f ) -
                                                           MINIMIZED_CAMERA_MODE_GAP );

            Chrome::FitTitleText( titleText, sizeof( titleText ), 12.5f, titleMaxW );
            Chrome::DrawMinimizedWindow( draw, minimized, titleText );
            const int cameraModeIndex = std::clamp( data.cameraModeIndex, 0, CAMERA_MODE_OPTION_COUNT - 1 );
            const uint32_t cameraModeDisabledMask = ( ( 1u << CAMERA_MODE_OPTION_COUNT ) - 1u ) &
                                                    ~( data.cameraModeEnabledMask &
                                                       ( ( 1u << CAMERA_MODE_OPTION_COUNT ) - 1u ) );

            widgets.cameraModeCombo.Draw( draw, "", kCameraModeOptions, CAMERA_MODE_OPTION_COUNT, cameraModeIndex,
                                          widgets.mouseX, widgets.mouseY, cameraModeDisabledMask );
        }

        DrawEditorObjectCounter( draw, data, screenW, screenH );
        m_frameDrawList.Append( drawList );

        drawStandaloneOverlays();
        return finishDraw();
    }

    PROFILE_BEGIN( m_profiler, "Frame/UI/Layout" );
    const UIRect windowBounds = Chrome::CurrentWindowRect( widgets.window, data.now );
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
    const float scrolledY = contentY - widgets.scrollY;
    char titleText[192] = {};

    Chrome::BuildWindowTitle( data, titleText, sizeof( titleText ) );
    const bool useTitleStats = w - 36.0f < 560.0f;
    char titleStat[32] = {};

    float titleStatW = 0.0f;
    float titleStatX = 0.0f;
    float titleMaxW = w - 150.0f;

    if ( useTitleStats )
    {
        snprintf( titleStat, sizeof( titleStat ), "%.0f FPS", data.fps );
        titleStatW = UIFontMetrics::MeasureText( 10.5f, titleStat );
        titleStatX = (std::max)( x + 148.0f, x + w - 128.0f - titleStatW );
        titleMaxW = titleStatX - ( x + 20.0f ) - 10.0f;
    }

    Chrome::FitTitleText( titleText, sizeof( titleText ), 15.5f, (std::max)( 40.0f, titleMaxW ) );
    ProfilerTab::ApplyDefaultExpansion( widgets.profilerTab );
    ProfilerTab::ApplyExpandAll( widgets.profilerTab );

    UICacheFrameKey cacheKey;
    cacheKey.screenW = screenW;
    cacheKey.screenH = screenH;
    cacheKey.windowBounds = windowBounds;
    cacheKey.activeTab = static_cast<int>( widgets.activeTab );
    cacheKey.scrollY = widgets.scrollY;
    cacheKey.blurEnabled = widgets.blurPreviewEnabled;
    cacheKey.contentSignature = BuildUIContentSignature( data );
    cacheKey.styleSignature = HashBool( HashBool( 2166136261u, widgets.blurPreviewEnabled ), widgets.hitboxOverlayEnabled );

    cacheKey.interactionSignature = BuildUIInteractionSignature( widgets.mouseX, widgets.mouseY,
                                                                 widgets.rendererCombo.IsOpen(),
                                                                 widgets.reflectionCombo.IsOpen(),
                                                                 widgets.sceneTab.combo.IsOpen(),
                                                                 CinematicTab::IsComboOpen( widgets.cinematicTab ),
                                                                 widgets.editorTab.objectCombo.IsOpen(),
                                                                 widgets.renderTargetCombo.IsOpen(),
                                                                 widgets.cameraModeCombo.IsOpen(),
                                                                 widgets.selectedRenderTargetPreview, widgets.activeSlider );

    // Why: Most UI frames only move the window/scroll offset. Replaying cached
    // draw commands keeps draw-call churn low while live render-target previews
    // still rebuild every frame.
    widgets.cache.BeginFrame( cacheKey );
    PROFILE_END( m_profiler, "Frame/UI/Layout" );

    const bool drawsLiveRenderTargetPreview = widgets.activeTab == InGameUITab::Targets;

    if ( !drawsLiveRenderTargetPreview && widgets.cache.CanReplayPositionOnly( cacheKey ) )
    {
        const float replayOffsetX = widgets.cache.ReplayOffsetX( cacheKey );
        const float replayOffsetY = widgets.cache.ReplayOffsetY( cacheKey );
        m_frameDrawList.Append( widgets.cache.DrawList(), replayOffsetX, replayOffsetY );

        drawStandaloneOverlays();
        widgets.cache.StoreFrame( cacheKey );
        return finishDraw();
    }

    UIDrawList& drawList = widgets.cache.MutableDrawList();
    drawList.Clear();
    const UIDrawContext draw( screenW, screenH, drawList );
    PROFILE_BEGIN( m_profiler, "Frame/UI/DrawBuild" );

    const UIRect blurBounds = { x, y, w, h };
    PROFILE_BEGIN( m_profiler, "Frame/UI/Blur" );
    widgets.backdropBlur.Draw( draw, blurBounds, screenW, screenH, data.currentFrame, data.now, widgets.blurPreviewEnabled );
    PROFILE_END( m_profiler, "Frame/UI/Blur" );

    Chrome::DrawWindowFrame( draw, windowBounds, titleH, tabH, widgets.blurPreviewEnabled, titleText );
    const Chrome::TitleButtonRects titleButtons = Chrome::GetTitleButtonRects( windowBounds );
    Chrome::DrawTitleButtons( draw, titleButtons, widgets.window.isMaximized, widgets.mouseX, widgets.mouseY );
    const UIRect objectCounterAvoidBounds = TitleButtonGroupBounds( titleButtons );
    DrawEditorObjectCounter( draw, data, screenW, screenH, &objectCounterAvoidBounds );

    static const char* kTabs[] = { "Prof",    "Scene", "Edit", "Phys", "Opt", "Render",
                                   "Targets", "Ctrl",  "Sky",  "Cine", "Mem" };

    const int tabCount = static_cast<int>( InGameUITab::Count );
    const float tabPad = 14.0f;
    widgets.tabBar.SetBounds( x + tabPad, y + titleH, w - tabPad * 2.0f, tabH );
    widgets.tabBar.Draw( draw, kTabs, tabCount, static_cast<int>( widgets.activeTab ) );

    const Style::UIPalette& palette = Style::Palette();
    draw.RoundedPanel( { contentX - 10.0f, contentY - 10.0f, contentW + 20.0f, contentH + 12.0f }, Style::Radii().window,
                       palette.windowSubtle, palette.innerBorder );

    if ( widgets.activeTab == InGameUITab::Profiler )
    {
        ProfilerTab::Draw( widgets.profilerTab, draw, data, contentX, contentY, contentW, contentH, widgets.scrollY,
                           widgets.activeSlider );
    }
    else if ( widgets.activeTab == InGameUITab::Memory )
    {
        MemoryTab::Draw( draw, widgets.memoryOverlay, data, contentX, contentY, contentW, contentH, scrolledY,
                         widgets.activeSlider, widgets.mouseX, widgets.mouseY );
    }
    else if ( widgets.activeTab == InGameUITab::Scene )
    {
        SceneTab::Draw( widgets.sceneTab, draw, data, contentX, contentY, contentW, contentH, scrolledY, widgets.mouseX,
                        widgets.mouseY );
    }
    else if ( widgets.activeTab == InGameUITab::Physics )
    {
        PhysicsTab::Draw( widgets.physicsTab, draw, data, contentX, contentY, contentW, contentH, scrolledY,
                          widgets.activeSlider, widgets.mouseX, widgets.mouseY );
    }
    else if ( widgets.activeTab == InGameUITab::Editor )
    {
        EditorTab::Draw( widgets.editorTab, draw, data, contentX, contentY, contentW, contentH, scrolledY, widgets.mouseX,
                         widgets.mouseY );
    }
    else if ( widgets.activeTab == InGameUITab::Options )
    {
        OptionsTab::Draw( widgets.optionsTab, draw, data, contentX, contentY, contentW, contentH, scrolledY,
                          widgets.activeSlider );
    }
    else if ( widgets.activeTab == InGameUITab::Render )
    {
        char buf[128];
        const float colW = (std::max)( 148.0f, contentW * 0.46f );
        DrawSectionTitle( draw, contentX, contentY, contentH, scrolledY + 16.0f, 16.0f, "Render" );
        DrawContentToggle( draw, contentY, contentH, widgets.renderShadowToggle, contentX,
                           scrolledY + UI_RENDER_FEATURE_START_Y, colW, "Shadows", data.ordinaryRender.shadow.enabled );

        widgets.saveRenderDefaultsButton.SetBounds( contentX + contentW - UI_RENDER_SAVE_BUTTON_W,
                                                    scrolledY + UI_RENDER_FEATURE_START_Y, UI_RENDER_SAVE_BUTTON_W, 24.0f );

        if ( IsRowVisible( contentY, contentH, scrolledY + UI_RENDER_FEATURE_START_Y, 24.0f ) )
        {
            widgets.saveRenderDefaultsButton.Draw( draw, "Save CFG", widgets.mouseX, widgets.mouseY );
        }

        static constexpr const char* visibilityLabels[] = { "Main", "Reflection", "Terrain shadow", "Object shadow" };
        char visibilityText[96];

        for ( int viewIndex = 0; viewIndex < static_cast<int>( UIRenderVisibilityView::Count ); ++viewIndex )
        {
            const UIRenderVisibilityViewStats& visibility = data.visibility.views[viewIndex];
            snprintf( visibilityText, sizeof( visibilityText ), "%d submitted, %d culled, %d draws", visibility.submitted,
                      visibility.culled, visibility.draws );

            DrawLabelValueAt( draw, contentY, contentH, contentX,
                              scrolledY + 76.0f + static_cast<float>( viewIndex ) * 18.0f, visibilityLabels[viewIndex],
                              visibilityText, palette.accent.r, palette.accent.g, palette.accent.b );
        }

        const float baseY = scrolledY + UI_RENDER_START_Y;

        for ( int i = 0; i < static_cast<int>( UIRenderParam::Count ); ++i )
        {
            const RenderSliderSpec& spec = kRenderSliderSpecs[i];
            const float sliderY = RenderSliderY( i, baseY );

            if ( RenderSliderStartsSection( i ) &&
                 IsRowVisible( contentY, contentH, sliderY - UI_RENDER_SECTION_H + 4.0f, 18.0f ) )
            {
                DrawSectionTitle( draw, contentX, contentY, contentH, sliderY - UI_RENDER_SECTION_H + 4.0f, 12.0f,
                                  UIRenderAuthoringSectionName( spec.section ) );

                if ( spec.section == UIRenderAuthoringSection::PredictionPaths )
                {
                    widgets.saveTrajectoryStyleButton.SetBounds( contentX + contentW - UI_TRAJECTORY_SAVE_BUTTON_W,
                                                                 sliderY - UI_RENDER_SECTION_H + 1.0f,
                                                                 UI_TRAJECTORY_SAVE_BUTTON_W, 20.0f );

                    widgets.saveTrajectoryStyleButton.Draw( draw, "Save Paths", widgets.mouseX, widgets.mouseY );
                }
            }

            const float value = std::clamp( RenderValueForParam( data.ordinaryRender, spec.param ), spec.minValue,
                                            spec.maxValue );

            snprintf( buf, sizeof( buf ), spec.valueFormat, value );
            widgets.renderSliders[i].SetBounds( contentX, sliderY, contentW, 34.0f );

            if ( IsRowVisible( contentY, contentH, sliderY, 34.0f ) )
            {
                widgets.renderSliders[i].Draw( draw, spec.label, buf, value, spec.minValue, spec.maxValue );
            }
        }
    }
    else if ( widgets.activeTab == InGameUITab::Targets )
    {
        const int targetCount = RenderTargetPreviewCount( data );
        const int selectedIndex = widgets.selectedRenderTargetPreview;
        const bool hasSelection = selectedIndex >= 0 && selectedIndex < targetCount;
        const UIRenderTargetPreviewResource* selected = hasSelection ? &data.renderTargetPreviews[selectedIndex] : nullptr;

        const bool selectedAvailable = selected && selected->available && selected->width > 0 && selected->height > 0;
        const Style::UIPalette& targetPalette = Style::Palette();
        const char* options[UI_RENDER_TARGET_PREVIEW_MAX] = {};

        int liveCount = 0;

        for ( int i = 0; i < targetCount; ++i )
        {
            const UIRenderTargetPreviewResource& resource = data.renderTargetPreviews[i];
            options[i] = resource.label;

            if ( resource.available && resource.width > 0 && resource.height > 0 )
            {
                ++liveCount;
            }
        }

        DrawSectionTitle( draw, contentX, contentY, contentH, scrolledY + 16.0f, 16.0f, "Targets" );

        char countText[64];
        snprintf( countText, sizeof( countText ), "%d / %d live", liveCount, targetCount );

        if ( IsRowVisible( contentY, contentH, scrolledY + UI_TARGETS_META_Y - 24.0f, 18.0f ) )
        {
            DrawLabelValueAt( draw, contentY, contentH, contentX, scrolledY + UI_TARGETS_META_Y - 24.0f, "Resources",
                              countText, targetPalette.accent.r, targetPalette.accent.g, targetPalette.accent.b );
        }

        if ( selected )
        {
            char detailText[160];

            if ( selectedAvailable )
            {
                snprintf( detailText, sizeof( detailText ), "%s, %d x %d, #%d", RenderTargetPreviewTypeText( *selected ),
                          selected->width, selected->height, selectedIndex );
            }
            else
            {
                snprintf( detailText, sizeof( detailText ), "%s, n/a", RenderTargetPreviewTypeText( *selected ) );
            }

            DrawLabelValueAt( draw, contentY, contentH, contentX, scrolledY + UI_TARGETS_META_Y, "Selected", detailText,
                              selectedAvailable ? targetPalette.textPrimary.r : targetPalette.textMuted.r,
                              selectedAvailable ? targetPalette.textPrimary.g : targetPalette.textMuted.g,
                              selectedAvailable ? targetPalette.textPrimary.b : targetPalette.textMuted.b );
        }

        const UIRect previewPanel = { contentX, scrolledY + UI_TARGETS_PREVIEW_Y, contentW, UI_TARGETS_PREVIEW_H };
        const UIRect previewClip = { contentX, contentY, contentW, contentH };

        UIRect previewImage = previewPanel;

        if ( IsBlockVisible( contentY, contentH, previewPanel.y, previewPanel.h ) )
        {
            draw.RoundedPanel( previewPanel, Style::Radii().control, targetPalette.windowSubtle, targetPalette.innerBorder );

            const UIRect previewInset = { previewPanel.x + 10.0f, previewPanel.y + 10.0f,
                                          (std::max)( 1.0f, previewPanel.w - 20.0f ),
                                          (std::max)( 1.0f, previewPanel.h - 20.0f ) };

            previewImage = selected ? FitRectToAspect( previewInset, selected->width, selected->height ) : previewInset;
            draw.RoundedRect( previewImage.x - 1.0f, previewImage.y - 1.0f, previewImage.w + 2.0f, previewImage.h + 2.0f,
                              Style::Radii().control, 0.01f, 0.015f, 0.018f, 0.92f );
        }

        if ( selectedAvailable && IsBlockVisible( contentY, contentH, previewImage.y, previewImage.h ) )
        {
            drawList.PushClip( previewClip.x, previewClip.y, previewClip.w, previewClip.h );
            drawList.AddPreviewImage( { static_cast<uint16_t>( selectedIndex ), true }, previewImage.x, previewImage.y,
                                      previewImage.w, previewImage.h, targetPalette.windowSubtle.r,
                                      targetPalette.windowSubtle.g, targetPalette.windowSubtle.b,
                                      targetPalette.windowSubtle.a, "Preview unavailable" );

            drawList.PopClip();
        }
        else if ( IsRowVisible( contentY, contentH, scrolledY + UI_TARGETS_PREVIEW_Y + 116.0f, 18.0f ) )
        {
            draw.Text( previewPanel.x + 18.0f, previewPanel.y + 116.0f, 12.0f, targetPalette.textMuted.r,
                       targetPalette.textMuted.g, targetPalette.textMuted.b, "Not available this frame" );
        }

        if ( IsBlockVisible( contentY, contentH, previewPanel.y, previewPanel.h ) )
        {
            draw.Outline( previewImage.x, previewImage.y, previewImage.w, previewImage.h, targetPalette.border.r,
                          targetPalette.border.g, targetPalette.border.b, 0.72f );
        }

        const char* selectedText = selected ? selected->label : "No targets";
        widgets.renderTargetCombo.SetBounds( contentX, scrolledY + UI_TARGETS_COMBO_Y, contentW, 24.0f );

        if ( IsRowVisible( contentY, contentH, scrolledY + UI_TARGETS_COMBO_Y, 24.0f ) )
        {
            widgets.renderTargetCombo.Draw( draw, "View", selectedText, options, targetCount, selectedIndex, widgets.mouseX,
                                            widgets.mouseY, widgets.lastRenderTargetDisabledMask );
        }
    }
    else if ( widgets.activeTab == InGameUITab::Sky )
    {
        SkyTab::Draw( widgets.skyTab, draw, data, contentX, contentY, contentW, contentH, scrolledY, widgets.mouseX,
                      widgets.mouseY );
    }
    else if ( widgets.activeTab == InGameUITab::Cinematic )
    {
        CinematicTab::Draw( widgets.cinematicTab, draw, data, contentX, contentY, contentW, contentH, scrolledY,
                            widgets.mouseX, widgets.mouseY );
    }
    else
    {
        ControlsTab::Draw( widgets.controlsTab, draw, data, contentX, contentY, contentW, contentH, scrolledY );
    }

    widgets.scrollBar.SetBounds( x + w - 14.0f, contentY, 4.0f, contentH );
    widgets.scrollBar.Draw( draw, static_cast<float>( m_windowInteraction.ContentHeight() ), contentH, widgets.scrollY,
                            widgets.scrollbarVisibleUntil, data.now );

    const float by = y + h - bottomH;
    draw.Rect( x + 16.0f, by, w - 32.0f, 1.0f, palette.lineSoft.r, palette.lineSoft.g, palette.lineSoft.b, 0.14f );
    const float footerPad = 18.0f;
    const float footerGap = 16.0f;
    const float footerX = x + footerPad;
    const float footerW = (std::max)( 120.0f, w - footerPad * 2.0f );
    const bool hasSeparateStats = footerW >= 560.0f;
    const float controlsW = hasSeparateStats ? 462.0f : footerW;
    draw.RoundedPanel( { footerX, by + 16.0f, controlsW, 56.0f }, Style::Radii().control, palette.windowSubtle,
                       palette.innerBorder );

    const UIRect rendererComboBounds = FooterRendererComboBounds( x, by );
    const UIRect waterComboBounds = FooterWaterComboBounds( x, by );
    const UIRect blurFooterBounds = FooterBlurBounds( x, by );
    const UIRect vsyncFooterBounds = FooterVsyncBounds( x, by );
    const UIRect hitboxFooterBounds = FooterHitboxBounds( x, by );
    const UIRect timelineFooterBounds = FooterTimelineBounds( x, by );
    const UIRect perfFooterBounds = FooterPerfBounds( x, by );
    widgets.rendererCombo.SetBounds( rendererComboBounds.x, rendererComboBounds.y, rendererComboBounds.w,
                                     rendererComboBounds.h );

    widgets.rendererCombo.SetDropUp( true );
    widgets.reflectionCombo.SetBounds( waterComboBounds.x, waterComboBounds.y, waterComboBounds.w, waterComboBounds.h );
    widgets.reflectionCombo.SetDropUp( true );
    widgets.blurToggle.SetBounds( blurFooterBounds.x, blurFooterBounds.y, blurFooterBounds.w, blurFooterBounds.h );
    widgets.vsyncToggle.SetBounds( vsyncFooterBounds.x, vsyncFooterBounds.y, vsyncFooterBounds.w, vsyncFooterBounds.h );
    widgets.hitboxToggle.SetBounds( hitboxFooterBounds.x, hitboxFooterBounds.y, hitboxFooterBounds.w, hitboxFooterBounds.h );

    widgets.histogramToggle.SetBounds( perfFooterBounds.x, perfFooterBounds.y, perfFooterBounds.w, perfFooterBounds.h );
    widgets.timelineToggle.SetBounds( timelineFooterBounds.x, timelineFooterBounds.y, timelineFooterBounds.w,
                                      timelineFooterBounds.h );

    static const char* kRendererOptions[] = { "DX12" };

    static const char* kReflectionOptions[] = { "FBO", "DXR", "None" };

    widgets.rendererCombo.Draw( draw, "Renderer", kRendererOptions, 1, 0, widgets.mouseX, widgets.mouseY );
    DrawFooterToggle( draw, blurFooterBounds, "Blur", widgets.blurPreviewEnabled );
    DrawFooterToggle( draw, vsyncFooterBounds, "VSync", data.vsyncEnabled );
    DrawFooterToggle( draw, hitboxFooterBounds, "Hitboxes", widgets.hitboxOverlayEnabled );
    DrawFooterToggle( draw, perfFooterBounds, "Perf", ProfilerTab::PerformanceHistogramEnabled( widgets.profilerTab ) );
    DrawFooterToggle( draw, timelineFooterBounds, "Timeline", ProfilerTab::TimelineEnabled( widgets.profilerTab ) );
    widgets.reflectionCombo.Draw( draw, "Water", kReflectionOptions, 3, WaterReflectionModeFromData( data ), widgets.mouseX,
                                  widgets.mouseY, ReflectionDisabledMask() );

    char status[128];
    const float frameDisplayMs = data.fps > 0.0f ? 1000.0f / data.fps : 0.0f;
    const int cpuPercent = static_cast<int>( std::clamp( ( data.renderMs + data.physicsMs ) / 16.67f * 100.0f, 0.0f, 99.0f ) );

    const int gpuPercent = static_cast<int>( std::clamp( data.renderMs / 16.67f * 100.0f, 0.0f, 99.0f ) );
    const int drawCalls = data.drawCallsBeforeUI + data.UIDrawCalls;
    snprintf( status, sizeof( status ), "%.0f", data.fps );

    if ( hasSeparateStats )
    {
        const float statsX = footerX + controlsW + footerGap;
        const float statsW = (std::max)( 120.0f, x + w - footerPad - statsX );
        draw.RoundedPanel( { statsX, by + 16.0f, statsW, 56.0f }, Style::Radii().control, palette.windowSubtle,
                           palette.innerBorder );

        if ( statsW < 350.0f )
        {
            char fpsText[32];
            char frameText[32];
            char drawText[32];
            snprintf( fpsText, sizeof( fpsText ), "%.0f", data.fps );
            snprintf( frameText, sizeof( frameText ), "%.2f ms", frameDisplayMs );
            snprintf( drawText, sizeof( drawText ), "%d/%d", drawCalls, data.UIDrawCalls );
            DrawCompactFooterStat( draw, statsX, by + 23.0f, "FPS", fpsText, palette.accent.r, palette.accent.g,
                                   palette.accent.b );

            DrawCompactFooterStat( draw, statsX, by + 41.0f, "Frame", frameText, palette.textPrimary.r,
                                   palette.textPrimary.g, palette.textPrimary.b );

            DrawCompactFooterStat( draw, statsX, by + 59.0f, "Draw/UI", drawText, palette.textPrimary.r,
                                   palette.textPrimary.g, palette.textPrimary.b );
        }
        else
        {
            DrawFooterStatCell( draw, statsX + 18.0f, by, "FPS", status, palette.accent.r, palette.accent.g,
                                palette.accent.b );

            DrawFooterStatDivider( draw, statsX + 78.0f, by );
            snprintf( status, sizeof( status ), "%.2f ms", frameDisplayMs );
            DrawFooterStatCell( draw, statsX + 100.0f, by, "Frame Time", status, palette.textPrimary.r,
                                palette.textPrimary.g, palette.textPrimary.b );

            DrawFooterStatDivider( draw, statsX + 190.0f, by );
            snprintf( status, sizeof( status ), "%d%%", cpuPercent );
            DrawFooterStatCell( draw, statsX + 212.0f, by, "CPU", status, palette.accent.r, palette.accent.g,
                                palette.accent.b );

            DrawFooterStatDivider( draw, statsX + 266.0f, by );
            snprintf( status, sizeof( status ), "%d%%", gpuPercent );
            DrawFooterStatCell( draw, statsX + 288.0f, by, "GPU", status, palette.accent.r, palette.accent.g,
                                palette.accent.b );

            DrawFooterStatDivider( draw, statsX + 342.0f, by );
            snprintf( status, sizeof( status ), "%d / %d", drawCalls, data.UIDrawCalls );
            DrawFooterStatCell( draw, statsX + statsW - 112.0f, by, "Draws / UI", status, palette.textPrimary.r,
                                palette.textPrimary.g, palette.textPrimary.b );
        }
    }
    else
    {

        if ( titleStatW > 0.0f && titleStatX + titleStatW < x + w - 116.0f )
        {
            draw.Text( titleStatX, y + 17.0f, 10.5f, palette.accent.r, palette.accent.g, palette.accent.b, titleStat );
        }
    }

    draw.Rect( x + w - 24.0f, y + h - 9.0f, 14.0f, 2.0f, palette.textMuted.r, palette.textMuted.g, palette.textMuted.b,
               0.58f );

    draw.Rect( x + w - 18.0f, y + h - 15.0f, 8.0f, 2.0f, palette.textMuted.r, palette.textMuted.g, palette.textMuted.b,
               0.46f );

    draw.Rect( x + w - 12.0f, y + h - 21.0f, 2.0f, 2.0f, palette.textMuted.r, palette.textMuted.g, palette.textMuted.b,
               0.38f );

    DrawHitboxOverlay( draw, data, windowBounds, { contentX, contentY, contentW, contentH },
                       { footerX, by + 16.0f, controlsW, 56.0f } );

    PROFILE_END( m_profiler, "Frame/UI/DrawBuild" );
    m_frameDrawList.Append( drawList );
    drawStandaloneOverlays();

    if ( drawsLiveRenderTargetPreview )
    {
        widgets.cache.Reset();
    }
    else
    {
        widgets.cache.StoreFrame( cacheKey );
    }

    return finishDraw();
}
