/*
File: SkullbonezSource/UI/UITabBar.h
Purpose:
  Declares a retained-strip adapter for stateless tab geometry, hit testing,
  and drawing.

Summary:
  The wrapper retains only strip bounds. Labels, selection, and interaction
  results remain caller values resolved through UIDrawWidgets.

Invariants:
  - Selection and pointer state are never retained by the tab strip.

Related:
  - SkullbonezSource/UI/UITabBar.cpp
  - SkullbonezSource/UI/UIDrawWidgets.h
  - Agentic/Reference/engine-glossary.md
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
