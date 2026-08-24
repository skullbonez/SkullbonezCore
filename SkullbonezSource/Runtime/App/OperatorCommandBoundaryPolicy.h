/*
File: SkullbonezSource/Runtime/App/OperatorCommandBoundaryPolicy.h
Purpose:
  Applies shared operator-control ranges at the App command boundary.

Summary:
  Stateless clamp functions translate detached operator command values into
  the exact intervals accepted by Runtime owners. Both production application
  and focused tests consume these functions, so endpoint or gravity-sign drift
  cannot hide behind duplicated literals.

Invariants:
  - Every endpoint comes from OperatorControlPolicy; App owns no duplicate range.
  - World gravity is a signed Y acceleration while the operator range is a
    positive strength, so its ascending interval is negative max through
    negative min.

Related:
  - SkullbonezSource/Runtime/App/OperatorCommandApplication.cpp
  - SkullbonezSource/Runtime/Interaction/OperatorEditorExchange.h
  - SkullbonezTests/TestOperatorCommandTransaction.cpp
*/
#pragma once

#include "../Interaction/OperatorEditorExchange.h"

#include <algorithm>

namespace SkullbonezCore::Runtime::OperatorCommandBoundaryPolicy
{
inline constexpr float ClampTimeScale( float value )
{
    return std::clamp( value, UI::OperatorControlPolicy::UI_TIME_SCALE_MIN,
                       UI::OperatorControlPolicy::UI_TIME_SCALE_MAX );
}

inline constexpr int ClampSeed( int value )
{
    return std::clamp( value, UI::OperatorControlPolicy::UI_SEED_MIN, UI::OperatorControlPolicy::UI_SEED_MAX );
}

inline constexpr float ClampWorldGravity( float gravity )
{
    return std::clamp( gravity, -UI::OperatorControlPolicy::UI_WORLD_GRAVITY_MAX,
                       -UI::OperatorControlPolicy::UI_WORLD_GRAVITY_MIN );
}

inline constexpr float ClampWorldFluidHeight( float height )
{
    return std::clamp( height, UI::OperatorControlPolicy::UI_WORLD_FLUID_HEIGHT_MIN,
                       UI::OperatorControlPolicy::UI_WORLD_FLUID_HEIGHT_MAX );
}

inline constexpr float ClampWorldFluidDensity( float density )
{
    return std::clamp( density, UI::OperatorControlPolicy::UI_WORLD_FLUID_DENSITY_MIN,
                       UI::OperatorControlPolicy::UI_WORLD_FLUID_DENSITY_MAX );
}
} // namespace SkullbonezCore::Runtime::OperatorCommandBoundaryPolicy
