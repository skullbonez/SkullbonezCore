/*
File: SkullbonezSource/Runtime/Replay/ReplayOverlayRenderer.cpp
Purpose:
  Draws replay scrubber and cause-tree overlays from replay-owned state.

Mental model:
  Replay overlay rendering is a late UI pass. Keep the same screen-space layout
  as replay input by using ReplayOverlayLayout helpers.

Glossary:
  UI (User Interface): Runtime controls and overlays drawn over the 3D scene.
  Scrubber: Replay timeline UI for retained samples, loaded artifacts, and
    future prediction frames.
  Cause tree: Contact/solver explanation view rooted at the selected replay
    body.
  Presentation sample: Render-only replay pose used for visual scrub previews.
  Solver sample: Replay frame with solver snapshot data used for restore and
    inspection.

Invariants:
  - Drawn controls must use ReplayOverlayLayout rectangles so input hit boxes
    stay identical.
  - Overlay rendering reads replay state only; replay mutation belongs to input
    and runtime replay helpers.

Related:
  - SkullbonezSource/Runtime/Replay/ReplayOverlayRenderer.h
  - SkullbonezSource/Runtime/Replay/ReplayOverlayLayout.h
*/
#include "ReplayOverlayRenderer.h"
#include "ReplayOverlayLayout.h"
#include "../../Core/Common.h"
#include "../../Core/Profiler.h"
#include "../../Rendering/Text.h"
#include "../../UI/UIDraw.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>

namespace SkullbonezCore::Basics::ReplayOverlay
{
using Text::Text2d;

// Concept: the replay overlay is a read-only projection of replay state.
//
// Input code owns mutations such as dragging, toggling prediction, and branch
// creation. This pass samples the current state and turns it into UI quads and
// text so rendering cannot accidentally advance or rewrite replay timelines.
void RenderReplayScrubberOverlay( const ReplayOverlayRenderContext& context )
{
    PROFILE_SCOPED( "Frame/Replay/ScrubberOverlay" );
    ReplayRuntime& replayRuntime = context.replayRuntime;
    // Why: the cause tree is an inspection tool, not a child of the scrubber.
    // Draw it even when the scrubber itself is hidden by UI/editor policy.
    RenderReplayCauseTreeOverlay( context );

    if ( !replayRuntime.ShouldRenderScrubber( context.editorModeEnabled, context.uiVisible, context.uiMinimized ) )
    {
        return;
    }

    const int screenW = context.screenW;
    const int screenH = context.screenH;
    const bool loadedPresentation = replayRuntime.HasLoadedPresentation();
    const ReplayRecorderStats solverReplayStats = replayRuntime.Solver().GetStats();
    if ( screenW <= 0 || screenH <= 0 ||
         ( !loadedPresentation && ( !solverReplayStats.enabled || solverReplayStats.sampleCount < 2 ) ) )
    {
        return;
    }

    const RunReplayTrack activeTrack = loadedPresentation ? RunReplayTrack::Presentation : RunReplayTrack::Solver;
    const float t = std::clamp( replayRuntime.TrackPosition( activeTrack ), 0.0f, 1.0f );
    const float solverPresentT = loadedPresentation ? 1.0f : replayRuntime.SolverPresentTrackPosition();
    // Concept: the solver track is split into retained history and generated
    // future. Positions past the live marker draw prediction frames instead of
    // retained solver samples.
    const bool futureTimelineVisible = !loadedPresentation && ReplayRuntime::TimelineHasFuture( solverPresentT );
    const bool futureSelected = !loadedPresentation && ReplayRuntime::TrackPositionIsFuture( t, solverPresentT );
    const float solverSampleT = ReplayRuntime::SolverNormalizedFromTrack( t, solverPresentT );
    const ReplayPresentationSample* selectedPresentation =
        loadedPresentation ? replayRuntime.LoadedPresentationSampleAtNormalized( t ) : nullptr;
    const ReplayPresentationSample* latestPresentation =
        loadedPresentation ? replayRuntime.LoadedPresentationLatestSample() : nullptr;
    const ReplaySolverFrameSample* selected =
        ( loadedPresentation || futureSelected ) ? nullptr : replayRuntime.Solver().SampleAtNormalized( solverSampleT );
    const ReplaySolverFrameSample* latest = loadedPresentation ? nullptr : replayRuntime.Solver().LatestSample();
    const RunReplayPredictionFrame* selectedPrediction =
        futureSelected ? replayRuntime.CurrentPredictionScrubFrame() : nullptr;
    const double selectedSeconds = selected ? selected->simulationSeconds : 0.0;
    const double latestSeconds = latest ? latest->simulationSeconds : 0.0;
    const double selectedPresentationSeconds = selectedPresentation ? selectedPresentation->simulationSeconds : 0.0;
    const double latestPresentationSeconds = latestPresentation ? latestPresentation->simulationSeconds : 0.0;
    const double futureSeconds = selectedPrediction ? static_cast<double>( selectedPrediction->frameIndex ) *
                                                          static_cast<double>( PHYSICS_FIXED_DT )
                                                    : 0.0;
    double secondsBack = 0.0;
    if ( loadedPresentation && latestPresentationSeconds >= selectedPresentationSeconds )
    {
        secondsBack = latestPresentationSeconds - selectedPresentationSeconds;
    }
    else if ( latestSeconds >= selectedSeconds )
    {
        secondsBack = latestSeconds - selectedSeconds;
    }

    char timeLabel[48] = {};
    if ( loadedPresentation && ReplayRuntime::AtPresentTrackPosition( t, 1.0f ) )
    {
        sprintf_s( timeLabel, sizeof( timeLabel ), "END" );
    }
    else if ( selectedPrediction )
    {
        sprintf_s( timeLabel, sizeof( timeLabel ), "+%.1fs", futureSeconds );
    }
    else if ( ReplayRuntime::AtPresentTrackPosition( t, solverPresentT ) &&
              !replayRuntime.Scrubber().historicalSamplePaused )
    {
        sprintf_s( timeLabel, sizeof( timeLabel ), "LIVE" );
    }
    else
    {
        sprintf_s( timeLabel, sizeof( timeLabel ), "-%.1fs", secondsBack );
    }

    const UI::UIDrawContext draw( screenW, screenH );
    const UI::UIRect panel = ReplayScrubberPanelRect( screenW, screenH );
    const bool live = !loadedPresentation && ReplayRuntime::AtPresentTrackPosition( t, solverPresentT ) &&
                      !replayRuntime.Scrubber().historicalSamplePaused;
    const double now = context.nowSeconds;
    const char* sourceLabel = loadedPresentation ? "V2 FILE" : "SOLVER";
    const bool branchEnabled = replayRuntime.Scrubber().historicalSamplePaused &&
                               ( ( loadedPresentation && replayRuntime.CurrentScrubSample() != nullptr ) ||
                                 ( !loadedPresentation && replayRuntime.CurrentSolverScrubSample() != nullptr ) );

    draw.RoundedRect( panel.x, panel.y, panel.w, panel.h, 8.0f, 0.015f, 0.018f, 0.024f, 0.74f );
    draw.Text( panel.x + 16.0f, panel.y + 19.0f, 10.5f, 0.54f, 0.98f, 0.80f, sourceLabel );
    const float labelW = Text2d::MeasureText( 11.0f, timeLabel );
    draw.Text( panel.x + panel.w - labelW - 16.0f,
               panel.y + 18.0f,
               11.0f,
               live ? 0.58f : 1.0f,
               live ? 0.96f : 0.86f,
               live ? 0.70f : 0.36f,
               timeLabel );

    {
        const UI::UIRect branchButton = ReplayScrubberBranchButtonRect( screenW, screenH );
        const bool branchHover = branchEnabled && replayRuntime.Scrubber().branchHovered;
        draw.RoundedRect( branchButton.x,
                          branchButton.y,
                          branchButton.w,
                          branchButton.h,
                          4.0f,
                          branchEnabled ? ( branchHover ? 0.20f : 0.10f ) : 0.06f,
                          branchEnabled ? ( branchHover ? 0.36f : 0.18f ) : 0.07f,
                          branchEnabled ? ( branchHover ? 0.54f : 0.24f ) : 0.09f,
                          branchEnabled ? 0.94f : 0.42f );
        draw.Outline( branchButton.x,
                      branchButton.y,
                      branchButton.w,
                      branchButton.h,
                      0.56f,
                      0.78f,
                      1.0f,
                      branchEnabled ? ( branchHover ? 0.84f : 0.42f ) : 0.18f );
        draw.Text( branchButton.x + 12.0f,
                   branchButton.y + 4.5f,
                   9.5f,
                   branchEnabled ? 0.78f : 0.42f,
                   branchEnabled ? 0.92f : 0.48f,
                   branchEnabled ? 1.0f : 0.56f,
                   "BRANCH" );
    }

    if ( !loadedPresentation )
    {
        const UI::UIRect pauseButton = ReplayScrubberPauseButtonRect( screenW, screenH );
        const bool liveAdvanceHeld = replayRuntime.Scrubber().liveAdvanceHeld;
        const bool pauseHover = replayRuntime.Scrubber().pauseHovered;
        draw.RoundedRect( pauseButton.x,
                          pauseButton.y,
                          pauseButton.w,
                          pauseButton.h,
                          4.0f,
                          liveAdvanceHeld ? 0.18f : 0.08f,
                          liveAdvanceHeld ? 0.33f : 0.12f,
                          liveAdvanceHeld ? 0.21f : 0.15f,
                          pauseHover || liveAdvanceHeld ? 0.94f : 0.78f );
        draw.Outline( pauseButton.x,
                      pauseButton.y,
                      pauseButton.w,
                      pauseButton.h,
                      0.58f,
                      0.92f,
                      0.72f,
                      pauseHover || liveAdvanceHeld ? 0.78f : 0.36f );
        draw.Text( pauseButton.x + 9.0f,
                   pauseButton.y + 5.0f,
                   9.5f,
                   liveAdvanceHeld ? 0.72f : 0.60f,
                   liveAdvanceHeld ? 1.0f : 0.72f,
                   liveAdvanceHeld ? 0.78f : 0.76f,
                   liveAdvanceHeld ? "PLAY" : "PAUSE" );

        {
            PROFILE_SCOPED( "Frame/Replay/ScrubberOverlay/VelocityEditControls" );
            const UI::UIRect velocityEdit = ReplayScrubberVelocityEditToggleRect( screenW, screenH );
            const bool velocityEditEnabled = replayRuntime.VelocityEdit().enabled;
            const bool velocityEditHover = replayRuntime.VelocityEdit().toggleHovered;
            draw.RoundedRect( velocityEdit.x,
                              velocityEdit.y,
                              velocityEdit.w,
                              velocityEdit.h,
                              4.0f,
                              velocityEditEnabled ? 0.25f : 0.08f,
                              velocityEditEnabled ? 0.19f : 0.12f,
                              velocityEditEnabled ? 0.06f : 0.15f,
                              velocityEditHover || velocityEditEnabled ? 0.94f : 0.78f );
            draw.Outline( velocityEdit.x,
                          velocityEdit.y,
                          velocityEdit.w,
                          velocityEdit.h,
                          0.98f,
                          0.82f,
                          0.42f,
                          velocityEditHover || velocityEditEnabled ? 0.78f : 0.34f );
            const float checkX = velocityEdit.x + 7.0f;
            const float checkY = velocityEdit.y + 5.0f;
            draw.Outline( checkX, checkY, 10.0f, 10.0f, 0.98f, 0.86f, 0.54f, 0.82f );
            if ( velocityEditEnabled )
            {
                draw.Rect( checkX + 2.0f, checkY + 2.0f, 6.0f, 6.0f, 1.0f, 0.62f, 0.16f, 0.95f );
            }
            draw.Text( velocityEdit.x + 23.0f,
                       velocityEdit.y + 4.5f,
                       9.5f,
                       velocityEditEnabled ? 1.0f : 0.66f,
                       velocityEditEnabled ? 0.86f : 0.72f,
                       velocityEditEnabled ? 0.56f : 0.76f,
                       "ALT VEL" );
        }
    }

    auto drawReplayRow = [&]( RunReplayTrack trackName,
                              float fillR,
                              float fillG,
                              float fillB,
                              float outlineR,
                              float outlineG,
                              float outlineB,
                              bool saveEnabled )
    {
        const UI::UIRect track = ReplayScrubberTrackRect( screenW, screenH, trackName );
        const UI::UIRect saveButton = ReplayScrubberSaveButtonRect( screenW, screenH, trackName );
        const UI::UIRect loadButton = ReplayScrubberLoadButtonRect( screenW, screenH, trackName );
        const float rowT = std::clamp( replayRuntime.TrackPosition( trackName ), 0.0f, 1.0f );
        const float fillW = (std::max)( REPLAY_SCRUBBER_TRACK_HEIGHT, track.w * rowT );
        const float knobX = track.x + track.w * rowT;
        const bool active = activeTrack == trackName;
        const bool inactiveDuringScrub =
            ( replayRuntime.Scrubber().dragging || replayRuntime.Scrubber().historicalSamplePaused ) && !active;
        const bool saveHover = saveEnabled && replayRuntime.Scrubber().saveHovered &&
                               replayRuntime.Scrubber().saveHoveredTrack == trackName;
        const bool saveFeedback =
            replayRuntime.Scrubber().saveMessage[0] != '\0' && replayRuntime.Scrubber().saveMessageUntil >= now;
        const bool saveFailed = saveFeedback && strstr( replayRuntime.Scrubber().saveMessage, "FAILED" ) != nullptr;
        const bool loadHover = replayRuntime.Scrubber().loadHovered;
        const float saveR = saveFeedback ? ( saveFailed ? 0.48f : 0.13f ) : ( saveHover ? 0.20f : 0.09f );
        const float saveG = saveFeedback ? ( saveFailed ? 0.12f : 0.48f ) : ( saveHover ? 0.42f : 0.20f );
        const float saveB = saveFeedback ? ( saveFailed ? 0.12f : 0.34f ) : ( saveHover ? 0.55f : 0.28f );

        const float rowBack = inactiveDuringScrub ? 0.11f : 0.16f;
        const float rowFillR = inactiveDuringScrub ? 0.30f : fillR;
        const float rowFillG = inactiveDuringScrub ? 0.33f : fillG;
        const float rowFillB = inactiveDuringScrub ? 0.36f : fillB;
        const float rowFillA = inactiveDuringScrub ? 0.40f : ( live && active ? 0.64f : 0.94f );
        draw.RoundedRect( track.x,
                          track.y,
                          track.w,
                          track.h,
                          track.h * 0.5f,
                          rowBack,
                          rowBack + 0.02f,
                          rowBack + 0.05f,
                          inactiveDuringScrub ? 0.74f : 0.92f );
        if ( trackName == RunReplayTrack::Solver && futureTimelineVisible )
        {
            const float presentX = track.x + track.w * solverPresentT;
            draw.Rect( presentX,
                       track.y,
                       (std::max)( 0.0f, track.x + track.w - presentX ),
                       track.h,
                       0.09f,
                       0.26f,
                       0.20f,
                       inactiveDuringScrub ? 0.34f : 0.62f );
        }
        draw.RoundedRect( track.x, track.y, fillW, track.h, track.h * 0.5f, rowFillR, rowFillG, rowFillB, rowFillA );
        draw.RoundedRect( knobX - 6.0f,
                          track.y - 5.0f,
                          12.0f,
                          18.0f,
                          5.0f,
                          active ? 0.98f : 0.52f,
                          active ? 0.98f : 0.56f,
                          active ? 1.0f : 0.60f,
                          active ? 0.98f : 0.70f );
        draw.Outline( knobX - 6.0f,
                      track.y - 5.0f,
                      12.0f,
                      18.0f,
                      outlineR,
                      outlineG,
                      outlineB,
                      active ? 0.72f : 0.22f );
        if ( trackName == RunReplayTrack::Solver && futureTimelineVisible )
        {
            const float presentX = track.x + track.w * solverPresentT;
            draw.Rect( presentX - 1.0f, track.y - 6.0f, 2.0f, track.h + 12.0f, 0.92f, 1.0f, 0.84f, 0.86f );
            draw.Rect( presentX - 4.0f, track.y - 8.0f, 8.0f, 2.0f, 0.92f, 1.0f, 0.84f, 0.70f );
            draw.Rect( presentX - 4.0f, track.y + track.h + 6.0f, 8.0f, 2.0f, 0.92f, 1.0f, 0.84f, 0.70f );
        }

        draw.RoundedRect( saveButton.x,
                          saveButton.y,
                          saveButton.w,
                          saveButton.h,
                          4.0f,
                          saveR,
                          saveG,
                          saveB,
                          saveEnabled ? 0.96f : 0.34f );
        draw.Outline( saveButton.x,
                      saveButton.y,
                      saveButton.w,
                      saveButton.h,
                      outlineR,
                      outlineG,
                      outlineB,
                      saveEnabled ? ( saveHover || saveFeedback ? 0.74f : 0.36f ) : 0.16f );

        const float iconX = saveButton.x + 6.0f;
        const float iconY = saveButton.y + 5.0f;
        const float iconW = 10.0f;
        const float iconH = 12.0f;
        const float iconA = saveEnabled ? 0.96f : 0.34f;
        draw.Outline( iconX, iconY, iconW, iconH, 0.88f, 0.97f, 1.0f, iconA );
        draw.Rect( iconX + 2.0f, iconY + 2.0f, iconW - 4.0f, 3.0f, 0.88f, 0.97f, 1.0f, iconA * 0.73f );
        draw.Rect( iconX + 3.0f, iconY + 8.0f, iconW - 6.0f, 3.0f, 0.88f, 0.97f, 1.0f, iconA * 0.85f );

        draw.RoundedRect( loadButton.x,
                          loadButton.y,
                          loadButton.w,
                          loadButton.h,
                          4.0f,
                          loadHover ? 0.18f : 0.08f,
                          loadHover ? 0.28f : 0.13f,
                          loadHover ? 0.44f : 0.20f,
                          0.92f );
        draw.Outline( loadButton.x,
                      loadButton.y,
                      loadButton.w,
                      loadButton.h,
                      outlineR,
                      outlineG,
                      outlineB,
                      loadHover ? 0.72f : 0.34f );
        draw.Text( loadButton.x + 9.0f, loadButton.y + 5.0f, 9.5f, 0.78f, 0.90f, 1.0f, "LOAD" );
    };

    if ( loadedPresentation )
    {
        drawReplayRow( RunReplayTrack::Presentation, 0.42f, 0.62f, 1.0f, 0.56f, 0.70f, 1.0f, false );
    }
    else
    {
        drawReplayRow( RunReplayTrack::Solver, 0.30f, 0.93f, 0.72f, 0.48f, 0.86f, 0.74f, true );
    }

    if ( loadedPresentation )
    {
        Text2d::FlushQuads();
        Text2d::FlushText();
        return;
    }

    const UI::UIRect predictToggle = ReplayScrubberPredictToggleRect( screenW, screenH );
    const UI::UIRect predict = ReplayScrubberPredictControlRect( screenW, screenH );
    const UI::UIRect predictHorizon = ReplayScrubberPredictHorizonRect( screenW, screenH );
    const UI::UIRect ragdollVisualToggle = ReplayScrubberRagdollVisualToggleRect( screenW, screenH );
    const bool predictHover = replayRuntime.Prediction().horizonHovered || replayRuntime.Prediction().horizonDragging;
    const bool predictEnabled = replayRuntime.Prediction().enabled;
    const bool ragdollVisualsEnabled = replayRuntime.Prediction().ragdollVisualsEnabled;
    const float predictSeconds = std::clamp( replayRuntime.Prediction().horizonSeconds,
                                             REPLAY_PREDICTION_MIN_SECONDS,
                                             REPLAY_PREDICTION_MAX_SECONDS );
    const float predictBackR = predictEnabled ? 0.08f : 0.055f;
    const float predictBackG = predictEnabled ? 0.24f : 0.08f;
    const float predictBackB = predictEnabled ? 0.16f : 0.105f;
    draw.RoundedRect( predictToggle.x,
                      predictToggle.y,
                      predictToggle.w,
                      predictToggle.h,
                      4.0f,
                      predictBackR + ( replayRuntime.Prediction().checkboxHovered ? 0.07f : 0.0f ),
                      predictBackG + ( replayRuntime.Prediction().checkboxHovered ? 0.07f : 0.0f ),
                      predictBackB + ( replayRuntime.Prediction().checkboxHovered ? 0.07f : 0.0f ),
                      0.88f );
    draw.Outline( predictToggle.x,
                  predictToggle.y,
                  predictToggle.w,
                  predictToggle.h,
                  0.62f,
                  0.86f,
                  0.78f,
                  replayRuntime.Prediction().checkboxHovered || predictEnabled ? 0.72f : 0.34f );
    const float checkX = predictToggle.x + 7.0f;
    const float checkY = predictToggle.y + 5.0f;
    draw.Outline( checkX, checkY, 10.0f, 10.0f, 0.82f, 0.94f, 0.90f, 0.82f );
    if ( predictEnabled )
    {
        draw.Rect( checkX + 2.0f, checkY + 2.0f, 6.0f, 6.0f, 0.38f, 1.0f, 0.58f, 0.95f );
    }
    draw.Text( predictToggle.x + 23.0f,
               predictToggle.y + 4.5f,
               9.5f,
               predictEnabled ? 0.70f : 0.60f,
               predictEnabled ? 1.0f : 0.72f,
               predictEnabled ? 0.78f : 0.76f,
               "PREDICT" );

    draw.RoundedRect( predict.x,
                      predict.y,
                      predict.w,
                      predict.h,
                      4.0f,
                      predictBackR + ( predictHover ? 0.07f : 0.0f ),
                      predictBackG + ( predictHover ? 0.07f : 0.0f ),
                      predictBackB + ( predictHover ? 0.07f : 0.0f ),
                      0.88f );
    draw.Outline( predict.x,
                  predict.y,
                  predict.w,
                  predict.h,
                  0.62f,
                  0.86f,
                  0.78f,
                  predictHover || predictEnabled ? 0.72f : 0.34f );

    char predictSecondsLabel[16] = {};
    sprintf_s( predictSecondsLabel, sizeof( predictSecondsLabel ), "%.0fs", static_cast<double>( predictSeconds ) );
    const float horizonT = ReplayPredictionHorizonT( predictSeconds );
    const float horizonFillW = (std::max)( 4.0f, predictHorizon.w * horizonT );
    const float horizonKnobX = predictHorizon.x + predictHorizon.w * horizonT;
    draw.RoundedRect( predictHorizon.x,
                      predictHorizon.y,
                      predictHorizon.w,
                      predictHorizon.h,
                      4.0f,
                      0.10f,
                      0.14f,
                      0.15f,
                      0.86f );
    draw.RoundedRect( predictHorizon.x,
                      predictHorizon.y,
                      horizonFillW,
                      predictHorizon.h,
                      4.0f,
                      0.34f,
                      0.95f,
                      0.62f,
                      predictEnabled ? 0.86f : 0.48f );
    draw.RoundedRect(
        horizonKnobX - 4.0f,
        predictHorizon.y - 3.0f,
        8.0f,
        14.0f,
        3.0f,
        predictEnabled ? 0.88f : 0.56f,
        predictEnabled ? 1.0f : 0.62f,
        predictEnabled ? 0.82f : 0.64f,
        replayRuntime.Prediction().horizonHovered || replayRuntime.Prediction().horizonDragging ? 0.98f : 0.86f );
    draw.Text( predictHorizon.x + predictHorizon.w + 8.0f,
               predict.y + 4.5f,
               8.5f,
               predictEnabled ? 0.90f : 0.64f,
               predictEnabled ? 1.0f : 0.74f,
               predictEnabled ? 0.88f : 0.76f,
               predictSecondsLabel );

    draw.RoundedRect( ragdollVisualToggle.x,
                      ragdollVisualToggle.y,
                      ragdollVisualToggle.w,
                      ragdollVisualToggle.h,
                      4.0f,
                      ragdollVisualsEnabled ? 0.12f : 0.055f,
                      ragdollVisualsEnabled ? 0.20f : 0.08f,
                      ragdollVisualsEnabled ? 0.26f : 0.105f,
                      0.88f );
    draw.Outline( ragdollVisualToggle.x,
                  ragdollVisualToggle.y,
                  ragdollVisualToggle.w,
                  ragdollVisualToggle.h,
                  0.56f,
                  0.76f,
                  0.92f,
                  replayRuntime.Prediction().ragdollVisualsHovered || ragdollVisualsEnabled ? 0.72f : 0.32f );
    const float ragdollCheckX = ragdollVisualToggle.x + 7.0f;
    const float ragdollCheckY = ragdollVisualToggle.y + 5.0f;
    draw.Outline( ragdollCheckX, ragdollCheckY, 10.0f, 10.0f, 0.72f, 0.86f, 0.98f, 0.82f );
    if ( ragdollVisualsEnabled )
    {
        draw.Rect( ragdollCheckX + 2.0f, ragdollCheckY + 2.0f, 6.0f, 6.0f, 0.42f, 0.82f, 1.0f, 0.95f );
    }
    draw.Text( ragdollVisualToggle.x + 23.0f,
               ragdollVisualToggle.y + 4.5f,
               9.0f,
               ragdollVisualsEnabled ? 0.76f : 0.58f,
               ragdollVisualsEnabled ? 0.92f : 0.68f,
               ragdollVisualsEnabled ? 1.0f : 0.74f,
               "RAGDOLL" );

    Text2d::FlushQuads();
    Text2d::FlushText();
}

void RenderReplayCauseTreeOverlay( const ReplayOverlayRenderContext& context )
{
    PROFILE_SCOPED( "Frame/Replay/CauseTree/Overlay" );
    ReplayRuntime& replayRuntime = context.replayRuntime;
    const int screenW = context.screenW;
    const int screenH = context.screenH;
    if ( screenW <= 0 || screenH <= 0 || !replayRuntime.BuildCauseTreeRows( context.models ) )
    {
        return;
    }

    // Invariant: row generation, scrolling, and drawing use the same mutable
    // cause-tree state. Clamp window geometry before deriving visible rows so
    // scroll offsets cannot point outside the rendered content.
    EnsureReplayCauseWindowPlacement( replayRuntime.CauseTree(), screenW, screenH );
    const UI::UIRect panel = ReplayCauseWindowRect( replayRuntime.CauseTree() );
    const UI::UIRect title = ReplayCauseWindowTitleRect( replayRuntime.CauseTree() );
    const UI::UIRect content = ReplayCauseWindowContentRect( replayRuntime.CauseTree() );
    const UI::UIRect resize = ReplayCauseWindowResizeRect( replayRuntime.CauseTree() );

    const UI::UIDrawContext draw( screenW, screenH );
    draw.RoundedRect( panel.x, panel.y, panel.w, panel.h, 7.0f, 0.014f, 0.018f, 0.024f, 0.88f );
    draw.Outline( panel.x, panel.y, panel.w, panel.h, 0.36f, 0.54f, 0.62f, 0.50f );
    draw.Rect( title.x, title.y + title.h - 1.0f, title.w, 1.0f, 0.36f, 0.54f, 0.62f, 0.35f );
    draw.Text( panel.x + 12.0f, panel.y + 10.0f, 13.5f, 0.82f, 0.94f, 1.0f, "REPLAY CAMERA" );
    draw.Text( panel.x + 136.0f, panel.y + 12.0f, 11.0f, 0.58f, 0.70f, 0.78f, "CAUSE" );

    const bool predictionRows =
        !replayRuntime.CauseTree().rows.empty() && replayRuntime.CauseTree().rows.front().prediction;
    const char* sourceLabel = predictionRows ? "PREDICT" : "REPLAY";
    const float sourceW = Text2d::MeasureText( 9.5f, sourceLabel );
    draw.RoundedRect( panel.x + panel.w - sourceW - 26.0f,
                      panel.y + 9.0f,
                      sourceW + 14.0f,
                      18.0f,
                      4.0f,
                      predictionRows ? 0.08f : 0.08f,
                      predictionRows ? 0.30f : 0.18f,
                      predictionRows ? 0.17f : 0.27f,
                      0.70f );
    draw.Text( panel.x + panel.w - sourceW - 19.0f,
               panel.y + 13.0f,
               9.5f,
               predictionRows ? 0.62f : 0.62f,
               predictionRows ? 1.0f : 0.86f,
               predictionRows ? 0.72f : 1.0f,
               sourceLabel );

    draw.Rect( content.x, content.y, content.w, content.h, 0.02f, 0.026f, 0.034f, 0.36f );

    const ReplaySolverFrameSample* scrubSample = replayRuntime.CurrentSolverScrubSample();
    const ReplayFrameIndex presentFrame = scrubSample ? scrubSample->frameIndex : 0;

    auto truncateText = []( const char* src, char* dst, std::size_t dstSize, int maxChars ) -> void
    {
        if ( dstSize == 0 )
        {
            return;
        }
        dst[0] = '\0';
        if ( !src )
        {
            return;
        }
        maxChars = (std::max)( 4, maxChars );
        strncpy_s( dst, dstSize, src, _TRUNCATE );
        if ( static_cast<int>( strlen( dst ) ) > maxChars )
        {
            const int end = (std::min)( maxChars, static_cast<int>( dstSize ) - 1 );
            if ( end >= 4 )
            {
                dst[end - 3] = '.';
                dst[end - 2] = '.';
                dst[end - 1] = '.';
                dst[end] = '\0';
            }
        }
    };

    const float rowAreaW = content.w - 12.0f;
    const int firstRow =
        (std::max)( 0,
                    static_cast<int>( floorf( replayRuntime.CauseTree().scrollY / REPLAY_CAUSE_WINDOW_ROW_HEIGHT ) ) );
    const int rowCount = static_cast<int>( replayRuntime.CauseTree().rows.size() );
    for ( int rowIndex = firstRow; rowIndex < rowCount; ++rowIndex )
    {
        const RunReplayCauseTreeRow& row = replayRuntime.CauseTree().rows[static_cast<std::size_t>( rowIndex )];
        const float rowY = content.y + static_cast<float>( rowIndex ) * REPLAY_CAUSE_WINDOW_ROW_HEIGHT -
                           replayRuntime.CauseTree().scrollY;
        if ( rowY + REPLAY_CAUSE_WINDOW_ROW_HEIGHT < content.y )
        {
            continue;
        }
        if ( rowY + REPLAY_CAUSE_WINDOW_ROW_HEIGHT > content.y + content.h )
        {
            break;
        }

        const UI::UIRect rowRect = { content.x + 2.0f, rowY, rowAreaW, REPLAY_CAUSE_WINDOW_ROW_HEIGHT - 2.0f };
        const bool hovered = rowIndex == replayRuntime.CauseTree().hoveredRow;
        const bool selected = rowIndex == replayRuntime.CauseTree().selectedRow;
        if ( hovered || selected )
        {
            draw.RoundedRect( rowRect.x,
                              rowRect.y,
                              rowRect.w,
                              rowRect.h,
                              4.0f,
                              selected ? 0.12f : 0.08f,
                              selected ? 0.30f : 0.18f,
                              selected ? 0.22f : 0.24f,
                              hovered ? 0.82f : 0.56f );
        }

        const float indent = (std::min)( rowRect.w * 0.40f, static_cast<float>( row.depth ) * 16.0f );
        if ( row.depth > 0 )
        {
            const float lineX = rowRect.x + 8.0f + indent - 9.0f;
            draw.Rect( lineX, rowRect.y + 4.0f, 1.0f, rowRect.h - 8.0f, 0.62f, 0.68f, 0.72f, 0.32f );
            draw.Rect( lineX, rowRect.y + rowRect.h * 0.5f, 8.0f, 1.0f, 0.62f, 0.68f, 0.72f, 0.32f );
        }

        char prefix[32] = {};
        switch ( row.kind )
        {
        case RunReplayCauseTreeRowKind::Body:
            if ( row.depth == 0 )
            {
                strncpy_s( prefix, sizeof( prefix ), "ROOT", _TRUNCATE );
            }
            else
            {
                double secondsToHit = 0.0;
                if ( row.prediction )
                {
                    secondsToHit = static_cast<double>( row.firstFrame ) * PHYSICS_FIXED_DT;
                }
                else if ( row.firstFrame > presentFrame )
                {
                    secondsToHit = static_cast<double>( row.firstFrame - presentFrame ) * PHYSICS_FIXED_DT;
                }
                sprintf_s( prefix, sizeof( prefix ), "+%.2fs", secondsToHit );
            }
            break;
        case RunReplayCauseTreeRowKind::Manifold:
            strncpy_s( prefix, sizeof( prefix ), "MANIFOLD", _TRUNCATE );
            break;
        case RunReplayCauseTreeRowKind::SolverRow:
            strncpy_s( prefix, sizeof( prefix ), "ROW", _TRUNCATE );
            break;
        case RunReplayCauseTreeRowKind::PredictionContact:
            strncpy_s( prefix, sizeof( prefix ), "CONTACT", _TRUNCATE );
            break;
        }

        char label[144] = {};
        sprintf_s( label, sizeof( label ), "%s  %s", prefix, row.name );
        char clippedLabel[144] = {};
        const int labelChars = static_cast<int>( ( rowRect.w - indent - 18.0f ) / 8.4f );
        truncateText( label, clippedLabel, sizeof( clippedLabel ), labelChars );
        char clippedDetail[160] = {};
        const int detailChars = static_cast<int>( ( rowRect.w - indent - 18.0f ) / 7.2f );
        truncateText( row.detail, clippedDetail, sizeof( clippedDetail ), detailChars );

        float markerR = 0.94f;
        float markerG = 1.0f;
        float markerB = 0.74f;
        if ( row.kind == RunReplayCauseTreeRowKind::Manifold )
        {
            markerR = 0.20f;
            markerG = 0.90f;
            markerB = 1.0f;
        }
        else if ( row.kind == RunReplayCauseTreeRowKind::SolverRow )
        {
            markerR = 1.0f;
            markerG = 0.42f;
            markerB = 0.18f;
        }
        else if ( row.kind == RunReplayCauseTreeRowKind::PredictionContact )
        {
            markerR = 0.38f;
            markerG = 1.0f;
            markerB = 0.58f;
        }

        const float markerX = rowRect.x + 8.0f + indent;
        const float markerY = rowRect.y + 8.0f;
        draw.Rect( markerX, markerY, 6.0f, 6.0f, markerR, markerG, markerB, 0.92f );
        draw.Text( markerX + 11.0f,
                   rowRect.y + 4.0f,
                   12.4f,
                   row.kind == RunReplayCauseTreeRowKind::Body ? 0.88f : 0.78f,
                   row.kind == RunReplayCauseTreeRowKind::Body ? 1.0f : 0.86f,
                   row.kind == RunReplayCauseTreeRowKind::Body ? 0.86f : 0.82f,
                   clippedLabel );
        if ( clippedDetail[0] != '\0' )
        {
            draw.Text( markerX + 11.0f, rowRect.y + 22.0f, 10.4f, 0.56f, 0.66f, 0.72f, clippedDetail );
        }
    }

    const float maxScroll = ReplayCauseWindowMaxScroll( replayRuntime.CauseTree() );
    if ( maxScroll > 0.0f )
    {
        const float trackX = content.x + content.w - 5.0f;
        draw.Rect( trackX, content.y + 3.0f, 3.0f, content.h - 6.0f, 0.16f, 0.22f, 0.28f, 0.72f );
        const float contentHeight = ReplayCauseWindowContentHeight( replayRuntime.CauseTree() );
        const float knobH = (std::max)( 24.0f, ( content.h / contentHeight ) * ( content.h - 6.0f ) );
        const float knobY =
            content.y + 3.0f + ( replayRuntime.CauseTree().scrollY / maxScroll ) * ( content.h - 6.0f - knobH );
        draw.RoundedRect( trackX - 1.0f, knobY, 5.0f, knobH, 2.0f, 0.42f, 0.60f, 0.68f, 0.78f );
    }

    draw.Rect( resize.x + 4.0f, resize.y + resize.h - 5.0f, resize.w - 7.0f, 1.0f, 0.56f, 0.70f, 0.76f, 0.68f );
    draw.Rect( resize.x + resize.w - 5.0f, resize.y + 4.0f, 1.0f, resize.h - 7.0f, 0.56f, 0.70f, 0.76f, 0.68f );

    Text2d::FlushQuads();
    Text2d::FlushText();
}
} // namespace SkullbonezCore::Basics::ReplayOverlay
