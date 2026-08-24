/*
File: SkullbonezSource/Runtime/App/OperatorCommandBoundaryPolicy.h
Purpose:
  Applies shared operator-control ranges at the App command boundary.

Summary:
  Stateless normalization translates detached operator command values onto the
  exact OperatorControlPolicy and render/cinematic catalog grids. InputFrame
  applies the covered whole-packet pass once before dispatch; App-owned phase
  application reuses scalar helpers where it directly mutates configuration.

Invariants:
  - Domain endpoints come from OperatorControlPolicy and render endpoints come
    from UIRenderAuthoringCatalog; App owns no duplicate range.
  - Snapping is anchored at each canonical minimum and chooses the nearest step
    with halfway values resolving toward the maximum.
  - World gravity is a signed Y acceleration while the operator range is a
    positive strength, so its ascending interval is negative max through
    negative min.
  - Replay reveal and horizon commands are outside this policy and retain their
    Prediction and ReplayOverlay/Capture semantics.

Related:
  - SkullbonezSource/Runtime/App/OperatorCommandApplication.cpp
  - SkullbonezSource/Runtime/Interaction/OperatorEditorExchange.h
  - SkullbonezSource/Runtime/Render/UIRenderAuthoringCatalog.h
  - SkullbonezTests/TestOperatorCommandTransaction.cpp
*/
#pragma once

#include "../Interaction/OperatorEditorExchange.h"
#include "../Render/UIRenderAuthoringCatalog.h"

#include <algorithm>
#include <cmath>

namespace SkullbonezCore::Runtime::OperatorCommandBoundaryPolicy
{
inline float NormalizeFloat( float value, float minValue, float maxValue, float step ) noexcept
{
    if ( value <= minValue )
    {
        return minValue;
    }

    if ( value >= maxValue )
    {
        return maxValue;
    }

    if ( step <= 0.0f )
    {
        return value;
    }

    // Invariant: double arithmetic makes the grid index stable even when a
    // decimal policy step has no exact binary-float representation.
    const double offset = static_cast<double>( value ) - static_cast<double>( minValue );
    const double stepCount = std::floor( offset / static_cast<double>( step ) + 0.5 );
    const float snapped = static_cast<float>( static_cast<double>( minValue ) + stepCount * static_cast<double>( step ) );
    return std::clamp( snapped, minValue, maxValue );
}

inline float NormalizeTimeScale( float value ) noexcept
{
    return NormalizeFloat( value, UI::OperatorControlPolicy::UI_TIME_SCALE_MIN, UI::OperatorControlPolicy::UI_TIME_SCALE_MAX,
                           UI::OperatorControlPolicy::UI_TIME_SCALE_STEP );
}

inline constexpr int ClampSeed( int value )
{
    return std::clamp( value, UI::OperatorControlPolicy::UI_SEED_MIN, UI::OperatorControlPolicy::UI_SEED_MAX );
}

inline float NormalizeWorldGravity( float gravity ) noexcept
{
    return NormalizeFloat( gravity, -UI::OperatorControlPolicy::UI_WORLD_GRAVITY_MAX,
                           -UI::OperatorControlPolicy::UI_WORLD_GRAVITY_MIN,
                           UI::OperatorControlPolicy::UI_WORLD_GRAVITY_STEP );
}

inline float NormalizeWorldFluidHeight( float height ) noexcept
{
    return NormalizeFloat( height, UI::OperatorControlPolicy::UI_WORLD_FLUID_HEIGHT_MIN,
                           UI::OperatorControlPolicy::UI_WORLD_FLUID_HEIGHT_MAX,
                           UI::OperatorControlPolicy::UI_WORLD_FLUID_HEIGHT_STEP );
}

inline float NormalizeWorldFluidDensity( float density ) noexcept
{
    return NormalizeFloat( density, UI::OperatorControlPolicy::UI_WORLD_FLUID_DENSITY_MIN,
                           UI::OperatorControlPolicy::UI_WORLD_FLUID_DENSITY_MAX,
                           UI::OperatorControlPolicy::UI_WORLD_FLUID_DENSITY_STEP );
}

inline float NormalizeOrdinaryRenderParameter( UI::UIRenderParam param, float value ) noexcept
{
    const int index = static_cast<int>( param );

    if ( index < 0 || index >= static_cast<int>( UI::UIRenderParam::Count ) )
    {
        return value;
    }

    const UI::RenderSliderSpec& policy = UI::kRenderSliderSpecs[index];
    return NormalizeFloat( value, policy.minValue, policy.maxValue, policy.step );
}

inline float NormalizeCinematicParameter( UI::UICinematicParam param, float value ) noexcept
{
    const int index = static_cast<int>( param );

    if ( index < 0 || index >= static_cast<int>( UI::UICinematicParam::Count ) )
    {
        return value;
    }

    const UI::CinematicSliderSpec& policy = UI::kCinematicSliderSpecs[index];
    return NormalizeFloat( value, policy.minValue, policy.maxValue, policy.step );
}

inline void NormalizeOperatorCommands( UI::InGameUICommands& commands ) noexcept
{
    using namespace UI::OperatorControlPolicy;

    const auto normalizeRequested = []( bool requested, float& value, float minValue, float maxValue, float step )
    {
        if ( requested )
        {
            value = NormalizeFloat( value, minValue, maxValue, step );
        }
    };

    normalizeRequested( commands.sceneOptions.requestedTimeScale > 0.0f, commands.sceneOptions.requestedTimeScale,
                        UI_TIME_SCALE_MIN, UI_TIME_SCALE_MAX, UI_TIME_SCALE_STEP );
    normalizeRequested( commands.physics.requestedPhysicsDebugAlpha >= 0.0f, commands.physics.requestedPhysicsDebugAlpha,
                        UI_PHYSICS_ALPHA_MIN, UI_PHYSICS_ALPHA_MAX, UI_PHYSICS_ALPHA_STEP );
    normalizeRequested( commands.physics.requestedPhysicsDebugContactLinger >= 0.0f,
                        commands.physics.requestedPhysicsDebugContactLinger, UI_CONTACT_LINGER_MIN, UI_CONTACT_LINGER_MAX,
                        UI_CONTACT_LINGER_STEP );
    normalizeRequested( commands.physics.requestRayCastImpulseStrength, commands.physics.requestedRayCastImpulseStrength,
                        UI_RAY_IMPULSE_MIN, UI_RAY_IMPULSE_MAX, UI_RAY_IMPULSE_STEP );
    normalizeRequested( commands.physics.requestLauncherProjectileSpeed, commands.physics.requestedLauncherProjectileSpeed,
                        UI_LAUNCHER_PROJECTILE_SPEED_MIN, UI_LAUNCHER_PROJECTILE_SPEED_MAX,
                        UI_LAUNCHER_PROJECTILE_SPEED_STEP );
    normalizeRequested( commands.physics.requestTerrainFrictionCoeff, commands.physics.requestedTerrainFrictionCoeff,
                        UI_FRICTION_COEFF_MIN, UI_FRICTION_COEFF_MAX, UI_FRICTION_COEFF_STEP );
    normalizeRequested( commands.physics.requestObjectFrictionCoeff, commands.physics.requestedObjectFrictionCoeff,
                        UI_FRICTION_COEFF_MIN, UI_FRICTION_COEFF_MAX, UI_FRICTION_COEFF_STEP );
    normalizeRequested( commands.physics.requestRollingFrictionCoeff, commands.physics.requestedRollingFrictionCoeff,
                        UI_ROLLING_FRICTION_COEFF_MIN, UI_ROLLING_FRICTION_COEFF_MAX, UI_ROLLING_FRICTION_COEFF_STEP );
    normalizeRequested( commands.physics.requestTornadoRadius, commands.physics.requestedTornadoRadius,
                        UI_TORNADO_RADIUS_MIN, UI_TORNADO_RADIUS_MAX, UI_TORNADO_RADIUS_STEP );
    normalizeRequested( commands.physics.requestTornadoHeight, commands.physics.requestedTornadoHeight,
                        UI_TORNADO_HEIGHT_MIN, UI_TORNADO_HEIGHT_MAX, UI_TORNADO_HEIGHT_STEP );
    normalizeRequested( commands.physics.requestTornadoInward, commands.physics.requestedTornadoInward,
                        UI_TORNADO_INWARD_MIN, UI_TORNADO_INWARD_MAX, UI_TORNADO_INWARD_STEP );
    normalizeRequested( commands.physics.requestTornadoSwirl, commands.physics.requestedTornadoSwirl, UI_TORNADO_SWIRL_MIN,
                        UI_TORNADO_SWIRL_MAX, UI_TORNADO_SWIRL_STEP );
    normalizeRequested( commands.physics.requestTornadoLift, commands.physics.requestedTornadoLift, UI_TORNADO_LIFT_MIN,
                        UI_TORNADO_LIFT_MAX, UI_TORNADO_LIFT_STEP );
    normalizeRequested( commands.water.requestWorldGravity, commands.water.requestedWorldGravity, -UI_WORLD_GRAVITY_MAX,
                        -UI_WORLD_GRAVITY_MIN, UI_WORLD_GRAVITY_STEP );
    normalizeRequested( commands.water.requestWorldFluidHeight, commands.water.requestedWorldFluidHeight,
                        UI_WORLD_FLUID_HEIGHT_MIN, UI_WORLD_FLUID_HEIGHT_MAX, UI_WORLD_FLUID_HEIGHT_STEP );
    normalizeRequested( commands.water.requestWorldFluidDensity, commands.water.requestedWorldFluidDensity,
                        UI_WORLD_FLUID_DENSITY_MIN, UI_WORLD_FLUID_DENSITY_MAX, UI_WORLD_FLUID_DENSITY_STEP );

    if ( commands.renderTuning.requestedParam != UI::UIRenderParam::None )
    {
        commands.renderTuning.requestedValue = NormalizeOrdinaryRenderParameter( commands.renderTuning.requestedParam,
                                                                                 commands.renderTuning.requestedValue );
    }

    if ( commands.cinematic.requestedParam != UI::UICinematicParam::None )
    {
        commands.cinematic.requestedValue = NormalizeCinematicParameter( commands.cinematic.requestedParam,
                                                                         commands.cinematic.requestedValue );
    }
}
} // namespace SkullbonezCore::Runtime::OperatorCommandBoundaryPolicy
