#include "UIButton.h"

#include "../SkullbonezText.h"
#include "UIStyle.h"

#include <algorithm>

namespace SkullbonezCore
{
namespace UI
{

void UIButton::SetBounds( float x, float y, float w, float h )
{
    m_bounds = { x, y, w, h };
}


bool UIButton::HitTest( int mouseX, int mouseY ) const
{
    return m_bounds.Contains( mouseX, mouseY );
}


void UIButton::Draw( const UIDrawContext& draw, const char* label, int mouseX, int mouseY ) const
{
    const bool hot = HitTest( mouseX, mouseY );
    const Style::UIPalette& palette = Style::Palette();
    const float radius = Style::Radii().control;
    const float textSize = 11.0f;
    const float labelW = Text::Text2d::MeasureText( textSize, label ? label : "" );
    const float labelX = m_bounds.x + (std::max)( 8.0f, ( m_bounds.w - labelW ) * 0.5f );
    const float labelY = m_bounds.y + ( m_bounds.h - textSize ) * 0.5f - 1.0f;
    draw.RoundedPanel( m_bounds, radius, hot ? palette.controlHover : palette.control, palette.border );
    draw.Text( labelX, labelY, textSize,
               hot ? palette.textPrimary.r : palette.textSecondary.r,
               hot ? palette.textPrimary.g : palette.textSecondary.g,
               hot ? palette.textPrimary.b : palette.textSecondary.b,
               label );
}

} // namespace UI
} // namespace SkullbonezCore
