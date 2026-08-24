/*
File: SkullbonezSource/UI/UICheckBox.cpp
Purpose:
  Adapts retained toggle bounds and caller-provided checked values to stateless
  component operations.

Summary:
  UIDrawWidgets owns toggle geometry, visual-state mapping, style, and ordered
  commands; this wrapper supplies only its bounds and disposable checked fact.

Invariants:
  - Draw and hit paths pass the same retained bounds to UIDrawWidgets.

Related:
  - SkullbonezSource/UI/UICheckBox.h
  - SkullbonezSource/UI/UIDrawWidgets.h
  - Agentic/Reference/engine-glossary.md
*/
#include "UICheckBox.h"

#include "UIDrawWidgets.h"
#include "UIStyle.h"

namespace SkullbonezCore
{
namespace UI
{

void UICheckBox::SetBounds( float x, float y, float w, float h )
{
    m_bounds = { x, y, w, h };
}


UIRect UICheckBox::Bounds() const
{
    return m_bounds;
}


bool UICheckBox::HitTest( int mouseX, int mouseY ) const
{
    constexpr UIVisualState kState = UIVisualState::Visible | UIVisualState::Enabled;
    return Widgets::CanActivateComponent( m_bounds, kState, mouseX, mouseY );
}


void UICheckBox::DrawToggle( const UIDrawContext& draw, const char* label, bool checked, float accentR, float accentG,
                             float accentB ) const
{
    UIVisualState state = UIVisualState::Visible | UIVisualState::Enabled;

    if ( checked )
    {
        state |= UIVisualState::Checked;
    }

    Widgets::DrawToggle( draw, m_bounds, label, { accentR, accentG, accentB, 1.0f }, state,
                         Widgets::ComponentAppearance::Established );
}

} // namespace UI
} // namespace SkullbonezCore
