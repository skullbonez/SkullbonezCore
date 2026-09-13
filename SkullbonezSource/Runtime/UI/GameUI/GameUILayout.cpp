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
// Controls and palette share one extent so narrow panels can reach every row.
float EditorContentHeight( float width )
{
    const float columns = (std::max)( 1.0f, std::floor( ( width + 4.0f ) / 36.0f ) );
    return EDITOR_PALETTE_TOP + 4.0f + 36.0f * std::ceil( 24.0f / columns );
}

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
    std::snprintf( title.data(), title.size(), "Skullbonez Core - %.*s%s", count, name.data(), name.size() > 20 ? "..." : "" );
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
    result.tabs = { bounds.x + ( result.compact ? 6.0f : 14.0f ), bounds.y + title, bounds.w - ( result.compact ? 12.0f : 28.0f ), tabs };
    result.content = { bounds.x + padding, bounds.y + title + tabs + gap, (std::max)( 0.0f, bounds.w - padding * 2.0f - 8.0f ), (std::max)( 0.0f, bounds.h - title - tabs - footer - padding ) };
    result.footer = { bounds.x, bounds.y + bounds.h - footer, bounds.w, footer };
    result.close = result.compact ? UIRect { bounds.x + bounds.w - 30.0f, bounds.y + 2.0f, 24.0f, 24.0f } : UIRect { bounds.x + bounds.w - 40.0f, bounds.y + 8.0f, 30.0f, 28.0f };
    return result;
}

PresentationPreferences SanitizePreferences( const PresentationPreferences& preferences )
{
    PresentationPreferences result = preferences;
    if ( result.layout != LayoutMode::Canvas && result.layout != LayoutMode::Editor )
    {
        result.layout = LayoutMode::Canvas;
    }

    if ( result.theme >= Style::Theme::Count )
    {
        result.theme = Style::Theme::Blue;
    }

    result.leftWidth = FiniteDimension( result.leftWidth, 280.0f );
    result.rightWidth = FiniteDimension( result.rightWidth, 360.0f );
    result.drawerHeight = FiniteDimension( result.drawerHeight, 360.0f );
    result.diagnosticsHeight = FiniteDimension( result.diagnosticsHeight, 140.0f );
    result.lastTool = std::clamp( result.lastTool, 0, 10 );
    if ( result.foldedSections > 7 )
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
    const float leftWidth = editor ? (std::min)( ( preferences.leftFolded && ( preferences.replayFolded || state.workspace == Workspace::SolverLab ) ) ? 24.0f : preferences.leftWidth, w * 0.4f )
                                   : 0.0f;
    const float rightWidth = editor ? (std::min)( preferences.rightFolded ? 24.0f : preferences.rightWidth, w * 0.4f ) : 0.0f;

    result.viewport = { leftWidth, contentY, w - leftWidth - rightWidth, contentHeight };
    if ( editor )
    {
        result.left = { 0.0f, contentY, leftWidth, contentHeight };
        result.right = { w - rightWidth, contentY, rightWidth, contentHeight };

        result.rightResize = { (std::max)( 0.0f, w - rightWidth - 5.0f ), contentY, (std::min)( 10.0f, w ), contentHeight };
        result.transport = { leftWidth, contentBottom, result.viewport.w, transportHeight };
    }
    else
    {
        const float stripWidth = (std::min)( 760.0f, w );
        result.transport = { ( w - stripWidth ) * 0.5f, (std::max)( 0.0f, drawerY - transportHeight ), stripWidth, transportHeight };
        if ( state.detailsOpen )
        {
            const float detailsWidth = (std::min)( preferences.rightWidth, w * 0.75f );
            result.right = { w - detailsWidth, result.header.h, detailsWidth, (std::max)( 0.0f, result.transport.y - result.header.h ) };
        }
    }

    // Status badges occupy clear content without changing scene projection.
    // Canvas Details is an overlay, so only badge placement yields to its width.
    result.statusContent = { result.viewport.x, result.header.h, result.viewport.w - ( editor ? 0.0f : result.right.w ), (std::max)( 0.0f, result.transport.y - result.header.h ) };
    if ( editor )
    {
        // Expanded sections stack from the top. A folded sibling reserves only its header.
        const bool scene = state.workspace == Workspace::Scene;
        const float foldedHeight = (std::min)( leftWidth <= 24 ? 108.0f : 30.0f, contentHeight * 0.5f );
        const float replayHeight = !scene                     ? 0.0f
                                   : preferences.replayFolded ? foldedHeight
                                   : preferences.leftFolded   ? contentHeight - foldedHeight
                                                              : (std::min)( 400.0f, contentHeight * 0.38f + 40.0f );
        const float editorHeight = !scene ? contentHeight : preferences.leftFolded ? foldedHeight : contentHeight - replayHeight;
        result.editorPane = { 0, contentY, leftWidth, editorHeight };
        result.replayPane = scene ? UIRect { 0, contentY + editorHeight, leftWidth, replayHeight } : UIRect {};
        const UIRect panes[] = { result.editorPane, result.replayPane };
        UIRect* headers[] = { &result.editorTab, &result.editorReplayTab };
        UIRect* folds[] = { &result.leftFold, &result.replayFold };
        UIRect* grips[] = { &result.leftResize, &result.replayResize };
        for ( int index = 0; index < ( scene ? 2 : 1 ); ++index )
        {
            const auto& pane = panes[index];
            const bool folded = pane.w <= 24.0f;
            *folds[index] = { pane.x, pane.y, 24, (std::min)( 28.0f, pane.h ) };
            *headers[index] = folded ? UIRect { pane.x + 2, pane.y + 30, 20, (std::min)( 120.0f, (std::max)( 0.0f, pane.h - 34 ) ) }
                                     : UIRect { pane.x + 24, pane.y + 2, (std::max)( 0.0f, pane.w - 30 ), 26 };
            if ( !( index == 0 ? preferences.leftFolded : preferences.replayFolded ) )
            {
                *grips[index] = { pane.x + pane.w - 5, pane.y, 10, pane.h };
            }
        }
        if ( !preferences.leftFolded )
        {
            const auto& pane = result.editorPane;
            if ( scene && !state.editorInTools )
            {
                result.editorControls = { pane.x + 10, pane.y + 32, (std::max)( 0.0f, pane.w - 20 ), (std::max)( 0.0f, pane.h - 32 ) };
            }
            if ( !scene )
            {
                result.replayControls = { pane.x, pane.y + 30, pane.w, (std::max)( 0.0f, pane.h - 30 ) };
            }
        }
        if ( scene && !preferences.replayFolded )
        {
            result.replayControls = { result.replayPane.x, result.replayPane.y + 30, result.replayPane.w, (std::max)( 0.0f, result.replayPane.h - 30 ) };
        }
        result.rightFold = { result.right.x, result.right.y, 24, (std::min)( 28.0f, result.right.h ) };
        if ( preferences.rightFolded )
        {
            result.rightResize = {};
            result.causeTab = { result.right.x + 2, contentY + 30, 20, (std::min)( 160.0f, (std::max)( 0.0f, contentHeight - 32 ) ) };
        }
        else
        {
            result.causeControls = { result.right.x, result.right.y + 30, result.right.w, (std::max)( 0.0f, result.right.h - 30 ) };
        }
    }
    else if ( !editor && result.right.w > 0.0f )
    {
        result.detailsReplayTab = { result.right.x + 6.0f, result.right.y + 4.0f, result.right.w * 0.5f - 9.0f, 24.0f };
        result.detailsCausesTab = { result.right.x + result.right.w * 0.5f + 3.0f, result.right.y + 4.0f, result.right.w * 0.5f - 9.0f, 24.0f };
        if ( !state.detailsCauses )
        {
            result.replayControls = { result.right.x, result.right.y + 30, result.right.w, (std::max)( 0.0f, result.right.h - 30 ) };
        }
        if ( state.detailsCauses )
        {
            result.causeControls = { result.right.x, result.right.y + 30.0f, result.right.w, (std::max)( 0.0f, result.right.h - 30.0f ) };
        }
    }
    const float detailsWidth = (std::min)( 96.0f, w * 0.25f );
    result.replayDetails = { w - detailsWidth, result.transport.y, detailsWidth, result.transport.h };
    result.transport.w = (std::min)( result.transport.w, result.replayDetails.x - result.transport.x );
    // Diagnostics own floating bounds and do not reserve layout space.
    result.replayScroll = std::clamp( state.replayScroll, 0.0f, 1.0f );
    result.editorScroll = std::clamp( state.editorScroll, 0.0f, (std::max)( 0.0f, EditorContentHeight( result.editorControls.w ) - result.editorControls.h ) );
    if ( state.toolsOpen )
    {
        result.drawer = { 0.0f, drawerY, w, drawerHeight };
        result.drawerResize = { 0.0f, drawerY, w, (std::min)( 6.0f, drawerHeight ) };
    }
    return result;
}

// Generated Split State geometry: tools/generate_app_icon.py shares these
// contours with Windows icons. Native strokes stay at least one physical pixel.
namespace
{
constexpr float SKULL_HALF[][2] = { { 16.000000f, 2.000000f },
                                    { 11.500000f, 2.400000f },
                                    { 7.600000f, 4.100000f },
                                    { 4.900000f, 7.000000f },
                                    { 3.600000f, 10.500000f },
                                    { 3.400000f, 14.500000f },
                                    { 4.100000f, 18.000000f },
                                    { 6.000000f, 20.700000f },
                                    { 9.600000f, 22.100000f },
                                    { 10.100000f, 26.400000f },
                                    { 11.500000f, 28.400000f },
                                    { 13.200000f, 29.000000f },
                                    { 16.000000f, 29.000000f } };
constexpr float SKULL_WIRES[][4] = { { 16.000000f, 2.000000f, 20.000000f, 9.000000f },
                                     { 22.400000f, 4.100000f, 20.000000f, 9.000000f },
                                     { 27.100000f, 7.000000f, 20.000000f, 9.000000f },
                                     { 16.000000f,
                                                                                                                                                                                             10.000000f,
                                                                                                                                                                                             20.000000f,
                                                                                                                                                                                             9.000000f },
                                     { 20.000000f,
                                                                                                                                                                                                            9.000000f,
                                                                                                                                                                                                            28.600000f,
                                                                                                                                                                                                            14.500000f },
                                     { 28.000000f,
                                                                                                                                                                                                                            18.000000f,
                                                                                                                                                                                                                            21.000000f,
                                                                                                                                                                                                                            21.000000f },
                                     { 21.000000f,
                                                                                                                                                                                                                                            21.000000f,
                                                                                                                                                                                                                                            16.000000f,
                                                                                                                                                                                                                                            22.000000f },
                                     { 21.000000f,
                                                                                                                                                                                                                                                            21.000000f,
                                                                                                                                                                                                                                                            20.500000f,
                                                                                                                                                                                                                                                            28.400000f } };
constexpr float SKULL_DETAIL[][4] = { { 16.000000f, 2.000000f, 22.400000f, 4.100000f },
                                      { 16.000000f, 10.000000f, 16.000000f, 2.000000f },
                                      { 16.000000f, 10.000000f, 21.000000f, 21.000000f },
                                      { 22.400000f,
                                                                                                                                                                                                 4.100000f,
                                                                                                                                                                                                 27.100000f,
                                                                                                                                                                                                 7.000000f },
                                      { 21.000000f,
                                                                                                                                                                                                                21.000000f,
                                                                                                                                                                                                                25.800000f,
                                                                                                                                                                                                                20.700000f } };
constexpr float SKULL_CIRCLE[][2] = { { 1.000000f, 0.000000f },
                                      { 0.980785f, 0.195090f },
                                      { 0.923880f, 0.382683f },
                                      { 0.831470f, 0.555570f },
                                      { 0.707107f, 0.707107f },
                                      { 0.555570f, 0.831470f },
                                      { 0.382683f, 0.923880f },
                                      { 0.195090f, 0.980785f },
                                      { 0.000000f, 1.000000f },
                                      { -0.195090f, 0.980785f },
                                      { -0.382683f, 0.923880f },
                                      { -0.555570f, 0.831470f },
                                      { -0.707107f, 0.707107f },
                                      { -0.831470f, 0.555570f },
                                      { -0.923880f, 0.382683f },
                                      { -0.980785f, 0.195090f },
                                      { -1.000000f, 0.000000f },
                                      { -0.980785f, -0.195090f },
                                      { -0.923880f, -0.382683f },
                                      { -0.831470f, -0.555570f },
                                      { -0.707107f, -0.707107f },
                                      { -0.555570f, -0.831470f },
                                      { -0.382683f, -0.923880f },
                                      { -0.195090f, -0.980785f },
                                      { -0.000000f, -1.000000f },
                                      { 0.195090f, -0.980785f },
                                      { 0.382683f, -0.923880f },
                                      { 0.555570f, -0.831470f },
                                      { 0.707107f, -0.707107f },
                                      { 0.831470f, -0.555570f },
                                      { 0.923880f, -0.382683f },
                                      { 0.980785f, -0.195090f } };
constexpr Style::UIColor SKULL_IVORY { 235.0f / 255, 239.0f / 255, 232.0f / 255, 1 };
constexpr Style::UIColor SKULL_CYAN { 91.0f / 255, 217.0f / 255, 238.0f / 255, 1 };
constexpr Style::UIColor SKULL_INK { 19.0f / 255, 38.0f / 255, 49.0f / 255, 1 };

void DrawSkullStroke( const UIDrawContext& draw, const UIRect& mark, const float ( &line )[4], const Style::UIColor& color, float weight = 1.0f )
{
    const float scale = mark.w / 32.0f;
    const float ax = mark.x + line[0] * scale, ay = mark.y + line[1] * scale;
    const float bx = mark.x + line[2] * scale, by = mark.y + line[3] * scale;
    const float dx = bx - ax, dy = by - ay;
    const float halfWidth = (std::max)( 1.0f, .85f * scale ) * weight * .5f;
    const float length = std::sqrt( dx * dx + dy * dy );
    const float nx = -dy / length * halfWidth, ny = dx / length * halfWidth;
    draw.Triangle( ax + nx, ay + ny, bx + nx, by + ny, ax - nx, ay - ny, color.r, color.g, color.b, 1 );
    draw.Triangle( ax - nx, ay - ny, bx + nx, by + ny, bx - nx, by - ny, color.r, color.g, color.b, 1 );
}

void DrawSkullHalf( const UIDrawContext& draw, const UIRect& mark, bool wire )
{
    const float scale = mark.w / 32.0f;
    const auto& fill = wire ? SKULL_INK : SKULL_IVORY;
    const auto& edge = wire ? SKULL_CYAN : SKULL_INK;
    const float cx = mark.x + 16 * scale, cy = mark.y + 15 * scale;
    for ( std::size_t index = 1; index < std::size( SKULL_HALF ); ++index )
    {
        const auto& a = SKULL_HALF[index - 1];
        const auto& b = SKULL_HALF[index];
        const float line[] = { wire ? 32 - a[0] : a[0], a[1], wire ? 32 - b[0] : b[0], b[1] };
        const float ax = mark.x + line[0] * scale, ay = mark.y + line[1] * scale;
        const float bx = mark.x + line[2] * scale, by = mark.y + line[3] * scale;
        // Mirroring reverses winding; both halves must face the screen-Y UI camera.
        if ( wire )
        {
            draw.Triangle( cx, cy, bx, by, ax, ay, fill.r, fill.g, fill.b, 1 );
        }
        else
        {
            draw.Triangle( cx, cy, ax, ay, bx, by, fill.r, fill.g, fill.b, 1 );
        }
        DrawSkullStroke( draw, mark, line, edge, wire ? 1.0f : .7f );
    }
}

void DrawSkullEllipse( const UIDrawContext& draw, const UIRect& mark, const float ( &ellipse )[4], const Style::UIColor& color )
{
    const float scale = mark.w / 32.0f;
    const float x = mark.x + ellipse[0] * scale, y = mark.y + ellipse[1] * scale;
    for ( std::size_t index = 0; index < std::size( SKULL_CIRCLE ); ++index )
    {
        const auto& a = SKULL_CIRCLE[index];
        const auto& b = SKULL_CIRCLE[( index + 1 ) % std::size( SKULL_CIRCLE )];
        draw.Triangle( x, y, x + b[0] * ellipse[2] * scale, y + b[1] * ellipse[3] * scale, x + a[0] * ellipse[2] * scale, y + a[1] * ellipse[3] * scale, color.r, color.g, color.b, 1 );
    }
}
} // namespace

void DrawSkullLogo( const UIDrawContext& draw, const UIRect& bounds )
{
    const float size = std::floor( (std::max)( 0.0f, (std::min)( bounds.w, bounds.h ) ) );
    if ( size <= 0 )
    {
        return;
    }
    // Pixel-aligned origins keep the small editor marks stable at compact widths.
    const UIRect mark { std::round( bounds.x + ( bounds.w - size ) * .5f ), std::round( bounds.y + ( bounds.h - size ) * .5f ), size, size };
    const float scale = size / 32.0f;
    const float stroke = (std::max)( .85f, 32.0f / size );
    DrawSkullHalf( draw, mark, false );
    DrawSkullHalf( draw, mark, true );
    for ( const auto& line : SKULL_WIRES )
    {
        DrawSkullStroke( draw, mark, line, SKULL_CYAN );
    }
    if ( size >= 32 )
    {
        for ( const auto& line : SKULL_DETAIL )
        {
            DrawSkullStroke( draw, mark, line, SKULL_CYAN );
        }
    }
    DrawSkullStroke( draw, mark, { 16, 2, 16, 29 }, SKULL_CYAN );
    DrawSkullEllipse( draw, mark, { 10, 14.3f, 3.8f, 4 }, SKULL_INK );
    DrawSkullEllipse( draw, mark, { 22, 14.3f, 4.4f, 4.6f }, SKULL_CYAN );
    DrawSkullEllipse( draw, mark, { 22, 14.3f, 4.4f - stroke, 4.6f - stroke }, SKULL_INK );
    draw.Triangle( mark.x + 16 * scale,
                   mark.y + 18.6f * scale,
                   mark.x + 13.7f * scale,
                   mark.y + 22.3f * scale,
                   mark.x + 18.3f * scale,
                   mark.y + 22.3f * scale,
                   SKULL_INK.r,
                   SKULL_INK.g,
                   SKULL_INK.b,
                   1 );
    for ( float tooth : { 13.0f, 18.0f } )
    {
        draw.Rect( mark.x + tooth * scale, mark.y + 25.3f * scale, stroke * scale, 4.2f * scale, SKULL_INK.r, SKULL_INK.g, SKULL_INK.b, 1 );
    }
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
    const float layoutWidth = 76.0f * unit;
    result.layout = { header.x + header.w - pad - exitSpace - layoutWidth, y, layoutWidth, height };
    result.workspace = { result.layout.x - 102.0f * unit, y, 94.0f * unit, height };
    const float cameraWidth = header.w >= 600.0f ? 120.0f : 74.0f * unit;
    result.camera = { result.workspace.x - cameraWidth - pad, y, cameraWidth, height };
    result.fourViews = { result.camera.x - 70.0f * unit, y, 62.0f * unit, height };
    result.scene = { result.skull.x + result.skull.w + pad, y, (std::max)( 0.0f, result.fourViews.x - result.skull.x - result.skull.w - pad * 2.0f ), height };
    return result;
}

std::array<UIRect, 4> EditorPaneRects( const UIRect& viewport )
{
    // Equal integer extents let the renderer reuse offscreen targets between
    // panes without alternating resource sizes on odd-sized windows.
    const float width = static_cast<float>( (std::max)( 1, ( static_cast<int>( viewport.w ) - 2 ) / 2 ) );
    const float height = static_cast<float>( (std::max)( 1, ( static_cast<int>( viewport.h ) - 2 ) / 2 ) );
    return { UIRect { viewport.x, viewport.y, width, height },
             UIRect { viewport.x + width + 2, viewport.y, width, height },
             UIRect { viewport.x, viewport.y + height + 2, width, height },
             UIRect { viewport.x + width + 2, viewport.y + height + 2, width, height } };
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
    constexpr float naturalButtons = UI_SCENE_RESET_BUTTON_W + UI_SCENE_RESET_DEFAULTS_BUTTON_W + UI_SCENE_SAVE_DEFAULTS_BUTTON_W;

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

std::array<SkullbonezCore::UI::UIRect, 4> SkullbonezCore::UI::GameLayout::EditorViewGizmoRects( const UIRect& viewport )
{
    if ( viewport.w < 132.0f || viewport.h < 96.0f )
    {
        return {};
    }
    const float x = viewport.x + 8.0f;
    const float y = viewport.y + viewport.h - 88.0f;
    return { UIRect { x + 30, y + 54, 60, 24 }, UIRect { x + 30, y, 60, 24 }, UIRect { x, y + 27, 56, 24 }, UIRect { x + 60, y + 27, 56, 24 } };
}
