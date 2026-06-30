/*
File: SkullbonezSource/Runtime/Replay/ReplayOverlayLayout.cpp
Purpose:
  Implements replay overlay screen-space geometry shared by input and drawing.

Mental model:
  Replay layout is a replay subsystem concern. Input hit boxes and drawn
  controls should stay mechanically identical by using the same helpers.

Glossary:
  Scrubber: Bottom-screen replay timeline control used for save/load, pause,
    branch, prediction, and velocity-edit actions.
  Cause window: Movable replay inspection panel that explains selected contact
    and solver relationships.
  UIRect: Pixel-space rectangle shared by hit testing and drawing.

Invariants:
  - Input and rendering must call these helpers for the same rectangles.
  - Clamp movable overlay windows before drawing or hit testing them.

Related:
  - SkullbonezSource/Runtime/Replay/ReplayOverlayLayout.h
*/
#include "ReplayOverlayLayout.h"

#include <algorithm>
#include <cmath>

namespace SkullbonezCore::Basics::ReplayOverlay
{
// Concept: replay overlay geometry is the contract between input and drawing.
//
// A replay control is only usable if the mouse hit box and drawn pixels agree.
// Keep derived rectangles here instead of duplicating layout math in
// RunReplayTools and ReplayOverlayRenderer.
UI::UIRect ReplayScrubberPanelRect( int screenW, int screenH )
{
    const float width =
        (std::min)( REPLAY_SCRUBBER_PANEL_MAX_WIDTH,
                    (std::max)( 260.0f, static_cast<float>( screenW ) - REPLAY_SCRUBBER_PANEL_MARGIN * 2.0f ) );
    const float x = ( static_cast<float>( screenW ) - width ) * 0.5f;
    const float y =
        (std::max)( 0.0f, static_cast<float>( screenH ) - REPLAY_SCRUBBER_PANEL_HEIGHT - REPLAY_SCRUBBER_PANEL_MARGIN );
    return { x, y, width, REPLAY_SCRUBBER_PANEL_HEIGHT };
}

float ReplayScrubberRowCenterY( const UI::UIRect& panel, RunReplayTrack track )
{
    (void)track;
    return panel.y + panel.h * 0.5f;
}

UI::UIRect ReplayScrubberTrackRect( int screenW, int screenH, RunReplayTrack track )
{
    const UI::UIRect panel = ReplayScrubberPanelRect( screenW, screenH );
    constexpr float leftInset = 70.0f + REPLAY_SCRUBBER_SAVE_BUTTON_SIZE + REPLAY_SCRUBBER_SAVE_BUTTON_GAP +
                                REPLAY_SCRUBBER_LOAD_BUTTON_WIDTH + REPLAY_SCRUBBER_SAVE_BUTTON_GAP;
    constexpr float rightInset = 10.0f + REPLAY_SCRUBBER_RIGHT_CONTROL_WIDTH;
    return { panel.x + leftInset,
             ReplayScrubberRowCenterY( panel, track ) - REPLAY_SCRUBBER_TRACK_HEIGHT * 0.5f,
             (std::max)( 80.0f, panel.w - leftInset - rightInset ),
             REPLAY_SCRUBBER_TRACK_HEIGHT };
}

UI::UIRect ReplayScrubberSaveButtonRect( int screenW, int screenH, RunReplayTrack trackName )
{
    const UI::UIRect panel = ReplayScrubberPanelRect( screenW, screenH );
    const UI::UIRect track = ReplayScrubberTrackRect( screenW, screenH, trackName );
    return { panel.x + 70.0f,
             track.y - ( REPLAY_SCRUBBER_SAVE_BUTTON_SIZE - track.h ) * 0.5f,
             REPLAY_SCRUBBER_SAVE_BUTTON_SIZE,
             REPLAY_SCRUBBER_SAVE_BUTTON_SIZE };
}

UI::UIRect ReplayScrubberLoadButtonRect( int screenW, int screenH, RunReplayTrack trackName )
{
    const UI::UIRect save = ReplayScrubberSaveButtonRect( screenW, screenH, trackName );
    return { save.x + save.w + REPLAY_SCRUBBER_SAVE_BUTTON_GAP, save.y, REPLAY_SCRUBBER_LOAD_BUTTON_WIDTH, save.h };
}

UI::UIRect ReplayScrubberBranchButtonRect( int screenW, int screenH )
{
    const UI::UIRect panel = ReplayScrubberPanelRect( screenW, screenH );
    return { panel.x + panel.w - REPLAY_SCRUBBER_RIGHT_CONTROL_WIDTH,
             panel.y + 14.0f,
             REPLAY_SCRUBBER_BRANCH_BUTTON_WIDTH,
             22.0f };
}

UI::UIRect ReplayScrubberPauseButtonRect( int screenW, int screenH )
{
    const UI::UIRect branch = ReplayScrubberBranchButtonRect( screenW, screenH );
    return { branch.x + branch.w + 10.0f, branch.y, REPLAY_SCRUBBER_PAUSE_BUTTON_WIDTH, 22.0f };
}

UI::UIRect ReplayScrubberVelocityEditToggleRect( int screenW, int screenH )
{
    const UI::UIRect pause = ReplayScrubberPauseButtonRect( screenW, screenH );
    return { pause.x + pause.w + 10.0f, pause.y, REPLAY_SCRUBBER_VELOCITY_BUTTON_WIDTH, pause.h };
}

UI::UIRect ReplayScrubberPredictControlRect( int screenW, int screenH )
{
    const UI::UIRect velocity = ReplayScrubberVelocityEditToggleRect( screenW, screenH );
    return { velocity.x + velocity.w + 10.0f + REPLAY_SCRUBBER_PREDICT_TOGGLE_WIDTH + 8.0f,
             velocity.y,
             REPLAY_SCRUBBER_PREDICT_SLOT_WIDTH,
             velocity.h };
}

UI::UIRect ReplayScrubberPredictToggleRect( int screenW, int screenH )
{
    const UI::UIRect velocity = ReplayScrubberVelocityEditToggleRect( screenW, screenH );
    return { velocity.x + velocity.w + 10.0f, velocity.y, REPLAY_SCRUBBER_PREDICT_TOGGLE_WIDTH, velocity.h };
}

UI::UIRect ReplayScrubberPredictHorizonRect( int screenW, int screenH )
{
    const UI::UIRect control = ReplayScrubberPredictControlRect( screenW, screenH );
    return { control.x + 8.0f, control.y + 7.0f, (std::max)( 40.0f, control.w - 44.0f ), 8.0f };
}

UI::UIRect ReplayScrubberRagdollVisualToggleRect( int screenW, int screenH )
{
    const UI::UIRect predict = ReplayScrubberPredictControlRect( screenW, screenH );
    return { predict.x + predict.w + 8.0f, predict.y, REPLAY_SCRUBBER_RAGDOLL_TOGGLE_WIDTH, predict.h };
}

float ReplayPredictionHorizonT( float seconds )
{
    return std::clamp(
        ( seconds - REPLAY_PREDICTION_MIN_SECONDS ) / ( REPLAY_PREDICTION_MAX_SECONDS - REPLAY_PREDICTION_MIN_SECONDS ),
        0.0f,
        1.0f );
}

float ReplayPredictionHorizonFromMouse( int mouseX, const UI::UIRect& horizon )
{
    // Why: prediction seconds use a normalized slider, but the user drags in
    // pixels. Clamp before scaling so mouse drift outside the slot cannot
    // create an invalid future horizon.
    const float t =
        horizon.w > 1.0f ? std::clamp( ( static_cast<float>( mouseX ) - horizon.x ) / horizon.w, 0.0f, 1.0f ) : 1.0f;
    const float seconds =
        REPLAY_PREDICTION_MIN_SECONDS + t * ( REPLAY_PREDICTION_MAX_SECONDS - REPLAY_PREDICTION_MIN_SECONDS );
    return std::clamp( std::round( seconds ), REPLAY_PREDICTION_MIN_SECONDS, REPLAY_PREDICTION_MAX_SECONDS );
}

UI::UIRect ReplayScrubberHotZoneRect( int screenW, int screenH )
{
    // Why: the bottom-left minimized/options UI is also a click target. Only
    // the centered two-thirds of the bottom edge should summon replay controls.
    const float width = static_cast<float>( screenW ) * ( 2.0f / 3.0f );
    const float x = ( static_cast<float>( screenW ) - width ) * 0.5f;
    return { x,
             (std::max)( 0.0f, static_cast<float>( screenH ) - REPLAY_SCRUBBER_HOT_ZONE_HEIGHT ),
             width,
             REPLAY_SCRUBBER_HOT_ZONE_HEIGHT };
}

UI::UIRect ReplayCauseTreePanelRect( int screenW, int screenH )
{
    const UI::UIRect scrubber = ReplayScrubberPanelRect( screenW, screenH );
    const float width =
        (std::min)( REPLAY_CAUSE_TREE_PANEL_WIDTH,
                    (std::max)( 220.0f, static_cast<float>( screenW ) - REPLAY_CAUSE_TREE_PANEL_MARGIN * 2.0f ) );
    const float x = (std::max)( REPLAY_CAUSE_TREE_PANEL_MARGIN,
                                static_cast<float>( screenW ) - width - REPLAY_CAUSE_TREE_PANEL_MARGIN );
    const float y = REPLAY_CAUSE_TREE_PANEL_TOP;
    const float maxHeight = (std::max)( 120.0f, scrubber.y - y - REPLAY_CAUSE_TREE_PANEL_MARGIN );
    const float height = (std::min)( 420.0f, maxHeight );
    return { x, y, width, height };
}

UI::UIRect ReplayCauseTreeRowRect( const UI::UIRect& panel, int visibleRow )
{
    return {
        panel.x + 10.0f,
        panel.y + REPLAY_CAUSE_TREE_HEADER_HEIGHT + static_cast<float>( visibleRow ) * REPLAY_CAUSE_TREE_ROW_HEIGHT,
        panel.w - 20.0f,
        REPLAY_CAUSE_TREE_ROW_HEIGHT - 3.0f };
}

int ReplayCauseTreeVisibleRowCapacity( const UI::UIRect& panel )
{
    return (std::max)( 0,
                       static_cast<int>( ( panel.h - REPLAY_CAUSE_TREE_HEADER_HEIGHT - 10.0f ) /
                                         REPLAY_CAUSE_TREE_ROW_HEIGHT ) );
}

UI::UIRect ReplayCauseWindowRect( const RunReplayCauseTreeState& state )
{
    return { static_cast<float>( state.x ),
             static_cast<float>( state.y ),
             static_cast<float>( state.width ),
             static_cast<float>( state.height ) };
}

UI::UIRect ReplayCauseWindowTitleRect( const RunReplayCauseTreeState& state )
{
    const UI::UIRect panel = ReplayCauseWindowRect( state );
    return { panel.x, panel.y, panel.w, REPLAY_CAUSE_WINDOW_TITLE_HEIGHT };
}

UI::UIRect ReplayCauseWindowContentRect( const RunReplayCauseTreeState& state )
{
    const UI::UIRect panel = ReplayCauseWindowRect( state );
    return { panel.x + REPLAY_CAUSE_WINDOW_PADDING,
             panel.y + REPLAY_CAUSE_WINDOW_TITLE_HEIGHT + 7.0f,
             panel.w - REPLAY_CAUSE_WINDOW_PADDING * 2.0f,
             panel.h - REPLAY_CAUSE_WINDOW_TITLE_HEIGHT - REPLAY_CAUSE_WINDOW_PADDING - 7.0f };
}

UI::UIRect ReplayCauseWindowResizeRect( const RunReplayCauseTreeState& state )
{
    const UI::UIRect panel = ReplayCauseWindowRect( state );
    return { panel.x + panel.w - REPLAY_CAUSE_WINDOW_RESIZE_SIZE,
             panel.y + panel.h - REPLAY_CAUSE_WINDOW_RESIZE_SIZE,
             REPLAY_CAUSE_WINDOW_RESIZE_SIZE,
             REPLAY_CAUSE_WINDOW_RESIZE_SIZE };
}

float ReplayCauseWindowContentHeight( const RunReplayCauseTreeState& state )
{
    return static_cast<float>( state.rows.size() ) * REPLAY_CAUSE_WINDOW_ROW_HEIGHT;
}

float ReplayCauseWindowMaxScroll( const RunReplayCauseTreeState& state )
{
    const UI::UIRect content = ReplayCauseWindowContentRect( state );
    return (std::max)( 0.0f, ReplayCauseWindowContentHeight( state ) - content.h );
}

void ClampReplayCauseWindow( RunReplayCauseTreeState& state, int screenW, int screenH )
{
    // Invariant: the resize handle and title bar must remain reachable after a
    // resolution change, or the inspection window can become permanently
    // off-screen for the session.
    state.width = (std::max)( REPLAY_CAUSE_WINDOW_MIN_W,
                              (std::min)( state.width, (std::max)( REPLAY_CAUSE_WINDOW_MIN_W, screenW - 16 ) ) );
    state.height = (std::max)( REPLAY_CAUSE_WINDOW_MIN_H,
                               (std::min)( state.height, (std::max)( REPLAY_CAUSE_WINDOW_MIN_H, screenH - 16 ) ) );
    state.x = std::clamp( state.x, 8, (std::max)( 8, screenW - state.width - 8 ) );
    state.y = std::clamp( state.y, 8, (std::max)( 8, screenH - state.height - 8 ) );
    state.scrollY = std::clamp( state.scrollY, 0.0f, ReplayCauseWindowMaxScroll( state ) );
}

void EnsureReplayCauseWindowPlacement( RunReplayCauseTreeState& state, int screenW, int screenH )
{
    if ( !state.hasWindowPlacement )
    {
        state.width = (std::min)( 380, (std::max)( REPLAY_CAUSE_WINDOW_MIN_W, screenW - 48 ) );
        state.height = (std::min)( 520, (std::max)( REPLAY_CAUSE_WINDOW_MIN_H, screenH - 160 ) );
        state.x = (std::max)( 12, screenW - state.width - 24 );
        state.y = 72;
        state.hasWindowPlacement = true;
    }
    ClampReplayCauseWindow( state, screenW, screenH );
}

float ReplayScrubberPositionFromMouse( int mouseX, int screenW, int screenH, RunReplayTrack trackName )
{
    const UI::UIRect track = ReplayScrubberTrackRect( screenW, screenH, trackName );
    return track.w > 1.0f ? std::clamp( ( static_cast<float>( mouseX ) - track.x ) / track.w, 0.0f, 1.0f ) : 1.0f;
}
} // namespace SkullbonezCore::Basics::ReplayOverlay
