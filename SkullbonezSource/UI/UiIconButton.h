#pragma once

#include "UiDraw.h"

namespace SkullbonezCore
{
namespace Ui
{

class UiIconButton
{
  public:
    void SetBounds( float x, float y, float w, float h );
    bool HitTest( int mouseX, int mouseY ) const;
    void DrawExpander( const UiDrawContext& draw, bool expanded ) const;

  private:
    UiRect m_bounds;
};

} // namespace Ui
} // namespace SkullbonezCore
