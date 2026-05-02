// --- Includes ---
#include "SkullbonezRenderBackendDX.h"
#include "SkullbonezShaderDX.h"
#include "SkullbonezMeshDX.h"
#include "SkullbonezFramebufferDX.h"
#include <stdexcept>
#include <string>
#include <algorithm>

#pragma comment( lib, "d3d11.lib" )
#pragma comment( lib, "dxgi.lib" )


// --- Usings ---
using namespace SkullbonezCore::Rendering;
using namespace SkullbonezCore::Math::Transformation;


RenderBackendDX* RenderBackendDX::s_instance = nullptr;


RenderBackendDX::RenderBackendDX()
    : m_swapChain( nullptr ), m_device( nullptr ), m_context( nullptr ), m_backBufferRTV( nullptr ), m_depthStencilTex( nullptr ), m_depthStencilView( nullptr ), m_width( 0 ), m_height( 0 ), m_depthTestEnabled( true ), m_blendEnabled( false ), m_clearColor{ 0.0f, 0.0f, 0.0f, 1.0f }, m_clearDepth( 1.0f ), m_dsDepthOn( nullptr ), m_dsDepthOff( nullptr ), m_blendOn( nullptr ), m_blendOff( nullptr ), m_rsCullOn( nullptr ), m_rsCullOff( nullptr ), m_rsCullOnPolyOffset( nullptr ), m_rsCullOffPolyOffset( nullptr ), m_samplerLinear( nullptr ), m_samplerNearest( nullptr ), m_cullEnabled( true ), m_polyOffsetEnabled( false ), m_activeShader( nullptr )
{
}


static D3D11_BLEND TranslateBlendFactor( BlendFactor f )
{
    switch ( f )
    {
    case BlendFactor::Zero:
        return D3D11_BLEND_ZERO;
    case BlendFactor::One:
        return D3D11_BLEND_ONE;
    case BlendFactor::SrcAlpha:
        return D3D11_BLEND_SRC_ALPHA;
    case BlendFactor::OneMinusSrcAlpha:
        return D3D11_BLEND_INV_SRC_ALPHA;
    default:
        return D3D11_BLEND_ONE;
    }
}


void RenderBackendDX::CreateStateObjects()
{
    // Depth stencil states
    D3D11_DEPTH_STENCIL_DESC dsDesc = {};
    dsDesc.DepthEnable = TRUE;
    dsDesc.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ALL;
    dsDesc.DepthFunc = D3D11_COMPARISON_LESS_EQUAL;
    m_device->CreateDepthStencilState( &dsDesc, &m_dsDepthOn );

    dsDesc.DepthEnable = FALSE;
    dsDesc.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ZERO;
    m_device->CreateDepthStencilState( &dsDesc, &m_dsDepthOff );

    // Blend states
    D3D11_BLEND_DESC blendDesc = {};
    blendDesc.RenderTarget[0].BlendEnable = TRUE;
    blendDesc.RenderTarget[0].SrcBlend = D3D11_BLEND_SRC_ALPHA;
    blendDesc.RenderTarget[0].DestBlend = D3D11_BLEND_INV_SRC_ALPHA;
    blendDesc.RenderTarget[0].BlendOp = D3D11_BLEND_OP_ADD;
    blendDesc.RenderTarget[0].SrcBlendAlpha = D3D11_BLEND_ONE;
    blendDesc.RenderTarget[0].DestBlendAlpha = D3D11_BLEND_ZERO;
    blendDesc.RenderTarget[0].BlendOpAlpha = D3D11_BLEND_OP_ADD;
    blendDesc.RenderTarget[0].RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL;
    m_device->CreateBlendState( &blendDesc, &m_blendOn );

    blendDesc.RenderTarget[0].BlendEnable = FALSE;
    m_device->CreateBlendState( &blendDesc, &m_blendOff );

    // Rasterizer states
    D3D11_RASTERIZER_DESC rsDesc = {};
    rsDesc.FillMode = D3D11_FILL_SOLID;
    rsDesc.CullMode = D3D11_CULL_BACK;
    rsDesc.FrontCounterClockwise = TRUE;
    rsDesc.DepthClipEnable = TRUE;
    m_device->CreateRasterizerState( &rsDesc, &m_rsCullOn );

    rsDesc.CullMode = D3D11_CULL_NONE;
    m_device->CreateRasterizerState( &rsDesc, &m_rsCullOff );

    rsDesc.CullMode = D3D11_CULL_BACK;
    rsDesc.DepthBias = -1;
    rsDesc.SlopeScaledDepthBias = -1.0f;
    m_device->CreateRasterizerState( &rsDesc, &m_rsCullOnPolyOffset );

    rsDesc.CullMode = D3D11_CULL_NONE;
    m_device->CreateRasterizerState( &rsDesc, &m_rsCullOffPolyOffset );

    // Sampler states
    D3D11_SAMPLER_DESC sampDesc = {};
    sampDesc.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
    sampDesc.AddressU = D3D11_TEXTURE_ADDRESS_WRAP;
    sampDesc.AddressV = D3D11_TEXTURE_ADDRESS_WRAP;
    sampDesc.AddressW = D3D11_TEXTURE_ADDRESS_WRAP;
    sampDesc.MaxAnisotropy = 1;
    sampDesc.ComparisonFunc = D3D11_COMPARISON_NEVER;
    sampDesc.MaxLOD = D3D11_FLOAT32_MAX;
    m_device->CreateSamplerState( &sampDesc, &m_samplerLinear );

    sampDesc.Filter = D3D11_FILTER_MIN_MAG_MIP_POINT;
    m_device->CreateSamplerState( &sampDesc, &m_samplerNearest );
}


void RenderBackendDX::ApplyRasterizerState()
{
    if ( m_cullEnabled && m_polyOffsetEnabled )
    {
        m_context->RSSetState( m_rsCullOnPolyOffset );
    }
    else if ( m_cullEnabled )
    {
        m_context->RSSetState( m_rsCullOn );
    }
    else if ( m_polyOffsetEnabled )
    {
        m_context->RSSetState( m_rsCullOffPolyOffset );
    }
    else
    {
        m_context->RSSetState( m_rsCullOff );
    }
}


bool RenderBackendDX::Init( HWND hwnd, HDC /*hdc*/, int width, int height )
{
    s_instance = this;
    m_width = width;
    m_height = height;

    DXGI_SWAP_CHAIN_DESC scd = {};
    scd.BufferCount = 1;
    scd.BufferDesc.Width = (UINT)width;
    scd.BufferDesc.Height = (UINT)height;
    scd.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    scd.BufferDesc.RefreshRate.Numerator = 60;
    scd.BufferDesc.RefreshRate.Denominator = 1;
    scd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    scd.OutputWindow = hwnd;
    scd.SampleDesc.Count = 1;
    scd.Windowed = TRUE;
    scd.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;

    D3D_FEATURE_LEVEL featureLevel;
    UINT flags = 0;
#ifdef _DEBUG
    flags |= D3D11_CREATE_DEVICE_DEBUG;
#endif

    HRESULT hr = D3D11CreateDeviceAndSwapChain( nullptr,
                                                D3D_DRIVER_TYPE_HARDWARE,
                                                nullptr,
                                                flags,
                                                nullptr,
                                                0,
                                                D3D11_SDK_VERSION,
                                                &scd,
                                                &m_swapChain,
                                                &m_device,
                                                &featureLevel,
                                                &m_context );
    if ( FAILED( hr ) )
    {
        throw std::runtime_error( "D3D11CreateDeviceAndSwapChain failed" );
    }

    // Create back buffer RTV
    ID3D11Texture2D* backBuffer = nullptr;
    m_swapChain->GetBuffer( 0, __uuidof( ID3D11Texture2D ), (void**)&backBuffer );
    m_device->CreateRenderTargetView( backBuffer, nullptr, &m_backBufferRTV );
    backBuffer->Release();

    // Create depth stencil
    D3D11_TEXTURE2D_DESC depthDesc = {};
    depthDesc.Width = (UINT)width;
    depthDesc.Height = (UINT)height;
    depthDesc.MipLevels = 1;
    depthDesc.ArraySize = 1;
    depthDesc.Format = DXGI_FORMAT_D24_UNORM_S8_UINT;
    depthDesc.SampleDesc.Count = 1;
    depthDesc.Usage = D3D11_USAGE_DEFAULT;
    depthDesc.BindFlags = D3D11_BIND_DEPTH_STENCIL;
    m_device->CreateTexture2D( &depthDesc, nullptr, &m_depthStencilTex );
    m_device->CreateDepthStencilView( m_depthStencilTex, nullptr, &m_depthStencilView );

    // Set default render target
    m_context->OMSetRenderTargets( 1, &m_backBufferRTV, m_depthStencilView );

    // Create state objects
    CreateStateObjects();

    // Apply initial state
    m_context->OMSetDepthStencilState( m_dsDepthOn, 0 );
    float blendFactor[4] = { 0, 0, 0, 0 };
    m_context->OMSetBlendState( m_blendOff, blendFactor, 0xFFFFFFFF );
    ApplyRasterizerState();

    // Set viewport
    D3D11_VIEWPORT vp = {};
    vp.Width = (float)width;
    vp.Height = (float)height;
    vp.MaxDepth = 1.0f;
    m_context->RSSetViewports( 1, &vp );

    return true;
}


void RenderBackendDX::Shutdown()
{
    // Destroy dynamic VBs
    for ( auto& dvb : m_dynamicVBs )
    {
        if ( dvb.inputLayout )
        {
            dvb.inputLayout->Release();
        }
        if ( dvb.vb )
        {
            dvb.vb->Release();
        }
    }
    m_dynamicVBs.clear();

    // Destroy instanced meshes
    for ( auto& im : m_instancedMeshes )
    {
        if ( im.inputLayout )
        {
            im.inputLayout->Release();
        }
        if ( im.instanceVB )
        {
            im.instanceVB->Release();
        }
        if ( im.staticVB )
        {
            im.staticVB->Release();
        }
    }
    m_instancedMeshes.clear();

    // Destroy textures
    for ( auto& entry : m_textures )
    {
        if ( entry.owned )
        {
            if ( entry.srv )
            {
                entry.srv->Release();
            }
            if ( entry.tex )
            {
                entry.tex->Release();
            }
            if ( entry.sampler )
            {
                entry.sampler->Release();
            }
        }
    }
    m_textures.clear();

    // State objects
    if ( m_samplerNearest )
    {
        m_samplerNearest->Release();
    }
    if ( m_samplerLinear )
    {
        m_samplerLinear->Release();
    }
    if ( m_rsCullOffPolyOffset )
    {
        m_rsCullOffPolyOffset->Release();
    }
    if ( m_rsCullOnPolyOffset )
    {
        m_rsCullOnPolyOffset->Release();
    }
    if ( m_rsCullOff )
    {
        m_rsCullOff->Release();
    }
    if ( m_rsCullOn )
    {
        m_rsCullOn->Release();
    }
    if ( m_blendOff )
    {
        m_blendOff->Release();
    }
    if ( m_blendOn )
    {
        m_blendOn->Release();
    }
    if ( m_dsDepthOff )
    {
        m_dsDepthOff->Release();
    }
    if ( m_dsDepthOn )
    {
        m_dsDepthOn->Release();
    }

    if ( m_depthStencilView )
    {
        m_depthStencilView->Release();
    }
    if ( m_depthStencilTex )
    {
        m_depthStencilTex->Release();
    }
    if ( m_backBufferRTV )
    {
        m_backBufferRTV->Release();
    }
    if ( m_swapChain )
    {
        m_swapChain->Release();
    }
    if ( m_context )
    {
        m_context->Release();
    }
    if ( m_device )
    {
        m_device->Release();
    }

    s_instance = nullptr;
}


void RenderBackendDX::Present()
{
    m_swapChain->Present( 1, 0 );
}


void RenderBackendDX::Finish()
{
    m_context->Flush();
}


void RenderBackendDX::Resize( int width, int height )
{
    if ( width <= 0 || height <= 0 )
    {
        return;
    }

    // Release all swap-chain-backed views before resizing
    m_context->OMSetRenderTargets( 0, nullptr, nullptr );
    m_backBufferRTV->Release();
    m_depthStencilView->Release();
    m_depthStencilTex->Release();
    m_context->ClearState();
    m_context->Flush();

    // Resize swap chain buffers
    HRESULT hr = m_swapChain->ResizeBuffers( 0, (UINT)width, (UINT)height, DXGI_FORMAT_UNKNOWN, 0 );
    if ( FAILED( hr ) )
    {
        throw std::runtime_error( "IDXGISwapChain::ResizeBuffers failed" );
    }

    // Recreate back buffer RTV
    ID3D11Texture2D* backBuffer = nullptr;
    m_swapChain->GetBuffer( 0, __uuidof( ID3D11Texture2D ), (void**)&backBuffer );
    m_device->CreateRenderTargetView( backBuffer, nullptr, &m_backBufferRTV );
    backBuffer->Release();

    // Recreate depth stencil at new size
    D3D11_TEXTURE2D_DESC depthDesc = {};
    depthDesc.Width = (UINT)width;
    depthDesc.Height = (UINT)height;
    depthDesc.MipLevels = 1;
    depthDesc.ArraySize = 1;
    depthDesc.Format = DXGI_FORMAT_D24_UNORM_S8_UINT;
    depthDesc.SampleDesc.Count = 1;
    depthDesc.Usage = D3D11_USAGE_DEFAULT;
    depthDesc.BindFlags = D3D11_BIND_DEPTH_STENCIL;
    m_device->CreateTexture2D( &depthDesc, nullptr, &m_depthStencilTex );
    m_device->CreateDepthStencilView( m_depthStencilTex, nullptr, &m_depthStencilView );

    // Rebind render targets
    m_context->OMSetRenderTargets( 1, &m_backBufferRTV, m_depthStencilView );

    // Reapply tracked state (ClearState reset everything)
    m_context->OMSetDepthStencilState( m_depthTestEnabled ? m_dsDepthOn : m_dsDepthOff, 0 );
    float blendFactor[4] = { 0, 0, 0, 0 };
    m_context->OMSetBlendState( m_blendEnabled ? m_blendOn : m_blendOff, blendFactor, 0xFFFFFFFF );
    ApplyRasterizerState();

    // Update viewport and dimensions
    m_width = width;
    m_height = height;
    SetViewport( 0, 0, width, height );
}


void RenderBackendDX::SetViewport( int x, int y, int w, int h )
{
    D3D11_VIEWPORT vp = {};
    vp.TopLeftX = (float)x;
    vp.TopLeftY = (float)y;
    vp.Width = (float)w;
    vp.Height = (float)h;
    vp.MaxDepth = 1.0f;
    m_context->RSSetViewports( 1, &vp );
}


void RenderBackendDX::Clear( bool color, bool depth )
{
    // Query the currently bound render target (may be back buffer or FBO)
    ID3D11RenderTargetView* rtv = nullptr;
    ID3D11DepthStencilView* dsv = nullptr;
    m_context->OMGetRenderTargets( 1, &rtv, &dsv );

    if ( color && rtv )
    {
        m_context->ClearRenderTargetView( rtv, m_clearColor );
    }
    if ( depth && dsv )
    {
        m_context->ClearDepthStencilView( dsv, D3D11_CLEAR_DEPTH, m_clearDepth, 0 );
    }

    if ( rtv )
    {
        rtv->Release();
    }
    if ( dsv )
    {
        dsv->Release();
    }
}


void RenderBackendDX::SetClearColor( float r, float g, float b, float a )
{
    m_clearColor[0] = r;
    m_clearColor[1] = g;
    m_clearColor[2] = b;
    m_clearColor[3] = a;
}


void RenderBackendDX::SetClearDepth( float depth )
{
    m_clearDepth = depth;
}


void RenderBackendDX::SetDepthTest( bool enable )
{
    m_depthTestEnabled = enable;
    m_context->OMSetDepthStencilState( enable ? m_dsDepthOn : m_dsDepthOff, 0 );
}


void RenderBackendDX::SetBlend( bool enable )
{
    m_blendEnabled = enable;
    float blendFactor[4] = { 0, 0, 0, 0 };
    m_context->OMSetBlendState( enable ? m_blendOn : m_blendOff, blendFactor, 0xFFFFFFFF );
}


void RenderBackendDX::SetBlendFunc( BlendFactor src, BlendFactor dst )
{
    // Recreate blend state with specified factors
    D3D11_BLEND_DESC blendDesc = {};
    blendDesc.RenderTarget[0].BlendEnable = TRUE;
    blendDesc.RenderTarget[0].SrcBlend = TranslateBlendFactor( src );
    blendDesc.RenderTarget[0].DestBlend = TranslateBlendFactor( dst );
    blendDesc.RenderTarget[0].BlendOp = D3D11_BLEND_OP_ADD;
    blendDesc.RenderTarget[0].SrcBlendAlpha = D3D11_BLEND_ONE;
    blendDesc.RenderTarget[0].DestBlendAlpha = D3D11_BLEND_ZERO;
    blendDesc.RenderTarget[0].BlendOpAlpha = D3D11_BLEND_OP_ADD;
    blendDesc.RenderTarget[0].RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL;

    if ( m_blendOn )
    {
        m_blendOn->Release();
    }
    m_device->CreateBlendState( &blendDesc, &m_blendOn );

    if ( m_blendEnabled )
    {
        float blendFactor4[4] = { 0, 0, 0, 0 };
        m_context->OMSetBlendState( m_blendOn, blendFactor4, 0xFFFFFFFF );
    }
}


void RenderBackendDX::SetCullFace( bool enable )
{
    m_cullEnabled = enable;
    ApplyRasterizerState();
}


void RenderBackendDX::SetPolygonOffset( bool enable, float /*factor*/, float /*units*/ )
{
    m_polyOffsetEnabled = enable;
    ApplyRasterizerState();
}


void RenderBackendDX::SetClipPlane( int /*index*/, bool /*enable*/ )
{
    // DX11 clip planes are handled via SV_ClipDistance in the shader.
    // The uClipPlane uniform controls the clip distance computation.
    // No DX API state change needed.
}


std::unique_ptr<IShader> RenderBackendDX::CreateShader( const char* vertPath, const char* /*fragPath*/ )
{
    // Transform GL path to HLSL path: "shaders/foo.vert" -> "shaders/foo.hlsl"
    std::string hlslPath( vertPath );
    size_t dot = hlslPath.rfind( '.' );
    if ( dot != std::string::npos )
    {
        hlslPath = hlslPath.substr( 0, dot ) + ".hlsl";
    }

    auto shader = std::make_unique<ShaderDX>( m_device, m_context );
    shader->Compile( hlslPath.c_str() );
    return shader;
}


std::unique_ptr<IMesh> RenderBackendDX::CreateMesh( const float* data, int vertexCount, bool hasNormals, bool hasTexCoords )
{
    auto mesh = std::make_unique<MeshDX>( m_device, m_context );
    mesh->Create( data, vertexCount, hasNormals, hasTexCoords );
    return mesh;
}


std::unique_ptr<IFramebuffer> RenderBackendDX::CreateFramebuffer( int width, int height )
{
    auto fbo = std::make_unique<FramebufferDX>( this, m_device, m_context );
    fbo->Create( width, height );
    return fbo;
}


uint32_t RenderBackendDX::CreateTexture2D( const uint8_t* data, int w, int h, int channels, bool generateMips, bool linearFilter )
{
    // Convert to RGBA if needed
    std::vector<uint8_t> rgbaData;
    const uint8_t* srcData = data;
    DXGI_FORMAT format = DXGI_FORMAT_R8G8B8A8_UNORM;

    if ( channels == 1 )
    {
        format = DXGI_FORMAT_R8_UNORM;
    }
    else if ( channels == 3 )
    {
        rgbaData.resize( (size_t)w * h * 4 );
        for ( int i = 0; i < w * h; ++i )
        {
            rgbaData[i * 4 + 0] = data[i * 3 + 0];
            rgbaData[i * 4 + 1] = data[i * 3 + 1];
            rgbaData[i * 4 + 2] = data[i * 3 + 2];
            rgbaData[i * 4 + 3] = 255;
        }
        srcData = rgbaData.data();
    }

    int bytesPerPixel = ( channels == 1 ) ? 1 : 4;

    D3D11_TEXTURE2D_DESC texDesc = {};
    texDesc.Width = (UINT)w;
    texDesc.Height = (UINT)h;
    texDesc.MipLevels = generateMips ? 0 : 1;
    texDesc.ArraySize = 1;
    texDesc.Format = format;
    texDesc.SampleDesc.Count = 1;
    texDesc.Usage = D3D11_USAGE_DEFAULT;
    texDesc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
    if ( generateMips )
    {
        texDesc.BindFlags |= D3D11_BIND_RENDER_TARGET;
        texDesc.MiscFlags = D3D11_RESOURCE_MISC_GENERATE_MIPS;
    }

    ID3D11Texture2D* tex = nullptr;
    HRESULT hr;

    if ( generateMips )
    {
        hr = m_device->CreateTexture2D( &texDesc, nullptr, &tex );
        if ( FAILED( hr ) )
        {
            throw std::runtime_error( "CreateTexture2D failed" );
        }

        m_context->UpdateSubresource( tex, 0, nullptr, srcData, (UINT)( w * bytesPerPixel ), 0 );
    }
    else
    {
        D3D11_SUBRESOURCE_DATA initData = {};
        initData.pSysMem = srcData;
        initData.SysMemPitch = (UINT)( w * bytesPerPixel );

        hr = m_device->CreateTexture2D( &texDesc, &initData, &tex );
        if ( FAILED( hr ) )
        {
            throw std::runtime_error( "CreateTexture2D failed" );
        }
    }

    ID3D11ShaderResourceView* srv = nullptr;
    hr = m_device->CreateShaderResourceView( tex, nullptr, &srv );
    if ( FAILED( hr ) )
    {
        tex->Release();
        throw std::runtime_error( "CreateSRV failed" );
    }

    if ( generateMips )
    {
        m_context->GenerateMips( srv );
    }

    // Choose sampler
    ID3D11SamplerState* sampler = linearFilter ? m_samplerLinear : m_samplerNearest;
    sampler->AddRef(); // keep a reference

    TextureEntryDX entry = {};
    entry.srv = srv;
    entry.tex = tex;
    entry.sampler = sampler;
    entry.owned = true;

    m_textures.push_back( entry );
    return (uint32_t)m_textures.size(); // 1-based handle
}


void RenderBackendDX::BindTexture( uint32_t handle, int slot )
{
    if ( handle == 0 || handle > (uint32_t)m_textures.size() )
    {
        return;
    }

    TextureEntryDX& entry = m_textures[handle - 1];
    m_context->PSSetShaderResources( (UINT)slot, 1, &entry.srv );
    m_context->PSSetSamplers( (UINT)slot, 1, &entry.sampler );
}


void RenderBackendDX::DeleteTexture( uint32_t handle )
{
    if ( handle == 0 || handle > (uint32_t)m_textures.size() )
    {
        return;
    }

    TextureEntryDX& entry = m_textures[handle - 1];
    if ( entry.owned )
    {
        if ( entry.sampler )
        {
            entry.sampler->Release();
            entry.sampler = nullptr;
        }
        if ( entry.srv )
        {
            entry.srv->Release();
            entry.srv = nullptr;
        }
        if ( entry.tex )
        {
            entry.tex->Release();
            entry.tex = nullptr;
        }
    }
}


uint32_t RenderBackendDX::RegisterSRV( ID3D11ShaderResourceView* srv )
{
    TextureEntryDX entry = {};
    entry.srv = srv;
    entry.sampler = m_samplerLinear;
    entry.owned = false;
    m_textures.push_back( entry );
    return (uint32_t)m_textures.size();
}


void RenderBackendDX::UnregisterSRV( uint32_t handle )
{
    if ( handle == 0 || handle > (uint32_t)m_textures.size() )
    {
        return;
    }
    TextureEntryDX& entry = m_textures[handle - 1];
    entry.srv = nullptr;
    entry.tex = nullptr;
    entry.sampler = nullptr;
    entry.owned = false;
}


std::vector<uint8_t> RenderBackendDX::CaptureBackbuffer( int& outWidth, int& outHeight )
{
    outWidth = m_width;
    outHeight = m_height;

    // Get back buffer
    ID3D11Texture2D* backBuffer = nullptr;
    m_swapChain->GetBuffer( 0, __uuidof( ID3D11Texture2D ), (void**)&backBuffer );

    // Create staging texture
    D3D11_TEXTURE2D_DESC desc;
    backBuffer->GetDesc( &desc );
    desc.Usage = D3D11_USAGE_STAGING;
    desc.BindFlags = 0;
    desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
    desc.MiscFlags = 0;

    ID3D11Texture2D* staging = nullptr;
    m_device->CreateTexture2D( &desc, nullptr, &staging );
    m_context->CopyResource( staging, backBuffer );
    backBuffer->Release();

    D3D11_MAPPED_SUBRESOURCE mapped;
    m_context->Map( staging, 0, D3D11_MAP_READ, 0, &mapped );

    // Convert RGBA to BGR bottom-up (matches GL's CaptureBackbuffer format for BMP compatibility)
    int rowBytes = m_width * 3;
    int paddedRow = ( rowBytes + 3 ) & ~3;
    std::vector<uint8_t> pixels( (size_t)paddedRow * m_height );

    for ( int y = 0; y < m_height; ++y )
    {
        const uint8_t* srcRow = (const uint8_t*)mapped.pData + (size_t)( m_height - 1 - y ) * mapped.RowPitch;
        uint8_t* dstRow = pixels.data() + (size_t)y * paddedRow;
        for ( int x = 0; x < m_width; ++x )
        {
            dstRow[x * 3 + 0] = srcRow[x * 4 + 2]; // B
            dstRow[x * 3 + 1] = srcRow[x * 4 + 1]; // G
            dstRow[x * 3 + 2] = srcRow[x * 4 + 0]; // R
        }
    }

    m_context->Unmap( staging, 0 );
    staging->Release();

    return pixels;
}


int RenderBackendDX::GetWidth() const
{
    return m_width;
}


int RenderBackendDX::GetHeight() const
{
    return m_height;
}


bool RenderBackendDX::IsDepthTestEnabled() const
{
    return m_depthTestEnabled;
}


bool RenderBackendDX::IsBlendEnabled() const
{
    return m_blendEnabled;
}


bool RenderBackendDX::UsesZeroToOneDepth() const
{
    return true;
}


// --- Dynamic Vertex Buffer ---


uint32_t RenderBackendDX::CreateDynamicVB( const int* attribComponents, int numAttribs, int maxVertices )
{
    DynamicVBDX dvb = {};
    dvb.maxVertices = maxVertices;
    dvb.numAttribs = numAttribs;
    dvb.lastVSBytecode = nullptr;

    int floatsPerVert = 0;
    for ( int i = 0; i < numAttribs && i < 8; ++i )
    {
        dvb.attribComponents[i] = attribComponents[i];
        floatsPerVert += attribComponents[i];
    }
    dvb.floatsPerVertex = floatsPerVert;
    dvb.stride = floatsPerVert * (int)sizeof( float );

    D3D11_BUFFER_DESC bd = {};
    bd.ByteWidth = (UINT)( maxVertices * dvb.stride );
    bd.Usage = D3D11_USAGE_DYNAMIC;
    bd.BindFlags = D3D11_BIND_VERTEX_BUFFER;
    bd.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
    m_device->CreateBuffer( &bd, nullptr, &dvb.vb );

    m_dynamicVBs.push_back( dvb );
    return (uint32_t)m_dynamicVBs.size();
}


void RenderBackendDX::UploadAndDrawDynamicVB( uint32_t handle, const float* data, int vertexCount )
{
    if ( handle == 0 || handle > (uint32_t)m_dynamicVBs.size() )
    {
        return;
    }
    DynamicVBDX& dvb = m_dynamicVBs[handle - 1];

    // Flush active shader CB
    if ( m_activeShader )
    {
        m_activeShader->FlushCB();
    }

    // Ensure input layout
    if ( m_activeShader && m_activeShader->GetVSBytecode() != dvb.lastVSBytecode )
    {
        if ( dvb.inputLayout )
        {
            dvb.inputLayout->Release();
            dvb.inputLayout = nullptr;
        }

        // Build input elements from attrib components
        D3D11_INPUT_ELEMENT_DESC elements[8] = {};
        UINT offset = 0;
        for ( int i = 0; i < dvb.numAttribs; ++i )
        {
            elements[i].SemanticName = ( i == 0 ) ? "POSITION" : "TEXCOORD";
            elements[i].SemanticIndex = ( i == 0 ) ? 0 : (UINT)( i - 1 );
            elements[i].InputSlot = 0;
            elements[i].AlignedByteOffset = offset;
            elements[i].InputSlotClass = D3D11_INPUT_PER_VERTEX_DATA;

            switch ( dvb.attribComponents[i] )
            {
            case 1:
                elements[i].Format = DXGI_FORMAT_R32_FLOAT;
                break;
            case 2:
                elements[i].Format = DXGI_FORMAT_R32G32_FLOAT;
                break;
            case 3:
                elements[i].Format = DXGI_FORMAT_R32G32B32_FLOAT;
                break;
            case 4:
                elements[i].Format = DXGI_FORMAT_R32G32B32A32_FLOAT;
                break;
            }
            offset += dvb.attribComponents[i] * (UINT)sizeof( float );
        }

        m_device->CreateInputLayout( elements,
                                     (UINT)dvb.numAttribs,
                                     m_activeShader->GetVSBytecode(),
                                     m_activeShader->GetVSBytecodeSize(),
                                     &dvb.inputLayout );
        dvb.lastVSBytecode = m_activeShader->GetVSBytecode();
    }

    // Upload data
    D3D11_MAPPED_SUBRESOURCE mapped;
    m_context->Map( dvb.vb, 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped );
    memcpy( mapped.pData, data, (size_t)vertexCount * dvb.floatsPerVertex * sizeof( float ) );
    m_context->Unmap( dvb.vb, 0 );

    // Draw
    m_context->IASetInputLayout( dvb.inputLayout );
    UINT stride = (UINT)dvb.stride;
    UINT vbOffset = 0;
    m_context->IASetVertexBuffers( 0, 1, &dvb.vb, &stride, &vbOffset );
    m_context->IASetPrimitiveTopology( D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST );
    m_context->Draw( (UINT)vertexCount, 0 );
}


void RenderBackendDX::DestroyDynamicVB( uint32_t handle )
{
    if ( handle == 0 || handle > (uint32_t)m_dynamicVBs.size() )
    {
        return;
    }
    DynamicVBDX& dvb = m_dynamicVBs[handle - 1];
    if ( dvb.inputLayout )
    {
        dvb.inputLayout->Release();
        dvb.inputLayout = nullptr;
    }
    if ( dvb.vb )
    {
        dvb.vb->Release();
        dvb.vb = nullptr;
    }
}


// --- Instanced Mesh ---


uint32_t RenderBackendDX::CreateInstancedMesh( const float* staticData, int staticVertCount, int staticFloatsPerVert, int maxInstances, int instanceFloats, int instanceStartAttrib, const int* instanceAttribSizes, int numInstanceAttribs )
{
    InstancedMeshDX im = {};
    im.staticFloatsPerVert = staticFloatsPerVert;
    im.staticStride = staticFloatsPerVert * (int)sizeof( float );
    im.instanceFloats = instanceFloats;
    im.instanceStride = instanceFloats * (int)sizeof( float );
    im.instanceStartAttrib = instanceStartAttrib;
    im.numInstanceAttribs = numInstanceAttribs;
    im.lastVSBytecode = nullptr;
    for ( int i = 0; i < numInstanceAttribs && i < 8; ++i )
    {
        im.instanceAttribSizes[i] = instanceAttribSizes[i];
    }

    // Static VB
    D3D11_BUFFER_DESC bd = {};
    bd.ByteWidth = (UINT)( staticVertCount * im.staticStride );
    bd.Usage = D3D11_USAGE_IMMUTABLE;
    bd.BindFlags = D3D11_BIND_VERTEX_BUFFER;
    D3D11_SUBRESOURCE_DATA initData = {};
    initData.pSysMem = staticData;
    m_device->CreateBuffer( &bd, &initData, &im.staticVB );

    // Instance VB
    bd.ByteWidth = (UINT)( maxInstances * im.instanceStride );
    bd.Usage = D3D11_USAGE_DYNAMIC;
    bd.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
    m_device->CreateBuffer( &bd, nullptr, &im.instanceVB );

    m_instancedMeshes.push_back( im );
    return (uint32_t)m_instancedMeshes.size();
}


void RenderBackendDX::UploadInstanceData( uint32_t handle, const float* data, int floatCount )
{
    if ( handle == 0 || handle > (uint32_t)m_instancedMeshes.size() )
    {
        return;
    }
    InstancedMeshDX& im = m_instancedMeshes[handle - 1];

    D3D11_MAPPED_SUBRESOURCE mapped;
    m_context->Map( im.instanceVB, 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped );
    memcpy( mapped.pData, data, (size_t)floatCount * sizeof( float ) );
    m_context->Unmap( im.instanceVB, 0 );
}


void RenderBackendDX::DrawInstancedMesh( uint32_t handle, int staticVertCount, int instanceCount )
{
    if ( handle == 0 || handle > (uint32_t)m_instancedMeshes.size() )
    {
        return;
    }
    InstancedMeshDX& im = m_instancedMeshes[handle - 1];

    // Flush active shader CB
    if ( m_activeShader )
    {
        m_activeShader->FlushCB();
    }

    // Ensure input layout
    if ( m_activeShader && m_activeShader->GetVSBytecode() != im.lastVSBytecode )
    {
        if ( im.inputLayout )
        {
            im.inputLayout->Release();
            im.inputLayout = nullptr;
        }

        // Build: slot 0 = static (POSITION float3), slot 1 = instance data
        D3D11_INPUT_ELEMENT_DESC elements[16] = {};
        int numElements = 0;

        // Static: POSITION
        elements[0].SemanticName = "POSITION";
        elements[0].SemanticIndex = 0;
        elements[0].Format = ( im.staticFloatsPerVert == 3 ) ? DXGI_FORMAT_R32G32B32_FLOAT : DXGI_FORMAT_R32G32_FLOAT;
        elements[0].InputSlot = 0;
        elements[0].AlignedByteOffset = 0;
        elements[0].InputSlotClass = D3D11_INPUT_PER_VERTEX_DATA;
        numElements = 1;

        // Instance attributes
        UINT instOffset = 0;
        for ( int i = 0; i < im.numInstanceAttribs; ++i )
        {
            elements[numElements].SemanticName = "TEXCOORD";
            elements[numElements].SemanticIndex = (UINT)( im.instanceStartAttrib + i - 2 );
            elements[numElements].InputSlot = 1;
            elements[numElements].AlignedByteOffset = instOffset;
            elements[numElements].InputSlotClass = D3D11_INPUT_PER_INSTANCE_DATA;
            elements[numElements].InstanceDataStepRate = 1;

            switch ( im.instanceAttribSizes[i] )
            {
            case 1:
                elements[numElements].Format = DXGI_FORMAT_R32_FLOAT;
                break;
            case 2:
                elements[numElements].Format = DXGI_FORMAT_R32G32_FLOAT;
                break;
            case 3:
                elements[numElements].Format = DXGI_FORMAT_R32G32B32_FLOAT;
                break;
            case 4:
                elements[numElements].Format = DXGI_FORMAT_R32G32B32A32_FLOAT;
                break;
            }

            instOffset += im.instanceAttribSizes[i] * (UINT)sizeof( float );
            numElements++;
        }

        m_device->CreateInputLayout( elements,
                                     (UINT)numElements,
                                     m_activeShader->GetVSBytecode(),
                                     m_activeShader->GetVSBytecodeSize(),
                                     &im.inputLayout );
        im.lastVSBytecode = m_activeShader->GetVSBytecode();
    }

    // Bind both VBs
    ID3D11Buffer* vbs[2] = { im.staticVB, im.instanceVB };
    UINT strides[2] = { (UINT)im.staticStride, (UINT)im.instanceStride };
    UINT offsets[2] = { 0, 0 };
    m_context->IASetInputLayout( im.inputLayout );
    m_context->IASetVertexBuffers( 0, 2, vbs, strides, offsets );
    m_context->IASetPrimitiveTopology( D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST );
    m_context->DrawInstanced( (UINT)staticVertCount, (UINT)instanceCount, 0, 0 );
}


void RenderBackendDX::DestroyInstancedMesh( uint32_t handle )
{
    if ( handle == 0 || handle > (uint32_t)m_instancedMeshes.size() )
    {
        return;
    }
    InstancedMeshDX& im = m_instancedMeshes[handle - 1];
    if ( im.inputLayout )
    {
        im.inputLayout->Release();
        im.inputLayout = nullptr;
    }
    if ( im.instanceVB )
    {
        im.instanceVB->Release();
        im.instanceVB = nullptr;
    }
    if ( im.staticVB )
    {
        im.staticVB->Release();
        im.staticVB = nullptr;
    }
}