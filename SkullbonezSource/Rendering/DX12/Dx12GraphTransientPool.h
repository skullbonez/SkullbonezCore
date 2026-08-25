/*
File: SkullbonezSource/Rendering/DX12/Dx12GraphTransientPool.h
Purpose:
  Owns materialized DX12 render-graph transient textures and target transactions.

Summary:
  The render graph compiler assigns logical lifetimes to reusable pool slots.
  This concrete owner materializes those slots, owns their descriptors and
  logical bindings, emits only compiler-selected state transitions, and
  saves/restores raster output state while callbacks draw into a graph texture.

Glossary:
  Logical binding: Mapping from a graph resource handle to one physical slot.
  Target transaction: Balanced Begin/End interval that temporarily replaces the
    pipeline owner's color target.

Invariants:
  - Pool state and the active target transaction live only in this owner.
  - Descriptor operations borrow Dx12DescriptorHeaps, never raw heap pointers.
  - Target Begin/End changes bindings only; ExecuteTransitions is the sole
    transient barrier authority.
  - A target transaction restores the exact RTV, DSV, and RTV format it saved.
  - Unused physical slots are recycled across compile/extent changes; retired
    resources and descriptor rows remain fence-owned by Dx12FrameOwner.
  - Shutdown releases the pool only after the frame owner proves terminal drain.

Related:
  - SkullbonezSource/Rendering/DX12/Dx12GraphTransientPool.cpp
  - SkullbonezSource/Rendering/DX12/RenderGraphTransientDX12.h
  - SkullbonezSource/Rendering/RenderGraph.h
  - Agentic/Reference/engine-glossary.md
*/
#pragma once

#include "RenderGraphTransientDX12.h"

#include <cstddef>
#include <vector>

namespace SkullbonezCore
{
namespace Rendering
{
class Dx12DescriptorHeaps;
class Dx12FrameOwner;
class Dx12PipelineOwner;
class Dx12RenderDevice;
class Dx12TextureOwner;

class Dx12GraphTransientPool
{
  public:
    Dx12GraphTransientPool( Dx12RenderDevice& device, Dx12DescriptorHeaps& descriptors, Dx12FrameOwner& frame,
                            Dx12TextureOwner& textures, Dx12PipelineOwner& pipeline );

    RenderGraphTransientMaterializationStats Materialize( const RenderGraph& graph,
                                                          const RenderGraphCompileResult& compiled );
    RenderGraphTextureBinding Resolve( RenderGraphResourceHandle resource ) const;
    size_t ExecuteTransitions( const RenderGraph& graph, const RenderGraphCompileResult& compiled, uint32_t passIndex );
    void BeginRenderTarget( const RenderGraphTextureBinding& binding, const char* passName );
    void EndRenderTarget( const RenderGraphTextureBinding& binding, const char* passName );
    void ReleaseAfterTerminalDrain( const char* reason );

    // Concept: the graph executor owns both transient and external-resource
    // transition publication. Runtime passes borrow this concrete owner rather
    // than a union command facade spanning unrelated draw categories.
    RenderGraphTransientMaterializationStats MaterializeGraphTransientResources( const RenderGraph& graph,
                                                                                 const RenderGraphCompileResult& compiled );
    RenderGraphTextureBinding ResolveGraphTextureBinding( RenderGraphResourceHandle resource ) const;
    RenderGraphNativeResourceToken ResolveGraphResourceToken( uint32_t textureHandle ) const;
    RenderGraphBackbufferBinding ResolveGraphBackbufferBinding() const;
    size_t ExecuteGraphTransitions( const RenderGraph& graph, const RenderGraphCompileResult& compiled, uint32_t passIndex );
    void BeginGraphTextureRenderTarget( const RenderGraphTextureBinding& binding, const char* passName );
    void EndGraphTextureRenderTarget( const RenderGraphTextureBinding& binding, const char* passName );

    size_t Size() const
    {
        return m_resources.size();
    }
    size_t Capacity() const
    {
        return m_resources.capacity();
    }

  private:
    void RetireSlotForReplacement( GraphTransientResourceDX12& slot );
    GraphTransientResourceDX12* FindSlot( RenderGraphResourceHandle resource );
    const GraphTransientResourceDX12* FindSlot( RenderGraphResourceHandle resource ) const;
    Dx12RenderDevice& m_device;
    Dx12DescriptorHeaps& m_descriptors;
    Dx12FrameOwner& m_frame;
    Dx12TextureOwner& m_textures;
    Dx12PipelineOwner& m_pipeline;
    std::vector<GraphTransientResourceDX12> m_resources;
    std::vector<GraphTransientBindingDX12> m_bindings;
    GraphTransientMaterializationStatsDX12 m_stats;
    bool m_renderTargetActive = false;
    RenderGraphResourceHandle m_activeRenderTarget;
    D3D12_CPU_DESCRIPTOR_HANDLE m_savedRtv = {};
    D3D12_CPU_DESCRIPTOR_HANDLE m_savedDsv = {};
    DXGI_FORMAT m_savedRtvFormat = DXGI_FORMAT_R8G8B8A8_UNORM;
};
} // namespace Rendering
} // namespace SkullbonezCore
