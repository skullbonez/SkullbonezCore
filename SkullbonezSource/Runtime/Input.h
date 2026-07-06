/*
File: SkullbonezSource/Runtime/Input.h
Purpose:
  Collects keyboard and mouse state for the run loop and UI.

Mental model:
  Runtime code connects authored scene data, input, simulation, render
  backends, and validation-oriented launch modes. Follow who owns state and
  when that state changes.

Glossary:
  Win32: Windows desktop API used for the app window, messages, and process
  integration.
  HWND (Window Handle): Win32 identifier for the native application window.
  HRAWINPUT: Win32 handle for one raw-input packet received through WM_INPUT.
  WM_INPUT: Win32 message carrying high-resolution mouse movement.
  Callback bridge: The process-local state that lets Win32 callbacks enqueue
    mouse data until the frame loop consumes it.
  Automation override: Scripted input snapshot used by interaction validation
    while the normal runtime input controller still owns command edges.
  Input event buffer: Snapshot of callback-fed mouse accumulators for the bound
    native window.
  Validation gate: Repository script that proves a class of changes before
  commit or PR.

Invariants:
  - InputState is a frame snapshot; command-edge memory lives in
    RuntimeInputContext, not in raw device polling.
  - Key enum order is storage ABI for the bit mask and should be appended to,
    not reordered.
  - Window-dependent polling borrows the active runtime window through the input
    bridge instead of reacquiring the process singleton.

Related:
  - SkullbonezSource/Runtime/Input.cpp
  - Agentic/Reference/runtime-reference.md
  - Agentic/Reference/comment-style-guide.md
*/
#pragma once


#include "../Core/Common.h"

namespace SkullbonezCore
{
namespace Basics
{
class Window;
}

namespace Hardware
{
/* -- Input State
------------------------------------------------------------------------------------------------------------------------------------------------

    Holds input state to help separate logic and input code.  Should be modified depending on the games input
requirements.
-----------------------------------------------------------------------------------------------------------------------------------------------------------------*/
struct InputState
{
    // InputState is copied through the run loop, so the per-frame booleans live
    // in one compact mask instead of a scattered set of mutable fields.
    enum Key : uint32_t
    {
        Up = 0,
        Down,
        Left,
        Right,
        Aux1,
        Aux2,
        GKeyWasDown,
        RKeyWasDown,
        Key0WasDown,
        F2WasDown,
        F3WasDown,
        FWasDown,
        VWasDown,
        NWasDown,
        EnterWasDown,
        LeftMouseWasDown,
        BackspaceWasDown,
        QKeyWasDown,
        CKeyWasDown,
        Key6WasDown,
        Key1WasDown,
        Key3WasDown,
        Key4WasDown,
        Key5WasDown,
        EscapeWasDown,
        OKeyWasDown,
        MKeyWasDown,
        KEY_COUNT
    };

    uint32_t keys = 0;                                   // One bit per Key enum entry; copied into frame-local camera/UI state.
    long xMove = 0, yMove = 0;

    bool Get( Key k ) const
    {
        return ( keys >> k ) & 1u;
    }
    void Set( Key k, bool v )
    {
        if ( v )
        {
            keys |= ( 1u << k );
        }
        else
        {
            keys &= ~( 1u << k );
        }
    }
};

/* -- Input
------------------------------------------------------------------------------------------------------------------------------------------------------

    Static methods to wrap up input functions from the Win32 API.
-----------------------------------------------------------------------------------------------------------------------------------------------------------------*/
class Input
{
  public:
    struct AutomationState
    {
        bool enabled = false;
        bool hasMouseClientPosition = false;
        POINT mouseClientPosition = {};
        bool leftMouseDown = false;
        bool rightMouseDown = false;
        int keyVirtualKey = 0;                           // Optional one-key automation override.
        bool keyDown = false;
    };

    struct InputEventBuffer
    {
        HWND window = nullptr;                           // Bound HWND that may write callback-fed accumulators.
        int mouseWheelDelta = 0;                         // Queued WM_MOUSEWHEEL clicks waiting for UI/frame consumption.
        long rawMouseDeltaX = 0, rawMouseDeltaY = 0;     // Queued WM_INPUT movement waiting for mouse-look consumption.
        bool rawMouseHasAbsolutePosition = false;        // True after the first absolute raw-input packet seeds tracking.
        long rawMouseLastAbsoluteX = 0, rawMouseLastAbsoluteY = 0;
    };

    static bool IsAppFocused();                          // True when the game window owns foreground input
    static void BindWindow( Basics::Window& window );    // Binds the runtime-owned window used by polling helpers.
    static void UnbindWindow( Basics::Window& window );  // Clears the polling window before HWND teardown.
    static void SetSystemCursorVisible( bool visible );  // Shows or hides the Win32 cursor display counter
    static bool IsSystemCursorVisibleRequested();        // Last requested native cursor ownership state
    static bool IsKeyDown( int virtualKey );             // Polls Win32 virtual-key state; alphabetic callers pass uppercase codes.
    static bool IsKeyToggled( int virtualKey );          // Reads Win32 toggle state for latch-style keys such as Caps Lock.
    static void BindCallbackBridge( HWND window );       // Arms callback-fed input queues for the active HWND.
    static void UnbindCallbackBridge( HWND window );     // Disarms callback-fed queues and clears stale queued input.
    static void ClearCallbackEventBuffer( HWND window ); // Clears queued callback data for the bound HWND.
    static bool RegisterRawMouseInput( HWND window );    // Registers the window for relative mouse movement messages
    static void
    AccumulateRawMouseDelta( HWND window,
                             HRAWINPUT rawInput );       // Adds WM_INPUT movement when the callback bridge is bound.
    static bool ConsumeRawMouseDelta( long& xMove,
                                      long& yMove );     // Moves queued raw deltas into caller outputs once per frame.
    static void ResetMouseLookDeltas();                  // Clears queued raw mouse movement and absolute tracking state
    static POINT GetMouseCoordinates();                  // Screen-space cursor position for compatibility paths.
    static POINT GetClientMouseCoordinates();            // Cursor position translated into the game window client area.
    static void SetMouseCoordinates(
        const POINT& pNewCoordinates );                  // Warps the OS cursor; used only by camera-control recentering.
    static void CentreMouseCoordinates();                // Recenters the cursor in the current game window.
    static bool IsLeftMouseDown();                       // Polls left-button state without consuming it.
    static bool IsRightMouseDown();                      // Polls right-button state without consuming it.
    static bool IsMiddleMouseDown();                     // Polls middle-button state without consuming it.
    static int ConsumeMouseWheelDelta();                 // Moves queued wheel clicks into the caller and resets the frame accumulator.
    static void AccumulateMouseWheelDelta( HWND window,
                                           int delta );  // Adds a Win32 wheel delta when the callback bridge is bound.
    static void SetAutomationState( const AutomationState& state );
    static void ClearAutomationState();
};
} // namespace Hardware
} // namespace SkullbonezCore
