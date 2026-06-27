/*
File: SkullbonezSource/Rendering/DX12/Dx12RenderGraphExecutor.h
Purpose:
  Translates render graph resource access records into DX12 barrier candidates.

Mental model:
  The render graph speaks in engine resource access terms. This executor is the
  DX12 edge where those access terms become D3D12_RESOURCE_STATES and, in live
  mode, ResourceBarrier calls.

Glossary:
  UAV (Unordered Access View): Descriptor row used when compute or raytracing
  shaders write textures or buffers.
  Resource barrier: DX12 synchronization command that transitions or orders GPU
  resource use.

Invariants:
  - DryRun records barrier candidates only; it must never call ResourceBarrier.
  - EmitBarriers requires both a command list and native resource identity before
    a transition can be emitted.
  - UAV access reports ordering-review cases separately from concrete transition
    barriers.

Related:
  - SkullbonezSource/Rendering/DX12/Dx12RenderGraphExecutor.cpp
  - SkullbonezSource/Rendering/RenderGraph.h
*/
#pragma once

#include "../RenderGraph.h"

#include <d3d12.h>
#include <cstddef>
#include <string>
#include <vector>

namespace SkullbonezCore
{
namespace Rendering
{

enum class Dx12RenderGraphExecutionMode
{
    DryRun,
    EmitBarriers
};

struct Dx12RenderGraphExecutionDesc
{
    Dx12RenderGraphExecutionMode mode = Dx12RenderGraphExecutionMode::DryRun;
    ID3D12GraphicsCommandList* commandList = nullptr;
    const char* sourcePrefix = "GraphDryRun";
};

struct Dx12RenderGraphBarrierRecord
{
    std::string source;
    std::string passName;
    std::string resourceName;
    const void* nativeResource = nullptr;
    RenderGraphResourceAccess beforeAccess = RenderGraphResourceAccess::Unknown;
    RenderGraphResourceAccess afterAccess = RenderGraphResourceAccess::Unknown;
    D3D12_RESOURCE_STATES beforeState = D3D12_RESOURCE_STATE_COMMON;
    D3D12_RESOURCE_STATES afterState = D3D12_RESOURCE_STATE_COMMON;
    UINT subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    bool hasConcreteStates = false;
    bool hasNativeResource = false;
    bool requiresUavOrderingReview = false;
    bool emitted = false;
};

struct Dx12RenderGraphExecutionResult
{
    std::vector<Dx12RenderGraphBarrierRecord> barriers;
    size_t transitionBarrierCount = 0;
    size_t emittedTransitionBarrierCount = 0;
    size_t skippedSameStateCount = 0;
    size_t unknownStateTransitionCount = 0;
    size_t missingNativeResourceTransitionCount = 0;
    size_t missingCommandListEmissionCount = 0;
    size_t uavAccessTransitionCount = 0;
};

struct Dx12RenderGraphSingleTransitionDesc
{
    ID3D12GraphicsCommandList* commandList = nullptr;
    ID3D12Resource* resource = nullptr;
    RenderGraphResourceAccess before = RenderGraphResourceAccess::Unknown;
    RenderGraphResourceAccess after = RenderGraphResourceAccess::Unknown;
    UINT subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
};

struct Dx12RenderGraphSingleTransitionResult
{
    D3D12_RESOURCE_STATES beforeState = D3D12_RESOURCE_STATE_COMMON;
    D3D12_RESOURCE_STATES afterState = D3D12_RESOURCE_STATE_COMMON;
    bool hasConcreteStates = false;
    bool hasNativeResource = false;
    bool missingCommandList = false;
    bool skippedSameState = false;
    bool requiresUavOrderingReview = false;
    bool emitted = false;
};

struct Dx12RenderGraphUavBarrierDesc
{
    ID3D12GraphicsCommandList* commandList = nullptr;
    ID3D12Resource* resource = nullptr;
};

struct Dx12RenderGraphUavBarrierResult
{
    bool hasNativeResource = false;
    bool missingCommandList = false;
    bool emitted = false;
};

bool TryDx12RenderGraphAccessToResourceState( RenderGraphResourceAccess access, D3D12_RESOURCE_STATES& outState );
std::string Dx12ResourceStateToString( D3D12_RESOURCE_STATES state );
Dx12RenderGraphSingleTransitionResult
EmitDx12RenderGraphTransitionBarrier( const Dx12RenderGraphSingleTransitionDesc& desc );
Dx12RenderGraphUavBarrierResult EmitDx12RenderGraphUavBarrier( const Dx12RenderGraphUavBarrierDesc& desc );

Dx12RenderGraphExecutionResult ExecuteDx12RenderGraphTransitions( const RenderGraph& graph,
                                                                  const RenderGraphCompileResult& compiled,
                                                                  const Dx12RenderGraphExecutionDesc& desc );

} // namespace Rendering
} // namespace SkullbonezCore
