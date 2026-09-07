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
#include <cmath>

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

inline constexpr std::array<ReplayOverlaySurfaceKind, 5> REPLAY_OVERLAY_COMPOSITION_ORDER =
    { ReplayOverlaySurfaceKind::Intercept, ReplayOverlaySurfaceKind::TripPlanner, ReplayOverlaySurfaceKind::Porkchop,
      ReplayOverlaySurfaceKind::CauseTree, ReplayOverlaySurfaceKind::Scrubber };

inline constexpr bool ShouldComposeReplayOverlay( bool gameUiSurfaceActive ) noexcept
{
    return gameUiSurfaceActive;
}

// Planning owns this transient screen-space cue. Its identity and frame come
// from the displayed body sample; no trajectory publication or GPU cache changes.
struct ReplayPositionGate
{
    Physics::PhysicsSceneObjectId id;
    ReplayFrameIndex frame = 0;
    UI::UIPoint center;
    UI::UIPoint tangent = { 1.0f, 0.0f };
    bool visible = false;
};

inline bool ProjectReplayGatePoint( const Math::Vector::Vector3& position, const ReplayOverlayViewport& viewport,
                                    UI::UIPoint& screen ) noexcept
{
    const float* matrix = viewport.viewProjection.Data();
    const float x = matrix[0] * position.x + matrix[4] * position.y + matrix[8] * position.z + matrix[12];
    const float y = matrix[1] * position.x + matrix[5] * position.y + matrix[9] * position.z + matrix[13];
    const float z = matrix[2] * position.x + matrix[6] * position.y + matrix[10] * position.z + matrix[14];
    const float w = matrix[3] * position.x + matrix[7] * position.y + matrix[11] * position.z + matrix[15];

    // Hazard: dividing a point behind the eye or near plane can place a false
    // gate over an unrelated visible object. DX12 clip depth is [0,w].
    if ( viewport.width <= 0 || viewport.height <= 0 || !std::isfinite( x ) || !std::isfinite( y ) || !std::isfinite( z ) ||
         !std::isfinite( w ) || w <= 0.0001f || z < 0.0f || z > w )
    {
        return false;
    }

    screen = { ( x / w * 0.5f + 0.5f ) * viewport.width, ( 0.5f - y / w * 0.5f ) * viewport.height };
    return std::isfinite( screen.x ) && std::isfinite( screen.y );
}

inline ReplayPositionGate BuildReplayPositionGate( const RunReplayPredictionFrame& frame,
                                                   Physics::PhysicsSceneObjectId selectedId,
                                                   const ReplayOverlayViewport& viewport ) noexcept
{
    ReplayPositionGate gate;

    if ( selectedId.value == 0 )
    {
        return gate;
    }

    for ( const RunReplayPredictionBodySample& body : frame.bodies )
    {
        if ( body.id != selectedId )
        {
            continue;
        }

        gate.id = body.id;
        gate.frame = frame.frameIndex;
        gate.visible = ProjectReplayGatePoint( body.position, viewport, gate.center ) && gate.center.x >= 0.0f &&
                       gate.center.x <= viewport.width && gate.center.y >= 0.0f && gate.center.y <= viewport.height;
        UI::UIPoint ahead;

        if ( gate.visible && ProjectReplayGatePoint( body.position + body.linearVelocity * 0.01f, viewport, ahead ) )
        {
            const float dx = ahead.x - gate.center.x;
            const float dy = ahead.y - gate.center.y;
            const float length = std::sqrt( dx * dx + dy * dy );

            // A stationary body or an end-on path keeps a stable horizontal gate.
            if ( std::isfinite( length ) && length > 0.001f )
            {
                gate.tangent = { dx / length, dy / length };
            }
        }

        return gate;
    }

    return gate;
}

inline std::array<Physics::PhysicsSceneObjectId, 2>
ReplayPositionGateSelection( const ReplayOverlayCausalityView& causality, Physics::PhysicsSceneObjectId pathTarget ) noexcept
{
    const ReplayCauseInspectionMode mode = causality.inspection.Transport().mode;
    const int row = causality.inspection.Selection().selectedRow;

    if ( mode == ReplayCauseInspectionMode::Inactive )
    {
        return { pathTarget, Physics::PhysicsSceneObjectId {} };
    }

    if ( row < 0 || static_cast<std::size_t>( row ) >= causality.tree.rows.size() )
    {
        return {};
    }

    const RunReplayCauseTreeRow& selected = causality.tree.rows[static_cast<std::size_t>( row )];
    return { selected.id,
             selected.counterpartId == selected.id ? Physics::PhysicsSceneObjectId {} : selected.counterpartId };
}

class ReplayOverlayDrawOwner
{
  public:
    const UI::UIDrawList& Compose( const ReplayOverlayStateView& replay, bool gameUiSurfaceActive, bool scenePhysicsEnabled,
                                   ReplayOverlayGestureView gesture, ReplayOverlayViewport viewport, double nowSeconds );

  private:
    // Lifetime: retained Planning scratch avoids placing the fixed-capacity UI
    // command storage on nested frame stacks. Render only borrows it during one
    // synchronous App-sequenced submission.
    UI::UIDrawList m_drawList;
};
} // namespace SkullbonezCore::Runtime::ReplayOverlay
