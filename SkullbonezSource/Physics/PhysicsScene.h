/*
File: SkullbonezSource/Physics/PhysicsScene.h
Purpose:
  Owns deterministic physics-scene state and store snapshots.

Mental model:
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
  - Agentic/Plans/physics-playground-refactor-and-file-prefix-cleanup-plan.md
*/
#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

#include "ColliderStore.h"
#include "PhysicsBodyStore.h"
#include "PhysicsObjectPolicy.h"
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

namespace Rendering
{
class IRenderCommandContext;
} // namespace Rendering

namespace Physics
{
struct PhysicsColliderCreateDesc;
struct PhysicsMaterial;

class PhysicsScene
{
  public:
    PhysicsScene();

    void ApplyRuntimeConfig( const Basics::EngineConfig& config );
    // Stamps current runtime policy onto cold authoring descriptors. Descriptor
    // storage may still live outside PhysicsScene, but policy values do not.
    void ApplyAuthoredBodyPolicy( PhysicsBodyCreateDesc& desc ) const;
    void ApplyAuthoredColliderPolicy( PhysicsColliderCreateDesc& desc ) const;
    // Caller contract: authored body descriptors are cold scene-authoring rows
    // keyed by model order. Collection may supply replay/grouping scalars, but
    // it must not keep a competing body descriptor sidecar.
    void ReserveAuthoredBodyCapacity( std::size_t capacity );
    int AuthoredBodyDescriptorCount() const;
    bool TryGetAuthoredBodyDescriptor( int modelIndex, PhysicsBodyCreateDesc& outDesc ) const;
    bool UpdateAuthoredBodyDescriptor( int modelIndex, PhysicsBodyCreateDesc& desc, int expectedModelCount );
    bool TrimAuthoredBodyDescriptorsToCount( int bodyCount );
    void Clear();
    bool RefreshBodyStoreFromAuthoredDescriptors( const std::vector<uint32_t>& replayBodyIds,
                                                  const std::vector<int>& fixedTreeReleaseRoots,
                                                  const std::vector<const char*>& diagnosticNames );
    void RefreshBodyStore( const std::vector<PhysicsBodyCreateDesc>& bodyDescs );
    // Owner passes the expected count so one-row descriptor commits stay a
    // same-topology edit and cannot hide missing body rows.
    void RefreshBodyFromDescriptor( const PhysicsBodyCreateDesc& desc, int modelIndex, int expectedModelCount );
    // Construction edge: registers one newly authored body value without a full
    // full descriptor reload. Owner is the scene/model creation edge.
    PhysicsBodyHandle RegisterAuthoredBody( const PhysicsBodyCreateDesc& desc );
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
    // Prepares body/collider rows for the render-store projection refresh. The
    // collection owner fills render presentation rows after this returns.
    bool PrepareRenderStoreRefresh( int expectedModelCount );
    void ReserveRenderPresentationCapacity( std::size_t capacity );
    bool ResizeRenderPresentationRecords( int presentationCount );
    Rendering::RenderInstancePresentationRecord* MutableRenderPresentationRecordForModelIndex( int modelIndex );
    const std::vector<Rendering::RenderInstancePresentationRecord>& RenderPresentationRecords() const;
    bool RefreshRenderInstancesFromPresentation();
    // Mutable only for replay/render presentation pose overrides.
    Rendering::RenderInstanceStore& MutableRenderInstances();
    void RunPhysics( float fChangeInTime,
                     const Basics::EngineConfig& config,
                     const PhysicsWorldForces& worldForces,
                     Threading::WorkerPool& workerPool,
                     const char* const* diagnosticNames,
                     int diagnosticNameCount );
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
    void BeginCollisionVisualFrame( int modelCount );
    void EndCollisionVisualFrame();
    void ClearPointJointConstraints();
    PhysicsConstraintHandle CreatePointJoint( const PhysicsPointJointCreateDesc& desc );
    void SetTornadoFieldConfig( const TornadoFieldConfig& config );
    const TornadoFieldConfig& GetTornadoFieldConfig() const;
    void SetTornadoSystemConfig( const TornadoSystemConfig& config );
    const TornadoSystemConfig& GetTornadoSystemConfig() const;
    float GetTornadoSystemElapsedSeconds() const;
    // Debug overlay edge: renderer capability checks stay outside the
    // deterministic physics scene.
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
    void ApplyFixedTreeReleaseEvents( const PhysicsWorldForces& worldForces );
#ifdef _DEBUG
    void ValidatePhysicsStoreMappings( int modelCount ) const;
    void ValidateRenderStoreMappings( int modelCount ) const;
#endif

    PhysicsWorld m_world;                                   // Deterministic solver and debug state over body-store records.
    std::vector<PhysicsBodyCreateDesc> m_authoredBodyDescs; // Cold body authoring descriptors keyed by scene/model order.
    PhysicsBodyStore m_bodyStore;                           // Mutable body state in model/replay order.
    ColliderStore m_colliderStore;                          // Collider snapshot in model/replay order.
    Rendering::RenderInstanceStore m_renderInstanceStore;   // Render snapshot in model/replay order.
    PhysicsMaterial m_physicsMaterial;                      // Runtime material policy copied into body/collider descriptors.
    BodySimulationLimits m_bodySimulationLimits;            // Runtime body caps copied at authoring/import boundaries.
    ContactPolicy m_contactPolicy;                          // Runtime contact thresholds copied at authoring/import boundaries.
    PhysicsWorldForces m_lastWorldForces;                   // Last real step boundary forces used by explicit wake commands.
    bool m_hasLastWorldForces = false;                      // False until the first physics step supplies world forces.
    std::vector<int> m_fixedTreeReleaseWakeBodies;          // Reused scene-edge wake list; avoids release-time allocation churn.
};
} // namespace Physics
} // namespace SkullbonezCore
