/*
File: SkullbonezSource/Runtime/App/Window.cpp
Purpose:
  Creates and owns the Win32 window and message pump integration.

Summary:
  CreateAppWindow establishes the native HWND, arms the input callback bridge,
  and then lets WndProc feed resize, focus, cursor, and raw mouse messages back
  into runtime-owned systems.

Glossary:
  Resize lifecycle: Borrowed renderer capability used only to resize swap-chain
  and depth resources when Win32 reports a new client size.

Invariants:
  - Window dimensions are client-area dimensions and drive both renderer resize
    and the perspective/text projections.
  - The resize lifecycle borrow is installed after renderer startup and cleared
    before backend teardown; Window never owns the renderer.
  - The singleton pointer is a legacy access shim around static storage; native
    HWND/HDC lifetime still follows CreateAppWindow and OS messages.

Related:
  - SkullbonezSource/Runtime/App/Window.h
  - Agentic/Reference/runtime-reference.md
  - Agentic/Reference/comment-style-guide.md
  - Agentic/Reference/engine-glossary.md
*/
#include "Window.h"
#include "../../Core/SbDiagnosticStore.h"
#include "../../Core/WindowConstants.h"
#include "../../Rendering/DX12/Dx12FrameOwner.h"
#include "../Input/Input.h"
#if defined( SKULLBONEZ_DEVELOPMENT_TOOLS )
#include "../DevelopmentTools/ImGuiEditorOwner.h"
#endif
#include "../../Core/FatalError.h"
#include "../../Core/Log.h"

#include <algorithm>
#include <cstdio>


using namespace SkullbonezCore::Runtime;
using namespace SkullbonezCore::Hardware;
using namespace SkullbonezCore::Rendering;


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
    m_resizeRenderFrame = nullptr;
}


Window::~Window()
{
#if defined( SKULLBONEZ_DEVELOPMENT_TOOLS )

    // Lifetime: Run must remove the native-message borrow before its ImGui
    // context disappears. A surviving pointer would let late messages enter
    // freed vendor state during native teardown.

    if ( m_developmentUiInput )
    {
        SB_FATAL( "Runtime/Window", "Development UI input owner remained bound during Window destruction." );
    }
#endif
}


#if defined( SKULLBONEZ_DEVELOPMENT_TOOLS )
void Window::BindDevelopmentUiInput( DevelopmentTools::ImGuiEditorOwner& owner )
{

    if ( m_developmentUiInput && m_developmentUiInput != &owner )
    {
        SB_FATAL( "Runtime/Window", "A different development UI input owner is already bound." );
    }

    m_developmentUiInput = &owner;
}


void Window::UnbindDevelopmentUiInput( DevelopmentTools::ImGuiEditorOwner& owner )
{

    if ( !m_developmentUiInput )
    {

        // Start may fail before the Window borrow is installed. Shutdown still
        // calls this one balanced cleanup path and has nothing to remove.
        return;
    }

    if ( m_developmentUiInput != &owner )
    {
        SB_FATAL( "Runtime/Window", "Development UI input owner unbound with a different lifetime target." );
    }

    m_developmentUiInput = nullptr;
}


DevelopmentTools::ImGuiEditorNativeMessageRoute Window::RouteDevelopmentUiMessage( HWND window, UINT message, WPARAM wParam,
                                                                                   LPARAM lParam )
{
    return m_developmentUiInput ? m_developmentUiInput->HandleNativeMessage( window, message, wParam, lParam )
                                : DevelopmentTools::ImGuiEditorNativeMessageRoute {};
}
#endif


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


void Window::SetResizeRenderFrameOwner( Dx12FrameOwner* renderFrame )
{
    m_resizeRenderFrame = renderFrame;
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


SkullbonezCore::Core::SbResult Window::HandleScreenResize()
{
    int w = m_sWindowDimensions.x;
    int h = m_sWindowDimensions.y;

    // Hazard: minimized windows report zero client area; resizing the backend
    // to zero dimensions would invalidate swap-chain and projection state.

    if ( w <= 0 || h <= 0 || !m_resizeRenderFrame )
    {
        return SkullbonezCore::Core::SbResult::Success();
    }

    const SkullbonezCore::Core::SbResult resizeResult = m_resizeRenderFrame->Resize( w, h );

    if ( !resizeResult.Ok() )
    {
        return resizeResult;
    }

    // DX12 clip-space depth is [0,1], so the perspective matrix must use the
    // matching projection convention after every resize.
    // Invariant: Window owns the projection depth range after startup; resize
    // must not reopen global config while handling OS messages.
    float aspect = static_cast<float>( w ) / static_cast<float>( h );
    projectionMatrix = Math::Transformation::Matrix4::PerspectiveZeroToOne( 45.0f, aspect, m_projectionNearPlane,
                                                                            m_projectionFarPlane );

    return SkullbonezCore::Core::SbResult::Success();
}


void Window::ChangeToFullScreen( int xResolution, int yResolution )
{
    DEVMODE dmSettings = { 0 }; // Device mode variable - required to change modes

    if ( !EnumDisplaySettings( nullptr, ENUM_CURRENT_SETTINGS, &dmSettings ) )
    {
        MsgBox( "Could Not Enumerate Display Settings", "Error", MB_OK );

        // Lane R: this boundary has no Run-owned result carrier, so publish a
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

        // Lane R: preserve failure at the process boundary even though Win32
        // supplies only the display-change status here.
        PostQuitMessage( 1 );
    }
}


// "Windows Procedure" - this function handles messages for our window
LRESULT CALLBACK WndProc( HWND hWnd, UINT iMsg, WPARAM wParam, LPARAM lParam )
{
    PAINTSTRUCT ps = { 0 }; // Assists with repainting the client area

    // Why: Win32 stores the non-owning Window address in LONG_PTR user data and
    // returns WM_CREATE payloads through LPARAM. Recover the typed owner only at
    // this WndProc ABI seam; the window object retains lifetime authority.
    Window* window = reinterpret_cast<Window*>( GetWindowLongPtr( hWnd, GWLP_USERDATA ) );

#if defined( SKULLBONEZ_DEVELOPMENT_TOOLS )

    // Concept: Dear ImGui observes the native stream first, then this value
    // decides whether the established engine input path also receives the
    // event. Window/OS lifecycle messages are never captured by editor policy.
    DevelopmentTools::ImGuiEditorNativeMessageRoute developmentUiRoute;

    if ( window )
    {
        developmentUiRoute = window->RouteDevelopmentUiMessage( hWnd, iMsg, wParam, lParam );
    }
#endif

    // Window callbacks cannot propagate failures through Win32. Engine-owned
    // operations invoked here use explicit result/fatal lanes.

    switch ( iMsg )
    {

    // Which message do we have to deal with today...?
    // WM_CREATE fired on window creation
    case WM_CREATE:
    {
        CREATESTRUCT* create = reinterpret_cast<CREATESTRUCT*>( lParam );
        window = reinterpret_cast<Window*>( create->lpCreateParams );
        SetWindowLongPtr( hWnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>( window ) );
        break;
    }

    // WM_SIZE fired on a resize
    case WM_SIZE:

        // LoWord = m_width, HiWord = m_height

        if ( window )
        {
            window->SetWindowDimensions( LOWORD( lParam ), HIWORD( lParam ) );
            const SkullbonezCore::Core::SbResult resizeResult = window->HandleScreenResize();

            if ( !resizeResult.Ok() )
            {
                const char* owner = resizeResult.ErrorOwner()[0] != '\0' ? resizeResult.ErrorOwner() : "Runtime/Window";
                const char* message = resizeResult.ErrorMessage()[0] != '\0' ? resizeResult.ErrorMessage()
                                                                             : "window resize failed";

                SkullbonezCore::Core::Log().WriteEventf( "window_resize_failed owner=\"%s\" message=\"%s\"", owner,
                                                         message );

                std::fprintf( stderr, "[window] Resize failed owner=%s reason=\"%s\"\n", owner, message );
                std::fflush( stderr );
                SkullbonezCore::Core::Log().FlushAll();
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
#if defined( SKULLBONEZ_DEVELOPMENT_TOOLS )

        if ( !developmentUiRoute.decision.engineConsumes )
        {
            break;
        }
#endif

        if ( GetForegroundWindow() == hWnd )
        {
            Input::AccumulateMouseWheelDelta( hWnd, GET_WHEEL_DELTA_WPARAM( wParam ) );
        }

        break;

    case WM_INPUT:
#if defined( SKULLBONEZ_DEVELOPMENT_TOOLS )

        if ( !developmentUiRoute.decision.engineConsumes )
        {
            break;
        }
#endif

        // Why: WM_INPUT carries an HRAWINPUT token in LPARAM by Win32 contract.
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
#if defined( SKULLBONEZ_DEVELOPMENT_TOOLS )

        if ( !developmentUiRoute.decision.engineConsumes )
        {
            return developmentUiRoute.backendResult;
        }
#endif

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

#if defined( SKULLBONEZ_DEVELOPMENT_TOOLS )

    if ( !developmentUiRoute.decision.engineConsumes )
    {
        return developmentUiRoute.backendResult;
    }
#endif

    // Now we have done whatever we wanted to do, let windows do anything else it
    // needs to do based on the message fired...
    return DefWindowProc( hWnd, iMsg, wParam, lParam );
}


SkullbonezCore::Core::SbResult Window::CreateAppWindow( HINSTANCE hInstance, bool isFullScreenMode, bool showOnCreate )
{
    HWND hWnd = nullptr;       // Handle to our window
    WNDCLASS wndclass = { 0 }; // Window class struct

    DWORD dwStyle = 0; // Window style

    wndclass.style = CS_HREDRAW | CS_VREDRAW; // Vert and Horiz redraw
    wndclass.lpfnWndProc = WndProc;           // Assign callback function

    wndclass.hInstance = hInstance; // Assign hInstance

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
                         windowW, windowH,
                         nullptr,   // Parent window handle
                         nullptr,   // Window menu handle
                         hInstance, // Application instance
                         this );    // Data to pass to WndProc

    if ( !hWnd )
    {

        // Lane R: native window creation can fail because of the host desktop
        // environment, so startup reports the result instead of unwinding.
        return m_resultDiagnostics.Failure( "Runtime/Window", "Window creation failed." );
    }

    m_sWindow = hWnd;
    Input::BindWindow( *this );

    // Lifetime: bind callback-fed input queues as soon as the HWND exists so
    // later window messages cannot enqueue input against an unknown window.
    Input::BindCallbackBridge( hWnd );

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
        SetWindowDimensions( clientDimensions );
    }

    Input::SetSystemCursorVisible( false );
    (void)Input::RegisterRawMouseInput( hWnd );
    return SkullbonezCore::Core::SbResult::Success();
}


void Window::SetTitleText( const char* cText )
{
    SetWindowText( m_sWindow, cText );
}


int Window::MsgBox( const char* cMsgBoxText, const char* cMsgBoxTitle, const UINT iMsgBoxType )
{
    return ( MessageBox( m_sWindow, cMsgBoxText, cMsgBoxTitle, iMsgBoxType ) );
}
