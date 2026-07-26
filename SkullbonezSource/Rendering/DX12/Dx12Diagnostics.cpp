/*
File: SkullbonezSource/Rendering/DX12/Dx12Diagnostics.cpp
Purpose:
  Implements DX12 GPU timing, draw evidence, fault setup, and architecture logs.

Summary:
  Timestamp queries resolve only written slot runs, publish one covering fence,
  and map non-blockingly during ordinary frames. Draw, visibility, and trace
  evidence share one reset boundary and one high-water report.

Glossary:
  Query heap: GPU timestamp slot storage written by EndQuery.
  Readback buffer: CPU-visible destination of ResolveQueryData.
  Slot run: Contiguous written query indices resolved without touching gaps.
  Architecture log: Cold aggregate of descriptor, upload, and draw high-water.

Invariants:
  - Unwritten timestamp slots are never resolved.
  - A pending sample is replaced without blocking a free-running Present loop.
  - Cold fault configuration cannot expose broader frame-owner authority.
  - Diagnostics logging never mutates renderer ownership state.

Related:
  - SkullbonezSource/Rendering/DX12/Dx12Diagnostics.h
  - SkullbonezSource/Rendering/DX12/Dx12FrameOwner.cpp
  - Agentic/Reference/comment-style-guide.md
*/
#include "Dx12Diagnostics.h"

#include "Dx12DescriptorHeaps.h"
#include "Dx12FrameOwner.h"
#include "RenderBackendDX12.h"
#include "../RenderRasterBindingContract.h"
#include "../../Core/FatalError.h"
#include "../../Core/Log.h"
#include "../../Core/PlatformProfiler.h"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <wrl/client.h>

using namespace SkullbonezCore::Rendering;
using Microsoft::WRL::ComPtr;

void Dx12Diagnostics::BindSources( Dx12RenderDevice& device, Dx12DescriptorHeaps& descriptors, Dx12FrameOwner& frame,
                                   Dx12TextureOwner& textures, Dx12PipelineOwner& pipeline, Dx12GeometryOwner& geometry,
                                   Dx12GraphTransientPool& graphTransients, Dx12RaytracingOwner& raytracing )
{
    m_device = &device;
    m_descriptors = &descriptors;
    m_frame = &frame;
    m_textures = &textures;
    m_pipeline = &pipeline;
    m_geometry = &geometry;
    m_graphTransients = &graphTransients;
    m_raytracing = &raytracing;
}

RenderCapabilities Dx12Diagnostics::GetCapabilities() const
{
    RenderCapabilities capabilities;
    capabilities.supportsBackbufferCapture = true;
    capabilities.supportsGpuTimers = SupportsGpuTimers();
    capabilities.supportsDxrReflection = m_raytracing && m_raytracing->Supported();
    capabilities.supportsDebugLines = true;
    return capabilities;
}

RenderMemoryStats Dx12Diagnostics::GetRenderMemoryStats() const
{

    // Concept: one value snapshot combines bounded engine tables with the
    // operating system's memory charge for the adapter backing this device.
    // It observes every owner but cannot resize or mutate any of them.
    RenderMemoryStats stats;
    strcpy_s( stats.backendName, sizeof( stats.backendName ), "DirectX 12" );
    stats.available = m_device && m_device->Device();
    stats.recreationGeneration = m_device ? m_device->RecreationGeneration() : 0;

    if ( !stats.available || !m_descriptors || !m_frame || !m_textures || !m_pipeline || !m_geometry || !m_graphTransients )
    {
        return stats;
    }

    const Dx12CpuDescriptorAllocatorStats rtvStats = m_descriptors->RtvStats();
    const Dx12CpuDescriptorAllocatorStats dsvStats = m_descriptors->DsvStats();
    const Dx12DescriptorAllocatorStats srvStats = m_descriptors->GetStats();
    stats.rtvDescriptorsUsed = rtvStats.used;
    stats.rtvDescriptorsCapacity = rtvStats.capacity;
    stats.dsvDescriptorsUsed = dsvStats.used;
    stats.dsvDescriptorsCapacity = dsvStats.capacity;
    stats.srvStaticDescriptorsUsed = srvStats.staticUsed;
    stats.srvStaticDescriptorsCapacity = srvStats.staticCapacity;
    stats.srvStaticDescriptorsHighWater = srvStats.staticHighWater;
    stats.srvTransientDescriptorsUsedThisFrame = srvStats.transientUsedThisFrame;
    stats.srvTransientDescriptorsCapacityPerFrame = srvStats.transientCapacityPerFrame;
    stats.srvTransientDescriptorsPeakThisRun = srvStats.transientPeakThisRun;

    for ( int frameIndex = 0; frameIndex < Dx12FrameOwner::FRAME_COUNT; ++frameIndex )
    {
        const Dx12UploadArenaStats uploadStats = m_frame->Uploads().GetStats( static_cast<UINT>( frameIndex ) );
        stats.uploadCapacityBytes += uploadStats.capacityBytes;
        stats.uploadUsedBytes += uploadStats.usedBytes;
        stats.uploadPeakBytes = (std::max)( stats.uploadPeakBytes, uploadStats.peakBytes );

        for ( std::size_t categoryIndex = 0; categoryIndex < RENDER_UPLOAD_CATEGORY_COUNT; ++categoryIndex )
        {
            stats.uploadCategoryUsedBytes[categoryIndex] += uploadStats.categoryUsedBytes[categoryIndex];
            stats.uploadCategoryPeakBytes[categoryIndex] = (std::max)( stats.uploadCategoryPeakBytes[categoryIndex],
                                                                       uploadStats.categoryPeakBytes[categoryIndex] );
        }
    }

    stats.uploadFlushCount = m_frame->UploadFlushCount();
    stats.uploadDropCount = m_frame->UploadDropCount();
    const Dx12ReadbackBufferStats timerStats = TimerReadbackStats();
    stats.timerReadbackBytes = timerStats.ready ? timerStats.sizeBytes : 0;
    stats.textureRegistryCount = m_textures->RegistryCount();
    stats.textureRegistryCapacity = m_textures->RegistryCapacity();
    stats.dynamicVertexBufferCount = m_geometry->DynamicCount();
    stats.dynamicVertexBufferCapacity = m_geometry->DynamicCapacity();
    stats.instancedMeshCount = m_geometry->InstancedCount();
    stats.instancedMeshCapacity = m_geometry->InstancedCapacity();
    stats.psoCacheCount = m_pipeline->CacheCount();
    stats.psoCacheHitCount = m_pipeline->CacheHitCount();
    stats.psoCacheMissCount = m_pipeline->CacheMissCount();
    stats.precompiledPsoCount = m_pipeline->PrecompiledPsoCount();
    stats.graphTransientCount = m_graphTransients->Size();
    stats.graphTransientCapacity = m_graphTransients->Capacity();

    if ( IDXGIFactory4* factory = m_device->Factory() )
    {

        // Why: adapter zero is not necessarily the adapter that created the
        // device. Match the device LUID before sampling graphics-kernel budgets.
        const LUID deviceLuid = m_device->Device()->GetAdapterLuid();
        ComPtr<IDXGIAdapter3> activeAdapter;

        for ( UINT adapterIndex = 0;; ++adapterIndex )
        {
            ComPtr<IDXGIAdapter1> adapter;
            const HRESULT enumResult = factory->EnumAdapters1( adapterIndex, adapter.GetAddressOf() );

            if ( enumResult == DXGI_ERROR_NOT_FOUND )
            {
                break;
            }

            DXGI_ADAPTER_DESC1 desc = {};

            if ( FAILED( enumResult ) || FAILED( adapter->GetDesc1( &desc ) ) ||
                 desc.AdapterLuid.HighPart != deviceLuid.HighPart || desc.AdapterLuid.LowPart != deviceLuid.LowPart )
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

            const bool localAvailable = SUCCEEDED( activeAdapter->QueryVideoMemoryInfo( 0, DXGI_MEMORY_SEGMENT_GROUP_LOCAL, &localInfo ) );

            const bool nonLocalAvailable = SUCCEEDED( activeAdapter->QueryVideoMemoryInfo( 0, DXGI_MEMORY_SEGMENT_GROUP_NON_LOCAL, &nonLocalInfo ) );

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
                stats.nonLocalAvailableForReservationBytes = static_cast<uint64_t>( nonLocalInfo.AvailableForReservation );
            }
        }
    }

    return stats;
}

void Dx12Diagnostics::GpuTimerBegin( int markerIndex )
{

    if ( m_frame )
    {
        GpuTimerBegin( m_frame->DiagnosticsFrame(), markerIndex );
    }
}

void Dx12Diagnostics::GpuTimerEnd( int markerIndex )
{

    if ( m_frame )
    {
        GpuTimerEnd( m_frame->DiagnosticsFrame(), markerIndex );
    }
}

void Dx12Diagnostics::GpuTimerInvalidate()
{

    if ( m_frame )
    {
        GpuTimerInvalidate( m_frame->DiagnosticsFrame() );
    }
}

bool Dx12Diagnostics::GpuTimerRead( int markerIndex, float& outMilliseconds )
{
    return m_frame && GpuTimerRead( m_frame->DiagnosticsFrame(), markerIndex, outMilliseconds );
}

void Dx12Diagnostics::PlatformProfilerGpuBegin( const char* name, uint32_t hash )
{

    if ( m_frame && SkullbonezCore::Core::PlatformProfiler::IsEnabled() )
    {
        m_frame->BeginProfilerEvent( name, hash );
    }
}

void Dx12Diagnostics::PlatformProfilerGpuEnd()
{

    if ( m_frame )
    {
        m_frame->EndProfilerEvent();
    }
}

void Dx12Diagnostics::PlatformProfilerGpuMarker( const char* name, uint32_t hash )
{

    if ( !m_frame || !m_device || !SkullbonezCore::Core::PlatformProfiler::IsEnabled() )
    {
        return;
    }

#if SKULLBONEZ_PLATFORM_PROFILER_HAVE_PIX3

    if ( !m_device->CommandList() || !m_frame->EnsureOpen().ok )
    {
        return;
    }

    char markerNameBuffer[SkullbonezCore::Core::PlatformProfiler::MAX_DECORATED_MARKER_NAME_CHARS];
    const char* markerName = SkullbonezCore::Core::PlatformProfiler::AreDetailedRangesEnabled()
                                 ? SkullbonezCore::Core::PlatformProfiler::DecorateMarkerName( name, "_GPU",
                                                                                               markerNameBuffer,
                                                                                               sizeof( markerNameBuffer ) )
                                 : name;

    PIXSetMarker( m_device->CommandList(), SkullbonezCore::Core::PlatformProfiler::ColorForMarker( markerName, hash ), "%s",
                  markerName );

#else
    (void)name;
    (void)hash;
#endif
}

SkullbonezCore::Core::SbResult Dx12Diagnostics::InitializeGpuTimers( ID3D12Device* device, ID3D12CommandQueue* queue )
{
    ShutdownGpuTimers();

    if ( !device || !queue )
    {
        return SkullbonezCore::Core::SbResult::Failure( "Dx12Diagnostics",
                                                        "GPU timer initialization requires a device and graphics queue." );
    }

    D3D12_QUERY_HEAP_DESC heapDesc = {};
    heapDesc.Type = D3D12_QUERY_HEAP_TYPE_TIMESTAMP;
    heapDesc.Count = static_cast<UINT>( TIMER_HEAP_SIZE );

    if ( FAILED( device->CreateQueryHeap( &heapDesc, IID_PPV_ARGS( &m_gpuTimers.queryHeap ) ) ) )
    {

        // Lane R: GPU timing is optional diagnostics. Renderer startup remains
        // usable when the driver cannot expose a timestamp query heap.
        return SkullbonezCore::Core::SbResult::Success();
    }

    NameDx12Object( m_gpuTimers.queryHeap, L"Skullbonez DX12 GPU Timer Query Heap" );

    const UINT64 readbackBytes = static_cast<UINT64>( TIMER_HEAP_SIZE ) * sizeof( uint64_t );

    if ( !m_gpuTimers.readback.InitBuffer( device, readbackBytes, L"Skullbonez DX12 GPU Timer Readback Buffer" ) )
    {
        m_gpuTimers.queryHeap->Release();
        m_gpuTimers.queryHeap = nullptr;
        return SkullbonezCore::Core::SbResult::Success();
    }

    const HRESULT frequencyResult = queue->GetTimestampFrequency( &m_gpuTimers.frequency );

    if ( FAILED( frequencyResult ) )
    {

        // Lane R: frequency is driver/device capability discovered at startup.
        ShutdownGpuTimers();
        return SkullbonezCore::Core::SbResult::Failure( "Dx12Diagnostics", "GetTimestampFrequency failed (HRESULT 0x%08X)",
                                                        static_cast<unsigned int>( frequencyResult ) );
    }

    return SkullbonezCore::Core::SbResult::Success();
}

void Dx12Diagnostics::ShutdownGpuTimers()
{
    m_gpuTimers.readback.Reset();

    if ( m_gpuTimers.queryHeap )
    {
        m_gpuTimers.queryHeap->Release();
    }

    m_gpuTimers.queryHeap = nullptr;
    std::memset( m_gpuTimers.resultMilliseconds, 0, sizeof( m_gpuTimers.resultMilliseconds ) );
    std::memset( m_gpuTimers.resultValid, 0, sizeof( m_gpuTimers.resultValid ) );
    std::memset( m_gpuTimers.slotWritten, 0, sizeof( m_gpuTimers.slotWritten ) );
    m_gpuTimers.frequency = 1;
    m_gpuTimers.readPending = false;
    m_gpuTimers.readFenceValue = 0;
}

void Dx12Diagnostics::ConsumeGpuTimerReadback( Dx12DiagnosticsFrame& frame, bool waitForFence )
{

    if ( !m_gpuTimers.queryHeap || !m_gpuTimers.readPending || !m_gpuTimers.readback.IsReady() || !frame.FrameFenceReady() )
    {
        return;
    }

    if ( waitForFence )
    {
        const SkullbonezCore::Core::SbResult waitResult = frame.WaitForFenceValue( m_gpuTimers.readFenceValue );

        if ( !waitResult.ok )
        {
            SkullbonezCore::Core::Log().WriteEventf( "dx12_gpu_timer_wait_failed owner=%s message=%s",
                                                     waitResult.error.owner, waitResult.error.message );

            m_gpuTimers.readPending = false;
            return;
        }
    }
    else if ( frame.CompletedFenceValue() < m_gpuTimers.readFenceValue )
    {

        // Why: catch a fence that is only a few microseconds behind without a
        // kernel wait. A genuinely incomplete sample remains pending.

        for ( int spin = 0; spin < 512; ++spin )
        {
            YieldProcessor();

            if ( frame.CompletedFenceValue() >= m_gpuTimers.readFenceValue )
            {
                break;
            }
        }

        if ( frame.CompletedFenceValue() < m_gpuTimers.readFenceValue )
        {
            return;
        }
    }

    const UINT64 readbackBytes = static_cast<UINT64>( TIMER_HEAP_SIZE ) * sizeof( uint64_t );
    const uint8_t* data = m_gpuTimers.readback.MapRead( readbackBytes );

    if ( !data )
    {
        SkullbonezCore::Core::Log().WriteEventf( "dx12_gpu_timer_map_failed" );
        m_gpuTimers.readPending = false;
        return;
    }

    std::memset( m_gpuTimers.resultMilliseconds, 0, sizeof( m_gpuTimers.resultMilliseconds ) );
    std::memset( m_gpuTimers.resultValid, 0, sizeof( m_gpuTimers.resultValid ) );

    for ( int markerIndex = 0; markerIndex < TIMER_HEAP_MARKERS; ++markerIndex )
    {
        uint64_t begin = 0;
        uint64_t end = 0;
        std::memcpy( &begin, data + markerIndex * 2 * sizeof( uint64_t ), sizeof( begin ) );
        std::memcpy( &end, data + ( markerIndex * 2 + 1 ) * sizeof( uint64_t ), sizeof( end ) );

        if ( end > begin && m_gpuTimers.frequency > 0 )
        {
            m_gpuTimers.resultMilliseconds[markerIndex] = static_cast<float>( static_cast<double>( end - begin ) / static_cast<double>( m_gpuTimers.frequency ) * 1000.0 );

            m_gpuTimers.resultValid[markerIndex] = true;
        }
    }

    m_gpuTimers.readback.UnmapNoWrite();
    m_gpuTimers.readPending = false;
}

void Dx12Diagnostics::GpuTimerBegin( Dx12DiagnosticsFrame& frame, int markerIndex )
{

    if ( !m_gpuTimers.queryHeap || markerIndex < 0 || markerIndex >= TIMER_HEAP_MARKERS || !frame.EnsureOpen().ok )
    {
        return;
    }

    const int slot = markerIndex * 2;
    frame.CommandList()->EndQuery( m_gpuTimers.queryHeap, D3D12_QUERY_TYPE_TIMESTAMP, static_cast<UINT>( slot ) );
    m_gpuTimers.slotWritten[slot] = true;
}

void Dx12Diagnostics::GpuTimerEnd( Dx12DiagnosticsFrame& frame, int markerIndex )
{

    if ( !m_gpuTimers.queryHeap || !frame.CanRecord() || markerIndex < 0 || markerIndex >= TIMER_HEAP_MARKERS )
    {
        return;
    }

    const int slot = markerIndex * 2 + 1;
    frame.CommandList()->EndQuery( m_gpuTimers.queryHeap, D3D12_QUERY_TYPE_TIMESTAMP, static_cast<UINT>( slot ) );
    m_gpuTimers.slotWritten[slot] = true;
}

void Dx12Diagnostics::GpuTimerInvalidate( Dx12DiagnosticsFrame& frame )
{

    if ( m_gpuTimers.readPending )
    {
        ConsumeGpuTimerReadback( frame, true );
    }

    // Invariant: retain the most recent valid values across marker-table reset;
    // the first post-reset fence may not be ready during the non-blocking read.
    std::memset( m_gpuTimers.slotWritten, 0, sizeof( m_gpuTimers.slotWritten ) );
    m_gpuTimers.readPending = false;
    m_gpuTimers.readFenceValue = 0;
}

bool Dx12Diagnostics::GpuTimerRead( Dx12DiagnosticsFrame& frame, int markerIndex, float& outMilliseconds )
{
    ConsumeGpuTimerReadback( frame, false );

    if ( markerIndex < 0 || markerIndex >= TIMER_HEAP_MARKERS || !m_gpuTimers.resultValid[markerIndex] )
    {
        return false;
    }

    outMilliseconds = m_gpuTimers.resultMilliseconds[markerIndex];
    return true;
}

bool Dx12Diagnostics::ResolveWrittenGpuTimers( Dx12DiagnosticsFrame& frame )
{
    bool resolved = false;

    if ( !m_gpuTimers.queryHeap )
    {
        return false;
    }

    int slot = 0;

    while ( slot < TIMER_HEAP_SIZE )
    {

        if ( !m_gpuTimers.slotWritten[slot] )
        {
            ++slot;
            continue;
        }

        const int start = slot;

        while ( slot < TIMER_HEAP_SIZE && m_gpuTimers.slotWritten[slot] )
        {
            ++slot;
        }

        frame.CommandList()->ResolveQueryData( m_gpuTimers.queryHeap, D3D12_QUERY_TYPE_TIMESTAMP, static_cast<UINT>( start ),
                                               static_cast<UINT>( slot - start ), m_gpuTimers.readback.Resource(),
                                               static_cast<UINT>( start * sizeof( uint64_t ) ) );

        resolved = true;
    }

    std::memset( m_gpuTimers.slotWritten, 0, sizeof( m_gpuTimers.slotWritten ) );
    return resolved;
}

void Dx12Diagnostics::PublishResolvedGpuTimerFence( bool resolvedThisFrame, UINT64 fenceValue )
{

    if ( !resolvedThisFrame )
    {
        return;
    }

    // Why: free-running Present may lap the GPU. Replacing one stale sample is
    // preferable to blocking the entire renderer diagnostics path.
    m_gpuTimers.readPending = true;
    m_gpuTimers.readFenceValue = fenceValue;
}

void Dx12Diagnostics::ResetFrameDrawCalls()
{
    m_frameDrawCallHighWater = (std::max)( m_frameDrawCallHighWater, m_frameDrawCallCount );
    m_frameDrawCallCount = 0;
    m_frameVisibilityStats = RenderVisibilityStats();
    m_drawCallTrace.BeginFrame();
}

void Dx12Diagnostics::RecordDrawCall( const DrawCallRecord& record )
{
    ++m_frameDrawCallCount;
    m_drawCallTrace.RecordDrawCall( record );
}

int Dx12Diagnostics::DrawCallHighWater() const
{
    return (std::max)( m_frameDrawCallHighWater, m_frameDrawCallCount );
}

void Dx12Diagnostics::RecordVisibility( RenderVisibilityView view, int candidates, int submitted, int culled, int draws )
{
    const int index = static_cast<int>( view );

    if ( index < 0 || index >= static_cast<int>( RenderVisibilityView::Count ) )
    {
        return;
    }

    RenderVisibilityViewStats& stats = m_frameVisibilityStats.views[index];
    stats.candidates += candidates;
    stats.submitted += submitted;
    stats.culled += culled;
    stats.draws += draws;
}

void Dx12Diagnostics::PushDrawCallTraceScope( const char* fullPathOrLeaf, uint32_t hash )
{
    m_drawCallTrace.PushScope( fullPathOrLeaf, hash );
}

void Dx12Diagnostics::PopDrawCallTraceScope( uint32_t hash )
{
    m_drawCallTrace.PopScope( hash );
}

void Dx12Diagnostics::ConfigureFaultInjection( Dx12DiagnosticsFrame& frame )
{
#ifdef _DEBUG
    char token[64] = {};
    const DWORD length = GetEnvironmentVariableA( "SKULLBONEZ_DX12_FAULT", token, static_cast<DWORD>( sizeof( token ) ) );

    frame.ConfigureFaultInjection( length > 0 && length < sizeof( token ) ? token : nullptr );
#else
    frame.ConfigureFaultInjection( nullptr );
#endif
}

void Dx12Diagnostics::ReportArchitectureStats( const char* reason, const Dx12DescriptorHeaps& descriptors,
                                               const Dx12FrameOwner& frame ) const
{
    const Dx12CpuDescriptorAllocatorStats rtvStats = descriptors.RtvStats();
    const Dx12CpuDescriptorAllocatorStats dsvStats = descriptors.DsvStats();
    const Dx12DescriptorAllocatorStats descriptorStats = descriptors.GetStats();
    UINT64 uploadCapacityBytes = 0;
    UINT64 uploadPeakBytes = 0;
    uint64_t categoryPeakBytes[RENDER_UPLOAD_CATEGORY_COUNT] = {};

    for ( int frameIndex = 0; frameIndex < Dx12FrameOwner::FRAME_COUNT; ++frameIndex )
    {
        const Dx12UploadArenaStats uploadStats = frame.Uploads().GetStats( static_cast<UINT>( frameIndex ) );
        uploadCapacityBytes += uploadStats.capacityBytes;
        uploadPeakBytes = (std::max)( uploadPeakBytes, uploadStats.peakBytes );

        for ( std::size_t category = 0; category < RENDER_UPLOAD_CATEGORY_COUNT; ++category )
        {
            categoryPeakBytes[category] = (std::max)( categoryPeakBytes[category], uploadStats.categoryPeakBytes[category] );
        }
    }

    SkullbonezCore::Core::Log()
        .WriteEventf( "dx12_render_architecture_stats reason=%s raster_contract=%s root_parameters=%u "
                      "raster_bindless_slots=%d texture_indices_register=b%u "
                      "rtv_descriptors=%u/%u dsv_descriptors=%u/%u static_srvs=%u/%u static_srv_high_water=%u "
                      "transient_srv_peak=%u/%u draw_call_high_water=%d "
                      "upload_peak_bytes=%llu upload_capacity_bytes=%llu "
                      "upload_constants_peak_bytes=%llu upload_dynamic_peak_bytes=%llu "
                      "upload_instances_peak_bytes=%llu upload_textures_peak_bytes=%llu "
                      "upload_overlay_peak_bytes=%llu upload_flushes=%llu upload_drops=%llu",
                      reason ? reason : "unknown", UnifiedRasterRootSignature::NAME,
                      UnifiedRasterRootSignature::ROOT_PARAMETER_COUNT, UnifiedRasterRootSignature::TEXTURE_SLOT_COUNT,
                      UnifiedRasterRootSignature::SHADER_REGISTER_TEXTURE_INDICES, rtvStats.used, rtvStats.capacity,
                      dsvStats.used, dsvStats.capacity, descriptorStats.staticUsed, descriptorStats.staticCapacity,
                      descriptorStats.staticHighWater, descriptorStats.transientPeakThisRun,
                      descriptorStats.transientCapacityPerFrame, DrawCallHighWater(),
                      static_cast<unsigned long long>( uploadPeakBytes ),
                      static_cast<unsigned long long>( uploadCapacityBytes ),
                      static_cast<unsigned long long>( categoryPeakBytes[static_cast<size_t>( RenderUploadCategory::Constants )] ),
                      static_cast<unsigned long long>( categoryPeakBytes[static_cast<size_t>( RenderUploadCategory::DynamicVertex )] ),
                      static_cast<unsigned long long>( categoryPeakBytes[static_cast<size_t>( RenderUploadCategory::InstanceData )] ),
                      static_cast<unsigned long long>( categoryPeakBytes[static_cast<size_t>( RenderUploadCategory::TextureRows )] ),
                      static_cast<unsigned long long>( categoryPeakBytes[static_cast<size_t>( RenderUploadCategory::RetainedGeometry )] ),
                      static_cast<unsigned long long>( frame.UploadFlushCount() ),
                      static_cast<unsigned long long>( frame.UploadDropCount() ) );
}
