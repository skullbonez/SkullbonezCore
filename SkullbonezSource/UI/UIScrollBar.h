#pragma once

#include "UIDraw.h"

namespace SkullbonezCore
{
namespace UI
{

class UIScrollBar
{
  public:
    void SetBounds( float x, float y, float w, float h );
    void Draw( const UIDrawContext& draw, float contentHeight, float viewportHeight, float scrollY, double visibleUntil, double now ) const;

  private:
    UIRect m_track;
};

} // namespace UI
} // namespace SkullbonezCore
