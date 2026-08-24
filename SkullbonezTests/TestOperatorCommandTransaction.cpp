/*
File: SkullbonezTests/TestOperatorCommandTransaction.cpp
Purpose:
  Locks the value-only operator-command owner and unified acceptance ledger.

Summary:
  Compile-time contracts reject a return to copyable transaction couriers or
  pointer-bearing acceptance values. Runtime checks pin detached command
  capture and the default ledger before phase execution. App command-boundary
  checks drive out-of-range values through the same stateless clamp seam used
  by live owner application.


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

    CHECK( Boundary::ClampTimeScale( Policy::UI_TIME_SCALE_MIN - Policy::UI_TIME_SCALE_STEP ) ==
           doctest::Approx( Policy::UI_TIME_SCALE_MIN ) );
    CHECK( Boundary::ClampTimeScale( Policy::UI_TIME_SCALE_MAX + Policy::UI_TIME_SCALE_STEP ) ==
           doctest::Approx( Policy::UI_TIME_SCALE_MAX ) );
    CHECK( Boundary::ClampSeed( Policy::UI_SEED_MIN - 1 ) == Policy::UI_SEED_MIN );
    CHECK( Boundary::ClampSeed( Policy::UI_SEED_MAX + 1 ) == Policy::UI_SEED_MAX );

    // Gravity's command value is signed world-Y acceleration. These reversed
    // signed endpoints catch both an omitted negation and a min/max swap.
    CHECK( Boundary::ClampWorldGravity( -Policy::UI_WORLD_GRAVITY_MAX - Policy::UI_WORLD_GRAVITY_STEP ) ==
           doctest::Approx( -Policy::UI_WORLD_GRAVITY_MAX ) );
    CHECK( Boundary::ClampWorldGravity( -Policy::UI_WORLD_GRAVITY_MIN + Policy::UI_WORLD_GRAVITY_STEP ) ==
           doctest::Approx( -Policy::UI_WORLD_GRAVITY_MIN ) );

    CHECK( Boundary::ClampWorldFluidHeight( Policy::UI_WORLD_FLUID_HEIGHT_MIN -
                                            Policy::UI_WORLD_FLUID_HEIGHT_STEP ) ==
           doctest::Approx( Policy::UI_WORLD_FLUID_HEIGHT_MIN ) );
    CHECK( Boundary::ClampWorldFluidHeight( Policy::UI_WORLD_FLUID_HEIGHT_MAX +
                                            Policy::UI_WORLD_FLUID_HEIGHT_STEP ) ==
           doctest::Approx( Policy::UI_WORLD_FLUID_HEIGHT_MAX ) );
    CHECK( Boundary::ClampWorldFluidDensity( Policy::UI_WORLD_FLUID_DENSITY_MIN -
                                             Policy::UI_WORLD_FLUID_DENSITY_STEP ) ==
           doctest::Approx( Policy::UI_WORLD_FLUID_DENSITY_MIN ) );
    CHECK( Boundary::ClampWorldFluidDensity( Policy::UI_WORLD_FLUID_DENSITY_MAX +
                                             Policy::UI_WORLD_FLUID_DENSITY_STEP ) ==
           doctest::Approx( Policy::UI_WORLD_FLUID_DENSITY_MAX ) );
}
} // namespace
