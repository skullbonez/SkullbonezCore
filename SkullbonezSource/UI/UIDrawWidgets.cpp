/*
File: SkullbonezSource/UI/UIDrawWidgets.cpp
Purpose:
  Implements UI DrawWidgets widgets, layout, drawing, or UI state for the in-engine controls.

Summary:
  UIDrawWidgets.cpp implements UI DrawWidgets widgets, layout, drawing, or UI
  state for the in-engine controls. As an implementation unit, keep edits
  anchored on UI request, layout, hit-test, and draw-command flow and on the
  glossary/invariants below.

Glossary:
  Draw command: Lightweight record describing a UI shape or text batch to
  render later in the frame.
  Hit box: Screen-space rectangle used to decide whether mouse input targets a
  widget.

Invariants:
  - Draw geometry and hit testing must be derived from the same layout
  constants.

Related:
  - SkullbonezSource/UI/UIDrawWidgets.h
  - Agentic/Reference/comment-style-guide.md
*/
#include "UIDrawWidgets.h"
#include "UIFontMetrics.h"
#include "UICheckBox.h"
#include "UIStyle.h"

#include <algorithm>

namespace SkullbonezCore
{
namespace UI
{
namespace Widgets
{

bool IsRowVisible( float contentY, float contentH, float rowY, float rowH )
{
    return rowY >= contentY && rowY + rowH <= contentY + contentH;
}


void DrawTitleButton( const UIDrawContext& draw, const UIRect& bounds, TitleButtonIcon icon, bool hot, bool active )
{
    const Style::UIPalette& palette = Style::Palette();
    const Style::UIColor bg = hot ? palette.controlHover : ( active ? palette.windowRaised : palette.control );
    const Style::UIColor iconColor = icon == TitleButtonIcon::Close && hot ? palette.warningAccent : palette.textSecondary;

    const float iconR = iconColor.r;
    const float iconG = iconColor.g;
    const float iconB = iconColor.b;
    const float iconA = hot || active ? 0.98f : 0.88f;
    const float cx = bounds.x + bounds.w * 0.5f;
    const float cy = bounds.y + bounds.h * 0.5f;

    draw.RoundedPanel( bounds, Style::Radii().smallButton, bg, palette.border );

    switch ( icon )
    {
    case TitleButtonIcon::Minimize:
        draw.Rect( cx - 5.0f, cy + 4.0f, 10.0f, 2.0f, iconR, iconG, iconB, iconA );
        break;
    case TitleButtonIcon::Maximize:
        draw.Outline( cx - 6.0f, cy - 6.0f, 12.0f, 12.0f, iconR, iconG, iconB, iconA );
        draw.Rect( cx - 6.0f, cy - 6.0f, 12.0f, 2.0f, iconR, iconG, iconB, iconA );
        break;
    case TitleButtonIcon::Restore:
        draw.Outline( cx - 2.0f, cy - 7.0f, 10.0f, 10.0f, iconR, iconG, iconB, iconA * 0.72f );
        draw.Rect( cx - 2.0f, cy - 7.0f, 10.0f, 2.0f, iconR, iconG, iconB, iconA * 0.72f );
        draw.Outline( cx - 7.0f, cy - 2.0f, 10.0f, 10.0f, iconR, iconG, iconB, iconA );
        draw.Rect( cx - 7.0f, cy - 2.0f, 10.0f, 2.0f, iconR, iconG, iconB, iconA );
        break;
    case TitleButtonIcon::Close:

        for ( int i = 0; i < 5; ++i )
        {
            const float offset = static_cast<float>( i ) * 2.0f;
            draw.Rect( cx - 5.0f + offset, cy - 5.0f + offset, 2.0f, 2.0f, iconR, iconG, iconB, iconA );

            if ( i != 2 )
            {
                draw.Rect( cx + 3.0f - offset, cy - 5.0f + offset, 2.0f, 2.0f, iconR, iconG, iconB, iconA );
            }
        }

        break;
    }
}


void DrawPipelineStepButton( const UIDrawContext& draw, const UIRect& bounds, bool previous, bool hot )
{
    const Style::UIPalette& palette = Style::Palette();
    const Style::UIColor bg = hot ? palette.controlHover : palette.control;
    const Style::UIColor icon = hot ? palette.textPrimary : palette.textSecondary;
    const float cx = bounds.x + bounds.w * 0.5f;
    const float cy = bounds.y + bounds.h * 0.5f;
    const float tipX = previous ? cx - 4.0f : cx + 4.0f;
    const float rearX = previous ? cx + 4.0f : cx - 4.0f;

    draw.RoundedPanel( bounds, Style::Radii().smallButton, bg, palette.border );
    draw.Triangle( tipX, cy, rearX, cy - 5.5f, rearX, cy + 5.5f, icon.r, icon.g, icon.b, hot ? 0.98f : 0.88f );
}


void DrawFooterToggle( const UIDrawContext& draw, const UIRect& bounds, const char* label, bool checked )
{
    const Style::FooterToggleStyle& style = Style::FooterToggle();
    const Style::UIPalette& palette = Style::Palette();
    const Style::UIColor& accent = Style::Accent();
    const float switchW = style.switchW;
    const float switchH = style.switchH;
    const float switchX = bounds.x + bounds.w - switchW - 2.0f;
    const float switchY = bounds.y + 5.0f;
    const float labelAreaW = (std::max)( 1.0f, switchX - bounds.x - 6.0f );
    const float labelW = UIFontMetrics::MeasureText( style.labelTextSize, label );
    const float labelX = bounds.x + (std::max)( 0.0f, ( labelAreaW - labelW ) * 0.5f );

    draw.Text( labelX, bounds.y + 4.0f, style.labelTextSize, style.label.r, style.label.g, style.label.b, label );
    const Style::UIColor offFill = { palette.control.r, palette.control.g, palette.control.b, 0.78f };
    draw.RoundedPanel( { switchX, switchY, switchW, switchH }, switchH * 0.5f, checked ? accent : offFill, palette.border );

    draw.RoundedRect( switchX + ( checked ? switchW - style.knobW - 3.0f : 3.0f ), bounds.y + 8.0f, style.knobW, style.knobH,
                      style.knobW * 0.5f, checked ? palette.accentStrong.r : palette.textMuted.r,
                      checked ? palette.accentStrong.g : palette.textMuted.g,
                      checked ? palette.accentStrong.b : palette.textMuted.b, 0.96f );
}


void DrawLabelValueAt( const UIDrawContext& draw, float contentY, float contentH, float tx, float rowY, const char* label,
                       const char* value, float vr, float vg, float vb )
{

    if ( !IsRowVisible( contentY, contentH, rowY, 18.0f ) )
    {
        return;
    }

    const Style::UIPalette& palette = Style::Palette();
    draw.Text( tx, rowY, 11.5f, palette.textSecondary.r, palette.textSecondary.g, palette.textSecondary.b, label );
    draw.Text( tx + 126.0f, rowY, 11.5f, vr, vg, vb, value );
}


void DrawSectionTitle( const UIDrawContext& draw, float contentX, float contentY, float contentH, float rowY, float textSize,
                       const char* text )
{

    if ( !IsRowVisible( contentY, contentH, rowY, textSize + 4.0f ) )
    {
        return;
    }

    const Style::UIColor& section = Style::Palette().textPrimary;
    draw.Text( contentX, rowY, textSize, section.r, section.g, section.b, text );
}


void DrawContentToggle( const UIDrawContext& draw, float contentY, float contentH, UICheckBox& toggle, float tx, float rowY,
                        float controlW, const char* label, bool checked )
{

    if ( !IsRowVisible( contentY, contentH, rowY, 24.0f ) )
    {
        return;
    }

    const Style::UIColor& accent = Style::Accent();
    toggle.SetBounds( tx, rowY, controlW, 24.0f );
    toggle.DrawToggle( draw, label, checked, accent.r, accent.g, accent.b );
}


void DrawFooterStatCell( const UIDrawContext& draw, float tx, float bottomY, const char* name, const char* value, float r,
                         float g, float b )
{
    const Style::UIPalette& palette = Style::Palette();
    draw.Text( tx, bottomY + 25.0f, 10.0f, palette.textMuted.r, palette.textMuted.g, palette.textMuted.b, name );
    draw.Text( tx, bottomY + 47.0f, 11.5f, r, g, b, value );
}


void DrawCompactFooterStat( const UIDrawContext& draw, float statsX, float ty, const char* name, const char* value, float r,
                            float g, float b )
{
    const Style::UIPalette& palette = Style::Palette();
    draw.Text( statsX + 12.0f, ty, 9.0f, palette.textMuted.r, palette.textMuted.g, palette.textMuted.b, name );
    draw.Text( statsX + 66.0f, ty, 9.5f, r, g, b, value );
}


void DrawFooterStatDivider( const UIDrawContext& draw, float x, float bottomY )
{
    const Style::UIColor& line = Style::Palette().lineSoft;
    draw.Rect( x, bottomY + 23.0f, 1.0f, 42.0f, line.r, line.g, line.b, 0.16f );
}

} // namespace Widgets
} // namespace UI
} // namespace SkullbonezCore
