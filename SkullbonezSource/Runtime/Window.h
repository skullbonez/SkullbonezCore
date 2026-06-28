/*
File: SkullbonezSource/Runtime/Window.h
Purpose:
  Creates and owns the Win32 window and message pump integration.

Mental model:
  Window owns native Win32 handles but borrows process configuration from Init.
  Resize callbacks can arrive through the OS, so callback-safe state must be
  bound before the HWND starts receiving messages.

Glossary:
  HWND (Window Handle): Win32 identifier for the native application window.
  HDC (Handle to Device Context): Win32 drawing context associated with the
  window.
  Resize backend: Borrowed renderer pointer used only while the backend is live
  so WM_SIZE can resize swap-chain resources without sampling the global facade.
  Validation gate: Repository script that proves a class of changes before
  commit or PR.

Invariants:
  - m_sWindowDimensions stores client width/height, not monitor or full window
    bounds.
  - projectionMatrix must be rebuilt whenever the client size changes.
  - m_config is borrowed from the process config singleton and must be bound
    before CreateWindow can dispatch resize messages.
  - m_resizeBackend is nullable during early window creation and after backend
    teardown; resize callbacks must no-op unless it is bound.

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
namespace Rendering
{
class IRenderBackend;
} // namespace Rendering
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

    Window();                                                    // Private singleton construction; use Instance().
    ~Window();                                                   // Static singleton lifetime; destructor currently has no native teardown.
    const EngineConfig& Config() const;                          // Live process config bound before HWND creation.

    const EngineConfig* m_config = nullptr;                      // Borrowed; owned by EngineConfig singleton loaded in Init.
    Rendering::IRenderBackend* m_resizeBackend = nullptr;        // Borrowed active renderer for WM_SIZE; cleared before backend teardown.

  public:
    HWND m_sWindow;                                              // Native Win32 window handle used by renderer and input code.
    HDC m_sDevice;                                               // Native device context paired with m_sWindow.
    POINT m_sWindowDimensions;                                   // Client width/height cached for projection and recentering.
    bool m_fIsFullScreenMode;                                    // Window-mode policy chosen at creation time.

    Math::Transformation::Matrix4 projectionMatrix;              // Perspective projection rebuilt after client-size changes.

    static Window* Instance();                                   // Lazy singleton access for legacy runtime systems.
    static void Destroy();                                       // Clears the singleton pointer; static Window storage remains alive.
    void BindResizeBackend( Rendering::IRenderBackend* backend ); // Arms or clears the renderer borrow used by resize callbacks.
    void HandleScreenResize();                                   // Resize the active renderer and projection when the client area changes
    void SetTitleText( const char* cText );                      // Updates the native title bar without touching renderer text.
    const Math::Transformation::Matrix4& GetProjectionMatrix() const
    {
        return projectionMatrix;
    } // Projection matrix currently used by render passes.
    void SetWindowDimensions( const RECT dimensions );           // Caches dimensions from a Win32 RECT.
    void SetWindowDimensions( int width, int height );           // Caches dimensions from explicit client width/height.
    void CreateAppWindow( HINSTANCE hInstance,
                          const EngineConfig& config );          // Creates the native window and stores the HWND/HDC pair.
    void ChangeToFullScreen( int xResolution, int yResolution ); // Applies fullscreen display mode dimensions.
    int MsgBox( const char* cMsgBoxText,
                const char* cMsgBoxTitle,
                const UINT iMsgBoxType );                        // Native modal message box for startup/validation failures.
};
} // namespace Basics
} // namespace SkullbonezCore
