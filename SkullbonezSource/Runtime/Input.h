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
  Validation gate: Repository script that proves a class of changes before
  commit or PR.

Related:
  - SkullbonezSource/Runtime/Input.cpp
  - Agentic/Reference/runtime-reference.md
  - Agentic/Reference/comment-style-guide.md
*/
#pragma once


#include "../Core/Common.h"

namespace SkullbonezCore
{
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

    uint32_t keys = 0;                                         // One bit per Key enum entry; copied into frame-local camera/UI state.
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
    };

    static bool IsAppFocused();                                // True when the game window owns foreground input
    static void SetSystemCursorVisible( bool visible );        // Shows or hides the Win32 cursor display counter
    static bool IsSystemCursorVisibleRequested();              // Last requested native cursor ownership state
    static bool IsKeyDown( int virtualKey );                   // Polls Win32 virtual-key state; alphabetic callers pass uppercase codes.
    static bool IsKeyToggled( int virtualKey );                // Reads Win32 toggle state for latch-style keys such as Caps Lock.
    static bool RegisterRawMouseInput( HWND window );          // Registers the window for relative mouse movement messages
    static void AccumulateRawMouseDelta( HRAWINPUT rawInput ); // Adds mouse movement from WM_INPUT to the per-frame queue
    static bool ConsumeRawMouseDelta( long& xMove,
                                      long& yMove );           // Moves queued raw deltas into caller outputs once per frame.
    static void ResetMouseLookDeltas();                        // Clears queued raw mouse movement and absolute tracking state
    static POINT GetMouseCoordinates();                        // Screen-space cursor position for compatibility paths.
    static POINT GetClientMouseCoordinates();                  // Cursor position translated into the game window client area.
    static void SetMouseCoordinates(
        const POINT& pNewCoordinates );                        // Warps the OS cursor; used only by camera-control recentering.
    static void CentreMouseCoordinates();                      // Recenters the cursor in the current game window.
    static bool IsLeftMouseDown();                             // Polls left-button state without consuming it.
    static bool IsRightMouseDown();                            // Polls right-button state without consuming it.
    static bool IsMiddleMouseDown();                           // Polls middle-button state without consuming it.
    static int ConsumeMouseWheelDelta();                       // Moves queued wheel clicks into the caller and resets the frame accumulator.
    static void AccumulateMouseWheelDelta( int delta );        // Adds a Win32 wheel delta to the per-frame queue
    static void SetAutomationState( const AutomationState& state );
    static void ClearAutomationState();
};
} // namespace Hardware
} // namespace SkullbonezCore
