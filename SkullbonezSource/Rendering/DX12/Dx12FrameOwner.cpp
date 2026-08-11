/*
File: SkullbonezSource/Rendering/DX12/Dx12FrameOwner.cpp
Purpose:
  Implements the private DX12 frame epoch and its restricted capability views.

Summary:
  The owner sequences command-list open/close, queue submission, fence waits,
  upload reservations, profiler markers, and resource retirement without
  exposing the aggregate renderer backend to draw-time collaborators. A narrow
  diagnostics view exposes timestamp recording, fence polling, and cold fault
  configuration without submission authority.

Invariants:
  - The two-frame ring advances only after submission and covering-fence bookkeeping agree.
  - Steady-runtime upload exhaustion drops the caller instead of allocating or stalling.
  - Profiler scopes close before submission and restore only on the replacement command list.
  - Dx12DiagnosticsFrame cannot reach uploads, descriptors, or frame advancement.

Related:
  - SkullbonezSource/Rendering/DX12/Dx12FrameOwner.h
  - SkullbonezSource/Rendering/DX12/RenderBackendDX12.cpp
  - SkullbonezSource/Rendering/DX12/Dx12Diagnostics.cpp
  - Agentic/Reference/engine-glossary.md
*/
#include "RenderBackendDX12.h"
#include "Dx12FrameOwner.h"
#include "ShaderDX12.h"
#include "Dx12RenderGraphExecutor.h"
#include "../../Core/FatalError.h"
#include "../../Core/Log.h"
#include "../../Core/PlatformProfiler.h"
#include "../../Core/TracyClientOwner.h"
#include "../../Core/Allocation/RuntimeAllocationTracker.h"

#include <algorithm>
#include <cstdio>
#include <cstring>

using namespace SkullbonezCore::Rendering;

Dx12FrameOwner::Dx12FrameOwner( SkullbonezCore::Core::SbDiagnosticStore& resultDiagnostics, Dx12RenderDevice& device,
                                Dx12PipelineOwner& pipeline, Dx12TextureOwner& textures, Dx12DescriptorHeaps& descriptors )
    : m_resultDiagnostics( resultDiagnostics ), m_device( device ), m_pipeline( pipeline ), m_textures( textures ),
      m_descriptors( descriptors ), m_recording( resultDiagnostics ), m_deviceHealth( resultDiagnostics ),
      m_faultInjection( resultDiagnostics ), m_uploads( resultDiagnostics ), m_drawGate( *this ),
      m_uploadReservations( *this ), m_resourceRelease( *this ), m_captureFrame( *this ), m_diagnosticsFrame( *this )
{
}


SkullbonezCore::Core::SbResult Dx12FrameOwner::FinishAndReopen( Dx12Diagnostics& diagnostics )
{
    if ( HasFailure() )
    {
        return CurrentResult();
    }

    if ( !m_device.CommandList() || !m_device.GraphicsQueue() || !m_device.FrameFence().IsReady() ||
         !m_device.CommandAllocator( AllocatorIndex() ) )
    {
        const SkullbonezCore::Core::SbResult waitResult = CommitWait( WaitForGpu() );

        if ( !waitResult.Ok() )
        {
            return waitResult;
        }

        diagnostics.ConsumeGpuTimerReadback( DiagnosticsFrame(), true );
        return SkullbonezCore::Core::SbResult::Success();
    }

    if ( IsOpen() )
    {
        AssertProfilerClosed( "Finish" );
        const SkullbonezCore::Core::SbResult closeResult = CommitClose( m_device.CommandList()->Close(),
                                                                        "Finish command list Close" );

        if ( !closeResult.Ok() )
        {
            return closeResult;
        }

        const SkullbonezCore::Core::SbResult submitResult = SubmitClosed();

        if ( !submitResult.Ok() )
        {
            return submitResult;
        }
    }

    const SkullbonezCore::Core::SbResult waitResult = CommitWait( WaitForGpu() );

    if ( !waitResult.Ok() )
    {
        return waitResult;
    }

    diagnostics.ConsumeGpuTimerReadback( DiagnosticsFrame(), true );

    // Hazard: pipeline synchronization can run between physics and rendering.
    // Reopen here so the next graph pass still has a legal recording epoch.
    return EnsureOpen();
}


void Dx12FrameOwner::ResetForDevice()
{
    m_recording.ResetForDevice();
    m_submittedWork.ResetForDevice();
    m_deviceHealth.ResetForDevice();
    m_retirement.ResetForDevice();
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
    m_uploadFlushCount = 0;
    m_uploadDropCount = 0;
    std::fill_n( m_uploadCategoryDropCount, RENDER_UPLOAD_CATEGORY_COUNT, uint64_t { 0 } );
}


void Dx12FrameOwner::ResetAfterShutdown()
{
    m_retirement.ResetAfterShutdown();
    m_profilerStackState.Reset();
    m_profilerScopes.fill( Dx12PlatformProfilerGpuScopeDX12() );

    for ( UINT64& value : m_frameFenceValues )
    {
        value = 0;
    }

    m_allocatorIndex = 0;
    m_frameIndex = 0;
    m_backBufferAccess = RenderGraphResourceAccess::Present;
    CancelPendingConstantUpload();
    m_uploadFlushCount = 0;
    m_uploadDropCount = 0;
    std::fill_n( m_uploadCategoryDropCount, RENDER_UPLOAD_CATEGORY_COUNT, uint64_t { 0 } );
}


bool Dx12FrameOwner::TransitionBackbuffer( const char* passName, RenderGraphResourceAccess after )
{
    // Exception boundary: this helper is reserved for Present, cold synchronous
    // capture, the editor viewport copy/restore pair, and lifecycle
    // reconciliation. Executable frame passes use compiled graph transitions.
    ID3D12Resource* backbuffer = RenderTarget( FrameIndex() );

    if ( !backbuffer || BackBufferAccess() == after )
    {
        return false;
    }

    if ( !CanRecord() && !EnsureOpen().Ok() )
    {
        return false;
    }

    Dx12RenderGraphSingleTransitionDesc desc;
    desc.commandList = CommandList();
    desc.resource = backbuffer;
    desc.before = BackBufferAccess();
    desc.after = after;
    const Dx12RenderGraphBarrierRecord record = ExecuteDx12RenderGraphSingleTransition( "Dx12Explicit", passName,
                                                                                        "SwapchainBackbuffer", desc );

    if ( !record.hasConcreteStates || !record.hasNativeResource || record.missingCommandList ||
         record.beforeState == record.afterState || !record.emitted )
    {
        // Hazard: advance the tracked state only after exactly one native
        // barrier was emitted for the current swap-chain image.
        SB_FATAL( "Dx12FrameOwner", "DX12 backbuffer transition did not emit exactly one concrete barrier. pass=%s",
                  passName ? passName : "unknown" );
    }

    SetBackBufferAccess( after );
    return true;
}


SkullbonezCore::Core::SbResult Dx12FrameOwner::EnsureOpen()
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
        return SkullbonezCore::Core::SbResult::Success();
    }

    const UINT64 completed = m_device.FrameFence().CompletedValue();
    m_submittedWork.ObserveCompletedFence( completed );

    if ( m_submittedWork.HasUnfencedOrUncertainWork() )
    {
        return m_recording.RetainFailure( m_resultDiagnostics
                                              .Failure( "Rendering/DX12",
                                                        "Draw epoch blocked because submitted work lacks a trustworthy completion fence." ) );
    }

    const UINT64 allocatorFence = m_frameFenceValues[m_allocatorIndex];

    if ( allocatorFence > completed )
    {
        const SkullbonezCore::Core::SbResult wait = m_device.FrameFence().WaitForValue( allocatorFence );
        m_submittedWork.CommitWait( wait, allocatorFence );

        if ( !wait.Ok() )
        {
            return m_recording.CommitWait( wait );
        }
    }

    m_retirement.ReleaseCompleted( m_device, m_descriptors, m_submittedWork, false );
    SkullbonezCore::Core::SbResult result = m_recording.CommitAllocatorReset( allocator->Reset(),
                                                                              "Dx12FrameOwner allocator Reset" );

    if ( !result.Ok() )
    {
        return result;
    }

    result = m_recording.CommitListReset( m_device.CommandList()->Reset( allocator, nullptr ),
                                          "Dx12FrameOwner command-list Reset" );

    if ( !result.Ok() )
    {
        return result;
    }

    m_descriptors.Bind( m_device.CommandList() );
    m_device.CommandList()->SetGraphicsRootSignature( m_pipeline.RootSignature() );
    m_uploads.ResetFrame( m_allocatorIndex );
    m_descriptors.ResetFrame( m_allocatorIndex );
    m_pipeline.InvalidateCommandState();
    m_textures.InvalidateBindings();
    return SkullbonezCore::Core::SbResult::Success();
}


bool Dx12FrameOwner::PrepareDraw()
{
    if ( !EnsureOpen().Ok() )
    {
        return false;
    }

    if ( !m_pipeline.RenderingToFramebuffer() && m_backBufferAccess != RenderGraphResourceAccess::RenderTarget )
    {
        // Invariant: executable graph callbacks acquire the normal render
        // target state. Draw submission is a consumer and must never recreate
        // the retired implicit transition fallback.
        SB_FATAL( "Dx12FrameOwner", "Backbuffer draw reached submission without graph acquisition. tracked=%s",
                  ToString( m_backBufferAccess ) );
    }

    return !m_recording.HasFailure();
}


bool Dx12FrameOwner::PrepareFramebufferBind()
{
    return EnsureOpen().Ok();
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
        SkullbonezCore::Core::Log()
            .WriteEventf( "dx12_platform_profiler_gpu_suspend_without_open_command_list reason=%s depth=%d",
                          reason ? reason : "unknown", depth );

        return 0;
    }

    SkullbonezCore::Core::Log().WriteEventf( "dx12_platform_profiler_gpu_stack_suspended_for_submit reason=%s depth=%d",
                                             reason ? reason : "unknown", depth );

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
        SkullbonezCore::Core::Log().WriteEventf( "dx12_platform_profiler_gpu_restore_bookkeeping_only depth=%d", depth );

        if ( !m_profilerStackState.RestoreAfterSubmit( depth, PROFILER_STACK_CAPACITY ) )
        {
            SB_FATAL( "Dx12FrameOwner", "SkullbonezCore::Core::Profiler bookkeeping restore exceeded fixed capacity." );
        }

        return;
    }

    for ( int i = 0; i < depth; ++i )
    {
        const Dx12PlatformProfilerGpuScopeDX12& scope = m_profilerScopes[i];
        const char* markerName = scope.name[0] != '\0' ? scope.name : "(null)";
        PIXBeginEvent( m_device.CommandList(),
                       SkullbonezCore::Core::PlatformProfiler::ColorForMarker( markerName, scope.hash ), "%s", markerName );
    }

    if ( !m_profilerStackState.RestoreAfterSubmit( depth, PROFILER_STACK_CAPACITY ) )
    {
        SB_FATAL( "Dx12FrameOwner", "SkullbonezCore::Core::Profiler stack restore exceeded fixed capacity." );
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

    SkullbonezCore::Core::Log().WriteEventf( "dx12_platform_profiler_open_stack_on_submit reason=%s depth=%d",
                                             reason ? reason : "unknown", m_profilerStackState.Depth() );

    assert( m_profilerStackState.Depth() == 0 );
    SB_FATAL( "Dx12FrameOwner", "DX12 platform profiler GPU stack left open before command submission. reason=%s depth=%d",
              reason ? reason : "unknown", m_profilerStackState.Depth() );
}


void Dx12FrameOwner::BeginProfilerEvent( const char* name, uint32_t hash )
{
#if SKULLBONEZ_PLATFORM_PROFILER_HAVE_PIX3

    if ( !CommandList() || !EnsureOpen().Ok() )
    {
        return;
    }

    if ( m_profilerStackState.Depth() >= PROFILER_STACK_CAPACITY )
    {
        SB_FATAL( "Dx12FrameOwner", "DX12 platform profiler GPU stack overflow. depth=%d capacity=%d",
                  m_profilerStackState.Depth(), PROFILER_STACK_CAPACITY );
    }

    char gpuMarkerName[SkullbonezCore::Core::PlatformProfiler::MAX_DECORATED_MARKER_NAME_CHARS];
    const char* markerName = SkullbonezCore::Core::PlatformProfiler::AreDetailedRangesEnabled()
                                 ? SkullbonezCore::Core::PlatformProfiler::DecorateMarkerName( name, "_GPU", gpuMarkerName,
                                                                                               sizeof( gpuMarkerName ) )
                                 : name;

    Dx12PlatformProfilerGpuScopeDX12& scope = m_profilerScopes[m_profilerStackState.Depth()];
    _snprintf_s( scope.name, sizeof( scope.name ), _TRUNCATE, "%s", markerName ? markerName : "(null)" );
    scope.hash = hash;
    PIXBeginEvent( CommandList(), SkullbonezCore::Core::PlatformProfiler::ColorForMarker( markerName, hash ), "%s",
                   markerName );

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
        if ( SkullbonezCore::Core::PlatformProfiler::IsEnabled() )
        {
            SkullbonezCore::Core::Log().WriteEventf( "dx12_platform_profiler_gpu_end_without_begin" );
        }

        return;
    }

    if ( !CommandList() || !m_recording.CanRecord() )
    {
        SkullbonezCore::Core::Log().WriteEventf( "dx12_platform_profiler_gpu_end_bookkeeping_only depth=%d",
                                                 m_profilerStackState.Depth() );

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


SkullbonezCore::Core::SbResult Dx12FrameOwner::SubmitClosed()
{
    SKORE_TRACY_SCOPED_OWNER_ZONE( "Frame/DX12/CommandSubmission", ::HashStr( "Frame/DX12/CommandSubmission" ) );

    if ( m_recording.HasFailure() )
    {
        if ( m_faultInjection.WasInjected() )
        {
            [[maybe_unused]] const SkullbonezCore::Core::SbResult blocked = m_faultInjection.BeforeSubmission();
            WriteFaultProbe();
        }

        return m_recording.CurrentResult();
    }

    if ( !m_device.CommandList() || !m_device.GraphicsQueue() || !m_recording.IsClosed() )
    {
        SB_FATAL( "Dx12FrameOwner", "Upload flush submission requires a healthy closed command list." );
    }

    const SkullbonezCore::Core::SbResult injected = m_faultInjection.BeforeSubmission();

    if ( !injected.Ok() )
    {
        const SkullbonezCore::Core::SbResult retained = m_recording.RetainFailure( injected );
        SkullbonezCore::Core::Log().WriteEventf( "dx12_fault_injected point=before-first-submit submissions=%u",
                                                 m_faultInjection.SubmissionCount() );

        fprintf( stderr, "[dx12-fault] owner=%s reason=\"%s\" submissions=%u\n", retained.ErrorOwner(),
                 retained.ErrorMessage(), m_faultInjection.SubmissionCount() );

        fflush( stderr );
        SkullbonezCore::Core::Log().FlushAll();
        WriteFaultProbe();
        return retained;
    }

    ID3D12CommandList* lists[] = { m_device.CommandList() };

    m_device.GraphicsQueue()->ExecuteCommandLists( 1, lists );
    m_faultInjection.CommitSubmission();
    m_submittedWork.MarkSubmitted();
    return SkullbonezCore::Core::SbResult::Success();
}


SkullbonezCore::Core::SbResult Dx12FrameOwner::WaitForGpu()
{
    if ( !m_device.FrameFence().IsReady() )
    {
        if ( m_submittedWork.HasSubmittedWork() )
        {
            const SkullbonezCore::Core::SbResult
                unavailable = m_resultDiagnostics
                                  .Failure( "Rendering/DX12",
                                            "DX12 fence timeline unavailable while submitted GPU work remains unproven." );

            m_submittedWork.CommitWait( unavailable, 0 );
            return unavailable;
        }

        ReleaseCompletedRetirements( !m_recording.IsOpen() );
        return SkullbonezCore::Core::SbResult::Success();
    }

    UINT64 fence = 0;
    const SkullbonezCore::Core::SbResult signal = m_device.FrameFence().Signal( fence );
    m_submittedWork.CommitSignal( signal, fence );

    if ( !signal.Ok() )
    {
        return signal;
    }

    const SkullbonezCore::Core::SbResult wait = m_device.FrameFence().WaitForValue( fence );
    m_submittedWork.CommitWait( wait, fence );

    if ( !wait.Ok() )
    {
        return wait;
    }

    if ( m_submittedWork.HasSubmittedWork() )
    {
        SB_FATAL( "Dx12FrameOwner", "Submitted work remained live after successful upload-flush drain. fence=%llu",
                  static_cast<unsigned long long>( fence ) );
    }

    for ( int i = 0; i < FRAME_COUNT; ++i )
    {
        m_frameFenceValues[i] = 0;
    }

    ReleaseCompletedRetirements( !m_recording.IsOpen() );
    return SkullbonezCore::Core::SbResult::Success();
}


SkullbonezCore::Core::SbResult Dx12FrameOwner::FlushUploadBuffer()
{
    const int profilerDepth = SuspendProfilerForSubmit( "FlushUploadBuffer" );
    AssertProfilerClosed( "FlushUploadBuffer" );
    SkullbonezCore::Core::SbResult result = m_recording.CommitClose( m_device.CommandList()->Close(),
                                                                     "Dx12FrameOwner upload flush Close" );

    if ( result.Ok() )
    {
        result = SubmitClosed();
    }

    if ( result.Ok() )
    {
        result = m_recording.CommitWait( WaitForGpu() );
    }

    if ( !result.Ok() )
    {
        RestoreProfilerAfterSubmit( profilerDepth );
        return result;
    }

    ID3D12CommandAllocator* allocator = m_device.CommandAllocator( m_allocatorIndex );
    result = m_recording.CommitAllocatorReset( allocator->Reset(), "Dx12FrameOwner upload flush allocator Reset" );

    if ( result.Ok() )
    {
        result = m_recording.CommitListReset( m_device.CommandList()->Reset( allocator, nullptr ),
                                              "Dx12FrameOwner upload flush list Reset" );
    }

    if ( !result.Ok() )
    {
        RestoreProfilerAfterSubmit( profilerDepth );
        return result;
    }

    m_descriptors.Bind( m_device.CommandList() );
    m_device.CommandList()->SetGraphicsRootSignature( m_pipeline.RootSignature() );
    m_uploads.ResetFrame( m_allocatorIndex );
    m_descriptors.ResetFrame( m_allocatorIndex );
    m_pipeline.InvalidateCommandState();
    m_textures.InvalidateBindings();
    RestoreProfilerAfterSubmit( profilerDepth );
    return SkullbonezCore::Core::SbResult::Success();
}


static const char* Dx12UploadCategoryName( RenderUploadCategory category )
{
    switch ( category )
    {
    case RenderUploadCategory::Constants:
        return "constants";
    case RenderUploadCategory::DynamicVertex:
        return "dynamic_vertex";
    case RenderUploadCategory::InstanceData:
        return "instance_data";
    case RenderUploadCategory::TextureRows:
        return "texture_rows";
    case RenderUploadCategory::RetainedGeometry:
        return "retained_geometry";
    default:
        return "unknown";
    }
}


bool Dx12FrameOwner::PrepareUploadReservation( UINT64 size, UINT64 alignment, RenderUploadCategory category )
{
    const bool fits = m_uploads.CanAllocate( m_allocatorIndex, size, alignment );
    const SkullbonezCore::Core::Allocation::RuntimeAllocationPhase
        phase = SkullbonezCore::Core::Allocation::GetRuntimeAllocationPhase();

    const Dx12UploadArenaStats stats = m_uploads.GetStats( m_allocatorIndex );
    const char* owner = Dx12UploadCategoryName( category );
    const Dx12UploadReservationResolution
        resolution = ResolveDx12UploadReservation( fits, phase,
                                                   [&]()
                                                   {
                                                       ++m_uploadFlushCount;

                                                       SkullbonezCore::Core::Log()
                                                           .WriteEventf( "dx12_upload_cold_flush owner=%s phase=%s "
                                                                         "requested_bytes=%llu used_bytes=%llu "
                                                                         "capacity_bytes=%llu flushes=%llu",
                                                                         owner,
                                                                         SkullbonezCore::Core::Allocation::
                                                                             RuntimeAllocationPhaseName( phase ),
                                                                         static_cast<unsigned long long>( size ),
                                                                         static_cast<unsigned long long>( stats.usedBytes ),
                                                                         static_cast<unsigned long long>( stats.capacityBytes ),
                                                                         static_cast<unsigned long long>( m_uploadFlushCount ) );

                                                       return FlushUploadBuffer().Ok() &&
                                                              m_uploads.CanAllocate( m_allocatorIndex, size, alignment );
                                                   } );

    if ( resolution.dropped )
    {
        ++m_uploadDropCount;
        const std::size_t categoryIndex = static_cast<std::size_t>( category );

        if ( categoryIndex >= RENDER_UPLOAD_CATEGORY_COUNT )
        {
            SB_FATAL( "Dx12FrameOwner", "DX12 upload drop used an invalid category. category=%zu", categoryIndex );
        }

        const uint64_t categoryDrops = ++m_uploadCategoryDropCount[categoryIndex];

        // Rate-limit independently per owner so the first texture, constants,
        // instance, or overlay offender cannot be hidden by another category.
        if ( ( categoryDrops & ( categoryDrops - 1u ) ) == 0u )
        {
            SkullbonezCore::Core::Log()
                .WriteEventf( "dx12_upload_drop owner=%s phase=%s requested_bytes=%llu used_bytes=%llu "
                              "capacity_bytes=%llu owner_drops=%llu total_drops=%llu",
                              owner, SkullbonezCore::Core::Allocation::RuntimeAllocationPhaseName( phase ),
                              static_cast<unsigned long long>( size ), static_cast<unsigned long long>( stats.usedBytes ),
                              static_cast<unsigned long long>( stats.capacityBytes ),
                              static_cast<unsigned long long>( categoryDrops ),
                              static_cast<unsigned long long>( m_uploadDropCount ) );
        }
    }

    return resolution.allowed;
}


D3D12_GPU_VIRTUAL_ADDRESS
Dx12FrameOwner::ReserveUpload( UINT64 size, UINT64 alignment, RenderUploadCategory category )
{
    if ( !EnsureOpen().Ok() || !PrepareUploadReservation( size, alignment, category ) )
    {
        return 0;
    }

    return m_uploads.Allocate( m_allocatorIndex, size, alignment, category );
}


D3D12_GPU_VIRTUAL_ADDRESS
Dx12FrameOwner::ReserveGeometryUpload( UINT64 vertexBytes, UINT64 constantBytes, RenderUploadCategory vertexCategory )
{
    if ( !EnsureOpen().Ok() || vertexBytes == 0 )
    {
        return 0;
    }

    m_pendingConstantAddress = 0;
    m_pendingConstantBytes = 0;

    // Hazard: a flush after either address is published invalidates both. Probe
    // the conservative combined aligned budget, flush at most once, then allocate
    // the constant row before the dependent vertex/instance bytes.
    constexpr UINT64 MAX_ALIGNMENT_PADDING = 255u + 3u;
    const UINT64 maxValue = ( std::numeric_limits<UINT64>::max )();
    const UINT64 combinedBudget = constantBytes <= maxValue - MAX_ALIGNMENT_PADDING &&
                                          vertexBytes <= maxValue - MAX_ALIGNMENT_PADDING - constantBytes
                                      ? constantBytes + vertexBytes + MAX_ALIGNMENT_PADDING
                                      : maxValue;

    if ( !PrepareUploadReservation( combinedBudget, 1, vertexCategory ) )
    {
        return 0;
    }

    if ( constantBytes > 0 )
    {
        m_pendingConstantAddress = m_uploads.Allocate( m_allocatorIndex, constantBytes, 256,
                                                       RenderUploadCategory::Constants );

        m_pendingConstantBytes = constantBytes;
    }

    return m_uploads.Allocate( m_allocatorIndex, vertexBytes, 4, vertexCategory );
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
    return ReserveUpload( size, 256, RenderUploadCategory::Constants );
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

    fprintf( report, "point=before-first-submit injected=%d submissions=%u blocked_after_failure=%u\n",
             m_faultInjection.WasInjected() ? 1 : 0, m_faultInjection.SubmissionCount(),
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


SkullbonezCore::Core::SbResult Dx12FrameOwner::CommitClose( HRESULT result, const char* operation )
{
    return m_recording.CommitClose( result, operation );
}


SkullbonezCore::Core::SbResult Dx12FrameOwner::CommitWait( const SkullbonezCore::Core::SbResult& result )
{
    return m_recording.CommitWait( result );
}


SkullbonezCore::Core::SbResult Dx12FrameOwner::RetainFailure( const SkullbonezCore::Core::SbResult& result )
{
    return m_recording.RetainFailure( result );
}


SkullbonezCore::Core::SbResult Dx12FrameOwner::RetainDeviceLoss( const char* operation, HRESULT result )
{
    return m_recording.RetainFailure( m_deviceHealth.RetainDeviceLoss( operation, result ) );
}


SkullbonezCore::Core::SbResult Dx12FrameOwner::SignalFrame( UINT64& outFenceValue )
{
    const SkullbonezCore::Core::SbResult result = m_device.FrameFence().Signal( outFenceValue );
    m_submittedWork.CommitSignal( result, outFenceValue );
    return result.Ok() ? result : m_recording.RetainFailure( result );
}


SkullbonezCore::Core::SbResult Dx12FrameOwner::WaitForFrameFence( UINT64 fenceValue )
{
    const SkullbonezCore::Core::SbResult result = m_device.FrameFence().WaitForValue( fenceValue );
    m_submittedWork.CommitWait( result, fenceValue );
    return result.Ok() ? result : m_recording.CommitWait( result );
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
    m_retirement.ReleaseCompleted( m_device, m_descriptors, m_submittedWork, releaseUnfenced );
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


void Dx12FrameOwner::RetireResource( ID3D12Resource* resource, UINT descriptorIndex )
{
    if ( !resource )
    {
        RetireStaticDescriptor( descriptorIndex );
        return;
    }

    const bool fenceReady = m_device.FrameFence().IsReady();

    if ( fenceReady )
    {
        m_submittedWork.ObserveCompletedFence( m_device.FrameFence().CompletedValue() );
    }

    const UINT64 completedFence = fenceReady ? m_device.FrameFence().CompletedValue() : 0;
    bool hasOutstandingFrameWork = m_recording.IsOpen();

    for ( const UINT64 frameFence : m_frameFenceValues )
    {
        hasOutstandingFrameWork = hasOutstandingFrameWork || frameFence > completedFence;
    }

    if ( !hasOutstandingFrameWork && m_submittedWork.CanReleaseWithoutFence() )
    {
        resource->Release();
        m_descriptors.FreeStatic( descriptorIndex );
        return;
    }

    m_retirement.Quarantine( resource, descriptorIndex );
}


void Dx12FrameOwner::RetireResource( ID3D12Resource* resource, UINT descriptorIndex, Dx12CpuDescriptorKind cpuKind,
                                     UINT cpuDescriptorIndex )
{
    if ( !resource )
    {
        if ( descriptorIndex != UINT_MAX )
        {
            RetireStaticDescriptor( descriptorIndex );
        }

        if ( cpuDescriptorIndex != UINT_MAX )
        {
            m_descriptors.FreeCpu( cpuKind, cpuDescriptorIndex );
        }

        return;
    }

    const bool fenceReady = m_device.FrameFence().IsReady();

    if ( fenceReady )
    {
        m_submittedWork.ObserveCompletedFence( m_device.FrameFence().CompletedValue() );
    }

    const UINT64 completedFence = fenceReady ? m_device.FrameFence().CompletedValue() : 0;
    bool hasOutstandingFrameWork = m_recording.IsOpen();

    for ( const UINT64 frameFence : m_frameFenceValues )
    {
        hasOutstandingFrameWork = hasOutstandingFrameWork || frameFence > completedFence;
    }

    if ( !hasOutstandingFrameWork && m_submittedWork.CanReleaseWithoutFence() )
    {
        resource->Release();

        if ( descriptorIndex != UINT_MAX )
        {
            m_descriptors.FreeStatic( descriptorIndex );
        }

        if ( cpuDescriptorIndex != UINT_MAX )
        {
            m_descriptors.FreeCpu( cpuKind, cpuDescriptorIndex );
        }

        return;
    }

    m_retirement.Quarantine( resource, descriptorIndex, cpuKind, cpuDescriptorIndex );
}


void Dx12FrameOwner::RetireStaticDescriptor( UINT descriptorIndex )
{
    // Lifetime: descriptor rows use the resource retirement proof because a
    // transient shader-visible copy may outlive the registry entry that named
    // its staging row. Reuse is legal only after all covering frame fences.
    const bool fenceReady = m_device.FrameFence().IsReady();

    if ( fenceReady )
    {
        m_submittedWork.ObserveCompletedFence( m_device.FrameFence().CompletedValue() );
    }

    const UINT64 completedFence = fenceReady ? m_device.FrameFence().CompletedValue() : 0;
    bool hasOutstandingFrameWork = m_recording.IsOpen();

    for ( const UINT64 frameFence : m_frameFenceValues )
    {
        hasOutstandingFrameWork = hasOutstandingFrameWork || frameFence > completedFence;
    }

    if ( !hasOutstandingFrameWork && m_submittedWork.CanReleaseWithoutFence() )
    {
        m_descriptors.FreeStatic( descriptorIndex );
        return;
    }

    m_retirement.QuarantineStaticDescriptor( descriptorIndex );
}


bool Dx12DrawGate::PrepareDraw()
{
    return m_owner.PrepareDraw();
}


bool Dx12DrawGate::PrepareFramebufferBind()
{
    return m_owner.PrepareFramebufferBind();
}


bool Dx12FrameOwner::PreparePipelineDraw( VertexFormat12 format, bool instanced, const InstancedMeshDX12* instancedMesh,
                                          const DynamicVBDX12* dynamicVertexBuffer, const RasterStateDesc& rasterState )
{
    if ( !PrepareDraw() )
    {
        return false;
    }

    return m_pipeline.PrepareDraw( Device(), CommandList(), m_recording, m_textures, format, instanced, instancedMesh,
                                   dynamicVertexBuffer, rasterState );
}


bool Dx12FrameOwner::PrecompilePipelineDraw( VertexFormat12 format, bool instanced, const InstancedMeshDX12* instancedMesh,
                                             const DynamicVBDX12* dynamicVertexBuffer,
                                             const RasterStateDesc& declaredRasterState )
{
    if ( !PrepareDraw() )
    {
        return false;
    }

    // Why: pass preparation warms the exact declared recipe before the first
    // submission. No command-list state is changed by this operation.
    return m_pipeline.PrecompileDraw( Device(), format, instanced, instancedMesh, dynamicVertexBuffer, declaredRasterState );
}


bool Dx12DrawGate::PreparePipelineDraw( VertexFormat12 format, bool instanced, const InstancedMeshDX12* instancedMesh,
                                        const DynamicVBDX12* dynamicVertexBuffer, const RasterStateDesc& rasterState )
{
    return m_owner.PreparePipelineDraw( format, instanced, instancedMesh, dynamicVertexBuffer, rasterState );
}


bool Dx12DrawGate::PrecompilePipelineDraw( VertexFormat12 format, bool instanced, const InstancedMeshDX12* instancedMesh,
                                           const DynamicVBDX12* dynamicVertexBuffer,
                                           const RasterStateDesc& declaredRasterState )
{
    return m_owner.PrecompilePipelineDraw( format, instanced, instancedMesh, dynamicVertexBuffer, declaredRasterState );
}


bool Dx12DrawGate::CanRecord() const
{
    return m_owner.CanRecord();
}


D3D12_GPU_VIRTUAL_ADDRESS
Dx12UploadReservations::ReserveUpload( UINT64 size, UINT64 alignment, RenderUploadCategory category )
{
    return m_owner.ReserveUpload( size, alignment, category );
}


D3D12_GPU_VIRTUAL_ADDRESS Dx12UploadReservations::ReserveGeometryUpload( UINT64 vertexBytes, UINT64 constantBytes,
                                                                         RenderUploadCategory vertexCategory )
{
    return m_owner.ReserveGeometryUpload( vertexBytes, constantBytes, vertexCategory );
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


void Dx12ResourceRelease::Retire( ID3D12Resource* resource, UINT descriptorIndex )
{
    m_owner.RetireResource( resource, descriptorIndex );
}


void Dx12ResourceRelease::Retire( ID3D12Resource* resource, UINT descriptorIndex, Dx12CpuDescriptorKind cpuKind,
                                  UINT cpuDescriptorIndex )
{
    m_owner.RetireResource( resource, descriptorIndex, cpuKind, cpuDescriptorIndex );
}


void Dx12ResourceRelease::RetireStaticDescriptor( UINT descriptorIndex )
{
    m_owner.RetireStaticDescriptor( descriptorIndex );
}


SkullbonezCore::Core::SbResult Dx12CaptureFrame::EnsureOpen()
{
    return m_owner.EnsureOpen();
}


bool Dx12CaptureFrame::HasFailure() const
{
    return m_owner.HasFailure();
}


const SkullbonezCore::Core::SbResult& Dx12CaptureFrame::CurrentResult() const
{
    return m_owner.CurrentResult();
}


RenderGraphResourceAccess Dx12CaptureFrame::BackBufferAccess() const
{
    return m_owner.BackBufferAccess();
}


bool Dx12CaptureFrame::TransitionBackbuffer( const char* passName, RenderGraphResourceAccess after )
{
    return m_owner.TransitionBackbuffer( passName, after );
}


ID3D12Device* Dx12CaptureFrame::Device() const
{
    return m_owner.Device();
}


ID3D12GraphicsCommandList* Dx12CaptureFrame::CommandList() const
{
    return m_owner.CommandList();
}


ID3D12Resource* Dx12CaptureFrame::BackBuffer() const
{
    return m_owner.RenderTarget( m_owner.FrameIndex() );
}


Dx12CaptureSubmitOutcome Dx12CaptureFrame::SubmitAndWait()
{
    Dx12CaptureSubmitOutcome outcome;
    m_owner.AssertProfilerClosed( "CaptureBackbuffer" );
    outcome.result = m_owner.CommitClose( m_owner.CommandList()->Close(), "CaptureBackbuffer command list Close" );

    if ( !outcome.result.Ok() )
    {
        outcome.readbackUseUncertain = true;
        outcome.failedOperation = "Close";
        return outcome;
    }

    outcome.result = m_owner.SubmitClosed();

    if ( !outcome.result.Ok() )
    {
        // Invariant: SubmitClosed reports failure only before ExecuteCommandLists;
        // the local readback remains safe for ordinary destruction.
        outcome.failedOperation = "Submit";
        return outcome;
    }

    outcome.result = m_owner.CommitWait( m_owner.WaitForGpu() );

    if ( !outcome.result.Ok() )
    {
        outcome.readbackUseUncertain = true;
        outcome.failedOperation = "Wait";
    }

    return outcome;
}


SkullbonezCore::Core::SbResult Dx12DiagnosticsFrame::EnsureOpen()
{
    return m_owner.EnsureOpen();
}


bool Dx12DiagnosticsFrame::CanRecord() const
{
    return m_owner.CanRecord();
}


ID3D12GraphicsCommandList* Dx12DiagnosticsFrame::CommandList() const
{
    return m_owner.CommandList();
}


bool Dx12DiagnosticsFrame::FrameFenceReady() const
{
    return m_owner.m_device.FrameFence().IsReady();
}


UINT64 Dx12DiagnosticsFrame::CompletedFenceValue() const
{
    return m_owner.m_device.FrameFence().CompletedValue();
}


SkullbonezCore::Core::SbResult Dx12DiagnosticsFrame::WaitForFenceValue( UINT64 fenceValue ) const
{
    return m_owner.m_device.FrameFence().WaitForValue( fenceValue );
}


void Dx12DiagnosticsFrame::ConfigureFaultInjection( const char* token )
{
    m_owner.m_faultInjection.Configure( token );
}
