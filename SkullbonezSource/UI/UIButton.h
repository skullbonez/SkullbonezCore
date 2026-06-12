#pragma once

#include "UIDraw.h"

namespace SkullbonezCore
{
namespace UI
{

class UIButton
{
  public:
    void SetBounds( float x, float y, float w, float h );
    UIRect Bounds() const;
    bool HitTest( int mouseX, int mouseY ) const;
    void Draw( const UIDrawContext& draw, const char* label, int mouseX, int mouseY ) const;

  private:
    UIRect m_bounds;
};

} // namespace UI
} // namespace SkullbonezCore
