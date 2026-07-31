/*
File: SkullbonezSource/UI/UIButton.h
Purpose:
  Implements UI Button widgets, layout, drawing, or UI state for the in-engine controls.

Summary:
  UIButton.h implements UI Button widgets, layout, drawing, or UI state for
  the in-engine controls.

Invariants:
  - Draw geometry and hit testing must be derived from the same layout
  constants.

Related:
  - SkullbonezSource/UI/UIButton.cpp
  - Agentic/Reference/comment-style-guide.md
  - Agentic/Reference/engine-glossary.md
*/
#pragma once

#include "UIDraw.h"

namespace SkullbonezCore
{
namespace UI
{

class UIButton
{
  public:
    void SetBounds( float x, float y, float w, float h );
    UIRect Bounds() const;
    bool HitTest( int mouseX, int mouseY ) const;
    void Draw( const UIDrawContext& draw, const char* label, int mouseX, int mouseY ) const;

  private:
    UIRect m_bounds;
};

} // namespace UI
} // namespace SkullbonezCore
