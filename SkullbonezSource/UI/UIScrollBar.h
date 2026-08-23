/*
File: SkullbonezSource/UI/UIScrollBar.h
Purpose:
  Declares a retained-track adapter for stateless scrollbar projection and
  drawing.

Summary:
  The wrapper retains only its track bounds. Content, viewport, offset, and
  visibility are caller values projected by UIDrawWidgets each frame.

Invariants:
  - No drag, pointer, visibility, or scroll offset state is retained here.

Related:
  - SkullbonezSource/UI/UIScrollBar.cpp
  - SkullbonezSource/UI/UIDrawWidgets.h
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
