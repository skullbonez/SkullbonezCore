#pragma once

#include "../SkullbonezCommon.h"

namespace SkullbonezCore
{
namespace UI
{

struct UIRect
{
    float x = 0.0f;
    float y = 0.0f;
    float w = 0.0f;
    float h = 0.0f;

    bool Contains( int px, int py ) const;
};

class UIDrawContext
{
  public:
    UIDrawContext( int screenW, int screenH );

    void Rect( float x, float y, float w, float h, float r, float g, float b, float a ) const;
    void Outline( float x, float y, float w, float h, float r, float g, float b, float a ) const;
    void Text( float x, float y, float pxSize, float r, float g, float b, const char* value ) const;
    float TextX( float x ) const;
    float TextY( float y ) const;

    float HalfW() const;
    float HalfH() const;
    float ScaleY() const;

  private:
    static float Snap( float value );
    float PixelX( float x ) const;
    float PixelY( float y ) const;

    float m_hw = 1.0f;
    float m_hh = 1.0f;
    float m_sx = 1.0f;
    float m_sy = 1.0f;
};

} // namespace UI
} // namespace SkullbonezCore
