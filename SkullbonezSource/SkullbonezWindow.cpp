/* -- INCLUDES --------------------------------------------------------------------*/
#include "SkullbonezWindow.h"

/* -- USING CLAUSES ---------------------------------------------------------------*/
using namespace SkullbonezCore::Basics;

/* -- SINGLETON INSTANCE INITIALISATION -------------------------------------------*/
SkullbonezWindow* SkullbonezWindow::pInstance = 0;

/* -- DEFAULT CONSTRUCTOR ---------------------------------------------------------*/
SkullbonezWindow::SkullbonezWindow()
{
    m_sWindow = 0;
    m_sDevice = 0;
    m_sRenderContext = 0;
}

/* -- DEFAULT DESTRUCTOR ----------------------------------------------------------*/
SkullbonezWindow::~SkullbonezWindow()
{
}

/* -- SINGLETON CONSTRUCTOR -------------------------------------------------------*/
SkullbonezWindow* SkullbonezWindow::Instance()
{
    if ( !SkullbonezWindow::pInstance )
    {
        static SkullbonezWindow instance;
        SkullbonezWindow::pInstance = &instance;
    }
    return SkullbonezWindow::pInstance;
}

/* -- SINGLETON DESTRUCTOR --------------------------------------------------------*/
void SkullbonezWindow::Destroy()
{
    SkullbonezWindow::pInstance = 0;
}

/* -- SET WINDOW DIMENSIONS -------------------------------------------------------*/
void SkullbonezWindow::SetWindowDimensions( int m_width, int m_height )
{
    m_sWindowDimensions.x = m_width;
    m_sWindowDimensions.y = m_height;
}

/* -- SET WINDOW DIMENSIONS -------------------------------------------------------*/
void SkullbonezWindow::SetWindowDimensions( const RECT dimensions )
{
    m_sWindowDimensions.x = dimensions.right;
    m_sWindowDimensions.y = dimensions.bottom;
}

/* -- HANDLE SCREEN RESIZE --------------------------------------------------------*/
void SkullbonezWindow::HandleScreenResize()
{
    // Guard: skip GL calls if GLAD hasn't loaded function pointers yet.
    // WM_SIZE fires during CreateAppWindow before InitialiseOpenGL.
    if ( !glViewport )
    {
        return;
    }

    // Create an instance of our window class
    SkullbonezWindow* m_cWindow = SkullbonezWindow::Instance();

    // Set m_width and m_height to local variables
    int m_width = m_cWindow->m_sWindowDimensions.x;
    int m_height = m_cWindow->m_sWindowDimensions.y;

    if ( !m_height )
    {
        m_height = 1; // Avoid division by zero
    }
    glViewport( 0, 0, m_width, m_height ); // Set viewport (screen m_boundaries)

    // Build and store the perspective projection matrix
    float aspect = static_cast<float>( m_width ) / static_cast<float>( m_height );
    m_cWindow->projectionMatrix = Math::Transformation::Matrix4::Perspective(
        45.0f,
        aspect,
        Cfg().frustumNear,
        Cfg().frustumFar );
}

/* -- CHANGE TO FULLSCREEN --------------------------------------------------------*/
void SkullbonezWindow::ChangeToFullScreen( int xResolution, int yResolution )
{
    /*
        Changes screeen to full screen mode...
        Notes for this method:
        ---------------------------------------------------------------------------
        *** This can be quite helpful in some scenarios: ***

        dmSettings.dmBitsPerPel = BITS_PER_PIXEL;		// Set bits per pixel
        dmSettings.dmDisplayFrequency = REFRESH_RATE;	// Set refresh rate
        ...
        DM_BITSPERPEL							// We changed bits per pixel
        DM_DISPLAYFREQUENCY;					// We changed display frequency
        ---------------------------------------------------------------------------
        *** This code is redundant as DEVMODE struct was set to {0}: ***

        memset(&dmSettings,		 // Beginning at the memory location of dmSettings
            0,					 // set to zero
            sizeof(dmSettings)); // the entire DEVMODE struct
        ---------------------------------------------------------------------------
    */

    DEVMODE dmSettings = { 0 }; // Device mode variable - required to change modes

    if ( !EnumDisplaySettings( nullptr, // Get current screen settings
                               ENUM_CURRENT_SETTINGS,
                               &dmSettings ) )
    {
        this->MsgBox( "Could Not Enumerate Display Settings", "Error", MB_OK );
        PostQuitMessage( 0 );
    }

    dmSettings.dmPelsWidth = xResolution;  // Set new m_width
    dmSettings.dmPelsHeight = yResolution; // Set new m_height

    // Specifiy what we have changed
    dmSettings.dmFields = DM_PELSWIDTH | // We changed m_width
                          DM_PELSHEIGHT; // We changed m_height

    // Save result of our change
    int result = ChangeDisplaySettings( &dmSettings,      // Change to this struct
                                        CDS_FULLSCREEN ); // Remove start bar

    // If we failed, quit
    if ( result != DISP_CHANGE_SUCCESSFUL )
    {
        this->MsgBox( "Display Mode Not Compatible", "Error", MB_OK );
        PostQuitMessage( 0 );
    }
}

/* -- SETUP PIXEL FORMAT ----------------------------------------------------------*/
bool SkullbonezWindow::SetupPixelFormat()
{
    // Create an instance of our window class
    SkullbonezWindow* m_cWindow = SkullbonezWindow::Instance();
    PIXELFORMATDESCRIPTOR pfd = { 0 }; // Declare struct to describe out pixel format
    int pixelFormat = 0;                  // To hold our bits per pixel information

    // We now fill the PIXELFORMATDESCRIPTOR struct with what we want
    pfd.nSize = sizeof( PIXELFORMATDESCRIPTOR ); // Set size of struct
    pfd.nVersion = 1;                            // Always should be 1
    pfd.dwFlags = PFD_DRAW_TO_WINDOW |           // Set flags we want
                  PFD_SUPPORT_OPENGL |           // being window drawing, openGL
                  PFD_DOUBLEBUFFER;              // support + two buffers
    pfd.dwLayerMask = PFD_MAIN_PLANE;            // Standard mask
    pfd.iPixelType = PFD_TYPE_RGBA;              // Red Green Blue Alpha pixels
    pfd.cColorBits = static_cast<BYTE>( Cfg().bitsPerPixel );
    pfd.cDepthBits = static_cast<BYTE>( Cfg().bitsPerPixel );
    pfd.cAccumBits = 0;   // No accumulation bits
    pfd.cStencilBits = 0; // No stencil bits

    // Gets a pixel format that best matches what we have requested
    pixelFormat = ChoosePixelFormat( m_cWindow->m_sDevice, &pfd );

    // If pixel format was not set from the previous line, fail
    if ( !pixelFormat )
    {
        this->MsgBox( "ChoosePixelFormat failed", "Error", MB_OK );
        return false;
    }

    // If SetPixelFormat fails, we fail too
    if ( !SetPixelFormat( m_cWindow->m_sDevice, pixelFormat, &pfd ) )
    {
        this->MsgBox( "SetPixelFormat failed", "Error", MB_OK );
        return false;
    }

    return true; // If we have made it down to here we have succeeded
}

/* -- WndProc FUNCTION ------------------------------------------------------------*/
// "Windows Procedure" - this function handles messages for our window
LRESULT CALLBACK WndProc( HWND hWnd, UINT iMsg, WPARAM wParam, LPARAM lParam )
{
    // Create an instance of our window class
    SkullbonezWindow* m_cWindow = SkullbonezWindow::Instance();
    PAINTSTRUCT ps = { 0 }; // Assists with repainting the client area

    try
    {
        // Which message do we have to deal with today...?
        switch ( iMsg )
        {
        // WM_CREATE fired on window creation
        case WM_CREATE:
            break;

        // WM_SIZE fired on a resize
        case WM_SIZE:
            // LoWord = m_width, HiWord = m_height
            m_cWindow->SetWindowDimensions( LOWORD( lParam ), HIWORD( lParam ) );
            // Adjust OpenGL viewport matrix
            m_cWindow->HandleScreenResize();
            break;

        // WM_PAINT fired when client area is invalidated
        case WM_PAINT:
            BeginPaint( hWnd, &ps ); // Init paint struct
            EndPaint( hWnd, &ps );   // End painting
            break;

        case WM_KEYDOWN:
            // Quit if ESCAPE pressed
            if ( wParam == VK_ESCAPE )
            {
                PostQuitMessage( 0 );
            }
            break;

        // WM_DESTROY is fired when the window is closed
        case WM_DESTROY:
            // Close the window
            PostQuitMessage( 0 );
            break;
        }
    }
    catch ( const std::exception& e ) // Catch all exceptions thrown by the Skullbonez Core
    {
        m_cWindow->MsgBox( e.what(), "FATAL ERROR", MB_OK );
    }

    // Now we have done whatever we wanted to do, let windows do anything else it
    // needs to do based on the message fired...
    return DefWindowProc( hWnd, iMsg, wParam, lParam );
}

/* -- GLAD LOADER CALLBACK --------------------------------------------------------*/
// GLAD needs a loader that returns GL function pointers.  For core GL 1.1
// functions Windows exposes them directly from opengl32.dll; everything
// else comes from wglGetProcAddress.
static GLADapiproc GladLoadFunc( const char* name )
{
    GLADapiproc p = reinterpret_cast<GLADapiproc>( wglGetProcAddress( name ) );
    if ( p == 0 || p == reinterpret_cast<GLADapiproc>( 0x1 ) ||
         p == reinterpret_cast<GLADapiproc>( 0x2 ) ||
         p == reinterpret_cast<GLADapiproc>( 0x3 ) ||
         p == reinterpret_cast<GLADapiproc>( -1 ) )
    {
        static HMODULE gl = GetModuleHandleA( "opengl32.dll" );
        p = reinterpret_cast<GLADapiproc>( GetProcAddress( gl, name ) );
    }
    return p;
}

/* -- INITIALISE OPEN GL ----------------------------------------------------------*/
void SkullbonezWindow::InitialiseOpenGL()
{
    SkullbonezWindow* m_cWindow = SkullbonezWindow::Instance();

    if ( !SetupPixelFormat() )
    {
        PostQuitMessage( 0 );
    }

    // Bootstrap: create a temporary legacy context so we can load
    // wglCreateContextAttribsARB, then replace it with a 3.3 context.
    HGLRC tempContext = wglCreateContext( m_cWindow->m_sDevice );
    wglMakeCurrent( m_cWindow->m_sDevice, tempContext );

    // Resolve the modern context creation function
    using PFNWGLCREATECONTEXTATTRIBSARBPROC = HGLRC( WINAPI* )( HDC, HGLRC, const int* );
    auto wglCreateContextAttribsARB = reinterpret_cast<PFNWGLCREATECONTEXTATTRIBSARBPROC>(
        wglGetProcAddress( "wglCreateContextAttribsARB" ) );

    if ( wglCreateContextAttribsARB )
    {
        // Request OpenGL 3.3 Core Profile
        const int attribs[] = {
            0x2091,
            3, // WGL_CONTEXT_MAJOR_VERSION_ARB = 3
            0x2092,
            3, // WGL_CONTEXT_MINOR_VERSION_ARB = 3
            0x9126,
            0x1, // WGL_CONTEXT_PROFILE_MASK_ARB = CORE (0x1)
            0    // terminator
        };

        HGLRC modernContext = wglCreateContextAttribsARB( m_cWindow->m_sDevice, 0, attribs );
        if ( modernContext )
        {
            wglMakeCurrent( nullptr, nullptr );
            wglDeleteContext( tempContext );
            wglMakeCurrent( m_cWindow->m_sDevice, modernContext );
            m_cWindow->m_sRenderContext = modernContext;
        }
        else
        {
            // Fall back to legacy context if 3.3 is not available
            m_cWindow->m_sRenderContext = tempContext;
        }
    }
    else
    {
        m_cWindow->m_sRenderContext = tempContext;
    }

    // Load all GL function pointers via GLAD
    int gladVersion = gladLoadGL( GladLoadFunc );
    if ( !gladVersion )
    {
        this->MsgBox( "gladLoadGL returned 0 - GL function loading failed", "GLAD Error", MB_OK );
        PostQuitMessage( 0 );
        return;
    }

    // Verify we are on a core profile context (not compatibility)
    GLint profileMask = 0;
    glGetIntegerv( GL_CONTEXT_PROFILE_MASK, &profileMask );
    if ( !( profileMask & GL_CONTEXT_CORE_PROFILE_BIT ) )
    {
        this->MsgBox( "OpenGL core profile context required but not active", "GL Context Error", MB_OK );
        PostQuitMessage( 0 );
        return;
    }

    // Set window dimensions
    RECT windowDimensions;
    GetClientRect( m_cWindow->m_sWindow, &windowDimensions );
    m_cWindow->SetWindowDimensions( windowDimensions );

    HandleScreenResize();
}

/* -- CREATE APP WINDOW -----------------------------------------------------------*/
void SkullbonezWindow::CreateAppWindow( HINSTANCE hInstance, bool isFullScreenMode )
{
    HWND hWnd = nullptr;          // Handle to our window
    WNDCLASS wndclass = { 0 }; // Window class struct
    DWORD dwStyle = 0;            // Window style

    wndclass.style = CS_HREDRAW | CS_VREDRAW;         // Vert and Horiz redraw
    wndclass.lpfnWndProc = WndProc;                   // Assign callback function
    wndclass.hInstance = hInstance;                   // Assign hInstance
    wndclass.hIcon = LoadIcon( nullptr, IDI_WINLOGO );   // Default icon
    wndclass.hCursor = LoadCursor( nullptr, IDC_ARROW ); // Arrow cursor
    wndclass.hbrBackground =
        reinterpret_cast<HBRUSH>( GetStockObject( WHITE_BRUSH ) ); // White client background
    wndclass.lpszClassName = WINDOW_NAME;      // Assign class name

    RegisterClass( &wndclass ); // Register class with OS

    // Set full screen mode flag member
    m_fIsFullScreenMode = isFullScreenMode;

    if ( m_fIsFullScreenMode )
    {
        // Set window properties for full screen mode
        dwStyle = WS_POPUP | WS_CLIPSIBLINGS | WS_CLIPCHILDREN;

        // Changes to full screen mode
        this->ChangeToFullScreen( Cfg().screenX, Cfg().screenY );

        // Hide the mouse cursor
        ShowCursor( false );
    }
    else
    {
        // Set window properties for non full screen mode
        dwStyle = WS_OVERLAPPEDWINDOW | WS_CLIPSIBLINGS | WS_CLIPCHILDREN;
    }

    hWnd = CreateWindow( WINDOW_NAME, // Window class name
                         TITLE_TEXT,  // Window title text
                         dwStyle,     // Set defined style
                         0,           // Window xPos
                         0,           // Window yPos
                         Cfg().screenX,
                         Cfg().screenY,
                         nullptr,      // Parent window handle
                         nullptr,      // Window menu handle
                         hInstance, // Application instance
                         nullptr );    // Data to pass to WndProc

    if ( !hWnd )
    {
        throw std::runtime_error( "Window creation failed" ); // Throw exception on failure
    }
    ShowWindow( hWnd, SW_SHOWNORMAL ); // Show window
    UpdateWindow( hWnd );              // Draw window
    SetFocus( hWnd );                  // Set keyboard focus
                                       // to our window

    m_sWindow = hWnd;
}

/* -- SET TITLE TEXT --------------------------------------------------------------*/
void SkullbonezWindow::SetTitleText( const char* cText )
{
    // set the window title text
    SetWindowText( m_sWindow, cText );
}

/* -- MSG BOX ---------------------------------------------------------------------*/
int SkullbonezWindow::MsgBox( const char* cMsgBoxText,
                              const char* cMsgBoxTitle,
                              const UINT iMsgBoxType )
{
    return ( MessageBox( m_sWindow, cMsgBoxText, cMsgBoxTitle, iMsgBoxType ) );
}
