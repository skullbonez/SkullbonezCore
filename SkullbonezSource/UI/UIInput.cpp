/*
File: SkullbonezSource/UI/UIInput.cpp
Purpose:
  Converts immutable runtime input snapshots into UI-local pointer and keyboard
  values without polling hardware or owning native capture.

Summary:
  UIInput.cpp implements UI Input widgets, layout, drawing, or UI state for
  the in-engine controls. As an implementation unit, keep edits anchored on UI
  request, layout, hit-test, and draw-command flow and on the
  glossary/invariants below.

Glossary:
  Draw command: Lightweight record describing a UI shape or text batch to
  render later in the frame.
  Hit box: Screen-space rectangle used to decide whether mouse input targets a
  widget.

Invariants:
  - Draw geometry and hit testing must be derived from the same layout
    constants.
  - Device levels and router-owned button edges are copied, never recomputed.

Related:
  - SkullbonezSource/UI/UIInput.h
  - Agentic/Reference/comment-style-guide.md
*/
#include "UIInput.h"
#include "../Runtime/Input.h"
#include "../Runtime/InputRouter.h"

namespace SkullbonezCore
{
namespace UI
{
namespace InputControl
{

UIInputSnapshot CaptureSnapshot( const Basics::DeviceInputFrame& frame,
                                 const Basics::RuntimeMouseEdges& mouse,
                                 bool hasMouseOverride,
                                 int overrideX,
                                 int overrideY )
{
    UIInputSnapshot snapshot;
    snapshot.wheelDelta = frame.wheelDelta;

    if ( hasMouseOverride )
    {
        snapshot.mouseX = overrideX;
        snapshot.mouseY = overrideY;
    }
    else
    {
        if ( frame.hasClientPosition )
        {
            snapshot.mouseX = frame.clientX;
            snapshot.mouseY = frame.clientY;
        }
    }

    snapshot.leftDown = mouse.leftDown;
    snapshot.leftPressed = mouse.leftPressed;
    snapshot.leftReleased = mouse.leftReleased;
    return snapshot;
}


void CaptureKeyStates( bool keyWasDown[256], const Basics::InputKeySnapshot& keys )
{
    if ( !keyWasDown )
    {
        return;
    }
    for ( int key = 0; key < 256; ++key )
    {
        keyWasDown[key] = IsVirtualKeyDown( keys, key );
    }
}


bool ConsumeKeyPress( bool keyWasDown[256], const Basics::InputKeySnapshot& keys, int virtualKey )
{
    if ( !keyWasDown || virtualKey < 0 || virtualKey >= 256 )
    {
        return false;
    }
    const bool isDown = IsVirtualKeyDown( keys, virtualKey );
    const bool wasPressed = isDown && !keyWasDown[virtualKey];
    keyWasDown[virtualKey] = isDown;
    return wasPressed;
}


bool IsVirtualKeyDown( const Basics::InputKeySnapshot& keys, int virtualKey )
{
    return keys.IsDown( virtualKey );
}


} // namespace InputControl
} // namespace UI
} // namespace SkullbonezCore
