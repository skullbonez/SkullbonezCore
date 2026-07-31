/*
File: SkullbonezSource/Physics/Stages/PhysicsNarrowphaseStage.h
Purpose:
  Owns object/object CCD event scratch and collision-independent island dispatch.

Summary:
  PhysicsNarrowphaseStage processes broadphase pairs into bounded value events,
  builds disjoint pair islands when worker dispatch can help, and retains every
  island/event scratch array. PhysicsWorld commits typed events in original pair
  order because sleep, diagnostics, and broadphase each own the affected state.

Glossary:
  Pair event: Value record describing diagnostics and visual/cell side effects.
  Narrowphase island: Connected group of candidate pairs that may mutate the
    same bodies and therefore must execute on one worker.
  Pair-order commit: Sequencer replay of events by original candidate index.

Invariants:
  - Serial processing commits each event immediately before the next pair.
  - Parallel workers write one event per pair slot; the sequencer commits only
    after all islands complete, in ascending original pair order.
  - All seven retained lists commit scene/candidate capacities before play and
    fail rather than grow during steady gameplay.
  - Worker callables are stack-scoped; WorkerPool completes every borrowed
    island/store reference before TryRunParallel returns.

Related:
  - SkullbonezSource/Physics/Stages/PhysicsNarrowphaseStage.cpp
  - SkullbonezSource/Physics/Stages/PhysicsNarrowphaseStage.Execution.cpp
  - SkullbonezSource/Physics/PhysicsWorld.cpp
  - Agentic/Reports/2026-07-15/physicsworld-ownership-map.md
  - Agentic/Reference/engine-glossary.md
*/
#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <utility>

#include "../PersistentContactSolver.h"
#include "../BuoyancySystem.h"
#include "../PhysicsBodyStore.h"
#include "../PhysicsDebugData.h"
#include "../PhysicsRuntimeSettings.h"
#include "../PhysicsStageCapacity.h"
#include "PhysicsSleepController.h"

namespace SkullbonezCore
{
namespace Core
{
class Profiler;
} // namespace Core

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
struct PhysicsWorldForces;

enum class ObjectNarrowphaseEventKind : uint8_t
{
    None,
    SweptObjectHit,
    SweptObjectMiss,
    WakeDecision
};

struct ObjectNarrowphaseEvent
{

    // Invariant: worker passes fill one event per candidate-pair slot; the
    // PhysicsWorld sequencer commits those slots later in original pair order.
    ObjectNarrowphaseEventKind kind = ObjectNarrowphaseEventKind::None;
    std::optional<PhysicsPipelineRecord> pipelineRecord;
    int collisionTimeBodyA = -1;
    int collisionTimeBodyB = -1;
    float collisionTime = 0.0f;
    float availableTime = 0.0f;
    int visualBodyA = -1;
    int visualBodyB = -1;
    int64_t collisionCellKey = 0;

    // Invariant: hasPipelineEvent preserves the canonical count in both modes.
    // The optional is engaged only by the compile-time full-record lane, so a
    // count-only worker never constructs a diagnostic payload.
    uint8_t hasPipelineEvent = 0;
    uint8_t emitCollisionTime = 0;
    uint8_t markVisualContact = 0;
    uint8_t hasCollisionCellKey = 0;
};

struct ObjectNarrowphaseStepPolicy
{

    // Value-only per-step policy: no owner reach-back or borrowed storage may
    // be added here.
    float sleepLinearSq = 0.0f;
    float sleepAngularSq = 0.0f;
    float contactSkin = 0.0f;
    float contactEpsilon = 0.0f;
    float invCellSize = 0.0f;
    float dt = 0.0f;

    // Value-only selector copied into worker callables. Island/serial owners
    // branch once before their pair loops and invoke a compile-time lane.
    bool retainPipelineRecords = true;
    bool parallel = false;
    bool parallelNarrowphase = false;
};

struct ObjectNarrowphaseIsland
{
    int minPairIndex = 0;
    size_t firstPairOffset = 0;
    size_t pairCount = 0;
};

class PhysicsNarrowphaseStage
{
  private:
    PhysicsFixedList<ObjectNarrowphaseEvent, PHYSICS_MAX_CANDIDATE_PAIRS>
        m_objectNarrowphaseEvents { "PhysicsNarrowphaseStage.events", PhysicsCapacityReason::CandidatePairs };
    PhysicsFixedList<ObjectNarrowphaseIsland, PHYSICS_MAX_BODY_ROWS>
        m_objectNarrowphaseIslands { "PhysicsNarrowphaseStage.islands", PhysicsCapacityReason::SceneBodies };
    PhysicsFixedList<int, PHYSICS_MAX_CANDIDATE_PAIRS>
        m_objectNarrowphaseIslandPairIndices { "PhysicsNarrowphaseStage.islandPairIndices",
                                               PhysicsCapacityReason::CandidatePairs };
    PhysicsFixedList<size_t, PHYSICS_MAX_BODY_ROWS>
        m_objectNarrowphaseIslandWriteOffsets { "PhysicsNarrowphaseStage.islandWriteOffsets",
                                                PhysicsCapacityReason::SceneBodies };
    PhysicsFixedList<int, PHYSICS_MAX_BODY_ROWS> m_objectNarrowphaseParent { "PhysicsNarrowphaseStage.parent",
                                                                             PhysicsCapacityReason::SceneBodies };
    PhysicsFixedList<uint8_t, PHYSICS_MAX_BODY_ROWS> m_objectNarrowphaseRank { "PhysicsNarrowphaseStage.rank",
                                                                               PhysicsCapacityReason::SceneBodies };
    PhysicsFixedList<int, PHYSICS_MAX_BODY_ROWS> m_objectNarrowphaseRootToIsland { "PhysicsNarrowphaseStage.rootToIsland",
                                                                                   PhysicsCapacityReason::SceneBodies };

    struct ObjectNarrowphaseIslandStage
    {

        // Lifetime: WorkerPool invokes this concrete callable synchronously;
        // these direct borrows expire before TryRunParallel returns.
        PhysicsNarrowphaseStage& stage;
        PhysicsBodyStore& bodyStore;
        const ColliderStore& colliderStore;
        PhysicsTerrainView terrain;
        std::span<BuoyancyBodyFacts> buoyancyFacts;
        std::span<const std::pair<int, int>> candidatePairs;
        PhysicsNarrowphaseWakeAccess wakeAccess;
        std::span<float> timeRemaining;
        std::span<const PersistentContactCacheEntry> persistentContactCache;
        ObjectNarrowphaseStepPolicy policy;
        Core::Profiler* profiler;

        void operator()( int islandIndex ) const;
    };

    static void ObserveObjectNarrowphaseEvent( ObjectNarrowphaseEvent& event, ObjectNarrowphaseEventKind kind );
    static void RecordObjectNarrowphaseEvent( ObjectNarrowphaseEvent& event, ObjectNarrowphaseEventKind kind,
                                              const PhysicsPipelineRecord& record );
    static void EmitObjectCollisionTimeEvent( ObjectNarrowphaseEvent& event, int bodyA, int bodyB, float collisionTime,
                                              float availableTime );
    static void MarkObjectVisualEvent( ObjectNarrowphaseEvent& event, int bodyA, int bodyB );
    static void WriteObjectCollisionCellEvent( ObjectNarrowphaseEvent& event, const PhysicsBodyHotFieldsConstView& hotFields,
                                               int bodyA, int bodyB, float invCellSize );
    template <bool RetainPipelineRecords>
    void ProcessObjectNarrowphaseIsland( PhysicsBodyStore& bodyStore, const ColliderStore& colliderStore,
                                         PhysicsTerrainView terrain, std::span<BuoyancyBodyFacts> buoyancyFacts,
                                         std::span<const std::pair<int, int>> candidatePairs,
                                         PhysicsNarrowphaseWakeAccess wakeAccess, std::span<float> timeRemaining,
                                         std::span<const PersistentContactCacheEntry> persistentContactCache,
                                         const ObjectNarrowphaseStepPolicy& policy, Core::Profiler* profiler,
                                         int islandIndex );
    void BuildObjectNarrowphaseIslands( Core::Profiler* profiler, std::span<const std::pair<int, int>> candidatePairs,
                                        int candidatePairCount, int modelCount );
    static bool ObjectNarrowphaseIslandPrecedesByMinPairIndex( const ObjectNarrowphaseIsland& a,
                                                               const ObjectNarrowphaseIsland& b );

  public:
    PhysicsNarrowphaseStage();

    void Clear();
    void ReserveSceneCapacity( std::size_t bodyCapacity );
    template <bool RetainPipelineRecords>
    void ProcessObjectNarrowphasePair( PhysicsBodyStore& bodyStore, const ColliderStore& colliderStore,
                                       PhysicsTerrainView terrain, std::span<BuoyancyBodyFacts> buoyancyFacts,
                                       std::span<const std::pair<int, int>> candidatePairs,
                                       PhysicsNarrowphaseWakeAccess wakeAccess, std::span<float> timeRemaining,
                                       std::span<const PersistentContactCacheEntry> persistentContactCache,
                                       const ObjectNarrowphaseStepPolicy& policy, Core::Profiler* profiler, int pairIndex,
                                       ObjectNarrowphaseEvent& event );
    bool TryRunParallel( PhysicsBodyStore& bodyStore, const ColliderStore& colliderStore, PhysicsTerrainView terrain,
                         std::span<BuoyancyBodyFacts> buoyancyFacts, std::span<const std::pair<int, int>> candidatePairs,
                         PhysicsNarrowphaseWakeAccess wakeAccess, std::span<float> timeRemaining,
                         std::span<const PersistentContactCacheEntry> persistentContactCache,
                         const ObjectNarrowphaseStepPolicy& policy, Core::Profiler* profiler,
                         Threading::WorkerPool& workerPool );
    std::span<const ObjectNarrowphaseEvent> GetEvents() const;
    uint64_t CollectDynamicMemoryBytes() const;
};
} // namespace Physics
} // namespace SkullbonezCore
