/*
File: SkullbonezSource/UI/UIInput.h
Purpose:
  Converts the runtime's immutable device/pointer snapshots into UI-local input
  values and applies scene-filter keyboard edges without polling hardware.

Summary:
  UIInput.h implements UI Input widgets, layout, drawing, or UI state for the
  in-engine controls. As a public header, keep edits anchored on UI request,
  layout, hit-test, and draw-command flow and on the glossary/invariants
  below.

Glossary:
  Draw command: Lightweight record describing a UI shape or text batch to
  render later in the frame.
  Hit box: Screen-space rectangle used to decide whether mouse input targets a
  widget.

Invariants:
  - Draw geometry and hit testing must be derived from the same layout
    constants.
  - UI input receives InputRouter-owned levels/edges and never samples Win32
    keyboard, pointer, wheel, or cursor state itself.

Related:
  - SkullbonezSource/UI/UIInput.cpp
  - Agentic/Reference/comment-style-guide.md
*/
#pragma once

#include "../Core/Common.h"

namespace SkullbonezCore
{
namespace Runtime
{
struct DeviceInputFrame;
class InputKeySnapshot;
struct RuntimeMouseEdges;
} // namespace Runtime

namespace UI
{
namespace InputControl
{

struct UIInputSnapshot
{
    int mouseX = 0;
    int mouseY = 0;
    int wheelDelta = 0;
    bool leftDown = false;
    bool leftPressed = false;
    bool leftReleased = false;
};

UIInputSnapshot CaptureSnapshot( const Runtime::DeviceInputFrame& frame,
                                 const Runtime::RuntimeMouseEdges& mouse,
                                 bool hasMouseOverride,
                                 int overrideX,
                                 int overrideY );

void CaptureKeyStates( bool keyWasDown[256], const Runtime::InputKeySnapshot& keys );
bool ConsumeKeyPress( bool keyWasDown[256], const Runtime::InputKeySnapshot& keys, int virtualKey );
bool IsVirtualKeyDown( const Runtime::InputKeySnapshot& keys, int virtualKey );

} // namespace InputControl
} // namespace UI
} // namespace SkullbonezCore
