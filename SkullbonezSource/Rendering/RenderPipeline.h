/*
File: SkullbonezSource/Rendering/RenderPipeline.h
Purpose:
  Owns renderer-facing frame pipeline diagnostics from immutable scene
  snapshots.

Summary:
  The runtime still executes the live pass bodies in order, but the render
  pipeline owns the frame graph description that proves the executed order and
  resource intent.

Glossary:
  Render graph: Engine-level record of passes, resources, and read/write
  intent.
  Snapshot: Value-only frame facts produced by runtime and consumed here.

Invariants:
  - RenderPipeline consumes RenderSceneSnapshot by const reference and never
    reaches into runtime owner collections or Run.
  - The graph dump mirrors the live pass order in RunRender.cpp until command
    callbacks move into the graph.

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

class RenderPipeline
{
  public:
    static std::string BuildExecutedFrameGraphText( const RenderSceneSnapshot& snapshot );
    static void DumpExecutedFrameGraphIfChanged( const RenderSceneSnapshot& snapshot );
};

} // namespace Rendering
} // namespace SkullbonezCore
