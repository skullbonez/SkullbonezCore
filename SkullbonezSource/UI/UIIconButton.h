/*
File: SkullbonezSource/UI/UIIconButton.h
Purpose:
  Implements UI IconButton widgets, layout, drawing, or UI state for the in-engine controls.

Summary:
  Keeps expander hit testing and chevron
  drawing on one shared bounds rectangle.

Invariants:
  - Draw geometry and hit testing must be derived from the same layout
  constants.

Related:
  - SkullbonezSource/UI/UIIconButton.cpp
  - Agentic/Reference/engine-glossary.md
*/
#pragma once

#include "UIDraw.h"

namespace SkullbonezCore
{
namespace UI
{

class UIIconButton
{
  public:
    void SetBounds( float x, float y, float w, float h );
    bool HitTest( int mouseX, int mouseY ) const;
    void DrawExpander( const UIDrawContext& draw, bool expanded ) const;

  private:
    UIRect m_bounds;
};

} // namespace UI
} // namespace SkullbonezCore
