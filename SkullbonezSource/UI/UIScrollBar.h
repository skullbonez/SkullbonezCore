/*
File: SkullbonezSource/UI/UIScrollBar.h
Purpose:
  Declares scroll-track/thumb geometry, offset projection, dragging, and
  drawing.

Summary:
  Projects content height and
  scroll offset into one
  clipped viewport track and thumb.

Invariants:
  - Draw geometry and hit testing must be derived from the same layout
  constants.

Related:
  - SkullbonezSource/UI/UIScrollBar.cpp
  - Agentic/Reference/engine-glossary.md
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
    void Draw( const UIDrawContext& draw, float contentHeight, float viewportHeight, float scrollY, double visibleUntil,
               double now ) const;

  private:
    UIRect m_track;
};

} // namespace UI
} // namespace SkullbonezCore
