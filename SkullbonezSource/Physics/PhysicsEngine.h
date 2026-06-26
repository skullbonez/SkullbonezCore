/*
File: SkullbonezSource/Physics/PhysicsEngine.h
Purpose:
  Exposes the public physics facade while preserving the existing PhysicsScene implementation.

Mental model:
  PhysicsEngine is the runtime-facing physics boundary. During migration it owns
  PhysicsScene and forwards old GameModelCollection-backed operations in the
  same order, so later scene/tool/replay callers can move to named physics
  commands without touching solver internals directly.

Invariants:
  - Forwarders must not reorder solver, store-refresh, replay, or diagnostics calls.
  - PhysicsScene and PhysicsWorld remain implementation details behind this facade.

Related:
  - SkullbonezSource/Physics/PhysicsEngine.cpp
  - SkullbonezSource/Physics/PhysicsApi.h
  - SkullbonezSource/Physics/PhysicsScene.h
*/
#pragma once

#include "PhysicsScene.h"

namespace SkullbonezCore
{
namespace GameObjects
{
class GameModelCollection;
} // namespace GameObjects

namespace Physics
{
class PhysicsEngine
{
  public:
    PhysicsEngine() = default;

    void Clear();
    void RefreshStores( GameObjects::GameModelCollection& collection );
    void RefreshPhysicsStores( GameObjects::GameModelCollection& collection );
    void RefreshBodyStore( GameObjects::GameModelCollection& collection );
    void RefreshColliderStore( GameObjects::GameModelCollection& collection );
    void RefreshRenderStore( GameObjects::GameModelCollection& collection );
    void Step( GameObjects::GameModelCollection& collection, float deltaSeconds );
    void WakeBody( GameObjects::GameModelCollection& collection, int bodyIndex );
    void SeedBodyAsleep( GameObjects::GameModelCollection& collection, int bodyIndex );
    void ApplyBodyImpulse( GameObjects::GameModelCollection& collection,
                           int bodyIndex,
                           const Math::Vector::Vector3& impulse,
                           const Math::Vector::Vector3& localApplicationPoint );
    void SetPendingBodyImpulse( GameObjects::GameModelCollection& collection,
                                int bodyIndex,
                                const Math::Vector::Vector3& impulse,
                                const Math::Vector::Vector3& localApplicationPoint );
    void SetSleepEnabled( bool enabled );
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
    PhysicsScene m_scene;
};
} // namespace Physics
} // namespace SkullbonezCore
