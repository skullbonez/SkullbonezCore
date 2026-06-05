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
    draw.Text( m_bounds.x + 4.0f, m_bounds.y + 0.5f, 11.0f, 0.82f, 0.98f, 1.0f, expanded ? "-" : "+" );
}

} // namespace Ui
} // namespace SkullbonezCore
