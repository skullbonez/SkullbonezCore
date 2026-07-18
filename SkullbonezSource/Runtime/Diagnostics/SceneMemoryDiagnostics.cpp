/*
File: SkullbonezSource/Runtime/Diagnostics/SceneMemoryDiagnostics.cpp
Purpose:
  Aggregates scene-owner memory capacity into one diagnostics value.

Summary:
  The diagnostics boundary reads the concrete entity, physics, and render
  owners directly. It replaces SceneController memory forwarding without
  granting diagnostics mutation authority over any owner.

Glossary:
  Metadata bytes: Reserved SceneEntityStore and presentation-record capacity.
  Physics-world bytes: Solver, stage, and persistent physics-owner storage.
  Debug/broadphase bytes: Separately reported diagnostic and spatial capacity.

Invariants:
  - Total bytes preserve the historical tracked-engine accounting formula.
  - Debug/broadphase bytes remain reported separately from total bytes.
  - Collection performs no allocation or owner mutation.

Related:
  - SkullbonezSource/Runtime/Diagnostics/SceneMemoryDiagnostics.h
  - SkullbonezSource/Core/MainMemoryStats.h
  - Agentic/Reference/comment-style-guide.md
*/
#include "SceneMemoryDiagnostics.h"

#include "../Scene/SceneEntityStore.h"
#include "../../Physics/ColliderStore.h"
#include "../../Physics/PhysicsApi.h"
#include "../../Physics/PhysicsBodyStore.h"
#include "../../Physics/PhysicsEngine.h"
#include "../../Rendering/RenderInstanceStore.h"

namespace SkullbonezCore
{
namespace Runtime
{
SkullbonezCore::Core::MainMemoryGameObjectStats CollectSceneMemoryStats( const SceneMemoryDiagnosticsView& view )
{
    SkullbonezCore::Core::MainMemoryGameObjectStats stats;
    const Physics::PhysicsBodyStore& bodyStore = Physics::PhysicsEngine::ReadBodies( view.physics );
    const Physics::ColliderStore& colliderStore = Physics::PhysicsEngine::ReadColliders( view.physics );

    stats.modelCount = static_cast<std::size_t>( view.renderInstances.PresentationCount() );
    stats.modelCapacity = view.renderInstances.PresentationCapacity();
    stats.bodyStoreCapacity = bodyStore.RecordCapacity();
    stats.colliderStoreCapacity = colliderStore.RecordCapacity();
    stats.renderStoreCapacity = view.renderInstances.RecordCapacity();
    stats.modelVectorBytes = view.renderInstances.PresentationCapacityBytes() + view.entities.CapacityBytes();
    stats.physicsStoreBytes =
        static_cast<uint64_t>( bodyStore.RecordCapacity() ) * sizeof( Physics::PhysicsBodyRecord );
    stats.colliderStoreBytes =
        static_cast<uint64_t>( colliderStore.RecordCapacity() ) * sizeof( Physics::ColliderRecord );
    stats.renderStoreBytes =
        static_cast<uint64_t>( view.renderInstances.RecordCapacity() ) * sizeof( Rendering::RenderInstanceRecord );
    stats.physicsWorldBytes = view.physics.CollectPhysicsWorldMemoryBytes();
    stats.debugAndBroadphaseBytes = view.physics.CollectDebugAndBroadphaseMemoryBytes();
    stats.totalBytes = stats.modelVectorBytes + stats.physicsStoreBytes + stats.colliderStoreBytes +
                       stats.renderStoreBytes + stats.physicsWorldBytes;
    return stats;
}
} // namespace Runtime
} // namespace SkullbonezCore
