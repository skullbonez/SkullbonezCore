#include "UITabProfiler.h"

#include "../SkullbonezCommon.h"
#include "../SkullbonezProfiler.h"
#include "SkullbonezUI.h"
#include "UIDraw.h"
#include "UIIconButton.h"

#include <algorithm>
#include <cstdio>

using namespace SkullbonezCore::Basics;

namespace
{

constexpr float PROFILER_UI_TIMELINE_BUDGET_MS = 16.67f;

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
    state.defaultExpansionApplied = false;
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


void ApplyDefaultExpansion( UIProfilerTabState& state )
{
    if ( state.defaultExpansionApplied )
    {
        return;
    }

    const Profiler& profiler = Profiler::Instance();
    static constexpr uint32_t kFrameHash = ::HashStr( "Frame" );
    static constexpr uint32_t kUIHash = ::HashStr( "Frame/UI" );
    static constexpr uint32_t kPhysicsHash = ::HashStr( "Frame/Physics" );
    for ( int i = 0; i < profiler.MarkerCount(); ++i )
    {
        const Profiler::Marker& marker = profiler.GetMarker( i );
        if ( ( marker.hash == kFrameHash || marker.hash == kUIHash || marker.hash == kPhysicsHash ) &&
             ProfilerMarkerHasChildren( profiler, i ) &&
             !IsMarkerExpanded( state, marker.hash ) &&
             state.expandedHashCount < MAX_MARKERS )
        {
            state.expandedHashes[state.expandedHashCount++] = marker.hash;
        }
    }
    state.defaultExpansionApplied = true;
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
        if ( ProfilerMarkerHasChildren( profiler, i ) && !IsMarkerExpanded( state, marker.hash ) && state.expandedHashCount < MAX_MARKERS )
        {
            state.expandedHashes[state.expandedHashCount++] = marker.hash;
        }
    }
}


int ContentHeight( const UIProfilerTabState& state )
{
    int visibleRows[MAX_MARKERS] = {};
    const int visibleMarkerCount = BuildVisibleRows( state, visibleRows, MAX_MARKERS );
    return 54 + visibleMarkerCount * 30;
}


bool HandleContentClick( UIProfilerTabState& state, int contentX, int contentY, float scrollY, int mouseX, int mouseY )
{
    const int headerH = 32;
    const int rowH = 30;
    const int localY = static_cast<int>( static_cast<float>( mouseY - contentY ) + scrollY );
    const int targetRow = ( localY - headerH ) / rowH;
    if ( targetRow < 0 )
    {
        return false;
    }

    const Profiler& profiler = Profiler::Instance();
    int visibleRows[MAX_MARKERS] = {};
    const int visibleRowCount = BuildVisibleRows( state, visibleRows, MAX_MARKERS );
    if ( targetRow >= visibleRowCount )
    {
        return false;
    }

    const int markerIndex = visibleRows[targetRow];
    const Profiler::Marker& marker = profiler.GetMarker( markerIndex );
    if ( !ProfilerMarkerHasChildren( profiler, markerIndex ) )
    {
        return false;
    }

    const float plusX = static_cast<float>( contentX + 18 + marker.depth * 18 );
    const float plusY = static_cast<float>( contentY + headerH + targetRow * rowH ) - scrollY + 8.0f;
    UIIconButton expander;
    expander.SetBounds( plusX, plusY, 14.0f, 14.0f );
    if ( !expander.HitTest( mouseX, mouseY ) )
    {
        return false;
    }

    ToggleMarker( state, marker.hash );
    return true;
}


void PushPerformanceHistogramSample( UIProfilerTabState& state, float cpuMs, float gpuMs )
{
    cpuMs = std::clamp( cpuMs, 0.0f, 250.0f );
    gpuMs = std::clamp( gpuMs, 0.0f, 250.0f );

    float previousMaxMs = 0.0f;
    for ( int i = 0; i < state.histogramCount; ++i )
    {
        const int sampleIndex = ( state.histogramHead - state.histogramCount + i + HISTOGRAM_SAMPLE_COUNT ) % HISTOGRAM_SAMPLE_COUNT;
        const PerformanceHistogramSample& sample = state.histogramSamples[sampleIndex];
        previousMaxMs = (std::max)( previousMaxMs, (std::max)( sample.cpuMs, sample.gpuMs ) );
    }

    const float sampleMaxMs = (std::max)( cpuMs, gpuMs );
    PerformanceHistogramSample& writeSample = state.histogramSamples[state.histogramHead];
    writeSample.cpuMs = cpuMs;
    writeSample.gpuMs = gpuMs;
    writeSample.spikeMs = 0.0f;
    if ( state.histogramCount > 8 &&
         sampleMaxMs > 1.0f &&
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
        const int sampleIndex = ( state.histogramHead - state.histogramCount + i + HISTOGRAM_SAMPLE_COUNT ) % HISTOGRAM_SAMPLE_COUNT;
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


void DrawPerformanceHistogram( const UIProfilerTabState& state, const UIDrawContext& draw, const InGameUIFrameData& data )
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

    draw.Rect( panelX - 5.0f, panelY - 5.0f, panelW + 10.0f, panelH + 10.0f, 0.03f, 0.54f, 0.86f, 0.10f );
    draw.Rect( panelX, panelY, panelW, panelH, 0.012f, 0.030f, 0.040f, 0.76f );
    draw.Outline( panelX, panelY, panelW, panelH, 0.39f, 0.88f, 1.0f, 0.82f );
    draw.Rect( panelX + 1.0f, panelY + 1.0f, panelW - 2.0f, 1.0f, 0.44f, 0.92f, 1.0f, 0.30f );
    draw.Text( panelX + 10.0f, panelY + 8.0f, 10.5f, 1.0f, 0.85f, 0.34f, "Frame Time" );

    char text[64] = {};
    snprintf( text, sizeof( text ), "%.0f ms", axisMs );
    draw.Text( panelX + panelW - 58.0f, panelY + 8.0f, 10.0f, 0.68f, 0.86f, 0.92f, text );

    draw.Rect( plotX, plotY, plotW, plotH, 0.005f, 0.014f, 0.020f, 0.68f );
    draw.Rect( plotX, plotY + plotH * 0.50f, plotW, 1.0f, 0.18f, 0.30f, 0.34f, 0.45f );
    draw.Rect( plotX, plotY, plotW, 1.0f, 0.18f, 0.30f, 0.34f, 0.62f );
    draw.Rect( plotX, baseY, plotW, 1.0f, 0.26f, 0.82f, 1.0f, 0.34f );

    const float step = plotW / static_cast<float>( HISTOGRAM_SAMPLE_COUNT );
    const float barW = (std::max)( 1.0f, step * 0.42f );
    float spikeX = -1.0f;
    float spikeY = plotY;
    float spikeMs = 0.0f;

    for ( int i = 0; i < state.histogramCount; ++i )
    {
        const int sampleIndex = ( state.histogramHead - state.histogramCount + i + HISTOGRAM_SAMPLE_COUNT ) % HISTOGRAM_SAMPLE_COUNT;
        const PerformanceHistogramSample& sample = state.histogramSamples[sampleIndex];
        const float x = plotX + static_cast<float>( HISTOGRAM_SAMPLE_COUNT - state.histogramCount + i ) * step;
        const float cpuH = std::clamp( sample.cpuMs / axisMs, 0.0f, 1.0f ) * plotH;
        const float gpuH = std::clamp( sample.gpuMs / axisMs, 0.0f, 1.0f ) * plotH;

        if ( cpuH > 0.5f )
        {
            draw.Rect( x, baseY - cpuH, barW, cpuH, 0.48f, 0.90f, 0.22f, 0.66f );
        }
        if ( gpuH > 0.5f )
        {
            draw.Rect( x + barW + 0.5f, baseY - gpuH, barW, gpuH, 0.34f, 0.91f, 1.0f, 0.78f );
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
        draw.Rect( spikeX, plotY, 1.0f, plotH, 1.0f, 0.85f, 0.34f, 0.58f );
        draw.Text( labelX, labelY, 9.5f, 1.0f, 0.85f, 0.34f, text );
    }

    const int newestIndex = ( state.histogramHead - 1 + HISTOGRAM_SAMPLE_COUNT ) % HISTOGRAM_SAMPLE_COUNT;
    const PerformanceHistogramSample& newest = state.histogramSamples[newestIndex];
    snprintf( text, sizeof( text ), "CPU %.2f", newest.cpuMs );
    draw.Text( panelX + 10.0f, panelY + panelH - 20.0f, 10.0f, 0.48f, 0.90f, 0.22f, text );
    snprintf( text, sizeof( text ), "GPU %.2f", newest.gpuMs );
    draw.Text( panelX + 96.0f, panelY + panelH - 20.0f, 10.0f, 0.34f, 0.91f, 1.0f, text );
}


void Draw( UIProfilerTabState& state,
           const UIDrawContext& draw,
           float contentX,
           float contentY,
           float contentW,
           float contentH,
           float scrollY )
{
    char buf[128];
    const Profiler& profiler = Profiler::Instance();
    const float tableX = contentX;
    const float tableY = contentY;
    const float tableW = contentW;
    const float rowH = 30.0f;
    const float headerH = 32.0f;
    const float colMarker = tableX + 18.0f;
    const float colCpu = tableX + tableW * 0.36f;
    const float colGpu = tableX + tableW * 0.46f;
    const float colP50 = tableX + tableW * 0.56f;
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

    draw.Rect( tableX, tableY, tableW, contentH + 2.0f, 0.018f, 0.030f, 0.038f, 0.58f );
    draw.Outline( tableX, tableY, tableW, contentH + 2.0f, 0.18f, 0.30f, 0.34f, 0.62f );
    draw.Rect( tableX, tableY + headerH, tableW, 1.0f, 0.26f, 0.44f, 0.50f, 0.45f );
    draw.Text( colMarker, tableY + 10.0f, 10.5f, 0.68f, 0.78f, 0.82f, "Marker" );
    draw.Text( colCpu, tableY + 10.0f, 10.5f, 0.68f, 0.78f, 0.82f, "CPU" );
    draw.Text( colGpu, tableY + 10.0f, 10.5f, 0.68f, 0.78f, 0.82f, "GPU" );
    draw.Text( colP50, tableY + 10.0f, 10.5f, 0.68f, 0.78f, 0.82f, "P50" );
    draw.Text( colP99, tableY + 10.0f, 10.5f, 0.68f, 0.78f, 0.82f, "P99" );
    draw.Text( barX, tableY + 10.0f, 10.5f, 0.68f, 0.78f, 0.82f, state.timelineEnabled ? "Span" : "0 ms" );
    draw.Text( barX + barW - 44.0f, tableY + 10.0f, 10.5f, 0.68f, 0.78f, 0.82f, state.timelineEnabled ? "Frame" : "16.67 ms" );

    int visibleRows[MAX_MARKERS] = {};
    const int visibleRowCount = BuildVisibleRows( state, visibleRows, MAX_MARKERS );
    TimelineSegment timelineSegments[MAX_MARKERS] = {};
    BuildTimelineSegments( state, visibleRows, visibleRowCount, timelineSegments );

    auto profilerRow = [&]( int rowIndex, const Profiler::Marker& marker, const TimelineSegment& segment, bool hasChildren, bool isExpanded )
    {
        const float rowY = tableY + headerH + static_cast<float>( rowIndex ) * rowH - scrollY;
        if ( rowY + rowH < tableY + headerH || rowY > tableY + contentH )
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
        const float gpuMs = marker.hasGpu ? ( marker.gpuAvgMs > 0.0f ? marker.gpuAvgMs : marker.gpuLastFrameMs ) : -1.0f;
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
                const float end = std::clamp( ( segment.startMs + segment.durationMs ) / timelineBudgetMs, start, 1.0f );
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
        profilerRow( visibleRow, marker, timelineSegments[visibleRow], hasChildren, IsMarkerExpanded( state, marker.hash ) );
    }
}

} // namespace ProfilerTab
} // namespace UI
} // namespace SkullbonezCore
