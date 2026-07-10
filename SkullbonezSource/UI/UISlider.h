/*
File: SkullbonezSource/UI/UISlider.h
Purpose:
  Implements UI Slider widgets, layout, drawing, or UI state for the in-engine controls.

Mental model:
  UISlider.h implements UI Slider widgets, layout, drawing, or UI state for
  the in-engine controls. As a public header, keep edits anchored on UI
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
  - SkullbonezSource/UI/UISlider.cpp
  - Agentic/Reference/comment-style-guide.md
*/
#pragma once

#include "UIDraw.h"

namespace SkullbonezCore
{
namespace UI
{

class UISlider
{
  public:
    void SetBounds( float x, float y, float w, float h );
    UIRect Bounds() const;
    bool HitTest( int mouseX, int mouseY ) const;
    float ValueFromMouse( int mouseX, float minValue, float maxValue, float step ) const;
    void Draw( const UIDrawContext& draw,
               const char* label,
               const char* valueText,
               float value,
               float minValue,
               float maxValue ) const;

  private:
    float TrackX() const;
    float TrackW() const;

    UIRect m_bounds;
};

} // namespace UI
} // namespace SkullbonezCore
