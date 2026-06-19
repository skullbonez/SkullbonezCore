/*
File: SkullbonezSource/UI/UIWindowChrome.cpp
Purpose:
  Implements UI WindowChrome widgets, layout, drawing, or UI state for the in-engine controls.

Mental model:
  The UI is immediate-mode-style: each frame reads engine state, computes hit
  boxes, emits draw commands, and returns requests for the run loop to apply.

Glossary:
  Draw command: Lightweight record describing a UI shape or text batch to
  render later in the frame.
  Hit box: Screen-space rectangle used to decide whether mouse input targets a
  widget.

Invariants:
  - Draw geometry and hit testing must be derived from the same layout
  constants.

Related:
  - SkullbonezSource/UI/UIWindowChrome.h
  - Agentic/Reference/comment-style-guide.md
*/
#include "UIWindowChrome.h"
#include "../SkullbonezText.h"
#include "SkullbonezUI.h"
#include "UIDrawWidgets.h"
#include "UILayout.h"
#include "UIStyle.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>

using namespace SkullbonezCore::Text;

namespace SkullbonezCore
{
namespace UI
{
namespace Chrome
{

void BuildWindowTitle( const InGameUIFrameData& data, char* out, size_t outSize )
{
    if ( outSize == 0 )
    {
        return;
    }

    if ( data.sceneMode && data.sceneName && data.sceneName[0] != '\0' )
    {
        const int displayedFrame = ( data.testComplete && data.targetFrameCount > 0 && data.currentFrame > data.targetFrameCount ) ? data.targetFrameCount : data.currentFrame;
        if ( data.testComplete )
        {
            if ( data.targetFrameCount > 0 )
            {
                snprintf( out, outSize, "%s  %d/%d complete", data.sceneName, displayedFrame, data.targetFrameCount );
            }
            else
            {
                snprintf( out, outSize, "%s  complete", data.sceneName );
            }
        }
        else if ( data.targetFrameCount > 0 )
        {
            snprintf( out, outSize, "%s  %d/%d", data.sceneName, displayedFrame, data.targetFrameCount );
        }
        else
        {
            snprintf( out, outSize, "%s", data.sceneName );
        }
    }
    else
    {
        snprintf( out, outSize, "Skullbonez Core" );
    }

    out[outSize - 1] = '\0';
}


void FitTitleText( char* text, size_t textSize, float fontSize, float maxWidth )
{
    if ( textSize == 0 || Text2d::MeasureText( fontSize, text ) <= maxWidth )
    {
        return;
    }

    char original[192] = {};
    strcpy_s( original, sizeof( original ), text );
    const size_t len = strlen( original );
    for ( size_t start = 1; start < len; ++start )
    {
        snprintf( text, textSize, "...%s", original + start );
        if ( Text2d::MeasureText( fontSize, text ) <= maxWidth )
        {
            return;
        }
    }

    snprintf( text, textSize, "..." );
}


UIRect WindowRect( const UIWindowState& window )
{
    return { static_cast<float>( window.x ), static_cast<float>( window.y ), static_cast<float>( window.width ), static_cast<float>( window.height ) };
}


void ApplyDefaultWindowPlacement( UIWindowState& window, int screenW, int screenH )
{
    constexpr int margin = 14;
    screenW = (std::max)( 1, screenW );
    screenH = (std::max)( 1, screenH );
    window.x = margin;
    window.y = (std::max)( margin, screenH - window.height - margin );
    window.restoreX = window.x;
    window.restoreY = window.y;
    window.restoreW = window.width;
    window.restoreH = window.height;
    window.hasAppliedDefaultPlacement = true;
}


void ClampWindowToScreen( UIWindowState& window, int screenW, int screenH, int minW, int minH, int margin )
{
    screenW = (std::max)( 1, screenW );
    screenH = (std::max)( 1, screenH );
    const int maxW = (std::max)( minW, screenW - margin * 2 );
    const int maxH = (std::max)( minH, screenH - margin * 2 );
    window.width = std::clamp( window.width, minW, maxW );
    window.height = std::clamp( window.height, minH, maxH );
    window.x = std::clamp( window.x, margin, (std::max)( margin, screenW - window.width - margin ) );
    window.y = std::clamp( window.y, margin, (std::max)( margin, screenH - window.height - margin ) );
}


bool SetMaximized( UIWindowState& window, bool maximized, int screenW, int screenH, double now )
{
    if ( window.isMaximized == maximized )
    {
        return false;
    }

    constexpr int minW = 390;
    constexpr int minH = 250;
    constexpr int margin = 10;
    screenW = (std::max)( 1, screenW );
    screenH = (std::max)( 1, screenH );
    const int maxW = (std::max)( minW, screenW - margin * 2 );
    const int maxH = (std::max)( minH, screenH - margin * 2 );
    const UIRect from = WindowRect( window );

    if ( maximized )
    {
        window.restoreX = window.x;
        window.restoreY = window.y;
        window.restoreW = window.width;
        window.restoreH = window.height;
        window.x = margin;
        window.y = margin;
        window.width = maxW;
        window.height = maxH;
    }
    else
    {
        window.x = std::clamp( window.restoreX, margin, (std::max)( margin, screenW - window.restoreW - margin ) );
        window.y = std::clamp( window.restoreY, margin, (std::max)( margin, screenH - window.restoreH - margin ) );
        window.width = std::clamp( window.restoreW, minW, maxW );
        window.height = std::clamp( window.restoreH, minH, maxH );
    }

    window.isMaximized = maximized;
    BeginWindowAnimation( window, from, WindowRect( window ), now, false );
    return true;
}


void BeginWindowAnimation( UIWindowState& window, const UIRect& from, const UIRect& to, double now, bool toMinimized )
{
    const bool sameBounds = std::fabs( from.x - to.x ) < 0.5f &&
                            std::fabs( from.y - to.y ) < 0.5f &&
                            std::fabs( from.w - to.w ) < 0.5f &&
                            std::fabs( from.h - to.h ) < 0.5f;
    if ( now <= 0.0 || sameBounds )
    {
        window.animationActive = false;
        return;
    }

    window.animationActive = true;
    window.animationToMinimized = toMinimized;
    window.animationStart = now;
    window.animationEnd = now + Layout::UI_WINDOW_ANIMATION_SECONDS;
    window.animationFrom = from;
    window.animationTo = to;
}


UIRect CurrentWindowRect( UIWindowState& window, double now )
{
    if ( !window.animationActive )
    {
        return WindowRect( window );
    }

    const double duration = window.animationEnd - window.animationStart;
    const float t = duration > 0.0 ? static_cast<float>( ( now - window.animationStart ) / duration ) : 1.0f;
    if ( t >= 1.0f )
    {
        window.animationActive = false;
        return window.animationTo;
    }

    return Layout::LerpRect( window.animationFrom, window.animationTo, t );
}


TitleButtonRects GetTitleButtonRects( const UIRect& windowBounds )
{
    return {
        { windowBounds.x + windowBounds.w - 112.0f, windowBounds.y + 8.0f, 30.0f, 28.0f },
        { windowBounds.x + windowBounds.w - 76.0f, windowBounds.y + 8.0f, 30.0f, 28.0f },
        { windowBounds.x + windowBounds.w - 40.0f, windowBounds.y + 8.0f, 30.0f, 28.0f }
    };
}


bool IsResizeHotspot( const UIRect& windowBounds, int mouseX, int mouseY )
{
    return mouseX >= windowBounds.x + windowBounds.w - 26.0f && mouseY >= windowBounds.y + windowBounds.h - 26.0f;
}


void DrawWindowAnimationShell( const UIDrawContext& draw, const UIRect& bounds )
{
    const Style::UIPalette& palette = Style::Palette();
    draw.RoundedRect( bounds.x + 4.0f, bounds.y + 6.0f, bounds.w, bounds.h, Style::Radii().window, 0.0f, 0.0f, 0.0f, 0.26f );
    draw.RoundedPanel( bounds, Style::Radii().window, palette.window, palette.border );
}


void DrawMinimizedWindow( const UIDrawContext& draw, const UIRect& minimized, const char* titleText )
{
    const Style::UIPalette& palette = Style::Palette();
    const UIRect restoreButton = { minimized.x + minimized.w - 36.0f, minimized.y + 7.0f, 26.0f, 22.0f };
    draw.RoundedRect( minimized.x + 4.0f, minimized.y + 5.0f, minimized.w, minimized.h, Style::Radii().window, 0.0f, 0.0f, 0.0f, 0.26f );
    draw.RoundedPanel( minimized, Style::Radii().window, palette.window, palette.border );
    draw.RoundedRect( minimized.x + 11.0f, minimized.y + 13.0f, 10.0f, 10.0f, 5.0f, palette.accent.r, palette.accent.g, palette.accent.b, 0.90f );
    draw.RoundedPanel( restoreButton, Style::Radii().smallButton, palette.control, palette.border );
    draw.Text( minimized.x + 32.0f, minimized.y + 11.0f, 12.5f, palette.textPrimary.r, palette.textPrimary.g, palette.textPrimary.b, titleText );
    const float plusX = restoreButton.x + restoreButton.w * 0.5f;
    const float plusY = restoreButton.y + restoreButton.h * 0.5f;
    draw.Rect( plusX - 5.0f, plusY - 1.0f, 10.0f, 2.0f, palette.textSecondary.r, palette.textSecondary.g, palette.textSecondary.b, 0.96f );
    draw.Rect( plusX - 1.0f, plusY - 5.0f, 2.0f, 10.0f, palette.textSecondary.r, palette.textSecondary.g, palette.textSecondary.b, 0.96f );
}


void DrawWindowFrame( const UIDrawContext& draw, const UIRect& bounds, float titleH, float tabH, bool blurEnabled, const char* titleText )
{
    const Style::UIPalette& palette = Style::Palette();
    Style::UIColor windowFill = palette.window;
    windowFill.a = blurEnabled ? 0.60f : palette.window.a;
    draw.RoundedRect( bounds.x + 5.0f, bounds.y + 7.0f, bounds.w, bounds.h, Style::Radii().window, 0.0f, 0.0f, 0.0f, 0.28f );
    draw.RoundedPanel( bounds, Style::Radii().window, windowFill, palette.border );
    draw.RoundedRect( bounds.x + 2.0f, bounds.y + 2.0f, bounds.w - 4.0f, titleH - 3.0f, Style::Radii().window - 2.0f, palette.windowRaised.r, palette.windowRaised.g, palette.windowRaised.b, 0.72f );
    draw.Rect( bounds.x + 2.0f, bounds.y + titleH, bounds.w - 4.0f, tabH, palette.windowSubtle.r, palette.windowSubtle.g, palette.windowSubtle.b, 0.58f );
    draw.Rect( bounds.x + 16.0f, bounds.y + titleH + tabH, bounds.w - 32.0f, 1.0f, palette.lineSoft.r, palette.lineSoft.g, palette.lineSoft.b, 0.14f );
    draw.Text( bounds.x + 20.0f, bounds.y + 12.0f, 15.5f, palette.textPrimary.r, palette.textPrimary.g, palette.textPrimary.b, titleText );
}


void DrawTitleButtons( const UIDrawContext& draw, const TitleButtonRects& buttons, bool isMaximized, int mouseX, int mouseY )
{
    Widgets::DrawTitleButton( draw, buttons.minimize, Widgets::TitleButtonIcon::Minimize, buttons.minimize.Contains( mouseX, mouseY ), false );
    Widgets::DrawTitleButton( draw, buttons.maximize, isMaximized ? Widgets::TitleButtonIcon::Restore : Widgets::TitleButtonIcon::Maximize, buttons.maximize.Contains( mouseX, mouseY ), isMaximized );
    Widgets::DrawTitleButton( draw, buttons.close, Widgets::TitleButtonIcon::Close, buttons.close.Contains( mouseX, mouseY ), false );
}

} // namespace Chrome
} // namespace UI
} // namespace SkullbonezCore
