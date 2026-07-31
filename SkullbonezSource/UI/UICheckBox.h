/*
File: SkullbonezSource/UI/UICheckBox.h
Purpose:
  Implements UI CheckBox widgets, layout, drawing, or UI state for the in-engine controls.

Summary:
  UICheckBox.h implements UI CheckBox widgets, layout, drawing, or UI state

  for the in-engine controls.

Invariants:
  - Draw geometry and hit testing must be derived from the same layout
  constants.

Related:
  - SkullbonezSource/UI/UICheckBox.cpp
  - Agentic/Reference/comment-style-guide.md
  - Agentic/Reference/engine-glossary.md
*/
#pragma once

#include "UIDraw.h"

namespace SkullbonezCore
{
namespace UI
{

class UICheckBox
{
  public:
    void SetBounds( float x, float y, float w, float h );
    UIRect Bounds() const;
    bool HitTest( int mouseX, int mouseY ) const;
    void DrawToggle( const UIDrawContext& draw, const char* label, bool checked, float accentR, float accentG,
                     float accentB ) const;

  private:
    UIRect m_bounds;
};

} // namespace UI
} // namespace SkullbonezCore
