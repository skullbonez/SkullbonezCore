/*
File: SkullbonezSource/UI/UIDraw.h
Purpose:
  Implements UI Draw widgets, layout, drawing, or UI state for the in-engine controls.

Mental model:
  The UI is immediate-mode-style: each frame reads engine state, computes hit
  boxes, emits draw commands, and returns requests for the run loop to apply.

Glossary:
  Draw command: Lightweight record describing a UI shape or text batch to
  render later in the frame.
  Hit box: Screen-space rectangle used to decide whether mouse input targets a
  widget.

Invariants:
  - Draw geometry and hit testing must be derived from the same layout
  constants.

Related:
  - SkullbonezSource/UI/UIDraw.cpp
  - Agentic/Reference/comment-style-guide.md
*/
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
