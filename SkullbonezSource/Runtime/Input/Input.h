/*
File: SkullbonezSource/Runtime/Input/Input.h
Purpose:
  Collects keyboard and mouse state for the run loop and UI.

Summary:
  Input.h collects keyboard and mouse state for the run loop and UI. As a
  public header, keep edits anchored on local owner boundaries and call
  direction and on the glossary/invariants below.

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
  Input event buffer: Callback-fed mouse accumulators drained into the next
    DeviceInputFrame for the bound native window.
  Lane R result: Recoverable input/environment failure reported without
    treating the cursor operation as a fatal engine invariant.

Invariants:
  - CaptureDeviceInputFrame is the only steady-frame hardware poll. Semantic
    keys, pointer consumers, UI, replay, editor, and camera use that value.
  - Key enum order is storage ABI for the bit mask and should be appended to,
    not reordered.
  - Window-dependent polling borrows the active runtime window through the input
    bridge instead of reacquiring the process singleton.

Related:
  - SkullbonezSource/Runtime/Input/Input.cpp
  - Agentic/Reference/runtime-reference.md
  - Agentic/Reference/comment-style-guide.md
*/
#pragma once

#include "../../Core/PlatformWin32.h"


#include "../../Core/Common.h"
#include "../../Core/SbResult.h"

namespace SkullbonezCore
{
namespace Runtime
{
struct DeviceInputFrame;
class Window;
} // namespace Runtime

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
        bool overrideAppFocused = false;                 // Automation-only focus-loss probe for one captured frame.
        bool appFocused = true;
        bool hasMouseClientPosition = false;
        POINT mouseClientPosition = {};
        bool leftMouseDown = false;
        bool rightMouseDown = false;
        int keyVirtualKey = 0;                           // Optional one-key automation override.
        bool keyDown = false;
        bool controlDown = false;                        // Optional modifier paired with the injected key.
    };

    struct InputEventBuffer
    {
        HWND window = nullptr;                           // Bound HWND that may write callback-fed accumulators.
        int mouseWheelDelta = 0;                         // Queued WM_MOUSEWHEEL clicks waiting for UI/frame consumption.
        long rawMouseDeltaX = 0, rawMouseDeltaY = 0;     // Queued WM_INPUT movement waiting for mouse-look consumption.
        bool rawMouseHasAbsolutePosition = false;        // True after the first absolute raw-input packet seeds tracking.
        long rawMouseLastAbsoluteX = 0, rawMouseLastAbsoluteY = 0;
    };

    struct MouseCoordinatesResult
    {
        SkullbonezCore::Core::SbResult result;           // Lane R result for Win32 cursor/client-coordinate failures.
        POINT coordinates = {};
    };

    static void BindWindow( Runtime::Window& window );   // Binds the runtime-owned window used by frame capture.
    static void UnbindWindow( Runtime::Window& window ); // Clears the polling window before HWND teardown.
    static void SetSystemCursorVisible( bool visible );  // Shows or hides the Win32 cursor display counter
    static bool IsSystemCursorVisibleRequested();        // Last requested native cursor ownership state
    static SkullbonezCore::Core::SbResult CaptureDeviceInputFrame(
        Runtime::DeviceInputFrame& frame );              // Captures the complete immutable keyboard/pointer frame once.
    static SkullbonezCore::Core::SbResult
    SetNativeMouseCapture( bool captured );              // Applies InputRouter's single native-capture decision.
    static void BindCallbackBridge( HWND window );       // Arms callback-fed input queues for the active HWND.
    static void UnbindCallbackBridge( HWND window );     // Disarms callback-fed queues and clears stale queued input.
    static void ClearCallbackEventBuffer( HWND window ); // Clears queued callback data for the bound HWND.
    static bool RegisterRawMouseInput( HWND window );    // Registers the window for relative mouse movement messages
    static void
    AccumulateRawMouseDelta( HWND window,
                             HRAWINPUT rawInput );       // Adds WM_INPUT movement when the callback bridge is bound.
    static void ResetMouseLookDeltas();                  // Clears queued raw mouse movement and absolute tracking state
    static void AccumulateMouseWheelDelta( HWND window,
                                           int delta );  // Adds a Win32 wheel delta when the callback bridge is bound.
    static void SetAutomationState( const AutomationState& state );
    static void ClearAutomationState();

  private:
    // Hardware reads are private so steady runtime consumers cannot bypass the
    // one DeviceInputFrame capture performed at the frame boundary.
    static bool IsAppFocused();
    static bool ConsumeRawMouseDelta( long& xMove, long& yMove );
    static MouseCoordinatesResult GetMouseCoordinates();
    static MouseCoordinatesResult GetClientMouseCoordinates();
    static int ConsumeMouseWheelDelta();
};
} // namespace Hardware
} // namespace SkullbonezCore
