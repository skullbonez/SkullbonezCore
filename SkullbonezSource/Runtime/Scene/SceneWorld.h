/*
File: SkullbonezSource/Runtime/Scene/SceneWorld.h
Purpose:
  Owns the concrete scene-lifetime world: entities, physics, cameras, terrain,
  environment settings, and render-instance presentation.

Summary:
  SceneWorld commits aligned entity/body/collider/render topology and exposes
  stable-identity operations, including transient editor visibility and locks.

Mental model:
  SceneController decides when a scene lifecycle advances. SceneWorld owns what
  that scene is. Callers borrow one explicit SceneWorld, then address the
  concrete store or domain operation they need; the lifecycle controller does
  not mirror those APIs.

Glossary:
  World owner: Concrete lifetime boundary joining the stores replaced together
    by a successful scene load or replay topology restore.
  Dense topology: Entity, body, collider, and render rows sharing one temporary
    model order while stable PhysicsSceneObjectId remains durable identity.
  Presentation capture: Previous/current physics poses retained by the render
    store across one fixed step for interpolation.
  Post-step output: Bounded physics facts borrowed synchronously by presentation.

Invariants:
  - All six owned domains are born, cleared, and replaced as one scene lifetime.
  - Entity, body, collider, and render row counts remain aligned after every
    successful topology mutation.
  - Physics stepping and topology repair occur inside this owner; there is no
    reach-back to SceneController or the process shell.
  - Accessors return borrowed owners and never transfer or duplicate authority.

Related:
  - SkullbonezSource/Runtime/Scene/SceneWorld.cpp
  - SkullbonezSource/Runtime/Scene/SceneController.h
  - Agentic/Reports/2026-07-18/scene-controller-round-2-census.md
*/
#pragma once

#include "SceneEntityStore.h"
#include "SceneTerrain.h"
#include "../../Maths/Vector3.h"
#include "../../Gameplay/TornadoGameplay.h"
#include "../../Physics/PhysicsApi.h"
#include "../../Physics/PhysicsEngine.h"
#include "../../Rendering/RenderInstanceStore.h"
#include "../CameraCollection.h"
#include "../../World/WorldEnvironment.h"

#include <span>
#include <vector>

namespace SkullbonezCore
{
namespace Core
{
class EngineConfig;
}
namespace Physics
{
class PhysicsDebugVisualizer;
struct PhysicsWorldForces;
} // namespace Physics
namespace Threading
{
class WorkerPool;
}
namespace Runtime
{
struct SceneEntityCreateResult
{
    // Lane R: authored input failures leave every store unchanged; success
    // publishes the body handle created by the cross-store commit.
    SkullbonezCore::Core::SbResult status;
    Physics::PhysicsBodyHandle body;
};

struct ScenePhysicsPostStepOutput
{
    // Lifetime: the span borrows the physics owner's fixed-capacity event rows
    // until the next physics step. Dense rows are synchronous-only hints.
    std::span<const int> fixedContactModelIndices;
};

class SceneWorld
{
  public:
    SceneWorld();

    void ApplyRuntimeConfig( const SkullbonezCore::Core::EngineConfig& config );
    // One preflighted command publishes entity, physics, collider, and render
    // rows together. A mismatched post-commit count is a fatal invariant.
    SceneEntityCreateResult TryCreateSceneEntity( SceneEntityCreateDesc entity,
                                                  Physics::PhysicsBodyCreateDesc bodyDesc,
                                                  Physics::PhysicsColliderCreateDesc colliderDesc );
    // Cold editor deletion removes the same four rows as one swap-last commit.
    bool DestroySceneEntity( Physics::PhysicsBodyHandle body );
    void Clear();
    bool TrimForReplayRestore( int bodyCount );

    void BeginPhysicsStepPresentationCapture();
    void CompletePhysicsStepPresentationCapture();
    // Executes the deterministic live/replay physics boundary. Returned dense
    // rows are valid only for the synchronous presentation handoff.
    ScenePhysicsPostStepOutput
    StepPhysics( float fixedDt, const Physics::PhysicsWorldForces& worldForces, Threading::WorkerPool& workerPool );
    void PrepareRenderInstances( float presentationAlpha = 1.0f );
    void BeginCollisionVisualFrame();
    void EndCollisionVisualFrame();

    bool TryGetModelPosition( int index, Math::Vector::Vector3& outPosition ) const;
    bool TryGetPresentationPose( int index,
                                 float presentationAlpha,
                                 Math::Vector::Vector3& outPosition,
                                 Math::Orientation::Quaternion& outOrientation ) const;
    int SceneEntityCount() const;
    // Current prepared physics views. Callers must not retain either span/view
    // across topology mutation or scene replacement.
    const Physics::PhysicsBodyStore& BodyStore() const;
    const Physics::ColliderStore& Colliders() const;
    Rendering::RenderInstanceStore& MutableRenderInstances();
    const Rendering::RenderInstanceStore& RenderInstances() const;
    bool TryQueueReplayRenderPoseOverride( int modelIndex,
                                           uint32_t replayBodyId,
                                           const Math::Vector::Vector3& position,
                                           const Math::Orientation::Quaternion& orientation );
    // Cold callers that need a refreshed snapshot use this command; hot render
    // passes consume RenderInstances() after PrepareRenderInstances().
    const Rendering::RenderInstanceStore& GetRenderInstanceStore();
    double GetSceneKineticEnergy();
    // Runtime-tool edge: resolve a picked row once, then perform release and
    // same-tree propagation through PhysicsEngine-owned handles.
    bool ReleaseAttachedFixedTreeParts( int sourceIndex,
                                        float releaseImpulseStrength,
                                        const Math::Vector::Vector3& seedLinearVelocity,
                                        const Math::Vector::Vector3& seedAngularVelocity );
    void CaptureReplaySolverWorldSnapshot( Physics::PhysicsSolverSnapshot& outSnapshot ) const;
    bool RestoreReplaySolverWorldSnapshot( const Physics::PhysicsSolverSnapshot& snapshot );
    // Explicit cold boundary used before tools borrow paired body/collider
    // handles. Hot passes must never trigger topology repair.
    bool RepairPhysicsBodyAndColliderTopology();

    std::span<const Rendering::RenderInstancePresentationRecord> RenderPresentationRecords() const
    {
        return m_renderInstanceStore.PresentationRecords();
    }

    SceneEntityStore& Entities();
    const SceneEntityStore& Entities() const;
    // Editor flags resolve durable scene identity at this owner boundary.
    // Visibility also updates the paired render row; locks gate edit commands.
    bool SetEditorEntityVisible( Physics::PhysicsSceneObjectId sceneObjectId, bool visible );
    bool SetEditorEntityLocked( Physics::PhysicsSceneObjectId sceneObjectId, bool locked );
    Environment::CameraCollection& Cameras();
    const Environment::CameraCollection& Cameras() const;
    Environment::WorldEnvironment& Environment();
    const Environment::WorldEnvironment& Environment() const;
    SceneTerrain& Terrain();
    const SceneTerrain& Terrain() const;
    Physics::PhysicsEngine& Physics();
    const Physics::PhysicsEngine& Physics() const;
    Gameplay::TornadoGameplay& Tornado();
    const Gameplay::TornadoGameplay& Tornado() const;

  private:
    void ReserveForActiveSceneObjectCapacity();
    std::vector<Physics::ModelRowHint> BuildFixedTreeReleaseRootsForReload() const;
    std::vector<const char*> BuildDiagnosticNamesForReload() const;
    bool RefreshPhysicsBodyStoreFromAuthoredDescriptors();
    bool RepairPhysicsBodyTopology();
    int FixedTreeReleaseRootForModelIndex( int modelIndex ) const;
    void RefreshRenderInstances( float presentationAlpha = 1.0f );
    void AssertSceneCreationTopology( int expectedCount ) const;
    bool CanTrimPresentationRowsForSceneRestore( int modelCount ) const;
    bool TrimPresentationRowsForSceneRestore( int modelCount );

    int m_activeSceneObjectCapacity = SkullbonezCore::Scene::Capacity::DEFAULT_SCENE_OBJECT_CAPACITY;
    SceneEntityStore m_entities;
    Environment::CameraCollection m_cameras;
    Environment::WorldEnvironment m_world;
    SceneTerrain m_terrain;
    Physics::PhysicsEngine m_physics;
    // Scene-lifetime gameplay state is a sibling of Physics; only its bounded
    // value frame crosses the fixed-step boundary.
    Gameplay::TornadoGameplay m_tornadoGameplay;
    Rendering::RenderInstanceStore m_renderInstanceStore;
};
} // namespace Runtime
} // namespace SkullbonezCore
