#include "UiIconButton.h"

namespace SkullbonezCore
{
namespace Ui
{

void UiIconButton::SetBounds( float x, float y, float w, float h )
{
    m_bounds = { x, y, w, h };
}


bool UiIconButton::HitTest( int mouseX, int mouseY ) const
{
    return m_bounds.Contains( mouseX, mouseY );
}


void UiIconButton::DrawExpander( const UiDrawContext& draw, bool expanded ) const
{
    draw.Rect( m_bounds.x, m_bounds.y, m_bounds.w, m_bounds.h, 0.03f, 0.16f, 0.20f, 0.86f );
    draw.Outline( m_bounds.x, m_bounds.y, m_bounds.w, m_bounds.h, 0.28f, 0.82f, 0.95f, 0.74f );

    const float cx = m_bounds.x + m_bounds.w * 0.5f;
    const float cy = m_bounds.y + m_bounds.h * 0.5f;
    draw.Rect( cx - 4.0f, cy - 1.0f, 8.0f, 2.0f, 0.82f, 0.98f, 1.0f, 0.96f );
    if ( !expanded )
    {
        draw.Rect( cx - 1.0f, cy - 4.0f, 2.0f, 8.0f, 0.82f, 0.98f, 1.0f, 0.96f );
    }
}

} // namespace Ui
} // namespace SkullbonezCore
