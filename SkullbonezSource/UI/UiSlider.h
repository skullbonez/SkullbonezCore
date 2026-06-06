#pragma once

#include "UiDraw.h"

namespace SkullbonezCore
{
namespace Ui
{

class UiSlider
{
  public:
    void SetBounds( float x, float y, float w, float h );
    bool HitTest( int mouseX, int mouseY ) const;
    float ValueFromMouse( int mouseX, float minValue, float maxValue, float step ) const;
    void Draw( const UiDrawContext& draw, const char* label, const char* valueText, float value, float minValue, float maxValue ) const;

  private:
    float TrackX() const;
    float TrackW() const;

    UiRect m_bounds;
};

} // namespace Ui
} // namespace SkullbonezCore
