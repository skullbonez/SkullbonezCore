// --- Includes ---
#include "SkullbonezFramebufferDX12.h"
#include "SkullbonezRenderBackendDX12.h"
#include <stdexcept>


// --- Usings ---
using namespace SkullbonezCore::Rendering;


static DXGI_FORMAT ToDX12ColorFormat( FramebufferColorFormat format )
{
    // Keep the public engine enum small and translate it once at the DX12 edge.
    // RGBA16F is the HDR format used by cinematic rendering.
    return ( format == FramebufferColorFormat::RGBA16F ) ? DXGI_FORMAT_R16G16B16A16_FLOAT : DXGI_FORMAT_R8G8B8A8_UNORM;
}


FramebufferDX12::FramebufferDX12( FramebufferColorFormat colorFormat )
    : m_colorTexture( nullptr ), m_depthTexture( nullptr ), m_srvIndex( 0 ), m_depthSrvIndex( 0 ), m_texHandle( 0 ), m_depthTexHandle( 0 ), m_colorFormat( colorFormat ), m_width( 0 ), m_height( 0 ), m_depthState( D3D12_RESOURCE_STATE_DEPTH_WRITE )
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
    // The color format is chosen by the render path. Reflections use normal
    // RGBA8; cinematic scene targets use RGBA16F to keep over-bright light.
    const DXGI_FORMAT colorFormat = ToDX12ColorFormat( m_colorFormat );
    colorDesc.Format = colorFormat;
    colorDesc.SampleDesc.Count = 1;
    colorDesc.Flags = D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;

    float clearColor[4] = { 0.0f, 0.0f, 0.0f, 1.0f };
    D3D12_CLEAR_VALUE colorClear = {};
    colorClear.Format = colorFormat;
    memcpy( colorClear.Color, clearColor, sizeof( clearColor ) );

    // Allocate GPU memory and create a committed resource for the off-screen color texture.
    // "Committed" means this texture gets its own dedicated GPU memory allocation. The initial
    // state is PIXEL_SHADER_RESOURCE because when not actively rendering to it, shaders read it.
    // Docs: https://learn.microsoft.com/en-us/windows/win32/api/d3d12/nf-d3d12-id3d12device-createcommittedresource
    device->CreateCommittedResource( &defaultHeap, D3D12_HEAP_FLAG_NONE, &colorDesc, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, &colorClear, IID_PPV_ARGS( &m_colorTexture ) );

    // Depth texture
    D3D12_RESOURCE_DESC depthDesc = {};
    depthDesc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    depthDesc.Width = (UINT64)width;
    depthDesc.Height = (UINT)height;
    depthDesc.DepthOrArraySize = 1;
    depthDesc.MipLevels = 1;
    // Typeless depth lets us create two descriptors over the same resource:
    // one DSV for depth testing and one SRV for post-process sampling.
    depthDesc.Format = DXGI_FORMAT_R24G8_TYPELESS;
    depthDesc.SampleDesc.Count = 1;
    depthDesc.Flags = D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL;

    D3D12_CLEAR_VALUE depthClear = {};
    depthClear.Format = DXGI_FORMAT_D24_UNORM_S8_UINT;
    depthClear.DepthStencil.Depth = 1.0f;

    // Allocate GPU memory and create a committed resource for the depth/stencil texture.
    // This stores per-pixel depth values so the GPU knows which objects are in front of others.
    // Initial state is DEPTH_WRITE because we clear and write depth whenever this FBO is bound.
    // Docs: https://learn.microsoft.com/en-us/windows/win32/api/d3d12/nf-d3d12-id3d12device-createcommittedresource
    device->CreateCommittedResource( &defaultHeap, D3D12_HEAP_FLAG_NONE, &depthDesc, D3D12_RESOURCE_STATE_DEPTH_WRITE, &depthClear, IID_PPV_ARGS( &m_depthTexture ) );

    // Allocate RTV and DSV from backend's heaps
    m_rtvHandle = backend->AllocateRTV();
    // Create a Render Target View (RTV) — this is a "descriptor" that tells the GPU how to
    // interpret the color texture when writing pixels to it during rendering. Without this view,
    // the GPU cannot use the texture as a render target.
    // Docs: https://learn.microsoft.com/en-us/windows/win32/api/d3d12/nf-d3d12-id3d12device-createrendertargetview
    device->CreateRenderTargetView( m_colorTexture, nullptr, m_rtvHandle );

    m_dsvHandle = backend->AllocateDSV();
    D3D12_DEPTH_STENCIL_VIEW_DESC dsvDesc = {};
    dsvDesc.Format = DXGI_FORMAT_D24_UNORM_S8_UINT;
    dsvDesc.ViewDimension = D3D12_DSV_DIMENSION_TEXTURE2D;
    // Create a Depth Stencil View (DSV) — this descriptor tells the GPU how to read/write
    // the depth texture during depth testing. Pixels that fail the depth test are discarded.
    // Docs: https://learn.microsoft.com/en-us/windows/win32/api/d3d12/nf-d3d12-id3d12device-createdepthstencilview
    device->CreateDepthStencilView( m_depthTexture, &dsvDesc, m_dsvHandle );

    // Create SRV for color texture
    m_srvIndex = backend->AllocateStaticSRV();
    D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
    srvDesc.Format = colorFormat;
    srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
    srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    srvDesc.Texture2D.MipLevels = 1;
    // Create a Shader Resource View (SRV) — this descriptor allows shaders to sample/read the
    // color texture (e.g. the water shader reads the reflection FBO as a texture input).
    // The same GPU resource can have multiple views: RTV for writing, SRV for reading.
    // Docs: https://learn.microsoft.com/en-us/windows/win32/api/d3d12/nf-d3d12-id3d12device-createshaderresourceview
    device->CreateShaderResourceView( m_colorTexture, &srvDesc, backend->GetSRVStagingCpuHandle( m_srvIndex ) );

    // Register SRV so GetColorTextureHandle() returns a valid texture handle
    m_texHandle = backend->RegisterSRV( m_srvIndex );

    // Depth SRV: the tonemap/volumetric shaders sample this to know where solid
    // geometry blocks fog and rays. It reads only the 24-bit depth component.
    m_depthSrvIndex = backend->AllocateStaticSRV();
    D3D12_SHADER_RESOURCE_VIEW_DESC depthSrvDesc = {};
    depthSrvDesc.Format = DXGI_FORMAT_R24_UNORM_X8_TYPELESS;
    depthSrvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
    depthSrvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    depthSrvDesc.Texture2D.MipLevels = 1;
    device->CreateShaderResourceView( m_depthTexture, &depthSrvDesc, backend->GetSRVStagingCpuHandle( m_depthSrvIndex ) );
    m_depthTexHandle = backend->RegisterSRV( m_depthSrvIndex );
    m_depthState = D3D12_RESOURCE_STATE_DEPTH_WRITE;
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

    // Clear stale texture bindings for the FBO textures before transition
    backend->SetRenderingToFBO( true, m_srvIndex, m_depthSrvIndex, ToDX12ColorFormat( m_colorFormat ) );

    // Transition color texture from SRV to render target.
    // In DX12, resources must be explicitly transitioned between states. The GPU needs to know
    // when a texture switches from being read by a shader (SRV) to being written to (RENDER_TARGET).
    // Failing to do this causes GPU corruption or validation errors — the driver does NOT track this for you.
    // Docs: https://learn.microsoft.com/en-us/windows/win32/api/d3d12/nf-d3d12-id3d12graphicscommandlist-resourcebarrier
    auto* cmdList = backend->GetCommandList();
    D3D12_RESOURCE_BARRIER barrier = {};
    barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    barrier.Transition.pResource = m_colorTexture;
    barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
    barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_RENDER_TARGET;
    barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    cmdList->ResourceBarrier( 1, &barrier );

    if ( m_depthState != D3D12_RESOURCE_STATE_DEPTH_WRITE )
    {
        // Unlike DX11/GL, DX12 requires us to explicitly say when a texture stops
        // being sampled by shaders and starts being written as a depth buffer.
        D3D12_RESOURCE_BARRIER depthBarrier = {};
        depthBarrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        depthBarrier.Transition.pResource = m_depthTexture;
        depthBarrier.Transition.StateBefore = m_depthState;
        depthBarrier.Transition.StateAfter = D3D12_RESOURCE_STATE_DEPTH_WRITE;
        depthBarrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
        cmdList->ResourceBarrier( 1, &depthBarrier );
        m_depthState = D3D12_RESOURCE_STATE_DEPTH_WRITE;
    }

    backend->SetCurrentTargets( m_rtvHandle, m_dsvHandle );
}


void FramebufferDX12::Unbind() const
{
    auto* backend = RenderBackendDX12::Get();
    if ( !backend )
    {
        return;
    }

    // Transition color texture back from RENDER_TARGET to PIXEL_SHADER_RESOURCE.
    // Now that we're done drawing into this FBO, we transition it back so shaders can read it.
    // This is the reverse of the Bind() transition — every state change must be paired.
    // Docs: https://learn.microsoft.com/en-us/windows/win32/api/d3d12/nf-d3d12-id3d12graphicscommandlist-resourcebarrier
    auto* cmdList = backend->GetCommandList();
    D3D12_RESOURCE_BARRIER barrier = {};
    barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    barrier.Transition.pResource = m_colorTexture;
    barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_RENDER_TARGET;
    barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
    barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    cmdList->ResourceBarrier( 1, &barrier );

    if ( m_depthState != D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE )
    {
        // After rendering, flip depth back into a shader-readable state so the
        // following full-screen post passes can sample it safely.
        D3D12_RESOURCE_BARRIER depthBarrier = {};
        depthBarrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        depthBarrier.Transition.pResource = m_depthTexture;
        depthBarrier.Transition.StateBefore = m_depthState;
        depthBarrier.Transition.StateAfter = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
        depthBarrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
        cmdList->ResourceBarrier( 1, &depthBarrier );
        m_depthState = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
    }

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
    if ( backend && m_depthTexHandle != 0 )
    {
        backend->UnregisterSRV( m_depthTexHandle );
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
    m_depthTexHandle = 0;
    m_depthSrvIndex = 0;
    m_depthState = D3D12_RESOURCE_STATE_DEPTH_WRITE;
}
