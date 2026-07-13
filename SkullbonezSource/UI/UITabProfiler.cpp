/*
File: SkullbonezSource/UI/UITabProfiler.cpp
Purpose:
  Implements UI TabProfiler widgets, layout, drawing, or UI state for the in-engine controls.

Summary:
  UITabProfiler.cpp implements UI TabProfiler widgets, layout, drawing, or UI
  state for the in-engine controls. As an implementation unit, keep edits
  anchored on UI request, layout, hit-test, and draw-command flow and on the
  glossary/invariants below.

Glossary:
  Draw command: Lightweight record describing a UI shape or text batch to
  render later in the frame.
  Hit box: Screen-space rectangle used to decide whether mouse input targets a
  widget.

Invariants:
  - Draw geometry and hit testing must be derived from the same layout
  constants.

Related:
  - SkullbonezSource/UI/UITabProfiler.h
  - Agentic/Reference/comment-style-guide.md
*/
#include "UITabProfiler.h"
#include "../Assets/AssetKeys.h"

#include "../Core/Common.h"
#include "../Rendering/Text.h"
#include "UI.h"
#include "UIDraw.h"
#include "UIIconButton.h"
#include "UIStyle.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdio>
#include <cstring>

namespace
{

constexpr float PROFILER_UI_TIMELINE_BUDGET_MS = 16.67f;
constexpr float PROFILER_WORKER_BLOCK_H = 96.0f;
constexpr float PROFILER_TABLE_OFFSET_H = PROFILER_WORKER_BLOCK_H;
constexpr float PROFILER_WORKER_TOGGLE_Y = 12.0f;
constexpr float PROFILER_WORKER_SLIDER_Y = 52.0f;
constexpr float PROFILER_CORE_CHART_H = 142.0f;
constexpr float PROFILER_CORE_CHART_AXIS_MIN_MS = 0.50f;
constexpr float HISTOGRAM_AXIS_LABEL_GUTTER = 54.0f;

struct ProfilerUiColor
{
    float r, g, b;
};

constexpr ProfilerUiColor PROFILER_UI_PALETTE[] = {
    { 0.90f, 0.30f, 0.30f }, { 0.30f, 0.75f, 0.93f }, { 0.40f, 0.85f, 0.40f }, { 0.95f, 0.70f, 0.20f },
    { 0.70f, 0.40f, 0.90f }, { 0.20f, 0.90f, 0.80f }, { 0.95f, 0.50f, 0.70f }, { 0.55f, 0.80f, 0.25f },
    { 0.30f, 0.50f, 0.95f }, { 0.95f, 0.85f, 0.30f }, { 0.85f, 0.45f, 0.20f }, { 0.50f, 0.90f, 0.60f },
    { 0.80f, 0.30f, 0.70f }, { 0.60f, 0.70f, 0.85f }, { 0.90f, 0.60f, 0.40f }, { 0.35f, 0.65f, 0.55f },
    { 0.75f, 0.55f, 0.85f }, { 0.65f, 0.85f, 0.75f }, { 0.85f, 0.75f, 0.55f }, { 0.45f, 0.45f, 0.80f },
};
constexpr int PROFILER_UI_PALETTE_SIZE =
    static_cast<int>( sizeof( PROFILER_UI_PALETTE ) / sizeof( PROFILER_UI_PALETTE[0] ) );

const ProfilerUiColor& ProfilerPaletteColor( int index )
{
    int paletteIndex = index % PROFILER_UI_PALETTE_SIZE;
    if ( paletteIndex < 0 )
    {
        paletteIndex += PROFILER_UI_PALETTE_SIZE;
    }
    return PROFILER_UI_PALETTE[paletteIndex];
}

bool IsProfilerRowVisible( float contentY, float contentH, float rowY, float rowH )
{
    return rowY + rowH >= contentY && rowY <= contentY + contentH;
}

void SetProfilerContentBounds( SkullbonezCore::UI::ProfilerTab::UIProfilerTabState& state,
                               float contentX,
                               float contentY,
                               float contentW )
{
    state.workerToggle.SetBounds( contentX, contentY + PROFILER_WORKER_TOGGLE_Y, 172.0f, 24.0f );
    state.workerThreadSlider.SetBounds( contentX, contentY + PROFILER_WORKER_SLIDER_Y, contentW, 34.0f );
}

bool ProfilerMarkerHasChildren( const SkullbonezCore::UI::ProfilerTab::FrameSnapshot& frame, int markerIndex )
{
    for ( int i = 0; i < frame.markerCount; ++i )
    {
        if ( frame.markers[i].parentIndex == markerIndex )
        {
            return true;
        }
    }
    return false;
}

float ProfilerMarkerDisplayCpuMs( const SkullbonezCore::UI::ProfilerTab::MarkerSnapshot& marker )
{
    return marker.avgMs > 0.0f ? marker.avgMs : marker.lastFrameMs;
}

float ProfilerMarkerDisplaySelfMs( const SkullbonezCore::UI::ProfilerTab::MarkerSnapshot& marker )
{
    return marker.selfAvgMs > 0.0f ? marker.selfAvgMs : marker.lastSelfMs;
}

bool IsMarkerExpanded( const SkullbonezCore::UI::ProfilerTab::UIProfilerTabState& state, uint32_t hash )
{
    for ( int i = 0; i < state.expandedHashCount; ++i )
    {
        if ( state.expandedHashes[i] == hash )
        {
            return true;
        }
    }
    return false;
}

void ToggleMarker( SkullbonezCore::UI::ProfilerTab::UIProfilerTabState& state, uint32_t hash )
{
    // Invariant: Expanded profiler rows are tracked by marker hash in a bounded
    // array so UI state survives marker reordering without heap churn.
    state.defaultExpansionApplied = true;
    for ( int i = 0; i < state.expandedHashCount; ++i )
    {
        if ( state.expandedHashes[i] == hash )
        {
            for ( int j = i; j < state.expandedHashCount - 1; ++j )
            {
                state.expandedHashes[j] = state.expandedHashes[j + 1];
            }
            --state.expandedHashCount;
            return;
        }
    }
    if ( state.expandedHashCount < SkullbonezCore::UI::ProfilerTab::MAX_MARKERS )
    {
        state.expandedHashes[state.expandedHashCount++] = hash;
    }
}

bool IsDrawNodeExpanded( const SkullbonezCore::UI::ProfilerTab::UIProfilerTabState& state, uint32_t hash )
{
    for ( int i = 0; i < state.drawExpandedHashCount; ++i )
    {
        if ( state.drawExpandedHashes[i] == hash )
        {
            return true;
        }
    }
    return false;
}

void ToggleDrawNode( SkullbonezCore::UI::ProfilerTab::UIProfilerTabState& state, uint32_t hash )
{
    state.drawDefaultExpansionApplied = true;
    for ( int i = 0; i < state.drawExpandedHashCount; ++i )
    {
        if ( state.drawExpandedHashes[i] == hash )
        {
            for ( int j = i; j < state.drawExpandedHashCount - 1; ++j )
            {
                state.drawExpandedHashes[j] = state.drawExpandedHashes[j + 1];
            }
            --state.drawExpandedHashCount;
            return;
        }
    }
    if ( state.drawExpandedHashCount < SkullbonezCore::UI::ProfilerTab::MAX_MARKERS )
    {
        state.drawExpandedHashes[state.drawExpandedHashCount++] = hash;
    }
}

bool DrawNodeHasVisibleChildren( const SkullbonezCore::UI::ProfilerTab::DrawTraceSnapshot& snapshot, int nodeIndex )
{
    for ( int i = 0; i < snapshot.nodeCount; ++i )
    {
        if ( snapshot.nodes[i].parentIndex == nodeIndex && snapshot.nodes[i].drawCallCount > 0 )
        {
            return true;
        }
    }
    return false;
}

int BuildVisibleDrawRows( const SkullbonezCore::UI::ProfilerTab::UIProfilerTabState& state,
                          const SkullbonezCore::UI::ProfilerTab::DrawTraceSnapshot& snapshot,
                          int* rows,
                          int maxRows )
{
    if ( !snapshot.nodes || snapshot.nodeCount <= 0 )
    {
        return 0;
    }

    int childIndices[SkullbonezCore::UI::ProfilerTab::MAX_MARKERS][SkullbonezCore::UI::ProfilerTab::MAX_MARKERS] = {};
    int childCounts[SkullbonezCore::UI::ProfilerTab::MAX_MARKERS] = {};
    const int nodeCount = (std::min)( snapshot.nodeCount, SkullbonezCore::UI::ProfilerTab::MAX_MARKERS );

    for ( int i = 0; i < nodeCount; ++i )
    {
        const int parentIndex = snapshot.nodes[i].parentIndex;
        if ( parentIndex >= 0 && parentIndex < nodeCount )
        {
            childIndices[parentIndex][childCounts[parentIndex]++] = i;
        }
    }

    int stack[SkullbonezCore::UI::ProfilerTab::MAX_MARKERS] = {};
    int stackTop = 0;
    for ( int i = nodeCount - 1; i >= 0; --i )
    {
        if ( snapshot.nodes[i].parentIndex == -1 && snapshot.nodes[i].drawCallCount > 0 )
        {
            stack[stackTop++] = i;
        }
    }

    int rowCount = 0;
    while ( stackTop > 0 )
    {
        const int nodeIndex = stack[--stackTop];
        if ( rowCount < maxRows )
        {
            rows[rowCount] = nodeIndex;
        }
        ++rowCount;

        const SkullbonezCore::UI::ProfilerTab::DrawTraceNodeSnapshot& node = snapshot.nodes[nodeIndex];
        if ( !IsDrawNodeExpanded( state, node.hash ) )
        {
            continue;
        }
        for ( int child = childCounts[nodeIndex] - 1; child >= 0; --child )
        {
            const int childIndex = childIndices[nodeIndex][child];
            if ( snapshot.nodes[childIndex].drawCallCount <= 0 )
            {
                continue;
            }
            if ( stackTop < SkullbonezCore::UI::ProfilerTab::MAX_MARKERS )
            {
                stack[stackTop++] = childIndex;
            }
        }
    }

    return (std::min)( rowCount, maxRows );
}

int BuildVisibleRows( const SkullbonezCore::UI::ProfilerTab::UIProfilerTabState& state, int* rows, int maxRows )
{
    const SkullbonezCore::UI::ProfilerTab::FrameSnapshot& frame = state.frame;
    const int markerCount = (std::min)( frame.markerCount, SkullbonezCore::UI::ProfilerTab::MAX_MARKERS );
    int childIndices[SkullbonezCore::UI::ProfilerTab::MAX_MARKERS][SkullbonezCore::UI::ProfilerTab::MAX_MARKERS] = {};
    int childCounts[SkullbonezCore::UI::ProfilerTab::MAX_MARKERS] = {};

    for ( int i = 0; i < markerCount; ++i )
    {
        const int parentIndex = frame.markers[i].parentIndex;
        if ( parentIndex >= 0 && parentIndex < markerCount )
        {
            childIndices[parentIndex][childCounts[parentIndex]++] = i;
        }
    }

    int stack[SkullbonezCore::UI::ProfilerTab::MAX_MARKERS] = {};
    int stackTop = 0;
    for ( int i = markerCount - 1; i >= 0; --i )
    {
        if ( frame.markers[i].parentIndex == -1 )
        {
            stack[stackTop++] = i;
        }
    }

    int rowCount = 0;
    while ( stackTop > 0 )
    {
        const int markerIndex = stack[--stackTop];
        if ( rowCount < maxRows )
        {
            rows[rowCount] = markerIndex;
        }
        ++rowCount;

        const SkullbonezCore::UI::ProfilerTab::MarkerSnapshot& marker = frame.markers[markerIndex];
        if ( !IsMarkerExpanded( state, marker.hash ) )
        {
            continue;
        }
        for ( int child = childCounts[markerIndex] - 1; child >= 0; --child )
        {
            if ( stackTop < SkullbonezCore::UI::ProfilerTab::MAX_MARKERS )
            {
                stack[stackTop++] = childIndices[markerIndex][child];
            }
        }
    }

    return (std::min)( rowCount, maxRows );
}

void BuildTimelineSegments( const SkullbonezCore::UI::ProfilerTab::UIProfilerTabState& state,
                            const int* rows,
                            int rowCount,
                            SkullbonezCore::UI::ProfilerTab::TimelineSegment* segments )
{
    const SkullbonezCore::UI::ProfilerTab::FrameSnapshot& frame = state.frame;
    const int markerCount = (std::min)( frame.markerCount, SkullbonezCore::UI::ProfilerTab::MAX_MARKERS );
    int rowForMarker[SkullbonezCore::UI::ProfilerTab::MAX_MARKERS] = {};
    int childIndices[SkullbonezCore::UI::ProfilerTab::MAX_MARKERS][SkullbonezCore::UI::ProfilerTab::MAX_MARKERS] = {};
    int childCounts[SkullbonezCore::UI::ProfilerTab::MAX_MARKERS] = {};

    for ( int i = 0; i < SkullbonezCore::UI::ProfilerTab::MAX_MARKERS; ++i )
    {
        rowForMarker[i] = -1;
    }
    for ( int row = 0; row < rowCount; ++row )
    {
        segments[row] = {};
        if ( rows[row] >= 0 && rows[row] < markerCount )
        {
            rowForMarker[rows[row]] = row;
        }
    }

    for ( int i = 0; i < markerCount; ++i )
    {
        const int parentIndex = frame.markers[i].parentIndex;
        if ( parentIndex >= 0 && parentIndex < markerCount )
        {
            childIndices[parentIndex][childCounts[parentIndex]++] = i;
        }
    }

    auto assignSubtree = [&]( auto&& self, int markerIndex, float startMs ) -> void
    {
        const SkullbonezCore::UI::ProfilerTab::MarkerSnapshot& marker = frame.markers[markerIndex];
        const bool hasChildren = childCounts[markerIndex] > 0;
        const bool expanded = hasChildren && IsMarkerExpanded( state, marker.hash );
        const int row = rowForMarker[markerIndex];
        const float markerMs = (std::max)( 0.0f, ProfilerMarkerDisplayCpuMs( marker ) );

        if ( row >= 0 )
        {
            segments[row].startMs = startMs;
            segments[row].durationMs = markerMs;
            segments[row].isFilled = !expanded;
        }

        if ( !expanded )
        {
            return;
        }

        float childStartMs = startMs;
        for ( int child = 0; child < childCounts[markerIndex]; ++child )
        {
            const int childIndex = childIndices[markerIndex][child];
            self( self, childIndex, childStartMs );
            childStartMs += (std::max)( 0.0f, ProfilerMarkerDisplayCpuMs( frame.markers[childIndex] ) );
        }
    };

    float rootStartMs = 0.0f;
    for ( int i = 0; i < markerCount; ++i )
    {
        if ( frame.markers[i].parentIndex == -1 )
        {
            assignSubtree( assignSubtree, i, rootStartMs );
            rootStartMs += (std::max)( 0.0f, ProfilerMarkerDisplayCpuMs( frame.markers[i] ) );
        }
    }
}

constexpr float HISTOGRAM_PANEL_MIN_W = 260.0f;
constexpr float HISTOGRAM_PANEL_MIN_H = 132.0f;
constexpr float HISTOGRAM_PANEL_DEFAULT_W = 340.0f;
constexpr float HISTOGRAM_PANEL_DEFAULT_H = 166.0f;
constexpr float HISTOGRAM_PANEL_MARGIN = 8.0f;
constexpr float HISTOGRAM_HEADER_H = 28.0f;
constexpr float HISTOGRAM_SELECTOR_H = 24.0f;
constexpr float HISTOGRAM_RESIZE_HOTSPOT = 22.0f;
constexpr float HISTOGRAM_DROPDOWN_ROW_H = 22.0f;
constexpr float HISTOGRAM_DROPDOWN_FOOTER_H = 16.0f;
constexpr int HISTOGRAM_DROPDOWN_VISIBLE_ROWS = 8;
constexpr float HISTOGRAM_SAMPLE_CLAMP_MS = 250.0f;
constexpr float HISTOGRAM_FRAME_CPU_BUDGET_MS = 16.7f;
constexpr float HISTOGRAM_FRAME_CPU_DEFAULT_AXIS_MS = 33.3f;
constexpr float HISTOGRAM_MARKER_DEFAULT_AXIS_MS = 16.67f;
constexpr double HISTOGRAM_AVERAGE_TEXT_REFRESH_SECONDS = 0.5;
constexpr int HISTOGRAM_OPTION_CAPACITY = SkullbonezCore::UI::ProfilerTab::MAX_MARKERS + 1;

bool HistogramOptionKeyMatches( uint32_t hash, bool frameTotal, uint32_t candidateHash, bool candidateFrameTotal )
{
    return frameTotal == candidateFrameTotal && ( frameTotal || hash == candidateHash );
}

bool HistogramMainSelected( const SkullbonezCore::UI::ProfilerTab::UIProfilerTabState& state )
{
    for ( int i = 0; i < state.histogramOptionCount; ++i )
    {
        if ( state.histogramOptionFrameTotals[i] && state.histogramOptionSelected[i] )
        {
            return true;
        }
    }
    return false;
}

int HistogramSelectedOptionCount( const SkullbonezCore::UI::ProfilerTab::UIProfilerTabState& state )
{
    int count = 0;
    for ( int i = 0; i < state.histogramOptionCount; ++i )
    {
        if ( state.histogramOptionSelected[i] )
        {
            ++count;
        }
    }
    return count;
}

bool HistogramAnyOptionSelected( const SkullbonezCore::UI::ProfilerTab::UIProfilerTabState& state )
{
    return HistogramSelectedOptionCount( state ) > 0;
}

float HistogramInitialAxisMs( const SkullbonezCore::UI::ProfilerTab::UIProfilerTabState& state )
{
    return HistogramMainSelected( state ) ? HISTOGRAM_FRAME_CPU_DEFAULT_AXIS_MS : HISTOGRAM_MARKER_DEFAULT_AXIS_MS;
}

float HistogramMinimumAxisMs( const SkullbonezCore::UI::ProfilerTab::UIProfilerTabState& state )
{
    return HistogramMainSelected( state ) ? HISTOGRAM_FRAME_CPU_DEFAULT_AXIS_MS : 0.25f;
}

void ClearHistogramSamples( SkullbonezCore::UI::ProfilerTab::UIProfilerTabState& state )
{
    state.histogramHead = 0;
    state.histogramCount = 0;
    state.histogramAxisMs = HistogramInitialAxisMs( state );
    state.histogramAverageTextLastUpdateSeconds = -1.0;
    state.histogramAverageCpuMs = 0.0f;
    state.histogramAverageWorkerMs = 0.0f;
    for ( int i = 0; i < SkullbonezCore::UI::ProfilerTab::HISTOGRAM_SAMPLE_COUNT; ++i )
    {
        state.histogramSamples[i] = {};
    }
}

int HistogramVisibleDropdownRows( int optionCount )
{
    return std::clamp( optionCount, 0, HISTOGRAM_DROPDOWN_VISIBLE_ROWS );
}

int HistogramMaxScroll( const SkullbonezCore::UI::ProfilerTab::UIProfilerTabState& state )
{
    return (std::max)( 0, state.histogramOptionCount - HistogramVisibleDropdownRows( state.histogramOptionCount ) );
}

int FindCachedHistogramSelectionIndex( const SkullbonezCore::UI::ProfilerTab::UIProfilerTabState& state )
{
    for ( int i = 0; i < state.histogramOptionCount; ++i )
    {
        if ( state.histogramOptionSelected[i] )
        {
            return i;
        }
    }
    return 0;
}

void ToggleHistogramSelectionFromCache( SkullbonezCore::UI::ProfilerTab::UIProfilerTabState& state, int optionIndex )
{
    if ( optionIndex < 0 || optionIndex >= state.histogramOptionCount )
    {
        return;
    }

    state.histogramOptionSelected[optionIndex] = !state.histogramOptionSelected[optionIndex];
    ClearHistogramSamples( state );
}

void ClampHistogramPanelToScreen( SkullbonezCore::UI::ProfilerTab::UIProfilerTabState& state, int screenW, int screenH )
{
    screenW = (std::max)( 1, screenW );
    screenH = (std::max)( 1, screenH );

    const float usableW = (std::max)( 80.0f, static_cast<float>( screenW ) - HISTOGRAM_PANEL_MARGIN * 2.0f );
    const float usableH = (std::max)( 80.0f, static_cast<float>( screenH ) - HISTOGRAM_PANEL_MARGIN * 2.0f );
    const float minW = (std::min)( HISTOGRAM_PANEL_MIN_W, usableW );
    const float minH = (std::min)( HISTOGRAM_PANEL_MIN_H, usableH );

    if ( !state.histogramPanelInitialized )
    {
        state.histogramPanelW = (std::min)( HISTOGRAM_PANEL_DEFAULT_W, usableW );
        state.histogramPanelH = (std::min)( HISTOGRAM_PANEL_DEFAULT_H, usableH );
        state.histogramPanelX = HISTOGRAM_PANEL_MARGIN * 2.0f;
        state.histogramPanelY = HISTOGRAM_PANEL_MARGIN * 2.0f;
        state.histogramPanelInitialized = true;
    }

    state.histogramPanelW = std::clamp( state.histogramPanelW, minW, usableW );
    state.histogramPanelH = std::clamp( state.histogramPanelH, minH, usableH );

    const float maxX = (std::max)( HISTOGRAM_PANEL_MARGIN,
                                   static_cast<float>( screenW ) - state.histogramPanelW - HISTOGRAM_PANEL_MARGIN );
    const float maxY = (std::max)( HISTOGRAM_PANEL_MARGIN,
                                   static_cast<float>( screenH ) - state.histogramPanelH - HISTOGRAM_PANEL_MARGIN );
    state.histogramPanelX = std::clamp( state.histogramPanelX, HISTOGRAM_PANEL_MARGIN, maxX );
    state.histogramPanelY = std::clamp( state.histogramPanelY, HISTOGRAM_PANEL_MARGIN, maxY );
}

SkullbonezCore::UI::UIRect HistogramPanelBounds( const SkullbonezCore::UI::ProfilerTab::UIProfilerTabState& state )
{
    return { state.histogramPanelX, state.histogramPanelY, state.histogramPanelW, state.histogramPanelH };
}

SkullbonezCore::UI::UIRect HistogramHeaderBounds( const SkullbonezCore::UI::ProfilerTab::UIProfilerTabState& state )
{
    const SkullbonezCore::UI::UIRect panel = HistogramPanelBounds( state );
    return { panel.x, panel.y, panel.w, HISTOGRAM_HEADER_H };
}

SkullbonezCore::UI::UIRect HistogramSelectorBounds( const SkullbonezCore::UI::ProfilerTab::UIProfilerTabState& state )
{
    const SkullbonezCore::UI::UIRect panel = HistogramPanelBounds( state );
    return { panel.x + 10.0f, panel.y + 32.0f, (std::max)( 32.0f, panel.w - 20.0f ), HISTOGRAM_SELECTOR_H };
}

SkullbonezCore::UI::UIRect HistogramResizeBounds( const SkullbonezCore::UI::ProfilerTab::UIProfilerTabState& state )
{
    const SkullbonezCore::UI::UIRect panel = HistogramPanelBounds( state );
    return { panel.x + panel.w - HISTOGRAM_RESIZE_HOTSPOT,
             panel.y + panel.h - HISTOGRAM_RESIZE_HOTSPOT,
             HISTOGRAM_RESIZE_HOTSPOT,
             HISTOGRAM_RESIZE_HOTSPOT };
}

SkullbonezCore::UI::UIRect HistogramPlotBounds( const SkullbonezCore::UI::ProfilerTab::UIProfilerTabState& state )
{
    const SkullbonezCore::UI::UIRect panel = HistogramPanelBounds( state );
    const float plotY = panel.y + 66.0f;
    const float plotH = (std::max)( 34.0f, panel.h - 96.0f );
    return { panel.x + 10.0f + HISTOGRAM_AXIS_LABEL_GUTTER,
             plotY,
             (std::max)( 32.0f, panel.w - 20.0f - HISTOGRAM_AXIS_LABEL_GUTTER ),
             plotH };
}

SkullbonezCore::UI::UIRect HistogramDropdownBounds( const SkullbonezCore::UI::ProfilerTab::UIProfilerTabState& state,
                                                    int screenH )
{
    const SkullbonezCore::UI::UIRect selector = HistogramSelectorBounds( state );
    const int visibleRows = HistogramVisibleDropdownRows( state.histogramOptionCount );
    const float footerH = state.histogramOptionCount > visibleRows ? HISTOGRAM_DROPDOWN_FOOTER_H : 0.0f;
    const float dropdownH = 4.0f + static_cast<float>( visibleRows ) * HISTOGRAM_DROPDOWN_ROW_H + footerH;
    float dropdownY = selector.y + selector.h + 4.0f;
    if ( dropdownY + dropdownH > static_cast<float>( screenH ) - HISTOGRAM_PANEL_MARGIN )
    {
        dropdownY = selector.y - dropdownH - 4.0f;
    }
    return { selector.x, dropdownY, selector.w, dropdownH };
}

int HitHistogramDropdownOption( const SkullbonezCore::UI::ProfilerTab::UIProfilerTabState& state,
                                int screenH,
                                int mouseX,
                                int mouseY )
{
    const SkullbonezCore::UI::UIRect dropdown = HistogramDropdownBounds( state, screenH );
    if ( !dropdown.Contains( mouseX, mouseY ) )
    {
        return -1;
    }

    const int row = static_cast<int>( ( static_cast<float>( mouseY ) - dropdown.y - 2.0f ) / HISTOGRAM_DROPDOWN_ROW_H );
    const int optionIndex = state.histogramSelectorScroll + row;
    if ( row < 0 || row >= HistogramVisibleDropdownRows( state.histogramOptionCount ) ||
         optionIndex >= state.histogramOptionCount )
    {
        return -1;
    }
    return optionIndex;
}

void RemapHistogramSamples( SkullbonezCore::UI::ProfilerTab::UIProfilerTabState& state,
                            const uint32_t* oldHashes,
                            const bool* oldFrameTotals,
                            int oldCount )
{
    if ( !oldHashes || !oldFrameTotals || oldCount <= 0 || state.histogramCount <= 0 )
    {
        return;
    }

    for ( int i = 0; i < state.histogramCount; ++i )
    {
        const int sampleIndex = ( state.histogramHead - state.histogramCount + i +
                                  SkullbonezCore::UI::ProfilerTab::HISTOGRAM_SAMPLE_COUNT ) %
                                SkullbonezCore::UI::ProfilerTab::HISTOGRAM_SAMPLE_COUNT;
        const SkullbonezCore::UI::ProfilerTab::PerformanceHistogramSample oldSample =
            state.histogramSamples[sampleIndex];
        SkullbonezCore::UI::ProfilerTab::PerformanceHistogramSample remappedSample = {};
        remappedSample.secondaryMs = oldSample.secondaryMs;
        remappedSample.hasSecondary = oldSample.hasSecondary;

        for ( int newIndex = 0; newIndex < state.histogramOptionCount; ++newIndex )
        {
            for ( int oldIndex = 0; oldIndex < oldCount; ++oldIndex )
            {
                if ( HistogramOptionKeyMatches( oldHashes[oldIndex],
                                                oldFrameTotals[oldIndex],
                                                state.histogramOptionHashes[newIndex],
                                                state.histogramOptionFrameTotals[newIndex] ) )
                {
                    remappedSample.markerMs[newIndex] = oldSample.markerMs[oldIndex];
                    remappedSample.markerSpikeMs[newIndex] = oldSample.markerSpikeMs[oldIndex];
                    remappedSample.hasMarker[newIndex] = oldSample.hasMarker[oldIndex];
                    break;
                }
            }
        }
        state.histogramSamples[sampleIndex] = remappedSample;
    }
}

void CacheHistogramOptions( SkullbonezCore::UI::ProfilerTab::UIProfilerTabState& state,
                            const SkullbonezCore::UI::InGameUIFrameData& data )
{
    uint32_t oldHashes[HISTOGRAM_OPTION_CAPACITY] = {};
    bool oldFrameTotals[HISTOGRAM_OPTION_CAPACITY] = {};
    bool oldSelected[HISTOGRAM_OPTION_CAPACITY] = {};
    const int oldCount = state.histogramOptionCount;
    for ( int i = 0; i < oldCount; ++i )
    {
        oldHashes[i] = state.histogramOptionHashes[i];
        oldFrameTotals[i] = state.histogramOptionFrameTotals[i];
        oldSelected[i] = state.histogramOptionSelected[i];
    }

    const bool defaultSelection = !state.histogramSelectionInitialized || oldCount <= 0;
    state.histogramOptionCount = std::clamp( data.profilerMarkerOptionCount, 0, HISTOGRAM_OPTION_CAPACITY );
    bool cacheChanged = oldCount != state.histogramOptionCount;
    bool selectedAny = false;
    for ( int i = 0; i < state.histogramOptionCount; ++i )
    {
        const SkullbonezCore::UI::UIProfilerMarkerOption& option = data.profilerMarkerOptions[i];
        if ( !cacheChanged && !HistogramOptionKeyMatches( state.histogramOptionHashes[i],
                                                          state.histogramOptionFrameTotals[i],
                                                          option.hash,
                                                          option.isFrameTotal ) )
        {
            cacheChanged = true;
        }

        state.histogramOptionHashes[i] = option.hash;
        state.histogramOptionFrameTotals[i] = option.isFrameTotal;
        state.histogramOptionSelected[i] = false;
        if ( defaultSelection )
        {
            state.histogramOptionSelected[i] = option.isFrameTotal;
        }
        else
        {
            for ( int oldIndex = 0; oldIndex < oldCount; ++oldIndex )
            {
                if ( oldSelected[oldIndex] && HistogramOptionKeyMatches( oldHashes[oldIndex],
                                                                         oldFrameTotals[oldIndex],
                                                                         option.hash,
                                                                         option.isFrameTotal ) )
                {
                    state.histogramOptionSelected[i] = true;
                    break;
                }
            }
        }
        selectedAny = selectedAny || state.histogramOptionSelected[i];
    }

    if ( defaultSelection && !selectedAny && state.histogramOptionCount > 0 )
    {
        state.histogramOptionSelected[0] = true;
        selectedAny = true;
    }
    for ( int i = state.histogramOptionCount; i < HISTOGRAM_OPTION_CAPACITY; ++i )
    {
        state.histogramOptionHashes[i] = 0u;
        state.histogramOptionFrameTotals[i] = false;
        state.histogramOptionSelected[i] = false;
    }
    state.histogramSelectionInitialized = true;
    if ( cacheChanged )
    {
        // Invariant: histogram samples are indexed by cached option slot. Scene
        // startup can append or reorder marker options for several frames, so
        // preserve history by moving old slots to their new key instead of
        // clearing the ring.
        RemapHistogramSamples( state, oldHashes, oldFrameTotals, oldCount );
    }
    state.histogramSelectorScroll = std::clamp( state.histogramSelectorScroll, 0, HistogramMaxScroll( state ) );
}

float HistogramSampleMax( const SkullbonezCore::UI::ProfilerTab::PerformanceHistogramSample& sample,
                          const SkullbonezCore::UI::ProfilerTab::UIProfilerTabState& state )
{
    float maxMs = 0.0f;
    for ( int optionIndex = 0; optionIndex < state.histogramOptionCount; ++optionIndex )
    {
        if ( state.histogramOptionSelected[optionIndex] && sample.hasMarker[optionIndex] )
        {
            maxMs = (std::max)( maxMs, sample.markerMs[optionIndex] );
        }
    }
    return maxMs;
}

float NiceHistogramAxis( float rawMs )
{
    static constexpr float kAxisSteps[] = { 0.25f,
                                            0.50f,
                                            1.0f,
                                            2.0f,
                                            4.0f,
                                            8.0f,
                                            12.0f,
                                            16.67f,
                                            24.0f,
                                            HISTOGRAM_FRAME_CPU_DEFAULT_AXIS_MS,
                                            48.0f,
                                            64.0f,
                                            96.0f,
                                            128.0f,
                                            192.0f,
                                            250.0f };
    rawMs = std::clamp( rawMs, 0.25f, HISTOGRAM_SAMPLE_CLAMP_MS );
    for ( float step : kAxisSteps )
    {
        if ( step >= rawMs )
        {
            return step;
        }
    }
    return HISTOGRAM_SAMPLE_CLAMP_MS;
}

void FormatHistogramMsLabel( char* out, std::size_t outSize, float ms )
{
    if ( !out || outSize == 0 )
    {
        return;
    }

    const float rounded = std::round( ms );
    if ( std::fabs( ms - rounded ) < 0.005f )
    {
        snprintf( out, outSize, "%.0f ms", rounded );
        return;
    }

    const float tenth = std::round( ms * 10.0f ) / 10.0f;
    if ( std::fabs( ms - tenth ) < 0.005f )
    {
        snprintf( out, outSize, "%.1f ms", tenth );
        return;
    }

    snprintf( out, outSize, "%.2f ms", ms );
}

void DrawHistogramLineSegment( const SkullbonezCore::UI::UIDrawContext& draw,
                               float x0,
                               float y0,
                               float x1,
                               float y1,
                               float thickness,
                               float r,
                               float g,
                               float b,
                               float a )
{
    const float dx = x1 - x0;
    const float dy = y1 - y0;
    const float len = std::sqrt( dx * dx + dy * dy );
    if ( len <= 0.001f )
    {
        const float half = thickness * 0.5f;
        draw.Rect( x0 - half, y0 - half, thickness, thickness, r, g, b, a );
        return;
    }

    const float half = thickness * 0.5f;
    const float nx = -dy / len * half;
    const float ny = dx / len * half;
    draw.Triangle( x0 + nx, y0 + ny, x1 + nx, y1 + ny, x1 - nx, y1 - ny, r, g, b, a );
    draw.Triangle( x0 + nx, y0 + ny, x1 - nx, y1 - ny, x0 - nx, y0 - ny, r, g, b, a );
}

void FitHistogramText( char* text, std::size_t textSize, float pxSize, float maxWidth )
{
    if ( !text || textSize == 0 || maxWidth <= 0.0f )
    {
        return;
    }
    if ( SkullbonezCore::Text::Text2d::MeasureText( pxSize, text ) <= maxWidth )
    {
        return;
    }

    std::size_t len = strlen( text );
    while ( len > 4 )
    {
        --len;
        text[len] = '\0';
        if ( len > 3 )
        {
            text[len - 3] = '.';
            text[len - 2] = '.';
            text[len - 1] = '.';
        }
        if ( SkullbonezCore::Text::Text2d::MeasureText( pxSize, text ) <= maxWidth )
        {
            return;
        }
        if ( len > 3 )
        {
            text[len - 3] = '\0';
        }
    }
}

const char* HistogramOptionDisplayName( const SkullbonezCore::UI::UIProfilerMarkerOption& option )
{
    if ( option.isFrameTotal )
    {
        return "Main";
    }
    if ( option.name && option.name[0] != '\0' )
    {
        return option.name;
    }
    return option.leafName && option.leafName[0] != '\0' ? option.leafName : "Marker";
}

void HistogramOptionColor( const SkullbonezCore::UI::UIProfilerMarkerOption& option,
                           const SkullbonezCore::UI::Style::UIPalette& palette,
                           float& r,
                           float& g,
                           float& b )
{
    const bool hasPayloadColor = option.colorR > 0.0f || option.colorG > 0.0f || option.colorB > 0.0f;
    if ( hasPayloadColor )
    {
        r = option.colorR;
        g = option.colorG;
        b = option.colorB;
        return;
    }
    r = palette.accent.r;
    g = palette.accent.g;
    b = palette.accent.b;
}

void FormatHistogramSelectionText( const SkullbonezCore::UI::ProfilerTab::UIProfilerTabState& state,
                                   const SkullbonezCore::UI::InGameUIFrameData& data,
                                   char* out,
                                   std::size_t outSize )
{
    if ( !out || outSize == 0 )
    {
        return;
    }

    const int optionCount = (std::min)( state.histogramOptionCount, data.profilerMarkerOptionCount );
    int selectedCount = 0;
    bool mainSelected = false;
    const char* firstSelectedName = nullptr;
    for ( int optionIndex = 0; optionIndex < optionCount; ++optionIndex )
    {
        if ( !state.histogramOptionSelected[optionIndex] )
        {
            continue;
        }
        const SkullbonezCore::UI::UIProfilerMarkerOption& option = data.profilerMarkerOptions[optionIndex];
        if ( !firstSelectedName )
        {
            firstSelectedName = HistogramOptionDisplayName( option );
        }
        mainSelected = mainSelected || option.isFrameTotal;
        ++selectedCount;
    }

    if ( selectedCount <= 0 )
    {
        snprintf( out, outSize, "Select markers" );
    }
    else if ( selectedCount == 1 )
    {
        snprintf( out, outSize, "%s", firstSelectedName ? firstSelectedName : "Marker" );
    }
    else if ( mainSelected )
    {
        snprintf( out, outSize, "Main + %d %s", selectedCount - 1, selectedCount == 2 ? "marker" : "markers" );
    }
    else
    {
        snprintf( out, outSize, "%d markers", selectedCount );
    }
}

void DrawHistogramCheckbox( const SkullbonezCore::UI::UIDrawContext& draw,
                            const SkullbonezCore::UI::Style::UIPalette& palette,
                            float x,
                            float y,
                            bool selected,
                            float r,
                            float g,
                            float b )
{
    draw.RoundedRect( x, y, 11.0f, 11.0f, 2.0f, palette.control.r, palette.control.g, palette.control.b, 0.88f );
    draw.Outline( x, y, 11.0f, 11.0f, palette.innerBorder.r, palette.innerBorder.g, palette.innerBorder.b, 0.80f );
    if ( !selected )
    {
        return;
    }

    draw.Rect( x + 2.0f, y + 2.0f, 7.0f, 7.0f, r, g, b, 0.86f );
    DrawHistogramLineSegment( draw,
                              x + 3.0f,
                              y + 6.0f,
                              x + 5.0f,
                              y + 8.0f,
                              1.4f,
                              palette.textPrimary.r,
                              palette.textPrimary.g,
                              palette.textPrimary.b,
                              0.95f );
    DrawHistogramLineSegment( draw,
                              x + 5.0f,
                              y + 8.0f,
                              x + 9.0f,
                              y + 3.0f,
                              1.4f,
                              palette.textPrimary.r,
                              palette.textPrimary.g,
                              palette.textPrimary.b,
                              0.95f );
}

} // namespace

namespace SkullbonezCore
{
namespace UI
{
namespace ProfilerTab
{

bool TimelineEnabled( const UIProfilerTabState& state )
{
    return state.timelineEnabled;
}


bool PerformanceHistogramEnabled( const UIProfilerTabState& state )
{
    return state.performanceHistogramEnabled;
}


void SetFrameSnapshot( UIProfilerTabState& state, const FrameSnapshot& frame )
{
    state.frame = frame;
}


void SetExpandAll( UIProfilerTabState& state, bool expandAll )
{
    state.expandAllMarkers = expandAll;
    state.expandedHashCount = 0;
    state.drawExpandedHashCount = 0;
    state.defaultExpansionApplied = false;
    state.drawDefaultExpansionApplied = false;
    if ( expandAll )
    {
        ApplyExpandAll( state );
    }
}


void SetTimelineEnabled( UIProfilerTabState& state, bool enabled )
{
    state.timelineEnabled = enabled;
}


void SetPerformanceHistogramEnabled( UIProfilerTabState& state, bool enabled )
{
    state.performanceHistogramEnabled = enabled;
    if ( !enabled )
    {
        state.histogramDragging = false;
        state.histogramResizing = false;
        state.histogramSelectorOpen = false;
        ClearHistogramSamples( state );
    }
}


bool PerformanceHistogramIsInteracting( const UIProfilerTabState& state )
{
    return state.histogramDragging || state.histogramResizing;
}


void CancelPerformanceHistogramInteraction( UIProfilerTabState& state )
{
    state.histogramDragging = false;
    state.histogramResizing = false;
    state.histogramSelectorOpen = false;
}


void ResetPreviewState( UIProfilerTabState& state )
{
    state.previewWorkerThreads = -1;
}


void ApplyDefaultExpansion( UIProfilerTabState& state )
{
    if ( state.defaultExpansionApplied && state.drawDefaultExpansionApplied )
    {
        return;
    }

    if ( !state.defaultExpansionApplied )
    {
        const FrameSnapshot& frame = state.frame;
        static constexpr uint32_t kFrameHash = ::HashStr( "Frame" );
        static constexpr uint32_t kUIHash = ::HashStr( "Frame/UI" );
        static constexpr uint32_t kPhysicsHash = ::HashStr( "Frame/Physics" );
        for ( int i = 0; i < frame.markerCount; ++i )
        {
            const MarkerSnapshot& marker = frame.markers[i];
            if ( ( marker.hash == kFrameHash || marker.hash == kUIHash || marker.hash == kPhysicsHash ) &&
                 ProfilerMarkerHasChildren( frame, i ) && !IsMarkerExpanded( state, marker.hash ) &&
                 state.expandedHashCount < MAX_MARKERS )
            {
                state.expandedHashes[state.expandedHashCount++] = marker.hash;
            }
        }
        state.defaultExpansionApplied = true;
    }

    if ( !state.drawDefaultExpansionApplied )
    {
        const DrawTraceSnapshot& snapshot = state.frame.drawTrace;
        if ( snapshot.nodeCount > 0 )
        {
            static constexpr uint32_t kFrameHash = ::HashStr( "Frame" );
            static constexpr uint32_t kRenderHash = ::HashStr( "Frame/Render" );
            static constexpr uint32_t kShadowsHash = ::HashStr( "Frame/Shadows" );
            static constexpr uint32_t kUIHash = ::HashStr( "Frame/UI" );
            for ( int i = 0; i < snapshot.nodeCount && state.drawExpandedHashCount < MAX_MARKERS; ++i )
            {
                const DrawTraceNodeSnapshot& node = snapshot.nodes[i];
                if ( ( node.hash == kFrameHash || node.hash == kRenderHash || node.hash == kShadowsHash ||
                       node.hash == kUIHash ) &&
                     DrawNodeHasVisibleChildren( snapshot, i ) && !IsDrawNodeExpanded( state, node.hash ) )
                {
                    state.drawExpandedHashes[state.drawExpandedHashCount++] = node.hash;
                }
            }
            state.drawDefaultExpansionApplied = true;
        }
    }
}


void ApplyExpandAll( UIProfilerTabState& state )
{
    if ( !state.expandAllMarkers )
    {
        return;
    }

    const FrameSnapshot& frame = state.frame;
    for ( int i = 0; i < frame.markerCount; ++i )
    {
        const MarkerSnapshot& marker = frame.markers[i];
        if ( ProfilerMarkerHasChildren( frame, i ) && !IsMarkerExpanded( state, marker.hash ) &&
             state.expandedHashCount < MAX_MARKERS )
        {
            state.expandedHashes[state.expandedHashCount++] = marker.hash;
        }
    }

    const DrawTraceSnapshot& snapshot = frame.drawTrace;
    for ( int i = 0; i < snapshot.nodeCount; ++i )
    {
        const DrawTraceNodeSnapshot& node = snapshot.nodes[i];
        if ( node.drawCallCount > 0 && DrawNodeHasVisibleChildren( snapshot, i ) &&
             !IsDrawNodeExpanded( state, node.hash ) && state.drawExpandedHashCount < MAX_MARKERS )
        {
            state.drawExpandedHashes[state.drawExpandedHashCount++] = node.hash;
        }
    }
}


int ContentHeight( const UIProfilerTabState& state )
{
    int visibleRows[MAX_MARKERS] = {};
    const int visibleMarkerCount = BuildVisibleRows( state, visibleRows, MAX_MARKERS );
    const DrawTraceSnapshot& snapshot = state.frame.drawTrace;
    int visibleDrawRows[MAX_MARKERS] = {};
    const int visibleDrawRowCount = BuildVisibleDrawRows( state, snapshot, visibleDrawRows, MAX_MARKERS );
    const int drawSectionHeight =
        visibleDrawRowCount > 0 ? 50 + static_cast<int>( PROFILER_CORE_CHART_H ) + visibleDrawRowCount * 26 : 0;
    return static_cast<int>( PROFILER_TABLE_OFFSET_H ) + 54 + visibleMarkerCount * 30 + drawSectionHeight;
}


bool HandleContentClick( UIProfilerTabState& state,
                         InGameUIInputResult& result,
                         int& activeSlider,
                         int contentX,
                         int contentY,
                         float contentW,
                         float scrollY,
                         int mouseX,
                         int mouseY,
                         int currentWorkerThreads,
                         int maxWorkerThreads )
{
    // Concept: The profiler tab owns UI expansion and slider preview state, but
    // worker-thread changes are returned as commands for runtime code to apply.
    SetProfilerContentBounds( state, static_cast<float>( contentX ), static_cast<float>( contentY ), contentW );
    const int workerMax = (std::max)( 1, maxWorkerThreads );
    const int workerCount = std::clamp( currentWorkerThreads, 0, workerMax );
    if ( state.workerToggle.HitTest( mouseX, mouseY ) )
    {
        if ( workerCount > 0 )
        {
            state.restoreWorkerThreads = workerCount;
            state.previewWorkerThreads = 0;
            result.commands.profiler.requestedWorkerThreads = 0;
        }
        else
        {
            const int restoredWorkerThreads =
                state.restoreWorkerThreads > 0 ? std::clamp( state.restoreWorkerThreads, 1, workerMax ) : -1;
            state.previewWorkerThreads = restoredWorkerThreads > 0 ? restoredWorkerThreads : -1;
            result.commands.profiler.requestedWorkerThreads = restoredWorkerThreads;
        }
        return true;
    }
    if ( state.workerThreadSlider.HitTest( mouseX, mouseY ) )
    {
        activeSlider = SLIDER_WORKER_THREADS;
        state.previewWorkerThreads = static_cast<int>(
            state.workerThreadSlider.ValueFromMouse( mouseX, 0.0f, static_cast<float>( workerMax ), 1.0f ) );
        return true;
    }

    const int headerH = 32;
    const int rowH = 30;
    const int localY = static_cast<int>( static_cast<float>( mouseY - contentY ) + scrollY - PROFILER_TABLE_OFFSET_H );
    if ( localY < headerH )
    {
        return false;
    }

    int visibleRows[MAX_MARKERS] = {};
    const int visibleRowCount = BuildVisibleRows( state, visibleRows, MAX_MARKERS );
    const int targetRow = ( localY - headerH ) / rowH;
    if ( targetRow >= 0 && targetRow < visibleRowCount )
    {
        const int markerIndex = visibleRows[targetRow];
        const MarkerSnapshot& marker = state.frame.markers[markerIndex];
        if ( !ProfilerMarkerHasChildren( state.frame, markerIndex ) )
        {
            return false;
        }

        const float plusX = static_cast<float>( contentX + 18 + marker.depth * 18 );
        const float plusY = static_cast<float>( contentY ) + PROFILER_TABLE_OFFSET_H +
                            static_cast<float>( headerH + targetRow * rowH ) - scrollY + 8.0f;
        UIIconButton expander;
        expander.SetBounds( plusX, plusY, 14.0f, 14.0f );
        if ( !expander.HitTest( mouseX, mouseY ) )
        {
            return false;
        }

        ToggleMarker( state, marker.hash );
        return true;
    }

    const int drawHeaderTop = headerH + visibleRowCount * rowH + 18;
    const int drawHeaderH = 32;
    const int coreChartH = static_cast<int>( PROFILER_CORE_CHART_H );
    const int drawRowH = 26;
    const int drawLocalY = localY - drawHeaderTop;
    if ( drawLocalY < drawHeaderH + coreChartH )
    {
        return false;
    }
    const int drawTargetRow = ( drawLocalY - drawHeaderH - coreChartH ) / drawRowH;
    const DrawTraceSnapshot& snapshot = state.frame.drawTrace;
    int visibleDrawRows[MAX_MARKERS] = {};
    const int visibleDrawRowCount = BuildVisibleDrawRows( state, snapshot, visibleDrawRows, MAX_MARKERS );
    if ( drawTargetRow < 0 || drawTargetRow >= visibleDrawRowCount )
    {
        return false;
    }

    const int nodeIndex = visibleDrawRows[drawTargetRow];
    if ( !DrawNodeHasVisibleChildren( snapshot, nodeIndex ) )
    {
        return false;
    }

    const DrawTraceNodeSnapshot& node = snapshot.nodes[nodeIndex];
    const float plusX = static_cast<float>( contentX + 18 + node.depth * 18 );
    const float plusY = static_cast<float>( contentY ) + PROFILER_TABLE_OFFSET_H +
                        static_cast<float>( drawHeaderTop + drawHeaderH + coreChartH + drawTargetRow * drawRowH ) -
                        scrollY + 6.0f;
    UIIconButton expander;
    expander.SetBounds( plusX, plusY, 14.0f, 14.0f );
    if ( !expander.HitTest( mouseX, mouseY ) )
    {
        return false;
    }

    ToggleDrawNode( state, node.hash );
    return true;
}


bool UpdateActiveSlider( UIProfilerTabState& state,
                         int activeSlider,
                         int mouseX,
                         int maxWorkerThreads,
                         InGameUIInputResult& result )
{
    static_cast<void>( result );
    if ( activeSlider != SLIDER_WORKER_THREADS )
    {
        return false;
    }

    const int workerMax = (std::max)( 1, maxWorkerThreads );
    state.previewWorkerThreads = static_cast<int>(
        state.workerThreadSlider.ValueFromMouse( mouseX, 0.0f, static_cast<float>( workerMax ), 1.0f ) );
    return true;
}


bool CommitActiveSlider( UIProfilerTabState& state, int activeSlider, InGameUIInputResult& result )
{
    if ( activeSlider != SLIDER_WORKER_THREADS || state.previewWorkerThreads < 0 )
    {
        return false;
    }

    result.commands.profiler.requestedWorkerThreads = state.previewWorkerThreads;
    return true;
}


bool HandlePerformanceHistogramInput( UIProfilerTabState& state,
                                      InGameUIInputResult& result,
                                      int screenW,
                                      int screenH,
                                      int mouseX,
                                      int mouseY,
                                      bool leftDown,
                                      bool leftPressed,
                                      bool leftReleased,
                                      int wheelDelta )
{
    if ( !state.performanceHistogramEnabled )
    {
        return false;
    }

    ClampHistogramPanelToScreen( state, screenW, screenH );
    const UIRect panel = HistogramPanelBounds( state );
    const UIRect header = HistogramHeaderBounds( state );
    const UIRect selector = HistogramSelectorBounds( state );
    const UIRect resize = HistogramResizeBounds( state );
    const UIRect dropdown = HistogramDropdownBounds( state, screenH );
    const bool dropdownOpen = state.histogramSelectorOpen && state.histogramOptionCount > 0;
    const bool insidePanel = panel.Contains( mouseX, mouseY );
    const bool insideDropdown = dropdownOpen && dropdown.Contains( mouseX, mouseY );
    bool handled = insidePanel || insideDropdown || state.histogramDragging || state.histogramResizing;

    if ( wheelDelta != 0 && dropdownOpen && ( selector.Contains( mouseX, mouseY ) || insideDropdown ) )
    {
        const int wheelSteps = wheelDelta / 120;
        state.histogramSelectorScroll =
            std::clamp( state.histogramSelectorScroll - wheelSteps, 0, HistogramMaxScroll( state ) );
        result.unhandledWheelDelta = 0;
        result.commands.ui.userInteracted = true;
        handled = true;
    }

    if ( leftPressed )
    {
        if ( dropdownOpen )
        {
            const int optionIndex = HitHistogramDropdownOption( state, screenH, mouseX, mouseY );
            if ( optionIndex >= 0 )
            {
                ToggleHistogramSelectionFromCache( state, optionIndex );
                result.commands.ui.userInteracted = true;
                handled = true;
            }
            else if ( selector.Contains( mouseX, mouseY ) )
            {
                state.histogramSelectorOpen = false;
                result.commands.ui.userInteracted = true;
                handled = true;
            }
            else if ( !insidePanel && !insideDropdown )
            {
                state.histogramSelectorOpen = false;
            }
            else
            {
                result.commands.ui.userInteracted = true;
                handled = true;
            }
        }
        else if ( selector.Contains( mouseX, mouseY ) )
        {
            state.histogramSelectorOpen = true;
            const int selectedIndex = FindCachedHistogramSelectionIndex( state );
            const int visibleRows = HistogramVisibleDropdownRows( state.histogramOptionCount );
            state.histogramSelectorScroll =
                std::clamp( selectedIndex - visibleRows / 2, 0, HistogramMaxScroll( state ) );
            result.commands.ui.userInteracted = true;
            handled = true;
        }
        else if ( resize.Contains( mouseX, mouseY ) )
        {
            state.histogramResizing = true;
            state.histogramResizeStartMouseX = mouseX;
            state.histogramResizeStartMouseY = mouseY;
            state.histogramResizeStartW = state.histogramPanelW;
            state.histogramResizeStartH = state.histogramPanelH;
            state.histogramSelectorOpen = false;
            result.commands.ui.userInteracted = true;
            handled = true;
        }
        else if ( header.Contains( mouseX, mouseY ) )
        {
            state.histogramDragging = true;
            state.histogramDragOffsetX = mouseX - static_cast<int>( std::round( state.histogramPanelX ) );
            state.histogramDragOffsetY = mouseY - static_cast<int>( std::round( state.histogramPanelY ) );
            state.histogramSelectorOpen = false;
            result.commands.ui.userInteracted = true;
            handled = true;
        }
        else if ( insidePanel )
        {
            state.histogramSelectorOpen = false;
            result.commands.ui.userInteracted = true;
            handled = true;
        }
    }

    if ( leftDown && state.histogramDragging )
    {
        state.histogramPanelX = static_cast<float>( mouseX - state.histogramDragOffsetX );
        state.histogramPanelY = static_cast<float>( mouseY - state.histogramDragOffsetY );
        ClampHistogramPanelToScreen( state, screenW, screenH );
        result.commands.ui.userInteracted = true;
        handled = true;
    }
    if ( leftDown && state.histogramResizing )
    {
        state.histogramPanelW =
            state.histogramResizeStartW + static_cast<float>( mouseX - state.histogramResizeStartMouseX );
        state.histogramPanelH =
            state.histogramResizeStartH + static_cast<float>( mouseY - state.histogramResizeStartMouseY );
        ClampHistogramPanelToScreen( state, screenW, screenH );
        result.commands.ui.userInteracted = true;
        handled = true;
    }

    if ( leftReleased )
    {
        if ( state.histogramDragging || state.histogramResizing )
        {
            result.commands.ui.userInteracted = true;
            handled = true;
        }
        state.histogramDragging = false;
        state.histogramResizing = false;
    }

    if ( handled )
    {
        result.unhandledWheelDelta = 0;
    }
    return handled;
}


void PushPerformanceHistogramSample( UIProfilerTabState& state, const InGameUIFrameData& data )
{
    CacheHistogramOptions( state, data );
    if ( !HistogramAnyOptionSelected( state ) )
    {
        return;
    }

    float previousMaxMs[HISTOGRAM_OPTION_CAPACITY] = {};
    for ( int i = 0; i < state.histogramCount; ++i )
    {
        const int sampleIndex =
            ( state.histogramHead - state.histogramCount + i + HISTOGRAM_SAMPLE_COUNT ) % HISTOGRAM_SAMPLE_COUNT;
        const PerformanceHistogramSample& sample = state.histogramSamples[sampleIndex];
        for ( int optionIndex = 0; optionIndex < state.histogramOptionCount; ++optionIndex )
        {
            if ( state.histogramOptionSelected[optionIndex] && sample.hasMarker[optionIndex] )
            {
                previousMaxMs[optionIndex] = (std::max)( previousMaxMs[optionIndex], sample.markerMs[optionIndex] );
            }
        }
    }

    const bool mainSelected = HistogramMainSelected( state );
    const int optionCount = (std::min)( state.histogramOptionCount, data.profilerMarkerOptionCount );
    PerformanceHistogramSample& writeSample = state.histogramSamples[state.histogramHead];
    writeSample = {};
    bool wroteMarker = false;
    for ( int optionIndex = 0; optionIndex < optionCount; ++optionIndex )
    {
        if ( !state.histogramOptionSelected[optionIndex] )
        {
            continue;
        }
        const UIProfilerMarkerOption& option = data.profilerMarkerOptions[optionIndex];
        if ( !option.sampleValid )
        {
            continue;
        }

        // Invariant: marker histories are stored by cached option slot. Main
        // keeps the fixed frame-budget axis, while non-main selections can use
        // their own dynamic scale without changing the ring layout.
        const float markerMs = std::clamp( option.cpuMs, 0.0f, HISTOGRAM_SAMPLE_CLAMP_MS );
        writeSample.markerMs[optionIndex] = markerMs;
        writeSample.hasMarker[optionIndex] = true;
        wroteMarker = true;

        const float axisSpikeThreshold = mainSelected && !option.isFrameTotal ? 0.10f : state.histogramAxisMs * 0.92f;
        if ( state.histogramCount > 8 && markerMs > 0.10f &&
             markerMs > (std::max)( previousMaxMs[optionIndex] * 1.20f, axisSpikeThreshold ) )
        {
            writeSample.markerSpikeMs[optionIndex] = markerMs;
        }
    }
    if ( !wroteMarker )
    {
        return;
    }

    if ( mainSelected && data.workerCoreTotalMs > 0.0f )
    {
        writeSample.secondaryMs = std::clamp( data.workerCoreTotalMs, 0.0f, HISTOGRAM_SAMPLE_CLAMP_MS );
        writeSample.hasSecondary = true;
    }

    state.histogramHead = ( state.histogramHead + 1 ) % HISTOGRAM_SAMPLE_COUNT;
    if ( state.histogramCount < HISTOGRAM_SAMPLE_COUNT )
    {
        ++state.histogramCount;
    }

    float visibleMaxMs = 0.0f;
    for ( int i = 0; i < state.histogramCount; ++i )
    {
        const int sampleIndex =
            ( state.histogramHead - state.histogramCount + i + HISTOGRAM_SAMPLE_COUNT ) % HISTOGRAM_SAMPLE_COUNT;
        visibleMaxMs = (std::max)( visibleMaxMs, HistogramSampleMax( state.histogramSamples[sampleIndex], state ) );
    }

    const float minimumAxisMs = HistogramMinimumAxisMs( state );
    float targetAxisMs = mainSelected ? HISTOGRAM_FRAME_CPU_DEFAULT_AXIS_MS : minimumAxisMs;
    if ( !mainSelected && visibleMaxMs > minimumAxisMs )
    {
        targetAxisMs = NiceHistogramAxis( visibleMaxMs * 1.18f );
    }
    if ( mainSelected )
    {
        state.histogramAxisMs = targetAxisMs;
    }
    else if ( targetAxisMs > state.histogramAxisMs )
    {
        state.histogramAxisMs = targetAxisMs;
    }
    else
    {
        state.histogramAxisMs += ( targetAxisMs - state.histogramAxisMs ) * 0.075f;
        if ( state.histogramAxisMs < minimumAxisMs )
        {
            state.histogramAxisMs = minimumAxisMs;
        }
    }
}


void DrawPerformanceHistogram( UIProfilerTabState& state, const UIDrawContext& draw, const InGameUIFrameData& data )
{
    ClampHistogramPanelToScreen( state, data.screenW, data.screenH );
    CacheHistogramOptions( state, data );

    const UIRect panel = HistogramPanelBounds( state );
    const UIRect selector = HistogramSelectorBounds( state );
    const UIRect plot = HistogramPlotBounds( state );
    const UIRect resize = HistogramResizeBounds( state );
    const float baseY = plot.y + plot.h;
    const float axisMs = (std::max)( 0.25f, state.histogramAxisMs );
    const bool mainSelected = HistogramMainSelected( state );
    const bool anySelection = HistogramAnyOptionSelected( state );
    const int selectedCount = HistogramSelectedOptionCount( state );
    const int optionCount = (std::min)( state.histogramOptionCount, data.profilerMarkerOptionCount );
    // Why: text rendering is batched separately from filled panels. While the
    // dropdown is open, hide chart-side labels that would otherwise draw above it.
    const bool dropdownOpen = state.histogramSelectorOpen && state.histogramOptionCount > 0;

    const Style::UIPalette& palette = Style::Palette();
    draw.RoundedRect( panel.x + 4.0f,
                      panel.y + 5.0f,
                      panel.w,
                      panel.h,
                      Style::Radii().window,
                      0.0f,
                      0.0f,
                      0.0f,
                      0.22f );
    draw.RoundedPanel( panel, Style::Radii().window, palette.windowSubtle, palette.border );

    char text[128] = {};
    draw.Text( panel.x + 10.0f,
               panel.y + 8.0f,
               10.5f,
               palette.textPrimary.r,
               palette.textPrimary.g,
               palette.textPrimary.b,
               "Marker History" );
    snprintf( text, sizeof( text ), mainSelected ? "CPU + WORK" : "CPU" );
    draw.Text( panel.x + panel.w - 10.0f - SkullbonezCore::Text::Text2d::MeasureText( 9.6f, text ),
               panel.y + 8.0f,
               9.6f,
               palette.textSecondary.r,
               palette.textSecondary.g,
               palette.textSecondary.b,
               text );

    draw.RoundedRect( selector.x,
                      selector.y,
                      selector.w,
                      selector.h,
                      Style::Radii().smallButton,
                      palette.control.r,
                      palette.control.g,
                      palette.control.b,
                      0.82f );
    draw.Outline( selector.x,
                  selector.y,
                  selector.w,
                  selector.h,
                  palette.innerBorder.r,
                  palette.innerBorder.g,
                  palette.innerBorder.b,
                  0.76f );
    FormatHistogramSelectionText( state, data, text, sizeof( text ) );
    FitHistogramText( text, sizeof( text ), 10.0f, selector.w - 26.0f );
    draw.Text( selector.x + 9.0f,
               selector.y + 6.0f,
               10.0f,
               palette.textPrimary.r,
               palette.textPrimary.g,
               palette.textPrimary.b,
               text );
    draw.Triangle( selector.x + selector.w - 15.0f,
                   selector.y + 9.0f,
                   selector.x + selector.w - 7.0f,
                   selector.y + 9.0f,
                   selector.x + selector.w - 11.0f,
                   selector.y + 15.0f,
                   palette.textSecondary.r,
                   palette.textSecondary.g,
                   palette.textSecondary.b,
                   0.88f );

    draw.Rect( plot.x, plot.y, plot.w, plot.h, palette.window.r, palette.window.g, palette.window.b, 0.58f );
    const float budgetY = mainSelected
                              ? baseY - std::clamp( HISTOGRAM_FRAME_CPU_BUDGET_MS / axisMs, 0.0f, 1.0f ) * plot.h
                              : plot.y + plot.h * 0.50f;
    draw.Rect( plot.x,
               budgetY,
               plot.w,
               1.0f,
               mainSelected ? palette.warningAccent.r : palette.lineSoft.r,
               mainSelected ? palette.warningAccent.g : palette.lineSoft.g,
               mainSelected ? palette.warningAccent.b : palette.lineSoft.b,
               mainSelected ? 0.58f : 0.14f );
    draw.Rect( plot.x, plot.y, plot.w, 1.0f, palette.lineSoft.r, palette.lineSoft.g, palette.lineSoft.b, 0.18f );
    draw.Rect( plot.x, plot.y, 1.0f, plot.h, palette.lineSoft.r, palette.lineSoft.g, palette.lineSoft.b, 0.28f );
    draw.Rect( plot.x, baseY, plot.w, 1.0f, palette.accent.r, palette.accent.g, palette.accent.b, 0.34f );

    auto drawAxisLabel = [&]( float y, float ms )
    {
        FormatHistogramMsLabel( text, sizeof( text ), ms );
        const float textW = SkullbonezCore::Text::Text2d::MeasureText( 8.8f, text );
        draw.Text( plot.x - 6.0f - textW,
                   y,
                   8.8f,
                   palette.textSecondary.r,
                   palette.textSecondary.g,
                   palette.textSecondary.b,
                   text );
    };
    if ( !dropdownOpen )
    {
        drawAxisLabel( plot.y + 2.0f, axisMs );
        drawAxisLabel( budgetY - 5.0f, mainSelected ? HISTOGRAM_FRAME_CPU_BUDGET_MS : axisMs * 0.50f );
    }

    if ( !dropdownOpen && ( state.histogramCount <= 0 || !anySelection ) )
    {
        draw.Text( plot.x + 10.0f,
                   plot.y + plot.h * 0.5f - 6.0f,
                   10.5f,
                   palette.textMuted.r,
                   palette.textMuted.g,
                   palette.textMuted.b,
                   anySelection ? "Waiting for samples" : "Select markers" );
    }

    const float step = plot.w / static_cast<float>( HISTOGRAM_SAMPLE_COUNT );
    constexpr float workerLineR = 0.42f;
    constexpr float workerLineG = 0.83f;
    constexpr float workerLineB = 1.00f;

    struct SpikeLabel
    {
        float x = -1.0f;
        float y = 0.0f;
        float ms = 0.0f;
        float r = 0.0f;
        float g = 0.0f;
        float b = 0.0f;
    };
    SpikeLabel spikeLabels[HISTOGRAM_OPTION_CAPACITY] = {};
    int spikeLabelCount = 0;

    // Concept: this is a CPU history line chart. Checked marker rows each get a
    // colored series; the optional light-blue line is worker-core CPU work, not
    // GPU timing, and appears only while Main is checked.
    for ( int optionIndex = 0; optionIndex < optionCount; ++optionIndex )
    {
        if ( !state.histogramOptionSelected[optionIndex] )
        {
            continue;
        }
        const UIProfilerMarkerOption& seriesOption = data.profilerMarkerOptions[optionIndex];
        float seriesR = 0.0f;
        float seriesG = 0.0f;
        float seriesB = 0.0f;
        HistogramOptionColor( seriesOption, palette, seriesR, seriesG, seriesB );

        float previousX = 0.0f;
        float previousY = 0.0f;
        bool previousValid = false;
        SpikeLabel seriesSpike;
        seriesSpike.r = seriesR;
        seriesSpike.g = seriesG;
        seriesSpike.b = seriesB;
        for ( int i = 0; i < state.histogramCount; ++i )
        {
            const int sampleIndex =
                ( state.histogramHead - state.histogramCount + i + HISTOGRAM_SAMPLE_COUNT ) % HISTOGRAM_SAMPLE_COUNT;
            const PerformanceHistogramSample& sample = state.histogramSamples[sampleIndex];
            if ( !sample.hasMarker[optionIndex] )
            {
                previousValid = false;
                continue;
            }

            const float x =
                plot.x + ( static_cast<float>( HISTOGRAM_SAMPLE_COUNT - state.histogramCount + i ) + 0.5f ) * step;
            const float markerY = baseY - std::clamp( sample.markerMs[optionIndex] / axisMs, 0.0f, 1.0f ) * plot.h;
            if ( previousValid )
            {
                DrawHistogramLineSegment( draw,
                                          previousX,
                                          previousY,
                                          x,
                                          markerY,
                                          2.0f,
                                          seriesR,
                                          seriesG,
                                          seriesB,
                                          0.86f );
            }
            draw.Rect( x - 1.0f, markerY - 1.0f, 2.0f, 2.0f, seriesR, seriesG, seriesB, 0.82f );
            previousX = x;
            previousY = markerY;
            previousValid = true;

            if ( sample.markerSpikeMs[optionIndex] > seriesSpike.ms )
            {
                seriesSpike.ms = sample.markerSpikeMs[optionIndex];
                seriesSpike.x = x;
                seriesSpike.y = baseY - std::clamp( sample.markerSpikeMs[optionIndex] / axisMs, 0.0f, 1.0f ) * plot.h;
            }
        }
        if ( seriesSpike.ms > 0.0f && seriesSpike.x >= plot.x && spikeLabelCount < HISTOGRAM_OPTION_CAPACITY )
        {
            spikeLabels[spikeLabelCount++] = seriesSpike;
        }
    }

    if ( mainSelected )
    {
        float previousWorkerX = 0.0f;
        float previousWorkerY = 0.0f;
        bool previousWorkerValid = false;
        for ( int i = 0; i < state.histogramCount; ++i )
        {
            const int sampleIndex =
                ( state.histogramHead - state.histogramCount + i + HISTOGRAM_SAMPLE_COUNT ) % HISTOGRAM_SAMPLE_COUNT;
            const PerformanceHistogramSample& sample = state.histogramSamples[sampleIndex];
            if ( !sample.hasSecondary )
            {
                previousWorkerValid = false;
                continue;
            }

            const float x =
                plot.x + ( static_cast<float>( HISTOGRAM_SAMPLE_COUNT - state.histogramCount + i ) + 0.5f ) * step;
            const float workerY = baseY - std::clamp( sample.secondaryMs / axisMs, 0.0f, 1.0f ) * plot.h;
            if ( previousWorkerValid )
            {
                DrawHistogramLineSegment( draw,
                                          previousWorkerX,
                                          previousWorkerY,
                                          x,
                                          workerY,
                                          2.0f,
                                          workerLineR,
                                          workerLineG,
                                          workerLineB,
                                          0.88f );
            }
            draw.Rect( x - 1.0f, workerY - 1.0f, 2.0f, 2.0f, workerLineR, workerLineG, workerLineB, 0.82f );
            previousWorkerX = x;
            previousWorkerY = workerY;
            previousWorkerValid = true;
        }
    }

    if ( !dropdownOpen )
    {
        for ( int spikeIndex = 0; spikeIndex < spikeLabelCount; ++spikeIndex )
        {
            const SpikeLabel& spike = spikeLabels[spikeIndex];
            snprintf( text, sizeof( text ), "%.2f ms", spike.ms );
            const float labelX = ( spike.x + 62.0f < panel.x + panel.w ) ? spike.x + 4.0f : spike.x - 62.0f;
            float labelY = (std::max)( plot.y + 2.0f, spike.y - 16.0f + static_cast<float>( spikeIndex % 3 ) * 10.0f );
            if ( labelY + 11.0f > baseY - 2.0f )
            {
                labelY = (std::max)( plot.y + 2.0f, baseY - 13.0f - static_cast<float>( spikeIndex % 3 ) * 10.0f );
            }
            draw.Rect( spike.x, plot.y, 1.0f, plot.h, spike.r, spike.g, spike.b, 0.48f );
            draw.Text( labelX, labelY, 9.5f, spike.r, spike.g, spike.b, text );
        }
    }

    if ( state.histogramCount > 0 && anySelection && !dropdownOpen )
    {
        float candidateCpuAverageMs = 0.0f;
        float candidateWorkerAverageMs = 0.0f;
        {
            float cpuSum = 0.0f;
            float workerSum = 0.0f;
            int cpuCount = 0;
            int workerCount = 0;
            for ( int i = 0; i < state.histogramCount; ++i )
            {
                const int sampleIndex = ( state.histogramHead - state.histogramCount + i + HISTOGRAM_SAMPLE_COUNT ) %
                                        HISTOGRAM_SAMPLE_COUNT;
                const PerformanceHistogramSample& sample = state.histogramSamples[sampleIndex];
                for ( int optionIndex = 0; optionIndex < optionCount; ++optionIndex )
                {
                    if ( state.histogramOptionSelected[optionIndex] && sample.hasMarker[optionIndex] )
                    {
                        cpuSum += sample.markerMs[optionIndex];
                        ++cpuCount;
                    }
                }
                if ( mainSelected && sample.hasSecondary )
                {
                    workerSum += sample.secondaryMs;
                    ++workerCount;
                }
            }
            candidateCpuAverageMs = cpuCount > 0 ? cpuSum / static_cast<float>( cpuCount ) : 0.0f;
            candidateWorkerAverageMs = workerCount > 0 ? workerSum / static_cast<float>( workerCount ) : 0.0f;
        }

        const bool averageBecameMeaningful = state.histogramAverageCpuMs <= 0.005f && candidateCpuAverageMs > 0.005f;
        const bool refreshAverageText =
            state.histogramCount >= 8 &&
            ( state.histogramAverageTextLastUpdateSeconds < 0.0 || averageBecameMeaningful ||
              data.now < state.histogramAverageTextLastUpdateSeconds ||
              data.now - state.histogramAverageTextLastUpdateSeconds >= HISTOGRAM_AVERAGE_TEXT_REFRESH_SECONDS );
        if ( refreshAverageText )
        {
            state.histogramAverageCpuMs = candidateCpuAverageMs;
            state.histogramAverageWorkerMs = candidateWorkerAverageMs;
            state.histogramAverageTextLastUpdateSeconds = data.now;
        }

        const float footerY = panel.y + panel.h - 20.0f;
        snprintf( text,
                  sizeof( text ),
                  selectedCount > 1 ? "Selected avg %.2f ms" : "CPU avg %.2f ms",
                  state.histogramAverageCpuMs );
        draw.Rect( panel.x + 10.0f,
                   footerY + 7.0f,
                   9.0f,
                   2.0f,
                   palette.accent.r,
                   palette.accent.g,
                   palette.accent.b,
                   0.86f );
        draw.Text( panel.x + 22.0f, footerY, 10.0f, palette.accent.r, palette.accent.g, palette.accent.b, text );
        if ( mainSelected && state.histogramAverageWorkerMs > 0.0f )
        {
            const float workerX = panel.x + 34.0f + SkullbonezCore::Text::Text2d::MeasureText( 10.0f, text );
            snprintf( text, sizeof( text ), "Other cores avg %.2f ms", state.histogramAverageWorkerMs );
            draw.Rect( workerX, footerY + 7.0f, 9.0f, 2.0f, workerLineR, workerLineG, workerLineB, 0.90f );
            draw.Text( workerX + 12.0f, footerY, 10.0f, workerLineR, workerLineG, workerLineB, text );
        }
    }

    draw.Rect( resize.x + 7.0f,
               resize.y + 16.0f,
               10.0f,
               1.0f,
               palette.textMuted.r,
               palette.textMuted.g,
               palette.textMuted.b,
               0.54f );
    draw.Rect( resize.x + 12.0f,
               resize.y + 11.0f,
               5.0f,
               1.0f,
               palette.textMuted.r,
               palette.textMuted.g,
               palette.textMuted.b,
               0.44f );

    if ( dropdownOpen )
    {
        const UIRect dropdown = HistogramDropdownBounds( state, data.screenH );
        Style::UIColor dropdownFill = palette.window;
        dropdownFill.a = 1.0f;
        draw.RoundedPanel( dropdown, Style::Radii().control, dropdownFill, palette.border );
        const int visibleRows = HistogramVisibleDropdownRows( state.histogramOptionCount );
        for ( int row = 0; row < visibleRows; ++row )
        {
            const int optionIndex = state.histogramSelectorScroll + row;
            if ( optionIndex < 0 || optionIndex >= optionCount )
            {
                continue;
            }
            const UIProfilerMarkerOption& rowOption = data.profilerMarkerOptions[optionIndex];
            const float rowY = dropdown.y + 2.0f + static_cast<float>( row ) * HISTOGRAM_DROPDOWN_ROW_H;
            const bool selected = state.histogramOptionSelected[optionIndex];
            float rowR = 0.0f;
            float rowG = 0.0f;
            float rowB = 0.0f;
            HistogramOptionColor( rowOption, palette, rowR, rowG, rowB );
            if ( selected )
            {
                draw.Rect( dropdown.x + 2.0f,
                           rowY,
                           dropdown.w - 4.0f,
                           HISTOGRAM_DROPDOWN_ROW_H,
                           palette.controlHover.r,
                           palette.controlHover.g,
                           palette.controlHover.b,
                           0.58f );
            }
            DrawHistogramCheckbox( draw, palette, dropdown.x + 8.0f, rowY + 5.0f, selected, rowR, rowG, rowB );
            snprintf( text, sizeof( text ), "%s", HistogramOptionDisplayName( rowOption ) );
            FitHistogramText( text, sizeof( text ), 9.4f, dropdown.w - 120.0f );
            draw.Text( dropdown.x + 39.0f,
                       rowY + 6.0f,
                       9.4f,
                       selected ? palette.textPrimary.r : palette.textSecondary.r,
                       selected ? palette.textPrimary.g : palette.textSecondary.g,
                       selected ? palette.textPrimary.b : palette.textSecondary.b,
                       text );
            snprintf( text,
                      sizeof( text ),
                      "%.3f",
                      rowOption.cpuAverageMs > 0.0f ? rowOption.cpuAverageMs : rowOption.cpuMs );
            draw.Text( dropdown.x + dropdown.w - 62.0f, rowY + 6.0f, 9.2f, rowR, rowG, rowB, text );
        }
        if ( state.histogramOptionCount > visibleRows )
        {
            const float footerY = dropdown.y + 2.0f + static_cast<float>( visibleRows ) * HISTOGRAM_DROPDOWN_ROW_H;
            draw.Rect( dropdown.x + 2.0f,
                       footerY,
                       dropdown.w - 4.0f,
                       1.0f,
                       palette.innerBorder.r,
                       palette.innerBorder.g,
                       palette.innerBorder.b,
                       0.78f );
            snprintf( text,
                      sizeof( text ),
                      "%d-%d/%d",
                      state.histogramSelectorScroll + 1,
                      state.histogramSelectorScroll + visibleRows,
                      state.histogramOptionCount );
            draw.Text( dropdown.x + dropdown.w - 54.0f,
                       footerY + 4.0f,
                       8.0f,
                       palette.textMuted.r,
                       palette.textMuted.g,
                       palette.textMuted.b,
                       text );
        }
    }
}

void Draw( UIProfilerTabState& state,
           const UIDrawContext& draw,
           const InGameUIFrameData& data,
           float contentX,
           float contentY,
           float contentW,
           float contentH,
           float scrollY,
           int activeSlider )
{
    char buf[128];
    const FrameSnapshot& frame = state.frame;
    const int workerMax = (std::max)( 1, data.maxWorkerThreadCount );
    const int rawWorkerCount = ( activeSlider == SLIDER_WORKER_THREADS && state.previewWorkerThreads >= 0 )
                                   ? state.previewWorkerThreads
                                   : data.workerThreadCount;
    const int displayWorkerCount = std::clamp( rawWorkerCount, 0, workerMax );
    SetProfilerContentBounds( state, contentX, contentY, contentW );
    if ( IsProfilerRowVisible( contentY, contentH, contentY + PROFILER_WORKER_TOGGLE_Y, 24.0f ) )
    {
        state.workerToggle.DrawToggle( draw, "Workers", displayWorkerCount > 0, 0.30f, 0.82f, 0.95f );
    }
    snprintf( buf, sizeof( buf ), "%d / %d", displayWorkerCount, workerMax );
    if ( IsProfilerRowVisible( contentY, contentH, contentY + PROFILER_WORKER_SLIDER_Y, 34.0f ) )
    {
        state.workerThreadSlider.Draw( draw,
                                       "Worker threads",
                                       buf,
                                       static_cast<float>( displayWorkerCount ),
                                       0.0f,
                                       static_cast<float>( workerMax ) );
    }

    const float tableX = contentX;
    const float tableY = contentY + PROFILER_TABLE_OFFSET_H;
    const float tableW = contentW;
    const float tableH = (std::max)( 0.0f, contentH - PROFILER_TABLE_OFFSET_H );
    if ( tableH <= 0.0f )
    {
        return;
    }
    const float rowH = 30.0f;
    const float headerH = 32.0f;
    const float colMarker = tableX + 18.0f;
    const float colCpu = tableX + tableW * 0.32f;
    const float colSelf = tableX + tableW * 0.42f;
    const float colP50 = tableX + tableW * 0.52f;
    const float colP99 = tableX + tableW * 0.62f;
    const float barX = tableX + tableW * 0.73f;
    const float barW = (std::max)( 105.0f, tableW * 0.23f );
    static constexpr uint32_t kFrameHash = ::HashStr( "Frame" );
    float timelineBudgetMs = PROFILER_UI_TIMELINE_BUDGET_MS;
    for ( int i = 0; i < frame.markerCount; ++i )
    {
        const MarkerSnapshot& marker = frame.markers[i];
        if ( marker.hash == kFrameHash )
        {
            timelineBudgetMs = (std::max)( PROFILER_UI_TIMELINE_BUDGET_MS, ProfilerMarkerDisplayCpuMs( marker ) );
            break;
        }
    }

    draw.Rect( tableX, tableY, tableW, tableH + 2.0f, 0.018f, 0.030f, 0.038f, 0.58f );
    draw.Outline( tableX, tableY, tableW, tableH + 2.0f, 0.18f, 0.30f, 0.34f, 0.62f );
    draw.Rect( tableX, tableY + headerH, tableW, 1.0f, 0.26f, 0.44f, 0.50f, 0.45f );
    draw.Text( colMarker, tableY + 10.0f, 10.5f, 0.68f, 0.78f, 0.82f, "Marker" );
    draw.Text( colCpu, tableY + 10.0f, 10.5f, 0.68f, 0.78f, 0.82f, "CPU" );
    draw.Text( colSelf, tableY + 10.0f, 10.5f, 0.68f, 0.78f, 0.82f, "Self" );
    draw.Text( colP50, tableY + 10.0f, 10.5f, 0.68f, 0.78f, 0.82f, "P50" );
    draw.Text( colP99, tableY + 10.0f, 10.5f, 0.68f, 0.78f, 0.82f, "P99" );
    draw.Text( barX, tableY + 10.0f, 10.5f, 0.68f, 0.78f, 0.82f, state.timelineEnabled ? "Span" : "0 ms" );
    draw.Text( barX + barW - 44.0f,
               tableY + 10.0f,
               10.5f,
               0.68f,
               0.78f,
               0.82f,
               state.timelineEnabled ? "Frame" : "16.67 ms" );

    int visibleRows[MAX_MARKERS] = {};
    const int visibleRowCount = BuildVisibleRows( state, visibleRows, MAX_MARKERS );
    TimelineSegment timelineSegments[MAX_MARKERS] = {};
    BuildTimelineSegments( state, visibleRows, visibleRowCount, timelineSegments );

    auto profilerRow = [&]( int rowIndex,
                            const MarkerSnapshot& marker,
                            const TimelineSegment& segment,
                            bool hasChildren,
                            bool isExpanded )
    {
        const float rowY = tableY + headerH + static_cast<float>( rowIndex ) * rowH - scrollY;
        if ( rowY + rowH < tableY + headerH || rowY > tableY + tableH )
        {
            return;
        }
        const float r = marker.colorR;
        const float g = marker.colorG;
        const float b = marker.colorB;
        const float indent = static_cast<float>( (std::min)( marker.depth, 8 ) ) * 18.0f;
        const float nameX = colMarker + indent;
        const float cpuMs = ProfilerMarkerDisplayCpuMs( marker );
        const float selfMs = ProfilerMarkerDisplaySelfMs( marker );
        const float p50Ms = marker.p50Ms > 0.0f ? marker.p50Ms : cpuMs;
        const float p99Ms = marker.p99Ms > 0.0f ? marker.p99Ms : cpuMs;
        draw.Rect( tableX, rowY + rowH - 1.0f, tableW, 1.0f, 0.16f, 0.26f, 0.30f, 0.38f );
        if ( hasChildren )
        {
            UIIconButton expander;
            expander.SetBounds( nameX, rowY + 8.0f, 14.0f, 14.0f );
            expander.DrawExpander( draw, isExpanded );
        }
        else
        {
            draw.Rect( nameX + 3.0f, rowY + 12.0f, 8.0f, 8.0f, r, g, b, 0.94f );
        }
        draw.Text( nameX + 22.0f, rowY + 8.0f, 12.0f, 0.92f, 0.96f, 0.97f, marker.leafName );
        snprintf( buf, sizeof( buf ), "%.2f", cpuMs );
        draw.Text( colCpu, rowY + 8.0f, 11.5f, r, g, b, buf );
        snprintf( buf, sizeof( buf ), "%.2f", selfMs );
        draw.Text( colSelf, rowY + 8.0f, 11.5f, r, g, b, buf );
        snprintf( buf, sizeof( buf ), "%.2f", p50Ms );
        draw.Text( colP50, rowY + 8.0f, 11.5f, 0.78f, 0.84f, 0.86f, buf );
        snprintf( buf, sizeof( buf ), "%.2f", p99Ms );
        draw.Text( colP99, rowY + 8.0f, 11.5f, 0.78f, 0.84f, 0.86f, buf );
        draw.Rect( barX, rowY + 16.0f, barW, 1.0f, 0.46f, 0.56f, 0.60f, 0.86f );
        const float fill = std::clamp( cpuMs / 16.67f, 0.0f, 1.0f );
        if ( state.timelineEnabled )
        {
            if ( segment.isFilled && segment.durationMs > 0.0f )
            {
                const float start = std::clamp( segment.startMs / timelineBudgetMs, 0.0f, 1.0f );
                const float end =
                    std::clamp( ( segment.startMs + segment.durationMs ) / timelineBudgetMs, start, 1.0f );
                draw.Rect( barX + barW * start, rowY + 11.0f, barW * ( end - start ), 10.0f, r, g, b, 0.88f );
                const float p99Tick = std::clamp( ( segment.startMs + p99Ms ) / timelineBudgetMs, start, end );
                draw.Rect( barX + barW * p99Tick, rowY + 7.0f, 1.0f, 18.0f, r, g, b, 0.98f );
            }
        }
        else
        {
            const float p99Tick = std::clamp( p99Ms / 16.67f, 0.0f, 1.0f );
            draw.Rect( barX, rowY + 11.0f, barW * fill, 10.0f, r, g, b, 0.88f );
            draw.Rect( barX + barW * p99Tick, rowY + 7.0f, 1.0f, 18.0f, r, g, b, 0.98f );
        }
    };

    for ( int visibleRow = 0; visibleRow < visibleRowCount; ++visibleRow )
    {
        const int markerIndex = visibleRows[visibleRow];
        const MarkerSnapshot& marker = frame.markers[markerIndex];
        const bool hasChildren = ProfilerMarkerHasChildren( frame, markerIndex );
        profilerRow( visibleRow,
                     marker,
                     timelineSegments[visibleRow],
                     hasChildren,
                     IsMarkerExpanded( state, marker.hash ) );
    }

    const DrawTraceSnapshot& drawSnapshot = frame.drawTrace;
    int visibleDrawRows[MAX_MARKERS] = {};
    const int visibleDrawRowCount = BuildVisibleDrawRows( state, drawSnapshot, visibleDrawRows, MAX_MARKERS );
    const float drawHeaderH = 32.0f;
    const float coreChartH = PROFILER_CORE_CHART_H;
    const float drawRowH = 26.0f;
    const float drawSectionY = tableY + headerH + static_cast<float>( visibleRowCount ) * rowH + 18.0f - scrollY;
    const float drawSectionH = visibleDrawRowCount > 0
                                   ? drawHeaderH + coreChartH + static_cast<float>( visibleDrawRowCount ) * drawRowH
                                   : 0.0f;
    const float colScope = colMarker;
    const float colDraws = colCpu;
    const float colInstances = colSelf;
    const float colVertices = colP50;

    if ( visibleDrawRowCount > 0 && drawSectionY + drawSectionH >= tableY && drawSectionY <= tableY + tableH )
    {
        draw.Rect( tableX, drawSectionY, tableW, drawSectionH, 0.018f, 0.030f, 0.038f, 0.52f );
        draw.Outline( tableX, drawSectionY, tableW, drawSectionH, 0.18f, 0.30f, 0.34f, 0.52f );
        draw.Rect( tableX, drawSectionY + drawHeaderH, tableW, 1.0f, 0.26f, 0.44f, 0.50f, 0.45f );
        const int totalDraws = drawSnapshot.nodeCount > 0 ? drawSnapshot.nodes[0].drawCallCount : 0;
        snprintf( buf, sizeof( buf ), "Draw Calls (%d)", totalDraws );
        draw.Text( colScope, drawSectionY + 10.0f, 10.5f, 0.68f, 0.78f, 0.82f, buf );
        draw.Text( colDraws, drawSectionY + 10.0f, 10.5f, 0.68f, 0.78f, 0.82f, "Draws" );
        draw.Text( colInstances, drawSectionY + 10.0f, 10.5f, 0.68f, 0.78f, 0.82f, "Instances" );
        draw.Text( colVertices, drawSectionY + 10.0f, 10.5f, 0.68f, 0.78f, 0.82f, "Vertices" );
        if ( drawSnapshot.nodeOverflowCount > 0 || drawSnapshot.eventOverflowCount > 0 ||
             drawSnapshot.scopeMismatchCount > 0 )
        {
            snprintf( buf,
                      sizeof( buf ),
                      "overflow n:%d e:%d s:%d",
                      drawSnapshot.nodeOverflowCount,
                      drawSnapshot.eventOverflowCount,
                      drawSnapshot.scopeMismatchCount );
            draw.Text( barX, drawSectionY + 10.0f, 10.5f, 1.0f, 0.72f, 0.24f, buf );
        }
    }

    const WorkerCoreSampleSnapshot* workerSamples[MAX_WORKER_CORE_SAMPLES] = {};
    const int coreSampleCount = frame.workerCoreSampleCount;
    for ( int i = 0; i < coreSampleCount; ++i )
    {
        const WorkerCoreSampleSnapshot& sample = frame.workerCoreSamples[i];
        if ( sample.workerIndex >= 0 && sample.workerIndex < MAX_WORKER_CORE_SAMPLES )
        {
            workerSamples[sample.workerIndex] = &sample;
        }
    }

    const int chartCoreCount = std::clamp( displayWorkerCount, 0, MAX_WORKER_CORE_SAMPLES );
    float coreAvgMs[MAX_WORKER_CORE_SAMPLES] = {};
    float totalCoreAvgMs = 0.0f;
    float maxCoreAvgMs = 0.0f;
    int hottestCore = -1;
    for ( int coreIndex = 0; coreIndex < chartCoreCount; ++coreIndex )
    {
        const WorkerCoreSampleSnapshot* sample = workerSamples[coreIndex];
        if ( sample )
        {
            coreAvgMs[coreIndex] = sample->avgCoreMs > 0.0f ? sample->avgCoreMs : sample->coreMs;
        }
        totalCoreAvgMs += coreAvgMs[coreIndex];
        if ( coreAvgMs[coreIndex] > maxCoreAvgMs )
        {
            maxCoreAvgMs = coreAvgMs[coreIndex];
            hottestCore = coreIndex;
        }
    }

    const float coreAxisMs = (std::max)( PROFILER_CORE_CHART_AXIS_MIN_MS, maxCoreAvgMs );
    const float coreChartY = drawSectionY + drawHeaderH;
    if ( visibleDrawRowCount > 0 && coreChartY + coreChartH >= tableY + headerH && coreChartY <= tableY + tableH )
    {
        draw.Rect( tableX, coreChartY, tableW, coreChartH, 0.014f, 0.024f, 0.031f, 0.64f );
        draw.Rect( tableX, coreChartY + coreChartH - 1.0f, tableW, 1.0f, 0.26f, 0.44f, 0.50f, 0.45f );
        snprintf( buf, sizeof( buf ), "CPU Core Work (%d columns, 0.5s avg ms/frame)", chartCoreCount );
        draw.Text( colScope, coreChartY + 10.0f, 10.0f, 0.78f, 0.88f, 0.91f, buf );
        if ( hottestCore >= 0 )
        {
            snprintf( buf,
                      sizeof( buf ),
                      "Scale %.2f ms/frame   total %.2f   hot core %d %.2f",
                      coreAxisMs,
                      totalCoreAvgMs,
                      hottestCore,
                      maxCoreAvgMs );
        }
        else
        {
            snprintf( buf, sizeof( buf ), "Scale %.2f ms/frame   total %.2f", coreAxisMs, totalCoreAvgMs );
        }
        draw.Text( barX - 88.0f, coreChartY + 10.0f, 10.0f, 0.68f, 0.78f, 0.82f, buf );

        const float plotX = tableX + 18.0f;
        const float plotY = coreChartY + 36.0f;
        const float plotW = (std::max)( 10.0f, tableW - 36.0f );
        const float plotH = 76.0f;
        const float baselineY = plotY + plotH;
        draw.Rect( plotX, plotY, plotW, plotH, 0.025f, 0.040f, 0.048f, 0.88f );
        draw.Outline( plotX, plotY, plotW, plotH, 0.18f, 0.30f, 0.34f, 0.62f );
        draw.Rect( plotX, baselineY - 1.0f, plotW, 1.0f, 0.52f, 0.62f, 0.64f, 0.72f );
        draw.Rect( plotX,
                   baselineY - plotH * ( PROFILER_CORE_CHART_AXIS_MIN_MS / coreAxisMs ),
                   plotW,
                   1.0f,
                   0.38f,
                   0.50f,
                   0.52f,
                   0.38f );
        draw.Text( plotX + 4.0f, plotY + 3.0f, 8.0f, 0.45f, 0.56f, 0.59f, "0.50 ms/frame" );

        if ( chartCoreCount <= 0 )
        {
            draw.Text( plotX + 10.0f, plotY + 30.0f, 11.0f, 0.88f, 0.74f, 0.38f, "Workers disabled" );
        }
        else
        {
            if ( coreSampleCount <= 0 )
            {
                draw.Text( plotX + 10.0f,
                           plotY + 24.0f,
                           10.0f,
                           0.76f,
                           0.84f,
                           0.86f,
                           "No worker jobs in this 0.5s window (idle, threshold, or config)" );
            }

            const float pitch = plotW / static_cast<float>( chartCoreCount );
            const float columnW = std::clamp( pitch * 0.62f, 2.0f, 24.0f );
            float lastValueLabelRight = plotX - 100.0f;
            for ( int coreIndex = 0; coreIndex < chartCoreCount; ++coreIndex )
            {
                const float ms = coreAvgMs[coreIndex];
                const float fillH = std::clamp( ms / coreAxisMs, 0.0f, 1.0f ) * plotH;
                const float x = plotX + static_cast<float>( coreIndex ) * pitch + ( pitch - columnW ) * 0.5f;
                const ProfilerUiColor& color = ProfilerPaletteColor( coreIndex + 9 );
                draw.Rect( x, baselineY - 1.0f, columnW, 1.0f, color.r, color.g, color.b, 0.72f );
                if ( fillH > 0.0f )
                {
                    draw.Rect( x, baselineY - fillH, columnW, fillH, color.r, color.g, color.b, 0.90f );
                }
                if ( pitch >= 18.0f )
                {
                    snprintf( buf, sizeof( buf ), "%d", coreIndex );
                    draw.Text( x, baselineY + 6.0f, 7.5f, 0.48f, 0.58f, 0.60f, buf );
                }
                if ( ms > 0.0f )
                {
                    snprintf( buf,
                              sizeof( buf ),
                              pitch >= 38.0f ? ( ms >= 10.0f ? "%.0fms" : ( ms >= 1.0f ? "%.1fms" : "%.2fms" ) )
                                             : ( ms >= 10.0f ? "%.0f" : ( ms >= 1.0f ? "%.1f" : "%.2f" ) ),
                              ms );
                    const float labelX = x - 2.0f;
                    const float labelY = (std::max)( plotY + 8.0f, baselineY - fillH - 12.0f );
                    if ( pitch >= 28.0f || labelX > lastValueLabelRight + 2.0f ||
                         ms >= PROFILER_CORE_CHART_AXIS_MIN_MS )
                    {
                        draw.Text( labelX, labelY, 7.5f, 0.94f, 0.98f, 0.99f, buf );
                        lastValueLabelRight = labelX + 26.0f;
                    }
                }
            }
        }
    }

    auto drawTraceRow = [&]( int rowIndex, const DrawTraceNodeSnapshot& node, bool hasChildren, bool isExpanded )
    {
        const float rowY = drawSectionY + drawHeaderH + coreChartH + static_cast<float>( rowIndex ) * drawRowH;
        if ( rowY + drawRowH < tableY + headerH || rowY > tableY + tableH )
        {
            return;
        }
        const ProfilerUiColor& color = ProfilerPaletteColor( static_cast<int>( node.hash % PROFILER_UI_PALETTE_SIZE ) );
        const float indent = static_cast<float>( (std::min)( node.depth, 8 ) ) * 18.0f;
        const float nameX = colScope + indent;
        draw.Rect( tableX, rowY + drawRowH - 1.0f, tableW, 1.0f, 0.16f, 0.26f, 0.30f, 0.34f );
        if ( hasChildren )
        {
            UIIconButton expander;
            expander.SetBounds( nameX, rowY + 6.0f, 14.0f, 14.0f );
            expander.DrawExpander( draw, isExpanded );
        }
        else
        {
            draw.Rect( nameX + 3.0f, rowY + 10.0f, 8.0f, 8.0f, color.r, color.g, color.b, 0.94f );
        }
        draw.Text( nameX + 22.0f, rowY + 6.0f, 11.5f, 0.92f, 0.96f, 0.97f, node.leafName ? node.leafName : "-" );
        snprintf( buf, sizeof( buf ), "%d", node.drawCallCount );
        draw.Text( colDraws, rowY + 6.0f, 11.0f, color.r, color.g, color.b, buf );
        snprintf( buf, sizeof( buf ), "%d", node.instanceCount );
        draw.Text( colInstances, rowY + 6.0f, 11.0f, 0.78f, 0.84f, 0.86f, buf );
        snprintf( buf, sizeof( buf ), "%d", node.vertexCount );
        draw.Text( colVertices, rowY + 6.0f, 11.0f, 0.78f, 0.84f, 0.86f, buf );
    };

    for ( int visibleRow = 0; visibleRow < visibleDrawRowCount; ++visibleRow )
    {
        const int nodeIndex = visibleDrawRows[visibleRow];
        const DrawTraceNodeSnapshot& node = drawSnapshot.nodes[nodeIndex];
        const bool hasChildren = DrawNodeHasVisibleChildren( drawSnapshot, nodeIndex );
        drawTraceRow( visibleRow, node, hasChildren, IsDrawNodeExpanded( state, node.hash ) );
    }
}

} // namespace ProfilerTab
} // namespace UI
} // namespace SkullbonezCore
