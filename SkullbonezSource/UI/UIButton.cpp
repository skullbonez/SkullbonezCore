/*
File: SkullbonezSource/UI/UIButton.cpp
Purpose:
  Adapts retained button bounds and legacy pointer inputs to stateless
  component operations.

Summary:
  The wrapper resolves only disposable hover state. UIDrawWidgets remains the
  single owner of button hit policy, text measurement, style, and draw output.

Invariants:
  - Draw and hit paths pass the same retained bounds to UIDrawWidgets.

Related:
  - SkullbonezSource/UI/UIButton.h
  - SkullbonezSource/UI/UIDrawWidgets.h
  - Agentic/Reference/engine-glossary.md
*/
#include "UIButton.h"

#include "UIDrawWidgets.h"

namespace SkullbonezCore
{
namespace UI
{

void UIButton::SetBounds( float x, float y, float w, float h )
{
    m_bounds = { x, y, w, h };
}


UIRect UIButton::Bounds() const
{
    return m_bounds;
}


bool UIButton::HitTest( int mouseX, int mouseY ) const
{
    constexpr UIVisualState kState = UIVisualState::Visible | UIVisualState::Enabled;
    return Widgets::CanActivateComponent( m_bounds, kState, mouseX, mouseY );
}


void UIButton::Draw( const UIDrawContext& draw, const char* label, int mouseX, int mouseY ) const
{
    UIVisualState state = UIVisualState::Visible | UIVisualState::Enabled;

    if ( Widgets::ContainsComponent( m_bounds, state, mouseX, mouseY ) )
    {
        state |= UIVisualState::Hovered;
    }

    Widgets::DrawButton( draw, m_bounds, label, state, Widgets::ComponentAppearance::Established );
}

} // namespace UI
} // namespace SkullbonezCore
