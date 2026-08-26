/*
File: SkullbonezSource/Rendering/RenderPipeline.h
Purpose:
  Writes renderer-facing diagnostics from the live production frame graph.

Summary:
  RuntimeRenderer supplies the exact graph that scheduled the frame plus a
  value-only outcome snapshot. RenderPipeline formats and caches that evidence;
  it does not reconstruct scheduling or resource declarations.

Invariants:
  - RenderPipeline never creates a RenderGraph or substitutes marker callbacks.
  - The graph borrow is consumed synchronously during diagnostic formatting.

Related:
  - SkullbonezSource/Rendering/RenderSceneSnapshot.h
  - SkullbonezSource/Rendering/RenderGraph.h
  - Agentic/Reference/engine-glossary.md
*/
#pragma once

#include "RenderSceneSnapshot.h"

#include <cstdint>
#include <string>

namespace SkullbonezCore
{
namespace Rendering
{

class RenderGraph;

// Owns only the most recent successfully published diagnostic. Failed writes
// never advance this cache, so an identical following frame retries the file.
class RenderPipelineDiagnosticWriteState
{
  public:
    bool MatchesFrame( const RenderSceneSnapshot& snapshot, uint64_t graphFingerprint ) const
    {
        return m_hasSuccessfulFrame && m_graphFingerprint == graphFingerprint && SameSnapshot( snapshot, m_snapshot );
    }

    bool MatchesDumpText( const std::string& dumpText ) const
    {
        return m_hasSuccessfulFrame && m_dumpText == dumpText;
    }

    void RecordPublicationResult( bool published, const RenderSceneSnapshot& snapshot, uint64_t graphFingerprint,
                                  const std::string& dumpText )
    {
        if ( !published )
        {
            return;
        }

        m_snapshot = snapshot;
        m_graphFingerprint = graphFingerprint;
        m_dumpText = dumpText;
        m_hasSuccessfulFrame = true;
    }

  private:
    static bool SameSnapshot( const RenderSceneSnapshot& lhs, const RenderSceneSnapshot& rhs )
    {
        return lhs.cinematicRender == rhs.cinematicRender && lhs.useCinematicTarget == rhs.useCinematicTarget &&
               lhs.terrainShadowValid == rhs.terrainShadowValid && lhs.objectShadowValid == rhs.objectShadowValid &&
               lhs.shadowPassExecuted == rhs.shadowPassExecuted &&
               lhs.reflectionPassExecuted == rhs.reflectionPassExecuted && lhs.reflectionUsedDxr == rhs.reflectionUsedDxr &&
               lhs.objectOpaquePass == rhs.objectOpaquePass && lhs.objectTransparentPass == rhs.objectTransparentPass &&
               lhs.terrainPassRendered == rhs.terrainPassRendered && lhs.waterPassRendered == rhs.waterPassRendered &&
               lhs.waterSamplesReflection == rhs.waterSamplesReflection &&
               lhs.worldExtensionRendered == rhs.worldExtensionRendered &&
               lhs.volumetricPassExecuted == rhs.volumetricPassExecuted && lhs.volumetricReady == rhs.volumetricReady &&
               lhs.volumetricTextureHandle == rhs.volumetricTextureHandle && lhs.volumetricWidth == rhs.volumetricWidth &&
               lhs.volumetricHeight == rhs.volumetricHeight;
    }

    bool m_hasSuccessfulFrame = false;
    RenderSceneSnapshot m_snapshot;
    uint64_t m_graphFingerprint = 0;
    std::string m_dumpText;
};

class RenderPipeline
{
  public:

    // Formats the supplied live schedule and value-only callback outcomes; it
    // never creates or mutates graph declarations.
    static std::string BuildExecutedFrameGraphText( const RenderGraph& graph, const RenderSceneSnapshot& snapshot );

    // Writes diagnostics only when either schedule shape or outcomes change.
    // The graph borrow is consumed synchronously during this call.
    static void DumpExecutedFrameGraphIfChanged( const RenderGraph& graph, const RenderSceneSnapshot& snapshot );
};

} // namespace Rendering
} // namespace SkullbonezCore
