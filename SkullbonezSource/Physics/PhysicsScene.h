/*
File: SkullbonezSource/Physics/PhysicsScene.h
Purpose:
  Owns deterministic physics-scene state and store snapshots.

Mental model:
  PhysicsScene is the boundary between the compatibility model view and the
  authoritative physics/render stores. PhysicsBodyStore owns the
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
class PhysicsScene
{
  public:
    PhysicsScene() = default;

    void ApplyRuntimeConfig( const Basics::EngineConfig& config );
    void Clear();
    void RefreshBodyStore( PhysicsModelAccess& modelAccess );
    void RefreshBodyFromModel( PhysicsModelAccess& modelAccess, int modelIndex );
    void ClearPendingBodyImpulses();
    // Replay restore trims the authoritative body store directly; callers must
    // not force a model-to-store refresh after this succeeds.
    bool TrimBodyStoreToCount( int bodyCount );
    // Store-owned replay restore facade used by runtime replay without
    // treating GameModel as the source of truth for simulation state.
    bool RestoreReplayBodyState( int modelIndex,
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
    void RefreshColliderStore( PhysicsModelAccess& modelAccess );
    void RefreshRenderStore( PhysicsModelAccess& modelAccess );
    void RunPhysics( PhysicsModelAccess& modelAccess,
                     float fChangeInTime,
                     const Basics::EngineConfig& config,
                     const PhysicsWorldForces& worldForces,
                     Threading::WorkerPool& workerPool );
    void WakeBody( PhysicsModelAccess& modelAccess, PhysicsBodyHandle body );
    // Sets replay/editor-authored live velocities by body handle and optionally
    // wakes a moving body before the normal step projects presentation state.
    bool SetBodyVelocity( PhysicsModelAccess& modelAccess,
                          PhysicsBodyHandle body,
                          const Math::Vector::Vector3& linearVelocity,
                          const Math::Vector::Vector3& angularVelocity,
                          bool wakeIfMoving );
    void SeedBodyAsleep( PhysicsBodyHandle body );
    // Queues one-shot solver input by body handle. The command is store-owned;
    // ApplyBodyImpulse owns the separate wake/presentation compatibility edge.
    void SetPendingBodyImpulse( PhysicsBodyHandle body,
                                const Math::Vector::Vector3& impulse,
                                const Math::Vector::Vector3& localApplicationPoint );
    void ApplyBodyImpulse( PhysicsModelAccess& modelAccess,
                           PhysicsBodyHandle body,
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
    void ApplyFixedTreeReleaseEvents( PhysicsModelAccess& modelAccess, const PhysicsWorldForces& worldForces );
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
