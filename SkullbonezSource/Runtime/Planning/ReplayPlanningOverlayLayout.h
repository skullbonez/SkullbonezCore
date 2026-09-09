// Shared scene-viewport geometry for trip controls and the transfer-cost grid.
#pragma once

#include "ReplayPorkchopPanel.h"
#include "ReplayTripPlanner.h"
#include "../../UI/UIDraw.h"

#include <cstddef>
#include <cstdint>

namespace SkullbonezCore::Runtime::ReplayOverlay
{
inline constexpr float REPLAY_INTERCEPT_READOUT_WIDTH = 250.0f;
inline constexpr float REPLAY_INTERCEPT_READOUT_HEIGHT = 28.0f;
inline constexpr float REPLAY_TRIP_PLANNER_PANEL_WIDTH = 500.0f;
inline constexpr float REPLAY_TRIP_PLANNER_PANEL_HEIGHT = 94.0f;
inline constexpr float REPLAY_PORKCHOP_PANEL_WIDTH = 720.0f;
inline constexpr float REPLAY_PORKCHOP_PANEL_HEIGHT = 420.0f;
inline constexpr float REPLAY_PORKCHOP_GRID_MARGIN_X = 40.0f;
inline constexpr float REPLAY_PORKCHOP_GRID_TOP = 52.0f;
inline constexpr float REPLAY_PORKCHOP_GRID_HEIGHT = 288.0f;

enum class ReplayTripPlannerControl : uint32_t
{
    None,
    TimeOfFlightDecrease,
    TimeOfFlightIncrease,
    Plan,
    Commit,
    Cancel,
    Panel
};

inline ReplayTripPlannerControl ReplayTripPlannerControlId( ReplayTripPlannerControl control )
{
    return control;
}

struct ReplayTripPlannerControlRow
{
    ReplayTripPlannerControl id = ReplayTripPlannerControl::None;
    ReplayTripPlannerCommandKind action = ReplayTripPlannerCommandKind::None;
    UI::UIRect drawRect;
    UI::UIRect hitRect;
    bool enabled = false;
};

// Planning retains command identity and pointer order; the component receives
// only the detached visibility/availability facts needed to draw this row.
inline UI::UIVisualState ReplayTripPlannerControlVisualState( const ReplayTripPlannerControlRow& control ) noexcept
{
    return control.enabled ? UI::UIVisualState::Visible | UI::UIVisualState::Enabled : UI::UIVisualState::Visible;
}

struct ReplayTripPlannerSurface
{
    ReplayTripPlannerControlRow controls[6] = {};
    std::size_t controlCount = 0;
    ReplayTripPlannerControl hotControl = ReplayTripPlannerControl::None;
    bool hasHotControl = false;
    bool consumesPointer = false;

    void Reset() noexcept;
    bool TryAdd( const ReplayTripPlannerControlRow& control ) noexcept;
    const ReplayTripPlannerControlRow* Find( ReplayTripPlannerControl id ) const noexcept;
    void ResolvePointer( int pointerX, int pointerY, bool pointerBlocked ) noexcept;
};

// Invariant: visible panels form one vertically scrollable column inside the
// scene viewport. Drawing, tooltips and pointer routing share its clipped bounds.
class ReplayPlanningLayout
{
  public:
    ReplayPlanningLayout( const UI::UIRect& viewport, bool interceptVisible, bool tripVisible, bool porkchopVisible,
                          float scroll = 0.0f );
    UI::UIRect Clip() const noexcept
    {
        return m_clip;
    }
    UI::UIRect Intercept() const noexcept
    {
        return m_intercept;
    }
    UI::UIRect Trip() const noexcept
    {
        return m_trip;
    }
    UI::UIRect Porkchop() const noexcept
    {
        return m_porkchop;
    }
    float Scroll() const noexcept
    {
        return m_scroll;
    }
    float MaximumScroll() const noexcept
    {
        return m_maximumScroll;
    }

  private:
    UI::UIRect m_clip;
    UI::UIRect m_intercept;
    UI::UIRect m_trip;
    UI::UIRect m_porkchop;
    float m_scroll = 0.0f;
    float m_maximumScroll = 0.0f;
};

UI::UIRect ReplayPorkchopGridRect( const UI::UIRect& panel );
UI::UIRect ReplayPorkchopCellRect( const UI::UIRect& panel, std::size_t cellIndex );
bool ReplayPorkchopCellAtPointer( const UI::UIRect& panel, int pointerX, int pointerY, std::size_t& outCellIndex );
void BuildReplayTripPlannerSurface( const ReplayTripPlannerView& planner, const UI::UIRect& panel,
                                    ReplayTripPlannerSurface& outSurface, bool baselineReady );
} // namespace SkullbonezCore::Runtime::ReplayOverlay
