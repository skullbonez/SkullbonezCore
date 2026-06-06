#include "UiDraw.h"

#include "../SkullbonezText.h"

#include <algorithm>
#include <cmath>

using namespace SkullbonezCore::Text;

namespace SkullbonezCore
{
namespace Ui
{

bool UiRect::Contains( int px, int py ) const
{
    return static_cast<float>( px ) >= x && static_cast<float>( px ) <= x + w &&
           static_cast<float>( py ) >= y && static_cast<float>( py ) <= y + h;
}


UiDrawContext::UiDrawContext( int screenW, int screenH )
{
    screenW = (std::max)( 1, screenW );
    screenH = (std::max)( 1, screenH );
    m_hw = Text2d::HalfW();
    m_hh = Text2d::HalfH();
    m_sx = ( m_hw * 2.0f ) / static_cast<float>( screenW );
    m_sy = ( m_hh * 2.0f ) / static_cast<float>( screenH );
}


void UiDrawContext::Rect( float x, float y, float w, float h, float r, float g, float b, float a ) const
{
    float x0 = Snap( x );
    float y0 = Snap( y );
    float x1 = Snap( x + w );
    float y1 = Snap( y + h );
    if ( x1 <= x0 && w > 0.0f )
    {
        x1 = x0 + 1.0f;
    }
    if ( y1 <= y0 && h > 0.0f )
    {
        y1 = y0 + 1.0f;
    }
    Text2d::BatchQuad( PixelX( x0 ), PixelY( y1 ), PixelX( x1 ), PixelY( y0 ), r, g, b, a );
}


void UiDrawContext::Outline( float x, float y, float w, float h, float r, float g, float b, float a ) const
{
    Rect( x, y, w, 1.0f, r, g, b, a );
    Rect( x, y + h - 1.0f, w, 1.0f, r, g, b, a );
    Rect( x, y, 1.0f, h, r, g, b, a );
    Rect( x + w - 1.0f, y, 1.0f, h, r, g, b, a );
}


void UiDrawContext::Text( float x, float y, float pxSize, float r, float g, float b, const char* value ) const
{
    const float unitSize = pxSize * m_sy;
    Text2d::Render2dTextColor( PixelX( Snap( x ) ), PixelY( Snap( y ) + pxSize ), unitSize, r, g, b, "%s", value );
}


float UiDrawContext::TextX( float x ) const
{
    return -m_hw + x * m_sx;
}


float UiDrawContext::TextY( float y ) const
{
    return m_hh - y * m_sy;
}


float UiDrawContext::HalfW() const
{
    return m_hw;
}


float UiDrawContext::HalfH() const
{
    return m_hh;
}


float UiDrawContext::ScaleY() const
{
    return m_sy;
}


float UiDrawContext::Snap( float value )
{
    return std::floor( value + 0.5f );
}


float UiDrawContext::PixelX( float x ) const
{
    return TextX( x );
}


float UiDrawContext::PixelY( float y ) const
{
    return TextY( y );
}

} // namespace Ui
} // namespace SkullbonezCore
