/*
File: SkullbonezSource/UI/UIInputCaptureIntent.h
Purpose:
  Publishes detached input-capture and viewport-mapping facts from a UI surface.

Summary:
  Any operator UI may publish this immutable-by-contract value after composing
  its frame. Runtime/Input consumes it on the next input turn without borrowing
  the UI implementation or retaining a callback.

Invariants:
  - Capture flags describe only the completed UI frame that produced them.
  - Viewport geometry is meaningful only when gameViewportMappingActive is true.

Related:
  - SkullbonezSource/Runtime/Input/InputRouter.h
  - SkullbonezSource/Runtime/DevelopmentTools/ImGuiEditorOwner.h
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

    bool gameViewportMappingActive = false;
    float gameViewportMinX = 0.0f;
    float gameViewportMinY = 0.0f;
    float gameViewportWidth = 0.0f;
    float gameViewportHeight = 0.0f;
    float gameViewportDpiScale = 1.0f;
    int gameViewportSourceWidth = 0;
    int gameViewportSourceHeight = 0;
};
} // namespace SkullbonezCore::UI
