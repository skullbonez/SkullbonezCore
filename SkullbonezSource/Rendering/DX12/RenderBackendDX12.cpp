/*
File: SkullbonezSource/Rendering/DX12/RenderBackendDX12.cpp
Purpose:
  Implements production DX12 device orchestration and backend-facing resource work.

Summary:
  RenderBackendDX12.cpp composes the concrete DX12 owners, controls device and
  swap-chain lifecycle, and delegates backend-facing resource operations.
  Frame epoch, deferred retirement, descriptor heaps, capture, and graph
  transient state live in dedicated concrete owners.

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
  - SkullbonezSource/Rendering/DX12/Dx12FrameOwner.cpp
  - SkullbonezSource/Rendering/DX12/Dx12GraphTransientPool.cpp
  - SkullbonezSource/Rendering/DX12/Dx12DeferredReleaseOwner.cpp
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
namespace Runtime = SkullbonezCore::Runtime;


// --- Helpers ---
static void ReportDX12DescriptorHeapExhausted( const char* heapName, UINT nextIndex, UINT capacity )
{
    const char* name = heapName ? heapName : "unknown";
    fprintf( stderr, "FATAL: DX12 %s heap exhausted (next=%u capacity=%u)\n", name, nextIndex, capacity );
    fprintf( stdout, "FATAL: DX12 %s heap exhausted (next=%u capacity=%u)\n", name, nextIndex, capacity );
    fflush( stderr );
    fflush( stdout );
    SkullbonezCore::Core::Log().WriteEventf( "dx12_descriptor_heap_exhausted heap=%s next=%u capacity=%u",
                                             name,
                                             nextIndex,
                                             capacity );
    SkullbonezCore::Core::Log().FlushAll();
}

static inline SkullbonezCore::Core::SbResult Dx12BackendInitResult( HRESULT hr, const char* msg )
{
    if ( FAILED( hr ) )
    {
        // Lane R: renderer startup depends on the adapter, driver, window, and
        // available descriptor resources. Return a bounded owner/message so the
        // process bootstrap can report the environment failure cleanly.
        return SkullbonezCore::Core::SbResult::Failure( "Rendering/DX12",
                                                        "%s (HRESULT 0x%08X)",
                                                        msg ? msg : "DX12 backend startup call failed",
                                                        static_cast<unsigned int>( hr ) );
    }
    return SkullbonezCore::Core::SbResult::Success();
}

static inline SkullbonezCore::Core::SbResult Dx12BackendOperationResult( HRESULT hr, const char* msg )
{
    if ( FAILED( hr ) )
    {
        // Lane R: runtime presentation, resize, and render-target creation
        // depend on the active adapter/driver/window state. Report the device
        // operation that failed instead of escaping through exception unwinding.
        return SkullbonezCore::Core::SbResult::Failure( "Rendering/DX12",
                                                        "%s (HRESULT 0x%08X)",
                                                        msg ? msg : "DX12 backend operation failed",
                                                        static_cast<unsigned int>( hr ) );
    }
    return SkullbonezCore::Core::SbResult::Success();
}

static bool IsDx12DeviceLostResult( HRESULT hr )
{
    return hr == DXGI_ERROR_DEVICE_REMOVED || hr == DXGI_ERROR_DEVICE_RESET || hr == DXGI_ERROR_DRIVER_INTERNAL_ERROR;
}

// --- Backend Setup Entry Point ---


RenderBackendDX12::RenderBackendDX12()
    : m_shaderDevelopment( m_pipelineOwner, m_textureOwner, m_geometryOwner ),
      m_frameOwner( m_renderDevice, m_pipelineOwner, m_textureOwner, m_descriptorHeaps ),
      m_graphTransientPool( m_renderDevice, m_descriptorHeaps, m_frameOwner, m_textureOwner, m_pipelineOwner )
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
    stats.recreationGeneration = m_renderDevice.RecreationGeneration();
    if ( !stats.available )
    {
        return stats;
    }

    const Dx12CpuDescriptorAllocatorStats rtvStats = m_descriptorHeaps.RtvStats();
    const Dx12CpuDescriptorAllocatorStats dsvStats = m_descriptorHeaps.DsvStats();
    const Dx12DescriptorAllocatorStats srvStats = m_descriptorHeaps.GetStats();
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
        const Dx12UploadArenaStats uploadStats = m_frameOwner.Uploads().GetStats( static_cast<UINT>( frameIndex ) );
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
    stats.uploadFlushCount = m_frameOwner.UploadFlushCount();
    stats.uploadDropCount = m_frameOwner.UploadDropCount();

    const Dx12ReadbackBufferStats timerReadbackStats = m_diagnostics.TimerReadbackStats();
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
    stats.graphTransientCount = m_graphTransientPool.Size();
    stats.graphTransientCapacity = m_graphTransientPool.Capacity();

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


RenderGraphTransientMaterializationStats
RenderBackendDX12::MaterializeGraphTransientResources( const RenderGraph& graph,
                                                       const RenderGraphCompileResult& compiled )
{
    return m_graphTransientPool.Materialize( graph, compiled );
}


RenderGraphTextureBinding RenderBackendDX12::ResolveGraphTextureBinding( RenderGraphResourceHandle resource ) const
{
    return m_graphTransientPool.Resolve( resource );
}


void RenderBackendDX12::BeginGraphTextureRenderTarget( const RenderGraphTextureBinding& binding, const char* passName )
{
    m_graphTransientPool.BeginRenderTarget( binding, passName );
}


void RenderBackendDX12::EndGraphTextureRenderTarget( const RenderGraphTextureBinding& binding, const char* passName )
{
    m_graphTransientPool.EndRenderTarget( binding, passName );
}

// --- Init / Shutdown ---


SkullbonezCore::Core::SbResult RenderBackendDX12::Init( HWND hwnd, HDC /*hdc*/, int width, int height )
{
    Dx12RenderDeviceInitDesc deviceDesc;
    deviceDesc.hwnd = hwnd;
    deviceDesc.width = static_cast<UINT>( width );
    deviceDesc.height = static_cast<UINT>( height );
    deviceDesc.frameCount = Dx12FrameOwner::FRAME_COUNT;
    deviceDesc.backBufferFormat = DXGI_FORMAT_R8G8B8A8_UNORM;
    const SkullbonezCore::Core::SbResult deviceResult = m_renderDevice.Init( deviceDesc );
    if ( !deviceResult.ok )
    {
        return deviceResult;
    }

    // Lane R: all shipping raster shaders use SM6.6 direct heap indexing. A
    // table-binding fallback would retain the per-draw descriptor copies this
    // renderer contract deliberately removes, so unsupported devices fail
    // startup with actionable capability diagnostics.
    D3D12_FEATURE_DATA_SHADER_MODEL shaderModel = { D3D_SHADER_MODEL_6_6 };
    const HRESULT shaderModelResult =
        Device()->CheckFeatureSupport( D3D12_FEATURE_SHADER_MODEL, &shaderModel, sizeof( shaderModel ) );
    D3D12_FEATURE_DATA_D3D12_OPTIONS bindingOptions = {};
    const HRESULT bindingTierResult =
        Device()->CheckFeatureSupport( D3D12_FEATURE_D3D12_OPTIONS, &bindingOptions, sizeof( bindingOptions ) );
    if ( FAILED( shaderModelResult ) || shaderModel.HighestShaderModel < D3D_SHADER_MODEL_6_6 ||
         FAILED( bindingTierResult ) || bindingOptions.ResourceBindingTier < D3D12_RESOURCE_BINDING_TIER_3 )
    {
        return SkullbonezCore::Core::SbResult::Failure(
            "Rendering/DX12",
            "SM6.6 bindless raster unsupported. shader_model_query=0x%08X highest_shader_model=0x%X "
            "binding_tier_query=0x%08X resource_binding_tier=%u required_tier=3",
            static_cast<unsigned int>( shaderModelResult ),
            static_cast<unsigned int>( shaderModel.HighestShaderModel ),
            static_cast<unsigned int>( bindingTierResult ),
            static_cast<unsigned int>( bindingOptions.ResourceBindingTier ) );
    }

    // A fresh device is the sole boundary allowed to clear a prior command
    // failure and submitted-work uncertainty. Dx12RenderDevice has already
    // closed the newly created list.
    m_frameOwner.ResetForDevice();
    m_diagnostics.ConfigureFaultInjection( m_frameOwner.DiagnosticsFrame() );

    // Invariant: the render device is the only owner and access path for its
    // factory, queue, allocators, swap chain, command list, and frame fence.
    // Backend initialization therefore cannot publish borrowed aliases before
    // all device objects exist, and rollback needs no separate rebind phase.
    // DXR is optional hardware support; fall back to raster water if the device
    // cannot expose raytracing interfaces.
    ShutdownDXR();
    m_raytracingOwner.ProbeCapability( Device() );

    // Descriptor heap Summary:
    //
    // The heap is a table. A descriptor is one row in that table. The actual
    // texture, depth buffer, or UAV texture is separate GPU memory.
    //
    // RTV rows are used when the GPU writes color pixels.
    // DSV rows are used when the GPU reads/writes depth and stencil.
    // SRV rows are used when shaders read textures or buffers.
    // UAV rows are used when compute/raytracing shaders write textures/buffers.
    //
    // The SRV/CBV/UAV heap has two views of the same static row identity:
    //
    // - staging heap: CPU-only, stable descriptor templates created at load time,
    // - shader-visible heap: GPU-readable rows indexed directly by SM6.6 raster
    //   shaders, plus fenced transient rows retained for compute/DXR tables.
    //
    // The descriptor allocator below owns row assignment for that pair. It keeps
    // long-lived static rows separate from short-lived per-frame rows so the CPU
    // does not overwrite a row while an in-flight command list still points at it.

    // Concept: the composition root starts one all-or-nothing descriptor epoch;
    // heap creation, capacities, row allocators, and published handles stay in
    // the concrete owner rather than being republished as backend fields.
    const SkullbonezCore::Core::SbResult descriptorResult =
        m_descriptorHeaps.Init( Device(), Dx12FrameOwner::FRAME_COUNT );
    if ( !descriptorResult.ok )
    {
        return descriptorResult;
    }
    m_descriptorHeaps.ResetFrame( m_frameOwner.AllocatorIndex() );

    // Cleared ordinary-raster texture slots still need a real descriptor table.
    // BindTexture(0) maps to this typed null SRV so shaders that sample an
    // intentionally empty slot read safe zero/default values instead of whatever
    // descriptor was previously bound to the root parameter.
    // Lifetime: this typed null row is process/device-epoch state. It is never
    // recreated by resize or scene churn and is discarded with the heaps.
    const UINT nullTextureSrvIndex = m_descriptorHeaps.AllocateStatic();
    m_textureOwner.SetNullSrvIndex( nullTextureSrvIndex );
    D3D12_SHADER_RESOURCE_VIEW_DESC nullTextureSrv = {};
    nullTextureSrv.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    nullTextureSrv.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
    nullTextureSrv.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    nullTextureSrv.Texture2D.MipLevels = 1;
    Device()->CreateShaderResourceView( nullptr,
                                        &nullTextureSrv,
                                        m_descriptorHeaps.StagingCpuHandle( nullTextureSrvIndex ) );
    m_descriptorHeaps.PublishStaticDescriptor( Device(), nullTextureSrvIndex );

    // Lifetime: swap-chain images are replaced on resize, but the engine keeps
    // one stable RTV descriptor row per back buffer index. ResizeBuffers swaps
    // the image memory; CreateRenderTargetView overwrites the existing row with
    // a view record for the new image.
    // Docs: https://learn.microsoft.com/en-us/windows/win32/api/d3d12/nf-d3d12-id3d12device-createrendertargetview
    for ( int i = 0; i < Dx12FrameOwner::FRAME_COUNT; ++i )
    {
        const SkullbonezCore::Core::SbResult backBufferResult = Dx12BackendInitResult(
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
        m_descriptorHeaps.PublishBackBufferRtv( Device(),
                                                static_cast<UINT>( i ),
                                                m_frameOwner.RenderTarget( static_cast<UINT>( i ) ) );
    }

    // Depth stencil
    ID3D12Resource* initialDepthStencil = nullptr;
    const SkullbonezCore::Core::SbResult depthStencilResult =
        m_renderDevice.CreateDepthStencilResource( width, height, initialDepthStencil );
    if ( !depthStencilResult.ok )
    {
        return depthStencilResult;
    }
    // Lifetime: the device owner adopts the candidate before the descriptor
    // owner publishes the matching DSV row for this presentation epoch.
    m_renderDevice.ReplaceDepthStencil( initialDepthStencil );
    m_descriptorHeaps.PublishMainDsv( Device(), m_renderDevice.DepthStencil() );

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
                                       Dx12FrameOwner::FRAME_COUNT,
                                       Dx12FrameOwner::UPLOAD_BUFFER_SIZE,
                                       L"Skullbonez DX12 Frame Upload Buffer" ) )
    {
        return SkullbonezCore::Core::SbResult::Failure( "Rendering/DX12",
                                                        "DX12 frame upload buffer creation or persistent Map failed" );
    }

    const SkullbonezCore::Core::SbResult rootSignatureResult = m_pipelineOwner.Initialize( Device() );
    if ( !rootSignatureResult.ok )
    {
        return rootSignatureResult;
    }
    Dx12TextureCommands textureCommands( m_renderDevice, m_frameOwner );
    const SkullbonezCore::Core::SbResult genMipsResult = m_textureOwner.Initialize( textureCommands );
    if ( !genMipsResult.ok )
    {
        return genMipsResult;
    }
    m_geometryOwner.AdoptGridLineShader( CreateShader( "shaders/grid_line" ) );
    if ( !m_geometryOwner.EnsureGridLinePipeline( Device(), m_pipelineOwner, DXGI_FORMAT_R8G8B8A8_UNORM ) ||
         !m_geometryOwner.EnsureGridLinePipeline( Device(), m_pipelineOwner, DXGI_FORMAT_R16G16B16A16_FLOAT ) )
    {
        return SkullbonezCore::Core::SbResult::Failure( "Rendering/DX12", "DX12 required grid-line warmup failed" );
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
            return SkullbonezCore::Core::SbResult::Failure(
                "Rendering/DX12",
                "DX12 required transient-triangle shader warmup failed (style=%u)",
                static_cast<unsigned int>( style ) );
        }
    }

    // GPU timestamp ownership is cold device-epoch diagnostics. The concrete
    // owner creates the query/readback pair and keeps covering-fence state local.
    const SkullbonezCore::Core::SbResult gpuTimerResult =
        m_diagnostics.InitializeGpuTimers( Device(), m_renderDevice.GraphicsQueue() );
    if ( !gpuTimerResult.ok )
    {
        return gpuTimerResult;
    }

    m_pipelineOwner.SetViewport( { 0.0f, 0.0f, (float)width, (float)height, 0.0f, 1.0f },
                                 { 0, 0, (LONG)width, (LONG)height } );
    m_pipelineOwner.SetCurrentTargets( m_descriptorHeaps.BackBufferRtv( m_frameOwner.FrameIndex() ),
                                       m_descriptorHeaps.MainDsv() );
    // Publication boundary: callers observe dimensions only after every
    // required device, upload, pipeline, and framebuffer resource is ready.
    m_renderDevice.PublishInitialExtent( width, height );

    return SkullbonezCore::Core::SbResult::Success();
}


SkullbonezCore::Core::SbResult Dx12PipelineOwner::Initialize( ID3D12Device* device )
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
        return SkullbonezCore::Core::SbResult::Failure( "Dx12PipelineOwner",
                                                        "%s reflection rejected: %s",
                                                        UnifiedRasterRootSignature::NAME,
                                                        reflectedContractError.c_str() );
    }
    // Root signature Summary:
    //
    // A shader cannot freely access arbitrary C++ variables or texture objects.
    // The root signature is the contract that says which small set of bindings
    // the command list may provide and which register names the HLSL shader will
    // use to find them.
    //
    // UnifiedRaster is deliberately simple and shared by every raster family:
    //
    // - DrawConstants binds the per-draw matrices, colors, and scalar values.
    // - TextureIndices supplies six static descriptor indices through b1 root
    //   constants; SM6.6 pixel shaders read ResourceDescriptorHeap directly.
    // - STATIC_SAMPLERS supplies the fixed filtering/addressing rules.
    //
    // The future render graph will not replace this shader contract. It will
    // decide when resources are safe to read/write and which pass binds them.
    D3D12_ROOT_PARAMETER1 params[UnifiedRasterRootSignature::ROOT_PARAMETER_COUNT] = {};
    params[UnifiedRasterRootSignature::ROOT_PARAMETER_DRAW_CONSTANTS].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
    params[UnifiedRasterRootSignature::ROOT_PARAMETER_DRAW_CONSTANTS].Descriptor.ShaderRegister =
        UnifiedRasterRootSignature::SHADER_REGISTER_DRAW_CONSTANTS;
    params[UnifiedRasterRootSignature::ROOT_PARAMETER_DRAW_CONSTANTS].Descriptor.RegisterSpace =
        UnifiedRasterRootSignature::REGISTER_SPACE;
    params[UnifiedRasterRootSignature::ROOT_PARAMETER_DRAW_CONSTANTS].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;

    params[UnifiedRasterRootSignature::ROOT_PARAMETER_TEXTURE_INDICES].ParameterType =
        D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
    params[UnifiedRasterRootSignature::ROOT_PARAMETER_TEXTURE_INDICES].Constants.ShaderRegister =
        UnifiedRasterRootSignature::SHADER_REGISTER_TEXTURE_INDICES;
    params[UnifiedRasterRootSignature::ROOT_PARAMETER_TEXTURE_INDICES].Constants.RegisterSpace =
        UnifiedRasterRootSignature::REGISTER_SPACE;
    params[UnifiedRasterRootSignature::ROOT_PARAMETER_TEXTURE_INDICES].Constants.Num32BitValues =
        UnifiedRasterRootSignature::TEXTURE_SLOT_COUNT;
    params[UnifiedRasterRootSignature::ROOT_PARAMETER_TEXTURE_INDICES].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;

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
    rootSigDesc.Desc_1_1.Flags = D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT |
                                 D3D12_ROOT_SIGNATURE_FLAG_CBV_SRV_UAV_HEAP_DIRECTLY_INDEXED;

    // Serialize the root signature description into a binary blob. The root signature defines
    // what data shaders can access: [0] CBV at b0, [1] six b1 descriptor-index
    // constants, the directly indexed resource heap, and fixed samplers.
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
        return SkullbonezCore::Core::SbResult::Failure( "Rendering/DX12", "%s", msg.c_str() );
    }
    if ( signature->GetBufferSize() > m_rootSignatureSerialized.size() )
    {
        return SkullbonezCore::Core::SbResult::Failure( "Rendering/DX12",
                                                        "UnifiedRaster serialized root signature exceeds reload cap" );
    }
    // Lifetime: retain canonical serialized bytes in fixed storage so manual
    // shader reload can reopen the manifest-keyed PSO blob store without
    // retaining the temporary serialization COM object.
    m_rootSignatureSerializedSize = signature->GetBufferSize();
    std::memcpy( m_rootSignatureSerialized.data(), signature->GetBufferPointer(), m_rootSignatureSerializedSize );

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
        return SkullbonezCore::Core::SbResult::Failure( "Rendering/DX12", "CreateRootSignature failed" );
    }
    NameDx12Object( m_rootSignature, L"Skullbonez DX12 UnifiedRaster Root Signature" );
    // Lane F: exhausting a 64-bit sequence requires more successful root-
    // signature creations than this owner can perform in any valid lifetime.
    // Publishing zero or reusing an old identity could alias incompatible PSOs.
    if ( m_nextRootSignatureIdentity == 0 )
    {
        SB_FATAL( "Dx12PipelineOwner", "Root-signature identity sequence exhausted." );
    }
    m_rootSignatureIdentity = m_nextRootSignatureIdentity++;
    // Lane R: a persistent PSO cache is an optional cold-start accelerator.
    // Its owner logs and discards missing/corrupt/driver-incompatible bytes;
    // failure must never reject an otherwise valid renderer device.
    // Why: ID3DBlob publishes serialized bytes through its COM void-pointer
    // ABI. The cache owner receives an immutable typed view only.
    m_persistentPsoCache.Initialize(
        { static_cast<const std::uint8_t*>( signature->GetBufferPointer() ), signature->GetBufferSize() } );
#ifdef _DEBUG
    SkullbonezCore::Core::Log().WriteEventf(
        "dx12_raster_binding_contract name=%s root_parameters=%u cbv=b%u texture_indices=b%u "
        "resource_heap=direct material_payload=packed_instance_params samplers=s%u,s%u,s%u bind_texture_slots=%d",
        UnifiedRasterRootSignature::NAME,
        UnifiedRasterRootSignature::ROOT_PARAMETER_COUNT,
        UnifiedRasterRootSignature::SHADER_REGISTER_DRAW_CONSTANTS,
        UnifiedRasterRootSignature::SHADER_REGISTER_TEXTURE_INDICES,
        UnifiedRasterRootSignature::STATIC_SAMPLERS[0].shaderRegister,
        UnifiedRasterRootSignature::STATIC_SAMPLERS[1].shaderRegister,
        UnifiedRasterRootSignature::STATIC_SAMPLERS[2].shaderRegister,
        UnifiedRasterRootSignature::TEXTURE_SLOT_COUNT );
#endif
    return SkullbonezCore::Core::SbResult::Success();
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
        const SkullbonezCore::Core::SbResult openResult = m_frameOwner.EnsureOpen();
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
        if ( !m_frameOwner.TransitionBackbuffer( "ShutdownBackbufferPresent", RenderGraphResourceAccess::Present ) )
        {
            const SkullbonezCore::Core::SbResult transitionResult = m_frameOwner.CurrentResult();
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
        m_frameOwner.AssertProfilerClosed( "Shutdown" );
        const SkullbonezCore::Core::SbResult closeResult =
            m_frameOwner.CommitClose( CommandList()->Close(), "Shutdown command list Close" );
        if ( !closeResult.ok )
        {
            SB_FATAL( "RenderBackendDX12",
                      "Shutdown command-list Close failed; resources remain process-owned. owner=%s reason=%s",
                      closeResult.error.owner,
                      closeResult.error.message );
        }
        const SkullbonezCore::Core::SbResult submitResult = m_frameOwner.SubmitClosed();
        if ( !submitResult.ok )
        {
            SB_FATAL( "RenderBackendDX12",
                      "Shutdown command-list submission failed. owner=%s reason=%s",
                      submitResult.error.owner,
                      submitResult.error.message );
        }
    }

    // Wait for all GPU work to complete (command queue + pending presents).
    const SkullbonezCore::Core::SbResult initialDrainResult =
        m_frameOwner.HasFailure() ? DrainForResourceRelease() : m_frameOwner.WaitForGpu();
    if ( !initialDrainResult.ok )
    {
        // Lane F: releasing any backend object after this point could race a
        // submitted command stream. Terminal shutdown must stop instead.
        SB_FATAL( "RenderBackendDX12",
                  "Shutdown could not prove initial GPU queue completion. owner=%s reason=%s",
                  initialDrainResult.error.owner,
                  initialDrainResult.error.message );
    }
    m_frameOwner.ReleaseCompletedRetirements( true );

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
            m_renderDevice.ReportDeviceLost( "Shutdown Present drain", drainPresentResult );
        }
        const SkullbonezCore::Core::SbResult checkedPresent =
            Dx12BackendOperationResult( drainPresentResult, "Shutdown swap-chain Present drain failed" );
        if ( !checkedPresent.ok )
        {
            SB_FATAL( "RenderBackendDX12",
                      "Shutdown could not drain the swap-chain present queue. owner=%s reason=%s",
                      checkedPresent.error.owner,
                      checkedPresent.error.message );
        }

        const SkullbonezCore::Core::SbResult presentDrainResult = m_frameOwner.WaitForGpu();
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

    // Lifetime: both terminal drains above succeeded, so detached screenshot
    // readbacks can no longer be referenced by the GPU or DXGI present queue.
    m_backbufferCapture.ReleaseAfterTerminalDrain();

    // DXR resources hang off newer D3D12 interfaces and contain GPU-side
    // acceleration structures. Release them before the shared renderer objects
    // below so no raytracing object outlives the device/command-list aliases it
    // was created from.
    ShutdownDXR();

    m_diagnostics.ReportArchitectureStats( "Shutdown", m_descriptorHeaps, m_frameOwner );
    m_graphTransientPool.ReleaseAfterTerminalDrain( "Shutdown" );

    m_diagnostics.ShutdownGpuTimers();

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
    m_shaderDevelopment.ResetAfterShutdown();
    m_textureOwner.Shutdown();
    m_pipelineOwner.Shutdown();
    m_frameOwner.Uploads().Shutdown();
    for ( int i = 0; i < Dx12FrameOwner::FRAME_COUNT; ++i )
    {
        if ( m_frameOwner.RenderTarget( static_cast<UINT>( i ) ) )
        {
            m_frameOwner.RenderTarget( static_cast<UINT>( i ) )->Release();
            m_frameOwner.RenderTarget( static_cast<UINT>( i ) ) = nullptr;
        }
    }
    m_descriptorHeaps.Shutdown();
    m_renderDevice.Shutdown();
    m_frameOwner.ResetAfterShutdown();
}


// --- Frame Management ---


SkullbonezCore::Core::SbResult RenderBackendDX12::Present()
{
    SkullbonezCore::Core::SbResult stateResult = m_frameOwner.EnsureOpen();
    if ( !stateResult.ok )
    {
        return stateResult;
    }

    // Opportunistically consume the previous frame's resolved timer buffer before writing
    // new query results into the same readback resource.
    m_diagnostics.ConsumeGpuTimerReadback( m_frameOwner.DiagnosticsFrame(), false );

    // Resolve GPU timer queries — only resolve contiguous ranges of slots that actually
    // had EndQuery recorded this frame. Resolving unwritten slots triggers D3D12 error 1319.
    const bool resolvedTimerSlotsThisFrame = m_diagnostics.ResolveWrittenGpuTimers( m_frameOwner.DiagnosticsFrame() );

    m_frameOwner.TransitionBackbuffer( "PresentBackbuffer", RenderGraphResourceAccess::Present );
    if ( m_frameOwner.HasFailure() )
    {
        return m_frameOwner.CurrentResult();
    }

    // Close the command list — finalizes the recorded commands. A closed command list can be
    // submitted to the GPU. No more commands can be recorded until Reset is called.
    // Docs: https://learn.microsoft.com/en-us/windows/win32/api/d3d12/nf-d3d12-id3d12graphicscommandlist-close
    m_frameOwner.AssertProfilerClosed( "Present" );
    stateResult = m_frameOwner.CommitClose( CommandList()->Close(), "Present command list Close" );
    if ( !stateResult.ok )
    {
        return stateResult;
    }

    // Submit the completed command list to the GPU for execution. The GPU processes commands
    // asynchronously — this call returns immediately while the GPU works in the background.
    // Docs: https://learn.microsoft.com/en-us/windows/win32/api/d3d12/nf-d3d12-id3d12commandqueue-executecommandlists
    stateResult = m_frameOwner.SubmitClosed();
    if ( !stateResult.ok )
    {
        return stateResult;
    }

    // Present the frame — flips the swap chain to show the just-rendered back buffer on screen.
    // Sync interval is configurable so perf scenes can disable V-Sync while visual scenes keep it.
    // Docs: https://learn.microsoft.com/en-us/windows/win32/api/dxgi/nf-dxgi-idxgiswapchain-present
    const UINT syncInterval = m_renderDevice.VsyncEnabled() ? 1u : 0u;
    const UINT presentFlags =
        ( !m_renderDevice.VsyncEnabled() && m_renderDevice.AllowTearing() ) ? DXGI_PRESENT_ALLOW_TEARING : 0u;
    const HRESULT presentResult = SwapChain()->Present( syncInterval, presentFlags );
    if ( IsDx12DeviceLostResult( presentResult ) )
    {
        m_renderDevice.ReportDeviceLost( "Present", presentResult );
        return m_frameOwner.RetainDeviceLoss( "Present", presentResult );
    }
    const SkullbonezCore::Core::SbResult presentFailure =
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
    const SkullbonezCore::Core::SbResult signalResult = m_frameOwner.SignalFrame( presentFenceValue );
    if ( !signalResult.ok )
    {
        return signalResult;
    }
    m_frameOwner.SetFrameFenceValue( m_frameOwner.AllocatorIndex(), presentFenceValue );
    m_frameOwner.AssignRetirementFence( presentFenceValue );

    // Timer readback can be mapped once this frame's signal fence is reached.
    // If there's an unconsumed readback still pending (e.g. fence wasn't ready during the
    // non-blocking TryConsume at the top of Present), do a blocking consume now to avoid
    // permanently losing that frame's GPU timing data by overwriting readFenceValue.
    m_diagnostics.PublishResolvedGpuTimerFence( resolvedTimerSlotsThisFrame, presentFenceValue );

    // Advance to next frame's allocator and swap chain buffer.
    m_frameOwner.AdvanceFrameIndices();
    m_pipelineOwner.SetCurrentColorTarget( m_descriptorHeaps.BackBufferRtv( m_frameOwner.FrameIndex() ) );

    // Charge allocator/upload/descriptor pacing to Present/VsyncWait instead of
    // letting the first render command of the next frame hit this wait mid-frame.
    const UINT64 nextFrameFenceValue = m_frameOwner.FrameFenceValue( m_frameOwner.AllocatorIndex() );
    if ( nextFrameFenceValue > m_renderDevice.FrameFence().CompletedValue() )
    {
        const SkullbonezCore::Core::SbResult waitResult = m_frameOwner.WaitForFrameFence( nextFrameFenceValue );
        if ( !waitResult.ok )
        {
            return waitResult;
        }
    }
    m_frameOwner.ReleaseCompletedRetirements( false );
    return SkullbonezCore::Core::SbResult::Success();
}


void RenderBackendDX12::SetVsyncEnabled( bool enabled )
{
    m_renderDevice.SetVsyncEnabled( enabled );
}


bool RenderBackendDX12::IsVsyncEnabled() const
{
    return m_renderDevice.VsyncEnabled();
}


SkullbonezCore::Core::SbResult RenderBackendDX12::Finish()
{
    if ( m_frameOwner.HasFailure() )
    {
        return m_frameOwner.CurrentResult();
    }

    if ( !CommandList() || !m_renderDevice.GraphicsQueue() || !m_renderDevice.FrameFence().IsReady() ||
         !m_renderDevice.CommandAllocator( m_frameOwner.AllocatorIndex() ) )
    {
        const SkullbonezCore::Core::SbResult waitResult = m_frameOwner.CommitWait( m_frameOwner.WaitForGpu() );
        if ( !waitResult.ok )
        {
            return waitResult;
        }
        m_diagnostics.ConsumeGpuTimerReadback( m_frameOwner.DiagnosticsFrame(), true );
        return SkullbonezCore::Core::SbResult::Success();
    }

    if ( m_frameOwner.IsOpen() )
    {
        m_frameOwner.AssertProfilerClosed( "Finish" );
        const SkullbonezCore::Core::SbResult closeResult =
            m_frameOwner.CommitClose( CommandList()->Close(), "Finish command list Close" );
        if ( !closeResult.ok )
        {
            return closeResult;
        }
        const SkullbonezCore::Core::SbResult submitResult = m_frameOwner.SubmitClosed();
        if ( !submitResult.ok )
        {
            return submitResult;
        }
    }
    const SkullbonezCore::Core::SbResult waitResult = m_frameOwner.CommitWait( m_frameOwner.WaitForGpu() );
    if ( !waitResult.ok )
    {
        return waitResult;
    }
    m_diagnostics.ConsumeGpuTimerReadback( m_frameOwner.DiagnosticsFrame(), true );

    // Hazard: runtime pipeline-sync calls Finish() between physics and render.
    // That wait is allowed to drain submitted GPU work, but the next render pass
    // still expects a recording command list for explicit barriers and draws.
    return m_frameOwner.EnsureOpen();
}


SkullbonezCore::Core::SbResult RenderBackendDX12::FlushGPU()
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
        return m_frameOwner.RetainFailure( SkullbonezCore::Core::SbResult::Failure(
            "Rendering/DX12",
            "FlushGPU requires a complete command queue, fence, allocator, and command list." ) );
    }

    Dx12GpuDrainProgress drainProgress( m_frameOwner.IsOpen() );
    if ( drainProgress.RequiresClose() )
    {
        m_frameOwner.AssertProfilerClosed( "FlushGPU" );
        const SkullbonezCore::Core::SbResult closeResult =
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

        const SkullbonezCore::Core::SbResult submitResult = m_frameOwner.SubmitClosed();
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
    const SkullbonezCore::Core::SbResult waitResult = m_frameOwner.CommitWait( m_frameOwner.WaitForGpu() );
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
    const SkullbonezCore::Core::SbResult reopenResult = m_frameOwner.EnsureOpen();
    if ( !reopenResult.ok )
    {
        return reopenResult;
    }
    if ( !drainProgress.CommitReopen() || !drainProgress.IsMutationSafe() )
    {
        SB_FATAL( "RenderBackendDX12", "FlushGPU drain order rejected a successful command-list reopen." );
    }
    return SkullbonezCore::Core::SbResult::Success();
}


SkullbonezCore::Core::SbResult RenderBackendDX12::DrainForResourceRelease()
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
        return SkullbonezCore::Core::SbResult::Success();
    }

    // Lifetime: a failed, never-submitted recording epoch cannot reference any
    // resource from the GPU. Release is already safe and teardown must not turn
    // that expected Lane R path into a second submission or a destructor fatal.
    if ( !m_frameOwner.HasSubmittedWork() )
    {
        return SkullbonezCore::Core::SbResult::Success();
    }

    // Hazard: if earlier work reached the queue before a later recording error,
    // terminal teardown may signal/wait for that submitted work. It must not
    // submit, reopen, or clear the sticky command-path failure.
    return m_frameOwner.WaitForGpu();
}


SkullbonezCore::Core::SbResult RenderBackendDX12::Resize( int width, int height )
{
    if ( width <= 0 || height <= 0 )
    {
        return SkullbonezCore::Core::SbResult::Success();
    }

    // Lane R: Resize replaces back buffers and depth memory. FlushGPU closes and
    // submits any open list, proves all queue work complete, and reopens an empty
    // recording epoch. Do not release the first old resource on any failure.
    const SkullbonezCore::Core::SbResult drainResult = FlushGPU();
    if ( !drainResult.ok )
    {
        return drainResult;
    }

    Dx12RecreationTransaction transaction;
    transaction.Begin( m_renderDevice.RecreationGeneration() );

    // Prepare the independent depth candidate before releasing a single
    // published resource. A creation failure therefore leaves the current
    // framebuffer and dimensions untouched.
    ID3D12Resource* candidateDepth = nullptr;
    const SkullbonezCore::Core::SbResult candidateDepthResult =
        m_renderDevice.CreateDepthStencilResource( width, height, candidateDepth );
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
    for ( int i = 0; i < Dx12FrameOwner::FRAME_COUNT; ++i )
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

    const UINT resizeFlags = m_renderDevice.AllowTearing() ? DXGI_SWAP_CHAIN_FLAG_ALLOW_TEARING : 0u;
    const HRESULT resizeResult = SwapChain()->ResizeBuffers( Dx12FrameOwner::FRAME_COUNT,
                                                             static_cast<UINT>( width ),
                                                             static_cast<UINT>( height ),
                                                             DXGI_FORMAT_R8G8B8A8_UNORM,
                                                             resizeFlags );
    if ( IsDx12DeviceLostResult( resizeResult ) )
    {
        candidateDepth->Release();
        m_renderDevice.ReportDeviceLost( "ResizeBuffers", resizeResult );
        return transaction.Fail( m_frameOwner.RetainDeviceLoss( "ResizeBuffers", resizeResult ) );
    }
    const SkullbonezCore::Core::SbResult resizeFailure =
        Dx12BackendOperationResult( resizeResult, "SwapChain ResizeBuffers failed" );
    if ( !resizeFailure.ok )
    {
        candidateDepth->Release();
        // A failed ResizeBuffers leaves the old swap-chain buffers owned by
        // DXGI. Reacquire all of them before returning so the published backend
        // remains usable at its previous dimensions.
        ID3D12Resource* restored[Dx12FrameOwner::FRAME_COUNT] = {};
        bool restoredAll = true;
        for ( int i = 0; i < Dx12FrameOwner::FRAME_COUNT; ++i )
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
            for ( int i = 0; i < Dx12FrameOwner::FRAME_COUNT; ++i )
            {
                m_frameOwner.RenderTarget( static_cast<UINT>( i ) ) = restored[i];
                Device()->CreateRenderTargetView( m_frameOwner.RenderTarget( static_cast<UINT>( i ) ),
                                                  nullptr,
                                                  m_descriptorHeaps.BackBufferRtv( static_cast<UINT>( i ) ) );
            }
            m_pipelineOwner.SetCurrentColorTarget( m_descriptorHeaps.BackBufferRtv( m_frameOwner.FrameIndex() ) );
            return transaction.Fail( resizeFailure );
        }
        for ( ID3D12Resource* resource : restored )
        {
            if ( resource )
            {
                resource->Release();
            }
        }
        return m_frameOwner.RetainFailure( transaction.Fail( SkullbonezCore::Core::SbResult::Failure(
            "Rendering/DX12",
            "ResizeBuffers failed and the previous back buffers could not be restored" ) ) );
    }
    if ( !transaction.CommitSwapChainResized() )
    {
        candidateDepth->Release();
        SB_FATAL( "RenderBackendDX12", "Resize transaction rejected successful ResizeBuffers." );
    }
    ID3D12Resource* candidateBackBuffers[Dx12FrameOwner::FRAME_COUNT] = {};
    for ( int i = 0; i < Dx12FrameOwner::FRAME_COUNT; ++i )
    {
        const SkullbonezCore::Core::SbResult backBufferResult =
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
    for ( int i = 0; i < Dx12FrameOwner::FRAME_COUNT; ++i )
    {
        m_frameOwner.RenderTarget( static_cast<UINT>( i ) ) = candidateBackBuffers[i];
        NameDx12ObjectIndexed( m_frameOwner.RenderTarget( static_cast<UINT>( i ) ),
                               L"Skullbonez DX12 Swapchain Backbuffer",
                               static_cast<UINT>( i ) );
        m_descriptorHeaps.RepublishBackBufferRtv( Device(),
                                                  static_cast<UINT>( i ),
                                                  m_frameOwner.RenderTarget( static_cast<UINT>( i ) ) );
    }
    ID3D12Resource* oldDepth = m_renderDevice.ReplaceDepthStencil( candidateDepth );
    m_descriptorHeaps.PublishMainDsv( Device(), m_renderDevice.DepthStencil() );
    if ( oldDepth )
    {
        oldDepth->Release();
    }

    m_pipelineOwner.SetViewport( { 0.0f, 0.0f, (float)width, (float)height, 0.0f, 1.0f },
                                 { 0, 0, (LONG)width, (LONG)height } );
    m_pipelineOwner.SetCurrentTargets( m_descriptorHeaps.BackBufferRtv( m_frameOwner.FrameIndex() ),
                                       m_descriptorHeaps.MainDsv() );
    const uint64_t recreationGeneration = m_renderDevice.PublishResizedExtent( width, height );
    if ( !transaction.CommitPublished( recreationGeneration ) ||
         transaction.PublishedGeneration() != recreationGeneration )
    {
        SB_FATAL( "RenderBackendDX12", "Resize transaction failed its publication proof." );
    }
    return SkullbonezCore::Core::SbResult::Success();
}


// --- Viewport & Clear ---


void RenderBackendDX12::SetViewport( int x, int y, int w, int h )
{
    m_pipelineOwner.SetViewport( { (float)x, (float)y, (float)w, (float)h, 0.0f, 1.0f },
                                 { (LONG)x, (LONG)y, (LONG)( x + w ), (LONG)( y + h ) } );
}


void RenderBackendDX12::Clear( bool color, bool depth )
{
    if ( !m_frameOwner.EnsureOpen().ok )
    {
        return;
    }

    if ( !m_pipelineOwner.RenderingToFramebuffer() )
    {
        m_frameOwner.TransitionBackbuffer( "ClearBackbuffer", RenderGraphResourceAccess::RenderTarget );
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
        m_pipelineOwner.ClearCurrentColor( CommandList() );
    }
    if ( depth )
    {
        // Clear the depth buffer to 1.0 (maximum distance), so all subsequent draws will pass
        // the depth test. This is done at the start of each frame or when switching render targets.
        // Docs:
        // https://learn.microsoft.com/en-us/windows/win32/api/d3d12/nf-d3d12-id3d12graphicscommandlist-cleardepthstencilview
        m_pipelineOwner.ClearCurrentDepth( CommandList() );
    }
}


void RenderBackendDX12::SetClearColor( float r, float g, float b, float a )
{
    m_pipelineOwner.SetClearColor( r, g, b, a );
}


void RenderBackendDX12::SetClearDepth( float depth )
{
    m_pipelineOwner.SetClearDepth( depth );
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
    return m_renderDevice.Width();
}


int RenderBackendDX12::GetHeight() const
{
    return m_renderDevice.Height();
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
