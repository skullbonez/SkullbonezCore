/*
File: SkullbonezSource/UI/UICheckBox.h
Purpose:
  Declares a retained-bounds adapter for the stateless toggle contract.

Summary:
  The wrapper retains layout bounds only. Checked state and accent color remain
  caller values consumed by UIDrawWidgets for each frame.

Invariants:
  - The wrapper never retains checked, hover, command, or input state.

Related:
  - SkullbonezSource/UI/UICheckBox.cpp
  - SkullbonezSource/UI/UIDrawWidgets.h
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
