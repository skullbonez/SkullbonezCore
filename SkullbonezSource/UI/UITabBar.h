#pragma once

#include "UIDraw.h"

namespace SkullbonezCore
{
namespace UI
{

class UITabBar
{
  public:
    void SetBounds( float x, float y, float w, float h );
    UIRect Bounds() const;
    int HitTest( int mouseX, int mouseY, int tabCount ) const;
    void Draw( const UIDrawContext& draw, const char* const* labels, int tabCount, int activeIndex ) const;

  private:
    UIRect m_bounds;
};

} // namespace UI
} // namespace SkullbonezCore
