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
    const float maxW = (std::max)( 154.0f, static_cast<float>( screenW ) - margin * 2.0f );
    const float w = std::clamp( requestedW, 154.0f, maxW );
    return { margin, (std::max)( margin, static_cast<float>( screenH ) - h - margin ), w, h };
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
