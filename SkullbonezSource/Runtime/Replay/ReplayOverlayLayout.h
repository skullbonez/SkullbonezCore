/*
File: SkullbonezSource/Runtime/Replay/ReplayOverlayLayout.h
Purpose:
  Names replay overlay rectangles and timing constants shared by replay input
  and replay overlay rendering.

Summary:
  Replay input and replay drawing must agree on hit boxes. Keep hierarchy
  geometry, filtered source-index projection, and generic attached-left
  clamping here so the runtime composition root does not own screen-space
  replay placement or filtered evidence.

Glossary:
  Track: Normalized timeline lane, either presentation or solver-backed.
  Hot zone: Bottom-screen hover strip that reveals replay controls.

Invariants:
  - Constants in this file shape both hit testing and rendering.
  - Slider helpers clamp to valid normalized or seconds ranges before callers
    mutate replay state.
  - Attachment extents may constrain the one window anchor but never retain a
    second x/y pair.
  - Filter projection storage is fixed-capacity and contains source indices,
    never duplicated evidence rows.

Related:
  - SkullbonezSource/Runtime/App/ReplayPredictionDrawing.cpp
  - SkullbonezSource/Runtime/Planning/ReplayOverlayRenderer.h
  - Agentic/Reference/engine-glossary.md
*/
#pragma once

#include "ReplayAuthoringPackets.h"
#include "ReplayCapturePackets.h"
#include "ReplayOverlaySurface.h"
#include "ReplayTimelinePackets.h"
#include "../../UI/UIDraw.h"

#include <array>
#include <cstddef>
#include <cstdint>

namespace SkullbonezCore::Runtime::ReplayOverlay
{
enum class ReplayOverlayControlKind : uint8_t
{
    Panel,
    HotZone,
    Button,
    Toggle,
    Slider,
    Track,
    ToolHandle
};

struct ReplayOverlayControlId
{
    uint32_t value = 0;

    explicit operator bool() const noexcept
    {
        return value != 0;
    }
};

inline bool operator==( ReplayOverlayControlId left, ReplayOverlayControlId right ) noexcept
{
    return left.value == right.value;
}

struct ReplayOverlayControl
{
    ReplayOverlayControlId id;
    ReplayOverlayControlKind kind = ReplayOverlayControlKind::Panel;
    uint32_t action = 0;
    UI::UIRect drawRect;
    UI::UIRect hitRect;
    bool visible = true;
    bool enabled = true;
    bool hovered = false;
    bool focused = false;
    bool active = false;
    bool checked = false;
    bool requestsReveal = false;
};

// Replay has two overlay surfaces but one fixed maximum. The packet is rebuilt
// every frame and carries no UI owner or callback.
struct ReplayOverlaySurface
{
    static constexpr std::size_t CONTROL_CAPACITY = 13;
    std::array<ReplayOverlayControl, CONTROL_CAPACITY> controls = {};
    std::size_t controlCount = 0;
    ReplayOverlayControlId hotControl;
    ReplayOverlayControlId pointerControl;
    ReplayOverlayControlId activeControl;
    bool hasHotControl = false;
    bool hasPointerControl = false;
    bool hasActiveControl = false;
    bool consumesPointer = false;

    void Reset() noexcept;
    bool TryAdd( const ReplayOverlayControl& control ) noexcept;
    ReplayOverlayControl* Find( ReplayOverlayControlId id ) noexcept;
    const ReplayOverlayControl* Find( ReplayOverlayControlId id ) const noexcept;
    void ResolvePointer( int pointerX, int pointerY, bool pointerBlocked = false ) noexcept;
};

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
inline constexpr float REPLAY_SCRUBBER_HIGH_DETAIL_TOGGLE_WIDTH = 58.0f;
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
inline constexpr float REPLAY_CAUSE_WINDOW_FILTER_HEIGHT = 70.0f;
inline constexpr float REPLAY_CAUSE_WINDOW_FOOTER_HEIGHT = 22.0f;
inline constexpr float REPLAY_CAUSE_WINDOW_ROW_HEIGHT = 38.0f;
inline constexpr float REPLAY_CAUSE_WINDOW_PADDING = 12.0f;
inline constexpr float REPLAY_CAUSE_WINDOW_RESIZE_SIZE = 18.0f;
inline constexpr int REPLAY_CAUSE_WINDOW_MIN_W = 320;
inline constexpr int REPLAY_CAUSE_WINDOW_MIN_H = 180;

enum class ReplayScrubberControl : uint32_t
{
    None,
    Branch,
    HighDetail,
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

inline ReplayOverlayControlId ReplayScrubberControlId( ReplayScrubberControl control )
{
    return ReplayOverlayControlId { static_cast<uint32_t>( control ) };
}

struct ReplayScrubberSurfaceInput
{
    int screenW = 1;
    int screenH = 1;
    RunReplayTrack track = RunReplayTrack::Solver;
    ReplayToolGestureKind gesture = ReplayToolGestureKind::None;
    bool loadedPresentation = false;
    bool solverToolsEnabled = false;
    bool predictionEnabled = false;
    bool predictionHighDetail = true;
    bool predictionToolsEnabled = false;
    bool pastPathToolsEnabled = false;
    bool branchTargetAvailable = false;
    bool scrubTrackDragEnabled = false;
    bool hotZoneEnabled = true;
};

using ReplayScrubberSurface = ReplayOverlaySurface;

enum class ReplayCauseWindowControl : uint32_t
{
    None,
    Resize,
    Title,
    FilterField,
    FilterFunnel,
    FilterAll,
    FilterPrediction,
    FilterContacts,
    Content,
    Panel
};

inline ReplayOverlayControlId ReplayCauseWindowControlId( ReplayCauseWindowControl control )
{
    return ReplayOverlayControlId { static_cast<uint32_t>( control ) };
}

using ReplayCauseWindowSurface = ReplayOverlaySurface;

struct ReplayCauseWindowProjection
{
    static constexpr std::size_t WORD_COUNT = ( REPLAY_CAUSE_TREE_ROW_CAPACITY + 63u ) / 64u;
    std::array<uint64_t, WORD_COUNT> included = {};
    int sourceCount = 0;
    int count = 0;

    int SourceRow( int visibleRow ) const noexcept;
    int VisibleRow( int sourceRow ) const noexcept;
};

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
UI::UIRect ReplayCauseWindowFilterFieldRect( const RunReplayCauseTreeState& state );
UI::UIRect ReplayCauseWindowFilterFunnelRect( const RunReplayCauseTreeState& state );
UI::UIRect ReplayCauseWindowFilterChipRect( const RunReplayCauseTreeState& state, RunReplayCauseTreeFilter filter );
UI::UIRect ReplayCauseWindowContentRect( const RunReplayCauseTreeState& state );
UI::UIRect ReplayCauseWindowResizeRect( const RunReplayCauseTreeState& state );

// Includes the panel edges so input ownership matches RuntimeUiSurface hit tests.
bool ReplayCauseWindowContainsPoint( const RunReplayCauseTreeState& state, int x, int y );
float ReplayCauseWindowContentHeight( const RunReplayCauseTreeState& state );
float ReplayCauseWindowMaxScroll( const RunReplayCauseTreeState& state );
void BuildReplayCauseWindowProjection( const RunReplayCauseTreeState& state,
                                       ReplayCauseWindowProjection& outProjection ) noexcept;
bool AppendReplayCauseFilterCharacter( RunReplayCauseTreeState& state, char value ) noexcept;
bool BackspaceReplayCauseFilter( RunReplayCauseTreeState& state ) noexcept;
bool ClearReplayCauseFilterText( RunReplayCauseTreeState& state ) noexcept;

// Returns the width that an attachment on the window's left can retain after
// preserving the hierarchy's minimum width and the viewport's outer margins.
// The attachment owns no placement; callers project it from the hierarchy
// anchor and pass the same desired/minimum widths to clamping and drawing.
float ReplayCauseWindowAttachedWidth( const RunReplayCauseTreeState& state, int screenW, float desiredWidth,
                                      float minimumWidth );
void ClampReplayCauseWindow( RunReplayCauseTreeState& state, int screenW, int screenH, float desiredAttachedLeftWidth = 0.0f,
                             float minimumAttachedLeftWidth = 0.0f );
void EnsureReplayCauseWindowPlacement( RunReplayCauseTreeState& state, int screenW, int screenH,
                                       float desiredAttachedLeftWidth = 0.0f, float minimumAttachedLeftWidth = 0.0f );
void MoveReplayCauseWindow( RunReplayCauseTreeState& state, int mouseX, int mouseY, int screenW, int screenH,
                            float desiredAttachedLeftWidth = 0.0f, float minimumAttachedLeftWidth = 0.0f );
void ResizeReplayCauseWindow( RunReplayCauseTreeState& state, int mouseX, int mouseY, int screenW, int screenH,
                              float desiredAttachedLeftWidth = 0.0f, float minimumAttachedLeftWidth = 0.0f );
float ReplayScrubberPositionFromMouse( int mouseX, int screenW, int screenH, RunReplayTrack trackName );
} // namespace SkullbonezCore::Runtime::ReplayOverlay
