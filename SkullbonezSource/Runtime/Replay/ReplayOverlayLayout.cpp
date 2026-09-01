/*
File: SkullbonezSource/Runtime/Replay/ReplayOverlayLayout.cpp
Purpose:
  Implements replay overlay screen-space geometry shared by input and drawing.

Summary:
  Replay layout is a replay subsystem concern. Input hit boxes and drawn
  controls stay mechanically identical by using the same helpers, including
  generic attached-left clamping that preserves the cause hierarchy's sole
  retained anchor. The same pure bounded projection maps filtered visible rows
  back to their original evidence indices for both input and drawing.

Invariants:
  - Input and rendering must call these helpers for the same rectangles.
  - Scrubber controls are published front-to-back so disabled rows block
    click-through and broad panel rows cannot steal specific actions.
  - Clamp movable overlay windows before drawing or hit testing them.
  - Attached surfaces contribute only a desired/minimum width; they never add
    retained placement coordinates to Replay layout.
  - Filtering preserves depth-first order and source indices; ancestor rows are
    retained for context instead of copied into a replacement tree.

Related:
  - SkullbonezSource/Runtime/Replay/ReplayOverlayLayout.h
  - Agentic/Reference/engine-glossary.md
*/
#include "ReplayOverlayLayout.h"
#include "ReplayScrubber.h"
#include "../../Core/FatalError.h"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace SkullbonezCore::Runtime::ReplayOverlay
{
void ReplayOverlaySurface::Reset() noexcept
{
    controlCount = 0;
    hotControl = {};
    pointerControl = {};
    activeControl = {};
    hasHotControl = false;
    hasPointerControl = false;
    hasActiveControl = false;
    consumesPointer = false;
}

bool ReplayOverlaySurface::TryAdd( const ReplayOverlayControl& control ) noexcept
{
    if ( !control.id || controlCount >= controls.size() || Find( control.id ) )
    {
        return false;
    }

    controls[controlCount++] = control;
    return true;
}

ReplayOverlayControl* ReplayOverlaySurface::Find( ReplayOverlayControlId id ) noexcept
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

void ReplayOverlaySurface::ResolvePointer( int pointerX, int pointerY, bool pointerBlocked ) noexcept
{
    hotControl = {};
    pointerControl = {};
    hasHotControl = false;
    hasPointerControl = false;
    consumesPointer = false;

    for ( std::size_t index = 0; index < controlCount; ++index )
    {
        controls[index].hovered = false;
    }

    if ( pointerBlocked )
    {
        return;
    }

    for ( std::size_t index = 0; index < controlCount; ++index )
    {
        ReplayOverlayControl& control = controls[index];
        const float x = static_cast<float>( pointerX );
        const float y = static_cast<float>( pointerY );
        const UI::UIRect& bounds = control.hitRect;

        if ( control.visible && x >= bounds.x && x <= bounds.x + bounds.w && y >= bounds.y && y <= bounds.y + bounds.h )
        {
            pointerControl = control.id;
            hasPointerControl = true;
            consumesPointer = true;

            if ( control.enabled )
            {
                control.hovered = true;
                hotControl = control.id;
                hasHotControl = true;
            }

            return;
        }
    }
}

namespace
{
// Why: the top-right scene/camera badges own the first screen rows, so the
// draggable cause window starts below them instead of covering status text.
constexpr int REPLAY_CAUSE_WINDOW_SAFE_TOP = 84;

int ReplayCauseWindowMinY( int screenH )
{
    return (std::min)( REPLAY_CAUSE_WINDOW_SAFE_TOP, (std::max)( 8, screenH - REPLAY_CAUSE_WINDOW_MIN_H - 8 ) );
}

char ReplayCauseAsciiLower( char value ) noexcept
{
    return value >= 'A' && value <= 'Z' ? static_cast<char>( value + ( 'a' - 'A' ) ) : value;
}

bool ReplayCauseContainsAscii( const char* text, const char* query ) noexcept
{
    if ( !query || query[0] == '\0' )
    {
        return true;
    }

    if ( !text )
    {
        return false;
    }

    const std::size_t queryLength = strlen( query );

    for ( const char* start = text; *start != '\0'; ++start )
    {
        std::size_t offset = 0;

        while ( offset < queryLength && start[offset] != '\0' &&
                ReplayCauseAsciiLower( start[offset] ) == ReplayCauseAsciiLower( query[offset] ) )
        {
            ++offset;
        }

        if ( offset == queryLength )
        {
            return true;
        }
    }

    return false;
}

bool ReplayCauseRowMatchesFilter( const RunReplayCauseTreeRow& row, const RunReplayCauseTreeState& state ) noexcept
{
    const bool familyMatches = state.filter == RunReplayCauseTreeFilter::All ||
                               ( state.filter == RunReplayCauseTreeFilter::Prediction && row.prediction ) ||
                               ( state.filter == RunReplayCauseTreeFilter::Contacts &&
                                 ( row.kind == RunReplayCauseTreeRowKind::Manifold ||
                                   row.kind == RunReplayCauseTreeRowKind::SolverRow ||
                                   row.kind == RunReplayCauseTreeRowKind::PredictionContact ) );
    return familyMatches && ( ReplayCauseContainsAscii( row.name, state.filterText ) ||
                              ReplayCauseContainsAscii( row.detail, state.filterText ) );
}
} // namespace

int ReplayCauseWindowProjection::SourceRow( int visibleRow ) const noexcept
{
    if ( visibleRow < 0 || visibleRow >= count )
    {
        return -1;
    }

    int visible = 0;

    for ( int sourceRow = 0; sourceRow < sourceCount; ++sourceRow )
    {
        const std::size_t word = static_cast<std::size_t>( sourceRow ) / 64u;
        const uint64_t bit = uint64_t { 1 } << ( static_cast<unsigned int>( sourceRow ) & 63u );

        if ( ( included[word] & bit ) != 0u && visible++ == visibleRow )
        {
            return sourceRow;
        }
    }

    return -1;
}

int ReplayCauseWindowProjection::VisibleRow( int sourceRow ) const noexcept
{
    if ( sourceRow < 0 || sourceRow >= sourceCount )
    {
        return -1;
    }

    int visible = 0;

    for ( int candidate = 0; candidate <= sourceRow; ++candidate )
    {
        const std::size_t word = static_cast<std::size_t>( candidate ) / 64u;
        const uint64_t bit = uint64_t { 1 } << ( static_cast<unsigned int>( candidate ) & 63u );

        if ( ( included[word] & bit ) != 0u )
        {
            if ( candidate == sourceRow )
            {
                return visible;
            }

            ++visible;
        }
    }

    return -1;
}

// Concept: replay overlay geometry is the contract between input and drawing.
//
// A replay control is only usable if the mouse hit box and drawn pixels agree.
// Keep derived rectangles here instead of duplicating layout math in
// ReplayPredictionDrawing and ReplayOverlayRenderer.
UI::UIRect ReplayScrubberPanelRect( int screenW, int screenH )
{
    const float width = (std::min)( REPLAY_SCRUBBER_PANEL_MAX_WIDTH,
                                    (std::max)( 260.0f,
                                                static_cast<float>( screenW ) - REPLAY_SCRUBBER_PANEL_MARGIN * 2.0f ) );

    const float x = ( static_cast<float>( screenW ) - width ) * 0.5f;
    const float y = (std::max)( 0.0f, static_cast<float>( screenH ) - REPLAY_SCRUBBER_PANEL_HEIGHT -
                                          REPLAY_SCRUBBER_PANEL_MARGIN );

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
    return { panel.x + leftInset, ReplayScrubberRowCenterY( panel, track ) - REPLAY_SCRUBBER_TRACK_HEIGHT * 0.5f,
             (std::max)( 80.0f, panel.w - leftInset - rightInset ), REPLAY_SCRUBBER_TRACK_HEIGHT };
}

UI::UIRect ReplayScrubberSaveButtonRect( int screenW, int screenH, RunReplayTrack trackName )
{
    const UI::UIRect panel = ReplayScrubberPanelRect( screenW, screenH );
    const UI::UIRect track = ReplayScrubberTrackRect( screenW, screenH, trackName );
    return { panel.x + 70.0f, track.y - ( REPLAY_SCRUBBER_SAVE_BUTTON_SIZE - track.h ) * 0.5f,
             REPLAY_SCRUBBER_SAVE_BUTTON_SIZE, REPLAY_SCRUBBER_SAVE_BUTTON_SIZE };
}

UI::UIRect ReplayScrubberLoadButtonRect( int screenW, int screenH, RunReplayTrack trackName )
{
    const UI::UIRect save = ReplayScrubberSaveButtonRect( screenW, screenH, trackName );
    return { save.x + save.w + REPLAY_SCRUBBER_SAVE_BUTTON_GAP, save.y, REPLAY_SCRUBBER_LOAD_BUTTON_WIDTH, save.h };
}

UI::UIRect ReplayScrubberBranchButtonRect( int screenW, int screenH )
{
    const UI::UIRect panel = ReplayScrubberPanelRect( screenW, screenH );
    return { panel.x + panel.w - REPLAY_SCRUBBER_RIGHT_CONTROL_WIDTH, panel.y + 14.0f, REPLAY_SCRUBBER_BRANCH_BUTTON_WIDTH,
             22.0f };
}

UI::UIRect ReplayScrubberHighDetailToggleRect( int screenW, int screenH )
{
    const UI::UIRect branch = ReplayScrubberBranchButtonRect( screenW, screenH );
    return { branch.x + branch.w + 10.0f, branch.y, REPLAY_SCRUBBER_HIGH_DETAIL_TOGGLE_WIDTH, 22.0f };
}

UI::UIRect ReplayScrubberVelocityEditToggleRect( int screenW, int screenH )
{
    const UI::UIRect highDetail = ReplayScrubberHighDetailToggleRect( screenW, screenH );
    return { highDetail.x + highDetail.w + 10.0f, highDetail.y, REPLAY_SCRUBBER_VELOCITY_BUTTON_WIDTH, highDetail.h };
}

UI::UIRect ReplayScrubberPredictControlRect( int screenW, int screenH )
{
    const UI::UIRect velocity = ReplayScrubberVelocityEditToggleRect( screenW, screenH );
    return { velocity.x + velocity.w + 10.0f + REPLAY_SCRUBBER_PREDICT_TOGGLE_WIDTH + 8.0f, velocity.y,
             REPLAY_SCRUBBER_PREDICT_SLOT_WIDTH, velocity.h };
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
    return std::clamp( ( seconds - REPLAY_PREDICTION_MIN_SECONDS ) /
                           ( REPLAY_PREDICTION_MAX_SECONDS - REPLAY_PREDICTION_MIN_SECONDS ),
                       0.0f, 1.0f );
}

float ReplayPredictionHorizonFromMouse( int mouseX, const UI::UIRect& horizon )
{
    // Why: prediction seconds use a normalized slider, but the user drags in
    // pixels. Clamp before scaling so mouse drift outside the slot cannot
    // create an invalid future horizon.
    const float t = horizon.w > 1.0f ? std::clamp( ( static_cast<float>( mouseX ) - horizon.x ) / horizon.w, 0.0f, 1.0f )
                                     : 1.0f;

    const float seconds = REPLAY_PREDICTION_MIN_SECONDS +
                          t * ( REPLAY_PREDICTION_MAX_SECONDS - REPLAY_PREDICTION_MIN_SECONDS );

    return std::clamp( std::round( seconds ), REPLAY_PREDICTION_MIN_SECONDS, REPLAY_PREDICTION_MAX_SECONDS );
}

UI::UIRect ReplayScrubberHotZoneRect( int screenW, int screenH )
{
    // Why: the bottom-left minimized/options UI is also a click target. Only
    // the centered two-thirds of the bottom edge should summon replay controls.
    const float width = static_cast<float>( screenW ) * ( 2.0f / 3.0f );
    const float x = ( static_cast<float>( screenW ) - width ) * 0.5f;
    return { x, (std::max)( 0.0f, static_cast<float>( screenH ) - REPLAY_SCRUBBER_HOT_ZONE_HEIGHT ), width,
             REPLAY_SCRUBBER_HOT_ZONE_HEIGHT };
}

void BuildReplayScrubberSurface( const ReplayScrubberSurfaceInput& input, ReplayScrubberSurface& outSurface )
{
    outSurface.Reset();

    const auto addControl = [&]( ReplayScrubberControl id, ReplayScrubberAction action, ReplayOverlayControlKind kind,
                                 const UI::UIRect& drawRect, const UI::UIRect& hitRect, bool visible, bool enabled,
                                 bool checked = false )
    {
        ReplayOverlayControl control;

        control.id = ReplayScrubberControlId( id );
        control.action = static_cast<uint32_t>( action );

        control.kind = kind;
        control.drawRect = drawRect;
        control.hitRect = hitRect;
        control.visible = visible;
        control.enabled = enabled;
        control.checked = checked;
        control.requestsReveal = true;

        if ( !outSurface.TryAdd( control ) )
        {
            // Fatal invariant: a duplicate id or undersized compile-time table makes UI
            // z-order and dispatch ambiguous, so the frame cannot safely continue.
            SB_FATAL( "ReplayScrubberSurface", "Cannot publish replay scrubber control id=%u.", control.id.value );
        }
    };

    const UI::UIRect branch = ReplayScrubberBranchButtonRect( input.screenW, input.screenH );
    const UI::UIRect highDetail = ReplayScrubberHighDetailToggleRect( input.screenW, input.screenH );
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
    const float horizonHitBottom = (std::max)( predictionHorizon.y + predictionHorizon.h,
                                               predictionPanel.y + predictionPanel.h );

    const float horizonHitTop = (std::min)( predictionHorizon.y, predictionPanel.y );
    const UI::UIRect predictionHorizonHit = { predictionHorizon.x, horizonHitTop, predictionHorizon.w,
                                              horizonHitBottom - horizonHitTop };

    // Invariant: rows are added front-to-back. Disabled controls still block
    // click-through, while broad panel/reveal zones sit behind real controls.
    addControl( ReplayScrubberControl::Branch, ReplayScrubberAction::RestoreBranch, ReplayOverlayControlKind::Button, branch,
                branch, true, input.branchTargetAvailable );

    addControl( ReplayScrubberControl::HighDetail, ReplayScrubberAction::SetPredictionDetailMode,
                ReplayOverlayControlKind::Toggle, highDetail, highDetail, !input.loadedPresentation,
                input.predictionToolsEnabled, input.predictionHighDetail );

    addControl( ReplayScrubberControl::VelocityEdit, ReplayScrubberAction::ToggleVelocityEdit,
                ReplayOverlayControlKind::Toggle, velocity, velocity, !input.loadedPresentation, input.solverToolsEnabled );

    addControl( ReplayScrubberControl::PredictionToggle, ReplayScrubberAction::TogglePrediction,
                ReplayOverlayControlKind::Toggle, predictionToggle, predictionToggle, !input.loadedPresentation,
                input.predictionToolsEnabled );

    addControl( ReplayScrubberControl::PredictionHorizon, ReplayScrubberAction::SetPredictionHorizon,
                ReplayOverlayControlKind::Slider, predictionHorizon, predictionHorizonHit, !input.loadedPresentation,
                input.predictionToolsEnabled );

    addControl( ReplayScrubberControl::RagdollVisuals, ReplayScrubberAction::ToggleRagdollVisuals,
                ReplayOverlayControlKind::Toggle, ragdoll, ragdoll, !input.loadedPresentation,
                input.predictionToolsEnabled );

    addControl( ReplayScrubberControl::PastPath, ReplayScrubberAction::TogglePastPath, ReplayOverlayControlKind::Toggle,
                pastPath, pastPath, !input.loadedPresentation, input.pastPathToolsEnabled );

    addControl( ReplayScrubberControl::Save, ReplayScrubberAction::Save, ReplayOverlayControlKind::Button, save, save, true,
                input.solverToolsEnabled );

    addControl( ReplayScrubberControl::Load, ReplayScrubberAction::Load, ReplayOverlayControlKind::Button, load, load, true,
                true );

    addControl( ReplayScrubberControl::ScrubTrack, ReplayScrubberAction::Scrub, ReplayOverlayControlKind::Track, track,
                track, true, input.scrubTrackDragEnabled );

    addControl( ReplayScrubberControl::PredictionPanel, ReplayScrubberAction::None, ReplayOverlayControlKind::Panel,
                predictionPanel, predictionPanel, !input.loadedPresentation, input.predictionToolsEnabled );

    addControl( ReplayScrubberControl::Panel, ReplayScrubberAction::None, ReplayOverlayControlKind::Panel, panel, panel,
                true, true );

    addControl( ReplayScrubberControl::HotZone, ReplayScrubberAction::None, ReplayOverlayControlKind::HotZone, hotZone,
                hotZone, input.hotZoneEnabled, true );

    ReplayScrubberControl active = ReplayScrubberControl::None;

    if ( input.gesture == ReplayToolGestureKind::ScrubDrag )
    {
        active = ReplayScrubberControl::ScrubTrack;
    }
    else if ( input.gesture == ReplayToolGestureKind::PredictionHorizonDrag )
    {
        active = ReplayScrubberControl::PredictionHorizon;
    }

    if ( active != ReplayScrubberControl::None )
    {
        outSurface.activeControl = ReplayScrubberControlId( active );
        outSurface.hasActiveControl = true;

        if ( ReplayOverlayControl* control = outSurface.Find( outSurface.activeControl ) )
        {
            control->active = true;
        }
    }
}

bool ReplayScrubberSourceAvailability::CurrentTrackAvailable( RunReplayTrack track ) const noexcept
{
    return track == RunReplayTrack::Presentation ? currentPresentation : currentSolver;
}


ReplayScrubberSurfaceInput DescribeReplayScrubberAvailability( const ReplayScrubberView& scrubber,
                                                               const ReplayRecorderStats& solverStats,
                                                               const ReplayScrubberSourceAvailability& sources )
{
    ReplayScrubberSurfaceInput input;
    input.loadedPresentation = sources.loadedPresentation;
    // Why: ordinary rewind follows the longer presentation ring. Prediction
    // supplies the only live surface that needs the solver track's future span.
    input.track = input.loadedPresentation || !sources.predictionTimeline ? RunReplayTrack::Presentation
                                                                          : RunReplayTrack::Solver;
    const bool solverReplayEnabled = solverStats.enabled;
    const bool solverReplayAvailable = solverReplayEnabled && solverStats.sampleCount >= 2;
    input.solverToolsEnabled = !input.loadedPresentation && solverReplayAvailable;
    input.predictionToolsEnabled = !input.loadedPresentation && solverReplayEnabled && sources.scenePhysics;
    input.pastPathToolsEnabled = input.solverToolsEnabled && sources.pathTarget;
    input.scrubTrackDragEnabled = input.loadedPresentation || input.solverToolsEnabled ||
                                  ( input.predictionToolsEnabled && sources.predictionTimeline );

    const bool activeTrackOwned = scrubber.activeTrack == RunReplayTrack::Presentation ? input.loadedPresentation
                                                                                       : input.solverToolsEnabled;
    input.branchTargetAvailable = scrubber.historicalSamplePaused && activeTrackOwned &&
                                  sources.CurrentTrackAvailable( scrubber.activeTrack );

    return input;
}

void BuildReplayCauseWindowSurface( const RunReplayCauseTreeState& state, ReplayCauseWindowSurface& outSurface )
{
    outSurface.Reset();
    const auto add = [&]( ReplayCauseWindowControl id, ReplayOverlayControlKind kind, const UI::UIRect& bounds )
    {
        ReplayOverlayControl control;

        control.id = ReplayCauseWindowControlId( id );
        control.action = static_cast<uint32_t>( id );

        control.kind = kind;
        control.drawRect = bounds;
        control.hitRect = bounds;

        if ( !outSurface.TryAdd( control ) )
        {
            SB_FATAL( "ReplayCauseWindowSurface", "Cannot publish cause-window control id=%u.", control.id.value );
        }
    };

    // Resize, title, and filter controls sit in front of content and the broad
    // panel background. Specific controls publish before their containing rows.
    add( ReplayCauseWindowControl::Resize, ReplayOverlayControlKind::ToolHandle, ReplayCauseWindowResizeRect( state ) );
    add( ReplayCauseWindowControl::Title, ReplayOverlayControlKind::Track, ReplayCauseWindowTitleRect( state ) );
    add( ReplayCauseWindowControl::FilterField, ReplayOverlayControlKind::Button,
         ReplayCauseWindowFilterFieldRect( state ) );
    add( ReplayCauseWindowControl::FilterFunnel, ReplayOverlayControlKind::Button,
         ReplayCauseWindowFilterFunnelRect( state ) );
    add( ReplayCauseWindowControl::FilterAll, ReplayOverlayControlKind::Button,
         ReplayCauseWindowFilterChipRect( state, RunReplayCauseTreeFilter::All ) );
    add( ReplayCauseWindowControl::FilterPrediction, ReplayOverlayControlKind::Button,
         ReplayCauseWindowFilterChipRect( state, RunReplayCauseTreeFilter::Prediction ) );
    add( ReplayCauseWindowControl::FilterContacts, ReplayOverlayControlKind::Button,
         ReplayCauseWindowFilterChipRect( state, RunReplayCauseTreeFilter::Contacts ) );
    add( ReplayCauseWindowControl::Content, ReplayOverlayControlKind::Panel, ReplayCauseWindowContentRect( state ) );
    add( ReplayCauseWindowControl::Panel, ReplayOverlayControlKind::Panel, ReplayCauseWindowRect( state ) );
}


UI::UIRect ReplayCauseWindowRect( const RunReplayCauseTreeState& state )
{
    return { static_cast<float>( state.x ), static_cast<float>( state.y ), static_cast<float>( state.width ),
             static_cast<float>( state.height ) };
}

UI::UIRect ReplayCauseWindowTitleRect( const RunReplayCauseTreeState& state )
{
    const UI::UIRect panel = ReplayCauseWindowRect( state );
    return { panel.x, panel.y, panel.w, REPLAY_CAUSE_WINDOW_TITLE_HEIGHT };
}

UI::UIRect ReplayCauseWindowFilterFieldRect( const RunReplayCauseTreeState& state )
{
    const UI::UIRect panel = ReplayCauseWindowRect( state );
    return { panel.x + REPLAY_CAUSE_WINDOW_PADDING, panel.y + REPLAY_CAUSE_WINDOW_TITLE_HEIGHT + 7.0f,
             panel.w - REPLAY_CAUSE_WINDOW_PADDING * 2.0f - 32.0f, 24.0f };
}

UI::UIRect ReplayCauseWindowFilterFunnelRect( const RunReplayCauseTreeState& state )
{
    const UI::UIRect field = ReplayCauseWindowFilterFieldRect( state );
    return { field.x + field.w + 6.0f, field.y, 26.0f, field.h };
}

UI::UIRect ReplayCauseWindowFilterChipRect( const RunReplayCauseTreeState& state, RunReplayCauseTreeFilter filter )
{
    const UI::UIRect panel = ReplayCauseWindowRect( state );
    const float available = panel.w - REPLAY_CAUSE_WINDOW_PADDING * 2.0f;
    const float chipWidth = available / 3.0f;
    const int index = filter == RunReplayCauseTreeFilter::All ? 0
                                                              : ( filter == RunReplayCauseTreeFilter::Prediction ? 1 : 2 );
    return { panel.x + REPLAY_CAUSE_WINDOW_PADDING + chipWidth * static_cast<float>( index ),
             panel.y + REPLAY_CAUSE_WINDOW_TITLE_HEIGHT + 38.0f, chipWidth, 24.0f };
}

UI::UIRect ReplayCauseWindowContentRect( const RunReplayCauseTreeState& state )
{
    const UI::UIRect panel = ReplayCauseWindowRect( state );
    return { panel.x + REPLAY_CAUSE_WINDOW_PADDING,
             panel.y + REPLAY_CAUSE_WINDOW_TITLE_HEIGHT + REPLAY_CAUSE_WINDOW_FILTER_HEIGHT,
             panel.w - REPLAY_CAUSE_WINDOW_PADDING * 2.0f,
             panel.h - REPLAY_CAUSE_WINDOW_TITLE_HEIGHT - REPLAY_CAUSE_WINDOW_FILTER_HEIGHT -
                 REPLAY_CAUSE_WINDOW_FOOTER_HEIGHT - REPLAY_CAUSE_WINDOW_PADDING };
}

UI::UIRect ReplayCauseWindowResizeRect( const RunReplayCauseTreeState& state )
{
    const UI::UIRect panel = ReplayCauseWindowRect( state );
    return { panel.x + panel.w - REPLAY_CAUSE_WINDOW_RESIZE_SIZE, panel.y + panel.h - REPLAY_CAUSE_WINDOW_RESIZE_SIZE,
             REPLAY_CAUSE_WINDOW_RESIZE_SIZE, REPLAY_CAUSE_WINDOW_RESIZE_SIZE };
}

bool ReplayCauseWindowContainsPoint( const RunReplayCauseTreeState& state, int x, int y )
{
    const UI::UIRect panel = ReplayCauseWindowRect( state );
    const float pointX = static_cast<float>( x );
    const float pointY = static_cast<float>( y );
    return pointX >= panel.x && pointX <= panel.x + panel.w && pointY >= panel.y && pointY <= panel.y + panel.h;
}

float ReplayCauseWindowContentHeight( const RunReplayCauseTreeState& state )
{
    ReplayCauseWindowProjection projection;
    BuildReplayCauseWindowProjection( state, projection );
    return static_cast<float>( projection.count ) * REPLAY_CAUSE_WINDOW_ROW_HEIGHT;
}

float ReplayCauseWindowMaxScroll( const RunReplayCauseTreeState& state )
{
    const UI::UIRect content = ReplayCauseWindowContentRect( state );
    return (std::max)( 0.0f, ReplayCauseWindowContentHeight( state ) - content.h );
}

void BuildReplayCauseWindowProjection( const RunReplayCauseTreeState& state,
                                       ReplayCauseWindowProjection& outProjection ) noexcept
{
    outProjection = {};
    const int sourceCount = (std::min)( static_cast<int>( state.rows.size() ),
                                        static_cast<int>( REPLAY_CAUSE_TREE_ROW_CAPACITY ) );
    outProjection.sourceCount = sourceCount;
    const auto includeSourceRow = [&]( int sourceRow )
    {
        const std::size_t word = static_cast<std::size_t>( sourceRow ) / 64u;
        const uint64_t bit = uint64_t { 1 } << ( static_cast<unsigned int>( sourceRow ) & 63u );

        if ( ( outProjection.included[word] & bit ) == 0u )
        {
            outProjection.included[word] |= bit;
            ++outProjection.count;
        }
    };

    for ( int sourceRow = 0; sourceRow < sourceCount; ++sourceRow )
    {
        const RunReplayCauseTreeRow& row = state.rows[static_cast<std::size_t>( sourceRow )];

        if ( !ReplayCauseRowMatchesFilter( row, state ) )
        {
            continue;
        }

        includeSourceRow( sourceRow );
        int ancestorDepth = row.depth - 1;

        // Invariant: the source rows are depth-first. Walking backward to the
        // nearest row at each lower depth retains ancestry without reparenting
        // or copying evidence identity into a second tree.
        for ( int ancestor = sourceRow - 1; ancestor >= 0 && ancestorDepth >= 0; --ancestor )
        {
            if ( state.rows[static_cast<std::size_t>( ancestor )].depth == ancestorDepth )
            {
                includeSourceRow( ancestor );
                --ancestorDepth;
            }
        }
    }
}

bool AppendReplayCauseFilterCharacter( RunReplayCauseTreeState& state, char value ) noexcept
{
    const unsigned char code = static_cast<unsigned char>( value );

    // The engine's bitmap font and virtual-key path are ASCII-only. Rejecting
    // unsupported bytes keeps truncation deterministic and prevents partial
    // multibyte sequences from becoming misleading evidence queries.
    if ( code < 32u || code > 126u )
    {
        return false;
    }

    const std::size_t length = strlen( state.filterText );

    if ( length + 1u >= sizeof( state.filterText ) )
    {
        return false;
    }

    state.filterText[length] = value;
    state.filterText[length + 1u] = '\0';
    return true;
}

bool BackspaceReplayCauseFilter( RunReplayCauseTreeState& state ) noexcept
{
    const std::size_t length = strlen( state.filterText );

    if ( length == 0u )
    {
        return false;
    }

    state.filterText[length - 1u] = '\0';
    return true;
}

bool ClearReplayCauseFilterText( RunReplayCauseTreeState& state ) noexcept
{
    if ( state.filterText[0] == '\0' )
    {
        return false;
    }

    state.filterText[0] = '\0';
    return true;
}

float ReplayCauseWindowAttachedWidth( const RunReplayCauseTreeState& state, int screenW, float desiredWidth,
                                      float minimumWidth )
{
    const float desired = (std::max)( 0.0f, desiredWidth );
    const float minimum = std::clamp( minimumWidth, 0.0f, desired );
    const float available = (std::max)( 0.0f, static_cast<float>( screenW - 16 - state.width ) );

    // Why: the drawer shrinks before the hierarchy, but a viewport narrower
    // than both minima still gets a truthful bounded remainder rather than an
    // off-screen target rectangle.
    return std::clamp( available, (std::min)( minimum, available ), desired );
}

void ClampReplayCauseWindow( RunReplayCauseTreeState& state, int screenW, int screenH, float desiredAttachedLeftWidth,
                             float minimumAttachedLeftWidth )
{
    // Invariant: the resize handle and title bar must remain reachable after a
    // resolution change, or the inspection window can become permanently
    // off-screen for the session.
    const int availableWidth = (std::max)( 1, screenW - 16 );
    state.width = (std::max)( REPLAY_CAUSE_WINDOW_MIN_W,
                              (std::min)( state.width, (std::max)( REPLAY_CAUSE_WINDOW_MIN_W, availableWidth ) ) );

    const int minimumAttachment = static_cast<int>( std::ceil( (std::max)( 0.0f, minimumAttachedLeftWidth ) ) );
    const int hierarchyWidthWithMinimumAttachment = availableWidth - minimumAttachment;

    if ( hierarchyWidthWithMinimumAttachment >= REPLAY_CAUSE_WINDOW_MIN_W )
    {
        state.width = (std::min)( state.width, hierarchyWidthWithMinimumAttachment );
    }

    state.height = (std::max)( REPLAY_CAUSE_WINDOW_MIN_H,
                               (std::min)( state.height, (std::max)( REPLAY_CAUSE_WINDOW_MIN_H, screenH - 16 ) ) );

    const float attachedWidth = ReplayCauseWindowAttachedWidth( state, screenW, desiredAttachedLeftWidth,
                                                                minimumAttachedLeftWidth );
    const int minY = ReplayCauseWindowMinY( screenH );
    const int minimumX = 8 + static_cast<int>( std::ceil( attachedWidth ) );
    state.x = std::clamp( state.x, minimumX, (std::max)( minimumX, screenW - state.width - 8 ) );
    state.y = std::clamp( state.y, minY, (std::max)( minY, screenH - state.height - 8 ) );
    state.scrollY = std::clamp( state.scrollY, 0.0f, ReplayCauseWindowMaxScroll( state ) );
}

void EnsureReplayCauseWindowPlacement( RunReplayCauseTreeState& state, int screenW, int screenH,
                                       float desiredAttachedLeftWidth, float minimumAttachedLeftWidth )
{
    if ( !state.hasWindowPlacement )
    {
        const int minY = ReplayCauseWindowMinY( screenH );
        state.width = (std::min)( 380, (std::max)( REPLAY_CAUSE_WINDOW_MIN_W, screenW - 48 ) );
        state.height = (std::min)( 520, (std::max)( REPLAY_CAUSE_WINDOW_MIN_H, screenH - minY - 8 ) );
        state.x = (std::max)( 12, screenW - state.width - 24 );
        state.y = minY;
        state.hasWindowPlacement = true;
    }

    ClampReplayCauseWindow( state, screenW, screenH, desiredAttachedLeftWidth, minimumAttachedLeftWidth );
}

void MoveReplayCauseWindow( RunReplayCauseTreeState& state, int mouseX, int mouseY, int screenW, int screenH,
                            float desiredAttachedLeftWidth, float minimumAttachedLeftWidth )
{
    state.x = mouseX - state.dragOffsetX;
    state.y = mouseY - state.dragOffsetY;
    ClampReplayCauseWindow( state, screenW, screenH, desiredAttachedLeftWidth, minimumAttachedLeftWidth );
}

void ResizeReplayCauseWindow( RunReplayCauseTreeState& state, int mouseX, int mouseY, int screenW, int screenH,
                              float desiredAttachedLeftWidth, float minimumAttachedLeftWidth )
{
    state.width = state.resizeStartWidth + ( mouseX - state.resizeStartMouseX );
    state.height = state.resizeStartHeight + ( mouseY - state.resizeStartMouseY );
    ClampReplayCauseWindow( state, screenW, screenH, desiredAttachedLeftWidth, minimumAttachedLeftWidth );
}

float ReplayScrubberPositionFromMouse( int mouseX, int screenW, int screenH, RunReplayTrack trackName )
{
    const UI::UIRect track = ReplayScrubberTrackRect( screenW, screenH, trackName );
    return track.w > 1.0f ? std::clamp( ( static_cast<float>( mouseX ) - track.x ) / track.w, 0.0f, 1.0f ) : 1.0f;
}
} // namespace SkullbonezCore::Runtime::ReplayOverlay
