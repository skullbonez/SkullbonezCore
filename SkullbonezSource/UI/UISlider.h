/*
File: SkullbonezSource/UI/UISlider.h
Purpose:
  Declares a retained-bounds adapter for stateless slider value and draw
  operations.

Summary:
  The wrapper preserves source-compatible layout calls while UIDrawWidgets owns
  track geometry, quantization, text measurement, style, and commands.

Invariants:
  - Value projection and drawing consume the same retained bounds.

Related:
  - SkullbonezSource/UI/UISlider.cpp
  - SkullbonezSource/UI/UIDrawWidgets.h
  - Agentic/Reference/engine-glossary.md
*/
#pragma once

#include "UIDraw.h"

namespace SkullbonezCore
{
namespace UI
{

class UISlider
{
  public:
    void SetBounds( float x, float y, float w, float h );
    UIRect Bounds() const;
    bool HitTest( int mouseX, int mouseY ) const;
    float ValueFromMouse( int mouseX, float minValue, float maxValue, float step ) const;
    void Draw( const UIDrawContext& draw, const char* label, const char* valueText, float value, float minValue,
               float maxValue ) const;

  private:
    UIRect m_bounds;
};

} // namespace UI
} // namespace SkullbonezCore
