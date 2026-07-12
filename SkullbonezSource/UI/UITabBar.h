/*
File: SkullbonezSource/UI/UITabBar.h
Purpose:
  Implements UI TabBar widgets, layout, drawing, or UI state for the in-engine controls.

Summary:
  UITabBar.h implements UI TabBar widgets, layout, drawing, or UI state for
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
  - SkullbonezSource/UI/UITabBar.cpp
  - Agentic/Reference/comment-style-guide.md
*/
#pragma once

#include "UIDraw.h"

namespace SkullbonezCore
{
namespace UI
{

class UITabBar
{
  public:
    void SetBounds( float x, float y, float w, float h );
    UIRect Bounds() const;
    int HitTest( int mouseX, int mouseY, int tabCount ) const;
    void Draw( const UIDrawContext& draw, const char* const* labels, int tabCount, int activeIndex ) const;

  private:
    UIRect m_bounds;
};

} // namespace UI
} // namespace SkullbonezCore
