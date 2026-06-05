#include "UiTabBar.h"

#include <algorithm>

namespace SkullbonezCore
{
namespace Ui
{

void UiTabBar::SetBounds( float x, float y, float w, float h )
{
    m_bounds = { x, y, w, h };
}


int UiTabBar::HitTest( int mouseX, int mouseY, int tabCount ) const
{
    if ( tabCount <= 0 || !m_bounds.Contains( mouseX, mouseY ) )
    {
        return -1;
    }
    const float tabW = (std::max)( 62.0f, m_bounds.w / static_cast<float>( tabCount ) );
    const int index = static_cast<int>( ( static_cast<float>( mouseX ) - m_bounds.x ) / tabW );
    return index >= 0 && index < tabCount ? index : -1;
}


void UiTabBar::Draw( const UiDrawContext& draw, const char* const* labels, int tabCount, int activeIndex ) const
{
    if ( tabCount <= 0 )
    {
        return;
    }

    const float tabW = (std::max)( 62.0f, m_bounds.w / static_cast<float>( tabCount ) );
    for ( int i = 0; i < tabCount; ++i )
    {
        const float tx = m_bounds.x + static_cast<float>( i ) * tabW;
        const float ty = m_bounds.y + 11.0f;
        const bool active = i == activeIndex;
        draw.Rect( tx + 2.0f, ty, tabW - 8.0f, 30.0f,
                   active ? 0.04f : 0.03f,
                   active ? 0.30f : 0.07f,
                   active ? 0.42f : 0.10f,
                   active ? 0.80f : 0.36f );
        if ( active )
        {
            draw.Rect( tx + 2.0f, ty + 30.0f, tabW - 8.0f, 2.0f, 0.34f, 0.91f, 1.0f, 1.0f );
        }
        draw.Text( tx + 12.0f, ty + 8.0f, 12.5f,
                   active ? 0.94f : 0.62f,
                   active ? 0.99f : 0.76f,
                   active ? 1.0f : 0.82f,
                   labels[i] );
    }
}

} // namespace Ui
} // namespace SkullbonezCore
