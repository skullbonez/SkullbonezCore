/*
File: SkullbonezSource/Runtime/Planning/ReplayOverlayRenderer.cpp
Purpose:
  Draws replay scrubber, cause-tree, and solver-detail overlays from owner views.

Summary:
  Replay overlay rendering is a late UI pass. Keep the same screen-space layout
  and pointer eligibility as replay input by rebuilding the same fixed-capacity
  surfaces from ReplayOverlayLayout. The causal solver panel renders exact-frame
  copied evidence in a translucent compact panel beside its cause tree through
  Planning's shared layout, with a scrollbar after the fourth visible row.

Invariants:
  - Drawn controls use the same surface rows and pointer-block fact as input, so
    visible hover and actionable hit state stay identical.
  - Overlay rendering reads replay state only; replay mutation belongs to input
    and runtime replay helpers.
  - Solver rows expose their units and sign conventions on the surface; missing
    stages render neutral values without inventing a replacement record.
  - Solver-panel draw bounds come from the same pure projection used by input;
    no more than four complete contact rows appear at once, and unavailable
    detail never reserves row-sized empty space.

Related:
  - SkullbonezSource/Runtime/Planning/ReplayOverlayRenderer.h
  - SkullbonezSource/Runtime/Replay/ReplayOverlayLayout.h
  - Agentic/Reference/engine-glossary.md
*/
#include "ReplayOverlayRenderer.h"
#include "../Render/UiDrawSubmission.h"
#include "ReplayPlanningOverlayLayout.h"
#include "../../Core/FatalError.h"
#include "../../Assets/AssetKeys.h"
#include "../Replay/ReplayOverlayLayout.h"
#include "../../Core/Common.h"
#include "../../Core/Profiler.h"
#include "../../Physics/PhysicsTimestep.h"
#include "../../Rendering/Text.h"
#include "../../UI/UIDraw.h"
#include "../../UI/UIDrawList.h"
#include "../../UI/UIFrameComposition.h"
#include "../../UI/UIStyle.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>

namespace SkullbonezCore::Runtime::ReplayOverlay
{
using namespace ReplayScrubberOperations;
using Text::Text2d;

namespace
{
bool ReplayPredictionContactsIncomplete( const ReplayPredictionPresentationView& prediction )
{
    // Concept: contact payloads are optional prediction evidence. The root path
    // can still be correct when contact-tree rows are partial, but the overlay
    // should label that loss instead of implying a complete causal tree.
    for ( const RunReplayPredictionFrame& frame : prediction.frames )
    {
        if ( frame.contactsIncomplete )
        {
            return true;
        }
    }

    return false;
}

float ReplayOverlayTrackPosition( const ReplayScrubberView& scrubber, RunReplayTrack track )
{
    switch ( track )
    {
    case RunReplayTrack::Presentation:
        return scrubber.presentationPosition;
    case RunReplayTrack::Solver:
        return scrubber.solverPosition;
    }

    return scrubber.position;
}

void FlushReplayDrawList( UiDrawSubmission& submission, const UI::UIDrawList& drawList, Text::TextBatch& textBatch,
                          Rendering::Dx12TextureOwner& renderTextures, Rendering::Dx12GeometryOwner& renderCommands,
                          Rendering::Dx12Diagnostics& renderDiagnostics, int screenW, int screenH )
{
    submission.Submit( drawList, textBatch, nullptr, renderTextures, renderCommands, renderDiagnostics, screenW, screenH );
}

const Physics::PhysicsPipelineRecord* FindSolverDetailRecord( const ReplayCauseInspectionView& inspection,
                                                              uint32_t featureId,
                                                              Physics::PhysicsPipelineStage stage ) noexcept
{
    for ( const Physics::PhysicsPipelineRecord& record : inspection.solverDetailPipelineRecords )
    {
        if ( record.stage == stage && record.featureId == featureId )
        {
            return &record;
        }
    }

    return nullptr;
}
void RenderReplayCauseSolverDetailPanel( const UI::UIDrawContext& draw, const ReplayOverlayStateView& replay, int screenW,
                                         int screenH )
{
    const ReplayCauseInspectionView& inspection = replay.causeInspection;

    if ( !inspection.detailVisible )
    {
        return;
    }

    PROFILE_SCOPED( "Frame/Replay/CauseInspection/PanelRender" );
    const ReplayCauseSolverPanelLayout layout = BuildReplayCauseSolverPanelLayout( inspection, replay.causeTree, screenW,
                                                                                   screenH );
    const UI::Style::UIPalette& palette = UI::Style::Palette();
    const UI::Style::UIRadii& radii = UI::Style::Radii();
    UI::Style::UIColor panelFill = palette.windowSubtle;
    panelFill.a = REPLAY_CAUSE_SOLVER_PANEL_OPACITY;
    UI::Style::UIColor panelBorder = palette.innerBorder;
    panelBorder.a = 0.62f;
    draw.RoundedPanel( layout.panel, radii.window, panelFill, panelBorder );
    draw.Text( layout.panel.x + 12.0f, layout.panel.y + 8.0f, 12.5f, palette.textPrimary.r, palette.textPrimary.g,
               palette.textPrimary.b, "SOLVER ROW DETAIL" );

    char frameLabel[96] = {};
    sprintf_s( frameLabel, sizeof( frameLabel ), "FRAME %llu | %zu ROWS%s",
               static_cast<unsigned long long>( inspection.targetFrame ), inspection.solverDetailContacts.size(),
               inspection.contactPresentation.truncated ? " | PATCH TRUNCATED" : "" );
    draw.Text( layout.panel.x + 154.0f, layout.panel.y + 10.0f, 9.0f,
               inspection.contactPresentation.truncated ? palette.warningAccent.r : palette.accent.r,
               inspection.contactPresentation.truncated ? palette.warningAccent.g : palette.accent.g,
               inspection.contactPresentation.truncated ? palette.warningAccent.b : palette.accent.b, frameLabel );

    // Sign/units are rendered as part of the surface so a captured frame remains
    // interpretable without consulting solver implementation comments.
    draw.Text( layout.panel.x + 12.0f, layout.panel.y + REPLAY_CAUSE_SOLVER_PANEL_TITLE_HEIGHT + 1.0f, 8.3f,
               palette.textSecondary.r, palette.textSecondary.g, palette.textSecondary.b, REPLAY_CAUSE_SOLVER_PANEL_UNITS );
    draw.Text( layout.panel.x + 12.0f, layout.panel.y + REPLAY_CAUSE_SOLVER_PANEL_TITLE_HEIGHT + 13.0f, 8.3f,
               palette.textSecondary.r, palette.textSecondary.g, palette.textSecondary.b,
               REPLAY_CAUSE_SOLVER_PANEL_UNITS_MORE );
    draw.Text( layout.panel.x + 12.0f, layout.panel.y + REPLAY_CAUSE_SOLVER_PANEL_TITLE_HEIGHT + 29.0f, 8.3f,
               palette.textMuted.r, palette.textMuted.g, palette.textMuted.b, REPLAY_CAUSE_SOLVER_PANEL_SIGNS );
    draw.Text( layout.panel.x + 12.0f, layout.panel.y + REPLAY_CAUSE_SOLVER_PANEL_TITLE_HEIGHT + 41.0f, 8.3f,
               palette.textMuted.r, palette.textMuted.g, palette.textMuted.b, REPLAY_CAUSE_SOLVER_PANEL_SIGNS_MORE );

    if ( inspection.solverDetailAvailability != ReplayCauseSolverDetailAvailability::Available ||
         inspection.solverDetailContacts.empty() )
    {
        draw.RoundedRect( layout.content.x, layout.content.y, layout.content.w, layout.content.h, radii.control,
                          palette.window.r, palette.window.g, palette.window.b, 0.30f );
        draw.Text( layout.content.x + 10.0f, layout.content.y + 15.0f, 11.5f, palette.textSecondary.r,
                   palette.textSecondary.g, palette.textSecondary.b, inspection.solverDetailFeedback );
        return;
    }

    const int rowCount = static_cast<int>( inspection.solverDetailContacts.size() );
    const int firstRow = std::clamp( inspection.solverDetailFirstRow, 0, (std::max)( 0, rowCount - layout.visibleRows ) );
    const int endRow = (std::min)( rowCount, firstRow + layout.visibleRows );

    for ( int rowIndex = firstRow; rowIndex < endRow; ++rowIndex )
    {
        const Physics::PhysicsSolverPersistentContactSample&
            contact = inspection.solverDetailContacts[static_cast<std::size_t>( rowIndex )];
        const float rowY = layout.content.y + static_cast<float>( rowIndex - firstRow ) * layout.rowHeight;
        const UI::UIRect rowRect { layout.content.x, rowY, layout.content.w - 8.0f, layout.rowHeight - 3.0f };
        draw.RoundedRect( rowRect.x, rowRect.y, rowRect.w, rowRect.h, radii.control, palette.window.r, palette.window.g,
                          palette.window.b, 0.44f );

        const Physics::PhysicsPipelineRecord*
            correction = FindSolverDetailRecord( inspection, contact.featureId,
                                                 Physics::PhysicsPipelineStage::PositionCorrection );
        const Physics::PhysicsPipelineRecord* cache = FindSolverDetailRecord( inspection, contact.featureId,
                                                                              Physics::PhysicsPipelineStage::CacheStore );
        const ReplayCauseSolverPanelRowText values = BuildReplayCauseSolverPanelRowText( inspection, rowIndex );
        char line[768] = {};
        float textY = rowY + 4.0f;
        draw.Text( rowRect.x + 7.0f, textY, 9.3f, palette.textPrimary.r, palette.textPrimary.g, palette.textPrimary.b,
                   values.headline );
        textY += 12.0f;
        draw.Text( rowRect.x + 7.0f, textY, 8.2f, palette.textSecondary.r, palette.textSecondary.g, palette.textSecondary.b,
                   values.basis );
        textY += 11.0f;
        draw.Text( rowRect.x + 7.0f, textY, 8.2f, palette.textSecondary.r, palette.textSecondary.g, palette.textSecondary.b,
                   values.geometry );
        textY += 11.0f;
        draw.Text( rowRect.x + 7.0f, textY, 8.2f, palette.textSecondary.r, palette.textSecondary.g, palette.textSecondary.b,
                   values.masses );
        textY += 11.0f;
        draw.Text( rowRect.x + 7.0f, textY, 8.2f, palette.textSecondary.r, palette.textSecondary.g, palette.textSecondary.b,
                   values.impulses );
        textY += 12.0f;

        int iterationColumn = 0;

        for ( const Physics::PhysicsPipelineRecord& record : inspection.solverDetailPipelineRecords )
        {
            if ( record.stage != Physics::PhysicsPipelineStage::SolverIteration || record.featureId != contact.featureId )
            {
                continue;
            }

            // Physics emits scalarC as the tangent-impulse magnitude. Treat a
            // value at the cone radius as clamped; no UI-only solver state is retained.
            const bool clamped = contact.frictionLimit > 0.0f && record.scalarC >= contact.frictionLimit - 1.0e-5f;
            const int column = iterationColumn % REPLAY_CAUSE_SOLVER_PANEL_ITERATIONS_PER_LINE;
            const int lineIndex = iterationColumn / REPLAY_CAUSE_SOLVER_PANEL_ITERATIONS_PER_LINE;
            const float columnWidth = ( rowRect.w - 16.0f ) /
                                      static_cast<float>( REPLAY_CAUSE_SOLVER_PANEL_ITERATIONS_PER_LINE );
            sprintf_s( line, sizeof( line ), "i%d dN %.3g N %.3g T %.3g %s", record.iteration, record.scalarA,
                       record.scalarB, record.scalarC, clamped ? "CLAMP" : "free" );
            draw.Text( rowRect.x + 7.0f + static_cast<float>( column ) * columnWidth,
                       textY + static_cast<float>( lineIndex ) * REPLAY_CAUSE_SOLVER_PANEL_ITERATION_LINE_HEIGHT, 7.0f,
                       clamped ? palette.accentStrong.r : palette.textMuted.r,
                       clamped ? palette.accentStrong.g : palette.textMuted.g,
                       clamped ? palette.accentStrong.b : palette.textMuted.b, line );
            ++iterationColumn;
        }

        const int iterationLines = ( iterationColumn + REPLAY_CAUSE_SOLVER_PANEL_ITERATIONS_PER_LINE - 1 ) /
                                   REPLAY_CAUSE_SOLVER_PANEL_ITERATIONS_PER_LINE;
        textY += static_cast<float>( iterationLines ) * REPLAY_CAUSE_SOLVER_PANEL_ITERATION_LINE_HEIGHT;

        char velocityText[256] = "wb none";
        int velocityCount = 0;

        for ( const Physics::PhysicsPipelineRecord& record : inspection.solverDetailPipelineRecords )
        {
            if ( record.stage != Physics::PhysicsPipelineStage::VelocityWriteback ||
                 ( record.bodyA != contact.bodyA && record.bodyA != contact.bodyB ) )
            {
                continue;
            }

            char append[96] = {};
            sprintf_s( append, sizeof( append ), "%sB%d L %.3g A %.3g", velocityCount == 0 ? "" : " / ", record.bodyA,
                       record.scalarA, record.scalarB );

            if ( velocityCount == 0 )
            {
                strcpy_s( velocityText, sizeof( velocityText ), "wb " );
            }

            strcat_s( velocityText, sizeof( velocityText ), append );
            ++velocityCount;
        }

        sprintf_s( line, sizeof( line ), "corr %.4g (pen %.4g slop %.4g) | %s | cache (%.4g %.4g %.4g)",
                   correction ? correction->scalarA : 0.0f, correction ? correction->scalarB : 0.0f,
                   correction ? correction->scalarC : 0.0f, velocityText, cache ? cache->scalarA : 0.0f,
                   cache ? cache->scalarB : 0.0f, cache ? cache->scalarC : 0.0f );
        draw.Text( rowRect.x + 7.0f, textY, 7.8f, palette.textMuted.r, palette.textMuted.g, palette.textMuted.b, line );
    }

    if ( rowCount > layout.visibleRows )
    {
        const float trackX = layout.content.x + layout.content.w - 5.0f;
        const int maximumFirst = rowCount - layout.visibleRows;
        const float knobH = layout.content.h * static_cast<float>( layout.visibleRows ) / static_cast<float>( rowCount );
        const float travel = layout.content.h - knobH;
        const float knobY = layout.content.y + travel * static_cast<float>( firstRow ) / static_cast<float>( maximumFirst );
        draw.Rect( trackX, layout.content.y, 3.0f, layout.content.h, palette.control.r, palette.control.g, palette.control.b,
                   0.72f );
        draw.RoundedRect( trackX - 1.0f, knobY, 5.0f, knobH, 2.0f, palette.accent.r, palette.accent.g, palette.accent.b,
                          0.82f );
    }
}
} // namespace

// Concept: the replay overlay is a read-only projection of replay state.
//
// Input code owns mutations such as dragging, toggling prediction, and branch
// creation. This pass samples the current state and turns it into UI quads and
// text so rendering cannot accidentally advance or rewrite replay timelines.
void RenderReplayScrubberOverlay( UiDrawSubmission& submission, Text::TextBatch& textBatch, UI::UIDrawList& drawList,
                                  const ReplayOverlayStateView& replay, Rendering::Dx12TextureOwner& renderTextures,
                                  Rendering::Dx12GeometryOwner& renderCommands,
                                  Rendering::Dx12Diagnostics& renderDiagnostics, Core::Profiler* profiler,
                                  bool scenePhysicsEnabled, RuntimeInteractionGestureKind gesture,
                                  ReplayOverlayViewport viewport, double nowSeconds )
{
    const int screenW = viewport.width;
    const int screenH = viewport.height;
    PROFILE_SCOPED( "Frame/Replay/ScrubberOverlay" );
    RenderReplayInterceptOverlay( submission, textBatch, drawList, replay, renderTextures, renderCommands, renderDiagnostics,
                                  screenW, screenH );

    RenderReplayTripPlannerOverlay( submission, textBatch, drawList, replay, renderTextures, renderCommands,
                                    renderDiagnostics, screenW, screenH );

    RenderReplayPorkchopOverlay( submission, textBatch, drawList, replay, renderTextures, renderCommands, renderDiagnostics,
                                 screenW, screenH );

    const ReplayScrubberView& scrubber = replay.scrubber;

    // Why: the cause tree is an inspection tool, not a child of the scrubber.
    // Draw it even when the scrubber itself is hidden by UI/editor policy.
    RenderReplayCauseTreeOverlay( submission, textBatch, drawList, replay, renderTextures, renderCommands, renderDiagnostics,
                                  profiler, screenW, screenH );

    if ( !replay.shouldRenderScrubber )
    {
        return;
    }

    const bool loadedPresentation = replay.selection.loadedPresentation;
    const ReplayRecorderStats solverReplayStats = replay.solverStats;
    const bool solverReplayEnabled = solverReplayStats.enabled;

    // Why: the replay bar may be visible while force-paused before two solver
    // frames exist. Retained-history tools stay dimmed, but prediction can run
    // from the current live solver state once scene physics is available.
    const bool solverToolsEnabled = solverReplayEnabled && solverReplayStats.sampleCount >= 2;
    const bool predictionToolsEnabled = solverReplayEnabled && scenePhysicsEnabled;

    if ( screenW <= 0 || screenH <= 0 || ( !loadedPresentation && !solverReplayEnabled ) )
    {
        return;
    }

    const RunReplayTrack activeTrack = loadedPresentation ? RunReplayTrack::Presentation : RunReplayTrack::Solver;
    ReplayScrubberSurfaceInput surfaceInput = DescribeReplayScrubberAvailability( scrubber, solverReplayStats,
                                                                                  loadedPresentation,
                                                                                  replay.pathVisualizer.hasTarget,
                                                                                  replay.predictionTimelineAvailable,
                                                                                  replay.selection.currentPresentation !=
                                                                                      nullptr,
                                                                                  replay.selection.currentSolver != nullptr,
                                                                                  scenePhysicsEnabled );

    surfaceInput.screenW = screenW;
    surfaceInput.screenH = screenH;
    surfaceInput.gesture = gesture;
    surfaceInput.predictionEnabled = replay.prediction.enabled;
    surfaceInput.predictionHighDetail = replay.prediction.detailMode == ReplayPredictionDetailMode::High;
    ReplayScrubberSurface surface;
    BuildReplayScrubberSurface( surfaceInput, surface );
    surface.ResolvePointer( scrubber.mouseX, scrubber.mouseY );
    const auto control = [&]( ReplayScrubberControl id ) -> const RuntimeUiControl&
    {
        const RuntimeUiControl* row = surface.Find( ReplayScrubberControlId( id ) );

        if ( !row )
        {
            SB_FATAL( "ReplayScrubberSurface", "Render snapshot is missing replay scrubber control id=%u.",
                      static_cast<uint32_t>( id ) );
        }

        return *row;
    };

    const auto isHotControl = [&]( ReplayScrubberControl id )
    { return surface.hasHotControl && surface.hotControl == ReplayScrubberControlId( id ); };

    const float t = std::clamp( ReplayOverlayTrackPosition( scrubber, activeTrack ), 0.0f, 1.0f );
    const float solverPresentT = loadedPresentation ? 1.0f : replay.selection.solverPresentTrackPosition;

    // Concept: the solver track is split into retained history and generated
    // future. Positions past the live marker draw prediction frames instead of
    // retained solver samples.
    const bool futureTimelineVisible = !loadedPresentation && ReplayTimelineHasFuture( solverPresentT );
    const ReplayPresentationSample* selectedPresentation = replay.selection.selectedPresentation;
    const ReplayPresentationSample* latestPresentation = replay.selection.latestPresentation;
    const ReplaySolverFrameSample* selected = replay.selection.selectedSolver;
    const ReplaySolverFrameSample* latest = replay.selection.latestSolver;
    const RunReplayPredictionFrame* selectedPrediction = replay.selectedPrediction;
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

    if ( loadedPresentation && ReplayAtPresentTrackPosition( t, 1.0f ) )
    {
        sprintf_s( timeLabel, sizeof( timeLabel ), "END" );
    }
    else if ( selectedPrediction )
    {
        sprintf_s( timeLabel, sizeof( timeLabel ), "+%.1fs", futureSeconds );
    }
    else if ( ReplayAtPresentTrackPosition( t, solverPresentT ) && !scrubber.historicalSamplePaused )
    {
        sprintf_s( timeLabel, sizeof( timeLabel ), "LIVE" );
    }
    else
    {
        sprintf_s( timeLabel, sizeof( timeLabel ), "-%.1fs", secondsBack );
    }

    drawList.Clear();
    const UI::UIDrawContext draw( screenW, screenH, drawList );
    const UI::UIRect panel = control( ReplayScrubberControl::Panel ).drawRect;
    const UI::Style::UIPalette& palette = UI::Style::Palette();
    const UI::Style::UIRadii& radii = UI::Style::Radii();
    const float fade = std::clamp( scrubber.visibleAlpha, 0.0f, 1.0f );

    if ( fade <= REPLAY_SCRUBBER_FADE_EPSILON )
    {
        return;
    }

    auto fadeA = [fade]( float alpha ) -> float { return alpha * fade; };

    auto fadeC = [fade]( float channel ) -> float { return channel * fade; };

    auto drawText = [&]( float x, float y, float pxSize, float r, float g, float b, const char* value )
    { draw.Text( x, y, pxSize, fadeC( r ), fadeC( g ), fadeC( b ), value ); };

    const bool live = !loadedPresentation && ReplayAtPresentTrackPosition( t, solverPresentT ) &&
                      !scrubber.historicalSamplePaused;

    const double now = nowSeconds;
    const char* sourceLabel = loadedPresentation ? "V2 FILE" : "SOLVER";
    const bool branchEnabled = scrubber.historicalSamplePaused &&
                               ( ( loadedPresentation && replay.selection.currentPresentation != nullptr ) ||
                                 ( !loadedPresentation && solverToolsEnabled &&
                                   replay.selection.currentSolver != nullptr ) );

    UI::Style::UIColor panelFill = palette.windowSubtle;
    panelFill.a = fadeA( 0.92f );
    UI::Style::UIColor panelBorder = palette.innerBorder;
    panelBorder.a = fadeA( 0.42f );
    draw.RoundedPanel( panel, radii.control, panelFill, panelBorder );
    drawText( panel.x + 16.0f, panel.y + 19.0f, 10.5f, palette.textSecondary.r, palette.textSecondary.g,
              palette.textSecondary.b, sourceLabel );

    const float labelW = Text2d::MeasureText( 11.0f, timeLabel );
    drawText( panel.x + panel.w - labelW - 16.0f, panel.y + 18.0f, 11.0f, live ? palette.accent.r : palette.warningAccent.r,
              live ? palette.accent.g : palette.warningAccent.g, live ? palette.accent.b : palette.warningAccent.b,
              timeLabel );

    {
        const UI::UIRect branchButton = control( ReplayScrubberControl::Branch ).drawRect;
        const bool branchHover = branchEnabled && isHotControl( ReplayScrubberControl::Branch );
        draw.RoundedRect( branchButton.x, branchButton.y, branchButton.w, branchButton.h, radii.smallButton,
                          branchHover ? palette.controlHover.r : palette.control.r,
                          branchHover ? palette.controlHover.g : palette.control.g,
                          branchHover ? palette.controlHover.b : palette.control.b, fadeA( branchEnabled ? 0.94f : 0.42f ) );

        draw.Outline( branchButton.x, branchButton.y, branchButton.w, branchButton.h, palette.accent.r, palette.accent.g,
                      palette.accent.b, fadeA( branchEnabled ? ( branchHover ? 0.84f : 0.42f ) : 0.18f ) );

        drawText( branchButton.x + 12.0f, branchButton.y + 4.5f, 9.5f,
                  branchEnabled ? palette.textPrimary.r : palette.textMuted.r,
                  branchEnabled ? palette.textPrimary.g : palette.textMuted.g,
                  branchEnabled ? palette.textPrimary.b : palette.textMuted.b, "BRANCH" );
    }

    if ( !loadedPresentation )
    {
        const UI::UIRect highDetail = control( ReplayScrubberControl::HighDetail ).drawRect;
        const bool highDetailEnabled = control( ReplayScrubberControl::HighDetail ).enabled;
        const bool highDetailChecked = control( ReplayScrubberControl::HighDetail ).checked;
        const bool highDetailHover = highDetailEnabled && isHotControl( ReplayScrubberControl::HighDetail );
        draw.RoundedRect( highDetail.x, highDetail.y, highDetail.w, highDetail.h, radii.smallButton,
                          highDetailHover ? palette.controlHover.r : palette.control.r,
                          highDetailHover ? palette.controlHover.g : palette.control.g,
                          highDetailHover ? palette.controlHover.b : palette.control.b,
                          fadeA( highDetailEnabled ? ( highDetailHover || highDetailChecked ? 0.94f : 0.78f ) : 0.38f ) );
        draw.Outline( highDetail.x, highDetail.y, highDetail.w, highDetail.h, palette.accent.r, palette.accent.g,
                      palette.accent.b,
                      fadeA( highDetailEnabled ? ( highDetailHover || highDetailChecked ? 0.78f : 0.36f ) : 0.14f ) );

        const float detailCheckX = highDetail.x + 6.0f;
        const float detailCheckY = highDetail.y + 5.0f;
        draw.Outline( detailCheckX, detailCheckY, 10.0f, 10.0f, palette.accent.r, palette.accent.g, palette.accent.b,
                      fadeA( highDetailEnabled ? 0.82f : 0.28f ) );

        if ( highDetailChecked )
        {
            draw.Rect( detailCheckX + 2.0f, detailCheckY + 2.0f, 6.0f, 6.0f, palette.accent.r, palette.accent.g,
                       palette.accent.b, fadeA( 0.95f ) );
        }

        const float detailTextR = !highDetailEnabled ? palette.textMuted.r
                                                     : ( highDetailChecked ? palette.accent.r : palette.textSecondary.r );
        const float detailTextG = !highDetailEnabled ? palette.textMuted.g
                                                     : ( highDetailChecked ? palette.accent.g : palette.textSecondary.g );
        const float detailTextB = !highDetailEnabled ? palette.textMuted.b
                                                     : ( highDetailChecked ? palette.accent.b : palette.textSecondary.b );
        drawText( highDetail.x + 20.0f, highDetail.y + 2.0f, 6.5f, detailTextR, detailTextG, detailTextB, "HIGH" );
        drawText( highDetail.x + 20.0f, highDetail.y + 10.0f, 6.5f, detailTextR, detailTextG, detailTextB, "DETAIL" );

        {
            PROFILE_SCOPED( "Frame/Replay/ScrubberOverlay/VelocityEditControls" );
            const UI::UIRect velocityEdit = control( ReplayScrubberControl::VelocityEdit ).drawRect;
            const bool velocityEditEnabled = solverToolsEnabled && replay.velocityEdit.enabled;
            const bool velocityEditHover = solverToolsEnabled && isHotControl( ReplayScrubberControl::VelocityEdit );
            draw.RoundedRect( velocityEdit.x, velocityEdit.y, velocityEdit.w, velocityEdit.h, radii.smallButton,
                              velocityEditHover ? palette.controlHover.r : palette.control.r,
                              velocityEditHover ? palette.controlHover.g : palette.control.g,
                              velocityEditHover ? palette.controlHover.b : palette.control.b,
                              fadeA( solverToolsEnabled ? ( velocityEditHover || velocityEditEnabled ? 0.94f : 0.78f )
                                                        : 0.38f ) );

            draw.Outline( velocityEdit.x, velocityEdit.y, velocityEdit.w, velocityEdit.h, palette.warningAccent.r,
                          palette.warningAccent.g, palette.warningAccent.b,
                          fadeA( solverToolsEnabled ? ( velocityEditHover || velocityEditEnabled ? 0.78f : 0.34f )
                                                    : 0.14f ) );

            const float checkX = velocityEdit.x + 7.0f;
            const float checkY = velocityEdit.y + 5.0f;
            draw.Outline( checkX, checkY, 10.0f, 10.0f, palette.warningAccent.r, palette.warningAccent.g,
                          palette.warningAccent.b, fadeA( solverToolsEnabled ? 0.82f : 0.28f ) );

            if ( velocityEditEnabled )
            {
                draw.Rect( checkX + 2.0f, checkY + 2.0f, 6.0f, 6.0f, palette.warningAccent.r, palette.warningAccent.g,
                           palette.warningAccent.b, fadeA( 0.95f ) );
            }

            drawText( velocityEdit.x + 23.0f, velocityEdit.y + 4.5f, 9.5f,
                      !solverToolsEnabled ? palette.textMuted.r
                                          : ( velocityEditEnabled ? palette.warningAccent.r : palette.textSecondary.r ),
                      !solverToolsEnabled ? palette.textMuted.g
                                          : ( velocityEditEnabled ? palette.warningAccent.g : palette.textSecondary.g ),
                      !solverToolsEnabled ? palette.textMuted.b
                                          : ( velocityEditEnabled ? palette.warningAccent.b : palette.textSecondary.b ),
                      "ALT VEL" );
        }
    }

    auto drawReplayRow = [&]( RunReplayTrack trackName, float fillR, float fillG, float fillB, float outlineR,
                              float outlineG, float outlineB, bool saveEnabled )
    {
        const UI::UIRect track = control( ReplayScrubberControl::ScrubTrack ).drawRect;

        const UI::UIRect saveButton = control( ReplayScrubberControl::Save ).drawRect;
        const UI::UIRect loadButton = control( ReplayScrubberControl::Load ).drawRect;
        const float rowT = std::clamp( ReplayOverlayTrackPosition( scrubber, trackName ), 0.0f, 1.0f );
        const float fillW = (std::max)( REPLAY_SCRUBBER_TRACK_HEIGHT, track.w * rowT );
        const float knobX = track.x + track.w * rowT;
        const bool active = activeTrack == trackName;
        const bool inactiveDuringScrub = ( gesture == RuntimeInteractionGestureKind::ReplayScrubDrag ||
                                           scrubber.historicalSamplePaused ) &&
                                         !active;

        const bool saveHover = saveEnabled && isHotControl( ReplayScrubberControl::Save );
        const bool saveFeedback = scrubber.saveMessage[0] != '\0' && scrubber.saveMessageUntil >= now;
        const bool saveFailed = saveFeedback && strstr( scrubber.saveMessage, "FAILED" ) != nullptr;
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
        draw.RoundedRect( track.x, track.y, track.w, track.h, track.h * 0.5f, rowBack, rowBack + 0.02f, rowBack + 0.05f,
                          fadeA( inactiveDuringScrub ? 0.74f : 0.92f ) );

        draw.RoundedRect( track.x, track.y, fillW, track.h, track.h * 0.5f, rowFillR, rowFillG, rowFillB,
                          fadeA( rowFillA ) );

        if ( trackName == RunReplayTrack::Solver && futureTimelineVisible )
        {
            const float presentX = track.x + track.w * solverPresentT;

            // Why: future prediction is a different timeline region, not just a
            // longer scrub value. Draw the right-hand side after the normal fill
            // so it stays visibly blue even when the selected knob is in future.
            draw.Rect( presentX, track.y, (std::max)( 0.0f, track.x + track.w - presentX ), track.h, 0.08f, 0.30f, 0.92f,
                       fadeA( inactiveDuringScrub ? 0.40f : 0.72f ) );
        }

        draw.RoundedRect( knobX - 6.0f, track.y - 5.0f, 12.0f, 18.0f, 5.0f, active ? 0.98f : 0.52f, active ? 0.98f : 0.56f,
                          active ? 1.0f : 0.60f, fadeA( active ? 0.98f : 0.70f ) );

        draw.Outline( knobX - 6.0f, track.y - 5.0f, 12.0f, 18.0f, outlineR, outlineG, outlineB,
                      fadeA( active ? 0.72f : 0.22f ) );

        if ( trackName == RunReplayTrack::Solver && futureTimelineVisible )
        {
            const float presentX = track.x + track.w * solverPresentT;
            draw.Rect( presentX - 1.0f, track.y - 6.0f, 2.0f, track.h + 12.0f, 0.92f, 1.0f, 0.84f, fadeA( 0.86f ) );
            draw.Rect( presentX - 4.0f, track.y - 8.0f, 8.0f, 2.0f, 0.92f, 1.0f, 0.84f, fadeA( 0.70f ) );
            draw.Rect( presentX - 4.0f, track.y + track.h + 6.0f, 8.0f, 2.0f, 0.92f, 1.0f, 0.84f, fadeA( 0.70f ) );
        }

        draw.RoundedRect( saveButton.x, saveButton.y, saveButton.w, saveButton.h, 4.0f, saveR, saveG, saveB,
                          fadeA( saveEnabled ? 0.96f : 0.34f ) );

        draw.Outline( saveButton.x, saveButton.y, saveButton.w, saveButton.h, outlineR, outlineG, outlineB,
                      fadeA( saveEnabled ? ( saveHover || saveFeedback ? 0.74f : 0.36f ) : 0.16f ) );

        const float iconX = saveButton.x + 6.0f;
        const float iconY = saveButton.y + 5.0f;
        const float iconW = 10.0f;
        const float iconH = 12.0f;
        const float iconA = fadeA( saveEnabled ? 0.96f : 0.34f );
        draw.Outline( iconX, iconY, iconW, iconH, 0.88f, 0.97f, 1.0f, iconA );
        draw.Rect( iconX + 2.0f, iconY + 2.0f, iconW - 4.0f, 3.0f, 0.88f, 0.97f, 1.0f, iconA * 0.73f );
        draw.Rect( iconX + 3.0f, iconY + 8.0f, iconW - 6.0f, 3.0f, 0.88f, 0.97f, 1.0f, iconA * 0.85f );

        draw.RoundedRect( loadButton.x, loadButton.y, loadButton.w, loadButton.h, radii.smallButton,
                          loadHover ? palette.controlHover.r : palette.control.r,
                          loadHover ? palette.controlHover.g : palette.control.g,
                          loadHover ? palette.controlHover.b : palette.control.b, fadeA( 0.92f ) );

        draw.Outline( loadButton.x, loadButton.y, loadButton.w, loadButton.h, outlineR, outlineG, outlineB,
                      fadeA( loadHover ? 0.72f : 0.34f ) );

        drawText( loadButton.x + 9.0f, loadButton.y + 5.0f, 9.5f, palette.textPrimary.r, palette.textPrimary.g,
                  palette.textPrimary.b, "LOAD" );
    };

    if ( loadedPresentation )
    {
        drawReplayRow( RunReplayTrack::Presentation, palette.accent.r, palette.accent.g, palette.accent.b,
                       palette.accentStrong.r, palette.accentStrong.g, palette.accentStrong.b, false );
    }
    else
    {
        drawReplayRow( RunReplayTrack::Solver, palette.accent.r, palette.accent.g, palette.accent.b, palette.accentStrong.r,
                       palette.accentStrong.g, palette.accentStrong.b, solverToolsEnabled );
    }

    if ( loadedPresentation )
    {
        FlushReplayDrawList( submission, drawList, textBatch, renderTextures, renderCommands, renderDiagnostics, screenW,
                             screenH );

        return;
    }

    const UI::UIRect predictToggle = control( ReplayScrubberControl::PredictionToggle ).drawRect;
    const UI::UIRect predict = control( ReplayScrubberControl::PredictionPanel ).drawRect;
    const UI::UIRect predictHorizon = control( ReplayScrubberControl::PredictionHorizon ).drawRect;
    const UI::UIRect ragdollVisualToggle = control( ReplayScrubberControl::RagdollVisuals ).drawRect;
    const UI::UIRect pastPathToggle = control( ReplayScrubberControl::PastPath ).drawRect;
    const bool predictHover = predictionToolsEnabled &&
                              ( isHotControl( ReplayScrubberControl::PredictionHorizon ) ||
                                gesture == RuntimeInteractionGestureKind::ReplayPredictionHorizonDrag );

    const bool predictEnabled = predictionToolsEnabled && replay.prediction.enabled;
    const bool ragdollVisualsEnabled = predictionToolsEnabled && replay.prediction.ragdollVisualsEnabled;
    const bool pastPathToolsEnabled = solverToolsEnabled && replay.pathVisualizer.hasTarget;
    const bool pastPathEnabled = pastPathToolsEnabled && replay.pathVisualizer.pastPathVisible;
    const bool predictionContactsIncomplete = ReplayPredictionContactsIncomplete( replay.prediction );
    const char* colorModeLabel = ReplayPathColorModeName( replay.pathVisualizer.colorMode );
    const float predictSeconds = std::clamp( replay.prediction.horizonSeconds, REPLAY_PREDICTION_MIN_SECONDS,
                                             REPLAY_PREDICTION_MAX_SECONDS );

    const UI::Style::UIColor predictFill = predictionToolsEnabled && isHotControl( ReplayScrubberControl::PredictionToggle )
                                               ? palette.controlHover
                                               : palette.control;

    const UI::Style::UIColor predictControlFill = predictHover ? palette.controlHover : palette.control;
    draw.RoundedRect( predictToggle.x, predictToggle.y, predictToggle.w, predictToggle.h, radii.smallButton, predictFill.r,
                      predictFill.g, predictFill.b, fadeA( predictionToolsEnabled ? 0.88f : 0.38f ) );

    draw.Outline( predictToggle.x, predictToggle.y, predictToggle.w, predictToggle.h, palette.accent.r, palette.accent.g,
                  palette.accent.b,
                  fadeA( predictionToolsEnabled
                             ? ( isHotControl( ReplayScrubberControl::PredictionToggle ) || predictEnabled ? 0.72f : 0.34f )
                             : 0.14f ) );

    const float checkX = predictToggle.x + 7.0f;
    const float checkY = predictToggle.y + 5.0f;
    draw.Outline( checkX, checkY, 10.0f, 10.0f, palette.accent.r, palette.accent.g, palette.accent.b,
                  fadeA( predictionToolsEnabled ? 0.82f : 0.28f ) );

    if ( predictEnabled )
    {
        draw.Rect( checkX + 2.0f, checkY + 2.0f, 6.0f, 6.0f, palette.accentStrong.r, palette.accentStrong.g,
                   palette.accentStrong.b, fadeA( 0.95f ) );
    }

    drawText( predictToggle.x + 23.0f, predictToggle.y + 4.5f, 9.5f,
              !predictionToolsEnabled ? palette.textMuted.r
                                      : ( predictEnabled ? palette.accentStrong.r : palette.textSecondary.r ),
              !predictionToolsEnabled ? palette.textMuted.g
                                      : ( predictEnabled ? palette.accentStrong.g : palette.textSecondary.g ),
              !predictionToolsEnabled ? palette.textMuted.b
                                      : ( predictEnabled ? palette.accentStrong.b : palette.textSecondary.b ),
              "PREDICT" );

    draw.RoundedRect( predict.x, predict.y, predict.w, predict.h, radii.smallButton, predictControlFill.r,
                      predictControlFill.g, predictControlFill.b, fadeA( predictionToolsEnabled ? 0.88f : 0.38f ) );

    draw.Outline( predict.x, predict.y, predict.w, predict.h, palette.accent.r, palette.accent.g, palette.accent.b,
                  fadeA( predictionToolsEnabled ? ( predictHover || predictEnabled ? 0.72f : 0.34f ) : 0.14f ) );

    char predictSecondsLabel[16] = {};
    sprintf_s( predictSecondsLabel, sizeof( predictSecondsLabel ), "%.0fs", static_cast<double>( predictSeconds ) );
    const float horizonT = ReplayPredictionHorizonT( predictSeconds );
    const float horizonFillW = (std::max)( 4.0f, predictHorizon.w * horizonT );
    const float horizonKnobX = predictHorizon.x + predictHorizon.w * horizonT;
    draw.RoundedRect( predictHorizon.x, predictHorizon.y, predictHorizon.w, predictHorizon.h, 4.0f, 0.10f, 0.14f, 0.15f,
                      fadeA( predictionToolsEnabled ? 0.86f : 0.34f ) );

    draw.RoundedRect( predictHorizon.x, predictHorizon.y, horizonFillW, predictHorizon.h, 4.0f, 0.34f, 0.95f, 0.62f,
                      fadeA( predictionToolsEnabled ? ( predictEnabled ? 0.86f : 0.48f ) : 0.20f ) );

    draw.RoundedRect( horizonKnobX - 4.0f, predictHorizon.y - 3.0f, 8.0f, 14.0f, 3.0f, predictEnabled ? 0.88f : 0.56f,
                      predictEnabled ? 1.0f : 0.62f, predictEnabled ? 0.82f : 0.64f,
                      fadeA( predictionToolsEnabled ? ( predictHover ? 0.98f : 0.86f ) : 0.34f ) );

    drawText( predictHorizon.x + predictHorizon.w + 8.0f, predict.y + 4.5f, 8.5f,
              !predictionToolsEnabled ? palette.textMuted.r
                                      : ( predictEnabled ? palette.accentStrong.r : palette.textSecondary.r ),
              !predictionToolsEnabled ? palette.textMuted.g
                                      : ( predictEnabled ? palette.accentStrong.g : palette.textSecondary.g ),
              !predictionToolsEnabled ? palette.textMuted.b
                                      : ( predictEnabled ? palette.accentStrong.b : palette.textSecondary.b ),
              predictSecondsLabel );

    draw.RoundedRect( ragdollVisualToggle.x, ragdollVisualToggle.y, ragdollVisualToggle.w, ragdollVisualToggle.h,
                      radii.smallButton,
                      predictionToolsEnabled && isHotControl( ReplayScrubberControl::RagdollVisuals )
                          ? palette.controlHover.r
                          : palette.control.r,
                      predictionToolsEnabled && isHotControl( ReplayScrubberControl::RagdollVisuals )
                          ? palette.controlHover.g
                          : palette.control.g,
                      predictionToolsEnabled && isHotControl( ReplayScrubberControl::RagdollVisuals )
                          ? palette.controlHover.b
                          : palette.control.b,
                      fadeA( predictionToolsEnabled ? 0.88f : 0.38f ) );

    draw.Outline( ragdollVisualToggle.x, ragdollVisualToggle.y, ragdollVisualToggle.w, ragdollVisualToggle.h,
                  palette.accent.r, palette.accent.g, palette.accent.b,
                  fadeA( predictionToolsEnabled
                             ? ( isHotControl( ReplayScrubberControl::RagdollVisuals ) || ragdollVisualsEnabled ? 0.72f
                                                                                                                : 0.32f )
                             : 0.14f ) );

    const float ragdollCheckX = ragdollVisualToggle.x + 7.0f;
    const float ragdollCheckY = ragdollVisualToggle.y + 5.0f;
    draw.Outline( ragdollCheckX, ragdollCheckY, 10.0f, 10.0f, palette.accent.r, palette.accent.g, palette.accent.b,
                  fadeA( predictionToolsEnabled ? 0.82f : 0.28f ) );

    if ( ragdollVisualsEnabled )
    {
        draw.Rect( ragdollCheckX + 2.0f, ragdollCheckY + 2.0f, 6.0f, 6.0f, palette.accentStrong.r, palette.accentStrong.g,
                   palette.accentStrong.b, fadeA( 0.95f ) );
    }

    drawText( ragdollVisualToggle.x + 23.0f, ragdollVisualToggle.y + 4.5f, 9.0f,
              !predictionToolsEnabled ? palette.textMuted.r
                                      : ( ragdollVisualsEnabled ? palette.accentStrong.r : palette.textSecondary.r ),
              !predictionToolsEnabled ? palette.textMuted.g
                                      : ( ragdollVisualsEnabled ? palette.accentStrong.g : palette.textSecondary.g ),
              !predictionToolsEnabled ? palette.textMuted.b
                                      : ( ragdollVisualsEnabled ? palette.accentStrong.b : palette.textSecondary.b ),
              "RAGDOLL" );

    draw.RoundedRect( pastPathToggle.x, pastPathToggle.y, pastPathToggle.w, pastPathToggle.h, radii.smallButton,
                      pastPathToolsEnabled && isHotControl( ReplayScrubberControl::PastPath ) ? palette.controlHover.r
                                                                                              : palette.control.r,
                      pastPathToolsEnabled && isHotControl( ReplayScrubberControl::PastPath ) ? palette.controlHover.g
                                                                                              : palette.control.g,
                      pastPathToolsEnabled && isHotControl( ReplayScrubberControl::PastPath ) ? palette.controlHover.b
                                                                                              : palette.control.b,
                      fadeA( pastPathToolsEnabled ? 0.88f : 0.38f ) );

    draw.Outline( pastPathToggle.x, pastPathToggle.y, pastPathToggle.w, pastPathToggle.h, palette.accent.r, palette.accent.g,
                  palette.accent.b,
                  fadeA( pastPathToolsEnabled
                             ? ( isHotControl( ReplayScrubberControl::PastPath ) || pastPathEnabled ? 0.72f : 0.32f )
                             : 0.14f ) );

    const float pastCheckX = pastPathToggle.x + 7.0f;
    const float pastCheckY = pastPathToggle.y + 5.0f;
    draw.Outline( pastCheckX, pastCheckY, 10.0f, 10.0f, palette.accent.r, palette.accent.g, palette.accent.b,
                  fadeA( pastPathToolsEnabled ? 0.82f : 0.28f ) );

    if ( pastPathEnabled )
    {
        draw.Rect( pastCheckX + 2.0f, pastCheckY + 2.0f, 6.0f, 6.0f, palette.accentStrong.r, palette.accentStrong.g,
                   palette.accentStrong.b, fadeA( 0.95f ) );
    }

    drawText( pastPathToggle.x + 23.0f, pastPathToggle.y + 4.5f, 9.0f,
              !pastPathToolsEnabled ? palette.textMuted.r
                                    : ( pastPathEnabled ? palette.accentStrong.r : palette.textSecondary.r ),
              !pastPathToolsEnabled ? palette.textMuted.g
                                    : ( pastPathEnabled ? palette.accentStrong.g : palette.textSecondary.g ),
              !pastPathToolsEnabled ? palette.textMuted.b
                                    : ( pastPathEnabled ? palette.accentStrong.b : palette.textSecondary.b ),
              "PAST" );

    if ( predictEnabled )
    {
        const ReplayPredictionPresentationView& prediction = replay.prediction;
        const char* modeLabel = prediction.buildMode == ReplayPredictionBuildMode::Instant     ? "Instant"
                                : prediction.buildMode == ReplayPredictionBuildMode::Amortized ? "Amortized"
                                                                                               : "Measuring";

        char schedulingLabel[128] = {};

        sprintf_s( schedulingLabel, sizeof( schedulingLabel ),
                   "Prediction: %s | Color: %s | %.0f ticks/ms | %.1f ms rebuild", modeLabel, colorModeLabel,
                   prediction.measuredTicksPerMs, prediction.lastBuildWallMs );

        drawText( predict.x, predict.y + 27.0f, 8.0f, palette.textSecondary.r, palette.textSecondary.g,
                  palette.textSecondary.b, schedulingLabel );
    }

    // This row is display-only: the comma binding owns mutation, so replay UI
    // hit testing does not gain a second command path for the same value.
    char colorOptionLabel[64] = {};

    sprintf_s( colorOptionLabel, sizeof( colorOptionLabel ), "COLOR [,]: %s", colorModeLabel );
    drawText( predict.x, predict.y + 38.0f, 8.0f, predictionToolsEnabled ? palette.accentStrong.r : palette.textMuted.r,
              predictionToolsEnabled ? palette.accentStrong.g : palette.textMuted.g,
              predictionToolsEnabled ? palette.accentStrong.b : palette.textMuted.b, colorOptionLabel );

    if ( predictionContactsIncomplete )
    {
        drawText( predict.x, predict.y + 49.0f, 8.0f, palette.warningAccent.r, palette.warningAccent.g,
                  palette.warningAccent.b, "CONTACTS PARTIAL" );
    }

    FlushReplayDrawList( submission, drawList, textBatch, renderTextures, renderCommands, renderDiagnostics, screenW,
                         screenH );
}

void RenderReplayInterceptOverlay( UiDrawSubmission& submission, Text::TextBatch& textBatch, UI::UIDrawList& drawList,
                                   const ReplayOverlayStateView& replay, Rendering::Dx12TextureOwner& renderTextures,
                                   Rendering::Dx12GeometryOwner& renderCommands,
                                   Rendering::Dx12Diagnostics& renderDiagnostics, int screenW, int screenH )
{
    // Why: closest approach is useful while the scrubber is hidden, so this
    // independent Legacy surface is invoked before scrubber visibility policy.
    const ReplayInterceptView& intercept = replay.intercept;

    if ( !intercept.valid || screenW <= 0 || screenH <= 0 )
    {
        return;
    }

    drawList.Clear();
    const UI::UIDrawContext draw( screenW, screenH, drawList );
    const UI::UIRect panel = ReplayInterceptReadoutRect( screenW );
    const UI::Style::UIPalette& palette = UI::Style::Palette();
    const UI::Style::UIRadii& radii = UI::Style::Radii();
    const UI::Style::UIColor accent = intercept.intercept ? palette.accentStrong : palette.warningAccent;

    char label[64] = {};

    if ( intercept.intercept )
    {
        sprintf_s( label, sizeof( label ), "INTERCEPT  ETA %.1fs", intercept.etaSeconds );
    }
    else
    {
        sprintf_s( label, sizeof( label ), "MISS %.1fu  ETA %.1fs", intercept.missDistance, intercept.etaSeconds );
    }

    draw.RoundedPanel( panel, radii.control, palette.windowSubtle, palette.innerBorder );
    const float labelWidth = Text2d::MeasureText( 11.0f, label );
    draw.Text( panel.x + ( panel.w - labelWidth ) * 0.5f, panel.y + 7.0f, 11.0f, accent.r, accent.g, accent.b, label );
    FlushReplayDrawList( submission, drawList, textBatch, renderTextures, renderCommands, renderDiagnostics, screenW,
                         screenH );
}

void RenderReplayTripPlannerOverlay( UiDrawSubmission& submission, Text::TextBatch& textBatch, UI::UIDrawList& drawList,
                                     const ReplayOverlayStateView& replay, Rendering::Dx12TextureOwner& renderTextures,
                                     Rendering::Dx12GeometryOwner& renderCommands,
                                     Rendering::Dx12Diagnostics& renderDiagnostics, int screenW, int screenH )
{
    const ReplayTripPlannerView& planner = replay.tripPlanner;

    if ( !planner.visible || !planner.available || screenW <= 0 || screenH <= 0 )
    {
        return;
    }

    drawList.Clear();
    const UI::UIDrawContext draw( screenW, screenH, drawList );
    const UI::Style::UIPalette& palette = UI::Style::Palette();
    const UI::Style::UIRadii& radii = UI::Style::Radii();

    // Invariant: rendering consumes the same fixed control rectangles that
    // ReplayScrubberTools uses for hit testing; draw and input cannot drift.
    ReplayTripPlannerSurface surface;
    BuildReplayTripPlannerSurface( planner, screenW, surface );
    const UI::UIRect panel = ReplayTripPlannerPanelRect( screenW );
    draw.RoundedPanel( panel, radii.control, palette.windowSubtle, palette.innerBorder );

    const auto control = [&]( ReplayTripPlannerControl id ) -> const RuntimeUiControl&
    {
        const RuntimeUiControl* row = surface.Find( ReplayTripPlannerControlId( id ) );

        if ( !row )
        {
            SB_FATAL( "ReplayTripPlannerSurface", "Render snapshot is missing trip-planner control id=%u.",
                      static_cast<uint32_t>( id ) );
        }

        return *row;
    };

    const auto button = [&]( ReplayTripPlannerControl id, const char* label )
    {
        const RuntimeUiControl& row = control( id );

        const UI::Style::UIColor fill = row.enabled ? palette.control : palette.windowSubtle;
        const UI::Style::UIColor text = row.enabled ? palette.textPrimary : palette.textMuted;
        draw.RoundedRect( row.drawRect.x, row.drawRect.y, row.drawRect.w, row.drawRect.h, radii.smallButton, fill.r, fill.g,
                          fill.b, row.enabled ? 0.92f : 0.45f );

        const float width = Text2d::MeasureText( 9.0f, label );
        draw.Text( row.drawRect.x + ( row.drawRect.w - width ) * 0.5f, row.drawRect.y + 7.0f, 9.0f, text.r, text.g, text.b,
                   label );
    };

    const char* stateLabel = "IDLE";

    switch ( planner.state )
    {
    case ReplayTripPlannerState::Seeding:
        stateLabel = "SEEDING";
        break;
    case ReplayTripPlannerState::AwaitingPrediction:
        stateLabel = "PREDICTING";
        break;
    case ReplayTripPlannerState::Correcting:
        stateLabel = "CORRECTING";
        break;
    case ReplayTripPlannerState::Converged:
        stateLabel = "INTERCEPT";
        break;
    case ReplayTripPlannerState::Failed:
        stateLabel = "NO SOLUTION";
        break;
    case ReplayTripPlannerState::Idle:
        break;
    }

    char title[160] = {};

    if ( planner.iteration > 0 )
    {
        sprintf_s( title, sizeof( title ), "TRIP: %s  TOF %.1fs  ITER %u/%zu  MISS %.2fu  %s",
                   planner.targetName[0] != '\0' ? planner.targetName : "TARGET", planner.timeOfFlightSeconds,
                   planner.iteration, REPLAY_TRIP_PLANNER_MAX_ITERATIONS, planner.missDistance, stateLabel );
    }
    else
    {
        sprintf_s( title, sizeof( title ), "TRIP: %s  TOF %.1fs  %s",
                   planner.targetName[0] != '\0' ? planner.targetName : "TARGET", planner.timeOfFlightSeconds, stateLabel );
    }

    const UI::Style::UIColor statusColor = planner.noSolution ? palette.warningAccent : palette.accentStrong;
    draw.Text( panel.x + 12.0f, panel.y + 13.0f, 10.0f, statusColor.r, statusColor.g, statusColor.b, title );
    button( ReplayTripPlannerControl::TimeOfFlightDecrease, "-" );
    draw.Text( panel.x + 58.0f, panel.y + 59.0f, 9.0f, palette.textPrimary.r, palette.textPrimary.g, palette.textPrimary.b,
               "TOF" );

    button( ReplayTripPlannerControl::TimeOfFlightIncrease, "+" );
    button( ReplayTripPlannerControl::Plan, "PLAN" );
    button( ReplayTripPlannerControl::Commit, "COMMIT" );
    button( ReplayTripPlannerControl::Cancel, "CANCEL" );
    FlushReplayDrawList( submission, drawList, textBatch, renderTextures, renderCommands, renderDiagnostics, screenW,
                         screenH );
}

void RenderReplayPorkchopOverlay( UiDrawSubmission& submission, Text::TextBatch& textBatch, UI::UIDrawList& drawList,
                                  const ReplayOverlayStateView& replay, Rendering::Dx12TextureOwner& renderTextures,
                                  Rendering::Dx12GeometryOwner& renderCommands,
                                  Rendering::Dx12Diagnostics& renderDiagnostics, int screenW, int screenH )
{
    const ReplayPorkchopPanelView& porkchop = replay.porkchop;

    if ( !porkchop.visible || screenW <= 0 || screenH <= 0 )
    {
        return;
    }

    drawList.Clear();
    const UI::UIDrawContext draw( screenW, screenH, drawList );
    const UI::Style::UIPalette& palette = UI::Style::Palette();
    const UI::Style::UIRadii& radii = UI::Style::Radii();
    const UI::UIRect panel = ReplayPorkchopPanelRect( screenW );
    const UI::UIRect grid = ReplayPorkchopGridRect( screenW );
    draw.RoundedPanel( panel, radii.control, palette.windowSubtle, palette.innerBorder );

    char title[128] = {};

    if ( porkchop.building )
    {
        sprintf_s( title, sizeof( title ), "PORKCHOP  64x48  BUILDING %zu/%zu", porkchop.completedCells,
                   REPLAY_PORKCHOP_CELL_COUNT );
    }
    else if ( porkchop.complete )
    {
        sprintf_s( title, sizeof( title ), "PORKCHOP  64x48  MIN %.3f u/s  TOTAL %.2f ms  MAX %.2f ms",
                   porkchop.minimumDeltaV, porkchop.refreshComputeMilliseconds, porkchop.maximumFrameComputeMilliseconds );
    }
    else if ( porkchop.evaluated )
    {
        sprintf_s( title, sizeof( title ), "PORKCHOP  64x48  NO SOLUTION" );
    }
    else
    {
        sprintf_s( title, sizeof( title ), "PORKCHOP  SELECT EARTH DEPARTURE + ORBIT TARGET" );
    }

    const UI::Style::UIColor titleColor = porkchop.available ? palette.textPrimary : palette.warningAccent;
    draw.Text( panel.x + 14.0f, panel.y + 16.0f, 11.0f, titleColor.r, titleColor.g, titleColor.b, title );

    // Concept: low transfer cost is the cool/strong accent; increasingly
    // expensive cells blend toward the existing neutral control color. Failed cells
    // remain the quiet window color and are never selectable.
    // Why: extreme window edges can be orders of magnitude above the useful
    // basin. Clamp only the display range so structure remains visible; the
    // retained values and cell selection stay exact.
    const float displayMaximum = (std::min)( porkchop.maximumDeltaV, porkchop.minimumDeltaV + 20.0f );
    const float costRange = (std::max)( 0.001f, displayMaximum - porkchop.minimumDeltaV );

    for ( std::size_t cellIndex = 0; cellIndex < porkchop.completedCells && cellIndex < porkchop.deltaV.size(); ++cellIndex )
    {
        const float deltaV = porkchop.deltaV[cellIndex];
        const UI::UIRect cell = ReplayPorkchopCellRect( screenW, cellIndex );

        if ( deltaV < 0.0f )
        {
            draw.Rect( cell.x, cell.y, cell.w + 0.25f, cell.h + 0.25f, palette.windowSubtle.r, palette.windowSubtle.g,
                       palette.windowSubtle.b, 0.72f );

            continue;
        }

        const float t = std::clamp( ( deltaV - porkchop.minimumDeltaV ) / costRange, 0.0f, 1.0f );
        const float r = palette.accentStrong.r + ( palette.control.r - palette.accentStrong.r ) * t;
        const float g = palette.accentStrong.g + ( palette.control.g - palette.accentStrong.g ) * t;
        const float b = palette.accentStrong.b + ( palette.control.b - palette.accentStrong.b ) * t;
        draw.Rect( cell.x, cell.y, cell.w + 0.25f, cell.h + 0.25f, r, g, b, 0.90f );
    }

    const auto outlineCell = [&]( int cellIndex, const UI::Style::UIColor& color, float thickness )
    {
        if ( cellIndex < 0 || static_cast<std::size_t>( cellIndex ) >= porkchop.deltaV.size() )
        {
            return;
        }

        const UI::UIRect cell = ReplayPorkchopCellRect( screenW, static_cast<std::size_t>( cellIndex ) );
        draw.Rect( cell.x, cell.y, cell.w, thickness, color.r, color.g, color.b, 1.0f );
        draw.Rect( cell.x, cell.y + cell.h - thickness, cell.w, thickness, color.r, color.g, color.b, 1.0f );
        draw.Rect( cell.x, cell.y, thickness, cell.h, color.r, color.g, color.b, 1.0f );
        draw.Rect( cell.x + cell.w - thickness, cell.y, thickness, cell.h, color.r, color.g, color.b, 1.0f );
    };

    outlineCell( porkchop.selectedCell, palette.textPrimary, 1.5f );
    outlineCell( porkchop.hoveredCell, palette.accentStrong, 1.0f );

    draw.Text( grid.x, grid.y + grid.h + 9.0f, 9.0f, palette.textMuted.r, palette.textMuted.g, palette.textMuted.b,
               "SNAPSHOT +0s" );

    draw.Text( grid.x + grid.w - 48.0f, grid.y + grid.h + 9.0f, 9.0f, palette.textMuted.r, palette.textMuted.g,
               palette.textMuted.b, "+48s" );

    draw.Text( grid.x - 30.0f, grid.y, 8.0f, palette.textMuted.r, palette.textMuted.g, palette.textMuted.b, "2s" );
    draw.Text( grid.x - 34.0f, grid.y + grid.h - 8.0f, 8.0f, palette.textMuted.r, palette.textMuted.g, palette.textMuted.b,
               "20s" );

    int readoutCell = porkchop.hoveredCell;

    if ( readoutCell < 0 && porkchop.complete )
    {
        readoutCell = static_cast<int>( porkchop.minimumCell );
    }

    char readout[160] = {};

    if ( readoutCell >= 0 && static_cast<std::size_t>( readoutCell ) < porkchop.deltaV.size() &&
         porkchop.deltaV[static_cast<std::size_t>( readoutCell )] >= 0.0f )
    {
        const std::size_t index = static_cast<std::size_t>( readoutCell );
        const float remainingWait = (std::max)( 0.0f, ReplayPorkchopPanel::DepartureDelaySeconds( index %
                                                                                                  REPLAY_PORKCHOP_COLUMNS ) -
                                                          porkchop.sweepAgeSeconds );

        sprintf_s( readout, sizeof( readout ), "WAIT %.2fs NOW   TOF %.2fs   DV %.3f u/s   CLICK TO SEED TRIP TOF",
                   remainingWait, ReplayPorkchopPanel::TimeOfFlightSeconds( index / REPLAY_PORKCHOP_COLUMNS ),
                   porkchop.deltaV[index] );
    }
    else if ( readoutCell >= 0 )
    {
        sprintf_s( readout, sizeof( readout ), "NO SOLUTION FOR THIS CELL" );
    }

    if ( porkchop.selectedCell >= 0 )
    {
        sprintf_s( readout, sizeof( readout ), "RECOMMENDED WAIT %.2fs   SEEDED TOF %.2fs   DV %.3f u/s",
                   porkchop.selectedDepartureDelaySeconds, porkchop.selectedTimeOfFlightSeconds, porkchop.selectedDeltaV );
    }

    draw.Text( panel.x + 14.0f, panel.y + panel.h - 28.0f, 10.0f, palette.accentStrong.r, palette.accentStrong.g,
               palette.accentStrong.b, readout );

    FlushReplayDrawList( submission, drawList, textBatch, renderTextures, renderCommands, renderDiagnostics, screenW,
                         screenH );
}

void RenderReplayCauseTreeOverlay( UiDrawSubmission& submission, Text::TextBatch& textBatch, UI::UIDrawList& drawList,
                                   const ReplayOverlayStateView& replay, Rendering::Dx12TextureOwner& renderTextures,
                                   Rendering::Dx12GeometryOwner& renderCommands,
                                   Rendering::Dx12Diagnostics& renderDiagnostics, Core::Profiler*, int screenW, int screenH )
{
    PROFILE_SCOPED( "Frame/Replay/CauseTree/Overlay" );

    if ( screenW <= 0 || screenH <= 0 || replay.causeTree.rows.empty() )
    {
        return;
    }

    // Invariant: input clamps the authoring-owned window before publication;
    // drawing consumes that const state and never repairs layout as a side effect.
    ReplayCauseWindowSurface surface;
    BuildReplayCauseWindowSurface( replay.causeTree, surface );
    surface.ResolvePointer( replay.causeTree.mouseX, replay.causeTree.mouseY, replay.causeTree.pointerBlocked );
    const auto controlRect = [&]( ReplayCauseWindowControl id ) -> const UI::UIRect&
    {
        const RuntimeUiControl* row = surface.Find( ReplayCauseWindowControlId( id ) );

        if ( !row )
        {
            SB_FATAL( "ReplayCauseWindowSurface", "Render snapshot is missing cause-window control id=%u.",
                      static_cast<uint32_t>( id ) );
        }

        return row->drawRect;
    };

    const UI::UIRect panel = controlRect( ReplayCauseWindowControl::Panel );
    const UI::UIRect title = controlRect( ReplayCauseWindowControl::Title );
    const UI::UIRect content = controlRect( ReplayCauseWindowControl::Content );
    const UI::UIRect resize = controlRect( ReplayCauseWindowControl::Resize );

    drawList.Clear();
    const UI::UIDrawContext draw( screenW, screenH, drawList );
    const UI::Style::UIPalette& palette = UI::Style::Palette();
    const UI::Style::UIRadii& radii = UI::Style::Radii();
    UI::Style::UIColor panelFill = palette.windowSubtle;
    panelFill.a = 0.93f;
    UI::Style::UIColor panelBorder = palette.innerBorder;
    panelBorder.a = 0.58f;
    draw.RoundedPanel( panel, radii.window, panelFill, panelBorder );
    draw.Rect( title.x + 12.0f, title.y + title.h - 1.0f, (std::max)( 0.0f, title.w - 24.0f ), 1.0f, palette.innerBorder.r,
               palette.innerBorder.g, palette.innerBorder.b, 0.42f );

    draw.Text( panel.x + 12.0f, panel.y + 10.0f, 13.5f, palette.textPrimary.r, palette.textPrimary.g, palette.textPrimary.b,
               "REPLAY CAMERA" );

    draw.Text( panel.x + 136.0f, panel.y + 12.0f, 11.0f, palette.textSecondary.r, palette.textSecondary.g,
               palette.textSecondary.b, "CAUSE" );

    const bool predictionRows = !replay.causeTree.rows.empty() && replay.causeTree.rows.front().prediction;
    const char* sourceLabel = predictionRows ? "PREDICT" : "REPLAY";
    const float sourceW = Text2d::MeasureText( 9.5f, sourceLabel );
    draw.RoundedRect( panel.x + panel.w - sourceW - 26.0f, panel.y + 9.0f, sourceW + 14.0f, 18.0f, radii.smallButton,
                      predictionRows ? palette.controlHover.r : palette.control.r,
                      predictionRows ? palette.controlHover.g : palette.control.g,
                      predictionRows ? palette.controlHover.b : palette.control.b, 0.80f );

    draw.Text( panel.x + panel.w - sourceW - 19.0f, panel.y + 13.0f, 9.5f,
               predictionRows ? palette.accentStrong.r : palette.accent.r,
               predictionRows ? palette.accentStrong.g : palette.accent.g,
               predictionRows ? palette.accentStrong.b : palette.accent.b, sourceLabel );

    draw.RoundedRect( content.x, content.y, content.w, content.h, radii.control, palette.window.r, palette.window.g,
                      palette.window.b, 0.46f );

    const ReplaySolverFrameSample* scrubSample = replay.selection.currentSolver;
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
    const int firstRow = (std::max)( 0, static_cast<int>( floorf( replay.causeTree.scrollY / REPLAY_CAUSE_WINDOW_ROW_HEIGHT ) ) );

    const int rowCount = static_cast<int>( replay.causeTree.rows.size() );
    int hoveredRow = -1;

    if ( surface.hasHotControl && surface.hotControl == ReplayCauseWindowControlId( ReplayCauseWindowControl::Content ) )
    {
        const float localY = static_cast<float>( replay.causeTree.mouseY ) - content.y + replay.causeTree.scrollY;
        const int candidate = static_cast<int>( floorf( localY / REPLAY_CAUSE_WINDOW_ROW_HEIGHT ) );

        if ( candidate >= 0 && candidate < rowCount )
        {
            hoveredRow = candidate;
        }
    }

    for ( int rowIndex = firstRow; rowIndex < rowCount; ++rowIndex )
    {
        const RunReplayCauseTreeRow& row = replay.causeTree.rows[static_cast<std::size_t>( rowIndex )];
        const float rowY = content.y + static_cast<float>( rowIndex ) * REPLAY_CAUSE_WINDOW_ROW_HEIGHT -
                           replay.causeTree.scrollY;

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
        const bool selected = rowIndex == replay.causeTree.selectedRow;

        if ( hovered || selected )
        {
            const UI::Style::UIColor& rowFill = selected ? palette.controlHover : palette.control;
            draw.RoundedRect( rowRect.x, rowRect.y, rowRect.w, rowRect.h, radii.smallButton, rowFill.r, rowFill.g, rowFill.b,
                              hovered ? 0.82f : 0.56f );
        }

        const float indent = (std::min)( rowRect.w * 0.40f, static_cast<float>( row.depth ) * 16.0f );

        if ( row.depth > 0 )
        {
            const float lineX = rowRect.x + 8.0f + indent - 9.0f;
            draw.Rect( lineX, rowRect.y + 4.0f, 1.0f, rowRect.h - 8.0f, palette.innerBorder.r, palette.innerBorder.g,
                       palette.innerBorder.b, 0.34f );

            draw.Rect( lineX, rowRect.y + rowRect.h * 0.5f, 8.0f, 1.0f, palette.innerBorder.r, palette.innerBorder.g,
                       palette.innerBorder.b, 0.34f );
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
        draw.Text( markerX + 11.0f, rowRect.y + 4.0f, 12.4f,
                   row.kind == RunReplayCauseTreeRowKind::Body ? palette.textPrimary.r : palette.textSecondary.r,
                   row.kind == RunReplayCauseTreeRowKind::Body ? palette.textPrimary.g : palette.textSecondary.g,
                   row.kind == RunReplayCauseTreeRowKind::Body ? palette.textPrimary.b : palette.textSecondary.b,
                   clippedLabel );

        if ( clippedDetail[0] != '\0' )
        {
            draw.Text( markerX + 11.0f, rowRect.y + 22.0f, 10.4f, palette.textMuted.r, palette.textMuted.g,
                       palette.textMuted.b, clippedDetail );
        }
    }

    const float maxScroll = ReplayCauseWindowMaxScroll( replay.causeTree );

    if ( maxScroll > 0.0f )
    {
        const float trackX = content.x + content.w - 5.0f;
        draw.Rect( trackX, content.y + 3.0f, 3.0f, content.h - 6.0f, palette.control.r, palette.control.g, palette.control.b,
                   0.72f );

        const float contentHeight = ReplayCauseWindowContentHeight( replay.causeTree );
        const float knobH = (std::max)( 24.0f, ( content.h / contentHeight ) * ( content.h - 6.0f ) );
        const float knobY = content.y + 3.0f + ( replay.causeTree.scrollY / maxScroll ) * ( content.h - 6.0f - knobH );
        draw.RoundedRect( trackX - 1.0f, knobY, 5.0f, knobH, 2.0f, palette.accent.r, palette.accent.g, palette.accent.b,
                          0.78f );
    }

    draw.Rect( resize.x + 4.0f, resize.y + resize.h - 5.0f, resize.w - 7.0f, 1.0f, palette.innerBorder.r,
               palette.innerBorder.g, palette.innerBorder.b, 0.68f );

    draw.Rect( resize.x + resize.w - 5.0f, resize.y + 4.0f, 1.0f, resize.h - 7.0f, palette.innerBorder.r,
               palette.innerBorder.g, palette.innerBorder.b, 0.68f );

    RenderReplayCauseSolverDetailPanel( draw, replay, screenW, screenH );

    FlushReplayDrawList( submission, drawList, textBatch, renderTextures, renderCommands, renderDiagnostics, screenW,
                         screenH );
}
} // namespace SkullbonezCore::Runtime::ReplayOverlay
