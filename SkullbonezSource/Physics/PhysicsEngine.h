/*
File: SkullbonezSource/Physics/PhysicsEngine.h
Purpose:
  Exposes the public physics facade while preserving the existing PhysicsScene implementation.

Mental model:
  PhysicsEngine is the runtime-facing physics boundary. It owns PhysicsScene and
  forwards store/compatibility operations in a fixed order so scene, tool, and
  replay callers use named physics commands without touching solver internals.

Glossary:
  Facade: Narrow public boundary that forwards commands while hiding solver
  internals.
  Fixed-tree release: Store-owned command that turns authored fixed props into
    dynamic bodies and wakes same-tree parts after an accepted impulse.
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

#include "PhysicsModelAccess.h"
#include "PhysicsScene.h"

namespace SkullbonezCore
{
namespace Basics
{
class EngineConfig;
} // namespace Basics

namespace Threading
{
class WorkerPool;
} // namespace Threading

namespace Physics
{
class PhysicsEngine
{
  public:
    PhysicsEngine() = default;

    void ApplyRuntimeConfig( const Basics::EngineConfig& config );
    void Clear();
    void RefreshBodyStore( PhysicsModelAccess& modelAccess );
    void RefreshBodyFromModel( PhysicsModelAccess& modelAccess, int modelIndex );
    // Scene/model construction receives a body handle at append time instead of
    // resolving a just-created model index through the compatibility adapter.
    PhysicsBodyHandle RegisterAuthoredBody( const PhysicsBodyRecord& record );
    void ClearPendingBodyImpulses();
    // Replay restore trims the authoritative body store directly; callers must
    // not force a model-to-store refresh after this succeeds.
    bool TrimBodyStoreToCount( int bodyCount );
    // Store-owned replay restore facade. Callers resolve a body handle at the
    // owner edge so physics does not accept transient model slots as authority.
    bool RestoreReplayBodyState( PhysicsBodyHandle body,
                                 uint32_t replayBodyId,
                                 bool fixed,
                                 const Math::Vector::Vector3& position,
                                 const Math::Orientation::Quaternion& orientation,
                                 const Math::Vector::Vector3& linearVelocity,
                                 const Math::Vector::Vector3& angularVelocity,
                                 float mass,
                                 float inverseMass,
                                 const Math::Vector::Vector3& rotationalInertia,
                                 const Math::Vector::Vector3& inverseRotationalInertia );
    // Refreshes collider records from model-owned authoring data while keeping
    // the current PhysicsBodyStore authority intact.
    void RefreshColliderSnapshot( PhysicsModelAccess& modelAccess );
    void RefreshRenderStore( PhysicsModelAccess& modelAccess );
    // Steps the owned stores. Model-order import/export lives with
    // GameModelCollection so the solver path does not borrow PhysicsModelAccess.
    void Step( float deltaSeconds,
               const Basics::EngineConfig& config,
               const PhysicsWorldForces& worldForces,
               Threading::WorkerPool& workerPool,
               const char* const* diagnosticNames,
               int diagnosticNameCount );
    // Runtime fixed-tree commands enter physics by handle; release, wake, and
    // sleep propagation stay inside the owned stores.
    bool ReleaseFixedBodyAndAttachedTreeParts( PhysicsBodyHandle sourceBody,
                                               float releaseImpulseStrength,
                                               const Math::Vector::Vector3& seedLinearVelocity,
                                               const Math::Vector::Vector3& seedAngularVelocity );
    // Wakes solver sleep/island state by handle. Legacy model-index callers
    // must refresh topology before entering this command.
    void WakeBody( PhysicsBodyHandle body );
    // Live tool commands edit body velocity by handle; legacy model-index
    // callers refresh topology before entering this store-owned command.
    bool SetBodyVelocity( PhysicsBodyHandle body,
                          const Math::Vector::Vector3& linearVelocity,
                          const Math::Vector::Vector3& angularVelocity,
                          bool wakeIfMoving );
    // Scene/editor construction commands seed solver sleep state by handle
    // without a per-command GameModel projection.
    void SeedBodyAsleep( PhysicsBodyHandle body );
    // Queues one-shot solver input by body handle. Callers that only need a
    // pending impulse must not borrow PhysicsModelAccess for presentation wake.
    void SetPendingBodyImpulse( PhysicsBodyHandle body,
                                const Math::Vector::Vector3& impulse,
                                const Math::Vector::Vector3& localApplicationPoint );
    // Queues a one-shot impulse and wakes by body handle without borrowing the
    // model owner.
    void ApplyBodyImpulse( PhysicsBodyHandle body,
                           const Math::Vector::Vector3& impulse,
                           const Math::Vector::Vector3& localApplicationPoint );
    void SetSleepEnabled( bool enabled );
    void BeginCollisionVisualFrame( int modelCount );
    void EndCollisionVisualFrame();
    void ClearPointJointConstraints();
    // Creates a point joint from physics body handles and rejects stale or
    // same-body endpoints before the solver stores its internal row.
    PhysicsConstraintHandle CreatePointJoint( const PhysicsPointJointCreateDesc& desc );
    void SetTornadoFieldConfig( const TornadoFieldConfig& config );
    const TornadoFieldConfig& GetTornadoFieldConfig() const;
    void SetTornadoSystemConfig( const TornadoSystemConfig& config );
    const TornadoSystemConfig& GetTornadoSystemConfig() const;
    float GetTornadoSystemElapsedSeconds() const;
    void RenderTornadoFieldVectors( const Math::Transformation::Matrix4& viewProj );
    void CaptureReplaySolverSnapshot( Basics::ReplaySolverWorldSnapshot& outSnapshot, int modelCount ) const;
    bool RestoreReplaySolverSnapshot( const Basics::ReplaySolverWorldSnapshot& snapshot, int modelCount );
    PhysicsDiagnosticsView GetDiagnosticsView() const;
    uint64_t CollectPhysicsWorldMemoryBytes() const;
    uint64_t CollectDebugAndBroadphaseMemoryBytes() const;
    bool ShouldEmitStepDiagnostics() const;
    bool ShouldEmitCollisionTimeDiagnostics() const;
    const std::vector<int>& GetFixedContactHighlightBodies() const;

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
