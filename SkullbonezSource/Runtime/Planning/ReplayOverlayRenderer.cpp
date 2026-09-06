/*
File: SkullbonezSource/Runtime/Planning/ReplayOverlayRenderer.cpp
Purpose:
  Composes replay scrubber, cause-tree, and solver-detail UI commands from owner views.

Summary:
  Replay overlay composition produces one detached late-UI value. Keep the same screen-space layout
  and pointer eligibility as replay input by rebuilding the same fixed-capacity
  surfaces from ReplayOverlayLayout. The causal solver panel renders exact-frame
  copied evidence in a flush attached drawer projected from the cause-tree
  anchor through Planning's shared compound layout. The hierarchy renders the
  same bounded filtered source-index projection consumed by Replay input.

Invariants:
  - Drawn controls use the same surface rows and pointer-block fact as input, so
    visible hover and actionable hit state stay identical.
  - Overlay rendering reads replay state only; replay mutation belongs to input
    and runtime replay helpers.
  - Solver rows expose their units and sign conventions on the surface; missing
    stages render neutral values without inventing a replacement record.
  - Inspector draw bounds come from the same pure projection used by input;
    no more than four complete contact rows appear at once, and unavailable
    detail never reserves row-sized empty space.
  - Filter labels, ancestry, selection, and footer counts are projections only;
    renderer code never mutates or duplicates retained evidence.

Related:
  - SkullbonezSource/Runtime/Planning/ReplayOverlayRenderer.h
  - SkullbonezSource/Runtime/Replay/ReplayOverlayLayout.h
  - Agentic/Reference/engine-glossary.md
*/
#include "ReplayOverlayRenderer.h"

#include "ReplayPlanningOverlayLayout.h"
#include "../../Core/FatalError.h"
#include "../../Assets/AssetKeys.h"
#include "../Replay/ReplayOverlayLayout.h"
#include "../../Core/Common.h"
#include "../../Core/Profiler.h"
#include "../../Physics/PhysicsTimestep.h"
#include "../../UI/UIDraw.h"
#include "../../UI/UIDrawList.h"
#include "../../UI/UIDrawWidgets.h"
#include "../../UI/UIFontMetrics.h"
#include "../../UI/UIStyle.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>

namespace SkullbonezCore::Runtime::ReplayOverlay
{
using namespace ReplayScrubberOperations;

static void ComposeReplayInterceptOverlay( UI::UIDrawList& drawList, const ReplayInterceptView& intercept, int screenW,
                                           int screenH );
static void ComposeReplayTripPlannerOverlay( UI::UIDrawList& drawList, const ReplayTripPlannerView& planner, int screenW,
                                             int screenH );
static void ComposeReplayPorkchopOverlay( UI::UIDrawList& drawList, const ReplayPorkchopPanelView& porkchop, int screenW,
                                          int screenH );
static void ComposeReplayCauseTreeOverlay( UI::UIDrawList& drawList, const ReplayOverlayCausalityView& causality,
                                           const ReplayPresentationSelection& selection, int screenW, int screenH );

namespace
{
// Concept: the scientific inspector uses one deliberately restrained colour
// system. Evidence families keep their colour across the hierarchy and drawer,
// while navy surfaces preserve contrast over a bright simulation viewport.
constexpr UI::Style::UIColor CAUSE_NAVY { 0.0f, 0.0745f, 0.1333f, 0.97f };
constexpr UI::Style::UIColor CAUSE_NAVY_ALT { 0.0314f, 0.0980f, 0.1608f, 0.96f };
constexpr UI::Style::UIColor CAUSE_SELECTED { 0.0f, 0.1922f, 0.3373f, 0.98f };
constexpr UI::Style::UIColor CAUSE_RULE { 0.0f, 0.6431f, 0.9255f, 1.0f };
constexpr UI::Style::UIColor CAUSE_PREDICTION { 0.6510f, 0.8078f, 0.4824f, 1.0f };
constexpr UI::Style::UIColor CAUSE_MANIFOLD { 0.1294f, 0.6941f, 0.8275f, 1.0f };
constexpr UI::Style::UIColor CAUSE_SOLVER { 0.9333f, 0.4157f, 0.2078f, 1.0f };

bool ReplayPredictionContactsIncomplete( const ReplayPredictionTimelineView& timeline )
{
    // Concept: contact payloads are optional prediction evidence. The root path
    // can still be correct when contact-tree rows are partial, but the overlay
    // should label that loss instead of implying a complete causal tree.
    for ( const RunReplayPredictionFrame& frame : timeline.frames )
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

static inline int ReplayCauseSelectedContactIndex( const ReplayCauseInspectionView& inspection ) noexcept
{
    if ( inspection.Selection().selectedDetailContactRow >= 0 &&
         static_cast<std::size_t>( inspection.Selection().selectedDetailContactRow ) <
             inspection.SolverDetail().solverDetailContacts.size() )
    {
        return inspection.Selection().selectedDetailContactRow;
    }

    return 0;
}

static void RenderReplayCauseSummaryTab( const UI::UIDrawContext& draw, const ReplayCauseInspectionView& inspection,
                                         const ReplayCauseInspectorLayout& layout, const UI::Style::UIPalette& palette )
{
    const int contactIndex = ReplayCauseSelectedContactIndex( inspection );
    const Physics::PhysicsSolverPersistentContactSample& contact = inspection.SolverDetail()
                                                                       .solverDetailContacts[contactIndex];
    const ReplayCauseSummaryText summary = BuildReplayCauseSummaryText( inspection.SolverDetail(), contactIndex );
    static constexpr const char* CARD_LABELS[] = { "NORMAL IMPULSE", "FRICTION |T|", "PENETRATION", "EFFECTIVE MASS" };
    const char* cardValues[] = { summary.normalImpulse, summary.frictionImpulse, summary.penetration,
                                 summary.effectiveMass };
    const float gap = 8.0f;
    const float cardW = ( layout.content.w - gap ) * 0.5f;
    const float cardH = 56.0f;

    for ( int cardIndex = 0; cardIndex < 4; ++cardIndex )
    {
        const int column = cardIndex % 2;
        const int row = cardIndex / 2;
        const UI::UIRect card { layout.content.x + static_cast<float>( column ) * ( cardW + gap ),
                                layout.content.y + static_cast<float>( row ) * ( cardH + gap ), cardW, cardH };
        const UI::Style::UIColor& accent = cardIndex < 2 ? CAUSE_SOLVER : CAUSE_MANIFOLD;
        draw.RoundedRect( card.x, card.y, card.w, card.h, 5.0f, CAUSE_NAVY_ALT.r, CAUSE_NAVY_ALT.g, CAUSE_NAVY_ALT.b, 1.0f );
        draw.Rect( card.x, card.y, 3.0f, card.h, accent.r, accent.g, accent.b, 0.9f );
        draw.Text( card.x + 11.0f, card.y + 9.0f, 9.0f, palette.textMuted.r, palette.textMuted.g, palette.textMuted.b,
                   CARD_LABELS[cardIndex] );
        draw.Text( card.x + 11.0f, card.y + 27.0f, 15.0f, accent.r, accent.g, accent.b, cardValues[cardIndex] );
    }

    const float basisY = layout.content.y + 128.0f;
    draw.Text( layout.content.x, basisY, 10.0f, CAUSE_PREDICTION.r, CAUSE_PREDICTION.g, CAUSE_PREDICTION.b,
               "CONTACT BASIS COMPONENTS  [-1, +1]" );
    const Math::Vector::Vector3 basis[] = { contact.normal, contact.tangent1, contact.tangent2 };
    static constexpr const char* BASIS_LABELS[] = { "NORMAL", "TANGENT 1", "TANGENT 2" };

    for ( int basisIndex = 0; basisIndex < 3; ++basisIndex )
    {
        const float rowY = basisY + 20.0f + static_cast<float>( basisIndex ) * 34.0f;
        const float components[] = { basis[basisIndex].x, basis[basisIndex].y, basis[basisIndex].z };
        draw.Text( layout.content.x, rowY + 5.0f, 9.5f, palette.textSecondary.r, palette.textSecondary.g,
                   palette.textSecondary.b, BASIS_LABELS[basisIndex] );

        for ( int componentIndex = 0; componentIndex < 3; ++componentIndex )
        {
            const float componentX = layout.content.x + 80.0f + static_cast<float>( componentIndex ) * 118.0f;
            const float centerX = componentX + 48.0f;
            const float value = std::clamp( components[componentIndex], -1.0f, 1.0f );
            draw.Rect( componentX, rowY, 96.0f, 18.0f, CAUSE_NAVY_ALT.r, CAUSE_NAVY_ALT.g, CAUSE_NAVY_ALT.b, 1.0f );
            draw.Rect( centerX, rowY + 2.0f, 1.0f, 14.0f, CAUSE_RULE.r, CAUSE_RULE.g, CAUSE_RULE.b, 0.45f );
            draw.Rect( value < 0.0f ? centerX + value * 42.0f : centerX, rowY + 5.0f, std::abs( value ) * 42.0f, 8.0f,
                       CAUSE_PREDICTION.r, CAUSE_PREDICTION.g, CAUSE_PREDICTION.b, 0.88f );
            char component[32] = {};
            sprintf_s( component, sizeof( component ), "%c %.3f", "XYZ"[componentIndex], components[componentIndex] );
            draw.Text( componentX + 5.0f, rowY + 4.0f, 8.5f, palette.textPrimary.r, palette.textPrimary.g,
                       palette.textPrimary.b, component );
        }
    }

    const float dynamicsY = basisY + 128.0f;
    draw.RoundedRect( layout.content.x, dynamicsY, layout.content.w, 86.0f, 5.0f, CAUSE_NAVY_ALT.r, CAUSE_NAVY_ALT.g,
                      CAUSE_NAVY_ALT.b, 1.0f );
    draw.Text( layout.content.x + 10.0f, dynamicsY + 10.0f, 10.0f, CAUSE_MANIFOLD.r, CAUSE_MANIFOLD.g, CAUSE_MANIFOLD.b,
               "ROW DYNAMICS" );
    draw.Text( layout.content.x + 10.0f, dynamicsY + 28.0f, 9.5f, palette.textPrimary.r, palette.textPrimary.g,
               palette.textPrimary.b, summary.identity );
    draw.Text( layout.content.x + 10.0f, dynamicsY + 46.0f, 9.0f, palette.textSecondary.r, palette.textSecondary.g,
               palette.textSecondary.b, summary.dynamics );
    draw.Text( layout.content.x + 10.0f, dynamicsY + 64.0f, 9.0f, palette.textMuted.r, palette.textMuted.g,
               palette.textMuted.b, summary.policy );
}

static void RenderReplayCauseRawRecordTab( UI::UIDrawList& drawList, const UI::UIDrawContext& draw,
                                           const ReplayCauseInspectionView& inspection,
                                           const ReplayCauseInspectorLayout& layout, const UI::Style::UIPalette& palette )
{
    const int contactIndex = ReplayCauseSelectedContactIndex( inspection );
    const ReplayCauseRawRecordProjection projection = BuildReplayCauseRawRecordProjection( inspection.SolverDetail(),
                                                                                           inspection.Transport(),
                                                                                           contactIndex );
    const int rowCount = static_cast<int>( projection.rowCount );
    const int firstRow = std::clamp( inspection.Display().rawRecordFirstRow, 0,
                                     (std::max)( 0, rowCount - layout.rawVisibleRows ) );
    const int endRow = (std::min)( rowCount, firstRow + layout.rawVisibleRows );

    drawList.PushClip( layout.rawTable );

    const bool hasScrollbar = rowCount > layout.rawVisibleRows;
    const float tableRowWidth = layout.rawTable.w - ( hasScrollbar ? REPLAY_CAUSE_INSPECTOR_SCROLLBAR_WIDTH + 4.0f : 0.0f );

    for ( int rowIndex = firstRow; rowIndex < endRow; ++rowIndex )
    {
        const ReplayCauseRawRecordRow& row = projection.rows[static_cast<std::size_t>( rowIndex )];
        const float rowY = layout.rawTable.y +
                           static_cast<float>( rowIndex - firstRow ) * REPLAY_CAUSE_RAW_RECORD_ROW_HEIGHT;

        if ( row.kind == ReplayCauseRawRecordRowKind::Section )
        {
            draw.Rect( layout.rawTable.x, rowY + REPLAY_CAUSE_RAW_RECORD_ROW_HEIGHT - 1.0f, tableRowWidth, 1.0f,
                       CAUSE_RULE.r, CAUSE_RULE.g, CAUSE_RULE.b, 0.45f );
            draw.Text( layout.rawTable.x + 4.0f, rowY + 4.0f, 10.0f, CAUSE_MANIFOLD.r, CAUSE_MANIFOLD.g, CAUSE_MANIFOLD.b,
                       row.label );
        }
        else
        {
            const UI::Style::UIColor& fill = ( rowIndex % 2 == 0 ) ? CAUSE_NAVY_ALT : CAUSE_NAVY;
            draw.RoundedRect( layout.rawTable.x, rowY, tableRowWidth, REPLAY_CAUSE_RAW_RECORD_ROW_HEIGHT - 1.0f, 2.0f,
                              fill.r, fill.g, fill.b, 1.0f );
            draw.Text( layout.rawTable.x + 8.0f, rowY + 4.0f, 9.5f, palette.textSecondary.r, palette.textSecondary.g,
                       palette.textSecondary.b, row.label );
            draw.Text( layout.rawTable.x + 190.0f, rowY + 4.0f, 9.5f, palette.textPrimary.r, palette.textPrimary.g,
                       palette.textPrimary.b, row.value );

            if ( row.unit[0] != '\0' )
            {
                draw.Text( layout.rawTable.x + tableRowWidth - 48.0f, rowY + 4.0f, 9.0f, palette.textMuted.r,
                           palette.textMuted.g, palette.textMuted.b, row.unit );
            }
        }
    }

    drawList.PopClip();

    if ( hasScrollbar && rowCount > 0 )
    {
        draw.RoundedRect( layout.drawerScrollbar.x, layout.drawerScrollbar.y, layout.drawerScrollbar.w,
                          layout.drawerScrollbar.h, 2.0f, CAUSE_NAVY_ALT.r, CAUSE_NAVY_ALT.g, CAUSE_NAVY_ALT.b, 0.8f );
        const float trackH = layout.drawerScrollbar.h;
        const float thumbH = (std::max)( 16.0f, trackH * static_cast<float>( layout.rawVisibleRows ) /
                                                    static_cast<float>( rowCount ) );
        const float maxScroll = static_cast<float>( (std::max)( 1, rowCount - layout.rawVisibleRows ) );
        const float thumbY = layout.drawerScrollbar.y + ( trackH - thumbH ) * ( static_cast<float>( firstRow ) / maxScroll );
        draw.RoundedRect( layout.drawerScrollbar.x, thumbY, layout.drawerScrollbar.w, thumbH, 2.0f, CAUSE_RULE.r,
                          CAUSE_RULE.g, CAUSE_RULE.b, 0.85f );
    }

    // Render Copy Record action button
    draw.RoundedRect( layout.rawCopy.x, layout.rawCopy.y, layout.rawCopy.w, layout.rawCopy.h, 4.0f, CAUSE_NAVY_ALT.r,
                      CAUSE_NAVY_ALT.g, CAUSE_NAVY_ALT.b, 1.0f );
    draw.Rect( layout.rawCopy.x, layout.rawCopy.y, 3.0f, layout.rawCopy.h, CAUSE_MANIFOLD.r, CAUSE_MANIFOLD.g,
               CAUSE_MANIFOLD.b, 0.88f );
    draw.Text( layout.rawCopy.x + 16.0f, layout.rawCopy.y + 8.0f, 11.0f, palette.textPrimary.r, palette.textPrimary.g,
               palette.textPrimary.b, "COPY RECORD" );
    draw.Text( layout.rawCopy.x + layout.rawCopy.w - 140.0f, layout.rawCopy.y + 9.0f, 9.0f, palette.textMuted.r,
               palette.textMuted.g, palette.textMuted.b, "Copy text to clipboard" );
}

static void RenderReplayCauseIterationsTab( UI::UIDrawList& drawList, const UI::UIDrawContext& draw,
                                            const ReplayCauseInspectionView& inspection,
                                            const ReplayCauseInspectorLayout& layout, const UI::Style::UIPalette& palette )
{
    const int contactIndex = ReplayCauseSelectedContactIndex( inspection );
    const ReplayCauseIterationsProjection projection = BuildReplayCauseIterationsProjection( inspection.SolverDetail(),
                                                                                             contactIndex );
    const int rowCount = static_cast<int>( projection.rowCount );
    const int firstRow = std::clamp( inspection.Display().iterationsFirstRow, 0,
                                     (std::max)( 0, rowCount - layout.iterationsVisibleRows ) );
    const int endRow = (std::min)( rowCount, firstRow + layout.iterationsVisibleRows );

    draw.Text( layout.content.x, layout.content.y + 2.0f, 9.5f, CAUSE_SOLVER.r, CAUSE_SOLVER.g, CAUSE_SOLVER.b,
               projection.summary );

    if ( rowCount == 0 )
    {
        draw.RoundedRect( layout.iterationsTable.x, layout.iterationsTable.y, layout.iterationsTable.w, 40.0f, 4.0f,
                          CAUSE_NAVY_ALT.r, CAUSE_NAVY_ALT.g, CAUSE_NAVY_ALT.b, 1.0f );
        draw.Text( layout.iterationsTable.x + 10.0f, layout.iterationsTable.y + 14.0f, 10.0f, palette.textMuted.r,
                   palette.textMuted.g, palette.textMuted.b, "No iteration pipeline records for selected contact" );
        return;
    }

    drawList.PushClip( layout.iterationsTable );

    const bool hasScrollbar = rowCount > layout.iterationsVisibleRows;
    const float tableRowWidth = layout.iterationsTable.w -
                                ( hasScrollbar ? REPLAY_CAUSE_INSPECTOR_SCROLLBAR_WIDTH + 4.0f : 0.0f );

    for ( int rowIndex = firstRow; rowIndex < endRow; ++rowIndex )
    {
        const ReplayCauseIterationRow& row = projection.rows[static_cast<std::size_t>( rowIndex )];
        const float rowY = layout.iterationsTable.y +
                           static_cast<float>( rowIndex - firstRow ) * REPLAY_CAUSE_ITERATIONS_ROW_HEIGHT;
        const UI::Style::UIColor& fill = ( rowIndex % 2 == 0 ) ? CAUSE_NAVY_ALT : CAUSE_NAVY;
        draw.RoundedRect( layout.iterationsTable.x, rowY, tableRowWidth, REPLAY_CAUSE_ITERATIONS_ROW_HEIGHT - 1.0f, 2.0f,
                          fill.r, fill.g, fill.b, 1.0f );

        draw.Text( layout.iterationsTable.x + 6.0f, rowY + 4.0f, 9.0f, palette.textPrimary.r, palette.textPrimary.g,
                   palette.textPrimary.b, row.stage );

        if ( row.kind == ReplayCauseIterationRowKind::SolverIteration )
        {
            draw.Text( layout.iterationsTable.x + 76.0f, rowY + 4.0f, 8.5f, palette.textSecondary.r, palette.textSecondary.g,
                       palette.textSecondary.b, row.deltaNormal );
            draw.Text( layout.iterationsTable.x + 140.0f, rowY + 4.0f, 8.5f, palette.textPrimary.r, palette.textPrimary.g,
                       palette.textPrimary.b, row.accNormal );
            draw.Text( layout.iterationsTable.x + 204.0f, rowY + 4.0f, 8.5f, palette.textPrimary.r, palette.textPrimary.g,
                       palette.textPrimary.b, row.tangentImpulse );
            const bool isClamped = ( std::strcmp( row.status, "CLAMP" ) == 0 );
            const UI::Style::UIColor& statusColor = isClamped ? CAUSE_SOLVER : palette.textMuted;
            draw.Text( layout.iterationsTable.x + 276.0f, rowY + 4.0f, 8.5f, statusColor.r, statusColor.g, statusColor.b,
                       row.status );
        }
        else
        {
            if ( row.accNormal[0] != '\0' )
            {
                draw.Text( layout.iterationsTable.x + 140.0f, rowY + 4.0f, 8.5f, palette.textPrimary.r,
                           palette.textPrimary.g, palette.textPrimary.b, row.accNormal );
            }
            else if ( row.deltaNormal[0] != '\0' )
            {
                draw.Text( layout.iterationsTable.x + 76.0f, rowY + 4.0f, 8.5f, palette.textSecondary.r,
                           palette.textSecondary.g, palette.textSecondary.b, row.deltaNormal );
            }

            if ( row.status[0] != '\0' )
            {
                draw.Text( layout.iterationsTable.x + 276.0f, rowY + 4.0f, 8.5f, CAUSE_MANIFOLD.r, CAUSE_MANIFOLD.g,
                           CAUSE_MANIFOLD.b, row.status );
            }

            if ( row.details[0] != '\0' )
            {
                draw.Text( layout.iterationsTable.x + 330.0f, rowY + 4.0f, 8.5f, palette.textMuted.r, palette.textMuted.g,
                           palette.textMuted.b, row.details );
            }
        }
    }

    drawList.PopClip();

    if ( hasScrollbar && rowCount > 0 )
    {
        draw.RoundedRect( layout.drawerScrollbar.x, layout.drawerScrollbar.y, layout.drawerScrollbar.w,
                          layout.drawerScrollbar.h, 2.0f, CAUSE_NAVY_ALT.r, CAUSE_NAVY_ALT.g, CAUSE_NAVY_ALT.b, 0.8f );
        const float trackH = layout.drawerScrollbar.h;
        const float thumbH = (std::max)( 16.0f, trackH * static_cast<float>( layout.iterationsVisibleRows ) /
                                                    static_cast<float>( rowCount ) );
        const float maxScroll = static_cast<float>( (std::max)( 1, rowCount - layout.iterationsVisibleRows ) );
        const float thumbY = layout.drawerScrollbar.y + ( trackH - thumbH ) * ( static_cast<float>( firstRow ) / maxScroll );
        draw.RoundedRect( layout.drawerScrollbar.x, thumbY, layout.drawerScrollbar.w, thumbH, 2.0f, CAUSE_RULE.r,
                          CAUSE_RULE.g, CAUSE_RULE.b, 0.85f );
    }
}

void RenderReplayCauseSolverDetailPanel( UI::UIDrawList& drawList, const UI::UIDrawContext& draw,
                                         const ReplayOverlayCausalityView& causality, int screenW, int screenH )
{
    PROFILE_SCOPED( "Frame/Replay/RenderCauseInspectorDrawer" );
    const ReplayCauseInspectionView& inspection = causality.inspection;

    if ( !inspection.Display().detailVisible || causality.tree.rows.empty() )
    {
        return;
    }

    const ReplayCauseInspectorLayout layout = BuildReplayCauseInspectorLayout( inspection.SolverDetail(), causality.tree,
                                                                               screenW, screenH,
                                                                               inspection.Display().drawerProgress );

    if ( layout.visibleDrawer.w <= 1.0f )
    {
        return;
    }

    // Invariant: the moving drawer is painted at its final coordinates behind
    // the hierarchy and clipped to the exposed slice. No hidden tab can render
    // or receive input before its pixels become visible.
    drawList.PushClip( layout.visibleDrawer );
    const UI::Style::UIPalette& palette = UI::Style::Palette();
    UI::Style::UIColor drawerFill = CAUSE_NAVY;
    drawerFill.a = REPLAY_CAUSE_SOLVER_PANEL_OPACITY;
    UI::Style::UIColor drawerBorder = CAUSE_RULE;
    drawerBorder.a = 0.72f;
    draw.RoundedPanel( layout.drawer, 6.0f, drawerFill, drawerBorder );
    draw.Rect( layout.drawer.x, layout.drawer.y, 3.0f, layout.drawer.h, CAUSE_RULE.r, CAUSE_RULE.g, CAUSE_RULE.b, 0.92f );
    draw.Text( layout.drawerTitle.x + 12.0f, layout.drawerTitle.y + 9.0f, 14.0f, palette.textPrimary.r,
               palette.textPrimary.g, palette.textPrimary.b, "SOLVER INSPECTOR" );

    char frameLabel[96] = {};
    sprintf_s( frameLabel, sizeof( frameLabel ), "FRAME %llu | %zu ROWS%s",
               static_cast<unsigned long long>( inspection.Transport().targetFrame ),
               inspection.SolverDetail().solverDetailContacts.size(),
               inspection.SolverDetail().contactPresentation.truncated ? " | PATCH TRUNCATED" : "" );
    draw.Text( layout.drawerTitle.x + 170.0f, layout.drawerTitle.y + 12.0f, 10.0f,
               inspection.SolverDetail().contactPresentation.truncated ? palette.warningAccent.r : palette.accent.r,
               inspection.SolverDetail().contactPresentation.truncated ? palette.warningAccent.g : palette.accent.g,
               inspection.SolverDetail().contactPresentation.truncated ? palette.warningAccent.b : palette.accent.b,
               frameLabel );

    // Sign/units are rendered as part of the surface so a captured frame remains
    // interpretable without consulting solver implementation comments.
    static constexpr const char* TAB_LABELS[] = { "SUMMARY", "RAW RECORD", "ITERATIONS" };

    for ( int tabIndex = 0; tabIndex < 3; ++tabIndex )
    {
        const UI::UIRect& tab = layout.tabs[static_cast<std::size_t>( tabIndex )];
        const bool selected = tabIndex == static_cast<int>( inspection.Display().activeTab );
        const UI::Style::UIColor& tabFill = selected ? CAUSE_SELECTED : CAUSE_NAVY_ALT;
        const UI::Style::UIColor& tabAccent = tabIndex == 0 ? CAUSE_PREDICTION
                                                            : ( tabIndex == 1 ? CAUSE_MANIFOLD : CAUSE_SOLVER );
        draw.RoundedRect( tab.x, tab.y, tab.w, tab.h, 4.0f, tabFill.r, tabFill.g, tabFill.b, 1.0f );
        draw.Rect( tab.x, tab.y + tab.h - 2.0f, tab.w, 2.0f, tabAccent.r, tabAccent.g, tabAccent.b,
                   selected ? 1.0f : 0.34f );
        draw.Text( tab.x + 10.0f, tab.y + 7.0f, 10.0f, selected ? tabAccent.r : palette.textSecondary.r,
                   selected ? tabAccent.g : palette.textSecondary.g, selected ? tabAccent.b : palette.textSecondary.b,
                   TAB_LABELS[tabIndex] );
    }

    draw.Text( layout.drawer.x + 12.0f, layout.drawer.y + 42.0f, 9.0f, palette.textSecondary.r, palette.textSecondary.g,
               palette.textSecondary.b, "UNITS  v=u/s  w=rad/s  impulse=mass*u/s  mass=mass  depth=u" );
    draw.Text( layout.drawer.x + 12.0f, layout.drawer.y + 55.0f, 9.0f, palette.textMuted.r, palette.textMuted.g,
               palette.textMuted.b, "SIGNS  +penetration=overlap  normal A->B  CLAMP=friction limit" );

    if ( inspection.SolverDetail().solverDetailAvailability != ReplayCauseSolverDetailAvailability::Available ||
         inspection.SolverDetail().solverDetailContacts.empty() )
    {
        draw.RoundedRect( layout.content.x, layout.content.y, layout.content.w, layout.content.h, 6.0f, CAUSE_NAVY_ALT.r,
                          CAUSE_NAVY_ALT.g, CAUSE_NAVY_ALT.b, 1.0f );
        draw.Text( layout.content.x + 10.0f, layout.content.y + 15.0f, 11.5f, palette.textSecondary.r,
                   palette.textSecondary.g, palette.textSecondary.b, inspection.SolverDetail().solverDetailFeedback );
        drawList.PopClip();
        return;
    }

    switch ( inspection.Display().activeTab )
    {
    case ReplayCauseInspectorTab::Summary:
        RenderReplayCauseSummaryTab( draw, inspection, layout, palette );
        break;
    case ReplayCauseInspectorTab::RawRecord:
        RenderReplayCauseRawRecordTab( drawList, draw, inspection, layout, palette );
        break;
    case ReplayCauseInspectorTab::Iterations:
        RenderReplayCauseIterationsTab( drawList, draw, inspection, layout, palette );
        break;
    }

    drawList.PopClip();
}

void RenderReplayCauseInspectorToggle( const UI::UIDrawContext& draw, const ReplayCauseInspectorLayout& layout,
                                       const ReplayCauseInspectionView& inspection, const RunReplayCauseTreeState& tree )
{
    const bool hovered = !tree.pointerBlocked && layout.drawerToggle.Contains( tree.mouseX, tree.mouseY );
    const float railAlpha = hovered ? 0.58f : 0.38f;
    draw.Rect( layout.sharedSeam.x, layout.sharedSeam.y, layout.sharedSeam.w, layout.sharedSeam.h, CAUSE_RULE.r,
               CAUSE_RULE.g, CAUSE_RULE.b, railAlpha );

    UI::Style::UIColor handleFill = hovered ? CAUSE_SELECTED : CAUSE_NAVY_ALT;
    handleFill.a = hovered ? 0.98f : 0.92f;
    UI::Style::UIColor handleBorder = CAUSE_RULE;
    handleBorder.a = hovered ? 0.82f : 0.58f;
    draw.RoundedPanel( layout.drawerToggle, 4.0f, handleFill, handleBorder );

    const char* arrow = inspection.Display().drawerOpen ? ">" : "<";
    draw.Text( layout.drawerToggle.x + 5.0f, layout.drawerToggle.y + 11.0f, 14.0f, CAUSE_RULE.r, CAUSE_RULE.g, CAUSE_RULE.b,
               arrow );
}

// Concept: one frame-local scrubber composer owns the values shared across the
// header, track, and prediction rows. Phase methods consume that state directly
// instead of repeatedly unpacking the root replay snapshot into wide calls.
class ReplayScrubberComposer
{
  public:
    ReplayScrubberComposer( UI::UIDrawList& drawList, ReplayScrubberPresentationView presentation, bool scenePhysicsEnabled,
                            ReplayOverlayGestureView gesture, ReplayOverlayViewport viewport, double nowSeconds );
    void Compose();

  private:
    void BuildSurface( bool scenePhysicsEnabled );
    const ReplayOverlayControl& Control( ReplayScrubberControl id );
    bool IsHot( ReplayScrubberControl id ) const;
    float FadeA( float alpha ) const;
    void DrawText( float x, float y, float size, const UI::Style::UIColor& color, const char* text ) const;
    void BuildTimeLabel();
    void DrawHeader();
    void DrawEditControls();
    void DrawReplayRow();
    void DrawPredictionToggleAndHorizon();
    void DrawRagdollAndPathControls();
    void DrawCheckControl( const UI::UIRect& bounds, ReplayScrubberControl control, bool enabled, bool checked,
                           const char* label );
    void DrawPredictionStatus();

    ReplayScrubberPresentationView m_presentation;
    const ReplayScrubberView& m_scrubber;
    ReplayOverlayGestureView m_gesture;
    ReplayOverlayViewport m_viewport;
    double m_nowSeconds = 0.0;
    bool m_loadedPresentation = false;
    bool m_solverReplayEnabled = false;
    bool m_solverToolsEnabled = false;
    bool m_predictionToolsEnabled = false;
    bool m_scenePhysicsEnabled = false;
    RunReplayTrack m_activeTrack = RunReplayTrack::Solver;
    float m_solverPresentT = 1.0f;
    UI::UIDrawContext m_draw;
    const UI::Style::UIPalette& m_palette;
    const UI::Style::UIRadii& m_radii;
    ReplayScrubberSurface m_surface;
    float m_trackPosition = 0.0f;
    float m_fade = 0.0f;
    bool m_futureTimelineVisible = false;
    bool m_live = false;
    char m_timeLabel[48] = {};
};
} // namespace

ReplayScrubberComposer::ReplayScrubberComposer( UI::UIDrawList& drawList, ReplayScrubberPresentationView presentation,
                                                bool scenePhysicsEnabled, ReplayOverlayGestureView gesture,
                                                ReplayOverlayViewport viewport, double nowSeconds )
    : m_presentation( presentation ), m_scrubber( m_presentation.scrubber ), m_gesture( gesture ), m_viewport( viewport ),
      m_nowSeconds( nowSeconds ), m_loadedPresentation( m_presentation.selection.loadedPresentation ),
      m_solverReplayEnabled( m_presentation.solverStats.enabled ),
      m_solverToolsEnabled( m_solverReplayEnabled && m_presentation.solverStats.sampleCount >= 2 ),
      m_predictionToolsEnabled( m_solverReplayEnabled && scenePhysicsEnabled ), m_scenePhysicsEnabled( scenePhysicsEnabled ),
      m_activeTrack( m_loadedPresentation ? RunReplayTrack::Presentation : RunReplayTrack::Solver ),
      m_solverPresentT( m_loadedPresentation ? 1.0f : m_presentation.selection.solverPresentTrackPosition ),
      m_draw( viewport.width, viewport.height, drawList ), m_palette( UI::Style::Palette() ), m_radii( UI::Style::Radii() )
{
}


void ReplayScrubberComposer::Compose()
{
    if ( !m_presentation.shouldRender || m_viewport.width <= 0 || m_viewport.height <= 0 ||
         ( !m_loadedPresentation && !m_solverReplayEnabled ) )
    {
        return;
    }

    BuildSurface( m_scenePhysicsEnabled );
    m_trackPosition = std::clamp( ReplayOverlayTrackPosition( m_scrubber, m_activeTrack ), 0.0f, 1.0f );
    m_futureTimelineVisible = !m_loadedPresentation && ReplayTimelineHasFuture( m_solverPresentT );
    m_fade = std::clamp( m_scrubber.visibleAlpha, 0.0f, 1.0f );

    if ( m_fade <= REPLAY_SCRUBBER_FADE_EPSILON )
    {
        return;
    }

    m_live = !m_loadedPresentation && ReplayAtPresentTrackPosition( m_trackPosition, m_solverPresentT ) &&
             !m_scrubber.historicalSamplePaused;
    BuildTimeLabel();
    DrawHeader();
    DrawReplayRow();

    if ( !m_loadedPresentation )
    {
        DrawPredictionToggleAndHorizon();
        DrawRagdollAndPathControls();
        DrawPredictionStatus();
    }
}


void ReplayScrubberComposer::BuildSurface( bool scenePhysicsEnabled )
{
    const ReplayScrubberSourceAvailability sources {
        m_loadedPresentation,
        m_presentation.pathVisualizer.hasTarget,
        m_presentation.predictionTimelineAvailable,
        m_presentation.selection.currentPresentation != nullptr,
        m_presentation.selection.currentSolver != nullptr,
        scenePhysicsEnabled,
    };
    ReplayScrubberSurfaceInput input = DescribeReplayScrubberAvailability( m_scrubber, m_presentation.solverStats, sources );
    input.screenW = m_viewport.width;
    input.screenH = m_viewport.height;
    input.gesture = m_gesture.scrubDrag ? ReplayToolGestureKind::ScrubDrag
                                        : ( m_gesture.predictionHorizonDrag ? ReplayToolGestureKind::PredictionHorizonDrag
                                                                            : ReplayToolGestureKind::None );
    input.predictionEnabled = m_presentation.predictionControls.enabled;
    input.predictionHighDetail = m_presentation.predictionDiagnostics.detailMode == ReplayPredictionDetailMode::High;
    BuildReplayScrubberSurface( input, m_surface );
    m_surface.ResolvePointer( m_scrubber.mouseX, m_scrubber.mouseY );
}


const ReplayOverlayControl& ReplayScrubberComposer::Control( ReplayScrubberControl id )
{
    const ReplayOverlayControl* row = m_surface.Find( ReplayScrubberControlId( id ) );

    if ( !row )
    {
        SB_FATAL( "ReplayScrubberSurface", "Render snapshot is missing replay scrubber control id=%u.",
                  static_cast<uint32_t>( id ) );
    }

    return *row;
}


bool ReplayScrubberComposer::IsHot( ReplayScrubberControl id ) const
{
    return m_surface.hasHotControl && m_surface.hotControl == ReplayScrubberControlId( id );
}


float ReplayScrubberComposer::FadeA( float alpha ) const
{
    return alpha * m_fade;
}


void ReplayScrubberComposer::DrawText( float x, float y, float size, const UI::Style::UIColor& color,
                                       const char* text ) const
{
    m_draw.Text( x, y, size, color.r * m_fade, color.g * m_fade, color.b * m_fade, text );
}


void ReplayScrubberComposer::BuildTimeLabel()
{
    const ReplayPresentationSample* selectedPresentation = m_presentation.selection.selectedPresentation;
    const ReplayPresentationSample* latestPresentation = m_presentation.selection.latestPresentation;
    const ReplaySolverFrameSample* selectedSolver = m_presentation.selection.selectedSolver;
    const ReplaySolverFrameSample* latestSolver = m_presentation.selection.latestSolver;
    const double selectedPresentationSeconds = selectedPresentation ? selectedPresentation->simulationSeconds : 0.0;
    const double latestPresentationSeconds = latestPresentation ? latestPresentation->simulationSeconds : 0.0;
    const double selectedSolverSeconds = selectedSolver ? selectedSolver->simulationSeconds : 0.0;
    const double latestSolverSeconds = latestSolver ? latestSolver->simulationSeconds : 0.0;
    double secondsBack = 0.0;

    if ( m_loadedPresentation && latestPresentationSeconds >= selectedPresentationSeconds )
    {
        secondsBack = latestPresentationSeconds - selectedPresentationSeconds;
    }
    else if ( latestSolverSeconds >= selectedSolverSeconds )
    {
        secondsBack = latestSolverSeconds - selectedSolverSeconds;
    }

    if ( m_loadedPresentation && ReplayAtPresentTrackPosition( m_trackPosition, 1.0f ) )
    {
        sprintf_s( m_timeLabel, sizeof( m_timeLabel ), "END" );
    }
    else if ( m_presentation.selectedPrediction )
    {
        const double futureSeconds = static_cast<double>( m_presentation.selectedPrediction->frameIndex ) *
                                     static_cast<double>( PHYSICS_FIXED_DT );
        sprintf_s( m_timeLabel, sizeof( m_timeLabel ), "+%.1fs", futureSeconds );
    }
    else if ( m_live )
    {
        sprintf_s( m_timeLabel, sizeof( m_timeLabel ), "LIVE" );
    }
    else
    {
        sprintf_s( m_timeLabel, sizeof( m_timeLabel ), "-%.1fs", secondsBack );
    }
}


void ReplayScrubberComposer::DrawHeader()
{
    const UI::UIRect panel = Control( ReplayScrubberControl::Panel ).drawRect;
    const bool branchEnabled = m_scrubber.historicalSamplePaused &&
                               ( ( m_loadedPresentation && m_presentation.selection.currentPresentation ) ||
                                 ( !m_loadedPresentation && m_solverToolsEnabled &&
                                   m_presentation.selection.currentSolver ) );
    UI::Style::UIColor panelFill = m_palette.windowSubtle;
    panelFill.a = FadeA( 0.92f );
    UI::Style::UIColor panelBorder = m_palette.innerBorder;
    panelBorder.a = FadeA( 0.42f );
    m_draw.RoundedPanel( panel, m_radii.control, panelFill, panelBorder );
    DrawText( panel.x + 16.0f, panel.y + 19.0f, 10.5f, m_palette.textSecondary,
              m_loadedPresentation ? "V2 FILE" : "SOLVER" );

    const float labelWidth = UI::UIFontMetrics::MeasureText( 11.0f, m_timeLabel );
    DrawText( panel.x + panel.w - labelWidth - 16.0f, panel.y + 18.0f, 11.0f,
              m_live ? m_palette.accent : m_palette.warningAccent, m_timeLabel );

    const UI::UIRect branchButton = Control( ReplayScrubberControl::Branch ).drawRect;
    const bool branchHover = branchEnabled && IsHot( ReplayScrubberControl::Branch );
    const UI::Style::UIColor& branchFill = branchHover ? m_palette.controlHover : m_palette.control;
    m_draw.RoundedRect( branchButton.x, branchButton.y, branchButton.w, branchButton.h, m_radii.smallButton, branchFill.r,
                        branchFill.g, branchFill.b, FadeA( branchEnabled ? 0.94f : 0.42f ) );
    m_draw.Outline( branchButton.x, branchButton.y, branchButton.w, branchButton.h, m_palette.accent.r, m_palette.accent.g,
                    m_palette.accent.b, FadeA( branchEnabled ? ( branchHover ? 0.84f : 0.42f ) : 0.18f ) );
    DrawText( branchButton.x + 12.0f, branchButton.y + 4.5f, 9.5f,
              branchEnabled ? m_palette.textPrimary : m_palette.textMuted, "BRANCH" );

    if ( !m_loadedPresentation )
    {
        DrawEditControls();
    }
}


void ReplayScrubberComposer::DrawEditControls()
{
    const UI::UIRect highDetail = Control( ReplayScrubberControl::HighDetail ).drawRect;
    const bool detailEnabled = Control( ReplayScrubberControl::HighDetail ).enabled;
    const bool detailChecked = Control( ReplayScrubberControl::HighDetail ).checked;
    const bool detailHover = detailEnabled && IsHot( ReplayScrubberControl::HighDetail );
    const UI::Style::UIColor& detailFill = detailHover ? m_palette.controlHover : m_palette.control;
    m_draw.RoundedRect( highDetail.x, highDetail.y, highDetail.w, highDetail.h, m_radii.smallButton, detailFill.r,
                        detailFill.g, detailFill.b,
                        FadeA( detailEnabled ? ( detailHover || detailChecked ? 0.94f : 0.78f ) : 0.38f ) );
    m_draw.Outline( highDetail.x, highDetail.y, highDetail.w, highDetail.h, m_palette.accent.r, m_palette.accent.g,
                    m_palette.accent.b, FadeA( detailEnabled ? ( detailHover || detailChecked ? 0.78f : 0.36f ) : 0.14f ) );
    const float detailCheckX = highDetail.x + 6.0f;
    const float detailCheckY = highDetail.y + 5.0f;
    m_draw.Outline( detailCheckX, detailCheckY, 10.0f, 10.0f, m_palette.accent.r, m_palette.accent.g, m_palette.accent.b,
                    FadeA( detailEnabled ? 0.82f : 0.28f ) );

    if ( detailChecked )
    {
        m_draw.Rect( detailCheckX + 2.0f, detailCheckY + 2.0f, 6.0f, 6.0f, m_palette.accent.r, m_palette.accent.g,
                     m_palette.accent.b, FadeA( 0.95f ) );
    }

    const UI::Style::UIColor& detailText = !detailEnabled ? m_palette.textMuted
                                                          : ( detailChecked ? m_palette.accent : m_palette.textSecondary );
    DrawText( highDetail.x + 20.0f, highDetail.y + 2.0f, 6.5f, detailText, "HIGH" );
    DrawText( highDetail.x + 20.0f, highDetail.y + 10.0f, 6.5f, detailText, "DETAIL" );

    PROFILE_SCOPED( "Frame/Replay/ScrubberOverlay/VelocityEditControls" );
    const UI::UIRect velocityEdit = Control( ReplayScrubberControl::VelocityEdit ).drawRect;
    const bool velocityEnabled = m_solverToolsEnabled && m_presentation.velocityEdit.enabled;
    const bool velocityHover = m_solverToolsEnabled && IsHot( ReplayScrubberControl::VelocityEdit );
    const UI::Style::UIColor& velocityFill = velocityHover ? m_palette.controlHover : m_palette.control;
    m_draw.RoundedRect( velocityEdit.x, velocityEdit.y, velocityEdit.w, velocityEdit.h, m_radii.smallButton, velocityFill.r,
                        velocityFill.g, velocityFill.b,
                        FadeA( m_solverToolsEnabled ? ( velocityHover || velocityEnabled ? 0.94f : 0.78f ) : 0.38f ) );
    m_draw.Outline( velocityEdit.x, velocityEdit.y, velocityEdit.w, velocityEdit.h, m_palette.warningAccent.r,
                    m_palette.warningAccent.g, m_palette.warningAccent.b,
                    FadeA( m_solverToolsEnabled ? ( velocityHover || velocityEnabled ? 0.78f : 0.34f ) : 0.14f ) );
    const float velocityCheckX = velocityEdit.x + 7.0f;
    const float velocityCheckY = velocityEdit.y + 5.0f;
    m_draw.Outline( velocityCheckX, velocityCheckY, 10.0f, 10.0f, m_palette.warningAccent.r, m_palette.warningAccent.g,
                    m_palette.warningAccent.b, FadeA( m_solverToolsEnabled ? 0.82f : 0.28f ) );

    if ( velocityEnabled )
    {
        m_draw.Rect( velocityCheckX + 2.0f, velocityCheckY + 2.0f, 6.0f, 6.0f, m_palette.warningAccent.r,
                     m_palette.warningAccent.g, m_palette.warningAccent.b, FadeA( 0.95f ) );
    }

    const UI::Style::UIColor& velocityText = !m_solverToolsEnabled
                                                 ? m_palette.textMuted
                                                 : ( velocityEnabled ? m_palette.warningAccent : m_palette.textSecondary );
    DrawText( velocityEdit.x + 23.0f, velocityEdit.y + 4.5f, 9.5f, velocityText, "ALT VEL" );
}


void ReplayScrubberComposer::DrawReplayRow()
{
    const UI::UIRect track = Control( ReplayScrubberControl::ScrubTrack ).drawRect;
    const UI::UIRect saveButton = Control( ReplayScrubberControl::Save ).drawRect;
    const UI::UIRect loadButton = Control( ReplayScrubberControl::Load ).drawRect;
    const float fillWidth = (std::max)( REPLAY_SCRUBBER_TRACK_HEIGHT, track.w * m_trackPosition );
    const float knobX = track.x + track.w * m_trackPosition;
    // This composer draws only the active track; the retired shared row helper
    // carried an inactive branch for a second row that was never submitted.
    constexpr bool inactive = false;
    const bool saveEnabled = !m_loadedPresentation && m_solverToolsEnabled;
    const bool saveHover = saveEnabled && IsHot( ReplayScrubberControl::Save );
    const bool saveFeedback = m_scrubber.saveMessage[0] != '\0' && m_scrubber.saveMessageUntil >= m_nowSeconds;
    const bool saveFailed = saveFeedback && strstr( m_scrubber.saveMessage, "FAILED" );
    const bool loadHover = IsHot( ReplayScrubberControl::Load );
    const float saveR = saveFeedback ? ( saveFailed ? 0.48f : m_palette.accent.r )
                                     : ( saveHover ? m_palette.controlHover.r : m_palette.control.r );
    const float saveG = saveFeedback ? ( saveFailed ? 0.12f : m_palette.accent.g )
                                     : ( saveHover ? m_palette.controlHover.g : m_palette.control.g );
    const float saveB = saveFeedback ? ( saveFailed ? 0.12f : m_palette.accent.b )
                                     : ( saveHover ? m_palette.controlHover.b : m_palette.control.b );
    const float back = inactive ? 0.11f : 0.16f;
    m_draw.RoundedRect( track.x, track.y, track.w, track.h, track.h * 0.5f, back, back + 0.02f, back + 0.05f,
                        FadeA( inactive ? 0.74f : 0.92f ) );
    m_draw.RoundedRect( track.x, track.y, fillWidth, track.h, track.h * 0.5f, inactive ? 0.30f : m_palette.accent.r,
                        inactive ? 0.33f : m_palette.accent.g, inactive ? 0.36f : m_palette.accent.b,
                        FadeA( inactive ? 0.40f : ( m_live ? 0.64f : 0.94f ) ) );

    if ( m_futureTimelineVisible )
    {
        const float presentX = track.x + track.w * m_solverPresentT;
        m_draw.Rect( presentX, track.y, (std::max)( 0.0f, track.x + track.w - presentX ), track.h, 0.08f, 0.30f, 0.92f,
                     FadeA( inactive ? 0.40f : 0.72f ) );
    }

    m_draw.RoundedRect( knobX - 6.0f, track.y - 5.0f, 12.0f, 18.0f, 5.0f, 0.98f, 0.98f, 1.0f, FadeA( 0.98f ) );
    m_draw.Outline( knobX - 6.0f, track.y - 5.0f, 12.0f, 18.0f, m_palette.accentStrong.r, m_palette.accentStrong.g,
                    m_palette.accentStrong.b, FadeA( 0.72f ) );

    if ( m_futureTimelineVisible )
    {
        const float presentX = track.x + track.w * m_solverPresentT;
        m_draw.Rect( presentX - 1.0f, track.y - 6.0f, 2.0f, track.h + 12.0f, 0.92f, 1.0f, 0.84f, FadeA( 0.86f ) );
        m_draw.Rect( presentX - 4.0f, track.y - 8.0f, 8.0f, 2.0f, 0.92f, 1.0f, 0.84f, FadeA( 0.70f ) );
        m_draw.Rect( presentX - 4.0f, track.y + track.h + 6.0f, 8.0f, 2.0f, 0.92f, 1.0f, 0.84f, FadeA( 0.70f ) );
    }

    m_draw.RoundedRect( saveButton.x, saveButton.y, saveButton.w, saveButton.h, 4.0f, saveR, saveG, saveB,
                        FadeA( saveEnabled ? 0.96f : 0.34f ) );
    m_draw.Outline( saveButton.x, saveButton.y, saveButton.w, saveButton.h, m_palette.accentStrong.r,
                    m_palette.accentStrong.g, m_palette.accentStrong.b,
                    FadeA( saveEnabled ? ( saveHover || saveFeedback ? 0.74f : 0.36f ) : 0.16f ) );
    const float iconX = saveButton.x + 6.0f;
    const float iconY = saveButton.y + 5.0f;
    const float iconAlpha = FadeA( saveEnabled ? 0.96f : 0.34f );
    m_draw.Outline( iconX, iconY, 10.0f, 12.0f, 0.88f, 0.97f, 1.0f, iconAlpha );
    m_draw.Rect( iconX + 2.0f, iconY + 2.0f, 6.0f, 3.0f, 0.88f, 0.97f, 1.0f, iconAlpha * 0.73f );
    m_draw.Rect( iconX + 3.0f, iconY + 8.0f, 4.0f, 3.0f, 0.88f, 0.97f, 1.0f, iconAlpha * 0.85f );
    const UI::Style::UIColor& loadFill = loadHover ? m_palette.controlHover : m_palette.control;
    m_draw.RoundedRect( loadButton.x, loadButton.y, loadButton.w, loadButton.h, m_radii.smallButton, loadFill.r, loadFill.g,
                        loadFill.b, FadeA( 0.92f ) );
    m_draw.Outline( loadButton.x, loadButton.y, loadButton.w, loadButton.h, m_palette.accentStrong.r,
                    m_palette.accentStrong.g, m_palette.accentStrong.b, FadeA( loadHover ? 0.72f : 0.34f ) );
    DrawText( loadButton.x + 9.0f, loadButton.y + 5.0f, 9.5f, m_palette.textPrimary, "LOAD" );
}


void ReplayScrubberComposer::DrawPredictionToggleAndHorizon()
{
    const UI::UIRect toggle = Control( ReplayScrubberControl::PredictionToggle ).drawRect;
    const UI::UIRect panel = Control( ReplayScrubberControl::PredictionPanel ).drawRect;
    const UI::UIRect horizon = Control( ReplayScrubberControl::PredictionHorizon ).drawRect;
    const bool hover = m_predictionToolsEnabled &&
                       ( IsHot( ReplayScrubberControl::PredictionHorizon ) || m_gesture.predictionHorizonDrag );
    const bool enabled = m_predictionToolsEnabled && m_presentation.predictionControls.enabled;
    const UI::Style::UIColor& toggleFill = m_predictionToolsEnabled && IsHot( ReplayScrubberControl::PredictionToggle )
                                               ? m_palette.controlHover
                                               : m_palette.control;
    m_draw.RoundedRect( toggle.x, toggle.y, toggle.w, toggle.h, m_radii.smallButton, toggleFill.r, toggleFill.g,
                        toggleFill.b, FadeA( m_predictionToolsEnabled ? 0.88f : 0.38f ) );
    m_draw.Outline( toggle.x, toggle.y, toggle.w, toggle.h, m_palette.accent.r, m_palette.accent.g, m_palette.accent.b,
                    FadeA( m_predictionToolsEnabled
                               ? ( IsHot( ReplayScrubberControl::PredictionToggle ) || enabled ? 0.72f : 0.34f )
                               : 0.14f ) );
    const float checkX = toggle.x + 7.0f;
    const float checkY = toggle.y + 5.0f;
    m_draw.Outline( checkX, checkY, 10.0f, 10.0f, m_palette.accent.r, m_palette.accent.g, m_palette.accent.b,
                    FadeA( m_predictionToolsEnabled ? 0.82f : 0.28f ) );

    if ( enabled )
    {
        m_draw.Rect( checkX + 2.0f, checkY + 2.0f, 6.0f, 6.0f, m_palette.accentStrong.r, m_palette.accentStrong.g,
                     m_palette.accentStrong.b, FadeA( 0.95f ) );
    }

    DrawText( toggle.x + 23.0f, toggle.y + 4.5f, 9.5f,
              !m_predictionToolsEnabled ? m_palette.textMuted
                                        : ( enabled ? m_palette.accentStrong : m_palette.textSecondary ),
              "PREDICT" );
    const UI::Style::UIColor& panelFill = hover ? m_palette.controlHover : m_palette.control;
    m_draw.RoundedRect( panel.x, panel.y, panel.w, panel.h, m_radii.smallButton, panelFill.r, panelFill.g, panelFill.b,
                        FadeA( m_predictionToolsEnabled ? 0.88f : 0.38f ) );
    m_draw.Outline( panel.x, panel.y, panel.w, panel.h, m_palette.accent.r, m_palette.accent.g, m_palette.accent.b,
                    FadeA( m_predictionToolsEnabled ? ( hover || enabled ? 0.72f : 0.34f ) : 0.14f ) );

    const float seconds = std::clamp( m_presentation.predictionControls.horizonSeconds, REPLAY_PREDICTION_MIN_SECONDS,
                                      REPLAY_PREDICTION_MAX_SECONDS );
    char secondsLabel[16] = {};
    sprintf_s( secondsLabel, sizeof( secondsLabel ), "%.0fs", static_cast<double>( seconds ) );
    const float horizonT = ReplayPredictionHorizonT( seconds );
    const float fillWidth = (std::max)( 4.0f, horizon.w * horizonT );
    const float knobX = horizon.x + horizon.w * horizonT;
    m_draw.RoundedRect( horizon.x, horizon.y, horizon.w, horizon.h, 4.0f, 0.10f, 0.14f, 0.15f,
                        FadeA( m_predictionToolsEnabled ? 0.86f : 0.34f ) );
    m_draw.RoundedRect( horizon.x, horizon.y, fillWidth, horizon.h, 4.0f, 0.34f, 0.95f, 0.62f,
                        FadeA( m_predictionToolsEnabled ? ( enabled ? 0.86f : 0.48f ) : 0.20f ) );
    m_draw.RoundedRect( knobX - 4.0f, horizon.y - 3.0f, 8.0f, 14.0f, 3.0f, enabled ? 0.88f : 0.56f, enabled ? 1.0f : 0.62f,
                        enabled ? 0.82f : 0.64f, FadeA( m_predictionToolsEnabled ? ( hover ? 0.98f : 0.86f ) : 0.34f ) );
    DrawText( horizon.x + horizon.w + 8.0f, panel.y + 4.5f, 8.5f,
              !m_predictionToolsEnabled ? m_palette.textMuted
                                        : ( enabled ? m_palette.accentStrong : m_palette.textSecondary ),
              secondsLabel );
}


void ReplayScrubberComposer::DrawRagdollAndPathControls()
{
    const bool ragdollEnabled = m_predictionToolsEnabled && m_presentation.predictionTopology.ragdollVisualsEnabled;
    const bool pathToolsEnabled = m_solverToolsEnabled && m_presentation.pathVisualizer.hasTarget;
    const bool pathEnabled = pathToolsEnabled && m_presentation.pathVisualizer.pastPathVisible;
    DrawCheckControl( Control( ReplayScrubberControl::RagdollVisuals ).drawRect, ReplayScrubberControl::RagdollVisuals,
                      m_predictionToolsEnabled, ragdollEnabled, "RAGDOLL" );
    DrawCheckControl( Control( ReplayScrubberControl::PastPath ).drawRect, ReplayScrubberControl::PastPath, pathToolsEnabled,
                      pathEnabled, "PAST" );
}


void ReplayScrubberComposer::DrawCheckControl( const UI::UIRect& bounds, ReplayScrubberControl control, bool enabled,
                                               bool checked, const char* label )
{
    const bool hot = enabled && IsHot( control );
    const UI::Style::UIColor& fill = hot ? m_palette.controlHover : m_palette.control;
    m_draw.RoundedRect( bounds.x, bounds.y, bounds.w, bounds.h, m_radii.smallButton, fill.r, fill.g, fill.b,
                        FadeA( enabled ? 0.88f : 0.38f ) );
    m_draw.Outline( bounds.x, bounds.y, bounds.w, bounds.h, m_palette.accent.r, m_palette.accent.g, m_palette.accent.b,
                    FadeA( enabled ? ( hot || checked ? 0.72f : 0.32f ) : 0.14f ) );
    const float checkX = bounds.x + 7.0f;
    const float checkY = bounds.y + 5.0f;
    m_draw.Outline( checkX, checkY, 10.0f, 10.0f, m_palette.accent.r, m_palette.accent.g, m_palette.accent.b,
                    FadeA( enabled ? 0.82f : 0.28f ) );

    if ( checked )
    {
        m_draw.Rect( checkX + 2.0f, checkY + 2.0f, 6.0f, 6.0f, m_palette.accentStrong.r, m_palette.accentStrong.g,
                     m_palette.accentStrong.b, FadeA( 0.95f ) );
    }

    DrawText( bounds.x + 23.0f, bounds.y + 4.5f, 9.0f,
              !enabled ? m_palette.textMuted : ( checked ? m_palette.accentStrong : m_palette.textSecondary ), label );
}


void ReplayScrubberComposer::DrawPredictionStatus()
{
    const UI::UIRect panel = Control( ReplayScrubberControl::PredictionPanel ).drawRect;
    const bool enabled = m_predictionToolsEnabled && m_presentation.predictionControls.enabled;
    const char* colorMode = ReplayPathColorModeName( m_presentation.pathVisualizer.colorMode );

    if ( enabled )
    {
        const ReplayPredictionDiagnosticsView& diagnostics = m_presentation.predictionDiagnostics;
        const char* buildMode = diagnostics.buildMode == ReplayPredictionBuildMode::Instant     ? "Instant"
                                : diagnostics.buildMode == ReplayPredictionBuildMode::Amortized ? "Amortized"
                                                                                                : "Measuring";
        char scheduling[128] = {};
        sprintf_s( scheduling, sizeof( scheduling ), "Prediction: %s | Color: %s | %.0f ticks/ms | %.1f ms rebuild",
                   buildMode, colorMode, diagnostics.measuredTicksPerMs, diagnostics.lastBuildWallMs );
        DrawText( panel.x, panel.y + 27.0f, 8.0f, m_palette.textSecondary, scheduling );
    }

    char colorLabel[64] = {};
    sprintf_s( colorLabel, sizeof( colorLabel ), "COLOR [,]: %s", colorMode );
    DrawText( panel.x, panel.y + 38.0f, 8.0f, m_predictionToolsEnabled ? m_palette.accentStrong : m_palette.textMuted,
              colorLabel );

    if ( ReplayPredictionContactsIncomplete( m_presentation.predictionTimeline ) )
    {
        DrawText( panel.x, panel.y + 49.0f, 8.0f, m_palette.warningAccent, "CONTACTS PARTIAL" );
    }
}

static void DrawReplayGateStroke( UI::UIDrawList& drawList, UI::UIPoint start, UI::UIPoint end,
                                  const UI::Style::UIColor& color, float width )
{
    const float dx = end.x - start.x;
    const float dy = end.y - start.y;
    const float length = std::sqrt( dx * dx + dy * dy );

    if ( !std::isfinite( length ) || length <= 0.001f )
    {
        return;
    }

    const float scale = width * 0.5f / length;
    const UI::UIPoint offset { -dy * scale, dx * scale };
    const UI::UIPoint a { start.x + offset.x, start.y + offset.y };
    const UI::UIPoint b { end.x + offset.x, end.y + offset.y };
    const UI::UIPoint c { end.x - offset.x, end.y - offset.y };
    const UI::UIPoint d { start.x - offset.x, start.y - offset.y };
    drawList.AddTriangle( { a, b, c }, color );
    drawList.AddTriangle( { a, c, d }, color );
}

static void DrawReplayPositionGate( UI::UIDrawList& drawList, const ReplayPositionGate& gate )
{
    if ( !gate.visible )
    {
        return;
    }

    const auto point = [&]( float along, float across ) -> UI::UIPoint
    {
        return { gate.center.x + gate.tangent.x * along - gate.tangent.y * across,
                 gate.center.y + gate.tangent.y * along + gate.tangent.x * across };
    };
    const std::array<UI::UIPoint, 4> diamond = { point( -9.0f, 0.0f ), point( 0.0f, -7.0f ), point( 9.0f, 0.0f ),
                                                 point( 0.0f, 7.0f ) };

    // Why: a thin dark backing keeps the crisp cue readable over bright bodies
    // and pale paths without bloom. It is drawn before panels so UI still wins.
    for ( int pass = 0; pass < 2; ++pass )
    {
        const float width = pass == 0 ? 4.0f : 2.0f;
        const UI::Style::UIColor cyan = pass == 0 ? UI::Style::UIColor { 0.015f, 0.06f, 0.075f, 0.85f }
                                                  : UI::Style::UIColor { 0.2f, 0.95f, 1.0f, 1.0f };
        const UI::Style::UIColor white = pass == 0 ? cyan : UI::Style::UIColor { 0.96f, 1.0f, 1.0f, 1.0f };

        for ( std::size_t edge = 0; edge < diamond.size(); ++edge )
        {
            DrawReplayGateStroke( drawList, diamond[edge], diamond[( edge + 1 ) % diamond.size()], cyan, width );
        }

        for ( float along : { -16.0f, 16.0f } )
        {
            DrawReplayGateStroke( drawList, point( along, -5.0f ), point( along, 5.0f ), white, width );
        }
    }
}

static void ComposeReplayPositionGates( UI::UIDrawList& drawList, const ReplayOverlayTimelineView& timeline,
                                        const ReplayOverlayCausalityView& causality, const ReplayOverlayViewport& viewport )
{
    if ( !timeline.prediction.controls.enabled || timeline.prediction.timeline.frames.empty() )
    {
        return;
    }

    // Invariant: use the same immutable sample as the rendered bodies, never the
    // collision's original frame or a nearest spatial match on a looping path.
    const RunReplayPredictionFrame* frame = timeline.selectedPrediction;

    if ( !frame )
    {
        // A live or historical pose need not match the prediction's source
        // frame. Hide the gate until an actual future sample is presented.
        return;
    }

    drawList.PushClip( { 0.0f, 0.0f, static_cast<float>( viewport.width ), static_cast<float>( viewport.height ) } );

    for ( Physics::PhysicsSceneObjectId id : ReplayPositionGateSelection( causality, timeline.pathVisualizer.targetId ) )
    {
        DrawReplayPositionGate( drawList, BuildReplayPositionGate( *frame, id, viewport ) );
    }

    drawList.PopClip();
}

static void DrawReplayContactStroke( UI::UIDrawList& drawList, UI::UIPoint start, UI::UIPoint end, UI::Style::UIColor color,
                                     float flash )
{
    color.r += ( 1.0f - color.r ) * flash;
    color.g += ( 1.0f - color.g ) * flash;
    color.b += ( 1.0f - color.b ) * flash;
    const float width = 2.5f + 1.5f * flash;
    DrawReplayGateStroke( drawList, start, end, { 0.02f, 0.035f, 0.045f, 0.9f }, width + 2.0f );
    DrawReplayGateStroke( drawList, start, end, color, width );
}

static void DrawReplayContactAxis( UI::UIDrawList& drawList, const ReplayOverlayViewport& viewport,
                                   const Rendering::ContactPointPresentation& contact,
                                   const Math::Vector::Vector3& direction, const UI::Style::UIColor& color, float flash,
                                   bool arrow )
{
    UI::UIPoint start;
    UI::UIPoint end;

    if ( !ProjectReplayGatePoint( contact.point, viewport, start ) ||
         !ProjectReplayGatePoint( contact.point + direction, viewport, end ) )
    {
        return;
    }

    const float dx = end.x - start.x;
    const float dy = end.y - start.y;
    const float length = std::sqrt( dx * dx + dy * dy );

    if ( !std::isfinite( length ) || length <= 0.001f )
    {
        return;
    }

    // Why: these glyphs identify the contact basis, not force magnitude. A
    // bounded pixel length keeps them legible during zoom without obscuring bodies.
    const float axisLength = std::clamp( length, arrow ? 18.0f : 10.0f, 72.0f );
    const UI::UIPoint unit { dx / length, dy / length };
    end = { start.x + unit.x * axisLength, start.y + unit.y * axisLength };
    DrawReplayContactStroke( drawList, start, end, color, flash );

    if ( arrow )
    {
        for ( float side : { -1.0f, 1.0f } )
        {
            const UI::UIPoint wing { end.x - unit.x * 6.0f - unit.y * 3.0f * side,
                                     end.y - unit.y * 6.0f + unit.x * 3.0f * side };
            DrawReplayContactStroke( drawList, end, wing, color, flash );
        }
    }
}

static void DrawReplayContactPoint( UI::UIDrawList& drawList, const ReplayOverlayViewport& viewport,
                                    const Rendering::ContactPointPresentation& contact, float flash )
{
    UI::UIPoint center;

    if ( !ProjectReplayGatePoint( contact.point, viewport, center ) )
    {
        return;
    }

    DrawReplayContactAxis( drawList, viewport, contact, contact.normal * 2.5f, { 0.1f, 0.9f, 1.0f, 1.0f }, flash, true );
    DrawReplayContactAxis( drawList, viewport, contact, contact.tangent1 * 1.25f, { 1.0f, 0.5f, 0.1f, 1.0f }, flash, false );
    DrawReplayContactAxis( drawList, viewport, contact, contact.tangent2 * 1.25f, { 1.0f, 0.5f, 0.1f, 1.0f }, flash, false );
    const UI::Style::UIColor gold { 1.0f, contact.exactSourcePoint ? 0.95f : 0.62f, 0.15f, 1.0f };
    DrawReplayContactStroke( drawList, { center.x - 5.0f, center.y }, { center.x + 5.0f, center.y }, gold, flash );
    DrawReplayContactStroke( drawList, { center.x, center.y - 5.0f }, { center.x, center.y + 5.0f }, gold, flash );

    if ( flash > 0.0f )
    {
        // A local ring makes the force event distinct from the steady normal
        // and tangents. Only its alpha changes; the contact location is retained.
        constexpr int sides = 16;
        UI::UIPoint previous { center.x + 11.0f, center.y };

        for ( int side = 1; side <= sides; ++side )
        {
            const float angle = static_cast<float>( side ) * 6.28318530718f / sides;
            const UI::UIPoint next { center.x + 11.0f * std::cos( angle ), center.y + 11.0f * std::sin( angle ) };
            DrawReplayGateStroke( drawList, previous, next, { 1.0f, 1.0f, 1.0f, flash }, 3.0f );
            previous = next;
        }
    }
}

static void ComposeReplayContactManifold( UI::UIDrawList& drawList, const ReplayCauseInspectionView& inspection,
                                          const ReplayOverlayViewport& viewport )
{
    const auto mode = inspection.Transport().mode;

    if ( mode != ReplayCauseInspectionMode::DetailPaused && mode != ReplayCauseInspectionMode::Transporting )
    {
        return;
    }

    drawList.PushClip( { 0.0f, 0.0f, static_cast<float>( viewport.width ), static_cast<float>( viewport.height ) } );
    const Rendering::ContactManifoldPresentation& presentation = inspection.SolverDetail().contactPresentation;
    const std::size_t count = (std::min)( static_cast<std::size_t>( presentation.pointCount ),
                                          Rendering::CONTACT_MANIFOLD_PRESENTATION_POINT_CAPACITY );

    for ( std::size_t index = 0; index < count; ++index )
    {
        DrawReplayContactPoint( drawList, viewport, presentation.points[index], inspection.Display().contactFlashAlpha );
    }

    drawList.PopClip();
}

// Concept: the replay overlay is a read-only projection of replay state.
//
// Input code owns mutations such as dragging, toggling prediction, and branch
// creation. This pass samples the current state and turns it into UI quads and
// text so rendering cannot accidentally advance or rewrite replay timelines.
const UI::UIDrawList& ReplayOverlayDrawOwner::Compose( const ReplayOverlayStateView& replay, bool gameUiSurfaceActive,
                                                       bool scenePhysicsEnabled, ReplayOverlayGestureView gesture,
                                                       ReplayOverlayViewport viewport, double nowSeconds )
{
    m_drawList.Clear();

    if ( !ShouldComposeReplayOverlay( gameUiSurfaceActive ) )
    {
        return m_drawList;
    }

    PROFILE_SCOPED( "Frame/Replay/ScrubberOverlay" );
    ComposeReplayPositionGates( m_drawList, replay.timeline, replay.causality, viewport );
    ComposeReplayContactManifold( m_drawList, replay.causality.inspection, viewport );

    for ( ReplayOverlaySurfaceKind surface : REPLAY_OVERLAY_COMPOSITION_ORDER )
    {
        switch ( surface )
        {
        case ReplayOverlaySurfaceKind::Intercept:
            ComposeReplayInterceptOverlay( m_drawList, replay.planning.intercept, viewport.width, viewport.height );
            break;
        case ReplayOverlaySurfaceKind::TripPlanner:
            ComposeReplayTripPlannerOverlay( m_drawList, replay.planning.tripPlanner, viewport.width, viewport.height );
            break;
        case ReplayOverlaySurfaceKind::Porkchop:
            ComposeReplayPorkchopOverlay( m_drawList, replay.planning.porkchop, viewport.width, viewport.height );
            break;
        case ReplayOverlaySurfaceKind::CauseTree:
            // Why: the cause tree is an inspection tool, not a child of the
            // scrubber. Compose it even when scrubber policy hides its surface.
            ComposeReplayCauseTreeOverlay( m_drawList, replay.causality, replay.timeline.selection, viewport.width,
                                           viewport.height );
            break;
        case ReplayOverlaySurfaceKind::Scrubber:
            break;
        }
    }

    ReplayScrubberComposer( m_drawList, replay.timeline.ScrubberPresentation(), scenePhysicsEnabled, gesture, viewport,
                            nowSeconds )
        .Compose();
    return m_drawList;
}

static void ComposeReplayInterceptOverlay( UI::UIDrawList& drawList, const ReplayInterceptView& intercept, int screenW,
                                           int screenH )
{
    // Why: closest approach is useful while the scrubber is hidden, so this
    // independent GameUI surface is invoked before scrubber visibility policy.
    if ( !intercept.valid || screenW <= 0 || screenH <= 0 )
    {
        return;
    }

    const UI::UIDrawContext draw( screenW, screenH, drawList );
    const UI::UIRect panel = ReplayInterceptReadoutRect( screenW );
    const UI::Style::UIPalette& palette = UI::Style::Palette();
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

    UI::Widgets::DrawPanel( draw, panel, UI::UIVisualState::Visible | UI::UIVisualState::Enabled,
                            UI::Widgets::ComponentAppearance::Compact );
    const float labelWidth = UI::UIFontMetrics::MeasureText( 11.0f, label );
    draw.Text( panel.x + ( panel.w - labelWidth ) * 0.5f, panel.y + 7.0f, 11.0f, accent.r, accent.g, accent.b, label );
}

static void ComposeReplayTripPlannerOverlay( UI::UIDrawList& drawList, const ReplayTripPlannerView& planner, int screenW,
                                             int screenH )
{
    if ( !planner.visible || !planner.available || screenW <= 0 || screenH <= 0 )
    {
        return;
    }

    const UI::UIDrawContext draw( screenW, screenH, drawList );
    const UI::Style::UIPalette& palette = UI::Style::Palette();

    // Invariant: rendering consumes the same fixed control rectangles that
    // ReplayScrubberTools uses for hit testing; draw and input cannot drift.
    ReplayTripPlannerSurface surface;
    BuildReplayTripPlannerSurface( planner, screenW, surface );
    const UI::UIRect panel = ReplayTripPlannerPanelRect( screenW );
    UI::Widgets::DrawPanel( draw, panel, UI::UIVisualState::Visible | UI::UIVisualState::Enabled,
                            UI::Widgets::ComponentAppearance::Compact );

    const auto control = [&]( ReplayTripPlannerControl id ) -> const ReplayTripPlannerControlRow&
    {
        const ReplayTripPlannerControlRow* row = surface.Find( ReplayTripPlannerControlId( id ) );

        if ( !row )
        {
            SB_FATAL( "ReplayTripPlannerSurface", "Render snapshot is missing trip-planner control id=%u.",
                      static_cast<uint32_t>( id ) );
        }

        return *row;
    };

    const auto button = [&]( ReplayTripPlannerControl id, const char* label )
    {
        const ReplayTripPlannerControlRow& row = control( id );
        UI::Widgets::DrawButton( draw, row.drawRect, label, ReplayTripPlannerControlVisualState( row ),
                                 UI::Widgets::ComponentAppearance::Compact );
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
}

static void ComposeReplayPorkchopOverlay( UI::UIDrawList& drawList, const ReplayPorkchopPanelView& porkchop, int screenW,
                                          int screenH )
{
    if ( !porkchop.visible || screenW <= 0 || screenH <= 0 )
    {
        return;
    }

    const UI::UIDrawContext draw( screenW, screenH, drawList );
    const UI::Style::UIPalette& palette = UI::Style::Palette();
    const UI::UIRect panel = ReplayPorkchopPanelRect( screenW );
    const UI::UIRect grid = ReplayPorkchopGridRect( screenW );
    UI::Widgets::DrawPanel( draw, panel, UI::UIVisualState::Visible | UI::UIVisualState::Enabled,
                            UI::Widgets::ComponentAppearance::Compact );

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
}

static void DrawReplayCauseLoading( const UI::UIDrawContext& draw, const UI::UIRect& content,
                                    const ReplayCauseLoadingView& loading )
{
    const UI::Style::UIPalette& palette = UI::Style::Palette();
    const float left = content.x + 20.0f;
    const float width = (std::max)( 0.0f, content.w - 40.0f );
    const float top = content.y + 34.0f;
    draw.Text( left, top, 13.0f, palette.textPrimary.r, palette.textPrimary.g, palette.textPrimary.b,
               "Resolving collisions..." );
    char progressText[32] = {};
    sprintf_s( progressText, "%d%%", static_cast<int>( loading.progress * 100.0f ) );
    draw.Text( left + width - UI::UIFontMetrics::MeasureText( 11.0f, progressText ), top + 29.0f, 11.0f, CAUSE_MANIFOLD.r,
               CAUSE_MANIFOLD.g, CAUSE_MANIFOLD.b, progressText );
    draw.RoundedRect( left, top + 51.0f, width, 8.0f, 4.0f, CAUSE_SELECTED.r, CAUSE_SELECTED.g, CAUSE_SELECTED.b, 1.0f );
    draw.RoundedRect( left, top + 51.0f, width * loading.progress, 8.0f, 4.0f, CAUSE_MANIFOLD.r, CAUSE_MANIFOLD.g,
                      CAUSE_MANIFOLD.b, 1.0f );
    draw.Text( left, top + 77.0f, 10.0f, palette.textMuted.r, palette.textMuted.g, palette.textMuted.b,
               "Evidence appears when ready." );
}

static void ComposeReplayCauseTreeOverlay( UI::UIDrawList& drawList, const ReplayOverlayCausalityView& causality,
                                           const ReplayPresentationSelection& selection, int screenW, int screenH )
{
    PROFILE_SCOPED( "Frame/Replay/CauseTree/Overlay" );
    const bool predictionRows = causality.loading.active ||
                                ( !causality.tree.rows.empty() && causality.tree.rows.front().prediction );

    if ( screenW <= 0 || screenH <= 0 || ( causality.tree.rows.empty() && !causality.loading.active ) ||
         !ReplayPredictionCauseWindowAvailable( causality.predictionDetailMode, predictionRows ) )
    {
        return;
    }

    // Invariant: input clamps the authoring-owned window before publication;
    // drawing consumes that const state and never repairs layout as a side effect.
    ReplayCauseWindowSurface surface;
    BuildReplayCauseWindowSurface( causality.tree, surface );
    surface.ResolvePointer( causality.tree.mouseX, causality.tree.mouseY, causality.tree.pointerBlocked );
    const auto controlRect = [&]( ReplayCauseWindowControl id ) -> const UI::UIRect&
    {
        const ReplayOverlayControl* row = surface.Find( ReplayCauseWindowControlId( id ) );

        if ( !row )
        {
            SB_FATAL( "ReplayCauseWindowSurface", "Render snapshot is missing cause-window control id=%u.",
                      static_cast<uint32_t>( id ) );
        }

        return row->drawRect;
    };

    const UI::UIRect panel = controlRect( ReplayCauseWindowControl::Panel );
    const UI::UIRect title = controlRect( ReplayCauseWindowControl::Title );
    const UI::UIRect filterField = controlRect( ReplayCauseWindowControl::FilterField );
    const UI::UIRect filterFunnel = controlRect( ReplayCauseWindowControl::FilterFunnel );
    const UI::UIRect content = controlRect( ReplayCauseWindowControl::Content );
    const UI::UIRect resize = controlRect( ReplayCauseWindowControl::Resize );

    const UI::UIDrawContext draw( screenW, screenH, drawList );
    const UI::Style::UIPalette& palette = UI::Style::Palette();
    const ReplayCauseInspectorLayout
        inspectorLayout = BuildReplayCauseInspectorLayout( causality.inspection.SolverDetail(), causality.tree, screenW,
                                                           screenH, causality.inspection.Display().drawerProgress );
    RenderReplayCauseSolverDetailPanel( drawList, draw, causality, screenW, screenH );
    UI::Style::UIColor panelBorder = CAUSE_RULE;
    panelBorder.a = 0.72f;
    draw.RoundedPanel( panel, 6.0f, CAUSE_NAVY, panelBorder );
    if ( !causality.loading.active )
    {
        RenderReplayCauseInspectorToggle( draw, inspectorLayout, causality.inspection, causality.tree );
    }
    draw.Rect( title.x + 12.0f, title.y + title.h - 2.0f, (std::max)( 0.0f, title.w - 24.0f ), 2.0f, CAUSE_RULE.r,
               CAUSE_RULE.g, CAUSE_RULE.b, 0.82f );

    draw.Text( panel.x + 12.0f, panel.y + 9.0f, 14.0f, palette.textPrimary.r, palette.textPrimary.g, palette.textPrimary.b,
               "CAUSE HIERARCHY" );

    const char* sourceLabel = predictionRows ? "PREDICT" : "REPLAY";
    const float sourceW = UI::UIFontMetrics::MeasureText( 9.5f, sourceLabel );
    draw.RoundedRect( panel.x + panel.w - sourceW - 14.0f, panel.y + 8.0f, sourceW + 10.0f, 20.0f, 4.0f,
                      predictionRows ? CAUSE_SELECTED.r : CAUSE_NAVY_ALT.r,
                      predictionRows ? CAUSE_SELECTED.g : CAUSE_NAVY_ALT.g,
                      predictionRows ? CAUSE_SELECTED.b : CAUSE_NAVY_ALT.b, 1.0f );

    draw.Text( panel.x + panel.w - sourceW - 9.0f, panel.y + 13.0f, 10.0f,
               predictionRows ? CAUSE_PREDICTION.r : CAUSE_RULE.r, predictionRows ? CAUSE_PREDICTION.g : CAUSE_RULE.g,
               predictionRows ? CAUSE_PREDICTION.b : CAUSE_RULE.b, sourceLabel );

    if ( causality.loading.active )
    {
        DrawReplayCauseLoading( draw, content, causality.loading );
        return;
    }

    draw.RoundedRect( filterField.x, filterField.y, filterField.w, filterField.h, 4.0f,
                      causality.tree.filterFocused ? CAUSE_SELECTED.r : CAUSE_NAVY_ALT.r,
                      causality.tree.filterFocused ? CAUSE_SELECTED.g : CAUSE_NAVY_ALT.g,
                      causality.tree.filterFocused ? CAUSE_SELECTED.b : CAUSE_NAVY_ALT.b, 1.0f );
    const char* filterText = causality.tree.filterText[0] != '\0' ? causality.tree.filterText : "Filter evidence...";
    draw.Text( filterField.x + 9.0f, filterField.y + 6.0f, 10.0f,
               causality.tree.filterText[0] != '\0' ? palette.textPrimary.r : palette.textMuted.r,
               causality.tree.filterText[0] != '\0' ? palette.textPrimary.g : palette.textMuted.g,
               causality.tree.filterText[0] != '\0' ? palette.textPrimary.b : palette.textMuted.b, filterText );
    draw.RoundedRect( filterFunnel.x, filterFunnel.y, filterFunnel.w, filterFunnel.h, 4.0f, CAUSE_NAVY_ALT.r,
                      CAUSE_NAVY_ALT.g, CAUSE_NAVY_ALT.b, 1.0f );
    draw.Rect( filterFunnel.x + 6.0f, filterFunnel.y + 7.0f, 14.0f, 2.0f, CAUSE_RULE.r, CAUSE_RULE.g, CAUSE_RULE.b, 0.86f );
    draw.Rect( filterFunnel.x + 9.0f, filterFunnel.y + 11.0f, 8.0f, 2.0f, CAUSE_RULE.r, CAUSE_RULE.g, CAUSE_RULE.b, 0.86f );
    draw.Rect( filterFunnel.x + 12.0f, filterFunnel.y + 15.0f, 2.0f, 4.0f, CAUSE_RULE.r, CAUSE_RULE.g, CAUSE_RULE.b, 0.86f );

    static constexpr const char* FILTER_LABELS[] = { "ALL", "PREDICTION", "CONTACTS" };
    static constexpr RunReplayCauseTreeFilter FILTER_VALUES[] = { RunReplayCauseTreeFilter::All,
                                                                  RunReplayCauseTreeFilter::Prediction,
                                                                  RunReplayCauseTreeFilter::Contacts };

    for ( int filterIndex = 0; filterIndex < 3; ++filterIndex )
    {
        const RunReplayCauseTreeFilter filter = FILTER_VALUES[filterIndex];
        const UI::UIRect chip = ReplayCauseWindowFilterChipRect( causality.tree, filter );
        const bool active = causality.tree.filter == filter;
        const UI::Style::UIColor& chipFill = active ? CAUSE_SELECTED : CAUSE_NAVY_ALT;
        const UI::Style::UIColor& chipText = filter == RunReplayCauseTreeFilter::Contacts
                                                 ? CAUSE_MANIFOLD
                                                 : ( filter == RunReplayCauseTreeFilter::Prediction ? CAUSE_PREDICTION
                                                                                                    : CAUSE_RULE );
        draw.RoundedRect( chip.x, chip.y, chip.w - 4.0f, chip.h, 4.0f, chipFill.r, chipFill.g, chipFill.b,
                          active ? 1.0f : 0.76f );
        draw.Text( chip.x + 8.0f, chip.y + 6.0f, 9.5f, active ? chipText.r : palette.textMuted.r,
                   active ? chipText.g : palette.textMuted.g, active ? chipText.b : palette.textMuted.b,
                   FILTER_LABELS[filterIndex] );
    }

    draw.RoundedRect( content.x, content.y, content.w, content.h, 6.0f, CAUSE_NAVY.r, CAUSE_NAVY.g, CAUSE_NAVY.b, 1.0f );

    const ReplaySolverFrameSample* scrubSample = selection.currentSolver;
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

    ReplayCauseWindowProjection projection;
    BuildReplayCauseWindowProjection( causality.tree, projection );
    char footerText[96] = {};
    sprintf_s( footerText, sizeof( footerText ), "%d / %zu EVIDENCE   SELECT TO INSPECT", projection.count,
               causality.tree.rows.size() );
    draw.Text( panel.x + 12.0f, panel.y + panel.h - 17.0f, 9.5f, palette.textMuted.r, palette.textMuted.g,
               palette.textMuted.b, footerText );
    const float rowAreaW = content.w - 12.0f;
    const int firstRow = (std::max)( 0,
                                     static_cast<int>( floorf( causality.tree.scrollY / REPLAY_CAUSE_WINDOW_ROW_HEIGHT ) ) );

    const int rowCount = projection.count;
    int hoveredRow = -1;

    if ( surface.hasHotControl && surface.hotControl == ReplayCauseWindowControlId( ReplayCauseWindowControl::Content ) )
    {
        const float localY = static_cast<float>( causality.tree.mouseY ) - content.y + causality.tree.scrollY;
        const int candidate = static_cast<int>( floorf( localY / REPLAY_CAUSE_WINDOW_ROW_HEIGHT ) );

        if ( candidate >= 0 && candidate < rowCount )
        {
            hoveredRow = candidate;
        }
    }

    if ( rowCount == 0 )
    {
        draw.Text( content.x + 12.0f, content.y + 16.0f, 11.0f, palette.textMuted.r, palette.textMuted.g,
                   palette.textMuted.b, "No matching evidence" );
    }

    for ( int visibleRow = firstRow; visibleRow < rowCount; ++visibleRow )
    {
        const int sourceRow = projection.SourceRow( visibleRow );
        const RunReplayCauseTreeRow& row = causality.tree.rows[static_cast<std::size_t>( sourceRow )];
        const float rowY = content.y + static_cast<float>( visibleRow ) * REPLAY_CAUSE_WINDOW_ROW_HEIGHT -
                           causality.tree.scrollY;

        if ( rowY + REPLAY_CAUSE_WINDOW_ROW_HEIGHT < content.y )
        {
            continue;
        }

        if ( rowY + REPLAY_CAUSE_WINDOW_ROW_HEIGHT > content.y + content.h )
        {
            break;
        }

        const UI::UIRect rowRect = { content.x + 2.0f, rowY, rowAreaW, REPLAY_CAUSE_WINDOW_ROW_HEIGHT - 2.0f };
        const bool hovered = visibleRow == hoveredRow;
        const bool selected = sourceRow == causality.tree.selectedRow;

        if ( selected )
        {
            draw.RoundedRect( rowRect.x, rowRect.y, rowRect.w, rowRect.h, 4.0f, CAUSE_SELECTED.r, CAUSE_SELECTED.g,
                              CAUSE_SELECTED.b, 1.0f );
        }
        else
        {
            const UI::Style::UIColor& rowFill = ( visibleRow % 2 ) == 0 ? CAUSE_NAVY_ALT : CAUSE_NAVY;
            draw.RoundedRect( rowRect.x, rowRect.y, rowRect.w, rowRect.h, 4.0f, rowFill.r, rowFill.g, rowFill.b,
                              hovered ? 1.0f : 0.78f );
        }

        const float indent = (std::min)( rowRect.w * 0.40f, static_cast<float>( row.depth ) * 16.0f );

        if ( row.depth > 0 )
        {
            const float lineX = rowRect.x + 8.0f + indent - 9.0f;
            draw.Rect( lineX, rowRect.y + 4.0f, 1.0f, rowRect.h - 8.0f, CAUSE_RULE.r, CAUSE_RULE.g, CAUSE_RULE.b, 0.28f );

            draw.Rect( lineX, rowRect.y + rowRect.h * 0.5f, 8.0f, 1.0f, CAUSE_RULE.r, CAUSE_RULE.g, CAUSE_RULE.b, 0.28f );
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

        float markerR = CAUSE_PREDICTION.r;
        float markerG = CAUSE_PREDICTION.g;
        float markerB = CAUSE_PREDICTION.b;

        if ( row.kind == RunReplayCauseTreeRowKind::Manifold )
        {
            markerR = CAUSE_MANIFOLD.r;
            markerG = CAUSE_MANIFOLD.g;
            markerB = CAUSE_MANIFOLD.b;
        }
        else if ( row.kind == RunReplayCauseTreeRowKind::SolverRow )
        {
            markerR = CAUSE_SOLVER.r;
            markerG = CAUSE_SOLVER.g;
            markerB = CAUSE_SOLVER.b;
        }
        else if ( row.kind == RunReplayCauseTreeRowKind::PredictionContact )
        {
            markerR = CAUSE_PREDICTION.r;
            markerG = CAUSE_PREDICTION.g;
            markerB = CAUSE_PREDICTION.b;
        }
        else if ( row.kind == RunReplayCauseTreeRowKind::PredictionMotion )
        {
            markerR = CAUSE_RULE.r;
            markerG = CAUSE_RULE.g;
            markerB = CAUSE_RULE.b;
        }

        const float markerX = rowRect.x + 8.0f + indent;
        const float markerY = rowRect.y + 8.0f;
        draw.Rect( markerX, markerY, 6.0f, 6.0f, markerR, markerG, markerB, 0.92f );
        draw.Text( markerX + 11.0f, rowRect.y + 4.0f, 12.0f,
                   row.kind == RunReplayCauseTreeRowKind::Body ? palette.textPrimary.r : palette.textSecondary.r,
                   row.kind == RunReplayCauseTreeRowKind::Body ? palette.textPrimary.g : palette.textSecondary.g,
                   row.kind == RunReplayCauseTreeRowKind::Body ? palette.textPrimary.b : palette.textSecondary.b,
                   clippedLabel );

        if ( clippedDetail[0] != '\0' )
        {
            draw.Text( markerX + 11.0f, rowRect.y + 22.0f, 10.0f, palette.textMuted.r, palette.textMuted.g,
                       palette.textMuted.b, clippedDetail );
        }
    }

    const float maxScroll = ReplayCauseWindowMaxScroll( causality.tree );

    if ( maxScroll > 0.0f )
    {
        const float trackX = content.x + content.w - 5.0f;
        draw.Rect( trackX, content.y + 3.0f, 3.0f, content.h - 6.0f, CAUSE_NAVY_ALT.r, CAUSE_NAVY_ALT.g, CAUSE_NAVY_ALT.b,
                   1.0f );

        const float contentHeight = ReplayCauseWindowContentHeight( causality.tree );
        const float knobH = (std::max)( 24.0f, ( content.h / contentHeight ) * ( content.h - 6.0f ) );
        const float knobY = content.y + 3.0f + ( causality.tree.scrollY / maxScroll ) * ( content.h - 6.0f - knobH );
        draw.RoundedRect( trackX - 1.0f, knobY, 5.0f, knobH, 2.0f, CAUSE_RULE.r, CAUSE_RULE.g, CAUSE_RULE.b, 0.88f );
    }

    draw.Rect( resize.x + 4.0f, resize.y + resize.h - 5.0f, resize.w - 7.0f, 1.0f, CAUSE_RULE.r, CAUSE_RULE.g, CAUSE_RULE.b,
               0.72f );

    draw.Rect( resize.x + resize.w - 5.0f, resize.y + 4.0f, 1.0f, resize.h - 7.0f, CAUSE_RULE.r, CAUSE_RULE.g, CAUSE_RULE.b,
               0.72f );
}
} // namespace SkullbonezCore::Runtime::ReplayOverlay
