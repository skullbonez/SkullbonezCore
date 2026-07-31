/*
File: SkullbonezSource/Runtime/Render/RenderModelFramePublisher.h
Purpose:
  Declares the scene-to-render frame publication boundary.

Summary:
  The publisher projects scene, physics, diagnostics, worker, and render-policy
  owners into one stack-only render model view. RuntimeRenderer consumes that
  view without gaining authority to traverse or diagnose the source owners.

Invariants:
  - Publication performs no allocation and retains no reference itself.
  - Scene and physics owners outlive every borrow in the returned frame view.
  - RuntimeRenderer receives the completed view and cannot reopen scene state.

Related:
  - SkullbonezSource/Runtime/Render/RuntimeRenderFrameValues.h defines the view.
  - SkullbonezSource/Runtime/App/RunFrame.cpp sequences frame publication.
  - Agentic/Reference/comment-style-guide.md
  - Agentic/Reference/engine-glossary.md
*/
#pragma once

#include "RuntimeRenderFrameValues.h"

namespace SkullbonezCore
{
namespace Core
{
class EngineConfig;
}
namespace Threading
{
class WorkerPool;
}
namespace Runtime
{
class SceneWorld;

// Lifetime: the returned record borrows from scene, physics, and worker owners;
// consume it synchronously during the frame and never retain it.
RuntimeRenderModelFrameView PublishRenderModelFrame( SceneWorld& scene, Threading::WorkerPool& workerPool,
                                                     const Core::EngineConfig& config );
} // namespace Runtime
} // namespace SkullbonezCore
