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
  Control surface: Ordered per-frame table that binds those rectangles to
    semantic controls and actions without retaining replay state.

Invariants:
  - Input and rendering must call these helpers for the same rectangles.
  - Scrubber controls are published front-to-back so disabled rows block
    click-through and broad panel rows cannot steal specific actions.
  - Clamp movable overlay windows before drawing or hit testing them.

Related:
  - SkullbonezSource/Runtime/Replay/ReplayOverlayLayout.h
*/
#include "ReplayOverlayLayout.h"
#include "../../Core/FatalError.h"

#include <algorithm>
#include <cmath>

namespace SkullbonezCore::Basics::ReplayOverlay
{
namespace
{
// Why: the top-right scene/camera badges own the first screen rows, so the
// draggable cause window starts below them instead of covering status text.
constexpr int REPLAY_CAUSE_WINDOW_SAFE_TOP = 124;

int ReplayCauseWindowMinY( int screenH )
{
    return (std::min)( REPLAY_CAUSE_WINDOW_SAFE_TOP, (std::max)( 8, screenH - REPLAY_CAUSE_WINDOW_MIN_H - 8 ) );
}
} // namespace

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

UI::UIRect ReplayScrubberPastPathToggleRect( int screenW, int screenH )
{
    const UI::UIRect ragdoll = ReplayScrubberRagdollVisualToggleRect( screenW, screenH );
    return { ragdoll.x + ragdoll.w + 8.0f, ragdoll.y, REPLAY_SCRUBBER_PAST_TOGGLE_WIDTH, ragdoll.h };
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

void BuildReplayScrubberSurface( const ReplayScrubberSurfaceInput& input, ReplayScrubberSurface& outSurface )
{
    outSurface.Reset();

    const auto addControl = [&]( ReplayScrubberControl id,
                                 ReplayScrubberAction action,
                                 RuntimeUiControlKind kind,
                                 const UI::UIRect& drawRect,
                                 const UI::UIRect& hitRect,
                                 bool visible,
                                 bool enabled )
    {
        RuntimeUiControl control;
        control.id = ReplayScrubberControlId( id );
        control.action = RuntimeUiActionId{ static_cast<uint32_t>( action ) };
        control.kind = kind;
        control.drawRect = drawRect;
        control.hitRect = hitRect;
        control.visible = visible;
        control.enabled = enabled;
        control.requestsReveal = true;
        if ( !outSurface.TryAdd( control ) )
        {
            // Lane F: a duplicate id or undersized compile-time table makes UI
            // z-order and dispatch ambiguous, so the frame cannot safely continue.
            SB_FATAL( "ReplayScrubberSurface", "Cannot publish replay scrubber control id=%u.", control.id.value );
        }
    };

    const UI::UIRect branch = ReplayScrubberBranchButtonRect( input.screenW, input.screenH );
    const UI::UIRect pause = ReplayScrubberPauseButtonRect( input.screenW, input.screenH );
    const UI::UIRect velocity = ReplayScrubberVelocityEditToggleRect( input.screenW, input.screenH );
    const UI::UIRect predictionToggle = ReplayScrubberPredictToggleRect( input.screenW, input.screenH );
    const UI::UIRect predictionPanel = ReplayScrubberPredictControlRect( input.screenW, input.screenH );
    const UI::UIRect predictionHorizon = ReplayScrubberPredictHorizonRect( input.screenW, input.screenH );
    const UI::UIRect ragdoll = ReplayScrubberRagdollVisualToggleRect( input.screenW, input.screenH );
    const UI::UIRect pastPath = ReplayScrubberPastPathToggleRect( input.screenW, input.screenH );
    const UI::UIRect save = ReplayScrubberSaveButtonRect( input.screenW, input.screenH, input.track );
    const UI::UIRect load = ReplayScrubberLoadButtonRect( input.screenW, input.screenH, input.track );
    const UI::UIRect track = ReplayScrubberTrackRect( input.screenW, input.screenH, input.track );
    const UI::UIRect panel = ReplayScrubberPanelRect( input.screenW, input.screenH );
    const UI::UIRect hotZone = ReplayScrubberHotZoneRect( input.screenW, input.screenH );
    const float horizonHitBottom =
        (std::max)( predictionHorizon.y + predictionHorizon.h, predictionPanel.y + predictionPanel.h );
    const float horizonHitTop = (std::min)( predictionHorizon.y, predictionPanel.y );
    const UI::UIRect predictionHorizonHit = { predictionHorizon.x,
                                              horizonHitTop,
                                              predictionHorizon.w,
                                              horizonHitBottom - horizonHitTop };

    // Invariant: rows are added front-to-back. Disabled controls still block
    // click-through, while broad panel/reveal zones sit behind real controls.
    addControl( ReplayScrubberControl::Branch,
                ReplayScrubberAction::RestoreBranch,
                RuntimeUiControlKind::Button,
                branch,
                branch,
                true,
                input.branchTargetAvailable );
    addControl( ReplayScrubberControl::Pause,
                ReplayScrubberAction::TogglePause,
                RuntimeUiControlKind::Button,
                pause,
                pause,
                !input.loadedPresentation,
                input.solverToolsEnabled );
    addControl( ReplayScrubberControl::VelocityEdit,
                ReplayScrubberAction::ToggleVelocityEdit,
                RuntimeUiControlKind::Toggle,
                velocity,
                velocity,
                !input.loadedPresentation,
                input.solverToolsEnabled );
    addControl( ReplayScrubberControl::PredictionToggle,
                ReplayScrubberAction::TogglePrediction,
                RuntimeUiControlKind::Toggle,
                predictionToggle,
                predictionToggle,
                !input.loadedPresentation,
                input.predictionToolsEnabled );
    addControl( ReplayScrubberControl::PredictionHorizon,
                ReplayScrubberAction::SetPredictionHorizon,
                RuntimeUiControlKind::Slider,
                predictionHorizon,
                predictionHorizonHit,
                !input.loadedPresentation,
                input.predictionToolsEnabled );
    addControl( ReplayScrubberControl::RagdollVisuals,
                ReplayScrubberAction::ToggleRagdollVisuals,
                RuntimeUiControlKind::Toggle,
                ragdoll,
                ragdoll,
                !input.loadedPresentation,
                input.predictionToolsEnabled );
    addControl( ReplayScrubberControl::PastPath,
                ReplayScrubberAction::TogglePastPath,
                RuntimeUiControlKind::Toggle,
                pastPath,
                pastPath,
                !input.loadedPresentation,
                input.pastPathToolsEnabled );
    addControl( ReplayScrubberControl::Save,
                ReplayScrubberAction::Save,
                RuntimeUiControlKind::Button,
                save,
                save,
                true,
                input.solverToolsEnabled );
    addControl( ReplayScrubberControl::Load,
                ReplayScrubberAction::Load,
                RuntimeUiControlKind::Button,
                load,
                load,
                true,
                true );
    addControl( ReplayScrubberControl::ScrubTrack,
                ReplayScrubberAction::Scrub,
                RuntimeUiControlKind::Track,
                track,
                track,
                true,
                input.scrubTrackDragEnabled );
    addControl( ReplayScrubberControl::PredictionPanel,
                ReplayScrubberAction::None,
                RuntimeUiControlKind::Panel,
                predictionPanel,
                predictionPanel,
                !input.loadedPresentation,
                input.predictionToolsEnabled );
    addControl( ReplayScrubberControl::Panel,
                ReplayScrubberAction::None,
                RuntimeUiControlKind::Panel,
                panel,
                panel,
                true,
                true );
    addControl( ReplayScrubberControl::HotZone,
                ReplayScrubberAction::None,
                RuntimeUiControlKind::HotZone,
                hotZone,
                hotZone,
                input.hotZoneEnabled,
                true );

    ReplayScrubberControl active = ReplayScrubberControl::None;
    if ( input.gesture == RuntimeInteractionGestureKind::ReplayScrubDrag )
    {
        active = ReplayScrubberControl::ScrubTrack;
    }
    else if ( input.gesture == RuntimeInteractionGestureKind::ReplayPredictionHorizonDrag )
    {
        active = ReplayScrubberControl::PredictionHorizon;
    }
    if ( active != ReplayScrubberControl::None )
    {
        outSurface.activeControl = ReplayScrubberControlId( active );
        outSurface.hasActiveControl = true;
        if ( RuntimeUiControl* control = outSurface.Find( outSurface.activeControl ) )
        {
            control->active = true;
        }
    }
}

ReplayScrubberSurfaceInput DescribeReplayScrubberSurface( const ReplayRuntime& replayRuntime,
                                                          bool scenePhysicsEnabled,
                                                          bool uiBlocksMouse,
                                                          int screenW,
                                                          int screenH,
                                                          RuntimeInteractionGestureKind gesture )
{
    ReplayScrubberSurfaceInput input;
    input.screenW = screenW;
    input.screenH = screenH;
    input.gesture = gesture;
    input.loadedPresentation = replayRuntime.HasLoadedPresentation();
    input.track = input.loadedPresentation ? RunReplayTrack::Presentation : RunReplayTrack::Solver;
    const ReplayRecorderStats solverStats = replayRuntime.Solver().GetStats();
    const bool solverReplayEnabled = solverStats.enabled;
    const bool solverReplayAvailable = solverReplayEnabled && solverStats.sampleCount >= 2;
    input.solverToolsEnabled = !input.loadedPresentation && solverReplayAvailable;
    input.predictionToolsEnabled = !input.loadedPresentation && solverReplayEnabled && scenePhysicsEnabled;
    input.pastPathToolsEnabled = input.solverToolsEnabled && replayRuntime.PathVisualizer().hasTarget;
    const bool predictionTimelineAvailable =
        input.predictionToolsEnabled && ( replayRuntime.ActivePredictionFrames().size() >= 2 ||
                                          replayRuntime.Prediction().BuildPrefixShouldBePresented() );
    input.scrubTrackDragEnabled = input.loadedPresentation || input.solverToolsEnabled || predictionTimelineAvailable;
    input.branchTargetAvailable =
        replayRuntime.Scrubber().historicalSamplePaused &&
        ( ( input.loadedPresentation && replayRuntime.Scrubber().activeTrack == RunReplayTrack::Presentation &&
            replayRuntime.CurrentScrubSample() != nullptr ) ||
          ( input.solverToolsEnabled && replayRuntime.Scrubber().activeTrack == RunReplayTrack::Solver &&
            replayRuntime.CurrentSolverScrubSample() != nullptr ) );
    input.hotZoneEnabled = !uiBlocksMouse;
    return input;
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
    const int minY = ReplayCauseWindowMinY( screenH );
    state.x = std::clamp( state.x, 8, (std::max)( 8, screenW - state.width - 8 ) );
    state.y = std::clamp( state.y, minY, (std::max)( minY, screenH - state.height - 8 ) );
    state.scrollY = std::clamp( state.scrollY, 0.0f, ReplayCauseWindowMaxScroll( state ) );
}

void EnsureReplayCauseWindowPlacement( RunReplayCauseTreeState& state, int screenW, int screenH )
{
    if ( !state.hasWindowPlacement )
    {
        const int minY = ReplayCauseWindowMinY( screenH );
        state.width = (std::min)( 380, (std::max)( REPLAY_CAUSE_WINDOW_MIN_W, screenW - 48 ) );
        state.height = (std::min)( 520, (std::max)( REPLAY_CAUSE_WINDOW_MIN_H, screenH - minY - 120 ) );
        state.x = (std::max)( 12, screenW - state.width - 24 );
        state.y = minY;
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
