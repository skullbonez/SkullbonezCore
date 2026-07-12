/*
File: SkullbonezSource/UI/UICheckBox.cpp
Purpose:
  Implements UI CheckBox widgets, layout, drawing, or UI state for the in-engine controls.

Summary:
  UICheckBox.cpp implements UI CheckBox widgets, layout, drawing, or UI state
  for the in-engine controls. As an implementation unit, keep edits anchored
  on UI request, layout, hit-test, and draw-command flow and on the
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
  - SkullbonezSource/UI/UICheckBox.h
  - Agentic/Reference/comment-style-guide.md
*/
#include "UICheckBox.h"

#include "UIStyle.h"

#include <algorithm>

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
    return m_bounds.Contains( mouseX, mouseY );
}


void UICheckBox::DrawToggle( const UIDrawContext& draw,
                             const char* label,
                             bool checked,
                             float accentR,
                             float accentG,
                             float accentB ) const
{
    const Style::UIPalette& palette = Style::Palette();
    const Style::UIControlStyle& control = Style::Control();
    const float switchW = control.switchW;
    const float switchH = control.switchH;
    const float switchX = m_bounds.x + (std::max)( 66.0f, m_bounds.w - switchW - 4.0f );
    const float switchY = m_bounds.y + 4.0f;
    const Style::UIColor offFill = { palette.control.r, palette.control.g, palette.control.b, 0.78f };
    const Style::UIColor onFill = { accentR, accentG, accentB, 0.90f };
    const Style::UIColor knobFill = checked ? palette.accentStrong : palette.textMuted;
    const float knobSize = 10.0f;
    const float knobX = switchX + ( checked ? switchW - knobSize - 3.0f : 3.0f );

    draw.Text( m_bounds.x,
               m_bounds.y + 4.0f,
               10.5f,
               palette.textSecondary.r,
               palette.textSecondary.g,
               palette.textSecondary.b,
               label );
    draw.RoundedPanel( { switchX, switchY, switchW, switchH },
                       switchH * 0.5f,
                       checked ? onFill : offFill,
                       palette.border );
    draw.RoundedRect( knobX,
                      switchY + 3.0f,
                      knobSize,
                      knobSize,
                      knobSize * 0.5f,
                      knobFill.r,
                      knobFill.g,
                      knobFill.b,
                      0.98f );
}

} // namespace UI
} // namespace SkullbonezCore
