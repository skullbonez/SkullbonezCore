/*
File: SkullbonezSource/UI/UIInput.cpp
Purpose:
  Implements UI Input widgets, layout, drawing, or UI state for the in-engine controls.

Mental model:
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

Related:
  - SkullbonezSource/UI/UIInput.h
  - Agentic/Reference/comment-style-guide.md
*/
#include "UIInput.h"
#include "../Runtime/Input.h"

namespace SkullbonezCore
{
namespace UI
{
namespace InputControl
{

UIInputSnapshot CaptureSnapshot( bool previousLeftDown, bool hasMouseOverride, int overrideX, int overrideY )
{
    UIInputSnapshot snapshot;
    snapshot.wheelDelta = Hardware::Input::ConsumeMouseWheelDelta();

    if ( hasMouseOverride )
    {
        snapshot.mouseX = overrideX;
        snapshot.mouseY = overrideY;
    }
    else
    {
        const Hardware::Input::MouseCoordinatesResult mouse = Hardware::Input::GetClientMouseCoordinates();
        if ( mouse.result.ok )
        {
            snapshot.mouseX = static_cast<int>( mouse.coordinates.x );
            snapshot.mouseY = static_cast<int>( mouse.coordinates.y );
        }
    }

    snapshot.leftDown = Hardware::Input::IsLeftMouseDown();
    snapshot.leftPressed = snapshot.leftDown && !previousLeftDown;
    snapshot.leftReleased = !snapshot.leftDown && previousLeftDown;
    return snapshot;
}


void CaptureKeyStates( bool keyWasDown[256] )
{
    if ( !keyWasDown )
    {
        return;
    }
    for ( int key = 0; key < 256; ++key )
    {
        keyWasDown[key] = IsVirtualKeyDown( key );
    }
}


bool ConsumeKeyPress( bool keyWasDown[256], int virtualKey )
{
    if ( !keyWasDown || virtualKey < 0 || virtualKey >= 256 )
    {
        return false;
    }
    const bool isDown = IsVirtualKeyDown( virtualKey );
    const bool wasPressed = isDown && !keyWasDown[virtualKey];
    keyWasDown[virtualKey] = isDown;
    return wasPressed;
}


bool IsVirtualKeyDown( int virtualKey )
{
    return ( GetKeyState( virtualKey ) & 0x8000 ) != 0;
}


void BeginMouseCapture( HWND hwnd )
{
    SetCapture( hwnd );
}


void EndMouseCapture()
{
    ReleaseCapture();
}

} // namespace InputControl
} // namespace UI
} // namespace SkullbonezCore
