// --- Includes ---
// --- DX12 vs DX11 Architecture ---
//
// DX11 (high-level, driver manages everything):
//   App -> DeviceContext -> Driver -> GPU
//   (The driver batches, reorders, and optimizes commands automatically)
//
// DX12 (low-level, app manages everything):
//   App -> CommandList -> CommandQueue -> GPU
//   (YOU manage memory, synchronization, resource states, and command recording)
//
// DX12 Frame Lifecycle:
//   1. Wait for GPU to finish frame N-2 (via Fence)
//   2. Reset CommandAllocator (reuse memory from completed frame)
//   3. Reset CommandList (start recording new commands)
//   4. Record: barriers -> clear -> bind PSO -> set descriptors -> draw -> barriers
//   5. Close CommandList
//   6. ExecuteCommandLists (submit to GPU)
//   7. Present (flip swap chain)
//   8. Signal fence (mark this frame as "submitted")
//
// Key DX12 concepts used in this file:
//   Device            = Factory for creating GPU resources and pipeline objects
//   CommandList       = Records GPU commands (like a to-do list for the GPU)
//   CommandQueue      = Submits command lists to the GPU for execution
//   CommandAllocator  = Memory pool backing a command list's recordings
//   Fence             = CPU/GPU synchronization (like a semaphore)
//   SwapChain         = Double-buffered presentation (two back buffers alternating)
//   Descriptor Heaps  = Tables of "views" describing how the GPU sees resources
//   Root Signature    = Defines what data (textures, constants) shaders can access
//   PSO               = Pipeline State Object — entire GPU state compiled into one object
//   Resource Barriers = Explicitly transition resources between states
//   Upload Heap       = CPU-writable staging memory for sending data to the GPU
//   Default Heap      = Fast GPU-only memory (not CPU-accessible)
//
#include "SkullbonezRenderBackendDX12.h"
#include "SkullbonezShaderDX12.h"
#include "SkullbonezMeshDX12.h"
#include "SkullbonezFramebufferDX12.h"
#include <stdexcept>
#include <cstdio>
#include <cstring>
#include <algorithm>
#include <string>
#include <fstream>


// --- Usings ---
using namespace SkullbonezCore::Math::Transformation;
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
{
}


// --- Helpers ---


void RenderBackendDX12::WaitForGpu()
{
    if ( !m_commandQueue || !m_fence || !m_fenceEvent )
    {
        return;
    }

    // Tell the GPU to signal the fence with an incremented value once all prior work completes.
    // Docs: https://learn.microsoft.com/en-us/windows/win32/api/d3d12/nf-d3d12-id3d12commandqueue-signal
    m_commandQueue->Signal( m_fence, ++m_fenceValue );

    // If the GPU hasn't reached our fence value yet, wait. SetEventOnCompletion tells the fence
    // to fire a Windows event when the value is reached, then we block on it.
    // Docs: https://learn.microsoft.com/en-us/windows/win32/api/d3d12/nf-d3d12-id3d12fence-seteventoncompletion
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
    if ( !m_commandList || !m_commandQueue || !m_fence || !m_fenceEvent || !m_commandAllocators[m_allocatorIndex] )
    {
        throw std::runtime_error( "DX12 backend is not fully initialised (command list/fence unavailable)." );
    }

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

    // Reset the command allocator — frees all memory from previously recorded commands.
    // This is only safe because we waited for the GPU to finish with this allocator above.
    // Docs: https://learn.microsoft.com/en-us/windows/win32/api/d3d12/nf-d3d12-id3d12commandallocator-reset
    m_commandAllocators[m_allocatorIndex]->Reset();

    // Reset the command list to start recording new commands. The command list is reused
    // every frame — Reset puts it back into the "recording" state with a fresh allocator.
    // Docs: https://learn.microsoft.com/en-us/windows/win32/api/d3d12/nf-d3d12-id3d12graphicscommandlist-reset
    m_commandList->Reset( m_commandAllocators[m_allocatorIndex], nullptr );

    // Bind the shader-visible descriptor heap — required before any draw calls that reference
    // textures or CBVs. Only ONE CBV/SRV/UAV heap can be bound at a time in DX12.
    // Docs: https://learn.microsoft.com/en-us/windows/win32/api/d3d12/nf-d3d12-id3d12graphicscommandlist-setdescriptorheaps
    ID3D12DescriptorHeap* heaps[] = { m_srvHeap };
    m_commandList->SetDescriptorHeaps( 1, heaps );

    // Bind the root signature — tells the GPU the layout of shader parameters (where to find
    // constant buffers, texture descriptors, etc.). Must match what the PSO was created with.
    // Docs: https://learn.microsoft.com/en-us/windows/win32/api/d3d12/nf-d3d12-id3d12graphicscommandlist-setgraphicsrootsignature
    m_commandList->SetGraphicsRootSignature( m_rootSignature );
    m_commandListOpen = true;
    m_uploadOffset = 0;
    m_nextTransientSRV = MAX_STATIC_SRVS + ( m_allocatorIndex * MAX_TRANSIENT_SRVS );

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
    // Record a resource state transition barrier. In DX12, YOU must tell the GPU when a resource
    // changes from one usage to another (e.g. from render target to shader input). The GPU uses
    // this to flush caches and resolve memory hazards. Forgetting barriers causes corruption.
    // Docs: https://learn.microsoft.com/en-us/windows/win32/api/d3d12/nf-d3d12-id3d12graphicscommandlist-resourcebarrier
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
    m_nextTransientSRV = MAX_STATIC_SRVS + ( m_allocatorIndex * MAX_TRANSIENT_SRVS );
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
    return m_uploadBuffers[m_allocatorIndex]->GetGPUVirtualAddress() + aligned;
}


uint8_t* RenderBackendDX12::GetUploadPtr( D3D12_GPU_VIRTUAL_ADDRESS addr )
{
    UINT64 offset = addr - m_uploadBuffers[m_allocatorIndex]->GetGPUVirtualAddress();
    return m_uploadBufferMapped[m_allocatorIndex] + offset;
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
    UINT transientBase = MAX_STATIC_SRVS + ( m_allocatorIndex * MAX_TRANSIENT_SRVS );
    UINT transientLimit = transientBase + MAX_TRANSIENT_SRVS;
    if ( m_nextTransientSRV < transientBase || m_nextTransientSRV >= transientLimit )
    {
        throw std::runtime_error( "DX12 transient SRV heap exhausted for current frame allocator" );
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
    // Enable the DX12 debug layer for development builds. This makes the runtime validate every
    // API call and report errors/warnings — essential for catching bugs but has a performance cost.
    // Docs: https://learn.microsoft.com/en-us/windows/win32/api/d3d12sdklayers/nf-d3d12sdklayers-id3d12debug-enabledebuglayer
    {
        ID3D12Debug* debugController = nullptr;
        if ( SUCCEEDED( D3D12GetDebugInterface( IID_PPV_ARGS( &debugController ) ) ) )
        {
            debugController->EnableDebugLayer();
            debugController->Release();
            factoryFlags = DXGI_CREATE_FACTORY_DEBUG;
        }
    }
    // Create the DXGI Factory — this is the starting point for all DirectX graphics.
    // DXGI (DirectX Graphics Infrastructure) manages adapters (GPUs), monitors, and swap chains.
    // The factory is used to enumerate GPUs and create the swap chain for presenting frames.
    // Docs: https://learn.microsoft.com/en-us/windows/win32/api/dxgi1_3/nf-dxgi1_3-createdxgifactory2
    if ( FAILED( CreateDXGIFactory2( factoryFlags, IID_PPV_ARGS( &m_factory ) ) ) )
    {
        throw std::runtime_error( "CreateDXGIFactory2 failed" );
    }
    {
        IDXGIFactory5* factory5 = nullptr;
        BOOL allowTearing = FALSE;
        if ( SUCCEEDED( m_factory->QueryInterface( IID_PPV_ARGS( &factory5 ) ) ) )
        {
            if ( FAILED( factory5->CheckFeatureSupport( DXGI_FEATURE_PRESENT_ALLOW_TEARING,
                                                        &allowTearing,
                                                        sizeof( allowTearing ) ) ) )
            {
                allowTearing = FALSE;
            }
            factory5->Release();
        }
        m_allowTearing = allowTearing == TRUE;
    }

    // Create the DX12 Device — this is the primary interface for creating ALL GPU resources.
    // The device represents a virtual GPU adapter. Pass nullptr for the first param to use the
    // default adapter. D3D_FEATURE_LEVEL_11_0 means we need at least DX11-capable hardware.
    // Docs: https://learn.microsoft.com/en-us/windows/win32/api/d3d12/nf-d3d12-d3d12createdevice
    if ( FAILED( D3D12CreateDevice( nullptr, D3D_FEATURE_LEVEL_11_0, IID_PPV_ARGS( &m_device ) ) ) )
    {
        throw std::runtime_error( "D3D12CreateDevice failed" );
    }

    // Check DXR capability
    CheckDXRSupport();

    // Configure the debug info queue: break on corruption and errors, suppress INFO-level chatter.
    {
        ID3D12InfoQueue* infoQueue = nullptr;
        if ( SUCCEEDED( m_device->QueryInterface( IID_PPV_ARGS( &infoQueue ) ) ) )
        {
            // Break into the debugger immediately on CORRUPTION or ERROR — same policy as DX11.
            // WARNING messages are logged but do not break; they must be investigated manually.
            infoQueue->SetBreakOnSeverity( D3D12_MESSAGE_SEVERITY_CORRUPTION, TRUE );
            infoQueue->SetBreakOnSeverity( D3D12_MESSAGE_SEVERITY_ERROR, TRUE );

            // Suppress all INFO-level messages from the debug layer. INFO-level messages are
            // pure lifecycle chatter (create/destroy resource, heap, command list, fence etc.)
            // and provide no actionable diagnostic value during development. We only want to
            // see WARNING and ERROR severity messages in the debug output.
            D3D12_MESSAGE_SEVERITY denySeverities[] = { D3D12_MESSAGE_SEVERITY_INFO };
            D3D12_INFO_QUEUE_FILTER filter = {};
            filter.DenyList.NumSeverities = _countof( denySeverities );
            filter.DenyList.pSeverityList = denySeverities;
            infoQueue->PushStorageFilter( &filter );
            infoQueue->Release();
        }
    }

    // Create the Command Queue — this is the submission point for GPU work. Command lists are
    // recorded on the CPU, then submitted here for the GPU to execute. DIRECT type means it can
    // run graphics, compute, and copy commands (as opposed to COMPUTE-only or COPY-only queues).
    // Docs: https://learn.microsoft.com/en-us/windows/win32/api/d3d12/nf-d3d12-id3d12device-createcommandqueue
    D3D12_COMMAND_QUEUE_DESC queueDesc = {};
    queueDesc.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
    if ( FAILED( m_device->CreateCommandQueue( &queueDesc, IID_PPV_ARGS( &m_commandQueue ) ) ) )
    {
        throw std::runtime_error( "CreateCommandQueue failed" );
    }

    // Create the Swap Chain — manages the double-buffered back buffers that are presented to the
    // screen. FLIP_DISCARD means the OS can discard the previous frame's content after presenting
    // (most efficient mode). BufferCount=2 gives us two alternating back buffers.
    // Docs: https://learn.microsoft.com/en-us/windows/win32/api/dxgi1_2/nf-dxgi1_2-idxgifactory2-createswapchainforhwnd
    DXGI_SWAP_CHAIN_DESC1 scDesc = {};
    scDesc.BufferCount = FRAME_COUNT;
    scDesc.Width = (UINT)width;
    scDesc.Height = (UINT)height;
    scDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    scDesc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    scDesc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
    scDesc.SampleDesc.Count = 1;
    scDesc.Flags = m_allowTearing ? DXGI_SWAP_CHAIN_FLAG_ALLOW_TEARING : 0;

    IDXGISwapChain1* swapChain1 = nullptr;
    if ( FAILED( m_factory->CreateSwapChainForHwnd( m_commandQueue, hwnd, &scDesc, nullptr, nullptr, &swapChain1 ) ) )
    {
        throw std::runtime_error( "CreateSwapChainForHwnd failed" );
    }
    swapChain1->QueryInterface( IID_PPV_ARGS( &m_swapChain ) );
    swapChain1->Release();
    m_factory->MakeWindowAssociation( hwnd, DXGI_MWA_NO_ALT_ENTER );
    m_frameIndex = m_swapChain->GetCurrentBackBufferIndex();

    // Create Descriptor Heaps — these are arrays of "descriptors" (small structs that describe
    // how the GPU should interpret a resource). DX12 requires you to pre-allocate descriptor
    // storage. RTV heap holds Render Target View descriptors (one per swap chain buffer + FBOs).
    // Docs: https://learn.microsoft.com/en-us/windows/win32/api/d3d12/nf-d3d12-id3d12device-createdescriptorheap
    {
        D3D12_DESCRIPTOR_HEAP_DESC desc = {};
        desc.NumDescriptors = 16;
        desc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
        ThrowIfFailed( m_device->CreateDescriptorHeap( &desc, IID_PPV_ARGS( &m_rtvHeap ) ), "CreateDescriptorHeap (RTV) failed" );
        m_rtvDescSize = m_device->GetDescriptorHandleIncrementSize( D3D12_DESCRIPTOR_HEAP_TYPE_RTV );
    }
    // DSV heap holds Depth Stencil View descriptors (main depth buffer + FBO depth buffers).
    // Docs: https://learn.microsoft.com/en-us/windows/win32/api/d3d12/nf-d3d12-id3d12device-createdescriptorheap
    {
        D3D12_DESCRIPTOR_HEAP_DESC desc = {};
        desc.NumDescriptors = 4;
        desc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_DSV;
        ThrowIfFailed( m_device->CreateDescriptorHeap( &desc, IID_PPV_ARGS( &m_dsvHeap ) ), "CreateDescriptorHeap (DSV) failed" );
        m_dsvDescSize = m_device->GetDescriptorHandleIncrementSize( D3D12_DESCRIPTOR_HEAP_TYPE_DSV );
    }
    // SRV/CBV/UAV heap — SHADER_VISIBLE means the GPU can directly access these descriptors.
    // This single heap holds all texture views (SRVs) and constant buffer views (CBVs) that
    // shaders reference at draw time. Must be bound with SetDescriptorHeaps before drawing.
    // Transient descriptors are partitioned per in-flight frame allocator to avoid writing over
    // descriptor slots that are still referenced by queued command lists.
    // Docs: https://learn.microsoft.com/en-us/windows/win32/api/d3d12/nf-d3d12-id3d12device-createdescriptorheap
    {
        D3D12_DESCRIPTOR_HEAP_DESC desc = {};
        desc.NumDescriptors = MAX_STATIC_SRVS + ( MAX_TRANSIENT_SRVS * FRAME_COUNT );
        desc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
        desc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
        ThrowIfFailed( m_device->CreateDescriptorHeap( &desc, IID_PPV_ARGS( &m_srvHeap ) ), "CreateDescriptorHeap (SRV) failed" );
        m_srvDescSize = m_device->GetDescriptorHandleIncrementSize( D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV );
    }
    {
        // CPU-only staging heap — used as a persistent "source of truth" for descriptor copies.
        // We create SRVs here once (at texture load), then copy them to the shader-visible heap
        // each frame as needed. This avoids descriptor management issues with multi-frame flight.
        // Docs: https://learn.microsoft.com/en-us/windows/win32/api/d3d12/nf-d3d12-id3d12device-createdescriptorheap
        D3D12_DESCRIPTOR_HEAP_DESC desc = {};
        desc.NumDescriptors = MAX_STATIC_SRVS;
        desc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
        desc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE; // CPU-only, can be read for copies
        ThrowIfFailed( m_device->CreateDescriptorHeap( &desc, IID_PPV_ARGS( &m_srvStagingHeap ) ), "CreateDescriptorHeap (staging) failed" );
    }

    // Create Render Target Views for each swap chain buffer. This tells the GPU how to write
    // pixels to the swap chain back buffers during rendering.
    // Docs: https://learn.microsoft.com/en-us/windows/win32/api/d3d12/nf-d3d12-id3d12device-createrendertargetview
    for ( int i = 0; i < FRAME_COUNT; ++i )
    {
        ThrowIfFailed( m_swapChain->GetBuffer( (UINT)i, IID_PPV_ARGS( &m_renderTargets[i] ) ), "SwapChain GetBuffer failed" );
        m_device->CreateRenderTargetView( m_renderTargets[i], nullptr, GetRTVHandle( (UINT)i ) );
    }

    // Depth stencil
    CreateDepthStencil( width, height );

    // Create Command Allocators — one per frame in flight. A command allocator is the backing
    // memory pool for command list recordings. You can't reuse an allocator until the GPU has
    // finished executing the commands that were recorded into it (enforced via fence).
    // Docs: https://learn.microsoft.com/en-us/windows/win32/api/d3d12/nf-d3d12-id3d12device-createcommandallocator
    for ( int i = 0; i < FRAME_COUNT; ++i )
    {
        ThrowIfFailed( m_device->CreateCommandAllocator( D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS( &m_commandAllocators[i] ) ), "CreateCommandAllocator failed" );
    }

    // Create the Command List — this is the "recording device" for GPU commands. You record draw
    // calls, resource transitions, and other operations into it, then submit it to the queue.
    // Only one command list is needed because we close/reset it between frames.
    // Docs: https://learn.microsoft.com/en-us/windows/win32/api/d3d12/nf-d3d12-id3d12device-createcommandlist
    ThrowIfFailed( m_device->CreateCommandList( 0, D3D12_COMMAND_LIST_TYPE_DIRECT, m_commandAllocators[0], nullptr, IID_PPV_ARGS( &m_commandList ) ), "CreateCommandList failed" );
    m_commandList->Close();
    m_commandListOpen = false;
    m_allocatorIndex = 0;

    // Create a Fence — the CPU/GPU synchronization primitive. A fence is essentially a counter:
    // the GPU signals it after completing work, and the CPU can wait until a specific value is
    // reached. This is how we ensure we don't overwrite command allocator memory that the GPU is
    // still reading from a previous frame.
    // Docs: https://learn.microsoft.com/en-us/windows/win32/api/d3d12/nf-d3d12-id3d12device-createfence
    ThrowIfFailed( m_device->CreateFence( 0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS( &m_fence ) ), "CreateFence failed" );
    m_fenceEvent = CreateEvent( nullptr, FALSE, FALSE, nullptr );

    // Create per-frame upload buffers — one per FRAME_COUNT allocator. Each holds CPU-writable,
    // GPU-readable memory for per-frame constant buffers, dynamic vertex buffers, and texture
    // uploads. Partitioned per-allocator so that frame N+1's CPU recording cannot overwrite data
    // that frame N's GPU is still reading (the per-allocator fence wait in EnsureCommandListOpen
    // guarantees frame N is done before we reuse that allocator's upload buffer on frame N+2).
    // Docs: https://learn.microsoft.com/en-us/windows/win32/api/d3d12/nf-d3d12-id3d12device-createcommittedresource
    for ( int i = 0; i < FRAME_COUNT; ++i )
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
        ThrowIfFailed( m_device->CreateCommittedResource( &heapProps, D3D12_HEAP_FLAG_NONE, &desc, D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS( &m_uploadBuffers[i] ) ), "CreateCommittedResource (upload) failed" );
        m_uploadBuffers[i]->Map( 0, nullptr, (void**)&m_uploadBufferMapped[i] );
    }

    // Root signature
    CreateRootSignature();
    InitGenMipsPipeline();

    // GPU timestamp query heap — used for GPU-side performance profiling. The GPU writes
    // timestamps at specific points in the command stream, which we later read back to
    // calculate elapsed time for specific rendering passes (terrain, spheres, water, etc.).
    // Docs: https://learn.microsoft.com/en-us/windows/win32/api/d3d12/nf-d3d12-id3d12device-createqueryheap
    {
        D3D12_QUERY_HEAP_DESC qhDesc = {};
        qhDesc.Type = D3D12_QUERY_HEAP_TYPE_TIMESTAMP;
        qhDesc.Count = (UINT)TIMER_HEAP_SIZE;
        if ( SUCCEEDED( m_device->CreateQueryHeap( &qhDesc, IID_PPV_ARGS( &m_gpuTimers.queryHeap ) ) ) )
        {
            // Readback buffer — CPU-readable memory where GPU timer results are copied to.
            // The READBACK heap type means the CPU can read from it (but the GPU cannot render to it).
            // Docs: https://learn.microsoft.com/en-us/windows/win32/api/d3d12/nf-d3d12-id3d12device-createcommittedresource
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
            // Buffers on all heap types are effectively created in COMMON state in D3D12
            // regardless of the specified initial state. For READBACK buffers the runtime
            // accepts any state but always uses COMMON — be explicit to keep the debug layer
            // quiet. CPU Map/Unmap access is independent of the GPU-visible resource state.
            // Docs: https://learn.microsoft.com/en-us/windows/win32/direct3d12/using-resource-barriers-to-synchronize-resource-states-in-direct3d-12#implicit-state-transitions
            if ( FAILED( m_device->CreateCommittedResource( &hp, D3D12_HEAP_FLAG_NONE, &rd, D3D12_RESOURCE_STATE_COMMON, nullptr, IID_PPV_ARGS( &m_gpuTimers.readbackBuf ) ) ) )
            {
                m_gpuTimers.queryHeap->Release();
                m_gpuTimers.queryHeap = nullptr;
            }
            else
            {
                m_commandQueue->GetTimestampFrequency( &m_gpuTimers.freq );
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

    D3D12_DESCRIPTOR_RANGE1 srvRange2 = {};
    srvRange2.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
    srvRange2.NumDescriptors = 1;
    srvRange2.BaseShaderRegister = 2;
    srvRange2.RegisterSpace = 0;
    srvRange2.OffsetInDescriptorsFromTableStart = 0;

    D3D12_ROOT_PARAMETER1 params[4] = {};
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

    params[3].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    params[3].DescriptorTable.NumDescriptorRanges = 1;
    params[3].DescriptorTable.pDescriptorRanges = &srvRange2;
    params[3].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;

    D3D12_STATIC_SAMPLER_DESC samplers[2] = {};
    // s0: linear wrap (most textures — terrain, skybox, sphere)
    samplers[0].Filter = D3D12_FILTER_MIN_MAG_MIP_LINEAR;
    samplers[0].AddressU = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
    samplers[0].AddressV = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
    samplers[0].AddressW = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
    samplers[0].MaxAnisotropy = 1;
    samplers[0].ComparisonFunc = D3D12_COMPARISON_FUNC_NEVER;
    samplers[0].MaxLOD = D3D12_FLOAT32_MAX; // allow all mip levels (default 0 = mip 0 only!)
    samplers[0].ShaderRegister = 0;
    samplers[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;

    // s1: linear clamp (FBO / reflection textures)
    samplers[1].Filter = D3D12_FILTER_MIN_MAG_MIP_LINEAR;
    samplers[1].AddressU = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
    samplers[1].AddressV = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
    samplers[1].AddressW = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
    samplers[1].MaxAnisotropy = 1;
    samplers[1].ComparisonFunc = D3D12_COMPARISON_FUNC_NEVER;
    samplers[1].MaxLOD = D3D12_FLOAT32_MAX;
    samplers[1].ShaderRegister = 1;
    samplers[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;

    D3D12_VERSIONED_ROOT_SIGNATURE_DESC rootSigDesc = {};
    rootSigDesc.Version = D3D_ROOT_SIGNATURE_VERSION_1_1;
    rootSigDesc.Desc_1_1.NumParameters = 4;
    rootSigDesc.Desc_1_1.pParameters = params;
    rootSigDesc.Desc_1_1.NumStaticSamplers = 2;
    rootSigDesc.Desc_1_1.pStaticSamplers = samplers;
    rootSigDesc.Desc_1_1.Flags = D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;

    // Serialize the root signature description into a binary blob. The root signature defines
    // what data shaders can access: [0] CBV at b0 (constants), [1] SRV table at t0,
    // [2] SRV table at t1, [3] SRV table at t2, plus two static samplers.
    // Docs: https://learn.microsoft.com/en-us/windows/win32/api/d3d12/nf-d3d12-d3d12serializeversionedrootsignature
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

    // Create the Root Signature object from the serialized blob. This is the "contract" between
    // the application and shaders — it defines the layout of all shader-visible parameters.
    // Every PSO must reference a root signature, and every draw call must bind matching data.
    // Docs: https://learn.microsoft.com/en-us/windows/win32/api/d3d12/nf-d3d12-id3d12device-createrootsignature
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

    // Create the main depth/stencil buffer on the default (GPU-only) heap. This texture stores
    // per-pixel depth values (24-bit depth + 8-bit stencil) for the z-buffer algorithm.
    // Docs: https://learn.microsoft.com/en-us/windows/win32/api/d3d12/nf-d3d12-id3d12device-createcommittedresource
    m_device->CreateCommittedResource( &heapProps, D3D12_HEAP_FLAG_NONE, &desc, D3D12_RESOURCE_STATE_DEPTH_WRITE, &clearValue, IID_PPV_ARGS( &m_depthStencil ) );
    if ( !m_depthStencil )
    {
        throw std::runtime_error( "CreateCommittedResource (depth stencil) failed" );
    }

    D3D12_DEPTH_STENCIL_VIEW_DESC dsvDesc = {};
    dsvDesc.Format = DXGI_FORMAT_D24_UNORM_S8_UINT;
    dsvDesc.ViewDimension = D3D12_DSV_DIMENSION_TEXTURE2D;
    // Create a Depth Stencil View for the main depth buffer so it can be bound as the depth target.
    // Docs: https://learn.microsoft.com/en-us/windows/win32/api/d3d12/nf-d3d12-id3d12device-createdepthstencilview
    m_device->CreateDepthStencilView( m_depthStencil, &dsvDesc, GetDSVHandle( 0 ) );
}


void RenderBackendDX12::Shutdown()
{
    if ( !m_device )
    {
        return;
    }

    // Scene-driven screenshots can leave the swap-chain back buffer restored to
    // RENDER_TARGET state after readback. Shutdown does one final DXGI Present()
    // below to drain the flip queue, and DX12 requires that resource to be in
    // PRESENT state first.
    if ( !m_renderingToFBO && m_backBufferIsRT && m_swapChain && m_renderTargets[m_frameIndex] )
    {
        EnsureCommandListOpen();
        TransitionBarrier( m_renderTargets[m_frameIndex], D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_PRESENT );
        m_backBufferIsRT = false;
    }

    // Ensure any open command list is closed and submitted before waiting.
    if ( m_commandListOpen )
    {
        m_commandList->Close();
        m_commandListOpen = false;
        ID3D12CommandList* ppCLs[] = { m_commandList };
        m_commandQueue->ExecuteCommandLists( 1, ppCLs );
    }

    // Wait for all GPU work to complete (command queue + pending presents).
    WaitForGpu();

    // Drain the DXGI flip queue. DX12's WaitForGpu only waits on the command queue fence,
    // but DXGI's flip-model present queue is separate. Without draining it, DWM may still
    // hold references to this swap chain's backbuffers after Release(), preventing GDI
    // (OpenGL SwapBuffers) from reclaiming the window's composition surface.
    // Present an empty frame with sync-interval 0 to flush the flip queue, then wait again.
    if ( m_swapChain )
    {
        m_swapChain->Present( 0, 0 );
        WaitForGpu();
    }

    // DXR cleanup
    ShutdownDXR();

    // GPU timer cleanup
    if ( m_gpuTimers.readbackBuf )
    {
        m_gpuTimers.readbackBuf->Release();
        m_gpuTimers.readbackBuf = nullptr;
    }
    if ( m_gpuTimers.queryHeap )
    {
        m_gpuTimers.queryHeap->Release();
        m_gpuTimers.queryHeap = nullptr;
    }

    if ( m_genMipsPSO )
    {
        m_genMipsPSO->Release();
        m_genMipsPSO = nullptr;
    }
    if ( m_genMipsRS )
    {
        m_genMipsRS->Release();
        m_genMipsRS = nullptr;
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

    // Grid line overlay resources
    if ( m_gridLinePSO )
    {
        m_gridLinePSO->Release();
        m_gridLinePSO = nullptr;
    }
    m_gridLineShader.reset();

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

    for ( int i = 0; i < FRAME_COUNT; ++i )
    {
        if ( m_uploadBuffers[i] )
        {
            m_uploadBuffers[i]->Unmap( 0, nullptr );
            m_uploadBuffers[i]->Release();
            m_uploadBuffers[i] = nullptr;
        }
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
        m_swapChain->SetFullscreenState( FALSE, nullptr );
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

    // Opportunistically consume the previous frame's resolved timer buffer before writing
    // new query results into the same readback resource.
    TryConsumeGpuTimerReadback( false );

    // Resolve GPU timer queries — only resolve contiguous ranges of slots that actually
    // had EndQuery recorded this frame. Resolving unwritten slots triggers D3D12 error 1319.
    bool resolvedTimerSlotsThisFrame = false;
    if ( m_gpuTimers.queryHeap )
    {
        int i = 0;
        while ( i < TIMER_HEAP_SIZE )
        {
            // skip unwritten slots
            if ( !m_gpuTimers.slotWritten[i] )
            {
                ++i;
                continue;
            }
            // find end of this contiguous written run
            int start = i;
            while ( i < TIMER_HEAP_SIZE && m_gpuTimers.slotWritten[i] )
            {
                ++i;
            }
            UINT byteOffset = (UINT)( start * sizeof( uint64_t ) );
            m_commandList->ResolveQueryData( m_gpuTimers.queryHeap, D3D12_QUERY_TYPE_TIMESTAMP, (UINT)start, (UINT)( i - start ), m_gpuTimers.readbackBuf, byteOffset );
            resolvedTimerSlotsThisFrame = true;
        }
        std::memset( m_gpuTimers.slotWritten, 0, sizeof( m_gpuTimers.slotWritten ) ); // reset for next frame
    }

    TransitionBarrier( m_renderTargets[m_frameIndex], D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_PRESENT );
    m_backBufferIsRT = false;

    // Close the command list — finalizes the recorded commands. A closed command list can be
    // submitted to the GPU. No more commands can be recorded until Reset is called.
    // Docs: https://learn.microsoft.com/en-us/windows/win32/api/d3d12/nf-d3d12-id3d12graphicscommandlist-close
    m_commandList->Close();
    m_commandListOpen = false;

    // Submit the completed command list to the GPU for execution. The GPU processes commands
    // asynchronously — this call returns immediately while the GPU works in the background.
    // Docs: https://learn.microsoft.com/en-us/windows/win32/api/d3d12/nf-d3d12-id3d12commandqueue-executecommandlists
    ID3D12CommandList* ppCLs[] = { m_commandList };
    m_commandQueue->ExecuteCommandLists( 1, ppCLs );

    // Present the frame — flips the swap chain to show the just-rendered back buffer on screen.
    // Sync interval is configurable so perf scenes can disable V-Sync while visual scenes keep it.
    // Docs: https://learn.microsoft.com/en-us/windows/win32/api/dxgi/nf-dxgi-idxgiswapchain-present
    const UINT syncInterval = m_isVsyncEnabled ? 1u : 0u;
    const UINT presentFlags = ( !m_isVsyncEnabled && m_allowTearing )
                                  ? DXGI_PRESENT_ALLOW_TEARING
                                  : 0u;
    m_swapChain->Present( syncInterval, presentFlags );

    // Signal the fence with the current frame's value. When the GPU reaches this point in its
    // command stream, it will update the fence to this value — letting the CPU know this frame's
    // allocator memory is safe to reuse (after we check GetCompletedValue >= this value).
    // Docs: https://learn.microsoft.com/en-us/windows/win32/api/d3d12/nf-d3d12-id3d12commandqueue-signal
    m_frameFenceValues[m_allocatorIndex] = ++m_fenceValue;
    m_commandQueue->Signal( m_fence, m_fenceValue );

    // Timer readback can be mapped once this frame's signal fence is reached.
    // If there's an unconsumed readback still pending (e.g. fence wasn't ready during the
    // non-blocking TryConsume at the top of Present), do a blocking consume now to avoid
    // permanently losing that frame's GPU timing data by overwriting readFenceValue.
    if ( resolvedTimerSlotsThisFrame )
    {
        if ( m_gpuTimers.readPending )
        {
            // In free-running off-vsync mode the CPU can lap the GPU. Dropping one
            // stale timer sample is better than blocking Present() and throttling
            // the whole frame loop; the next fence will publish fresh data.
            m_gpuTimers.readPending = false;
        }
        m_gpuTimers.readPending = true;
        m_gpuTimers.readFenceValue = m_fenceValue;
    }

    // Advance to next frame's allocator and swap chain buffer
    m_allocatorIndex = ( m_allocatorIndex + 1 ) % FRAME_COUNT;
    m_frameIndex = m_swapChain->GetCurrentBackBufferIndex();
    m_currentRTV = GetRTVHandle( m_frameIndex );
}


void RenderBackendDX12::SetVsyncEnabled( bool enabled )
{
    m_isVsyncEnabled = enabled;
}


bool RenderBackendDX12::IsVsyncEnabled() const
{
    return m_isVsyncEnabled;
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
    TryConsumeGpuTimerReadback( true );
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

    const UINT resizeFlags = m_allowTearing ? DXGI_SWAP_CHAIN_FLAG_ALLOW_TEARING : 0u;
    m_swapChain->ResizeBuffers( FRAME_COUNT, (UINT)width, (UINT)height, DXGI_FORMAT_R8G8B8A8_UNORM, resizeFlags );
    m_frameIndex = m_swapChain->GetCurrentBackBufferIndex();

    // ResizeBuffers puts all back buffers into PRESENT state — reset our tracking flag
    // so the next Clear() correctly transitions to RENDER_TARGET before use.
    m_backBufferIsRT = false;

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
    // Bind the render target and depth buffer to the Output Merger (OM) stage — this tells the
    // GPU where to write pixel colors and depth values for subsequent draw calls.
    // Docs: https://learn.microsoft.com/en-us/windows/win32/api/d3d12/nf-d3d12-id3d12graphicscommandlist-omsetrendertargets
    m_commandList->OMSetRenderTargets( 1, &m_currentRTV, FALSE, &m_currentDSV );

    // Set the viewport (the rectangle on screen where rendering appears) and scissor rect
    // (pixels outside the scissor are clipped/discarded). Both must be set every time in DX12.
    // Docs: https://learn.microsoft.com/en-us/windows/win32/api/d3d12/nf-d3d12-id3d12graphicscommandlist-rssetviewports
    // Docs: https://learn.microsoft.com/en-us/windows/win32/api/d3d12/nf-d3d12-id3d12graphicscommandlist-rssetscissorrects
    m_commandList->RSSetViewports( 1, &m_viewport );
    m_commandList->RSSetScissorRects( 1, &m_scissorRect );

    if ( color )
    {
        // Clear the render target to a solid color (wipes the entire back buffer).
        // Docs: https://learn.microsoft.com/en-us/windows/win32/api/d3d12/nf-d3d12-id3d12graphicscommandlist-clearrendertargetview
        m_commandList->ClearRenderTargetView( m_currentRTV, m_clearColor, 0, nullptr );
    }
    if ( depth )
    {
        // Clear the depth buffer to 1.0 (maximum distance), so all subsequent draws will pass
        // the depth test. This is done at the start of each frame or when switching render targets.
        // Docs: https://learn.microsoft.com/en-us/windows/win32/api/d3d12/nf-d3d12-id3d12graphicscommandlist-cleardepthstencilview
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


void RenderBackendDX12::SetDepthWrite( bool enable )
{
    if ( m_depthWriteEnabled != enable )
    {
        m_depthWriteEnabled = enable;
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
    hashCombine( h, (size_t)key.depthWriteEnabled );
    hashCombine( h, (size_t)key.cullEnabled );
    hashCombine( h, (size_t)key.polyOffsetEnabled );
    hashCombine( h, (size_t)key.rtvFormat );
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
    psoDesc.DepthStencilState.DepthWriteMask = m_depthWriteEnabled ? D3D12_DEPTH_WRITE_MASK_ALL : D3D12_DEPTH_WRITE_MASK_ZERO;
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
    psoDesc.RTVFormats[0] = m_currentRTVFormat;
    psoDesc.DSVFormat = DXGI_FORMAT_D24_UNORM_S8_UINT;
    psoDesc.SampleDesc.Count = 1;

    // Create a Graphics Pipeline State Object (PSO). In DX12, ALL render state is compiled into
    // one monolithic object: shaders, input layout, rasterizer settings, blend mode, depth test,
    // etc. This is very different from DX11 where you set each state individually. The PSO is
    // expensive to create but fast to bind — so we cache them by hash and reuse across frames.
    // Docs: https://learn.microsoft.com/en-us/windows/win32/api/d3d12/nf-d3d12-id3d12device-creategraphicspipelinestate
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

    if ( !m_renderingToFBO && !m_backBufferIsRT )
    {
        TransitionBarrier( m_renderTargets[m_frameIndex], D3D12_RESOURCE_STATE_PRESENT, D3D12_RESOURCE_STATE_RENDER_TARGET );
        m_backBufferIsRT = true;
        m_targetsDirty = true;
    }

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
    key.depthWriteEnabled = m_depthWriteEnabled;
    key.cullEnabled = m_cullEnabled;
    key.polyOffsetEnabled = m_polyOffsetEnabled;
    key.rtvFormat = m_currentRTVFormat;

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

        // Bind the PSO — sets ALL GPU pipeline state (shaders, blend, depth, rasterizer) in one call.
        // Unlike DX11 where you set states individually, DX12 switches the entire pipeline at once.
        // Docs: https://learn.microsoft.com/en-us/windows/win32/api/d3d12/nf-d3d12-id3d12graphicscommandlist-setpipelinestate
        m_commandList->SetPipelineState( pso );

        // Re-bind root signature after PSO change (required by DX12 spec).
        // Docs: https://learn.microsoft.com/en-us/windows/win32/api/d3d12/nf-d3d12-id3d12graphicscommandlist-setgraphicsrootsignature
        m_commandList->SetGraphicsRootSignature( m_rootSignature );
        m_lastPSOHash = psoHash;
    }

    // Flush constant buffer data and bind it at root parameter [0] — this is where per-draw
    // shader uniforms (MVP matrix, colors, time, etc.) are uploaded to the GPU each draw call.
    // Docs: https://learn.microsoft.com/en-us/windows/win32/api/d3d12/nf-d3d12-id3d12graphicscommandlist-setgraphicsrootconstantbufferview
    if ( m_activeShader )
    {
        D3D12_GPU_VIRTUAL_ADDRESS cbAddr = m_activeShader->FlushCB();
        if ( cbAddr )
        {
            m_commandList->SetGraphicsRootConstantBufferView( 0, cbAddr );
        }
    }

    // Bind textures by copying their SRV descriptors to the shader-visible heap and pointing
    // the root descriptor table at them. Root params [1..3] map to texture slots t0..t2.
    // Docs: https://learn.microsoft.com/en-us/windows/win32/api/d3d12/nf-d3d12-id3d12graphicscommandlist-setgraphicsrootdescriptortable
    if ( m_texBindingsDirty )
    {
        for ( int slot = 0; slot < 3; ++slot )
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

    // Set the primitive topology — tells the Input Assembler how to interpret vertex data.
    // TRIANGLELIST means every 3 vertices form an independent triangle.
    // Docs: https://learn.microsoft.com/en-us/windows/win32/api/d3d12/nf-d3d12-id3d12graphicscommandlist-iasetprimitivetopology
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


void RenderBackendDX12::SetRenderingToFBO( bool rendering, UINT fboSrvIndex, UINT fboDepthSrvIndex, DXGI_FORMAT rtvFormat )
{
    m_renderingToFBO = rendering;
    m_currentRTVFormat = rendering ? rtvFormat : DXGI_FORMAT_R8G8B8A8_UNORM;
    m_psoDirty = true;
    if ( rendering )
    {
        // Clear any texture slot still referencing the FBO color texture
        // (it's now in RENDER_TARGET state and cannot be used as SRV)
        for ( int i = 0; i < 3; ++i )
        {
            if ( m_boundTexSlot[i] == fboSrvIndex || m_boundTexSlot[i] == fboDepthSrvIndex )
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


std::unique_ptr<IShader> RenderBackendDX12::CreateShader( const char* baseName )
{
    std::string hlslPath = std::string( DATA_ROOT ) + baseName + ".hlsl";
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


std::unique_ptr<IFramebuffer> RenderBackendDX12::CreateFramebuffer( int width, int height, FramebufferColorFormat colorFormat )
{
    auto fbo = std::make_unique<FramebufferDX12>( colorFormat );
    fbo->Create( width, height );
    return fbo;
}


// --- Textures ---


// =============================================================================
// InitGenMipsPipeline — compile generate_mips.hlsl, create root signature and
// compute PSO for GPU-side mipmap generation via compute shader.
//
// Root signature layout:
//   Param 0: 4 root constants at b0 (NumMipLevels, SrcDimension, TexelSizeX, TexelSizeY)
//   Param 1: Descriptor table — 1 SRV  (t0): source mip (single-level view)
//   Param 2: Descriptor table — 4 UAVs (u0-u3): output mips (unused slots use null UAV)
//   Static sampler s0: LinearClamp
// =============================================================================
void RenderBackendDX12::InitGenMipsPipeline()
{
    // -------------------------------------------------------------------------
    // Compile the compute shader from HLSL source
    // -------------------------------------------------------------------------
    std::string csPath = std::string( DATA_ROOT ) + "shaders/generate_mips.hlsl";
    std::ifstream csFile( csPath, std::ios::binary );
    if ( !csFile.is_open() )
    {
        throw std::runtime_error( "Cannot open generate_mips.hlsl: " + csPath );
    }
    std::string csSource( ( std::istreambuf_iterator<char>( csFile ) ),
                          std::istreambuf_iterator<char>() );

    UINT compileFlags = D3DCOMPILE_ENABLE_STRICTNESS;
#ifdef _DEBUG
    compileFlags |= D3DCOMPILE_DEBUG | D3DCOMPILE_SKIP_OPTIMIZATION;
#else
    compileFlags |= D3DCOMPILE_OPTIMIZATION_LEVEL3;
#endif

    ID3DBlob* csBlob = nullptr;
    ID3DBlob* errors = nullptr;
    HRESULT hr = D3DCompile( csSource.c_str(), csSource.size(), csPath.c_str(), nullptr, D3D_COMPILE_STANDARD_FILE_INCLUDE, "main_cs", "cs_5_0", compileFlags, 0, &csBlob, &errors );
    if ( FAILED( hr ) )
    {
        std::string msg = "generate_mips.hlsl CS compile failed: ";
        if ( errors )
        {
            msg += reinterpret_cast<const char*>( errors->GetBufferPointer() );
            errors->Release();
        }
        throw std::runtime_error( msg );
    }
    if ( errors )
    {
        errors->Release();
        errors = nullptr;
    }

    // -------------------------------------------------------------------------
    // Root signature
    // -------------------------------------------------------------------------
    // Param 0: 4 root constants (b0)
    D3D12_ROOT_PARAMETER1 params[3] = {};
    params[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
    params[0].Constants.ShaderRegister = 0;
    params[0].Constants.RegisterSpace = 0;
    params[0].Constants.Num32BitValues = 4;
    params[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;

    // Param 1: SRV descriptor table (t0)
    D3D12_DESCRIPTOR_RANGE1 srvRange = {};
    srvRange.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
    srvRange.NumDescriptors = 1;
    srvRange.BaseShaderRegister = 0;
    srvRange.RegisterSpace = 0;
    srvRange.OffsetInDescriptorsFromTableStart = 0;
    params[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    params[1].DescriptorTable.NumDescriptorRanges = 1;
    params[1].DescriptorTable.pDescriptorRanges = &srvRange;
    params[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;

    // Param 2: UAV descriptor table (u0-u3, 4 consecutive slots)
    D3D12_DESCRIPTOR_RANGE1 uavRange = {};
    uavRange.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_UAV;
    uavRange.NumDescriptors = 4;
    uavRange.BaseShaderRegister = 0;
    uavRange.RegisterSpace = 0;
    uavRange.OffsetInDescriptorsFromTableStart = 0;
    params[2].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    params[2].DescriptorTable.NumDescriptorRanges = 1;
    params[2].DescriptorTable.pDescriptorRanges = &uavRange;
    params[2].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;

    // Static LinearClamp sampler at s0
    D3D12_STATIC_SAMPLER_DESC sampler = {};
    sampler.Filter = D3D12_FILTER_MIN_MAG_MIP_LINEAR;
    sampler.AddressU = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
    sampler.AddressV = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
    sampler.AddressW = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
    sampler.MipLODBias = 0.0f;
    sampler.MaxAnisotropy = 1;
    sampler.ComparisonFunc = D3D12_COMPARISON_FUNC_NEVER;
    sampler.BorderColor = D3D12_STATIC_BORDER_COLOR_TRANSPARENT_BLACK;
    sampler.MinLOD = 0.0f;
    sampler.MaxLOD = D3D12_FLOAT32_MAX;
    sampler.ShaderRegister = 0;
    sampler.RegisterSpace = 0;
    sampler.ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;

    D3D12_VERSIONED_ROOT_SIGNATURE_DESC rsDesc = {};
    rsDesc.Version = D3D_ROOT_SIGNATURE_VERSION_1_1;
    rsDesc.Desc_1_1.NumParameters = 3;
    rsDesc.Desc_1_1.pParameters = params;
    rsDesc.Desc_1_1.NumStaticSamplers = 1;
    rsDesc.Desc_1_1.pStaticSamplers = &sampler;
    rsDesc.Desc_1_1.Flags = D3D12_ROOT_SIGNATURE_FLAG_NONE;

    ID3DBlob* rsBlob = nullptr;
    ThrowIfFailed( D3D12SerializeVersionedRootSignature( &rsDesc, &rsBlob, &errors ),
                   "GenerateMips root signature serialization failed" );
    if ( errors )
    {
        errors->Release();
        errors = nullptr;
    }

    ThrowIfFailed( m_device->CreateRootSignature( 0, rsBlob->GetBufferPointer(), rsBlob->GetBufferSize(), IID_PPV_ARGS( &m_genMipsRS ) ),
                   "CreateRootSignature (genMips) failed" );
    rsBlob->Release();

    // -------------------------------------------------------------------------
    // Compute PSO
    // -------------------------------------------------------------------------
    D3D12_COMPUTE_PIPELINE_STATE_DESC psoDesc = {};
    psoDesc.pRootSignature = m_genMipsRS;
    psoDesc.CS.pShaderBytecode = csBlob->GetBufferPointer();
    psoDesc.CS.BytecodeLength = csBlob->GetBufferSize();
    ThrowIfFailed( m_device->CreateComputePipelineState( &psoDesc, IID_PPV_ARGS( &m_genMipsPSO ) ),
                   "CreateComputePipelineState (genMips) failed" );
    csBlob->Release();

    // -------------------------------------------------------------------------
    // Null UAV descriptor — used to pad unused UAV table slots so the
    // debug layer doesn't complain about unbound descriptors.
    // -------------------------------------------------------------------------
    m_genMipsNullUAV = AllocateStaticSRV();

    D3D12_UNORDERED_ACCESS_VIEW_DESC nullUAVDesc = {};
    nullUAVDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    nullUAVDesc.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2D;
    nullUAVDesc.Texture2D.MipSlice = 0;

    // Create null UAV (null resource = "nothing bound") in staging heap
    m_device->CreateUnorderedAccessView( nullptr, nullptr, &nullUAVDesc, GetSRVStagingCpuHandle( m_genMipsNullUAV ) );

    // Copy to shader-visible heap so it can be referenced by descriptor tables
    D3D12_CPU_DESCRIPTOR_HANDLE svDst = m_srvHeap->GetCPUDescriptorHandleForHeapStart();
    svDst.ptr += (SIZE_T)m_genMipsNullUAV * m_srvDescSize;
    m_device->CopyDescriptorsSimple( 1, svDst, GetSRVStagingCpuHandle( m_genMipsNullUAV ), D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV );
}


// =============================================================================
// GenerateMipsGPU — GPU compute shader mip generation.
//
// The texture must have been created with D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS
// and have all subresources in D3D12_RESOURCE_STATE_COPY_DEST (only mip 0 was
// uploaded). This function:
//   1. Transitions mip 0 to NON_PIXEL_SHADER_RESOURCE.
//   2. For each batch of up to 4 mip levels:
//        a. Transitions destination mips to UNORDERED_ACCESS.
//        b. Binds a single-level SRV (source mip) and 4 UAVs (output mips).
//        c. Dispatches the compute shader.
//        d. UAV barrier, then transitions outputs to NON_PIXEL_SHADER_RESOURCE.
//   3. Transitions all mips to PIXEL_SHADER_RESOURCE (D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES).
// =============================================================================
void RenderBackendDX12::GenerateMipsGPU( ID3D12Resource* tex, DXGI_FORMAT fmt, UINT w, UINT h, UINT numMips )
{
    if ( numMips <= 1 )
    {
        return;
    }

    // Transition mip 0 from COPY_DEST to NON_PIXEL_SHADER_RESOURCE so the compute
    // shader can sample it. (Subsequent source mips are transitioned at end of each batch.)
    {
        D3D12_RESOURCE_BARRIER b = {};
        b.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        b.Transition.pResource = tex;
        b.Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_DEST;
        b.Transition.StateAfter = D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
        b.Transition.Subresource = 0;
        m_commandList->ResourceBarrier( 1, &b );
    }

    UINT srcMip = 0;
    UINT srcMipW = w;
    UINT srcMipH = h;

    while ( srcMip < numMips - 1 )
    {
        UINT mipsToGenerate = (std::min)( numMips - 1 - srcMip, 4u );
        UINT dstW = (std::max)( srcMipW >> 1, 1u );
        UINT dstH = (std::max)( srcMipH >> 1, 1u );

        // ------------------------------------------------------------------
        // Source SRV: single-level view of the source mip.
        // Create directly in the shader-visible heap (transient slot).
        // ------------------------------------------------------------------
        UINT srcSrvIdx = AllocateTransientSRV();
        {
            D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
            srvDesc.Format = fmt;
            srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
            srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
            srvDesc.Texture2D.MostDetailedMip = srcMip;
            srvDesc.Texture2D.MipLevels = 1;

            D3D12_CPU_DESCRIPTOR_HANDLE cpuHandle = m_srvHeap->GetCPUDescriptorHandleForHeapStart();
            cpuHandle.ptr += (SIZE_T)srcSrvIdx * m_srvDescSize;
            m_device->CreateShaderResourceView( tex, &srvDesc, cpuHandle );
        }

        // ------------------------------------------------------------------
        // UAV slots: 4 consecutive transient slots (u0=base, u1=+1, u2=+2, u3=+3).
        // Valid mips get real UAVs; unused slots get the null UAV.
        // ------------------------------------------------------------------
        UINT uavBase = AllocateTransientSRV(); // u0
        AllocateTransientSRV();                // u1
        AllocateTransientSRV();                // u2
        AllocateTransientSRV();                // u3

        for ( UINT i = 0; i < 4; ++i )
        {
            UINT uavIdx = uavBase + i;
            D3D12_CPU_DESCRIPTOR_HANDLE cpuHandle = m_srvHeap->GetCPUDescriptorHandleForHeapStart();
            cpuHandle.ptr += (SIZE_T)uavIdx * m_srvDescSize;

            if ( i < mipsToGenerate )
            {
                D3D12_UNORDERED_ACCESS_VIEW_DESC uavDesc = {};
                uavDesc.Format = fmt;
                uavDesc.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2D;
                uavDesc.Texture2D.MipSlice = srcMip + 1 + i;
                m_device->CreateUnorderedAccessView( tex, nullptr, &uavDesc, cpuHandle );
            }
            else
            {
                // Pad with null UAV to keep the debug layer happy
                D3D12_CPU_DESCRIPTOR_HANDLE nullSrc = GetSRVStagingCpuHandle( m_genMipsNullUAV );
                m_device->CopyDescriptorsSimple( 1, cpuHandle, nullSrc, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV );
            }
        }

        // ------------------------------------------------------------------
        // Transition destination mips COPY_DEST → UNORDERED_ACCESS
        // ------------------------------------------------------------------
        for ( UINT i = 0; i < mipsToGenerate; ++i )
        {
            D3D12_RESOURCE_BARRIER b = {};
            b.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
            b.Transition.pResource = tex;
            b.Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_DEST;
            b.Transition.StateAfter = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
            b.Transition.Subresource = srcMip + 1 + i;
            m_commandList->ResourceBarrier( 1, &b );
        }

        // ------------------------------------------------------------------
        // Dispatch the compute shader
        // ------------------------------------------------------------------
        struct GenMipsCB
        {
            UINT NumMipLevels;
            UINT SrcDimension;
            float TexelSizeX;
            float TexelSizeY;
        };
        GenMipsCB cb;
        cb.NumMipLevels = mipsToGenerate;
        cb.SrcDimension = ( ( srcMipW & 1 ) ? 1u : 0u ) | ( ( srcMipH & 1 ) ? 2u : 0u );
        cb.TexelSizeX = 1.0f / static_cast<float>( dstW );
        cb.TexelSizeY = 1.0f / static_cast<float>( dstH );

        m_commandList->SetComputeRootSignature( m_genMipsRS );
        m_commandList->SetPipelineState( m_genMipsPSO );
        m_commandList->SetComputeRoot32BitConstants( 0, 4, &cb, 0 );
        m_commandList->SetComputeRootDescriptorTable( 1, GetSRVGpuHandle( srcSrvIdx ) );
        m_commandList->SetComputeRootDescriptorTable( 2, GetSRVGpuHandle( uavBase ) );

        UINT groupsX = ( dstW + 7 ) / 8;
        UINT groupsY = ( dstH + 7 ) / 8;
        m_commandList->Dispatch( groupsX, groupsY, 1 );

        // UAV barrier: ensures writes complete before next SRV read or UAV write
        {
            D3D12_RESOURCE_BARRIER uavBarrier = {};
            uavBarrier.Type = D3D12_RESOURCE_BARRIER_TYPE_UAV;
            uavBarrier.UAV.pResource = tex;
            m_commandList->ResourceBarrier( 1, &uavBarrier );
        }

        // ------------------------------------------------------------------
        // Transition output mips UNORDERED_ACCESS → NON_PIXEL_SHADER_RESOURCE
        // so the next batch can read them as a source SRV.
        // ------------------------------------------------------------------
        for ( UINT i = 0; i < mipsToGenerate; ++i )
        {
            D3D12_RESOURCE_BARRIER b = {};
            b.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
            b.Transition.pResource = tex;
            b.Transition.StateBefore = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
            b.Transition.StateAfter = D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
            b.Transition.Subresource = srcMip + 1 + i;
            m_commandList->ResourceBarrier( 1, &b );
        }

        srcMip += mipsToGenerate;
        srcMipW = (std::max)( srcMipW >> mipsToGenerate, 1u );
        srcMipH = (std::max)( srcMipH >> mipsToGenerate, 1u );
    }

    // All mips are now in NON_PIXEL_SHADER_RESOURCE. Transition ALL_SUBRESOURCES
    // to PIXEL_SHADER_RESOURCE for use in pixel shaders.
    TransitionBarrier( tex, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE );

    // Force full rebind of graphics state on the next draw call, since we
    // switched root signatures and PSO for the compute dispatch.
    m_lastPSOHash = 0;
    m_texBindingsDirty = true;
    m_targetsDirty = true;
}


uint32_t RenderBackendDX12::CreateTexture2D( const uint8_t* data, int w, int h, int channels, bool generateMips, bool /*linearFilter*/ )
{
    EnsureCommandListOpen();

    // Resolve format and bytes-per-pixel
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

    // Convert RGB → RGBA if needed; srcData always has bytesPerPixel channels after this.
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

    // Compute full mip count (log2 of the larger dimension + 1)
    UINT numMips = 1;
    if ( generateMips && m_genMipsPSO )
    {
        UINT mw = static_cast<UINT>( w );
        UINT mh = static_cast<UINT>( h );
        while ( mw > 1 || mh > 1 )
        {
            mw = (std::max)( mw >> 1, 1u );
            mh = (std::max)( mh >> 1, 1u );
            ++numMips;
        }
    }

    // Create the texture resource on the Default Heap.
    // ALLOW_UNORDERED_ACCESS is required when generating mips via compute shader.
    // Docs: https://learn.microsoft.com/en-us/windows/win32/api/d3d12/nf-d3d12-id3d12device-createcommittedresource
    D3D12_HEAP_PROPERTIES defaultHeap = {};
    defaultHeap.Type = D3D12_HEAP_TYPE_DEFAULT;

    D3D12_RESOURCE_DESC texDesc = {};
    texDesc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    texDesc.Width = static_cast<UINT64>( w );
    texDesc.Height = static_cast<UINT>( h );
    texDesc.DepthOrArraySize = 1;
    texDesc.MipLevels = static_cast<UINT16>( numMips );
    texDesc.Format = fmt;
    texDesc.SampleDesc.Count = 1;
    texDesc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
    texDesc.Flags = ( numMips > 1 )
                        ? D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS
                        : D3D12_RESOURCE_FLAG_NONE;

    ID3D12Resource* texResource = nullptr;
    ThrowIfFailed( m_device->CreateCommittedResource(
                       &defaultHeap,
                       D3D12_HEAP_FLAG_NONE,
                       &texDesc,
                       D3D12_RESOURCE_STATE_COPY_DEST,
                       nullptr,
                       IID_PPV_ARGS( &texResource ) ),
                   "CreateCommittedResource (texture) failed" );

    // -------------------------------------------------------------------------
    // Upload mip 0 only. GetCopyableFootprints for 1 subresource gives the
    // GPU row-pitch-aligned layout we must write to in the upload buffer.
    // Docs: https://learn.microsoft.com/en-us/windows/win32/api/d3d12/nf-d3d12-id3d12device-getcopyablefootprints
    // -------------------------------------------------------------------------
    D3D12_PLACED_SUBRESOURCE_FOOTPRINT fp0 = {};
    UINT rowCount0;
    UINT64 rowSize0, mip0Bytes;
    m_device->GetCopyableFootprints( &texDesc, 0, 1, 0, &fp0, &rowCount0, &rowSize0, &mip0Bytes );

    FlushUploadBufferIfNeeded( mip0Bytes, D3D12_TEXTURE_DATA_PLACEMENT_ALIGNMENT );
    D3D12_GPU_VIRTUAL_ADDRESS uploadBase = SubAllocateUpload( mip0Bytes, D3D12_TEXTURE_DATA_PLACEMENT_ALIGNMENT );
    const UINT64 baseOffset = uploadBase - m_uploadBuffers[m_allocatorIndex]->GetGPUVirtualAddress();
    uint8_t* uploadDst = GetUploadPtr( uploadBase );

    const UINT srcRowPitch = static_cast<UINT>( w ) * static_cast<UINT>( bytesPerPixel );
    for ( UINT row = 0; row < rowCount0; ++row )
    {
        memcpy( uploadDst + row * fp0.Footprint.RowPitch,
                srcData + row * srcRowPitch,
                srcRowPitch );
    }

    D3D12_TEXTURE_COPY_LOCATION dstLoc = {};
    dstLoc.pResource = texResource;
    dstLoc.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
    dstLoc.SubresourceIndex = 0;

    D3D12_TEXTURE_COPY_LOCATION srcLoc = {};
    srcLoc.pResource = m_uploadBuffers[m_allocatorIndex];
    srcLoc.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
    srcLoc.PlacedFootprint = fp0;
    srcLoc.PlacedFootprint.Offset = baseOffset + fp0.Offset;

    m_commandList->CopyTextureRegion( &dstLoc, 0, 0, 0, &srcLoc, nullptr );

    // -------------------------------------------------------------------------
    // Generate remaining mips on the GPU (compute shader), or transition
    // directly to PIXEL_SHADER_RESOURCE for single-mip textures.
    // -------------------------------------------------------------------------
    if ( numMips > 1 )
    {
        GenerateMipsGPU( texResource, fmt, static_cast<UINT>( w ), static_cast<UINT>( h ), numMips );
        // GenerateMipsGPU ends with all subresources in PIXEL_SHADER_RESOURCE state.
    }
    else
    {
        TransitionBarrier( texResource,
                           D3D12_RESOURCE_STATE_COPY_DEST,
                           D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE );
    }

    // Create a Shader Resource View exposing the full mip chain.
    // Docs: https://learn.microsoft.com/en-us/windows/win32/api/d3d12/nf-d3d12-id3d12device-createshaderresourceview
    UINT srvIdx = AllocateStaticSRV();
    D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
    srvDesc.Format = fmt;
    srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
    srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    srvDesc.Texture2D.MipLevels = numMips;
    m_device->CreateShaderResourceView( texResource, &srvDesc, GetSRVStagingCpuHandle( srvIdx ) );

    // Register in texture array (1-based handle)
    TextureEntryDX12 entry = {};
    entry.resource = texResource;
    entry.srvIndex = srvIdx;
    entry.owned = true;
    m_textures.push_back( entry );
    return static_cast<uint32_t>( m_textures.size() );
}


void RenderBackendDX12::BindTexture( uint32_t handle, int slot )
{
    if ( slot < 0 || slot > 2 )
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

    // F3 screenshots are taken in input handling before Clear()/Render, so the backbuffer is
    // usually still in PRESENT state at this point. Scene-driven captures can happen after render
    // where the backbuffer is in RENDER_TARGET state. Preserve whichever state we're currently in.
    const D3D12_RESOURCE_STATES backBufferStateBeforeCopy =
        m_backBufferIsRT ? D3D12_RESOURCE_STATE_RENDER_TARGET : D3D12_RESOURCE_STATE_PRESENT;

    // Transition backbuffer to COPY_SOURCE for readback.
    TransitionBarrier( m_renderTargets[m_frameIndex], backBufferStateBeforeCopy, D3D12_RESOURCE_STATE_COPY_SOURCE );

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
    // Buffers are always created in COMMON state in D3D12 regardless of the initial state
    // specified. Specifying any other state fires warning #1328 (CREATERESOURCE_STATE_IGNORED).
    // READBACK buffers are accessed via CPU Map/Unmap — no GPU state barrier is needed.
    // Docs: https://learn.microsoft.com/en-us/windows/win32/direct3d12/using-resource-barriers-to-synchronize-resource-states-in-direct3d-12
    m_device->CreateCommittedResource( &readbackHeap, D3D12_HEAP_FLAG_NONE, &readbackDesc, D3D12_RESOURCE_STATE_COMMON, nullptr, IID_PPV_ARGS( &readbackBuffer ) );

    // Copy texture to readback buffer
    D3D12_TEXTURE_COPY_LOCATION dstLoc = {};
    dstLoc.pResource = readbackBuffer;
    dstLoc.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
    dstLoc.PlacedFootprint = footprint;

    D3D12_TEXTURE_COPY_LOCATION srcLoc = {};
    srcLoc.pResource = m_renderTargets[m_frameIndex];
    srcLoc.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;

    m_commandList->CopyTextureRegion( &dstLoc, 0, 0, 0, &srcLoc, nullptr );

    // Restore the exact state we found before the capture.
    TransitionBarrier( m_renderTargets[m_frameIndex], D3D12_RESOURCE_STATE_COPY_SOURCE, backBufferStateBeforeCopy );

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
    // Bind and draw the dynamic vertex buffer directly from upload heap memory.
    // Dynamic VBs (e.g. text quads) change every frame so they're drawn from upload memory
    // without copying to a default heap buffer — simpler but slightly slower for large batches.
    // Docs: https://learn.microsoft.com/en-us/windows/win32/api/d3d12/nf-d3d12-id3d12graphicscommandlist-iasetvertexbuffers
    m_commandList->IASetVertexBuffers( 0, 1, &vbv );
    // Docs: https://learn.microsoft.com/en-us/windows/win32/api/d3d12/nf-d3d12-id3d12graphicscommandlist-drawinstanced
    NoteDrawCall();
    m_commandList->DrawInstanced( (UINT)vertexCount, 1, 0, 0 );
}


void RenderBackendDX12::DestroyDynamicVB( uint32_t /*handle*/ )
{
    // No GPU resources to release — upload buffer is shared
}


// Draws per-vertex colored lines. data is interleaved [x,y,z,r,g,b] per vertex (6 floats each).
// Uses the shared upload buffer to stream vertex data and draws with LINE_LIST topology.
// Lazy-creates a LINE_LIST PSO on first call.
void RenderBackendDX12::DrawLinesColored( const float* data, int vertCount, const float* viewProjMatrix16 )
{
    if ( vertCount <= 0 )
    {
        return;
    }

    EnsureCommandListOpen();

    // Lazy-init shader and LINE_LIST PSO
    if ( !m_gridLineShader )
    {
        m_gridLineShader = CreateShader( "shaders/grid_line" );
    }
    if ( !m_gridLinePSO )
    {
        ShaderDX12* shader = static_cast<ShaderDX12*>( m_gridLineShader.get() );

        // Input layout: POSITION (float3) + TEXCOORD0 (float3)
        D3D12_INPUT_ELEMENT_DESC elements[2] = {};
        elements[0].SemanticName = "POSITION";
        elements[0].Format = DXGI_FORMAT_R32G32B32_FLOAT;
        elements[0].AlignedByteOffset = 0;
        elements[1].SemanticName = "TEXCOORD";
        elements[1].Format = DXGI_FORMAT_R32G32B32_FLOAT;
        elements[1].AlignedByteOffset = 12;

        D3D12_GRAPHICS_PIPELINE_STATE_DESC psoDesc = {};
        psoDesc.InputLayout.pInputElementDescs = elements;
        psoDesc.InputLayout.NumElements = 2;
        psoDesc.pRootSignature = m_rootSignature;
        psoDesc.VS.pShaderBytecode = shader->GetVSBytecode();
        psoDesc.VS.BytecodeLength = shader->GetVSBytecodeSize();
        psoDesc.PS.pShaderBytecode = shader->GetPSBytecode();
        psoDesc.PS.BytecodeLength = shader->GetPSBytecodeSize();
        psoDesc.RasterizerState.FillMode = D3D12_FILL_MODE_SOLID;
        psoDesc.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;
        psoDesc.RasterizerState.DepthClipEnable = TRUE;
        psoDesc.BlendState.RenderTarget[0].RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;
        psoDesc.DepthStencilState.DepthEnable = FALSE;
        psoDesc.DepthStencilState.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ZERO;
        psoDesc.SampleMask = UINT_MAX;
        psoDesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_LINE;
        psoDesc.NumRenderTargets = 1;
        psoDesc.RTVFormats[0] = DXGI_FORMAT_R8G8B8A8_UNORM;
        psoDesc.DSVFormat = DXGI_FORMAT_D32_FLOAT;
        psoDesc.SampleDesc.Count = 1;
        m_device->CreateGraphicsPipelineState( &psoDesc, IID_PPV_ARGS( &m_gridLinePSO ) );
    }

    // Upload vertex data to the shared upload buffer
    UINT64 dataSize = (UINT64)vertCount * 6 * sizeof( float );
    FlushUploadBufferIfNeeded( dataSize, 4 );
    UINT64 vbOffset = m_uploadOffset;
    memcpy( m_uploadBufferMapped[m_allocatorIndex] + m_uploadOffset, data, (size_t)dataSize );
    m_uploadOffset += ( dataSize + 255 ) & ~255ULL; // align to 256 bytes

    // Set pipeline state and draw
    m_commandList->SetPipelineState( m_gridLinePSO );
    m_commandList->SetGraphicsRootSignature( m_rootSignature );
    m_commandList->IASetPrimitiveTopology( D3D_PRIMITIVE_TOPOLOGY_LINELIST );

    // Set the viewProj matrix via root constants or CB slot 0
    ShaderDX12* shader = static_cast<ShaderDX12*>( m_gridLineShader.get() );
    m_activeShader = shader;
    m_psoDirty = true; // Force PSO rebind on next normal draw

    Matrix4 vpMat( viewProjMatrix16 );
    shader->SetMat4( "uViewProj", vpMat );
    D3D12_GPU_VIRTUAL_ADDRESS cbAddr = shader->FlushCB();
    if ( cbAddr )
    {
        m_commandList->SetGraphicsRootConstantBufferView( 0, cbAddr );
    }

    // Bind vertex buffer view
    D3D12_VERTEX_BUFFER_VIEW vbView = {};
    vbView.BufferLocation = m_uploadBuffers[m_allocatorIndex]->GetGPUVirtualAddress() + vbOffset;
    vbView.SizeInBytes = (UINT)dataSize;
    vbView.StrideInBytes = 6 * sizeof( float );
    m_commandList->IASetVertexBuffers( 0, 1, &vbView );

    // Bind render targets (depth disabled in PSO)
    m_commandList->OMSetRenderTargets( 1, &m_currentRTV, FALSE, &m_currentDSV );
    m_commandList->RSSetViewports( 1, &m_viewport );
    m_commandList->RSSetScissorRects( 1, &m_scissorRect );

    NoteDrawCall();
    m_commandList->DrawInstanced( (UINT)vertCount, 1, 0, 0 );
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

    // Create the static (shared) vertex buffer on the GPU-only Default Heap.
    // This holds geometry that doesn't change (sphere mesh) — it's uploaded once and reused.
    // Docs: https://learn.microsoft.com/en-us/windows/win32/api/d3d12/nf-d3d12-id3d12device-createcommittedresource
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

    // Buffers are always created in COMMON state in D3D12 regardless of what is specified here.
    // Specifying COPY_DEST fires warning #1328 (CREATERESOURCE_STATE_IGNORED). Use COMMON
    // explicitly, then rely on implicit promotion to COPY_DEST when CopyBufferRegion executes.
    // Docs: https://learn.microsoft.com/en-us/windows/win32/direct3d12/using-resource-barriers-to-synchronize-resource-states-in-direct3d-12#implicit-state-transitions
    m_device->CreateCommittedResource( &defaultHeap, D3D12_HEAP_FLAG_NONE, &bufDesc, D3D12_RESOURCE_STATE_COMMON, nullptr, IID_PPV_ARGS( &im.staticVB ) );

    // Upload static vertex data from CPU to GPU via the upload buffer, then transition to VB state.
    // Docs: https://learn.microsoft.com/en-us/windows/win32/api/d3d12/nf-d3d12-id3d12graphicscommandlist-copybufferregion
    FlushUploadBufferIfNeeded( dataSize, 4 );
    D3D12_GPU_VIRTUAL_ADDRESS uploadAddr = SubAllocateUpload( dataSize, 4 );
    memcpy( GetUploadPtr( uploadAddr ), staticData, (size_t)dataSize );
    m_commandList->CopyBufferRegion( im.staticVB, 0, m_uploadBuffers[m_allocatorIndex], uploadAddr - m_uploadBuffers[m_allocatorIndex]->GetGPUVirtualAddress(), dataSize );
    // Transition from COPY_DEST (implicit promotion after CopyBufferRegion) to the
    // combined read state used for both vertex fetch and DXR BLAS build SRV access.
    TransitionBarrier( im.staticVB, D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER | D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE );

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

    // Bind two vertex buffer slots: slot 0 has the shared geometry (sphere mesh), slot 1 has
    // per-instance data (position, color for each ball). The GPU reads slot 0 once per vertex
    // and slot 1 once per instance, combining them in the vertex shader.
    // Docs: https://learn.microsoft.com/en-us/windows/win32/api/d3d12/nf-d3d12-id3d12graphicscommandlist-iasetvertexbuffers
    m_commandList->IASetVertexBuffers( 0, 2, vbvs );

    // Draw all instances in one call — renders staticVertCount vertices × instanceCount copies.
    // This is the key optimization: 300 balls drawn in a single GPU dispatch.
    // Docs: https://learn.microsoft.com/en-us/windows/win32/api/d3d12/nf-d3d12-id3d12graphicscommandlist-drawinstanced
    NoteDrawCall();
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

    // Create the DXR root signature from the serialized blob. Same concept as the raster root
    // signature, but this one defines bindings for raytracing shaders (TLAS, UAV output, CBV, textures).
    // Docs: https://learn.microsoft.com/en-us/windows/win32/api/d3d12/nf-d3d12-id3d12device-createrootsignature
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
    std::string dxilPath = std::string( DATA_ROOT ) + "shaders/reflect.rt.dxil";
    std::string rtHlslPath = std::string( DATA_ROOT ) + "shaders/reflect.rt.hlsl";

    FILE* existCheck = nullptr;
    fopen_s( &existCheck, dxilPath.c_str(), "rb" );
    const bool dxilMissing = ( existCheck == nullptr );
    if ( existCheck )
    {
        fclose( existCheck );
    }

    if ( dxilMissing )
    {
        std::string cmd1 = "dxc.exe -T lib_6_3 -Fo " + dxilPath + " " + rtHlslPath + " 2>nul";
        std::string cmd2 = "\"C:/Program Files (x86)/Windows Kits/10/bin/10.0.22621.0/x64/dxc.exe\" -T lib_6_3 -Fo " + dxilPath + " " + rtHlslPath + " 2>nul";
        std::string cmd3 = "\"C:/Program Files (x86)/Windows Kits/10/bin/10.0.26100.0/x64/dxc.exe\" -T lib_6_3 -Fo " + dxilPath + " " + rtHlslPath + " 2>nul";
        const char* dxcPaths[] = { cmd1.c_str(), cmd2.c_str(), cmd3.c_str() };

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
    fopen_s( &dxilFile, dxilPath.c_str(), "rb" );
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

    // Create the DXR Raytracing Pipeline State Object (RTPSO). Unlike a graphics PSO, an RTPSO is
    // built from "subobjects" — a DXIL shader library containing all RT shaders, hit groups that
    // map geometry types to closest-hit shaders, shader config (payload/attribute sizes), pipeline
    // config (max recursion), and the root signature. This is more flexible than graphics PSOs.
    // Docs: https://learn.microsoft.com/en-us/windows/win32/api/d3d12/nf-d3d12-id3d12device5-createstateobject
    if ( FAILED( m_device5->CreateStateObject( &stateObjDesc, IID_PPV_ARGS( &m_rtPSO ) ) ) )
    {
        throw std::runtime_error( "CreateStateObject (RTPSO) failed" );
    }

    // Query the state object for shader identifier lookup (used when building the SBT).
    // Docs: https://learn.microsoft.com/en-us/windows/win32/api/d3d12/nn-d3d12-id3d12stateobjectproperties
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

    // Create the reflection UAV texture — this is the output target for DXR ray tracing.
    // Rays are cast from the water surface and the resulting reflections are written here.
    // The ALLOW_UNORDERED_ACCESS flag lets the ray generation shader write to arbitrary pixels.
    // Docs: https://learn.microsoft.com/en-us/windows/win32/api/d3d12/nf-d3d12-id3d12device-createcommittedresource
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

    // Skip re-initialisation if DXR is already set up (scene reload path). The terrain and sphere
    // meshes (and their BLAS) do not change between scenes — only the TLAS is rebuilt per-frame.
    // The full init path is only needed once; on renderer switch the backend is destroyed/recreated,
    // so m_cmdList4 is null and we fall through to the full init below.
    if ( m_cmdList4 )
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

    // Create RT constant buffer on the upload heap — holds per-frame raytracing parameters
    // (inverse VP matrix, camera position, water height, light position, etc.). Persistently
    // mapped so we can update it every frame without Map/Unmap overhead. 256-byte aligned per DX12 CBV rules.
    // Docs: https://learn.microsoft.com/en-us/windows/win32/api/d3d12/nf-d3d12-id3d12device-createcommittedresource
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

    // Set the compute root signature for raytracing. DXR uses the compute pipeline (not graphics)
    // because ray tracing doesn't use the traditional rasterization pipeline (no vertex/pixel stages).
    // Docs: https://learn.microsoft.com/en-us/windows/win32/api/d3d12/nf-d3d12-id3d12graphicscommandlist-setcomputerootsignature
    m_cmdList4->SetComputeRootSignature( m_rtRootSignature );

    // Bind the DXR raytracing pipeline state object. SetPipelineState1 is the DXR-specific version
    // that accepts an ID3D12StateObject (RTPSO) instead of a regular ID3D12PipelineState (graphics PSO).
    // Docs: https://learn.microsoft.com/en-us/windows/win32/api/d3d12/nf-d3d12-id3d12graphicscommandlist4-setpipelinestate1
    m_cmdList4->SetPipelineState1( m_rtPSO );

    // Bind the shader-visible descriptor heap for DXR (same heap as raster, re-bound after compute).
    // Docs: https://learn.microsoft.com/en-us/windows/win32/api/d3d12/nf-d3d12-id3d12graphicscommandlist-setdescriptorheaps
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

    // DispatchRays — the DXR equivalent of a draw call. This launches one ray per pixel of the
    // reflection texture. The GPU executes the ray generation shader, which casts rays into the
    // scene. When a ray hits geometry, the closest hit shader runs. If nothing is hit, the miss
    // shader runs. Results are written to the reflection UAV texture. The SBT tells the GPU which
    // shader to invoke for each case.
    // Docs: https://learn.microsoft.com/en-us/windows/win32/api/d3d12/nf-d3d12-id3d12graphicscommandlist4-dispatchrays
    D3D12_DISPATCH_RAYS_DESC dispatchDesc = {};
    dispatchDesc.RayGenerationShaderRecord = m_sbt.RayGenRange();
    dispatchDesc.MissShaderTable = m_sbt.MissRange();
    dispatchDesc.HitGroupTable = m_sbt.HitGroupRange();
    dispatchDesc.Width = (UINT)m_reflectionWidth;
    dispatchDesc.Height = (UINT)m_reflectionHeight;
    dispatchDesc.Depth = 1;

    m_cmdList4->DispatchRays( &dispatchDesc );

    // UAV barrier — ensures all ray tracing writes complete before the water shader reads the texture.
    // Docs: https://learn.microsoft.com/en-us/windows/win32/api/d3d12/nf-d3d12-id3d12graphicscommandlist-resourcebarrier
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


void RenderBackendDX12::TryConsumeGpuTimerReadback( bool waitForFence )
{
    if ( !m_gpuTimers.queryHeap || !m_gpuTimers.readPending || !m_gpuTimers.readbackBuf || !m_fence )
    {
        return;
    }

    // Non-blocking mode is used in the normal frame loop so GPU timers keep working even when
    // PipelineSync is disabled. Blocking mode is only used by Finish()/FlushGPU().
    if ( waitForFence )
    {
        if ( m_fence->GetCompletedValue() < m_gpuTimers.readFenceValue )
        {
            m_fence->SetEventOnCompletion( m_gpuTimers.readFenceValue, m_fenceEvent );
            WaitForSingleObject( m_fenceEvent, INFINITE );
        }
    }
    else if ( m_fence->GetCompletedValue() < m_gpuTimers.readFenceValue )
    {
        // The Signal is submitted right after vsync — the GPU needs only nanoseconds to
        // process it, but in optimised builds the CPU can arrive here before it fires.
        // Spin briefly (a few hundred pauses ≈ a few microseconds) to catch it without
        // burning a full WaitForSingleObject kernel call.
        // Docs: https://learn.microsoft.com/en-us/windows/win32/api/synchapi/nf-synchapi-yieldprocessor
        for ( int spin = 0; spin < 512; ++spin )
        {
            YieldProcessor();
            if ( m_fence->GetCompletedValue() >= m_gpuTimers.readFenceValue )
            {
                break;
            }
        }
        if ( m_fence->GetCompletedValue() < m_gpuTimers.readFenceValue )
        {
            return; // genuinely not ready — try again next frame
        }
    }

    D3D12_RANGE readRange = { 0, (SIZE_T)TIMER_HEAP_SIZE * sizeof( uint64_t ) };
    uint64_t* pData = nullptr;
    if ( SUCCEEDED( m_gpuTimers.readbackBuf->Map( 0, &readRange, (void**)&pData ) ) )
    {
        std::memset( m_gpuTimers.resultMs, 0, sizeof( m_gpuTimers.resultMs ) );
        std::memset( m_gpuTimers.resultValid, 0, sizeof( m_gpuTimers.resultValid ) );

        for ( int i = 0; i < TIMER_HEAP_MARKERS; ++i )
        {
            const uint64_t t0 = pData[i * 2 + 0];
            const uint64_t t1 = pData[i * 2 + 1];
            if ( t1 > t0 && m_gpuTimers.freq > 0 )
            {
                m_gpuTimers.resultMs[i] = static_cast<float>( static_cast<double>( t1 - t0 ) / static_cast<double>( m_gpuTimers.freq ) * 1000.0 );
                m_gpuTimers.resultValid[i] = true;
            }
        }

        D3D12_RANGE writeRange = { 0, 0 };
        m_gpuTimers.readbackBuf->Unmap( 0, &writeRange );
    }

    m_gpuTimers.readPending = false;
}


void RenderBackendDX12::GpuTimerBegin( int markerIdx )
{
    if ( !m_gpuTimers.queryHeap || markerIdx < 0 || markerIdx >= TIMER_HEAP_MARKERS )
    {
        return;
    }
    EnsureCommandListOpen();
    int slot = markerIdx * 2 + 0;
    m_commandList->EndQuery( m_gpuTimers.queryHeap, D3D12_QUERY_TYPE_TIMESTAMP, (UINT)slot );
    m_gpuTimers.slotWritten[slot] = true;
}


void RenderBackendDX12::GpuTimerEnd( int markerIdx )
{
    if ( !m_gpuTimers.queryHeap || !m_commandListOpen || markerIdx < 0 || markerIdx >= TIMER_HEAP_MARKERS )
    {
        return;
    }
    int slot = markerIdx * 2 + 1;
    m_commandList->EndQuery( m_gpuTimers.queryHeap, D3D12_QUERY_TYPE_TIMESTAMP, (UINT)slot );
    m_gpuTimers.slotWritten[slot] = true;
}


void RenderBackendDX12::GpuTimerInvalidate()
{
    // If there's a pending readback from a previous frame, consume it now (blocking)
    // before clearing state. This prevents the dangling readPending from causing
    // stale data to be attributed to newly-registered markers after the reset.
    if ( m_gpuTimers.readPending )
    {
        TryConsumeGpuTimerReadback( true );
    }

    // resultMs and resultValid are intentionally PRESERVED (same reasoning as DX11):
    // After a reset, the non-blocking TryConsumeGpuTimerReadback in GpuTimerRead may fail
    // its 512-spin if the GPU hasn't completed the first post-reset frame yet. Preserving
    // stale resultValid lets ReadPendingGpuResults immediately see data and keeps the GPU
    // column visible. The next successful consume overwrites all entries with fresh data.
    std::memset( m_gpuTimers.slotWritten, 0, sizeof( m_gpuTimers.slotWritten ) );
    m_gpuTimers.readPending = false;
    m_gpuTimers.readFenceValue = 0;
}


bool RenderBackendDX12::GpuTimerRead( int markerIdx, float& outMs )
{
    TryConsumeGpuTimerReadback( false );

    if ( markerIdx < 0 || markerIdx >= TIMER_HEAP_MARKERS || !m_gpuTimers.resultValid[markerIdx] )
    {
        return false;
    }
    outMs = m_gpuTimers.resultMs[markerIdx];
    return true;
}
