/*
File: SkullbonezSource/Runtime/Planning/ReplayOverlayRenderer.h
Purpose:
  Declares Planning's retained composer for the GameUI replay overlay.

Summary:
  Planning projects one frame-local ReplayOverlayStateView into a bounded,
  backend-neutral UIDrawList. App sequences that composition before handing the
  immutable list to Runtime/Render for GPU submission.

Invariants:
  - The owner retains only copied UI commands and text, never Replay, Prediction,
    renderer, or backend capabilities.
  - Compose clears the previous frame before appending surfaces in visual order.
  - The returned list remains valid until the next Compose call on this owner.
  - A hidden GameUI surface publishes an empty list.

Related:
  - SkullbonezSource/Runtime/Planning/ReplayOverlayPackets.h
  - SkullbonezSource/Runtime/App/OperatorEditorFramePhase.cpp
  - SkullbonezSource/UI/UIDrawList.h
*/
#pragma once

#include "ReplayOverlayPackets.h"
#include "../../UI/UIDrawList.h"

#include <array>

namespace SkullbonezCore::Runtime::ReplayOverlay
{
enum class ReplayOverlaySurfaceKind : uint8_t
{
    Intercept,
    TripPlanner,
    Porkchop,
    CauseTree,
    Scrubber
};

inline constexpr std::array<ReplayOverlaySurfaceKind, 5> REPLAY_OVERLAY_COMPOSITION_ORDER = {
    ReplayOverlaySurfaceKind::Intercept, ReplayOverlaySurfaceKind::TripPlanner, ReplayOverlaySurfaceKind::Porkchop,
    ReplayOverlaySurfaceKind::CauseTree, ReplayOverlaySurfaceKind::Scrubber
};

inline constexpr bool ShouldComposeReplayOverlay( bool gameUiSurfaceActive ) noexcept
{
    return gameUiSurfaceActive;
}

class ReplayOverlayDrawOwner
{
  public:
    const UI::UIDrawList& Compose( const ReplayOverlayStateView& replay, bool gameUiSurfaceActive,
                                   bool scenePhysicsEnabled, RuntimeInteractionGestureKind gesture,
                                   ReplayOverlayViewport viewport, double nowSeconds );

  private:
    // Lifetime: retained Planning scratch avoids placing the fixed-capacity UI
    // command storage on nested frame stacks. Render only borrows it during one
    // synchronous App-sequenced submission.
    UI::UIDrawList m_drawList;
};
} // namespace SkullbonezCore::Runtime::ReplayOverlay
