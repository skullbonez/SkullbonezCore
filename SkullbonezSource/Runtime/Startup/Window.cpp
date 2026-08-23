/*
File: SkullbonezSource/Runtime/Startup/Window.cpp
Purpose:
  Creates and owns the Win32 window and message pump integration.

Summary:
  CreateAppWindow establishes the native HWND. WndProc copies resize and mouse
  payloads into a bounded host queue; App applies them to concrete owners after
  each native dispatch.

Glossary:
  Dispatch route: One synchronous App decision describing whether the selected
    development UI captured a native message.

Invariants:
  - Window dimensions are client-area dimensions and drive both renderer resize
    and the perspective/text projections.
  - Window retains no Input, renderer, or development-UI owner.
  - The singleton pointer is a legacy access shim around static storage; native
    HWND/HDC lifetime still follows CreateAppWindow and OS messages.

Related:
  - SkullbonezSource/Runtime/Startup/Window.h
  - Agentic/Reference/runtime-reference.md
  - Agentic/Reference/engine-glossary.md
*/
#include "Window.h"
#include "../../Core/SbDiagnosticStore.h"
#include "../../Core/WindowConstants.h"
#include "../../Core/FatalError.h"

#include <algorithm>


using namespace SkullbonezCore::Runtime;


Window::Window( SkullbonezCore::Core::SbDiagnosticStore& resultDiagnostics ) : m_resultDiagnostics( resultDiagnostics )
{
    m_sWindow = 0;
    m_sDevice = 0;
    m_sWindowDimensions = {};
    m_fIsFullScreenMode = false;
    m_projectionNearPlane = 1.0f;
    m_projectionFarPlane = 5500.0f;
    m_startupWindowWidth = 1800;
    m_startupWindowHeight = 1000;
}


Window::~Window() = default;


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


HDC Window::AcquireDeviceContext()
{
    m_sDevice = GetDC( m_sWindow );
    return m_sDevice;
}


void Window::ReleaseDeviceContext()
{
    if ( !m_sDevice )
    {
        return;
    }

    // Lifetime: the HDC is paired with this HWND. Centralize release here so
    // startup cleanup does not need to know which native fields are live.
    ReleaseDC( m_sWindow, m_sDevice );
    m_sDevice = nullptr;
}


void Window::UpdateProjectionForCurrentClient()
{
    const int w = m_sWindowDimensions.x;
    const int h = m_sWindowDimensions.y;

    if ( w <= 0 || h <= 0 )
    {
        return;
    }

    // DX12 clip-space depth is [0,1], so the perspective matrix must use the
    // matching projection convention after every resize.
    // Invariant: Window owns the projection depth range after startup; resize
    // must not reopen global config while handling OS messages.
    const float aspect = static_cast<float>( w ) / static_cast<float>( h );
    projectionMatrix = Math::Transformation::Matrix4::PerspectiveZeroToOne( 45.0f, aspect, m_projectionNearPlane,
                                                                            m_projectionFarPlane );
}


void Window::PushEvent( const NativeHostEvent& event )
{
    if ( m_eventCount >= m_events.size() )
    {
        SB_FATAL( "Runtime/Startup/Window", "Native event queue exhausted capacity=%zu", m_events.size() );
    }

    const std::size_t write = ( m_eventRead + m_eventCount ) % m_events.size();
    m_events[write] = event;
    ++m_eventCount;
}


NativeHostMessageRoute Window::ActiveRoute() const noexcept
{
    return m_dispatchActive ? m_activeRoute : NativeHostMessageRoute {};
}


bool Window::PeekNativeMessage( NativeHostMessage& message )
{
    MSG native = {};

    if ( !PeekMessage( &native, nullptr, 0, 0, PM_REMOVE ) )
    {
        return false;
    }

    message = { native.hwnd,
                native.message,
                native.wParam,
                native.lParam,
                static_cast<int>( native.wParam ),
                native.message == WM_QUIT };
    return true;
}


void Window::DispatchNativeMessage( const NativeHostMessage& message, const NativeHostMessageRoute& route )
{
    if ( message.quit )
    {
        SB_FATAL( "Runtime/Startup/Window", "WM_QUIT cannot be dispatched as a native callback." );
    }

    MSG native = {};
    native.hwnd = message.window;
    native.message = message.id;
    native.wParam = message.wParam;
    native.lParam = message.lParam;
    m_activeRoute = route;
    m_dispatchActive = true;
    TranslateMessage( &native );
    DispatchMessage( &native );
    m_dispatchActive = false;
    m_activeRoute = {};
}


bool Window::ConsumeNativeEvent( NativeHostEvent& event )
{
    if ( m_eventCount == 0 )
    {
        return false;
    }

    event = m_events[m_eventRead];
    m_eventRead = ( m_eventRead + 1 ) % m_events.size();
    --m_eventCount;
    return true;
}


void Window::ChangeToFullScreen( int xResolution, int yResolution )
{
    DEVMODE dmSettings = { 0 }; // Device mode variable - required to change modes

    if ( !EnumDisplaySettings( nullptr, ENUM_CURRENT_SETTINGS, &dmSettings ) )
    {
        MsgBox( "Could Not Enumerate Display Settings", "Error", MB_OK );

        // Recoverable error: this boundary has no Run-owned result carrier, so publish a
        // nonzero platform code for ApplicationExitState to translate.
        PostQuitMessage( 1 );
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

        // Recoverable error: preserve failure at the process boundary even though Win32
        // supplies only the display-change status here.
        PostQuitMessage( 1 );
    }
}


// "Windows Procedure" - this function handles messages for our window
LRESULT CALLBACK SkullbonezCore::Runtime::WndProc( HWND windowHandle, UINT messageId, WPARAM wParam, LPARAM lParam )
{
    PAINTSTRUCT ps = { 0 }; // Assists with repainting the client area

    // Why: Win32 stores the non-owning Window address in LONG_PTR user data and
    // returns WM_CREATE payloads through LPARAM. Recover the typed owner only at
    // this WndProc ABI seam; the window object retains lifetime authority.
    Window* window = reinterpret_cast<Window*>( GetWindowLongPtr( windowHandle, GWLP_USERDATA ) );

    const NativeHostMessageRoute route = window ? window->ActiveRoute() : NativeHostMessageRoute {};

    // Window callbacks cannot propagate failures through Win32. Engine-owned
    // operations invoked here use explicit result/fatal lanes.
    switch ( messageId )
    {
    // Which message do we have to deal with today...?
    // WM_CREATE fired on window creation
    case WM_CREATE:
    {
        CREATESTRUCT* create = reinterpret_cast<CREATESTRUCT*>( lParam );
        window = reinterpret_cast<Window*>( create->lpCreateParams );
        SetWindowLongPtr( windowHandle, GWLP_USERDATA, reinterpret_cast<LONG_PTR>( window ) );
        break;
    }

    // WM_SIZE fired on a resize
    case WM_SIZE:

        // LoWord = m_width, HiWord = m_height
        if ( window )
        {
            const int width = LOWORD( lParam );
            const int height = HIWORD( lParam );
            window->m_sWindowDimensions.x = width;
            window->m_sWindowDimensions.y = height;
            window->PushEvent( NativeHostEvent { NativeHostEventType::Resize, windowHandle, width, height } );
        }

        break;

    // WM_PAINT fired when client area is invalidated
    case WM_PAINT:
        BeginPaint( windowHandle, &ps );
        EndPaint( windowHandle, &ps ); // End painting
        break;

    case WM_MOUSEWHEEL:

        if ( !route.engineConsumes )
        {
            break;
        }

        if ( window && GetForegroundWindow() == windowHandle )
        {
            window->PushEvent( NativeHostEvent { NativeHostEventType::MouseWheel, windowHandle, GET_WHEEL_DELTA_WPARAM( wParam ) } );
        }

        break;

    case WM_INPUT:
    {
        if ( !route.engineConsumes || !window || GetForegroundWindow() != windowHandle )
        {
            break;
        }

        // Lifetime: HRAWINPUT is valid only while handling this callback. Copy
        // the mouse payload into the bounded value queue before returning.
        RAWINPUT raw = {};
        UINT rawSize = sizeof( raw );

        if ( GetRawInputData( reinterpret_cast<HRAWINPUT>( lParam ), RID_INPUT, &raw, &rawSize, sizeof( RAWINPUTHEADER ) ) !=
                 static_cast<UINT>( -1 ) &&
             raw.header.dwType == RIM_TYPEMOUSE )
        {
            const RAWMOUSE& mouse = raw.data.mouse;
            window->PushEvent( NativeHostEvent { NativeHostEventType::RawMouse, windowHandle,
                                                 static_cast<int>( mouse.lLastX ), static_cast<int>( mouse.lLastY ),
                                                 ( mouse.usFlags & MOUSE_MOVE_ABSOLUTE ) != 0,
                                                 ( mouse.usFlags & MOUSE_VIRTUAL_DESKTOP ) != 0 } );
        }

        break;
    }

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
        break;

    case WM_KILLFOCUS:
        break;

    case WM_SETCURSOR:

        if ( !route.engineConsumes )
        {
            return route.capturedResult;
        }

        if ( route.engineCursorHandled )
        {
            return TRUE;
        }

        break;

    // WM_DESTROY is fired when the window is closed
    case WM_DESTROY:
        PostQuitMessage( 0 );
        break;
    }

    if ( !route.engineConsumes )
    {
        return route.capturedResult;
    }

    // Now we have done whatever we wanted to do, let windows do anything else it
    // needs to do based on the message fired...
    return DefWindowProc( windowHandle, messageId, wParam, lParam );
}


SkullbonezCore::Core::SbResult Window::CreateAppWindow( HINSTANCE instance, bool isFullScreenMode, bool showOnCreate )
{
    HWND hWnd = nullptr;       // Handle to our window
    WNDCLASS wndclass = { 0 }; // Window class struct

    DWORD dwStyle = 0; // Window style

    wndclass.style = CS_HREDRAW | CS_VREDRAW; // Vert and Horiz redraw
    wndclass.lpfnWndProc = WndProc;           // Assign callback function

    wndclass.hInstance = instance; // Assign application instance

    wndclass.hIcon = LoadIcon( nullptr, IDI_WINLOGO ); // Default icon

    wndclass.hCursor = nullptr; // Engine/UI draws its own cursor when needed

    // Why: GetStockObject returns a generic GDI handle; WNDCLASS requires the
    // HBRUSH view for this borrowed stock brush and never deletes it.
    wndclass.hbrBackground = reinterpret_cast<HBRUSH>( GetStockObject( WHITE_BRUSH ) ); // White client background

    wndclass.lpszClassName = WINDOW_NAME; // Assign class name

    RegisterClass( &wndclass ); // Register class with OS

    m_fIsFullScreenMode = isFullScreenMode;

    if ( m_fIsFullScreenMode )
    {
        dwStyle = WS_POPUP | WS_CLIPSIBLINGS | WS_CLIPCHILDREN;

        // Changes to full screen mode
        ChangeToFullScreen( m_startupWindowWidth, m_startupWindowHeight );
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
                         windowW, windowH,
                         nullptr,  // Parent window handle
                         nullptr,  // Window menu handle
                         instance, // Application instance
                         this );   // Data to pass to WndProc

    if ( !hWnd )
    {
        // Recoverable error: native window creation can fail because of the host desktop
        // environment, so startup reports the result instead of unwinding.
        return m_resultDiagnostics.Failure( "Runtime/Window", "Window creation failed." );
    }

    m_sWindow = hWnd;

    // Why: long-running automation still creates a real HWND and DX12 swap
    // chain so screenshots and presentation buffers follow production. The
    // hidden lane only suppresses repeated desktop demonstrations; it does not
    // switch validation to a headless or alternate renderer.
    if ( showOnCreate )
    {
        ShowWindow( hWnd, SW_SHOWNORMAL );
        UpdateWindow( hWnd );
        SetFocus( hWnd );
    }
    else
    {
        RECT clientDimensions = {};

        if ( !GetClientRect( hWnd, &clientDimensions ) || clientDimensions.right <= 0 || clientDimensions.bottom <= 0 )
        {
            return m_resultDiagnostics.Failure( "Runtime/Window", "Hidden automation window has no drawable client area." );
        }

        // Hidden windows do not receive the normal WM_SIZE publication before
        // renderer startup. Publish the real client rectangle here so DX12 uses
        // the same swap-chain dimensions as the shown-window path.
        m_sWindowDimensions.x = clientDimensions.right;
        m_sWindowDimensions.y = clientDimensions.bottom;
    }

    // CreateWindow/ShowWindow synchronously deliver the initial WM_SIZE. Init
    // applies the final cached client dimensions directly once the renderer
    // exists, so do not replay those construction-only events into frame one.
    m_eventRead = 0;
    m_eventCount = 0;

    return SkullbonezCore::Core::SbResult::Success();
}


void Window::SetTitleText( const char* text )
{
    SetWindowText( m_sWindow, text );
}


int Window::MsgBox( const char* text, const char* title, const UINT type )
{
    return ( MessageBox( m_sWindow, text, title, type ) );
}
