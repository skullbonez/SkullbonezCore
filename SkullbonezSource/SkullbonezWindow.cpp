// --- Includes ---
#include "SkullbonezWindow.h"
#include "SkullbonezIRenderBackend.h"
#include "SkullbonezText.h"


// --- Usings ---
using namespace SkullbonezCore::Basics;
using namespace SkullbonezCore::Rendering;


SkullbonezWindow::SkullbonezWindow()
{
    m_sWindow = 0;
    m_sDevice = 0;
    m_sRenderContext = 0;
}


SkullbonezWindow::~SkullbonezWindow()
{
}


SkullbonezWindow* SkullbonezWindow::Instance()
{
    if ( !SkullbonezWindow::pInstance )
    {
        static SkullbonezWindow instance;
        SkullbonezWindow::pInstance = &instance;
    }
    return SkullbonezWindow::pInstance;
}


void SkullbonezWindow::Destroy()
{
    SkullbonezWindow::pInstance = nullptr;
}


void SkullbonezWindow::SetWindowDimensions( int m_width, int m_height )
{
    m_sWindowDimensions.x = m_width;
    m_sWindowDimensions.y = m_height;
}


void SkullbonezWindow::SetWindowDimensions( const RECT dimensions )
{
    m_sWindowDimensions.x = dimensions.right;
    m_sWindowDimensions.y = dimensions.bottom;
}


void SkullbonezWindow::HandleScreenResize()
{
    SkullbonezWindow* cWindow = SkullbonezWindow::Instance();
    int w = cWindow->m_sWindowDimensions.x;
    int h = cWindow->m_sWindowDimensions.y;

    // Skip resize when minimized or before backend is initialized
    if ( w <= 0 || h <= 0 || !IsGfxReady() )
    {
        return;
    }

    Gfx().Resize( w, h );

    // Recompute the 2D text ortho projection to match the new aspect ratio.
    // Without this, text stretches when the window is resized or maximized.
    Text::Text2d::RebuildProjection( w, h );

    // Build projection matrix with the correct depth range for the active backend
    float aspect = static_cast<float>( w ) / static_cast<float>( h );
    if ( Gfx().UsesZeroToOneDepth() )
    {
        cWindow->projectionMatrix = Math::Transformation::Matrix4::PerspectiveZeroToOne(
            45.0f,
            aspect,
            Cfg().frustumNear,
            Cfg().frustumFar );
    }
    else
    {
        cWindow->projectionMatrix = Math::Transformation::Matrix4::Perspective(
            45.0f,
            aspect,
            Cfg().frustumNear,
            Cfg().frustumFar );
    }
}


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
        MsgBox( "Could Not Enumerate Display Settings", "Error", MB_OK );
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
        MsgBox( "Display Mode Not Compatible", "Error", MB_OK );
        PostQuitMessage( 0 );
    }
}


bool SkullbonezWindow::SetupPixelFormat()
{
    // --- Pixel Format Concept ---
    // Before creating an OpenGL context, we must tell Windows what kind of framebuffer
    // we want: color depth, double buffering, depth buffer size, etc. This is called the
    // "pixel format" and it's configured via the PIXELFORMATDESCRIPTOR.
    //
    // Think of it as ordering a screen setup:
    //   "I want RGBA color, 32-bit depth, double buffering, and OpenGL support please."
    // Windows then finds the closest matching format the GPU supports.

    SkullbonezWindow* m_cWindow = SkullbonezWindow::Instance();
    PIXELFORMATDESCRIPTOR pfd = { 0 };
    int pixelFormat = 0;

    pfd.nSize = sizeof( PIXELFORMATDESCRIPTOR );
    pfd.nVersion = 1;
    // PFD_DRAW_TO_WINDOW = render to a window (not a bitmap)
    // PFD_SUPPORT_OPENGL = this is for OpenGL (not GDI or DirectX)
    // PFD_DOUBLEBUFFER   = use two buffers (draw to back, display front, swap)
    pfd.dwFlags = PFD_DRAW_TO_WINDOW |
                  PFD_SUPPORT_OPENGL |
                  PFD_DOUBLEBUFFER;
    pfd.dwLayerMask = PFD_MAIN_PLANE;
    pfd.iPixelType = PFD_TYPE_RGBA; // RGBA color mode (not palette/indexed)
    pfd.cColorBits = static_cast<BYTE>( Cfg().bitsPerPixel );
    pfd.cDepthBits = static_cast<BYTE>( Cfg().bitsPerPixel );
    pfd.cAccumBits = 0;
    pfd.cStencilBits = 0;

    // Ask Windows to find a pixel format matching our requirements.
    // Docs: https://learn.microsoft.com/en-us/windows/win32/api/wingdi/nf-wingdi-choosepixelformat
    pixelFormat = ChoosePixelFormat( m_cWindow->m_sDevice, &pfd );

    if ( !pixelFormat )
    {
        MsgBox( "ChoosePixelFormat failed", "Error", MB_OK );
        return false;
    }

    // Apply the chosen pixel format to our window's device context.
    // This must be done BEFORE creating an OpenGL context.
    // Docs: https://learn.microsoft.com/en-us/windows/win32/api/wingdi/nf-wingdi-setpixelformat
    if ( !SetPixelFormat( m_cWindow->m_sDevice, pixelFormat, &pfd ) )
    {
        MsgBox( "SetPixelFormat failed", "Error", MB_OK );
        return false;
    }

    return true;
}


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


// GLAD needs a loader that returns GL function pointers. For core GL 1.1
// functions Windows exposes them directly from opengl32.dll; everything
// else (modern functions like glCreateShader, glBindFramebuffer, etc.) comes
// from wglGetProcAddress which queries the GPU driver at runtime.
//
// This two-step lookup exists because Windows ships with a minimal opengl32.dll
// that only implements GL 1.1 — the driver provides everything else via ICD (Installable
// Client Driver) extensions. wglGetProcAddress returns NULL/0x1/0x2/0x3 for functions
// it doesn't know, so we fall back to the DLL for those.
static GLADapiproc GladLoadFunc( const char* name )
{
    // Try the driver's extension mechanism first (modern GL 1.2+ functions).
    // Docs: https://learn.microsoft.com/en-us/windows/win32/api/wingdi/nf-wingdi-wglgetprocaddress
    GLADapiproc p = reinterpret_cast<GLADapiproc>( wglGetProcAddress( name ) );
    if ( p == 0 || p == reinterpret_cast<GLADapiproc>( 0x1 ) ||
         p == reinterpret_cast<GLADapiproc>( 0x2 ) ||
         p == reinterpret_cast<GLADapiproc>( 0x3 ) ||
         p == reinterpret_cast<GLADapiproc>( -1 ) )
    {
        // Fall back to opengl32.dll for base GL 1.1 functions (glGetString, glEnable, etc.).
        static HMODULE gl = GetModuleHandleA( "opengl32.dll" );
        p = reinterpret_cast<GLADapiproc>( GetProcAddress( gl, name ) );
    }
    return p;
}


void SkullbonezWindow::InitialiseOpenGL()
{
    // --- OpenGL Context Creation ---
    // Before we can make any OpenGL calls, we need an "OpenGL context" — a connection
    // between our window and the GPU driver. The process on Windows is a bit convoluted:
    //
    //  1. Create a "dummy" legacy OpenGL context (needed to load modern functions)
    //  2. Use the dummy context to load wglCreateContextAttribsARB
    //  3. Create the REAL modern OpenGL 3.3 Core context
    //  4. Delete the dummy context
    //  5. Load all GL function pointers via GLAD
    //
    // This bootstrap dance exists because Windows only natively supports OpenGL 1.1 —
    // everything modern requires extensions that can only be queried from an existing context.

    SkullbonezWindow* m_cWindow = SkullbonezWindow::Instance();

    if ( !SetupPixelFormat() )
    {
        PostQuitMessage( 0 );
    }

    // Step 1: Create a temporary legacy OpenGL context. This gives us basic GL 1.1 so we
    // can query for the modern context-creation extension.
    // Docs: https://learn.microsoft.com/en-us/windows/win32/api/wingdi/nf-wingdi-wglcreatecontext
    HGLRC tempContext = wglCreateContext( m_cWindow->m_sDevice );

    // Make the temp context "current" (active) on this thread. OpenGL is thread-local —
    // only one context per thread can be current at a time.
    // Docs: https://learn.microsoft.com/en-us/windows/win32/api/wingdi/nf-wingdi-wglmakecurrent
    wglMakeCurrent( m_cWindow->m_sDevice, tempContext );

    // Step 2: Load the modern context-creation function from the driver.
    // wglGetProcAddress only works when a context is current — that's why we needed the temp.
    // Docs: https://learn.microsoft.com/en-us/windows/win32/api/wingdi/nf-wingdi-wglgetprocaddress
    using PFNWGLCREATECONTEXTATTRIBSARBPROC = HGLRC( WINAPI* )( HDC, HGLRC, const int* );
    auto wglCreateContextAttribsARB = reinterpret_cast<PFNWGLCREATECONTEXTATTRIBSARBPROC>(
        wglGetProcAddress( "wglCreateContextAttribsARB" ) );

    if ( wglCreateContextAttribsARB )
    {
        // Step 3: Request an OpenGL 3.3 Core Profile context.
        // Core Profile = modern OpenGL only, no deprecated functions (immediate mode, etc.)
        // This is what enables us to use shaders, VAOs, and all modern features.
        const int attribs[] = {
            0x2091,
            3, // WGL_CONTEXT_MAJOR_VERSION_ARB = 3
            0x2092,
            3, // WGL_CONTEXT_MINOR_VERSION_ARB = 3
            0x9126,
            0x1, // WGL_CONTEXT_PROFILE_MASK_ARB = CORE (0x1)
            0    // terminator
        };

        // Docs: https://registry.khronos.org/OpenGL/extensions/ARB/WGL_ARB_create_context.txt
        HGLRC modernContext = wglCreateContextAttribsARB( m_cWindow->m_sDevice, 0, attribs );
        if ( modernContext )
        {
            // Step 4: Destroy the temp context and switch to the modern one.
            wglMakeCurrent( nullptr, nullptr );
            // Docs: https://learn.microsoft.com/en-us/windows/win32/api/wingdi/nf-wingdi-wgldeletecontext
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

    // Step 5: Load all GL function pointers via GLAD.
    // OpenGL functions are not directly linked — they're loaded at runtime from the driver DLL.
    // GLAD handles this by calling our GladLoadFunc for each of the ~500+ GL functions.
    int gladVersion = gladLoadGL( GladLoadFunc );
    if ( !gladVersion )
    {
        MsgBox( "gladLoadGL returned 0 - GL function loading failed", "GLAD Error", MB_OK );
        PostQuitMessage( 0 );
        return;
    }

    // Verify we got a Core Profile context (not Compatibility — which allows deprecated calls).
    // Docs: https://registry.khronos.org/OpenGL-Refpages/gl4/html/glGet.xhtml
    GLint profileMask = 0;
    glGetIntegerv( GL_CONTEXT_PROFILE_MASK, &profileMask );
    if ( !( profileMask & GL_CONTEXT_CORE_PROFILE_BIT ) )
    {
        MsgBox( "OpenGL core profile context required but not active", "GL Context Error", MB_OK );
        PostQuitMessage( 0 );
        return;
    }

    // V-Sync policy is owned by the render backend so scene/config overrides apply consistently
    // across OpenGL and DirectX implementations.

    // Set window dimensions (HandleScreenResize is called from WinMain after SetGfxBackend)
    RECT windowDimensions;
    GetClientRect( m_cWindow->m_sWindow, &windowDimensions );
    m_cWindow->SetWindowDimensions( windowDimensions );
}


void SkullbonezWindow::CreateAppWindow( HINSTANCE hInstance, bool isFullScreenMode )
{
    HWND hWnd = nullptr;       // Handle to our window
    WNDCLASS wndclass = { 0 }; // Window class struct
    DWORD dwStyle = 0;         // Window style

    wndclass.style = CS_HREDRAW | CS_VREDRAW;            // Vert and Horiz redraw
    wndclass.lpfnWndProc = WndProc;                      // Assign callback function
    wndclass.hInstance = hInstance;                      // Assign hInstance
    wndclass.hIcon = LoadIcon( nullptr, IDI_WINLOGO );   // Default icon
    wndclass.hCursor = LoadCursor( nullptr, IDC_ARROW ); // Arrow cursor
    wndclass.hbrBackground =
        reinterpret_cast<HBRUSH>( GetStockObject( WHITE_BRUSH ) ); // White client background
    wndclass.lpszClassName = WINDOW_NAME;                          // Assign class name

    RegisterClass( &wndclass ); // Register class with OS

    // Set full screen mode flag member
    m_fIsFullScreenMode = isFullScreenMode;

    if ( m_fIsFullScreenMode )
    {
        // Set window properties for full screen mode
        dwStyle = WS_POPUP | WS_CLIPSIBLINGS | WS_CLIPCHILDREN;

        // Changes to full screen mode
        ChangeToFullScreen( Cfg().screenX, Cfg().screenY );

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
                         nullptr,   // Parent window handle
                         nullptr,   // Window menu handle
                         hInstance, // Application instance
                         nullptr ); // Data to pass to WndProc

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


void SkullbonezWindow::SetTitleText( const char* cText )
{
    // set the window title text
    SetWindowText( m_sWindow, cText );
}


int SkullbonezWindow::MsgBox( const char* cMsgBoxText,
                              const char* cMsgBoxTitle,
                              const UINT iMsgBoxType )
{
    return ( MessageBox( m_sWindow, cMsgBoxText, cMsgBoxTitle, iMsgBoxType ) );
}
