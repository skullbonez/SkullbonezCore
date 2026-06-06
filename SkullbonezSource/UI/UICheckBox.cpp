#include "UICheckBox.h"

#include <algorithm>

namespace SkullbonezCore
{
namespace UI
{

void UICheckBox::SetBounds( float x, float y, float w, float h )
{
    m_bounds = { x, y, w, h };
}


bool UICheckBox::HitTest( int mouseX, int mouseY ) const
{
    return m_bounds.Contains( mouseX, mouseY );
}


void UICheckBox::DrawToggle( const UIDrawContext& draw, const char* label, bool checked, float accentR, float accentG, float accentB ) const
{
    const float switchX = m_bounds.x + (std::max)( 66.0f, m_bounds.w - 34.0f );
    draw.Text( m_bounds.x, m_bounds.y + 4.0f, 10.5f, 0.74f, 0.82f, 0.84f, label );
    draw.Rect( switchX, m_bounds.y + 5.0f, 28.0f, 14.0f,
               checked ? accentR * 0.32f : 0.05f,
               checked ? accentG * 0.32f : 0.08f,
               checked ? accentB * 0.32f : 0.09f,
               0.92f );
    draw.Outline( switchX, m_bounds.y + 5.0f, 28.0f, 14.0f,
                  checked ? accentR : 0.20f,
                  checked ? accentG : 0.30f,
                  checked ? accentB : 0.34f,
                  checked ? 0.82f : 0.58f );
    draw.Rect( switchX + ( checked ? 14.0f : 2.0f ), m_bounds.y + 7.0f, 10.0f, 10.0f,
               checked ? 0.82f : 0.34f,
               checked ? 0.98f : 0.46f,
               checked ? 1.0f : 0.52f,
               0.96f );
}

} // namespace UI
} // namespace SkullbonezCore
