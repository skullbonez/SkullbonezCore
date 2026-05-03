// --- Includes ---
#include "SkullbonezFramebufferDX11.h"
#include "SkullbonezRenderBackendDX11.h"
#include <stdexcept>


// --- Usings ---
using namespace SkullbonezCore::Rendering;


FramebufferDX11::FramebufferDX11( RenderBackendDX11* backend, ID3D11Device* device, ID3D11DeviceContext* context )
    : m_backend( backend ), m_device( device ), m_context( context ), m_colorTex( nullptr ), m_rtv( nullptr ), m_srv( nullptr ), m_depthTex( nullptr ), m_dsv( nullptr ), m_textureHandle( 0 ), m_width( 0 ), m_height( 0 ), m_savedRTV( nullptr ), m_savedDSV( nullptr )
{
}


FramebufferDX11::~FramebufferDX11()
{
    ResetResources();
}


bool FramebufferDX11::Create( int width, int height )
{
    m_width = width;
    m_height = height;

    // Color texture
    D3D11_TEXTURE2D_DESC texDesc = {};
    texDesc.Width = (UINT)width;
    texDesc.Height = (UINT)height;
    texDesc.MipLevels = 1;
    texDesc.ArraySize = 1;
    texDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    texDesc.SampleDesc.Count = 1;
    texDesc.Usage = D3D11_USAGE_DEFAULT;
    texDesc.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;

    HRESULT hr = m_device->CreateTexture2D( &texDesc, nullptr, &m_colorTex );
    if ( FAILED( hr ) )
    {
        return false;
    }

    hr = m_device->CreateRenderTargetView( m_colorTex, nullptr, &m_rtv );
    if ( FAILED( hr ) )
    {
        return false;
    }

    hr = m_device->CreateShaderResourceView( m_colorTex, nullptr, &m_srv );
    if ( FAILED( hr ) )
    {
        return false;
    }

    // Register as a texture in the backend so BindTexture works
    m_textureHandle = m_backend->RegisterSRV( m_srv );

    // Depth stencil
    D3D11_TEXTURE2D_DESC depthDesc = {};
    depthDesc.Width = (UINT)width;
    depthDesc.Height = (UINT)height;
    depthDesc.MipLevels = 1;
    depthDesc.ArraySize = 1;
    depthDesc.Format = DXGI_FORMAT_D24_UNORM_S8_UINT;
    depthDesc.SampleDesc.Count = 1;
    depthDesc.Usage = D3D11_USAGE_DEFAULT;
    depthDesc.BindFlags = D3D11_BIND_DEPTH_STENCIL;

    hr = m_device->CreateTexture2D( &depthDesc, nullptr, &m_depthTex );
    if ( FAILED( hr ) )
    {
        return false;
    }

    hr = m_device->CreateDepthStencilView( m_depthTex, nullptr, &m_dsv );
    if ( FAILED( hr ) )
    {
        return false;
    }

    return true;
}


void FramebufferDX11::Bind() const
{
    // Save current render target
    m_context->OMGetRenderTargets( 1, &m_savedRTV, &m_savedDSV );

    // Bind our FBO
    m_context->OMSetRenderTargets( 1, &m_rtv, m_dsv );

    D3D11_VIEWPORT vp = {};
    vp.Width = (float)m_width;
    vp.Height = (float)m_height;
    vp.MaxDepth = 1.0f;
    m_context->RSSetViewports( 1, &vp );
}


void FramebufferDX11::Unbind() const
{
    // Restore previous render target
    if ( m_savedRTV || m_savedDSV )
    {
        m_context->OMSetRenderTargets( 1, &m_savedRTV, m_savedDSV );
        if ( m_savedRTV )
        {
            m_savedRTV->Release();
            m_savedRTV = nullptr;
        }
        if ( m_savedDSV )
        {
            m_savedDSV->Release();
            m_savedDSV = nullptr;
        }
    }
}


uint32_t FramebufferDX11::GetColorTextureHandle() const
{
    return m_textureHandle;
}


void FramebufferDX11::ResetResources()
{
    if ( m_textureHandle && m_backend )
    {
        m_backend->UnregisterSRV( m_textureHandle );
        m_textureHandle = 0;
    }
    if ( m_dsv )
    {
        m_dsv->Release();
        m_dsv = nullptr;
    }
    if ( m_depthTex )
    {
        m_depthTex->Release();
        m_depthTex = nullptr;
    }
    if ( m_srv )
    {
        m_srv->Release();
        m_srv = nullptr;
    }
    if ( m_rtv )
    {
        m_rtv->Release();
        m_rtv = nullptr;
    }
    if ( m_colorTex )
    {
        m_colorTex->Release();
        m_colorTex = nullptr;
    }
}