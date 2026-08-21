/*
File: SkullbonezSource/Runtime/Input/Input.h
Purpose:
  Collects keyboard and mouse state for the run loop and UI.

Summary:
  Input captures one immutable keyboard/mouse frame, drains callback-fed raw
  mouse events, and applies automation through that same device snapshot before
  semantic routing.

Glossary:
  HRAWINPUT: Win32 handle for one raw-input packet received through WM_INPUT.
  WM_INPUT: Win32 message carrying high-resolution mouse movement.
  Automation override: Scripted input snapshot used by interaction validation

    while the normal runtime input controller still owns command edges.
  Input event buffer: Callback-fed mouse accumulators drained into the next
    DeviceInputFrame for the bound native window.

Invariants:
  - CaptureDeviceInputFrame is the only steady-frame hardware poll. Semantic
    keys, pointer consumers, UI, replay, editor, and camera use that value.
  - Key enum order is storage ABI for the bit mask and should be appended to,
    not reordered.
  - Window-dependent polling borrows the active runtime window through the input
    bridge instead of reacquiring the process singleton.
  - Input owns the sole retained Window borrow; unbind clears it only when the
    caller presents the currently bound Window identity.

Related:
  - SkullbonezSource/Runtime/Input/Input.cpp
  - Agentic/Reference/runtime-reference.md
  - Agentic/Reference/engine-glossary.md
*/
#pragma once

#include "../../Core/PlatformWin32.h"


#include "../../Core/Common.h"
#include "../../Core/SbResult.h"

#include <array>
#include <cassert>
#include <cstdint>

namespace SkullbonezCore
{
namespace Core
{
class SbDiagnosticStore;
}
namespace Runtime
{
struct DeviceInputFrame;
class Window;
} // namespace Runtime

namespace Hardware
{

struct InputWindowBridgeTestAccess;

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

    uint32_t keys = 0;                                                                                                  // One bit per Key enum entry; copied into frame-local camera/UI state.
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

class Input
{
  public:
    struct AutomationState
    {
        bool enabled = false;
        bool overrideAppFocused = false;                                                                                // Automation-only focus-loss probe for one captured frame.
        bool appFocused = true;
        bool hasMouseClientPosition = false;
        POINT mouseClientPosition = {};
        bool leftMouseDown = false;
        bool rightMouseDown = false;
        int mouseWheelDelta = 0;                                                                                        // One-frame wheel delta routed through the normal device snapshot.
        int keyVirtualKey = 0;                                                                                          // Optional one-key automation override.
        bool keyDown = false;
        bool controlDown = false;                                                                                       // Optional modifier paired with the injected key.
        std::array<uint64_t, 4> keyWords = {};                                                                          // Multi-key mask for held keyboard inputs (WASD, modifiers, space).
    };

    struct InputEventBuffer
    {
        HWND window = nullptr;                                                                                          // Bound HWND that may write callback-fed accumulators.
        int mouseWheelDelta = 0;                                                                                        // Queued WM_MOUSEWHEEL clicks waiting for UI/frame consumption.
        long rawMouseDeltaX = 0, rawMouseDeltaY = 0;                                                                    // Queued WM_INPUT movement waiting for mouse-look consumption.
        bool rawMouseHasAbsolutePosition = false;                                                                       // True after the first absolute raw-input packet seeds tracking.
        long rawMouseLastAbsoluteX = 0, rawMouseLastAbsoluteY = 0;
    };

    struct MouseCoordinatesResult
    {
        SkullbonezCore::Core::SbResult result;                                                                          // recoverable result for Win32 cursor/client-coordinate failures.
        POINT coordinates = {};
    };

    static void BindWindow( Runtime::Window& window );                                                                  // Binds the runtime-owned window used by frame capture.
    static void UnbindWindow( Runtime::Window& window );                                                                // Clears the polling window before HWND teardown.
    static void SetSystemCursorVisible( bool visible );                                                                 // Shows or hides the Win32 cursor display counter
    static bool IsSystemCursorVisibleRequested();                                                                       // Last requested native cursor ownership state
    static SkullbonezCore::Core::SbResult CaptureDeviceInputFrame( SkullbonezCore::Core::SbDiagnosticStore& diagnostics,
                                                                   Runtime::DeviceInputFrame& frame );                  // Captures the complete immutable keyboard/pointer frame once.
                                                                   static SkullbonezCore::Core::SbResult
                                                                   SetNativeMouseCapture( SkullbonezCore::Core::SbDiagnosticStore& diagnostics,
                                                                   bool captured );                                     // Applies InputRouter's single native-capture decision.
                                                                   static void BindCallbackBridge( HWND window );       // Arms callback-fed input queues for the active HWND.
                                                                   static void UnbindCallbackBridge( HWND window );     // Disarms callback-fed queues and clears stale queued input.
                                                                   static void ClearCallbackEventBuffer( HWND window ); // Clears queued callback data for the bound HWND.
                                                                   static bool RegisterRawMouseInput( HWND window );    // Registers the window for relative mouse movement messages
                                                                   static void AccumulateRawMouseDelta( HWND window,
                                                                   HRAWINPUT rawInput );                                // Adds WM_INPUT movement when the callback bridge is bound.
                                                                   static void ResetMouseLookDeltas();                  // Clears queued raw mouse movement and absolute tracking state
                                                                   static void AccumulateMouseWheelDelta( HWND window,
                                                                   int delta );                                         // Adds a Win32 wheel delta when the callback bridge is bound.
                                                                   static void SetAutomationState( const AutomationState& state );
                                                                   static void ClearAutomationState();

                                                                   private:
                                                                   friend struct InputWindowBridgeTestAccess;

                                                                   class WindowBridge
                                                                   {
                                                                   public:
                                                                   void Bind( Runtime::Window* window )
                                                                   {
                                                                   assert( !m_window && "Input window bridge is already bound" );
                                                                   m_window = window;
                                                                   }

                                                                   void Unbind( Runtime::Window* window )
                                                                   {
                                                                   assert( m_window == window && "Input window bridge unbound with a different window" );

                                                                   if ( m_window == window )
                                                                   {
                                                                   m_window = nullptr;
                                                                   }
                                                                   }

                                                                   Runtime::Window* BoundWindow() const
                                                                   {
                                                                   return m_window;
                                                                   }

                                                                   private:

                                                                   // Lifetime: one process-local borrow follows the WinMain-owned Window
                                                                   // from startup binding through explicit pre-destruction unbinding.
                                                                   Runtime::Window* m_window = nullptr;
                                                                   };

                                                                   static WindowBridge s_windowBridge;

                                                                   // Hardware reads are private so steady runtime consumers cannot bypass the
                                                                   // one DeviceInputFrame capture performed at the frame boundary.
                                                                   static bool IsAppFocused();
                                                                   static bool ConsumeRawMouseDelta( long& xMove, long& yMove );
                                                                   static MouseCoordinatesResult GetMouseCoordinates( SkullbonezCore::Core::SbDiagnosticStore& diagnostics );
                                                                   static MouseCoordinatesResult GetClientMouseCoordinates( SkullbonezCore::Core::SbDiagnosticStore& diagnostics );
                                                                   static int ConsumeMouseWheelDelta();
                                                                   };
                                                                   } // namespace Hardware
                                                                   } // namespace SkullbonezCore
