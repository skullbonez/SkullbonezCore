/*
File: SkullbonezSource/Rendering/DX12/RenderBackendDX12.cpp
Purpose:
  Implements the production DX12 renderer and its frame, resource, and pipeline state.

Mental model:
  RenderBackendDX12.cpp implements the production DX12 renderer and its frame,
  resource, and pipeline state. As an implementation unit, keep edits anchored
  on DX12 ownership, descriptors, resources, and command submission and on the
  glossary/invariants below.

Glossary:
  Recording epoch: Logical open/closed state of the reusable command list,
  committed only after successful Close or Reset.
  Sticky failure: First active command-path failure retained until a new device
  initialization establishes a fresh command-list lifetime.
  DXR (DirectX Raytracing): DX12 API used for hardware ray traversal and
  reflection dispatch.
  RTV (Render Target View): Descriptor row used when the GPU writes color
  pixels into a texture or back buffer.
  DSV (Depth Stencil View): Descriptor row used when the GPU reads or writes
  depth/stencil data for depth testing.
  SRV (Shader Resource View): Descriptor row used when shaders read textures
  or buffers.
  UAV (Unordered Access View): Descriptor row used when compute or raytracing
  shaders write textures or buffers.
  CBV (Constant Buffer View): Descriptor row used when shaders read a packed
  block of constants.
  PSO (Pipeline State Object): Precompiled bundle of shaders and fixed render
  state that DX12 binds before drawing or dispatching.
  COM (Component Object Model): Windows interface lifetime model used by DX12
  through reference-counted objects.
  Descriptor: Small binding record that tells a renderer how to interpret a
  resource.
  Back buffer: Swap-chain image that will be presented to the window.

Invariants:
  - DX12 object lifetime, resource states, descriptor rows, and fence ordering
  must stay explicit.
  - No active-frame command records, submits, or reuses allocator/upload memory
    after the recording epoch latches a failure.

Related:
  - SkullbonezSource/Rendering/DX12/RenderBackendDX12.h
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
//     DX12 backend helpers emit these transitions from explicit before/after
//     access states so pass code names intent without hand-coding D3D12 barrier
//     structs at every call site.
//
#include "RenderBackendDX12.h"
#include "ShaderDX12.h"
#include "MeshDX12.h"
#include "FramebufferDX12.h"
#include "../RenderGraph.h"
#include "Dx12RenderGraphExecutor.h"
#include "../../Core/Log.h"
#include "../../Core/PlatformProfiler.h"
#include "../../Core/FatalError.h"
#include <cstdio>
#include <algorithm>
#include <string>
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

static inline SkullbonezCore::Basics::SbResult Dx12BackendInitResult( HRESULT hr, const char* msg )
{
    if ( FAILED( hr ) )
    {
        // Lane R: renderer startup depends on the adapter, driver, window, and
        // available descriptor resources. Return a bounded owner/message so the
        // process bootstrap can report the environment failure cleanly.
        return SkullbonezCore::Basics::SbResult::Failure( "Rendering/DX12",
                                                          "%s (HRESULT 0x%08X)",
                                                          msg ? msg : "DX12 backend startup call failed",
                                                          static_cast<unsigned int>( hr ) );
    }
    return SkullbonezCore::Basics::SbResult::Success();
}

static inline SkullbonezCore::Basics::SbResult Dx12BackendOperationResult( HRESULT hr, const char* msg )
{
    if ( FAILED( hr ) )
    {
        // Lane R: runtime presentation, resize, and render-target creation
        // depend on the active adapter/driver/window state. Report the device
        // operation that failed instead of escaping through exception unwinding.
        return SkullbonezCore::Basics::SbResult::Failure( "Rendering/DX12",
                                                          "%s (HRESULT 0x%08X)",
                                                          msg ? msg : "DX12 backend operation failed",
                                                          static_cast<unsigned int>( hr ) );
    }
    return SkullbonezCore::Basics::SbResult::Success();
}

static bool IsDx12DeviceLostResult( HRESULT hr )
{
    return hr == DXGI_ERROR_DEVICE_REMOVED || hr == DXGI_ERROR_DEVICE_RESET || hr == DXGI_ERROR_DRIVER_INTERNAL_ERROR;
}

// --- Backend Setup Entry Point ---


RenderBackendDX12::RenderBackendDX12()
{
}


RenderMemoryStats RenderBackendDX12::GetRenderMemoryStats() const
{
    // Concept: this snapshot mixes engine-owned cache counters with DXGI's
    // adapter-memory counters. The engine counters identify which renderer
    // tables are growing; the DXGI counters say whether the graphics kernel is
    // charging local or non-local video memory to this process.
    RenderMemoryStats stats;
    strcpy_s( stats.backendName, sizeof( stats.backendName ), "DirectX 12" );
    stats.available = Device() != nullptr;
    if ( !stats.available )
    {
        return stats;
    }

    const Dx12CpuDescriptorAllocatorStats rtvStats = m_rtvDescriptors.GetStats();
    const Dx12CpuDescriptorAllocatorStats dsvStats = m_dsvDescriptors.GetStats();
    const Dx12DescriptorAllocatorStats srvStats = m_srvDescriptors.GetStats();
    stats.rtvDescriptorsUsed = rtvStats.used;
    stats.rtvDescriptorsCapacity = rtvStats.capacity;
    stats.dsvDescriptorsUsed = dsvStats.used;
    stats.dsvDescriptorsCapacity = dsvStats.capacity;
    stats.srvStaticDescriptorsUsed = srvStats.staticUsed;
    stats.srvStaticDescriptorsCapacity = srvStats.staticCapacity;
    stats.srvTransientDescriptorsUsedThisFrame = srvStats.transientUsedThisFrame;
    stats.srvTransientDescriptorsCapacityPerFrame = srvStats.transientCapacityPerFrame;
    stats.srvTransientDescriptorsPeakThisRun = srvStats.transientPeakThisRun;

    for ( int frameIndex = 0; frameIndex < FRAME_COUNT; ++frameIndex )
    {
        const Dx12UploadArenaStats uploadStats = m_uploadSystem.GetStats( static_cast<UINT>( frameIndex ) );
        stats.uploadCapacityBytes += uploadStats.capacityBytes;
        stats.uploadUsedBytes += uploadStats.usedBytes;
        stats.uploadPeakBytes += uploadStats.peakBytes;
    }

    const Dx12ReadbackBufferStats timerReadbackStats = m_gpuTimers.readback.GetStats();
    if ( timerReadbackStats.ready )
    {
        stats.timerReadbackBytes = timerReadbackStats.sizeBytes;
    }

    stats.textureRegistryCount = m_textures.size();
    stats.textureRegistryCapacity = m_textures.capacity();
    stats.dynamicVertexBufferCount = m_dynamicVBs.size();
    stats.dynamicVertexBufferCapacity = m_dynamicVBs.capacity();
    stats.instancedMeshCount = m_instancedMeshes.size();
    stats.instancedMeshCapacity = m_instancedMeshes.capacity();
    stats.psoCacheCount = m_psoCacheCount;
    stats.graphTransientCount = m_graphTransientResources.size();
    stats.graphTransientCapacity = m_graphTransientResources.capacity();

    if ( m_factory )
    {
        // Why: multi-GPU machines can expose several adapters. Match the
        // device LUID instead of sampling adapter 0 so stress logs describe the
        // GPU actually backing this DX12 device.
        const LUID deviceLuid = Device()->GetAdapterLuid();
        ComPtr<IDXGIAdapter3> activeAdapter;
        for ( UINT adapterIndex = 0;; ++adapterIndex )
        {
            ComPtr<IDXGIAdapter1> adapter;
            const HRESULT enumResult = m_factory->EnumAdapters1( adapterIndex, adapter.GetAddressOf() );
            if ( enumResult == DXGI_ERROR_NOT_FOUND )
            {
                break;
            }
            if ( FAILED( enumResult ) )
            {
                continue;
            }

            DXGI_ADAPTER_DESC1 desc = {};
            if ( FAILED( adapter->GetDesc1( &desc ) ) || desc.AdapterLuid.HighPart != deviceLuid.HighPart ||
                 desc.AdapterLuid.LowPart != deviceLuid.LowPart )
            {
                continue;
            }

            (void)adapter.As( &activeAdapter );
            break;
        }

        if ( activeAdapter )
        {
            DXGI_QUERY_VIDEO_MEMORY_INFO localInfo = {};
            DXGI_QUERY_VIDEO_MEMORY_INFO nonLocalInfo = {};
            const bool localAvailable =
                SUCCEEDED( activeAdapter->QueryVideoMemoryInfo( 0, DXGI_MEMORY_SEGMENT_GROUP_LOCAL, &localInfo ) );
            const bool nonLocalAvailable = SUCCEEDED(
                activeAdapter->QueryVideoMemoryInfo( 0, DXGI_MEMORY_SEGMENT_GROUP_NON_LOCAL, &nonLocalInfo ) );
            stats.adapterMemoryAvailable = localAvailable || nonLocalAvailable;
            if ( localAvailable )
            {
                stats.localBudgetBytes = static_cast<uint64_t>( localInfo.Budget );
                stats.localCurrentUsageBytes = static_cast<uint64_t>( localInfo.CurrentUsage );
                stats.localCurrentReservationBytes = static_cast<uint64_t>( localInfo.CurrentReservation );
                stats.localAvailableForReservationBytes = static_cast<uint64_t>( localInfo.AvailableForReservation );
            }
            if ( nonLocalAvailable )
            {
                stats.nonLocalBudgetBytes = static_cast<uint64_t>( nonLocalInfo.Budget );
                stats.nonLocalCurrentUsageBytes = static_cast<uint64_t>( nonLocalInfo.CurrentUsage );
                stats.nonLocalCurrentReservationBytes = static_cast<uint64_t>( nonLocalInfo.CurrentReservation );
                stats.nonLocalAvailableForReservationBytes =
                    static_cast<uint64_t>( nonLocalInfo.AvailableForReservation );
            }
        }
    }

    return stats;
}


// --- Helpers ---


SkullbonezCore::Basics::SbResult RenderBackendDX12::WaitForGpu()
{
    if ( !m_renderDevice.FrameFence().IsReady() )
    {
        if ( m_submittedWork.HasSubmittedWork() )
        {
            const SkullbonezCore::Basics::SbResult unavailable = SkullbonezCore::Basics::SbResult::Failure(
                "Rendering/DX12",
                "DX12 fence timeline unavailable while submitted GPU work remains unproven." );
            m_submittedWork.CommitWait( unavailable, 0 );
            return unavailable;
        }
        ReleaseCompletedDeferredResources( !m_commandRecording.IsOpen() );
        return SkullbonezCore::Basics::SbResult::Success();
    }

    // Tell the GPU to mark the next fence value after all already-submitted
    // queue work, then block until that value is complete. In plain terms:
    // WaitForGpu() means "do not let the CPU continue until the GPU has caught
    // up to every command we submitted so far."
    UINT64 drainFenceValue = 0;
    const SkullbonezCore::Basics::SbResult signalResult = m_renderDevice.FrameFence().Signal( drainFenceValue );
    m_submittedWork.CommitSignal( signalResult, drainFenceValue );
    if ( !signalResult.ok )
    {
        return signalResult;
    }

    const SkullbonezCore::Basics::SbResult waitResult = m_renderDevice.FrameFence().WaitForValue( drainFenceValue );
    m_submittedWork.CommitWait( waitResult, drainFenceValue );
    if ( !waitResult.ok )
    {
        return waitResult;
    }
    if ( m_submittedWork.HasSubmittedWork() )
    {
        // Lane F: a successful wait for the covering drain fence must retire
        // every submission tracked before that signal.
        SB_FATAL( "RenderBackendDX12",
                  "DX12 submitted-work state remained live after successful full drain. fence=%llu",
                  static_cast<unsigned long long>( drainFenceValue ) );
    }

    // After full GPU wait, all frame fences are implicitly completed
    for ( int i = 0; i < FRAME_COUNT; ++i )
    {
        m_frameFenceValues[i] = 0;
    }

    ReleaseCompletedDeferredResources( !m_commandRecording.IsOpen() );
    return SkullbonezCore::Basics::SbResult::Success();
}


void RenderBackendDX12::SubmitClosedCommandList()
{
    if ( !CommandList() || !m_commandQueue || !m_commandRecording.IsClosed() || m_commandRecording.HasFailure() )
    {
        // Lane F: queue submission without a successfully closed recording
        // epoch would make all later fence/resource reasoning meaningless.
        SB_FATAL( "RenderBackendDX12",
                  "DX12 submission requires a healthy closed command list. commandList=%p queue=%p closed=%d failed=%d",
                  static_cast<const void*>( CommandList() ),
                  static_cast<const void*>( m_commandQueue ),
                  m_commandRecording.IsClosed() ? 1 : 0,
                  m_commandRecording.HasFailure() ? 1 : 0 );
    }

    ID3D12CommandList* commandLists[] = { CommandList() };
    m_commandQueue->ExecuteCommandLists( 1, commandLists );
    // ExecuteCommandLists has no HRESULT. Treat the work as live immediately;
    // only a later covering fence observation may clear it.
    m_submittedWork.MarkSubmitted();
}


void RenderBackendDX12::AssignDeferredResourceReleaseFence( UINT64 fenceValue )
{
    if ( fenceValue == 0 )
    {
        return;
    }

    for ( DeferredResourceReleaseDX12& retired : m_deferredResourceReleases )
    {
        if ( retired.resource && !retired.fenceAssigned )
        {
            retired.fenceValue = fenceValue;
            retired.fenceAssigned = true;
        }
    }
}


void RenderBackendDX12::ReleaseCompletedDeferredResources( bool releaseUnfenced )
{
    const bool fenceReady = m_renderDevice.FrameFence().IsReady();
    const UINT64 completedFence = fenceReady ? m_renderDevice.FrameFence().CompletedValue() : 0;
    if ( fenceReady )
    {
        m_submittedWork.ObserveCompletedFence( completedFence );
    }
    if ( m_deferredResourceReleases.empty() )
    {
        return;
    }

    const bool canReleaseUnfenced = releaseUnfenced && m_submittedWork.CanReleaseWithoutFence();
    size_t writeIndex = 0;
    for ( size_t readIndex = 0; readIndex < m_deferredResourceReleases.size(); ++readIndex )
    {
        DeferredResourceReleaseDX12& retired = m_deferredResourceReleases[readIndex];
        const bool canRelease = retired.resource == nullptr || canReleaseUnfenced ||
                                ( retired.fenceAssigned && fenceReady && retired.fenceValue <= completedFence );
        if ( canRelease )
        {
            if ( retired.resource )
            {
                retired.resource->Release();
                retired.resource = nullptr;
            }
            continue;
        }

        if ( writeIndex != readIndex )
        {
            m_deferredResourceReleases[writeIndex] = retired;
        }
        ++writeIndex;
    }

    m_deferredResourceReleases.resize( writeIndex );
}


void RenderBackendDX12::RetireResource( ID3D12Resource* resource )
{
    if ( !resource )
    {
        return;
    }

    const bool fenceReady = m_renderDevice.FrameFence().IsReady();
    if ( fenceReady )
    {
        m_submittedWork.ObserveCompletedFence( m_renderDevice.FrameFence().CompletedValue() );
    }

    if ( ( !Device() || !fenceReady ) && !m_commandRecording.IsOpen() && m_submittedWork.CanReleaseWithoutFence() )
    {
        resource->Release();
        return;
    }

    const UINT64 completedFence = fenceReady ? m_renderDevice.FrameFence().CompletedValue() : 0;
    bool hasOutstandingFrameWork = false;
    for ( int frame = 0; frame < FRAME_COUNT; ++frame )
    {
        if ( m_frameFenceValues[frame] > completedFence )
        {
            hasOutstandingFrameWork = true;
            break;
        }
    }
    if ( !m_commandRecording.IsOpen() && !hasOutstandingFrameWork && m_submittedWork.CanReleaseWithoutFence() )
    {
        resource->Release();
        return;
    }

    // Lifetime: D3D12 command lists record references to resource objects, but
    // execution is asynchronous. Keep the caller's COM reference alive until a
    // later fence or full GPU drain proves every submitted command stream that
    // could mention this resource has finished. A closed command list alone is
    // not proof: m_submittedWork may still be unfenced or completion-uncertain.
    DeferredResourceReleaseDX12 retired;
    retired.resource = resource;
    m_deferredResourceReleases.push_back( retired );
}


void RenderBackendDX12::AssertPlatformProfilerGpuStackClosed( const char* reason ) const
{
    if ( m_platformProfilerGpuDepth == 0 )
    {
        return;
    }

    Log().WriteEventf( "dx12_platform_profiler_open_stack_on_submit reason=%s depth=%d",
                       reason ? reason : "unknown",
                       m_platformProfilerGpuDepth );
    assert( m_platformProfilerGpuDepth == 0 );
    SB_FATAL( "RenderBackendDX12",
              "DX12 platform profiler GPU stack left open before command submission. reason=%s depth=%d",
              reason ? reason : "unknown",
              m_platformProfilerGpuDepth );
}


SkullbonezCore::Basics::SbResult RenderBackendDX12::EnsureCommandListOpen()
{
    if ( m_commandRecording.HasFailure() )
    {
        return m_commandRecording.CurrentResult();
    }

    if ( !CommandList() || !m_commandQueue || !m_renderDevice.FrameFence().IsReady() ||
         !m_commandAllocators[m_allocatorIndex] )
    {
        SB_FATAL( "RenderBackendDX12",
                  "DX12 backend is not fully initialised. allocatorIndex=%u commandList=%p commandQueue=%p",
                  m_allocatorIndex,
                  static_cast<const void*>( CommandList() ),
                  static_cast<const void*>( m_commandQueue ) );
    }

    if ( m_commandRecording.IsOpen() )
    {
        return SkullbonezCore::Basics::SbResult::Success();
    }

    // Wait for the GPU to finish with this allocator's previous work.
    //
    // "Allocator" here is easy to misread. It is the command allocator: memory
    // for recorded GPU commands, not texture memory. This backend also ties the
    // upload arena and transient descriptor range to the same frame index. The
    // fence value proves all three pieces of temporary per-frame storage are no
    // longer being read by the GPU before we reset them.
    UINT64 completedFence = m_renderDevice.FrameFence().CompletedValue();
    m_submittedWork.ObserveCompletedFence( completedFence );
    if ( m_submittedWork.HasUnfencedOrUncertainWork() )
    {
        return m_commandRecording.RetainFailure( SkullbonezCore::Basics::SbResult::Failure(
            "Rendering/DX12",
            "Command allocator reuse blocked because submitted GPU work lacks a trustworthy completion fence." ) );
    }
    if ( m_frameFenceValues[m_allocatorIndex] > completedFence )
    {
        const UINT64 allocatorFence = m_frameFenceValues[m_allocatorIndex];
        const SkullbonezCore::Basics::SbResult waitResult = m_renderDevice.FrameFence().WaitForValue( allocatorFence );
        m_submittedWork.CommitWait( waitResult, allocatorFence );
        if ( !waitResult.ok )
        {
            Log().WriteEventf( "dx12_frame_allocator_wait_failed owner=%s message=%s",
                               waitResult.error.owner,
                               waitResult.error.message );
            return m_commandRecording.CommitWait( waitResult );
        }
    }
    ReleaseCompletedDeferredResources( false );

    // Reset the command allocator — frees all memory from previously recorded commands.
    // This is only safe because we waited for the GPU to finish with this allocator above.
    // Docs: https://learn.microsoft.com/en-us/windows/win32/api/d3d12/nf-d3d12-id3d12commandallocator-reset
    SkullbonezCore::Basics::SbResult stateResult =
        m_commandRecording.CommitAllocatorReset( m_commandAllocators[m_allocatorIndex]->Reset(),
                                                 "EnsureCommandListOpen allocator Reset" );
    if ( !stateResult.ok )
    {
        return stateResult;
    }

    // Reset the command list to start recording new commands. The command list is reused
    // every frame — Reset puts it back into the "recording" state with a fresh allocator.
    // Docs: https://learn.microsoft.com/en-us/windows/win32/api/d3d12/nf-d3d12-id3d12graphicscommandlist-reset
    stateResult =
        m_commandRecording.CommitListReset( CommandList()->Reset( m_commandAllocators[m_allocatorIndex], nullptr ),
                                            "EnsureCommandListOpen list Reset" );
    if ( !stateResult.ok )
    {
        return stateResult;
    }

    // Bind the shader-visible descriptor heap — required before any draw calls that reference
    // textures or CBVs. Only ONE CBV/SRV/UAV heap can be bound at a time in DX12.
    // Docs:
    // https://learn.microsoft.com/en-us/windows/win32/api/d3d12/nf-d3d12-id3d12graphicscommandlist-setdescriptorheaps
    ID3D12DescriptorHeap* heaps[] = { m_srvHeap };
    CommandList()->SetDescriptorHeaps( 1, heaps );

    // Bind the root signature — tells the GPU the layout of shader parameters (where to find
    // constant buffers, texture descriptors, etc.). Must match what the PSO was created with.
    // Docs:
    // https://learn.microsoft.com/en-us/windows/win32/api/d3d12/nf-d3d12-id3d12graphicscommandlist-setgraphicsrootsignature
    CommandList()->SetGraphicsRootSignature( m_rootSignature );
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
    return SkullbonezCore::Basics::SbResult::Success();
}


bool RenderBackendDX12::ExecuteGraphTransition( const char* passName,
                                                const char* resourceName,
                                                ID3D12Resource* resource,
                                                RenderGraphResourceAccess before,
                                                RenderGraphResourceAccess after,
                                                UINT subresource )
{
    if ( !resource || before == after )
    {
        return true;
    }

    if ( !m_commandRecording.CanRecord() )
    {
        // Hazard: an explicit backend barrier can be the first command after Present()
        // or a mid-frame drain closed the list. Reopen before handing the raw
        // list to the DX12 executor; ResourceBarrier is still a recorded command.
        if ( !EnsureCommandListOpen().ok )
        {
            return false;
        }
    }

    Dx12RenderGraphSingleTransitionDesc desc;
    desc.commandList = CommandList();
    desc.resource = resource;
    desc.before = before;
    desc.after = after;
    desc.subresource = subresource;
    const Dx12RenderGraphBarrierRecord record =
        ExecuteDx12RenderGraphSingleTransition( "Dx12Explicit", passName, resourceName, desc );
    if ( !record.hasConcreteStates || !record.hasNativeResource || record.missingCommandList ||
         record.beforeState == record.afterState || !record.emitted )
    {
        SB_FATAL( "RenderBackendDX12",
                  "DX12 explicit transition did not emit exactly one concrete barrier. pass=%s resource=%s",
                  passName ? passName : "unknown",
                  resourceName ? resourceName : "unknown" );
    }
    return true;
}


bool RenderBackendDX12::TransitionBackbuffer( const char* passName, RenderGraphResourceAccess after )
{
    ID3D12Resource* backbuffer = m_renderTargets[m_frameIndex];
    if ( !backbuffer || m_backBufferAccess == after )
    {
        return false;
    }

    // Hazard: text-only or diagnostic frames can reach Present() without
    // Clear(), so the present barrier must start from the tracked state instead
    // of assuming the swap-chain image was rendered this frame.
    if ( !ExecuteGraphTransition( passName, "SwapchainBackbuffer", backbuffer, m_backBufferAccess, after ) )
    {
        return false;
    }
    m_backBufferAccess = after;
    return true;
}


bool RenderBackendDX12::ExecuteGraphUavBarrier( const char* passName,
                                                const char* resourceName,
                                                ID3D12Resource* resource )
{
    if ( !resource )
    {
        return true;
    }

    if ( !m_commandRecording.CanRecord() )
    {
        if ( !EnsureCommandListOpen().ok )
        {
            return false;
        }
    }

    Dx12RenderGraphUavBarrierDesc desc;
    desc.commandList = CommandList();
    desc.resource = resource;
    const Dx12RenderGraphUavBarrierRecord record =
        ExecuteDx12RenderGraphUavBarrier( "Dx12Explicit", passName, resourceName, desc );
    if ( !record.hasNativeResource || record.missingCommandList || !record.emitted )
    {
        SB_FATAL( "RenderBackendDX12",
                  "DX12 explicit UAV barrier did not emit exactly one concrete barrier. pass=%s resource=%s",
                  passName ? passName : "unknown",
                  resourceName ? resourceName : "unknown" );
    }
    return true;
}


SkullbonezCore::Basics::SbResult RenderBackendDX12::FlushUploadBuffer()
{
    if ( m_commandRecording.HasFailure() )
    {
        return m_commandRecording.CurrentResult();
    }
    if ( !m_commandRecording.IsOpen() )
    {
        return SkullbonezCore::Basics::SbResult::Success();
    }
    // Submit current work and wait for completion (mid-frame flush for upload exhaustion)
    const int suspendedPlatformGpuDepth = SuspendPlatformProfilerGpuStackForSubmit( "FlushUploadBuffer" );
    AssertPlatformProfilerGpuStackClosed( "FlushUploadBuffer" );
    SkullbonezCore::Basics::SbResult stateResult =
        m_commandRecording.CommitClose( CommandList()->Close(), "FlushUploadBuffer Close" );
    if ( !stateResult.ok )
    {
        RestorePlatformProfilerGpuStackAfterSubmit( suspendedPlatformGpuDepth );
        return stateResult;
    }
    SubmitClosedCommandList();
    stateResult = m_commandRecording.CommitWait( WaitForGpu() );
    if ( !stateResult.ok )
    {
        RestorePlatformProfilerGpuStackAfterSubmit( suspendedPlatformGpuDepth );
        return stateResult;
    }

    // Reopen with same allocator (WaitForGpu completed everything)
    stateResult = m_commandRecording.CommitAllocatorReset( m_commandAllocators[m_allocatorIndex]->Reset(),
                                                           "FlushUploadBuffer allocator Reset" );
    if ( !stateResult.ok )
    {
        RestorePlatformProfilerGpuStackAfterSubmit( suspendedPlatformGpuDepth );
        return stateResult;
    }
    stateResult =
        m_commandRecording.CommitListReset( CommandList()->Reset( m_commandAllocators[m_allocatorIndex], nullptr ),
                                            "FlushUploadBuffer list Reset" );
    if ( !stateResult.ok )
    {
        RestorePlatformProfilerGpuStackAfterSubmit( suspendedPlatformGpuDepth );
        return stateResult;
    }
    ID3D12DescriptorHeap* heaps[] = { m_srvHeap };
    CommandList()->SetDescriptorHeaps( 1, heaps );
    CommandList()->SetGraphicsRootSignature( m_rootSignature );
    RestorePlatformProfilerGpuStackAfterSubmit( suspendedPlatformGpuDepth );

    // FlushUploadBuffer submits and waits for all current GPU work before it
    // reopens the command list. That blocking wait is expensive, but it gives
    // the same safety proof as a normal frame-fence wait: the old upload bytes
    // and temporary descriptors are no longer in use, so the arenas can rewind.
    m_uploadSystem.ResetFrame( m_allocatorIndex );
    m_srvDescriptors.ResetFrame( m_allocatorIndex );
    m_lastPSOHash = 0;
    m_texBindingsDirty = true;
    m_targetsDirty = true;
    return SkullbonezCore::Basics::SbResult::Success();
}


SkullbonezCore::Basics::SbResult RenderBackendDX12::FlushUploadBufferIfNeeded( UINT64 size, UINT64 alignment )
{
    if ( m_commandRecording.HasFailure() )
    {
        return m_commandRecording.CurrentResult();
    }
    if ( !m_uploadSystem.CanAllocate( m_allocatorIndex, size, alignment ) )
    {
        // This is the expensive fallback path. It means the CPU has filled this
        // frame's upload arena before the frame was submitted, so we must submit
        // the current command list and wait before reusing the same bytes. The
        // event log keeps this visible because frequent mid-frame flushes are a
        // sign that the future Dx12RenderDevice needs larger upload pages or a
        // different upload strategy for the current workload.
        const Dx12UploadArenaStats stats = m_uploadSystem.GetStats( m_allocatorIndex );
        Log().WriteEventf(
            "dx12_upload_arena_flush frame=%u used_bytes=%llu capacity_bytes=%llu requested_bytes=%llu alignment=%llu",
            m_allocatorIndex,
            static_cast<unsigned long long>( stats.usedBytes ),
            static_cast<unsigned long long>( stats.capacityBytes ),
            static_cast<unsigned long long>( size ),
            static_cast<unsigned long long>( alignment ) );
        return FlushUploadBuffer();
    }
    return SkullbonezCore::Basics::SbResult::Success();
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
    Log().WriteEventf( "dx12_render_architecture_stats reason=%s root_parameters=%u ordinary_raster_srv_slots=t%u..t%u "
                       "rtv_descriptors=%u/%u dsv_descriptors=%u/%u static_srvs=%u/%u transient_srv_peak=%u/%u "
                       "upload_peak_bytes=%llu upload_capacity_bytes=%llu",
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


static DXGI_FORMAT ToDx12GraphColorFormat( RenderGraphResourceFormat format )
{
    switch ( format )
    {
    case RenderGraphResourceFormat::RGBA8:
        return DXGI_FORMAT_R8G8B8A8_UNORM;
    case RenderGraphResourceFormat::RGBA16F:
        return DXGI_FORMAT_R16G16B16A16_FLOAT;
    default:
        SB_FATAL( "RenderBackendDX12",
                  "Unsupported render graph color transient format. format=%d",
                  static_cast<int>( format ) );
    }
}


static DXGI_FORMAT ToDx12GraphSrvFormat( RenderGraphResourceFormat format )
{
    if ( format == RenderGraphResourceFormat::Depth24Stencil8 )
    {
        return DXGI_FORMAT_R24_UNORM_X8_TYPELESS;
    }
    return ToDx12GraphColorFormat( format );
}


static size_t CountGraphDescriptorRows( const RenderGraphDescriptorNeeds& descriptors )
{
    return ( descriptors.renderTarget ? 1u : 0u ) + ( descriptors.depthStencil ? 1u : 0u ) +
           ( descriptors.shaderResource ? 1u : 0u ) + ( descriptors.unorderedAccess ? 1u : 0u );
}

static void MarkGraphTransientMaterializationFailure( RenderGraphTransientMaterializationStats& stats,
                                                      HRESULT result,
                                                      const RenderGraphResourceDesc& resource )
{
    stats.failed = true;
    stats.failureHresult = static_cast<unsigned int>( result );
    std::snprintf( stats.failureStage, sizeof( stats.failureStage ), "%s", "CreateCommittedResource" );
    std::snprintf( stats.failureResource,
                   sizeof( stats.failureResource ),
                   "%s",
                   ( resource.name && resource.name[0] != '\0' ) ? resource.name : "UnnamedGraphTransient" );
    Log().WriteEventf( "dx12_graph_transient_materialize_failed stage=%s resource=%s hresult=0x%08X",
                       stats.failureStage,
                       stats.failureResource,
                       stats.failureHresult );
    Log().FlushAll();
}


RenderGraphTransientMaterializationStats
RenderBackendDX12::MaterializeGraphTransientResources( const RenderGraph& graph,
                                                       const RenderGraphCompileResult& compiled )
{
    // Concept: graph transients are frame-target pool slots, not scene assets.
    //
    // The render graph compiler decides which transient declarations may share
    // one slot. The DX12 backend materializes that slot as a texture plus view
    // descriptors, then keeps the slot reusable until backend shutdown. Existing
    // material/object SRV tables remain separate because those descriptors are
    // long-lived content bindings, not frame-target lifetime records.
    m_graphTransientStats = {};
    m_graphTransientBindings.clear();
    for ( GraphTransientResourceDX12& slot : m_graphTransientResources )
    {
        slot.usedThisCompile = false;
    }

    if ( !Device() )
    {
        SB_FATAL( "RenderBackendDX12", "DX12 graph transient materialization requires an initialized device." );
    }

    for ( const RenderGraphTransientAllocationDesc& allocation : compiled.transientAllocations )
    {
        if ( allocation.resource.index >= graph.Resources().size() )
        {
            SB_FATAL( "RenderBackendDX12",
                      "DX12 graph transient allocation references an invalid resource. index=%u resourceCount=%zu",
                      allocation.resource.index,
                      graph.Resources().size() );
        }

        const RenderGraphResourceDesc& resource = graph.Resources()[allocation.resource.index];
        const RenderGraphTransientResourceDesc& desc = resource.transient;
        if ( desc.kind != RenderGraphResourceKind::Texture2D )
        {
            SB_FATAL( "RenderBackendDX12",
                      "DX12 graph transient materializer currently supports Texture2D resources only." );
        }
        if ( desc.format == RenderGraphResourceFormat::Unknown )
        {
            SB_FATAL( "RenderBackendDX12", "DX12 graph transient materializer requires a concrete resource format." );
        }
        if ( desc.descriptors.depthStencil && desc.descriptors.unorderedAccess )
        {
            SB_FATAL( "RenderBackendDX12", "DX12 graph transient depth resources cannot request UAV descriptors." );
        }

        GraphTransientResourceDX12* slot = nullptr;
        for ( GraphTransientResourceDX12& candidate : m_graphTransientResources )
        {
            if ( GraphTransientPoolSlotCanSatisfyDX12( candidate, allocation.poolSlot, desc ) )
            {
                slot = &candidate;
                ++m_graphTransientStats.reusedThisCompile;
                break;
            }
        }

        if ( !slot )
        {
            m_graphTransientResources.push_back( GraphTransientResourceDX12() );
            slot = &m_graphTransientResources.back();
            slot->desc = desc;
            slot->poolSlot = allocation.poolSlot;

            D3D12_RESOURCE_DESC textureDesc = {};
            textureDesc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
            textureDesc.Width = desc.width;
            textureDesc.Height = desc.height;
            textureDesc.DepthOrArraySize = 1;
            textureDesc.MipLevels = static_cast<UINT16>( desc.mipLevels );
            textureDesc.SampleDesc.Count = 1;
            textureDesc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
            textureDesc.Flags = D3D12_RESOURCE_FLAG_NONE;

            D3D12_CLEAR_VALUE clearValue = {};
            D3D12_CLEAR_VALUE* clearValuePtr = nullptr;
            if ( desc.descriptors.depthStencil )
            {
                textureDesc.Format = DXGI_FORMAT_R24G8_TYPELESS;
                textureDesc.Flags |= D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL;
                clearValue.Format = DXGI_FORMAT_D24_UNORM_S8_UINT;
                clearValue.DepthStencil.Depth = 1.0f;
                clearValuePtr = &clearValue;
            }
            else
            {
                textureDesc.Format = ToDx12GraphColorFormat( desc.format );
                if ( desc.descriptors.renderTarget )
                {
                    textureDesc.Flags |= D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;
                    clearValue.Format = textureDesc.Format;
                    clearValue.Color[3] = 1.0f;
                    clearValuePtr = &clearValue;
                }
                if ( desc.descriptors.unorderedAccess )
                {
                    textureDesc.Flags |= D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
                }
            }

            D3D12_RESOURCE_STATES initialState = D3D12_RESOURCE_STATE_COMMON;
            if ( !TryDx12RenderGraphAccessToResourceState( resource.initialAccess, initialState ) )
            {
                initialState = D3D12_RESOURCE_STATE_COMMON;
            }

            D3D12_HEAP_PROPERTIES defaultHeap = {};
            defaultHeap.Type = D3D12_HEAP_TYPE_DEFAULT;
            const HRESULT hr = Device()->CreateCommittedResource( &defaultHeap,
                                                                  D3D12_HEAP_FLAG_NONE,
                                                                  &textureDesc,
                                                                  initialState,
                                                                  clearValuePtr,
                                                                  IID_PPV_ARGS( &slot->resource ) );
            if ( FAILED( hr ) )
            {
                // Lane R: graph transients back optional post-process features.
                // Report the failed resource and let the pass fall back to its
                // older framebuffer target instead of unwinding the frame.
                MarkGraphTransientMaterializationFailure( m_graphTransientStats, hr, resource );
                m_graphTransientResources.pop_back();
                m_graphTransientStats.poolSize = m_graphTransientResources.size();
                return m_graphTransientStats;
            }
            NameDx12Object( slot->resource, L"Skullbonez DX12 RenderGraph Transient Texture" );

            if ( desc.descriptors.renderTarget )
            {
                slot->rtv = AllocateRTV();
                Device()->CreateRenderTargetView( slot->resource, nullptr, slot->rtv );
            }
            if ( desc.descriptors.depthStencil )
            {
                slot->dsv = AllocateDSV();
                D3D12_DEPTH_STENCIL_VIEW_DESC dsvDesc = {};
                dsvDesc.Format = DXGI_FORMAT_D24_UNORM_S8_UINT;
                dsvDesc.ViewDimension = D3D12_DSV_DIMENSION_TEXTURE2D;
                Device()->CreateDepthStencilView( slot->resource, &dsvDesc, slot->dsv );
            }
            if ( desc.descriptors.shaderResource )
            {
                slot->srvIndex = AllocateStaticSRV();
                D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
                srvDesc.Format = ToDx12GraphSrvFormat( desc.format );
                srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
                srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
                srvDesc.Texture2D.MipLevels = desc.mipLevels;
                Device()->CreateShaderResourceView( slot->resource,
                                                    &srvDesc,
                                                    GetSRVStagingCpuHandle( slot->srvIndex ) );
            }
            if ( desc.descriptors.unorderedAccess )
            {
                slot->uavIndex = AllocateStaticSRV();
                D3D12_UNORDERED_ACCESS_VIEW_DESC uavDesc = {};
                uavDesc.Format = ToDx12GraphColorFormat( desc.format );
                uavDesc.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2D;
                Device()->CreateUnorderedAccessView( slot->resource,
                                                     nullptr,
                                                     &uavDesc,
                                                     GetSRVStagingCpuHandle( slot->uavIndex ) );
            }
            ++m_graphTransientStats.createdThisCompile;
        }

        std::snprintf( slot->resourceName,
                       sizeof( slot->resourceName ),
                       "%s",
                       ( resource.name && resource.name[0] != '\0' ) ? resource.name : "UnnamedGraphTransient" );
        if ( desc.descriptors.shaderResource && slot->textureHandle == 0 && slot->srvIndex != UINT_MAX )
        {
            slot->textureHandle = RegisterSRV( slot->srvIndex );
        }
        const bool firstUseThisCompile = !slot->usedThisCompile;
        slot->poolSlot = allocation.poolSlot;
        slot->firstPass = allocation.firstPass;
        slot->lastPass = allocation.lastPass;
        if ( firstUseThisCompile )
        {
            slot->currentAccess = resource.initialAccess;
        }
        slot->usedThisCompile = true;
        m_graphTransientBindings.push_back(
            { allocation.resource, static_cast<size_t>( slot - m_graphTransientResources.data() ) } );
    }

    m_graphTransientStats.poolSize = m_graphTransientResources.size();
    m_graphTransientStats.releasedAtFrameEnd = compiled.transientDiagnostics.releaseCount;
    for ( const GraphTransientResourceDX12& slot : m_graphTransientResources )
    {
        if ( slot.resource )
        {
            m_graphTransientStats.descriptorRowsOwned += CountGraphDescriptorRows( slot.desc.descriptors );
        }
    }
    Log().WriteEventf( "dx12_graph_transient_materialize allocations=%zu pool_size=%zu created_this_compile=%zu "
                       "reused_this_compile=%zu descriptor_rows_owned=%zu released_at_frame_end=%zu",
                       compiled.transientAllocations.size(),
                       m_graphTransientStats.poolSize,
                       m_graphTransientStats.createdThisCompile,
                       m_graphTransientStats.reusedThisCompile,
                       m_graphTransientStats.descriptorRowsOwned,
                       m_graphTransientStats.releasedAtFrameEnd );
    return m_graphTransientStats;
}


GraphTransientResourceDX12* RenderBackendDX12::FindGraphTransientSlot( RenderGraphResourceHandle resource )
{
    for ( const GraphTransientBindingDX12& binding : m_graphTransientBindings )
    {
        if ( binding.resource.index == resource.index && binding.slotIndex < m_graphTransientResources.size() )
        {
            return &m_graphTransientResources[binding.slotIndex];
        }
    }
    return nullptr;
}


const GraphTransientResourceDX12* RenderBackendDX12::FindGraphTransientSlot( RenderGraphResourceHandle resource ) const
{
    for ( const GraphTransientBindingDX12& binding : m_graphTransientBindings )
    {
        if ( binding.resource.index == resource.index && binding.slotIndex < m_graphTransientResources.size() )
        {
            return &m_graphTransientResources[binding.slotIndex];
        }
    }
    return nullptr;
}


RenderGraphTextureBinding RenderBackendDX12::ResolveGraphTextureBinding( RenderGraphResourceHandle resource ) const
{
    const GraphTransientResourceDX12* slot = FindGraphTransientSlot( resource );
    if ( !slot || !slot->resource )
    {
        return {};
    }

    RenderGraphTextureBinding binding;
    binding.resource = resource;
    binding.textureHandle = slot->textureHandle;
    binding.width = slot->desc.width;
    binding.height = slot->desc.height;
    binding.renderTarget = slot->desc.descriptors.renderTarget;
    binding.shaderResource = slot->desc.descriptors.shaderResource;
    return binding;
}


void RenderBackendDX12::BeginGraphTextureRenderTarget( const RenderGraphTextureBinding& binding, const char* passName )
{
    if ( m_graphRenderTargetActive )
    {
        SB_FATAL( "RenderBackendDX12", "DX12 graph transient render target is already active." );
    }
    if ( !binding.IsValid() || !binding.renderTarget )
    {
        SB_FATAL( "RenderBackendDX12",
                  "DX12 graph transient render target binding is invalid. textureHandle=%u renderTarget=%d",
                  binding.textureHandle,
                  binding.renderTarget ? 1 : 0 );
    }
    GraphTransientResourceDX12* slot = FindGraphTransientSlot( binding.resource );
    if ( !slot || !slot->resource || slot->rtv.ptr == 0 )
    {
        SB_FATAL( "RenderBackendDX12", "DX12 graph transient render target was not materialized." );
    }

    // Lifetime: callback-owned graph passes borrow the active backbuffer/depth
    // target while a transient is bound, then restore it before the next pass.
    m_savedGraphRTV = m_currentRTV;
    m_savedGraphDSV = m_currentDSV;
    m_savedGraphRTVFormat = m_currentRTVFormat;
    if ( !ExecuteGraphTransition( passName,
                                  slot->resourceName,
                                  slot->resource,
                                  slot->currentAccess,
                                  RenderGraphResourceAccess::RenderTarget ) )
    {
        return;
    }
    SetRenderingToFBO( true, slot->srvIndex, UINT_MAX, ToDx12GraphColorFormat( slot->desc.format ) );
    slot->currentAccess = RenderGraphResourceAccess::RenderTarget;
    SetCurrentTargets( slot->rtv, m_savedGraphDSV );
    m_graphRenderTargetActive = true;
    m_activeGraphRenderTarget = binding.resource;
}


void RenderBackendDX12::EndGraphTextureRenderTarget( const RenderGraphTextureBinding& binding, const char* passName )
{
    if ( !m_graphRenderTargetActive || m_activeGraphRenderTarget.index != binding.resource.index )
    {
        SB_FATAL( "RenderBackendDX12",
                  "DX12 graph transient render target end does not match the active binding. active=%u requested=%u",
                  m_activeGraphRenderTarget.index,
                  binding.resource.index );
    }
    GraphTransientResourceDX12* slot = FindGraphTransientSlot( binding.resource );
    if ( !slot || !slot->resource )
    {
        SB_FATAL( "RenderBackendDX12", "DX12 graph transient render target was lost before unbind." );
    }

    if ( !ExecuteGraphTransition( passName,
                                  slot->resourceName,
                                  slot->resource,
                                  slot->currentAccess,
                                  RenderGraphResourceAccess::PixelShaderResource ) )
    {
        return;
    }
    slot->currentAccess = RenderGraphResourceAccess::PixelShaderResource;
    SetRenderingToFBO( false );
    SetCurrentTargets( m_savedGraphRTV, m_savedGraphDSV );
    m_currentRTVFormat = m_savedGraphRTVFormat;
    m_psoDirty = true;
    m_graphRenderTargetActive = false;
    m_activeGraphRenderTarget = {};
}


void RenderBackendDX12::ReleaseGraphTransientResources( const char* reason )
{
    size_t released = 0;
    for ( GraphTransientResourceDX12& slot : m_graphTransientResources )
    {
        if ( slot.textureHandle != 0 )
        {
            UnregisterSRV( slot.textureHandle );
            slot.textureHandle = 0;
        }
        if ( ReleaseGraphTransientPoolSlotResourceDX12( slot ) )
        {
            ++released;
        }
    }
    m_graphTransientResources.clear();
    m_graphTransientBindings.clear();
    m_graphTransientStats = {};
    m_graphRenderTargetActive = false;
    m_activeGraphRenderTarget = {};
    Log().WriteEventf( "dx12_graph_transient_release reason=%s released_resources=%zu",
                       reason ? reason : "unknown",
                       released );
}


void RenderBackendDX12::ReportDeviceLost( const char* context, HRESULT result ) const
{
    const HRESULT removedReason = Device() ? Device()->GetDeviceRemovedReason() : result;
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

    if ( Device() )
    {
        ID3D12DeviceRemovedExtendedData* dred = nullptr;
        if ( SUCCEEDED( Device()->QueryInterface( IID_PPV_ARGS( &dred ) ) ) )
        {
            D3D12_DRED_AUTO_BREADCRUMBS_OUTPUT breadcrumbs = {};
            D3D12_DRED_PAGE_FAULT_OUTPUT pageFault = {};
            const HRESULT breadcrumbResult = dred->GetAutoBreadcrumbsOutput( &breadcrumbs );
            const HRESULT pageFaultResult = dred->GetPageFaultAllocationOutput( &pageFault );

            Log().WriteEventf( "dx12_dred context=%s breadcrumbs_hr=0x%08lX breadcrumbs_head=%p page_fault_hr=0x%08lX "
                               "page_fault_va=0x%llX existing_allocations=%p recent_freed_allocations=%p",
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
                         "dred breadcrumbs_hr=0x%08lX breadcrumbs_head=%p page_fault_hr=0x%08lX page_fault_va=0x%llX "
                         "existing_allocations=%p recent_freed_allocations=%p\n",
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
    if ( !m_commandRecording.CanRecord() || !FlushUploadBufferIfNeeded( size, alignment ).ok )
    {
        return 0;
    }
    return SubAllocateUpload( size, alignment );
}


uint8_t* RenderBackendDX12::GetUploadPtr( D3D12_GPU_VIRTUAL_ADDRESS addr )
{
    return addr != 0 ? m_uploadSystem.GetMappedPtr( m_allocatorIndex, addr ) : nullptr;
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


SkullbonezCore::Basics::SbResult RenderBackendDX12::Init( HWND hwnd, HDC /*hdc*/, int width, int height )
{
    m_width = width;
    m_height = height;

    Dx12RenderDeviceInitDesc deviceDesc;
    deviceDesc.hwnd = hwnd;
    deviceDesc.width = static_cast<UINT>( width );
    deviceDesc.height = static_cast<UINT>( height );
    deviceDesc.frameCount = FRAME_COUNT;
    deviceDesc.backBufferFormat = DXGI_FORMAT_R8G8B8A8_UNORM;
    const SkullbonezCore::Basics::SbResult deviceResult = m_renderDevice.Init( deviceDesc );
    if ( !deviceResult.ok )
    {
        return deviceResult;
    }

    // A fresh device is the sole boundary allowed to clear a prior command
    // failure and submitted-work uncertainty. Dx12RenderDevice has already
    // closed the newly created list.
    m_commandRecording.ResetForDevice();
    m_submittedWork.ResetForDevice();

    // The render device owns the DXGI/D3D12 platform objects: factory, device,
    // graphics queue, swap chain, command allocators, command list, and frame
    // fence. The backend keeps only the queue/allocator aliases that still need
    // follow-up migration; device, swap-chain, and command-list access stays
    // owner-routed through Dx12RenderDevice.
    m_factory = m_renderDevice.Factory();
    m_commandQueue = m_renderDevice.GraphicsQueue();
    for ( int i = 0; i < FRAME_COUNT; ++i )
    {
        m_commandAllocators[i] = m_renderDevice.CommandAllocator( static_cast<UINT>( i ) );
        m_frameFenceValues[i] = 0;
    }
    m_allocatorIndex = m_renderDevice.AllocatorIndex();
    m_frameIndex = m_renderDevice.FrameIndex();
    m_allowTearing = m_renderDevice.AllowTearing();
    // DXR is optional hardware support; fall back to raster water if the device
    // cannot expose raytracing interfaces.
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
        const SkullbonezCore::Basics::SbResult rtvHeapResult =
            Dx12BackendInitResult( Device()->CreateDescriptorHeap( &desc, IID_PPV_ARGS( &m_rtvHeap ) ),
                                   "CreateDescriptorHeap (RTV) failed" );
        if ( !rtvHeapResult.ok )
        {
            return rtvHeapResult;
        }
        NameDx12Object( m_rtvHeap, L"Skullbonez DX12 RTV Heap" );
        m_rtvDescSize = Device()->GetDescriptorHandleIncrementSize( D3D12_DESCRIPTOR_HEAP_TYPE_RTV );
        m_rtvDescriptors.Init( m_rtvHeap, m_rtvDescSize, MAX_RTV_DESCRIPTORS, "RTV" );
    }
    // DSV rows describe depth/stencil targets: the main window depth buffer and
    // any off-screen depth buffers used by framebuffer passes.
    // Docs: https://learn.microsoft.com/en-us/windows/win32/api/d3d12/nf-d3d12-id3d12device-createdescriptorheap
    {
        D3D12_DESCRIPTOR_HEAP_DESC desc = {};
        desc.NumDescriptors = MAX_DSV_DESCRIPTORS;
        desc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_DSV;
        const SkullbonezCore::Basics::SbResult dsvHeapResult =
            Dx12BackendInitResult( Device()->CreateDescriptorHeap( &desc, IID_PPV_ARGS( &m_dsvHeap ) ),
                                   "CreateDescriptorHeap (DSV) failed" );
        if ( !dsvHeapResult.ok )
        {
            return dsvHeapResult;
        }
        NameDx12Object( m_dsvHeap, L"Skullbonez DX12 DSV Heap" );
        m_dsvDescSize = Device()->GetDescriptorHandleIncrementSize( D3D12_DESCRIPTOR_HEAP_TYPE_DSV );
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
        const SkullbonezCore::Basics::SbResult srvHeapResult =
            Dx12BackendInitResult( Device()->CreateDescriptorHeap( &desc, IID_PPV_ARGS( &m_srvHeap ) ),
                                   "CreateDescriptorHeap (SRV) failed" );
        if ( !srvHeapResult.ok )
        {
            return srvHeapResult;
        }
        NameDx12Object( m_srvHeap, L"Skullbonez DX12 Shader Visible SRV Heap" );
        m_srvDescSize = Device()->GetDescriptorHandleIncrementSize( D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV );
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
        const SkullbonezCore::Basics::SbResult srvStagingHeapResult =
            Dx12BackendInitResult( Device()->CreateDescriptorHeap( &desc, IID_PPV_ARGS( &m_srvStagingHeap ) ),
                                   "CreateDescriptorHeap (staging) failed" );
        if ( !srvStagingHeapResult.ok )
        {
            return srvStagingHeapResult;
        }
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
    m_srvDescriptors
        .Init( m_srvHeap, m_srvStagingHeap, m_srvDescSize, MAX_STATIC_SRVS, MAX_TRANSIENT_SRVS, FRAME_COUNT );
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
    Device()->CreateShaderResourceView( nullptr, &nullTextureSrv, GetSRVStagingCpuHandle( m_nullTextureSRVIndex ) );

    // Lifetime: swap-chain images are replaced on resize, but the engine keeps
    // one stable RTV descriptor row per back buffer index. ResizeBuffers swaps
    // the image memory; CreateRenderTargetView overwrites the existing row with
    // a view record for the new image.
    // Docs: https://learn.microsoft.com/en-us/windows/win32/api/d3d12/nf-d3d12-id3d12device-createrendertargetview
    for ( int i = 0; i < FRAME_COUNT; ++i )
    {
        const SkullbonezCore::Basics::SbResult backBufferResult =
            Dx12BackendInitResult( SwapChain()->GetBuffer( (UINT)i, IID_PPV_ARGS( &m_renderTargets[i] ) ),
                                   "SwapChain GetBuffer failed" );
        if ( !backBufferResult.ok )
        {
            return backBufferResult;
        }
        NameDx12ObjectIndexed( m_renderTargets[i], L"Skullbonez DX12 Swapchain Backbuffer", (UINT)i );
        // Reserve one stable RTV row for each swap-chain buffer. ResizeBuffers
        // replaces the back-buffer resources later, but the descriptor rows stay
        // the same and are simply overwritten with new view records.
        m_backBufferRTVs[i] = m_rtvDescriptors.Allocate().cpuHandle;
        Device()->CreateRenderTargetView( m_renderTargets[i], nullptr, m_backBufferRTVs[i] );
    }

    // Depth stencil
    const SkullbonezCore::Basics::SbResult depthStencilResult = CreateDepthStencil( width, height );
    if ( !depthStencilResult.ok )
    {
        return depthStencilResult;
    }

    // Create per-frame upload buffers — one per FRAME_COUNT allocator. Each holds CPU-writable,
    // GPU-readable memory for per-frame constant buffers, dynamic vertex buffers, and texture
    // uploads. Partitioned per-allocator so that frame N+1's CPU recording cannot overwrite data
    // that frame N's GPU is still reading (the per-allocator fence wait in EnsureCommandListOpen
    // guarantees frame N is done before we reuse that allocator's upload buffer on frame N+2).
    // Docs: https://learn.microsoft.com/en-us/windows/win32/api/d3d12/nf-d3d12-id3d12device-createcommittedresource
    // Dx12FrameUploadSystem owns the actual upload resources and their
    // persistent CPU Map() pointers. RenderBackendDX12 now asks for byte ranges
    // instead of owning the raw upload-buffer lifecycle itself.
    m_uploadSystem.Init( Device(), FRAME_COUNT, UPLOAD_BUFFER_SIZE, L"Skullbonez DX12 Frame Upload Buffer" );

    const SkullbonezCore::Basics::SbResult rootSignatureResult = CreateRootSignature();
    if ( !rootSignatureResult.ok )
    {
        return rootSignatureResult;
    }
    const SkullbonezCore::Basics::SbResult genMipsResult = InitGenMipsPipeline();
    if ( !genMipsResult.ok )
    {
        return genMipsResult;
    }
    EnsureGridLinePipeline( DXGI_FORMAT_R8G8B8A8_UNORM );
    EnsureGridLinePipeline( DXGI_FORMAT_R16G16B16A16_FLOAT );
    EnsureTransientTriangleShader( TransientTriangleStyle::Color );
    EnsureTransientTriangleShader( TransientTriangleStyle::SoftAdditiveRibbon );
    EnsureTransientTriangleShader( TransientTriangleStyle::TrajectoryRibbon );
    EnsureTransientTriangleShader( TransientTriangleStyle::TrajectoryRibbonDepthHint );

    // GPU timestamp query heap — used for GPU-side performance profiling. The GPU writes
    // timestamps at specific points in the command stream, which we later read back to
    // calculate elapsed time for specific rendering passes (terrain, spheres, water, etc.).
    // Docs: https://learn.microsoft.com/en-us/windows/win32/api/d3d12/nf-d3d12-id3d12device-createqueryheap
    {
        D3D12_QUERY_HEAP_DESC qhDesc = {};
        qhDesc.Type = D3D12_QUERY_HEAP_TYPE_TIMESTAMP;
        qhDesc.Count = (UINT)TIMER_HEAP_SIZE;
        if ( SUCCEEDED( Device()->CreateQueryHeap( &qhDesc, IID_PPV_ARGS( &m_gpuTimers.queryHeap ) ) ) )
        {
            NameDx12Object( m_gpuTimers.queryHeap, L"Skullbonez DX12 GPU Timer Query Heap" );
            // Readback buffer — CPU-readable memory where GPU timer results are copied to.
            // The READBACK heap type means the CPU can read from it (but the GPU cannot render to it).
            // Docs:
            // https://learn.microsoft.com/en-us/windows/win32/api/d3d12/nf-d3d12-id3d12device-createcommittedresource
            const UINT64 timerReadbackBytes = static_cast<UINT64>( TIMER_HEAP_SIZE ) * sizeof( uint64_t );
            // Dx12ReadbackBuffer owns the CPU-readable resource. The backend
            // still decides when the fence is safe to read, but it no longer
            // carries the raw COM allocation/release path for timer bytes.
            // Buffers on all heap types are effectively created in COMMON state in D3D12
            // regardless of the specified initial state. For READBACK buffers the runtime
            // accepts any state but always uses COMMON — be explicit to keep the debug layer
            // quiet. CPU Map/Unmap access is independent of the GPU-visible resource state.
            // Docs:
            // https://learn.microsoft.com/en-us/windows/win32/direct3d12/using-resource-barriers-to-synchronize-resource-states-in-direct3d-12#implicit-state-transitions
            if ( !m_gpuTimers.readback.InitBuffer( Device(),
                                                   timerReadbackBytes,
                                                   L"Skullbonez DX12 GPU Timer Readback Buffer" ) )
            {
                m_gpuTimers.queryHeap->Release();
                m_gpuTimers.queryHeap = nullptr;
            }
            else
            {
                const SkullbonezCore::Basics::SbResult timestampFrequencyResult =
                    Dx12BackendInitResult( m_commandQueue->GetTimestampFrequency( &m_gpuTimers.freq ),
                                           "GetTimestampFrequency failed" );
                if ( !timestampFrequencyResult.ok )
                {
                    // Lifetime: a frequency failure leaves the profiler unusable,
                    // so release both timer resources before aborting initialization.
                    m_gpuTimers.readback.Reset();
                    m_gpuTimers.queryHeap->Release();
                    m_gpuTimers.queryHeap = nullptr;
                    return timestampFrequencyResult;
                }
            }
        }
    }

    m_viewport = { 0.0f, 0.0f, (float)width, (float)height, 0.0f, 1.0f };
    m_scissorRect = { 0, 0, (LONG)width, (LONG)height };

    m_currentRTV = m_backBufferRTVs[m_frameIndex];
    m_currentDSV = m_mainDSV;

    return SkullbonezCore::Basics::SbResult::Success();
}


SkullbonezCore::Basics::SbResult RenderBackendDX12::CreateRootSignature()
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
    if ( FAILED(
             D3D12SerializeVersionedRootSignature( &rootSigDesc, signature.GetAddressOf(), error.GetAddressOf() ) ) )
    {
        std::string msg = "Root signature serialization failed";
        if ( error )
        {
            msg += ": ";
            msg += (const char*)error->GetBufferPointer();
        }
        return SkullbonezCore::Basics::SbResult::Failure( "Rendering/DX12", "%s", msg.c_str() );
    }

    // Create the Root Signature object from the serialized blob. This is the "contract" between
    // the application and shaders — it defines the layout of all shader-visible parameters.
    // Every PSO must reference a root signature, and every draw call must bind matching data.
    // Docs: https://learn.microsoft.com/en-us/windows/win32/api/d3d12/nf-d3d12-id3d12device-createrootsignature
    if ( FAILED( Device()->CreateRootSignature( 0,
                                                signature->GetBufferPointer(),
                                                signature->GetBufferSize(),
                                                IID_PPV_ARGS( &m_rootSignature ) ) ) )
    {
        return SkullbonezCore::Basics::SbResult::Failure( "Rendering/DX12", "CreateRootSignature failed" );
    }
    NameDx12Object( m_rootSignature, L"Skullbonez DX12 Main Root Signature" );
#ifdef _DEBUG
    Log().WriteEventf(
        "dx12_ordinary_raster_binding_abi root_parameters=%u cbv=b%u srv_slots=t%u..t%u material_table=t4 "
        "samplers=s%u,s%u,s%u bind_texture_slots=%d material_payload=packed_instance_params",
        ORDINARY_RASTER_ROOT_PARAMETER_COUNT,
        SHADER_REGISTER_FRAME_CONSTANTS,
        SHADER_REGISTER_FIRST_TEXTURE,
        SHADER_REGISTER_FIRST_TEXTURE + static_cast<UINT>( TEXTURE_SLOT_COUNT - 1 ),
        SAMPLER_REGISTER_LINEAR_WRAP,
        SAMPLER_REGISTER_LINEAR_CLAMP,
        SAMPLER_REGISTER_SHADOW_POINT_CLAMP,
        TEXTURE_SLOT_COUNT );
#endif
    return SkullbonezCore::Basics::SbResult::Success();
}


SkullbonezCore::Basics::SbResult RenderBackendDX12::CreateDepthStencil( int w, int h )
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
    const HRESULT createResult = Device()->CreateCommittedResource( &heapProps,
                                                                    D3D12_HEAP_FLAG_NONE,
                                                                    &desc,
                                                                    D3D12_RESOURCE_STATE_DEPTH_WRITE,
                                                                    &clearValue,
                                                                    IID_PPV_ARGS( &m_depthStencil ) );
    if ( FAILED( createResult ) || !m_depthStencil )
    {
        return Dx12BackendOperationResult( FAILED( createResult ) ? createResult : E_FAIL,
                                           "CreateCommittedResource (depth stencil) failed" );
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
    // Docs: https://learn.microsoft.com/en-us/windows/win32/api/d3d12/nf-d3d12-id3d12device-createdepthstencilview
    Device()->CreateDepthStencilView( m_depthStencil, &dsvDesc, m_mainDSV );
    return SkullbonezCore::Basics::SbResult::Success();
}


void RenderBackendDX12::Shutdown()
{
    if ( !Device() )
    {
        if ( m_submittedWork.HasSubmittedWork() )
        {
            // Lane F: terminal shutdown cannot release a partially owned device
            // after losing the only fence path that could prove queue completion.
            SB_FATAL( "RenderBackendDX12",
                      "Shutdown found submitted GPU work after the DX12 device/fence became unavailable." );
        }
        // Partial initialisation can fail inside Dx12RenderDevice before the
        // backend has copied all remaining queue/allocator aliases. Even in
        // that state, the device owner may already hold factory/device/queue/
        // swap-chain objects, so always give it a chance to release them.
        m_renderDevice.Shutdown();
        m_factory = nullptr;
        m_commandQueue = nullptr;
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
    // PRESENT state first so the final DXGI Present() has a legal resource.
    if ( !m_renderingToFBO && m_backBufferAccess != RenderGraphResourceAccess::Present && SwapChain() &&
         m_renderTargets[m_frameIndex] )
    {
        const SkullbonezCore::Basics::SbResult openResult = EnsureCommandListOpen();
        if ( !openResult.ok )
        {
            // Lane F: shutdown cannot return a recoverable result, and Present
            // cannot legally drain a back buffer left in render-target state.
            SB_FATAL(
                "RenderBackendDX12",
                "Shutdown could not open the command list for the final backbuffer transition. owner=%s reason=%s",
                openResult.error.owner,
                openResult.error.message );
        }
        if ( !TransitionBackbuffer( "ShutdownBackbufferPresent", RenderGraphResourceAccess::Present ) )
        {
            const SkullbonezCore::Basics::SbResult transitionResult = m_commandRecording.CurrentResult();
            SB_FATAL( "RenderBackendDX12",
                      "Shutdown could not record the final backbuffer transition. owner=%s reason=%s",
                      transitionResult.error.owner,
                      transitionResult.error.message );
        }
    }

    // Ensure any open command list is closed and submitted before waiting.
    if ( m_commandRecording.IsOpen() )
    {
        if ( m_commandRecording.HasFailure() )
        {
            SB_FATAL( "RenderBackendDX12",
                      "Shutdown cannot submit a logically open command list after recording failed. owner=%s reason=%s",
                      m_commandRecording.CurrentResult().error.owner,
                      m_commandRecording.CurrentResult().error.message );
        }
        AssertPlatformProfilerGpuStackClosed( "Shutdown" );
        const SkullbonezCore::Basics::SbResult closeResult =
            m_commandRecording.CommitClose( CommandList()->Close(), "Shutdown command list Close" );
        if ( !closeResult.ok )
        {
            SB_FATAL( "RenderBackendDX12",
                      "Shutdown command-list Close failed; resources remain process-owned. owner=%s reason=%s",
                      closeResult.error.owner,
                      closeResult.error.message );
        }
        SubmitClosedCommandList();
    }

    // Wait for all GPU work to complete (command queue + pending presents).
    const SkullbonezCore::Basics::SbResult initialDrainResult = WaitForGpu();
    if ( !initialDrainResult.ok )
    {
        // Lane F: releasing any backend object after this point could race a
        // submitted command stream. Terminal shutdown must stop instead.
        SB_FATAL( "RenderBackendDX12",
                  "Shutdown could not prove initial GPU queue completion. owner=%s reason=%s",
                  initialDrainResult.error.owner,
                  initialDrainResult.error.message );
    }

    // Drain the DXGI flip queue. DX12's WaitForGpu only waits on the command queue fence,
    // but DXGI's flip-model present queue is separate. Without draining it, DWM may still
    // hold references to this swap chain's backbuffers after Release(), delaying the
    // window/compositor surface cleanup.
    // Present an empty frame with sync-interval 0 to flush the flip queue, then wait again.
    if ( SwapChain() )
    {
        const HRESULT drainPresentResult = SwapChain()->Present( 0, 0 );
        if ( IsDx12DeviceLostResult( drainPresentResult ) )
        {
            ReportDeviceLost( "Shutdown Present drain", drainPresentResult );
        }
        const SkullbonezCore::Basics::SbResult checkedPresent =
            Dx12BackendOperationResult( drainPresentResult, "Shutdown swap-chain Present drain failed" );
        if ( !checkedPresent.ok )
        {
            SB_FATAL( "RenderBackendDX12",
                      "Shutdown could not drain the swap-chain present queue. owner=%s reason=%s",
                      checkedPresent.error.owner,
                      checkedPresent.error.message );
        }

        const SkullbonezCore::Basics::SbResult presentDrainResult = WaitForGpu();
        if ( !presentDrainResult.ok )
        {
            SB_FATAL( "RenderBackendDX12",
                      "Shutdown could not prove queue completion after the final Present. owner=%s reason=%s",
                      presentDrainResult.error.owner,
                      presentDrainResult.error.message );
        }
    }

    if ( m_submittedWork.HasSubmittedWork() )
    {
        SB_FATAL( "RenderBackendDX12", "Shutdown reached resource release with unproven submitted GPU work." );
    }
    if ( !m_deferredResourceReleases.empty() )
    {
        SB_FATAL( "RenderBackendDX12",
                  "Shutdown drain completed but deferred GPU resources remain quarantined. count=%zu",
                  m_deferredResourceReleases.size() );
    }

    // Lifetime: screenshot readbacks detached after an uncertain wait remain
    // process-owned until the successful terminal drains above make Release safe.
    for ( size_t i = 0; i < m_uncertainReadbackResourceCount; ++i )
    {
        if ( m_uncertainReadbackResources[i] )
        {
            m_uncertainReadbackResources[i]->Release();
            m_uncertainReadbackResources[i] = nullptr;
        }
    }
    m_uncertainReadbackResourceCount = 0;

    // DXR resources hang off newer D3D12 interfaces and contain GPU-side
    // acceleration structures. Release them before the shared renderer objects
    // below so no raytracing object outlives the device/command-list aliases it
    // was created from.
    ShutdownDXR();

    ReportArchitectureStats( "Shutdown" );
    ReleaseGraphTransientResources( "Shutdown" );

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
        if ( SUCCEEDED( Device()->QueryInterface( IID_PPV_ARGS( &infoQueue ) ) ) )
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
    for ( size_t i = 0; i < m_psoCacheCount; ++i )
    {
        if ( m_psoCache[i].pso )
        {
            m_psoCache[i].pso->Release();
            m_psoCache[i].pso = nullptr;
        }
    }
    m_psoCacheCount = 0;

    // Grid line overlay resources. These PSOs are keyed by RTV format because
    // cinematic HDR and ordinary swapchain draws bind different color formats.
    for ( size_t i = 0; i < m_gridLinePSOCount; ++i )
    {
        if ( m_gridLinePSOs[i].pso )
        {
            m_gridLinePSOs[i].pso->Release();
            m_gridLinePSOs[i].pso = nullptr;
        }
    }
    m_gridLinePSOCount = 0;
    m_gridLineShader.reset();
    for ( std::unique_ptr<IShader>& shader : m_transientTriangleShaders )
    {
        shader.reset();
    }

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
    m_commandQueue = nullptr;
    for ( int i = 0; i < FRAME_COUNT; ++i )
    {
        m_commandAllocators[i] = nullptr;
        m_frameFenceValues[i] = 0;
    }
    m_allocatorIndex = 0;
    m_frameIndex = 0;
    m_allowTearing = false;
}


// --- Frame Management ---


SkullbonezCore::Basics::SbResult RenderBackendDX12::Present()
{
    SkullbonezCore::Basics::SbResult stateResult = EnsureCommandListOpen();
    if ( !stateResult.ok )
    {
        return stateResult;
    }

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
            CommandList()->ResolveQueryData( m_gpuTimers.queryHeap,
                                             D3D12_QUERY_TYPE_TIMESTAMP,
                                             (UINT)start,
                                             (UINT)( i - start ),
                                             m_gpuTimers.readback.Resource(),
                                             byteOffset );
            resolvedTimerSlotsThisFrame = true;
        }
        std::memset( m_gpuTimers.slotWritten, 0, sizeof( m_gpuTimers.slotWritten ) );
    }

    TransitionBackbuffer( "PresentBackbuffer", RenderGraphResourceAccess::Present );
    if ( m_commandRecording.HasFailure() )
    {
        return m_commandRecording.CurrentResult();
    }

    // Close the command list — finalizes the recorded commands. A closed command list can be
    // submitted to the GPU. No more commands can be recorded until Reset is called.
    // Docs: https://learn.microsoft.com/en-us/windows/win32/api/d3d12/nf-d3d12-id3d12graphicscommandlist-close
    AssertPlatformProfilerGpuStackClosed( "Present" );
    stateResult = m_commandRecording.CommitClose( CommandList()->Close(), "Present command list Close" );
    if ( !stateResult.ok )
    {
        return stateResult;
    }

    // Submit the completed command list to the GPU for execution. The GPU processes commands
    // asynchronously — this call returns immediately while the GPU works in the background.
    // Docs: https://learn.microsoft.com/en-us/windows/win32/api/d3d12/nf-d3d12-id3d12commandqueue-executecommandlists
    SubmitClosedCommandList();

    // Present the frame — flips the swap chain to show the just-rendered back buffer on screen.
    // Sync interval is configurable so perf scenes can disable V-Sync while visual scenes keep it.
    // Docs: https://learn.microsoft.com/en-us/windows/win32/api/dxgi/nf-dxgi-idxgiswapchain-present
    const UINT syncInterval = m_isVsyncEnabled ? 1u : 0u;
    const UINT presentFlags = ( !m_isVsyncEnabled && m_allowTearing ) ? DXGI_PRESENT_ALLOW_TEARING : 0u;
    const HRESULT presentResult = SwapChain()->Present( syncInterval, presentFlags );
    if ( IsDx12DeviceLostResult( presentResult ) )
    {
        ReportDeviceLost( "Present", presentResult );
        return m_commandRecording.RetainFailure( SkullbonezCore::Basics::SbResult::Failure(
            "Rendering/DX12",
            "DX12 device lost during Present; see dx12_device_lost.txt (HRESULT 0x%08X)",
            static_cast<unsigned int>( presentResult ) ) );
    }
    const SkullbonezCore::Basics::SbResult presentFailure =
        Dx12BackendOperationResult( presentResult, "SwapChain Present failed" );
    if ( !presentFailure.ok )
    {
        return m_commandRecording.RetainFailure( presentFailure );
    }

    // Signal the fence with the current frame's value. When the GPU reaches
    // this point in its command stream, it updates the fence to that value.
    // Later, EnsureCommandListOpen asks the timeline helper whether this value
    // has completed before reusing this frame's command allocator, upload arena,
    // and transient descriptor range.
    UINT64 presentFenceValue = 0;
    const SkullbonezCore::Basics::SbResult signalResult = m_renderDevice.FrameFence().Signal( presentFenceValue );
    m_submittedWork.CommitSignal( signalResult, presentFenceValue );
    if ( !signalResult.ok )
    {
        return m_commandRecording.RetainFailure( signalResult );
    }
    m_frameFenceValues[m_allocatorIndex] = presentFenceValue;
    AssignDeferredResourceReleaseFence( m_frameFenceValues[m_allocatorIndex] );

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
    m_backBufferAccess = RenderGraphResourceAccess::Present;

    // Charge allocator/upload/descriptor pacing to Present/VsyncWait instead of
    // letting the first render command of the next frame hit this wait mid-frame.
    const UINT64 nextFrameFenceValue = m_frameFenceValues[m_allocatorIndex];
    if ( nextFrameFenceValue > m_renderDevice.FrameFence().CompletedValue() )
    {
        const SkullbonezCore::Basics::SbResult waitResult =
            m_renderDevice.FrameFence().WaitForValue( nextFrameFenceValue );
        m_submittedWork.CommitWait( waitResult, nextFrameFenceValue );
        if ( !waitResult.ok )
        {
            return m_commandRecording.CommitWait( waitResult );
        }
    }
    ReleaseCompletedDeferredResources( false );
    return SkullbonezCore::Basics::SbResult::Success();
}


void RenderBackendDX12::SetVsyncEnabled( bool enabled )
{
    m_isVsyncEnabled = enabled;
}


bool RenderBackendDX12::IsVsyncEnabled() const
{
    return m_isVsyncEnabled;
}


SkullbonezCore::Basics::SbResult RenderBackendDX12::Finish()
{
    if ( m_commandRecording.HasFailure() )
    {
        return m_commandRecording.CurrentResult();
    }

    if ( !CommandList() || !m_commandQueue || !m_renderDevice.FrameFence().IsReady() ||
         !m_commandAllocators[m_allocatorIndex] )
    {
        const SkullbonezCore::Basics::SbResult waitResult = m_commandRecording.CommitWait( WaitForGpu() );
        if ( !waitResult.ok )
        {
            return waitResult;
        }
        TryConsumeGpuTimerReadback( true );
        return SkullbonezCore::Basics::SbResult::Success();
    }

    if ( m_commandRecording.IsOpen() )
    {
        AssertPlatformProfilerGpuStackClosed( "Finish" );
        const SkullbonezCore::Basics::SbResult closeResult =
            m_commandRecording.CommitClose( CommandList()->Close(), "Finish command list Close" );
        if ( !closeResult.ok )
        {
            return closeResult;
        }
        SubmitClosedCommandList();
    }
    const SkullbonezCore::Basics::SbResult waitResult = m_commandRecording.CommitWait( WaitForGpu() );
    if ( !waitResult.ok )
    {
        return waitResult;
    }
    TryConsumeGpuTimerReadback( true );

    // Hazard: runtime pipeline-sync calls Finish() between physics and render.
    // That wait is allowed to drain submitted GPU work, but the next render pass
    // still expects a recording command list for explicit barriers and draws.
    return EnsureCommandListOpen();
}


SkullbonezCore::Basics::SbResult RenderBackendDX12::FlushGPU()
{
    if ( m_commandRecording.HasFailure() )
    {
        return m_commandRecording.CurrentResult();
    }

    if ( !CommandList() || !m_commandQueue || !m_renderDevice.FrameFence().IsReady() ||
         !m_commandAllocators[m_allocatorIndex] )
    {
        // Lane R: an active resource-mutation drain cannot claim success unless
        // it can both wait for submitted work and reopen the recording epoch.
        return m_commandRecording.RetainFailure( SkullbonezCore::Basics::SbResult::Failure(
            "Rendering/DX12",
            "FlushGPU requires a complete command queue, fence, allocator, and command list." ) );
    }

    Dx12GpuDrainProgress drainProgress( m_commandRecording.IsOpen() );
    if ( drainProgress.RequiresClose() )
    {
        AssertPlatformProfilerGpuStackClosed( "FlushGPU" );
        const SkullbonezCore::Basics::SbResult closeResult =
            m_commandRecording.CommitClose( CommandList()->Close(), "FlushGPU command list Close" );
        if ( !closeResult.ok )
        {
            return closeResult;
        }
        if ( !drainProgress.CommitClose() || !drainProgress.CanSubmit() )
        {
            // Lane F: this local sequence can advance only after the successful
            // Close above; disagreement means the engine's ordering proof broke.
            SB_FATAL( "RenderBackendDX12", "FlushGPU drain order rejected a successful command-list Close." );
        }

        SubmitClosedCommandList();
        if ( !drainProgress.CommitSubmission() )
        {
            SB_FATAL( "RenderBackendDX12", "FlushGPU drain order rejected command-list submission." );
        }
    }

    if ( !drainProgress.CanWait() )
    {
        SB_FATAL( "RenderBackendDX12", "FlushGPU attempted a fence wait before submission ordering completed." );
    }

    // Hazard: ExecuteCommandLists has no success result. SubmitClosedCommandList
    // already marked the work live; if this drain fence fails, both the sticky
    // Lane R result and m_submittedWork block mutation, reuse, and unfenced release.
    const SkullbonezCore::Basics::SbResult waitResult = m_commandRecording.CommitWait( WaitForGpu() );
    if ( !waitResult.ok )
    {
        return waitResult;
    }
    if ( !drainProgress.CommitWait() || !drainProgress.CanReopen() )
    {
        SB_FATAL( "RenderBackendDX12", "FlushGPU drain order rejected a successful fence wait." );
    }

    // Hazard: scene swaps and graphics stress call this in the middle of the
    // runtime loop. Reopen only after the full wait, and return its failure so
    // no caller treats a closed/failed epoch as permission to destroy resources.
    const SkullbonezCore::Basics::SbResult reopenResult = EnsureCommandListOpen();
    if ( !reopenResult.ok )
    {
        return reopenResult;
    }
    if ( !drainProgress.CommitReopen() || !drainProgress.IsMutationSafe() )
    {
        SB_FATAL( "RenderBackendDX12", "FlushGPU drain order rejected a successful command-list reopen." );
    }
    return SkullbonezCore::Basics::SbResult::Success();
}


SkullbonezCore::Basics::SbResult RenderBackendDX12::Resize( int width, int height )
{
    if ( width <= 0 || height <= 0 )
    {
        return SkullbonezCore::Basics::SbResult::Success();
    }

    // Lane R: Resize replaces back buffers and depth memory. FlushGPU closes and
    // submits any open list, proves all queue work complete, and reopens an empty
    // recording epoch. Do not release the first old resource on any failure.
    const SkullbonezCore::Basics::SbResult drainResult = FlushGPU();
    if ( !drainResult.ok )
    {
        return drainResult;
    }

    for ( int i = 0; i < FRAME_COUNT; ++i )
    {
        m_renderTargets[i]->Release();
        m_renderTargets[i] = nullptr;
    }
    m_depthStencil->Release();
    m_depthStencil = nullptr;

    const UINT resizeFlags = m_allowTearing ? DXGI_SWAP_CHAIN_FLAG_ALLOW_TEARING : 0u;
    const HRESULT resizeResult =
        SwapChain()->ResizeBuffers( FRAME_COUNT, (UINT)width, (UINT)height, DXGI_FORMAT_R8G8B8A8_UNORM, resizeFlags );
    if ( IsDx12DeviceLostResult( resizeResult ) )
    {
        ReportDeviceLost( "ResizeBuffers", resizeResult );
        return SkullbonezCore::Basics::SbResult::Failure( "Rendering/DX12",
                                                          "DX12 device lost during ResizeBuffers; see "
                                                          "dx12_device_lost.txt (HRESULT 0x%08X)",
                                                          static_cast<unsigned int>( resizeResult ) );
    }
    const SkullbonezCore::Basics::SbResult resizeFailure =
        Dx12BackendOperationResult( resizeResult, "SwapChain ResizeBuffers failed" );
    if ( !resizeFailure.ok )
    {
        return resizeFailure;
    }
    m_frameIndex = m_renderDevice.RefreshFrameIndexFromSwapChain();

    // ResizeBuffers puts all back buffers into PRESENT state, so the next
    // Clear()/PrepareDraw() must transition from that concrete state.
    m_backBufferAccess = RenderGraphResourceAccess::Present;

    for ( int i = 0; i < FRAME_COUNT; ++i )
    {
        const SkullbonezCore::Basics::SbResult backBufferResult =
            Dx12BackendOperationResult( SwapChain()->GetBuffer( (UINT)i, IID_PPV_ARGS( &m_renderTargets[i] ) ),
                                        "SwapChain GetBuffer after resize failed" );
        if ( !backBufferResult.ok )
        {
            return backBufferResult;
        }
        NameDx12ObjectIndexed( m_renderTargets[i], L"Skullbonez DX12 Swapchain Backbuffer", (UINT)i );
        Device()->CreateRenderTargetView( m_renderTargets[i], nullptr, m_backBufferRTVs[i] );
    }

    const SkullbonezCore::Basics::SbResult depthStencilResult = CreateDepthStencil( width, height );
    if ( !depthStencilResult.ok )
    {
        return depthStencilResult;
    }

    m_width = width;
    m_height = height;
    m_viewport = { 0.0f, 0.0f, (float)width, (float)height, 0.0f, 1.0f };
    m_scissorRect = { 0, 0, (LONG)width, (LONG)height };
    m_currentRTV = m_backBufferRTVs[m_frameIndex];
    m_currentDSV = m_mainDSV;
    return SkullbonezCore::Basics::SbResult::Success();
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
    if ( !EnsureCommandListOpen().ok )
    {
        return;
    }

    if ( !m_renderingToFBO )
    {
        TransitionBackbuffer( "ClearBackbuffer", RenderGraphResourceAccess::RenderTarget );
        if ( m_commandRecording.HasFailure() )
        {
            return;
        }
    }
    // Bind the render target and depth buffer to the Output Merger (OM) stage — this tells the
    // GPU where to write pixel colors and depth values for subsequent draw calls.
    // Docs:
    // https://learn.microsoft.com/en-us/windows/win32/api/d3d12/nf-d3d12-id3d12graphicscommandlist-omsetrendertargets
    CommandList()->OMSetRenderTargets( 1, &m_currentRTV, FALSE, &m_currentDSV );

    // Viewport defines where rendering appears, and the scissor rect clips pixels
    // (pixels outside the scissor are clipped/discarded). Both must be set every time in DX12.
    // Docs: https://learn.microsoft.com/en-us/windows/win32/api/d3d12/nf-d3d12-id3d12graphicscommandlist-rssetviewports
    // Docs:
    // https://learn.microsoft.com/en-us/windows/win32/api/d3d12/nf-d3d12-id3d12graphicscommandlist-rssetscissorrects
    CommandList()->RSSetViewports( 1, &m_viewport );
    CommandList()->RSSetScissorRects( 1, &m_scissorRect );

    if ( color )
    {
        // Clear the render target to a solid color (wipes the entire back buffer).
        // Docs:
        // https://learn.microsoft.com/en-us/windows/win32/api/d3d12/nf-d3d12-id3d12graphicscommandlist-clearrendertargetview
        CommandList()->ClearRenderTargetView( m_currentRTV, m_clearColor, 0, nullptr );
    }
    if ( depth )
    {
        // Clear the depth buffer to 1.0 (maximum distance), so all subsequent draws will pass
        // the depth test. This is done at the start of each frame or when switching render targets.
        // Docs:
        // https://learn.microsoft.com/en-us/windows/win32/api/d3d12/nf-d3d12-id3d12graphicscommandlist-cleardepthstencilview
        CommandList()->ClearDepthStencilView( m_currentDSV, D3D12_CLEAR_FLAG_DEPTH, m_clearDepth, 0, 0, nullptr );
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
// GPU-side mip generation uses a compute PSO separate from raster draw PSOs.
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


// Per-vertex colored line data is interleaved [x,y,z,r,g,b] per vertex (6 floats each).
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


bool RenderBackendDX12::IsCullFaceEnabled() const
{
    return m_cullEnabled;
}


void RenderBackendDX12::GetBlendFunc( BlendFactor& outSrc, BlendFactor& outDst ) const
{
    outSrc = m_blendSrc;
    outDst = m_blendDst;
}


// --- DXR Raytracing ---


// --- GPU Timers ---
