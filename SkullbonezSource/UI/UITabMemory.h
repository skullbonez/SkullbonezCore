/*
File: SkullbonezSource/UI/UITabMemory.h
Purpose:
  Declares the in-engine memory diagnostics tab.

Mental model:
  The memory tab is a read-only view over cached runtime diagnostics. Runtime
  owns sampling process, replay, model, and reserve-growth data; UI only formats
  the frame snapshot it was handed.

Glossary:
  Reserve growth event: A replay-approved vector reserve bump with a named owner,
    target structure, frame, capacity delta, and byte size.
  Main memory: Coarsely reconciled process, replay, and game-object memory stats.
  Memory waterline: Compact F6 overlay that tracks the current process-memory
    level while keeping allocator events pinned instead of time-scrolling them.

Invariants:
  - Drawing the tab must not allocate or resample memory directly.
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

#include <cstdint>

namespace SkullbonezCore
{
namespace UI
{

class UIDrawContext;
struct InGameUIFrameData;

namespace MemoryTab
{

constexpr int MEMORY_OVERLAY_SAMPLE_COUNT = 120;
constexpr int MEMORY_OVERLAY_PINNED_EVENT_MAX = 64;

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
           const InGameUIFrameData& data,
           float contentX,
           float contentY,
           float contentW,
           float contentH,
           float scrolledY );

} // namespace MemoryTab
} // namespace UI
} // namespace SkullbonezCore
