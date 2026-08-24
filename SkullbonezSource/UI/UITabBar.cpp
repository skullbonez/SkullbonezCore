/*
File: SkullbonezSource/UI/UITabBar.cpp
Purpose:
  Adapts retained tab-strip bounds to stateless tab geometry, hit, and draw
  operations.

Summary:
  UIDrawWidgets owns tab partitioning, text fitting, style, and ordered draw
  commands; this wrapper supplies only bounds, labels, and disposable selection.

Invariants:
  - Hit routing and drawing select the same Established appearance profile.

Related:
  - SkullbonezSource/UI/UITabBar.h
  - SkullbonezSource/UI/UIDrawWidgets.h
  - Agentic/Reference/engine-glossary.md
*/
#include "UITabBar.h"

#include "UIDrawWidgets.h"

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
    constexpr UIVisualState kState = UIVisualState::Visible | UIVisualState::Enabled;
    return Widgets::HitTestTab( m_bounds, kState, mouseX, mouseY, tabCount, Widgets::ComponentAppearance::Established );
}


void UITabBar::Draw( const UIDrawContext& draw, const char* const* labels, int tabCount, int activeIndex ) const
{
    for ( int tabIndex = 0; tabIndex < tabCount; ++tabIndex )
    {
        UIVisualState state = UIVisualState::Visible | UIVisualState::Enabled;

        if ( tabIndex == activeIndex )
        {
            state |= UIVisualState::Selected;
        }

        const Widgets::TabLayout layout = Widgets::ResolveTabLayout( m_bounds, tabIndex, tabCount,
                                                                     Widgets::ComponentAppearance::Established );
        Widgets::DrawTab( draw, layout.visualBounds, labels ? labels[tabIndex] : nullptr, state,
                          Widgets::ComponentAppearance::Established );
    }
}

} // namespace UI
} // namespace SkullbonezCore
