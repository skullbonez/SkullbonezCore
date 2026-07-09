/*
File: SkullbonezSource/Runtime/Window.cpp
Purpose:
  Creates and owns the Win32 window and message pump integration.

Mental model:
  CreateAppWindow establishes the native HWND, arms the input callback bridge,
  and then lets WndProc feed resize, focus, cursor, and raw mouse messages back
  into runtime-owned systems.

Glossary:
  HWND (Window Handle): Win32 identifier for the native application window.
  HDC (Handle to Device Context): Win32 drawing context associated with the
  window.
  Resize lifecycle: Borrowed renderer capability used only to resize swap-chain
  and depth resources when Win32 reports a new client size.
  WndProc: Win32 callback used by the OS to deliver window, focus, cursor, and
  input messages.
  Callback bridge: Input's bound HWND gate that keeps late or foreign window
  callbacks from mutating frame input queues.
  Lane R result: Recoverable renderer/window failure returned with an
    owner/message instead of throwing through WndProc.

Invariants:
  - Window dimensions are client-area dimensions and drive both renderer resize
    and the perspective/text projections.
  - The resize lifecycle borrow is installed after renderer startup and cleared
    before backend teardown; Window never owns the renderer.
  - The singleton pointer is a legacy access shim around static storage; native
    HWND/HDC lifetime still follows CreateAppWindow and OS messages.

Related:
  - SkullbonezSource/Runtime/Window.h
  - Agentic/Reference/runtime-reference.md
  - Agentic/Reference/comment-style-guide.md
*/
#include "Window.h"
#include "../Rendering/IRenderDeviceLifecycle.h"
#include "Input.h"
#include "../Core/Log.h"
#include "../Rendering/Text.h"

#include <algorithm>
#include <cstdio>


using namespace SkullbonezCore::Basics;
using namespace SkullbonezCore::Hardware;
using namespace SkullbonezCore::Rendering;


Window::Window()
{
    m_sWindow = 0;
    m_sDevice = 0;
    m_projectionNearPlane = 1.0f;
    m_projectionFarPlane = 5500.0f;
    m_startupWindowWidth = 1800;
    m_startupWindowHeight = 1000;
    m_resizeRenderLifecycle = nullptr;
}


Window::~Window()
{
}


void Window::SetWindowDimensions( int m_width, int m_height )
{
    m_sWindowDimensions.x = m_width;
    m_sWindowDimensions.y = m_height;
}


void Window::SetWindowDimensions( const RECT dimensions )
{
    m_sWindowDimensions.x = dimensions.right;
    m_sWindowDimensions.y = dimensions.bottom;
}


void Window::SetProjectionFrustum( float nearPlane, float farPlane )
{
    m_projectionNearPlane = nearPlane;
    m_projectionFarPlane = farPlane;
}


void Window::SetStartupWindowSize( int width, int height )
{
    m_startupWindowWidth = (std::max)( 1, width );
    m_startupWindowHeight = (std::max)( 1, height );
}


void Window::SetResizeRenderLifecycle( IRenderDeviceLifecycle* deviceLifecycle )
{
    m_resizeRenderLifecycle = deviceLifecycle;
}


SbResult Window::HandleScreenResize()
{
    int w = m_sWindowDimensions.x;
    int h = m_sWindowDimensions.y;

    // Hazard: minimized windows report zero client area; resizing the backend
    // to zero dimensions would invalidate swap-chain and projection state.
    if ( w <= 0 || h <= 0 || !m_resizeRenderLifecycle )
    {
        return SbResult::Success();
    }

    const SbResult resizeResult = m_resizeRenderLifecycle->Resize( w, h );
    if ( !resizeResult.ok )
    {
        return resizeResult;
    }

    // Recompute the 2D text ortho projection to match the new aspect ratio.
    // Without this, text stretches when the window is resized or maximized.
    Text::Text2d::RebuildProjection( w, h );

    // DX12 clip-space depth is [0,1], so the perspective matrix must use the
    // matching projection convention after every resize.
    // Invariant: Window owns the projection depth range after startup; resize
    // must not reopen global config while handling OS messages.
    float aspect = static_cast<float>( w ) / static_cast<float>( h );
    projectionMatrix = Math::Transformation::Matrix4::PerspectiveZeroToOne( 45.0f,
                                                                            aspect,
                                                                            m_projectionNearPlane,
                                                                            m_projectionFarPlane );
    return SbResult::Success();
}


void Window::ChangeToFullScreen( int xResolution, int yResolution )
{
    DEVMODE dmSettings = { 0 }; // Device mode variable - required to change modes

    if ( !EnumDisplaySettings( nullptr, ENUM_CURRENT_SETTINGS, &dmSettings ) )
    {
        MsgBox( "Could Not Enumerate Display Settings", "Error", MB_OK );
        PostQuitMessage( 0 );
    }

    dmSettings.dmPelsWidth = xResolution;
    dmSettings.dmPelsHeight = yResolution;

    // Specifiy what we have changed
    dmSettings.dmFields = DM_PELSWIDTH | // We changed m_width
                          DM_PELSHEIGHT; // We changed m_height

    int result = ChangeDisplaySettings( &dmSettings, CDS_FULLSCREEN );

    // If we failed, quit
    if ( result != DISP_CHANGE_SUCCESSFUL )
    {
        MsgBox( "Display Mode Not Compatible", "Error", MB_OK );
        PostQuitMessage( 0 );
    }
}


// "Windows Procedure" - this function handles messages for our window
LRESULT CALLBACK WndProc( HWND hWnd, UINT iMsg, WPARAM wParam, LPARAM lParam )
{
    PAINTSTRUCT ps = { 0 }; // Assists with repainting the client area
    Window* m_cWindow = reinterpret_cast<Window*>( GetWindowLongPtr( hWnd, GWLP_USERDATA ) );

    try
    {
        // Which message do we have to deal with today...?
        switch ( iMsg )
        {
        // WM_CREATE fired on window creation
        case WM_CREATE:
        {
            CREATESTRUCT* create = reinterpret_cast<CREATESTRUCT*>( lParam );
            m_cWindow = reinterpret_cast<Window*>( create->lpCreateParams );
            SetWindowLongPtr( hWnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>( m_cWindow ) );
            break;
        }

        // WM_SIZE fired on a resize
        case WM_SIZE:
            // LoWord = m_width, HiWord = m_height
            if ( m_cWindow )
            {
                m_cWindow->SetWindowDimensions( LOWORD( lParam ), HIWORD( lParam ) );
                const SbResult resizeResult = m_cWindow->HandleScreenResize();
                if ( !resizeResult.ok )
                {
                    const char* owner =
                        resizeResult.error.owner[0] != '\0' ? resizeResult.error.owner : "Runtime/Window";
                    const char* message =
                        resizeResult.error.message[0] != '\0' ? resizeResult.error.message : "window resize failed";
                    Log().WriteEventf( "window_resize_failed owner=\"%s\" message=\"%s\"", owner, message );
                    std::fprintf( stderr, "[window] Resize failed owner=%s reason=\"%s\"\n", owner, message );
                    std::fflush( stderr );
                    Log().FlushAll();
                    PostQuitMessage( 1 );
                }
            }
            break;

        // WM_PAINT fired when client area is invalidated
        case WM_PAINT:
            BeginPaint( hWnd, &ps );
            EndPaint( hWnd, &ps ); // End painting
            break;

        case WM_MOUSEWHEEL:
            if ( GetForegroundWindow() == hWnd )
            {
                Input::AccumulateMouseWheelDelta( hWnd, GET_WHEEL_DELTA_WPARAM( wParam ) );
            }
            break;

        case WM_INPUT:
            Input::AccumulateRawMouseDelta( hWnd, reinterpret_cast<HRAWINPUT>( lParam ) );
            break;

        case WM_SYSKEYDOWN:
        case WM_SYSKEYUP:
            if ( wParam == VK_MENU )
            {
                return 0;
            }
            break;

        case WM_SYSCHAR:
            return 0;

        case WM_SYSCOMMAND:
            if ( ( wParam & 0xfff0u ) == SC_KEYMENU )
            {
                return 0;
            }
            break;

        case WM_SETFOCUS:
            Input::SetSystemCursorVisible( Input::IsSystemCursorVisibleRequested() );
            break;

        case WM_KILLFOCUS:
            Input::SetSystemCursorVisible( true );
            break;

        case WM_SETCURSOR:
            if ( LOWORD( lParam ) == HTCLIENT )
            {
                if ( GetForegroundWindow() == hWnd )
                {
                    Input::SetSystemCursorVisible( Input::IsSystemCursorVisibleRequested() );
                }
                else
                {
                    Input::SetSystemCursorVisible( true );
                    SetCursor( LoadCursor( nullptr, IDC_ARROW ) );
                }
                return TRUE;
            }
            break;

        // WM_DESTROY is fired when the window is closed
        case WM_DESTROY:
            PostQuitMessage( 0 );
            break;
        }
    }
    catch ( const std::exception& e ) // Catch all exceptions thrown by the Skullbonez Core
    {
        if ( m_cWindow )
        {
            m_cWindow->MsgBox( e.what(), "FATAL ERROR", MB_OK );
        }
        else
        {
            MessageBoxA( hWnd, e.what(), "FATAL ERROR", MB_OK );
        }
    }

    // Now we have done whatever we wanted to do, let windows do anything else it
    // needs to do based on the message fired...
    return DefWindowProc( hWnd, iMsg, wParam, lParam );
}


SbResult Window::CreateAppWindow( HINSTANCE hInstance, bool isFullScreenMode )
{
    HWND hWnd = nullptr;       // Handle to our window
    WNDCLASS wndclass = { 0 }; // Window class struct
    DWORD dwStyle = 0;         // Window style

    wndclass.style = CS_HREDRAW | CS_VREDRAW;          // Vert and Horiz redraw
    wndclass.lpfnWndProc = WndProc;                    // Assign callback function
    wndclass.hInstance = hInstance;                    // Assign hInstance
    wndclass.hIcon = LoadIcon( nullptr, IDI_WINLOGO ); // Default icon
    wndclass.hCursor = nullptr;                        // Engine/UI draws its own cursor when needed
    wndclass.hbrBackground = reinterpret_cast<HBRUSH>( GetStockObject( WHITE_BRUSH ) ); // White client background
    wndclass.lpszClassName = WINDOW_NAME;                                               // Assign class name

    RegisterClass( &wndclass ); // Register class with OS

    m_fIsFullScreenMode = isFullScreenMode;

    if ( m_fIsFullScreenMode )
    {
        dwStyle = WS_POPUP | WS_CLIPSIBLINGS | WS_CLIPCHILDREN;

        // Changes to full screen mode
        ChangeToFullScreen( m_startupWindowWidth, m_startupWindowHeight );

        Input::SetSystemCursorVisible( false );
    }
    else
    {
        dwStyle = WS_OVERLAPPEDWINDOW | WS_CLIPSIBLINGS | WS_CLIPCHILDREN;
    }

    int windowX = 0;
    int windowY = 0;
    const int windowW = m_startupWindowWidth;
    const int windowH = m_startupWindowHeight;
    if ( !m_fIsFullScreenMode )
    {
        // Default the window to the bottom-left of the usable desktop work area.
        // The work area excludes the taskbar, so a tall 1800x1000 window does not
        // open with its title bar hidden behind shell chrome.
        RECT workArea = {};
        if ( SystemParametersInfoA( SPI_GETWORKAREA, 0, &workArea, 0 ) )
        {
            windowX = workArea.left;
            windowY = (std::max)( workArea.top, workArea.bottom - windowH );
        }
    }

    hWnd = CreateWindow( WINDOW_NAME, // Window class name
                         TITLE_TEXT,  // Window title text
                         dwStyle,
                         windowX, // Window xPos
                         windowY, // Window yPos
                         windowW,
                         windowH,
                         nullptr,   // Parent window handle
                         nullptr,   // Window menu handle
                         hInstance, // Application instance
                         this );    // Data to pass to WndProc

    if ( !hWnd )
    {
        // Lane R: native window creation can fail because of the host desktop
        // environment, so startup reports the result instead of unwinding.
        return SbResult::Failure( "Runtime/Window", "Window creation failed." );
    }
    m_sWindow = hWnd;
    Input::BindWindow( *this );
    // Lifetime: bind callback-fed input queues as soon as the HWND exists so
    // later window messages cannot enqueue input against an unknown window.
    Input::BindCallbackBridge( hWnd );
    ShowWindow( hWnd, SW_SHOWNORMAL ); // Show window
    UpdateWindow( hWnd );
    SetFocus( hWnd );
    Input::SetSystemCursorVisible( false );
    (void)Input::RegisterRawMouseInput( hWnd );
    return SbResult::Success();
}


void Window::SetTitleText( const char* cText )
{
    SetWindowText( m_sWindow, cText );
}


int Window::MsgBox( const char* cMsgBoxText, const char* cMsgBoxTitle, const UINT iMsgBoxType )
{
    return ( MessageBox( m_sWindow, cMsgBoxText, cMsgBoxTitle, iMsgBoxType ) );
}
