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

#include <algorithm>

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
    const std::span<const uint8_t> collisionContacts = Physics::PhysicsEngine::ReadCollisionVisualContacts( physics );
    const std::span<const int> sleepIslandIds = Physics::PhysicsEngine::ReadSleepIslandVisualIds( physics );
    const std::span<const uint8_t> sleepSupported = Physics::PhysicsEngine::ReadSleepSupportedStates( physics );
    const std::span<const uint8_t> sleepInhibited = Physics::PhysicsEngine::ReadSleepInhibitedStates( physics );

    // Invariant: visualizers may index every per-body row up to modelCount. Clamp
    // once at publication so a partial diagnostic snapshot cannot manufacture an
    // oversized cache or lend one overlay rows from a different generation.
    const int collisionModelCount = (std::max)( 0, (std::min)( { modelCount, static_cast<int>( bodies.Records().size() ),
                                                                 static_cast<int>( colliders.Records().size() ),
                                                                 static_cast<int>( renderInstances.Records().size() ),
                                                                 static_cast<int>( collisionContacts.size() ),
                                                                 static_cast<int>( sleepStates.size() ),
                                                                 static_cast<int>( sleepIslandIds.size() ) } ) );
    const int physicsDebugModelCount = (std::max)( 0, (std::min)( { modelCount, static_cast<int>( bodies.Records().size() ),
                                                                    static_cast<int>( colliders.Records().size() ),
                                                                    static_cast<int>( sleepStates.size() ),
                                                                    static_cast<int>( sleepSupported.size() ),
                                                                    static_cast<int>( sleepInhibited.size() ) } ) );
    RuntimeRenderDebugViews debug { { physics },
                                    { colliders, renderInstances, collisionContacts, sleepStates, sleepIslandIds,
                                      collisionModelCount },
                                    { bodies, colliders, sleepStates, sleepSupported, sleepInhibited,
                                      Physics::PhysicsEngine::ReadDebugContacts( physics ),
                                      Physics::PhysicsEngine::ReadPipelineTrace( physics ), physicsDebugModelCount } };
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
