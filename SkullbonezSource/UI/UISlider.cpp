#include "UISlider.h"

#include "../SkullbonezText.h"
#include "UIStyle.h"

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
    const Style::UIPalette& palette = Style::Palette();
    const Style::UIControlStyle& control = Style::Control();
    maxValue = (std::max)( minValue, maxValue );
    const float trackX = TrackX();
    const float trackW = TrackW();
    const float trackH = control.sliderTrackHeight;
    const float trackY = m_bounds.y + 17.0f;
    const float t = maxValue > minValue ? std::clamp( ( value - minValue ) / ( maxValue - minValue ), 0.0f, 1.0f ) : 0.0f;
    const float knobX = trackX + trackW * t;
    const float textSize = 10.5f;
    const float valueW = Text::Text2d::MeasureText( textSize, valueText ? valueText : "" );
    const float valueX = m_bounds.x + m_bounds.w - valueW - 4.0f;

    draw.Text( m_bounds.x, m_bounds.y + 1.0f, textSize, palette.textSecondary.r, palette.textSecondary.g, palette.textSecondary.b, label );
    draw.Text( valueX, m_bounds.y + 1.0f, textSize, palette.accentStrong.r, palette.accentStrong.g, palette.accentStrong.b, valueText );
    draw.RoundedRect( trackX, trackY, trackW, trackH, trackH * 0.5f, palette.control.r, palette.control.g, palette.control.b, 0.78f );
    draw.RoundedRect( trackX, trackY, (std::max)( trackH, trackW * t ), trackH, trackH * 0.5f, palette.accent.r, palette.accent.g, palette.accent.b, 0.90f );
    draw.RoundedPanel( { knobX - 5.0f, trackY - 5.0f, 10.0f, 16.0f }, 5.0f, palette.accentStrong, palette.border );
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
