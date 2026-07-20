/*
File: SkullbonezSource/Runtime/Diagnostics/SceneMemoryDiagnostics.h
Purpose:
  Declares aggregate scene-memory accounting over concrete const owners.

Summary:
  Diagnostics joins entity, Physics, and render owner capacity with precomputed
  Gameplay byte values while building one value snapshot. No lifecycle
  controller or mutable cross-domain context is retained.

Glossary:
  Owner view: Three synchronous const store references plus Gameplay byte
    values projected by SceneWorld.
  Aggregate snapshot: Value-only MainMemoryGameObjectStats returned to UI,
    stress, renderer, or shutdown diagnostics.

Invariants:
  - Every reference is const and valid only for the collection call.
  - Accounting reads capacity; it does not reserve, grow, or mutate storage.
  - The returned snapshot owns no pointer or reference.

Related:
  - SkullbonezSource/Runtime/Scene/SceneEntityStore.h
  - SkullbonezSource/Physics/PhysicsEngine.h
  - SkullbonezSource/Rendering/RenderInstanceStore.h
  - Agentic/Reference/comment-style-guide.md
*/
#pragma once

#include "../../Core/MainMemoryStats.h"

namespace SkullbonezCore
{
namespace Physics
{
class PhysicsEngine;
}
namespace Rendering
{
class RenderInstanceStore;
}
namespace Runtime
{
class SceneEntityStore;

struct SceneMemoryDiagnosticsView
{
    const SceneEntityStore& entities;
    uint64_t gameplayWorldBytes;
    uint64_t gameplayDebugBytes;
    const Physics::PhysicsEngine& physics;
    const Rendering::RenderInstanceStore& renderInstances;
};

// Returns one value snapshot without retaining or mutating any supplied owner.
SkullbonezCore::Core::MainMemoryGameObjectStats CollectSceneMemoryStats( const SceneMemoryDiagnosticsView& view );
} // namespace Runtime
} // namespace SkullbonezCore
