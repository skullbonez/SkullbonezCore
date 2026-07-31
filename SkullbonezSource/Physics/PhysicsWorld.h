/*
File: SkullbonezSource/Physics/PhysicsWorld.h
Purpose:
  Owns per-scene physics working state shared by broadphase, solver, and diagnostics.

Summary:
  PhysicsWorld.h owns per-scene physics working state shared by broadphase,
  solver, and diagnostics. As a public header, keep edits anchored on
  deterministic physics, diagnostics, or world-state flow and on the
  glossary/invariants below.

Glossary:
  SkullScope: Queryable physics diagnostics workflow backed by bounded trace
  output and local queries.
  Broadphase: Cheap collision pass that finds object pairs worth testing more
  precisely.
  Narrowphase: Precise collision pass that computes contact points, normals,
  and penetration.
  Manifold: Set of contact points and normals describing one colliding pair.
  Point joint: Constraint that keeps two local anchor points close together
    without yet modelling a full hinge, cone, or motor.
  Sleep island: Connected body group that may deactivate only as a unit.
  Underwater sleep lock: Sleep policy that keeps fully submerged balls dormant
    so buoyancy jitter does not repeatedly wake them.
  Mutual-gravity pair scratch: Preallocated triangular force table whose unique
    slots let workers compute pairs without racing or regrouping additions.
  Awake index list: Sleep-owned ascending dense rows borrowed synchronously by
    fixed-step stages that can ignore dormant bodies.

Invariants:
  - Physics-visible behavior must remain deterministic; byte-exact baselines
    are the validation contract.
  - Body and pair force scratch capacity is established during scene load and
    may not grow while fixed ticks are running.
  - Authored topology edits invalidate derived awake/grid state before the next
    fixed step, including same-count destroy/create sequences.

Related:
  - SkullbonezSource/Physics/PhysicsWorld.cpp
  - Agentic/Reference/physics-overview.md
  - Agentic/Reference/comment-style-guide.md
*/
#pragma once

#include <cstdint>
#include <span>
#include <utility>

#include "ColliderStore.h"
#include "PhysicsBodyStore.h"
#include "PhysicsDiagnosticsSink.h"
#include "PhysicsDiagnosticsView.h"
#include "PhysicsDebugData.h"
#include "PhysicsStageCapacity.h"
#include "Ragdoll.h"
#include "PhysicsSolverSnapshot.h"
#include "PhysicsRuntimeSettings.h"
#include "SleepIslandSystem.h"
#include "SpatialGrid.h"
#include "Stages/PhysicsBroadphaseStage.h"
#include "Stages/PhysicsContactSolverStage.h"
#include "Stages/ExternalForceStage.h"
#include "Stages/PhysicsForceStage.h"
#include "Stages/PhysicsNarrowphaseStage.h"
#include "Stages/PhysicsTerrainStage.h"
#include "Stages/PhysicsSleepController.h"
#include "Stages/PhysicsStepDiagnostics.h"
#include "TerrainContactManifold.h"

namespace SkullbonezCore
{
namespace Core
{
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
class ColliderStore;
class PhysicsEngine;
class PhysicsBodyStore;
struct BuoyancyBodyFacts;
struct ColliderRecord;
struct PhysicsBodyRecord;
struct PhysicsPointJointCreateDesc;
struct PhysicsPointJointUpdateDesc;
struct PhysicsWorldForces;
struct SleepSupportPropagationContext;
class DisjointSet;

class PhysicsWorld
{
  public:

    // Source-compatible type names only; storage and mutation authority belong
    // exclusively to PhysicsContactSolverStage.
    using PersistentContact = Physics::PersistentContact;
    using PersistentContactSolverStats = Physics::PersistentContactSolverStats;

  private:

    // Lifetime: startup-bound diagnostics borrow; stage operations never retain it.
    SkullbonezCore::Core::Profiler* m_profiler = nullptr;
    PhysicsForceStage m_forceStage;

    // Gameplay force content crosses one bounded value lane; this stage owns
    // only reusable physics-side release scratch and application policy.
    ExternalForceStage m_externalForceStage;

    // Concrete broadphase owner retains the grid, pair output, and diagnostic
    // cell keys. The sequencer borrows its candidate span for the remaining stages.
    PhysicsBroadphaseStage m_broadphase;

    // Narrowphase owns bounded pair/island scratch. The sequencer commits typed
    // events in pair order because they target sleep and diagnostics owners.
    PhysicsNarrowphaseStage m_narrowphase;

    // Lifetime: this detached span borrows SceneTerrain cells. SceneWorld clears
    // the view before replacing that backing owner and republishes afterward.
    PhysicsTerrainView m_terrainView;

    // Terrain owns detection candidates, committed manifolds, and solver rest
    // rows. Sleep-support and remaining-time outputs are synchronous borrows.
    PhysicsTerrainStage m_terrain;

    // Persistent rows, cache, bounded solve scratch, and consequence queues
    // move as one cohesive contact-solver owner.
    PhysicsContactSolverStage m_contactSolverStage;

    // Invariant: narrowphase, terrain, and final integration all write this
    // cross-stage CCD clock, so it deliberately remains on the sequencer.
    PhysicsBodyRowList<float> m_timeRemaining { "PhysicsWorld.timeRemaining", PhysicsCapacityReason::SceneBodies };
    float m_lastTimeRemainingStep = 0.0f;
    bool m_lastTimeRemainingStepValid = false;

    // Cold/explicit sleep seeds need one underwater-lock census. Ordinary
    // island transitions probe the new sleeper immediately and leave this off.
    bool m_underwaterSleepProbeNeeded = true;
    float m_lastUnderwaterProbeFluidSurfaceHeight = 0.0f;
    bool m_lastUnderwaterProbeFluidSurfaceHeightValid = false;

    // Sleep state, wake propagation, and island transitions have one concrete
    // owner. PhysicsWorld only sequences its typed fixed-step operations.
    PhysicsSleepController m_sleepController;

    // Diagnostic rows, collision visuals, and cold output live behind one
    // concrete owner; PhysicsWorld only supplies synchronous physics views.
    PhysicsStepDiagnostics m_stepDiagnostics;

  private:
    friend class PhysicsEngine;

    // Copies only the non-snapshot topology and sequencing cursors needed to
    // seed an isolated replay prediction. PhysicsSolverSnapshot remains the
    // authority for solver/stage state restored immediately afterward.
    void CloneReplayPredictionTopologyFrom( const PhysicsWorld& source );

    void CommitObjectNarrowphaseEvent( const ObjectNarrowphaseEvent& event );
    void AdvancePointJointHandleGeneration();

    // Point joints are PhysicsWorld-owned solver state; the solver and sleep
    // owner borrow the dense rows synchronously.
    PhysicsBodyRowList<PointJointConstraint> m_pointJointConstraints { "PhysicsWorld.pointJointConstraints",
                                                                       PhysicsCapacityReason::PointJoints };
    std::size_t m_pointJointCapacity = 0u;
    uint32_t m_nextPointJointHandleIndex = 0u;
    uint32_t m_pointJointHandleGeneration = PHYSICS_HANDLE_INITIAL_GENERATION;
#ifdef _DEBUG

    // Scoped diagnostic suppression is a sequencer policy override, while every
    // diagnostic row and output sink belongs to its concrete owner.
    bool m_diagnosticsSuppressed = false;
#endif

    void RunSolverPhysics( PhysicsBodyStore& bodyStore, const ColliderStore& colliderStore,
                           std::span<BuoyancyBodyFacts> buoyancyFacts, float dt, const PhysicsRuntimeSettings& settings,
                           const PhysicsWorldForces& worldForces, const ExternalForceFrameInput& externalForces,
                           Threading::WorkerPool& workerPool, bool probeDormantUnderwaterLocks );
    void CommitContactSolverConsequences( PhysicsBodyStore& bodyStore, const ColliderStore& colliderStore,
                                          std::span<BuoyancyBodyFacts> buoyancyFacts,
                                          const PhysicsWorldForces& worldForces );
    void ApplyExternalForces( PhysicsBodyStore& bodyStore, const ColliderStore& colliderStore,
                              std::span<BuoyancyBodyFacts> buoyancyFacts, const PhysicsWorldForces& worldForces,
                              const ExternalForceFrameInput& input, const PhysicsExecutionSettings& execution,
                              Threading::WorkerPool& workerPool );

  public:
    PhysicsWorld();
    void BindProfiler( SkullbonezCore::Core::Profiler* profiler ) noexcept;

    void ApplyRuntimeSettings( const PhysicsRuntimeSettings& settings );
    void SetTerrainView( PhysicsTerrainView terrain ) noexcept;
    void ClearTerrainView() noexcept;
    void Clear();
    void ReserveBodyScratchCapacity( std::size_t bodyCapacity, std::size_t pointJointCapacity );
    std::size_t PointJointCapacity() const noexcept;

    // Runs one fixed world step over the stores. Collision diagnostics append
    // fixed events only; name lookup and file output occur after the hot pass.
    void RunPhysics( PhysicsBodyStore& bodyStore, const ColliderStore& colliderStore,
                     std::span<BuoyancyBodyFacts> buoyancyFacts, float fChangeInTime, const PhysicsRuntimeSettings& settings,
                     const PhysicsWorldForces& worldForces, const ExternalForceFrameInput& externalForces,
                     Threading::WorkerPool& workerPool );

    // Emits Debug-only regression and SkullScope records from the stores the
    // caller passes in. The diagnostics sink owns the registered cold name
    // table and runtime owns the CSV writer, so fixed steps do not borrow model
    // or logging globals.
    bool ShouldEmitStepDiagnostics() const;
    void SetDiagnosticNames( std::span<const char* const> diagnosticNames );
    void EmitStepDiagnostics( const PhysicsBodyStore& bodyStore, const ColliderStore& colliderStore, float fChangeInTime,
                              const PhysicsDiagnosticsCsvWriter& diagnosticsCsvWriter );

    // Wake and seed decisions read physics-owned fixed/sleep state before the
    // scene edge performs any owner-side cache invalidation.
    void WakeModel( PhysicsBodyStore& bodyStore, int index );
    void WakeModel( PhysicsBodyStore& bodyStore, const ColliderStore& colliderStore,
                    std::span<BuoyancyBodyFacts> buoyancyFacts, const PhysicsWorldForces& worldForces, int index );
    void SeedModelAsleep( const PhysicsBodyStore& bodyStore, int index );
    void SetPhysicsSleepEnabled( bool enabled );
    bool IsPhysicsSleepEnabled() const;
    void BeginCollisionVisualFrame( int modelCount );
    void EndCollisionVisualFrame();
    void SetPipelineTraceFullRecordConsumerActive( bool active );

    // Cold authored mutation boundary: dense-row identity or fixed/sleep
    // classification may have changed before the next fixed step.
    void InvalidateBodyTopology();
    void ClearPointJointConstraints();

    // Deletion pre-pass: no constraint may retain a body handle after retirement.
    void DestroyPointJointsForBody( PhysicsBodyHandle body );
    PhysicsConstraintHandle CreatePointJoint( const PhysicsPointJointCreateDesc& desc );
    bool UpdatePointJoint( const PhysicsPointJointUpdateDesc& desc );
    bool DestroyConstraint( PhysicsConstraintHandle constraint );
    const PhysicsBodyRowList<PointJointConstraint>& GetPointJointConstraints() const;
    void CaptureReplaySolverSnapshot( PhysicsSolverSnapshot& outSnapshot, int modelCount ) const;
    bool RestoreReplaySolverSnapshot( const PhysicsSolverSnapshot& snapshot, int modelCount );
    PhysicsDiagnosticsView GetDiagnosticsView() const;
    uint64_t CollectMemoryBytes() const;
    uint64_t CollectDebugAndBroadphaseMemoryBytes() const;
    const Math::CollisionDetection::SpatialGrid& GetSpatialGrid() const;
    std::span<const int64_t> GetCollisionCellKeys() const;
    std::span<const uint8_t> GetCollisionVisualContacts() const;
    std::span<const int> GetFixedContactHighlightBodies() const;

    // Returns solver-emitted fixed-tree releases from the latest step. The
    // scene edge applies them before diagnostics and owner-side projection.
    std::span<const PhysicsFixedTreeReleaseEvent> GetFixedTreeReleaseEvents() const;
    std::span<const uint8_t> GetSleepStates() const;
    std::span<const int> GetSleepIslandVisualIds() const;
    std::span<const uint8_t> GetSleepSupportedStates() const;
    std::span<const uint8_t> GetSleepInhibitedStates() const;
    std::span<const PhysicsDebugContact> GetPhysicsDebugContacts() const;
    uint32_t GetPhysicsPipelineRecordCount() const;
    std::span<const PhysicsPipelineRecord> GetPhysicsPipelineTrace() const;

#ifdef _DEBUG
    void SetPhysicsRegressionLogPath( const char* path );
    void SetPhysicsCollisionTimeLogPath( const char* path );
    void SetPhysicsDiagnosticsPath( const char* path );
    void SetPhysicsDiagnosticsRunId( const char* runId );
#endif
};

} // namespace Physics
} // namespace SkullbonezCore
