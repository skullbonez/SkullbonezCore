// --- Includes ---
#include "SkullbonezFramebufferDX12.h"
#include "SkullbonezRenderBackendDX12.h"
#include <stdexcept>


// --- Usings ---
using namespace SkullbonezCore::Rendering;


FramebufferDX12::FramebufferDX12()
    : m_colorTexture( nullptr ), m_depthTexture( nullptr ), m_srvIndex( 0 ), m_texHandle( 0 ), m_width( 0 ), m_height( 0 )
{
    m_rtvHandle = {};
    m_dsvHandle = {};
    m_savedRTV = {};
    m_savedDSV = {};
}


FramebufferDX12::~FramebufferDX12()
{
    ResetResources();
}


void FramebufferDX12::Create( int width, int height )
{
    auto* backend = RenderBackendDX12::Get();
    if ( !backend )
    {
        throw std::runtime_error( "FramebufferDX12::Create: no DX12 backend" );
    }

    m_width = width;
    m_height = height;
    auto* device = backend->GetDevice();

    // Color texture (RENDER_TARGET + PIXEL_SHADER_RESOURCE)
    D3D12_HEAP_PROPERTIES defaultHeap = {};
    defaultHeap.Type = D3D12_HEAP_TYPE_DEFAULT;

    D3D12_RESOURCE_DESC colorDesc = {};
    colorDesc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    colorDesc.Width = (UINT64)width;
    colorDesc.Height = (UINT)height;
    colorDesc.DepthOrArraySize = 1;
    colorDesc.MipLevels = 1;
    colorDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    colorDesc.SampleDesc.Count = 1;
    colorDesc.Flags = D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;

    float clearColor[4] = { 0.0f, 0.0f, 0.0f, 1.0f };
    D3D12_CLEAR_VALUE colorClear = {};
    colorClear.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    memcpy( colorClear.Color, clearColor, sizeof( clearColor ) );

    device->CreateCommittedResource( &defaultHeap, D3D12_HEAP_FLAG_NONE, &colorDesc, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, &colorClear, IID_PPV_ARGS( &m_colorTexture ) );

    // Depth texture
    D3D12_RESOURCE_DESC depthDesc = {};
    depthDesc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    depthDesc.Width = (UINT64)width;
    depthDesc.Height = (UINT)height;
    depthDesc.DepthOrArraySize = 1;
    depthDesc.MipLevels = 1;
    depthDesc.Format = DXGI_FORMAT_D24_UNORM_S8_UINT;
    depthDesc.SampleDesc.Count = 1;
    depthDesc.Flags = D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL;

    D3D12_CLEAR_VALUE depthClear = {};
    depthClear.Format = DXGI_FORMAT_D24_UNORM_S8_UINT;
    depthClear.DepthStencil.Depth = 1.0f;

    device->CreateCommittedResource( &defaultHeap, D3D12_HEAP_FLAG_NONE, &depthDesc, D3D12_RESOURCE_STATE_DEPTH_WRITE, &depthClear, IID_PPV_ARGS( &m_depthTexture ) );

    // Allocate RTV and DSV from backend's heaps
    m_rtvHandle = backend->AllocateRTV();
    device->CreateRenderTargetView( m_colorTexture, nullptr, m_rtvHandle );

    m_dsvHandle = backend->AllocateDSV();
    D3D12_DEPTH_STENCIL_VIEW_DESC dsvDesc = {};
    dsvDesc.Format = DXGI_FORMAT_D24_UNORM_S8_UINT;
    dsvDesc.ViewDimension = D3D12_DSV_DIMENSION_TEXTURE2D;
    device->CreateDepthStencilView( m_depthTexture, &dsvDesc, m_dsvHandle );

    // Create SRV for color texture
    m_srvIndex = backend->AllocateStaticSRV();
    D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
    srvDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
    srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    srvDesc.Texture2D.MipLevels = 1;
    device->CreateShaderResourceView( m_colorTexture, &srvDesc, backend->GetSRVStagingCpuHandle( m_srvIndex ) );

    // Register SRV so GetColorTextureHandle() returns a valid texture handle
    m_texHandle = backend->RegisterSRV( m_srvIndex );
}


void FramebufferDX12::Bind() const
{
    auto* backend = RenderBackendDX12::Get();
    if ( !backend )
    {
        return;
    }

    // Save current targets
    m_savedRTV = backend->GetCurrentRTV();
    m_savedDSV = backend->GetCurrentDSV();

    // Clear stale texture bindings for the FBO color texture before transition
    backend->SetRenderingToFBO( true, m_srvIndex );

    // Transition color texture from SRV to render target
    auto* cmdList = backend->GetCommandList();
    D3D12_RESOURCE_BARRIER barrier = {};
    barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    barrier.Transition.pResource = m_colorTexture;
    barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
    barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_RENDER_TARGET;
    barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    cmdList->ResourceBarrier( 1, &barrier );

    backend->SetCurrentTargets( m_rtvHandle, m_dsvHandle );
}


void FramebufferDX12::Unbind() const
{
    auto* backend = RenderBackendDX12::Get();
    if ( !backend )
    {
        return;
    }

    // Transition color texture back to SRV
    auto* cmdList = backend->GetCommandList();
    D3D12_RESOURCE_BARRIER barrier = {};
    barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    barrier.Transition.pResource = m_colorTexture;
    barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_RENDER_TARGET;
    barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
    barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    cmdList->ResourceBarrier( 1, &barrier );

    backend->SetRenderingToFBO( false );
    backend->SetCurrentTargets( m_savedRTV, m_savedDSV );
}


void FramebufferDX12::ResetResources()
{
    auto* backend = RenderBackendDX12::Get();
    if ( backend && m_texHandle != 0 )
    {
        backend->UnregisterSRV( m_texHandle );
    }

    if ( m_colorTexture )
    {
        m_colorTexture->Release();
        m_colorTexture = nullptr;
    }
    if ( m_depthTexture )
    {
        m_depthTexture->Release();
        m_depthTexture = nullptr;
    }
    m_texHandle = 0;
}
