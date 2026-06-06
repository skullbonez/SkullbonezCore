#include "UIButton.h"

#include "../SkullbonezText.h"

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
    const float textSize = 11.0f;
    const float labelW = Text::Text2d::MeasureText( textSize, label ? label : "" );
    const float labelX = m_bounds.x + (std::max)( 8.0f, ( m_bounds.w - labelW ) * 0.5f );
    const float labelY = m_bounds.y + ( m_bounds.h - textSize ) * 0.5f - 1.0f;
    draw.Rect( m_bounds.x, m_bounds.y, m_bounds.w, m_bounds.h,
               hot ? 0.050f : 0.026f,
               hot ? 0.250f : 0.100f,
               hot ? 0.330f : 0.132f,
               hot ? 0.92f : 0.78f );
    draw.Outline( m_bounds.x, m_bounds.y, m_bounds.w, m_bounds.h,
                  hot ? 0.44f : 0.24f,
                  hot ? 0.92f : 0.58f,
                  hot ? 1.0f : 0.70f,
                  hot ? 0.96f : 0.78f );
    draw.Text( labelX, labelY, textSize,
               hot ? 0.96f : 0.78f,
               hot ? 1.0f : 0.92f,
               1.0f,
               label );
}

} // namespace UI
} // namespace SkullbonezCore
