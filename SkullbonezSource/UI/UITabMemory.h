/*
File: SkullbonezSource/UI/UITabMemory.h
Purpose:
  Declares the in-engine memory diagnostics tab.

Summary:
  The memory tab shows cached runtime diagnostics and emits replay-memory policy
  requests. Runtime owns process sampling and replay reconfiguration; the F6
  overlay reads tracked/cached counters and reserve-growth events without
  initiating diagnostics work.

Glossary:
  Reserve growth event: A replay-approved vector reserve bump with a named owner,
    target structure, frame, capacity delta, and byte size.
  Main memory: Coarsely reconciled process, replay, and game-object memory stats.
  Replay policy: Preset, retention, and budget request displayed by the Memory
    tab and applied by ReplayRuntime.
  Memory waterline: Compact F6 overlay that tracks known engine memory and
    pinned reserve-growth events without polling process memory.

Invariants:
  - Drawing the tab must not allocate, resample memory, or resize replay rings
    directly.
  - Reserve-growth rows are fixed-frame-data entries copied from the allocator's
    no-heap diagnostics ring.
  - The F6 overlay retains important allocation events in fixed storage so
    steady gameplay diagnostics do not allocate while reporting allocations.

Related:
  - SkullbonezSource/UI/UITabMemory.cpp
  - SkullbonezSource/Runtime/Allocation/RuntimeReserveAllocator.h
*/
#pragma once

#include "../Runtime/Allocation/RuntimeReserveAllocator.h"
#include "UIButton.h"
#include "UISlider.h"

#include <cstdint>

namespace SkullbonezCore
{
namespace UI
{

class UIDrawContext;
struct InGameUIFrameData;
struct InGameUIInputResult;

namespace MemoryTab
{

constexpr int MEMORY_OVERLAY_SAMPLE_COUNT = 120;
constexpr int MEMORY_OVERLAY_PINNED_EVENT_MAX = 64;
constexpr int MEMORY_REPLAY_PRESET_COUNT = 3;

struct MemoryOverlaySample
{
    uint64_t totalBytes = 0;
    uint64_t trackedBytes = 0;
    bool isFilled = false;
};

struct MemoryOverlayPinnedEvent
{
    Runtime::Allocation::RuntimeReserveGrowthEventView event;
    uint64_t levelBytes = 0; // Process-memory level observed when the event first reached the UI.
    double firstSeenSeconds = 0.0;
    bool isFilled = false;
};

struct UIMemoryOverlayState
{
    bool overlayEnabled = false;
    MemoryOverlaySample samples[MEMORY_OVERLAY_SAMPLE_COUNT] = {};
    int sampleHead = 0;
    int sampleCount = 0;
    uint64_t axisMinBytes = 0;
    uint64_t axisMaxBytes = 64ull * 1024ull * 1024ull;
    MemoryOverlayPinnedEvent pinnedEvents[MEMORY_OVERLAY_PINNED_EVENT_MAX] = {};
    int pinnedEventCount = 0;
    uint64_t lastObservedEventTotal = 0;
    uint64_t retainedOverflowEventCount = 0;
    UIButton replayPresetButtons[MEMORY_REPLAY_PRESET_COUNT];
    UISlider replayRetentionSlider;
    UISlider replayBudgetSlider;
    int previewRetentionSeconds = -1;
    int previewBudgetMiB = -1;
    int lastReplayPreset = 0;
    int lastRequestedRetentionSeconds = 60;
    int lastRequestedBudgetMiB = 256;
    int lastPresentationRetentionSeconds = 60;
    int lastSolverRetentionSeconds = 60;
    bool lastBudgetClamped = false;
    bool lastSolverWindowReduced = false;
};

int ContentHeight();
bool OverlayEnabled( const UIMemoryOverlayState& state );
void SetOverlayEnabled( UIMemoryOverlayState& state, bool enabled );
void PushOverlayFrame( UIMemoryOverlayState& state, const InGameUIFrameData& data );
void DrawOverlay( UIMemoryOverlayState& state,
                  const UIDrawContext& draw,
                  const InGameUIFrameData& data,
                  float preferredX,
                  float preferredY );
void Draw( const UIDrawContext& draw,
           UIMemoryOverlayState& state,
           const InGameUIFrameData& data,
           float contentX,
           float contentY,
           float contentW,
           float contentH,
           float scrolledY,
           int activeSlider,
           int mouseX,
           int mouseY );
bool HandleContentClick( UIMemoryOverlayState& state,
                         InGameUIInputResult& result,
                         int& activeSlider,
                         int mouseX,
                         int mouseY,
                         float contentX,
                         float scrolledY,
                         float contentW );
bool UpdateActiveSlider( UIMemoryOverlayState& state, int activeSlider, int mouseX, InGameUIInputResult& result );
bool CommitActiveSlider( UIMemoryOverlayState& state, int activeSlider, InGameUIInputResult& result );
void ResetPreviewState( UIMemoryOverlayState& state );

} // namespace MemoryTab
} // namespace UI
} // namespace SkullbonezCore
