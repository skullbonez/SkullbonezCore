#pragma once

#include "UiDraw.h"

namespace SkullbonezCore
{
namespace Ui
{

class UiButton
{
  public:
    void SetBounds( float x, float y, float w, float h );
    bool HitTest( int mouseX, int mouseY ) const;
    void Draw( const UiDrawContext& draw, const char* label, int mouseX, int mouseY ) const;

  private:
    UiRect m_bounds;
};

} // namespace Ui
} // namespace SkullbonezCore
