/*
File: SkullbonezSource/UI/UIDraw.h
Purpose:
  Declares the renderer-free screen-space recorder used by GameUI widgets.

Summary:
  UIDrawContext turns widget shapes and labels into one bounded UIDrawList.
  Projection and backend submission happen after UI construction, so tests can
  build complete streams without a renderer device.

Invariants:
  - Draw geometry and hit testing must be derived from the same layout
    constants.
  - UIDrawContext retains only its destination list; backend owners, handles,
    callbacks, and opaque renderer tokens are forbidden.
  - Commands remain in call order and use screen pixels.

Related:
  - SkullbonezSource/UI/UIDraw.cpp
  - Agentic/Reference/engine-glossary.md
*/
#pragma once

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
    // The dimensions identify the complete frame being authored. Geometry
    // remains in screen pixels, so recording does not need projection or
    // renderer state.
    UIDrawContext( int screenW, int screenH, UIDrawList& drawList );

    void Rect( float x, float y, float w, float h, float r, float g, float b, float a ) const;
    void Triangle( float x0, float y0, float x1, float y1, float x2, float y2, float r, float g, float b, float a ) const;
    void Outline( float x, float y, float w, float h, float r, float g, float b, float a ) const;
    void RoundedRect( float x, float y, float w, float h, float radius, float r, float g, float b, float a ) const;
    void RoundedPanel( const UIRect& bounds, float radius, const Style::UIColor& fill, const Style::UIColor& border ) const;

    // Records a clip boundary in the same screen-space geometry vocabulary.
    // Callers must balance every accepted push within their draw operation.
    void PushClip( const UIRect& bounds ) const;
    void PopClip() const;
    void Text( float x, float y, float pxSize, float r, float g, float b, const char* value ) const;
    float TextX( float x ) const;
    float TextY( float y ) const;
    float HalfW() const;
    float HalfH() const;

  private:
    float m_hw = 1.0f;
    float m_hh = 1.0f;
    float m_sx = 1.0f;
    float m_sy = 1.0f;
    UIDrawList* m_drawList = nullptr;
};

// Concept: interaction owners resolve component state before presentation.
//
// The flags are deliberately component-neutral. A caller may combine them to
// describe one disposable frame without giving UI access to a pointer device,
// domain action, retained owner, or renderer resource.
enum class UIVisualState : unsigned char
{
    None = 0,
    Visible = 1 << 0,
    Enabled = 1 << 1,
    Hovered = 1 << 2,
    Focused = 1 << 3,
    Active = 1 << 4,
    Selected = 1 << 5,
    Checked = 1 << 6
};

constexpr UIVisualState operator|( UIVisualState left, UIVisualState right )
{
    return static_cast<UIVisualState>( static_cast<unsigned char>( left ) | static_cast<unsigned char>( right ) );
}

constexpr UIVisualState& operator|=( UIVisualState& left, UIVisualState right )
{
    left = left | right;
    return left;
}

constexpr bool HasVisualState( UIVisualState states, UIVisualState state )
{
    return ( static_cast<unsigned char>( states ) & static_cast<unsigned char>( state ) ) != 0;
}

} // namespace UI
} // namespace SkullbonezCore
