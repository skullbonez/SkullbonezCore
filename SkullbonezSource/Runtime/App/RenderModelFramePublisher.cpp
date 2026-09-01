/*
File: SkullbonezSource/Runtime/App/RenderModelFramePublisher.cpp
Purpose:
  Projects scene-owned state into the render model frame view.

Summary:
  Frame publication resolves Physics-owned stores and diagnostics once, beside
  the typed render boundary. The renderer receives only the completed borrowed
  record and does not acquire scene traversal or diagnostics authority.

Glossary:
  Store view: Borrowed contiguous Physics or Rendering records owned elsewhere.

Invariants:
  - Each published child view matches one consumer-owned frame phase.
  - Physics debug and sleep rows come from the same PhysicsEngine generation.
  - Collection performs no allocation and retains no owner reference.

Related:
  - SkullbonezSource/Runtime/App/RenderModelFramePublisher.h
  - SkullbonezSource/Runtime/Diagnostics/SceneMemoryDiagnostics.h
  - SkullbonezSource/Runtime/Scene/SceneWorld.h
  - Agentic/Reference/engine-glossary.md
*/
#include "RenderModelFramePublisher.h"

#include "../Diagnostics/SceneMemoryDiagnostics.h"
#include "../Scene/SceneWorld.h"
#include "../Render/RuntimeRenderFrameValues.h"
#include "../../Core/Config.h"
#include "../../Physics/PhysicsEngine.h"

namespace SkullbonezCore
{
namespace Runtime
{
RuntimeRenderFrameViews PublishRenderModelFrame( SceneWorld& scene, Threading::WorkerPool& workerPool,
                                                 const Core::EngineConfig& config )
{
    Physics::PhysicsEngine& physics = scene.Physics();
    Rendering::RenderInstanceStore& renderInstances = scene.MutableRenderInstances();
    const Physics::ColliderStore& colliders = Physics::PhysicsEngine::ReadColliders( physics );
    const int modelCount = scene.SceneEntityCount();

    RuntimeRenderModelPresentationView presentation { renderInstances,
                                                      colliders,
                                                      scene.RenderPresentationRecords(),
                                                      &workerPool,
                                                      modelCount,
                                                      config.runtimeRender.renderCollisionVolumes,
                                                      config.runtimeRender.shadowParallelPrep };
    const Physics::PhysicsBodyStore& bodies = Physics::PhysicsEngine::ReadBodies( physics );
    const std::span<const uint8_t> sleepStates = Physics::PhysicsEngine::ReadSleepStates( physics );
    RuntimeRenderDebugViews debug { { physics },
                                    { bodies, colliders, renderInstances,
                                      Physics::PhysicsEngine::ReadCollisionVisualContacts( physics ), sleepStates,
                                      Physics::PhysicsEngine::ReadSleepIslandVisualIds( physics ), modelCount },
                                    { bodies, colliders, sleepStates,
                                      Physics::PhysicsEngine::ReadSleepSupportedStates( physics ),
                                      Physics::PhysicsEngine::ReadSleepInhibitedStates( physics ),
                                      Physics::PhysicsEngine::ReadDebugContacts( physics ),
                                      Physics::PhysicsEngine::ReadPipelineTrace( physics ), modelCount } };
    const RuntimeRenderWorldExtensionDebugView worldExtensionDebug { scene.BuildWorldExtensionDebugLines() };
    const RuntimeRenderDiagnosticsFrameValues
        diagnostics { scene.GetSceneKineticEnergy(),
                      CollectSceneMemoryStats( SceneMemoryDiagnosticsView { scene.Entities().CapacityBytes(),
                                                                            scene.CollectGameplayMemoryBytes(),
                                                                            scene.CollectGameplayDebugMemoryBytes(), physics,
                                                                            scene.RenderInstances() } ) };
    return RuntimeRenderFrameViews { presentation, debug, worldExtensionDebug, diagnostics };
}
} // namespace Runtime
} // namespace SkullbonezCore
