/*
File: SkullbonezSource/Rendering/DX12/Dx12GraphTransientPool.cpp
Purpose:
  Implements DX12 render-graph transient materialization and target binding.

Summary:
  Compatible logical graph lifetimes reuse physical texture slots. New slots
  receive typed descriptor allocations and optional texture handles, while
  callback-owned passes use a balanced transaction to render into a slot and
  restore the previous output state.

Glossary:
  Materialization: Creating the native texture and view rows for a logical slot.
  Current access: Last compiled graph use emitted for a physical texture slot.

Invariants:
  - Slot reuse requires the compiler's pool id and descriptor shape to match.
  - Only transitions from the current compiled graph may change a slot's state.
  - Every required compiled transition emits exactly one concrete DX12 barrier.
  - Engine texture handles unregister before native resource release.
  - Descriptor retirement remains delegated to the frame/descriptor owners.

Related:
  - SkullbonezSource/Rendering/DX12/Dx12GraphTransientPool.h
  - SkullbonezSource/Rendering/DX12/Dx12DescriptorHeaps.h
  - SkullbonezSource/Rendering/DX12/Dx12FrameOwner.h
  - Agentic/Reference/engine-glossary.md
*/
#include "Dx12GraphTransientPool.h"

#include "Dx12DescriptorHeaps.h"
#include "Dx12FrameOwner.h"
#include "Dx12RenderGraphExecutor.h"
#include "RenderBackendDX12.h"
#include "../../Core/FatalError.h"
#include "../../Core/Log.h"

#include <cstdio>

using namespace SkullbonezCore::Rendering;

namespace
{
DXGI_FORMAT ToColorFormat( RenderGraphResourceFormat format )
{
    switch ( format )
    {
    case RenderGraphResourceFormat::RGBA8:
        return DXGI_FORMAT_R8G8B8A8_UNORM;
    case RenderGraphResourceFormat::RGBA16F:
        return DXGI_FORMAT_R16G16B16A16_FLOAT;
    default:
        SB_FATAL( "Dx12GraphTransientPool", "Unsupported render graph color transient format. format=%d",
                  static_cast<int>( format ) );
    }
}

DXGI_FORMAT ToSrvFormat( RenderGraphResourceFormat format )
{
    return format == RenderGraphResourceFormat::Depth24Stencil8 ? DXGI_FORMAT_R24_UNORM_X8_TYPELESS
                                                                : ToColorFormat( format );
}

size_t CountDescriptorRows( const RenderGraphDescriptorNeeds& descriptors )
{
    return ( descriptors.renderTarget ? 1u : 0u ) + ( descriptors.depthStencil ? 1u : 0u ) +
           ( descriptors.shaderResource ? 1u : 0u ) + ( descriptors.unorderedAccess ? 1u : 0u );
}

void MarkMaterializationFailure( RenderGraphTransientMaterializationStats& stats, HRESULT result,
                                 const RenderGraphResourceDesc& resource )
{
    stats.failed = true;
    stats.failureHresult = static_cast<unsigned int>( result );
    std::snprintf( stats.failureStage, sizeof( stats.failureStage ), "%s", "CreateCommittedResource" );
    std::snprintf( stats.failureResource, sizeof( stats.failureResource ), "%s",
                   ( resource.name && resource.name[0] != '\0' ) ? resource.name : "UnnamedGraphTransient" );

    SkullbonezCore::Core::Log().WriteEventf( "dx12_graph_transient_materialize_failed stage=%s resource=%s hresult=0x%08X",
                                             stats.failureStage, stats.failureResource, stats.failureHresult );

    SkullbonezCore::Core::Log().FlushAll();
}
} // namespace

Dx12GraphTransientPool::Dx12GraphTransientPool( Dx12RenderDevice& device, Dx12DescriptorHeaps& descriptors,
                                                Dx12FrameOwner& frame, Dx12TextureOwner& textures,
                                                Dx12PipelineOwner& pipeline )
    : m_device( device ), m_descriptors( descriptors ), m_frame( frame ), m_textures( textures ), m_pipeline( pipeline )
{
}

void Dx12GraphTransientPool::RetireSlotForReplacement( GraphTransientResourceDX12& slot )
{
    UINT srvIndex = slot.srvIndex;

    if ( slot.textureHandle != 0 )
    {
        const UINT registeredIndex = m_textures.UnregisterSRV( slot.textureHandle );

        if ( registeredIndex != UINT_MAX )
        {
            srvIndex = registeredIndex;
        }
    }

    if ( slot.uavIndex != UINT_MAX )
    {
        m_frame.ResourceRelease().RetireStaticDescriptor( slot.uavIndex );
    }

    if ( slot.rtvIndex != UINT_MAX && slot.dsvIndex != UINT_MAX && slot.resource )
    {
        // Each retirement record owns one resource reference and one CPU row.
        // Retain a second reference so even a malformed dual-view description
        // cannot leak either row or release the texture before both fences pass.
        slot.resource->AddRef();
        m_frame.ResourceRelease().Retire( slot.resource, srvIndex, Dx12CpuDescriptorKind::Rtv, slot.rtvIndex );
        m_frame.ResourceRelease().Retire( slot.resource, UINT_MAX, Dx12CpuDescriptorKind::Dsv, slot.dsvIndex );
    }
    else
    {
        const Dx12CpuDescriptorKind cpuKind = slot.rtvIndex != UINT_MAX
                                                    ? Dx12CpuDescriptorKind::Rtv
                                                    : ( slot.dsvIndex != UINT_MAX ? Dx12CpuDescriptorKind::Dsv
                                                                                 : Dx12CpuDescriptorKind::None );
        const UINT cpuIndex = slot.rtvIndex != UINT_MAX ? slot.rtvIndex : slot.dsvIndex;
        m_frame.ResourceRelease().Retire( slot.resource, srvIndex, cpuKind, cpuIndex );
    }

    slot = {};
}

RenderGraphTransientMaterializationStats Dx12GraphTransientPool::Materialize( const RenderGraph& graph,
                                                                              const RenderGraphCompileResult& compiled )
{
    // Concept: these are frame-target pool slots, not scene assets. The graph
    // compiler proves alias compatibility; this owner keeps that proof beside
    // the physical texture and every view row that represents it.
    m_stats = {};
    m_bindings.clear();

    for ( GraphTransientResourceDX12& slot : m_resources )
    {
        slot.usedThisCompile = false;
    }

    ID3D12Device* device = m_device.Device();

    if ( !device )
    {
        SB_FATAL( "Dx12GraphTransientPool", "Graph transient materialization requires an initialized device." );
    }

    for ( const RenderGraphTransientAllocationDesc& allocation : compiled.transientAllocations )
    {
        if ( allocation.resource.index >= graph.Resources().size() )
        {
            SB_FATAL( "Dx12GraphTransientPool",
                      "Graph transient allocation references an invalid resource. index=%u resourceCount=%zu",
                      allocation.resource.index, graph.Resources().size() );
        }

        const RenderGraphResourceDesc& resource = graph.Resources()[allocation.resource.index];
        const RenderGraphTransientResourceDesc& desc = resource.transient;

        if ( desc.kind != RenderGraphResourceKind::Texture2D )
        {
            SB_FATAL( "Dx12GraphTransientPool", "Graph transient materializer supports Texture2D resources only." );
        }

        if ( desc.format == RenderGraphResourceFormat::Unknown )
        {
            SB_FATAL( "Dx12GraphTransientPool", "Graph transient materializer requires a concrete format." );
        }

        if ( desc.descriptors.depthStencil && desc.descriptors.renderTarget )
        {
            SB_FATAL( "Dx12GraphTransientPool",
                      "Graph transient resources cannot combine depth-stencil and render-target descriptors." );
        }

        if ( desc.descriptors.depthStencil && desc.descriptors.unorderedAccess )
        {
            SB_FATAL( "Dx12GraphTransientPool", "Graph transient depth resources cannot request UAV descriptors." );
        }

        if ( allocation.firstPass >= graph.Passes().size() )
        {
            SB_FATAL( "Dx12GraphTransientPool",
                      "Transient allocation has an invalid first pass. resource=%s firstPass=%u passCount=%zu",
                      resource.name, allocation.firstPass, graph.Passes().size() );
        }

        RenderGraphResourceAccess firstAccess = RenderGraphResourceAccess::Unknown;
        const RenderGraphPassDesc& firstPass = graph.Passes()[allocation.firstPass];

        for ( const RenderGraphResourceUse& read : firstPass.reads )
        {
            if ( read.resource.index == allocation.resource.index )
            {
                firstAccess = read.access;
                break;
            }
        }

        if ( firstAccess == RenderGraphResourceAccess::Unknown )
        {
            for ( const RenderGraphResourceUse& write : firstPass.writes )
            {
                if ( write.resource.index == allocation.resource.index )
                {
                    firstAccess = write.access;
                    break;
                }
            }
        }

        if ( firstAccess == RenderGraphResourceAccess::Unknown )
        {
            SB_FATAL( "Dx12GraphTransientPool", "Transient allocation has no concrete first-pass access. resource=%s",
                      resource.name );
        }

        GraphTransientResourceDX12* slot = nullptr;
        bool appendedSlot = false;
        const GraphTransientPoolSlotSelectionDX12 selection =
            SelectGraphTransientPoolSlotDX12( m_resources, allocation.poolSlot, desc );

        if ( selection.found )
        {
            slot = &m_resources[selection.index];

            if ( selection.replaceResource )
            {
                // Lifetime: command lists from older frames may still name the
                // previous extent. Fence retirement owns that resource and its
                // descriptor rows while this stable vector slot is repopulated.
                RetireSlotForReplacement( *slot );
            }
            else
            {
                ++m_stats.reusedThisCompile;
            }
        }

        if ( !slot || selection.replaceResource )
        {
            // Runtime allocation exception: render-graph pool growth is a
            // warm-up materialization action. Created slots persist to shutdown;
            // steady frames reuse the compiler-assigned physical pool.
            if ( !slot )
            {
                m_resources.push_back( GraphTransientResourceDX12() );
                slot = &m_resources.back();
                appendedSlot = true;
            }

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
                textureDesc.Format = ToColorFormat( desc.format );

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

            const RenderGraphResourceAccess creationAccess =
                ResolveGraphTransientCreationAccessDX12( resource.initialAccess, firstAccess );
            D3D12_RESOURCE_STATES initialState = D3D12_RESOURCE_STATE_COMMON;

            if ( !TryDx12RenderGraphAccessToResourceState( creationAccess, initialState ) )
            {
                SB_FATAL( "Dx12GraphTransientPool",
                          "Graph transient creation requires a concrete DX12 initial state. resource=%s access=%s",
                          resource.name, ToString( creationAccess ) );
            }

            D3D12_HEAP_PROPERTIES defaultHeap = {};
            defaultHeap.Type = D3D12_HEAP_TYPE_DEFAULT;
            const HRESULT hr = device->CreateCommittedResource( &defaultHeap, D3D12_HEAP_FLAG_NONE, &textureDesc,
                                                                initialState, clearValuePtr,
                                                                IID_PPV_ARGS( &slot->resource ) );

            if ( FAILED( hr ) )
            {
                // Recoverable error: optional post-process passes may use their older
                // framebuffer path when native transient creation fails.
                MarkMaterializationFailure( m_stats, hr, resource );
                if ( appendedSlot )
                {
                    m_resources.pop_back();
                }

                m_stats.poolSize = m_resources.size();
                return m_stats;
            }

            NameDx12Object( slot->resource, L"Skullbonez DX12 RenderGraph Transient Texture" );
            // Invariant: Unknown is a logical declaration, never a physical
            // D3D12 state. A new resource begins in its first concrete access so
            // write-only states do not rely on illegal COMMON promotion.
            slot->currentAccess = creationAccess;

            if ( desc.descriptors.renderTarget )
            {
                const Dx12CpuDescriptorAllocation rtvAllocation = m_descriptors.AllocateRtv();
                slot->rtv = rtvAllocation.cpuHandle;
                slot->rtvIndex = rtvAllocation.index;
                device->CreateRenderTargetView( slot->resource, nullptr, slot->rtv );
            }

            if ( desc.descriptors.depthStencil )
            {
                const Dx12CpuDescriptorAllocation dsvAllocation = m_descriptors.AllocateDsv();
                slot->dsv = dsvAllocation.cpuHandle;
                slot->dsvIndex = dsvAllocation.index;
                D3D12_DEPTH_STENCIL_VIEW_DESC dsvDesc = {};
                dsvDesc.Format = DXGI_FORMAT_D24_UNORM_S8_UINT;
                dsvDesc.ViewDimension = D3D12_DSV_DIMENSION_TEXTURE2D;
                device->CreateDepthStencilView( slot->resource, &dsvDesc, slot->dsv );
            }

            if ( desc.descriptors.shaderResource )
            {
                slot->srvIndex = m_descriptors.AllocateStatic();
                D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};

                srvDesc.Format = ToSrvFormat( desc.format );
                srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
                srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
                srvDesc.Texture2D.MipLevels = desc.mipLevels;
                device->CreateShaderResourceView( slot->resource, &srvDesc,
                                                  m_descriptors.StagingCpuHandle( slot->srvIndex ) );

                m_descriptors.PublishStaticDescriptor( device, slot->srvIndex );
            }

            if ( desc.descriptors.unorderedAccess )
            {
                slot->uavIndex = m_descriptors.AllocateStatic();
                D3D12_UNORDERED_ACCESS_VIEW_DESC uavDesc = {};

                uavDesc.Format = ToColorFormat( desc.format );
                uavDesc.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2D;
                device->CreateUnorderedAccessView( slot->resource, nullptr, &uavDesc,
                                                   m_descriptors.StagingCpuHandle( slot->uavIndex ) );

                m_descriptors.PublishStaticDescriptor( device, slot->uavIndex );
            }

            ++m_stats.createdThisCompile;
        }

        std::snprintf( slot->resourceName, sizeof( slot->resourceName ), "%s",
                       ( resource.name && resource.name[0] != '\0' ) ? resource.name : "UnnamedGraphTransient" );

        if ( desc.descriptors.shaderResource && slot->textureHandle == 0 && slot->srvIndex != UINT_MAX )
        {
            slot->textureHandle = m_textures.RegisterSRV( slot->srvIndex, slot->resource );
        }

        slot->poolSlot = allocation.poolSlot;
        slot->firstPass = allocation.firstPass;
        slot->lastPass = allocation.lastPass;

        slot->usedThisCompile = true;
        m_bindings.push_back( { allocation.resource, static_cast<size_t>( slot - m_resources.data() ),
                                allocation.firstPass, firstAccess, false } );
    }

    m_stats.poolSize = m_resources.size();
    m_stats.releasedAtFrameEnd = compiled.transientDiagnostics.releaseCount;

    for ( const GraphTransientResourceDX12& slot : m_resources )
    {
        if ( slot.resource )
        {
            m_stats.descriptorRowsOwned += CountDescriptorRows( slot.desc.descriptors );
        }
    }

    SkullbonezCore::Core::Log()
        .WriteEventf( "dx12_graph_transient_materialize allocations=%zu pool_size=%zu created_this_compile=%zu "
                      "reused_this_compile=%zu descriptor_rows_owned=%zu released_at_frame_end=%zu",
                      compiled.transientAllocations.size(), m_stats.poolSize, m_stats.createdThisCompile,
                      m_stats.reusedThisCompile, m_stats.descriptorRowsOwned, m_stats.releasedAtFrameEnd );

    return m_stats;
}

GraphTransientResourceDX12* Dx12GraphTransientPool::FindSlot( RenderGraphResourceHandle resource )
{
    for ( const GraphTransientBindingDX12& binding : m_bindings )
    {
        if ( binding.resource.index == resource.index && binding.slotIndex < m_resources.size() )
        {
            return &m_resources[binding.slotIndex];
        }
    }

    return nullptr;
}

const GraphTransientResourceDX12* Dx12GraphTransientPool::FindSlot( RenderGraphResourceHandle resource ) const
{
    for ( const GraphTransientBindingDX12& binding : m_bindings )
    {
        if ( binding.resource.index == resource.index && binding.slotIndex < m_resources.size() )
        {
            return &m_resources[binding.slotIndex];
        }
    }

    return nullptr;
}

RenderGraphTextureBinding Dx12GraphTransientPool::Resolve( RenderGraphResourceHandle resource ) const
{
    const GraphTransientResourceDX12* slot = FindSlot( resource );

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

size_t Dx12GraphTransientPool::ExecuteTransitions( const RenderGraph& graph, const RenderGraphCompileResult& compiled,
                                                   uint32_t passIndex )
{
    if ( passIndex >= graph.Passes().size() )
    {
        SB_FATAL( "Dx12GraphTransientPool", "Graph transient transition requested an invalid pass. pass=%u passCount=%zu",
                  passIndex, graph.Passes().size() );
    }

    size_t emittedCount = 0;

    // A physical slot can back several non-overlapping logical resources in one
    // compile. Activate each logical lifetime from the physical state left by
    // its predecessor before consuming that resource's compiled transitions.
    for ( GraphTransientBindingDX12& binding : m_bindings )
    {
        if ( binding.activated || binding.firstPass != passIndex || binding.resource.index >= graph.Resources().size() ||
             binding.slotIndex >= m_resources.size() )
        {
            continue;
        }

        GraphTransientResourceDX12& slot = m_resources[binding.slotIndex];
        const RenderGraphResourceDesc& graphResource = graph.Resources()[binding.resource.index];
        const GraphTransientAliasActivationDX12 activation = PlanGraphTransientAliasActivationDX12(
            slot.currentAccess, graphResource.initialAccess, binding.firstAccess, slot.hasActivatedLifetime );

        if ( !activation.valid )
        {
            SB_FATAL( "Dx12GraphTransientPool",
                      "Transient alias activation requires concrete physical and target states. pass=%s resource=%s "
                      "physical=%s target=%s",
                      graph.Passes()[passIndex].name, graphResource.name, ToString( slot.currentAccess ),
                      ToString( activation.trackedAccess ) );
        }

        if ( activation.barrier != GraphTransientAliasBarrierDX12::None )
        {
            if ( !m_frame.CanRecord() && !m_frame.EnsureOpen().Ok() )
            {
                SB_FATAL( "Dx12GraphTransientPool",
                          "Transient alias activation could not open command recording. pass=%s resource=%s",
                          graph.Passes()[passIndex].name, graphResource.name );
            }

            if ( activation.barrier == GraphTransientAliasBarrierDX12::Transition )
            {
                Dx12RenderGraphSingleTransitionDesc desc;
                desc.commandList = m_frame.CommandList();
                desc.resource = slot.resource;
                desc.before = slot.currentAccess;
                desc.after = activation.trackedAccess;
                const Dx12RenderGraphBarrierRecord record = ExecuteDx12RenderGraphSingleTransition(
                    "Dx12GraphTransientAliasActivation", graph.Passes()[passIndex].name, graphResource.name, desc );

                if ( !record.hasConcreteStates || !record.hasNativeResource || record.missingCommandList ||
                     !record.emitted )
                {
                    SB_FATAL( "Dx12GraphTransientPool",
                              "Transient alias activation did not emit one concrete transition. pass=%s resource=%s",
                              graph.Passes()[passIndex].name, graphResource.name );
                }
            }
            else
            {
                Dx12RenderGraphUavBarrierDesc desc;
                desc.commandList = m_frame.CommandList();
                desc.resource = slot.resource;
                const Dx12RenderGraphUavBarrierRecord record = ExecuteDx12RenderGraphUavBarrier(
                    "Dx12GraphTransientAliasActivation", graph.Passes()[passIndex].name, graphResource.name, desc );

                if ( !record.hasNativeResource || record.missingCommandList || !record.emitted )
                {
                    SB_FATAL( "Dx12GraphTransientPool",
                              "Transient alias activation did not emit one UAV ordering barrier. pass=%s resource=%s",
                              graph.Passes()[passIndex].name, graphResource.name );
                }
            }

            ++emittedCount;
        }

        // The physical state was concrete at creation and remains concrete
        // across logical aliases. State changes use transition barriers; a
        // same-state UAV handoff uses an ordering barrier.
        slot.currentAccess = activation.trackedAccess;
        slot.hasActivatedLifetime = true;
        binding.activated = true;
    }

    emittedCount += DispatchCompiledUavBarriersForPass(
        graph, compiled, passIndex, false,
        [&]( const RenderGraphUavBarrierDesc& barrier, const RenderGraphResourceDesc& graphResource )
        {
            GraphTransientResourceDX12* slot = FindSlot( barrier.resource );

            if ( !slot || !slot->resource )
            {
                return false;
            }

            if ( slot->currentAccess != RenderGraphResourceAccess::UnorderedAccess )
            {
                SB_FATAL( "Dx12GraphTransientPool",
                          "Compiled transient UAV barrier has a non-UAV physical state. pass=%s resource=%s access=%s",
                          graph.Passes()[passIndex].name, graphResource.name, ToString( slot->currentAccess ) );
            }

            if ( !m_frame.CanRecord() && !m_frame.EnsureOpen().Ok() )
            {
                SB_FATAL( "Dx12GraphTransientPool",
                          "Compiled transient UAV barrier could not open command recording. pass=%s resource=%s",
                          graph.Passes()[passIndex].name, graphResource.name );
            }

            Dx12RenderGraphUavBarrierDesc desc;
            desc.commandList = m_frame.CommandList();
            desc.resource = slot->resource;
            const Dx12RenderGraphUavBarrierRecord record = ExecuteDx12RenderGraphUavBarrier(
                "Dx12GraphCompiledTransient", graph.Passes()[passIndex].name, graphResource.name, desc );

            if ( !record.hasNativeResource || record.missingCommandList || !record.emitted )
            {
                SB_FATAL( "Dx12GraphTransientPool",
                          "Compiled transient UAV ordering barrier was not emitted. pass=%s resource=%s",
                          graph.Passes()[passIndex].name, graphResource.name );
            }

            return true;
        } );

    for ( const RenderGraphTransitionDesc& transition : compiled.transitions )
    {
        if ( transition.passIndex != passIndex )
        {
            continue;
        }

        if ( transition.resource.index >= graph.Resources().size() )
        {
            SB_FATAL( "Dx12GraphTransientPool",
                      "Compiled transition references an invalid resource. resource=%u resourceCount=%zu",
                      transition.resource.index, graph.Resources().size() );
        }

        const RenderGraphResourceDesc& graphResource = graph.Resources()[transition.resource.index];

        if ( graphResource.external )
        {
            continue;
        }

        GraphTransientResourceDX12* slot = FindSlot( transition.resource );

        if ( !slot || !slot->resource )
        {
            // Recoverable error: materialization already logged the allocation failure.
            // The callback disables that optional effect, so there is no native
            // resource and therefore no barrier to emit for this logical edge.
            continue;
        }

        if ( slot->currentAccess != transition.before )
        {
            // Hazard: accepting a mismatch would make the graph's StateBefore
            // claim disagree with the actual physical slot, which DX12 treats
            // as undefined command-stream state rather than a recoverable miss.
            SB_FATAL( "Dx12GraphTransientPool",
                      "Compiled transient state disagrees with the physical slot. pass=%s resource=%s tracked=%s "
                      "compiled=%s",
                      graph.Passes()[passIndex].name, graphResource.name, ToString( slot->currentAccess ),
                      ToString( transition.before ) );
        }

        if ( !m_frame.CanRecord() && !m_frame.EnsureOpen().Ok() )
        {
            SB_FATAL( "Dx12GraphTransientPool",
                      "Compiled transient transition could not open command recording. pass=%s resource=%s",
                      graph.Passes()[passIndex].name, graphResource.name );
        }

        Dx12RenderGraphSingleTransitionDesc desc;
        desc.commandList = m_frame.CommandList();
        desc.resource = slot->resource;
        desc.before = transition.before;
        desc.after = transition.after;
        desc.subresource = static_cast<UINT>( transition.subresource );
        const Dx12RenderGraphBarrierRecord record = ExecuteDx12RenderGraphSingleTransition( "Dx12GraphCompiledTransient",
                                                                                            graph.Passes()[passIndex].name,
                                                                                            graphResource.name, desc );

        if ( !record.hasConcreteStates || !record.hasNativeResource || record.missingCommandList ||
             record.beforeState == record.afterState || !record.emitted )
        {
            SB_FATAL( "Dx12GraphTransientPool",
                      "Compiled graph transition did not emit one concrete barrier. pass=%s resource=%s",
                      graph.Passes()[passIndex].name, graphResource.name );
        }

        slot->currentAccess = transition.after;
        ++emittedCount;
    }

    return emittedCount;
}

void Dx12GraphTransientPool::BeginRenderTarget( const RenderGraphTextureBinding& binding, const char* passName )
{
    if ( m_renderTargetActive )
    {
        SB_FATAL( "Dx12GraphTransientPool", "Graph transient render target is already active." );
    }

    if ( !binding.IsValid() || !binding.renderTarget )
    {
        SB_FATAL( "Dx12GraphTransientPool",
                  "Graph transient render target binding is invalid. textureHandle=%u renderTarget=%d",
                  binding.textureHandle, binding.renderTarget ? 1 : 0 );
    }

    GraphTransientResourceDX12* slot = FindSlot( binding.resource );

    if ( !slot || !slot->resource || slot->rtv.ptr == 0 )
    {
        SB_FATAL( "Dx12GraphTransientPool", "Graph transient render target was not materialized." );
    }

    // Lifetime: one balanced callback interval borrows the current output state.
    // No saved target escapes this owner or survives the matching End call.
    // Invariant: ExecuteTransitions must consume the compiled producer edge
    // before binding. Begin only changes descriptors/targets; it emits no barrier.
    if ( slot->currentAccess != RenderGraphResourceAccess::RenderTarget )
    {
        SB_FATAL( "Dx12GraphTransientPool",
                  "Graph transient target bound before its compiled transition. pass=%s resource=%s access=%s",
                  passName ? passName : "unknown", slot->resourceName, ToString( slot->currentAccess ) );
    }

    m_savedRtv = m_pipeline.CurrentRTV();
    m_savedDsv = m_pipeline.CurrentDSV();
    m_savedRtvFormat = m_pipeline.RenderTargetFormat();
    m_pipeline.SetRenderingToFBO( true, ToColorFormat( slot->desc.format ) );
    m_textures.ClearBoundSlotsForSrv( slot->srvIndex );
    m_pipeline.SetCurrentTargets( slot->rtv, m_savedDsv );
    m_renderTargetActive = true;
    m_activeRenderTarget = binding.resource;
}

void Dx12GraphTransientPool::EndRenderTarget( const RenderGraphTextureBinding& binding, const char* passName )
{
    if ( !m_renderTargetActive || m_activeRenderTarget.index != binding.resource.index )
    {
        SB_FATAL( "Dx12GraphTransientPool",
                  "Graph transient render target end does not match active binding. active=%u requested=%u",
                  m_activeRenderTarget.index, binding.resource.index );
    }

    GraphTransientResourceDX12* slot = FindSlot( binding.resource );

    if ( !slot || !slot->resource )
    {
        SB_FATAL( "Dx12GraphTransientPool", "Graph transient render target was lost before unbind." );
    }

    // End restores the caller's target but deliberately leaves the texture in
    // RenderTarget state. The next consuming graph pass owns the compiled
    // RenderTarget -> PixelShaderResource transition immediately before use.
    if ( slot->currentAccess != RenderGraphResourceAccess::RenderTarget )
    {
        SB_FATAL( "Dx12GraphTransientPool",
                  "Graph transient target ended after an unexpected transition. pass=%s resource=%s access=%s",
                  passName ? passName : "unknown", slot->resourceName, ToString( slot->currentAccess ) );
    }

    m_pipeline.SetRenderingToFBO( false, DXGI_FORMAT_R8G8B8A8_UNORM );
    m_pipeline.SetCurrentTargets( m_savedRtv, m_savedDsv );
    m_pipeline.RestoreRenderTargetFormat( m_savedRtvFormat );
    m_renderTargetActive = false;
    m_activeRenderTarget = {};
}

void Dx12GraphTransientPool::ReleaseAfterTerminalDrain( const char* reason )
{
    size_t released = 0;

    for ( GraphTransientResourceDX12& slot : m_resources )
    {
        UINT srvIndex = slot.srvIndex;

        if ( slot.textureHandle != 0 )
        {
            const UINT registeredIndex = m_textures.UnregisterSRV( slot.textureHandle );

            if ( registeredIndex != UINT_MAX )
            {
                srvIndex = registeredIndex;
            }

            slot.textureHandle = 0;
        }

        if ( srvIndex != UINT_MAX )
        {
            m_frame.ResourceRelease().RetireStaticDescriptor( srvIndex );
            slot.srvIndex = UINT_MAX;
        }

        if ( slot.uavIndex != UINT_MAX )
        {
            m_frame.ResourceRelease().RetireStaticDescriptor( slot.uavIndex );
            slot.uavIndex = UINT_MAX;
        }

        if ( slot.rtvIndex != UINT_MAX )
        {
            m_descriptors.FreeCpu( Dx12CpuDescriptorKind::Rtv, slot.rtvIndex );
        }

        if ( slot.dsvIndex != UINT_MAX )
        {
            m_descriptors.FreeCpu( Dx12CpuDescriptorKind::Dsv, slot.dsvIndex );
        }

        if ( ReleaseGraphTransientPoolSlotResourceDX12( slot ) )
        {
            ++released;
        }
    }

    m_resources.clear();
    m_bindings.clear();
    m_stats = {};

    m_renderTargetActive = false;
    m_activeRenderTarget = {};

    SkullbonezCore::Core::Log().WriteEventf( "dx12_graph_transient_release reason=%s released_resources=%zu",
                                             reason ? reason : "unknown", released );
}
