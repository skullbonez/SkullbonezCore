/*
File: SkullbonezSource/Physics/PhysicsScene.h
Purpose:
  Owns deterministic physics-scene state and store snapshots.

Mental model:
  PhysicsScene is the boundary between the compatibility GameModelCollection
  facade and the authoritative physics/render stores. PhysicsBodyStore owns the
  mutable body state passed through PhysicsWorld, while GameModel remains the
  compatibility shape/presentation surface until later runtime migrations.

Glossary:
  Solver: Physics step that integrates bodies and resolves contacts.
  Store: Ordered snapshot for one concern, such as bodies, colliders, or render
    instances.
  SkullScope: Queryable physics diagnostics trace workflow.
  Determinism: Same inputs produce byte-exact validation output.

Invariants:
  - Body, collider, render, replay, and diagnostics ordering stays aligned.
  - PhysicsWorld remains authoritative until store migration has its own gate.

Related:
  - SkullbonezSource/Physics/PhysicsScene.cpp
  - SkullbonezSource/Physics/PhysicsWorld.h
  - Agentic/Plans/physics-playground-refactor-and-file-prefix-cleanup-plan.md
*/
#pragma once

#include "ColliderStore.h"
#include "PhysicsBodyStore.h"
#include "PhysicsWorld.h"
#include "../Rendering/RenderInstanceStore.h"

namespace SkullbonezCore
{
namespace GameObjects
{
class GameModelCollection;
} // namespace GameObjects

namespace Physics
{
class PhysicsScene
{
  public:
    PhysicsScene() = default;

    void Clear();
    void RefreshStores( GameObjects::GameModelCollection& collection );
    void RefreshPhysicsStores( GameObjects::GameModelCollection& collection );
    void RefreshBodyStore( GameObjects::GameModelCollection& collection );
    void RefreshColliderStore( GameObjects::GameModelCollection& collection );
    void RefreshRenderStore( GameObjects::GameModelCollection& collection );
    void RunPhysics( GameObjects::GameModelCollection& collection, float fChangeInTime );
    void WakeModel( GameObjects::GameModelCollection& collection, int index );
    void SeedModelAsleep( GameObjects::GameModelCollection& collection, int index );
    void ApplyBodyImpulse( GameObjects::GameModelCollection& collection,
                           int bodyIndex,
                           const Math::Vector::Vector3& impulse,
                           const Math::Vector::Vector3& localApplicationPoint );
    void SetPendingBodyImpulse( GameObjects::GameModelCollection& collection,
                                int bodyIndex,
                                const Math::Vector::Vector3& impulse,
                                const Math::Vector::Vector3& localApplicationPoint );
    void SetPhysicsSleepEnabled( bool enabled );
    void BeginCollisionVisualFrame( int modelCount );
    void EndCollisionVisualFrame();
    void ClearPointJointConstraints();
    void AddPointJointConstraint( const PointJointConstraint& constraint );
    void SetTornadoFieldConfig( const TornadoFieldConfig& config );
    const TornadoFieldConfig& GetTornadoFieldConfig() const;
    void SetTornadoSystemConfig( const TornadoSystemConfig& config );
    const TornadoSystemConfig& GetTornadoSystemConfig() const;
    float GetTornadoSystemElapsedSeconds() const;
    void RenderTornadoFieldVectors( const Math::Transformation::Matrix4& viewProj );
    void CaptureReplaySolverSnapshot( Basics::ReplaySolverWorldSnapshot& outSnapshot, int modelCount ) const;
    bool RestoreReplaySolverSnapshot( const Basics::ReplaySolverWorldSnapshot& snapshot, int modelCount );
    PhysicsWorld::DiagnosticsView GetDiagnosticsView() const;
    uint64_t CollectPhysicsWorldMemoryBytes() const;
    uint64_t CollectDebugAndBroadphaseMemoryBytes() const;

    const PhysicsBodyStore& BodyStore() const;
    const ColliderStore& Colliders() const;
    const Rendering::RenderInstanceStore& RenderInstances() const;
    const Math::CollisionDetection::SpatialGrid& GetSpatialGrid() const;
    const std::vector<int64_t>& GetCollisionCellKeys() const;
    const std::vector<uint8_t>& GetCollisionVisualContacts() const;
    const std::vector<uint8_t>& GetSleepStates() const;
    const std::vector<int>& GetSleepIslandVisualIds() const;
    const std::vector<uint8_t>& GetSleepSupportedStates() const;
    const std::vector<uint8_t>& GetSleepInhibitedStates() const;
    const std::vector<PhysicsDebugContact>& GetPhysicsDebugContacts() const;
    const std::vector<PhysicsPipelineRecord>& GetPhysicsPipelineTrace() const;
    const std::vector<PointJointConstraint>& GetPointJointConstraints() const;

#ifdef _DEBUG
    void SetPhysicsRegressionLogPath( const char* path );
    void SetPhysicsCollisionTimeLogPath( const char* path );
    void SetPhysicsDiagnosticsPath( const char* path );
    void SetPhysicsDiagnosticsRunId( const char* runId );
    bool SetDiagnosticsSuppressed( bool suppressed );
#endif

  private:
#ifdef _DEBUG
    void ValidatePhysicsStoreMappings( int modelCount ) const;
    void ValidateRenderStoreMappings( int modelCount ) const;
#endif

    PhysicsWorld m_world;                                 // Deterministic solver and debug state over body-store records.
    PhysicsBodyStore m_bodyStore;                         // Mutable body state in model/replay order.
    ColliderStore m_colliderStore;                        // Collider snapshot in model/replay order.
    Rendering::RenderInstanceStore m_renderInstanceStore; // Render snapshot in model/replay order.
};
} // namespace Physics
} // namespace SkullbonezCore
