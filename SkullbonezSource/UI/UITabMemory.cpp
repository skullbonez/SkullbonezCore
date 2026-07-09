/*
File: SkullbonezSource/UI/UITabMemory.cpp
Purpose:
  Draws the in-engine memory diagnostics tab.

Mental model:
  Runtime refreshes memory data for the Memory tab, while the F6 overlay renders
  tracked/cached counters and reserve-growth events without sampling process
  memory. This file formats those snapshots without owning any sampling policy.

Glossary:
  Allocation size: Bytes newly reserved by a successful growth request.
  Capacity span: Old, requested, and granted element capacities for the target.
  Memory waterline: Compact overlay that tracks known engine memory and pinned
    reserve-growth events without polling process memory.

Invariants:
  - Formatting uses stack buffers only; memory diagnostics must not allocate.
  - Event rows are newest-first because the most recent replay growth is usually
    the one the user is trying to understand.
  - The F6 overlay keeps retained event pins in fixed arrays so diagnostics do
    not allocate while visualizing allocator activity.

Related:
  - SkullbonezSource/UI/UITabMemory.h
  - SkullbonezSource/UI/UI.h
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

using namespace SkullbonezCore::Basics;

namespace
{
constexpr float MEMORY_SUMMARY_BLOCK_H = 176.0f;
constexpr float MEMORY_EVENT_HEADER_H = 62.0f;
constexpr float MEMORY_EVENT_ROW_H = 22.0f;
constexpr float MEMORY_EVENT_BOTTOM_PAD = 18.0f;
constexpr float MEMORY_OVERLAY_PANEL_W = 340.0f;
constexpr float MEMORY_OVERLAY_PANEL_H = 166.0f;
constexpr float MEMORY_OVERLAY_MARGIN = 16.0f;
constexpr float MEMORY_OVERLAY_EVENT_RAIL_W = 74.0f;
constexpr uint64_t MEMORY_BYTES_PER_MIB = 1024ull * 1024ull;

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

uint64_t CurrentTotalMemoryBytes( const MainMemoryStats& memory )
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
                              const SkullbonezCore::UI::UIRect& plot,
                              uint64_t bytes )
{
    const uint64_t axisSpan = state.axisMaxBytes > state.axisMinBytes ? state.axisMaxBytes - state.axisMinBytes : 1u;
    const double normalized =
        ( static_cast<double>( bytes ) - static_cast<double>( state.axisMinBytes ) ) / static_cast<double>( axisSpan );
    return plot.y + plot.h - std::clamp( static_cast<float>( normalized ), 0.0f, 1.0f ) * plot.h;
}

void DrawMemoryOverlayLineSegment( const SkullbonezCore::UI::UIDrawContext& draw,
                                   float x0,
                                   float y0,
                                   float x1,
                                   float y1,
                                   float thickness,
                                   float r,
                                   float g,
                                   float b,
                                   float a )
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
                         const SkullbonezCore::Runtime::Allocation::RuntimeReserveGrowthEventView& event,
                         uint64_t levelBytes,
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
                           const SkullbonezCore::UI::InGameUIFrameData& data,
                           uint64_t levelBytes )
{
    if ( data.reserveGrowthEventTotalCount < state.lastObservedEventTotal )
    {
        ClearOverlayPinnedEvents( state );
    }
    state.lastObservedEventTotal = data.reserveGrowthEventTotalCount;

    const int eventCount =
        std::clamp( data.reserveGrowthEventCount, 0, SkullbonezCore::UI::UI_RUNTIME_RESERVE_GROWTH_EVENT_MAX );
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
        const int sampleIndex =
            ( state.sampleHead - state.sampleCount + i + SkullbonezCore::UI::MemoryTab::MEMORY_OVERLAY_SAMPLE_COUNT ) %
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

void MemoryEventColor( const SkullbonezCore::Runtime::Allocation::RuntimeReserveGrowthEventView& event,
                       float& r,
                       float& g,
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

void DrawMemoryStackSegment( const SkullbonezCore::UI::UIDrawContext& draw,
                             float& x,
                             float y,
                             float h,
                             float stackW,
                             float stackEndX,
                             uint64_t bytes,
                             uint64_t totalBytes,
                             float r,
                             float g,
                             float b )
{
    if ( bytes == 0u || totalBytes == 0u || x >= stackEndX )
    {
        return;
    }
    const float remainingW = stackEndX - x;
    const float w =
        std::clamp( static_cast<float>( static_cast<double>( bytes ) / static_cast<double>( totalBytes ) ) * stackW,
                    1.0f,
                    remainingW );
    draw.Rect( x, y, w, h, r, g, b, 0.82f );
    x += w;
}

void DrawOverlaySubsystemStack( const SkullbonezCore::UI::UIDrawContext& draw,
                                const MainMemoryStats& memory,
                                const SkullbonezCore::UI::UIRect& stackRect )
{
    const uint64_t totalBytes = (std::max)( CurrentTotalMemoryBytes( memory ), 1ull );
    float x = stackRect.x;
    const float stackEndX = stackRect.x + stackRect.w;
    DrawMemoryStackSegment( draw,
                            x,
                            stackRect.y,
                            stackRect.h,
                            stackRect.w,
                            stackEndX,
                            memory.gameObjects.totalBytes,
                            totalBytes,
                            0.70f,
                            0.90f,
                            0.54f );
    DrawMemoryStackSegment( draw,
                            x,
                            stackRect.y,
                            stackRect.h,
                            stackRect.w,
                            stackEndX,
                            memory.replay.totalBytes,
                            totalBytes,
                            0.42f,
                            0.86f,
                            0.94f );
    DrawMemoryStackSegment( draw,
                            x,
                            stackRect.y,
                            stackRect.h,
                            stackRect.w,
                            stackEndX,
                            memory.otherTrackedBytes,
                            totalBytes,
                            0.95f,
                            0.76f,
                            0.34f );
    DrawMemoryStackSegment( draw,
                            x,
                            stackRect.y,
                            stackRect.h,
                            stackRect.w,
                            stackEndX,
                            memory.unattributedProcessBytes,
                            totalBytes,
                            0.62f,
                            0.70f,
                            0.78f );
    if ( x < stackRect.x + stackRect.w )
    {
        draw.Rect( x, stackRect.y, stackRect.x + stackRect.w - x, stackRect.h, 0.12f, 0.18f, 0.20f, 0.72f );
    }
}

void DrawMemoryRow( const SkullbonezCore::UI::UIDrawContext& draw,
                    float x,
                    float y,
                    float labelW,
                    const char* label,
                    uint64_t bytes,
                    float r,
                    float g,
                    float b )
{
    char value[32] = {};
    FormatMemoryMiB( bytes, value, sizeof( value ) );
    draw.Text( x, y, 9.6f, 0.68f, 0.78f, 0.82f, label );
    draw.Text( x + labelW, y, 9.6f, r, g, b, value );
}

uint64_t ReplayTrajectoryLaneCounter( const uint64_t* counters, MainMemoryReplayTrajectoryLane lane )
{
    const std::size_t laneIndex = static_cast<std::size_t>( lane );
    return laneIndex < MAIN_MEMORY_REPLAY_TRAJECTORY_LANE_COUNT ? counters[laneIndex] : 0;
}

// Concept: the memory tab prints emitted/dropped trajectory pairs compactly so
// a manual flicker repro can watch which lane starts dropping segments.
void FormatReplayTrajectoryPair( char* out,
                                 std::size_t outSize,
                                 const char* label,
                                 const MainMemoryReplayTrajectoryStats& trajectory,
                                 MainMemoryReplayTrajectoryLane lane )
{
    snprintf( out,
              outSize,
              "%s %llu/%llu",
              label,
              static_cast<unsigned long long>( ReplayTrajectoryLaneCounter( trajectory.emittedSegments, lane ) ),
              static_cast<unsigned long long>( ReplayTrajectoryLaneCounter( trajectory.droppedSegments, lane ) ) );
}

void DrawMainMemoryPanel( const SkullbonezCore::UI::UIDrawContext& draw,
                          const SkullbonezCore::UI::InGameUIFrameData& data,
                          float contentX,
                          float contentY,
                          float contentW,
                          float contentH,
                          float scrolledY )
{
    const MainMemoryStats& memory = data.mainMemory;
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
    const SkullbonezCore::UI::Style::UIPalette& palette = SkullbonezCore::UI::Style::Palette();

    draw.Rect( panelX, panelY, panelW, panelH, 0.018f, 0.030f, 0.038f, 0.58f );
    draw.Outline( panelX, panelY, panelW, panelH, 0.18f, 0.30f, 0.34f, 0.62f );
    draw.Rect( panelX, panelY + 27.0f, panelW, 1.0f, 0.26f, 0.44f, 0.50f, 0.45f );
    draw.Text( x,
               panelY + 9.0f,
               10.4f,
               palette.textSecondary.r,
               palette.textSecondary.g,
               palette.textSecondary.b,
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
    FormatMemoryMiB( memory.gameObjects.physicsWorldBytes, c, sizeof( c ) );
    snprintf( text, sizeof( text ), "Models %s  Stores %s  World %s", a, b, c );
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
        snprintf( text,
                  sizeof( text ),
                  "models %llu/%llu  replay %llu/%llu samples",
                  static_cast<unsigned long long>( memory.gameObjects.modelCount ),
                  static_cast<unsigned long long>( memory.gameObjects.modelCapacity ),
                  static_cast<unsigned long long>( memory.replay.presentationSamples ),
                  static_cast<unsigned long long>( memory.replay.solverSamples ) );
        draw.Text( x, row0 + 92.0f, 8.8f, 0.48f, 0.60f, 0.64f, text );
    }

    char pastPair[36] = {};
    char futurePair[36] = {};
    char childInPair[40] = {};
    char childOutPair[40] = {};
    FormatReplayTrajectoryPair( pastPair,
                                sizeof( pastPair ),
                                "past",
                                memory.replay.trajectory,
                                MainMemoryReplayTrajectoryLane::PastRoot );
    FormatReplayTrajectoryPair( futurePair,
                                sizeof( futurePair ),
                                "future",
                                memory.replay.trajectory,
                                MainMemoryReplayTrajectoryLane::FutureRoot );
    FormatReplayTrajectoryPair( childInPair,
                                sizeof( childInPair ),
                                "child-in",
                                memory.replay.trajectory,
                                MainMemoryReplayTrajectoryLane::FutureChildIncoming );
    FormatReplayTrajectoryPair( childOutPair,
                                sizeof( childOutPair ),
                                "child-out",
                                memory.replay.trajectory,
                                MainMemoryReplayTrajectoryLane::FutureChildOutgoing );
    snprintf( text, sizeof( text ), "traj seg e/d  %s  %s  %s  %s", pastPair, futurePair, childInPair, childOutPair );
    draw.Text( x, row0 + 112.0f, 8.0f, 0.48f, 0.66f, 0.68f, text );

    char retainedPair[40] = {};
    char baselinePair[40] = {};
    char markerPair[40] = {};
    char auxiliaryPair[40] = {};
    FormatReplayTrajectoryPair( retainedPair,
                                sizeof( retainedPair ),
                                "retained",
                                memory.replay.trajectory,
                                MainMemoryReplayTrajectoryLane::RetainedTrail );
    FormatReplayTrajectoryPair( baselinePair,
                                sizeof( baselinePair ),
                                "base",
                                memory.replay.trajectory,
                                MainMemoryReplayTrajectoryLane::BaselineRoot );
    FormatReplayTrajectoryPair( markerPair,
                                sizeof( markerPair ),
                                "mark",
                                memory.replay.trajectory,
                                MainMemoryReplayTrajectoryLane::CausalMarker );
    FormatReplayTrajectoryPair( auxiliaryPair,
                                sizeof( auxiliaryPair ),
                                "aux",
                                memory.replay.trajectory,
                                MainMemoryReplayTrajectoryLane::AuxiliaryTrail );
    snprintf( text, sizeof( text ), "%s  %s  %s  %s", retainedPair, baselinePair, markerPair, auxiliaryPair );
    draw.Text( x, row0 + 126.0f, 8.0f, 0.48f, 0.66f, 0.68f, text );

    const uint64_t predictionDrawExpiries =
        memory.replay.trajectory
            .budgetExpiries[static_cast<std::size_t>( MainMemoryReplayBudgetPass::PredictionDrawRoot )] +
        memory.replay.trajectory
            .budgetExpiries[static_cast<std::size_t>( MainMemoryReplayBudgetPass::PredictionDrawChildren )] +
        memory.replay.trajectory
            .budgetExpiries[static_cast<std::size_t>( MainMemoryReplayBudgetPass::PredictionDrawAffectedBodies )] +
        memory.replay.trajectory
            .budgetExpiries[static_cast<std::size_t>( MainMemoryReplayBudgetPass::PredictionDrawRagdolls )];
    const uint64_t retainedDrawExpiries =
        memory.replay.trajectory
            .budgetExpiries[static_cast<std::size_t>( MainMemoryReplayBudgetPass::RetainedDrawRoot )] +
        memory.replay.trajectory
            .budgetExpiries[static_cast<std::size_t>( MainMemoryReplayBudgetPass::RetainedDrawChildren )] +
        memory.replay.trajectory
            .budgetExpiries[static_cast<std::size_t>( MainMemoryReplayBudgetPass::RetainedDrawMarker )];
    snprintf(
        text,
        sizeof( text ),
        "budget begin %llu step %llu pdraw %llu rdraw %llu rebuild d/a %llu/%llu",
        static_cast<unsigned long long>( memory.replay.trajectory.budgetExpiries[static_cast<std::size_t>(
            MainMemoryReplayBudgetPass::PredictionBegin )] ),
        static_cast<unsigned long long>( memory.replay.trajectory.budgetExpiries[static_cast<std::size_t>(
            MainMemoryReplayBudgetPass::PredictionStep )] ),
        static_cast<unsigned long long>( predictionDrawExpiries ),
        static_cast<unsigned long long>( retainedDrawExpiries ),
        static_cast<unsigned long long>(
            memory.replay.trajectory.rebuildCauses[static_cast<std::size_t>( MainMemoryReplayRebuildCause::Dirty )] ),
        static_cast<unsigned long long>( memory.replay.trajectory.rebuildCauses[static_cast<std::size_t>(
            MainMemoryReplayRebuildCause::AutomaticRefresh )] ) );
    draw.Text( x, row0 + 140.0f, 8.0f, 0.48f, 0.66f, 0.68f, text );
}

void DrawReserveGrowthEvents( const SkullbonezCore::UI::UIDrawContext& draw,
                              const SkullbonezCore::UI::InGameUIFrameData& data,
                              float contentX,
                              float contentY,
                              float contentW,
                              float contentH,
                              float scrolledY )
{
    const float tableX = contentX;
    const float tableY = scrolledY + MEMORY_SUMMARY_BLOCK_H + 18.0f;
    const float tableW = contentW;
    const float headerY = tableY + 34.0f;
    const int eventCount =
        std::clamp( data.reserveGrowthEventCount, 0, SkullbonezCore::UI::UI_RUNTIME_RESERVE_GROWTH_EVENT_MAX );
    const float tableH = MEMORY_EVENT_HEADER_H + static_cast<float>( eventCount ) * MEMORY_EVENT_ROW_H;
    if ( !IsMemoryRowVisible( contentY, contentH, tableY, tableH ) )
    {
        return;
    }

    const SkullbonezCore::UI::Style::UIPalette& palette = SkullbonezCore::UI::Style::Palette();
    char text[160] = {};
    draw.Rect( tableX, tableY, tableW, tableH, 0.018f, 0.030f, 0.038f, 0.58f );
    draw.Outline( tableX, tableY, tableW, tableH, 0.18f, 0.30f, 0.34f, 0.62f );
    draw.Text( tableX + 14.0f,
               tableY + 9.0f,
               10.4f,
               palette.textSecondary.r,
               palette.textSecondary.g,
               palette.textSecondary.b,
               "Reserve Growth" );
    snprintf( text,
              sizeof( text ),
              "total %llu  shown %d  dropped %llu",
              static_cast<unsigned long long>( data.reserveGrowthEventTotalCount ),
              eventCount,
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
        const SkullbonezCore::Runtime::Allocation::RuntimeReserveGrowthEventView& event = data.reserveGrowthEvents[i];
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
    return static_cast<int>( MEMORY_SUMMARY_BLOCK_H + 18.0f + MEMORY_EVENT_HEADER_H +
                             static_cast<float>( UI_RUNTIME_RESERVE_GROWTH_EVENT_MAX ) * MEMORY_EVENT_ROW_H +
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

void DrawOverlay( UIMemoryOverlayState& state,
                  const UIDrawContext& draw,
                  const InGameUIFrameData& data,
                  float preferredX,
                  float preferredY )
{
    if ( !state.overlayEnabled )
    {
        return;
    }

    const float screenW = static_cast<float>( (std::max)( 1, data.screenW ) );
    const float screenH = static_cast<float>( (std::max)( 1, data.screenH ) );
    const float panelX =
        std::clamp( preferredX,
                    MEMORY_OVERLAY_MARGIN,
                    (std::max)( MEMORY_OVERLAY_MARGIN, screenW - MEMORY_OVERLAY_PANEL_W - MEMORY_OVERLAY_MARGIN ) );
    const float panelY =
        std::clamp( preferredY,
                    MEMORY_OVERLAY_MARGIN,
                    (std::max)( MEMORY_OVERLAY_MARGIN, screenH - MEMORY_OVERLAY_PANEL_H - MEMORY_OVERLAY_MARGIN ) );
    const UIRect panel = { panelX, panelY, MEMORY_OVERLAY_PANEL_W, MEMORY_OVERLAY_PANEL_H };
    const UIRect plot = { panel.x + 52.0f,
                          panel.y + 38.0f,
                          panel.w - 52.0f - MEMORY_OVERLAY_EVENT_RAIL_W - 14.0f,
                          66.0f };
    const UIRect eventRail = { plot.x + plot.w + 9.0f, plot.y, MEMORY_OVERLAY_EVENT_RAIL_W, plot.h };
    const UIRect stack = { plot.x, plot.y + plot.h + 10.0f, plot.w + MEMORY_OVERLAY_EVENT_RAIL_W + 9.0f, 7.0f };

    const Style::UIPalette& palette = Style::Palette();
    Style::UIColor fill = palette.windowSubtle;
    fill.a = 0.92f;
    draw.RoundedRect( panel.x + 4.0f,
                      panel.y + 5.0f,
                      panel.w,
                      panel.h,
                      Style::Radii().window,
                      0.0f,
                      0.0f,
                      0.0f,
                      0.22f );
    draw.RoundedPanel( panel, Style::Radii().window, fill, palette.border );

    const MainMemoryStats& memory = data.mainMemory;
    const uint64_t totalBytes = CurrentTotalMemoryBytes( memory );
    char totalText[32] = {};
    char trackedText[32] = {};
    char axisText[32] = {};
    FormatMemoryMiB( totalBytes, totalText, sizeof( totalText ) );
    FormatMemoryMiB( memory.trackedEngineBytes, trackedText, sizeof( trackedText ) );

    draw.Text( panel.x + 10.0f,
               panel.y + 8.0f,
               10.5f,
               palette.textPrimary.r,
               palette.textPrimary.g,
               palette.textPrimary.b,
               "Memory" );
    draw.Text( panel.x + 70.0f, panel.y + 9.0f, 9.0f, 0.54f, 0.66f, 0.70f, "F6 waterline" );
    draw.Text( panel.x + panel.w - 112.0f, panel.y + 8.0f, 10.0f, 0.82f, 0.96f, 0.92f, totalText );

    draw.Rect( plot.x, plot.y, plot.w, plot.h, palette.window.r, palette.window.g, palette.window.b, 0.58f );
    draw.Rect( plot.x, plot.y, plot.w, 1.0f, palette.lineSoft.r, palette.lineSoft.g, palette.lineSoft.b, 0.20f );
    draw.Rect( plot.x,
               plot.y + plot.h * 0.5f,
               plot.w,
               1.0f,
               palette.lineSoft.r,
               palette.lineSoft.g,
               palette.lineSoft.b,
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
        const int sampleIndex =
            ( state.sampleHead - state.sampleCount + i + MEMORY_OVERLAY_SAMPLE_COUNT ) % MEMORY_OVERLAY_SAMPLE_COUNT;
        const MemoryOverlaySample& sample = state.samples[sampleIndex];
        if ( !sample.isFilled )
        {
            previousValid = false;
            continue;
        }
        const float x =
            plot.x + ( static_cast<float>( MEMORY_OVERLAY_SAMPLE_COUNT - state.sampleCount + i ) + 0.5f ) * step;
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

    draw.RoundedRect( eventRail.x,
                      eventRail.y,
                      eventRail.w,
                      eventRail.h,
                      Style::Radii().control,
                      0.02f,
                      0.036f,
                      0.044f,
                      0.70f );
    draw.Outline( eventRail.x,
                  eventRail.y,
                  eventRail.w,
                  eventRail.h,
                  palette.innerBorder.r,
                  palette.innerBorder.g,
                  palette.innerBorder.b,
                  0.52f );
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
    draw.Outline( stack.x,
                  stack.y,
                  stack.w,
                  stack.h,
                  palette.innerBorder.r,
                  palette.innerBorder.g,
                  palette.innerBorder.b,
                  0.42f );

    char text[160] = {};
    snprintf( text,
              sizeof( text ),
              "tracked %s   pins %d/%llu",
              trackedText,
              retainedCount,
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
        snprintf( text,
                  sizeof( text ),
                  "+%llu coalesced",
                  static_cast<unsigned long long>( state.retainedOverflowEventCount ) );
        draw.Text( panel.x + panel.w - 92.0f, panel.y + 142.0f, 8.2f, 0.90f, 0.62f, 0.38f, text );
    }
}

void Draw( const UIDrawContext& draw,
           const InGameUIFrameData& data,
           float contentX,
           float contentY,
           float contentW,
           float contentH,
           float scrolledY )
{
    DrawMainMemoryPanel( draw, data, contentX, contentY, contentW, contentH, scrolledY );
    DrawReserveGrowthEvents( draw, data, contentX, contentY, contentW, contentH, scrolledY );
}

} // namespace MemoryTab
} // namespace UI
} // namespace SkullbonezCore
