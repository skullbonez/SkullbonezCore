/*
File: SkullbonezSource/Runtime/Replay/ReplayOverlayLayout.h
Purpose:
  Names replay overlay rectangles and timing constants shared by replay input
  and replay overlay rendering.

Summary:
  Replay input and replay drawing must agree on hit boxes. Keep geometry here
  so the runtime composition root does not own screen-space replay layout.

Glossary:
  Track: Normalized timeline lane, either presentation or solver-backed.
  Hot zone: Bottom-screen hover strip that reveals replay controls.

Invariants:
  - Constants in this file shape both hit testing and rendering.
  - Slider helpers clamp to valid normalized or seconds ranges before callers
    mutate replay state.

Related:
  - SkullbonezSource/Runtime/Prediction/ReplayPredictionDrawing.cpp
  - SkullbonezSource/Runtime/Planning/ReplayOverlayRenderer.h
  - Agentic/Reference/engine-glossary.md
*/
#pragma once

#include "ReplayAuthoring.h"
#include "ReplayOverlaySurface.h"
#include "ReplayRecorder.h"
#include "ReplayScrubber.h"
#include "../Interaction/RuntimeInteractionController.h"
#include "../UI/RuntimeUiSurface.h"
#include "../../UI/UIDraw.h"

namespace SkullbonezCore::Runtime::ReplayOverlay
{
inline constexpr float REPLAY_SCRUBBER_HOT_ZONE_HEIGHT = 78.0f; // Bottom-screen hover strip that reveals replay controls.
inline constexpr float REPLAY_SCRUBBER_PANEL_HEIGHT = 50.0f;
inline constexpr float REPLAY_SCRUBBER_PANEL_MAX_WIDTH = 1080.0f;
inline constexpr float REPLAY_SCRUBBER_PANEL_MARGIN = 18.0f;
inline constexpr float REPLAY_SCRUBBER_TRACK_HEIGHT = 8.0f;
inline constexpr float REPLAY_SCRUBBER_SAVE_BUTTON_SIZE = 22.0f;
inline constexpr float REPLAY_SCRUBBER_LOAD_BUTTON_WIDTH = 48.0f;
inline constexpr float REPLAY_SCRUBBER_SAVE_BUTTON_GAP = 10.0f;
inline constexpr float REPLAY_SCRUBBER_RIGHT_CONTROL_WIDTH = 708.0f;
inline constexpr float REPLAY_SCRUBBER_BRANCH_BUTTON_WIDTH = 74.0f;
inline constexpr float REPLAY_SCRUBBER_PAUSE_BUTTON_WIDTH = 58.0f;
inline constexpr float REPLAY_SCRUBBER_VELOCITY_BUTTON_WIDTH = 86.0f;
inline constexpr float REPLAY_SCRUBBER_PREDICT_TOGGLE_WIDTH = 104.0f;
inline constexpr float REPLAY_SCRUBBER_PREDICT_SLOT_WIDTH = 140.0f;
inline constexpr float REPLAY_SCRUBBER_RAGDOLL_TOGGLE_WIDTH = 78.0f;
inline constexpr float REPLAY_SCRUBBER_PAST_TOGGLE_WIDTH = 68.0f;
inline constexpr double REPLAY_SCRUBBER_FADE_IN_SECONDS = 0.18;
inline constexpr double REPLAY_SCRUBBER_FADE_OUT_SECONDS = 0.24;
inline constexpr float REPLAY_SCRUBBER_FADE_EPSILON = 0.015f;
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

enum class ReplayScrubberControl : uint32_t
{
    None,
    Branch,
    Pause,
    VelocityEdit,
    PredictionToggle,
    PredictionHorizon,
    RagdollVisuals,
    PastPath,
    Save,
    Load,
    ScrubTrack,
    PredictionPanel,
    Panel,
    HotZone
};

inline RuntimeUiControlId ReplayScrubberControlId( ReplayScrubberControl control )
{
    return RuntimeUiControlId { static_cast<uint32_t>( control ) };
}

struct ReplayScrubberSurfaceInput
{
    int screenW = 1;
    int screenH = 1;
    RunReplayTrack track = RunReplayTrack::Solver;
    RuntimeInteractionGestureKind gesture = RuntimeInteractionGestureKind::None;
    bool loadedPresentation = false;
    bool solverToolsEnabled = false;
    bool predictionEnabled = false;
    bool predictionToolsEnabled = false;
    bool pastPathToolsEnabled = false;
    bool branchTargetAvailable = false;
    bool scrubTrackDragEnabled = false;
    bool hotZoneEnabled = true;
};

using ReplayScrubberSurface = RuntimeUiSurface<13>;

enum class ReplayCauseWindowControl : uint32_t
{
    None,
    Resize,
    Title,
    Content,
    Panel
};

inline RuntimeUiControlId ReplayCauseWindowControlId( ReplayCauseWindowControl control )
{
    return RuntimeUiControlId { static_cast<uint32_t>( control ) };
}

using ReplayCauseWindowSurface = RuntimeUiSurface<4>;

// Derives track/tool availability from replay state. Callers then add their
// one-frame screen, gesture, and pointer-blocking facts before surface layout.
ReplayScrubberSurfaceInput DescribeReplayScrubberAvailability( const ReplayScrubberView& scrubber,
                                                               const ReplayRecorderStats& solverStats,
                                                               bool loadedPresentation, bool pathTargetAvailable,
                                                               bool predictionTimelineAvailable,
                                                               bool currentPresentationAvailable,
                                                               bool currentSolverAvailable, bool scenePhysicsEnabled );
void BuildReplayScrubberSurface( const ReplayScrubberSurfaceInput& input, ReplayScrubberSurface& outSurface );
void BuildReplayCauseWindowSurface( const RunReplayCauseTreeState& state, ReplayCauseWindowSurface& outSurface );

UI::UIRect ReplayScrubberPanelRect( int screenW, int screenH );
float ReplayScrubberRowCenterY( const UI::UIRect& panel, RunReplayTrack track );
UI::UIRect ReplayScrubberSaveButtonRect( int screenW, int screenH, RunReplayTrack trackName );
UI::UIRect ReplayScrubberLoadButtonRect( int screenW, int screenH, RunReplayTrack trackName );
UI::UIRect ReplayScrubberPredictControlRect( int screenW, int screenH );
UI::UIRect ReplayScrubberPredictHorizonRect( int screenW, int screenH );
UI::UIRect ReplayScrubberRagdollVisualToggleRect( int screenW, int screenH );
float ReplayPredictionHorizonT( float seconds );
float ReplayPredictionHorizonFromMouse( int mouseX, const UI::UIRect& horizon );
UI::UIRect ReplayScrubberHotZoneRect( int screenW, int screenH );
UI::UIRect ReplayCauseWindowRect( const RunReplayCauseTreeState& state );
UI::UIRect ReplayCauseWindowTitleRect( const RunReplayCauseTreeState& state );
UI::UIRect ReplayCauseWindowContentRect( const RunReplayCauseTreeState& state );
UI::UIRect ReplayCauseWindowResizeRect( const RunReplayCauseTreeState& state );

// Includes the panel edges so input ownership matches RuntimeUiSurface hit tests.
bool ReplayCauseWindowContainsPoint( const RunReplayCauseTreeState& state, int x, int y );
float ReplayCauseWindowContentHeight( const RunReplayCauseTreeState& state );
float ReplayCauseWindowMaxScroll( const RunReplayCauseTreeState& state );
void ClampReplayCauseWindow( RunReplayCauseTreeState& state, int screenW, int screenH );
void EnsureReplayCauseWindowPlacement( RunReplayCauseTreeState& state, int screenW, int screenH );
float ReplayScrubberPositionFromMouse( int mouseX, int screenW, int screenH, RunReplayTrack trackName );
} // namespace SkullbonezCore::Runtime::ReplayOverlay
