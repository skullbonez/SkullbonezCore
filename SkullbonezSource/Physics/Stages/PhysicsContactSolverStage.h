/*
Purpose:
  Owns persistent-contact rows, cache, the guarded solve transaction,
  statistics, and outputs.

Invariants:
  - Owned lists commit scene-derived runtime capacities before play and fail
    loudly rather than grow during steady gameplay.
  - Solve proves every consequence lane's required capacity independently,
    then prepares a fresh batch before invoking the row solver.
  - Contact phases advance in one adjacent order. The two existing no-work
    exits may terminate only from entry/setup or joint preparation.
  - The solve transaction owns solver-body scratch and impulse application; it
    retains no borrowed store, span, stage, or world pointer.
  - Normal, sliding, rolling, spin and point-joint impulses share one transaction. The
    later terrain-rest phase publishes support policy and mutates no velocity.
  - Pipeline event counts remain exact in both payload modes; count-only
    specializations leave the consequence record list empty.
  - The stage retains no pointer or reference to PhysicsWorld or borrowed rows.
  - Contact-interval derivation reads the PhysicsWorld-owned sleep and
    remaining-time spans synchronously; the transaction never retains either
    borrow.
  - Wake propagation receives only a cache-invalidation capability, never the
    concrete contact-solver owner.
  - At most 64 convergence samples survive one solve; excess iterations are
    counted, and replay capture never copies the trace.
*/
#pragma once

#include <cstdint>
#include <span>
#include <utility>
#include <vector>

#include "../PersistentContactSolver.h"
#include "../PointJointBlock.h"
#include "../ConstraintIslandSchedule.h"
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
struct ConstraintSolveTransactionTestAccess;
struct PersistentContactPositionCorrectionTestAccess;
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

class ConstraintSolvePhaseCursor
{
  public:
    enum class Phase : uint8_t
    {
        Idle,
        EntryPolicySetup,
        BodySetup,
        BuildManifolds,
        TerrainRows,
        PrepareJoints,
        Precompute,
        WarmStartJoints,
        SolveRows,
        PointSupportInstability,
        WriteBack,
        DebugContacts,
        PositionCorrection,
        FixedContactRelease,
        ReleasedBodySetup,
        ReleasedManifolds,
        ReleasedTerrainRows,
        ReleasedJoints,
        ReleasedPrecompute,
        ReleasedSolveRows,
        ReleasedWriteBack,
        ReleasedDebugContacts,
        CacheStore,
        Complete,
        Count
    };

    static constexpr bool IsLegalTransition( Phase from, Phase to )
    {
        const bool adjacent = from >= Phase::Idle && from < Phase::Complete &&
                              to == static_cast<Phase>( static_cast<uint8_t>( from ) + 1u );
        const bool noInput = from == Phase::EntryPolicySetup && to == Phase::Complete;
        const bool noRows = from == Phase::PrepareJoints && to == Phase::Complete;
        const bool normalCompletion = from == Phase::FixedContactRelease && to == Phase::CacheStore;
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

// Owns the shared body scratch, preparation, warm starts, sweeps, and publication.
// Fixed-contact release precedes cache storage. When release changes connectivity,
// PhysicsWorld wakes the new component and resumes explicit Released* phases;
// the same transaction then publishes final caches and reaches Complete.
// TestRuntimeContracts proves every legal/illegal phase edge and non-copyability.
class ConstraintSolveTransaction
{
  public:
    ConstraintSolveTransaction() = default;
    ConstraintSolveTransaction( const ConstraintSolveTransaction& ) = delete;
    ConstraintSolveTransaction& operator=( const ConstraintSolveTransaction& ) = delete;
    ConstraintSolveTransaction( ConstraintSolveTransaction&& ) = delete;
    ConstraintSolveTransaction& operator=( ConstraintSolveTransaction&& ) = delete;

    void BeginEntryPolicySetup();
    void Complete();

    ConstraintSolvePhaseCursor::Phase Phase() const
    {
        return m_phase.Current();
    }

    void ReserveSceneCapacity( std::size_t bodyCapacity, std::size_t jointCapacity = 0u );
    void Clear();
    uint64_t CollectDynamicMemoryBytes() const;
    void ResetBodies( std::size_t bodyCount );
    std::size_t BodyCount() const;
    std::span<const PointJointIterationSample> JointSamples() const
    {
        return m_jointSamples;
    }
    SolverBodyState& Body( std::size_t index );
    const SolverBodyState& Body( std::size_t index ) const;

    static int64_t MakeKey( int bodyA, int bodyB, uint32_t featureId );
    static float ConservativeContactRadius( const ColliderRecord& collider );
    Math::Vector::Vector3 ApplyInverseInertia( int bodyIndex, const Math::Vector::Vector3& value ) const;
    void ApplyImpulse( const PersistentContact& contact, const Math::Vector::Vector3& impulse );

  private:
    friend class PhysicsContactSolverStage;
    friend struct ConstraintSolveTransactionTestAccess;
    friend struct PersistentContactPositionCorrectionTestAccess;

    bool IsReactivatedPair( int a, int b ) const;
    void RecomputeContactMass( PersistentContact& contact );
    void OrderCandidatePairs( const PhysicsBodyStore& bodyStore, std::span<const std::pair<int, int>> candidates );
    void PrepareJoints( PhysicsContactSolverStage& stage, const PhysicsBodyStore& bodyStore,
                        std::span<const PointJointConstraint> constraints, const PersistentContactSolverStepPolicy& policy );
    void WarmStartJoints();
    void StoreJointImpulses( std::span<PointJointConstraint> constraints ) const;
    template <bool CollectDiagnostics> void SolveJointBlocks( const PhysicsBodyStore& bodyStore, int iteration );
    void SetupBodies( const PhysicsBodyStore& bodyStore, std::span<const uint8_t> sleepState, int modelCount,
                      Core::Profiler* profiler );
    template <bool RetainPipelineRecords>
    void BuildManifolds( PhysicsContactSolverStage& stage, const PhysicsBodyStore& bodyStore,
                         const ColliderStore& colliderStore, const PersistentContactSolverStepPolicy& stepPolicy,
                         std::span<const std::pair<int, int>> candidatePairs, std::span<const uint8_t> sleepState,
                         PhysicsCandidatePairList& sleepSupportEdges, int modelCount, std::size_t pipelineRecordCapacity,
                         Core::Profiler* profiler );
    template <bool RetainPipelineRecords>
    void BuildTerrainRows( PhysicsContactSolverStage& stage, const PhysicsBodyStore& bodyStore,
                           const PersistentContactSolverStepPolicy& stepPolicy,
                           PhysicsBodyRowList<TerrainContactManifold>& terrainContactManifolds,
                           std::span<const uint8_t> sleepState, std::span<const float> timeRemaining, int modelCount,
                           std::size_t pipelineRecordCapacity, float dt, Core::Profiler* profiler );
    template <bool RetainPipelineRecords>
    void PrecomputeRows( PhysicsContactSolverStage& stage, const PhysicsBodyStore& bodyStore,
                         const ColliderStore& colliderStore, const PersistentContactSolverStepPolicy& stepPolicy,
                         std::span<const uint8_t> sleepState, std::span<const float> timeRemaining,
                         std::size_t pipelineRecordCapacity, float dt, Core::Profiler* profiler );
    void PrecomputeTerrainAngularResistance( PersistentContact& contact, const ColliderRecord& collider,
                                             const PersistentContactSolverStepPolicy& stepPolicy, float normalVelocity );
    template <bool CollectConvergenceDiagnostics, bool RetainPipelineRecords>
    void SolveRowsIterations( PhysicsContactSolverStage& stage, const PhysicsBodyStore& bodyStore,
                              const PersistentContactSolverStepPolicy& stepPolicy, std::size_t pipelineRecordCapacity );
    void SolveRows( PhysicsContactSolverStage& stage, const PhysicsBodyStore& bodyStore,
                    const PersistentContactSolverStepPolicy& stepPolicy, bool retainPipelineRecords,
                    std::size_t pipelineRecordCapacity, Core::Profiler* profiler );
    void ApplyPointSupportInstability( PhysicsContactSolverStage& stage, const PhysicsBodyStore& bodyStore,
                                       const ColliderStore& colliderStore,
                                       const PersistentContactSolverStepPolicy& stepPolicy,
                                       std::span<const uint8_t> sleepState, std::span<const uint8_t> sleepSupportedThisFrame,
                                       int modelCount, float dt, Core::Profiler* profiler );
    template <bool RetainPipelineRecords>
    void WriteBack( PhysicsContactSolverStage& stage, PhysicsBodyStore& bodyStore, std::span<const uint8_t> sleepState,
                    int modelCount, std::size_t pipelineRecordCapacity, Core::Profiler* profiler );
    void PublishDebugContacts( PhysicsContactSolverStage& stage, const PhysicsBodyStore& bodyStore,
                               PhysicsStepDiagnostics& stepDiagnostics, Core::Profiler* profiler );
    template <bool RetainPipelineRecords>
    void CorrectPositions( PhysicsContactSolverStage& stage, PhysicsBodyStore& bodyStore,
                           const PersistentContactSolverStepPolicy& stepPolicy, std::span<const uint8_t> sleepState,
                           std::size_t pipelineRecordCapacity, Core::Profiler* profiler );
    template <bool RetainPipelineRecords>
    void StoreCache( PhysicsContactSolverStage& stage, const PhysicsBodyStore& bodyStore, std::size_t pipelineRecordCapacity,
                     Core::Profiler* profiler );
    void ReleaseFixedContacts( PhysicsContactSolverStage& stage, PhysicsBodyStore& bodyStore, int modelCount,
                               Core::Profiler* profiler );
    void AdvanceOrFatal( ConstraintSolvePhaseCursor::Phase next, const char* operation );

    PhysicsCandidatePairList m_candidatePairs { "ConstraintSolveTransaction.candidatePairs",
                                                PhysicsCapacityReason::CandidatePairs };
    PhysicsBodyRowList<int> m_reactivatedBodies { "ConstraintSolveTransaction.reactivatedBodies",
                                                  PhysicsCapacityReason::SceneBodies };
    std::size_t m_continuationContactCount = 0u;
    ConstraintIslandSchedule m_islands;
    PhysicsBodyRowList<PointJointBlock> m_jointBlocks { "ConstraintSolveTransaction.jointBlocks",
                                                        PhysicsCapacityReason::SceneBodies };
    PhysicsBodyRowList<PointJointIterationSample> m_jointSamples { "ConstraintSolveTransaction.jointSamples",
                                                                   PhysicsCapacityReason::SceneBodies };
    ConstraintSolvePhaseCursor m_phase;
    SolverBodyStateList m_bodies { "PhysicsContactSolverStage.solverBodies", PhysicsCapacityReason::SceneBodies };
};

struct PersistentContactSolverSideEffects
{
    // These are values, not callbacks: the sequencer applies them in the same
    // deterministic order after Solve returns.
    // Invariant: a step publishes either ordered pipelineRecords or their
    // equivalent pipelineEventCount. Count-only mode leaves the record list
    // empty so no payload can be mistaken for live diagnostic output.
    PhysicsPipelineRecordList pipelineRecords { "PhysicsContactSolverStage.pipelineRecords",
                                                PhysicsCapacityReason::PipelineRecords };
    std::size_t pipelineEventCount = 0;
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
    friend class ConstraintSolveTransaction;
    friend struct PersistentContactPositionCorrectionTestAccess;
    friend struct PhysicsContactSolverStageTestAccess;

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
    ConstraintSolveTransaction m_solveTransaction;
    PersistentContactSolverSideEffects m_sideEffects;

    // Invariant: all five consequence lanes are proved independently before
    // Solve can publish into fixed-capacity storage.
    bool CanAppendObjectManifold( std::size_t pointCount ) const;
    void RestoreContactRows( std::span<const PhysicsSolverPersistentContactSample> samples );
    void RestoreContactCache( std::span<const PhysicsSolverContactCacheSample> samples );

    void PrepareSideEffects( int modelCount, std::size_t candidatePairCount, int pipelineRecordCapacity );

  public:
    PhysicsContactSolverStage();

    void Clear();
    void ReserveSceneCapacity( std::size_t bodyCapacity, std::size_t jointCapacity = 0u );

    // Returns the single per-solve normalization of raw stamped settings and
    // live world-force policy. Tests use this seam to pin bounds without
    // recreating solver math.
    static PersistentContactSolverStepPolicy ResolveStepPolicy( const PhysicsRuntimeSettings& settings,
                                                                const PhysicsWorldForces& worldForces,
                                                                float stepDurationSeconds ) noexcept;
    void Solve( PhysicsBodyStore& bodyStore, const ColliderStore& colliderStore,
                const PersistentContactSolverStepPolicy& stepPolicy, std::span<const std::pair<int, int>> candidatePairs,
                std::span<const uint8_t> sleepState, std::span<const float> timeRemaining,
                PhysicsCandidatePairList& sleepSupportEdges,
                PhysicsBodyRowList<TerrainContactManifold>& terrainContactManifolds,
                std::span<uint8_t> sleepSupportedThisFrame, PhysicsStepDiagnostics& stepDiagnostics,
                std::span<PointJointConstraint> joints = {} );
    bool HasPendingReleasedConstraints() const
    {
        return m_solveTransaction.Phase() == ConstraintSolvePhaseCursor::Phase::FixedContactRelease;
    }
    void PrepareReleasedBodies( const PhysicsBodyStore& bodies, std::span<const uint8_t> sleepState );
    std::span<const int> ReactivatedBodies() const
    {
        return m_solveTransaction.m_reactivatedBodies;
    }
    void ContinueReleasedConstraints( PhysicsBodyStore& bodies, const ColliderStore& colliders,
                                      const PersistentContactSolverStepPolicy& policy,
                                      std::span<const std::pair<int, int>> candidates, std::span<const uint8_t> sleep,
                                      std::span<const float> timeRemaining, PhysicsCandidatePairList& supportEdges,
                                      PhysicsBodyRowList<TerrainContactManifold>& terrain,
                                      PhysicsStepDiagnostics& diagnostics, std::span<PointJointConstraint> joints );
    PhysicsContactCacheWakeAccess CreateWakeAccess();

    void CaptureReplayState( PhysicsSolverSnapshot& outSnapshot ) const;

    // Invariant: replay restore is atomic only when every contact/count row is
    // coherent and already fits this stage's committed backing.
    bool CanRestoreReplayState( const PhysicsSolverSnapshot& snapshot, int modelCount ) const noexcept;
    void RestoreReplayState( const PhysicsSolverSnapshot& snapshot );

    std::span<const PersistentContact> GetPersistentContacts() const;
    std::span<const PersistentContactCacheEntry> GetPersistentContactCache() const;
    const PersistentContactSolverStats& GetStats() const;
    const PersistentContactConvergenceTrace& GetConvergenceTrace() const;
    std::span<const PointJointIterationSample> GetJointSamples() const
    {
        return m_solveTransaction.JointSamples();
    }
    std::span<const uint16_t> GetPersistentContactCounts() const;
    std::span<const uint16_t> GetPersistentRestingContactCounts() const;
    const PersistentContactSolverSideEffects& GetSideEffects() const;
    uint64_t CollectDynamicMemoryBytes() const;
};
} // namespace Physics
} // namespace SkullbonezCore
