/*
File: SkullbonezSource/Rendering/RenderProfilerPresentation.cpp
Purpose:
  Bridges Core profiler values to DX12 timing queries and diagnostic overlays.

Summary:
  The renderer owns GPU query lifetimes, platform GPU brackets, render counter
  publication, and text/bar presentation. Core exposes only marker and history
  values, keeping the dependency direction from Rendering down to Core.

Glossary:
  Render GPU timing owner: Renderer lifecycle object that owns query brackets.
  Completed sample: Hash plus milliseconds returned from a finished GPU query.
  Profiler frame view: Read-only Core spans consumed for presentation.
  Lane F: Fatal invariant path for an unbalanced renderer timing stack.

Invariants:
  - GPU begin/end scopes balance before frame or device boundaries.
  - Marker epochs invalidate stale backend query slots before reuse.
  - Presentation reads Core values and does not mutate profiler identity.

Related:
  - SkullbonezSource/Core/Profiler.cpp
  - SkullbonezSource/Rendering/RenderGpuTimingOwner.h
  - SkullbonezSource/Rendering/ProfilerOverlayPresenter.h
  - SkullbonezSource/Rendering/DX12/Dx12Diagnostics.h
*/
#include "../Core/Profiler.h"
#include "../Core/FatalError.h"
#include "../Core/PlatformProfiler.h"
#include "../Core/TracyClientOwner.h"
#include "../Core/WorkerPool.h"
#include "DX12/Dx12Diagnostics.h"
#include "ProfilerOverlayPresenter.h"
#include "RenderGpuTimingOwner.h"


using namespace SkullbonezCore::Core;
using namespace SkullbonezCore::Rendering;


#if defined( SKULLBONEZ_PROFILE_ENABLED )

#include <algorithm>
#include <cfloat>
#include <cstdio>
#include <cstring>
#include "Text.h"

RenderGpuTimingOwner::RenderGpuTimingOwner( Core::Profiler* profiler, Dx12Diagnostics* diagnostics )
    : m_profiler( profiler ), m_diagnostics( diagnostics ), m_markerEpoch( profiler ? profiler->MarkerEpoch() : 0 )
{
}


void RenderGpuTimingOwner::BeginFrame()
{
    if ( !m_profiler || !m_diagnostics )
    {
        return;
    }
    if ( m_openDepth != 0 )
    {
        SB_FATAL( "RenderGpuTimingOwner", "frame boundary reached with %d open GPU range(s)", m_openDepth );
    }

    // Invariant: Core clears marker identities only at FrameBegin. Mirror that
    // epoch before reading query slots so an old index can never populate a new
    // marker row after a profiler reset.
    if ( m_markerEpoch != m_profiler->MarkerEpoch() )
    {
        m_diagnostics->GpuTimerInvalidate();
        m_markerEpoch = m_profiler->MarkerEpoch();
    }

    int completedCount = 0;
    const Core::Profiler::ProfilerFrameView frame = m_profiler->FrameView();
    if ( m_diagnostics->GetCapabilities().supportsGpuTimers )
    {
        for ( std::size_t markerIndex = 0; markerIndex < frame.markers.size(); ++markerIndex )
        {
            const Core::Profiler::Marker& marker = frame.markers[markerIndex];
            if ( !marker.hasGpu )
            {
                continue;
            }
            float milliseconds = 0.0f;
            if ( m_diagnostics->GpuTimerRead( static_cast<int>( markerIndex ), milliseconds ) )
            {
                m_completedSamples[completedCount++] = { marker.hash, milliseconds };
            }
        }
    }
    m_profiler->ApplyGpuTimingSamples(
        std::span<const Core::Profiler::GpuTimingSample>(
            m_completedSamples,
            static_cast<std::size_t>( completedCount )
        )
    );

#if defined( TRACY_ENABLE )
    if ( SkullbonezCore::Core::DevelopmentTools::TracyClientOwner::CopyStatus().viewerConnected )
    {
        const RenderMemoryStats memory = m_diagnostics->GetRenderMemoryStats();
        SKORE_TRACY_PLOT_VALUE( "Counter/Render/DrawCalls", m_diagnostics->GetFrameDrawCallCount() );
        SKORE_TRACY_PLOT_VALUE( "Counter/Render/UploadUsedBytes", memory.uploadUsedBytes );
        SKORE_TRACY_PLOT_VALUE( "Counter/Render/UploadPeakBytes", memory.uploadPeakBytes );
        SKORE_TRACY_PLOT_VALUE( "Counter/Render/RTVDescriptorsUsed", memory.rtvDescriptorsUsed );
        SKORE_TRACY_PLOT_VALUE( "Counter/Render/DSVDescriptorsUsed", memory.dsvDescriptorsUsed );
        SKORE_TRACY_PLOT_VALUE( "Counter/Render/StaticSRVDescriptorsUsed", memory.srvStaticDescriptorsUsed );
        SKORE_TRACY_PLOT_VALUE( "Counter/Render/StaticSRVDescriptorsHighWater", memory.srvStaticDescriptorsHighWater );
        SKORE_TRACY_PLOT_VALUE(
            "Counter/Render/TransientSRVDescriptorsUsed",
            memory.srvTransientDescriptorsUsedThisFrame
        );

        SKORE_TRACY_PLOT_VALUE(
            "Counter/Render/TransientSRVDescriptorsPeak",
            memory.srvTransientDescriptorsPeakThisRun
        );

        SkullbonezCore::Core::DevelopmentTools::TracyClientOwner::PublishDevelopmentAllocationPlots();
    }
#endif
}


void RenderGpuTimingOwner::Begin( const char* fullPath, uint32_t hash )
{
    if ( !m_profiler )
    {
        return;
    }
    if ( m_openDepth >= Core::Profiler::MAX_DEPTH )
    {
        SB_FATAL( "RenderGpuTimingOwner", "GPU range stack overflow at %s", fullPath ? fullPath : "<null>" );
    }

    const int markerIndex = m_profiler->BeginRenderRecord( fullPath, hash );
    if ( markerIndex < 0 )
    {
        return;
    }
    OpenScope& scope = m_openScopes[m_openDepth++];
    scope = { fullPath, hash, markerIndex, false, false };

    if ( m_diagnostics && PlatformProfiler::IsEnabled() )
    {
        m_diagnostics->PlatformProfilerGpuBegin( fullPath, hash );
        scope.platformEventOpen = true;
    }
    if ( m_diagnostics && m_diagnostics->GetCapabilities().supportsGpuTimers )
    {
        m_profiler->MarkGpuMarkerWritten( markerIndex );
        m_diagnostics->GpuTimerBegin( markerIndex );
        scope.timerOpen = true;
    }
}


void RenderGpuTimingOwner::End( const char* fullPath, uint32_t hash )
{
    if ( !m_profiler )
    {
        return;
    }
    if ( SkullbonezCore::Threading::WorkerPool::IsCurrentThreadWorker() )
    {
        return;
    }
    if ( m_openDepth <= 0 )
    {
        SB_FATAL( "RenderGpuTimingOwner", "GPU range end without begin for %s", fullPath ? fullPath : "<null>" );
    }

    OpenScope& scope = m_openScopes[m_openDepth - 1];
    if ( scope.hash != hash )
    {
        SB_FATAL(
            "RenderGpuTimingOwner",
            "GPU range mismatch: expected %s, received %s",
            scope.fullPath ? scope.fullPath : "<null>",
            fullPath ? fullPath : "<null>"
        );
    }
    if ( scope.timerOpen && m_diagnostics )
    {
        m_diagnostics->GpuTimerEnd( scope.markerIndex );
    }
    if ( scope.platformEventOpen && m_diagnostics )
    {
        m_diagnostics->PlatformProfilerGpuEnd();
    }
    scope = OpenScope();
    --m_openDepth;
    m_profiler->EndRenderRecord( fullPath, hash );
}


void RenderGpuTimingOwner::InvalidateDevice()
{
    if ( m_openDepth != 0 )
    {
        SB_FATAL( "RenderGpuTimingOwner", "device invalidation reached with %d open GPU range(s)", m_openDepth );
    }
    if ( m_diagnostics )
    {
        m_diagnostics->GpuTimerInvalidate();
    }
    if ( m_profiler )
    {
        m_profiler->InvalidateGpuSamples();
        m_markerEpoch = m_profiler->MarkerEpoch();
    }
}


void ProfilerOverlayPresenter::RenderOverlay(
    const Core::Profiler::ProfilerFrameView& frame,
    Text::TextBatch& textBatch,
    SkullbonezCore::Rendering::Dx12GeometryOwner& renderCommands,
    float xLeft,
    float yAnchor,
    float lineHeight,
    float fSize,
    float fps,
    bool rightAnchored
) const
{
    using SkullbonezCore::Text::Text2d;
    using Marker = Core::Profiler::Marker;
    constexpr int MAX_MARKERS = Core::Profiler::MAX_MARKERS;
    const Marker* m_markers = frame.markers.data();
    const int m_markerCount = static_cast<int>( frame.markers.size() );

    bool anyGpu = false;
    for ( int i = 0; i < m_markerCount; ++i )
    {
        if ( m_markers[i].hasGpu && m_markers[i].gpuRingFilled > 0 )
        {
            anyGpu = true;
            break;
        }
    }

    // Layout constants
    const float padX = fSize * 0.6f;
    const float padY = lineHeight * 1.2f;

    float markerNameW = Text2d::MeasureText( fSize, "MARKER" );
    for ( int i = 0; i < m_markerCount; ++i )
    {
        char nameBuf[64] = { 0 };

        int spaces = m_markers[i].depth * 2;
        if ( spaces > 20 )
        {
            spaces = 20;
        }
        for ( int k = 0; k < spaces; ++k )
        {
            nameBuf[k] = ' ';
        }
        strcpy_s( nameBuf + spaces, sizeof( nameBuf ) - spaces, m_markers[i].leafName );
        markerNameW = (std::max)( markerNameW, Text2d::MeasureText( fSize, nameBuf ) );
    }

    // Column x-offsets
    const float colName = 0.0f;
    const float valueColStep = fSize * 7.0f;
    const float colAvg = markerNameW + fSize * 1.5f;
    const float colSelf = colAvg + valueColStep;
    const float colGpu = anyGpu ? colSelf + valueColStep : -1.0f;
    const float colP50 = anyGpu ? colGpu + valueColStep : colSelf + valueColStep;
    const float colP99 = colP50 + valueColStep;
    const float colMin = colP99 + valueColStep;
    const float colMax = colMin + valueColStep;

    // Dynamically size the panel: right edge of last column plus measured value width + right padding.
    // Using MeasureText ensures the background quad is always wide enough for the actual font metrics.
    const float colValW = Text2d::MeasureText( fSize, "9999.99" );
    const float panelW = colMax + colValW + padX;

    // Clamp rows to available screen height so the panel never overflows the top of the screen.
    const float screenH = Text2d::HalfH( textBatch ) * 2.0f;
    const int maxRows = static_cast<int>( ( screenH - 4.0f * padY ) / lineHeight );
    const int visRows = ( m_markerCount + 2 < maxRows ) ? m_markerCount + 2 : maxRows;
    const float rowsHeight = static_cast<float>( visRows ) * lineHeight;

    // When right-anchored, xLeft is the desired right edge of the panel; resolve to true xLeft.
    if ( rightAnchored )
    {
        xLeft = xLeft + padX - panelW;
    }

    const float yBottom = yAnchor + padY;
    const float yTop = yBottom + rowsHeight;

    // Background quad
    Text2d::Render2dQuad(
        textBatch,
        renderCommands,
        xLeft - padX,
        yBottom,
        xLeft - padX + panelW,
        yTop + padY,
        0.12f,
        0.12f,
        0.12f,
        0.5f
    );

    // Color palette
    const float hdrR = 1.0f, hdrG = 0.85f, hdrB = 0.2f; // gold header
    const float colR = 0.6f, colG = 0.6f, colB = 0.6f;  // grey column headers
    const float gpuR = 0.4f, gpuG = 0.8f, gpuB = 1.0f;  // cyan for GPU values

    // Frame samples are work-only; VsyncWait remains a separate marker row.
    static constexpr uint32_t kFrameHash = ::HashStr( "Frame" );
    static constexpr uint32_t kVsyncHash = ::HashStr( "Frame/VsyncWait" );
    float frameAvgMs = 0.0f;
    for ( int i = 0; i < m_markerCount; ++i )
    {
        if ( m_markers[i].hash == kFrameHash )
        {
            frameAvgMs = m_markers[i].avgMs;
        }
    }
    const float cpuMs = frameAvgMs;

    // Header line
    float y = yTop;
    Text2d::Render2dTextColor( textBatch, xLeft, y, fSize, hdrR, hdrG, hdrB, "CPU: %.2f ms  FPS: %.1f", cpuMs, fps );
    y -= lineHeight;

    // Column labels
    Text2d::Render2dTextColor( textBatch, xLeft + colName, y, fSize, colR, colG, colB, "MARKER" );
    Text2d::Render2dTextColor( textBatch, xLeft + colAvg, y, fSize, colR, colG, colB, "CPU" );
    Text2d::Render2dTextColor( textBatch, xLeft + colSelf, y, fSize, colR, colG, colB, "SELF" );
    if ( anyGpu )
    {
        Text2d::Render2dTextColor( textBatch, xLeft + colGpu, y, fSize, gpuR, gpuG, gpuB, "GPU" );
    }
    Text2d::Render2dTextColor( textBatch, xLeft + colP50, y, fSize, colR, colG, colB, "P50" );
    Text2d::Render2dTextColor( textBatch, xLeft + colP99, y, fSize, colR, colG, colB, "P99" );
    Text2d::Render2dTextColor( textBatch, xLeft + colMin, y, fSize, colR, colG, colB, "MIN" );
    Text2d::Render2dTextColor( textBatch, xLeft + colMax, y, fSize, colR, colG, colB, "MAX" );
    y -= lineHeight;

    // Traffic-light threshold: proportion of CPU budget
    float budgetMs = ( cpuMs > 0.001f ) ? cpuMs : 1.0f;

    // Marker rows -- VsyncWait rendered last (at bottom).
    auto renderMarkerRow = [&]( const Marker& m )
    {
        if ( y < yBottom )
        {
            y -= lineHeight;
            return;
        }
        char nameBuf[64] = { 0 };

        int spaces = m.depth * 2;
        if ( spaces > 20 )
        {
            spaces = 20;
        }
        for ( int k = 0; k < spaces; ++k )
        {
            nameBuf[k] = ' ';
        }
        strcpy_s( nameBuf + spaces, sizeof( nameBuf ) - spaces, m.leafName );

        float mr, mg, mb;
        if ( m.hash == kVsyncHash )
        {
            mr = 0.5f;
            mg = 0.5f;
            mb = 0.5f;
        }
        else
        {
            float ratio = m.avgMs / budgetMs;
            if ( ratio < 0.15f )
            {
                mr = 0.3f;
                mg = 0.9f;
                mb = 0.3f;
            }
            else if ( ratio < 0.5f )
            {
                mr = 1.0f;
                mg = 0.7f;
                mb = 0.2f;
            }
            else
            {
                mr = 1.0f;
                mg = 0.3f;
                mb = 0.3f;
            }
        }

        Text2d::Render2dTextColor( textBatch, xLeft + colName, y, fSize, mr, mg, mb, "%s", nameBuf );
        Text2d::Render2dTextColor( textBatch, xLeft + colAvg, y, fSize, mr, mg, mb, "%6.2f", m.avgMs );
        const float selfMs = m.selfAvgMs > 0.0f ? m.selfAvgMs : m.lastSelfMs;
        Text2d::Render2dTextColor( textBatch, xLeft + colSelf, y, fSize, mr, mg, mb, "%6.2f", selfMs );
        if ( anyGpu )
        {
            if ( m.hasGpu && m.gpuRingFilled > 0 )
            {
                Text2d::Render2dTextColor( textBatch, xLeft + colGpu, y, fSize, gpuR, gpuG, gpuB, "%6.2f", m.gpuAvgMs );
            }
            else
            {
                Text2d::Render2dTextColor( textBatch, xLeft + colGpu, y, fSize, colR, colG, colB, "    - " );
            }
        }
        Text2d::Render2dTextColor( textBatch, xLeft + colP50, y, fSize, mr, mg, mb, "%6.2f", m.p50Ms );
        Text2d::Render2dTextColor( textBatch, xLeft + colP99, y, fSize, mr, mg, mb, "%6.2f", m.p99Ms );
        float displayMin = ( m.ringFilled > 0 ) ? m.minMs : 0.0f;
        float displayMax = ( m.ringFilled > 0 ) ? m.maxMs : 0.0f;
        Text2d::Render2dTextColor( textBatch, xLeft + colMin, y, fSize, mr, mg, mb, "%6.2f", displayMin );
        Text2d::Render2dTextColor( textBatch, xLeft + colMax, y, fSize, mr, mg, mb, "%6.2f", displayMax );
        y -= lineHeight;
    };

    // Build children lists (fixed arrays — no heap allocation).
    // children[i] holds indices of markers whose parentIndex == i, in registration order.
    int childBuf[MAX_MARKERS][MAX_MARKERS]; // [parent][slot]
    int childCount[MAX_MARKERS] = {};

    for ( int i = 0; i < m_markerCount; ++i )
    {
        int p = m_markers[i].parentIndex;
        if ( p >= 0 && p < m_markerCount )
        {
            childBuf[p][childCount[p]++] = i;
        }
    }

    // Depth-first tree walk: roots first in registration order, then their children, etc.
    // Preserves execution order within each sibling group.
    int dfsStack[MAX_MARKERS];
    int dfsTop = 0;
    for ( int i = m_markerCount - 1; i >= 0; --i )
    {
        if ( m_markers[i].parentIndex == -1 && m_markers[i].hash != kVsyncHash )
        {
            dfsStack[dfsTop++] = i;
        }
    }
    while ( dfsTop > 0 )
    {
        int idx = dfsStack[--dfsTop];
        renderMarkerRow( m_markers[idx] );
        for ( int j = childCount[idx] - 1; j >= 0; --j )
        {
            dfsStack[dfsTop++] = childBuf[idx][j];
        }
    }
    for ( int i = 0; i < m_markerCount; ++i )
    {
        if ( m_markers[i].hash == kVsyncHash )
        {
            renderMarkerRow( m_markers[i] );
            break;
        }
    }
}


/* -- RenderBarOverlay
-------------------------------------------------------------------------------------------------------------------------------------------

    Renders a visual profiler panel with horizontal stacked bars:
      - One bar for CPU timing (all leaf markers that have CPU timing)
      - One bar for GPU timing (only leaf markers that have GPU timing)

    Each bar is subdivided into coloured segments proportional to the leaf marker's avgMs.
    A colour legend is rendered below the bars.

    absolute=false: "Normalized" — segments fill the entire bar width (relative proportions).
    absolute=true:  "Absolute"  — bar width = full frame time; white segment = idle/vsync remainder.

    The panel is designed with vertical headroom for future multi-core stacking (CPU bar per thread).
-----------------------------------------------------------------------------------------------------------------------------------------------------------------*/
void ProfilerOverlayPresenter::RenderBarOverlay(
    const Core::Profiler::ProfilerFrameView& frame,
    Text::TextBatch& textBatch,
    SkullbonezCore::Rendering::Dx12GeometryOwner& renderCommands,
    float xLeft,
    float yBottom,
    float panelWidth,
    float panelHeight,
    bool absolute
) const
{
    using SkullbonezCore::Text::Text2d;
    using Marker = Core::Profiler::Marker;
    using BarColor = Core::Profiler::BarColor;
    constexpr int MAX_MARKERS = Core::Profiler::MAX_MARKERS;
    constexpr int BAR_PALETTE_SIZE = Core::Profiler::BAR_PALETTE_SIZE;
    const auto& BAR_PALETTE = Core::Profiler::BAR_PALETTE;
    const Marker* m_markers = frame.markers.data();
    const int m_markerCount = static_cast<int>( frame.markers.size() );

    // Identify leaf markers: a marker is a leaf if no other marker has it as parentIndex.
    // Also skip "Frame" (top-level container) from appearing as a bar segment.
    bool isLeaf[MAX_MARKERS] = {};
    for ( int i = 0; i < m_markerCount; ++i )
    {
        isLeaf[i] = true;
    }
    for ( int i = 0; i < m_markerCount; ++i )
    {
        if ( m_markers[i].parentIndex >= 0 )
        {
            isLeaf[m_markers[i].parentIndex] = false;
        }
    }

    // Exclude VsyncWait from CPU segments (it becomes idle in absolute mode).
    static constexpr uint32_t kFrameHash = ::HashStr( "Frame" );
    static constexpr uint32_t kVsyncHash = ::HashStr( "Frame/VsyncWait" );

    // Gather leaf indices for CPU and GPU bars
    int cpuLeaves[MAX_MARKERS];
    int cpuLeafCount = 0;
    int gpuLeaves[MAX_MARKERS];
    int gpuLeafCount = 0;
    float cpuTotalMs = 0.0f;
    float gpuTotalMs = 0.0f;

    for ( int i = 0; i < m_markerCount; ++i )
    {
        if ( !isLeaf[i] )
        {
            continue;
        }
        if ( m_markers[i].hash == kFrameHash )
        {
            continue;
        }

        bool isIdle = ( m_markers[i].hash == kVsyncHash );
        if ( !isIdle )
        {
            cpuLeaves[cpuLeafCount++] = i;
            cpuTotalMs += m_markers[i].avgMs;
        }
        if ( m_markers[i].hasGpu && m_markers[i].gpuRingFilled > 0 && !isIdle )
        {
            gpuLeaves[gpuLeafCount++] = i;
            gpuTotalMs += m_markers[i].gpuAvgMs;
        }
    }

    // In absolute mode, the bar width represents the full frame time.
    // The idle portion is drawn as white at the end.
    float frameMs = 0.0f;
    for ( int i = 0; i < m_markerCount; ++i )
    {
        if ( m_markers[i].hash == kFrameHash )
        {
            frameMs = m_markers[i].avgMs;
            break;
        }
    }
    if ( frameMs < 0.001f )
    {
        frameMs = 16.67f; // fallback: assume 60 Hz
    }

    // Layout constants
    const float pad = panelHeight * 0.06f;
    const float barHeight = panelHeight * 0.18f;    // each bar slightly smaller to make room
    const float barGap = panelHeight * 0.09f;       // larger gap between bars
    const float legendHeight = panelHeight * 0.20f; // legend row (increased for wrapping)
    const float titleH = panelHeight * 0.12f;       // title line (bigger)
    const float barX0 = xLeft + pad;
    const float barX1 = xLeft + panelWidth - pad;
    const float fSz = barHeight * 0.45f; // text size proportional to bar

    // Background quad
    Text2d::BatchQuad(
        textBatch,
        renderCommands,
        xLeft,
        yBottom,
        xLeft + panelWidth,
        yBottom + panelHeight,
        0.06f,
        0.06f,
        0.10f,
        0.90f
    );

    // Title
    float ty = yBottom + panelHeight - pad - titleH;
    const char* title = absolute ? "PROFILER BARS (ABSOLUTE)" : "PROFILER BARS (NORMALIZED)";
    Text2d::Render2dTextColor( textBatch, barX0, ty + titleH * 0.35f, fSz * 1.05f, 1.0f, 0.85f, 0.35f, "%s", title );

    // Totals (right-aligned on title row)
    char totalsBuf[128] = { 0 };
    if ( absolute )
    {
        // In absolute mode show CPU sum, GPU sum, and overall frame time
        sprintf_s(
            totalsBuf,
            sizeof( totalsBuf ),
            "CPU: %.2f ms  GPU: %.2f ms  Frame: %.2f ms",
            cpuTotalMs,
            gpuTotalMs,
            frameMs
        );
    }
    else
    {
        sprintf_s( totalsBuf, sizeof( totalsBuf ), "CPU: %.2f ms  GPU: %.2f ms", cpuTotalMs, gpuTotalMs );
    }
    float totalsW = Text2d::MeasureText( fSz * 0.9f, totalsBuf );
    Text2d::Render2dTextColor(
        textBatch,
        barX1 - totalsW,
        ty + titleH * 0.35f,
        fSz * 0.9f,
        0.85f,
        0.85f,
        0.85f,
        "%s",
        totalsBuf
    );

    // --- CPU bar ---
    float cpuBarY = ty - barGap - barHeight * 0.4f; // shift down so title doesn't overlap
    Text2d::Render2dTextColor( textBatch, barX0, cpuBarY + barHeight * 0.3f, fSz, 0.85f, 0.85f, 0.85f, "CPU" );
    float cpuLabelW = Text2d::MeasureText( fSz, "CPU " ) + pad * 0.5f;
    float cpuBarX0 = barX0 + cpuLabelW;
    float cpuBarWidth = barX1 - cpuBarX0;

    // Draw background (dark grey = empty / absolute idle)
    Text2d::BatchQuad(
        textBatch,
        renderCommands,
        cpuBarX0,
        cpuBarY,
        barX1,
        cpuBarY + barHeight,
        0.15f,
        0.15f,
        0.15f,
        1.0f
    );

    // Scale bars either against the absolute frame or the CPU subtotal.
    float cpuScale = 1.0f;
    if ( absolute )
    {
        cpuScale = ( frameMs > 0.001f ) ? ( cpuBarWidth / frameMs ) : 0.0f;
    }
    else
    {
        cpuScale = ( cpuTotalMs > 0.001f ) ? ( cpuBarWidth / cpuTotalMs ) : 0.0f;
    }

    float cx = cpuBarX0;
    for ( int i = 0; i < cpuLeafCount; ++i )
    {
        const Marker& m = m_markers[cpuLeaves[i]];
        float segW = m.avgMs * cpuScale;
        if ( segW < 0.0001f )
        {
            continue;
        }
        if ( cx + segW > barX1 )
        {
            segW = barX1 - cx; // Keep the segment inside the panel.
        }
        const BarColor& c = BAR_PALETTE[m.colorIndex % BAR_PALETTE_SIZE];
        Text2d::BatchQuad(
            textBatch,
            renderCommands,
            cx,
            cpuBarY,
            cx + segW,
            cpuBarY + barHeight,
            c.r,
            c.g,
            c.b,
            1.0f
        );

        cx += segW;
    }

    // Absolute mode: remaining space = white (idle)
    if ( absolute && cx < barX1 )
    {
        Text2d::BatchQuad(
            textBatch,
            renderCommands,
            cx,
            cpuBarY,
            barX1,
            cpuBarY + barHeight,
            0.85f,
            0.85f,
            0.85f,
            0.7f
        );
    }

    // --- GPU bar ---
    float gpuBarY = cpuBarY - barGap - barHeight;
    if ( gpuLeafCount > 0 )
    {
        Text2d::Render2dTextColor( textBatch, barX0, gpuBarY + barHeight * 0.3f, fSz, 0.4f, 0.8f, 1.0f, "GPU" );
        float gpuLabelW = cpuLabelW; // align with CPU bar
        float gpuBarX0 = barX0 + gpuLabelW;

        Text2d::BatchQuad(
            textBatch,
            renderCommands,
            gpuBarX0,
            gpuBarY,
            barX1,
            gpuBarY + barHeight,
            0.15f,
            0.15f,
            0.15f,
            1.0f
        );

        float gpuScale = 1.0f;
        if ( absolute )
        {
            gpuScale = ( frameMs > 0.001f ) ? ( ( barX1 - gpuBarX0 ) / frameMs ) : 0.0f;
        }
        else
        {
            gpuScale = ( gpuTotalMs > 0.001f ) ? ( ( barX1 - gpuBarX0 ) / gpuTotalMs ) : 0.0f;
        }

        float gx = gpuBarX0;
        for ( int i = 0; i < gpuLeafCount; ++i )
        {
            const Marker& m = m_markers[gpuLeaves[i]];
            float segW = m.gpuAvgMs * gpuScale;
            if ( segW < 0.0001f )
            {
                continue;
            }
            if ( gx + segW > barX1 )
            {
                segW = barX1 - gx;
            }
            const BarColor& c = BAR_PALETTE[m.colorIndex % BAR_PALETTE_SIZE];
            Text2d::BatchQuad(
                textBatch,
                renderCommands,
                gx,
                gpuBarY,
                gx + segW,
                gpuBarY + barHeight,
                c.r,
                c.g,
                c.b,
                1.0f
            );

            gx += segW;
        }

        if ( absolute && gx < barX1 )
        {
            Text2d::BatchQuad(
                textBatch,
                renderCommands,
                gx,
                gpuBarY,
                barX1,
                gpuBarY + barHeight,
                0.85f,
                0.85f,
                0.85f,
                0.7f
            );
        }
    }

    // --- Colour legend (below bars) ---
    // Place legend a little further down to avoid overlapping the bars and title.
    float legendY =
        ( gpuLeafCount > 0 ) ? ( gpuBarY - barGap - legendHeight * 0.5f ) : ( cpuBarY - barGap - legendHeight * 0.5f );
    float legendFSz = fSz * 0.85f;
    float swatchW = legendFSz * 1.5f; // colour swatch width
    float swatchH = legendFSz;
    float legendX = barX0 + cpuLabelW;
    float legendSpacing = pad * 0.4f;

    // Collect unique leaf markers for legend (union of CPU + GPU leaves, no duplicates)
    int legendIndices[MAX_MARKERS];
    int legendCount = 0;
    bool inLegend[MAX_MARKERS] = {};

    for ( int i = 0; i < cpuLeafCount; ++i )
    {
        inLegend[cpuLeaves[i]] = true;
        legendIndices[legendCount++] = cpuLeaves[i];
    }
    for ( int i = 0; i < gpuLeafCount; ++i )
    {
        if ( !inLegend[gpuLeaves[i]] )
        {
            inLegend[gpuLeaves[i]] = true;
            legendIndices[legendCount++] = gpuLeaves[i];
        }
    }

    // Render legend entries — wrap to next row if they overflow the bar width
    float lx = legendX;
    float ly = legendY;
    for ( int i = 0; i < legendCount; ++i )
    {
        const Marker& m = m_markers[legendIndices[i]];
        const BarColor& c = BAR_PALETTE[m.colorIndex % BAR_PALETTE_SIZE];
        float labelW = Text2d::MeasureText( legendFSz, m.leafName );
        float entryW = swatchW + legendSpacing + labelW + pad;

        // Wrap if this entry would overflow
        if ( lx + entryW > barX1 && lx > legendX + 0.001f )
        {
            lx = legendX;
            ly -= legendHeight;
        }

        // Swatch
        Text2d::BatchQuad( textBatch, renderCommands, lx, ly, lx + swatchW, ly + swatchH, c.r, c.g, c.b, 1.0f );
        // Label
        Text2d::Render2dTextColor(
            textBatch,
            lx + swatchW + legendSpacing,
            ly,
            legendFSz,
            0.85f,
            0.85f,
            0.85f,
            "%s",
            m.leafName
        );
        lx += entryW;
    }

    // Flush all batched quads in one draw call before the text labels are flushed by the caller.
    // This gives the full bar overlay exactly 2 draw calls: one for all quads, one for all text.
    Text2d::FlushQuads( textBatch, renderCommands );
}


#else // SKULLBONEZ_PROFILE_ENABLED

// Why: unprofiled tools/tests retain the same renderer-facing no-op contract.
RenderGpuTimingOwner::RenderGpuTimingOwner( Core::Profiler* profiler, Dx12Diagnostics* diagnostics )
    : m_profiler( profiler ), m_diagnostics( diagnostics )
{
}


void RenderGpuTimingOwner::BeginFrame()
{
}


void RenderGpuTimingOwner::InvalidateDevice()
{
}


void RenderGpuTimingOwner::Begin( const char*, uint32_t )
{
}


void RenderGpuTimingOwner::End( const char*, uint32_t )
{
}


void ProfilerOverlayPresenter::RenderOverlay(
    const Core::Profiler::ProfilerFrameView&,
    Text::TextBatch&,
    Rendering::Dx12GeometryOwner&,
    float,
    float,
    float,
    float,
    float,
    bool
) const
{
}


void ProfilerOverlayPresenter::RenderBarOverlay(
    const Core::Profiler::ProfilerFrameView&,
    Text::TextBatch&,
    Rendering::Dx12GeometryOwner&,
    float,
    float,
    float,
    float,
    bool
) const
{
}


#endif // SKULLBONEZ_PROFILE_ENABLED
