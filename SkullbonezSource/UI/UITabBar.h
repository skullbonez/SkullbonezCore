/*
File: SkullbonezSource/UI/UITabBar.h
Purpose:
  Implements UI TabBar widgets, layout, drawing, or UI state for the in-engine controls.

Summary:
  Maps tab bounds to a selected index and
  draws active labels from the same geometry.

Invariants:
  - Draw geometry and hit testing must be derived from the same layout
  constants.

Related:
  - SkullbonezSource/UI/UITabBar.cpp
  - Agentic/Reference/comment-style-guide.md
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
