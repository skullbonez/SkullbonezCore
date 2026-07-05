/*
File: SkullbonezSource/Physics/PhysicsScene.h
Purpose:
  Owns deterministic physics-scene state and store snapshots.

Mental model:
  PhysicsScene is the boundary between the compatibility model view and the
  authoritative physics/render stores. PhysicsBodyStore owns mutable body
  records, PhysicsWorld owns solver scratch and diagnostics, and GameModel
  remains only the compatibility authoring/presentation surface until later
  runtime migrations.

Glossary:
  Solver: Physics step that integrates bodies and resolves contacts.
  Store: Ordered snapshot for one concern, such as bodies, colliders, or render
    instances.
  Physics material: Runtime policy for collider friction and sphere drag.
  Fixed-tree release: Store-owned command that turns authored fixed props into
    dynamic bodies and wakes same-tree parts after an accepted impulse.
  Sleep: Solver optimization that stops integrating stable bodies until an
    explicit wake or contact event reactivates them.
  SkullScope: Queryable physics diagnostics trace workflow.
  Determinism: Same inputs produce byte-exact validation output.

Invariants:
  - Body, collider, render, replay, and diagnostics ordering stays aligned.
  - RunPhysics must not borrow PhysicsModelAccess; model projection stays at
    GameModelCollection after the store-owned step.

Related:
  - SkullbonezSource/Physics/PhysicsScene.cpp
  - SkullbonezSource/Physics/PhysicsWorld.h
  - Agentic/Plans/physics-playground-refactor-and-file-prefix-cleanup-plan.md
*/
#pragma once

#include <vector>

#include "ColliderStore.h"
#include "PhysicsBodyStore.h"
#include "PhysicsModelAccess.h"
#include "PhysicsWorld.h"
#include "PhysicsWorldForces.h"
#include "../Rendering/RenderInstanceStore.h"

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
struct PhysicsColliderCreateDesc;
struct PhysicsMaterial;

class PhysicsScene
{
  public:
    PhysicsScene();

    void ApplyRuntimeConfig( const Basics::EngineConfig& config );
    // Applies collection-wide material config to live collider rows without
    // reopening the GameModel authoring mirror.
    void ApplyColliderMaterial( const PhysicsMaterial& material );
    void Clear();
    void RefreshBodyStore( PhysicsModelAccess& modelAccess );
    // Owner passes the expected count so PhysicsModelAccess stays a refresh-only
    // facade rather than a generic model-order query surface.
    void RefreshBodyFromModel( PhysicsModelAccess& modelAccess, int modelIndex, int expectedModelCount );
    // Construction edge: registers one newly authored body value without a full
    // compatibility reload. Owner is GameModelCollection until scene creation
    // writes body descriptors directly.
    PhysicsBodyHandle RegisterAuthoredBody( const PhysicsBodyRecord& record );
    // Construction edge: registers the collider descriptor paired with a newly
    // authored body without forcing a collider snapshot refresh through the
    // model container.
    PhysicsColliderHandle RegisterAuthoredCollider( const PhysicsColliderCreateDesc& desc );
    // Authoring/config edge: replaces one live collider row from a descriptor
    // while preserving the allocator-owned collider handle.
    bool UpdateAuthoredCollider( PhysicsColliderHandle collider, const PhysicsColliderCreateDesc& desc );
    void ClearPendingBodyImpulses();
    // Replay restore trims the authoritative body store directly; callers must
    // not force a model-to-store refresh after this succeeds.
    bool TrimBodyStoreToCount( int bodyCount );
    bool TrimColliderStoreToCount( int colliderCount );
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
    void RefreshRenderStore( PhysicsModelAccess& modelAccess, int expectedModelCount );
    void RunPhysics( float fChangeInTime,
                     const Basics::EngineConfig& config,
                     const PhysicsWorldForces& worldForces,
                     Threading::WorkerPool& workerPool,
                     const char* const* diagnosticNames,
                     int diagnosticNameCount );
    // Releases an authored fixed body, then same-tree parts, using body-store
    // policy and waking touched rows without a per-release model mirror.
    bool ReleaseFixedBodyAndAttachedTreeParts( PhysicsBodyHandle sourceBody,
                                               float releaseImpulseStrength,
                                               const Math::Vector::Vector3& seedLinearVelocity,
                                               const Math::Vector::Vector3& seedAngularVelocity );
    // Wakes solver sleep/island state by handle. Legacy model-index callers
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
    // ApplyBodyImpulse owns the separate wake/presentation compatibility edge.
    void SetPendingBodyImpulse( PhysicsBodyHandle body,
                                const Math::Vector::Vector3& impulse,
                                const Math::Vector::Vector3& localApplicationPoint );
    // Queues a one-shot impulse and wakes by body handle without borrowing the
    // model owner.
    void ApplyBodyImpulse( PhysicsBodyHandle body,
                           const Math::Vector::Vector3& impulse,
                           const Math::Vector::Vector3& localApplicationPoint );
    void SetPhysicsSleepEnabled( bool enabled );
    void BeginCollisionVisualFrame( int modelCount );
    void EndCollisionVisualFrame();
    void ClearPointJointConstraints();
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
    void ApplyFixedTreeReleaseEvents( const PhysicsWorldForces& worldForces );
#ifdef _DEBUG
    void ValidatePhysicsStoreMappings( int modelCount ) const;
    void ValidateRenderStoreMappings( int modelCount ) const;
#endif

    PhysicsWorld m_world;                                 // Deterministic solver and debug state over body-store records.
    PhysicsBodyStore m_bodyStore;                         // Mutable body state in model/replay order.
    ColliderStore m_colliderStore;                        // Collider snapshot in model/replay order.
    Rendering::RenderInstanceStore m_renderInstanceStore; // Render snapshot in model/replay order.
    PhysicsWorldForces m_lastWorldForces;                 // Last real step boundary forces used by explicit wake commands.
    bool m_hasLastWorldForces = false;                    // False until the first physics step supplies world forces.
    std::vector<int> m_fixedTreeReleaseWakeBodies;        // Reused scene-edge wake list; avoids release-time allocation churn.
};
} // namespace Physics
} // namespace SkullbonezCore
