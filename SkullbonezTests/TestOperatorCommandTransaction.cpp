/*
File: SkullbonezTests/TestOperatorCommandTransaction.cpp
Purpose:
  Locks the value-only operator-command owner and unified acceptance ledger.

Summary:
  Compile-time contracts reject a return to copyable transaction couriers or
  pointer-bearing acceptance values. Runtime checks pin detached command
  capture and the default ledger before phase execution. App command-boundary
  checks drive endpoint and off-grid values through the same stateless
  clamp-and-snap seam used by live owner application.


  One normalized UI packet enters one non-copyable transaction. The only
  durable output is a detached acceptance ledger consumed by InputFrame.

Invariants:
  - The transaction cannot be copied or assigned.
  - The acceptance ledger is a value and contains no runtime owner.
  - Command capture does not mutate the source packet.

Related:
  - SkullbonezSource/Runtime/Interaction/OperatorCommandTransaction.h
  - SkullbonezSource/Runtime/App/InputFrame.cpp
*/
#include "../ThirdPtySource/doctest/doctest.h"

#include "../SkullbonezSource/Runtime/App/OperatorCommandBoundaryPolicy.h"
#include "../SkullbonezSource/Runtime/Interaction/OperatorCommandTransaction.h"

#include <type_traits>

namespace
{
using SkullbonezCore::Runtime::OperatorCommandAcceptanceLedger;
using SkullbonezCore::Runtime::OperatorCommandTransaction;

static_assert( !std::is_copy_constructible_v<OperatorCommandTransaction> );
static_assert( !std::is_copy_assignable_v<OperatorCommandTransaction> );
static_assert( std::is_trivially_copyable_v<OperatorCommandAcceptanceLedger> );

TEST_CASE( "Operator command transaction starts with a detached empty ledger" )
{
    SkullbonezCore::UI::InGameUICommands commands;
    commands.renderer.toggleVsync = true;
    OperatorCommandTransaction transaction( commands );
    commands.renderer.toggleVsync = false;

    CHECK( transaction.Phase() == SkullbonezCore::Runtime::OperatorCommandPhaseCursor::Phase::Idle );
    CHECK_FALSE( transaction.Acceptance().toggledVsync );
    CHECK_FALSE( transaction.Acceptance().cameraModeAccepted );
    CHECK( transaction.Acceptance().cameraModeIndex == -1 );
    CHECK_FALSE( transaction.Acceptance().worldOverrideAccepted );
}

TEST_CASE( "App command boundary clamps out-of-range operator values to shared policy endpoints" )
{
    namespace Boundary = SkullbonezCore::Runtime::OperatorCommandBoundaryPolicy;
    namespace Policy = SkullbonezCore::UI::OperatorControlPolicy;

    CHECK( Boundary::NormalizeTimeScale( Policy::UI_TIME_SCALE_MIN - Policy::UI_TIME_SCALE_STEP ) ==
           doctest::Approx( Policy::UI_TIME_SCALE_MIN ) );
    CHECK( Boundary::NormalizeTimeScale( Policy::UI_TIME_SCALE_MAX + Policy::UI_TIME_SCALE_STEP ) ==
           doctest::Approx( Policy::UI_TIME_SCALE_MAX ) );
    CHECK( Boundary::ClampSeed( Policy::UI_SEED_MIN - 1 ) == Policy::UI_SEED_MIN );
    CHECK( Boundary::ClampSeed( Policy::UI_SEED_MAX + 1 ) == Policy::UI_SEED_MAX );

    // Gravity's command value is signed world-Y acceleration. These reversed
    // signed endpoints catch both an omitted negation and a min/max swap.
    CHECK( Boundary::NormalizeWorldGravity( -Policy::UI_WORLD_GRAVITY_MAX - Policy::UI_WORLD_GRAVITY_STEP ) ==
           doctest::Approx( -Policy::UI_WORLD_GRAVITY_MAX ) );
    CHECK( Boundary::NormalizeWorldGravity( -Policy::UI_WORLD_GRAVITY_MIN + Policy::UI_WORLD_GRAVITY_STEP ) ==
           doctest::Approx( -Policy::UI_WORLD_GRAVITY_MIN ) );

    CHECK( Boundary::NormalizeWorldFluidHeight( Policy::UI_WORLD_FLUID_HEIGHT_MIN -
                                                Policy::UI_WORLD_FLUID_HEIGHT_STEP ) ==
           doctest::Approx( Policy::UI_WORLD_FLUID_HEIGHT_MIN ) );
    CHECK( Boundary::NormalizeWorldFluidHeight( Policy::UI_WORLD_FLUID_HEIGHT_MAX +
                                                Policy::UI_WORLD_FLUID_HEIGHT_STEP ) ==
           doctest::Approx( Policy::UI_WORLD_FLUID_HEIGHT_MAX ) );
    CHECK( Boundary::NormalizeWorldFluidDensity( Policy::UI_WORLD_FLUID_DENSITY_MIN -
                                                 Policy::UI_WORLD_FLUID_DENSITY_STEP ) ==
           doctest::Approx( Policy::UI_WORLD_FLUID_DENSITY_MIN ) );
    CHECK( Boundary::NormalizeWorldFluidDensity( Policy::UI_WORLD_FLUID_DENSITY_MAX +
                                                 Policy::UI_WORLD_FLUID_DENSITY_STEP ) ==
           doctest::Approx( Policy::UI_WORLD_FLUID_DENSITY_MAX ) );

    for ( const SkullbonezCore::UI::RenderSliderSpec& policy : SkullbonezCore::UI::kRenderSliderSpecs )
    {
        CHECK( Boundary::NormalizeOrdinaryRenderParameter( policy.param, policy.minValue - policy.step ) ==
               doctest::Approx( policy.minValue ) );
        CHECK( Boundary::NormalizeOrdinaryRenderParameter( policy.param, policy.maxValue + policy.step ) ==
               doctest::Approx( policy.maxValue ) );
    }

    for ( const SkullbonezCore::UI::CinematicSliderSpec& policy : SkullbonezCore::UI::kCinematicSliderSpecs )
    {
        CHECK( Boundary::NormalizeCinematicParameter( policy.param, policy.minValue - policy.step ) ==
               doctest::Approx( policy.minValue ) );
        CHECK( Boundary::NormalizeCinematicParameter( policy.param, policy.maxValue + policy.step ) ==
               doctest::Approx( policy.maxValue ) );
    }
}

TEST_CASE( "App command boundary snaps every shared float command to its canonical interior grid" )
{
    namespace Boundary = SkullbonezCore::Runtime::OperatorCommandBoundaryPolicy;
    namespace Policy = SkullbonezCore::UI::OperatorControlPolicy;

    const auto offGrid = []( float minValue, float step ) { return minValue + 1.4f * step; };

    // Ray policy values are exactly representable at these magnitudes. These
    // checks distinguish nearest-grid rounding from an incorrect floor-only
    // implementation and lock the documented halfway direction.
    CHECK( Boundary::NormalizeFloat( Policy::UI_RAY_IMPULSE_MIN + 1.6f * Policy::UI_RAY_IMPULSE_STEP,
                                     Policy::UI_RAY_IMPULSE_MIN, Policy::UI_RAY_IMPULSE_MAX,
                                     Policy::UI_RAY_IMPULSE_STEP ) ==
           doctest::Approx( Policy::UI_RAY_IMPULSE_MIN + 2.0f * Policy::UI_RAY_IMPULSE_STEP ) );
    CHECK( Boundary::NormalizeFloat( Policy::UI_RAY_IMPULSE_MIN + 1.5f * Policy::UI_RAY_IMPULSE_STEP,
                                     Policy::UI_RAY_IMPULSE_MIN, Policy::UI_RAY_IMPULSE_MAX,
                                     Policy::UI_RAY_IMPULSE_STEP ) ==
           doctest::Approx( Policy::UI_RAY_IMPULSE_MIN + 2.0f * Policy::UI_RAY_IMPULSE_STEP ) );

    SkullbonezCore::UI::InGameUICommands commands;
    commands.sceneOptions.requestedTimeScale = offGrid( Policy::UI_TIME_SCALE_MIN, Policy::UI_TIME_SCALE_STEP );
    commands.physics.requestedPhysicsDebugAlpha = offGrid( Policy::UI_PHYSICS_ALPHA_MIN, Policy::UI_PHYSICS_ALPHA_STEP );
    commands.physics.requestedPhysicsDebugContactLinger =
        offGrid( Policy::UI_CONTACT_LINGER_MIN, Policy::UI_CONTACT_LINGER_STEP );
    commands.physics.requestRayCastImpulseStrength = true;
    commands.physics.requestedRayCastImpulseStrength = offGrid( Policy::UI_RAY_IMPULSE_MIN, Policy::UI_RAY_IMPULSE_STEP );
    commands.physics.requestLauncherProjectileSpeed = true;
    commands.physics.requestedLauncherProjectileSpeed =
        offGrid( Policy::UI_LAUNCHER_PROJECTILE_SPEED_MIN, Policy::UI_LAUNCHER_PROJECTILE_SPEED_STEP );
    commands.physics.requestTerrainFrictionCoeff = true;
    commands.physics.requestedTerrainFrictionCoeff = offGrid( Policy::UI_FRICTION_COEFF_MIN, Policy::UI_FRICTION_COEFF_STEP );
    commands.physics.requestObjectFrictionCoeff = true;
    commands.physics.requestedObjectFrictionCoeff = offGrid( Policy::UI_FRICTION_COEFF_MIN, Policy::UI_FRICTION_COEFF_STEP );
    commands.physics.requestRollingFrictionCoeff = true;
    commands.physics.requestedRollingFrictionCoeff =
        offGrid( Policy::UI_ROLLING_FRICTION_COEFF_MIN, Policy::UI_ROLLING_FRICTION_COEFF_STEP );
    commands.physics.requestTornadoRadius = true;
    commands.physics.requestedTornadoRadius = offGrid( Policy::UI_TORNADO_RADIUS_MIN, Policy::UI_TORNADO_RADIUS_STEP );
    commands.physics.requestTornadoHeight = true;
    commands.physics.requestedTornadoHeight = offGrid( Policy::UI_TORNADO_HEIGHT_MIN, Policy::UI_TORNADO_HEIGHT_STEP );
    commands.physics.requestTornadoInward = true;
    commands.physics.requestedTornadoInward = offGrid( Policy::UI_TORNADO_INWARD_MIN, Policy::UI_TORNADO_INWARD_STEP );
    commands.physics.requestTornadoSwirl = true;
    commands.physics.requestedTornadoSwirl = offGrid( Policy::UI_TORNADO_SWIRL_MIN, Policy::UI_TORNADO_SWIRL_STEP );
    commands.physics.requestTornadoLift = true;
    commands.physics.requestedTornadoLift = offGrid( Policy::UI_TORNADO_LIFT_MIN, Policy::UI_TORNADO_LIFT_STEP );
    commands.water.requestWorldGravity = true;
    commands.water.requestedWorldGravity = offGrid( -Policy::UI_WORLD_GRAVITY_MAX, Policy::UI_WORLD_GRAVITY_STEP );
    commands.water.requestWorldFluidHeight = true;
    commands.water.requestedWorldFluidHeight =
        offGrid( Policy::UI_WORLD_FLUID_HEIGHT_MIN, Policy::UI_WORLD_FLUID_HEIGHT_STEP );
    commands.water.requestWorldFluidDensity = true;
    commands.water.requestedWorldFluidDensity =
        offGrid( Policy::UI_WORLD_FLUID_DENSITY_MIN, Policy::UI_WORLD_FLUID_DENSITY_STEP );
    const SkullbonezCore::UI::RenderSliderSpec& renderPolicy = SkullbonezCore::UI::kRenderSliderSpecs[0];
    commands.renderTuning.requestedParam = renderPolicy.param;
    commands.renderTuning.requestedValue = offGrid( renderPolicy.minValue, renderPolicy.step );
    const SkullbonezCore::UI::CinematicSliderSpec& cinematicPolicy = SkullbonezCore::UI::kCinematicSliderSpecs[0];
    commands.cinematic.requestedParam = cinematicPolicy.param;
    commands.cinematic.requestedValue = offGrid( cinematicPolicy.minValue, cinematicPolicy.step );

    Boundary::NormalizeOperatorCommands( commands );

    CHECK( commands.sceneOptions.requestedTimeScale ==
           doctest::Approx( Policy::UI_TIME_SCALE_MIN + Policy::UI_TIME_SCALE_STEP ) );
    CHECK( commands.physics.requestedPhysicsDebugAlpha ==
           doctest::Approx( Policy::UI_PHYSICS_ALPHA_MIN + Policy::UI_PHYSICS_ALPHA_STEP ) );
    CHECK( commands.physics.requestedPhysicsDebugContactLinger ==
           doctest::Approx( Policy::UI_CONTACT_LINGER_MIN + Policy::UI_CONTACT_LINGER_STEP ) );
    CHECK( commands.physics.requestedRayCastImpulseStrength ==
           doctest::Approx( Policy::UI_RAY_IMPULSE_MIN + Policy::UI_RAY_IMPULSE_STEP ) );
    CHECK( commands.physics.requestedLauncherProjectileSpeed ==
           doctest::Approx( Policy::UI_LAUNCHER_PROJECTILE_SPEED_MIN + Policy::UI_LAUNCHER_PROJECTILE_SPEED_STEP ) );
    CHECK( commands.physics.requestedTerrainFrictionCoeff ==
           doctest::Approx( Policy::UI_FRICTION_COEFF_MIN + Policy::UI_FRICTION_COEFF_STEP ) );
    CHECK( commands.physics.requestedObjectFrictionCoeff ==
           doctest::Approx( Policy::UI_FRICTION_COEFF_MIN + Policy::UI_FRICTION_COEFF_STEP ) );
    CHECK( commands.physics.requestedRollingFrictionCoeff ==
           doctest::Approx( Policy::UI_ROLLING_FRICTION_COEFF_MIN + Policy::UI_ROLLING_FRICTION_COEFF_STEP ) );
    CHECK( commands.physics.requestedTornadoRadius ==
           doctest::Approx( Policy::UI_TORNADO_RADIUS_MIN + Policy::UI_TORNADO_RADIUS_STEP ) );
    CHECK( commands.physics.requestedTornadoHeight ==
           doctest::Approx( Policy::UI_TORNADO_HEIGHT_MIN + Policy::UI_TORNADO_HEIGHT_STEP ) );
    CHECK( commands.physics.requestedTornadoInward ==
           doctest::Approx( Policy::UI_TORNADO_INWARD_MIN + Policy::UI_TORNADO_INWARD_STEP ) );
    CHECK( commands.physics.requestedTornadoSwirl ==
           doctest::Approx( Policy::UI_TORNADO_SWIRL_MIN + Policy::UI_TORNADO_SWIRL_STEP ) );
    CHECK( commands.physics.requestedTornadoLift ==
           doctest::Approx( Policy::UI_TORNADO_LIFT_MIN + Policy::UI_TORNADO_LIFT_STEP ) );
    CHECK( commands.water.requestedWorldGravity ==
           doctest::Approx( -Policy::UI_WORLD_GRAVITY_MAX + Policy::UI_WORLD_GRAVITY_STEP ) );
    CHECK( commands.water.requestedWorldFluidHeight ==
           doctest::Approx( Policy::UI_WORLD_FLUID_HEIGHT_MIN + Policy::UI_WORLD_FLUID_HEIGHT_STEP ) );
    CHECK( commands.water.requestedWorldFluidDensity ==
           doctest::Approx( Policy::UI_WORLD_FLUID_DENSITY_MIN + Policy::UI_WORLD_FLUID_DENSITY_STEP ) );
    CHECK( commands.renderTuning.requestedValue == doctest::Approx( renderPolicy.minValue + renderPolicy.step ) );
    CHECK( commands.cinematic.requestedValue ==
           doctest::Approx( cinematicPolicy.minValue + cinematicPolicy.step ) );

    for ( const SkullbonezCore::UI::RenderSliderSpec& policy : SkullbonezCore::UI::kRenderSliderSpecs )
    {
        CHECK( Boundary::NormalizeOrdinaryRenderParameter( policy.param, offGrid( policy.minValue, policy.step ) ) ==
               doctest::Approx( policy.minValue + policy.step ) );
    }

    for ( const SkullbonezCore::UI::CinematicSliderSpec& policy : SkullbonezCore::UI::kCinematicSliderSpecs )
    {
        CHECK( Boundary::NormalizeCinematicParameter( policy.param, offGrid( policy.minValue, policy.step ) ) ==
               doctest::Approx( policy.minValue + policy.step ) );
    }
}
} // namespace
