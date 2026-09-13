// Planning owns bounded panel geometry shared by input and drawing.
#include "ReplayPlanningOverlayLayout.h"
#include "../../Core/FatalError.h"

#include <algorithm>
#include <cmath>

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

namespace
{
int TripControlColumns( float width )
{
    return std::clamp( static_cast<int>( ( width - 16.0f ) / 86.0f ), 1, 5 );
}
} // namespace

ReplayPlanningLayout::ReplayPlanningLayout( const UI::UIRect& viewport, bool interceptVisible, bool tripVisible, bool porkchopVisible, float scroll )
{
    m_clip = viewport;
    const float width = (std::max)( 0.0f, viewport.w - 16.0f );
    float y = viewport.y + 8.0f;
    const auto append = [&]( float preferredWidth, float height, bool visible ) -> UI::UIRect
    {
        if ( !visible || width <= 0.0f || viewport.h <= 0.0f )
        {
            return {};
        }
        const float panelWidth = (std::min)( width, preferredWidth );
        const UI::UIRect panel { viewport.x + ( viewport.w - panelWidth ) * 0.5f, y, panelWidth, height };
        y += height + 8.0f;
        return panel;
    };
    m_intercept = append( REPLAY_INTERCEPT_READOUT_WIDTH, REPLAY_INTERCEPT_READOUT_HEIGHT, interceptVisible );
    const int columns = TripControlColumns( (std::min)( width, REPLAY_TRIP_PLANNER_PANEL_WIDTH ) );
    const int rows = ( 5 + columns - 1 ) / columns;
    m_trip = append( REPLAY_TRIP_PLANNER_PANEL_WIDTH, 64.0f + rows * 30.0f, tripVisible );
    m_porkchop = append( REPLAY_PORKCHOP_PANEL_WIDTH, REPLAY_PORKCHOP_PANEL_HEIGHT, porkchopVisible );
    m_maximumScroll = (std::max)( 0.0f, y - viewport.y - viewport.h );
    m_scroll = std::isfinite( scroll ) ? std::clamp( scroll, 0.0f, m_maximumScroll ) : 0.0f;
    for ( UI::UIRect* panel : { &m_intercept, &m_trip, &m_porkchop } )
    {
        if ( panel->w > 0.0f )
        {
            panel->y -= m_scroll;
        }
    }
}

UI::UIRect ReplayPorkchopGridRect( const UI::UIRect& panel )
{
    const float margin = (std::min)( REPLAY_PORKCHOP_GRID_MARGIN_X, panel.w * 0.15f );
    return { panel.x + margin, panel.y + REPLAY_PORKCHOP_GRID_TOP, (std::max)( 0.0f, panel.w - 2.0f * margin ), REPLAY_PORKCHOP_GRID_HEIGHT };
}

UI::UIRect ReplayPorkchopCellRect( const UI::UIRect& panel, std::size_t cellIndex )
{
    const UI::UIRect grid = ReplayPorkchopGridRect( panel );
    const std::size_t bounded = (std::min)( cellIndex, REPLAY_PORKCHOP_CELL_COUNT - 1u );
    const std::size_t column = bounded % REPLAY_PORKCHOP_COLUMNS;
    const std::size_t row = bounded / REPLAY_PORKCHOP_COLUMNS;
    const float cellWidth = grid.w / static_cast<float>( REPLAY_PORKCHOP_COLUMNS );
    const float cellHeight = grid.h / static_cast<float>( REPLAY_PORKCHOP_ROWS );
    return { grid.x + static_cast<float>( column ) * cellWidth, grid.y + static_cast<float>( row ) * cellHeight, cellWidth, cellHeight };
}

bool ReplayPorkchopCellAtPointer( const UI::UIRect& panel, int pointerX, int pointerY, std::size_t& outCellIndex )
{
    const UI::UIRect grid = ReplayPorkchopGridRect( panel );
    const float x = static_cast<float>( pointerX );
    const float y = static_cast<float>( pointerY );

    if ( x < grid.x || y < grid.y || x >= grid.x + grid.w || y >= grid.y + grid.h )
    {
        return false;
    }

    const float normalizedX = ( x - grid.x ) / grid.w;
    const float normalizedY = ( y - grid.y ) / grid.h;
    const std::size_t column = (std::min)( REPLAY_PORKCHOP_COLUMNS - 1u, static_cast<std::size_t>( normalizedX * static_cast<float>( REPLAY_PORKCHOP_COLUMNS ) ) );

    const std::size_t row = (std::min)( REPLAY_PORKCHOP_ROWS - 1u, static_cast<std::size_t>( normalizedY * static_cast<float>( REPLAY_PORKCHOP_ROWS ) ) );

    outCellIndex = row * REPLAY_PORKCHOP_COLUMNS + column;
    return true;
}

void BuildReplayTripPlannerSurface( const ReplayTripPlannerView& planner, const UI::UIRect& panel, ReplayTripPlannerSurface& outSurface, bool baselineReady )
{
    outSurface.Reset();
    const int columns = TripControlColumns( panel.w );
    const float width = (std::max)( 0.0f, panel.w - 16.0f ) / columns;
    const auto button = [&]( int index ) -> UI::UIRect
    { return { panel.x + 8.0f + ( index % columns ) * width, panel.y + 52.0f + ( index / columns ) * 30.0f, (std::max)( 0.0f, width - 4.0f ), 26.0f }; };
    const UI::UIRect decrease = button( 0 );
    const UI::UIRect increase = button( 1 );
    const UI::UIRect plan = button( 2 );
    const UI::UIRect commit = button( 3 );
    const UI::UIRect cancel = button( 4 );

    const bool awaiting = planner.state == ReplayTripPlannerState::Seeding || planner.state == ReplayTripPlannerState::AwaitingPrediction || planner.state == ReplayTripPlannerState::Correcting;

    const bool idle = planner.state == ReplayTripPlannerState::Idle;
    const bool canCancel = awaiting || planner.state == ReplayTripPlannerState::Converged || planner.state == ReplayTripPlannerState::Failed;

    const auto add = [&]( ReplayTripPlannerControl id, ReplayTripPlannerCommandKind action, const UI::UIRect& rect, bool enabled )
    {
        ReplayTripPlannerControlRow control;

        control.id = ReplayTripPlannerControlId( id );
        control.action = action;
        control.drawRect = rect;
        control.hitRect = rect;
        control.enabled = enabled;

        if ( !outSurface.TryAdd( control ) )
        {
            SB_FATAL( "ReplayTripPlannerSurface", "Cannot publish trip-planner control id=%u.", static_cast<uint32_t>( id ) );
        }
    };

    add( ReplayTripPlannerControl::TimeOfFlightDecrease, ReplayTripPlannerCommandKind::DecreaseTimeOfFlight, decrease, idle );

    add( ReplayTripPlannerControl::TimeOfFlightIncrease, ReplayTripPlannerCommandKind::IncreaseTimeOfFlight, increase, idle );

    add( ReplayTripPlannerControl::Plan, ReplayTripPlannerCommandKind::Plan, plan, planner.available && idle && baselineReady && !planner.liveAdvancing );

    add( ReplayTripPlannerControl::Commit, ReplayTripPlannerCommandKind::Commit, commit, planner.state == ReplayTripPlannerState::Converged );

    add( ReplayTripPlannerControl::Cancel, ReplayTripPlannerCommandKind::Cancel, cancel, canCancel );

    add( ReplayTripPlannerControl::Panel, ReplayTripPlannerCommandKind::None, panel, true );
}
} // namespace SkullbonezCore::Runtime::ReplayOverlay
