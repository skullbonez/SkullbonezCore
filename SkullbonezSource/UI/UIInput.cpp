#include "UIInput.h"
#include "../SkullbonezInput.h"

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

    POINT mouse = Hardware::Input::GetClientMouseCoordinates();
    snapshot.mouseX = static_cast<int>( mouse.x );
    snapshot.mouseY = static_cast<int>( mouse.y );
    if ( hasMouseOverride )
    {
        snapshot.mouseX = overrideX;
        snapshot.mouseY = overrideY;
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
