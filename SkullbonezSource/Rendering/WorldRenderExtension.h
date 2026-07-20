/*
File: SkullbonezSource/Rendering/WorldRenderExtension.h
Purpose:
  Defines the synchronous, content-neutral world-render extension seam.

Summary:
  Higher-level composition may bind one typed frame callback. RuntimeRenderer
  opens this scope after terrain and water, and the callback appends exactly one
  graphics pass against the current color/depth resources before returning.

Glossary:
  Extension scope: Stack-only access to the current graph targets and narrow
    frame draw values.
  Surface view: Type-erased, read-only height query valid only for this scope.
  Registration: One typed callback borrow selected by higher composition.

Invariants:
  - Registration and callback payload borrows are consumed synchronously.
  - An extension appends exactly one callback-owned graphics pass.
  - No scene container, replay owner, renderer owner, or backend service is
    reachable through this seam.

Related:
  - SkullbonezSource/Rendering/RenderGraph.h
  - SkullbonezSource/Runtime/Render/RuntimeRenderer.cpp
*/
#pragma once

#include "RenderGraph.h"
#include "../Maths/Matrix4.h"
#include "../Maths/Vector3.h"

#include <cstddef>
#include <memory>

namespace SkullbonezCore::Rendering
{
class IRenderCommandContext;
class IRenderDiagnostics;
class RenderGpuTimingOwner;

class WorldSurfaceHeightView
{
  public:
    WorldSurfaceHeightView() = default;

    // Lifetime: the erased surface is a one-frame borrow. Type erasure keeps
    // Geometry out of this generic render contract; callers can only issue the
    // single height query and cannot recover the underlying owner.
    template <typename Surface, auto Sample> static WorldSurfaceHeightView Bind( Surface& surface )
    {
        WorldSurfaceHeightView view;
        view.m_surface = std::addressof( surface );
        view.m_sample = []( void* erased, float x, float z, float fallback )
        { return Sample( *static_cast<Surface*>( erased ), x, z, fallback ); };
        return view;
    }

    float SampleHeight( float x, float z, float fallback ) const
    {
        return m_sample ? m_sample( m_surface, x, z, fallback ) : fallback;
    }

  private:
    void* m_surface = nullptr;
    float ( *m_sample )( void*, float, float, float ) = nullptr;
};

struct WorldRenderExtensionFrameView
{
    Math::Transformation::Matrix4 viewProjection;
    Math::Vector::Vector3 eye;
    Math::Vector::Vector3 viewCenter;
    Math::Vector::Vector3 up;
    IRenderCommandContext& renderCommands;
    IRenderDiagnostics& renderDiagnostics;
    RenderGpuTimingOwner& renderGpuTiming;
    WorldSurfaceHeightView surfaceHeight;
};

class WorldRenderExtensionScope
{
  public:
    WorldRenderExtensionScope( RenderGraph& graph,
                               RenderGraphCompileResult& compileScratch,
                               RenderGraphResourceHandle colorTarget,
                               RenderGraphResourceHandle depthTarget,
                               const WorldRenderExtensionFrameView& frame )
        : m_graph( graph ), m_compileScratch( compileScratch ), m_colorTarget( colorTarget ),
          m_depthTarget( depthTarget ), m_frame( frame )
    {
    }

    const WorldRenderExtensionFrameView& Frame() const
    {
        return m_frame;
    }

    template <auto Callback, typename Payload>
    void AppendGraphicsPass( const char* passName, Payload& payload, const char* timingLabel )
    {
        const std::size_t firstPass = m_graph.Passes().size();
        const uint32_t pass = m_graph.AddPass( passName, RenderGraphQueueType::Graphics );
        m_graph.AddWrite( pass, m_colorTarget, RenderGraphResourceAccess::RenderTarget );
        m_graph.AddWrite( pass, m_depthTarget, RenderGraphResourceAccess::DepthWrite );
        m_graph.SetPassCallback<Callback>( pass, payload, true, timingLabel );
        m_graph.Compile( m_compileScratch );

        // Invariant: the callback payload is stack-owned. Both validation and
        // live execution finish before this method returns and before graph
        // callback borrows can be released at the frame boundary.
        if ( m_graph.Passes().size() != firstPass + 1u )
        {
            SB_FATAL( "Rendering/WorldRenderExtension",
                      "World extension must append exactly one pass. before=%zu after=%zu",
                      firstPass,
                      m_graph.Passes().size() );
        }
        m_graph.ExecuteCallbacks( RenderGraphCallbackExecutionMode::DryRun, static_cast<uint32_t>( firstPass ), 1u );
        const RenderGraphCallbackExecutionResult executed =
            m_graph.ExecuteCallbacks( RenderGraphCallbackExecutionMode::Execute,
                                      static_cast<uint32_t>( firstPass ),
                                      1u );
        if ( executed.executedPassCount != 1u )
        {
            SB_FATAL( "Rendering/WorldRenderExtension",
                      "World extension callback was omitted. pass=%s actual=%u",
                      passName ? passName : "unknown",
                      executed.executedPassCount );
        }
    }

  private:
    RenderGraph& m_graph;
    RenderGraphCompileResult& m_compileScratch;
    RenderGraphResourceHandle m_colorTarget;
    RenderGraphResourceHandle m_depthTarget;
    const WorldRenderExtensionFrameView& m_frame;
};

class WorldRenderExtensionRegistration
{
  public:
    WorldRenderExtensionRegistration() = default;
    WorldRenderExtensionRegistration( const WorldRenderExtensionRegistration& ) = delete;
    WorldRenderExtensionRegistration& operator=( const WorldRenderExtensionRegistration& ) = delete;
    WorldRenderExtensionRegistration( WorldRenderExtensionRegistration&& ) = default;
    WorldRenderExtensionRegistration& operator=( WorldRenderExtensionRegistration&& ) = default;

    // Concept: this is the same private typed-erasure pattern as RenderGraph's
    // callback record. The erased value is the content owner selected by higher
    // composition, never a renderer or Run pointer, and is invoked only while
    // the caller's stack registration remains alive.
    template <typename Payload, auto Register> static WorldRenderExtensionRegistration Bind( Payload& payload )
    {
        WorldRenderExtensionRegistration registration;
        registration.m_payload = std::addressof( payload );
        registration.m_register = []( void* erased, WorldRenderExtensionScope& scope )
        { return Register( *static_cast<Payload*>( erased ), scope ); };
        return registration;
    }

    bool Register( WorldRenderExtensionScope& scope ) const
    {
        return m_register ? m_register( m_payload, scope ) : false;
    }

  private:
    void* m_payload = nullptr;
    bool ( *m_register )( void*, WorldRenderExtensionScope& ) = nullptr;
};
} // namespace SkullbonezCore::Rendering
