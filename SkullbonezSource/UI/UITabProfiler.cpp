/*
File: SkullbonezSource/UI/UITabProfiler.cpp
Purpose:
  Implements UI TabProfiler widgets, layout, drawing, or UI state for the in-engine controls.

Mental model:
  The UI is immediate-mode-style: each frame reads engine state, computes hit
  boxes, emits draw commands, and returns requests for the run loop to apply.

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

#include "../Core/Common.h"
#include "../Rendering/IRenderBackend.h"
#include "../Core/Profiler.h"
#include "UI.h"
#include "UIDraw.h"
#include "UIIconButton.h"
#include "UIStyle.h"

#include <algorithm>
#include <cstdio>

using namespace SkullbonezCore::Basics;

namespace
{

constexpr float PROFILER_UI_TIMELINE_BUDGET_MS = 16.67f;
constexpr float PROFILER_WORKER_BLOCK_H = 96.0f;
constexpr float PROFILER_MEMORY_BLOCK_H = 158.0f;
constexpr float PROFILER_TABLE_OFFSET_H = PROFILER_WORKER_BLOCK_H + PROFILER_MEMORY_BLOCK_H;
constexpr float PROFILER_WORKER_TOGGLE_Y = 12.0f;
constexpr float PROFILER_WORKER_SLIDER_Y = 52.0f;
constexpr float PROFILER_CORE_CHART_H = 142.0f;
constexpr float PROFILER_CORE_CHART_AXIS_MIN_MS = 0.50f;

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

bool ProfilerMarkerHasChildren( const SkullbonezCore::Basics::Profiler& profiler, int markerIndex )
{
    for ( int i = 0; i < profiler.MarkerCount(); ++i )
    {
        if ( profiler.GetMarker( i ).parentIndex == markerIndex )
        {
            return true;
        }
    }
    return false;
}

float ProfilerMarkerDisplayCpuMs( const SkullbonezCore::Basics::Profiler::Marker& marker )
{
    return marker.avgMs > 0.0f ? marker.avgMs : marker.lastFrameMs;
}

float ProfilerMarkerDisplaySelfMs( const SkullbonezCore::Basics::Profiler::Marker& marker )
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

SkullbonezCore::Rendering::DrawCallTraceSnapshot GetDrawTraceSnapshot()
{
    if ( !SkullbonezCore::Rendering::IsGfxReady() )
    {
        return SkullbonezCore::Rendering::DrawCallTraceSnapshot();
    }
    return SkullbonezCore::Rendering::Gfx().GetFrameDrawCallTrace();
}

bool DrawNodeHasVisibleChildren( const SkullbonezCore::Rendering::DrawCallTraceSnapshot& snapshot, int nodeIndex )
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
                          const SkullbonezCore::Rendering::DrawCallTraceSnapshot& snapshot,
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

        const SkullbonezCore::Rendering::DrawCallTraceNode& node = snapshot.nodes[nodeIndex];
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
    const SkullbonezCore::Basics::Profiler& profiler = SkullbonezCore::Basics::Profiler::Instance();
    const int markerCount = (std::min)( profiler.MarkerCount(), SkullbonezCore::UI::ProfilerTab::MAX_MARKERS );
    int childIndices[SkullbonezCore::UI::ProfilerTab::MAX_MARKERS][SkullbonezCore::UI::ProfilerTab::MAX_MARKERS] = {};
    int childCounts[SkullbonezCore::UI::ProfilerTab::MAX_MARKERS] = {};

    for ( int i = 0; i < markerCount; ++i )
    {
        const int parentIndex = profiler.GetMarker( i ).parentIndex;
        if ( parentIndex >= 0 && parentIndex < markerCount )
        {
            childIndices[parentIndex][childCounts[parentIndex]++] = i;
        }
    }

    int stack[SkullbonezCore::UI::ProfilerTab::MAX_MARKERS] = {};
    int stackTop = 0;
    for ( int i = markerCount - 1; i >= 0; --i )
    {
        if ( profiler.GetMarker( i ).parentIndex == -1 )
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

        const SkullbonezCore::Basics::Profiler::Marker& marker = profiler.GetMarker( markerIndex );
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
    const SkullbonezCore::Basics::Profiler& profiler = SkullbonezCore::Basics::Profiler::Instance();
    const int markerCount = (std::min)( profiler.MarkerCount(), SkullbonezCore::UI::ProfilerTab::MAX_MARKERS );
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
        const int parentIndex = profiler.GetMarker( i ).parentIndex;
        if ( parentIndex >= 0 && parentIndex < markerCount )
        {
            childIndices[parentIndex][childCounts[parentIndex]++] = i;
        }
    }

    auto assignSubtree = [&]( auto&& self, int markerIndex, float startMs ) -> void
    {
        const SkullbonezCore::Basics::Profiler::Marker& marker = profiler.GetMarker( markerIndex );
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
            childStartMs += (std::max)( 0.0f, ProfilerMarkerDisplayCpuMs( profiler.GetMarker( childIndex ) ) );
        }
    };

    float rootStartMs = 0.0f;
    for ( int i = 0; i < markerCount; ++i )
    {
        if ( profiler.GetMarker( i ).parentIndex == -1 )
        {
            assignSubtree( assignSubtree, i, rootStartMs );
            rootStartMs += (std::max)( 0.0f, ProfilerMarkerDisplayCpuMs( profiler.GetMarker( i ) ) );
        }
    }
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
        state.histogramCount = 0;
        state.histogramHead = 0;
        state.histogramAxisMs = 16.67f;
    }
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
        const Profiler& profiler = Profiler::Instance();
        static constexpr uint32_t kFrameHash = ::HashStr( "Frame" );
        static constexpr uint32_t kUIHash = ::HashStr( "Frame/UI" );
        static constexpr uint32_t kPhysicsHash = ::HashStr( "Frame/Physics" );
        for ( int i = 0; i < profiler.MarkerCount(); ++i )
        {
            const Profiler::Marker& marker = profiler.GetMarker( i );
            if ( ( marker.hash == kFrameHash || marker.hash == kUIHash || marker.hash == kPhysicsHash ) &&
                 ProfilerMarkerHasChildren( profiler, i ) && !IsMarkerExpanded( state, marker.hash ) &&
                 state.expandedHashCount < MAX_MARKERS )
            {
                state.expandedHashes[state.expandedHashCount++] = marker.hash;
            }
        }
        state.defaultExpansionApplied = true;
    }

    if ( !state.drawDefaultExpansionApplied )
    {
        const auto snapshot = GetDrawTraceSnapshot();
        if ( snapshot.nodeCount > 0 )
        {
            static constexpr uint32_t kFrameHash = ::HashStr( "Frame" );
            static constexpr uint32_t kRenderHash = ::HashStr( "Frame/Render" );
            static constexpr uint32_t kShadowsHash = ::HashStr( "Frame/Shadows" );
            static constexpr uint32_t kUIHash = ::HashStr( "Frame/UI" );
            for ( int i = 0; i < snapshot.nodeCount && state.drawExpandedHashCount < MAX_MARKERS; ++i )
            {
                const SkullbonezCore::Rendering::DrawCallTraceNode& node = snapshot.nodes[i];
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

    const Profiler& profiler = Profiler::Instance();
    for ( int i = 0; i < profiler.MarkerCount(); ++i )
    {
        const Profiler::Marker& marker = profiler.GetMarker( i );
        if ( ProfilerMarkerHasChildren( profiler, i ) && !IsMarkerExpanded( state, marker.hash ) &&
             state.expandedHashCount < MAX_MARKERS )
        {
            state.expandedHashes[state.expandedHashCount++] = marker.hash;
        }
    }

    const auto snapshot = GetDrawTraceSnapshot();
    for ( int i = 0; i < snapshot.nodeCount; ++i )
    {
        const SkullbonezCore::Rendering::DrawCallTraceNode& node = snapshot.nodes[i];
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
    const auto snapshot = GetDrawTraceSnapshot();
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

    const Profiler& profiler = Profiler::Instance();
    int visibleRows[MAX_MARKERS] = {};
    const int visibleRowCount = BuildVisibleRows( state, visibleRows, MAX_MARKERS );
    const int targetRow = ( localY - headerH ) / rowH;
    if ( targetRow >= 0 && targetRow < visibleRowCount )
    {
        const int markerIndex = visibleRows[targetRow];
        const Profiler::Marker& marker = profiler.GetMarker( markerIndex );
        if ( !ProfilerMarkerHasChildren( profiler, markerIndex ) )
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
    const auto snapshot = GetDrawTraceSnapshot();
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

    const SkullbonezCore::Rendering::DrawCallTraceNode& node = snapshot.nodes[nodeIndex];
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


void PushPerformanceHistogramSample( UIProfilerTabState& state, float cpuMs, float gpuMs )
{
    // Invariant: Histogram samples are a fixed ring buffer; clamp outliers so
    // one bad frame does not permanently flatten the visible chart scale.
    cpuMs = std::clamp( cpuMs, 0.0f, 250.0f );
    gpuMs = std::clamp( gpuMs, 0.0f, 250.0f );

    float previousMaxMs = 0.0f;
    for ( int i = 0; i < state.histogramCount; ++i )
    {
        const int sampleIndex =
            ( state.histogramHead - state.histogramCount + i + HISTOGRAM_SAMPLE_COUNT ) % HISTOGRAM_SAMPLE_COUNT;
        const PerformanceHistogramSample& sample = state.histogramSamples[sampleIndex];
        previousMaxMs = (std::max)( previousMaxMs, (std::max)( sample.cpuMs, sample.gpuMs ) );
    }

    const float sampleMaxMs = (std::max)( cpuMs, gpuMs );
    PerformanceHistogramSample& writeSample = state.histogramSamples[state.histogramHead];
    writeSample.cpuMs = cpuMs;
    writeSample.gpuMs = gpuMs;
    writeSample.spikeMs = 0.0f;
    if ( state.histogramCount > 8 && sampleMaxMs > 1.0f &&
         sampleMaxMs > (std::max)( previousMaxMs * 1.20f, state.histogramAxisMs * 0.92f ) )
    {
        writeSample.spikeMs = sampleMaxMs;
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
        const PerformanceHistogramSample& sample = state.histogramSamples[sampleIndex];
        visibleMaxMs = (std::max)( visibleMaxMs, (std::max)( sample.cpuMs, sample.gpuMs ) );
    }

    float targetAxisMs = 8.0f;
    const float targetRawMs = (std::max)( 8.0f, visibleMaxMs * 1.18f );
    while ( targetAxisMs < targetRawMs )
    {
        targetAxisMs += targetAxisMs < 32.0f ? 4.0f : 8.0f;
    }

    if ( targetAxisMs > state.histogramAxisMs )
    {
        state.histogramAxisMs = targetAxisMs;
    }
    else
    {
        state.histogramAxisMs += ( targetAxisMs - state.histogramAxisMs ) * 0.055f;
    }
}


void DrawPerformanceHistogram( const UIProfilerTabState& state,
                               const UIDrawContext& draw,
                               const InGameUIFrameData& data )
{
    if ( state.histogramCount <= 0 )
    {
        return;
    }

    const float panelX = 16.0f;
    const float panelY = 16.0f;
    const float panelW = (std::max)( 180.0f, (std::min)( 260.0f, static_cast<float>( data.screenW ) - 32.0f ) );
    const float panelH = 116.0f;
    const float plotX = panelX + 12.0f;
    const float plotY = panelY + 32.0f;
    const float plotW = panelW - 24.0f;
    const float plotH = 58.0f;
    const float baseY = plotY + plotH;
    const float axisMs = (std::max)( 1.0f, state.histogramAxisMs );

    const SkullbonezCore::UI::Style::UIPalette& palette = SkullbonezCore::UI::Style::Palette();
    draw.RoundedRect( panelX + 4.0f,
                      panelY + 5.0f,
                      panelW,
                      panelH,
                      SkullbonezCore::UI::Style::Radii().window,
                      0.0f,
                      0.0f,
                      0.0f,
                      0.22f );
    draw.RoundedPanel( { panelX, panelY, panelW, panelH },
                       SkullbonezCore::UI::Style::Radii().window,
                       palette.windowSubtle,
                       palette.border );
    draw.Text( panelX + 10.0f,
               panelY + 8.0f,
               10.5f,
               palette.textPrimary.r,
               palette.textPrimary.g,
               palette.textPrimary.b,
               "Frame Time" );

    char text[64] = {};
    snprintf( text, sizeof( text ), "%.0f ms", axisMs );
    draw.Text( panelX + panelW - 58.0f,
               panelY + 8.0f,
               10.0f,
               palette.textSecondary.r,
               palette.textSecondary.g,
               palette.textSecondary.b,
               text );

    draw.Rect( plotX, plotY, plotW, plotH, palette.window.r, palette.window.g, palette.window.b, 0.58f );
    draw.Rect( plotX,
               plotY + plotH * 0.50f,
               plotW,
               1.0f,
               palette.lineSoft.r,
               palette.lineSoft.g,
               palette.lineSoft.b,
               0.14f );
    draw.Rect( plotX, plotY, plotW, 1.0f, palette.lineSoft.r, palette.lineSoft.g, palette.lineSoft.b, 0.18f );
    draw.Rect( plotX, baseY, plotW, 1.0f, palette.accent.r, palette.accent.g, palette.accent.b, 0.34f );

    const float step = plotW / static_cast<float>( HISTOGRAM_SAMPLE_COUNT );
    const float barW = (std::max)( 1.0f, step * 0.42f );
    float spikeX = -1.0f;
    float spikeY = plotY;
    float spikeMs = 0.0f;

    for ( int i = 0; i < state.histogramCount; ++i )
    {
        const int sampleIndex =
            ( state.histogramHead - state.histogramCount + i + HISTOGRAM_SAMPLE_COUNT ) % HISTOGRAM_SAMPLE_COUNT;
        const PerformanceHistogramSample& sample = state.histogramSamples[sampleIndex];
        const float x = plotX + static_cast<float>( HISTOGRAM_SAMPLE_COUNT - state.histogramCount + i ) * step;
        const float cpuH = std::clamp( sample.cpuMs / axisMs, 0.0f, 1.0f ) * plotH;
        const float gpuH = std::clamp( sample.gpuMs / axisMs, 0.0f, 1.0f ) * plotH;

        if ( cpuH > 0.5f )
        {
            draw.Rect( x, baseY - cpuH, barW, cpuH, palette.accent.r, palette.accent.g, palette.accent.b, 0.66f );
        }
        if ( gpuH > 0.5f )
        {
            draw.Rect( x + barW + 0.5f,
                       baseY - gpuH,
                       barW,
                       gpuH,
                       palette.accentStrong.r,
                       palette.accentStrong.g,
                       palette.accentStrong.b,
                       0.78f );
        }
        if ( sample.spikeMs > spikeMs )
        {
            spikeMs = sample.spikeMs;
            spikeX = x + barW;
            spikeY = baseY - std::clamp( sample.spikeMs / axisMs, 0.0f, 1.0f ) * plotH;
        }
    }

    if ( spikeMs > 0.0f && spikeX >= plotX )
    {
        snprintf( text, sizeof( text ), "%.1f ms", spikeMs );
        const float labelX = ( spikeX + 54.0f < panelX + panelW ) ? spikeX + 4.0f : spikeX - 54.0f;
        const float labelY = (std::max)( plotY + 2.0f, spikeY - 16.0f );
        draw.Rect( spikeX,
                   plotY,
                   1.0f,
                   plotH,
                   palette.warningAccent.r,
                   palette.warningAccent.g,
                   palette.warningAccent.b,
                   0.58f );
        draw.Text( labelX,
                   labelY,
                   9.5f,
                   palette.warningAccent.r,
                   palette.warningAccent.g,
                   palette.warningAccent.b,
                   text );
    }

    const int newestIndex = ( state.histogramHead - 1 + HISTOGRAM_SAMPLE_COUNT ) % HISTOGRAM_SAMPLE_COUNT;
    const PerformanceHistogramSample& newest = state.histogramSamples[newestIndex];
    snprintf( text, sizeof( text ), "CPU %.2f", newest.cpuMs );
    draw.Text( panelX + 10.0f,
               panelY + panelH - 20.0f,
               10.0f,
               palette.accent.r,
               palette.accent.g,
               palette.accent.b,
               text );
    snprintf( text, sizeof( text ), "GPU %.2f", newest.gpuMs );
    draw.Text( panelX + 96.0f,
               panelY + panelH - 20.0f,
               10.0f,
               palette.accentStrong.r,
               palette.accentStrong.g,
               palette.accentStrong.b,
               text );
}

void FormatMemoryMiB( uint64_t bytes, char* out, std::size_t outSize )
{
    const double mib = static_cast<double>( bytes ) / ( 1024.0 * 1024.0 );
    if ( mib >= 1024.0 )
    {
        snprintf( out, outSize, "%.2f GiB", mib / 1024.0 );
    }
    else if ( mib >= 100.0 )
    {
        snprintf( out, outSize, "%.0f MiB", mib );
    }
    else
    {
        snprintf( out, outSize, "%.2f MiB", mib );
    }
}


void DrawMemoryRow( const UIDrawContext& draw,
                    float x,
                    float y,
                    float labelW,
                    const char* label,
                    uint64_t bytes,
                    float r,
                    float g,
                    float b )
{
    char value[32] = {};
    FormatMemoryMiB( bytes, value, sizeof( value ) );
    draw.Text( x, y, 9.6f, 0.68f, 0.78f, 0.82f, label );
    draw.Text( x + labelW, y, 9.6f, r, g, b, value );
}


void DrawMainMemoryPanel( const UIDrawContext& draw,
                          const InGameUIFrameData& data,
                          float contentX,
                          float contentY,
                          float contentW,
                          float contentH )
{
    const MainMemoryStats& memory = data.mainMemory;
    const float panelX = contentX;
    const float panelY = contentY + PROFILER_WORKER_BLOCK_H;
    const float panelW = contentW;
    const float panelH = PROFILER_MEMORY_BLOCK_H - 12.0f;
    if ( panelY + panelH > contentY + contentH )
    {
        return;
    }
    const float labelW = (std::min)( 118.0f, panelW * 0.34f );
    const float x = panelX + 14.0f;
    const float subX = panelX + (std::max)( 214.0f, panelW * 0.52f );
    char text[128] = {};
    char a[32] = {};
    char b[32] = {};
    char c[32] = {};

    draw.Rect( panelX, panelY, panelW, panelH, 0.018f, 0.030f, 0.038f, 0.58f );
    draw.Outline( panelX, panelY, panelW, panelH, 0.18f, 0.30f, 0.34f, 0.62f );
    draw.Rect( panelX, panelY + 27.0f, panelW, 1.0f, 0.26f, 0.44f, 0.50f, 0.45f );
    draw.Text( x, panelY + 9.0f, 10.4f, 0.68f, 0.78f, 0.82f, "Main Memory" );
    snprintf( text, sizeof( text ), "%s", memory.process.taskManagerMetricName );
    draw.Text( panelX + panelW - 118.0f, panelY + 9.0f, 9.2f, 0.54f, 0.66f, 0.70f, text );

    const float row0 = panelY + 36.0f;
    if ( memory.process.available )
    {
        DrawMemoryRow( draw, x, row0, labelW, "TaskMgr", memory.process.taskManagerBytes, 0.90f, 0.96f, 0.98f );
    }
    else
    {
        draw.Text( x, row0, 9.6f, 0.68f, 0.78f, 0.82f, "TaskMgr" );
        draw.Text( x + labelW, row0, 9.6f, 0.90f, 0.52f, 0.38f, "n/a" );
    }

    DrawMemoryRow( draw, x, row0 + 18.0f, labelW, "Replay", memory.replay.totalBytes, 0.42f, 0.86f, 0.94f );
    FormatMemoryMiB( memory.replay.presentationBytes, a, sizeof( a ) );
    FormatMemoryMiB( memory.replay.solverBytes, b, sizeof( b ) );
    FormatMemoryMiB( memory.replay.predictionBytes, c, sizeof( c ) );
    snprintf( text, sizeof( text ), "P %s  S %s  Pred %s", a, b, c );
    draw.Text( subX, row0 + 18.0f, 8.4f, 0.48f, 0.60f, 0.64f, text );

    DrawMemoryRow( draw, x, row0 + 36.0f, labelW, "Objects", memory.gameObjects.totalBytes, 0.70f, 0.90f, 0.54f );
    FormatMemoryMiB( memory.gameObjects.modelVectorBytes, a, sizeof( a ) );
    FormatMemoryMiB( memory.gameObjects.soaCacheBytes, b, sizeof( b ) );
    FormatMemoryMiB( memory.gameObjects.physicsWorldBytes, c, sizeof( c ) );
    snprintf( text, sizeof( text ), "Models %s  SoA %s  World %s", a, b, c );
    draw.Text( subX, row0 + 36.0f, 8.4f, 0.48f, 0.60f, 0.64f, text );

    DrawMemoryRow( draw, x, row0 + 54.0f, labelW, "Unattrib", memory.unattributedProcessBytes, 0.82f, 0.74f, 0.55f );
    FormatMemoryMiB( memory.trackedEngineBytes, a, sizeof( a ) );
    FormatMemoryMiB( memory.reconciledTotalBytes, b, sizeof( b ) );
    snprintf( text, sizeof( text ), "Tracked %s  Sum %s", a, b );
    draw.Text( subX, row0 + 54.0f, 8.4f, 0.48f, 0.60f, 0.64f, text );

    if ( memory.trackedOvershootBytes > 0 )
    {
        FormatMemoryMiB( memory.trackedOvershootBytes, a, sizeof( a ) );
        snprintf( text, sizeof( text ), "Tracked exceeds process by %s", a );
        draw.Text( x, row0 + 76.0f, 9.2f, 0.95f, 0.58f, 0.38f, text );
    }
    else
    {
        snprintf( text,
                  sizeof( text ),
                  "models %llu/%llu  replay %llu/%llu samples",
                  static_cast<unsigned long long>( memory.gameObjects.modelCount ),
                  static_cast<unsigned long long>( memory.gameObjects.modelCapacity ),
                  static_cast<unsigned long long>( memory.replay.presentationSamples ),
                  static_cast<unsigned long long>( memory.replay.solverSamples ) );
        draw.Text( x, row0 + 76.0f, 8.8f, 0.48f, 0.60f, 0.64f, text );
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
    const Profiler& profiler = Profiler::Instance();
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

    DrawMainMemoryPanel( draw, data, contentX, contentY, contentW, contentH );

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
    const float colSelf = tableX + tableW * 0.41f;
    const float colGpu = tableX + tableW * 0.50f;
    const float colP50 = tableX + tableW * 0.58f;
    const float colP99 = tableX + tableW * 0.66f;
    const float barX = tableX + tableW * 0.76f;
    const float barW = (std::max)( 95.0f, tableW * 0.20f );
    static constexpr uint32_t kFrameHash = ::HashStr( "Frame" );
    float timelineBudgetMs = PROFILER_UI_TIMELINE_BUDGET_MS;
    for ( int i = 0; i < profiler.MarkerCount(); ++i )
    {
        const Profiler::Marker& marker = profiler.GetMarker( i );
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
    draw.Text( colGpu, tableY + 10.0f, 10.5f, 0.68f, 0.78f, 0.82f, "GPU" );
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
                            const Profiler::Marker& marker,
                            const TimelineSegment& segment,
                            bool hasChildren,
                            bool isExpanded )
    {
        const float rowY = tableY + headerH + static_cast<float>( rowIndex ) * rowH - scrollY;
        if ( rowY + rowH < tableY + headerH || rowY > tableY + tableH )
        {
            return;
        }
        const Profiler::BarColor& color = Profiler::BAR_PALETTE[marker.colorIndex % Profiler::BAR_PALETTE_SIZE];
        const float r = color.r;
        const float g = color.g;
        const float b = color.b;
        const float indent = static_cast<float>( (std::min)( marker.depth, 8 ) ) * 18.0f;
        const float nameX = colMarker + indent;
        const float cpuMs = ProfilerMarkerDisplayCpuMs( marker );
        const float selfMs = ProfilerMarkerDisplaySelfMs( marker );
        const float gpuMs =
            marker.hasGpu ? ( marker.gpuAvgMs > 0.0f ? marker.gpuAvgMs : marker.gpuLastFrameMs ) : -1.0f;
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
        if ( gpuMs >= 0.0f )
        {
            snprintf( buf, sizeof( buf ), "%.2f", gpuMs );
        }
        else
        {
            snprintf( buf, sizeof( buf ), "-" );
        }
        draw.Text( colGpu, rowY + 8.0f, 11.5f, 0.38f, 0.84f, 1.0f, buf );
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
        const Profiler::Marker& marker = profiler.GetMarker( markerIndex );
        const bool hasChildren = ProfilerMarkerHasChildren( profiler, markerIndex );
        profilerRow( visibleRow,
                     marker,
                     timelineSegments[visibleRow],
                     hasChildren,
                     IsMarkerExpanded( state, marker.hash ) );
    }

    const auto drawSnapshot = GetDrawTraceSnapshot();
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
    const float colInstances = colGpu;
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

    const Profiler::WorkerCoreSample* workerSamples[Profiler::MAX_WORKER_CORES] = {};
    const int coreSampleCount = profiler.WorkerCoreSampleCount();
    for ( int i = 0; i < coreSampleCount; ++i )
    {
        const Profiler::WorkerCoreSample& sample = profiler.GetWorkerCoreSample( i );
        if ( sample.workerIndex >= 0 && sample.workerIndex < Profiler::MAX_WORKER_CORES )
        {
            workerSamples[sample.workerIndex] = &sample;
        }
    }

    const int chartCoreCount = std::clamp( displayWorkerCount, 0, Profiler::MAX_WORKER_CORES );
    float coreAvgMs[Profiler::MAX_WORKER_CORES] = {};
    float totalCoreAvgMs = 0.0f;
    float maxCoreAvgMs = 0.0f;
    int hottestCore = -1;
    for ( int coreIndex = 0; coreIndex < chartCoreCount; ++coreIndex )
    {
        const Profiler::WorkerCoreSample* sample = workerSamples[coreIndex];
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
                const Profiler::BarColor& color = Profiler::BAR_PALETTE[( coreIndex + 9 ) % Profiler::BAR_PALETTE_SIZE];
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

    auto drawTraceRow =
        [&]( int rowIndex, const SkullbonezCore::Rendering::DrawCallTraceNode& node, bool hasChildren, bool isExpanded )
    {
        const float rowY = drawSectionY + drawHeaderH + coreChartH + static_cast<float>( rowIndex ) * drawRowH;
        if ( rowY + drawRowH < tableY + headerH || rowY > tableY + tableH )
        {
            return;
        }
        const Profiler::BarColor& color = Profiler::BAR_PALETTE[node.hash % Profiler::BAR_PALETTE_SIZE];
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
        const SkullbonezCore::Rendering::DrawCallTraceNode& node = drawSnapshot.nodes[nodeIndex];
        const bool hasChildren = DrawNodeHasVisibleChildren( drawSnapshot, nodeIndex );
        drawTraceRow( visibleRow, node, hasChildren, IsDrawNodeExpanded( state, node.hash ) );
    }
}

} // namespace ProfilerTab
} // namespace UI
} // namespace SkullbonezCore
