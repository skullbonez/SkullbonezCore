/*
File: SkullbonezSource/Rendering/RenderGraph.cpp
Purpose:
  Records render pass/resource intent, callback execution, and transient texture
  lifetime plans.

Summary:
  RenderGraph records declared reads, writes, and callback order, then derives
  API-neutral transitions and transient alias lifetimes for a backend executor
  to materialize.

Invariants:
  - The graph records pass/resource intent, transient lifetime diagnostics, and
    optional callback execution; backend code owns API object creation.
  - Pass resource accesses must name concrete states so backend helpers can
    reason about transition intent.

Related:
  - SkullbonezSource/Rendering/RenderGraph.h
  - Agentic/Reference/engine-glossary.md
*/
#include "RenderGraph.h"

#include <algorithm>
#include <cstring>
#include <sstream>

namespace SkullbonezCore
{
namespace Rendering
{

const char* ToString( RenderGraphQueueType queue )
{
    switch ( queue )
    {
    case RenderGraphQueueType::Graphics:
        return "Graphics";
    case RenderGraphQueueType::Compute:
        return "Compute";
    case RenderGraphQueueType::Copy:
        return "Copy";
    default:
        return "Unknown";
    }
}


const char* ToString( RenderGraphPassExecutionOwner owner )
{
    switch ( owner )
    {
    case RenderGraphPassExecutionOwner::DeclarationOnly:
        return "DeclarationOnly";
    case RenderGraphPassExecutionOwner::Callback:
        return "Callback";
    default:
        return "Unknown";
    }
}


const char* ToString( RenderGraphResourceAccess access )
{
    switch ( access )
    {
    case RenderGraphResourceAccess::Unknown:
        return "Unknown";
    case RenderGraphResourceAccess::RenderTarget:
        return "RenderTarget";
    case RenderGraphResourceAccess::DepthRead:
        return "DepthRead";
    case RenderGraphResourceAccess::DepthWrite:
        return "DepthWrite";
    case RenderGraphResourceAccess::ShaderResource:
        return "ShaderResource";
    case RenderGraphResourceAccess::PixelShaderResource:
        return "PixelShaderResource";
    case RenderGraphResourceAccess::NonPixelShaderResource:
        return "NonPixelShaderResource";
    case RenderGraphResourceAccess::UnorderedAccess:
        return "UnorderedAccess";
    case RenderGraphResourceAccess::CopySource:
        return "CopySource";
    case RenderGraphResourceAccess::CopyDest:
        return "CopyDest";
    case RenderGraphResourceAccess::VertexAndNonPixelShaderResource:
        return "VertexAndNonPixelShaderResource";
    case RenderGraphResourceAccess::Present:
        return "Present";
    default:
        return "Unknown";
    }
}


const char* ToString( RenderGraphResourceKind kind )
{
    switch ( kind )
    {
    case RenderGraphResourceKind::Texture2D:
        return "Texture2D";
    case RenderGraphResourceKind::Buffer:
        return "Buffer";
    default:
        return "Unknown";
    }
}


const char* ToString( RenderGraphResourceFormat format )
{
    switch ( format )
    {
    case RenderGraphResourceFormat::Unknown:
        return "Unknown";
    case RenderGraphResourceFormat::RGBA8:
        return "RGBA8";
    case RenderGraphResourceFormat::RGBA16F:
        return "RGBA16F";
    case RenderGraphResourceFormat::Depth24Stencil8:
        return "Depth24Stencil8";
    default:
        return "Unknown";
    }
}


const char* RenderGraphSubresourceToString( uint32_t subresource )
{
    return subresource == RENDER_GRAPH_ALL_SUBRESOURCES ? "all" : nullptr;
}


uint32_t CountDescriptorNeeds( const RenderGraphDescriptorNeeds& descriptors )
{
    return ( descriptors.renderTarget ? 1u : 0u ) + ( descriptors.depthStencil ? 1u : 0u ) +
           ( descriptors.shaderResource ? 1u : 0u ) + ( descriptors.unorderedAccess ? 1u : 0u );
}


bool DescriptorNeedsEqual( const RenderGraphDescriptorNeeds& lhs, const RenderGraphDescriptorNeeds& rhs )
{
    return lhs.renderTarget == rhs.renderTarget && lhs.depthStencil == rhs.depthStencil &&
           lhs.shaderResource == rhs.shaderResource && lhs.unorderedAccess == rhs.unorderedAccess;
}


bool TransientResourceDescCompatible( const RenderGraphTransientResourceDesc& lhs,
                                      const RenderGraphTransientResourceDesc& rhs )
{
    return lhs.kind == rhs.kind && lhs.format == rhs.format && lhs.width == rhs.width && lhs.height == rhs.height &&
           lhs.mipLevels == rhs.mipLevels && DescriptorNeedsEqual( lhs.descriptors, rhs.descriptors );
}


void AppendDescriptorNeeds( std::ostringstream& out, const RenderGraphDescriptorNeeds& descriptors )
{
    bool wroteAny = false;
    const auto append = [&]( bool enabled, const char* name )
    {
        if ( !enabled )
        {
            return;
        }

        if ( wroteAny )
        {
            out << "|";
        }

        out << name;
        wroteAny = true;
    };

    append( descriptors.renderTarget, "RTV" );
    append( descriptors.depthStencil, "DSV" );
    append( descriptors.shaderResource, "SRV" );
    append( descriptors.unorderedAccess, "UAV" );

    if ( !wroteAny )
    {
        out << "None";
    }
}


void RenderGraphResourceUseList::push_back( const RenderGraphResourceUse& use )
{
    if ( count >= uses.size() )
    {
        SB_FATAL( "RenderGraph", "Pass resource-use capacity exceeded. count=%zu capacity=%zu", count, uses.size() );
    }

    uses[count++] = use;
}


void RenderGraphCompileResult::Clear()
{
    transitions.clear();
    resourceLifetimes.clear();
    transientAllocations.clear();
    transientDiagnostics = RenderGraphTransientAllocationDiagnostics();
}


void RenderGraphCompileResult::ReserveForRuntimePassGraph()
{
    transitions.reserve( RENDER_GRAPH_MAX_TRANSITIONS );
    resourceLifetimes.reserve( RENDER_GRAPH_MAX_RESOURCES );
    transientAllocations.reserve( RENDER_GRAPH_MAX_TRANSIENT_ALLOCATIONS );
}


void RenderGraph::Clear()
{

    // Clear starts a fresh frame graph. It forgets declarations only; it does
    // not destroy GPU textures because this first graph contract does not own
    // GPU resources yet.
    m_resources.clear();
    m_passes.clear();
    m_callbackRecords = {};
}


void RenderGraph::ReserveForRuntimePassGraph()
{
    m_resources.reserve( RENDER_GRAPH_MAX_RESOURCES );
    m_passes.reserve( RENDER_GRAPH_MAX_PASSES );
}


RenderGraphResourceHandle RenderGraph::AddExternalResource( const char* name, RenderGraphResourceAccess initialAccess,
                                                            RenderGraphNativeResourceToken nativeResource )
{

    // External resources are objects the current renderer already owns, such as
    // the swap-chain back buffer or an existing reflection target. The graph can
    // reason about how passes use them without taking over allocation yet.
    //
    // initialAccess is the graph's best knowledge of how the backend-owned
    // resource starts the frame. For example, a swap-chain backbuffer often
    // starts as Present, then the first draw pass transitions it to RenderTarget.
    const char* resolvedName = ( name && name[0] != '\0' ) ? name : "UnnamedResource";

    for ( size_t resourceIndex = 0; resourceIndex < m_resources.size(); ++resourceIndex )
    {
        RenderGraphResourceDesc& existing = m_resources[resourceIndex];

        if ( std::strcmp( existing.name, resolvedName ) != 0 )
        {
            continue;
        }

        if ( !existing.external )
        {
            SB_FATAL( "RenderGraph", "External resource name aliases a transient resource. name=%s", resolvedName );
        }

        if ( static_cast<bool>( existing.nativeResource ) && static_cast<bool>( nativeResource ) &&
             existing.nativeResource.value != nativeResource.value )
        {
            SB_FATAL( "RenderGraph", "External resource name aliases two native resources. name=%s", resolvedName );
        }

        if ( !static_cast<bool>( existing.nativeResource ) && static_cast<bool>( nativeResource ) )
        {
            existing.nativeResource = nativeResource;
        }

        RenderGraphResourceHandle existingHandle;
        existingHandle.index = static_cast<uint32_t>( resourceIndex );
        return existingHandle;
    }

    if ( m_resources.size() >= RENDER_GRAPH_MAX_RESOURCES )
    {
        SB_FATAL( "RenderGraph", "Resource capacity exceeded while adding external resource. count=%zu capacity=%zu",
                  m_resources.size(), RENDER_GRAPH_MAX_RESOURCES );
    }

    RenderGraphResourceDesc desc;
    desc.name = resolvedName;
    desc.external = true;
    desc.initialAccess = initialAccess;
    desc.nativeResource = nativeResource;

    RenderGraphResourceHandle handle;
    handle.index = static_cast<uint32_t>( m_resources.size() );
    m_resources.push_back( desc );
    return handle;
}


RenderGraphResourceHandle RenderGraph::AddTransientResource( const char* name,
                                                             const RenderGraphTransientResourceDesc& transient,
                                                             RenderGraphResourceAccess initialAccess )
{
    if ( transient.width == 0 || transient.height == 0 || transient.mipLevels == 0 )
    {
        SB_FATAL( "RenderGraph", "Transient resource dimensions must be non-zero. width=%u height=%u mipLevels=%u",
                  transient.width, transient.height, transient.mipLevels );
    }

    if ( CountDescriptorNeeds( transient.descriptors ) == 0 )
    {
        SB_FATAL( "RenderGraph", "Transient resource requires at least one descriptor need." );
    }

    if ( m_resources.size() >= RENDER_GRAPH_MAX_RESOURCES )
    {
        SB_FATAL( "RenderGraph", "Resource capacity exceeded while adding transient resource. count=%zu capacity=%zu",
                  m_resources.size(), RENDER_GRAPH_MAX_RESOURCES );
    }

    RenderGraphResourceDesc desc;
    desc.name = ( name && name[0] != '\0' ) ? name : "UnnamedTransientResource";
    desc.external = false;
    desc.initialAccess = initialAccess;
    desc.nativeResource = {};
    desc.transient = transient;

    RenderGraphResourceHandle handle;
    handle.index = static_cast<uint32_t>( m_resources.size() );
    m_resources.push_back( desc );
    return handle;
}


uint32_t RenderGraph::AddPass( const char* name, RenderGraphQueueType queue )
{

    // A pass is a named unit of frame work. A declaration becomes executable
    // when its callback is installed; callback-free rows are frame-edge
    // bookkeeping only.
    if ( m_passes.size() >= RENDER_GRAPH_MAX_PASSES )
    {
        SB_FATAL( "RenderGraph", "Pass capacity exceeded. count=%zu capacity=%zu", m_passes.size(),
                  RENDER_GRAPH_MAX_PASSES );
    }

    RenderGraphPassDesc pass;
    pass.name = ( name && name[0] != '\0' ) ? name : "UnnamedPass";
    pass.debugLabel = pass.name;
    pass.queue = queue;

    const uint32_t index = static_cast<uint32_t>( m_passes.size() );
    m_passes.push_back( pass );
    m_callbackRecords[index] = {};

    return index;
}


void RenderGraph::AddRead( uint32_t passIndex, RenderGraphResourceHandle resource, RenderGraphResourceAccess access,
                           uint32_t subresource )
{

    // A read means this pass expects the previous contents of the resource to
    // already exist and be visible to the shader or fixed-function GPU stage.
    // Graph compilation compares this read against the previous write and emits
    // a transition record before the pass that needs the new access.
    CheckedConcreteAccess( access );
    CheckedResource( resource );
    RenderGraphPassDesc& pass = CheckedPass( passIndex );
    pass.reads.push_back( { resource, access, subresource } );
}


void RenderGraph::AddWrite( uint32_t passIndex, RenderGraphResourceHandle resource, RenderGraphResourceAccess access,
                            uint32_t subresource )
{

    // A write means this pass produces or overwrites contents in the resource.
    // Graph compilation remembers this as the latest known resource state, then
    // transitions the resource before the next incompatible read or write. This
    // is the API-neutral shape the DX12 executor maps to concrete barriers.
    CheckedConcreteAccess( access );
    CheckedResource( resource );
    RenderGraphPassDesc& pass = CheckedPass( passIndex );
    pass.writes.push_back( { resource, access, subresource } );
}


void RenderGraph::SetPassCallbackRecord( uint32_t passIndex, CallbackRecord record, bool enabled, const char* debugLabel )
{

    // Concept: callback ownership is a pass-order contract, not a closure
    // warehouse. A raw function pointer plus caller-owned userdata keeps the
    // graph from allocating or retaining broad runtime state just to execute one
    // pass body.
    if ( record.invoke == nullptr )
    {
        SB_FATAL( "RenderGraph", "Callback pass requires a non-null callback." );
    }

    RenderGraphPassDesc& pass = CheckedPass( passIndex );
    pass.executionOwner = RenderGraphPassExecutionOwner::Callback;
    pass.callbackEnabled = enabled;
    pass.debugLabel = ( debugLabel && debugLabel[0] != '\0' ) ? debugLabel : pass.name;
    m_callbackRecords[passIndex] = record;
}


std::string RenderGraph::DumpText() const
{

    // Human-readable dumps are an early diagnostic tool. Before a render graph
    // text dump lets an engineer confirm the frame is declared in the intended
    // order and that each pass uses the expected resources.
    std::ostringstream out;
    out << "RenderGraph\n";
    out << "Resources:\n";

    for ( size_t i = 0; i < m_resources.size(); ++i )
    {
        const RenderGraphResourceDesc& resource = m_resources[i];
        out << "  [" << i << "] " << resource.name << " external=" << ( resource.external ? "true" : "false" )
            << " initial=" << ToString( resource.initialAccess ) << " native=" << resource.nativeResource.value << "\n";

        if ( !resource.external )
        {
            out << "      transient kind=" << ToString( resource.transient.kind )
                << " format=" << ToString( resource.transient.format ) << " size=" << resource.transient.width << "x"
                << resource.transient.height << " mips=" << resource.transient.mipLevels << " descriptors=";
            AppendDescriptorNeeds( out, resource.transient.descriptors );
            out << "\n";
        }
    }

    out << "Passes:\n";

    for ( size_t passIndex = 0; passIndex < m_passes.size(); ++passIndex )
    {
        const RenderGraphPassDesc& pass = m_passes[passIndex];
        out << "  [" << passIndex << "] " << pass.name << " queue=" << ToString( pass.queue )
            << " execution=" << ToString( pass.executionOwner );

        if ( pass.executionOwner == RenderGraphPassExecutionOwner::Callback )
        {
            out << " callback_enabled=" << ( pass.callbackEnabled ? "true" : "false" ) << " debug_label=" << pass.debugLabel;
        }

        out << "\n";

        for ( const RenderGraphResourceUse& read : pass.reads )
        {
            const RenderGraphResourceDesc& resource = CheckedResource( read.resource );
            out << "    read  " << resource.name << " as " << ToString( read.access ) << "\n";

            if ( const char* subresourceText = RenderGraphSubresourceToString( read.subresource ) )
            {
                out << "      subresource=" << subresourceText << "\n";
            }
            else
            {
                out << "      subresource=" << read.subresource << "\n";
            }
        }

        for ( const RenderGraphResourceUse& write : pass.writes )
        {
            const RenderGraphResourceDesc& resource = CheckedResource( write.resource );
            out << "    write " << resource.name << " as " << ToString( write.access ) << "\n";

            if ( const char* subresourceText = RenderGraphSubresourceToString( write.subresource ) )
            {
                out << "      subresource=" << subresourceText << "\n";
            }
            else
            {
                out << "      subresource=" << write.subresource << "\n";
            }
        }
    }

    const RenderGraphCompileResult compiled = Compile();
    out << "Transitions:\n";

    for ( const RenderGraphTransitionDesc& transition : compiled.transitions )
    {
        const RenderGraphResourceDesc& resource = CheckedResource( transition.resource );
        const RenderGraphPassDesc& pass = m_passes[transition.passIndex];
        out << "  before pass [" << transition.passIndex << "] " << pass.name << ": " << resource.name << " "
            << ToString( transition.before ) << " -> " << ToString( transition.after );

        if ( const char* subresourceText = RenderGraphSubresourceToString( transition.subresource ) )
        {
            out << " subresource=" << subresourceText;
        }
        else
        {
            out << " subresource=" << transition.subresource;
        }

        out << "\n";
    }

    out << "TransientAllocations:\n";

    for ( const RenderGraphTransientAllocationDesc& allocation : compiled.transientAllocations )
    {
        const RenderGraphResourceDesc& resource = CheckedResource( allocation.resource );
        out << "  resource=" << resource.name << " slot=" << allocation.poolSlot << " first_pass=" << allocation.firstPass
            << " last_pass=" << allocation.lastPass << " descriptors=" << allocation.descriptorCount
            << " reused=" << ( allocation.reused ? "true" : "false" )
            << " released_at_frame_end=" << ( allocation.releasedAtFrameEnd ? "true" : "false" ) << "\n";
    }

    out << "TransientDiagnostics allocation_count=" << compiled.transientDiagnostics.allocationCount
        << " reuse_count=" << compiled.transientDiagnostics.reuseCount
        << " release_count=" << compiled.transientDiagnostics.releaseCount
        << " high_water_resources=" << compiled.transientDiagnostics.highWaterResources
        << " high_water_descriptors=" << compiled.transientDiagnostics.highWaterDescriptors << "\n";
    return out.str();
}


RenderGraphCompileResult RenderGraph::Compile() const
{
    RenderGraphCompileResult result;
    Compile( result );
    return result;
}


void RenderGraph::Compile( RenderGraphCompileResult& result ) const
{

    // This is the first deliberately simple graph compiler.
    //
    // It does not execute callbacks. It does not create backend API textures.
    // It does not optimize away barriers or reason about async queues yet.
    //
    // What it does:
    //
    // 1. Start every resource in its declared initial access state.
    // 2. Walk passes in the order they were added.
    // 3. Visit each declared read, then each declared write.
    // 4. Whenever the desired access differs from the tracked current access,
    //    emit a transition record before that pass.
    // 5. Remember the new access as the resource's current state.
    // 6. Plan graph-declared transient lifetime, aliasing, and descriptor high
    //    water diagnostics from the first/last use of each resource.
    //
    // That mirrors the resource-access story in API-neutral terms. The DX12
    // backend still emits live barriers from explicit before/after calls and can
    // create API resources from the transient allocation plan when a production
    // pass stops importing its target.
    result.Clear();
    result.resourceLifetimes.resize( m_resources.size() );

    for ( size_t resourceIndex = 0; resourceIndex < m_resources.size(); ++resourceIndex )
    {
        RenderGraphResourceLifetimeDesc& lifetime = result.resourceLifetimes[resourceIndex];
        lifetime.resource.index = static_cast<uint32_t>( resourceIndex );
    }

    std::array<RenderGraphResourceAccess, RENDER_GRAPH_MAX_RESOURCES> allSubresourceAccess = {};

    for ( size_t resourceIndex = 0; resourceIndex < m_resources.size(); ++resourceIndex )
    {
        allSubresourceAccess[resourceIndex] = m_resources[resourceIndex].initialAccess;
    }

    struct SubresourceAccessState
    {
        uint32_t subresource = RENDER_GRAPH_ALL_SUBRESOURCES;
        RenderGraphResourceAccess access = RenderGraphResourceAccess::Unknown;
    };

    struct SubresourceAccessList
    {
        SubresourceAccessState states[RENDER_GRAPH_MAX_SUBRESOURCE_STATES_PER_RESOURCE] = {};
        size_t count = 0;
    };

    std::array<SubresourceAccessList, RENDER_GRAPH_MAX_RESOURCES> subresourceAccess = {};

    for ( size_t passIndex = 0; passIndex < m_passes.size(); ++passIndex )
    {
        const RenderGraphPassDesc& pass = m_passes[passIndex];

        const auto emitTransition = [&]( const RenderGraphResourceUse& use,
                                        RenderGraphResourceAccess before, uint32_t subresource )
        {
            if ( before == RenderGraphResourceAccess::Unknown )
            {
                return;
            }

            if ( before == use.access )
            {
                return;
            }

            RenderGraphTransitionDesc transition;
            transition.passIndex = static_cast<uint32_t>( passIndex );
            transition.resource = use.resource;
            transition.nativeResource = m_resources[use.resource.index].nativeResource;
            transition.before = before;
            transition.after = use.access;
            transition.subresource = subresource;

            if ( result.transitions.size() >= RENDER_GRAPH_MAX_TRANSITIONS )
            {
                SB_FATAL( "RenderGraph", "Transition capacity exceeded. count=%zu capacity=%zu", result.transitions.size(),
                          RENDER_GRAPH_MAX_TRANSITIONS );
            }

            result.transitions.push_back( transition );
        };

        const auto findSubresource = []( SubresourceAccessList& states, uint32_t subresource ) -> SubresourceAccessState*
        {
            for ( size_t i = 0; i < states.count; ++i )
            {
                SubresourceAccessState& state = states.states[i];

                if ( state.subresource == subresource )
                {
                    return &state;
                }
            }

            return nullptr;
        };

        const auto recordUse = [&]( const RenderGraphResourceUse& use )
        {
            CheckedConcreteAccess( use.access );

            CheckedResource( use.resource );
            const uint32_t resourceIndex = use.resource.index;
            RenderGraphResourceLifetimeDesc& lifetime = result.resourceLifetimes[resourceIndex];

            if ( !lifetime.used )
            {
                lifetime.firstPass = static_cast<uint32_t>( passIndex );
                lifetime.used = true;
            }

            lifetime.lastPass = static_cast<uint32_t>( passIndex );

            RenderGraphResourceAccess& allAccess = allSubresourceAccess[resourceIndex];
            SubresourceAccessList& specificAccess = subresourceAccess[resourceIndex];

            if ( use.subresource == RENDER_GRAPH_ALL_SUBRESOURCES )
            {
                for ( size_t stateIndex = 0; stateIndex < specificAccess.count; ++stateIndex )
                {
                    const SubresourceAccessState& state = specificAccess.states[stateIndex];
                    emitTransition( use, state.access, state.subresource );
                }

                const bool hadSpecificAccess = specificAccess.count > 0;
                specificAccess.count = 0;

                if ( allAccess == RenderGraphResourceAccess::Unknown )
                {

                    // Unknown means "legacy code still owns the real initial
                    // DX12 state." It is useful as a diagnostic marker, but it
                    // is not a real barrier source state.
                    allAccess = use.access;
                    return;
                }

                if ( hadSpecificAccess && allAccess != use.access )
                {
                    SB_FATAL( "RenderGraph",
                              "Cannot compile an all-subresources transition after mixed subresource states." );
                }

                emitTransition( use, allAccess, RENDER_GRAPH_ALL_SUBRESOURCES );
                allAccess = use.access;
                return;
            }

            SubresourceAccessState* state = findSubresource( specificAccess, use.subresource );
            const RenderGraphResourceAccess before = state ? state->access : allAccess;
            emitTransition( use, before, use.subresource );

            if ( state )
            {
                state->access = use.access;

                if ( state->access == allAccess )
                {
                    for ( size_t stateIndex = 0; stateIndex < specificAccess.count; ++stateIndex )
                    {
                        if ( &specificAccess.states[stateIndex] == state )
                        {
                            for ( size_t moveIndex = stateIndex + 1; moveIndex < specificAccess.count; ++moveIndex )
                            {
                                specificAccess.states[moveIndex - 1] = specificAccess.states[moveIndex];
                            }

                            --specificAccess.count;
                            break;
                        }
                    }
                }
            }
            else if ( before == RenderGraphResourceAccess::Unknown || before != use.access )
            {
                if ( specificAccess.count >= RENDER_GRAPH_MAX_SUBRESOURCE_STATES_PER_RESOURCE )
                {
                    SB_FATAL( "RenderGraph", "Subresource state capacity exceeded. count=%zu capacity=%zu",
                              specificAccess.count, RENDER_GRAPH_MAX_SUBRESOURCE_STATES_PER_RESOURCE );
                }

                specificAccess.states[specificAccess.count++] = { use.subresource, use.access };
            }
        };

        for ( const RenderGraphResourceUse& read : pass.reads )
        {
            recordUse( read );
        }

        for ( const RenderGraphResourceUse& write : pass.writes )
        {
            recordUse( write );
        }
    }

    struct TransientPoolSlot
    {
        RenderGraphTransientResourceDesc desc;
        uint32_t lastPass = 0;
        bool occupied = false;
    };

    std::array<TransientPoolSlot, RENDER_GRAPH_MAX_RESOURCES> poolSlots = {};
    size_t poolSlotCount = 0;

    for ( size_t resourceIndex = 0; resourceIndex < m_resources.size(); ++resourceIndex )
    {
        const RenderGraphResourceDesc& resource = m_resources[resourceIndex];

        if ( resource.external )
        {
            continue;
        }

        const RenderGraphResourceLifetimeDesc& lifetime = result.resourceLifetimes[resourceIndex];

        if ( !lifetime.used )
        {
            SB_FATAL( "RenderGraph", "Transient resource must be read or written by at least one pass. resourceIndex=%zu",
                      resourceIndex );
        }

        uint32_t poolSlot = static_cast<uint32_t>( poolSlotCount );
        bool reused = false;

        for ( size_t candidateIndex = 0; candidateIndex < poolSlotCount; ++candidateIndex )
        {
            TransientPoolSlot& candidate = poolSlots[candidateIndex];

            if ( candidate.occupied && candidate.lastPass < lifetime.firstPass &&
                 TransientResourceDescCompatible( candidate.desc, resource.transient ) )
            {
                poolSlot = static_cast<uint32_t>( candidateIndex );
                reused = true;
                break;
            }
        }

        if ( reused )
        {
            TransientPoolSlot& slot = poolSlots[poolSlot];
            slot.lastPass = lifetime.lastPass;
        }
        else
        {
            TransientPoolSlot slot;
            slot.desc = resource.transient;
            slot.lastPass = lifetime.lastPass;
            slot.occupied = true;

            if ( poolSlotCount >= poolSlots.size() )
            {
                SB_FATAL( "RenderGraph", "Transient pool capacity exceeded. count=%zu capacity=%zu", poolSlotCount,
                          poolSlots.size() );
            }

            poolSlots[poolSlotCount++] = slot;
        }

        RenderGraphTransientAllocationDesc allocation;
        allocation.resource.index = static_cast<uint32_t>( resourceIndex );
        allocation.poolSlot = poolSlot;
        allocation.firstPass = lifetime.firstPass;
        allocation.lastPass = lifetime.lastPass;
        allocation.descriptorCount = CountDescriptorNeeds( resource.transient.descriptors );
        allocation.reused = reused;
        allocation.releasedAtFrameEnd = true;

        if ( result.transientAllocations.size() >= RENDER_GRAPH_MAX_TRANSIENT_ALLOCATIONS )
        {
            SB_FATAL( "RenderGraph", "Transient allocation capacity exceeded. count=%zu capacity=%zu",
                      result.transientAllocations.size(), RENDER_GRAPH_MAX_TRANSIENT_ALLOCATIONS );
        }

        result.transientAllocations.push_back( allocation );
    }

    result.transientDiagnostics.allocationCount = result.transientAllocations.size();
    result.transientDiagnostics.releaseCount = result.transientAllocations.size();

    for ( const RenderGraphTransientAllocationDesc& allocation : result.transientAllocations )
    {
        if ( allocation.reused )
        {
            ++result.transientDiagnostics.reuseCount;
        }
    }

    for ( size_t passIndex = 0; passIndex < m_passes.size(); ++passIndex )
    {
        size_t liveResources = 0;
        size_t liveDescriptors = 0;

        for ( const RenderGraphTransientAllocationDesc& allocation : result.transientAllocations )
        {
            if ( allocation.firstPass <= passIndex && passIndex <= allocation.lastPass )
            {
                ++liveResources;
                liveDescriptors += allocation.descriptorCount;
            }
        }

        result.transientDiagnostics.highWaterResources = (std::max)( result.transientDiagnostics.highWaterResources,
                                                                     liveResources );

        result.transientDiagnostics.highWaterDescriptors = (std::max)( result.transientDiagnostics.highWaterDescriptors,
                                                                       liveDescriptors );
    }
}


RenderGraphCallbackExecutionResult RenderGraph::ExecuteCallbacks( RenderGraphCallbackExecutionMode mode, uint32_t firstPass,
                                                                  uint32_t passCount ) const
{
    RenderGraphCallbackExecutionResult result;
    const size_t first = static_cast<size_t>( firstPass );
    const size_t requestedEnd = first + static_cast<size_t>( passCount );

    if ( first > m_passes.size() || requestedEnd < first || requestedEnd > m_passes.size() )
    {
        SB_FATAL( "RenderGraph", "Callback execution range is out of bounds. first=%u count=%u passes=%zu", firstPass,
                  passCount, m_passes.size() );
    }

    for ( size_t passIndex = first; passIndex < requestedEnd; ++passIndex )
    {
        const RenderGraphPassDesc& pass = m_passes[passIndex];

        if ( pass.executionOwner != RenderGraphPassExecutionOwner::Callback )
        {
            ++result.declarationOnlyPassCount;
            continue;
        }

        ++result.callbackPassCount;

        if ( !pass.callbackEnabled )
        {
            ++result.disabledCallbackPassCount;
            continue;
        }

        const CallbackRecord& callback = m_callbackRecords[passIndex];

        if ( callback.invoke == nullptr )
        {
            SB_FATAL( "RenderGraph", "Callback pass has no callback." );
        }

        if ( pass.reads.empty() && pass.writes.empty() )
        {
            SB_FATAL( "RenderGraph", "Callback pass must declare at least one resource use." );
        }

        RenderGraphPassContext context;
        context.graph = this;
        context.pass = &pass;
        context.passIndex = static_cast<uint32_t>( passIndex );
        context.debugLabel = pass.debugLabel;
        context.dryRun = mode == RenderGraphCallbackExecutionMode::DryRun;

        if ( context.dryRun )
        {
            ++result.dryRunValidatedPassCount;
            continue;
        }

        callback.invoke( context, callback );
        ++result.executedPassCount;
    }

    return result;
}


RenderGraphExecutionContractResult RenderGraph::ValidateFrameExecutionContract( const char* declarationOnlyPassName ) const
{
    RenderGraphExecutionContractResult result;
    const bool expectsDeclarationOnlyPass = declarationOnlyPassName && declarationOnlyPassName[0] != '\0';
    result.expectedDeclarationOnlyPassCount = expectsDeclarationOnlyPass ? 1u : 0u;
    const char* expectedName = expectsDeclarationOnlyPass ? declarationOnlyPassName : "";

    for ( const RenderGraphPassDesc& pass : m_passes )
    {
        if ( pass.executionOwner == RenderGraphPassExecutionOwner::Callback )
        {
            ++result.callbackPassCount;
            result.allCallbacksEnabled = result.allCallbacksEnabled && pass.callbackEnabled;
            continue;
        }

        ++result.declarationOnlyPassCount;
        result.declarationOnlyNameMatches = result.declarationOnlyNameMatches && std::strcmp( pass.name, expectedName ) == 0;
    }

    return result;
}


void RenderGraph::ReleaseCallbackPayloadBorrows()
{

    // Lifetime: production callbacks borrow stack payloads only through their
    // synchronous append/execute range. Once frame diagnostics are complete,
    // poison every erased invocation slot so an accidental full-graph rerun
    // fails through the missing-callback invariant instead of dereferencing a
    // payload whose owner has left scope.
    for ( size_t passIndex = 0; passIndex < m_passes.size(); ++passIndex )
    {
        if ( m_passes[passIndex].executionOwner == RenderGraphPassExecutionOwner::Callback )
        {
            m_callbackRecords[passIndex] = {};
        }
    }
}


const RenderGraphResourceDesc& RenderGraph::CheckedResource( RenderGraphResourceHandle handle ) const
{

    // Fail immediately when a pass refers to a resource that was never declared.
    // That is much easier to debug than letting a bad graph handle turn into a
    // wrong DX12 descriptor, a bad resource barrier, or a GPU validation error
    // later in the frame.
    if ( !handle.IsValid() || handle.index >= m_resources.size() )
    {
        SB_FATAL( "RenderGraph", "Resource handle out of range. index=%u count=%zu", handle.index, m_resources.size() );
    }

    return m_resources[handle.index];
}


RenderGraphPassDesc& RenderGraph::CheckedPass( uint32_t passIndex )
{

    // Pass indices are local to this graph. Failing fatally here keeps graph
    // construction mistakes in CPU code instead of letting them silently produce
    // incomplete barrier schedules later.
    if ( passIndex >= m_passes.size() )
    {
        SB_FATAL( "RenderGraph", "Pass index out of range. index=%u count=%zu", passIndex, m_passes.size() );
    }

    return m_passes[passIndex];
}


void RenderGraph::CheckedConcreteAccess( RenderGraphResourceAccess access ) const
{

    // Unknown is useful as an initial state when legacy backend code still owns
    // the actual DX12 object. It is not useful as a pass declaration because a
    // backend barrier helper cannot translate "unknown" into a safe read/write
    // state for a draw or dispatch.
    if ( access == RenderGraphResourceAccess::Unknown )
    {
        SB_FATAL( "RenderGraph", "Pass resource access must be concrete." );
    }
}

} // namespace Rendering
} // namespace SkullbonezCore
