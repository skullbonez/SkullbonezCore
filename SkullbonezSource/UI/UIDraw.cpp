/*
File: SkullbonezSource/UI/UIDraw.cpp
Purpose:
  Implements UI Draw widgets, layout, drawing, or UI state for the in-engine controls.

Mental model:
  UIDraw.cpp implements UI Draw widgets, layout, drawing, or UI state for the
  in-engine controls. As an implementation unit, keep edits anchored on UI
  request, layout, hit-test, and draw-command flow and on the
  glossary/invariants below.

Glossary:
  Draw command: Lightweight record describing a UI shape or text batch to
  render later in the frame.
  Hit box: Screen-space rectangle used to decide whether mouse input targets a
  widget.

Invariants:
  - Draw geometry and hit testing must be derived from the same layout
  constants.

Related:
  - SkullbonezSource/UI/UIDraw.h
  - Agentic/Reference/comment-style-guide.md
*/
#include "UIDraw.h"

#include "../Rendering/Text.h"
#include "UIDrawList.h"
#include "UIStyle.h"

#include <algorithm>
#include <cmath>

using namespace SkullbonezCore::Text;

namespace SkullbonezCore
{
namespace UI
{

bool UIRect::Contains( int px, int py ) const
{
    return static_cast<float>( px ) >= x && static_cast<float>( px ) <= x + w && static_cast<float>( py ) >= y &&
           static_cast<float>( py ) <= y + h;
}


UIDrawContext::UIDrawContext( int screenW,
                              int screenH,
                              UIDrawList* drawList,
                              Rendering::IRenderCommandContext* renderCommands )
{
    screenW = (std::max)( 1, screenW );
    screenH = (std::max)( 1, screenH );
    m_hw = Text2d::HalfW();
    m_hh = Text2d::HalfH();
    m_sx = ( m_hw * 2.0f ) / static_cast<float>( screenW );
    m_sy = ( m_hh * 2.0f ) / static_cast<float>( screenH );
    m_drawList = drawList;
    m_renderCommands = renderCommands;
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
    assert( m_renderCommands && "UIDrawContext immediate Rect requires render commands" );
    Text2d::BatchQuad( *m_renderCommands, PixelX( x0 ), PixelY( y1 ), PixelX( x1 ), PixelY( y0 ), r, g, b, a );
}


void UIDrawContext::Triangle( float x0,
                              float y0,
                              float x1,
                              float y1,
                              float x2,
                              float y2,
                              float r,
                              float g,
                              float b,
                              float a ) const
{
    if ( m_drawList )
    {
        m_drawList->AddTriangle( x0, y0, x1, y1, x2, y2, r, g, b, a );
        return;
    }

    assert( m_renderCommands && "UIDrawContext immediate Triangle requires render commands" );
    Text2d::BatchTriangle( *m_renderCommands,
                           PixelXUnsnapped( x0 ),
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


void DrawRoundedSpan( const UIDrawContext& draw, float left, float y, float right, float r, float g, float b, float a )
{
    if ( right <= left || a <= 0.0f )
    {
        return;
    }

    const float fullLeft = std::ceil( left );
    const float fullRight = std::floor( right );
    const float leftCoverage = std::clamp( fullLeft - left, 0.0f, 1.0f );
    const float rightCoverage = std::clamp( right - fullRight, 0.0f, 1.0f );

    if ( leftCoverage > 0.01f )
    {
        draw.Rect( fullLeft - 1.0f, y, 1.0f, 1.0f, r, g, b, a * leftCoverage );
    }
    if ( fullRight > fullLeft )
    {
        draw.Rect( fullLeft, y, fullRight - fullLeft, 1.0f, r, g, b, a );
    }
    if ( rightCoverage > 0.01f )
    {
        draw.Rect( fullRight, y, 1.0f, 1.0f, r, g, b, a * rightCoverage );
    }
}


void DrawRoundedRectFill( const UIDrawContext& draw,
                          float x,
                          float y,
                          float w,
                          float h,
                          float radius,
                          float r,
                          float g,
                          float b,
                          float a )
{
    if ( w <= 0.0f || h <= 0.0f || a <= 0.0f )
    {
        return;
    }

    const float clampedRadius = std::clamp( radius, 0.0f, (std::min)( w, h ) * 0.5f );
    if ( clampedRadius <= 0.5f )
    {
        draw.Rect( x, y, w, h, r, g, b, a );
        return;
    }

    const int capRows = (std::max)( 1, static_cast<int>( std::ceil( clampedRadius ) ) );
    const float middleY = y + static_cast<float>( capRows );
    const float middleH = h - static_cast<float>( capRows * 2 );
    if ( middleH > 0.0f )
    {
        draw.Rect( x, middleY, w, middleH, r, g, b, a );
    }

    const float radiusSq = clampedRadius * clampedRadius;
    for ( int row = 0; row < capRows; ++row )
    {
        const float sample = (std::min)( static_cast<float>( row ) + 0.5f, clampedRadius );
        const float dy = clampedRadius - sample;
        const float xInset = clampedRadius - std::sqrt( (std::max)( 0.0f, radiusSq - dy * dy ) );
        const float left = x + xInset;
        const float right = x + w - xInset;
        DrawRoundedSpan( draw, left, y + static_cast<float>( row ), right, r, g, b, a );
        DrawRoundedSpan( draw, left, y + h - static_cast<float>( row ) - 1.0f, right, r, g, b, a );
    }
}


void UIDrawContext::RoundedRect( float x, float y, float w, float h, float radius, float r, float g, float b, float a )
    const
{
    if ( m_drawList )
    {
        m_drawList->AddRoundedRect( x, y, w, h, radius, r, g, b, a );
        return;
    }

    if ( radius > 1.0f && w > 4.0f && h > 4.0f && a > 0.05f )
    {
        DrawRoundedRectFill( *this, x - 0.5f, y - 0.5f, w + 1.0f, h + 1.0f, radius + 0.5f, r, g, b, a * 0.30f );
    }
    DrawRoundedRectFill( *this, x, y, w, h, radius, r, g, b, a );
}


void UIDrawContext::RoundedPanel( const UIRect& bounds,
                                  float radius,
                                  const Style::UIColor& fill,
                                  const Style::UIColor& border ) const
{
    RoundedRect( bounds.x, bounds.y, bounds.w, bounds.h, radius, border.r, border.g, border.b, border.a );
    const float inset = border.a > 0.0f ? 1.0f : 0.0f;
    RoundedRect( bounds.x + inset,
                 bounds.y + inset,
                 (std::max)( 0.0f, bounds.w - inset * 2.0f ),
                 (std::max)( 0.0f, bounds.h - inset * 2.0f ),
                 (std::max)( 0.0f, radius - inset ),
                 fill.r,
                 fill.g,
                 fill.b,
                 fill.a );
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


void UIDrawContext::FlushQuads() const
{
    if ( m_drawList )
    {
        return;
    }

    assert( m_renderCommands && "UIDrawContext immediate FlushQuads requires render commands" );
    Text2d::FlushQuads( *m_renderCommands );
}


void UIDrawContext::FlushText() const
{
    if ( m_drawList )
    {
        return;
    }

    assert( m_renderCommands && "UIDrawContext immediate FlushText requires render commands" );
    Text2d::FlushText( *m_renderCommands );
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
