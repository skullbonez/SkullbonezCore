// --- Includes ---
#include "SkullbonezRenderBackendDX11.h"
#include "SkullbonezShaderDX11.h"
#include "SkullbonezMeshDX11.h"
#include "SkullbonezFramebufferDX11.h"
#include <stdexcept>
#include <string>
#include <algorithm>

#pragma comment( lib, "d3d11.lib" )
#pragma comment( lib, "dxgi.lib" )


// --- DX11 Rendering Pipeline Overview ---
//
//  CPU (C++ code)                      GPU
//  +-----------+                       +------------------+
//  | Create    | ---(Device)--->       | Resource Memory  |
//  | Resources |                       | (Textures, VBs)  |
//  +-----------+                       +------------------+
//       |
//       v
//  +-----------+                       +------------------+
//  | Record    | ---(Context)--->      | Input Assembler  |
//  | Commands  |                       |       |          |
//  +-----------+                       |       v          |
//                                      | Vertex Shader    |
//                                      |       |          |
//                                      |       v          |
//                                      | Rasterizer       |
//                                      |       |          |
//                                      |       v          |
//                                      | Pixel Shader     |
//                                      |       |          |
//                                      |       v          |
//                                      | Output Merger    |
//                                      | (Depth+Blend)    |
//                                      +------------------+
//                                              |
//                                              v
//                                      +------------------+
//  +-----------+                       | Back Buffer      |
//  | Present() | <--(SwapChain)---     | (rendered frame) |
//  +-----------+                       +------------------+
//
// Key Concepts:
//   Device       = GPU abstraction that creates resources (textures, buffers, shaders, states)
//   Context      = Records rendering commands (draw calls, state changes, resource updates)
//   SwapChain    = Manages front/back buffer flip (double-buffering for tear-free display)
//   RTV          = Render Target View -- tells DX11 "draw pixels into this texture"
//   DSV          = Depth Stencil View -- depth buffer for z-testing (closer objects occlude farther)
//   SRV          = Shader Resource View -- makes a texture readable/sampleable in shaders
//   OM (Output Merger)  = Final pipeline stage where depth test + blending happen
//   RS (Rasterizer)     = Converts triangles to pixel fragments, applies viewport transform
//   IA (Input Assembler) = Feeds raw vertex data into the vertex shader
//   Constant Buffers    = DX11's equivalent of OpenGL uniforms (small data blocks sent to shaders)
//   Pipeline States     = Pre-baked GPU configurations (rasterizer, blend, depth-stencil states)


// --- Usings ---
using namespace SkullbonezCore::Rendering;
using namespace SkullbonezCore::Math::Transformation;


// --- Helpers ---
static inline void ThrowIfFailed( HRESULT hr, const char* msg )
{
    if ( FAILED( hr ) )
    {
        throw std::runtime_error( msg );
    }
}


RenderBackendDX11* RenderBackendDX11::s_instance = nullptr;


RenderBackendDX11::RenderBackendDX11()
    : m_swapChain( nullptr ), m_device( nullptr ), m_context( nullptr ), m_backBufferRTV( nullptr ), m_depthStencilTex( nullptr ), m_depthStencilView( nullptr ), m_width( 0 ), m_height( 0 ), m_depthTestEnabled( true ), m_depthWriteEnabled( true ), m_blendEnabled( false ), m_clearColor{ 0.0f, 0.0f, 0.0f, 1.0f }, m_clearDepth( 1.0f ), m_dsDepthOn( nullptr ), m_dsDepthOff( nullptr ), m_dsDepthOnWriteOff( nullptr ), m_blendOff( nullptr ), m_rsCullOn( nullptr ), m_rsCullOff( nullptr ), m_rsCullOnPolyOffset( nullptr ), m_rsCullOffPolyOffset( nullptr ), m_samplerLinear( nullptr ), m_samplerNearest( nullptr ), m_activeBlendState( nullptr ), m_currentBlendSrc( BlendFactor::One ), m_currentBlendDst( BlendFactor::Zero ), m_cullEnabled( true ), m_polyOffsetEnabled( false ), m_currentRTV( nullptr ), m_currentDSV( nullptr ), m_stagingTex( nullptr ), m_stagingWidth( 0 ), m_stagingHeight( 0 ), m_activeShader( nullptr )
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


void RenderBackendDX11::CreateStateObjects()
{
    HRESULT hr;

    // Depth stencil states
    D3D11_DEPTH_STENCIL_DESC dsDesc = {};
    dsDesc.DepthEnable = TRUE;
    dsDesc.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ALL;
    dsDesc.DepthFunc = D3D11_COMPARISON_LESS_EQUAL;

    // Create a Depth-Stencil State object that enables depth testing. This pre-baked state tells
    // the Output Merger "compare each pixel's depth against the depth buffer; only write if it's
    // closer (LESS_EQUAL)". DX11 uses immutable state objects instead of individual glEnable calls.
    // Docs: https://learn.microsoft.com/en-us/windows/win32/api/d3d11/nf-d3d11-id3d11device-createdepthstencilstate
    hr = m_device->CreateDepthStencilState( &dsDesc, &m_dsDepthOn );
    ThrowIfFailed( hr, "CreateDepthStencilState (depth on) failed" );

    dsDesc.DepthEnable = FALSE;
    dsDesc.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ZERO;

    // Create a depth-off state (no depth testing, no depth writes). Used for UI/overlay rendering
    // where everything should draw on top regardless of depth.
    // Docs: https://learn.microsoft.com/en-us/windows/win32/api/d3d11/nf-d3d11-id3d11device-createdepthstencilstate
    hr = m_device->CreateDepthStencilState( &dsDesc, &m_dsDepthOff );
    ThrowIfFailed( hr, "CreateDepthStencilState (depth off) failed" );

    dsDesc.DepthEnable = TRUE;
    dsDesc.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ZERO;
    dsDesc.DepthFunc = D3D11_COMPARISON_LESS_EQUAL;

    // Create a depth-test-on, depth-write-off state. Depth testing is still performed (geometry
    // behind already-drawn closer geometry is correctly occluded), but successful fragments do NOT
    // update the depth buffer. Used for shadow decals: they respect terrain depth but must not
    // overwrite it, so the water surface (drawn after) can still pass the depth test.
    // Docs: https://learn.microsoft.com/en-us/windows/win32/api/d3d11/nf-d3d11-id3d11device-createdepthstencilstate
    hr = m_device->CreateDepthStencilState( &dsDesc, &m_dsDepthOnWriteOff );
    ThrowIfFailed( hr, "CreateDepthStencilState (depth on, write off) failed" );

    // Blend-off state (no blending)
    D3D11_BLEND_DESC blendDesc = {};
    blendDesc.RenderTarget[0].BlendEnable = FALSE;
    blendDesc.RenderTarget[0].RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL;

    // Create a Blend State with blending disabled. Blend states control how new pixel colors
    // combine with existing render target colors (e.g. alpha blending for transparency).
    // This "off" state means new pixels simply overwrite old ones.
    // Docs: https://learn.microsoft.com/en-us/windows/win32/api/d3d11/nf-d3d11-id3d11device-createblendstate
    hr = m_device->CreateBlendState( &blendDesc, &m_blendOff );
    ThrowIfFailed( hr, "CreateBlendState (off) failed" );

    // Rasterizer states
    D3D11_RASTERIZER_DESC rsDesc = {};
    rsDesc.FillMode = D3D11_FILL_SOLID;
    rsDesc.CullMode = D3D11_CULL_BACK;
    rsDesc.FrontCounterClockwise = TRUE;
    rsDesc.DepthClipEnable = TRUE;

    // Create a Rasterizer State with back-face culling enabled. The rasterizer converts triangles
    // into pixel fragments; this state tells it to discard triangles facing away from the camera
    // (back faces), which halves the pixel shader workload for solid objects.
    // Docs: https://learn.microsoft.com/en-us/windows/win32/api/d3d11/nf-d3d11-id3d11device-createrasterizerstate
    hr = m_device->CreateRasterizerState( &rsDesc, &m_rsCullOn );
    ThrowIfFailed( hr, "CreateRasterizerState (cull on) failed" );

    rsDesc.CullMode = D3D11_CULL_NONE;

    // Create a rasterizer state with culling disabled. Used for two-sided geometry like water
    // planes or foliage that should be visible from both sides.
    // Docs: https://learn.microsoft.com/en-us/windows/win32/api/d3d11/nf-d3d11-id3d11device-createrasterizerstate
    hr = m_device->CreateRasterizerState( &rsDesc, &m_rsCullOff );
    ThrowIfFailed( hr, "CreateRasterizerState (cull off) failed" );

    rsDesc.CullMode = D3D11_CULL_BACK;
    rsDesc.DepthBias = -1;
    rsDesc.SlopeScaledDepthBias = -1.0f;

    // Create a rasterizer state with polygon offset (depth bias). Depth bias nudges depth values
    // slightly to prevent z-fighting (flickering) when two surfaces are nearly coplanar, such as
    // decals or shadow projections on terrain.
    // Docs: https://learn.microsoft.com/en-us/windows/win32/api/d3d11/nf-d3d11-id3d11device-createrasterizerstate
    hr = m_device->CreateRasterizerState( &rsDesc, &m_rsCullOnPolyOffset );
    ThrowIfFailed( hr, "CreateRasterizerState (cull on + poly offset) failed" );

    rsDesc.CullMode = D3D11_CULL_NONE;
    // Docs: https://learn.microsoft.com/en-us/windows/win32/api/d3d11/nf-d3d11-id3d11device-createrasterizerstate
    hr = m_device->CreateRasterizerState( &rsDesc, &m_rsCullOffPolyOffset );
    ThrowIfFailed( hr, "CreateRasterizerState (cull off + poly offset) failed" );

    // Sampler states
    D3D11_SAMPLER_DESC sampDesc = {};
    sampDesc.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
    sampDesc.AddressU = D3D11_TEXTURE_ADDRESS_WRAP;
    sampDesc.AddressV = D3D11_TEXTURE_ADDRESS_WRAP;
    sampDesc.AddressW = D3D11_TEXTURE_ADDRESS_WRAP;
    sampDesc.MaxAnisotropy = 1;
    sampDesc.ComparisonFunc = D3D11_COMPARISON_NEVER;
    sampDesc.MaxLOD = D3D11_FLOAT32_MAX;

    // Create a Sampler State with linear (bilinear) filtering. Samplers control how textures are
    // read in shaders: filtering (smooth vs pixelated), addressing (wrap vs clamp at edges), and
    // mipmap selection. LINEAR gives smooth blending between texel neighbors.
    // Docs: https://learn.microsoft.com/en-us/windows/win32/api/d3d11/nf-d3d11-id3d11device-createsamplerstate
    hr = m_device->CreateSamplerState( &sampDesc, &m_samplerLinear );
    ThrowIfFailed( hr, "CreateSamplerState (linear) failed" );

    sampDesc.Filter = D3D11_FILTER_MIN_MAG_MIP_POINT;

    // Create a nearest-neighbor (point) sampler. No interpolation between texels -- useful for
    // pixel-art style rendering or font atlas sampling where crisp edges are desired.
    // Docs: https://learn.microsoft.com/en-us/windows/win32/api/d3d11/nf-d3d11-id3d11device-createsamplerstate
    hr = m_device->CreateSamplerState( &sampDesc, &m_samplerNearest );
    ThrowIfFailed( hr, "CreateSamplerState (nearest) failed" );
}


void RenderBackendDX11::ApplyRasterizerState()
{
    // Bind a pre-created rasterizer state to the Rasterizer Stage. RSSetState switches which
    // rasterizer configuration (cull mode, polygon offset) is active for subsequent draw calls.
    // Docs: https://learn.microsoft.com/en-us/windows/win32/api/d3d11/nf-d3d11-id3d11devicecontext-rssetstate
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


bool RenderBackendDX11::Init( HWND hwnd, HDC /*hdc*/, int width, int height )
{
    s_instance = this;
    m_width = width;
    m_height = height;

    // Create device and flip-model swap chain (DXGI 1.2+)
    DXGI_SWAP_CHAIN_DESC scd = {};
    scd.BufferCount = 2;
    scd.BufferDesc.Width = (UINT)width;
    scd.BufferDesc.Height = (UINT)height;
    scd.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    scd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    scd.OutputWindow = hwnd;
    scd.SampleDesc.Count = 1;
    scd.Windowed = TRUE;
    scd.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;

    D3D_FEATURE_LEVEL featureLevel;
    UINT flags = 0;
#ifdef _DEBUG
    flags |= D3D11_CREATE_DEVICE_DEBUG;
#endif

    // Create the DX11 Device, DeviceContext, and SwapChain in one call. This is the primary
    // bootstrap for DirectX 11. The Device is the GPU abstraction (creates resources); the
    // DeviceContext records rendering commands; the SwapChain manages double-buffered presentation
    // (flip-discard model for low-latency display). D3D_DRIVER_TYPE_HARDWARE = use the real GPU.
    // Docs: https://learn.microsoft.com/en-us/windows/win32/api/d3d11/nf-d3d11-d3d11createdeviceandswapchain
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
    ThrowIfFailed( hr, "D3D11CreateDeviceAndSwapChain failed" );

#ifdef _DEBUG
    // Configure ID3D11InfoQueue for debug message filtering
    ID3D11InfoQueue* infoQueue = nullptr;
    if ( SUCCEEDED( m_device->QueryInterface( __uuidof( ID3D11InfoQueue ), (void**)&infoQueue ) ) )
    {
        infoQueue->SetBreakOnSeverity( D3D11_MESSAGE_SEVERITY_CORRUPTION, TRUE );
        infoQueue->SetBreakOnSeverity( D3D11_MESSAGE_SEVERITY_ERROR, TRUE );
        infoQueue->Release();
    }
#endif

    // Create back buffer RTV
    ID3D11Texture2D* backBuffer = nullptr;

    // Retrieve the back buffer texture from the swap chain. GetBuffer(0) returns the current
    // back buffer that we'll render into. The swap chain owns this texture; we just get a pointer.
    // Docs: https://learn.microsoft.com/en-us/windows/win32/api/dxgi/nf-dxgi-idxgiswapchain-getbuffer
    hr = m_swapChain->GetBuffer( 0, __uuidof( ID3D11Texture2D ), (void**)&backBuffer );
    ThrowIfFailed( hr, "SwapChain::GetBuffer failed" );

    // Create a Render Target View for the back buffer texture. This RTV is what we bind to the
    // Output Merger so draw calls write their pixels into the back buffer (which gets displayed).
    // Docs: https://learn.microsoft.com/en-us/windows/win32/api/d3d11/nf-d3d11-id3d11device-createrendertargetview
    hr = m_device->CreateRenderTargetView( backBuffer, nullptr, &m_backBufferRTV );
    backBuffer->Release();
    ThrowIfFailed( hr, "CreateRenderTargetView failed" );

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

    // Create the main depth-stencil texture. This is the z-buffer that stores per-pixel depth
    // values so the GPU can determine which objects are in front of others. D24 = 24-bit depth
    // precision, S8 = 8-bit stencil (unused here but required by the format).
    // Docs: https://learn.microsoft.com/en-us/windows/win32/api/d3d11/nf-d3d11-id3d11device-createtexture2d
    hr = m_device->CreateTexture2D( &depthDesc, nullptr, &m_depthStencilTex );
    ThrowIfFailed( hr, "CreateTexture2D (depth stencil) failed" );

    // Create a Depth Stencil View to make the depth texture usable by the Output Merger stage.
    // Docs: https://learn.microsoft.com/en-us/windows/win32/api/d3d11/nf-d3d11-id3d11device-createdepthstencilview
    hr = m_device->CreateDepthStencilView( m_depthStencilTex, nullptr, &m_depthStencilView );
    ThrowIfFailed( hr, "CreateDepthStencilView failed" );

    // Set default render target and cache it
    m_currentRTV = m_backBufferRTV;
    m_currentDSV = m_depthStencilView;

    // Bind the back buffer RTV and depth buffer DSV to the Output Merger. This tells the GPU
    // "all draw calls should output their pixels here and depth-test against this buffer".
    // Docs: https://learn.microsoft.com/en-us/windows/win32/api/d3d11/nf-d3d11-id3d11devicecontext-omsetrendertargets
    m_context->OMSetRenderTargets( 1, &m_backBufferRTV, m_depthStencilView );

    // Create state objects
    CreateStateObjects();

    // Apply initial state

    // Activate the depth-on state in the Output Merger so depth testing is enabled from the start.
    // Docs: https://learn.microsoft.com/en-us/windows/win32/api/d3d11/nf-d3d11-id3d11devicecontext-omsetdepthstencilstate
    m_context->OMSetDepthStencilState( m_dsDepthOn, 0 );
    float blendFactor[4] = { 0, 0, 0, 0 };

    // Set the initial blend state to "off" (opaque rendering). The blend factor array and sample
    // mask (0xFFFFFFFF = all samples) are standard defaults.
    // Docs: https://learn.microsoft.com/en-us/windows/win32/api/d3d11/nf-d3d11-id3d11devicecontext-omsetblendstate
    m_context->OMSetBlendState( m_blendOff, blendFactor, 0xFFFFFFFF );
    ApplyRasterizerState();

    // Set viewport
    D3D11_VIEWPORT vp = {};
    vp.Width = (float)width;
    vp.Height = (float)height;
    vp.MaxDepth = 1.0f;

    // Configure the Rasterizer Stage viewport. The viewport maps normalized device coordinates
    // (-1 to +1) to pixel coordinates on screen. Width/Height define the render area size.
    // Docs: https://learn.microsoft.com/en-us/windows/win32/api/d3d11/nf-d3d11-id3d11devicecontext-rssetviewports
    m_context->RSSetViewports( 1, &vp );

    return true;
}


void RenderBackendDX11::Shutdown()
{
    if ( !m_device )
    {
        return;
    }

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

    // Blend cache
    for ( auto& pair : m_blendCache )
    {
        if ( pair.second )
        {
            pair.second->Release();
        }
    }
    m_blendCache.clear();

    // Staging texture
    if ( m_stagingTex )
    {
        m_stagingTex->Release();
        m_stagingTex = nullptr;
    }

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
    if ( m_dsDepthOff )
    {
        m_dsDepthOff->Release();
    }
    if ( m_dsDepthOnWriteOff )
    {
        m_dsDepthOnWriteOff->Release();
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
        m_device = nullptr;
    }

    m_currentRTV = nullptr;
    m_currentDSV = nullptr;
    s_instance = nullptr;
}


void RenderBackendDX11::Present()
{
    // Present the completed frame to the display. Present() flips the back buffer to the front
    // buffer so the user sees the rendered image. The first arg (1) = sync to VSync (1 refresh
    // interval). With FLIP_DISCARD, the old back buffer contents are discarded after present.
    // Docs: https://learn.microsoft.com/en-us/windows/win32/api/dxgi/nf-dxgi-idxgiswapchain-present
    m_swapChain->Present( 1, 0 );

    // FLIP_DISCARD unbinds the back buffer RTV from the output-merger after Present.
    // Rebind immediately so the next frame's draws have a valid render target.
    // Docs: https://learn.microsoft.com/en-us/windows/win32/api/d3d11/nf-d3d11-id3d11devicecontext-omsetrendertargets
    m_context->OMSetRenderTargets( 1, &m_backBufferRTV, m_depthStencilView );
    m_currentRTV = m_backBufferRTV;
    m_currentDSV = m_depthStencilView;
}


void RenderBackendDX11::Finish()
{
    // Flush sends all queued GPU commands to the driver immediately rather than waiting for the
    // command buffer to fill. Used to ensure all work is submitted (e.g. before a frame capture).
    // Docs: https://learn.microsoft.com/en-us/windows/win32/api/d3d11/nf-d3d11-id3d11devicecontext-flush
    m_context->Flush();
}


void RenderBackendDX11::FlushGPU()
{
    // Docs: https://learn.microsoft.com/en-us/windows/win32/api/d3d11/nf-d3d11-id3d11devicecontext-flush
    m_context->Flush();
}


void RenderBackendDX11::Resize( int width, int height )
{
    if ( width <= 0 || height <= 0 )
    {
        return;
    }

    // Release all swap-chain-backed views before resizing
    m_currentRTV = nullptr;
    m_currentDSV = nullptr;

    // Unbind all render targets before resize. Setting nullptr prevents the GPU from holding
    // references to the back buffer we're about to release.
    // Docs: https://learn.microsoft.com/en-us/windows/win32/api/d3d11/nf-d3d11-id3d11devicecontext-omsetrendertargets
    m_context->OMSetRenderTargets( 0, nullptr, nullptr );
    m_backBufferRTV->Release();
    m_depthStencilView->Release();
    m_depthStencilTex->Release();

    // Reset all pipeline state to defaults. ClearState unbinds every shader, resource, and render
    // target -- a clean slate. Required before ResizeBuffers to ensure no dangling references.
    // Docs: https://learn.microsoft.com/en-us/windows/win32/api/d3d11/nf-d3d11-id3d11devicecontext-clearstate
    m_context->ClearState();

    // Docs: https://learn.microsoft.com/en-us/windows/win32/api/d3d11/nf-d3d11-id3d11devicecontext-flush
    m_context->Flush();

    // Resize the swap chain's internal buffers to match the new window size. Passing 0 for width/
    // height would auto-detect from the window, but we specify explicitly. DXGI_FORMAT_UNKNOWN
    // keeps the existing format. All existing back buffer references are now invalid.
    // Docs: https://learn.microsoft.com/en-us/windows/win32/api/dxgi/nf-dxgi-idxgiswapchain-resizebuffers
    HRESULT hr = m_swapChain->ResizeBuffers( 0, (UINT)width, (UINT)height, DXGI_FORMAT_UNKNOWN, 0 );
    ThrowIfFailed( hr, "IDXGISwapChain::ResizeBuffers failed" );

    // Recreate back buffer RTV
    ID3D11Texture2D* backBuffer = nullptr;

    // Docs: https://learn.microsoft.com/en-us/windows/win32/api/dxgi/nf-dxgi-idxgiswapchain-getbuffer
    hr = m_swapChain->GetBuffer( 0, __uuidof( ID3D11Texture2D ), (void**)&backBuffer );
    ThrowIfFailed( hr, "SwapChain::GetBuffer failed (resize)" );

    // Docs: https://learn.microsoft.com/en-us/windows/win32/api/d3d11/nf-d3d11-id3d11device-createrendertargetview
    hr = m_device->CreateRenderTargetView( backBuffer, nullptr, &m_backBufferRTV );
    backBuffer->Release();
    ThrowIfFailed( hr, "CreateRenderTargetView failed (resize)" );

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

    // Docs: https://learn.microsoft.com/en-us/windows/win32/api/d3d11/nf-d3d11-id3d11device-createtexture2d
    hr = m_device->CreateTexture2D( &depthDesc, nullptr, &m_depthStencilTex );
    ThrowIfFailed( hr, "CreateTexture2D (depth, resize) failed" );

    // Docs: https://learn.microsoft.com/en-us/windows/win32/api/d3d11/nf-d3d11-id3d11device-createdepthstencilview
    hr = m_device->CreateDepthStencilView( m_depthStencilTex, nullptr, &m_depthStencilView );
    ThrowIfFailed( hr, "CreateDepthStencilView (resize) failed" );

    // Rebind render targets and update cache
    m_currentRTV = m_backBufferRTV;
    m_currentDSV = m_depthStencilView;

    // Docs: https://learn.microsoft.com/en-us/windows/win32/api/d3d11/nf-d3d11-id3d11devicecontext-omsetrendertargets
    m_context->OMSetRenderTargets( 1, &m_backBufferRTV, m_depthStencilView );

    // Reapply tracked state (ClearState reset everything)
    // Docs: https://learn.microsoft.com/en-us/windows/win32/api/d3d11/nf-d3d11-id3d11devicecontext-omsetdepthstencilstate
    m_context->OMSetDepthStencilState( m_depthTestEnabled ? m_dsDepthOn : m_dsDepthOff, 0 );
    float blendFactor[4] = { 0, 0, 0, 0 };
    // Docs: https://learn.microsoft.com/en-us/windows/win32/api/d3d11/nf-d3d11-id3d11devicecontext-omsetblendstate
    m_context->OMSetBlendState( m_blendEnabled ? m_activeBlendState : m_blendOff, blendFactor, 0xFFFFFFFF );
    ApplyRasterizerState();

    // Invalidate staging texture (wrong size now)
    if ( m_stagingTex )
    {
        m_stagingTex->Release();
        m_stagingTex = nullptr;
    }

    // Update viewport and dimensions
    m_width = width;
    m_height = height;
    SetViewport( 0, 0, width, height );
}


void RenderBackendDX11::SetViewport( int x, int y, int w, int h )
{
    D3D11_VIEWPORT vp = {};
    vp.TopLeftX = (float)x;
    vp.TopLeftY = (float)y;
    vp.Width = (float)w;
    vp.Height = (float)h;
    vp.MaxDepth = 1.0f;

    // Update the viewport rectangle in the Rasterizer Stage. Defines which portion of the
    // render target receives the rendered output. Commonly changed when rendering to sub-regions
    // or switching between full-screen and off-screen framebuffers.
    // Docs: https://learn.microsoft.com/en-us/windows/win32/api/d3d11/nf-d3d11-id3d11devicecontext-rssetviewports
    m_context->RSSetViewports( 1, &vp );
}


void RenderBackendDX11::Clear( bool color, bool depth )
{
    if ( color && m_currentRTV )
    {
        // Fill the entire render target with a solid color (the clear color). This wipes the
        // previous frame's pixels before drawing new geometry. Much faster than drawing a
        // full-screen quad.
        // Docs: https://learn.microsoft.com/en-us/windows/win32/api/d3d11/nf-d3d11-id3d11devicecontext-clearrendertargetview
        m_context->ClearRenderTargetView( m_currentRTV, m_clearColor );
    }
    if ( depth && m_currentDSV )
    {
        // Reset the depth buffer to the maximum depth value (1.0 = infinitely far away). This
        // ensures all new geometry will pass the depth test at the start of a new frame.
        // Docs: https://learn.microsoft.com/en-us/windows/win32/api/d3d11/nf-d3d11-id3d11devicecontext-cleardepthstencilview
        m_context->ClearDepthStencilView( m_currentDSV, D3D11_CLEAR_DEPTH, m_clearDepth, 0 );
    }
}


void RenderBackendDX11::SetClearColor( float r, float g, float b, float a )
{
    m_clearColor[0] = r;
    m_clearColor[1] = g;
    m_clearColor[2] = b;
    m_clearColor[3] = a;
}


void RenderBackendDX11::SetClearDepth( float depth )
{
    m_clearDepth = depth;
}


void RenderBackendDX11::SetDepthTest( bool enable )
{
    if ( m_depthTestEnabled == enable )
    {
        return;
    }
    m_depthTestEnabled = enable;
    ApplyDepthState();
}


void RenderBackendDX11::SetDepthWrite( bool enable )
{
    if ( m_depthWriteEnabled == enable )
    {
        return;
    }
    m_depthWriteEnabled = enable;
    ApplyDepthState();
}


void RenderBackendDX11::ApplyDepthState()
{
    // Select the depth-stencil state based on the combination of depth test and depth write flags.
    // DX11 uses immutable pre-baked state objects, so both flags must be encoded into the chosen object.
    //   depthTest=ON,  depthWrite=ON  → m_dsDepthOn          (normal opaque geometry)
    //   depthTest=ON,  depthWrite=OFF → m_dsDepthOnWriteOff   (shadow decals: test but don't overwrite)
    //   depthTest=OFF                 → m_dsDepthOff          (UI/overlays: ignore depth entirely)
    // Docs: https://learn.microsoft.com/en-us/windows/win32/api/d3d11/nf-d3d11-id3d11devicecontext-omsetdepthstencilstate
    ID3D11DepthStencilState* state;
    if ( !m_depthTestEnabled )
    {
        state = m_dsDepthOff;
    }
    else if ( m_depthWriteEnabled )
    {
        state = m_dsDepthOn;
    }
    else
    {
        state = m_dsDepthOnWriteOff;
    }
    m_context->OMSetDepthStencilState( state, 0 );
}


void RenderBackendDX11::SetBlend( bool enable )
{
    if ( m_blendEnabled == enable )
    {
        return;
    }
    m_blendEnabled = enable;
    float blendFactor[4] = { 0, 0, 0, 0 };

    // Toggle alpha blending on/off in the Output Merger. When enabled, new pixel colors are
    // blended with the existing render target color (e.g. for transparency). When disabled,
    // new pixels overwrite completely.
    // Docs: https://learn.microsoft.com/en-us/windows/win32/api/d3d11/nf-d3d11-id3d11devicecontext-omsetblendstate
    m_context->OMSetBlendState( enable ? m_activeBlendState : m_blendOff, blendFactor, 0xFFFFFFFF );
}


void RenderBackendDX11::SetBlendFunc( BlendFactor src, BlendFactor dst )
{
    if ( m_currentBlendSrc == src && m_currentBlendDst == dst && m_activeBlendState )
    {
        return;
    }
    m_currentBlendSrc = src;
    m_currentBlendDst = dst;

    // Look up or create the blend state for this (src, dst) pair
    BlendKey key = { src, dst };
    auto it = m_blendCache.find( key );
    if ( it == m_blendCache.end() )
    {
        D3D11_BLEND_DESC blendDesc = {};
        blendDesc.RenderTarget[0].BlendEnable = TRUE;
        blendDesc.RenderTarget[0].SrcBlend = TranslateBlendFactor( src );
        blendDesc.RenderTarget[0].DestBlend = TranslateBlendFactor( dst );
        blendDesc.RenderTarget[0].BlendOp = D3D11_BLEND_OP_ADD;
        blendDesc.RenderTarget[0].SrcBlendAlpha = D3D11_BLEND_ONE;
        blendDesc.RenderTarget[0].DestBlendAlpha = D3D11_BLEND_ZERO;
        blendDesc.RenderTarget[0].BlendOpAlpha = D3D11_BLEND_OP_ADD;
        blendDesc.RenderTarget[0].RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL;

        ID3D11BlendState* state = nullptr;

        // Create a new blend state for this (src, dst) factor combination. DX11 requires blend
        // states to be pre-created objects (unlike OpenGL's per-call glBlendFunc). We cache them
        // to avoid recreating the same state repeatedly.
        // Docs: https://learn.microsoft.com/en-us/windows/win32/api/d3d11/nf-d3d11-id3d11device-createblendstate
        HRESULT hr = m_device->CreateBlendState( &blendDesc, &state );
        ThrowIfFailed( hr, "CreateBlendState (cached) failed" );
        m_blendCache[key] = state;
        m_activeBlendState = state;
    }
    else
    {
        m_activeBlendState = it->second;
    }

    if ( m_blendEnabled )
    {
        float blendFactor4[4] = { 0, 0, 0, 0 };
        // Docs: https://learn.microsoft.com/en-us/windows/win32/api/d3d11/nf-d3d11-id3d11devicecontext-omsetblendstate
        m_context->OMSetBlendState( m_activeBlendState, blendFactor4, 0xFFFFFFFF );
    }
}


void RenderBackendDX11::SetCullFace( bool enable )
{
    if ( m_cullEnabled == enable )
    {
        return;
    }
    m_cullEnabled = enable;
    ApplyRasterizerState();
}


void RenderBackendDX11::SetPolygonOffset( bool enable, float /*factor*/, float /*units*/ )
{
    if ( m_polyOffsetEnabled == enable )
    {
        return;
    }
    m_polyOffsetEnabled = enable;
    ApplyRasterizerState();
}


void RenderBackendDX11::SetClipPlane( int /*index*/, bool /*enable*/ )
{
    // DX11 clip planes are handled via SV_ClipDistance in the ShaderGL.
    // The uClipPlane uniform controls the clip distance computation.
    // No DX API state change needed.
}


std::unique_ptr<IShader> RenderBackendDX11::CreateShader( const char* baseName )
{
    std::string hlslPath = std::string( DATA_ROOT ) + baseName + ".hlsl";
    auto shader = std::make_unique<ShaderDX11>( m_device, m_context );
    shader->Compile( hlslPath.c_str() );
    return shader;
}


std::unique_ptr<IMesh> RenderBackendDX11::CreateMesh( const float* data, int vertexCount, bool hasNormals, bool hasTexCoords )
{
    auto mesh = std::make_unique<MeshDX11>( m_device, m_context );
    mesh->Create( data, vertexCount, hasNormals, hasTexCoords );
    return mesh;
}


std::unique_ptr<IFramebuffer> RenderBackendDX11::CreateFramebuffer( int width, int height )
{
    auto fbo = std::make_unique<FramebufferDX11>( this, m_device, m_context );
    fbo->Create( width, height );
    return fbo;
}


uint32_t RenderBackendDX11::CreateTexture2D( const uint8_t* data, int w, int h, int channels, bool generateMips, bool linearFilter )
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
        // Create a 2D texture with auto-generated mipmaps. MipLevels=0 tells DX11 to create a
        // full mip chain. GENERATE_MIPS flag + RENDER_TARGET bind flag enable GenerateMips() later.
        // We create empty then upload separately because DX11 can't init mipped textures directly.
        // Docs: https://learn.microsoft.com/en-us/windows/win32/api/d3d11/nf-d3d11-id3d11device-createtexture2d
        hr = m_device->CreateTexture2D( &texDesc, nullptr, &tex );
        if ( FAILED( hr ) )
        {
            throw std::runtime_error( "CreateTexture2D failed" );
        }

        // Upload pixel data to mip level 0 of the texture. UpdateSubresource copies CPU memory
        // to GPU texture memory for USAGE_DEFAULT resources (can't use Map on non-dynamic textures).
        // Docs: https://learn.microsoft.com/en-us/windows/win32/api/d3d11/nf-d3d11-id3d11devicecontext-updatesubresource
        m_context->UpdateSubresource( tex, 0, nullptr, srcData, (UINT)( w * bytesPerPixel ), 0 );
    }
    else
    {
        D3D11_SUBRESOURCE_DATA initData = {};
        initData.pSysMem = srcData;
        initData.SysMemPitch = (UINT)( w * bytesPerPixel );

        // Create a 2D texture with initial data (no mipmaps). The texture is created and filled
        // in one call -- more efficient than create-then-upload for single-mip textures.
        // Docs: https://learn.microsoft.com/en-us/windows/win32/api/d3d11/nf-d3d11-id3d11device-createtexture2d
        hr = m_device->CreateTexture2D( &texDesc, &initData, &tex );
        if ( FAILED( hr ) )
        {
            throw std::runtime_error( "CreateTexture2D failed" );
        }
    }

    // Create a Shader Resource View so the texture can be sampled in pixel shaders.
    // Docs: https://learn.microsoft.com/en-us/windows/win32/api/d3d11/nf-d3d11-id3d11device-createshaderresourceview
    ID3D11ShaderResourceView* srv = nullptr;
    hr = m_device->CreateShaderResourceView( tex, nullptr, &srv );
    if ( FAILED( hr ) )
    {
        tex->Release();
        throw std::runtime_error( "CreateSRV failed" );
    }

    if ( generateMips )
    {
        // Auto-generate the full mipmap chain from the base level (level 0) image. The GPU
        // downsamples each level to half-resolution using the SRV's associated filter. Mipmaps
        // prevent texture shimmering at distance.
        // Docs: https://learn.microsoft.com/en-us/windows/win32/api/d3d11/nf-d3d11-id3d11devicecontext-generatemips
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


void RenderBackendDX11::BindTexture( uint32_t handle, int slot )
{
    if ( handle == 0 || handle > (uint32_t)m_textures.size() )
    {
        return;
    }

    TextureEntryDX& entry = m_textures[handle - 1];

    // Bind a texture (SRV) to a pixel shader texture slot. This makes the texture available for
    // sampling in the pixel shader at register t<slot>.
    // Docs: https://learn.microsoft.com/en-us/windows/win32/api/d3d11/nf-d3d11-id3d11devicecontext-pssetshaderresources
    m_context->PSSetShaderResources( (UINT)slot, 1, &entry.srv );

    // Bind a sampler state to the same pixel shader slot. The sampler controls how the texture
    // is filtered (linear/nearest) and addressed (wrap/clamp) when read in the shader.
    // Docs: https://learn.microsoft.com/en-us/windows/win32/api/d3d11/nf-d3d11-id3d11devicecontext-pssetsamplers
    m_context->PSSetSamplers( (UINT)slot, 1, &entry.sampler );
}


void RenderBackendDX11::DeleteTexture( uint32_t handle )
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


uint32_t RenderBackendDX11::RegisterSRV( ID3D11ShaderResourceView* srv )
{
    TextureEntryDX entry = {};
    entry.srv = srv;
    entry.sampler = m_samplerLinear;
    entry.owned = false;
    m_textures.push_back( entry );
    return (uint32_t)m_textures.size();
}


void RenderBackendDX11::UnregisterSRV( uint32_t handle )
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


std::vector<uint8_t> RenderBackendDX11::CaptureBackbuffer( int& outWidth, int& outHeight )
{
    outWidth = m_width;
    outHeight = m_height;

    // Get back buffer
    ID3D11Texture2D* backBuffer = nullptr;

    // Docs: https://learn.microsoft.com/en-us/windows/win32/api/dxgi/nf-dxgi-idxgiswapchain-getbuffer
    HRESULT hr = m_swapChain->GetBuffer( 0, __uuidof( ID3D11Texture2D ), (void**)&backBuffer );
    ThrowIfFailed( hr, "GetBuffer failed (capture)" );

    // Create or reuse staging texture
    if ( !m_stagingTex || m_stagingWidth != m_width || m_stagingHeight != m_height )
    {
        if ( m_stagingTex )
        {
            m_stagingTex->Release();
        }

        D3D11_TEXTURE2D_DESC desc;
        backBuffer->GetDesc( &desc );
        desc.Usage = D3D11_USAGE_STAGING;
        desc.BindFlags = 0;
        desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
        desc.MiscFlags = 0;

        // Create a staging texture for CPU readback. USAGE_STAGING + CPU_ACCESS_READ means this
        // texture lives in CPU-accessible memory. GPU textures can't be read directly by the CPU;
        // we must copy into a staging resource first, then Map it for reading.
        // Docs: https://learn.microsoft.com/en-us/windows/win32/api/d3d11/nf-d3d11-id3d11device-createtexture2d
        hr = m_device->CreateTexture2D( &desc, nullptr, &m_stagingTex );
        if ( FAILED( hr ) )
        {
            backBuffer->Release();
            throw std::runtime_error( "CreateTexture2D (staging) failed" );
        }
        m_stagingWidth = m_width;
        m_stagingHeight = m_height;
    }

    // Copy the back buffer GPU texture into the staging texture. CopyResource is a GPU-side copy
    // that moves pixel data from one texture to another (here: from VRAM to CPU-readable memory).
    // This must complete before we can Map the staging texture.
    // Docs: https://learn.microsoft.com/en-us/windows/win32/api/d3d11/nf-d3d11-id3d11devicecontext-copyresource
    m_context->CopyResource( m_stagingTex, backBuffer );
    backBuffer->Release();

    // Map the staging texture to get a CPU pointer to the pixel data. D3D11_MAP_READ gives
    // read-only access to the GPU-copied data. After this call, mapped.pData points to the
    // raw pixel bytes and mapped.RowPitch tells us the stride between rows.
    // Docs: https://learn.microsoft.com/en-us/windows/win32/api/d3d11/nf-d3d11-id3d11devicecontext-map
    D3D11_MAPPED_SUBRESOURCE mapped;
    hr = m_context->Map( m_stagingTex, 0, D3D11_MAP_READ, 0, &mapped );
    ThrowIfFailed( hr, "Map staging texture failed" );

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

    // Release the CPU mapping. After Unmap, the mapped pointer is invalid.
    // Docs: https://learn.microsoft.com/en-us/windows/win32/api/d3d11/nf-d3d11-id3d11devicecontext-unmap
    m_context->Unmap( m_stagingTex, 0 );

    return pixels;
}


int RenderBackendDX11::GetWidth() const
{
    return m_width;
}


int RenderBackendDX11::GetHeight() const
{
    return m_height;
}


bool RenderBackendDX11::IsDepthTestEnabled() const
{
    return m_depthTestEnabled;
}


bool RenderBackendDX11::IsBlendEnabled() const
{
    return m_blendEnabled;
}


bool RenderBackendDX11::UsesZeroToOneDepth() const
{
    return true;
}


// --- Dynamic Vertex Buffer ---


uint32_t RenderBackendDX11::CreateDynamicVB( const int* attribComponents, int numAttribs, int maxVertices )
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

    // Create a dynamic vertex buffer. DYNAMIC + CPU_ACCESS_WRITE means the CPU can update this
    // buffer every frame via Map/Unmap (using WRITE_DISCARD). Used for geometry that changes
    // per-frame like text quads or particle systems.
    // Docs: https://learn.microsoft.com/en-us/windows/win32/api/d3d11/nf-d3d11-id3d11device-createbuffer
    HRESULT hr = m_device->CreateBuffer( &bd, nullptr, &dvb.vb );
    ThrowIfFailed( hr, "CreateBuffer (dynamic VB) failed" );

    m_dynamicVBs.push_back( dvb );
    return (uint32_t)m_dynamicVBs.size();
}


void RenderBackendDX11::UploadAndDrawDynamicVB( uint32_t handle, const float* data, int vertexCount )
{
    if ( handle == 0 || handle > (uint32_t)m_dynamicVBs.size() )
    {
        return;
    }
    DynamicVBDX& dvb = m_dynamicVBs[handle - 1];

    // Flush active ShaderGL CB
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

        // Build input elements from attrib components (creates an input layout for the dynamic VB)
        // Docs: https://learn.microsoft.com/en-us/windows/win32/api/d3d11/nf-d3d11-id3d11device-createinputlayout
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

    // Upload data -- Map with WRITE_DISCARD to get a fresh pointer (avoids GPU stalls)
    // Docs: https://learn.microsoft.com/en-us/windows/win32/api/d3d11/nf-d3d11-id3d11devicecontext-map
    D3D11_MAPPED_SUBRESOURCE mapped;
    m_context->Map( dvb.vb, 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped );
    memcpy( mapped.pData, data, (size_t)vertexCount * dvb.floatsPerVertex * sizeof( float ) );
    // Docs: https://learn.microsoft.com/en-us/windows/win32/api/d3d11/nf-d3d11-id3d11devicecontext-unmap
    m_context->Unmap( dvb.vb, 0 );

    // Draw
    // Docs: https://learn.microsoft.com/en-us/windows/win32/api/d3d11/nf-d3d11-id3d11devicecontext-iasetinputlayout
    m_context->IASetInputLayout( dvb.inputLayout );
    UINT stride = (UINT)dvb.stride;
    UINT vbOffset = 0;
    // Docs: https://learn.microsoft.com/en-us/windows/win32/api/d3d11/nf-d3d11-id3d11devicecontext-iasetvertexbuffers
    m_context->IASetVertexBuffers( 0, 1, &dvb.vb, &stride, &vbOffset );
    // Docs: https://learn.microsoft.com/en-us/windows/win32/api/d3d11/nf-d3d11-id3d11devicecontext-iasetprimitivetopology
    m_context->IASetPrimitiveTopology( D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST );
    // Docs: https://learn.microsoft.com/en-us/windows/win32/api/d3d11/nf-d3d11-id3d11devicecontext-draw
    m_context->Draw( (UINT)vertexCount, 0 );
}


void RenderBackendDX11::DestroyDynamicVB( uint32_t handle )
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


// --- Instanced mesh ---


uint32_t RenderBackendDX11::CreateInstancedMesh( const float* staticData, int staticVertCount, int staticFloatsPerVert, int maxInstances, int instanceFloats, int instanceStartAttrib, const int* instanceAttribSizes, int numInstanceAttribs, const int* staticAttribSizes, int numStaticAttribs )
{
    InstancedMeshDX im = {};
    im.staticFloatsPerVert = staticFloatsPerVert;
    im.staticStride = staticFloatsPerVert * (int)sizeof( float );
    im.instanceFloats = instanceFloats;
    im.instanceStride = instanceFloats * (int)sizeof( float );
    im.instanceStartAttrib = instanceStartAttrib;
    im.numInstanceAttribs = numInstanceAttribs;
    im.lastVSBytecode = nullptr;
    im.numStaticAttribs = numStaticAttribs;
    for ( int i = 0; i < numInstanceAttribs && i < 8; ++i )
    {
        im.instanceAttribSizes[i] = instanceAttribSizes[i];
    }
    for ( int i = 0; i < numStaticAttribs && i < 8; ++i )
    {
        im.staticAttribSizes[i] = staticAttribSizes[i];
    }

    // Static VB
    D3D11_BUFFER_DESC bd = {};
    bd.ByteWidth = (UINT)( staticVertCount * im.staticStride );
    bd.Usage = D3D11_USAGE_IMMUTABLE;
    bd.BindFlags = D3D11_BIND_VERTEX_BUFFER;
    D3D11_SUBRESOURCE_DATA initData = {};
    initData.pSysMem = staticData;

    // Create an immutable vertex buffer for the shared mesh geometry (e.g. a unit sphere). This
    // data never changes; each instance will reuse these same vertices with different transforms.
    // Docs: https://learn.microsoft.com/en-us/windows/win32/api/d3d11/nf-d3d11-id3d11device-createbuffer
    HRESULT hr = m_device->CreateBuffer( &bd, &initData, &im.staticVB );
    ThrowIfFailed( hr, "CreateBuffer (static VB, instanced) failed" );

    // Instance VB
    bd.ByteWidth = (UINT)( maxInstances * im.instanceStride );
    bd.Usage = D3D11_USAGE_DYNAMIC;
    bd.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;

    // Create a dynamic vertex buffer for per-instance data (world matrices, colors, etc.).
    // Updated every frame via Map/Unmap with the unique data for each instance.
    // Docs: https://learn.microsoft.com/en-us/windows/win32/api/d3d11/nf-d3d11-id3d11device-createbuffer
    hr = m_device->CreateBuffer( &bd, nullptr, &im.instanceVB );
    ThrowIfFailed( hr, "CreateBuffer (instance VB) failed" );

    m_instancedMeshes.push_back( im );
    return (uint32_t)m_instancedMeshes.size();
}


void RenderBackendDX11::UploadInstanceData( uint32_t handle, const float* data, int floatCount )
{
    if ( handle == 0 || handle > (uint32_t)m_instancedMeshes.size() )
    {
        return;
    }
    InstancedMeshDX& im = m_instancedMeshes[handle - 1];

    // Map the instance buffer and upload per-instance data (WRITE_DISCARD for zero-stall update)
    // Docs: https://learn.microsoft.com/en-us/windows/win32/api/d3d11/nf-d3d11-id3d11devicecontext-map
    D3D11_MAPPED_SUBRESOURCE mapped;
    m_context->Map( im.instanceVB, 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped );
    memcpy( mapped.pData, data, (size_t)floatCount * sizeof( float ) );
    // Docs: https://learn.microsoft.com/en-us/windows/win32/api/d3d11/nf-d3d11-id3d11devicecontext-unmap
    m_context->Unmap( im.instanceVB, 0 );
}


void RenderBackendDX11::DrawInstancedMesh( uint32_t handle, int staticVertCount, int instanceCount )
{
    if ( handle == 0 || handle > (uint32_t)m_instancedMeshes.size() )
    {
        return;
    }
    InstancedMeshDX& im = m_instancedMeshes[handle - 1];

    // Flush active ShaderGL CB
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

        // Build input layout: slot 0 = static geometry, slot 1 = instance data
        // An input layout for instanced rendering maps two vertex buffer slots: slot 0 has the
        // shared mesh geometry (per-vertex data), slot 1 has per-instance data (e.g. world matrix
        // rows) that advances once per instance rather than once per vertex.
        // Docs: https://learn.microsoft.com/en-us/windows/win32/api/d3d11/nf-d3d11-id3d11device-createinputlayout
        D3D11_INPUT_ELEMENT_DESC elements[16] = {};
        int numElements = 0;

        // Static vertex attributes (slot 0)
        if ( im.numStaticAttribs > 0 )
        {
            // Multi-attribute: POSITION, NORMAL, TEXCOORD0 etc.
            static const char* staticSemantics[] = { "POSITION", "NORMAL", "TEXCOORD" };
            UINT staticOffset = 0;
            for ( int i = 0; i < im.numStaticAttribs; ++i )
            {
                elements[numElements].SemanticName = staticSemantics[i < 3 ? i : 2];
                elements[numElements].SemanticIndex = ( i >= 2 ) ? (UINT)( i - 2 ) : 0;
                elements[numElements].InputSlot = 0;
                elements[numElements].AlignedByteOffset = staticOffset;
                elements[numElements].InputSlotClass = D3D11_INPUT_PER_VERTEX_DATA;
                switch ( im.staticAttribSizes[i] )
                {
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
                staticOffset += im.staticAttribSizes[i] * (UINT)sizeof( float );
                numElements++;
            }
        }
        else
        {
            // Legacy: single POSITION attribute
            elements[0].SemanticName = "POSITION";
            elements[0].SemanticIndex = 0;
            elements[0].Format = ( im.staticFloatsPerVert == 3 ) ? DXGI_FORMAT_R32G32B32_FLOAT : DXGI_FORMAT_R32G32_FLOAT;
            elements[0].InputSlot = 0;
            elements[0].AlignedByteOffset = 0;
            elements[0].InputSlotClass = D3D11_INPUT_PER_VERTEX_DATA;
            numElements = 1;
        }

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

        // Docs: https://learn.microsoft.com/en-us/windows/win32/api/d3d11/nf-d3d11-id3d11device-createinputlayout
        m_device->CreateInputLayout( elements,
                                     (UINT)numElements,
                                     m_activeShader->GetVSBytecode(),
                                     m_activeShader->GetVSBytecodeSize(),
                                     &im.inputLayout );
        im.lastVSBytecode = m_activeShader->GetVSBytecode();
    }

    // Bind both VBs (slot 0 = static geometry, slot 1 = instance data)
    ID3D11Buffer* vbs[2] = { im.staticVB, im.instanceVB };
    UINT strides[2] = { (UINT)im.staticStride, (UINT)im.instanceStride };
    UINT offsets[2] = { 0, 0 };

    // Docs: https://learn.microsoft.com/en-us/windows/win32/api/d3d11/nf-d3d11-id3d11devicecontext-iasetinputlayout
    m_context->IASetInputLayout( im.inputLayout );

    // Bind two vertex buffers simultaneously: the shared mesh in slot 0 and per-instance data
    // in slot 1. The Input Assembler reads per-vertex data from slot 0 and advances slot 1
    // once per instance (controlled by InstanceDataStepRate in the input layout).
    // Docs: https://learn.microsoft.com/en-us/windows/win32/api/d3d11/nf-d3d11-id3d11devicecontext-iasetvertexbuffers
    m_context->IASetVertexBuffers( 0, 2, vbs, strides, offsets );

    // Docs: https://learn.microsoft.com/en-us/windows/win32/api/d3d11/nf-d3d11-id3d11devicecontext-iasetprimitivetopology
    m_context->IASetPrimitiveTopology( D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST );

    // Draw all instances in one GPU call. Renders staticVertCount vertices × instanceCount copies.
    // Docs: https://learn.microsoft.com/en-us/windows/win32/api/d3d11/nf-d3d11-id3d11devicecontext-drawinstanced
    m_context->DrawInstanced( (UINT)staticVertCount, (UINT)instanceCount, 0, 0 );
}


void RenderBackendDX11::DestroyInstancedMesh( uint32_t handle )
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