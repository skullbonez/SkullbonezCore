// --- Includes ---
#include "SkullbonezWindow.h"


// --- Usings ---
using namespace SkullbonezCore::Basics;


SkullbonezWindow::SkullbonezWindow()
{
    m_sWindow = nullptr;
    m_featureLevel = D3D_FEATURE_LEVEL_11_0;
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
    // Guard: skip resize if device not yet initialized
    if ( !m_d3dDevice || !m_swapChain )
    {
        return;
    }

    SkullbonezWindow* m_cWindow = SkullbonezWindow::Instance();

    int m_width = m_cWindow->m_sWindowDimensions.x;
    int m_height = m_cWindow->m_sWindowDimensions.y;

    if ( !m_height )
    {
        m_height = 1;
    }

    // Release old render target and depth stencil views
    m_renderTargetView.Reset();
    m_depthStencilView.Reset();

    // Resize swap chain buffers
    HRESULT hr = m_swapChain->ResizeBuffers( 0, m_width, m_height, DXGI_FORMAT_R8G8B8A8_UNORM, 0 );
    if ( FAILED( hr ) )
    {
        MsgBox( "Failed to resize swap chain buffers", "DirectX Error", MB_OK );
        PostQuitMessage( 0 );
        return;
    }

    // Create new render target view
    ComPtr<ID3D11Texture2D> backBuffer;
    hr = m_swapChain->GetBuffer( 0, IID_PPV_ARGS( &backBuffer ) );
    if ( FAILED( hr ) )
    {
        MsgBox( "Failed to get back buffer", "DirectX Error", MB_OK );
        PostQuitMessage( 0 );
        return;
    }

    hr = m_d3dDevice->CreateRenderTargetView( backBuffer.Get(), nullptr, &m_renderTargetView );
    if ( FAILED( hr ) )
    {
        MsgBox( "Failed to create render target view", "DirectX Error", MB_OK );
        PostQuitMessage( 0 );
        return;
    }

    // Create depth stencil texture
    D3D11_TEXTURE2D_DESC depthDesc = { 0 };
    depthDesc.Width = m_width;
    depthDesc.Height = m_height;
    depthDesc.MipLevels = 1;
    depthDesc.ArraySize = 1;
    depthDesc.Format = DXGI_FORMAT_D24_UNORM_S8_UINT;
    depthDesc.SampleDesc.Count = 1;
    depthDesc.SampleDesc.Quality = 0;
    depthDesc.Usage = D3D11_USAGE_DEFAULT;
    depthDesc.BindFlags = D3D11_BIND_DEPTH_STENCIL;
    depthDesc.CPUAccessFlags = 0;
    depthDesc.MiscFlags = 0;

    ComPtr<ID3D11Texture2D> depthBuffer;
    hr = m_d3dDevice->CreateTexture2D( &depthDesc, nullptr, &depthBuffer );
    if ( FAILED( hr ) )
    {
        MsgBox( "Failed to create depth stencil texture", "DirectX Error", MB_OK );
        PostQuitMessage( 0 );
        return;
    }

    hr = m_d3dDevice->CreateDepthStencilView( depthBuffer.Get(), nullptr, &m_depthStencilView );
    if ( FAILED( hr ) )
    {
        MsgBox( "Failed to create depth stencil view", "DirectX Error", MB_OK );
        PostQuitMessage( 0 );
        return;
    }

    // Bind render target and depth stencil to pipeline
    m_d3dContext->OMSetRenderTargets( 1, m_renderTargetView.GetAddressOf(), m_depthStencilView.Get() );

    // Set viewport
    D3D11_VIEWPORT viewport = { 0 };
    viewport.TopLeftX = 0.0f;
    viewport.TopLeftY = 0.0f;
    viewport.Width = static_cast<float>( m_width );
    viewport.Height = static_cast<float>( m_height );
    viewport.MinDepth = 0.0f;
    viewport.MaxDepth = 1.0f;
    m_d3dContext->RSSetViewports( 1, &viewport );

    // Build and store the perspective projection matrix
    float aspect = static_cast<float>( m_width ) / static_cast<float>( m_height );
    m_cWindow->projectionMatrix = Math::Transformation::Matrix4::Perspective(
        45.0f,
        aspect,
        Cfg().frustumNear,
        Cfg().frustumFar );
}


void SkullbonezWindow::ChangeToFullScreen( int xResolution, int yResolution )
{
    DEVMODE dmSettings = { 0 };

    if ( !EnumDisplaySettings( nullptr, ENUM_CURRENT_SETTINGS, &dmSettings ) )
    {
        MsgBox( "Could Not Enumerate Display Settings", "Error", MB_OK );
        PostQuitMessage( 0 );
    }

    dmSettings.dmPelsWidth = xResolution;
    dmSettings.dmPelsHeight = yResolution;
    dmSettings.dmFields = DM_PELSWIDTH | DM_PELSHEIGHT;

    int result = ChangeDisplaySettings( &dmSettings, CDS_FULLSCREEN );

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
    PAINTSTRUCT ps = { 0 };

    try
    {
        switch ( iMsg )
        {
        case WM_CREATE:
            break;

        case WM_SIZE:
            m_cWindow->SetWindowDimensions( LOWORD( lParam ), HIWORD( lParam ) );
            m_cWindow->HandleScreenResize();
            break;

        case WM_PAINT:
            BeginPaint( hWnd, &ps );
            EndPaint( hWnd, &ps );
            break;

        case WM_KEYDOWN:
            if ( wParam == VK_ESCAPE )
            {
                PostQuitMessage( 0 );
            }
            break;

        case WM_DESTROY:
            PostQuitMessage( 0 );
            break;
        }
    }
    catch ( const std::exception& e )
    {
        m_cWindow->MsgBox( e.what(), "FATAL ERROR", MB_OK );
    }

    return DefWindowProc( hWnd, iMsg, wParam, lParam );
}


void SkullbonezWindow::InitialiseDirectX()
{
    SkullbonezWindow* m_cWindow = SkullbonezWindow::Instance();

    RECT windowDimensions;
    GetClientRect( m_cWindow->m_sWindow, &windowDimensions );
    m_cWindow->SetWindowDimensions( windowDimensions );

    // Create the D3D11 device and device context
    UINT creationFlags = 0;
#ifdef _DEBUG
    creationFlags |= D3D11_CREATE_DEVICE_DEBUG;
#endif

    const D3D_FEATURE_LEVEL featureLevels[] = {
        D3D_FEATURE_LEVEL_11_1,
        D3D_FEATURE_LEVEL_11_0,
    };

    HRESULT hr = D3D11CreateDevice(
        nullptr, // Use default adapter
        D3D_DRIVER_TYPE_HARDWARE,
        nullptr,
        creationFlags,
        featureLevels,
        ARRAYSIZE( featureLevels ),
        D3D11_SDK_VERSION,
        &m_d3dDevice,
        &m_featureLevel,
        &m_d3dContext );

    if ( FAILED( hr ) )
    {
        MsgBox( "Failed to create D3D11 device", "DirectX Error", MB_OK );
        PostQuitMessage( 0 );
        return;
    }

    // Create swap chain via DXGI
    ComPtr<IDXGIDevice> dxgiDevice;
    hr = m_d3dDevice->QueryInterface( IID_PPV_ARGS( &dxgiDevice ) );
    if ( FAILED( hr ) )
    {
        MsgBox( "Failed to get DXGI device", "DirectX Error", MB_OK );
        PostQuitMessage( 0 );
        return;
    }

    ComPtr<IDXGIAdapter> dxgiAdapter;
    hr = dxgiDevice->GetAdapter( &dxgiAdapter );
    if ( FAILED( hr ) )
    {
        MsgBox( "Failed to get DXGI adapter", "DirectX Error", MB_OK );
        PostQuitMessage( 0 );
        return;
    }

    ComPtr<IDXGIFactory> dxgiFactory;
    hr = dxgiAdapter->GetParent( IID_PPV_ARGS( &dxgiFactory ) );
    if ( FAILED( hr ) )
    {
        MsgBox( "Failed to get DXGI factory", "DirectX Error", MB_OK );
        PostQuitMessage( 0 );
        return;
    }

    DXGI_SWAP_CHAIN_DESC swapChainDesc = { 0 };
    swapChainDesc.BufferCount = 2;
    swapChainDesc.BufferDesc.Width = m_cWindow->m_sWindowDimensions.x;
    swapChainDesc.BufferDesc.Height = m_cWindow->m_sWindowDimensions.y;
    swapChainDesc.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    swapChainDesc.BufferDesc.RefreshRate.Numerator = 60;
    swapChainDesc.BufferDesc.RefreshRate.Denominator = 1;
    swapChainDesc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    swapChainDesc.OutputWindow = m_cWindow->m_sWindow;
    swapChainDesc.SampleDesc.Count = 1;
    swapChainDesc.SampleDesc.Quality = 0;
    swapChainDesc.Windowed = TRUE;
    swapChainDesc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
    swapChainDesc.Flags = DXGI_SWAP_CHAIN_FLAG_ALLOW_MODE_SWITCH;

    hr = dxgiFactory->CreateSwapChain( m_d3dDevice.Get(), &swapChainDesc, &m_swapChain );
    if ( FAILED( hr ) )
    {
        MsgBox( "Failed to create swap chain", "DirectX Error", MB_OK );
        PostQuitMessage( 0 );
        return;
    }

    // Prevent ALT+ENTER from toggling fullscreen (we handle fullscreen manually)
    dxgiFactory->MakeWindowAssociation( m_cWindow->m_sWindow, DXGI_MWA_NO_ALT_ENTER );

    HandleScreenResize();
}


void SkullbonezWindow::Present()
{
    if ( m_swapChain )
    {
        m_swapChain->Present( 1, 0 ); // Present with vsync
    }
}


void SkullbonezWindow::CreateAppWindow( HINSTANCE hInstance, bool isFullScreenMode )
{
    HWND hWnd = nullptr;
    WNDCLASS wndclass = { 0 };
    DWORD dwStyle = 0;

    wndclass.style = CS_HREDRAW | CS_VREDRAW;
    wndclass.lpfnWndProc = WndProc;
    wndclass.hInstance = hInstance;
    wndclass.hIcon = LoadIcon( nullptr, IDI_WINLOGO );
    wndclass.hCursor = LoadCursor( nullptr, IDC_ARROW );
    wndclass.hbrBackground = reinterpret_cast<HBRUSH>( GetStockObject( WHITE_BRUSH ) );
    wndclass.lpszClassName = WINDOW_NAME;

    RegisterClass( &wndclass );

    m_fIsFullScreenMode = isFullScreenMode;

    if ( m_fIsFullScreenMode )
    {
        dwStyle = WS_POPUP | WS_CLIPSIBLINGS | WS_CLIPCHILDREN;
        ChangeToFullScreen( Cfg().screenX, Cfg().screenY );
        ShowCursor( false );
    }
    else
    {
        dwStyle = WS_OVERLAPPEDWINDOW | WS_CLIPSIBLINGS | WS_CLIPCHILDREN;
    }

    hWnd = CreateWindow( WINDOW_NAME,
                         TITLE_TEXT,
                         dwStyle,
                         0,
                         0,
                         Cfg().screenX,
                         Cfg().screenY,
                         nullptr,
                         nullptr,
                         hInstance,
                         nullptr );

    if ( !hWnd )
    {
        throw std::runtime_error( "Window creation failed" );
    }

    ShowWindow( hWnd, SW_SHOWNORMAL );
    UpdateWindow( hWnd );
    SetFocus( hWnd );

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
