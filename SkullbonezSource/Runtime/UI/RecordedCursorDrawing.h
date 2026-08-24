/*
File: SkullbonezSource/Runtime/UI/RecordedCursorDrawing.h
Purpose:
  Composes one filtered recorded-pointer value into bounded UI draw commands.

Summary:
  Runtime/UI owns the provisional two-tone arrow geometry and converts one
  frame-local Automation value into generic triangles. The caller submits that
  detached list at App's final UI seam; this code retains no state and reaches
  neither native pointer owners nor renderer resources.

Invariants:
  - A visible cursor emits exactly two triangles, outer first and inner second.
  - Both triangles keep their tip at the resolved recorded client point.
  - Each body axis flips or contracts independently to remain inside the viewport.
  - Composition emits no text and performs no texture, descriptor, or heap work.

Related:
  - SkullbonezSource/Runtime/Automation/RecordedCursorFrame.h
  - SkullbonezSource/Runtime/UI/RecordedCursorPresentationPolicy.h
  - SkullbonezSource/UI/UIDrawList.h
  - SkullbonezSource/Runtime/App/RunFrame.cpp
*/
#pragma once

#include "../Automation/RecordedCursorFrame.h"
#include "../../UI/UIDrawList.h"

namespace SkullbonezCore::Runtime
{
inline constexpr int RECORDED_CURSOR_DRAW_COMMAND_HIGH_WATER = 2;
static_assert( RECORDED_CURSOR_DRAW_COMMAND_HIGH_WATER <= UI::UIDrawList::MAX_COMMANDS );

namespace RecordedCursorDrawingDetail
{
struct AxisPlacement
{
    float sign = 1.0f;
    float scale = 1.0f;
};

constexpr AxisPlacement ResolveAxisPlacement( int point, int viewportExtent, int bodyExtent ) noexcept
{
    const int maximumCoordinate = viewportExtent - 1;
    const int positiveSpace = maximumCoordinate - point;
    const int negativeSpace = point;
    const bool positiveFits = positiveSpace >= bodyExtent;
    const bool negativeFits = negativeSpace >= bodyExtent;

    // Why: preserve the familiar down-right arrow whenever it fits. At a tight
    // edge choose the roomier side, then contract only pathological tiny views.
    const bool usePositive = positiveFits || ( !negativeFits && positiveSpace >= negativeSpace );
    const int availableSpace = usePositive ? positiveSpace : negativeSpace;
    const float scale = availableSpace >= bodyExtent
                            ? 1.0f
                            : static_cast<float>( availableSpace ) / static_cast<float>( bodyExtent );
    return { usePositive ? 1.0f : -1.0f, scale };
}
} // namespace RecordedCursorDrawingDetail

inline void ComposeRecordedCursorDrawList( UI::UIDrawList& drawList, const RecordedCursorFrame& frame, int viewportWidth,
                                           int viewportHeight ) noexcept
{
    drawList.Clear();

    const bool visible = frame.publishedRealTurn && frame.pointerResolved && frame.recordedAppFocused && viewportWidth > 0 &&
                         viewportHeight > 0 && frame.clientX >= 0 && frame.clientY >= 0 && frame.clientX < viewportWidth &&
                         frame.clientY < viewportHeight;

    if ( !visible )
    {
        return;
    }

    constexpr int outerWidth = 14;
    constexpr int outerHeight = 21;
    const auto horizontal = RecordedCursorDrawingDetail::ResolveAxisPlacement( frame.clientX, viewportWidth, outerWidth );
    const auto vertical = RecordedCursorDrawingDetail::ResolveAxisPlacement( frame.clientY, viewportHeight, outerHeight );
    const float tipX = static_cast<float>( frame.clientX );
    const float tipY = static_cast<float>( frame.clientY );
    const auto x = [tipX, horizontal]( float offset ) { return tipX + offset * horizontal.sign * horizontal.scale; };
    const auto y = [tipY, vertical]( float offset ) { return tipY + offset * vertical.sign * vertical.scale; };

    // Decision: these dimensions and colors are a reversible RIC2 presentation
    // choice. The stable contract is the shared hot tip and bounded 0/2 commands.
    drawList.AddTriangle( tipX, tipY, x( 4.0f ), y( 21.0f ), x( 14.0f ), y( 14.0f ), 0.03f, 0.04f, 0.05f, 1.0f );
    drawList.AddTriangle( tipX, tipY, x( 4.0f ), y( 16.0f ), x( 10.0f ), y( 11.0f ), 0.96f, 0.98f, 1.0f, 1.0f );
}
} // namespace SkullbonezCore::Runtime
