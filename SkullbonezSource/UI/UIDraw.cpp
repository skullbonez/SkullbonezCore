/*
File: SkullbonezSource/UI/UIDraw.cpp
Purpose:
  Implements renderer-free GameUI draw-command recording.

Summary:
  Shapes, clips, and text remain in screen pixels and append to UIDrawList in caller
  order. The coordinate helpers expose the same 45-degree projection math for
  passive layout calculations without borrowing TextBatch or a backend owner.

Invariants:
  - Draw geometry and hit testing must be derived from the same layout
    constants.
  - Recording never flushes, binds, uploads, allocates GPU storage, or measures
    GPU time.

Related:
  - SkullbonezSource/UI/UIDraw.h
  - Agentic/Reference/engine-glossary.md
*/
#include "UIDraw.h"

#include "UIDrawList.h"
#include "UIStyle.h"

#include <algorithm>
#include <cmath>

namespace SkullbonezCore
{
namespace UI
{

bool UIRect::Contains( int px, int py ) const
{
    return static_cast<float>( px ) >= x && static_cast<float>( px ) <= x + w && static_cast<float>( py ) >= y &&
           static_cast<float>( py ) <= y + h;
}

UIRect IntersectRect( const UIRect& leftRect, const UIRect& rightRect )
{
    const float left = (std::max)( leftRect.x, rightRect.x );
    const float top = (std::max)( leftRect.y, rightRect.y );
    const float right = (std::min)( leftRect.x + leftRect.w, rightRect.x + rightRect.w );
    const float bottom = (std::min)( leftRect.y + leftRect.h, rightRect.y + rightRect.h );

    if ( right <= left || bottom <= top )
    {
        return {};
    }

    return { left, top, right - left, bottom - top };
}


UIDrawContext::UIDrawContext( int screenW, int screenH, UIDrawList& drawList )
{
    screenW = (std::max)( 1, screenW );
    screenH = (std::max)( 1, screenH );
    m_hh = std::tan( 22.5f * 3.14159265358979323846f / 180.0f );
    m_hw = m_hh * static_cast<float>( screenW ) / static_cast<float>( screenH );
    m_sx = ( m_hw * 2.0f ) / static_cast<float>( screenW );
    m_sy = ( m_hh * 2.0f ) / static_cast<float>( screenH );
    m_drawList = &drawList;
}


void UIDrawContext::Rect( float x, float y, float w, float h, float r, float g, float b, float a ) const
{
    m_drawList->AddRect( x, y, w, h, r, g, b, a );
}


void UIDrawContext::Triangle( float x0, float y0, float x1, float y1, float x2, float y2, float r, float g, float b,
                              float a ) const
{
    m_drawList->AddTriangle( x0, y0, x1, y1, x2, y2, r, g, b, a );
}


void UIDrawContext::Outline( float x, float y, float w, float h, float r, float g, float b, float a ) const
{
    Rect( x, y, w, 1.0f, r, g, b, a );
    Rect( x, y + h - 1.0f, w, 1.0f, r, g, b, a );
    Rect( x, y, 1.0f, h, r, g, b, a );
    Rect( x + w - 1.0f, y, 1.0f, h, r, g, b, a );
}


void UIDrawContext::RoundedRect( float x, float y, float w, float h, float radius, float r, float g, float b, float a ) const
{
    m_drawList->AddRoundedRect( x, y, w, h, radius, r, g, b, a );
}


void UIDrawContext::RoundedPanel( const UIRect& bounds, float radius, const Style::UIColor& fill,
                                  const Style::UIColor& border ) const
{
    RoundedRect( bounds.x, bounds.y, bounds.w, bounds.h, radius, border.r, border.g, border.b, border.a );
    const float inset = border.a > 0.0f ? 1.0f : 0.0f;
    RoundedRect( bounds.x + inset, bounds.y + inset, (std::max)( 0.0f, bounds.w - inset * 2.0f ),
                 (std::max)( 0.0f, bounds.h - inset * 2.0f ), (std::max)( 0.0f, radius - inset ), fill.r, fill.g, fill.b,
                 fill.a );
}


void UIDrawContext::PushClip( const UIRect& bounds ) const
{
    m_drawList->PushClip( bounds.x, bounds.y, bounds.w, bounds.h );
}


void UIDrawContext::PopClip() const
{
    m_drawList->PopClip();
}


void UIDrawContext::Text( float x, float y, float pxSize, float r, float g, float b, const char* value ) const
{
    m_drawList->AddText( x, y, pxSize, r, g, b, value );
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

} // namespace UI
} // namespace SkullbonezCore
