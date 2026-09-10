/*
File: SkullbonezSource/Runtime/UI/GameUI/GameUILayout.cpp
Purpose:
  Implements product-specific GameUI geometry and small display policy.

Summary:
  Scene selectors, Physics pipeline buttons, and footer controls use one
  Runtime-owned geometry vocabulary; reflection availability remains a small
  product policy while the reusable UI library stays domain-neutral.

Invariants:
  - Bounds returned here are consumed unchanged by drawing and hit testing.

Related:
  - SkullbonezSource/Runtime/UI/GameUI/GameUILayout.h
*/
#include "GameUILayout.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <string_view>

namespace SkullbonezCore::UI::GameLayout
{
namespace
{
float FiniteDimension( float value, float fallback )
{
    return std::isfinite( value ) ? std::clamp( value, 24.0f, 2048.0f ) : fallback;
}
} // namespace

std::array<char, 64> HeaderTitle( const char* sceneName )
{
    std::string_view name = sceneName && sceneName[0] ? sceneName : "Generated demo";
    if ( const auto slash = name.find_last_of( "/\\" ); slash != std::string_view::npos )
    {
        name.remove_prefix( slash + 1 );
    }
    if ( name.ends_with( ".scene.json" ) )
    {
        name.remove_suffix( 11 );
    }
    std::array<char, 64> title {};
    const int count = static_cast<int>( (std::min)( name.size(), size_t { 20 } ) );
    std::snprintf( title.data(), title.size(), "Skullbonez Core - %.*s%s", count, name.data(),
                   name.size() > 20 ? "..." : "" );
    return title;
}

ToolsChromeRects ComputeToolsChromeRects( const UIRect& bounds, bool sharedShell )
{
    ToolsChromeRects result;
    result.compact = sharedShell && ( bounds.h < 240.0f || bounds.w < 720.0f );
    const float title = result.compact ? 28.0f : 44.0f;
    const float tabs = result.compact ? 26.0f : 44.0f;
    const float footer = result.compact ? 28.0f : 78.0f;
    const float padding = result.compact ? 6.0f : 18.0f;
    const float gap = result.compact ? 6.0f : 12.0f;
    result.title = { bounds.x, bounds.y, bounds.w, title };
    result.tabs = { bounds.x + ( result.compact ? 6.0f : 14.0f ), bounds.y + title,
                    bounds.w - ( result.compact ? 12.0f : 28.0f ), tabs };
    result.content = { bounds.x + padding, bounds.y + title + tabs + gap,
                       (std::max)( 0.0f, bounds.w - padding * 2.0f - 8.0f ),
                       (std::max)( 0.0f, bounds.h - title - tabs - footer - padding ) };
    result.footer = { bounds.x, bounds.y + bounds.h - footer, bounds.w, footer };
    result.close = result.compact ? UIRect { bounds.x + bounds.w - 30.0f, bounds.y + 2.0f, 24.0f, 24.0f }
                                  : UIRect { bounds.x + bounds.w - 40.0f, bounds.y + 8.0f, 30.0f, 28.0f };
    return result;
}

PresentationPreferences SanitizePreferences( const PresentationPreferences& preferences )
{
    PresentationPreferences result = preferences;
    if ( result.layout != LayoutMode::Canvas && result.layout != LayoutMode::Editor )
    {
        result.layout = LayoutMode::Canvas;
    }

    result.leftWidth = FiniteDimension( result.leftWidth, 280.0f );
    result.rightWidth = FiniteDimension( result.rightWidth, 360.0f );
    result.drawerHeight = FiniteDimension( result.drawerHeight, 360.0f );
    result.diagnosticsHeight = FiniteDimension( result.diagnosticsHeight, 140.0f );
    result.lastTool = std::clamp( result.lastTool, 0, 10 );
    if ( result.foldedSections != 3 && result.foldedSections != 5 && result.foldedSections != 6 )
    {
        result.foldedSections = 7;
    }
    return result;
}

PresentationRects ComputePresentationRects( const PresentationState& state, int width, int height )
{
    PresentationRects result;
    const float w = static_cast<float>( (std::max)( 1, width ) );
    const float h = static_cast<float>( (std::max)( 1, height ) );
    const PresentationPreferences preferences = SanitizePreferences( state.preferences );
    const bool editor = preferences.layout == LayoutMode::Editor;
    result.window = { 0.0f, 0.0f, w, h };
    result.header = { 0.0f, 0.0f, w, (std::min)( 42.0f, h * 0.15f ) };

    const float transportHeight = (std::min)( 28.0f, h * 0.1f );
    // F5/F6 are independent floating overlays and reserve no dock space.
    const float contentY = editor || state.workspace == Workspace::SolverLab ? result.header.h : 0.0f;
    // Even at the minimum client size, reserve scene space below the header.
    const float maximumDrawer = (std::min)( h * 0.8f, h * 0.9f - contentY - ( editor ? transportHeight : 0.0f ) );
    const float drawerHeight = state.toolsOpen ? (std::min)( preferences.drawerHeight, maximumDrawer ) : 0.0f;
    const float drawerY = h - drawerHeight;
    const float contentBottom = drawerY - ( editor ? transportHeight : 0.0f );
    const float contentHeight = (std::max)( 0.0f, contentBottom - contentY );
    const float leftWidth = editor ? (std::min)( preferences.leftFolded ? 24.0f : preferences.leftWidth, w * 0.25f ) : 0.0f;
    const float rightWidth = editor ? (std::min)( preferences.rightFolded ? 24.0f : preferences.rightWidth, w * 0.3f )
                                    : 0.0f;

    result.viewport = { leftWidth, contentY, w - leftWidth - rightWidth, contentHeight };
    if ( editor )
    {
        result.left = { 0.0f, contentY, leftWidth, contentHeight };
        result.right = { w - rightWidth, contentY, rightWidth, contentHeight };
        result.leftResize = { (std::max)( 0.0f, leftWidth - 3.0f ), contentY, (std::min)( 6.0f, w ), contentHeight };
        result.rightResize = { (std::max)( 0.0f, w - rightWidth - 3.0f ), contentY, (std::min)( 6.0f, w ), contentHeight };
        result.transport = { leftWidth, contentBottom, result.viewport.w, transportHeight };
    }
    else
    {
        const float stripWidth = (std::min)( 760.0f, w );
        result.transport = { ( w - stripWidth ) * 0.5f, (std::max)( 0.0f, drawerY - transportHeight ), stripWidth,
                             transportHeight };
        if ( state.detailsOpen )
        {
            const float detailsWidth = (std::min)( preferences.rightWidth, w * 0.75f );
            result.right = { w - detailsWidth, result.header.h, detailsWidth,
                             (std::max)( 0.0f, result.transport.y - result.header.h ) };
        }
    }

    // Status badges occupy clear content without changing scene projection.
    // Canvas Details is an overlay, so only badge placement yields to its width.
    result.statusContent = { result.viewport.x, result.header.h, result.viewport.w - ( editor ? 0.0f : result.right.w ),
                             (std::max)( 0.0f, result.transport.y - result.header.h ) };
    const UIRect replayPane = editor ? result.left : result.right;
    if ( replayPane.w > 24.0f &&
         ( editor ? ( state.editorReplay || state.workspace == Workspace::SolverLab ) : !state.detailsCauses ) )
    {
        result.replayControls = { replayPane.x, replayPane.y + 30.0f, replayPane.w,
                                  (std::max)( 0.0f, replayPane.h - 30.0f ) };
    }
    if ( editor && result.right.w > 24.0f )
    {
        result.causeControls = { result.right.x, result.right.y + 30.0f, result.right.w,
                                 (std::max)( 0.0f, result.right.h - 30.0f ) };
    }
    if ( editor )
    {
        result.leftFold = { result.left.x, result.left.y, (std::min)( 24.0f, result.left.w ), 28.0f };
        result.rightFold = { result.right.x, result.right.y, (std::min)( 24.0f, result.right.w ), 28.0f };
        if ( preferences.leftFolded )
        {
            const float tabHeight = (std::min)( 120.0f, (std::max)( 0.0f, contentHeight - 32.0f ) * 0.45f );
            result.editorTab = { result.left.x + 2.0f, contentY + 30.0f, 20.0f, tabHeight };
            if ( state.workspace == Workspace::Scene )
            {
                result.editorReplayTab = { result.left.x + 2.0f, contentY + 30.0f + ( contentHeight - 32.0f ) * 0.5f, 20.0f,
                                           tabHeight };
            }
        }
        if ( preferences.rightFolded )
        {
            result.causeTab = { result.right.x + 2.0f, contentY + 30.0f, 20.0f,
                                (std::min)( 160.0f, (std::max)( 0.0f, contentHeight - 32.0f ) ) };
        }
        if ( result.left.w > 24.0f && state.workspace == Workspace::Scene )
        {
            const float tabWidth = ( result.left.w - 30.0f ) * 0.5f;
            result.editorTab = { result.left.x + 24.0f, result.left.y + 4.0f, tabWidth, 24.0f };
            result.editorReplayTab = { result.editorTab.x + tabWidth, result.editorTab.y, tabWidth, 24.0f };
            if ( !state.editorReplay && !state.editorInTools )
            {
                result.editorControls = { result.left.x + 10.0f, result.left.y + 32.0f,
                                          (std::max)( 0.0f, result.left.w - 20.0f ),
                                          (std::max)( 0.0f, result.left.h - 32.0f ) };
            }
        }
    }
    else if ( !editor && result.right.w > 0.0f )
    {
        result.detailsReplayTab = { result.right.x + 6.0f, result.right.y + 4.0f, result.right.w * 0.5f - 9.0f, 24.0f };
        result.detailsCausesTab = { result.right.x + result.right.w * 0.5f + 3.0f, result.right.y + 4.0f,
                                    result.right.w * 0.5f - 9.0f, 24.0f };
        if ( state.detailsCauses )
        {
            result.causeControls = { result.right.x, result.right.y + 30.0f, result.right.w,
                                     (std::max)( 0.0f, result.right.h - 30.0f ) };
        }
    }
    const float detailsWidth = (std::min)( 96.0f, w * 0.25f );
    result.replayDetails = { w - detailsWidth, result.transport.y, detailsWidth, result.transport.h };
    result.transport.w = (std::min)( result.transport.w, result.replayDetails.x - result.transport.x );
    result.replayScroll = std::clamp( state.replayScroll, 0.0f, 1.0f );
    result.editorScroll = std::clamp( state.editorScroll, 0.0f,
                                      (std::max)( 0.0f,
                                                  ( 320.0f +
                                                    36.0f * std::ceil(
                                                                24.0f /
                                                                (std::max)( 1.0f,
                                                                            std::floor( ( result.editorControls.w + 4.0f ) /
                                                                                        36.0f ) ) ) ) -
                                                      result.editorControls.h ) );
    if ( state.toolsOpen )
    {
        result.drawer = { 0.0f, drawerY, w, drawerHeight };
        result.drawerResize = { 0.0f, drawerY, w, (std::min)( 6.0f, drawerHeight ) };
    }
    return result;
}

void DrawSkullLogo( const UIDrawContext& draw, const UIRect& bounds )
{
    // Concept: the demo logo's round cranium, large paired sockets and three
    // small teeth share one 24-unit silhouette at every native drawing size.
    const float size = (std::max)( 0.0f, (std::min)( bounds.w, bounds.h ) );
    const float scale = size / 24.0f;
    const float x = bounds.x + ( bounds.w - size ) * 0.5f;
    const float y = bounds.y + ( bounds.h - size ) * 0.5f;
    draw.RoundedRect( x + 2.0f * scale, y + scale, 20.0f * scale, 19.0f * scale, 10.0f * scale, 0.9f, 0.92f, 0.94f, 1.0f );
    for ( int tooth = 0; tooth < 3; ++tooth )
    {
        draw.RoundedRect( x + ( 6.0f + static_cast<float>( tooth ) * 4.0f ) * scale, y + 17.0f * scale, 3.0f * scale,
                          6.0f * scale, scale, 0.9f, 0.92f, 0.94f, 1.0f );
    }
    draw.RoundedRect( x + 5.0f * scale, y + 9.0f * scale, 6.0f * scale, 6.0f * scale, 3.0f * scale, 0.10f, 0.12f, 0.15f,
                      1.0f );
    draw.RoundedRect( x + 13.0f * scale, y + 9.0f * scale, 6.0f * scale, 6.0f * scale, 3.0f * scale, 0.10f, 0.12f, 0.15f,
                      1.0f );
    draw.Triangle( x + 12.0f * scale, y + 14.0f * scale, x + 10.0f * scale, y + 18.0f * scale, x + 14.0f * scale,
                   y + 18.0f * scale, 0.10f, 0.12f, 0.15f, 1.0f );
}

HeaderRects ComputeHeaderRects( const UIRect& header, Workspace workspace )
{
    HeaderRects result;
    const float unit = (std::min)( 1.0f, header.w / 440.0f );
    const float pad = 8.0f * unit;
    const float height = (std::max)( 0.0f, header.h - 12.0f * unit );
    const float y = header.y + 6.0f * unit;
    result.skull = { header.x + pad, y, 30.0f * unit, height };
    const float exitSpace = workspace == Workspace::SolverLab ? height + pad : 0.0f;
    if ( workspace == Workspace::SolverLab )
    {
        result.close = { header.x + header.w - pad - height, y, height, height };
    }
    const float layoutWidth = header.w >= 600.0f ? 148.0f : 90.0f * unit;
    result.layout = { header.x + header.w - pad - exitSpace - layoutWidth, y, layoutWidth, height };
    result.workspace = { result.layout.x - 102.0f * unit, y, 94.0f * unit, height };
    const float cameraWidth = header.w >= 600.0f ? 120.0f : 74.0f * unit;
    result.camera = { result.workspace.x - cameraWidth - pad, y, cameraWidth, height };
    result.scene = { result.skull.x + result.skull.w + pad, y,
                     (std::max)( 0.0f, result.camera.x - result.skull.x - result.skull.w - pad * 2.0f ), height };
    return result;
}

UIRect DiagnosticDetailsBounds( const UIRect& panel )
{
    if ( panel.w < 100.0f || panel.h < 28.0f )
    {
        return {};
    }
    return { panel.x + panel.w - 78.0f, panel.y + 4.0f, 68.0f, 22.0f };
}

int SceneComboVisibleCount( int optionCount )
{
    return std::clamp( optionCount, 0, UI_SCENE_COMBO_VISIBLE_OPTIONS );
}

int ClampSceneComboScroll( int scroll, int optionCount )
{
    const int maxScroll = (std::max)( 0, optionCount - SceneComboVisibleCount( optionCount ) );
    return std::clamp( scroll, 0, maxScroll );
}

int SceneComboScrollForSelection( int selectedIndex, int optionCount )
{
    const int visibleCount = SceneComboVisibleCount( optionCount );

    if ( selectedIndex < 0 || visibleCount <= 0 )
    {
        return 0;
    }

    return ClampSceneComboScroll( selectedIndex - visibleCount / 2, optionCount );
}

SceneHeaderWidths ResolveSceneHeaderWidths( float contentW )
{
    SceneHeaderWidths widths;
    contentW = (std::max)( 1.0f, contentW );
    constexpr float naturalButtons = UI_SCENE_RESET_BUTTON_W + UI_SCENE_RESET_DEFAULTS_BUTTON_W +
                                     UI_SCENE_SAVE_DEFAULTS_BUTTON_W;

    widths.gap = (std::min)( UI_SCENE_HEADER_BUTTON_GAP, contentW / 16.0f );
    const float availableWithoutGaps = (std::max)( 1.0f, contentW - widths.gap * 3.0f );

    if ( availableWithoutGaps >= naturalButtons + 1.0f )
    {
        widths.reset = UI_SCENE_RESET_BUTTON_W;
        widths.resetDefaults = UI_SCENE_RESET_DEFAULTS_BUTTON_W;
        widths.saveDefaults = UI_SCENE_SAVE_DEFAULTS_BUTTON_W;
        widths.combo = (std::min)( 520.0f, availableWithoutGaps - naturalButtons );
        return widths;
    }

    // Invariant: every control remains inside the one authored header row. At
    // compact widths the combo receives one quarter and buttons scale together.
    widths.combo = availableWithoutGaps * 0.25f;
    const float scaledButtonSpace = availableWithoutGaps - widths.combo;
    const float scale = scaledButtonSpace / naturalButtons;
    widths.reset = UI_SCENE_RESET_BUTTON_W * scale;
    widths.resetDefaults = UI_SCENE_RESET_DEFAULTS_BUTTON_W * scale;
    widths.saveDefaults = scaledButtonSpace - widths.reset - widths.resetDefaults;
    return widths;
}

float SceneTabComboWidth( float contentW )
{
    return ResolveSceneHeaderWidths( contentW ).combo;
}

void SetPipelineStepButtonBounds( UIRect& previous, UIRect& next, float contentX, float contentW, float y )
{
    const float nextX = contentX + contentW - UI_PIPELINE_STEP_BUTTON_W;
    const float previousX = nextX - UI_PIPELINE_STEP_BUTTON_GAP - UI_PIPELINE_STEP_BUTTON_W;
    previous = { previousX, y, UI_PIPELINE_STEP_BUTTON_W, UI_PIPELINE_STEP_BUTTON_H };
    next = { nextX, y, UI_PIPELINE_STEP_BUTTON_W, UI_PIPELINE_STEP_BUTTON_H };
}

UIRect FooterRendererComboBounds( float x, float bottomY )
{
    return { x + 32.0f, bottomY + 20.0f, 172.0f, 24.0f };
}

UIRect FooterWaterComboBounds( float x, float bottomY )
{
    return { x + 32.0f, bottomY + 46.0f, 172.0f, 24.0f };
}

UIRect FooterBlurBounds( float x, float bottomY )
{
    return { x + 218.0f, bottomY + 22.0f, 86.0f, 24.0f };
}

UIRect FooterVsyncBounds( float x, float bottomY )
{
    return { x + 306.0f, bottomY + 22.0f, 86.0f, 24.0f };
}

UIRect FooterHitboxBounds( float x, float bottomY )
{
    return { x + 394.0f, bottomY + 22.0f, 86.0f, 24.0f };
}

UIRect FooterTimelineBounds( float x, float bottomY )
{
    return { x + 218.0f, bottomY + 48.0f, 86.0f, 24.0f };
}

UIRect FooterPerfBounds( float x, float bottomY )
{
    return { x + 306.0f, bottomY + 48.0f, 86.0f, 24.0f };
}

uint32_t ReflectionDisabledMask()
{
    return 0u;
}
} // namespace SkullbonezCore::UI::GameLayout
