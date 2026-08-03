/*
File: SkullbonezSource/UI/UISlider.h
Purpose:
  Implements UI Slider widgets, layout, drawing, or UI state for the in-engine controls.

Summary:
  Maps mouse positions to quantized values and
  renders the same track used for interaction.

Invariants:
  - Draw geometry and hit testing must be derived from the same layout
  constants.

Related:
  - SkullbonezSource/UI/UISlider.cpp
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
    float TrackX() const;
    float TrackW() const;

    UIRect m_bounds;
};

} // namespace UI
} // namespace SkullbonezCore
