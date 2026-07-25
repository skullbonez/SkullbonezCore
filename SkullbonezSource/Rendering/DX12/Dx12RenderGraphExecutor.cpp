/*
File: SkullbonezSource/Rendering/DX12/Dx12RenderGraphExecutor.cpp
Purpose:
  Translates render graph resource access records into DX12 barrier candidates.

Summary:
  The graph compiler decides that a resource use changes from access A to
  access B. This file translates A and B into DX12 states and optionally emits
  the concrete transition barrier.

Glossary:
  Resource state: DX12 usage mode that controls which reads or writes are legal.
  Barrier: Command-list operation that orders GPU work or transitions a
  resource between states.

Invariants:
  - Unknown graph access is never translated to a fake COMMON transition.
  - Dry-run mode records candidates only; it does not call ResourceBarrier().
  - Live emission requires exact native resource identity.

Related:
  - SkullbonezSource/Rendering/DX12/Dx12RenderGraphExecutor.h
  - SkullbonezSource/Rendering/RenderGraph.cpp
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

void AppendDx12StateFlag(
    std::ostringstream& out,
    bool& wroteAny,
    D3D12_RESOURCE_STATES state,
    D3D12_RESOURCE_STATES flag,
    const char* name
)
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
    snprintf(
        destination,
        N,
        "%s:%s",
        ( prefix && prefix[0] != '\0' ) ? prefix : "Graph",
        ( passName && passName[0] != '\0' ) ? passName : "UnnamedPass"
    );
}

template <size_t N>
void MakeBarrierSource( char ( &destination )[N], const char* prefix, const RenderGraphPassDesc& pass )
{
    MakeBarrierSource( destination, prefix, pass.name );
}

Dx12RenderGraphBarrierRecord MakeSingleTransitionRecord(
    const char* sourcePrefix,
    const char* passName,
    const char* resourceName,
    const Dx12RenderGraphSingleTransitionDesc& desc
)
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


std::string Dx12ResourceStateToString( D3D12_RESOURCE_STATES state )
{
    if ( state == D3D12_RESOURCE_STATE_COMMON )
    {
        return "COMMON";
    }

    std::ostringstream out;
    bool wroteAny = false;
    AppendDx12StateFlag(
        out,
        wroteAny,
        state,
        D3D12_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER,
        "VERTEX_AND_CONSTANT_BUFFER"
    );

    AppendDx12StateFlag( out, wroteAny, state, D3D12_RESOURCE_STATE_INDEX_BUFFER, "INDEX_BUFFER" );
    AppendDx12StateFlag( out, wroteAny, state, D3D12_RESOURCE_STATE_RENDER_TARGET, "RENDER_TARGET" );
    AppendDx12StateFlag( out, wroteAny, state, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, "UNORDERED_ACCESS" );
    AppendDx12StateFlag( out, wroteAny, state, D3D12_RESOURCE_STATE_DEPTH_WRITE, "DEPTH_WRITE" );
    AppendDx12StateFlag( out, wroteAny, state, D3D12_RESOURCE_STATE_DEPTH_READ, "DEPTH_READ" );
    AppendDx12StateFlag(
        out,
        wroteAny,
        state,
        D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,
        "NON_PIXEL_SHADER_RESOURCE"
    );

    AppendDx12StateFlag( out, wroteAny, state, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, "PIXEL_SHADER_RESOURCE" );
    AppendDx12StateFlag( out, wroteAny, state, D3D12_RESOURCE_STATE_STREAM_OUT, "STREAM_OUT" );
    AppendDx12StateFlag( out, wroteAny, state, D3D12_RESOURCE_STATE_INDIRECT_ARGUMENT, "INDIRECT_ARGUMENT" );
    AppendDx12StateFlag( out, wroteAny, state, D3D12_RESOURCE_STATE_COPY_DEST, "COPY_DEST" );
    AppendDx12StateFlag( out, wroteAny, state, D3D12_RESOURCE_STATE_COPY_SOURCE, "COPY_SOURCE" );
    AppendDx12StateFlag( out, wroteAny, state, D3D12_RESOURCE_STATE_RESOLVE_DEST, "RESOLVE_DEST" );
    AppendDx12StateFlag( out, wroteAny, state, D3D12_RESOURCE_STATE_RESOLVE_SOURCE, "RESOLVE_SOURCE" );
    AppendDx12StateFlag( out, wroteAny, state, D3D12_RESOURCE_STATE_PRESENT, "PRESENT" );
    if ( !wroteAny )
    {
        out << "UNKNOWN(" << static_cast<unsigned int>( state ) << ")";
    }
    return out.str();
}


Dx12RenderGraphSingleTransitionResult
EmitDx12RenderGraphTransitionBarrier( const Dx12RenderGraphSingleTransitionDesc& desc )
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


Dx12RenderGraphBarrierRecord ExecuteDx12RenderGraphSingleTransition(
    const char* sourcePrefix,
    const char* passName,
    const char* resourceName,
    const Dx12RenderGraphSingleTransitionDesc& desc
)
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


Dx12RenderGraphUavBarrierRecord ExecuteDx12RenderGraphUavBarrier(
    const char* sourcePrefix,
    const char* passName,
    const char* resourceName,
    const Dx12RenderGraphUavBarrierDesc& desc
)
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


Dx12RenderGraphExecutionResult ExecuteDx12RenderGraphTransitions(
    const RenderGraph& graph,
    const RenderGraphCompileResult& compiled,
    const Dx12RenderGraphExecutionDesc& desc
)
{
    Dx12RenderGraphExecutionResult result;

    for ( const RenderGraphTransitionDesc& transition : compiled.transitions )
    {
        const RenderGraphResourceDesc& resource = graph.Resources()[transition.resource.index];
        const RenderGraphPassDesc& pass = graph.Passes()[transition.passIndex];

        Dx12RenderGraphSingleTransitionDesc singleDesc;
        singleDesc.commandList = desc.commandList;
        singleDesc.resource = transition.nativeResource.As<ID3D12Resource>();
        singleDesc.before = transition.before;
        singleDesc.after = transition.after;
        singleDesc.subresource = static_cast<UINT>( transition.subresource );

        Dx12RenderGraphBarrierRecord record =
            MakeSingleTransitionRecord( desc.sourcePrefix, pass.name, resource.name, singleDesc );

        if ( record.requiresUavOrderingReview )
        {
            ++result.uavAccessTransitionCount;
        }
        if ( !record.hasConcreteStates )
        {
            ++result.unknownStateTransitionCount;
            result.AddBarrier( record );
            continue;
        }
        if ( record.beforeState == record.afterState )
        {
            ++result.skippedSameStateCount;
            result.AddBarrier( record );
            continue;
        }
        if ( !record.hasNativeResource )
        {
            ++result.missingNativeResourceTransitionCount;
            result.AddBarrier( record );
            continue;
        }

        ++result.transitionBarrierCount;
        if ( desc.mode == Dx12RenderGraphExecutionMode::EmitBarriers )
        {
            record = ExecuteDx12RenderGraphSingleTransition( desc.sourcePrefix, pass.name, resource.name, singleDesc );
            if ( record.emitted )
            {
                ++result.emittedTransitionBarrierCount;
            }
            if ( record.missingCommandList )
            {
                ++result.missingCommandListEmissionCount;
            }
        }

        result.AddBarrier( record );
    }

    return result;
}

} // namespace Rendering
} // namespace SkullbonezCore
