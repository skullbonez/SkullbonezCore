/*
File: SkullbonezSource/UI/UILayout.h
Purpose:
  Declares deterministic component geometry and interpolation helpers.

Summary:
  Keeps screen placement and animation interpolation reusable while Runtime
  owns scene, Physics, pipeline, and footer product layout.

Invariants:
  - Draw geometry and hit testing must be derived from the same layout
  constants.

Related:
  - SkullbonezSource/UI/UILayout.cpp
  - SkullbonezSource/Runtime/UI/GameUI/GameUILayout.h
  - Agentic/Reference/engine-glossary.md
*/
#pragma once

#include "UIDraw.h"

namespace SkullbonezCore
{
namespace UI
{
namespace Layout
{

constexpr double UI_WINDOW_ANIMATION_SECONDS = 0.18;

UIRect MinimizedRect( int screenW, int screenH, float requestedW );

float SmoothStep( float t );
UIRect LerpRect( const UIRect& from, const UIRect& to, float t );

} // namespace Layout
} // namespace UI
} // namespace SkullbonezCore
