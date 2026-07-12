/*
File: SkullbonezSource/Runtime/Replay/ReplayOverlayRenderer.cpp
Purpose:
  Draws replay scrubber and cause-tree overlays from replay-owned state.

Summary:
  Replay overlay rendering is a late UI pass. Keep the same screen-space layout
  and pointer eligibility as replay input by rebuilding the same fixed-capacity
  surfaces from ReplayOverlayLayout.

Glossary:
  UI (User Interface): Runtime controls and overlays drawn over the 3D scene.
  Scrubber: Replay timeline UI for retained samples, loaded artifacts, and
    future prediction frames.
  Cause tree: Contact, solver, and predicted-motion explanation view rooted at
    the selected replay body.
  Presentation sample: Render-only replay pose used for visual scrub previews.
  Solver sample: Replay frame with solver snapshot data used for restore and
    inspection.

Invariants:
  - Drawn controls use the same surface rows and pointer-block fact as input, so
    visible hover and actionable hit state stay identical.
  - Overlay rendering reads replay state only; replay mutation belongs to input
    and runtime replay helpers.

Related:
  - SkullbonezSource/Runtime/Replay/ReplayOverlayRenderer.h
  - SkullbonezSource/Runtime/Replay/ReplayOverlayLayout.h
*/
#include "ReplayOverlayRenderer.h"
#include "../../Core/FatalError.h"
#include "../../Assets/AssetKeys.h"
#include "ReplayOverlayLayout.h"
#include "../../Core/Common.h"
#include "../../Core/Profiler.h"
#include "../../Physics/PhysicsTimestep.h"
#include "../../Rendering/Text.h"
#include "../../UI/UIDraw.h"
#include "../../UI/UIStyle.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>

namespace SkullbonezCore::Basics::ReplayOverlay
{
using Text::Text2d;

namespace
{
bool ReplayPredictionContactsIncomplete( const ReplayRuntime& replayRuntime )
{
    // Concept: contact payloads are optional prediction evidence. The root path
    // can still be correct when contact-tree rows are partial, but the overlay
    // should label that loss instead of implying a complete causal tree.
    const RunReplayPredictionState& prediction = replayRuntime.Prediction();
    const std::vector<RunReplayPredictionFrame>* frames = &prediction.simulation.frames;
    std::size_t frameCount = frames->size();
    if ( prediction.BuildPrefixShouldBePresented() )
    {
        frames = &prediction.build.buildFrames;
        frameCount = prediction.PublishedBuildFrameCount();
    }
    frameCount = (std::min)( frameCount, frames->size() );
    for ( std::size_t i = 0; i < frameCount; ++i )
    {
        if ( ( *frames )[i].contactsIncomplete )
        {
            return true;
        }
    }
    return false;
}
} // namespace

// Concept: the replay overlay is a read-only projection of replay state.
//
// Input code owns mutations such as dragging, toggling prediction, and branch
// creation. This pass samples the current state and turns it into UI quads and
// text so rendering cannot accidentally advance or rewrite replay timelines.
void RenderReplayScrubberOverlay( const ReplayOverlayRenderContext& context )
{
    PROFILE_SCOPED( "Frame/Replay/ScrubberOverlay" );
    ReplayRuntime& replayRuntime = context.replayRuntime;
    Rendering::IRenderCommandContext& renderCommands = context.renderCommands;
    // Why: the cause tree is an inspection tool, not a child of the scrubber.
    // Draw it even when the scrubber itself is hidden by UI/editor policy.
    RenderReplayCauseTreeOverlay( context );

    if ( !replayRuntime.ShouldRenderScrubber( context.editorModeEnabled,
                                              context.uiVisible,
                                              context.uiMinimized,
                                              context.gesture ) )
    {
        return;
    }

    const int screenW = context.screenW;
    const int screenH = context.screenH;
    const bool loadedPresentation = replayRuntime.HasLoadedPresentation();
    const ReplayRecorderStats solverReplayStats = replayRuntime.Solver().GetStats();
    const bool solverReplayEnabled = solverReplayStats.enabled;
    // Why: the replay bar may be visible while force-paused before two solver
    // frames exist. Retained-history tools stay dimmed, but prediction can run
    // from the current live solver state once scene physics is available.
    const bool solverToolsEnabled = solverReplayEnabled && solverReplayStats.sampleCount >= 2;
    const bool predictionToolsEnabled = solverReplayEnabled && context.scenePhysicsEnabled;
    if ( screenW <= 0 || screenH <= 0 || ( !loadedPresentation && !solverReplayEnabled ) )
    {
        return;
    }

    const RunReplayTrack activeTrack = loadedPresentation ? RunReplayTrack::Presentation : RunReplayTrack::Solver;
    const ReplayScrubberSurfaceInput surfaceInput = DescribeReplayScrubberSurface( replayRuntime,
                                                                                   context.scenePhysicsEnabled,
                                                                                   false,
                                                                                   screenW,
                                                                                   screenH,
                                                                                   context.gesture );
    ReplayScrubberSurface surface;
    BuildReplayScrubberSurface( surfaceInput, surface );
    surface.ResolvePointer( replayRuntime.Scrubber().mouseX, replayRuntime.Scrubber().mouseY );
    const auto control = [&]( ReplayScrubberControl id ) -> const RuntimeUiControl&
    {
        const RuntimeUiControl* row = surface.Find( ReplayScrubberControlId( id ) );
        if ( !row )
        {
            SB_FATAL( "ReplayScrubberSurface",
                      "Render snapshot is missing replay scrubber control id=%u.",
                      static_cast<uint32_t>( id ) );
        }
        return *row;
    };
    const auto isHotControl = [&]( ReplayScrubberControl id )
    { return surface.hasHotControl && surface.hotControl == ReplayScrubberControlId( id ); };
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

    const UI::UIDrawContext draw( screenW, screenH, nullptr, &renderCommands );
    const UI::UIRect panel = control( ReplayScrubberControl::Panel ).drawRect;
    const UI::Style::UIPalette& palette = UI::Style::Palette();
    const UI::Style::UIRadii& radii = UI::Style::Radii();
    const float fade = std::clamp( replayRuntime.Scrubber().visibleAlpha, 0.0f, 1.0f );
    if ( fade <= REPLAY_SCRUBBER_FADE_EPSILON )
    {
        return;
    }
    auto fadeA = [fade]( float alpha ) -> float { return alpha * fade; };
    auto fadeC = [fade]( float channel ) -> float { return channel * fade; };
    auto drawText = [&]( float x, float y, float pxSize, float r, float g, float b, const char* value )
    { draw.Text( x, y, pxSize, fadeC( r ), fadeC( g ), fadeC( b ), value ); };
    const bool live = !loadedPresentation && ReplayRuntime::AtPresentTrackPosition( t, solverPresentT ) &&
                      !replayRuntime.Scrubber().historicalSamplePaused;
    const double now = context.nowSeconds;
    const char* sourceLabel = loadedPresentation ? "V2 FILE" : "SOLVER";
    const bool branchEnabled =
        replayRuntime.Scrubber().historicalSamplePaused &&
        ( ( loadedPresentation && replayRuntime.CurrentScrubSample() != nullptr ) ||
          ( !loadedPresentation && solverToolsEnabled && replayRuntime.CurrentSolverScrubSample() != nullptr ) );

    UI::Style::UIColor panelFill = palette.windowSubtle;
    panelFill.a = fadeA( 0.92f );
    UI::Style::UIColor panelBorder = palette.innerBorder;
    panelBorder.a = fadeA( 0.42f );
    draw.RoundedPanel( panel, radii.control, panelFill, panelBorder );
    drawText( panel.x + 16.0f,
              panel.y + 19.0f,
              10.5f,
              palette.textSecondary.r,
              palette.textSecondary.g,
              palette.textSecondary.b,
              sourceLabel );
    const float labelW = Text2d::MeasureText( 11.0f, timeLabel );
    drawText( panel.x + panel.w - labelW - 16.0f,
              panel.y + 18.0f,
              11.0f,
              live ? palette.accent.r : palette.warningAccent.r,
              live ? palette.accent.g : palette.warningAccent.g,
              live ? palette.accent.b : palette.warningAccent.b,
              timeLabel );

    {
        const UI::UIRect branchButton = control( ReplayScrubberControl::Branch ).drawRect;
        const bool branchHover = branchEnabled && isHotControl( ReplayScrubberControl::Branch );
        draw.RoundedRect( branchButton.x,
                          branchButton.y,
                          branchButton.w,
                          branchButton.h,
                          radii.smallButton,
                          branchHover ? palette.controlHover.r : palette.control.r,
                          branchHover ? palette.controlHover.g : palette.control.g,
                          branchHover ? palette.controlHover.b : palette.control.b,
                          fadeA( branchEnabled ? 0.94f : 0.42f ) );
        draw.Outline( branchButton.x,
                      branchButton.y,
                      branchButton.w,
                      branchButton.h,
                      palette.accent.r,
                      palette.accent.g,
                      palette.accent.b,
                      fadeA( branchEnabled ? ( branchHover ? 0.84f : 0.42f ) : 0.18f ) );
        drawText( branchButton.x + 12.0f,
                  branchButton.y + 4.5f,
                  9.5f,
                  branchEnabled ? palette.textPrimary.r : palette.textMuted.r,
                  branchEnabled ? palette.textPrimary.g : palette.textMuted.g,
                  branchEnabled ? palette.textPrimary.b : palette.textMuted.b,
                  "BRANCH" );
    }

    if ( !loadedPresentation )
    {
        const UI::UIRect pauseButton = control( ReplayScrubberControl::Pause ).drawRect;
        const bool liveAdvanceHeld = replayRuntime.Scrubber().liveAdvanceHeld;
        const bool pauseHover = solverToolsEnabled && isHotControl( ReplayScrubberControl::Pause );
        draw.RoundedRect( pauseButton.x,
                          pauseButton.y,
                          pauseButton.w,
                          pauseButton.h,
                          radii.smallButton,
                          pauseHover ? palette.controlHover.r : palette.control.r,
                          pauseHover ? palette.controlHover.g : palette.control.g,
                          pauseHover ? palette.controlHover.b : palette.control.b,
                          fadeA( solverToolsEnabled ? ( pauseHover || liveAdvanceHeld ? 0.94f : 0.78f ) : 0.38f ) );
        draw.Outline( pauseButton.x,
                      pauseButton.y,
                      pauseButton.w,
                      pauseButton.h,
                      liveAdvanceHeld ? palette.accentStrong.r : palette.accent.r,
                      liveAdvanceHeld ? palette.accentStrong.g : palette.accent.g,
                      liveAdvanceHeld ? palette.accentStrong.b : palette.accent.b,
                      fadeA( solverToolsEnabled ? ( pauseHover || liveAdvanceHeld ? 0.78f : 0.36f ) : 0.14f ) );
        drawText( pauseButton.x + 9.0f,
                  pauseButton.y + 5.0f,
                  9.5f,
                  !solverToolsEnabled ? palette.textMuted.r
                                      : ( liveAdvanceHeld ? palette.accentStrong.r : palette.textSecondary.r ),
                  !solverToolsEnabled ? palette.textMuted.g
                                      : ( liveAdvanceHeld ? palette.accentStrong.g : palette.textSecondary.g ),
                  !solverToolsEnabled ? palette.textMuted.b
                                      : ( liveAdvanceHeld ? palette.accentStrong.b : palette.textSecondary.b ),
                  liveAdvanceHeld ? "PLAY" : "PAUSE" );

        {
            PROFILE_SCOPED( "Frame/Replay/ScrubberOverlay/VelocityEditControls" );
            const UI::UIRect velocityEdit = control( ReplayScrubberControl::VelocityEdit ).drawRect;
            const bool velocityEditEnabled = solverToolsEnabled && replayRuntime.VelocityEdit().enabled;
            const bool velocityEditHover = solverToolsEnabled && isHotControl( ReplayScrubberControl::VelocityEdit );
            draw.RoundedRect(
                velocityEdit.x,
                velocityEdit.y,
                velocityEdit.w,
                velocityEdit.h,
                radii.smallButton,
                velocityEditHover ? palette.controlHover.r : palette.control.r,
                velocityEditHover ? palette.controlHover.g : palette.control.g,
                velocityEditHover ? palette.controlHover.b : palette.control.b,
                fadeA( solverToolsEnabled ? ( velocityEditHover || velocityEditEnabled ? 0.94f : 0.78f ) : 0.38f ) );
            draw.Outline(
                velocityEdit.x,
                velocityEdit.y,
                velocityEdit.w,
                velocityEdit.h,
                palette.warningAccent.r,
                palette.warningAccent.g,
                palette.warningAccent.b,
                fadeA( solverToolsEnabled ? ( velocityEditHover || velocityEditEnabled ? 0.78f : 0.34f ) : 0.14f ) );
            const float checkX = velocityEdit.x + 7.0f;
            const float checkY = velocityEdit.y + 5.0f;
            draw.Outline( checkX,
                          checkY,
                          10.0f,
                          10.0f,
                          palette.warningAccent.r,
                          palette.warningAccent.g,
                          palette.warningAccent.b,
                          fadeA( solverToolsEnabled ? 0.82f : 0.28f ) );
            if ( velocityEditEnabled )
            {
                draw.Rect( checkX + 2.0f,
                           checkY + 2.0f,
                           6.0f,
                           6.0f,
                           palette.warningAccent.r,
                           palette.warningAccent.g,
                           palette.warningAccent.b,
                           fadeA( 0.95f ) );
            }
            drawText( velocityEdit.x + 23.0f,
                      velocityEdit.y + 4.5f,
                      9.5f,
                      !solverToolsEnabled ? palette.textMuted.r
                                          : ( velocityEditEnabled ? palette.warningAccent.r : palette.textSecondary.r ),
                      !solverToolsEnabled ? palette.textMuted.g
                                          : ( velocityEditEnabled ? palette.warningAccent.g : palette.textSecondary.g ),
                      !solverToolsEnabled ? palette.textMuted.b
                                          : ( velocityEditEnabled ? palette.warningAccent.b : palette.textSecondary.b ),
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
        const UI::UIRect track = control( ReplayScrubberControl::ScrubTrack ).drawRect;
        const UI::UIRect saveButton = control( ReplayScrubberControl::Save ).drawRect;
        const UI::UIRect loadButton = control( ReplayScrubberControl::Load ).drawRect;
        const float rowT = std::clamp( replayRuntime.TrackPosition( trackName ), 0.0f, 1.0f );
        const float fillW = (std::max)( REPLAY_SCRUBBER_TRACK_HEIGHT, track.w * rowT );
        const float knobX = track.x + track.w * rowT;
        const bool active = activeTrack == trackName;
        const bool inactiveDuringScrub = ( context.gesture == RuntimeInteractionGestureKind::ReplayScrubDrag ||
                                           replayRuntime.Scrubber().historicalSamplePaused ) &&
                                         !active;
        const bool saveHover = saveEnabled && isHotControl( ReplayScrubberControl::Save );
        const bool saveFeedback =
            replayRuntime.Scrubber().saveMessage[0] != '\0' && replayRuntime.Scrubber().saveMessageUntil >= now;
        const bool saveFailed = saveFeedback && strstr( replayRuntime.Scrubber().saveMessage, "FAILED" ) != nullptr;
        const bool loadHover = isHotControl( ReplayScrubberControl::Load );
        const float saveR = saveFeedback ? ( saveFailed ? 0.48f : palette.accent.r )
                                         : ( saveHover ? palette.controlHover.r : palette.control.r );
        const float saveG = saveFeedback ? ( saveFailed ? 0.12f : palette.accent.g )
                                         : ( saveHover ? palette.controlHover.g : palette.control.g );
        const float saveB = saveFeedback ? ( saveFailed ? 0.12f : palette.accent.b )
                                         : ( saveHover ? palette.controlHover.b : palette.control.b );

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
                          fadeA( inactiveDuringScrub ? 0.74f : 0.92f ) );
        draw.RoundedRect( track.x,
                          track.y,
                          fillW,
                          track.h,
                          track.h * 0.5f,
                          rowFillR,
                          rowFillG,
                          rowFillB,
                          fadeA( rowFillA ) );
        if ( trackName == RunReplayTrack::Solver && futureTimelineVisible )
        {
            const float presentX = track.x + track.w * solverPresentT;
            // Why: future prediction is a different timeline region, not just a
            // longer scrub value. Draw the right-hand side after the normal fill
            // so it stays visibly blue even when the selected knob is in future.
            draw.Rect( presentX,
                       track.y,
                       (std::max)( 0.0f, track.x + track.w - presentX ),
                       track.h,
                       0.08f,
                       0.30f,
                       0.92f,
                       fadeA( inactiveDuringScrub ? 0.40f : 0.72f ) );
        }
        draw.RoundedRect( knobX - 6.0f,
                          track.y - 5.0f,
                          12.0f,
                          18.0f,
                          5.0f,
                          active ? 0.98f : 0.52f,
                          active ? 0.98f : 0.56f,
                          active ? 1.0f : 0.60f,
                          fadeA( active ? 0.98f : 0.70f ) );
        draw.Outline( knobX - 6.0f,
                      track.y - 5.0f,
                      12.0f,
                      18.0f,
                      outlineR,
                      outlineG,
                      outlineB,
                      fadeA( active ? 0.72f : 0.22f ) );
        if ( trackName == RunReplayTrack::Solver && futureTimelineVisible )
        {
            const float presentX = track.x + track.w * solverPresentT;
            draw.Rect( presentX - 1.0f, track.y - 6.0f, 2.0f, track.h + 12.0f, 0.92f, 1.0f, 0.84f, fadeA( 0.86f ) );
            draw.Rect( presentX - 4.0f, track.y - 8.0f, 8.0f, 2.0f, 0.92f, 1.0f, 0.84f, fadeA( 0.70f ) );
            draw.Rect( presentX - 4.0f, track.y + track.h + 6.0f, 8.0f, 2.0f, 0.92f, 1.0f, 0.84f, fadeA( 0.70f ) );
        }

        draw.RoundedRect( saveButton.x,
                          saveButton.y,
                          saveButton.w,
                          saveButton.h,
                          4.0f,
                          saveR,
                          saveG,
                          saveB,
                          fadeA( saveEnabled ? 0.96f : 0.34f ) );
        draw.Outline( saveButton.x,
                      saveButton.y,
                      saveButton.w,
                      saveButton.h,
                      outlineR,
                      outlineG,
                      outlineB,
                      fadeA( saveEnabled ? ( saveHover || saveFeedback ? 0.74f : 0.36f ) : 0.16f ) );

        const float iconX = saveButton.x + 6.0f;
        const float iconY = saveButton.y + 5.0f;
        const float iconW = 10.0f;
        const float iconH = 12.0f;
        const float iconA = fadeA( saveEnabled ? 0.96f : 0.34f );
        draw.Outline( iconX, iconY, iconW, iconH, 0.88f, 0.97f, 1.0f, iconA );
        draw.Rect( iconX + 2.0f, iconY + 2.0f, iconW - 4.0f, 3.0f, 0.88f, 0.97f, 1.0f, iconA * 0.73f );
        draw.Rect( iconX + 3.0f, iconY + 8.0f, iconW - 6.0f, 3.0f, 0.88f, 0.97f, 1.0f, iconA * 0.85f );

        draw.RoundedRect( loadButton.x,
                          loadButton.y,
                          loadButton.w,
                          loadButton.h,
                          radii.smallButton,
                          loadHover ? palette.controlHover.r : palette.control.r,
                          loadHover ? palette.controlHover.g : palette.control.g,
                          loadHover ? palette.controlHover.b : palette.control.b,
                          fadeA( 0.92f ) );
        draw.Outline( loadButton.x,
                      loadButton.y,
                      loadButton.w,
                      loadButton.h,
                      outlineR,
                      outlineG,
                      outlineB,
                      fadeA( loadHover ? 0.72f : 0.34f ) );
        drawText( loadButton.x + 9.0f,
                  loadButton.y + 5.0f,
                  9.5f,
                  palette.textPrimary.r,
                  palette.textPrimary.g,
                  palette.textPrimary.b,
                  "LOAD" );
    };

    if ( loadedPresentation )
    {
        drawReplayRow( RunReplayTrack::Presentation,
                       palette.accent.r,
                       palette.accent.g,
                       palette.accent.b,
                       palette.accentStrong.r,
                       palette.accentStrong.g,
                       palette.accentStrong.b,
                       false );
    }
    else
    {
        drawReplayRow( RunReplayTrack::Solver,
                       palette.accent.r,
                       palette.accent.g,
                       palette.accent.b,
                       palette.accentStrong.r,
                       palette.accentStrong.g,
                       palette.accentStrong.b,
                       solverToolsEnabled );
    }

    if ( loadedPresentation )
    {
        Text2d::FlushQuads( renderCommands );
        Text2d::FlushText( renderCommands );
        return;
    }

    const UI::UIRect predictToggle = control( ReplayScrubberControl::PredictionToggle ).drawRect;
    const UI::UIRect predict = control( ReplayScrubberControl::PredictionPanel ).drawRect;
    const UI::UIRect predictHorizon = control( ReplayScrubberControl::PredictionHorizon ).drawRect;
    const UI::UIRect ragdollVisualToggle = control( ReplayScrubberControl::RagdollVisuals ).drawRect;
    const UI::UIRect pastPathToggle = control( ReplayScrubberControl::PastPath ).drawRect;
    const bool predictHover =
        predictionToolsEnabled && ( isHotControl( ReplayScrubberControl::PredictionHorizon ) ||
                                    context.gesture == RuntimeInteractionGestureKind::ReplayPredictionHorizonDrag );
    const bool predictEnabled = predictionToolsEnabled && replayRuntime.Prediction().enabled;
    const bool ragdollVisualsEnabled = predictionToolsEnabled && replayRuntime.Prediction().ragdollVisualsEnabled;
    const bool pastPathToolsEnabled = solverToolsEnabled && replayRuntime.PathVisualizer().hasTarget;
    const bool pastPathEnabled = pastPathToolsEnabled && replayRuntime.PathVisualizer().pastPathVisible;
    const bool predictionContactsIncomplete = ReplayPredictionContactsIncomplete( replayRuntime );
    const float predictSeconds = std::clamp( replayRuntime.Prediction().simulation.horizonSeconds,
                                             REPLAY_PREDICTION_MIN_SECONDS,
                                             REPLAY_PREDICTION_MAX_SECONDS );
    const UI::Style::UIColor predictFill =
        predictionToolsEnabled && isHotControl( ReplayScrubberControl::PredictionToggle ) ? palette.controlHover
                                                                                          : palette.control;
    const UI::Style::UIColor predictControlFill = predictHover ? palette.controlHover : palette.control;
    draw.RoundedRect( predictToggle.x,
                      predictToggle.y,
                      predictToggle.w,
                      predictToggle.h,
                      radii.smallButton,
                      predictFill.r,
                      predictFill.g,
                      predictFill.b,
                      fadeA( predictionToolsEnabled ? 0.88f : 0.38f ) );
    draw.Outline(
        predictToggle.x,
        predictToggle.y,
        predictToggle.w,
        predictToggle.h,
        palette.accent.r,
        palette.accent.g,
        palette.accent.b,
        fadeA( predictionToolsEnabled
                   ? ( isHotControl( ReplayScrubberControl::PredictionToggle ) || predictEnabled ? 0.72f : 0.34f )
                   : 0.14f ) );
    const float checkX = predictToggle.x + 7.0f;
    const float checkY = predictToggle.y + 5.0f;
    draw.Outline( checkX,
                  checkY,
                  10.0f,
                  10.0f,
                  palette.accent.r,
                  palette.accent.g,
                  palette.accent.b,
                  fadeA( predictionToolsEnabled ? 0.82f : 0.28f ) );
    if ( predictEnabled )
    {
        draw.Rect( checkX + 2.0f,
                   checkY + 2.0f,
                   6.0f,
                   6.0f,
                   palette.accentStrong.r,
                   palette.accentStrong.g,
                   palette.accentStrong.b,
                   fadeA( 0.95f ) );
    }
    drawText( predictToggle.x + 23.0f,
              predictToggle.y + 4.5f,
              9.5f,
              !predictionToolsEnabled ? palette.textMuted.r
                                      : ( predictEnabled ? palette.accentStrong.r : palette.textSecondary.r ),
              !predictionToolsEnabled ? palette.textMuted.g
                                      : ( predictEnabled ? palette.accentStrong.g : palette.textSecondary.g ),
              !predictionToolsEnabled ? palette.textMuted.b
                                      : ( predictEnabled ? palette.accentStrong.b : palette.textSecondary.b ),
              "PREDICT" );

    draw.RoundedRect( predict.x,
                      predict.y,
                      predict.w,
                      predict.h,
                      radii.smallButton,
                      predictControlFill.r,
                      predictControlFill.g,
                      predictControlFill.b,
                      fadeA( predictionToolsEnabled ? 0.88f : 0.38f ) );
    draw.Outline( predict.x,
                  predict.y,
                  predict.w,
                  predict.h,
                  palette.accent.r,
                  palette.accent.g,
                  palette.accent.b,
                  fadeA( predictionToolsEnabled ? ( predictHover || predictEnabled ? 0.72f : 0.34f ) : 0.14f ) );

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
                      fadeA( predictionToolsEnabled ? 0.86f : 0.34f ) );
    draw.RoundedRect( predictHorizon.x,
                      predictHorizon.y,
                      horizonFillW,
                      predictHorizon.h,
                      4.0f,
                      0.34f,
                      0.95f,
                      0.62f,
                      fadeA( predictionToolsEnabled ? ( predictEnabled ? 0.86f : 0.48f ) : 0.20f ) );
    draw.RoundedRect( horizonKnobX - 4.0f,
                      predictHorizon.y - 3.0f,
                      8.0f,
                      14.0f,
                      3.0f,
                      predictEnabled ? 0.88f : 0.56f,
                      predictEnabled ? 1.0f : 0.62f,
                      predictEnabled ? 0.82f : 0.64f,
                      fadeA( predictionToolsEnabled ? ( predictHover ? 0.98f : 0.86f ) : 0.34f ) );
    drawText( predictHorizon.x + predictHorizon.w + 8.0f,
              predict.y + 4.5f,
              8.5f,
              !predictionToolsEnabled ? palette.textMuted.r
                                      : ( predictEnabled ? palette.accentStrong.r : palette.textSecondary.r ),
              !predictionToolsEnabled ? palette.textMuted.g
                                      : ( predictEnabled ? palette.accentStrong.g : palette.textSecondary.g ),
              !predictionToolsEnabled ? palette.textMuted.b
                                      : ( predictEnabled ? palette.accentStrong.b : palette.textSecondary.b ),
              predictSecondsLabel );

    draw.RoundedRect(
        ragdollVisualToggle.x,
        ragdollVisualToggle.y,
        ragdollVisualToggle.w,
        ragdollVisualToggle.h,
        radii.smallButton,
        predictionToolsEnabled && isHotControl( ReplayScrubberControl::RagdollVisuals ) ? palette.controlHover.r
                                                                                        : palette.control.r,
        predictionToolsEnabled && isHotControl( ReplayScrubberControl::RagdollVisuals ) ? palette.controlHover.g
                                                                                        : palette.control.g,
        predictionToolsEnabled && isHotControl( ReplayScrubberControl::RagdollVisuals ) ? palette.controlHover.b
                                                                                        : palette.control.b,
        fadeA( predictionToolsEnabled ? 0.88f : 0.38f ) );
    draw.Outline(
        ragdollVisualToggle.x,
        ragdollVisualToggle.y,
        ragdollVisualToggle.w,
        ragdollVisualToggle.h,
        palette.accent.r,
        palette.accent.g,
        palette.accent.b,
        fadeA( predictionToolsEnabled
                   ? ( isHotControl( ReplayScrubberControl::RagdollVisuals ) || ragdollVisualsEnabled ? 0.72f : 0.32f )
                   : 0.14f ) );
    const float ragdollCheckX = ragdollVisualToggle.x + 7.0f;
    const float ragdollCheckY = ragdollVisualToggle.y + 5.0f;
    draw.Outline( ragdollCheckX,
                  ragdollCheckY,
                  10.0f,
                  10.0f,
                  palette.accent.r,
                  palette.accent.g,
                  palette.accent.b,
                  fadeA( predictionToolsEnabled ? 0.82f : 0.28f ) );
    if ( ragdollVisualsEnabled )
    {
        draw.Rect( ragdollCheckX + 2.0f,
                   ragdollCheckY + 2.0f,
                   6.0f,
                   6.0f,
                   palette.accentStrong.r,
                   palette.accentStrong.g,
                   palette.accentStrong.b,
                   fadeA( 0.95f ) );
    }
    drawText( ragdollVisualToggle.x + 23.0f,
              ragdollVisualToggle.y + 4.5f,
              9.0f,
              !predictionToolsEnabled ? palette.textMuted.r
                                      : ( ragdollVisualsEnabled ? palette.accentStrong.r : palette.textSecondary.r ),
              !predictionToolsEnabled ? palette.textMuted.g
                                      : ( ragdollVisualsEnabled ? palette.accentStrong.g : palette.textSecondary.g ),
              !predictionToolsEnabled ? palette.textMuted.b
                                      : ( ragdollVisualsEnabled ? palette.accentStrong.b : palette.textSecondary.b ),
              "RAGDOLL" );

    draw.RoundedRect( pastPathToggle.x,
                      pastPathToggle.y,
                      pastPathToggle.w,
                      pastPathToggle.h,
                      radii.smallButton,
                      pastPathToolsEnabled && isHotControl( ReplayScrubberControl::PastPath ) ? palette.controlHover.r
                                                                                              : palette.control.r,
                      pastPathToolsEnabled && isHotControl( ReplayScrubberControl::PastPath ) ? palette.controlHover.g
                                                                                              : palette.control.g,
                      pastPathToolsEnabled && isHotControl( ReplayScrubberControl::PastPath ) ? palette.controlHover.b
                                                                                              : palette.control.b,
                      fadeA( pastPathToolsEnabled ? 0.88f : 0.38f ) );
    draw.Outline( pastPathToggle.x,
                  pastPathToggle.y,
                  pastPathToggle.w,
                  pastPathToggle.h,
                  palette.accent.r,
                  palette.accent.g,
                  palette.accent.b,
                  fadeA( pastPathToolsEnabled
                             ? ( isHotControl( ReplayScrubberControl::PastPath ) || pastPathEnabled ? 0.72f : 0.32f )
                             : 0.14f ) );
    const float pastCheckX = pastPathToggle.x + 7.0f;
    const float pastCheckY = pastPathToggle.y + 5.0f;
    draw.Outline( pastCheckX,
                  pastCheckY,
                  10.0f,
                  10.0f,
                  palette.accent.r,
                  palette.accent.g,
                  palette.accent.b,
                  fadeA( pastPathToolsEnabled ? 0.82f : 0.28f ) );
    if ( pastPathEnabled )
    {
        draw.Rect( pastCheckX + 2.0f,
                   pastCheckY + 2.0f,
                   6.0f,
                   6.0f,
                   palette.accentStrong.r,
                   palette.accentStrong.g,
                   palette.accentStrong.b,
                   fadeA( 0.95f ) );
    }
    drawText( pastPathToggle.x + 23.0f,
              pastPathToggle.y + 4.5f,
              9.0f,
              !pastPathToolsEnabled ? palette.textMuted.r
                                    : ( pastPathEnabled ? palette.accentStrong.r : palette.textSecondary.r ),
              !pastPathToolsEnabled ? palette.textMuted.g
                                    : ( pastPathEnabled ? palette.accentStrong.g : palette.textSecondary.g ),
              !pastPathToolsEnabled ? palette.textMuted.b
                                    : ( pastPathEnabled ? palette.accentStrong.b : palette.textSecondary.b ),
              "PAST" );
    if ( predictEnabled )
    {
        const RunReplayPredictionState& predictionState = replayRuntime.Prediction();
        const char* modeLabel = predictionState.build.buildMode == ReplayPredictionBuildMode::Instant     ? "Instant"
                                : predictionState.build.buildMode == ReplayPredictionBuildMode::Amortized ? "Amortized"
                                                                                                          : "Measuring";
        const double ticksPerMs = predictionState.simulation.measuredTicksPerMs.load( std::memory_order_acquire );
        char schedulingLabel[96] = {};
        sprintf_s( schedulingLabel,
                   sizeof( schedulingLabel ),
                   "Prediction: %s | %.0f ticks/ms | %.1f ms rebuild",
                   modeLabel,
                   ticksPerMs,
                   predictionState.build.lastBuildWallMs );
        drawText( predict.x,
                  predict.y + 27.0f,
                  8.0f,
                  palette.textSecondary.r,
                  palette.textSecondary.g,
                  palette.textSecondary.b,
                  schedulingLabel );
    }
    if ( predictionContactsIncomplete )
    {
        drawText( predict.x,
                  predict.y + 38.0f,
                  8.0f,
                  palette.warningAccent.r,
                  palette.warningAccent.g,
                  palette.warningAccent.b,
                  "CONTACTS PARTIAL" );
    }

    Text2d::FlushQuads( renderCommands );
    Text2d::FlushText( renderCommands );
}

void RenderReplayCauseTreeOverlay( const ReplayOverlayRenderContext& context )
{
    PROFILE_SCOPED( "Frame/Replay/CauseTree/Overlay" );
    ReplayRuntime& replayRuntime = context.replayRuntime;
    Rendering::IRenderCommandContext& renderCommands = context.renderCommands;
    const int screenW = context.screenW;
    const int screenH = context.screenH;
    if ( screenW <= 0 || screenH <= 0 ||
         !replayRuntime.BuildCauseTreeRows( context.presentationRecords, context.bodyStore ) )
    {
        return;
    }

    // Invariant: row generation, scrolling, and drawing use the same mutable
    // cause-tree state. Clamp window geometry before deriving visible rows so
    // scroll offsets cannot point outside the rendered content.
    EnsureReplayCauseWindowPlacement( replayRuntime.CauseTree(), screenW, screenH );
    ReplayCauseWindowSurface surface;
    BuildReplayCauseWindowSurface( replayRuntime.CauseTree(), surface );
    surface.ResolvePointer( replayRuntime.CauseTree().mouseX,
                            replayRuntime.CauseTree().mouseY,
                            replayRuntime.CauseTree().pointerBlocked );
    const auto controlRect = [&]( ReplayCauseWindowControl id ) -> const UI::UIRect&
    {
        const RuntimeUiControl* row = surface.Find( ReplayCauseWindowControlId( id ) );
        if ( !row )
        {
            SB_FATAL( "ReplayCauseWindowSurface",
                      "Render snapshot is missing cause-window control id=%u.",
                      static_cast<uint32_t>( id ) );
        }
        return row->drawRect;
    };
    const UI::UIRect panel = controlRect( ReplayCauseWindowControl::Panel );
    const UI::UIRect title = controlRect( ReplayCauseWindowControl::Title );
    const UI::UIRect content = controlRect( ReplayCauseWindowControl::Content );
    const UI::UIRect resize = controlRect( ReplayCauseWindowControl::Resize );

    const UI::UIDrawContext draw( screenW, screenH, nullptr, &renderCommands );
    const UI::Style::UIPalette& palette = UI::Style::Palette();
    const UI::Style::UIRadii& radii = UI::Style::Radii();
    UI::Style::UIColor panelFill = palette.windowSubtle;
    panelFill.a = 0.93f;
    UI::Style::UIColor panelBorder = palette.innerBorder;
    panelBorder.a = 0.58f;
    draw.RoundedPanel( panel, radii.window, panelFill, panelBorder );
    draw.Rect( title.x + 12.0f,
               title.y + title.h - 1.0f,
               (std::max)( 0.0f, title.w - 24.0f ),
               1.0f,
               palette.innerBorder.r,
               palette.innerBorder.g,
               palette.innerBorder.b,
               0.42f );
    draw.Text( panel.x + 12.0f,
               panel.y + 10.0f,
               13.5f,
               palette.textPrimary.r,
               palette.textPrimary.g,
               palette.textPrimary.b,
               "REPLAY CAMERA" );
    draw.Text( panel.x + 136.0f,
               panel.y + 12.0f,
               11.0f,
               palette.textSecondary.r,
               palette.textSecondary.g,
               palette.textSecondary.b,
               "CAUSE" );

    const bool predictionRows =
        !replayRuntime.CauseTree().rows.empty() && replayRuntime.CauseTree().rows.front().prediction;
    const char* sourceLabel = predictionRows ? "PREDICT" : "REPLAY";
    const float sourceW = Text2d::MeasureText( 9.5f, sourceLabel );
    draw.RoundedRect( panel.x + panel.w - sourceW - 26.0f,
                      panel.y + 9.0f,
                      sourceW + 14.0f,
                      18.0f,
                      radii.smallButton,
                      predictionRows ? palette.controlHover.r : palette.control.r,
                      predictionRows ? palette.controlHover.g : palette.control.g,
                      predictionRows ? palette.controlHover.b : palette.control.b,
                      0.80f );
    draw.Text( panel.x + panel.w - sourceW - 19.0f,
               panel.y + 13.0f,
               9.5f,
               predictionRows ? palette.accentStrong.r : palette.accent.r,
               predictionRows ? palette.accentStrong.g : palette.accent.g,
               predictionRows ? palette.accentStrong.b : palette.accent.b,
               sourceLabel );

    draw.RoundedRect( content.x,
                      content.y,
                      content.w,
                      content.h,
                      radii.control,
                      palette.window.r,
                      palette.window.g,
                      palette.window.b,
                      0.46f );

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
    int hoveredRow = -1;
    if ( surface.hasHotControl &&
         surface.hotControl == ReplayCauseWindowControlId( ReplayCauseWindowControl::Content ) )
    {
        const float localY =
            static_cast<float>( replayRuntime.CauseTree().mouseY ) - content.y + replayRuntime.CauseTree().scrollY;
        const int candidate = static_cast<int>( floorf( localY / REPLAY_CAUSE_WINDOW_ROW_HEIGHT ) );
        if ( candidate >= 0 && candidate < rowCount )
        {
            hoveredRow = candidate;
        }
    }
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
        const bool hovered = rowIndex == hoveredRow;
        const bool selected = rowIndex == replayRuntime.CauseTree().selectedRow;
        if ( hovered || selected )
        {
            const UI::Style::UIColor& rowFill = selected ? palette.controlHover : palette.control;
            draw.RoundedRect( rowRect.x,
                              rowRect.y,
                              rowRect.w,
                              rowRect.h,
                              radii.smallButton,
                              rowFill.r,
                              rowFill.g,
                              rowFill.b,
                              hovered ? 0.82f : 0.56f );
        }

        const float indent = (std::min)( rowRect.w * 0.40f, static_cast<float>( row.depth ) * 16.0f );
        if ( row.depth > 0 )
        {
            const float lineX = rowRect.x + 8.0f + indent - 9.0f;
            draw.Rect( lineX,
                       rowRect.y + 4.0f,
                       1.0f,
                       rowRect.h - 8.0f,
                       palette.innerBorder.r,
                       palette.innerBorder.g,
                       palette.innerBorder.b,
                       0.34f );
            draw.Rect( lineX,
                       rowRect.y + rowRect.h * 0.5f,
                       8.0f,
                       1.0f,
                       palette.innerBorder.r,
                       palette.innerBorder.g,
                       palette.innerBorder.b,
                       0.34f );
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
        case RunReplayCauseTreeRowKind::PredictionMotion:
            strncpy_s( prefix, sizeof( prefix ), "MOTION", _TRUNCATE );
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
        else if ( row.kind == RunReplayCauseTreeRowKind::PredictionMotion )
        {
            markerR = 1.0f;
            markerG = 0.72f;
            markerB = 0.20f;
        }

        const float markerX = rowRect.x + 8.0f + indent;
        const float markerY = rowRect.y + 8.0f;
        draw.Rect( markerX, markerY, 6.0f, 6.0f, markerR, markerG, markerB, 0.92f );
        draw.Text( markerX + 11.0f,
                   rowRect.y + 4.0f,
                   12.4f,
                   row.kind == RunReplayCauseTreeRowKind::Body ? palette.textPrimary.r : palette.textSecondary.r,
                   row.kind == RunReplayCauseTreeRowKind::Body ? palette.textPrimary.g : palette.textSecondary.g,
                   row.kind == RunReplayCauseTreeRowKind::Body ? palette.textPrimary.b : palette.textSecondary.b,
                   clippedLabel );
        if ( clippedDetail[0] != '\0' )
        {
            draw.Text( markerX + 11.0f,
                       rowRect.y + 22.0f,
                       10.4f,
                       palette.textMuted.r,
                       palette.textMuted.g,
                       palette.textMuted.b,
                       clippedDetail );
        }
    }

    const float maxScroll = ReplayCauseWindowMaxScroll( replayRuntime.CauseTree() );
    if ( maxScroll > 0.0f )
    {
        const float trackX = content.x + content.w - 5.0f;
        draw.Rect( trackX,
                   content.y + 3.0f,
                   3.0f,
                   content.h - 6.0f,
                   palette.control.r,
                   palette.control.g,
                   palette.control.b,
                   0.72f );
        const float contentHeight = ReplayCauseWindowContentHeight( replayRuntime.CauseTree() );
        const float knobH = (std::max)( 24.0f, ( content.h / contentHeight ) * ( content.h - 6.0f ) );
        const float knobY =
            content.y + 3.0f + ( replayRuntime.CauseTree().scrollY / maxScroll ) * ( content.h - 6.0f - knobH );
        draw.RoundedRect( trackX - 1.0f,
                          knobY,
                          5.0f,
                          knobH,
                          2.0f,
                          palette.accent.r,
                          palette.accent.g,
                          palette.accent.b,
                          0.78f );
    }

    draw.Rect( resize.x + 4.0f,
               resize.y + resize.h - 5.0f,
               resize.w - 7.0f,
               1.0f,
               palette.innerBorder.r,
               palette.innerBorder.g,
               palette.innerBorder.b,
               0.68f );
    draw.Rect( resize.x + resize.w - 5.0f,
               resize.y + 4.0f,
               1.0f,
               resize.h - 7.0f,
               palette.innerBorder.r,
               palette.innerBorder.g,
               palette.innerBorder.b,
               0.68f );

    Text2d::FlushQuads( renderCommands );
    Text2d::FlushText( renderCommands );
}
} // namespace SkullbonezCore::Basics::ReplayOverlay
