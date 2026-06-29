/*
File: SkullbonezSource/Rendering/DX12/RenderGraphTransientDX12.h
Purpose:
  Names DX12-side materialized render-graph transient resource records.

Mental model:
  The render graph compiler owns logical lifetimes. The DX12 backend turns each
  compatible pool slot into one physical texture plus the descriptors needed to
  write or sample it.

Glossary:
  Graph transient: A frame-local graph resource that is created by the backend,
  used by one or more graph passes, and reusable by later compatible lifetimes.
  Pool slot: Compiler-assigned alias bucket for non-overlapping transient
  lifetimes with matching descriptor needs.
  SRV/RTV/DSV/UAV: DX12 descriptor row types for shader reads, color writes,
  depth/stencil writes, and unordered writes.

Invariants:
  - Pool-slot reuse is legal even when a previous logical transient already
    used the physical slot in the same compile.
  - Descriptor compatibility must match before a physical texture can satisfy a
    logical transient allocation.

Related:
  - SkullbonezSource/Rendering/DX12/RenderBackendDX12.cpp
  - SkullbonezSource/Rendering/RenderGraph.h
  - Agentic/Reference/comment-style-guide.md
*/
#pragma once

#include "../RenderGraph.h"

#include <d3d12.h>
#include <cstddef>
#include <cstdint>

namespace SkullbonezCore
{
namespace Rendering
{

struct GraphTransientResourceDX12
{
    ID3D12Resource* resource = nullptr;
    RenderGraphTransientResourceDesc desc;
    D3D12_CPU_DESCRIPTOR_HANDLE rtv = {};
    D3D12_CPU_DESCRIPTOR_HANDLE dsv = {};
    UINT srvIndex = UINT_MAX;
    UINT uavIndex = UINT_MAX;
    uint32_t textureHandle = 0;
    char resourceName[64] = {};
    uint32_t poolSlot = 0;
    uint32_t firstPass = 0;
    uint32_t lastPass = 0;
    RenderGraphResourceAccess currentAccess = RenderGraphResourceAccess::Unknown;
    bool usedThisCompile = false;
};

struct GraphTransientBindingDX12
{
    RenderGraphResourceHandle resource;
    size_t slotIndex = 0;
};

using GraphTransientMaterializationStatsDX12 = RenderGraphTransientMaterializationStats;

inline bool GraphTransientDescEqualDX12( const RenderGraphTransientResourceDesc& lhs,
                                         const RenderGraphTransientResourceDesc& rhs )
{
    return lhs.kind == rhs.kind && lhs.format == rhs.format && lhs.width == rhs.width && lhs.height == rhs.height &&
           lhs.mipLevels == rhs.mipLevels && lhs.descriptors.renderTarget == rhs.descriptors.renderTarget &&
           lhs.descriptors.depthStencil == rhs.descriptors.depthStencil &&
           lhs.descriptors.shaderResource == rhs.descriptors.shaderResource &&
           lhs.descriptors.unorderedAccess == rhs.descriptors.unorderedAccess;
}

inline bool GraphTransientPoolSlotCanSatisfyDX12( const GraphTransientResourceDX12& candidate,
                                                  uint32_t poolSlot,
                                                  const RenderGraphTransientResourceDesc& desc )
{
    return candidate.resource && candidate.poolSlot == poolSlot && GraphTransientDescEqualDX12( candidate.desc, desc );
}

} // namespace Rendering
} // namespace SkullbonezCore
