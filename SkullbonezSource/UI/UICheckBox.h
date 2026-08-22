/*
File: SkullbonezSource/UI/UICheckBox.h
Purpose:
  Declares checkbox geometry, hit testing, checked state, and drawing.

Summary:
  Keeps toggle hit testing and
  checked-state drawing on
  one shared bounds rectangle.

Invariants:
  - Draw geometry and hit testing must be derived from the same layout
  constants.

Related:
  - SkullbonezSource/UI/UICheckBox.cpp
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
