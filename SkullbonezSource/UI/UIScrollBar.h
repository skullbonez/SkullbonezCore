/*
File: SkullbonezSource/UI/UIScrollBar.h
Purpose:
  Implements UI ScrollBar widgets, layout, drawing, or UI state for the in-engine controls.

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
  - SkullbonezSource/UI/UIScrollBar.cpp
  - Agentic/Reference/comment-style-guide.md
*/
#pragma once

#include "UIDraw.h"

namespace SkullbonezCore
{
namespace UI
{

class UIScrollBar
{
  public:
    void SetBounds( float x, float y, float w, float h );
    UIRect Bounds() const;
    void Draw( const UIDrawContext& draw, float contentHeight, float viewportHeight, float scrollY, double visibleUntil, double now ) const;

  private:
    UIRect m_track;
};

} // namespace UI
} // namespace SkullbonezCore
