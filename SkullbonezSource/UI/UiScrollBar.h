#pragma once

#include "UiDraw.h"

namespace SkullbonezCore
{
namespace Ui
{

class UiScrollBar
{
  public:
    void SetBounds( float x, float y, float w, float h );
    void Draw( const UiDrawContext& draw, float contentHeight, float viewportHeight, float scrollY, double visibleUntil, double now ) const;

  private:
    UiRect m_track;
};

} // namespace Ui
} // namespace SkullbonezCore
