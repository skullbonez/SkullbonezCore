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

Invariants:
  - Physics-visible behavior must remain deterministic; byte-exact baselines
    are the validation contract.
  - Body and pair force scratch capacity is established during scene load and
    may not grow while fixed ticks are running.

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
#include "../Runtime/Replay/ReplaySolverSnapshot.h"
#include "SleepIslandSystem.h"
#include "SpatialGrid.h"
#include "Stages/PhysicsBroadphaseStage.h"
#include "Stages/PhysicsContactSolverStage.h"
#include "Stages/PhysicsForceStage.h"
#include "Stages/PhysicsNarrowphaseStage.h"
#include "Stages/PhysicsStageContexts.h"
#include "Stages/PhysicsTerrainStage.h"
#include "TerrainContactManifold.h"
#include "TornadoGameplay.h"

namespace SkullbonezCore
{
namespace Core
{
class EngineConfig;
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
    PhysicsForceStage m_forceStage;
    // Concrete broadphase owner retains the grid, pair output, and diagnostic
    // cell keys. The facade borrows its candidate span for the remaining stages.
    PhysicsBroadphaseStage m_broadphase;
    // Narrowphase owns bounded pair/island scratch; event commit remains on
    // this sequencer until diagnostics and presentation ownership move in P7.
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

    // Sleep policy working state.
    //
    // Sleeping is a performance and stability optimization: bodies that are
    // supported and quiet can stop integrating until something wakes them. The
    // "supported" and "inhibited" arrays are rebuilt each frame from contacts.
    // PhysicsBodyStore owns the persisted sleep flag; m_sleepState is the
    // solver's model-indexed working copy for existing diagnostics and sleep
    // algorithms. A fully submerged sleeping sphere also gets a one-way
    // lock so water-floor balls behave like static rocks instead of rejoining
    // buoyancy/contact churn.
    std::vector<uint8_t> m_sleepSupportedThisFrame;
    std::vector<uint8_t> m_sleepInhibitedThisFrame;
    std::vector<uint8_t> m_sleepState;
    std::vector<uint8_t> m_sleepCounter;
    std::vector<uint8_t> m_underwaterSleepLocked;
    // Debug visualization state. These arrays intentionally mirror scene/model
    // slot order so render/debug code can look up one byte/id without map
    // lookups in the overlay path.
    std::vector<uint8_t> m_collisionVisualContacts;
    std::vector<int> m_sleepIslandVisualId;
    std::vector<int> m_sleepIslandAssignedVisualId;
    int m_nextSleepIslandVisualId = 1;
    bool m_sleepEnabled = true;
    uint8_t m_seedSleepFrameCount = 30;
    bool m_collisionVisualFrameActive = false;

    // Sleep islands are connected components of "this body is safely supported
    // by that body" edges. If an entire island is quiet and has a stable anchor,
    // all of it may sleep together; if one member wakes, the island should not
    // leave neighbors suspended in mid-air.
    std::vector<std::pair<int, int>> m_sleepSupportEdges;
    std::vector<int> m_sleepIslandParent;
    std::vector<uint8_t> m_sleepIslandRank;
    std::vector<uint8_t> m_sleepIslandHasAwake;
    std::vector<uint8_t> m_sleepIslandHasSupportAnchor;
    std::vector<uint8_t> m_sleepIslandEligible;
    std::vector<uint8_t> m_sleepIslandCanSleep;

    // Point-joint sleep metadata is rebuilt during the sleep pass. It treats
    // ragdoll joints as connectivity/support edges while still leaving contacts
    // as the source of collision impulses. The same shape can later host a
    // generic constraint graph without changing the public sleep API.
    std::vector<uint8_t> m_sleepPointJointBody;
    std::vector<uint8_t> m_sleepIslandHasPointJoint;
    std::vector<uint8_t> m_sleepIslandPointJointsRelaxed;

    // Scratch index for persisted sleep island ids. Contacts are intentionally
    // pruned for sleeping bodies, so this reconnects a resting pile from its
    // sleep identity without storing extra prediction state.
    std::vector<int> m_sleepVisualIslandIds;
    std::vector<int> m_sleepVisualIslandBodies;

  private:
    void RunSleepIslandStage( PhysicsBodyStore& bodyStore,
                              const ColliderStore& colliderStore,
                              const PhysicsWorldForces& worldForces,
                              std::span<PhysicsBodyRecord> bodyRecords,
                              int modelCount,
                              float sleepLinearSq,
                              float sleepAngularSq,
                              uint8_t sleepFrames );
    void ApplySleepIslandTransitions( PhysicsBodyStore& bodyStore,
                                      const ColliderStore& colliderStore,
                                      const PhysicsWorldForces& worldForces,
                                      std::span<PhysicsBodyRecord> bodyRecords,
                                      DisjointSet& sleepIslands,
                                      int modelCount,
                                      uint8_t sleepFrames );

    void CommitObjectNarrowphaseEvent( const ObjectNarrowphaseEvent& event );

    std::vector<PhysicsDebugContact> m_physicsDebugContacts;
    std::vector<PhysicsPipelineRecord> m_physicsPipelineTrace;
    std::vector<uint8_t> m_restingWakeVisitedScratch;
    std::vector<int> m_restingWakeQueueScratch;
    std::vector<PointJointConstraint> m_pointJointConstraints;
    TornadoGameplay m_tornadoGameplay;
    SleepIslandSystem m_sleepIslandSystem;
    PhysicsDiagnosticsSink m_diagnostics;
#ifdef _DEBUG
    bool m_diagnosticsSuppressed = false;
#endif

    void RunSolverPhysics( PhysicsBodyStore& bodyStore,
                           const ColliderStore& colliderStore,
                           float dt,
                           const SkullbonezCore::Core::EngineConfig& config,
                           const PhysicsWorldForces& worldForces,
                           Threading::WorkerPool& workerPool );
    void EmitPhysicsCollisionTime( const char* type, int bodyA, int bodyB, float collisionTime, float availableTime );
    void CommitContactSolverConsequences( PhysicsBodyStore& bodyStore,
                                          const ColliderStore& colliderStore,
                                          const PhysicsWorldForces& worldForces );
    SleepSupportPropagationContext CreateSleepSupportPropagationContext();
    bool CanRecordPhysicsPipelineStage() const;
    void RecordPhysicsPipelineStage( const PhysicsPipelineRecord& record );
    void EnsureCollisionVisualBuffers( int modelCount );
    void EnsureUnderwaterSleepLockBuffer( int modelCount );
    void LockUnderwaterSleeperIfReady( const PhysicsWorldForces& worldForces,
                                       PhysicsBodyStore& bodyStore,
                                       const ColliderStore& colliderStore,
                                       int index );
    bool IsUnderwaterSleepLocked( int bodyCount, int index );
    void MarkCollisionVisualContact( int index );
    void ApplyTornadoGameplay( PhysicsBodyStore& bodyStore,
                               const ColliderStore& colliderStore,
                               const PhysicsWorldForces& worldForces,
                               float dt,
                               const SkullbonezCore::Core::EngineConfig& runtimeConfig,
                               Threading::WorkerPool& workerPool );
    void PropagateSleepSupport( std::span<const PhysicsBodyRecord> bodyRecords );
    void AppendPointJointSupportEdges( const PhysicsBodyStore& bodyStore, int modelCount );
    void WakeModel( int bodyCount,
                    std::span<const PhysicsBodyRecord> bodyRecords,
                    PhysicsBodyStore* bodyStore,
                    const ColliderStore* colliderStore,
                    const PhysicsWorldForces* worldForces,
                    int index );
    void SeedModelAsleep( int bodyCount, std::span<const PhysicsBodyRecord> bodyRecords, int index );
    bool WakeDynamicBodyState( int bodyCount,
                               std::span<const PhysicsBodyRecord> bodyRecords,
                               PhysicsBodyStore* bodyStore,
                               int index,
                               float dt,
                               bool applyForces,
                               const PhysicsWorldForces* worldForces = nullptr,
                               const ColliderStore* colliderStore = nullptr );
    void WakeSleepVisualIsland( int bodyCount,
                                std::span<const PhysicsBodyRecord> bodyRecords,
                                PhysicsBodyStore* bodyStore,
                                int index,
                                float dt,
                                bool applyForces,
                                const PhysicsWorldForces* worldForces = nullptr,
                                const ColliderStore* colliderStore = nullptr );
    void WakePointJointIsland( int bodyCount,
                               std::span<const PhysicsBodyRecord> bodyRecords,
                               PhysicsBodyStore* bodyStore,
                               int index,
                               float dt,
                               bool applyForces,
                               const PhysicsWorldForces* worldForces = nullptr,
                               const ColliderStore* colliderStore = nullptr );
    void WakeRestingContactIsland( int bodyCount,
                                   std::span<const PhysicsBodyRecord> bodyRecords,
                                   PhysicsBodyStore* bodyStore,
                                   int index,
                                   float dt,
                                   bool applyForces,
                                   const PhysicsWorldForces* worldForces = nullptr,
                                   const ColliderStore* colliderStore = nullptr );
    bool IsPointJointPair( const PhysicsBodyStore& bodyStore, int bodyA, int bodyB ) const;
    void WakePointJointConnectedBodies( PhysicsBodyStore& bodyStore,
                                        const ColliderStore& colliderStore,
                                        const PhysicsWorldForces& worldForces,
                                        float dt );

  public:
    PhysicsWorld();

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
    // caller passes in. PhysicsScene owns the cold presentation-name overlay and
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
    void CaptureReplaySolverSnapshot( Runtime::ReplaySolverWorldSnapshot& outSnapshot, int modelCount ) const;
    bool RestoreReplaySolverSnapshot( const Runtime::ReplaySolverWorldSnapshot& snapshot, int modelCount );
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

struct SleepSupportPropagationContext
{
    std::span<uint8_t> sleepState;
    std::span<const std::pair<int, int>> sleepSupportEdges;
    std::span<uint8_t> sleepSupportedThisFrame;
};
} // namespace Physics
} // namespace SkullbonezCore
