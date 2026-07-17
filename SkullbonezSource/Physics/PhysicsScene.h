/*
File: SkullbonezSource/Physics/PhysicsScene.h
Purpose:
  Owns deterministic physics-scene state and store snapshots.

Summary:
  PhysicsScene is the boundary between cold authoring descriptors and the
  authoritative physics/render stores. PhysicsBodyStore owns mutable body
  records, while PhysicsWorld owns solver scratch and diagnostics.

Glossary:
  Solver: Physics step that integrates bodies and resolves contacts.
  Store: Ordered snapshot for one concern, such as bodies, colliders, or render
    instances.
  Physics material: Runtime policy for collider friction and sphere drag.
  Body simulation limit: Scalar cap applied to body descriptors before store import.
  Contact policy: Terrain/contact thresholds copied into authored body descriptors.
  Read view: Frame-local immutable projection of the thirteen store and
    diagnostic reads published through PhysicsEngine.
  Fixed-tree release: Store-owned command that turns authored fixed props into
    dynamic bodies and wakes same-tree parts after an accepted impulse.
  Sleep: Solver optimization that stops integrating stable bodies until an
    explicit wake or contact event reactivates them.
  SkullScope: Queryable physics diagnostics trace workflow.
  Determinism: Same inputs produce byte-exact validation output.

Invariants:
  - Body, collider, render, replay, and diagnostics ordering stays aligned.
  - RunPhysics must not rebuild authoring descriptors; presentation projection
    stays at the collection owner after the store-owned step.

Related:
  - SkullbonezSource/Physics/PhysicsScene.cpp
  - SkullbonezSource/Physics/PhysicsWorld.h
  - Agentic/Reports/2026-07-11/physics-authority-and-identity-closure-review.md
*/
#pragma once

#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

#include "ColliderStore.h"
#include "PhysicsBodyStore.h"
#include "PhysicsObjectPolicy.h"
#include "PhysicsWorld.h"
#include "PhysicsWorldForces.h"

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

// Concept: PhysicsSceneReadView is the complete immutable query projection of
// PhysicsScene. It exposes only the thirteen store/diagnostic reads ratified by
// the ownership census; command authority remains on PhysicsScene.
//
// Lifetime: every reference and span borrows PhysicsScene-owned storage. The
// view is frame-local and must not outlive the scene or cross a scene reset.
struct PhysicsSceneReadView
{
    const PhysicsBodyStore& bodies;
    const ColliderStore& colliders;
    const Math::CollisionDetection::SpatialGrid& spatialGrid;
    std::span<const int> fixedContactHighlightBodies;
    const std::vector<int64_t>& collisionCellKeys;
    const std::vector<uint8_t>& collisionVisualContacts;
    std::span<const uint8_t> sleepStates;
    std::span<const int> sleepIslandVisualIds;
    std::span<const uint8_t> sleepSupportedStates;
    std::span<const uint8_t> sleepInhibitedStates;
    const std::vector<PhysicsDebugContact>& debugContacts;
    const std::vector<PhysicsPipelineRecord>& pipelineTrace;
    const std::vector<PointJointConstraint>& pointJointConstraints;
};

class PhysicsScene
{
  public:
    PhysicsScene();
    void BindProfiler( SkullbonezCore::Core::Profiler* profiler ) noexcept;

    void ApplyRuntimeConfig( const SkullbonezCore::Core::EngineConfig& config );
    // Stamps current runtime policy onto cold authoring descriptors. Descriptor
    // storage may still live outside PhysicsScene, but policy values do not.
    void ApplyAuthoredBodyPolicy( PhysicsBodyCreateDesc& desc ) const;
    void ApplyAuthoredColliderPolicy( PhysicsColliderCreateDesc& desc ) const;
    // Caller contract: authored body descriptors are cold scene-authoring rows
    // keyed by model order. Collection may supply replay/grouping scalars, but
    // it must not keep a competing body descriptor sidecar.
    void ReserveAuthoredBodyCapacity( std::size_t capacity );
    PhysicsAuthoredBodyCount AuthoredBodyDescriptorCount() const;
    // Creation preflight proves descriptor and fixed body storage can append
    // without mutation or allocation before the cross-owner commit begins.
    bool CanRegisterAuthoredBody( PhysicsAuthoredBodyCount expectedBodyCount ) const;
    bool TrimAuthoredBodyDescriptorsToCount( PhysicsAuthoredBodyCount bodyCount );
    void Clear();
    bool RefreshBodyStoreFromAuthoredDescriptors( const PhysicsAuthoredBodyRefreshView& refreshView );
    // Construction edge: publishes one descriptor/body/collider topology unit.
    PhysicsAuthoredBodyRegistration RegisterAuthoredBody( const PhysicsBodyCreateDesc& body,
                                                          PhysicsColliderCreateDesc collider );
    // Retires the body handle and removes its authored descriptor, collider,
    // and joints before any handle slot can be reused.
    bool DestroyAuthoredBody( PhysicsBodyHandle body );
    // Handle-based authoring commands update both the live record and its cold
    // descriptor; the paired variant also preserves collider handle identity.
    bool UpdateAuthoredBody( const PhysicsBodyUpdateDesc& update );
    bool UpdateAuthoredBodyAndCollider( const PhysicsBodyUpdateDesc& update, PhysicsColliderCreateDesc collider );
    void ClearPendingBodyImpulses();
    // Replay restore trims the authoritative body store directly; callers must
    // not force a model-to-store refresh after this succeeds.
    bool TrimBodiesToCount( PhysicsBodyCount bodyCount );
    bool TrimCollidersToCount( PhysicsColliderCount colliderCount );
    // Store-owned replay restore facade used by runtime replay without
    // treating model-order slots as the source of truth for simulation state.
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
    // Rebinds existing collider rows against the already-current body store.
    // Count drift must be fixed by the creator/editor path that owns shape data.
    bool RefreshColliderSnapshot();
    void RunPhysics( float fChangeInTime,
                     const SkullbonezCore::Core::EngineConfig& config,
                     const PhysicsWorldForces& worldForces,
                     Threading::WorkerPool& workerPool,
                     const char* const* diagnosticNames,
                     int diagnosticNameCount,
                     const PhysicsDiagnosticsCsvWriter& diagnosticsCsvWriter );
    // Releases an authored fixed body, then same-tree parts, using body-store
    // policy and waking touched rows without a per-release model-side body
    // projection.
    bool ReleaseFixedBodyAndAttachedTreeParts( PhysicsBodyHandle sourceBody,
                                               float releaseImpulseStrength,
                                               const Math::Vector::Vector3& seedLinearVelocity,
                                               const Math::Vector::Vector3& seedAngularVelocity );
    // Wakes solver sleep/island state by handle. Model-index callers
    // must refresh topology before entering this command.
    void WakeBody( PhysicsBodyHandle body );
    // Sets replay/editor-authored live velocities by body handle and optionally
    // wakes a moving body before the normal step projects presentation state.
    bool SetBodyVelocity( PhysicsBodyHandle body,
                          const Math::Vector::Vector3& linearVelocity,
                          const Math::Vector::Vector3& angularVelocity,
                          bool wakeIfMoving );
    void SeedBodyAsleep( PhysicsBodyHandle body );
    // Queues one-shot solver input by body handle. The command is store-owned;
    // ApplyBodyImpulse owns the separate wake/presentation edge.
    void SetPendingBodyImpulse( PhysicsBodyHandle body,
                                const Math::Vector::Vector3& impulse,
                                const Math::Vector::Vector3& localApplicationPoint );
    // Queues a one-shot impulse and wakes by body handle without borrowing the
    // model owner.
    void ApplyBodyImpulse( PhysicsBodyHandle body,
                           const Math::Vector::Vector3& impulse,
                           const Math::Vector::Vector3& localApplicationPoint );
    void SetPhysicsSleepEnabled( bool enabled );
    bool IsPhysicsSleepEnabled() const;
    void BeginCollisionVisualFrame( PhysicsBodyCount bodyCount );
    void EndCollisionVisualFrame();
    void ClearPointJointConstraints();
    PhysicsConstraintHandle CreatePointJoint( const PhysicsPointJointCreateDesc& desc );
    void SetTornadoFieldConfig( const TornadoFieldConfig& config );
    const TornadoFieldConfig& GetTornadoFieldConfig() const;
    void SetTornadoSystemConfig( const TornadoSystemConfig& config );
    const TornadoSystemConfig& GetTornadoSystemConfig() const;
    float GetTornadoSystemElapsedSeconds() const;
    void CaptureReplaySolverSnapshot( Runtime::ReplaySolverWorldSnapshot& outSnapshot,
                                      PhysicsBodyCount bodyCount ) const;
    bool RestoreReplaySolverSnapshot( const Runtime::ReplaySolverWorldSnapshot& snapshot, PhysicsBodyCount bodyCount );
    PhysicsDiagnosticsView GetDiagnosticsView() const;
    uint64_t CollectPhysicsWorldMemoryBytes() const;
    uint64_t CollectDebugAndBroadphaseMemoryBytes() const;
    bool ShouldEmitStepDiagnostics() const;
    bool ShouldEmitCollisionTimeDiagnostics() const;
    // Returns the exact immutable query surface consumed by PhysicsEngine.
    // Mutation continues through the named commands above.
    PhysicsSceneReadView ReadView() const noexcept;

#ifdef _DEBUG
    void SetPhysicsRegressionLogPath( const char* path );
    void SetPhysicsCollisionTimeLogPath( const char* path );
    void SetPhysicsDiagnosticsPath( const char* path );
    void SetPhysicsDiagnosticsRunId( const char* runId );
    bool SetDiagnosticsSuppressed( bool suppressed );
#endif

  private:
    void LoadBodyDescriptors( const std::vector<PhysicsBodyCreateDesc>& bodyDescs );

    void ApplyFixedTreeReleaseEvents( const PhysicsWorldForces& worldForces );
#ifdef _DEBUG
    void ValidatePhysicsStoreMappings( int modelCount ) const;
#endif

    PhysicsWorld m_world;                                   // Deterministic solver and debug state over body-store records.
    std::vector<PhysicsBodyCreateDesc> m_authoredBodyDescs; // Cold body authoring descriptors keyed by scene/model order.
    PhysicsBodyStore m_bodyStore;                           // Mutable body state in model/replay order.
    ColliderStore m_colliderStore;                          // Collider snapshot in model/replay order.
    PhysicsMaterial m_physicsMaterial;                      // Runtime material policy copied into body/collider descriptors.
    BodySimulationLimits m_bodySimulationLimits;            // Runtime body caps copied at authoring/import boundaries.
    ContactPolicy m_contactPolicy;                          // Runtime contact thresholds copied at authoring/import boundaries.
    PhysicsWorldForces m_lastWorldForces;                   // Last real step boundary forces used by explicit wake commands.
    bool m_hasLastWorldForces = false;                      // False until the first physics step supplies world forces.
    std::vector<int> m_fixedTreeReleaseWakeBodies;          // Reused scene-edge wake list; avoids release-time allocation churn.
};
} // namespace Physics
} // namespace SkullbonezCore
