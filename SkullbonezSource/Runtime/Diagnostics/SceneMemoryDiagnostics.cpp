/*
File: SkullbonezSource/Runtime/Diagnostics/SceneMemoryDiagnostics.cpp
Purpose:
  Aggregates scene-owner memory capacity into one diagnostics value.

Summary:
  The diagnostics boundary reads concrete Physics and render owners, then joins
  entity and Gameplay byte values projected by the Scene owner.

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
*/
#include "SceneMemoryDiagnostics.h"

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
    stats.modelVectorBytes = view.renderInstances.PresentationCapacityBytes() + view.entityCapacityBytes;
    stats.colliderStoreBytes = colliderStore.CollectRuntimeCapacityMemoryBytes();

    // Invariant: PhysicsEngine's scene-sized total includes every body hot
    // column, handle table, authored descriptor, buoyancy row, and scratch
    // owner. ColliderStore publishes its complete subset separately, so the
    // remainder is the exact non-collider Physics store contribution.
    const uint64_t sceneSizedStoreBytes = view.physics.CollectSceneSizedStoreMemoryBytes();
    stats.physicsStoreBytes = sceneSizedStoreBytes - stats.colliderStoreBytes;

    stats.renderStoreBytes = static_cast<uint64_t>( view.renderInstances.RecordCapacity() ) *
                             sizeof( Rendering::RenderInstanceRecord );

    stats.physicsWorldBytes = view.physics.CollectPhysicsWorldMemoryBytes();
    stats.gameplayWorldBytes = view.gameplayWorldBytes;

    // Historical category semantics: debug capacity is an informational
    // subset of its owning world totals, never an extra contribution to total.
    stats.debugAndBroadphaseBytes = view.physics.CollectDebugAndBroadphaseMemoryBytes() + view.gameplayDebugBytes;
    stats.totalBytes = stats.modelVectorBytes + stats.physicsStoreBytes + stats.colliderStoreBytes + stats.renderStoreBytes +
                       stats.physicsWorldBytes + stats.gameplayWorldBytes;

    return stats;
}
} // namespace Runtime
} // namespace SkullbonezCore
