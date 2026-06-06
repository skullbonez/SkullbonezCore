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
    bool HitTest( int mouseX, int mouseY ) const;
    void DrawToggle( const UIDrawContext& draw, const char* label, bool checked, float accentR, float accentG, float accentB ) const;

  private:
    UIRect m_bounds;
};

} // namespace UI
} // namespace SkullbonezCore
