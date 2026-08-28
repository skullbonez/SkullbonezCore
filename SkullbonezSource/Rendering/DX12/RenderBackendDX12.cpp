/*
File: SkullbonezSource/Rendering/DX12/RenderBackendDX12.cpp
Purpose:
  Implements production DX12 device orchestration and backend-facing resource work.

Summary:
  Composes the concrete DX12 owners, controls device and
  swap-chain lifecycle, and delegates backend-facing resource
  operations. Frame epoch, deferred retirement, descriptor heaps,
  capture, and graph transient state live in dedicated concrete owners.

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
  - Agentic/Reference/engine-glossary.md
*/

// DX12 Architecture:
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
#include "../../Core/TracyClientOwner.h"
#include "../../Core/FatalError.h"
#include <cstdio>
#include <algorithm>
#include <string>
#include <wrl/client.h>


using namespace SkullbonezCore::Math::Transformation;
using namespace SkullbonezCore::Rendering;
using Microsoft::WRL::ComPtr;


static void ReportDX12DescriptorHeapExhausted( const char* heapName, UINT nextIndex, UINT capacity )
{
    const char* name = heapName ? heapName : "unknown";
    fprintf( stderr, "FATAL: DX12 %s heap exhausted (next=%u capacity=%u)\n", name, nextIndex, capacity );
    fprintf( stdout, "FATAL: DX12 %s heap exhausted (next=%u capacity=%u)\n", name, nextIndex, capacity );
    fflush( stderr );
    fflush( stdout );
    SkullbonezCore::Core::Log().WriteEventf( "dx12_descriptor_heap_exhausted heap=%s next=%u capacity=%u", name, nextIndex,
                                             capacity );

    SkullbonezCore::Core::Log().FlushAll();
}

static inline SkullbonezCore::Core::SbResult
Dx12BackendInitResult( SkullbonezCore::Core::SbDiagnosticStore& resultDiagnostics, HRESULT hr, const char* msg )
{
    if ( FAILED( hr ) )
    {
        // Recoverable error: renderer startup depends on the adapter, driver, window, and
        // available descriptor resources. Return a bounded owner/message so the
        // process bootstrap can report the environment failure cleanly.
        return resultDiagnostics.Failure( "Rendering/DX12", "%s (HRESULT 0x%08X)",
                                          msg ? msg : "DX12 backend startup call failed", static_cast<unsigned int>( hr ) );
    }

    return SkullbonezCore::Core::SbResult::Success();
}

static inline SkullbonezCore::Core::SbResult
Dx12BackendOperationResult( SkullbonezCore::Core::SbDiagnosticStore& resultDiagnostics, HRESULT hr, const char* msg )
{
    if ( FAILED( hr ) )
    {
        // Recoverable error: runtime presentation, resize, and render-target creation
        // depend on the active adapter/driver/window state. Report the device
        // operation that failed instead of escaping through exception unwinding.
        return resultDiagnostics.Failure( "Rendering/DX12", "%s (HRESULT 0x%08X)",
                                          msg ? msg : "DX12 backend operation failed", static_cast<unsigned int>( hr ) );
    }

    return SkullbonezCore::Core::SbResult::Success();
}

static bool IsDx12DeviceLostResult( HRESULT hr )
{
    return hr == DXGI_ERROR_DEVICE_REMOVED || hr == DXGI_ERROR_DEVICE_RESET || hr == DXGI_ERROR_DRIVER_INTERNAL_ERROR;
}

// Backend Setup Entry Point:


RenderBackendDX12::RenderBackendDX12( SkullbonezCore::Core::SbDiagnosticStore& resultDiagnostics )
    : m_resultDiagnostics( resultDiagnostics ), m_textureOwner( resultDiagnostics ), m_pipelineOwner( resultDiagnostics ),
      m_diagnostics( resultDiagnostics ), m_renderDevice( resultDiagnostics ), m_descriptorHeaps( resultDiagnostics ),
      m_frameOwner( resultDiagnostics, m_renderDevice, m_pipelineOwner, m_textureOwner, m_descriptorHeaps ),
      m_shaderDevelopment( resultDiagnostics, m_pipelineOwner, m_textureOwner, m_geometryOwner, m_renderDevice, m_frameOwner,
                           m_diagnostics ),
      m_resourceBuilder( m_renderDevice, m_pipelineOwner, m_textureOwner, m_descriptorHeaps, m_frameOwner,
                         m_shaderDevelopment, m_diagnostics ),
      m_raytracingOwner( resultDiagnostics, m_renderDevice, m_descriptorHeaps, m_frameOwner, m_textureOwner, m_pipelineOwner,
                         m_geometryOwner ),
#if defined( SKULLBONEZ_DEVELOPMENT_TOOLS )
      m_imguiRenderer( resultDiagnostics, m_renderDevice, m_descriptorHeaps, m_frameOwner, m_pipelineOwner, m_textureOwner ),
#endif
      m_backbufferCapture( resultDiagnostics, m_frameOwner.CaptureFrame(), m_renderDevice ),
      m_graphTransientPool( m_renderDevice, m_descriptorHeaps, m_frameOwner, m_textureOwner, m_pipelineOwner )
{
    m_textureOwner.BindResourceOwners( m_renderDevice, m_frameOwner, m_pipelineOwner );
    m_geometryOwner.BindResourceOwners( m_renderDevice, m_frameOwner, m_pipelineOwner, m_diagnostics );
    m_diagnostics.BindSources( m_renderDevice, m_descriptorHeaps, m_frameOwner, m_textureOwner, m_pipelineOwner,
                               m_geometryOwner, m_graphTransientPool, m_raytracingOwner );
}


RenderGraphTransientMaterializationStats
Dx12GraphTransientPool::MaterializeGraphTransientResources( const RenderGraph& graph,
                                                            const RenderGraphCompileResult& compiled )
{
    return Materialize( graph, compiled );
}


RenderGraphTextureBinding Dx12GraphTransientPool::ResolveGraphTextureBinding( RenderGraphResourceHandle resource ) const
{
    return Resolve( resource );
}


RenderGraphNativeResourceToken Dx12GraphTransientPool::ResolveGraphResourceToken( uint32_t textureHandle ) const
{
    return RenderGraphNativeResourceToken::From( m_textures.ResolveResource( textureHandle ) );
}


RenderGraphBackbufferBinding Dx12GraphTransientPool::ResolveGraphBackbufferBinding() const
{
    RenderGraphBackbufferBinding binding;
    binding.nativeResource = RenderGraphNativeResourceToken::From( m_frame.RenderTarget( m_frame.FrameIndex() ) );
    binding.currentAccess = m_frame.BackBufferAccess();
    return binding;
}


size_t Dx12GraphTransientPool::ExecuteGraphTransitions( const RenderGraph& graph, const RenderGraphCompileResult& compiled,
                                                        uint32_t passIndex )
{
    size_t emittedCount = ExecuteTransitions( graph, compiled, passIndex );

    if ( passIndex >= graph.Passes().size() )
    {
        SB_FATAL( "RenderBackendDX12", "Graph transition requested an invalid pass. pass=%u", passIndex );
    }

    emittedCount += DispatchCompiledUavBarriersForPass(
        graph, compiled, passIndex, true,
        [&]( const RenderGraphUavBarrierDesc& barrier, const RenderGraphResourceDesc& resource )
        {
            ID3D12Resource* nativeResource = barrier.nativeResource.As<ID3D12Resource>();

            if ( !nativeResource )
            {
                SB_FATAL( "RenderBackendDX12", "Compiled external UAV barrier has no native resource. pass=%s resource=%s",
                          graph.Passes()[passIndex].name, resource.name );
            }

            if ( !m_frame.CanRecord() && !m_frame.EnsureOpen().Ok() )
            {
                SB_FATAL( "RenderBackendDX12",
                          "Compiled external UAV barrier could not open command recording. pass=%s resource=%s",
                          graph.Passes()[passIndex].name, resource.name );
            }

            Dx12RenderGraphUavBarrierDesc desc;
            desc.commandList = m_frame.CommandList();
            desc.resource = nativeResource;
            const Dx12RenderGraphUavBarrierRecord record = ExecuteDx12RenderGraphUavBarrier( "Dx12GraphCompiledExternal",
                                                                                             graph.Passes()[passIndex].name,
                                                                                             resource.name, desc );

            if ( !record.hasNativeResource || record.missingCommandList || !record.emitted )
            {
                SB_FATAL( "RenderBackendDX12", "Compiled external UAV ordering barrier was not emitted. pass=%s resource=%s",
                          graph.Passes()[passIndex].name, resource.name );
            }

            return true;
        } );

    for ( const RenderGraphTransitionDesc& transition : compiled.transitions )
    {
        if ( transition.passIndex != passIndex )
        {
            continue;
        }

        const RenderGraphResourceDesc& resource = graph.Resources()[transition.resource.index];

        if ( !resource.external )
        {
            continue;
        }

        ID3D12Resource* nativeResource = transition.nativeResource.As<ID3D12Resource>();

        if ( !nativeResource )
        {
            SB_FATAL( "RenderBackendDX12", "Compiled external transition has no native resource. pass=%s resource=%s",
                      graph.Passes()[passIndex].name, resource.name );
        }

        if ( !m_frame.CanRecord() && !m_frame.EnsureOpen().Ok() )
        {
            SB_FATAL( "RenderBackendDX12",
                      "Compiled external transition could not open command recording. pass=%s resource=%s",
                      graph.Passes()[passIndex].name, resource.name );
        }

        const bool isCurrentBackbuffer = nativeResource == m_frame.RenderTarget( m_frame.FrameIndex() );

        if ( isCurrentBackbuffer && transition.before != m_frame.BackBufferAccess() )
        {
            // Hazard: graph compilation uses the tracked access sampled by the
            // wrapper. A mismatch means an untracked frame-edge transition ran
            // between declaration and callback execution.
            SB_FATAL( "RenderBackendDX12",
                      "Compiled backbuffer transition has stale before-state. pass=%s tracked=%s compiled=%s",
                      graph.Passes()[passIndex].name, ToString( m_frame.BackBufferAccess() ),
                      ToString( transition.before ) );
        }

        // Hazard: leaving UAV writes unordered before the compiled consumer
        // transition can expose partially written reflection pixels to water.
        if ( transition.before == RenderGraphResourceAccess::UnorderedAccess )
        {
            Dx12RenderGraphUavBarrierDesc uavDesc;
            uavDesc.commandList = m_frame.CommandList();
            uavDesc.resource = nativeResource;
            const Dx12RenderGraphUavBarrierRecord
                uavRecord = ExecuteDx12RenderGraphUavBarrier( "Dx12GraphCompiledExternal", graph.Passes()[passIndex].name,
                                                              resource.name, uavDesc );

            if ( !uavRecord.emitted )
            {
                SB_FATAL( "RenderBackendDX12", "Compiled graph UAV ordering barrier was not emitted. pass=%s resource=%s",
                          graph.Passes()[passIndex].name, resource.name );
            }
        }

        Dx12RenderGraphSingleTransitionDesc transitionDesc;
        transitionDesc.commandList = m_frame.CommandList();
        transitionDesc.resource = nativeResource;
        transitionDesc.before = transition.before;
        transitionDesc.after = transition.after;
        transitionDesc.subresource = static_cast<UINT>( transition.subresource );
        const Dx12RenderGraphBarrierRecord record = ExecuteDx12RenderGraphSingleTransition( "Dx12GraphCompiledExternal",
                                                                                            graph.Passes()[passIndex].name,
                                                                                            resource.name, transitionDesc );

        if ( !record.emitted )
        {
            SB_FATAL( "RenderBackendDX12", "Compiled graph external transition was not emitted. pass=%s resource=%s",
                      graph.Passes()[passIndex].name, resource.name );
        }

        if ( isCurrentBackbuffer )
        {
            m_frame.SetBackBufferAccess( transition.after );
            m_pipeline.InvalidateTargets();
        }

        ++emittedCount;
    }

    return emittedCount;
}


void Dx12GraphTransientPool::BeginGraphTextureRenderTarget( const RenderGraphTextureBinding& binding, const char* passName )
{
    BeginRenderTarget( binding, passName );
}


void Dx12GraphTransientPool::EndGraphTextureRenderTarget( const RenderGraphTextureBinding& binding, const char* passName )
{
    EndRenderTarget( binding, passName );
}

// Init / Shutdown:


SkullbonezCore::Core::SbResult RenderBackendDX12::Init( HWND hwnd, HDC /*hdc*/, int width, int height, UINT frameCount,
                                                        const char* retainedGeometryShaderBaseName )
{
    if ( frameCount < 2u || frameCount > static_cast<UINT>( Dx12FrameOwner::MAX_FRAME_COUNT ) )
    {
        return m_resultDiagnostics.Failure( "Rendering/DX12", "Swap-chain frame count must be 2 or 3. requested=%u",
                                            frameCount );
    }

    Dx12RenderDeviceInitDesc deviceDesc;
    deviceDesc.hwnd = hwnd;
    deviceDesc.width = static_cast<UINT>( width );
    deviceDesc.height = static_cast<UINT>( height );
    deviceDesc.frameCount = frameCount;
    deviceDesc.backBufferFormat = DXGI_FORMAT_R8G8B8A8_UNORM;
    const SkullbonezCore::Core::SbResult deviceResult = m_renderDevice.Init( deviceDesc );

    if ( !deviceResult.Ok() )
    {
        return deviceResult;
    }

    // Recoverable error: all shipping raster shaders use SM6.6 direct heap indexing. A
    // table-binding fallback would retain the per-draw descriptor copies this
    // renderer contract deliberately removes, so unsupported devices fail
    // startup with actionable capability diagnostics.
    D3D12_FEATURE_DATA_SHADER_MODEL shaderModel = { D3D_SHADER_MODEL_6_6 };
    const HRESULT shaderModelResult = Device()->CheckFeatureSupport( D3D12_FEATURE_SHADER_MODEL, &shaderModel,
                                                                     sizeof( shaderModel ) );

    D3D12_FEATURE_DATA_D3D12_OPTIONS bindingOptions = {};

    const HRESULT bindingTierResult = Device()->CheckFeatureSupport( D3D12_FEATURE_D3D12_OPTIONS, &bindingOptions,
                                                                     sizeof( bindingOptions ) );

    if ( FAILED( shaderModelResult ) || shaderModel.HighestShaderModel < D3D_SHADER_MODEL_6_6 ||
         FAILED( bindingTierResult ) || bindingOptions.ResourceBindingTier < D3D12_RESOURCE_BINDING_TIER_3 )
    {
        return m_resultDiagnostics
            .Failure( "Rendering/DX12",
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
    m_raytracingOwner.ShutdownDXR();
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
    const SkullbonezCore::Core::SbResult descriptorResult = m_descriptorHeaps.Init( Device(), m_renderDevice.FrameCount() );

    if ( !descriptorResult.Ok() )
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
    Device()->CreateShaderResourceView( nullptr, &nullTextureSrv,
                                        m_descriptorHeaps.StagingCpuHandle( nullTextureSrvIndex ) );

    m_descriptorHeaps.PublishStaticDescriptor( Device(), nullTextureSrvIndex );

    // Lifetime: swap-chain images are replaced on resize, but the engine keeps
    // one stable RTV descriptor row per back buffer index. ResizeBuffers swaps
    // the image memory; CreateRenderTargetView overwrites the existing row with
    // a view record for the new image.
    // Docs: https://learn.microsoft.com/en-us/windows/win32/api/d3d12/nf-d3d12-id3d12device-createrendertargetview
    for ( UINT i = 0; i < m_renderDevice.FrameCount(); ++i )
    {
        const SkullbonezCore::Core::SbResult
            backBufferResult = Dx12BackendInitResult( m_resultDiagnostics,
                                                      SwapChain()->GetBuffer( i, IID_PPV_ARGS(
                                                                                     &m_frameOwner.RenderTarget( i ) ) ),
                                                      "SwapChain GetBuffer failed" );

        if ( !backBufferResult.Ok() )
        {
            return backBufferResult;
        }

        NameDx12ObjectIndexed( m_frameOwner.RenderTarget( i ), L"Skullbonez DX12 Swapchain Backbuffer", i );

        // Reserve one stable RTV row for each swap-chain buffer. ResizeBuffers
        // replaces the back-buffer resources later, but the descriptor rows stay
        // the same and are simply overwritten with new view records.
        m_descriptorHeaps.PublishBackBufferRtv( Device(), i, m_frameOwner.RenderTarget( i ) );
    }

    // Depth stencil
    ID3D12Resource* initialDepthStencil = nullptr;
    const SkullbonezCore::Core::SbResult
        depthStencilResult = m_renderDevice.CreateDepthStencilResource( width, height, initialDepthStencil );

    if ( !depthStencilResult.Ok() )
    {
        return depthStencilResult;
    }

    // Lifetime: the device owner adopts the candidate before the descriptor
    // owner publishes the matching DSV row for this presentation epoch.
    m_renderDevice.ReplaceDepthStencil( initialDepthStencil );
    m_descriptorHeaps.PublishMainDsv( Device(), m_renderDevice.DepthStencil() );

    // Create one upload buffer per active allocator. Each holds CPU-writable,
    // GPU-readable memory for per-frame constant buffers, dynamic vertex buffers, and texture
    // uploads. Partitioned per-allocator so that frame N+1's CPU recording cannot overwrite data
    // that frame N's GPU is still reading (the per-allocator fence wait in EnsureCommandListOpen
    // guarantees frame N is done before we reuse that allocator's upload buffer on frame N+2).
    // Docs: https://learn.microsoft.com/en-us/windows/win32/api/d3d12/nf-d3d12-id3d12device-createcommittedresource
    // Dx12FrameUploadSystem owns the actual upload resources and their
    // persistent CPU Map() pointers. RenderBackendDX12 now asks for byte ranges
    // instead of owning the raw upload-buffer lifecycle itself.
    // Invariant: retained geometry bytes are a fixed suffix of these cold
    // device-epoch arenas. Ordinary frame allocation cannot overwrite them,
    // and activating an upper-layer feature never creates a steady-phase GPU resource.
    // Capacity preserves the existing 32 MiB frame arena after adding the
    // disjoint compact retained slice; retained storage must not starve scene
    // or UI uploads.
    if ( !m_frameOwner.Uploads().Init( Device(), m_renderDevice.FrameCount(),
                                       Dx12FrameOwner::UPLOAD_BUFFER_SIZE +
                                           Dx12GeometryOwner::RetainedGeometryCompactBufferSizeBytes(),
                                       Dx12GeometryOwner::RetainedGeometryBufferSizeBytes(),
                                       L"Skullbonez DX12 Frame Upload Buffer" ) )
    {
        return m_resultDiagnostics.Failure( "Rendering/DX12", "DX12 frame upload buffer creation or persistent Map failed" );
    }

    if ( !m_geometryOwner.InitializeRetainedGeometryCommands( Device() ) )
    {
        return m_resultDiagnostics.Failure( "Rendering/DX12",
                                            "DX12 retained geometry indirect command signature creation failed" );
    }

    const SkullbonezCore::Core::SbResult rootSignatureResult = m_pipelineOwner.Initialize( Device() );

    if ( !rootSignatureResult.Ok() )
    {
        return rootSignatureResult;
    }

    Dx12TextureCommands textureCommands( m_renderDevice, m_frameOwner );
    const SkullbonezCore::Core::SbResult genMipsResult = m_textureOwner.Initialize( textureCommands );

    if ( !genMipsResult.Ok() )
    {
        return genMipsResult;
    }

    m_geometryOwner.AdoptGridLineShader( m_resourceBuilder.CreateShader( "shaders/grid_line" ) );

    if ( !m_geometryOwner.EnsureGridLinePipeline( Device(), m_pipelineOwner, DXGI_FORMAT_R8G8B8A8_UNORM ) ||
         !m_geometryOwner.EnsureGridLinePipeline( Device(), m_pipelineOwner, DXGI_FORMAT_R16G16B16A16_FLOAT ) )
    {
        return m_resultDiagnostics.Failure( "Rendering/DX12", "DX12 required grid-line warmup failed" );
    }

    constexpr TransientTriangleStyle requiredTriangleStyles[] = {
        TransientTriangleStyle::Color,
        TransientTriangleStyle::SoftAdditiveRibbon,
        TransientTriangleStyle::InstancedRibbon,
        TransientTriangleStyle::InstancedRibbonDepthHint,
    };

    for ( const TransientTriangleStyle style : requiredTriangleStyles )
    {
        const bool instancedRibbon = style == TransientTriangleStyle::InstancedRibbon ||
                                     style == TransientTriangleStyle::InstancedRibbonDepthHint;

        m_geometryOwner.AdoptTransientTriangleShader( style,
                                                      instancedRibbon
                                                          ? m_resourceBuilder.CreateShader( retainedGeometryShaderBaseName,
                                                                                            "retained_ribbon" )
                                                          : m_resourceBuilder.CreateShader(
                                                                Dx12GeometryOwner::TransientShaderBaseName( style ) ) );

        if ( !m_geometryOwner.HasTransientTriangleShader( style ) )
        {
            return m_resultDiagnostics.Failure( "Rendering/DX12",
                                                "DX12 required transient-triangle shader warmup failed (style=%u)",
                                                static_cast<unsigned int>( style ) );
        }
    }

    // GPU timestamp ownership is cold device-epoch diagnostics. The concrete
    // owner creates the query/readback pair and keeps covering-fence state local.
    const SkullbonezCore::Core::SbResult
        gpuTimerResult = m_diagnostics.InitializeGpuTimers( Device(), m_renderDevice.GraphicsQueue() );

    if ( !gpuTimerResult.Ok() )
    {
        return gpuTimerResult;
    }

    m_pipelineOwner.SetViewport( { 0.0f, 0.0f, static_cast<float>( width ), static_cast<float>( height ), 0.0f, 1.0f },
                                 { 0, 0, static_cast<LONG>( width ), static_cast<LONG>( height ) } );

    m_pipelineOwner.SetCurrentTargets( m_descriptorHeaps.BackBufferRtv( m_frameOwner.FrameIndex() ),
                                       m_descriptorHeaps.MainDsv() );

    // Publication boundary: hot texture/geometry operations and dimensions
    // become visible only after every required device, upload, pipeline, and
    // framebuffer resource is ready.
    m_textureOwner.BeginResourceEpoch();
    m_geometryOwner.BeginSubmissionEpoch();
    m_renderDevice.PublishInitialExtent( width, height );

    return SkullbonezCore::Core::SbResult::Success();
}


SkullbonezCore::Core::SbResult Dx12PipelineOwner::Initialize( ID3D12Device* device )
{
    // Lifetime: initialization is reusable after a prior Shutdown or partial
    // startup failure. Command bindings return to cold defaults before publishing
    // a new root signature.
    ResetCommandState();
    std::string reflectedContractError;

    if ( !ValidateGeneratedUnifiedRasterRootSignature( reflectedContractError ) )
    {
        // Recoverable error: checked-in DXIL is startup input. Reject a stale or incompatible
        // family before publishing a native root signature or any PSO that uses it.
        return m_resultDiagnostics.Failure( "Dx12PipelineOwner", "%s reflection rejected: %s",
                                            UnifiedRasterRootSignature::NAME, reflectedContractError.c_str() );
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
    params[UnifiedRasterRootSignature::ROOT_PARAMETER_DRAW_CONSTANTS]
        .Descriptor.ShaderRegister = UnifiedRasterRootSignature::SHADER_REGISTER_DRAW_CONSTANTS;
    params[UnifiedRasterRootSignature::ROOT_PARAMETER_DRAW_CONSTANTS]
        .Descriptor.RegisterSpace = UnifiedRasterRootSignature::REGISTER_SPACE;
    params[UnifiedRasterRootSignature::ROOT_PARAMETER_DRAW_CONSTANTS].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;

    params[UnifiedRasterRootSignature::ROOT_PARAMETER_TEXTURE_INDICES]
        .ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
    params[UnifiedRasterRootSignature::ROOT_PARAMETER_TEXTURE_INDICES]
        .Constants.ShaderRegister = UnifiedRasterRootSignature::SHADER_REGISTER_TEXTURE_INDICES;
    params[UnifiedRasterRootSignature::ROOT_PARAMETER_TEXTURE_INDICES]
        .Constants.RegisterSpace = UnifiedRasterRootSignature::REGISTER_SPACE;
    params[UnifiedRasterRootSignature::ROOT_PARAMETER_TEXTURE_INDICES]
        .Constants.Num32BitValues = UnifiedRasterRootSignature::TEXTURE_SLOT_COUNT;
    params[UnifiedRasterRootSignature::ROOT_PARAMETER_TEXTURE_INDICES].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;

    D3D12_STATIC_SAMPLER_DESC samplers[3] = {};
    const auto nativeAddressMode = []( UnifiedRasterRootSignature::StaticSampler::AddressMode mode )
    {
        return mode == UnifiedRasterRootSignature::StaticSampler::AddressMode::Wrap ? D3D12_TEXTURE_ADDRESS_MODE_WRAP
                                                                                    : D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
    };

    // s0 repeats material textures; independent skybox faces bind s1 below.
    samplers[0].Filter = D3D12_FILTER_MIN_MAG_MIP_LINEAR;
    samplers[0].AddressU = nativeAddressMode( UnifiedRasterRootSignature::STATIC_SAMPLERS[0].addressMode );
    samplers[0].AddressV = samplers[0].AddressU;
    samplers[0].AddressW = samplers[0].AddressU;
    samplers[0].MaxAnisotropy = 1;
    samplers[0].ComparisonFunc = D3D12_COMPARISON_FUNC_NEVER;
    samplers[0].MaxLOD = D3D12_FLOAT32_MAX; // allow all mip levels (default 0 = mip 0 only!)
    samplers[0].ShaderRegister = UnifiedRasterRootSignature::STATIC_SAMPLERS[0].shaderRegister;

    samplers[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;

    // s1 clamps render targets and independent skybox faces at their edges.
    samplers[1].Filter = D3D12_FILTER_MIN_MAG_MIP_LINEAR;
    samplers[1].AddressU = nativeAddressMode( UnifiedRasterRootSignature::STATIC_SAMPLERS[1].addressMode );
    samplers[1].AddressV = samplers[1].AddressU;
    samplers[1].AddressW = samplers[1].AddressU;
    samplers[1].MaxAnisotropy = 1;
    samplers[1].ComparisonFunc = D3D12_COMPARISON_FUNC_NEVER;
    samplers[1].MaxLOD = D3D12_FLOAT32_MAX;
    samplers[1].ShaderRegister = UnifiedRasterRootSignature::STATIC_SAMPLERS[1].shaderRegister;
    samplers[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;

    // s3: point clamp for manual shadow-map PCF.
    samplers[2].Filter = D3D12_FILTER_MIN_MAG_MIP_POINT;
    samplers[2].AddressU = nativeAddressMode( UnifiedRasterRootSignature::STATIC_SAMPLERS[2].addressMode );
    samplers[2].AddressV = samplers[2].AddressU;
    samplers[2].AddressW = samplers[2].AddressU;
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

    if ( FAILED( D3D12SerializeVersionedRootSignature( &rootSigDesc, signature.GetAddressOf(), error.GetAddressOf() ) ) )
    {
        std::string msg = "Root signature serialization failed";

        if ( error )
        {
            msg += ": ";
            msg += static_cast<const char*>( error->GetBufferPointer() );
        }

        return m_resultDiagnostics.Failure( "Rendering/DX12", "%s", msg.c_str() );
    }

    if ( signature->GetBufferSize() > m_rootSignatureSerialized.size() )
    {
        return m_resultDiagnostics.Failure( "Rendering/DX12", "UnifiedRaster serialized root signature exceeds reload cap" );
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
    const HRESULT rootSignatureResult = device->CreateRootSignature( 0, signature->GetBufferPointer(),
                                                                     signature->GetBufferSize(),
                                                                     IID_PPV_ARGS( &m_rootSignature ) );

    if ( FAILED( rootSignatureResult ) || !m_rootSignature )
    {
        if ( m_rootSignature )
        {
            m_rootSignature->Release();
            m_rootSignature = nullptr;
        }

        return m_resultDiagnostics.Failure( "Rendering/DX12", "CreateRootSignature failed" );
    }

    NameDx12Object( m_rootSignature, L"Skullbonez DX12 UnifiedRaster Root Signature" );

    // Fatal invariant: exhausting a 64-bit sequence requires more successful root-
    // signature creations than this owner can perform in any valid lifetime.
    // Publishing zero or reusing an old identity could alias incompatible PSOs.
    if ( m_nextRootSignatureIdentity == 0 )
    {
        SB_FATAL( "Dx12PipelineOwner", "Root-signature identity sequence exhausted." );
    }

    m_rootSignatureIdentity = m_nextRootSignatureIdentity++;

    // Recoverable error: a persistent PSO cache is an optional cold-start accelerator.
    // Its owner logs and discards missing/corrupt/driver-incompatible bytes;
    // failure must never reject an otherwise valid renderer device.
    // Why: ID3DBlob publishes serialized bytes through its COM void-pointer
    // ABI. The cache owner receives an immutable typed view only.
    m_persistentPsoCache.Initialize(
        { static_cast<const std::uint8_t*>( signature->GetBufferPointer() ), signature->GetBufferSize() } );

#ifdef _DEBUG
    SkullbonezCore::Core::Log()
        .WriteEventf( "dx12_raster_binding_contract name=%s root_parameters=%u cbv=b%u texture_indices=b%u "
                      "resource_heap=direct material_payload=packed_instance_params samplers=s%u,s%u,s%u "
                      "bind_texture_slots=%d",
                      UnifiedRasterRootSignature::NAME, UnifiedRasterRootSignature::ROOT_PARAMETER_COUNT,
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
            // Fatal invariant: terminal shutdown cannot release a partially owned device
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

        if ( !openResult.Ok() )
        {
            // Fatal invariant: shutdown cannot return a recoverable result, and Present
            // cannot legally drain a back buffer left in render-target state.
            SB_FATAL( "RenderBackendDX12",
                      "Shutdown could not open the command list for the final backbuffer transition. owner=%s "
                      "reason=%s",
                      openResult.ErrorOwner(), openResult.ErrorMessage() );
        }

        if ( !m_frameOwner.TransitionBackbuffer( "ShutdownBackbufferPresent", RenderGraphResourceAccess::Present ) )
        {
            const SkullbonezCore::Core::SbResult transitionResult = m_frameOwner.CurrentResult();
            SB_FATAL( "RenderBackendDX12", "Shutdown could not record the final backbuffer transition. owner=%s reason=%s",
                      transitionResult.ErrorOwner(), transitionResult.ErrorMessage() );
        }
    }

    // Ensure any open command list is closed and submitted before waiting.
    // Hazard: an open epoch with a retained failure never reached
    // ExecuteCommandLists. Terminal cleanup discards it in place; closing or
    // submitting would issue work after the first retained failure.
    if ( m_frameOwner.IsOpen() && !m_frameOwner.HasFailure() )
    {
        m_frameOwner.AssertProfilerClosed( "Shutdown" );
        const SkullbonezCore::Core::SbResult closeResult = m_frameOwner.CommitClose( CommandList()->Close(),
                                                                                     "Shutdown command list Close" );

        if ( !closeResult.Ok() )
        {
            SB_FATAL( "RenderBackendDX12",
                      "Shutdown command-list Close failed; resources remain process-owned. owner=%s reason=%s",
                      closeResult.ErrorOwner(), closeResult.ErrorMessage() );
        }

        const SkullbonezCore::Core::SbResult submitResult = m_frameOwner.SubmitClosed();

        if ( !submitResult.Ok() )
        {
            SB_FATAL( "RenderBackendDX12", "Shutdown command-list submission failed. owner=%s reason=%s",
                      submitResult.ErrorOwner(), submitResult.ErrorMessage() );
        }
    }

    // Wait for all GPU work to complete (command queue + pending presents).
    const SkullbonezCore::Core::SbResult initialDrainResult = m_frameOwner.HasFailure()
                                                                  ? m_frameOwner.DrainForResourceRelease()
                                                                  : m_frameOwner.WaitForGpu();

    if ( !initialDrainResult.Ok() )
    {
        // Fatal invariant: releasing any backend object after this point could race a
        // submitted command stream. Terminal shutdown must stop instead.
        SB_FATAL( "RenderBackendDX12", "Shutdown could not prove initial GPU queue completion. owner=%s reason=%s",
                  initialDrainResult.ErrorOwner(), initialDrainResult.ErrorMessage() );
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

        const SkullbonezCore::Core::SbResult
            checkedPresent = Dx12BackendOperationResult( m_resultDiagnostics, drainPresentResult,
                                                         "Shutdown swap-chain Present drain failed" );

        if ( !checkedPresent.Ok() )
        {
            SB_FATAL( "RenderBackendDX12", "Shutdown could not drain the swap-chain present queue. owner=%s reason=%s",
                      checkedPresent.ErrorOwner(), checkedPresent.ErrorMessage() );
        }

        const SkullbonezCore::Core::SbResult presentDrainResult = m_frameOwner.WaitForGpu();

        if ( !presentDrainResult.Ok() )
        {
            SB_FATAL( "RenderBackendDX12",
                      "Shutdown could not prove queue completion after the final Present. owner=%s reason=%s",
                      presentDrainResult.ErrorOwner(), presentDrainResult.ErrorMessage() );
        }
    }

    if ( m_frameOwner.HasSubmittedWork() )
    {
        SB_FATAL( "RenderBackendDX12", "Shutdown reached resource release with unproven submitted GPU work." );
    }

    if ( !m_frameOwner.RetirementEmpty() )
    {
        SB_FATAL( "RenderBackendDX12", "Shutdown drain completed but deferred GPU resources remain quarantined. count=%zu",
                  m_frameOwner.RetirementCount() );
    }

    // Lifetime: both terminal drains above succeeded, so detached screenshot
    // readbacks can no longer be referenced by the GPU or DXGI present queue.
    m_backbufferCapture.ReleaseAfterTerminalDrain();

    // DXR resources hang off newer D3D12 interfaces and contain GPU-side
    // acceleration structures. Release them before the shared renderer objects
    // below so no raytracing object outlives the device/command-list aliases it
    // was created from.
    m_raytracingOwner.ShutdownDXR();

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
                auto* msg = static_cast<D3D12_MESSAGE*>( malloc( msgLen ) );

                if ( !msg )
                {
                    continue;
                }

                infoQueue->GetMessage( i, msg, &msgLen );

                if ( msg->Severity <= D3D12_MESSAGE_SEVERITY_ERROR )
                {
                    ++errorCount;
                }

                if ( fp && msg->Severity <= D3D12_MESSAGE_SEVERITY_ERROR )
                {
                    fprintf( fp, "[%llu] ID=%d: %s\n", i, static_cast<int>( msg->ID ), msg->pDescription );
                }

                free( msg );
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
#if defined( SKULLBONEZ_DEVELOPMENT_TOOLS )

    if ( m_imguiRenderer.IsInitialized() )
    {
        // Fatal invariant: only the ImGui context owner can safely invoke the vendor
        // shutdown API. Reaching device teardown still bound would release
        // descriptor/device storage before its context-owned resources.
        SB_FATAL( "RenderBackendDX12", "DX12 shutdown reached descriptor teardown with the ImGui renderer still bound." );
    }
#endif
    m_geometryOwner.Shutdown();
    m_shaderDevelopment.ResetAfterShutdown();
    m_textureOwner.Shutdown();
    m_pipelineOwner.Shutdown();
    m_frameOwner.Uploads().Shutdown();

    for ( UINT i = 0; i < m_renderDevice.FrameCount(); ++i )
    {
        if ( m_frameOwner.RenderTarget( i ) )
        {
            m_frameOwner.RenderTarget( i )->Release();
            m_frameOwner.RenderTarget( i ) = nullptr;
        }
    }

    m_descriptorHeaps.Shutdown();
    m_renderDevice.Shutdown();
    m_frameOwner.ResetAfterShutdown();
}


// Frame Management:


SkullbonezCore::Core::SbResult Dx12FrameOwner::Present( Dx12Diagnostics& diagnostics )
{
    SKORE_TRACY_SCOPED_OWNER_ZONE( "Frame/DX12/Present", ::HashStr( "Frame/DX12/Present" ) );
    SkullbonezCore::Core::SbResult stateResult = EnsureOpen();

    if ( !stateResult.Ok() )
    {
        return stateResult;
    }

    // Opportunistically consume the previous frame's resolved timer buffer before writing
    // new query results into the same readback resource.
    diagnostics.ConsumeGpuTimerReadback( DiagnosticsFrame(), false );

    // Resolve GPU timer queries — only resolve contiguous ranges of slots that actually
    // had EndQuery recorded this frame. Resolving unwritten slots triggers D3D12 error 1319.
    const bool resolvedTimerSlotsThisFrame = diagnostics.ResolveWrittenGpuTimers( DiagnosticsFrame() );

    // Frame-edge exception: Present coordinates command-list close, submission,
    // swap-chain flip, and fence advancement after all executable graph passes.
    // It is not command-recording frame work and therefore retains this one
    // explicit RenderTarget -> Present transition.
    TransitionBackbuffer( "PresentBackbuffer", RenderGraphResourceAccess::Present );

    if ( HasFailure() )
    {
        return CurrentResult();
    }

    // Close the command list — finalizes the recorded commands. A closed command list can be
    // submitted to the GPU. No more commands can be recorded until Reset is called.
    // Docs: https://learn.microsoft.com/en-us/windows/win32/api/d3d12/nf-d3d12-id3d12graphicscommandlist-close
    AssertProfilerClosed( "Present" );
    {
        SKORE_TRACY_SCOPED_OWNER_ZONE( "Frame/DX12/CommandRecording/Close",
                                       ::HashStr( "Frame/DX12/CommandRecording/Close" ) );

        stateResult = CommitClose( CommandList()->Close(), "Present command list Close" );
    }

    if ( !stateResult.Ok() )
    {
        return stateResult;
    }

    // Submit the completed command list to the GPU for execution. The GPU processes commands
    // asynchronously — this call returns immediately while the GPU works in the background.
    // Docs: https://learn.microsoft.com/en-us/windows/win32/api/d3d12/nf-d3d12-id3d12commandqueue-executecommandlists
    stateResult = SubmitClosed();

    if ( !stateResult.Ok() )
    {
        return stateResult;
    }

    // Present the frame — flips the swap chain to show the just-rendered back buffer on screen.
    // Sync interval is configurable so perf scenes can disable V-Sync while visual scenes keep it.
    // Docs: https://learn.microsoft.com/en-us/windows/win32/api/dxgi/nf-dxgi-idxgiswapchain-present
    const UINT syncInterval = m_device.VsyncEnabled() ? 1u : 0u;
    const UINT presentFlags = ( !m_device.VsyncEnabled() && m_device.AllowTearing() ) ? DXGI_PRESENT_ALLOW_TEARING : 0u;
    HRESULT presentResult = S_OK;
    {
        SKORE_TRACY_SCOPED_OWNER_ZONE( "Frame/DX12/SwapChainPresent", ::HashStr( "Frame/DX12/SwapChainPresent" ) );
        presentResult = m_device.SwapChain()->Present( syncInterval, presentFlags );
    }

    if ( IsDx12DeviceLostResult( presentResult ) )
    {
        m_device.ReportDeviceLost( "Present", presentResult );
        return RetainDeviceLoss( "Present", presentResult );
    }

    const SkullbonezCore::Core::SbResult presentFailure = Dx12BackendOperationResult( m_resultDiagnostics, presentResult,
                                                                                      "SwapChain Present failed" );

    if ( !presentFailure.Ok() )
    {
        return RetainFailure( presentFailure );
    }

    // Signal the fence with the current frame's value. When the GPU reaches
    // this point in its command stream, it updates the fence to that value.
    // Later, EnsureCommandListOpen asks the timeline helper whether this value
    // has completed before reusing this frame's command allocator, upload arena,
    // and transient descriptor range.
    UINT64 presentFenceValue = 0;
    const SkullbonezCore::Core::SbResult signalResult = SignalFrame( presentFenceValue );

    if ( !signalResult.Ok() )
    {
        return signalResult;
    }

    SetFrameFenceValue( AllocatorIndex(), presentFenceValue );
    AssignRetirementFence( presentFenceValue );

    // Invariant: publish this frame's resolved timer fence without waiting for an older
    // diagnostic sample. Dx12Diagnostics deliberately replaces a stale pending sample so
    // free-running Present never blocks the renderer merely to preserve timing telemetry.
    diagnostics.PublishResolvedGpuTimerFence( resolvedTimerSlotsThisFrame, presentFenceValue );

    // Advance to next frame's allocator and swap chain buffer.
    AdvanceFrameIndices();
    m_pipeline.SetCurrentColorTarget( m_descriptors.BackBufferRtv( FrameIndex() ) );

    // Charge allocator/upload/descriptor pacing to Present/VsyncWait instead of
    // letting the first render command of the next frame hit this wait mid-frame.
    const UINT64 nextFrameFenceValue = FrameFenceValue( AllocatorIndex() );

    if ( nextFrameFenceValue > m_device.FrameFence().CompletedValue() )
    {
        const SkullbonezCore::Core::SbResult waitResult = WaitForFrameFence( nextFrameFenceValue );

        if ( !waitResult.Ok() )
        {
            return waitResult;
        }
    }

    ReleaseCompletedRetirements( false );
    return SkullbonezCore::Core::SbResult::Success();
}


SkullbonezCore::Core::SbResult Dx12FrameOwner::FlushGPU()
{
    if ( HasFailure() )
    {
        return CurrentResult();
    }

    if ( !CommandList() || !m_device.GraphicsQueue() || !m_device.FrameFence().IsReady() ||
         !m_device.CommandAllocator( AllocatorIndex() ) )
    {
        // Recoverable error: an active resource-mutation drain cannot claim success unless
        // it can both wait for submitted work and reopen the recording epoch.
        return RetainFailure(
            m_resultDiagnostics
                .Failure( "Rendering/DX12",
                          "FlushGPU requires a complete command queue, fence, allocator, and command list." ) );
    }

    Dx12GpuDrainProgress drainProgress( IsOpen() );

    if ( drainProgress.RequiresClose() )
    {
        AssertProfilerClosed( "FlushGPU" );
        const SkullbonezCore::Core::SbResult closeResult = CommitClose( CommandList()->Close(),
                                                                        "FlushGPU command list Close" );

        if ( !closeResult.Ok() )
        {
            return closeResult;
        }

        if ( !drainProgress.CommitClose() || !drainProgress.CanSubmit() )
        {
            // Fatal invariant: this local sequence can advance only after the successful
            // Close above; disagreement means the engine's ordering proof broke.
            SB_FATAL( "Dx12FrameOwner", "FlushGPU drain order rejected a successful command-list Close." );
        }

        const SkullbonezCore::Core::SbResult submitResult = SubmitClosed();

        if ( !submitResult.Ok() )
        {
            return submitResult;
        }

        if ( !drainProgress.CommitSubmission() )
        {
            SB_FATAL( "Dx12FrameOwner", "FlushGPU drain order rejected command-list submission." );
        }
    }

    if ( !drainProgress.CanWait() )
    {
        SB_FATAL( "Dx12FrameOwner", "FlushGPU attempted a fence wait before submission ordering completed." );
    }

    // Hazard: ExecuteCommandLists has no success result. SubmitClosedCommandList
    // already marked the work live; if this drain fence fails, both the sticky
    // recoverable result and m_submittedWork block mutation, reuse, and unfenced release.
    const SkullbonezCore::Core::SbResult waitResult = CommitWait( WaitForGpu() );

    if ( !waitResult.Ok() )
    {
        return waitResult;
    }

    if ( !drainProgress.CommitWait() || !drainProgress.CanReopen() )
    {
        SB_FATAL( "Dx12FrameOwner", "FlushGPU drain order rejected a successful fence wait." );
    }

    // Hazard: scene swaps and graphics stress call this in the middle of the
    // runtime loop. Reopen only after the full wait, and return its failure so
    // no caller treats a closed/failed epoch as permission to destroy resources.
    const SkullbonezCore::Core::SbResult reopenResult = EnsureOpen();

    if ( !reopenResult.Ok() )
    {
        return reopenResult;
    }

    if ( !drainProgress.CommitReopen() || !drainProgress.IsMutationSafe() )
    {
        SB_FATAL( "Dx12FrameOwner", "FlushGPU drain order rejected a successful command-list reopen." );
    }

    return SkullbonezCore::Core::SbResult::Success();
}


SkullbonezCore::Core::SbResult Dx12FrameOwner::DrainForResourceRelease()
{
    if ( !HasFailure() )
    {
        return FlushGPU();
    }

    if ( DeviceLost() )
    {
        // Lifetime: DXGI device removal terminates this device/queue lifetime;
        // the submitted commands can no longer execute against resources from
        // it. Do not issue a fence Signal after removal. Abandon the completion
        // proof only for terminal COM release, never for runtime reuse.
        AbandonSubmittedWork();
        return SkullbonezCore::Core::SbResult::Success();
    }

    // Lifetime: a failed, never-submitted recording epoch cannot reference any
    // resource from the GPU. Release is already safe and teardown must not turn
    // that expected recoverable error path into a second submission or a destructor fatal.
    if ( !HasSubmittedWork() )
    {
        return SkullbonezCore::Core::SbResult::Success();
    }

    // Hazard: if earlier work reached the queue before a later recording error,
    // terminal teardown may signal/wait for that submitted work. It must not
    // submit, reopen, or clear the sticky command-path failure.
    return WaitForGpu();
}


SkullbonezCore::Core::SbResult Dx12FrameOwner::Resize( int width, int height )
{
    if ( width <= 0 || height <= 0 )
    {
        return SkullbonezCore::Core::SbResult::Success();
    }

    // Recoverable error: Resize replaces back buffers and depth memory. FlushGPU closes and
    // submits any open list, proves all queue work complete, and reopens an empty
    // recording epoch. Do not release the first old resource on any failure.
    const SkullbonezCore::Core::SbResult drainResult = FlushGPU();

    if ( !drainResult.Ok() )
    {
        return drainResult;
    }

    Dx12RecreationTransaction transaction( m_resultDiagnostics );
    transaction.Begin( m_device.RecreationGeneration() );

    // Prepare the independent depth candidate before releasing a single
    // published resource. A creation failure therefore leaves the current
    // framebuffer and dimensions untouched.
    ID3D12Resource* candidateDepth = nullptr;
    const SkullbonezCore::Core::SbResult candidateDepthResult = m_device.CreateDepthStencilResource( width, height,
                                                                                                     candidateDepth );

    if ( !candidateDepthResult.Ok() )
    {
        return transaction.Fail( candidateDepthResult );
    }

    if ( !transaction.CommitCandidateReady() )
    {
        candidateDepth->Release();
        SB_FATAL( "Dx12FrameOwner", "Resize transaction rejected prepared depth candidate." );
    }

    // DXGI requires every application-held back-buffer reference to be released
    // before ResizeBuffers. Member publication is restored from the swap chain
    // if ResizeBuffers rejects the request without removing the device.
    for ( UINT i = 0; i < m_device.FrameCount(); ++i )
    {
        if ( RenderTarget( i ) )
        {
            RenderTarget( i )->Release();
        }

        RenderTarget( i ) = nullptr;
    }

    if ( !transaction.CommitOldReferencesReleased() )
    {
        candidateDepth->Release();
        SB_FATAL( "Dx12FrameOwner", "Resize transaction rejected released back-buffer references." );
    }

    const UINT resizeFlags = m_device.AllowTearing() ? DXGI_SWAP_CHAIN_FLAG_ALLOW_TEARING : 0u;
    const HRESULT resizeResult = m_device.SwapChain()->ResizeBuffers( m_device.FrameCount(), static_cast<UINT>( width ),
                                                                      static_cast<UINT>( height ),
                                                                      DXGI_FORMAT_R8G8B8A8_UNORM, resizeFlags );

    if ( IsDx12DeviceLostResult( resizeResult ) )
    {
        candidateDepth->Release();
        m_device.ReportDeviceLost( "ResizeBuffers", resizeResult );
        return transaction.Fail( RetainDeviceLoss( "ResizeBuffers", resizeResult ) );
    }

    const SkullbonezCore::Core::SbResult resizeFailure = Dx12BackendOperationResult( m_resultDiagnostics, resizeResult,
                                                                                     "SwapChain ResizeBuffers failed" );

    if ( !resizeFailure.Ok() )
    {
        candidateDepth->Release();

        // A failed ResizeBuffers leaves the old swap-chain buffers owned by
        // DXGI. Reacquire all of them before returning so the published backend
        // remains usable at its previous dimensions.
        ID3D12Resource* restored[Dx12FrameOwner::MAX_FRAME_COUNT] = {};

        bool restoredAll = true;

        for ( UINT i = 0; i < m_device.FrameCount(); ++i )
        {
            if ( FAILED( m_device.SwapChain()->GetBuffer( i, IID_PPV_ARGS( &restored[i] ) ) ) || !restored[i] )
            {
                restoredAll = false;
                break;
            }
        }

        if ( restoredAll )
        {
            for ( UINT i = 0; i < m_device.FrameCount(); ++i )
            {
                RenderTarget( i ) = restored[i];
                Device()->CreateRenderTargetView( RenderTarget( i ), nullptr, m_descriptors.BackBufferRtv( i ) );
            }

            m_pipeline.SetCurrentColorTarget( m_descriptors.BackBufferRtv( FrameIndex() ) );
            return transaction.Fail( resizeFailure );
        }

        for ( ID3D12Resource* resource : restored )
        {
            if ( resource )
            {
                resource->Release();
            }
        }

        return RetainFailure( transaction.Fail(
            m_resultDiagnostics.Failure( "Rendering/DX12",
                                         "ResizeBuffers failed and the previous back buffers could not be restored" ) ) );
    }

    if ( !transaction.CommitSwapChainResized() )
    {
        candidateDepth->Release();
        SB_FATAL( "Dx12FrameOwner", "Resize transaction rejected successful ResizeBuffers." );
    }

    ID3D12Resource* candidateBackBuffers[Dx12FrameOwner::MAX_FRAME_COUNT] = {};

    for ( UINT i = 0; i < m_device.FrameCount(); ++i )
    {
        const SkullbonezCore::Core::SbResult
            backBufferResult = Dx12BackendOperationResult( m_resultDiagnostics,
                                                           m_device.SwapChain()->GetBuffer( i,
                                                                                            IID_PPV_ARGS(
                                                                                                &candidateBackBuffers[i] ) ),
                                                           "SwapChain GetBuffer after resize failed" );

        if ( !backBufferResult.Ok() )
        {
            for ( ID3D12Resource* resource : candidateBackBuffers )
            {
                if ( resource )
                {
                    resource->Release();
                }
            }

            candidateDepth->Release();
            return RetainFailure( transaction.Fail( backBufferResult ) );
        }
    }

    if ( !transaction.CommitBackBuffersReady() )
    {
        for ( ID3D12Resource* resource : candidateBackBuffers )
        {
            resource->Release();
        }

        candidateDepth->Release();
        SB_FATAL( "Dx12FrameOwner", "Resize transaction rejected complete back-buffer candidates." );
    }

    // Publication boundary: no member points at a replacement until every
    // back buffer and the depth candidate exists.
    RefreshFrameIndex();

    // ResizeBuffers puts all back buffers into PRESENT state, so the next
    // executable backbuffer graph pass compiles from that concrete state.
    SetBackBufferAccess( RenderGraphResourceAccess::Present );

    for ( UINT i = 0; i < m_device.FrameCount(); ++i )
    {
        RenderTarget( i ) = candidateBackBuffers[i];
        NameDx12ObjectIndexed( RenderTarget( i ), L"Skullbonez DX12 Swapchain Backbuffer", i );

        m_descriptors.RepublishBackBufferRtv( Device(), i, RenderTarget( i ) );
    }

    ID3D12Resource* oldDepth = m_device.ReplaceDepthStencil( candidateDepth );
    m_descriptors.PublishMainDsv( Device(), m_device.DepthStencil() );

    if ( oldDepth )
    {
        oldDepth->Release();
    }

    m_pipeline.SetViewport( { 0.0f, 0.0f, static_cast<float>( width ), static_cast<float>( height ), 0.0f, 1.0f },
                            { 0, 0, static_cast<LONG>( width ), static_cast<LONG>( height ) } );

    m_pipeline.SetCurrentTargets( m_descriptors.BackBufferRtv( FrameIndex() ), m_descriptors.MainDsv() );
    const uint64_t recreationGeneration = m_device.PublishResizedExtent( width, height );

    if ( !transaction.CommitPublished( recreationGeneration ) || transaction.PublishedGeneration() != recreationGeneration )
    {
        SB_FATAL( "Dx12FrameOwner", "Resize transaction failed its publication proof." );
    }

    return SkullbonezCore::Core::SbResult::Success();
}


// Viewport & Clear:


void Dx12FrameOwner::SetViewport( int x, int y, int w, int h )
{
    m_pipeline.SetViewport( { static_cast<float>( x ), static_cast<float>( y ), static_cast<float>( w ),
                              static_cast<float>( h ), 0.0f, 1.0f },
                            { static_cast<LONG>( x ), static_cast<LONG>( y ), static_cast<LONG>( x + w ),
                              static_cast<LONG>( y + h ) } );
}


void Dx12FrameOwner::Clear( const ClearTargetDesc& target )
{
    if ( !EnsureOpen().Ok() )
    {
        return;
    }

    if ( !m_pipeline.RenderingToFramebuffer() && BackBufferAccess() != RenderGraphResourceAccess::RenderTarget )
    {
        // Invariant: BackbufferClear is an executable graph pass. Clear only
        // records the operation after that pass has acquired RenderTarget.
        SB_FATAL( "Dx12FrameOwner", "Backbuffer clear reached the frame owner without graph acquisition. tracked=%s",
                  ToString( BackBufferAccess() ) );
    }

    // Bind the render target and depth buffer to the Output Merger (OM) stage — this tells the
    // GPU where to write pixel colors and depth values for subsequent draw calls.
    // Docs:
    // https://learn.microsoft.com/en-us/windows/win32/api/d3d12/nf-d3d12-id3d12graphicscommandlist-omsetrendertargets
    m_pipeline.BindCurrentOutputs( CommandList() );

    // Viewport defines where rendering appears, and the scissor rect clips pixels
    // (pixels outside the scissor are clipped/discarded). Both must be set every time in DX12.
    // Docs: https://learn.microsoft.com/en-us/windows/win32/api/d3d12/nf-d3d12-id3d12graphicscommandlist-rssetviewports
    // Docs:
    // https://learn.microsoft.com/en-us/windows/win32/api/d3d12/nf-d3d12-id3d12graphicscommandlist-rssetscissorrects
    if ( target.color )
    {
        // Clear the render target to a solid color (wipes the entire back buffer).
        // Docs:
        // https://learn.microsoft.com/en-us/windows/win32/api/d3d12/nf-d3d12-id3d12graphicscommandlist-clearrendertargetview
        m_pipeline.ClearCurrentColor( CommandList(), target.colorValue );
    }

    if ( target.depth )
    {
        // Clear the depth buffer to 1.0 (maximum distance), so all subsequent draws will pass
        // the depth test. This is done at the start of each frame or when switching render targets.
        // Docs:
        // https://learn.microsoft.com/en-us/windows/win32/api/d3d12/nf-d3d12-id3d12graphicscommandlist-cleardepthstencilview
        m_pipeline.ClearCurrentDepth( CommandList(), target.depthValue );
    }
}


// PSO Management:


// Resource Creation:


// Textures:


// InitGenMipsPipeline — compile generate_mips.hlsl, create root signature and
// GPU-side mip generation uses a compute PSO separate from raster draw PSOs.
//
// Root signature layout:
//   Param 0: 4 root constants at b0 (NumMipLevels, SrcDimension, TexelSizeX, TexelSizeY)
//   Param 1: Descriptor table — 1 SRV  (t0): source mip (single-level view)
//   Param 2: Descriptor table — 4 UAVs (u0-u3): output mips (unused slots use null UAV)
//   Static sampler s0: LinearClamp


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


// Screenshot:


// Dynamic VB:


// Per-vertex colored line data is interleaved [x,y,z,r,g,b] per vertex (6 floats each).
// Uses the shared upload buffer to stream vertex data and draws with LINE_LIST topology.
// Lazy-creates a LINE_LIST PSO on first call.


// Instanced mesh:


// Queries:


// DXR Raytracing:


// GPU Timers:
