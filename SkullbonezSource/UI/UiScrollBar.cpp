#include "UiScrollBar.h"

#include <algorithm>

namespace SkullbonezCore
{
namespace Ui
{

void UiScrollBar::SetBounds( float x, float y, float w, float h )
{
    m_track = { x, y, w, h };
}


void UiScrollBar::Draw( const UiDrawContext& draw, float contentHeight, float viewportHeight, float scrollY, double visibleUntil, double now ) const
{
    const float maxScroll = (std::max)( 0.0f, contentHeight - viewportHeight );
    if ( maxScroll <= 0.0f )
    {
        return;
    }

    const float alpha = static_cast<float>( std::clamp( visibleUntil - now, 0.0, 0.74 ) );
    if ( alpha <= 0.02f )
    {
        return;
    }

    draw.Rect( m_track.x, m_track.y, m_track.w, m_track.h, 0.05f, 0.16f, 0.22f, alpha * 0.68f );
    const float thumbH = (std::max)( 28.0f, viewportHeight * viewportHeight / contentHeight );
    const float thumbY = m_track.y + ( viewportHeight - thumbH ) * ( scrollY / maxScroll );
    draw.Rect( m_track.x - 1.0f, thumbY, m_track.w + 2.0f, thumbH, 0.32f, 0.88f, 1.0f, alpha );
}

} // namespace Ui
} // namespace SkullbonezCore
