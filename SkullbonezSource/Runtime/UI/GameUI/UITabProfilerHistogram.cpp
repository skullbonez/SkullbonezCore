/*
File: SkullbonezSource/Runtime/UI/GameUI/UITabProfilerHistogram.cpp
Purpose:
  Owns the profiler tab's detachable performance-histogram interaction, sampling, and drawing transaction.

Summary:
  The histogram is an independent UI implementation owner behind
  UIProfilerTabState. Option-cache remapping, bounded history samples, panel
  input, and draw geometry stay together so selection changes cannot desync
  stored series from their labels or hit boxes.

Glossary:
  Histogram option: A cached marker identity and frame-total discriminator that
  indexes one fixed-capacity sample column.
  Sample ring: The 120-entry bounded history rendered from oldest to newest.

Invariants:
  - Cache remapping preserves samples by semantic option key, never row index.
  - Input bounds and draw bounds use the same panel geometry helpers.
  - Sampling and rendering allocate no runtime storage.

Related:
  - SkullbonezSource/Runtime/UI/GameUI/UITabProfiler.h
  - SkullbonezSource/Runtime/UI/GameUI/UITabProfiler.cpp
*/
#include "UITabProfiler.h"

#include "../../../UI/UIFontMetrics.h"
#include "UI.h"
#include "../../../UI/UIDraw.h"
#include "../../../UI/UIStyle.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdio>
#include <cstring>

namespace
{

constexpr float HISTOGRAM_AXIS_LABEL_GUTTER = 54.0f;

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

// Why: six seconds of history across the fixed ring. Long enough that a
// prediction horizon build stays readable after it ends, short enough that each
// slot still resolves a single spike rather than a smear.
constexpr double HISTOGRAM_WINDOW_SECONDS = 6.0;

constexpr double HISTOGRAM_BUCKET_SECONDS = HISTOGRAM_WINDOW_SECONDS /
                                            static_cast<double>( SkullbonezCore::UI::ProfilerTab::HISTOGRAM_SAMPLE_COUNT );
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
    state.histogramBucketStartSeconds = -1.0;
    state.histogramBucketOpen = false;
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
    return { panel.x + panel.w - HISTOGRAM_RESIZE_HOTSPOT, panel.y + panel.h - HISTOGRAM_RESIZE_HOTSPOT,
             HISTOGRAM_RESIZE_HOTSPOT, HISTOGRAM_RESIZE_HOTSPOT };
}

SkullbonezCore::UI::UIRect HistogramPlotBounds( const SkullbonezCore::UI::ProfilerTab::UIProfilerTabState& state )
{
    const SkullbonezCore::UI::UIRect panel = HistogramPanelBounds( state );
    const float plotY = panel.y + 66.0f;
    const float plotH = (std::max)( 34.0f, panel.h - 96.0f );
    return { panel.x + 10.0f + HISTOGRAM_AXIS_LABEL_GUTTER, plotY,
             (std::max)( 32.0f, panel.w - 20.0f - HISTOGRAM_AXIS_LABEL_GUTTER ), plotH };
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

int HitHistogramDropdownOption( const SkullbonezCore::UI::ProfilerTab::UIProfilerTabState& state, int screenH, int mouseX,
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

void RemapHistogramSamples( SkullbonezCore::UI::ProfilerTab::UIProfilerTabState& state, const uint32_t* oldHashes,
                            const bool* oldFrameTotals, int oldCount )
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

        const SkullbonezCore::UI::ProfilerTab::PerformanceHistogramSample oldSample = state.histogramSamples[sampleIndex];

        SkullbonezCore::UI::ProfilerTab::PerformanceHistogramSample remappedSample = {};

        remappedSample.secondaryMs = oldSample.secondaryMs;
        remappedSample.hasSecondary = oldSample.hasSecondary;

        for ( int newIndex = 0; newIndex < state.histogramOptionCount; ++newIndex )
        {
            for ( int oldIndex = 0; oldIndex < oldCount; ++oldIndex )
            {
                if ( HistogramOptionKeyMatches( oldHashes[oldIndex], oldFrameTotals[oldIndex],
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
                            const SkullbonezCore::UI::UIProfilerTabFrameView& data )
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
    state.histogramOptionCount = std::clamp( data.markerOptionCount, 0, HISTOGRAM_OPTION_CAPACITY );
    bool cacheChanged = oldCount != state.histogramOptionCount;
    bool selectedAny = false;

    for ( int i = 0; i < state.histogramOptionCount; ++i )
    {
        const SkullbonezCore::UI::UIProfilerMarkerOption& option = data.markerOptions[i];

        if ( !cacheChanged &&
             !HistogramOptionKeyMatches( state.histogramOptionHashes[i], state.histogramOptionFrameTotals[i], option.hash,
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
                if ( oldSelected[oldIndex] && HistogramOptionKeyMatches( oldHashes[oldIndex], oldFrameTotals[oldIndex],
                                                                         option.hash, option.isFrameTotal ) )
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
    static constexpr float kAxisSteps[] = { 0.25f, 0.50f, 1.0f,   2.0f,   4.0f,
                                            8.0f,  12.0f, 16.67f, 24.0f,  HISTOGRAM_FRAME_CPU_DEFAULT_AXIS_MS,
                                            48.0f, 64.0f, 96.0f,  128.0f, 192.0f,
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

void DrawHistogramLineSegment( const SkullbonezCore::UI::UIDrawContext& draw, float x0, float y0, float x1, float y1,
                               float thickness, float r, float g, float b, float a )
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

    if ( SkullbonezCore::UI::UIFontMetrics::MeasureText( pxSize, text ) <= maxWidth )
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

        if ( SkullbonezCore::UI::UIFontMetrics::MeasureText( pxSize, text ) <= maxWidth )
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
                           const SkullbonezCore::UI::Style::UIPalette& palette, float& r, float& g, float& b )
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
                                   const SkullbonezCore::UI::UIProfilerTabFrameView& data, char* out, std::size_t outSize )
{
    if ( !out || outSize == 0 )
    {
        return;
    }

    const int optionCount = (std::min)( state.histogramOptionCount, data.markerOptionCount );
    int selectedCount = 0;
    bool mainSelected = false;
    const char* firstSelectedName = nullptr;

    for ( int optionIndex = 0; optionIndex < optionCount; ++optionIndex )
    {
        if ( !state.histogramOptionSelected[optionIndex] )
        {
            continue;
        }

        const SkullbonezCore::UI::UIProfilerMarkerOption& option = data.markerOptions[optionIndex];

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
                            const SkullbonezCore::UI::Style::UIPalette& palette, float x, float y, bool selected, float r,
                            float g, float b )
{
    draw.RoundedRect( x, y, 11.0f, 11.0f, 2.0f, palette.control.r, palette.control.g, palette.control.b, 0.88f );
    draw.Outline( x, y, 11.0f, 11.0f, palette.innerBorder.r, palette.innerBorder.g, palette.innerBorder.b, 0.80f );

    if ( !selected )
    {
        return;
    }

    draw.Rect( x + 2.0f, y + 2.0f, 7.0f, 7.0f, r, g, b, 0.86f );
    DrawHistogramLineSegment( draw, x + 3.0f, y + 6.0f, x + 5.0f, y + 8.0f, 1.4f, palette.textPrimary.r,
                              palette.textPrimary.g, palette.textPrimary.b, 0.95f );

    DrawHistogramLineSegment( draw, x + 5.0f, y + 8.0f, x + 9.0f, y + 3.0f, 1.4f, palette.textPrimary.r,
                              palette.textPrimary.g, palette.textPrimary.b, 0.95f );
}

} // namespace

namespace SkullbonezCore
{
namespace UI
{
namespace ProfilerTab
{

bool PerformanceHistogramEnabled( const UIProfilerTabState& state )
{
    return state.performanceHistogramEnabled;
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

bool HandlePerformanceHistogramInput( UIProfilerTabState& state, InGameUIInputResult& result, int screenW, int screenH,
                                      int mouseX, int mouseY, bool leftDown, bool leftPressed, bool leftReleased,
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
        state.histogramSelectorScroll = std::clamp( state.histogramSelectorScroll - wheelSteps, 0,
                                                    HistogramMaxScroll( state ) );

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
            state.histogramSelectorScroll = std::clamp( selectedIndex - visibleRows / 2, 0, HistogramMaxScroll( state ) );

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
        state.histogramPanelW = state.histogramResizeStartW +
                                static_cast<float>( mouseX - state.histogramResizeStartMouseX );

        state.histogramPanelH = state.histogramResizeStartH +
                                static_cast<float>( mouseY - state.histogramResizeStartMouseY );

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

void PushPerformanceHistogramSample( UIProfilerTabState& state, const UIProfilerTabFrameView& data )
{
    CacheHistogramOptions( state, data );

    if ( !HistogramAnyOptionSelected( state ) )
    {
        return;
    }

    float previousMaxMs[HISTOGRAM_OPTION_CAPACITY] = {};

    for ( int i = 0; i < state.histogramCount; ++i )
    {
        const int sampleIndex = ( state.histogramHead - state.histogramCount + i + HISTOGRAM_SAMPLE_COUNT ) %
                                HISTOGRAM_SAMPLE_COUNT;

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
    const int optionCount = (std::min)( state.histogramOptionCount, data.markerOptionCount );

    // Invariant: a slot closes on elapsed wall time, so the visible window is a
    // constant duration instead of a constant frame count. A backwards clock
    // (scene reload resets the runtime timer) starts a fresh slot rather than
    // holding the current one open forever.
    const bool bucketExpired = state.histogramBucketStartSeconds < 0.0 || data.now < state.histogramBucketStartSeconds ||
                               ( data.now - state.histogramBucketStartSeconds ) >= HISTOGRAM_BUCKET_SECONDS;

    if ( bucketExpired )
    {
        if ( state.histogramBucketOpen )
        {
            state.histogramHead = ( state.histogramHead + 1 ) % HISTOGRAM_SAMPLE_COUNT;

            if ( state.histogramCount < HISTOGRAM_SAMPLE_COUNT )
            {
                ++state.histogramCount;
            }
        }

        state.histogramSamples[state.histogramHead] = {};
        state.histogramBucketStartSeconds = data.now;
        state.histogramBucketOpen = false;
    }

    PerformanceHistogramSample& writeSample = state.histogramSamples[state.histogramHead];
    bool wroteMarker = false;

    for ( int optionIndex = 0; optionIndex < optionCount; ++optionIndex )
    {
        if ( !state.histogramOptionSelected[optionIndex] )
        {
            continue;
        }

        const UIProfilerMarkerOption& option = data.markerOptions[optionIndex];

        if ( !option.sampleValid )
        {
            continue;
        }

        // Invariant: marker histories are stored by cached option slot. Main
        // keeps the fixed frame-budget axis, while non-main selections can use
        // their own dynamic scale without changing the ring layout.
        // Hazard: a worker-owned marker such as the replay prediction slice has
        // no frame-thread time at all. Plotting cpuMs alone would graph a flat
        // zero for exactly the markers an operator selects to see worker load,
        // so the line is the marker's total across both threads.
        const float markerMs = std::clamp( option.cpuMs + option.workerMs, 0.0f, HISTOGRAM_SAMPLE_CLAMP_MS );

        // Why: peak, not last or mean. A slot can span many frames at high frame
        // rates, and the spike is the reason an operator is looking.
        writeSample.markerMs[optionIndex] = (std::max)( writeSample.markerMs[optionIndex], markerMs );
        writeSample.hasMarker[optionIndex] = true;
        wroteMarker = true;

        const float axisSpikeThreshold = mainSelected && !option.isFrameTotal ? 0.10f : state.histogramAxisMs * 0.92f;

        if ( state.histogramCount > 8 && markerMs > 0.10f &&
             markerMs > (std::max)( previousMaxMs[optionIndex] * 1.20f, axisSpikeThreshold ) )
        {
            writeSample.markerSpikeMs[optionIndex] = (std::max)( writeSample.markerSpikeMs[optionIndex], markerMs );
        }
    }

    if ( !wroteMarker )
    {
        return;
    }

    if ( mainSelected && data.workerCoreTotalMs > 0.0f )
    {
        writeSample.secondaryMs = (std::max)( writeSample.secondaryMs,
                                              std::clamp( data.workerCoreTotalMs, 0.0f, HISTOGRAM_SAMPLE_CLAMP_MS ) );

        writeSample.hasSecondary = true;
    }

    // Invariant: the slot stays at the current head until its wall-clock slice
    // expires. Marking it open is what lets the next expiry advance the ring.
    state.histogramBucketOpen = true;

    float visibleMaxMs = 0.0f;

    for ( int i = 0; i < state.histogramCount; ++i )
    {
        const int sampleIndex = ( state.histogramHead - state.histogramCount + i + HISTOGRAM_SAMPLE_COUNT ) %
                                HISTOGRAM_SAMPLE_COUNT;

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

void DrawPerformanceHistogram( UIProfilerTabState& state, const UIDrawContext& draw, const UIProfilerTabFrameView& data )
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
    const int optionCount = (std::min)( state.histogramOptionCount, data.markerOptionCount );

    // Why: text rendering is batched separately from filled panels. While the
    // dropdown is open, hide chart-side labels that would otherwise draw above it.
    const bool dropdownOpen = state.histogramSelectorOpen && state.histogramOptionCount > 0;

    const Style::UIPalette& palette = Style::Palette();
    draw.RoundedRect( panel.x + 4.0f, panel.y + 5.0f, panel.w, panel.h, Style::Radii().window, 0.0f, 0.0f, 0.0f, 0.22f );

    draw.RoundedPanel( panel, Style::Radii().window, palette.windowSubtle, palette.border );

    char text[128] = {};
    draw.Text( panel.x + 10.0f, panel.y + 8.0f, 10.5f, palette.textPrimary.r, palette.textPrimary.g, palette.textPrimary.b,
               "Marker History" );

    snprintf( text, sizeof( text ), mainSelected ? "CPU + WORK" : "CPU" );
    draw.Text( panel.x + panel.w - 10.0f - UIFontMetrics::MeasureText( 9.6f, text ), panel.y + 8.0f, 9.6f,
               palette.textSecondary.r, palette.textSecondary.g, palette.textSecondary.b, text );

    draw.RoundedRect( selector.x, selector.y, selector.w, selector.h, Style::Radii().smallButton, palette.control.r,
                      palette.control.g, palette.control.b, 0.82f );

    draw.Outline( selector.x, selector.y, selector.w, selector.h, palette.innerBorder.r, palette.innerBorder.g,
                  palette.innerBorder.b, 0.76f );

    FormatHistogramSelectionText( state, data, text, sizeof( text ) );
    FitHistogramText( text, sizeof( text ), 10.0f, selector.w - 26.0f );
    draw.Text( selector.x + 9.0f, selector.y + 6.0f, 10.0f, palette.textPrimary.r, palette.textPrimary.g,
               palette.textPrimary.b, text );

    draw.Triangle( selector.x + selector.w - 15.0f, selector.y + 9.0f, selector.x + selector.w - 7.0f, selector.y + 9.0f,
                   selector.x + selector.w - 11.0f, selector.y + 15.0f, palette.textSecondary.r, palette.textSecondary.g,
                   palette.textSecondary.b, 0.88f );

    draw.Rect( plot.x, plot.y, plot.w, plot.h, palette.window.r, palette.window.g, palette.window.b, 0.58f );
    const float budgetY = mainSelected ? baseY - std::clamp( HISTOGRAM_FRAME_CPU_BUDGET_MS / axisMs, 0.0f, 1.0f ) * plot.h
                                       : plot.y + plot.h * 0.50f;

    draw.Rect( plot.x, budgetY, plot.w, 1.0f, mainSelected ? palette.warningAccent.r : palette.lineSoft.r,
               mainSelected ? palette.warningAccent.g : palette.lineSoft.g,
               mainSelected ? palette.warningAccent.b : palette.lineSoft.b, mainSelected ? 0.58f : 0.14f );

    draw.Rect( plot.x, plot.y, plot.w, 1.0f, palette.lineSoft.r, palette.lineSoft.g, palette.lineSoft.b, 0.18f );
    draw.Rect( plot.x, plot.y, 1.0f, plot.h, palette.lineSoft.r, palette.lineSoft.g, palette.lineSoft.b, 0.28f );
    draw.Rect( plot.x, baseY, plot.w, 1.0f, palette.accent.r, palette.accent.g, palette.accent.b, 0.34f );

    auto drawAxisLabel = [&]( float y, float ms )
    {
        FormatHistogramMsLabel( text, sizeof( text ), ms );

        const float textW = UIFontMetrics::MeasureText( 8.8f, text );
        draw.Text( plot.x - 6.0f - textW, y, 8.8f, palette.textSecondary.r, palette.textSecondary.g, palette.textSecondary.b,
                   text );
    };

    if ( !dropdownOpen )
    {
        drawAxisLabel( plot.y + 2.0f, axisMs );
        drawAxisLabel( budgetY - 5.0f, mainSelected ? HISTOGRAM_FRAME_CPU_BUDGET_MS : axisMs * 0.50f );
    }

    if ( !dropdownOpen && ( state.histogramCount <= 0 || !anySelection ) )
    {
        draw.Text( plot.x + 10.0f, plot.y + plot.h * 0.5f - 6.0f, 10.5f, palette.textMuted.r, palette.textMuted.g,
                   palette.textMuted.b, anySelection ? "Waiting for samples" : "Select markers" );
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

        const UIProfilerMarkerOption& seriesOption = data.markerOptions[optionIndex];
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
            const int sampleIndex = ( state.histogramHead - state.histogramCount + i + HISTOGRAM_SAMPLE_COUNT ) %
                                    HISTOGRAM_SAMPLE_COUNT;

            const PerformanceHistogramSample& sample = state.histogramSamples[sampleIndex];

            if ( !sample.hasMarker[optionIndex] )
            {
                previousValid = false;
                continue;
            }

            const float x = plot.x +
                            ( static_cast<float>( HISTOGRAM_SAMPLE_COUNT - state.histogramCount + i ) + 0.5f ) * step;

            const float markerY = baseY - std::clamp( sample.markerMs[optionIndex] / axisMs, 0.0f, 1.0f ) * plot.h;

            if ( previousValid )
            {
                DrawHistogramLineSegment( draw, previousX, previousY, x, markerY, 2.0f, seriesR, seriesG, seriesB, 0.86f );
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
            const int sampleIndex = ( state.histogramHead - state.histogramCount + i + HISTOGRAM_SAMPLE_COUNT ) %
                                    HISTOGRAM_SAMPLE_COUNT;

            const PerformanceHistogramSample& sample = state.histogramSamples[sampleIndex];

            if ( !sample.hasSecondary )
            {
                previousWorkerValid = false;
                continue;
            }

            const float x = plot.x +
                            ( static_cast<float>( HISTOGRAM_SAMPLE_COUNT - state.histogramCount + i ) + 0.5f ) * step;

            const float workerY = baseY - std::clamp( sample.secondaryMs / axisMs, 0.0f, 1.0f ) * plot.h;

            if ( previousWorkerValid )
            {
                DrawHistogramLineSegment( draw, previousWorkerX, previousWorkerY, x, workerY, 2.0f, workerLineR, workerLineG,
                                          workerLineB, 0.88f );
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
        const bool refreshAverageText = state.histogramCount >= 8 &&
                                        ( state.histogramAverageTextLastUpdateSeconds < 0.0 || averageBecameMeaningful ||
                                          data.now < state.histogramAverageTextLastUpdateSeconds ||
                                          data.now - state.histogramAverageTextLastUpdateSeconds >=
                                              HISTOGRAM_AVERAGE_TEXT_REFRESH_SECONDS );

        if ( refreshAverageText )
        {
            state.histogramAverageCpuMs = candidateCpuAverageMs;
            state.histogramAverageWorkerMs = candidateWorkerAverageMs;
            state.histogramAverageTextLastUpdateSeconds = data.now;
        }

        const float footerY = panel.y + panel.h - 20.0f;
        snprintf( text, sizeof( text ), selectedCount > 1 ? "Selected avg %.2f ms" : "CPU avg %.2f ms",
                  state.histogramAverageCpuMs );

        draw.Rect( panel.x + 10.0f, footerY + 7.0f, 9.0f, 2.0f, palette.accent.r, palette.accent.g, palette.accent.b,
                   0.86f );

        draw.Text( panel.x + 22.0f, footerY, 10.0f, palette.accent.r, palette.accent.g, palette.accent.b, text );

        if ( mainSelected && state.histogramAverageWorkerMs > 0.0f )
        {
            const float workerX = panel.x + 34.0f + UIFontMetrics::MeasureText( 10.0f, text );
            snprintf( text, sizeof( text ), "Other cores avg %.2f ms", state.histogramAverageWorkerMs );
            draw.Rect( workerX, footerY + 7.0f, 9.0f, 2.0f, workerLineR, workerLineG, workerLineB, 0.90f );
            draw.Text( workerX + 12.0f, footerY, 10.0f, workerLineR, workerLineG, workerLineB, text );
        }
    }

    draw.Rect( resize.x + 7.0f, resize.y + 16.0f, 10.0f, 1.0f, palette.textMuted.r, palette.textMuted.g, palette.textMuted.b,
               0.54f );

    draw.Rect( resize.x + 12.0f, resize.y + 11.0f, 5.0f, 1.0f, palette.textMuted.r, palette.textMuted.g, palette.textMuted.b,
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

            const UIProfilerMarkerOption& rowOption = data.markerOptions[optionIndex];
            const float rowY = dropdown.y + 2.0f + static_cast<float>( row ) * HISTOGRAM_DROPDOWN_ROW_H;
            const bool selected = state.histogramOptionSelected[optionIndex];
            float rowR = 0.0f;
            float rowG = 0.0f;
            float rowB = 0.0f;
            HistogramOptionColor( rowOption, palette, rowR, rowG, rowB );

            if ( selected )
            {
                draw.Rect( dropdown.x + 2.0f, rowY, dropdown.w - 4.0f, HISTOGRAM_DROPDOWN_ROW_H, palette.controlHover.r,
                           palette.controlHover.g, palette.controlHover.b, 0.58f );
            }

            DrawHistogramCheckbox( draw, palette, dropdown.x + 8.0f, rowY + 5.0f, selected, rowR, rowG, rowB );
            snprintf( text, sizeof( text ), "%s", HistogramOptionDisplayName( rowOption ) );
            FitHistogramText( text, sizeof( text ), 9.4f, dropdown.w - 120.0f );
            draw.Text( dropdown.x + 39.0f, rowY + 6.0f, 9.4f, selected ? palette.textPrimary.r : palette.textSecondary.r,
                       selected ? palette.textPrimary.g : palette.textSecondary.g,
                       selected ? palette.textPrimary.b : palette.textSecondary.b, text );

            // Why: the selector value must match the line the row plots, or a
            // worker-owned marker reads 0.000 here and an operator never finds
            // the row that carries the work.
            const float rowCpuMs = rowOption.cpuAverageMs > 0.0f ? rowOption.cpuAverageMs : rowOption.cpuMs;
            const float rowWorkerMs = rowOption.workerAverageMs > 0.0f ? rowOption.workerAverageMs : rowOption.workerMs;
            snprintf( text, sizeof( text ), "%.3f", rowCpuMs + rowWorkerMs );

            draw.Text( dropdown.x + dropdown.w - 62.0f, rowY + 6.0f, 9.2f, rowR, rowG, rowB, text );
        }

        if ( state.histogramOptionCount > visibleRows )
        {
            const float footerY = dropdown.y + 2.0f + static_cast<float>( visibleRows ) * HISTOGRAM_DROPDOWN_ROW_H;
            draw.Rect( dropdown.x + 2.0f, footerY, dropdown.w - 4.0f, 1.0f, palette.innerBorder.r, palette.innerBorder.g,
                       palette.innerBorder.b, 0.78f );

            snprintf( text, sizeof( text ), "%d-%d/%d", state.histogramSelectorScroll + 1,
                      state.histogramSelectorScroll + visibleRows, state.histogramOptionCount );

            draw.Text( dropdown.x + dropdown.w - 54.0f, footerY + 4.0f, 8.0f, palette.textMuted.r, palette.textMuted.g,
                       palette.textMuted.b, text );
        }
    }
}

} // namespace ProfilerTab
} // namespace UI
} // namespace SkullbonezCore
