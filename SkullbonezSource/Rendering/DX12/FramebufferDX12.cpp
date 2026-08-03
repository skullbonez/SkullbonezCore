/*
File: SkullbonezSource/Rendering/DX12/FramebufferDX12.cpp
Purpose:
  Implements off-screen framebuffer resources and descriptor views for the DX12 renderer.

Summary:
  FramebufferDX12 owns off-screen color and depth resources with render-pass
  write views and later-pass shader views, while borrowing the descriptor owner
  that bounds their lifetime.

Invariants:
  - DX12 object lifetime, resource states, descriptor rows, and fence ordering
    must stay explicit.
  - Render-graph callbacks own every resource transition; Bind/Unbind changes
    output descriptors only.
  - FramebufferDX12 is created from initialized concrete owners; missing device state
    means the render owner lifetime contract was broken before resource
    creation.

Related:
  - SkullbonezSource/Rendering/DX12/FramebufferDX12.h
  - Agentic/Reference/skullbonez-core-class-structure.md
  - Agentic/Reference/comment-style-guide.md
  - Agentic/Reference/engine-glossary.md
*/
#include "FramebufferDX12.h"
#include "../../Core/FatalError.h"
#include "../../Core/Log.h"
#include "RenderBackendDX12.h"
#include "../RenderGraph.h"


using namespace SkullbonezCore::Rendering;


static DXGI_FORMAT ToDX12ColorFormat( FramebufferColorFormat format )
{

    // Keep the public engine enum small and translate it once at the DX12 edge.
    // RGBA16F is the HDR format used by cinematic rendering.
    return ( format == FramebufferColorFormat::RGBA16F ) ? DXGI_FORMAT_R16G16B16A16_FLOAT : DXGI_FORMAT_R8G8B8A8_UNORM;
}


FramebufferDX12::FramebufferDX12( Dx12RenderDevice& device, Dx12PipelineOwner& pipeline, Dx12TextureOwner& textures,
                                  Dx12DescriptorHeaps& descriptors, Dx12DrawGate& drawGate,
                                  Dx12ResourceRelease& resourceRelease, FramebufferColorFormat colorFormat )
    : m_device( device ), m_pipeline( pipeline ), m_textures( textures ), m_descriptors( descriptors ),
      m_drawGate( drawGate ), m_resourceRelease( resourceRelease ), m_colorTexture( nullptr ), m_depthTexture( nullptr ),
      m_rtvIndex( UINT_MAX ), m_dsvIndex( UINT_MAX ), m_srvIndex( UINT_MAX ), m_depthSrvIndex( UINT_MAX ), m_texHandle( 0 ),
      m_depthTexHandle( 0 ), m_colorFormat( colorFormat ), m_width( 0 ), m_height( 0 )
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


bool FramebufferDX12::Create( int width, int height )
{

    // Invariant: off-screen targets borrow stable descriptor, texture, pipeline,
    // retirement, and device owners. Without a device there is no recoverable
    // framebuffer creation boundary inside this helper.

    if ( !m_device.Device() )
    {
        SB_FATAL( "FramebufferDX12", "Create requires an initialized DX12 backend." );
    }

    auto* device = m_device.Device();

    // Color texture (RENDER_TARGET + PIXEL_SHADER_RESOURCE)
    D3D12_HEAP_PROPERTIES defaultHeap = {};
    defaultHeap.Type = D3D12_HEAP_TYPE_DEFAULT;

    D3D12_RESOURCE_DESC colorDesc = {};
    colorDesc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    colorDesc.Width = static_cast<UINT64>( width );
    colorDesc.Height = static_cast<UINT>( height );
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
    const HRESULT colorResult = device->CreateCommittedResource( &defaultHeap, D3D12_HEAP_FLAG_NONE, &colorDesc,
                                                                 D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, &colorClear,
                                                                 IID_PPV_ARGS( &m_colorTexture ) );

    if ( FAILED( colorResult ) )
    {

        // Lane R: off-screen targets are optional render resources for
        // reflection, shadow, and post passes. The factory returns null and the
        // owning pass skips until a later recreate succeeds.
        SkullbonezCore::Core::Log()
            .WriteEventf( "dx12_framebuffer_color_create_failed hresult=0x%08X width=%d height=%d format=%u",
                          static_cast<unsigned int>( colorResult ), width, height,
                          static_cast<unsigned int>( colorFormat ) );

        SkullbonezCore::Core::Log().FlushAll();
        return false;
    }

    NameDx12Object( m_colorTexture, L"Skullbonez DX12 Framebuffer Color Texture" );

    // Depth texture
    D3D12_RESOURCE_DESC depthDesc = {};
    depthDesc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    depthDesc.Width = static_cast<UINT64>( width );
    depthDesc.Height = static_cast<UINT>( height );
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
    // Initial state is shader-readable so the graph's first producer edge uses
    // the same before-state as every later frame.
    // Docs: https://learn.microsoft.com/en-us/windows/win32/api/d3d12/nf-d3d12-id3d12device-createcommittedresource
    const HRESULT depthResult = device->CreateCommittedResource( &defaultHeap, D3D12_HEAP_FLAG_NONE, &depthDesc,
                                                                 D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, &depthClear,
                                                                 IID_PPV_ARGS( &m_depthTexture ) );

    if ( FAILED( depthResult ) )
    {
        SkullbonezCore::Core::Log().WriteEventf( "dx12_framebuffer_depth_create_failed hresult=0x%08X width=%d height=%d",
                                                 static_cast<unsigned int>( depthResult ), width, height );

        SkullbonezCore::Core::Log().FlushAll();
        ResetResources();
        return false;
    }

    NameDx12Object( m_depthTexture, L"Skullbonez DX12 Framebuffer Depth Texture" );

    // Allocate descriptor rows from the backend heaps. The color/depth textures
    // are the resources; RTV/DSV are the binding records that let the output
    // merger write into those resources.
    const Dx12CpuDescriptorAllocation rtvAllocation = m_descriptors.AllocateRtv();
    m_rtvIndex = rtvAllocation.index;
    m_rtvHandle = rtvAllocation.cpuHandle;

    // Create a Render Target View (RTV). This descriptor tells the GPU how to
    // interpret the color texture when writing pixels to it during rendering.
    // Without this view, the GPU cannot use the texture as a render target.
    // Docs: https://learn.microsoft.com/en-us/windows/win32/api/d3d12/nf-d3d12-id3d12device-createrendertargetview
    device->CreateRenderTargetView( m_colorTexture, nullptr, m_rtvHandle );

    const Dx12CpuDescriptorAllocation dsvAllocation = m_descriptors.AllocateDsv();
    m_dsvIndex = dsvAllocation.index;
    m_dsvHandle = dsvAllocation.cpuHandle;
    D3D12_DEPTH_STENCIL_VIEW_DESC dsvDesc = {};

    dsvDesc.Format = DXGI_FORMAT_D24_UNORM_S8_UINT;
    dsvDesc.ViewDimension = D3D12_DSV_DIMENSION_TEXTURE2D;

    // Create a Depth Stencil View (DSV). This descriptor tells the GPU how to
    // read/write the depth texture during depth testing. Pixels that fail the
    // depth test are discarded.
    // Docs: https://learn.microsoft.com/en-us/windows/win32/api/d3d12/nf-d3d12-id3d12device-createdepthstencilview
    device->CreateDepthStencilView( m_depthTexture, &dsvDesc, m_dsvHandle );

    // Create the SRV used after Unbind() when shaders sample the finished color
    // texture.
    m_srvIndex = m_descriptors.AllocateStatic();
    D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
    srvDesc.Format = colorFormat;
    srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
    srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    srvDesc.Texture2D.MipLevels = 1;

    // Create a Shader Resource View (SRV). This descriptor allows shaders to
    // sample/read the color texture. The same GPU resource can have multiple
    // views: RTV for writing, SRV for reading.
    // Docs: https://learn.microsoft.com/en-us/windows/win32/api/d3d12/nf-d3d12-id3d12device-createshaderresourceview
    device->CreateShaderResourceView( m_colorTexture, &srvDesc, m_descriptors.StagingCpuHandle( m_srvIndex ) );
    m_descriptors.PublishStaticDescriptor( device, m_srvIndex );

    // Register the SRV with the normal backend texture registry so renderer code
    // can bind this framebuffer with a texture handle instead of a raw descriptor
    // index.
    m_texHandle = m_textures.RegisterSRV( m_srvIndex, m_colorTexture );

    // Depth SRV: the tonemap/volumetric shaders sample this to know where solid
    // geometry blocks fog and rays. It reads only the 24-bit depth component.
    m_depthSrvIndex = m_descriptors.AllocateStatic();
    D3D12_SHADER_RESOURCE_VIEW_DESC depthSrvDesc = {};
    depthSrvDesc.Format = DXGI_FORMAT_R24_UNORM_X8_TYPELESS;
    depthSrvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
    depthSrvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    depthSrvDesc.Texture2D.MipLevels = 1;
    device->CreateShaderResourceView( m_depthTexture, &depthSrvDesc, m_descriptors.StagingCpuHandle( m_depthSrvIndex ) );

    m_descriptors.PublishStaticDescriptor( device, m_depthSrvIndex );
    m_depthTexHandle = m_textures.RegisterSRV( m_depthSrvIndex, m_depthTexture );
    m_width = width;
    m_height = height;
    return true;
}


void FramebufferDX12::Bind() const
{

    if ( !m_device.Device() || !m_colorTexture || !m_depthTexture || !m_drawGate.PrepareFramebufferBind() )
    {
        return;
    }

    // Save the previously active targets so Unbind() can restore the caller's
    // render destination.
    m_savedRTV = m_pipeline.CurrentRTV();
    m_savedDSV = m_pipeline.CurrentDSV();

    // Invariant: the graph callback has already emitted every producer edge.
    // Bind changes only descriptor/target state and must never duplicate it.

    // Clear stale reads before publishing the framebuffer as the active output.
    m_textures.ClearBoundSlotsForSrv( m_srvIndex );
    m_textures.ClearBoundSlotsForSrv( m_depthSrvIndex );
    m_pipeline.SetRenderingToFBO( true, ToDX12ColorFormat( m_colorFormat ) );
    m_pipeline.SetCurrentTargets( m_rtvHandle, m_dsvHandle );
}


void FramebufferDX12::Unbind() const
{

    if ( !m_device.Device() || !m_colorTexture || !m_depthTexture || !m_drawGate.CanRecord() )
    {
        return;
    }

    // The following graph consumer owns the state transition. Unbind restores
    // only the output descriptors saved by Bind.

    m_pipeline.SetRenderingToFBO( false, DXGI_FORMAT_R8G8B8A8_UNORM );
    m_pipeline.SetCurrentTargets( m_savedRTV, m_savedDSV );
}


void FramebufferDX12::ResetResources()
{
    const bool backendReady = m_device.Device() != nullptr;
    UINT colorSrvToRetire = UINT_MAX;
    UINT depthSrvToRetire = UINT_MAX;

    if ( backendReady && m_texHandle != 0 )
    {
        colorSrvToRetire = m_textures.UnregisterSRV( m_texHandle );
    }

    if ( backendReady && m_depthTexHandle != 0 )
    {
        depthSrvToRetire = m_textures.UnregisterSRV( m_depthTexHandle );
    }

    if ( m_colorTexture )
    {

        if ( backendReady )
        {
            m_resourceRelease.Retire( m_colorTexture, colorSrvToRetire, Dx12CpuDescriptorKind::Rtv, m_rtvIndex );
            colorSrvToRetire = UINT_MAX;
        }
        else
        {
            m_colorTexture->Release();
        }

        m_colorTexture = nullptr;
    }

    if ( m_depthTexture )
    {

        if ( backendReady )
        {
            m_resourceRelease.Retire( m_depthTexture, depthSrvToRetire, Dx12CpuDescriptorKind::Dsv, m_dsvIndex );
            depthSrvToRetire = UINT_MAX;
        }
        else
        {
            m_depthTexture->Release();
        }

        m_depthTexture = nullptr;
    }

    if ( backendReady && colorSrvToRetire != UINT_MAX )
    {
        m_resourceRelease.RetireStaticDescriptor( colorSrvToRetire );
    }

    if ( backendReady && depthSrvToRetire != UINT_MAX )
    {
        m_resourceRelease.RetireStaticDescriptor( depthSrvToRetire );
    }

    m_texHandle = 0;
    m_depthTexHandle = 0;
    m_rtvIndex = UINT_MAX;
    m_dsvIndex = UINT_MAX;
    m_srvIndex = UINT_MAX;
    m_depthSrvIndex = UINT_MAX;
}
