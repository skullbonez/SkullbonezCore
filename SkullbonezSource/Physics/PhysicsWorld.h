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
#include <vector>

#include "ColliderStore.h"
#include "PhysicsBodyStore.h"
#include "PhysicsDiagnosticsSink.h"
#include "PhysicsDebugData.h"
#include "Ragdoll.h"
#include "PhysicsSolverSnapshot.h"
#include "SleepIslandSystem.h"
#include "SpatialGrid.h"
#include "Stages/PhysicsBroadphaseStage.h"
#include "Stages/PhysicsContactSolverStage.h"
#include "Stages/PhysicsForceStage.h"
#include "Stages/PhysicsNarrowphaseStage.h"
#include "Stages/PhysicsStageContexts.h"
#include "Stages/PhysicsTerrainStage.h"
#include "Stages/PhysicsSleepController.h"
#include "Stages/PhysicsStepDiagnostics.h"
#include "TerrainContactManifold.h"
#include "TornadoGameplay.h"

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
class ColliderStore;
class PhysicsBodyStore;
struct ColliderRecord;
struct PhysicsBodyRecord;
struct PhysicsPointJointCreateDesc;
struct PhysicsDiagnosticsView;
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
    // Lifetime: startup-bound diagnostics borrow; stage contexts never retain it.
    SkullbonezCore::Core::Profiler* m_profiler = nullptr;
    PhysicsForceStage m_forceStage;
    // Concrete broadphase owner retains the grid, pair output, and diagnostic
    // cell keys. The facade borrows its candidate span for the remaining stages.
    PhysicsBroadphaseStage m_broadphase;
    // Narrowphase owns bounded pair/island scratch. The sequencer commits typed
    // events in pair order because they target sleep and diagnostics owners.
    PhysicsNarrowphaseStage m_narrowphase;
    // Terrain owns detection candidates, committed manifolds, and solver rest
    // rows. Sleep-support and remaining-time outputs are synchronous borrows.
    PhysicsTerrainStage m_terrain;
    // Persistent rows, cache, bounded solve scratch, and consequence queues
    // move as one cohesive contact-solver owner.
    PhysicsContactSolverStage m_contactSolverStage;
    // Invariant: narrowphase, terrain, and final integration all write this
    // cross-stage CCD clock, so it deliberately remains on the sequencer.
    std::vector<float> m_timeRemaining;
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
    // concrete owner; this facade only supplies synchronous physics views.
    PhysicsStepDiagnostics m_stepDiagnostics;

  private:
    void CommitObjectNarrowphaseEvent( const ObjectNarrowphaseEvent& event );

    // Stay-behind: point joints are a facade-owned top-level constraint lane;
    // the solver and sleep owner borrow the dense rows synchronously.
    std::vector<PointJointConstraint> m_pointJointConstraints;
    // Stay-behind: tornado gameplay is already a cohesive sibling owner whose
    // force application is sequenced alongside the extracted force stage.
    TornadoGameplay m_tornadoGameplay;
#ifdef _DEBUG
    // Stay-behind: scoped diagnostic suppression is a facade policy override,
    // while every diagnostic row and output sink belongs to its concrete owner.
    bool m_diagnosticsSuppressed = false;
#endif

    void RunSolverPhysics( PhysicsBodyStore& bodyStore,
                           const ColliderStore& colliderStore,
                           float dt,
                           const SkullbonezCore::Core::EngineConfig& config,
                           const PhysicsWorldForces& worldForces,
                           Threading::WorkerPool& workerPool,
                           bool probeDormantUnderwaterLocks );
    void CommitContactSolverConsequences( PhysicsBodyStore& bodyStore,
                                          const ColliderStore& colliderStore,
                                          const PhysicsWorldForces& worldForces );
    void ApplyTornadoGameplay( PhysicsBodyStore& bodyStore,
                               const ColliderStore& colliderStore,
                               const PhysicsWorldForces& worldForces,
                               float dt,
                               const SkullbonezCore::Core::EngineConfig& runtimeConfig,
                               Threading::WorkerPool& workerPool );

  public:
    PhysicsWorld();
    void BindProfiler( SkullbonezCore::Core::Profiler* profiler ) noexcept;

    void ApplyRuntimeConfig( const SkullbonezCore::Core::EngineConfig& config );
    void Clear();
    void ReserveBodyScratchCapacity( std::size_t capacity );
    // Runs one fixed world step over the stores. Collision diagnostics append
    // fixed events only; name lookup and file output occur after the hot pass.
    void RunPhysics( PhysicsBodyStore& bodyStore,
                     const ColliderStore& colliderStore,
                     float fChangeInTime,
                     const SkullbonezCore::Core::EngineConfig& config,
                     const PhysicsWorldForces& worldForces,
                     Threading::WorkerPool& workerPool );
    // Emits Debug-only regression and SkullScope records from the stores the
    // caller passes in. PhysicsEngine owns the cold presentation-name overlay and
    // runtime owns the CSV writer, so diagnostics do not borrow model or logging
    // globals from inside PhysicsWorld.
    bool ShouldEmitStepDiagnostics() const;
    bool ShouldEmitCollisionTimeDiagnostics() const;
    void EmitStepDiagnostics( const PhysicsBodyStore& bodyStore,
                              const ColliderStore& colliderStore,
                              float fChangeInTime,
                              const char* const* diagnosticNames,
                              int diagnosticNameCount,
                              const PhysicsDiagnosticsCsvWriter& diagnosticsCsvWriter );
    // Wake and seed decisions read physics-owned fixed/sleep state before the
    // scene edge performs any owner-side cache invalidation.
    void WakeModel( PhysicsBodyStore& bodyStore, int index );
    void WakeModel( PhysicsBodyStore& bodyStore,
                    const ColliderStore& colliderStore,
                    const PhysicsWorldForces& worldForces,
                    int index );
    void SeedModelAsleep( const PhysicsBodyStore& bodyStore, int index );
    void SetPhysicsSleepEnabled( bool enabled );
    bool IsPhysicsSleepEnabled() const;
    void BeginCollisionVisualFrame( int modelCount );
    void EndCollisionVisualFrame();
    // Cold authored mutation boundary: dense-row identity or fixed/sleep
    // classification may have changed before the next fixed step.
    void InvalidateBodyTopology();
    void ClearPointJointConstraints();
    // Deletion pre-pass: no constraint may retain a body handle after retirement.
    void DestroyPointJointsForBody( PhysicsBodyHandle body );
    PhysicsConstraintHandle CreatePointJoint( const PhysicsPointJointCreateDesc& desc );
    const std::vector<PointJointConstraint>& GetPointJointConstraints() const;
    void SetTornadoFieldConfig( const TornadoFieldConfig& config );
    const TornadoFieldConfig& GetTornadoFieldConfig() const;
    void SetTornadoSystemConfig( const TornadoSystemConfig& config );
    const TornadoSystemConfig& GetTornadoSystemConfig() const;
    float GetTornadoSystemElapsedSeconds() const;
    void CaptureReplaySolverSnapshot( PhysicsSolverSnapshot& outSnapshot, int modelCount ) const;
    bool RestoreReplaySolverSnapshot( const PhysicsSolverSnapshot& snapshot, int modelCount );
    PhysicsDiagnosticsView GetDiagnosticsView() const;
    uint64_t CollectMemoryBytes() const;
    uint64_t CollectDebugAndBroadphaseMemoryBytes() const;
    const Math::CollisionDetection::SpatialGrid& GetSpatialGrid() const;
    const std::vector<int64_t>& GetCollisionCellKeys() const;
    const std::vector<uint8_t>& GetCollisionVisualContacts() const;
    std::span<const int> GetFixedContactHighlightBodies() const;
    // Returns solver-emitted fixed-tree releases from the latest step. The
    // scene edge applies them before diagnostics and owner-side projection.
    std::span<const PhysicsFixedTreeReleaseEvent> GetFixedTreeReleaseEvents() const;
    std::span<const uint8_t> GetSleepStates() const;
    std::span<const int> GetSleepIslandVisualIds() const;
    std::span<const uint8_t> GetSleepSupportedStates() const;
    std::span<const uint8_t> GetSleepInhibitedStates() const;
    const std::vector<PhysicsDebugContact>& GetPhysicsDebugContacts() const;
    const std::vector<PhysicsPipelineRecord>& GetPhysicsPipelineTrace() const;

#ifdef _DEBUG
    void SetPhysicsRegressionLogPath( const char* path );
    void SetPhysicsCollisionTimeLogPath( const char* path );
    void SetPhysicsDiagnosticsPath( const char* path );
    void SetPhysicsDiagnosticsRunId( const char* runId );
    bool SetDiagnosticsSuppressed( bool suppressed );
#endif
};

struct PhysicsDiagnosticsView
{
    const std::vector<PersistentContact>& persistentContacts;
    const PersistentContactSolverStats& persistentContactSolverStats;
    const std::vector<int>& sleepIslandParent;
    const std::vector<uint8_t>& sleepSupportedThisFrame;
    const std::vector<uint8_t>& sleepInhibitedThisFrame;
    const std::vector<uint8_t>& sleepState;
    const std::vector<uint8_t>& sleepCounter;
    const std::vector<uint8_t>& sleepIslandEligible;
    const std::vector<uint8_t>& sleepIslandCanSleep;
    const std::vector<PointJointConstraint>& pointJointConstraints;
    const Math::CollisionDetection::SpatialGrid& spatialGrid;
    std::span<const std::pair<int, int>> candidatePairs;
    const std::vector<int64_t>& collisionCellKeys;
    const std::vector<std::pair<int, int>>& sleepSupportEdges;
    const std::vector<int>& sleepIslandVisualId;
    const std::vector<PhysicsPipelineRecord>& physicsPipelineTrace;
    const std::vector<TerrainContactManifold>& terrainContactManifolds;
};

} // namespace Physics
} // namespace SkullbonezCore
