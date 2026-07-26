/*
File: SkullbonezSource/Runtime/Input/Input.cpp
Purpose:
  Collects keyboard and mouse state for the run loop and UI.

Summary:
  Win32 callbacks enqueue mouse-only edge data into process-local
  accumulators. CaptureDeviceInputFrame drains those queues and captures
  keyboard, buttons, pointer position, and focus exactly once. Cursor hardware
  application and scripted automation share this file but never bypass that
  immutable frame value.

Glossary:
  Win32: Windows desktop API used for the app window, messages, and cursor
    state.
  WndProc: Win32 window callback that receives mouse wheel and raw mouse
    packets before the frame boundary captures input.
  Input window bridge: Borrowed pointer to the active runtime window used by
    frame capture to translate pointer positions through the current client area.
  Accumulator: Small process-local queue that stores callback data until the
    frame loop consumes it.
  Lane R result: Recoverable input/environment failure reported without
    treating the cursor operation as a fatal engine invariant.

Invariants:
  - Process-local Win32 input accumulators are drained into one DeviceInputFrame;
    stale mouse deltas must not leak across focus/UI transitions.
  - The input window bridge is bound before frame capture translates pointer
    coordinates.
  - ShowCursor is normalized through helper loops because Win32 exposes a
    reference counter, not a simple visible/hidden boolean.

Related:
  - SkullbonezSource/Runtime/Input/Input.h
  - Agentic/Reference/runtime-reference.md
  - Agentic/Reference/comment-style-guide.md
*/
#include "Input.h"
#include "InputRouter.h"
#include "../App/Window.h"

#include "../../Core/FatalError.h"

#include <cassert>

using namespace SkullbonezCore::Hardware;
using namespace SkullbonezCore::Runtime;

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

// WndProc WM_MOUSEWHEEL writes; CaptureDeviceInputFrame consumes once per
// frame so UI, editor, and replay cannot each drain the same wheel input.
int g_mouseWheelDelta = 0;

// Cursor policy latch, not a callback accumulator. InputFrameExecution and
// window/focus paths write the requested native cursor state; WndProc
// focus/cursor messages reapply that state when Windows asks.
bool g_systemCursorVisibleRequested = false;

// WndProc WM_INPUT writes these raw movement deltas. CaptureDeviceInputFrame
// drains them once; focus and scene transitions may clear the queue before the
// next frame so stale motion never reaches camera look.
long g_rawMouseDeltaX = 0;
long g_rawMouseDeltaY = 0;
bool g_rawMouseHasAbsolutePosition = false;
long g_rawMouseLastAbsoluteX = 0;
long g_rawMouseLastAbsoluteY = 0;

// Scripted input override, not a callback accumulator. InteractionAutomationController
// writes it through SetAutomationState()/ClearAutomationState(); mouse and
// capture folds it into DeviceInputFrame so deterministic validation uses the
// same immutable value as physical input.
Input::AutomationState s_automationState;

constexpr int RAW_MOUSE_ABSOLUTE_RANGE = 65535;

[[noreturn]] void FatalInputWindowBridgeMissing( const char* functionName )
{

    // Why: printf-style %p requires a void pointer in this fatal diagnostic;
    // the casts do not establish ownership or serve as runtime identity.
    SB_FATAL( "Input", "%s requires a bound input window bridge. inputWindow=%p callbackWindow=%p automation=%d",
              functionName, static_cast<void*>( s_inputWindow ), static_cast<void*>( s_callbackBridgeWindow ),
              s_automationState.enabled ? 1 : 0 );
}


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


bool Input::IsAppFocused()
{
    Window* window = BoundInputWindow();
    const HWND windowHandle = window ? window->NativeWindowHandle() : nullptr;

    if ( !windowHandle )
    {
        return false;
    }

    return GetForegroundWindow() == windowHandle;
}


SkullbonezCore::Core::SbResult Input::CaptureDeviceInputFrame( DeviceInputFrame& frame )
{
    frame = {};
    frame.appFocused = s_automationState.enabled && s_automationState.overrideAppFocused ? s_automationState.appFocused
                                                                                         : IsAppFocused();

    if ( !frame.appFocused )
    {
        ResetMouseLookDeltas();
        (void)ConsumeMouseWheelDelta();
        return SkullbonezCore::Core::SbResult::Success();
    }

    BYTE keyboardState[InputKeySnapshot::VIRTUAL_KEY_COUNT] = {};

    if ( !GetKeyboardState( keyboardState ) )
    {

        // Lane R: desktop/session state can make Win32 keyboard capture fail.
        // The frame owner must stop rather than route a fabricated all-up frame.
        return SkullbonezCore::Core::SbResult::
            Failure( "Runtime/Input", "GetKeyboardState failed while capturing the device frame (win32=%lu)",
                     static_cast<unsigned long>( GetLastError() ) );
    }

    std::array<uint64_t, InputKeySnapshot::WORD_COUNT> words = {};

    for ( int virtualKey = 0; virtualKey < InputKeySnapshot::VIRTUAL_KEY_COUNT; ++virtualKey )
    {

        if ( ( keyboardState[virtualKey] & 0x80u ) == 0u )
        {
            continue;
        }

        const std::size_t word = static_cast<std::size_t>( virtualKey ) / 64u;
        words[word] |= uint64_t { 1 } << ( static_cast<unsigned int>( virtualKey ) & 63u );
    }

    if ( s_automationState.enabled && s_automationState.keyDown && s_automationState.keyVirtualKey >= 0 &&
         s_automationState.keyVirtualKey < InputKeySnapshot::VIRTUAL_KEY_COUNT )
    {

        // Invariant: automation augments the same immutable snapshot consumed by
        // physical input; it does not retain a second command-edge path.
        const int virtualKey = s_automationState.keyVirtualKey;
        const std::size_t word = static_cast<std::size_t>( virtualKey ) / 64u;
        words[word] |= uint64_t { 1 } << ( static_cast<unsigned int>( virtualKey ) & 63u );
    }

    if ( s_automationState.enabled && s_automationState.controlDown )
    {

        // Why: modifier-aware interaction probes must exercise the normal
        // immutable keyboard snapshot used by editor shortcuts.
        const std::size_t word = static_cast<std::size_t>( VK_CONTROL ) / 64u;
        words[word] |= uint64_t { 1 } << ( static_cast<unsigned int>( VK_CONTROL ) & 63u );
    }

    frame.keys = InputKeySnapshot::FromWords( words );
    const MouseCoordinatesResult clientPosition = GetClientMouseCoordinates();

    if ( !clientPosition.result.ok )
    {
        return clientPosition.result;
    }

    frame.clientX = clientPosition.coordinates.x;
    frame.clientY = clientPosition.coordinates.y;
    frame.hasClientPosition = true;
    frame.leftDown = s_automationState.enabled ? s_automationState.leftMouseDown
                                               : ( keyboardState[VK_LBUTTON] & 0x80u ) != 0u;

    frame.rightDown = s_automationState.enabled ? s_automationState.rightMouseDown
                                                : ( keyboardState[VK_RBUTTON] & 0x80u ) != 0u;

    frame.middleDown = ( keyboardState[VK_MBUTTON] & 0x80u ) != 0u;
    frame.wheelDelta = ConsumeMouseWheelDelta();
    (void)ConsumeRawMouseDelta( frame.rawMouseX, frame.rawMouseY );
    return SkullbonezCore::Core::SbResult::Success();
}


SkullbonezCore::Core::SbResult Input::SetNativeMouseCapture( bool captured )
{

    // Lane R: InputRouter owns the decision, while this narrow hardware seam
    // verifies that Win32 accepted the requested capture transition.
    Window* window = BoundInputWindow();
    const HWND windowHandle = window ? window->NativeWindowHandle() : nullptr;

    if ( !windowHandle )
    {
        return SkullbonezCore::Core::SbResult::Failure( "Runtime/Input",
                                                        "Native mouse capture requires the bound runtime window" );
    }

    if ( captured )
    {
        SetCapture( windowHandle );

        if ( GetCapture() != windowHandle )
        {
            return SkullbonezCore::Core::SbResult::Failure( "Runtime/Input",
                                                            "SetCapture did not assign the runtime window (win32=%lu)",
                                                            static_cast<unsigned long>( GetLastError() ) );
        }

        return SkullbonezCore::Core::SbResult::Success();
    }

    // Do not release capture acquired by another window on this thread; this
    // seam owns only the bound runtime HWND.

    if ( GetCapture() != windowHandle )
    {
        return SkullbonezCore::Core::SbResult::Success();
    }

    if ( !ReleaseCapture() )
    {
        return SkullbonezCore::Core::SbResult::Failure( "Runtime/Input", "ReleaseCapture failed (win32=%lu)",
                                                        static_cast<unsigned long>( GetLastError() ) );
    }

    return SkullbonezCore::Core::SbResult::Success();
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

    device.dwFlags = 0; // Keep legacy mouse messages for UI hit-testing

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


// Why: Win32 cursor queries can fail for environment reasons outside engine
// ownership. Return a Lane R result so frame/UI owners can skip pointer input
// without unwinding through WndProc or the run loop.
Input::MouseCoordinatesResult Input::GetMouseCoordinates()
{
    MouseCoordinatesResult result;
    POINT mousePos = {};

    if ( !GetCursorPos( &mousePos ) ) // attempt to get the mouse m_position
    {
        result.result = SkullbonezCore::Core::SbResult::
            Failure( "Runtime/Input", "GetCursorPos failed in Input::GetMouseCoordinates lastError=%lu",
                     static_cast<unsigned long>( GetLastError() ) );

        return result;
    }

    result.coordinates = mousePos;
    return result;
}


Input::MouseCoordinatesResult Input::GetClientMouseCoordinates()
{
    MouseCoordinatesResult result;

    if ( s_automationState.enabled && s_automationState.hasMouseClientPosition )
    {
        result.coordinates = s_automationState.mouseClientPosition;
        return result;
    }

    MouseCoordinatesResult mousePos = GetMouseCoordinates();

    if ( !mousePos.result.ok )
    {
        return mousePos;
    }

    Window* m_cWindow = BoundInputWindow();
    assert( m_cWindow && "Input client mouse coordinates require a bound window" );

    if ( !m_cWindow )
    {
        FatalInputWindowBridgeMissing( "Input::GetClientMouseCoordinates" );
    }

    POINT clientCoordinates = mousePos.coordinates;

    if ( !ScreenToClient( m_cWindow->NativeWindowHandle(), &clientCoordinates ) )
    {
        result.result = SkullbonezCore::Core::SbResult::
            Failure( "Runtime/Input", "ScreenToClient failed in Input::GetClientMouseCoordinates lastError=%lu",
                     static_cast<unsigned long>( GetLastError() ) );

        return result;
    }

    result.coordinates = clientCoordinates;
    return result;
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


void Input::SetAutomationState( const AutomationState& state )
{
    s_automationState = state;
}


void Input::ClearAutomationState()
{
    s_automationState = AutomationState {};
}
