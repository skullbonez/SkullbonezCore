/*
File: SkullbonezSource/Runtime/Startup/Window.h
Purpose:
  Creates and owns the Win32 window and message pump integration.

Summary:
  Window owns native window/client-size state and turns Win32 callbacks into a
  bounded value queue. App synchronously applies those values to Input and the
  renderer after each dispatched native message.

Glossary:
  Native host event: Bounded value copied from one Win32 callback; it carries no
    subsystem pointer or callback.
  Projection frustum: Camera depth range used when rebuilding the perspective
  matrix after a client-size change.

Invariants:
  - Cached dimensions store client width/height, not monitor or full window
    bounds.
  - Native callbacks never call Runtime owners or retain subsystem pointers.
  - App drains events synchronously after each dispatched message, so raw-input
    values cannot outlive the native packet that produced them.
  - projectionMatrix must be rebuilt from cached depth settings whenever the
    client size changes.

Related:
  - SkullbonezSource/Runtime/Startup/Window.cpp
  - Agentic/Reference/runtime-reference.md
  - Agentic/Reference/engine-glossary.md
*/
#pragma once

#include "../../Core/PlatformWin32.h"


#include "../../Core/Common.h"
#include "../../Core/SbResult.h"
#include "../../Maths/Matrix4.h"

#include <array>
#include <cstddef>
#include <cstdint>

namespace SkullbonezCore
{
namespace Runtime
{
enum class NativeHostEventType : uint8_t
{
    Resize,
    MouseWheel,
    RawMouse
};

struct NativeHostEvent
{
    NativeHostEventType type = NativeHostEventType::Resize;
    HWND window = nullptr;
    int first = 0;
    int second = 0;
    bool absolute = false;
    bool virtualDesktop = false;
};

struct NativeHostMessage
{
    HWND window = nullptr;
    UINT id = 0;
    WPARAM wParam = 0;
    LPARAM lParam = 0;
    int exitCode = 0;
    bool quit = false;
};

struct NativeHostMessageRoute
{
    LRESULT capturedResult = 0;
    bool engineConsumes = true;
    bool engineCursorHandled = false;
};

class Window
{
    friend LRESULT CALLBACK WndProc( HWND windowHandle, UINT messageId, WPARAM wParam, LPARAM lParam );

  private:
    static constexpr std::size_t kEventCapacity = 256;

    SkullbonezCore::Core::SbDiagnosticStore& m_resultDiagnostics;                                                          // Process diagnostic lease owner borrowed for the window lifetime.
    HWND m_sWindow;                                                                                                        // Native Win32 window handle owned by startup/window creation.
    HDC m_sDevice;                                                                                                         // Native device context paired with m_sWindow.
    POINT m_sWindowDimensions;                                                                                             // Client width/height cached for projection and recentering.
    bool m_fIsFullScreenMode;                                                                                              // Window-mode policy chosen at creation time.
    Math::Transformation::Matrix4 projectionMatrix;                                                                        // Perspective projection rebuilt after client-size changes.
    float m_projectionNearPlane;                                                                                           // Near depth plane used by the cached perspective projection.
    float m_projectionFarPlane;                                                                                            // Far depth plane used by the cached perspective projection.
    int m_startupWindowWidth = 1800;                                                                                       // Configured initial window width supplied by startup.
    int m_startupWindowHeight = 1000;                                                                                      // Configured initial window height supplied by startup.
    std::array<NativeHostEvent, kEventCapacity> m_events = {};
    std::size_t m_eventRead = 0;
    std::size_t m_eventCount = 0;
    NativeHostMessageRoute m_activeRoute;
    bool m_dispatchActive = false;

  public:
    explicit Window( SkullbonezCore::Core::SbDiagnosticStore& resultDiagnostics );                                         // Startup constructs the single runtime window owner.
    ~Window();                                                                                                             // Native teardown is explicit in Runtime/App/Init.cpp cleanup.

    HWND NativeWindowHandle() const
    {
        return m_sWindow;
    } // Native Win32 handle for renderer/input API calls.
    HDC NativeDeviceContext() const
    {
        return m_sDevice;
    } // Native device context currently acquired for the window.
    POINT ClientDimensions() const
    {
        return m_sWindowDimensions;
    } // Snapshot of cached client-area width/height.
    int ClientWidth() const
    {
        return static_cast<int>( m_sWindowDimensions.x );
    } // Cached client-area width in pixels.
    int ClientHeight() const
    {
        return static_cast<int>( m_sWindowDimensions.y );
    } // Cached client-area height in pixels.
    bool IsFullScreenMode() const
    {
        return m_fIsFullScreenMode;
    } // True when CreateAppWindow selected fullscreen mode.
    HDC AcquireDeviceContext();                                                                                            // Caches GetDC() for startup render initialization.
    void ReleaseDeviceContext();                                                                                           // Releases the cached HDC before native window teardown.
    void UpdateProjectionForCurrentClient();                                                                               // Rebuilds the host projection after App accepts a nonzero resize.
    void SetTitleText( const char* text );                                                                                 // Updates the native title bar without touching renderer text.
    void SetProjectionFrustum( float nearPlane, float farPlane );                                                          // Stores projection depth planes used by later resize messages.
    void SetStartupWindowSize( int width, int height );                                                                    // Stores config-owned initial window/fullscreen dimensions.
    bool PeekNativeMessage( NativeHostMessage& message );                                                                  // Removes one thread message and returns a detached value.
    void DispatchNativeMessage( const NativeHostMessage& message, const NativeHostMessageRoute& route );                   // Dispatches one message with App's synchronous UI route.
    bool ConsumeNativeEvent( NativeHostEvent& event );                                                                     // Drains one callback-produced event in FIFO order.
    const Math::Transformation::Matrix4& GetProjectionMatrix() const
    {
        return projectionMatrix;
    } // Projection matrix currently used by render passes.
    SkullbonezCore::Core::SbResult CreateAppWindow( HINSTANCE instance, bool isFullScreenMode, bool showOnCreate = true ); // Creates the native window or report recoverable error startup failure.
    void ChangeToFullScreen( int xResolution, int yResolution );                                                           // Applies fullscreen display mode dimensions.
    int MsgBox( const char* text, const char* title, const UINT type );                                                    // Native modal message box for startup/validation failures.
};
} // namespace Runtime
} // namespace SkullbonezCore
