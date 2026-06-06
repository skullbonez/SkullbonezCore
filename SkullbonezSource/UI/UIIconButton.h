#pragma once

#include "UIDraw.h"

namespace SkullbonezCore
{
namespace UI
{

class UIIconButton
{
  public:
    void SetBounds( float x, float y, float w, float h );
    bool HitTest( int mouseX, int mouseY ) const;
    void DrawExpander( const UIDrawContext& draw, bool expanded ) const;

  private:
    UIRect m_bounds;
};

} // namespace UI
} // namespace SkullbonezCore
