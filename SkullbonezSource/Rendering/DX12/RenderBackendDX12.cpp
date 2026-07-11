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
  Platform profiler GPU stack: Fixed nesting state that must be closed before
  any command list is submitted.

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
#include "../ShaderReflectionContracts.h"
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
namespace Basics = SkullbonezCore::Basics;


Dx12FrameOwner::Dx12FrameOwner( Dx12RenderDevice& device, Dx12PipelineOwner& pipeline, Dx12TextureOwner& textures )
    : m_device( device ), m_pipeline( pipeline ), m_textures( textures ), m_drawGate( *this ),
      m_uploadReservations( *this ), m_resourceRelease( *this )
{
}


void Dx12FrameOwner::ResetForDevice()
{
    m_recording.ResetForDevice();
    m_submittedWork.ResetForDevice();
    m_deviceHealth.ResetForDevice();
    m_profilerStackState.Reset();
    m_profilerScopes.fill( Dx12PlatformProfilerGpuScopeDX12() );
    for ( UINT64& value : m_frameFenceValues )
    {
        value = 0;
    }
    m_allocatorIndex = m_device.AllocatorIndex();
    m_frameIndex = m_device.FrameIndex();
    m_backBufferAccess = RenderGraphResourceAccess::Present;
    CancelPendingConstantUpload();
}


void Dx12FrameOwner::ResetAfterShutdown()
{
    m_profilerStackState.Reset();
    m_profilerScopes.fill( Dx12PlatformProfilerGpuScopeDX12() );
    for ( UINT64& value : m_frameFenceValues )
    {
        value = 0;
    }
    m_allocatorIndex = 0;
    m_frameIndex = 0;
    m_srvHeap = nullptr;
    m_backBufferAccess = RenderGraphResourceAccess::Present;
    CancelPendingConstantUpload();
}


Basics::SbResult Dx12FrameOwner::EnsureOpen()
{
    if ( !m_deviceHealth.CanIssueDeviceWork() )
    {
        return m_recording.RetainFailure( m_deviceHealth.CurrentResult() );
    }
    if ( m_recording.HasFailure() )
    {
        return m_recording.CurrentResult();
    }
    ID3D12CommandAllocator* allocator = m_device.CommandAllocator( m_allocatorIndex );
    if ( !m_device.CommandList() || !m_device.GraphicsQueue() || !m_device.FrameFence().IsReady() || !allocator )
    {
        SB_FATAL( "Dx12FrameOwner", "Draw epoch requires device queue, fence, allocator, and command list." );
    }
    if ( m_recording.IsOpen() )
    {
        return Basics::SbResult::Success();
    }

    const UINT64 completed = m_device.FrameFence().CompletedValue();
    m_submittedWork.ObserveCompletedFence( completed );
    if ( m_submittedWork.HasUnfencedOrUncertainWork() )
    {
        return m_recording.RetainFailure( Basics::SbResult::Failure(
            "Rendering/DX12",
            "Draw epoch blocked because submitted work lacks a trustworthy completion fence." ) );
    }
    const UINT64 allocatorFence = m_frameFenceValues[m_allocatorIndex];
    if ( allocatorFence > completed )
    {
        const Basics::SbResult wait = m_device.FrameFence().WaitForValue( allocatorFence );
        m_submittedWork.CommitWait( wait, allocatorFence );
        if ( !wait.ok )
        {
            return m_recording.CommitWait( wait );
        }
    }
    m_retirement.ReleaseCompleted( m_device, m_submittedWork, false );
    Basics::SbResult result = m_recording.CommitAllocatorReset( allocator->Reset(), "Dx12FrameOwner allocator Reset" );
    if ( !result.ok )
    {
        return result;
    }
    result = m_recording.CommitListReset( m_device.CommandList()->Reset( allocator, nullptr ),
                                          "Dx12FrameOwner command-list Reset" );
    if ( !result.ok )
    {
        return result;
    }
    ID3D12DescriptorHeap* heaps[] = { m_srvHeap };
    m_device.CommandList()->SetDescriptorHeaps( 1, heaps );
    m_device.CommandList()->SetGraphicsRootSignature( m_pipeline.RootSignature() );
    m_uploads.ResetFrame( m_allocatorIndex );
    m_descriptors.ResetFrame( m_allocatorIndex );
    m_pipeline.InvalidateCommandState();
    m_textures.InvalidateBindings();
    return Basics::SbResult::Success();
}


bool Dx12FrameOwner::PrepareDraw()
{
    if ( !EnsureOpen().ok )
    {
        return false;
    }
    if ( !m_pipeline.RenderingToFramebuffer() && m_backBufferAccess != RenderGraphResourceAccess::RenderTarget )
    {
        Dx12RenderGraphSingleTransitionDesc desc;
        desc.commandList = m_device.CommandList();
        desc.resource = m_renderTargets[m_frameIndex];
        desc.before = m_backBufferAccess;
        desc.after = RenderGraphResourceAccess::RenderTarget;
        const Dx12RenderGraphBarrierRecord record = ExecuteDx12RenderGraphSingleTransition( "Dx12Explicit",
                                                                                            "PrepareDrawBackbuffer",
                                                                                            "SwapchainBackbuffer",
                                                                                            desc );
        if ( !record.emitted )
        {
            SB_FATAL( "Dx12FrameOwner", "Draw backbuffer transition did not emit." );
        }
        m_backBufferAccess = RenderGraphResourceAccess::RenderTarget;
        m_pipeline.InvalidateTargets();
    }
    return !m_recording.HasFailure();
}


bool Dx12FrameOwner::PrepareFramebufferBind()
{
    return EnsureOpen().ok;
}


int Dx12FrameOwner::SuspendProfilerForSubmit( const char* reason )
{
#if SKULLBONEZ_PLATFORM_PROFILER_HAVE_PIX3
    const int depth = m_profilerStackState.Depth();
    if ( depth <= 0 )
    {
        return 0;
    }
    if ( !CommandList() || !m_recording.CanRecord() )
    {
        Log().WriteEventf( "dx12_platform_profiler_gpu_suspend_without_open_command_list reason=%s depth=%d",
                           reason ? reason : "unknown",
                           depth );
        return 0;
    }
    Log().WriteEventf( "dx12_platform_profiler_gpu_stack_suspended_for_submit reason=%s depth=%d",
                       reason ? reason : "unknown",
                       depth );
    for ( int i = depth - 1; i >= 0; --i )
    {
        PIXEndEvent( m_device.CommandList() );
    }
    const int committedDepth = m_profilerStackState.SuspendForSubmit();
    if ( committedDepth != depth )
    {
        SB_FATAL( "Dx12FrameOwner", "DX12 platform profiler suspend depth changed during submission." );
    }
    return committedDepth;
#else
    (void)reason;
    return 0;
#endif
}


void Dx12FrameOwner::RestoreProfilerAfterSubmit( int depth )
{
#if SKULLBONEZ_PLATFORM_PROFILER_HAVE_PIX3
    if ( !m_recording.CanRecord() )
    {
        Log().WriteEventf( "dx12_platform_profiler_gpu_restore_bookkeeping_only depth=%d", depth );
        if ( !m_profilerStackState.RestoreAfterSubmit( depth, PROFILER_STACK_CAPACITY ) )
        {
            SB_FATAL( "Dx12FrameOwner", "Profiler bookkeeping restore exceeded fixed capacity." );
        }
        return;
    }
    for ( int i = 0; i < depth; ++i )
    {
        const Dx12PlatformProfilerGpuScopeDX12& scope = m_profilerScopes[i];
        const char* markerName = scope.name[0] != '\0' ? scope.name : "(null)";
        PIXBeginEvent( m_device.CommandList(),
                       Basics::PlatformProfiler::ColorForMarker( markerName, scope.hash ),
                       "%s",
                       markerName );
    }
    if ( !m_profilerStackState.RestoreAfterSubmit( depth, PROFILER_STACK_CAPACITY ) )
    {
        SB_FATAL( "Dx12FrameOwner", "Profiler stack restore exceeded fixed capacity." );
    }
#else
    (void)depth;
#endif
}


void Dx12FrameOwner::AssertProfilerClosed( const char* reason ) const
{
    if ( m_profilerStackState.Depth() == 0 )
    {
        return;
    }
    Log().WriteEventf( "dx12_platform_profiler_open_stack_on_submit reason=%s depth=%d",
                       reason ? reason : "unknown",
                       m_profilerStackState.Depth() );
    assert( m_profilerStackState.Depth() == 0 );
    SB_FATAL( "Dx12FrameOwner",
              "DX12 platform profiler GPU stack left open before command submission. reason=%s depth=%d",
              reason ? reason : "unknown",
              m_profilerStackState.Depth() );
}


void Dx12FrameOwner::BeginProfilerEvent( const char* name, uint32_t hash )
{
#if SKULLBONEZ_PLATFORM_PROFILER_HAVE_PIX3
    if ( !CommandList() || !EnsureOpen().ok )
    {
        return;
    }
    if ( m_profilerStackState.Depth() >= PROFILER_STACK_CAPACITY )
    {
        SB_FATAL( "Dx12FrameOwner",
                  "DX12 platform profiler GPU stack overflow. depth=%d capacity=%d",
                  m_profilerStackState.Depth(),
                  PROFILER_STACK_CAPACITY );
    }
    char gpuMarkerName[Basics::PlatformProfiler::MAX_DECORATED_MARKER_NAME_CHARS];
    const char* markerName =
        Basics::PlatformProfiler::AreDetailedRangesEnabled()
            ? Basics::PlatformProfiler::DecorateMarkerName( name, "_GPU", gpuMarkerName, sizeof( gpuMarkerName ) )
            : name;
    Dx12PlatformProfilerGpuScopeDX12& scope = m_profilerScopes[m_profilerStackState.Depth()];
    _snprintf_s( scope.name, sizeof( scope.name ), _TRUNCATE, "%s", markerName ? markerName : "(null)" );
    scope.hash = hash;
    PIXBeginEvent( CommandList(), Basics::PlatformProfiler::ColorForMarker( markerName, hash ), "%s", markerName );
    if ( !m_profilerStackState.CommitBegin( PROFILER_STACK_CAPACITY ) )
    {
        SB_FATAL( "Dx12FrameOwner", "DX12 platform profiler begin did not commit after capacity check." );
    }
#else
    (void)name;
    (void)hash;
#endif
}


void Dx12FrameOwner::EndProfilerEvent()
{
#if SKULLBONEZ_PLATFORM_PROFILER_HAVE_PIX3
    if ( m_profilerStackState.Depth() <= 0 )
    {
        if ( Basics::PlatformProfiler::IsEnabled() )
        {
            Log().WriteEventf( "dx12_platform_profiler_gpu_end_without_begin" );
        }
        return;
    }
    if ( !CommandList() || !m_recording.CanRecord() )
    {
        Log().WriteEventf( "dx12_platform_profiler_gpu_end_bookkeeping_only depth=%d", m_profilerStackState.Depth() );
        if ( !m_profilerStackState.CommitEnd() )
        {
            SB_FATAL( "Dx12FrameOwner", "DX12 platform profiler bookkeeping end lost its open scope." );
        }
        m_profilerScopes[m_profilerStackState.Depth()] = Dx12PlatformProfilerGpuScopeDX12();
        return;
    }
    PIXEndEvent( CommandList() );
    if ( !m_profilerStackState.CommitEnd() )
    {
        SB_FATAL( "Dx12FrameOwner", "DX12 platform profiler end lost its open scope." );
    }
    m_profilerScopes[m_profilerStackState.Depth()] = Dx12PlatformProfilerGpuScopeDX12();
#endif
}


Basics::SbResult Dx12FrameOwner::SubmitClosed()
{
    if ( m_recording.HasFailure() )
    {
        if ( m_faultInjection.WasInjected() )
        {
            [[maybe_unused]] const Basics::SbResult blocked = m_faultInjection.BeforeSubmission();
            WriteFaultProbe();
        }
        return m_recording.CurrentResult();
    }
    if ( !m_device.CommandList() || !m_device.GraphicsQueue() || !m_recording.IsClosed() )
    {
        SB_FATAL( "Dx12FrameOwner", "Upload flush submission requires a healthy closed command list." );
    }
    const Basics::SbResult injected = m_faultInjection.BeforeSubmission();
    if ( !injected.ok )
    {
        const Basics::SbResult retained = m_recording.RetainFailure( injected );
        Log().WriteEventf( "dx12_fault_injected point=before-first-submit submissions=%u",
                           m_faultInjection.SubmissionCount() );
        fprintf( stderr,
                 "[dx12-fault] owner=%s reason=\"%s\" submissions=%u\n",
                 retained.error.owner,
                 retained.error.message,
                 m_faultInjection.SubmissionCount() );
        fflush( stderr );
        Log().FlushAll();
        WriteFaultProbe();
        return retained;
    }
    ID3D12CommandList* lists[] = { m_device.CommandList() };
    m_device.GraphicsQueue()->ExecuteCommandLists( 1, lists );
    m_faultInjection.CommitSubmission();
    m_submittedWork.MarkSubmitted();
    return Basics::SbResult::Success();
}


Basics::SbResult Dx12FrameOwner::WaitForGpu()
{
    if ( !m_device.FrameFence().IsReady() )
    {
        if ( m_submittedWork.HasSubmittedWork() )
        {
            const Basics::SbResult unavailable = Basics::SbResult::Failure(
                "Rendering/DX12",
                "DX12 fence timeline unavailable while submitted GPU work remains unproven." );
            m_submittedWork.CommitWait( unavailable, 0 );
            return unavailable;
        }
        ReleaseCompletedRetirements( !m_recording.IsOpen() );
        return Basics::SbResult::Success();
    }
    UINT64 fence = 0;
    const Basics::SbResult signal = m_device.FrameFence().Signal( fence );
    m_submittedWork.CommitSignal( signal, fence );
    if ( !signal.ok )
    {
        return signal;
    }
    const Basics::SbResult wait = m_device.FrameFence().WaitForValue( fence );
    m_submittedWork.CommitWait( wait, fence );
    if ( !wait.ok )
    {
        return wait;
    }
    if ( m_submittedWork.HasSubmittedWork() )
    {
        SB_FATAL( "Dx12FrameOwner",
                  "Submitted work remained live after successful upload-flush drain. fence=%llu",
                  static_cast<unsigned long long>( fence ) );
    }
    for ( int i = 0; i < FRAME_COUNT; ++i )
    {
        m_frameFenceValues[i] = 0;
    }
    ReleaseCompletedRetirements( !m_recording.IsOpen() );
    return Basics::SbResult::Success();
}


Basics::SbResult Dx12FrameOwner::FlushUploadBuffer()
{
    const int profilerDepth = SuspendProfilerForSubmit( "FlushUploadBuffer" );
    AssertProfilerClosed( "FlushUploadBuffer" );
    Basics::SbResult result =
        m_recording.CommitClose( m_device.CommandList()->Close(), "Dx12FrameOwner upload flush Close" );
    if ( result.ok )
    {
        result = SubmitClosed();
    }
    if ( result.ok )
    {
        result = m_recording.CommitWait( WaitForGpu() );
    }
    if ( !result.ok )
    {
        RestoreProfilerAfterSubmit( profilerDepth );
        return result;
    }
    ID3D12CommandAllocator* allocator = m_device.CommandAllocator( m_allocatorIndex );
    result = m_recording.CommitAllocatorReset( allocator->Reset(), "Dx12FrameOwner upload flush allocator Reset" );
    if ( result.ok )
    {
        result = m_recording.CommitListReset( m_device.CommandList()->Reset( allocator, nullptr ),
                                              "Dx12FrameOwner upload flush list Reset" );
    }
    if ( !result.ok )
    {
        RestoreProfilerAfterSubmit( profilerDepth );
        return result;
    }
    ID3D12DescriptorHeap* heaps[] = { m_srvHeap };
    m_device.CommandList()->SetDescriptorHeaps( 1, heaps );
    m_device.CommandList()->SetGraphicsRootSignature( m_pipeline.RootSignature() );
    m_uploads.ResetFrame( m_allocatorIndex );
    m_descriptors.ResetFrame( m_allocatorIndex );
    m_pipeline.InvalidateCommandState();
    m_textures.InvalidateBindings();
    RestoreProfilerAfterSubmit( profilerDepth );
    return Basics::SbResult::Success();
}


D3D12_GPU_VIRTUAL_ADDRESS Dx12FrameOwner::ReserveUpload( UINT64 size, UINT64 alignment )
{
    if ( !EnsureOpen().ok )
    {
        return 0;
    }
    if ( !m_uploads.CanAllocate( m_allocatorIndex, size, alignment ) )
    {
        if ( !FlushUploadBuffer().ok || !m_uploads.CanAllocate( m_allocatorIndex, size, alignment ) )
        {
            return 0;
        }
    }
    return m_uploads.Allocate( m_allocatorIndex, size, alignment );
}


D3D12_GPU_VIRTUAL_ADDRESS Dx12FrameOwner::ReserveGeometryUpload( UINT64 vertexBytes, UINT64 constantBytes )
{
    if ( !EnsureOpen().ok || vertexBytes == 0 )
    {
        return 0;
    }
    m_pendingConstantAddress = 0;
    m_pendingConstantBytes = 0;
    // Hazard: a flush after either address is published invalidates both. Probe
    // the conservative combined aligned budget, flush at most once, then allocate
    // the constant row before the dependent vertex/instance bytes.
    const UINT64 combinedBudget = constantBytes + vertexBytes + 255u + 3u;
    if ( !m_uploads.CanAllocate( m_allocatorIndex, combinedBudget, 1 ) )
    {
        if ( !FlushUploadBuffer().ok || !m_uploads.CanAllocate( m_allocatorIndex, combinedBudget, 1 ) )
        {
            return 0;
        }
    }
    if ( constantBytes > 0 )
    {
        m_pendingConstantAddress = m_uploads.Allocate( m_allocatorIndex, constantBytes, 256 );
        m_pendingConstantBytes = constantBytes;
    }
    return m_uploads.Allocate( m_allocatorIndex, vertexBytes, 4 );
}


D3D12_GPU_VIRTUAL_ADDRESS Dx12FrameOwner::ReserveConstantUpload( UINT64 size )
{
    if ( m_pendingConstantAddress != 0 && m_pendingConstantBytes == size )
    {
        const D3D12_GPU_VIRTUAL_ADDRESS address = m_pendingConstantAddress;
        m_pendingConstantAddress = 0;
        m_pendingConstantBytes = 0;
        return address;
    }
    m_pendingConstantAddress = 0;
    m_pendingConstantBytes = 0;
    return ReserveUpload( size, 256 );
}


void Dx12FrameOwner::CancelPendingConstantUpload()
{
    m_pendingConstantAddress = 0;
    m_pendingConstantBytes = 0;
}


void Dx12FrameOwner::WriteFaultProbe() const
{
#ifdef _DEBUG
    if ( !m_faultInjection.IsArmed() )
    {
        return;
    }
    FILE* report = nullptr;
    if ( fopen_s( &report, "TestOutput/dx12_fault_injection.txt", "wb" ) != 0 || !report )
    {
        return;
    }
    fprintf( report,
             "point=before-first-submit injected=%d submissions=%u blocked_after_failure=%u\n",
             m_faultInjection.WasInjected() ? 1 : 0,
             m_faultInjection.SubmissionCount(),
             m_faultInjection.BlockedSubmissionCount() );
    fclose( report );
#endif
}


uint8_t* Dx12FrameOwner::UploadPointer( D3D12_GPU_VIRTUAL_ADDRESS address ) const
{
    return m_uploads.GetMappedPtr( m_allocatorIndex, address );
}


ID3D12Device* Dx12FrameOwner::Device() const
{
    return m_device.Device();
}
ID3D12GraphicsCommandList* Dx12FrameOwner::CommandList() const
{
    return m_device.CommandList();
}
void Dx12FrameOwner::ActivateShader( ShaderDX12* shader )
{
    m_pipeline.SetActiveShader( shader );
}


Basics::SbResult Dx12FrameOwner::CommitClose( HRESULT result, const char* operation )
{
    return m_recording.CommitClose( result, operation );
}


Basics::SbResult Dx12FrameOwner::CommitWait( const Basics::SbResult& result )
{
    return m_recording.CommitWait( result );
}


Basics::SbResult Dx12FrameOwner::RetainFailure( const Basics::SbResult& result )
{
    return m_recording.RetainFailure( result );
}


Basics::SbResult Dx12FrameOwner::RetainDeviceLoss( const char* operation, HRESULT result )
{
    return m_recording.RetainFailure( m_deviceHealth.RetainDeviceLoss( operation, result ) );
}


Basics::SbResult Dx12FrameOwner::SignalFrame( UINT64& outFenceValue )
{
    const Basics::SbResult result = m_device.FrameFence().Signal( outFenceValue );
    m_submittedWork.CommitSignal( result, outFenceValue );
    return result.ok ? result : m_recording.RetainFailure( result );
}


Basics::SbResult Dx12FrameOwner::WaitForFrameFence( UINT64 fenceValue )
{
    const Basics::SbResult result = m_device.FrameFence().WaitForValue( fenceValue );
    m_submittedWork.CommitWait( result, fenceValue );
    return result.ok ? result : m_recording.CommitWait( result );
}


void Dx12FrameOwner::AdvanceFrameIndices()
{
    m_allocatorIndex = m_device.AdvanceAllocatorIndex();
    m_frameIndex = m_device.RefreshFrameIndexFromSwapChain();
    m_backBufferAccess = RenderGraphResourceAccess::Present;
}


void Dx12FrameOwner::RefreshFrameIndex()
{
    m_frameIndex = m_device.RefreshFrameIndexFromSwapChain();
    m_backBufferAccess = RenderGraphResourceAccess::Present;
}


void Dx12FrameOwner::ReleaseCompletedRetirements( bool releaseUnfenced )
{
    m_retirement.ReleaseCompleted( m_device, m_submittedWork, releaseUnfenced );
}


void Dx12FrameOwner::RetireResource( ID3D12Resource* resource )
{
    if ( !resource )
    {
        return;
    }
    const bool fenceReady = m_device.FrameFence().IsReady();
    if ( fenceReady )
    {
        m_submittedWork.ObserveCompletedFence( m_device.FrameFence().CompletedValue() );
    }
    if ( ( !Device() || !fenceReady ) && !m_recording.IsOpen() && m_submittedWork.CanReleaseWithoutFence() )
    {
        resource->Release();
        return;
    }
    const UINT64 completedFence = fenceReady ? m_device.FrameFence().CompletedValue() : 0;
    bool hasOutstandingFrameWork = false;
    for ( const UINT64 frameFence : m_frameFenceValues )
    {
        hasOutstandingFrameWork = hasOutstandingFrameWork || frameFence > completedFence;
    }
    if ( !m_recording.IsOpen() && !hasOutstandingFrameWork && m_submittedWork.CanReleaseWithoutFence() )
    {
        resource->Release();
        return;
    }
    // Lifetime: only this frame owner may quarantine a resource because only
    // it can prove whether recording, submission, and covering fences are done.
    m_retirement.Quarantine( resource );
}


bool Dx12DrawGate::PrepareDraw()
{
    return m_owner.PrepareDraw();
}


bool Dx12DrawGate::PrepareFramebufferBind()
{
    return m_owner.PrepareFramebufferBind();
}


bool Dx12FrameOwner::PreparePipelineDraw( VertexFormat12 format,
                                          bool instanced,
                                          const InstancedMeshDX12* instancedMesh,
                                          const DynamicVBDX12* dynamicVertexBuffer )
{
    if ( !PrepareDraw() )
    {
        return false;
    }
    return m_pipeline.PrepareDraw( Device(),
                                   CommandList(),
                                   m_recording,
                                   m_textures,
                                   m_descriptors,
                                   format,
                                   instanced,
                                   instancedMesh,
                                   dynamicVertexBuffer );
}


bool Dx12DrawGate::PreparePipelineDraw( VertexFormat12 format,
                                        bool instanced,
                                        const InstancedMeshDX12* instancedMesh,
                                        const DynamicVBDX12* dynamicVertexBuffer )
{
    return m_owner.PreparePipelineDraw( format, instanced, instancedMesh, dynamicVertexBuffer );
}


bool Dx12DrawGate::CanRecord() const
{
    return m_owner.CanRecord();
}


D3D12_GPU_VIRTUAL_ADDRESS Dx12UploadReservations::ReserveUpload( UINT64 size, UINT64 alignment )
{
    return m_owner.ReserveUpload( size, alignment );
}


D3D12_GPU_VIRTUAL_ADDRESS Dx12UploadReservations::ReserveGeometryUpload( UINT64 vertexBytes, UINT64 constantBytes )
{
    return m_owner.ReserveGeometryUpload( vertexBytes, constantBytes );
}


D3D12_GPU_VIRTUAL_ADDRESS Dx12UploadReservations::ReserveConstantUpload( UINT64 size )
{
    return m_owner.ReserveConstantUpload( size );
}


void Dx12UploadReservations::CancelPendingConstantUpload()
{
    m_owner.CancelPendingConstantUpload();
}


uint8_t* Dx12UploadReservations::UploadPointer( D3D12_GPU_VIRTUAL_ADDRESS address ) const
{
    return m_owner.UploadPointer( address );
}


void Dx12ResourceRelease::Retire( ID3D12Resource* resource )
{
    m_owner.RetireResource( resource );
}


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


RenderBackendDX12::RenderBackendDX12() : m_frameOwner( m_renderDevice, m_pipelineOwner, m_textureOwner )
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
    const Dx12DescriptorAllocatorStats srvStats = m_frameOwner.Descriptors().GetStats();
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
        const Dx12UploadArenaStats uploadStats = m_frameOwner.Uploads().GetStats( static_cast<UINT>( frameIndex ) );
        stats.uploadCapacityBytes += uploadStats.capacityBytes;
        stats.uploadUsedBytes += uploadStats.usedBytes;
        stats.uploadPeakBytes += uploadStats.peakBytes;
    }

    const Dx12ReadbackBufferStats timerReadbackStats = m_gpuTimers.readback.GetStats();
    if ( timerReadbackStats.ready )
    {
        stats.timerReadbackBytes = timerReadbackStats.sizeBytes;
    }

    stats.textureRegistryCount = m_textureOwner.RegistryCount();
    stats.textureRegistryCapacity = m_textureOwner.RegistryCapacity();
    stats.dynamicVertexBufferCount = m_geometryOwner.DynamicCount();
    stats.dynamicVertexBufferCapacity = m_geometryOwner.DynamicCapacity();
    stats.instancedMeshCount = m_geometryOwner.InstancedCount();
    stats.instancedMeshCapacity = m_geometryOwner.InstancedCapacity();
    stats.psoCacheCount = m_pipelineOwner.CacheCount();
    stats.graphTransientCount = m_graphTransientResources.size();
    stats.graphTransientCapacity = m_graphTransientResources.capacity();

    if ( IDXGIFactory4* factory = m_renderDevice.Factory() )
    {
        // Why: multi-GPU machines can expose several adapters. Match the
        // device LUID instead of sampling adapter 0 so stress logs describe the
        // GPU actually backing this DX12 device.
        const LUID deviceLuid = Device()->GetAdapterLuid();
        ComPtr<IDXGIAdapter3> activeAdapter;
        for ( UINT adapterIndex = 0;; ++adapterIndex )
        {
            ComPtr<IDXGIAdapter1> adapter;
            const HRESULT enumResult = factory->EnumAdapters1( adapterIndex, adapter.GetAddressOf() );
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
    return m_frameOwner.WaitForGpu();
}


void RenderBackendDX12::ConfigureFaultInjection()
{
#ifdef _DEBUG
    char token[64] = {};
    const DWORD length =
        GetEnvironmentVariableA( "SKULLBONEZ_DX12_FAULT", token, static_cast<DWORD>( sizeof( token ) ) );
    m_frameOwner.ConfigureFaultInjection( length > 0 && length < sizeof( token ) ? token : nullptr );
#else
    m_frameOwner.ConfigureFaultInjection( nullptr );
#endif
}


SkullbonezCore::Basics::SbResult RenderBackendDX12::SubmitClosedCommandList()
{
    return m_frameOwner.SubmitClosed();
}


void Dx12DeferredReleaseOwner::Quarantine( ID3D12Resource* resource )
{
    if ( resource )
    {
        DeferredResourceReleaseDX12 retired;
        retired.resource = resource;
        m_pending.push_back( retired );
    }
}


void Dx12DeferredReleaseOwner::AssignFence( UINT64 fenceValue )
{
    if ( fenceValue == 0 )
    {
        return;
    }

    for ( DeferredResourceReleaseDX12& retired : m_pending )
    {
        if ( retired.resource && !retired.fenceAssigned )
        {
            retired.fenceValue = fenceValue;
            retired.fenceAssigned = true;
        }
    }
}


void Dx12DeferredReleaseOwner::ReleaseCompleted( Dx12RenderDevice& device,
                                                 Dx12SubmittedWorkState& submittedWork,
                                                 bool releaseUnfenced )
{
    const bool fenceReady = device.FrameFence().IsReady();
    const UINT64 completedFence = fenceReady ? device.FrameFence().CompletedValue() : 0;
    if ( fenceReady )
    {
        submittedWork.ObserveCompletedFence( completedFence );
    }
    if ( m_pending.empty() )
    {
        return;
    }

    const bool canReleaseUnfenced = releaseUnfenced && submittedWork.CanReleaseWithoutFence();
    size_t writeIndex = 0;
    for ( size_t readIndex = 0; readIndex < m_pending.size(); ++readIndex )
    {
        DeferredResourceReleaseDX12& retired = m_pending[readIndex];
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
            m_pending[writeIndex] = retired;
        }
        ++writeIndex;
    }

    m_pending.resize( writeIndex );
}


bool Dx12DeferredReleaseOwner::Empty() const
{
    return m_pending.empty();
}


size_t Dx12DeferredReleaseOwner::Count() const
{
    return m_pending.size();
}


void RenderBackendDX12::AssignDeferredResourceReleaseFence( UINT64 fenceValue )
{
    m_frameOwner.AssignRetirementFence( fenceValue );
}


void RenderBackendDX12::ReleaseCompletedDeferredResources( bool releaseUnfenced )
{
    m_frameOwner.ReleaseCompletedRetirements( releaseUnfenced );
}


void RenderBackendDX12::RetireResource( ID3D12Resource* resource )
{
    m_frameOwner.RetireResource( resource );
}


void RenderBackendDX12::AssertPlatformProfilerGpuStackClosed( const char* reason ) const
{
    m_frameOwner.AssertProfilerClosed( reason );
}


SkullbonezCore::Basics::SbResult RenderBackendDX12::EnsureCommandListOpen()
{
    return m_frameOwner.EnsureOpen();
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

    if ( !m_frameOwner.CanRecord() )
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
    ID3D12Resource* backbuffer = m_frameOwner.RenderTarget( m_frameOwner.FrameIndex() );
    if ( !backbuffer || m_frameOwner.BackBufferAccess() == after )
    {
        return false;
    }

    // Hazard: text-only or diagnostic frames can reach Present() without
    // Clear(), so the present barrier must start from the tracked state instead
    // of assuming the swap-chain image was rendered this frame.
    if ( !ExecuteGraphTransition( passName,
                                  "SwapchainBackbuffer",
                                  backbuffer,
                                  m_frameOwner.BackBufferAccess(),
                                  after ) )
    {
        return false;
    }
    m_frameOwner.SetBackBufferAccess( after );
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

    if ( !m_frameOwner.CanRecord() )
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
    return m_frameOwner.FlushUploadBuffer();
}


SkullbonezCore::Basics::SbResult RenderBackendDX12::FlushUploadBufferIfNeeded( UINT64 size, UINT64 alignment )
{
    if ( m_frameOwner.HasFailure() )
    {
        return m_frameOwner.CurrentResult();
    }
    if ( !m_frameOwner.Uploads().CanAllocate( m_frameOwner.AllocatorIndex(), size, alignment ) )
    {
        // This is the expensive fallback path. It means the CPU has filled this
        // frame's upload arena before the frame was submitted, so we must submit
        // the current command list and wait before reusing the same bytes. The
        // event log keeps this visible because frequent mid-frame flushes are a
        // sign that the future Dx12RenderDevice needs larger upload pages or a
        // different upload strategy for the current workload.
        const Dx12UploadArenaStats stats = m_frameOwner.Uploads().GetStats( m_frameOwner.AllocatorIndex() );
        Log().WriteEventf(
            "dx12_upload_arena_flush frame=%u used_bytes=%llu capacity_bytes=%llu requested_bytes=%llu alignment=%llu",
            m_frameOwner.AllocatorIndex(),
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
    const Dx12DescriptorAllocatorStats descriptorStats = m_frameOwner.Descriptors().GetStats();
    UINT64 uploadPeakBytes = 0;
    UINT64 uploadCapacityBytes = 0;
    for ( int i = 0; i < FRAME_COUNT; ++i )
    {
        const Dx12UploadArenaStats uploadStats = m_frameOwner.Uploads().GetStats( static_cast<UINT>( i ) );
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
    Log().WriteEventf( "dx12_render_architecture_stats reason=%s raster_contract=%s root_parameters=%u "
                       "raster_srv_slots=t%u..t%u "
                       "rtv_descriptors=%u/%u dsv_descriptors=%u/%u static_srvs=%u/%u transient_srv_peak=%u/%u "
                       "upload_peak_bytes=%llu upload_capacity_bytes=%llu",
                       reason ? reason : "unknown",
                       UnifiedRasterRootSignature::NAME,
                       UnifiedRasterRootSignature::ROOT_PARAMETER_COUNT,
                       UnifiedRasterRootSignature::SHADER_REGISTER_FIRST_TEXTURE,
                       UnifiedRasterRootSignature::SHADER_REGISTER_FIRST_TEXTURE +
                           static_cast<UINT>( UnifiedRasterRootSignature::TEXTURE_SLOT_COUNT - 1 ),
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
    m_savedGraphRTV = m_pipelineOwner.CurrentRTV();
    m_savedGraphDSV = m_pipelineOwner.CurrentDSV();
    m_savedGraphRTVFormat = m_pipelineOwner.RenderTargetFormat();
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
    m_pipelineOwner.RestoreRenderTargetFormat( m_savedGraphRTVFormat );
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


D3D12_GPU_VIRTUAL_ADDRESS RenderBackendDX12::ReserveUpload( UINT64 size, UINT64 alignment )
{
    return m_frameOwner.UploadReservations().ReserveUpload( size, alignment );
}


uint8_t* RenderBackendDX12::GetUploadPtr( D3D12_GPU_VIRTUAL_ADDRESS addr )
{
    return addr != 0 ? m_frameOwner.UploadReservations().UploadPointer( addr ) : nullptr;
}


UINT RenderBackendDX12::AllocateStaticSRV()
{
    return m_frameOwner.Descriptors().AllocateStatic();
}


UINT RenderBackendDX12::AllocateTransientSRV()
{
    return m_frameOwner.Descriptors().AllocateTransient();
}


UINT RenderBackendDX12::AllocateTransientSRVRange( UINT count )
{
    return m_frameOwner.Descriptors().AllocateTransientRange( count );
}


D3D12_CPU_DESCRIPTOR_HANDLE RenderBackendDX12::GetSRVStagingCpuHandle( UINT index )
{
    return m_frameOwner.Descriptors().StagingCpuHandle( index );
}


D3D12_GPU_DESCRIPTOR_HANDLE RenderBackendDX12::GetSRVGpuHandle( UINT index )
{
    return m_frameOwner.Descriptors().ShaderVisibleGpuHandle( index );
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
    m_frameOwner.ResetForDevice();
    ConfigureFaultInjection();

    // Invariant: the render device is the only owner and access path for its
    // factory, queue, allocators, swap chain, command list, and frame fence.
    // Backend initialization therefore cannot publish borrowed aliases before
    // all device objects exist, and rollback needs no separate rebind phase.
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
    m_frameOwner.Descriptors()
        .Init( m_srvHeap, m_srvStagingHeap, m_srvDescSize, MAX_STATIC_SRVS, MAX_TRANSIENT_SRVS, FRAME_COUNT );
    m_frameOwner.PublishSrvHeap( m_srvHeap );
    m_frameOwner.Descriptors().ResetFrame( m_frameOwner.AllocatorIndex() );

    // Cleared ordinary-raster texture slots still need a real descriptor table.
    // BindTexture(0) maps to this typed null SRV so shaders that sample an
    // intentionally empty slot read safe zero/default values instead of whatever
    // descriptor was previously bound to the root parameter.
    const UINT nullTextureSrvIndex = AllocateStaticSRV();
    m_textureOwner.SetNullSrvIndex( nullTextureSrvIndex );
    D3D12_SHADER_RESOURCE_VIEW_DESC nullTextureSrv = {};
    nullTextureSrv.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    nullTextureSrv.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
    nullTextureSrv.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    nullTextureSrv.Texture2D.MipLevels = 1;
    Device()->CreateShaderResourceView( nullptr, &nullTextureSrv, GetSRVStagingCpuHandle( nullTextureSrvIndex ) );

    // Lifetime: swap-chain images are replaced on resize, but the engine keeps
    // one stable RTV descriptor row per back buffer index. ResizeBuffers swaps
    // the image memory; CreateRenderTargetView overwrites the existing row with
    // a view record for the new image.
    // Docs: https://learn.microsoft.com/en-us/windows/win32/api/d3d12/nf-d3d12-id3d12device-createrendertargetview
    for ( int i = 0; i < FRAME_COUNT; ++i )
    {
        const SkullbonezCore::Basics::SbResult backBufferResult = Dx12BackendInitResult(
            SwapChain()->GetBuffer( (UINT)i, IID_PPV_ARGS( &m_frameOwner.RenderTarget( static_cast<UINT>( i ) ) ) ),
            "SwapChain GetBuffer failed" );
        if ( !backBufferResult.ok )
        {
            return backBufferResult;
        }
        NameDx12ObjectIndexed( m_frameOwner.RenderTarget( static_cast<UINT>( i ) ),
                               L"Skullbonez DX12 Swapchain Backbuffer",
                               (UINT)i );
        // Reserve one stable RTV row for each swap-chain buffer. ResizeBuffers
        // replaces the back-buffer resources later, but the descriptor rows stay
        // the same and are simply overwritten with new view records.
        m_backBufferRTVs[i] = m_rtvDescriptors.Allocate().cpuHandle;
        Device()->CreateRenderTargetView( m_frameOwner.RenderTarget( static_cast<UINT>( i ) ),
                                          nullptr,
                                          m_backBufferRTVs[i] );
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
    if ( !m_frameOwner.Uploads().Init( Device(),
                                       FRAME_COUNT,
                                       UPLOAD_BUFFER_SIZE,
                                       L"Skullbonez DX12 Frame Upload Buffer" ) )
    {
        return SkullbonezCore::Basics::SbResult::Failure(
            "Rendering/DX12",
            "DX12 frame upload buffer creation or persistent Map failed" );
    }

    const SkullbonezCore::Basics::SbResult rootSignatureResult = m_pipelineOwner.Initialize( Device() );
    if ( !rootSignatureResult.ok )
    {
        return rootSignatureResult;
    }
    const SkullbonezCore::Basics::SbResult genMipsResult = m_textureOwner.Initialize( *this );
    if ( !genMipsResult.ok )
    {
        return genMipsResult;
    }
    m_geometryOwner.AdoptGridLineShader( CreateShader( "shaders/grid_line" ) );
    if ( !m_geometryOwner.EnsureGridLinePipeline( Device(), m_pipelineOwner, DXGI_FORMAT_R8G8B8A8_UNORM ) ||
         !m_geometryOwner.EnsureGridLinePipeline( Device(), m_pipelineOwner, DXGI_FORMAT_R16G16B16A16_FLOAT ) )
    {
        return SkullbonezCore::Basics::SbResult::Failure( "Rendering/DX12", "DX12 required grid-line warmup failed" );
    }
    constexpr TransientTriangleStyle requiredTriangleStyles[] = {
        TransientTriangleStyle::Color,
        TransientTriangleStyle::SoftAdditiveRibbon,
        TransientTriangleStyle::TrajectoryRibbon,
        TransientTriangleStyle::TrajectoryRibbonDepthHint,
    };
    for ( const TransientTriangleStyle style : requiredTriangleStyles )
    {
        m_geometryOwner.AdoptTransientTriangleShader(
            style,
            CreateShader( Dx12GeometryOwner::TransientShaderBaseName( style ) ) );
        if ( !m_geometryOwner.HasTransientTriangleShader( style ) )
        {
            return SkullbonezCore::Basics::SbResult::Failure(
                "Rendering/DX12",
                "DX12 required transient-triangle shader warmup failed (style=%u)",
                static_cast<unsigned int>( style ) );
        }
    }

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
                    Dx12BackendInitResult( m_renderDevice.GraphicsQueue()->GetTimestampFrequency( &m_gpuTimers.freq ),
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

    m_pipelineOwner.SetViewport( { 0.0f, 0.0f, (float)width, (float)height, 0.0f, 1.0f },
                                 { 0, 0, (LONG)width, (LONG)height } );
    m_pipelineOwner.SetCurrentTargets( m_backBufferRTVs[m_frameOwner.FrameIndex()], m_mainDSV );
    // Publication boundary: callers observe dimensions only after every
    // required device, upload, pipeline, and framebuffer resource is ready.
    m_width = width;
    m_height = height;
    m_recreationGeneration = 1;

    return SkullbonezCore::Basics::SbResult::Success();
}


SkullbonezCore::Basics::SbResult Dx12PipelineOwner::Initialize( ID3D12Device* device )
{
    // Lifetime: initialization is reusable after a prior Shutdown or partial
    // startup failure. Desired state returns to cold defaults before publishing
    // a new root signature.
    ResetDesiredState();
    std::string reflectedContractError;
    if ( !ValidateGeneratedUnifiedRasterRootSignature( reflectedContractError ) )
    {
        // Lane R: checked-in DXIL is startup input. Reject a stale or incompatible
        // family before publishing a native root signature or any PSO that uses it.
        return SkullbonezCore::Basics::SbResult::Failure( "Dx12PipelineOwner",
                                                          "%s reflection rejected: %s",
                                                          UnifiedRasterRootSignature::NAME,
                                                          reflectedContractError.c_str() );
    }
    // Root signature mental model:
    //
    // A shader cannot freely access arbitrary C++ variables or texture objects.
    // The root signature is the contract that says which small set of bindings
    // the command list may provide and which register names the HLSL shader will
    // use to find them.
    //
    // UnifiedRaster is deliberately simple and shared by every raster family:
    //
    // - DrawConstants binds the per-draw matrices, colors, and scalar values.
    // - TEXTURE_SLOTS supplies one descriptor table for each named texture row;
    //   each table points at one transient SRV row prepared by the allocator.
    // - STATIC_SAMPLERS supplies the fixed filtering/addressing rules.
    //
    // The future render graph will not replace this shader contract. It will
    // decide when resources are safe to read/write and which pass binds them.
    D3D12_DESCRIPTOR_RANGE1 srvRanges[UnifiedRasterRootSignature::TEXTURE_SLOT_COUNT] = {};
    for ( int slot = 0; slot < UnifiedRasterRootSignature::TEXTURE_SLOT_COUNT; ++slot )
    {
        srvRanges[slot].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
        srvRanges[slot].NumDescriptors = 1;
        srvRanges[slot].BaseShaderRegister = UnifiedRasterRootSignature::TEXTURE_SLOTS[slot].shaderRegister;
        srvRanges[slot].RegisterSpace = UnifiedRasterRootSignature::REGISTER_SPACE;
        srvRanges[slot].OffsetInDescriptorsFromTableStart = 0;
    }

    D3D12_ROOT_PARAMETER1 params[UnifiedRasterRootSignature::ROOT_PARAMETER_COUNT] = {};
    params[UnifiedRasterRootSignature::ROOT_PARAMETER_DRAW_CONSTANTS].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
    params[UnifiedRasterRootSignature::ROOT_PARAMETER_DRAW_CONSTANTS].Descriptor.ShaderRegister =
        UnifiedRasterRootSignature::SHADER_REGISTER_DRAW_CONSTANTS;
    params[UnifiedRasterRootSignature::ROOT_PARAMETER_DRAW_CONSTANTS].Descriptor.RegisterSpace =
        UnifiedRasterRootSignature::REGISTER_SPACE;
    params[UnifiedRasterRootSignature::ROOT_PARAMETER_DRAW_CONSTANTS].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;

    for ( int slot = 0; slot < UnifiedRasterRootSignature::TEXTURE_SLOT_COUNT; ++slot )
    {
        const UINT rootParameter = UnifiedRasterRootSignature::TEXTURE_SLOTS[slot].rootParameter;
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
    samplers[0].ShaderRegister = UnifiedRasterRootSignature::STATIC_SAMPLERS[0].shaderRegister;
    samplers[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;

    // s1: linear clamp (FBO / reflection textures)
    samplers[1].Filter = D3D12_FILTER_MIN_MAG_MIP_LINEAR;
    samplers[1].AddressU = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
    samplers[1].AddressV = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
    samplers[1].AddressW = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
    samplers[1].MaxAnisotropy = 1;
    samplers[1].ComparisonFunc = D3D12_COMPARISON_FUNC_NEVER;
    samplers[1].MaxLOD = D3D12_FLOAT32_MAX;
    samplers[1].ShaderRegister = UnifiedRasterRootSignature::STATIC_SAMPLERS[1].shaderRegister;
    samplers[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;

    // s3: point clamp for manual shadow-map PCF.
    samplers[2].Filter = D3D12_FILTER_MIN_MAG_MIP_POINT;
    samplers[2].AddressU = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
    samplers[2].AddressV = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
    samplers[2].AddressW = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
    samplers[2].MaxAnisotropy = 1;
    samplers[2].ComparisonFunc = D3D12_COMPARISON_FUNC_NEVER;
    samplers[2].MaxLOD = D3D12_FLOAT32_MAX;
    samplers[2].ShaderRegister = UnifiedRasterRootSignature::STATIC_SAMPLERS[2].shaderRegister;
    samplers[2].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;

    D3D12_VERSIONED_ROOT_SIGNATURE_DESC rootSigDesc = {};
    rootSigDesc.Version = D3D_ROOT_SIGNATURE_VERSION_1_1;
    rootSigDesc.Desc_1_1.NumParameters = UnifiedRasterRootSignature::ROOT_PARAMETER_COUNT;
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
    const HRESULT rootSignatureResult = device->CreateRootSignature( 0,
                                                                     signature->GetBufferPointer(),
                                                                     signature->GetBufferSize(),
                                                                     IID_PPV_ARGS( &m_rootSignature ) );
    if ( FAILED( rootSignatureResult ) || !m_rootSignature )
    {
        if ( m_rootSignature )
        {
            m_rootSignature->Release();
            m_rootSignature = nullptr;
        }
        return SkullbonezCore::Basics::SbResult::Failure( "Rendering/DX12", "CreateRootSignature failed" );
    }
    NameDx12Object( m_rootSignature, L"Skullbonez DX12 UnifiedRaster Root Signature" );
#ifdef _DEBUG
    Log().WriteEventf(
        "dx12_raster_binding_contract name=%s root_parameters=%u cbv=b%u srv_slots=t%u..t%u material_table=t4 "
        "samplers=s%u,s%u,s%u bind_texture_slots=%d material_payload=packed_instance_params",
        UnifiedRasterRootSignature::NAME,
        UnifiedRasterRootSignature::ROOT_PARAMETER_COUNT,
        UnifiedRasterRootSignature::SHADER_REGISTER_DRAW_CONSTANTS,
        UnifiedRasterRootSignature::SHADER_REGISTER_FIRST_TEXTURE,
        UnifiedRasterRootSignature::SHADER_REGISTER_FIRST_TEXTURE +
            static_cast<UINT>( UnifiedRasterRootSignature::TEXTURE_SLOT_COUNT - 1 ),
        UnifiedRasterRootSignature::STATIC_SAMPLERS[0].shaderRegister,
        UnifiedRasterRootSignature::STATIC_SAMPLERS[1].shaderRegister,
        UnifiedRasterRootSignature::STATIC_SAMPLERS[2].shaderRegister,
        UnifiedRasterRootSignature::TEXTURE_SLOT_COUNT );
#endif
    return SkullbonezCore::Basics::SbResult::Success();
}


SkullbonezCore::Basics::SbResult RenderBackendDX12::CreateDepthStencil( int w, int h )
{
    ID3D12Resource* candidate = nullptr;
    const SkullbonezCore::Basics::SbResult createResult = CreateDepthStencilResource( w, h, candidate );
    if ( !createResult.ok )
    {
        return createResult;
    }

    ID3D12Resource* oldDepth = m_depthStencil;
    m_depthStencil = candidate;
    PublishDepthStencilView( m_depthStencil );
    if ( oldDepth )
    {
        oldDepth->Release();
    }
    return SkullbonezCore::Basics::SbResult::Success();
}


SkullbonezCore::Basics::SbResult
RenderBackendDX12::CreateDepthStencilResource( int w, int h, ID3D12Resource*& outResource )
{
    outResource = nullptr;
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
                                                                    IID_PPV_ARGS( &outResource ) );
    if ( FAILED( createResult ) || !outResource )
    {
        return Dx12BackendOperationResult( FAILED( createResult ) ? createResult : E_FAIL,
                                           "CreateCommittedResource (depth stencil) failed" );
    }
    NameDx12Object( outResource, L"Skullbonez DX12 Main Depth Stencil" );
    return SkullbonezCore::Basics::SbResult::Success();
}


void RenderBackendDX12::PublishDepthStencilView( ID3D12Resource* resource )
{
    if ( !resource )
    {
        SB_FATAL( "RenderBackendDX12", "Cannot publish a null main depth-stencil resource." );
    }

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
    Device()->CreateDepthStencilView( resource, &dsvDesc, m_mainDSV );
}


void RenderBackendDX12::Shutdown()
{
    if ( !Device() )
    {
        if ( m_frameOwner.HasSubmittedWork() )
        {
            // Lane F: terminal shutdown cannot release a partially owned device
            // after losing the only fence path that could prove queue completion.
            SB_FATAL( "RenderBackendDX12",
                      "Shutdown found submitted GPU work after the DX12 device/fence became unavailable." );
        }
        // Partial initialisation can fail inside Dx12RenderDevice. Even in that
        // state, the device owner may already hold factory/device/queue/swap-chain
        // objects, so always give it a chance to release them.
        m_renderDevice.Shutdown();
        m_frameOwner.ResetAfterShutdown();
        return;
    }

    // Scene-driven screenshots can leave the swap-chain back buffer restored to
    // RENDER_TARGET state after readback. Shutdown does one final DXGI Present()
    // below to drain the flip queue, and DX12 requires that resource to be in
    // PRESENT state first so the final DXGI Present() has a legal resource.
    if ( !m_frameOwner.HasFailure() && m_frameOwner.DeviceHealthy() && !m_pipelineOwner.RenderingToFramebuffer() &&
         m_frameOwner.BackBufferAccess() != RenderGraphResourceAccess::Present && SwapChain() &&
         m_frameOwner.RenderTarget( m_frameOwner.FrameIndex() ) )
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
            const SkullbonezCore::Basics::SbResult transitionResult = m_frameOwner.CurrentResult();
            SB_FATAL( "RenderBackendDX12",
                      "Shutdown could not record the final backbuffer transition. owner=%s reason=%s",
                      transitionResult.error.owner,
                      transitionResult.error.message );
        }
    }

    // Ensure any open command list is closed and submitted before waiting.
    // Hazard: an open epoch with a retained failure never reached
    // ExecuteCommandLists. Terminal cleanup discards it in place; closing or
    // submitting would issue work after the first retained failure.
    if ( m_frameOwner.IsOpen() && !m_frameOwner.HasFailure() )
    {
        AssertPlatformProfilerGpuStackClosed( "Shutdown" );
        const SkullbonezCore::Basics::SbResult closeResult =
            m_frameOwner.CommitClose( CommandList()->Close(), "Shutdown command list Close" );
        if ( !closeResult.ok )
        {
            SB_FATAL( "RenderBackendDX12",
                      "Shutdown command-list Close failed; resources remain process-owned. owner=%s reason=%s",
                      closeResult.error.owner,
                      closeResult.error.message );
        }
        const SkullbonezCore::Basics::SbResult submitResult = SubmitClosedCommandList();
        if ( !submitResult.ok )
        {
            SB_FATAL( "RenderBackendDX12",
                      "Shutdown command-list submission failed. owner=%s reason=%s",
                      submitResult.error.owner,
                      submitResult.error.message );
        }
    }

    // Wait for all GPU work to complete (command queue + pending presents).
    const SkullbonezCore::Basics::SbResult initialDrainResult =
        m_frameOwner.HasFailure() ? DrainForResourceRelease() : WaitForGpu();
    if ( !initialDrainResult.ok )
    {
        // Lane F: releasing any backend object after this point could race a
        // submitted command stream. Terminal shutdown must stop instead.
        SB_FATAL( "RenderBackendDX12",
                  "Shutdown could not prove initial GPU queue completion. owner=%s reason=%s",
                  initialDrainResult.error.owner,
                  initialDrainResult.error.message );
    }
    ReleaseCompletedDeferredResources( true );

    // Drain the DXGI flip queue. DX12's WaitForGpu only waits on the command queue fence,
    // but DXGI's flip-model present queue is separate. Without draining it, DWM may still
    // hold references to this swap chain's backbuffers after Release(), delaying the
    // window/compositor surface cleanup.
    // Present an empty frame with sync-interval 0 to flush the flip queue, then wait again.
    // Hazard: once command recording or device health has failed, shutdown is
    // terminal cleanup only. A final Present would be new native device work
    // after the first retained failure and would invalidate the fail-closed
    // guarantee exercised by the Debug fault probe.
    if ( SwapChain() && !m_frameOwner.HasFailure() && m_frameOwner.DeviceHealthy() )
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

    if ( m_frameOwner.HasSubmittedWork() )
    {
        SB_FATAL( "RenderBackendDX12", "Shutdown reached resource release with unproven submitted GPU work." );
    }
    if ( !m_frameOwner.RetirementEmpty() )
    {
        SB_FATAL( "RenderBackendDX12",
                  "Shutdown drain completed but deferred GPU resources remain quarantined. count=%zu",
                  m_frameOwner.RetirementCount() );
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

    // Lifetime: the concrete owners release their registries and compiled
    // pipelines only after the terminal GPU drain above proves no command list
    // can still reference them.
    m_geometryOwner.Shutdown();
    m_textureOwner.Shutdown();
    m_pipelineOwner.Shutdown();
    m_frameOwner.Uploads().Shutdown();
    if ( m_depthStencil )
    {
        m_depthStencil->Release();
    }
    for ( int i = 0; i < FRAME_COUNT; ++i )
    {
        if ( m_frameOwner.RenderTarget( static_cast<UINT>( i ) ) )
        {
            m_frameOwner.RenderTarget( static_cast<UINT>( i ) )->Release();
            m_frameOwner.RenderTarget( static_cast<UINT>( i ) ) = nullptr;
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
    m_frameOwner.Descriptors().Reset();
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
    m_frameOwner.ResetAfterShutdown();
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
    if ( m_frameOwner.HasFailure() )
    {
        return m_frameOwner.CurrentResult();
    }

    // Close the command list — finalizes the recorded commands. A closed command list can be
    // submitted to the GPU. No more commands can be recorded until Reset is called.
    // Docs: https://learn.microsoft.com/en-us/windows/win32/api/d3d12/nf-d3d12-id3d12graphicscommandlist-close
    AssertPlatformProfilerGpuStackClosed( "Present" );
    stateResult = m_frameOwner.CommitClose( CommandList()->Close(), "Present command list Close" );
    if ( !stateResult.ok )
    {
        return stateResult;
    }

    // Submit the completed command list to the GPU for execution. The GPU processes commands
    // asynchronously — this call returns immediately while the GPU works in the background.
    // Docs: https://learn.microsoft.com/en-us/windows/win32/api/d3d12/nf-d3d12-id3d12commandqueue-executecommandlists
    stateResult = SubmitClosedCommandList();
    if ( !stateResult.ok )
    {
        return stateResult;
    }

    // Present the frame — flips the swap chain to show the just-rendered back buffer on screen.
    // Sync interval is configurable so perf scenes can disable V-Sync while visual scenes keep it.
    // Docs: https://learn.microsoft.com/en-us/windows/win32/api/dxgi/nf-dxgi-idxgiswapchain-present
    const UINT syncInterval = m_isVsyncEnabled ? 1u : 0u;
    const UINT presentFlags = ( !m_isVsyncEnabled && m_allowTearing ) ? DXGI_PRESENT_ALLOW_TEARING : 0u;
    const HRESULT presentResult = SwapChain()->Present( syncInterval, presentFlags );
    if ( IsDx12DeviceLostResult( presentResult ) )
    {
        ReportDeviceLost( "Present", presentResult );
        return m_frameOwner.RetainDeviceLoss( "Present", presentResult );
    }
    const SkullbonezCore::Basics::SbResult presentFailure =
        Dx12BackendOperationResult( presentResult, "SwapChain Present failed" );
    if ( !presentFailure.ok )
    {
        return m_frameOwner.RetainFailure( presentFailure );
    }

    // Signal the fence with the current frame's value. When the GPU reaches
    // this point in its command stream, it updates the fence to that value.
    // Later, EnsureCommandListOpen asks the timeline helper whether this value
    // has completed before reusing this frame's command allocator, upload arena,
    // and transient descriptor range.
    UINT64 presentFenceValue = 0;
    const SkullbonezCore::Basics::SbResult signalResult = m_frameOwner.SignalFrame( presentFenceValue );
    if ( !signalResult.ok )
    {
        return signalResult;
    }
    m_frameOwner.SetFrameFenceValue( m_frameOwner.AllocatorIndex(), presentFenceValue );
    AssignDeferredResourceReleaseFence( presentFenceValue );

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
        m_gpuTimers.readFenceValue = presentFenceValue;
    }

    // Advance to next frame's allocator and swap chain buffer.
    m_frameOwner.AdvanceFrameIndices();
    m_pipelineOwner.SetCurrentColorTarget( m_backBufferRTVs[m_frameOwner.FrameIndex()] );

    // Charge allocator/upload/descriptor pacing to Present/VsyncWait instead of
    // letting the first render command of the next frame hit this wait mid-frame.
    const UINT64 nextFrameFenceValue = m_frameOwner.FrameFenceValue( m_frameOwner.AllocatorIndex() );
    if ( nextFrameFenceValue > m_renderDevice.FrameFence().CompletedValue() )
    {
        const SkullbonezCore::Basics::SbResult waitResult = m_frameOwner.WaitForFrameFence( nextFrameFenceValue );
        if ( !waitResult.ok )
        {
            return waitResult;
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
    if ( m_frameOwner.HasFailure() )
    {
        return m_frameOwner.CurrentResult();
    }

    if ( !CommandList() || !m_renderDevice.GraphicsQueue() || !m_renderDevice.FrameFence().IsReady() ||
         !m_renderDevice.CommandAllocator( m_frameOwner.AllocatorIndex() ) )
    {
        const SkullbonezCore::Basics::SbResult waitResult = m_frameOwner.CommitWait( WaitForGpu() );
        if ( !waitResult.ok )
        {
            return waitResult;
        }
        TryConsumeGpuTimerReadback( true );
        return SkullbonezCore::Basics::SbResult::Success();
    }

    if ( m_frameOwner.IsOpen() )
    {
        AssertPlatformProfilerGpuStackClosed( "Finish" );
        const SkullbonezCore::Basics::SbResult closeResult =
            m_frameOwner.CommitClose( CommandList()->Close(), "Finish command list Close" );
        if ( !closeResult.ok )
        {
            return closeResult;
        }
        const SkullbonezCore::Basics::SbResult submitResult = SubmitClosedCommandList();
        if ( !submitResult.ok )
        {
            return submitResult;
        }
    }
    const SkullbonezCore::Basics::SbResult waitResult = m_frameOwner.CommitWait( WaitForGpu() );
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
    if ( m_frameOwner.HasFailure() )
    {
        return m_frameOwner.CurrentResult();
    }

    if ( !CommandList() || !m_renderDevice.GraphicsQueue() || !m_renderDevice.FrameFence().IsReady() ||
         !m_renderDevice.CommandAllocator( m_frameOwner.AllocatorIndex() ) )
    {
        // Lane R: an active resource-mutation drain cannot claim success unless
        // it can both wait for submitted work and reopen the recording epoch.
        return m_frameOwner.RetainFailure( SkullbonezCore::Basics::SbResult::Failure(
            "Rendering/DX12",
            "FlushGPU requires a complete command queue, fence, allocator, and command list." ) );
    }

    Dx12GpuDrainProgress drainProgress( m_frameOwner.IsOpen() );
    if ( drainProgress.RequiresClose() )
    {
        AssertPlatformProfilerGpuStackClosed( "FlushGPU" );
        const SkullbonezCore::Basics::SbResult closeResult =
            m_frameOwner.CommitClose( CommandList()->Close(), "FlushGPU command list Close" );
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

        const SkullbonezCore::Basics::SbResult submitResult = SubmitClosedCommandList();
        if ( !submitResult.ok )
        {
            return submitResult;
        }
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
    const SkullbonezCore::Basics::SbResult waitResult = m_frameOwner.CommitWait( WaitForGpu() );
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


SkullbonezCore::Basics::SbResult RenderBackendDX12::DrainForResourceRelease()
{
    if ( !m_frameOwner.HasFailure() )
    {
        return FlushGPU();
    }

    if ( m_frameOwner.DeviceLost() )
    {
        // Lifetime: DXGI device removal terminates this device/queue lifetime;
        // the submitted commands can no longer execute against resources from
        // it. Do not issue a fence Signal after removal. Abandon the completion
        // proof only for terminal COM release, never for runtime reuse.
        m_frameOwner.AbandonSubmittedWork();
        return SkullbonezCore::Basics::SbResult::Success();
    }

    // Lifetime: a failed, never-submitted recording epoch cannot reference any
    // resource from the GPU. Release is already safe and teardown must not turn
    // that expected Lane R path into a second submission or a destructor fatal.
    if ( !m_frameOwner.HasSubmittedWork() )
    {
        return SkullbonezCore::Basics::SbResult::Success();
    }

    // Hazard: if earlier work reached the queue before a later recording error,
    // terminal teardown may signal/wait for that submitted work. It must not
    // submit, reopen, or clear the sticky command-path failure.
    return WaitForGpu();
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

    Dx12RecreationTransaction transaction;
    transaction.Begin( m_recreationGeneration );

    // Prepare the independent depth candidate before releasing a single
    // published resource. A creation failure therefore leaves the current
    // framebuffer and dimensions untouched.
    ID3D12Resource* candidateDepth = nullptr;
    const SkullbonezCore::Basics::SbResult candidateDepthResult =
        CreateDepthStencilResource( width, height, candidateDepth );
    if ( !candidateDepthResult.ok )
    {
        return transaction.Fail( candidateDepthResult );
    }
    if ( !transaction.CommitCandidateReady() )
    {
        candidateDepth->Release();
        SB_FATAL( "RenderBackendDX12", "Resize transaction rejected prepared depth candidate." );
    }

    // DXGI requires every application-held back-buffer reference to be released
    // before ResizeBuffers. Member publication is restored from the swap chain
    // if ResizeBuffers rejects the request without removing the device.
    for ( int i = 0; i < FRAME_COUNT; ++i )
    {
        if ( m_frameOwner.RenderTarget( static_cast<UINT>( i ) ) )
        {
            m_frameOwner.RenderTarget( static_cast<UINT>( i ) )->Release();
        }
        m_frameOwner.RenderTarget( static_cast<UINT>( i ) ) = nullptr;
    }
    if ( !transaction.CommitOldReferencesReleased() )
    {
        candidateDepth->Release();
        SB_FATAL( "RenderBackendDX12", "Resize transaction rejected released back-buffer references." );
    }

    const UINT resizeFlags = m_allowTearing ? DXGI_SWAP_CHAIN_FLAG_ALLOW_TEARING : 0u;
    const HRESULT resizeResult =
        SwapChain()->ResizeBuffers( FRAME_COUNT, (UINT)width, (UINT)height, DXGI_FORMAT_R8G8B8A8_UNORM, resizeFlags );
    if ( IsDx12DeviceLostResult( resizeResult ) )
    {
        candidateDepth->Release();
        ReportDeviceLost( "ResizeBuffers", resizeResult );
        return transaction.Fail( m_frameOwner.RetainDeviceLoss( "ResizeBuffers", resizeResult ) );
    }
    const SkullbonezCore::Basics::SbResult resizeFailure =
        Dx12BackendOperationResult( resizeResult, "SwapChain ResizeBuffers failed" );
    if ( !resizeFailure.ok )
    {
        candidateDepth->Release();
        // A failed ResizeBuffers leaves the old swap-chain buffers owned by
        // DXGI. Reacquire all of them before returning so the published backend
        // remains usable at its previous dimensions.
        ID3D12Resource* restored[FRAME_COUNT] = {};
        bool restoredAll = true;
        for ( int i = 0; i < FRAME_COUNT; ++i )
        {
            if ( FAILED( SwapChain()->GetBuffer( static_cast<UINT>( i ), IID_PPV_ARGS( &restored[i] ) ) ) ||
                 !restored[i] )
            {
                restoredAll = false;
                break;
            }
        }
        if ( restoredAll )
        {
            for ( int i = 0; i < FRAME_COUNT; ++i )
            {
                m_frameOwner.RenderTarget( static_cast<UINT>( i ) ) = restored[i];
                Device()->CreateRenderTargetView( m_frameOwner.RenderTarget( static_cast<UINT>( i ) ),
                                                  nullptr,
                                                  m_backBufferRTVs[i] );
            }
            m_pipelineOwner.SetCurrentColorTarget( m_backBufferRTVs[m_frameOwner.FrameIndex()] );
            return transaction.Fail( resizeFailure );
        }
        for ( ID3D12Resource* resource : restored )
        {
            if ( resource )
            {
                resource->Release();
            }
        }
        return m_frameOwner.RetainFailure( transaction.Fail( SkullbonezCore::Basics::SbResult::Failure(
            "Rendering/DX12",
            "ResizeBuffers failed and the previous back buffers could not be restored" ) ) );
    }
    if ( !transaction.CommitSwapChainResized() )
    {
        candidateDepth->Release();
        SB_FATAL( "RenderBackendDX12", "Resize transaction rejected successful ResizeBuffers." );
    }
    ID3D12Resource* candidateBackBuffers[FRAME_COUNT] = {};
    for ( int i = 0; i < FRAME_COUNT; ++i )
    {
        const SkullbonezCore::Basics::SbResult backBufferResult =
            Dx12BackendOperationResult( SwapChain()->GetBuffer( (UINT)i, IID_PPV_ARGS( &candidateBackBuffers[i] ) ),
                                        "SwapChain GetBuffer after resize failed" );
        if ( !backBufferResult.ok )
        {
            for ( ID3D12Resource* resource : candidateBackBuffers )
            {
                if ( resource )
                {
                    resource->Release();
                }
            }
            candidateDepth->Release();
            return m_frameOwner.RetainFailure( transaction.Fail( backBufferResult ) );
        }
    }
    if ( !transaction.CommitBackBuffersReady() )
    {
        for ( ID3D12Resource* resource : candidateBackBuffers )
        {
            resource->Release();
        }
        candidateDepth->Release();
        SB_FATAL( "RenderBackendDX12", "Resize transaction rejected complete back-buffer candidates." );
    }

    // Publication boundary: no member points at a replacement until every
    // back buffer and the depth candidate exists.
    m_frameOwner.RefreshFrameIndex();
    // ResizeBuffers puts all back buffers into PRESENT state, so the next
    // Clear()/PrepareDraw() must transition from that concrete state.
    m_frameOwner.SetBackBufferAccess( RenderGraphResourceAccess::Present );
    for ( int i = 0; i < FRAME_COUNT; ++i )
    {
        m_frameOwner.RenderTarget( static_cast<UINT>( i ) ) = candidateBackBuffers[i];
        NameDx12ObjectIndexed( m_frameOwner.RenderTarget( static_cast<UINT>( i ) ),
                               L"Skullbonez DX12 Swapchain Backbuffer",
                               static_cast<UINT>( i ) );
        Device()->CreateRenderTargetView( m_frameOwner.RenderTarget( static_cast<UINT>( i ) ),
                                          nullptr,
                                          m_backBufferRTVs[i] );
    }
    ID3D12Resource* oldDepth = m_depthStencil;
    m_depthStencil = candidateDepth;
    PublishDepthStencilView( m_depthStencil );
    if ( oldDepth )
    {
        oldDepth->Release();
    }

    m_width = width;
    m_height = height;
    m_pipelineOwner.SetViewport( { 0.0f, 0.0f, (float)width, (float)height, 0.0f, 1.0f },
                                 { 0, 0, (LONG)width, (LONG)height } );
    m_pipelineOwner.SetCurrentTargets( m_backBufferRTVs[m_frameOwner.FrameIndex()], m_mainDSV );
    ++m_recreationGeneration;
    if ( !transaction.CommitPublished( m_recreationGeneration ) ||
         transaction.PublishedGeneration() != m_recreationGeneration )
    {
        SB_FATAL( "RenderBackendDX12", "Resize transaction failed its publication proof." );
    }
    return SkullbonezCore::Basics::SbResult::Success();
}


// --- Viewport & Clear ---


void RenderBackendDX12::SetViewport( int x, int y, int w, int h )
{
    m_pipelineOwner.SetViewport( { (float)x, (float)y, (float)w, (float)h, 0.0f, 1.0f },
                                 { (LONG)x, (LONG)y, (LONG)( x + w ), (LONG)( y + h ) } );
}


void RenderBackendDX12::Clear( bool color, bool depth )
{
    if ( !EnsureCommandListOpen().ok )
    {
        return;
    }

    if ( !m_pipelineOwner.RenderingToFramebuffer() )
    {
        TransitionBackbuffer( "ClearBackbuffer", RenderGraphResourceAccess::RenderTarget );
        if ( m_frameOwner.HasFailure() )
        {
            return;
        }
    }
    // Bind the render target and depth buffer to the Output Merger (OM) stage — this tells the
    // GPU where to write pixel colors and depth values for subsequent draw calls.
    // Docs:
    // https://learn.microsoft.com/en-us/windows/win32/api/d3d12/nf-d3d12-id3d12graphicscommandlist-omsetrendertargets
    m_pipelineOwner.BindCurrentOutputs( CommandList() );

    // Viewport defines where rendering appears, and the scissor rect clips pixels
    // (pixels outside the scissor are clipped/discarded). Both must be set every time in DX12.
    // Docs: https://learn.microsoft.com/en-us/windows/win32/api/d3d12/nf-d3d12-id3d12graphicscommandlist-rssetviewports
    // Docs:
    // https://learn.microsoft.com/en-us/windows/win32/api/d3d12/nf-d3d12-id3d12graphicscommandlist-rssetscissorrects
    if ( color )
    {
        // Clear the render target to a solid color (wipes the entire back buffer).
        // Docs:
        // https://learn.microsoft.com/en-us/windows/win32/api/d3d12/nf-d3d12-id3d12graphicscommandlist-clearrendertargetview
        m_pipelineOwner.ClearCurrentColor( CommandList(), m_clearColor );
    }
    if ( depth )
    {
        // Clear the depth buffer to 1.0 (maximum distance), so all subsequent draws will pass
        // the depth test. This is done at the start of each frame or when switching render targets.
        // Docs:
        // https://learn.microsoft.com/en-us/windows/win32/api/d3d12/nf-d3d12-id3d12graphicscommandlist-cleardepthstencilview
        m_pipelineOwner.ClearCurrentDepth( CommandList(), m_clearDepth );
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
    m_pipelineOwner.SetDepthTest( enable );
}


void RenderBackendDX12::SetDepthWrite( bool enable )
{
    m_pipelineOwner.SetDepthWrite( enable );
}


void RenderBackendDX12::SetBlend( bool enable )
{
    m_pipelineOwner.SetBlend( enable );
}


void RenderBackendDX12::SetBlendFunc( BlendFactor src, BlendFactor dst )
{
    m_pipelineOwner.SetBlendFunc( src, dst );
}


void RenderBackendDX12::SetCullFace( bool enable )
{
    m_pipelineOwner.SetCullFace( enable );
}


void RenderBackendDX12::SetPolygonOffset( bool enable, float factor, float units )
{
    m_pipelineOwner.SetPolygonOffset( enable, factor, units );
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
    return m_pipelineOwner.DepthTestEnabled();
}


bool RenderBackendDX12::IsDepthWriteEnabled() const
{
    return m_pipelineOwner.DepthWriteEnabled();
}


bool RenderBackendDX12::IsBlendEnabled() const
{
    return m_pipelineOwner.BlendEnabled();
}


bool RenderBackendDX12::IsCullFaceEnabled() const
{
    return m_pipelineOwner.CullEnabled();
}


void RenderBackendDX12::GetBlendFunc( BlendFactor& outSrc, BlendFactor& outDst ) const
{
    m_pipelineOwner.GetBlendFunc( outSrc, outDst );
}


// --- DXR Raytracing ---


// --- GPU Timers ---
