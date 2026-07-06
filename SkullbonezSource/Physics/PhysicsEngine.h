/*
File: SkullbonezSource/Physics/PhysicsEngine.h
Purpose:
  Exposes the public physics facade while preserving the existing PhysicsScene implementation.

Mental model:
  PhysicsEngine is the runtime-facing physics boundary. It owns PhysicsScene and
  forwards store/descriptor operations in a fixed order so scene, tool, and
  replay callers use named physics commands without touching solver internals.

Glossary:
  Facade: Narrow public boundary that forwards commands while hiding solver
  internals.
  Fixed-tree release: Store-owned command that turns authored fixed props into
    dynamic bodies and wakes same-tree parts after an accepted impulse.
  Physics material: Runtime policy for collider friction and sphere drag.
  Diagnostics view: Borrowed read-only solver/debug state exposed for tooling.
  Descriptor refresh: Cold authoring edge that replaces body rows from explicit
    values supplied by the model collection owner.

Invariants:
  - Forwarders must not reorder solver, store-refresh, replay, or diagnostics calls.
  - PhysicsScene and PhysicsWorld remain implementation details behind this facade.

Related:
  - SkullbonezSource/Physics/PhysicsEngine.cpp
  - SkullbonezSource/Physics/PhysicsApi.h
  - SkullbonezSource/Physics/PhysicsScene.h
*/
#pragma once

#include <vector>

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

namespace Rendering
{
class IRenderCommandContext;
} // namespace Rendering

namespace Physics
{
struct PhysicsColliderCreateDesc;
struct PhysicsMaterial;

class PhysicsEngine
{
  public:
    PhysicsEngine() = default;

    void ApplyRuntimeConfig( const Basics::EngineConfig& config );
    // Applies config material policy to live collider rows without rebuilding
    // shape descriptors from model-order storage.
    void ApplyColliderMaterial( const PhysicsMaterial& material );
    void Clear();
    void RefreshBodyStore( const std::vector<PhysicsBodyCreateDesc>& bodyDescs );
    // Owner passes the expected count so single-row descriptor commits cannot
    // paper over topology drift.
    void RefreshBodyFromDescriptor( const PhysicsBodyCreateDesc& desc, int modelIndex, int expectedModelCount );
    // Scene/model construction receives a body handle at append time instead of
    // resolving the just-created row through a legacy adapter.
    PhysicsBodyHandle RegisterAuthoredBody( const PhysicsBodyCreateDesc& desc );
    // Scene/model construction submits a collider descriptor at the append
    // edge; PhysicsScene owns conversion into the dense ColliderStore row.
    PhysicsColliderHandle RegisterAuthoredCollider( const PhysicsColliderCreateDesc& desc );
    // Replaces one authored collider descriptor without moving its stable
    // ColliderStore handle.
    bool UpdateAuthoredCollider( PhysicsColliderHandle collider, const PhysicsColliderCreateDesc& desc );
    void ClearPendingBodyImpulses();
    // Replay restore trims the authoritative body store directly; callers must
    // not force a model-to-store refresh after this succeeds.
    bool TrimBodyStoreToCount( int bodyCount );
    bool TrimColliderStoreToCount( int colliderCount );
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
    // Rebinds existing collider rows from PhysicsBodyStore. Missing collider
    // rows are a topology bug, not a cue to rebuild shape facts from authoring
    // storage.
    bool RefreshColliderSnapshot();
    // Prepares body/collider rows for the owner-side render projection refresh.
    // The collection owner fills material/highlight facts after this returns.
    bool PrepareRenderStoreRefresh( int expectedModelCount );
    // Applies a one-frame draw-pose override to the prepared render snapshot.
    // Replay uses this after normal projection refresh so historical/future
    // poses affect pixels without mutating body rows or authoring descriptors.
    bool OverrideRenderInstancePose( int modelIndex,
                                     uint32_t replayBodyId,
                                     const Math::Vector::Vector3& position,
                                     const Math::Orientation::Quaternion& orientation );
    // Mutable only for the cold collection-owned render projection edge.
    Rendering::RenderInstanceStore& MutableRenderInstances();
    // Steps the owned stores. Model-order descriptor import lives with
    // the collection owner so the solver path does not rebuild authoring values.
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
    // Wakes solver sleep/island state by handle. Model-index callers
    // must refresh topology before entering this command.
    void WakeBody( PhysicsBodyHandle body );
    // Live tool commands edit body velocity by handle; model-index
    // callers refresh topology before entering this store-owned command.
    bool SetBodyVelocity( PhysicsBodyHandle body,
                          const Math::Vector::Vector3& linearVelocity,
                          const Math::Vector::Vector3& angularVelocity,
                          bool wakeIfMoving );
    // Scene/editor construction commands seed solver sleep state by handle
    // without a per-command presentation projection.
    void SeedBodyAsleep( PhysicsBodyHandle body );
    // Queues one-shot solver input by body handle. Callers that only need a
    // pending impulse must not rebuild descriptor rows for presentation wake.
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
    // Debug overlay edge: caller owns renderer readiness/capabilities; physics
    // only contributes tornado vector geometry.
    void RenderTornadoFieldVectors( const Math::Transformation::Matrix4& viewProj,
                                    Rendering::IRenderCommandContext& renderCommands,
                                    bool supportsDebugLines );
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
    void ValidateRenderStore( int expectedModelCount ) const;
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
