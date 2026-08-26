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
  - Raw mouse device registration either succeeds before renderer startup or
    returns a diagnostic that aborts process initialization.

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
        bool middleMouseDown = false;
        int mouseWheelDelta = 0;                                                                                        // One-frame wheel delta routed through the normal device snapshot.
        long rawMouseDeltaX = 0;                                                                                        // Synthetic relative camera-look movement; never viewport-scaled.
        long rawMouseDeltaY = 0;
        int keyVirtualKey = 0;                                                                                          // Optional one-key automation override.
        bool keyDown = false;
        bool controlDown = false;                                                                                       // Optional modifier paired with the injected key.
        std::array<uint64_t, 4> keyWords = {};                                                                          // Complete synthetic key snapshot; physical keys are ignored while enabled.
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

    static void BindNativeWindow( HWND window );                                                                        // Binds the detached native handle used by frame capture.
    static void UnbindNativeWindow( HWND window );                                                                      // Clears the polling handle before HWND teardown.
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
                                                                   [[nodiscard]] static SkullbonezCore::Core::SbResult
                                                                   RegisterRawMouseInput( SkullbonezCore::Core::SbDiagnosticStore& diagnostics,
                                                                   HWND window );                                      // Registers relative mouse messages or returns a startup failure.
                                                                   static void
                                                                   AccumulateRawMouseSample( HWND window, long x, long y, bool absolute,
                                                                   bool virtualDesktop );                               // Adds a copied WM_INPUT sample when the callback bridge is bound.
                                                                   static void ResetMouseLookDeltas();                  // Clears queued raw mouse movement and absolute tracking state
                                                                   static void AccumulateMouseWheelDelta( HWND window,
                                                                   int delta );                                         // Adds a Win32 wheel delta when the callback bridge is bound.
                                                                   static void SetAutomationState( const AutomationState& state );
                                                                   static void ClearAutomationState();

                                                                   private:
                                                                   friend struct InputWindowBridgeTestAccess;

                                                                   using RawMouseRegistrationOperation = DWORD ( * )(
                                                                   const RAWINPUTDEVICE& device, void* context ) noexcept;
                                                                   static SkullbonezCore::Core::SbResult RegisterRawMouseInputWithOperation(
                                                                   SkullbonezCore::Core::SbDiagnosticStore& diagnostics, HWND window,
                                                                   bool callbackBridgeBound, RawMouseRegistrationOperation operation,
                                                                   void* context );

                                                                   class NativeWindowBinding
                                                                   {
                                                                   public:
                                                                   void Bind( HWND window )
                                                                   {
                                                                   assert( window && !m_window && "Input native window is already bound" );

                                                                   if ( window && !m_window )
                                                                   {
                                                                   m_window = window;
                                                                   }
                                                                   }

                                                                   void Unbind( HWND window )
                                                                   {
                                                                   assert( m_window == window && "Input native window unbound with a different HWND" );

                                                                   if ( m_window == window )
                                                                   {
                                                                   m_window = nullptr;
                                                                   }
                                                                   }

                                                                   HWND BoundHandle() const
                                                                   {
                                                                   return m_window;
                                                                   }

                                                                   private:

                                                                   // Invariant: one detached HWND identity is active from startup bind
                                                                   // through explicit pre-destruction unbind.
                                                                   HWND m_window = nullptr;
                                                                   };

                                                                   static NativeWindowBinding s_nativeWindow;

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
