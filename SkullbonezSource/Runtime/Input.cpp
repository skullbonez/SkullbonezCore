/*
File: SkullbonezSource/Runtime/Input.cpp
Purpose:
  Collects keyboard and mouse state for the run loop and UI.

Mental model:
  Win32 callbacks enqueue mouse-only edge data into process-local
  accumulators. The run loop and UI sample those queues once per frame and
  reset them while building camera/UI input state. Cursor policy and scripted
  automation overrides share this file, but they are not callback queues.

Glossary:
  Win32: Windows desktop API used for the app window, messages, and cursor
  state.
  WndProc: Win32 window callback that receives mouse wheel and raw mouse
  packets before the frame loop polls input.
  Accumulator: Small process-local queue that stores callback data until the
  frame loop consumes it.

Invariants:
  - Process-local Win32 input accumulators are drained into frame and UI
    snapshots; stale mouse deltas must not leak across focus/UI transitions.
  - ShowCursor is normalized through helper loops because Win32 exposes a
    reference counter, not a simple visible/hidden boolean.

Related:
  - SkullbonezSource/Runtime/Input.h
  - Agentic/Reference/runtime-reference.md
  - Agentic/Reference/comment-style-guide.md
*/
#include "Input.h"
#include "Window.h"

#include <cassert>

using namespace SkullbonezCore::Hardware;
using namespace SkullbonezCore::Basics;

namespace
{
// Lifetime: Win32 calls WndProc without a Run instance, so callback-fed mouse
// queues stay process-local behind Input's static API. Keep new callback
// accumulator state behind the bound HWND so late or foreign callbacks cannot
// mutate stale frame input.
HWND s_callbackBridgeWindow = nullptr;
// Lifetime: ordinary polling helpers borrow the active runtime window while
// WinMain owns it. WndProc still passes HWNDs directly to callback-specific
// APIs, so this pointer is only for normal frame/input paths.
Window* s_inputWindow = nullptr;
// WndProc WM_MOUSEWHEEL writes; UIInput::CaptureSnapshot() consumes once per
// frame through InGameUI::UpdateInput() from RunInput.cpp. Focus-loss cleanup
// also drains it through InputController::ResetUnfocusedInput().
int g_mouseWheelDelta = 0;
// Cursor policy latch, not a callback accumulator. RunInput and window/focus
// paths write the requested native cursor state; WndProc focus/cursor messages
// reapply that state when Windows asks.
bool g_systemCursorVisibleRequested = false;
// WndProc WM_INPUT writes these raw movement deltas; camera mouse-look sampling
// drains them through ConsumeRawMouseDelta(). RegisterRawMouseInput(),
// InputController::ResetMouseLook(), InputController::ResetUnfocusedInput(),
// focus loss, and camera/mode transitions reset them through
// ResetMouseLookDeltas().
long g_rawMouseDeltaX = 0;
long g_rawMouseDeltaY = 0;
bool g_rawMouseHasAbsolutePosition = false;
long g_rawMouseLastAbsoluteX = 0;
long g_rawMouseLastAbsoluteY = 0;
// Scripted input override, not a callback accumulator. RunInteractionAutomation
// writes it through SetAutomationState()/ClearAutomationState(); mouse polling
// reads it before touching Win32 so deterministic UI/click validation can avoid
// the physical cursor.
Input::AutomationState g_automationState;

constexpr int RAW_MOUSE_ABSOLUTE_RANGE = 65535;

bool IsCallbackBridgeBoundForWindow( HWND window )
{
    return window && s_callbackBridgeWindow == window;
}


Window* BoundInputWindow()
{
    return s_inputWindow;
}

void EnsureShowCursorVisible()
{
    int counter = ShowCursor( TRUE );
    while ( counter < 0 )
    {
        counter = ShowCursor( TRUE );
    }
    if ( counter > 0 )
    {
        ShowCursor( FALSE );
    }
}


void EnsureShowCursorHidden()
{
    int counter = ShowCursor( FALSE );
    while ( counter >= 0 )
    {
        counter = ShowCursor( FALSE );
    }
    if ( counter < -1 )
    {
        ShowCursor( TRUE );
    }
}


long RawAbsoluteToPixels( long value, int extent )
{
    if ( extent <= 0 )
    {
        return 0;
    }
    return static_cast<long>( MulDiv( static_cast<int>( value ), extent, RAW_MOUSE_ABSOLUTE_RANGE ) );
}
} // namespace


/*
   0x8000 = (16^3)*8 + (16^2)*0 + (16^1)*0 + (16^0)*0
          = 32,768 (decimal)
          = 1000 0000 0000 0000 (binary)
   This is the highest order bit of a 16 bit piece of data (SHORT)
 */
#define HIGHEST_ORDER_BIT_16 0x8000

/*
   0x1 = 0x0001 = (16^3)*0 + (16^2)*0 + (16^1)*0 + (16^0)*1
       = 1 (decimal)
       = 1 (binary)
       = 0000 0000 0000 0001 (binary)
   This is the lowest order bit of any sized piece of binary data
   */
#define LOWEST_ORDER_BIT_16 0x1

bool Input::IsAppFocused()
{
    Window* window = BoundInputWindow();
    if ( !window || !window->m_sWindow )
    {
        return false;
    }

    return GetForegroundWindow() == window->m_sWindow;
}


void Input::BindWindow( Window& window )
{
    assert( !s_inputWindow && "Input window bridge is already bound" );
    s_inputWindow = &window;
}


void Input::UnbindWindow( Window& window )
{
    assert( s_inputWindow == &window && "Input window bridge unbound with a different window" );
    if ( s_inputWindow == &window )
    {
        s_inputWindow = nullptr;
    }
}


void Input::SetSystemCursorVisible( bool visible )
{
    g_systemCursorVisibleRequested = visible;
    if ( visible )
    {
        SetCursor( LoadCursor( nullptr, IDC_ARROW ) );
        EnsureShowCursorVisible();
    }
    else
    {
        SetCursor( nullptr );
        EnsureShowCursorHidden();
    }
}


bool Input::IsSystemCursorVisibleRequested()
{
    return g_systemCursorVisibleRequested;
}


bool Input::IsKeyDown( int virtualKey )
{
    if ( !IsAppFocused() )
    {
        return false;
    }

    /*
        recall that HIGHEST_ORDER_BIT_16 = 1000 0000 0000 0000 (binary)
        recall that a conditional statement in C++ simply checks if the value is
        nonzero, and if it is nonzero returns true, otherwise false.
        GetKeyState(virtualKey) will return a 16 bit SHORT and if the key is pressed,
        the SHORT returned will have the highest order bit set to 1.
        the binary AND operator '&' will do the comparision on the two SHORTs
        1000 0000 0000 0000 & 1000 0000 0000 0000 (if the key is pressed)
           = 1000 0000 0000 0000 != 0
        1000 0000 0000 0000 & 0000 0000 0000 0000 (if the key is not pressed)
           = 0000 0000 0000 0000 == 0
    */
    return ( ( GetKeyState( virtualKey ) & HIGHEST_ORDER_BIT_16 ) != 0 );
}


bool Input::IsKeyToggled( int virtualKey )
{
    if ( !IsAppFocused() )
    {
        return false;
    }

    // lowest order bit is set to 1 if key is toggled, see Input::IsKeyDown
    // for an explanation on the conditional statement below
    return ( ( GetKeyState( virtualKey ) & LOWEST_ORDER_BIT_16 ) != 0 );
}


bool Input::RegisterRawMouseInput( HWND window )
{
    assert( IsCallbackBridgeBoundForWindow( window ) &&
            "Raw mouse input must register through the bound callback bridge HWND" );
    if ( !IsCallbackBridgeBoundForWindow( window ) )
    {
        return false;
    }
    if ( !window )
    {
        return false;
    }

    RAWINPUTDEVICE device = {};
    device.usUsagePage = 0x01; // Generic desktop controls
    device.usUsage = 0x02;     // Mouse
    device.dwFlags = 0;        // Keep legacy mouse messages for UI hit-testing
    device.hwndTarget = window;

    if ( !RegisterRawInputDevices( &device, 1, sizeof( device ) ) )
    {
        return false;
    }

    ResetMouseLookDeltas();
    return true;
}


void Input::BindCallbackBridge( HWND window )
{
    assert( window && "Input callback bridge requires a live HWND" );
    assert( !s_callbackBridgeWindow && "Input callback bridge is already bound" );
    if ( !window )
    {
        return;
    }

    s_callbackBridgeWindow = window;
    ClearCallbackEventBuffer( window );
}


void Input::UnbindCallbackBridge( HWND window )
{
    assert( s_callbackBridgeWindow && "Input callback bridge must be bound before unbind" );
    assert( s_callbackBridgeWindow == window && "Input callback bridge unbound with a different HWND" );
    if ( s_callbackBridgeWindow != window )
    {
        return;
    }

    ClearCallbackEventBuffer( window );
    s_callbackBridgeWindow = nullptr;
}


void Input::ClearCallbackEventBuffer( HWND window )
{
    assert( IsCallbackBridgeBoundForWindow( window ) &&
            "Input callback event buffer can only be cleared for the bound HWND" );
    if ( !IsCallbackBridgeBoundForWindow( window ) )
    {
        return;
    }

    (void)ConsumeMouseWheelDelta();
    ResetMouseLookDeltas();
}


void Input::AccumulateRawMouseDelta( HWND window, HRAWINPUT rawInput )
{
    if ( !IsCallbackBridgeBoundForWindow( window ) || !rawInput || !IsAppFocused() )
    {
        return;
    }

    RAWINPUT data = {};
    UINT dataSize = sizeof( data );
    const UINT bytesRead = GetRawInputData( rawInput, RID_INPUT, &data, &dataSize, sizeof( RAWINPUTHEADER ) );
    if ( bytesRead == static_cast<UINT>( -1 ) || data.header.dwType != RIM_TYPEMOUSE )
    {
        return;
    }

    const RAWMOUSE& mouse = data.data.mouse;
    if ( ( mouse.usFlags & MOUSE_MOVE_ABSOLUTE ) != 0 )
    {
        const int width = ( mouse.usFlags & MOUSE_VIRTUAL_DESKTOP ) != 0 ? GetSystemMetrics( SM_CXVIRTUALSCREEN )
                                                                         : GetSystemMetrics( SM_CXSCREEN );
        const int height = ( mouse.usFlags & MOUSE_VIRTUAL_DESKTOP ) != 0 ? GetSystemMetrics( SM_CYVIRTUALSCREEN )
                                                                          : GetSystemMetrics( SM_CYSCREEN );

        const long currentX = RawAbsoluteToPixels( mouse.lLastX, width );
        const long currentY = RawAbsoluteToPixels( mouse.lLastY, height );
        if ( g_rawMouseHasAbsolutePosition )
        {
            g_rawMouseDeltaX += currentX - g_rawMouseLastAbsoluteX;
            g_rawMouseDeltaY += currentY - g_rawMouseLastAbsoluteY;
        }
        g_rawMouseLastAbsoluteX = currentX;
        g_rawMouseLastAbsoluteY = currentY;
        g_rawMouseHasAbsolutePosition = true;
    }
    else
    {
        g_rawMouseDeltaX += mouse.lLastX;
        g_rawMouseDeltaY += mouse.lLastY;
        g_rawMouseHasAbsolutePosition = false;
    }
}


bool Input::ConsumeRawMouseDelta( long& xMove, long& yMove )
{
    if ( !IsAppFocused() )
    {
        ResetMouseLookDeltas();
        xMove = 0;
        yMove = 0;
        return false;
    }

    xMove = g_rawMouseDeltaX;
    yMove = g_rawMouseDeltaY;
    g_rawMouseDeltaX = 0;
    g_rawMouseDeltaY = 0;
    return xMove != 0 || yMove != 0;
}


void Input::ResetMouseLookDeltas()
{
    g_rawMouseDeltaX = 0;
    g_rawMouseDeltaY = 0;
    g_rawMouseHasAbsolutePosition = false;
    g_rawMouseLastAbsoluteX = 0;
    g_rawMouseLastAbsoluteY = 0;
}


POINT Input::GetMouseCoordinates()
{
    POINT mousePos;
    if ( !GetCursorPos( &mousePos ) ) // attempt to get the mouse m_position
    {
        throw std::runtime_error( "Getting mouse coordinates failed (Input::GetMouseCoordinates)." );
    }

    return mousePos;
}


POINT Input::GetClientMouseCoordinates()
{
    if ( g_automationState.enabled && g_automationState.hasMouseClientPosition )
    {
        return g_automationState.mouseClientPosition;
    }

    POINT mousePos = GetMouseCoordinates();
    Window* m_cWindow = BoundInputWindow();
    assert( m_cWindow && "Input client mouse coordinates require a bound window" );
    if ( !m_cWindow )
    {
        throw std::runtime_error( "Input window bridge is not bound (Input::GetClientMouseCoordinates)." );
    }
    if ( !ScreenToClient( m_cWindow->m_sWindow, &mousePos ) )
    {
        throw std::runtime_error( "Converting mouse coordinates failed (Input::GetClientMouseCoordinates)." );
    }

    return mousePos;
}


void Input::SetMouseCoordinates( const POINT& pNewCoordinates )
{
    if ( !IsAppFocused() )
    {
        return;
    }

    // attempt to set the mouse m_position
    if ( !SetCursorPos( pNewCoordinates.x, pNewCoordinates.y ) )
    {
        throw std::runtime_error( "Setting mouse m_position failed (Input::SetMouseCoordinates)." );
    }
}


bool Input::IsLeftMouseDown()
{
    if ( g_automationState.enabled )
    {
        return g_automationState.leftMouseDown;
    }

    if ( !IsAppFocused() )
    {
        return false;
    }

    return ( ( GetAsyncKeyState( VK_LBUTTON ) & HIGHEST_ORDER_BIT_16 ) != 0 );
}


bool Input::IsRightMouseDown()
{
    if ( g_automationState.enabled )
    {
        return g_automationState.rightMouseDown;
    }

    if ( !IsAppFocused() )
    {
        return false;
    }

    return ( ( GetAsyncKeyState( VK_RBUTTON ) & HIGHEST_ORDER_BIT_16 ) != 0 );
}


bool Input::IsMiddleMouseDown()
{
    if ( !IsAppFocused() )
    {
        return false;
    }

    return ( ( GetAsyncKeyState( VK_MBUTTON ) & HIGHEST_ORDER_BIT_16 ) != 0 );
}


int Input::ConsumeMouseWheelDelta()
{
    if ( !IsAppFocused() )
    {
        g_mouseWheelDelta = 0;
        return 0;
    }

    const int delta = g_mouseWheelDelta;
    g_mouseWheelDelta = 0;
    return delta;
}


void Input::AccumulateMouseWheelDelta( HWND window, int delta )
{
    if ( !IsCallbackBridgeBoundForWindow( window ) || !IsAppFocused() )
    {
        return;
    }

    g_mouseWheelDelta += delta;
}


void Input::CentreMouseCoordinates()
{
    if ( !IsAppFocused() )
    {
        return;
    }

    Window* m_cWindow = BoundInputWindow();
    assert( m_cWindow && "Input mouse centering requires a bound window" );
    if ( !m_cWindow )
    {
        throw std::runtime_error( "Input window bridge is not bound (Input::CentreMouseCoordinates)." );
    }
    POINT clientCenter = { m_cWindow->m_sWindowDimensions.x >> 1, m_cWindow->m_sWindowDimensions.y >> 1 };
    if ( !ClientToScreen( m_cWindow->m_sWindow, &clientCenter ) )
    {
        throw std::runtime_error( "Converting mouse center failed (Input::CentreMouseCoordinates)." );
    }

    if ( !SetCursorPos( clientCenter.x, clientCenter.y ) )
    {
        throw std::runtime_error( "Setting mouse center failed (Input::CentreMouseCoordinates)." );
    }
}


void Input::SetAutomationState( const AutomationState& state )
{
    g_automationState = state;
}


void Input::ClearAutomationState()
{
    g_automationState = AutomationState{};
}
