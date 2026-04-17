/*------------------------------------------------------------------------------------------------------------------------------------------------------------------------
                                                                          THE SKULLBONEZ CORE
                                                                                _______
                                                                             .-"       "-.
                                                                            /             \
                                                                           /               \
                                                                           |   .--. .--.   |
                                                                           | )/   | |   \( |
                                                                           |/ \__/   \__/ \|
                                                                           /      /^\      \
                                                                           \__    '='    __/
                                                                             |\         /|
                                                                             |\'"VUUUV"'/|
                                                                             \ `"""""""` /
                                                                              `-._____.-'

                                                                         www.simoneschbach.com
-------------------------------------------------------------------------------------------------------------------------------------------------------------------------*/

/* -- INCLUDE GUARDS ----------------------------------------------------------------------------------------------------------------------------------------------------*/
#ifndef SKULLBONEZ_WINDOW_H
#define SKULLBONEZ_WINDOW_H

/* -- INCLUDES ----------------------------------------------------------------------------------------------------------------------------------------------------------*/
#include "SkullbonezCommon.h"
#include "SkullbonezMatrix4.h"

namespace SkullbonezCore
{
namespace Basics
{
/* -- Skullbonez Window ------------------------------------------------------------------------------------------------------------------------------------------

    A singleton class representing a Windows OS application window.
-----------------------------------------------------------------------------------------------------------------------------------------------------------------*/
class SkullbonezWindow
{

  private:
    static SkullbonezWindow* pInstance; // Pointer to singleton instance

    SkullbonezWindow( void );  // Default constructor
    ~SkullbonezWindow( void ); // Default destructor

  public:
    HWND m_sWindow;            // Handle to window
    HDC m_sDevice;             // Handle to device context
    HGLRC m_sRenderContext;    // Handle to rendering context
    POINT m_sWindowDimensions; // Window m_width and m_height
    bool m_fIsFullScreenMode;  // Flag for fullscreen mode

    Math::Transformation::Matrix4 projectionMatrix; // Current perspective projection matrix

    static SkullbonezWindow* Instance( void ); // Call to request a pointer to the singleton instance
    static void Destroy( void );               // Call to destroy the singleton instance
    void HandleScreenResize( void );           // Reset OpenGL drawing boundaries and aspect ratio when the screen is resized
    bool SetupPixelFormat( void );             // Prepares pixel format of back and front buffer
    void InitialiseOpenGL( void );             // For all OpenGL API initialisation code (after the window has been created)
    void SetTitleText( const char* cText );    // Draws text to title bar of window
    const Math::Transformation::Matrix4& GetProjectionMatrix( void ) const
    {
        return projectionMatrix;
    } // Returns the current perspective projection matrix
    void SetWindowDimensions( const RECT dimensions );                                       // Sets window dimensions by RECT struct
    void SetWindowDimensions( int width, int height );                                       // Sets window dimensions by integer values
    void CreateAppWindow( HINSTANCE hInstance, bool isFullScreenMode );                      // Creates our application window, returns a handle to it
    void ChangeToFullScreen( int xResolution, int yResolution );                             // Changes screeen to full screen mode
    int MsgBox( const char* cMsgBoxText, const char* cMsgBoxTitle, const UINT iMsgBoxType ); // Draws a message box to the screen
};
} // namespace Basics
} // namespace SkullbonezCore

#endif /*----------------------------------------------------------------------------------------------------------------------------------------------------------------*/