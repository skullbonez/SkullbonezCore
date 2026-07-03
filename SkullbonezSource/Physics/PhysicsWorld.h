/*
File: SkullbonezSource/Physics/PhysicsWorld.h
Purpose:
  Owns per-scene physics working state shared by broadphase, solver, and diagnostics.

Mental model:
  Physics is deterministic fixed-step state update. Units, contact ownership,
  solver stages, sleep policy, and baseline-sensitive behavior are the key
  reading anchors.

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

Invariants:
  - Physics-visible behavior must remain deterministic; byte-exact baselines
    are the validation contract.

Related:
  - SkullbonezSource/Physics/PhysicsWorld.cpp
  - Agentic/Reference/physics-overview.md
  - Agentic/Reference/comment-style-guide.md
*/
#pragma once

#include <array>
#include <cstdint>
#include <utility>
#include <vector>

#include "PersistentContactSolver.h"
#include "PhysicsModelAccess.h"
#include "PhysicsDiagnosticsSink.h"
#include "Debug/PhysicsDebugVisualizer.h"
#include "Ragdoll.h"
#include "../Runtime/Replay/ReplaySolverSnapshot.h"
#include "SleepIslandSystem.h"
#include "SpatialGrid.h"
#include "TerrainContactManifold.h"
#include "TornadoField.h"

namespace SkullbonezCore
{
namespace Basics
{
class EngineConfig;
} // namespace Basics

namespace GameObjects
{
struct GameModelBodyStream;
} // namespace GameObjects

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
struct PhysicsDiagnosticsView;
struct PhysicsWorldForces;
struct PersistentContactSolverSideEffects;
struct PersistentContactSolverContext;
struct SleepSupportPropagationContext;

struct PersistentContactSolverSideEffects
{
    // Solver output queues. The persistent solver appends plain body indices and
    // records into these vectors; PhysicsWorld applies owner-side consequences
    // after the solve so the hot contact loop stays on dense physics storage.
    std::vector<PhysicsPipelineRecord> pipelineRecords;
    std::vector<int> collisionVisualBodies;
    std::vector<int> fixedContactBodies;
    std::vector<int> bodyMirrorWritebacks;
    std::vector<int> releaseWakeBodies;
    std::vector<PhysicsFixedTreeReleaseEvent> fixedTreeReleases;
};

class PhysicsWorld
{
  private:
    // Broadphase output for the current fixed tick.
    //
    // SpatialGrid bins objects into cells, m_candidatePairs stores object pairs
    // that might collide, and m_timeRemaining tracks how much of the fixed step
    // each body still has after swept movement/impact handling.
    Math::CollisionDetection::SpatialGrid m_spatialGrid;
    std::vector<std::pair<int, int>> m_candidatePairs;
    std::vector<float> m_timeRemaining;

    // Sleep policy working state.
    //
    // Sleeping is a performance and stability optimization: bodies that are
    // supported and quiet can stop integrating until something wakes them. The
    // "supported" and "inhibited" arrays are rebuilt each frame from contacts.
    // PhysicsBodyStore owns the persisted sleep flag; m_sleepState is the
    // solver's model-indexed compatibility mirror for existing diagnostics and
    // sleep algorithms. A fully submerged sleeping sphere also gets a one-way
    // lock so water-floor balls behave like static rocks instead of rejoining
    // buoyancy/contact churn.
    std::vector<uint8_t> m_sleepSupportedThisFrame;
    std::vector<uint8_t> m_sleepInhibitedThisFrame;
    std::vector<uint8_t> m_sleepState;
    std::vector<uint8_t> m_sleepCounter;
    std::vector<uint8_t> m_underwaterSleepLocked;
    std::vector<float> m_tornadoCaptureSeconds;
    std::vector<float> m_tornadoEjectCooldownSeconds;

    // Debug visualization state. These arrays intentionally mirror model index
    // order so render/debug code can look up one byte/id per GameModel without
    // doing map lookups in the overlay path.
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

  public:
    struct PersistentContact
    {
        // One solver row for one contact point. bodyB == -1 means static
        // terrain. accN/accT1/accT2 are accumulated impulses reused by warm
        // starting; they are cache-sensitive and therefore validation-sensitive.
        int bodyA = -1;
        int bodyB = -1;
        uint32_t featureId = 0;
        int64_t key = 0;
        Math::Vector::Vector3 normal = Math::Vector::ZERO_VECTOR;
        Math::Vector::Vector3 tangent1 = Math::Vector::ZERO_VECTOR;
        Math::Vector::Vector3 tangent2 = Math::Vector::ZERO_VECTOR;
        Math::Vector::Vector3 rA = Math::Vector::ZERO_VECTOR;
        Math::Vector::Vector3 rB = Math::Vector::ZERO_VECTOR;
        float penetration = 0.0f;
        float normalMass = 0.0f;
        float tangentMass1 = 0.0f;
        float tangentMass2 = 0.0f;
        float bias = 0.0f;
        float frictionLimit = 0.0f;
        float accN = 0.0f;
        float accT1 = 0.0f;
        float accT2 = 0.0f;
        bool warmStarted = false;
        bool isTerrain = false;
        bool supportsRestingPolicy = true;
        bool allowsTangentFriction = true;
        bool normalCoupledFriction = false;
        bool inhibitsSleep = false;
        uint8_t manifoldPointCount = 1;
        Math::Vector::Vector3 terrainNormal = Math::Vector::ZERO_VECTOR;
        float terrainWarmStart = 0.0f;
        // Contact-point speeds captured before this row applies solver impulses.
        // Audio and diagnostics use these to reject force-transfer rows that had
        // no real relative impact motion.
        float preSolveNormalSpeed = 0.0f;
        float preSolveClosingSpeed = 0.0f;
        float preSolveSlipSpeed = 0.0f;
    };

  public:
    struct PersistentContactSolverStats
    {
        // Bounded per-frame counters for SkullScope, profiler overlays, and
        // regression diagnostics. These explain what the solver did without
        // forcing agents to ingest full raw CSV/NDJSON artifacts.
        int rowCount = 0;
        int cachePreviousRows = 0;
        int cacheHits = 0;
        int cacheMisses = 0;
        int warmStartedRows = 0;
        int positionCorrectionRows = 0;
        int solverIterations = 0;
        float positionCorrectionTotal = 0.0f;
        float positionCorrectionMax = 0.0f;
    };

  private:
    struct TerrainDetectionCandidate
    {
        float availableTime = 0.0f;
        TerrainContactSweepResult sweep;
        uint8_t tested = 0;
    };

    enum class ObjectNarrowphaseEventKind : uint8_t
    {
        None,
        SweptObjectHit,
        SweptObjectMiss,
        WakeDecision
    };

    struct ObjectNarrowphaseEvent
    {
        ObjectNarrowphaseEventKind kind = ObjectNarrowphaseEventKind::None;
        PhysicsPipelineRecord pipelineRecord;
        int collisionTimeBodyA = -1;
        int collisionTimeBodyB = -1;
        float collisionTime = 0.0f;
        float availableTime = 0.0f;
        int visualBodyA = -1;
        int visualBodyB = -1;
        int64_t collisionCellKey = 0;
        uint8_t hasPipelineRecord = 0;
        uint8_t emitCollisionTime = 0;
        uint8_t markVisualContact = 0;
        uint8_t hasCollisionCellKey = 0;
    };

    struct ObjectNarrowphaseIsland
    {
        int minPairIndex = 0;
        std::vector<int> pairIndices;
    };

    // Persistent rows and diagnostics produced during the current fixed tick.
    // Terrain manifolds are appended into the same row solver as object/object
    // contacts so velocity response has one owner.
    std::vector<PersistentContact> m_persistentContacts;
    std::vector<PersistentContactCacheEntry> m_persistentContactCache;
    PersistentContactSolverStats m_persistentContactSolverStats;
    std::vector<uint16_t> m_persistentContactCounts;
    std::vector<uint16_t> m_persistentRestingContactCounts;
    std::vector<SolverBodyState> m_solverBodies;
    std::vector<PhysicsDebugContact> m_physicsDebugContacts;
    std::vector<PhysicsPipelineRecord> m_physicsPipelineTrace;
    PersistentContactSolverSideEffects m_persistentContactSideEffects;
    std::vector<TerrainContactManifold> m_terrainContactManifolds;
    std::vector<TerrainDetectionCandidate> m_terrainDetectionCandidates;
    std::vector<ObjectNarrowphaseEvent> m_objectNarrowphaseEvents;
    std::vector<ObjectNarrowphaseIsland> m_objectNarrowphaseIslands;
    std::vector<int> m_objectNarrowphaseParent;
    std::vector<uint8_t> m_objectNarrowphaseRank;
    std::vector<int> m_objectNarrowphaseRootToIsland;
    std::vector<PointJointConstraint> m_pointJointConstraints;
    std::vector<int64_t> m_collisionCellKeys;
    std::array<uint8_t, MAX_GAME_MODELS> m_terrainRestApplied = {};
    TornadoField m_tornadoField;
    TornadoSystem m_tornadoSystem;
    PersistentContactSolver m_contactSolver;
    SleepIslandSystem m_sleepIslandSystem;
    PhysicsDiagnosticsSink m_diagnostics;
#ifdef _DEBUG
    bool m_diagnosticsSuppressed = false;
#endif

    void RunSolverPhysics( PhysicsModelAccess& modelAccess,
                           PhysicsBodyStore& bodyStore,
                           const ColliderStore& colliderStore,
                           float dt,
                           const Basics::EngineConfig& config,
                           const PhysicsWorldForces& worldForces,
                           Threading::WorkerPool& workerPool );
    void SolvePersistentObjectContacts( PhysicsModelAccess& modelAccess, float dt );
#ifdef _DEBUG
    void EmitPhysicsDiagnosticsFrame( PhysicsModelAccess& modelAccess, float dt );
#endif
    void EmitPhysicsCollisionTime( PhysicsModelAccess& modelAccess,
                                   const char* type,
                                   int bodyA,
                                   int bodyB,
                                   float collisionTime,
                                   float availableTime );
    PersistentContactSolverContext
    CreatePersistentContactSolverContext( const GameObjects::GameModelBodyStream& bodyStream,
                                          PhysicsBodyStore& bodyStore,
                                          const ColliderStore& colliderStore,
                                          const Basics::EngineConfig& config );
    void PreparePersistentContactSideEffects( int modelCount );
    void ApplyPersistentContactSideEffects( PhysicsModelAccess& modelAccess,
                                            PhysicsBodyStore& bodyStore,
                                            const ColliderStore& colliderStore,
                                            const PhysicsWorldForces& worldForces );
    SleepSupportPropagationContext CreateSleepSupportPropagationContext();
    bool CanRecordPhysicsPipelineStage() const;
    void RecordPhysicsPipelineStage( const PhysicsPipelineRecord& record );
    void EnsureCollisionVisualBuffers( int modelCount );
    void EnsureTornadoStateBuffers( int modelCount );
    void EnsureUnderwaterSleepLockBuffer( int modelCount );
    bool IsFullySubmergedBall( const PhysicsBodyRecord& bodyRecord,
                               const GameObjects::GameModelBodyStream& bodyStream,
                               int index );
    bool RefreshUnderwaterSubmersionForBall( const PhysicsWorldForces& worldForces,
                                             PhysicsBodyStore& bodyStore,
                                             const ColliderStore& colliderStore,
                                             int index );
    void LockUnderwaterSleeperIfReady( const PhysicsWorldForces& worldForces,
                                       PhysicsBodyStore& bodyStore,
                                       const ColliderStore& colliderStore,
                                       const GameObjects::GameModelBodyStream& bodyStream,
                                       int index );
    bool IsUnderwaterSleepLocked( PhysicsModelAccess& modelAccess,
                                  const GameObjects::GameModelBodyStream& bodyStream,
                                  int index );
    void MarkCollisionVisualContact( int index );
    void ApplyTornadoField( PhysicsModelAccess& modelAccess,
                            PhysicsBodyStore& bodyStore,
                            const ColliderStore& colliderStore,
                            const PhysicsWorldForces& worldForces,
                            float dt,
                            const Basics::EngineConfig& runtimeConfig,
                            Threading::WorkerPool& workerPool );
    void PropagateSleepSupport( const std::vector<PhysicsBodyRecord>& bodyRecords );
    void AppendPointJointSupportEdges( const PhysicsBodyStore& bodyStore, int modelCount );
    void ForgetPersistentContactCacheForBody( int bodyIndex );
    void WakeModel( PhysicsModelAccess& modelAccess,
                    const GameObjects::GameModelBodyStream& bodyStream,
                    PhysicsBodyStore* bodyStore,
                    const ColliderStore* colliderStore,
                    const PhysicsWorldForces* worldForces,
                    int index );
    void SeedModelAsleep( PhysicsModelAccess& modelAccess,
                          const GameObjects::GameModelBodyStream& bodyStream,
                          const PhysicsBodyStore* bodyStore,
                          int index );
    bool WakeDynamicBodyState( PhysicsModelAccess& modelAccess,
                               const GameObjects::GameModelBodyStream& bodyStream,
                               PhysicsBodyStore* bodyStore,
                               int index,
                               float dt,
                               bool applyForces,
                               const PhysicsWorldForces* worldForces = nullptr,
                               const ColliderStore* colliderStore = nullptr );
    void WakeSleepVisualIsland( PhysicsModelAccess& modelAccess,
                                const GameObjects::GameModelBodyStream& bodyStream,
                                PhysicsBodyStore* bodyStore,
                                int index,
                                float dt,
                                bool applyForces,
                                const PhysicsWorldForces* worldForces = nullptr,
                                const ColliderStore* colliderStore = nullptr );
    void WakePointJointIsland( PhysicsModelAccess& modelAccess,
                               const GameObjects::GameModelBodyStream& bodyStream,
                               PhysicsBodyStore* bodyStore,
                               int index,
                               float dt,
                               bool applyForces,
                               const PhysicsWorldForces* worldForces = nullptr,
                               const ColliderStore* colliderStore = nullptr );
    void WakeRestingContactIsland( PhysicsModelAccess& modelAccess,
                                   const GameObjects::GameModelBodyStream& bodyStream,
                                   PhysicsBodyStore* bodyStore,
                                   int index,
                                   float dt,
                                   bool applyForces,
                                   const PhysicsWorldForces* worldForces = nullptr,
                                   const ColliderStore* colliderStore = nullptr );
    bool IsPointJointPair( const PhysicsBodyStore& bodyStore, int bodyA, int bodyB ) const;
    void WakePointJointConnectedBodies( PhysicsModelAccess& modelAccess,
                                        PhysicsBodyStore& bodyStore,
                                        const ColliderStore& colliderStore,
                                        const PhysicsWorldForces& worldForces,
                                        float dt );

  public:
    PhysicsWorld();

    void ApplyRuntimeConfig( const Basics::EngineConfig& config );
    void Clear();
    void RunPhysics( PhysicsModelAccess& modelAccess,
                     PhysicsBodyStore& bodyStore,
                     const ColliderStore& colliderStore,
                     float fChangeInTime,
                     const Basics::EngineConfig& config,
                     const PhysicsWorldForces& worldForces,
                     Threading::WorkerPool& workerPool );
    // Callers with a refreshed body store should use the overload so wake and
    // seed decisions read physics-owned fixed/sleep state before compatibility
    // writeback.
    void WakeModel( PhysicsModelAccess& modelAccess, int index );
    void WakeModel( PhysicsModelAccess& modelAccess, PhysicsBodyStore& bodyStore, int index );
    void WakeModel( PhysicsModelAccess& modelAccess,
                    PhysicsBodyStore& bodyStore,
                    const ColliderStore& colliderStore,
                    const PhysicsWorldForces& worldForces,
                    int index );
    void SeedModelAsleep( PhysicsModelAccess& modelAccess, int index );
    void SeedModelAsleep( PhysicsModelAccess& modelAccess, const PhysicsBodyStore& bodyStore, int index );
    void SetPhysicsSleepEnabled( bool enabled );
    void BeginCollisionVisualFrame( int modelCount );
    void EndCollisionVisualFrame();
    void ClearPointJointConstraints();
    void AddPointJointConstraint( const PointJointConstraint& constraint );
    const std::vector<PointJointConstraint>& GetPointJointConstraints() const;
    void SetTornadoFieldConfig( const TornadoFieldConfig& config );
    const TornadoFieldConfig& GetTornadoFieldConfig() const;
    void SetTornadoSystemConfig( const TornadoSystemConfig& config );
    const TornadoSystemConfig& GetTornadoSystemConfig() const;
    float GetTornadoSystemElapsedSeconds() const;
    void RenderTornadoFieldVectors( const Math::Transformation::Matrix4& viewProj );
    void CaptureReplaySolverSnapshot( Basics::ReplaySolverWorldSnapshot& outSnapshot, int modelCount ) const;
    bool RestoreReplaySolverSnapshot( const Basics::ReplaySolverWorldSnapshot& snapshot, int modelCount );
    PhysicsDiagnosticsView GetDiagnosticsView() const;
    uint64_t CollectMemoryBytes() const;
    uint64_t CollectDebugAndBroadphaseMemoryBytes() const;
    const Math::CollisionDetection::SpatialGrid& GetSpatialGrid() const;
    const std::vector<int64_t>& GetCollisionCellKeys() const;
    const std::vector<uint8_t>& GetCollisionVisualContacts() const;
    const std::vector<uint8_t>& GetSleepStates() const;
    const std::vector<int>& GetSleepIslandVisualIds() const;
    const std::vector<uint8_t>& GetSleepSupportedStates() const;
    const std::vector<uint8_t>& GetSleepInhibitedStates() const;
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
    const std::vector<PhysicsWorld::PersistentContact>& persistentContacts;
    const PhysicsWorld::PersistentContactSolverStats& persistentContactSolverStats;
    const std::vector<int>& sleepIslandParent;
    const std::vector<uint8_t>& sleepSupportedThisFrame;
    const std::vector<uint8_t>& sleepInhibitedThisFrame;
    const std::vector<uint8_t>& sleepState;
    const std::vector<uint8_t>& sleepCounter;
    const std::vector<uint8_t>& sleepIslandEligible;
    const std::vector<uint8_t>& sleepIslandCanSleep;
    const Math::CollisionDetection::SpatialGrid& spatialGrid;
    const std::vector<std::pair<int, int>>& candidatePairs;
    const std::vector<int64_t>& collisionCellKeys;
    const std::vector<std::pair<int, int>>& sleepSupportEdges;
    const std::vector<int>& sleepIslandVisualId;
    const std::vector<PhysicsPipelineRecord>& physicsPipelineTrace;
    const std::vector<TerrainContactManifold>& terrainContactManifolds;
};

struct PersistentContactSolverContext
{
    std::vector<std::pair<int, int>>& candidatePairs;
    std::vector<uint8_t>& sleepState;
    std::vector<std::pair<int, int>>& sleepSupportEdges;
    std::vector<PhysicsWorld::PersistentContact>& persistentContacts;
    std::vector<PersistentContactCacheEntry>& persistentContactCache;
    PhysicsWorld::PersistentContactSolverStats& persistentContactSolverStats;
    std::vector<uint16_t>& persistentContactCounts;
    std::vector<uint16_t>& persistentRestingContactCounts;
    std::vector<SolverBodyState>& solverBodies;
    std::vector<PhysicsDebugContact>& physicsDebugContacts;
    std::vector<TerrainContactManifold>& terrainContactManifolds;
    std::array<uint8_t, MAX_GAME_MODELS>& terrainRestApplied;
    std::vector<uint8_t>& sleepSupportedThisFrame;
    PersistentContactSolverSideEffects& sideEffects;
    const GameObjects::GameModelBodyStream& bodyStream;
    std::vector<PhysicsBodyRecord>& bodyRecords;
    const std::vector<ColliderRecord>& colliderRecords;
    int bodyStoreCount = 0;
    int pipelineRecordCapacity = 0;
    const Basics::EngineConfig& config;
};

struct SleepSupportPropagationContext
{
    std::vector<uint8_t>& sleepState;
    std::vector<std::pair<int, int>>& sleepSupportEdges;
    std::vector<uint8_t>& sleepSupportedThisFrame;
};
} // namespace Physics
} // namespace SkullbonezCore
