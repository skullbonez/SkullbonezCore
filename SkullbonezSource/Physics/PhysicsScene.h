/*
File: SkullbonezSource/Physics/PhysicsScene.h
Purpose:
  Owns deterministic physics-scene state and store snapshots.

Mental model:
  PhysicsScene is the boundary between the compatibility GameModelCollection
  facade and the future authoritative physics/render stores. PhysicsWorld
  remains the solver owner for now, while stores mirror the same model order for
  replay, diagnostics, and migration checks.

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
class SkullScope;
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
    void SetPhysicsSleepEnabled( bool enabled );
    void BeginCollisionVisualFrame( int modelCount );
    void EndCollisionVisualFrame();
    void SetTornadoFieldConfig( const TornadoFieldConfig& config );
    const TornadoFieldConfig& GetTornadoFieldConfig() const;
    void RenderTornadoFieldVectors( const Math::Transformation::Matrix4& viewProj );
    void CaptureReplaySolverSnapshot( Basics::ReplaySolverWorldSnapshot& outSnapshot, int modelCount ) const;
    bool RestoreReplaySolverSnapshot( const Basics::ReplaySolverWorldSnapshot& snapshot, int modelCount );

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

#ifdef _DEBUG
    void SetPhysicsRegressionLogPath( const char* path );
    void SetPhysicsCollisionTimeLogPath( const char* path );
    void SetPhysicsDiagnosticsPath( const char* path );
    void SetPhysicsDiagnosticsRunId( const char* runId );
    bool SetDiagnosticsSuppressed( bool suppressed );
#endif

  private:
    friend class GameObjects::SkullScope;

    PhysicsWorld& DiagnosticsWorldForSkullScope();
    const PhysicsWorld& DiagnosticsWorldForSkullScope() const;

    PhysicsWorld m_world;                                 // Existing deterministic solver and debug state.
    PhysicsBodyStore m_bodyStore;                         // Body snapshot in model/replay order.
    ColliderStore m_colliderStore;                        // Collider snapshot in model/replay order.
    Rendering::RenderInstanceStore m_renderInstanceStore; // Render snapshot in model/replay order.
};
} // namespace Physics
} // namespace SkullbonezCore
