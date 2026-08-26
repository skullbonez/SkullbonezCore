/*
File: SkullbonezSource/UI/UILayout.cpp
Purpose:
  Implements deterministic component geometry and interpolation helpers.

Summary:
  Keeps screen placement and animation interpolation reusable while Runtime
  owns scene, Physics, pipeline, and footer product layout.

Invariants:
  - Draw geometry and hit testing must be derived from the same layout
  constants.

Related:
  - SkullbonezSource/UI/UILayout.h
  - SkullbonezSource/Runtime/UI/GameUI/GameUILayout.h
  - Agentic/Reference/engine-glossary.md
*/
#include "UILayout.h"

#include <algorithm>

namespace SkullbonezCore
{
namespace UI
{
namespace Layout
{

UIRect MinimizedRect( int screenW, int screenH, float requestedW )
{
    constexpr float h = 38.0f;
    constexpr float margin = 14.0f;
    const float safeScreenW = static_cast<float>( (std::max)( 1, screenW ) );
    const float safeScreenH = static_cast<float>( (std::max)( 1, screenH ) );
    const float marginX = (std::min)( margin, ( safeScreenW - 1.0f ) * 0.5f );
    const float marginY = (std::min)( margin, ( safeScreenH - 1.0f ) * 0.5f );
    const float maxW = (std::max)( 1.0f, safeScreenW - marginX * 2.0f );
    const float minW = (std::min)( 154.0f, maxW );
    const float boundedH = (std::min)( h, (std::max)( 1.0f, safeScreenH - marginY * 2.0f ) );
    const float w = std::clamp( requestedW, minW, maxW );
    return { marginX, (std::max)( marginY, safeScreenH - boundedH - marginY ), w, boundedH };
}


float SmoothStep( float t )
{
    t = std::clamp( t, 0.0f, 1.0f );
    return t * t * ( 3.0f - 2.0f * t );
}


UIRect LerpRect( const UIRect& from, const UIRect& to, float t )
{
    const float e = SmoothStep( t );
    return { from.x + ( to.x - from.x ) * e, from.y + ( to.y - from.y ) * e, from.w + ( to.w - from.w ) * e,
             from.h + ( to.h - from.h ) * e };
}

} // namespace Layout
} // namespace UI
} // namespace SkullbonezCore
