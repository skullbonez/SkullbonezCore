#include "RuntimeRenderer.h"
#include "../../Core/Allocation/RuntimeAllocationTracker.h"
#include "../Startup/Window.h"

using namespace SkullbonezCore;
using namespace SkullbonezCore::Runtime;
using namespace SkullbonezCore::Rendering;

// Invariant: each callback packet borrows its resources until the synchronous
// three-pass execution returns; no graph callback may retain it afterward.
struct RuntimeRenderer::PairPass
{
    PairedViewRenderer& renderer;
    RenderResourceLifecycle& resources;
    const PairedViewFrame& frame;
    const RenderGraphCompileResult* compiled = nullptr;
    int side = 0;
    PhysicsDebugVisualizer* contacts = nullptr;
};

void RuntimeRenderer::ExecutePairPass( const RenderGraphPassContext& context, PairPass& pass )
{
    pass.resources.RenderGraph().ExecuteGraphTransitions( *context.graph, *pass.compiled, context.passIndex );
    if ( pass.side < 2 )
    {
        pass.renderer.DrawSide( pass.frame, pass.resources.PrimitiveBatches(), pass.resources.Config().ordinaryRender,
                                pass.resources.RenderFrame(), pass.resources.RenderTextures(), pass.side );
    }
    else
    {
        pass.renderer.Composite( pass.frame, pass.resources.RenderFrame(), pass.resources.RenderGeometry(),
                                 pass.resources.RenderTextures() );
        if ( pass.frame.mode != 4 )
        {
            for ( int side = 0; side < 2; ++side )
            {
                if ( ( pass.frame.mode == 2 && side == 1 ) || ( pass.frame.mode == 3 && side == 0 ) )
                {
                    continue;
                }
                const auto& frame = pass.frame;
                const int width = frame.ImageWidth(), height = frame.ImageHeight();
                const int x = frame.x + ( frame.mode == 0 && !frame.stacked ? side * width : 0 );
                const int y = frame.y + ( frame.mode == 0 && frame.stacked ? side * height : 0 );
                pass.resources.RenderFrame().SetViewport( x, y, width, height );
                pass.contacts->RenderContactManifold( frame.contacts[side], frame.projection * frame.view,
                                                      pass.resources.RenderGeometry(), true );
            }
        }
    }
}


void RuntimeRenderer::RenderPairedViews( const PairedViewFrame& frame )
{
    {
        // Resize shares the backend initialization allocation boundary; retired
        // framebuffer resources remain fenced by the backend.
        Core::Allocation::RuntimeAllocationScope prepare( Core::Allocation::RuntimeAllocationPhase::BackendInit );
        if ( !m_pairedViews.Prepare( m_resources.RenderResources(), m_resources.RenderGeometry(), frame.ImageWidth(),
                                     frame.ImageHeight() ) )
        {
            SB_FATAL( "PairedViews", "Unable to allocate paired render targets" );
        }
    }
    Rendering::RenderGraph& graph = BeginRenderPassGraph();
    auto& transitions = m_resources.RenderGraph();
    const uint32_t first = static_cast<uint32_t>( graph.Passes().size() );
    std::array<RenderGraphResourceHandle, 2> colors, depths;
    std::array<PairPass, 3> invocations = { PairPass { m_pairedViews, m_resources, frame, nullptr, 0 },
                                            PairPass { m_pairedViews, m_resources, frame, nullptr, 1 },
                                            PairPass { m_pairedViews, m_resources, frame, nullptr, 2 } };
    for ( int side = 0; side < 2; ++side )
    {
        auto& target = m_pairedViews.Target( side );
        colors[side] = graph.AddExternalResource( side ? "ViewTwoColor" : "ViewOneColor",
                                                  RenderGraphResourceAccess::PixelShaderResource,
                                                  transitions.ResolveGraphResourceToken( target.GetColorTextureHandle() ) );
        depths[side] = graph.AddExternalResource( side ? "ViewTwoDepth" : "ViewOneDepth",
                                                  RenderGraphResourceAccess::PixelShaderResource,
                                                  transitions.ResolveGraphResourceToken( target.GetDepthTextureHandle() ) );
        const auto pass = graph.AddPass( side ? "ViewTwo" : "ViewOne" );
        graph.AddWrite( pass, colors[side], RenderGraphResourceAccess::RenderTarget );
        graph.AddWrite( pass, depths[side], RenderGraphResourceAccess::DepthWrite );
        graph.SetPassCallback<ExecutePairPass>( pass, invocations[side] );
    }
    const auto composite = graph.AddPass( "ImagePairComposite" );
    for ( int side = 0; side < 2; ++side )
    {
        graph.AddRead( composite, colors[side], RenderGraphResourceAccess::PixelShaderResource );
        graph.AddRead( composite, depths[side], RenderGraphResourceAccess::PixelShaderResource );
    }
    const auto backbuffer = transitions.ResolveGraphBackbufferBinding();
    const auto destination = graph.AddExternalResource( "SwapchainBackbuffer", backbuffer.currentAccess,
                                                        backbuffer.nativeResource );
    graph.AddWrite( composite, destination, RenderGraphResourceAccess::RenderTarget );
    graph.SetPassCallback<ExecutePairPass>( composite, invocations[2] );
    for ( auto& invocation : invocations )
    {
        invocation.contacts = &m_physicsDebugVisualizer;
    }
    const auto& compiled = CompileRenderPassGraph( graph );
    for ( auto& invocation : invocations )
    {
        invocation.compiled = &compiled;
    }
    // These callbacks borrow stack packets only for this synchronous range.
    graph.ExecuteCallbacks( RenderGraphCallbackExecutionMode::DryRun, first, 3 );
    const auto executed = graph.ExecuteCallbacks( RenderGraphCallbackExecutionMode::Execute, first, 3 );
    if ( executed.executedPassCount != 3 )
    {
        SB_FATAL( "PairedViews", "Missing image pass" );
    }
    m_resources.RenderFrame().SetViewport( 0, 0, m_window.ClientWidth(), m_window.ClientHeight() );
}
