#include "SkullbonezUI.h"
#include "../SkullbonezPhysicsDebugVisualizer.h"
#include "../SkullbonezProfiler.h"
#include "../SkullbonezText.h"
#include "UIDraw.h"
#include "UIDrawWidgets.h"
#include "UIIconButton.h"
#include "UIInput.h"
#include "UILayout.h"
#include "UITabScene.h"
#include "UIWindowChrome.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>

using namespace SkullbonezCore::Basics;
using namespace SkullbonezCore::Physics;
using namespace SkullbonezCore::Text;
using namespace SkullbonezCore::UI;
using namespace SkullbonezCore::UI::Widgets;
using namespace SkullbonezCore::UI::Layout;

namespace
{
constexpr int PROFILER_UI_MAX_MARKERS = 64;
constexpr float PROFILER_UI_TIMELINE_BUDGET_MS = 16.67f;

int GetRendererIndexFromName( const char* rendererName )
{
    if ( rendererName && strstr( rendererName, "12" ) )
    {
        return RENDERER_DX12;
    }
    if ( rendererName && strstr( rendererName, "11" ) )
    {
        return RENDERER_DX11;
    }
    return RENDERER_GL;
}

bool ProfilerMarkerHasChildren( const Profiler& profiler, int markerIndex )
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


int WaterReflectionModeFromData( const InGameUIFrameData& data )
{
    if ( data.waterNoReflect )
    {
        return 2;
    }
    return data.waterRTReflect ? 1 : 0;
}


float ProfilerMarkerDisplayCpuMs( const Profiler::Marker& marker )
{
    return marker.avgMs > 0.0f ? marker.avgMs : marker.lastFrameMs;
}


} // namespace

bool InGameUI::IsVisible() const
{
    return m_window.isVisible;
}


void InGameUI::SetVisible( bool visible, double now )
{
    m_window.isVisible = visible;
    m_backdropBlur.Invalidate();
    if ( visible )
    {
        m_window.isMinimized = false;
        m_scrollbarVisibleUntil = now + 1.2;
    }
    else
    {
        m_window.isMinimized = true;
        m_interaction.isDragging = false;
        m_interaction.isResizing = false;
        m_interaction.blocksCameraMouse = false;
        m_rendererCombo.Close();
        m_reflectionCombo.Close();
        CloseSceneCombo();
    }
}


void InGameUI::ToggleVisible( double now )
{
    if ( !m_window.isVisible )
    {
        SetVisible( true, now );
        return;
    }
    SetMinimized( !m_window.isMinimized, now );
}


void InGameUI::SetMinimized( bool minimized, double now )
{
    if ( m_window.isMinimized == minimized )
    {
        return;
    }

    const UIRect currentBounds = Chrome::WindowRect( m_window );
    const UIRect minimizedBounds = MinimizedRect( m_lastScreenW, m_lastScreenH, m_window.minimizedWidth );
    m_interaction.isDragging = false;
    m_interaction.isResizing = false;
    m_interaction.blocksCameraMouse = false;
    if ( minimized )
    {
        m_window.isMinimized = true;
        Chrome::BeginWindowAnimation( m_window, currentBounds, minimizedBounds, now, true );
        m_rendererCombo.Close();
        m_reflectionCombo.Close();
        CloseSceneCombo();
        m_activeSlider = 0;
    }
    else
    {
        m_window.isMinimized = false;
        Chrome::BeginWindowAnimation( m_window, minimizedBounds, Chrome::WindowRect( m_window ), now, false );
        m_scrollbarVisibleUntil = now + 1.2;
    }
    m_backdropBlur.Invalidate();
}


void InGameUI::ToggleMaximizeMinimize( int screenW, int screenH, double now )
{
    if ( !m_window.isVisible )
    {
        SetVisible( true, now );
        return;
    }

    if ( m_window.isMinimized )
    {
        SetMinimized( false, now );
        return;
    }

    SetMaximized( !m_window.isMaximized, screenW, screenH, now );
}


void InGameUI::SetActiveTab( InGameUITab tab )
{
    m_activeTab = tab;
    m_scrollY = 0.0f;
    m_rendererCombo.Close();
    m_reflectionCombo.Close();
    CloseSceneCombo();
    m_activeSlider = 0;
    m_previewTimeScale = -1.0f;
    m_previewModelCount = -1;
    m_previewPhysicsAlpha = -1.0f;
    m_previewContactLinger = -1.0f;
    m_previewSolverBallCount = -1;
    m_previewSolverBoxCount = -1;
    m_backdropBlur.Invalidate();
}


InGameUITab InGameUI::GetActiveTab() const
{
    return m_activeTab;
}


void InGameUI::CancelInputCapture()
{
    m_interaction.leftWasDown = false;
    m_interaction.isDragging = false;
    m_interaction.isResizing = false;
    m_interaction.blocksCameraMouse = false;
    m_activeSlider = 0;
    m_previewTimeScale = -1.0f;
    m_previewModelCount = -1;
    m_previewPhysicsAlpha = -1.0f;
    m_previewContactLinger = -1.0f;
    m_previewSolverBallCount = -1;
    m_previewSolverBoxCount = -1;
}


bool InGameUI::BlocksCameraMouse() const
{
    return m_interaction.blocksCameraMouse;
}


bool InGameUI::BlocksKeyboard() const
{
    return m_window.isVisible && !m_window.isMinimized && m_sceneCombo.IsOpen();
}


bool InGameUI::WantsNativeMouseCursor() const
{
    return m_window.isVisible && !m_window.isMinimized;
}


void InGameUI::SetWindowBounds( int x, int y, int width, int height )
{
    m_window.x = x;
    m_window.y = y;
    m_window.width = width;
    m_window.height = height;
    m_window.restoreX = x;
    m_window.restoreY = y;
    m_window.restoreW = width;
    m_window.restoreH = height;
    m_window.hasAppliedDefaultPlacement = true;
    m_window.isMaximized = false;
    m_window.animationActive = false;
    m_scrollY = 0.0f;
    m_scrollbarVisibleUntil = 0.0;
    m_backdropBlur.Invalidate();
}


void InGameUI::SetBlurEnabled( bool enabled )
{
    if ( m_blurPreviewEnabled != enabled )
    {
        m_blurPreviewEnabled = enabled;
        m_backdropBlur.Invalidate();
    }
}


void InGameUI::SetRendererComboOpen( bool open )
{
    m_rendererCombo.SetOpen( open );
    if ( open )
    {
        m_reflectionCombo.Close();
        CloseSceneCombo();
    }
}


void InGameUI::SetWaterComboOpen( bool open )
{
    m_reflectionCombo.SetOpen( open );
    if ( open )
    {
        m_rendererCombo.Close();
        CloseSceneCombo();
    }
}


void InGameUI::SetSceneComboOpen( bool open )
{
    m_sceneCombo.SetOpen( open );
    if ( open )
    {
        m_rendererCombo.Close();
        m_reflectionCombo.Close();
        SceneTab::CaptureFilterKeyState( m_sceneTab );
    }
    else
    {
        SceneTab::ClearFilter( m_sceneTab );
    }
}


void InGameUI::SetSceneFilter( const char* filter )
{
    SceneTab::SetFilter( m_sceneTab, filter );
}


void InGameUI::SetProfilerExpandAll( bool expandAll )
{
    m_expandAllProfilerMarkers = expandAll;
    m_expandedProfilerHashCount = 0;
    m_profilerDefaultExpansionApplied = false;
    if ( expandAll )
    {
        ApplyProfilerExpandAll();
    }
}


void InGameUI::SetProfilerTimelineEnabled( bool enabled )
{
    m_profilerTimelineEnabled = enabled;
}


void InGameUI::SetPerformanceHistogramEnabled( bool enabled )
{
    m_performanceHistogramEnabled = enabled;
    if ( !enabled )
    {
        m_performanceHistogramCount = 0;
        m_performanceHistogramHead = 0;
        m_performanceHistogramAxisMs = 16.67f;
    }
}


void InGameUI::SetScrollY( float scrollY )
{
    m_scrollY = (std::max)( 0.0f, scrollY );
    m_scrollbarVisibleUntil = 1.2;
}


void InGameUI::SetMouseOverride( bool enabled, int x, int y )
{
    m_hasMouseOverride = enabled;
    m_mouseOverrideX = x;
    m_mouseOverrideY = y;
    if ( enabled )
    {
        m_mouseX = x;
        m_mouseY = y;
    }
}


void InGameUI::SetMaximized( bool maximized, int screenW, int screenH, double now )
{
    if ( Chrome::SetMaximized( m_window, maximized, screenW, screenH, now ) )
    {
        m_scrollbarVisibleUntil = 0.0;
        m_backdropBlur.Invalidate();
    }
}


void InGameUI::ResetResources()
{
    m_backdropBlur.ResetResources();
}


void InGameUI::ApplyProfilerExpandAll()
{
    if ( !m_expandAllProfilerMarkers )
    {
        return;
    }

    const Profiler& profiler = Profiler::Instance();
    for ( int i = 0; i < profiler.MarkerCount(); ++i )
    {
        const Profiler::Marker& marker = profiler.GetMarker( i );
        if ( ProfilerMarkerHasChildren( profiler, i ) && !IsProfilerMarkerExpanded( marker.hash ) && m_expandedProfilerHashCount < PROFILER_UI_MAX_MARKERS )
        {
            m_expandedProfilerHashes[m_expandedProfilerHashCount++] = marker.hash;
        }
    }
}


void InGameUI::ApplyProfilerDefaultExpansion()
{
    if ( m_profilerDefaultExpansionApplied )
    {
        return;
    }

    const Profiler& profiler = Profiler::Instance();
    const int markerCount = (std::min)( profiler.MarkerCount(), PROFILER_UI_MAX_MARKERS );
    if ( markerCount <= 0 )
    {
        return;
    }

    for ( int i = 0; i < markerCount; ++i )
    {
        const Profiler::Marker& marker = profiler.GetMarker( i );
        if ( marker.parentIndex == -1 &&
             ProfilerMarkerHasChildren( profiler, i ) &&
             !IsProfilerMarkerExpanded( marker.hash ) &&
             m_expandedProfilerHashCount < PROFILER_UI_MAX_MARKERS )
        {
            m_expandedProfilerHashes[m_expandedProfilerHashCount++] = marker.hash;
        }
    }

    m_profilerDefaultExpansionApplied = true;
}


void InGameUI::DrawCursor( const UIDrawContext& draw ) const
{
    const float x = static_cast<float>( m_mouseX );
    const float y = static_cast<float>( m_mouseY );

    auto drawShape = [&]( const float* p, float r, float g, float b, float a )
    {
        draw.Triangle( x + p[0], y + p[1], x + p[2], y + p[3], x + p[4], y + p[5], r, g, b, a );
        draw.Triangle( x + p[0], y + p[1], x + p[4], y + p[5], x + p[12], y + p[13], r, g, b, a );
        draw.Triangle( x + p[4], y + p[5], x + p[6], y + p[7], x + p[8], y + p[9], r, g, b, a );
        draw.Triangle( x + p[4], y + p[5], x + p[8], y + p[9], x + p[10], y + p[11], r, g, b, a );
        draw.Triangle( x + p[4], y + p[5], x + p[10], y + p[11], x + p[12], y + p[13], r, g, b, a );
    };

    const float shadow[] = { 1.7f, 2.0f, 2.1f, 24.2f, 8.5f, 17.3f, 12.9f, 26.5f, 17.5f, 24.2f, 13.0f, 15.9f, 20.8f, 14.8f };
    const float outer[] = { 0.0f, 0.0f, 0.7f, 22.3f, 7.0f, 15.6f, 11.5f, 24.8f, 16.0f, 22.6f, 11.5f, 14.3f, 19.0f, 13.2f };
    const float inner[] = { 3.0f, 4.0f, 3.4f, 15.7f, 7.0f, 12.0f, 10.6f, 20.2f, 12.2f, 19.4f, 8.5f, 11.7f, 13.2f, 11.1f };

    drawShape( shadow, 0.0f, 0.0f, 0.0f, 0.30f );
    drawShape( outer, 0.42f, 0.91f, 1.0f, 0.98f );
    drawShape( inner, 0.014f, 0.064f, 0.102f, 0.98f );
    draw.Triangle( x + 4.0f, y + 5.0f, x + 4.2f, y + 10.8f, x + 5.9f, y + 9.0f, 0.18f, 0.46f, 0.58f, 0.58f );
}


int InGameUI::ContentHeight() const
{
    switch ( m_activeTab )
    {
    case InGameUITab::Keys:
        return 338;
    case InGameUITab::Profiler:
    {
        int visibleRows[PROFILER_UI_MAX_MARKERS] = {};
        const int visibleMarkerCount = BuildVisibleProfilerRows( visibleRows, PROFILER_UI_MAX_MARKERS );
        return 54 + visibleMarkerCount * 30;
    }
    case InGameUITab::Physics:
        return 438;
    case InGameUITab::Options:
        return 286;
    default:
        return 338;
    }
}


int InGameUI::BuildVisibleProfilerRows( int* rows, int maxRows ) const
{
    const Profiler& profiler = Profiler::Instance();
    const int markerCount = (std::min)( profiler.MarkerCount(), PROFILER_UI_MAX_MARKERS );
    int childIndices[PROFILER_UI_MAX_MARKERS][PROFILER_UI_MAX_MARKERS] = {};
    int childCounts[PROFILER_UI_MAX_MARKERS] = {};

    for ( int i = 0; i < markerCount; ++i )
    {
        const int parentIndex = profiler.GetMarker( i ).parentIndex;
        if ( parentIndex >= 0 && parentIndex < markerCount )
        {
            childIndices[parentIndex][childCounts[parentIndex]++] = i;
        }
    }

    int stack[PROFILER_UI_MAX_MARKERS] = {};
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

        const Profiler::Marker& marker = profiler.GetMarker( markerIndex );
        if ( !IsProfilerMarkerExpanded( marker.hash ) )
        {
            continue;
        }
        for ( int child = childCounts[markerIndex] - 1; child >= 0; --child )
        {
            if ( stackTop < PROFILER_UI_MAX_MARKERS )
            {
                stack[stackTop++] = childIndices[markerIndex][child];
            }
        }
    }

    return (std::min)( rowCount, maxRows );
}


void InGameUI::BuildProfilerTimelineSegments( const int* rows, int rowCount, ProfilerTimelineSegment* segments ) const
{
    const Profiler& profiler = Profiler::Instance();
    const int markerCount = (std::min)( profiler.MarkerCount(), PROFILER_UI_MAX_MARKERS );
    int rowForMarker[PROFILER_UI_MAX_MARKERS] = {};
    int childIndices[PROFILER_UI_MAX_MARKERS][PROFILER_UI_MAX_MARKERS] = {};
    int childCounts[PROFILER_UI_MAX_MARKERS] = {};

    for ( int i = 0; i < PROFILER_UI_MAX_MARKERS; ++i )
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
        const Profiler::Marker& marker = profiler.GetMarker( markerIndex );
        const bool hasChildren = childCounts[markerIndex] > 0;
        const bool expanded = hasChildren && IsProfilerMarkerExpanded( marker.hash );
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


bool InGameUI::IsProfilerMarkerExpanded( uint32_t hash ) const
{
    for ( int i = 0; i < m_expandedProfilerHashCount; ++i )
    {
        if ( m_expandedProfilerHashes[i] == hash )
        {
            return true;
        }
    }
    return false;
}


void InGameUI::ToggleProfilerMarker( uint32_t hash )
{
    m_profilerDefaultExpansionApplied = true;
    for ( int i = 0; i < m_expandedProfilerHashCount; ++i )
    {
        if ( m_expandedProfilerHashes[i] == hash )
        {
            for ( int j = i; j < m_expandedProfilerHashCount - 1; ++j )
            {
                m_expandedProfilerHashes[j] = m_expandedProfilerHashes[j + 1];
            }
            --m_expandedProfilerHashCount;
            return;
        }
    }
    if ( m_expandedProfilerHashCount < PROFILER_UI_MAX_MARKERS )
    {
        m_expandedProfilerHashes[m_expandedProfilerHashCount++] = hash;
    }
}


void InGameUI::PushPerformanceHistogramSample( float cpuMs, float gpuMs )
{
    cpuMs = std::clamp( cpuMs, 0.0f, 250.0f );
    gpuMs = std::clamp( gpuMs, 0.0f, 250.0f );

    float previousMaxMs = 0.0f;
    for ( int i = 0; i < m_performanceHistogramCount; ++i )
    {
        const int sampleIndex = ( m_performanceHistogramHead - m_performanceHistogramCount + i + PERFORMANCE_HISTOGRAM_SAMPLE_COUNT ) % PERFORMANCE_HISTOGRAM_SAMPLE_COUNT;
        const PerformanceHistogramSample& sample = m_performanceHistogramSamples[sampleIndex];
        previousMaxMs = (std::max)( previousMaxMs, (std::max)( sample.cpuMs, sample.gpuMs ) );
    }

    const float sampleMaxMs = (std::max)( cpuMs, gpuMs );
    PerformanceHistogramSample& writeSample = m_performanceHistogramSamples[m_performanceHistogramHead];
    writeSample.cpuMs = cpuMs;
    writeSample.gpuMs = gpuMs;
    writeSample.spikeMs = 0.0f;
    if ( m_performanceHistogramCount > 8 &&
         sampleMaxMs > 1.0f &&
         sampleMaxMs > (std::max)( previousMaxMs * 1.20f, m_performanceHistogramAxisMs * 0.92f ) )
    {
        writeSample.spikeMs = sampleMaxMs;
    }

    m_performanceHistogramHead = ( m_performanceHistogramHead + 1 ) % PERFORMANCE_HISTOGRAM_SAMPLE_COUNT;
    if ( m_performanceHistogramCount < PERFORMANCE_HISTOGRAM_SAMPLE_COUNT )
    {
        ++m_performanceHistogramCount;
    }

    float visibleMaxMs = 0.0f;
    for ( int i = 0; i < m_performanceHistogramCount; ++i )
    {
        const int sampleIndex = ( m_performanceHistogramHead - m_performanceHistogramCount + i + PERFORMANCE_HISTOGRAM_SAMPLE_COUNT ) % PERFORMANCE_HISTOGRAM_SAMPLE_COUNT;
        const PerformanceHistogramSample& sample = m_performanceHistogramSamples[sampleIndex];
        visibleMaxMs = (std::max)( visibleMaxMs, (std::max)( sample.cpuMs, sample.gpuMs ) );
    }

    float targetAxisMs = 8.0f;
    const float targetRawMs = (std::max)( 8.0f, visibleMaxMs * 1.18f );
    while ( targetAxisMs < targetRawMs )
    {
        targetAxisMs += targetAxisMs < 32.0f ? 4.0f : 8.0f;
    }

    if ( targetAxisMs > m_performanceHistogramAxisMs )
    {
        m_performanceHistogramAxisMs = targetAxisMs;
    }
    else
    {
        m_performanceHistogramAxisMs += ( targetAxisMs - m_performanceHistogramAxisMs ) * 0.055f;
    }
}


void InGameUI::DrawPerformanceHistogram( const UIDrawContext& draw, const InGameUIFrameData& data ) const
{
    if ( m_performanceHistogramCount <= 0 )
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
    const float axisMs = (std::max)( 1.0f, m_performanceHistogramAxisMs );

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

    const float step = plotW / static_cast<float>( PERFORMANCE_HISTOGRAM_SAMPLE_COUNT );
    const float barW = (std::max)( 1.0f, step * 0.42f );
    float spikeX = -1.0f;
    float spikeY = plotY;
    float spikeMs = 0.0f;

    for ( int i = 0; i < m_performanceHistogramCount; ++i )
    {
        const int sampleIndex = ( m_performanceHistogramHead - m_performanceHistogramCount + i + PERFORMANCE_HISTOGRAM_SAMPLE_COUNT ) % PERFORMANCE_HISTOGRAM_SAMPLE_COUNT;
        const PerformanceHistogramSample& sample = m_performanceHistogramSamples[sampleIndex];
        const float x = plotX + static_cast<float>( PERFORMANCE_HISTOGRAM_SAMPLE_COUNT - m_performanceHistogramCount + i ) * step;
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

    const int newestIndex = ( m_performanceHistogramHead - 1 + PERFORMANCE_HISTOGRAM_SAMPLE_COUNT ) % PERFORMANCE_HISTOGRAM_SAMPLE_COUNT;
    const PerformanceHistogramSample& newest = m_performanceHistogramSamples[newestIndex];
    snprintf( text, sizeof( text ), "CPU %.2f", newest.cpuMs );
    draw.Text( panelX + 10.0f, panelY + panelH - 20.0f, 10.0f, 0.48f, 0.90f, 0.22f, text );
    snprintf( text, sizeof( text ), "GPU %.2f", newest.gpuMs );
    draw.Text( panelX + 96.0f, panelY + panelH - 20.0f, 10.0f, 0.34f, 0.91f, 1.0f, text );
}


void InGameUI::CloseSceneCombo()
{
    SceneTab::CloseCombo( m_sceneTab, m_sceneCombo );
}


InGameUIInputResult InGameUI::UpdateInput( HWND hwnd, int screenW, int screenH, double now, const char* const* sceneOptions, int sceneOptionCount, int selectedSceneOption )
{
    InGameUIInputResult result;
    m_interaction.blocksCameraMouse = false;
    const InputControl::UIInputSnapshot input = InputControl::CaptureSnapshot( m_interaction.leftWasDown, m_hasMouseOverride, m_mouseOverrideX, m_mouseOverrideY );
    const int wheelDelta = input.wheelDelta;
    if ( !m_window.isVisible )
    {
        result.SyncLegacyFields();
        return result;
    }
    ApplyProfilerDefaultExpansion();

    m_mouseX = input.mouseX;
    m_mouseY = input.mouseY;

    screenW = (std::max)( 1, screenW );
    screenH = (std::max)( 1, screenH );
    m_lastScreenW = screenW;
    m_lastScreenH = screenH;
    const int minW = 430;
    const int minH = 250;
    const int margin = 10;
    const int titleH = 44;
    const int tabH = 54;
    const int bottomH = 88;
    const int contentPad = 18;
    const int maxW = (std::max)( minW, screenW - margin * 2 );
    const int maxH = (std::max)( minH, screenH - margin * 2 );

    if ( !m_window.hasAppliedDefaultPlacement )
    {
        Chrome::ApplyDefaultWindowPlacement( m_window, screenW, screenH );
    }
    Chrome::ClampWindowToScreen( m_window, screenW, screenH, minW, minH, margin );

    const bool leftNow = input.leftDown;
    if ( m_window.isMinimized )
    {
        const UIRect minimized = MinimizedRect( screenW, screenH, m_window.minimizedWidth );
        const bool insideMinimized = minimized.Contains( m_mouseX, m_mouseY );
        if ( input.leftPressed && insideMinimized )
        {
            SetMinimized( false, now );
            result.commands.ui.userInteracted = true;
        }
        m_interaction.leftWasDown = leftNow;
        m_interaction.blocksCameraMouse = insideMinimized;
        result.SyncLegacyFields();
        return result;
    }

    const UIRect inputBounds = Chrome::CurrentWindowRect( m_window, now );
    const int inputX = static_cast<int>( std::round( inputBounds.x ) );
    const int inputY = static_cast<int>( std::round( inputBounds.y ) );
    const int inputW = static_cast<int>( std::round( inputBounds.w ) );
    const int inputH = static_cast<int>( std::round( inputBounds.h ) );
    const UIRect inputHitBounds = { static_cast<float>( inputX ), static_cast<float>( inputY ), static_cast<float>( inputW ), static_cast<float>( inputH ) };
    const bool inside = m_mouseX >= inputX && m_mouseX <= inputX + inputW &&
                        m_mouseY >= inputY && m_mouseY <= inputY + inputH;
    const bool inTitle = inside && m_mouseY < inputY + titleH;
    const bool inTabs = inside && m_mouseY >= inputY + titleH && m_mouseY < inputY + titleH + tabH;
    const bool inResize = !m_window.isMaximized && inside && Chrome::IsResizeHotspot( inputHitBounds, m_mouseX, m_mouseY );
    const int contentY = inputY + titleH + tabH + 12;
    const int contentH = (std::max)( 24, inputH - titleH - tabH - bottomH - contentPad );
    const int bottomY = inputY + inputH - bottomH;
    const bool inContent = inside && m_mouseY >= contentY && m_mouseY <= contentY + contentH;
    const float maxScroll = static_cast<float>( (std::max)( 0, ContentHeight() - contentH ) );
    const Chrome::TitleButtonRects titleButtons = Chrome::GetTitleButtonRects( inputHitBounds );

    m_tabBar.SetBounds( static_cast<float>( inputX + 14 ), static_cast<float>( inputY + titleH ), static_cast<float>( inputW - 28 ), static_cast<float>( tabH ) );
    const float footerX = static_cast<float>( inputX );
    const float footerY = static_cast<float>( bottomY );
    const UIRect rendererComboBounds = FooterRendererComboBounds( footerX, footerY );
    const UIRect waterComboBounds = FooterWaterComboBounds( footerX, footerY );
    const UIRect blurBounds = FooterBlurBounds( footerX, footerY );
    const UIRect vsyncBounds = FooterVsyncBounds( footerX, footerY );
    const UIRect timelineBounds = FooterTimelineBounds( footerX, footerY );
    const UIRect perfBounds = FooterPerfBounds( footerX, footerY );
    m_rendererCombo.SetBounds( rendererComboBounds.x, rendererComboBounds.y, rendererComboBounds.w, rendererComboBounds.h );
    m_rendererCombo.SetDropUp( true );
    m_reflectionCombo.SetBounds( waterComboBounds.x, waterComboBounds.y, waterComboBounds.w, waterComboBounds.h );
    m_reflectionCombo.SetDropUp( true );
    m_blurToggle.SetBounds( blurBounds.x, blurBounds.y, blurBounds.w, blurBounds.h );
    m_vsyncToggle.SetBounds( vsyncBounds.x, vsyncBounds.y, vsyncBounds.w, vsyncBounds.h );
    m_histogramToggle.SetBounds( perfBounds.x, perfBounds.y, perfBounds.w, perfBounds.h );
    m_timelineToggle.SetBounds( timelineBounds.x, timelineBounds.y, timelineBounds.w, timelineBounds.h );

    if ( ( leftNow && ( inside || m_interaction.isDragging || m_interaction.isResizing || m_activeSlider != 0 ) ) ||
         ( wheelDelta != 0 && inside ) )
    {
        result.commands.ui.userInteracted = true;
    }

    if ( m_activeTab == InGameUITab::Scene )
    {
        SceneTab::UpdateFilterTyping( m_sceneTab, m_sceneCombo, result, sceneOptions, sceneOptionCount );
    }

    bool wheelHandled = false;
    if ( wheelDelta != 0 && m_sceneCombo.IsOpen() && m_activeTab == InGameUITab::Scene )
    {
        const float contentX = static_cast<float>( inputX + contentPad );
        const float rowBase = static_cast<float>( contentY ) + 42.0f - m_scrollY;
        const float contentW = static_cast<float>( inputW ) - static_cast<float>( contentPad ) * 2.0f - 8.0f;
        wheelHandled = SceneTab::HandleComboWheel( m_sceneTab, m_sceneCombo, sceneOptions, sceneOptionCount, m_mouseX, m_mouseY, wheelDelta, contentX, rowBase, contentW );
    }

    if ( wheelDelta != 0 && inContent && !wheelHandled )
    {
        m_scrollY -= static_cast<float>( wheelDelta ) / static_cast<float>( WHEEL_DELTA ) * 42.0f;
        m_scrollY = std::clamp( m_scrollY, 0.0f, maxScroll );
        m_scrollbarVisibleUntil = now + 1.4;
    }

    if ( input.leftPressed )
    {
        if ( titleButtons.minimize.Contains( m_mouseX, m_mouseY ) || titleButtons.close.Contains( m_mouseX, m_mouseY ) )
        {
            SetMinimized( true, now );
        }
        else if ( titleButtons.maximize.Contains( m_mouseX, m_mouseY ) )
        {
            SetMaximized( !m_window.isMaximized, screenW, screenH, now );
        }
        else if ( inResize )
        {
            m_interaction.isResizing = true;
            m_interaction.resizeStartMouseX = m_mouseX;
            m_interaction.resizeStartMouseY = m_mouseY;
            m_interaction.resizeStartW = inputW;
            m_interaction.resizeStartH = inputH;
            InputControl::BeginMouseCapture( hwnd );
        }
        else if ( inTitle )
        {
            m_interaction.isDragging = true;
            m_interaction.dragOffsetX = m_mouseX - inputX;
            m_interaction.dragOffsetY = m_mouseY - inputY;
            InputControl::BeginMouseCapture( hwnd );
        }
        else if ( inTabs )
        {
            static const int kTabCount = static_cast<int>( InGameUITab::Count );
            const int index = m_tabBar.HitTest( m_mouseX, m_mouseY, kTabCount );
            if ( index >= 0 && index < kTabCount )
            {
                SetActiveTab( static_cast<InGameUITab>( index ) );
                m_scrollbarVisibleUntil = now + 1.0;
            }
        }
        else if ( m_sceneCombo.IsOpen() )
        {
            if ( m_activeTab == InGameUITab::Scene )
            {
                const float contentX = static_cast<float>( inputX + contentPad );
                const float rowBase = static_cast<float>( contentY ) + 42.0f - m_scrollY;
                const float contentW = static_cast<float>( inputW ) - static_cast<float>( contentPad ) * 2.0f - 8.0f;
                SceneTab::HandleOpenComboClick( m_sceneTab,
                                                m_sceneCombo,
                                                m_resetSceneButton,
                                                m_resetDefaultsButton,
                                                m_saveDefaultsButton,
                                                result,
                                                sceneOptions,
                                                sceneOptionCount,
                                                m_mouseX,
                                                m_mouseY,
                                                contentX,
                                                rowBase,
                                                contentW );
            }
            else
            {
                CloseSceneCombo();
            }
            m_rendererCombo.Close();
            m_reflectionCombo.Close();
        }
        else if ( m_reflectionCombo.IsOpen() )
        {
            const int option = m_reflectionCombo.HitOption( m_mouseX, m_mouseY, 3 );
            const bool isDXRDisabled = option == 1 && m_lastRendererIndex != RENDERER_DX12;
            if ( option >= 0 && option < 3 && !isDXRDisabled )
            {
                result.commands.water.requestedWaterReflectionMode = option;
                m_reflectionCombo.Close();
            }
            else if ( m_reflectionCombo.HitBox( m_mouseX, m_mouseY ) )
            {
                m_reflectionCombo.ToggleOpen();
            }
            else
            {
                m_reflectionCombo.Close();
            }
            m_rendererCombo.Close();
            CloseSceneCombo();
        }
        else if ( m_rendererCombo.IsOpen() )
        {
            const int option = m_rendererCombo.HitOption( m_mouseX, m_mouseY, 3 );
            if ( option >= 0 && option < 3 )
            {
                result.commands.renderer.requestedRendererIndex = option;
                m_rendererCombo.Close();
            }
            else if ( m_rendererCombo.HitBox( m_mouseX, m_mouseY ) )
            {
                m_rendererCombo.ToggleOpen();
                m_reflectionCombo.Close();
                CloseSceneCombo();
            }
            else if ( !m_rendererCombo.HitBox( m_mouseX, m_mouseY ) )
            {
                m_rendererCombo.Close();
            }
        }
        else if ( inContent && m_activeTab == InGameUITab::Profiler )
        {
            const int headerH = 32;
            const int rowH = 30;
            const int localY = static_cast<int>( static_cast<float>( m_mouseY - contentY ) + m_scrollY );
            const int targetRow = ( localY - headerH ) / rowH;
            if ( targetRow >= 0 )
            {
                const Profiler& profiler = Profiler::Instance();
                int visibleRows[PROFILER_UI_MAX_MARKERS] = {};
                const int visibleRowCount = BuildVisibleProfilerRows( visibleRows, PROFILER_UI_MAX_MARKERS );
                if ( targetRow < visibleRowCount )
                {
                    const int markerIndex = visibleRows[targetRow];
                    const Profiler::Marker& marker = profiler.GetMarker( markerIndex );
                    if ( ProfilerMarkerHasChildren( profiler, markerIndex ) )
                    {
                        const float plusX = static_cast<float>( inputX + contentPad + 18 + marker.depth * 18 );
                        const float plusY = static_cast<float>( contentY + headerH + targetRow * rowH ) - m_scrollY + 8.0f;
                        UIIconButton expander;
                        expander.SetBounds( plusX, plusY, 14.0f, 14.0f );
                        if ( expander.HitTest( m_mouseX, m_mouseY ) )
                        {
                            ToggleProfilerMarker( marker.hash );
                            m_scrollbarVisibleUntil = now + 1.2;
                        }
                    }
                }
            }
            m_rendererCombo.Close();
            CloseSceneCombo();
        }
        else if ( inContent && m_activeTab == InGameUITab::Scene )
        {
            const float contentX = static_cast<float>( inputX + contentPad );
            const float rowBase = static_cast<float>( contentY ) + 42.0f - m_scrollY;
            const float contentW = static_cast<float>( inputW ) - static_cast<float>( contentPad ) * 2.0f - 8.0f;
            const bool sceneClickHandled = SceneTab::HandleContentClick( m_sceneTab,
                                                                         m_sceneCombo,
                                                                         m_resetSceneButton,
                                                                         m_resetDefaultsButton,
                                                                         m_saveDefaultsButton,
                                                                         result,
                                                                         sceneOptions,
                                                                         sceneOptionCount,
                                                                         selectedSceneOption,
                                                                         m_mouseX,
                                                                         m_mouseY,
                                                                         contentX,
                                                                         rowBase,
                                                                         contentW );
            m_rendererCombo.Close();
            if ( sceneClickHandled )
            {
                m_reflectionCombo.Close();
            }
        }
        else if ( inContent && m_activeTab == InGameUITab::Physics )
        {
            const float contentX = static_cast<float>( inputX + contentPad );
            const float rowBase = static_cast<float>( contentY ) + 42.0f - m_scrollY;
            const float contentW = static_cast<float>( inputW ) - static_cast<float>( contentPad ) * 2.0f - 8.0f;
            const float colW = (std::max)( 148.0f, contentW * 0.46f );
            const float col1 = contentX;
            const float col2 = contentX + colW + 18.0f;
            const auto setToggle = [&]( int index, int row, int column ) -> void
            {
                const float tx = column == 0 ? col1 : col2;
                m_physicsToggles[index].SetBounds( tx, rowBase + static_cast<float>( row ) * CONTENT_TOGGLE_ROW_H, colW, 24.0f );
            };

            setToggle( 0, 0, 0 );
            setToggle( 4, 1, 0 );
            setToggle( 5, 2, 0 );
            setToggle( 7, 3, 0 );
            setToggle( 1, 0, 1 );
            setToggle( 2, 1, 1 );
            setToggle( 3, 2, 1 );
            setToggle( 6, 3, 1 );
            SetPipelineStepButtonBounds( m_pipelinePrevButton, m_pipelineNextButton, contentX, contentW, rowBase + 194.0f );
            m_physicsAlphaSlider.SetBounds( contentX, rowBase + 242.0f, contentW, 34.0f );
            m_contactLingerSlider.SetBounds( contentX, rowBase + 290.0f, contentW, 34.0f );
            m_worldGravitySlider.SetBounds( contentX, rowBase + 374.0f, contentW, 34.0f );

            if ( m_physicsToggles[0].HitTest( m_mouseX, m_mouseY ) )
            {
                result.commands.physics.toggleCollisionVisualizer = true;
            }
            else if ( m_physicsToggles[1].HitTest( m_mouseX, m_mouseY ) )
            {
                result.commands.physics.togglePhysicsDebugFlags = PHYSICS_DEBUG_AXES;
            }
            else if ( m_physicsToggles[2].HitTest( m_mouseX, m_mouseY ) )
            {
                result.commands.physics.togglePhysicsDebugFlags = PHYSICS_DEBUG_CONTACTS;
            }
            else if ( m_physicsToggles[3].HitTest( m_mouseX, m_mouseY ) )
            {
                result.commands.physics.togglePhysicsDebugFlags = PHYSICS_DEBUG_SLEEP;
            }
            else if ( m_physicsToggles[4].HitTest( m_mouseX, m_mouseY ) )
            {
                result.commands.physics.togglePhysicsDebugTransparent = true;
            }
            else if ( m_physicsToggles[5].HitTest( m_mouseX, m_mouseY ) )
            {
                result.commands.physics.toggleBroadphaseOverlay = true;
            }
            else if ( m_physicsToggles[7].HitTest( m_mouseX, m_mouseY ) )
            {
                result.commands.physics.togglePhysicsDebugFlags = PHYSICS_DEBUG_PIPELINE;
            }
            else if ( m_physicsToggles[6].HitTest( m_mouseX, m_mouseY ) )
            {
                result.commands.physics.togglePhysicsSleepPolicy = true;
            }
            else if ( m_pipelinePrevButton.Contains( m_mouseX, m_mouseY ) )
            {
                result.commands.physics.stepPhysicsPipelinePrevious = true;
            }
            else if ( m_pipelineNextButton.Contains( m_mouseX, m_mouseY ) )
            {
                result.commands.physics.stepPhysicsPipelineNext = true;
            }
            else if ( m_physicsAlphaSlider.HitTest( m_mouseX, m_mouseY ) )
            {
                m_activeSlider = 3;
                m_previewPhysicsAlpha = m_physicsAlphaSlider.ValueFromMouse( m_mouseX, UI_PHYSICS_ALPHA_MIN, UI_PHYSICS_ALPHA_MAX, UI_PHYSICS_ALPHA_STEP );
                result.commands.physics.requestedPhysicsDebugAlpha = m_previewPhysicsAlpha;
                InputControl::BeginMouseCapture( hwnd );
            }
            else if ( m_contactLingerSlider.HitTest( m_mouseX, m_mouseY ) )
            {
                m_activeSlider = 4;
                m_previewContactLinger = m_contactLingerSlider.ValueFromMouse( m_mouseX, UI_CONTACT_LINGER_MIN, UI_CONTACT_LINGER_MAX, UI_CONTACT_LINGER_STEP );
                result.commands.physics.requestedPhysicsDebugContactLinger = m_previewContactLinger;
                InputControl::BeginMouseCapture( hwnd );
            }
            else if ( m_worldGravitySlider.HitTest( m_mouseX, m_mouseY ) )
            {
                m_activeSlider = 11;
                result.commands.water.requestWorldGravity = true;
                result.commands.water.requestedWorldGravity = WorldGravityFromStrength( m_worldGravitySlider.ValueFromMouse( m_mouseX,
                                                                                                             UI_WORLD_GRAVITY_MIN,
                                                                                                             UI_WORLD_GRAVITY_MAX,
                                                                                                             UI_WORLD_GRAVITY_STEP ) );
                InputControl::BeginMouseCapture( hwnd );
            }
            m_rendererCombo.Close();
        }
        else if ( inContent && m_activeTab == InGameUITab::Options )
        {
            const float contentX = static_cast<float>( inputX + contentPad );
            const float rowBase = static_cast<float>( contentY ) + 42.0f - m_scrollY;
            const float contentW = static_cast<float>( inputW ) - static_cast<float>( contentPad ) * 2.0f - 8.0f;
            const float colW = (std::max)( 148.0f, contentW * 0.46f );
            const float col1 = contentX;
            const float col2 = contentX + colW + 18.0f;
            const auto setToggle = [&]( int index, int row, int column ) -> void
            {
                const float tx = column == 0 ? col1 : col2;
                m_optionToggles[index].SetBounds( tx, rowBase + static_cast<float>( row ) * CONTENT_TOGGLE_ROW_H, colW, 24.0f );
            };

            setToggle( 0, 0, 0 );
            setToggle( 1, 0, 1 );
            setToggle( 2, 1, 0 );
            setToggle( 3, 1, 1 );
            setToggle( 4, 2, 0 );
            m_timeScaleSlider.SetBounds( contentX, rowBase + 126.0f, contentW, 34.0f );
            m_modelCountSlider.SetBounds( contentX, rowBase + 174.0f, contentW, 34.0f );

            if ( m_optionToggles[0].HitTest( m_mouseX, m_mouseY ) )
            {
                result.commands.sceneOptions.toggleFixedStep = true;
            }
            else if ( m_optionToggles[1].HitTest( m_mouseX, m_mouseY ) )
            {
                result.commands.sceneOptions.toggleTerrainHidden = true;
            }
            else if ( m_optionToggles[2].HitTest( m_mouseX, m_mouseY ) )
            {
                result.commands.sceneOptions.toggleWaterHidden = true;
            }
            else if ( m_optionToggles[3].HitTest( m_mouseX, m_mouseY ) )
            {
                result.commands.sceneOptions.toggleWaterFreeze = true;
            }
            else if ( m_optionToggles[4].HitTest( m_mouseX, m_mouseY ) )
            {
                result.commands.sceneOptions.toggleWaterFlat = true;
            }
            else if ( m_timeScaleSlider.HitTest( m_mouseX, m_mouseY ) )
            {
                m_activeSlider = 1;
                m_previewTimeScale = m_timeScaleSlider.ValueFromMouse( m_mouseX, UI_TIME_SCALE_MIN, UI_TIME_SCALE_MAX, UI_TIME_SCALE_STEP );
                result.commands.sceneOptions.requestedTimeScale = m_previewTimeScale;
                InputControl::BeginMouseCapture( hwnd );
            }
            else if ( m_modelCountSlider.HitTest( m_mouseX, m_mouseY ) )
            {
                m_activeSlider = 2;
                m_previewModelCount = static_cast<int>( m_modelCountSlider.ValueFromMouse( m_mouseX,
                                                                                           static_cast<float>( UI_MODEL_COUNT_MIN ),
                                                                                           static_cast<float>( UI_MODEL_COUNT_MAX ),
                                                                                           1.0f ) );
                InputControl::BeginMouseCapture( hwnd );
            }
            m_rendererCombo.Close();
            m_reflectionCombo.Close();
        }
        else if ( inContent && m_activeTab == InGameUITab::Keys )
        {
            const float contentX = static_cast<float>( inputX + contentPad );
            const float rowBase = static_cast<float>( contentY ) + 42.0f - m_scrollY;
            const float contentW = static_cast<float>( inputW ) - static_cast<float>( contentPad ) * 2.0f - 8.0f;
            const int displayBalls = m_previewSolverBallCount >= 0 ? m_previewSolverBallCount : m_lastSolverBallCount;
            const int displayBoxes = m_previewSolverBoxCount >= 0 ? m_previewSolverBoxCount : m_lastSolverBoxCount;

            m_seedSlider.SetBounds( contentX, rowBase, contentW, 34.0f );
            m_solverBallSlider.SetBounds( contentX, rowBase + 88.0f, contentW, 34.0f );
            m_solverBoxSlider.SetBounds( contentX, rowBase + 128.0f, contentW, 34.0f );
            m_worldFluidHeightSlider.SetBounds( contentX, rowBase + 210.0f, contentW, 34.0f );
            m_worldFluidDensitySlider.SetBounds( contentX, rowBase + 250.0f, contentW, 34.0f );

            if ( m_seedSlider.HitTest( m_mouseX, m_mouseY ) )
            {
                m_activeSlider = 6;
                result.commands.run.requestedSeed = static_cast<int>( m_seedSlider.ValueFromMouse( m_mouseX,
                                                                                      static_cast<float>( UI_SEED_MIN ),
                                                                                      static_cast<float>( UI_SEED_MAX ),
                                                                                      1.0f ) );
                InputControl::BeginMouseCapture( hwnd );
            }
            else if ( m_solverBallSlider.HitTest( m_mouseX, m_mouseY ) )
            {
                m_activeSlider = 7;
                const int maxBalls = RemainingGameModelSlots( displayBoxes );
                m_previewSolverBallCount = static_cast<int>( m_solverBallSlider.ValueFromMouse( m_mouseX,
                                                                                                static_cast<float>( UI_SOLVER_COUNT_MIN ),
                                                                                                static_cast<float>( maxBalls ),
                                                                                                1.0f ) );
                InputControl::BeginMouseCapture( hwnd );
            }
            else if ( m_solverBoxSlider.HitTest( m_mouseX, m_mouseY ) )
            {
                m_activeSlider = 8;
                const int maxBoxes = RemainingGameModelSlots( displayBalls );
                m_previewSolverBoxCount = static_cast<int>( m_solverBoxSlider.ValueFromMouse( m_mouseX,
                                                                                              static_cast<float>( UI_SOLVER_COUNT_MIN ),
                                                                                              static_cast<float>( maxBoxes ),
                                                                                              1.0f ) );
                InputControl::BeginMouseCapture( hwnd );
            }
            else if ( m_worldFluidHeightSlider.HitTest( m_mouseX, m_mouseY ) )
            {
                m_activeSlider = 12;
                result.commands.water.requestWorldFluidHeight = true;
                result.commands.water.requestedWorldFluidHeight = m_worldFluidHeightSlider.ValueFromMouse( m_mouseX, UI_WORLD_FLUID_HEIGHT_MIN, UI_WORLD_FLUID_HEIGHT_MAX, UI_WORLD_FLUID_HEIGHT_STEP );
                InputControl::BeginMouseCapture( hwnd );
            }
            else if ( m_worldFluidDensitySlider.HitTest( m_mouseX, m_mouseY ) )
            {
                m_activeSlider = 13;
                result.commands.water.requestWorldFluidDensity = true;
                result.commands.water.requestedWorldFluidDensity = m_worldFluidDensitySlider.ValueFromMouse( m_mouseX, UI_WORLD_FLUID_DENSITY_MIN, UI_WORLD_FLUID_DENSITY_MAX, UI_WORLD_FLUID_DENSITY_STEP );
                InputControl::BeginMouseCapture( hwnd );
            }
            m_rendererCombo.Close();
            m_reflectionCombo.Close();
        }
        else if ( inside && m_mouseY >= inputY + inputH - bottomH )
        {
            if ( m_rendererCombo.HitBox( m_mouseX, m_mouseY ) )
            {
                m_rendererCombo.ToggleOpen();
                m_reflectionCombo.Close();
                CloseSceneCombo();
            }
            else if ( m_reflectionCombo.HitBox( m_mouseX, m_mouseY ) )
            {
                m_reflectionCombo.ToggleOpen();
                m_rendererCombo.Close();
                CloseSceneCombo();
            }
            else if ( m_blurToggle.HitTest( m_mouseX, m_mouseY ) )
            {
                m_blurPreviewEnabled = !m_blurPreviewEnabled;
                m_backdropBlur.Invalidate();
            }
            else if ( m_vsyncToggle.HitTest( m_mouseX, m_mouseY ) )
            {
                result.commands.renderer.toggleVsync = true;
            }
            else if ( m_histogramToggle.HitTest( m_mouseX, m_mouseY ) )
            {
                SetPerformanceHistogramEnabled( !m_performanceHistogramEnabled );
            }
            else if ( m_timelineToggle.HitTest( m_mouseX, m_mouseY ) )
            {
                m_profilerTimelineEnabled = !m_profilerTimelineEnabled;
            }
        }
        else
        {
            m_rendererCombo.Close();
            m_reflectionCombo.Close();
        }
    }

    if ( leftNow && m_activeSlider == 1 )
    {
        // Sliders update previews continuously while dragged.  Heavy operations
        // such as rebuilding generated bodies are delayed until mouse release,
        // but cheap scalar controls are emitted every frame for immediate feedback.
        m_previewTimeScale = m_timeScaleSlider.ValueFromMouse( m_mouseX, UI_TIME_SCALE_MIN, UI_TIME_SCALE_MAX, UI_TIME_SCALE_STEP );
        result.commands.sceneOptions.requestedTimeScale = m_previewTimeScale;
    }
    else if ( leftNow && m_activeSlider == 2 )
    {
        m_previewModelCount = static_cast<int>( m_modelCountSlider.ValueFromMouse( m_mouseX,
                                                                                   static_cast<float>( UI_MODEL_COUNT_MIN ),
                                                                                   static_cast<float>( UI_MODEL_COUNT_MAX ),
                                                                                   1.0f ) );
    }
    else if ( leftNow && m_activeSlider == 3 )
    {
        m_previewPhysicsAlpha = m_physicsAlphaSlider.ValueFromMouse( m_mouseX, UI_PHYSICS_ALPHA_MIN, UI_PHYSICS_ALPHA_MAX, UI_PHYSICS_ALPHA_STEP );
        result.commands.physics.requestedPhysicsDebugAlpha = m_previewPhysicsAlpha;
    }
    else if ( leftNow && m_activeSlider == 4 )
    {
        m_previewContactLinger = m_contactLingerSlider.ValueFromMouse( m_mouseX, UI_CONTACT_LINGER_MIN, UI_CONTACT_LINGER_MAX, UI_CONTACT_LINGER_STEP );
        result.commands.physics.requestedPhysicsDebugContactLinger = m_previewContactLinger;
    }
    else if ( leftNow && m_activeSlider == 6 )
    {
        result.commands.run.requestedSeed = static_cast<int>( m_seedSlider.ValueFromMouse( m_mouseX,
                                                                              static_cast<float>( UI_SEED_MIN ),
                                                                              static_cast<float>( UI_SEED_MAX ),
                                                                              1.0f ) );
    }
    else if ( leftNow && m_activeSlider == 7 )
    {
        const int boxes = m_previewSolverBoxCount >= 0 ? m_previewSolverBoxCount : m_lastSolverBoxCount;
        const int maxBalls = RemainingGameModelSlots( boxes );
        m_previewSolverBallCount = static_cast<int>( m_solverBallSlider.ValueFromMouse( m_mouseX,
                                                                                        static_cast<float>( UI_SOLVER_COUNT_MIN ),
                                                                                        static_cast<float>( maxBalls ),
                                                                                        1.0f ) );
    }
    else if ( leftNow && m_activeSlider == 8 )
    {
        const int balls = m_previewSolverBallCount >= 0 ? m_previewSolverBallCount : m_lastSolverBallCount;
        const int maxBoxes = RemainingGameModelSlots( balls );
        m_previewSolverBoxCount = static_cast<int>( m_solverBoxSlider.ValueFromMouse( m_mouseX,
                                                                                      static_cast<float>( UI_SOLVER_COUNT_MIN ),
                                                                                      static_cast<float>( maxBoxes ),
                                                                                      1.0f ) );
    }
    else if ( leftNow && m_activeSlider == 11 )
    {
        result.commands.water.requestWorldGravity = true;
        result.commands.water.requestedWorldGravity = WorldGravityFromStrength( m_worldGravitySlider.ValueFromMouse( m_mouseX,
                                                                                                     UI_WORLD_GRAVITY_MIN,
                                                                                                     UI_WORLD_GRAVITY_MAX,
                                                                                                     UI_WORLD_GRAVITY_STEP ) );
    }
    else if ( leftNow && m_activeSlider == 12 )
    {
        result.commands.water.requestWorldFluidHeight = true;
        result.commands.water.requestedWorldFluidHeight = m_worldFluidHeightSlider.ValueFromMouse( m_mouseX, UI_WORLD_FLUID_HEIGHT_MIN, UI_WORLD_FLUID_HEIGHT_MAX, UI_WORLD_FLUID_HEIGHT_STEP );
    }
    else if ( leftNow && m_activeSlider == 13 )
    {
        result.commands.water.requestWorldFluidDensity = true;
        result.commands.water.requestedWorldFluidDensity = m_worldFluidDensitySlider.ValueFromMouse( m_mouseX, UI_WORLD_FLUID_DENSITY_MIN, UI_WORLD_FLUID_DENSITY_MAX, UI_WORLD_FLUID_DENSITY_STEP );
    }

    if ( leftNow && m_interaction.isDragging )
    {
        const int oldX = m_window.x;
        const int oldY = m_window.y;
        m_window.x = std::clamp( m_mouseX - m_interaction.dragOffsetX, margin, (std::max)( margin, screenW - m_window.width - margin ) );
        m_window.y = std::clamp( m_mouseY - m_interaction.dragOffsetY, margin, (std::max)( margin, screenH - m_window.height - margin ) );
        if ( oldX != m_window.x || oldY != m_window.y )
        {
            m_backdropBlur.Invalidate();
        }
    }
    if ( leftNow && m_interaction.isResizing )
    {
        const int oldW = m_window.width;
        const int oldH = m_window.height;
        m_window.width = std::clamp( m_interaction.resizeStartW + m_mouseX - m_interaction.resizeStartMouseX, minW, maxW );
        m_window.height = std::clamp( m_interaction.resizeStartH + m_mouseY - m_interaction.resizeStartMouseY, minH, maxH );
        m_scrollbarVisibleUntil = now + 1.4;
        if ( oldW != m_window.width || oldH != m_window.height )
        {
            m_backdropBlur.Invalidate();
        }
    }

    if ( input.leftReleased )
    {
        // Commit deferred slider previews exactly once on release.  This avoids
        // rebuilding solver objects or generated model pools every mouse-move
        // while still letting the drawn slider thumb track the user's drag.
        if ( m_activeSlider == 1 && m_previewTimeScale > 0.0f )
        {
            result.commands.sceneOptions.requestedTimeScale = m_previewTimeScale;
        }
        else if ( m_activeSlider == 2 && m_previewModelCount >= 0 )
        {
            result.commands.sceneOptions.requestedModelCount = m_previewModelCount;
        }
        else if ( m_activeSlider == 3 && m_previewPhysicsAlpha >= 0.0f )
        {
            result.commands.physics.requestedPhysicsDebugAlpha = m_previewPhysicsAlpha;
        }
        else if ( m_activeSlider == 4 && m_previewContactLinger >= 0.0f )
        {
            result.commands.physics.requestedPhysicsDebugContactLinger = m_previewContactLinger;
        }
        else if ( m_activeSlider == 7 && m_previewSolverBallCount >= 0 )
        {
            result.commands.run.requestedSolverBallCount = m_previewSolverBallCount;
        }
        else if ( m_activeSlider == 8 && m_previewSolverBoxCount >= 0 )
        {
            result.commands.run.requestedSolverBoxCount = m_previewSolverBoxCount;
        }
        m_activeSlider = 0;
        m_interaction.isDragging = false;
        m_interaction.isResizing = false;
        InputControl::EndMouseCapture();
    }

    m_interaction.leftWasDown = leftNow;
    m_scrollY = std::clamp( m_scrollY, 0.0f, maxScroll );
    m_interaction.blocksCameraMouse = inside || m_interaction.isDragging || m_interaction.isResizing || m_activeSlider != 0;
    result.SyncLegacyFields();
    return result;
}


void InGameUI::Draw( const InGameUIFrameData& data )
{
    if ( !m_window.isVisible )
    {
        return;
    }

    const int screenW = (std::max)( 1, data.screenW );
    const int screenH = (std::max)( 1, data.screenH );
    m_lastScreenW = screenW;
    m_lastScreenH = screenH;
    m_lastSolverBallCount = std::clamp( data.solverBallCount, UI_SOLVER_COUNT_MIN, UI_GAME_MODEL_TOTAL_MAX );
    m_lastSolverBoxCount = std::clamp( data.solverBoxCount, UI_SOLVER_COUNT_MIN, UI_GAME_MODEL_TOTAL_MAX );
    const int currentRendererIndex = GetRendererIndexFromName( data.rendererName );
    m_lastRendererIndex = currentRendererIndex;
    const UIDrawContext draw( screenW, screenH );
    const bool shouldDrawCursor = !data.cameraMouseActive && !data.nativeCursorVisible;
    if ( m_performanceHistogramEnabled )
    {
        PushPerformanceHistogramSample( data.cpuFrameMs, data.gpuFrameMs );
    }

    if ( m_window.isMinimized )
    {
        if ( m_window.animationActive && m_window.animationToMinimized )
        {
            const UIRect animBounds = Chrome::CurrentWindowRect( m_window, data.now );
            if ( m_window.animationActive )
            {
                Chrome::DrawWindowAnimationShell( draw, animBounds );
                if ( m_performanceHistogramEnabled )
                {
                    DrawPerformanceHistogram( draw, data );
                }
                Text2d::FlushQuads();
                Text2d::FlushText();
                if ( shouldDrawCursor )
                {
                    DrawCursor( draw );
                    Text2d::FlushQuads();
                }
                return;
            }
        }

        char titleText[192] = {};
        Chrome::BuildWindowTitle( data, titleText, sizeof( titleText ) );
        m_window.minimizedWidth = MinimizedWidthForTitle( titleText, screenW );
        const UIRect minimized = MinimizedRect( screenW, screenH, m_window.minimizedWidth );
        Chrome::FitTitleText( titleText, sizeof( titleText ), 12.5f, minimized.w - 76.0f );
        Chrome::DrawMinimizedWindow( draw, minimized, titleText );
        if ( m_performanceHistogramEnabled )
        {
            DrawPerformanceHistogram( draw, data );
        }
        Text2d::FlushQuads();
        Text2d::FlushText();
        if ( shouldDrawCursor )
        {
            DrawCursor( draw );
            Text2d::FlushQuads();
        }
        return;
    }

    const UIRect windowBounds = Chrome::CurrentWindowRect( m_window, data.now );
    const float x = windowBounds.x;
    const float y = windowBounds.y;
    const float w = windowBounds.w;
    const float h = windowBounds.h;
    const float titleH = 44.0f;
    const float tabH = 54.0f;
    const float bottomH = 88.0f;
    const float pad = 18.0f;
    const float contentX = x + pad;
    const float contentY = y + titleH + tabH + 12.0f;
    const float contentW = w - pad * 2.0f - 8.0f;
    const float contentH = (std::max)( 30.0f, h - titleH - tabH - bottomH - pad );
    const float scrolledY = contentY - m_scrollY;
    char titleText[192] = {};
    Chrome::BuildWindowTitle( data, titleText, sizeof( titleText ) );
    const bool useTitleStats = w - 36.0f < 560.0f;
    char titleStat[32] = {};
    float titleStatW = 0.0f;
    float titleStatX = 0.0f;
    float titleMaxW = w - 150.0f;
    if ( useTitleStats )
    {
        snprintf( titleStat, sizeof( titleStat ), "%.0f FPS", data.fps );
        titleStatW = Text2d::MeasureText( 10.5f, titleStat );
        titleStatX = (std::max)( x + 148.0f, x + w - 128.0f - titleStatW );
        titleMaxW = titleStatX - ( x + 20.0f ) - 10.0f;
    }
    Chrome::FitTitleText( titleText, sizeof( titleText ), 15.5f, (std::max)( 40.0f, titleMaxW ) );
    ApplyProfilerDefaultExpansion();
    ApplyProfilerExpandAll();

    const UIRect blurBounds = { x, y, w, h };
    Text2d::FlushQuads();
    m_backdropBlur.Draw( draw, blurBounds, screenW, screenH, data.currentFrame, data.now, m_blurPreviewEnabled );

    Chrome::DrawWindowFrame( draw, windowBounds, titleH, tabH, m_blurPreviewEnabled, titleText );
    Chrome::DrawTitleButtons( draw, Chrome::GetTitleButtonRects( windowBounds ), m_window.isMaximized, m_mouseX, m_mouseY );

    static const char* kTabs[] = { "Profile", "Scene", "Physics", "Options", "Controls" };
    const int tabCount = static_cast<int>( InGameUITab::Count );
    const float tabPad = 14.0f;
    m_tabBar.SetBounds( x + tabPad, y + titleH, w - tabPad * 2.0f, tabH );
    m_tabBar.Draw( draw, kTabs, tabCount, static_cast<int>( m_activeTab ) );

    draw.Rect( contentX - 10.0f, contentY - 10.0f, contentW + 20.0f, contentH + 12.0f, 0.010f, 0.020f, 0.028f, 0.56f );
    draw.Outline( contentX - 10.0f, contentY - 10.0f, contentW + 20.0f, contentH + 12.0f, 0.16f, 0.28f, 0.34f, 0.62f );

    auto visible = [&]( float rowY, float rowH ) -> bool
    {
        return IsRowVisible( contentY, contentH, rowY, rowH );
    };
    if ( m_activeTab == InGameUITab::Profiler )
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
        draw.Text( barX, tableY + 10.0f, 10.5f, 0.68f, 0.78f, 0.82f, m_profilerTimelineEnabled ? "Span" : "0 ms" );
        draw.Text( barX + barW - 44.0f, tableY + 10.0f, 10.5f, 0.68f, 0.78f, 0.82f, m_profilerTimelineEnabled ? "Frame" : "16.67 ms" );

        int visibleRows[PROFILER_UI_MAX_MARKERS] = {};
        const int visibleRowCount = BuildVisibleProfilerRows( visibleRows, PROFILER_UI_MAX_MARKERS );
        ProfilerTimelineSegment timelineSegments[PROFILER_UI_MAX_MARKERS] = {};
        BuildProfilerTimelineSegments( visibleRows, visibleRowCount, timelineSegments );

        auto profilerRow = [&]( int rowIndex, const Profiler::Marker& marker, const ProfilerTimelineSegment& segment, bool hasChildren, bool isExpanded )
        {
            const float rowY = tableY + headerH + static_cast<float>( rowIndex ) * rowH - m_scrollY;
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
            if ( m_profilerTimelineEnabled )
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
            profilerRow( visibleRow, marker, timelineSegments[visibleRow], hasChildren, IsProfilerMarkerExpanded( marker.hash ) );
        }
    }
    else if ( m_activeTab == InGameUITab::Scene )
    {
        SceneTab::Draw( m_sceneTab,
                        m_sceneCombo,
                        m_resetSceneButton,
                        m_resetDefaultsButton,
                        m_saveDefaultsButton,
                        draw,
                        data,
                        contentX,
                        contentY,
                        contentW,
                        contentH,
                        scrolledY,
                        m_mouseX,
                        m_mouseY );
    }
    else if ( m_activeTab == InGameUITab::Physics )
    {
        char buf[128];
        const float colW = (std::max)( 148.0f, contentW * 0.46f );
        const float col1 = contentX;
        const float col2 = contentX + colW + 18.0f;
        const float displayAlpha = ( m_activeSlider == 3 && m_previewPhysicsAlpha >= 0.0f ) ? m_previewPhysicsAlpha : data.physicsDebugAlpha;
        const float displayLinger = ( m_activeSlider == 4 && m_previewContactLinger >= 0.0f ) ? m_previewContactLinger : data.physicsDebugContactLinger;
        DrawSectionTitle( draw, contentX, contentY, contentH, scrolledY, 16.0f, "Physics Controls" );
        DrawContentToggle( draw, contentY, contentH, m_physicsToggles[0], col1, scrolledY + 42.0f, colW, "Collision state", data.collisionVisualizer );
        DrawContentToggle( draw, contentY, contentH, m_physicsToggles[4], col1, scrolledY + 72.0f, colW, "Transparent", data.physicsDebugTransparent );
        DrawContentToggle( draw, contentY, contentH, m_physicsToggles[5], col1, scrolledY + 102.0f, colW, "Broadphase", data.broadphaseOverlay );
        DrawContentToggle( draw, contentY, contentH, m_physicsToggles[7], col1, scrolledY + 132.0f, colW, "Pipeline", ( data.physicsDebugFlags & PHYSICS_DEBUG_PIPELINE ) != 0 );
        DrawContentToggle( draw, contentY, contentH, m_physicsToggles[1], col2, scrolledY + 42.0f, colW, "Axes", ( data.physicsDebugFlags & PHYSICS_DEBUG_AXES ) != 0 );
        DrawContentToggle( draw, contentY, contentH, m_physicsToggles[2], col2, scrolledY + 72.0f, colW, "Contacts", ( data.physicsDebugFlags & PHYSICS_DEBUG_CONTACTS ) != 0 );
        DrawContentToggle( draw, contentY, contentH, m_physicsToggles[3], col2, scrolledY + 102.0f, colW, "Sleep state", ( data.physicsDebugFlags & PHYSICS_DEBUG_SLEEP ) != 0 );
        DrawContentToggle( draw, contentY, contentH, m_physicsToggles[6], col2, scrolledY + 132.0f, colW, "Sleep policy", data.physicsSleepEnabled );
        snprintf( buf, sizeof( buf ), "0x%04X", data.physicsDebugFlags );
        DrawLabelValueAt( draw, contentY, contentH, contentX, scrolledY + 178.0f, "Debug flags", buf, 0.52f, 0.94f, 1.0f );
        snprintf( buf, sizeof( buf ), "%d/%d %s", data.physicsPipelineStageIndex + 1, data.physicsPipelineStageCount, data.physicsPipelineStageName );
        DrawLabelValueAt( draw, contentY, contentH, contentX, scrolledY + 198.0f, "Pipeline stage", buf, 0.52f, 0.94f, 1.0f );
        SetPipelineStepButtonBounds( m_pipelinePrevButton, m_pipelineNextButton, contentX, contentW, scrolledY + 194.0f );
        if ( visible( scrolledY + 194.0f, UI_PIPELINE_STEP_BUTTON_H ) )
        {
            DrawPipelineStepButton( draw, m_pipelinePrevButton, true, m_pipelinePrevButton.Contains( m_mouseX, m_mouseY ) );
            DrawPipelineStepButton( draw, m_pipelineNextButton, false, m_pipelineNextButton.Contains( m_mouseX, m_mouseY ) );
        }
        if ( visible( scrolledY + 216.0f, 18.0f ) )
        {
            DrawSectionTitle( draw, contentX, contentY, contentH, scrolledY + 216.0f, 12.0f, "Debug Draw" );
        }
        snprintf( buf, sizeof( buf ), "%.2f", displayAlpha );
        m_physicsAlphaSlider.SetBounds( contentX, scrolledY + 242.0f, contentW, 34.0f );
        if ( visible( scrolledY + 242.0f, 34.0f ) )
        {
            m_physicsAlphaSlider.Draw( draw, "Body alpha", buf, displayAlpha, UI_PHYSICS_ALPHA_MIN, UI_PHYSICS_ALPHA_MAX );
        }
        snprintf( buf, sizeof( buf ), "%.2fs", displayLinger );
        m_contactLingerSlider.SetBounds( contentX, scrolledY + 290.0f, contentW, 34.0f );
        if ( visible( scrolledY + 290.0f, 34.0f ) )
        {
            m_contactLingerSlider.Draw( draw, "Contact linger", buf, displayLinger, UI_CONTACT_LINGER_MIN, UI_CONTACT_LINGER_MAX );
        }
        if ( visible( scrolledY + 348.0f, 18.0f ) )
        {
            DrawSectionTitle( draw, contentX, contentY, contentH, scrolledY + 348.0f, 12.0f, "World" );
        }
        const float displayGravityStrength = GravityStrengthFromWorld( data.worldGravity );
        snprintf( buf, sizeof( buf ), "%.1f", displayGravityStrength );
        m_worldGravitySlider.SetBounds( contentX, scrolledY + 374.0f, contentW, 34.0f );
        if ( visible( scrolledY + 374.0f, 34.0f ) )
        {
            m_worldGravitySlider.Draw( draw, "Gravity", buf, displayGravityStrength, UI_WORLD_GRAVITY_MIN, UI_WORLD_GRAVITY_MAX );
        }
    }
    else if ( m_activeTab == InGameUITab::Options )
    {
        char buf[128];
        const float colW = (std::max)( 148.0f, contentW * 0.46f );
        const float col1 = contentX;
        const float col2 = contentX + colW + 18.0f;
        const float displayTimeScale = ( m_activeSlider == 1 && m_previewTimeScale > 0.0f ) ? m_previewTimeScale : data.timeScale;
        const int displayModelCount = ( m_activeSlider == 2 && m_previewModelCount >= 0 ) ? m_previewModelCount : data.modelCount;
        DrawSectionTitle( draw, contentX, contentY, contentH, scrolledY, 16.0f, "Scene Options" );
        DrawContentToggle( draw, contentY, contentH, m_optionToggles[0], col1, scrolledY + 42.0f, colW, "Fixed step", data.fixedStep );
        DrawContentToggle( draw, contentY, contentH, m_optionToggles[1], col2, scrolledY + 42.0f, colW, "Hide terrain", data.terrainHidden );
        DrawContentToggle( draw, contentY, contentH, m_optionToggles[2], col1, scrolledY + 72.0f, colW, "Hide water", data.waterHidden );
        DrawContentToggle( draw, contentY, contentH, m_optionToggles[3], col2, scrolledY + 72.0f, colW, "Freeze water", data.waterFreezeDebug );
        DrawContentToggle( draw, contentY, contentH, m_optionToggles[4], col1, scrolledY + 102.0f, colW, "Flat water", data.waterFlatDebug );
        snprintf( buf, sizeof( buf ), "%.2fx", displayTimeScale );
        m_timeScaleSlider.SetBounds( contentX, scrolledY + 168.0f, contentW, 34.0f );
        if ( visible( scrolledY + 168.0f, 34.0f ) )
        {
            m_timeScaleSlider.Draw( draw, "Time scale", buf, displayTimeScale, UI_TIME_SCALE_MIN, UI_TIME_SCALE_MAX );
        }
        snprintf( buf, sizeof( buf ), "%d", displayModelCount );
        m_modelCountSlider.SetBounds( contentX, scrolledY + 216.0f, contentW, 34.0f );
        if ( visible( scrolledY + 216.0f, 34.0f ) )
        {
            m_modelCountSlider.Draw( draw, "Model count", buf, static_cast<float>( displayModelCount ), static_cast<float>( UI_MODEL_COUNT_MIN ), static_cast<float>( UI_MODEL_COUNT_MAX ) );
        }
    }
    else
    {
        char buf[128];
        const int displaySeed = static_cast<int>( (std::max)( 1u, data.rngSeed ) );
        const int displaySolverBalls = ( m_activeSlider == 7 && m_previewSolverBallCount >= 0 ) ? m_previewSolverBallCount : data.solverBallCount;
        const int displaySolverBoxes = ( m_activeSlider == 8 && m_previewSolverBoxCount >= 0 ) ? m_previewSolverBoxCount : data.solverBoxCount;
        const int displayBallMax = RemainingGameModelSlots( displaySolverBoxes );
        const int displayBoxMax = RemainingGameModelSlots( displaySolverBalls );

        DrawSectionTitle( draw, contentX, contentY, contentH, scrolledY, 16.0f, "Run Controls" );
        snprintf( buf, sizeof( buf ), "%d", displaySeed );
        m_seedSlider.SetBounds( contentX, scrolledY + 42.0f, contentW, 34.0f );
        if ( visible( scrolledY + 42.0f, 34.0f ) )
        {
            m_seedSlider.Draw( draw, "Seed", buf, static_cast<float>( displaySeed ), static_cast<float>( UI_SEED_MIN ), static_cast<float>( UI_SEED_MAX ) );
        }
        if ( visible( scrolledY + 104.0f, 18.0f ) )
        {
            DrawSectionTitle( draw, contentX, contentY, contentH, scrolledY + 104.0f, 12.0f, "Game Models" );
        }
        snprintf( buf, sizeof( buf ), "%d", displaySolverBalls );
        m_solverBallSlider.SetBounds( contentX, scrolledY + 130.0f, contentW, 34.0f );
        if ( visible( scrolledY + 130.0f, 34.0f ) )
        {
            m_solverBallSlider.Draw( draw, "Balls", buf, static_cast<float>( displaySolverBalls ), static_cast<float>( UI_SOLVER_COUNT_MIN ), static_cast<float>( displayBallMax ) );
        }
        snprintf( buf, sizeof( buf ), "%d", displaySolverBoxes );
        m_solverBoxSlider.SetBounds( contentX, scrolledY + 170.0f, contentW, 34.0f );
        if ( visible( scrolledY + 170.0f, 34.0f ) )
        {
            m_solverBoxSlider.Draw( draw, "Boxes", buf, static_cast<float>( displaySolverBoxes ), static_cast<float>( UI_SOLVER_COUNT_MIN ), static_cast<float>( displayBoxMax ) );
        }
        if ( visible( scrolledY + 226.0f, 18.0f ) )
        {
            DrawSectionTitle( draw, contentX, contentY, contentH, scrolledY + 226.0f, 12.0f, "Fluid" );
        }
        snprintf( buf, sizeof( buf ), "%.0f", data.worldFluidHeight );
        m_worldFluidHeightSlider.SetBounds( contentX, scrolledY + 252.0f, contentW, 34.0f );
        if ( visible( scrolledY + 252.0f, 34.0f ) )
        {
            m_worldFluidHeightSlider.Draw( draw, "Fluid height", buf, data.worldFluidHeight, UI_WORLD_FLUID_HEIGHT_MIN, UI_WORLD_FLUID_HEIGHT_MAX );
        }
        snprintf( buf, sizeof( buf ), "%.2f", data.worldFluidDensity );
        m_worldFluidDensitySlider.SetBounds( contentX, scrolledY + 292.0f, contentW, 34.0f );
        if ( visible( scrolledY + 292.0f, 34.0f ) )
        {
            m_worldFluidDensitySlider.Draw( draw, "Fluid density", buf, data.worldFluidDensity, UI_WORLD_FLUID_DENSITY_MIN, UI_WORLD_FLUID_DENSITY_MAX );
        }
    }

    m_scrollBar.SetBounds( x + w - 14.0f, contentY, 4.0f, contentH );
    m_scrollBar.Draw( draw, static_cast<float>( ContentHeight() ), contentH, m_scrollY, m_scrollbarVisibleUntil, data.now );

    const float by = y + h - bottomH;
    draw.Rect( x + 2.0f, by, w - 4.0f, bottomH - 2.0f, 0.014f, 0.042f, 0.056f, 0.82f );
    draw.Rect( x + 2.0f, by, w - 4.0f, 1.0f, 0.30f, 0.88f, 1.0f, 0.46f );
    const float footerPad = 18.0f;
    const float footerGap = 16.0f;
    const float footerX = x + footerPad;
    const float footerW = (std::max)( 120.0f, w - footerPad * 2.0f );
    const bool hasSeparateStats = footerW >= 560.0f;
    const float controlsW = hasSeparateStats ? 414.0f : footerW;
    draw.Rect( footerX, by + 16.0f, controlsW, 56.0f, 0.018f, 0.030f, 0.038f, 0.66f );
    draw.Outline( footerX, by + 16.0f, controlsW, 56.0f, 0.18f, 0.30f, 0.34f, 0.68f );

    const UIRect rendererComboBounds = FooterRendererComboBounds( x, by );
    const UIRect waterComboBounds = FooterWaterComboBounds( x, by );
    const UIRect blurFooterBounds = FooterBlurBounds( x, by );
    const UIRect vsyncFooterBounds = FooterVsyncBounds( x, by );
    const UIRect timelineFooterBounds = FooterTimelineBounds( x, by );
    const UIRect perfFooterBounds = FooterPerfBounds( x, by );
    m_rendererCombo.SetBounds( rendererComboBounds.x, rendererComboBounds.y, rendererComboBounds.w, rendererComboBounds.h );
    m_rendererCombo.SetDropUp( true );
    m_reflectionCombo.SetBounds( waterComboBounds.x, waterComboBounds.y, waterComboBounds.w, waterComboBounds.h );
    m_reflectionCombo.SetDropUp( true );
    m_blurToggle.SetBounds( blurFooterBounds.x, blurFooterBounds.y, blurFooterBounds.w, blurFooterBounds.h );
    m_vsyncToggle.SetBounds( vsyncFooterBounds.x, vsyncFooterBounds.y, vsyncFooterBounds.w, vsyncFooterBounds.h );
    m_histogramToggle.SetBounds( perfFooterBounds.x, perfFooterBounds.y, perfFooterBounds.w, perfFooterBounds.h );
    m_timelineToggle.SetBounds( timelineFooterBounds.x, timelineFooterBounds.y, timelineFooterBounds.w, timelineFooterBounds.h );
    static const char* kRendererOptions[] = { "GL", "DX11", "DX12" };
    static const char* kReflectionOptions[] = { "FBO", "DXR", "None" };
    m_rendererCombo.Draw( draw, "Renderer", kRendererOptions, 3, currentRendererIndex, m_mouseX, m_mouseY );
    DrawFooterToggle( draw, blurFooterBounds, "Blur", m_blurPreviewEnabled );
    DrawFooterToggle( draw, vsyncFooterBounds, "VSync", data.vsyncEnabled );
    DrawFooterToggle( draw, perfFooterBounds, "Perf", m_performanceHistogramEnabled );
    DrawFooterToggle( draw, timelineFooterBounds, "Timeline", m_profilerTimelineEnabled );
    m_reflectionCombo.Draw( draw,
                            "Water",
                            kReflectionOptions,
                            3,
                            WaterReflectionModeFromData( data ),
                            m_mouseX,
                            m_mouseY,
                            ReflectionDisabledMask( currentRendererIndex ) );

    char status[128];
    const float frameDisplayMs = data.fps > 0.0f ? 1000.0f / data.fps : 0.0f;
    const int cpuPercent = static_cast<int>( std::clamp( ( data.renderMs + data.physicsMs ) / 16.67f * 100.0f, 0.0f, 99.0f ) );
    const int gpuPercent = static_cast<int>( std::clamp( data.renderMs / 16.67f * 100.0f, 0.0f, 99.0f ) );
    const int drawCalls = data.drawCallsBeforeUI + data.UIDrawCalls;
    snprintf( status, sizeof( status ), "%.0f", data.fps );
    if ( hasSeparateStats )
    {
        const float statsX = footerX + controlsW + footerGap;
        const float statsW = (std::max)( 120.0f, x + w - footerPad - statsX );
        draw.Rect( statsX, by + 16.0f, statsW, 56.0f, 0.018f, 0.030f, 0.038f, 0.66f );
        draw.Outline( statsX, by + 16.0f, statsW, 56.0f, 0.18f, 0.30f, 0.34f, 0.68f );

        if ( statsW < 350.0f )
        {
            char fpsText[32];
            char frameText[32];
            char drawText[32];
            snprintf( fpsText, sizeof( fpsText ), "%.0f", data.fps );
            snprintf( frameText, sizeof( frameText ), "%.2f ms", frameDisplayMs );
            snprintf( drawText, sizeof( drawText ), "%d/%d", drawCalls, data.UIDrawCalls );
            DrawCompactFooterStat( draw, statsX, by + 23.0f, "FPS", fpsText, 0.48f, 0.90f, 0.22f );
            DrawCompactFooterStat( draw, statsX, by + 41.0f, "Frame", frameText, 0.32f, 0.90f, 1.0f );
            DrawCompactFooterStat( draw, statsX, by + 59.0f, "Draw/UI", drawText, 0.32f, 0.90f, 1.0f );
        }
        else
        {
            DrawFooterStatCell( draw, statsX + 18.0f, by, "FPS", status, 0.48f, 0.90f, 0.22f );
            DrawFooterStatDivider( draw, statsX + 78.0f, by );
            snprintf( status, sizeof( status ), "%.2f ms", frameDisplayMs );
            DrawFooterStatCell( draw, statsX + 100.0f, by, "Frame Time", status, 0.32f, 0.90f, 1.0f );
            DrawFooterStatDivider( draw, statsX + 190.0f, by );
            snprintf( status, sizeof( status ), "%d%%", cpuPercent );
            DrawFooterStatCell( draw, statsX + 212.0f, by, "CPU", status, 0.48f, 0.90f, 0.22f );
            DrawFooterStatDivider( draw, statsX + 266.0f, by );
            snprintf( status, sizeof( status ), "%d%%", gpuPercent );
            DrawFooterStatCell( draw, statsX + 288.0f, by, "GPU", status, 0.48f, 0.90f, 0.22f );
            DrawFooterStatDivider( draw, statsX + 342.0f, by );
            snprintf( status, sizeof( status ), "%d / %d", drawCalls, data.UIDrawCalls );
            DrawFooterStatCell( draw, statsX + statsW - 112.0f, by, "Draws / UI", status, 0.32f, 0.90f, 1.0f );
        }
    }
    else
    {
        if ( titleStatW > 0.0f && titleStatX + titleStatW < x + w - 116.0f )
        {
            draw.Text( titleStatX, y + 17.0f, 10.5f, 0.48f, 0.90f, 0.22f, titleStat );
        }
    }

    if ( m_performanceHistogramEnabled )
    {
        DrawPerformanceHistogram( draw, data );
    }

    draw.Rect( x + w - 24.0f, y + h - 9.0f, 14.0f, 2.0f, 0.34f, 0.91f, 1.0f, 0.88f );
    draw.Rect( x + w - 18.0f, y + h - 15.0f, 8.0f, 2.0f, 0.34f, 0.91f, 1.0f, 0.72f );
    draw.Rect( x + w - 12.0f, y + h - 21.0f, 2.0f, 2.0f, 0.34f, 0.91f, 1.0f, 0.60f );

    Text2d::FlushQuads();
    Text2d::FlushText();
    if ( shouldDrawCursor )
    {
        DrawCursor( draw );
        Text2d::FlushQuads();
    }
}
