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
    bool HitTest( int mouseX, int mouseY ) const;
    float ValueFromMouse( int mouseX, float minValue, float maxValue, float step ) const;
    void Draw( const UIDrawContext& draw, const char* label, const char* valueText, float value, float minValue, float maxValue ) const;

  private:
    float TrackX() const;
    float TrackW() const;

    UIRect m_bounds;
};

} // namespace UI
} // namespace SkullbonezCore
