/*
File: SkullbonezSource/SkullbonezDx12RenderGraphExecutor.cpp
Purpose:
  Translates render graph resource access records into DX12 barrier candidates.

Mental model:
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
  - SkullbonezSource/SkullbonezDx12RenderGraphExecutor.h
  - SkullbonezSource/SkullbonezRenderGraph.cpp
*/
#include "SkullbonezDx12RenderGraphExecutor.h"

#include <sstream>

namespace SkullbonezCore
{
namespace Rendering
{
namespace
{

void AppendDx12StateFlag( std::ostringstream& out, bool& wroteAny, D3D12_RESOURCE_STATES state, D3D12_RESOURCE_STATES flag, const char* name )
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

std::string MakeBarrierSource( const char* prefix, const RenderGraphPassDesc& pass )
{
    std::ostringstream out;
    out << ( ( prefix && prefix[0] != '\0' ) ? prefix : "Graph" );
    out << ":" << pass.name;
    return out.str();
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
    AppendDx12StateFlag( out, wroteAny, state, D3D12_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER, "VERTEX_AND_CONSTANT_BUFFER" );
    AppendDx12StateFlag( out, wroteAny, state, D3D12_RESOURCE_STATE_INDEX_BUFFER, "INDEX_BUFFER" );
    AppendDx12StateFlag( out, wroteAny, state, D3D12_RESOURCE_STATE_RENDER_TARGET, "RENDER_TARGET" );
    AppendDx12StateFlag( out, wroteAny, state, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, "UNORDERED_ACCESS" );
    AppendDx12StateFlag( out, wroteAny, state, D3D12_RESOURCE_STATE_DEPTH_WRITE, "DEPTH_WRITE" );
    AppendDx12StateFlag( out, wroteAny, state, D3D12_RESOURCE_STATE_DEPTH_READ, "DEPTH_READ" );
    AppendDx12StateFlag( out, wroteAny, state, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, "NON_PIXEL_SHADER_RESOURCE" );
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


Dx12RenderGraphSingleTransitionResult EmitDx12RenderGraphTransitionBarrier( const Dx12RenderGraphSingleTransitionDesc& desc )
{
    Dx12RenderGraphSingleTransitionResult result;
    result.hasNativeResource = desc.resource != nullptr;
    result.hasConcreteStates =
        TryDx12RenderGraphAccessToResourceState( desc.before, result.beforeState ) &&
        TryDx12RenderGraphAccessToResourceState( desc.after, result.afterState );
    result.requiresUavOrderingReview =
        desc.before == RenderGraphResourceAccess::UnorderedAccess ||
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


Dx12RenderGraphExecutionResult ExecuteDx12RenderGraphTransitions( const RenderGraph& graph,
                                                                  const RenderGraphCompileResult& compiled,
                                                                  const Dx12RenderGraphExecutionDesc& desc )
{
    Dx12RenderGraphExecutionResult result;

    for ( const RenderGraphTransitionDesc& transition : compiled.transitions )
    {
        const RenderGraphResourceDesc& resource = graph.Resources()[transition.resource.index];
        const RenderGraphPassDesc& pass = graph.Passes()[transition.passIndex];

        Dx12RenderGraphBarrierRecord record;
        record.source = MakeBarrierSource( desc.sourcePrefix, pass );
        record.passName = pass.name;
        record.resourceName = resource.name;
        record.nativeResource = transition.nativeResource;
        record.beforeAccess = transition.before;
        record.afterAccess = transition.after;
        record.subresource = static_cast<UINT>( transition.subresource );
        record.hasNativeResource = transition.nativeResource != nullptr;
        record.hasConcreteStates =
            TryDx12RenderGraphAccessToResourceState( transition.before, record.beforeState ) &&
            TryDx12RenderGraphAccessToResourceState( transition.after, record.afterState );
        record.requiresUavOrderingReview =
            transition.before == RenderGraphResourceAccess::UnorderedAccess ||
            transition.after == RenderGraphResourceAccess::UnorderedAccess;

        if ( record.requiresUavOrderingReview )
        {
            ++result.uavAccessTransitionCount;
        }
        if ( !record.hasConcreteStates )
        {
            ++result.unknownStateTransitionCount;
            result.barriers.push_back( record );
            continue;
        }
        if ( record.beforeState == record.afterState )
        {
            ++result.skippedSameStateCount;
            result.barriers.push_back( record );
            continue;
        }
        if ( !record.hasNativeResource )
        {
            ++result.missingNativeResourceTransitionCount;
            result.barriers.push_back( record );
            continue;
        }

        ++result.transitionBarrierCount;
        if ( desc.mode == Dx12RenderGraphExecutionMode::EmitBarriers )
        {
            Dx12RenderGraphSingleTransitionDesc singleDesc;
            singleDesc.commandList = desc.commandList;
            singleDesc.resource = static_cast<ID3D12Resource*>( const_cast<void*>( record.nativeResource ) );
            singleDesc.before = transition.before;
            singleDesc.after = transition.after;
            singleDesc.subresource = static_cast<UINT>( transition.subresource );
            const Dx12RenderGraphSingleTransitionResult singleResult = EmitDx12RenderGraphTransitionBarrier( singleDesc );
            if ( singleResult.emitted )
            {
                record.emitted = true;
                ++result.emittedTransitionBarrierCount;
            }
            if ( singleResult.missingCommandList )
            {
                ++result.missingCommandListEmissionCount;
            }
        }

        result.barriers.push_back( record );
    }

    return result;
}

} // namespace Rendering
} // namespace SkullbonezCore
