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
} // namespace SkullbonezCore::Runtime
