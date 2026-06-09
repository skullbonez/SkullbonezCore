#pragma once


// --- Includes ---
#include "SkullbonezCommon.h"

namespace SkullbonezCore
{
namespace Hardware
{
/* -- Input State ------------------------------------------------------------------------------------------------------------------------------------------------

    Holds input state to help separate logic and input code.  Should be modified depending on the games input requirements.
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
        ZWasDown,
        XWasDown,
        BackspaceWasDown,
        QKeyWasDown,
        CKeyWasDown,
        Key6WasDown,
        Key1WasDown,
        Key3WasDown,
        Key4WasDown,
        Key5WasDown,
        EscapeWasDown,
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

/* -- Input ------------------------------------------------------------------------------------------------------------------------------------------------------

    Static methods to wrap up input functions from the Win32 API.
-----------------------------------------------------------------------------------------------------------------------------------------------------------------*/
class Input
{
  public:
    static bool IsAppFocused();                                      // True when the game window owns foreground input
    static void SetSystemCursorVisible( bool visible );              // Shows or hides the Win32 cursor display counter
    static bool IsKeyDown( const char cKey );                        // Returns true if specified key is pressed (use upper case)
    static bool IsKeyToggled( const char cKey );                     // Returns true if specified key is toggled (use upper case)
    static POINT GetMouseCoordinates();                              // Returns the coordinates of the mouse cursor
    static POINT GetClientMouseCoordinates();                        // Returns mouse coordinates relative to the app client area
    static void SetMouseCoordinates( const POINT& pNewCoordinates ); // Sets the mouse coordinates
    static void CentreMouseCoordinates();                            // Sets the mouse cursor to the centre of the screen
    static bool IsLeftMouseDown();                                   // Returns true if the left mouse button is pressed
    static int ConsumeMouseWheelDelta();                             // Returns and clears accumulated wheel delta from Win32 messages
    static void AccumulateMouseWheelDelta( int delta );              // Adds a Win32 wheel delta to the per-frame queue
};
} // namespace Hardware
} // namespace SkullbonezCore
