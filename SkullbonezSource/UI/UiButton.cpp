#include "UiButton.h"

namespace SkullbonezCore
{
namespace Ui
{

void UiButton::SetBounds( float x, float y, float w, float h )
{
    m_bounds = { x, y, w, h };
}


bool UiButton::HitTest( int mouseX, int mouseY ) const
{
    return m_bounds.Contains( mouseX, mouseY );
}


void UiButton::Draw( const UiDrawContext& draw, const char* label, int mouseX, int mouseY ) const
{
    const bool hot = HitTest( mouseX, mouseY );
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
    draw.Text( m_bounds.x + 15.0f, m_bounds.y + 8.0f, 11.0f,
               hot ? 0.96f : 0.78f,
               hot ? 1.0f : 0.92f,
               1.0f,
               label );
}

} // namespace Ui
} // namespace SkullbonezCore
