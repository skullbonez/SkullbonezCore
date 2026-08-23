/*
File: SkullbonezSource/UI/UIIconButton.cpp
Purpose:
  Adapts retained icon-button bounds and caller-provided expander state to
  stateless component operations.

Summary:
  UIDrawWidgets owns hit policy, glyph geometry, style, and command recording;
  this wrapper retains only layout bounds.

Invariants:
  - Hit and draw paths pass the same retained bounds downstream.

Related:
  - SkullbonezSource/UI/UIIconButton.h
  - SkullbonezSource/UI/UIDrawWidgets.h
  - Agentic/Reference/engine-glossary.md
*/
#include "UIIconButton.h"

#include "UIDrawWidgets.h"

namespace SkullbonezCore
{
namespace UI
{

void UIIconButton::SetBounds( float x, float y, float w, float h )
{
    m_bounds = { x, y, w, h };
}


bool UIIconButton::HitTest( int mouseX, int mouseY ) const
{
    constexpr UIVisualState kState = UIVisualState::Visible | UIVisualState::Enabled;
    return Widgets::CanActivateComponent( m_bounds, kState, mouseX, mouseY );
}


void UIIconButton::DrawExpander( const UIDrawContext& draw, bool expanded ) const
{
    constexpr UIVisualState kState = UIVisualState::Visible | UIVisualState::Enabled;
    Widgets::DrawIconButton( draw, m_bounds, expanded ? Widgets::ComponentIcon::Minus : Widgets::ComponentIcon::Plus, kState,
                             Widgets::ComponentAppearance::Established );
}

} // namespace UI
} // namespace SkullbonezCore
