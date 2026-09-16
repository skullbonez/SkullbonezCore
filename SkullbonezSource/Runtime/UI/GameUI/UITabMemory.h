// Memory controls and allocation-free process history. Owner rows are allocated
// capacities; the plotted waterline always measures private resident RAM.
#pragma once

#include "../../../Core/Allocation/RuntimeReserveAllocator.h"
#include "../../../UI/UIButton.h"
#include "../../../UI/UISlider.h"

#include <cstdint>

namespace SkullbonezCore
{
namespace UI
{

class UIDrawContext;
struct UIMemoryTabFrameView;
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
    SkullbonezCore::Core::Allocation::RuntimeReserveGrowthEventView event;
    uint64_t levelBytes = 0; // Process-memory level observed when the event first reached the UI.
    double firstSeenSeconds = 0.0;
    bool isFilled = false;
};

struct UIMemoryOverlayState
{
    bool overlayEnabled = false;
    uint64_t memoryPrivateBytes = 0;
    uint64_t memoryWorkingSetBytes = 0;
    uint64_t memoryCommitBytes = 0;
    uint64_t memoryPredictionCapacityBytes = 0;
    uint64_t memoryCapacityTableBytes = 0;
    double memorySampleSeconds = 0.0;
    bool memoryPrivateAvailable = false;
    bool memoryCapacityRowsValid = true;

    MemoryOverlaySample samples[MEMORY_OVERLAY_SAMPLE_COUNT] = {};
    UIRect dockedBounds;
    // Placement survives F6 toggles; native capture is requested through the UI owner.
    UIRect floatingBounds;
    UIRect floatingViewport;
    UIPoint pointerOffset;
    bool dragging = false;
    bool resizing = false;
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
void PushOverlayFrame( UIMemoryOverlayState& state, const UIMemoryTabFrameView& data );
void DrawOverlay( UIMemoryOverlayState& state, const UIDrawContext& draw, const UIMemoryTabFrameView& data, float preferredX, float preferredY );
void Draw( const UIDrawContext& draw,
           UIMemoryOverlayState& state,
           const UIMemoryTabFrameView& data,
           float contentX,
           float contentY,
           float contentW,
           float contentH,
           float scrolledY,
           int activeSlider,
           int mouseX,
           int mouseY );
bool HandleContentClick( UIMemoryOverlayState& state, InGameUIInputResult& result, int& activeSlider, int mouseX, int mouseY, float contentX, float scrolledY, float contentW );
bool UpdateActiveSlider( UIMemoryOverlayState& state, int activeSlider, int mouseX, InGameUIInputResult& result );
bool CommitActiveSlider( UIMemoryOverlayState& state, int activeSlider, InGameUIInputResult& result );
void ResetPreviewState( UIMemoryOverlayState& state );

} // namespace MemoryTab
} // namespace UI
} // namespace SkullbonezCore
