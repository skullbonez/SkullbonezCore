/*
File: SkullbonezSource/UI/UISlider.cpp
Purpose:
  Adapts retained slider bounds to stateless value, hit, and draw operations.

Summary:
  UIDrawWidgets is the sole track, quantization, text, style, and draw owner;
  this wrapper keeps only the bounds established by layout.

Invariants:
  - Pointer projection and drawing pass the same bounds value downstream.

Related:
  - SkullbonezSource/UI/UISlider.h
  - SkullbonezSource/UI/UIDrawWidgets.h
  - Agentic/Reference/engine-glossary.md
*/
#include "UISlider.h"

#include "UIDrawWidgets.h"

namespace SkullbonezCore
{
namespace UI
{

void UISlider::SetBounds( float x, float y, float w, float h )
{
    m_bounds = { x, y, w, h };
}


UIRect UISlider::Bounds() const
{
    return m_bounds;
}


bool UISlider::HitTest( int mouseX, int mouseY ) const
{
    constexpr UIVisualState kState = UIVisualState::Visible | UIVisualState::Enabled;
    return Widgets::CanActivateComponent( m_bounds, kState, mouseX, mouseY );
}


float UISlider::ValueFromMouse( int mouseX, float minValue, float maxValue, float step ) const
{
    return Widgets::SliderValueFromPointer( m_bounds, mouseX, minValue, maxValue, step );
}


void UISlider::Draw( const UIDrawContext& draw, const char* label, const char* valueText, float value, float minValue,
                     float maxValue ) const
{
    constexpr UIVisualState kState = UIVisualState::Visible | UIVisualState::Enabled;
    Widgets::DrawSlider( draw, m_bounds, label, valueText, value, minValue, maxValue, kState,
                         Widgets::ComponentAppearance::Established );
}

} // namespace UI
} // namespace SkullbonezCore
