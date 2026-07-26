/*
File: SkullbonezSource/Runtime/Render/RenderModelFramePublisher.cpp
Purpose:
  Projects scene-owned state into the render model frame view.

Summary:
  Frame publication resolves Physics-owned stores and diagnostics once, beside
  the typed render boundary. The renderer receives only the completed borrowed
  record and does not acquire scene traversal or diagnostics authority.

Glossary:
  Frame publication: One-time projection of owner-backed rows and values for
    synchronous render-pass consumption during the current frame.
  Store view: Borrowed contiguous Physics or Rendering records owned elsewhere.

Invariants:
  - Field order matches RuntimeRenderModelFrameView exactly.
  - Physics debug and sleep rows come from the same PhysicsEngine generation.
  - Collection performs no allocation and retains no owner reference.

Related:
  - SkullbonezSource/Runtime/Render/RenderModelFramePublisher.h
  - SkullbonezSource/Runtime/Diagnostics/SceneMemoryDiagnostics.h
  - SkullbonezSource/Runtime/Scene/SceneWorld.h
*/
#include "RenderModelFramePublisher.h"

#include "../Diagnostics/SceneMemoryDiagnostics.h"
#include "../Scene/SceneWorld.h"
#include "../../Core/Config.h"
#include "../../Physics/PhysicsEngine.h"

namespace SkullbonezCore
{
namespace Runtime
{
RuntimeRenderModelFrameView
PublishRenderModelFrame( SceneWorld& scene, Threading::WorkerPool& workerPool, const Core::EngineConfig& config )
{
    Physics::PhysicsEngine& physics = scene.Physics();
    return RuntimeRenderModelFrameView {
        scene.MutableRenderInstances(),
        Physics::PhysicsEngine::ReadColliders( physics ),
        Physics::PhysicsEngine::ReadBodies( physics ),
        physics,
        scene.BuildWorldExtensionDebugLines(),
        scene.RenderPresentationRecords(),
        Physics::PhysicsEngine::ReadCollisionVisualContacts( physics ),
        Physics::PhysicsEngine::ReadSleepStates( physics ),
        Physics::PhysicsEngine::ReadSleepIslandVisualIds( physics ),
        Physics::PhysicsEngine::ReadSleepSupportedStates( physics ),
        Physics::PhysicsEngine::ReadSleepInhibitedStates( physics ),
        Physics::PhysicsEngine::ReadDebugContacts( physics ),
        Physics::PhysicsEngine::ReadPipelineTrace( physics ),
        &workerPool,
        scene.SceneEntityCount(),
        config.runtimeRender.renderCollisionVolumes,
        config.runtimeRender.shadowParallelPrep,
        scene.GetSceneKineticEnergy(),
        CollectSceneMemoryStats( SceneMemoryDiagnosticsView { scene.Entities(),
                                                              scene.CollectGameplayMemoryBytes(),
                                                              scene.CollectGameplayDebugMemoryBytes(),
                                                              physics,
                                                              scene.RenderInstances() } ) };
}
} // namespace Runtime
} // namespace SkullbonezCore
