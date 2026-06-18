/*
File: SkullbonezSource/SkullbonezWindow.cpp
Purpose:
  Creates and owns the Win32 window and message pump integration.

Mental model:
  Runtime code connects authored scene data, input, simulation, render
  backends, and validation-oriented launch modes. Follow who owns state and
  when that state changes.

Glossary:
  DX12 (DirectX 12): Production renderer API whose swap-chain window target is
  created from this Win32 window.
  GPU (Graphics Processing Unit): Processor that executes rendering, compute,
  and raytracing commands asynchronously from the CPU.
  Validation gate: Repository script that proves a class of changes before
  commit or PR.

Related:
  - SkullbonezSource/SkullbonezWindow.h
  - Agentic/Reference/runtime-reference.md
  - Agentic/Reference/comment-style-guide.md
*/
#include "SkullbonezWindow.h"
#include "SkullbonezIRenderBackend.h"
#include "SkullbonezInput.h"
#include "SkullbonezText.h"

#include <algorithm>


using namespace SkullbonezCore::Basics;
using namespace SkullbonezCore::Hardware;
using namespace SkullbonezCore::Rendering;


SkullbonezWindow::SkullbonezWindow()
{
    m_sWindow = 0;
    m_sDevice = 0;
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

    // DX12 clip-space depth is [0,1], so the perspective matrix must use the
    // matching projection convention after every resize.
    float aspect = static_cast<float>( w ) / static_cast<float>( h );
    cWindow->projectionMatrix = Math::Transformation::Matrix4::PerspectiveZeroToOne(
        45.0f,
        aspect,
        Cfg().frustumNear,
        Cfg().frustumFar );
}


void SkullbonezWindow::ChangeToFullScreen( int xResolution, int yResolution )
{
    DEVMODE dmSettings = { 0 }; // Device mode variable - required to change modes

    if ( !EnumDisplaySettings( nullptr,
                               ENUM_CURRENT_SETTINGS,
                               &dmSettings ) )
    {
        MsgBox( "Could Not Enumerate Display Settings", "Error", MB_OK );
        PostQuitMessage( 0 );
    }

    dmSettings.dmPelsWidth = xResolution;
    dmSettings.dmPelsHeight = yResolution;

    // Specifiy what we have changed
    dmSettings.dmFields = DM_PELSWIDTH | // We changed m_width
                          DM_PELSHEIGHT; // We changed m_height

    int result = ChangeDisplaySettings( &dmSettings,
                                        CDS_FULLSCREEN );

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
            m_cWindow->HandleScreenResize();
            break;

        // WM_PAINT fired when client area is invalidated
        case WM_PAINT:
            BeginPaint( hWnd, &ps );
            EndPaint( hWnd, &ps ); // End painting
            break;

        case WM_MOUSEWHEEL:
            if ( GetForegroundWindow() == hWnd )
            {
                Input::AccumulateMouseWheelDelta( GET_WHEEL_DELTA_WPARAM( wParam ) );
            }
            break;

        case WM_INPUT:
            Input::AccumulateRawMouseDelta( reinterpret_cast<HRAWINPUT>( lParam ) );
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
        m_cWindow->MsgBox( e.what(), "FATAL ERROR", MB_OK );
    }

    // Now we have done whatever we wanted to do, let windows do anything else it
    // needs to do based on the message fired...
    return DefWindowProc( hWnd, iMsg, wParam, lParam );
}


void SkullbonezWindow::CreateAppWindow( HINSTANCE hInstance, bool isFullScreenMode )
{
    HWND hWnd = nullptr;       // Handle to our window
    WNDCLASS wndclass = { 0 }; // Window class struct
    DWORD dwStyle = 0;         // Window style

    wndclass.style = CS_HREDRAW | CS_VREDRAW;          // Vert and Horiz redraw
    wndclass.lpfnWndProc = WndProc;                    // Assign callback function
    wndclass.hInstance = hInstance;                    // Assign hInstance
    wndclass.hIcon = LoadIcon( nullptr, IDI_WINLOGO ); // Default icon
    wndclass.hCursor = nullptr;                        // Engine/UI draws its own cursor when needed
    wndclass.hbrBackground =
        reinterpret_cast<HBRUSH>( GetStockObject( WHITE_BRUSH ) ); // White client background
    wndclass.lpszClassName = WINDOW_NAME;                          // Assign class name

    RegisterClass( &wndclass ); // Register class with OS

    m_fIsFullScreenMode = isFullScreenMode;

    if ( m_fIsFullScreenMode )
    {
        dwStyle = WS_POPUP | WS_CLIPSIBLINGS | WS_CLIPCHILDREN;

        // Changes to full screen mode
        ChangeToFullScreen( Cfg().window.screenX, Cfg().window.screenY );

        Input::SetSystemCursorVisible( false );
    }
    else
    {
        dwStyle = WS_OVERLAPPEDWINDOW | WS_CLIPSIBLINGS | WS_CLIPCHILDREN;
    }

    int windowX = 0;
    int windowY = 0;
    const int windowW = Cfg().window.screenX;
    const int windowH = Cfg().window.screenY;
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
                         nullptr ); // Data to pass to WndProc

    if ( !hWnd )
    {
        throw std::runtime_error( "Window creation failed" ); // Throw exception on failure
    }
    ShowWindow( hWnd, SW_SHOWNORMAL ); // Show window
    UpdateWindow( hWnd );
    SetFocus( hWnd );
    Input::SetSystemCursorVisible( false );
    (void)Input::RegisterRawMouseInput( hWnd );

    m_sWindow = hWnd;
}


void SkullbonezWindow::SetTitleText( const char* cText )
{
    SetWindowText( m_sWindow, cText );
}


int SkullbonezWindow::MsgBox( const char* cMsgBoxText,
                              const char* cMsgBoxTitle,
                              const UINT iMsgBoxType )
{
    return ( MessageBox( m_sWindow, cMsgBoxText, cMsgBoxTitle, iMsgBoxType ) );
}
