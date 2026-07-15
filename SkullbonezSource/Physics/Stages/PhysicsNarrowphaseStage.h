/*
File: SkullbonezSource/Physics/Stages/PhysicsNarrowphaseStage.h
Purpose:
  Owns object/object CCD event scratch and collision-independent island dispatch.

Summary:
  PhysicsNarrowphaseStage processes broadphase pairs into bounded value events,
  builds disjoint pair islands when worker dispatch can help, and retains every
  island/event scratch array. PhysicsWorld commits those events in original pair
  order because diagnostics, visual state, and collision-cell keys still belong
  to other owners until their scheduled extraction tasks.

Glossary:
  Pair event: Value record describing diagnostics and visual/cell side effects.
  Narrowphase island: Connected group of candidate pairs that may mutate the
    same bodies and therefore must execute on one worker.
  Pair-order commit: Sequencer replay of events by original candidate index.

Invariants:
  - Serial processing commits each event immediately before the next pair.
  - Parallel workers write one event per pair slot; the sequencer commits only
    after all islands complete, in ascending original pair order.
  - All seven retained vectors are construction-reserved and cannot grow during
    steady gameplay beyond scene/candidate capacities.

Related:
  - SkullbonezSource/Physics/Stages/PhysicsNarrowphaseStage.cpp
  - SkullbonezSource/Physics/PhysicsWorld.cpp
  - Agentic/Reports/2026-07-15/physicsworld-ownership-map.md
*/
#pragma once

#include <cstddef>
#include <cstdint>
#include <span>
#include <utility>
#include <vector>

#include "../PersistentContactSolver.h"
#include "../PhysicsDebugData.h"

namespace SkullbonezCore
{
namespace Core
{
struct PhysicsExecutionConfig;
} // namespace Core

namespace Threading
{
class WorkerPool;
} // namespace Threading

namespace Physics
{
class ColliderStore;
class PhysicsBodyStore;
class PhysicsSleepController;
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
    // facade commits those slots later in original pair order.
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

struct ObjectNarrowphasePairStageContext
{
    // Lifetime: pair processing and bounded island dispatch borrow these values
    // only for the current synchronous narrowphase pass.
    PhysicsBodyStore& bodyStore;
    const ColliderStore& colliderStore;
    const PhysicsWorldForces& worldForces;
    std::span<PhysicsBodyRecord> bodyRecords;
    std::span<const ColliderRecord> colliderRecords;
    std::span<const std::pair<int, int>> candidatePairs;
    PhysicsSleepController& sleepController;
    std::span<const uint8_t> sleepState;
    std::span<float> timeRemaining;
    std::span<const uint8_t> underwaterSleepLocked;
    const std::vector<PersistentContactCacheEntry>& persistentContactCache;
    int modelCount = 0;
    float sleepLinearSq = 0.0f;
    float sleepAngularSq = 0.0f;
    float contactSkin = 0.0f;
    float contactEpsilon = 0.0f;
    float invCellSize = 0.0f;
    float dt = 0.0f;
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
    std::vector<ObjectNarrowphaseEvent> m_objectNarrowphaseEvents;
    std::vector<ObjectNarrowphaseIsland> m_objectNarrowphaseIslands;
    std::vector<int> m_objectNarrowphaseIslandPairIndices;
    std::vector<size_t> m_objectNarrowphaseIslandWriteOffsets;
    std::vector<int> m_objectNarrowphaseParent;
    std::vector<uint8_t> m_objectNarrowphaseRank;
    std::vector<int> m_objectNarrowphaseRootToIsland;

    struct ObjectNarrowphaseIslandStage
    {
        PhysicsNarrowphaseStage& stage;
        const ObjectNarrowphasePairStageContext& pairContext;

        void operator()( int islandIndex ) const;
    };

    static void RecordObjectNarrowphaseEvent( ObjectNarrowphaseEvent& event,
                                              ObjectNarrowphaseEventKind kind,
                                              const PhysicsPipelineRecord& record );
    static void EmitObjectCollisionTimeEvent( ObjectNarrowphaseEvent& event,
                                              int bodyA,
                                              int bodyB,
                                              float collisionTime,
                                              float availableTime );
    static void MarkObjectVisualEvent( ObjectNarrowphaseEvent& event, int bodyA, int bodyB );
    static void WriteObjectCollisionCellEvent( ObjectNarrowphaseEvent& event,
                                               std::span<const PhysicsBodyRecord> bodyRecords,
                                               int bodyA,
                                               int bodyB,
                                               float invCellSize );
    void ProcessObjectNarrowphaseIsland( const ObjectNarrowphasePairStageContext& context, int islandIndex );
    void BuildObjectNarrowphaseIslands( std::span<const std::pair<int, int>> candidatePairs,
                                        int candidatePairCount,
                                        int modelCount );
    static bool ObjectNarrowphaseIslandPrecedesByMinPairIndex( const ObjectNarrowphaseIsland& a,
                                                               const ObjectNarrowphaseIsland& b );

  public:
    PhysicsNarrowphaseStage();

    void Clear();
    void ProcessObjectNarrowphasePair( const ObjectNarrowphasePairStageContext& context,
                                       int pairIndex,
                                       ObjectNarrowphaseEvent& event );
    bool TryRunParallel( const ObjectNarrowphasePairStageContext& context,
                         int candidatePairCount,
                         int modelCount,
                         const Core::PhysicsExecutionConfig& execution,
                         Threading::WorkerPool& workerPool );
    std::span<const ObjectNarrowphaseEvent> GetEvents() const;
    uint64_t CollectDynamicMemoryBytes() const;
};
} // namespace Physics
} // namespace SkullbonezCore
