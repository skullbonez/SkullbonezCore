// --- Includes ---
#include "SkullbonezRenderBackendDX12.h"
#include "SkullbonezShaderDX12.h"
#include "SkullbonezMeshDX12.h"
#include "SkullbonezFramebufferDX12.h"
#include <stdexcept>
#include <cstdio>
#include <cstring>
#include <algorithm>
#include <string>


// --- Usings ---
using namespace SkullbonezCore::Rendering;


// --- Helpers ---
static inline void ThrowIfFailed( HRESULT hr, const char* msg )
{
    if ( FAILED( hr ) )
    {
        throw std::runtime_error( msg );
    }
}


RenderBackendDX12* RenderBackendDX12::s_instance = nullptr;


// --- Constructor ---


RenderBackendDX12::RenderBackendDX12()
    : m_factory( nullptr ), m_swapChain( nullptr ), m_device( nullptr ), m_commandQueue( nullptr ), m_commandList( nullptr ), m_commandListOpen( false ), m_frameIndex( 0 ), m_allocatorIndex( 0 ), m_fence( nullptr ), m_fenceValue( 0 ), m_fenceEvent( nullptr ), m_rtvHeap( nullptr ), m_dsvHeap( nullptr ), m_srvHeap( nullptr ), m_srvStagingHeap( nullptr ), m_rtvDescSize( 0 ), m_dsvDescSize( 0 ), m_srvDescSize( 0 ), m_nextRTV( FRAME_COUNT ) // 0-1 reserved for swap chain
      ,
      m_nextDSV( 1 ) // 0 reserved for main depth
      ,
      m_nextStaticSRV( 0 ), m_nextTransientSRV( 0 ), m_depthStencil( nullptr ), m_uploadBuffer( nullptr ), m_uploadBufferMapped( nullptr ), m_uploadOffset( 0 ), m_rootSignature( nullptr ), m_width( 0 ), m_height( 0 ), m_depthTestEnabled( true ), m_blendEnabled( false ), m_blendSrc( BlendFactor::One ), m_blendDst( BlendFactor::Zero ), m_cullEnabled( true ), m_polyOffsetEnabled( false ), m_polyOffsetFactor( 0.0f ), m_polyOffsetUnits( 0.0f ), m_clearDepth( 1.0f ), m_psoDirty( true ), m_activeShader( nullptr ), m_renderingToFBO( false ), m_backBufferIsRT( false ), m_lastPSOHash( 0 ), m_texBindingsDirty( true ), m_targetsDirty( true ), m_dxrSupported( false ), m_device5( nullptr ), m_cmdList4( nullptr ), m_rtPSO( nullptr ), m_rtPSOProps( nullptr ), m_rtRootSignature( nullptr ), m_reflectionUAV( nullptr ), m_reflectionUAVIndex( 0 ), m_reflectionSRVIndex( 0 ), m_reflectionWidth( 0 ), m_reflectionHeight( 0 ), m_reflectionInSRVState( false ), m_rtConstantBuffer( nullptr ), m_rtConstantBufferMapped( nullptr )
{
    m_clearColor[0] = 0.0f;
    m_clearColor[1] = 0.0f;
    m_clearColor[2] = 0.0f;
    m_clearColor[3] = 1.0f;
    m_renderTargets[0] = nullptr;
    m_renderTargets[1] = nullptr;
    m_commandAllocators[0] = nullptr;
    m_commandAllocators[1] = nullptr;
    m_frameFenceValues[0] = 0;
    m_frameFenceValues[1] = 0;
    m_boundTexSlot[0] = UINT_MAX;
    m_boundTexSlot[1] = UINT_MAX;
    m_currentRTV = {};
    m_currentDSV = {};
    m_timerQueryHeap = nullptr;
    m_timerReadbackBuf = nullptr;
    m_timerFreq = 1;
    m_timerReadPending = false;
    std::memset( m_timerResultMs, 0, sizeof( m_timerResultMs ) );
    std::memset( m_timerResultValid, 0, sizeof( m_timerResultValid ) );
}


// --- Helpers ---


void RenderBackendDX12::WaitForGpu()
{
    m_commandQueue->Signal( m_fence, ++m_fenceValue );
    if ( m_fence->GetCompletedValue() < m_fenceValue )
    {
        m_fence->SetEventOnCompletion( m_fenceValue, m_fenceEvent );
        WaitForSingleObject( m_fenceEvent, INFINITE );
    }
    // After full GPU wait, all frame fences are implicitly completed
    for ( int i = 0; i < FRAME_COUNT; ++i )
    {
        m_frameFenceValues[i] = 0;
    }
}


void RenderBackendDX12::EnsureCommandListOpen()
{
    if ( m_commandListOpen )
    {
        return;
    }

    // Wait for the GPU to finish with this allocator's previous work
    UINT64 completedFence = m_fence->GetCompletedValue();
    if ( m_frameFenceValues[m_allocatorIndex] > completedFence )
    {
        m_fence->SetEventOnCompletion( m_frameFenceValues[m_allocatorIndex], m_fenceEvent );
        WaitForSingleObject( m_fenceEvent, INFINITE );
    }

    // Safe to reset this allocator now
    m_commandAllocators[m_allocatorIndex]->Reset();
    m_commandList->Reset( m_commandAllocators[m_allocatorIndex], nullptr );
    ID3D12DescriptorHeap* heaps[] = { m_srvHeap };
    m_commandList->SetDescriptorHeaps( 1, heaps );
    m_commandList->SetGraphicsRootSignature( m_rootSignature );
    m_commandListOpen = true;
    m_uploadOffset = 0;
    m_nextTransientSRV = MAX_STATIC_SRVS;

    // All command list state is reset — force full rebind on next draw
    m_lastPSOHash = 0;
    m_texBindingsDirty = true;
    m_targetsDirty = true;
}


void RenderBackendDX12::TransitionBarrier( ID3D12Resource* resource, D3D12_RESOURCE_STATES before, D3D12_RESOURCE_STATES after )
{
    if ( before == after )
    {
        return;
    }
    D3D12_RESOURCE_BARRIER barrier = {};
    barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    barrier.Transition.pResource = resource;
    barrier.Transition.StateBefore = before;
    barrier.Transition.StateAfter = after;
    barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    m_commandList->ResourceBarrier( 1, &barrier );
}


void RenderBackendDX12::FlushUploadBuffer()
{
    if ( !m_commandListOpen )
    {
        return;
    }
    // Submit current work and wait for completion (mid-frame flush for upload exhaustion)
    m_commandList->Close();
    m_commandListOpen = false;
    ID3D12CommandList* ppCLs[] = { m_commandList };
    m_commandQueue->ExecuteCommandLists( 1, ppCLs );
    WaitForGpu();

    // Reopen with same allocator (WaitForGpu completed everything)
    m_commandAllocators[m_allocatorIndex]->Reset();
    m_commandList->Reset( m_commandAllocators[m_allocatorIndex], nullptr );
    ID3D12DescriptorHeap* heaps[] = { m_srvHeap };
    m_commandList->SetDescriptorHeaps( 1, heaps );
    m_commandList->SetGraphicsRootSignature( m_rootSignature );
    m_commandListOpen = true;
    m_uploadOffset = 0;
    m_nextTransientSRV = MAX_STATIC_SRVS;
    m_lastPSOHash = 0;
    m_texBindingsDirty = true;
    m_targetsDirty = true;
}


void RenderBackendDX12::FlushUploadBufferIfNeeded( UINT64 size, UINT64 alignment )
{
    UINT64 aligned = ( m_uploadOffset + alignment - 1 ) & ~( alignment - 1 );
    if ( aligned + size > UPLOAD_BUFFER_SIZE )
    {
        FlushUploadBuffer();
    }
}


D3D12_GPU_VIRTUAL_ADDRESS RenderBackendDX12::SubAllocateUpload( UINT64 size, UINT64 alignment )
{
    UINT64 aligned = ( m_uploadOffset + alignment - 1 ) & ~( alignment - 1 );
    if ( aligned + size > UPLOAD_BUFFER_SIZE )
    {
        throw std::runtime_error( "DX12 upload buffer exhausted" );
    }
    m_uploadOffset = aligned + size;
    return m_uploadBuffer->GetGPUVirtualAddress() + aligned;
}


uint8_t* RenderBackendDX12::GetUploadPtr( D3D12_GPU_VIRTUAL_ADDRESS addr )
{
    UINT64 offset = addr - m_uploadBuffer->GetGPUVirtualAddress();
    return m_uploadBufferMapped + offset;
}


UINT RenderBackendDX12::AllocateStaticSRV()
{
    if ( m_nextStaticSRV >= MAX_STATIC_SRVS )
    {
        throw std::runtime_error( "DX12 static SRV heap exhausted" );
    }
    return m_nextStaticSRV++;
}


UINT RenderBackendDX12::AllocateTransientSRV()
{
    UINT total = MAX_STATIC_SRVS + MAX_TRANSIENT_SRVS;
    if ( m_nextTransientSRV >= total )
    {
        throw std::runtime_error( "DX12 transient SRV heap exhausted" );
    }
    return m_nextTransientSRV++;
}


D3D12_CPU_DESCRIPTOR_HANDLE RenderBackendDX12::GetSRVStagingCpuHandle( UINT index )
{
    D3D12_CPU_DESCRIPTOR_HANDLE handle = m_srvStagingHeap->GetCPUDescriptorHandleForHeapStart();
    handle.ptr += (SIZE_T)index * m_srvDescSize;
    return handle;
}


D3D12_GPU_DESCRIPTOR_HANDLE RenderBackendDX12::GetSRVGpuHandle( UINT index )
{
    D3D12_GPU_DESCRIPTOR_HANDLE handle = m_srvHeap->GetGPUDescriptorHandleForHeapStart();
    handle.ptr += (UINT64)index * m_srvDescSize;
    return handle;
}


D3D12_CPU_DESCRIPTOR_HANDLE RenderBackendDX12::GetRTVHandle( UINT index )
{
    D3D12_CPU_DESCRIPTOR_HANDLE handle = m_rtvHeap->GetCPUDescriptorHandleForHeapStart();
    handle.ptr += (SIZE_T)index * m_rtvDescSize;
    return handle;
}


D3D12_CPU_DESCRIPTOR_HANDLE RenderBackendDX12::GetDSVHandle( UINT index )
{
    D3D12_CPU_DESCRIPTOR_HANDLE handle = m_dsvHeap->GetCPUDescriptorHandleForHeapStart();
    handle.ptr += (SIZE_T)index * m_dsvDescSize;
    return handle;
}


// --- Init / Shutdown ---


bool RenderBackendDX12::Init( HWND hwnd, HDC /*hdc*/, int width, int height )
{
    s_instance = this;
    m_width = width;
    m_height = height;

    // DXGI Factory
    UINT factoryFlags = 0;
    {
        ID3D12Debug* debugController = nullptr;
        if ( SUCCEEDED( D3D12GetDebugInterface( IID_PPV_ARGS( &debugController ) ) ) )
        {
            debugController->EnableDebugLayer();
            debugController->Release();
            factoryFlags = DXGI_CREATE_FACTORY_DEBUG;
        }
    }
    if ( FAILED( CreateDXGIFactory2( factoryFlags, IID_PPV_ARGS( &m_factory ) ) ) )
    {
        throw std::runtime_error( "CreateDXGIFactory2 failed" );
    }

    // Device
    if ( FAILED( D3D12CreateDevice( nullptr, D3D_FEATURE_LEVEL_11_0, IID_PPV_ARGS( &m_device ) ) ) )
    {
        throw std::runtime_error( "D3D12CreateDevice failed" );
    }

    // Check DXR capability
    CheckDXRSupport();

    // Enable break-on-corruption (severe errors only) — errors are still logged
    {
        ID3D12InfoQueue* infoQueue = nullptr;
        if ( SUCCEEDED( m_device->QueryInterface( IID_PPV_ARGS( &infoQueue ) ) ) )
        {
            infoQueue->SetBreakOnSeverity( D3D12_MESSAGE_SEVERITY_CORRUPTION, TRUE );

            // Suppress noisy warnings about buffer initial states
            D3D12_MESSAGE_ID denyIds[] = { D3D12_MESSAGE_ID_CREATERESOURCE_STATE_IGNORED };
            D3D12_INFO_QUEUE_FILTER filter = {};
            filter.DenyList.NumIDs = _countof( denyIds );
            filter.DenyList.pIDList = denyIds;
            infoQueue->PushStorageFilter( &filter );
            infoQueue->Release();
        }
    }

    // Command Queue
    D3D12_COMMAND_QUEUE_DESC queueDesc = {};
    queueDesc.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
    if ( FAILED( m_device->CreateCommandQueue( &queueDesc, IID_PPV_ARGS( &m_commandQueue ) ) ) )
    {
        throw std::runtime_error( "CreateCommandQueue failed" );
    }

    // Swap Chain
    DXGI_SWAP_CHAIN_DESC1 scDesc = {};
    scDesc.BufferCount = FRAME_COUNT;
    scDesc.Width = (UINT)width;
    scDesc.Height = (UINT)height;
    scDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    scDesc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    scDesc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
    scDesc.SampleDesc.Count = 1;

    IDXGISwapChain1* swapChain1 = nullptr;
    if ( FAILED( m_factory->CreateSwapChainForHwnd( m_commandQueue, hwnd, &scDesc, nullptr, nullptr, &swapChain1 ) ) )
    {
        throw std::runtime_error( "CreateSwapChainForHwnd failed" );
    }
    swapChain1->QueryInterface( IID_PPV_ARGS( &m_swapChain ) );
    swapChain1->Release();
    m_factory->MakeWindowAssociation( hwnd, DXGI_MWA_NO_ALT_ENTER );
    m_frameIndex = m_swapChain->GetCurrentBackBufferIndex();

    // Descriptor Heaps
    {
        D3D12_DESCRIPTOR_HEAP_DESC desc = {};
        desc.NumDescriptors = 16;
        desc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
        ThrowIfFailed( m_device->CreateDescriptorHeap( &desc, IID_PPV_ARGS( &m_rtvHeap ) ), "CreateDescriptorHeap (RTV) failed" );
        m_rtvDescSize = m_device->GetDescriptorHandleIncrementSize( D3D12_DESCRIPTOR_HEAP_TYPE_RTV );
    }
    {
        D3D12_DESCRIPTOR_HEAP_DESC desc = {};
        desc.NumDescriptors = 4;
        desc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_DSV;
        ThrowIfFailed( m_device->CreateDescriptorHeap( &desc, IID_PPV_ARGS( &m_dsvHeap ) ), "CreateDescriptorHeap (DSV) failed" );
        m_dsvDescSize = m_device->GetDescriptorHandleIncrementSize( D3D12_DESCRIPTOR_HEAP_TYPE_DSV );
    }
    {
        D3D12_DESCRIPTOR_HEAP_DESC desc = {};
        desc.NumDescriptors = MAX_STATIC_SRVS + MAX_TRANSIENT_SRVS;
        desc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
        desc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
        ThrowIfFailed( m_device->CreateDescriptorHeap( &desc, IID_PPV_ARGS( &m_srvHeap ) ), "CreateDescriptorHeap (SRV) failed" );
        m_srvDescSize = m_device->GetDescriptorHandleIncrementSize( D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV );
    }
    {
        // CPU-only staging heap for persistent SRV storage (source for descriptor copies)
        D3D12_DESCRIPTOR_HEAP_DESC desc = {};
        desc.NumDescriptors = MAX_STATIC_SRVS;
        desc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
        desc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE; // CPU-only, can be read for copies
        ThrowIfFailed( m_device->CreateDescriptorHeap( &desc, IID_PPV_ARGS( &m_srvStagingHeap ) ), "CreateDescriptorHeap (staging) failed" );
    }

    // Swap chain RTVs
    for ( int i = 0; i < FRAME_COUNT; ++i )
    {
        ThrowIfFailed( m_swapChain->GetBuffer( (UINT)i, IID_PPV_ARGS( &m_renderTargets[i] ) ), "SwapChain GetBuffer failed" );
        m_device->CreateRenderTargetView( m_renderTargets[i], nullptr, GetRTVHandle( (UINT)i ) );
    }

    // Depth stencil
    CreateDepthStencil( width, height );

    // Command allocators (one per frame) and command list
    for ( int i = 0; i < FRAME_COUNT; ++i )
    {
        ThrowIfFailed( m_device->CreateCommandAllocator( D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS( &m_commandAllocators[i] ) ), "CreateCommandAllocator failed" );
    }
    ThrowIfFailed( m_device->CreateCommandList( 0, D3D12_COMMAND_LIST_TYPE_DIRECT, m_commandAllocators[0], nullptr, IID_PPV_ARGS( &m_commandList ) ), "CreateCommandList failed" );
    m_commandList->Close();
    m_commandListOpen = false;
    m_allocatorIndex = 0;

    // Fence
    ThrowIfFailed( m_device->CreateFence( 0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS( &m_fence ) ), "CreateFence failed" );
    m_fenceEvent = CreateEvent( nullptr, FALSE, FALSE, nullptr );

    // Upload buffer
    {
        D3D12_HEAP_PROPERTIES heapProps = {};
        heapProps.Type = D3D12_HEAP_TYPE_UPLOAD;
        D3D12_RESOURCE_DESC desc = {};
        desc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
        desc.Width = UPLOAD_BUFFER_SIZE;
        desc.Height = 1;
        desc.DepthOrArraySize = 1;
        desc.MipLevels = 1;
        desc.SampleDesc.Count = 1;
        desc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
        ThrowIfFailed( m_device->CreateCommittedResource( &heapProps, D3D12_HEAP_FLAG_NONE, &desc, D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS( &m_uploadBuffer ) ), "CreateCommittedResource (upload) failed" );
        m_uploadBuffer->Map( 0, nullptr, (void**)&m_uploadBufferMapped );
    }

    // Root signature
    CreateRootSignature();

    // GPU timestamp query heap and readback buffer for profiler overlay
    {
        D3D12_QUERY_HEAP_DESC qhDesc = {};
        qhDesc.Type = D3D12_QUERY_HEAP_TYPE_TIMESTAMP;
        qhDesc.Count = (UINT)TIMER_HEAP_SIZE;
        if ( SUCCEEDED( m_device->CreateQueryHeap( &qhDesc, IID_PPV_ARGS( &m_timerQueryHeap ) ) ) )
        {
            D3D12_HEAP_PROPERTIES hp = {};
            hp.Type = D3D12_HEAP_TYPE_READBACK;
            D3D12_RESOURCE_DESC rd = {};
            rd.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
            rd.Width = (UINT64)TIMER_HEAP_SIZE * sizeof( uint64_t );
            rd.Height = 1;
            rd.DepthOrArraySize = 1;
            rd.MipLevels = 1;
            rd.SampleDesc.Count = 1;
            rd.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
            if ( FAILED( m_device->CreateCommittedResource( &hp, D3D12_HEAP_FLAG_NONE, &rd, D3D12_RESOURCE_STATE_COPY_DEST, nullptr, IID_PPV_ARGS( &m_timerReadbackBuf ) ) ) )
            {
                m_timerQueryHeap->Release();
                m_timerQueryHeap = nullptr;
            }
            else
            {
                m_commandQueue->GetTimestampFrequency( &m_timerFreq );
            }
        }
    }

    // Set initial viewport / scissor
    m_viewport = { 0.0f, 0.0f, (float)width, (float)height, 0.0f, 1.0f };
    m_scissorRect = { 0, 0, (LONG)width, (LONG)height };

    // Set default render targets
    m_currentRTV = GetRTVHandle( m_frameIndex );
    m_currentDSV = GetDSVHandle( 0 );

    return true;
}


void RenderBackendDX12::CreateRootSignature()
{
    D3D12_DESCRIPTOR_RANGE1 srvRange0 = {};
    srvRange0.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
    srvRange0.NumDescriptors = 1;
    srvRange0.BaseShaderRegister = 0;
    srvRange0.RegisterSpace = 0;
    srvRange0.OffsetInDescriptorsFromTableStart = 0;

    D3D12_DESCRIPTOR_RANGE1 srvRange1 = {};
    srvRange1.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
    srvRange1.NumDescriptors = 1;
    srvRange1.BaseShaderRegister = 1;
    srvRange1.RegisterSpace = 0;
    srvRange1.OffsetInDescriptorsFromTableStart = 0;

    D3D12_ROOT_PARAMETER1 params[3] = {};
    params[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
    params[0].Descriptor.ShaderRegister = 0;
    params[0].Descriptor.RegisterSpace = 0;
    params[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;

    params[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    params[1].DescriptorTable.NumDescriptorRanges = 1;
    params[1].DescriptorTable.pDescriptorRanges = &srvRange0;
    params[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;

    params[2].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    params[2].DescriptorTable.NumDescriptorRanges = 1;
    params[2].DescriptorTable.pDescriptorRanges = &srvRange1;
    params[2].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;

    D3D12_STATIC_SAMPLER_DESC samplers[2] = {};
    // s0: linear wrap (most textures)
    samplers[0].Filter = D3D12_FILTER_MIN_MAG_MIP_LINEAR;
    samplers[0].AddressU = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
    samplers[0].AddressV = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
    samplers[0].AddressW = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
    samplers[0].ShaderRegister = 0;
    samplers[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;

    // s1: linear clamp (FBO textures)
    samplers[1].Filter = D3D12_FILTER_MIN_MAG_MIP_LINEAR;
    samplers[1].AddressU = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
    samplers[1].AddressV = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
    samplers[1].AddressW = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
    samplers[1].ShaderRegister = 1;
    samplers[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;

    D3D12_VERSIONED_ROOT_SIGNATURE_DESC rootSigDesc = {};
    rootSigDesc.Version = D3D_ROOT_SIGNATURE_VERSION_1_1;
    rootSigDesc.Desc_1_1.NumParameters = 3;
    rootSigDesc.Desc_1_1.pParameters = params;
    rootSigDesc.Desc_1_1.NumStaticSamplers = 2;
    rootSigDesc.Desc_1_1.pStaticSamplers = samplers;
    rootSigDesc.Desc_1_1.Flags = D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;

    ID3DBlob* signature = nullptr;
    ID3DBlob* error = nullptr;
    if ( FAILED( D3D12SerializeVersionedRootSignature( &rootSigDesc, &signature, &error ) ) )
    {
        std::string msg = "Root signature serialization failed";
        if ( error )
        {
            msg += ": ";
            msg += (const char*)error->GetBufferPointer();
            error->Release();
        }
        throw std::runtime_error( msg );
    }
    if ( error )
    {
        error->Release();
    }

    if ( FAILED( m_device->CreateRootSignature( 0, signature->GetBufferPointer(), signature->GetBufferSize(), IID_PPV_ARGS( &m_rootSignature ) ) ) )
    {
        signature->Release();
        throw std::runtime_error( "CreateRootSignature failed" );
    }
    signature->Release();
}


void RenderBackendDX12::CreateDepthStencil( int w, int h )
{
    D3D12_HEAP_PROPERTIES heapProps = {};
    heapProps.Type = D3D12_HEAP_TYPE_DEFAULT;

    D3D12_RESOURCE_DESC desc = {};
    desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    desc.Width = (UINT64)w;
    desc.Height = (UINT)h;
    desc.DepthOrArraySize = 1;
    desc.MipLevels = 1;
    desc.Format = DXGI_FORMAT_D24_UNORM_S8_UINT;
    desc.SampleDesc.Count = 1;
    desc.Flags = D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL;

    D3D12_CLEAR_VALUE clearValue = {};
    clearValue.Format = DXGI_FORMAT_D24_UNORM_S8_UINT;
    clearValue.DepthStencil.Depth = 1.0f;

    m_device->CreateCommittedResource( &heapProps, D3D12_HEAP_FLAG_NONE, &desc, D3D12_RESOURCE_STATE_DEPTH_WRITE, &clearValue, IID_PPV_ARGS( &m_depthStencil ) );
    if ( !m_depthStencil )
    {
        throw std::runtime_error( "CreateCommittedResource (depth stencil) failed" );
    }

    D3D12_DEPTH_STENCIL_VIEW_DESC dsvDesc = {};
    dsvDesc.Format = DXGI_FORMAT_D24_UNORM_S8_UINT;
    dsvDesc.ViewDimension = D3D12_DSV_DIMENSION_TEXTURE2D;
    m_device->CreateDepthStencilView( m_depthStencil, &dsvDesc, GetDSVHandle( 0 ) );
}


void RenderBackendDX12::Shutdown()
{
    if ( !m_device )
    {
        return;
    }

    WaitForGpu();

    // DXR cleanup
    ShutdownDXR();

    // GPU timer cleanup
    if ( m_timerReadbackBuf )
    {
        m_timerReadbackBuf->Release();
        m_timerReadbackBuf = nullptr;
    }
    if ( m_timerQueryHeap )
    {
        m_timerQueryHeap->Release();
        m_timerQueryHeap = nullptr;
    }

    // Report any accumulated D3D12 validation errors to dx12_validation.txt
    {
        ID3D12InfoQueue* infoQueue = nullptr;
        if ( SUCCEEDED( m_device->QueryInterface( IID_PPV_ARGS( &infoQueue ) ) ) )
        {
            UINT64 numMessages = infoQueue->GetNumStoredMessages();
            int errorCount = 0;
            FILE* fp = nullptr;
            fopen_s( &fp, "dx12_validation.txt", "w" );
            for ( UINT64 i = 0; i < numMessages; ++i )
            {
                SIZE_T msgLen = 0;
                infoQueue->GetMessage( i, nullptr, &msgLen );
                auto* msg = (D3D12_MESSAGE*)malloc( msgLen );
                if ( msg )
                {
                    infoQueue->GetMessage( i, msg, &msgLen );
                    if ( msg->Severity <= D3D12_MESSAGE_SEVERITY_ERROR )
                    {
                        ++errorCount;
                        if ( fp )
                        {
                            fprintf( fp, "[%llu] ID=%d: %s\n", i, (int)msg->ID, msg->pDescription );
                        }
                    }
                    free( msg );
                }
            }
            if ( fp )
            {
                fprintf( fp, "---\n%d\n", errorCount );
                fclose( fp );
            }
            infoQueue->Release();
        }
    }

    // PSO cache
    for ( auto& pair : m_psoCache )
    {
        pair.second->Release();
    }
    m_psoCache.clear();

    // Instanced meshes
    for ( auto& im : m_instancedMeshes )
    {
        if ( im.staticVB )
        {
            im.staticVB->Release();
        }
    }
    m_instancedMeshes.clear();
    m_dynamicVBs.clear();

    // Textures
    for ( auto& tex : m_textures )
    {
        if ( tex.owned && tex.resource )
        {
            tex.resource->Release();
        }
    }
    m_textures.clear();

    if ( m_uploadBuffer )
    {
        m_uploadBuffer->Unmap( 0, nullptr );
        m_uploadBuffer->Release();
    }
    if ( m_depthStencil )
    {
        m_depthStencil->Release();
    }
    if ( m_rootSignature )
    {
        m_rootSignature->Release();
    }
    if ( m_fence )
    {
        m_fence->Release();
    }
    if ( m_fenceEvent )
    {
        CloseHandle( m_fenceEvent );
    }
    if ( m_commandList )
    {
        m_commandList->Release();
    }
    for ( int i = 0; i < FRAME_COUNT; ++i )
    {
        if ( m_commandAllocators[i] )
        {
            m_commandAllocators[i]->Release();
        }
    }
    for ( int i = 0; i < FRAME_COUNT; ++i )
    {
        if ( m_renderTargets[i] )
        {
            m_renderTargets[i]->Release();
        }
    }
    if ( m_srvHeap )
    {
        m_srvHeap->Release();
    }
    if ( m_srvStagingHeap )
    {
        m_srvStagingHeap->Release();
    }
    if ( m_dsvHeap )
    {
        m_dsvHeap->Release();
    }
    if ( m_rtvHeap )
    {
        m_rtvHeap->Release();
    }
    if ( m_swapChain )
    {
        m_swapChain->Release();
    }
    if ( m_commandQueue )
    {
        m_commandQueue->Release();
    }
    if ( m_device )
    {
        m_device->Release();
        m_device = nullptr;
    }
    if ( m_factory )
    {
        m_factory->Release();
        m_factory = nullptr;
    }

    s_instance = nullptr;
}


// --- Frame Management ---


void RenderBackendDX12::Present()
{
    EnsureCommandListOpen();

    // Resolve GPU timer queries for this frame into the readback buffer
    if ( m_timerQueryHeap )
    {
        m_commandList->ResolveQueryData( m_timerQueryHeap, D3D12_QUERY_TYPE_TIMESTAMP, 0, (UINT)TIMER_HEAP_SIZE, m_timerReadbackBuf, 0 );
        m_timerReadPending = true;
    }

    TransitionBarrier( m_renderTargets[m_frameIndex], D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_PRESENT );
    m_backBufferIsRT = false;
    m_commandList->Close();
    m_commandListOpen = false;

    ID3D12CommandList* ppCLs[] = { m_commandList };
    m_commandQueue->ExecuteCommandLists( 1, ppCLs );
    m_swapChain->Present( 1, 0 );

    // Signal fence for this frame's allocator so we know when GPU is done with it
    m_frameFenceValues[m_allocatorIndex] = ++m_fenceValue;
    m_commandQueue->Signal( m_fence, m_fenceValue );

    // Advance to next frame's allocator and swap chain buffer
    m_allocatorIndex = ( m_allocatorIndex + 1 ) % FRAME_COUNT;
    m_frameIndex = m_swapChain->GetCurrentBackBufferIndex();
    m_currentRTV = GetRTVHandle( m_frameIndex );
}


void RenderBackendDX12::Finish()
{
    if ( m_commandListOpen )
    {
        m_commandList->Close();
        m_commandListOpen = false;
        ID3D12CommandList* ppCLs[] = { m_commandList };
        m_commandQueue->ExecuteCommandLists( 1, ppCLs );
    }
    WaitForGpu();

    // Read back GPU timer results — safe because WaitForGpu() guarantees completion
    if ( m_timerQueryHeap && m_timerReadPending )
    {
        D3D12_RANGE readRange = { 0, (SIZE_T)TIMER_HEAP_SIZE * sizeof( uint64_t ) };
        uint64_t* pData = nullptr;
        if ( SUCCEEDED( m_timerReadbackBuf->Map( 0, &readRange, (void**)&pData ) ) )
        {
            for ( int i = 0; i < TIMER_HEAP_MARKERS; ++i )
            {
                uint64_t t0 = pData[i * 2 + 0];
                uint64_t t1 = pData[i * 2 + 1];
                if ( t1 > t0 && m_timerFreq > 0 )
                {
                    m_timerResultMs[i] = static_cast<float>( static_cast<double>( t1 - t0 ) / static_cast<double>( m_timerFreq ) * 1000.0 );
                    m_timerResultValid[i] = true;
                }
            }
            D3D12_RANGE writeRange = { 0, 0 };
            m_timerReadbackBuf->Unmap( 0, &writeRange );
        }
        m_timerReadPending = false;
    }
}


void RenderBackendDX12::FlushGPU()
{
    if ( m_commandListOpen )
    {
        m_commandList->Close();
        m_commandListOpen = false;
        ID3D12CommandList* ppCLs[] = { m_commandList };
        m_commandQueue->ExecuteCommandLists( 1, ppCLs );
    }
    WaitForGpu();
}


void RenderBackendDX12::Resize( int width, int height )
{
    if ( width <= 0 || height <= 0 )
    {
        return;
    }

    WaitForGpu();

    for ( int i = 0; i < FRAME_COUNT; ++i )
    {
        m_renderTargets[i]->Release();
        m_renderTargets[i] = nullptr;
    }
    m_depthStencil->Release();
    m_depthStencil = nullptr;

    m_swapChain->ResizeBuffers( FRAME_COUNT, (UINT)width, (UINT)height, DXGI_FORMAT_R8G8B8A8_UNORM, 0 );
    m_frameIndex = m_swapChain->GetCurrentBackBufferIndex();

    for ( int i = 0; i < FRAME_COUNT; ++i )
    {
        m_swapChain->GetBuffer( (UINT)i, IID_PPV_ARGS( &m_renderTargets[i] ) );
        m_device->CreateRenderTargetView( m_renderTargets[i], nullptr, GetRTVHandle( (UINT)i ) );
    }

    CreateDepthStencil( width, height );

    m_width = width;
    m_height = height;
    m_viewport = { 0.0f, 0.0f, (float)width, (float)height, 0.0f, 1.0f };
    m_scissorRect = { 0, 0, (LONG)width, (LONG)height };
    m_currentRTV = GetRTVHandle( m_frameIndex );
    m_currentDSV = GetDSVHandle( 0 );
}


// --- Viewport & Clear ---


void RenderBackendDX12::SetViewport( int x, int y, int w, int h )
{
    m_viewport = { (float)x, (float)y, (float)w, (float)h, 0.0f, 1.0f };
    m_scissorRect = { (LONG)x, (LONG)y, (LONG)( x + w ), (LONG)( y + h ) };
    m_targetsDirty = true;
}


void RenderBackendDX12::Clear( bool color, bool depth )
{
    EnsureCommandListOpen();

    if ( !m_renderingToFBO && !m_backBufferIsRT )
    {
        TransitionBarrier( m_renderTargets[m_frameIndex], D3D12_RESOURCE_STATE_PRESENT, D3D12_RESOURCE_STATE_RENDER_TARGET );
        m_backBufferIsRT = true;
    }
    m_commandList->OMSetRenderTargets( 1, &m_currentRTV, FALSE, &m_currentDSV );
    m_commandList->RSSetViewports( 1, &m_viewport );
    m_commandList->RSSetScissorRects( 1, &m_scissorRect );

    if ( color )
    {
        m_commandList->ClearRenderTargetView( m_currentRTV, m_clearColor, 0, nullptr );
    }
    if ( depth )
    {
        m_commandList->ClearDepthStencilView( m_currentDSV, D3D12_CLEAR_FLAG_DEPTH, m_clearDepth, 0, 0, nullptr );
    }
}


void RenderBackendDX12::SetClearColor( float r, float g, float b, float a )
{
    m_clearColor[0] = r;
    m_clearColor[1] = g;
    m_clearColor[2] = b;
    m_clearColor[3] = a;
}


void RenderBackendDX12::SetClearDepth( float depth )
{
    m_clearDepth = depth;
}


// --- Render State ---


void RenderBackendDX12::SetDepthTest( bool enable )
{
    if ( m_depthTestEnabled != enable )
    {
        m_depthTestEnabled = enable;
        m_psoDirty = true;
    }
}


void RenderBackendDX12::SetBlend( bool enable )
{
    if ( m_blendEnabled != enable )
    {
        m_blendEnabled = enable;
        m_psoDirty = true;
    }
}


void RenderBackendDX12::SetBlendFunc( BlendFactor src, BlendFactor dst )
{
    if ( m_blendSrc != src || m_blendDst != dst )
    {
        m_blendSrc = src;
        m_blendDst = dst;
        m_psoDirty = true;
    }
}


void RenderBackendDX12::SetCullFace( bool enable )
{
    if ( m_cullEnabled != enable )
    {
        m_cullEnabled = enable;
        m_psoDirty = true;
    }
}


void RenderBackendDX12::SetPolygonOffset( bool enable, float factor, float units )
{
    if ( m_polyOffsetEnabled != enable || m_polyOffsetFactor != factor || m_polyOffsetUnits != units )
    {
        m_polyOffsetEnabled = enable;
        m_polyOffsetFactor = factor;
        m_polyOffsetUnits = units;
        m_psoDirty = true;
    }
}


void RenderBackendDX12::SetClipPlane( int /*index*/, bool /*enable*/ )
{
    // Clip planes handled via shader constants (same as DX11)
}


// --- PSO Management ---


static D3D12_BLEND MapBlendFactor( BlendFactor f )
{
    switch ( f )
    {
    case BlendFactor::Zero:
        return D3D12_BLEND_ZERO;
    case BlendFactor::One:
        return D3D12_BLEND_ONE;
    case BlendFactor::SrcAlpha:
        return D3D12_BLEND_SRC_ALPHA;
    case BlendFactor::OneMinusSrcAlpha:
        return D3D12_BLEND_INV_SRC_ALPHA;
    default:
        return D3D12_BLEND_ONE;
    }
}


size_t RenderBackendDX12::HashPSOKey( const PSOKey12& key )
{
    size_t h = 0;
    auto hashCombine = []( size_t& seed, size_t val )
    {
        seed ^= val + 0x9e3779b9 + ( seed << 6 ) + ( seed >> 2 );
    };
    hashCombine( h, (size_t)key.shaderVS );
    hashCombine( h, (size_t)key.shaderPS );
    hashCombine( h, (size_t)key.format );
    hashCombine( h, (size_t)key.isInstanced );
    hashCombine( h, (size_t)key.blendEnabled );
    hashCombine( h, (size_t)key.blendSrc );
    hashCombine( h, (size_t)key.blendDst );
    hashCombine( h, (size_t)key.depthEnabled );
    hashCombine( h, (size_t)key.cullEnabled );
    hashCombine( h, (size_t)key.polyOffsetEnabled );
    return h;
}


void RenderBackendDX12::BuildInputLayout( VertexFormat12 format, D3D12_INPUT_ELEMENT_DESC* out, UINT& count )
{
    count = 0;
    switch ( format )
    {
    case VertexFormat12::Pos3:
        out[0] = { "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 };
        count = 1;
        break;
    case VertexFormat12::Pos3_Tex2:
        out[0] = { "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 };
        out[1] = { "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 12, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 };
        count = 2;
        break;
    case VertexFormat12::Pos3_Norm3_Tex2:
        out[0] = { "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 };
        out[1] = { "NORMAL", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 12, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 };
        out[2] = { "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 24, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 };
        count = 3;
        break;
    case VertexFormat12::Pos2_Tex2:
        out[0] = { "POSITION", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 0, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 };
        out[1] = { "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 8, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 };
        count = 2;
        break;
    case VertexFormat12::Pos2:
        out[0] = { "POSITION", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 0, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 };
        count = 1;
        break;
    }
}


void RenderBackendDX12::BuildInstancedInputLayout( const InstancedMeshDX12& im, D3D12_INPUT_ELEMENT_DESC* out, UINT& count )
{
    count = 0;

    // Slot 0: static vertex data
    if ( im.numStaticAttribs > 0 )
    {
        // Multi-attribute layout (e.g. POSITION + NORMAL + TEXCOORD)
        static const char* staticSemantics[] = { "POSITION", "NORMAL", "TEXCOORD" };
        UINT staticOffset = 0;
        for ( int i = 0; i < im.numStaticAttribs; ++i )
        {
            DXGI_FORMAT fmt = DXGI_FORMAT_R32_FLOAT;
            if ( im.staticAttribSizes[i] == 2 )
            {
                fmt = DXGI_FORMAT_R32G32_FLOAT;
            }
            else if ( im.staticAttribSizes[i] == 3 )
            {
                fmt = DXGI_FORMAT_R32G32B32_FLOAT;
            }
            else if ( im.staticAttribSizes[i] == 4 )
            {
                fmt = DXGI_FORMAT_R32G32B32A32_FLOAT;
            }

            out[count].SemanticName = staticSemantics[i < 3 ? i : 2];
            out[count].SemanticIndex = ( i >= 2 ) ? (UINT)( i - 2 ) : 0;
            out[count].Format = fmt;
            out[count].InputSlot = 0;
            out[count].AlignedByteOffset = staticOffset;
            out[count].InputSlotClass = D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA;
            out[count].InstanceDataStepRate = 0;
            ++count;
            staticOffset += (UINT)im.staticAttribSizes[i] * sizeof( float );
        }
    }
    else
    {
        // Legacy: single POSITION attribute
        out[count++] = { "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 };
    }

    // Slot 1: per-instance attributes
    UINT instOffset = 0;
    for ( int i = 0; i < im.numInstanceAttribs; ++i )
    {
        DXGI_FORMAT fmt = DXGI_FORMAT_R32_FLOAT;
        if ( im.instanceAttribSizes[i] == 2 )
        {
            fmt = DXGI_FORMAT_R32G32_FLOAT;
        }
        else if ( im.instanceAttribSizes[i] == 3 )
        {
            fmt = DXGI_FORMAT_R32G32B32_FLOAT;
        }
        else if ( im.instanceAttribSizes[i] == 4 )
        {
            fmt = DXGI_FORMAT_R32G32B32A32_FLOAT;
        }

        out[count].SemanticName = "TEXCOORD";
        out[count].SemanticIndex = (UINT)( im.instanceStartAttrib + i - 2 );
        out[count].Format = fmt;
        out[count].InputSlot = 1;
        out[count].AlignedByteOffset = instOffset;
        out[count].InputSlotClass = D3D12_INPUT_CLASSIFICATION_PER_INSTANCE_DATA;
        out[count].InstanceDataStepRate = 1;
        ++count;
        instOffset += (UINT)im.instanceAttribSizes[i] * sizeof( float );
    }
}


void RenderBackendDX12::BuildDynamicVBInputLayout( const DynamicVBDX12& dvb, D3D12_INPUT_ELEMENT_DESC* out, UINT& count )
{
    count = 0;
    UINT offset = 0;
    for ( int i = 0; i < dvb.numAttribs; ++i )
    {
        DXGI_FORMAT fmt = DXGI_FORMAT_R32_FLOAT;
        if ( dvb.attribComponents[i] == 2 )
        {
            fmt = DXGI_FORMAT_R32G32_FLOAT;
        }
        else if ( dvb.attribComponents[i] == 3 )
        {
            fmt = DXGI_FORMAT_R32G32B32_FLOAT;
        }
        else if ( dvb.attribComponents[i] == 4 )
        {
            fmt = DXGI_FORMAT_R32G32B32A32_FLOAT;
        }

        if ( i == 0 )
        {
            out[count] = { "POSITION", 0, fmt, 0, offset, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 };
        }
        else
        {
            out[count] = { "TEXCOORD", (UINT)( i - 1 ), fmt, 0, offset, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 };
        }
        ++count;
        offset += (UINT)dvb.attribComponents[i] * sizeof( float );
    }
}


ID3D12PipelineState* RenderBackendDX12::CreatePSO( VertexFormat12 format, bool instanced, const InstancedMeshDX12* im, const DynamicVBDX12* dvb )
{
    D3D12_INPUT_ELEMENT_DESC elements[16] = {};
    UINT numElements = 0;

    if ( instanced && im )
    {
        BuildInstancedInputLayout( *im, elements, numElements );
    }
    else if ( dvb )
    {
        BuildDynamicVBInputLayout( *dvb, elements, numElements );
    }
    else
    {
        BuildInputLayout( format, elements, numElements );
    }

    D3D12_GRAPHICS_PIPELINE_STATE_DESC psoDesc = {};
    psoDesc.pRootSignature = m_rootSignature;
    psoDesc.VS = { m_activeShader->GetVSBytecode(), m_activeShader->GetVSBytecodeSize() };
    psoDesc.PS = { m_activeShader->GetPSBytecode(), m_activeShader->GetPSBytecodeSize() };
    psoDesc.InputLayout = { elements, numElements };
    psoDesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;

    // Rasterizer
    psoDesc.RasterizerState.FillMode = D3D12_FILL_MODE_SOLID;
    psoDesc.RasterizerState.CullMode = m_cullEnabled ? D3D12_CULL_MODE_BACK : D3D12_CULL_MODE_NONE;
    psoDesc.RasterizerState.FrontCounterClockwise = TRUE;
    if ( m_polyOffsetEnabled )
    {
        psoDesc.RasterizerState.DepthBias = (INT)m_polyOffsetUnits;
        psoDesc.RasterizerState.SlopeScaledDepthBias = m_polyOffsetFactor;
    }
    psoDesc.RasterizerState.DepthClipEnable = TRUE;

    // Depth stencil
    psoDesc.DepthStencilState.DepthEnable = m_depthTestEnabled ? TRUE : FALSE;
    psoDesc.DepthStencilState.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ALL;
    psoDesc.DepthStencilState.DepthFunc = D3D12_COMPARISON_FUNC_LESS;

    // Blend
    psoDesc.BlendState.RenderTarget[0].RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;
    if ( m_blendEnabled )
    {
        psoDesc.BlendState.RenderTarget[0].BlendEnable = TRUE;
        psoDesc.BlendState.RenderTarget[0].SrcBlend = MapBlendFactor( m_blendSrc );
        psoDesc.BlendState.RenderTarget[0].DestBlend = MapBlendFactor( m_blendDst );
        psoDesc.BlendState.RenderTarget[0].BlendOp = D3D12_BLEND_OP_ADD;
        psoDesc.BlendState.RenderTarget[0].SrcBlendAlpha = D3D12_BLEND_ONE;
        psoDesc.BlendState.RenderTarget[0].DestBlendAlpha = D3D12_BLEND_ZERO;
        psoDesc.BlendState.RenderTarget[0].BlendOpAlpha = D3D12_BLEND_OP_ADD;
    }

    psoDesc.SampleMask = UINT_MAX;
    psoDesc.NumRenderTargets = 1;
    psoDesc.RTVFormats[0] = DXGI_FORMAT_R8G8B8A8_UNORM;
    psoDesc.DSVFormat = DXGI_FORMAT_D24_UNORM_S8_UINT;
    psoDesc.SampleDesc.Count = 1;

    ID3D12PipelineState* pso = nullptr;
    HRESULT hr = m_device->CreateGraphicsPipelineState( &psoDesc, IID_PPV_ARGS( &pso ) );
    if ( FAILED( hr ) )
    {
        throw std::runtime_error( "CreateGraphicsPipelineState failed" );
    }
    return pso;
}


void RenderBackendDX12::PrepareDraw( VertexFormat12 format, bool instanced, const InstancedMeshDX12* im, const DynamicVBDX12* dvb )
{
    EnsureCommandListOpen();

    // Build PSO key
    PSOKey12 key = {};
    key.shaderVS = m_activeShader->GetVSBytecode();
    key.shaderPS = m_activeShader->GetPSBytecode();
    key.format = format;
    key.isInstanced = instanced;
    key.blendEnabled = m_blendEnabled;
    key.blendSrc = m_blendSrc;
    key.blendDst = m_blendDst;
    key.depthEnabled = m_depthTestEnabled;
    key.cullEnabled = m_cullEnabled;
    key.polyOffsetEnabled = m_polyOffsetEnabled;

    size_t psoHash = HashPSOKey( key );
    if ( dvb )
    {
        for ( int i = 0; i < dvb->numAttribs; ++i )
        {
            psoHash ^= ( (size_t)dvb->attribComponents[i] << ( i * 4 ) );
        }
    }

    // Fast path: if PSO, textures, and targets are all unchanged, only flush the CB
    bool psoChanged = ( psoHash != m_lastPSOHash );

    if ( !psoChanged && !m_texBindingsDirty && !m_targetsDirty )
    {
        // Only the constant buffer has changed (e.g. model matrix per ball)
        if ( m_activeShader )
        {
            D3D12_GPU_VIRTUAL_ADDRESS cbAddr = m_activeShader->FlushCB();
            if ( cbAddr )
            {
                m_commandList->SetGraphicsRootConstantBufferView( 0, cbAddr );
            }
        }
        return;
    }

    // Full state setup path
    if ( psoChanged )
    {
        auto it = m_psoCache.find( psoHash );
        ID3D12PipelineState* pso;
        if ( it != m_psoCache.end() )
        {
            pso = it->second;
        }
        else
        {
            pso = CreatePSO( format, instanced, im, dvb );
            m_psoCache[psoHash] = pso;
        }

        m_commandList->SetPipelineState( pso );
        m_commandList->SetGraphicsRootSignature( m_rootSignature );
        m_lastPSOHash = psoHash;
    }

    // Flush CB
    if ( m_activeShader )
    {
        D3D12_GPU_VIRTUAL_ADDRESS cbAddr = m_activeShader->FlushCB();
        if ( cbAddr )
        {
            m_commandList->SetGraphicsRootConstantBufferView( 0, cbAddr );
        }
    }

    // Bind textures only when bindings have changed
    if ( m_texBindingsDirty )
    {
        for ( int slot = 0; slot < 2; ++slot )
        {
            UINT srcIdx = m_boundTexSlot[slot];
            if ( srcIdx != UINT_MAX )
            {
                UINT transient = AllocateTransientSRV();
                D3D12_CPU_DESCRIPTOR_HANDLE dstHandle = { m_srvHeap->GetCPUDescriptorHandleForHeapStart().ptr + (SIZE_T)transient * m_srvDescSize };
                m_device->CopyDescriptorsSimple( 1, dstHandle, GetSRVStagingCpuHandle( srcIdx ), D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV );
                m_commandList->SetGraphicsRootDescriptorTable( 1 + slot, GetSRVGpuHandle( transient ) );
            }
        }
        m_texBindingsDirty = false;
    }

    // Set viewport/scissor/render targets only when changed
    if ( m_targetsDirty )
    {
        m_commandList->RSSetViewports( 1, &m_viewport );
        m_commandList->RSSetScissorRects( 1, &m_scissorRect );
        m_commandList->OMSetRenderTargets( 1, &m_currentRTV, FALSE, &m_currentDSV );
        m_targetsDirty = false;
    }

    m_commandList->IASetPrimitiveTopology( D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST );
    m_psoDirty = false;
}


void RenderBackendDX12::SetActiveShader( ShaderDX12* shader )
{
    m_activeShader = shader;
    m_psoDirty = true;
}


void RenderBackendDX12::SetCurrentTargets( D3D12_CPU_DESCRIPTOR_HANDLE rtv, D3D12_CPU_DESCRIPTOR_HANDLE dsv )
{
    m_currentRTV = rtv;
    m_currentDSV = dsv;
    m_targetsDirty = true;
}


void RenderBackendDX12::SetRenderingToFBO( bool rendering, UINT fboSrvIndex )
{
    m_renderingToFBO = rendering;
    if ( rendering && fboSrvIndex != UINT_MAX )
    {
        // Clear any texture slot still referencing the FBO color texture
        // (it's now in RENDER_TARGET state and cannot be used as SRV)
        for ( int i = 0; i < 2; ++i )
        {
            if ( m_boundTexSlot[i] == fboSrvIndex )
            {
                m_boundTexSlot[i] = UINT_MAX;
                m_texBindingsDirty = true;
            }
        }
    }
}


D3D12_CPU_DESCRIPTOR_HANDLE RenderBackendDX12::AllocateRTV()
{
    if ( m_nextRTV >= 16 )
    {
        throw std::runtime_error( "DX12 RTV heap exhausted" );
    }
    return GetRTVHandle( m_nextRTV++ );
}


D3D12_CPU_DESCRIPTOR_HANDLE RenderBackendDX12::AllocateDSV()
{
    if ( m_nextDSV >= 4 )
    {
        throw std::runtime_error( "DX12 DSV heap exhausted" );
    }
    return GetDSVHandle( m_nextDSV++ );
}


// --- Resource Creation ---


std::unique_ptr<IShader> RenderBackendDX12::CreateShader( const char* vertPath, const char* /*fragPath*/ )
{
    // Derive HLSL path from .vert path (same convention as DX11)
    std::string hlslPath( vertPath );
    auto dotPos = hlslPath.rfind( '.' );
    if ( dotPos != std::string::npos )
    {
        hlslPath = hlslPath.substr( 0, dotPos ) + ".hlsl";
    }

    auto shader = std::make_unique<ShaderDX12>();
    if ( !shader->Compile( hlslPath.c_str() ) )
    {
        throw std::runtime_error( "ShaderDX12 compilation failed: " + hlslPath );
    }
    return shader;
}


std::unique_ptr<IMesh> RenderBackendDX12::CreateMesh( const float* data, int vertexCount, bool hasNormals, bool hasTexCoords )
{
    VertexFormat12 format;
    int floatsPerVert;
    if ( hasNormals && hasTexCoords )
    {
        format = VertexFormat12::Pos3_Norm3_Tex2;
        floatsPerVert = 8;
    }
    else if ( hasTexCoords )
    {
        format = VertexFormat12::Pos3_Tex2;
        floatsPerVert = 5;
    }
    else
    {
        format = VertexFormat12::Pos3;
        floatsPerVert = 3;
    }

    EnsureCommandListOpen();
    UINT64 dataSize = (UINT64)vertexCount * floatsPerVert * sizeof( float );
    FlushUploadBufferIfNeeded( dataSize, 4 );
    D3D12_GPU_VIRTUAL_ADDRESS uploadAddr = SubAllocateUpload( dataSize, 4 );
    uint8_t* uploadPtr = GetUploadPtr( uploadAddr );

    auto mesh = std::make_unique<MeshDX12>();
    mesh->Create( m_device, m_commandList, data, vertexCount, floatsPerVert, format, uploadAddr, uploadPtr );
    return mesh;
}


std::unique_ptr<IFramebuffer> RenderBackendDX12::CreateFramebuffer( int width, int height )
{
    auto fbo = std::make_unique<FramebufferDX12>();
    fbo->Create( width, height );
    return fbo;
}


// --- Textures ---


uint32_t RenderBackendDX12::CreateTexture2D( const uint8_t* data, int w, int h, int channels, bool /*generateMips*/, bool /*linearFilter*/ )
{
    EnsureCommandListOpen();

    // Create texture resource on default heap
    D3D12_HEAP_PROPERTIES defaultHeap = {};
    defaultHeap.Type = D3D12_HEAP_TYPE_DEFAULT;

    DXGI_FORMAT fmt;
    int bytesPerPixel;
    if ( channels == 1 )
    {
        fmt = DXGI_FORMAT_R8_UNORM;
        bytesPerPixel = 1;
    }
    else
    {
        fmt = DXGI_FORMAT_R8G8B8A8_UNORM;
        bytesPerPixel = 4;
    }

    D3D12_RESOURCE_DESC texDesc = {};
    texDesc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    texDesc.Width = (UINT64)w;
    texDesc.Height = (UINT)h;
    texDesc.DepthOrArraySize = 1;
    texDesc.MipLevels = 1;
    texDesc.Format = fmt;
    texDesc.SampleDesc.Count = 1;
    texDesc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;

    ID3D12Resource* texResource = nullptr;
    m_device->CreateCommittedResource( &defaultHeap, D3D12_HEAP_FLAG_NONE, &texDesc, D3D12_RESOURCE_STATE_COPY_DEST, nullptr, IID_PPV_ARGS( &texResource ) );

    // Convert RGB to RGBA if needed (channels==1 stays as R8, channels==4 is already RGBA)
    std::vector<uint8_t> rgba;
    const uint8_t* srcData = data;
    if ( channels == 3 )
    {
        rgba.resize( (size_t)w * h * 4 );
        for ( int i = 0; i < w * h; ++i )
        {
            rgba[i * 4 + 0] = data[i * 3 + 0];
            rgba[i * 4 + 1] = data[i * 3 + 1];
            rgba[i * 4 + 2] = data[i * 3 + 2];
            rgba[i * 4 + 3] = 255;
        }
        srcData = rgba.data();
        bytesPerPixel = 4;
    }

    // Upload via upload buffer
    D3D12_PLACED_SUBRESOURCE_FOOTPRINT footprint = {};
    UINT numRows = 0;
    UINT64 rowSizeBytes = 0;
    UINT64 totalBytes = 0;
    m_device->GetCopyableFootprints( &texDesc, 0, 1, 0, &footprint, &numRows, &rowSizeBytes, &totalBytes );

    FlushUploadBufferIfNeeded( totalBytes, D3D12_TEXTURE_DATA_PLACEMENT_ALIGNMENT );
    D3D12_GPU_VIRTUAL_ADDRESS uploadAddr = SubAllocateUpload( totalBytes, D3D12_TEXTURE_DATA_PLACEMENT_ALIGNMENT );
    uint8_t* uploadDst = GetUploadPtr( uploadAddr );

    // Copy row by row (respecting pitch alignment)
    UINT srcRowPitch = (UINT)w * bytesPerPixel;
    for ( UINT row = 0; row < numRows; ++row )
    {
        memcpy( uploadDst + row * footprint.Footprint.RowPitch, srcData + row * srcRowPitch, srcRowPitch );
    }

    // Record copy command
    D3D12_TEXTURE_COPY_LOCATION dstLoc = {};
    dstLoc.pResource = texResource;
    dstLoc.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;

    D3D12_TEXTURE_COPY_LOCATION srcLoc = {};
    srcLoc.pResource = m_uploadBuffer;
    srcLoc.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
    srcLoc.PlacedFootprint = footprint;
    srcLoc.PlacedFootprint.Offset = uploadAddr - m_uploadBuffer->GetGPUVirtualAddress();

    m_commandList->CopyTextureRegion( &dstLoc, 0, 0, 0, &srcLoc, nullptr );
    TransitionBarrier( texResource, D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE );

    // Create SRV
    UINT srvIdx = AllocateStaticSRV();
    D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
    srvDesc.Format = fmt;
    srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
    srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    srvDesc.Texture2D.MipLevels = 1;
    m_device->CreateShaderResourceView( texResource, &srvDesc, GetSRVStagingCpuHandle( srvIdx ) );

    // Register in texture array (1-based)
    TextureEntryDX12 entry = {};
    entry.resource = texResource;
    entry.srvIndex = srvIdx;
    entry.owned = true;
    m_textures.push_back( entry );
    return (uint32_t)m_textures.size(); // 1-based handle
}


void RenderBackendDX12::BindTexture( uint32_t handle, int slot )
{
    if ( slot < 0 || slot > 1 )
    {
        return;
    }
    UINT newSlot;
    if ( handle == 0 || handle > (uint32_t)m_textures.size() )
    {
        newSlot = UINT_MAX;
    }
    else
    {
        newSlot = m_textures[handle - 1].srvIndex;
    }
    if ( m_boundTexSlot[slot] != newSlot )
    {
        m_boundTexSlot[slot] = newSlot;
        m_texBindingsDirty = true;
    }
}


void RenderBackendDX12::DeleteTexture( uint32_t handle )
{
    if ( handle == 0 || handle > (uint32_t)m_textures.size() )
    {
        return;
    }
    auto& entry = m_textures[handle - 1];
    if ( entry.owned && entry.resource )
    {
        entry.resource->Release();
    }
    entry.resource = nullptr;
    entry.srvIndex = UINT_MAX;
    entry.owned = false;
}


UINT RenderBackendDX12::RegisterSRV( UINT srvIndex )
{
    TextureEntryDX12 entry = {};
    entry.resource = nullptr;
    entry.srvIndex = srvIndex;
    entry.owned = false;
    m_textures.push_back( entry );
    return (uint32_t)m_textures.size(); // 1-based handle
}


void RenderBackendDX12::UnregisterSRV( uint32_t handle )
{
    if ( handle == 0 || handle > (uint32_t)m_textures.size() )
    {
        return;
    }
    auto& entry = m_textures[handle - 1];
    entry.resource = nullptr;
    entry.srvIndex = UINT_MAX;
    entry.owned = false;
}


// --- Screenshot ---


std::vector<uint8_t> RenderBackendDX12::CaptureBackbuffer( int& outWidth, int& outHeight )
{
    EnsureCommandListOpen();
    outWidth = m_width;
    outHeight = m_height;

    // Transition backbuffer to copy source
    TransitionBarrier( m_renderTargets[m_frameIndex], D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_COPY_SOURCE );

    // Get copyable footprint
    D3D12_RESOURCE_DESC bbDesc = m_renderTargets[m_frameIndex]->GetDesc();
    D3D12_PLACED_SUBRESOURCE_FOOTPRINT footprint = {};
    UINT numRows = 0;
    UINT64 rowSizeBytes = 0;
    UINT64 totalBytes = 0;
    m_device->GetCopyableFootprints( &bbDesc, 0, 1, 0, &footprint, &numRows, &rowSizeBytes, &totalBytes );

    // Create readback buffer
    D3D12_HEAP_PROPERTIES readbackHeap = {};
    readbackHeap.Type = D3D12_HEAP_TYPE_READBACK;
    D3D12_RESOURCE_DESC readbackDesc = {};
    readbackDesc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    readbackDesc.Width = totalBytes;
    readbackDesc.Height = 1;
    readbackDesc.DepthOrArraySize = 1;
    readbackDesc.MipLevels = 1;
    readbackDesc.SampleDesc.Count = 1;
    readbackDesc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;

    ID3D12Resource* readbackBuffer = nullptr;
    m_device->CreateCommittedResource( &readbackHeap, D3D12_HEAP_FLAG_NONE, &readbackDesc, D3D12_RESOURCE_STATE_COPY_DEST, nullptr, IID_PPV_ARGS( &readbackBuffer ) );

    // Copy texture to readback buffer
    D3D12_TEXTURE_COPY_LOCATION dstLoc = {};
    dstLoc.pResource = readbackBuffer;
    dstLoc.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
    dstLoc.PlacedFootprint = footprint;

    D3D12_TEXTURE_COPY_LOCATION srcLoc = {};
    srcLoc.pResource = m_renderTargets[m_frameIndex];
    srcLoc.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;

    m_commandList->CopyTextureRegion( &dstLoc, 0, 0, 0, &srcLoc, nullptr );

    // Transition backbuffer back to render target
    TransitionBarrier( m_renderTargets[m_frameIndex], D3D12_RESOURCE_STATE_COPY_SOURCE, D3D12_RESOURCE_STATE_RENDER_TARGET );

    // Execute and wait
    m_commandList->Close();
    m_commandListOpen = false;
    ID3D12CommandList* ppCLs[] = { m_commandList };
    m_commandQueue->ExecuteCommandLists( 1, ppCLs );
    WaitForGpu();

    // Map and read pixels
    void* mappedData = nullptr;
    D3D12_RANGE readRange = { 0, (SIZE_T)totalBytes };
    readbackBuffer->Map( 0, &readRange, &mappedData );

    // Convert RGBA top-down → BGR bottom-up (BMP format)
    int rowStride = ( m_width * 3 + 3 ) & ~3;
    std::vector<uint8_t> result( (size_t)rowStride * m_height );

    const uint8_t* src = (const uint8_t*)mappedData;
    for ( int y = 0; y < m_height; ++y )
    {
        int flippedY = m_height - 1 - y;
        const uint8_t* srcRow = src + (size_t)y * footprint.Footprint.RowPitch;
        uint8_t* dstRow = result.data() + (size_t)flippedY * rowStride;
        for ( int x = 0; x < m_width; ++x )
        {
            dstRow[x * 3 + 0] = srcRow[x * 4 + 2]; // B
            dstRow[x * 3 + 1] = srcRow[x * 4 + 1]; // G
            dstRow[x * 3 + 2] = srcRow[x * 4 + 0]; // R
        }
    }

    D3D12_RANGE writeRange = { 0, 0 };
    readbackBuffer->Unmap( 0, &writeRange );
    readbackBuffer->Release();

    return result;
}


// --- Dynamic VB ---


uint32_t RenderBackendDX12::CreateDynamicVB( const int* attribComponents, int numAttribs, int maxVertices )
{
    DynamicVBDX12 dvb = {};
    dvb.numAttribs = numAttribs;
    dvb.maxVertices = maxVertices;
    int totalFloats = 0;
    for ( int i = 0; i < numAttribs && i < 8; ++i )
    {
        dvb.attribComponents[i] = attribComponents[i];
        totalFloats += attribComponents[i];
    }
    dvb.floatsPerVertex = totalFloats;
    dvb.stride = totalFloats * (int)sizeof( float );
    m_dynamicVBs.push_back( dvb );
    return (uint32_t)m_dynamicVBs.size(); // 1-based
}


void RenderBackendDX12::UploadAndDrawDynamicVB( uint32_t handle, const float* data, int vertexCount )
{
    if ( handle == 0 || handle > (uint32_t)m_dynamicVBs.size() || vertexCount <= 0 )
    {
        return;
    }
    DynamicVBDX12& dvb = m_dynamicVBs[handle - 1];

    EnsureCommandListOpen();

    // Sub-allocate from upload buffer for vertex data
    UINT64 dataSize = (UINT64)vertexCount * dvb.stride;
    D3D12_GPU_VIRTUAL_ADDRESS vbAddr = SubAllocateUpload( dataSize, 4 );
    memcpy( GetUploadPtr( vbAddr ), data, (size_t)dataSize );

    // Determine vertex format
    VertexFormat12 fmt = VertexFormat12::Pos2_Tex2;
    if ( dvb.numAttribs == 2 && dvb.attribComponents[0] == 2 && dvb.attribComponents[1] == 2 )
    {
        fmt = VertexFormat12::Pos2_Tex2;
    }

    PrepareDraw( fmt, false, nullptr, &dvb );

    D3D12_VERTEX_BUFFER_VIEW vbv = {};
    vbv.BufferLocation = vbAddr;
    vbv.SizeInBytes = (UINT)dataSize;
    vbv.StrideInBytes = (UINT)dvb.stride;
    m_commandList->IASetVertexBuffers( 0, 1, &vbv );
    m_commandList->DrawInstanced( (UINT)vertexCount, 1, 0, 0 );
}


void RenderBackendDX12::DestroyDynamicVB( uint32_t /*handle*/ )
{
    // No GPU resources to release — upload buffer is shared
}


// --- Instanced mesh ---


uint32_t RenderBackendDX12::CreateInstancedMesh( const float* staticData, int staticVertCount, int staticFloatsPerVert, int /*maxInstances*/, int instanceFloats, int instanceStartAttrib, const int* instanceAttribSizes, int numInstanceAttribs, const int* staticAttribSizes, int numStaticAttribs )
{
    EnsureCommandListOpen();

    InstancedMeshDX12 im = {};
    im.staticFloatsPerVert = staticFloatsPerVert;
    im.staticStride = staticFloatsPerVert * (int)sizeof( float );
    im.instanceFloats = instanceFloats;
    im.instanceStride = instanceFloats * (int)sizeof( float );
    im.instanceStartAttrib = instanceStartAttrib;
    im.numInstanceAttribs = numInstanceAttribs;
    im.numStaticAttribs = numStaticAttribs;
    for ( int i = 0; i < numInstanceAttribs && i < 8; ++i )
    {
        im.instanceAttribSizes[i] = instanceAttribSizes[i];
    }
    for ( int i = 0; i < numStaticAttribs && i < 8; ++i )
    {
        im.staticAttribSizes[i] = staticAttribSizes[i];
    }

    // Create static VB on default heap
    UINT64 dataSize = (UINT64)staticVertCount * staticFloatsPerVert * sizeof( float );

    D3D12_HEAP_PROPERTIES defaultHeap = {};
    defaultHeap.Type = D3D12_HEAP_TYPE_DEFAULT;
    D3D12_RESOURCE_DESC bufDesc = {};
    bufDesc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    bufDesc.Width = dataSize;
    bufDesc.Height = 1;
    bufDesc.DepthOrArraySize = 1;
    bufDesc.MipLevels = 1;
    bufDesc.SampleDesc.Count = 1;
    bufDesc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;

    m_device->CreateCommittedResource( &defaultHeap, D3D12_HEAP_FLAG_NONE, &bufDesc, D3D12_RESOURCE_STATE_COPY_DEST, nullptr, IID_PPV_ARGS( &im.staticVB ) );

    // Upload static data
    FlushUploadBufferIfNeeded( dataSize, 4 );
    D3D12_GPU_VIRTUAL_ADDRESS uploadAddr = SubAllocateUpload( dataSize, 4 );
    memcpy( GetUploadPtr( uploadAddr ), staticData, (size_t)dataSize );
    m_commandList->CopyBufferRegion( im.staticVB, 0, m_uploadBuffer, uploadAddr - m_uploadBuffer->GetGPUVirtualAddress(), dataSize );
    TransitionBarrier( im.staticVB, D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER );

    im.staticVBV.BufferLocation = im.staticVB->GetGPUVirtualAddress();
    im.staticVBV.SizeInBytes = (UINT)dataSize;
    im.staticVBV.StrideInBytes = (UINT)im.staticStride;

    m_instancedMeshes.push_back( im );
    return (uint32_t)m_instancedMeshes.size(); // 1-based
}


void RenderBackendDX12::UploadInstanceData( uint32_t handle, const float* data, int floatCount )
{
    if ( handle == 0 || handle > (uint32_t)m_instancedMeshes.size() || floatCount <= 0 )
    {
        return;
    }
    InstancedMeshDX12& im = m_instancedMeshes[handle - 1];

    EnsureCommandListOpen();

    UINT64 dataSize = (UINT64)floatCount * sizeof( float );
    D3D12_GPU_VIRTUAL_ADDRESS addr = SubAllocateUpload( dataSize, 4 );
    memcpy( GetUploadPtr( addr ), data, (size_t)dataSize );

    im.instanceDataAddr = addr;
    im.instanceDataSize = (UINT)dataSize;
}


void RenderBackendDX12::DrawInstancedMesh( uint32_t handle, int staticVertCount, int instanceCount )
{
    if ( handle == 0 || handle > (uint32_t)m_instancedMeshes.size() || instanceCount <= 0 )
    {
        return;
    }
    InstancedMeshDX12& im = m_instancedMeshes[handle - 1];

    if ( im.instanceDataAddr == 0 )
    {
        return; // No instance data uploaded yet
    }

    PrepareDraw( VertexFormat12::Pos3, true, &im, nullptr );

    // Slot 0: static geometry, Slot 1: per-instance data
    D3D12_VERTEX_BUFFER_VIEW vbvs[2] = {};
    vbvs[0] = im.staticVBV;
    vbvs[1].BufferLocation = im.instanceDataAddr;
    vbvs[1].SizeInBytes = im.instanceDataSize;
    vbvs[1].StrideInBytes = (UINT)im.instanceStride;

    m_commandList->IASetVertexBuffers( 0, 2, vbvs );
    m_commandList->DrawInstanced( (UINT)staticVertCount, (UINT)instanceCount, 0, 0 );
}


void RenderBackendDX12::DestroyInstancedMesh( uint32_t handle )
{
    if ( handle == 0 || handle > (uint32_t)m_instancedMeshes.size() )
    {
        return;
    }
    auto& im = m_instancedMeshes[handle - 1];
    if ( im.staticVB )
    {
        im.staticVB->Release();
        im.staticVB = nullptr;
    }
}


// --- Queries ---


int RenderBackendDX12::GetWidth() const
{
    return m_width;
}


int RenderBackendDX12::GetHeight() const
{
    return m_height;
}


bool RenderBackendDX12::IsDepthTestEnabled() const
{
    return m_depthTestEnabled;
}


bool RenderBackendDX12::IsBlendEnabled() const
{
    return m_blendEnabled;
}


bool RenderBackendDX12::UsesZeroToOneDepth() const
{
    return true; // DX12 uses [0,1] depth range like DX11
}


// --- DXR Raytracing ---


void RenderBackendDX12::CheckDXRSupport()
{
    m_dxrSupported = false;
    m_device5 = nullptr;
    m_cmdList4 = nullptr;

    // Check raytracing tier
    D3D12_FEATURE_DATA_D3D12_OPTIONS5 opts5 = {};
    if ( FAILED( m_device->CheckFeatureSupport( D3D12_FEATURE_D3D12_OPTIONS5, &opts5, sizeof( opts5 ) ) ) )
    {
        return;
    }
    if ( opts5.RaytracingTier < D3D12_RAYTRACING_TIER_1_0 )
    {
        return;
    }

    // QueryInterface for DXR interfaces
    if ( FAILED( m_device->QueryInterface( IID_PPV_ARGS( &m_device5 ) ) ) )
    {
        return;
    }

    m_dxrSupported = true;
}


void RenderBackendDX12::CreateRTRootSignature()
{
    // RT root signature layout:
    // [0] SRV - TLAS (t0, space1) — inline raw descriptor
    // [1] UAV - output texture (u0) — descriptor table
    // [2] CBV - RT constants (b1) — inline raw descriptor
    // [3] SRV - texture table (t0..t7, space0) — 8-descriptor table:
    //           t0=sphere, t1=terrain, t2=skyUp, t3=skyDown, t4=skyRight, t5=skyLeft, t6=skyFront, t7=skyBack
    // [s0] Static linear-wrap sampler
    D3D12_DESCRIPTOR_RANGE1 uavRange = {};
    uavRange.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_UAV;
    uavRange.NumDescriptors = 1;
    uavRange.BaseShaderRegister = 0;
    uavRange.RegisterSpace = 0;

    D3D12_DESCRIPTOR_RANGE1 texRange = {};
    texRange.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
    texRange.NumDescriptors = 8;
    texRange.BaseShaderRegister = 0;
    texRange.RegisterSpace = 0;

    D3D12_ROOT_PARAMETER1 params[4] = {};
    // Slot 0: TLAS SRV (inline)
    params[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_SRV;
    params[0].Descriptor.ShaderRegister = 0;
    params[0].Descriptor.RegisterSpace = 1;
    params[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;

    // Slot 1: UAV descriptor table
    params[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    params[1].DescriptorTable.NumDescriptorRanges = 1;
    params[1].DescriptorTable.pDescriptorRanges = &uavRange;
    params[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;

    // Slot 2: CBV for RT constants
    params[2].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
    params[2].Descriptor.ShaderRegister = 1;
    params[2].Descriptor.RegisterSpace = 0;
    params[2].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;

    // Slot 3: SRV descriptor table for sphere + terrain textures
    params[3].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    params[3].DescriptorTable.NumDescriptorRanges = 1;
    params[3].DescriptorTable.pDescriptorRanges = &texRange;
    params[3].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;

    // Static linear-wrap sampler at s0
    D3D12_STATIC_SAMPLER_DESC samplerDesc = {};
    samplerDesc.Filter = D3D12_FILTER_MIN_MAG_MIP_LINEAR;
    samplerDesc.AddressU = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
    samplerDesc.AddressV = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
    samplerDesc.AddressW = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
    samplerDesc.MaxAnisotropy = 1;
    samplerDesc.MaxLOD = D3D12_FLOAT32_MAX;
    samplerDesc.ShaderRegister = 0;
    samplerDesc.RegisterSpace = 0;
    samplerDesc.ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;

    D3D12_VERSIONED_ROOT_SIGNATURE_DESC rootSigDesc = {};
    rootSigDesc.Version = D3D_ROOT_SIGNATURE_VERSION_1_1;
    rootSigDesc.Desc_1_1.NumParameters = 4;
    rootSigDesc.Desc_1_1.pParameters = params;
    rootSigDesc.Desc_1_1.NumStaticSamplers = 1;
    rootSigDesc.Desc_1_1.pStaticSamplers = &samplerDesc;
    rootSigDesc.Desc_1_1.Flags = D3D12_ROOT_SIGNATURE_FLAG_NONE;

    ID3DBlob* signature = nullptr;
    ID3DBlob* error = nullptr;
    if ( FAILED( D3D12SerializeVersionedRootSignature( &rootSigDesc, &signature, &error ) ) )
    {
        if ( error )
        {
            error->Release();
        }
        throw std::runtime_error( "RT root signature serialization failed" );
    }
    if ( error )
    {
        error->Release();
    }

    if ( FAILED( m_device->CreateRootSignature( 0, signature->GetBufferPointer(), signature->GetBufferSize(), IID_PPV_ARGS( &m_rtRootSignature ) ) ) )
    {
        signature->Release();
        throw std::runtime_error( "CreateRootSignature (RT) failed" );
    }
    signature->Release();
}


void RenderBackendDX12::CreateRTPipeline()
{
    // Only compile HLSL → DXIL when the cached DXIL is absent.
    // Delete reflect.rt.dxil to force a recompile after editing the shader.
    FILE* existCheck = nullptr;
    fopen_s( &existCheck, "SkullbonezData/shaders/reflect.rt.dxil", "rb" );
    const bool dxilMissing = ( existCheck == nullptr );
    if ( existCheck )
    {
        fclose( existCheck );
    }

    if ( dxilMissing )
    {
        const char* dxcPaths[] = {
            "dxc.exe -T lib_6_3 -Fo SkullbonezData/shaders/reflect.rt.dxil SkullbonezData/shaders/reflect.rt.hlsl 2>nul",
            "\"C:/Program Files (x86)/Windows Kits/10/bin/10.0.22621.0/x64/dxc.exe\" -T lib_6_3 -Fo SkullbonezData/shaders/reflect.rt.dxil SkullbonezData/shaders/reflect.rt.hlsl 2>nul",
            "\"C:/Program Files (x86)/Windows Kits/10/bin/10.0.26100.0/x64/dxc.exe\" -T lib_6_3 -Fo SkullbonezData/shaders/reflect.rt.dxil SkullbonezData/shaders/reflect.rt.hlsl 2>nul" };

        bool compiled = false;
        for ( const char* cmd : dxcPaths )
        {
            if ( system( cmd ) == 0 )
            {
                compiled = true;
                break;
            }
        }
        if ( !compiled )
        {
            throw std::runtime_error( "DXC compilation of reflect.rt.hlsl failed — ensure dxc.exe is in PATH or Windows SDK is installed" );
        }
    }

    // Load compiled DXIL blob
    FILE* dxilFile = nullptr;
    fopen_s( &dxilFile, "SkullbonezData/shaders/reflect.rt.dxil", "rb" );
    if ( !dxilFile )
    {
        throw std::runtime_error( "Failed to open compiled reflect.rt.dxil" );
    }
    fseek( dxilFile, 0, SEEK_END );
    long dxilSize = ftell( dxilFile );
    fseek( dxilFile, 0, SEEK_SET );
    std::vector<uint8_t> dxilBlob( (size_t)dxilSize );
    fread( dxilBlob.data(), 1, (size_t)dxilSize, dxilFile );
    fclose( dxilFile );

    // Build RTPSO with subobjects
    // We need: DXIL library, hit groups, shader config, pipeline config, global root signature
    D3D12_DXIL_LIBRARY_DESC libDesc = {};
    libDesc.DXILLibrary.pShaderBytecode = dxilBlob.data();
    libDesc.DXILLibrary.BytecodeLength = dxilBlob.size();
    libDesc.NumExports = 0; // Export all entry points

    D3D12_HIT_GROUP_DESC terrainHitGroup = {};
    terrainHitGroup.HitGroupExport = L"TerrainHitGroup";
    terrainHitGroup.Type = D3D12_HIT_GROUP_TYPE_TRIANGLES;
    terrainHitGroup.ClosestHitShaderImport = L"ClosestHit";

    D3D12_HIT_GROUP_DESC sphereHitGroup = {};
    sphereHitGroup.HitGroupExport = L"SphereHitGroup";
    sphereHitGroup.Type = D3D12_HIT_GROUP_TYPE_TRIANGLES;
    sphereHitGroup.ClosestHitShaderImport = L"ClosestHit";

    D3D12_RAYTRACING_SHADER_CONFIG shaderConfig = {};
    shaderConfig.MaxPayloadSizeInBytes = 16;  // float3 color + float hitT
    shaderConfig.MaxAttributeSizeInBytes = 8; // float2 barycentrics

    D3D12_RAYTRACING_PIPELINE_CONFIG pipelineConfig = {};
    pipelineConfig.MaxTraceRecursionDepth = 1;

    D3D12_GLOBAL_ROOT_SIGNATURE globalRootSig = {};
    globalRootSig.pGlobalRootSignature = m_rtRootSignature;

    // Build state object description with subobjects
    D3D12_STATE_SUBOBJECT subobjects[6] = {};

    subobjects[0].Type = D3D12_STATE_SUBOBJECT_TYPE_DXIL_LIBRARY;
    subobjects[0].pDesc = &libDesc;

    subobjects[1].Type = D3D12_STATE_SUBOBJECT_TYPE_HIT_GROUP;
    subobjects[1].pDesc = &terrainHitGroup;

    subobjects[2].Type = D3D12_STATE_SUBOBJECT_TYPE_HIT_GROUP;
    subobjects[2].pDesc = &sphereHitGroup;

    subobjects[3].Type = D3D12_STATE_SUBOBJECT_TYPE_RAYTRACING_SHADER_CONFIG;
    subobjects[3].pDesc = &shaderConfig;

    subobjects[4].Type = D3D12_STATE_SUBOBJECT_TYPE_RAYTRACING_PIPELINE_CONFIG;
    subobjects[4].pDesc = &pipelineConfig;

    subobjects[5].Type = D3D12_STATE_SUBOBJECT_TYPE_GLOBAL_ROOT_SIGNATURE;
    subobjects[5].pDesc = &globalRootSig;

    D3D12_STATE_OBJECT_DESC stateObjDesc = {};
    stateObjDesc.Type = D3D12_STATE_OBJECT_TYPE_RAYTRACING_PIPELINE;
    stateObjDesc.NumSubobjects = 6;
    stateObjDesc.pSubobjects = subobjects;

    if ( FAILED( m_device5->CreateStateObject( &stateObjDesc, IID_PPV_ARGS( &m_rtPSO ) ) ) )
    {
        throw std::runtime_error( "CreateStateObject (RTPSO) failed" );
    }

    m_rtPSO->QueryInterface( IID_PPV_ARGS( &m_rtPSOProps ) );
}


void RenderBackendDX12::CreateReflectionUAV( int width, int height )
{
    m_reflectionWidth = width;
    m_reflectionHeight = height;
    m_reflectionInSRVState = false;

    // Create the reflection UAV texture
    D3D12_HEAP_PROPERTIES heapProps = {};
    heapProps.Type = D3D12_HEAP_TYPE_DEFAULT;

    D3D12_RESOURCE_DESC texDesc = {};
    texDesc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    texDesc.Width = (UINT64)width;
    texDesc.Height = (UINT)height;
    texDesc.DepthOrArraySize = 1;
    texDesc.MipLevels = 1;
    texDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    texDesc.SampleDesc.Count = 1;
    texDesc.Flags = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;

    if ( FAILED( m_device->CreateCommittedResource( &heapProps, D3D12_HEAP_FLAG_NONE, &texDesc, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, nullptr, IID_PPV_ARGS( &m_reflectionUAV ) ) ) )
    {
        throw std::runtime_error( "Failed to create DXR reflection UAV texture" );
    }

    // Create UAV descriptor
    m_reflectionUAVIndex = AllocateStaticSRV();
    D3D12_UNORDERED_ACCESS_VIEW_DESC uavDesc = {};
    uavDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    uavDesc.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2D;
    m_device->CreateUnorderedAccessView( m_reflectionUAV, nullptr, &uavDesc, GetSRVStagingCpuHandle( m_reflectionUAVIndex ) );

    // Also copy to shader-visible heap
    D3D12_CPU_DESCRIPTOR_HANDLE srvHeapCpu = m_srvHeap->GetCPUDescriptorHandleForHeapStart();
    srvHeapCpu.ptr += (SIZE_T)m_reflectionUAVIndex * m_srvDescSize;
    m_device->CopyDescriptorsSimple( 1, srvHeapCpu, GetSRVStagingCpuHandle( m_reflectionUAVIndex ), D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV );

    // Create SRV for sampling in water shader
    m_reflectionSRVIndex = AllocateStaticSRV();
    D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
    srvDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
    srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    srvDesc.Texture2D.MipLevels = 1;
    m_device->CreateShaderResourceView( m_reflectionUAV, &srvDesc, GetSRVStagingCpuHandle( m_reflectionSRVIndex ) );

    // Copy SRV to shader-visible heap
    srvHeapCpu = m_srvHeap->GetCPUDescriptorHandleForHeapStart();
    srvHeapCpu.ptr += (SIZE_T)m_reflectionSRVIndex * m_srvDescSize;
    m_device->CopyDescriptorsSimple( 1, srvHeapCpu, GetSRVStagingCpuHandle( m_reflectionSRVIndex ), D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV );
}


void RenderBackendDX12::InitDXR( uint64_t terrainVBVA, int terrainVertCount, int terrainStride, uint64_t sphereVBVA, int sphereVertCount, int sphereStride, int maxInstances )
{
    if ( !m_dxrSupported )
    {
        return;
    }

    // Get CmdList4 from command list
    if ( FAILED( m_commandList->QueryInterface( IID_PPV_ARGS( &m_cmdList4 ) ) ) )
    {
        m_dxrSupported = false;
        return;
    }

    // Create RT root signature and pipeline
    CreateRTRootSignature();
    CreateRTPipeline();

    // Create reflection UAV at 2x viewport
    CreateReflectionUAV( m_width * 2, m_height * 2 );

    // Create RT constant buffer (upload heap, persistently mapped)
    {
        D3D12_HEAP_PROPERTIES heapProps = {};
        heapProps.Type = D3D12_HEAP_TYPE_UPLOAD;
        D3D12_RESOURCE_DESC bufDesc = {};
        bufDesc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
        bufDesc.Width = 256; // Aligned to 256 bytes for CBV
        bufDesc.Height = 1;
        bufDesc.DepthOrArraySize = 1;
        bufDesc.MipLevels = 1;
        bufDesc.SampleDesc.Count = 1;
        bufDesc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;

        if ( FAILED( m_device->CreateCommittedResource( &heapProps, D3D12_HEAP_FLAG_NONE, &bufDesc, D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS( &m_rtConstantBuffer ) ) ) )
        {
            throw std::runtime_error( "Failed to create RT constant buffer" );
        }
        m_rtConstantBuffer->Map( 0, nullptr, (void**)&m_rtConstantBufferMapped );
    }

    // Build BLAS for terrain and sphere
    EnsureCommandListOpen();

    m_terrainBLAS.Build( m_device5, m_cmdList4, (D3D12_GPU_VIRTUAL_ADDRESS)terrainVBVA, terrainVertCount, terrainStride, DXGI_FORMAT_R32G32B32_FLOAT, true );
    m_sphereBLAS.Build( m_device5, m_cmdList4, (D3D12_GPU_VIRTUAL_ADDRESS)sphereVBVA, sphereVertCount, sphereStride, DXGI_FORMAT_R32G32B32_FLOAT, false );

    // Submit and wait for BLAS builds to complete
    m_commandList->Close();
    m_commandListOpen = false;
    ID3D12CommandList* ppCLs[] = { m_commandList };
    m_commandQueue->ExecuteCommandLists( 1, ppCLs );
    WaitForGpu();

    // Free scratch memory
    m_terrainBLAS.ReleaseAfterBuild();
    m_sphereBLAS.ReleaseAfterBuild();

    // Init TLAS (sized for maxInstances: terrain + all balls)
    m_tlas.Init( m_device5, maxInstances + 1 );

    // Build SBT
    m_sbt.Build( m_device, m_rtPSOProps, L"RayGen", L"Miss", L"TerrainHitGroup", L"SphereHitGroup" );
}


void RenderBackendDX12::BuildTLAS( const float* instanceTransforms, int instanceCount, uint64_t /*terrainBLAS*/, uint64_t /*sphereBLAS*/ )
{
    if ( !m_dxrSupported || !m_cmdList4 )
    {
        return;
    }

    // Build instance descriptors
    // Instance 0: terrain (identity)
    // Instance 1..N: spheres with their world transforms
    std::vector<D3D12_RAYTRACING_INSTANCE_DESC> instances( (size_t)instanceCount + 1 );

    // Terrain instance
    D3D12_RAYTRACING_INSTANCE_DESC& terrainInst = instances[0];
    memset( &terrainInst, 0, sizeof( terrainInst ) );
    terrainInst.Transform[0][0] = 1.0f;
    terrainInst.Transform[1][1] = 1.0f;
    terrainInst.Transform[2][2] = 1.0f;
    terrainInst.InstanceMask = 0xFF;
    terrainInst.InstanceContributionToHitGroupIndex = 0;
    terrainInst.AccelerationStructure = m_terrainBLAS.GetResultVA();
    terrainInst.InstanceID = 0;

    // Sphere instances
    for ( int i = 0; i < instanceCount; ++i )
    {
        D3D12_RAYTRACING_INSTANCE_DESC& inst = instances[(size_t)i + 1];
        memset( &inst, 0, sizeof( inst ) );

        // Copy 3x4 transform from the flat float array (row-major 4x4 → DXR 3x4 row-major)
        const float* m = instanceTransforms + i * 16;
        inst.Transform[0][0] = m[0];
        inst.Transform[0][1] = m[4];
        inst.Transform[0][2] = m[8];
        inst.Transform[0][3] = m[12];
        inst.Transform[1][0] = m[1];
        inst.Transform[1][1] = m[5];
        inst.Transform[1][2] = m[9];
        inst.Transform[1][3] = m[13];
        inst.Transform[2][0] = m[2];
        inst.Transform[2][1] = m[6];
        inst.Transform[2][2] = m[10];
        inst.Transform[2][3] = m[14];

        inst.InstanceMask = 0xFF;
        inst.InstanceContributionToHitGroupIndex = 1; // Sphere hit group
        inst.AccelerationStructure = m_sphereBLAS.GetResultVA();
        inst.InstanceID = (UINT)( i + 1 );
    }

    EnsureCommandListOpen();
    m_tlas.Build( m_device5, m_cmdList4, instances.data(), (int)instances.size() );
}


void RenderBackendDX12::DispatchReflectionRays( const float* invViewProj, const float* cameraPos, float waterY, float time, const float* lightPos, int width, int height, uint32_t sphereTexHandle, uint32_t terrainTexHandle, uint32_t skyUpHandle, uint32_t skyDownHandle, uint32_t skyRightHandle, uint32_t skyLeftHandle, uint32_t skyFrontHandle, uint32_t skyBackHandle )
{
    if ( !m_dxrSupported || !m_cmdList4 || !m_rtPSO )
    {
        return;
    }

    (void)width;
    (void)height;

    EnsureCommandListOpen();

    // Transition reflection UAV back to writable state if it was left as SRV from previous frame
    if ( m_reflectionInSRVState )
    {
        TransitionBarrier( m_reflectionUAV, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_UNORDERED_ACCESS );
    }

    // Update RT constant buffer
    // Layout: float4x4 invVP, float3 cameraPos, float waterY, float3 lightPos, float time, float3 skyTop, pad, float3 skyBottom, pad
    struct RTConstants
    {
        float invViewProj[16];
        float cameraPos[3];
        float waterY;
        float lightPos[3];
        float time;
        float skyColorTop[3];
        float pad0;
        float skyColorBottom[3];
        float pad1;
    };

    RTConstants cb = {};
    memcpy( cb.invViewProj, invViewProj, 16 * sizeof( float ) );
    cb.cameraPos[0] = cameraPos[0];
    cb.cameraPos[1] = cameraPos[1];
    cb.cameraPos[2] = cameraPos[2];
    cb.waterY = waterY;
    cb.lightPos[0] = lightPos[0];
    cb.lightPos[1] = lightPos[1];
    cb.lightPos[2] = lightPos[2];
    cb.time = time;
    cb.skyColorTop[0] = 0.4f;
    cb.skyColorTop[1] = 0.6f;
    cb.skyColorTop[2] = 0.9f;
    cb.skyColorBottom[0] = 0.7f;
    cb.skyColorBottom[1] = 0.8f;
    cb.skyColorBottom[2] = 0.95f;
    memcpy( m_rtConstantBufferMapped, &cb, sizeof( cb ) );

    // Set compute root signature and pipeline
    m_cmdList4->SetComputeRootSignature( m_rtRootSignature );
    m_cmdList4->SetPipelineState1( m_rtPSO );

    // Bind descriptor heap
    ID3D12DescriptorHeap* heaps[] = { m_srvHeap };
    m_cmdList4->SetDescriptorHeaps( 1, heaps );

    // Root params: [0] TLAS SRV, [1] UAV table, [2] CBV, [3] texture SRV table
    m_cmdList4->SetComputeRootShaderResourceView( 0, m_tlas.GetResultVA() );
    m_cmdList4->SetComputeRootDescriptorTable( 1, GetSRVGpuHandle( m_reflectionUAVIndex ) );
    m_cmdList4->SetComputeRootConstantBufferView( 2, m_rtConstantBuffer->GetGPUVirtualAddress() );

    // Bind sphere + terrain + 6 sky face textures at root param [3] (t0..t7)
    const uint32_t texHandles[8] = { sphereTexHandle, terrainTexHandle, skyUpHandle, skyDownHandle, skyRightHandle, skyLeftHandle, skyFrontHandle, skyBackHandle };
    bool allValid = true;
    for ( int i = 0; i < 8; ++i )
    {
        if ( texHandles[i] == 0 || texHandles[i] > (uint32_t)m_textures.size() )
        {
            allValid = false;
            break;
        }
    }
    if ( allValid )
    {
        UINT slot0 = AllocateTransientSRV();
        for ( int i = 1; i < 8; ++i )
        {
            AllocateTransientSRV(); // ensure 8 contiguous slots
        }

        D3D12_CPU_DESCRIPTOR_HANDLE dstBase = m_srvHeap->GetCPUDescriptorHandleForHeapStart();
        dstBase.ptr += (SIZE_T)slot0 * m_srvDescSize;

        for ( int i = 0; i < 8; ++i )
        {
            D3D12_CPU_DESCRIPTOR_HANDLE dst = dstBase;
            dst.ptr += (SIZE_T)i * m_srvDescSize;
            UINT srcIdx = m_textures[texHandles[i] - 1].srvIndex;
            m_device->CopyDescriptorsSimple( 1, dst, GetSRVStagingCpuHandle( srcIdx ), D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV );
        }

        m_cmdList4->SetComputeRootDescriptorTable( 3, GetSRVGpuHandle( slot0 ) );
    }

    // Dispatch rays
    D3D12_DISPATCH_RAYS_DESC dispatchDesc = {};
    dispatchDesc.RayGenerationShaderRecord = m_sbt.RayGenRange();
    dispatchDesc.MissShaderTable = m_sbt.MissRange();
    dispatchDesc.HitGroupTable = m_sbt.HitGroupRange();
    dispatchDesc.Width = (UINT)m_reflectionWidth;
    dispatchDesc.Height = (UINT)m_reflectionHeight;
    dispatchDesc.Depth = 1;

    m_cmdList4->DispatchRays( &dispatchDesc );

    // UAV barrier so the water shader can sample the result
    D3D12_RESOURCE_BARRIER barrier = {};
    barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_UAV;
    barrier.UAV.pResource = m_reflectionUAV;
    m_cmdList4->ResourceBarrier( 1, &barrier );

    // Transition to SRV state for water shader sampling
    TransitionBarrier( m_reflectionUAV, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE );
    m_reflectionInSRVState = true;

    // Force re-bind of raster state after compute dispatch
    m_lastPSOHash = 0;
    m_texBindingsDirty = true;
    m_targetsDirty = true;
}


uint32_t RenderBackendDX12::GetReflectionUAVTexture() const
{
    // Return a texture handle that maps to the reflection SRV
    // The water shader will bind this at t1 instead of the FBO texture
    // We return a handle into the texture registry — but for DXR we just return the SRV index
    // encoded as a texture handle. The caller can pass this to BindTexture.
    // Actually, we need to register the SRV in the texture registry.
    // This is called once, so we can cast-away const for registration.
    if ( m_reflectionSRVIndex == 0 )
    {
        return 0;
    }
    // Find if already registered
    for ( size_t i = 0; i < m_textures.size(); ++i )
    {
        if ( m_textures[i].srvIndex == m_reflectionSRVIndex )
        {
            return (uint32_t)( i + 1 );
        }
    }
    // Register it (const_cast justified: lazy one-time registration)
    auto* self = const_cast<RenderBackendDX12*>( this );
    return self->RegisterSRV( m_reflectionSRVIndex );
}


uint64_t RenderBackendDX12::GetInstancedMeshStaticVBVA( uint32_t handle ) const
{
    if ( handle == 0 || handle > (uint32_t)m_instancedMeshes.size() )
    {
        return 0;
    }
    const auto& im = m_instancedMeshes[handle - 1];
    return im.staticVB ? im.staticVB->GetGPUVirtualAddress() : 0;
}


int RenderBackendDX12::GetInstancedMeshStaticStride( uint32_t handle ) const
{
    if ( handle == 0 || handle > (uint32_t)m_instancedMeshes.size() )
    {
        return 0;
    }
    return m_instancedMeshes[handle - 1].staticStride;
}


void RenderBackendDX12::ShutdownDXR()
{
    m_sbt.Reset();
    m_tlas.Reset();
    m_terrainBLAS.Reset();
    m_sphereBLAS.Reset();

    if ( m_rtConstantBuffer )
    {
        m_rtConstantBuffer->Unmap( 0, nullptr );
        m_rtConstantBuffer->Release();
        m_rtConstantBuffer = nullptr;
        m_rtConstantBufferMapped = nullptr;
    }
    if ( m_reflectionUAV )
    {
        m_reflectionUAV->Release();
        m_reflectionUAV = nullptr;
    }
    if ( m_rtPSOProps )
    {
        m_rtPSOProps->Release();
        m_rtPSOProps = nullptr;
    }
    if ( m_rtPSO )
    {
        m_rtPSO->Release();
        m_rtPSO = nullptr;
    }
    if ( m_rtRootSignature )
    {
        m_rtRootSignature->Release();
        m_rtRootSignature = nullptr;
    }
    if ( m_cmdList4 )
    {
        m_cmdList4->Release();
        m_cmdList4 = nullptr;
    }
    if ( m_device5 )
    {
        m_device5->Release();
        m_device5 = nullptr;
    }
    m_dxrSupported = false;
}


// --- GPU Timers ---


void RenderBackendDX12::GpuTimerBegin( int markerIdx )
{
    if ( !m_timerQueryHeap || markerIdx < 0 || markerIdx >= TIMER_HEAP_MARKERS )
    {
        return;
    }
    EnsureCommandListOpen();
    m_commandList->EndQuery( m_timerQueryHeap, D3D12_QUERY_TYPE_TIMESTAMP, (UINT)( markerIdx * 2 + 0 ) );
}


void RenderBackendDX12::GpuTimerEnd( int markerIdx )
{
    if ( !m_timerQueryHeap || !m_commandListOpen || markerIdx < 0 || markerIdx >= TIMER_HEAP_MARKERS )
    {
        return;
    }
    m_commandList->EndQuery( m_timerQueryHeap, D3D12_QUERY_TYPE_TIMESTAMP, (UINT)( markerIdx * 2 + 1 ) );
}


void RenderBackendDX12::GpuTimerInvalidate()
{
    std::memset( m_timerResultMs, 0, sizeof( m_timerResultMs ) );
    std::memset( m_timerResultValid, 0, sizeof( m_timerResultValid ) );
    m_timerReadPending = false;
}


bool RenderBackendDX12::GpuTimerRead( int markerIdx, float& outMs )
{
    if ( markerIdx < 0 || markerIdx >= TIMER_HEAP_MARKERS || !m_timerResultValid[markerIdx] )
    {
        return false;
    }
    outMs = m_timerResultMs[markerIdx];
    return true;
}
