#include "UISlider.h"

#include "../SkullbonezText.h"

#include <algorithm>
#include <cmath>

namespace SkullbonezCore
{
namespace UI
{

void UISlider::SetBounds( float x, float y, float w, float h )
{
    m_bounds = { x, y, w, h };
}


bool UISlider::HitTest( int mouseX, int mouseY ) const
{
    return m_bounds.Contains( mouseX, mouseY );
}


float UISlider::ValueFromMouse( int mouseX, float minValue, float maxValue, float step ) const
{
    maxValue = (std::max)( minValue, maxValue );
    const float trackX = TrackX();
    const float trackW = TrackW();
    const float t = trackW > 1.0f ? std::clamp( ( static_cast<float>( mouseX ) - trackX ) / trackW, 0.0f, 1.0f ) : 0.0f;
    float value = minValue + ( maxValue - minValue ) * t;
    if ( step > 0.0f )
    {
        value = minValue + std::round( ( value - minValue ) / step ) * step;
    }
    return std::clamp( value, minValue, maxValue );
}


void UISlider::Draw( const UIDrawContext& draw, const char* label, const char* valueText, float value, float minValue, float maxValue ) const
{
    maxValue = (std::max)( minValue, maxValue );
    const float trackX = TrackX();
    const float trackW = TrackW();
    const float trackY = m_bounds.y + 17.0f;
    const float t = maxValue > minValue ? std::clamp( ( value - minValue ) / ( maxValue - minValue ), 0.0f, 1.0f ) : 0.0f;
    const float knobX = trackX + trackW * t;
    const float textSize = 10.5f;
    const float valueW = Text::Text2d::MeasureText( textSize, valueText ? valueText : "" );
    const float valueX = m_bounds.x + m_bounds.w - valueW - 4.0f;

    draw.Text( m_bounds.x, m_bounds.y + 1.0f, textSize, 0.74f, 0.82f, 0.84f, label );
    draw.Text( valueX, m_bounds.y + 1.0f, textSize, 0.76f, 0.96f, 1.0f, valueText );
    draw.Rect( trackX, trackY, trackW, 3.0f, 0.05f, 0.12f, 0.15f, 0.92f );
    draw.Rect( trackX, trackY, trackW * t, 3.0f, 0.24f, 0.78f, 0.96f, 0.90f );
    draw.Outline( trackX, trackY - 2.0f, trackW, 7.0f, 0.16f, 0.32f, 0.38f, 0.70f );
    draw.Rect( knobX - 5.0f, trackY - 6.0f, 10.0f, 15.0f, 0.58f, 0.94f, 1.0f, 0.98f );
    draw.Outline( knobX - 5.0f, trackY - 6.0f, 10.0f, 15.0f, 0.86f, 0.99f, 1.0f, 0.96f );
}


float UISlider::TrackX() const
{
    return m_bounds.x + 118.0f;
}


float UISlider::TrackW() const
{
    return (std::max)( 80.0f, m_bounds.w - 190.0f );
}

} // namespace UI
} // namespace SkullbonezCore
