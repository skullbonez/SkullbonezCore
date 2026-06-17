/*
File: SkullbonezSource/SkullbonezSpatialGrid.h
Purpose:
  Partitions space into broadphase cells so physics can test nearby objects cheaply.

Mental model:
  Physics is deterministic fixed-step state update. Units, contact ownership,
  solver stages, sleep policy, and baseline-sensitive behavior are the key
  reading anchors.

Glossary:
  CCD (Continuous Collision Detection): Swept test that asks whether moving
  bodies collide during a tick.
  AABB (Axis-Aligned Bounding Box): Box aligned to world axes, often used for
  cheap broadphase overlap tests.
  Broadphase: Cheap collision pass that finds object pairs worth testing more
  precisely.
  Narrowphase: Precise collision pass that computes contact points, normals,
  and penetration.
  Manifold: Set of contact points and normals describing one colliding pair.

Invariants:
  - Physics-visible behavior must remain deterministic; byte-exact baselines
  are the validation contract.

Related:
  - SkullbonezSource/SkullbonezSpatialGrid.cpp
  - Agentic/Reference/physics-overview.md
  - Agentic/Reference/comment-style-guide.md
*/
#pragma once


#include <vector>
#include <utility>
#include <cstdint>
#include <cstring>
#include <cmath>
#include <cassert>
#include "SkullbonezCommon.h"
#include "SkullbonezVector3.h"

namespace SkullbonezCore
{
namespace Math
{
namespace CollisionDetection
{
/* -- Spatial Grid ------------------------------------------------------------------------------------------------------------------------------------------

    Zero-allocation uniform spatial grid for broadphase collision detection.  Uses open-addressing hash table with
    generation stamping (no per-frame clearing) and a flat index pool with linked lists per cell.  Pair deduplication
    via triangular bit array.  Complexity: O(n + k) where n = objects and k = candidate pairs.
    No heap allocations after construction.

    Layman version:
      Instead of asking every object about every other object, the world is cut
      into invisible boxes. Objects only become candidate collision pairs when
      they share one of those invisible boxes. This is the cheap first filter;
      it never decides the final collision response.
-------------------------------------------------------------------------------------------------------------------------------------------------------------*/
class SpatialGrid
{

  private:
    // --- Capacity derivation ---
    // Static objects of radius R in a grid of cell size C span at most
    // ceil(2R/C + 1) cells per axis. With the default 24-unit cells, normal
    // scene bodies are expected to use at most 2x2x2 = 8 cells.
    //
    // Fast dynamic bodies can insert their swept AABB for CCD pairing. That is
    // intentionally a limited escape hatch for bullets and other rare high-speed
    // movers, not a promise that every body can sweep across the whole world in
    // one tick. Large projectile clouds should use a dedicated ray/query path.
    static constexpr int TABLE_SIZE = 4096;
    static constexpr int TABLE_MASK = TABLE_SIZE - 1;
    static constexpr int MAX_STATIC_CELL_ENTRIES = MAX_GAME_MODELS * 8;
    static constexpr int MAX_SWEPT_CELL_ENTRIES = 4096;
    static constexpr int MAX_CELL_ENTRIES = MAX_STATIC_CELL_ENTRIES + MAX_SWEPT_CELL_ENTRIES + 4;
    static constexpr int MAX_SWEPT_AABB_CELLS = MAX_SWEPT_CELL_ENTRIES / 2;
    static constexpr int MAX_SWEPT_TRAVERSED_CELLS = MAX_SWEPT_CELL_ENTRIES;
    static constexpr int PAIR_WORDS = ( MAX_GAME_MODELS * ( MAX_GAME_MODELS - 1 ) / 2 + 63 ) / 64;

    struct Entry
    {
        int objectIndex; // GameModel index stored in one occupied grid cell.
        int next;        // Linked-list index into entries[], -1 = end of list.
    };

    struct Bucket
    {
        int64_t key;         // Packed/hashable grid coordinate identity.
        uint32_t generation; // Current frame stamp; old stamps behave as empty.
        int head;            // Linked-list head in entries[], -1 = empty.
        int count;           // Number of object entries in this cell.
        int16_t ix, iy, iz;  // Cell grid coordinates (stored for visualization)
    };

    float cellSize;
    float inverseCellSize;
    uint32_t generation;
    int entryPoolUsed;
    int objectCount;
    int activeBucketCount;

    Bucket buckets[TABLE_SIZE];
    int activeBuckets[TABLE_SIZE];
    Entry entries[MAX_CELL_ENTRIES];
    uint64_t pairSeen[PAIR_WORDS];

    int FindOrCreate( int64_t key, int16_t cx, int16_t cy, int16_t cz );
    void InsertCell( int index, int ix, int iy, int iz );
    void InsertBounds( int index, const Vector::Vector3& minBounds, const Vector::Vector3& maxBounds );

  public:
    static constexpr int MAX_BUCKETS = TABLE_SIZE;

    struct ActiveCell
    {
        int16_t ix, iy, iz;
        int objectCount;
    };

    SpatialGrid( float fCellSize );
    void Clear();
    void Insert( int index, const Vector::Vector3& position, float radius );
    void InsertSwept( int index, const Vector::Vector3& position, const Vector::Vector3& displacement, float radius );
    void GetCandidatePairs( std::vector<std::pair<int, int>>& outPairs );
    float GetCellSize() const
    {
        return cellSize;
    }
    int GetActiveCellCount() const
    {
        return activeBucketCount;
    }
    void GetActiveCells( ActiveCell* outCells, int maxCells ) const;
};
} // namespace CollisionDetection
} // namespace Math
} // namespace SkullbonezCore
