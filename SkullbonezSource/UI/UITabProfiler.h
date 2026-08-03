/*
File: SkullbonezSource/UI/UITabProfiler.h
Purpose:
  Implements UI TabProfiler widgets, layout, drawing, or UI state for the in-engine controls.

Summary:
  Retains bounded profiler snapshots, expansion state,
  worker controls, timeline, and histogram interaction.

Glossary:
  Profiler frame snapshot: Fixed per-frame values consumed by the profiler tab;
    it never borrows live runtime or development-tool owners.

Invariants:
  - Draw geometry and hit testing must be derived from the same layout
    constants.
  - Optional Tracy instrumentation never changes the detached snapshot layout;
    producers without Tracy leave its capability flags false.

Related:
  - SkullbonezSource/UI/UITabProfiler.cpp
  - Agentic/Reference/engine-glossary.md
*/
#pragma once

#include "UICheckBox.h"
#include "UISlider.h"

#include <cstdint>

namespace SkullbonezCore
{
namespace UI
{

class UIDrawContext;
struct InGameUIFrameData;
struct InGameUIInputResult;

namespace ProfilerTab
{

constexpr int MAX_MARKERS = 192;
constexpr int MAX_WORKER_CORE_SAMPLES = 128;
constexpr int HISTOGRAM_SAMPLE_COUNT = 120;
constexpr int SLIDER_WORKER_THREADS = 19;

// Concept: FrameSnapshot is the profiler tab's runtime boundary. UiTextPass
// owns the live profiler/renderer reads, copies fixed-capacity values here, and
// the UI keeps this copy for drawing plus next-frame input hit tests.
// Lifetime: name pointers are borrowed from profiler/render diagnostics storage
// for immediate rendering; input paths only depend on counts, hashes, and depth.
struct MarkerSnapshot
{
    const char* name = "";
    const char* leafName = "";
    uint32_t hash = 0;
    int parentIndex = -1;
    int depth = 0;
    float lastFrameMs = 0.0f;
    float lastSelfMs = 0.0f;
    float avgMs = 0.0f;
    float selfAvgMs = 0.0f;
    float p50Ms = 0.0f;
    float p99Ms = 0.0f;
    float colorR = 0.0f;
    float colorG = 0.0f;
    float colorB = 0.0f;
};

struct WorkerCoreSampleSnapshot
{
    int workerIndex = -1;
    int jobCount = 0;
    float coreMs = 0.0f;
    float avgCoreMs = 0.0f;
    float spanStartMs = 0.0f;
    float spanEndMs = 0.0f;
};

struct DrawTraceNodeSnapshot
{
    const char* name = "";
    const char* leafName = "";
    uint32_t hash = 0;
    int parentIndex = -1;
    int depth = 0;
    int drawCallCount = 0;
    int vertexCount = 0;
    int instanceCount = 0;
};

struct DrawTraceSnapshot
{
    DrawTraceNodeSnapshot nodes[MAX_MARKERS] = {};
    int nodeCount = 0;
    int nodeOverflowCount = 0;
    int eventCount = 0;
    int eventOverflowCount = 0;
    int scopeMismatchCount = 0;
};

struct FrameSnapshot
{
    MarkerSnapshot markers[MAX_MARKERS] = {};
    int markerCount = 0;
    WorkerCoreSampleSnapshot workerCoreSamples[MAX_WORKER_CORE_SAMPLES] = {};
    int workerCoreSampleCount = 0;
    DrawTraceSnapshot drawTrace;

    // ABI invariant: detached UI values never change layout with optional
    // profiler instrumentation. Non-Tracy producers leave these false.
    bool tracyBuildEnabled = false;
    bool tracyInitialized = false;
    bool tracyViewerConnected = false;
};

struct TimelineSegment
{
    float startMs = 0.0f;
    float durationMs = 0.0f;
    bool isFilled = false;
};

struct PerformanceHistogramSample
{
    float markerMs[MAX_MARKERS + 1] = {};                // Per cached profiler option slot, in milliseconds.
    float markerSpikeMs[MAX_MARKERS + 1] = {};           // Non-zero when that slot owns a spike label for this sample.
    float secondaryMs = 0.0f;
    bool hasMarker[MAX_MARKERS + 1] = {};                // Guards sparse slots when markers are toggled or unavailable.
    bool hasSecondary = false;
};

struct UIProfilerTabState
{
    FrameSnapshot frame;                                 // Last bounded runtime snapshot used by layout and input hit tests.
    uint32_t expandedHashes[MAX_MARKERS] = {};
    int expandedHashCount = 0;
    uint32_t drawExpandedHashes[MAX_MARKERS] = {};
    int drawExpandedHashCount = 0;
    bool expandAllMarkers = false;
    bool defaultExpansionApplied = false;
    bool drawDefaultExpansionApplied = false;
    bool timelineEnabled = false;
    bool performanceHistogramEnabled = false;
    PerformanceHistogramSample histogramSamples[HISTOGRAM_SAMPLE_COUNT] = {};
    int histogramHead = 0;
    int histogramCount = 0;
    float histogramAxisMs = 33.3f;                       // Default F5 frame-total CPU scale: 0..33.3ms.
    double histogramAverageTextLastUpdateSeconds = -1.0; // Runtime seconds; -1 = footer average not latched yet.
    float histogramAverageCpuMs = 0.0f;                  // Latched selected-marker footer average refreshed on a 0.5s cadence.
    float histogramAverageWorkerMs = 0.0f;               // Latched worker-core footer average for Frame Total.
    bool histogramPanelInitialized = false;
    float histogramPanelX = 16.0f;
    float histogramPanelY = 16.0f;
    float histogramPanelW = 340.0f;
    float histogramPanelH = 166.0f;
    bool histogramDragging = false;
    bool histogramResizing = false;
    int histogramDragOffsetX = 0;
    int histogramDragOffsetY = 0;
    int histogramResizeStartMouseX = 0;
    int histogramResizeStartMouseY = 0;
    float histogramResizeStartW = 0.0f;
    float histogramResizeStartH = 0.0f;
    bool histogramSelectorOpen = false;
    int histogramSelectorScroll = 0;
    uint32_t histogramOptionHashes[MAX_MARKERS + 1] = {};
    bool histogramOptionFrameTotals[MAX_MARKERS + 1] = {};
    bool histogramOptionSelected[MAX_MARKERS + 1] = {};
    int histogramOptionCount = 0;
    bool histogramSelectionInitialized = false;
    UICheckBox workerToggle;
    UISlider workerThreadSlider;
    int previewWorkerThreads = -1;
    int restoreWorkerThreads = -1;
};

bool TimelineEnabled( const UIProfilerTabState& state );
bool PerformanceHistogramEnabled( const UIProfilerTabState& state );

void SetFrameSnapshot( UIProfilerTabState& state, const FrameSnapshot& frame );
void SetExpandAll( UIProfilerTabState& state, bool expandAll );
void SetTimelineEnabled( UIProfilerTabState& state, bool enabled );
void SetPerformanceHistogramEnabled( UIProfilerTabState& state, bool enabled );
bool PerformanceHistogramIsInteracting( const UIProfilerTabState& state );
void CancelPerformanceHistogramInteraction( UIProfilerTabState& state );

void ResetPreviewState( UIProfilerTabState& state );
void ApplyDefaultExpansion( UIProfilerTabState& state );
void ApplyExpandAll( UIProfilerTabState& state );

int ContentHeight( const UIProfilerTabState& state );
bool HandleContentClick( UIProfilerTabState& state, InGameUIInputResult& result, int& activeSlider, int contentX,
                         int contentY, float contentW, float scrollY, int mouseX, int mouseY, int currentWorkerThreads,
                         int maxWorkerThreads );
bool UpdateActiveSlider( UIProfilerTabState& state, int activeSlider, int mouseX, int maxWorkerThreads,
                         InGameUIInputResult& result );
bool CommitActiveSlider( UIProfilerTabState& state, int activeSlider, InGameUIInputResult& result );

bool HandlePerformanceHistogramInput( UIProfilerTabState& state, InGameUIInputResult& result, int screenW, int screenH,
                                      int mouseX, int mouseY, bool leftDown, bool leftPressed, bool leftReleased,
                                      int wheelDelta );
void PushPerformanceHistogramSample( UIProfilerTabState& state, const InGameUIFrameData& data );
void DrawPerformanceHistogram( UIProfilerTabState& state, const UIDrawContext& draw, const InGameUIFrameData& data );

void Draw( UIProfilerTabState& state, const UIDrawContext& draw, const InGameUIFrameData& data, float contentX,
           float contentY, float contentW, float contentH, float scrollY, int activeSlider );

} // namespace ProfilerTab
} // namespace UI
} // namespace SkullbonezCore
