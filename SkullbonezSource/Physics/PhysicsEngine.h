/*
File: SkullbonezSource/Physics/PhysicsEngine.h
Purpose:
  Exposes the public physics facade while preserving the existing PhysicsScene implementation.

Summary:
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
  Scene read view: Exact immutable store and diagnostic projection supplied by
    PhysicsScene without granting the facade private access.
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

#include <cstddef>
#include <cstdint>
#include <span>
#include "PhysicsScene.h"

namespace SkullbonezCore
{
namespace Core
{
class EngineConfig;
class Profiler;
} // namespace Core
namespace Runtime
{
} // namespace Runtime

namespace Threading
{
class WorkerPool;
} // namespace Threading

namespace Physics
{
struct PhysicsAuthoredBodyRefreshView;
struct PhysicsAuthoredBodyRegistration;
struct PhysicsBodyUpdateDesc;
struct PhysicsColliderCreateDesc;
struct PhysicsMaterial;

class PhysicsEngine
{
  public:
    PhysicsEngine() = default;
    void BindProfiler( SkullbonezCore::Core::Profiler* profiler ) noexcept;

    void ApplyRuntimeConfig( const SkullbonezCore::Core::EngineConfig& config );
    // Stamps the PhysicsScene-owned runtime policy onto cold authoring
    // descriptors before they become store rows.
    void ApplyAuthoredBodyPolicy( PhysicsBodyCreateDesc& desc ) const;
    void ApplyAuthoredColliderPolicy( PhysicsColliderCreateDesc& desc ) const;
    void ReserveAuthoredBodyCapacity( std::size_t capacity );
    PhysicsAuthoredBodyCount AuthoredBodyDescriptorCount() const;
    // Scene creation uses this before its first owner mutation; false is a
    // topology/reservation invariant, not recoverable authored input.
    bool CanRegisterAuthoredBody( PhysicsAuthoredBodyCount expectedBodyCount ) const;
    bool TrimAuthoredBodyDescriptorsToCount( PhysicsAuthoredBodyCount bodyCount );
    void Clear();
    bool RefreshBodyStoreFromAuthoredDescriptors( const PhysicsAuthoredBodyRefreshView& refreshView );
    // One physics-owned registration command publishes the authored descriptor,
    // live body, and paired collider or rolls all three back.
    PhysicsAuthoredBodyRegistration RegisterAuthoredBody( const PhysicsBodyCreateDesc& body,
                                                          PhysicsColliderCreateDesc collider );
    // Deterministically removes the paired collider/descriptor/body rows and
    // invalidates the retired body handle before returning.
    bool DestroyAuthoredBody( PhysicsBodyHandle body );
    // Cold editor/replay authoring edits enter by stable handle; no caller can
    // mutate a descriptor row independently from its live body record.
    bool UpdateAuthoredBody( const PhysicsBodyUpdateDesc& update );
    bool UpdateAuthoredBodyAndCollider( const PhysicsBodyUpdateDesc& update, PhysicsColliderCreateDesc collider );
    void ClearPendingBodyImpulses();
    // Replay restore trims authoritative physics bodies directly; callers must
    // not force a model-to-store refresh after this succeeds.
    bool TrimBodiesToCount( PhysicsBodyCount bodyCount );
    bool TrimCollidersToCount( PhysicsColliderCount colliderCount );
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
    // Rebinds existing collider rows from physics body identity. Missing collider
    // rows are a topology bug, not a cue to rebuild shape facts from authoring
    // storage.
    bool RefreshColliderSnapshot();
    // Steps the owned stores. Model-order descriptor import lives with the
    // collection owner, and diagnosticsCsvWriter carries cold Debug CSV output
    // authority instead of letting physics reach through global logging.
    void Step( float deltaSeconds,
               const SkullbonezCore::Core::EngineConfig& config,
               const PhysicsWorldForces& worldForces,
               Threading::WorkerPool& workerPool,
               const char* const* diagnosticNames,
               int diagnosticNameCount,
               const PhysicsDiagnosticsCsvWriter& diagnosticsCsvWriter );
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
    bool IsSleepEnabled() const;
    void BeginCollisionVisualFrame( PhysicsBodyCount bodyCount );
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
    void CaptureReplaySolverSnapshot( PhysicsSolverSnapshot& outSnapshot, PhysicsBodyCount bodyCount ) const;
    bool RestoreReplaySolverSnapshot( const PhysicsSolverSnapshot& snapshot, PhysicsBodyCount bodyCount );
    PhysicsDiagnosticsView GetDiagnosticsView() const;
    uint64_t CollectPhysicsWorldMemoryBytes() const;
    uint64_t CollectDebugAndBroadphaseMemoryBytes() const;
    bool ShouldEmitStepDiagnostics() const;
    bool ShouldEmitCollisionTimeDiagnostics() const;

    // Immutable dense views are an explicit PhysicsEngine query contract for
    // renderer, replay, diagnostics, and cold tools. Each is projected from
    // PhysicsSceneReadView without friendship or mutable-store authority.
    static const PhysicsBodyStore& ReadBodies( const PhysicsEngine& engine );
    static const ColliderStore& ReadColliders( const PhysicsEngine& engine );
    static const Math::CollisionDetection::SpatialGrid& ReadSpatialGrid( const PhysicsEngine& engine );
    static std::span<const int> ReadFixedContactHighlightBodies( const PhysicsEngine& engine );
    static const std::vector<int64_t>& ReadCollisionCellKeys( const PhysicsEngine& engine );
    static const std::vector<uint8_t>& ReadCollisionVisualContacts( const PhysicsEngine& engine );
    static std::span<const uint8_t> ReadSleepStates( const PhysicsEngine& engine );
    static std::span<const int> ReadSleepIslandVisualIds( const PhysicsEngine& engine );
    static std::span<const uint8_t> ReadSleepSupportedStates( const PhysicsEngine& engine );
    static std::span<const uint8_t> ReadSleepInhibitedStates( const PhysicsEngine& engine );
    static const std::vector<PhysicsDebugContact>& ReadDebugContacts( const PhysicsEngine& engine );
    static const std::vector<PhysicsPipelineRecord>& ReadPipelineTrace( const PhysicsEngine& engine );
    static const std::vector<PointJointConstraint>& ReadPointJointConstraints( const PhysicsEngine& engine );

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
