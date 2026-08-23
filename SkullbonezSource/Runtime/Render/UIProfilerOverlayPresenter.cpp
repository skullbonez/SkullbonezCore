/*
File: SkullbonezSource/UI/UIProfilerOverlayPresenter.cpp
Purpose:
  Records profiler tables and timing bars as bounded UI draw values.

Summary:
  This is the sole owner of operator-facing profiler labels, ordering, colors,
  chart geometry, and units. A small projection adapter preserves the legacy
  panel dimensions while converting them to screen-pixel UIDrawList commands.

Glossary:
  Marker tree: Parent-indexed profiler rows displayed depth first.
  Absolute bar: Timing segments scaled against the complete frame duration.
  Normalized bar: Timing segments scaled against the subtotal being shown.
  Projection adapter: UI-only conversion from legacy overlay coordinates to
    the screen pixels stored by UIDrawList.

Invariants:
  - Fixed Core profiler limits bound all temporary arrays.
  - Text measurement uses the same immutable baked glyph advances as drawing.
  - UI records values only; Runtime/Render owns submission and GPU lifetime.

Related:
  - SkullbonezSource/UI/UIProfilerOverlayPresenter.h
  - SkullbonezSource/UI/UIFontMetrics.h
  - SkullbonezSource/Runtime/Render/UiDrawSubmission.cpp
*/
#include "UIProfilerOverlayPresenter.h"

#include "../../UI/UIDraw.h"
#include "../../UI/UIFontMetrics.h"

#include <algorithm>
#include <cstdarg>
#include <cstdio>
#include <cstring>

namespace SkullbonezCore::UI
{
namespace
{
class ProjectedDraw
{
  public:
    explicit ProjectedDraw( const UIDrawContext& draw )
        : m_draw( draw ), m_scaleX( draw.TextX( 1.0f ) - draw.TextX( 0.0f ) ),
          m_scaleY( draw.TextY( 0.0f ) - draw.TextY( 1.0f ) )
    {
    }

    float MeasureText( float size, const char* value ) const
    {
        return UIFontMetrics::MeasureText( size, value );
    }

    float HalfH() const
    {
        return m_draw.HalfH();
    }

    void Quad( float xLeft, float yBottom, float xRight, float yTop, float red, float green, float blue, float alpha ) const
    {
        const float pixelX = ( xLeft + m_draw.HalfW() ) / m_scaleX;

        const float pixelY = ( m_draw.HalfH() - yTop ) / m_scaleY;
        m_draw.Rect( pixelX, pixelY, ( xRight - xLeft ) / m_scaleX, ( yTop - yBottom ) / m_scaleY, red, green, blue, alpha );
    }

    void Text( float x, float baselineY, float size, float red, float green, float blue, const char* format, ... ) const
    {
        char value[256] = {};
        va_list arguments;
        va_start( arguments, format );
        vsnprintf( value, sizeof( value ), format, arguments );
        va_end( arguments );

        const float pixelSize = size / m_scaleY;
        const float pixelX = ( x + m_draw.HalfW() ) / m_scaleX;
        const float pixelY = ( m_draw.HalfH() - baselineY ) / m_scaleY - pixelSize;
        m_draw.Text( pixelX, pixelY, pixelSize, red, green, blue, value );
    }

  private:
    const UIDrawContext& m_draw;
    float m_scaleX = 1.0f;
    float m_scaleY = 1.0f;
};
} // namespace


void UIProfilerOverlayPresenter::RecordOverlay( const Core::Profiler::ProfilerFrameView& frame,
                                                const UIDrawContext& drawContext, float xLeft, float yAnchor,
                                                float lineHeight, float fontSize, float fps, bool rightAnchored ) const
{
    using Marker = Core::Profiler::Marker;
    constexpr int MAX_MARKERS = Core::Profiler::MAX_MARKERS;
    const ProjectedDraw draw( drawContext );
    const Marker* markers = frame.markers.data();
    const int markerCount = static_cast<int>( frame.markers.size() );

    bool anyGpu = false;

    for ( int index = 0; index < markerCount; ++index )
    {
        if ( markers[index].hasGpu && markers[index].gpuRingFilled > 0 )
        {
            anyGpu = true;
            break;
        }
    }

    // Why: the WORK column only earns its width when a worker actually reached
    // an instrumented path this session. Scenes below the parallel-dispatch
    // threshold, and runs with no replay prediction, keep the original layout.
    bool anyWorker = false;

    for ( int index = 0; index < markerCount; ++index )
    {
        if ( markers[index].workerAvgMs > 0.0f || markers[index].lastFrameWorkerMs > 0.0f )
        {
            anyWorker = true;
            break;
        }
    }

    const float padX = fontSize * 0.6f;
    const float padY = lineHeight * 1.2f;

    float markerNameWidth = draw.MeasureText( fontSize, "MARKER" );

    for ( int index = 0; index < markerCount; ++index )
    {
        char name[64] = {};

        int spaces = markers[index].depth * 2;

        if ( spaces > 20 )
        {
            spaces = 20;
        }

        for ( int space = 0; space < spaces; ++space )
        {
            name[space] = ' ';
        }

        strcpy_s( name + spaces, sizeof( name ) - spaces, markers[index].leafName );
        markerNameWidth = (std::max)( markerNameWidth, draw.MeasureText( fontSize, name ) );
    }

    const float nameColumn = 0.0f;
    const float valueColumnStep = fontSize * 7.0f;
    const float averageColumn = markerNameWidth + fontSize * 1.5f;
    const float selfColumn = averageColumn + valueColumnStep;
    const float workerColumn = anyWorker ? selfColumn + valueColumnStep : -1.0f;
    const float afterWorkerColumn = anyWorker ? workerColumn : selfColumn;
    const float gpuColumn = anyGpu ? afterWorkerColumn + valueColumnStep : -1.0f;
    const float p50Column = anyGpu ? gpuColumn + valueColumnStep : afterWorkerColumn + valueColumnStep;
    const float p99Column = p50Column + valueColumnStep;
    const float minimumColumn = p99Column + valueColumnStep;
    const float maximumColumn = minimumColumn + valueColumnStep;

    const float valueWidth = draw.MeasureText( fontSize, "9999.99" );
    const float panelWidth = maximumColumn + valueWidth + padX;
    const float screenHeight = draw.HalfH() * 2.0f;
    const int maximumRows = static_cast<int>( ( screenHeight - 4.0f * padY ) / lineHeight );
    const int visibleRows = ( markerCount + 2 < maximumRows ) ? markerCount + 2 : maximumRows;
    const float rowsHeight = static_cast<float>( visibleRows ) * lineHeight;

    if ( rightAnchored )
    {
        xLeft = xLeft + padX - panelWidth;
    }

    const float yBottom = yAnchor + padY;
    const float yTop = yBottom + rowsHeight;
    draw.Quad( xLeft - padX, yBottom, xLeft - padX + panelWidth, yTop + padY, 0.12f, 0.12f, 0.12f, 0.5f );

    const float headerRed = 1.0f;
    const float headerGreen = 0.85f;
    const float headerBlue = 0.2f;
    const float columnRed = 0.6f;
    const float columnGreen = 0.6f;
    const float columnBlue = 0.6f;
    const float gpuRed = 0.4f;
    const float gpuGreen = 0.8f;
    const float gpuBlue = 1.0f;

    // Why: WORK reuses the light blue the histogram already assigns to
    // other-core time, so the aggregate line and the per-marker column read as
    // the same quantity split two ways.
    const float workerRed = 0.42f;
    const float workerGreen = 0.83f;
    const float workerBlue = 1.0f;

    static constexpr uint32_t kFrameHash = ::HashStr( "Frame" );
    static constexpr uint32_t kVsyncHash = ::HashStr( "Frame/VsyncWait" );
    float frameAverageMs = 0.0f;

    for ( int index = 0; index < markerCount; ++index )
    {
        if ( markers[index].hash == kFrameHash )
        {
            frameAverageMs = markers[index].avgMs;
        }
    }

    const float cpuMs = frameAverageMs;

    float y = yTop;
    draw.Text( xLeft, y, fontSize, headerRed, headerGreen, headerBlue, "CPU: %.2f ms  FPS: %.1f", cpuMs, fps );

    y -= lineHeight;

    draw.Text( xLeft + nameColumn, y, fontSize, columnRed, columnGreen, columnBlue, "MARKER" );
    draw.Text( xLeft + averageColumn, y, fontSize, columnRed, columnGreen, columnBlue, "CPU" );
    draw.Text( xLeft + selfColumn, y, fontSize, columnRed, columnGreen, columnBlue, "SELF" );

    if ( anyWorker )
    {
        draw.Text( xLeft + workerColumn, y, fontSize, workerRed, workerGreen, workerBlue, "WORK" );
    }

    if ( anyGpu )
    {
        draw.Text( xLeft + gpuColumn, y, fontSize, gpuRed, gpuGreen, gpuBlue, "GPU" );
    }

    draw.Text( xLeft + p50Column, y, fontSize, columnRed, columnGreen, columnBlue, "P50" );
    draw.Text( xLeft + p99Column, y, fontSize, columnRed, columnGreen, columnBlue, "P99" );
    draw.Text( xLeft + minimumColumn, y, fontSize, columnRed, columnGreen, columnBlue, "MIN" );
    draw.Text( xLeft + maximumColumn, y, fontSize, columnRed, columnGreen, columnBlue, "MAX" );
    y -= lineHeight;

    const float budgetMs = ( cpuMs > 0.001f ) ? cpuMs : 1.0f;
    auto recordMarkerRow = [&]( const Marker& marker )
    {
        if ( y < yBottom )
        {
            y -= lineHeight;

            return;
        }

        char name[64] = {};
        int spaces = marker.depth * 2;

        if ( spaces > 20 )
        {
            spaces = 20;
        }

        for ( int space = 0; space < spaces; ++space )
        {
            name[space] = ' ';
        }

        strcpy_s( name + spaces, sizeof( name ) - spaces, marker.leafName );

        float red = 0.0f;
        float green = 0.0f;
        float blue = 0.0f;

        if ( marker.hash == kVsyncHash )
        {
            red = 0.5f;
            green = 0.5f;
            blue = 0.5f;
        }
        else
        {
            const float ratio = marker.avgMs / budgetMs;

            if ( ratio < 0.15f )
            {
                red = 0.3f;
                green = 0.9f;
                blue = 0.3f;
            }
            else if ( ratio < 0.5f )
            {
                red = 1.0f;
                green = 0.7f;
                blue = 0.2f;
            }
            else
            {
                red = 1.0f;
                green = 0.3f;
                blue = 0.3f;
            }
        }

        draw.Text( xLeft + nameColumn, y, fontSize, red, green, blue, "%s", name );
        draw.Text( xLeft + averageColumn, y, fontSize, red, green, blue, "%6.2f", marker.avgMs );
        const float selfMs = marker.selfAvgMs > 0.0f ? marker.selfAvgMs : marker.lastSelfMs;
        draw.Text( xLeft + selfColumn, y, fontSize, red, green, blue, "%6.2f", selfMs );

        if ( anyWorker )
        {
            const float workerMs = marker.workerAvgMs > 0.0f ? marker.workerAvgMs : marker.lastFrameWorkerMs;

            if ( workerMs > 0.0f )
            {
                draw.Text( xLeft + workerColumn, y, fontSize, workerRed, workerGreen, workerBlue, "%6.2f", workerMs );
            }
            else
            {
                draw.Text( xLeft + workerColumn, y, fontSize, columnRed, columnGreen, columnBlue, "    - " );
            }
        }

        if ( anyGpu )
        {
            if ( marker.hasGpu && marker.gpuRingFilled > 0 )
            {
                draw.Text( xLeft + gpuColumn, y, fontSize, gpuRed, gpuGreen, gpuBlue, "%6.2f", marker.gpuAvgMs );
            }
            else
            {
                draw.Text( xLeft + gpuColumn, y, fontSize, columnRed, columnGreen, columnBlue, "    - " );
            }
        }

        draw.Text( xLeft + p50Column, y, fontSize, red, green, blue, "%6.2f", marker.p50Ms );
        draw.Text( xLeft + p99Column, y, fontSize, red, green, blue, "%6.2f", marker.p99Ms );
        const float displayMinimum = marker.ringFilled > 0 ? marker.minMs : 0.0f;
        const float displayMaximum = marker.ringFilled > 0 ? marker.maxMs : 0.0f;
        draw.Text( xLeft + minimumColumn, y, fontSize, red, green, blue, "%6.2f", displayMinimum );
        draw.Text( xLeft + maximumColumn, y, fontSize, red, green, blue, "%6.2f", displayMaximum );
        y -= lineHeight;
    };

    // Invariant: registration order defines sibling order, while the fixed
    // child table makes nesting visible without allocating a tree.
    int children[MAX_MARKERS][MAX_MARKERS];
    int childCount[MAX_MARKERS] = {};

    for ( int index = 0; index < markerCount; ++index )
    {
        const int parent = markers[index].parentIndex;

        if ( parent >= 0 && parent < markerCount )
        {
            children[parent][childCount[parent]++] = index;
        }
    }

    int stack[MAX_MARKERS];
    int stackSize = 0;

    for ( int index = markerCount - 1; index >= 0; --index )
    {
        if ( markers[index].parentIndex == -1 && markers[index].hash != kVsyncHash )
        {
            stack[stackSize++] = index;
        }
    }

    while ( stackSize > 0 )
    {
        const int index = stack[--stackSize];
        recordMarkerRow( markers[index] );

        for ( int child = childCount[index] - 1; child >= 0; --child )
        {
            stack[stackSize++] = children[index][child];
        }
    }

    for ( int index = 0; index < markerCount; ++index )
    {
        if ( markers[index].hash == kVsyncHash )
        {
            recordMarkerRow( markers[index] );
            break;
        }
    }
}


void UIProfilerOverlayPresenter::RecordBarOverlay( const Core::Profiler::ProfilerFrameView& frame,
                                                   const UIDrawContext& drawContext, float xLeft, float yBottom,
                                                   float panelWidth, float panelHeight, bool absolute ) const
{
    using Marker = Core::Profiler::Marker;
    using BarColor = Core::Profiler::BarColor;
    constexpr int MAX_MARKERS = Core::Profiler::MAX_MARKERS;
    constexpr int BAR_PALETTE_SIZE = Core::Profiler::BAR_PALETTE_SIZE;
    const auto& palette = Core::Profiler::BAR_PALETTE;
    const ProjectedDraw draw( drawContext );
    const Marker* markers = frame.markers.data();
    const int markerCount = static_cast<int>( frame.markers.size() );

    bool isLeaf[MAX_MARKERS] = {};

    for ( int index = 0; index < markerCount; ++index )
    {
        isLeaf[index] = true;
    }

    for ( int index = 0; index < markerCount; ++index )
    {
        if ( markers[index].parentIndex >= 0 )
        {
            isLeaf[markers[index].parentIndex] = false;
        }
    }

    static constexpr uint32_t kFrameHash = ::HashStr( "Frame" );
    static constexpr uint32_t kVsyncHash = ::HashStr( "Frame/VsyncWait" );
    int cpuLeaves[MAX_MARKERS];
    int cpuLeafCount = 0;
    int gpuLeaves[MAX_MARKERS];
    int gpuLeafCount = 0;
    float cpuTotalMs = 0.0f;
    float gpuTotalMs = 0.0f;

    for ( int index = 0; index < markerCount; ++index )
    {
        if ( !isLeaf[index] || markers[index].hash == kFrameHash )
        {
            continue;
        }

        const bool idle = markers[index].hash == kVsyncHash;

        if ( !idle )
        {
            cpuLeaves[cpuLeafCount++] = index;
            cpuTotalMs += markers[index].avgMs;
        }

        if ( markers[index].hasGpu && markers[index].gpuRingFilled > 0 && !idle )
        {
            gpuLeaves[gpuLeafCount++] = index;
            gpuTotalMs += markers[index].gpuAvgMs;
        }
    }

    float frameMs = 0.0f;

    for ( int index = 0; index < markerCount; ++index )
    {
        if ( markers[index].hash == kFrameHash )
        {
            frameMs = markers[index].avgMs;
            break;
        }
    }

    if ( frameMs < 0.001f )
    {
        frameMs = 16.67f;
    }

    const float pad = panelHeight * 0.06f;
    const float barHeight = panelHeight * 0.18f;
    const float barGap = panelHeight * 0.09f;
    const float legendHeight = panelHeight * 0.20f;
    const float titleHeight = panelHeight * 0.12f;
    const float barLeft = xLeft + pad;
    const float barRight = xLeft + panelWidth - pad;
    const float fontSize = barHeight * 0.45f;

    draw.Quad( xLeft, yBottom, xLeft + panelWidth, yBottom + panelHeight, 0.06f, 0.06f, 0.10f, 0.90f );

    const float titleY = yBottom + panelHeight - pad - titleHeight;
    const char* title = absolute ? "PROFILER BARS (ABSOLUTE)" : "PROFILER BARS (NORMALIZED)";
    draw.Text( barLeft, titleY + titleHeight * 0.35f, fontSize * 1.05f, 1.0f, 0.85f, 0.35f, "%s", title );

    char totals[128] = {};

    if ( absolute )
    {
        sprintf_s( totals, sizeof( totals ), "CPU: %.2f ms  GPU: %.2f ms  Frame: %.2f ms", cpuTotalMs, gpuTotalMs, frameMs );
    }
    else
    {
        sprintf_s( totals, sizeof( totals ), "CPU: %.2f ms  GPU: %.2f ms", cpuTotalMs, gpuTotalMs );
    }

    const float totalsWidth = draw.MeasureText( fontSize * 0.9f, totals );
    draw.Text( barRight - totalsWidth, titleY + titleHeight * 0.35f, fontSize * 0.9f, 0.85f, 0.85f, 0.85f, "%s", totals );

    const float cpuBarY = titleY - barGap - barHeight * 0.4f;
    draw.Text( barLeft, cpuBarY + barHeight * 0.3f, fontSize, 0.85f, 0.85f, 0.85f, "CPU" );
    const float cpuLabelWidth = draw.MeasureText( fontSize, "CPU " ) + pad * 0.5f;
    const float cpuBarLeft = barLeft + cpuLabelWidth;
    const float cpuBarWidth = barRight - cpuBarLeft;
    draw.Quad( cpuBarLeft, cpuBarY, barRight, cpuBarY + barHeight, 0.15f, 0.15f, 0.15f, 1.0f );

    const float cpuScale = absolute ? ( frameMs > 0.001f ? cpuBarWidth / frameMs : 0.0f )
                                    : ( cpuTotalMs > 0.001f ? cpuBarWidth / cpuTotalMs : 0.0f );

    float cpuX = cpuBarLeft;

    for ( int leafIndex = 0; leafIndex < cpuLeafCount; ++leafIndex )
    {
        const Marker& marker = markers[cpuLeaves[leafIndex]];
        float segmentWidth = marker.avgMs * cpuScale;

        if ( segmentWidth < 0.0001f )
        {
            continue;
        }

        if ( cpuX + segmentWidth > barRight )
        {
            segmentWidth = barRight - cpuX;
        }

        const BarColor& color = palette[marker.colorIndex % BAR_PALETTE_SIZE];
        draw.Quad( cpuX, cpuBarY, cpuX + segmentWidth, cpuBarY + barHeight, color.r, color.g, color.b, 1.0f );

        cpuX += segmentWidth;
    }

    if ( absolute && cpuX < barRight )
    {
        draw.Quad( cpuX, cpuBarY, barRight, cpuBarY + barHeight, 0.85f, 0.85f, 0.85f, 0.7f );
    }

    const float gpuBarY = cpuBarY - barGap - barHeight;

    if ( gpuLeafCount > 0 )
    {
        draw.Text( barLeft, gpuBarY + barHeight * 0.3f, fontSize, 0.4f, 0.8f, 1.0f, "GPU" );
        const float gpuBarLeft = barLeft + cpuLabelWidth;
        const float gpuBarWidth = barRight - gpuBarLeft;
        draw.Quad( gpuBarLeft, gpuBarY, barRight, gpuBarY + barHeight, 0.15f, 0.15f, 0.15f, 1.0f );

        const float gpuScale = absolute ? ( frameMs > 0.001f ? gpuBarWidth / frameMs : 0.0f )
                                        : ( gpuTotalMs > 0.001f ? gpuBarWidth / gpuTotalMs : 0.0f );

        float gpuX = gpuBarLeft;

        for ( int leafIndex = 0; leafIndex < gpuLeafCount; ++leafIndex )
        {
            const Marker& marker = markers[gpuLeaves[leafIndex]];
            float segmentWidth = marker.gpuAvgMs * gpuScale;

            if ( segmentWidth < 0.0001f )
            {
                continue;
            }

            if ( gpuX + segmentWidth > barRight )
            {
                segmentWidth = barRight - gpuX;
            }

            const BarColor& color = palette[marker.colorIndex % BAR_PALETTE_SIZE];
            draw.Quad( gpuX, gpuBarY, gpuX + segmentWidth, gpuBarY + barHeight, color.r, color.g, color.b, 1.0f );

            gpuX += segmentWidth;
        }

        if ( absolute && gpuX < barRight )
        {
            draw.Quad( gpuX, gpuBarY, barRight, gpuBarY + barHeight, 0.85f, 0.85f, 0.85f, 0.7f );
        }
    }

    const float legendY = gpuLeafCount > 0 ? gpuBarY - barGap - legendHeight * 0.5f : cpuBarY - barGap - legendHeight * 0.5f;

    const float legendFontSize = fontSize * 0.85f;
    const float swatchWidth = legendFontSize * 1.5f;
    const float swatchHeight = legendFontSize;
    const float legendLeft = barLeft + cpuLabelWidth;
    const float legendSpacing = pad * 0.4f;

    int legendIndices[MAX_MARKERS];
    int legendCount = 0;
    bool inLegend[MAX_MARKERS] = {};

    for ( int leafIndex = 0; leafIndex < cpuLeafCount; ++leafIndex )
    {
        inLegend[cpuLeaves[leafIndex]] = true;
        legendIndices[legendCount++] = cpuLeaves[leafIndex];
    }

    for ( int leafIndex = 0; leafIndex < gpuLeafCount; ++leafIndex )
    {
        if ( !inLegend[gpuLeaves[leafIndex]] )
        {
            inLegend[gpuLeaves[leafIndex]] = true;
            legendIndices[legendCount++] = gpuLeaves[leafIndex];
        }
    }

    float legendX = legendLeft;
    float currentLegendY = legendY;

    for ( int legendIndex = 0; legendIndex < legendCount; ++legendIndex )
    {
        const Marker& marker = markers[legendIndices[legendIndex]];
        const BarColor& color = palette[marker.colorIndex % BAR_PALETTE_SIZE];
        const float labelWidth = draw.MeasureText( legendFontSize, marker.leafName );
        const float entryWidth = swatchWidth + legendSpacing + labelWidth + pad;

        if ( legendX + entryWidth > barRight && legendX > legendLeft + 0.001f )
        {
            legendX = legendLeft;
            currentLegendY -= legendHeight;
        }

        draw.Quad( legendX, currentLegendY, legendX + swatchWidth, currentLegendY + swatchHeight, color.r, color.g, color.b,
                   1.0f );

        draw.Text( legendX + swatchWidth + legendSpacing, currentLegendY, legendFontSize, 0.85f, 0.85f, 0.85f, "%s",
                   marker.leafName );

        legendX += entryWidth;
    }
}
} // namespace SkullbonezCore::UI
