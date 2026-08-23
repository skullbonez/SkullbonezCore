/*
File: SkullbonezSource/Runtime/Planning/ReplayPlanningOverlayLayout.cpp
Purpose:
  Implements Planning overlay rectangles and fixed control-surface publication.

Summary:
  Intercept, trip-planner, and porkchop presentation use one set of pixel-space
  helpers for both drawing and pointer routing. The implementation publishes
  bounded rows only and retains no UI or Planning owner borrow.

Glossary:
  Porkchop cell: One row-major launch-delay/time-of-flight candidate.
  Awaiting state: Planner phase waiting for a prediction generation or bounded
    correction result.

Invariants:
  - Cell-to-index mapping matches ReplayPorkchopPanel sweep order exactly.
  - Failure to publish the compile-time control count is a fatal-invariant invariant.

Related:
  - SkullbonezSource/Runtime/Planning/ReplayPlanningOverlayLayout.h
  - SkullbonezSource/Runtime/Planning/ReplayOverlayRenderer.cpp
*/
#include "ReplayPlanningOverlayLayout.h"
#include "../../Core/FatalError.h"

#include <algorithm>

namespace SkullbonezCore::Runtime::ReplayOverlay
{
void ReplayTripPlannerSurface::Reset() noexcept
{
    controlCount = 0;
    hotControl = ReplayTripPlannerControl::None;
    hasHotControl = false;
    consumesPointer = false;
}

bool ReplayTripPlannerSurface::TryAdd( const ReplayTripPlannerControlRow& control ) noexcept
{
    if ( control.id == ReplayTripPlannerControl::None || controlCount >= std::size( controls ) || Find( control.id ) )
    {
        return false;
    }

    controls[controlCount++] = control;
    return true;
}

const ReplayTripPlannerControlRow* ReplayTripPlannerSurface::Find( ReplayTripPlannerControl id ) const noexcept
{
    for ( std::size_t index = 0; index < controlCount; ++index )
    {
        if ( controls[index].id == id )
        {
            return &controls[index];
        }
    }

    return nullptr;
}

void ReplayTripPlannerSurface::ResolvePointer( int pointerX, int pointerY, bool pointerBlocked ) noexcept
{
    hotControl = ReplayTripPlannerControl::None;
    hasHotControl = false;
    consumesPointer = false;

    if ( pointerBlocked )
    {
        return;
    }

    const float x = static_cast<float>( pointerX );
    const float y = static_cast<float>( pointerY );

    for ( std::size_t index = 0; index < controlCount; ++index )
    {
        const ReplayTripPlannerControlRow& control = controls[index];
        const UI::UIRect& hit = control.hitRect;

        if ( x < hit.x || y < hit.y || x >= hit.x + hit.w || y >= hit.y + hit.h )
        {
            continue;
        }

        consumesPointer = true;

        if ( control.enabled )
        {
            hotControl = control.id;
            hasHotControl = true;
        }

        return;
    }
}

UI::UIRect ReplayInterceptReadoutRect( int screenW )
{
    const float width = (std::min)( REPLAY_INTERCEPT_READOUT_WIDTH, static_cast<float>( screenW ) );
    return { ( static_cast<float>( screenW ) - width ) * 0.5f, REPLAY_INTERCEPT_READOUT_TOP, width,
             REPLAY_INTERCEPT_READOUT_HEIGHT };
}

UI::UIRect ReplayTripPlannerPanelRect( int screenW )
{
    const float width = (std::min)( REPLAY_TRIP_PLANNER_PANEL_WIDTH, static_cast<float>( screenW ) );
    return { ( static_cast<float>( screenW ) - width ) * 0.5f, REPLAY_TRIP_PLANNER_PANEL_TOP, width,
             REPLAY_TRIP_PLANNER_PANEL_HEIGHT };
}

UI::UIRect ReplayPorkchopPanelRect( int screenW )
{
    const float width = (std::min)( REPLAY_PORKCHOP_PANEL_WIDTH, static_cast<float>( screenW ) );
    return { ( static_cast<float>( screenW ) - width ) * 0.5f, REPLAY_PORKCHOP_PANEL_TOP, width,
             REPLAY_PORKCHOP_PANEL_HEIGHT };
}

UI::UIRect ReplayPorkchopGridRect( int screenW )
{
    const UI::UIRect panel = ReplayPorkchopPanelRect( screenW );
    return { panel.x + REPLAY_PORKCHOP_GRID_MARGIN_X, panel.y + REPLAY_PORKCHOP_GRID_TOP,
             (std::max)( 1.0f, panel.w - 2.0f * REPLAY_PORKCHOP_GRID_MARGIN_X ), REPLAY_PORKCHOP_GRID_HEIGHT };
}

UI::UIRect ReplayPorkchopCellRect( int screenW, std::size_t cellIndex )
{
    const UI::UIRect grid = ReplayPorkchopGridRect( screenW );
    const std::size_t bounded = (std::min)( cellIndex, REPLAY_PORKCHOP_CELL_COUNT - 1u );
    const std::size_t column = bounded % REPLAY_PORKCHOP_COLUMNS;
    const std::size_t row = bounded / REPLAY_PORKCHOP_COLUMNS;
    const float cellWidth = grid.w / static_cast<float>( REPLAY_PORKCHOP_COLUMNS );
    const float cellHeight = grid.h / static_cast<float>( REPLAY_PORKCHOP_ROWS );
    return { grid.x + static_cast<float>( column ) * cellWidth, grid.y + static_cast<float>( row ) * cellHeight, cellWidth,
             cellHeight };
}

bool ReplayPorkchopCellAtPointer( int screenW, int pointerX, int pointerY, std::size_t& outCellIndex )
{
    const UI::UIRect grid = ReplayPorkchopGridRect( screenW );
    const float x = static_cast<float>( pointerX );
    const float y = static_cast<float>( pointerY );

    if ( x < grid.x || y < grid.y || x >= grid.x + grid.w || y >= grid.y + grid.h )
    {
        return false;
    }

    const float normalizedX = ( x - grid.x ) / grid.w;
    const float normalizedY = ( y - grid.y ) / grid.h;
    const std::size_t column = (std::min)( REPLAY_PORKCHOP_COLUMNS - 1u,
                                           static_cast<std::size_t>( normalizedX *
                                                                     static_cast<float>( REPLAY_PORKCHOP_COLUMNS ) ) );

    const std::size_t row = (std::min)( REPLAY_PORKCHOP_ROWS - 1u,
                                        static_cast<std::size_t>( normalizedY *
                                                                  static_cast<float>( REPLAY_PORKCHOP_ROWS ) ) );

    outCellIndex = row * REPLAY_PORKCHOP_COLUMNS + column;
    return true;
}

void BuildReplayTripPlannerSurface( const ReplayTripPlannerView& planner, int screenW, ReplayTripPlannerSurface& outSurface )
{
    outSurface.Reset();
    const UI::UIRect panel = ReplayTripPlannerPanelRect( screenW );
    const float y = panel.y + 52.0f;
    const UI::UIRect decrease { panel.x + 12.0f, y, 34.0f, 26.0f };
    const UI::UIRect increase { panel.x + 116.0f, y, 34.0f, 26.0f };
    const UI::UIRect plan { panel.x + 166.0f, y, 92.0f, 26.0f };
    const UI::UIRect commit { panel.x + 274.0f, y, 92.0f, 26.0f };
    const UI::UIRect cancel { panel.x + 382.0f, y, 92.0f, 26.0f };

    const bool awaiting = planner.state == ReplayTripPlannerState::Seeding ||
                          planner.state == ReplayTripPlannerState::AwaitingPrediction ||
                          planner.state == ReplayTripPlannerState::Correcting;

    const bool idle = planner.state == ReplayTripPlannerState::Idle;
    const bool canCancel = awaiting || planner.state == ReplayTripPlannerState::Converged ||
                           planner.state == ReplayTripPlannerState::Failed;

    const auto add = [&]( ReplayTripPlannerControl id, ReplayTripPlannerCommandKind action, const UI::UIRect& rect,
                          bool enabled )
    {
        ReplayTripPlannerControlRow control;

        control.id = ReplayTripPlannerControlId( id );
        control.action = action;
        control.drawRect = rect;
        control.hitRect = rect;
        control.enabled = enabled;

        if ( !outSurface.TryAdd( control ) )
        {
            SB_FATAL( "ReplayTripPlannerSurface", "Cannot publish trip-planner control id=%u.",
                      static_cast<uint32_t>( id ) );
        }
    };

    add( ReplayTripPlannerControl::TimeOfFlightDecrease, ReplayTripPlannerCommandKind::DecreaseTimeOfFlight, decrease, idle );

    add( ReplayTripPlannerControl::TimeOfFlightIncrease, ReplayTripPlannerCommandKind::IncreaseTimeOfFlight, increase, idle );

    add( ReplayTripPlannerControl::Plan, ReplayTripPlannerCommandKind::Plan, plan, planner.available && idle );

    add( ReplayTripPlannerControl::Commit, ReplayTripPlannerCommandKind::Commit, commit,
         planner.state == ReplayTripPlannerState::Converged );

    add( ReplayTripPlannerControl::Cancel, ReplayTripPlannerCommandKind::Cancel, cancel, canCancel );

    add( ReplayTripPlannerControl::Panel, ReplayTripPlannerCommandKind::None, panel, true );
}
} // namespace SkullbonezCore::Runtime::ReplayOverlay
