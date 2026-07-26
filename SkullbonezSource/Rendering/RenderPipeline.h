/*
File: SkullbonezSource/Rendering/RenderPipeline.h
Purpose:
  Writes renderer-facing diagnostics from the live production frame graph.

Summary:
  RuntimeRenderer supplies the exact graph that scheduled the frame plus a
  value-only outcome snapshot. RenderPipeline formats and caches that evidence;
  it does not reconstruct scheduling or resource declarations.

Glossary:
  Live graph: Production callback schedule accumulated across the frame.
  Snapshot: Value-only outcomes produced by executed pass callbacks.

Invariants:
  - RenderPipeline never creates a RenderGraph or substitutes marker callbacks.
  - The graph borrow is consumed synchronously during diagnostic formatting.

Related:
  - SkullbonezSource/Rendering/RenderSceneSnapshot.h
  - SkullbonezSource/Rendering/RenderGraph.h
  - Agentic/Reference/comment-style-guide.md
*/
#pragma once

#include "RenderSceneSnapshot.h"

#include <string>

namespace SkullbonezCore
{
namespace Rendering
{

class RenderGraph;

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
