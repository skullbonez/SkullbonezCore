#include "UIDraw.h"

#include "../SkullbonezText.h"
#include "UIDrawList.h"

#include <algorithm>
#include <cmath>

using namespace SkullbonezCore::Text;

namespace SkullbonezCore
{
namespace UI
{

bool UIRect::Contains( int px, int py ) const
{
    return static_cast<float>( px ) >= x && static_cast<float>( px ) <= x + w &&
           static_cast<float>( py ) >= y && static_cast<float>( py ) <= y + h;
}


UIDrawContext::UIDrawContext( int screenW, int screenH, UIDrawList* drawList )
{
    screenW = (std::max)( 1, screenW );
    screenH = (std::max)( 1, screenH );
    m_hw = Text2d::HalfW();
    m_hh = Text2d::HalfH();
    m_sx = ( m_hw * 2.0f ) / static_cast<float>( screenW );
    m_sy = ( m_hh * 2.0f ) / static_cast<float>( screenH );
    m_drawList = drawList;
}


void UIDrawContext::Rect( float x, float y, float w, float h, float r, float g, float b, float a ) const
{
    if ( m_drawList )
    {
        m_drawList->AddRect( x, y, w, h, r, g, b, a );
        return;
    }

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


void UIDrawContext::Triangle( float x0, float y0, float x1, float y1, float x2, float y2, float r, float g, float b, float a ) const
{
    if ( m_drawList )
    {
        m_drawList->AddTriangle( x0, y0, x1, y1, x2, y2, r, g, b, a );
        return;
    }

    Text2d::BatchTriangle( PixelXUnsnapped( x0 ),
                           PixelYUnsnapped( y0 ),
                           PixelXUnsnapped( x1 ),
                           PixelYUnsnapped( y1 ),
                           PixelXUnsnapped( x2 ),
                           PixelYUnsnapped( y2 ),
                           r,
                           g,
                           b,
                           a );
}


void UIDrawContext::Outline( float x, float y, float w, float h, float r, float g, float b, float a ) const
{
    Rect( x, y, w, 1.0f, r, g, b, a );
    Rect( x, y + h - 1.0f, w, 1.0f, r, g, b, a );
    Rect( x, y, 1.0f, h, r, g, b, a );
    Rect( x + w - 1.0f, y, 1.0f, h, r, g, b, a );
}


void UIDrawContext::Text( float x, float y, float pxSize, float r, float g, float b, const char* value ) const
{
    if ( m_drawList )
    {
        m_drawList->AddText( x, y, pxSize, r, g, b, value );
        return;
    }

    const float unitSize = pxSize * m_sy;
    Text2d::Render2dTextColor( PixelX( Snap( x ) ), PixelY( Snap( y ) + pxSize ), unitSize, r, g, b, "%s", value );
}


float UIDrawContext::TextX( float x ) const
{
    return -m_hw + x * m_sx;
}


float UIDrawContext::TextY( float y ) const
{
    return m_hh - y * m_sy;
}


float UIDrawContext::HalfW() const
{
    return m_hw;
}


float UIDrawContext::HalfH() const
{
    return m_hh;
}


float UIDrawContext::ScaleY() const
{
    return m_sy;
}


float UIDrawContext::Snap( float value )
{
    return std::floor( value + 0.5f );
}


float UIDrawContext::PixelXUnsnapped( float x ) const
{
    return TextX( x );
}


float UIDrawContext::PixelYUnsnapped( float y ) const
{
    return TextY( y );
}


float UIDrawContext::PixelX( float x ) const
{
    return TextX( x );
}


float UIDrawContext::PixelY( float y ) const
{
    return TextY( y );
}

} // namespace UI
} // namespace SkullbonezCore
