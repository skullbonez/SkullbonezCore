/*
File: SkullbonezSource/Window.h
Purpose:
  Creates and owns the Win32 window and message pump integration.

Mental model:
  Runtime code connects authored scene data, input, simulation, render
  backends, and validation-oriented launch modes. Follow who owns state and
  when that state changes.

Glossary:
  Validation gate: Repository script that proves a class of changes before
  commit or PR.

Related:
  - SkullbonezSource/Window.cpp
  - Agentic/Reference/runtime-reference.md
  - Agentic/Reference/comment-style-guide.md
*/
#pragma once


#include "Common.h"
#include "Matrix4.h"

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

    Window();                                                    // Default constructor
    ~Window();                                                   // Default destructor

  public:
    HWND m_sWindow;                                              // Handle to window
    HDC m_sDevice;                                               // Handle to device context
    POINT m_sWindowDimensions;                                   // Window m_width and m_height
    bool m_fIsFullScreenMode;                                    // Flag for fullscreen mode

    Math::Transformation::Matrix4 projectionMatrix;              // Current perspective projection matrix

    static Window* Instance();                                   // Call to request a pointer to the singleton instance
    static void Destroy();                                       // Call to destroy the singleton instance
    void HandleScreenResize();                                   // Resize the active renderer and projection when the client area changes
    void SetTitleText( const char* cText );                      // Draws text to title bar of window
    const Math::Transformation::Matrix4& GetProjectionMatrix() const
    {
        return projectionMatrix;
    } // Returns the current perspective projection matrix
    void SetWindowDimensions( const RECT dimensions );           // Sets window dimensions by RECT struct
    void SetWindowDimensions( int width, int height );           // Sets window dimensions by integer values
    void CreateAppWindow( HINSTANCE hInstance,
                          bool isFullScreenMode );               // Creates our application window, returns a handle to it
    void ChangeToFullScreen( int xResolution, int yResolution ); // Changes screeen to full screen mode
    int MsgBox( const char* cMsgBoxText,
                const char* cMsgBoxTitle,
                const UINT iMsgBoxType );                        // Draws a message box to the screen
};
} // namespace Basics
} // namespace SkullbonezCore
