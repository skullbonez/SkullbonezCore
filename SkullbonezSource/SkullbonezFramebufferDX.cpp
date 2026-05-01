// --- Includes ---
#include "SkullbonezFramebufferDX.h"
#include "SkullbonezWindowDX.h"
#include <stdexcept>


// --- Usings ---
using namespace SkullbonezCore::Rendering;


Framebuffer::Framebuffer( int width, int height )
    : m_width( width ), m_height( height )
{
    Build();
}


Framebuffer::~Framebuffer()
{
    m_colorSRV = nullptr;
    m_depthStencilView = nullptr;
    m_renderTargetView = nullptr;
    m_depthStencilTexture = nullptr;
    m_colorTexture = nullptr;
}


void Framebuffer::Build()
{
    ID3D11Device* pDevice = SkullbonezCore::Basics::SkullbonezWindow::Instance()->GetDevice();
    ID3D11DeviceContext* pContext = SkullbonezCore::Basics::SkullbonezWindow::Instance()->GetDeviceContext();

    // Create color texture
    D3D11_TEXTURE2D_DESC colorDesc = {};
    colorDesc.Width = m_width;
    colorDesc.Height = m_height;
    colorDesc.MipLevels = 1;
    colorDesc.ArraySize = 1;
    colorDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    colorDesc.SampleDesc.Count = 1;
    colorDesc.SampleDesc.Quality = 0;
    colorDesc.Usage = D3D11_USAGE_DEFAULT;
    colorDesc.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;
    colorDesc.CPUAccessFlags = 0;
    colorDesc.MiscFlags = 0;

    HRESULT hr = pDevice->CreateTexture2D( &colorDesc, nullptr, m_colorTexture.GetAddressOf() );
    if ( FAILED( hr ) )
    {
        throw std::runtime_error( "Failed to create color texture for framebuffer" );
    }

    // Create render target view
    D3D11_RENDER_TARGET_VIEW_DESC rtvDesc = {};
    rtvDesc.Format = colorDesc.Format;
    rtvDesc.ViewDimension = D3D11_RTV_DIMENSION_TEXTURE2D;
    rtvDesc.Texture2D.MipSlice = 0;

    hr = pDevice->CreateRenderTargetView( m_colorTexture.Get(), &rtvDesc, m_renderTargetView.GetAddressOf() );
    if ( FAILED( hr ) )
    {
        throw std::runtime_error( "Failed to create render target view for framebuffer" );
    }

    // Create shader resource view for color texture
    D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
    srvDesc.Format = colorDesc.Format;
    srvDesc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
    srvDesc.Texture2D.MostDetailedMip = 0;
    srvDesc.Texture2D.MipLevels = 1;

    hr = pDevice->CreateShaderResourceView( m_colorTexture.Get(), &srvDesc, m_colorSRV.GetAddressOf() );
    if ( FAILED( hr ) )
    {
        throw std::runtime_error( "Failed to create shader resource view for framebuffer color" );
    }

    // Create depth stencil texture
    D3D11_TEXTURE2D_DESC depthDesc = {};
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

    hr = pDevice->CreateTexture2D( &depthDesc, nullptr, m_depthStencilTexture.GetAddressOf() );
    if ( FAILED( hr ) )
    {
        throw std::runtime_error( "Failed to create depth stencil texture for framebuffer" );
    }

    // Create depth stencil view
    D3D11_DEPTH_STENCIL_VIEW_DESC dsvDesc = {};
    dsvDesc.Format = depthDesc.Format;
    dsvDesc.ViewDimension = D3D11_DSV_DIMENSION_TEXTURE2D;
    dsvDesc.Texture2D.MipSlice = 0;

    hr = pDevice->CreateDepthStencilView( m_depthStencilTexture.Get(), &dsvDesc, m_depthStencilView.GetAddressOf() );
    if ( FAILED( hr ) )
    {
        throw std::runtime_error( "Failed to create depth stencil view for framebuffer" );
    }
}


void Framebuffer::Bind() const
{
    ID3D11DeviceContext* pContext = SkullbonezCore::Basics::SkullbonezWindow::Instance()->GetDeviceContext();
    pContext->OMSetRenderTargets( 1, m_renderTargetView.GetAddressOf(), m_depthStencilView.Get() );

    // Set viewport
    D3D11_VIEWPORT viewport = {};
    viewport.TopLeftX = 0.0f;
    viewport.TopLeftY = 0.0f;
    viewport.Width = static_cast<float>( m_width );
    viewport.Height = static_cast<float>( m_height );
    viewport.MinDepth = 0.0f;
    viewport.MaxDepth = 1.0f;
    pContext->RSSetViewports( 1, &viewport );
}


void Framebuffer::Unbind() const
{
    ID3D11DeviceContext* pContext = SkullbonezCore::Basics::SkullbonezWindow::Instance()->GetDeviceContext();

    // Restore the backbuffer as render target
    ID3D11RenderTargetView* pRTV = SkullbonezCore::Basics::SkullbonezWindow::Instance()->m_renderTargetView.Get();
    ID3D11DepthStencilView* pDSV = SkullbonezCore::Basics::SkullbonezWindow::Instance()->m_depthStencilView.Get();
    pContext->OMSetRenderTargets( 1, &pRTV, pDSV );

    // Restore default viewport
    D3D11_VIEWPORT viewport = {};
    viewport.TopLeftX = 0.0f;
    viewport.TopLeftY = 0.0f;
    viewport.Width = static_cast<float>( SkullbonezCore::Basics::SkullbonezWindow::Instance()->m_sWindowDimensions.x );
    viewport.Height = static_cast<float>( SkullbonezCore::Basics::SkullbonezWindow::Instance()->m_sWindowDimensions.y );
    viewport.MinDepth = 0.0f;
    viewport.MaxDepth = 1.0f;
    pContext->RSSetViewports( 1, &viewport );
}


ID3D11ShaderResourceView* Framebuffer::GetColorTexture() const
{
    return m_colorSRV.Get();
}


int Framebuffer::GetWidth() const
{
    return m_width;
}


int Framebuffer::GetHeight() const
{
    return m_height;
}


void Framebuffer::ResetGLResources()
{
    m_colorSRV = nullptr;
    m_depthStencilView = nullptr;
    m_renderTargetView = nullptr;
    m_depthStencilTexture = nullptr;
    m_colorTexture = nullptr;
    Build();
}
