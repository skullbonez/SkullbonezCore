/*
File: SkullbonezSource/Runtime/Automation/RecordedCursorFrame.h
Purpose:
  Publishes the recorded pointer facts needed by late fake-cursor presentation.

Summary:
  Automation constructs one detached value from the same resolved client point
  sent through normal synthetic input. App and Runtime/UI may filter the copy

  for presentation, but no owner retains it beyond the current frame.

Invariants:
  - A semantic anchor cannot make pointerResolved true for a pointerless turn.
  - Unresolved coordinates remain zero and consumers must check pointerResolved.
  - The value carries no buttons, native-pointer state, owner references, or storage.
  - Presentation observations report only the filtered frame and bounded draw
    result; they cannot request or commit native pointer state.

Related:
  - SkullbonezSource/Runtime/Automation/InteractionAutomationInputDriver.h
  - SkullbonezSource/Runtime/UI/RecordedCursorPresentationPolicy.h
*/
#pragma once

#include <type_traits>

namespace SkullbonezCore::Runtime
{
struct RecordedCursorFrame
{
    int clientX = 0;
    int clientY = 0;
    bool publishedRealTurn = false;
    bool pointerResolved = false;
    bool recordedAppFocused = false;
};

static_assert( std::is_trivially_copyable_v<RecordedCursorFrame> );

struct RecordedCursorPresentationObservation
{
    int clientX = 0;
    int clientY = 0;
    int drawCommandCount = 0;
    int drawCommandCapacity = 0;
    bool visible = false;
    bool submitted = false;
};

constexpr RecordedCursorPresentationObservation ObserveRecordedCursorPresentation( const RecordedCursorFrame& frame,
                                                                                   int drawCommandCount,
                                                                                   int drawCommandCapacity,
                                                                                   bool submitted ) noexcept
{
    const bool visible = frame.publishedRealTurn && frame.pointerResolved && frame.recordedAppFocused && submitted &&
                         drawCommandCount > 0;
    return { visible ? frame.clientX : 0,
             visible ? frame.clientY : 0,
             drawCommandCount,
             drawCommandCapacity,
             visible,
             submitted };
}

static_assert( std::is_trivially_copyable_v<RecordedCursorPresentationObservation> );
} // namespace SkullbonezCore::Runtime
