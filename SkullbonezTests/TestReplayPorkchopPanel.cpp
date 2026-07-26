/*
File: SkullbonezTests/TestReplayPorkchopPanel.cpp
Purpose:
  Verifies the bounded solar departure-window sweep and selection contract.

Summary:
  Tests use the authored Earth/Mars design scale: G=4, sun mass=10000,
  departure radius=80, target radius=121.6, and a 44.1-degree target lead.
  They prove the fixed work budget, the Hohmann-neighbourhood minimum, failure
  sentinels, and click-selection values consumed by the trip planner.

Glossary:
  Hohmann neighbourhood: Low-cost transfer region near the ideal circular-orbit
    half-ellipse time and initial phase.
  Sweep quantum: Maximum number of grid cells evaluated by one frame call.

Invariants:
  - Tests never depend on wall-clock timing for mathematical correctness.
  - Every completed valid cell has a finite non-negative delta-v.
  - Invalid source state never publishes a complete heatmap.

Related:
  - SkullbonezSource/Runtime/Planning/ReplayPorkchopPanel.h
  - SkullbonezSource/Maths/OrbitalMechanics.h
*/
#include "../SkullbonezSource/Runtime/Planning/ReplayPorkchopPanel.h"
#include "../SkullbonezSource/Runtime/Planning/ReplayPlanningOverlayLayout.h"

#include <doctest/doctest.h>

#include <cmath>

namespace
{
using SkullbonezCore::Math::Vector::Vector3;
using SkullbonezCore::Runtime::REPLAY_PORKCHOP_CELL_COUNT;
using SkullbonezCore::Runtime::REPLAY_PORKCHOP_CELLS_PER_FRAME;
using SkullbonezCore::Runtime::ReplayPorkchopBodyState;
using SkullbonezCore::Runtime::ReplayPorkchopPanel;
using SkullbonezCore::Runtime::ReplayPorkchopSweepInput;

ReplayPorkchopBodyState Body( uint32_t id, const Vector3& position, const Vector3& velocity, float mass = 1.0f )
{
    ReplayPorkchopBodyState body;
    body.id.value = id;
    body.position = position;
    body.linearVelocity = velocity;
    body.mass = mass;
    body.valid = true;
    return body;
}

ReplayPorkchopSweepInput SolarDesignWindow()
{
    constexpr float phase = 44.1f * 3.14159265358979323846f / 180.0f;
    ReplayPorkchopSweepInput input;
    input.sun = Body( 1, Vector3( 0.0f, 0.0f, 0.0f ), Vector3( 0.0f, 0.0f, 0.0f ), 10000.0f );
    input.departure = Body( 2, Vector3( 80.0f, 0.0f, 0.0f ), Vector3( 0.0f, std::sqrt( 500.0f ), 0.0f ) );
    input.target = Body( 3,
                         Vector3( 121.6f * std::cos( phase ), 121.6f * std::sin( phase ), 0.0f ),
                         Vector3( -std::sqrt( 40000.0f / 121.6f ) * std::sin( phase ),
                                  std::sqrt( 40000.0f / 121.6f ) * std::cos( phase ),
                                  0.0f ) );
    input.gravitationalConstant = 4.0f;
    input.mutualGravityEnabled = true;
    return input;
}
} // namespace

TEST_CASE( "Replay porkchop sweep is bounded and finds the low-cost transfer neighbourhood" )
{
    ReplayPorkchopPanel panel;
    ReplayPorkchopSweepInput input = SolarDesignWindow();
    input.epochSeconds = 100.0;
    CHECK_FALSE( panel.Visible() );
    CHECK_FALSE( panel.NeedsRefresh( input.target.id, true ) );

    panel.Toggle();
    REQUIRE( panel.NeedsRefresh( input.target.id, true ) );
    panel.BeginSweep( input );
    REQUIRE( panel.View().building );

    std::size_t previous = 0u;
    double nowSeconds = input.epochSeconds;
    while ( panel.View().building )
    {
        nowSeconds += 1.0 / 60.0;
        panel.AdvanceSweep( nowSeconds );
        const auto view = panel.View();
        CHECK( view.completedCells - previous <= REPLAY_PORKCHOP_CELLS_PER_FRAME );
        previous = view.completedCells;
    }

    const auto view = panel.View();
    REQUIRE( view.complete );
    CHECK( view.completedCells == REPLAY_PORKCHOP_CELL_COUNT );
    CHECK( view.minimumDeltaV > 3.5f );
    CHECK( view.minimumDeltaV < 5.0f );
    CHECK( ReplayPorkchopPanel::DepartureDelaySeconds( view.minimumCell % 64u ) < 3.0f );
    CHECK( ReplayPorkchopPanel::TimeOfFlightSeconds( view.minimumCell / 64u ) > 14.0f );
    CHECK( ReplayPorkchopPanel::TimeOfFlightSeconds( view.minimumCell / 64u ) < 18.0f );
    CHECK( view.refreshComputeMilliseconds >= 0.0 );
    CHECK( view.maximumFrameComputeMilliseconds >= 0.0 );
    CHECK( view.maximumFrameComputeMilliseconds <= view.refreshComputeMilliseconds );

    REQUIRE( panel.SelectCell( view.minimumCell ) );
    const auto selected = panel.View();
    CHECK( selected.selectedCell == static_cast<int>( view.minimumCell ) );
    CHECK( selected.selectedDeltaV == doctest::Approx( view.minimumDeltaV ) );
    CHECK( selected.selectedTimeOfFlightSeconds ==
           doctest::Approx( ReplayPorkchopPanel::TimeOfFlightSeconds( view.minimumCell / 64u ) ) );

    std::size_t lateCell = 63u;
    while ( lateCell < selected.deltaV.size() && selected.deltaV[lateCell] < 0.0f )
    {
        lateCell += 64u;
    }
    REQUIRE( lateCell < selected.deltaV.size() );
    REQUIRE( panel.SelectCell( lateCell ) );
    panel.AdvanceSweep( input.epochSeconds + 5.0 );
    CHECK( panel.View().sweepAgeSeconds == doctest::Approx( 5.0f ) );
    CHECK( panel.View().selectedDepartureDelaySeconds ==
           doctest::Approx( ReplayPorkchopPanel::DepartureDelaySeconds( 63u ) - 5.0f ) );
}

TEST_CASE( "Replay porkchop invalid source state stays unavailable and failed" )
{
    ReplayPorkchopPanel panel;
    ReplayPorkchopSweepInput input = SolarDesignWindow();
    input.target.position = input.sun.position;
    input.target.linearVelocity = input.sun.linearVelocity;
    panel.Toggle();
    panel.BeginSweep( input );
    panel.AdvanceSweep( input.epochSeconds );

    const auto view = panel.View();
    CHECK( view.visible );
    CHECK_FALSE( view.available );
    CHECK_FALSE( view.building );
    CHECK_FALSE( view.complete );
    CHECK( view.completedCells == 0u );
    CHECK_FALSE( panel.SelectCell( 0u ) );
    CHECK_FALSE( panel.NeedsRefresh( input.target.id, true ) );
}

TEST_CASE( "Replay porkchop refresh follows visibility and target identity" )
{
    ReplayPorkchopPanel panel;
    const ReplayPorkchopSweepInput input = SolarDesignWindow();
    panel.Toggle();
    panel.BeginSweep( input );
    panel.AdvanceSweep( input.epochSeconds );
    CHECK_FALSE( panel.NeedsRefresh( input.target.id, true ) );

    auto replacementId = input.target.id;
    ++replacementId.value;
    CHECK( panel.NeedsRefresh( replacementId, true ) );
    CHECK( panel.NeedsRefresh( input.target.id, false ) );

    const std::size_t completed = panel.View().completedCells;
    panel.Toggle();
    panel.AdvanceSweep( input.epochSeconds + 1.0 );
    CHECK( panel.View().completedCells == completed );
    panel.Reset();
    CHECK_FALSE( panel.Visible() );
    CHECK( panel.View().completedCells == 0u );
}

TEST_CASE( "Replay porkchop drawing and hit testing share one cell geometry" )
{
    constexpr int screenWidth = 1800;
    constexpr std::size_t cellIndex = 36u * 64u + 1u;
    const auto cell = SkullbonezCore::Runtime::ReplayOverlay::ReplayPorkchopCellRect( screenWidth, cellIndex );
    std::size_t resolved = 0u;
    REQUIRE(
        SkullbonezCore::Runtime::ReplayOverlay::ReplayPorkchopCellAtPointer( screenWidth,
                                                                             static_cast<int>( cell.x + cell.w * 0.5f ),
                                                                             static_cast<int>( cell.y + cell.h * 0.5f ),
                                                                             resolved ) );
    CHECK( resolved == cellIndex );
    CHECK_FALSE( SkullbonezCore::Runtime::ReplayOverlay::ReplayPorkchopCellAtPointer( screenWidth, 0, 0, resolved ) );
}
