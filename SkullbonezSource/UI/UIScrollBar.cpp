/*
File: SkullbonezSource/UI/UIScrollBar.cpp
Purpose:
  Adapts retained scrollbar track bounds and time-derived visibility to the
  stateless scrollbar contract.

Summary:
  UIDrawWidgets owns thumb projection, style, and command recording; this
  wrapper retains only the track established by layout.

Invariants:
  - Visibility time is converted to one frame-local alpha before delegation.

Related:
  - SkullbonezSource/UI/UIScrollBar.h
  - SkullbonezSource/UI/UIDrawWidgets.h
  - Agentic/Reference/engine-glossary.md
*/
#include "UIScrollBar.h"

#include "UIDrawWidgets.h"

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
    const float alpha = static_cast<float>( std::clamp( visibleUntil - now, 0.0, 0.74 ) );
    constexpr UIVisualState kState = UIVisualState::Visible | UIVisualState::Enabled;
    Widgets::DrawScrollBar( draw, m_track, contentHeight, viewportHeight, scrollY, alpha, kState,
                            Widgets::ComponentAppearance::Established );
}

} // namespace UI
} // namespace SkullbonezCore
