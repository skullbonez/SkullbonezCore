/*
File: SkullbonezSource/SkullbonezRenderBackendDX12.cpp
Purpose:
  Implements the production DX12 renderer and its frame, resource, and pipeline state.

Mental model:
  DX12 separates resource memory, descriptor rows, command recording, and GPU
  execution. Ownership, state transitions, descriptor lifetime, and fence
  ordering are the important ideas.

Glossary:
  DX12 (DirectX 12): Production renderer API used for explicit GPU resource,
  descriptor, and command-list control.
  SRV (Shader Resource View): Descriptor row used when shaders read textures
  or buffers.
  UAV (Unordered Access View): Descriptor row used when compute or raytracing
  shaders write textures or buffers.
  CBV (Constant Buffer View): Descriptor row used when shaders read a packed
  block of constants.
  PSO (Pipeline State Object): Precompiled bundle of shaders and fixed render
  state that DX12 binds before drawing or dispatching.
  GPU (Graphics Processing Unit): Processor that executes rendering, compute,
  and raytracing commands asynchronously from the CPU.
  CPU (Central Processing Unit): Host processor running engine code and
  recording GPU commands.
  Descriptor: Small binding record that tells a renderer how to interpret a
  resource.
  Back buffer: Swap-chain image that will be presented to the window.

Invariants:
  - DX12 object lifetime, resource states, descriptor rows, and fence ordering
  must stay explicit.

Related:
  - SkullbonezSource/SkullbonezRenderBackendDX12.h
  - Agentic/Reference/skullbonez-core-class-structure.md
  - Agentic/Reference/comment-style-guide.md
*/
// --- DX12 Architecture ---
//
// DX12 is explicit: the engine records command lists, submits them to queues,
// manages resource states, and waits on fences before reusing memory.
//
// Core flow:
//   App -> CommandList -> CommandQueue -> GPU
//   (the engine manages memory, synchronization, resource states, and command recording)
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
// Additional architecture glossary for non-GPU readers:
//
//   Descriptor
//     A small GPU-readable record that describes a resource view. It is not the
//     texture or buffer itself. It says how a shader should see that resource:
//     as a texture SRV, writable UAV, render target, depth target, and so on.
//
//   Descriptor Heap
//     A table of descriptors. DX12 makes the engine allocate rows in this table
//     explicitly. Shaders are given GPU handles that point at rows in a
//     shader-visible heap.
//
//   Descriptor Allocator
//     This is our table-row allocator for descriptors. It does not allocate GPU
//     images. It only hands out descriptor indices and makes static-vs-transient
//     lifetime explicit so the CPU does not overwrite a table row while the GPU
//     still has a handle pointing at it.
//
//   Resource Barrier
//     A synchronization command that tells the GPU a resource is changing use.
//     Example: "this texture was a render target; now shaders will sample it."
//     The future render graph exists so these transitions are declared once
//     from pass/resource usage instead of hand-coded throughout the backend.
//
#include "SkullbonezRenderBackendDX12.h"
#include "SkullbonezShaderDX12.h"
#include "SkullbonezMeshDX12.h"
#include "SkullbonezFramebufferDX12.h"
#include "SkullbonezRenderGraph.h"
#include "SkullbonezPlatformProfiler.h"
#include <stdexcept>
#include <cstdio>
#include <cstring>
#include <algorithm>
#include <string>
#include <fstream>
#include <sstream>
#include <wrl/client.h>


using namespace SkullbonezCore::Math::Transformation;
using namespace SkullbonezCore::Rendering;
using Microsoft::WRL::ComPtr;


// --- Helpers ---
static void ReportDX12DescriptorHeapExhausted( const char* heapName, UINT nextIndex, UINT capacity )
{
    const char* name = heapName ? heapName : "unknown";
    fprintf( stderr, "FATAL: DX12 %s heap exhausted (next=%u capacity=%u)\n", name, nextIndex, capacity );
    fprintf( stdout, "FATAL: DX12 %s heap exhausted (next=%u capacity=%u)\n", name, nextIndex, capacity );
    fflush( stderr );
    fflush( stdout );
    Log().WriteEventf( "dx12_descriptor_heap_exhausted heap=%s next=%u capacity=%u", name, nextIndex, capacity );
    Log().FlushAll();
}

static inline void ThrowIfFailed( HRESULT hr, const char* msg )
{
    if ( FAILED( hr ) )
    {
        throw std::runtime_error( msg );
    }
}

static bool IsDx12DeviceLostResult( HRESULT hr )
{
    return hr == DXGI_ERROR_DEVICE_REMOVED ||
           hr == DXGI_ERROR_DEVICE_RESET ||
           hr == DXGI_ERROR_DRIVER_INTERNAL_ERROR;
}

static void AppendDx12StateFlag( std::ostringstream& out, bool& wroteAny, D3D12_RESOURCE_STATES state, D3D12_RESOURCE_STATES flag, const char* name )
{
    if ( ( state & flag ) != 0 )
    {
        if ( wroteAny )
        {
            out << "|";
        }
        out << name;
        wroteAny = true;
    }
}

static std::string Dx12StateToString( D3D12_RESOURCE_STATES state )
{
    if ( state == D3D12_RESOURCE_STATE_COMMON )
    {
        return "COMMON";
    }

    std::ostringstream out;
    bool wroteAny = false;
    AppendDx12StateFlag( out, wroteAny, state, D3D12_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER, "VERTEX_AND_CONSTANT_BUFFER" );
    AppendDx12StateFlag( out, wroteAny, state, D3D12_RESOURCE_STATE_INDEX_BUFFER, "INDEX_BUFFER" );
    AppendDx12StateFlag( out, wroteAny, state, D3D12_RESOURCE_STATE_RENDER_TARGET, "RENDER_TARGET" );
    AppendDx12StateFlag( out, wroteAny, state, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, "UNORDERED_ACCESS" );
    AppendDx12StateFlag( out, wroteAny, state, D3D12_RESOURCE_STATE_DEPTH_WRITE, "DEPTH_WRITE" );
    AppendDx12StateFlag( out, wroteAny, state, D3D12_RESOURCE_STATE_DEPTH_READ, "DEPTH_READ" );
    AppendDx12StateFlag( out, wroteAny, state, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, "NON_PIXEL_SHADER_RESOURCE" );
    AppendDx12StateFlag( out, wroteAny, state, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, "PIXEL_SHADER_RESOURCE" );
    AppendDx12StateFlag( out, wroteAny, state, D3D12_RESOURCE_STATE_STREAM_OUT, "STREAM_OUT" );
    AppendDx12StateFlag( out, wroteAny, state, D3D12_RESOURCE_STATE_INDIRECT_ARGUMENT, "INDIRECT_ARGUMENT" );
    AppendDx12StateFlag( out, wroteAny, state, D3D12_RESOURCE_STATE_COPY_DEST, "COPY_DEST" );
    AppendDx12StateFlag( out, wroteAny, state, D3D12_RESOURCE_STATE_COPY_SOURCE, "COPY_SOURCE" );
    AppendDx12StateFlag( out, wroteAny, state, D3D12_RESOURCE_STATE_RESOLVE_DEST, "RESOLVE_DEST" );
    AppendDx12StateFlag( out, wroteAny, state, D3D12_RESOURCE_STATE_RESOLVE_SOURCE, "RESOLVE_SOURCE" );
    AppendDx12StateFlag( out, wroteAny, state, D3D12_RESOURCE_STATE_PRESENT, "PRESENT" );
    if ( !wroteAny )
    {
        out << "UNKNOWN(" << static_cast<unsigned int>( state ) << ")";
    }
    return out.str();
}

static bool TryGraphAccessToDx12State( RenderGraphResourceAccess access, D3D12_RESOURCE_STATES& outState )
{
    switch ( access )
    {
    case RenderGraphResourceAccess::RenderTarget:
        outState = D3D12_RESOURCE_STATE_RENDER_TARGET;
        return true;
    case RenderGraphResourceAccess::DepthRead:
        outState = D3D12_RESOURCE_STATE_DEPTH_READ;
        return true;
    case RenderGraphResourceAccess::DepthWrite:
        outState = D3D12_RESOURCE_STATE_DEPTH_WRITE;
        return true;
    case RenderGraphResourceAccess::PixelShaderResource:
        outState = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
        return true;
    case RenderGraphResourceAccess::NonPixelShaderResource:
        outState = D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
        return true;
    case RenderGraphResourceAccess::UnorderedAccess:
        outState = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
        return true;
    case RenderGraphResourceAccess::CopySource:
        outState = D3D12_RESOURCE_STATE_COPY_SOURCE;
        return true;
    case RenderGraphResourceAccess::CopyDest:
        outState = D3D12_RESOURCE_STATE_COPY_DEST;
        return true;
    case RenderGraphResourceAccess::Present:
        outState = D3D12_RESOURCE_STATE_PRESENT;
        return true;
    case RenderGraphResourceAccess::Unknown:
    default:
        outState = D3D12_RESOURCE_STATE_COMMON;
        return false;
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
    if ( !m_renderDevice.FrameFence().IsReady() )
    {
        return;
    }

    // Tell the GPU to mark the next fence value after all already-submitted
    // queue work, then block until that value is complete. In plain terms:
    // WaitForGpu() means "do not let the CPU continue until the GPU has caught
    // up to every command we submitted so far."
    m_renderDevice.FrameFence().SignalAndWait();

    // After full GPU wait, all frame fences are implicitly completed
    for ( int i = 0; i < FRAME_COUNT; ++i )
    {
        m_frameFenceValues[i] = 0;
    }
}


void RenderBackendDX12::AssertPlatformProfilerGpuStackClosed( const char* reason ) const
{
    if ( m_platformProfilerGpuDepth == 0 || !SkullbonezCore::Basics::PlatformProfiler::IsEnabled() )
    {
        return;
    }

    Log().WriteEventf( "dx12_platform_profiler_open_stack_on_submit reason=%s depth=%d", reason ? reason : "unknown", m_platformProfilerGpuDepth );
    assert( m_platformProfilerGpuDepth == 0 );
    throw std::runtime_error( "DX12 platform profiler GPU stack left open before command submission" );
}


void RenderBackendDX12::EnsureCommandListOpen()
{
    if ( !m_commandList || !m_commandQueue || !m_renderDevice.FrameFence().IsReady() || !m_commandAllocators[m_allocatorIndex] )
    {
        throw std::runtime_error( "DX12 backend is not fully initialised (command list/fence unavailable)." );
    }

    if ( m_commandListOpen )
    {
        return;
    }

    // Wait for the GPU to finish with this allocator's previous work.
    //
    // "Allocator" here is easy to misread. It is the command allocator: memory
    // for recorded GPU commands, not texture memory. This backend also ties the
    // upload arena and transient descriptor range to the same frame index. The
    // fence value proves all three pieces of temporary per-frame storage are no
    // longer being read by the GPU before we reset them.
    UINT64 completedFence = m_renderDevice.FrameFence().CompletedValue();
    if ( m_frameFenceValues[m_allocatorIndex] > completedFence )
    {
        m_renderDevice.FrameFence().WaitForValue( m_frameFenceValues[m_allocatorIndex] );
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

    // The command allocator, upload arena, and transient descriptor range all
    // share the same lifetime. We waited for this frame allocator's fence above,
    // so the GPU is no longer reading:
    //
    // - commands recorded into this allocator,
    // - upload bytes written for those commands,
    // - temporary descriptors bound by those commands.
    //
    // That is why it is safe to reset these two cursors here. Without the fence
    // wait, these resets could make the CPU overwrite data the GPU still needs.
    m_uploadSystem.ResetFrame( m_allocatorIndex );
    m_srvDescriptors.ResetFrame( m_allocatorIndex );

    // All command list state is reset — force full rebind on next draw
    m_lastPSOHash = 0;
    m_texBindingsDirty = true;
    m_targetsDirty = true;
}


void RenderBackendDX12::TransitionBarrier( ID3D12Resource* resource, D3D12_RESOURCE_STATES before, D3D12_RESOURCE_STATES after )
{
    if ( !resource || before == after )
    {
        return;
    }
    RecordLiveBarrier( "TransitionBarrier", resource, before, after );
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


void RenderBackendDX12::RecordLiveBarrier( const char* source, ID3D12Resource* resource, D3D12_RESOURCE_STATES before, D3D12_RESOURCE_STATES after )
{
    if ( !resource || before == after )
    {
        return;
    }

    // This is a migration diagnostic, not a hot-path render feature. Keep a
    // bounded sample of barriers so long validation runs cannot grow the vector
    // without limit. The first records are the most useful because they show the
    // early frame shape the render graph must eventually own.
    constexpr size_t MAX_LIVE_BARRIER_RECORDS = 4096;
    if ( m_liveBarrierRecords.size() >= MAX_LIVE_BARRIER_RECORDS )
    {
        return;
    }

    LiveBarrierRecordDX12 record;
    record.resource = resource;
    record.before = before;
    record.after = after;
    record.source = source ? source : "unknown";
    m_liveBarrierRecords.push_back( record );
}


void RenderBackendDX12::FlushUploadBuffer()
{
    if ( !m_commandListOpen )
    {
        return;
    }
    // Submit current work and wait for completion (mid-frame flush for upload exhaustion)
    AssertPlatformProfilerGpuStackClosed( "FlushUploadBuffer" );
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

    // FlushUploadBuffer submits and waits for all current GPU work before it
    // reopens the command list. That blocking wait is expensive, but it gives
    // the same safety proof as a normal frame-fence wait: the old upload bytes
    // and temporary descriptors are no longer in use, so the arenas can rewind.
    m_uploadSystem.ResetFrame( m_allocatorIndex );
    m_srvDescriptors.ResetFrame( m_allocatorIndex );
    m_lastPSOHash = 0;
    m_texBindingsDirty = true;
    m_targetsDirty = true;
}


void RenderBackendDX12::FlushUploadBufferIfNeeded( UINT64 size, UINT64 alignment )
{
    if ( !m_uploadSystem.CanAllocate( m_allocatorIndex, size, alignment ) )
    {
        // This is the expensive fallback path. It means the CPU has filled this
        // frame's upload arena before the frame was submitted, so we must submit
        // the current command list and wait before reusing the same bytes. The
        // event log keeps this visible because frequent mid-frame flushes are a
        // sign that the future Dx12RenderDevice needs larger upload pages or a
        // different upload strategy for the current workload.
        const Dx12UploadArenaStats stats = m_uploadSystem.GetStats( m_allocatorIndex );
        Log().WriteEventf( "dx12_upload_arena_flush frame=%u used_bytes=%llu capacity_bytes=%llu requested_bytes=%llu alignment=%llu",
                           m_allocatorIndex,
                           static_cast<unsigned long long>( stats.usedBytes ),
                           static_cast<unsigned long long>( stats.capacityBytes ),
                           static_cast<unsigned long long>( size ),
                           static_cast<unsigned long long>( alignment ) );
        FlushUploadBuffer();
    }
}


void RenderBackendDX12::ReportArchitectureStats( const char* reason ) const
{
    const Dx12CpuDescriptorAllocatorStats rtvStats = m_rtvDescriptors.GetStats();
    const Dx12CpuDescriptorAllocatorStats dsvStats = m_dsvDescriptors.GetStats();
    const Dx12DescriptorAllocatorStats descriptorStats = m_srvDescriptors.GetStats();
    UINT64 uploadPeakBytes = 0;
    UINT64 uploadCapacityBytes = 0;
    for ( int i = 0; i < FRAME_COUNT; ++i )
    {
        const Dx12UploadArenaStats uploadStats = m_uploadSystem.GetStats( static_cast<UINT>( i ) );
        uploadPeakBytes = (std::max)( uploadPeakBytes, uploadStats.peakBytes );
        uploadCapacityBytes += uploadStats.capacityBytes;
    }

    // This event is intentionally written at the architecture boundary rather
    // than in every draw call. It tells a future render-graph/device pass how
    // much descriptor and upload memory the old backend needed, without turning
    // the hot path into noisy logging. The numbers are also layman-readable:
    // "RTV/DSV descriptors" are CPU-only output/depth target view slots,
    // "static SRVs" are persistent texture/view slots, "transient SRVs" are
    // per-frame descriptor copies, and "upload peak" is the largest CPU-written
    // staging allocation used by any one in-flight frame.
    Log().WriteEventf( "dx12_render_architecture_stats reason=%s root_parameters=%u ordinary_raster_srv_slots=t%u..t%u rtv_descriptors=%u/%u dsv_descriptors=%u/%u static_srvs=%u/%u transient_srv_peak=%u/%u upload_peak_bytes=%llu upload_capacity_bytes=%llu",
                       reason ? reason : "unknown",
                       ORDINARY_RASTER_ROOT_PARAMETER_COUNT,
                       SHADER_REGISTER_FIRST_TEXTURE,
                       SHADER_REGISTER_FIRST_TEXTURE + static_cast<UINT>( TEXTURE_SLOT_COUNT - 1 ),
                       rtvStats.used,
                       rtvStats.capacity,
                       dsvStats.used,
                       dsvStats.capacity,
                       descriptorStats.staticUsed,
                       descriptorStats.staticCapacity,
                       descriptorStats.transientPeakThisRun,
                       descriptorStats.transientCapacityPerFrame,
                       static_cast<unsigned long long>( uploadPeakBytes ),
                       static_cast<unsigned long long>( uploadCapacityBytes ) );
}


void RenderBackendDX12::DumpFrameGraphSkeleton() const
{
    // Diagnostic-only render graph sketch.
    //
    // This is intentionally not the live renderer yet. The current backend still
    // records barriers by hand in Clear(), FramebufferDX12::Bind/Unbind(),
    // DispatchReflectionRays(), GenerateMipsGPU(), screenshot readback, and
    // Present().
    //
    // The purpose of this skeleton is to make the intended frame shape visible
    // in the same pass/resource language the future render graph will use. It is
    // a bridge for humans and future code review:
    //
    // - resources below are names for existing backend-owned render targets,
    //   depth buffers, shadow maps, and reflection outputs,
    // - passes below are the current high-level frame phases, including optional
    //   cinematic and DXR paths,
    // - Compile() emits API-neutral transitions that can later be compared with
    //   hand-written DX12 barriers before those barriers move into the graph.
    //
    // This is a superset of possible frame paths. A normal non-cinematic frame
    // writes directly to the backbuffer; a cinematic frame writes SceneColor and
    // then tonemaps it to the backbuffer; reflection can be raster FBO or DXR.
    // Keeping the alternatives explicit is useful while the old renderer is
    // still being decomposed.
    RenderGraph graph;

    const RenderGraphResourceHandle backbuffer = graph.AddExternalResource( "SwapchainBackbuffer", RenderGraphResourceAccess::Present, m_renderTargets[m_frameIndex] );
    const RenderGraphResourceHandle mainDepth = graph.AddExternalResource( "MainDepthStencil", RenderGraphResourceAccess::DepthWrite, m_depthStencil );
    const RenderGraphResourceHandle shadowDepth = graph.AddExternalResource( "TerrainShadowMapDepth", RenderGraphResourceAccess::PixelShaderResource );
    const RenderGraphResourceHandle objectShadowDepth = graph.AddExternalResource( "ObjectShadowMapDepth", RenderGraphResourceAccess::PixelShaderResource );
    const RenderGraphResourceHandle reflectionColor = graph.AddExternalResource( "RasterReflectionColor", RenderGraphResourceAccess::PixelShaderResource );
    const RenderGraphResourceHandle reflectionDepth = graph.AddExternalResource( "RasterReflectionDepth", RenderGraphResourceAccess::PixelShaderResource );
    const RenderGraphResourceHandle dxrReflection = graph.AddExternalResource( "DxrReflectionTexture", RenderGraphResourceAccess::PixelShaderResource, m_reflectionUAV );
    const RenderGraphResourceHandle sceneColor = graph.AddExternalResource( "CinematicSceneColor", RenderGraphResourceAccess::PixelShaderResource );
    const RenderGraphResourceHandle sceneDepth = graph.AddExternalResource( "CinematicSceneDepth", RenderGraphResourceAccess::PixelShaderResource );
    const RenderGraphResourceHandle volumetricLight = graph.AddExternalResource( "VolumetricLight", RenderGraphResourceAccess::PixelShaderResource );

    uint32_t pass = graph.AddPass( "ShadowMapPass" );
    graph.AddWrite( pass, shadowDepth, RenderGraphResourceAccess::DepthWrite );
    graph.AddWrite( pass, objectShadowDepth, RenderGraphResourceAccess::DepthWrite );

    pass = graph.AddPass( "RasterReflectionPass" );
    graph.AddRead( pass, shadowDepth, RenderGraphResourceAccess::PixelShaderResource );
    graph.AddRead( pass, objectShadowDepth, RenderGraphResourceAccess::PixelShaderResource );
    graph.AddWrite( pass, reflectionColor, RenderGraphResourceAccess::RenderTarget );
    graph.AddWrite( pass, reflectionDepth, RenderGraphResourceAccess::DepthWrite );

    pass = graph.AddPass( "DxrReflectionPass", RenderGraphQueueType::Compute );
    graph.AddWrite( pass, dxrReflection, RenderGraphResourceAccess::UnorderedAccess );

    pass = graph.AddPass( "BackbufferScenePass" );
    graph.AddRead( pass, shadowDepth, RenderGraphResourceAccess::PixelShaderResource );
    graph.AddRead( pass, objectShadowDepth, RenderGraphResourceAccess::PixelShaderResource );
    graph.AddRead( pass, reflectionColor, RenderGraphResourceAccess::PixelShaderResource );
    graph.AddRead( pass, dxrReflection, RenderGraphResourceAccess::PixelShaderResource );
    graph.AddWrite( pass, backbuffer, RenderGraphResourceAccess::RenderTarget );
    graph.AddWrite( pass, mainDepth, RenderGraphResourceAccess::DepthWrite );

    pass = graph.AddPass( "CinematicScenePass" );
    graph.AddRead( pass, shadowDepth, RenderGraphResourceAccess::PixelShaderResource );
    graph.AddRead( pass, objectShadowDepth, RenderGraphResourceAccess::PixelShaderResource );
    graph.AddRead( pass, reflectionColor, RenderGraphResourceAccess::PixelShaderResource );
    graph.AddRead( pass, dxrReflection, RenderGraphResourceAccess::PixelShaderResource );
    graph.AddWrite( pass, sceneColor, RenderGraphResourceAccess::RenderTarget );
    graph.AddWrite( pass, sceneDepth, RenderGraphResourceAccess::DepthWrite );

    pass = graph.AddPass( "VolumetricLightPass", RenderGraphQueueType::Graphics, RenderGraphBarrierPolicy::HandoffValidated );
    graph.AddRead( pass, sceneColor, RenderGraphResourceAccess::PixelShaderResource );
    graph.AddRead( pass, sceneDepth, RenderGraphResourceAccess::PixelShaderResource );
    graph.AddWrite( pass, volumetricLight, RenderGraphResourceAccess::RenderTarget );

    pass = graph.AddPass( "ToneMapPass", RenderGraphQueueType::Graphics, RenderGraphBarrierPolicy::HandoffValidated );
    graph.AddRead( pass, sceneColor, RenderGraphResourceAccess::PixelShaderResource );
    graph.AddRead( pass, sceneDepth, RenderGraphResourceAccess::PixelShaderResource );
    graph.AddRead( pass, volumetricLight, RenderGraphResourceAccess::PixelShaderResource );
    graph.AddWrite( pass, backbuffer, RenderGraphResourceAccess::RenderTarget );

    pass = graph.AddPass( "DebugAndUiPass" );
    graph.AddWrite( pass, backbuffer, RenderGraphResourceAccess::RenderTarget );

    pass = graph.AddPass( "Present" );
    graph.AddWrite( pass, backbuffer, RenderGraphResourceAccess::Present );

    const RenderGraphCompileResult compiled = graph.Compile();
    std::vector<bool> liveBarrierMatched( m_liveBarrierRecords.size(), false );
    const auto liveResourceLabel = [&]( const void* resource ) -> const char*
    {
        if ( !resource )
        {
            return nullptr;
        }
        for ( int i = 0; i < FRAME_COUNT; ++i )
        {
            if ( resource == m_renderTargets[i] )
            {
                return "SwapchainBackbuffer";
            }
        }
        if ( resource == m_depthStencil )
        {
            return "MainDepthStencil";
        }
        if ( resource == m_reflectionUAV )
        {
            return "DxrReflectionTexture";
        }
        return nullptr;
    };

    std::ostringstream out;
    out << graph.DumpText();
    out << "\nLiveBackendTransitionBarriers:\n";
    if ( m_liveBarrierRecords.empty() )
    {
        out << "  none recorded yet\n";
    }
    for ( size_t i = 0; i < m_liveBarrierRecords.size(); ++i )
    {
        const LiveBarrierRecordDX12& live = m_liveBarrierRecords[i];
        const char* resourceLabel = liveResourceLabel( live.resource );
        out << "  [" << i << "] source=" << ( live.source ? live.source : "unknown" )
            << " resource=" << live.resource
            << " label=" << ( resourceLabel ? resourceLabel : "unlabeled" )
            << " " << Dx12StateToString( live.before )
            << " -> " << Dx12StateToString( live.after ) << "\n";
    }

    out << "\nGraphVsLiveTransitionStatePairs:\n";
    out << "  graph_transition_count=" << compiled.transitions.size() << "\n";
    out << "  live_transition_barrier_count=" << m_liveBarrierRecords.size() << "\n";

    size_t matchedResourcePairs = 0;
    size_t matchedStateOnlyPairs = 0;
    size_t graphHandoffTransitionCount = 0;
    size_t graphOnlyDetails = 0;
    size_t unknownGraphTransitions = 0;
    constexpr size_t MAX_COMPARISON_DETAILS = 256;
    for ( const RenderGraphTransitionDesc& transition : compiled.transitions )
    {
        D3D12_RESOURCE_STATES graphBefore = D3D12_RESOURCE_STATE_COMMON;
        D3D12_RESOURCE_STATES graphAfter = D3D12_RESOURCE_STATE_COMMON;
        const bool hasConcreteBefore = TryGraphAccessToDx12State( transition.before, graphBefore );
        const bool hasConcreteAfter = TryGraphAccessToDx12State( transition.after, graphAfter );
        const RenderGraphResourceDesc& resource = graph.Resources()[transition.resource.index];
        const RenderGraphPassDesc& passDesc = graph.Passes()[transition.passIndex];
        if ( passDesc.barrierPolicy == RenderGraphBarrierPolicy::HandoffValidated )
        {
            ++graphHandoffTransitionCount;
        }
        if ( !hasConcreteBefore || !hasConcreteAfter )
        {
            ++unknownGraphTransitions;
            if ( graphOnlyDetails < MAX_COMPARISON_DETAILS )
            {
                out << "  graph_unknown before pass [" << transition.passIndex << "] " << passDesc.name
                    << ": " << resource.name
                    << " " << ToString( transition.before )
                    << " -> " << ToString( transition.after ) << "\n";
                ++graphOnlyDetails;
            }
            continue;
        }

        bool matched = false;
        for ( size_t liveIndex = 0; liveIndex < m_liveBarrierRecords.size(); ++liveIndex )
        {
            const LiveBarrierRecordDX12& live = m_liveBarrierRecords[liveIndex];
            const char* label = liveResourceLabel( live.resource );
            if ( !liveBarrierMatched[liveIndex] && transition.nativeResource && live.resource == transition.nativeResource && live.before == graphBefore && live.after == graphAfter )
            {
                liveBarrierMatched[liveIndex] = true;
                matched = true;
                ++matchedResourcePairs;
                break;
            }
            if ( !liveBarrierMatched[liveIndex] && label && std::strcmp( label, resource.name.c_str() ) == 0 && live.before == graphBefore && live.after == graphAfter )
            {
                liveBarrierMatched[liveIndex] = true;
                matched = true;
                ++matchedResourcePairs;
                break;
            }
        }
        if ( !matched )
        {
            for ( size_t liveIndex = 0; liveIndex < m_liveBarrierRecords.size(); ++liveIndex )
            {
                const LiveBarrierRecordDX12& live = m_liveBarrierRecords[liveIndex];
                if ( !liveBarrierMatched[liveIndex] && live.before == graphBefore && live.after == graphAfter )
                {
                    liveBarrierMatched[liveIndex] = true;
                    matched = true;
                    ++matchedStateOnlyPairs;
                    break;
                }
            }
        }

        if ( !matched && graphOnlyDetails < MAX_COMPARISON_DETAILS )
        {
            out << "  graph_only before pass [" << transition.passIndex << "] " << passDesc.name
                << ": " << resource.name
                << " " << ToString( transition.before ) << "/" << Dx12StateToString( graphBefore )
                << " -> " << ToString( transition.after ) << "/" << Dx12StateToString( graphAfter ) << "\n";
            ++graphOnlyDetails;
        }
    }

    size_t liveOnlyDetails = 0;
    for ( size_t liveIndex = 0; liveIndex < m_liveBarrierRecords.size(); ++liveIndex )
    {
        if ( liveBarrierMatched[liveIndex] )
        {
            continue;
        }
        const LiveBarrierRecordDX12& live = m_liveBarrierRecords[liveIndex];
        if ( liveOnlyDetails < MAX_COMPARISON_DETAILS )
        {
            const char* resourceLabel = liveResourceLabel( live.resource );
            out << "  live_only [" << liveIndex << "] source=" << ( live.source ? live.source : "unknown" )
                << " resource=" << live.resource
                << " label=" << ( resourceLabel ? resourceLabel : "unlabeled" )
                << " " << Dx12StateToString( live.before )
                << " -> " << Dx12StateToString( live.after ) << "\n";
        }
        ++liveOnlyDetails;
    }

    out << "  matched_resource_state_pairs=" << matchedResourcePairs << "\n";
    out << "  matched_state_only_pairs=" << matchedStateOnlyPairs << "\n";
    out << "  graph_handoff_transition_count=" << graphHandoffTransitionCount << "\n";
    out << "  unknown_graph_transition_count=" << unknownGraphTransitions << "\n";
    out << "  graph_only_detail_count=" << graphOnlyDetails << "\n";
    out << "  live_only_count=" << liveOnlyDetails << "\n";
    out << "  note=Resource-labeled matches are stronger than state-only matches. Unlabeled live resources remain telemetry, not proof; PRESENT and COMMON share a DX12 value. Handoff transitions are reviewed declarations only; live DX12 barriers still own execution.\n";

    const std::string dump = out.str();
    {
        std::ofstream file( "Debug/dx12_frame_graph_skeleton.txt", std::ios::binary );
        if ( file.is_open() )
        {
            file << dump << "\n";
        }
    }
    Log().Writef( "Debug/dx12_frame_graph_skeleton.txt", "%s\n", dump.c_str() );
    Log().FlushAll();
}


void RenderBackendDX12::ReportDeviceLost( const char* context, HRESULT result ) const
{
    const HRESULT removedReason = m_device ? m_device->GetDeviceRemovedReason() : result;
    Log().WriteEventf( "dx12_device_lost context=%s result=0x%08lX removed_reason=0x%08lX",
                       context ? context : "unknown",
                       static_cast<unsigned long>( result ),
                       static_cast<unsigned long>( removedReason ) );

    FILE* fp = nullptr;
    fopen_s( &fp, "dx12_device_lost.txt", "a" );
    if ( fp )
    {
        fprintf( fp,
                 "context=%s result=0x%08lX removed_reason=0x%08lX\n",
                 context ? context : "unknown",
                 static_cast<unsigned long>( result ),
                 static_cast<unsigned long>( removedReason ) );
    }

    if ( m_device )
    {
        ID3D12DeviceRemovedExtendedData* dred = nullptr;
        if ( SUCCEEDED( m_device->QueryInterface( IID_PPV_ARGS( &dred ) ) ) )
        {
            D3D12_DRED_AUTO_BREADCRUMBS_OUTPUT breadcrumbs = {};
            D3D12_DRED_PAGE_FAULT_OUTPUT pageFault = {};
            const HRESULT breadcrumbResult = dred->GetAutoBreadcrumbsOutput( &breadcrumbs );
            const HRESULT pageFaultResult = dred->GetPageFaultAllocationOutput( &pageFault );

            Log().WriteEventf( "dx12_dred context=%s breadcrumbs_hr=0x%08lX breadcrumbs_head=%p page_fault_hr=0x%08lX page_fault_va=0x%llX existing_allocations=%p recent_freed_allocations=%p",
                               context ? context : "unknown",
                               static_cast<unsigned long>( breadcrumbResult ),
                               breadcrumbs.pHeadAutoBreadcrumbNode,
                               static_cast<unsigned long>( pageFaultResult ),
                               static_cast<unsigned long long>( pageFault.PageFaultVA ),
                               pageFault.pHeadExistingAllocationNode,
                               pageFault.pHeadRecentFreedAllocationNode );

            if ( fp )
            {
                fprintf( fp,
                         "dred breadcrumbs_hr=0x%08lX breadcrumbs_head=%p page_fault_hr=0x%08lX page_fault_va=0x%llX existing_allocations=%p recent_freed_allocations=%p\n",
                         static_cast<unsigned long>( breadcrumbResult ),
                         breadcrumbs.pHeadAutoBreadcrumbNode,
                         static_cast<unsigned long>( pageFaultResult ),
                         static_cast<unsigned long long>( pageFault.PageFaultVA ),
                         pageFault.pHeadExistingAllocationNode,
                         pageFault.pHeadRecentFreedAllocationNode );
            }
            dred->Release();
        }
        else if ( fp )
        {
            fprintf( fp, "dred unavailable\n" );
        }
    }

    if ( fp )
    {
        fprintf( fp, "---\n" );
        fclose( fp );
    }
}


D3D12_GPU_VIRTUAL_ADDRESS RenderBackendDX12::SubAllocateUpload( UINT64 size, UINT64 alignment )
{
    return m_uploadSystem.Allocate( m_allocatorIndex, size, alignment );
}


D3D12_GPU_VIRTUAL_ADDRESS RenderBackendDX12::ReserveUpload( UINT64 size, UINT64 alignment )
{
    // Upload allocation has two inseparable parts:
    //
    // 1. Ask whether the current frame arena has enough aligned space.
    // 2. If not, submit/wait/reset before handing out bytes.
    //
    // Keeping that pair in one helper prevents the old failure mode where one
    // caller probes with one alignment, allocates with another, or skips the
    // probe entirely. The alignment matters because DX12 constant buffers,
    // texture rows, and vertex data can each require different byte boundaries.
    FlushUploadBufferIfNeeded( size, alignment );
    return SubAllocateUpload( size, alignment );
}


uint8_t* RenderBackendDX12::GetUploadPtr( D3D12_GPU_VIRTUAL_ADDRESS addr )
{
    return m_uploadSystem.GetMappedPtr( m_allocatorIndex, addr );
}


UINT RenderBackendDX12::AllocateStaticSRV()
{
    return m_srvDescriptors.AllocateStatic();
}


UINT RenderBackendDX12::AllocateTransientSRV()
{
    return m_srvDescriptors.AllocateTransient();
}


UINT RenderBackendDX12::AllocateTransientSRVRange( UINT count )
{
    return m_srvDescriptors.AllocateTransientRange( count );
}


D3D12_CPU_DESCRIPTOR_HANDLE RenderBackendDX12::GetSRVStagingCpuHandle( UINT index )
{
    return m_srvDescriptors.StagingCpuHandle( index );
}


D3D12_GPU_DESCRIPTOR_HANDLE RenderBackendDX12::GetSRVGpuHandle( UINT index )
{
    return m_srvDescriptors.ShaderVisibleGpuHandle( index );
}


D3D12_CPU_DESCRIPTOR_HANDLE RenderBackendDX12::GetRTVHandle( UINT index )
{
    return m_rtvDescriptors.CpuHandle( index );
}


D3D12_CPU_DESCRIPTOR_HANDLE RenderBackendDX12::GetDSVHandle( UINT index )
{
    return m_dsvDescriptors.CpuHandle( index );
}


// --- Init / Shutdown ---


bool RenderBackendDX12::Init( HWND hwnd, HDC /*hdc*/, int width, int height )
{
    s_instance = this;
    m_width = width;
    m_height = height;

    Dx12RenderDeviceInitDesc deviceDesc;
    deviceDesc.hwnd = hwnd;
    deviceDesc.width = static_cast<UINT>( width );
    deviceDesc.height = static_cast<UINT>( height );
    deviceDesc.frameCount = FRAME_COUNT;
    deviceDesc.backBufferFormat = DXGI_FORMAT_R8G8B8A8_UNORM;
    m_renderDevice.Init( deviceDesc );

    // The render device now owns the DXGI/D3D12 platform objects: factory,
    // device, graphics queue, swap chain, command allocators, command list, and
    // frame fence. RenderBackendDX12 still acts as the IRenderBackend facade,
    // so it borrows raw pointers from the device layer while the rest of the
    // renderer is migrated in small slices.
    m_factory = m_renderDevice.Factory();
    m_swapChain = m_renderDevice.SwapChain();
    m_device = m_renderDevice.Device();
    m_commandQueue = m_renderDevice.GraphicsQueue();
    m_commandList = m_renderDevice.CommandList();
    for ( int i = 0; i < FRAME_COUNT; ++i )
    {
        m_commandAllocators[i] = m_renderDevice.CommandAllocator( static_cast<UINT>( i ) );
        m_frameFenceValues[i] = 0;
    }
    m_commandListOpen = false;
    m_allocatorIndex = m_renderDevice.AllocatorIndex();
    m_frameIndex = m_renderDevice.FrameIndex();
    m_allowTearing = m_renderDevice.AllowTearing();
    m_liveBarrierRecords.clear();

    // Check DXR capability
    CheckDXRSupport();

    // Descriptor heap mental model:
    //
    // The heap is a table. A descriptor is one row in that table. The actual
    // texture, depth buffer, or UAV texture is separate GPU memory.
    //
    // RTV rows are used when the GPU writes color pixels.
    // DSV rows are used when the GPU reads/writes depth and stencil.
    // SRV rows are used when shaders read textures or buffers.
    // UAV rows are used when compute/raytracing shaders write textures/buffers.
    //
    // The high-churn SRV/CBV/UAV heap has two copies of the same idea:
    //
    // - staging heap: CPU-only, stable descriptor templates created at load time,
    // - shader-visible heap: GPU-readable rows bound during draws/dispatches.
    //
    // The descriptor allocator below owns row assignment for that pair. It keeps
    // long-lived static rows separate from short-lived per-frame rows so the CPU
    // does not overwrite a row while an in-flight command list still points at it.

    // Concept: create the three descriptor tables used by this backend.
    //
    // A descriptor heap is storage for descriptor rows. The row describes a
    // resource; it does not own the resource. RTV and DSV rows are CPU-only
    // because the output-merger stage receives CPU descriptor handles directly.
    // SRV/CBV/UAV rows need a shader-visible heap because shaders follow GPU
    // descriptor handles at draw/dispatch time.
    //
    // This first heap holds RTV rows: swap-chain back buffers and FBO color
    // targets that the GPU can write color pixels into.
    // Docs: https://learn.microsoft.com/en-us/windows/win32/api/d3d12/nf-d3d12-id3d12device-createdescriptorheap
    {
        D3D12_DESCRIPTOR_HEAP_DESC desc = {};
        desc.NumDescriptors = MAX_RTV_DESCRIPTORS;
        desc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
        ThrowIfFailed( m_device->CreateDescriptorHeap( &desc, IID_PPV_ARGS( &m_rtvHeap ) ), "CreateDescriptorHeap (RTV) failed" );
        NameDx12Object( m_rtvHeap, L"Skullbonez DX12 RTV Heap" );
        m_rtvDescSize = m_device->GetDescriptorHandleIncrementSize( D3D12_DESCRIPTOR_HEAP_TYPE_RTV );
        m_rtvDescriptors.Init( m_rtvHeap, m_rtvDescSize, MAX_RTV_DESCRIPTORS, "RTV" );
    }
    // DSV rows describe depth/stencil targets: the main window depth buffer and
    // any off-screen depth buffers used by framebuffer passes.
    // Docs: https://learn.microsoft.com/en-us/windows/win32/api/d3d12/nf-d3d12-id3d12device-createdescriptorheap
    {
        D3D12_DESCRIPTOR_HEAP_DESC desc = {};
        desc.NumDescriptors = MAX_DSV_DESCRIPTORS;
        desc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_DSV;
        ThrowIfFailed( m_device->CreateDescriptorHeap( &desc, IID_PPV_ARGS( &m_dsvHeap ) ), "CreateDescriptorHeap (DSV) failed" );
        NameDx12Object( m_dsvHeap, L"Skullbonez DX12 DSV Heap" );
        m_dsvDescSize = m_device->GetDescriptorHandleIncrementSize( D3D12_DESCRIPTOR_HEAP_TYPE_DSV );
        m_dsvDescriptors.Init( m_dsvHeap, m_dsvDescSize, MAX_DSV_DESCRIPTORS, "DSV" );
    }
    // SRV/CBV/UAV rows are shader-visible. "Shader-visible" means the GPU can
    // index these rows directly when a shader samples a texture, reads a
    // constant buffer, or writes a UAV. Transient rows are partitioned per
    // in-flight frame allocator to avoid rewriting descriptor slots that queued
    // command lists may still reference.
    // Docs: https://learn.microsoft.com/en-us/windows/win32/api/d3d12/nf-d3d12-id3d12device-createdescriptorheap
    {
        D3D12_DESCRIPTOR_HEAP_DESC desc = {};
        desc.NumDescriptors = MAX_STATIC_SRVS + ( MAX_TRANSIENT_SRVS * FRAME_COUNT );
        desc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
        desc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
        ThrowIfFailed( m_device->CreateDescriptorHeap( &desc, IID_PPV_ARGS( &m_srvHeap ) ), "CreateDescriptorHeap (SRV) failed" );
        NameDx12Object( m_srvHeap, L"Skullbonez DX12 Shader Visible SRV Heap" );
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
        NameDx12Object( m_srvStagingHeap, L"Skullbonez DX12 SRV Staging Heap" );
    }
    // The descriptor allocator receives both heaps:
    //
    // - m_srvStagingHeap is CPU-only storage for persistent descriptor templates.
    // - m_srvHeap is shader-visible storage the GPU can read at draw time.
    //
    // Static descriptors occupy the first MAX_STATIC_SRVS rows. Temporary rows
    // come after that, split into one range per frame allocator so the CPU never
    // rewrites descriptors still referenced by an in-flight frame.
    m_srvDescriptors.Init( m_srvHeap, m_srvStagingHeap, m_srvDescSize, MAX_STATIC_SRVS, MAX_TRANSIENT_SRVS, FRAME_COUNT );
    m_srvDescriptors.ResetFrame( m_allocatorIndex );

    // Cleared ordinary-raster texture slots still need a real descriptor table.
    // BindTexture(0) maps to this typed null SRV so shaders that sample an
    // intentionally empty slot read safe zero/default values instead of whatever
    // descriptor was previously bound to the root parameter.
    m_nullTextureSRVIndex = AllocateStaticSRV();
    D3D12_SHADER_RESOURCE_VIEW_DESC nullTextureSrv = {};
    nullTextureSrv.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    nullTextureSrv.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
    nullTextureSrv.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    nullTextureSrv.Texture2D.MipLevels = 1;
    m_device->CreateShaderResourceView( nullptr, &nullTextureSrv, GetSRVStagingCpuHandle( m_nullTextureSRVIndex ) );

    // Lifetime: swap-chain images are replaced on resize, but the engine keeps
    // one stable RTV descriptor row per back buffer index. ResizeBuffers swaps
    // the image memory; CreateRenderTargetView overwrites the existing row with
    // a view record for the new image.
    // Docs: https://learn.microsoft.com/en-us/windows/win32/api/d3d12/nf-d3d12-id3d12device-createrendertargetview
    for ( int i = 0; i < FRAME_COUNT; ++i )
    {
        ThrowIfFailed( m_swapChain->GetBuffer( (UINT)i, IID_PPV_ARGS( &m_renderTargets[i] ) ), "SwapChain GetBuffer failed" );
        NameDx12ObjectIndexed( m_renderTargets[i], L"Skullbonez DX12 Swapchain Backbuffer", (UINT)i );
        // Reserve one stable RTV row for each swap-chain buffer. ResizeBuffers
        // replaces the back-buffer resources later, but the descriptor rows stay
        // the same and are simply overwritten with new view records.
        m_backBufferRTVs[i] = m_rtvDescriptors.Allocate().cpuHandle;
        m_device->CreateRenderTargetView( m_renderTargets[i], nullptr, m_backBufferRTVs[i] );
    }

    // Depth stencil
    CreateDepthStencil( width, height );

    // Create per-frame upload buffers — one per FRAME_COUNT allocator. Each holds CPU-writable,
    // GPU-readable memory for per-frame constant buffers, dynamic vertex buffers, and texture
    // uploads. Partitioned per-allocator so that frame N+1's CPU recording cannot overwrite data
    // that frame N's GPU is still reading (the per-allocator fence wait in EnsureCommandListOpen
    // guarantees frame N is done before we reuse that allocator's upload buffer on frame N+2).
    // Docs: https://learn.microsoft.com/en-us/windows/win32/api/d3d12/nf-d3d12-id3d12device-createcommittedresource
    // Dx12FrameUploadSystem owns the actual upload resources and their
    // persistent CPU Map() pointers. RenderBackendDX12 now asks for byte ranges
    // instead of owning the raw upload-buffer lifecycle itself.
    m_uploadSystem.Init( m_device, FRAME_COUNT, UPLOAD_BUFFER_SIZE, L"Skullbonez DX12 Frame Upload Buffer" );

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
            NameDx12Object( m_gpuTimers.queryHeap, L"Skullbonez DX12 GPU Timer Query Heap" );
            // Readback buffer — CPU-readable memory where GPU timer results are copied to.
            // The READBACK heap type means the CPU can read from it (but the GPU cannot render to it).
            // Docs: https://learn.microsoft.com/en-us/windows/win32/api/d3d12/nf-d3d12-id3d12device-createcommittedresource
            const UINT64 timerReadbackBytes = static_cast<UINT64>( TIMER_HEAP_SIZE ) * sizeof( uint64_t );
            // Dx12ReadbackBuffer owns the CPU-readable resource. The backend
            // still decides when the fence is safe to read, but it no longer
            // carries the raw COM allocation/release path for timer bytes.
            // Buffers on all heap types are effectively created in COMMON state in D3D12
            // regardless of the specified initial state. For READBACK buffers the runtime
            // accepts any state but always uses COMMON — be explicit to keep the debug layer
            // quiet. CPU Map/Unmap access is independent of the GPU-visible resource state.
            // Docs: https://learn.microsoft.com/en-us/windows/win32/direct3d12/using-resource-barriers-to-synchronize-resource-states-in-direct3d-12#implicit-state-transitions
            if ( !m_gpuTimers.readback.InitBuffer( m_device, timerReadbackBytes, L"Skullbonez DX12 GPU Timer Readback Buffer" ) )
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
    m_currentRTV = m_backBufferRTVs[m_frameIndex];
    m_currentDSV = m_mainDSV;

    DumpFrameGraphSkeleton();

    return true;
}


void RenderBackendDX12::CreateRootSignature()
{
    // Root signature mental model:
    //
    // A shader cannot freely access arbitrary C++ variables or texture objects.
    // The root signature is the contract that says which small set of bindings
    // the command list may provide and which register names the HLSL shader will
    // use to find them.
    //
    // This renderer's main graphics root signature is deliberately simple:
    //
    // - root parameter 0: one constant buffer view at b0. Per-draw matrices,
    //   colors, and scalar shader values are uploaded there.
    // - root parameters 1..5: one descriptor table each for texture slots t0..t4.
    //   Each table points at one transient SRV descriptor row prepared by the
    //   descriptor allocator. Slot t4 is reserved for the object material table.
    // - static samplers: fixed filtering/addressing rules named s0, s1, and s3.
    //
    // The future render graph will not replace this shader contract. It will
    // decide when resources are safe to read/write and which pass binds them.
    D3D12_DESCRIPTOR_RANGE1 srvRanges[TEXTURE_SLOT_COUNT] = {};
    for ( int slot = 0; slot < TEXTURE_SLOT_COUNT; ++slot )
    {
        srvRanges[slot].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
        srvRanges[slot].NumDescriptors = 1;
        srvRanges[slot].BaseShaderRegister = SHADER_REGISTER_FIRST_TEXTURE + static_cast<UINT>( slot );
        srvRanges[slot].RegisterSpace = 0;
        srvRanges[slot].OffsetInDescriptorsFromTableStart = 0;
    }

    D3D12_ROOT_PARAMETER1 params[ORDINARY_RASTER_ROOT_PARAMETER_COUNT] = {};
    params[ROOT_PARAMETER_FRAME_CONSTANTS].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
    params[ROOT_PARAMETER_FRAME_CONSTANTS].Descriptor.ShaderRegister = SHADER_REGISTER_FRAME_CONSTANTS;
    params[ROOT_PARAMETER_FRAME_CONSTANTS].Descriptor.RegisterSpace = 0;
    params[ROOT_PARAMETER_FRAME_CONSTANTS].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;

    for ( int slot = 0; slot < TEXTURE_SLOT_COUNT; ++slot )
    {
        const UINT rootParameter = ROOT_PARAMETER_FIRST_TEXTURE + static_cast<UINT>( slot );
        params[rootParameter].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
        params[rootParameter].DescriptorTable.NumDescriptorRanges = 1;
        params[rootParameter].DescriptorTable.pDescriptorRanges = &srvRanges[slot];
        params[rootParameter].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
    }

    D3D12_STATIC_SAMPLER_DESC samplers[3] = {};
    // s0: linear wrap (most textures — terrain, skybox, sphere)
    samplers[0].Filter = D3D12_FILTER_MIN_MAG_MIP_LINEAR;
    samplers[0].AddressU = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
    samplers[0].AddressV = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
    samplers[0].AddressW = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
    samplers[0].MaxAnisotropy = 1;
    samplers[0].ComparisonFunc = D3D12_COMPARISON_FUNC_NEVER;
    samplers[0].MaxLOD = D3D12_FLOAT32_MAX; // allow all mip levels (default 0 = mip 0 only!)
    samplers[0].ShaderRegister = SAMPLER_REGISTER_LINEAR_WRAP;
    samplers[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;

    // s1: linear clamp (FBO / reflection textures)
    samplers[1].Filter = D3D12_FILTER_MIN_MAG_MIP_LINEAR;
    samplers[1].AddressU = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
    samplers[1].AddressV = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
    samplers[1].AddressW = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
    samplers[1].MaxAnisotropy = 1;
    samplers[1].ComparisonFunc = D3D12_COMPARISON_FUNC_NEVER;
    samplers[1].MaxLOD = D3D12_FLOAT32_MAX;
    samplers[1].ShaderRegister = SAMPLER_REGISTER_LINEAR_CLAMP;
    samplers[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;

    // s3: point clamp for manual shadow-map PCF.
    samplers[2].Filter = D3D12_FILTER_MIN_MAG_MIP_POINT;
    samplers[2].AddressU = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
    samplers[2].AddressV = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
    samplers[2].AddressW = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
    samplers[2].MaxAnisotropy = 1;
    samplers[2].ComparisonFunc = D3D12_COMPARISON_FUNC_NEVER;
    samplers[2].MaxLOD = D3D12_FLOAT32_MAX;
    samplers[2].ShaderRegister = SAMPLER_REGISTER_SHADOW_POINT_CLAMP;
    samplers[2].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;

    D3D12_VERSIONED_ROOT_SIGNATURE_DESC rootSigDesc = {};
    rootSigDesc.Version = D3D_ROOT_SIGNATURE_VERSION_1_1;
    rootSigDesc.Desc_1_1.NumParameters = ORDINARY_RASTER_ROOT_PARAMETER_COUNT;
    rootSigDesc.Desc_1_1.pParameters = params;
    rootSigDesc.Desc_1_1.NumStaticSamplers = 3;
    rootSigDesc.Desc_1_1.pStaticSamplers = samplers;
    rootSigDesc.Desc_1_1.Flags = D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;

    // Serialize the root signature description into a binary blob. The root signature defines
    // what data shaders can access: [0] CBV at b0 (constants), [1..5] SRV
    // tables at t0..t4, plus static samplers for regular, FBO, and shadow reads.
    // Docs: https://learn.microsoft.com/en-us/windows/win32/api/d3d12/nf-d3d12-d3d12serializeversionedrootsignature
    ComPtr<ID3DBlob> signature;
    ComPtr<ID3DBlob> error;
    if ( FAILED( D3D12SerializeVersionedRootSignature( &rootSigDesc, signature.GetAddressOf(), error.GetAddressOf() ) ) )
    {
        std::string msg = "Root signature serialization failed";
        if ( error )
        {
            msg += ": ";
            msg += (const char*)error->GetBufferPointer();
        }
        throw std::runtime_error( msg );
    }

    // Create the Root Signature object from the serialized blob. This is the "contract" between
    // the application and shaders — it defines the layout of all shader-visible parameters.
    // Every PSO must reference a root signature, and every draw call must bind matching data.
    // Docs: https://learn.microsoft.com/en-us/windows/win32/api/d3d12/nf-d3d12-id3d12device-createrootsignature
    if ( FAILED( m_device->CreateRootSignature( 0, signature->GetBufferPointer(), signature->GetBufferSize(), IID_PPV_ARGS( &m_rootSignature ) ) ) )
    {
        throw std::runtime_error( "CreateRootSignature failed" );
    }
    NameDx12Object( m_rootSignature, L"Skullbonez DX12 Main Root Signature" );
#ifdef _DEBUG
    Log().WriteEventf( "dx12_ordinary_raster_binding_abi root_parameters=%u cbv=b%u srv_slots=t%u..t%u material_table=t4 samplers=s%u,s%u,s%u bind_texture_slots=%d material_payload=packed_instance_params",
                       ORDINARY_RASTER_ROOT_PARAMETER_COUNT,
                       SHADER_REGISTER_FRAME_CONSTANTS,
                       SHADER_REGISTER_FIRST_TEXTURE,
                       SHADER_REGISTER_FIRST_TEXTURE + static_cast<UINT>( TEXTURE_SLOT_COUNT - 1 ),
                       SAMPLER_REGISTER_LINEAR_WRAP,
                       SAMPLER_REGISTER_LINEAR_CLAMP,
                       SAMPLER_REGISTER_SHADOW_POINT_CLAMP,
                       TEXTURE_SLOT_COUNT );
#endif
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
    NameDx12Object( m_depthStencil, L"Skullbonez DX12 Main Depth Stencil" );

    D3D12_DEPTH_STENCIL_VIEW_DESC dsvDesc = {};
    dsvDesc.Format = DXGI_FORMAT_D24_UNORM_S8_UINT;
    dsvDesc.ViewDimension = D3D12_DSV_DIMENSION_TEXTURE2D;
    if ( m_mainDSV.ptr == 0 )
    {
        // The main depth buffer is recreated on resize, but it is always the
        // same engine concept: "the window depth target." Allocate its DSV row
        // once, then overwrite that row with the new resource view whenever the
        // texture is recreated.
        m_mainDSV = m_dsvDescriptors.Allocate().cpuHandle;
    }
    // Create a Depth Stencil View for the main depth buffer so it can be bound as the depth target.
    // Docs: https://learn.microsoft.com/en-us/windows/win32/api/d3d12/nf-d3d12-id3d12device-createdepthstencilview
    m_device->CreateDepthStencilView( m_depthStencil, &dsvDesc, m_mainDSV );
}


void RenderBackendDX12::Shutdown()
{
    if ( !m_device )
    {
        // Partial initialisation can fail inside Dx12RenderDevice before the
        // backend has copied borrowed aliases such as m_device. Even in that
        // state, the device owner may already hold factory/device/queue/swap
        // chain objects, so always give it a chance to release them.
        m_renderDevice.Shutdown();
        m_factory = nullptr;
        m_swapChain = nullptr;
        m_commandQueue = nullptr;
        m_commandList = nullptr;
        for ( int i = 0; i < FRAME_COUNT; ++i )
        {
            m_commandAllocators[i] = nullptr;
            m_frameFenceValues[i] = 0;
        }
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
        AssertPlatformProfilerGpuStackClosed( "Shutdown" );
        m_commandList->Close();
        m_commandListOpen = false;
        ID3D12CommandList* ppCLs[] = { m_commandList };
        m_commandQueue->ExecuteCommandLists( 1, ppCLs );
    }

    // Wait for all GPU work to complete (command queue + pending presents).
    WaitForGpu();

    // Drain the DXGI flip queue. DX12's WaitForGpu only waits on the command queue fence,
    // but DXGI's flip-model present queue is separate. Without draining it, DWM may still
    // hold references to this swap chain's backbuffers after Release(), delaying the
    // window/compositor surface cleanup.
    // Present an empty frame with sync-interval 0 to flush the flip queue, then wait again.
    if ( m_swapChain )
    {
        m_swapChain->Present( 0, 0 );
        WaitForGpu();
    }

    // DXR resources hang off newer D3D12 interfaces and contain GPU-side
    // acceleration structures. Release them before the shared renderer objects
    // below so no raytracing object outlives the device/command-list aliases it
    // was created from.
    ShutdownDXR();

    ReportArchitectureStats( "Shutdown" );
    DumpFrameGraphSkeleton();

    // GPU timer cleanup
    m_gpuTimers.readback.Reset();
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

    // Cached PSOs are backend-owned COM objects. They are shared across draws
    // while the backend lives, then released as one cache at shutdown.
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

    m_uploadSystem.Shutdown();
    if ( m_depthStencil )
    {
        m_depthStencil->Release();
    }
    if ( m_rootSignature )
    {
        m_rootSignature->Release();
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
    m_srvDescriptors.Reset();
    m_nullTextureSRVIndex = UINT_MAX;
    if ( m_dsvHeap )
    {
        m_dsvHeap->Release();
        m_dsvHeap = nullptr;
    }
    m_dsvDescriptors.Reset();
    m_mainDSV = {};
    if ( m_rtvHeap )
    {
        m_rtvHeap->Release();
        m_rtvHeap = nullptr;
    }
    m_rtvDescriptors.Reset();
    for ( int i = 0; i < FRAME_COUNT; ++i )
    {
        m_backBufferRTVs[i] = {};
    }
    m_renderDevice.Shutdown();
    m_factory = nullptr;
    m_swapChain = nullptr;
    m_device = nullptr;
    m_commandQueue = nullptr;
    m_commandList = nullptr;
    for ( int i = 0; i < FRAME_COUNT; ++i )
    {
        m_commandAllocators[i] = nullptr;
        m_frameFenceValues[i] = 0;
    }
    m_commandListOpen = false;
    m_allocatorIndex = 0;
    m_frameIndex = 0;
    m_allowTearing = false;

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
            m_commandList->ResolveQueryData( m_gpuTimers.queryHeap, D3D12_QUERY_TYPE_TIMESTAMP, (UINT)start, (UINT)( i - start ), m_gpuTimers.readback.Resource(), byteOffset );
            resolvedTimerSlotsThisFrame = true;
        }
        std::memset( m_gpuTimers.slotWritten, 0, sizeof( m_gpuTimers.slotWritten ) ); // reset for next frame
    }

    TransitionBarrier( m_renderTargets[m_frameIndex], D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_PRESENT );
    m_backBufferIsRT = false;

    // Close the command list — finalizes the recorded commands. A closed command list can be
    // submitted to the GPU. No more commands can be recorded until Reset is called.
    // Docs: https://learn.microsoft.com/en-us/windows/win32/api/d3d12/nf-d3d12-id3d12graphicscommandlist-close
    AssertPlatformProfilerGpuStackClosed( "Present" );
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
    const HRESULT presentResult = m_swapChain->Present( syncInterval, presentFlags );
    if ( IsDx12DeviceLostResult( presentResult ) )
    {
        ReportDeviceLost( "Present", presentResult );
        throw std::runtime_error( "DX12 device lost during Present; see dx12_device_lost.txt" );
    }
    ThrowIfFailed( presentResult, "SwapChain Present failed" );

    // Signal the fence with the current frame's value. When the GPU reaches
    // this point in its command stream, it updates the fence to that value.
    // Later, EnsureCommandListOpen asks the timeline helper whether this value
    // has completed before reusing this frame's command allocator, upload arena,
    // and transient descriptor range.
    m_frameFenceValues[m_allocatorIndex] = m_renderDevice.FrameFence().Signal();

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
        m_gpuTimers.readFenceValue = m_frameFenceValues[m_allocatorIndex];
    }

    // Advance to next frame's allocator and swap chain buffer.
    m_allocatorIndex = m_renderDevice.AdvanceAllocatorIndex();
    m_frameIndex = m_renderDevice.RefreshFrameIndexFromSwapChain();
    m_currentRTV = m_backBufferRTVs[m_frameIndex];

    // Charge allocator/upload/descriptor pacing to Present/VsyncWait instead of
    // letting the first render command of the next frame hit this wait mid-frame.
    const UINT64 nextFrameFenceValue = m_frameFenceValues[m_allocatorIndex];
    if ( nextFrameFenceValue > m_renderDevice.FrameFence().CompletedValue() )
    {
        m_renderDevice.FrameFence().WaitForValue( nextFrameFenceValue );
    }
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
        AssertPlatformProfilerGpuStackClosed( "Finish" );
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
        AssertPlatformProfilerGpuStackClosed( "FlushGPU" );
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
    const HRESULT resizeResult = m_swapChain->ResizeBuffers( FRAME_COUNT, (UINT)width, (UINT)height, DXGI_FORMAT_R8G8B8A8_UNORM, resizeFlags );
    if ( IsDx12DeviceLostResult( resizeResult ) )
    {
        ReportDeviceLost( "ResizeBuffers", resizeResult );
        throw std::runtime_error( "DX12 device lost during ResizeBuffers; see dx12_device_lost.txt" );
    }
    ThrowIfFailed( resizeResult, "SwapChain ResizeBuffers failed" );
    m_frameIndex = m_renderDevice.RefreshFrameIndexFromSwapChain();

    // ResizeBuffers puts all back buffers into PRESENT state — reset our tracking flag
    // so the next Clear() correctly transitions to RENDER_TARGET before use.
    m_backBufferIsRT = false;

    for ( int i = 0; i < FRAME_COUNT; ++i )
    {
        ThrowIfFailed( m_swapChain->GetBuffer( (UINT)i, IID_PPV_ARGS( &m_renderTargets[i] ) ), "SwapChain GetBuffer after resize failed" );
        NameDx12ObjectIndexed( m_renderTargets[i], L"Skullbonez DX12 Swapchain Backbuffer", (UINT)i );
        m_device->CreateRenderTargetView( m_renderTargets[i], nullptr, m_backBufferRTVs[i] );
    }

    CreateDepthStencil( width, height );

    m_width = width;
    m_height = height;
    m_viewport = { 0.0f, 0.0f, (float)width, (float)height, 0.0f, 1.0f };
    m_scissorRect = { 0, 0, (LONG)width, (LONG)height };
    m_currentRTV = m_backBufferRTVs[m_frameIndex];
    m_currentDSV = m_mainDSV;
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
    // Clip planes are handled through shader constants.
}


// --- PSO Management ---


// --- Resource Creation ---


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


// --- Screenshot ---


// --- Dynamic VB ---


// Draws per-vertex colored lines. data is interleaved [x,y,z,r,g,b] per vertex (6 floats each).
// Uses the shared upload buffer to stream vertex data and draws with LINE_LIST topology.
// Lazy-creates a LINE_LIST PSO on first call.


// --- Instanced mesh ---


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


bool RenderBackendDX12::IsDepthWriteEnabled() const
{
    return m_depthWriteEnabled;
}


bool RenderBackendDX12::IsBlendEnabled() const
{
    return m_blendEnabled;
}


void RenderBackendDX12::GetBlendFunc( BlendFactor& outSrc, BlendFactor& outDst ) const
{
    outSrc = m_blendSrc;
    outDst = m_blendDst;
}


// --- DXR Raytracing ---


// --- GPU Timers ---
