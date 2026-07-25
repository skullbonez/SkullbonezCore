/*
File: SkullbonezSource/UI/UITabBar.cpp
Purpose:
  Implements UI TabBar widgets, layout, drawing, or UI state for the in-engine controls.

Summary:
  UITabBar.cpp implements UI TabBar widgets, layout, drawing, or UI state for
  the in-engine controls. As an implementation unit, keep edits anchored on UI
  request, layout, hit-test, and draw-command flow and on the
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
  - SkullbonezSource/UI/UITabBar.h
  - Agentic/Reference/comment-style-guide.md
*/
#include "UITabBar.h"

#include "../Rendering/Text.h"
#include "UIStyle.h"

#include <algorithm>

namespace SkullbonezCore
{
namespace UI
{

void UITabBar::SetBounds( float x, float y, float w, float h )
{
    m_bounds = { x, y, w, h };
}


UIRect UITabBar::Bounds() const
{
    return m_bounds;
}


int UITabBar::HitTest( int mouseX, int mouseY, int tabCount ) const
{
    if ( tabCount <= 0 || !m_bounds.Contains( mouseX, mouseY ) )
    {
        return -1;
    }
    const float tabW = m_bounds.w / static_cast<float>( tabCount );
    const int index = static_cast<int>( ( static_cast<float>( mouseX ) - m_bounds.x ) / tabW );
    return index >= 0 && index < tabCount ? index : -1;
}


void UITabBar::Draw( const UIDrawContext& draw, const char* const* labels, int tabCount, int activeIndex ) const
{
    if ( tabCount <= 0 )
    {
        return;
    }

    const float tabW = m_bounds.w / static_cast<float>( tabCount );
    const Style::UIPalette& palette = Style::Palette();
    const float radius = Style::Radii().control;
    for ( int i = 0; i < tabCount; ++i )
    {
        const float tx = m_bounds.x + static_cast<float>( i ) * tabW;
        const float ty = m_bounds.y + 11.0f;
        const float pillX = tx + 2.0f;
        const float pillW = tabW - 8.0f;
        const bool active = i == activeIndex;
        if ( active )
        {
            draw.RoundedPanel( { pillX, ty, pillW, 30.0f }, radius, palette.windowRaised, palette.innerBorder );
        }
        else
        {
            draw.RoundedRect(
                pillX,
                ty,
                pillW,
                30.0f,
                radius,
                palette.windowSubtle.r,
                palette.windowSubtle.g,
                palette.windowSubtle.b,
                0.20f
            );
        }
        if ( active )
        {
            draw.Rect(
                pillX + 8.0f,
                ty + 29.0f,
                (std::max)( 1.0f, pillW - 16.0f ),
                2.0f,
                palette.accent.r,
                palette.accent.g,
                palette.accent.b,
                0.86f
            );
        }
        float textSize = 11.5f;
        while ( textSize > 8.5f && Text::Text2d::MeasureText( textSize, labels[i] ? labels[i] : "" ) > pillW - 10.0f )
        {
            textSize -= 0.5f;
        }
        const float labelW = Text::Text2d::MeasureText( textSize, labels[i] ? labels[i] : "" );
        const float labelX = pillX + (std::max)( 6.0f, ( pillW - labelW ) * 0.5f );
        draw.Text(
            labelX,
            ty + 8.0f,
            textSize,
            active ? palette.textPrimary.r : palette.textSecondary.r,
            active ? palette.textPrimary.g : palette.textSecondary.g,
            active ? palette.textPrimary.b : palette.textSecondary.b,
            labels[i]
        );
    }
}

} // namespace UI
} // namespace SkullbonezCore
