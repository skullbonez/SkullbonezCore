/*
File: SkullbonezSource/Physics/SpatialGrid.h
Purpose:
  Partitions space into broadphase cells so physics can test nearby objects cheaply.

Summary:
  Persistent current-position membership and a one-step swept overlay feed one
  fixed hash-bucket topology. Pair collection builds sorted per-body slices of
  eligible shared-bucket ordinals, omitting cells that cannot witness a pair, so
  the earliest eligible shared bucket owns filtering without retaining one bit

  for every possible body pair. A Debug-only legacy
  same-state oracle remains isolated until BD4 removes it.

Invariants:
  - Physics-visible behavior must remain deterministic; byte-exact baselines
    are the validation contract.
  - Inserted bounds stay finite and within MAX_WORLD_COORDINATE before any
    float-to-cell conversion.
  - One 8,192-row table owns every live persistent or current swept-overlay
    cell; the next unique cell is a Lane F failure because dropping it could
    hide a collision.
  - Candidate discovery may follow bucket/list order, but solver-visible output
    is canonical and uses fixed-capacity staging owned by this grid.
  - Exact cell coordinates remain `int` through hashing and membership.
    Visualization alone saturates them to signed 16-bit [-32,768, 32,767].
  - Pair-source stamps restrict this frame's work only; they never remove or
    mutate a sleeper's persistent membership.
  - A normalized pair is filtered only at its earliest shared active bucket
    that is eligible for the current pair-source mode; coordinate-hash aliases
    intentionally collapse to that bucket identity.
  - Every scene-sized store is reserved under SceneLoad, retained across cold
    clears, and never grows during fixed-step work.

Related:
  - SkullbonezSource/Physics/SpatialGrid.cpp
  - Agentic/Reference/physics-overview.md
  - Agentic/Reference/comment-style-guide.md
  - Agentic/Reports/2026-07-29/broadphase-canonical-order-guard-closure.md
  - Agentic/Reports/2026-08-02/broadphase-pair-dedup-cost-bd1-decision.md
  - Agentic/Reference/engine-glossary.md
*/
#pragma once


#include <vector>
#include <utility>
#include <cstdint>
#include <cstring>
#include <cmath>
#include <cassert>
#include <limits>
#include <span>
#include "../Core/Common.h"
#include "../Core/SceneCapacity.h"
#include "../Maths/Vector3.h"
#include "PhysicsBroadphaseDebugView.h"
#include "PhysicsStageCapacity.h"

namespace SkullbonezCore
{
namespace Physics
{
class ColliderStore;
class PhysicsBodyStore;
} // namespace Physics
namespace Math
{
namespace CollisionDetection
{

/* -- Spatial Grid
------------------------------------------------------------------------------------------------------------------------------------------

    Zero-allocation uniform spatial grid for broadphase collision detection. Persistent body membership uses a
    fixed hash-chain table, per-body cell ranges, intrusive back-links, and reusable bucket/entry pools. Swept CCD
    occupancy uses a separate per-frame stamped overlay. Sorted per-body eligible shared-bucket memberships assign each pair
    to its earliest eligible shared bucket; fixed radix staging then emits ascending normalized pairs independent of
    discovery order. The Debug legacy same-state oracle is separate from production filtering. Unchanged integer ranges
    touch no cells. SceneLoad reservation establishes retained backing; fixed-step work performs no heap allocation.

    Layman version:
      Instead of asking every object about every other object, the world is cut
      into invisible boxes. Objects only become candidate collision pairs when
      they share one of those invisible boxes. This is the cheap first filter;
      it never decides the final collision response.
-------------------------------------------------------------------------------------------------------------------------------------------------------------*/
class SpatialGrid
{

  public:
    struct MaintenanceStats
    {
        int insertedBodies = 0;
        int movedBodies = 0;
        int unchangedBodies = 0;
        int removedBodies = 0;
        int persistentCellsAdded = 0;
        int persistentCellsRemoved = 0;
        int sweptOverlayCellsAdded = 0;
    };

  private:

    // --- Capacity derivation ---
    // Static objects of radius R in a grid of cell size C span at most
    // ceil(2R/C + 1) cells per axis. PhysicsWorld chooses C from the largest
    // current broadphase radius, capped by the configured legacy cell size, so
    // normal scene bodies are expected to use at most 2x2x2 = 8 cells.
    //
    // Fast dynamic bodies can insert their swept AABB for CCD pairing. That is
    // intentionally a limited escape hatch for bullets and other rare high-speed
    // movers, not a promise that every body can sweep across the whole world in
    // one tick. Large projectile clouds should use a dedicated ray/query path.
    // One power-of-two table keeps lookup, storage, iteration, and exhaustion
    // on the same deterministic path at both ordinary and scale-scene sizes.
    static constexpr int TABLE_SIZE = static_cast<int>( Physics::PHYSICS_SPATIAL_GRID_BUCKET_COUNT );
    static constexpr int TABLE_MASK = TABLE_SIZE - 1;
    static_assert( ( TABLE_SIZE & TABLE_MASK ) == 0, "SpatialGrid table size must remain a power of two" );
    static constexpr int PERSISTENT_ENTRIES_PER_BODY = 8;

    // Why: ordinary bodies consume at most eight rows, but the accepted
    // shoreline-lever and low-poly stress scenes combine oversized bodies with
    // dense ordinary occupancy. A fixed 1,024-row spill covers the measured
    // 1,340-row stress layout with deterministic motion margin, without
    // restoring the retired 4,096-row blanket or permitting steady-step growth.
    static constexpr int PERSISTENT_ENTRY_SPILL_ROWS = 1024;
    static constexpr int MAX_STATIC_CELL_ENTRIES = SkullbonezCore::Scene::Capacity::MAX_SCENE_OBJECTS *
                                                   PERSISTENT_ENTRIES_PER_BODY;
    static constexpr int MAX_SWEPT_CELL_ENTRIES = 4096;
    static constexpr int MAX_CELL_ENTRIES = MAX_STATIC_CELL_ENTRIES + MAX_SWEPT_CELL_ENTRIES + 4;
    static constexpr int MAX_SWEPT_AABB_CELLS = MAX_SWEPT_CELL_ENTRIES / 2;
    static constexpr int MAX_SWEPT_TRAVERSED_CELLS = MAX_SWEPT_CELL_ENTRIES;
    static constexpr int MAX_CANDIDATE_PAIRS = SkullbonezCore::Scene::Capacity::MAX_SCENE_OBJECTS * 4;
    using PairMembershipOrdinal = uint16_t;
    static constexpr int MAX_PAIR_MEMBERSHIP_ROWS = MAX_CELL_ENTRIES + MAX_SWEPT_CELL_ENTRIES;
    static_assert( TABLE_SIZE - 1 <= ( std::numeric_limits<PairMembershipOrdinal>::max )(),
                   "SpatialGrid active-bucket ordinals no longer fit the membership row type." );
#if defined( _DEBUG )
    static constexpr int64_t MAX_PAIR_IDENTITIES = static_cast<int64_t>( SkullbonezCore::Scene::Capacity::MAX_SCENE_OBJECTS ) *
                                                   ( SkullbonezCore::Scene::Capacity::MAX_SCENE_OBJECTS - 1 ) / 2;
    static_assert( MAX_PAIR_IDENTITIES - 1 <= ( std::numeric_limits<int>::max )(),
                   "MAX_SCENE_OBJECTS exceeds the signed candidate-pair identity range." );
    static constexpr int PAIR_WORDS = static_cast<int>( ( MAX_PAIR_IDENTITIES + 63 ) / 64 );
#endif

    struct CellRange
    {
        int minX, minY, minZ;
        int maxX, maxY, maxZ;
    };

    struct Entry
    {
        int objectIndex;               // Scene/model slot stored in one occupied grid cell.
        int bucketIndex;               // Owning buckets[] row for direct unlink.
        int nextInBucket;              // Next persistent member of the same cell.
        int previousInBucket;          // Previous member; -1 means bucket head.
        int nextForObject;             // Next cell entry owned by this body.
        int previousForObject;         // Previous entry; -1 means body head.
        int nextFree;                  // Reusable-slot chain when this entry is inactive.
        int ix, iy, iz;                // Exact cell coordinate; hash collisions share a bucket conservatively.
    };

    struct SweptOverlayEntry
    {
        int objectIndex;
        int next;
        int ix, iy, iz;
    };

    struct Bucket
    {
        int64_t key;                   // Existing hashed cell identity; collisions remain conservative false positives.
        bool occupied;                 // Live hash-chain row; false rows belong to the bucket free list.
        int nextHash;                  // Next row sharing this table home slot.
        int previousHash;              // Back-link for O(1) removal from the hash chain.
        int nextFree;                  // Reusable bucket-slot chain while unoccupied.
        int head;                      // Persistent entries[] chain head.
        int count;                     // Persistent object count.
        uint32_t pairSourceGeneration; // Current-frame stamp when an awake body reaches this cell.
        uint32_t overlayGeneration;    // Current-frame stamp for swept-only occupancy.
        int overlayHead;               // SweptOverlayEntry chain head for overlayGeneration.
        int overlayCount;              // Current swept-only object count.
        int activeIndex;               // Back-link into activeBuckets[] for swap removal.
        int16_t ix, iy, iz;            // Cell grid coordinates (stored for visualization).
    };

    struct BodyMembership
    {
        CellRange range {};
        int entryHead = -1;
        bool active = false;
    };

    struct CandidatePairNode
    {
        int maxIndex;                  // Larger normalized body index for one accepted pair.
        int next;                      // Next node with the same smaller body index; -1 ends the list.
    };

    float cellSize;
    float inverseCellSize;
    uint32_t overlayGeneration;
    uint32_t pairSourceGeneration;
    int freeBucketHead;
    int freeEntryHead;
    int persistentEntryCount;
    int objectCount;
    int activeBucketCount;
    int overlayEntryCount;
    int overlayActiveBucketCount;

    Bucket buckets[TABLE_SIZE];
    int bucketHashHeads[TABLE_SIZE];
    int activeBuckets[TABLE_SIZE];
    int overlayActiveBuckets[TABLE_SIZE];
    Physics::PhysicsFixedList<Entry, MAX_CELL_ENTRIES>
        entries { "SpatialGrid.entries", Physics::PhysicsCapacityReason::SpatialGridPersistentEntries };
    Physics::PhysicsFixedList<SweptOverlayEntry, MAX_SWEPT_CELL_ENTRIES>
        overlayEntries { "SpatialGrid.overlayEntries", Physics::PhysicsCapacityReason::SpatialGridSweptOverlayEntries };
    Physics::PhysicsFixedList<BodyMembership, SkullbonezCore::Scene::Capacity::MAX_SCENE_OBJECTS>
        bodyMemberships { "SpatialGrid.bodyMemberships", Physics::PhysicsCapacityReason::SpatialGridBodyMemberships };
    Physics::PhysicsFixedList<PairMembershipOrdinal, MAX_PAIR_MEMBERSHIP_ROWS>
        pairMembershipOrdinals { "SpatialGrid.pairMembershipOrdinals",
                                 Physics::PhysicsCapacityReason::SpatialGridPairMembershipOrdinals };
    Physics::PhysicsFixedList<uint32_t, SkullbonezCore::Scene::Capacity::MAX_SCENE_OBJECTS + 1>
        pairMembershipOffsets { "SpatialGrid.pairMembershipOffsets",
                                Physics::PhysicsCapacityReason::SpatialGridPairMembershipOffsets };
    Physics::PhysicsFixedList<uint32_t, SkullbonezCore::Scene::Capacity::MAX_SCENE_OBJECTS>
        pairMembershipCounts { "SpatialGrid.pairMembershipCounts",
                               Physics::PhysicsCapacityReason::SpatialGridPairMembershipCounts };
#if defined( _DEBUG )
    Physics::PhysicsFixedList<uint64_t, PAIR_WORDS> pairSeen { "SpatialGrid.pairSeen",
                                                               Physics::PhysicsCapacityReason::SpatialGridPairDedupWords };
    std::size_t pairMembershipLogicalCapacityForTest = ( std::numeric_limits<std::size_t>::max )();
#endif

    // Canonical pair staging is scene-reserved storage owned by the grid. Cell
    // traversal may discover pairs in any bucket/list order, but emission is
    // always sorted by normalized body identity before narrowphase sees it.
    Physics::PhysicsFixedList<int, SkullbonezCore::Scene::Capacity::MAX_SCENE_OBJECTS>
        candidatePairHeads { "SpatialGrid.candidatePairHeads",
                             Physics::PhysicsCapacityReason::SpatialGridCandidatePairHeads };
    Physics::PhysicsFixedList<CandidatePairNode, MAX_CANDIDATE_PAIRS>
        candidatePairNodes { "SpatialGrid.candidatePairNodes",
                             Physics::PhysicsCapacityReason::SpatialGridCandidatePairNodes };
    Physics::PhysicsFixedList<int, MAX_CANDIDATE_PAIRS>
        candidatePairSortKeys { "SpatialGrid.candidatePairSortKeys",
                                Physics::PhysicsCapacityReason::SpatialGridCandidatePairSortKeys };
    Physics::PhysicsFixedList<int, MAX_CANDIDATE_PAIRS>
        candidatePairSortScratch { "SpatialGrid.candidatePairSortScratch",
                                   Physics::PhysicsCapacityReason::SpatialGridCandidatePairSortScratch };
    uint32_t cellObjectGeneration;
    Physics::PhysicsFixedList<uint32_t, SkullbonezCore::Scene::Capacity::MAX_SCENE_OBJECTS>
        cellObjectSeen { "SpatialGrid.cellObjectSeen", Physics::PhysicsCapacityReason::SpatialGridCellObjectSeen };
    MaintenanceStats maintenanceStats;

    int FindBucket( int64_t key ) const;
    int FindOrCreateBucket( int64_t key, int16_t cx, int16_t cy, int16_t cz );
    void RetireBucketIfEmpty( int bucketIndex );
    int AllocatePersistentEntry();
    void ReleasePersistentEntry( int entryIndex );
    void InsertPersistentCell( int index, int ix, int iy, int iz );
    void RemovePersistentCell( int index, int ix, int iy, int iz );
    void RemovePersistentEntry( int entryIndex );
    void RemoveBody( int index );
    void InsertRangeDifference( int index, const CellRange& range, const CellRange* excludedRange );
    void RemoveRangeDifference( int index, const CellRange& range, const CellRange* retainedRange );
    void InsertOverlayCell( int index, int ix, int iy, int iz );
    void InsertOverlayBounds( int index, const Vector::Vector3& minBounds, const Vector::Vector3& maxBounds );
    CellRange RangeForBounds( int index, const Vector::Vector3& minBounds, const Vector::Vector3& maxBounds,
                              int capacity ) const;
    void MaintainBounds( int index, const Vector::Vector3& minBounds, const Vector::Vector3& maxBounds );
    void ResetSweptOverlay();
    int CollectBucketObjects( const Bucket& bucket, int* outIndices, int capacity, std::size_t* observedRawRows = nullptr,
                              int expectedBucketIndex = -1 );
    void BuildPairMembershipIndex();
    void AppendUniquePairMembershipOrdinal( int bodyIndex, int activeIndex );
    int CollectPairBucketObjects( const Bucket& bucket, int bucketIndex, int activeIndex, int* outIndices, int capacity,
                                  std::size_t& observedRawRows );
    void RequirePairMembershipRowsObserved( std::size_t observedRows ) const;
    bool IsEarliestEligibleSharedBucket( int a, int b, int currentActiveIndex, bool restrictToPairSourceCells ) const;
    bool MarkCandidatePairFirstSeen( int a, int b, int currentActiveIndex, bool restrictToPairSourceCells );
    bool FilterCandidatePairAfterFirstSeen( int a, int b, const Physics::PhysicsBodyStore& bodyStore,
                                            const Physics::ColliderStore& colliderStore, std::span<const uint8_t> sleepState,
                                            float dt, float contactSkin,
                                            Physics::PhysicsCandidatePairList* sleepPrunedPairs );
#if defined( _DEBUG )
    void ObservePairBucketRows( const Bucket& bucket, int bucketIndex, int activeIndex, std::size_t& observedRawRows );
    void ResetDensePairCrossCheck();
    bool MarkDensePairFirstSeen( int a, int b );
#endif
    void GetFilteredCandidatePairsImpl( Physics::PhysicsCandidatePairList& outPairs,
                                        const Physics::PhysicsBodyStore& bodyStore,
                                        const Physics::ColliderStore& colliderStore, std::span<const uint8_t> sleepState,
                                        float dt, float contactSkin, Physics::PhysicsCandidatePairList* sleepPrunedPairs,
                                        bool restrictToPairSourceCells );

  public:
    static constexpr int MAX_BUCKETS = TABLE_SIZE;

    // PhysicsWorld already clamps authored settings to this lower bound. Keep
    // the grid's own constructor/setter equally strict so direct users cannot
    // create cell coordinates outside the integer representation envelope.
    static constexpr float MIN_CELL_SIZE = 0.5f;

    // Broadphase owner limit: authored/runtime physics state outside this cube
    // is corrupt. The generous bound also keeps ordinary cell conversion far
    // from integer limits for supported broadphase cell sizes.
    static constexpr float MAX_WORLD_COORDINATE = 100000.0f;

    // Hazard: exact coordinates can reach +/-200,000 at the minimum cell size.
    // Bucket and PhysicsBroadphaseActiveCell retain only a saturated
    // visualization projection; collision identity continues to use the
    // full-width Entry coordinates and hash key. Widen both visualization
    // structs before removing saturation.
    static constexpr int MIN_VISUALIZATION_CELL_COORDINATE = ( std::numeric_limits<int16_t>::min )();
    static constexpr int MAX_VISUALIZATION_CELL_COORDINATE = ( std::numeric_limits<int16_t>::max )();
    static constexpr int MAX_ABSOLUTE_CELL_COORDINATE = static_cast<int>( MAX_WORLD_COORDINATE / MIN_CELL_SIZE );
    static_assert( MAX_ABSOLUTE_CELL_COORDINATE == 200000,
                   "SpatialGrid world/cell limits changed: review exact and visualization coordinate storage." );
    static_assert( MAX_ABSOLUTE_CELL_COORDINATE <= ( std::numeric_limits<int>::max )() - 1024,
                   "SpatialGrid exact cell coordinates exceed the guarded signed-int conversion range." );

    SpatialGrid( float fCellSize );

    // SceneLoad-only sizing for every scene-derived store. The compile-time
    // ceilings remain larger so future evidence can change a runtime formula
    // without changing supported scene limits.
    void ReserveSceneCapacity( std::size_t bodyCapacity );

    // Cold reset for scene load, replay restore, and cell-size changes.
    void Clear();

    // Begins one fixed-step maintenance pass. Persistent memberships remain;
    // removed dense rows and the prior tick's velocity-dependent overlay do not.
    void BeginFrame( int currentObjectCount );

    // A changed cell size invalidates every cached integer range and performs a
    // cold clear. Reapplying the same value is intentionally maintenance-free.
    void SetCellSize( float fCellSize );
    void Insert( int index, const Vector::Vector3& position, float radius );

    // Maintains the body's ordinary current-position cells, then adds only the
    // velocity-dependent sweep to the current frame's stamped overlay.
    void InsertSwept( int index, const Vector::Vector3& position, const Vector::Vector3& displacement, float radius );

    // Marks every persistent cell currently reachable from one awake body as a
    // candidate source for this frame. Swept insertions stamp overlay cells as
    // they are created.
    void MarkPairSourceCells( int index );

    // Emits deduplicated cell-sharing pairs in ascending normalized body-index
    // order. The unfiltered overload exposes pure membership to focused tools;
    // filtered overloads require concrete stores and step scalars. Debug may
    // additionally retain sleep-only geometric admissions as bounded evidence.
    void GetCandidatePairs( std::vector<std::pair<int, int>>& outPairs, bool restrictToPairSourceCells = false );
    void GetFilteredCandidatePairs( Physics::PhysicsCandidatePairList& outPairs, const Physics::PhysicsBodyStore& bodyStore,
                                    const Physics::ColliderStore& colliderStore, std::span<const uint8_t> sleepState,
                                    float dt, float contactSkin, Physics::PhysicsCandidatePairList& sleepPrunedPairs,
                                    bool restrictToPairSourceCells );
    void GetFilteredCandidatePairs( Physics::PhysicsCandidatePairList& outPairs, const Physics::PhysicsBodyStore& bodyStore,
                                    const Physics::ColliderStore& colliderStore, std::span<const uint8_t> sleepState,
                                    float dt, float contactSkin, bool restrictToPairSourceCells );
#if defined( _DEBUG )

    // P1 transition oracle only: emits the pre-transition bucket-history order
    // from the same grid state so Debug runs can compare work membership without
    // evolving a second simulation.
    void GetFilteredCandidatePairsLegacyForOracle( Physics::PhysicsCandidatePairList& outPairs,
                                                   const Physics::PhysicsBodyStore& bodyStore,
                                                   const Physics::ColliderStore& colliderStore,
                                                   std::span<const uint8_t> sleepState, float dt, float contactSkin );

    // Debug child-probe seam: valid grids derive enough ordinal capacity from
    // their source stores, so fatal diagnostics need a planted logical ceiling.
    // Keeping this inline avoids manufacturing a production-reachability row.
    void SetPairMembershipLogicalCapacityForTest( std::size_t capacity )
    {
        pairMembershipLogicalCapacityForTest = capacity;
    }
    uint32_t GetPairMembershipUniqueCountForTest( int bodyIndex ) const
    {
        assert( bodyIndex >= 0 && bodyIndex < objectCount );
        return pairMembershipCounts[bodyIndex];
    }
#endif
    float GetCellSize() const
    {
        return cellSize;
    }
    int GetActiveCellCount() const
    {
        return activeBucketCount;
    }
    const MaintenanceStats& GetMaintenanceStats() const
    {
        return maintenanceStats;
    }
    std::size_t GetPersistentEntryCapacity() const
    {
        return entries.capacity();
    }
    std::size_t GetPersistentEntryHighWater() const
    {
        return entries.high_water();
    }
    std::size_t GetBodyMembershipCapacity() const
    {
        return bodyMemberships.capacity();
    }
    std::size_t GetPairMembershipOrdinalCapacity() const
    {
        return pairMembershipOrdinals.capacity();
    }
    std::size_t GetPairMembershipOrdinalHighWater() const
    {
        return pairMembershipOrdinals.high_water();
    }
    std::size_t GetPairMembershipOffsetCapacity() const
    {
        return pairMembershipOffsets.capacity();
    }
    std::size_t GetPairMembershipCountCapacity() const
    {
        return pairMembershipCounts.capacity();
    }
#if defined( _DEBUG )
    std::size_t GetPairDedupWordCapacity() const
    {
        return pairSeen.capacity();
    }
    std::size_t GetPairDedupWordHighWater() const
    {
        return pairSeen.high_water();
    }
#endif
    std::size_t GetCandidatePairHeadCapacity() const
    {
        return candidatePairHeads.capacity();
    }
    std::size_t GetCandidatePairNodeCapacity() const
    {
        return candidatePairNodes.capacity();
    }
    std::size_t GetCandidatePairSortKeyCapacity() const
    {
        return candidatePairSortKeys.capacity();
    }
    std::size_t GetCandidatePairSortScratchCapacity() const
    {
        return candidatePairSortScratch.capacity();
    }
    std::size_t GetCellObjectSeenCapacity() const
    {
        return cellObjectSeen.capacity();
    }
    std::size_t GetSweptOverlayEntryCapacity() const
    {
        return overlayEntries.capacity();
    }
    std::size_t GetSweptOverlayEntryHighWater() const
    {
        return overlayEntries.high_water();
    }
    uint64_t CollectDynamicMemoryBytes() const;
    void GetActiveCells( Physics::PhysicsBroadphaseActiveCell* outCells, int maxCells ) const;
};
} // namespace CollisionDetection
} // namespace Math
} // namespace SkullbonezCore
