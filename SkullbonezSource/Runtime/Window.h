/*
File: SkullbonezSource/Runtime/Window.h
Purpose:
  Creates and owns the Win32 window and message pump integration.

Mental model:
  Runtime code connects authored scene data, input, simulation, render
  backends, and validation-oriented launch modes. Follow who owns state and
  when that state changes.

Glossary:
  HWND (Window Handle): Win32 identifier for the native application window.
  HDC (Handle to Device Context): Win32 drawing context associated with the
  window.
  Projection frustum: Camera depth range used when rebuilding the perspective
  matrix after a client-size change.
  Validation gate: Repository script that proves a class of changes before
  commit or PR.

Invariants:
  - m_sWindowDimensions stores client width/height, not monitor or full window
    bounds.
  - projectionMatrix must be rebuilt from cached depth settings whenever the
    client size changes.

Related:
  - SkullbonezSource/Runtime/Window.cpp
  - Agentic/Reference/runtime-reference.md
  - Agentic/Reference/comment-style-guide.md
*/
#pragma once


#include "../Core/Common.h"
#include "../Maths/Matrix4.h"

namespace SkullbonezCore
{
namespace Basics
{
/* -- Skullbonez Window
------------------------------------------------------------------------------------------------------------------------------------------

    A singleton class representing a Windows OS application window.
-----------------------------------------------------------------------------------------------------------------------------------------------------------------*/
class Window
{

  private:
    inline static Window* pInstance = nullptr;

    float m_projectionNearPlane;                                 // Near depth plane used by the cached perspective projection.
    float m_projectionFarPlane;                                  // Far depth plane used by the cached perspective projection.
    int m_startupWindowWidth = 1800;                             // Configured initial window width supplied by startup.
    int m_startupWindowHeight = 1000;                            // Configured initial window height supplied by startup.

    Window();                                                    // Private singleton construction; use Instance().
    ~Window();                                                   // Static singleton lifetime; destructor currently has no native teardown.

  public:
    HWND m_sWindow;                                              // Native Win32 window handle used by renderer and input code.
    HDC m_sDevice;                                               // Native device context paired with m_sWindow.
    POINT m_sWindowDimensions;                                   // Client width/height cached for projection and recentering.
    bool m_fIsFullScreenMode;                                    // Window-mode policy chosen at creation time.

    Math::Transformation::Matrix4 projectionMatrix;              // Perspective projection rebuilt after client-size changes.

    static Window* Instance();                                   // Lazy singleton access for legacy runtime systems.
    static void Destroy();                                       // Clears the singleton pointer; static Window storage remains alive.
    void HandleScreenResize();                                   // Resize the active renderer and projection when the client area changes
    void SetTitleText( const char* cText );                      // Updates the native title bar without touching renderer text.
    void SetProjectionFrustum( float nearPlane,
                               float farPlane );                 // Stores projection depth planes used by later resize messages.
    void SetStartupWindowSize( int width, int height );          // Stores config-owned initial window/fullscreen dimensions.
    const Math::Transformation::Matrix4& GetProjectionMatrix() const
    {
        return projectionMatrix;
    } // Projection matrix currently used by render passes.
    void SetWindowDimensions( const RECT dimensions );           // Caches dimensions from a Win32 RECT.
    void SetWindowDimensions( int width, int height );           // Caches dimensions from explicit client width/height.
    void CreateAppWindow( HINSTANCE hInstance,
                          bool isFullScreenMode );               // Creates the native window and stores the HWND/HDC pair.
    void ChangeToFullScreen( int xResolution, int yResolution ); // Applies fullscreen display mode dimensions.
    int MsgBox( const char* cMsgBoxText,
                const char* cMsgBoxTitle,
                const UINT iMsgBoxType );                        // Native modal message box for startup/validation failures.
};
} // namespace Basics
} // namespace SkullbonezCore
