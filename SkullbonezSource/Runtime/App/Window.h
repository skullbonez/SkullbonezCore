/*
File: SkullbonezSource/Runtime/App/Window.h
Purpose:
  Creates and owns the Win32 window and message pump integration.

Summary:
  Window.h creates and owns the Win32 window and message pump integration. As
  a public header, keep edits anchored on local owner boundaries and call
  direction and on the glossary/invariants below.

Glossary:
  HWND (Window Handle): Win32 identifier for the native application window.
  HDC (Handle to Device Context): Win32 drawing context associated with the
  window.
  Resize frame owner: Borrowed concrete owner used only to resize swap-chain and
    depth resources when Win32 reports a new client size.
  Lane R result: Recoverable renderer/window failure returned with an
    owner/message instead of throwing through WndProc.
  Projection frustum: Camera depth range used when rebuilding the perspective
  matrix after a client-size change.

Invariants:
  - Cached dimensions store client width/height, not monitor or full window
    bounds.
  - m_resizeRenderFrame is borrowed from startup and must be cleared before
    the render backend is destroyed.
  - projectionMatrix must be rebuilt from cached depth settings whenever the
    client size changes.

Related:
  - SkullbonezSource/Runtime/App/Window.cpp
  - Agentic/Reference/runtime-reference.md
  - Agentic/Reference/comment-style-guide.md
*/
#pragma once

#include "../../Core/PlatformWin32.h"


#include "../../Core/Common.h"
#include "../../Core/SbResult.h"
#include "../../Maths/Matrix4.h"

namespace SkullbonezCore
{
namespace Rendering
{
class Dx12FrameOwner;
}
namespace Runtime
{
#if defined( SKULLBONEZ_DEVELOPMENT_TOOLS )
namespace DevelopmentTools
{
class ImGuiEditorOwner;
struct ImGuiEditorNativeMessageRoute;
} // namespace DevelopmentTools
#endif
/* -- Skullbonez Window
------------------------------------------------------------------------------------------------------------------------------------------

    Startup-owned wrapper for the Win32 application window.
-----------------------------------------------------------------------------------------------------------------------------------------------------------------*/
class Window
{

  private:
    HWND m_sWindow;                                              // Native Win32 window handle owned by startup/window creation.
    HDC m_sDevice;                                               // Native device context paired with m_sWindow.
    POINT m_sWindowDimensions;                                   // Client width/height cached for projection and recentering.
    bool m_fIsFullScreenMode;                                    // Window-mode policy chosen at creation time.
    Math::Transformation::Matrix4 projectionMatrix;              // Perspective projection rebuilt after client-size changes.
    float m_projectionNearPlane;                                 // Near depth plane used by the cached perspective projection.
    float m_projectionFarPlane;                                  // Far depth plane used by the cached perspective projection.
    int m_startupWindowWidth = 1800;                             // Configured initial window width supplied by startup.
    int m_startupWindowHeight = 1000;                            // Configured initial window height supplied by startup.
    Rendering::Dx12FrameOwner* m_resizeRenderFrame = nullptr;    // Borrowed resize-only frame owner.
#if defined( SKULLBONEZ_DEVELOPMENT_TOOLS )
    DevelopmentTools::ImGuiEditorOwner*
        m_developmentUiInput = nullptr;                          // Borrowed native-message target bound for Run lifetime.
#endif

  public:
    Window();                                                    // Startup constructs the single runtime window owner.
    ~Window();                                                   // Native teardown is explicit in Runtime/App/Init.cpp cleanup.

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
    HDC AcquireDeviceContext();                                  // Caches GetDC() for startup render initialization.
    void ReleaseDeviceContext();                                 // Releases the cached HDC before native window teardown.
    SkullbonezCore::Core::SbResult HandleScreenResize();         // Resizes the renderer/projection or reports a Lane R resize
                                                         // failure.
    void SetTitleText( const char* cText );                      // Updates the native title bar without touching renderer text.
    void SetProjectionFrustum( float nearPlane,
                               float farPlane );                 // Stores projection depth planes used by later resize messages.
    void SetStartupWindowSize( int width, int height );          // Stores config-owned initial window/fullscreen dimensions.
    void SetResizeRenderFrameOwner(
        Rendering::Dx12FrameOwner* renderFrame );                // Borrows or clears the resize transaction owner.
#if defined( SKULLBONEZ_DEVELOPMENT_TOOLS )
    void BindDevelopmentUiInput( DevelopmentTools::ImGuiEditorOwner& owner );
    void UnbindDevelopmentUiInput( DevelopmentTools::ImGuiEditorOwner& owner );
    DevelopmentTools::ImGuiEditorNativeMessageRoute
    RouteDevelopmentUiMessage( HWND window, UINT message, WPARAM wParam, LPARAM lParam );
#endif
    const Math::Transformation::Matrix4& GetProjectionMatrix() const
    {
        return projectionMatrix;
    } // Projection matrix currently used by render passes.
    void SetWindowDimensions( const RECT dimensions );           // Caches dimensions from a Win32 RECT.
    void SetWindowDimensions( int width, int height );           // Caches dimensions from explicit client width/height.
    SkullbonezCore::Core::SbResult
    CreateAppWindow( HINSTANCE hInstance,
                     bool isFullScreenMode,
                     bool showOnCreate = true );                 // Creates the native window or reports Lane R startup failure.
    void ChangeToFullScreen( int xResolution, int yResolution ); // Applies fullscreen display mode dimensions.
    int MsgBox( const char* cMsgBoxText,
                const char* cMsgBoxTitle,
                const UINT iMsgBoxType );                        // Native modal message box for startup/validation failures.
};
} // namespace Runtime
} // namespace SkullbonezCore
