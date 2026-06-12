#pragma once

#include "../SkullbonezCommon.h"

namespace SkullbonezCore
{
namespace UI
{

namespace Style
{
struct UIColor;
}

struct UIRect
{
    float x = 0.0f;
    float y = 0.0f;
    float w = 0.0f;
    float h = 0.0f;

    bool Contains( int px, int py ) const;
};

class UIDrawList;

class UIDrawContext
{
  public:
    UIDrawContext( int screenW, int screenH, UIDrawList* drawList = nullptr );

    void Rect( float x, float y, float w, float h, float r, float g, float b, float a ) const;
    void Triangle( float x0, float y0, float x1, float y1, float x2, float y2, float r, float g, float b, float a ) const;
    void Outline( float x, float y, float w, float h, float r, float g, float b, float a ) const;
    void RoundedRect( float x, float y, float w, float h, float radius, float r, float g, float b, float a ) const;
    void RoundedPanel( const UIRect& bounds, float radius, const Style::UIColor& fill, const Style::UIColor& border ) const;
    void Text( float x, float y, float pxSize, float r, float g, float b, const char* value ) const;
    float TextX( float x ) const;
    float TextY( float y ) const;

    float HalfW() const;
    float HalfH() const;
    float ScaleY() const;

  private:
    static float Snap( float value );
    float PixelXUnsnapped( float x ) const;
    float PixelYUnsnapped( float y ) const;
    float PixelX( float x ) const;
    float PixelY( float y ) const;

    float m_hw = 1.0f;
    float m_hh = 1.0f;
    float m_sx = 1.0f;
    float m_sy = 1.0f;
    UIDrawList* m_drawList = nullptr;
};

} // namespace UI
} // namespace SkullbonezCore
