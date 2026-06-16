/*
File: SkullbonezSource/UI/UITabProfiler.h
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
  - SkullbonezSource/UI/UITabProfiler.cpp
  - Agentic/Reference/comment-style-guide.md
*/
#pragma once

#include <cstdint>

namespace SkullbonezCore
{
namespace UI
{

class UIDrawContext;
struct InGameUIFrameData;

namespace ProfilerTab
{

constexpr int MAX_MARKERS = 128;
constexpr int HISTOGRAM_SAMPLE_COUNT = 120;

struct TimelineSegment
{
    float startMs = 0.0f;
    float durationMs = 0.0f;
    bool isFilled = false;
};

struct PerformanceHistogramSample
{
    float cpuMs = 0.0f;
    float gpuMs = 0.0f;
    float spikeMs = 0.0f;
};

struct UIProfilerTabState
{
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
    float histogramAxisMs = 16.67f;
};

bool TimelineEnabled( const UIProfilerTabState& state );
bool PerformanceHistogramEnabled( const UIProfilerTabState& state );

void SetExpandAll( UIProfilerTabState& state, bool expandAll );
void SetTimelineEnabled( UIProfilerTabState& state, bool enabled );
void SetPerformanceHistogramEnabled( UIProfilerTabState& state, bool enabled );

void ApplyDefaultExpansion( UIProfilerTabState& state );
void ApplyExpandAll( UIProfilerTabState& state );

int ContentHeight( const UIProfilerTabState& state );
bool HandleContentClick( UIProfilerTabState& state, int contentX, int contentY, float scrollY, int mouseX, int mouseY );

void PushPerformanceHistogramSample( UIProfilerTabState& state, float cpuMs, float gpuMs );
void DrawPerformanceHistogram( const UIProfilerTabState& state, const UIDrawContext& draw, const InGameUIFrameData& data );

void Draw( UIProfilerTabState& state,
           const UIDrawContext& draw,
           float contentX,
           float contentY,
           float contentW,
           float contentH,
           float scrollY );

} // namespace ProfilerTab
} // namespace UI
} // namespace SkullbonezCore
