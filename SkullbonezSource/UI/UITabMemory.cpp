/*
File: SkullbonezSource/UI/UITabMemory.cpp
Purpose:
  Draws the in-engine memory diagnostics tab.

Summary:
  Runtime refreshes memory data for the Memory tab, while the F6 overlay renders
  tracked/cached counters and reserve-growth events without sampling process
  memory. Detached capacity rows are sorted by resident bytes for the tab. This
  file formats snapshots and emits replay-memory policy commands; Replay
  timeline composition owns the actual recorder reconfiguration.

Glossary:
  Allocation size: Bytes newly reserved by a successful growth request.
  Capacity span: Old, requested, and granted element capacities for the target.
  Peak utilisation: Session high-water divided by committed capacity.

Invariants:
  - Formatting uses stack buffers only; memory diagnostics must not allocate.
  - Event rows are newest-first because the most recent replay growth is usually
    the one the user is trying to understand.
  - Capacity rows are resident-bytes-descending with owner name as the stable
    tie break.
  - The F6 overlay keeps retained event pins in fixed arrays so diagnostics do
    not allocate while visualizing allocator activity.
  - Replay policy controls emit one-frame requests and never resize recorder
    storage from UI code.
  - Cohesion ruling: overlay counters, replay-policy controls, samples, and
    reserve-event rows all mutate one UIMemoryOverlayState and share one
    scroll/layout transaction; there is no independent owner seam to extract.

Related:
  - SkullbonezSource/UI/UITabMemory.h
  - SkullbonezSource/UI/UI.h
  - Agentic/Reference/engine-glossary.md
*/
#include "UITabMemory.h"

#include "../Core/MainMemoryStats.h"
#include "UI.h"
#include "UIDraw.h"
#include "UIStyle.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>

namespace
{
constexpr float MEMORY_SUMMARY_BLOCK_H = 310.0f;
constexpr float MEMORY_REPLAY_POLICY_BLOCK_H = 154.0f;
constexpr float MEMORY_PANEL_GAP = 14.0f;
constexpr float MEMORY_EVENT_HEADER_H = 62.0f;
constexpr float MEMORY_EVENT_ROW_H = 22.0f;
constexpr float MEMORY_EVENT_BOTTOM_PAD = 18.0f;
constexpr float MEMORY_CAPACITY_HEADER_H = 62.0f;
constexpr float MEMORY_CAPACITY_ROW_H = 22.0f;
constexpr float MEMORY_CAPACITY_BOTTOM_PAD = 18.0f;
constexpr float MEMORY_OVERLAY_PANEL_W = 340.0f;
constexpr float MEMORY_OVERLAY_PANEL_H = 166.0f;
constexpr float MEMORY_OVERLAY_MARGIN = 16.0f;
constexpr float MEMORY_OVERLAY_EVENT_RAIL_W = 74.0f;
constexpr uint64_t MEMORY_BYTES_PER_MIB = 1024ull * 1024ull;
constexpr int MEMORY_REPLAY_SLIDER_BASE = 9000;
constexpr int MEMORY_REPLAY_SLIDER_RETENTION = MEMORY_REPLAY_SLIDER_BASE + 0;
constexpr int MEMORY_REPLAY_SLIDER_BUDGET = MEMORY_REPLAY_SLIDER_BASE + 1;
constexpr int MEMORY_REPLAY_RETENTION_MIN = 1;
constexpr int MEMORY_REPLAY_RETENTION_MAX = 120;
constexpr int MEMORY_REPLAY_BUDGET_MIN_MIB = 32;
constexpr int MEMORY_REPLAY_BUDGET_MAX_MIB = 512;
constexpr int MEMORY_REPLAY_BUDGET_STEP_MIB = 16;
constexpr int MEMORY_REPLAY_DEFAULT_RETENTION_SECONDS = 60;
constexpr int MEMORY_REPLAY_DEFAULT_BUDGET_MIB = 256;

bool IsMemoryRowVisible( float contentY, float contentH, float rowY, float rowH )
{
    return rowY + rowH >= contentY && rowY <= contentY + contentH;
}

void FormatMemoryMiB( uint64_t bytes, char* out, std::size_t outSize )
{
    const double mib = static_cast<double>( bytes ) / ( 1024.0 * 1024.0 );

    if ( mib >= 1024.0 )
    {
        snprintf( out, outSize, "%.2f GiB", mib / 1024.0 );
    }
    else if ( mib >= 100.0 )
    {
        snprintf( out, outSize, "%.0f MiB", mib );
    }
    else
    {
        snprintf( out, outSize, "%.2f MiB", mib );
    }
}

void FormatAllocationBytes( uint64_t bytes, char* out, std::size_t outSize )
{
    if ( bytes >= 1024u * 1024u )
    {
        FormatMemoryMiB( bytes, out, outSize );
    }
    else if ( bytes >= 1024u )
    {
        snprintf( out, outSize, "%.1f KiB", static_cast<double>( bytes ) / 1024.0 );
    }
    else
    {
        snprintf( out, outSize, "%llu B", static_cast<unsigned long long>( bytes ) );
    }
}

const char* ReplayMemoryPresetLabel( int preset )
{
    switch ( preset )
    {
    case 1:
        return "Balanced";
    case 2:
        return "Compact";
    case 0:
    default:
        return "Lossless";
    }
}

int ReplayMemoryDisplayRetention( const SkullbonezCore::UI::MemoryTab::UIMemoryOverlayState& state )
{
    const int value = state.previewRetentionSeconds >= 0 ? state.previewRetentionSeconds
                                                         : state.lastRequestedRetentionSeconds;

    return std::clamp( value, MEMORY_REPLAY_RETENTION_MIN, MEMORY_REPLAY_RETENTION_MAX );
}

int ReplayMemoryDisplayBudgetMiB( const SkullbonezCore::UI::MemoryTab::UIMemoryOverlayState& state )
{
    const int value = state.previewBudgetMiB >= 0 ? state.previewBudgetMiB : state.lastRequestedBudgetMiB;
    return std::clamp( value, MEMORY_REPLAY_BUDGET_MIN_MIB, MEMORY_REPLAY_BUDGET_MAX_MIB );
}

void SetReplayMemoryPolicyCommand( SkullbonezCore::UI::InGameUIInputResult& result, int presetIndex, int retentionSeconds,
                                   int budgetMiB )
{
    result.commands.replayMemory.requestPolicy = true;
    result.commands.replayMemory.requestedPresetIndex = presetIndex;
    result.commands.replayMemory.requestedRetentionSeconds = retentionSeconds;
    result.commands.replayMemory.requestedBudgetMiB = budgetMiB;
}

void SetReplayPolicyControlBounds( SkullbonezCore::UI::MemoryTab::UIMemoryOverlayState& state, float contentX, float panelY,
                                   float contentW )
{
    const float buttonGap = 6.0f;
    const float buttonW = ( contentW - 28.0f -
                            buttonGap *
                                static_cast<float>( SkullbonezCore::UI::MemoryTab::MEMORY_REPLAY_PRESET_COUNT - 1 ) ) /
                          static_cast<float>( SkullbonezCore::UI::MemoryTab::MEMORY_REPLAY_PRESET_COUNT );

    const float buttonY = panelY + 35.0f;

    for ( int i = 0; i < SkullbonezCore::UI::MemoryTab::MEMORY_REPLAY_PRESET_COUNT; ++i )
    {
        state.replayPresetButtons[i].SetBounds( contentX + 14.0f + static_cast<float>( i ) * ( buttonW + buttonGap ),
                                                buttonY, buttonW, 24.0f );
    }

    state.replayRetentionSlider.SetBounds( contentX + 14.0f, panelY + 70.0f, contentW - 28.0f, 34.0f );
    state.replayBudgetSlider.SetBounds( contentX + 14.0f, panelY + 108.0f, contentW - 28.0f, 34.0f );
}

void RefreshReplayPolicySnapshot( SkullbonezCore::UI::MemoryTab::UIMemoryOverlayState& state,
                                  const SkullbonezCore::UI::InGameUIFrameData& data )
{
    state.lastReplayPreset = std::clamp( data.replayMemoryPreset, 0,
                                         SkullbonezCore::UI::MemoryTab::MEMORY_REPLAY_PRESET_COUNT - 1 );

    state.lastRequestedRetentionSeconds = data.replayMemoryRequestedRetentionSeconds > 0
                                              ? data.replayMemoryRequestedRetentionSeconds
                                              : MEMORY_REPLAY_DEFAULT_RETENTION_SECONDS;

    state.lastRequestedBudgetMiB = data.replayMemoryRequestedBudgetMiB > 0 ? data.replayMemoryRequestedBudgetMiB
                                                                           : MEMORY_REPLAY_DEFAULT_BUDGET_MIB;

    state.lastPresentationRetentionSeconds = data.replayMemoryPresentationRetentionSeconds > 0
                                                 ? data.replayMemoryPresentationRetentionSeconds
                                                 : state.lastRequestedRetentionSeconds;

    state.lastSolverRetentionSeconds = data.replayMemorySolverRetentionSeconds > 0 ? data.replayMemorySolverRetentionSeconds
                                                                                   : state.lastRequestedRetentionSeconds;

    state.lastBudgetClamped = data.replayMemoryBudgetClamped;
    state.lastSolverWindowReduced = data.replayMemorySolverWindowReduced;
}

void CopyShortLabel( const char* source, char* out, std::size_t outSize )
{
    if ( !out || outSize == 0 )
    {
        return;
    }

    const char* text = source ? source : "";
    const char* tail = std::strstr( text, "::" );

    while ( tail )
    {
        text = tail + 2;
        tail = std::strstr( text, "::" );
    }

    snprintf( out, outSize, "%s", text );
}

uint64_t CurrentTotalMemoryBytes( const SkullbonezCore::Core::MainMemoryStats& memory )
{
    if ( memory.process.available && memory.process.taskManagerBytes > 0u )
    {
        return memory.process.taskManagerBytes;
    }

    if ( memory.reconciledTotalBytes > 0u )
    {
        return memory.reconciledTotalBytes;
    }

    return memory.trackedEngineBytes;
}

uint64_t OverlayMinimumAxisSpan( uint64_t totalBytes )
{
    if ( totalBytes >= 4ull * 1024ull * MEMORY_BYTES_PER_MIB )
    {
        return 256ull * MEMORY_BYTES_PER_MIB;
    }

    if ( totalBytes >= 1024ull * MEMORY_BYTES_PER_MIB )
    {
        return 128ull * MEMORY_BYTES_PER_MIB;
    }

    return 32ull * MEMORY_BYTES_PER_MIB;
}

float MemoryOverlayYForBytes( const SkullbonezCore::UI::MemoryTab::UIMemoryOverlayState& state,
                              const SkullbonezCore::UI::UIRect& plot, uint64_t bytes )
{
    const uint64_t axisSpan = state.axisMaxBytes > state.axisMinBytes ? state.axisMaxBytes - state.axisMinBytes : 1u;
    const double normalized = ( static_cast<double>( bytes ) - static_cast<double>( state.axisMinBytes ) ) /
                              static_cast<double>( axisSpan );

    return plot.y + plot.h - std::clamp( static_cast<float>( normalized ), 0.0f, 1.0f ) * plot.h;
}

void DrawMemoryOverlayLineSegment( const SkullbonezCore::UI::UIDrawContext& draw, float x0, float y0, float x1, float y1,
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

int FindPinnedEventBySequence( const SkullbonezCore::UI::MemoryTab::UIMemoryOverlayState& state, uint64_t sequence )
{
    if ( sequence == 0u )
    {
        return -1;
    }

    for ( int i = 0; i < state.pinnedEventCount; ++i )
    {
        if ( state.pinnedEvents[i].isFilled && state.pinnedEvents[i].event.sequence == sequence )
        {
            return i;
        }
    }

    return -1;
}

int OldestPinnedEventIndex( const SkullbonezCore::UI::MemoryTab::UIMemoryOverlayState& state, bool grantedOnly )
{
    int result = -1;
    uint64_t oldestSequence = UINT64_MAX;

    for ( int i = 0; i < state.pinnedEventCount; ++i )
    {
        const SkullbonezCore::UI::MemoryTab::MemoryOverlayPinnedEvent& pinned = state.pinnedEvents[i];

        if ( !pinned.isFilled || ( grantedOnly && !pinned.event.granted ) )
        {
            continue;
        }

        if ( pinned.event.sequence < oldestSequence )
        {
            oldestSequence = pinned.event.sequence;
            result = i;
        }
    }

    return result;
}

int AllocatePinnedEventSlot( SkullbonezCore::UI::MemoryTab::UIMemoryOverlayState& state, bool newEventGranted )
{
    if ( state.pinnedEventCount < SkullbonezCore::UI::MemoryTab::MEMORY_OVERLAY_PINNED_EVENT_MAX )
    {
        return state.pinnedEventCount++;
    }

    // Invariant: denied/policy events are more diagnostic than ordinary replay
    // growth. Preserve denied pins first when the fixed event rail is full.
    int replacement = OldestPinnedEventIndex( state, true );

    if ( replacement >= 0 )
    {
        return replacement;
    }

    if ( !newEventGranted )
    {
        return OldestPinnedEventIndex( state, false );
    }

    return -1;
}

void RetainOverlayEvent( SkullbonezCore::UI::MemoryTab::UIMemoryOverlayState& state,
                         const SkullbonezCore::Core::Allocation::RuntimeReserveGrowthEventView& event, uint64_t levelBytes,
                         double now )
{
    if ( event.sequence == 0u || FindPinnedEventBySequence( state, event.sequence ) >= 0 )
    {
        return;
    }

    const int slot = AllocatePinnedEventSlot( state, event.granted );

    if ( slot < 0 )
    {
        ++state.retainedOverflowEventCount;
        return;
    }

    SkullbonezCore::UI::MemoryTab::MemoryOverlayPinnedEvent& pinned = state.pinnedEvents[slot];
    pinned.event = event;
    pinned.levelBytes = levelBytes;
    pinned.firstSeenSeconds = now;
    pinned.isFilled = true;
}

void ClearOverlayPinnedEvents( SkullbonezCore::UI::MemoryTab::UIMemoryOverlayState& state )
{
    for ( int i = 0; i < SkullbonezCore::UI::MemoryTab::MEMORY_OVERLAY_PINNED_EVENT_MAX; ++i )
    {
        state.pinnedEvents[i] = {};
    }

    state.pinnedEventCount = 0;
    state.retainedOverflowEventCount = 0;
}

void RefreshOverlayEvents( SkullbonezCore::UI::MemoryTab::UIMemoryOverlayState& state,
                           const SkullbonezCore::UI::InGameUIFrameData& data, uint64_t levelBytes )
{
    if ( data.reserveGrowthEventTotalCount < state.lastObservedEventTotal )
    {
        ClearOverlayPinnedEvents( state );
    }

    state.lastObservedEventTotal = data.reserveGrowthEventTotalCount;

    const int eventCount = std::clamp( data.reserveGrowthEventCount, 0,
                                       SkullbonezCore::UI::UI_RUNTIME_RESERVE_GROWTH_EVENT_MAX );

    for ( int i = eventCount - 1; i >= 0; --i )
    {
        RetainOverlayEvent( state, data.reserveGrowthEvents[i], levelBytes, data.now );
    }
}

void PushOverlaySample( SkullbonezCore::UI::MemoryTab::UIMemoryOverlayState& state,
                        const SkullbonezCore::UI::InGameUIFrameData& data )
{
    SkullbonezCore::UI::MemoryTab::MemoryOverlaySample& sample = state.samples[state.sampleHead];
    sample.totalBytes = CurrentTotalMemoryBytes( data.mainMemory );
    sample.trackedBytes = data.mainMemory.trackedEngineBytes;
    sample.isFilled = true;
    state.sampleHead = ( state.sampleHead + 1 ) % SkullbonezCore::UI::MemoryTab::MEMORY_OVERLAY_SAMPLE_COUNT;

    if ( state.sampleCount < SkullbonezCore::UI::MemoryTab::MEMORY_OVERLAY_SAMPLE_COUNT )
    {
        ++state.sampleCount;
    }
}

void RefreshOverlayAxis( SkullbonezCore::UI::MemoryTab::UIMemoryOverlayState& state,
                         const SkullbonezCore::UI::InGameUIFrameData& data )
{
    uint64_t minBytes = CurrentTotalMemoryBytes( data.mainMemory );
    uint64_t maxBytes = minBytes;

    for ( int i = 0; i < state.sampleCount; ++i )
    {
        const int sampleIndex = ( state.sampleHead - state.sampleCount + i +
                                  SkullbonezCore::UI::MemoryTab::MEMORY_OVERLAY_SAMPLE_COUNT ) %
                                SkullbonezCore::UI::MemoryTab::MEMORY_OVERLAY_SAMPLE_COUNT;

        const SkullbonezCore::UI::MemoryTab::MemoryOverlaySample& sample = state.samples[sampleIndex];

        if ( !sample.isFilled )
        {
            continue;
        }

        minBytes = (std::min)( minBytes, sample.totalBytes );
        maxBytes = (std::max)( maxBytes, sample.totalBytes );
    }

    for ( int i = 0; i < state.pinnedEventCount; ++i )
    {
        const SkullbonezCore::UI::MemoryTab::MemoryOverlayPinnedEvent& pinned = state.pinnedEvents[i];

        if ( !pinned.isFilled )
        {
            continue;
        }

        minBytes = (std::min)( minBytes, pinned.levelBytes );
        maxBytes = (std::max)( maxBytes, pinned.levelBytes );
    }

    const uint64_t currentTotal = CurrentTotalMemoryBytes( data.mainMemory );
    const uint64_t visibleSpan = maxBytes > minBytes ? maxBytes - minBytes : 0u;
    const uint64_t paddedSpan = (std::max)( OverlayMinimumAxisSpan( currentTotal ),
                                            visibleSpan + visibleSpan / 2u + 1u * MEMORY_BYTES_PER_MIB );

    const uint64_t center = minBytes + ( maxBytes - minBytes ) / 2u;
    state.axisMinBytes = center > paddedSpan / 2u ? center - paddedSpan / 2u : 0u;
    state.axisMaxBytes = state.axisMinBytes + paddedSpan;
}

void MemoryEventColor( const SkullbonezCore::Core::Allocation::RuntimeReserveGrowthEventView& event, float& r, float& g,
                       float& b )
{
    if ( !event.granted )
    {
        r = 0.96f;
        g = 0.32f;
        b = 0.24f;
        return;
    }

    const char* subsystem = event.subsystemName ? event.subsystemName : "";

    if ( std::strcmp( subsystem, "Replay" ) == 0 )
    {
        r = 0.38f;
        g = 0.84f;
        b = 0.96f;
    }
    else if ( std::strcmp( subsystem, "Renderer" ) == 0 || std::strcmp( subsystem, "DX12Telemetry" ) == 0 )
    {
        r = 0.78f;
        g = 0.56f;
        b = 0.96f;
    }
    else if ( std::strcmp( subsystem, "Physics" ) == 0 )
    {
        r = 0.72f;
        g = 0.92f;
        b = 0.46f;
    }
    else if ( std::strcmp( subsystem, "UI" ) == 0 )
    {
        r = 0.95f;
        g = 0.76f;
        b = 0.34f;
    }
    else
    {
        r = 0.62f;
        g = 0.76f;
        b = 0.82f;
    }
}

float MemoryEventLength( uint64_t bytes, float maxLength )
{
    if ( bytes == 0u )
    {
        return 14.0f;
    }

    const double kib = static_cast<double>( bytes ) / 1024.0;
    const double scaled = std::sqrt( (std::max)( 1.0, kib ) );
    return std::clamp( static_cast<float>( 10.0 + scaled * 2.8 ), 16.0f, maxLength );
}

const SkullbonezCore::UI::MemoryTab::MemoryOverlayPinnedEvent*
NewestPinnedEvent( const SkullbonezCore::UI::MemoryTab::UIMemoryOverlayState& state )
{
    const SkullbonezCore::UI::MemoryTab::MemoryOverlayPinnedEvent* newest = nullptr;

    for ( int i = 0; i < state.pinnedEventCount; ++i )
    {
        const SkullbonezCore::UI::MemoryTab::MemoryOverlayPinnedEvent& pinned = state.pinnedEvents[i];

        if ( !pinned.isFilled )
        {
            continue;
        }

        if ( !newest || pinned.event.sequence > newest->event.sequence )
        {
            newest = &pinned;
        }
    }

    return newest;
}

void DrawMemoryStackSegment( const SkullbonezCore::UI::UIDrawContext& draw, float& x, float y, float h, float stackW,
                             float stackEndX, uint64_t bytes, uint64_t totalBytes, float r, float g, float b )
{
    if ( bytes == 0u || totalBytes == 0u || x >= stackEndX )
    {
        return;
    }

    const float remainingW = stackEndX - x;
    const float w = std::clamp( static_cast<float>( static_cast<double>( bytes ) / static_cast<double>( totalBytes ) ) *
                                    stackW,
                                1.0f, remainingW );

    draw.Rect( x, y, w, h, r, g, b, 0.82f );
    x += w;
}

void DrawOverlaySubsystemStack( const SkullbonezCore::UI::UIDrawContext& draw,
                                const SkullbonezCore::Core::MainMemoryStats& memory,
                                const SkullbonezCore::UI::UIRect& stackRect )
{
    const uint64_t totalBytes = (std::max)( CurrentTotalMemoryBytes( memory ), 1ull );
    float x = stackRect.x;
    const float stackEndX = stackRect.x + stackRect.w;
    DrawMemoryStackSegment( draw, x, stackRect.y, stackRect.h, stackRect.w, stackEndX, memory.gameObjects.totalBytes,
                            totalBytes, 0.70f, 0.90f, 0.54f );

    DrawMemoryStackSegment( draw, x, stackRect.y, stackRect.h, stackRect.w, stackEndX, memory.replay.totalBytes, totalBytes,
                            0.42f, 0.86f, 0.94f );

    DrawMemoryStackSegment( draw, x, stackRect.y, stackRect.h, stackRect.w, stackEndX, memory.otherTrackedBytes, totalBytes,
                            0.95f, 0.76f, 0.34f );

    DrawMemoryStackSegment( draw, x, stackRect.y, stackRect.h, stackRect.w, stackEndX, memory.unattributedProcessBytes,
                            totalBytes, 0.62f, 0.70f, 0.78f );

    if ( x < stackRect.x + stackRect.w )
    {
        draw.Rect( x, stackRect.y, stackRect.x + stackRect.w - x, stackRect.h, 0.12f, 0.18f, 0.20f, 0.72f );
    }
}

void DrawMemoryRow( const SkullbonezCore::UI::UIDrawContext& draw, float x, float y, float labelW, const char* label,
                    uint64_t bytes, float r, float g, float b )
{
    char value[32] = {};
    FormatMemoryMiB( bytes, value, sizeof( value ) );
    draw.Text( x, y, 9.6f, 0.68f, 0.78f, 0.82f, label );
    draw.Text( x + labelW, y, 9.6f, r, g, b, value );
}

uint64_t ReplayTrajectoryLaneCounter( const uint64_t* counters, SkullbonezCore::Core::MainMemoryReplayTrajectoryLane lane )
{
    const std::size_t laneIndex = static_cast<std::size_t>( lane );
    return laneIndex < SkullbonezCore::Core::MAIN_MEMORY_REPLAY_TRAJECTORY_LANE_COUNT ? counters[laneIndex] : 0;
}

uint64_t ReplayMemoryCategoryCounter( const SkullbonezCore::Core::MainMemoryReplayStats& replay,
                                      SkullbonezCore::Core::MainMemoryReplayByteCategory category )
{
    return SkullbonezCore::Core::MainMemoryReplayCategoryByte( replay.categoryBytes, category );
}

// Concept: the memory tab prints emitted/dropped trajectory pairs compactly so
// a manual flicker repro can watch which lane starts dropping segments.
void FormatReplayTrajectoryPair( char* out, std::size_t outSize, const char* label,
                                 const SkullbonezCore::Core::MainMemoryReplayTrajectoryStats& trajectory,
                                 SkullbonezCore::Core::MainMemoryReplayTrajectoryLane lane )
{
    snprintf( out, outSize, "%s %llu/%llu", label,
              static_cast<unsigned long long>( ReplayTrajectoryLaneCounter( trajectory.emittedSegments, lane ) ),
              static_cast<unsigned long long>( ReplayTrajectoryLaneCounter( trajectory.droppedSegments, lane ) ) );
}

void DrawReplayPresetButton( const SkullbonezCore::UI::UIDrawContext& draw, const SkullbonezCore::UI::UIButton& button,
                             const char* label, bool active, bool hovered )
{
    const SkullbonezCore::UI::UIRect bounds = button.Bounds();
    const SkullbonezCore::UI::Style::UIPalette& palette = SkullbonezCore::UI::Style::Palette();
    const float fillA = active ? 0.78f : ( hovered ? 0.48f : 0.30f );
    const float r = active ? palette.accentStrong.r : palette.window.r;
    const float g = active ? palette.accentStrong.g : palette.window.g;
    const float b = active ? palette.accentStrong.b : palette.window.b;
    draw.RoundedRect( bounds.x, bounds.y, bounds.w, bounds.h, SkullbonezCore::UI::Style::Radii().control, r, g, b, fillA );

    draw.Outline( bounds.x, bounds.y, bounds.w, bounds.h, active ? palette.accentStrong.r : palette.innerBorder.r,
                  active ? palette.accentStrong.g : palette.innerBorder.g,
                  active ? palette.accentStrong.b : palette.innerBorder.b, active ? 0.78f : 0.54f );

    draw.Text( bounds.x + 9.0f, bounds.y + 6.0f, 8.8f, active ? 0.02f : palette.textSecondary.r,
               active ? 0.04f : palette.textSecondary.g, active ? 0.05f : palette.textSecondary.b, label );
}

void DrawReplayMemoryPolicyPanel( const SkullbonezCore::UI::UIDrawContext& draw,
                                  SkullbonezCore::UI::MemoryTab::UIMemoryOverlayState& state,
                                  const SkullbonezCore::UI::InGameUIFrameData& data, float contentX, float contentY,
                                  float contentW, float contentH, float panelY, int activeSlider, int mouseX, int mouseY )
{
    if ( !IsMemoryRowVisible( contentY, contentH, panelY, MEMORY_REPLAY_POLICY_BLOCK_H ) )
    {
        return;
    }

    SetReplayPolicyControlBounds( state, contentX, panelY, contentW );
    RefreshReplayPolicySnapshot( state, data );

    const SkullbonezCore::UI::Style::UIPalette& palette = SkullbonezCore::UI::Style::Palette();
    draw.Rect( contentX, panelY, contentW, MEMORY_REPLAY_POLICY_BLOCK_H, 0.018f, 0.030f, 0.038f, 0.58f );
    draw.Outline( contentX, panelY, contentW, MEMORY_REPLAY_POLICY_BLOCK_H, 0.18f, 0.30f, 0.34f, 0.62f );
    draw.Rect( contentX, panelY + 27.0f, contentW, 1.0f, 0.26f, 0.44f, 0.50f, 0.45f );
    draw.Text( contentX + 14.0f, panelY + 9.0f, 10.4f, palette.textSecondary.r, palette.textSecondary.g,
               palette.textSecondary.b, "Replay Policy" );

    char text[160] = {};
    snprintf( text, sizeof( text ), "visual %ds  solver %ds%s", state.lastPresentationRetentionSeconds,
              state.lastSolverRetentionSeconds, state.lastSolverWindowReduced ? "  solver trimmed" : "" );

    draw.Text( contentX + contentW - 226.0f, panelY + 10.0f, 8.4f, 0.54f, 0.66f, 0.70f, text );

    for ( int i = 0; i < SkullbonezCore::UI::MemoryTab::MEMORY_REPLAY_PRESET_COUNT; ++i )
    {
        DrawReplayPresetButton( draw, state.replayPresetButtons[i], ReplayMemoryPresetLabel( i ),
                                state.lastReplayPreset == i, state.replayPresetButtons[i].HitTest( mouseX, mouseY ) );
    }

    const int displayRetention = ReplayMemoryDisplayRetention( state );
    const int displayBudget = ReplayMemoryDisplayBudgetMiB( state );
    snprintf( text, sizeof( text ), "%ds", displayRetention );

    if ( IsMemoryRowVisible( contentY, contentH, panelY + 70.0f, 34.0f ) )
    {
        state.replayRetentionSlider.Draw( draw, "Retention", text, static_cast<float>( displayRetention ),
                                          static_cast<float>( MEMORY_REPLAY_RETENTION_MIN ),
                                          static_cast<float>( MEMORY_REPLAY_RETENTION_MAX ) );
    }

    snprintf( text, sizeof( text ), "%d MiB", displayBudget );

    if ( IsMemoryRowVisible( contentY, contentH, panelY + 108.0f, 34.0f ) )
    {
        state.replayBudgetSlider.Draw( draw, "Budget", text, static_cast<float>( displayBudget ),
                                       static_cast<float>( MEMORY_REPLAY_BUDGET_MIN_MIB ),
                                       static_cast<float>( MEMORY_REPLAY_BUDGET_MAX_MIB ) );
    }

    if ( state.lastBudgetClamped && activeSlider == 0 )
    {
        draw.Text( contentX + 14.0f, panelY + 136.0f, 8.0f, 0.90f, 0.66f, 0.34f, "budget clamp active" );
    }
}

void DrawMainMemoryPanel( const SkullbonezCore::UI::UIDrawContext& draw, const SkullbonezCore::UI::InGameUIFrameData& data,
                          float contentX, float contentY, float contentW, float contentH, float scrolledY )
{
    const SkullbonezCore::Core::MainMemoryStats& memory = data.mainMemory;
    const float panelX = contentX;
    const float panelY = scrolledY;
    const float panelW = contentW;
    const float panelH = MEMORY_SUMMARY_BLOCK_H;

    if ( !IsMemoryRowVisible( contentY, contentH, panelY, panelH ) )
    {
        return;
    }

    const float labelW = (std::min)( 118.0f, panelW * 0.34f );
    const float x = panelX + 14.0f;
    const float subX = panelX + (std::max)( 214.0f, panelW * 0.52f );
    char text[160] = {};

    char a[32] = {};

    char b[32] = {};

    char c[32] = {};

    char d[32] = {};

    const SkullbonezCore::UI::Style::UIPalette& palette = SkullbonezCore::UI::Style::Palette();

    draw.Rect( panelX, panelY, panelW, panelH, 0.018f, 0.030f, 0.038f, 0.58f );
    draw.Outline( panelX, panelY, panelW, panelH, 0.18f, 0.30f, 0.34f, 0.62f );
    draw.Rect( panelX, panelY + 27.0f, panelW, 1.0f, 0.26f, 0.44f, 0.50f, 0.45f );
    draw.Text( x, panelY + 9.0f, 10.4f, palette.textSecondary.r, palette.textSecondary.g, palette.textSecondary.b,
               "Main Memory" );

    snprintf( text, sizeof( text ), "%s", memory.process.taskManagerMetricName );
    draw.Text( panelX + panelW - 118.0f, panelY + 9.0f, 9.2f, 0.54f, 0.66f, 0.70f, text );

    const float row0 = panelY + 36.0f;

    if ( memory.process.available )
    {
        DrawMemoryRow( draw, x, row0, labelW, "TaskMgr", memory.process.taskManagerBytes, 0.90f, 0.96f, 0.98f );
    }
    else
    {
        draw.Text( x, row0, 9.6f, 0.68f, 0.78f, 0.82f, "TaskMgr" );
        draw.Text( x + labelW, row0, 9.6f, 0.90f, 0.52f, 0.38f, "n/a" );
    }

    DrawMemoryRow( draw, x, row0 + 18.0f, labelW, "Replay", memory.replay.totalBytes, 0.42f, 0.86f, 0.94f );
    FormatMemoryMiB( memory.replay.presentationBytes, a, sizeof( a ) );
    FormatMemoryMiB( memory.replay.solverBytes, b, sizeof( b ) );
    FormatMemoryMiB( memory.replay.predictionBytes, c, sizeof( c ) );
    snprintf( text, sizeof( text ), "P %s  S %s  Pred %s", a, b, c );
    draw.Text( subX, row0 + 18.0f, 8.4f, 0.48f, 0.60f, 0.64f, text );

    FormatMemoryMiB( memory.replay.pathAndCauseBytes, a, sizeof( a ) );
    FormatMemoryMiB( memory.replay.renderScratchBytes, b, sizeof( b ) );
    FormatMemoryMiB( memory.replay.trajectory.storeBytes, c, sizeof( c ) );
    snprintf( text, sizeof( text ), "Path %s  Scratch %s  Store %s", a, b, c );
    draw.Text( subX, row0 + 32.0f, 8.1f, 0.44f, 0.56f, 0.60f, text );

    DrawMemoryRow( draw, x, row0 + 50.0f, labelW, "Objects", memory.gameObjects.totalBytes, 0.70f, 0.90f, 0.54f );
    const uint64_t gameObjectStoreBytes = memory.gameObjects.physicsStoreBytes + memory.gameObjects.colliderStoreBytes +
                                          memory.gameObjects.renderStoreBytes;

    FormatMemoryMiB( memory.gameObjects.modelVectorBytes, a, sizeof( a ) );
    FormatMemoryMiB( gameObjectStoreBytes, b, sizeof( b ) );
    FormatMemoryMiB( memory.gameObjects.physicsWorldBytes + memory.gameObjects.gameplayWorldBytes, c, sizeof( c ) );
    snprintf( text, sizeof( text ), "Models %s  Stores %s  Worlds %s", a, b, c );
    draw.Text( subX, row0 + 50.0f, 8.4f, 0.48f, 0.60f, 0.64f, text );

    DrawMemoryRow( draw, x, row0 + 68.0f, labelW, "Unattrib", memory.unattributedProcessBytes, 0.82f, 0.74f, 0.55f );
    FormatMemoryMiB( memory.trackedEngineBytes, a, sizeof( a ) );
    FormatMemoryMiB( memory.reconciledTotalBytes, b, sizeof( b ) );
    snprintf( text, sizeof( text ), "Tracked %s  Sum %s", a, b );
    draw.Text( subX, row0 + 68.0f, 8.4f, 0.48f, 0.60f, 0.64f, text );

    if ( memory.trackedOvershootBytes > 0 )
    {
        FormatMemoryMiB( memory.trackedOvershootBytes, a, sizeof( a ) );
        snprintf( text, sizeof( text ), "Tracked exceeds process by %s", a );
        draw.Text( x, row0 + 92.0f, 9.2f, 0.95f, 0.58f, 0.38f, text );
    }
    else
    {
        const bool hasForeignFrees = memory.foreignFreeCount > 0u;

        if ( hasForeignFrees )
        {
            snprintf( text, sizeof( text ), "FOREIGN FREES %llu  models %llu/%llu  replay %llu/%llu samples",
                      static_cast<unsigned long long>( memory.foreignFreeCount ),
                      static_cast<unsigned long long>( memory.gameObjects.modelCount ),
                      static_cast<unsigned long long>( memory.gameObjects.modelCapacity ),
                      static_cast<unsigned long long>( memory.replay.presentationSamples ),
                      static_cast<unsigned long long>( memory.replay.solverSamples ) );
        }
        else
        {
            snprintf( text, sizeof( text ), "models %llu/%llu  replay %llu/%llu samples",
                      static_cast<unsigned long long>( memory.gameObjects.modelCount ),
                      static_cast<unsigned long long>( memory.gameObjects.modelCapacity ),
                      static_cast<unsigned long long>( memory.replay.presentationSamples ),
                      static_cast<unsigned long long>( memory.replay.solverSamples ) );
        }

        draw.Text( x, row0 + 92.0f, 8.8f, hasForeignFrees ? 0.95f : 0.48f, hasForeignFrees ? 0.48f : 0.60f,
                   hasForeignFrees ? 0.34f : 0.64f, text );
    }

    char pastPair[36] = {};
    char futurePair[36] = {};

    char childInPair[40] = {};

    char childOutPair[40] = {};

    FormatReplayTrajectoryPair( pastPair, sizeof( pastPair ), "past", memory.replay.trajectory,
                                SkullbonezCore::Core::MainMemoryReplayTrajectoryLane::PastRoot );

    FormatReplayTrajectoryPair( futurePair, sizeof( futurePair ), "future", memory.replay.trajectory,
                                SkullbonezCore::Core::MainMemoryReplayTrajectoryLane::FutureRoot );

    FormatReplayTrajectoryPair( childInPair, sizeof( childInPair ), "child-in", memory.replay.trajectory,
                                SkullbonezCore::Core::MainMemoryReplayTrajectoryLane::FutureChildIncoming );

    FormatReplayTrajectoryPair( childOutPair, sizeof( childOutPair ), "child-out", memory.replay.trajectory,
                                SkullbonezCore::Core::MainMemoryReplayTrajectoryLane::FutureChildOutgoing );

    snprintf( text, sizeof( text ), "traj seg e/d  %s  %s  %s  %s", pastPair, futurePair, childInPair, childOutPair );
    draw.Text( x, row0 + 112.0f, 8.0f, 0.48f, 0.66f, 0.68f, text );

    char retainedPair[40] = {};
    char baselinePair[40] = {};

    char markerPair[40] = {};

    char auxiliaryPair[40] = {};

    FormatReplayTrajectoryPair( retainedPair, sizeof( retainedPair ), "retained", memory.replay.trajectory,
                                SkullbonezCore::Core::MainMemoryReplayTrajectoryLane::RetainedTrail );

    FormatReplayTrajectoryPair( baselinePair, sizeof( baselinePair ), "base", memory.replay.trajectory,
                                SkullbonezCore::Core::MainMemoryReplayTrajectoryLane::BaselineRoot );

    FormatReplayTrajectoryPair( markerPair, sizeof( markerPair ), "mark", memory.replay.trajectory,
                                SkullbonezCore::Core::MainMemoryReplayTrajectoryLane::CausalMarker );

    FormatReplayTrajectoryPair( auxiliaryPair, sizeof( auxiliaryPair ), "aux", memory.replay.trajectory,
                                SkullbonezCore::Core::MainMemoryReplayTrajectoryLane::AuxiliaryTrail );

    snprintf( text, sizeof( text ), "%s  %s  %s  %s", retainedPair, baselinePair, markerPair, auxiliaryPair );
    draw.Text( x, row0 + 126.0f, 8.0f, 0.48f, 0.66f, 0.68f, text );

    snprintf( text, sizeof( text ), "traj store rec %llu  pts %llu/%llu  ver max %u churn %llu",
              static_cast<unsigned long long>( memory.replay.trajectory.recordCount ),
              static_cast<unsigned long long>( memory.replay.trajectory.publishedPointCount ),
              static_cast<unsigned long long>( memory.replay.trajectory.pointCount ),
              memory.replay.trajectory.maxRecordVersion,
              static_cast<unsigned long long>( memory.replay.trajectory.versionChurn ) );

    draw.Text( x, row0 + 140.0f, 8.0f, 0.48f, 0.66f, 0.68f, text );

    snprintf( text, sizeof( text ), "budget begin %llu step %llu tree %llu retained %llu rebuild d/a %llu/%llu",
              static_cast<unsigned long long>( memory.replay.trajectory.budgetExpiries[static_cast<std::size_t>( SkullbonezCore::Core::MainMemoryReplayBudgetPass::PredictionBegin )] ),
              static_cast<unsigned long long>( memory.replay.trajectory.budgetExpiries[static_cast<std::size_t>( SkullbonezCore::Core::MainMemoryReplayBudgetPass::PredictionStep )] ),
              static_cast<unsigned long long>( memory.replay.trajectory.budgetExpiries[static_cast<std::size_t>( SkullbonezCore::Core::MainMemoryReplayBudgetPass::PredictionBuildTree )] ),
              static_cast<unsigned long long>( memory.replay.trajectory.budgetExpiries[static_cast<std::size_t>( SkullbonezCore::Core::MainMemoryReplayBudgetPass::RetainedRefresh )] ),
              static_cast<unsigned long long>( memory.replay.trajectory.rebuildCauses[static_cast<std::size_t>( SkullbonezCore::Core::MainMemoryReplayRebuildCause::Dirty )] ),
              static_cast<unsigned long long>( memory.replay.trajectory.rebuildCauses[static_cast<std::size_t>( SkullbonezCore::Core::MainMemoryReplayRebuildCause::AutomaticRefresh )] ) );

    draw.Text( x, row0 + 154.0f, 8.0f, 0.48f, 0.66f, 0.68f, text );

    // Why: the category rows make memory-model tradeoffs visible during manual
    // replay repros without opening the full JSON dump.
    FormatMemoryMiB( ReplayMemoryCategoryCounter( memory.replay,
                                                  SkullbonezCore::Core::MainMemoryReplayByteCategory::PresentationBodies ),
                     a, sizeof( a ) );

    FormatMemoryMiB( ReplayMemoryCategoryCounter( memory.replay,
                                                  SkullbonezCore::Core::MainMemoryReplayByteCategory::SolverBodies ),
                     b, sizeof( b ) );

    FormatMemoryMiB( ReplayMemoryCategoryCounter( memory.replay, SkullbonezCore::Core::MainMemoryReplayByteCategory::
                                                                     PredictionFrameBodies ),
                     c, sizeof( c ) );

    FormatMemoryMiB( ReplayMemoryCategoryCounter( memory.replay,
                                                  SkullbonezCore::Core::MainMemoryReplayByteCategory::LoadedBodies ),
                     d, sizeof( d ) );

    snprintf( text, sizeof( text ), "cat bodies p %s  s %s  pred %s  load %s", a, b, c, d );
    draw.Text( x, row0 + 168.0f, 8.0f, 0.50f, 0.64f, 0.66f, text );

    FormatMemoryMiB( ReplayMemoryCategoryCounter( memory.replay,
                                                  SkullbonezCore::Core::MainMemoryReplayByteCategory::SolverWorldState ),
                     a, sizeof( a ) );

    FormatMemoryMiB( ReplayMemoryCategoryCounter( memory.replay,
                                                  SkullbonezCore::Core::MainMemoryReplayByteCategory::PredictionWorldState ),
                     b, sizeof( b ) );

    FormatMemoryMiB( ReplayMemoryCategoryCounter( memory.replay,
                                                  SkullbonezCore::Core::MainMemoryReplayByteCategory::PredictionEngine ),
                     c, sizeof( c ) );

    FormatMemoryMiB( ReplayMemoryCategoryCounter( memory.replay,
                                                  SkullbonezCore::Core::MainMemoryReplayByteCategory::PredictionFutureTree ),
                     d, sizeof( d ) );

    snprintf( text, sizeof( text ), "cat state sw %s  pw %s  eng %s  tree %s", a, b, c, d );
    draw.Text( x, row0 + 182.0f, 8.0f, 0.50f, 0.64f, 0.66f, text );

    const uint64_t recordBytes = ReplayMemoryCategoryCounter( memory.replay,
                                                              SkullbonezCore::Core::MainMemoryReplayByteCategory::
                                                                  PresentationSampleRecords ) +
                                 ReplayMemoryCategoryCounter( memory.replay,
                                                              SkullbonezCore::Core::MainMemoryReplayByteCategory::
                                                                  SolverSampleRecords ) +
                                 ReplayMemoryCategoryCounter( memory.replay,
                                                              SkullbonezCore::Core::MainMemoryReplayByteCategory::
                                                                  LoadedSampleRecords ) +
                                 ReplayMemoryCategoryCounter( memory.replay,
                                                              SkullbonezCore::Core::MainMemoryReplayByteCategory::
                                                                  PredictionFrameRecords );

    const uint64_t checkpointBytes = ReplayMemoryCategoryCounter( memory.replay,
                                                                  SkullbonezCore::Core::MainMemoryReplayByteCategory::
                                                                      PresentationCheckpoints ) +
                                     ReplayMemoryCategoryCounter( memory.replay,
                                                                  SkullbonezCore::Core::MainMemoryReplayByteCategory::
                                                                      SolverCheckpoints );

    const uint64_t
        scratchBytes = ReplayMemoryCategoryCounter( memory.replay, SkullbonezCore::Core::MainMemoryReplayByteCategory::
                                                                       PresentationScratch ) +
                       ReplayMemoryCategoryCounter( memory.replay,
                                                    SkullbonezCore::Core::MainMemoryReplayByteCategory::SolverScratch );

    FormatMemoryMiB( recordBytes, a, sizeof( a ) );
    FormatMemoryMiB( checkpointBytes, b, sizeof( b ) );
    FormatMemoryMiB( scratchBytes, c, sizeof( c ) );
    FormatMemoryMiB( ReplayMemoryCategoryCounter( memory.replay,
                                                  SkullbonezCore::Core::MainMemoryReplayByteCategory::Events ),
                     d, sizeof( d ) );

    snprintf( text, sizeof( text ), "cat records %s  check %s  scratch %s  events %s", a, b, c, d );
    draw.Text( x, row0 + 196.0f, 8.0f, 0.50f, 0.64f, 0.66f, text );

    const uint64_t
        visualPathBytes = ReplayMemoryCategoryCounter( memory.replay,
                                                       SkullbonezCore::Core::MainMemoryReplayByteCategory::PathTargets ) +
                          ReplayMemoryCategoryCounter( memory.replay,
                                                       SkullbonezCore::Core::MainMemoryReplayByteCategory::PathFutureNodes );

    const uint64_t launcherVisualBytes = ReplayMemoryCategoryCounter( memory.replay,
                                                                      SkullbonezCore::Core::MainMemoryReplayByteCategory::
                                                                          SolverLauncherVisuals ) +
                                         ReplayMemoryCategoryCounter( memory.replay,
                                                                      SkullbonezCore::Core::MainMemoryReplayByteCategory::
                                                                          RenderLauncherBackup );

    FormatMemoryMiB( visualPathBytes, a, sizeof( a ) );
    FormatMemoryMiB( ReplayMemoryCategoryCounter( memory.replay,
                                                  SkullbonezCore::Core::MainMemoryReplayByteCategory::PathCauseRows ),
                     b, sizeof( b ) );

    FormatMemoryMiB( ReplayMemoryCategoryCounter( memory.replay,
                                                  SkullbonezCore::Core::MainMemoryReplayByteCategory::RenderGhostRequests ),
                     c, sizeof( c ) );

    FormatMemoryMiB( launcherVisualBytes, d, sizeof( d ) );
    snprintf( text, sizeof( text ), "cat visual path %s  cause %s  ghost %s  launch %s", a, b, c, d );
    draw.Text( x, row0 + 210.0f, 8.0f, 0.50f, 0.64f, 0.66f, text );

    // Concept: upload rows separate the fixed arena waterline from the caller
    // category that consumed it, so a texture-load spike is not mistaken for a
    // steady prediction-overlay regression.
    const SkullbonezCore::UI::UIRenderMemoryStats& render = data.renderMemory;
    FormatMemoryMiB( render.uploadUsedBytes, a, sizeof( a ) );
    FormatMemoryMiB( render.uploadPeakBytes, b, sizeof( b ) );
    FormatMemoryMiB( render.uploadCapacityBytes, c, sizeof( c ) );
    snprintf( text, sizeof( text ), "upload used %s  peak %s  cap %s  flush/drop %llu/%llu", a, b, c,
              static_cast<unsigned long long>( render.uploadFlushCount ),
              static_cast<unsigned long long>( render.uploadDropCount ) );

    draw.Text( x, row0 + 228.0f, 8.0f, 0.54f, 0.72f, 0.74f, text );

    FormatMemoryMiB( render.uploadCategoryPeakBytes[static_cast<std::size_t>( SkullbonezCore::UI::UIRenderUploadCategory::Constants )],
                     a, sizeof( a ) );

    FormatMemoryMiB( render.uploadCategoryPeakBytes[static_cast<std::size_t>( SkullbonezCore::UI::UIRenderUploadCategory::DynamicVertex )],
                     b, sizeof( b ) );

    FormatMemoryMiB( render.uploadCategoryPeakBytes[static_cast<std::size_t>( SkullbonezCore::UI::UIRenderUploadCategory::InstanceData )],
                     c, sizeof( c ) );

    snprintf( text, sizeof( text ), "upload peak const %s  dynamic %s  instance %s", a, b, c );
    draw.Text( x, row0 + 242.0f, 8.0f, 0.50f, 0.66f, 0.68f, text );

    FormatMemoryMiB( render.uploadCategoryPeakBytes[static_cast<std::size_t>( SkullbonezCore::UI::UIRenderUploadCategory::TextureRows )],
                     a, sizeof( a ) );

    FormatMemoryMiB( render.uploadCategoryPeakBytes[static_cast<std::size_t>( SkullbonezCore::UI::UIRenderUploadCategory::RetainedGeometry )],
                     b, sizeof( b ) );

    snprintf( text, sizeof( text ), "upload peak texture %s  debug/prediction %s", a, b );
    draw.Text( x, row0 + 256.0f, 8.0f, 0.50f, 0.66f, 0.68f, text );
}

float CapacityTableHeight( const SkullbonezCore::UI::InGameUIFrameData& data )
{
    const int rowCount = data.reserveCapacityRows ? std::clamp( data.reserveCapacityRowCount, 0,
                                                                SkullbonezCore::UI::UI_RUNTIME_RESERVE_CAPACITY_ROW_MAX )
                                                  : 0;

    return MEMORY_CAPACITY_HEADER_H + static_cast<float>( rowCount ) * MEMORY_CAPACITY_ROW_H;
}

void DrawReserveCapacityRows( const SkullbonezCore::UI::UIDrawContext& draw,
                              const SkullbonezCore::UI::InGameUIFrameData& data, float contentX, float contentY,
                              float contentW, float contentH, float tableY )
{
    const float tableX = contentX;
    const float tableW = contentW;
    const float headerY = tableY + 34.0f;
    const int rowCount = data.reserveCapacityRows ? std::clamp( data.reserveCapacityRowCount, 0,
                                                                SkullbonezCore::UI::UI_RUNTIME_RESERVE_CAPACITY_ROW_MAX )
                                                  : 0;

    const float tableH = CapacityTableHeight( data );

    if ( !IsMemoryRowVisible( contentY, contentH, tableY, tableH ) )
    {
        return;
    }

    const SkullbonezCore::UI::UIRuntimeReserveCapacityRow*
        sortedRows[SkullbonezCore::UI::UI_RUNTIME_RESERVE_CAPACITY_ROW_MAX] = {};

    for ( int index = 0; index < rowCount; ++index )
    {
        sortedRows[index] = &data.reserveCapacityRows[index];
    }

    std::sort( sortedRows, sortedRows + rowCount,
               []( const SkullbonezCore::UI::UIRuntimeReserveCapacityRow* left,
                   const SkullbonezCore::UI::UIRuntimeReserveCapacityRow* right )
               {
                   if ( left->residentBytes != right->residentBytes )
                   {
                       return left->residentBytes > right->residentBytes;
                   }

                   return std::strcmp( left->ownerName, right->ownerName ) < 0;
               } );

    const SkullbonezCore::UI::Style::UIPalette& palette = SkullbonezCore::UI::Style::Palette();
    char text[96] = {};
    uint64_t totalResidentBytes = 0u;

    for ( int index = 0; index < rowCount; ++index )
    {
        totalResidentBytes += sortedRows[index]->residentBytes;
    }

    char totalResident[32] = {};
    const float visibleTableTop = (std::max)( tableY, contentY );
    const float visibleTableBottom = (std::min)( tableY + tableH, contentY + contentH );
    FormatMemoryMiB( totalResidentBytes, totalResident, sizeof( totalResident ) );
    draw.Rect( tableX, visibleTableTop, tableW, visibleTableBottom - visibleTableTop, 0.018f, 0.030f, 0.038f, 0.58f );

    if ( tableY >= contentY && tableY + 28.0f <= contentY + contentH )
    {
        draw.Text( tableX + 14.0f, tableY + 9.0f, 10.4f, palette.textSecondary.r, palette.textSecondary.g,
                   palette.textSecondary.b, "Store Capacity" );

        snprintf( text, sizeof( text ), "%d owners  %s resident", rowCount, totalResident );
        draw.Text( tableX + tableW - 180.0f, tableY + 10.0f, 8.4f, 0.54f, 0.66f, 0.70f, text );
    }

    const float ownerX = tableX + 14.0f;
    const float subsystemX = tableX + tableW * 0.43f;
    const float elementX = tableX + tableW * 0.52f;
    const float capacityX = tableX + tableW * 0.60f;
    const float liveX = tableX + tableW * 0.68f;
    const float peakX = tableX + tableW * 0.75f;
    const float utilisationX = tableX + tableW * 0.82f;
    const float residentX = tableX + tableW - 74.0f;

    if ( headerY >= contentY && headerY + 21.0f <= contentY + contentH )
    {
        draw.Rect( tableX, headerY + 20.0f, tableW, 1.0f, 0.26f, 0.44f, 0.50f, 0.45f );
        draw.Text( ownerX, headerY, 8.8f, 0.68f, 0.78f, 0.82f, "Owner" );
        draw.Text( subsystemX, headerY, 8.8f, 0.68f, 0.78f, 0.82f, "System" );
        draw.Text( elementX, headerY, 8.8f, 0.68f, 0.78f, 0.82f, "Elem" );
        draw.Text( capacityX, headerY, 8.8f, 0.68f, 0.78f, 0.82f, "Cap" );
        draw.Text( liveX, headerY, 8.8f, 0.68f, 0.78f, 0.82f, "Live" );
        draw.Text( peakX, headerY, 8.8f, 0.68f, 0.78f, 0.82f, "Peak" );
        draw.Text( utilisationX, headerY, 8.8f, 0.68f, 0.78f, 0.82f, "Use" );
        draw.Text( residentX, headerY, 8.8f, 0.68f, 0.78f, 0.82f, "Resident" );
    }

    for ( int index = 0; index < rowCount; ++index )
    {
        const SkullbonezCore::UI::UIRuntimeReserveCapacityRow& row = *sortedRows[index];
        const float rowY = tableY + MEMORY_CAPACITY_HEADER_H + static_cast<float>( index ) * MEMORY_CAPACITY_ROW_H;

        if ( rowY < contentY || rowY + MEMORY_CAPACITY_ROW_H > contentY + contentH )
        {
            continue;
        }

        char owner[40] = {};
        char element[24] = {};
        char capacity[24] = {};
        char live[24] = {};
        char peak[24] = {};
        char utilisation[24] = {};
        char resident[32] = {};
        CopyShortLabel( row.ownerName, owner, sizeof( owner ) );
        snprintf( element, sizeof( element ), "%d B", row.elementSizeBytes );
        snprintf( capacity, sizeof( capacity ), "%d", row.currentCapacity );
        snprintf( live, sizeof( live ), "%d", row.liveCount );
        snprintf( peak, sizeof( peak ), "%d", row.sessionHighWater );
        const double peakUtilisation = row.currentCapacity > 0 ? static_cast<double>( row.sessionHighWater ) * 100.0 /
                                                                     static_cast<double>( row.currentCapacity )
                                                               : 0.0;

        snprintf( utilisation, sizeof( utilisation ), "%.1f%%", peakUtilisation );
        FormatAllocationBytes( row.residentBytes, resident, sizeof( resident ) );
        draw.Rect( tableX, rowY + MEMORY_CAPACITY_ROW_H - 1.0f, tableW, 1.0f, 0.16f, 0.26f, 0.30f, 0.30f );
        draw.Text( ownerX, rowY + 5.0f, 8.2f, 0.78f, 0.86f, 0.88f, owner );
        draw.Text( subsystemX, rowY + 5.0f, 8.2f, 0.58f, 0.68f, 0.72f, row.subsystemName );

        draw.Text( elementX, rowY + 5.0f, 8.2f, 0.58f, 0.68f, 0.72f, element );
        draw.Text( capacityX, rowY + 5.0f, 8.2f, 0.78f, 0.86f, 0.88f, capacity );
        draw.Text( liveX, rowY + 5.0f, 8.2f, 0.78f, 0.86f, 0.88f, live );
        draw.Text( peakX, rowY + 5.0f, 8.2f, 0.78f, 0.86f, 0.88f, peak );
        draw.Text( utilisationX, rowY + 5.0f, 8.2f, 0.42f, 0.86f, 0.94f, utilisation );
        draw.Text( residentX, rowY + 5.0f, 8.2f, 0.58f, 0.68f, 0.72f, resident );
    }
}

void DrawReserveGrowthEvents( const SkullbonezCore::UI::UIDrawContext& draw,
                              const SkullbonezCore::UI::InGameUIFrameData& data, float contentX, float contentY,
                              float contentW, float contentH, float tableY )
{
    const float tableX = contentX;
    const float tableW = contentW;
    const float headerY = tableY + 34.0f;
    const int eventCount = std::clamp( data.reserveGrowthEventCount, 0,
                                       SkullbonezCore::UI::UI_RUNTIME_RESERVE_GROWTH_EVENT_MAX );

    const float tableH = MEMORY_EVENT_HEADER_H + static_cast<float>( eventCount ) * MEMORY_EVENT_ROW_H;

    if ( !IsMemoryRowVisible( contentY, contentH, tableY, tableH ) )
    {
        return;
    }

    const SkullbonezCore::UI::Style::UIPalette& palette = SkullbonezCore::UI::Style::Palette();
    char text[160] = {};

    draw.Rect( tableX, tableY, tableW, tableH, 0.018f, 0.030f, 0.038f, 0.58f );
    draw.Outline( tableX, tableY, tableW, tableH, 0.18f, 0.30f, 0.34f, 0.62f );
    draw.Text( tableX + 14.0f, tableY + 9.0f, 10.4f, palette.textSecondary.r, palette.textSecondary.g,
               palette.textSecondary.b, "Reserve Growth" );

    snprintf( text, sizeof( text ), "total %llu  shown %d  dropped %llu",
              static_cast<unsigned long long>( data.reserveGrowthEventTotalCount ), eventCount,
              static_cast<unsigned long long>( data.reserveGrowthEventDroppedCount ) );

    draw.Text( tableX + tableW - 196.0f, tableY + 10.0f, 8.4f, 0.54f, 0.66f, 0.70f, text );

    const float frameX = tableX + 14.0f;
    const float targetX = tableX + 74.0f;
    const float allocX = tableX + tableW * 0.52f;
    const float capX = tableX + tableW * 0.66f;
    const float statusX = tableX + tableW - 88.0f;
    draw.Rect( tableX, headerY + 20.0f, tableW, 1.0f, 0.26f, 0.44f, 0.50f, 0.45f );
    draw.Text( frameX, headerY, 8.8f, 0.68f, 0.78f, 0.82f, "Frame" );
    draw.Text( targetX, headerY, 8.8f, 0.68f, 0.78f, 0.82f, "Struct" );
    draw.Text( allocX, headerY, 8.8f, 0.68f, 0.78f, 0.82f, "Alloc" );
    draw.Text( capX, headerY, 8.8f, 0.68f, 0.78f, 0.82f, "Capacity" );
    draw.Text( statusX, headerY, 8.8f, 0.68f, 0.78f, 0.82f, "Status" );

    for ( int i = 0; i < eventCount; ++i )
    {
        const SkullbonezCore::Core::Allocation::RuntimeReserveGrowthEventView& event = data.reserveGrowthEvents[i];
        const float rowY = tableY + MEMORY_EVENT_HEADER_H + static_cast<float>( i ) * MEMORY_EVENT_ROW_H;

        if ( !IsMemoryRowVisible( contentY, contentH, rowY, MEMORY_EVENT_ROW_H ) )
        {
            continue;
        }

        char frame[32] = {};
        char target[52] = {};

        char bytes[32] = {};

        char capacity[64] = {};

        snprintf( frame, sizeof( frame ), "%d", event.frameNumber );
        CopyShortLabel( event.targetName, target, sizeof( target ) );
        FormatAllocationBytes( event.bytes, bytes, sizeof( bytes ) );
        snprintf( capacity, sizeof( capacity ), "%d->%d", event.oldCapacity, event.grantedCapacity );

        const float statusR = event.granted ? 0.42f : 0.95f;
        const float statusG = event.granted ? 0.86f : 0.58f;
        const float statusB = event.granted ? 0.94f : 0.38f;
        draw.Rect( tableX, rowY + MEMORY_EVENT_ROW_H - 1.0f, tableW, 1.0f, 0.16f, 0.26f, 0.30f, 0.30f );
        draw.Text( frameX, rowY + 5.0f, 8.2f, 0.78f, 0.86f, 0.88f, frame );
        draw.Text( targetX, rowY + 5.0f, 8.2f, 0.78f, 0.86f, 0.88f, target );
        draw.Text( allocX, rowY + 5.0f, 8.2f, 0.78f, 0.86f, 0.88f, bytes );
        draw.Text( capX, rowY + 5.0f, 8.2f, 0.58f, 0.68f, 0.72f, capacity );
        draw.Text( statusX, rowY + 5.0f, 8.2f, statusR, statusG, statusB, event.granted ? "OK" : "DENY" );
    }
}
} // namespace

namespace SkullbonezCore
{
namespace UI
{
namespace MemoryTab
{

int ContentHeight()
{
    return static_cast<int>( MEMORY_REPLAY_POLICY_BLOCK_H + MEMORY_PANEL_GAP + MEMORY_SUMMARY_BLOCK_H + 18.0f + MEMORY_CAPACITY_HEADER_H +
                             static_cast<float>( UI_RUNTIME_RESERVE_CAPACITY_ROW_MAX ) * MEMORY_CAPACITY_ROW_H + MEMORY_CAPACITY_BOTTOM_PAD +
                             MEMORY_EVENT_HEADER_H + static_cast<float>( UI_RUNTIME_RESERVE_GROWTH_EVENT_MAX ) * MEMORY_EVENT_ROW_H +
                             MEMORY_EVENT_BOTTOM_PAD );
}

bool OverlayEnabled( const UIMemoryOverlayState& state )
{
    return state.overlayEnabled;
}

void SetOverlayEnabled( UIMemoryOverlayState& state, bool enabled )
{
    state.overlayEnabled = enabled;
}

void PushOverlayFrame( UIMemoryOverlayState& state, const InGameUIFrameData& data )
{
    if ( !state.overlayEnabled )
    {
        return;
    }

    const uint64_t totalBytes = CurrentTotalMemoryBytes( data.mainMemory );
    PushOverlaySample( state, data );
    RefreshOverlayEvents( state, data, totalBytes );
    RefreshOverlayAxis( state, data );
}

void DrawOverlay( UIMemoryOverlayState& state, const UIDrawContext& draw, const InGameUIFrameData& data, float preferredX,
                  float preferredY )
{
    if ( !state.overlayEnabled )
    {
        return;
    }

    const float screenW = static_cast<float>( (std::max)( 1, data.screenW ) );
    const float screenH = static_cast<float>( (std::max)( 1, data.screenH ) );
    const float panelX = std::clamp( preferredX, MEMORY_OVERLAY_MARGIN,
                                     (std::max)( MEMORY_OVERLAY_MARGIN,
                                                 screenW - MEMORY_OVERLAY_PANEL_W - MEMORY_OVERLAY_MARGIN ) );

    const float panelY = std::clamp( preferredY, MEMORY_OVERLAY_MARGIN,
                                     (std::max)( MEMORY_OVERLAY_MARGIN,
                                                 screenH - MEMORY_OVERLAY_PANEL_H - MEMORY_OVERLAY_MARGIN ) );

    const UIRect panel = { panelX, panelY, MEMORY_OVERLAY_PANEL_W, MEMORY_OVERLAY_PANEL_H };

    const UIRect plot = { panel.x + 52.0f, panel.y + 38.0f, panel.w - 52.0f - MEMORY_OVERLAY_EVENT_RAIL_W - 14.0f, 66.0f };

    const UIRect eventRail = { plot.x + plot.w + 9.0f, plot.y, MEMORY_OVERLAY_EVENT_RAIL_W, plot.h };

    const UIRect stack = { plot.x, plot.y + plot.h + 10.0f, plot.w + MEMORY_OVERLAY_EVENT_RAIL_W + 9.0f, 7.0f };

    const Style::UIPalette& palette = Style::Palette();
    Style::UIColor fill = palette.windowSubtle;
    fill.a = 0.92f;
    draw.RoundedRect( panel.x + 4.0f, panel.y + 5.0f, panel.w, panel.h, Style::Radii().window, 0.0f, 0.0f, 0.0f, 0.22f );

    draw.RoundedPanel( panel, Style::Radii().window, fill, palette.border );

    const SkullbonezCore::Core::MainMemoryStats& memory = data.mainMemory;
    const uint64_t totalBytes = CurrentTotalMemoryBytes( memory );
    char totalText[32] = {};

    char trackedText[32] = {};

    char axisText[32] = {};

    FormatMemoryMiB( totalBytes, totalText, sizeof( totalText ) );
    FormatMemoryMiB( memory.trackedEngineBytes, trackedText, sizeof( trackedText ) );

    draw.Text( panel.x + 10.0f, panel.y + 8.0f, 10.5f, palette.textPrimary.r, palette.textPrimary.g, palette.textPrimary.b,
               "Memory" );

    draw.Text( panel.x + 70.0f, panel.y + 9.0f, 9.0f, 0.54f, 0.66f, 0.70f, "F6 waterline" );
    draw.Text( panel.x + panel.w - 112.0f, panel.y + 8.0f, 10.0f, 0.82f, 0.96f, 0.92f, totalText );

    draw.Rect( plot.x, plot.y, plot.w, plot.h, palette.window.r, palette.window.g, palette.window.b, 0.58f );
    draw.Rect( plot.x, plot.y, plot.w, 1.0f, palette.lineSoft.r, palette.lineSoft.g, palette.lineSoft.b, 0.20f );
    draw.Rect( plot.x, plot.y + plot.h * 0.5f, plot.w, 1.0f, palette.lineSoft.r, palette.lineSoft.g, palette.lineSoft.b,
               0.12f );

    draw.Rect( plot.x, plot.y + plot.h, plot.w, 1.0f, palette.accent.r, palette.accent.g, palette.accent.b, 0.32f );

    FormatMemoryMiB( state.axisMaxBytes, axisText, sizeof( axisText ) );
    draw.Text( panel.x + 9.0f, plot.y - 1.0f, 8.2f, 0.52f, 0.64f, 0.68f, axisText );
    FormatMemoryMiB( state.axisMinBytes, axisText, sizeof( axisText ) );
    draw.Text( panel.x + 9.0f, plot.y + plot.h - 9.0f, 8.2f, 0.52f, 0.64f, 0.68f, axisText );

    const float totalY = MemoryOverlayYForBytes( state, plot, totalBytes );
    draw.Rect( plot.x, totalY, plot.w, plot.y + plot.h - totalY, 0.17f, 0.45f, 0.48f, 0.18f );
    const float step = plot.w / static_cast<float>( MEMORY_OVERLAY_SAMPLE_COUNT );
    float previousX = 0.0f;
    float previousY = 0.0f;
    bool previousValid = false;

    for ( int i = 0; i < state.sampleCount; ++i )
    {
        const int sampleIndex = ( state.sampleHead - state.sampleCount + i + MEMORY_OVERLAY_SAMPLE_COUNT ) %
                                MEMORY_OVERLAY_SAMPLE_COUNT;

        const MemoryOverlaySample& sample = state.samples[sampleIndex];

        if ( !sample.isFilled )
        {
            previousValid = false;
            continue;
        }

        const float x = plot.x + ( static_cast<float>( MEMORY_OVERLAY_SAMPLE_COUNT - state.sampleCount + i ) + 0.5f ) * step;

        const float y = MemoryOverlayYForBytes( state, plot, sample.totalBytes );

        if ( previousValid )
        {
            DrawMemoryOverlayLineSegment( draw, previousX, previousY, x, y, 2.0f, 0.42f, 0.88f, 0.84f, 0.88f );
        }

        draw.Rect( x - 1.0f, y - 1.0f, 2.0f, 2.0f, 0.42f, 0.88f, 0.84f, 0.82f );
        previousX = x;
        previousY = y;
        previousValid = true;
    }

    draw.RoundedRect( eventRail.x, eventRail.y, eventRail.w, eventRail.h, Style::Radii().control, 0.02f, 0.036f, 0.044f,
                      0.70f );

    draw.Outline( eventRail.x, eventRail.y, eventRail.w, eventRail.h, palette.innerBorder.r, palette.innerBorder.g,
                  palette.innerBorder.b, 0.52f );

    draw.Text( eventRail.x + 6.0f, eventRail.y + 5.0f, 8.2f, 0.58f, 0.70f, 0.74f, "Events" );
    int retainedCount = 0;

    for ( int i = 0; i < state.pinnedEventCount; ++i )
    {
        const MemoryOverlayPinnedEvent& pinned = state.pinnedEvents[i];

        if ( !pinned.isFilled )
        {
            continue;
        }

        ++retainedCount;
        float r = 0.0f;
        float g = 0.0f;
        float b = 0.0f;
        MemoryEventColor( pinned.event, r, g, b );
        const float eventY = MemoryOverlayYForBytes( state, eventRail, pinned.levelBytes );
        const float maxLength = eventRail.w - 16.0f;
        const float eventLength = MemoryEventLength( pinned.event.bytes, maxLength );
        const double ageSeconds = (std::max)( 0.0, data.now - pinned.firstSeenSeconds );
        const float alpha = pinned.event.granted
                                ? std::clamp( 0.92f - static_cast<float>( ageSeconds ) * 0.012f, 0.46f, 0.92f )
                                : 0.96f;

        const float eventX = eventRail.x + eventRail.w - 7.0f - eventLength;
        draw.Rect( eventX, eventY - 1.0f, eventLength, 2.0f, r, g, b, alpha );
        draw.Rect( eventX + eventLength - 2.0f, eventY - 3.0f, 2.0f, 6.0f, r, g, b, alpha );

        if ( !pinned.event.granted )
        {
            draw.Rect( eventRail.x + 4.0f, eventY - 3.0f, 4.0f, 6.0f, r, g, b, 0.96f );
        }
    }

    DrawOverlaySubsystemStack( draw, memory, stack );
    draw.Outline( stack.x, stack.y, stack.w, stack.h, palette.innerBorder.r, palette.innerBorder.g, palette.innerBorder.b,
                  0.42f );

    char text[160] = {};
    snprintf( text, sizeof( text ), "tracked %s   pins %d/%llu", trackedText, retainedCount,
              static_cast<unsigned long long>( data.reserveGrowthEventTotalCount ) );

    draw.Text( panel.x + 10.0f, panel.y + 124.0f, 8.6f, 0.52f, 0.64f, 0.68f, text );

    const MemoryOverlayPinnedEvent* newest = NewestPinnedEvent( state );

    if ( newest )
    {
        char target[34] = {};

        char bytes[32] = {};

        CopyShortLabel( newest->event.targetName, target, sizeof( target ) );
        FormatAllocationBytes( newest->event.bytes, bytes, sizeof( bytes ) );
        snprintf( text, sizeof( text ), "%s %s  %s", newest->event.granted ? "+" : "DENY", bytes, target );
        const float r = newest->event.granted ? 0.42f : 0.96f;
        const float g = newest->event.granted ? 0.86f : 0.32f;
        const float b = newest->event.granted ? 0.94f : 0.24f;
        draw.Text( panel.x + 10.0f, panel.y + 142.0f, 9.2f, r, g, b, text );
    }
    else
    {
        draw.Text( panel.x + 10.0f, panel.y + 142.0f, 9.2f, 0.46f, 0.58f, 0.62f, "no allocator growth events" );
    }

    if ( state.retainedOverflowEventCount > 0u )
    {
        snprintf( text, sizeof( text ), "+%llu coalesced",
                  static_cast<unsigned long long>( state.retainedOverflowEventCount ) );

        draw.Text( panel.x + panel.w - 92.0f, panel.y + 142.0f, 8.2f, 0.90f, 0.62f, 0.38f, text );
    }
}

void Draw( const UIDrawContext& draw, UIMemoryOverlayState& state, const InGameUIFrameData& data, float contentX,
           float contentY, float contentW, float contentH, float scrolledY, int activeSlider, int mouseX, int mouseY )
{
    DrawReplayMemoryPolicyPanel( draw, state, data, contentX, contentY, contentW, contentH, scrolledY, activeSlider, mouseX,
                                 mouseY );

    const float memoryPanelY = scrolledY + MEMORY_REPLAY_POLICY_BLOCK_H + MEMORY_PANEL_GAP;
    DrawMainMemoryPanel( draw, data, contentX, contentY, contentW, contentH, memoryPanelY );
    const float capacityTableY = memoryPanelY + MEMORY_SUMMARY_BLOCK_H + 18.0f;
    DrawReserveCapacityRows( draw, data, contentX, contentY, contentW, contentH, capacityTableY );
    const float growthTableY = capacityTableY + CapacityTableHeight( data ) + MEMORY_CAPACITY_BOTTOM_PAD;
    DrawReserveGrowthEvents( draw, data, contentX, contentY, contentW, contentH, growthTableY );
}

bool HandleContentClick( UIMemoryOverlayState& state, InGameUIInputResult& result, int& activeSlider, int mouseX, int mouseY,
                         float contentX, float scrolledY, float contentW )
{
    SetReplayPolicyControlBounds( state, contentX, scrolledY, contentW );

    for ( int i = 0; i < MEMORY_REPLAY_PRESET_COUNT; ++i )
    {
        if ( state.replayPresetButtons[i].HitTest( mouseX, mouseY ) )
        {
            state.previewRetentionSeconds = -1;
            state.previewBudgetMiB = -1;
            SetReplayMemoryPolicyCommand( result, i, -1, -1 );
            return false;
        }
    }

    if ( state.replayRetentionSlider.HitTest( mouseX, mouseY ) )
    {
        activeSlider = MEMORY_REPLAY_SLIDER_RETENTION;
        state.previewRetentionSeconds = static_cast<int>( state.replayRetentionSlider.ValueFromMouse( mouseX, static_cast<float>( MEMORY_REPLAY_RETENTION_MIN ),
                                                                                                      static_cast<float>( MEMORY_REPLAY_RETENTION_MAX ), 1.0f ) );

        return true;
    }

    if ( state.replayBudgetSlider.HitTest( mouseX, mouseY ) )
    {
        activeSlider = MEMORY_REPLAY_SLIDER_BUDGET;
        state.previewBudgetMiB = static_cast<int>( state.replayBudgetSlider.ValueFromMouse( mouseX, static_cast<float>( MEMORY_REPLAY_BUDGET_MIN_MIB ),
                                                                                            static_cast<float>( MEMORY_REPLAY_BUDGET_MAX_MIB ),
                                                                                            static_cast<float>( MEMORY_REPLAY_BUDGET_STEP_MIB ) ) );

        return true;
    }

    return false;
}

bool UpdateActiveSlider( UIMemoryOverlayState& state, int activeSlider, int mouseX, InGameUIInputResult& )
{
    if ( activeSlider == MEMORY_REPLAY_SLIDER_RETENTION )
    {
        state.previewRetentionSeconds = static_cast<int>( state.replayRetentionSlider.ValueFromMouse( mouseX, static_cast<float>( MEMORY_REPLAY_RETENTION_MIN ),
                                                                                                      static_cast<float>( MEMORY_REPLAY_RETENTION_MAX ), 1.0f ) );

        return true;
    }

    if ( activeSlider == MEMORY_REPLAY_SLIDER_BUDGET )
    {
        state.previewBudgetMiB = static_cast<int>( state.replayBudgetSlider.ValueFromMouse( mouseX, static_cast<float>( MEMORY_REPLAY_BUDGET_MIN_MIB ),
                                                                                            static_cast<float>( MEMORY_REPLAY_BUDGET_MAX_MIB ),
                                                                                            static_cast<float>( MEMORY_REPLAY_BUDGET_STEP_MIB ) ) );

        return true;
    }

    return false;
}

bool CommitActiveSlider( UIMemoryOverlayState& state, int activeSlider, InGameUIInputResult& result )
{
    if ( activeSlider == MEMORY_REPLAY_SLIDER_RETENTION && state.previewRetentionSeconds >= 0 )
    {
        SetReplayMemoryPolicyCommand( result, -1, state.previewRetentionSeconds, -1 );
        return true;
    }

    if ( activeSlider == MEMORY_REPLAY_SLIDER_BUDGET && state.previewBudgetMiB >= 0 )
    {
        SetReplayMemoryPolicyCommand( result, -1, -1, state.previewBudgetMiB );
        return true;
    }

    return false;
}

void ResetPreviewState( UIMemoryOverlayState& state )
{
    state.previewRetentionSeconds = -1;
    state.previewBudgetMiB = -1;
}

} // namespace MemoryTab
} // namespace UI
} // namespace SkullbonezCore
