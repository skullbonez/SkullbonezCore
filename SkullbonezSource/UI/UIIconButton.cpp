/*
File: SkullbonezSource/UI/UIIconButton.cpp
Purpose:
  Implements compact icon-button geometry, hit testing, and chevron drawing.

Summary:
  Keeps expander hit testing and chevron
  drawing on one shared bounds rectangle.

Invariants:
  - Draw geometry and hit testing must be derived from the same layout
  constants.

Related:
  - SkullbonezSource/UI/UIIconButton.h
  - Agentic/Reference/engine-glossary.md
*/
#include "UIIconButton.h"
#include "UIStyle.h"

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
    return m_bounds.Contains( mouseX, mouseY );
}


void UIIconButton::DrawExpander( const UIDrawContext& draw, bool expanded ) const
{
    const Style::UIPalette& palette = Style::Palette();
    draw.RoundedPanel( m_bounds, Style::Radii().smallButton, palette.control, palette.border );

    const float cx = m_bounds.x + m_bounds.w * 0.5f;
    const float cy = m_bounds.y + m_bounds.h * 0.5f;
    draw.Rect( cx - 4.0f, cy - 1.0f, 8.0f, 2.0f, palette.textSecondary.r, palette.textSecondary.g, palette.textSecondary.b,
               0.96f );

    if ( !expanded )
    {
        draw.Rect( cx - 1.0f, cy - 4.0f, 2.0f, 8.0f, palette.textSecondary.r, palette.textSecondary.g,
                   palette.textSecondary.b, 0.96f );
    }
}

} // namespace UI
} // namespace SkullbonezCore
