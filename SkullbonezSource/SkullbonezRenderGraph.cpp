#include "SkullbonezRenderGraph.h"

#include <sstream>
#include <stdexcept>

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
    case RenderGraphResourceAccess::Present:
        return "Present";
    default:
        return "Unknown";
    }
}


void RenderGraph::Clear()
{
    // Clear starts a fresh frame graph. It forgets declarations only; it does
    // not destroy GPU textures because this first graph contract does not own
    // GPU resources yet.
    m_resources.clear();
    m_passes.clear();
}


RenderGraphResourceHandle RenderGraph::AddExternalResource( const char* name )
{
    // External resources are objects the current renderer already owns, such as
    // the swap-chain back buffer or an existing reflection target. The graph can
    // reason about how passes use them without taking over allocation yet.
    RenderGraphResourceDesc desc;
    desc.name = ( name && name[0] != '\0' ) ? name : "UnnamedResource";
    desc.external = true;

    RenderGraphResourceHandle handle;
    handle.index = static_cast<uint32_t>( m_resources.size() );
    m_resources.push_back( desc );
    return handle;
}


uint32_t RenderGraph::AddPass( const char* name, RenderGraphQueueType queue )
{
    // A pass is a named unit of frame work. It does not record commands in this
    // first slice. It records intent, so future code can compare pass order and
    // resource uses before command recording starts.
    RenderGraphPassDesc pass;
    pass.name = ( name && name[0] != '\0' ) ? name : "UnnamedPass";
    pass.queue = queue;

    const uint32_t index = static_cast<uint32_t>( m_passes.size() );
    m_passes.push_back( pass );
    return index;
}


void RenderGraph::AddRead( uint32_t passIndex, RenderGraphResourceHandle resource, RenderGraphResourceAccess access )
{
    // A read means this pass expects the previous contents of the resource to
    // already exist and be visible to the shader or fixed-function GPU stage.
    // Future graph compilation can compare this read against the previous write
    // and insert the correct barrier before the pass records commands.
    CheckedResource( resource );
    RenderGraphPassDesc& pass = CheckedPass( passIndex );
    pass.reads.push_back( { resource, access } );
}


void RenderGraph::AddWrite( uint32_t passIndex, RenderGraphResourceHandle resource, RenderGraphResourceAccess access )
{
    // A write means this pass produces or overwrites contents in the resource.
    // Future graph compilation can remember this as the latest known resource
    // state, then transition the resource before the next incompatible read or
    // write. This is how scattered hand-written barriers eventually become a
    // single pass/resource scheduling problem.
    CheckedResource( resource );
    RenderGraphPassDesc& pass = CheckedPass( passIndex );
    pass.writes.push_back( { resource, access } );
}


std::string RenderGraph::DumpText() const
{
    // Human-readable dumps are an early diagnostic tool. Before a render graph
    // compiler starts inserting barriers, a text dump lets an engineer confirm
    // the frame is declared in the intended order and that each pass uses the
    // expected resources.
    std::ostringstream out;
    out << "RenderGraph\n";
    out << "Resources:\n";
    for ( size_t i = 0; i < m_resources.size(); ++i )
    {
        const RenderGraphResourceDesc& resource = m_resources[i];
        out << "  [" << i << "] " << resource.name << " external=" << ( resource.external ? "true" : "false" ) << "\n";
    }

    out << "Passes:\n";
    for ( size_t passIndex = 0; passIndex < m_passes.size(); ++passIndex )
    {
        const RenderGraphPassDesc& pass = m_passes[passIndex];
        out << "  [" << passIndex << "] " << pass.name << " queue=" << ToString( pass.queue ) << "\n";

        for ( const RenderGraphResourceUse& read : pass.reads )
        {
            const RenderGraphResourceDesc& resource = CheckedResource( read.resource );
            out << "    read  " << resource.name << " as " << ToString( read.access ) << "\n";
        }

        for ( const RenderGraphResourceUse& write : pass.writes )
        {
            const RenderGraphResourceDesc& resource = CheckedResource( write.resource );
            out << "    write " << resource.name << " as " << ToString( write.access ) << "\n";
        }
    }
    return out.str();
}


const RenderGraphResourceDesc& RenderGraph::CheckedResource( RenderGraphResourceHandle handle ) const
{
    // Fail immediately when a pass refers to a resource that was never declared.
    // That is much easier to debug than letting a bad graph handle turn into a
    // wrong DX12 descriptor, a bad resource barrier, or a GPU validation error
    // later in the frame.
    if ( !handle.IsValid() || handle.index >= m_resources.size() )
    {
        throw std::runtime_error( "RenderGraph resource handle out of range" );
    }
    return m_resources[handle.index];
}


RenderGraphPassDesc& RenderGraph::CheckedPass( uint32_t passIndex )
{
    // Pass indices are local to this graph. Throwing here keeps graph
    // construction mistakes in CPU code instead of letting them silently produce
    // incomplete barrier schedules later.
    if ( passIndex >= m_passes.size() )
    {
        throw std::runtime_error( "RenderGraph pass index out of range" );
    }
    return m_passes[passIndex];
}

} // namespace Rendering
} // namespace SkullbonezCore
