/*
File: SkullbonezSource/UI/UIScrollBar.cpp
Purpose:
  Implements UI ScrollBar widgets, layout, drawing, or UI state for the in-engine controls.

Summary:
  Projects content height and scroll offset
  into one clipped viewport track and thumb.

Invariants:
  - Draw geometry and hit testing must be derived from the same layout
  constants.

Related:
  - SkullbonezSource/UI/UIScrollBar.h
  - Agentic/Reference/comment-style-guide.md
  - Agentic/Reference/engine-glossary.md
*/
#include "UIScrollBar.h"

#include "UIStyle.h"

#include <algorithm>

namespace SkullbonezCore
{
namespace UI
{

void UIScrollBar::SetBounds( float x, float y, float w, float h )
{
    m_track = { x, y, w, h };
}


UIRect UIScrollBar::Bounds() const
{
    return m_track;
}


void UIScrollBar::Draw( const UIDrawContext& draw, float contentHeight, float viewportHeight, float scrollY,
                        double visibleUntil, double now ) const
{
    const float maxScroll = (std::max)( 0.0f, contentHeight - viewportHeight );

    if ( maxScroll <= 0.0f )
    {
        return;
    }

    const float alpha = static_cast<float>( std::clamp( visibleUntil - now, 0.0, 0.74 ) );

    if ( alpha <= 0.02f )
    {
        return;
    }

    const Style::UIPalette& palette = Style::Palette();
    draw.RoundedRect( m_track.x, m_track.y, m_track.w, m_track.h, m_track.w * 0.5f, palette.control.r, palette.control.g,
                      palette.control.b, alpha * 0.52f );

    const float thumbH = (std::max)( 28.0f, viewportHeight * viewportHeight / contentHeight );
    const float thumbY = m_track.y + ( viewportHeight - thumbH ) * ( scrollY / maxScroll );
    draw.RoundedRect( m_track.x - 1.0f, thumbY, m_track.w + 2.0f, thumbH, ( m_track.w + 2.0f ) * 0.5f, palette.accent.r,
                      palette.accent.g, palette.accent.b, alpha );
}

} // namespace UI
} // namespace SkullbonezCore
