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
  Immutable projection: Field-specific borrowed store or diagnostic read whose
    lifetime remains tied to PhysicsEngine.
  Descriptor refresh: Cold authoring edge that replaces body rows from explicit
    values supplied by the model collection owner.
  Replay prediction seed: Phase-checked clone of authored topology and concrete
    store state into Prediction's isolated engine before snapshot restore.

Invariants:
  - Solver, store-refresh, replay, and diagnostics call order remains deterministic.
  - PhysicsWorld is the cohesive solver implementation owned by PhysicsEngine;
    callers receive no mutable-store or solver authority.
  - Replay prediction seeding requires a registered Replay growth owner and
    delegates clone authority to concrete stores; ordinary value transfer stays
    deleted.

Related:
  - SkullbonezSource/Physics/PhysicsEngine.cpp
  - SkullbonezSource/Physics/PhysicsApi.h
  - SkullbonezSource/Physics/PhysicsWorld.h
  - Agentic/Reference/engine-glossary.md
*/
#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <vector>

#include "ColliderStore.h"
#include "BuoyancySystem.h"
#include "PhysicsBodyStore.h"
#include "PhysicsBroadphaseDebugView.h"
#include "PhysicsDiagnosticsSink.h"
#include "PhysicsDiagnosticsView.h"
#include "PhysicsObjectPolicy.h"
#include "PhysicsRuntimeSettings.h"
#include "PhysicsSolverSnapshot.h"
#include "PhysicsStageCapacity.h"
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
struct ExternalForceFrameInput;
class PhysicsWorld;

class PhysicsEngine
{
  public:
    PhysicsEngine();
    ~PhysicsEngine();
    PhysicsEngine( const PhysicsEngine& ) = delete;
    PhysicsEngine& operator=( const PhysicsEngine& ) = delete;
    PhysicsEngine( PhysicsEngine&& ) = delete;
    PhysicsEngine& operator=( PhysicsEngine&& ) = delete;
    void BindProfiler( SkullbonezCore::Core::Profiler* profiler ) noexcept;

    void ApplyRuntimeConfig( const SkullbonezCore::Core::EngineConfig& config );

    // Cold conversion seam used by config stamping and field-faithfulness tests.
    // Fixed-step code receives only the returned Physics-owned value snapshot.
    static PhysicsRuntimeSettings RuntimeSettingsFromConfig( const SkullbonezCore::Core::EngineConfig& config );

    // Stamps the PhysicsEngine-owned runtime policy onto cold authoring
    // descriptors before they become store rows.
    void ApplyAuthoredBodyPolicy( PhysicsBodyCreateDesc& desc ) const;
    void ApplyAuthoredColliderPolicy( PhysicsColliderCreateDesc& desc ) const;

    // SceneWorld orders one monotonic scene-load commit across concrete owners.
    // Shape counts are per-kind backing limits; point joints use the exact
    // authored/ragdoll allowance and fail loud if creation exceeds it.
    void ReserveAuthoredBodyCapacity( std::size_t bodyCapacity, std::size_t sphereCapacity = 0u,
                                      std::size_t boxCapacity = 0u, std::size_t hullCapacity = 0u,
                                      std::size_t pointJointCapacity = 0u );

    // Synchronously seeds private replay-prediction storage through explicit
    // concrete store/topology clones. The destination remains unpublished and
    // unstepped until its caller restores body values and PhysicsSolverSnapshot;
    // the exact canonical prediction owner scope must already be active.
    void SeedReplayPredictionStorageFrom( const PhysicsEngine& source );

    // Cold editor/tool topology can extend a loaded scene one body at a time.
    // A complete load-time commit makes this a no-op during initial population.
    void ReserveAdditionalAuthoredBodyCapacity( const PhysicsColliderCreateDesc& colliderDesc );
    void ReserveAdditionalAuthoredCapacity( std::size_t sphereCount, std::size_t boxCount, std::size_t hullCount,
                                            std::size_t pointJointCount );
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
    void Step( float deltaSeconds, const PhysicsWorldForces& worldForces, Threading::WorkerPool& workerPool,
               const PhysicsDiagnosticsCsvWriter& diagnosticsCsvWriter );
    void Step( float deltaSeconds, const PhysicsWorldForces& worldForces, const ExternalForceFrameInput& externalForces,
               Threading::WorkerPool& workerPool, const PhysicsDiagnosticsCsvWriter& diagnosticsCsvWriter );

    // Runtime fixed-tree commands enter physics by handle; release, wake, and
    // sleep propagation stay inside the owned stores.
    bool ReleaseFixedBodyAndAttachedTreeParts( PhysicsBodyHandle sourceBody, float releaseImpulseStrength,
                                               const Math::Vector::Vector3& seedLinearVelocity,
                                               const Math::Vector::Vector3& seedAngularVelocity );

    // Wakes solver sleep/island state by handle. Model-index callers
    // must refresh topology before entering this command.
    void WakeBody( PhysicsBodyHandle body );

    // Live tool commands edit body velocity by handle; model-index
    // callers refresh topology before entering this store-owned command.
    bool SetBodyVelocity( PhysicsBodyHandle body, const Math::Vector::Vector3& linearVelocity,
                          const Math::Vector::Vector3& angularVelocity, bool wakeIfMoving );

    // Scene/editor construction commands seed solver sleep state by handle
    // without a per-command presentation projection.
    void SeedBodyAsleep( PhysicsBodyHandle body );

    // Queues one-shot solver input by body handle. The application offset is a
    // world-space vector from the body's center of mass. Callers that only need
    // a pending impulse must not rebuild descriptor rows for presentation wake.
    void SetPendingBodyImpulse( PhysicsBodyHandle body, const Math::Vector::Vector3& impulse,
                                const Math::Vector::Vector3& worldApplicationOffset );

    // Queues a one-shot impulse and wakes by body handle without borrowing the
    // model owner.
    void ApplyBodyImpulse( PhysicsBodyHandle body, const Math::Vector::Vector3& impulse,
                           const Math::Vector::Vector3& worldApplicationOffset );
    void SetSleepEnabled( bool enabled );
    bool IsSleepEnabled() const;
    void BeginCollisionVisualFrame( PhysicsBodyCount bodyCount );
    void EndCollisionVisualFrame();

    // Counting remains active for Replay identity. Runtime calls this before a
    // step to retain payload rows only when a full-record consumer is live.
    void SetPipelineTraceFullRecordConsumerActive( bool active );
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

    // Replay caller contract: CanRestore is a non-mutating preflight and must
    // precede topology trim. Restore revalidates the snapshot before commit.
    void CaptureReplaySolverSnapshot( PhysicsSolverSnapshot& outSnapshot, PhysicsBodyCount bodyCount ) const;

    // Prediction seeding needs physical solver state but not the previous
    // frame's diagnostic trace. This explicit lane remains valid after a
    // count-only step and restores an empty trace into the private engine.
    void CaptureReplaySimulationSnapshot( PhysicsSolverSnapshot& outSnapshot, PhysicsBodyCount bodyCount ) const;
    bool CanRestoreReplaySolverSnapshot( const PhysicsSolverSnapshot& snapshot, PhysicsBodyCount bodyCount ) const;
    bool RestoreReplaySolverSnapshot( const PhysicsSolverSnapshot& snapshot, PhysicsBodyCount bodyCount );
    PhysicsDiagnosticsView GetDiagnosticsView() const;
    uint64_t CollectPhysicsWorldMemoryBytes() const;
    uint64_t CollectDebugAndBroadphaseMemoryBytes() const;
    uint64_t CollectSceneSizedStoreMemoryBytes() const;

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
    static float ReadBroadphaseCellSize( const PhysicsEngine& engine );
    static int ReadBroadphaseActiveCells( const PhysicsEngine& engine, std::span<PhysicsBroadphaseActiveCell> outCells );
    static std::span<const int> ReadFixedContactHighlightBodies( const PhysicsEngine& engine );
    static std::span<const int64_t> ReadCollisionCellKeys( const PhysicsEngine& engine );
    static std::span<const uint8_t> ReadCollisionVisualContacts( const PhysicsEngine& engine );
    static std::span<const uint8_t> ReadSleepStates( const PhysicsEngine& engine );
    static std::span<const int> ReadSleepIslandVisualIds( const PhysicsEngine& engine );
    static std::span<const uint8_t> ReadSleepSupportedStates( const PhysicsEngine& engine );
    static std::span<const uint8_t> ReadSleepInhibitedStates( const PhysicsEngine& engine );
    static std::span<const PhysicsDebugContact> ReadDebugContacts( const PhysicsEngine& engine );
    static uint32_t ReadPipelineRecordCount( const PhysicsEngine& engine );
    static std::span<const PhysicsPipelineRecord> ReadPipelineTrace( const PhysicsEngine& engine );
    static const PhysicsBodyRowList<PointJointConstraint>& ReadPointJointConstraints( const PhysicsEngine& engine );
    static std::size_t ReadPointJointCapacity( const PhysicsEngine& engine );

#if defined( _DEBUG ) || defined( SKULLBONEZ_AUTOMATION_DIAGNOSTICS )
    void SetPhysicsRegressionLogPath( const char* path );
    void SetPhysicsCollisionTimeLogPath( const char* path );
    void SetPhysicsDiagnosticsPath( const char* path );
    void SetPhysicsDiagnosticsRunId( const char* runId );
#endif

  private:
    void LoadBodyDescriptors( const std::vector<PhysicsBodyCreateDesc>& bodyDescs );
    void ApplyFixedTreeReleaseEvents( const PhysicsWorldForces& worldForces );

    // Lifetime: this fixed-size owner allocation is created and destroyed with
    // PhysicsEngine. PhysicsWorld owns its hot stage storage; the indirection is
    // confined to engine/world boundaries rather than leaking into consumers.
    std::unique_ptr<PhysicsWorld> m_world;
    PhysicsBodyRowList<PhysicsBodyCreateDesc>
        m_authoredBodyDescs { "PhysicsEngine.m_authoredBodyDescs",
                              PhysicsCapacityReason::SceneBodies }; // Cold descriptors keyed by scene/model order.
    PhysicsBodyStore m_bodyStore;                                   // Mutable body state in model/replay order.
    ColliderStore m_colliderStore;                                  // Collider snapshot in model/replay order.
    BuoyancySystem m_buoyancySystem;                                // Fluid facts aligned with body/collider model rows.
    PhysicsMaterial m_physicsMaterial;                  // Runtime material policy copied into body/collider descriptors.
    BodySimulationLimits m_bodySimulationLimits;        // Runtime body caps copied at authoring/import boundaries.
    ContactPolicy m_contactPolicy;                      // Runtime contact thresholds copied at authoring/import boundaries.
    PhysicsRuntimeSettings m_runtimeSettings;           // Physics-owned process settings stamped before fixed stepping.
    PhysicsWorldForces m_lastWorldForces;               // Last real step boundary forces used by explicit wake commands.
    bool m_hasLastWorldForces = false;                  // False until the first physics step supplies world forces.
    PhysicsBodyIndexList m_fixedTreeReleaseWakeBodies { // Fixed owner-edge wake list; never grows during release.
                                                        "PhysicsEngine fixed-tree release output",
                                                        PhysicsCapacityReason::SceneBodies };
    mutable PhysicsBodyHandleList m_broadphaseQueryScratch { // Borrowed query result, replaced by the next query.
                                                             "PhysicsEngine broadphase query results",
                                                             PhysicsCapacityReason::SceneBodies };
};
} // namespace Physics
} // namespace SkullbonezCore
