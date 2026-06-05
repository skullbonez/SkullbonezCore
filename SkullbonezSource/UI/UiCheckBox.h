#pragma once

#include "UiDraw.h"

namespace SkullbonezCore
{
namespace Ui
{

class UiCheckBox
{
  public:
    void SetBounds( float x, float y, float w, float h );
    bool HitTest( int mouseX, int mouseY ) const;
    void DrawToggle( const UiDrawContext& draw, const char* label, bool checked, float accentR, float accentG, float accentB ) const;

  private:
    UiRect m_bounds;
};

} // namespace Ui
} // namespace SkullbonezCore
