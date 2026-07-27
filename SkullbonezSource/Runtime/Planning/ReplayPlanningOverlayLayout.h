/*
File: SkullbonezSource/Runtime/Planning/ReplayPlanningOverlayLayout.h
Purpose:
  Declares screen-space layout shared by Planning input and overlay drawing.

Summary:
  Planning owns the intercept readout, trip-planner controls, and porkchop grid.
  Input and rendering consume the same fixed geometry so visible controls and
  hit boxes cannot drift apart.

Glossary:
  Porkchop grid: Fixed launch-delay/time-of-flight matrix whose cells publish
    bounded transfer costs.
  Control surface: Ordered fixed-capacity rows coupling one rectangle to one
    semantic planning action.
  UIRect: Pixel-space rectangle shared by hit testing and drawing.

Invariants:
  - Specific buttons precede the broad panel row so disabled controls still
    block click-through.
  - Porkchop cell mapping remains row-major and matches ReplayPorkchopPanel.

Related:
  - SkullbonezSource/Runtime/Planning/ReplayPlanningOverlayLayout.cpp
  - SkullbonezSource/Runtime/Planning/ReplayTripPlanner.h
  - SkullbonezSource/Runtime/Planning/ReplayPorkchopPanel.h
*/
#pragma once

#include "ReplayPorkchopPanel.h"
#include "ReplayTripPlanner.h"
#include "../UI/RuntimeUiSurface.h"
#include "../../UI/UIDraw.h"

#include <cstddef>
#include <cstdint>

namespace SkullbonezCore::Runtime::ReplayOverlay
{
inline constexpr float REPLAY_INTERCEPT_READOUT_WIDTH = 250.0f;
inline constexpr float REPLAY_INTERCEPT_READOUT_HEIGHT = 28.0f;
inline constexpr float REPLAY_INTERCEPT_READOUT_TOP = 52.0f;
inline constexpr float REPLAY_TRIP_PLANNER_PANEL_WIDTH = 500.0f;
inline constexpr float REPLAY_TRIP_PLANNER_PANEL_HEIGHT = 94.0f;
inline constexpr float REPLAY_TRIP_PLANNER_PANEL_TOP = 88.0f;
inline constexpr float REPLAY_PORKCHOP_PANEL_WIDTH = 720.0f;
inline constexpr float REPLAY_PORKCHOP_PANEL_HEIGHT = 420.0f;
inline constexpr float REPLAY_PORKCHOP_PANEL_TOP = 198.0f;
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

inline RuntimeUiControlId ReplayTripPlannerControlId( ReplayTripPlannerControl control )
{
    return RuntimeUiControlId { static_cast<uint32_t>( control ) };
}

using ReplayTripPlannerSurface = RuntimeUiSurface<6>;

UI::UIRect ReplayInterceptReadoutRect( int screenW );
UI::UIRect ReplayTripPlannerPanelRect( int screenW );
UI::UIRect ReplayPorkchopPanelRect( int screenW );
UI::UIRect ReplayPorkchopGridRect( int screenW );
UI::UIRect ReplayPorkchopCellRect( int screenW, std::size_t cellIndex );
bool ReplayPorkchopCellAtPointer( int screenW, int pointerX, int pointerY, std::size_t& outCellIndex );
void BuildReplayTripPlannerSurface( const ReplayTripPlannerView& planner, int screenW,
                                    ReplayTripPlannerSurface& outSurface );
} // namespace SkullbonezCore::Runtime::ReplayOverlay
