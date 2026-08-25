/*
File: SkullbonezSource/Rendering/DX12/RenderGraphTransientDX12.h
Purpose:
  Names DX12-side materialized render-graph transient resource records.

Summary:
  The render graph compiler owns logical lifetimes. The DX12 backend turns each
  compatible pool slot into one physical texture plus the descriptors needed to
  write or sample it.

Glossary:
  Graph transient: A frame-local graph resource that is created by the backend,
  used by one or more graph passes, and reusable by later compatible lifetimes.

Invariants:
  - Pool-slot reuse is legal even when a previous logical transient already
    used the physical slot in the same compile.
  - Descriptor compatibility must match before a physical texture can satisfy a
    logical transient allocation.

Related:
  - SkullbonezSource/Rendering/DX12/Dx12GraphTransientPool.cpp
  - SkullbonezSource/Rendering/RenderGraph.h
  - Agentic/Reference/engine-glossary.md
*/
#pragma once

#include "../RenderGraph.h"

#include <d3d12.h>
#include <cstddef>
#include <cstdint>
#include <span>

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
    UINT rtvIndex = UINT_MAX;
    UINT dsvIndex = UINT_MAX;
    UINT srvIndex = UINT_MAX;
    UINT uavIndex = UINT_MAX;
    uint32_t textureHandle = 0;
    char resourceName[64] = {};
    uint32_t poolSlot = 0;
    uint32_t firstPass = 0;
    uint32_t lastPass = 0;
    RenderGraphResourceAccess currentAccess = RenderGraphResourceAccess::Unknown;
    bool usedThisCompile = false;
    bool hasActivatedLifetime = false;
};

inline bool ReleaseGraphTransientPoolSlotResourceDX12( GraphTransientResourceDX12& slot )
{
    if ( !slot.resource )
    {
        return false;
    }

    // Lifetime: native texture release belongs to the graph transient pool slot,
    // not to material/object texture ownership. The caller unregisters any engine
    // texture handle first because the texture owner owns that mapping;
    // this helper then retires only the DX12 resource and descriptor identities
    // cached on the physical pool slot.
    slot.resource->Release();
    slot.resource = nullptr;
    slot.rtv = {};
    slot.dsv = {};
    slot.rtvIndex = UINT_MAX;
    slot.dsvIndex = UINT_MAX;
    slot.srvIndex = UINT_MAX;
    slot.uavIndex = UINT_MAX;
    slot.resourceName[0] = '\0';
    slot.currentAccess = RenderGraphResourceAccess::Unknown;
    slot.usedThisCompile = false;
    slot.hasActivatedLifetime = false;
    return true;
}

struct GraphTransientBindingDX12
{
    RenderGraphResourceHandle resource;
    size_t slotIndex = 0;
    uint32_t firstPass = 0;
    RenderGraphResourceAccess firstAccess = RenderGraphResourceAccess::Unknown;
    bool activated = false;
};

using GraphTransientMaterializationStatsDX12 = RenderGraphTransientMaterializationStats;

enum class GraphTransientAliasBarrierDX12 : uint8_t
{
    None,
    Transition,
    UnorderedAccess
};

struct GraphTransientAliasActivationDX12
{
    RenderGraphResourceAccess trackedAccess = RenderGraphResourceAccess::Unknown;
    GraphTransientAliasBarrierDX12 barrier = GraphTransientAliasBarrierDX12::None;
    bool valid = false;
};

inline RenderGraphResourceAccess ResolveGraphTransientCreationAccessDX12(
    RenderGraphResourceAccess logicalInitialAccess, RenderGraphResourceAccess firstUseAccess ) noexcept
{
    return logicalInitialAccess == RenderGraphResourceAccess::Unknown ? firstUseAccess : logicalInitialAccess;
}

inline GraphTransientAliasActivationDX12
PlanGraphTransientAliasActivationDX12( RenderGraphResourceAccess physicalAccess,
                                       RenderGraphResourceAccess logicalInitialAccess,
                                       RenderGraphResourceAccess firstUseAccess,
                                       bool hasPriorLogicalLifetime ) noexcept
{
    const RenderGraphResourceAccess targetAccess =
        ResolveGraphTransientCreationAccessDX12( logicalInitialAccess, firstUseAccess );

    GraphTransientAliasActivationDX12 plan;
    plan.trackedAccess = targetAccess;
    plan.valid = physicalAccess != RenderGraphResourceAccess::Unknown &&
                 targetAccess != RenderGraphResourceAccess::Unknown;

    if ( !plan.valid )
    {
        return plan;
    }

    if ( physicalAccess != targetAccess )
    {
        plan.barrier = GraphTransientAliasBarrierDX12::Transition;
    }
    else if ( hasPriorLogicalLifetime && targetAccess == RenderGraphResourceAccess::UnorderedAccess )
    {
        // Hazard: a new logical owner does not inherit completion of UAV writes
        // merely because it reuses the same physical texture and state.
        plan.barrier = GraphTransientAliasBarrierDX12::UnorderedAccess;
    }

    return plan;
}

inline bool GraphTransientDescEqualDX12( const RenderGraphTransientResourceDesc& lhs,
                                         const RenderGraphTransientResourceDesc& rhs )
{
    return lhs.kind == rhs.kind && lhs.format == rhs.format && lhs.width == rhs.width && lhs.height == rhs.height &&
           lhs.mipLevels == rhs.mipLevels && lhs.descriptors.renderTarget == rhs.descriptors.renderTarget &&
           lhs.descriptors.depthStencil == rhs.descriptors.depthStencil &&
           lhs.descriptors.shaderResource == rhs.descriptors.shaderResource &&
           lhs.descriptors.unorderedAccess == rhs.descriptors.unorderedAccess;
}

inline bool GraphTransientPoolSlotCanSatisfyDX12( const GraphTransientResourceDX12& candidate, uint32_t poolSlot,
                                                  const RenderGraphTransientResourceDesc& desc )
{
    return candidate.resource && candidate.poolSlot == poolSlot && GraphTransientDescEqualDX12( candidate.desc, desc );
}

struct GraphTransientPoolSlotSelectionDX12
{
    size_t index = 0;
    bool found = false;
    bool replaceResource = false;
};

inline GraphTransientPoolSlotSelectionDX12
SelectGraphTransientPoolSlotDX12( std::span<const GraphTransientResourceDX12> candidates, uint32_t poolSlot,
                                 const RenderGraphTransientResourceDesc& desc )
{
    for ( size_t index = 0; index < candidates.size(); ++index )
    {
        if ( GraphTransientPoolSlotCanSatisfyDX12( candidates[index], poolSlot, desc ) )
        {
            return { index, true, false };
        }
    }

    for ( size_t index = 0; index < candidates.size(); ++index )
    {
        const GraphTransientResourceDX12& candidate = candidates[index];

        if ( !candidate.usedThisCompile && candidate.resource && GraphTransientDescEqualDX12( candidate.desc, desc ) )
        {
            return { index, true, false };
        }
    }

    for ( size_t index = 0; index < candidates.size(); ++index )
    {
        if ( !candidates[index].usedThisCompile )
        {
            return { index, true, true };
        }
    }

    return {};
}

} // namespace Rendering
} // namespace SkullbonezCore
