/*
File: SkullbonezSource/Rendering/DX12/Dx12RenderGraphExecutor.cpp
Purpose:
  Translates render graph resource access records into DX12 barrier candidates.

Summary:
  The graph compiler decides that a resource use changes from access A to
  access B. This file translates A and B into DX12 states and optionally emits
  the concrete transition barrier.

Glossary:
  Barrier: Command-list operation that orders GPU work or transitions a
  resource between states.

Invariants:
  - Unknown graph access is never translated to a fake COMMON transition.
  - Dry-run mode records candidates only; it does not call ResourceBarrier().
  - Live emission requires exact native resource identity.

Related:
  - SkullbonezSource/Rendering/DX12/Dx12RenderGraphExecutor.h
  - SkullbonezSource/Rendering/RenderGraph.cpp
  - Agentic/Reference/engine-glossary.md
*/
#include "Dx12RenderGraphExecutor.h"

#include <cstdio>
#include <sstream>

namespace SkullbonezCore
{
namespace Rendering
{
namespace
{

void AppendDx12StateFlag( std::ostringstream& out, bool& wroteAny, D3D12_RESOURCE_STATES state, D3D12_RESOURCE_STATES flag,
                          const char* name )
{

    if ( ( state & flag ) != 0 )
    {

        if ( wroteAny )
        {
            out << "|";
        }

        out << name;
        wroteAny = true;
    }
}

template <size_t N> void CopyLabel( char ( &destination )[N], const char* value )
{
    snprintf( destination, N, "%s", ( value && value[0] != '\0' ) ? value : "unknown" );
}

template <size_t N> void MakeBarrierSource( char ( &destination )[N], const char* prefix, const char* passName )
{
    snprintf( destination, N, "%s:%s", ( prefix && prefix[0] != '\0' ) ? prefix : "Graph",
              ( passName && passName[0] != '\0' ) ? passName : "UnnamedPass" );
}

template <size_t N> void MakeBarrierSource( char ( &destination )[N], const char* prefix, const RenderGraphPassDesc& pass )
{
    MakeBarrierSource( destination, prefix, pass.name );
}

Dx12RenderGraphBarrierRecord MakeSingleTransitionRecord( const char* sourcePrefix, const char* passName,
                                                         const char* resourceName,
                                                         const Dx12RenderGraphSingleTransitionDesc& desc )
{
    Dx12RenderGraphBarrierRecord record;
    MakeBarrierSource( record.source, sourcePrefix, passName );
    CopyLabel( record.passName, passName );
    CopyLabel( record.resourceName, resourceName );
    record.nativeResource = desc.resource;
    record.beforeAccess = desc.before;
    record.afterAccess = desc.after;
    record.subresource = desc.subresource;
    record.hasNativeResource = desc.resource != nullptr;
    record.hasConcreteStates = TryDx12RenderGraphAccessToResourceState( desc.before, record.beforeState ) &&
                               TryDx12RenderGraphAccessToResourceState( desc.after, record.afterState );

    record.requiresUavOrderingReview = desc.before == RenderGraphResourceAccess::UnorderedAccess ||
                                       desc.after == RenderGraphResourceAccess::UnorderedAccess;

    return record;
}

} // namespace


bool TryDx12RenderGraphAccessToResourceState( RenderGraphResourceAccess access, D3D12_RESOURCE_STATES& outState )
{

    switch ( access )
    {
    case RenderGraphResourceAccess::RenderTarget:
        outState = D3D12_RESOURCE_STATE_RENDER_TARGET;
        return true;
    case RenderGraphResourceAccess::DepthRead:
        outState = D3D12_RESOURCE_STATE_DEPTH_READ;
        return true;
    case RenderGraphResourceAccess::DepthWrite:
        outState = D3D12_RESOURCE_STATE_DEPTH_WRITE;
        return true;
    case RenderGraphResourceAccess::ShaderResource:
        outState = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE | D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
        return true;
    case RenderGraphResourceAccess::PixelShaderResource:
        outState = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
        return true;
    case RenderGraphResourceAccess::NonPixelShaderResource:
        outState = D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
        return true;
    case RenderGraphResourceAccess::UnorderedAccess:
        outState = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
        return true;
    case RenderGraphResourceAccess::CopySource:
        outState = D3D12_RESOURCE_STATE_COPY_SOURCE;
        return true;
    case RenderGraphResourceAccess::CopyDest:
        outState = D3D12_RESOURCE_STATE_COPY_DEST;
        return true;
    case RenderGraphResourceAccess::VertexAndNonPixelShaderResource:
        outState = D3D12_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER | D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
        return true;
    case RenderGraphResourceAccess::Present:
        outState = D3D12_RESOURCE_STATE_PRESENT;
        return true;
    case RenderGraphResourceAccess::Unknown:
    default:
        outState = D3D12_RESOURCE_STATE_COMMON;
        return false;
    }
}


Dx12RenderGraphSingleTransitionResult EmitDx12RenderGraphTransitionBarrier( const Dx12RenderGraphSingleTransitionDesc& desc )
{
    Dx12RenderGraphSingleTransitionResult result;
    result.hasNativeResource = desc.resource != nullptr;
    result.hasConcreteStates = TryDx12RenderGraphAccessToResourceState( desc.before, result.beforeState ) &&
                               TryDx12RenderGraphAccessToResourceState( desc.after, result.afterState );

    result.requiresUavOrderingReview = desc.before == RenderGraphResourceAccess::UnorderedAccess ||
                                       desc.after == RenderGraphResourceAccess::UnorderedAccess;

    if ( !result.hasNativeResource || !result.hasConcreteStates )
    {
        return result;
    }

    if ( result.beforeState == result.afterState )
    {
        result.skippedSameState = true;
        return result;
    }

    if ( !desc.commandList )
    {
        result.missingCommandList = true;
        return result;
    }

    D3D12_RESOURCE_BARRIER barrier = {};
    barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    barrier.Transition.pResource = desc.resource;
    barrier.Transition.StateBefore = result.beforeState;
    barrier.Transition.StateAfter = result.afterState;
    barrier.Transition.Subresource = desc.subresource;
    desc.commandList->ResourceBarrier( 1, &barrier );
    result.emitted = true;
    return result;
}


Dx12RenderGraphBarrierRecord ExecuteDx12RenderGraphSingleTransition( const char* sourcePrefix, const char* passName,
                                                                     const char* resourceName,
                                                                     const Dx12RenderGraphSingleTransitionDesc& desc )
{
    Dx12RenderGraphBarrierRecord record = MakeSingleTransitionRecord( sourcePrefix, passName, resourceName, desc );
    const Dx12RenderGraphSingleTransitionResult result = EmitDx12RenderGraphTransitionBarrier( desc );
    record.beforeState = result.beforeState;
    record.afterState = result.afterState;
    record.hasConcreteStates = result.hasConcreteStates;
    record.hasNativeResource = result.hasNativeResource;
    record.missingCommandList = result.missingCommandList;
    record.requiresUavOrderingReview = result.requiresUavOrderingReview;
    record.emitted = result.emitted;
    return record;
}


Dx12RenderGraphUavBarrierResult EmitDx12RenderGraphUavBarrier( const Dx12RenderGraphUavBarrierDesc& desc )
{
    Dx12RenderGraphUavBarrierResult result;
    result.hasNativeResource = desc.resource != nullptr;

    if ( !result.hasNativeResource )
    {
        return result;
    }

    if ( !desc.commandList )
    {
        result.missingCommandList = true;
        return result;
    }

    D3D12_RESOURCE_BARRIER barrier = {};
    barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_UAV;
    barrier.UAV.pResource = desc.resource;
    desc.commandList->ResourceBarrier( 1, &barrier );
    result.emitted = true;
    return result;
}


Dx12RenderGraphUavBarrierRecord ExecuteDx12RenderGraphUavBarrier( const char* sourcePrefix, const char* passName,
                                                                  const char* resourceName,
                                                                  const Dx12RenderGraphUavBarrierDesc& desc )
{
    Dx12RenderGraphUavBarrierRecord record;
    MakeBarrierSource( record.source, sourcePrefix, passName );
    CopyLabel( record.resourceName, resourceName );
    record.nativeResource = desc.resource;

    const Dx12RenderGraphUavBarrierResult result = EmitDx12RenderGraphUavBarrier( desc );
    record.hasNativeResource = result.hasNativeResource;
    record.missingCommandList = result.missingCommandList;
    record.emitted = result.emitted;
    return record;
}


} // namespace Rendering
} // namespace SkullbonezCore
