/*
File: SkullbonezSource/UI/UIDraw.h
Purpose:
  Implements UI Draw widgets, layout, drawing, or UI state for the in-engine controls.

Summary:
  UIDraw.h implements UI Draw widgets, layout, drawing, or UI state for the
  in-engine controls. As a public header, keep edits anchored on UI request,
  layout, hit-test, and draw-command flow and on the glossary/invariants
  below.

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

#include "../Core/Common.h"

namespace SkullbonezCore
{
namespace Rendering
{
class Dx12GeometryOwner;
class Dx12TextureOwner;
} // namespace Rendering

namespace Text
{
class TextBatch;
} // namespace Text

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
    UIDrawContext(
        int screenW,
        int screenH,
        UIDrawList* drawList = nullptr,
        Rendering::Dx12TextureOwner* renderTextures = nullptr,
        Rendering::Dx12GeometryOwner* renderCommands = nullptr,
        Text::TextBatch* textBatch = nullptr
    );

    void Rect( float x, float y, float w, float h, float r, float g, float b, float a ) const;
    void
    Triangle( float x0, float y0, float x1, float y1, float x2, float y2, float r, float g, float b, float a ) const;
    void Outline( float x, float y, float w, float h, float r, float g, float b, float a ) const;
    void RoundedRect( float x, float y, float w, float h, float radius, float r, float g, float b, float a ) const;
    void
    RoundedPanel( const UIRect& bounds, float radius, const Style::UIColor& fill, const Style::UIColor& border ) const;
    void Text( float x, float y, float pxSize, float r, float g, float b, const char* value ) const;
    float TextX( float x ) const;
    float TextY( float y ) const;

    float HalfW() const;
    float HalfH() const;
    float ScaleY() const;
    void FlushQuads() const;
    void FlushText() const;

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
    Rendering::Dx12TextureOwner* m_renderTextures = nullptr;
    // Lifetime: immediate contexts borrow commands for this draw replay only.
    // Recording contexts keep this null because they enqueue CPU draw commands.
    Rendering::Dx12GeometryOwner* m_renderCommands = nullptr;
    // Lifetime: both recording and immediate contexts borrow the owning
    // RuntimeRenderer batch so pixel/frustum conversion stays owner-specific.
    Text::TextBatch* m_textBatch = nullptr;
};

} // namespace UI
} // namespace SkullbonezCore
