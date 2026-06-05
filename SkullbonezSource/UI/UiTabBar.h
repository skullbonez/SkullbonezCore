#pragma once

#include "UiDraw.h"

namespace SkullbonezCore
{
namespace Ui
{

class UiTabBar
{
  public:
    void SetBounds( float x, float y, float w, float h );
    int HitTest( int mouseX, int mouseY, int tabCount ) const;
    void Draw( const UiDrawContext& draw, const char* const* labels, int tabCount, int activeIndex ) const;

  private:
    UiRect m_bounds;
};

} // namespace Ui
} // namespace SkullbonezCore
