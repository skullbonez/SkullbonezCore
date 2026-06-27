/*
File: SkullbonezSource/Physics/PhysicsEngine.h
Purpose:
  Exposes the public physics facade while preserving the existing PhysicsScene implementation.

Mental model:
  PhysicsEngine is the runtime-facing physics boundary. During migration it owns
  PhysicsScene and forwards compatibility-view operations in the same order, so
  later scene/tool/replay callers can move to named physics commands without
  touching solver internals directly.

Glossary:
  Facade: Narrow public boundary that forwards commands while hiding solver
  internals.
  Diagnostics view: Borrowed read-only solver/debug state exposed for tooling.
  Compatibility view: Transitional model-order view used while stores migrate.

Invariants:
  - Forwarders must not reorder solver, store-refresh, replay, or diagnostics calls.
  - PhysicsScene and PhysicsWorld remain implementation details behind this facade.

Related:
  - SkullbonezSource/Physics/PhysicsEngine.cpp
  - SkullbonezSource/Physics/PhysicsApi.h
  - SkullbonezSource/Physics/PhysicsScene.h
*/
#pragma once

#include "PhysicsModelView.h"
#include "PhysicsScene.h"

namespace SkullbonezCore
{

namespace Physics
{
class PhysicsEngine
{
  public:
    PhysicsEngine() = default;

    void Clear();
    void RefreshStores( PhysicsModelView& modelView );
    void RefreshPhysicsStores( PhysicsModelView& modelView );
    void RefreshBodyStore( PhysicsModelView& modelView );
    void ClearPendingBodyImpulses();
    void RefreshColliderStore( PhysicsModelView& modelView );
    void RefreshRenderStore( PhysicsModelView& modelView );
    void Step( PhysicsModelView& modelView, float deltaSeconds );
    void WakeBody( PhysicsModelView& modelView, PhysicsBodyHandle body );
    void SeedBodyAsleep( PhysicsModelView& modelView, PhysicsBodyHandle body );
    void ApplyBodyImpulse( PhysicsModelView& modelView,
                           PhysicsBodyHandle body,
                           const Math::Vector::Vector3& impulse,
                           const Math::Vector::Vector3& localApplicationPoint );
    void SetPendingBodyImpulse( PhysicsModelView& modelView,
                                PhysicsBodyHandle body,
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
