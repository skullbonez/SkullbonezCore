#pragma once

#include "../SkullbonezCommon.h"

namespace SkullbonezCore
{
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

UIInputSnapshot CaptureSnapshot( bool previousLeftDown, bool hasMouseOverride, int overrideX, int overrideY );

void CaptureKeyStates( bool keyWasDown[256] );
bool ConsumeKeyPress( bool keyWasDown[256], int virtualKey );
bool IsVirtualKeyDown( int virtualKey );

void BeginMouseCapture( HWND hwnd );
void EndMouseCapture();

} // namespace InputControl
} // namespace UI
} // namespace SkullbonezCore
