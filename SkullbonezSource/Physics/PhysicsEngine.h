/*
File: SkullbonezSource/Physics/PhysicsEngine.h
Purpose:
  Owns deterministic physics state, stores, solver coordination, and public commands.

Summary:
  PhysicsEngine is the single runtime-facing physics owner. It coordinates cold
  authored descriptors, dense body/collider/buoyancy stores, PhysicsWorld
  stepping, replay restore, and immutable diagnostics queries without a second
  simulation owner.

Glossary:
  Owner boundary: Public command/query surface that retains the state and
    sequencing authority behind it.
  Fixed-tree release: Store-owned command that turns authored fixed props into
    dynamic bodies and wakes same-tree parts after an accepted impulse.
  Physics material: Runtime policy for collider friction and sphere drag.
  Diagnostics view: Borrowed read-only solver/debug state exposed for tooling.
  Immutable projection: Field-specific borrowed store or diagnostic read whose
    lifetime remains tied to PhysicsEngine.
  Descriptor refresh: Cold authoring edge that replaces body rows from explicit
    values supplied by the model collection owner.

Invariants:
  - Solver, store-refresh, replay, and diagnostics call order remains deterministic.
  - PhysicsWorld is the cohesive solver implementation owned by PhysicsEngine;
    callers receive no mutable-store or solver authority.

Related:
  - SkullbonezSource/Physics/PhysicsEngine.cpp
  - SkullbonezSource/Physics/PhysicsApi.h
  - SkullbonezSource/Physics/PhysicsWorld.h
*/
#pragma once

#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

#include "ColliderStore.h"
#include "BuoyancySystem.h"
#include "PhysicsBodyStore.h"
#include "PhysicsObjectPolicy.h"
#include "PhysicsRuntimeSettings.h"
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
struct PhysicsBroadphaseCellQueryDesc;
struct PhysicsBroadphaseQueryResultView;
struct PhysicsPointJointUpdateDesc;
struct PhysicsRayCastDesc;
struct PhysicsRayCastHit;
struct PhysicsMaterial;

class PhysicsEngine
{
  public:
    PhysicsEngine();
    void BindProfiler( SkullbonezCore::Core::Profiler* profiler ) noexcept;

    void ApplyRuntimeConfig( const SkullbonezCore::Core::EngineConfig& config );
    // Cold conversion seam used by config stamping and field-faithfulness tests.
    // Fixed-step code receives only the returned Physics-owned value snapshot.
    static PhysicsRuntimeSettings RuntimeSettingsFromConfig( const SkullbonezCore::Core::EngineConfig& config );
    // Stamps the PhysicsEngine-owned runtime policy onto cold authoring
    // descriptors before they become store rows.
    void ApplyAuthoredBodyPolicy( PhysicsBodyCreateDesc& desc ) const;
    void ApplyAuthoredColliderPolicy( PhysicsColliderCreateDesc& desc ) const;
    void ReserveAuthoredBodyCapacity( std::size_t capacity );
    PhysicsAuthoredBodyCount AuthoredBodyDescriptorCount() const;
    // Scene creation uses this before its first owner mutation; false is a
    // topology/reservation invariant, not recoverable authored input.
    bool CanRegisterAuthoredBody( PhysicsAuthoredBodyCount expectedBodyCount ) const;
    bool TrimAuthoredBodyDescriptorsToCount( PhysicsAuthoredBodyCount bodyCount );
    void SetTerrainView( PhysicsTerrainView terrain ) noexcept;
    void ClearTerrainView() noexcept;
    void Clear();
    bool RefreshBodyStoreFromAuthoredDescriptors( const PhysicsAuthoredBodyRefreshView& refreshView );
    // One physics-owned registration command publishes the authored descriptor,
    // live body, paired collider, and buoyancy row or rolls the transaction back.
    PhysicsAuthoredBodyRegistration RegisterAuthoredBody( const PhysicsBodyCreateDesc& body,
                                                          PhysicsColliderCreateDesc collider );
    // Deterministically removes the paired collider, buoyancy, descriptor, and
    // body rows and invalidates the retired body handle before returning.
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
    // Store-owned replay restore command. Callers resolve a body handle at the
    // owner edge so physics does not accept transient model slots as authority.
    bool RestoreReplayBodyState( const PhysicsBodyRestoreState& restore );
    // Rebinds existing collider rows from physics body identity. Missing collider
    // rows are a topology bug, not a cue to rebuild shape facts from authoring
    // storage.
    bool RefreshColliderSnapshot();
    // Steps the owned stores. Model-order descriptor import and diagnostic-name
    // registration are cold commands; the per-tick call carries only simulation
    // inputs plus concrete Debug CSV output authority.
    void Step( float deltaSeconds,
               const PhysicsWorldForces& worldForces,
               Threading::WorkerPool& workerPool,
               const PhysicsDiagnosticsCsvWriter& diagnosticsCsvWriter );
    void Step( float deltaSeconds,
               const PhysicsWorldForces& worldForces,
               const ExternalForceFrameInput& externalForces,
               Threading::WorkerPool& workerPool,
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
    // Updates or retires one exact stable constraint handle. Dense solver-row
    // compaction never changes the identity of surviving point joints.
    bool UpdatePointJoint( const PhysicsPointJointUpdateDesc& desc );
    bool DestroyConstraint( PhysicsConstraintHandle constraint );
    // Conservative store queries return stable typed identities without
    // exposing mutable body/collider or broadphase-owner state.
    PhysicsRayCastHit RayCast( const PhysicsRayCastDesc& desc ) const;
    PhysicsBroadphaseQueryResultView QueryBroadphaseCells( const PhysicsBroadphaseCellQueryDesc& desc ) const;
    void CaptureReplaySolverSnapshot( PhysicsSolverSnapshot& outSnapshot, PhysicsBodyCount bodyCount ) const;
    bool RestoreReplaySolverSnapshot( const PhysicsSolverSnapshot& snapshot, PhysicsBodyCount bodyCount );
    PhysicsDiagnosticsView GetDiagnosticsView() const;
    uint64_t CollectPhysicsWorldMemoryBytes() const;
    uint64_t CollectDebugAndBroadphaseMemoryBytes() const;
    bool ShouldEmitStepDiagnostics() const;
    bool ShouldEmitCollisionTimeDiagnostics() const;
    // Registers scene-lifetime presentation names at a cold topology boundary.
    // The diagnostics sink copies only the pointer table into fixed storage.
    void SetDiagnosticNames( std::span<const char* const> diagnosticNames );

    // Immutable dense views are an explicit PhysicsEngine query contract for
    // renderer, replay, diagnostics, and cold tools. Each aliases one owned
    // field directly without friendship or mutable-store authority.
    static const PhysicsBodyStore& ReadBodies( const PhysicsEngine& engine );
    static const ColliderStore& ReadColliders( const PhysicsEngine& engine );
    static std::span<const BuoyancyBodyFacts> ReadBuoyancyFacts( const PhysicsEngine& engine );
    static std::size_t ReadBuoyancyFactCapacity( const PhysicsEngine& engine );
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
    void LoadBodyDescriptors( const std::vector<PhysicsBodyCreateDesc>& bodyDescs );
    void ApplyFixedTreeReleaseEvents( const PhysicsWorldForces& worldForces );
#ifdef _DEBUG
    void ValidatePhysicsStoreMappings( int modelCount ) const;
#endif

    PhysicsWorld m_world;                                    // Deterministic solver and debug state over body-store records.
    std::vector<PhysicsBodyCreateDesc> m_authoredBodyDescs;  // Cold body authoring descriptors keyed by scene/model order.
    PhysicsBodyStore m_bodyStore;                            // Mutable body state in model/replay order.
    ColliderStore m_colliderStore;                           // Collider snapshot in model/replay order.
    BuoyancySystem m_buoyancySystem;                         // Fluid facts aligned with body/collider model rows.
    PhysicsMaterial m_physicsMaterial;                       // Runtime material policy copied into body/collider descriptors.
    BodySimulationLimits m_bodySimulationLimits;             // Runtime body caps copied at authoring/import boundaries.
    ContactPolicy m_contactPolicy;                           // Runtime contact thresholds copied at authoring/import boundaries.
    PhysicsRuntimeSettings m_runtimeSettings;                // Physics-owned process settings stamped before fixed stepping.
    PhysicsWorldForces m_lastWorldForces;                    // Last real step boundary forces used by explicit wake commands.
    bool m_hasLastWorldForces = false;                       // False until the first physics step supplies world forces.
    PhysicsBodyIndexList m_fixedTreeReleaseWakeBodies {      // Fixed owner-edge wake list; never grows during release.
                                                        "PhysicsEngine fixed-tree release output" };
    mutable PhysicsBodyHandleList m_broadphaseQueryScratch { // Borrowed query result, replaced by the next query.
                                                             "PhysicsEngine broadphase query results" };
};
} // namespace Physics
} // namespace SkullbonezCore
