/*
File: SkullbonezSource/Input.h
Purpose:
  Collects keyboard and mouse state for the run loop and UI.

Mental model:
  Runtime code connects authored scene data, input, simulation, render
  backends, and validation-oriented launch modes. Follow who owns state and
  when that state changes.

Glossary:
  Win32: Windows desktop API used for the app window, messages, and process
  integration.
  Validation gate: Repository script that proves a class of changes before
  commit or PR.

Related:
  - SkullbonezSource/Input.cpp
  - Agentic/Reference/runtime-reference.md
  - Agentic/Reference/comment-style-guide.md
*/
#pragma once


#include "Common.h"

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
    // Bit indices for the key-state bitmask
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

    uint32_t keys = 0; // Packed bit field for all boolean key states
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
    static bool IsAppFocused();                         // True when the game window owns foreground input
    static void SetSystemCursorVisible( bool visible ); // Shows or hides the Win32 cursor display counter
    static bool IsSystemCursorVisibleRequested();       // Last requested native cursor ownership state
    static bool IsKeyDown( int virtualKey );            // Returns true if specified key is pressed (use upper case)
    static bool IsKeyToggled( int virtualKey );         // Returns true if specified key is toggled (use upper case)
    static bool RegisterRawMouseInput( HWND window );   // Registers the window for relative mouse movement messages
    static void
    AccumulateRawMouseDelta( HRAWINPUT rawInput ); // Adds mouse movement from WM_INPUT to the per-frame queue
    static bool ConsumeRawMouseDelta( long& xMove, long& yMove ); // Returns and clears accumulated raw mouse movement
    static void ResetMouseLookDeltas();       // Clears queued raw mouse movement and absolute tracking state
    static POINT GetMouseCoordinates();       // Returns the coordinates of the mouse cursor
    static POINT GetClientMouseCoordinates(); // Returns mouse coordinates relative to the app client area
    static void SetMouseCoordinates( const POINT& pNewCoordinates ); // Sets the mouse coordinates
    static void CentreMouseCoordinates(); // Sets the mouse cursor to the centre of the screen
    static bool IsLeftMouseDown();        // Returns true if the left mouse button is pressed
    static bool IsRightMouseDown();       // Returns true if the right mouse button is pressed
    static bool IsMiddleMouseDown();      // Returns true if the middle mouse button is pressed
    static int ConsumeMouseWheelDelta();  // Returns and clears accumulated wheel delta from Win32 messages
    static void AccumulateMouseWheelDelta( int delta ); // Adds a Win32 wheel delta to the per-frame queue
};
} // namespace Hardware
} // namespace SkullbonezCore
