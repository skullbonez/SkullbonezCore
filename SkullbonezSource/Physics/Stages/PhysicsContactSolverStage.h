/*
File: SkullbonezSource/Physics/Stages/PhysicsContactSolverStage.h
Purpose:
  Owns persistent-contact rows, cache, the guarded solve transaction,
  statistics, and outputs.

Summary:
  PhysicsContactSolverStage owns and executes the complete persistent-row solve

  for one fixed step. It borrows dense body, collider, sleep, terrain, and
  diagnostics rows synchronously and publishes a typed consequence batch for
  the PhysicsWorld sequencer to commit.

Glossary:
  Persistent contact: Solver row retained long enough to warm-start a matching
    contact feature on the next fixed tick.
  Consequence batch: Bounded post-solve records and body indices whose foreign
    owner-side effects are committed after the hot solver pass.
  Phase transaction: Non-copyable owner that admits solver work only through
    the current fixed-step phase and retains the solver-body working set.
  Wake access: Narrow synchronous capability that can invalidate cache rows
    without exposing the contact-solver owner to the sleep subsystem.
  Warm start: Reuse of last tick's accumulated contact impulses.
  Convergence trace: Fixed-capacity live diagnostic history for PGS stopping
    metrics; it is not replay state.

Invariants:
  - Owned lists commit scene-derived runtime capacities before play and fail
    loudly rather than grow during steady gameplay.
  - Solve prepares a fresh consequence batch before invoking the row solver.
  - Contact phases advance in one adjacent order. The two existing no-work
    exits may terminate only from entry/setup or terrain-row completion.
  - The solve transaction owns solver-body scratch and impulse application; it
    retains no borrowed store, span, stage, or world pointer.
  - The stage retains no pointer or reference to PhysicsWorld or borrowed rows.
  - Wake propagation receives only a cache-invalidation capability, never the
    concrete contact-solver owner.
  - At most 64 convergence samples survive one solve; excess iterations are
    counted, and replay capture never copies the trace.

Related:
  - SkullbonezSource/Physics/Stages/PhysicsContactSolverStage.cpp
  - SkullbonezSource/Physics/PersistentContactSolver.cpp
  - SkullbonezSource/Physics/PhysicsWorld.cpp
  - Agentic/Reports/2026-07-29/box-vibration-and-warm-start-integrity-closure.md
  - Agentic/Reports/2026-07-29/persistent-contact-convergence-early-out-ce1.md
*/
#pragma once

#include <cstdint>
#include <span>
#include <utility>
#include <vector>

#include "../PersistentContactSolver.h"
#include "../PhysicsBodyStore.h"
#include "../PhysicsDiagnosticsView.h"
#include "../PhysicsRuntimeSettings.h"
#include "../PhysicsDebugData.h"
#include "../PhysicsStageCapacity.h"
#include "../TerrainContactManifold.h"
#include "../PhysicsSolverSnapshot.h"

namespace SkullbonezCore
{
namespace Core
{
class Profiler;
} // namespace Core

namespace Physics
{
struct PersistentContactSolveTransactionTestAccess;
class ColliderStore;
class PhysicsBodyStore;
struct ColliderRecord;
struct PhysicsBodyRecord;
struct PhysicsWorldForces;
class PhysicsStepDiagnostics;

using PersistentContactList = PhysicsFixedList<PersistentContact, PHYSICS_MAX_CONTACT_ROWS>;
using PersistentContactCacheList = PhysicsFixedList<PersistentContactCacheEntry, PHYSICS_MAX_CONTACT_ROWS>;
using PersistentContactCountList = PhysicsFixedList<uint16_t, PHYSICS_MAX_BODY_ROWS>;
using SolverBodyStateList = PhysicsFixedList<SolverBodyState, PHYSICS_MAX_BODY_ROWS>;
using PhysicsPipelineRecordList = PhysicsFixedList<PhysicsPipelineRecord, PHYSICS_MAX_PIPELINE_TRACE_RECORDS>;
using PhysicsCollisionVisualBodyList = PhysicsFixedList<int, PHYSICS_MAX_COLLISION_VISUAL_BODY_ROWS>;
using PhysicsContactBodyList = PhysicsFixedList<int, PHYSICS_MAX_CONTACT_ROWS>;
using PhysicsReleaseWakeBodyList = PhysicsFixedList<int, PHYSICS_MAX_BODY_ROWS>;
using PhysicsFixedTreeReleaseList = PhysicsFixedList<PhysicsFixedTreeReleaseEvent, PHYSICS_MAX_BODY_ROWS>;

class PersistentContactSolvePhaseCursor
{
  public:
    enum class Phase : uint8_t
    {
        Idle,
        EntryPolicySetup,
        BodySetup,
        BuildManifolds,
        TerrainRows,
        Precompute,
        SolveRows,
        PointSupportInstability,
        TerrainRestPolicy,
        WriteBack,
        DebugContacts,
        PositionCorrection,
        CacheStore,
        FixedContactRelease,
        Complete,
        Count
    };

    static constexpr bool IsLegalTransition( Phase from, Phase to )
    {
        const bool adjacent = from >= Phase::Idle && from < Phase::FixedContactRelease &&
                              to == static_cast<Phase>( static_cast<uint8_t>( from ) + 1u );
        const bool noInput = from == Phase::EntryPolicySetup && to == Phase::Complete;
        const bool noRows = from == Phase::TerrainRows && to == Phase::Complete;
        const bool normalCompletion = from == Phase::FixedContactRelease && to == Phase::Complete;
        return adjacent || noInput || noRows || normalCompletion;
    }

    bool TryAdvance( Phase next )
    {

        if ( !IsLegalTransition( m_phase, next ) )
        {
            return false;
        }

        m_phase = next;
        return true;
    }

    bool ResetAfterComplete()
    {

        if ( m_phase != Phase::Complete )
        {
            return false;
        }

        m_phase = Phase::Idle;
        return true;
    }

    Phase Current() const
    {
        return m_phase;
    }

  private:
    Phase m_phase = Phase::Idle;
};

// Invariant:
// - A fixed-step solve follows EntryPolicySetup -> BodySetup -> BuildManifolds
//   -> TerrainRows -> Precompute -> SolveRows -> PointSupportInstability
//   -> TerrainRestPolicy -> WriteBack -> DebugContacts -> PositionCorrection
//   -> CacheStore -> FixedContactRelease -> Complete.
// - The existing empty-input and empty-row exits may reach Complete only from
//   EntryPolicySetup and TerrainRows respectively.
// - Solver-body storage, impulse application, and every phase advancement have
//   one owner. Phase methods synchronously borrow the stage only to mutate its
//   retained rows, cache statistics, and consequence lists; no caller borrow
//   survives a transaction method return.
//   TestRuntimeContracts.cpp proves the complete transition matrix, every
//   illegal Lane F edge, and non-copyability.
class PersistentContactSolveTransaction
{
  public:
    PersistentContactSolveTransaction() = default;
    PersistentContactSolveTransaction( const PersistentContactSolveTransaction& ) = delete;
    PersistentContactSolveTransaction& operator=( const PersistentContactSolveTransaction& ) = delete;
    PersistentContactSolveTransaction( PersistentContactSolveTransaction&& ) = delete;
    PersistentContactSolveTransaction& operator=( PersistentContactSolveTransaction&& ) = delete;

    void BeginEntryPolicySetup();
    void Complete();

    PersistentContactSolvePhaseCursor::Phase Phase() const
    {
        return m_phase.Current();
    }

    void ReserveSceneCapacity( std::size_t bodyCapacity );
    void Clear();
    uint64_t CollectDynamicMemoryBytes() const;
    void ResetBodies( std::size_t bodyCount );
    std::size_t BodyCount() const;
    SolverBodyState& Body( std::size_t index );
    const SolverBodyState& Body( std::size_t index ) const;

    static int64_t MakeKey( int bodyA, int bodyB, uint32_t featureId );
    static bool HasCachedImpulse( const PersistentContactCacheList& cache, int bodyA, int bodyB, uint32_t featureId );
    static float ConservativeContactRadius( const ColliderRecord& collider );
    Math::Vector::Vector3 ApplyInverseInertia( int bodyIndex, const Math::Vector::Vector3& value ) const;
    void ApplyImpulse( const PersistentContact& contact, const Math::Vector::Vector3& impulse );

  private:
    friend class PhysicsContactSolverStage;
    friend struct PersistentContactSolveTransactionTestAccess;

    void SetupBodies( const PhysicsBodyStore& bodyStore, std::span<const uint8_t> sleepState, int modelCount,
                      Core::Profiler* profiler );
    void BuildManifolds( PhysicsContactSolverStage& stage, const PhysicsBodyStore& bodyStore,
                         const ColliderStore& colliderStore, const PersistentContactSolverStepPolicy& stepPolicy,
                         std::span<const std::pair<int, int>> candidatePairs, std::span<const uint8_t> sleepState,
                         PhysicsCandidatePairList& sleepSupportEdges, int modelCount, std::size_t pipelineRecordCapacity,
                         Core::Profiler* profiler );
    void BuildTerrainRows( PhysicsContactSolverStage& stage,
                           PhysicsBodyRowList<TerrainContactManifold>& terrainContactManifolds,
                           std::span<const uint8_t> sleepState, int modelCount, std::size_t pipelineRecordCapacity,
                           Core::Profiler* profiler );
    void PrecomputeRows( PhysicsContactSolverStage& stage, const PhysicsBodyStore& bodyStore,
                         const ColliderStore& colliderStore, const PersistentContactSolverStepPolicy& stepPolicy,
                         std::size_t pipelineRecordCapacity, float dt, Core::Profiler* profiler );
    template <bool CollectConvergenceDiagnostics>
    void SolveRowsIterations( PhysicsContactSolverStage& stage, const PhysicsBodyStore& bodyStore,
                              const PersistentContactSolverStepPolicy& stepPolicy, std::size_t pipelineRecordCapacity );
    void SolveRows( PhysicsContactSolverStage& stage, const PhysicsBodyStore& bodyStore,
                    const PersistentContactSolverStepPolicy& stepPolicy, std::size_t pipelineRecordCapacity,
                    Core::Profiler* profiler );
    void ApplyPointSupportInstability( PhysicsContactSolverStage& stage, const PhysicsBodyStore& bodyStore,
                                       const ColliderStore& colliderStore,
                                       const PersistentContactSolverStepPolicy& stepPolicy,
                                       std::span<const uint8_t> sleepState, std::span<const uint8_t> sleepSupportedThisFrame,
                                       int modelCount, float dt, Core::Profiler* profiler );
    void ApplyTerrainRestPolicy( const PhysicsBodyStore& bodyStore, const ColliderStore& colliderStore,
                                 const PersistentContactSolverStepPolicy& stepPolicy,
                                 PhysicsBodyRowList<TerrainContactManifold>& terrainContactManifolds,
                                 std::span<uint8_t> terrainRestApplied, std::span<const uint8_t> sleepState, int modelCount,
                                 float dt, Core::Profiler* profiler );
    void WriteBack( PhysicsContactSolverStage& stage, PhysicsBodyStore& bodyStore, std::span<const uint8_t> sleepState,
                    int modelCount, std::size_t pipelineRecordCapacity, Core::Profiler* profiler );
    void PublishDebugContacts( PhysicsContactSolverStage& stage, const PhysicsBodyStore& bodyStore,
                               PhysicsStepDiagnostics& stepDiagnostics, Core::Profiler* profiler );
    void CorrectPositions( PhysicsContactSolverStage& stage, PhysicsBodyStore& bodyStore,
                           const PersistentContactSolverStepPolicy& stepPolicy, std::span<const uint8_t> sleepState,
                           std::size_t pipelineRecordCapacity, Core::Profiler* profiler );
    void StoreCache( PhysicsContactSolverStage& stage, const PhysicsBodyStore& bodyStore, std::size_t pipelineRecordCapacity,
                     Core::Profiler* profiler );
    void ReleaseFixedContacts( PhysicsContactSolverStage& stage, PhysicsBodyStore& bodyStore, int modelCount,
                               Core::Profiler* profiler );
    void AdvanceOrFatal( PersistentContactSolvePhaseCursor::Phase next, const char* operation );

    PersistentContactSolvePhaseCursor m_phase;
    SolverBodyStateList m_bodies { "PhysicsContactSolverStage.solverBodies", PhysicsCapacityReason::SceneBodies };
};

struct PersistentContactSolverSideEffects
{

    // These are values, not callbacks: the sequencer applies them in the same
    // deterministic order after Solve returns.
    PhysicsPipelineRecordList pipelineRecords { "PhysicsContactSolverStage.pipelineRecords",
                                                PhysicsCapacityReason::PipelineRecords };
    PhysicsCollisionVisualBodyList collisionVisualBodies { "PhysicsContactSolverStage.collisionVisualBodies",
                                                           PhysicsCapacityReason::CollisionVisualBodies };
    PhysicsContactBodyList fixedContactBodies { "PhysicsContactSolverStage.fixedContactBodies",
                                                PhysicsCapacityReason::PersistentContacts };
    PhysicsReleaseWakeBodyList releaseWakeBodies { "PhysicsContactSolverStage.releaseWakeBodies",
                                                   PhysicsCapacityReason::SceneBodies };
    PhysicsFixedTreeReleaseList fixedTreeReleases { "PhysicsContactSolverStage.fixedTreeReleases",
                                                    PhysicsCapacityReason::SceneBodies };
};

class PhysicsContactCacheWakeAccess
{
  private:
    PersistentContactCacheList& m_cache;

  public:

    // Lifetime: this narrow capability borrows the contact owner's cache only
    // for the synchronous wake operation that requested it.
    explicit PhysicsContactCacheWakeAccess( PersistentContactCacheList& cache ) : m_cache( cache )
    {
    }
    void ForgetBody( int bodyIndex ) const;
};

class PhysicsContactSolverStage
{
  private:
    friend class PersistentContactSolveTransaction;

    PersistentContactList m_persistentContacts { "PhysicsContactSolverStage.persistentContacts",
                                                 PhysicsCapacityReason::PersistentContacts };
    PersistentContactCacheList m_persistentContactCache { "PhysicsContactSolverStage.persistentContactCache",
                                                          PhysicsCapacityReason::PersistentContacts };
    PersistentContactSolverStats m_persistentContactSolverStats;
    PersistentContactConvergenceTrace m_persistentContactConvergenceTrace;
    PersistentContactCountList m_persistentContactCounts { "PhysicsContactSolverStage.persistentContactCounts",
                                                           PhysicsCapacityReason::SceneBodies };
    PersistentContactCountList m_persistentRestingContactCounts { "PhysicsContactSolverStage.persistentRestingContactCounts",
                                                                  PhysicsCapacityReason::SceneBodies };
    PersistentContactSolveTransaction m_solveTransaction;
    PersistentContactSolverSideEffects m_sideEffects;

    void PrepareSideEffects( int modelCount, std::size_t candidatePairCount, int pipelineRecordCapacity );

  public:
    PhysicsContactSolverStage();

    void Clear();
    void ReserveSceneCapacity( std::size_t bodyCapacity );

    // Returns the single per-solve normalization of raw stamped settings and
    // live world-force policy. Tests use this seam to pin bounds without
    // recreating solver math.
    static PersistentContactSolverStepPolicy ResolveStepPolicy( const PhysicsRuntimeSettings& settings,
                                                                const PhysicsWorldForces& worldForces ) noexcept;
    void Solve( PhysicsBodyStore& bodyStore, const ColliderStore& colliderStore,
                const PersistentContactSolverStepPolicy& stepPolicy, std::span<const std::pair<int, int>> candidatePairs,
                std::span<const uint8_t> sleepState, PhysicsCandidatePairList& sleepSupportEdges,
                PhysicsBodyRowList<TerrainContactManifold>& terrainContactManifolds, std::span<uint8_t> terrainRestApplied,
                std::span<uint8_t> sleepSupportedThisFrame, PhysicsStepDiagnostics& stepDiagnostics, float dt,
                Core::Profiler* profiler );
    PhysicsContactCacheWakeAccess CreateWakeAccess();

    void CaptureReplayState( PhysicsSolverSnapshot& outSnapshot ) const;
    void RestoreReplayState( const PhysicsSolverSnapshot& snapshot );

    std::span<const PersistentContact> GetPersistentContacts() const;
    std::span<const PersistentContactCacheEntry> GetPersistentContactCache() const;
    const PersistentContactSolverStats& GetStats() const;
    const PersistentContactConvergenceTrace& GetConvergenceTrace() const;
    std::span<const uint16_t> GetPersistentContactCounts() const;
    std::span<const uint16_t> GetPersistentRestingContactCounts() const;
    const PersistentContactSolverSideEffects& GetSideEffects() const;
    uint64_t CollectDynamicMemoryBytes() const;
};
} // namespace Physics
} // namespace SkullbonezCore
