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
  Native window handle: Detached Win32 identity used for focus, capture, and
    pointer-coordinate queries without borrowing the Startup host owner.

Invariants:
  - Process-local Win32 input accumulators are drained into one DeviceInputFrame;
    stale mouse deltas must not leak across focus/UI transitions.
  - The native handle is bound before frame capture translates pointer
    coordinates.
  - Wrong-window and repeated unbind requests retain the current binding in
    Release while Debug keeps the caller-contract tripwire.
  - ShowCursor is normalized through helper loops because Win32 exposes a
    reference counter, not a simple visible/hidden boolean.
  - While automation is enabled, the complete synthetic keyboard, button,
    wheel, focus, pointer, and raw-delta state replaces physical device state.

Related:
  - SkullbonezSource/Runtime/Input/Input.h
  - Agentic/Reference/runtime-reference.md
  - Agentic/Reference/engine-glossary.md
*/
#include "Input.h"
#include "../../Core/SbDiagnosticStore.h"
#include "InputRouter.h"
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

DWORD RegisterRawMouseDevice( const RAWINPUTDEVICE& device, void* ) noexcept
{
    if ( RegisterRawInputDevices( &device, 1u, sizeof( device ) ) )
    {
        return ERROR_SUCCESS;
    }

    const DWORD error = GetLastError();
    return error == ERROR_SUCCESS ? ERROR_GEN_FAILURE : error;
}

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

[[noreturn]] void FatalInputWindowBridgeMissing( const char* functionName, HWND inputWindow )
{
    SB_FATAL( "Input", "%s requires a bound input window bridge. inputWindow=%p callbackWindow=%p automation=%d",
              functionName, static_cast<void*>( inputWindow ), static_cast<void*>( s_callbackBridgeWindow ),
              s_automationState.enabled ? 1 : 0 );
}


bool IsCallbackBridgeBoundForWindow( HWND window )
{
    return window && s_callbackBridgeWindow == window;
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

Input::NativeWindowBinding Input::s_nativeWindow;


bool Input::IsAppFocused()
{
    const HWND window = s_nativeWindow.BoundHandle();

    if ( !window )
    {
        return false;
    }

    return GetForegroundWindow() == window;
}


SkullbonezCore::Core::SbResult Input::CaptureDeviceInputFrame( SkullbonezCore::Core::SbDiagnosticStore& diagnostics,
                                                               DeviceInputFrame& frame )
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

    std::array<uint64_t, InputKeySnapshot::WORD_COUNT> words = {};

    if ( s_automationState.enabled )
    {
        // Invariant: synthetic playback replaces the complete device snapshot.
        // Operator input must never contaminate a deterministic automation run.
        words = s_automationState.keyWords;

        if ( s_automationState.keyDown && s_automationState.keyVirtualKey >= 0 &&
             s_automationState.keyVirtualKey < InputKeySnapshot::VIRTUAL_KEY_COUNT )
        {
            const int virtualKey = s_automationState.keyVirtualKey;
            const std::size_t word = static_cast<std::size_t>( virtualKey ) / 64u;
            words[word] |= uint64_t { 1 } << ( static_cast<unsigned int>( virtualKey ) & 63u );
        }

        if ( s_automationState.controlDown )
        {
            const std::size_t word = static_cast<std::size_t>( VK_CONTROL ) / 64u;
            words[word] |= uint64_t { 1 } << ( static_cast<unsigned int>( VK_CONTROL ) & 63u );
        }
    }
    else
    {
        BYTE keyboardState[InputKeySnapshot::VIRTUAL_KEY_COUNT] = {};

        if ( !GetKeyboardState( keyboardState ) )
        {
            return diagnostics.Failure( "Runtime/Input",
                                        "GetKeyboardState failed while capturing the device frame (win32=%lu)",
                                        static_cast<unsigned long>( GetLastError() ) );
        }

        for ( int virtualKey = 0; virtualKey < InputKeySnapshot::VIRTUAL_KEY_COUNT; ++virtualKey )
        {
            if ( ( keyboardState[virtualKey] & 0x80u ) != 0u )
            {
                const std::size_t word = static_cast<std::size_t>( virtualKey ) / 64u;
                words[word] |= uint64_t { 1 } << ( static_cast<unsigned int>( virtualKey ) & 63u );
            }
        }
    }

    frame.keys = InputKeySnapshot::FromWords( words );

    if ( s_automationState.enabled )
    {
        // Invariant: absence is part of the recorded pointer state. Do not turn
        // an absent sample into a synthetic (0,0) position that can hit UI.
        frame.SetClientPosition( s_automationState.hasMouseClientPosition, s_automationState.mouseClientPosition.x,
                                 s_automationState.mouseClientPosition.y );
    }
    else
    {
        const MouseCoordinatesResult clientPosition = GetClientMouseCoordinates( diagnostics );

        if ( !clientPosition.result.Ok() )
        {
            return clientPosition.result;
        }

        frame.SetClientPosition( true, clientPosition.coordinates.x, clientPosition.coordinates.y );
    }

    const int callbackWheelDelta = ConsumeMouseWheelDelta();

    if ( s_automationState.enabled )
    {
        frame.leftDown = s_automationState.leftMouseDown;
        frame.rightDown = s_automationState.rightMouseDown;
        frame.middleDown = s_automationState.middleMouseDown;
        frame.wheelDelta = s_automationState.mouseWheelDelta;
        frame.rawMouseX = s_automationState.rawMouseDeltaX;
        frame.rawMouseY = s_automationState.rawMouseDeltaY;
        ResetMouseLookDeltas();
    }
    else
    {
        frame.leftDown = frame.keys.IsDown( VK_LBUTTON );
        frame.rightDown = frame.keys.IsDown( VK_RBUTTON );
        frame.middleDown = frame.keys.IsDown( VK_MBUTTON );
        frame.wheelDelta = callbackWheelDelta;
        (void)ConsumeRawMouseDelta( frame.rawMouseX, frame.rawMouseY );
    }

    return SkullbonezCore::Core::SbResult::Success();
}


SkullbonezCore::Core::SbResult Input::SetNativeMouseCapture( SkullbonezCore::Core::SbDiagnosticStore& diagnostics,
                                                             bool captured )
{
    // Recoverable error: InputRouter owns the decision, while this narrow hardware seam
    // verifies that Win32 accepted the requested capture transition.
    const HWND windowHandle = s_nativeWindow.BoundHandle();

    if ( !windowHandle )
    {
        return diagnostics.Failure( "Runtime/Input", "Native mouse capture requires the bound runtime window" );
    }

    if ( captured )
    {
        SetCapture( windowHandle );

        if ( GetCapture() != windowHandle )
        {
            return diagnostics.Failure( "Runtime/Input", "SetCapture did not assign the runtime window (win32=%lu)",
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
        return diagnostics.Failure( "Runtime/Input", "ReleaseCapture failed (win32=%lu)",
                                    static_cast<unsigned long>( GetLastError() ) );
    }

    return SkullbonezCore::Core::SbResult::Success();
}


void Input::BindNativeWindow( HWND window )
{
    s_nativeWindow.Bind( window );
}


void Input::UnbindNativeWindow( HWND window )
{
    s_nativeWindow.Unbind( window );
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


SkullbonezCore::Core::SbResult Input::RegisterRawMouseInput( SkullbonezCore::Core::SbDiagnosticStore& diagnostics,
                                                            HWND window )
{
    return RegisterRawMouseInputWithOperation( diagnostics, window, IsCallbackBridgeBoundForWindow( window ),
                                               RegisterRawMouseDevice, nullptr );
}


SkullbonezCore::Core::SbResult Input::RegisterRawMouseInputWithOperation(
    SkullbonezCore::Core::SbDiagnosticStore& diagnostics, HWND window, bool callbackBridgeBound,
    RawMouseRegistrationOperation operation, void* context )
{
    assert( callbackBridgeBound && "Raw mouse input must register through the bound callback bridge HWND" );

    if ( !callbackBridgeBound || !window )
    {
        return diagnostics.Failure( "Runtime/Input", "Raw mouse registration requires the bound application window." );
    }

    if ( !operation )
    {
        return diagnostics.Failure( "Runtime/Input", "Raw mouse registration has no native operation." );
    }

    RAWINPUTDEVICE device = {};
    device.usUsagePage = 0x01; // Generic desktop controls
    device.usUsage = 0x02;     // Mouse

    device.dwFlags = 0; // Keep legacy mouse messages for UI hit-testing

    device.hwndTarget = window;

    const DWORD error = operation( device, context );

    if ( error != ERROR_SUCCESS )
    {
        return diagnostics.Failure( "Runtime/Input", "RegisterRawInputDevices failed. win32_error=%lu",
                                    static_cast<unsigned long>( error ) );
    }

    ResetMouseLookDeltas();
    return SkullbonezCore::Core::SbResult::Success();
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


void Input::AccumulateRawMouseSample( HWND window, long x, long y, bool absolute, bool virtualDesktop )
{
    if ( !IsCallbackBridgeBoundForWindow( window ) || !IsAppFocused() )
    {
        return;
    }

    if ( absolute )
    {
        const int width = GetSystemMetrics( virtualDesktop ? SM_CXVIRTUALSCREEN : SM_CXSCREEN );
        const int height = GetSystemMetrics( virtualDesktop ? SM_CYVIRTUALSCREEN : SM_CYSCREEN );
        const long currentX = RawAbsoluteToPixels( x, width );
        const long currentY = RawAbsoluteToPixels( y, height );

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
        g_rawMouseDeltaX += x;
        g_rawMouseDeltaY += y;
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
// ownership. Return a recoverable result so frame/UI owners can skip pointer input
// without unwinding through WndProc or the run loop.
Input::MouseCoordinatesResult Input::GetMouseCoordinates( SkullbonezCore::Core::SbDiagnosticStore& diagnostics )
{
    MouseCoordinatesResult result;
    POINT mousePos = {};

    if ( s_automationState.enabled )
    {
        if ( s_automationState.hasMouseClientPosition )
        {
            mousePos = s_automationState.mouseClientPosition;
        }

        result.coordinates = mousePos;
        return result;
    }

    if ( !GetCursorPos( &mousePos ) ) // attempt to get the mouse m_position
    {
        result.result = diagnostics.Failure( "Runtime/Input",
                                             "GetCursorPos failed in Input::GetMouseCoordinates lastError=%lu",
                                             static_cast<unsigned long>( GetLastError() ) );

        return result;
    }

    result.coordinates = mousePos;
    return result;
}


Input::MouseCoordinatesResult Input::GetClientMouseCoordinates( SkullbonezCore::Core::SbDiagnosticStore& diagnostics )
{
    MouseCoordinatesResult result;

    if ( s_automationState.enabled )
    {
        if ( s_automationState.hasMouseClientPosition )
        {
            result.coordinates = s_automationState.mouseClientPosition;
        }

        return result;
    }

    MouseCoordinatesResult mousePos = GetMouseCoordinates( diagnostics );

    if ( !mousePos.result.Ok() )
    {
        return mousePos;
    }

    const HWND window = s_nativeWindow.BoundHandle();
    assert( window && "Input client mouse coordinates require a bound window" );

    if ( !window )
    {
        FatalInputWindowBridgeMissing( "Input::GetClientMouseCoordinates", window );
    }

    POINT clientCoordinates = mousePos.coordinates;

    if ( !ScreenToClient( window, &clientCoordinates ) )
    {
        result.result = diagnostics.Failure( "Runtime/Input",
                                             "ScreenToClient failed in Input::GetClientMouseCoordinates lastError=%lu",
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
