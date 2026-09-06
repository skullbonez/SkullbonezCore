/*
File: SkullbonezSource/UI/UIInputCaptureIntent.h
Purpose:
  Publishes detached input-capture facts from a UI surface.

Summary:
  Any operator UI may publish this immutable-by-contract value after composing
  its frame. Runtime/Input consumes it on the next input turn without borrowing
  the UI implementation or retaining a callback.

Invariants:
  - Capture flags describe only the completed UI frame that produced them.

Related:
  - SkullbonezSource/Runtime/Input/InputRouter.h
*/

#pragma once

namespace SkullbonezCore::UI
{
struct InputCaptureIntent
{
    bool mouse = false;
    bool keyboard = false;
    bool text = false;
    bool nativePointerStateTouched = false;
};
} // namespace SkullbonezCore::UI
