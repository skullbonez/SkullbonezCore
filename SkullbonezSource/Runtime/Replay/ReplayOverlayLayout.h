/*
File: SkullbonezSource/Runtime/Replay/ReplayOverlayLayout.h
Purpose:
  Names replay overlay rectangles and timing constants shared by replay input
  and replay overlay rendering.

Mental model:
  Replay input and replay drawing must agree on hit boxes. Keep geometry here
  so the runtime composition root does not own screen-space replay layout.

Related:
  - SkullbonezSource/Runtime/Replay/RunReplayTools.cpp
  - SkullbonezSource/Runtime/Replay/ReplayOverlayRenderer.h
*/
#pragma once

#include "ReplayRuntime.h"
#include "../../UI/UIDraw.h"

namespace SkullbonezCore::Basics::ReplayOverlay
{
inline constexpr float REPLAY_SCRUBBER_HOT_ZONE_HEIGHT =
    78.0f; // Bottom-screen hover strip that reveals replay controls.
inline constexpr float REPLAY_SCRUBBER_PANEL_HEIGHT = 50.0f;
inline constexpr float REPLAY_SCRUBBER_PANEL_MAX_WIDTH = 1080.0f;
inline constexpr float REPLAY_SCRUBBER_PANEL_MARGIN = 18.0f;
inline constexpr float REPLAY_SCRUBBER_TRACK_HEIGHT = 8.0f;
inline constexpr float REPLAY_SCRUBBER_SAVE_BUTTON_SIZE = 22.0f;
inline constexpr float REPLAY_SCRUBBER_LOAD_BUTTON_WIDTH = 48.0f;
inline constexpr float REPLAY_SCRUBBER_SAVE_BUTTON_GAP = 10.0f;
inline constexpr float REPLAY_SCRUBBER_RIGHT_CONTROL_WIDTH = 630.0f;
inline constexpr float REPLAY_SCRUBBER_BRANCH_BUTTON_WIDTH = 74.0f;
inline constexpr float REPLAY_SCRUBBER_PAUSE_BUTTON_WIDTH = 58.0f;
inline constexpr float REPLAY_SCRUBBER_VELOCITY_BUTTON_WIDTH = 86.0f;
inline constexpr float REPLAY_SCRUBBER_PREDICT_TOGGLE_WIDTH = 104.0f;
inline constexpr float REPLAY_SCRUBBER_PREDICT_SLOT_WIDTH = 140.0f;
inline constexpr float REPLAY_SCRUBBER_RAGDOLL_TOGGLE_WIDTH = 78.0f;
inline constexpr double REPLAY_SCRUBBER_VISIBLE_SECONDS = 1.40;
inline constexpr float REPLAY_PREDICTION_MIN_SECONDS = 1.0f;
inline constexpr float REPLAY_PREDICTION_MAX_SECONDS = REPLAY_FUTURE_BUFFER_SECONDS;
inline constexpr float REPLAY_CAUSE_TREE_PANEL_WIDTH = 312.0f;
inline constexpr float REPLAY_CAUSE_TREE_PANEL_MARGIN = 18.0f;
inline constexpr float REPLAY_CAUSE_TREE_PANEL_TOP = 84.0f;
inline constexpr float REPLAY_CAUSE_TREE_ROW_HEIGHT = 22.0f;
inline constexpr float REPLAY_CAUSE_TREE_HEADER_HEIGHT = 42.0f;
inline constexpr float REPLAY_CAUSE_WINDOW_TITLE_HEIGHT = 38.0f;
inline constexpr float REPLAY_CAUSE_WINDOW_ROW_HEIGHT = 38.0f;
inline constexpr float REPLAY_CAUSE_WINDOW_PADDING = 12.0f;
inline constexpr float REPLAY_CAUSE_WINDOW_RESIZE_SIZE = 18.0f;
inline constexpr int REPLAY_CAUSE_WINDOW_MIN_W = 320;
inline constexpr int REPLAY_CAUSE_WINDOW_MIN_H = 180;

UI::UIRect ReplayScrubberPanelRect( int screenW, int screenH );
float ReplayScrubberRowCenterY( const UI::UIRect& panel, RunReplayTrack track );
UI::UIRect ReplayScrubberTrackRect( int screenW, int screenH, RunReplayTrack track );
UI::UIRect ReplayScrubberSaveButtonRect( int screenW, int screenH, RunReplayTrack trackName );
UI::UIRect ReplayScrubberLoadButtonRect( int screenW, int screenH, RunReplayTrack trackName );
UI::UIRect ReplayScrubberBranchButtonRect( int screenW, int screenH );
UI::UIRect ReplayScrubberPauseButtonRect( int screenW, int screenH );
UI::UIRect ReplayScrubberVelocityEditToggleRect( int screenW, int screenH );
UI::UIRect ReplayScrubberPredictControlRect( int screenW, int screenH );
UI::UIRect ReplayScrubberPredictToggleRect( int screenW, int screenH );
UI::UIRect ReplayScrubberPredictHorizonRect( int screenW, int screenH );
UI::UIRect ReplayScrubberRagdollVisualToggleRect( int screenW, int screenH );
float ReplayPredictionHorizonT( float seconds );
float ReplayPredictionHorizonFromMouse( int mouseX, const UI::UIRect& horizon );
UI::UIRect ReplayScrubberHotZoneRect( int screenW, int screenH );
UI::UIRect ReplayCauseTreePanelRect( int screenW, int screenH );
UI::UIRect ReplayCauseTreeRowRect( const UI::UIRect& panel, int visibleRow );
int ReplayCauseTreeVisibleRowCapacity( const UI::UIRect& panel );
UI::UIRect ReplayCauseWindowRect( const RunReplayCauseTreeState& state );
UI::UIRect ReplayCauseWindowTitleRect( const RunReplayCauseTreeState& state );
UI::UIRect ReplayCauseWindowContentRect( const RunReplayCauseTreeState& state );
UI::UIRect ReplayCauseWindowResizeRect( const RunReplayCauseTreeState& state );
float ReplayCauseWindowContentHeight( const RunReplayCauseTreeState& state );
float ReplayCauseWindowMaxScroll( const RunReplayCauseTreeState& state );
void ClampReplayCauseWindow( RunReplayCauseTreeState& state, int screenW, int screenH );
void EnsureReplayCauseWindowPlacement( RunReplayCauseTreeState& state, int screenW, int screenH );
float ReplayScrubberPositionFromMouse( int mouseX, int screenW, int screenH, RunReplayTrack trackName );
} // namespace SkullbonezCore::Basics::ReplayOverlay
