#include "UiTabBar.h"

#include "../SkullbonezText.h"

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
    const float tabW = m_bounds.w / static_cast<float>( tabCount );
    const int index = static_cast<int>( ( static_cast<float>( mouseX ) - m_bounds.x ) / tabW );
    return index >= 0 && index < tabCount ? index : -1;
}


void UiTabBar::Draw( const UiDrawContext& draw, const char* const* labels, int tabCount, int activeIndex ) const
{
    if ( tabCount <= 0 )
    {
        return;
    }

    const float tabW = m_bounds.w / static_cast<float>( tabCount );
    for ( int i = 0; i < tabCount; ++i )
    {
        const float tx = m_bounds.x + static_cast<float>( i ) * tabW;
        const float ty = m_bounds.y + 11.0f;
        const float pillX = tx + 2.0f;
        const float pillW = tabW - 8.0f;
        const bool active = i == activeIndex;
        draw.Rect( pillX, ty, pillW, 30.0f,
                   active ? 0.04f : 0.03f,
                   active ? 0.30f : 0.07f,
                   active ? 0.42f : 0.10f,
                   active ? 0.80f : 0.36f );
        if ( active )
        {
            draw.Rect( pillX, ty + 30.0f, pillW, 2.0f, 0.34f, 0.91f, 1.0f, 1.0f );
        }
        const float textSize = 12.5f;
        const float labelW = Text::Text2d::MeasureText( textSize, labels[i] ? labels[i] : "" );
        const float labelX = pillX + (std::max)( 6.0f, ( pillW - labelW ) * 0.5f );
        draw.Text( labelX, ty + 8.0f, textSize,
                   active ? 0.94f : 0.62f,
                   active ? 0.99f : 0.76f,
                   active ? 1.0f : 0.82f,
                   labels[i] );
    }
}

} // namespace Ui
} // namespace SkullbonezCore
